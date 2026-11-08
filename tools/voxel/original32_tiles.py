#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""変愚蛮怒の公式の 32 画素のタイル（Original32）から、実体（プレイヤー・怪物・アイテム）のボクセルの板と対応表を作る。

決めたこと（2026-11-08）「変愚の新バージョンが出たから対応しよう。今回は公式の32pxタイルが入ってるからボクセル板にして
8px64pxと切り替えられるようにして」「今回のリリースでは32pxの板をデフォルトに」。決めたこと（同日・遊ぶ人）:
  - 素材は hengband.xtra の `graf/original32.png`（dis- 氏。CC0 1.0。2026-11-05 のコミット 3a00142）。
    本体の対応表（変愚 3.0.2.7 の `src/view/original-*.inc`）と組で使う。
  - 怪物とプレイヤーは、本体が 2D 表示で読み込むときに掛ける手当て（`view/actor-readability.h` の readable_actor_tile。
    枠いっぱいへの拡大・色の段の削減・明るい縁取り）を掛けた絵を板にする。アイテムには本体も掛けないので掛けない。
  - 変愚と短愚に出す。短愚は番号がほぼ変愚と同じなので変愚の絵を使い、別物の番号（公式 8 画素の表と同じ）は描かない。
  - 板は 1 画素 = 1 ボクセル（1 マス 32 ボクセル）、厚さは今までの板と同じ（マスの 1/16 = 2 ボクセル）。

本体の引き方（同じにする）:
  - 怪物: 見かけの種族の番号で表を引き、表示色が表の色と違えば色違いの枠（`original-monster-tiles.cpp` の monster_catalog_tile_symbol）。
    絵の面の位置は MONSTER_CATALOG_BASE_ROW 段から 16 列で並ぶ。
  - アイテム: 鑑定済みの固定アーティファクトは専用の絵、ほかは見た目の番号（未鑑定でも正体は出ない）。
    表示色が白（1）でなく色違いの枠があればそれ（`original-item-tiles.cpp` の catalog_tile_symbol）。CATALOG_BASE_ROW 段から。
  - プレイヤー: 職業 × 2 ＋ 性別。表示色ごとの枠のうち白の枠（`original-player-tiles.cpp`）。PLAYER_BASE_ROW 段から。

作る物:
  presentation/bridge/original32_tile_table.h   対応表（生成物・追跡する）。コア（presentation_bridge.cpp）が引く。
  assets/voxel/slab/H32_<枠>.vox / .jsonc        絵の面の枠（左上から 16 列で数えた番号）ごとの板。使う枠だけ。

目録の番号は INDEX_BASE + 枠（公式 8 画素の 30000 台〔最大 38191〕と重ならない。最大 54095 で 16 ビットに収まる）。
原作の写し（../roguelike-cores/upstream/hengband）は読むだけ（git show で版を指定して読む）。

使い方:
    python tools/voxel/original32_tiles.py            # 表と板
    python tools/voxel/original32_tiles.py --table    # 表だけ
    python tools/voxel/original32_tiles.py --sheet <出力.png> --frames 1,2,3   # 板にする前の絵を並べて見る
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT.parent / "roguelike-cores" / "upstream" / "hengband"
SHEET = ROOT.parent / "roguelike-cores" / "extras" / "hengband-original32" / "original32.png"
SHEET_SHA256 = "c54f92b25c7fa81bace3969186236b116e316cd2cda6323d08dfbb03564ea189"  # original32.README.txt に書かれた値
REV = "3.0.2.7-Beta"
HEADER = ROOT / "presentation" / "bridge" / "original32_tile_table.h"
SLAB_DIR = ROOT / "assets" / "voxel" / "slab"
WORK = ROOT / ".taskwork" / "original32"
TILE = 32
COLS = 16
INDEX_BASE = 40000
TRANSPARENT = (255, 0, 255)  # 本体の ACTOR_TRANSPARENT（0x00ff00ff）。不透明のマゼンタが透明の印
WHITE = 1  # TERM_WHITE

