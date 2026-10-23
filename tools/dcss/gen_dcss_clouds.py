# -*- coding: utf-8 -*-
"""DCSS の雲のタイル（`TILE_CLOUD_*`。96 枚）から、画面が立体の雲を作る材料 `assets/ui/dcss/atlas/clouds.tsv` を作る。

決めたこと D14（決め直し）: 雲は原作の雲のタイルの**色と模様**に合わせてボクセルの塊（視界を遮る雲）か低い層（遮らない雲）にする。
タイルを 32 ドット四方に置き（原作 `draw_tile` と同じ位置）、8x8 の区画（1 区画 = 4x4 画素）ごとに、
色（不透明度で重みを付けた平均）と不透明度（平均）を求める。画面（`hd2d/dcss/dcss_clouds.cpp`）はこの区画ごとに箱を置く。

出力 `clouds.tsv`: 1 行 1 タイル `<タイル番号>\t<自発光 0|1>\t<64 区画ぶんの RRGGBBAA を 16 進で並べた字>`。区画の並びは左上から右へ、上から下へ。
自発光 1 は火・エネルギー系（画面が光らせる）。区分の語は `gen_dcss_terrain_rules.py` の旧 `CLOUD_CLASSES`（火・エネルギー）と同じ。

使い方: `python tools/dcss/gen_dcss_clouds.py`（既定の生成物 `DCSS_WORK` の `gen/rltiles` と `assets/ui/dcss/atlas` から作る）。`--check` で一致を見る。
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parent))
import dcss_tilefam as tf  # noqa: E402

REPO = HERE.parents[2]
ATLAS = REPO / "assets" / "ui" / "dcss" / "atlas"
OUT = ATLAS / "clouds.tsv"
GRID = 8  # 1 辺の区画の数（4x4 画素の区画）

#: 自発光にする雲（タイル名の語）。火・エネルギー系。
EMISSIVE_WORDS = ("FIRE", "FLAME", "FOREST_FIRE", "BLASTMOTES", "ELECTRICITY", "STORM", "TLOC_ENERGY", "MAGIC_TRAIL", "SPECTRAL", "CHAOS", "MUTAGENIC")


def tile_rows() -> dict[int, tuple[str, ...]]:
    out = {}
    for line in (ATLAS / "tiles.tsv").read_text(encoding="utf-8").splitlines():
        if line and not line.startswith(("#", "const ")):
            p = line.split("\t")
            out[int(p[0])] = tuple(p[1:])
    return out


def emissive_of(name: str) -> int:
    word = name[len("TILE_CLOUD_"):]
    parts = word.split("_")
    # 末尾の数字（濃さ）を落とす
    while parts and parts[-1].isdigit():
        parts.pop()
    base = "_".join(parts)
    return 1 if base in EMISSIVE_WORDS else 0


def build(gen: str | None) -> list[str]:
    tiles = tf.Tiles(gen)
    rows = tile_rows()
    main = Image.open(ATLAS / "main.png").convert("RGBA")
    clouds = sorted((v, n) for n, v in tiles.names.items() if n.startswith("TILE_CLOUD_"))
    lines = ["# DCSS の雲のタイルの区画ごとの色と不透明度（画面が立体の雲を作る材料）。tools/dcss/gen_dcss_clouds.py が作る（手で直さない）。",
             f"# <タイル番号> <自発光 0|1> <{GRID * GRID} 区画の RRGGBBAA（左上から右・下へ）>。区画 = 32 ドットのタイルを {GRID}x{GRID} に割った 1 つ"]
    cell = 32 // GRID
    for idx, name in clouds:
        r = rows.get(idx)
        if r is None:
            continue
        w, h, ox, oy, sx, sy, ex, ey = (int(x) for x in r[1:9])
        crop = main.crop((sx, sy, ex, ey))
        canvas = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
        # 原作 draw_tile（centre = 真）: x = ox + (16 - w/2)、y = oy + (32 - h)
        x0 = int(ox + 16 - w / 2)
        y0 = oy + (32 - h)
        canvas.alpha_composite(crop, (max(0, x0), max(0, y0)))
        px = canvas.load()
        out = []
        for by in range(GRID):
            for bx in range(GRID):
                sr = sg = sb = sa = 0
                for j in range(cell):
                    for i in range(cell):
                        r_, g_, b_, a_ = px[bx * cell + i, by * cell + j]
                        sr += r_ * a_
                        sg += g_ * a_
                        sb += b_ * a_
                        sa += a_
                n = cell * cell
                if sa > 0:
                    cr, cg, cb = round(sr / sa), round(sg / sa), round(sb / sa)
                else:
                    cr = cg = cb = 0
                out.append(f"{cr:02x}{cg:02x}{cb:02x}{round(sa / n):02x}")
        lines.append(f"{idx}\t{emissive_of(name)}\t{''.join(out)}")
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
    print(f"{OUT} に {len(text.splitlines()) - 2} タイルぶん書いた")
    return 0


if __name__ == "__main__":
    sys.exit(main())
