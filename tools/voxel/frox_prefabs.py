# -*- coding: utf-8 -*-
"""FroxComposband の新しい 2 町の素材の生成器。

`gen_prefabs.py` の末尾から `register(globals())` で呼ばれ、接尾辞 2 つぶんの素材を
`CATALOG` へ足す。

- `_ana` … アナンバール（5 番）。**金と商いの新興都市**。赤い焼き煉瓦と鋳鉄とすす色の
  スレート。石は台輪と窓枠の白い切石だけで、木は使わない（§4.1）
- `_tha` … タロス（6 番）。**東方の王都**。日干し煉瓦と漆喰（砂色 3 階調）と青い施釉タイル、
  白い石。木は棕櫚と糸杉だけ（§4.2）

**ほかの町の生成器には 1 バイトも触らない。** 色も形もこのファイルだけで閉じている
（読み込みの順に依存させない——`gold_gilt` のような別の町の色は借りない）。

## 色の名前について（`palette_materials.classify` との整合）
設計書 §4.2 の註記は「名前は `brick_` `rock_` `roof_` `iron_` `leaf_` `cloth_` `tile_` で
始める」と書くが、`tools/voxel/palette_materials.py` の `KEYWORDS` に **`brick` という語は
無い**（`Stone` の当たり語は `wall` `rock` `tile` `pav` `roof` `mortar` `band` …）。頭に
`brick_` を置くと材質が `Default` に落ちて面の汚しが効かなくなるので、**煉瓦の色は
`wall_ana_brick*` と名前の頭を `wall_` にした**（`telmora_stone_prefabs.py` が
`stone_` → `rock_` と読み替えたのと同じ扱い）。値と用途は設計書のとおりで、頭の語だけを
実装の都合で変えてある。当たる語を選んだ結果は次のとおり。

| 何 | 頭の語 | 当たる材質 |
| --- | --- | --- |
| 煉瓦・日干し煉瓦 | `wall_` | Stone |
| スレート | `roof_` | Stone |
| 敷石・舗石 | `pav_` | Stone |
| 青い施釉タイル | `tile_` | Stone |
| 白い切石 | `rock_` | Stone |
| 鋳鉄・鉄 | `iron_` | Metal |
| 金 | `gold_` | Metal |
| 漆喰 | `plas_` | Plaster |
| 布（日除け・天幕） | `cloth_` | Fabric |
| 葉（並木・棕櫚） | `leaf_` | Leaf |
| 木（格子・戸） | `timber_` | Wood |
| ガラス・水面 | `glass_` | Clean |

**`ana_` で始まる名前は付けない**——`palette_materials.py` の `Soil` の当たり語に `ana`
（穴）があり、`ana_...` という色は土に分類されてしまう。意匠の名は必ず材の語の後ろへ置く。

## 作りの規律（`gen_prefabs.py` 冒頭・設計 §4.3 と同じ）
- 無作為な表面ノイズは入れない。彫りは 1 ボクセルの深さ。色は塊ごとに 1 色
- 接地層（z=0）の平面方向は縮めない。**+y は南＝カメラ側**。戸口は手前が y 大
- **柵（`fence_*`）の高さ 14 は変えない**（遮蔽の約束）。町を囲む塀（`palisade_*`）と
  敷地の門はこの約束の外
- 大物は `hollow` で中身を空ける（`.vox` が桁違いに膨れる）
"""
from __future__ import annotations

import numpy as np

G = None   #: gen_prefabs の名前空間（register で入る）
C = None   #: 色の名前 → 索引
V = 32


# ------------------------------------------------------------------ 形の道具（既存の生成器と同じ流儀）
def vol(h=32, w=None, d=None):
    return np.zeros((w or V, d or V, h), np.int16)


def box(v, x0, x1, y0, y1, z0, z1, colour):
    X, Y, Z = v.shape
    x0, x1 = max(0, x0), min(X, x1)
    y0, y1 = max(0, y0), min(Y, y1)
    z0, z1 = max(0, z0), min(Z, z1)
    if (x0 < x1) and (y0 < y1) and (z0 < z1):
        v[x0:x1, y0:y1, z0:z1] = colour


def disc(v, cx, cy, r, z0, z1, colour, hollow_r=0.0):
    X, Y, Z = v.shape
    xs = np.arange(X)[:, None] + 0.5 - cx
    ys = np.arange(Y)[None, :] + 0.5 - cy
    d2 = (xs * xs) + (ys * ys)
    m = d2 <= (r * r)
    if hollow_r > 0:
        m &= d2 >= (hollow_r * hollow_r)
    z0, z1 = max(0, z0), min(Z, z1)
    if z0 < z1:
        v[:, :, z0:z1][m] = colour


def ball(v, cx, cy, cz, r, colour, squash=1.0):
    """球（`squash` < 1 で上下に潰す）。"""
    X, Y, Z = v.shape
    xs = np.arange(X)[:, None, None] + 0.5 - cx
    ys = np.arange(Y)[None, :, None] + 0.5 - cy
    zs = (np.arange(Z)[None, None, :] + 0.5 - cz) / squash
    v[(xs * xs) + (ys * ys) + (zs * zs) <= (r * r)] = colour


def dome_ribs(v, colour, base_colour, step=4, z0=0, z1=None):
    """丸屋根の**縦筋**。`base_colour` で塗ってある所だけを塗り替える。

    **矩形で塗ってはならない**（罠）——初版は `box(...)` で全面に縦の板を立てていて、
    屋根の上に**青い鰭が何枚も生えた**（絵で見つけた。`--prefab=` の 1 枚が無ければ
    quads の数だけでは気づけない）。既にある面だけを塗り替えれば筋になる。
    """
    X, _, Z = v.shape
    z1 = Z if z1 is None else z1
    mask = (v == base_colour)
    mask[:, :, :z0] = False
    mask[:, :, z1:] = False
    for a in range(0, X, step):
        sel = np.zeros_like(mask)
        sel[a:a + 1, :, :] = True
        v[mask & sel] = colour


def dome(v, cx, cy, z0, r, colour, rim=0.0):
    """丸屋根（半球）。`z0` から上だけを残す。`rim` を渡すと殻だけにする。"""
    X, Y, Z = v.shape
    xs = np.arange(X)[:, None, None] + 0.5 - cx
    ys = np.arange(Y)[None, :, None] + 0.5 - cy
    zs = np.arange(Z)[None, None, :] + 0.5 - z0
    d2 = (xs * xs) + (ys * ys) + (zs * zs)
    m = (d2 <= (r * r)) & (zs >= 0)
    if rim > 0:
        m &= d2 >= (rim * rim)
    v[m] = colour


def octagon(v, cx, cy, r, z0, z1, colour):
    """八角の柱（32 ボクセルの円柱は糸巻きに見える。モリバントの記録）。"""
    X, Y, Z = v.shape
    xs = np.abs(np.arange(X)[:, None] + 0.5 - cx)
    ys = np.abs(np.arange(Y)[None, :] + 0.5 - cy)
    m = (xs <= r) & (ys <= r) & ((xs + ys) <= (r * 1.42))
    z0, z1 = max(0, z0), min(Z, z1)
    if z0 < z1:
        v[:, :, z0:z1][m] = colour


def hollow(v, depth=6):
    """中身を空ける（大物の `.vox` 膨れを防ぐ。`angwil_prefabs.py` と同じ道具）。"""
    core = v != 0
    for _ in range(depth):
        nxt = core.copy()
        nxt[1:, :, :] &= core[:-1, :, :]
        nxt[:-1, :, :] &= core[1:, :, :]
        nxt[:, 1:, :] &= core[:, :-1, :]
        nxt[:, :-1, :] &= core[:, 1:, :]
        nxt[:, :, 1:] &= core[:, :, :-1]
        nxt[:, :, :-1] &= core[:, :, 1:]
        nxt[0, :, :] = False
        nxt[-1, :, :] = False
        nxt[:, 0, :] = False
        nxt[:, -1, :] = False
        core &= ~nxt
    v[core] = 0
    return v


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


def _slice(name):
    return G['_slice_of'](name)


def _outward(name):
    return G['SLICE_OUT'][_slice(name)]


def static_part(motion=None, wind_k=0.0):
    return G['static_part'](motion, wind_k)


