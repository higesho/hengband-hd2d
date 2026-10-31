#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""NetHack の板（怪物・アイテム・主人公・石像）と地形のブロックを、原作のタイルの絵から作る（組み立ての一部）。

 の D19〜D25 と §9.6。Windows の `build_windows.py` と Linux の CMake（platform/linux/CMakeLists.txt の
NetHack の節）が、同じこの道具を呼ぶ。原作のタイルの絵は NetHack General Public License の下にあり、作った物はその派生物なので、
**リポジトリでは追跡しない**（板は `/nethack/data/`、ブロックは `assets/voxel/nh_*` が .gitignore に入っている）。

やること（失敗したら終了コード 1 で止まる。上から順に）:
  1. 板: `tiles_to_slabs.py` が `--tiles` の tiles.png・tiles.json から `--slab-out` へ板（NH*.vox・NH*.jsonc）と表（slabs.tsv・slabs.json）を作る。
     2 回目からは変わった板だけ書き直す。
  2. ブロック: `gen_nethack_terrain.py` が同じ絵から `--terrain-out` へ地形のプレハブ（nh_*.vox・nh_*.jsonc）を作る。
     この道具は `gen_nethack_terrain.py` を取り込んで呼び、絵の置き場だけ `--tiles` へ差し替える（Linux の組み立てでは、絵が
     ビルド先 `bin/nethack/data/en` にあり、リポジトリの `nethack/data/en` ではないため）。確認用の「床を持ち上げた」出力は作らない。
  3. 照合: `gen_nethack_terrain_rules.py --check` が、規則（assets/voxel/terrain_prefabs.jsonc の NetHack の節）とコアの地形番号の表
     （nethack/adapter/nh_terrain_ids.inc）が最新で、規則が指すプレハブがそろっていることを確かめる。
     食い違うときは、その道具を引数なしで走らせて作り直し、アダプタとプレハブを一緒に直してから組み直す。

使い方:
    python tools/nethack/build_assets.py                          # Windows の既定: 絵は nethack/data/en、板は nethack/data/slab、ブロックは assets/voxel
    python tools/nethack/build_assets.py --tiles <bin>/nethack/data/en --slab-out <bin>/nethack/data/slab   # Linux の組み立て
    python tools/nethack/build_assets.py --force                  # 板の控えを無視して全部作り直す

numpy・scipy・Pillow の入った Python が要る（この環境では `python`〔3.10〕。`py`〔3.13〕には numpy が無い）。
"""
from __future__ import annotations

import argparse
import contextlib
import io
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

DEFAULT_TILES = REPO / "nethack" / "data" / "en"
DEFAULT_SLAB_OUT = REPO / "nethack" / "data" / "slab"
DEFAULT_TERRAIN_OUT = REPO / "assets" / "voxel"
DEFAULT_WORK = REPO / ".taskwork" / "nethack_build" / "assets"
ADAPTER_INC = REPO / "nethack" / "adapter" / "nh_terrain_ids.inc"


def run(step: str, argv: list[str], timeout: int) -> None:
    started = time.monotonic()
    env = dict(os.environ, PYTHONIOENCODING="utf-8", PYTHONUTF8="1")  # 子の出力を UTF-8 にそろえる（Windows の既定は CP932）
    result = subprocess.run([sys.executable, "-B", *argv], capture_output=True, text=True, encoding="utf-8", errors="replace",
                            timeout=timeout, env=env)
    tail = (result.stdout or "").strip().splitlines()[-6:]
    if result.returncode != 0:
        print(f"[{step}] 失敗（終了コード {result.returncode}）")
        print((result.stdout or "")[-3000:])
        print((result.stderr or "")[-3000:])
        raise SystemExit(1)
    print(f"[{step}] ok（{time.monotonic() - started:.0f} 秒）")
    for line in tail:
        print("   " + line)


def build_terrain(tiles: Path, out: Path, work: Path) -> None:
    """gen_nethack_terrain.py を、絵の置き場だけ差し替えて呼ぶ。"""
    started = time.monotonic()
    import gen_nethack_terrain as G  # noqa: E402

    original = G.set_data_dir
    G.set_data_dir = lambda _path: original(str(tiles))  # main() が固定のリポジトリの場所へ戻すのを防ぐ
    work.mkdir(parents=True, exist_ok=True)
    log = io.StringIO()
    with contextlib.redirect_stdout(log):
        rc = G.main(["--out", str(out), "--report-dir", str(work), "--no-view"])
    if rc != 0:
        print("[terrain] 失敗（gen_nethack_terrain.py が %s を返した）" % rc)
        print(log.getvalue()[-3000:])
        raise SystemExit(1)
    print(f"[terrain] ok（{time.monotonic() - started:.0f} 秒）→ {out}")
    for line in log.getvalue().strip().splitlines()[-2:]:
        print("   " + line)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tiles", type=Path, default=DEFAULT_TILES, help="tiles.png と tiles.json のあるフォルダ（既定 nethack/data/en）")
    ap.add_argument("--slab-out", type=Path, default=DEFAULT_SLAB_OUT, help="板と表の出力先（既定 nethack/data/slab）")
    ap.add_argument("--terrain-out", type=Path, default=DEFAULT_TERRAIN_OUT, help="地形のプレハブの出力先（既定 assets/voxel）")
    ap.add_argument("--work", type=Path, default=DEFAULT_WORK, help="作業用（地形の作った物の一覧）")
    ap.add_argument("--force", action="store_true", help="板の控えを無視して全部作り直す")
    ap.add_argument("--skip-slabs", action="store_true")
    ap.add_argument("--skip-terrain", action="store_true")
    args = ap.parse_args()

    tiles = args.tiles.resolve()
    if not (tiles / "tiles.png").is_file() or not (tiles / "tiles.json").is_file():
        raise SystemExit(f"{tiles} に tiles.png と tiles.json が無い。先に build_tiles.py を走らせること")

    if not args.skip_slabs:
        slab_argv = [str(HERE / "tiles_to_slabs.py"), "--tiles", str(tiles), "--out", str(args.slab_out.resolve())]
        if args.force:
            slab_argv.append("--force")
        run("slabs", slab_argv, timeout=3600)
    if not args.skip_terrain:
        build_terrain(tiles, args.terrain_out.resolve(), args.work.resolve())

    rules_argv = [str(HERE / "gen_nethack_terrain_rules.py"), "--check", "--inc", str(ADAPTER_INC)]
    if args.terrain_out.resolve() == DEFAULT_TERRAIN_OUT.resolve():
        rules_argv.append("--files")  # 規則が指すプレハブが assets/voxel にそろっているか（画面が読む場所）
    run("rules", rules_argv, timeout=300)
    print("NetHack の板とブロック: ok")


if __name__ == "__main__":
    main()
