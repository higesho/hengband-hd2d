#!/usr/bin/env python3
"""コアが送る `pad_commands`（命令の表）の番号 `id` が、画面の割り当てで使える値かを全コアで確かめる。

背景（2026-10-09）: 変愚・短愚の表を作る `presentation/bridge/pad_command_table.cpp` が番号を 0 から
振っていたため、先頭の「使う(m)」が 0 になった。画面側は 0 を「割り当て無し」
（`hd2d/ui/key_binds.h` の `kActionNone`）として扱うので、操作の割り当ての一覧で見出し扱いになり、
キーにもゲームパッドにも割り当てられなかった。番号は cfg の `key_bind=`・`pad_bind=` に書かれるので、
0 を使う命令が 1 つでもあると、その命令だけ割り当てが効かない。

見るもの（コアごと）:
  * 命令が 1 件以上届く（届かないコアは「未確認」。失敗にはしない）
  * すべての `id` が 1 以上（0 と負の値は画面側の予約: 0 = 割り当て無し、負 = 画面自身の操作）
  * `id` が重複しない
  * `id` が 16 ビット符号なしに収まる（`PadCommandEntry::id` は `uint16_t`）

コアを `--ui-protocol=stdio` で起こし、hello だけ送って `pad_commands` を拾う（ゲームには入らない）。
データの置き場は作業フォルダの写しで、開発ツリーのセーブには触らない。

    python tools/hd2d_verify/pad_command_ids_check.py all
    python tools/hd2d_verify/pad_command_ids_check.py heng,tang --bin-dir D:/somewhere/core

--bin-dir を省くと開発ツリー直下の実行ファイルを使う（古いことがあるので、直した直後は組んだ先を指す）。
終了コード 0 = 失敗なし（未確認は失敗にしない）。
"""
from __future__ import annotations

import argparse
import queue
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import inventory_actions_audit as ia  # noqa: E402

#: 検査する Windows 版のコア。名前は `ia.CORES` の鍵に合わせる（誕生まで進めないので職業違いは要らない）。
CORE_NAMES = ["heng", "tang", "genso", "silq", "frox", "angband", "rogue2", "rogue36", "rogue54"]


def fetch_pad_commands(spec: "ia.CoreSpec", root: Path, work: Path, wait: float) -> list[dict] | None:
    """hello を送り、最初に届いた pad_commands の entries を返す。届かなければ None。"""
    core = ia.Core(spec, root, work / f"{spec.name}.err.log")
    try:
        core.send({"t": "hello", "protocol": 1, "ui_name": "pad-command-ids-check", "ui_version": "1", "features": []})
        core.send({"t": "ui_state", "modern_ui": True, "targeting_ui": True, "lang": "ja", "view_cells": {"w": 48, "h": 40}})
        end = time.monotonic() + wait
        while time.monotonic() < end and not core.eof:
            try:
                msg = core.q.get(timeout=0.2)
            except queue.Empty:
                continue
            if msg is None:
                core.eof = True
                break
            if msg.get("t") == "pad_commands":
                return list(msg.get("entries") or [])
        return None
    finally:
        core.close()


def judge(entries: list[dict]) -> list[str]:
    """問題の説明を返す（空なら合格）。"""
    problems: list[str] = []
    seen: dict[int, str] = {}
    for e in entries:
        ident = e.get("id")
        label = e.get("label_utf8", "?")
        if not isinstance(ident, int):
            problems.append(f"id が整数でない: {label!r}={ident!r}")
            continue
        if ident < 1:
            problems.append(f"id={ident} は予約値（0=割り当て無し・負=画面の操作）: {label!r}")
        if ident > 0xFFFF:
            problems.append(f"id={ident} が 16 ビットに収まらない: {label!r}")
        if ident in seen:
            problems.append(f"id={ident} が重複: {seen[ident]!r} と {label!r}")
        else:
            seen[ident] = label
    return problems


def main() -> int:
    for stream in (sys.stdout, sys.stderr):  # パイプ越しでも日本語が化けないように
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap =argparse.ArgumentParser()
    ap.add_argument("cores", help="comma list or 'all': " + ",".join(CORE_NAMES))
    ap.add_argument("--bin-dir", type=Path, default=None, help="コア実行ファイルのあるフォルダ（省略時は開発ツリー直下）")
    ap.add_argument("--wait", type=float, default=25.0, help="1 コアあたり pad_commands を待つ秒数")
    a = ap.parse_args()
    names = CORE_NAMES if a.cores == "all" else a.cores.split(",")
    fails = 0
    with tempfile.TemporaryDirectory(prefix="pad-ids-") as tmp:
        work = Path(tmp)
        for name in names:
            spec = ia.CORES[name]
            if a.bin_dir is not None:
                # 絶対パスを渡すと `REPO / spec.exe` はその絶対パスになる。
                spec = ia.CoreSpec(**{**spec.__dict__, "exe": str(a.bin_dir.resolve() / spec.exe)})
            root = work / f"root-{name}"
            root.mkdir(parents=True, exist_ok=True)
            ia.build_root(spec, root)
            try:
                entries = fetch_pad_commands(spec, root, work, a.wait)
            except Exception as exc:  # noqa: BLE001
                print(f"FAIL {name}: 起動・受信に失敗: {exc}", flush=True)
                fails += 1
                continue
            if not entries:
                print(f"未確認 {name}: pad_commands が届かなかった（{a.wait:.0f} 秒）", flush=True)
                continue
            problems = judge(entries)
            ids = [e["id"] for e in entries if isinstance(e.get("id"), int)]
            if problems:
                fails += 1
                print(f"FAIL {name}: {len(entries)} 件（id {min(ids)}..{max(ids)}）", flush=True)
                for p in problems:
                    print(f"    {p}", flush=True)
            else:
                print(f"PASS {name}: {len(entries)} 件（id {min(ids)}..{max(ids)}、すべて 1 以上で重複なし）", flush=True)
    print(f"\n失敗 {fails} 件")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