def wind_parts(names, wind_k=0.6):
    """`frame`（静止・接地）＋風で揺れる布の部品。`telmora_stone_prefabs.py` と同じ形。"""
    parts = [{'name': 'frame', 'voxels': 'frame', 'grounded': True,
              'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    for name in names:
        parts.append({'name': name, 'voxels': name, 'parent': 'frame', 'grounded': False,
                      'motion': {'kind': 'wind'}, 'wind_k': float(wind_k)})
    return parts


def swing_parts(name, pivot, axis='x', amplitude=6.0, period=2.4, phase=0.0):
    """`frame`（静止・接地）＋振り子で揺れる部品 1 つ。

    **支点は必ずその部品のボクセルの中に置く**（`load_prefab` の照合。`gen_prefabs.py` の
    `fountain_parts` の註記）。鐘は冠の中心、看板は板の吊り元がそれに当たる。
    """
    return [
        {'name': 'frame', 'voxels': 'frame', 'grounded': True,
         'motion': {'kind': 'static'}, 'wind_k': 0.0},
        {'name': name, 'voxels': name, 'parent': 'frame', 'grounded': False,
         'pivot': [float(pivot[0]), float(pivot[1]), float(pivot[2])],
         'motion': {'kind': 'pendulum', 'axis': axis, 'amplitude': float(amplitude),
                    'period': float(period), 'phase': float(phase), 'damping': 0.0},
         'wind_k': 0.0},
    ]


# ------------------------------------------------------------------ 色
def _rl(name, rgb):
    """`reg_local` の包み。**既にある名前は上書きしない**（別の町の色を奪う）。"""
    if name in C:
        raise ValueError('色の名前が既にあります: %s' % name)
    G['reg_local'](name, rgb)


def _palette():
    # ---- アナンバール（§4.1 の色。赤い焼き煉瓦・すす・鋳鉄・白石・金）----
    #! 赤い焼き煉瓦（壁・塀・煙突）。3 階調。**頭は `wall_`**（冒頭の表。`brick_` は
    #! `palette_materials.KEYWORDS` に無く、材質が Default に落ちる）。
    _rl('wall_ana_brick_d', (0x8C, 0x4A, 0x38))
    _rl('wall_ana_brick', (0xA0, 0x5A, 0x44))
    _rl('wall_ana_brick_l', (0xB5, 0x6C, 0x52))
    #! 煉瓦の目地（1 ボクセルの筋）。設計の 3 階調より一段暗い影の色（実装で足した）。
    _rl('wall_ana_joint', (0x6A, 0x38, 0x2C))
    #! すす色のスレート（屋根・櫓の屋根）。設計値 0x2E2A28 を中に、上下 1 段ずつ。
    _rl('roof_ana_slate_d', (0x22, 0x1F, 0x1E))
    _rl('roof_ana_slate', (0x2E, 0x2A, 0x28))
    _rl('roof_ana_slate_l', (0x3E, 0x3A, 0x37))
    #! 鋳鉄（柵・縁石・手すり・街灯・門扉）。
    _rl('iron_ana', (0x1E, 0x20, 0x24))
    _rl('iron_ana_l', (0x33, 0x37, 0x3E))
    #! 郵便の柱の赤い鋳鉄（1 色）。
    _rl('iron_ana_red', (0xA0, 0x32, 0x2A))
    #! 白い切石（台輪・窓枠・笠石）。
    _rl('rock_ana_ash', (0xD8, 0xD4, 0xCC))
    _rl('rock_ana_ash_d', (0xB2, 0xAE, 0xA6))
    #! 車道の黒い舗石。3 階調（目地は `_d`）。
    _rl('pav_ana_road_d', (0x2C, 0x2E, 0x32))
    _rl('pav_ana_road', (0x3A, 0x3D, 0x42))
    _rl('pav_ana_road_l', (0x48, 0x4B, 0x51))
    #! 歩道と広場の平らな敷石。3 階調。
    _rl('pav_ana_flag_d', (0x84, 0x80, 0x78))
    _rl('pav_ana_flag', (0x9A, 0x96, 0x8C))
    _rl('pav_ana_flag_l', (0xAC, 0xA8, 0x9E))
    #! 金（像・時計の針・飾り）。設計は「既存の `gold_`」だが、**読み込みの順に依存させない**
    #! ためこの生成器で登録し直す（別の町の `gold_gilt` は借りない）。
    _rl('gold_ana', (0xC8, 0xA8, 0x48))
    #! ガス灯の硝子（明るい暖色）。**`glass_` で始める**——`iron_` で登録すると夜に光って見えない
    #! （テルモラの記録 §9.9 #4）。
    _rl('glass_ana', (0xFF, 0xC8, 0x78))
    #! 警邏の詰め所の青い灯り。
    _rl('glass_ana_blue', (0x4C, 0x8C, 0xC8))
    #! 噴水の水面（静止の 1 色）。
    _rl('glass_ana_water', (0x5C, 0x6C, 0x7A))
    #! 街路樹の葉（刈り込まない低い並木。2 階調）と幹。
    _rl('leaf_ana', (0x4E, 0x6E, 0x3A))
    _rl('leaf_ana_d', (0x38, 0x52, 0x2C))
    _rl('timber_ana', (0x5A, 0x46, 0x34))

    # ---- タロス（§4.2 の色。砂 3 階調・漆喰の白・青いタイル・木・棕櫚の葉）----
    _rl('wall_tha_sand_l', (0xD9, 0xC6, 0x9C))
    _rl('wall_tha_sand', (0xC4, 0xAE, 0x84))
    _rl('wall_tha_sand_d', (0xA8, 0x8E, 0x68))
    #! 漆喰の白（塀・胸壁・尖塔）。
    _rl('plas_tha_white', (0xED, 0xE4, 0xD2))
    _rl('plas_tha_white_d', (0xCF, 0xC5, 0xB2))
    #! 青い施釉タイル（帯・丸屋根・水盤）。3 階調。
    _rl('tile_tha_blue_d', (0x2E, 0x6E, 0x9E))
    _rl('tile_tha_blue', (0x3E, 0x8A, 0xBE))
    _rl('tile_tha_blue_l', (0x6F, 0xB4, 0xDC))
    #! 木（窓の格子・戸・卓）。
    _rl('timber_tha', (0x6B, 0x4A, 0x2E))
    _rl('timber_tha_l', (0x87, 0x62, 0x40))
    #! 棕櫚と糸杉の葉。2 階調。
    _rl('leaf_tha', (0x5A, 0x7A, 0x3A))
    _rl('leaf_tha_d', (0x40, 0x59, 0x2A))
    #! 布（日除けと天幕）。生成りに赤の縞。
    _rl('cloth_tha', (0xE8, 0xDC, 0xC4))
    _rl('cloth_tha_r', (0xB4, 0x44, 0x3C))
    _rl('cloth_tha_d', (0xC9, 0xB8, 0x96))
    #! 砂色の敷石（通りと広場）。3 階調。
    _rl('pav_tha_d', (0xB4, 0x9E, 0x78))
    _rl('pav_tha', (0xCB, 0xB8, 0x94))
    _rl('pav_tha_l', (0xDC, 0xC9, 0xA6))
    #! 白い石（尖塔の帯・水盤の縁）。
    _rl('rock_tha_white', (0xF0, 0xED, 0xE4))
    _rl('rock_tha_white_d', (0xCD, 0xC8, 0xBC))
    #! 吊りランタンの鉄と、その灯りの硝子。
    _rl('iron_tha', (0x2A, 0x28, 0x24))
    _rl('glass_tha', (0xFF, 0xD2, 0x8C))
    #! 水盤の水面（静止の 1 色）。
    _rl('glass_tha_water', (0x4A, 0x86, 0xA8))
    #! 金（宮殿の門と尖塔の頂の飾り）。
    _rl('gold_tha', (0xC8, 0xA8, 0x48))


# ------------------------------------------------------------------ 建物の寸法（両方の意匠で共通）
WALL_H = 34    #!< 壁 1 層の高さ（基＝`HOUSE_WALL_H` と同じ。階はこれを積んで作る）
ROOF_H = 30    #!< 寄棟屋根の高さ（アナンバール）
ROOF_H_FLAT = 24   #!< 陸屋根の高さ（タロス。丸屋根のぶんだけ余裕を取る）
EAVE = 4
INSET = 2
#: 窓の高さ（壁の中の z）。**`house_light_*` はこの高さに合わせる**——置く側は灯りを
#: 「その階の足元 + 0.40 マス」に置く（`terrain_view.cpp`）ので、灯りの中の z は
#: ここから 12.8 ≒ 13 を引いた値になる。
WIN_Z0, WIN_Z1 = 14, 26
LIGHT_LIFT = 13   #!< 置く側の 0.40 マス（≒12.8 ボクセル）


def _face_slice(x0, x1, y0, y1, side):
    """外に面した 1 ボクセルの面を (x スライス, y スライス) で返す。"""
    if side == 'n':
        return (slice(x0, x1), slice(y0, y0 + 1))
    if side == 's':
        return (slice(x0, x1), slice(y1 - 1, y1))
    if side == 'w':
        return (slice(x0, x0 + 1), slice(y0, y1))
    return (slice(x1 - 1, x1), slice(y0, y1))


def _face_put(v, side, x0, x1, y0, y1, a0, a1, z0, z1, colour, depth=1, offset=0):
    """面に沿った帯を置く。

    `a0..a1` は辺に沿う軸、`depth` は帯の厚み、**`offset` は面からの深さ**
    （0 = いちばん外の 1 ボクセル、1 = その 1 つ奥、**負なら外へ張り出す**）。

    **`offset` を持たせたのは罠を踏んだから**である。初版は `depth` しか無く、
    「開口を 1 ボクセル彫ってから硝子を `depth=2` で置く」と書いたところ、
    彫った層まで硝子で埋め戻して**窓が 1 つも凹まなかった**（`--prefab-check` の
    quads が 10 ＝ ただの箱）。彫りと詰めは別の層を指さなければならない。
    """
    if side == 'n':
        box(v, a0, a1, y0 + offset, y0 + offset + depth, z0, z1, colour)
    elif side == 's':
        box(v, a0, a1, y1 - offset - depth, y1 - offset, z0, z1, colour)
    elif side == 'w':
        box(v, x0 + offset, x0 + offset + depth, a0, a1, z0, z1, colour)
    else:
        box(v, x1 - offset - depth, x1 - offset, a0, a1, z0, z1, colour)


def _face_span(x0, x1, y0, y1, side):
    """辺に沿う軸の範囲（`n`/`s` なら x、`w`/`e` なら y）。"""
    return (x0, x1) if side in ('n', 's') else (y0, y1)


def _faces_of(name):
    north, south, west, east = _outward(name)
    return [s for s, on in (('n', north), ('s', south), ('w', west), ('e', east)) if on]


def _body_bounds(name):
    north, south, west, east = _outward(name)
    return (INSET if west else 0, V - INSET if east else V,
            INSET if north else 0, V - INSET if south else V)


# ================================================================== アナンバール（_ana）
def _brick_courses(v, x0, x1, y0, y1, z0, z1, course=4):
    """煉瓦の積み。**1 段 1 色**（塊ごとに 1 色）で 3 階調を順に、段の境に 1 ボクセルの目地。"""
    #! 段の色は**3 段に 1 度だけ**変える。毎段変えた版は、絵で見ると丸太小屋の
    #! 横縞に見えた（`--prefab=` の 1 枚で気づいた）。目地も `wall_ana_joint` では
    #! 濃すぎたので、1 段暗い煉瓦（`_d`）にしてある。
    tones = (K('wall_ana_brick'), K('wall_ana_brick'), K('wall_ana_brick_l'))
    i, z = 0, z0
    while z < z1:
        top = min(z1, z + course)
        box(v, x0, x1, y0, y1, z, top, tones[i % len(tones)])
        box(v, x0, x1, y0, y1, top - 1, top, K('wall_ana_brick_d'))
        z = top
        i += 1


def _sash_window(v, side, x0, x1, y0, y1, at, width=6):
    """縦長の上げ下げ窓 1 つ。**開口を面から 1 ボクセル彫り**、その 1 つ奥へ硝子、
    彫った面へ中桟、外へ**白い切石の枠を 1 ボクセル張り出す**（躯体は `INSET`＝2 だけ
    引っ込んでいるので、張り出してもマスからは出ない）。
    """
    lo, hi = _face_span(x0, x1, y0, y1, side)
    a0 = max(lo + 2, min(hi - width - 2, at - (width // 2)))
    a1 = a0 + width
    # 開口（面の 1 層を抜く）と硝子（その 1 つ奥）。
    _face_put(v, side, x0, x1, y0, y1, a0, a1, WIN_Z0, WIN_Z1, 0, offset=0)
    _face_put(v, side, x0, x1, y0, y1, a0, a1, WIN_Z0, WIN_Z1, K('glass'), offset=1)
    # 上げ下げ窓の中桟（真ん中の横木 1 本と縦の召し合わせ 1 本）。彫った層に置く。
    mid = (WIN_Z0 + WIN_Z1) // 2
    _face_put(v, side, x0, x1, y0, y1, a0, a1, mid, mid + 1, K('rock_ana_ash'), offset=0)
    _face_put(v, side, x0, x1, y0, y1, a0 + (width // 2), a0 + (width // 2) + 1,
              WIN_Z0, WIN_Z1, K('rock_ana_ash'), offset=0)
    # 白い切石の枠（外へ 1 ボクセル）。方立 2 本・窓台・楣。
    for b0, b1 in ((a0 - 1, a0), (a1, a1 + 1)):
        _face_put(v, side, x0, x1, y0, y1, b0, b1, WIN_Z0 - 1, WIN_Z1 + 1,
                  K('rock_ana_ash'), offset=-1)
    _face_put(v, side, x0, x1, y0, y1, a0 - 2, a1 + 2, WIN_Z0 - 3, WIN_Z0 - 1,
              K('rock_ana_ash'), offset=-1)
    _face_put(v, side, x0, x1, y0, y1, a0 - 2, a1 + 2, WIN_Z1 + 1, WIN_Z1 + 3,
              K('rock_ana_ash_d'), offset=-1)


def house_wall_ana(name):
    """煉瓦の商館の壁 1 層。白い切石の台輪 → 煉瓦の躯体（腰に石の帯）→ 縦長の上げ下げ窓
    → 軒の鋸歯の煉瓦の蛇腹。§4.1「3 階の煉瓦の商館」。
    """
    x0, x1, y0, y1 = _body_bounds(name)
    faces = _faces_of(name)
    v = vol(h=WALL_H)
    #! 白い切石の台輪。**マスいっぱい**（土台に隙間を作らない）。
    box(v, 0, V, 0, V, 0, 4, K('rock_ana_ash_d'))
    box(v, 0, V, 0, V, 3, 4, K('rock_ana_ash'))
    body0, body1 = 4, WALL_H - 4
    _brick_courses(v, x0, x1, y0, y1, body0, body1)
    #! 腰の石の帯（白い切石。2 段）。外に面した辺だけ 1 ボクセル張り出す。
    box(v, x0, x1, y0, y1, 10, 12, K('rock_ana_ash'))
    box(v, x0, x1, y0, y1, 12, 13, K('rock_ana_ash_d'))
    for side in faces:
        lo, hi = _face_span(x0, x1, y0, y1, side)
        _face_put(v, side, x0, x1, y0, y1, lo, hi, 10, 12, K('rock_ana_ash'), offset=-1)
        _face_put(v, side, x0, x1, y0, y1, lo, hi, 12, 13, K('rock_ana_ash_d'), offset=-1)
    #! 縦長の上げ下げ窓（面ごとに 2 つ）。
    for side in faces:
        lo, hi = _face_span(x0, x1, y0, y1, side)
        q = max(4, (hi - lo) // 4)
        _sash_window(v, side, x0, x1, y0, y1, lo + q)
        _sash_window(v, side, x0, x1, y0, y1, hi - q)
    #! 軒の鋸歯の蛇腹。外へ 1 ボクセル出した帯に、**2 ボクセルおきにもう 1 段出す歯**
    #! を並べる（鋸歯）。その上に白い切石の笠を 2 ボクセル張り出す。
    box(v, x0, x1, y0, y1, body1, WALL_H, K('wall_ana_brick_d'))
    for side in faces:
        lo, hi = _face_span(x0, x1, y0, y1, side)
        _face_put(v, side, x0, x1, y0, y1, lo, hi, body1, body1 + 2, K('wall_ana_brick_l'), offset=-1)
        for a in range(lo, hi, 3):
            _face_put(v, side, x0, x1, y0, y1, a, a + 2, body1, body1 + 2,
                      K('wall_ana_brick_d'), offset=-2)
        _face_put(v, side, x0, x1, y0, y1, lo, hi, body1 + 2, WALL_H,
                  K('rock_ana_ash_d'), depth=2, offset=-2)
    return one(v)


def house_roof_ana(name):
    """すす色のスレートの寄棟。軒に煉瓦の蛇腹、外に面した辺へ**鉄の手すり**。§4.1。"""
    north, south, west, east = _outward(name)
    height, hip = G['_roof_field'](north, south, west, east, EAVE, ROOF_H, 1.0)
    v = vol(h=ROOF_H)
    #! **胴は 1 色**（すす色のスレート）。段の縞は**いちばん上のボクセルだけ**に付ける。
    #! 段ごとに 2 色を交ぜた初版は、斜面の段差と縞が重なって**波板の屋根**に見えた
    #! （絵で気づいた。quads では判らない）。
    for x in range(V):
        for y in range(V):
            h = int(height[x, y])
            for z in range(h):
                if z < 2:
                    v[x, y, z] = K('wall_ana_brick_d')       # 軒の煉瓦
                elif z < EAVE:
                    v[x, y, z] = K('rock_ana_ash_d')         # 石の水切り
                else:
                    v[x, y, z] = K('roof_ana_slate_d')
            if h > EAVE:
                row = (h - EAVE) // 5
                v[x, y, h - 1] = K('roof_ana_slate') if (row % 2 == 0) else K('roof_ana_slate_l')
            if (h > EAVE) and hip[x, y]:
                v[x, y, h - 1] = K('roof_ana_slate_l')       # 隅棟
    #! 鉄の手すり（外に面した辺の際。1 つおきに柱、上に横木）。
    for side, on in (('n', north), ('s', south), ('w', west), ('e', east)):
        if not on:
            continue
        for a in range(1, V, 3):
            _face_put(v, side, 0, V, 0, V, a, a + 1, EAVE, EAVE + 7, K('iron_ana'), depth=2)
        _face_put(v, side, 0, V, 0, V, 0, V, EAVE + 6, EAVE + 8, K('iron_ana_l'), depth=2)
    return one(v)


def house_light_ana(name):
    """窓の灯り（南面の上げ下げ窓 2 つ）。**`house_wall_s_ana` の窓と同じ座標**へ置く
    ——置く側は灯りをその階の足元 + 0.40 マスに置くので、ここでは `WIN_Z0 - LIGHT_LIFT` から。
    """
    v = vol(h=WIN_Z1 - LIGHT_LIFT + 1)
    y1 = V - INSET
    for at in (8, 24):
        a0 = at - 3
        box(v, a0, a0 + 6, y1 - 2, y1, WIN_Z0 - LIGHT_LIFT, WIN_Z1 - LIGHT_LIFT, K('lit'))
    return one(v)


def house_entrance_ana(name):
    """見た目だけの玄関。白い切石の段 3 段・鉄の両開き戸・石の楣と切妻の飾り・
    両脇にガス灯の腕木。**戸口は手前（y 大）**。§4.1。
    """
    v = vol(h=36)
    y1 = 31
    for i, z in enumerate((0, 1, 2)):
        box(v, 8 - i, 24 + i, y1 - i, y1 + 1, z, z + 1, K('rock_ana_ash'))
    box(v, 6, 26, y1 - 1, y1 + 1, 3, 26, K('wall_ana_brick'))         # 戸口の煉瓦の枠
    box(v, 6, 8, y1 - 1, y1 + 1, 3, 26, K('rock_ana_ash'))            # 白い切石の方立
    box(v, 24, 26, y1 - 1, y1 + 1, 3, 26, K('rock_ana_ash'))
    box(v, 5, 27, y1 - 1, y1 + 1, 26, 29, K('rock_ana_ash'))          # 楣
    box(v, 4, 28, y1 - 1, y1 + 1, 29, 31, K('rock_ana_ash_d'))        # 切妻の飾り
    box(v, 12, 20, y1 - 1, y1 + 1, 31, 33, K('rock_ana_ash_d'))
    box(v, 8, 24, y1, y1 + 1, 3, 24, K('iron_ana'))                   # 鉄の両開き戸
    box(v, 15, 17, y1, y1 + 1, 3, 24, K('iron_ana_l'))                # 召し合わせ
    for z in (7, 13, 19):
        for x in (10, 12, 20, 22):
            v[x, y1, z] = K('gold_ana')                               # 真鍮の鋲
    box(v, 9, 23, y1, y1 + 1, 24, 26, K('glass'))                     # 欄間の硝子
    for x0, x1 in ((4, 6), (26, 28)):                                 # ガス灯の腕木
        box(v, x0, x1, y1 - 1, y1 + 1, 20, 24, K('iron_ana'))
        box(v, x0, x1, y1 - 1, y1 + 1, 24, 27, K('glass_ana'))
    return one(v)


def fence_ana(name):
    """敷地の柵。**煉瓦の腰壁**（高さ 6）に**鋳鉄の柵**（渦巻きの飾り）。
    高さは基と同じ 14（「視線を遮らない」約束）。
    """
    side = _slice(name)
    v = vol(h=14)
    th = 4
    lo = 0 if side in ('n', 'w') else V - th
    hi = lo + th

    def put(a0, a1, z0, z1, colour):
        if side in ('n', 's'):
            box(v, a0, a1, lo, hi, z0, z1, colour)
        else:
            box(v, lo, hi, a0, a1, z0, z1, colour)

    put(0, V, 0, 6, K('wall_ana_brick'))          # 煉瓦の腰壁
    put(0, V, 3, 4, K('wall_ana_joint'))          # 目地
    put(0, V, 5, 6, K('rock_ana_ash_d'))          # 白い切石の笠
    for p in range(1, V, 3):                      # 鉄の縦格子
        put(p, p + 1, 6, 13, K('iron_ana'))
    for p in range(2, V, 6):                      # 渦巻きの飾り（格子の間の玉）
        put(p, p + 2, 8, 10, K('iron_ana_l'))
    put(0, V, 12, 14, K('iron_ana_l'))            # 笠木
    for p in range(0, V, 14):                     # 煉瓦の親柱
        put(p, p + 4, 0, 14, K('wall_ana_brick_d'))
        put(p, p + 4, 13, 14, K('rock_ana_ash'))
    return one(v)


def palisade_ana(name):
    """町を囲む煉瓦の塀。白い切石の台輪と笠石、8 ボクセルおきの控え柱。高さ 48。"""
    v = vol(h=48)
    box(v, 0, V, 0, V, 0, 4, K('rock_ana_ash_d'))
    _brick_courses(v, 0, V, 0, V, 4, 42)
    for side in ('n', 's', 'w', 'e'):
        for a in range(0, V, 10):                 # 控え柱（外へ 1 ボクセル出す）
            _face_put(v, side, 0, V, 0, V, a, a + 4, 4, 42, K('wall_ana_brick_d'))
    box(v, 0, V, 0, V, 42, 45, K('rock_ana_ash'))     # 笠石
    box(v, 0, V, 0, V, 45, 46, K('rock_ana_ash_d'))
    for p in range(1, V, 3):                          # 天端の鉄の忍び返し
        box(v, p, p + 1, 14, 18, 46, 48, K('iron_ana'))
        box(v, 14, 18, p, p + 1, 46, 48, K('iron_ana'))
    return one(v)


def watchtower_ana(name):
    """塀の隅の煉瓦の塔。石の帯・持ち送りの張り出し・鉄の手すり・スレートの方形屋根。
    **塀（48）より頭ひとつ高い**という約束は守る（高さ 96）。
    """
    height = 96
    v = vol(h=height)
    box(v, 4, 28, 4, 28, 0, 4, K('rock_ana_ash_d'))
    _brick_courses(v, 6, 26, 6, 26, 4, 66)
    for z in (20, 40, 58):                                   # 白い切石の帯
        box(v, 5, 27, 5, 27, z, z + 2, K('rock_ana_ash'))
    for side in ('n', 's', 'w', 'e'):                        # 縦長の窓（面ごとに 1 つ）
        _face_put(v, side, 6, 26, 6, 26, 14, 18, 44, 56, K('glass'), depth=2)
    box(v, 3, 29, 3, 29, 66, 70, K('rock_ana_ash'))          # 持ち送りの張り出し
    for a in range(4, 28, 4):                                # 鉄の手すり
        box(v, a, a + 1, 3, 29, 70, 78, K('iron_ana'))
        box(v, 3, 29, a, a + 1, 70, 78, K('iron_ana'))
    box(v, 3, 29, 3, 29, 77, 79, K('iron_ana_l'))
    for k in range(height - 79):                             # スレートの方形屋根
        r = 12 - k
        if r < 1:
            break
        box(v, 16 - r, 16 + r, 16 - r, 16 + r, 79 + k, 80 + k,
            K('roof_ana_slate') if (k % 2 == 0) else K('roof_ana_slate_d'))
    box(v, 15, 17, 15, 17, height - 3, height, K('gold_ana'))
    hollow(v, depth=5)
    return one(v)


def yard_ground_ana(name):
    """前庭。敷石（歩道より暗い——**前庭は街路より暗い**という約束）に鉄の排水の格子。"""
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('pav_ana_road_d'))
    layer = _bond_pavers(rng, [K('pav_ana_flag_d'), K('pav_ana_road_l'), K('pav_ana_flag')],
                         [0.5, 0.3, 0.2], K('pav_ana_road_d'))
    layer[13:19, 13:19] = K('iron_ana')
    for a in range(14, 19, 2):
        layer[a, 13:19] = K('iron_ana_l')
    v[:, :, 1] = layer
    return ground(v, 2)


def chimney_ana(name):
    """屋根の煙突（`roof_vent`）。煉瓦の煙突 2 本と石の笠。§4.1「煙突を 2 本」。"""
    v = vol(h=26)
    for x0 in (8, 18):
        _brick_courses(v, x0, x0 + 7, 12, 20, 0, 20, course=3)
        box(v, x0 - 1, x0 + 8, 11, 21, 20, 22, K('rock_ana_ash_d'))   # 笠石
        box(v, x0 + 1, x0 + 6, 13, 19, 22, 25, K('roof_ana_slate_d'))  # 煙出しの筒
        box(v, x0 + 2, x0 + 5, 14, 18, 24, 26, K('soot'))
    return one(v)


# ---- アナンバールの街路と地面 ----
def _bond_pavers(rng, tones, weights, joint_colour):
    """大判の敷石を 8×8 と 8×16 で段違いに組んだ層（目地は 1 ボクセル）。"""
    layer = np.full((V, V), joint_colour, np.int16)
    y, row_i = 0, 0
    while y < V:
        y1 = min(V, y + 8)
        xs, w = ((0, 16), 16) if (row_i % 2 == 0) else ((0, 8, 16, 24), 8)
        for x in xs:
            x1 = min(V, x + w)
            layer[x + 1:x1, y + 1:y1] = pick(rng, tones, weights)
        y = y1
        row_i += 1
    return layer


def _setts(rng, tones, weights, joint_colour):
    """小舗石の層（車道。歩道の大判とはっきり大きさを分ける）。目地 1 ボクセル。"""
    layer = np.zeros((V, V), np.int16)
    p = 0
    while p < V:
        w = int(rng.integers(3, 6))
        layer[p:min(V, p + w), :] = pick(rng, tones, weights)
        p += w + 1
    layer[layer == 0] = joint_colour
    q = 0
    while q < V:
        w = int(rng.integers(3, 6))
        if (q + w) < V:
            layer[:, q + w] = joint_colour
        q += w + 1
    return layer


def ground_path_ana(name):
    """車道。黒い舗石（小さな塊・目地 1 ボクセル・3 階調）。§4.1「車道は黒い舗石」。"""
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('pav_ana_road_d'))
    v[:, :, 1] = _setts(rng, [K('pav_ana_road_d'), K('pav_ana_road'), K('pav_ana_road_l')],
                        [0.3, 0.42, 0.28], K('pav_ana_road_d'))
    return ground(v, 2)


def ground_turf_ana(name):
    """歩道。平らな敷石（大判・段違い）。**車道より明るい**ことで車道と歩道が分かれる。"""
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('pav_ana_road_d'))
    v[:, :, 1] = _bond_pavers(rng, [K('pav_ana_flag_d'), K('pav_ana_flag'), K('pav_ana_flag_l')],
                              [0.24, 0.46, 0.30], K('pav_ana_road_d'))
    return ground(v, 2)


def ground_plaza_ana(name):
    """広場の敷石。歩道と同じ大判だが**半段明るく**、対角に暗い帯を 1 本だけ通す
    （どの変種でも同じ位置なので、並べたマスをまたいで続いて見える）。
    """
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('pav_ana_road_d'))
    layer = _bond_pavers(rng, [K('pav_ana_flag'), K('pav_ana_flag_l')], [0.35, 0.65],
                         K('pav_ana_road_d'))
    xs_idx = np.arange(V)[:, None]
    ys_idx = np.arange(V)[None, :]
    layer[np.abs(xs_idx - ys_idx) < 6] = K('pav_ana_flag_d')
    v[:, :, 1] = layer
    return ground(v, 2)


def kerb_ana(name):
    """鋳鉄の縁石（`path_edge`）。別体の小物として地面の上へ乗るので原点は z=0。
    回転せずに辺へ寄せて置くので 4 辺すべてに帯を持つ。
    """
    v = vol(h=4)
    for a0, a1 in ((0, 4), (V - 4, V)):
        box(v, a0, a1, 0, V, 0, 3, K('rock_ana_ash_d'))
        box(v, 0, V, a0, a1, 0, 3, K('rock_ana_ash_d'))
    for a in (1, V - 2):
        box(v, a, a + 1, 0, V, 3, 4, K('iron_ana'))
        box(v, 0, V, a, a + 1, 3, 4, K('iron_ana'))
    return one(v)


def lamp_post_ana(name):
    """ガス灯。石の沓石に鋳鉄の柱、頂に**ガラスの箱**（4 面）と鉄の笠。火は別体。"""
    v = vol(h=72)
    box(v, 12, 20, 12, 20, 0, 5, K('rock_ana_ash_d'))
    box(v, 12, 20, 12, 20, 4, 5, K('rock_ana_ash'))
    box(v, 14, 18, 14, 18, 5, 10, K('iron_ana_l'))       # 根元の飾り台
    box(v, 15, 18, 15, 18, 10, 56, K('iron_ana'))        # 柱
    for z in range(16, 56, 12):
        box(v, 14, 19, 14, 19, z, z + 1, K('iron_ana_l'))
    box(v, 12, 21, 12, 21, 56, 58, K('iron_ana_l'))      # 受け
    z0, z1 = 58, 68
    for cx, cy in ((12, 12), (20, 12), (12, 20), (20, 20)):
        box(v, cx, cx + 1, cy, cy + 1, z0, z1, K('iron_ana'))
    box(v, 13, 20, 12, 13, z0 + 1, z1 - 1, K('glass_ana'))
    box(v, 13, 20, 20, 21, z0 + 1, z1 - 1, K('glass_ana'))
    box(v, 12, 13, 13, 20, z0 + 1, z1 - 1, K('glass_ana'))
    box(v, 20, 21, 13, 20, z0 + 1, z1 - 1, K('glass_ana'))
    box(v, 11, 22, 11, 22, z1, z1 + 2, K('iron_ana'))    # 笠
    box(v, 15, 18, 15, 18, z1 + 2, z1 + 4, K('iron_ana_l'))
    return one(v)


def lamp_flame_ana(name):
    """ガス灯の火（別体。夜だけ置く側が自発光を足す）。ガラスの箱の中いっぱいに置く。"""
    v = vol(h=72)
    box(v, 14, 19, 14, 19, 59, 67, K('lit'))
    return one(v)


# ---- アナンバールの街路樹と 1 点物 ----
def street_tree_ana(name):
    """街路樹。**刈り込まない低い並木**（鉄の木枠つき）。幹は細く、樹冠は横に広い。"""
    v = vol(h=44)
    box(v, 9, 23, 9, 23, 0, 1, K('iron_ana'))            # 鉄の木枠
    box(v, 11, 21, 11, 21, 0, 1, K('pav_ana_flag_d'))
    for a in range(10, 22, 3):
        v[a, 9:23, 0] = K('iron_ana_l')
    box(v, 15, 18, 15, 18, 1, 20, K('timber_ana'))       # 幹
    ball(v, 16, 16, 26, 9.5, K('leaf_ana_d'), squash=0.8)
    ball(v, 16, 16, 33, 7.5, K('leaf_ana_d'), squash=0.9)
    dark = (v == K('leaf_ana_d'))
    lightmask = np.zeros_like(dark)
    lightmask[:, 16:, :] = True                          # 南面（+y）
    lightmask[:, :, 34:] = True                          # 上面
    v[dark & lightmask] = K('leaf_ana')
    return one(v)


def clock_tower_ana(name):
    """**時計塔**（市長室の前）。3×3 マス・高さ約 11 マスの煉瓦の塔。白い切石の帯、
    南面に金の針の文字盤、鐘楼にスレートの尖り屋根。**鐘は振り子**（別部品）。
    """
    S, H = 96, 352
    frame = np.zeros((S, S, H), np.int16)
    cx = cy = 48
    box(frame, 8, 88, 8, 88, 0, 8, K('rock_ana_ash_d'))          # 基壇（3×3 マスの接地）
    box(frame, 10, 86, 10, 86, 8, 12, K('rock_ana_ash'))
    #! 塔身（煉瓦。上へわずかに細る 3 段）。
    for i, (a0, a1, z0, z1) in enumerate(((16, 80, 12, 130), (18, 78, 130, 236), (20, 76, 236, 300))):
        _brick_courses(frame, a0, a1, a0, a1, z0, z1, course=6)
        box(frame, a0 - 2, a1 + 2, a0 - 2, a1 + 2, z1 - 4, z1, K('rock_ana_ash'))   # 白い切石の帯
    #! 縦長の窓（4 面。塔身の中ほど）。
    for side in ('n', 's', 'w', 'e'):
        _face_put(frame, side, 18, 78, 18, 78, 42, 54, 150, 200, K('glass'), depth=2)
    #! 文字盤（4 面。白い切石の円に金の針）。
    for side, (fx, fy) in (('n', (None, 18)), ('s', (None, 76)), ('w', (18, None)), ('e', (76, None))):
        if side in ('n', 's'):
            y0 = 18 if side == 'n' else 76
            disc_face = np.s_[30:66, y0:y0 + 2, 244:280]
        else:
            x0 = 18 if side == 'w' else 76
            disc_face = np.s_[x0:x0 + 2, 30:66, 244:280]
        frame[disc_face] = K('rock_ana_ash')
    for side in ('n', 's', 'w', 'e'):                       # 針（金。長針と短針）
        if side in ('n', 's'):
            y0 = 17 if side == 'n' else 77
            frame[47:49, y0:y0 + 1, 262:276] = K('gold_ana')
            frame[48:58, y0:y0 + 1, 261:263] = K('gold_ana')
        else:
            x0 = 17 if side == 'w' else 77
            frame[x0:x0 + 1, 47:49, 262:276] = K('gold_ana')
            frame[x0:x0 + 1, 48:58, 261:263] = K('gold_ana')
    #! 鐘楼（開いた四隅の柱と鉄の手すり）。
    box(frame, 20, 76, 20, 76, 300, 304, K('rock_ana_ash'))
    for a0 in (20, 68):
        for b0 in (20, 68):
            box(frame, a0, a0 + 8, b0, b0 + 8, 304, 328, K('wall_ana_brick'))
    box(frame, 20, 76, 20, 76, 328, 332, K('rock_ana_ash'))
    for a in range(22, 76, 4):
        box(frame, a, a + 1, 20, 76, 304, 312, K('iron_ana'))
        box(frame, 20, 76, a, a + 1, 304, 312, K('iron_ana'))
    #! スレートの尖り屋根と金の頂。
    for k in range(332, H - 6):
        r = max(1, 30 - int((k - 332) * 1.5))
        box(frame, cx - r, cx + r, cy - r, cy + r, k, k + 1,
            K('roof_ana_slate') if ((k // 3) % 2 == 0) else K('roof_ana_slate_d'))
    box(frame, cx - 2, cx + 2, cy - 2, cy + 2, H - 6, H, K('gold_ana'))
    hollow(frame, depth=4)
    #! 鐘（別部品。振り子。**支点は鐘の冠の中**に置く）。
    bell = np.zeros((S, S, H), np.int16)
    box(bell, cx - 2, cx + 2, cy - 2, cy + 2, 322, 326, K('iron_ana_l'))    # 吊り金具（支点の周り）
    for k in range(14):
        r = 5 + int(k * 0.6)
        box(bell, cx - r, cx + r, cy - r, cy + r, 322 - k, 323 - k, K('gold_ana'))
    box(bell, cx - 2, cx + 2, cy - 2, cy + 2, 305, 310, K('iron_ana'))      # 舌
    return [('frame', frame, (0, 0, 0)), ('bell', bell, (0, 0, 0))]


def clock_tower_ana_parts(name=None):
    return swing_parts('bell', (48.0, 48.0, 324.0), axis='x', amplitude=7.0, period=2.6)


def statue_gold_ana(name):
    """**金の像**（拝金の神殿の前）。黒い切石の台座に、金貨を掲げた金の立像。高さ 2.5 マス。"""
    v = vol(h=80)
    box(v, 8, 24, 8, 24, 0, 8, K('rock_ana_ash_d'))
    box(v, 9, 23, 9, 23, 7, 8, K('rock_ana_ash'))
    box(v, 10, 22, 10, 22, 8, 12, K('wall_ana_brick_d'))     # 煉瓦の腰
    box(v, 12, 20, 13, 19, 12, 40, K('gold_ana'))            # 胴
    box(v, 11, 21, 12, 20, 12, 16, K('gold_ana'))            # 沓
    box(v, 10, 22, 13, 19, 38, 42, K('gold_ana'))            # 肩（横へ広げる）
    box(v, 13, 19, 13, 19, 42, 50, K('gold_ana'))            # 頭
    box(v, 20, 23, 14, 18, 30, 54, K('gold_ana'))            # 掲げた腕
    disc(v, 21, 16, 5.5, 54, 56, K('gold_ana'))              # 金貨
    box(v, 10, 13, 14, 18, 22, 40, K('gold_ana'))            # もう一方の腕（袋を提げる）
    box(v, 8, 13, 12, 20, 14, 24, K('gold_ana'))
    return one(v)


def notice_pillar_ana(name):
    """**掲示の柱**（新聞記者のギルド）。八角の柱に刷り物を貼り、頂に鉄の笠。高さ 2 マス。"""
    v = vol(h=64)
    octagon(v, 16, 16, 11.0, 0, 3, K('rock_ana_ash_d'))
    octagon(v, 16, 16, 9.0, 3, 50, K('wall_ana_brick'))
    for z in range(6, 48, 12):                              # 貼り紙の帯（白い切石の色を借りる）
        octagon(v, 16, 16, 9.5, z, z + 9, K('rock_ana_ash'))
        octagon(v, 16, 16, 9.5, z + 9, z + 10, K('rock_ana_ash_d'))
    octagon(v, 16, 16, 11.0, 50, 53, K('iron_ana'))         # 笠
    octagon(v, 16, 16, 7.0, 53, 58, K('iron_ana_l'))
    box(v, 15, 18, 15, 18, 58, 62, K('gold_ana'))           # 頂の飾り
    return one(v)


def post_pillar_ana(name):
    """**郵便の柱**。赤い鋳鉄の丸柱に投入口と金の帯、頂は丸い笠。高さ 1.3 マス。"""
    v = vol(h=44)
    octagon(v, 16, 16, 9.0, 0, 2, K('iron_ana'))
    octagon(v, 16, 16, 8.0, 2, 34, K('iron_ana_red'))
    box(v, 11, 21, 22, 24, 24, 27, K('iron_ana'))           # 投入口（南面）
    octagon(v, 16, 16, 8.5, 28, 30, K('gold_ana'))          # 金の帯
    octagon(v, 16, 16, 9.5, 34, 36, K('iron_ana'))          # 笠
    ball(v, 16, 16, 37, 6.0, K('iron_ana_red'), squash=0.7)
    box(v, 15, 18, 15, 18, 40, 43, K('gold_ana'))
    return one(v)


def fountain_iron_ana(name):
    """**鋳鉄の噴水**。石の水盤に鉄の柱と 2 段の受け皿。水面は静止の 1 色。"""
    v = vol(h=40)
    disc(v, 16, 16, 14.0, 0, 4, K('rock_ana_ash_d'))
    disc(v, 16, 16, 14.0, 3, 4, K('rock_ana_ash'))
    disc(v, 16, 16, 11.5, 4, 9, K('rock_ana_ash'), hollow_r=10.0)
    disc(v, 16, 16, 10.0, 4, 7, K('glass_ana_water'))       # 水面
    box(v, 14, 18, 14, 18, 7, 30, K('iron_ana'))            # 鉄の柱
    disc(v, 16, 16, 7.0, 16, 18, K('iron_ana_l'))           # 受け皿（下）
    disc(v, 16, 16, 5.0, 26, 28, K('iron_ana_l'))           # 受け皿（上）
    ball(v, 16, 16, 33, 4.0, K('gold_ana'), squash=1.2)     # 頂の飾り
    return one(v)


def guard_post_ana(name):
    """**警邏の詰め所**。煉瓦の小屋に鉄の戸と青い灯り。1 マス・高さ 2 マス。"""
    v = vol(h=64)
    box(v, 2, 30, 2, 30, 0, 3, K('rock_ana_ash_d'))
    _brick_courses(v, 3, 29, 3, 29, 3, 44)
    box(v, 12, 20, 27, 30, 4, 26, K('iron_ana'))            # 戸（南＝手前）
    box(v, 15, 17, 27, 30, 4, 26, K('iron_ana_l'))
    box(v, 6, 12, 28, 30, 14, 26, K('glass'))               # 窓
    box(v, 20, 26, 28, 30, 14, 26, K('glass'))
    box(v, 2, 30, 2, 30, 44, 47, K('rock_ana_ash'))         # 蛇腹
    for k in range(10):                                     # スレートの寄棟
        r = 14 - k
        if r < 1:
            break
        box(v, 16 - r, 16 + r, 16 - r, 16 + r, 47 + k, 48 + k,
            K('roof_ana_slate') if (k % 2 == 0) else K('roof_ana_slate_d'))
    box(v, 14, 19, 14, 19, 56, 60, K('iron_ana'))           # 青い灯りの台
    box(v, 13, 20, 13, 20, 60, 64, K('glass_ana_blue'))
    hollow(v, depth=4)
    return one(v)


def loading_dock_ana(name):
    """**荷積みの台**。煉瓦の腰に石の天板、鉄の縁と車止め、木の渡し板。高さ 0.4 マス。"""
    v = vol(h=16)
    _brick_courses(v, 0, V, 0, 24, 0, 10, course=3)
    #! 天板は歩道と同じ敷石（白い切石は絵で見ると光って浮いた）。
    box(v, 0, V, 0, 24, 10, 12, K('pav_ana_flag'))          # 天板
    box(v, 0, V, 0, 24, 11, 12, K('pav_ana_flag_l'))
    box(v, 0, V, 22, 24, 10, 13, K('iron_ana'))             # 縁の鉄
    for x in (4, 15, 26):                                   # 車止め
        box(v, x, x + 3, 24, 27, 0, 6, K('iron_ana'))
    box(v, 8, 24, 24, 32, 8, 10, K('timber_ana'))           # 渡し板（手前へ下ろす）
    for x in range(9, 24, 4):
        box(v, x, x + 1, 24, 32, 10, 11, K('timber_ana'))
    return one(v)


def sign_arm_ana(name):
    """**鉄の看板の腕木**。柱と腕木は静止、**吊った板は振り子**（別部品）。
    支点は板の吊り元（＝板のボクセルの中）に置く。
    """
    frame = vol(h=64)
    box(frame, 12, 20, 12, 20, 0, 4, K('rock_ana_ash_d'))
    box(frame, 14, 18, 14, 18, 4, 52, K('iron_ana'))            # 柱
    box(frame, 14, 30, 15, 17, 46, 48, K('iron_ana'))           # 腕木（東へ張り出す）
    for k in range(6):                                          # 方杖（斜めの支え）
        box(frame, 18 + k, 20 + k, 15, 17, 40 - k, 42 - k, K('iron_ana'))
    box(frame, 13, 19, 13, 19, 52, 55, K('iron_ana_l'))         # 柱頭
    box(frame, 15, 17, 15, 17, 55, 58, K('gold_ana'))
    sign = vol(h=64)
    box(sign, 23, 25, 15, 17, 42, 46, K('iron_ana'))            # 吊り金具（支点の周り）
    box(sign, 18, 30, 15, 17, 30, 44, K('iron_ana_l'))          # 板
    box(sign, 19, 29, 15, 17, 32, 42, K('gold_ana'))            # 金の縁取り
    box(sign, 21, 27, 15, 17, 34, 40, K('iron_ana'))
    return [('frame', frame, (0, 0, 0)), ('sign', sign, (0, 0, 0))]


def sign_arm_ana_parts(name=None):
    return swing_parts('sign', (24.0, 16.0, 44.0), axis='y', amplitude=8.0, period=1.9)


# ================================================================== タロス（_tha）
def _adobe_courses(v, x0, x1, y0, y1, z0, z1, course=6):
    """日干し煉瓦と漆喰。砂色 3 階調を段ごとに（**1 段 1 色**）。段の境は彫らない
    （漆喰で塗り込めるので、テルモラの切石のような目地は出ない）。
    """
    tones = (K('wall_tha_sand'), K('wall_tha_sand_l'), K('wall_tha_sand'), K('wall_tha_sand_d'))
    i, z = 0, z0
    while z < z1:
        top = min(z1, z + course)
        box(v, x0, x1, y0, y1, z, top, tones[i % len(tones)])
        z = top
        i += 1


def _arch_face(v, side, x0, x1, y0, y1, a0, a1, z0, shoulder, rise, colour, depth=1, offset=0):
    """**尖頭アーチ**の面を塗る（`colour` が 0 なら彫る）。肩まで四角、その上は中央が尖る。"""
    w = a1 - a0
    for i in range(w):
        t = abs((i + 0.5) - (w / 2.0)) / (w / 2.0)
        top = shoulder + int(round(rise * ((1.0 - t) ** 0.85)))
        _face_put(v, side, x0, x1, y0, y1, a0 + i, a0 + i + 1, z0, top, colour, depth, offset)


def _mashrabiya(v, side, x0, x1, y0, y1, a0, a1, z0, z1):
    """窓の木の格子。**彫った層**（面から 1 ボクセル奥）に縦横の桟を組む。
    奥の硝子との間に 1 ボクセルの段差ができるので、格子が影を落として読める。
    """
    for a in range(a0, a1, 2):
        _face_put(v, side, x0, x1, y0, y1, a, a + 1, z0, z1, K('timber_tha'), offset=0)
    for z in range(z0, z1, 3):
        _face_put(v, side, x0, x1, y0, y1, a0, a1, z, z + 1, K('timber_tha_l'), offset=0)


def house_wall_tha(name):
    """砂色の日干し煉瓦と漆喰の壁 1 層。**尖頭アーチの窓**（木の格子つき）、腰と軒に
    青い施釉タイルの帯。§4.2「2〜3 階の陸屋根」。
    """
    x0, x1, y0, y1 = _body_bounds(name)
    faces = _faces_of(name)
    v = vol(h=WALL_H)
    #! 石の台輪（**マスいっぱい**）。
    box(v, 0, V, 0, V, 0, 3, K('wall_tha_sand_d'))
    box(v, 0, V, 0, V, 2, 3, K('plas_tha_white_d'))
    body0, body1 = 3, WALL_H - 3
    _adobe_courses(v, x0, x1, y0, y1, body0, body1)
    #! 腰の青いタイルの帯（2 段）。外に面した辺だけ 1 ボクセル張り出す。
    box(v, x0, x1, y0, y1, 8, 10, K('tile_tha_blue_d'))
    box(v, x0, x1, y0, y1, 9, 10, K('tile_tha_blue'))
    for side in faces:
        lo, hi = _face_span(x0, x1, y0, y1, side)
        _face_put(v, side, x0, x1, y0, y1, lo, hi, 8, 9, K('tile_tha_blue_d'), offset=-1)
        _face_put(v, side, x0, x1, y0, y1, lo, hi, 9, 10, K('tile_tha_blue'), offset=-1)
    #! 尖頭アーチの窓（面ごとに 2 つ。開口を 1 ボクセル彫り、奥に硝子、面に木の格子）。
    for side in faces:
        lo, hi = _face_span(x0, x1, y0, y1, side)
        q = max(5, (hi - lo) // 4)
        for at in (lo + q, hi - q):
            a0 = max(lo + 1, min(hi - 9, at - 4))
            a1 = a0 + 8
            #! 白い縁取り（外へ 1 ボクセル張り出す）→ **その中を抜いて枠にする** →
            #! 壁の開口を 1 彫る → 奥に硝子 → 木の格子。
            #! **枠の中を抜くのを忘れると窓が 1 つも見えない**（塗った白い板が開口の前に
            #! 立ちはだかる。絵で気づいた——quads は増えるので数では判らない）。
            _arch_face(v, side, x0, x1, y0, y1, a0 - 1, a1 + 1, WIN_Z0 - 2, WIN_Z1, 7,
                       K('plas_tha_white'), offset=-1)
            _arch_face(v, side, x0, x1, y0, y1, a0, a1, WIN_Z0, WIN_Z1 - 1, 6, 0, offset=-1)
            _arch_face(v, side, x0, x1, y0, y1, a0, a1, WIN_Z0, WIN_Z1 - 1, 6, 0, offset=0)
            _arch_face(v, side, x0, x1, y0, y1, a0, a1, WIN_Z0, WIN_Z1 - 1, 6,
                       K('glass'), offset=1)
            _mashrabiya(v, side, x0, x1, y0, y1, a0, a1, WIN_Z0, WIN_Z1 - 4)
    #! 軒（陸屋根の水切り）。漆喰の白 1 段の上に、外へ 2 ボクセル張り出す青いタイルの帯。
    box(v, x0, x1, y0, y1, body1, body1 + 1, K('wall_tha_sand_d'))
    box(v, x0, x1, y0, y1, body1 + 1, WALL_H, K('plas_tha_white'))
    for side in faces:
        lo, hi = _face_span(x0, x1, y0, y1, side)
        _face_put(v, side, x0, x1, y0, y1, lo, hi, body1 + 1, WALL_H - 1,
                  K('tile_tha_blue'), depth=2, offset=-2)
        _face_put(v, side, x0, x1, y0, y1, lo, hi, WALL_H - 1, WALL_H,
                  K('plas_tha_white_d'), depth=2, offset=-2)
    return one(v)


def house_roof_tha(name):
    """陸屋根。砂色の屋上に**低い胸壁**（外に面した辺）と青いタイルの帯。
    建物の中ほど（`mid`）にだけ**青い丸屋根**を載せる（§4.2「要所に青い丸屋根」——
    外に面した辺を持つスライスに載せると、小さな家にまでドームが乗って町が散る）。
    """
    north, south, west, east = _outward(name)
    v = vol(h=ROOF_H_FLAT)
    box(v, 0, V, 0, V, 0, 3, K('wall_tha_sand_d'))          # 屋根の版
    box(v, 0, V, 0, V, 3, 4, K('wall_tha_sand_l'))          # 屋上の床
    for side, on in (('n', north), ('s', south), ('w', west), ('e', east)):
        if not on:
            continue
        _face_put(v, side, 0, V, 0, V, 0, V, 0, 3, K('plas_tha_white'), depth=2)     # 水切り
        _face_put(v, side, 0, V, 0, V, 0, V, 4, 10, K('plas_tha_white'), depth=3)    # 胸壁
        _face_put(v, side, 0, V, 0, V, 0, V, 8, 9, K('tile_tha_blue'), depth=3)      # タイルの帯
        for a in range(0, V, 4):                                                     # 鋸歯の天端
            _face_put(v, side, 0, V, 0, V, a, a + 2, 10, 13, K('plas_tha_white'), depth=3)
    if not (north or south or west or east):
        #! 建物の中ほど（`mid`）。青いタイルの丸屋根と細い尖り。
        octagon(v, 16, 16, 11.0, 4, 8, K('plas_tha_white'))
        dome(v, 16, 16, 8, 11.0, K('tile_tha_blue'))
        dome_ribs(v, K('tile_tha_blue_d'), K('tile_tha_blue'))   # 丸屋根の縦筋（2 階調）
        box(v, 15, 18, 15, 18, 19, 22, K('gold_tha'))
    return one(v)


def house_light_tha(name):
    """窓の灯り（南面の尖頭アーチの窓 2 つ）。**`house_wall_s_tha` の窓と同じ座標**。"""
    v = vol(h=WIN_Z1 - LIGHT_LIFT + 1)
    y1 = V - INSET
    for at in (8, 24):
        a0 = max(1, min(V - 9, at - 4))
        box(v, a0, a0 + 8, y1 - 2, y1, WIN_Z0 - LIGHT_LIFT, WIN_Z1 - LIGHT_LIFT - 2, K('lit'))
    return one(v)


def house_entrance_tha(name):
    """見た目だけの玄関。**尖頭アーチの戸口**に青いタイルの枠、木の両開き戸と真鍮の鋲。
    **戸口は手前（y 大）**。§4.2。
    """
    v = vol(h=36)
    y1 = 31
    box(v, 7, 25, y1 - 1, y1 + 1, 0, 2, K('plas_tha_white'))           # 沓脱ぎ
    box(v, 5, 27, y1 - 1, y1 + 1, 2, 24, K('tile_tha_blue_d'))         # 枠（青いタイル）
    box(v, 6, 26, y1 - 1, y1 + 1, 2, 24, K('tile_tha_blue'))
    #! 尖頭アーチの開口（中央が尖る）。
    for x in range(7, 25):
        t = abs((x + 0.5) - 16.0) / 9.0
        top = 18 + int(round(8 * ((1.0 - t) ** 0.85)))
        box(v, x, x + 1, y1 - 1, y1 + 1, 2, top, 0)
        box(v, x, x + 1, y1, y1 + 1, 2, top, K('timber_tha'))          # 木の戸
        if (x % 3) == 0:
            box(v, x, x + 1, y1, y1 + 1, 2, top, K('timber_tha_l'))    # 縦板の目地
    box(v, 15, 17, y1, y1 + 1, 2, 24, K('timber_tha_l'))               # 召し合わせ
    for z in (8, 14, 20):
        for x in (10, 13, 19, 22):
            v[x, y1, z] = K('gold_tha')                                # 真鍮の鋲
    box(v, 4, 28, y1 - 1, y1 + 1, 24, 27, K('plas_tha_white'))         # 楣
    box(v, 4, 28, y1 - 1, y1 + 1, 27, 28, K('tile_tha_blue'))
    for x0, x1 in ((3, 5), (27, 29)):                                  # 吊りランタンの腕木
        box(v, x0, x1, y1 - 1, y1 + 1, 22, 26, K('iron_tha'))
        box(v, x0, x1, y1 - 1, y1 + 1, 19, 22, K('glass_tha'))
    return one(v)


def fence_tha(name):
    """敷地の塀。漆喰の塀に**青いタイルの帯**、上に**鋸歯の胸壁**。
    高さは基と同じ 14（「視線を遮らない」約束）。
    """
    side = _slice(name)
    v = vol(h=14)
    th = 4
    lo = 0 if side in ('n', 'w') else V - th
    hi = lo + th

    def put(a0, a1, z0, z1, colour):
        if side in ('n', 's'):
            box(v, a0, a1, lo, hi, z0, z1, colour)
        else:
            box(v, lo, hi, a0, a1, z0, z1, colour)

    put(0, V, 0, 2, K('wall_tha_sand_d'))       # 礎
    put(0, V, 2, 10, K('plas_tha_white'))       # 漆喰の塀
    put(0, V, 6, 8, K('tile_tha_blue'))         # 青いタイルの帯
    put(0, V, 7, 8, K('tile_tha_blue_d'))
    for p in range(0, V, 4):                    # 鋸歯の胸壁
        put(p, p + 2, 10, 14, K('plas_tha_white'))
        put(p, p + 2, 13, 14, K('plas_tha_white_d'))
    return one(v)


def palisade_tha(name):
    """町を囲む漆喰の塀。砂色の礎・青いタイルの帯・**鋸歯の胸壁**。高さ 48。"""
    v = vol(h=48)
    box(v, 0, V, 0, V, 0, 6, K('wall_tha_sand_d'))
    _adobe_courses(v, 0, V, 0, V, 6, 34, course=7)
    box(v, 0, V, 0, V, 34, 38, K('plas_tha_white'))
    box(v, 0, V, 0, V, 30, 32, K('tile_tha_blue'))          # 青いタイルの帯
    box(v, 0, V, 0, V, 31, 32, K('tile_tha_blue_d'))
    for side in ('n', 's', 'w', 'e'):                       # 控え柱
        for a in range(0, V, 12):
            _face_put(v, side, 0, V, 0, V, a, a + 3, 6, 34, K('wall_tha_sand_l'))
    #! 鋸歯の胸壁。**塊を足すのではなく、通した胸壁に切り込みを入れる**（縦横の両方へ
    #! 塊を並べた初版は、天端がマス目の格子に見えた——絵で気づいた）。切り込みは基の
    #! `palisade()` と同じく 1 方向だけに入れる。
    box(v, 0, V, 0, V, 38, 48, K('plas_tha_white'))
    box(v, 0, V, 0, V, 47, 48, K('plas_tha_white_d'))
    for p in range(2, V, 6):
        box(v, p, p + 3, 0, V, 42, 48, 0)
    return one(v)


def watchtower_tha(name):
    """塀の隅の丸い塔。漆喰の胴に青いタイルの帯、頂は**青いタイルの丸屋根**。
    **塀（48）より頭ひとつ高い**（高さ 96）。
    """
    height = 96
    v = vol(h=height)
    octagon(v, 16, 16, 14.0, 0, 6, K('wall_tha_sand_d'))
    octagon(v, 16, 16, 12.5, 6, 66, K('plas_tha_white'))
    for z in (24, 46):
        octagon(v, 16, 16, 13.0, z, z + 3, K('tile_tha_blue'))
        octagon(v, 16, 16, 13.0, z + 2, z + 3, K('tile_tha_blue_d'))
    for side in ('n', 's', 'w', 'e'):                     # 細い窓
        _face_put(v, side, 3, 29, 3, 29, 14, 18, 52, 62, K('timber_tha'), depth=2)
    octagon(v, 16, 16, 14.5, 66, 70, K('plas_tha_white'))  # 持ち送り
    for a in range(2, 30, 4):                              # 低い胸壁
        box(v, a, a + 2, 2, 30, 70, 76, K('plas_tha_white'))
        box(v, 2, 30, a, a + 2, 70, 76, K('plas_tha_white'))
    octagon(v, 16, 16, 11.0, 70, 76, K('plas_tha_white_d'))
    dome(v, 16, 16, 76, 12.0, K('tile_tha_blue'))          # 青い丸屋根
    dome_ribs(v, K('tile_tha_blue_d'), K('tile_tha_blue'), z0=76)
    box(v, 15, 18, 15, 18, 88, 94, K('gold_tha'))
    hollow(v, depth=5)
    return one(v)


def yard_ground_tha(name):
    """前庭。踏んだ砂に**幾何のタイル**（青い星形の中心）。**街路より暗い**砂を地にする。"""
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('wall_tha_sand_d'))
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('wall_tha_sand_d'), K('pav_tha_d'), K('wall_tha_sand')],
                 [0.5, 0.3, 0.2])
    #! 中央の幾何のタイル（八角の中に星）。**塊ごとに 1 色**。
    xs = np.arange(V)[:, None]
    ys = np.arange(V)[None, :]
    ax, ay = np.abs(xs - 16), np.abs(ys - 16)
    octa = (ax <= 9) & (ay <= 9) & ((ax + ay) <= 13)
    layer[octa] = K('tile_tha_blue_d')
    star = ((ax + ay) <= 8) | ((ax <= 2) & (ay <= 9)) | ((ay <= 2) & (ax <= 9))
    layer[octa & star] = K('tile_tha_blue_l')
    layer[octa & (((ax + ay) <= 4))] = K('plas_tha_white')
    v[:, :, 1] = layer
    return ground(v, 2)


def chimney_tha(name):
    """屋根の風抜きの塔（`roof_vent`）。陸屋根の町に煙突は立たないので、砂色の小塔に
    縦の抜きを開けた**風抜き**にした（青いタイルの帯つき）。設計から外れた点は §7 に記す。
    """
    v = vol(h=28)
    box(v, 10, 22, 10, 22, 0, 20, K('wall_tha_sand'))
    box(v, 10, 22, 10, 22, 8, 10, K('tile_tha_blue'))
    for side in ('n', 's', 'w', 'e'):                     # 縦の抜き（風の口）
        for a in range(12, 21, 3):
            _face_put(v, side, 10, 22, 10, 22, a, a + 2, 12, 19, 0, depth=2)
    box(v, 8, 24, 8, 24, 20, 23, K('plas_tha_white'))     # 笠
    box(v, 8, 24, 8, 24, 22, 23, K('tile_tha_blue_d'))
    box(v, 14, 19, 14, 19, 23, 27, K('wall_tha_sand_l'))
    return one(v)


# ---- タロスの街路と地面 ----
def ground_path_tha(name):
    """通り。砂色の敷石（大判を段違いに）。"""
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('wall_tha_sand_d'))
    v[:, :, 1] = _bond_pavers(rng, [K('pav_tha_d'), K('pav_tha'), K('pav_tha_l')],
                              [0.26, 0.44, 0.30], K('wall_tha_sand_d'))
    return ground(v, 2)


def ground_turf_tha(name):
    """道でない開けたマス。踏んだ砂に**乾いた草の株**を疎らに（塊で置く。1 ボクセルは砂嵐に見える）。"""
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('wall_tha_sand_d'))
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('wall_tha_sand'), K('wall_tha_sand_l'), K('pav_tha_d')],
                 [0.46, 0.32, 0.22])
    #! 草は**小さな塊で控えめに**（block=3・1 割は絵で見ると緑のマス目が目立ちすぎた）。
    G['_grain'](rng, layer, layer > 0, [K('leaf_tha_d'), K('leaf_tha')], [0.05, 0.03], block=2)
    v[:, :, 1] = layer
    return ground(v, 2)


