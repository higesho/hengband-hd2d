"""Windows build scripts of Umoria / Rogue 3.6 / Rogue 5.4 / RogueClone2 use this to get their source tree.

Those four cores are built by `tools/{umoria,rogue_legacy,rogue2}/build_windows.py` (clang from the bundled
toolchain), not by a Visual Studio project, so `VisualStudio/CoreSources.props` does not run for them.  They must
still compile the copy of `<core>/patched-src` that carries the attack-effect observation calls
(tools/targeting/native_fx_overlay.py), exactly like the CMake builds (Linux, Android, Quest) do through
tools/targeting/targeting_sources.cmake.

The overlay is written to the same place the Visual Studio projects use (`<prepared root>-targeting-windows`,
CoreSources.props), under the same lock and with the same fingerprint, so building a core here and a project
there in parallel does not regenerate it twice.  `HENGBAND_TARGETING_OVERLAY_ROOT` overrides the place.
"""
from __future__ import annotations

import os
from pathlib import Path

import prepare_windows_overlay


def overlay_root(source_root: Path) -> Path:
    override = os.environ.get("HENGBAND_TARGETING_OVERLAY_ROOT")
    if override:
        return Path(override).resolve()
    source_root = source_root.resolve()
    return source_root.with_name(source_root.name + "-targeting-windows")


def prepare(source_root: Path) -> Path:
    """Make (or reuse) the overlay of the prepared sources at `source_root` and return its root."""
    output = overlay_root(source_root)
    prepare_windows_overlay.prepare(source_root, output)
    return output
