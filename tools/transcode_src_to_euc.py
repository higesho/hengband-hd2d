#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""コア（``src/``）を UTF-8 から EUC-JP へ書き写した変換ツリーを作る。

なぜ要るか
----------
コアは日本語の内部表現を EUC-JP とみなして動く（``-DEUC``。``iskanji`` は
``iseuckanji``）。ところが**ソースは UTF-8 で書かれている**。Linux の autotools 版は
この食い違いを GCC の ``-fexec-charset=euc-jp-ms`` で吸収している
（``configure.ac:178`` / ``src/Makefile.am:1463``）。

**Android の clang（NDK 27 / clang 18）には ``-fexec-charset`` が実質無い**
（``UTF-8`` と ``IBM-1047`` しか受け付けない。実測でエラーになる）。そのまま組むと
文字列リテラルだけが UTF-8 のまま残り、実行時に EUC として解釈されて化ける
（キャラクター作成の説明文が 1 文字おきに豆腐になる、で発覚）。

そこで **コンパイラの代わりにここで書き換える**。やることは ``-fexec-charset`` と同じ。

なぜ EUC なら安全か
-------------------
EUC-JP の 2〜3 バイト目は必ず ``0x8E`` / ``0x8F`` / ``0xA1..0xFE`` で、**ASCII と重ならない**。
``\\`` や ``"`` に化けるバイトが出ないので、リテラルの区切りが壊れない
（Shift-JIS だと 2 バイト目に ``0x5C`` が出るためこの手は使えない）。
clang はリテラル中の非 UTF-8 バイトを警告つきで**そのまま通す**ので、これで狙いどおりになる。

対象外
------
``presentation/`` ``ui/`` ``platform/`` は書き換えない。あちらのリテラルは
**UTF-8 のまま描画へ渡る**（``ui`` は UTF-8 を直接描く）ので、EUC にすると逆に化ける。

使い方::

    python tools/transcode_src_to_euc.py --out android/build-src-euc
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

import sys as _core_sys
from pathlib import Path as _CorePath
_core_sys.path.insert(0, str(_CorePath(__file__).resolve().parents[0]))
from core_sources.paths import core_source as _core_source


REPO = Path(__file__).resolve().parent.parent
SRC = _core_source("hengband")

# 中身を書き換える対象（テキストのソース）。それ以外はバイト列のまま複製する。
TEXT_SUFFIXES = {".cpp", ".cc", ".c", ".h", ".hpp", ".inc"}


def log(message: str) -> None:
    print(f"[transcode-euc] {message}", flush=True)


def transcode_bytes(data: bytes, rel: Path, report: list[str]) -> bytes:
    """UTF-8 のバイト列を EUC-JP へ。EUC-JP に無い文字は `?` にして記録する。"""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        # すでに UTF-8 でない（＝書き換え済みか、そもそもバイナリ）。触らない。
        return data

    try:
        return text.encode("euc_jp")
    except UnicodeEncodeError:
        # 1 文字ずつ落とす。落ちるのはたいてい注釈の記号（① ～ ㎡ など）。
        out = bytearray()
        dropped: set[str] = set()
        for ch in text:
            try:
                out += ch.encode("euc_jp")
            except UnicodeEncodeError:
                out += b"?"
                dropped.add(ch)
        report.append(f"{rel.as_posix()}: EUC-JP に無い文字を ? へ: {''.join(sorted(dropped))}")
        return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", required=True, help="変換ツリーを作る場所")
    parser.add_argument("--quiet", action="store_true", help="1 ファイルずつの報告を出さない")
    parser.add_argument("--extra", action="append", default=[],
                        help="src/ 以外で追加変換するリポジトリ相対パス（複数可）。"
                             "HD2D 版のコア入口 platform/windows/core_main.cpp が使う"
                             "（_(\"(緊急セーブ)\") がセーブへ書かれるため。ANDROID_HD2D_PORT.md §5）")
    args = parser.parse_args()

    out_root = Path(args.out).resolve()
    dst_src = out_root / "src"
    dst_src.mkdir(parents=True, exist_ok=True)

    if not SRC.is_dir():
        sys.exit(f"コアがありません: {SRC}")

    report: list[str] = []
    written = 0
    skipped = 0

    for src_path in sorted(SRC.rglob("*")):
        if not src_path.is_file():
            continue
        rel = src_path.relative_to(SRC)
        dst_path = dst_src / rel
        dst_path.parent.mkdir(parents=True, exist_ok=True)

        if src_path.suffix.lower() in TEXT_SUFFIXES:
            data = transcode_bytes(src_path.read_bytes(), rel, report)
        else:
            data = src_path.read_bytes()

        # 中身が同じなら触らない（触るとその TU が毎回リビルドされる）。
        if dst_path.exists() and dst_path.read_bytes() == data:
            skipped += 1
            continue
        dst_path.write_bytes(data)
        written += 1

    # 変換ツリーに取り残された古いファイルを消す（src から消えたファイルを拾い続けない）。
    removed = 0
    for stale in sorted(dst_src.rglob("*"), reverse=True):
        rel = stale.relative_to(dst_src)
        origin = SRC / rel
        if origin.exists():
            continue
        if stale.is_file():
            stale.unlink()
            removed += 1
        elif stale.is_dir():
            try:
                stale.rmdir()
            except OSError:
                pass

    # --extra: src/ 以外の名指しの追加分。変換ツリーの同じ相対位置へ写す。
    # 掃除（stale 削除）は src/ 配下しか見ないので、ここで写した分は名簿がすべて
    # （消したくなったら変換ツリーごと作り直す）。
    for extra in args.extra:
        src_path = REPO / extra
        if not src_path.is_file():
            sys.exit(f"--extra が見つかりません: {src_path}")
        rel = Path(extra)
        dst_path = out_root / rel
        dst_path.parent.mkdir(parents=True, exist_ok=True)
        if src_path.suffix.lower() in TEXT_SUFFIXES:
            data = transcode_bytes(src_path.read_bytes(), rel, report)
        else:
            data = src_path.read_bytes()
        if dst_path.exists() and dst_path.read_bytes() == data:
            skipped += 1
            continue
        dst_path.write_bytes(data)
        written += 1

    if report and not args.quiet:
        for line in report:
            log(line)
    log(f"更新 {written} / 据置 {skipped} / 削除 {removed}  -> {dst_src}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
