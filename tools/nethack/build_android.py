#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""NetHack のコア（英語版・日本語版）と実行時のデータを Android 向けに作る。。

Android では NetHack を画面とは別のプロセスで動かす（決定 D5。 の案 A2）。
コアは実行ファイル（PIE）のまま APK の jniLibs に `lib*.so` の名前で入れ、画面が `posix_spawn` で起こす
（platform/android/process_core.cpp）。

作る物（どれも追跡しない。原作の派生物なので。.gitignore）:
  android/nethack-jni/<abi>/libNetHackCore.so    英語版のコア（arm64-v8a・x86_64。シンボルを落とした実行ファイル）
  android/nethack-jni/<abi>/libNetHackJpCore.so  日本語版のコア
  android/nethack-data/en|ja/                    実行時のデータ（dat/ の写し・makedefs の出力・Unix の sysconf・タイルの絵）
  android/nethack-data/slab/                     板（怪物・アイテム・主人公・石像）
  assets/voxel/nh_*                              地形のブロック（Windows の組み立てと同じ物を作り直す）

データは Linux の組み立て（platform/linux/CMakeLists.txt の make_data.sh）と同じ手順を WSL で走らせて作る。
Windows の組み立ての nethack/data は改行が CRLF で sysconf も Windows 用なので、Android（Unix）には使えない。
makedefs は組み立てる機械の上で走らせる物なので、WSL の gcc で組む。

使い方:
    python tools/nethack/build_android.py               # 全部
    python tools/nethack/build_android.py --cores-only  # コアだけ組み直す
    python tools/nethack/build_android.py --data-only   # データだけ作り直す
要る物: WSL（gcc・sh）、NDK 27.2.12479018 と SDK の CMake 3.22.1、numpy・scipy・Pillow の入った Python。
原作は ../roguelike-cores/build-sources/nethack・nethackjp を読むだけで、何も書き込まない。
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCES = ROOT.parent / "roguelike-cores" / "build-sources"
SDK = Path(r"C:\Android\sdk")
NDK = SDK / "ndk" / "27.2.12479018"
CMAKE = SDK / "cmake" / "3.22.1" / "bin" / "cmake.exe"
NINJA = SDK / "cmake" / "3.22.1" / "bin" / "ninja.exe"
STRIP = NDK / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin" / "llvm-strip.exe"
ABIS = ("arm64-v8a", "x86_64")
#: 言語 -> (原作のフォルダ名, 実行ファイルの名前)。日本語版の名前は "Jp" を含むこと（画面が言語を見分ける。hd2d_app.cpp の language_core_variant）
LANGS = {"en": ("nethack", "NetHackCore"), "ja": ("nethackjp", "NetHackJpCore")}
JNI_OUT = ROOT / "android" / "nethack-jni"
DATA_OUT = ROOT / "android" / "nethack-data"
WORK = ROOT / ".taskwork" / "nethack_android"

#: WSL で走らせる、makedefs を組んでデータを作る手順（platform/linux/CMakeLists.txt の make_data.sh と同じ中身）
MAKE_DATA_SH = r"""
set -e
src=$1
work="$HOME/$2"
out=$3
rm -rf "$work" "$out"
mkdir -p "$work/util" "$out"
gcc -w -O1 -DSHIM_GRAPHICS -DNOTTYGRAPHICS -DNOSHELL -DNOMAIL -I"$src/include" -I"$src/nhlua/lua/src" \
    "$src/util/makedefs.c" "$src/src/monst.c" "$src/src/objects.c" "$src/src/date.c" \
    "$src/src/alloc.c" "$src/util/panic.c" "$src/src/hacklib.c" -lm -o "$work/makedefs"
cp -R "$src/dat" "$work/dat"
# 英語版の原作は CRLF。ゲームが読むデータは LF にする
find "$work/dat" -type f -exec sed -i 's/\r$//' {} +
# makedefs は作業フォルダの ../dat/ へ書く
( cd "$work/util" && for flag in -d -r -h -1 -2 -3; do "$work/makedefs" $flag; done )
cp -R "$work/dat/." "$out/"
sed -e 's/\r$//' -e 's;^GDBPATH=/usr/bin/gdb;#GDBPATH=/usr/bin/gdb;' -e 's;PANICTRACE_GDB=1;PANICTRACE_GDB=0;' \
    "$src/sys/unix/sysconf" > "$out/sysconf"
"""


