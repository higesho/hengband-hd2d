# -*- coding: utf-8 -*-
"""DCSS の地形のタイル（rltiles/dngn/ の 32x32 PNG）を、ボクセルのプレハブ（`.vox` ＋ `.jsonc`）にする。

決めたこと（2026-10-21）:
  1. 壁は、タイル画像に合わせて **1 ボクセルほどの凹凸**を付ける（石の積み方・模様に沿って出っ張り・へこみ）。
  2. 祭壇・泉などのオブジェクトは、**絵を解釈してボクセルのオブジェクトを一から作る**（`gen_dcss_objects.py`）。
  3. 上り階段は、下り階段の色調に合わせて色を変える。扉も見直す。
  4. 権利が確認できない絵も、原作で実際に使われているのでそのまま使う。作らずに止まる作りは外し、
     **使った絵を `dcss/assets/voxel/tiles_used.tsv` に記録する**（来歴は `tile_provenance.py`）。

何を作るか（行の元）: `dcss/terrain/feature_map.csv`（地形の名寄せの表）の全行。表のタイル（`元のタイル画像`）と
プレハブ案の名前から、作る物の一覧（プラン）を組む。名前が決まらない行（仕分け a・b）は `AB_NAMES` で決める。
種類（kind）はタイルの置き場から決める（`classify`）:

| 種類 | 形 | 作り方 |
|---|---|---|
| wall | 32x32x32 のブロック | 4 側面と天面にタイルを写し、明るさの差で 3 段（出っ張り 0・基準 1・へこみ 2）の凹凸を付ける |
| floor / liquid | 32x32x8 の床の板 | 天面にタイル。継ぎ目を 1 ボクセル彫る。水・溶岩は天面を 2 ボクセル下げる |
| door | 幅 32 x 厚み 12 x 高さ 32（南向き。`_ew` は東西向き） | 枠は厚み 12、扉板は 4 引っ込める。開いた・壊れた扉は暗い所を抜いて向こうを見せる |
| stairs_up / stairs_down | ブロック / 床の板 | 上りは輪郭が段の断面（下りの色調に合わせて色替え）、下りは段状の穴 |
| gate | 板（物） | 輪郭の縁を厚く内側をへこませた門 |
| decal | 板（物。床に貼り付く） | 罠・印を床に貼った浮き彫り |
| pit | 床の板 | 縦穴 |
| 祭壇・噴水・像・木・店・隠し場所・格子 | 物 | `gen_dcss_objects.py`（一から作る） |

使い方:
  python -u tools/dcss/gen_dcss_terrain.py [名前 ...]     # 名前を省くと全部（`--list` で一覧だけ）
  出力は dcss/assets/voxel/ 。見るときは dcss/assets/README.md の「確かめ方」を参照。
"""
import argparse
import csv
import io
import os
import re
import sys

import numpy as np
from scipy import ndimage

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dcss_vox as dv  # noqa: E402
from dcss_vox import (V, FLOOR_H, BURIED, Palette, Canvas, load_tile, lum, shade, hsv, relief_levels,  # noqa: E402
                      line_mask, lift_to, write_pair)

FEATURE_MAP = os.path.join(dv.ROOT, 'dcss', 'terrain', 'feature_map.csv')

#: 仕分け a・b の行（名前がプレハブ案にない）に付ける名前。定義名（DNGN_ を除く）-> プレハブ名
AB_NAMES = {
    'CLOSED_DOOR': 'dc_door_closed', 'OPEN_DOOR': 'dc_door_open', 'BROKEN_DOOR': 'dc_door_broken',
    'RUNED_DOOR': 'dc_door_runed', 'SEALED_DOOR': 'dc_door_sealed',
    'CLOSED_CLEAR_DOOR': 'dc_door_clear_closed', 'OPEN_CLEAR_DOOR': 'dc_door_clear_open',
    'BROKEN_CLEAR_DOOR': 'dc_door_clear_broken', 'RUNED_CLEAR_DOOR': 'dc_door_clear_runed',
    'SEALED_CLEAR_DOOR': 'dc_door_clear_sealed',
    'TREE': 'dc_tree',
    'ROCK_WALL': 'dc_wall_rock', 'STONE_WALL': 'dc_wall_stone', 'PERMAROCK_WALL': 'dc_wall_permarock',
    'CLEAR_ROCK_WALL': 'dc_wall_clear_rock', 'CLEAR_STONE_WALL': 'dc_wall_clear_stone',
    'CLEAR_PERMAROCK_WALL': 'dc_wall_clear_permarock', 'SLIMY_WALL': 'dc_wall_slimy', 'FRIGID_WALL': 'dc_wall_frigid',
    'OPEN_SEA': 'dc_water_open_sea', 'DEEP_WATER': 'dc_water_deep', 'SHALLOW_WATER': 'dc_water_shallow',
    'TOXIC_BOG': 'dc_water_toxic_bog', 'LAVA_SEA': 'dc_lava_sea', 'LAVA': 'dc_lava', 'MUD': 'dc_floor_mud',
    'FLOOR': 'dc_floor_grey_dirt',
    'GRANITE_STATUE': 'dc_statue_granite', 'METAL_STATUE': 'dc_statue_metal',
    'TRAP_SHAFT': 'dc_trap_shaft', 'TRAP_WEB': 'dc_trap_web',
    'ABANDONED_SHOP': 'dc_shop_abandoned', 'STONE_ARCH': 'dc_gate_stone_arch', 'EXPIRED_PORTAL': 'dc_gate_expired',
    'STONE_STAIRS_DOWN_I': 'dc_stairs_down', 'STONE_STAIRS_DOWN_II': 'dc_stairs_down', 'STONE_STAIRS_DOWN_III': 'dc_stairs_down',
    'STONE_STAIRS_UP_I': 'dc_stairs_up', 'STONE_STAIRS_UP_II': 'dc_stairs_up', 'STONE_STAIRS_UP_III': 'dc_stairs_up',
    'EXIT_DUNGEON': 'dc_stairs_exit_dungeon',
    'SEALED_STAIRS_UP': 'dc_stairs_sealed_up', 'SEALED_STAIRS_DOWN': 'dc_stairs_sealed_down',
    'FOUNTAIN_BLUE': 'dc_fountain_blue', 'FOUNTAIN_SPARKLING': 'dc_fountain_sparkling',
    'FOUNTAIN_BLOOD': 'dc_fountain_blood', 'FOUNTAIN_EYES': 'dc_fountain_eyes',
    'DRY_FOUNTAIN': 'dc_fountain_dry', 'DRY_FOUNTAIN_BLUE': 'dc_fountain_dry', 'DRY_FOUNTAIN_SPARKLING': 'dc_fountain_dry',
    'DRY_FOUNTAIN_BLOOD': 'dc_fountain_dry',
    'ORB_DAIS': 'dc_dais_orb',
    'STONE_ARCH/HELL': 'dc_gate_stone_arch_hell',
}


