#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""変愚蛮怒の公式の 8 画素のタイルから、実体（プレイヤー・怪物・アイテム）のボクセルの板と対応表を作る。

決めたこと（2026-10-28）「変愚と短愚のキャラクター、アイテム、モンスターについて、公式の 8px のタイルがあるよね。
新バージョン対応してる抜けのない奴。あれもボクセル板にして切り替えられるようにしたい」。決めたこと（同日・遊ぶ人）:
  - 素材は変愚 3.0.2.6 の上流の `lib/xtra/graf/8x8.bmp` と `lib/pref/graf-xxx.prf`（2026-10-03 に multilockon123 氏の絵で
    新しくなった物）と `lib/pref/xtra-xxx.prf`（プレイヤー）。短愚は上流に 8 画素の絵が無く表も古いので、番号が同じ変愚の絵を使う。
  - 板は 1 画素を 4×4 ボクセル（1 マス 32 ボクセル）、厚さは今の 64 画素の板と同じ（マスの 1/16 ＝ 2 ボクセル）。
  - 切り替えは機能メニュー。選んだ組に無い物は描かない。

作る物:
  presentation/bridge/official8_tile_table.h   対応表（怪物の番号・アイテムの番号 → 絵の面の位置、種族 → 列、職業 → 段、
                                               短愚で変愚と別物の番号）。コア（presentation_bridge.cpp）が引く。
  assets/voxel/slab/H8_<段>_<列>.vox / .jsonc   絵の面の位置ごとの板（使われている位置だけ）。今の板と同じ置き場に別の名前で置く。

絵の地は濃い灰色（パレットの 0 番 = 48,48,48）なので抜く。黒はふちどりなので残す。
原作の写し（../roguelike-cores/upstream/hengband・tangband）は読むだけ。

使い方:
    python tools/voxel/official8_tiles.py            # 表と板
    python tools/voxel/official8_tiles.py --table    # 表だけ
