#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DCSS コアが実行時に読む原作のデータ（dat・docs・settings）を `dcss/lib/` へ写す。

    python -u tools/dcss/prepare_runtime_data.py --work <prepare_gen.py の作業フォルダ>
    python -u tools/dcss/prepare_runtime_data.py --source <crawl-ref のフォルダ>   # 生成物なしで原作を直に写す（dat の生成物が足りない）

タイルの画像ページ（`dcss/assets/atlas`）は `dcss/lib/tiles` へ写す（コアが実行時に板を作る材料）。`dcss/lib/slabs` は実行時に作る板の置き場。
`dcss/lib/` は**追跡しない**（`.gitignore`）。コア（DcssCore.exe）は `<データの根>/dcss/lib`（`HENGBAND_DATA_ROOT`、無ければ実行ファイルの隣）を
原作の `CRAWL_DIR` にして、読むデータ（dat・docs・settings）と書く場所（saves・morgue・db・des）を同じ置き場に持つ。

元は **prepare_gen.py が作る mirror**（`<work>/mirror/crawl-ref`）を勧める: dat の中に生成物（`dat/dlua/tags.lua` など）が入っているため。
原作の写し（`../roguelike-cores/upstream/crawl-ja`）は書き換えない（読むだけ）。
dat の `tiles`（画面用の画像 11 MB）は要らないので写さない。docs の `develop`・`obsolete`・PDF も写さない。
すでに同じ大きさ・更新時刻のファイルがあれば飛ばす（2 回目は速い）。
"""
from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

REPO = Path(__file__).resolve().parents[2]
DEFAULT_UPSTREAM = REPO.parent / "roguelike-cores" / "build-sources" / "dcss"

SKIP = {
    "dat": {"tiles"},
    "docs": {"develop", "obsolete", "quickstart.pdf", "template"},
    "settings": set(),
}


def copy_tree(src: Path, dst: Path, skip: set[str]) -> tuple[int, int]:
    copied = skipped = 0
    for root, dirs, files in os.walk(src):
        rel = Path(root).relative_to(src)
        dirs[:] = [d for d in dirs if not (rel == Path(".") and d in skip)]
        out = dst / rel
        out.mkdir(parents=True, exist_ok=True)
        for name in files:
            if rel == Path(".") and name in skip:
                continue
            s = Path(root) / name
            d = out / name
            try:
                if d.exists() and d.stat().st_size == s.stat().st_size and int(d.stat().st_mtime) >= int(s.stat().st_mtime):
                    skipped += 1
                    continue
            except OSError:
                pass
            shutil.copy2(s, d)
            copied += 1
    return copied, skipped


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", help="prepare_gen.py の作業フォルダ（<work>/mirror/crawl-ref から写す）")
    ap.add_argument("--source", help="crawl-ref のフォルダを直に指す（--work の代わり）")
    ap.add_argument("--dest", default=str(REPO / "dcss" / "lib"), help="写し先（既定 dcss/lib）")
    args = ap.parse_args()
    if args.source:
        ref = Path(args.source)
    elif args.work:
        ref = Path(args.work) / "mirror" / "crawl-ref"
    else:
        ref = DEFAULT_UPSTREAM / "crawl-ref"
        print("注意: 原作の写しを直に読みます（dat の生成物が無い）。通常は --work を指してください", file=sys.stderr)
    dest = Path(args.dest)
    srcs = {"dat": ref / "source" / "dat", "docs": ref / "docs", "settings": ref / "settings"}
    for name, path in srcs.items():
        if not path.is_dir():
            print(f"見つかりません: {path}", file=sys.stderr)
            return 1
    dest.mkdir(parents=True, exist_ok=True)
    for name, path in srcs.items():
        copied, skipped = copy_tree(path, dest / name, SKIP[name])
        print(f"{name}: 写した {copied} 本・飛ばした {skipped} 本  ({path} -> {dest / name})")
    # タイルの画像ページ（怪物・アイテム・プレイヤーの板の材料。tools/dcss/dcss_tile_atlas.py が dcss/assets/atlas に作る）。
    atlas = REPO / "dcss" / "assets" / "atlas"
    if atlas.is_dir():
        copied, skipped = copy_tree(atlas, dest / "tiles", set())
        print(f"tiles: 写した {copied} 本・飛ばした {skipped} 本  ({atlas} -> {dest / 'tiles'})")
    else:
        print(f"注意: {atlas} が無い（怪物・アイテムは字で出る。python tools/dcss/dcss_tile_atlas.py で作る）", file=sys.stderr)
    (dest / "saves").mkdir(exist_ok=True)
    (dest / "morgue").mkdir(exist_ok=True)
    (dest / "slabs").mkdir(exist_ok=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