def ground_plaza_tha(name):
    """広場。砂色の敷石に**青いタイルの縁取り**（マスの外周 2 ボクセル）。並べると
    タイルの格子が通りに続いて見える。
    """
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('wall_tha_sand_d'))
    layer = _bond_pavers(rng, [K('pav_tha'), K('pav_tha_l')], [0.4, 0.6], K('wall_tha_sand_d'))
    layer[0:2, :] = K('tile_tha_blue_d')
    layer[V - 2:V, :] = K('tile_tha_blue_d')
    layer[:, 0:2] = K('tile_tha_blue_d')
    layer[:, V - 2:V] = K('tile_tha_blue_d')
    layer[14:18, 14:18] = K('tile_tha_blue')
    v[:, :, 1] = layer
    return ground(v, 2)


def lamp_post_tha(name):
    """街灯＝**吊りランタン**。鉄の柱と腕木から、星形の透かしのランタンを提げる。火は別体。"""
    v = vol(h=64)
    box(v, 12, 20, 12, 20, 0, 4, K('wall_tha_sand_d'))
    box(v, 13, 19, 13, 19, 3, 4, K('plas_tha_white'))
    box(v, 15, 18, 15, 18, 4, 52, K('iron_tha'))            # 柱
    box(v, 15, 18, 8, 17, 48, 50, K('iron_tha'))            # 腕木（南へ張り出す）
    for k in range(4):                                      # 方杖
        box(v, 15, 18, 13 - k, 15 - k, 42 + k, 44 + k, K('iron_tha'))
    box(v, 14, 19, 14, 19, 52, 55, K('iron_tha'))
    box(v, 15, 18, 15, 18, 55, 58, K('gold_tha'))           # 柱頭
    #! ランタン（八角）。**面は硝子・鉄は骨だけ**にする——鉄の八角で塗り潰した初版は、
    #! 中の硝子が 1 画素も見えず**夜に光らない黒い箱**になっていた（絵で気づいた。
    #! テルモラの街灯で踏んだのと同じ罠 §9.9 #4）。南面に星形の透かしを打つ。
    octagon(v, 16, 12, 4.6, 34, 46, K('glass_tha'))
    octagon(v, 16, 12, 5.2, 33, 36, K('iron_tha'))          # 下の枠
    octagon(v, 16, 12, 5.2, 44, 47, K('iron_tha'))          # 上の枠
    for dx, dy in ((-4, 0), (4, 0), (0, -4), (0, 4), (-3, -3), (3, -3), (-3, 3), (3, 3)):
        box(v, 16 + dx, 17 + dx, 12 + dy, 13 + dy, 34, 46, K('iron_tha'))   # 縦の骨
    for dx, dz in ((0, 0), (-2, 0), (2, 0), (0, -2), (0, 2), (-1, -1), (1, -1), (-1, 1), (1, 1)):
        box(v, 16 + dx, 17 + dx, 15, 17, 39 + dz, 40 + dz, K('iron_tha'))   # 星形の透かし（南面）
    box(v, 15, 18, 11, 14, 46, 49, K('iron_tha'))           # 吊り鎖
    octagon(v, 16, 12, 5.6, 45, 47, K('iron_tha'))          # 笠
    octagon(v, 16, 12, 3.0, 31, 34, K('iron_tha'))          # 裾の飾り
    return one(v)


