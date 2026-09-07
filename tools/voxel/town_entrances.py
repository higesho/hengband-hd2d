#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""町の入口の向きと、カットアウェイが要る箇所を実データから数え直す（P7）。

基準は `` §10.2、段取りは `` P7。

P7 の検証項目は「**カットアウェイは北向き入口 44 箇所で効くことを町で確かめる**」である。
ところが §10 の町の意味づけ（敷地・柵・門）は**まだ実装されていない**（引き継ぎ §5-5）ので、
実機の町に建物は 1 つも立っていない。**立っていない建物の遮蔽は実機では確かめられない。**

そこで確かめられることを 2 つに分けた。

===============  ==========================================================
どこで            何を
===============  ==========================================================
本スクリプト      **町の実データ**から入口の向きを数え直し、カメラの媒介変数を
                  設計書 §4.4 の遮蔽の式に入れて「どの入口が隠れるか」を出す
``--cutaway-check``  その形（北向き入口）を合成フレームで作り、**画素で**
                  切れば見えず・入れれば見えることを確かめる
===============  ==========================================================

使い方::

    python tools/voxel/town_entrances.py
    python tools/voxel/town_entrances.py --pitch 46 --cell-px 110 --height 2.5

**この世界は x = 東・y = 南**（`math3d.h`）。カメラは注視点の**南**にいる
（`Camera::eye()`）ので、隠す側は「入口より南（y が大きい側）」の `#` である。
"""

from __future__ import annotations

import argparse
import math
import pathlib
import sys

# §10.2 の「入口」。1〜9・0 が店、a〜p が施設。
SHOP_GLYPHS = set("1234567890")
FACILITY_GLYPHS = set("abcdefghijklmnop")
ENTRANCE_GLYPHS = SHOP_GLYPHS | FACILITY_GLYPHS

#: 敷地（PERMANENT）。§10.3 が「建物ではなく敷地」として読めと言っているもの。
SITE_GLYPH = "#"

#: 数える町。``06_Rlyeh_Obsoleted`` は名前のとおり使われていない。
#:
#: Outpost には 3 つの版があり、**どれが読まれるかは荒野の設定で決まる**
#: （``TownDefinitionList.txt`` の ``$WILDERNESS``）。1 つに決められないので両方数える。
OTHER_TOWNS = [
    "02_Telmora.txt",
    "03_Morivant.txt",
    "04_Angwil.txt",
    "05_Zul.txt",
]
OUTPOST_VARIANTS = [
    ("$WILDERNESS NORMAL", "01_Outpost_Full.txt"),
    ("$WILDERNESS LITE", "01_Outpost_Lite.txt"),
]


def read_map(path: pathlib.Path) -> list[str]:
    """``D:`` の行だけを拾って地図にする。"""
    rows = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if raw.startswith("D:"):
            rows.append(raw[2:])
    width = max((len(r) for r in rows), default=0)
    return [r.ljust(width) for r in rows]


def facing(rows: list[str], x: int, y: int) -> str:
    """入口がどちらを向いているか。**開いている隣**を向きとする。

    実データを見ると、入口の隣の開き方は**きれいに 5 通りしか無い**::

        開いているのが 1 方向だけ …… 南 / 北 / 東 / 西
        四方すべて開いている     …… 12 箇所（通りの真ん中に立つ自立した入口）

    つまり向きは**一意に決まる**（優先順位を決める必要が無い）。四方が開いているものは
    そもそも遮蔽が起きようがないので ``free`` として分けて数える。
    """
    height = len(rows)
    width = len(rows[0]) if rows else 0

    def open_at(nx: int, ny: int) -> bool:
        if (nx < 0) or (ny < 0) or (nx >= width) or (ny >= height):
            return False
        return (rows[ny][nx] != SITE_GLYPH) and (rows[ny][nx] not in ENTRANCE_GLYPHS)

    # y が大きい側が南（この世界の約束）。
    north = open_at(x, y - 1)
    south = open_at(x, y + 1)
    sides = open_at(x - 1, y) or open_at(x + 1, y)
    if north and south and sides:
        return "free"
    if south:
        return "south"
    if sides:
        return "eastwest"
    if north:
        return "north"
    return "enclosed"


def occluded(rows: list[str], x: int, y: int, distance: float, eye_height: float,
             building_height: float, eye_target_z: float) -> int:
    """南にいるカメラから見て、この入口を隠している敷地マスの数。

    設計書 §4.4 の遮蔽そのもの。視点 ``(y + distance, eye_height)`` から
    入口の ``(y, eye_target_z)`` へ引いた直線が、南側 k マス目で通る高さは::

        z(k) = eye_target_z + (eye_height - eye_target_z) * k / distance

    そこに ``building_height`` の敷地が建っていれば隠れる。
    """
    height = len(rows)
    width = len(rows[0]) if rows else 0
    hidden = 0
    k = 1
    while k <= int(math.ceil(distance)):
        ny = y + k
        if (ny >= height) or (x < 0) or (x >= width):
            break
        ray_z = eye_target_z + ((eye_height - eye_target_z) * k / distance)
        if ray_z >= building_height:
            break  # これより南は、どれだけ建っていても届かない
        if rows[ny][x] == SITE_GLYPH:
            hidden += 1
        k += 1
    return hidden


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=".", help="リポジトリの根")
    parser.add_argument("--pitch", type=float, default=46.0, help="見下ろし角（度）")
    parser.add_argument("--fov", type=float, default=40.0, help="水平画角（度）")
    parser.add_argument("--cell-px", type=float, default=110.0, help="注視点での 1 マスの px")
    parser.add_argument("--viewport", default="1600x900", help="画面の大きさ")
    parser.add_argument("--cutaway-px", type=float, default=190.0, help="カットアウェイの半径（px）")
    parser.add_argument("--height", type=float, nargs="*", default=[1.0, 2.5],
                        help="建物の高さ（マス）。複数指定できる")
    args = parser.parse_args()

    root = pathlib.Path(args.root)
    town_dir = root / "lib" / "edit" / "towns"
    if not town_dir.is_dir():
        print(f"町のデータが見つかりません: {town_dir}", file=sys.stderr)
        return 1

    # --- カメラ（`distance_for_cell_px()` と同じ式） ---
    screen_w = int(args.viewport.split("x")[0])
    tan_half_x = math.tan(math.radians(args.fov) * 0.5)
    view_distance = screen_w / (2.0 * args.cell_px * tan_half_x)
    distance = view_distance * math.cos(math.radians(args.pitch))
    eye_height = distance * math.tan(math.radians(args.pitch))
    eye_target_z = 0.55  # `make_player_cutaway()` の腰の高さ

    print(f"カメラ: 見下ろし {args.pitch:.0f}° 画角 {args.fov:.0f}° 1 マス {args.cell_px:.0f}px"
          f" → 水平距離 {distance:.1f} マス / 視点の高さ {eye_height:.1f} マス")
    print(f"カットアウェイの半径 {args.cutaway_px:.0f}px"
          f" ＝ 注視点で {args.cutaway_px / args.cell_px:.2f} マス"
          f"（プレイヤの板は 1 マス ＝ {args.cell_px:.0f}px）")
    print()

    kinds = ["south", "eastwest", "north", "free", "enclosed"]
    labels = {"south": "南", "eastwest": "東西", "north": "北", "free": "自立", "enclosed": "囲"}

    for note, outpost in OUTPOST_VARIANTS:
        towns = [outpost] + OTHER_TOWNS
        totals = {k: 0 for k in kinds}
        shop_totals = {k: 0 for k in kinds}
        hidden_by_height = {h: {k: 0 for k in kinds} for h in args.height}

        print(f"### Outpost = {outpost.replace('.txt', '')}（{note}）")
        header = f"{'町':<24}{'計':>4}" + "".join(f"{labels[k]:>6}" for k in kinds)
        for h in args.height:
            header += f"{'  隠れる@' + format(h, '.1f'):>13}"
        print(header)

        for name in towns:
            path = town_dir / name
            if not path.is_file():
                print(f"  （{name} が無い）")
                continue
            rows = read_map(path)
            counts = {k: 0 for k in kinds}
            town_hidden = {h: 0 for h in args.height}
            for y, row in enumerate(rows):
                for x, glyph in enumerate(row):
                    if glyph not in ENTRANCE_GLYPHS:
                        continue
                    side = facing(rows, x, y)
                    counts[side] += 1
                    totals[side] += 1
                    if glyph in SHOP_GLYPHS:
                        shop_totals[side] += 1
                    for h in args.height:
                        if occluded(rows, x, y, distance, eye_height, h, eye_target_z) > 0:
                            town_hidden[h] += 1
                            hidden_by_height[h][side] += 1
            line = f"{name.replace('.txt', ''):<24}{sum(counts.values()):>4}"
            line += "".join(f"{counts[k]:>6}" for k in kinds)
            for h in args.height:
                line += f"{town_hidden[h]:>13}"
            print(line)

        total = sum(totals.values())
        print(f"  合計 {total} 箇所 — " + " / ".join(f"{labels[k]} {totals[k]}" for k in kinds))
        print("  うち店（1〜9・0）: " + " / ".join(f"{labels[k]} {shop_totals[k]}" for k in kinds))
        print("  **カメラの南から見て隠れる入口**（設計書 §4.4 の遮蔽の式）")
        for h in args.height:
            by = hidden_by_height[h]
            total_hidden = sum(by.values())
            print(f"    建物の高さ {h:.1f} マス: {total_hidden:>3} 箇所（"
                  + " / ".join(f"{labels[k]} {by[k]}" for k in kinds) + "）")
            if totals["north"] > 0:
                print(f"        北向き入口 {totals['north']} のうち {by['north']} 箇所"
                      f"（{100.0 * by['north'] / totals['north']:.0f}%）が隠れる")
        print()

    print("※ 隠れる＝カットアウェイが要る。**要ることの確認**であって、効くことの確認ではない。")
    print("   効くことは `HengbandHd2d.exe --cutaway-check` が画素で見る（合成フレーム）。")
    print()
    print("※ **設計書 §10.2 の「全 5 町で 127 箇所・北 44」とは一致しない。**")
    print("   本スクリプトの数え方（開いている隣を向きとする）では 98〜111 箇所・北 24〜29 になる。")
    print("   §10.2 は本企画の前の検討で採った値で、数え方が記録されていない。")
    print("   **結論は変わらない**（むしろ強まる）: 北向き入口は建物が実物の高さを持てば")
    print("   100% 隠れ、東西向きの多くも隠れる。カットアウェイは要る。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
