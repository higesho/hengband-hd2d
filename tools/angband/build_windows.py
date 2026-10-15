#!/usr/bin/env python3
"""Build AngbandCore.exe with the bundled LLVM-MinGW toolchain.

Follows tools/rogue_legacy/build_windows.py: the upstream Angband 4.2.6 .c
files are compiled with warnings silenced (-w); the angband/adapter/*.c(pp)
files and the shared presentation/ TUs are compiled with warnings on (design
 section 3: "アダプタ側は警告を黙らせない").
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

from prepare_sources import DEFAULT_UPSTREAM, copy_runtime_data

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT.parent / "roguelike-cores" / "build-sources"
CLANG = ROOT / "core-import" / "toolchain" / "bin" / "clang.exe"
CLANGXX = ROOT / "core-import" / "toolchain" / "bin" / "clang++.exe"
ADAPTER = ROOT / "angband" / "adapter"

# design section 3: src/*.c minus main-*.c, snd-*.c,
# wiz-stats.c is 150 files , but that count
# still includes main.c textually. Design section 3 separately excludes main.c
# itself ("main.c も組まない"): an_bootstrap.c replays main.c's startup sequence
# instead, so only 149 files are actually compiled here. src/borg/ and
# src/tests/ are subdirectories and are never produced by tools/core_sources/prepare.py
# (only *.c/*.h directly under src/ are copied), so they are excluded implicitly.
CORE_C_FILES = [
    "buildid.c", "cave-map.c", "cave-square.c", "cave-view.c", "cave.c", "cmd-cave.c", "cmd-core.c",
    "cmd-misc.c", "cmd-obj.c", "cmd-pickup.c", "cmd-spoil.c", "cmd-wizard.c", "datafile.c", "debug.c",
    "effect-handler-attack.c", "effect-handler-general.c", "effects-info.c", "effects.c", "game-event.c",
    "game-input.c", "game-world.c", "gen-cave.c", "gen-chunk.c", "gen-monster.c", "gen-room.c", "gen-util.c",
    "generate.c", "grafmode.c", "guid.c", "init.c", "load.c", "message.c", "mon-attack.c", "mon-blows.c",
    "mon-desc.c", "mon-group.c", "mon-init.c", "mon-list.c", "mon-lore.c", "mon-make.c", "mon-move.c",
    "mon-msg.c", "mon-predicate.c", "mon-spell.c", "mon-summon.c", "mon-timed.c", "mon-util.c", "obj-chest.c",
    "obj-curse.c", "obj-desc.c", "obj-gear.c", "obj-ignore.c", "obj-info.c", "obj-init.c", "obj-knowledge.c",
    "obj-list.c", "obj-make.c", "obj-pile.c", "obj-power.c", "obj-properties.c", "obj-randart.c",
    "obj-slays.c", "obj-tval.c", "obj-util.c", "option.c", "parser.c", "player-attack.c", "player-birth.c",
    "player-calcs.c", "player-class.c", "player-history.c", "player-path.c", "player-properties.c",
    "player-quest.c", "player-race.c", "player-spell.c", "player-timed.c", "player-util.c", "player.c",
    "project-feat.c", "project-mon.c", "project-obj.c", "project-player.c", "project.c", "randname.c",
    "save-charoutput.c", "save.c", "savefile.c", "score-util.c", "score.c", "sound-core.c", "source.c",
    "store.c", "target.c", "trap.c", "ui-birth.c", "ui-command.c", "ui-context.c", "ui-curse.c", "ui-death.c",
    "ui-display.c", "ui-effect.c", "ui-entry-combiner.c", "ui-entry-renderers.c", "ui-entry.c",
    "ui-equip-cmp.c", "ui-event.c", "ui-game.c", "ui-help.c", "ui-history.c", "ui-init.c", "ui-input.c",
    "ui-keymap.c", "ui-knowledge.c", "ui-map.c", "ui-menu.c", "ui-mon-list.c", "ui-mon-lore.c",
    "ui-obj-list.c", "ui-object.c", "ui-options.c", "ui-output.c", "ui-player-properties.c", "ui-player.c",
    "ui-prefs.c", "ui-score.c", "ui-signals.c", "ui-spell.c", "ui-spoil.c", "ui-store.c", "ui-target.c",
    "ui-term.c", "ui-visuals.c", "ui-wizard.c", "wiz-debug.c", "wiz-spoil.c", "z-bitflag.c", "z-color.c",
    "z-dice.c", "z-expression.c", "z-file.c", "z-form.c", "z-quark.c", "z-queue.c", "z-rand.c",
    "z-textblock.c", "z-type.c", "z-util.c", "z-virt.c",
]

ADAPTER_C_FILES = ["an_bootstrap.c", "an_null_term.c", "an_wiz_stats_stub.c", "an_map_shim.c",
                    "an_native_inventory.c", "an_text_hooks.c", "an_pad_shim.c", "an_ui_shim.c",
                    "an_targeting.c", "an_town_rng.c", "an_town_map.c", "an_profile.c", "an_fx.c",
                    # （日本語層）: 全角 2 セル・名前の組み立て。
                    "an_wide.c", "an_name_ja.c", "an_lore_ja.c"]

CPP_SHARED = [
    ROOT / "angband" / "adapter" / "an_main.cpp",
    ROOT / "angband" / "adapter" / "an_frame.cpp",
    ROOT / "angband" / "adapter" / "an_manifest.cpp",
    # : 訳のカタログと名前の表。
    ROOT / "angband" / "adapter" / "an_lang.cpp",
    ROOT / "presentation" / "frame" / "frame_codec.cpp",
    ROOT / "presentation" / "frame" / "protocol_messages.cpp",
    ROOT / "presentation" / "frame" / "protocol_transport.cpp",
    ROOT / "presentation" / "bridge" / "input_event_adapter.cpp",
    # §8.5 T1: `hb_target_choose()` ほか（`native_targeting_api.h` の実体）。
    ROOT / "presentation" / "bridge" / "native_targeting_runtime.cpp",
    ROOT / "platform" / "portable" / "legacy_os.cpp",
]


def run(args: list[str]) -> None:
    subprocess.run(args, cwd=ROOT, check=True)


def build(source: Path, output: Path, work: Path) -> None:
    if len(CORE_C_FILES) != 149:
        raise RuntimeError(f"expected 149 core .c files (150 minus main.c), got {len(CORE_C_FILES)}")
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    core_c = [
        str(CLANG), "--target=i686-w64-windows-gnu", "-std=gnu11", "-O2", "-w",
        "-I", str(source),
    ]
    adapter_c = [
        str(CLANG), "--target=i686-w64-windows-gnu", "-std=gnu11", "-O2",
        "-I", str(source), "-I", str(ADAPTER), "-I", str(ROOT / "platform"),
        # an_targeting.c（設計 §8.5 T1）が presentation/bridge/native_targeting_api.h を
        # 「bridge/...」で引く。C++ TU 側（cpp_flags）と同じ ROOT/presentation を足す。
        "-I", str(ROOT / "presentation"),
    ]

    objects: list[Path] = []
    for filename in CORE_C_FILES:
        obj = work / (Path(filename).stem + ".o")
        run(core_c + ["-c", str(source / filename), "-o", str(obj)])
        objects.append(obj)

    for filename in ADAPTER_C_FILES:
        obj = work / (Path(filename).stem + ".adapter.o")
        run(adapter_c + ["-c", str(ADAPTER / filename), "-o", str(obj)])
        objects.append(obj)

    cpp_flags = [
        str(CLANGXX), "--target=i686-w64-windows-gnu", "-std=c++20", "-O2",
        "-I", str(ADAPTER), "-I", str(ROOT / "third_party" / "cpp" / "include"),
        "-I", str(ROOT / "presentation"), "-I", str(ROOT / "platform"),
    ]
    for cpp_src in CPP_SHARED:
        obj = work / (cpp_src.stem + ".cpp.o")
        run(cpp_flags + ["-c", str(cpp_src), "-o", str(obj)])
        objects.append(obj)

    output.parent.mkdir(parents=True, exist_ok=True)
    run([str(CLANGXX), "--target=i686-w64-windows-gnu", "-static", *map(str, objects),
         "-ladvapi32", "-lshell32", "-luser32", "-lshlwapi", "-lws2_32", "-o", str(output)])
    print(f"built {output}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", type=Path, default=DEFAULT_UPSTREAM)
    parser.add_argument("--source-root", type=Path, default=Path(os.environ.get("HENGBAND_CORE_SOURCE_ROOT", DEFAULT_OUTPUT)),
        help="prepared-source root made by tools/core_sources/prepare.py (angband/patched-src is read from it)")
    parser.add_argument("--out-dir", type=Path, default=ROOT)
    parser.add_argument("--obj-dir", type=Path, default=ROOT / ".taskwork" / "angband-win-build",
        help="where the .o files go (give each concurrent build its own, with its own --source-root)")
    parser.add_argument("--refresh-lib", action="store_true",
        help="re-copy angband/lib/{gamedata,customize,screens,help} from upstream even if "
             "angband/lib/gamedata already exists (S1 review item 8: a plain build should "
             "NOT rmtree angband/lib every time -- M1 will put translated files there)")
    args = parser.parse_args()
    subprocess.run([sys.executable, str(ROOT / "tools" / "core_sources" / "prepare.py"),
                    "--upstream", str(args.upstream.resolve()), "--output", str(args.source_root.resolve())],
                   cwd=ROOT, check=True)
    src = args.source_root.resolve() / "angband" / "patched-src"
    lib_marker = ROOT / "angband" / "lib" / "gamedata"
    if args.refresh_lib or not lib_marker.is_dir():
        copy_runtime_data(args.upstream.resolve(), ROOT)
    else:
        print(f"angband: lib/ already prepared ({lib_marker} exists); "
              "pass --refresh-lib to re-copy from upstream")
    # （P3）: 日本語の説明文を差し替えた gamedata の写し。
    # 英語の lib/gamedata が変われば作り直す（作成物は追跡しない）。
    subprocess.run([sys.executable, str(ROOT / "tools" / "angband" / "an_build_ja_gamedata.py"), "build"],
                   cwd=ROOT, check=True)
    build(src, (args.out_dir / "AngbandCore.exe").resolve(), args.obj_dir.resolve())


if __name__ == "__main__":
    main()
