# -*- coding: utf-8 -*-
"""モリバント（町 3 番）の作り込み — 素材の生成器。

`gen_prefabs.py` の末尾から `register(globals())` で呼ばれ、接尾辞 `_mor` の素材を
`CATALOG` へ足す。**既存の `_mor` の素材には 1 バイトも触らない**（別の名前で足すだけ）。

## 作りの規律（`gen_prefabs.py` 冒頭と同じ）
- 無作為な表面ノイズは入れない。彫りは 1 ボクセルの深さ。色は塊ごとに 1 色
- 接地層（z=0）の平面方向は縮めない。**+y は南＝カメラ側**。戸口は手前が y 大
- 柵の高さ（14）は変えない（遮蔽の約束）
- 色は共通の登録簿（`C`）を使い、足りない色だけ `reg_local`。
  **`reg_local` の名前は材質の語で始める**（`cloth_` `fur_` `soil_` `grass_` `dirt_` `wa_`）
  ——`palette_materials.classify` は「名前が材質の語で始まるか」で当てるので、
  `mor_cloth_r` のように町の頭を先に置くと材質が `Default` に落ちる（§7 の記録）。

小物は 1 マスに収める（接地する部分は z=0 のマスの中。上のほうがはみ出すのは可）。
火と光の相方（`*_fire_mor` `*_glow_mor`）は別体で、置く側が自発光をつけて重ねる。
"""
from __future__ import annotations

import math
import numpy as np

G = None   #: gen_prefabs の名前空間（register で入る）
C = None   #: 色の名前 → 索引
V = 32


# ------------------------------------------------------------------ 形の道具
def vol(h=32, w=None, d=None):
    """int16 の空の体積（`reg_local` の 1000 番台が入る）。"""
    return np.zeros((w or V, d or V, h), np.int16)


def box(v, x0, x1, y0, y1, z0, z1, colour):
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


def tile(h=2):
    """地面のタイル（天面が z=0 に来るよう origin_z = -h で返す）。"""
    return vol(h=h)


def one(v, z0=0):
    return [('main', v, (0, 0, z0))]


def ground(v, h):
    return [('main', v, (0, 0, -h))]


def K(name):
    return C[name]


def rng_for(name):
    return G['rng_for'](name)


def pick(rng, shades, weights=None):
    return int(rng.choice(shades, p=weights))


def mottle(rng, layer, shades, weights):
    G['_mottle'](rng, layer, shades, weights)


def grain(rng, layer, mask, shades, weights, block=2):
    G['_grain'](rng, layer, mask, shades, weights, block=block)


def static_part(motion=None, wind_k=0.0):
    return G['static_part'](motion, wind_k)


