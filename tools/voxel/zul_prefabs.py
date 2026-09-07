# -*- coding: utf-8 -*-
"""ズル（町 5 番）の作り込み — 素材の生成器。

`gen_prefabs.py` の末尾から `register(globals())` で呼ばれ、接尾辞 `_zul` の素材を
`CATALOG` へ足す。**既存の素材には 1 バイトも触らない**（別の名前で足すだけ）。

ズルは**山を刳り抜いた谷**で、町マップの 86% が山、敷地は 1 件も無い。店も塔も
山肌に開いた穴なので、この意匠が受け持つのは**岩肌・地面・門・谷に置く構造物**である
（建物の壁と屋根は 1 枚も要らない）。

## 作りの規律（`gen_prefabs.py` 冒頭と同じ）
- 無作為な表面ノイズは入れない。彫りは 1 ボクセルの深さ。色は塊ごとに 1 色
- 接地層（z=0）の平面方向は縮めない。**+y は南＝カメラ側**
- 色は共通の登録簿（`C`）を使い、足りない色だけ `reg_local`。
  **`reg_local` の名前は材質の語で始める**（`crag_` `ash_` `iron_` `cloth_` `lava_` `grass_`）
  ——`palette_materials.classify` は「名前が材質の語で始まるか」でしか当てない


## 岩山の段の高さ（**麓だけは動かせない**）
`crag_low` は 44 ボクセル（1.375 マス）のままにする。町の読み取り側が
**看板の手前 3 マスを麓の段へ下げて**（`town_plan.cpp` の `crag_lowered`）看板が
岩に隠れないようにしているので、麓を高くすると 2026-08-10 に気づいた
「看板が南の地形に隠れる」不具合がそのまま戻る。中腹から上は高くしてよい。
"""
from __future__ import annotations

import math

import numpy as np

G = None   #: gen_prefabs の名前空間（register で入る）
C = None   #: 色の名前 → 索引
V = 32

#: 岩山の段の高さ（ボクセル）。**low は 44 から動かさない**（上の註記）。
#: 中腹から上は共通の `crag`（78 / 118 / 158）より高くしてある——谷の縁が 1.4 マスの
#: 低い段のままだと「庭の石垣」に見えて崖にならない（§7 の記録）。
CRAG_H = {'low': 44, 'mid': 92, 'high': 140, 'peak': 184}

#: **層理は絶対の高さで決める**（地層は水平なので、隣のマスと必ず揃う）。
#: (この高さから, 色) の並び。マスごとに乱数で決めると、隣と段が食い違って縞が散る。
CRAG_BANDS = (
    (0, 'crag_zl_basdd'),   #: 谷底に近い暗い玄武岩
    (14, 'crag_zl_basd'),
    (30, 'crag_zl_red'),    #: 鉄気の薄い赤い層（4 ボクセル）
    (34, 'crag_zl_basd'),
    (48, 'crag_zl_bas'),
    (66, 'crag_zl_tuf'),    #: 凝灰岩の明るい層
    (84, 'crag_zl_sul'),    #: 硫黄の吹いた薄い層（4 ボクセル）
    (88, 'crag_zl_bas'),
    (112, 'crag_zl_tuf'),
    (130, 'crag_zl_basd'),
    (152, 'crag_zl_bas'),
    (170, 'crag_zl_basl'),
)


# ------------------------------------------------------------------ 形の道具
def vol(h=32, w=None, d=None):
    """int16 の空の体積（`reg_local` の 1000 番台が入る）。"""
    return np.zeros((w or V, d or V, h), np.int16)


def box(v, x0, x1, y0, y1, z0, z1, colour):
    """直方体を塗る（範囲は配列に切り詰める）。**高さの外へ書いた分は黙って消える。**"""
    X, Y, Z = v.shape
    x0, x1 = max(0, x0), min(X, x1)
    y0, y1 = max(0, y0), min(Y, y1)
    z0, z1 = max(0, z0), min(Z, z1)
    if (x0 < x1) and (y0 < y1) and (z0 < z1):
        v[x0:x1, y0:y1, z0:z1] = colour


def disc(v, cx, cy, r, z0, z1, colour, hollow=0.0):
    """縦の円柱（z 軸）。`hollow` を渡すと内側を空ける（輪）。"""
    X, Y, Z = v.shape
    xs = np.arange(X)[:, None] + 0.5 - cx
    ys = np.arange(Y)[None, :] + 0.5 - cy
    d2 = (xs * xs) + (ys * ys)
    m = d2 <= (r * r)
    if hollow > 0:
        m &= d2 >= (hollow * hollow)
    z0, z1 = max(0, z0), min(Z, z1)
    if z0 < z1:
        v[:, :, z0:z1][m] = colour


def log_x(v, cy, cz, r, x0, x1, colour):
    """x 軸に沿って横たわる丸太（断面は y-z の円）。"""
    X, Y, Z = v.shape
    ys = np.arange(Y)[:, None] + 0.5 - cy
    zs = np.arange(Z)[None, :] + 0.5 - cz
    m = (ys * ys) + (zs * zs) <= (r * r)
    x0, x1 = max(0, x0), min(X, x1)
    for x in range(x0, x1):
        v[x][m] = colour


def log_y(v, cx, cz, r, y0, y1, colour):
    """y 軸に沿って横たわる丸太。"""
    X, Y, Z = v.shape
    xs = np.arange(X)[:, None] + 0.5 - cx
    zs = np.arange(Z)[None, :] + 0.5 - cz
    m = (xs * xs) + (zs * zs) <= (r * r)
    y0, y1 = max(0, y0), min(Y, y1)
    for y in range(y0, y1):
        v[:, y, :][m] = colour


def ball(v, cx, cy, cz, r, colour, squash=1.0):
    """球（`squash` < 1 で上下に潰す）。"""
    X, Y, Z = v.shape
    xs = np.arange(X)[:, None, None] + 0.5 - cx
    ys = np.arange(Y)[None, :, None] + 0.5 - cy
    zs = (np.arange(Z)[None, None, :] + 0.5 - cz) / squash
    v[(xs * xs) + (ys * ys) + (zs * zs) <= (r * r)] = colour


def cone(v, cx, cy, z0, z1, r0, r1, colour):
    """円錐台（z0 で半径 r0、z1 で r1）。"""
    for z in range(max(0, z0), min(v.shape[2], z1)):
        t = (z - z0) / float(max(1, z1 - z0))
        disc(v, cx, cy, r0 + ((r1 - r0) * t), z, z + 1, colour)


