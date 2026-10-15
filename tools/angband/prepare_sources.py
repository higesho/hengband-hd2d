#!/usr/bin/env python3
"""Copy the upstream Angband 4.2.6 runtime data into the tracked angband/lib/ tree.

The patched C sources are no longer produced here: `python tools/core_sources/prepare.py`
reconstructs them (angband/patched-src) under the prepared-source root
(default ../roguelike-cores/build-sources) from range recipes, without any upstream
source text in this repository. This script only copies the game data
(gamedata/customize/screens/help). section 2.
"""
from __future__ import annotations

import argparse
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_UPSTREAM = ROOT.parent / "roguelike-cores" / "upstream"

# Design section 2: the four upstream lib/ subdirectories copied into angband/lib/.
# lib/tiles, lib/fonts, lib/sounds and lib/icons are never copied (design section 1,
# constraint 4: Shockbolt's tiles need separate permission for modification/commercial use).
RUNTIME_DIRS = ["gamedata", "customize", "screens", "help"]


def copy_runtime_data(upstream_root: Path, ui_root: Path) -> None:
    """Copy design section 2's four lib/ subdirectories into angband/lib/ (git-tracked)."""
    src_lib = upstream_root / "angband" / "lib"
    dst_lib = ui_root / "angband" / "lib"
    dst_lib.mkdir(parents=True, exist_ok=True)

    for name in RUNTIME_DIRS:
        src_dir = src_lib / name
        if not src_dir.is_dir():
            raise FileNotFoundError(f"missing upstream lib dir: {src_dir}")
        dst_dir = dst_lib / name
        if dst_dir.exists():
            shutil.rmtree(dst_dir)
        shutil.copytree(src_dir, dst_dir)
        n = sum(1 for p in dst_dir.rglob("*") if p.is_file())
        print(f"angband: lib/{name}: {n} file(s)")

    # lib/tiles, lib/fonts, lib/sounds, lib/icons are intentionally not copied
    # (design section 1 constraint 4). lib/user is a tracked empty marker; the
    # save/user directories a running core actually writes to live under
    # portable::data_dir()/angband (design section 2), not here.
    user_dir = dst_lib / "user"
    user_dir.mkdir(exist_ok=True)
    keep = user_dir / ".gitkeep"
    if not keep.exists():
        keep.write_text("", encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", type=Path, default=DEFAULT_UPSTREAM)
    args = parser.parse_args()
    copy_runtime_data(args.upstream.resolve(), ROOT)


if __name__ == "__main__":
    main()