# ------------------------------------------------------------------ 材質（色相から当てる）
def mat_stone(c):
    return 'stone'


def mat_door(c):
    r, g, b = c
    sat, _ = hsv(c)
    if r > 150 and g > 110 and b < 90 and sat > 0.4:
        return 'metal'                         # 金色の帯
    if r > g + 18 and r > b + 18:
        return 'wood'                          # 赤茶の扉板
    if g > r + 20 and g > b + 20:
        return 'plant'
    return 'stone'


def mat_any(c):
    """絵から作る物の材質の当て方: 緑 = plant（蔦・苔）、茶 = wood、金・鉄色の高彩度 = metal、青緑の明るい = clean、ほかは stone。"""
    r, g, b = c
    sat, mx = hsv(c)
    if g > r + 25 and g > b + 15:
        return 'plant'
    if sat > 0.45 and r > 150 and g > 100 and b < 90 and r >= g:
        return 'metal'
    if sat > 0.35 and r > g + 25 and r > b + 25 and mx < 0.75:
        return 'wood'
    if b > r + 50 and mx > 0.6:
        return 'clean'
    return 'stone'


# ------------------------------------------------------------------ 壁のブロック
def build_wall(name, rel, what, top_k=0.9, recolor=None):
    img, opaque = load_tile(rel, name, max_colours=110)       # 天面の暗い色を足しても 255 に収める
    if recolor:
        img = recolor_img(img, recolor)
    if not opaque.all():
        fill = tuple(int(x) for x in img[opaque].mean(axis=0)) if opaque.any() else (128, 128, 128)
        img = img.copy()
        img[~opaque] = fill
    pal = Palette(mat_stone)
    v = np.zeros((V, V, V), np.int16)
    mean = tuple(img.reshape(-1, 3).mean(axis=0))
    v[1:V - 1, 1:V - 1, :V - 1] = pal(shade(mean, 0.7))
    faces = {'S': img, 'N': np.roll(img, 11, axis=1), 'W': np.roll(img, 16, axis=1), 'E': np.roll(img, 5, axis=1)}
    carved = proud = 0

    def cell(k, i, z, d):
        # 面 k の (横 i, 高さ z) の、表面からの深さ d の座標
        if k == 'S':
            return i, V - 1 - d, z
        if k == 'N':
            return V - 1 - i, d, z
        if k == 'W':
            return d, i, z
        return V - 1 - d, V - 1 - i, z

    for k, f in faces.items():
        lev = relief_levels(f, wrap=True)[::-1]          # [z, u]
        col = f[::-1]
        for z in range(V):
            for i in range(V):
                d = int(lev[z, i])
                if i in (0, V - 1) or z in (0, V - 1):
                    d = 0                                 # 角の柱・天面の縁・接地の段は面の位置のまま
                c = tuple(col[z, i])
                for depth in range(3):
                    x, y, zz = cell(k, i, z, depth)
                    if depth < d:
                        v[x, y, zz] = 0
                    elif depth == d:
                        v[x, y, zz] = pal(c)
                    else:
                        v[x, y, zz] = pal(c)
                carved += d == 2
                proud += d == 0
    top = np.rot90(img)
    tl = relief_levels(top, wrap=True)
    for y in range(V):
        for x in range(V):
            d = int(tl[y, x])
            edge = x in (0, V - 1) or y in (0, V - 1)
            if edge:
                d = 0
            c = shade(tuple(top[y, x]), top_k)
            for depth in range(1 if edge else 3):          # 縁は 1 段だけ（側面の上の段を潰さない）
                zz = V - 1 - depth
                if depth < d:
                    v[x, y, zz] = 0
                else:
                    v[x, y, zz] = pal(c)
    size, ncol, nfp = write_pair(name, [('main', v, (0, 0, 0))], pal,
                                 'DCSS の壁 %s（%s）を 4 側面と天面に写し、明るさの差で 3 段の凹凸（出っ張り・基準・へこみ）を付けた'
                                 '（tools/dcss/gen_dcss_terrain.py）' % (what, rel))
    return '%dx%dx%d' % v.shape, ncol, size, '出 %d へこみ %d' % (proud, carved), nfp


