#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DCSS コアを Android・Quest（NDK）で組むための前処理（ホストの Windows で走らせる）。

Android の CMake（android/hd2d/src/main/cpp/CMakeLists.txt → tools/core_sources/dcss_android_core.cmake）は、
ホストで走らせる道具（原作の tilegen）を組めない。そこで、組む前に次を済ませて、生成物の置き場 `<work>/gen` を用意する。

  1. dcss/adapter/prepare_gen.py を呼ぶ（Windows と同じ手順。cl で tilegen を組み、ヘッダとタイルの表を作る）。
     原作は ../roguelike-cores/build-sources/dcss（tools/core_sources/prepare.py が作る）。書き換えない。
  2. config.h だけ作り直す。prepare_gen.py の config.h は Windows（MSVC）の確認結果で、fdatasync・strlcpy・mkstemp・usleep が
     「無い」になる。bionic には全部あるので、NDK の clang で原作の util/configure.py を走らせて作る（実行はせず、コンパイルが通るかだけ見る）。
     これを怠ると syscalls.h が自前の usleep を宣言して、bionic のものとぶつかって組めない
     （原作側の記録 ja-tools/ANDROID_PORT.md §3 の「config.h を取り合う」罠と同じ）。
  3. 実行時のデータ（dat・docs・settings）を、prepare_runtime_data.py で `<work>/lib` に写す。
     開発ツリーの dcss/lib には遊んだ跡（saves・morgue）があるので、そこからは写さない。

    python -u tools/dcss/prepare_android_gen.py                  # 既定の作業フォルダ
    python -u tools/dcss/prepare_android_gen.py --refresh        # 原作の写しから作り直す

出力（Android の CMake と素材の組み立てが読む）:
  <work>/gen   生成したヘッダとタイルの表（config.h は Android 用）
  <work>/lib   dat・docs・settings（実行時のデータ）

作業フォルダは短くする（原作の最長のパスが 118 文字で、深いと tilegen が止まる）。
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CORE_SOURCES = Path(os.environ.get("HENGBAND_CORE_SOURCE_ROOT", REPO.parent / "roguelike-cores" / "build-sources"))
DEFAULT_WORK = Path(os.environ.get("DCSS_ANDROID_WORK", REPO.parent / "roguelike-cores" / "dcss-android"))
DEFAULT_NDK = Path(r"C:\Android\sdk\ndk\27.2.12479018")
DEFAULT_PYTHON = Path(sys.executable)  # PyYAML が入った Python で呼ぶこと

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


def run(cmd: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, cwd=str(cwd) if cwd else None)
    if r.returncode != 0:
        raise SystemExit(f"失敗（終了コード {r.returncode}）: {cmd[0]}")


def android_config_h(source: Path, ndk: Path, api: int, work: Path) -> str:
    """NDK の clang で原作の util/configure.py を走らせ、Android 用の config.h の中身を返す。"""
    clang = ndk / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin" / "clang++.exe"
    if not clang.is_file():
        raise SystemExit(f"NDK の clang++ がありません: {clang}")
    tmp = work / "cfg_probe"
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    # configure.py は引数を shlex で割る（posix 流）ので、道は / で書く。コンパイルが通るかだけ見る（-c）。
    cxx = f"{clang.as_posix()} --target=aarch64-linux-android{api} --sysroot={(ndk / 'toolchains/llvm/prebuilt/windows-x86_64/sysroot').as_posix()} -std=gnu++14"
    shutil.copy2(source / "util" / "configure.py", tmp / "configure.py")
    run([sys.executable, "-I", "configure.py", cxx], cwd=tmp)
    return (tmp / "config.h").read_text(encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", default=str(DEFAULT_WORK))
    ap.add_argument("--upstream", default=str(CORE_SOURCES / "dcss"), help="再構成した原作（crawl-ref/ と ja-tools/ を持つ）")
    ap.add_argument("--ndk", default=str(DEFAULT_NDK))
    ap.add_argument("--api", type=int, default=33, help="minSdk（android/hd2d/build.gradle）")
    ap.add_argument("--refresh", action="store_true", help="原作の複製から作り直す")
    args = ap.parse_args()
    work = Path(args.work)
    upstream = Path(args.upstream)
    if not (upstream / "crawl-ref" / "source" / "AppHdr.cc").is_file():
        raise SystemExit(f"原作がありません: {upstream}（python tools/core_sources/prepare.py を先に）")
    work.mkdir(parents=True, exist_ok=True)

    py = str(DEFAULT_PYTHON if DEFAULT_PYTHON.is_file() else sys.executable)
    cmd = [py, "-u", str(REPO / "dcss" / "adapter" / "prepare_gen.py"), "--upstream", str(upstream), "--work", str(work)]
    if args.refresh:
        cmd.append("--refresh")
    run(cmd)

    gen = work / "gen"
    config = android_config_h(upstream / "crawl-ref" / "source", Path(args.ndk), args.api, work)
    if "#undef" in config:
        raise SystemExit("config.h に「無い」が混ざりました（bionic には全部ある想定）:\n" + config)
    (gen / "config.h").write_text(config, encoding="utf-8", newline="\n")
    print("config.h を Android 用に置き換えました:", gen / "config.h", flush=True)

    lib = work / "lib"
    if lib.is_dir():
        shutil.rmtree(lib)  # 前の版にしか無かったファイルを残さない（作業フォルダの中の物だけ）
    run([sys.executable, "-u", str(REPO / "tools" / "dcss" / "prepare_runtime_data.py"), "--work", str(work), "--dest", str(lib)])
    (work / ".android-ready").write_text("ok\n", encoding="utf-8")
    print(f"できました: gen={gen} lib={lib}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
