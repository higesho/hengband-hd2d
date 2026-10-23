#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DcssCore.exe を、画面（HengbandHd2d.exe）なしで、プロトコル v1（u32 の長さ＋UTF-8 の JSON）だけで駆動する道具。

    python -u tools/dcss/dc_protocol_driver.py --exe <DcssCore.exe> --scratch <一時フォルダ>     # 起動 → 握手 → 最初のフレームを見せる
    python -u tools/dcss/dc_protocol_driver.py --exe <DcssCore> --scratch <一時フォルダ> --flow   # 握手 → 人物作成 → 数歩 → 保存 → 終了 → 続きから（合否を返す）

Windows でも Linux（WSL を含む）でも動く。Linux では実行ファイルは `DcssCore`（拡張子なし）、データの置き場への結びつけはシンボリックリンク。

検査の部品としても使う（`dc_m0_check.py` は画面を通すので別。こちらは**画面を通らない**確かめ用）。
サンドボックス相当の置き場を作る: `<scratch>/DcssCore.exe`・`<scratch>/dcss/lib/{dat,docs,settings}`（開発ツリーの `dcss/lib` へのジャンクション）・
書き込む `saves`・`morgue` は実のフォルダ。開発ツリーのセーブは触らない。起こしたプロセスだけを PID で止める。
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


IS_WINDOWS = os.name == "nt"
EXE_NAME = "DcssCore.exe" if IS_WINDOWS else "DcssCore"


def make_junction(link: Path, target: Path) -> None:
    """Windows はジャンクション、それ以外はシンボリックリンク。"""
    if link.exists():
        return
    if IS_WINDOWS:
        r = subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], capture_output=True)
        if r.returncode != 0:
            raise RuntimeError(f"ジャンクションを作れません: {link} -> {target}")
    else:
        os.symlink(target, link)


def stage(scratch: Path, exe: Path, fresh: bool = True) -> Path:
    """検査用の置き場を作る。戻り値は実行ファイルのパス。"""
    if fresh and scratch.exists():
        # ジャンクションは先に外す（中身を消さない）
        lib = scratch / "dcss" / "lib"
        for sub in ("dat", "docs", "settings", "tiles"):
            p = lib / sub
            if p.is_symlink() and not IS_WINDOWS:
                p.unlink()
            elif p.exists():
                os.rmdir(p)
        shutil.rmtree(scratch, ignore_errors=True)
    lib = scratch / "dcss" / "lib"
    lib.mkdir(parents=True, exist_ok=True)
    for sub in ("dat", "docs", "settings", "tiles"):
        if (REPO / "dcss" / "lib" / sub).is_dir():   # tiles は板の材料（M2）。無くても遊べる
            make_junction(lib / sub, REPO / "dcss" / "lib" / sub)
    out = scratch / EXE_NAME
    shutil.copy2(exe, out)
    return out


class Core:
    def __init__(self, exe: Path, extra_args: list[str] | None = None, env: dict | None = None):
        e = dict(os.environ)
        e.pop("HENGBAND_DATA_ROOT", None)
        e["HENGBAND_MUTE"] = "1"
        if env:
            e.update(env)
        self.proc = subprocess.Popen([str(exe), "--ui-protocol=stdio", *(extra_args or [])], cwd=str(exe.parent),
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=e, bufsize=0)
        self.msgs: list[dict] = []
        self.lock = threading.Lock()
        self.err = bytearray()
        self.eof = False
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=self._read_err, daemon=True).start()

    def _read_exact(self, n: int) -> bytes:
        buf = b""
        while len(buf) < n:
            d = self.proc.stdout.read(n - len(buf))
            if not d:
                return b""
            buf += d
        return buf

    def _read(self) -> None:
        while True:
            head = self._read_exact(4)
            if not head:
                self.eof = True
                return
            (n,) = struct.unpack("<I", head)
            body = self._read_exact(n)
            if len(body) != n:
                self.eof = True
                return
            try:
                m = json.loads(body.decode("utf-8"))
            except ValueError:
                continue
            with self.lock:
                self.msgs.append(m)

    def _read_err(self) -> None:
        while True:
            d = self.proc.stderr.read(4096)
            if not d:
                return
            self.err += d

    def send(self, obj: dict) -> None:
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.proc.stdin.write(struct.pack("<I", len(body)) + body)
        self.proc.stdin.flush()

    def hello(self, lang: str = "ja") -> None:
        self.send({"t": "hello", "protocol": 1, "ui_name": "dc_driver", "ui_version": "0", "features": []})
        self.send({"t": "ui_state", "lang": lang, "modern_ui": False})

    def input_events(self, *events: dict) -> None:
        self.send({"t": "input_event", "events": list(events)})

    def key(self, ch: str, ctrl: bool = False) -> None:
        self.input_events({"e": "key", "char": ch, "ctrl": ctrl})

    def text(self, s: str) -> None:
        self.input_events({"e": "text", "s": s})

    def move(self, dx: int, dy: int) -> None:
        self.input_events({"e": "move", "dx": dx, "dy": dy})

    def confirm(self) -> None:
        self.input_events({"e": "confirm"})

    def cancel(self) -> None:
        self.input_events({"e": "cancel"})

    def frames(self) -> list[dict]:
        with self.lock:
            return [m for m in self.msgs if m.get("t") == "frame"]

    def latest(self) -> dict | None:
        f = self.frames()
        return f[-1] if f else None

    def wait_frame(self, pred, timeout: float = 30.0, label: str = ""):
        end = time.time() + timeout
        while time.time() < end:
            f = self.latest()
            if f is not None and pred(f):
                return f
            if self.proc.poll() is not None and self.eof:
                break
            time.sleep(0.05)
        raise TimeoutError(f"フレームが期待どおりにならない: {label}\n最後のフレーム: {summarize(self.latest())}\nstderr: {bytes(self.err[-600:]).decode('utf-8', 'replace')}")

    def alive(self) -> bool:
        return self.proc.poll() is None

    def kill(self) -> None:
        if self.alive():
            self.proc.kill()


