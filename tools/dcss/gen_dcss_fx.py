# -*- coding: utf-8 -*-
"""DCSS の攻撃の演出（ビームの弾・矢・投げた物・爆発）の絵の種類の表 `assets/ui/dcss/atlas/fx_tiles.tsv` を作る。

原作のウェブタイル版は、ビーム・矢・爆発を地図のマスの `t.ov`（画像ページ main のタイル番号）で送る（`beam.cc` の `bolt::draw`・`explosion_draw_cell`、
`view.cc` の `view_add_tile_overlay`）。画面（`hd2d/dcss/dcss_fx.cpp`）はそのタイルの画素をボクセルにして立体の地図の上に出す。
その際、タイルごとに「向きを持つ細長い絵か、丸い塊か」「光る物か、実物か」が要るので、タイルの名前（`TILE_MI_*`・`TILE_BOLT_*`）から種類を決めてここへ書く。

種類（1 字）:
  M … 矢・クロスボウの矢・投げ槍・ダーツ・スリングの弾・ブーメランなどの実物（`TILE_MI_*`）。8 方向の変種が向きの絵になっている。地図の面に寝かせて出す。光らない。
  D … 向きを持つ魔法の絵（氷柱・石の矢・結晶の槍・稲妻の線など。原作の `vary_bolt_tile` が方向の変種を選ぶ物）。地図の面に寝かせて出す。光る。
  B … 丸い塊の魔法の絵（火の玉・爆発・雲・渦など）。画面（カメラ）へ向けて立てる。光る。
  （表に無い main のタイル = 投げたアイテムの絵ほか。光らない塊として画面へ向けて立てる）

出力: 1 行 1 タイル `<タイル番号>\\t<種類>\\t<名前>`。

使い方: `python tools/dcss/gen_dcss_fx.py`（生成物 `DCSS_WORK` の `gen/rltiles` から作る）。`--check` で一致を見る。
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parent))
import dcss_tilefam as tf  # noqa: E402

REPO = HERE.parents[2]
ATLAS = REPO / "assets" / "ui" / "dcss" / "atlas"
OUT = ATLAS / "fx_tiles.tsv"

#: 原作 `tilepick.cc` の `vary_bolt_tile` が「方向の変種」(tile + dir) で選ぶ物の基の名前。
DIRECTIONAL = (
    "STONE_ARROW", "CRYSTAL_SPEAR", "ICICLE", "ICICLE_SALVO", "LIGHT", "IRON_SHOT", "BLOOD_ARROW", "SPLINTERSPRAY", "PIE", "POISON_ARROW",
    "SHADOW_SHOT", "FORCE_LANCE", "HARPOON_SHOT", "METAL_SPLINTERS", "FROSTFIRE", "ZAP", "SHADOW_BEAM",
)


def base_of(name: str) -> str:
    """`TILE_BOLT_ICICLE_3` -> `ICICLE`。末尾の `_数字`（変種の通し番号）を落とす。"""
    word = name[len("TILE_BOLT_"):]
    return re.sub(r"(_\d+)+$", "", word)


def kind_of(name: str) -> str:
    if name.startswith("TILE_MI_"):
        return "M"
    if name.startswith("TILE_BOLT_"):
        return "D" if base_of(name) in DIRECTIONAL else "B"
    return ""


def build(gen: str | None) -> list[str]:
    tiles = tf.Tiles(gen)
    lines = ["# DCSS の攻撃の演出の絵の種類（M 実物の細長い絵・D 向きのある魔法の絵・B 丸い塊の魔法の絵）。tools/dcss/gen_dcss_fx.py が作る（手で直さない）。",
             "# <タイル番号> <種類> <名前>"]
    lo, hi = tiles.names["TILE_FEAT_MAX"], tiles.names["TILE_MAIN_MAX"]
    seen: dict[int, str] = {}
    for name, value in tiles.names.items():
        if not (lo <= value < hi):
            continue
        # 同じ値に複数の名前（`TILE_SYM_BOLT_OFS = TILE_BOLT_DEFAULT_BLUE` など）があるときは、最初の名前を採る。
        if value in seen:
            continue
        seen[value] = tiles.first_name.get(value, name)
    for value in sorted(seen):
        name = seen[value]
        kind = kind_of(name)
        if kind:
            lines.append(f"{value}\t{kind}\t{name}")
    return lines


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gen", default="", help="tilegen の出力（既定は DCSS_WORK の gen/rltiles）")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    text = "\n".join(build(args.gen or None)) + "\n"
    if args.check:
        same = OUT.is_file() and OUT.read_text(encoding="utf-8") == text
        print("一致" if same else "食い違い")
        return 0 if same else 1
    OUT.write_text(text, encoding="utf-8", newline="\n")
    counts = {}
    for line in text.splitlines()[2:]:
        k = line.split("\t")[1]
        counts[k] = counts.get(k, 0) + 1
    print(f"{OUT} に {len(text.splitlines()) - 2} タイルぶん書いた（{counts}）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
