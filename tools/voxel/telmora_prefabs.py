# -*- coding: utf-8 -*-
"""テルモラ（町 2 番）の作り込み — 素材の生成器。

`gen_prefabs.py` の末尾から `register(globals())` で呼ばれ、接尾辞 `_tel` の素材を
`CATALOG` へ足す。**既存の `_tel` の素材には 1 バイトも触らない**（別の名前で足すだけ）。

## この町の材（既存の `_tel` に合わせる）
- 石は**乱石積み**（`rub` / `rub_l` / `rub_d`）。切石（`ash` `flag`）はモリバントの材なので使わない
- 木は**濃い木骨**（`beam`）と**素の材**（`timber` `oak` `board` `log`）
- 壁の面は**生成りの漆喰**（`cream` / `cream_l` / `cream_d`）
- 屋根は**藁**（`straw` 系）と**こけら板**（`shin` 系）

## 作りの規律（`gen_prefabs.py` 冒頭と同じ）
- 無作為な表面ノイズは入れない。彫りは 1 ボクセルの深さ。色は塊ごとに 1 色
- 接地層（z=0）の平面方向は縮めない。**+y は南＝カメラ側**。戸口は手前が y 大
- 柵の高さ（14）は変えない（遮蔽の約束）
- 色は共通の登録簿（`C`）を使い、足りない色だけ `reg_local`。
  **`reg_local` の名前は材質の語で始める**（`cloth_` `fur_` `soil_` `grass_` `dirt_` `wa_`
  `leaf_` `steel_` `iron_`）——`palette_materials.classify` は「名前が材質の語で始まるか」で
  当てるので、`tel_cloth_r` のように町の頭を先に置くと材質が `Default` に落ちる。

小物は 1 マスに収める（接地する部分は z=0 のマスの中。上のほうがはみ出すのは可）。
火と光の相方（`*_fire_tel` `*_glow_tel`）は別体で、置く側が自発光をつけて重ねる。
"""
from __future__ import annotations

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