# ------------------------------------------------------------------ 地面（床・水・溶岩・泥）
#: 地面の種類ごとの凹凸の付け方（地面もタイルの平らな写しでなく、絵に合わせた凹凸のあるボクセルのオブジェクトにする）。
#:   stone … 石畳・板石: 明るさの差で石ごとの高低（出っ張り +1・基準 0・継ぎ目 -1）
#:   organic … 草・苔・蔦・粘液: 明るい所に株（+1）、暗い所はくぼみ（-1）、そこへ乱数の塊（株の盛り上がり）を重ねる
#:   sand … 砂・土・小石・塩: ゆるい起伏（なめらかな乱数 ±1）に粒の明暗を重ねる
#:   liquid … 水・溶岩・泥: 天面を 2 下げ、明るさで ±1（波・泡）
#: 歩けるマスなので凹凸は ±1 ボクセル（基準の天面は z = 6、出っ張りで床の面 z = 7 にそろう）。
#: **縁の 1 ボクセルは必ず基準の高さにする**（隣のマスと継ぎ目で段差が出ない。並べて確かめた）。
GROUND_STYLES = {
    'organic': ('GRASS', 'GRASS_DARK', 'MOSS', 'LAIR', 'SLIME', 'SLIME_ACIDIC', 'VINES', 'SPROUTING_STONE', 'SPROUTING_SQUARES',
                'SPROUTING_RUIN', 'SWAMP', 'NECROPOLIS_SQUARE_GRASS', 'NECROPOLIS_GRASS', 'NERVES', 'SPIDER', 'MYSTIC_CHASM'),
    'sand': ('GREY_DIRT', 'GREY_DIRT_B', 'PEBBLE', 'PEBBLE_BROWN', 'DIRT', 'SAND', 'SALT', 'ORC', 'ROUGH', 'VOLCANIC',
             'SEISMOROCK', 'DEPTHSTONE', 'SALT_ENDLESS', 'MUD', 'COBBLE_BLOOD'),
}


def ground_style(enum):
    for st, names in GROUND_STYLES.items():
        if enum in names:
            return st
    return 'stone'


def build_floor(name, rel, what, lift=0, drop=0, liquid=False, style='stone'):
    img, opaque = load_tile(rel, name, max_colours=128)
    if not opaque.all():
        fill = tuple(int(x) for x in img[opaque].mean(axis=0)) if opaque.any() else (96, 96, 96)
        img = img.copy()
        img[~opaque] = fill
        opaque = np.ones_like(opaque)
    if lift:
        img = lift_to(img, opaque, lift)
    if liquid:
        style = 'liquid'
    pal = Palette(mat_stone if not liquid else (lambda c: 'clean'))
    h = FLOOR_H
    v = np.zeros((V, V, h), np.int16)
    mean = tuple(img.reshape(-1, 3).mean(axis=0))
    base = h - 2 - drop
    lev = relief_levels(img, wrap=True)
    d = np.array([[{0: 1, 1: 0, 2: -1}[int(lev[y, x])] for x in range(V)] for y in range(V)])
    rng = np.random.default_rng(sum(ord(ch) * (i + 3) for i, ch in enumerate(name)))
    smooth = ndimage.gaussian_filter(rng.random((V, V)), 1.3, mode='wrap')
    smooth = (smooth - smooth.min()) / max(smooth.max() - smooth.min(), 1e-6)
    if style == 'organic':
        d = np.where(lev == 0, 1, 0)
        d = np.where((smooth > 0.62) & (lev != 2), 1, d)          # 株の盛り上がり
        d = np.where((smooth < 0.30) | (lev == 2), -1, d)         # くぼみ・暗い所
    elif style == 'sand':
        d = np.clip(np.round((smooth - 0.5) * 3.0).astype(int) + np.where(lev == 0, 1, 0) - np.where(lev == 2, 1, 0), -1, 1)
    d[0, :] = d[-1, :] = 0
    d[:, 0] = d[:, -1] = 0
    side = pal(shade(mean, 0.75))
    up = down = 0
    LAST.clear()
    LAST.append((v, pal))
    for y in range(V):
        for x in range(V):
            zt = base + int(d[y, x])
            v[x, y, :zt] = side
            v[x, y, zt] = pal(img[y, x])
            up += d[y, x] > 0
            down += d[y, x] < 0
    size, ncol, nfp = write_pair(name, [('main', v, (0, 0, -h + RAISE))], pal,
                                 'DCSS の地面 %s（%s）。タイルに合わせた凹凸（%s。出 %d・へこみ %d。縁は基準の高さ）のボクセルの物'
                                 '（tools/dcss/gen_dcss_terrain.py）' % (what, rel, style, up, down))
    return '%dx%dx%d' % v.shape, ncol, size, '%s 出%d 凹%d' % (style, up, down), nfp