def lamp_flame_tha(name):
    """吊りランタンの火（別体。夜だけ置く側が自発光を足す）。"""
    v = vol(h=64)
    box(v, 14, 19, 10, 15, 36, 44, K('lit'))
    return one(v)


# ---- タロスの街路樹と 1 点物 ----
def palm_tha(name):
    """**棕櫚**。節のある幹（上へ細る）に、放射に開いた葉。高さ約 3.4 マス。"""
    v = vol(h=110)
    box(v, 12, 20, 12, 20, 0, 2, K('wall_tha_sand_d'))
    rng = rng_for(name)
    lean = int(rng.integers(-2, 3))
    for z in range(2, 76):                                  # 幹（節を 1 ボクセルの帯で）
        r = 4 - int(z / 30)
        cx = 16 + int(round(lean * (z / 76.0)))
        box(v, cx - r, cx + r, 16 - r, 16 + r, z, z + 1,
            K('timber_tha_l') if ((z % 6) == 0) else K('timber_tha'))
    cx = 16 + lean
    #! 葉（8 方向。付け根から先へ下がる）。
    for j in range(8):
        ang = j * 0.785
        dx, dy = np.cos(ang), np.sin(ang)
        for k in range(1, 15):
            px = cx + int(round(dx * k))
            py = 16 + int(round(dy * k))
            pz = 78 + int(round(4.0 - (k * k) * 0.035))
            wide = 2 if (k < 10) else 1
            box(v, px - wide, px + wide, py - wide, py + wide, pz, pz + 2,
                K('leaf_tha') if (j % 2 == 0) else K('leaf_tha_d'))
    box(v, cx - 3, cx + 3, 13, 19, 74, 80, K('leaf_tha_d'))  # 葉の付け根
    box(v, cx - 2, cx + 2, 14, 18, 70, 76, K('timber_tha_l'))
    return one(v)


