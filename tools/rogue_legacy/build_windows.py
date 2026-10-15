#!/usr/bin/env python3
"""Build the Rogue 3.x and 5.x HD2D cores with the bundled LLVM-MinGW toolchain."""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "targeting"))
import windows_native_overlay  # noqa: E402  攻撃の効果の観測の呼び出しを足した原作の写しを組む
DEFAULT_UPSTREAM = ROOT.parent / "roguelike-cores" / "upstream"
DEFAULT_OUTPUT = ROOT.parent / "roguelike-cores" / "build-sources"
CLANG = ROOT / "core-import" / "toolchain" / "bin" / "clang.exe"
CLANGXX = ROOT / "core-import" / "toolchain" / "bin" / "clang++.exe"
COMPAT = ROOT / "rogue_legacy" / "adapter" / "compat"
ADAPTER = ROOT / "rogue_legacy" / "adapter"

R36 = [
    "vers.c", "armor.c", "chase.c", "command.c", "daemon.c", "daemons.c", "fight.c", "init.c", "io.c",
    "list.c", "main.c", "mdport.c", "misc.c", "monsters.c", "move.c", "newlevel.c", "options.c", "pack.c",
    "passages.c", "potions.c", "rings.c", "rip.c", "rooms.c", "save.c", "scrolls.c", "state.c", "sticks.c",
    "things.c", "weapons.c", "wizard.c", "xcrypt.c",
]
R54 = [
    "vers.c", "extern.c", "armor.c", "chase.c", "command.c", "daemon.c", "daemons.c", "fight.c", "init.c",
    "io.c", "list.c", "mach_dep.c", "main.c", "mdport.c", "misc.c", "monsters.c", "move.c", "new_level.c",
    "options.c", "pack.c", "passages.c", "potions.c", "rings.c", "rip.c", "rooms.c", "save.c", "scrolls.c",
    "state.c", "sticks.c", "things.c", "weapons.c", "wizard.c", "xcrypt.c",
]

CPP_SHARED = [
    ROOT / "rogue_legacy" / "adapter" / "rl_main.cpp",
    ROOT / "rogue_legacy" / "adapter" / "rl_frame.cpp",
    ROOT / "presentation" / "frame" / "frame_codec.cpp",
    ROOT / "presentation" / "frame" / "protocol_messages.cpp",
    ROOT / "presentation" / "frame" / "protocol_transport.cpp",
    ROOT / "presentation" / "bridge" / "input_event_adapter.cpp",
    ROOT / "platform" / "portable" / "legacy_os.cpp",
]


def run(args: list[str]) -> None:
    subprocess.run(args, cwd=ROOT, check=True)


def build(core: str, source: Path, output: Path, work: Path) -> None:
    if core == "rogue36":
        cfiles, entry, core_name, version, score = R36, "r36_game_main", "Rogue 3.6", "3.6.4", "rogue36.scr"
        generation = 36
    else:
        cfiles, entry, core_name, version, score = R54, "r54_game_main", "Rogue 5.4", "5.4.4", "rogue54.scr"
        generation = 54
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    common_c = [
        str(CLANG), "--target=i686-w64-windows-gnu", "-std=gnu11", "-O2", "-w",
        f"-DRL_GENERATION={generation}",
        "-include", str(COMPAT / ("r36_build_config.h" if core == "rogue36" else "r54_build_config.h")),
        "-I", str(COMPAT), "-I", str(ADAPTER), "-I", str(source),
    ]
    objects: list[Path] = []
    for filename in cfiles:
        obj = work / (Path(filename).stem + ".o")
        args = common_c.copy()
        if filename == "main.c":
            args.append(f"-Dmain={entry}")
        args += ["-c", str(source / filename), "-o", str(obj)]
        run(args)
        objects.append(obj)

    for adapter_c in ["rl_curses.c", "rl_i18n.c", "rl_exit.c", "rl_inventory.c", "rl_sound.c"]:
        obj = work / (Path(adapter_c).stem + ".o")
        run(common_c + ["-c", str(ADAPTER / adapter_c), "-o", str(obj)])
        objects.append(obj)

    cpp_flags = [
        str(CLANGXX), "--target=i686-w64-windows-gnu", "-std=c++20", "-O2",
        f"-DRL_GENERATION={generation}",
        f"-DRL_CORE_NAME=\"{core_name}\"", f"-DRL_CORE_VERSION=\"{version}\"", f"-DRL_GAME_MAIN={entry}",
        "-I", str(ADAPTER), "-I", str(COMPAT), "-I", str(source),
        "-I", str(ROOT / "third_party" / "cpp" / "include"), "-I", str(ROOT / "presentation"),
        "-I", str(ROOT / "platform"),
    ]
    for src in CPP_SHARED:
        obj = work / (src.stem + ".cpp.o")
        run(cpp_flags + ["-c", str(src), "-o", str(obj)])
        objects.append(obj)

    output.parent.mkdir(parents=True, exist_ok=True)
    run([str(CLANGXX), "--target=i686-w64-windows-gnu", "-static", *map(str, objects),
         "-ladvapi32", "-lshell32", "-luser32", "-lshlwapi", "-lws2_32", "-o", str(output)])
    print(f"built {output}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", choices=["rogue36", "rogue54", "all"], default="all")
    parser.add_argument("--upstream", type=Path, default=DEFAULT_UPSTREAM)
    parser.add_argument("--source-root", type=Path, default=Path(os.environ.get("HENGBAND_CORE_SOURCE_ROOT", DEFAULT_OUTPUT)),
        help="prepared-source root made by tools/core_sources/prepare.py")
    parser.add_argument("--out-dir", type=Path, default=ROOT)
    args = parser.parse_args()
    names = ["rogue36", "rogue54"] if args.core == "all" else [args.core]
    subprocess.run([sys.executable, str(ROOT / "tools" / "core_sources" / "prepare.py"),
                    "--upstream", str(args.upstream.resolve()), "--output", str(args.source_root.resolve())],
                   cwd=ROOT, check=True)
    overlay = windows_native_overlay.prepare(args.source_root.resolve())
    for name in names:
        src = overlay / name / "patched-src"
        exe = "Rogue36Core.exe" if name == "rogue36" else "Rogue54Core.exe"
        build(name, src, (args.out_dir / exe).resolve(), ROOT / ".taskwork" / (name + "-win-build"))


if __name__ == "__main__":
    main()