# ------------------------------------------------------------------ 縦穴（床の板に穴）
def build_pit(name, rel, what):
    img, opaque = load_tile(rel, name)
    pal = Palette(mat_stone)
    h = FLOOR_H
    v = np.zeros((V, V, h), np.int16)
    base = (88, 84, 78)
    v[:, :, :h - 1] = pal(shade(base, 0.7))
    v[:, :, h - 1] = pal(base)
    hole = ndimage.binary_dilation(opaque, iterations=1)
    dist = ndimage.distance_transform_edt(hole)
    depth_max = h - 2
    n = 0
    for y in range(V):
        for x in range(V):
            if hole[y, x]:
                d = int(min(depth_max, 1 + dist[y, x] * 1.5))
                top = h - 1 - d
                v[x, y, top + 1:] = 0
                v[x, y, top] = pal(shade(img[y, x] if opaque[y, x] else (60, 56, 52), 0.8 if d > 1 else 1.0))
                n += 1
    size, ncol, nfp = write_pair(name, [('main', v, (0, 0, -h))], pal,
                                 'DCSS の縦穴（%s）。絵の黒い染みの形に床の板を掘った（tools/dcss/gen_dcss_terrain.py）' % rel)
    return '%dx%dx%d' % v.shape, ncol, size, '穴 %d' % n, nfp


# ------------------------------------------------------------------ 扉
def door_leaf_mask():
    m = np.zeros((V, V), bool)
    for j in range(V):
        for i in range(V):
            if j < 4:
                continue
            if j < 10:
                m[j, i] = ((i + 0.5 - 16) / 11.0) ** 2 + ((j - 10) / 6.0) ** 2 <= 1.0
            else:
                m[j, i] = 5 <= i <= 26
    return m


def build_door(name, rel, what, mode='closed', ew=False):
    img, opaque = load_tile(rel, name)
    img = lift_to(img, opaque, LIFT_OBJ)
    inner = fill_outline(img, opaque)                   # 奥・側面の色（輪郭の黒を内側の色で置き換える）
    pal = Palette(mat_door)
    thick_frame, thick_leaf, recess = 12, 8, 4
    y_front = (V + thick_frame) // 2
    v = np.zeros((V, V, V), np.int16)
    leaf = door_leaf_mask()
    dark = lum(img) < 42
    cut = (leaf & dark) if mode in ('open', 'broken') else np.zeros_like(leaf)
    gone = ~opaque
    lev = relief_levels(img, wrap=False)
    ncut = 0
    for j in range(V):
        z = V - 1 - j
        for i in range(V):
            if gone[j, i] or cut[j, i]:
                ncut += 1
                continue
            c = tuple(img[j, i])
            if leaf[j, i]:
                front, t = y_front - recess, thick_leaf
            else:
                front, t = y_front, thick_frame
            e = 1 if lev[j, i] == 0 else (-1 if lev[j, i] == 2 else 0)   # 明るい面は 1 出っ張り、暗い継ぎ目は 1 へこむ
            front += e
            v[i, front - t:front - 1, z] = pal(shade(tuple(inner[j, i]), 0.85))
            v[i, front - 1, z] = pal(c)
    sill_c = pal(shade(tuple(int(x) for x in np.median(inner[opaque], axis=0)), 0.8))   # 敷居（開口の下の段）
    for i in range(V):
        for y in range(y_front - thick_frame, y_front - 1):
            if v[i, y, 0] == 0:
                v[i, y, 0] = sill_c
    if ew:
        v = np.rot90(v, k=1, axes=(0, 1)).copy()
    size, ncol, nfp = write_pair(name, [('main', v, (0, 0, 0))], pal,
                                 'DCSS の扉 %s（%s）。%s。枠は厚み %d、扉板は %d 引っ込める（tools/dcss/gen_dcss_terrain.py）'
                                 % (what, rel, '東西向き' if ew else '南向き', thick_frame, recess))
    return '%dx%dx%d' % v.shape, ncol, size, '扉板 %d 抜き %d' % (int(leaf.sum()), ncut), nfp


# ------------------------------------------------------------------ 色調を合わせる
def match_tone(src, src_op, ref, ref_op, floor=70, ceil=185):
    """src の明るさの分布を ref の分布に合わせる（ヒストグラム整合）。色味（色相の比）は src のまま。"""
    ls = lum(src)
    lr = np.sort(lum(ref)[ref_op])
    out = src.copy().astype(float)
    vals = ls[src_op]                                   # 画素ごとの明るさ（元の並び）
    order = np.argsort(vals, kind='stable')
    ranks = np.empty(len(vals))
    ranks[order] = (np.arange(len(vals)) + 0.5) / len(vals)
    newl = np.interp(ranks, (np.arange(len(lr)) + 0.5) / len(lr), lr)
    newl = np.clip(newl, floor, ceil)
    shift = newl - vals                       # 明るさの差を足す（掛け算だと真っ黒な画素が黒のまま残る）
    sel = np.nonzero(src_op)
    for c in range(3):
        out[sel[0], sel[1], c] = np.clip(src[sel[0], sel[1], c] + shift, 0, 255)
    out[~src_op] = 0
    return out.astype(int)