def lines_of(frame: dict | None) -> list[str]:
    if not frame:
        return []
    return [str(l.get("text_utf8", "")) for l in frame.get("menu_term_lines", [])]


def summarize(frame: dict | None) -> str:
    if not frame:
        return "（フレームなし）"
    keys = ("menu_open", "pre_game_menu", "awaiting_command", "text_input_active", "title_screen", "game_over")
    flags = {k: frame.get(k) for k in keys if frame.get(k)}
    text = " | ".join(lines_of(frame)[:12])
    return f"frame_id={frame.get('frame_id')} flags={flags} cells={len(frame.get('cells', []))} 字: {text[:400]}"


NAME = "Wt"


class Report:
    def __init__(self) -> None:
        self.failed = 0

    def check(self, name: str, ok: bool, detail: str = "") -> bool:
        print(("  OK  " if ok else "  NG  ") + name + ("" if ok or not detail else f"  [{detail}]"), flush=True)
        if not ok:
            self.failed += 1
        return ok


def _is_game(f: dict) -> bool:
    return bool(f.get("awaiting_command")) and len(f.get("cells", [])) > 0


def run_flow(exe: Path, scratch: Path) -> int:
    """握手 → 人物作成（人間・戦士・レイピア）→ 数歩 → 所持品 → 保存して終了 → 起こし直して続きから → 保存して終了。合否を返す。

    窓なし（modern_ui 偽）なので、原作の文字の画面に選択肢のキーを送る。敵・階は `-seed` で固める。
    """
    rep = Report()
    env = {"DCSS_EXTRA_ARGS": "-seed 7"}

    def pos(f: dict) -> tuple:
        return (f.get("cam_x"), f.get("cam_y"))

    print("[1] 起動 → 握手 → 人物作成", flush=True)
    core = Core(exe, env=env)
    try:
        core.hello()
        f = core.wait_frame(lambda f: f.get("text_input_active"), 90, "名前の入力欄")
        ack = [m for m in core.msgs if m.get("t") == "hello_ack"]
        rep.check("握手: hello_ack の core_name=dcss・features に lang-restart", bool(ack) and ack[0].get("core_name") == "dcss" and "lang-restart" in ack[0].get("features", []), str(ack[:1]))
        rep.check("最初のフレームが起動メニュー（名前の入力欄）", "名前を入力" in " ".join(lines_of(f)), summarize(f))
        core.text(NAME)
        core.wait_frame(lambda f: NAME in " ".join(lines_of(f)), 15, "名前が映る")
        core.confirm()
        core.wait_frame(lambda f: "種族" in " ".join(lines_of(f)), 30, "種族の選択")
        core.key("j")  # 人間
        core.wait_frame(lambda f: "職業" in " ".join(lines_of(f)), 30, "職業の選択")
        core.key("a")  # 戦士
        core.wait_frame(lambda f: "武器" in " ".join(lines_of(f)), 30, "武器の選択")
        core.key("a")  # レイピア
        f = core.wait_frame(_is_game, 60, "ゲームに入る")
        known = len(f.get("cells", []))
        rep.check("ゲームに入る（awaiting_command・HUD の名前・HP）", (f.get("hud") or {}).get("name") == NAME and (f.get("hud") or {}).get("hp_max", 0) > 0, str(f.get("hud")))
        rep.check("地図の既知のマスが 5 個以上ある", known >= 5, str(known))

        print("[2] 歩く・所持品", flush=True)
        start = pos(f)
        moved = False
        for k in ("l", "h", "j", "k", "l", "h", "j", "k"):
            core.key(k)
            time.sleep(0.6)
            g = core.latest()
            if g is not None and pos(g) != start:
                moved = True
                break
        rep.check("キーで視点（プレイヤーの位置）が動く", moved, f"{start} → {pos(core.latest())}")
        core.key("i")
        try:
            core.wait_frame(lambda f: "レイピア" in " ".join(lines_of(f)), 15, "所持品")
            rep.check("所持品の画面に原作の持ち物（レイピア）が出る", True)
        except TimeoutError as e:
            rep.check("所持品の画面に原作の持ち物（レイピア）が出る", False, str(e)[:200])
        core.cancel()
        core.wait_frame(_is_game, 15, "コマンド待ちに戻る")

        print("[3] 保存して終了", flush=True)
        core.key("S")
        core.wait_frame(lambda f: "セーブ" in " ".join(lines_of(f)), 15, "保存の確認")
        core.key("Y")
        deadline = time.time() + 30
        while time.time() < deadline and core.proc.poll() is None:
            time.sleep(0.1)
        code = core.proc.poll()
        exits = [m for m in core.msgs if m.get("t") == "exit"]
        rep.check("保存して終了すると、プロセスが終了コード 0 で終わる", code == 0, str(code))
        rep.check("exit メッセージ（code 0）を送る", bool(exits) and exits[-1].get("code") == 0, str(exits[-1:]))
        saves = scratch / "dcss" / "lib" / "saves"
        rep.check("セーブができる（saves/*.cs）", any(p.suffix == ".cs" and p.stat().st_size > 0 for p in saves.glob("*.cs")), str(sorted(x.name for x in saves.glob("*"))[:8]))
        rep.check("標準エラーが空（プロトコルの標準出力に何も混ざらず、起動の失敗も無い）", not core.err.strip(), bytes(core.err[-300:]).decode("utf-8", "replace"))
    finally:
        core.kill()

    print("[4] 起こし直して続きから", flush=True)
    core = Core(exe, extra_args=["--resume"], env=env)
    try:
        core.hello()
        deadline = time.time() + 90
        f = None
        while time.time() < deadline:
            f = core.latest()
            if f is not None and _is_game(f):
                break
            if f is not None and f.get("text_input_active"):
                core.confirm()  # 前回の名前が入っている
                time.sleep(0.5)
            time.sleep(0.2)
        rep.check("セーブから入ると、同じ名前・HP の人物の続き", f is not None and _is_game(f) and (f.get("hud") or {}).get("name") == NAME and (f.get("hud") or {}).get("hp_max", 0) > 0, summarize(f))
        core.key("S")
        core.wait_frame(lambda f: "セーブ" in " ".join(lines_of(f)), 15, "保存の確認")
        core.key("Y")
        deadline = time.time() + 30
        while time.time() < deadline and core.proc.poll() is None:
            time.sleep(0.1)
        rep.check("続きからも保存して終了できる（終了コード 0）", core.proc.poll() == 0, str(core.proc.poll()))
    finally:
        core.kill()
    print("合格" if rep.failed == 0 else f"不合格（{rep.failed} 件）", flush=True)
    return 0 if rep.failed == 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--flow", action="store_true", help="握手 → 人物作成 → 数歩 → 保存 → 終了 → 続きから、を通して合否を返す")
    a = ap.parse_args()
    exe = stage(Path(a.scratch), Path(a.exe))
    if a.flow:
        return run_flow(exe, Path(a.scratch))
    core = Core(exe)
    try:
        core.hello()
        deadline = time.time() + 60
        while time.time() < deadline and core.latest() is None:
            time.sleep(0.1)
        print("握手:", [m for m in core.msgs if m.get("t") == "hello_ack"][:1])
        print("最初のフレーム:", summarize(core.latest()))
        print("kinds:", sorted({m.get("t") for m in core.msgs}))
        print("stderr:", bytes(core.err[-400:]).decode("utf-8", "replace"))
    finally:
        core.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
