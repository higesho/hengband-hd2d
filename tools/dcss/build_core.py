#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DcssCore.exe を組み、実行時のデータ（dcss/lib）を用意する（Windows・MSVC・x64）。

    python -u tools/dcss/build_core.py                      # 生成物 → 組む → データを写す（順に全部）
    python -u tools/dcss/build_core.py --skip-gen           # 生成物（作業フォルダの gen）が済んでいるとき
    python -u tools/dcss/build_core.py --work C:\\短い\\作業 --out-dir <成果物の置き場>

手順の正本は `dcss/adapter/README.md` §2。ここはその 3 つの手順を 1 本にしただけ:
  1. `dcss/adapter/prepare_gen.py --work <W>` — 原作を複製し、組む前の生成物（ヘッダ・タイルの表）を `<W>/gen` に作る（約 40 秒）
  2. MSBuild `VisualStudio/DcssCore/DcssCore.vcxproj`（Release|x64。`/p:DcssGenRoot=<W>/gen`）— 成果物は既定でリポジトリ直下の `DcssCore.exe`
  3. `tools/dcss/prepare_runtime_data.py --work <W>` — 原作の dat・docs・settings を `dcss/lib/` へ写す（追跡しない）

作業フォルダは**短く**する（原作の最長のパスが 118 文字で、深いと tilegen が止まる）。原作の写し（roguelike-cores/upstream/crawl-ja）は書き換えない。
"""
from __future__ import annotations

import argparse
import os
import tempfile
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_WORK = Path(os.environ.get("DCSS_WORK", os.path.join(tempfile.gettempdir(), "dcss_w")))
DEFAULT_MSBUILD = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe")
DEFAULT_PYTHON = Path(sys.executable)  # PyYAML が入った Python で呼ぶこと（prepare_gen.py が要る）

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, cwd=str(REPO))
    if r.returncode != 0:
        raise SystemExit(f"失敗（終了コード {r.returncode}）: {cmd[0]}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", default=str(DEFAULT_WORK), help="prepare_gen.py の作業フォルダ（短い道）")
    ap.add_argument("--skip-gen", action="store_true", help="生成物が済んでいる（<work>/gen がある）ので 1 を飛ばす")
    ap.add_argument("--out-dir", default="", help="DcssCore.exe の置き場（既定: リポジトリ直下）。末尾の \\ は付けても付けなくてもよい")
    ap.add_argument("--msbuild", default=str(DEFAULT_MSBUILD))
    ap.add_argument("--python", default=str(DEFAULT_PYTHON if DEFAULT_PYTHON.is_file() else sys.executable))
    args = ap.parse_args()
    work = Path(args.work)
    if not args.skip_gen or not (work / "gen" / "mon-data.h").is_file():
        run([args.python, "-u", str(REPO / "dcss" / "adapter" / "prepare_gen.py"), "--work", str(work)])
    cmd = [args.msbuild, str(REPO / "VisualStudio" / "DcssCore" / "DcssCore.vcxproj"), "/m", "/nologo", "/v:m",
           "/p:Configuration=Release", "/p:Platform=x64", f"/p:DcssGenRoot={work / 'gen'}"]
    if args.out_dir:
        cmd.append("/p:OutDir=" + str(Path(args.out_dir)) + "\\")
    run(cmd)
    run([sys.executable, "-u", str(REPO / "tools" / "dcss" / "prepare_runtime_data.py"), "--work", str(work)])
    return 0


if __name__ == "__main__":
    sys.exit(main())