def fill_outline(img, opaque):
    """輪郭の黒い 1 画素の縁（絵の側の描き込み）を、いちばん近い内側の色で置き換える。"""
    l = lum(img)
    inner = ndimage.binary_erosion(opaque, structure=np.ones((3, 3), bool), border_value=0)
    outline = opaque & ~inner & (l < 40)
    good = opaque & ~outline & (l >= 40)
    if not outline.any() or not good.any():
        return img
    idx = ndimage.distance_transform_edt(~good, return_distances=False, return_indices=True)
    out = img.copy()
    out[outline] = img[idx[0][outline], idx[1][outline]]
    return out


# ------------------------------------------------------------------ 階段（上り）
STAIRS_DOWN_TILE = 'dngn/gateways/stone_stairs_down'
LAST = []              # 直前に作った床の (配列, パレット)。--tile3 が同じ床を 3x3 に並べるのに使う
RAISE = 0              # 床を床の板の厚みだけ持ち上げて作る（`--raise-floors`。プレハブの確認画面は地面の板が床を隠すので、並べて見るときだけ使う）
LIFT_OBJ = 62          # 門・扉・罠の絵の平均の明るさの下限（黒い塊に見えるのを避ける）


def build_stairs_up(name, rel, what, tint=None):
    img, opaque = load_tile(rel, name, max_colours=100)
    ref, ref_op = load_tile(STAIRS_DOWN_TILE, name)
    img = fill_outline(img, opaque)
    img = match_tone(img, opaque, fill_outline(ref, ref_op), ref_op)          # 下りの色調に合わせる
    img = dv.requantize(img, opaque, 90)
    if tint:
        img = dv.requantize(np.clip(img * np.array(tint), 0, 255).astype(int), opaque, 90)
    pal = Palette(mat_stone)
    v = np.zeros((V, V, V), np.int16)
    heights = []
    lev = relief_levels(img, wrap=False)
    for x in range(V):
        rows = np.nonzero(opaque[:, x])[0]
        if rows.size == 0:
            heights.append(0)
            continue
        top_row = int(rows.min())
        heights.append(V - top_row)
        for j in range(top_row, V):
            if not opaque[j, x]:
                continue
            z = V - 1 - j
            c = tuple(img[j, x])
            v[x, :V - 1, z] = pal(shade(c, 0.88))   # 奥は少し暗く（前面だけ絵のまま）
            v[x, V - 1, z] = pal(c)
    size, ncol, nfp = write_pair(name, [('main', v, (0, 0, 0))], pal,
                                 'DCSS の階段 %s（%s）。絵の輪郭を段の断面として y へ押し出した。色は下り階段の色調に合わせた'
                                 '（tools/dcss/gen_dcss_terrain.py）。西へ向かって上る' % (what, rel))
    return '%dx%dx%d' % v.shape, ncol, size, '段の高さ %d..%d' % (min(heights), max(heights)), nfp


# ------------------------------------------------------------------ 階段（下り）
def build_stairs_down(name, rel, what, tint=None):
    img, opaque = load_tile(rel, name, max_colours=128)
    img = lift_to(img, opaque, 70)
    if tint:
        img = np.clip(img * np.array(tint), 0, 255).astype(int)
    pal = Palette(mat_stone)
    h = FLOOR_H
    v = np.zeros((V, V, h), np.int16)
    mean = tuple(img.reshape(-1, 3).mean(axis=0))
    v[:, :, :h - 1] = pal(shade(mean, 0.75))
    border = 3
    x0, x1 = border, V - border
    max_depth = 6
    deepest = 0
    for y in range(V):
        for x in range(V):
            d = 0
            if x0 <= x < x1 and border <= y < V - border:
                d = 1 + (x - x0) * max_depth // (x1 - x0)
                d = min(d, max_depth)
            deepest = max(deepest, d)
            surface = h - 1 - d
            v[x, y, surface + 1:] = 0
            v[x, y, surface] = pal(img[y, x])
    size, ncol, nfp = write_pair(name, [('main', v, (0, 0, -h))], pal,
                                 'DCSS の階段 %s（%s）。絵を天面に貼り、枠の内側を東へ向かって段状に彫り下げた（tools/dcss/gen_dcss_terrain.py）'
                                 % (what, rel))
    return '%dx%dx%d' % v.shape, ncol, size, '最深 %d' % deepest, nfp