def cypress_tha(name):
    """**糸杉**。細く高い暗緑の柱。高さ約 3.6 マス。"""
    v = vol(h=116)
    box(v, 12, 20, 12, 20, 0, 2, K('wall_tha_sand_d'))
    box(v, 14, 18, 14, 18, 2, 24, K('timber_tha'))
    for z in range(14, 112):                                 # 樹冠（紡錘）
        t = (z - 14) / 98.0
        r = 7.5 * (1.0 - abs(t - 0.35) * 1.15)
        if r < 0.8:
            continue
        disc(v, 16, 16, r, z, z + 1, K('leaf_tha_d') if ((z % 5) < 3) else K('leaf_tha'))
    for z in range(20, 108, 8):                              # 南面（+y）を明るく
        disc(v, 16, 18, 4.0, z, z + 2, K('leaf_tha'))
    return one(v)


def awning_tha(name):
    """**日除けの布**（通りをまたぐ）。両端の柱と竿は静止、**布は風**（別部品）。"""
    frame = vol(h=52)
    for x0 in (0, 28):
        box(frame, x0, x0 + 4, 14, 18, 0, 44, K('timber_tha'))       # 柱
        box(frame, x0, x0 + 4, 13, 19, 0, 3, K('wall_tha_sand_d'))   # 礎
        box(frame, x0, x0 + 4, 14, 18, 42, 44, K('timber_tha_l'))
    box(frame, 0, V, 15, 17, 44, 46, K('timber_tha_l'))              # 竿
    box(frame, 0, V, 15, 17, 46, 47, K('iron_tha'))
    cloth = vol(h=52)
    box(cloth, 1, V - 1, 10, 22, 42, 44, K('cloth_tha'))             # 布（東西に張る）
    for x in range(2, V - 2, 5):                                     # 赤の縞
        box(cloth, x, x + 2, 10, 22, 42, 44, K('cloth_tha_r'))
    box(cloth, 1, V - 1, 21, 22, 39, 44, K('cloth_tha_d'))           # 南の垂れ
    box(cloth, 1, V - 1, 10, 11, 39, 44, K('cloth_tha_d'))           # 北の垂れ
    return [('frame', frame, (0, 0, 0)), ('cloth', cloth, (0, 0, 0))]