def wind_parts(names, wind_k=0.6):
    """`frame`（静止）＋ 風に揺れる布の部品。"""
    parts = [{'name': 'frame', 'voxels': 'frame', 'grounded': True,
              'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    for name in names:
        parts.append({'name': name, 'voxels': name, 'parent': 'frame', 'grounded': False,
                      'motion': {'kind': 'wind'}, 'wind_k': float(wind_k)})
    return parts


def spin_parts(name, pivot, axis='y', speed=0.35):
    """`frame`（静止）＋ 回る部品（風車の羽根・水車）。"""
    return [
        {'name': 'frame', 'voxels': 'frame', 'grounded': True,
         'motion': {'kind': 'static'}, 'wind_k': 0.0},
        {'name': name, 'voxels': name, 'parent': 'frame', 'grounded': False,
         'pivot': [float(pivot[0]), float(pivot[1]), float(pivot[2])],
         'motion': {'kind': 'rotate', 'axis': axis, 'speed': float(speed), 'phase': 0.0},
         'wind_k': 0.0},
    ]


# ------------------------------------------------------------------ 色
def _palette():
    """この意匠だけの色。**名前は材質の語で始める**（冒頭の註記）。"""
    rl = G['reg_local']
    #! 布（幟・旗・垂れ幕・洗濯物・日除け）。テルモラの旗は**城の朱**と**藍**の 2 色。
    rl('cloth_tm_r', (172, 46, 40)); rl('cloth_tm_rd', (126, 32, 28))
    rl('cloth_tm_b', (46, 68, 132)); rl('cloth_tm_bd', (32, 48, 98))
    rl('cloth_tm_g', (62, 104, 72)); rl('cloth_tm_y', (212, 174, 92))
    rl('cloth_tm_w', (238, 234, 224)); rl('cloth_tm_wd', (204, 198, 188))
    #! 毛（馬・鶏・猫・豚）。`fur` で布の材質（毛は織り物と同じ扱い）。
    rl('fur_tm_horse', (98, 72, 50)); rl('fur_tm_horsed', (64, 46, 32))
    rl('fur_tm_horsew', (226, 220, 208))
    rl('fur_tm_hen', (206, 198, 184)); rl('fur_tm_hend', (150, 140, 126))
    rl('fur_tm_cat', (86, 74, 62)); rl('fur_tm_catl', (138, 122, 104))
    rl('fur_tm_pig', (212, 158, 146))
    #! 馬糞・畑の黒土・堆肥（`soil` `dirt` で土の材質）。
    rl('soil_tm_dung', (94, 74, 48)); rl('soil_tm_furrow', (84, 62, 42))
    rl('dirt_tm_bed', (70, 54, 38)); rl('dirt_tm_bedl', (96, 76, 56))
    #! 畑の葉物と薬草（`grass` で草の材質。木の葉の描き込みは入れない）。
    rl('grass_tm_veg', (94, 138, 62)); rl('grass_tm_vegl', (130, 172, 86))
    rl('grass_tm_herb', (116, 136, 94))
    #! 溜まり水と轍の黒い水（`wa` で汚さない材質）。
    rl('wa_tm_mud', (82, 74, 62)); rl('wa_tm_mudl', (112, 102, 88))
    rl('wa_tm_dark', (32, 30, 28)); rl('wa_tm_brew', (108, 168, 96))
    rl('wa_tm_brewl', (152, 208, 132))
    #! 蔦と果樹の実（`leaf` で木の葉の材質）。
    rl('leaf_tm_ivy', (70, 104, 54)); rl('leaf_tm_ivyd', (48, 76, 40))
    rl('leaf_tm_apple', (194, 62, 50)); rl('leaf_tm_pear', (198, 182, 78))
    #! 磨いた鋼と黒い錬鉄（共通の表には `steel_l` と黒い鉄が無い）。
    rl('steel_tm_l', (214, 218, 226)); rl('iron_tm_k', (46, 46, 52))


# ------------------------------------------------------------------ 共通の作り
def _rubble(v, x0, x1, y0, y1, z0, z1, rng, course=7):
    """乱石積みの塊（既存の `_house_wall_timber` の土台と同じ流儀）。

    **段の境を 1 段彫って暗くする**。石は横に並べるので、外面は横の帯になる
    ——2 次元のむらをそのまま上へ伸ばすと側面が縦縞になって板壁に見える。
    """
    box(v, x0, x1, y0, y1, z0, z1, K('rub'))
    for z in range(z0, z1, course):
        box(v, x0, x1, y0, y1, z, z + 1, K('joint'))
        box(v, x0, x1, y0, y1, z + 1, min(z1, z + course),
            pick(rng, [K('rub'), K('rub_l'), K('rub_d')], [0.48, 0.28, 0.24]))
    box(v, x0, x1, y0, y1, z1 - 1, z1, K('rub_l'))


def _crenels(v, x0, x1, y0, y1, z0, z1, step=8, gap=4):
    """胸壁の狭間（外周の帯を `step` ごとに `gap` だけ抜く）。"""
    for x in range(x0, x1, step):
        box(v, x, x + gap, y0, y1, z0, z1, 0)
    for y in range(y0, y1, step):
        box(v, x0, x1, y, y + gap, z0, z1, 0)


def _shingle_pyramid(v, x0, x1, y0, y1, z0, rise, rng):
    """こけら板の方形の屋根（4 方へ落ちる）。段ごとに板の色を替える。"""
    for k in range(rise):
        a0, a1 = x0 + k, x1 - k
        b0, b1 = y0 + k, y1 - k
        if (a0 >= a1) or (b0 >= b1):
            break
        shade = pick(rng, [K('shin'), K('shin_l'), K('shin_d')], [0.44, 0.28, 0.28])
        box(v, a0, a1, b0, b1, z0 + k, z0 + k + 1, shade)
        box(v, a0 + 1, a1 - 1, b0 + 1, b1 - 1, z0 + k, z0 + k + 1, K('shin_d')
            if (k % 3 == 0) else shade)


def _spire(v, x0, x1, y0, y1, z0, hold, rng):
    """こけら板の**尖った**屋根。`hold` 段ごとに 1 だけ細くする（1:1 の四角錐は寝て見える）。

    塔の頭に載せるものは、寄棟と同じ勾配では「屋根」にしかならない。
    段の数を稼いで**縦に伸ばす**と、はじめて尖塔として読める。
    """
    k = 0
    z = z0
    while True:
        a0, a1 = x0 + k, x1 - k
        b0, b1 = y0 + k, y1 - k
        if (a0 >= a1 - 1) or (b0 >= b1 - 1):
            break
        for _ in range(hold):
            shade = pick(rng, [K('shin'), K('shin_l'), K('shin_d')], [0.42, 0.30, 0.28])
            box(v, a0, a1, b0, b1, z, z + 1, shade)
            box(v, a0 + 1, a1 - 1, b0 + 1, b1 - 1, z, z + 1,
                K('shin_d') if ((z - z0) % 5 == 0) else shade)
            z += 1
        k += 1
    box(v, x0 + k - 1, x1 - k + 1, y0 + k - 1, y1 - k + 1, z, z + 2, K('shin_l'))
    return z + 2


def _thatch_cap(v, x0, x1, y0, y1, z0, rise, rng):
    """藁葺きの小さな寄棟（井戸・祠・屋台の屋根）。棟は明るい藁で締める。"""
    for k in range(rise):
        a0, a1 = x0 + k, x1 - k
        b0, b1 = y0 + k, y1 - k
        if (a0 >= a1) or (b0 >= b1):
            break
        shade = pick(rng, [K('straw'), K('straw_l'), K('straw_d'), K('straw_g')],
                     [0.42, 0.24, 0.22, 0.12])
        box(v, a0, a1, b0, b1, z0 + k, z0 + k + 1, shade)
        if k == rise - 1:
            box(v, a0, a1, b0, b1, z0 + k, z0 + k + 1, K('straw_l'))
    box(v, x0, x1, y0, y1, z0, z0 + 1, K('straw_d'))   # 軒の切り口は影で暗い


def _staves(v, z0, z1, dark, step=3):
    """桶の縦板。**既に材のある所だけ**を塗り替える。

    `box(...)` で縞を引くと、桶の外の空のマスまで塗って**四角い箱**になる（この回の記録）。
    """
    for x in range(0, v.shape[0], step):
        layer = v[x:x + 1, :, z0:z1]
        layer[layer != 0] = dark


def _cart_wheel(v, cx, cz, r, y0, y1, spokes=8):
    """車輪（軸は y。輪・輻・轂）。**輻は線で引く**（塊で描くと車輪に見えない）。"""
    log_y(v, cx, cz, r, y0, y1, K('timber_d'))
    log_y(v, cx, cz, r - 1.6, y0, y1, 0)
    for k in range(spokes):
        a = k * (6.2832 / spokes)
        for t in range(int(r)):
            px = int(round(cx + (t * np.cos(a))))
            pz = int(round(cz + (t * np.sin(a))))
            box(v, px, px + 1, y0, y1, pz, pz + 1, K('timber'))
    log_y(v, cx, cz, 1.6, y0, y1, K('iron'))


def _horse_east(v):
    """馬 1 頭（**東を向く**。1 マスの東半分に収める）。

    太い箱 1 つでは馬に読めない（モリバントの記録）——胴は薄く長く、脚は細く 4 本、
    首は斜めに上げ、頭は前へ出す。鬣と尾と蹄で輪郭を締める。
    """
    box(v, 21, 30, 13, 19, 13, 19, K('fur_tm_horse'))          # 胴
    box(v, 21, 30, 13, 19, 18, 19, K('fur_tm_horsed'))         # 背
    box(v, 21, 30, 18, 19, 13, 19, K('fur_tm_horsed'))         # 南側の陰
    for (x, y) in ((22, 13), (22, 17), (28, 13), (28, 17)):    # 脚
        box(v, x, x + 2, y, y + 2, 2, 14, K('fur_tm_horse'))
        box(v, x, x + 2, y, y + 2, 0, 2, K('char'))            # 蹄
    for k in range(6):                                         # 首（斜めに上がる）
        box(v, 29 + (k // 3), 31 + (k // 3), 14, 18, 18 + k, 20 + k, K('fur_tm_horse'))
    box(v, 28, 32, 14, 18, 24, 27, K('fur_tm_horse'))          # 頭
    box(v, 27, 29, 15, 17, 24, 26, K('fur_tm_horse'))          # 鼻面
    box(v, 27, 28, 15, 17, 25, 26, K('fur_tm_horsew'))         # 星（白斑）
    box(v, 30, 32, 14, 15, 26, 28, K('fur_tm_horse'))          # 耳
    box(v, 30, 32, 17, 18, 26, 28, K('fur_tm_horse'))
    for k in range(7):                                         # 鬣
        box(v, 28 + (k // 4), 30 + (k // 4), 15, 17, 20 + k, 22 + k, K('fur_tm_horsed'))
    box(v, 19, 22, 15, 17, 10, 19, K('fur_tm_horsed'))         # 尾


# ------------------------------------------------------------------ 地面
def _halfpaved(rng, face):
    """テルモラの半舗装（既存の `_ground_path_halfpaved` と同じ流儀）。

    踏み固めた土に割った石が疎らに埋まる。**石は面積の 3 割ほど**に留める
    ——全面を敷石にするとモリバントの都と見分けが付かなくなる。
    """
    mottle(rng, face, [K('pack'), K('pack_l'), K('pack_d'), K('dirt')], [0.40, 0.26, 0.24, 0.10])
    for _ in range(int(rng.integers(5, 10))):
        x, y = int(rng.integers(0, V - 6)), int(rng.integers(0, V - 6))
        w, d = int(rng.integers(3, 7)), int(rng.integers(3, 7))
        face[x:x + w, y:y + d] = pick(rng, [K('rub'), K('rub_l'), K('rub_d')], [0.5, 0.28, 0.22])


def ground_market_tel(name):
    """市の広場 — 半舗装に藁くず・こぼれた麦・野菜くず・轍。

    表通り（`ground_path_tel_*`）と同じ土と石を敷いて**同じ通りの続き**に見せ、
    落ちている物だけで「市が立つ場所」を言う。
    """
    rng = rng_for(name)
    v = tile(2)
    box(v, 0, V, 0, V, 0, 1, K('pack_d'))
    face = np.zeros((V, V), np.int16)
    _halfpaved(rng, face)
    x = int(rng.integers(4, 12))                                # 轍（車輪の 2 筋）
    for y in range(V):
        for w in (x, x + 13):
            if 0 <= w < V - 1:
                face[w:w + 2, y] = K('pack_d')
        x += int(rng.integers(-1, 2))
    for _ in range(int(rng.integers(5, 9))):                    # 藁くず（短い線）
        px, py = int(rng.integers(0, V - 4)), int(rng.integers(0, V - 1))
        face[px:px + int(rng.integers(2, 5)), py] = K('straw_d') if rng.random() < 0.5 else K('hay_d')
    for _ in range(int(rng.integers(2, 5))):                    # こぼれた麦
        px, py = int(rng.integers(0, V - 2)), int(rng.integers(0, V - 2))
        face[px:px + 2, py:py + 2] = K('straw_l')
    for _ in range(int(rng.integers(1, 4))):                    # 野菜くず
        px, py = int(rng.integers(0, V - 3)), int(rng.integers(0, V - 3))
        face[px:px + 2, py:py + 3] = K('grass_tm_veg')
    grain(rng, face, face > 0, [K('grit'), K('pebble')], [0.05, 0.04])
    v[:, :, 1] = face
    return ground(v, 2)


def ground_muck_tel(name):
    """路地のぬかるみ — 踏み固めた土に泥が溜まり、溜まり水と轍と馬糞。"""
    rng = rng_for(name)
    v = tile(2)
    box(v, 0, V, 0, V, 0, 1, K('pack_d'))
    face = np.zeros((V, V), np.int16)
    mottle(rng, face, [K('dirt_d'), K('pack_d'), K('dirt'), K('wa_tm_mud')],
           [0.34, 0.28, 0.22, 0.16])
    for _ in range(int(rng.integers(3, 6))):                    # 溜まり水（塊で置く）
        px, py = int(rng.integers(0, V - 7)), int(rng.integers(0, V - 7))
        w, d = int(rng.integers(4, 8)), int(rng.integers(4, 8))
        face[px:px + w, py:py + d] = K('wa_tm_dark')
        face[px + 1:px + w - 1, py + 1:py + d - 1] = K('wa_tm_mud')
    x = int(rng.integers(5, 14))                                # 深い轍
    for y in range(V):
        for w in (x, x + 11):
            if 0 <= w < V - 2:
                face[w:w + 3, y] = K('wa_tm_dark')
        x += int(rng.integers(-1, 2))
    for _ in range(int(rng.integers(1, 4))):                    # 馬糞と藁
        px, py = int(rng.integers(0, V - 4)), int(rng.integers(0, V - 4))
        face[px:px + 3, py:py + 3] = K('soil_tm_dung')
    v[:, :, 1] = face
    return ground(v, 2)


def ground_field_tel(name):
    """畑の畝 — 黒土を東西に起こし、溝を彫って芽を並べる。

    **厚みは 6 とる。**4 で作って葉を z=4..5 に置くと `box()` が範囲へ切り詰めて
    葉が 1 枚も出ないタイルになる（モリバントの記録）。
    """
    rng = rng_for(name)
    v = tile(6)
    box(v, 0, V, 0, V, 0, 4, K('dirt_tm_bed'))
    for y in range(0, V, 8):                                    # 畝（東西に走る）
        box(v, 0, V, y, y + 5, 4, 5, K('dirt_tm_bedl'))
        box(v, 0, V, y + 5, y + 8, 3, 4, K('soil_tm_furrow'))   # 溝は 1 段沈む
        box(v, 0, V, y + 5, y + 8, 4, 5, 0)
    for y in range(0, V, 8):                                    # 芽（畝の背に並ぶ）
        for x in range(2, V - 2, 6):
            if rng.random() < 0.72:
                w = int(rng.integers(2, 4))
                box(v, x, x + w, y + 1, y + 4, 5, 6, K('grass_tm_veg'))
                if rng.random() < 0.4:
                    box(v, x, x + w, y + 2, y + 3, 5, 6, K('grass_tm_vegl'))
    return ground(v, 6)


def ground_grave_tel(name):
    """墓地の地面 — 踏み跡の残る芝と、掘り返した土の盛り。"""
    rng = rng_for(name)
    v = tile(3)
    box(v, 0, V, 0, V, 0, 1, K('pack_d'))
    box(v, 0, V, 0, V, 1, 2, K('pack'))
    face = np.zeros((V, V), np.int16)
    mottle(rng, face, [K('turf_d'), K('turf'), K('pack'), K('moss_d')], [0.34, 0.28, 0.22, 0.16])
    for _ in range(int(rng.integers(2, 5))):                    # 新しい土盛り
        px, py = int(rng.integers(0, V - 9)), int(rng.integers(0, V - 6))
        face[px:px + int(rng.integers(6, 10)), py:py + int(rng.integers(4, 7))] = K('dirt_tm_bed')
    for _ in range(int(rng.integers(1, 4))):                    # 踏み跡
        py = int(rng.integers(0, V - 3))
        face[:, py:py + 3] = K('pack_d')
    v[:, :, 2] = face
    return ground(v, 3)


def ground_lists_tel(name):
    """馬場の砂 — ならした砂に蹄の跡と、走った筋。

    城の前の試合場。**筋は南北**（東西に馬を走らせる）に引かず、走路に沿わせる。
    """
    rng = rng_for(name)
    v = tile(2)
    box(v, 0, V, 0, V, 0, 1, K('pack_d'))
    face = np.zeros((V, V), np.int16)
    mottle(rng, face, [K('grit'), K('pack_l'), K('pack'), K('dirt_l')], [0.36, 0.28, 0.20, 0.16])
    for _ in range(int(rng.integers(2, 4))):                    # 走った筋（東西）
        py = int(rng.integers(2, V - 4))
        face[:, py:py + 3] = K('pack_d')
    for _ in range(int(rng.integers(6, 12))):                   # 蹄の跡（対で置く）
        px, py = int(rng.integers(0, V - 5)), int(rng.integers(0, V - 5))
        face[px:px + 3, py:py + 2] = K('dirt_d')
        face[px + 1:px + 4, py + 3:py + 5] = K('dirt_d')
    v[:, :, 1] = face
    return ground(v, 2)


def ground_bridge_tel(name):
    """石橋の甲板 — 迫石の見える橋面。堀を渡る 2 マスに敷く。"""
    rng = rng_for(name)
    v = tile(3)
    box(v, 0, V, 0, V, 0, 3, K('rub_d'))
    face = np.zeros((V, V), np.int16)
    for by in range(0, V, 6):                                   # 迫石（南北に並ぶ帯）
        shade = pick(rng, [K('rub'), K('rub_l'), K('rub_d')], [0.46, 0.30, 0.24])
        face[:, by:by + 5] = shade
        face[:, by + 5:by + 6] = K('joint')
    face[0:3, :] = K('rub_d')                                   # 甲板の縁（車止め）
    face[V - 3:V, :] = K('rub_d')
    grain(rng, face, face > 0, [K('grit'), K('moss_d')], [0.05, 0.04])
    v[:, :, 2] = face
    return ground(v, 3)


# ------------------------------------------------------------------ 区画の柵
def _rim_side(name):
    """名前の末尾（`_n` / `_s` / `_w` / `_e`）を返す。"""
    return name.rsplit('_', 1)[-1]


def wattle_tel(name):
    """編み垣 — 割り杭に細枝を横へ編み込む。畑と菜園の囲い。高さ 14。"""
    rng = rng_for(name)
    side = _rim_side(name)
    v = vol(h=16)
    d0, d1 = (0, 4)
    for x in range(1, V - 1, 6):                                # 杭
        box(v, x, x + 2, d0, d1, 0, 15, K('timber_d'))
    for i, z in enumerate(range(1, 14, 2)):                     # 編んだ枝（1 段おきに前後）
        off = 1 if (i % 2) else 0
        box(v, 0, V, d0 + off, d0 + off + 3, z, z + 2,
            pick(rng, [K('mbr'), K('mbr_d'), K('bark')], [0.44, 0.32, 0.24]))
    box(v, 0, V, d0, d1, 13, 14, K('mbr_d'))                    # 編み終いの縁
    return _rim_place(v, side)


def railing_tel(name):
    """木の横桟の柵 — 角の杭に貫を 2 本。墓地と馬場の囲い。高さ 14。"""
    rng = rng_for(name)
    side = _rim_side(name)
    v = vol(h=16)
    d0, d1 = (0, 4)
    for x in range(0, V, 8):                                    # 杭（頭を斜めに落とす）
        box(v, x, x + 3, d0, d1, 0, 15, K('oak'))
        box(v, x, x + 3, d0, d1, 13, 15, K('oak_l'))
    for z in (4, 9):                                            # 貫
        box(v, 0, V, d0 + 1, d0 + 3, z, z + 3,
            pick(rng, [K('timber'), K('timber_l')], [0.6, 0.4]))
    return _rim_place(v, side)


def _rim_place(v, side):
    """北の辺に作った柵を、名前の向きへ回す（`_n` はそのまま）。"""
    if side == 'n':
        return one(v)
    if side == 's':
        return one(v[:, ::-1, :].copy())
    parts = G['rotate_parts_90'](one(v))
    if side == 'w':
        return parts
    name, arr, org = parts[0]
    return [(name, arr[::-1, :, :].copy(), org)]


# ------------------------------------------------------------------ 都市の骨（大物）
def gatehouse_tel(name):
    """**東の大門楼** — 街道が城壁と堀を抜ける 1 か所に立つ、町でいちばん大きな門。

    脚を北と南に置き、**東西にくぐる**（テルモラの街道は東西に走る）。
    上に落とし格子・持ち送りの狭間・歩廊・朱の旗。塀（1.4 マス）の**2.7 倍**の高さで、
    遠目にはこれ 1 つで「城塞街の入口」が読める。

    **意匠は x の向きに繰り返せる**ようにしてある（(68,33) と (69,33) の 2 マスへ
    同じ物を置くと、落とし格子が外と内に 1 枚ずつ立つ 2 マスぶんの門道になる）。
    """
    rng = rng_for(name)
    v = vol(h=124)
    #! 脚（乱石積み。北と南の袖）。
    _rubble(v, 0, V, 0, 9, 0, 72, rng)
    _rubble(v, 0, V, 23, V, 0, 72, rng)
    box(v, 0, V, 0, 3, 0, 72, K('rub_d'))                       # 外面は影で締める
    box(v, 0, V, V - 3, V, 0, 72, K('rub_d'))
    #! 門道のアーチ（内側へ迫り出す）。**4 段まで**——深く迫り出すと開口が閉じて
    #! 「割れ目の入った塔」に見える（モリバントの記録）。
    for k in range(1, 5):
        box(v, 0, V, 9, 9 + k, 52 + k, 54 + k, K('rub_l'))
        box(v, 0, V, V - 9 - k, V - 9, 52 + k, 54 + k, K('rub_l'))
    box(v, 0, V, 11, V - 11, 0, 52, 0)                          # 門道を刳り抜く
    box(v, 0, V, 9, V - 9, 0, 8, K('rub_d'))                    # 門道の敷石
    for x in range(0, V, 6):
        box(v, x, x + 5, 9, V - 9, 6, 8, K('rub'))
    #! 落とし格子（鉄。門道の中ほどに下ろした形で見せる）。
    box(v, 14, 17, 10, V - 10, 8, 50, K('iron_tm_k'))
    for y in range(10, V - 10, 4):
        box(v, 13, 18, y, y + 2, 8, 50, K('iron'))
    for z in range(12, 50, 8):
        box(v, 13, 18, 10, V - 10, z, z + 2, K('iron'))
    box(v, 13, 18, 10, V - 10, 8, 11, K('rust_d'))              # 下端の錆
    #! 門道の上の床と持ち送り（machicolation）。
    box(v, 0, V, 0, V, 72, 78, K('rub'))
    box(v, 0, V, 0, V, 72, 73, K('rub_d'))
    box(v, 0, V, 0, 3, 76, 80, K('rub_l'))
    box(v, 0, V, V - 3, V, 76, 80, K('rub_l'))
    for x in range(1, V - 2, 6):                                # 石落としの穴
        box(v, x, x + 3, 0, 3, 76, 78, 0)
        box(v, x, x + 3, V - 3, V, 76, 78, 0)
    #! 上の部屋（木骨と漆喰。町の家と同じ材で「町の門」だと分かる）。
    #! **木骨は 4 面ぜんぶに要る**——街道を来る旅人が見るのは x の面（門道の正面）で、
    #! y の面だけに柱を立てると、正面が漆喰の無地の板に見えた（この回の記録）。
    box(v, 0, V, 3, V - 3, 78, 100, K('cream'))
    box(v, 0, V, 3, V - 3, 78, 81, K('beam'))                   # 土台差し
    box(v, 0, V, 3, V - 3, 96, 100, K('beam'))                  # 軒桁
    for x in range(0, V, 8):                                    # 柱（南北の面に出る）
        box(v, x, x + 2, 3, V - 3, 78, 100, K('beam'))
    for y in range(3, V - 3, 8):                                # 柱（東西の面に出る）
        box(v, 0, 2, y, y + 2, 78, 100, K('beam'))
        box(v, V - 2, V, y, y + 2, 78, 100, K('beam'))
    for y in (5, 13, 21):                                       # 筋交い（東西の面）
        for t in range(8):
            z = 82 + (t * 2)
            box(v, 0, 2, y + t, y + t + 2, z, z + 3, K('beam'))
            box(v, V - 2, V, y + t, y + t + 2, z, z + 3, K('beam'))
    box(v, 0, 2, 3, V - 3, 86, 89, K('beam'))                   # 胴差し（東西の面）
    box(v, V - 2, V, 3, V - 3, 86, 89, K('beam'))
    for x in range(3, V - 3, 10):                               # 矢狭間（南北の面）
        box(v, x, x + 3, 2, 5, 84, 94, K('dark'))
        box(v, x, x + 3, V - 5, V - 2, 84, 94, K('dark'))
    for y in (8, 18):                                           # 鎧戸の窓（東西の面）
        box(v, 0, 3, y, y + 6, 89, 95, K('dark'))
        box(v, V - 3, V, y, y + 6, 89, 95, K('dark'))
        box(v, 0, 3, y - 1, y + 7, 95, 97, K('timber_d'))
        box(v, V - 3, V, y - 1, y + 7, 95, 97, K('timber_d'))
    #! 胸壁（狭間つき）と歩廊。
    box(v, 0, V, 0, V, 100, 104, K('rub_l'))
    box(v, 0, V, 0, V, 104, 116, K('rub'))
    box(v, 0, V, 5, V - 5, 104, 116, 0)
    _crenels(v, 0, V, 0, V, 110, 116, step=8, gap=4)
    box(v, 0, V, 0, V, 104, 105, K('rub_d'))
    #! 旗竿と朱の旗（風で揺れる。x の周期に合わせて 1 本）。
    frame = v
    flag = vol(h=124)
    box(frame, 14, 16, 14, 16, 104, 124, K('timber_d'))
    box(flag, 16, 27, 14, 15, 110, 122, K('cloth_tm_r'))
    box(flag, 16, 27, 14, 15, 114, 118, K('cloth_tm_y'))
    box(flag, 24, 27, 14, 15, 110, 122, K('cloth_tm_rd'))
    return [('frame', frame, (0, 0, 0)), ('flag', flag, (0, 0, 0))]


def wall_tower_tel(name):
    """**城壁の櫓** — 乱石積みの方形の塔。城壁のマスへ載せる。

    テルモラの塀は 1.4 マスと低く、そのままでは「畑を分ける囲い」に見える。
    塔を 10 マスおきに立てると、同じ塀が**城塞の周壁**として読める
    ——遠目の輪郭を作るのは壁の高さではなく、壁に並ぶ縦の要素である。
    高さ 3.3 マス（角の `watchtower_tel` の 2.9 マスより頭ひとつ高い）。
    """
    rng = rng_for(name)
    v = vol(h=106)
    _rubble(v, 0, V, 0, V, 0, 72, rng)
    box(v, 0, V, 0, V, 0, 4, K('rub_d'))                        # 裾の石垣（幅を出す）
    for z in range(20, 68, 16):                                 # 矢狭間（4 面）
        for a in (7, 21):
            box(v, a, a + 4, 0, 2, z, z + 9, K('dark'))
            box(v, a, a + 4, V - 2, V, z, z + 9, K('dark'))
            box(v, 0, 2, a, a + 4, z, z + 9, K('dark'))
            box(v, V - 2, V, a, a + 4, z, z + 9, K('dark'))
    box(v, 0, V, 0, V, 72, 76, K('rub_l'))                      # 持ち送りの帯
    box(v, 0, V, 0, V, 76, 88, K('rub'))                        # 胸壁
    box(v, 4, V - 4, 4, V - 4, 76, 88, 0)
    _crenels(v, 0, V, 0, V, 82, 88, step=8, gap=4)
    box(v, 4, V - 4, 4, V - 4, 76, 78, K('board'))              # 歩廊の板
    #! 中に立てる見張り小屋（こけら板の方形屋根）。**塔の頭を尖らせる**。
    box(v, 8, V - 8, 8, V - 8, 78, 88, K('timber'))
    box(v, 9, V - 9, 9, V - 9, 80, 86, K('dark'))
    box(v, 8, V - 8, 8, V - 8, 86, 88, K('beam'))
    _shingle_pyramid(v, 5, V - 5, 5, V - 5, 88, 14, rng)
    box(v, 15, 17, 15, 17, 100, 106, K('iron'))                 # 風見の心棒
    box(v, 15, 22, 15, 16, 102, 105, K('rust'))
    return one(v)


def belfry_tel(name):
    """**鐘楼** — 町でいちばん高い塔（4.7 マス）。鐘は振り子で揺れる。

    寺院と生命魔術の塔の脇に立てる。乱石積みの塔身に木の鐘楼を載せ、
    こけら板の尖った屋根で締める。**尖塔は町の輪郭に縦の線を 1 本入れる**もので、
    低い藁屋根が続くテルモラでは、これが遠目でいちばん効く。
    """
    rng = rng_for(name)
    frame = vol(h=168)
    box(frame, 2, 30, 2, 30, 0, 7, K('rub_d'))                  # 基壇
    box(frame, 2, 30, 2, 30, 6, 7, K('rub_l'))
    _rubble(frame, 5, 27, 5, 27, 7, 88, rng, course=8)
    for (x, y) in ((5, 5), (24, 5), (5, 24), (24, 24)):         # 角の隅石（明るい石）
        box(frame, x, x + 3, y, y + 3, 7, 88, K('rub_l'))
        for z in range(7, 88, 12):
            box(frame, x, x + 3, y, y + 3, z, z + 6, K('rub'))
    box(frame, 4, 28, 4, 28, 44, 47, K('rub_l'))                # 中ほどの胴蛇腹
    box(frame, 4, 28, 4, 28, 44, 45, K('rub_d'))
    for z in (24, 58, 74):                                      # 縦長の窓（4 面）
        box(frame, 14, 18, 4, 7, z, z + 12, K('dark'))
        box(frame, 14, 18, 25, 28, z, z + 12, K('dark'))
        box(frame, 4, 7, 14, 18, z, z + 12, K('dark'))
        box(frame, 25, 28, 14, 18, z, z + 12, K('dark'))
        box(frame, 13, 19, 4, 7, z + 12, z + 14, K('rub_l'))    # 窓のアーチ（明るい石）
        box(frame, 13, 19, 25, 28, z + 12, z + 14, K('rub_l'))
        box(frame, 4, 7, 13, 19, z + 12, z + 14, K('rub_l'))
        box(frame, 25, 28, 13, 19, z + 12, z + 14, K('rub_l'))
    box(frame, 12, 20, 25, 28, 7, 28, K('board_d'))             # 南面の扉
    box(frame, 11, 21, 25, 28, 26, 30, K('rub_l'))
    box(frame, 15, 17, 25, 28, 7, 28, K('timber_d'))
    box(frame, 3, 29, 3, 29, 88, 92, K('rub_l'))                # 鐘楼の床（張り出し）
    box(frame, 3, 29, 3, 29, 88, 89, K('rub_d'))
    #! 鐘楼（4 面とも開いた木の櫓）。**柱と鎧戸の羽板だけ**にして、中の鐘を見せる。
    for (x, y) in ((4, 4), (25, 4), (4, 25), (25, 25)):
        box(frame, x, x + 3, y, y + 3, 92, 122, K('oak'))
    for z in (92, 118):                                         # 上下の貫
        box(frame, 4, 28, 4, 7, z, z + 3, K('beam'))
        box(frame, 4, 28, 25, 28, z, z + 3, K('beam'))
        box(frame, 4, 7, 4, 28, z, z + 3, K('beam'))
        box(frame, 25, 28, 4, 28, z, z + 3, K('beam'))
    for z in range(97, 118, 5):                                 # 鎧戸の羽板（4 面）
        box(frame, 7, 25, 5, 6, z, z + 2, K('board_d'))
        box(frame, 7, 25, 26, 27, z, z + 2, K('board_d'))
        box(frame, 5, 6, 7, 25, z, z + 2, K('board_d'))
        box(frame, 26, 27, 7, 25, z, z + 2, K('board_d'))
    box(frame, 12, 20, 5, 6, 97, 118, 0)                        # 南北だけ羽板を抜いて鐘を見せる
    box(frame, 12, 20, 26, 27, 97, 118, 0)
    box(frame, 3, 29, 3, 29, 121, 124, K('beam'))               # 桁
    box(frame, 10, 22, 14, 18, 116, 119, K('timber_d'))         # 鐘を吊る梁
    top = _spire(frame, 3, 29, 3, 29, 124, 2, rng)              # 尖塔
    box(frame, 15, 17, 15, 17, top, top + 8, K('iron'))         # 十字と風見
    box(frame, 12, 20, 15, 17, top + 3, top + 5, K('iron'))
    box(frame, 17, 22, 15, 17, top + 5, top + 8, K('rust'))
    bell = vol(h=168)
    for z in range(100, 114):                                   # 鐘（下ほど広い）
        r = 2.0 + ((114 - z) * 0.5)
        disc(bell, 16, 16, r, z, z + 1, K('brass') if (z % 4) else K('brass_d'))
    disc(bell, 16, 16, 8.0, 100, 101, K('brass_d'))
    box(bell, 15, 17, 15, 17, 114, 118, K('brass_d'))           # 冠
    box(bell, 15, 17, 15, 17, 95, 100, K('brass_d'))            # 舌
    return [('frame', frame, (0, 0, 0)), ('bell', bell, (0, 0, 0))]


def belfry_parts():
    return swing_parts([('bell', (16, 16, 118))], amplitude=8.0, period=3.6)


def windmill_tel(name):
    """**風車** — 塔身は乱石積み、帽子はこけら板。**羽根が回る**（2 マス幅）。

    テルモラは「畑つきの町」（既存の草地は刈り跡と麦わら）なので、粉挽きの風車が
    その性格をそのまま言う。**回るもの**は静止画では出せない印象を作る唯一の手で、
    町の北の畑の中に 1 基だけ立てる。

    **羽根は南の面（カメラの側）へ出す**——北の面に置くと塔の影に隠れて、
    ただの石の煙突に見えた（この回の記録）。
    """
    rng = rng_for(name)
    W = V * 2
    frame = vol(h=140, w=W, d=V)
    cx, cy = W / 2.0, 13.0
    #! 塔身。半径は**段ごとに決める**（1 段ずつ細らせると縁が鋸の歯になる）。
    for band, z0 in enumerate(range(0, 96, 8)):
        r = 14.0 - (band * 0.42)
        shade = pick(rng, [K('rub'), K('rub_l'), K('rub_d')], [0.48, 0.28, 0.24])
        disc(frame, cx, cy, r, z0, z0 + 8, shade)
        disc(frame, cx, cy, r, z0, z0 + 1, K('joint'))
    disc(frame, cx, cy, 15.5, 0, 6, K('rub_d'))                 # 裾の石垣
    disc(frame, cx, cy, 15.5, 5, 6, K('rub_l'))
    box(frame, int(cx) - 5, int(cx) + 5, 22, 26, 6, 30, K('board_d'))   # 南面の戸口
    box(frame, int(cx) - 5, int(cx) - 3, 22, 26, 6, 30, K('timber_d'))
    box(frame, int(cx) + 3, int(cx) + 5, 22, 26, 6, 30, K('timber_d'))
    box(frame, int(cx) - 6, int(cx) + 6, 22, 26, 29, 32, K('timber_d'))
    for z in (44, 68):                                          # 荷積みの窓
        box(frame, int(cx) - 4, int(cx) + 4, 21, 25, z, z + 8, K('dark'))
        box(frame, int(cx) - 5, int(cx) + 5, 21, 25, z + 8, z + 10, K('timber'))
    #! 帽子（回り舞台に載る木の蓋）。**塔身より一回り大きく**して庇を出す。
    disc(frame, cx, cy, 14.4, 96, 100, K('beam'))
    disc(frame, cx, cy, 13.0, 96, 100, K('timber_d'))
    for k in range(18):
        r = 14.4 - (k * 0.75)
        shade = pick(rng, [K('shin'), K('shin_l'), K('shin_d')], [0.42, 0.30, 0.28])
        disc(frame, cx, cy, max(1.0, r), 100 + k, 101 + k, shade)
    log_y(frame, cx, 106, 3.2, 10, 29, K('timber_d'))           # 羽根の軸（南へ突き出す）
    box(frame, int(cx) - 4, int(cx) + 4, 8, 12, 100, 108, K('beam'))    # 軸受け
    #! 羽根（4 枚。木の骨に格子を組み、片側へ帆布を張る）。**回転の軸は y**。
    sail = vol(h=140, w=W, d=V)
    hub = (cx, 29.0, 106.0)
    for k in range(4):
        a = k * 1.5708
        ca, sa = float(np.cos(a)), float(np.sin(a))
        for t in range(4, 29):                                  # 腕（木の骨）
            px = int(round(cx + (t * ca)))
            pz = int(round(106 + (t * sa)))
            box(sail, px - 1, px + 2, 26, 31, pz - 1, pz + 2, K('timber_d'))
        for t in range(8, 28):                                  # 格子の桟（腕の片側へ）
            if (t % 3) != 0:
                continue
            for u in range(1, 7):
                px = int(round(cx + (t * ca) - (u * sa)))
                pz = int(round(106 + (t * sa) + (u * ca)))
                box(sail, px, px + 2, 27, 30, pz, pz + 2, K('mbr'))
        for t in range(9, 26):                                  # 帆布（外寄りの半分だけ張る）
            for u in (2, 4, 6):
                px = int(round(cx + (t * ca) - (u * sa)))
                pz = int(round(106 + (t * sa) + (u * ca)))
                shade = K('cloth_tm_w') if ((t // 5) % 2) else K('cloth_tm_wd')
                box(sail, px - 1, px + 2, 28, 30, pz - 1, pz + 2, shade)
    log_y(sail, cx, 106, 2.6, 26, 31, K('iron'))
    return [('frame', frame, (0, 0, 0)), ('sail', sail, (0, 0, 0))], hub


def windmill_build(name):
    return windmill_tel(name)[0]


def windmill_parts():
    return spin_parts('sail', (V * 2 / 2.0, 29.0, 106.0), axis='y', speed=0.30)


def arena_stand_tel(name):
    """**闘技場の観客席** — 段になった石と木の桟敷（2 マス幅）。両端に幟。

    段は北へ向かって上がり、席は**南（試合場の側）を向く**。
    段の面が横に長く並ぶので、家並みの中では珍しい「横の縞」になり、
    遠目でも闘技場の前だと分かる。
    """
    rng = rng_for(name)
    W = V * 2
    frame = vol(h=80, w=W, d=V)
    _rubble(frame, 0, W, 20, V, 0, 14, rng, course=5)           # 石の基壇（南の前面）
    for i in range(6):                                          # 段（北へ 6 段上がる）
        y0 = 20 - (i * 4)
        z0 = 14 + (i * 8)
        box(frame, 0, W, max(0, y0 - 4), y0, 0, z0 + 8, K('rub_d'))
        box(frame, 0, W, max(0, y0 - 4), y0, z0, z0 + 8, K('rub'))
        box(frame, 0, W, max(0, y0 - 4), y0, z0 + 6, z0 + 8, K('board'))   # 板の腰掛け
        for x in range(2, W - 2, 9):                            # 板の継ぎ目
            box(frame, x, x + 1, max(0, y0 - 4), y0, z0 + 6, z0 + 8, K('board_d'))
    box(frame, 0, W, 0, 4, 0, 66, K('rub_d'))                   # 背の壁
    box(frame, 0, W, 0, 3, 58, 66, K('rub_l'))
    _crenels(frame, 0, W, 0, 4, 62, 66, step=10, gap=5)
    for x in (4, 30, 33, 59):                                   # 柱（日除けを支える）
        box(frame, x, x + 3, 4, 7, 40, 74, K('oak'))
    box(frame, 0, W, 4, 8, 72, 76, K('beam'))                   # 日除けの桁
    for x in range(0, W, 10):                                   # 日除け（縞の布）
        box(frame, x, x + 5, 6, 20, 74, 76, K('cloth_tm_r'))
        box(frame, x + 5, x + 10, 6, 20, 74, 76, K('cloth_tm_w'))
    #! 幟（両端。風で揺れる）。
    for x in (1, W - 4):
        box(frame, x, x + 2, 22, 24, 14, 76, K('timber_d'))
    banner = vol(h=80, w=W, d=V)
    for x, col in ((1, K('cloth_tm_r')), (W - 4, K('cloth_tm_b'))):
        box(banner, x, x + 2, 24, 25, 44, 74, col)
        box(banner, x - 1, x + 3, 24, 25, 44, 74, col)
        box(banner, x - 1, x + 3, 24, 25, 56, 62, K('cloth_tm_y'))
    return [('frame', frame, (0, 0, 0)), ('banner', banner, (0, 0, 0))]


def arena_stand_parts():
    return wind_parts(['banner'], wind_k=0.55)


def well_house_tel(name):
    """屋根つきの共同井戸 — 乱石の井筒、木の轆轤、藁葺きの小屋根。桶が揺れる。"""
    rng = rng_for(name)
    frame = vol(h=64)
    disc(frame, 16, 16, 11.0, 0, 12, K('rub'))                  # 井筒
    for z in range(0, 12, 4):
        disc(frame, 16, 16, 11.2, z, z + 1, K('joint'))
        disc(frame, 16, 16, 11.0, z + 1, z + 4,
             pick(rng, [K('rub'), K('rub_l'), K('rub_d')], [0.46, 0.3, 0.24]))
    disc(frame, 16, 16, 11.6, 11, 13, K('rub_l'))               # 天端
    disc(frame, 16, 16, 7.5, 0, 13, K('wa_tm_dark'))            # 井戸の口
    for (x, y) in ((5, 12), (25, 12)):                          # 柱
        box(frame, x, x + 3, y, y + 8, 12, 42, K('oak'))
    log_x(frame, 16, 38, 2.6, 6, 26, K('timber'))               # 轆轤
    box(frame, 24, 30, 15, 17, 30, 40, K('timber_d'))           # 取っ手
    box(frame, 15, 17, 15, 17, 26, 38, K('lash'))               # 縄
    _thatch_cap(frame, 2, 30, 6, 26, 42, 12, rng)
    bucket = vol(h=64)
    disc(bucket, 16, 16, 3.6, 20, 27, K('timber_l'))
    disc(bucket, 16, 16, 3.8, 25, 27, K('iron'), hollow=3.0)
    disc(bucket, 16, 16, 2.6, 24, 27, K('wa'))
    box(bucket, 15, 17, 15, 17, 27, 38, K('lash'))
    return [('frame', frame, (0, 0, 0)), ('bucket', bucket, (0, 0, 0))]


def well_house_parts():
    return swing_parts([('bucket', (16, 16, 38))], amplitude=5.0, period=2.8)


def gallows_tel(name):
    """絞首台 — 石の段に丸太の 2 本柱と横木。縄が揺れる。町の外れの空き地に 1 つ。"""
    rng = rng_for(name)
    frame = vol(h=76)
    _rubble(frame, 4, 28, 8, 26, 0, 8, rng, course=4)           # 石の段
    box(frame, 4, 28, 8, 26, 8, 10, K('board_d'))               # 板の床
    for x in (6, 24):                                           # 柱
        box(frame, x, x + 4, 15, 19, 10, 64, K('timber'))
        box(frame, x - 1, x + 5, 15, 19, 10, 14, K('timber_d'))
    box(frame, 4, 28, 15, 19, 62, 68, K('timber_d'))            # 横木
    box(frame, 6, 12, 15, 19, 56, 64, K('beam'))                # 方杖
    box(frame, 20, 26, 15, 19, 56, 64, K('beam'))
    for i, y in enumerate(range(26, 32, 2)):                    # 段（南から上がる）
        box(frame, 12, 20, y, y + 2, 0, max(1, 9 - (i * 3)), K('timber_d'))
    rope = vol(h=76)
    box(rope, 15, 17, 16, 18, 44, 62, K('lash'))
    disc(rope, 16, 17, 3.0, 42, 46, K('lash'), hollow=1.6)
    return [('frame', frame, (0, 0, 0)), ('rope', rope, (0, 0, 0))]


def gallows_parts():
    return swing_parts([('rope', (16, 17, 63))], amplitude=6.0, period=3.2)


def pillory_tel(name):
    """さらし台 — 板の台に、首と手を挟む 2 枚の板。町の市場の隅に。"""
    rng = rng_for(name)
    v = vol(h=48)
    _rubble(v, 6, 26, 10, 24, 0, 6, rng, course=3)
    box(v, 5, 27, 9, 25, 6, 8, K('board'))                      # 台
    for x in range(6, 26, 5):
        box(v, x, x + 1, 9, 25, 6, 8, K('board_d'))
    for x in (10, 22):                                          # 柱
        box(v, x, x + 3, 15, 18, 8, 40, K('oak'))
    box(v, 8, 25, 15, 18, 28, 32, K('board_d'))                 # 下の板
    box(v, 8, 25, 14, 15, 32, 36, K('board'))                   # 上の板（開いて留めてある）
    box(v, 8, 25, 12, 14, 34, 38, K('board_l'))
    for x in (12, 16, 20):                                      # 首と手の穴
        box(v, x, x + 3, 15, 18, 30, 32, 0)
    box(v, 24, 26, 15, 18, 30, 34, K('iron'))                   # 蝶番とロック
    return one(v)


def notice_post_tel(name):
    """触れ書きの柱 — 石の礎に樫の柱、板に張った布告、鉄の腕木に小さな鐘。"""
    rng = rng_for(name)
    v = vol(h=64)
    _rubble(v, 10, 22, 10, 22, 0, 8, rng, course=4)
    box(v, 13, 19, 13, 19, 8, 52, K('oak'))
    box(v, 13, 19, 13, 19, 50, 52, K('oak_l'))
    box(v, 6, 26, 19, 21, 22, 44, K('board'))                   # 南面の掲示板
    box(v, 6, 26, 19, 21, 22, 24, K('timber_d'))
    box(v, 6, 26, 19, 21, 42, 44, K('timber_d'))
    for (x, z, w, h) in ((8, 26, 6, 10), (16, 30, 8, 8), (9, 36, 5, 6)):
        box(v, x, x + w, 20, 21, z, z + h, K('cloth_tm_w'))     # 張られた布告
        box(v, x, x + w, 20, 21, z, z + 1, K('cloth_tm_wd'))
    box(v, 19, 28, 15, 17, 48, 50, K('iron'))                   # 腕木
    box(v, 26, 28, 15, 17, 42, 48, K('iron'))
    disc(v, 27, 16, 3.2, 38, 44, K('brass'))
    disc(v, 27, 16, 3.4, 38, 39, K('brass_d'))
    return one(v)


def bridge_rail_tel(name):
    """石橋の欄干 — 甲板の北と南の辺へ低い石の壁。橋のマスに重ねる。"""
    rng = rng_for(name)
    v = vol(h=24)
    for (y0, y1) in ((0, 4), (V - 4, V)):
        _rubble(v, 0, V, y0, y1, 0, 16, rng, course=5)
        box(v, 0, V, y0, y1, 16, 18, K('rub_l'))                # 笠石
        for x in range(3, V - 3, 8):                            # 彫った小さな窓
            box(v, x, x + 3, y0 + 1, y1 - 1, 8, 14, 0)
    for x in (0, V - 4):                                        # 橋端の親柱
        box(v, x, x + 4, 0, 4, 0, 22, K('rub_d'))
        box(v, x, x + 4, V - 4, V, 0, 22, K('rub_d'))
        box(v, x, x + 4, 0, 4, 20, 22, K('rub_l'))
        box(v, x, x + 4, V - 4, V, 20, 22, K('rub_l'))
    return one(v)


def guard_post_tel(name):
    """衛兵の詰め所 — 板張りの小屋に槍立てと篝火受け。門の内側に。"""
    rng = rng_for(name)
    v = vol(h=48)
    _rubble(v, 4, 26, 6, 24, 0, 5, rng, course=3)
    box(v, 5, 25, 7, 23, 5, 32, K('board'))                     # 板壁
    for x in range(5, 25, 4):
        box(v, x, x + 1, 7, 23, 5, 32, K('board_d'))
    box(v, 5, 25, 7, 9, 5, 32, K('beam'))
    box(v, 5, 25, 21, 23, 5, 32, K('beam'))
    box(v, 9, 21, 21, 24, 6, 28, K('dark'))                     # 南の口（中は暗い）
    box(v, 8, 22, 21, 24, 27, 29, K('timber_d'))
    _shingle_pyramid(v, 2, 28, 4, 26, 32, 10, rng)
    box(v, 25, 28, 12, 18, 5, 26, K('timber_d'))                # 脇の槍立て
    for y in (13, 15, 17):
        box(v, 26, 27, y, y + 1, 5, 34, K('timber'))
        box(v, 26, 27, y, y + 1, 32, 34, K('steel_tm_l'))
    return one(v)


# ------------------------------------------------------------------ 通りと生活
def crate_tel(name):
    """木箱の積み — 板の箱を 3 つ。**箱ごとに 1 色**で、縁だけ暗く締める。"""
    rng = rng_for(name)
    v = vol(h=32)
    for (x, y, z, w, d, h) in ((4, 6, 0, 13, 12, 11), (6, 17, 0, 12, 11, 10), (17, 9, 0, 11, 11, 9)):
        shade = pick(rng, [K('board'), K('board_l'), K('board_d')], [0.44, 0.30, 0.26])
        box(v, x, x + w, y, y + d, z, z + h, shade)
        box(v, x, x + w, y, y + d, z + h - 1, z + h, K('board_l'))
        box(v, x, x + 1, y, y + d, z, z + h, K('timber_d'))
        box(v, x + w - 1, x + w, y, y + d, z, z + h, K('timber_d'))
        box(v, x, x + w, y, y + d, z + h // 2, z + h // 2 + 1, K('timber_d'))
    box(v, 6, 17, 8, 19, 11, 19, K('board_d'))                  # 上に 1 つ載せる
    box(v, 6, 17, 8, 19, 18, 19, K('board'))
    return one(v)


def barrel_tel(name):
    """樽 — 3 つ。胴を膨らませ、鉄の箍を 2 本。1 つは横に転がしてある。"""
    rng = rng_for(name)
    v = vol(h=32)
    for (cx, cy, h) in ((9, 9, 18), (21, 12, 16)):
        for z in range(h):
            r = 5.4 + (1.2 * np.sin(np.pi * z / max(1, h - 1)))
            disc(v, cx, cy, r, z, z + 1, K('timber_l') if (z % 5) else K('timber'))
        disc(v, cx, cy, 6.6, 4, 6, K('iron'), hollow=5.0)
        disc(v, cx, cy, 6.6, h - 6, h - 4, K('iron'), hollow=5.0)
        disc(v, cx, cy, 5.0, h - 1, h, K('timber_d'))
    log_x(v, 24, 6, 6.0, 6, 22, K('timber'))                    # 横に転がした 1 つ
    log_x(v, 24, 6, 6.2, 8, 10, K('iron'))
    log_x(v, 24, 6, 6.2, 18, 20, K('iron'))
    box(v, 5, 7, 18, 30, 0, 12, K('timber_d'))
    return one(v)


def sack_tel(name):
    """麻袋 — 口を縛った袋を 4 つ。**丸で作って口だけ角ばらせる**。"""
    rng = rng_for(name)
    v = vol(h=28)
    for (cx, cy, r) in ((9, 10, 6.0), (19, 8, 5.4), (14, 20, 5.8), (24, 18, 5.0)):
        h = int(r * 2.2)
        ball(v, cx, cy, r * 0.9, r, K('cloth_d') if rng.random() < 0.5 else K('cloth'), squash=0.9)
        box(v, int(cx) - 2, int(cx) + 2, int(cy) - 2, int(cy) + 2, h - 4, h + 2, K('cloth_d'))
        box(v, int(cx) - 1, int(cx) + 1, int(cy) - 1, int(cy) + 1, h + 1, h + 3, K('lash'))
    return one(v)


def handcart_tel(name):
    """手押し荷車 — 板の荷台に 1 輪、柄を南へ。中に麦の袋。"""
    v = vol(h=32)
    box(v, 6, 26, 10, 22, 10, 12, K('board'))                   # 荷台
    for x in range(7, 26, 4):
        box(v, x, x + 1, 10, 22, 10, 12, K('board_d'))
    box(v, 6, 26, 10, 12, 12, 20, K('board_d'))                 # あおり
    box(v, 6, 26, 20, 22, 12, 20, K('board_d'))
    box(v, 6, 8, 10, 22, 12, 20, K('board_d'))
    _cart_wheel(v, 8, 8, 8.0, 15, 18)
    box(v, 20, 30, 12, 14, 12, 14, K('timber'))                 # 柄
    box(v, 20, 30, 18, 20, 12, 14, K('timber'))
    box(v, 28, 30, 12, 20, 12, 14, K('timber_d'))
    box(v, 24, 26, 12, 14, 0, 13, K('timber_d'))                # 支え脚（接地する）
    box(v, 24, 26, 18, 20, 0, 13, K('timber_d'))
    ball(v, 14, 16, 16, 5.0, K('cloth'), squash=0.85)           # 麦の袋
    box(v, 12, 16, 14, 18, 19, 22, K('cloth_d'))
    return one(v)


def wagon_horse_tel(name):
    """荷馬車と馬 — 4 輪の荷車に麦の袋と樽、前に馬 1 頭（東を向く）。

    馬車と馬を 1 マスに収めるので、**荷台は西半分・馬は東半分**へ寄せる。
    """
    rng = rng_for(name)
    v = vol(h=36)
    box(v, 1, 18, 8, 24, 9, 12, K('board'))                     # 荷台
    for x in range(2, 18, 4):
        box(v, x, x + 1, 8, 24, 9, 12, K('board_d'))
    box(v, 1, 18, 8, 9, 12, 20, K('timber_d'))                  # あおり
    box(v, 1, 18, 23, 24, 12, 20, K('timber_d'))
    box(v, 1, 2, 8, 24, 12, 20, K('timber_d'))
    box(v, 3, 16, 10, 22, 12, 17, K('hay'))                     # 積んだ干し草
    box(v, 3, 16, 10, 22, 16, 17, K('hay_d'))
    for _ in range(int(rng.integers(1, 3))):                    # 載せた樽
        cx = int(rng.integers(4, 14))
        for z in range(17, 27):
            disc(v, cx, 16, 4.2, z, z + 1, K('timber_l') if (z % 4) else K('timber'))
        disc(v, cx, 16, 4.4, 19, 21, K('iron'), hollow=3.2)
    _cart_wheel(v, 4, 6, 6.0, 6, 9)
    _cart_wheel(v, 4, 6, 6.0, 23, 26)
    _cart_wheel(v, 15, 7, 6.8, 6, 9)
    _cart_wheel(v, 15, 7, 6.8, 23, 26)
    box(v, 17, 23, 15, 17, 10, 12, K('timber'))                 # 轅（ながえ）
    _horse_east(v)
    return one(v)


def hitch_rail_tel(name):
    """馬つなぎの横木 — 杭 2 本に丸太の横木、繋いだ縄と落ちた藁。"""
    v = vol(h=32)
    for x in (4, 26):
        box(v, x, x + 4, 14, 18, 0, 22, K('timber'))
        box(v, x, x + 4, 14, 18, 20, 22, K('timber_d'))
    log_x(v, 16, 19, 2.6, 3, 29, K('timber_l'))
    box(v, 10, 12, 16, 18, 10, 19, K('lash'))
    box(v, 20, 22, 16, 18, 12, 19, K('lash'))
    box(v, 6, 14, 20, 26, 0, 2, K('hay_d'))
    box(v, 18, 26, 8, 13, 0, 2, K('straw_d'))
    return one(v)


def trough_tel(name):
    """石の水槽 — 刳り抜いた乱石の桶に水。**縁を先に立てて内を刳ってから水を張る**

    （水を先に置くと縁が水の天面を上書きして白い箱に見える。モリバントの記録）。
    """
    rng = rng_for(name)
    v = vol(h=24)
    _rubble(v, 3, 29, 10, 24, 0, 14, rng, course=5)
    box(v, 5, 27, 12, 22, 5, 14, 0)                             # 内を刳る
    box(v, 5, 27, 12, 22, 5, 12, K('wa'))                       # 水
    box(v, 5, 27, 12, 22, 11, 12, K('wa_l'))
    box(v, 3, 29, 10, 24, 13, 14, K('rub_l'))                   # 縁の天端
    box(v, 5, 27, 12, 22, 13, 14, 0)
    box(v, 3, 29, 10, 24, 0, 3, K('rub_d'))
    box(v, 8, 12, 8, 11, 0, 4, K('moss_d'))                     # 根元の苔
    return one(v)


def woodpile_tel(name):
    """薪の山 — 割った薪を**南北に寝かせて**積む。木口が南を向くので、
    見下ろすカメラでは丸い切り口の並びがそのまま絵になる。両端に崩れ止めの杭。
    """
    rng = rng_for(name)
    v = vol(h=28)
    for row, z in enumerate(range(0, 22, 5)):
        off = 2 if (row % 2) else 0
        for x in range(4 + off, 28, 5):
            if rng.random() < 0.92:
                shade = pick(rng, [K('log'), K('log_l'), K('log_d')], [0.44, 0.3, 0.26])
                log_y(v, x, z + 3, 2.6, 6, 27, shade)
                log_y(v, x, z + 3, 2.6, 25, 27, K('log_end'))   # 南の木口
                log_y(v, x, z + 3, 1.2, 25, 27, K('log_d'))     # 芯の割れ
    box(v, 1, 3, 6, 27, 0, 24, K('timber_d'))                   # 崩れ止めの杭
    box(v, 29, 31, 6, 27, 0, 24, K('timber_d'))
    box(v, 1, 31, 6, 27, 0, 1, K('pack_d'))                     # 下に敷いた土
    return one(v)


def haystack_tel(name):
    """干し草の山 — 心棒に巻いた円錐の山。天は雨仕舞いに藁を掛けてある。"""
    rng = rng_for(name)
    v = vol(h=40)
    for z in range(0, 32):
        r = 13.5 - (z * 0.30) if z < 22 else 13.5 - (22 * 0.30) - ((z - 22) * 0.85)
        shade = pick(rng, [K('hay'), K('straw'), K('straw_d'), K('hay_d')], [0.4, 0.26, 0.2, 0.14])
        disc(v, 16, 16, max(0.8, r), z, z + 1, shade)
    box(v, 15, 17, 15, 17, 30, 38, K('timber_d'))               # 心棒
    for a in (0.4, 2.5, 4.6):                                   # 押さえの縄
        x = int(16 + (11 * np.cos(a)))
        y = int(16 + (11 * np.sin(a)))
        box(v, x, x + 2, y, y + 2, 4, 28, K('lash'))
    return one(v)


def dung_tel(name):
    """馬糞と藁 — 通りに落ちたもの。**小さく低く**（大きくすると土の塊に見える）。"""
    rng = rng_for(name)
    v = vol(h=8)
    for _ in range(int(rng.integers(2, 5))):
        cx, cy = int(rng.integers(6, 26)), int(rng.integers(6, 26))
        ball(v, cx, cy, 0, 3.4, K('soil_tm_dung'), squash=0.5)
        box(v, cx - 2, cx + 2, cy - 2, cy + 2, 2, 3, K('dirt_d'))
    for _ in range(int(rng.integers(3, 6))):
        cx, cy = int(rng.integers(2, 28)), int(rng.integers(2, 30))
        box(v, cx, cx + int(rng.integers(3, 7)), cy, cy + 1, 0, 1, K('straw_d'))
    return one(v)


def laundry_tel(name):
    """通りをまたぐ洗濯物 — 2 本の柱に縄、シーツと肌着が揺れる（振り子）。"""
    rng = rng_for(name)
    frame = vol(h=48)
    for x in (2, 27):
        box(frame, x, x + 3, 14, 17, 0, 40, K('timber'))
        box(frame, x, x + 3, 14, 17, 38, 40, K('timber_d'))
    box(frame, 2, 30, 15, 16, 37, 38, K('lash'))
    cloth = vol(h=48)
    for (x, w, col) in ((5, 8, K('cloth_tm_w')), (15, 6, K('cloth_tm_b')), (23, 5, K('cloth_tm_y'))):
        h = int(rng.integers(12, 20))
        box(cloth, x, x + w, 14, 16, 37 - h, 38, col)
        box(cloth, x, x + w, 14, 16, 37 - h, 38 - h + 2, K('cloth_tm_wd'))
    return [('frame', frame, (0, 0, 0)), ('cloth', cloth, (0, 0, 0))]


def laundry_parts():
    return swing_parts([('cloth', (16, 15, 38))], amplitude=7.0, period=2.4)


def bench_tel(name):
    """木の腰掛け — 板 1 枚に太い脚。座面の板目だけで木に読ませる。"""
    v = vol(h=16)
    box(v, 3, 29, 12, 21, 8, 11, K('board'))
    for x in range(4, 29, 5):
        box(v, x, x + 1, 12, 21, 8, 11, K('board_d'))
    box(v, 3, 29, 12, 13, 8, 11, K('timber_d'))
    box(v, 3, 29, 20, 21, 8, 11, K('timber_d'))
    for x in (5, 24):
        box(v, x, x + 4, 13, 20, 0, 8, K('timber'))
        box(v, x, x + 4, 13, 20, 0, 2, K('timber_d'))
    return one(v)


def planter_tel(name):
    """木の花桶 — 板を箍で締めた桶に土と花。半分に割った樽をそのまま使ったもの。"""
    rng = rng_for(name)
    v = vol(h=24)
    disc(v, 16, 16, 10.0, 0, 12, K('timber'))
    _staves(v, 0, 12, K('timber_d'))
    disc(v, 16, 16, 10.4, 2, 4, K('iron'), hollow=9.0)
    disc(v, 16, 16, 10.4, 9, 11, K('iron'), hollow=9.0)
    disc(v, 16, 16, 8.4, 0, 12, 0)
    disc(v, 16, 16, 8.4, 0, 11, K('dirt_tm_bed'))
    for _ in range(int(rng.integers(4, 8))):
        cx, cy = int(rng.integers(10, 22)), int(rng.integers(10, 22))
        box(v, cx, cx + 3, cy, cy + 3, 11, 15, K('grass_tm_veg'))
        if rng.random() < 0.55:
            box(v, cx, cx + 2, cy, cy + 2, 15, 17,
                pick(rng, [K('fl_w'), K('fl_y'), K('fl_r')], [0.4, 0.3, 0.3]))
    return one(v)


def chicken_tel(name):
    """鶏 3 羽 — 胴を暗く、翼帯を入れ、頭だけ明るく。**明るい灰一色にしない**

    （上から見て白い紙くずにしか見えなくなる。モリバントの記録）。
    """
    rng = rng_for(name)
    v = vol(h=16)
    for (cx, cy, face) in ((8, 10, 1), (19, 8, -1), (14, 22, 1)):
        ball(v, cx, cy, 5, 3.4, K('fur_tm_hend'), squash=0.85)
        box(v, cx - 2, cx + 2, cy - 3, cy + 3, 5, 7, K('fur_tm_hen'))
        n2, n4, n5 = (cy + (2 * face)), (cy + (4 * face)), (cy + (5 * face))
        n3 = cy + (3 * face)
        box(v, cx - 1, cx + 1, min(n2, n4), max(n2, n4), 6, 11, K('fur_tm_hen'))   # 首
        box(v, cx - 1, cx + 1, min(n3, n5), max(n3, n5), 9, 12, K('fur_tm_hend'))  # 頭
        box(v, cx - 1, cx + 1, min(n4, n5), max(n4, n5), 10, 11, K('fl_y'))        # 嘴
        box(v, cx - 1, cx + 1, min(n3, n4), max(n3, n4), 11, 13, K('fl_r'))        # 鶏冠
        for dx in (-1, 1):
            box(v, cx + dx, cx + dx + 1, cy - 1, cy + 1, 0, 3, K('fl_y'))
    for _ in range(int(rng.integers(2, 5))):
        cx, cy = int(rng.integers(2, 28)), int(rng.integers(2, 30))
        box(v, cx, cx + 3, cy, cy + 1, 0, 1, K('straw_d'))
    return one(v)


def pig_tel(name):
    """豚 — 泥に寝そべった 1 頭。胴を長く低く、鼻と耳と巻き尾で読ませる。"""
    v = vol(h=20)
    box(v, 2, 30, 4, 28, 0, 2, K('wa_tm_mud'))                  # 泥だまり
    box(v, 6, 26, 8, 24, 0, 2, K('wa_tm_dark'))
    box(v, 8, 24, 11, 21, 2, 11, K('fur_tm_pig'))               # 胴
    box(v, 8, 24, 11, 21, 9, 11, K('cloth_d'))
    box(v, 5, 10, 13, 19, 3, 10, K('fur_tm_pig'))               # 頭
    box(v, 3, 6, 14, 18, 4, 8, K('fl_r'))                       # 鼻
    box(v, 6, 8, 12, 14, 9, 12, K('fur_tm_pig'))                # 耳
    box(v, 6, 8, 18, 20, 9, 12, K('fur_tm_pig'))
    box(v, 24, 27, 15, 17, 7, 9, K('fur_tm_pig'))               # 尾
    box(v, 26, 27, 15, 16, 8, 11, K('fur_tm_pig'))
    for dx in (10, 20):                                         # 脚（伏せているので短い）
        box(v, dx, dx + 3, 9, 11, 2, 4, K('fur_tm_pig'))
        box(v, dx, dx + 3, 21, 23, 2, 4, K('fur_tm_pig'))
    return one(v)


def cat_tel(name):
    """猫 — 塀の際で丸くなっている。**耳と尾**が無いと毛皮の袋に見える。"""
    v = vol(h=16)
    ball(v, 16, 17, 4, 5.2, K('fur_tm_cat'), squash=0.7)
    ball(v, 11, 15, 6, 3.4, K('fur_tm_cat'))                    # 頭
    box(v, 9, 12, 13, 15, 8, 11, K('fur_tm_cat'))               # 耳
    box(v, 9, 12, 16, 18, 8, 11, K('fur_tm_cat'))
    box(v, 9, 11, 14, 15, 6, 7, K('fl_y'))                      # 目
    box(v, 9, 11, 17, 18, 6, 7, K('fl_y'))
    box(v, 18, 25, 20, 22, 2, 4, K('fur_tm_catl'))              # 尾（体に巻きつく）
    box(v, 23, 25, 14, 22, 2, 4, K('fur_tm_catl'))
    box(v, 13, 20, 12, 20, 6, 8, K('fur_tm_catl'))              # 背の明るい毛
    return one(v)


def beehive_tel(name):
    """藁の蜂の巣（skep）— 巻いた藁の釣鐘。台の上に 2 つ、周りに飛ぶ蜂。"""
    rng = rng_for(name)
    v = vol(h=32)
    box(v, 4, 28, 10, 24, 0, 5, K('timber_d'))                  # 台
    box(v, 4, 28, 10, 24, 4, 5, K('board'))
    for (cx, cy, h) in ((9, 15, 16), (23, 18, 12)):
        for z in range(h):
            r = 6.4 - (z * (5.2 / h))
            shade = pick(rng, [K('straw'), K('straw_d'), K('hay')], [0.44, 0.3, 0.26])
            disc(v, cx, cy, max(1.0, r), 5 + z, 6 + z, shade)
            if (z % 3) == 0:
                disc(v, cx, cy, max(1.0, r), 5 + z, 6 + z, K('straw_g'))
        box(v, cx - 3, cx + 3, cy + 5, cy + 7, 6, 9, K('dark'))  # 出入口
    for _ in range(int(rng.integers(3, 6))):                    # 飛ぶ蜂
        cx, cy = int(rng.integers(4, 28)), int(rng.integers(4, 28))
        box(v, cx, cx + 2, cy, cy + 1, int(rng.integers(20, 28)), int(rng.integers(21, 29)), K('fl_y'))
    return one(v)


def scarecrow_tel(name):
    """案山子 — 十字に組んだ棒に古着、頭は麻袋、鴉が肩に止まっている。"""
    rng = rng_for(name)
    v = vol(h=56)
    box(v, 15, 18, 15, 18, 0, 44, K('timber'))                  # 心棒
    box(v, 4, 29, 15, 17, 30, 33, K('timber_d'))                # 横木
    box(v, 10, 23, 13, 20, 20, 36, K('cloth_tm_bd'))            # 胴の古着
    box(v, 10, 23, 13, 20, 34, 36, K('cloth_tm_b'))
    box(v, 5, 11, 14, 18, 28, 33, K('cloth_tm_rd'))             # 袖
    box(v, 22, 28, 14, 18, 28, 33, K('cloth_tm_rd'))
    for _ in range(4):                                          # 裾のほつれ
        x = int(rng.integers(10, 22))
        box(v, x, x + 2, 13, 20, 17, 21, K('rag'))
    ball(v, 16, 16, 41, 5.2, K('cloth'), squash=0.95)           # 麻袋の頭
    box(v, 12, 21, 14, 18, 36, 38, K('lash'))
    box(v, 13, 15, 12, 14, 41, 43, K('ink'))                    # 目
    box(v, 18, 20, 12, 14, 41, 43, K('ink'))
    box(v, 8, 24, 13, 19, 45, 47, K('straw'))                   # 麦わら帽
    box(v, 12, 20, 14, 18, 46, 50, K('straw_d'))
    box(v, 24, 28, 15, 18, 33, 39, K('ink'))                    # 肩の鴉
    box(v, 26, 29, 16, 17, 36, 38, K('dark'))
    box(v, 23, 25, 15, 18, 34, 36, K('dark'))
    return one(v)


def alley_junk_tel(name):
    """路地のがらくた — 折れた樽、割れた壺、板切れ、捨てられた籠。"""
    rng = rng_for(name)
    v = vol(h=24)
    log_x(v, 8, 5, 5.4, 4, 16, K('timber_d'))                   # 折れた樽
    box(v, 12, 16, 3, 13, 0, 10, 0)
    log_x(v, 8, 5, 5.6, 5, 7, K('rust'))
    disc(v, 23, 9, 5.0, 0, 8, K('clay'))                        # 割れた壺
    disc(v, 23, 9, 3.4, 3, 8, 0)
    box(v, 23, 28, 5, 12, 5, 8, 0)
    for _ in range(int(rng.integers(3, 6))):                    # 板切れ
        x, y = int(rng.integers(2, 22)), int(rng.integers(14, 28))
        box(v, x, x + int(rng.integers(6, 12)), y, y + 3, 0, 2,
            pick(rng, [K('board_d'), K('board'), K('timber_d')], [0.4, 0.32, 0.28]))
    disc(v, 10, 24, 5.4, 0, 7, K('mbr'))                        # 捨てられた籠
    disc(v, 10, 24, 4.0, 1, 7, 0)
    box(v, 20, 30, 20, 30, 0, 1, K('rag'))
    return one(v)


def ivy_tel(name):
    """壁の蔦 — 北の面へ這わせる。**葉は塊で置く**（1 ボクセルずつでは苔に見える）。"""
    rng = rng_for(name)
    frame = vol(h=40)
    box(frame, 0, V, 0, 2, 0, 34, K('leaf_tm_ivyd'))
    for _ in range(int(rng.integers(12, 20))):
        x = int(rng.integers(0, V - 4))
        z = int(rng.integers(0, 32))
        w, h = int(rng.integers(3, 7)), int(rng.integers(3, 7))
        box(frame, x, x + w, 1, 3, z, z + h,
            K('leaf_tm_ivy') if rng.random() < 0.62 else K('leaf_tm_ivyd'))
    for x in range(2, V - 2, 7):                                # 蔓
        box(frame, x, x + 1, 1, 2, 0, 34, K('bark'))
    leaf = vol(h=40)
    for _ in range(int(rng.integers(5, 9))):                    # 風に揺れる先端
        x = int(rng.integers(0, V - 4))
        z = int(rng.integers(20, 34))
        box(leaf, x, x + 4, 2, 4, z, z + 5, K('leaf_tm_ivy'))
    return [('frame', frame, (0, 0, 0)), ('leaf', leaf, (0, 0, 0))]


def ivy_parts():
    return wind_parts(['leaf'], wind_k=0.4)


def herb_bed_tel(name):
    """薬草の畝 — 低い板の枠に黒土、薬草と花が並ぶ。"""
    rng = rng_for(name)
    v = vol(h=20)
    box(v, 2, 30, 4, 28, 0, 5, K('board_d'))                    # 枠
    box(v, 4, 28, 6, 26, 0, 6, K('dirt_tm_bed'))
    box(v, 4, 28, 6, 26, 5, 6, K('dirt_tm_bedl'))
    for y in range(7, 26, 5):
        for x in range(5, 27, 5):
            if rng.random() < 0.82:
                box(v, x, x + 4, y, y + 4, 6, 9, K('grass_tm_herb'))
                box(v, x + 1, x + 3, y + 1, y + 3, 9, 12, K('grass_tm_veg'))
                if rng.random() < 0.35:
                    box(v, x + 1, x + 3, y + 1, y + 3, 12, 14,
                        pick(rng, [K('fl_w'), K('fl_y')], [0.6, 0.4]))
    return one(v)


def bean_poles_tel(name):
    """豆の支柱 — 3 本ずつ組んだ棒に蔓が巻きつく。畑の縦の要素。"""
    rng = rng_for(name)
    v = vol(h=44)
    for (cx, cy) in ((9, 12), (22, 18)):
        for a in (0.0, 2.09, 4.19):
            tx = int(cx + (5 * np.cos(a)))
            ty = int(cy + (5 * np.sin(a)))
            for t in range(34):
                px = int(round(tx + ((cx - tx) * t / 34.0)))
                py = int(round(ty + ((cy - ty) * t / 34.0)))
                box(v, px, px + 2, py, py + 2, t, t + 1, K('mbr'))
                if (t % 4) == 0 and rng.random() < 0.7:
                    box(v, px - 1, px + 3, py - 1, py + 3, t, t + 2, K('grass_tm_veg'))
        box(v, cx - 1, cx + 2, cy - 1, cy + 2, 32, 38, K('grass_tm_vegl'))
    return one(v)


def compost_tel(name):
    """堆肥の山 — 板の囲いに藁と黒土と野菜くず。湯気は出さない。"""
    rng = rng_for(name)
    v = vol(h=24)
    for (x0, x1, y0, y1) in ((3, 29, 6, 8), (3, 29, 24, 26), (3, 5, 6, 26), (27, 29, 6, 26)):
        box(v, x0, x1, y0, y1, 0, 14, K('board_d'))
        box(v, x0, x1, y0, y1, 12, 14, K('board'))
    for z in range(0, 12):
        r = 12.0 - (z * 0.55)
        disc(v, 16, 16, r, z, z + 1,
             pick(rng, [K('dirt_tm_bed'), K('straw_d'), K('hay_d'), K('soil_tm_dung')],
                  [0.36, 0.26, 0.22, 0.16]))
    for _ in range(int(rng.integers(3, 6))):
        cx, cy = int(rng.integers(8, 24)), int(rng.integers(8, 24))
        box(v, cx, cx + 3, cy, cy + 3, 11, 13, K('grass_tm_veg'))
    return one(v)


def reeds_tel(name):
    """堀の縁の葦 — 束で立てる。水際の輪郭を柔らかくする。"""
    rng = rng_for(name)
    frame = vol(h=40)
    box(frame, 0, V, 0, V, 0, 1, K('silt'))
    stalk = vol(h=40)
    for _ in range(int(rng.integers(14, 22))):
        cx, cy = int(rng.integers(1, 30)), int(rng.integers(1, 30))
        h = int(rng.integers(16, 34))
        shade = pick(rng, [K('stalk'), K('stalk_d'), K('grass_tm_herb')], [0.4, 0.34, 0.26])
        box(stalk, cx, cx + 2, cy, cy + 2, 0, h, shade)
        if rng.random() < 0.4:
            box(stalk, cx, cx + 2, cy, cy + 2, h, h + 4, K('mbr_d'))
    return [('frame', frame, (0, 0, 0)), ('stalk', stalk, (0, 0, 0))]


def reeds_parts():
    return wind_parts(['stalk'], wind_k=0.7)


def apple_tree_tel(name):
    """果樹（林檎・梨）— 幹を低く、枝を横へ張る。実は塊で置く。"""
    rng = rng_for(name)
    fruit = K('leaf_tm_apple') if name.endswith('01') else K('leaf_tm_pear')
    frame = vol(h=68)
    box(frame, 13, 19, 13, 19, 0, 26, K('bark'))                # 幹
    box(frame, 12, 20, 12, 20, 0, 4, K('bark_l'))
    for a in (0.5, 2.0, 3.6, 5.1):                              # 枝
        for t in range(4, 13):
            px = int(round(16 + (t * np.cos(a))))
            py = int(round(16 + (t * np.sin(a))))
            box(frame, px - 1, px + 2, py - 1, py + 2, 24 + t, 26 + t, K('bark'))
    leaf = vol(h=68)
    for _ in range(int(rng.integers(16, 24))):                  # 樹冠（塊で置く）
        cx, cy = int(rng.integers(3, 26)), int(rng.integers(3, 26))
        cz = int(rng.integers(36, 56))
        r = float(rng.integers(4, 8))
        ball(leaf, cx, cy, cz, r, pick(rng, [K('leaf_m'), K('leaf_d'), K('leaf_l')],
                                       [0.44, 0.32, 0.24]), squash=0.8)
    for _ in range(int(rng.integers(5, 10))):                   # 実
        cx, cy = int(rng.integers(5, 26)), int(rng.integers(5, 26))
        cz = int(rng.integers(36, 54))
        box(leaf, cx, cx + 3, cy, cy + 3, cz, cz + 3, fruit)
    return [('frame', frame, (0, 0, 0)), ('leaf', leaf, (0, 0, 0))]


def apple_tree_parts():
    return wind_parts(['leaf'], wind_k=0.5)


def grave_cross_tel(name):
    """墓標 — 木の十字架（`_01`）と傾いた石板（`_02`）。根方に草と花。"""
    rng = rng_for(name)
    v = vol(h=40)
    if name.endswith('01'):
        box(v, 14, 18, 15, 18, 0, 30, K('timber_d'))            # 木の十字架
        box(v, 8, 24, 15, 18, 22, 26, K('timber_d'))
        box(v, 14, 18, 15, 18, 28, 30, K('timber'))
        box(v, 10, 22, 15, 18, 22, 23, K('timber'))
        box(v, 6, 26, 8, 24, 0, 2, K('dirt_tm_bed'))            # 土盛り
    else:
        for z in range(0, 26):                                  # 傾いた石板
            d = z // 7
            box(v, 10 + d, 22 + d, 14, 18, z, z + 1,
                K('rub') if (z % 5) else K('rub_l'))
        box(v, 13, 20, 13, 14, 8, 20, K('joint'))               # 彫った銘
        box(v, 10, 15, 14, 18, 0, 6, K('moss_d'))
    for _ in range(int(rng.integers(2, 5))):
        x, y = int(rng.integers(4, 26)), int(rng.integers(6, 26))
        box(v, x, x + 3, y, y + 2, 0, 4, K('turf'))
        if rng.random() < 0.4:
            box(v, x + 1, x + 2, y, y + 1, 4, 6, K('fl_w'))
    return one(v)


def tomb_slab_tel(name):
    """石棺の蓋 — 地に伏せた乱石の板。彫った銘と縁、隅に苔。"""
    rng = rng_for(name)
    v = vol(h=16)
    box(v, 1, 31, 3, 29, 0, 5, K('rub_d'))
    _rubble(v, 2, 30, 4, 28, 5, 10, rng, course=4)
    box(v, 3, 29, 5, 27, 9, 10, K('rub_l'))
    box(v, 4, 28, 6, 8, 9, 10, K('joint'))                      # 彫った縁
    box(v, 4, 28, 24, 26, 9, 10, K('joint'))
    box(v, 12, 20, 9, 23, 9, 10, K('joint'))                    # 彫った十字の銘
    box(v, 8, 24, 14, 18, 9, 10, K('joint'))
    for _ in range(int(rng.integers(2, 5))):
        x, y = int(rng.integers(2, 26)), int(rng.integers(4, 24))
        box(v, x, x + 5, y, y + 4, 9, 10, K('moss_d'))
    return one(v)


def market_stall_tel(name):
    """市の屋台 — 4 本の柱に布の日除け、板の台に商品。3 種（魚・青物・布地）。"""
    rng = rng_for(name)
    which = int(name[-1]) if name[-1].isdigit() else 1
    frame = vol(h=56)
    for (x, y) in ((2, 4), (27, 4), (2, 24), (27, 24)):         # 柱
        box(frame, x, x + 3, y, y + 3, 0, 40, K('timber'))
    box(frame, 2, 30, 4, 28, 38, 40, K('timber_d'))             # 桁
    box(frame, 3, 29, 16, 28, 16, 19, K('board'))               # 台（南寄り）
    for x in range(4, 29, 5):
        box(frame, x, x + 1, 16, 28, 16, 19, K('board_d'))
    box(frame, 3, 29, 26, 28, 8, 17, K('board_d'))              # 台の前板
    for x in (4, 26):
        box(frame, x, x + 3, 18, 26, 0, 16, K('timber_d'))
    if which == 1:                                              # 魚
        for _ in range(int(rng.integers(5, 9))):
            cx, cy = int(rng.integers(5, 26)), int(rng.integers(18, 26))
            box(frame, cx, cx + 6, cy, cy + 3, 19, 21, K('steel_tm_l'))
            box(frame, cx + 5, cx + 7, cy, cy + 3, 19, 21, K('steel_d'))
    elif which == 2:                                            # 青物
        for _ in range(int(rng.integers(6, 11))):
            cx, cy = int(rng.integers(5, 26)), int(rng.integers(18, 26))
            ball(frame, cx, cy, 21, 2.6, pick(rng, [K('grass_tm_veg'), K('grass_tm_vegl'),
                                                    K('fl_r')], [0.5, 0.34, 0.16]))
    else:                                                       # 布地
        for i, col in enumerate((K('cloth_tm_r'), K('cloth_tm_b'), K('cloth_tm_y'))):
            box(frame, 5 + (i * 8), 11 + (i * 8), 18, 26, 19, 24, col)
            box(frame, 5 + (i * 8), 11 + (i * 8), 18, 26, 23, 24, K('cloth_tm_w'))
    canopy = vol(h=56)
    stripe = (K('cloth_tm_r'), K('cloth_tm_b'), K('cloth_tm_g'))[which - 1]
    for x in range(0, V, 8):                                    # 縞の日除け
        box(canopy, x, x + 4, 2, 30, 40, 42, stripe)
        box(canopy, x + 4, x + 8, 2, 30, 40, 42, K('cloth_tm_w'))
    box(canopy, 0, V, 28, 32, 34, 42, stripe)                   # 前へ垂れた庇
    return [('frame', frame, (0, 0, 0)), ('canopy', canopy, (0, 0, 0))]


def market_stall_parts():
    return wind_parts(['canopy'], wind_k=0.35)


def banner_pole_tel(name):
    """幟 — 石の礎に高い柱、縦長の朱の旗（風で揺れる）。城と広場の縦の線。"""
    rng = rng_for(name)
    frame = vol(h=88)
    _rubble(frame, 11, 21, 11, 21, 0, 8, rng, course=4)
    box(frame, 14, 18, 14, 18, 6, 78, K('timber'))
    box(frame, 13, 19, 13, 19, 74, 78, K('timber_d'))
    box(frame, 15, 17, 15, 17, 78, 84, K('brass'))              # 頭の金具
    box(frame, 12, 20, 15, 17, 76, 78, K('iron'))               # 腕木
    flag = vol(h=88)
    box(flag, 8, 24, 15, 17, 42, 76, K('cloth_tm_r'))
    box(flag, 8, 24, 15, 17, 54, 64, K('cloth_tm_y'))
    box(flag, 12, 20, 15, 17, 57, 61, K('cloth_tm_r'))
    box(flag, 8, 24, 15, 17, 42, 47, K('cloth_tm_rd'))
    box(flag, 8, 12, 15, 17, 42, 76, K('cloth_tm_rd'))
    return [('frame', frame, (0, 0, 0)), ('flag', flag, (0, 0, 0))]


def banner_pole_parts():
    return wind_parts(['flag'], wind_k=0.7)


def guild_sign_tel(name):
    """張り出し看板 — 鉄の腕木に吊った板。振り子で軋みながら揺れる。"""
    rng = rng_for(name)
    frame = vol(h=48)
    box(frame, 0, 4, 14, 18, 0, 44, K('beam'))                  # 壁側の柱
    box(frame, 2, 22, 15, 17, 38, 40, K('iron'))                # 腕木
    box(frame, 4, 10, 15, 17, 32, 39, K('iron'))                # 方杖
    box(frame, 18, 20, 15, 17, 34, 39, K('iron'))
    panel = vol(h=48)
    box(panel, 12, 26, 14, 16, 20, 36, K('board'))
    box(panel, 12, 26, 14, 16, 20, 22, K('timber_d'))
    box(panel, 12, 26, 14, 16, 34, 36, K('timber_d'))
    box(panel, 15, 23, 14, 15, 24, 32, K('cloth_tm_r'))         # 描かれた紋
    box(panel, 17, 21, 14, 15, 26, 30, K('cloth_tm_y'))
    box(panel, 18, 20, 15, 17, 36, 39, K('iron'))               # 吊り金具
    return [('frame', frame, (0, 0, 0)), ('panel', panel, (0, 0, 0))]


def guild_sign_parts():
    return swing_parts([('panel', (19, 16, 39))], amplitude=5.0, period=2.2)


def covered_cart_tel(name):
    """幌の荷車 — 馬を外して停めてある。闇市の店先。"""
    rng = rng_for(name)
    v = vol(h=44)
    box(v, 4, 28, 8, 24, 12, 15, K('board_d'))
    box(v, 4, 28, 8, 10, 15, 22, K('board_d'))
    box(v, 4, 28, 22, 24, 15, 22, K('board_d'))
    for x in (7, 15, 25):
        box(v, x, x + 2, 8, 10, 22, 34, K('mbr'))
        box(v, x, x + 2, 22, 24, 22, 34, K('mbr'))
        box(v, x, x + 2, 8, 24, 32, 34, K('mbr'))
    for x in range(4, 28, 6):                                   # 継ぎの当たった幌
        box(v, x, x + 6, 7, 25, 32, 35,
            pick(rng, [K('cloth_tm_wd'), K('rag'), K('cloth_d')], [0.44, 0.3, 0.26]))
        box(v, x, x + 6, 7, 9, 22, 34,
            pick(rng, [K('cloth_tm_wd'), K('rag')], [0.6, 0.4]))
    _cart_wheel(v, 9, 8, 7.5, 6, 9)
    _cart_wheel(v, 9, 8, 7.5, 23, 26)
    _cart_wheel(v, 24, 7, 6.5, 6, 9)
    _cart_wheel(v, 24, 7, 6.5, 23, 26)
    box(v, 26, 32, 15, 17, 0, 14, K('timber'))                  # 下ろした轅
    return one(v)


def brazier_tel(name):
    """篝火の鉄鉢 — 三脚に鉄の鉢、薪。火は `brazier_fire_tel`。"""
    v = vol(h=36)
    for a in (0.0, 2.09, 4.19):
        tx = int(16 + (9 * np.cos(a)))
        ty = int(16 + (9 * np.sin(a)))
        for t in range(20):
            px = int(round(tx + ((16 - tx) * t / 20.0)))
            py = int(round(ty + ((16 - ty) * t / 20.0)))
            box(v, px - 1, px + 2, py - 1, py + 2, t, t + 1, K('iron'))
    disc(v, 16, 16, 9.0, 20, 28, K('iron'))
    disc(v, 16, 16, 7.4, 21, 28, 0)
    disc(v, 16, 16, 9.2, 26, 28, K('rust'))
    disc(v, 16, 16, 7.4, 21, 23, K('char'))
    for a in (0.6, 2.4, 4.2, 5.6):
        x = int(16 + (4 * np.cos(a)))
        y = int(16 + (4 * np.sin(a)))
        box(v, x, x + 2, y, y + 2, 22, 27, K('log_d'))
    return one(v)


def brazier_fire_tel(name):
    """篝火の火（別体。自発光は置く側が足す）。"""
    rng = rng_for(name)
    v = vol(h=36)
    for z in range(23, 34):
        r = 6.0 - ((z - 23) * 0.5)
        shade = K('lava_o') if z < 28 else K('lava_y')
        disc(v, 16, 16, max(0.8, r), z, z + 1, shade)
    for _ in range(int(rng.integers(2, 5))):                    # 舞う火の粉
        cx, cy = int(rng.integers(12, 20)), int(rng.integers(12, 20))
        cz = int(rng.integers(32, 36))
        box(v, cx, cx + 1, cy, cy + 1, cz, cz + 1, K('lava_y'))
    return one(v)


def lantern_post_tel(name):
    """吊りランタンの柱 — 木の柱に鉄の腕木、四角い角灯。火は `lantern_flame_tel`。"""
    rng = rng_for(name)
    v = vol(h=64)
    _rubble(v, 12, 20, 12, 20, 0, 6, rng, course=3)
    box(v, 14, 18, 14, 18, 4, 52, K('timber'))
    box(v, 13, 19, 13, 19, 48, 52, K('timber_d'))
    box(v, 16, 26, 15, 17, 48, 50, K('iron'))                   # 腕木
    box(v, 16, 20, 15, 17, 44, 49, K('iron'))
    box(v, 22, 24, 15, 17, 42, 48, K('iron'))                   # 吊り金具
    box(v, 19, 27, 12, 20, 32, 42, K('iron_tm_k'))              # 角灯の枠
    box(v, 20, 26, 13, 19, 33, 41, K('glass'))
    box(v, 19, 27, 12, 20, 41, 44, K('iron'))                   # 笠
    box(v, 20, 26, 13, 19, 43, 45, K('iron_tm_k'))
    return one(v)


def lantern_flame_tel(name):
    """角灯の火（別体。夜だけ置く）。"""
    v = vol(h=64)
    box(v, 21, 25, 14, 18, 34, 40, K('lava_y'))
    box(v, 22, 24, 15, 17, 33, 42, K('fl_y'))
    return one(v)


def forge_tel(name):
    """鍛冶の炉 — 乱石の炉に鞴、上に煙出し。火は `forge_fire_tel`（昼も光る）。"""
    rng = rng_for(name)
    v = vol(h=60)
    _rubble(v, 3, 25, 6, 26, 0, 20, rng, course=5)
    box(v, 6, 22, 9, 23, 16, 20, 0)                             # 火床（刳る）
    box(v, 6, 22, 9, 23, 16, 18, K('char'))
    box(v, 3, 25, 6, 26, 19, 20, K('rub_l'))
    _rubble(v, 8, 20, 8, 20, 20, 50, rng, course=6)             # 煙突
    box(v, 10, 18, 10, 18, 20, 48, 0)
    box(v, 9, 19, 9, 19, 48, 52, K('rub_l'))
    box(v, 10, 18, 10, 18, 48, 52, K('soot'))
    box(v, 25, 31, 10, 22, 8, 20, K('board_d'))                 # 鞴
    box(v, 25, 31, 11, 21, 12, 18, K('cloth_d'))
    box(v, 24, 26, 14, 18, 14, 18, K('iron'))
    box(v, 29, 32, 15, 17, 18, 30, K('timber'))                 # 鞴の柄
    for _ in range(int(rng.integers(2, 5))):                    # 炭の山
        cx, cy = int(rng.integers(4, 12)), int(rng.integers(26, 30))
        ball(v, cx, cy, 0, 4.0, K('char'), squash=0.6)
    return one(v)


def forge_fire_tel(name):
    """炉の火（別体。**昼も光る**）。"""
    v = vol(h=60)
    box(v, 7, 21, 10, 22, 17, 22, K('lava_o'))
    box(v, 9, 19, 12, 20, 19, 25, K('lava_y'))
    box(v, 11, 17, 14, 18, 22, 28, K('fl_y'))
    return one(v)


def anvil_tel(name):
    """金床 — 樫の切り株に鋼の金床。槌と火挟みが置いてある。"""
    v = vol(h=32)
    disc(v, 13, 16, 8.0, 0, 12, K('log'))                       # 切り株
    disc(v, 13, 16, 8.0, 10, 12, K('log_end'))
    box(v, 6, 21, 12, 20, 12, 15, K('steel_d'))                 # 台
    box(v, 8, 19, 13, 19, 15, 18, K('steel_d'))
    box(v, 4, 21, 13, 19, 18, 22, K('steel'))                   # 面
    box(v, 4, 21, 13, 19, 21, 22, K('steel_tm_l'))
    box(v, 21, 26, 14, 18, 19, 22, K('steel_d'))                # 角（ホーン）
    box(v, 24, 30, 20, 24, 0, 3, K('timber'))                   # 槌
    box(v, 22, 26, 20, 24, 1, 5, K('steel_d'))
    box(v, 24, 31, 8, 10, 0, 2, K('iron'))                      # 火挟み
    box(v, 24, 31, 11, 13, 0, 2, K('iron'))
    return one(v)


def quench_tel(name):
    """焼き入れの水桶 — 板の桶に黒い水、鋏と屑鉄。"""
    v = vol(h=24)
    disc(v, 15, 16, 10.0, 0, 16, K('timber'))
    _staves(v, 0, 16, K('timber_d'))
    disc(v, 15, 16, 10.4, 2, 4, K('iron'), hollow=9.0)
    disc(v, 15, 16, 10.4, 12, 14, K('iron'), hollow=9.0)
    disc(v, 15, 16, 8.4, 2, 16, 0)
    disc(v, 15, 16, 8.4, 2, 14, K('wa_tm_dark'))
    disc(v, 15, 16, 8.4, 13, 14, K('murk'))
    box(v, 24, 31, 20, 24, 0, 3, K('rust'))
    box(v, 26, 30, 6, 12, 0, 2, K('rust_d'))
    return one(v)


def weapon_rack_tel(name):
    """武器立て — 斜めの木の枠に剣・斧・矛。**刃を上へ**（下向きは倒れて見える）。"""
    rng = rng_for(name)
    v = vol(h=48)
    box(v, 2, 30, 18, 24, 0, 4, K('timber_d'))                  # 台
    for x in (3, 28):
        box(v, x, x + 3, 18, 24, 0, 30, K('timber'))
    box(v, 2, 30, 19, 22, 26, 29, K('timber_d'))
    slots = [4, 10, 16, 22, 27]
    for i, x in enumerate(slots):
        kind = int(rng.integers(0, 3))
        box(v, x, x + 2, 20, 22, 4, 40, K('timber_d'))          # 柄
        if kind == 0:                                           # 剣
            box(v, x - 1, x + 3, 20, 22, 26, 28, K('brass'))
            box(v, x, x + 2, 20, 22, 28, 44, K('steel_tm_l'))
        elif kind == 1:                                         # 斧
            box(v, x - 2, x + 5, 20, 22, 36, 42, K('steel'))
            box(v, x - 2, x + 1, 20, 22, 34, 44, K('steel_tm_l'))
        else:                                                   # 矛
            box(v, x, x + 2, 20, 22, 40, 46, K('steel_tm_l'))
            box(v, x - 2, x + 4, 20, 22, 38, 40, K('steel'))
    return one(v)


def armour_stand_tel(name):
    """鎧の陳列 — 木の台に胴鎧と兜、脇に盾。**肩を胴より広く**して人の形に読ませる。

    胴を 1 つの箱にすると洗濯機に見える（この回の記録）——肩・胴・腰で幅を変え、
    兜は胴より細く、あいだの首を暗くして境を作る。
    """
    v = vol(h=52)
    box(v, 5, 27, 11, 23, 0, 4, K('timber_d'))                  # 台
    box(v, 5, 27, 11, 23, 3, 4, K('board'))
    box(v, 14, 18, 15, 19, 4, 16, K('timber'))                  # 心棒
    box(v, 11, 21, 13, 21, 16, 20, K('steel_d'))                # 腰
    box(v, 10, 22, 12, 22, 20, 32, K('steel'))                  # 胴
    box(v, 15, 17, 12, 22, 20, 32, K('steel_tm_l'))             # 中央の稜
    box(v, 10, 22, 12, 22, 30, 32, K('steel_tm_l'))
    box(v, 8, 24, 12, 22, 32, 36, K('steel'))                   # 肩（胴より広い）
    box(v, 8, 24, 12, 22, 35, 36, K('steel_tm_l'))
    box(v, 8, 10, 13, 21, 26, 34, K('steel_d'))                 # 腕
    box(v, 22, 24, 13, 21, 26, 34, K('steel_d'))
    box(v, 13, 19, 14, 20, 36, 38, K('dark'))                   # 首（暗くして境を作る）
    box(v, 12, 20, 13, 21, 38, 45, K('steel'))                  # 兜
    box(v, 12, 20, 13, 21, 43, 45, K('steel_tm_l'))
    box(v, 12, 20, 20, 21, 40, 42, K('dark'))                   # 面の隙間
    box(v, 15, 17, 13, 21, 38, 45, K('steel_tm_l'))
    box(v, 15, 18, 15, 19, 45, 49, K('cloth_tm_r'))             # 前立ての羽
    box(v, 27, 31, 11, 23, 0, 22, K('board'))                   # 立て掛けた盾
    box(v, 27, 31, 13, 21, 3, 19, K('cloth_tm_b'))
    box(v, 27, 31, 15, 19, 6, 16, K('cloth_tm_y'))
    box(v, 27, 31, 11, 23, 21, 22, K('steel_d'))
    return one(v)


def spear_rack_tel(name):
    """槍立て — 枠に槍を並べる。門と集会所の脇に。"""
    rng = rng_for(name)
    v = vol(h=52)
    box(v, 3, 29, 18, 24, 0, 4, K('timber_d'))
    box(v, 3, 29, 19, 23, 20, 24, K('timber'))
    for x in (4, 27):
        box(v, x, x + 2, 19, 23, 0, 24, K('timber'))
    for x in range(6, 28, 4):
        if rng.random() < 0.85:
            box(v, x, x + 2, 20, 22, 4, 44, K('timber'))
            box(v, x, x + 2, 20, 22, 42, 48, K('steel_tm_l'))
            box(v, x - 1, x + 3, 20, 22, 40, 42, K('steel'))
    return one(v)


def butts_tel(name):
    """弓の的 — 藁を巻いた円い的を斜めに立て、刺さった矢。アーチャーのギルド。"""
    rng = rng_for(name)
    v = vol(h=48)
    for x in (6, 24):                                           # 支え
        box(v, x, x + 3, 18, 22, 0, 30, K('timber'))
        box(v, x, x + 3, 22, 28, 0, 12, K('timber_d'))
    #! 巻いた藁の的（軸は y。円は x-z 平面で描く）。
    for y in range(15, 20):
        for z in range(12, 40):
            for x in range(2, 30):
                d2 = ((x - 16) ** 2) + ((z - 26) ** 2)
                if d2 > 169:
                    continue
                if d2 <= 16:
                    v[x, y, z] = K('cloth_tm_r')                # 的の芯（朱）
                elif d2 <= 42:
                    v[x, y, z] = K('cloth_tm_b')
                elif d2 <= 72:
                    v[x, y, z] = K('cloth_tm_w')
                else:
                    v[x, y, z] = K('straw') if ((z // 3) % 2) else K('straw_d')
    for _ in range(int(rng.integers(3, 6))):                    # 刺さった矢
        a = float(rng.random() * 6.28)
        x = int(16 + (7 * np.cos(a)))
        z = int(26 + (7 * np.sin(a)))
        box(v, x, x + 1, 19, 27, z, z + 1, K('mbr'))
        box(v, x, x + 1, 25, 27, z, z + 3, K('cloth_tm_w'))
    return one(v)


def pell_tel(name):
    """打ち込み杭 — 傷だらけの太い杭に木の剣と盾。戦士の集会所の庭。"""
    rng = rng_for(name)
    v = vol(h=52)
    box(v, 4, 28, 8, 26, 0, 3, K('pack_d'))
    disc(v, 14, 16, 6.0, 0, 40, K('log'))
    for _ in range(int(rng.integers(6, 12))):                   # 打ち込みの傷
        z = int(rng.integers(14, 38))
        a = float(rng.random() * 6.28)
        x = int(14 + (5 * np.cos(a)))
        y = int(16 + (5 * np.sin(a)))
        box(v, x, x + 3, y, y + 3, z, z + 2, K('log_d'))
    disc(v, 14, 16, 6.2, 38, 40, K('log_end'))
    box(v, 24, 27, 20, 24, 0, 22, K('timber'))                  # 立て掛けた木剣
    box(v, 24, 27, 20, 24, 20, 30, K('oak_l'))
    box(v, 22, 30, 8, 12, 0, 14, K('board'))                    # 盾
    box(v, 23, 29, 9, 11, 2, 12, K('cloth_tm_b'))
    return one(v)


def herb_rack_tel(name):
    """薬草の干し台 — 木の枠に束ねた草を吊る。錬金術店の庭。"""
    rng = rng_for(name)
    v = vol(h=48)
    for x in (3, 27):
        box(v, x, x + 3, 14, 18, 0, 38, K('timber'))
    box(v, 2, 30, 15, 17, 36, 39, K('timber_d'))
    box(v, 2, 30, 15, 17, 26, 28, K('mbr'))
    for x in range(5, 28, 4):                                   # 吊るした草の束
        h = int(rng.integers(8, 16))
        shade = pick(rng, [K('grass_tm_herb'), K('wilt'), K('grass_tm_veg')], [0.42, 0.32, 0.26])
        box(v, x, x + 3, 14, 18, 36 - h, 37, shade)
        box(v, x, x + 3, 14, 18, 35, 37, K('lash'))
    for x in range(6, 28, 6):                                   # 下の段
        h = int(rng.integers(5, 10))
        box(v, x, x + 3, 15, 18, 26 - h, 27, K('wilt_d') if (x % 4) else K('grass_tm_herb'))
    box(v, 4, 12, 18, 26, 0, 6, K('mbr'))                       # 足元の籠
    box(v, 5, 11, 19, 25, 4, 8, K('grass_tm_herb'))
    return one(v)


def cauldron_tel(name):
    """煮え立つ大釜 — 石の炉に三脚の鉄鍋。火は `cauldron_fire_tel`（昼も光る）。"""
    rng = rng_for(name)
    v = vol(h=44)
    _rubble(v, 4, 28, 8, 26, 0, 10, rng, course=4)
    box(v, 8, 24, 12, 22, 6, 10, 0)
    box(v, 8, 24, 12, 22, 6, 8, K('char'))
    for a in (0.0, 2.09, 4.19):                                 # 三脚
        x = int(16 + (10 * np.cos(a)))
        y = int(17 + (7 * np.sin(a)))
        box(v, x, x + 2, y, y + 2, 8, 26, K('iron'))
    for z in range(16, 30):                                     # 鉄鍋
        r = 9.0 - abs(z - 24) * 0.3
        disc(v, 16, 17, r, z, z + 1, K('iron_tm_k') if (z % 4) else K('iron'))
    disc(v, 16, 17, 7.4, 26, 30, K('wa_tm_brew'))
    disc(v, 16, 17, 7.4, 28, 30, K('wa_tm_brewl'))
    box(v, 6, 12, 20, 26, 0, 8, K('mbr'))                       # 脇の籠
    return one(v)


def cauldron_fire_tel(name):
    """大釜の火と湧き立つ泡（別体。**昼も光る**）。"""
    v = vol(h=44)
    box(v, 9, 23, 13, 21, 7, 14, K('lava_o'))
    box(v, 11, 21, 14, 20, 10, 16, K('lava_y'))
    disc(v, 16, 17, 6.0, 29, 32, K('wa_tm_brewl'))
    box(v, 14, 19, 15, 19, 31, 36, K('wa_tm_brewl'))
    return one(v)


def crystal_tel(name):
    """光る水晶と符石 — 石の台に大きな結晶。光は `crystal_glow_tel`（昼も光る）。"""
    rng = rng_for(name)
    v = vol(h=48)
    _rubble(v, 8, 24, 8, 24, 0, 10, rng, course=4)
    box(v, 7, 25, 7, 25, 9, 11, K('rub_l'))
    for k in range(20):                                         # 結晶（上ほど細い）
        r = 6.5 - (k * 0.28)
        disc(v, 16, 16, max(0.8, r), 11 + k, 12 + k, K('qz') if (k % 3) else K('qz_l'))
    box(v, 15, 17, 15, 17, 31, 38, K('qz_l'))
    for (cx, cy) in ((9, 20), (23, 12), (20, 24)):              # 符石
        box(v, cx, cx + 5, cy, cy + 5, 10, 13, K('rub_d'))
        box(v, cx + 1, cx + 4, cy + 1, cy + 4, 12, 13, K('ward'))
    return one(v)


def crystal_glow_tel(name):
    """水晶の光（別体。**昼も光る**）。"""
    v = vol(h=48)
    for k in range(20):
        r = 5.0 - (k * 0.22)
        disc(v, 16, 16, max(0.6, r), 12 + k, 13 + k, K('mgl'))
    box(v, 15, 17, 15, 17, 32, 38, K('mgl_d'))
    return one(v)


def book_stack_tel(name):
    """本の山と書見台 — 図書館と書店の庭先。革表紙の色を分けて積む。"""
    rng = rng_for(name)
    v = vol(h=40)
    box(v, 3, 29, 16, 26, 0, 4, K('timber_d'))                  # 低い台
    z = 4
    for _ in range(int(rng.integers(4, 7))):                    # 積んだ本
        w, d, h = int(rng.integers(9, 14)), int(rng.integers(7, 10)), int(rng.integers(2, 4))
        x, y = int(rng.integers(4, 16)), int(rng.integers(17, 21))
        col = pick(rng, [K('rot'), K('cloth_tm_rd'), K('cloth_tm_bd'), K('timber_d')],
                   [0.3, 0.26, 0.24, 0.20])
        box(v, x, x + w, y, y + d, z, z + h, col)
        box(v, x, x + w, y, y + d, z + h - 1, z + h, K('cream_d'))
        box(v, x, x + 1, y, y + d, z, z + h, K('gold_d'))
        z += h
    box(v, 20, 24, 18, 22, 4, 26, K('oak'))                     # 書見台
    box(v, 17, 28, 15, 25, 26, 29, K('board'))
    box(v, 18, 27, 16, 24, 29, 31, K('cream'))
    box(v, 22, 23, 16, 24, 29, 31, K('timber_d'))
    return one(v)


def relic_plinth_tel(name):
    """台座の遺物 — 石の台に竜の頭骨。博物館の庭先。"""
    rng = rng_for(name)
    v = vol(h=44)
    _rubble(v, 8, 24, 9, 23, 0, 16, rng, course=5)
    box(v, 6, 26, 7, 25, 15, 18, K('rub_l'))
    box(v, 8, 26, 12, 22, 18, 26, K('bone'))                    # 頭蓋
    box(v, 8, 26, 12, 22, 24, 26, K('bone_d'))
    box(v, 4, 10, 13, 21, 18, 24, K('bone'))                    # 吻
    box(v, 3, 6, 14, 20, 18, 22, K('bone_d'))
    box(v, 10, 14, 10, 12, 20, 24, K('dark'))                   # 眼窩
    box(v, 10, 14, 22, 24, 20, 24, K('dark'))
    for x in range(5, 14, 3):                                   # 牙
        box(v, x, x + 2, 13, 15, 15, 19, K('bone'))
        box(v, x, x + 2, 19, 21, 15, 19, K('bone'))
    for x in range(18, 26, 3):                                  # 角
        box(v, x, x + 3, 11, 14, 25, 31, K('bone_d'))
        box(v, x, x + 3, 20, 23, 25, 31, K('bone_d'))
    return one(v)


def shrine_tel(name):
    """道端の祠 — 乱石の壁龕に木の聖像と蝋燭。火は `shrine_flame_tel`。"""
    rng = rng_for(name)
    v = vol(h=52)
    _rubble(v, 4, 28, 8, 24, 0, 40, rng, course=6)
    box(v, 9, 23, 14, 25, 10, 34, 0)                            # 龕を刳る
    box(v, 9, 23, 14, 24, 10, 12, K('rub_l'))
    for k in range(1, 4):                                       # 龕のアーチ
        box(v, 9, 9 + k, 14, 24, 30 + k, 32 + k, K('rub_l'))
        box(v, 23 - k, 23, 14, 24, 30 + k, 32 + k, K('rub_l'))
    box(v, 13, 19, 16, 20, 12, 28, K('oak'))                    # 木の聖像
    ball(v, 16, 18, 29, 3.4, K('cream'))
    box(v, 12, 20, 16, 20, 20, 23, K('cloth_tm_b'))
    box(v, 15, 17, 16, 20, 30, 33, K('gold'))                   # 光背
    for x in (10, 21):                                          # 蝋燭
        box(v, x, x + 2, 21, 23, 12, 20, K('cream_l'))
    _thatch_cap(v, 2, 30, 6, 26, 40, 10, rng)                   # 小さな藁の庇
    return one(v)


def shrine_flame_tel(name):
    """祠の蝋燭の火（別体）。"""
    v = vol(h=52)
    for x in (10, 21):
        box(v, x, x + 2, 21, 23, 20, 24, K('fl_y'))
        box(v, x, x + 2, 21, 23, 19, 21, K('lava_y'))
    box(v, 13, 20, 16, 20, 30, 34, K('lit'))
    return one(v)


def ale_barrels_tel(name):
    """麦酒の樽 — 架台に横倒しの大樽 2 つ、注ぎ口と木の杯。"""
    v = vol(h=36)
    for y in (7, 22):
        box(v, 4, 28, y, y + 3, 0, 8, K('timber_d'))            # 架台
        box(v, 4, 7, y - 2, y + 5, 6, 10, K('timber_d'))
        box(v, 25, 28, y - 2, y + 5, 6, 10, K('timber_d'))
    for cy in (9, 24):
        log_x(v, cy, 17, 7.6, 5, 27, K('timber'))
        log_x(v, cy, 17, 7.8, 8, 10, K('iron'))
        log_x(v, cy, 17, 7.8, 21, 23, K('iron'))
        log_x(v, cy, 17, 6.4, 5, 6, K('timber_d'))
        box(v, 3, 5, int(cy) - 1, int(cy) + 1, 14, 16, K('brass'))  # 注ぎ口
    box(v, 8, 12, 15, 19, 0, 5, K('timber_l'))                  # 木の杯
    box(v, 9, 11, 16, 18, 3, 5, K('cream_d'))
    box(v, 18, 22, 14, 18, 0, 5, K('timber_l'))
    return one(v)


def tavern_table_tel(name):
    """外の卓と長椅子 — 厚板の卓に杯と壺。宿屋とカジノの前。"""
    rng = rng_for(name)
    v = vol(h=24)
    box(v, 4, 28, 11, 21, 12, 15, K('board'))                   # 卓
    for x in range(5, 28, 5):
        box(v, x, x + 1, 11, 21, 12, 15, K('board_d'))
    for x in (6, 24):
        box(v, x, x + 3, 12, 20, 0, 12, K('timber'))
    for y in (5, 24):                                           # 長椅子
        box(v, 4, 28, y, y + 4, 7, 9, K('board'))
        for x in (6, 24):
            box(v, x, x + 3, y, y + 4, 0, 7, K('timber_d'))
    for _ in range(int(rng.integers(2, 5))):                    # 杯
        cx = int(rng.integers(7, 25))
        cy = int(rng.integers(13, 19))
        disc(v, cx, cy, 2.4, 15, 20, K('timber_l'))
        disc(v, cx, cy, 1.6, 18, 20, K('cream_d'))
    disc(v, 10, 16, 4.0, 15, 22, K('clay'))                     # 壺
    disc(v, 10, 16, 2.4, 21, 23, K('clay_d'))
    return one(v)


def cage_tel(name):
    """獣の檻 — 鉄格子の木の車輪つきの檻。闘技場の脇に。"""
    v = vol(h=40)
    box(v, 3, 29, 8, 26, 8, 11, K('board_d'))                   # 床
    box(v, 3, 29, 8, 26, 30, 33, K('board_d'))                  # 天
    for x in range(4, 29, 4):                                   # 鉄格子
        box(v, x, x + 2, 8, 10, 11, 31, K('iron'))
        box(v, x, x + 2, 24, 26, 11, 31, K('iron'))
    for y in range(9, 26, 4):
        box(v, 3, 5, y, y + 2, 11, 31, K('iron'))
        box(v, 27, 29, y, y + 2, 11, 31, K('iron'))
    box(v, 3, 29, 8, 26, 20, 22, K('iron_tm_k'))                # 帯
    box(v, 12, 20, 24, 26, 11, 28, K('iron_tm_k'))              # 扉
    box(v, 19, 21, 24, 27, 18, 22, K('rust'))                   # ロック
    box(v, 6, 26, 10, 24, 11, 13, K('straw_d'))                 # 敷き藁
    _cart_wheel(v, 8, 5, 5.0, 6, 9)
    _cart_wheel(v, 8, 5, 5.0, 25, 28)
    _cart_wheel(v, 24, 5, 5.0, 6, 9)
    _cart_wheel(v, 24, 5, 5.0, 25, 28)
    return one(v)


# ------------------------------------------------------------------ 屋根
def chimney_tel(name):
    """石の煙突 — 藁屋根の棟の真ん中に立てる（`roof_vent`）。煙は置く側が積む。

    テルモラの家は木骨と藁葺きなので、煙突は**乱石積みで細く高く**する
    ——切石の角柱にするとモリバントの都の家に見える。頭に鴉が 1 羽止まる。
    """
    rng = rng_for(name)
    v = vol(h=30)
    _rubble(v, 11, 21, 11, 21, 0, 20, rng, course=5)
    box(v, 10, 22, 10, 22, 20, 22, K('rub_l'))                  # 笠石
    box(v, 12, 20, 12, 20, 20, 24, K('rub_d'))                  # 煙道
    box(v, 13, 19, 13, 19, 21, 24, K('soot'))
    box(v, 9, 23, 9, 23, 0, 3, K('straw_d'))                    # 棟の藁の掛かり
    box(v, 9, 23, 9, 23, 2, 3, K('straw_l'))
    ball(v, 22, 14, 24, 2.6, K('dark'), squash=0.8)             # 鴉
    box(v, 20, 24, 12, 16, 24, 25, K('obs_l'))
    box(v, 21, 23, 16, 18, 24, 28, K('dark'))
    box(v, 21, 23, 18, 19, 26, 27, K('fl_y'))
    return one(v)


# ------------------------------------------------------------------ 登録
def register(g):
    """`gen_prefabs.py` の名前空間 `g` へテルモラの素材を足す。

    **`TOWN_STYLES` には触らない**——`'tel'` は既に入っている（既存の壁・屋根・地面が
    そこから意匠を選んでいるので、足すと二重になる）。
    """
    global G, C, V
    G = g
    C = g['C']
    V = g['V']
    _palette()
    cat = g['CATALOG']
    static = g['static_part']

    # ---- 地面 ----
    for i in range(1, 4):
        cat['ground_market_tel_%02d' % i] = (ground_market_tel, static(), '市の広場の半舗装')
    for i in range(1, 3):
        cat['ground_muck_tel_%02d' % i] = (ground_muck_tel, static(), '路地のぬかるみ')
        cat['ground_field_tel_%02d' % i] = (ground_field_tel, static(), '畑の畝')
        cat['ground_grave_tel_%02d' % i] = (ground_grave_tel, static(), '墓地の芝と踏み土')
        cat['ground_lists_tel_%02d' % i] = (ground_lists_tel, static(), '馬場のならした砂')
    cat['ground_bridge_tel'] = (ground_bridge_tel, static(), '石橋の甲板')

    # ---- 区画の柵 ----
    #! **名前は `<茎>_n/_s/_w/_e`。**区画の柵は置く側が `rim` の名前へ `_n` を足して引く
    #! （`terrain_view.cpp` の `kRimSuffix`）ので、`wattle_n_tel` と書くと 1 枚も出ない。
    for side in ('n', 's', 'w', 'e'):
        cat['wattle_tel_%s' % side] = (wattle_tel, static(), '編み垣（%s 側）' % side)
        cat['railing_tel_%s' % side] = (railing_tel, static(), '木の横桟の柵（%s 側）' % side)

    # ---- 都市の骨（大物）----
    cat['gatehouse_tel'] = (gatehouse_tel, wind_parts(['flag'], 0.7), '大門楼（東西にくぐる。旗は風）')
    cat['wall_tower_tel'] = (wall_tower_tel, static(), '城壁の櫓（乱石積みの方形の塔）')
    cat['belfry_tel'] = (belfry_tel, belfry_parts(), '鐘楼（鐘は振り子）')
    cat['windmill_tel'] = (windmill_build, windmill_parts(), '風車（2 マス幅。羽根は回転）')
    cat['arena_stand_tel'] = (arena_stand_tel, arena_stand_parts(), '闘技場の観客席（2 マス幅。幟は風）')
    cat['well_house_tel'] = (well_house_tel, well_house_parts(), '屋根つきの共同井戸（桶は振り子）')
    cat['gallows_tel'] = (gallows_tel, gallows_parts(), '絞首台（縄は振り子）')
    cat['pillory_tel'] = (pillory_tel, static(), 'さらし台')
    cat['notice_post_tel'] = (notice_post_tel, static(), '触れ書きの柱と鐘')
    cat['bridge_rail_tel'] = (bridge_rail_tel, static(), '石橋の欄干（北と南の辺）')
    cat['guard_post_tel'] = (guard_post_tel, static(), '衛兵の詰め所')

    # ---- 通りと生活 ----
    street = {
        'crate': (crate_tel, '木箱の積み'),
        'barrel': (barrel_tel, '樽'),
        'sack': (sack_tel, '麻袋'),
        'handcart': (handcart_tel, '手押し荷車'),
        'wagon_horse': (wagon_horse_tel, '荷馬車と馬'),
        'hitch_rail': (hitch_rail_tel, '馬つなぎの横木'),
        'trough': (trough_tel, '石の水槽'),
        'woodpile': (woodpile_tel, '薪の山'),
        'haystack': (haystack_tel, '干し草の山'),
        'dung': (dung_tel, '馬糞と藁'),
        'bench': (bench_tel, '木の腰掛け'),
        'planter': (planter_tel, '木の花桶'),
        'chicken': (chicken_tel, '鶏 3 羽'),
        'pig': (pig_tel, '泥に寝そべる豚'),
        'cat': (cat_tel, '猫'),
        'beehive': (beehive_tel, '藁の蜂の巣'),
        'scarecrow': (scarecrow_tel, '案山子'),
        'alley_junk': (alley_junk_tel, '路地のがらくた'),
        'herb_bed': (herb_bed_tel, '薬草の畝'),
        'bean_poles': (bean_poles_tel, '豆の支柱'),
        'compost': (compost_tel, '堆肥の山'),
        'tomb_slab': (tomb_slab_tel, '石棺の蓋'),
        'forge': (forge_tel, '鍛冶の炉。火は forge_fire_tel'),
        'forge_fire': (forge_fire_tel, '炉の火（昼も光る）'),
        'anvil': (anvil_tel, '金床と槌'),
        'quench': (quench_tel, '焼き入れの水桶'),
        'weapon_rack': (weapon_rack_tel, '武器立て'),
        'armour_stand': (armour_stand_tel, '鎧の陳列'),
        'spear_rack': (spear_rack_tel, '槍立て'),
        'butts': (butts_tel, '弓の的'),
        'pell': (pell_tel, '打ち込み杭'),
        'herb_rack': (herb_rack_tel, '薬草の干し台'),
        'cauldron': (cauldron_tel, '煮え立つ大釜。火は cauldron_fire_tel'),
        'cauldron_fire': (cauldron_fire_tel, '大釜の火（昼も光る）'),
        'crystal': (crystal_tel, '光る水晶と符石。光は crystal_glow_tel'),
        'crystal_glow': (crystal_glow_tel, '水晶の光（昼も光る）'),
        'book_stack': (book_stack_tel, '本の山と書見台'),
        'relic_plinth': (relic_plinth_tel, '台座の竜の頭骨'),
        'shrine': (shrine_tel, '道端の祠。火は shrine_flame_tel'),
        'shrine_flame': (shrine_flame_tel, '祠の蝋燭の火'),
        'ale_barrels': (ale_barrels_tel, '麦酒の樽'),
        'tavern_table': (tavern_table_tel, '外の卓と長椅子'),
        'covered_cart': (covered_cart_tel, '幌の荷車'),
        'brazier': (brazier_tel, '篝火の鉄鉢。火は brazier_fire_tel'),
        'brazier_fire': (brazier_fire_tel, '篝火の火'),
        'lantern_post': (lantern_post_tel, '吊りランタンの柱。火は lantern_flame_tel'),
        'lantern_flame': (lantern_flame_tel, '角灯の火'),
        'cage': (cage_tel, '獣の檻'),
    }
    for key, (builder, note) in street.items():
        cat[key + '_tel'] = (builder, static(), 'テルモラの小物: ' + note)
    cat['ivy_tel'] = (ivy_tel, ivy_parts(), 'テルモラの小物: 壁の蔦（先端は風）')
    cat['reeds_tel'] = (reeds_tel, reeds_parts(), 'テルモラの小物: 堀の縁の葦（風）')
    cat['laundry_tel'] = (laundry_tel, laundry_parts(), '通りをまたぐ洗濯物（振り子）')
    cat['banner_pole_tel'] = (banner_pole_tel, banner_pole_parts(), '幟（風）')
    cat['guild_sign_tel'] = (guild_sign_tel, guild_sign_parts(), '張り出し看板（振り子）')
    for i in range(1, 4):
        cat['market_stall_tel_%02d' % i] = (market_stall_tel, market_stall_parts(), '市の屋台')

    # ---- 緑と墓 ----
    for i in range(1, 3):
        cat['apple_tree_tel_%02d' % i] = (apple_tree_tel, apple_tree_parts(), '果樹（林檎・梨）')
        cat['grave_cross_tel_%02d' % i] = (grave_cross_tel, static(), '墓標')

    # ---- 屋根 ----
    cat['chimney_tel'] = (chimney_tel, static(), '石の煙突と鴉（roof_vent）')