# ------------------------------------------------------------------ 門（立てた板。縁を厚く内側をへこませる）
def build_gate(name, rel, what, rim=11, inner=5):
    img, opaque = load_tile(rel, name)
    img = lift_to(img, opaque, LIFT_OBJ)
    fill = fill_outline(img, opaque)
    pal = Palette(mat_any)
    padded = np.pad(opaque, 1, constant_values=False)
    dist = ndimage.distance_transform_edt(padded)[1:-1, 1:-1]
    lev = relief_levels(img, wrap=False)
    rows = np.nonzero(opaque.any(axis=1))[0]
    bottom, top_row = int(rows.max()), int(rows.min())
    shift = -(V - 1 - bottom) + BURIED
    h = (V - top_row) + BURIED
    v = np.zeros((V, V, h), np.int16)
    y_face = (V + rim + 1) // 2
    for j in range(top_row, bottom + 1):
        z = (V - 1 - j) + shift
        for i in range(V):
            if not opaque[j, i]:
                continue
            ring = float(np.clip((3.5 - dist[j, i]) / 3.0, 0.0, 1.0))
            t = inner + int(round((rim - inner) * ring))
            t += 1 if lev[j, i] == 0 else (-1 if lev[j, i] == 2 else 0)
            t = max(t, 2)
            c = tuple(img[j, i])
            v[i, y_face - t:y_face - 1, z] = pal(shade(tuple(fill[j, i]), 0.85))
            v[i, y_face - 1, z] = pal(c)
    for i in range(V):                          # 根元（地面へ沈む分）
        col = v[i, :, BURIED]
        for z in range(BURIED):
            v[i, :, z] = col
    size, ncol, nfp = write_pair(name, [('main', v, (0, 0, 0))], pal,
                                 'DCSS の門 %s（%s）。輪郭の縁を厚み %d、内側を %d にした立てた板。根元を %d ボクセル足してある'
                                 '（tools/dcss/gen_dcss_terrain.py）' % (what, rel, rim, inner, BURIED))
    return '%dx%dx%d' % v.shape, ncol, size, '縁 %d 内 %d' % (rim, inner), nfp


# ------------------------------------------------------------------ 床に貼る印・罠（浮き彫り）
def build_decal(name, rel, what, lift=0):
    img, opaque = load_tile(rel, name, max_colours=80)         # 面の色が 3 通り（絵・奥・根元）になる
    img = lift_to(img, opaque, lift or LIFT_OBJ)
    fill = fill_outline(img, opaque)
    pal = Palette(mat_any)
    lev = relief_levels(img, wrap=False)
    h = BURIED + 3
    v = np.zeros((V, V, h), np.int16)
    n = 0
    for y in range(V):
        for x in range(V):
            if not opaque[y, x]:
                continue
            t = 2 + (1 if lev[y, x] == 0 else (-1 if lev[y, x] == 2 else 0))     # 1〜3 ボクセルの厚み
            c = tuple(img[y, x])
            f = tuple(fill[y, x])
            v[x, y, BURIED:BURIED + t - 1] = pal(shade(f, 0.85)) if t > 1 else 0
            v[x, y, BURIED + t - 1] = pal(c)
            v[x, y, :BURIED] = pal(shade(f, 0.7))
            n += 1
    size, ncol, nfp = write_pair(name, [('main', v, (0, 0, 0))], pal,
                                 'DCSS の床の印 %s（%s）。絵の通り上から見て床に貼った厚み 1〜3 の浮き彫り（tools/dcss/gen_dcss_terrain.py）' % (what, rel))
    return '%dx%dx%d' % v.shape, ncol, size, '画素 %d' % n, nfp


# ------------------------------------------------------------------ プラン（全行）
def classify(rel, name):
    t = rel
    if t.startswith('dngn/wall/'):
        return 'wall'
    if t.startswith('dngn/water/') or 'lava' in os.path.basename(t):
        return 'liquid'
    if t.startswith('dngn/doors/'):
        return 'door'
    if t.startswith('dngn/gateways/stone_stairs_up'):
        return 'stairs_up'
    if t.startswith('dngn/gateways/stone_stairs_down') or t.endswith('sealed_stairs_down'):
        return 'stairs_down'
    if 'escape_hatch' in t:
        return 'decal'
    if t.startswith('dngn/gateways/'):
        return 'gate'
    if t.startswith('dngn/altars/'):
        return 'altar'
    if 'fountain' in t:
        return 'fountain'
    if 'cache_of' in t:
        return 'cache'
    if t.startswith('dngn/statues/'):
        return 'statue'
    if t.startswith('dngn/trees/'):
        return 'tree'
    if t.endswith('traps/shaft'):
        return 'pit'
    if t.startswith('dngn/traps/') or t.startswith('dngn/floor/runelight') or t.endswith('transporter_landing_site') or t.endswith('teleporter'):
        return 'decal'
    if t.endswith('abandoned_shop') or t.endswith('transporter'):
        return 'gate'
    if t.startswith('dngn/shops/'):
        return 'shop'
    if t.endswith('dngn/grate'):
        return 'grate'
    if t.startswith('dngn/vaults/orb_dais'):
        return 'dais'
    if t.startswith('dngn/floor/') or t.endswith('decorative_floor'):
        return 'floor'
    raise SystemExit('種類が決まらない絵: %s (%s)' % (t, name))


def read_rows():
    txt = ''.join(l for l in open(FEATURE_MAP, encoding='utf-8') if not l.startswith('#'))
    rows = list(csv.reader(io.StringIO(txt)))
    return rows[1:]


def tile_stem(rel):
    s = os.path.basename(rel)
    s = re.sub(r'\.png$', '', s)
    return re.sub(r'\d+$', '', s).rstrip('_')