def swing_parts(names_pivots, amplitude=6.0, period=2.6):
    """`frame`（静止）＋ 吊り下げた部品（振り子）。`names_pivots` は [(名前, ピボット), …]。"""
    parts = [{'name': 'frame', 'voxels': 'frame', 'grounded': True,
              'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    for i, (name, pivot) in enumerate(names_pivots):
        parts.append({'name': name, 'voxels': name, 'parent': 'frame', 'grounded': False,
                      'pivot': [float(pivot[0]), float(pivot[1]), float(pivot[2])],
                      'motion': {'kind': 'pendulum', 'axis': 'x' if (i % 2 == 0) else 'y',
                                 'amplitude': float(amplitude), 'period': float(period) + (0.35 * i),
                                 'phase': 0.9 * i, 'damping': 0.0},
                      'wind_k': 0.0})
    return parts


# ------------------------------------------------------------------ 色
def _palette():
    """この意匠だけの色。**名前は材質の語で始める**（冒頭の註記）。"""
    rl = G['reg_local']
    #! 布（日除け・旗・垂れ幕・洗濯物）。`cloth` で布の材質になる。
    rl('cloth_mv_r', (168, 48, 44)); rl('cloth_mv_rd', (124, 34, 32))
    rl('cloth_mv_b', (52, 72, 140)); rl('cloth_mv_bd', (36, 52, 104))
    rl('cloth_mv_g', (58, 106, 78)); rl('cloth_mv_y', (206, 168, 90))
    rl('cloth_mv_w', (238, 236, 228)); rl('cloth_mv_wd', (206, 202, 194))
    #! 毛（猫・鳩・馬）。`fur` で布の材質（毛は織り物と同じ扱い）。
    rl('fur_mv_cat', (74, 66, 58)); rl('fur_mv_catl', (126, 114, 100))
    rl('fur_mv_dove', (98, 102, 116)); rl('fur_mv_dovel', (146, 150, 164))
    rl('fur_mv_horse', (96, 70, 50)); rl('fur_mv_horsed', (62, 44, 32))
    rl('fur_mv_horsew', (224, 220, 210))
    #! 馬糞・菜園の黒土（`soil` `dirt` で土の材質）。
    rl('soil_mv_dung', (92, 72, 46))
    rl('dirt_mv_bed', (72, 56, 40)); rl('dirt_mv_bedl', (98, 78, 58))
    #! 葉物と薬草（`grass` で草の材質。木の葉の描き込みは入れない）。
    rl('grass_mv_veg', (96, 140, 64)); rl('grass_mv_vegl', (132, 174, 88))
    rl('grass_mv_herb', (118, 138, 96))
    #! 溜まり水（`wa` で汚さない材質）。
    rl('wa_mv_mud', (86, 88, 78)); rl('wa_mv_mudl', (116, 118, 106))
    #! 蔦の葉（`leaf` で木の葉の材質）。
    rl('leaf_mv_ivy', (72, 106, 56)); rl('leaf_mv_ivyd', (50, 78, 42))
    #! 磨いた鋼と明るい真鍮（共通の表には `steel_l` / `brass_l` が無い）。
    rl('steel_mv_l', (216, 220, 228)); rl('brass_mv_l', (224, 192, 112))


def _wheel(v, cx, cz, r, y0, y1, spokes=6):
    """車輪（軸は y。輪・輻・轂）。**輻は線で引く**（塊で描くと車輪に見えない）。"""
    log_y(v, cx, cz, r, y0, y1, K('timber_d'))
    log_y(v, cx, cz, r - 1.6, y0, y1, 0)
    for k in range(spokes):
        a = k * (6.2832 / spokes)
        for t in range(int(r)):
            px = int(round(cx + t * np.cos(a)))
            pz = int(round(cz + t * np.sin(a)))
            box(v, px, px + 1, y0, y1, pz, pz + 1, K('timber'))
    log_y(v, cx, cz, 1.6, y0, y1, K('iron'))


# ------------------------------------------------------------------ 地面
def _flagstone(rng, face, step=16, shades=None, weights=None):
    """大判の切石を段ごとに半枚ずらして敷く（`_ground_path_flagstone` と同じ流儀）。"""
    shades = shades or [K('flag'), K('flag_l'), K('flag_d')]
    weights = weights or [0.48, 0.28, 0.24]
    for by in range(0, V, step):
        off = (by // step) % 2 * (step // 2)
        for bx in range(-step, V + step, step):
            x0 = bx + off
            shade = pick(rng, shades, weights)
            xa, xb = max(x0, 0), min(x0 + step - 1, V)
            ya, yb = by, min(by + step - 1, V)
            if xa < xb and ya < yb:
                face[xa:xb, ya:yb] = shade


def _cobble(rng, face, step=4):
    """玉石敷き（`_ground_turf_cobble` と同じ流儀。マスの幅で割り切れる）。"""
    for by in range(0, V, step):
        off = (by // step) % 2 * (step // 2)
        for bx in range(-step, V + step, step):
            x0 = bx + off
            shade = pick(rng, [K('cob'), K('cob_l'), K('cob_d'), K('ash_d')],
                         [0.40, 0.22, 0.24, 0.14])
            xa, xb = max(x0, 0), min(x0 + step - 1, V)
            ya, yb = by, min(by + step - 1, V)
            if xa < xb and ya < yb:
                face[xa:xb, ya:yb] = shade


def ground_market_mor(name):
    """市の広場 — 大判の切石に、藁くず・こぼれた麦・野菜くず・轍。

    表通り（`ground_path_mor_*`）と同じ石を敷いて**同じ広場の続き**に見せ、
    落ちている物だけで「市が立つ場所」を言う。
    """
    rng = rng_for(name)
    v = tile(2)
    box(v, 0, V, 0, V, 0, 1, K('joint'))          # 彫った目地の底
    face = np.zeros((V, V), np.int16)
    _flagstone(rng, face)
    grain(rng, face, face > 0, [K('pav_g1'), K('ash'), K('ash_l')], [0.12, 0.10, 0.08])
    # 轍（車輪の 2 筋）。段の目地より薄い線なので石の上を通してよい。
    x = int(rng.integers(4, 12))
    for y in range(V):
        for w in (x, x + 13):
            if 0 <= w < V - 1:
                face[w:w + 2, y] = K('ash_d')
        x += int(rng.integers(-1, 2))
    # 藁くず（短い線）とこぼれた麦（点）と野菜くず（塊）。**塊で置く**（罠 38）。
    for _ in range(int(rng.integers(5, 9))):
        px, py = int(rng.integers(0, V - 4)), int(rng.integers(0, V - 1))
        face[px:px + int(rng.integers(2, 5)), py] = K('straw_d') if rng.random() < 0.5 else K('hay_d')
    for _ in range(int(rng.integers(2, 5))):
        px, py = int(rng.integers(0, V - 2)), int(rng.integers(0, V - 2))
        face[px:px + 2, py:py + 2] = K('straw_l')
    for _ in range(int(rng.integers(1, 4))):
        px, py = int(rng.integers(0, V - 3)), int(rng.integers(0, V - 3))
        face[px:px + 2, py:py + 3] = K('grass_mv_veg')
    v[:, :, 1] = face
    return ground(v, 2)


def ground_muck_mor(name):
    """路地のぬかるみ — 玉石の間に泥が溜まり、溜まり水と轍と馬糞。"""
    rng = rng_for(name)
    v = tile(2)
    box(v, 0, V, 0, V, 0, 1, K('joint'))
    face = np.zeros((V, V), np.int16)
    _cobble(rng, face)
    # 泥の膜（塊で被せる。石が透ける所を残す）。
    for _ in range(int(rng.integers(4, 8))):
        px, py = int(rng.integers(0, V - 8)), int(rng.integers(0, V - 8))
        w, d = int(rng.integers(5, 11)), int(rng.integers(5, 11))
        face[px:px + w, py:py + d] = K('dirt_d') if rng.random() < 0.6 else K('silt')
    # 溜まり水（1 段彫って水を敷く）。
    for _ in range(int(rng.integers(1, 4))):
        px, py = int(rng.integers(0, V - 7)), int(rng.integers(0, V - 6))
        w, d = int(rng.integers(4, 8)), int(rng.integers(3, 7))
        face[px:px + w, py:py + d] = K('wa_mv_mud')
        face[px + 1:px + w - 1, py + 1:py + d - 1] = K('wa_mv_mudl')
    # 轍と馬糞。
    x = int(rng.integers(5, 14))
    for y in range(V):
        if 0 <= x < V - 1:
            face[x:x + 2, y] = K('dirt_d')
        x += int(rng.integers(-1, 2))
    for _ in range(int(rng.integers(1, 3))):
        px, py = int(rng.integers(0, V - 3)), int(rng.integers(0, V - 3))
        face[px:px + 3, py:py + 3] = K('soil_mv_dung')
    v[:, :, 1] = face
    return ground(v, 2)


def ground_garden_mor(name):
    """菜園の畝 — 黒土を東西に盛り、畝の頭に葉物を並べる。

    **厚みは 6 ボクセル**（畝 2 + 葉 2）。4 で作ると葉が配列の外へ出て**黙って消える**
    （`box` が範囲を切り詰めるので警告も出ない。§7 の記録）。
    """
    rng = rng_for(name)
    v = tile(6)
    box(v, 0, V, 0, V, 0, 4, K('dirt_mv_bed'))
    box(v, 0, V, 0, V, 4, 5, K('soil'))           # 畝の間（1 段低い）
    for y in range(2, V - 4, 8):                  # 畝（幅 5・1 段高い）
        box(v, 0, V, y, y + 5, 4, 6, K('dirt_mv_bedl'))
        box(v, 0, V, y, y + 1, 4, 6, K('dirt_mv_bed'))    # 畝の肩（暗く）
        box(v, 0, V, y + 4, y + 5, 4, 6, K('dirt_mv_bed'))
        for x in range(2, V - 2, 5):              # 葉物（畝の上に並ぶ）
            leaf = K('grass_mv_veg') if rng.random() < 0.6 else K('grass_mv_vegl')
            box(v, x, x + 3, y + 1, y + 4, 5, 6, leaf)
    return ground(v, 6)


def ground_grave_mor(name):
    """墓地の地面 — 短い芝に踏み跡の土。小石が覗く。"""
    rng = rng_for(name)
    v = tile(2)
    box(v, 0, V, 0, V, 0, 1, K('pack_d'))
    face = np.zeros((V, V), np.int16)
    mottle(rng, face, [K('turf_d'), K('turf'), K('moss_d'), K('pack')], [0.36, 0.28, 0.20, 0.16])
    for _ in range(int(rng.integers(2, 5))):      # 踏み跡（塊で抜く）
        px, py = int(rng.integers(0, V - 6)), int(rng.integers(0, V - 6))
        face[px:px + int(rng.integers(4, 8)), py:py + int(rng.integers(3, 7))] = K('pack')
    grain(rng, face, face > 0, [K('pebble'), K('gravel')], [0.05, 0.04], block=2)
    v[:, :, 1] = face
    return ground(v, 2)


def _gutter(name, along_x):
    """排水溝の走る石畳。溝は**1 段彫って**底に汚れ水を敷く（色だけの線にしない＝罠 44）。"""
    rng = rng_for(name)
    v = tile(2)
    box(v, 0, V, 0, V, 0, 1, K('joint'))
    face = np.zeros((V, V), np.int16)
    _flagstone(rng, face)
    grain(rng, face, face > 0, [K('pav_g1'), K('ash')], [0.10, 0.08])
    v[:, :, 1] = face
    lo, hi = 13, 19
    if along_x:                                   # 東西へ走る溝
        box(v, 0, V, lo, hi, 1, 2, 0)
        box(v, 0, V, lo, hi, 0, 1, K('ash_d'))
        box(v, 0, V, lo + 2, hi - 2, 0, 1, K('wa_mv_mud'))
        box(v, 0, V, lo, lo + 1, 1, 2, K('ash_l'))   # 溝の肩（縁石）
        box(v, 0, V, hi - 1, hi, 1, 2, K('ash_l'))
    else:                                         # 南北へ走る溝
        box(v, lo, hi, 0, V, 1, 2, 0)
        box(v, lo, hi, 0, V, 0, 1, K('ash_d'))
        box(v, lo + 2, hi - 2, 0, V, 0, 1, K('wa_mv_mud'))
        box(v, lo, lo + 1, 0, V, 1, 2, K('ash_l'))
        box(v, hi - 1, hi, 0, V, 1, 2, K('ash_l'))
    return ground(v, 2)


def ground_gutter_ns_mor(name):
    """排水溝（南北へ走る）。"""
    return _gutter(name, along_x=False)


def ground_gutter_ew_mor(name):
    """排水溝（東西へ走る）。"""
    return _gutter(name, along_x=True)


def ground_bridge_mor(name):
    """石橋の甲板 — 東西へ渡る橋面。迫石の継ぎ目を東西方向に通す。"""
    rng = rng_for(name)
    v = tile(3)
    box(v, 0, V, 0, V, 0, 2, K('ash_d'))
    face = np.zeros((V, V), np.int16)
    for x0 in range(0, V, 8):                     # 迫石（南北に長い石を東西へ並べる）
        shade = pick(rng, [K('ash'), K('ash_l'), K('marb')], [0.5, 0.3, 0.2])
        face[x0:x0 + 7, :] = shade
    grain(rng, face, face > 0, [K('ash_d'), K('pav_g1')], [0.08, 0.06])
    v[:, :, 2] = face
    return ground(v, 3)


#! **前庭の敷石（`yard_ground_mor`）は作らない。**
#! 置く側は `styled("yard_ground")` で**名前だけを見て**引くので、この名前で素材を置いた
#! 瞬間に、表を 1 行も書いていなくても 18 の敷地の前庭 382 マスの絵が変わる
#! （＝「表を当てる前の絵は着手前と同じ」という約束を破る）。前庭の地面を変えるなら
#! **置く側に表の項目を足してから**にする。§7 の記録。


# ------------------------------------------------------------------ 区画の柵
FENCE_H = 14


def _side(name):
    """名前の末尾から辺を採る（`railing_n_mor` → `'n'`）。"""
    for part in name.split('_'):
        if part in ('n', 's', 'w', 'e'):
            return part
    return 'n'


def _rim_span(side, th):
    """辺に沿う細長い箱の範囲（x0, x1, y0, y1）。"""
    if side == 'n':
        return (0, V, 0, th)
    if side == 's':
        return (0, V, V - th, V)
    if side == 'w':
        return (0, th, 0, V)
    return (V - th, V, 0, V)


def railing_mor(name):
    """錬鉄の低い柵 — 台石の上に立てた鉄柵。竪子の頭は槍先。高さは 14（遮蔽の約束）。"""
    side = _side(name)
    th = 4
    x0, x1, y0, y1 = _rim_span(side, th)
    v = vol(h=FENCE_H)
    box(v, x0, x1, y0, y1, 0, 3, K('ash_d'))                  # 台石
    box(v, x0, x1, y0 + (0 if side != 's' else 1), y1 - (0 if side != 'n' else 1), 2, 3, K('ash_l'))
    ax0, ax1, ay0, ay1 = _rim_span(side, 2)
    if side in ('n', 's'):
        ay0 = ay0 + 1 if side == 'n' else ay0
        for x in range(1, V, 4):                              # 竪子
            box(v, x, x + 1, ay0, ay0 + 1, 3, 12, K('iron'))
            box(v, x, x + 1, ay0, ay0 + 1, 12, 14, K('iron_l'))
        box(v, 0, V, ay0, ay0 + 1, 5, 6, K('iron_l'))         # 横桟
        box(v, 0, V, ay0, ay0 + 1, 10, 11, K('iron_l'))
        for x in range(0, V, 8):                              # 親柱
            box(v, x, x + 2, ay0 - 1, ay0 + 2, 3, 14, K('iron_l'))
    else:
        ax0 = ax0 + 1 if side == 'w' else ax0
        for y in range(1, V, 4):
            box(v, ax0, ax0 + 1, y, y + 1, 3, 12, K('iron'))
            box(v, ax0, ax0 + 1, y, y + 1, 12, 14, K('iron_l'))
        box(v, ax0, ax0 + 1, 0, V, 5, 6, K('iron_l'))
        box(v, ax0, ax0 + 1, 0, V, 10, 11, K('iron_l'))
        for y in range(0, V, 8):
            box(v, ax0 - 1, ax0 + 2, y, y + 2, 3, 14, K('iron_l'))
    return one(v)


def garden_wall_mor(name):
    """菜園の低い石垣 — 乱石積みに平らな笠石。高さは 14（遮蔽の約束）。"""
    side = _side(name)
    rng = rng_for(name)
    th = 6
    x0, x1, y0, y1 = _rim_span(side, th)
    v = vol(h=FENCE_H)
    box(v, x0, x1, y0, y1, 0, 12, K('rub_d'))
    # 石の目（横に並べる。縦縞に見せない）。**面の中なので四角形は増えない。**
    for z in range(1, 11, 3):
        p = int(rng.integers(0, 3))
        while p < V:
            w = int(rng.integers(4, 9))
            shade = pick(rng, [K('rub'), K('rub_l'), K('rub_d')], [0.5, 0.28, 0.22])
            if side in ('n', 's'):
                box(v, p, min(V, p + w), y0, y1, z, z + 2, shade)
            else:
                box(v, x0, x1, p, min(V, p + w), z, z + 2, shade)
            p += w + 1
    box(v, x0, x1, y0, y1, 12, 14, K('ash_l'))                # 笠石
    for _ in range(int(rng.integers(2, 5))):                  # 苔
        p = int(rng.integers(0, V - 5))
        if side in ('n', 's'):
            box(v, p, p + 5, y0, y1, 0, 4, K('moss_d'))
        else:
            box(v, x0, x1, p, p + 5, 0, 4, K('moss_d'))
    return one(v)


# ------------------------------------------------------------------ 都市の骨
def gate_tower_mor(name):
    """市門の櫓 — 道をまたぐ石のアーチ。上に狭間と旗。**中央は開けて道を通す。**

    城壁の切れ目（道のマス）に載せるので、脚は東西の端に置く。
    """
    rng = rng_for(name)
    v = vol(h=76)
    for x0 in (0, V - 7):                                     # 脚（切石積み）
        box(v, x0, x0 + 7, 4, 28, 0, 46, K('ash'))
        for z in range(0, 46, 6):
            box(v, x0, x0 + 7, 4, 28, z, z + 1, K('joint'))
            shade = pick(rng, [K('ash'), K('ash_l'), K('ash_d')], [0.5, 0.28, 0.22])
            box(v, x0, x0 + 7, 4, 28, z + 1, z + 5, shade)
        box(v, x0, x0 + 2, 4, 28, 0, 46, K('ash_l'))
    #! アーチ（内側へ迫り出す）。**4 段まで**——8 段だと開口が 2 ボクセルまで閉じて
    #! 「門」ではなく「割れ目の入った塔」に見えた（§7 の記録）。
    for k in range(1, 5):
        box(v, 7, 7 + k, 6, 26, 40 + k, 42 + k, K('ash_l'))
        box(v, V - 7 - k, V - 7, 6, 26, 40 + k, 42 + k, K('ash_l'))
    box(v, 7, V - 7, 4, 6, 0, 46, K('ash_d'))                 # 通路の奥（北の袖）
    box(v, 7, V - 7, 26, 28, 0, 46, K('ash_d'))               # 通路の手前（南の袖）
    box(v, 9, V - 9, 4, 28, 0, 40, 0)                         # 通路を刳り抜く
    box(v, 0, V, 4, 28, 46, 52, K('ash'))                     # 桁と床
    box(v, 0, V, 4, 28, 46, 47, K('ash_d'))
    box(v, 0, V, 2, 4, 48, 52, K('ash_l'))                    # 持ち送り（machicolation）
    box(v, 0, V, 28, 30, 48, 52, K('ash_l'))
    for x in range(2, V - 2, 6):
        box(v, x, x + 3, 2, 4, 48, 50, 0)
        box(v, x, x + 3, 28, 30, 48, 50, 0)
    box(v, 0, V, 4, 28, 52, 66, K('ash'))                     # 胸壁
    box(v, 4, V - 4, 8, 24, 52, 66, 0)                        # 中は開ける（歩廊）
    for x in range(2, V - 2, 7):                              # 狭間
        box(v, x, x + 4, 4, 28, 60, 66, 0)
    box(v, 12, 14, 12, 14, 52, 74, K('timber_d'))             # 旗竿
    box(v, 14, 24, 12, 13, 62, 73, K('cloth_mv_r'))
    box(v, 16, 21, 12, 13, 65, 70, K('gold'))
    return one(v)


def gate_tower_ew_mor(name):
    """市門の櫓（東西にくぐる姿勢）。**意匠は 1 つ**で、90° 回した写しを返す。

    置く側は `features` で 1 点ずつ名指すので、道の向きに合わせてこちらを書く。
    """
    return G['rotate_parts_90'](gate_tower_mor(name))


def market_cross_mor(name):
    """市の十字塔 — 4 段の四角い石の壇に八角の柱を立て、笠と十字を載せる。

    英国の市場町の market cross。**市がここで開かれる**という目印そのもので、
    段は腰掛けにも使われた。八角は「四角の角を落とす」で作る（円柱にすると
    32 ボクセルではぎざぎざの糸巻きに見えた。§7 の記録）。
    """
    def octagon(v, x0, x1, y0, y1, z0, z1, colour, cut):
        """四角の 4 隅を落として八角にする。"""
        box(v, x0, x1, y0, y1, z0, z1, colour)
        for k in range(cut):
            d = cut - k
            box(v, x0 + k, x0 + k + 1, y0, y0 + d, z0, z1, 0)
            box(v, x0 + k, x0 + k + 1, y1 - d, y1, z0, z1, 0)
            box(v, x1 - k - 1, x1 - k, y0, y0 + d, z0, z1, 0)
            box(v, x1 - k - 1, x1 - k, y1 - d, y1, z0, z1, 0)

    v = vol(h=64)
    for i, (inset, z0, z1) in enumerate(((0, 0, 4), (3, 4, 8), (6, 8, 12), (9, 12, 15))):
        shade = K('ash') if (i % 2) else K('ash_l')
        box(v, 1 + inset, 31 - inset, 1 + inset, 31 - inset, z0, z1, shade)
        box(v, 1 + inset, 31 - inset, 1 + inset, 31 - inset, z1 - 1, z1, K('ash_l'))
    octagon(v, 11, 21, 11, 21, 15, 19, K('ash'), 3)           # 柱の礎
    octagon(v, 12, 20, 12, 20, 19, 46, K('ash_l'), 2)         # 柱身
    for z in (24, 32, 40):                                    # 節の輪
        octagon(v, 11, 21, 11, 21, z, z + 2, K('ash'), 2)
    octagon(v, 9, 23, 9, 23, 46, 49, K('ash'), 3)             # 台輪
    for z in range(49, 57):                                   # 笠（角錐）
        d = (z - 49)
        box(v, 8 + d, 24 - d, 8 + d, 24 - d, z, z + 1, K('ash_l') if (z % 2) else K('ash'))
    box(v, 14, 18, 14, 18, 57, 63, K('ash_l'))                # 十字
    box(v, 10, 22, 14, 18, 58, 61, K('ash_l'))
    box(v, 14, 18, 10, 22, 58, 61, K('ash_l'))
    box(v, 15, 17, 15, 17, 63, 64, K('gold'))
    return one(v)


def market_stall_mor(name):
    """市の屋台 — 4 本の柱に布の日除け、台の上に品物。変種で布と品物が変わる。"""
    rng = rng_for(name)
    kind = int(name[-2:]) if name[-2:].isdigit() else 1
    v = vol(h=40)
    cloth, cloth_d = ((K('cloth_mv_r'), K('cloth_mv_rd')), (K('cloth_mv_b'), K('cloth_mv_bd')),
                      (K('cloth_mv_g'), K('cloth_mv_y')))[(kind - 1) % 3]
    for (x, y) in ((3, 5), (28, 5), (3, 26), (28, 26)):       # 柱
        box(v, x, x + 2, y, y + 2, 0, 30, K('timber_d'))
    box(v, 2, 31, 4, 29, 30, 31, K('timber'))                 # 桁
    for x in range(2, 31, 5):                                 # 日除け（縞）
        box(v, x, x + 3, 3, 30, 31, 33, cloth)
        box(v, x + 3, x + 5, 3, 30, 31, 33, cloth_d)
    box(v, 2, 31, 3, 30, 33, 34, cloth_d)
    box(v, 2, 31, 28, 30, 25, 33, cloth)                      # 南へ垂らした前掛け
    box(v, 4, 29, 8, 24, 13, 16, K('board'))                  # 台
    box(v, 4, 29, 8, 24, 12, 13, K('board_d'))
    for x in (5, 27):
        box(v, x, x + 2, 9, 23, 0, 13, K('timber_d'))
    if kind == 1:                                             # 魚
        for i in range(5):
            x = 6 + (i * 4)
            ball(v, x, 12 + (i % 2) * 6, 17, 2.4, K('steel'), squash=0.55)
            box(v, x + 2, x + 3, 12 + (i % 2) * 6, 13 + (i % 2) * 6, 16, 19, K('steel_d'))
    elif kind == 2:                                           # 青物
        for i in range(6):
            x = 6 + (i * 4)
            ball(v, x, 11 + (i % 3) * 5, 18, 2.6, K('grass_mv_veg') if i % 2 else K('grass_mv_vegl'))
        box(v, 20, 27, 17, 23, 16, 21, K('timber'))           # 籠
        box(v, 21, 26, 18, 22, 17, 21, K('fl_y'))
    else:                                                     # 布地
        for i, col in enumerate((K('cloth_mv_r'), K('cloth_mv_b'), K('cloth_mv_y'), K('cloth_mv_w'))):
            box(v, 5 + (i * 6), 10 + (i * 6), 9, 23, 16, 19, col)
            box(v, 5 + (i * 6), 10 + (i * 6), 21, 23, 10, 17, col)
    return one(v)


def pillory_mor(name):
    """さらし台 — 石の段に太い柱を 2 本、首と手を挟む厚い板。

    **段も柱も板も太くする**（細いと 2 本の棒にしか見えない。§7 の記録）。
    """
    v = vol(h=42)
    box(v, 3, 29, 8, 26, 0, 6, K('ash'))                      # 段（2 段）
    box(v, 3, 29, 8, 26, 5, 6, K('ash_l'))
    box(v, 6, 26, 11, 23, 6, 10, K('ash'))
    box(v, 6, 26, 11, 23, 9, 10, K('ash_l'))
    for x in (6, 22):                                         # 柱
        box(v, x, x + 4, 14, 19, 10, 36, K('timber_d'))
        box(v, x, x + 4, 14, 15, 10, 36, K('timber'))
        box(v, x - 1, x + 5, 13, 20, 34, 36, K('timber'))     # 冠木
    box(v, 5, 27, 14, 19, 24, 28, K('board'))                 # 下の板
    box(v, 5, 27, 14, 19, 28, 32, K('board_l'))               # 上の板
    box(v, 5, 27, 14, 15, 24, 32, K('board_d'))               # 板の木口
    for x in (10, 15, 20):                                    # 首と手の穴
        box(v, x, x + 3, 13, 20, 26, 30, 0)
    box(v, 5, 27, 13, 14, 27, 29, K('iron'))                  # 蝶番
    box(v, 25, 28, 13, 20, 26, 31, K('iron_l'))               # ロック
    box(v, 8, 11, 20, 24, 10, 12, K('rag'))                   # 投げつけられた物
    return one(v)


def gallows_mor(name):
    """絞首台 — 石の基壇に 2 本の柱と横木、垂らした縄と梯子。"""
    v = vol(h=64)
    box(v, 4, 28, 8, 26, 0, 5, K('ash_d'))                    # 基壇
    box(v, 5, 27, 9, 25, 4, 5, K('ash'))
    for x in (7, 23):                                         # 柱
        box(v, x, x + 3, 15, 18, 5, 54, K('timber_d'))
        box(v, x - 1, x + 4, 14, 19, 5, 8, K('timber'))       # 根元の飼い木
    box(v, 6, 26, 15, 18, 54, 58, K('timber'))                # 横木
    box(v, 4, 28, 15, 18, 58, 59, K('timber_d'))
    for x in (10, 20):                                        # 方杖
        box(v, x, x + 2, 16, 17, 46, 54, K('timber_d'))
    box(v, 15, 17, 16, 17, 40, 54, K('lash'))                 # 縄
    disc(v, 16, 16, 2.4, 36, 40, K('lash'), hollow=1.2)       # 輪
    for z in range(6, 50, 5):                                 # 梯子
        box(v, 24, 29, 20, 21, z, z + 1, K('timber'))
    box(v, 24, 25, 20, 21, 5, 50, K('timber'))
    box(v, 28, 29, 20, 21, 5, 50, K('timber'))
    return one(v)


def bell_tower_mor(name):
    """鐘楼 — 切石の櫓に鐘（振り子で揺れる）。屋根は瓦の四角錐。"""
    rng = rng_for(name)
    frame = vol(h=88)
    box(frame, 5, 27, 5, 27, 0, 4, K('ash_d'))                # 基壇
    box(frame, 7, 25, 7, 25, 4, 58, K('ash'))                 # 塔身
    for z in range(4, 58, 7):                                 # 石の段（彫った目地）
        box(frame, 7, 25, 7, 25, z, z + 1, K('joint'))
        shade = pick(rng, [K('ash'), K('ash_l')], [0.6, 0.4])
        box(frame, 7, 25, 7, 25, z + 1, z + 6, shade)
    box(frame, 13, 19, 24, 25, 6, 22, K('door_w'))            # 南面の扉
    box(frame, 13, 19, 24, 25, 20, 22, K('door_wd'))
    box(frame, 8, 24, 8, 24, 58, 60, K('ash_l'))              # 鐘楼の床
    for (x, y) in ((7, 7), (23, 7), (7, 23), (23, 23)):       # 鐘楼の柱
        box(frame, x, x + 2, y, y + 2, 60, 76, K('ash_l'))
    box(frame, 7, 25, 7, 25, 76, 78, K('ash'))                # 桁
    box(frame, 10, 22, 15, 17, 74, 76, K('timber_d'))         # 鐘を吊る梁
    for z in range(78, 88):                                   # 瓦の四角錐
        d = (z - 78)
        box(frame, 5 + d, 27 - d, 5 + d, 27 - d, z, z + 1,
            K('tile_d') if (z % 3 == 0) else K('tile'))
    bell = vol(h=88)
    for z in range(62, 74):                                   # 鐘（下ほど広い）
        r = 2.0 + ((74 - z) * 0.45)
        disc(bell, 16, 16, r, z, z + 1, K('brass') if (z % 4) else K('brass_d'))
    disc(bell, 16, 16, 7.4, 62, 63, K('brass_d'))
    box(bell, 15, 17, 15, 17, 74, 76, K('brass_d'))           # 冠
    box(bell, 15, 17, 15, 17, 58, 62, K('brass_d'))           # 舌
    return [('frame', frame, (0, 0, 0)), ('bell', bell, (0, 0, 0))]


def bell_parts():
    return swing_parts([('bell', (16, 16, 75))], amplitude=7.0, period=3.4)


def saint_statue_mor(name):
    """聖人の像 — 暗い石の台座の上に、白い石の立像。

    **台座と像の色を分ける**（同じ石で作ると段だけが見えて人に読めない。§7 の記録）。
    衣の裾は 2 段だけ広げ、腕・襟・頭・光輪で人の輪郭を出す。
    """
    v = vol(h=60)
    box(v, 5, 27, 6, 26, 0, 4, K('rub_d'))                    # 台座（暗い乱石）
    box(v, 7, 25, 8, 24, 4, 12, K('rub'))
    box(v, 6, 26, 7, 25, 11, 13, K('rub_l'))                  # 台座の笠
    box(v, 9, 23, 10, 22, 13, 15, K('ash_d'))                 # 沓台
    box(v, 12, 20, 13, 19, 15, 17, K('marb'))                 # 沓
    box(v, 11, 21, 12, 20, 17, 24, K('marb'))                 # 衣の裾（下段）
    box(v, 12, 20, 13, 19, 24, 34, K('marb_l'))               # 衣（上段）
    for z in range(18, 34, 4):                                # 衣の襞（縦の線）
        box(v, 13, 14, 12, 20, z, z + 3, K('marb'))
        box(v, 18, 19, 12, 20, z, z + 3, K('marb'))
    box(v, 9, 23, 12, 20, 34, 37, K('marb'))                  # 肩（衣より広い）
    box(v, 8, 12, 14, 18, 24, 36, K('marb'))                  # 左腕（下ろす）
    box(v, 20, 24, 14, 18, 26, 36, K('marb'))                 # 右腕（前へ）
    box(v, 8, 12, 14, 18, 24, 26, K('cream'))                 # 手
    box(v, 20, 24, 14, 18, 26, 28, K('cream'))
    box(v, 20, 24, 18, 21, 26, 30, K('marb_l'))
    box(v, 20, 24, 19, 21, 27, 29, K('oak'))                  # 手にした本
    box(v, 9, 11, 15, 17, 16, 44, K('ash_d'))                 # 杖
    box(v, 8, 12, 14, 18, 44, 46, K('gold'))
    box(v, 11, 21, 13, 19, 36, 38, K('marb'))                 # 襟
    box(v, 14, 18, 14, 18, 38, 41, K('ash_d'))                # 首（暗くして頭を切る）
    box(v, 12, 20, 13, 19, 41, 48, K('cream'))                # 頭
    box(v, 12, 20, 12, 14, 42, 47, K('marb'))                 # 頭巾（後ろ）
    box(v, 13, 19, 19, 20, 43, 46, K('marb'))                 # 顔（南を向く）
    disc(v, 16, 15, 5.6, 48, 49, K('gold'), hollow=4.0)       # 光輪
    return one(v)


def city_well_mor(name):
    """都市の井戸 — 切石の井筒に石の柱と瓦屋根、滑車と桶。"""
    rng = rng_for(name)
    v = vol(h=52)
    for z in range(0, 12, 3):                                 # 井筒
        disc(v, 16, 16, 10.0, z, z + 3, K('ash'), hollow=7.0)
        for k in range(8):
            a = (rng.random() * 0.6) + (k * 0.785)
            x, y = int(16 + 8.5 * np.cos(a)), int(16 + 8.5 * np.sin(a))
            box(v, x, x + 1, y, y + 1, z, z + 3, K('joint'))
    disc(v, 16, 16, 10.0, 11, 12, K('ash_l'), hollow=7.0)     # 笠石
    disc(v, 16, 16, 7.0, 0, 2, K('wa_dd'))                    # 水面
    for x in (6, 24):                                         # 柱
        box(v, x, x + 3, 15, 18, 0, 38, K('ash'))
        box(v, x, x + 3, 15, 18, 36, 38, K('ash_l'))
    log_x(v, 16, 39, 2.0, 4, 28, K('timber'))                 # 桁
    for z in range(40, 48):                                   # 瓦の切妻屋根
        d = (z - 40)
        box(v, 2 + d, 30 - d, 8, 24, z, z + 1, K('tile_d') if (z % 3 == 0) else K('tile'))
    box(v, 14, 18, 8, 24, 47, 49, K('ridge'))
    disc(v, 16, 16, 2.6, 36, 39, K('iron'))                   # 滑車
    box(v, 15, 17, 15, 17, 20, 36, K('lash'))                 # 縄
    box(v, 13, 19, 13, 19, 15, 20, K('oak_l'))                # 桶
    box(v, 13, 19, 13, 19, 15, 16, K('iron'))
    return one(v)


def _bridge_rail(name, north):
    """石橋の欄干 — 水に面した辺に立てる。親柱に小さな灯。"""
    rng = rng_for(name)
    v = vol(h=26)
    y0, y1 = (0, 5) if north else (V - 5, V)
    box(v, 0, V, y0, y1, 0, 16, K('ash'))
    for z in range(1, 15, 4):                                 # 石の段
        p = int(rng.integers(0, 3))
        while p < V:
            w = int(rng.integers(5, 10))
            box(v, p, min(V, p + w), y0, y1, z, z + 3,
                pick(rng, [K('ash'), K('ash_l'), K('ash_d')], [0.5, 0.28, 0.22]))
            p += w + 1
    box(v, 0, V, y0, y1, 16, 18, K('ash_l'))                  # 笠石
    for x in (0, V - 6):                                      # 親柱
        box(v, x, x + 6, y0 - 1 if north else y0, y1 + 1 if not north else y1, 0, 22, K('ash_l'))
        box(v, x + 1, x + 5, y0, y1, 22, 24, K('ash_d'))
        box(v, x + 2, x + 4, y0 + 1, y1 - 1, 24, 26, K('gold'))
    return one(v)


def bridge_rail_n_mor(name):
    """石橋の欄干（北の辺）。"""
    return _bridge_rail(name, north=True)


def bridge_rail_s_mor(name):
    """石橋の欄干（南の辺）。"""
    return _bridge_rail(name, north=False)


# ------------------------------------------------------------------ 通りと生活
def crate_pile_mor(name):
    """木箱の積み — 3 つ。縁の板と釘。"""
    rng = rng_for(name)
    v = vol(h=30)
    for (x, y, z, w) in ((3, 6, 0, 12), (16, 4, 0, 13), (5, 10, 12, 11)):
        shade = pick(rng, [K('oak'), K('oak_l'), K('timber')], [0.4, 0.34, 0.26])
        box(v, x, x + w, y, y + w, z, z + w, shade)
        box(v, x, x + w, y, y + 1, z, z + w, K('timber_d'))   # 縁の板
        box(v, x, x + w, y + w - 1, y + w, z, z + w, K('timber_d'))
        box(v, x, x + 1, y, y + w, z, z + w, K('timber_d'))
        box(v, x + w - 1, x + w, y, y + w, z, z + w, K('timber_d'))
        box(v, x + 1, x + w - 1, y + 1, y + w - 1, z + w - 1, z + w, K('board_l'))
    return one(v)


def barrel_stack_mor(name):
    """樽 — 立てた 2 つと横たえた 1 つ。鉄の箍。"""
    v = vol(h=30)
    for (cx, cy) in ((9, 10), (22, 12)):
        for z in range(16):
            r = 5.6 - (abs(z - 8) * 0.16)
            disc(v, cx, cy, r, z, z + 1, K('timber') if (z % 5) else K('timber_l'))
        for z in (2, 12):
            disc(v, cx, cy, 5.9, z, z + 2, K('iron'), hollow=4.6)
        disc(v, cx, cy, 4.6, 15, 16, K('timber_d'))
    log_x(v, 24, 5, 5.2, 2, 26, K('timber'))                  # 横たえた樽
    log_x(v, 24, 5, 5.6, 6, 9, K('iron'))
    log_x(v, 24, 5, 5.6, 19, 22, K('iron'))
    return one(v)


def sack_pile_mor(name):
    """麻袋 — 3 つ。口を縄で縛ってある。"""
    rng = rng_for(name)
    v = vol(h=24)
    for (cx, cy, h) in ((9, 9, 14), (21, 12, 16), (13, 21, 12)):
        for z in range(h):
            t = z / float(h)
            r = 5.4 - (t * t * 3.2)
            disc(v, cx, cy, r, z, z + 1, K('hay') if (z % 4) else K('hay_d'))
        box(v, cx - 1, cx + 2, cy - 1, cy + 2, h, h + 2, K('lash'))
        if rng.random() < 0.5:
            box(v, cx - 2, cx + 3, cy - 2, cy + 3, 3, 5, K('hay_d'))
    return one(v)


def handcart_mor(name):
    """手押し荷車 — 二輪。梶棒を地面に下ろし、荷台に袋と籠。"""
    v = vol(h=26)
    box(v, 5, 27, 9, 23, 8, 11, K('oak'))                     # 荷台
    box(v, 5, 27, 9, 10, 11, 16, K('oak_l'))                  # あおり
    box(v, 5, 27, 22, 23, 11, 16, K('oak_l'))
    box(v, 5, 6, 9, 23, 11, 16, K('oak_l'))
    for cy in (7, 25):                                        # 車輪
        _wheel(v, 8, 6, 6.0, cy - 1, cy + 1)
    box(v, 24, 31, 12, 14, 2, 4, K('timber'))                 # 梶棒（下ろしてある）
    box(v, 24, 31, 18, 20, 2, 4, K('timber'))
    for z in range(11):                                       # 荷（袋）
        r = 4.6 - (z * 0.3)
        disc(v, 12, 16, r, 11 + z, 12 + z, K('hay') if (z % 3) else K('hay_d'))
    box(v, 18, 25, 12, 20, 11, 18, K('timber_l'))             # 籠
    box(v, 19, 24, 13, 19, 12, 18, K('fl_y'))
    return one(v)


def wagon_horse_mor(name):
    """荷馬車と馬 — 四輪の荷馬車に馬を 1 頭つないである。馬は**東を向く**。

    馬は「太い箱を 1 つ」では馬に読めない（§7 の記録）。胴は薄く長く、脚は細く、
    首は斜めに上げ、頭は前へ出す。首の後ろに鬣、尻に尾。
    """
    v = vol(h=34)
    box(v, 1, 18, 8, 24, 9, 12, K('oak'))                     # 荷台
    box(v, 1, 18, 8, 9, 12, 19, K('oak_l'))
    box(v, 1, 18, 23, 24, 12, 19, K('oak_l'))
    box(v, 1, 2, 8, 24, 12, 19, K('oak_l'))
    box(v, 3, 16, 10, 22, 12, 17, K('hay'))                   # 積んだ干し草
    box(v, 3, 16, 10, 22, 16, 17, K('hay_d'))
    for (cx, cy, r) in ((4, 7, 6.0), (4, 25, 6.0), (15, 7, 6.6), (15, 25, 6.6)):
        _wheel(v, cx, r, r, cy - 1, cy + 1)
    box(v, 17, 22, 15, 17, 10, 12, K('timber'))               # 轅（ながえ）
    # 馬。胴は 4 マス幅の薄い箱、脚 4 本、首と頭は東（+x）へ。
    box(v, 21, 30, 13, 19, 13, 19, K('fur_mv_horse'))         # 胴
    box(v, 21, 30, 13, 19, 18, 19, K('fur_mv_horsed'))        # 背
    box(v, 21, 30, 18, 19, 13, 19, K('fur_mv_horsed'))        # 南側の陰
    for (x, y) in ((22, 13), (22, 17), (28, 13), (28, 17)):   # 脚
        box(v, x, x + 2, y, y + 2, 2, 14, K('fur_mv_horse'))
        box(v, x, x + 2, y, y + 2, 0, 2, K('char'))           # 蹄
    for k in range(6):                                        # 首（斜めに上がる）
        box(v, 29 + (k // 3), 31 + (k // 3), 14, 18, 18 + k, 20 + k, K('fur_mv_horse'))
    box(v, 28, 32, 14, 18, 24, 27, K('fur_mv_horse'))         # 頭
    box(v, 27, 29, 15, 17, 24, 26, K('fur_mv_horse'))         # 鼻面
    box(v, 27, 28, 15, 17, 25, 26, K('fur_mv_horsew'))        # 星（白斑）
    box(v, 30, 32, 14, 15, 26, 28, K('fur_mv_horse'))         # 耳
    box(v, 30, 32, 17, 18, 26, 28, K('fur_mv_horse'))
    for k in range(7):                                        # 鬣
        box(v, 28 - (k // 3), 30 - (k // 3), 15, 17, 19 + k, 21 + k, K('fur_mv_horsed'))
    box(v, 19, 22, 15, 17, 12, 18, K('fur_mv_horsed'))        # 尾
    box(v, 20, 21, 15, 17, 6, 13, K('fur_mv_horsed'))
    box(v, 21, 30, 13, 14, 15, 17, K('lash'))                 # 引き綱
    box(v, 17, 22, 15, 17, 12, 14, K('lash'))
    return one(v)


def hitch_rail_mor(name):
    """馬つなぎ — 2 本の柱に横木。手綱の輪と鉄環。"""
    v = vol(h=26)
    for x in (4, 26):
        box(v, x, x + 3, 14, 17, 0, 22, K('timber_d'))
        box(v, x - 1, x + 4, 13, 18, 21, 23, K('timber'))
    box(v, 3, 29, 14, 17, 16, 19, K('timber'))
    box(v, 3, 29, 14, 15, 16, 19, K('timber_l'))
    for x in (10, 20):
        disc(v, x, 15, 2.0, 12, 13, K('iron'), hollow=1.0)
        box(v, x, x + 1, 15, 16, 13, 17, K('iron'))
    box(v, 14, 18, 15, 16, 8, 17, K('lash'))                  # 垂れた手綱
    return one(v)


def horse_trough_mor(name):
    """石の水槽 — 馬の水飲み場。縁に苔、水面に波。"""
    rng = rng_for(name)
    v = vol(h=16)
    box(v, 2, 30, 8, 24, 0, 11, K('ash'))
    for z in range(1, 10, 3):
        p = int(rng.integers(0, 3))
        while p < 30:
            w = int(rng.integers(5, 9))
            box(v, p, min(30, p + w), 8, 24, z, z + 2,
                pick(rng, [K('ash'), K('ash_l'), K('ash_d')], [0.5, 0.28, 0.22]))
            p += w + 1
    box(v, 2, 30, 8, 24, 11, 14, K('ash_l'))                  # 縁（先に立てる）
    box(v, 4, 28, 10, 22, 4, 14, 0)                           # 内を刳る
    box(v, 4, 28, 10, 22, 4, 11, K('wa_d'))                   # 水（縁より下）
    box(v, 4, 28, 10, 22, 10, 11, K('wa'))
    for _ in range(3):
        x = int(rng.integers(5, 24))
        box(v, x, x + 4, 10, 22, 10, 11, K('wa_l'))
    box(v, 2, 8, 8, 24, 0, 4, K('moss_d'))
    return one(v)


def stone_bench_mor(name):
    """石の腰掛け — 2 つの脚に厚い石板。片側に苔。"""
    v = vol(h=14)
    for x in (4, 22):
        box(v, x, x + 6, 12, 20, 0, 8, K('ash'))
        box(v, x, x + 6, 12, 20, 6, 8, K('ash_d'))
    box(v, 1, 31, 10, 22, 8, 12, K('ash_l'))
    box(v, 1, 31, 10, 11, 8, 12, K('ash'))
    box(v, 1, 31, 21, 22, 8, 12, K('ash'))
    box(v, 1, 9, 10, 22, 8, 9, K('moss_d'))
    return one(v)


def planter_mor(name):
    """石の花壇 — 切石の枠に土と花。"""
    rng = rng_for(name)
    v = vol(h=16)
    box(v, 2, 30, 6, 26, 0, 9, K('ash'))
    box(v, 2, 30, 6, 26, 8, 9, K('ash_l'))
    box(v, 4, 28, 8, 24, 0, 8, K('dirt_mv_bed'))
    box(v, 4, 28, 8, 24, 7, 8, K('soil'))
    for _ in range(int(rng.integers(7, 12))):                 # 花と葉（塊で）
        x, y = int(rng.integers(4, 26)), int(rng.integers(8, 22))
        box(v, x, x + 3, y, y + 3, 8, 10, K('leaf_m'))
        col = pick(rng, [K('fl_r'), K('fl_y'), K('fl_w')], [0.4, 0.32, 0.28])
        box(v, x + 1, x + 3, y + 1, y + 3, 10, 12, col)
    return one(v)


def dung_pile_mor(name):
    """馬糞と藁 — 通りに落ちているもの。低い。"""
    rng = rng_for(name)
    v = vol(h=8)
    for _ in range(int(rng.integers(3, 6))):
        cx, cy = int(rng.integers(8, 24)), int(rng.integers(8, 24))
        ball(v, cx, cy, 1, 3.0, K('soil_mv_dung'), squash=0.5)
    for _ in range(int(rng.integers(4, 8))):                  # 混じった藁
        x, y = int(rng.integers(4, 27)), int(rng.integers(4, 28))
        box(v, x, x + 4, y, y + 1, 1, 2, K('straw_d'))
    return one(v)


def gutter_grate_mor(name):
    """排水の鉄格子 — 石畳に嵌めた鉄の格子。低い。"""
    v = vol(h=4)
    box(v, 4, 28, 10, 22, 0, 2, K('ash_d'))
    box(v, 6, 26, 12, 20, 0, 1, K('wa_mv_mud'))
    for x in range(6, 26, 4):                                 # 格子
        box(v, x, x + 2, 11, 21, 1, 3, K('iron'))
    box(v, 4, 28, 10, 12, 1, 3, K('iron_l'))
    box(v, 4, 28, 20, 22, 1, 3, K('iron_l'))
    box(v, 4, 6, 10, 22, 1, 3, K('iron_l'))
    box(v, 26, 28, 10, 22, 1, 3, K('iron_l'))
    return one(v)


def alley_junk_mor(name):
    """路地のがらくた — 壊れた樽、立て掛けた細い板、割れた壺、布切れ、籠。

    **板は 2 ボクセル幅で隙間を空ける**（幅を持たせると茶色い衝立に見えた。§7 の記録）。
    """
    rng = rng_for(name)
    v = vol(h=24)
    log_x(v, 8, 5, 5.0, 2, 16, K('timber_d'))                 # 横たえた壊れ樽
    log_x(v, 8, 5, 3.6, 2, 16, 0)
    log_x(v, 8, 5, 5.4, 4, 6, K('rust'))
    box(v, 10, 16, 3, 9, 4, 11, 0)                            # 割れた口
    for x in range(20, 30, 3):                                # 立て掛けた板（隙間つき）
        tall = int(rng.integers(13, 22))
        box(v, x, x + 2, 24, 27, 0, tall, K('board_d') if (x % 2) else K('board'))
        box(v, x, x + 2, 21, 24, 0, tall // 2, K('board_d'))
    for z in range(6):                                        # 割れた壺
        r = 4.2 - abs(z - 3) * 0.5
        disc(v, 23, 10, r, z, z + 1, K('clay') if (z % 2) else K('clay_d'))
    box(v, 23, 29, 6, 11, 0, 6, 0)
    box(v, 3, 11, 18, 26, 0, 2, K('rag'))                     # 布切れ
    box(v, 5, 9, 20, 25, 1, 4, K('cloth_d'))
    for z in range(8):                                        # 潰れた籠
        disc(v, 14, 20, 5.0 - (z * 0.2), z, z + 1, K('timber_l') if (z % 2) else K('hay_d'))
    disc(v, 14, 20, 3.4, 4, 8, 0)
    return one(v)


def cat_mor(name):
    """猫 — 路地に伏せている。**横に長い**（座り姿は上から見ると塊になる。§7 の記録）。

    胴は東西に長く、頭を南（カメラ側）へ向け、尾を胴に沿わせる。
    """
    v = vol(h=16)
    box(v, 8, 24, 13, 19, 2, 8, K('fur_mv_cat'))              # 胴（伏せ）
    box(v, 8, 24, 13, 19, 7, 8, K('fur_mv_catl'))             # 背の明るい毛
    box(v, 9, 23, 18, 19, 2, 8, K('fur_mv_catl'))             # 南面
    box(v, 8, 12, 12, 20, 0, 4, K('fur_mv_cat'))              # 前脚（畳んである）
    box(v, 20, 24, 12, 20, 0, 4, K('fur_mv_cat'))             # 後脚
    box(v, 20, 30, 11, 13, 1, 3, K('fur_mv_cat'))             # 尾（胴に沿って巻く）
    box(v, 28, 30, 11, 19, 1, 3, K('fur_mv_cat'))
    box(v, 26, 30, 17, 19, 1, 3, K('fur_mv_catl'))
    box(v, 10, 18, 14, 18, 8, 10, K('fur_mv_cat'))            # 首
    box(v, 10, 18, 13, 20, 9, 15, K('fur_mv_cat'))            # 頭
    box(v, 11, 17, 19, 20, 10, 14, K('fur_mv_catl'))          # 顔（南向き）
    box(v, 10, 12, 14, 18, 15, 18, K('fur_mv_cat'))           # 耳
    box(v, 16, 18, 14, 18, 15, 18, K('fur_mv_cat'))
    box(v, 11, 12, 19, 20, 12, 13, K('fl_y'))                 # 目
    box(v, 16, 17, 19, 20, 12, 13, K('fl_y'))
    box(v, 13, 15, 19, 20, 10, 11, K('fur_mv_catl'))          # 鼻先
    return one(v)


def pigeons_mor(name):
    """鳩 — 3 羽。灰色の胴に暗い翼帯、赤い脚。餌をつつくものと立っているもの。

    **明るい色で全身を覆わない**（初版は上から見て白い塊にしか見えなかった。§7 の記録）。
    """
    rng = rng_for(name)
    v = vol(h=14)
    for (cx, cy, up) in ((8, 9, True), (21, 15, False), (14, 24, True)):
        ball(v, cx, cy, 5, 3.8, K('fur_mv_dove'), squash=0.85)   # 胴
        box(v, cx - 4, cx + 4, cy - 3, cy + 1, 4, 6, K('fur_mv_dove'))
        box(v, cx - 4, cx + 4, cy - 3, cy - 1, 4, 6, K('char'))  # 翼帯（暗い線）
        box(v, cx - 5, cx - 3, cy - 3, cy + 2, 3, 6, K('fur_mv_dove'))  # たたんだ翼
        box(v, cx + 3, cx + 5, cy - 3, cy + 2, 3, 6, K('fur_mv_dove'))
        if up:
            box(v, cx - 1, cx + 2, cy + 2, cy + 4, 6, 10, K('fur_mv_dovel'))  # 首
            ball(v, cx, cy + 4, 10, 2.2, K('fur_mv_dovel'))                   # 頭
            box(v, cx, cx + 1, cy + 5, cy + 7, 9, 10, K('fl_y'))              # 嘴
        else:
            box(v, cx - 1, cx + 2, cy + 3, cy + 5, 4, 7, K('fur_mv_dovel'))
            ball(v, cx, cy + 5, 6, 2.2, K('fur_mv_dovel'))
            box(v, cx, cx + 1, cy + 6, cy + 8, 5, 6, K('fl_y'))
        box(v, cx - 2, cx - 1, cy, cy + 1, 0, 4, K('fl_r'))      # 脚
        box(v, cx + 1, cx + 2, cy, cy + 1, 0, 4, K('fl_r'))
        if rng.random() < 0.5:                                   # 開いた尾
            box(v, cx - 3, cx + 3, cy - 6, cy - 3, 4, 5, K('fur_mv_dove'))
    return one(v)


def ivy_mor(name):
    """蔦 — 崩れた石の上に絡む蔦。蔓が立ち上がって葉を広げる。"""
    rng = rng_for(name)
    v = vol(h=32)
    box(v, 2, 30, 22, 30, 0, 6, K('rub_d'))                   # 石の残り
    for _ in range(5):
        x = int(rng.integers(3, 26))
        box(v, x, x + 5, 23, 29, 4, 7, pick(rng, [K('rub'), K('rub_l')], [0.6, 0.4]))
    for _ in range(int(rng.integers(6, 10))):                 # 蔓（登る）
        bx = int(rng.integers(3, 28))
        tall = int(rng.integers(12, 30))
        lean = int(rng.integers(-4, 5))
        for z in range(tall):
            t = z / float(tall)
            x = min(V - 2, max(0, bx + int(round(lean * t))))
            y = 26 - int(round(6 * t))
            box(v, x, x + 1, y, y + 2, z + 4, z + 5, K('vine_d'))
            if z % 3 == 0:
                leaf = K('leaf_mv_ivy') if rng.random() < 0.6 else K('leaf_mv_ivyd')
                box(v, x - 1, x + 3, y - 1, y + 2, z + 4, z + 6, leaf)
    return one(v)


def laundry_line_mor(name):
    """通りをまたぐ洗濯物 — 2 本の柱に張った綱。布は振り子で揺れる。"""
    frame = vol(h=44)
    for x in (2, 27):
        box(frame, x, x + 3, 14, 17, 0, 40, K('timber_d'))
        box(frame, x - 1, x + 4, 13, 18, 38, 40, K('timber'))
    box(frame, 2, 30, 15, 16, 38, 39, K('lash'))
    sheet = vol(h=44)
    box(sheet, 5, 15, 15, 16, 20, 38, K('cloth_mv_w'))
    box(sheet, 5, 15, 15, 16, 20, 23, K('cloth_mv_wd'))
    box(sheet, 5, 7, 15, 16, 34, 38, K('cloth_mv_wd'))
    shirt = vol(h=44)
    box(shirt, 18, 27, 15, 16, 26, 38, K('cloth_mv_b'))
    box(shirt, 16, 29, 15, 16, 32, 36, K('cloth_mv_b'))       # 袖
    box(shirt, 18, 27, 15, 16, 26, 29, K('cloth_mv_bd'))
    return [('frame', frame, (0, 0, 0)), ('sheet', sheet, (0, 0, 0)), ('shirt', shirt, (0, 0, 0))]


def laundry_parts():
    return swing_parts([('sheet', (10, 15.5, 38)), ('shirt', (22, 15.5, 38))],
                       amplitude=5.0, period=2.8)


def guild_sign_mor(name):
    """ギルドの張り出し看板 — 鉄の腕木に吊った板。板は振り子で揺れる。

    **店の看板（`sign_*`）とは別物**である。あちらは入口の脇に立つ機能の看板で、
    こちらは前庭の飾り。渦巻き鍛冶の腕木が中世都市の通りの記号になる。
    """
    frame = vol(h=46)
    box(frame, 3, 7, 14, 18, 0, 42, K('ash'))                 # 石の柱
    box(frame, 2, 8, 13, 19, 0, 3, K('ash_d'))
    box(frame, 3, 7, 14, 18, 40, 42, K('ash_l'))
    box(frame, 6, 26, 15, 17, 38, 40, K('iron'))              # 腕木
    for k in range(5):                                        # 渦巻きの支え
        box(frame, 7 + (k * 2), 9 + (k * 2), 15, 17, 34 - k, 36 - k, K('iron_l'))
    box(frame, 24, 26, 15, 17, 34, 40, K('iron_l'))
    box(frame, 23, 27, 15, 17, 36, 38, K('iron'))
    board = vol(h=46)
    box(board, 15, 27, 15, 17, 20, 34, K('oak'))
    box(board, 15, 27, 15, 16, 20, 34, K('oak_l'))
    box(board, 15, 17, 15, 17, 20, 34, K('iron'))             # 縁金
    box(board, 25, 27, 15, 17, 20, 34, K('iron'))
    box(board, 18, 24, 15, 16, 24, 30, K('gold'))             # 紋
    box(board, 20, 22, 15, 16, 22, 32, K('gold'))
    return [('frame', frame, (0, 0, 0)), ('board', board, (0, 0, 0))]


def guild_sign_parts():
    return swing_parts([('board', (21, 16, 37))], amplitude=4.5, period=2.4)


def banner_drape_mor(name):
    """紋章の垂れ幕 — 壁に掛ける長い幕。上の横木から下がり、風で揺れる。"""
    frame = vol(h=58)
    box(frame, 4, 8, 20, 24, 0, 52, K('ash'))                 # 掛ける柱
    box(frame, 3, 9, 19, 25, 0, 3, K('ash_d'))
    box(frame, 4, 26, 21, 23, 50, 52, K('timber_d'))          # 横木
    box(frame, 25, 27, 20, 24, 48, 52, K('gold'))
    drape = vol(h=58)
    box(drape, 6, 26, 21, 22, 14, 50, K('cloth_mv_b'))
    box(drape, 6, 26, 21, 22, 14, 18, K('cloth_mv_bd'))
    for x in range(7, 25, 5):                                 # 裾の切れ込み
        box(drape, x, x + 3, 21, 22, 14, 19, 0)
    box(drape, 10, 22, 21, 22, 26, 42, K('gold'))             # 紋（盾形）
    box(drape, 12, 20, 21, 22, 22, 28, K('gold'))
    box(drape, 13, 19, 21, 22, 30, 38, K('cloth_mv_r'))
    return [('frame', frame, (0, 0, 0)), ('drape', drape, (0, 0, 0))]


def banner_parts():
    return swing_parts([('drape', (16, 21.5, 50))], amplitude=3.5, period=3.6)


def flag_pole_mor(name):
    """旗竿 — 石の台に高い竿、市の旗（振り子で揺れる）。"""
    frame = vol(h=70)
    disc(frame, 16, 16, 7.0, 0, 4, K('ash'))
    disc(frame, 16, 16, 6.0, 3, 5, K('ash_l'))
    box(frame, 15, 18, 15, 18, 4, 66, K('timber'))
    box(frame, 15, 18, 15, 18, 64, 66, K('gold'))
    ball(frame, 16, 16, 68, 2.4, K('gold'))
    flag = vol(h=70)
    box(flag, 4, 16, 16, 17, 40, 62, K('cloth_mv_r'))
    box(flag, 4, 16, 16, 17, 40, 44, K('cloth_mv_rd'))
    box(flag, 7, 13, 16, 17, 46, 58, K('gold'))               # 紋
    box(flag, 9, 11, 16, 17, 44, 60, K('gold'))
    return [('frame', frame, (0, 0, 0)), ('flag', flag, (0, 0, 0))]


def flag_parts():
    return swing_parts([('flag', (10, 16.5, 62))], amplitude=4.0, period=3.2)


# ------------------------------------------------------------------ 店ごとの小物
def forge_mor(name):
    """鍛冶の炉 — 切石の炉に煉瓦の火床、上に煙出しの覆いと石の煙突。火は `forge_fire_mor`。"""
    rng = rng_for(name)
    v = vol(h=48)
    box(v, 2, 26, 6, 26, 0, 14, K('ash'))                     # 炉の台
    for z in range(1, 13, 3):
        p = int(rng.integers(0, 3))
        while p < 26:
            w = int(rng.integers(4, 8))
            box(v, p, min(26, p + w), 6, 26, z, z + 2,
                pick(rng, [K('ash'), K('ash_d'), K('rub')], [0.44, 0.3, 0.26]))
            p += w + 1
    box(v, 2, 26, 6, 26, 14, 16, K('ash_l'))
    box(v, 6, 22, 12, 24, 12, 16, K('char'))                  # 火床（南に開く）
    box(v, 7, 21, 13, 23, 13, 16, K('emb'))
    #! 煙突の胸壁（北側の壁）。**南は開けたまま**——鍛冶は手前から火を焚く。
    for z in range(16, 26):
        d = (z - 16)
        box(v, 3 + d, 25 - d, 6, 8 + d, z, z + 1, K('iron'))
        box(v, 3 + d, 5 + d, 6, 18, z, z + 1, K('iron'))
        box(v, 23 - d, 25 - d, 6, 18, z, z + 1, K('iron'))
    box(v, 11, 19, 6, 14, 26, 44, K('rub'))                   # 煙突
    box(v, 10, 20, 5, 15, 44, 46, K('rub_l'))
    box(v, 12, 18, 7, 13, 44, 48, K('soot'))
    box(v, 24, 30, 20, 26, 0, 10, K('timber_d'))              # 鞴（ふいご）の枠
    box(v, 25, 30, 21, 26, 10, 13, K('cloth_d'))
    return one(v)


def forge_fire_mor(name):
    """炉の火（`forge_mor` の相方。**昼も光る**）。"""
    rng = rng_for(name)
    v = vol(h=30)
    box(v, 7, 21, 13, 23, 14, 16, K('emb_l'))
    for _ in range(int(rng.integers(5, 9))):
        x, y = int(rng.integers(8, 19)), int(rng.integers(14, 21))
        h = int(rng.integers(3, 8))
        box(v, x, x + 2, y, y + 2, 16, 16 + h, K('fl_mid'))
        box(v, x, x + 1, y, y + 1, 16, 16 + h + 2, K('fl_core'))
    return one(v)


def anvil_mor(name):
    """金床 — 樫の切り株の上に鉄の金床。角（ホーン）が西へ出る。槌と鋏と水桶。

    **切り株を高く細く、金床を小さく**（同じ太さだと上から見て 1 つの塊になる。§7 の記録）。
    """
    v = vol(h=32)
    for z in range(16):                                       # 切り株（丸太）
        r = 6.4 - (z * 0.05)
        disc(v, 17, 18, r, z, z + 1, K('oak') if (z % 5) else K('oak_l'))
    disc(v, 17, 18, 6.4, 0, 2, K('bark'))
    disc(v, 17, 18, 5.8, 15, 16, K('bark'))
    box(v, 13, 22, 15, 22, 16, 19, K('iron'))                 # 台（下ほど広い）
    box(v, 15, 20, 16, 21, 19, 23, K('iron_l'))               # 胴（くびれ）
    box(v, 11, 25, 15, 22, 23, 26, K('iron_l'))               # 面（上に広い）
    box(v, 11, 25, 15, 22, 25, 26, K('steel_mv_l'))           # 打ち面（磨いてある）
    for k in range(5):                                        # 角（西へ細る）
        box(v, 7 + k, 8 + k, 17 - (k // 3), 20 + (k // 3), 23 + (k // 4), 26, K('iron_l'))
    box(v, 25, 28, 17, 20, 23, 26, K('iron'))                 # 尾（東）
    box(v, 25, 27, 18, 19, 24, 25, 0)                         # 角穴
    box(v, 2, 4, 22, 25, 0, 14, K('timber'))                  # 立て掛けた槌
    box(v, 1, 6, 21, 26, 14, 18, K('steel'))
    box(v, 20, 30, 25, 27, 0, 2, K('iron'))                   # 床の鋏
    box(v, 28, 31, 24, 28, 0, 2, K('iron_l'))
    for z in range(9):                                        # 焼き入れの桶
        disc(v, 27, 8, 5.0, z, z + 1, K('timber_d') if (z % 4) else K('timber'))
    disc(v, 27, 8, 3.6, 6, 9, K('wa_d'))
    disc(v, 27, 8, 3.6, 8, 9, K('wa'))
    return one(v)


def armour_display_mor(name):
    """鎧の陳列 — 木の立て台に板金鎧一式。草摺・肩当て・兜、脇に盾。

    **胴を広く取る**（細いと totem に見える。§7 の記録）。
    """
    v = vol(h=48)
    box(v, 6, 26, 10, 22, 0, 3, K('timber_d'))                # 台
    box(v, 14, 19, 14, 18, 3, 20, K('timber'))                # 支柱
    box(v, 8, 24, 11, 21, 18, 26, K('steel_mv_l'))            # 草摺（裾）
    for x in range(9, 24, 4):                                 # 草摺の板の境
        box(v, x, x + 1, 11, 21, 18, 26, K('steel_d'))
    box(v, 9, 23, 11, 21, 26, 38, K('steel_mv_l'))            # 胴鎧
    box(v, 9, 23, 11, 13, 26, 38, K('steel'))                 # 北面は暗く
    box(v, 15, 17, 19, 21, 28, 38, K('steel_d'))              # 胸の稜
    for z in range(27, 36, 3):                                # 帯の段
        box(v, 9, 23, 20, 21, z, z + 1, K('steel_d'))
    box(v, 5, 10, 12, 20, 32, 38, K('steel_mv_l'))            # 肩当て
    box(v, 22, 27, 12, 20, 32, 38, K('steel_mv_l'))
    box(v, 5, 10, 12, 20, 36, 38, K('steel'))
    box(v, 22, 27, 12, 20, 36, 38, K('steel'))
    box(v, 5, 8, 13, 19, 24, 33, K('steel'))                  # 腕（籠手）
    box(v, 24, 27, 13, 19, 24, 33, K('steel'))
    box(v, 12, 20, 13, 19, 38, 40, K('steel_d'))              # 首（喉輪）
    box(v, 11, 21, 12, 20, 40, 46, K('steel_mv_l'))           # 兜
    box(v, 11, 21, 19, 20, 42, 44, K('char'))                 # 面の隙間
    box(v, 15, 17, 19, 21, 40, 46, K('steel'))                # 面の稜
    box(v, 13, 19, 12, 20, 46, 48, K('cloth_mv_r'))           # 前立て
    box(v, 27, 30, 20, 29, 0, 20, K('oak'))                   # 立て掛けた盾
    box(v, 27, 30, 21, 28, 2, 18, K('cloth_mv_b'))
    box(v, 27, 30, 23, 26, 5, 15, K('gold'))
    return one(v)


def weapon_rack_mor(name):
    """武器立て — 斜めの枠に剣と斧と槍を並べる。"""
    rng = rng_for(name)
    v = vol(h=40)
    box(v, 2, 30, 18, 24, 0, 3, K('timber_d'))                # 台
    for x in (3, 28):
        box(v, x, x + 2, 19, 23, 0, 24, K('timber'))
    box(v, 2, 30, 19, 23, 22, 25, K('timber'))
    for x in range(4, 28, 5):                                 # 穴
        box(v, x, x + 3, 19, 23, 22, 25, 0)
    for i, x in enumerate(range(4, 28, 5)):
        kind = int(rng.integers(0, 3))
        if kind == 0:                                         # 剣
            box(v, x, x + 3, 20, 22, 3, 32, K('steel_mv_l'))
            box(v, x - 1, x + 4, 20, 22, 30, 32, K('iron'))
            box(v, x, x + 3, 20, 22, 32, 36, K('oak'))
        elif kind == 1:                                       # 斧
            box(v, x, x + 3, 20, 22, 3, 34, K('timber_d'))
            box(v, x - 2, x + 5, 20, 22, 30, 36, K('steel'))
            box(v, x + 1, x + 5, 20, 22, 32, 34, 0)
        else:                                                 # 槍
            box(v, x, x + 2, 20, 22, 3, 36, K('timber'))
            box(v, x, x + 2, 20, 22, 36, 40, K('steel_mv_l'))
    return one(v)


def spear_rack_mor(name):
    """槍立て — 束ねた槍と、立て掛けた矛。城の詰め所の脇に置く。"""
    v = vol(h=42)
    disc(v, 16, 16, 7.0, 0, 4, K('timber_d'))                 # 桶型の台
    disc(v, 16, 16, 7.4, 0, 2, K('iron'), hollow=6.0)
    disc(v, 16, 16, 7.4, 3, 5, K('iron'), hollow=6.0)
    for (x, y, tall) in ((12, 13, 34), (16, 12, 38), (20, 15, 33), (13, 19, 36),
                         (18, 20, 35), (22, 18, 32)):
        box(v, x, x + 2, y, y + 2, 2, tall, K('timber'))
        box(v, x, x + 2, y, y + 2, tall, tall + 4, K('steel_mv_l'))
        box(v, x - 1, x + 3, y, y + 2, tall, tall + 2, K('steel'))
    box(v, 24, 26, 22, 24, 0, 36, K('timber_d'))              # 立て掛けた矛
    box(v, 23, 28, 22, 24, 36, 40, K('steel'))
    box(v, 24, 26, 22, 24, 40, 42, K('steel_mv_l'))
    return one(v)


def butts_mor(name):
    """弓の的 — 藁を巻いた円い的（南を向く）を三脚に載せ、矢が刺さっている。

    **的の面は x-z 平面**（南のカメラへ正対する）なので、輪は y 軸に沿う筒で作る。
    """
    rng = rng_for(name)
    v = vol(h=42)
    for (x, y) in ((4, 20), (26, 20), (15, 6)):               # 三脚
        box(v, x, x + 2, y, y + 2, 0, 20, K('timber_d'))
    box(v, 3, 29, 12, 20, 18, 21, K('timber'))                # 受けの桁
    log_y(v, 16, 30, 9.4, 12, 20, K('hay_d'))                 # 藁の的（芯）
    for k in range(4):                                        # 藁の巻き（同心の輪）
        log_y(v, 16, 30, 9.4 - (k * 2.2), 12, 20, K('hay') if (k % 2) else K('hay_d'))
    log_y(v, 16, 30, 6.2, 19, 20, K('cloth_mv_w'))            # 的の白い輪（南面）
    log_y(v, 16, 30, 3.0, 19, 20, K('cloth_mv_r'))            # 的の芯（南面の赤）
    for _ in range(int(rng.integers(3, 6))):                  # 刺さった矢
        a = rng.random() * 6.28
        r = 2.0 + (rng.random() * 6.0)
        x = int(16 + r * np.cos(a))
        z = int(30 + r * np.sin(a))
        box(v, x, x + 1, 18, 27, z, z + 1, K('timber'))
        box(v, x, x + 1, 25, 27, z, z + 2, K('cloth_mv_w'))
    return one(v)


def pell_mor(name):
    """打ち込み杭 — 剣の稽古で叩く太い杭。傷だらけで、盾が縛りつけてある。"""
    rng = rng_for(name)
    v = vol(h=52)
    box(v, 4, 28, 8, 24, 0, 3, K('pack_d'))                   # 踏み固めた地面
    for z in range(3, 46):                                    # 杭
        disc(v, 16, 16, 5.4, z, z + 1, K('oak') if (z % 7) else K('oak_l'))
    for _ in range(int(rng.integers(8, 14))):                 # 打ち傷（塊で彫る）
        z = int(rng.integers(16, 44))
        a = rng.random() * 6.28
        x, y = int(16 + 4.6 * np.cos(a)), int(16 + 4.6 * np.sin(a))
        box(v, x, x + 3, y, y + 3, z, z + 2, K('bark'))
    disc(v, 16, 16, 5.8, 44, 46, K('iron'))                   # 頭の鉄輪
    box(v, 10, 22, 20, 23, 20, 34, K('oak_l'))                # 縛った盾
    box(v, 11, 21, 21, 23, 22, 32, K('cloth_mv_b'))
    box(v, 14, 18, 21, 23, 24, 30, K('gold'))
    box(v, 10, 22, 20, 23, 26, 28, K('lash'))
    return one(v)


def herb_rack_mor(name):
    """薬草の干し台 — 木の枠に薬草の束を吊るす。下に籠。"""
    rng = rng_for(name)
    v = vol(h=40)
    for x in (3, 27):
        box(v, x, x + 3, 14, 18, 0, 36, K('timber_d'))
        box(v, x - 1, x + 4, 13, 19, 0, 3, K('timber'))
    box(v, 2, 30, 15, 17, 32, 35, K('timber'))
    box(v, 2, 30, 15, 17, 22, 24, K('timber'))
    for x in range(5, 27, 4):                                 # 吊るした束
        tall = int(rng.integers(8, 14))
        leaf = pick(rng, [K('grass_mv_herb'), K('leaf_m'), K('wilt')], [0.4, 0.34, 0.26])
        box(v, x, x + 3, 15, 18, 32 - tall, 32, leaf)
        box(v, x, x + 3, 15, 18, 30, 32, K('lash'))
    box(v, 6, 16, 18, 27, 0, 8, K('timber_l'))                # 籠
    box(v, 7, 15, 19, 26, 6, 8, K('grass_mv_herb'))
    return one(v)


def covered_cart_mor(name):
    """幌の荷車 — 骨に布を張った半円の幌。闇市の売り台。

    幌は**半円の筒**で作る（z ごとに帯を並べる作りは、上から見ると楔形の塊に見えた。
    §7 の記録）。`log_x` で筒を作り、内側を抜き、下半分を落とす。
    """
    v = vol(h=34)
    box(v, 3, 29, 8, 24, 8, 11, K('oak'))                     # 荷台
    box(v, 3, 29, 8, 9, 11, 14, K('oak_l'))
    box(v, 3, 29, 23, 24, 11, 14, K('oak_l'))
    for cy in (6, 26):                                        # 車輪
        _wheel(v, 8, 6, 6.0, cy - 1, cy + 1)
        _wheel(v, 23, 6, 6.0, cy - 1, cy + 1)
    log_x(v, 16, 14, 9.4, 3, 29, K('cloth_mv_y'))             # 幌（筒）
    log_x(v, 16, 14, 8.0, 3, 29, 0)                           # 内を抜く
    box(v, 0, V, 0, V, 0, 14, 0)                              # 下半分を落とす
    for x in (4, 12, 20, 27):                                 # 骨（外へ 1 段出す）
        log_x(v, 16, 14, 9.8, x, x + 2, K('timber_d'))
        log_x(v, 16, 14, 8.0, x, x + 2, 0)
    box(v, 0, V, 0, V, 0, 14, 0)
    box(v, 3, 29, 8, 24, 8, 11, K('oak'))                     # 荷台を戻す
    box(v, 3, 29, 8, 9, 11, 14, K('oak_l'))
    box(v, 3, 29, 23, 24, 11, 14, K('oak_l'))
    for cy in (6, 26):
        _wheel(v, 8, 6, 6.0, cy - 1, cy + 1)
        _wheel(v, 23, 6, 6.0, cy - 1, cy + 1)
    box(v, 3, 5, 7, 25, 14, 24, K('cloth_d'))                 # 西の妻を閉じる
    box(v, 5, 27, 22, 25, 11, 20, K('cloth_d'))               # 垂らした前布
    box(v, 8, 16, 10, 20, 11, 16, K('timber'))                # 中の木箱
    box(v, 18, 26, 12, 20, 11, 15, K('oak_l'))
    return one(v)


def brazier_mor(name):
    """篝火の鉄鉢 — 三脚の上に鉄の鉢と炭。火は `brazier_fire_mor`。"""
    v = vol(h=34)
    for k in range(3):                                        # 三脚
        a = k * 2.09
        x, y = int(16 + 9 * np.cos(a)), int(16 + 9 * np.sin(a))
        for z in range(20):
            t = z / 20.0
            px = int(round(16 + (9 - 6 * t) * np.cos(a)))
            py = int(round(16 + (9 - 6 * t) * np.sin(a)))
            box(v, px, px + 2, py, py + 2, z, z + 1, K('iron'))
        box(v, x, x + 3, y, y + 3, 0, 2, K('iron_l'))
    disc(v, 16, 16, 9.0, 20, 26, K('iron'), hollow=7.2)       # 鉢
    disc(v, 16, 16, 9.0, 20, 21, K('iron_l'))
    disc(v, 16, 16, 9.6, 25, 27, K('iron_l'), hollow=8.2)
    disc(v, 16, 16, 7.2, 21, 24, K('char'))                   # 炭
    return one(v)


def brazier_fire_mor(name):
    """篝火の火（`brazier_mor` の相方）。"""
    rng = rng_for(name)
    v = vol(h=40)
    disc(v, 16, 16, 7.0, 23, 26, K('emb_l'))
    for _ in range(int(rng.integers(5, 9))):
        a = rng.random() * 6.28
        r = rng.random() * 4.5
        x, y = int(16 + r * np.cos(a)), int(16 + r * np.sin(a))
        h = int(rng.integers(5, 12))
        box(v, x, x + 2, y, y + 2, 25, 25 + h, K('fl_mid'))
        box(v, x, x + 1, y, y + 1, 25, 25 + h + 3, K('fl_core'))
    return one(v)


def ale_barrels_mor(name):
    """麦酒の樽 — 横たえた樽 2 つを架台に載せ、栓と受け皿。酒場の外。"""
    v = vol(h=28)
    for (y, cz) in ((9, 9), (22, 9)):                         # 架台
        box(v, 3, 8, y - 3, y + 3, 0, 4, K('timber_d'))
        box(v, 24, 29, y - 3, y + 3, 0, 4, K('timber_d'))
    for y in (9, 22):
        log_x(v, y, 10, 5.6, 4, 28, K('timber'))
        log_x(v, y, 10, 6.0, 7, 10, K('iron'))
        log_x(v, y, 10, 6.0, 22, 25, K('iron'))
        box(v, 27, 30, y - 1, y + 2, 8, 11, K('brass'))       # 栓
        box(v, 29, 31, y - 1, y + 2, 6, 9, K('brass_d'))
    box(v, 28, 32, 6, 26, 0, 3, K('timber_l'))                # 受けの板
    for x in (12, 18):                                        # 木のジョッキ
        disc(v, x, 16, 2.4, 16, 22, K('oak'))
        disc(v, x, 16, 1.6, 20, 22, K('cream'))
    return one(v)


def tavern_table_mor(name):
    """外の卓と長椅子 — 厚板の卓に長椅子 2 脚。皿とジョッキ。"""
    v = vol(h=22)
    box(v, 4, 28, 10, 22, 12, 15, K('board'))                 # 卓
    box(v, 4, 28, 10, 22, 11, 12, K('board_d'))
    for x in (6, 24):
        box(v, x, x + 3, 11, 21, 0, 12, K('timber_d'))
    for y in (4, 26):                                         # 長椅子
        box(v, 3, 29, y, y + 4, 7, 9, K('timber'))
        for x in (5, 25):
            box(v, x, x + 3, y, y + 4, 0, 7, K('timber_d'))
    for (x, y) in ((10, 13), (20, 17)):                       # ジョッキ
        disc(v, x, y, 2.4, 15, 20, K('oak'))
        disc(v, x, y, 1.6, 18, 20, K('cream'))
    disc(v, 15, 18, 3.4, 15, 16, K('plas_l'))                 # 皿
    box(v, 14, 17, 17, 20, 16, 17, K('fl_y'))
    return one(v)


def relic_plinth_mor(name):
    """台座の遺物 — 石の台に古い壺を載せ、鎖の柵で囲う。博物館の前。"""
    v = vol(h=36)
    box(v, 8, 24, 8, 24, 0, 4, K('ash_d'))                    # 台座
    box(v, 10, 22, 10, 22, 4, 18, K('ash'))
    box(v, 9, 23, 9, 23, 17, 19, K('ash_l'))
    for z in range(19, 32):                                   # 壺
        t = (z - 19) / 13.0
        r = 3.0 + (5.0 * np.sin(t * 3.14))
        disc(v, 16, 16, r, z, z + 1, K('clay') if (z % 4) else K('clay_d'))
    disc(v, 16, 16, 3.4, 31, 33, K('clay_d'))                 # 口
    disc(v, 16, 16, 4.2, 33, 34, K('clay'))
    box(v, 12, 20, 8, 10, 24, 28, K('gold'))                  # 描かれた帯
    for (x, y) in ((2, 2), (28, 2), (2, 28), (28, 28)):       # 鎖の柵
        box(v, x, x + 2, y, y + 2, 0, 14, K('iron'))
        box(v, x, x + 2, y, y + 2, 14, 16, K('brass'))
    for k in range(0, V, 4):                                  # 垂れた鎖
        box(v, k, k + 2, 2, 4, 10 + (k % 3), 12 + (k % 3), K('iron_l'))
        box(v, k, k + 2, 28, 30, 10 + (k % 3), 12 + (k % 3), K('iron_l'))
    return one(v)


def orb_stand_mor(name):
    """天球儀 — 三脚の石台に真鍮の輪を組んだ球。中に光る玉。光は `orb_glow_mor`。"""
    v = vol(h=44)
    disc(v, 16, 16, 8.0, 0, 3, K('ash'))                      # 台
    disc(v, 16, 16, 6.4, 2, 4, K('ash_l'))
    box(v, 14, 19, 14, 19, 3, 16, K('ash'))                   # 柱
    disc(v, 16, 16, 5.0, 15, 17, K('brass_d'))
    disc(v, 16, 16, 11.0, 26, 28, K('brass'), hollow=9.4)     # 赤道の輪
    for z in range(17, 38):                                   # 子午線の輪（縦）
        dz = (z - 27) / 11.0
        if abs(dz) > 1.0:
            continue
        r = 11.0 * np.sqrt(max(0.0, 1.0 - (dz * dz)))
        box(v, int(16 - r), int(16 - r) + 2, 15, 18, z, z + 1, K('brass_mv_l'))
        box(v, int(16 + r) - 2, int(16 + r), 15, 18, z, z + 1, K('brass_mv_l'))
        box(v, 15, 18, int(16 - r), int(16 - r) + 2, z, z + 1, K('brass'))
        box(v, 15, 18, int(16 + r) - 2, int(16 + r), z, z + 1, K('brass'))
    box(v, 15, 18, 15, 18, 38, 42, K('brass_d'))              # 軸
    box(v, 15, 18, 15, 18, 14, 18, K('brass_d'))
    ball(v, 16, 16, 27, 3.4, K('glass'))                      # 中の玉
    return one(v)


def orb_glow_mor(name):
    """天球儀の光（`orb_stand_mor` の相方。**昼も光る**）。"""
    v = vol(h=40)
    ball(v, 16, 16, 27, 4.2, K('glass_l'))
    ball(v, 16, 16, 27, 2.4, K('lit'))
    return one(v)


def wayside_shrine_mor(name):
    """壁龕の祠 — 切石の柱に彫り込んだ龕。中に小さな像と蝋燭。火は `shrine_flame_mor`。"""
    rng = rng_for(name)
    v = vol(h=52)
    box(v, 7, 25, 10, 22, 0, 5, K('ash_d'))                   # 基壇
    box(v, 8, 24, 11, 21, 4, 5, K('ash'))
    box(v, 9, 23, 12, 20, 5, 40, K('ash'))                    # 柱身
    for z in range(6, 40, 7):                                 # 石の段
        box(v, 9, 23, 12, 20, z, z + 1, K('joint'))
        box(v, 9, 23, 12, 20, z + 1, z + 6,
            pick(rng, [K('ash'), K('ash_l')], [0.6, 0.4]))
    box(v, 11, 21, 17, 20, 16, 32, 0)                         # 龕（南面に彫る）
    box(v, 11, 21, 17, 18, 16, 32, K('ash_d'))
    for k in range(5):                                        # 龕の上のアーチ
        box(v, 11 + k, 21 - k, 17, 20, 32 + k, 33 + k, 0)
        box(v, 11 + k, 21 - k, 17, 18, 32 + k, 33 + k, K('ash_d'))
    box(v, 14, 18, 17, 19, 17, 20, K('marb'))                 # 小さな像
    box(v, 14, 18, 17, 19, 20, 27, K('marb_l'))
    ball(v, 16, 18, 28, 2.0, K('marb_l'))
    for x in (12, 19):                                        # 蝋燭
        box(v, x, x + 2, 18, 20, 17, 23, K('cream'))
    box(v, 8, 24, 11, 21, 40, 42, K('ash_l'))                 # 笠石
    for z in range(42, 50):
        d = (z - 42)
        box(v, 8 + d, 24 - d, 11 + d, 21 - d, z, z + 1, K('tile_d') if (z % 3 == 0) else K('tile'))
    return one(v)


def shrine_flame_mor(name):
    """祠の蝋燭の火（`wayside_shrine_mor` の相方）。"""
    v = vol(h=32)
    for x in (12, 19):
        box(v, x, x + 2, 18, 20, 23, 26, K('fl_mid'))
        box(v, x, x + 1, 18, 19, 23, 28, K('fl_core'))
    return one(v)


def guard_post_mor(name):
    """衛兵の詰め所 — 三方を板で囲った番小屋。瓦の片流れ、鉤に掛けた矛と角灯。"""
    v = vol(h=54)
    box(v, 2, 30, 4, 28, 0, 3, K('ash_d'))                    # 敷石
    box(v, 3, 29, 5, 7, 3, 40, K('board'))                    # 北の板壁
    box(v, 3, 5, 5, 27, 3, 40, K('board'))                    # 西
    box(v, 27, 29, 5, 27, 3, 40, K('board'))                  # 東
    for z in range(6, 38, 6):                                 # 板の継ぎ目
        box(v, 3, 29, 5, 7, z, z + 1, K('board_d'))
        box(v, 3, 5, 5, 27, z, z + 1, K('board_d'))
        box(v, 27, 29, 5, 27, z, z + 1, K('board_d'))
    for x in (3, 27):                                         # 南の柱
        box(v, x, x + 2, 25, 27, 3, 42, K('timber_d'))
    box(v, 2, 30, 25, 27, 40, 42, K('timber_d'))              # 桁
    for z in range(40, 50):                                   # 瓦の片流れ（北が高い）
        d = (z - 40) * 2
        box(v, 1, 31, 27 - d, 30 - d, z, z + 1, K('tile_d') if (z % 3 == 0) else K('tile'))
    box(v, 22, 24, 6, 8, 6, 40, K('timber'))                  # 立て掛けた矛
    box(v, 21, 26, 6, 8, 38, 42, K('steel'))
    box(v, 6, 12, 6, 12, 30, 36, K('iron'))                   # 角灯
    box(v, 7, 11, 7, 11, 31, 35, K('fl_y'))
    box(v, 8, 10, 8, 10, 36, 40, K('iron_l'))
    box(v, 8, 26, 8, 24, 8, 12, K('timber'))                  # 中の腰掛け
    return one(v)


# ------------------------------------------------------------------ 緑と墓
def orchard_tree_mor(name):
    """果樹 — 林檎（01）と梨（02）。低く枝を張り、実が生る。"""
    rng = rng_for(name)
    kind = int(name[-2:]) if name[-2:].isdigit() else 1
    fruit = K('fl_r') if kind == 1 else K('fl_y')
    v = vol(h=62)
    for z in range(0, 26):                                    # 幹（曲がる）
        r = 3.4 - (z * 0.05)
        cx = 16 + int(round(1.6 * np.sin(z * 0.16)))
        disc(v, cx, 16, r, z, z + 1, K('bark') if (z % 5) else K('bark_l'))
    for k in range(5):                                        # 主枝
        a = (k * 1.256) + (rng.random() * 0.3)
        for t in range(10):
            x = int(round(16 + (t * 1.3) * np.cos(a)))
            y = int(round(16 + (t * 1.3) * np.sin(a)))
            box(v, x, x + 2, y, y + 2, 24 + t, 26 + t, K('bark'))
    for _ in range(int(rng.integers(9, 14))):                 # 樹冠（塊で）
        a = rng.random() * 6.28
        r = 4.0 + (rng.random() * 7.0)
        cx = int(16 + r * np.cos(a))
        cy = int(16 + r * np.sin(a))
        cz = int(rng.integers(36, 52))
        leaf = pick(rng, [K('leaf_m'), K('leaf_l'), K('leaf_d')], [0.42, 0.3, 0.28])
        ball(v, cx, cy, cz, rng.integers(4, 8), leaf, squash=0.8)
    for _ in range(int(rng.integers(6, 11))):                 # 実
        a = rng.random() * 6.28
        r = 5.0 + (rng.random() * 7.0)
        cx = int(16 + r * np.cos(a))
        cy = int(16 + r * np.sin(a))
        cz = int(rng.integers(34, 48))
        ball(v, cx, cy, cz, 2, fruit)
    for _ in range(int(rng.integers(1, 4))):                  # 根元に落ちた実
        fx, fy = int(rng.integers(4, 27)), int(rng.integers(4, 27))
        ball(v, fx, fy, 1, 2, fruit)
    return one(v)


def herb_bed_mor(name):
    """薬草の畝 — 板で囲った上げ床に薬草を並べ、名札の杭を立てる。"""
    rng = rng_for(name)
    v = vol(h=20)
    box(v, 2, 30, 4, 28, 0, 6, K('board_d'))                  # 板の枠
    box(v, 4, 28, 6, 26, 0, 7, K('dirt_mv_bed'))
    box(v, 4, 28, 6, 26, 6, 7, K('soil'))
    box(v, 2, 30, 4, 28, 6, 7, K('board'))
    for x in range(5, 27, 5):                                 # 薬草の株
        for y in range(7, 25, 6):
            leaf = pick(rng, [K('grass_mv_herb'), K('grass_mv_veg'), K('leaf_m')],
                        [0.4, 0.34, 0.26])
            ball(v, x + 1, y + 2, 8, 2.6, leaf, squash=0.7)
            box(v, x + 1, x + 3, y + 2, y + 4, 7, 12, leaf)
    box(v, 26, 28, 24, 26, 6, 16, K('timber'))                # 名札の杭
    box(v, 24, 30, 24, 25, 13, 16, K('plas_l'))
    return one(v)


def grave_cross_mor(name):
    """墓標 — 石の十字架（01）と、傾いた石板（02）。根元に草と花。"""
    rng = rng_for(name)
    kind = int(name[-2:]) if name[-2:].isdigit() else 1
    v = vol(h=32)
    box(v, 8, 24, 10, 22, 0, 3, K('rub_d'))                   # 土盛りの縁
    box(v, 9, 23, 11, 21, 2, 4, K('turf_d'))
    if kind == 1:
        box(v, 12, 20, 14, 18, 3, 8, K('ash'))                # 礎
        box(v, 14, 18, 15, 17, 8, 26, K('ash_l'))             # 縦木
        box(v, 9, 23, 15, 17, 18, 22, K('ash_l'))             # 横木
        box(v, 14, 18, 15, 17, 24, 26, K('ash'))
        box(v, 9, 11, 15, 17, 18, 22, K('moss_d'))            # 苔
    else:
        for z in range(3, 22):                                # 傾いた石板
            d = (z - 3) // 6
            box(v, 10 + d, 22 + d, 14, 17, z, z + 1, K('ash') if (z % 5) else K('ash_l'))
        box(v, 13, 20, 13, 14, 8, 18, K('joint'))             # 彫った銘
        box(v, 10, 14, 14, 17, 3, 8, K('moss_d'))
    for _ in range(int(rng.integers(2, 5))):                  # 草と花
        x, y = int(rng.integers(4, 26)), int(rng.integers(6, 26))
        box(v, x, x + 3, y, y + 2, 0, 4, K('turf'))
        if rng.random() < 0.4:
            box(v, x + 1, x + 2, y, y + 1, 4, 6, K('fl_w'))
    return one(v)


def tomb_slab_mor(name):
    """石棺の蓋 — 地に伏せた大きな石板。彫った銘と縁、隅に苔。"""
    rng = rng_for(name)
    v = vol(h=14)
    box(v, 1, 31, 3, 29, 0, 5, K('ash_d'))                    # 台
    box(v, 2, 30, 4, 28, 5, 9, K('ash'))                      # 蓋
    for z in range(5, 9):
        box(v, 2, 30, 4, 28, z, z + 1,
            pick(rng, [K('ash'), K('ash_l')], [0.6, 0.4]))
    box(v, 3, 29, 5, 27, 8, 9, K('ash_l'))
    box(v, 4, 28, 6, 8, 8, 9, K('joint'))                     # 彫った縁
    box(v, 4, 28, 24, 26, 8, 9, K('joint'))
    box(v, 12, 20, 9, 23, 8, 9, K('joint'))                   # 彫った十字の銘
    box(v, 8, 24, 14, 18, 8, 9, K('joint'))
    for _ in range(int(rng.integers(2, 5))):
        x, y = int(rng.integers(2, 26)), int(rng.integers(4, 24))
        box(v, x, x + 5, y, y + 4, 8, 9, K('moss_d'))
    return one(v)


# ------------------------------------------------------------------ 屋根
def chimney_mor(name):
    """石の煙突と鳩 — 屋根の棟の真ん中に載せる（`roof_vent`）。

    笠石の上に鳩が 2 羽。**「屋根の上の煙突と鳩」を 1 個で済ませる**。
    煙は置く側が蒸気の口へ積む。
    """
    rng = rng_for(name)
    v = vol(h=26)
    box(v, 10, 22, 10, 22, 0, 18, K('ash'))
    for z in range(0, 18, 4):                                 # 切石の段（彫った目地）
        box(v, 10, 22, 10, 22, z, z + 1, K('joint'))
        box(v, 10, 22, 10, 22, z + 1, z + 4,
            pick(rng, [K('ash'), K('ash_l'), K('ash_d')], [0.46, 0.3, 0.24]))
    box(v, 9, 23, 9, 23, 18, 20, K('ash_l'))                  # 笠石
    box(v, 12, 20, 12, 20, 18, 22, K('ash_d'))                # 煙道
    box(v, 13, 19, 13, 19, 19, 22, K('soot'))
    for (cx, cy) in ((8, 12), (23, 18)):                      # 鳩（笠石の縁に）
        ball(v, cx, cy, 22, 2.6, K('fur_mv_dove'), squash=0.8)
        box(v, cx - 2, cx + 2, cy - 2, cy + 2, 22, 23, K('fur_mv_dovel'))
        box(v, cx - 1, cx + 1, cy + 1, cy + 3, 23, 26, K('fur_mv_dove'))
        box(v, cx - 1, cx + 1, cy + 3, cy + 4, 24, 25, K('fl_y'))
        box(v, cx - 1, cx + 1, cy - 1, cy + 1, 19, 20, K('fl_r'))
    return one(v)


# ------------------------------------------------------------------ 登録
def register(g):
    """`gen_prefabs.py` の名前空間 `g` へモリバントの素材を足す。

    **`TOWN_STYLES` には触らない**——`'mor'` は既に入っている（既存の壁・屋根・地面が
    そこから意匠を選んでいるので、足すと二重になる）。
    """
    global G, C, V
    G = g
    C = g['C']
    V = g['V']
    _palette()
    cat = g['CATALOG']
    static = g['static_part']
    wind = static(motion={'kind': 'wind'}, wind_k=0.5)

    # ---- 地面 ----
    for i in range(1, 4):
        cat['ground_market_mor_%02d' % i] = (ground_market_mor, static(), '市の広場の敷石')
    for i in range(1, 3):
        cat['ground_muck_mor_%02d' % i] = (ground_muck_mor, static(), '路地のぬかるみ')
        cat['ground_garden_mor_%02d' % i] = (ground_garden_mor, static(), '菜園の畝')
        cat['ground_grave_mor_%02d' % i] = (ground_grave_mor, static(), '墓地の芝と踏み土')
    cat['ground_gutter_ns_mor'] = (ground_gutter_ns_mor, static(), '排水溝の走る石畳（南北）')
    cat['ground_gutter_ew_mor'] = (ground_gutter_ew_mor, static(), '排水溝の走る石畳（東西）')
    cat['ground_bridge_mor'] = (ground_bridge_mor, static(), '石橋の甲板')

    # ---- 区画の柵 ----
    #! **名前は `<茎>_n/_s/_w/_e`。**区画の柵は置く側が `rim` の名前へ `_n` を足して引く
    #! （`terrain_view.cpp` の `kRimSuffix`）ので、`railing_n_mor` と書くと 1 枚も出ない。§7 の記録。
    for side in ('n', 's', 'w', 'e'):
        cat['railing_mor_%s' % side] = (railing_mor, static(), '錬鉄の低い柵（%s 側）' % side)
        cat['garden_wall_mor_%s' % side] = (garden_wall_mor, static(), '菜園の低い石垣（%s 側）' % side)

    # ---- 都市の骨 ----
    cat['gate_tower_mor'] = (gate_tower_mor, static(), '市門の櫓（南北にくぐる）')
    cat['gate_tower_ew_mor'] = (gate_tower_ew_mor, static(), '市門の櫓（東西にくぐる）')
    cat['market_cross_mor'] = (market_cross_mor, static(), '市の十字塔')
    for i in range(1, 4):
        cat['market_stall_mor_%02d' % i] = (market_stall_mor, static(), '市の屋台')
    cat['pillory_mor'] = (pillory_mor, static(), 'さらし台')
    cat['gallows_mor'] = (gallows_mor, static(), '絞首台')
    cat['bell_tower_mor'] = (bell_tower_mor, bell_parts(), '鐘楼（鐘は振り子で揺れる）')
    cat['saint_statue_mor'] = (saint_statue_mor, static(), '聖人の像')
    cat['city_well_mor'] = (city_well_mor, static(), '屋根つきの石の井戸')
    cat['bridge_rail_n_mor'] = (bridge_rail_n_mor, static(), '石橋の欄干（北の辺）')
    cat['bridge_rail_s_mor'] = (bridge_rail_s_mor, static(), '石橋の欄干（南の辺）')

    # ---- 通りと生活 ----
    street = {
        'crate_pile': (crate_pile_mor, '木箱の積み'),
        'barrel_stack': (barrel_stack_mor, '樽'),
        'sack_pile': (sack_pile_mor, '麻袋'),
        'handcart': (handcart_mor, '手押し荷車'),
        'wagon_horse': (wagon_horse_mor, '荷馬車と馬'),
        'hitch_rail': (hitch_rail_mor, '馬つなぎ'),
        'horse_trough': (horse_trough_mor, '石の水槽'),
        'stone_bench': (stone_bench_mor, '石の腰掛け'),
        'planter': (planter_mor, '石の花壇'),
        'dung_pile': (dung_pile_mor, '馬糞と藁'),
        'gutter_grate': (gutter_grate_mor, '排水の鉄格子'),
        'alley_junk': (alley_junk_mor, '路地のがらくた'),
        'cat': (cat_mor, '猫'),
        'pigeons': (pigeons_mor, '鳩 3 羽'),
        'forge': (forge_mor, '鍛冶の炉。火は forge_fire_mor'),
        'forge_fire': (forge_fire_mor, '炉の火'),
        'anvil': (anvil_mor, '金床'),
        'armour_display': (armour_display_mor, '鎧の陳列'),
        'weapon_rack': (weapon_rack_mor, '武器立て'),
        'spear_rack': (spear_rack_mor, '槍立て'),
        'butts': (butts_mor, '弓の的'),
        'pell': (pell_mor, '打ち込み杭'),
        'herb_rack': (herb_rack_mor, '薬草の干し台'),
        'covered_cart': (covered_cart_mor, '幌の荷車'),
        'brazier': (brazier_mor, '篝火の鉄鉢。火は brazier_fire_mor'),
        'brazier_fire': (brazier_fire_mor, '篝火の火'),
        'ale_barrels': (ale_barrels_mor, '麦酒の樽'),
        'tavern_table': (tavern_table_mor, '外の卓と長椅子'),
        'relic_plinth': (relic_plinth_mor, '台座の遺物'),
        'orb_stand': (orb_stand_mor, '天球儀。光は orb_glow_mor'),
        'orb_glow': (orb_glow_mor, '天球儀の光'),
        'wayside_shrine': (wayside_shrine_mor, '壁龕の祠。火は shrine_flame_mor'),
        'shrine_flame': (shrine_flame_mor, '祠の蝋燭の火'),
        'guard_post': (guard_post_mor, '衛兵の詰め所'),
        'herb_bed': (herb_bed_mor, '薬草の畝'),
        'tomb_slab': (tomb_slab_mor, '石棺の蓋'),
    }
    for key, (builder, note) in street.items():
        cat[key + '_mor'] = (builder, static(), 'モリバントの小物: ' + note)
    cat['ivy_mor'] = (ivy_mor, wind, 'モリバントの小物: 蔦')
    cat['laundry_line_mor'] = (laundry_line_mor, laundry_parts(), '通りをまたぐ洗濯物')
    cat['guild_sign_mor'] = (guild_sign_mor, guild_sign_parts(), 'ギルドの張り出し看板')
    cat['banner_drape_mor'] = (banner_drape_mor, banner_parts(), '紋章の垂れ幕')
    cat['flag_pole_mor'] = (flag_pole_mor, flag_parts(), '旗竿')

    # ---- 緑と墓 ----
    for i in range(1, 3):
        cat['orchard_tree_mor_%02d' % i] = (orchard_tree_mor, wind, '果樹（林檎・梨）')
        cat['grave_cross_mor_%02d' % i] = (grave_cross_mor, static(), '墓標')

    # ---- 屋根 ----
    cat['chimney_mor'] = (chimney_mor, static(), '石の煙突と鳩（roof_vent）')


# ------------------------------------------------------------------ 噴水（2026-09-05）
#
# こう決めた——「噴水の水はボクセルをうまく順番に明滅させ水滴が動くようにアニメーションさせて。
# 現状は紐がぶらぶらしてるみたい」。基の `fountain_jet` は弧を振り子で揺らしていた。
# ここでは**水滴の位置を段ごとに別のパーツ**にし、`blink`（明滅）の位相をずらして順に点す。
# 点いた所が上へ昇り、頂で砕けて四方の柱へ落ちて見える。
FJ_STAGES = 18      #: 1 周の段数（0〜7 が上昇、8〜17 が四方への落下）
FJ_PERIOD = 1.9     #: 1 周の秒数
FJ_DUTY = 0.22      #: 各段が見えている割合（4 段ほどが同時に点く＝粒の列）


def _fj_blob(v, cx, cy, cz, r, colour):
    ri = int(math.ceil(r))
    for x in range(max(0, cx - ri), min(v.shape[0], cx + ri + 1)):
        for y in range(max(0, cy - ri), min(v.shape[1], cy + ri + 1)):
            for z in range(max(0, cz - ri), min(v.shape[2], cz + ri + 1)):
                if (((x - cx) ** 2) + ((y - cy) ** 2) + ((z - cz) ** 2)) <= (r * r):
                    v[x, y, z] = colour


def fountain_jet_mor(name):
    """噴水の噴き上げ（明滅で動く水滴）。3×3 マス。接地しない。"""
    span = G['FOUNTAIN_V']
    top = G['FOUNTAIN_TOP']
    mid = span // 2
    rng = G['rng_for'](name)
    parts = []
    #! 細い芯（静止）。水滴だけだと噴水の形が読めない。
    core = np.zeros((span, span, top), np.int16)
    for z in range(top):
        r = 3.0 - (1.6 * (z / float(top)))
        colour = C['jetwa'] if z < top * 0.6 else C['spray']
        ri = int(math.ceil(r))
        for x in range(mid - ri, mid + ri + 1):
            for y in range(mid - ri, mid + ri + 1):
                if (((x - mid) ** 2) + ((y - mid) ** 2)) <= (r * r):
                    core[x, y, z] = colour
    parts.append(('core', core, (0, 0, 2)))
    #! 段ごとの水滴。上昇 8 段は芯のまわりに 4 粒、落下 10 段は四方の弧に 1 粒ずつ（＋しぶき）。
    for stage in range(FJ_STAGES):
        drops = np.zeros((span, span, top + 20), np.int16)
        if stage < 8:
            t = (stage + 0.5) / 8.0
            z = int(round(t * top))
            rad = 3.0 + (3.0 * t)
            for k in range(4):
                ang = (k * 1.5708) + (stage * 0.6)
                _fj_blob(drops, int(round(mid + rad * math.cos(ang))), int(round(mid + rad * math.sin(ang))), z,
                         2.5 + (0.8 * t), C['foam'] if (k % 2) else C['spray'])
        else:
            t = (stage - 8 + 0.5) / 10.0
            for (dx, dy) in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                reach = t * (G['V'] * 0.98)
                z = top + int(round((16.0 * t) - (60.0 * t * t)))
                if z < 2:
                    continue
                _fj_blob(drops, int(round(mid + dx * reach)), int(round(mid + dy * reach)), z,
                         3.2 - (1.0 * t), C['foam'] if (stage % 2) else C['spray'])
        if not drops.any():
            drops[mid, mid, top + 19] = C['foam']
        parts.append(('drop_%02d' % stage, drops, (0, 0, 2)))
    #! 頂の冠（2 枚を交互に明滅＝きらめき）。
    for k in range(2):
        crown = np.zeros((span, span, top + 20), np.int16)
        for i in range(16):
            ang = (i / 16.0) * 6.283 + (k * 0.2)
            rad = 4.0 + (float(rng.random()) * 6.0)
            _fj_blob(crown, int(round(mid + rad * math.cos(ang))), int(round(mid + rad * math.sin(ang))),
                     top + int(rng.integers(0, 10)), 1.5 + float(rng.random()), C['foam'] if (i % 3) else C['spray'])
        parts.append(('crown_%d' % k, crown, (0, 0, 2)))
    #! 柱の上のしぶき（落ちた粒が砕ける所。4 本。落下の終わりに合わせて点く）。
    for index, (dx, dy) in enumerate(((0, -1), (0, 1), (-1, 0), (1, 0))):
        splash = np.zeros((span, span, top + 20), np.int16)
        cx, cy = int(round(mid + dx * G['V'] * 0.98)), int(round(mid + dy * G['V'] * 0.98))
        for i in range(6):
            ang = (i / 6.0) * 6.283
            _fj_blob(splash, int(round(cx + 3 * math.cos(ang))), int(round(cy + 3 * math.sin(ang))), 8 + (i % 2) * 2, 1.4, C['foam'])
        parts.append(('splash_%d' % index, splash, (0, 0, 2)))
    return parts


def fountain_jet_mor_parts():
    decl = [{'name': 'core', 'voxels': 'core', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    #! **順番は逆回し**（2026-09-06 に決めた「噴水のアニメーション順番を逆にして」）。
    #! 位相を `1 - phase` にすると時間が逆に流れる（段 0 はそのまま、あとは後ろから点く）。
    for stage in range(FJ_STAGES):
        decl.append({'name': 'drop_%02d' % stage, 'voxels': 'drop_%02d' % stage, 'grounded': False,
                     'motion': {'kind': 'blink', 'period': FJ_PERIOD,
                                'phase': round(((FJ_STAGES - stage) % FJ_STAGES) / float(FJ_STAGES), 4),
                                'duty': FJ_DUTY},
                     'wind_k': 0.0})
    for k in range(2):
        decl.append({'name': 'crown_%d' % k, 'voxels': 'crown_%d' % k, 'grounded': False,
                     'motion': {'kind': 'blink', 'period': 0.5, 'phase': 0.5 * k, 'duty': 0.5}, 'wind_k': 0.0})
    for index in range(4):
        #! しぶきも同じ式で逆回しにする（落下の終わり ＝ 逆回しでは始まり）。
        decl.append({'name': 'splash_%d' % index, 'voxels': 'splash_%d' % index, 'grounded': False,
                     'motion': {'kind': 'blink', 'period': FJ_PERIOD,
                                'phase': round((1.0 - (0.92 + 0.02 * index)) % 1.0, 4), 'duty': 0.25},
                     'wind_k': 0.0})
    return decl


_register_before_fountain = register


def register(g):  # noqa: F811
    _register_before_fountain(g)
    g['CATALOG']['fountain_jet_mor'] = (fountain_jet_mor, fountain_jet_mor_parts(),
                                        '噴水の噴き上げ（水滴が順に明滅して昇り、四方へ落ちる）')
