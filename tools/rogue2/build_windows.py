"""RogueClone2のWindowsコアを、遊ぶ人インポートと同じLLVM-MinGWで組む。"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "targeting"))
import windows_native_overlay  # noqa: E402  攻撃の効果の観測の呼び出しを足した原作の写しを組む
DEFAULT_SOURCE_ROOT = ROOT.parent / "roguelike-cores" / "build-sources"

UPSTREAM_C = [
    "display.c", "hit.c", "init.c", "invent.c", "level.c", "machdep.c",
    "main.c", "message.c", "monster.c", "move.c", "object.c", "pack.c",
    "play.c", "random.c", "ring.c", "room.c", "save.c", "score.c",
    "spechit.c", "throw.c", "trap.c", "use.c", "zap.c",
]

ADAPTER_C = [
    ROOT / "rogue2/adapter/r2_curses.c",
    ROOT / "rogue2/adapter/r2_compat_iconv.c",
    ROOT / "rogue2/adapter/r2_persist.c",
    ROOT / "rogue2/adapter/r2_shim.c",
]

ADAPTER_CPP = [
    ROOT / "rogue2/adapter/r2_main.cpp",
    ROOT / "rogue2/adapter/r2_frame.cpp",
    ROOT / "presentation/frame/frame_codec.cpp",
    ROOT / "presentation/frame/protocol_messages.cpp",
    ROOT / "presentation/frame/protocol_transport.cpp",
    ROOT / "presentation/bridge/input_event_adapter.cpp",
    ROOT / "platform/portable/legacy_os.cpp",
]


def run(args: list[str]) -> None:
    result = subprocess.run(args)
    if result.returncode:
        raise SystemExit(result.returncode)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(os.environ.get("HENGBAND_CORE_SOURCE_ROOT", DEFAULT_SOURCE_ROOT)),
    )
    parser.add_argument("--output", type=Path, default=ROOT / "Rogue2Core.exe")
    parser.add_argument("--work", type=Path, default=ROOT / "scratch_old/rogue2/windows")
    args = parser.parse_args()

    source_root = args.source_root.resolve()
    patched_source = source_root / "rogue2" / "patched-src"
    # 日本語のメッセージ表 rogue2/mesg（実行時にコアが読む。原作の mesg_J の写しで、リポジトリには置かない）。
    message = ROOT / "rogue2" / "mesg"
    if not patched_source.is_dir():
        subprocess.run([sys.executable, str(ROOT / "tools" / "core_sources" / "prepare.py"),
                        "--output", str(source_root)], cwd=ROOT, check=True)
    if not patched_source.is_dir():
        raise SystemExit(f"RogueClone2 prepared source is missing: {patched_source} (run tools/core_sources/prepare.py)")
    # 攻撃の効果の観測の呼び出しを足した写しを組む（CMake の Linux・Android と同じ）。
    patched_source = windows_native_overlay.prepare(source_root) / "rogue2" / "patched-src"
    if not message.is_file():
        # 原作のデータはリポジトリに無いので、固定版の原作から写す（--upstream か HENGBAND_UPSTREAM_ROOT で場所を変えられる）。
        subprocess.run([sys.executable, str(ROOT / "tools" / "core_sources" / "prepare_original_data.py")], cwd=ROOT, check=True)
    if not message.is_file():
        raise SystemExit(f"RogueClone2 Japanese message file is missing: {message} (run tools/core_sources/prepare_original_data.py)")

    tool_dir = ROOT / "core-import/toolchain/bin"
    cc = tool_dir / "clang.exe"
    cxx = tool_dir / "clang++.exe"
    if not cc.is_file() or not cxx.is_file():
        raise SystemExit("Run the core import toolchain preparation first.")

    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    for old in work.glob("*.o"):
        old.unlink()

    compat = ROOT / "rogue2/adapter/compat"
    adapter = ROOT / "rogue2/adapter"
    third_party = ROOT / "third_party/cpp/include"

    common_c = [
        str(cc), "--target=i686-w64-windows-gnu", "-std=gnu11", "-O1", "-w",
        "-DJAPAN", "-DTOPSCO", "-D_CRT_SECURE_NO_WARNINGS",
        f"-I{compat}", f"-I{adapter}", f"-I{patched_source}",
        "-include", "r2_build_config.h",
    ]
    objects: list[Path] = []
    for index, name in enumerate(UPSTREAM_C):
        src = patched_source / name
        if not src.is_file():
            raise SystemExit(f"RogueClone2 source file is missing: {src}")
        obj = work / f"up_{index:02d}_{src.stem}.o"
        extra = ["-Dmain=r2_game_main"] if name == "main.c" else []
        run([*common_c, *extra, "-c", str(src), "-o", str(obj)])
        objects.append(obj)

    for index, src in enumerate(ADAPTER_C):
        obj = work / f"adapter_c_{index:02d}_{src.stem}.o"
        run([*common_c, "-c", str(src), "-o", str(obj)])
        objects.append(obj)

    common_cpp = [
        str(cxx), "--target=i686-w64-windows-gnu", "-std=c++20", "-O1", "-w",
        "-D_CRT_SECURE_NO_WARNINGS", f"-I{adapter}", f"-I{compat}", f"-I{patched_source}",
        f"-I{ROOT / 'presentation'}", f"-I{ROOT / 'platform'}", f"-I{third_party}",
    ]
    for index, src in enumerate(ADAPTER_CPP):
        obj = work / f"adapter_cpp_{index:02d}_{src.stem}.o"
        run([*common_cpp, "-c", str(src), "-o", str(obj)])
        objects.append(obj)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    run([
        str(cxx), "--target=i686-w64-windows-gnu", "-static",
        *map(str, objects), "-lws2_32", "-o", str(args.output.resolve()),
    ])

    print(f"built {args.output.resolve()}")
    print(f"source {patched_source}")
    print(f"message {message}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