def make_plan():
    """プラン: プレハブ名 -> dict(tile, kind, defs[], source)。表の全行（タイルのある行）を覆う。"""
    plan = {}
    owner = {}
    for r in read_rows():
        d, cls, tile, pre = r[0], r[3], r[6], r[8]
        if not tile or d == 'DNGN_UNSEEN':          # 描かない行（地図に出ない疑似地形）
            continue
        tile = re.sub(r'\.png$', '', tile)
        key = d.replace('DNGN_', '', 1)
        name = AB_NAMES.get(key)                    # 名前の決め（a・b だった行）が先。表は manifest.tsv を読んで c にそろえるので、表の名前は古いことがある
        if name is None and cls == 'c':
            m = re.findall(r'dc_[a-z0-9_]+', pre)
            name = m[0] if m else None
        if name is None:
            raise SystemExit('名前が決まらない行: %s (%s)' % (d, tile))
        # 名前の衝突（別のタイルが同じ名前を指す）は、タイルの名前から付け直す
        if name in plan and plan[name]['tile'] != tile:
            name = 'dc_gate_' + tile_stem(tile)
        if name in plan and plan[name]['tile'] != tile:
            raise SystemExit('名前の衝突: %s %s %s' % (name, plan[name]['tile'], tile))
        p = plan.setdefault(name, dict(tile=tile, kind=classify(tile, name), defs=[]))
        p['defs'].append(d)
    return plan


#: 色替えの壁（原作は `%variation` で実行時に色を替える。元の絵が無いので、ベースの絵の色の並びを入れ替えて作る）
RECOLOR = {
    'cyan': lambda r, g, b: (r, np.maximum(g, (b * 0.85).astype(r.dtype)), b),
    'red': lambda r, g, b: (b, g, r),
    'yellow': lambda r, g, b: (b, b, r),
}


def recolor_img(img, how):
    f = RECOLOR[how]
    r, g, b = (img[..., i].astype(np.int32) for i in range(3))
    r2, g2, b2 = f(r, g, b)
    out = np.stack([r2, g2, b2], axis=-1)
    return np.clip(out, 0, 255).astype(img.dtype)


def wall_families(plan):
    """ブランチごとの壁（`dcss_tilefam.branch_wall_names()`: 原作が `tile_default_flv` と `_apply_branch_tile_overrides` で選ぶ壁のタイル）の、
    先頭の 1 枚から作る壁。表の地形の壁（ROCK_WALL・STONE_WALL など）と同じ絵はそちらのプレハブを使う（作らない）。
    原作の生成物（tilegen の出力）が要る。無いときは作らずに申告する。"""
    import dcss_tilefam as tf
    try:
        tiles = tf.Tiles()
    except SystemExit as e:
        print('壁の組を作らない（%s）' % e)
        return {}
    pngs = tf.wall_family_pngs(tiles)
    have = {p['tile'] for p in plan.values()}
    extra = {}
    starts = set()
    for n in tf.branch_wall_names():
        starts.add(tiles.family(tiles.names['TILE_' + n])[0])
    for st in sorted(starts):
        canon = tiles.name_of(st)
        rec = None
        if st in pngs:
            _, tile = pngs[st]
        else:
            m = re.match(r'WALL_ZOT_(CYAN|RED|YELLOW)$', canon)
            if not m:
                print('壁の組の絵が見つからない（作らない）:', canon)
                continue
            tile, rec = pngs[tiles.family(tiles.names['TILE_WALL_ZOT'])[0]][1], m.group(1).lower()
        if tile in have and rec is None:
            continue
        have.add(tile)
        nm = tf.wall_prefab_name(canon)
        if nm in plan:
            nm += '_wall'
        extra[nm] = dict(tile=tile, kind='wall', defs=[], enum=canon, recolor=rec)
    return extra


def floor_families(plan):
    """ブランチごとの床（`rltiles/dc-floor.txt` の FLOOR_* の先頭の 1 枚）。表の地形ではなく意匠（`dcss/terrain/README.md` §6）なので、
    `dungeon_styles` から引く名前 `dc_floor_<列挙名>` で作る。同じ絵が表の行に既にあればそちらの名前を使う（作らない）。"""
    extra = {}
    have = {p['tile'] for p in plan.values()}
    sdir = ''
    for line in open(os.path.join(dv.RLTILES, 'dc-floor.txt'), encoding='utf-8', errors='replace'):
        t = line.split()
        if not t or t[0].startswith('#'):
            continue
        if t[0] == '%sdir':
            sdir = t[1] if len(t) > 1 else ''
            continue
        if t[0].startswith('%') or len(t) < 2 or not t[1].startswith('FLOOR_'):
            continue
        tile, enum = sdir + '/' + t[0], t[1][len('FLOOR_'):]
        if tile in have or not os.path.exists(os.path.join(dv.RLTILES, tile + '.png')):
            continue
        have.add(tile)
        nm = 'dc_floor_' + enum.lower()
        if nm in plan:                                  # 表の行と名前がぶつかる（地形の泥と床の泥など）は `_floor` を付ける
            nm += '_floor'
        extra[nm] = dict(tile=tile, kind='floor', defs=[], enum=enum)
    return extra


def derived_variants(plan):
    """表にない追加（東西向きの扉）を足す。"""
    extra = {}
    for n, p in plan.items():
        if p['kind'] == 'door':
            extra[n + '_ew'] = dict(tile=p['tile'], kind='door', defs=[], ew=True, base=n)
    return extra


