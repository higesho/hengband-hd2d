#!/usr/bin/env python3
"""Drive UmoriaCore.exe over the stdio v1 protocol and print the text screen .

Silent: it never starts the HD2D window.  Saves go to a temporary HENGBAND_DATA_ROOT unless --data-root is given.

    python tools/umoria/umoria_driver.py --keys "<esc>" --keys "n"
"""
from __future__ import annotations

import argparse
import json
import os
import queue
import shutil
import struct
import subprocess
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMED = {"<esc>": 27, "<cr>": 13, "<bs>": 8, "<sp>": 32}


def parse_keys(text: str) -> list[int]:
    keys: list[int] = []
    i = 0
    while i < len(text):
        if text[i] == "<":
            end = text.find(">", i)
            token = text[i:end + 1]
            if token in NAMED:
                keys.append(NAMED[token])
                i = end + 1
                continue
            if token.startswith("<^") and len(token) == 4:
                keys.append(ord(token[2].upper()) - 64)
                i = end + 1
                continue
        keys.append(ord(text[i]))
        i += 1
    return keys


_TABLE: dict[str, str] | None = None


def T(english: str) -> str:
    """検査で比べる字。`UMORIA_LANG=ja` なら訳の表（umoria/lang/ja/*.ja.txt の完全一致）を引く（M5）。"""
    global _TABLE
    if os.environ.get("UMORIA_LANG") != "ja":
        return english
    if _TABLE is None:
        _TABLE = {}
        for path in sorted((ROOT / "umoria" / "lang" / "ja").glob("*.ja.txt")):
            for line in path.read_text(encoding="utf-8").splitlines():
                if line and not line.startswith("#") and "	" in line:
                    en, ja = line.split("\t")[:2]
                    _TABLE.setdefault(en, ja)
    return _TABLE.get(english, english)


def depth(core: "Core") -> int | None:
    """いまの深さ（階。町は 0）。字ではなくフレームの数で見る（言語によらない）。"""
    return ((core.frame or {}).get("hud") or {}).get("depth")


class Core:
    def __init__(self, exe: Path, data_root: Path, keep_history: bool = False):
        # UMORIA_CORE で別のコア（Linux 版など）を検査する。Linux と Android のコアは現在のフォルダーを
        # 「実行ファイルの隣」とみなして umoria/data と町の地図を読む（legacy_os.cpp の exe_dir()）ので、
        # 配布物の初回のデータコピー（linux-data.json）と同じく、データの置き場へ写しておく。
        exe = Path(os.environ.get("UMORIA_CORE", exe))
        if exe.suffix.lower() != ".exe":
            for sub in ("data", "hd2d", "lang"):
                shutil.copytree(ROOT / "umoria" / sub, Path(data_root) / "umoria" / sub, dirs_exist_ok=True)
        self.proc = subprocess.Popen([str(exe), "--ui-protocol=stdio"], cwd=str(data_root),
                                     env={**os.environ, "HENGBAND_DATA_ROOT": str(data_root), "HENGBAND_MUTE": "1", "UMORIA_TEST_TERM_MIRROR": "1"},
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.messages: queue.Queue[dict | None] = queue.Queue()
        self.frame: dict | None = None
        self.exit_code: int | None = None
        #: フレーム以外の届いた知らせ（pad_commands など）。keep_history のときだけ残す。
        self.keep_history = keep_history
        self.history: list[dict] = []
        #: フレームを受け取るたびに呼ぶ（m6 の検査が、画面へ送った字に英語が残っていないかを数える）。
        self.on_frame = None
        threading.Thread(target=self._reader, daemon=True).start()
        self.send({"t": "hello", "protocol": 1, "ui_name": "umoria-driver", "ui_version": "1", "features": []})
        # UMORIA_LANG=ja で日本語の検査（M5）。指定が無ければ ui_state を送らない（英語のまま。M4 までと同じ）。
        # UMORIA_MODERN=1 で新しい UI（モダン UI の窓）を有効にする（実際の画面側と同じ。コマンドの操作の検査で使う）。
        if os.environ.get("UMORIA_LANG") or os.environ.get("UMORIA_MODERN"):
            state = {"t": "ui_state", "lang": os.environ.get("UMORIA_LANG", "en")}
            if os.environ.get("UMORIA_MODERN"):
                state["modern_ui"] = True
            self.send(state)

    def _reader(self) -> None:
        out = self.proc.stdout
        assert out is not None
        while True:
            head = out.read(4)
            if len(head) < 4:
                self.messages.put(None)
                return
            size = struct.unpack("<I", head)[0]
            self.messages.put(json.loads(out.read(size).decode("utf-8")))

    def send(self, value: dict) -> None:
        body = json.dumps(value, separators=(",", ":")).encode("utf-8")
        assert self.proc.stdin is not None
        self.proc.stdin.write(struct.pack("<I", len(body)) + body)
        self.proc.stdin.flush()

    def settle(self, quiet: float = 0.4, limit: float = 10.0) -> None:
        """Read messages until nothing arrives for `quiet` seconds."""
        deadline = time.monotonic() + limit
        while time.monotonic() < deadline:
            try:
                message = self.messages.get(timeout=quiet)
            except queue.Empty:
                return
            if message is None:
                return
            if message.get("t") == "frame":
                self.frame = message
                if self.on_frame is not None:
                    self.on_frame(message)
                continue
            if self.keep_history:
                self.history.append(message)
            if message.get("t") == "exit":
                self.exit_code = int(message.get("code", 0))

    def keys(self, keys: list[int]) -> None:
        self.send({"t": "keys", "keys": keys})
        self.settle()

    def screen(self) -> str:
        if self.frame is None:
            return "(no frame)"
        lines = [line.get("text_utf8", "") for line in self.frame.get("menu_term_lines", [])]
        return "\n".join(f"{i:2}|{text}" for i, text in enumerate(lines))

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.proc.stdin.close()  # type: ignore[union-attr]
                self.proc.wait(timeout=10)
            except Exception:
                self.proc.kill()
                self.proc.wait(timeout=5)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=ROOT / "UmoriaCore.exe")
    parser.add_argument("--data-root", type=Path)
    parser.add_argument("--keys", action="append", default=[], help="one step; printed after each step")
    parser.add_argument("--quiet", action="store_true", help="print only the last screen")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="umoria-driver-") as temp:
        data_root = (args.data_root or Path(temp)).resolve()
        data_root.mkdir(parents=True, exist_ok=True)
        core = Core(args.exe.resolve(), data_root)
        try:
            core.settle(limit=15.0)
            if not args.quiet or not args.keys:
                print("== start\n" + core.screen())
            for i, step in enumerate(args.keys):
                core.keys(parse_keys(step))
                if not args.quiet or i == len(args.keys) - 1:
                    print(f"== {step!r}\n" + core.screen())
            print("exit", core.exit_code, "alive", core.proc.poll() is None)
        finally:
            core.close()


if __name__ == "__main__":
    main()