"""
from __future__ import annotations

import argparse
import difflib
import json
import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT.parent / "roguelike-cores" / "upstream"
HENG = UPSTREAM / "hengband"
TANG = UPSTREAM / "tangband"
HEADER = ROOT / "presentation" / "bridge" / "official8_tile_table.h"
SLAB_DIR = ROOT / "assets" / "voxel" / "slab"
WORK = ROOT / ".taskwork" / "official8"
TILE = 8
SCALE = 4  # 1 画素 = 4×4 ボクセル（板の一辺 32）
BACKGROUND = (48, 48, 48)
INDEX_BASE = 30000  # コアが送る目録の番号 = INDEX_BASE + 段 * 64 + 列（64 画素の組の番号〔1〜2000 ほど〕と重ならない）


def slab_name(row: int, col: int) -> str:
    return f"H8_{row}_{col}"


def read_prf(path: Path) -> dict[tuple[str, int], tuple[int, int]]:
    """`R:<番号>:0x<attr>:0x<char>`（区切りは `:` か `/`）のうち、絵を指す行（attr の上の桁が立つ）。"""
    out: dict[tuple[str, int], tuple[int, int]] = {}
    text = path.read_text(encoding="utf-8", errors="replace")
    for m in re.finditer(r"^([RK]):(\d+):0x([0-9A-Fa-f]+)[:/]0x([0-9A-Fa-f]+)", text, re.M):
        attr = int(m.group(3), 16)
        if attr & 0x80:
            out[(m.group(1), int(m.group(2)))] = (attr & 0x7F, int(m.group(4), 16) & 0x7F)
    return out


def read_player(path: Path) -> tuple[list[int], list[int]]:
    """xtra-xxx.prf: 種族のかたまり（列）と職業のかたまり（段）。どちらも原作の番号の順に並んでいる。"""
    races: list[int] = []
    classes: list[int] = []
    current = None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("?:[EQU $RACE"):
            current = "race"
        elif line.startswith("?:[EQU $CLASS"):
            current = "class"
        elif line.startswith("R:0:") and current:
            attr_s, char_s = re.split(r"[:/]", line[4:].strip())[:2]
            attr = int(attr_s, 16) if attr_s.lower().startswith("0x") else int(attr_s)
            char = int(char_s, 16) if char_s.lower().startswith("0x") else int(char_s)
            if current == "race":
                races.append(char & 0x7F)
            else:
                classes.append(attr & 0x7F)
            current = None
    return races, classes


def load_jsonc(path: Path):
    s = path.read_text(encoding="utf-8")
    s = re.sub(r"//[^\n]*", "", s)
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r",(\s*[}\]])", r"\1", s)
    return json.loads(s)


def names(core: Path, kind: str) -> dict[int, str]:
    if kind == "R":
        d = load_jsonc(core / "lib" / "edit" / "MonraceDefinitions.jsonc")
        lst = d.get("monsters") if isinstance(d, dict) else d
    else:
        d = load_jsonc(core / "lib" / "edit" / "BaseitemDefinitions.jsonc")
        lst = d.get("baseitems") if isinstance(d, dict) else d
    return {int(m["id"]): ((m.get("name") or {}).get("en") or "") for m in lst}


def tangband_skips(kind: str) -> list[int]:
    """短愚で、同じ番号が変愚と別物を指すもの（名前がほとんど似ていない）。綴りの違い（Draon・Avater）は同じ物とみなす。"""
    h = names(HENG, kind)
    t = names(TANG, kind)
    out = []
    for i, tn in t.items():
        hn = h.get(i)
        if hn is None:
            out.append(i)
            continue
        if difflib.SequenceMatcher(None, hn.lower(), tn.lower()).ratio() < 0.6:
            out.append(i)
    return sorted(out)


def write_header(table, races, classes, skip_r, skip_k, cells) -> None:
    mon = sorted((i, rc) for (k, i), rc in table.items() if k == "R")
    itm = sorted((i, rc) for (k, i), rc in table.items() if k == "K")
    lines = []
    w = lines.append
    w("/*!")
    w(" * @file official8_tile_table.h")
    w(" * @brief 変愚蛮怒の公式の 8 画素のタイル（変愚 3.0.2.6 の上流の graf-xxx.prf・xtra-xxx.prf。multilockon123 氏の絵）の対応表。")
    w(" * @details **生成物。手で直さない。** `tools/voxel/official8_tiles.py` が作る。実体の板を「公式 8 画素」の組にしたとき、")
    w(" * コア（`presentation_bridge.cpp`）が怪物・アイテム・プレイヤーの絵の面の位置（段・列）を引き、目録の番号")
    w(" * `kOfficial8IndexBase + 段 * kOfficial8Cols + 列` として画面へ送る。板の名前は `H8_<段>_<列>`（`assets/voxel/slab`）。")
    w(f" * 怪物 {len(mon)}・アイテム {len(itm)}・種族 {len(races)}・職業 {len(classes)}・使う絵の面の位置 {len(cells)}。")
    w(" */")
    w("#pragma once")
    w("")
    w("#include <cstddef>")
    w("#include <cstdint>")
    w("")
    w("namespace presentation {")
    w("")
    w(f"inline constexpr int kOfficial8Cols = 64;")
    w(f"inline constexpr int kOfficial8IndexBase = {INDEX_BASE};")
    w("")
    w("struct Official8Entry {")
    w("    uint16_t id; //!< 怪物の種族の番号・ベースアイテムの番号")
    w("    uint8_t row;")
    w("    uint8_t col;")
    w("};")
    w("")
    w("inline constexpr Official8Entry kOfficial8Monsters[] = {")
    for i, (r, c) in mon:
        w(f"    {{ {i}, {r}, {c} }},")
    w("};")
    w("inline constexpr Official8Entry kOfficial8Items[] = {")
    for i, (r, c) in itm:
        w(f"    {{ {i}, {r}, {c} }},")
    w("};")
    w("//! 種族の番号 → 列（xtra-xxx.prf の種族のかたまり。無い種族は -1）")
    w("inline constexpr int8_t kOfficial8RaceCol[] = { " + ", ".join(str(c) for c in races) + " };")
    w("//! 職業の番号 → 段（xtra-xxx.prf の職業のかたまり）")
    w("inline constexpr int8_t kOfficial8ClassRow[] = { " + ", ".join(str(r) for r in classes) + " };")
    w("//! 短愚で、同じ番号が変愚と別物を指す物（描かない）")
    w("inline constexpr uint16_t kOfficial8TangbandSkipMonsters[] = { " + (", ".join(str(i) for i in skip_r) or "0xFFFF") + " };")
    w("inline constexpr uint16_t kOfficial8TangbandSkipItems[] = { " + (", ".join(str(i) for i in skip_k) or "0xFFFF") + " };")
    w("//! 使っている絵の面の位置（段 * kOfficial8Cols + 列）。目録へ載せる")
    w("inline constexpr uint16_t kOfficial8Cells[] = {")
    for n in range(0, len(cells), 16):
        w("    " + ", ".join(str(r * 64 + c) for r, c in cells[n:n + 16]) + ",")
    w("};")
    w("")
    w("} // namespace presentation")
    HEADER.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print("wrote", HEADER, f"(怪物 {len(mon)} / アイテム {len(itm)} / 位置 {len(cells)})")


def bake_slabs(cells) -> int:
    sheet = Image.open(HENG / "lib" / "xtra" / "graf" / "8x8.bmp").convert("RGBA")
    px_dir = WORK / "px32"
    px_dir.mkdir(parents=True, exist_ok=True)
    names_ = []
    for r, c in cells:
        t = sheet.crop((c * TILE, r * TILE, c * TILE + TILE, r * TILE + TILE))
        p = t.load()
        for y in range(TILE):
            for x in range(TILE):
                if p[x, y][:3] == BACKGROUND:
                    p[x, y] = (0, 0, 0, 0)
        big = t.resize((TILE * SCALE, TILE * SCALE), Image.NEAREST)
        name = slab_name(r, c)
        big.save(px_dir / f"{name}.png")
        names_.append(name)
    sys.path.insert(0, str(ROOT / "tools" / "voxel"))
    import tile_to_slab  # noqa: E402
    return tile_to_slab.main(["--ids", ",".join(names_), "--px-dir", str(px_dir), "--src", str(px_dir),
                              "--side", str(TILE * SCALE), "--out", str(SLAB_DIR), "--no-count"])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--table", action="store_true", help="対応表だけ作る（板は作らない）")
    args = ap.parse_args()
    for p in (HENG / "lib" / "xtra" / "graf" / "8x8.bmp", HENG / "lib" / "pref" / "graf-xxx.prf", HENG / "lib" / "pref" / "xtra-xxx.prf"):
        if not p.is_file():
            raise SystemExit(f"上流の写しが無い: {p}")
    table = read_prf(HENG / "lib" / "pref" / "graf-xxx.prf")
    races, classes = read_player(HENG / "lib" / "pref" / "xtra-xxx.prf")
    if len(races) < 30 or len(classes) < 20:
        raise SystemExit(f"xtra-xxx.prf の読み取りがおかしい（種族 {len(races)}・職業 {len(classes)}）")
    skip_r = tangband_skips("R")
    skip_k = tangband_skips("K")
    print("短愚で別物の番号: 怪物", skip_r, "アイテム", skip_k)
    cells = sorted(set(table.values()) | {(r, c) for r in classes for c in races})
    write_header(table, races, classes, skip_r, skip_k, cells)
    if args.table:
        return 0
    return bake_slabs(cells)


if __name__ == "__main__":
    sys.exit(main())