# 絵ごとの細かい指定（明るさの持ち上げ・色など）。見て決めた値（理由は README）
FLOOR_LIFT = {'dc_floor_grey_dirt': 62, 'dc_floor_mud': 0}
STAIRS_TINT = {}      # 封印の階段は絵がもともと緑なので色は掛けない


def build_one(name, p, obj_builders):
    kind, rel = p['kind'], p['tile']
    what = os.path.basename(rel)
    if kind == 'wall':
        return build_wall(name, rel, what, recolor=p.get('recolor'))
    if kind == 'floor':
        return build_floor(name, rel, what, lift=FLOOR_LIFT.get(name, 48), style=p.get('style') or ground_style(p.get('enum', 'X')))
    if kind == 'liquid':
        return build_floor(name, rel, what, lift=40, drop=2, liquid=True)
    if kind == 'door':
        mode = 'open' if 'open' in rel else ('broken' if 'broken' in rel else 'closed')
        return build_door(name, rel, what, mode=mode, ew=p.get('ew', False))
    if kind == 'stairs_up':
        return build_stairs_up(name, rel, what, tint=STAIRS_TINT.get(name))
    if kind == 'stairs_down':
        return build_stairs_down(name, rel, what, tint=STAIRS_TINT.get(name))
    if kind == 'gate':
        return build_gate(name, rel, what)
    if kind == 'decal':
        return build_decal(name, rel, what)
    if kind == 'pit':
        return build_pit(name, rel, what)
    if kind in obj_builders:
        return obj_builders[kind](name, rel, what, p)
    raise SystemExit('作り方が無い種類: %s (%s)' % (kind, name))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('names', nargs='*')
    ap.add_argument('--out', default=dv.OUT)
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--tile3', action='store_true', help='作った床を 3x3 に並べた確認用のプレハブ <名前>_3x3 も書く（継ぎ目の確認。--raise-floors と併用）')
    ap.add_argument('--raise-floors', action='store_true', help='床を持ち上げて作る（並べて見る確認用。--out は作業用の一時フォルダにすること）')
    opt = ap.parse_args()
    global RAISE
    RAISE = FLOOR_H if opt.raise_floors else 0
    dv.set_out(opt.out)
    import gen_dcss_objects as go
    plan = make_plan()
    plan.update(derived_variants(plan))
    plan.update(floor_families(plan))
    plan.update(wall_families(plan))
    # 試作から引き継ぐ 1 個（表に無い）
    plan.setdefault('dc_wall_catacombs', dict(tile='dngn/wall/catacombs0', kind='wall', defs=[]))
    todo = [n for n in plan if not opt.names or n in opt.names]
    if opt.list:
        for n in todo:
            p = plan[n]
            print('%-34s %-10s %-44s %s' % (n, p['kind'], p['tile'], ' '.join(p['defs'])[:60]))
        print(len(todo), '個')
        return 0
    manifest = []
    kinds = {}
    fail = []
    for n in todo:
        p = plan[n]
        try:
            size, ncol, nbytes, extra, nfp = build_one(n, p, go.BUILDERS)
        except Exception as e:                      # 1 個の失敗で全体を止めない（最後に一覧する）
            fail.append((n, repr(e)))
            print('失敗 %-30s %r' % (n, e))
            continue
        if opt.tile3 and LAST and p['kind'] in ('floor', 'liquid'):
            v3 = np.tile(LAST[0][0], (3, 3, 1))
            write_pair(n + '_3x3', [('main', v3, (0, 0, -FLOOR_H + RAISE))], LAST[0][1], '3x3 に並べた確認用（tools/dcss/gen_dcss_terrain.py --tile3）')
        kinds[p['kind']] = kinds.get(p['kind'], 0) + 1
        print('%-34s %-10s %-10s 色 %3d  %6d B  %-18s 接地マス %d' % (n, p['kind'], size, ncol, nbytes, extra, nfp))
        manifest.append((n, p['kind'], p['tile'], ' '.join(p['defs'])))
    if dv.REDUCED:
        print('色を %d へ詰めた絵（元の色数）: %s' % (dv.MAX_COLOURS, ', '.join('%s=%d' % r[:2] for r in dv.REDUCED[:40])), '…' if len(dv.REDUCED) > 40 else '')
    if not opt.names:
        with open(os.path.join(dv.OUT, 'manifest.tsv'), 'w', encoding='utf-8', newline='\n') as fp:
            fp.write('# 作ったプレハブの一覧（tools/dcss/gen_dcss_terrain.py が書く）: 名前、種類、元の絵、DCSS の地形定義\n')
            for m in sorted(manifest):
                fp.write('\t'.join(m) + '\n')
        dv.write_used(os.path.join(dv.OUT, 'tiles_used.tsv'))
    if dv.CROPPED:
        print('32x32 に切った絵（元の寸法）:', ', '.join('%s=%dx%d' % (r[1], r[2][0], r[2][1]) for r in dv.CROPPED))
    print('種類ごと:', kinds, '合計', sum(kinds.values()), '失敗', len(fail))
    return 1 if fail else 0


if __name__ == '__main__':
    sys.exit(main())