# 定義の色の名前 → 端末の色の番号（src/term/gameterm.cpp の color_list）
COLOR_NAMES = {
    "Black": 0, "White": 1, "Gray": 2, "Orange": 3, "Red": 4, "Green": 5, "Blue": 6, "Brown": 7,
    "Dark Gray": 8, "Light Gray": 9, "Violet": 10, "Yellow": 11, "Light Red": 12, "Light Green": 13,
    "Light Blue": 14, "Light Brown": 15,
}


def slab_name(frame: int) -> str:
    return f"H32_{frame}"


def git_show(path: str) -> str:
    r = subprocess.run(["git", "-C", str(UPSTREAM), "show", f"{REV}:{path}"], capture_output=True, check=True)
    return r.stdout.decode("utf-8-sig")


def constant(path: str, name: str) -> int:
    m = re.search(rf"\b{name}\s*=\s*(\d+)\s*;", git_show(path))
    if not m:
        raise SystemExit(f"{path} に {name} が無い")
    return int(m.group(1))


def entries(path: str) -> list[tuple[int, ...]]:
    out = []
    for line in git_show(path).splitlines():
        s = line.strip()
        if s.startswith("{"):
            out.append(tuple(int(v) for v in re.findall(r"-?\d+", s)))
    return out


def numbers(path: str) -> list[int]:
    return [int(v) for v in re.findall(r"-?\d+", git_show(path))]


def load_jsonc(path: str):
    s = git_show(path)
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"(?m)^\s*//[^\n]*", "", s)
    s = re.sub(r",(\s*[}\]])", r"\1", s)
    return json.loads(s)


