#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DCSS のゲームパッドの割り当ての画面（`hd2d/dcss/dcss_pad_screen.cpp`）が並べるコマンドの名前と分類の表を作る。

原作の日本語版は、割り当ての画面（`gamepad-menu.cc`。ローカルのタイル版だけに組まれる）に、割り当てられるコマンドを
分類ごとに名前つきで並べる。画面の部品はコアの外にあるので、同じ表を画面の側へ持つ（コマンドの番号とキーの名前は
コアが `{"t":"dcss_ui","k":"pad"}` で送る。ここは名前と分類だけ）。

    python tools/dcss/gen_dcss_pad_cmds.py [--source <原作の crawl-ref/source>] [--out hd2d/dcss/dcss_pad_cmds.inc]

出どころ:
  - 分類・コマンドの並びと英語の名前  `gamepad-menu.cc` の `pad_command_groups()`
  - 日本語の名前・分類・層の名前       `dat/database/ja/jtrans/gamepad-menu.txt`
                                       （`[pad command]<英語>`・`[pad group]<英語>`・`[pad layer]<英語>`）
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = REPO.parent / "roguelike-cores" / "build-sources" / "dcss" / "crawl-ref" / "source"


def read_entries(path: Path):
    text = path.read_text(encoding="utf-8")
    for block in text.split("%%%%"):
        lines = [l for l in block.strip("\n").split("\n") if not l.startswith("#")]
        while lines and not lines[0].strip():
            lines.pop(0)
        if not lines:
            continue
        value = "\n".join(lines[1:]).strip()
        if value:
            yield lines[0], value


def parse_groups(cc: Path):
    """gamepad-menu.cc の pad_command_groups() を (分類の英語, [(CMD 名, 英語の名前)]) の並びにする。"""
    text = cc.read_text(encoding="utf-8")
    start = text.index("pad_command_groups()")
    start = text.index("static const vector<pad_command_group> groups", start)
    end = text.index("return groups;", start)
    body = text[start:end]
    groups = []
    for m in re.finditer(r'\{\s*"([^"]+)",\s*\{(.*?)\}\s*\},?\s*(?=\{\s*"|\};)', body, re.S):
        name = m.group(1)
        cmds = re.findall(r"\{\s*(CMD_[A-Z0-9_]+),\s*\"([^\"]+)\"\s*\}", m.group(2))
        groups.append((name, cmds))
    return groups


def esc(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", default=str(DEFAULT_SOURCE))
    ap.add_argument("--out", default=str(REPO / "hd2d" / "dcss" / "dcss_pad_cmds.inc"))
    args = ap.parse_args()
    src = Path(args.source)
    groups = parse_groups(src / "gamepad-menu.cc")
    ja = {}
    for key, value in read_entries(src / "dat" / "database" / "ja" / "jtrans" / "gamepad-menu.txt"):
        ja[key] = value
    lines = [
        "// 自動生成: tools/dcss/gen_dcss_pad_cmds.py（原作の gamepad-menu.cc と日本語版の辞書 dat/database/ja/jtrans/gamepad-menu.txt から）。手で直さない。",
        "// 割り当ての画面に並べるコマンドの分類・名前。番号とキーの名前はコアが送る（dcss/adapter/dcss_pad_data.h）。",
        "inline constexpr DcssPadGroupName kDcssPadGroups[] = {",
    ]
    for name, _ in groups:
        lines.append(f'    {{ "{esc(name)}", "{esc(ja.get("[pad group]" + name, name))}" }},')
    lines.append("};")
    lines.append("inline constexpr DcssPadCmdName kDcssPadCmdNames[] = {")
    missing = []
    total = 0
    for gi, (_, cmds) in enumerate(groups):
        for cmd, label in cmds:
            total += 1
            jl = ja.get("[pad command]" + label)
            if jl is None:
                missing.append(label)
                jl = label
            lines.append(f'    {{ "{cmd}", {gi}, "{esc(label)}", "{esc(jl)}" }},')
    lines.append("};")
    lines.append("inline constexpr DcssPadLayerName kDcssPadLayers[] = {")
    for en in ("Single press", "Holding LB", "Holding RB", "Holding LB and RB"):
        lines.append(f'    {{ "{en}", "{esc(ja.get("[pad layer]" + en, en))}" }},')
    lines.append("};")
    Path(args.out).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\r\n")
    print(f"{len(groups)} 分類・{total} コマンド -> {args.out}")
    if missing:
        print("日本語の名前が無い:", missing)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