def octagon(v, x0, x1, y0, y1, z0, z1, colour, cut=None):
    """四角の 4 隅を落とした八角の柱。

    32 ボクセルで `disc` の柱を立てると**糸巻きに見える**（モリバントの記録 §7.1）。
    同じ太さでも八角にすると石の柱に読める。
    """
    w = min(x1 - x0, y1 - y0)
    cut = cut if cut is not None else max(1, w // 4)
    box(v, x0, x1, y0, y1, z0, z1, colour)
    for i in range(cut):
        k = cut - i
        box(v, x0 + i, x0 + i + 1, y0, y0 + k, z0, z1, 0)
        box(v, x0 + i, x0 + i + 1, y1 - k, y1, z0, z1, 0)
        box(v, x1 - i - 1, x1 - i, y0, y0 + k, z0, z1, 0)
        box(v, x1 - i - 1, x1 - i, y1 - k, y1, z0, z1, 0)


def one(v, z0=0):
    return [('main', v, (0, 0, z0))]


def ground(v, h):
    """地面のタイル（天面が z=0 に来るよう origin_z = -h）。"""
    return [('main', v, (0, 0, -h))]


def K(name):
    return C[name]


def rng_for(name):
    return G['rng_for'](name)


def pick(rng, shades, weights=None):
    return int(rng.choice(shades, p=weights))


def static_part(motion=None, wind_k=0.0):
    return G['static_part'](motion, wind_k)


def spin_parts(wheels, frame='frame'):
    """静止の骨組み ＋ 回る輪。`wheels` は [(部品名, ピボット, 軸, 速さ, 位相), …]。"""
    parts = [{'name': frame, 'voxels': frame, 'grounded': True,
              'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    for name, pivot, axis, speed, phase in wheels:
        parts.append({'name': name, 'voxels': name, 'parent': frame, 'grounded': False,
                      'pivot': [float(pivot[0]), float(pivot[1]), float(pivot[2])],
                      'motion': {'kind': 'rotate', 'axis': axis,
                                 'speed': float(speed), 'phase': float(phase)},
                      'wind_k': 0.0})
    return parts


def swing_parts(names_pivots, amplitude=6.0, period=2.6, frame='frame'):
    """静止の骨組み ＋ 吊り下げた部品（振り子）。"""
    parts = [{'name': frame, 'voxels': frame, 'grounded': True,
              'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    for i, (name, pivot) in enumerate(names_pivots):
        parts.append({'name': name, 'voxels': name, 'parent': frame, 'grounded': False,
                      'pivot': [float(pivot[0]), float(pivot[1]), float(pivot[2])],
                      'motion': {'kind': 'pendulum', 'axis': 'x' if (i % 2 == 0) else 'y',
                                 'amplitude': float(amplitude),
                                 'period': float(period) + (0.35 * i),
                                 'phase': 0.9 * i, 'damping': 0.0},
                      'wind_k': 0.0})
    return parts


def wind_parts(names, frame='frame', wind_k=1.0):
    """静止の骨組み ＋ 風になびく布。"""
    parts = [{'name': frame, 'voxels': frame, 'grounded': True,
              'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    for name in names:
        parts.append({'name': name, 'voxels': name, 'parent': frame, 'grounded': False,
                      'motion': {'kind': 'wind'}, 'wind_k': float(wind_k)})
    return parts


# ------------------------------------------------------------------ 色
def _palette():
    """この意匠だけの色。**名前は材質の語で始める**（冒頭の註記）。"""
    rl = G['reg_local']
    #! 玄武岩（谷の岩肌）。共通の `crag`（134,126,114）は乾いた灰褐で、火山の谷には明るすぎる。
    rl('crag_zl_bas', (96, 93, 100)); rl('crag_zl_basl', (124, 121, 130))
    rl('crag_zl_basd', (68, 66, 73)); rl('crag_zl_basdd', (46, 45, 51))
    #! 凝灰岩の層（岩肌の帯）。玄武岩との明度差で層理を読ませる。
    rl('crag_zl_tuf', (134, 124, 106)); rl('crag_zl_tufl', (160, 150, 130))
    #! 鉄気の赤い層と硫黄の吹き出し（割れ目に差す帯）。
    rl('crag_zl_red', (112, 70, 54)); rl('crag_zl_sul', (162, 148, 76))
    #! 谷底の火山灰と黒い礫。
    rl('ash_zl_g', (62, 59, 57)); rl('ash_zl_gl', (78, 74, 71)); rl('ash_zl_gd', (48, 46, 45))
    #! 焼けた礫（溶岩のほとり）と軽石。
    rl('ash_zl_cin', (86, 52, 42)); rl('ash_zl_pum', (116, 111, 104))
    #! 硬い草（火山の谷に生える草。共通の `turf` より青みを落として乾かす）。
    rl('grass_zl_h', (98, 108, 62)); rl('grass_zl_hd', (74, 82, 48))
    #! 幟と天幕の布（赤と黄土）。
    rl('cloth_zl_r', (176, 62, 44)); rl('cloth_zl_o', (198, 146, 66))
    #! 熱い溶岩の芯（共通の `lava_y` より白い）。滝と炉に使う。
    rl('lava_zl_w', (255, 236, 168))
    #! 磨いた鋼（共通の表に `steel_l` が無い）。
    rl('iron_zl_l', (208, 214, 224))


# =================================================================== 岩山
def _columns(rng, seeds=7):
    """柱状節理の割り付け。**マスを不定形の柱へ分ける**（各柱の天端が別の高さになる）。

    返すのは `(所属, 継ぎ目)`。所属は 32×32 の柱番号、継ぎ目は柱の境の 1 ボクセル。
    """
    pts = []
    for _ in range(seeds):
        pts.append((float(rng.uniform(-3.0, V + 3.0)), float(rng.uniform(-3.0, V + 3.0))))
    xs = np.arange(V)[:, None] + 0.5
    ys = np.arange(V)[None, :] + 0.5
    d = np.stack([((xs - px) ** 2) + ((ys - py) ** 2) for px, py in pts], axis=0)
    own = np.argmin(d, axis=0)
    #: 継ぎ目＝いちばん近い種と 2 番目が競っている所（柱の境）。
    srt = np.sort(d, axis=0)
    #! **継ぎ目は細く。**9.0 で採ると 1 マスに何本もの黒い筋が走り、遠目に炭素繊維の
    #! 織り目に見えた（§7 の記録）。5.0 なら柱の境が 1 本だけ通る。
    seam = (srt[1] - srt[0]) < 5.0
    return own, seam


def crag_zul(name):
    """**ズルの岩肌** — 縦に割れた玄武岩の柱。

    共通の `crag()` は「天面を斜めに削いで角を欠いた塊」で、谷底から見ると**砕けた
    瓦礫の壁**に読める。ズルは山を刳り抜いた谷なので、**垂直の割れ**が要る——
    マスを 6〜8 本の柱へ分け、柱ごとに天端の高さを変え、境に暗い継ぎ目を通す。
    隣のマスとは割り付けが違うので、継ぎ目が格子にならない。

    段は 4 つ（麓 1.375 / 中腹 3.0 / 上部 4.6 / 峰 6.1 マス）。**麓だけは 44 ボクセルに
    据え置く**（冒頭の註記。看板が隠れる）。中腹から上を高くしたのは、谷の縁が 1.4 マスの
    低い段のままだと「庭の石垣」に見えて、崖にならないからである。

    層理は横帯（下ほど暗い玄武岩 → 明るい凝灰岩の帯 → また玄武岩）。割れ目には
    鉄気の赤と硫黄の黄を 1 本ずつ差す——**帯は塊で置く**（1 ボクセルずつ振ると砂粒になる）。
    """
    tier = name.split('_')[1]
    h = CRAG_H[tier]
    rng = rng_for(name)
    low = (tier == 'low')
    #! ここまではマスいっぱい（足元は削らない。罠 5。「入れない所」の読みは足元で決まる）。
    base = min(h - 8, 14 if low else 26)

    own, seam = _columns(rng, seeds=int(rng.integers(4, 7)))
    n = int(own.max()) + 1
    #! 柱ごとの天端。**1 本は満高**にして、残りを落とす（稜線が尖る）。
    #! **麓は落とし幅を狭くする**——1.4 マスの段で大きく落とすと、崖ではなく
    #! 崩れた狭間胸壁に見える（実測。§7 の記録）。
    #! **上の段ほど削ぎ幅を狭くする。**麓は 1.4 マスなので大きく削ると崩れた狭間胸壁に
    #! 見え、峰は 5.75 マスなので大きく削ると**針山**になる（実測。§7 の記録）。
    #! 谷から見える崖（麓・中腹）で形を作り、奥の山は塊のまま置く。
    lo_hi = {'low': (0.05, 0.20), 'mid': (0.08, 0.26),
             'high': (0.06, 0.18), 'peak': (0.04, 0.14)}[tier]
    drop = rng.uniform(lo_hi[0], lo_hi[1], size=n)
    drop[int(rng.integers(0, n))] = 0.0
    tops = np.rint(h - (drop[own] * h)).astype(np.int32)
    #! 柱の天端をさらに欠く（4×4 の塊で。1 ボクセル単位だと毛羽立つ）。
    chip = rng.integers(0, 3 if low else 4, size=((V + 3) // 4, (V + 3) // 4))
    tops -= np.repeat(np.repeat(chip, 4, 0), 4, 1)[:V, :V]
    tops = np.clip(tops, base, h).astype(np.int32)

    # --- 層理（z の帯）。**絶対の高さで置く**ので隣のマスと必ず揃う ---
    column = np.zeros(h, np.int16)
    for z0, cname in CRAG_BANDS:
        if z0 < h:
            column[z0:] = K(cname)
    zgrid = np.arange(h)[None, None, :]
    solid = zgrid < tops[:, :, None]
    v = np.where(solid, column[None, None, :], 0).astype(np.int16)

    # --- 柱ごとの明暗（同じ柱は 1 色ぶんずらす。塊ごとに 1 色の規律） ---
    up = {K('crag_zl_basdd'): K('crag_zl_basd'), K('crag_zl_basd'): K('crag_zl_bas'),
          K('crag_zl_bas'): K('crag_zl_basl'), K('crag_zl_tuf'): K('crag_zl_tufl')}
    down = {K('crag_zl_basd'): K('crag_zl_basdd'), K('crag_zl_bas'): K('crag_zl_basd'),
            K('crag_zl_basl'): K('crag_zl_bas'), K('crag_zl_tufl'): K('crag_zl_tuf')}
    shift = rng.choice([-1, 0, 0, 1], size=n)
    for i in range(n):
        if shift[i] == 0:
            continue
        mask = (own == i)
        col = v[mask]
        table = up if (shift[i] > 0) else down
        for src, dst in table.items():
            col[col == src] = dst
        v[mask] = col

    # --- 柱の継ぎ目。**色で通す**（刳ると面が倍に増えて四角形が跳ね上がる） ---
    seam3 = np.repeat(seam[:, :, None], h, axis=2) & (v != 0)
    seam3[:, :, :base] = False
    #! **層の色から 1 段だけ暗くする**（真っ黒で通すと縞が主役になる）。
    seam_v = v[seam3]
    for src, dst in down.items():
        seam_v[seam_v == src] = dst
    seam_v[seam_v == K('crag_zl_basdd')] = K('crag_zl_basdd')
    v[seam3] = seam_v
    # --- 天端だけは実際に 1 段欠く（継ぎ目の所で柱が割れて見える） ---
    #! **谷から見える 2 段だけ**に留める。奥の山（上部・峰）まで刻むと、
    #! 遠景が黒い針の林になった（§7 の記録）。
    if tier in ('low', 'mid'):
        for x in range(V):
            for y in range(V):
                t = int(tops[x, y])
                if seam[x, y] and (t - 1 > base):
                    v[x, y, t - 1] = 0
                    tops[x, y] = t - 1

    # --- 天面。**明るくしない**（明るいと登れる所に見える）。地衣はまばらに塊で ---
    #: **層の色から離さない**。派手な色を天面へ散らすと、真上から見て岩が斑の菓子に見えた。
    face = rng.choice([K('crag_zl_basd'), K('crag_zl_basdd'), K('lichen')],
                      size=((V + 3) // 4, (V + 3) // 4), p=[0.56, 0.34, 0.10])
    face = np.repeat(np.repeat(face, 4, 0), 4, 1)[:V, :V]
    for x in range(V):
        for y in range(V):
            t = int(tops[x, y])
            if t > 0:
                v[x, y, t - 1] = face[x, y]

    #! **麓の変種には人の手を入れる**（山肌が町の壁なので、そこに人の暮らしが要る）。
    #! ただし**6 枚のうち 1 枚だけ**——2 枚に入れたら谷じゅうの岩に木が生えて
    #! 工事現場に見えた（§7 の記録）。
    if low and name.endswith('_04'):
        #: **岩を刳った回廊**。南の面（谷に向いた面）を掘り込み、頭に岩の庇を残す。
        #: 庇に見せるには**下を彫らないと駄目**——上に石を足すだけでは平らな縁にしかならない。
        top = int(np.max(tops))
        box(v, 3, 29, 24, V, 7, top - 7, 0)                      # 掘った所
        box(v, 0, V, 22, V, top - 7, top, K('crag_zl_bas'))      # 残した庇
        box(v, 3, 29, 24, V, 7, 9, K('board_d'))                 # 敷いた板
        for x in (5, 13, 21, 27):                                # 庇を支える木の柱
            box(v, x, x + 2, 28, 30, 9, top - 7, K('timber_d'))
        box(v, 3, 29, 28, 29, 18, 20, K('log_d'))                # 手すりの丸太
    return one(v)


def alpine_zul(name):
    """岩の天面に「ちらほら」生えるもの。ズルは火山なので**硫黄の結晶と焦げた灌木**。

    ======  ==========================================================
    01      硫黄の結晶（割れ目に吹いた黄色い針）
    02      焦げた低い灌木（黒い枝に硬い葉が少し）
    03      岩に張り付いた地衣と苔（いちばん低い）
    ======  ==========================================================
    """
    kind = int(name[-2:]) if name[-2:].isdigit() else 1
    rng = rng_for(name)
    height = {1: 12, 2: 15, 3: 6}[kind]
    v = vol(h=height)
    for _ in range(int(rng.integers(4, 8))):                  # 根元の砂利
        x, y = int(rng.integers(8, 22)), int(rng.integers(8, 22))
        box(v, x, x + 3, y, y + 3, 0, 1, K('grit'))
    if kind == 1:
        for _ in range(int(rng.integers(5, 9))):
            bx, by = int(rng.integers(9, 22)), int(rng.integers(9, 22))
            tall = int(rng.integers(4, height - 2))
            box(v, bx, bx + 2, by, by + 2, 0, tall, K('crag_zl_sul'))
            box(v, bx, bx + 1, by, by + 1, tall, tall + 2, K('sulf'))
    elif kind == 2:
        for _ in range(int(rng.integers(3, 6))):
            bx, by = int(rng.integers(10, 21)), int(rng.integers(10, 21))
            box(v, bx, bx + 2, by, by + 2, 0, 7, K('char'))
            for _ in range(5):
                x = min(V - 2, max(0, bx + int(rng.integers(-4, 5))))
                y = min(V - 2, max(0, by + int(rng.integers(-4, 5))))
                z = int(rng.integers(5, height - 2))
                box(v, x, x + 2, y, y + 2, z, z + 2, K('char_l'))
                if rng.random() < 0.4:
                    box(v, x, x + 1, y, y + 1, z + 2, z + 3, K('grass_zl_hd'))
    else:
        for _ in range(int(rng.integers(6, 11))):
            x, y = int(rng.integers(6, 24)), int(rng.integers(6, 24))
            r = int(rng.integers(3, 6))
            box(v, x, x + r, y, y + r, 0, 2, K('lichen') if rng.random() < 0.6 else K('moss_d'))
    return one(v)


# =================================================================== 地面
def _grit_face(rng, face, base, speck, patch, pebble=None):
    """**礫の面**を作る。塊の大きさを 2 段にするのが要点である。

    初版は 4 ボクセルの塊 1 段で敷いたので、谷底が**灰色の迷彩**に見えた（§7 の記録）。
    ここでは (1) 2 ボクセルの細かい粒で全面をざらつかせ、(2) その上に 6〜12 ボクセルの
    平らな岩盤の露出を 2〜3 枚だけ置く。粒と岩盤で縮尺が離れているので、
    近くでは砂利、遠目では岩の面に見える。

    `base` 素の色 ／ `speck` 粒の色の並び ／ `patch` 岩盤の色の並び ／ `pebble` 小石。
    """
    face[:, :] = base
    #! (1) 細かい粒（2 ボクセル）。**明暗差は 1 段ぶんに留める**。
    n = (V + 1) // 2
    grid = rng.choice(list(speck) + [0], size=(n, n),
                      p=[0.22, 0.24] + [0.54])
    big = np.repeat(np.repeat(grid, 2, 0), 2, 1)[:V, :V]
    face[big > 0] = big[big > 0]
    #! (2) 岩盤の露出（平らな面。2〜3 枚だけ）。
    for _ in range(int(rng.integers(2, 4))):
        w = int(rng.integers(6, 13))
        d = int(rng.integers(5, 12))
        x = int(rng.integers(0, V - w))
        y = int(rng.integers(0, V - d))
        face[x:x + w, y:y + d] = pick(rng, list(patch))
    #! (3) 小石（1〜2 ボクセルの点）。
    if pebble is not None:
        for _ in range(int(rng.integers(8, 16))):
            x, y = int(rng.integers(0, V - 1)), int(rng.integers(0, V - 1))
            r = int(rng.integers(1, 3))
            face[x:x + r, y:y + r] = pick(rng, list(pebble))


def _cracks(rng, face, colour, lines=3):
    """岩を割る筋。**マスの縁から縁まで通す**（途中で切れると引っ掻き傷に見える）。"""
    for _ in range(lines):
        if rng.random() < 0.5:
            y = float(rng.integers(3, V - 3))
            for x in range(V):
                yy = int(round(y + (2.2 * math.sin((x / 9.0) + rng.random()))))
                if 0 <= yy < V:
                    face[x, yy] = colour
        else:
            x = float(rng.integers(3, V - 3))
            for y in range(V):
                xx = int(round(x + (2.2 * math.sin((y / 9.0) + rng.random()))))
                if 0 <= xx < V:
                    face[xx, y] = colour


def ground_path_zul(name):
    """**岩を削った道** — 谷を通る道は岩盤を削って通してある。轍が 2 本、砂利が寄る。"""
    rng = rng_for(name)
    h = 3
    v = vol(h=h)
    box(v, 0, V, 0, V, 0, h, K('ash_zl_gd'))
    face = np.zeros((V, V), np.int16)
    _grit_face(rng, face, K('ash_zl_g'),
               [K('ash_zl_gl'), K('ash_zl_gd')],
               [K('crag_zl_basd'), K('ash_zl_gd')],
               [K('gravel_d'), K('ash_zl_gl')])
    _cracks(rng, face, K('ash_zl_gd'), lines=2)
    #! 轍 2 本。**マスの縁では必ず同じ位置**（10 と 21）にして、隣と繋がるようにする。
    for cx in (10, 21):
        for y in range(V):
            xx = cx + int(round(1.8 * math.sin(y / 7.0)))
            for k in range(-1, 2):
                if 0 <= xx + k < V:
                    face[xx + k, y] = K('char') if (k == 0) else K('ash_zl_gd')
    v[:, :, h - 1] = face
    return ground(v, h)


def ground_turf_zul(name):
    """**谷底の礫地** — 黒い火山礫と灰。硬い草が割れ目にだけ生え、軽石が白く散る。"""
    rng = rng_for(name)
    h = 3
    v = vol(h=h)
    box(v, 0, V, 0, V, 0, h, K('ash_zl_gd'))
    face = np.zeros((V, V), np.int16)
    _grit_face(rng, face, K('ash_zl_g'),
               [K('ash_zl_gl'), K('ash_zl_gd')],
               [K('crag_zl_basd'), K('ash_zl_gl')],
               [K('ash_zl_pum'), K('gravel_d')])
    _cracks(rng, face, K('ash_zl_gd'), lines=3)
    #! 割れ目の硬い草。**筋で置く**（四角い塊で置くと芝の切れ端に見える）。
    for _ in range(int(rng.integers(2, 5))):
        bx, by = int(rng.integers(2, V - 8)), int(rng.integers(2, V - 8))
        for t in range(int(rng.integers(4, 9))):
            x = min(V - 1, bx + t)
            y = min(V - 1, by + int(round(1.6 * math.sin(t / 2.2))))
            face[x, y] = K('grass_zl_h')
            if y + 1 < V:
                face[x, y + 1] = K('grass_zl_hd')
    v[:, :, h - 1] = face
    return ground(v, h)


def ground_ash_zul(name):
    """**灰の平地**（区画の地面）— 細かい火山灰。風紋が薄く寄り、足跡が残る。"""
    rng = rng_for(name)
    h = 3
    v = vol(h=h)
    box(v, 0, V, 0, V, 0, h, K('ash_zl_gd'))
    face = np.zeros((V, V), np.int16)
    _grit_face(rng, face, K('ash_zl_g'),
               [K('ash_zl_gl'), K('ash_zl_gd')],
               [K('ash_zl_gd'), K('ash_zl_gl')])
    ang = float(rng.uniform(0.0, math.pi))                    # 風紋（細く・薄く）
    for x in range(V):
        for y in range(V):
            t = ((x * math.cos(ang)) + (y * math.sin(ang))) / 6.0
            if math.sin(t) > 0.86:
                face[x, y] = K('ash_zl_gl')
    for _ in range(int(rng.integers(3, 7))):                  # 足跡
        x, y = int(rng.integers(2, V - 4)), int(rng.integers(2, V - 4))
        face[x:x + 2, y:y + 3] = K('ash_zl_gd')
    _cracks(rng, face, K('ash_zl_gd'), lines=2)
    v[:, :, h - 1] = face
    return ground(v, h)


def ground_cinder_zul(name):
    """**焼けた礫**（区画の地面）— 溶岩のほとり。赤黒い焼け石に黒曜の欠片が混じる。"""
    rng = rng_for(name)
    h = 3
    v = vol(h=h)
    box(v, 0, V, 0, V, 0, h, K('ash_zl_gd'))
    face = np.zeros((V, V), np.int16)
    _grit_face(rng, face, K('ash_zl_cin'),
               [K('emb'), K('ash_zl_gd')],
               [K('ash_zl_gd'), K('emb_d')],
               [K('obs_l'), K('emb_l')])
    _cracks(rng, face, K('emb_d'), lines=2)
    v[:, :, h - 1] = face
    return ground(v, h)


def ground_causeway_zul(name):
    """**溶岩の堀に渡した石の桟**（1 マス）。切石を並べ、両脇に鉄の帯を打ってある。"""
    rng = rng_for(name)
    h = 4
    v = vol(h=h)
    box(v, 0, V, 0, V, 0, h, K('crag_zl_basd'))
    for by in range(0, V, 8):                                 # 渡した切石（南北へ 4 枚）
        shade = pick(rng, [K('crag_zl_tuf'), K('ash_zl_gl'), K('crag_zl_bas')],
                     [0.42, 0.32, 0.26])
        box(v, 1, V - 1, by + 1, by + 7, h - 1, h, shade)
    box(v, 0, 2, 0, V, h - 1, h + 2, K('rust'))               # 鉄の帯（両脇）
    box(v, V - 2, V, 0, V, h - 1, h + 2, K('rust'))
    return ground(v, h)


def grass_zul(name):
    """硬い草の株（`tuft`）。**背は伸ばさない**——火山の谷の草は低い。"""
    rng = rng_for(name)
    height = 14
    v = vol(h=height)
    for _ in range(int(rng.integers(5, 9))):
        bx, by = int(rng.integers(6, V - 6)), int(rng.integers(6, V - 6))
        tall = int(rng.integers(6, height - 2))
        lean_x, lean_y = int(rng.integers(-2, 3)), int(rng.integers(-2, 3))
        blade = K('grass_zl_h') if rng.random() < 0.6 else K('grass_zl_hd')
        for z in range(tall):
            t = z / float(max(1, tall))
            x = min(V - 1, max(0, bx + int(round(lean_x * t))))
            y = min(V - 1, max(0, by + int(round(lean_y * t))))
            v[x, y, z] = blade
    return one(v)


# =================================================================== 門と灯り
def _lantern(v, cx, cy, z0, cage=K):
    """鉄の籠に入れた炭火（門と街灯で共通の形）。"""
    box(v, cx - 3, cx + 3, cy - 3, cy + 3, z0, z0 + 1, K('iron'))
    for (ax, ay) in ((cx - 3, cy - 3), (cx + 2, cy - 3), (cx - 3, cy + 2), (cx + 2, cy + 2)):
        box(v, ax, ax + 1, ay, ay + 1, z0, z0 + 7, K('iron'))
    box(v, cx - 3, cx + 3, cy - 3, cy + 3, z0 + 6, z0 + 7, K('iron_l'))
    box(v, cx - 2, cx + 2, cy - 2, cy + 2, z0 + 1, z0 + 4, K('emb_l'))


def arch_zul(name):
    """**岩を穿った入口の額縁** — 山肌の穴に木の梁と控え柱を入れ、上に岩の庇を出す。

    ズルの店は敷地を持たない（山肌の穴そのもの）ので、門は「穴の縁を人が補強した」形にする。
    石のアーチ（`arch_stone`）だと山肌に西洋の門が生えて見えた。

    脇に鉄の輪と鎖、梁から吊り灯（火は `arch_flame_zul`）。高さ 1.8 マス。
    """
    rng = rng_for(name)
    h = 58
    v = vol(h=h)
    #! 控え柱（両脇）。**丸太を 2 本合わせて岩へ噛ませる**。
    for x0 in (0, V - 7):
        box(v, x0, x0 + 7, 12, 20, 0, 44, K('crag_zl_basd'))          # 岩の残し
        box(v, x0 + 1, x0 + 5, 13, 19, 0, 42, K('log_d'))             # 控えの丸太
        box(v, x0 + 1, x0 + 5, 13, 19, 20, 22, K('rust'))             # 鉄の帯
        box(v, x0 + 1, x0 + 5, 13, 19, 34, 36, K('rust'))
    box(v, 0, V, 12, 21, 42, 48, K('log'))                            # 梁（まぐさ）
    box(v, 0, V, 12, 21, 46, 47, K('log_d'))
    for x in range(2, V - 2, 6):                                      # 梁を留めた楔
        box(v, x, x + 2, 19, 21, 43, 46, K('timber_d'))
    #! 岩の庇（梁の上へせり出す）。**2 段でせり出す**——1 段だと板を載せただけに見える。
    box(v, 0, V, 10, 22, 48, 53, K('crag_zl_bas'))
    box(v, 0, V, 8, 24, 53, 58, K('crag_zl_basl'))
    for _ in range(5):                                                # 庇の欠け
        x = int(rng.integers(0, V - 4))
        box(v, x, x + 4, 8, 10, 53, 58, 0)
    box(v, 12, 20, 14, 19, 40, 42, K('iron'))                         # 吊り金具
    _lantern(v, 16, 16, 33)                                           # 吊り灯
    box(v, 15, 17, 15, 17, 40, 41, K('iron'))
    for cy in (13, 20):                                               # 脇の鉄の輪と鎖
        box(v, 3, 5, cy, cy + 1, 26, 28, K('rust'))
        box(v, V - 5, V - 3, cy, cy + 1, 26, 28, K('rust'))
    return one(v)


def arch_zul_ew(name):
    return G['rotate_parts_90'](arch_zul('arch_zul'))


def arch_flame_zul(name):
    """門の吊り灯の火（夜だけ重ねる。`gate_flame`）。"""
    v = vol(h=44)
    box(v, 14, 18, 14, 18, 34, 38, K('fl_mid'))
    box(v, 15, 17, 15, 17, 36, 41, K('fl_core'))
    box(v, 13, 19, 13, 19, 33, 35, K('fl_edge'))
    return one(v)


def lamp_post_zul(name):
    """**街灯＝鉄の篝火台**。谷は溶岩の照り返しで赤いので、灯りも炭火にする。高さ 2.1 マス。"""
    v = vol(h=68)
    box(v, 12, 20, 12, 20, 0, 3, K('crag_zl_basd'))          # 石の据え
    box(v, 14, 18, 14, 18, 2, 4, K('flag_d'))
    box(v, 15, 17, 15, 17, 3, 52, K('iron'))                 # 鉄の柱
    for z in (18, 34):                                       # 錆の帯
        box(v, 15, 17, 15, 17, z, z + 2, K('rust'))
    for (dx, dy) in ((-4, 0), (4, 0), (0, -4), (0, 4)):      # 支え
        box(v, 16 + dx, 16 + dx + 1, 16 + dy, 16 + dy + 1, 46, 52, K('iron'))
    disc(v, 16, 16, 6.5, 52, 58, K('iron'), hollow=5.0)      # 炭籠
    disc(v, 16, 16, 5.5, 52, 54, K('char'))
    disc(v, 16, 16, 4.5, 54, 57, K('emb_l'))
    disc(v, 16, 16, 7.0, 58, 60, K('iron_l'))                # 縁
    return one(v)


def lamp_flame_zul(name):
    """街灯の炭火（夜だけ重ねる）。"""
    v = vol(h=72)
    disc(v, 16, 16, 4.2, 55, 60, K('fl_mid'))
    disc(v, 16, 16, 2.6, 58, 65, K('fl_core'))
    disc(v, 16, 16, 5.4, 54, 56, K('fl_edge'))
    return one(v)


# =================================================================== 大物
def headframe_zul(name):
    """**鉱山の巻き上げ櫓**（2×2 マス・高さ 6 マス）。町でいちばん高い人工物。

    四本の脚を内へ絞った木の櫓に、鉄の索輪（**回る**）を載せる。索は下の巻き上げ小屋へ
    降り、櫓の足元には鉱車の軌道と鉱石の落とし口がある。

    **輪は縦に立てる**（軸は x）。見下ろすカメラでは寝かせた輪は円盤にしか見えない。
    """
    rng = rng_for(name)
    W = D = 64
    h = 196
    frame = np.zeros((W, D, h), np.int16)
    #! 石の基壇（4 隅の脚を岩へ据える）。**接地層はマスいっぱいに広げない**——
    #! 2×2 マスの真ん中に据えるので、基壇は 44×44 で足りる。
    box(frame, 10, 54, 10, 54, 0, 6, K('crag_zl_basd'))
    box(frame, 12, 52, 12, 52, 5, 7, K('flag_d'))
    legs = ((14, 14, 1), (44, 14, 1), (14, 44, 1), (44, 44, 1))
    top_z = 150
    for (lx, ly, _s) in legs:
        #! 脚は上へ行くほど内へ寄る（絞りが無いと櫓ではなく足場に見える）。
        for z in range(6, top_z):
            t = (z - 6) / float(top_z - 6)
            x = int(round(lx + ((26 - lx) * 0.62 * t)))
            y = int(round(ly + ((26 - ly) * 0.62 * t)))
            box(frame, x, x + 6, y, y + 6, z, z + 1, K('timber') if ((z // 8) % 2) else K('timber_d'))
    for z in (30, 62, 94, 126):                              # 横木（4 段）
        box(frame, 16, 48, 16, 22, z, z + 3, K('timber_d'))
        box(frame, 16, 48, 42, 48, z, z + 3, K('timber_d'))
        box(frame, 16, 22, 16, 48, z, z + 3, K('timber_d'))
        box(frame, 42, 48, 16, 48, z, z + 3, K('timber_d'))
    for k, z in enumerate((30, 62, 94, 126)):                # 筋交い（南面。向きは交互）
        span = 32
        for t in range(span):
            zz = z + int(round(30.0 * ((t / span) if (k % 2 == 0) else (1.0 - (t / span)))))
            box(frame, 18 + t, 19 + t, 44, 47, zz, zz + 2, K('timber_l'))
    box(frame, 18, 46, 18, 46, top_z, top_z + 4, K('board_d'))   # 頭の床
    box(frame, 20, 44, 20, 44, top_z + 4, top_z + 6, K('board'))
    #! 索輪の軸受（両側の柱）。
    for x0 in (20, 40):
        box(frame, x0, x0 + 4, 28, 36, top_z + 4, top_z + 34, K('timber_d'))
    log_x(frame, 32, top_z + 30, 2.4, 18, 46, K('iron'))         # 軸
    #! 索（櫓の頭から斜めに巻き上げ小屋へ）。
    for t in range(64):
        x = 30 + int(round(t * 0.06))
        y = 32 + int(round(t * 0.42))
        z = top_z + 28 - int(round(t * 1.9))
        box(frame, x, x + 2, y, y + 2, max(0, z), max(1, z + 2), K('iron_l'))
    box(frame, 26, 48, 46, 62, 0, 26, K('crag_zl_basd'))         # 巻き上げ小屋（石積み）
    box(frame, 27, 47, 47, 61, 24, 28, K('board_d'))
    box(frame, 30, 44, 46, 48, 6, 20, K('timber_d'))             # 小屋の口
    box(frame, 4, 26, 50, 60, 0, 2, K('rust'))                   # 鉱車の軌道
    for x in range(4, 26, 5):
        box(frame, x, x + 2, 49, 61, 0, 2, K('timber_d'))
    for _ in range(6):                                           # 落とした鉱石
        x, y = int(rng.integers(4, 24)), int(rng.integers(50, 60))
        box(frame, x, x + 4, y, y + 4, 1, 4, K('crag_zl_red'))

    wheel = np.zeros((W, D, h), np.int16)
    cz = top_z + 30
    log_x(wheel, 32, cz, 15.0, 28, 36, K('iron'))                # 索輪
    log_x(wheel, 32, cz, 12.0, 28, 36, 0)
    for k in range(8):                                           # 輻
        a = k * (math.pi / 4.0)
        for t in range(13):
            y = int(round(32 + (t * math.sin(a))))
            z = int(round(cz + (t * math.cos(a))))
            box(wheel, 29, 35, y, y + 2, z, z + 2, K('iron_l'))
    log_x(wheel, 32, cz, 3.0, 26, 38, K('rust'))                 # 轂
    return [('frame', frame, (0, 0, 0)), ('wheel', wheel, (0, 0, 0))]


def headframe_parts():
    return spin_parts([('wheel', (32.0, 32.0, 180.0), 'x', 0.42, 0.0)])


def lavafall_zul(name):
    """**溶岩の滝**（1×2 マス・高さ 4 マス）。岩壁の樋から溶岩が落ち、受け皿で跳ねる。

    落ちる動きは**沈めた車輪の縁**で作る（`lava_bubble` と同じ手。上下の平行移動が
    無いので、輪の縁に付けた塊が面を横切る所だけが「落ちている」ように見える）。

    **落ちる帯は岩の南面へ出す。** 初版は岩の袖壁を y=0〜22 に厚く作り、その内側
    （y=12〜18）へ溶岩を置いたので、**溶岩がまるごと岩に埋まって見えなかった**
    （§7 の記録）。岩は北側の薄い背板にして、溶岩はその手前へ垂らす。
    """
    W, D, h = 32, 64, 132
    rock = np.zeros((W, D, h), np.int16)
    #! 岩の背板（滝の裏。**薄く**して、溶岩を隠さない）。段で積んで岩に見せる。
    for z in range(0, h):
        band = K('crag_zl_bas') if ((z // 11) % 2) else K('crag_zl_basd')
        box(rock, 0, W, 0, 9, z, z + 1, band)
    #! 両脇の袖（溶岩の帯を挟む。ここが無いと溶岩が宙に垂れて見える）。
    for x0 in (0, W - 6):
        for z in range(0, h - 6):
            band = K('crag_zl_basd') if ((z // 9) % 2) else K('crag_zl_basdd')
            box(rock, x0, x0 + 6, 9, 17, z, z + 1, band)
    #! 樋の口（頭で前へせり出す）。
    box(rock, 2, 30, 0, 20, h - 16, h - 6, K('crag_zl_basl'))
    box(rock, 5, 27, 9, 20, h - 14, h - 6, 0)
    box(rock, 5, 27, 9, 20, h - 14, h - 12, K('lava_o'))
    #! 落ちる帯（静止の芯。**岩の南面**へ出す）。
    box(rock, 6, 26, 10, 16, 12, h - 12, K('lava_o'))
    box(rock, 10, 22, 10, 17, 12, h - 12, K('lava_y'))
    #! 受け皿（南へ広がる溶岩の池）。
    box(rock, 4, 28, 12, 34, 0, 12, K('lava_o'))
    box(rock, 7, 25, 16, 30, 2, 10, K('lava_zl_w'))
    box(rock, 1, 31, 10, 38, 0, 4, K('crust'))                   # 冷えた縁
    box(rock, 3, 29, 14, 36, 0, 2, K('crust_l'))
    box(rock, 4, 28, 12, 34, 3, 6, K('lava_y'))

    drops = np.zeros((W, D, h), np.int16)
    cy, cz = 13.0, 62.0
    r = 46.0
    for k in range(5):                                           # 落ちる塊（輪の縁に）
        a = k * (2.0 * math.pi / 5.0)
        y = int(round(cy + (r * math.sin(a) * 0.10)))
        z = int(round(cz + (r * math.cos(a))))
        if 0 <= z < h - 8:
            box(drops, 6, 26, max(0, y), max(1, y + 6), z, z + 7, K('lava_y'))
            box(drops, 10, 22, max(0, y + 1), max(1, y + 5), z + 1, z + 6, K('lava_zl_w'))
    steam = np.zeros((W, D, h), np.int16)
    for k in range(5):                                           # 立ちのぼるガス
        z = 10 + (k * 9)
        w = 3 + k
        box(steam, 16 - w, 16 + w, 20 + k, 30 + k, z, z + 5,
            K('gas') if (k % 2) else K('gas_d'))
    return [('frame', rock, (0, 0, 0)), ('drops', drops, (0, 0, 0)), ('steam', steam, (0, 0, 0))]


def lavafall_parts():
    return [
        {'name': 'frame', 'voxels': 'frame', 'grounded': True,
         'motion': {'kind': 'static'}, 'wind_k': 0.0},
        {'name': 'drops', 'voxels': 'drops', 'parent': 'frame', 'grounded': False,
         'pivot': [16.0, 13.0, 62.0],
         'motion': {'kind': 'rotate', 'axis': 'x', 'speed': 0.30, 'phase': 0.0}, 'wind_k': 0.0},
        {'name': 'steam', 'voxels': 'steam', 'parent': 'frame', 'grounded': False,
         'motion': {'kind': 'wind'}, 'wind_k': 0.55},
    ]


def _ropebridge(span_axis):
    """吊り橋の中身（`span_axis` は 'y'＝南北に渡る / 'x'＝東西に渡る）。"""

    def build(name):
        rng = rng_for(name)
        long_n = 3 * V
        W, D = (V, long_n) if span_axis == 'y' else (long_n, V)
        h = 96
        frame = np.zeros((W, D, h), np.int16)
        deck = np.zeros((W, D, h), np.int16)

        def at(u, w):
            """(渡る向きの座標 u, 幅の向きの座標 w) → (x, y)。"""
            return (w, u) if span_axis == 'y' else (u, w)

        #! 両端の櫓（岩へ打ち込んだ 2 本の柱と横木）。
        for u0 in (2, long_n - 10):
            for w0 in (7, 21):
                x, y = at(u0, w0)
                box(frame, x, x + (8 if span_axis == 'x' else 4),
                    y, y + (4 if span_axis == 'x' else 8), 0, 72, K('log_d'))
            x, y = at(u0, 6)
            box(frame, x, x + (8 if span_axis == 'x' else 22),
                y, y + (22 if span_axis == 'x' else 8), 66, 72, K('log'))
        #! 索（両端の櫓の頭から中央へ垂れる。**垂れないと綱に見えない**）。
        for u in range(4, long_n - 4):
            t = (u - (long_n / 2.0)) / (long_n / 2.0)
            z = 40 + int(round(28 * t * t))
            for w in (8, 23):
                x, y = at(u, w)
                box(frame, x, x + 2, y, y + 2, z, z + 2, K('lash'))
        for u in range(8, long_n - 8, 7):                       # 吊り縄
            t = (u - (long_n / 2.0)) / (long_n / 2.0)
            z = 40 + int(round(28 * t * t))
            for w in (8, 23):
                x, y = at(u, w)
                box(frame, x, x + 1, y, y + 1, 30, z, K('lash'))
        #! 橋板（振り子で揺れる部品）。板は 1 枚ずつ隙間を空ける。
        for u in range(6, long_n - 6, 4):
            x, y = at(u, 8)
            box(deck, x, x + (3 if span_axis == 'x' else 17),
                y, y + (17 if span_axis == 'x' else 3), 28, 30,
                pick(rng, [K('board'), K('board_d'), K('board_l')], [0.44, 0.34, 0.22]))
        for u in range(4, long_n - 4):                          # 縁の縄
            for w in (8, 24):
                x, y = at(u, w)
                box(deck, x, x + 1, y, y + 1, 28, 31, K('lash'))
        return [('frame', frame, (0, 0, 0)), ('deck', deck, (0, 0, 0))]

    return build


def ropebridge_parts(span_axis):
    long_n = 3 * V
    pivot = (16.0, long_n / 2.0, 44.0) if span_axis == 'y' else (long_n / 2.0, 16.0, 44.0)
    return [
        {'name': 'frame', 'voxels': 'frame', 'grounded': True,
         'motion': {'kind': 'static'}, 'wind_k': 0.0},
        {'name': 'deck', 'voxels': 'deck', 'parent': 'frame', 'grounded': False,
         'pivot': list(pivot),
         'motion': {'kind': 'pendulum', 'axis': ('y' if span_axis == 'y' else 'x'),
                    'amplitude': 3.2, 'period': 4.4, 'phase': 0.0, 'damping': 0.0},
         'wind_k': 0.0},
    ]


def _canyon_gate(ew):
    """谷の入口の岩の門（1 マス）。左右の岩からせり出した持ち送りが頭上で合わさる。"""

    def build(name):
        rng = rng_for(name)
        h = 116
        v = vol(h=h)
        band = (10, 22) if not ew else (10, 22)
        b0, b1 = band
        #! 両脇の岩（谷壁の続き。**下は太く上は細く**して、削り残しに見せる）。
        for z in range(0, 78):
            t = z / 78.0
            wide = int(round(9 - (3 * t)))
            if ew:
                box(v, b0, b1, 0, wide, z, z + 1,
                    K('crag_zl_bas') if ((z // 9) % 2) else K('crag_zl_basd'))
                box(v, b0, b1, V - wide, V, z, z + 1,
                    K('crag_zl_bas') if ((z // 9) % 2) else K('crag_zl_basd'))
            else:
                box(v, 0, wide, b0, b1, z, z + 1,
                    K('crag_zl_bas') if ((z // 9) % 2) else K('crag_zl_basd'))
                box(v, V - wide, V, b0, b1, z, z + 1,
                    K('crag_zl_bas') if ((z // 9) % 2) else K('crag_zl_basd'))
        #! 持ち送り（4 段でせり出して頭上で合わさる。**8 段出すと開口が塞がる**）。
        for i, z in enumerate(range(78, 102, 6)):
            reach = 6 + (i * 3)
            if ew:
                box(v, b0 - 1, b1 + 1, 0, reach, z, z + 6, K('crag_zl_basl'))
                box(v, b0 - 1, b1 + 1, V - reach, V, z, z + 6, K('crag_zl_basl'))
            else:
                box(v, 0, reach, b0 - 1, b1 + 1, z, z + 6, K('crag_zl_basl'))
                box(v, V - reach, V, b0 - 1, b1 + 1, z, z + 6, K('crag_zl_basl'))
        box(v, 0, V, b0 - 2, b1 + 2, 102, 116, K('crag_zl_bas'))     # 合わさった頭
        for _ in range(6):                                           # 頭の欠け
            if ew:
                y = int(rng.integers(0, V - 5))
                box(v, b0 - 2, b0, y, y + 5, 108, 116, 0)
            else:
                x = int(rng.integers(0, V - 5))
                box(v, x, x + 5, b0 - 2, b0, 108, 116, 0)
        #! 木の梁（人が入れた補強）と吊り灯 2 つ。
        if ew:
            box(v, b0, b1, 4, V - 4, 96, 102, K('log_d'))
            for y in (10, V - 14):
                box(v, 14, 18, y, y + 2, 88, 96, K('iron'))
                _lantern(v, 16, y + 1, 80)
                box(v, 14, 18, y - 1, y + 3, 81, 85, K('fl_mid'))    # 炎（焼き込む）
                box(v, 15, 17, y, y + 2, 83, 88, K('fl_core'))
        else:
            box(v, 4, V - 4, b0, b1, 96, 102, K('log_d'))
            for x in (10, V - 14):
                box(v, x, x + 2, 14, 18, 88, 96, K('iron'))
                _lantern(v, x + 1, 16, 80)
                box(v, x - 1, x + 3, 14, 18, 81, 85, K('fl_mid'))    # 炎（焼き込む）
                box(v, x, x + 2, 15, 17, 83, 88, K('fl_core'))
        return one(v)

    return build


def cliff_stair_zul(name):
    """**岩を彫った大階段**（1×2 マス・高さ 2.8 マス）。谷底から岩棚へ折り返して上がる。"""
    v = vol(h=92, d=64)
    box(v, 0, V, 0, 64, 0, 8, K('crag_zl_basd'))                 # 取り付きの岩
    #! 下の段（南から北へ 10 段）。**踏み面を明るく・蹴上げを暗く**して段を読ませる。
    for i in range(10):
        z = 6 + (i * 5)
        y0 = 56 - (i * 5)
        box(v, 4, 28, y0, y0 + 6, 0, z, K('crag_zl_basd'))
        box(v, 4, 28, y0, y0 + 6, z - 2, z, K('flag_d'))
    box(v, 4, 28, 4, 14, 0, 56, K('crag_zl_basd'))               # 踊り場
    box(v, 4, 28, 4, 14, 54, 56, K('flag'))
    #! 上の段（折り返して東へ）。
    for i in range(7):
        z = 56 + (i * 5)
        x0 = 4 + (i * 3)
        box(v, x0, x0 + 4, 14, 30, 0, z, K('crag_zl_basd'))
        box(v, x0, x0 + 4, 14, 30, z - 2, z, K('flag_d'))
    for y in range(6, 60, 8):                                    # 手すりの杭と縄
        box(v, 28, 30, y, y + 2, 4, 22, K('timber_d'))
    for y in range(6, 60):
        z = 18 - int(round(y * 0.06))
        box(v, 28, 30, y, y + 1, z, z + 2, K('lash'))
    return one(v)


def watch_spire_zul(name):
    """**物見の櫓**（1 マス・高さ 5.6 マス）。細い岩の尖りに木の見張り台と幟。"""
    v = vol(h=182)
    #! 岩の尖り（下は太く、上は細く。**八角**にして石の柱に見せる）。
    for z in range(0, 126):
        t = z / 126.0
        w = int(round(13 - (6 * t)))
        octagon(v, 16 - w, 16 + w, 16 - w, 16 + w, z, z + 1,
                K('crag_zl_bas') if ((z // 11) % 2) else K('crag_zl_basd'))
    for z in range(20, 120, 26):                                 # 岩へ打った杭（登る足掛かり）
        box(v, 16, 22, 15, 17, z, z + 2, K('rust'))
    box(v, 4, 28, 4, 28, 126, 130, K('board_d'))                 # 見張り台の床
    box(v, 6, 26, 6, 26, 130, 132, K('board'))
    for (x, y) in ((5, 5), (25, 5), (5, 25), (25, 25)):          # 手すりの柱
        box(v, x, x + 2, y, y + 2, 130, 148, K('timber_d'))
    box(v, 4, 28, 4, 6, 144, 148, K('log_d'))                    # 手すり
    box(v, 4, 28, 26, 28, 144, 148, K('log_d'))
    box(v, 4, 6, 4, 28, 144, 148, K('log_d'))
    box(v, 26, 28, 4, 28, 144, 148, K('log_d'))
    box(v, 15, 18, 15, 18, 130, 182, K('timber'))                # 幟竿
    box(v, 12, 20, 12, 20, 126, 130, K('crag_zl_basd'))          # 台の芯
    flag = vol(h=182)
    for i in range(12):                                          # 幟（風でなびく）
        box(flag, 18 + (i // 3), 18 + (i // 3) + 1, 14, 20, 176 - (i * 3), 179 - (i * 3),
            K('cloth_zl_r') if (i % 3) else K('cloth_zl_o'))
    return [('frame', v, (0, 0, 0)), ('flag', flag, (0, 0, 0))]


def smelter_zul(name):
    """**溶鉱炉**（1 マス・高さ 3.6 マス）。石積みの炉と煙突。口が赤く光る（火は別体）。"""
    rng = rng_for(name)
    v = vol(h=116)
    box(v, 2, 30, 4, 30, 0, 8, K('crag_zl_basd'))                # 基壇
    for z in range(6, 62, 6):                                    # 石積み（目地を彫る）
        shade = pick(rng, [K('flag'), K('flag_d'), K('ash_d')], [0.42, 0.34, 0.24])
        box(v, 5, 27, 6, 28, z, z + 6, shade)
        box(v, 5, 27, 6, 28, z, z + 1, K('joint'))
    box(v, 11, 21, 26, 30, 8, 26, K('char'))                     # 炉の口
    box(v, 12, 20, 27, 30, 10, 22, K('emb_l'))
    box(v, 9, 23, 24, 30, 24, 27, K('crag_zl_basl'))             # 口のまぐさ
    box(v, 6, 26, 7, 27, 60, 64, K('flag_l'))                    # 炉の肩
    for z in range(62, 108, 6):                                  # 煙突
        octagon(v, 11, 21, 11, 21, z, z + 6,
                K('soot') if ((z // 6) % 2) else K('soot_l'), cut=2)
    box(v, 9, 23, 9, 23, 106, 110, K('rust'))                    # 煙突の笠
    box(v, 3, 29, 26, 32, 0, 3, K('crust'))                      # 前の鋳床（冷えた湯道）
    box(v, 8, 24, 28, 32, 0, 2, K('lava_o'))
    for _ in range(4):                                           # 炭の山
        x = int(rng.integers(2, 8))
        box(v, x, x + 5, 8, 14, 0, 5, K('char_l'))
    box(v, 12, 20, 27, 30, 12, 20, K('fl_mid'))                  # 炉の火（焼き込む）
    box(v, 14, 18, 28, 30, 14, 19, K('fl_core'))
    box(v, 9, 23, 29, 32, 0, 2, K('fl_edge'))                    # 鋳床へ流れ出た湯
    return one(v)


def ore_crusher_zul(name):
    """**鉱石の砕き機**（1 マス・高さ 2.6 マス）。木の枠に鉄の輪と杵。輪が回る。"""
    frame = vol(h=84)
    box(frame, 2, 30, 6, 28, 0, 5, K('crag_zl_basd'))            # 石の据え
    for x0 in (4, 24):                                           # 枠の柱
        box(frame, x0, x0 + 4, 8, 12, 4, 72, K('timber_d'))
        box(frame, x0, x0 + 4, 22, 26, 4, 72, K('timber_d'))
    box(frame, 2, 30, 8, 12, 68, 74, K('log_d'))                 # 頭の横木
    box(frame, 2, 30, 22, 26, 68, 74, K('log_d'))
    for (x0, ph) in ((9, 0), (15, 1), (21, 0)):                  # 杵（高さを違える）
        box(frame, x0, x0 + 4, 14, 20, 20 + (ph * 8), 66, K('timber'))
        box(frame, x0, x0 + 4, 13, 21, 16 + (ph * 8), 22 + (ph * 8), K('rust'))
    box(frame, 6, 26, 12, 22, 4, 12, K('iron'))                  # 臼
    box(frame, 8, 24, 14, 20, 10, 12, K('crag_zl_red'))          # 砕いた鉱石
    wheel = vol(h=84)
    log_x(wheel, 24, 50, 11.0, 3, 9, K('iron'))                  # 輪（軸は x）
    log_x(wheel, 24, 50, 8.0, 3, 9, 0)
    for k in range(6):
        a = k * (math.pi / 3.0)
        for t in range(9):
            y = int(round(24 + (t * math.sin(a))))
            z = int(round(50 + (t * math.cos(a))))
            box(wheel, 4, 8, y, y + 2, z, z + 2, K('iron_l'))
    log_x(wheel, 24, 50, 2.4, 2, 12, K('rust'))                  # 軸
    return [('frame', frame, (0, 0, 0)), ('wheel', wheel, (0, 0, 0))]


def ore_crusher_parts():
    return spin_parts([('wheel', (6.0, 24.0, 50.0), 'x', 0.55, 0.0)])


# =================================================================== 小物
def brazier_zul(name):
    """篝火（三脚の鉄鉢に炭）。火は `brazier_fire_zul`。"""
    v = vol(h=40)
    for (dx, dy) in ((-7, -7), (7, -7), (0, 8)):                 # 三脚
        box(v, 16 + dx - 1, 16 + dx + 1, 16 + dy - 1, 16 + dy + 1, 0, 20, K('iron'))
    disc(v, 16, 16, 7.6, 18, 26, K('iron'), hollow=6.0)          # 鉢
    disc(v, 16, 16, 7.0, 18, 20, K('iron'))
    disc(v, 16, 16, 6.0, 20, 23, K('char'))
    disc(v, 16, 16, 4.5, 22, 25, K('emb'))
    disc(v, 16, 16, 8.0, 26, 27, K('rust'))                      # 縁
    #! **炎は本体へ焼き込む。**区画（`zones`）と 1 点（`features`）の置き口は
    #! 自発光を渡さない（渡せるのは `gate_flame` と街灯だけ）ので、別体にすると
    #! 「夜に消える炎」になる。§7 の記録。
    disc(v, 16, 16, 3.4, 24, 28, K('fl_mid'))
    disc(v, 16, 16, 1.8, 26, 33, K('fl_core'))
    disc(v, 16, 16, 4.6, 23, 25, K('fl_edge'))
    return one(v)


def rail_track_zul(name):
    """鉱車の軌道（低い。地面に敷く）。**南北へ通す**——枕木は等間隔でマスをまたいで続く。"""
    v = vol(h=6)
    box(v, 0, V, 0, V, 0, 1, K('gravel_d'))
    for y in range(1, V, 5):
        box(v, 6, 26, y, y + 3, 1, 3, K('timber_d'))             # 枕木
        box(v, 6, 26, y, y + 1, 1, 3, K('log_d'))
    box(v, 9, 12, 0, V, 3, 5, K('rust'))                         # 軌条
    box(v, 20, 23, 0, V, 3, 5, K('rust'))
    return one(v)


def ore_cart_zul(name):
    """鉱車（鉄の箱に 4 つの車輪。鉱石を積んである）。"""
    rng = rng_for(name)
    v = vol(h=34)
    #! **車輪を箱の外へ出す**（箱の下に隠すと茶色い箱にしか見えない。§7 の記録）。
    for y in (8, 23):
        log_x(v, y, 5, 5.0, 4, 8, K('iron'))
        log_x(v, y, 5, 5.0, 24, 28, K('iron'))
        log_x(v, y, 5, 2.0, 8, 24, K('rust'))                    # 車軸
    box(v, 8, 24, 6, 26, 9, 11, K('rust'))                       # 台枠
    box(v, 8, 24, 6, 26, 11, 26, K('iron'))                      # 箱
    box(v, 10, 22, 8, 24, 13, 26, 0)                             # 中を刳る
    for z in range(12, 26, 5):                                   # 箱の帯
        box(v, 8, 24, 6, 26, z, z + 1, K('rust'))
    #! 鉱石は**縁より上へ盛る**（縁と同じ高さだと蓋に見える）。
    for _ in range(int(rng.integers(7, 12))):
        x, y = int(rng.integers(10, 20)), int(rng.integers(8, 21))
        z = int(rng.integers(20, 27))
        box(v, x, x + 4, y, y + 4, 18, z,
            pick(rng, [K('crag_zl_red'), K('crag_zl_basd'), K('crag_zl_sul')],
                 [0.46, 0.36, 0.18]))
    box(v, 15, 17, 26, 31, 13, 15, K('iron'))                    # 連結棒
    return one(v)


def ore_pile_zul(name):
    """選り分けた鉱石の山（赤い鉄鉱と黄色い硫黄が層になる）。"""
    rng = rng_for(name)
    v = vol(h=22)
    cone(v, 16, 16, 0, 16, 12.0, 2.0, K('crag_zl_red'))
    for _ in range(int(rng.integers(6, 11))):
        x, y = int(rng.integers(6, 24)), int(rng.integers(6, 24))
        z = int(rng.integers(2, 12))
        box(v, x, x + 3, y, y + 3, z, z + 3, K('crag_zl_sul') if rng.random() < 0.4
            else K('crag_zl_basd'))
    for _ in range(4):                                           # こぼれた石
        x, y = int(rng.integers(2, 28)), int(rng.integers(2, 28))
        box(v, x, x + 3, y, y + 3, 0, 2, K('crag_zl_red'))
    return one(v)


def rubble_zul(name):
    """崩れた岩屑（崖の裾に溜まる）。角の立った塊を積む。"""
    rng = rng_for(name)
    v = vol(h=20)
    for _ in range(int(rng.integers(7, 12))):
        x, y = int(rng.integers(0, 24)), int(rng.integers(0, 24))
        w = int(rng.integers(4, 9))
        z = int(rng.integers(0, 8))
        box(v, x, x + w, y, y + w - 1, z, z + int(rng.integers(3, 8)),
            pick(rng, [K('crag_zl_basd'), K('crag_zl_basdd'), K('crag_zl_bas')],
                 [0.40, 0.34, 0.26]))
    return one(v)


def basalt_column_zul(name):
    """折れた柱状節理の柱（六角の柱が 2〜3 本、斜めに倒れている）。"""
    rng = rng_for(name)
    v = vol(h=46)
    #! 立った柱（八角。**折れ口を明るく**して断面を見せる）。
    for (cx, cy, tall) in ((11, 12, 40), (21, 19, 28), (14, 23, 17)):
        octagon(v, cx - 5, cx + 5, cy - 5, cy + 5, 0, tall,
                pick(rng, [K('crag_zl_basd'), K('crag_zl_bas')], [0.55, 0.45]), cut=2)
        #! **折れ口を明るくしすぎない。**谷底より 2 倍明るいと、見下ろすカメラで
        #! 白い円柱の頭が点々と散って見えた（§7 の記録）。
        octagon(v, cx - 5, cx + 5, cy - 5, cy + 5, tall - 2, tall, K('crag_zl_bas'), cut=2)
        for z in range(6, tall - 4, 9):                          # 節理の目
            box(v, cx - 5, cx + 5, cy - 5, cy + 5, z, z + 1, K('crag_zl_basdd'))
    #! 倒れた柱（横たえた 1 本）。
    log_x(v, 27, 4, 4.0, 2, 24, K('crag_zl_basdd'))
    box(v, 22, 24, 23, 31, 0, 8, K('crag_zl_bas'))               # 折れ口
    return one(v)


def scaffold_zul(name):
    """岩肌に組んだ木の足場（梯子と板。人が岩を掘っている証拠）。"""
    v = vol(h=76)
    for x0 in (5, 24):                                           # 柱
        box(v, x0, x0 + 3, 22, 25, 0, 70, K('timber_d'))
        box(v, x0, x0 + 3, 8, 11, 0, 46, K('timber_d'))
    for z in (22, 44, 66):                                       # 貫
        box(v, 4, 28, 22, 25, z, z + 3, K('timber'))
    for z in (22, 44):
        box(v, 4, 28, 8, 11, z, z + 3, K('timber'))
    box(v, 4, 28, 8, 26, 44, 46, K('board_d'))                   # 足場板
    box(v, 4, 28, 8, 26, 22, 24, K('board'))
    for z in range(4, 44, 6):                                    # 梯子
        box(v, 13, 20, 26, 28, z, z + 2, K('log_d'))
    box(v, 12, 14, 26, 28, 0, 46, K('timber_d'))
    box(v, 19, 21, 26, 28, 0, 46, K('timber_d'))
    box(v, 6, 12, 12, 20, 46, 52, K('crag_zl_red'))              # 積んだ鉱石
    return one(v)


def cairn_zul(name):
    """道しるべの石積み（谷は道が分かりにくい。旅人が積んだ塚）。"""
    rng = rng_for(name)
    v = vol(h=34)
    z = 0
    r = 8.0
    cx, cy = 16.0, 16.0
    while z < 28:
        thick = int(rng.integers(3, 6))
        #: **段ごとに中心をずらす**（真円で積むと段の菓子に見える）。
        cx += float(rng.uniform(-1.6, 1.6))
        cy += float(rng.uniform(-1.6, 1.6))
        w = int(round(r))
        box(v, int(cx) - w, int(cx) + w, int(cy) - w + 1, int(cy) + w - 1, z, z + thick,
            pick(rng, [K('crag_zl_bas'), K('crag_zl_basd'), K('crag_zl_tuf')],
                 [0.38, 0.36, 0.26]))
        box(v, int(cx) - w + 1, int(cx) + w - 1, int(cy) - w, int(cy) + w, z, z + thick,
            pick(rng, [K('crag_zl_bas'), K('crag_zl_basd'), K('crag_zl_basl')],
                 [0.40, 0.34, 0.26]))
        z += thick
        r = max(2.0, r - float(rng.uniform(0.8, 1.5)))
    return one(v)


def awning_zul(name):
    """岩に張った日除け（柱 2 本と布。布は風でなびく）。"""
    frame = vol(h=52)
    for x0 in (5, 24):
        box(frame, x0, x0 + 3, 24, 27, 0, 44, K('timber_d'))
    box(frame, 4, 28, 24, 27, 42, 45, K('log_d'))                # 前の桁
    box(frame, 4, 28, 4, 7, 46, 49, K('log_d'))                  # 岩へ留めた桁
    box(frame, 3, 6, 4, 7, 44, 50, K('rust'))
    box(frame, 26, 29, 4, 7, 44, 50, K('rust'))
    cloth = vol(h=52)
    for i in range(18):                                          # 布（前へ下がる）
        y = 6 + i
        z = 47 - int(round(i * 0.2))
        box(cloth, 4, 28, y, y + 1, z, z + 1,
            K('cloth_zl_o') if ((i // 3) % 2) else K('cloth_zl_r'))
    for i in range(6):                                           # 前垂れ
        box(cloth, 4, 28, 24, 25, 38 + i, 39 + i, K('cloth_zl_o'))
    return [('frame', frame, (0, 0, 0)), ('cloth', cloth, (0, 0, 0))]


def banner_zul(name):
    """幟竿（岩に据えた石の台に木の竿。旗は風でなびく）。"""
    frame = vol(h=104)
    octagon(frame, 10, 22, 10, 22, 0, 8, K('crag_zl_basd'), cut=3)
    box(frame, 13, 19, 13, 19, 6, 96, K('timber'))
    box(frame, 12, 20, 12, 20, 94, 98, K('rust'))
    box(frame, 14, 18, 14, 18, 98, 102, K('iron_l'))
    flag = vol(h=104)
    for i in range(14):
        box(flag, 19 + (i // 4), 20 + (i // 4), 12, 20, 92 - (i * 4), 95 - (i * 4),
            K('cloth_zl_r') if ((i // 2) % 2) else K('cloth_zl_o'))
    return [('frame', frame, (0, 0, 0)), ('flag', flag, (0, 0, 0))]


def crate_zul(name):
    """木箱の積み（鉄の帯で締めてある）。"""
    rng = rng_for(name)
    v = vol(h=30)
    for (x, y, z, w) in ((4, 6, 0, 12), (17, 9, 0, 11), (7, 14, 12, 10)):
        box(v, x, x + w, y, y + w, z, z + w, K('board_d'))
        box(v, x + 1, x + w - 1, y + 1, y + w - 1, z + 1, z + w - 1, K('board'))
        box(v, x, x + w, y, y + w, z + (w // 2), z + (w // 2) + 1, K('rust'))
    return one(v)


def barrel_zul(name):
    """樽（鉄の箍。谷では水も炭も樽で運ぶ）。"""
    v = vol(h=30)
    for (cx, cy, tall) in ((10, 11, 22), (21, 15, 26), (13, 23, 18)):
        disc(v, cx, cy, 5.2, 0, tall, K('board_d'))
        disc(v, cx, cy, 5.8, 2, 5, K('rust'))
        disc(v, cx, cy, 5.8, tall - 5, tall - 2, K('rust'))
        disc(v, cx, cy, 4.6, tall - 1, tall, K('board'))
    return one(v)


def anvil_zul(name):
    """金床（岩の切り株の上）。"""
    v = vol(h=26)
    octagon(v, 9, 23, 9, 23, 0, 10, K('crag_zl_basd'), cut=3)    # 岩の台
    box(v, 10, 22, 12, 20, 10, 14, K('iron'))                    # 台座
    box(v, 12, 20, 13, 19, 14, 17, K('iron'))                    # くびれ
    box(v, 6, 26, 13, 19, 17, 21, K('iron'))                     # 身
    box(v, 6, 26, 13, 19, 20, 21, K('iron_zl_l'))                # 打面
    box(v, 2, 7, 14, 18, 17, 20, K('iron'))                      # 角
    return one(v)


def forge_zul(name):
    """鍛冶の炉（石積みに炭と鞴）。火は `forge_fire_zul`（**昼も光る**）。"""
    rng = rng_for(name)
    v = vol(h=46)
    for z in range(0, 18, 4):
        box(v, 4, 26, 8, 26, z, z + 4,
            pick(rng, [K('flag'), K('flag_d'), K('ash_d')], [0.44, 0.32, 0.24]))
        box(v, 4, 26, 8, 26, z, z + 1, K('joint'))
    box(v, 7, 23, 11, 23, 16, 20, K('char'))                     # 炭床
    box(v, 9, 21, 13, 21, 17, 20, K('emb_l'))
    box(v, 12, 18, 6, 12, 18, 40, K('crag_zl_basd'))             # 煙出し
    box(v, 11, 19, 5, 13, 38, 42, K('rust'))
    box(v, 24, 30, 12, 24, 8, 12, K('board_d'))                  # 鞴
    box(v, 25, 29, 14, 22, 12, 18, K('fur'))
    box(v, 22, 26, 16, 20, 16, 18, K('timber_d'))
    box(v, 9, 21, 13, 21, 19, 24, K('fl_mid'))                   # 炭火（本体へ焼き込む）
    box(v, 12, 18, 15, 19, 21, 30, K('fl_core'))
    box(v, 7, 23, 11, 23, 18, 20, K('fl_edge'))
    return one(v)


def steam_vent_zul(name):
    """蒸気の噴気口（岩の割れ目から湯気。湯気は風でなびく）。"""
    frame = vol(h=16)
    box(frame, 4, 28, 6, 26, 0, 4, K('crag_zl_basd'))
    box(frame, 10, 22, 12, 20, 3, 6, K('crag_zl_basdd'))
    box(frame, 12, 20, 13, 19, 2, 5, 0)
    box(frame, 6, 26, 8, 24, 0, 2, K('crust'))
    #! **湯気は角柱にしない。**箱で積むと白い大理石の柱に見えた（§7 の記録）。
    #! 小さい塊を横へずらしながら 6 段、間を空けて積むと立ちのぼる湯気に読める。
    steam = vol(h=36)
    rng = rng_for(name + ':steam')
    #! **塊と塊の間を空ける。**続けて積むと白い角柱になり、大理石の柱に見えた
    #! （§7 の記録）。3〜5 ボクセルの小さな塊を、間を空けて斜めに散らす。
    cx, cy = 16.0, 16.0
    for k in range(7):
        z = 4 + (k * 4)
        w = 2 + int(k * 0.55)
        cx += float(rng.uniform(-2.6, 2.6))
        cy += float(rng.uniform(-2.6, 2.6))
        if k % 3 == 2:
            continue                                          #: ここは切れ目（間を空ける）
        box(steam, int(cx) - w, int(cx) + w, int(cy) - w, int(cy) + w, z, z + 2,
            K('cloud') if (k % 2) else K('cloud_d'))
    return [('frame', frame, (0, 0, 0)), ('steam', steam, (0, 0, 0))]


def sulphur_vent_zul(name):
    """硫黄の噴気口（黄色く縁取られた穴。ガスがゆらぐ）。"""
    frame = vol(h=14)
    disc(frame, 16, 16, 12.0, 0, 3, K('crag_zl_basd'))
    disc(frame, 16, 16, 9.0, 2, 6, K('crag_zl_sul'))
    disc(frame, 16, 16, 6.0, 3, 8, K('sulf'))
    disc(frame, 16, 16, 4.0, 2, 4, K('char'))
    gas = vol(h=32)
    rng = rng_for(name + ':gas')
    cx, cy = 16.0, 16.0
    for k in range(7):
        z = 6 + (k * 3)
        w = 2 + int(k * 0.5)
        cx += float(rng.uniform(-2.4, 2.4))
        cy += float(rng.uniform(-2.4, 2.4))
        if k % 3 == 1:
            continue                                          #: 切れ目
        box(gas, int(cx) - w, int(cx) + w, int(cy) - w, int(cy) + w, z, z + 2,
            K('gas_y') if (k % 2) else K('gas'))
    return [('frame', frame, (0, 0, 0)), ('gas', gas, (0, 0, 0))]


def crystal_zul(name):
    """岩に露出した鉱脈の結晶（光る。光は `crystal_glow_zul`）。"""
    rng = rng_for(name)
    v = vol(h=40)
    box(v, 4, 28, 8, 26, 0, 8, K('crag_zl_basd'))                # 露頭
    box(v, 6, 26, 10, 24, 6, 9, K('crag_zl_red'))
    for _ in range(int(rng.integers(4, 7))):                     # 結晶（尖った柱）
        cx, cy = int(rng.integers(9, 22)), int(rng.integers(11, 22))
        tall = int(rng.integers(12, 30))
        for z in range(8, tall):
            t = (z - 8) / float(max(1, tall - 8))
            w = max(1, int(round(3.5 - (2.6 * t))))
            box(v, cx - w, cx + w, cy - w, cy + w, z, z + 1,
                K('mgl') if (z < tall - 4) else K('qz_l'))
    return one(v)


def tent_zul(name):
    """岩棚に建てた掘っ立て小屋（板壁とトタンまがいの鉄板の片流れ）。"""
    rng = rng_for(name)
    v = vol(h=52)
    box(v, 3, 29, 5, 27, 0, 3, K('crag_zl_basd'))                # 石の据え
    for x in range(4, 28, 3):                                    # 板壁
        box(v, x, x + 3, 5, 7, 2, 32, pick(rng, [K('board'), K('board_d'), K('board_l')],
                                           [0.4, 0.36, 0.24]))
        box(v, x, x + 3, 25, 27, 2, 26, pick(rng, [K('board'), K('board_d'), K('board_l')],
                                             [0.4, 0.36, 0.24]))
    box(v, 3, 6, 5, 27, 2, 32, K('board_d'))
    box(v, 26, 29, 5, 27, 2, 32, K('board_d'))
    box(v, 12, 20, 25, 28, 2, 20, K('timber_d'))                 # 戸口
    for i in range(24):                                          # 片流れの鉄板（北が高い）
        y = 4 + i
        z = 34 - int(round(i * 0.42))
        box(v, 2, 30, y, y + 1, z, z + 2, K('rust') if ((i // 4) % 2) else K('iron'))
    box(v, 6, 10, 2, 6, 30, 46, K('soot'))                       # 煙突
    return one(v)


def weapon_rack_zul(name):
    """武器立て（岩に立てかけた木の枠に槍と鶴嘴）。"""
    v = vol(h=52)
    box(v, 4, 28, 20, 24, 0, 3, K('timber_d'))
    for x0 in (5, 25):
        box(v, x0, x0 + 2, 20, 24, 0, 34, K('timber_d'))
    box(v, 4, 28, 20, 24, 30, 34, K('timber'))
    for i, x in enumerate(range(7, 26, 4)):                      # 柄
        box(v, x, x + 2, 21, 23, 2, 44 - (i % 3), K('timber_l'))
        if i % 2 == 0:                                           # 穂（槍）
            box(v, x, x + 2, 20, 24, 44 - (i % 3), 50 - (i % 3), K('steel'))
        else:                                                    # 鶴嘴
            box(v, x - 3, x + 5, 21, 23, 42 - (i % 3), 44 - (i % 3), K('rust'))
    return one(v)


def shield_rack_zul(name):
    """盾と兜を並べた台（防具屋の前）。"""
    v = vol(h=44)
    box(v, 3, 29, 18, 26, 0, 4, K('timber_d'))
    for x0 in (4, 27):
        box(v, x0, x0 + 2, 18, 22, 0, 36, K('timber_d'))
    box(v, 3, 29, 18, 22, 32, 36, K('timber'))
    #! **盾は縁・面・鋲・臍で 4 色に割る**（1 色で丸を描いても暗い塊にしか見えない）。
    for (cx, face, rim) in ((9, K('rust'), K('iron_zl_l')), (23, K('iron_l'), K('gold_d'))):
        for z in range(9, 31):
            t = (z - 20) / 11.0
            w = int(round(7.0 * math.sqrt(max(0.0, 1.0 - (t * t)))))
            if w <= 0:
                continue
            box(v, cx - w, cx + w, 19, 21, z, z + 1, rim)        # 縁
            box(v, cx - w + 1, cx + w - 1, 19, 21, z, z + 1, face)
        box(v, cx - 3, cx + 3, 18, 20, 17, 23, rim)              # 臍（中央の膨らみ）
        box(v, cx - 2, cx + 2, 17, 19, 18, 22, K('iron_zl_l'))
    ball(v, 16, 25, 7, 4.6, K('iron_l'), squash=0.8)             # 兜
    box(v, 12, 20, 21, 29, 7, 10, K('iron'))                     # 兜の庇
    box(v, 15, 17, 21, 25, 8, 12, K('iron_zl_l'))                # 鼻当て
    return one(v)


def ale_barrels_zul(name):
    """宿の前の麦酒の樽（横倒しの樽と栓）。"""
    v = vol(h=26)
    for (cy, cz, x0, x1) in ((10, 7, 3, 17), (22, 7, 12, 28)):
        log_x(v, cy, cz, 6.4, x0, x1, K('board_d'))
        log_x(v, cy, cz, 6.8, x0 + 2, x0 + 4, K('rust'))
        log_x(v, cy, cz, 6.8, x1 - 4, x1 - 2, K('rust'))
        box(v, x0 + 6, x0 + 8, cy - 1, cy + 1, cz + 6, cz + 9, K('iron'))
    box(v, 2, 30, 4, 28, 0, 2, K('board_d'))                     # 受け台
    return one(v)


def shrine_zul(name):
    """岩を刳った祠（寺院の前）。奥に灯明（火は `shrine_flame_zul`）。"""
    rng = rng_for(name)
    v = vol(h=54)
    #! 岩の塊は**段で積む**（平らな箱にすると冷ライブラリ庫に見えた。§7 の記録）。
    for z in range(0, 44, 5):
        w = 13 - (z // 14)
        box(v, 16 - w, 16 + w, 8, 26, z, z + 5,
            pick(rng, [K('crag_zl_basd'), K('crag_zl_bas'), K('crag_zl_basdd')],
                 [0.40, 0.34, 0.26]))
    box(v, 8, 24, 15, 27, 5, 34, 0)                              # 刳った龕（深く）
    box(v, 8, 24, 15, 27, 5, 7, K('crag_zl_tuf'))                # 龕の床
    box(v, 7, 25, 14, 16, 5, 36, K('crag_zl_basdd'))             # 龕の奥（暗い）
    box(v, 6, 26, 6, 28, 42, 48, K('crag_zl_basl'))              # 庇
    box(v, 4, 28, 24, 30, 44, 46, K('crag_zl_bas'))              # 庇のせり出し
    box(v, 12, 20, 17, 23, 7, 13, K('crag_zl_tuf'))              # 中の台
    box(v, 13, 19, 18, 22, 13, 17, K('emb_l'))                   # 灯明の皿
    for x in (6, 24):                                            # 脇の柱と注連の縄
        box(v, x, x + 2, 26, 28, 5, 38, K('timber_d'))
    box(v, 5, 27, 26, 28, 36, 39, K('log_d'))
    box(v, 6, 26, 26, 27, 33, 35, K('lash'))
    box(v, 13, 19, 18, 22, 15, 21, K('fl_mid'))                  # 灯明（焼き込む）
    box(v, 14, 18, 19, 21, 18, 26, K('fl_core'))
    return one(v)


def orb_stand_zul(name):
    """塔の前の天球（三脚の鉄台に浮く球。光は `orb_glow_zul`）。"""
    v = vol(h=54)
    for (dx, dy) in ((-8, -6), (8, -6), (0, 9)):                 # 三脚
        box(v, 16 + dx - 1, 16 + dx + 2, 16 + dy - 1, 16 + dy + 2, 0, 24, K('iron'))
    disc(v, 16, 16, 6.5, 22, 25, K('iron'))                      # 受け
    box(v, 15, 17, 15, 17, 24, 30, K('rust'))                    # 首
    #! **球は 1 色で通す。**多層にすると果物籠に見えた（§7 の記録）。
    ball(v, 16, 16, 38, 8.0, K('mgl_d'))
    ball(v, 16, 16, 39, 6.0, K('mgl'))
    #! 環は**縦に 1 本だけ**（水平の環を 2 本掛けると籠の縁になる）。
    for z in range(30, 47):
        t = (z - 38) / 8.5
        w = int(round(8.6 * (1.0 - (t * t)) ** 0.5)) if abs(t) < 1.0 else 0
        if w > 0:
            box(v, 16 - w, 16 - w + 1, 15, 18, z, z + 1, K('gold_d'))
            box(v, 16 + w - 1, 16 + w, 15, 18, z, z + 1, K('gold_d'))
    return one(v)


def sapling_zul(name):
    """溶けた岩に根を張った若木（自然魔術の塔の前。谷で唯一の緑）。"""
    rng = rng_for(name)
    v = vol(h=62)
    box(v, 4, 28, 6, 26, 0, 6, K('crust'))                       # 割れた溶岩の殻
    box(v, 8, 24, 10, 22, 4, 8, K('soil'))
    for _ in range(5):                                           # 根
        a = float(rng.uniform(0, 2 * math.pi))
        for t in range(9):
            x = int(round(16 + (t * math.cos(a))))
            y = int(round(16 + (t * math.sin(a))))
            box(v, x, x + 2, y, y + 2, 5, 8, K('bark'))
    box(v, 14, 18, 14, 18, 6, 40, K('bark'))                     # 幹
    box(v, 15, 17, 15, 17, 26, 40, K('bark_l'))
    for _ in range(int(rng.integers(6, 10))):                    # 葉
        x, y = int(rng.integers(7, 22)), int(rng.integers(7, 22))
        z = int(rng.integers(34, 56))
        box(v, x, x + 6, y, y + 6, z, z + 4,
            pick(rng, [K('leaf_m'), K('leaf_l'), K('leaf_d')], [0.4, 0.34, 0.26]))
    return one(v)


def chaos_stone_zul(name):
    """歪んだ石（カオスの塔の前。段の合わない石が捩れて積み上がる）。"""
    rng = rng_for(name)
    v = vol(h=64)
    z = 0
    cx, cy = 16.0, 16.0
    while z < 56:
        thick = int(rng.integers(4, 8))
        w = float(rng.uniform(4.5, 9.0))
        cx += float(rng.uniform(-2.4, 2.4))
        cy += float(rng.uniform(-2.4, 2.4))
        cx = min(24.0, max(8.0, cx))
        cy = min(24.0, max(8.0, cy))
        box(v, int(cx - w), int(cx + w), int(cy - w), int(cy + w), z, z + thick,
            pick(rng, [K('obs'), K('obs_l'), K('crag_zl_basdd'), K('crag_zl_red')],
                 [0.34, 0.26, 0.24, 0.16]))
        z += thick
    box(v, int(cx) - 3, int(cx) + 3, int(cy) - 3, int(cy) + 3, z, z + 4, K('mgl_d'))
    return one(v)


def map_board_zul(name):
    """観光案内の掲示板（板に谷の絵と道しるべの矢）。"""
    v = vol(h=46)
    for x0 in (5, 24):
        box(v, x0, x0 + 3, 20, 23, 0, 34, K('timber_d'))
    box(v, 4, 28, 20, 22, 14, 34, K('board'))                    # 板
    box(v, 4, 28, 20, 22, 30, 32, K('board_d'))
    box(v, 7, 25, 20, 21, 17, 29, K('cream'))                    # 貼った紙
    for (y0, w) in ((0, 12), (4, 8), (8, 14)):                   # 描いた谷（線）
        box(v, 8 + y0, 8 + y0 + w, 20, 21, 20 + y0, 21 + y0, K('ink'))
    box(v, 3, 29, 19, 23, 34, 38, K('log_d'))                    # 笠木
    for (x, dy) in ((6, 0), (18, 0)):                            # 道しるべの矢
        box(v, x, x + 10, 24 + dy, 26 + dy, 22, 26, K('board_l'))
        box(v, x + 8, x + 12, 24 + dy, 26 + dy, 23, 25, K('board_d'))
    return one(v)


def chest_zul(name):
    """闇市の櫃（鉄の帯と錠前。布を掛けてある）。"""
    v = vol(h=28)
    box(v, 4, 28, 8, 24, 0, 16, K('board_d'))
    box(v, 4, 28, 8, 24, 14, 20, K('board'))
    for x in (7, 16, 24):
        box(v, x, x + 2, 8, 24, 0, 20, K('rust'))
    box(v, 14, 18, 22, 25, 8, 14, K('gold_d'))                   # ロック前
    for i in range(8):                                           # 掛けた布
        box(v, 2 + (i * 3), 5 + (i * 3), 6, 12, 18 + (i % 3), 21 + (i % 3), K('rag'))
    return one(v)


def book_stack_zul(name):
    """書店の前の本の山（岩の台に積んだ本と巻物）。"""
    rng = rng_for(name)
    v = vol(h=32)
    octagon(v, 5, 27, 8, 26, 0, 8, K('crag_zl_basd'), cut=3)
    z = 8
    for _ in range(6):
        x, y = int(rng.integers(7, 16)), int(rng.integers(10, 18))
        box(v, x, x + 10, y, y + 8, z, z + 3,
            pick(rng, [K('cloth_zl_r'), K('board_d'), K('cloth_zl_o'), K('ai')],
                 [0.3, 0.28, 0.24, 0.18]))
        box(v, x, x + 10, y, y + 8, z + 2, z + 3, K('cream'))
        z += 3
    for (cx, cy) in ((22, 12), (22, 20)):                        # 巻物
        log_x(v, cy, 11, 3.0, 18, 27, K('cream_d'))
        box(v, 18, 27, cy - 1, cy + 1, 8, 15, K('cream'))
    return one(v)


def herb_rack_zul(name):
    """錬金術店の前の干し台（硫黄と鉱物と乾いた草を吊る）。"""
    v = vol(h=48)
    for x0 in (5, 24):
        box(v, x0, x0 + 3, 22, 25, 0, 42, K('timber_d'))
    box(v, 4, 28, 22, 25, 38, 41, K('log_d'))
    box(v, 4, 28, 22, 25, 24, 26, K('timber'))
    for i, x in enumerate(range(7, 26, 5)):                      # 吊った束
        box(v, x, x + 3, 23, 24, 28, 38, K('lash'))
        box(v, x - 1, x + 4, 22, 25, 22 + (i % 3), 30,
            [K('grass_zl_hd'), K('crag_zl_sul'), K('mgl_d'), K('crag_zl_red')][i % 4])
    box(v, 8, 24, 8, 20, 0, 6, K('crag_zl_basd'))                # 台と壺
    for cx in (11, 17, 22):
        disc(v, cx, 14, 3.0, 6, 14, K('clay'))
        disc(v, cx, 14, 2.0, 12, 14, K('clay_d'))
    return one(v)


def bench_zul(name):
    """石の腰掛け（切り出した岩板を 2 つの石に載せただけ）。"""
    v = vol(h=16)
    #! 受けは**割れた岩**（切石にすると加工した卓に見える）。
    box(v, 4, 11, 11, 21, 0, 8, K('crag_zl_basd'))
    box(v, 21, 28, 13, 23, 0, 9, K('crag_zl_basdd'))
    box(v, 3, 29, 12, 22, 8, 11, K('crag_zl_bas'))               # 渡した岩板
    box(v, 3, 29, 12, 22, 10, 11, K('crag_zl_basl'))             # 座面（踏まれて明るい）
    box(v, 3, 8, 12, 22, 9, 10, K('crag_zl_basd'))               # 端の欠け
    box(v, 25, 29, 12, 22, 9, 10, K('crag_zl_basd'))
    return one(v)


# ------------------------------------------------------------------ 登録
def register(g):
    """`gen_prefabs.py` の名前空間 `g` へズルの素材を足す。"""
    global G, C, V
    G = g
    C = g['C']
    V = g['V']
    if 'zul' not in g['TOWN_STYLES']:
        g['TOWN_STYLES'] = tuple(g['TOWN_STYLES']) + ('zul',)
    _palette()
    cat = g['CATALOG']
    static = g['static_part']
    wind = static(motion={'kind': 'wind'}, wind_k=0.55)

    # ---- 岩山（意匠つき。置く側は `gather` で意匠つきを先に見る）----
    for tier in ('low', 'mid', 'high', 'peak'):
        count = 6 if tier == 'low' else (3 if tier == 'mid' else 2)
        for i in range(1, count + 1):
            cat['crag_%s_zul_%02d' % (tier, i)] = (
                crag_zul, static(), 'ズルの岩肌（柱状節理の玄武岩。%s 段）' % tier)
    for i in range(1, 4):
        cat['alpine_zul_%02d' % i] = (
            alpine_zul, static(), 'ズルの岩に生えるもの（硫黄の結晶・焦げた灌木・地衣）')

    # ---- 地面 ----
    for i in range(1, 5):
        cat['ground_path_zul_%02d' % i] = (ground_path_zul, static(), 'ズルの道（岩を削った轍）')
        cat['ground_turf_zul_%02d' % i] = (ground_turf_zul, static(), 'ズルの谷底（黒い火山礫）')
    for i in range(1, 4):
        cat['ground_ash_zul_%02d' % i] = (ground_ash_zul, static(), 'ズルの灰の平地（区画）')
        cat['ground_cinder_zul_%02d' % i] = (ground_cinder_zul, static(), 'ズルの焼けた礫（区画）')
    cat['ground_causeway_zul'] = (ground_causeway_zul, static(), '溶岩の堀に渡した石の桟')
    cat['grass_zul'] = (grass_zul, static(motion={'kind': 'wind'}, wind_k=0.7), '硬い草の株（tuft）')

    # ---- 門と灯り ----
    cat['arch_zul'] = (arch_zul, static(), '岩を穿った入口の額縁（南北にくぐる）')
    cat['arch_zul_ew'] = (arch_zul_ew, static(), '岩を穿った入口の額縁（東西にくぐる）')
    cat['arch_flame_zul'] = (arch_flame_zul, static(), '門の吊り灯の火（gate_flame）')
    cat['lamp_post_zul'] = (lamp_post_zul, static(), 'ズルの街灯（鉄の篝火台）')
    cat['lamp_flame_zul'] = (lamp_flame_zul, static(), 'ズルの街灯の炭火')

    # ---- 大物 ----
    cat['headframe_zul'] = (headframe_zul, headframe_parts(), '鉱山の巻き上げ櫓（2×2 マス・6 マス。索輪が回る）')
    cat['lavafall_zul'] = (lavafall_zul, lavafall_parts(), '溶岩の滝（1×2 マス・4 マス。落ちる塊と湯気）')
    cat['ropebridge_zul'] = (_ropebridge('y'), ropebridge_parts('y'), '吊り橋（南北に渡る・3 マス。橋板が揺れる）')
    cat['ropebridge_zul_ew'] = (_ropebridge('x'), ropebridge_parts('x'), '吊り橋（東西に渡る・3 マス）')
    cat['canyon_gate_zul'] = (_canyon_gate(False), static(), '谷の岩の門（南北にくぐる・3.6 マス）')
    cat['canyon_gate_zul_ew'] = (_canyon_gate(True), static(), '谷の岩の門（東西にくぐる）')
    cat['cliff_stair_zul'] = (cliff_stair_zul, static(), '岩を彫った大階段（1×2 マス・2.8 マス）')
    cat['watch_spire_zul'] = (watch_spire_zul, wind_parts(['flag']), '物見の櫓（5.6 マス。幟が風になびく）')
    cat['smelter_zul'] = (smelter_zul, static(), '溶鉱炉（3.6 マス。火は本体へ焼き込んである）')
    cat['ore_crusher_zul'] = (ore_crusher_zul, ore_crusher_parts(), '鉱石の砕き機（輪が回る）')

    # ---- 小物 ----
    simple = {
        'brazier': (brazier_zul, '篝火の鉄鉢（炎は焼き込んである）'),
        'rail_track': (rail_track_zul, '鉱車の軌道（南北）'),
        'ore_cart': (ore_cart_zul, '鉱車'),
        'ore_pile': (ore_pile_zul, '選り分けた鉱石の山'),
        'basalt_column': (basalt_column_zul, '折れた柱状節理の柱'),
        'scaffold': (scaffold_zul, '岩肌に組んだ木の足場'),
        'cairn': (cairn_zul, '道しるべの石積み'),
        'barrel': (barrel_zul, '樽'),
        'anvil': (anvil_zul, '金床'),
        'forge': (forge_zul, '鍛冶の炉（炭火は焼き込んである）'),
        'crystal': (crystal_zul, '鉱脈の結晶'),
        'tent': (tent_zul, '岩棚の掘っ立て小屋'),
        'weapon_rack': (weapon_rack_zul, '武器立て（槍と鶴嘴）'),
        'shield_rack': (shield_rack_zul, '盾と兜の台'),
        'sapling': (sapling_zul, '溶けた岩に根を張った若木'),
        'chaos_stone': (chaos_stone_zul, '歪んだ石'),
        'map_board': (map_board_zul, '観光案内の掲示板'),
        'bench': (bench_zul, '石の腰掛け'),
    }
    for key, (builder, note) in simple.items():
        cat[key + '_zul'] = (builder, static(), 'ズルの小物: ' + note)
    #! **入口の脇に並べる物は `_01` から始める名前で登録する。**
    #! `entrance_flanks` は `gather_into`（`_01`〜`_10` しか見ない）で引くので、
    #! 裸の名前だけで登録すると**その行ごと黙って捨てられる**（§7 の記録）。
    flank = {
        'crate': (crate_zul, 2, '木箱の積み'),
        'book_stack': (book_stack_zul, 2, '本の山と巻物'),
        'chest': (chest_zul, 1, '闇市の櫃'),
        'shrine': (shrine_zul, 1, '岩を刳った祠（灯明は焼き込んである）'),
        'herb_rack': (herb_rack_zul, 1, '干し台（硫黄と鉱物と乾いた草）'),
        'orb_stand': (orb_stand_zul, 1, '天球'),
        'ale_barrels': (ale_barrels_zul, 1, '麦酒の樽'),
    }
    for key, (builder, count, note) in flank.items():
        for i in range(1, count + 1):
            cat['%s_zul_%02d' % (key, i)] = (builder, static(), 'ズルの小物: ' + note)
    for i in range(1, 3):
        cat['rubble_zul_%02d' % i] = (rubble_zul, static(), 'ズルの小物: 崩れた岩屑')
    cat['awning_zul'] = (awning_zul, wind_parts(['cloth']), 'ズルの小物: 岩に張った日除け')
    cat['banner_zul'] = (banner_zul, wind_parts(['flag']), 'ズルの小物: 幟竿')
    cat['steam_vent_zul'] = (steam_vent_zul, wind_parts(['steam'], wind_k=0.5),
                             'ズルの小物: 蒸気の噴気口')
    cat['sulphur_vent_zul'] = (sulphur_vent_zul, wind_parts(['gas'], wind_k=0.5),
                               'ズルの小物: 硫黄の噴気口')