def readable_actor_tile(tile: Image.Image, player: bool) -> Image.Image:
    """本体の `view/actor-readability.h` の readable_actor_tile() と同じ計算（整数の割り算まで同じ）。

    本体の画素は 0x00RRGGBB の並び（DIB の 32 ビット）なので、色の定数は (R, G, B) に読み替えてある。
    """
    src = tile.convert("RGB").load()
    pts = [(x, y) for y in range(TILE) for x in range(TILE) if src[x, y] != TRANSPARENT]
    body = Image.new("RGB", (TILE, TILE), TRANSPARENT)
    if not pts:
        return body
    left = min(x for x, _ in pts)
    right = max(x for x, _ in pts)
    top = min(y for _, y in pts)
    bottom = max(y for _, y in pts)
    width = right - left + 1
    height = bottom - top + 1
    extent = 30 if player else 28
    longest = max(width, height)
    out_w = max(1, width * extent // longest)
    out_h = max(1, height * extent // longest)
    ox = (TILE - out_w) // 2
    oy = (TILE - out_h) // 2
    b = body.load()
    for y in range(out_h):
        for x in range(out_w):
            sx = left + (2 * x + 1) * width // (2 * out_w)
            sy = top + (2 * y + 1) * height // (2 * out_h)
            px = src[sx, sy]
            if px == TRANSPARENT:
                continue
            maximum = max(px)
            if maximum < 48:
                out = (0x14, 0x18, 0x20)
            else:
                level = 104 if maximum < 80 else 144 if maximum < 128 else 184 if maximum < 176 else 224 if maximum < 224 else 248
                out = tuple(min(248, ((c * level // maximum + 8) // 16) * 16) for c in px)
            b[ox + x, oy + y] = out
    result = body.copy()
    r = result.load()
    outline = (0xF0, 0xD8, 0x90) if player else (0x9C, 0xAC, 0xB8)
    for y in range(TILE):
        for x in range(TILE):
            if b[x, y] != TRANSPARENT:
                continue
            if any(0 <= x + dx < TILE and 0 <= y + dy < TILE and b[x + dx, y + dy] != TRANSPARENT
                   for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                r[x, y] = outline
    return result


def load_sheet() -> Image.Image:
    if not SHEET.is_file():
        raise SystemExit(f"素材が無い: {SHEET}（hengband.xtra の 3a00142 の graf/original32.png を置く）")
    digest = hashlib.sha256(SHEET.read_bytes()).hexdigest()
    if digest != SHEET_SHA256:
        raise SystemExit(f"素材のハッシュが違う: {digest}（説明書の値 {SHEET_SHA256}）")
    sheet = Image.open(SHEET).convert("RGB")
    if sheet.width != COLS * TILE:
        raise SystemExit(f"素材の幅が {COLS * TILE} でない: {sheet.width}")
    return sheet


class Catalog:
    """本体の対応表（変愚 3.0.2.7 の src/view/original-*）。"""

    def __init__(self) -> None:
        self.monster_base_row = constant("src/view/original-monster-catalog.h", "MONSTER_CATALOG_BASE_ROW")
        self.item_base_row = constant("src/view/original-item-catalog.h", "CATALOG_BASE_ROW")
        self.player_base_row = constant("src/view/original-player-tiles.h", "PLAYER_BASE_ROW")
        self.monster_frames = constant("src/view/original-monster-catalog.h", "MONSTER_CATALOG_FRAMES")
        self.item_icons = constant("src/view/original-item-catalog.h", "CATALOG_ICONS")
        # 怪物: { id, color_start, color, character[, base_frame] }。id は 0 から隙間なく並ぶ（道具が確かめる）
        self.monsters = entries("src/view/original-monster-catalog.inc")
        if [m[0] for m in self.monsters] != list(range(len(self.monsters))):
            raise SystemExit("怪物の表の番号が 0 から隙間なく並んでいない")
        self.bases = dict(entries("src/view/original-catalog-base.inc"))
        self.artifacts = dict(entries("src/view/original-catalog-artifacts.inc"))
        self.color_starts = numbers("src/view/original-catalog-colors.inc")
        if len(self.color_starts) != self.item_icons:
            raise SystemExit(f"アイテムの色の表が {self.item_icons} 個でない: {len(self.color_starts)}")

    # 本体の monster_catalog_tile_symbol と同じ
    def monster_payload(self, mid: int, color: int) -> int:
        m = self.monsters[mid]
        color_start, entry_color = m[1], m[2]
        base_frame = m[4] if len(m) > 4 else mid
        color &= 15
        extra = color if color < entry_color else color - 1
        return base_frame if (color_start < 0 or color == entry_color) else color_start + extra

    # 本体の catalog_tile_symbol と同じ
    def item_payload(self, icon: int, color: int) -> int:
        start = self.color_starts[icon]
        color &= 15
        return start + (0 if color == 0 else color - 1) if (start >= 0 and color != WHITE) else icon

    def monster_frame(self, payload: int) -> int:
        return self.monster_base_row * COLS + payload

    def item_frame(self, payload: int) -> int:
        return self.item_base_row * COLS + payload

    def player_frame(self, icon: int) -> int:
        return self.player_base_row * COLS + WHITE * 58 + icon


def item_frames(cat: Catalog) -> set[int]:
    """アイテムで使う枠: 全部の絵の白の枠＋（ベースアイテムの絵, その表示色）＋（アーティファクトの絵, 基のアイテムの表示色）。

    見た目（フレーバー）は同じ種別の中で入れ替わるだけで、表示色は見た目のアイテムの色なので、組は番号ごとに決まる。
    基のアイテムが見た目を持つ種別（指輪など）のアーティファクトは、その種別の見た目の色を全部入れる。
    ここに無い組は、コアが白の枠へ落とす（presentation_bridge.cpp）。
    """
    items = load_jsonc("lib/edit/BaseitemDefinitions.jsonc")["baseitems"]
    color = {int(k["id"]): COLOR_NAMES[k["symbol"]["color"]] for k in items}
    kind = {int(k["id"]): (k["itemkind"]["type_value"], k["itemkind"].get("subtype_value", 0)) for k in items}
    flavored = {int(k["id"]) for k in items if k.get("flavor_name")}
    by_kind = {v: k for k, v in kind.items()}
    frames = {cat.item_frame(icon) for icon in range(cat.item_icons)}
    for bid, icon in cat.bases.items():
        if bid in color:
            frames.add(cat.item_frame(cat.item_payload(icon, color[bid])))
    arts = load_jsonc("lib/edit/ArtifactDefinitions.jsonc")["artifacts"]
    for a in arts:
        aid = int(a["id"])
        if aid not in cat.artifacts:
            continue
        base = by_kind.get((a["base_item"]["type_value"], a["base_item"].get("subtype_value", 0)))
        if base is None:
            continue
        if base in flavored:
            tval = kind[base][0]
            colors = {color[k] for k in flavored if kind[k][0] == tval}
        else:
            colors = {color[base]}
        for c in colors:
            frames.add(cat.item_frame(cat.item_payload(cat.artifacts[aid], c)))
    return frames


def write_header(cat: Catalog, frames: list[int]) -> None:
    lines = []
    w = lines.append
    w("/*!")
    w(" * @file original32_tile_table.h")
    w(" * @brief 変愚蛮怒の公式の 32 画素のタイル（Original32。dis- 氏。CC0 1.0）の対応表。変愚 3.0.2.7 の src/view/original-*.inc から。")
    w(" * @details **生成物。手で直さない。** `tools/voxel/original32_tiles.py` が作る。実体の板を「公式 32 画素」の組にしたとき、")
    w(" * コア（`presentation_bridge.cpp`）が本体の 2D 表示と同じ引き方で絵の面の枠を決め、目録の番号 `kOriginal32IndexBase + 枠` を画面へ送る。")
    w(" * 板の名前は `H32_<枠>`（`assets/voxel/slab`）。枠は絵の面の左上から 16 列で数えた番号。")
    w(f" * 怪物 {len(cat.monsters)}・ベースアイテム {len(cat.bases)}・固定アーティファクト {len(cat.artifacts)}・アイテムの絵 {cat.item_icons}・板にした枠 {len(frames)}。")
    w(" */")
    w("#pragma once")
    w("")
    w("#include <cstddef>")
    w("#include <cstdint>")
    w("")
    w("namespace presentation {")
    w("")
    w(f"inline constexpr int kOriginal32IndexBase = {INDEX_BASE};")
    w(f"inline constexpr int kOriginal32MonsterBaseFrame = {cat.monster_base_row * COLS}; //!< 怪物の表の 0 番の枠（MONSTER_CATALOG_BASE_ROW * 16）")
    w(f"inline constexpr int kOriginal32ItemBaseFrame = {cat.item_base_row * COLS}; //!< アイテムの表の 0 番の枠（CATALOG_BASE_ROW * 16）")
    w(f"inline constexpr int kOriginal32PlayerFrame = {cat.player_base_row * COLS + WHITE * 58}; //!< プレイヤーの白の枠の 0 番（PLAYER_BASE_ROW * 16 + 白 * 58）")
    w("inline constexpr int kOriginal32PlayerIcons = 58; //!< 職業 29 × 性別 2（職業 × 2 ＋ 性別）")
    w("")
    w("//! 怪物（種族の番号で引く。0 から隙間なく並ぶ）。本体の CatalogMonster と同じ中身")
    w("struct Original32Monster {")
    w("    int16_t color_start; //!< 色違いの枠の先頭（無ければ -1）")
    w("    uint8_t color; //!< 表の色（この色なら base_frame）")
    w("    int16_t base_frame; //!< 表の色のときの枠（怪物の表の中の番号）")
    w("};")
    w("inline constexpr Original32Monster kOriginal32Monsters[] = {")
    for m in cat.monsters:
        base_frame = m[4] if len(m) > 4 else m[0]
        w(f"    {{ {m[1]}, {m[2]}, {base_frame} }}, // {m[0]}")
    w("};")
    w("")
    w("struct Original32Pair {")
    w("    uint16_t id; //!< ベースアイテムの番号・固定アーティファクトの番号")
    w("    uint16_t icon; //!< アイテムの絵の番号")
    w("};")
    w("inline constexpr Original32Pair kOriginal32Baseitems[] = {")
    for k, v in sorted(cat.bases.items()):
        w(f"    {{ {k}, {v} }},")
    w("};")
    w("inline constexpr Original32Pair kOriginal32Artifacts[] = {")
    for k, v in sorted(cat.artifacts.items()):
        w(f"    {{ {k}, {v} }},")
    w("};")
    w("//! アイテムの絵ごとの色違いの枠の先頭（無ければ -1。アイテムの表の中の番号）")
    w("inline constexpr int16_t kOriginal32ItemColorStarts[] = {")
    for n in range(0, len(cat.color_starts), 16):
        w("    " + ", ".join(str(v) for v in cat.color_starts[n:n + 16]) + ",")
    w("};")
    w("//! 板にした枠（昇順）。目録へ載せ、コアは無い枠を白の枠へ落とす")
    w("inline constexpr uint16_t kOriginal32Frames[] = {")
    for n in range(0, len(frames), 16):
        w("    " + ", ".join(str(v) for v in frames[n:n + 16]) + ",")
    w("};")
    w("")
    w("} // namespace presentation")
    HEADER.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    print("wrote", HEADER, f"(枠 {len(frames)})")


def frame_image(sheet: Image.Image, cat: Catalog, frame: int) -> Image.Image:
    x, y = frame % COLS * TILE, frame // COLS * TILE
    tile = sheet.crop((x, y, x + TILE, y + TILE))
    monster_lo = cat.monster_frame(0)
    monster_hi = cat.monster_frame(cat.monster_frames)
    player_lo = cat.player_base_row * COLS
    player_hi = player_lo + 58 * 16
    if monster_lo <= frame < monster_hi:
        tile = readable_actor_tile(tile, False)
    elif player_lo <= frame < player_hi:
        tile = readable_actor_tile(tile, True)
    rgba = tile.convert("RGBA")
    p = rgba.load()
    for yy in range(TILE):
        for xx in range(TILE):
            if p[xx, yy][:3] == TRANSPARENT:
                p[xx, yy] = (0, 0, 0, 0)
    return rgba


def bake_slabs(sheet: Image.Image, cat: Catalog, frames: list[int]) -> int:
    px_dir = WORK / "px32"
    px_dir.mkdir(parents=True, exist_ok=True)
    names = []
    for frame in frames:
        name = slab_name(frame)
        img = frame_image(sheet, cat, frame)
        if img.getbbox() is None:
            raise SystemExit(f"枠 {frame} が空（透明だけ）")
        img.save(px_dir / f"{name}.png")
        names.append(name)
    sys.path.insert(0, str(ROOT / "tools" / "voxel"))
    import tile_to_slab  # noqa: E402
    return tile_to_slab.main(["--ids", ",".join(names), "--px-dir", str(px_dir), "--src", str(px_dir),
                              "--side", str(TILE), "--out", str(SLAB_DIR), "--no-count"])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--table", action="store_true", help="対応表だけ作る（板は作らない）")
    ap.add_argument("--sheet", type=Path, help="板にする前の絵を並べた PNG を書く（--frames と組で）")
    ap.add_argument("--frames", help="--sheet で並べる枠（カンマ区切り）")
    args = ap.parse_args()
    cat = Catalog()
    sheet = load_sheet()
    if args.sheet:
        ids = [int(v) for v in args.frames.split(",")]
        out = Image.new("RGBA", (len(ids) * TILE * 4, TILE * 4), (92, 96, 104, 255))
        for n, f in enumerate(ids):
            out.alpha_composite(frame_image(sheet, cat, f).resize((TILE * 4, TILE * 4), Image.NEAREST), (n * TILE * 4, 0))
        out.save(args.sheet)
        print("wrote", args.sheet)
        return 0
    monsters = {cat.monster_frame(p) for p in range(cat.monster_frames)}
    players = {cat.player_frame(i) for i in range(58)}
    items = item_frames(cat)
    frames = sorted(monsters | players | items)
    if INDEX_BASE + frames[-1] > 0xFFFF:
        raise SystemExit("目録の番号が 16 ビットを超える")
    print(f"怪物 {len(monsters)}・プレイヤー {len(players)}・アイテム {len(items)}・計 {len(frames)}")
    write_header(cat, frames)
    if args.table:
        return 0
    return bake_slabs(sheet, cat, frames)


if __name__ == "__main__":
    sys.exit(main())
