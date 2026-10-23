#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DCSS の地形（dungeon_feature_type の値）→ HD2D の地形番号・地形の旗・ミニマップの種別の表を作る。

    python tools/dcss/gen_terrain_table.py          # dcss/adapter/dcss_terrain_table.inc を書く
    python tools/dcss/gen_terrain_table.py --check  # 書かずに、いまの .inc が表と一致するか照合する

元は `dcss/terrain/feature_map.csv`（別の担当が持つ。**読むだけ**）。列は「DCSS の定義名、番号、…、寄せ先（HD2D の地形番号）、…、
feature_flags 案、MinimapKind 案」。番号が空の行（店の種別などの派生）は地形の値を持たないので、表には入れない。
出力は `dcss/adapter/dcss_frame.cpp` が取り込む。値は 221 個（0〜220。20〜29 の穴は 0）。
"""
from __future__ import annotations

import csv
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CSV_PATH = REPO / "dcss" / "terrain" / "feature_map.csv"
OUT_PATH = REPO / "dcss" / "adapter" / "dcss_terrain_table.inc"

NUM_FEATURES = 221

FLAG_NAMES = {
    "WALL": "CELL_FEAT_WALL", "DOOR": "CELL_FEAT_DOOR", "DOOR_OPEN": "CELL_FEAT_DOOR_OPEN", "STAIRS": "CELL_FEAT_STAIRS",
    "PERMANENT": "CELL_FEAT_PERMANENT", "TREE": "CELL_FEAT_TREE", "WATER": "CELL_FEAT_WATER", "LAVA": "CELL_FEAT_LAVA",
    "GLOW": "CELL_FEAT_GLOW", "KNOWN": "CELL_FEAT_KNOWN", "PASSABLE": "CELL_FEAT_PASSABLE", "RUBBLE": "CELL_FEAT_RUBBLE",
}
KIND_NAMES = {
    "Unknown": "MinimapKind::Unknown", "Floor": "MinimapKind::Floor", "Wall": "MinimapKind::Wall", "Door": "MinimapKind::Door",
    "Stairs": "MinimapKind::Stairs", "Tree": "MinimapKind::Tree", "Water": "MinimapKind::Water", "Lava": "MinimapKind::Lava",
    "Grass": "MinimapKind::Grass", "Snow": "MinimapKind::Snow", "Mountain": "MinimapKind::Mountain",
}


def parse_flags(text: str) -> tuple[list[str], bool]:
    """feature_flags 案 → (旗の名前の並び, 既知の旗を立てるか)。"""
    text = text.strip()
    if text.startswith("(WALL"):
        return [], True            # 「WALL も PASSABLE も立てない」（鉄格子・像など。既知の旗だけ立てる）
    if text.startswith("("):
        return [], False           # 「なし。KNOWN も立てない」（未探索の疑似地形）
    names = [t for t in text.split("|") if t]
    for n in names:
        if n not in FLAG_NAMES:
            raise SystemExit(f"知らない旗の名前: {n!r}（{text!r}）")
    return names, True


def build() -> str:
    rows = [r for r in csv.reader(open(CSV_PATH, encoding="utf-8", newline="")) if r and not r[0].startswith("#")]
    table: dict[int, tuple[str, int, list[str], bool, str]] = {}
    for r in rows[1:]:
        if not r[1].strip():
            continue                # 派生の行
        num = int(r[1])
        if not 0 <= num < NUM_FEATURES:
            raise SystemExit(f"番号が範囲外: {r[:2]}")
        flags, known = parse_flags(r[9])
        kind = r[10].strip()
        if kind not in KIND_NAMES:
            raise SystemExit(f"知らない種別: {kind!r}（{r[0]}）")
        table[num] = (r[0], int(r[4]), flags, known, kind)
    out = ["// 生成物: tools/dcss/gen_terrain_table.py（元: dcss/terrain/feature_map.csv）。手で直さない。",
           "// DCSS の dungeon_feature_type の値 → {HD2D の地形番号, feature_flags, ミニマップの種別}。",
           f"static constexpr int kDcssFeatureCount = {NUM_FEATURES};",
           "static const DcssTerrainRow kDcssTerrain[kDcssFeatureCount] = {"]
    for num in range(NUM_FEATURES):
        if num not in table:
            out.append(f"    {{ 0, 0, MinimapKind::Unknown }}, // {num}")
            continue
        name, tid, flags, known, kind = table[num]
        bits = [FLAG_NAMES[f] for f in flags] + (["CELL_FEAT_KNOWN"] if known and "KNOWN" not in flags else [])
        expr = " | ".join(bits) if bits else "0"
        out.append(f"    {{ {tid}, static_cast<std::uint16_t>({expr}), {KIND_NAMES[kind]} }}, // {num} {name}")
    out.append("};")
    return "\n".join(out) + "\n"


def main() -> int:
    text = build()
    if "--check" in sys.argv:
        have = OUT_PATH.read_text(encoding="utf-8") if OUT_PATH.exists() else ""
        if have != text:
            print("dcss_terrain_table.inc が feature_map.csv と食い違っています（python tools/dcss/gen_terrain_table.py で作り直す）")
            return 1
        print("dcss_terrain_table.inc: feature_map.csv と一致")
        return 0
    OUT_PATH.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {OUT_PATH} ({len(text)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
