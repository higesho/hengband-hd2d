#!/usr/bin/env python3
"""Build UmoriaCore.exe with the bundled LLVM-MinGW toolchain ."""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "targeting"))
import windows_native_overlay  # noqa: E402  攻撃の効果の観測の呼び出しを足した原作の写しを組む
DEFAULT_UPSTREAM = ROOT.parent / "roguelike-cores" / "upstream"
DEFAULT_SOURCES = ROOT.parent / "roguelike-cores" / "build-sources"
TOOLCHAIN = ROOT / "core-import" / "toolchain" / "bin"
ADAPTER = ROOT / "umoria" / "adapter"
UPSTREAM_COMMIT = "3bf8abc4f76812810e59d738492c7b61fb8f1bb6"
UMORIA_VERSION = "5.7.15"

# 上流の src/ のうち組まないもの（端末は umoria/adapter/um_io.cpp が代わる）。
SKIPPED = {"ui_io.cpp"}

CPP_SHARED = [
    ADAPTER / "um_io.cpp",
    ADAPTER / "um_main.cpp",
    ADAPTER / "um_frame.cpp",
    ADAPTER / "um_town.cpp",
    ADAPTER / "um_lang.cpp",
    ADAPTER / "um_recall_ja.cpp",
    ROOT / "presentation" / "frame" / "frame_codec.cpp",
    ROOT / "presentation" / "frame" / "protocol_messages.cpp",
    ROOT / "presentation" / "frame" / "protocol_transport.cpp",
    ROOT / "presentation" / "bridge" / "input_event_adapter.cpp",
    ROOT / "platform" / "portable" / "legacy_os.cpp",
]


def run(args: list[str]) -> None:
    subprocess.run(args, cwd=ROOT, check=True)


def compile_all(jobs: list[list[str]]) -> None:
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for future in [pool.submit(run, job) for job in jobs]:
            future.result()


def build(source: Path, output: Path, work: Path) -> None:
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    clangxx = str(TOOLCHAIN / "clang++.exe")
    target = "--target=i686-w64-windows-gnu"
    upstream_flags = [clangxx, target, "-std=c++14", "-O2", "-w", "-I", str(source)]
    adapter_flags = [clangxx, target, "-std=c++20", "-O2", "-I", str(source), "-I", str(ADAPTER),
                     "-I", str(ROOT / "third_party" / "cpp" / "include"), "-I", str(ROOT / "presentation"),
                     "-I", str(ROOT / "platform")]
    jobs: list[list[str]] = []
    objects: list[Path] = []
    for src in sorted(source.glob("*.cpp")):
        if src.name in SKIPPED:
            continue
        obj = work / (src.stem + ".o")
        args = upstream_flags.copy()
        if src.name == "main.cpp":
            args.append("-Dmain=um_game_main")
        jobs.append(args + ["-c", str(src), "-o", str(obj)])
        objects.append(obj)
    for src in CPP_SHARED:
        obj = work / (src.stem + ".adapter.o")
        jobs.append(adapter_flags + ["-c", str(src), "-o", str(obj)])
        objects.append(obj)
    compile_all(jobs)
    output.parent.mkdir(parents=True, exist_ok=True)
    run([clangxx, target, "-static", *map(str, objects),
         "-ladvapi32", "-lshell32", "-luser32", "-lshlwapi", "-lws2_32", "-o", str(output)])
    print(f"built {output}")


def install_data(upstream_repo: Path, out_dir: Path) -> None:
    """Write the runtime text files (data/*.txt and LICENSE) beside the exe, as upstream CMake does."""
    data = out_dir / "umoria" / "data"
    if data.exists():
        shutil.rmtree(data)
    data.mkdir(parents=True)
    names = subprocess.check_output(["git", "-C", str(upstream_repo), "ls-tree", "--name-only",
                                     UPSTREAM_COMMIT, "data/"], text=True).split()
    for name in names + ["LICENSE"]:
        raw = subprocess.check_output(["git", "-C", str(upstream_repo), "show", f"{UPSTREAM_COMMIT}:{name}"])
        dest = Path(name).name
        if dest.endswith(".in"):
            dest = dest[:-3]
            raw = raw.replace(b"${umoria_version}", UMORIA_VERSION.encode())
        if dest == "scores.dat":
            continue  # スコアは data_dir() 側に作る（um_main.cpp の configure_paths）
        (data / dest).write_bytes(raw)
    print(f"installed {data}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", type=Path, default=DEFAULT_UPSTREAM)
    parser.add_argument("--source-root", type=Path,
                        default=Path(os.environ.get("HENGBAND_CORE_SOURCE_ROOT", DEFAULT_SOURCES)))
    parser.add_argument("--out-dir", type=Path, default=ROOT)
    parser.add_argument("--data-only", action="store_true",
                        help="only write umoria/data (the Linux/Android packages use it; no compiler needed)")
    args = parser.parse_args()
    if args.data_only:
        install_data(args.upstream.resolve() / "umoria", args.out_dir.resolve())
        return
    subprocess.run([sys.executable, str(ROOT / "tools" / "core_sources" / "prepare.py"),
                    "--upstream", str(args.upstream.resolve()), "--output", str(args.source_root.resolve())],
                   cwd=ROOT, check=True)
    source = windows_native_overlay.prepare(args.source_root.resolve()) / "umoria" / "patched-src"
    out_dir = args.out_dir.resolve()
    build(source, out_dir / "UmoriaCore.exe", ROOT / ".taskwork" / "umoria-win-build")
    install_data(args.upstream.resolve() / "umoria", out_dir)


if __name__ == "__main__":
    main()