def minaret_tha(name):
    """**尖塔**（町でいちばん高い。4 本立てる）。八角の白い軸に青いタイルの帯、
    中ほどに張り出しの回廊、頂に小さな丸屋根と金の飾り。高さ 10 マス。
    """
    H = 320
    v = vol(h=H)
    octagon(v, 16, 16, 14.0, 0, 8, K('wall_tha_sand_d'))
    octagon(v, 16, 16, 12.0, 8, 20, K('plas_tha_white_d'))
    for z in range(20, 200):                                  # 軸（上へ細る）
        r = 10.0 - (3.5 * ((z - 20) / 180.0))
        octagon(v, 16, 16, r, z, z + 1, K('plas_tha_white'))
    for z in range(30, 196, 26):                              # 青いタイルの帯
        octagon(v, 16, 16, 10.0, z, z + 3, K('tile_tha_blue'))
        octagon(v, 16, 16, 10.0, z + 2, z + 3, K('tile_tha_blue_d'))
    octagon(v, 16, 16, 12.0, 200, 205, K('plas_tha_white_d'))  # 回廊の張り出し
    for a in range(2, 30, 4):                                  # 回廊の手すり
        box(v, a, a + 2, 4, 28, 205, 213, K('plas_tha_white'))
        box(v, 4, 28, a, a + 2, 205, 213, K('plas_tha_white'))
    for z in range(205, 268):                                  # 上の軸
        r = 6.5 - (1.5 * ((z - 205) / 63.0))
        octagon(v, 16, 16, r, z, z + 1, K('plas_tha_white'))
    octagon(v, 16, 16, 8.0, 268, 272, K('plas_tha_white_d'))
    dome(v, 16, 16, 272, 8.0, K('tile_tha_blue'))              # 小さな丸屋根
    dome_ribs(v, K('tile_tha_blue_d'), K('tile_tha_blue'), step=3, z0=272)
    box(v, 15, 18, 15, 18, 280, 300, K('gold_tha'))            # 金の針
    ball(v, 16, 16, 304, 4.0, K('gold_tha'), squash=1.3)
    hollow(v, depth=4)
    return one(v)