def wsl_path(p: Path) -> str:
    """C:\\a\\b を /mnt/c/a/b にする。"""
    s = str(p.resolve()).replace("\\", "/")
    return "/mnt/" + s[0].lower() + s[2:]


def cm(p: Path) -> str:
    """CMake に渡すパス（\ はエスケープに読まれるので / にする）。"""
    return str(p).replace("\\", "/")


def run(cmd: list[str], **kw) -> None:
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def build_data(sources: Path) -> None:
    DATA_OUT.mkdir(parents=True, exist_ok=True)
    for lang, (src_name, _exe) in LANGS.items():
        out = DATA_OUT / lang
        # WSL の作業フォルダは WSL の家の下（/mnt/c の上で makedefs を走らせると遅い）。出力だけ Windows 側へ
        run(["wsl", "-e", "sh", "-c", MAKE_DATA_SH, "make_data", wsl_path(sources / src_name),
             "nh-android-work/" + lang, wsl_path(out)])
        if not (out / "data").is_file() or not (out / "sysconf").is_file():
            raise SystemExit(f"データができていない: {out}")
        if b"\r\n" in (out / "data").read_bytes():
            raise SystemExit(f"データの改行が CRLF のまま: {out / 'data'}")
    # タイルの絵（tiles.png・tiles.json）を <out>/<lang>/ に作る
    run([sys.executable, ROOT / "tools" / "nethack" / "build_tiles.py", "--lang", "both",
         "--source-root", sources, "--out", DATA_OUT])
    # 板（DATA_OUT/slab）と地形のブロック（assets/voxel/nh_*。Windows の組み立てと同じ物）
    run([sys.executable, ROOT / "tools" / "nethack" / "build_assets.py", "--tiles", DATA_OUT / "en",
         "--slab-out", DATA_OUT / "slab", "--work", WORK / "assets"])


def build_cores(sources: Path) -> None:
    for abi in ABIS:
        build = WORK / f"build-{abi}"
        run([CMAKE, "-S", ROOT / "tools" / "nethack" / "android", "-B", build, "-G", "Ninja",
             f"-DCMAKE_MAKE_PROGRAM={cm(NINJA)}",
             f"-DCMAKE_TOOLCHAIN_FILE={cm(NDK / 'build' / 'cmake' / 'android.toolchain.cmake')}",
             f"-DANDROID_ABI={abi}", "-DANDROID_PLATFORM=android-33", "-DANDROID_STL=c++_static",
             "-DCMAKE_BUILD_TYPE=Release", f"-DHB_ROOT={cm(ROOT)}", f"-DHB_NH_SOURCES={cm(sources)}"])
        run([CMAKE, "--build", build, "-j", "12"])
        out = JNI_OUT / abi
        out.mkdir(parents=True, exist_ok=True)
        for _lang, (_src, exe) in LANGS.items():
            dst = out / f"lib{exe}.so"
            # シンボルを落とした写しを置く（34 MB が数 MB になる）。Gradle の剥がしに任せず、ここで決める
            run([STRIP, "--strip-all", "-o", dst, build / exe])
            print(f"  {dst}  {dst.stat().st_size / 1e6:.1f} MB", flush=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sources", type=Path, default=DEFAULT_SOURCES, help="原作の根（既定 ../roguelike-cores/build-sources）")
    ap.add_argument("--cores-only", action="store_true")
    ap.add_argument("--data-only", action="store_true")
    args = ap.parse_args()
    for name in ("nethack", "nethackjp"):
        if not (args.sources / name / "src" / "allmain.c").is_file():
            raise SystemExit(f"原作が無い: {args.sources / name}（tools/core_sources/prepare.py を先に）")
    if not args.cores_only:
        build_data(args.sources)
    if not args.data_only:
        build_cores(args.sources)
    print("できました:", JNI_OUT, DATA_OUT, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