def dome_gate_tha(name):
    """**宮殿の門**。3 マスに渡る**尖頭アーチ**と青いタイルの枠。中の 1 マスは通れる
    （接地するのは両脇の門柱だけなので、`footprint` は 2 マスになる）。
    """
    S, H = 96, 160
    v = np.zeros((S, S, H), np.int16)
    #! 門柱 2 本（左右のマス）。
    for x0 in (0, 64):
        box(v, x0, x0 + 32, 8, 24, 0, 6, K('wall_tha_sand_d'))
        _adobe_courses(v, x0 + 2, x0 + 30, 9, 23, 6, 108, course=8)
        box(v, x0 + 1, x0 + 31, 8, 24, 40, 44, K('tile_tha_blue'))
        box(v, x0 + 1, x0 + 31, 8, 24, 43, 44, K('tile_tha_blue_d'))
        box(v, x0, x0 + 32, 7, 25, 108, 114, K('plas_tha_white'))
        for a in range(x0 + 2, x0 + 32, 6):                    # 鋸歯の天端
            box(v, a, a + 4, 7, 25, 114, 122, K('plas_tha_white'))
    #! 尖頭アーチ（中の 1 マスをまたぐ。接地しない）。
    for x in range(32, 64):
        t = abs((x + 0.5) - 48.0) / 16.0
        springing = 68
        top = springing + int(round(40 * ((1.0 - t) ** 0.85)))
        box(v, x, x + 1, 9, 23, springing, top + 14, K('wall_tha_sand'))
        box(v, x, x + 1, 8, 9, springing, top + 14, K('tile_tha_blue'))      # 北面のタイル
        box(v, x, x + 1, 23, 24, springing, top + 14, K('tile_tha_blue'))    # 南面のタイル
        box(v, x, x + 1, 9, 23, springing, top, 0)                           # くぐる口を抜く
    box(v, 30, 66, 8, 24, 108, 114, K('plas_tha_white'))       # 楣
    box(v, 30, 66, 8, 24, 114, 118, K('tile_tha_blue_d'))
    for a in range(32, 66, 6):
        box(v, a, a + 4, 7, 25, 118, 126, K('plas_tha_white'))
    box(v, 40, 56, 10, 22, 126, 130, K('plas_tha_white_d'))    # 中央の丸屋根の台
    dome(v, 48, 16, 130, 12.0, K('tile_tha_blue'))
    box(v, 46, 51, 14, 19, 140, 150, K('gold_tha'))
    hollow(v, depth=4)
    return one(v)


def basin_tha(name):
    """**水盤**（青いタイル）。八角の縁に静止した水面。1 マス。"""
    v = vol(h=12)
    octagon(v, 16, 16, 14.0, 0, 5, K('plas_tha_white'))
    octagon(v, 16, 16, 14.0, 4, 5, K('rock_tha_white'))
    octagon(v, 16, 16, 12.0, 3, 9, K('tile_tha_blue_d'), )
    octagon(v, 16, 16, 10.5, 3, 8, K('glass_tha_water'))
    for a in range(0, V, 4):                                   # 縁のタイルの割り付け
        box(v, a, a + 2, 0, V, 4, 5, K('tile_tha_blue'))
        box(v, 0, V, a, a + 2, 4, 5, K('tile_tha_blue'))
    octagon(v, 16, 16, 3.0, 8, 11, K('rock_tha_white'))        # 中央の噴き口
    return one(v)


def market_tent_tha(name):
    """**市場の天幕**（3 種）。木の枠は静止、**日除けの布は風**（別部品）。
    変種は名前の末尾の番号で分ける（品物の並べ方と布の縞が変わる）。
    """
    kind = int(name[-2:]) if name[-2:].isdigit() else 1
    frame = vol(h=56)
    for x0, y0 in ((2, 4), (26, 4), (2, 24), (26, 24)):
        box(frame, x0, x0 + 4, y0, y0 + 4, 0, 40, K('timber_tha'))
        box(frame, x0, x0 + 4, y0, y0 + 4, 38, 40, K('timber_tha_l'))
    box(frame, 2, 30, 4, 8, 40, 42, K('timber_tha_l'))          # 桁
    box(frame, 2, 30, 24, 28, 40, 42, K('timber_tha_l'))
    box(frame, 2, 30, 8, 26, 12, 14, K('timber_tha'))           # 台
    box(frame, 3, 29, 9, 25, 14, 15, K('timber_tha_l'))
    if kind == 1:                                               # 壺を並べる
        for x in range(5, 28, 6):
            box(frame, x, x + 4, 12, 16, 15, 21, K('wall_tha_sand'))
            box(frame, x + 1, x + 3, 13, 15, 21, 23, K('wall_tha_sand_d'))
    elif kind == 2:                                             # 布を積む
        for i, x in enumerate(range(4, 28, 5)):
            box(frame, x, x + 4, 11, 19, 15, 18 + (i % 3),
                K('cloth_tha_r') if (i % 2 == 0) else K('tile_tha_blue'))
    else:                                                       # 籠と果物
        for x in range(4, 28, 7):
            box(frame, x, x + 5, 11, 18, 15, 19, K('timber_tha_l'))
            box(frame, x + 1, x + 4, 12, 17, 19, 21, K('leaf_tha'))
    cloth = vol(h=56)
    for k in range(9):                                          # 切妻の布（南北に流れる）
        y0, y1 = 2 + k, V - 2 - k
        box(cloth, 1, 31, y0, y0 + 1, 42 + k, 43 + k, K('cloth_tha'))
        box(cloth, 1, 31, y1 - 1, y1, 42 + k, 43 + k, K('cloth_tha'))
    box(cloth, 1, 31, 10, 22, 50, 52, K('cloth_tha'))           # 棟
    for x in range(2, 30, 6):                                   # 縞
        box(cloth, x, x + 2, 2, 30, 42, 52, K('cloth_tha_r') if (kind != 2) else K('tile_tha_blue'))
    box(cloth, 1, 31, 28, 30, 36, 43, K('cloth_tha_d'))         # 南の垂れ
    return [('frame', frame, (0, 0, 0)), ('cloth', cloth, (0, 0, 0))]


def stand_tha(name):
    """**闘技場の観客席**。砂色の段が 5 段（北へ高くなる）。上に日除けの柱と青い帯。1 マス。"""
    v = vol(h=48)
    box(v, 0, V, 0, V, 0, 4, K('wall_tha_sand_d'))
    for i in range(5):                                          # 段（南が低い＝カメラ側）
        y0 = V - 6 * (i + 1)
        box(v, 0, V, y0, y0 + 6, 4, 4 + (i + 1) * 6, K('wall_tha_sand'))
        box(v, 0, V, y0, y0 + 6, 3 + (i + 1) * 6, 4 + (i + 1) * 6, K('wall_tha_sand_l'))
        box(v, 0, V, y0 + 5, y0 + 6, 4, 4 + (i + 1) * 6, K('wall_tha_sand_d'))
    box(v, 0, V, 0, 2, 34, 36, K('tile_tha_blue'))              # 最上段の縁のタイル
    for x0 in (2, 26):                                          # 日除けの柱
        box(v, x0, x0 + 4, 0, 4, 34, 46, K('timber_tha'))
    box(v, 0, V, 0, 4, 44, 46, K('timber_tha_l'))
    return one(v)


def urn_tha(name):
    """**壺**（前庭・市場の脇）。砂色の胴に青いタイルの帯。高さ 0.8 マス。"""
    v = vol(h=26)
    disc(v, 16, 16, 5.0, 0, 3, K('wall_tha_sand_d'))
    disc(v, 16, 16, 7.5, 3, 14, K('wall_tha_sand'))
    disc(v, 16, 16, 8.0, 8, 10, K('tile_tha_blue'))
    disc(v, 16, 16, 5.5, 14, 20, K('wall_tha_sand_l'))
    disc(v, 16, 16, 6.5, 20, 22, K('wall_tha_sand'))
    disc(v, 16, 16, 4.5, 21, 22, 0)                             # 口を抜く
    return one(v)


def tile_table_tha(name):
    """**日除けの下の卓**。青い幾何のタイルの天板と木の脚、脇に低い腰掛け 2 つ。1 マス。"""
    v = vol(h=20)
    for x0, y0 in ((10, 10), (18, 10), (10, 18), (18, 18)):     # 脚
        box(v, x0, x0 + 4, y0, y0 + 4, 0, 12, K('timber_tha'))
    box(v, 7, 25, 7, 25, 12, 14, K('timber_tha_l'))             # 天板の下地
    box(v, 8, 24, 8, 24, 14, 15, K('tile_tha_blue_d'))          # タイルの天板
    for x in range(9, 24, 3):
        for y in range(9, 24, 3):
            box(v, x, x + 2, y, y + 2, 14, 15, K('tile_tha_blue_l') if ((x + y) % 6 == 0)
                else K('tile_tha_blue'))
    for x0 in (2, 26):                                          # 腰掛け
        box(v, x0, x0 + 4, 13, 19, 0, 8, K('timber_tha'))
        box(v, x0 - 1, x0 + 5, 12, 20, 8, 10, K('timber_tha_l'))
    return one(v)


# ------------------------------------------------------------------ 登録
def register(g):
    """`gen_prefabs.py` の名前空間 `g` へアナンバール（`_ana`）とタロス（`_tha`）を足す。

    **`TOWN_STYLES` に 2 つを足す**——`_slice_of` が名前から意匠を切り出せないと、
    `house_wall_nw_ana` のスライスが `'ana'` に化けて 9 スライスが全部同じ形になる
    （`angwil_prefabs.py` の register と同じ理由）。
    """
    global G, C, V
    G = g
    C = g['C']
    V = g['V']
    g['TOWN_STYLES'] = tuple(g['TOWN_STYLES']) + ('ana', 'tha')
    _palette()
    cat = g['CATALOG']
    static = g['static_part']

    # ---- 意匠ごとに必ず作るもの（設計 §4.3。無いと置く側が辺境の絵へ落ちる）----
    for sl in g['HOUSE_SLICES']:
        cat['house_wall_%s_ana' % sl] = (house_wall_ana, static(), '煉瓦の商館の壁 1 層（%s）' % sl)
        cat['house_roof_%s_ana' % sl] = (house_roof_ana, static(), 'スレートの寄棟と鉄の手すり（%s）' % sl)
        cat['house_wall_%s_tha' % sl] = (house_wall_tha, static(), '砂色の日干し煉瓦の壁 1 層（%s）' % sl)
        cat['house_roof_%s_tha' % sl] = (house_roof_tha, static(), '陸屋根と低い胸壁（%s）' % sl)
    cat['house_light_ana'] = (house_light_ana, static(), '窓の灯り（南面の上げ下げ窓）')
    cat['house_light_tha'] = (house_light_tha, static(), '窓の灯り（南面の尖頭アーチの窓）')
    cat['house_entrance_ana'] = (house_entrance_ana, static(), '鉄の両開き戸の玄関')
    cat['house_entrance_tha'] = (house_entrance_tha, static(), '尖頭アーチの戸口')
    for side in ('n', 's', 'w', 'e'):
        cat['fence_%s_ana' % side] = (fence_ana, static(), '煉瓦の腰壁に鋳鉄の柵（%s 側）' % side)
        cat['fence_%s_tha' % side] = (fence_tha, static(), '漆喰の塀に青いタイルの帯（%s 側）' % side)
    cat['watchtower_ana'] = (watchtower_ana, static(), '煉瓦の隅の塔（鉄の手すりとスレートの屋根）')
    cat['watchtower_tha'] = (watchtower_tha, static(), '漆喰の丸い塔（青いタイルの丸屋根）')
    cat['yard_ground_ana'] = (yard_ground_ana, static(), '前庭（敷石と鉄の排水の格子）')
    cat['yard_ground_tha'] = (yard_ground_tha, static(), '前庭（砂と幾何のタイル）')
    cat['chimney_ana'] = (chimney_ana, static(), '屋根の煙突 2 本（roof_vent）')
    cat['chimney_tha'] = (chimney_tha, static(), '屋根の風抜きの塔（roof_vent）')
    cat['lamp_post_ana'] = (lamp_post_ana, static(), 'ガス灯（鋳鉄の柱にガラスの箱）')
    cat['lamp_flame_ana'] = (lamp_flame_ana, static(), 'ガス灯の火（別体。夜だけ）')
    cat['lamp_post_tha'] = (lamp_post_tha, static(), '吊りランタン（鉄の腕木と星形の透かし）')
    cat['lamp_flame_tha'] = (lamp_flame_tha, static(), '吊りランタンの火（別体。夜だけ）')
    for i in range(1, 6):
        cat['ground_path_ana_%02d' % i] = (ground_path_ana, static(), '車道（黒い舗石）')
        cat['ground_turf_ana_%02d' % i] = (ground_turf_ana, static(), '歩道（平らな敷石）')
        cat['ground_path_tha_%02d' % i] = (ground_path_tha, static(), '通り（砂色の敷石）')
        cat['ground_turf_tha_%02d' % i] = (ground_turf_tha, static(), '踏んだ砂と乾いた草')
    for i in range(1, 4):
        cat['palisade_ana_%02d' % i] = (palisade_ana, static(), '町を囲む煉瓦の塀')
        cat['palisade_tha_%02d' % i] = (palisade_tha, static(), '町を囲む漆喰の塀（鋸歯の胸壁）')

    # ---- 名指しで置くもの（設計 §4.3 の後半）----
    cat['street_tree_ana'] = (street_tree_ana, static(), '街路樹（低い並木。鉄の木枠）')
    cat['kerb_ana'] = (kerb_ana, static(), '鋳鉄の縁石（path_edge）')
    cat['clock_tower_ana'] = (clock_tower_ana, clock_tower_ana_parts(), '時計塔（3×3 マス。鐘は振り子）')
    cat['statue_gold_ana'] = (statue_gold_ana, static(), '金の像（拝金の神殿の前）')
    cat['notice_pillar_ana'] = (notice_pillar_ana, static(), '掲示の柱（新聞）')
    cat['post_pillar_ana'] = (post_pillar_ana, static(), '郵便の柱')
    cat['fountain_iron_ana'] = (fountain_iron_ana, static(), '鋳鉄の噴水（水面は静止）')
    cat['guard_post_ana'] = (guard_post_ana, static(), '警邏の詰め所（青い灯り）')
    cat['loading_dock_ana'] = (loading_dock_ana, static(), '荷積みの台')
    cat['sign_arm_ana'] = (sign_arm_ana, sign_arm_ana_parts(), '鉄の看板の腕木（板は振り子）')
    for i in range(1, 3):
        cat['ground_plaza_ana_%02d' % i] = (ground_plaza_ana, static(), '広場の敷石（対角の帯）')
        cat['ground_plaza_tha_%02d' % i] = (ground_plaza_tha, static(), '広場の敷石（青いタイルの縁取り）')
    cat['palm_tha'] = (palm_tha, static(), '棕櫚（節のある幹と放射の葉）')
    cat['cypress_tha'] = (cypress_tha, static(), '糸杉（細く高い暗緑）')
    cat['awning_tha'] = (awning_tha, wind_parts(['cloth'], 0.7), '日除けの布（通りをまたぐ。布は風）')
    cat['minaret_tha'] = (minaret_tha, static(), '尖塔（町でいちばん高い）')
    cat['dome_gate_tha'] = (dome_gate_tha, static(), '宮殿の門（尖頭アーチと青いタイル。3 マス）')
    cat['basin_tha'] = (basin_tha, static(), '水盤（青いタイル。水面は静止）')
    for i in range(1, 4):
        cat['market_tent_tha_%02d' % i] = (market_tent_tha, wind_parts(['cloth'], 0.6),
                                           '市場の天幕（日除けは風。変種 %d/3）' % i)
    cat['stand_tha'] = (stand_tha, static(), '闘技場の観客席')
    cat['urn_tha'] = (urn_tha, static(), '壺（砂色に青いタイルの帯）')
    cat['tile_table_tha'] = (tile_table_tha, static(), '日除けの下の卓（青い幾何のタイルの天板）')
