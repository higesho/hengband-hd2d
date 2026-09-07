# -*- coding: utf-8 -*-
"""アングウィル（町 4 番）の作り込み — 素材の生成器。

`gen_prefabs.py` の末尾から `register(globals())` で呼ばれ、接尾辞 `_ang` の素材を
`CATALOG` へ足す。**既存の素材には 1 バイトも触らない**（別の名前で足すだけ）。

## 作りの規律（`gen_prefabs.py` 冒頭と同じ）
- 無作為な表面ノイズは入れない。彫りは 1 ボクセルの深さ。色は塊ごとに 1 色
- 接地層（z=0）の平面方向は縮めない。**+y は南＝カメラ側**。戸口は手前が y 大
- 柵の高さ（14）は変えない（遮蔽の約束）
- 色は共通の登録簿（`C`）を使い、足りない色だけ `reg_local`。
  **`reg_local` の名前は材質の語で始める**（`bark_` `leaf_` `moss_` `cloth_` `wa_`
  `dirt_` `stem_` `fur_` `steel_`）——`palette_materials.classify` は名前の頭でしか
  材質を当てないので、`ang_cloth_r` のように町の頭を先に置くと `Default` に落ちる。

意匠は**森のエルフの集落**。丸太の高床・樹皮と編み枝の壁・苔の板葺き・生垣・
銀色の樹皮の木立。大物（母なる大樹・樹上の吊り橋・2 本の塔・森の祭壇）で高さを作る。
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


def octagon(v, cx, cy, r, z0, z1, colour):
    """八角の柱（32 ボクセルの円柱は糸巻きに見える。モリバントの記録 §7.1）。"""
    X, Y, Z = v.shape
    xs = np.abs(np.arange(X)[:, None] + 0.5 - cx)
    ys = np.abs(np.arange(Y)[None, :] + 0.5 - cy)
    m = (xs <= r) & (ys <= r) & ((xs + ys) <= (r * 1.42))
    z0, z1 = max(0, z0), min(Z, z1)
    if z0 < z1:
        v[:, :, z0:z1][m] = colour


def hollow(v, depth=6):
    """**中身を空ける**（表から `depth` より深い所を消す）。

    大物を塊で組むと中まで詰まって、`.vox` が桁違いに膨れる（館の初版は 25 MB で、
    このツリーでいちばん大きかった `great_tree_ang` の 2 倍あった。§9 の罠）。
    配列の外は「空」として数えるので、**接地層（z=0）は残る**——footprint は変わらない。
    """
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
        nxt[:, :, 0] = False
        nxt[:, :, -1] = False
        core = nxt
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
    """この意匠だけの色。**名前は材質の語で始める**（冒頭の註記）。

    **2026-09-05 に裂け谷の色へ入れ替えた**（§9）。前は灰緑に沈んでいた——
    幹が灰色・葉が青みの深緑・土が焦げ茶で、昼の絵がひとつづきの暗い緑になっていた。
    いまは**白い幹・金と赤と淡い緑の葉・白い石・金の飾り**で、明るいほうへ振ってある。
    名前は変えていない（形を作る関数を書き換えずに色だけ替えられるように）。
    """
    rl = G['reg_local']
    #! **白い樹皮**（白樺・銀の橅）。町の 61% が木なので、この 4 色が第一印象を決める。
    rl('bark_an_s', (228, 226, 218)); rl('bark_an_sl', (247, 246, 241))
    rl('bark_an_sd', (196, 192, 181)); rl('bark_an_scar', (150, 146, 137))
    #! 削り出した白木（彫りの帯・戸・手すり・垂木）。
    rl('bark_an_w', (238, 230, 208)); rl('bark_an_wl', (252, 248, 236))
    rl('bark_an_wd', (200, 188, 160))
    #! 編み枝（withe）。壁の嵌め板と吊り紐。**白木より一段濃い**だけにする。
    rl('bark_an_wi', (186, 164, 122)); rl('bark_an_wid', (150, 128, 92))
    #! **金と琥珀と淡い緑の葉**（裂け谷の秋）。`leaf_an_h` がいちばん明るい梢。
    rl('leaf_an_d', (150, 116, 44)); rl('leaf_an_m', (206, 166, 64))
    rl('leaf_an_l', (236, 204, 112)); rl('leaf_an_h', (250, 232, 168))
    #! 赤い葉（楓）と、淡い緑の葉（常緑）。木の変種ごとに混ぜ方を変える。
    rl('leaf_an_r', (198, 98, 54)); rl('leaf_an_rd', (152, 62, 40))
    rl('leaf_an_g', (140, 178, 128)); rl('leaf_an_gd', (98, 138, 96))
    #! 苔と地衣（石の根方と林床）。**前より明るい**。
    rl('moss_an', (118, 150, 104)); rl('moss_an_l', (158, 188, 134)); rl('moss_an_d', (88, 116, 82))
    #! 落ち葉（林床と道）。金の葉が散ったもの。
    rl('leaf_an_f', (188, 150, 84)); rl('leaf_an_fd', (150, 114, 60)); rl('leaf_an_fl', (224, 192, 126))
    #! 踏み固めた土。**前より 2 段明るい**（暗い土が町ぜんぶを沈めていた）。
    rl('dirt_an_p', (172, 154, 126)); rl('dirt_an_pd', (140, 124, 100)); rl('dirt_an_pl', (202, 186, 158))
    #! シダと下草（`stem` は草の材質）。
    rl('stem_an_f', (124, 160, 104)); rl('stem_an_fl', (162, 194, 130))
    #! エルフの布（幟・洗濯物・幌）。白・深い青・金。
    rl('cloth_an_g', (58, 84, 132)); rl('cloth_an_gd', (40, 60, 100))
    rl('cloth_an_w', (246, 242, 230)); rl('cloth_an_wd', (214, 208, 192))
    rl('cloth_an_y', (222, 190, 104))
    #! **暖かい金の灯り**（前は青白かった。夜の町の色を柔らかくする）。
    rl('fl_an_pale', (250, 226, 168)); rl('fl_an_core', (255, 247, 216))
    #! 月の石だけは青白いまま（魔法のものと灯りを見分けるため）。
    rl('fl_an_moon', (178, 222, 245))
    #! 磨いた鋼。
    rl('steel_an_l', (214, 220, 228))
    #! **白い切石**（館・塔・門・水盤・橋）。裂け谷の骨はこれで組む。
    rl('marb_an_m', (240, 239, 236)); rl('marb_an_md', (210, 210, 210))
    rl('marb_an_s', (176, 178, 184))
    #! **銀青のスレート**（屋根）。`tile` は石の材質。
    rl('tile_an', (152, 166, 180)); rl('tile_an_l', (190, 202, 212)); rl('tile_an_d', (112, 126, 142))
    #! **金の飾り**（棟飾り・尖塔の先・彫りの縁・欄干の帯）。`gold` は金属の材質。
    rl('gold_an', (216, 178, 86)); rl('gold_an_l', (244, 216, 134)); rl('gold_an_d', (166, 130, 54))
    #! 獣（鹿・兎・梟）。`fur` は布の材質。
    rl('fur_an_deer', (152, 118, 82)); rl('fur_an_deerl', (192, 162, 122))
    rl('fur_an_owl', (166, 148, 120))
    #! 澄んだ水（泉・水盤・落ちる水・雨水の桶）。
    rl('wa_an_c', (128, 182, 196)); rl('wa_an_cl', (186, 224, 232)); rl('wa_an_f', (242, 251, 253))


# ------------------------------------------------------------------ 家（9 スライス）
#! **2026-09-05 に背を高くした**（§9）。前は壁 34・屋根 30 ＝ 1 階建てで 2.0 マスしか
#! 無く、遠目にはただの箱だった。いまは壁 42・屋根 52 ＝ 1 階で 2.9 マス、2 階で 4.2 マス。
HOUSE_WALL_H = 42
HOUSE_ROOF_H = 52
INSET = 2      #: 外に面した壁を引っ込める量（軒の出）
PLINTH = 3     #: 白い切石の礎（**マスいっぱい**。罠 5）
UNDER = 17     #: **列柱の回廊**（開いた柱とアーチ）の天。ここに回廊の床が載る


def _slice(name):
    return G['_slice_of'](name)


def _outward(name):
    return G['SLICE_OUT'][_slice(name)]


def _faces(north, south, west, east):
    return [s for s, on in (('n', north), ('s', south), ('w', west), ('e', east)) if on]


def _face_slice(side, x0, x1, y0, y1):
    if side == 'n':
        return (slice(x0, x1), slice(y0, y0 + 1))
    if side == 's':
        return (slice(x0, x1), slice(y1 - 1, y1))
    if side == 'w':
        return (slice(x0, x0 + 1), slice(y0, y1))
    return (slice(x1 - 1, x1), slice(y0, y1))


def house_wall_ang(name):
    """**裂け谷の家の壁** — 白い切石の礎、**開いた列柱の回廊**、白い漆喰と金の帯。

    下から順に: 礎（z 0..3・マスいっぱい）→ **回廊**（z 3..17。外に面した辺に細い白石の
    柱が並び、その間に丸いアーチが開く。奥は暗がり）→ 回廊の床（z 17..19。壁より
    1 ボクセル張り出す）→ 壁（z 19..42。白い漆喰の面を白木の方立で割り、腰に金の帯）。
    南面には**頭の丸い高い窓**、軒下に彫りの帯と金の縁。

    前は樹皮の縦板と編み枝の腰で、灰緑に沈んでいた（§9）。**開いた柱とアーチ**が
    裂け谷の署名なので、19 の敷地すべてがこれを持つ。
    """
    north, south, west, east = _outward(name)
    rng = rng_for(name)
    v = vol(h=HOUSE_WALL_H)
    #! 礎（白い切石）。**マスいっぱい**（隙間から下が見えないように）。
    box(v, 0, V, 0, V, 0, PLINTH, K('marb_an_s'))
    box(v, 0, V, 0, V, PLINTH - 1, PLINTH, K('marb_an_md'))
    for z in range(1, PLINTH - 1):                            # 石の目地（1 ボクセルの彫り）
        row = np.zeros(V, np.int16)
        p = int(rng.integers(0, 3))
        while p < V:
            w = int(rng.integers(6, 11))
            row[p:p + w] = pick(rng, [K('marb_an_md'), K('marb_an_m'), K('marb_an_s')],
                                [0.46, 0.30, 0.24])
            p += w + 1
        v[:, 0, z] = row
        v[:, V - 1, z] = row[::-1]
        v[0, :, z] = row[::-1]
        v[V - 1, :, z] = row

    x0 = INSET if west else 0
    x1 = V - INSET if east else V
    y0 = INSET if north else 0
    y1 = V - INSET if south else V
    faces = _faces(north, south, west, east)

    #! **列柱の回廊**。中は暗がりで埋め、外に面した辺へ細い柱を 8 ボクセルおきに立て、
    #! その間へ丸いアーチを開ける（柱の頭を 3 段で丸めて弧にする）。
    box(v, x0, x1, y0, y1, PLINTH, UNDER, K('dark'))
    for side in faces:
        fx, fy = _face_slice(side, x0, x1, y0, y1)
        span = fx if side in ('n', 's') else fy
        lo, hi = span.start, span.stop

        def put(a0, a1, z0, z1, colour, depth=(0, 2)):
            """外に面した辺へ置く。`depth` は面からの深さ（0 が面そのもの）。"""
            a0, a1 = max(lo, a0), min(hi, a1)
            if a0 >= a1:
                return
            if side == 'n':
                box(v, a0, a1, fy.start + depth[0], fy.start + depth[1], z0, z1, colour)
            elif side == 's':
                box(v, a0, a1, fy.stop - depth[1], fy.stop - depth[0], z0, z1, colour)
            elif side == 'w':
                box(v, fx.start + depth[0], fx.start + depth[1], a0, a1, z0, z1, colour)
            else:
                box(v, fx.stop - depth[1], fx.stop - depth[0], a0, a1, z0, z1, colour)

        #! **アーチの奥は真っ黒にしない**。`dark` だけだと回廊が黒い腰に見えて
        #! 「開いた柱」に読めない。面から 2〜4 ボクセル奥を薄い石にし、面の 2 ボクセルは
        #! **空にして**（0）柱の間から奥が覗くようにする。
        put(lo, hi, PLINTH, UNDER, K('marb_an_s'), depth=(2, 5))
        put(lo, hi, PLINTH, UNDER, 0, depth=(0, 2))
        put(lo, hi, PLINTH, PLINTH + 1, K('marb_an_md'), depth=(0, 2))   # 回廊の床
        put(lo, hi, 15, UNDER, K('marb_an_m'), depth=(0, 2))             # 楣
        for p in range(lo, hi, 8):
            #! 柱（幅 3。白い石に金の首輪と沓）。
            put(p, p + 3, PLINTH, UNDER, K('marb_an_m'))
            put(p, p + 3, PLINTH, PLINTH + 1, K('gold_an_d'))
            put(p, p + 3, 13, 14, K('gold_an'))
            #! アーチの肩（柱と柱の間の頭を 3 段で丸める）。
            for k in range(3):
                put(p + 1 + k, p + 10 - k, 12 + k, 13 + k, K('marb_an_m'))
    #! 回廊の床（壁より 1 ボクセル張り出す＝軒下の縁）。
    box(v, max(0, x0 - 1), min(V, x1 + 1), max(0, y0 - 1), min(V, y1 + 1), UNDER, UNDER + 2,
        K('marb_an_md'))
    box(v, max(0, x0 - 1), min(V, x1 + 1), max(0, y0 - 1), min(V, y1 + 1), UNDER + 1, UNDER + 2,
        K('marb_an_m'))

    #! 壁の本体（白い漆喰）。
    top = HOUSE_WALL_H
    box(v, x0, x1, y0, y1, UNDER + 2, top, K('marb_an_md'))
    for side in faces:
        fx, fy = _face_slice(side, x0, x1, y0, y1)
        span = fx if side in ('n', 's') else fy
        lo, hi = span.start, span.stop
        if side in ('n', 's'):
            v[lo:hi, fy, UNDER + 2:top] = K('marb_an_m')
        else:
            v[fx, lo:hi, UNDER + 2:top] = K('marb_an_m')
        #! 白木の方立（8 ボクセルおき。下の柱の位置に揃える）。
        for p in range(lo, hi, 8):
            if side in ('n', 's'):
                v[p:min(hi, p + 3), fy, UNDER + 2:top] = K('bark_an_w')
                v[p + 1:min(hi, p + 2), fy, UNDER + 2:top] = K('bark_an_wl')
            else:
                v[fx, p:min(hi, p + 3), UNDER + 2:top] = K('bark_an_w')
                v[fx, p + 1:min(hi, p + 2), UNDER + 2:top] = K('bark_an_wl')
        #! 腰の金の帯と、軒下の彫りの帯（白木＋金の縁）。
        if side in ('n', 's'):
            v[lo:hi, fy, 21:22] = K('gold_an')
            v[lo:hi, fy, 22:23] = K('gold_an_d')
            v[lo:hi, fy, top - 5:top - 1] = K('bark_an_wl')
            v[lo:hi:3, fy, top - 4:top - 2] = K('gold_an')
            v[lo:hi, fy, top - 1:top] = K('gold_an_l')
        else:
            v[fx, lo:hi, 21:22] = K('gold_an')
            v[fx, lo:hi, 22:23] = K('gold_an_d')
            v[fx, lo:hi, top - 5:top - 1] = K('bark_an_wl')
            v[fx, lo:hi:3, top - 4:top - 2] = K('gold_an')
            v[fx, lo:hi, top - 1:top] = K('gold_an_l')

    #! 窓（南面だけ。**頭の丸い高い窓**。灯りは `house_light_ang` が同じ高さへ重なる）。
    if south:
        wx = ((x0 + x1) // 2) - 5
        wz = 24
        box(v, wx - 2, wx + 12, y1 - 2, y1, wz - 2, wz + 15, K('bark_an_wl'))   # 白木の枠
        box(v, wx, wx + 10, y1 - 2, y1, wz, wz + 9, K('glass'))
        for k in range(5):                                    # 頭の丸み（段で丸める）
            box(v, wx + k, wx + 10 - k, y1 - 2, y1, wz + 8 + k, wz + 9 + k, K('glass'))
        box(v, wx + 4, wx + 6, y1 - 2, y1, wz, wz + 13, K('gold_an'))           # 中方立
        box(v, wx - 2, wx + 12, y1 - 1, y1, wz - 2, wz - 1, K('gold_an'))       # 窓台
        #! 窓の下の張り出した棚と、そこから垂れる蔓。
        box(v, wx - 3, wx + 13, y1, min(V, y1 + 2), wz - 5, wz - 3, K('marb_an_m'))
        box(v, wx - 3, wx + 13, y1, min(V, y1 + 2), wz - 3, wz - 2, K('gold_an_d'))
        for fx_ in range(wx - 2, wx + 12, 2):
            v[fx_, min(V - 1, y1 + 1), wz - 6] = pick(rng, [K('leaf_an_g'), K('leaf_an_l'),
                                                            K('fl_w'), K('fl_y')])
            v[fx_, min(V - 1, y1 + 1), wz - 7] = K('leaf_an_gd')
    return one(v)


def house_roof_ang(name):
    """**急な尖り屋根** — 銀青のスレートを重ね、棟と隅棟を金で通す。

    前は苔の板葺きで高さ 30（0.94 マス）しか無く、寄棟が平たく見えた。いまは 52
    （1.6 マス）で、軒から棟まで 49 ボクセル上がる＝**急勾配**である（§9）。
    """
    north, south, west, east = _outward(name)
    rng = rng_for(name)
    #! `curve` は 1.0（まっすぐな斜面）。丸めると急勾配が寝てしまう。
    height, hip = G['_roof_field'](north, south, west, east, 3, HOUSE_ROOF_H, 1.0)
    v = vol(h=HOUSE_ROOF_H)
    slate = rng.choice([K('tile_an'), K('tile_an_l'), K('tile_an_d'), K('marb_an_s')],
                       size=(V // 3 + 2, V // 3 + 2, HOUSE_ROOF_H // 3 + 1),
                       p=[0.42, 0.26, 0.20, 0.12])
    for x in range(V):
        for y in range(V):
            h = int(height[x, y])
            for z in range(h):
                if z < 3:
                    v[x, y, z] = K('bark_an_wl')            # 軒先（白木の鼻隠し）
                else:
                    row = z // 3
                    off = (row % 2) * 2
                    v[x, y, z] = slate[(x + off) // 3, (y + off) // 3, row]
            if (h % 3) == 0:
                v[x, y, h - 1] = K('tile_an_d')             # 板の重なりの影
            if hip[x, y] and (h < HOUSE_ROOF_H):
                v[x, y, h - 1] = K('gold_an')               # 隅棟（金）
            if h >= HOUSE_ROOF_H:
                v[x, y, h - 1] = K('gold_an_l')             # 棟（金）
    #! 苔の筋（谷の側だけ。**天面に塊で載せると屋根が緑の板になる**ので控えめに）。
    for _ in range(int(rng.integers(2, 5))):
        cx, cy = int(rng.integers(1, V - 5)), int(rng.integers(1, V - 3))
        for x in range(cx, cx + 4):
            for y in range(cy, cy + 2):
                v[x, y, height[x, y] - 1] = K('moss_an_d')
    return one(v)


def house_entrance_ang(name):
    """見た目だけの戸口 — 白石の段を上がり、金の縁を取った丸い頭の扉。

    回廊の床（z 17）まで段で上がる。**手前が y 大**（戸口の約束）。
    """
    v = vol(h=42)
    y1 = 31
    box(v, 6, 26, 22, 32, 0, 3, K('marb_an_s'))               # 踏み石
    box(v, 6, 26, 22, 32, 2, 3, K('marb_an_md'))
    for k in range(5):                                        # 白石の段（回廊の高さまで）
        box(v, 8, 24, 30 - (k * 2), 32, 3 + (k * 3), 6 + (k * 3), K('marb_an_md'))
        box(v, 8, 24, 30 - (k * 2), 32, 5 + (k * 3), 6 + (k * 3), K('marb_an_m'))
    box(v, 6, 26, 29, 32, 17, 40, K('marb_an_m'))             # 扉口の枠
    box(v, 6, 8, 29, 32, 17, 40, K('gold_an_d'))
    box(v, 24, 26, 29, 32, 17, 40, K('gold_an_d'))
    box(v, 9, 23, y1, y1 + 1, 18, 36, K('bark_an_wl'))        # 扉
    box(v, 15, 17, y1, y1 + 1, 18, 36, K('gold_an'))          # 召し合わせ
    for k in range(5):                                        # 頭の丸み
        box(v, 9 + k, 23 - k, y1, y1 + 1, 35 + k, 36 + k, K('bark_an_wl'))
        box(v, 8 + k, 24 - k, 29, 32, 36 + k, 37 + k, K('gold_an'))
    box(v, 11, 14, y1, y1 + 1, 22, 30, K('gold_an'))          # 金の葉の彫り（左右）
    box(v, 18, 21, y1, y1 + 1, 22, 30, K('gold_an'))
    box(v, 12, 13, y1, y1 + 1, 20, 32, K('gold_an_d'))
    box(v, 19, 20, y1, y1 + 1, 20, 32, K('gold_an_d'))
    box(v, 13, 15, y1, y1 + 1, 25, 27, K('steel_an_l'))       # 把手
    box(v, 17, 19, y1, y1 + 1, 25, 27, K('steel_an_l'))
    return one(v)


def house_light_ang(name):
    """窓の灯り（`house_wall_ang` の窓 z 24..38 に重なる高さ）。

    置く側は壁の +0.40 マス（12.8 ボクセル）へ持ち上げるので、ここでは z 11..25 に置く。
    """
    v = vol(h=28)
    box(v, 11, 21, 29, 32, 11, 25, K('lit'))
    return one(v)


def chimney_ang(name):
    """煙出し（`roof_vent`）— 白石の胴に金の笠。屋根の棟に載る。"""
    rng = rng_for(name)
    v = vol(h=30)
    box(v, 12, 20, 12, 20, 0, 18, K('marb_an_md'))
    for z in range(0, 18, 4):
        for (x0, x1, y0, y1) in ((12, 20, 12, 13), (12, 20, 19, 20), (12, 13, 12, 20), (19, 20, 12, 20)):
            box(v, x0, x1, y0, y1, z, z + 2,
                pick(rng, [K('marb_an_m'), K('marb_an_md'), K('marb_an_s')], [0.48, 0.30, 0.22]))
    box(v, 11, 21, 11, 21, 18, 20, K('marb_an_m'))            # 笠石
    for (x, y) in ((11, 11), (19, 11), (11, 19), (19, 19)):   # 笠を支える細い柱
        box(v, x, x + 2, y, y + 2, 20, 25, K('marb_an_m'))
    box(v, 10, 22, 10, 22, 25, 27, K('gold_an'))              # 金の笠
    box(v, 14, 18, 14, 18, 27, 30, K('gold_an_l'))            # 頂の飾り
    box(v, 13, 19, 13, 19, 18, 25, K('soot'))                 # 煙道
    return one(v)
# ------------------------------------------------------------------ 柵・門
FENCE_H = 14


def fence_ang(name):
    """編み枝の垣（wattle）— 若木の杭に細枝を交互に編む。高さは既存と同じ 14。"""
    side = _slice(name)
    rng = rng_for(name)
    th = 4
    v = vol(h=FENCE_H)
    lo = 0 if side in ('n', 'w') else V - th
    hi = lo + th

    def put(a0, a1, z0, z1, colour):
        if side in ('n', 's'):
            box(v, a0, a1, lo, hi, z0, z1, colour)
        else:
            box(v, lo, hi, a0, a1, z0, z1, colour)

    #! 編み枝（1 段ごとに前後へ振る＝奥行き 2 の帯で交互に）。
    for i, z in enumerate(range(1, FENCE_H - 2, 2)):
        shade = pick(rng, [K('bark_an_wi'), K('bark_an_wid'), K('bark_an_sd')], [0.5, 0.3, 0.2])
        near = (i % 2) == 0
        if side in ('n', 's'):
            a = lo if near else lo + 2
            box(v, 0, V, a, a + 2, z, z + 2, shade)
        else:
            a = lo if near else lo + 2
            box(v, a, a + 2, 0, V, z, z + 2, shade)
    for p in range(1, V, 9):                                  # 杭（若木。頭が少し高い）
        put(p, p + 3, 0, FENCE_H, K('bark_an_s'))
        put(p + 1, p + 2, 0, FENCE_H, K('bark_an_sl'))
    put(0, V, 0, 1, K('moss_an_d'))                           # 根元の苔
    return one(v)


def arch_ang(name):
    """**曲線の彫りを入れた高い門**（南北にくぐる。2.4 マス）。

    細い白石の柱 2 本に金の柱頭を載せ、半円のアーチを渡す。頂に金の星の飾り、
    アーチの下に暖かい灯りの角灯を吊る。**前は若木を曲げた門**で、森の色に沈んで
    門に見えなかった（§9）。白い石は森の中でいちばんよく目に立つ。
    """
    v = vol(h=76)
    #! 柱（幅 6 の白石。根方に沓、頭に金の柱頭）。
    for cx in (4, 22):
        box(v, cx, cx + 6, 12, 20, 0, 46, K('marb_an_m'))
        box(v, cx + 1, cx + 5, 12, 20, 0, 46, K('marb_an_md'))       # 溝彫り（1 ボクセル）
        box(v, cx + 2, cx + 4, 12, 20, 0, 46, K('marb_an_m'))
        box(v, cx - 1, cx + 7, 11, 21, 0, 4, K('marb_an_s'))          # 沓石
        box(v, cx - 1, cx + 7, 11, 21, 3, 4, K('marb_an_md'))
        box(v, cx - 1, cx + 7, 11, 21, 44, 47, K('gold_an'))          # 柱頭
        box(v, cx - 1, cx + 7, 11, 21, 46, 47, K('gold_an_l'))
    #! 半円のアーチ（**64 段で刻む**。24 段では階段に見えた。前の担当の記録 §7.1）。
    for k in range(64):
        t = k / 63.0
        a = t * np.pi
        px = 16 - int(round(np.cos(a) * 12))
        pz = 47 + int(round(np.sin(a) * 16))
        box(v, px - 3, px + 3, 12, 20, pz, pz + 5, K('marb_an_m'))
        box(v, px - 3, px + 3, 12, 13, pz, pz + 5, K('marb_an_md'))   # 側面の縁取り
        box(v, px - 3, px + 3, 19, 20, pz, pz + 5, K('marb_an_md'))
        box(v, px - 2, px + 2, 12, 20, pz + 4, pz + 5, K('gold_an'))  # 弧の上端に金の線
    #! 頂の飾り（金の星と、その台）。
    box(v, 12, 20, 12, 20, 63, 66, K('marb_an_m'))
    box(v, 13, 19, 13, 19, 66, 68, K('gold_an'))
    box(v, 15, 17, 13, 19, 68, 74, K('gold_an_l'))
    box(v, 13, 19, 15, 17, 70, 72, K('gold_an_l'))
    #! アーチの下に吊る角灯（枠だけ。火は `arch_flame_ang`）。
    box(v, 15, 17, 15, 17, 52, 62, K('gold_an_d'))
    for (x0, x1) in ((13, 14), (18, 19)):
        for (y0, y1) in ((13, 14), (18, 19)):
            box(v, x0, x1, y0, y1, 42, 52, K('gold_an'))
    box(v, 12, 20, 12, 20, 40, 42, K('gold_an'))
    box(v, 12, 20, 12, 20, 52, 54, K('gold_an_l'))
    #! 柱に絡む蔓（1 本だけ。白い石に緑の線が入ると「森の門」に見える）。
    for z in range(4, 40, 3):
        dx = int(round(1.5 * np.sin(z * 0.25)))
        box(v, 3 + dx, 5 + dx, 11, 13, z, z + 2, K('leaf_an_g'))
    return one(v)


def arch_ang_ew(name):
    return G['rotate_parts_90'](arch_ang(name))


def arch_flame_ang(name):
    """門の角灯の火（夜だけ置く側が自発光をつけて重ねる）。暖かい金の灯り。"""
    a = vol(h=54)
    box(a, 13, 19, 13, 19, 43, 51, K('fl_an_pale'))
    box(a, 14, 18, 14, 18, 44, 53, K('fl_an_core'))
    return one(a)


def arch_flame_ang_ew(name):
    return G['rotate_parts_90'](arch_flame_ang(name))


def lamp_post_ang(name):
    """**灯籠の列**（`lamp_step`。1.9 マス）— 細い白石の柱に金の角灯を載せる。

    前は若木の腕木から角灯を提げていて、森の中で形が読めなかった。まっすぐ立つ
    白い柱にすると、道に沿って並んだときに**列**として読める（§9）。**火は別体**（罠 39）。
    """
    v = vol(h=62)
    disc(v, 16, 16, 5.0, 0, 3, K('marb_an_s'))                # 沓石
    disc(v, 16, 16, 4.2, 2, 4, K('marb_an_md'))
    octagon(v, 16, 16, 2.6, 3, 44, K('marb_an_m'))            # 柱（八角。丸柱は糸巻きに見える）
    for z in range(6, 42, 6):                                 # 節の目地（1 ボクセル）
        octagon(v, 16, 16, 2.6, z, z + 1, K('marb_an_md'))
    box(v, 12, 21, 12, 21, 44, 46, K('gold_an_d'))            # 灯りの受け皿
    for (x0, x1) in ((12, 14), (19, 21)):                     # 角灯の四隅
        for (y0, y1) in ((12, 14), (19, 21)):
            box(v, x0, x1, y0, y1, 46, 56, K('gold_an'))
    box(v, 12, 21, 12, 21, 56, 58, K('gold_an_l'))            # 笠
    box(v, 13, 20, 13, 20, 58, 59, K('gold_an'))
    box(v, 15, 18, 15, 18, 59, 62, K('gold_an_l'))            # 頂の芽
    return one(v)


def lamp_flame_ang(name):
    """灯籠の火（`lamp_post_ang` と同じマスへ。ゆらぎは振り子 2 枚）。"""
    a = np.zeros((6, 6, 10), np.int16)
    a[1:5, 1:5, 0:8] = K('fl_an_pale')
    a[2:4, 2:4, 4:10] = K('fl_an_core')
    b = np.zeros((4, 4, 8), np.int16)
    b[1:3, 1:3, 0:8] = K('fl_an_core')
    return [('flame_a', a, (13, 13, 47)), ('flame_b', b, (14, 14, 48))]
def lamp_flame_parts():
    return [
        {'name': 'flame_a', 'voxels': 'flame_a', 'grounded': False,
         #! **支点はその部品の体積の中**でなければならない（外だと読み込みで落ちる。§9）。
         #! `lamp_flame_ang` は (13,13,47) に 6×6×10 を置くので、真ん中は (16,16,50)。
         'pivot': [16.0, 16.0, 50.0],
         'motion': {'kind': 'pendulum', 'axis': 'x', 'amplitude': 7.0, 'period': 0.9,
                    'phase': 0.0, 'damping': 0.0},
         'wind_k': 0.0},
        {'name': 'flame_b', 'voxels': 'flame_b', 'grounded': False,
         'pivot': [16.0, 16.0, 51.0],
         'motion': {'kind': 'pendulum', 'axis': 'y', 'amplitude': 9.0, 'period': 0.63,
                    'phase': 1.7, 'damping': 0.0},
         'wind_k': 0.0},
    ]


# ------------------------------------------------------------------ 地面
def _tile(h=2):
    return vol(h=h)


def ground_path_ang(name):
    """森の小径 — 踏み固めた土に落ち葉が散り、ところどころ根と飛び石。

    **縁を特別扱いしない。** 初版は 4 辺へ落ち葉の帯を敷き、変種の半分に丸太の木道を
    通していた——**719 マスの道が縞と格子になった**（真上からの絵で気づいた）。
    模様はマスの内側の塊で作り、隣のマスと繋がるものは置かない。
    """
    rng = rng_for(name)
    v = _tile(2)
    box(v, 0, V, 0, V, 0, 1, K('dirt_an_pd'))
    layer = np.zeros((V, V), np.int16)
    mottle(rng, layer, [K('dirt_an_p'), K('dirt_an_pd'), K('dirt_an_pl'), K('leaf_an_fd')],
           [0.38, 0.26, 0.22, 0.14])
    #! 落ち葉の吹き溜まり（塊で 3〜6 個。位置は毎回ちがう）。
    for _ in range(int(rng.integers(3, 7))):
        x, y = int(rng.integers(0, V - 6)), int(rng.integers(0, V - 6))
        w, d = int(rng.integers(4, 9)), int(rng.integers(3, 8))
        shade = pick(rng, [K('leaf_an_f'), K('leaf_an_fd'), K('leaf_an_fl')], [0.5, 0.3, 0.2])
        layer[x:x + w, y:y + d] = shade
    kind = int(name[-2:]) % 4
    if kind == 2:
        #! 露出した根（**マスの真ん中で切れる短い 1 本**。端まで引くと隣と繋がって縞になる）。
        y = int(rng.integers(9, 22))
        x0 = int(rng.integers(2, 9))
        for x in range(x0, min(V - 2, x0 + int(rng.integers(12, 20)))):
            yy = min(V - 2, max(0, y + int(round(2.5 * np.sin(x * 0.25)))))
            layer[x, yy:yy + 2] = K('bark_an_sd')
            layer[x, yy:yy + 1] = K('bark_an_s')
    elif kind == 0:
        #! 飛び石（白い平石。3〜5 枚）。
        for _ in range(int(rng.integers(3, 6))):
            x, y = int(rng.integers(2, V - 7)), int(rng.integers(2, V - 7))
            r = int(rng.integers(2, 4))
            for dx in range(-r, r + 1):
                for dy in range(-r, r + 1):
                    if (dx * dx) + (dy * dy) <= (r * r):
                        layer[x + dx, y + dy] = K('marb_an_md') if ((dx + dy) % 3) else K('marb_an_m')
    grain(rng, layer, layer > 0, [K('pebble'), K('moss_an_d')], [0.04, 0.04], block=2)
    v[:, :, 1] = layer
    return ground(v, 2)


def ground_turf_ang(name):
    """林床 — 苔と落ち葉と下草。土が透け、根の瘤が覗く。**森の地の色を決めるタイル**。"""
    rng = rng_for(name)
    v = _tile(2)
    box(v, 0, V, 0, V, 0, 1, K('dirt_an_pd'))
    layer = np.zeros((V, V), np.int16)
    #! **苔を主にする**（落ち葉を主にした版は林床が茶色く濁って見えた）。
    mottle(rng, layer, [K('moss_an'), K('moss_an_l'), K('moss_an_d'), K('leaf_an_f'), K('leaf_an_fd')],
           [0.30, 0.24, 0.20, 0.14, 0.12])
    for _ in range(int(rng.integers(2, 5))):                  # 土が覗く所
        x, y = int(rng.integers(0, V - 5)), int(rng.integers(0, V - 5))
        layer[x:x + int(rng.integers(3, 7)), y:y + int(rng.integers(3, 6))] = K('dirt_an_p')
    if (int(name[-2:]) % 2) == 0:
        #! 根の瘤（**短い弧。マスの端まで引かない**——引くと隣と繋がって縞になる）。
        cx, cy = int(rng.integers(8, 24)), int(rng.integers(8, 24))
        for t in range(int(rng.integers(10, 18))):
            x = min(V - 3, max(0, cx + t - 6))
            y = min(V - 2, max(0, cy + int(round(3 * np.sin(t * 0.4)))))
            layer[x:x + 3, y] = K('bark_an_sd')
            layer[x + 1:x + 2, y] = K('bark_an_s')
    grain(rng, layer, layer > 0, [K('stem_an_f'), K('pebble'), K('fl_w')], [0.05, 0.03, 0.02], block=2)
    v[:, :, 1] = layer
    return ground(v, 2)


def ground_trail_ang(name):
    """**集落の踏み跡**（データの `DIRT` 659 マスを丸ごと差し替える）。

    小径より広く踏まれ、落ち葉が退いて土が出ている。**縁は塗らない**——初版は 4 辺を
    苔で縁取ったので、真上から見ると集落じゅうが緑の格子になった。
    """
    rng = rng_for(name)
    v = _tile(2)
    box(v, 0, V, 0, V, 0, 1, K('dirt_an_pd'))
    layer = np.zeros((V, V), np.int16)
    mottle(rng, layer, [K('dirt_an_p'), K('dirt_an_pl'), K('dirt_an_pd')], [0.44, 0.30, 0.26])
    for _ in range(int(rng.integers(3, 7))):                  # 足跡（2×2 の暗い塊）
        px, py = int(rng.integers(0, V - 2)), int(rng.integers(0, V - 2))
        layer[px:px + 2, py:py + 2] = K('leaf_an_fd')
    for _ in range(int(rng.integers(2, 5))):                  # 踏まれ残った苔の島
        px, py = int(rng.integers(0, V - 5)), int(rng.integers(0, V - 5))
        layer[px:px + int(rng.integers(3, 6)), py:py + int(rng.integers(2, 5))] = K('moss_an_d')
    grain(rng, layer, layer > 0, [K('leaf_an_f'), K('pebble')], [0.06, 0.04], block=2)
    v[:, :, 1] = layer
    return ground(v, 2)


def yard_ground_ang(name):
    """前庭の地面 — **白い敷石**を張り、目地に苔が入る。館と塔の足元がこれになる。

    前は踏み固めた土に木くずだったが、白い石の建物の足元が土だと沈んで見えた（§9）。
    **縁は塗らない**（隣のマスと繋がると格子になる。§7.3 と同じ罠）。
    """
    rng = rng_for(name)
    v = _tile(2)
    box(v, 0, V, 0, V, 0, 1, K('marb_an_s'))
    layer = np.zeros((V, V), np.int16)
    #! 敷石（**マスの内側だけ**で割る。8×8 の石を 4×4 枚）。
    for bx in range(0, V, 8):
        for by in range(0, V, 8):
            shade = pick(rng, [K('marb_an_md'), K('marb_an_m'), K('marb_an_s')], [0.44, 0.34, 0.22])
            layer[bx:bx + 7, by:by + 7] = shade
    grain(rng, layer, layer > 0, [K('moss_an_d'), K('leaf_an_f')], [0.05, 0.04], block=2)
    v[:, :, 1] = layer
    return ground(v, 2)


def ground_glade_ang(name):
    """**日の差す空き地**（区画の地面）— 短い草に木漏れ日の斑と、小さな花。"""
    rng = rng_for(name)
    v = _tile(2)
    box(v, 0, V, 0, V, 0, 1, K('dirt_an_pd'))
    layer = np.zeros((V, V), np.int16)
    mottle(rng, layer, [K('turf'), K('turf_l'), K('moss_an_l'), K('turf_d')], [0.34, 0.28, 0.22, 0.16])
    #! 木漏れ日（明るい塊を 3〜5 個。丸く）。
    for _ in range(int(rng.integers(3, 6))):
        cx, cy = int(rng.integers(4, V - 4)), int(rng.integers(4, V - 4))
        r = int(rng.integers(3, 6))
        for x in range(max(0, cx - r), min(V, cx + r)):
            for y in range(max(0, cy - r), min(V, cy + r)):
                if ((x - cx) ** 2 + (y - cy) ** 2) <= (r * r):
                    layer[x, y] = K('turf_l')
    grain(rng, layer, layer > 0, [K('fl_w'), K('fl_y'), K('stem_an_fl')], [0.03, 0.03, 0.04], block=2)
    v[:, :, 1] = layer
    return ground(v, 2)


def ground_field_ang(name):
    """**畑の畝**（区画の地面）— 黒土の畝に芽。畝は南北に走る。"""
    rng = rng_for(name)
    v = _tile(6)
    box(v, 0, V, 0, V, 0, 3, K('dirt_an_pd'))
    for x in range(V):
        ridge = ((x // 5) % 2) == 0
        top = 5 if ridge else 3
        box(v, x, x + 1, 0, V, 0, top, K('dirt_an_p') if ridge else K('dirt_an_pd'))
        if ridge:
            box(v, x, x + 1, 0, V, top - 1, top, K('dirt_an_pl'))
    #! 芽（畝の上に点々と）。**厚みは 6 取ってある**（4 で作ると芽が黙って消える。罠）。
    for _ in range(int(rng.integers(10, 18))):
        x = (int(rng.integers(0, 7)) * 5) + int(rng.integers(1, 4))
        y = int(rng.integers(1, V - 2))
        if x >= V:
            continue
        box(v, x, x + 2, y, y + 2, 5, 6, K('stem_an_f'))
        box(v, x, x + 1, y, y + 1, 5, 6, K('stem_an_fl'))
    return ground(v, 6)


def grass_ang(name):
    """シダの株（`tuft`）— 羽状の葉を放射に広げる。高さ 0.55 マス。"""
    rng = rng_for(name)
    v = vol(h=18)
    for _ in range(int(rng.integers(5, 9))):
        bx, by = int(rng.integers(9, V - 9)), int(rng.integers(9, V - 9))
        tall = int(rng.integers(8, 17))
        ang = rng.random() * 6.283
        lean = rng.uniform(3.0, 6.0)
        blade = pick(rng, [K('stem_an_f'), K('stem_an_fl'), K('moss_an_l')], [0.45, 0.35, 0.20])
        for z in range(tall):
            t = z / float(tall)
            x = min(V - 1, max(0, bx + int(round(np.cos(ang) * lean * t))))
            y = min(V - 1, max(0, by + int(round(np.sin(ang) * lean * t))))
            v[x, y, z] = blade
            #! 羽（左右へ 1 ボクセルの小葉。シダの形はこれで出る）。
            if ((z % 3) == 1) and (z > 2):
                v[min(V - 1, x + 1), y, z] = blade
                v[max(0, x - 1), y, z] = blade
    return one(v)


def grass_edge_ang(name):
    """道の縁の下草（`path_edge`）— 低く広い羊歯とスゲの帯。"""
    rng = rng_for(name)
    v = vol(h=13)
    for _ in range(int(rng.integers(20, 28))):
        bx, by = int(rng.integers(3, V - 3)), int(rng.integers(10, V - 10))
        tall = int(rng.integers(4, 12))
        lean_x, lean_y = int(rng.integers(-2, 3)), int(rng.integers(-2, 3))
        blade = pick(rng, [K('stem_an_f'), K('stem_an_fl'), K('moss_an_l')], [0.44, 0.36, 0.20])
        for z in range(tall):
            t = z / float(tall)
            x = min(V - 1, max(0, bx + int(round(lean_x * t))))
            y = min(V - 1, max(0, by + int(round(lean_y * t))))
            v[x, y, z] = blade
    return one(v)


# ------------------------------------------------------------------ 塀・櫓・生垣
def palisade_ang(name):
    """**生きた木を編んだ塀** — 若木を密に立て、その間を枝で編み、蔦と苔が覆う。

    丸太の柵列（辺境の地）と違って**上が揃わない**（生きているので）。
    高さは基の塀と同じ 64 ボクセル（2 マス）＝遮蔽の約束を変えない。
    """
    rng = rng_for(name)
    height = 64
    v = vol(h=height)
    step = 8
    for bx in range(0, V, step):
        for by in range(0, V, step):
            r = float(rng.choice([4.2, 3.8, 3.4]))
            cx, cy = bx + 4.0, by + 4.0
            top = int(rng.integers(height - 16, height))
            shade = pick(rng, [K('bark_an_s'), K('bark_an_sl'), K('bark_an_sd')], [0.48, 0.28, 0.24])
            for x in range(bx, min(bx + step, V)):
                for y in range(by, min(by + step, V)):
                    d2 = ((x + 0.5 - cx) ** 2) + ((y + 0.5 - cy) ** 2)
                    if d2 > r * r:
                        continue
                    taper = min(6, int(round(4.0 * (d2 / (r * r)))))
                    v[x, y, 0:max(1, top - taper)] = shade
                    v[x, y, max(0, top - taper - 1)] = K('bark_an_scar')
    #! 編んだ枝（3 段の帯。柱の間を埋める）。**隙間を縦に抜く**と編み目に見える。
    for z0 in (14, 30, 46):
        box(v, 0, V, 0, V, z0, z0 + 3, K('bark_an_wi'))
        box(v, 0, V, 0, V, z0 + 1, z0 + 2, K('bark_an_wid'))
        v[::5, :, z0:z0 + 3] = 0
        v[:, ::5, z0:z0 + 3] = 0
    #! 根元の苔と、這い上がる蔦。
    for _ in range(int(rng.integers(3, 6))):
        x, y = int(rng.integers(0, V - 5)), int(rng.integers(0, V - 5))
        blk = v[x:x + 5, y:y + 5, 0:6]
        blk[blk != 0] = K('moss_an_d') if rng.random() < 0.5 else K('moss_an')
    for _ in range(int(rng.integers(2, 5))):
        x, y = int(rng.integers(0, V)), int(rng.integers(0, V))
        for z in range(0, int(rng.integers(24, 52)), 2):
            xx = min(V - 1, max(0, x + int(round(2 * np.sin(z * 0.3)))))
            if v[xx, y, z] != 0:
                v[xx, y, z] = K('leaf_an_g')
                v[xx, y, min(height - 1, z + 1)] = K('leaf_an_gd')
    return one(v)


def watchtower_ang(name):
    """**樹上の見張り台** — 生きた木の幹を柱にした櫓（6.6 マス）。

    梯子・板の床・手すり・葉に隠れた小屋・吊り灯。**塀（2 マス）より頭 2 つ以上高い**
    ので、遠目には森の天蓋から突き出た点として読める。**高さで町の輪郭を作る**素材。
    """
    rng = rng_for(name)
    #! **天蓋（5.9 マス）より高い**——低い櫓は森に埋もれて見張り台に読めない。
    height = 212
    v = vol(h=height)
    #! 幹（4 本。上へ細る）。
    for (cx, cy) in ((7, 7), (25, 7), (7, 25), (25, 25)):
        for z in range(0, 168):
            r = 4.6 - (2.0 * (z / 168.0))
            disc(v, cx, cy, r, z, z + 1, K('bark_an_s'))
            disc(v, cx, cy, r - 1.6, z, z + 1, K('bark_an_sl'))
    #! 貫（4 段）。
    for z in (34, 74, 114, 148):
        box(v, 4, 28, 5, 9, z, z + 3, K('bark_an_sd'))
        box(v, 4, 28, 23, 27, z, z + 3, K('bark_an_sd'))
        box(v, 5, 9, 4, 28, z, z + 3, K('bark_an_sd'))
        box(v, 23, 27, 4, 28, z, z + 3, K('bark_an_sd'))
    #! 梯子（南面）。
    box(v, 13, 15, 27, 29, 0, 168, K('bark_an_wd'))
    box(v, 18, 20, 27, 29, 0, 168, K('bark_an_wd'))
    for z in range(3, 168, 5):
        box(v, 13, 20, 27, 29, z, z + 1, K('bark_an_w'))
    #! 床（張り出す）と手すり。
    box(v, 0, V, 0, V, 168, 172, K('bark_an_wd'))
    box(v, 1, V - 1, 1, V - 1, 171, 172, K('bark_an_w'))
    for (a0, a1) in ((0, 3), (29, 32)):
        box(v, a0, a1, 0, V, 172, 184, K('bark_an_s'))
        box(v, 0, V, a0, a1, 172, 184, K('bark_an_s'))
    blk = v[:, :, 182:184]
    blk[blk != 0] = K('gold_an')
    #! 小屋（北寄り。片流れの板屋根）。
    box(v, 5, 27, 2, 18, 172, 192, K('bark_an_sd'))
    box(v, 6, 26, 3, 17, 173, 192, K('dark'))
    for k in range(10):
        box(v, 3, 29, 1 + k, 2 + k, 192 + k, 195 + k, K('tile_an'))
        box(v, 3, 29, 1 + k, 2 + k, 194 + k, 195 + k, K('tile_an_l'))
    box(v, 3, 29, 11, 20, 192, 195, K('tile_an_d'))
    box(v, 3, 29, 11, 20, 194, 195, K('tile_an'))
    #! 頂の葉（見張り台を森に溶かす）。
    for _ in range(int(rng.integers(8, 13))):
        bx, by = int(rng.integers(0, V - 8)), int(rng.integers(0, V - 8))
        bz = int(rng.integers(196, height - 6))
        box(v, bx, bx + int(rng.integers(5, 9)), by, by + int(rng.integers(4, 8)), bz, bz + 4,
            pick(rng, [K('leaf_an_m'), K('leaf_an_l'), K('leaf_an_d'), K('leaf_an_h')],
                 [0.34, 0.28, 0.22, 0.16]))
    #! 吊り灯（南の手すりに 1 つ。昼も形が見える）。
    box(v, 14, 18, 29, 31, 168, 172, K('gold_an_d'))
    box(v, 15, 17, 29, 31, 164, 169, K('fl_an_pale'))
    return one(hollow(v, 3))


def hedge_ang(name):
    """区画の生垣（`rim`。名前は `hedge_ang_n` の順）— 刈り込んだ低い茂み。"""
    side = name.rsplit('_', 1)[-1]
    rng = rng_for(name)
    v = vol(h=FENCE_H)
    th = 6
    lo = 0 if side in ('n', 'w') else V - th
    hi = lo + th
    if side in ('n', 's'):
        box(v, 0, V, lo, hi, 0, FENCE_H - 2, K('leaf_an_gd'))
    else:
        box(v, lo, hi, 0, V, 0, FENCE_H - 2, K('leaf_an_gd'))
    #! 天面を明るい葉の塊で刻む（塊ごとに 1 色）。
    for _ in range(14):
        a = int(rng.integers(0, V - 4))
        w = int(rng.integers(3, 7))
        z = int(rng.integers(FENCE_H - 5, FENCE_H))
        shade = pick(rng, [K('leaf_an_g'), K('moss_an_l'), K('leaf_an_l')], [0.45, 0.35, 0.20])
        if side in ('n', 's'):
            box(v, a, a + w, lo, hi, z - 3, z, shade)
        else:
            box(v, lo, hi, a, a + w, z - 3, z, shade)
    #! 幹の見える根元。
    if side in ('n', 's'):
        for p in range(2, V, 6):
            box(v, p, p + 2, lo + 2, hi - 2, 0, 4, K('bark_an_sd'))
    else:
        for p in range(2, V, 6):
            box(v, lo + 2, hi - 2, p, p + 2, 0, 4, K('bark_an_sd'))
    return one(v)


# ------------------------------------------------------------------ 森の木立（`grove`）
#: 変種 01〜06 の葉の色（**暗い順に 4 色**）。関数にしてあるのは、`C` が
#: `register()` の中で `_palette()` を呼んでから埋まるからである（束縛の順序）。
TREE_LEAF_SETS = (
    #! 01 下木 — 淡い緑の常緑（林床の近くは緑にして、金だけの林にしない）
    lambda: [K('leaf_an_gd'), K('leaf_an_g'), K('moss_an_l'), K('leaf_an_h')],
    #! 02 下木 — 赤い楓（低い所に赤を置くと、金の天蓋の下が締まる）
    lambda: [K('leaf_an_rd'), K('leaf_an_r'), K('leaf_an_d'), K('leaf_an_m')],
    #! 03 中木 — 金
    lambda: [K('leaf_an_d'), K('leaf_an_m'), K('leaf_an_l'), K('leaf_an_h')],
    #! 04 中木 — **淡い緑**（金だけの林にしない。天蓋の 1/4 をこれにする）
    lambda: [K('leaf_an_gd'), K('leaf_an_g'), K('leaf_an_g'), K('leaf_an_h')],
    #! 05 大木 — **赤銅**（いちばん多い高さ。ここに赤を入れると天蓋が一枚の黄色でなくなる）
    lambda: [K('leaf_an_rd'), K('leaf_an_r'), K('leaf_an_d'), K('leaf_an_m')],
    #! 06 大木 — 明るい金（天蓋の頭。ここがいちばん光る）
    lambda: [K('leaf_an_m'), K('leaf_an_l'), K('leaf_an_h'), K('leaf_an_h')],
)


def tree_ang(name):
    """**アングウィルの木立**（`grove`。町の 7,938 マスがこれになる）。**針葉樹**。

    2026-09-06 に決めた「木を針葉樹にしてみたい。既存の丸いシルエットと通常の木は
    全て広葉樹の印象。アングウィルだけ針葉樹に」。白い高い幹はそのまま（林の下が抜ける
    §7.3 の答え）、樹冠を**段になった円錐**にする。段ごとに半径がすぼまってまた広がる
    （枝の輪）ので、遠目に樅・唐松の輪郭になる。葉の色は変種ごとに違う（金と赤は秋の
    唐松、緑は樅と唐檜）。

    名前の末尾 2 桁が高さを決める（01 = 1.5 マスの若木 〜 06 = 5.9 マスの大木）。
    """
    rng = rng_for(name)
    step = int(name[-2:]) if name[-2:].isdigit() else 3
    heights = (48, 88, 118, 146, 168, 188)
    h = heights[min(5, max(0, step - 1))]
    span = 64 if h < 100 else (96 if h < 168 else 128)
    v = np.zeros((span, span, h), np.int16)
    cx = cy = (((span // V) // 2) * V) + (V // 2)
    low = h < 100
    #! 幹の丈。針葉樹は葉が低い所から付くが、林の下を抜くために **0.45〜0.55** から始める
    #! （若木は 0.25 から）。幹はその上も梢まで通る。
    crown_bot = int(h * (rng.uniform(0.22, 0.30) if low else rng.uniform(0.45, 0.55)))
    r_base = max(2.2, h * 0.040)
    xs = np.arange(span)[:, None] + 0.5 - cx
    ys = np.arange(span)[None, :] + 0.5 - cy
    d2 = (xs * xs) + (ys * ys)
    #! 幹（下は太く、梢で 1 ボクセルに）。
    for z in range(h):
        tt = z / float(h)
        r = r_base * (1.0 - (0.8 * tt))
        v[:, :, z][d2 <= max(0.9, r * r)] = K('bark_an_s')
    for z in range(2, crown_bot, 5):
        v[cx + max(1, int(r_base)) - 1, cy - 2:cy + 2, z:z + 2] = K('bark_an_scar')
    #! 樹冠。半径は根元で span の 0.17（若木 0.24）、梢で 0。段（枝の輪）は 6〜8 ボクセル。
    leaf_shades = TREE_LEAF_SETS[min(5, max(0, step - 1))]()
    crown_r = span * (0.24 if low else 0.17)
    tier = int(rng.integers(6, 9))
    lean = float(rng.uniform(0.0, 6.28))
    for z in range(crown_bot, h - 1):
        tt = (z - crown_bot) / float(max(1, h - 1 - crown_bot))
        base = crown_r * ((1.0 - tt) ** 0.85)
        phase = ((z - crown_bot) % tier) / float(tier)
        r = base * (1.0 - (0.42 * phase)) + 0.6
        if r < 1.0:
            continue
        #! 輪の縁は少し不揃いに（向きで ±12%）。
        ang = np.arctan2(ys, xs)
        wobble = 1.0 + (0.12 * np.sin((3.0 * ang) + lean + (z * 0.37)))
        m = d2 <= ((r * wobble) ** 2)
        inner = d2 <= ((r * 0.55) ** 2)
        #! 色: 輪の下段は暗く、上段（新しい葉）は明るい。外側の縁は最も明るい。
        shade_i = 0 if phase < 0.3 else (1 if phase < 0.65 else 2)
        slab = v[:, :, z]
        outer = m & ~inner & (slab == 0)
        core = inner & (slab == 0)
        slab[core] = leaf_shades[min(3, shade_i)]
        slab[outer] = leaf_shades[min(3, shade_i + 1)]
    #! 梢の穂（1〜2 ボクセルの明るい先）。
    v[cx, cy, h - 2:h] = leaf_shades[3]
    return one(v)


def arcade(v, x0, x1, y0, y1, z0, z1, pitch=20, colw=6, depth=6,
           col=None, back=None, gold=None):
    """**開いた列柱の回廊**を矩形の 4 面へ彫る（裂け谷の署名。§9）。

    `x0..x1` `y0..y1` は建物の外形。まず外周 `depth` ボクセルの帯を空にし、その帯へ
    `pitch` おきに幅 `colw` の柱を立てて、柱の間の頭を段で丸めてアーチにする。
    帯の内側は `back`（陰の落ちた奥）が受ける——**真っ黒にすると黒い腰に見える**。
    """
    col = K('marb_an_m') if col is None else col
    back = K('marb_an_s') if back is None else back
    gold = K('gold_an') if gold is None else gold
    top = z1 - 6          #: アーチの頂（ここから上は楣）
    #! 奥（帯のさらに内側 2 ボクセル）を薄い石にしてから、帯を空ける。
    #! **中身は空にする**——solid のままだと .vox が桁違いに膨れる（罠。§9）。
    box(v, x0 + depth, x1 - depth, y0 + depth, y1 - depth, z0, z1, back)
    box(v, x0 + depth + 10, x1 - depth - 10, y0 + depth + 10, y1 - depth - 10, z0, z1, 0)
    box(v, x0, x1, y0, y1, z0, z1, 0)
    box(v, x0 + depth - 2, x1 - depth + 2, y0 + depth - 2, y1 - depth + 2, z0, z1, back)
    box(v, x0 + depth + 10, x1 - depth - 10, y0 + depth + 10, y1 - depth - 10, z0 + 2, z1 - 6, 0)
    #! 床（回廊の敷石）と楣。
    box(v, x0, x1, y0, y1, z0, z0 + 2, K('marb_an_md'))
    box(v, x0, x1, y0, y1, top + 2, z1, col)
    box(v, x0, x1, y0, y1, z1 - 2, z1, gold)

    def column(cx, cy):
        box(v, cx, cx + colw, cy, cy + colw, z0, top + 2, col)
        box(v, cx, cx + colw, cy, cy + colw, z0, z0 + 2, K('marb_an_md'))
        box(v, cx, cx + colw, cy, cy + colw, top - 2, top + 2, gold)

    #! 4 隅の柱と、辺に並ぶ柱。
    for cx in list(range(x0, x1 - colw, pitch)) + [x1 - colw]:
        column(cx, y0)
        column(cx, y1 - colw)
    for cy in list(range(y0, y1 - colw, pitch)) + [y1 - colw]:
        column(x0, cy)
        column(x1 - colw, cy)
    #! アーチの肩（柱と柱の間。3 段で丸める）。
    for k in range(4):
        pad = 3 - k
        for cx in range(x0, x1 - colw, pitch):
            box(v, cx + colw - pad, cx + pitch + pad, y0, y0 + depth, top - 8 + (k * 2), top - 6 + (k * 2), col)
            box(v, cx + colw - pad, cx + pitch + pad, y1 - depth, y1, top - 8 + (k * 2), top - 6 + (k * 2), col)
        for cy in range(y0, y1 - colw, pitch):
            box(v, x0, x0 + depth, cy + colw - pad, cy + pitch + pad, top - 8 + (k * 2), top - 6 + (k * 2), col)
            box(v, x1 - depth, x1, cy + colw - pad, cy + pitch + pad, top - 8 + (k * 2), top - 6 + (k * 2), col)


def balustrade(v, x0, x1, y0, y1, z0, height=14, pitch=8):
    """欄干（回廊とバルコニーの手すり）— 白石の小柱に金の笠木。"""
    for cx in range(x0, x1, pitch):
        box(v, cx, cx + 3, y0, y0 + 3, z0, z0 + height - 3, K('marb_an_m'))
        box(v, cx, cx + 3, y1 - 3, y1, z0, z0 + height - 3, K('marb_an_m'))
    for cy in range(y0, y1, pitch):
        box(v, x0, x0 + 3, cy, cy + 3, z0, z0 + height - 3, K('marb_an_m'))
        box(v, x1 - 3, x1, cy, cy + 3, z0, z0 + height - 3, K('marb_an_m'))
    for (a0, a1, b0, b1) in ((x0, x1, y0, y0 + 3), (x0, x1, y1 - 3, y1),
                             (x0, x0 + 3, y0, y1), (x1 - 3, x1, y0, y1)):
        box(v, a0, a1, b0, b1, z0 + height - 3, z0 + height, K('gold_an'))
        box(v, a0, a1, b0, b1, z0, z0 + 2, K('marb_an_md'))


def hip_roof(v, x0, x1, y0, y1, z0, pitch=2.0, cap=None, slate=None, ridge=None, eave=3):
    """**急な寄棟**（軒から棟まで `pitch` の勾配で立ち上がる）。棟と隅棟は金。

    高さは辺までの距離 × `pitch`（`cap` で頭打ち）。真ん中に平らな棟が通るので、
    上から見ると「線」になる——ここが遠目の輪郭を作る。
    """
    slate = [K('tile_an'), K('tile_an_l'), K('tile_an_d')] if slate is None else slate
    ridge = K('gold_an_l') if ridge is None else ridge
    span = min(x1 - x0, y1 - y0) // 2
    cap = int(span * pitch) if cap is None else cap
    #! 軒（1 マスぶん外へ出す）。
    box(v, x0 - 4, x1 + 4, y0 - 4, y1 + 4, z0, z0 + eave, K('bark_an_wl'))
    box(v, x0 - 4, x1 + 4, y0 - 4, y1 + 4, z0 + eave - 1, z0 + eave, K('gold_an_d'))
    cap = min(cap, v.shape[2] - z0)          #: 体積からはみ出さない（範囲外で落ちる）
    for x in range(x0, x1):
        for y in range(y0, y1):
            d = min(x - x0, x1 - 1 - x, y - y0, y1 - 1 - y)
            h = min(cap, int(round(d * pitch)) + eave)
            #! **殻だけ**（下は見えない）。中まで詰めると .vox が桁違いに膨れる（§9 の罠）。
            v[x, y, max(0, z0 + h - 12):z0 + h] = slate[((x // 5) + (y // 5)) % len(slate)]
            v[x, y, z0 + h - 1] = K('tile_an_d')
            if (h >= cap) or (abs((x - x0) - (y - y0)) < 2) or (abs((x1 - 1 - x) - (y - y0)) < 2):
                v[x, y, z0 + h - 1] = ridge      # 棟と隅棟
    return z0 + cap


def spire(v, cx, cy, z0, z1, r0, r1=0.0, body=None, rib=None, ribs=8):
    """**針のような尖塔** — 八角の錐に金の稜線を通し、頂に金の芽を載せる。"""
    body = K('marb_an_m') if body is None else body
    rib = K('gold_an') if rib is None else rib
    n = max(1, z1 - z0)
    for k in range(n):
        z = z0 + k
        r = r0 + ((r1 - r0) * (k / float(n)))
        if r < 0.6:
            break
        octagon(v, cx, cy, r, z, z + 1, body)
        if (k % 3) == 0:
            for j in range(ribs):
                a = j * (6.283 / ribs)
                px = cx + int(round(np.cos(a) * (r - 0.4)))
                py = cy + int(round(np.sin(a) * (r - 0.4)))
                box(v, px, px + 1, py, py + 1, z, z + 1, rib)
    #! 頂の芽（金。**尖塔は先が細って消えるので、必ず頭を作る**）。
    tip = z0 + int(n * 0.94)
    box(v, cx - 1, cx + 2, cy - 1, cy + 2, tip, tip + 6, K('gold_an'))
    box(v, cx - 2, cx + 3, cy - 2, cy + 3, tip + 4, tip + 6, K('gold_an_l'))
    box(v, cx, cx + 1, cy, cy + 1, tip + 6, tip + 12, K('gold_an_l'))


def lit_window(v, x0, x1, y0, y1, z0, z1, frame=None):
    """頭の丸い高い窓（灯りの色をそのまま入れて、昼も夜も形が読めるようにする）。"""
    frame = K('bark_an_wl') if frame is None else frame
    box(v, x0 - 2, x1 + 2, y0, y1, z0 - 2, z1 + 6, frame)
    box(v, x0, x1, y0, y1, z0, z1, K('lit'))
    w = (x1 - x0) // 2
    for k in range(w):
        box(v, x0 + k, x1 - k, y0, y1, z1 - 1 + k, z1 + k, K('lit'))
    box(v, x0 - 2, x1 + 2, y0, y1, z0 - 2, z0 - 1, K('gold_an'))

# ------------------------------------------------------------------ 遠目の骨（大物）
def manor_ang(name):
    """**最後の憩いの館**（`landmarks` の BUILDING_1）— 町でいちばん大きな一点物。

    7×5 マス・12 マス。白い切石の基壇に**開いた列柱の回廊**を巡らせ、その上に
    バルコニーと高い窓の階を載せ、急な尖り屋根を 3 つ架ける。東の端から
    **針のような尖塔**が 12 マスまで立ち上がる。窓は昼も夜も灯る。

    **森の天蓋（5.9 マス）の倍の高さ**を持たせてある——低い建物は、どんなに作り込んでも
    遠目には 1 画素も見えない（前の担当の記録 §7.3）。
    """
    rng = rng_for(name)
    W, D, H = 224, 160, 384
    v = np.zeros((W, D, H), np.int16)
    #! 基壇（**マスいっぱい**に敷く＝接地は 7×5 マス）。中は空ける。
    box(v, 0, W, 0, D, 0, 18, K('marb_an_s'))
    box(v, 20, W - 20, 20, D - 20, 1, 14, 0)
    box(v, 2, W - 2, 2, D - 2, 14, 18, K('marb_an_md'))
    box(v, 4, W - 4, 4, D - 4, 17, 18, K('marb_an_m'))
    #! 南の大階段（**手前が y 大**）。
    for k in range(5):
        box(v, 84, 140, 148 + (k * 2), D, max(0, 15 - (k * 3)), 18, K('marb_an_md'))
        box(v, 84, 140, 148 + (k * 2), 150 + (k * 2), max(0, 15 - (k * 3)), 18 - (k * 3), K('marb_an_m'))
    #! 腰の繰形。
    box(v, 6, W - 6, 6, D - 6, 18, 22, K('marb_an_m'))
    box(v, 6, W - 6, 6, D - 6, 20, 22, K('gold_an_d'))
    #! 1 階＝**開いた列柱の回廊**（館の署名）。
    arcade(v, 12, 212, 12, 148, 22, 96, pitch=24, colw=8, depth=8)
    box(v, 8, 216, 8, 152, 96, 104, K('marb_an_m'))
    box(v, 8, 216, 8, 152, 102, 104, K('gold_an'))
    #! バルコニー（張り出した床と欄干）。
    box(v, 2, W - 2, 2, D - 2, 104, 110, K('marb_an_md'))
    box(v, 2, W - 2, 2, D - 2, 108, 110, K('marb_an_m'))
    balustrade(v, 2, W - 2, 2, D - 2, 110, height=16, pitch=10)
    #! 2 階の壁（白い漆喰と付け柱）。**中は空ける**。
    box(v, 24, 200, 24, 136, 104, 180, K('marb_an_m'))
    box(v, 26, 198, 26, 134, 104, 180, K('marb_an_md'))
    box(v, 36, 188, 36, 124, 106, 178, 0)
    box(v, 24, 200, 24, 136, 104, 108, K('marb_an_md'))
    for px in range(24, 200, 20):                              # 付け柱
        box(v, px, px + 5, 24, 26, 104, 180, K('bark_an_wl'))
        box(v, px, px + 5, 134, 136, 104, 180, K('bark_an_wl'))
    for py in range(24, 136, 20):
        box(v, 24, 26, py, py + 5, 104, 180, K('bark_an_wl'))
        box(v, 198, 200, py, py + 5, 104, 180, K('bark_an_wl'))
    #! 高い窓（南面 6 つ・北面 6 つ・東西 3 つずつ。灯りの色をそのまま入れる）。
    for px in range(34, 190, 30):
        lit_window(v, px, px + 16, 134, 136, 120, 156)
        lit_window(v, px, px + 16, 24, 26, 120, 156)
    for py in range(36, 126, 30):
        lit_window(v, 24, 26, py, py + 16, 120, 156)
        lit_window(v, 198, 200, py, py + 16, 120, 156)
    box(v, 20, 204, 20, 140, 180, 190, K('marb_an_m'))         # 蛇腹
    box(v, 20, 204, 20, 140, 188, 190, K('gold_an'))
    #! 両翼の急な寄棟。
    hip_roof(v, 24, 80, 24, 136, 190, pitch=2.2)
    hip_roof(v, 144, 200, 24, 136, 190, pitch=2.2)
    #! 中央の広間 — 3 階は**開いた回廊**（灯りの回廊）、その上に高い尖り屋根。
    arcade(v, 84, 140, 28, 132, 190, 250, pitch=26, colw=8, depth=8)
    box(v, 80, 144, 24, 136, 250, 258, K('marb_an_m'))
    box(v, 80, 144, 24, 136, 256, 258, K('gold_an'))
    hip_roof(v, 80, 144, 24, 136, 258, pitch=2.4)
    #! 東の尖塔（**12 マスまで立ち上がる**。町のどこからでも見える頭）。
    tcx, tcy = 190, 44
    octagon(v, tcx, tcy, 20, 18, 26, K('marb_an_md'))
    octagon(v, tcx, tcy, 17, 26, 252, K('marb_an_m'))
    for z in range(34, 250, 26):                               # 石の目地
        octagon(v, tcx, tcy, 17, z, z + 1, K('marb_an_md'))
    for k, z in enumerate(range(60, 240, 44)):                 # 細く高い窓（4 方）
        for (dx, dy) in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            px = tcx + int(round(dx * 14)) - 3
            py = tcy + int(round(dy * 14)) - 3
            box(v, px, px + 7, py, py + 7, z, z + 24, K('lit'))
            box(v, px - 1, px + 8, py - 1, py + 8, z - 2, z, K('gold_an'))
    octagon(v, tcx, tcy, 22, 252, 258, K('marb_an_md'))        # 塔の張り出し
    octagon(v, tcx, tcy, 20, 256, 258, K('marb_an_m'))
    spire(v, tcx, tcy, 258, 362, 18.0, 1.0)
    #! 幟（回廊の柱から垂らす。形だけ＝館は静止物）。
    for (px, colour) in ((30, K('cloth_an_g')), (108, K('cloth_an_w')), (186, K('cloth_an_g'))):
        box(v, px, px + 12, 146, 148, 40, 90, colour)
        box(v, px + 3, px + 9, 146, 148, 52, 78, K('gold_an'))
    #! 回廊に吊る角灯（枠だけ。光は `manor_lights_ang` が持つ）。
    for px in range(28, 200, 28):
        box(v, px, px + 6, 144, 150, 76, 84, K('gold_an'))
        box(v, px + 1, px + 5, 145, 149, 77, 83, K('fl_an_pale'))
    #! 根方の苔（石の建物が地面から生えて見えないように）。
    for _ in range(int(rng.integers(10, 16))):
        x, y = int(rng.integers(0, W - 8)), int(rng.integers(0, D - 8))
        blk = v[x:x + 8, y:y + 8, 0:4]
        blk[blk != 0] = K('moss_an_d')
    return one(hollow(v, 4))


def manor_lights_ang(name):
    """館の回廊の灯り（`features` の相方。夜は自発光がつく）。

    館は `landmarks` で建つので**火の欄を持てない**（`TownLandmark` にはその欄が無い）。
    そこで**同じマスへ `features` で灯りだけを重ねる**——`flame` と `glow` は
    `features` にも書けるようになった（2026-09-05）。
    """
    v = np.zeros((224, 160, 96), np.int16)
    for px in range(28, 200, 28):
        box(v, px + 1, px + 5, 145, 149, 77, 83, K('fl_an_pale'))
    return one(v)


def manor_lights_flame_ang(name):
    """館の回廊の灯りの芯（夜だけ、置く側が自発光をつけて重ねる）。"""
    v = np.zeros((224, 160, 96), np.int16)
    for px in range(28, 200, 28):
        box(v, px + 2, px + 4, 146, 148, 78, 84, K('fl_an_core'))
    return one(v)


def tower_sage_ang(name):
    """**賢者の塔**（`landmarks` の BUILDING_8）— 3×3 マス・**14 マスの白い尖塔**。

    前は生きた大樹をくり抜いた 11 マスの塔で、灰色の筒に見えた（§9）。いまは
    白い切石の八角の軸を細く高く取り、頂に張り出しと**針のような尖塔**を載せる。
    森の上へ 8 マス突き出るので、東の森の目印になる。
    """
    rng = rng_for(name)
    S, H = 96, 448
    v = np.zeros((S, S, H), np.int16)
    cx = cy = 48
    #! 接地の座布団（anchor のマスと東西南北の 3×3。基壇がそのまま接地面になる）。
    box(v, 8, 88, 8, 88, 0, 10, K('marb_an_s'))
    octagon(v, cx, cy, 34, 6, 18, K('marb_an_md'))
    octagon(v, cx, cy, 30, 16, 26, K('marb_an_m'))
    octagon(v, cx, cy, 26, 24, 36, K('marb_an_md'))
    #! 軸（八角。上へ細る）。
    for z in range(34, 300):
        r = 17.0 - (5.0 * ((z - 34) / 266.0))
        octagon(v, cx, cy, r, z, z + 1, K('marb_an_m'))
        octagon(v, cx, cy, r - 6.0, z, z + 1, 0)          # 中は空ける（.vox を膨らませない）
    for z in range(40, 296, 24):                               # 石の目地
        octagon(v, cx, cy, 17.0, z, z + 1, K('marb_an_md'))
    #! 金の稜線（8 本。白い軸に縦の線が入ると「尖塔」に読める）。
    for j in range(8):
        a = j * 0.785
        for z in range(34, 300, 2):
            r = 16.0 - (5.0 * ((z - 34) / 266.0))
            px = cx + int(round(np.cos(a) * r))
            py = cy + int(round(np.sin(a) * r))
            box(v, px, px + 1, py, py + 1, z, z + 1, K('gold_an_d'))
    #! 細く高い窓（4 方向 × 5 段）。
    for z in range(60, 280, 46):
        for (dx, dy) in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            r = 15.0 - (5.0 * ((z - 34) / 266.0))
            px = cx + int(round(dx * r)) - 3
            py = cy + int(round(dy * r)) - 3
            box(v, px, px + 7, py, py + 7, z, z + 26, K('lit'))
            box(v, px - 1, px + 8, py - 1, py + 8, z - 2, z, K('gold_an'))
            box(v, px - 1, px + 8, py - 1, py + 8, z + 26, z + 28, K('gold_an'))
    #! 頂の張り出し（書斎の回廊）と欄干。
    octagon(v, cx, cy, 22, 300, 308, K('marb_an_md'))
    octagon(v, cx, cy, 20, 306, 308, K('marb_an_m'))
    for j in range(16):
        a = j * 0.3925
        px = cx + int(round(np.cos(a) * 19))
        py = cy + int(round(np.sin(a) * 19))
        box(v, px - 1, px + 2, py - 1, py + 2, 308, 320, K('marb_an_m'))
    octagon(v, cx, cy, 20, 320, 323, K('gold_an'))
    #! 書斎（八角の胴。八方に灯る窓）。
    octagon(v, cx, cy, 14, 308, 348, K('marb_an_m'))
    for j in range(8):
        a = j * 0.785
        px = cx + int(round(np.cos(a) * 12)) - 2
        py = cy + int(round(np.sin(a) * 12)) - 2
        box(v, px, px + 5, py, py + 5, 316, 340, K('lit'))
    octagon(v, cx, cy, 17, 348, 354, K('marb_an_md'))
    #! **針のような尖塔**（ここが 14 マスの頭）。
    spire(v, cx, cy, 354, 428, 15.0, 1.0)
    #! 根方の苔と、絡む蔦（白い石を森に馴染ませる）。
    for _ in range(int(rng.integers(4, 8))):
        x, y = int(rng.integers(10, 78)), int(rng.integers(10, 78))
        blk = v[x:x + 8, y:y + 8, 0:8]
        blk[blk != 0] = K('moss_an_d')
    for z in range(10, 120, 3):
        dx = int(round(3 * np.sin(z * 0.16)))
        box(v, cx - 18 + dx, cx - 14 + dx, cy - 3, cy + 2, z, z + 3, K('leaf_an_g'))
    return one(hollow(v, 5))


def tower_sage_glow_ang(name):
    """賢者の塔の頂の光（`features` の相方。夜だけ灯る）。"""
    v = np.zeros((96, 96, 360), np.int16)
    for j in range(8):
        a = j * 0.785
        px = 48 + int(round(np.cos(a) * 12)) - 2
        py = 48 + int(round(np.sin(a) * 12)) - 2
        box(v, px, px + 5, py, py + 5, 316, 340, K('fl_an_core'))
    return one(v)


def tower_trump_ang(name):
    """**トランプ魔術の塔**（`landmarks` の BUILDING_13）— 2×2 マス・**13 マスの細い尖塔**。

    賢者の塔よりさらに細い。八角の白石の軸に金の稜線が 4 本だけ通り、頂の石の花弁の上に
    月の石が浮く。**森の中でただ 1 つ青白く光るもの**。
    """
    rng = rng_for(name)
    S, H = 64, 416
    v = np.zeros((S, S, H), np.int16)
    cx = cy = 32
    box(v, 2, 62, 2, 62, 0, 8, K('marb_an_s'))
    octagon(v, cx, cy, 22, 6, 16, K('marb_an_md'))
    octagon(v, cx, cy, 18, 14, 24, K('marb_an_m'))
    for z in range(22, 290):
        r = 11.0 - (3.4 * ((z - 22) / 268.0))
        octagon(v, cx, cy, r, z, z + 1, K('marb_an_m'))
    for z in range(28, 286, 22):
        octagon(v, cx, cy, 11.0, z, z + 1, K('marb_an_md'))
    for j in range(4):
        a = (j * 1.5708) + 0.785
        for z in range(22, 290, 2):
            r = 10.0 - (3.4 * ((z - 22) / 268.0))
            px = cx + int(round(np.cos(a) * r))
            py = cy + int(round(np.sin(a) * r))
            box(v, px, px + 1, py, py + 1, z, z + 1, K('gold_an_d'))
    #! 細く高い窓（南面に 5 つ）。
    for z in range(52, 264, 44):
        r = 10.0 - (3.4 * ((z - 22) / 268.0))
        box(v, cx - 3, cx + 3, cy + int(r) - 2, cy + int(r) + 2, z, z + 24, K('lit'))
        box(v, cx - 4, cx + 4, cy + int(r) - 2, cy + int(r) + 2, z - 2, z, K('gold_an'))
    #! 蔦（西面を這い上がる）。
    for z in range(6, 200, 3):
        dx = int(round(2 * np.sin(z * 0.19)))
        box(v, cx - 12, cx - 8, cy - 3 + dx, cy + 2 + dx, z, z + 3, K('leaf_an_g'))
    #! 石の花弁（8 枚。**5 段まで**——10 段では爪に見え、月の石が隠れた）。
    octagon(v, cx, cy, 15, 290, 296, K('marb_an_md'))
    octagon(v, cx, cy, 13, 294, 296, K('marb_an_m'))
    for k in range(8):
        a = k * 0.785
        px = cx + int(round(np.cos(a) * 12))
        py = cy + int(round(np.sin(a) * 12))
        for t in range(5):
            box(v, px - 2, px + 3, py - 2, py + 3, 296 + t, 299 + t, K('marb_an_m'))
            px += int(round(np.cos(a) * 0.9))
            py += int(round(np.sin(a) * 0.9))
    #! 中央に浮く月の石と、その上へ続く針。
    octagon(v, cx, cy, 3, 296, 316, K('marb_an_md'))
    ball(v, cx, cy, 326, 11, K('marb_an_m'))
    ball(v, cx, cy, 326, 7, K('fl_an_moon'))
    spire(v, cx, cy, 336, 400, 7.0, 0.8)
    for _ in range(int(rng.integers(2, 5))):
        x, y = int(rng.integers(8, 48)), int(rng.integers(8, 48))
        blk = v[x:x + 6, y:y + 6, 0:6]
        blk[blk != 0] = K('moss_an_d')
    return one(hollow(v, 4))


def tower_trump_glow_ang(name):
    """トランプ魔術の塔の月の石の光（`features` の相方。夜は青白く灯る）。"""
    v = np.zeros((64, 64, 344), np.int16)
    ball(v, 32, 32, 326, 8, K('fl_an_core'))
    return one(v)

# ------------------------------------------------------------------ 水（泉と段々の水盤）
#
# **この町のデータに水のマスは 1 つも無い**（`04_Angwil.txt` を数えた）。だから
# 泉は地形ではなく `features` で置く——落ちる水は**明滅**（`blink`）で動かす。
# 手本はモリバントの噴水 `fountain_jet_mor`（`morivant_prefabs.py` の末尾）で、
# 段ごとに別のパーツを作り、位相をずらして順に点すと粒が動いて見える。
FOUNT_STAGES = 12   #: 1 周の段数
FOUNT_PERIOD = 2.1  #: 1 周の秒数
FOUNT_DUTY = 0.26   #: 各段が見えている割合（3 段ほどが同時に点く＝粒の列）


def _blob(v, cx, cy, cz, r, colour):
    """水滴の塊（球）。`_fj_blob`（モリバント）と同じ流儀。"""
    ri = int(np.ceil(r))
    for x in range(max(0, cx - ri), min(v.shape[0], cx + ri + 1)):
        for y in range(max(0, cy - ri), min(v.shape[1], cy + ri + 1)):
            for z in range(max(0, cz - ri), min(v.shape[2], cz + ri + 1)):
                if (((x - cx) ** 2) + ((y - cy) ** 2) + ((z - cz) ** 2)) <= (r * r):
                    v[x, y, z] = colour


def _bowl(v, cx, cy, r, z0, z1, wall=5, water_top=None):
    """石の水盤（八角の鉢）。内側を掘って水を張り、縁に金の線を通す。"""
    octagon(v, cx, cy, r, z0, z1, K('marb_an_m'))
    octagon(v, cx, cy, r, z0, z0 + 2, K('marb_an_md'))
    octagon(v, cx, cy, r - wall, z0 + 3, z1, 0)
    top = (z1 - 2) if water_top is None else water_top
    octagon(v, cx, cy, r - wall, z0 + 3, top, K('wa_an_c'))
    octagon(v, cx, cy, r - wall, top - 1, top, K('wa_an_cl'))
    octagon(v, cx, cy, r, z1 - 1, z1, K('gold_an'))
    octagon(v, cx, cy, r - wall + 1, z1 - 1, z1, 0)


def fountain_ang(name):
    """**泉**（`features`。3×3 マス・3.9 マス）— 3 段の水盤から水が落ちる。

    上の鉢から水が噴き上がり、縁から中の鉢へ、そこから下の池へ落ちる。**落ちる水は
    段ごとに別のパーツ**で、`blink` の位相をずらして順に点す（モリバントの噴水と同じ）。
    """
    S, H = 96, 176
    v = np.zeros((S, S, H), np.int16)
    cx = cy = 48
    #! 敷石の台（**マスいっぱい**＝接地は 3×3 マス）。
    box(v, 0, S, 0, S, 0, 4, K('marb_an_s'))
    box(v, 2, S - 2, 2, S - 2, 3, 4, K('marb_an_md'))
    #! 下の池（いちばん大きい鉢）。
    _bowl(v, cx, cy, 44, 2, 20, wall=7, water_top=15)
    #! 台座 → 中の鉢 → 茎 → 上の鉢。
    octagon(v, cx, cy, 12, 14, 48, K('marb_an_m'))
    for z in range(18, 46, 8):
        octagon(v, cx, cy, 12, z, z + 1, K('gold_an_d'))
    _bowl(v, cx, cy, 27, 46, 62, wall=6, water_top=58)
    octagon(v, cx, cy, 7, 58, 84, K('marb_an_m'))
    _bowl(v, cx, cy, 16, 82, 96, wall=5, water_top=92)
    #! 頂の金の芽（噴き上げの根元）。
    octagon(v, cx, cy, 4, 92, 104, K('gold_an'))
    octagon(v, cx, cy, 6, 102, 106, K('gold_an_l'))
    #! **噴き上げの芯（静止）**。水滴だけだと粒が宙に浮いて見え、噴水の形が読めない
    #! （モリバントの `fountain_jet_mor` と同じ手当て。絵で判じた。§9）。
    for z in range(104, 156):
        t = (z - 104) / 52.0
        octagon(v, cx, cy, 3.6 - (2.4 * t), z, z + 1,
                K('wa_an_cl') if t < 0.6 else K('wa_an_f'))
    parts = [('main', v, (0, 0, 0))]
    #! ---- 落ちる水（明滅）----
    for stage in range(FOUNT_STAGES):
        w = np.zeros((S, S, H), np.int16)
        t = (stage + 0.5) / FOUNT_STAGES
        #! 噴き上げ（頂から昇って砕ける）。
        rise = 106 + int(round(50.0 * np.sin(min(1.0, t * 1.6) * 1.57)))
        for k in range(4):
            a = (k * 1.5708) + (stage * 0.5)
            rad = 2.0 + (6.0 * t)
            _blob(w, cx + int(round(np.cos(a) * rad)), cy + int(round(np.sin(a) * rad)), rise,
                  3.0 - (1.0 * t), K('wa_an_f') if (k % 2) else K('wa_an_cl'))
        #! 上の鉢 → 中の鉢（8 方向の細い簾）。
        za = 92 - int(round(t * 34))
        for k in range(8):
            a = k * 0.785
            rad = 15 + int(round(t * 8))
            _blob(w, cx + int(round(np.cos(a) * rad)), cy + int(round(np.sin(a) * rad)), za,
                  2.4, K('wa_an_cl'))
        #! 中の鉢 → 下の池（8 方向）。位相をずらして「後から落ちる」ように見せる。
        u = ((t + 0.35) % 1.0)
        zb = 58 - int(round(u * 42))
        for k in range(8):
            a = (k * 0.785) + 0.39
            rad = 26 + int(round(u * 14))
            _blob(w, cx + int(round(np.cos(a) * rad)), cy + int(round(np.sin(a) * rad)), zb,
                  2.6, K('wa_an_cl') if (k % 2) else K('wa_an_c'))
        #! 水面のしぶき（落ちきる直前）。
        if u > 0.82:
            for k in range(6):
                a = k * 1.047
                _blob(w, cx + int(round(np.cos(a) * 30)), cy + int(round(np.sin(a) * 30)), 17,
                      2.2, K('wa_an_f'))
        if not w.any():
            w[cx, cy, H - 1] = K('wa_an_f')
        parts.append(('w%02d' % stage, w, (0, 0, 0)))
    return parts


def fountain_parts():
    """泉の部品宣言。`main` は静止、水の段は `blink` で順に点す。"""
    decl = [{'name': 'main', 'voxels': 'main', 'grounded': True,
             'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    for stage in range(FOUNT_STAGES):
        decl.append({'name': 'w%02d' % stage, 'voxels': 'w%02d' % stage, 'grounded': False,
                     'motion': {'kind': 'blink', 'period': FOUNT_PERIOD,
                                'phase': round(stage / float(FOUNT_STAGES), 4),
                                'duty': FOUNT_DUTY},
                     'wind_k': 0.0})
    return decl


CASC_STAGES = 8
CASC_PERIOD = 1.6
CASC_DUTY = 0.3


def cascade_ang(name):
    """**段々の水盤**（`features`。1 マス・3.4 マス）— 壁龕から 3 段に水が落ちる。

    泉より小さく、道沿いに何基も置ける。落ちる水は `blink` の 8 段。
    """
    H = 110
    v = vol(h=H)
    #! 背の立石（水を受ける壁）。
    box(v, 4, 28, 2, 8, 0, 96, K('marb_an_m'))
    box(v, 5, 27, 2, 8, 0, 94, K('marb_an_md'))
    for k in range(6):                                        # 頭を丸める
        box(v, 5 + k, 27 - k, 2, 8, 94 + k, 95 + k, K('marb_an_m'))
    box(v, 12, 20, 6, 8, 40, 86, K('gold_an'))                # 刻んだ印
    box(v, 14, 18, 6, 8, 44, 82, K('gold_an_d'))
    #! 3 段の鉢（南へ向かって下がる）。
    _bowl(v, 16, 12, 11, 62, 74, wall=4, water_top=71)
    _bowl(v, 16, 18, 13, 34, 46, wall=4, water_top=43)
    _bowl(v, 16, 24, 15, 4, 20, wall=5, water_top=16)
    parts = [('main', v, (0, 0, 0))]
    for stage in range(CASC_STAGES):
        w = vol(h=H)
        t = (stage + 0.5) / CASC_STAGES
        for (z0, z1, cy0, cy1, phase) in ((71, 46, 12, 18, 0.0), (43, 20, 18, 24, 0.4)):
            u = ((t + phase) % 1.0)
            z = int(round(z0 + ((z1 - z0) * u)))
            cy = int(round(cy0 + ((cy1 - cy0) * u)))
            for dx in (-4, 0, 4):
                _blob(w, 16 + dx, cy, z, 2.0, K('wa_an_cl') if (dx == 0) else K('wa_an_c'))
        if not w.any():
            w[16, 16, H - 1] = K('wa_an_f')
        parts.append(('w%d' % stage, w, (0, 0, 0)))
    return parts


def cascade_parts():
    decl = [{'name': 'main', 'voxels': 'main', 'grounded': True,
             'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    for stage in range(CASC_STAGES):
        decl.append({'name': 'w%d' % stage, 'voxels': 'w%d' % stage, 'grounded': False,
                     'motion': {'kind': 'blink', 'period': CASC_PERIOD,
                                'phase': round(stage / float(CASC_STAGES), 4),
                                'duty': CASC_DUTY},
                     'wind_k': 0.0})
    return decl


# ------------------------------------------------------------------ 高い回廊・像・関門
def colonnade_ew_ang(name):
    """**高い回廊**（`features`。3×1 マス・6.8 マス）— 白石の柱の上を渡る屋根つきの廊下。

    木のあいだを渡すので、**森の天蓋（5.9 マス）より高く**取る（§7.3 の罠）。
    吊り橋（縄と板）と対になる、石の側の渡り。歩けるマスの上を跨ぐので当たり判定は
    増えない（「踏破できるなら何を置いてもよい」と決めた）。
    """
    W, D, H = 96, 32, 248
    v = np.zeros((W, D, H), np.int16)
    #! 柱（両端のマスに 2 本ずつ。**接地はその 2 マスだけ**）。
    for cx in (12, 84):
        for cy in (10, 22):
            octagon(v, cx, cy, 5.0, 0, 150, K('marb_an_m'))
            octagon(v, cx, cy, 7.0, 0, 6, K('marb_an_s'))
            octagon(v, cx, cy, 5.0, 144, 150, K('gold_an'))
        #! 柱のあいだのアーチ（東西に抜ける）。
        for k in range(24):
            a = (k / 23.0) * np.pi
            px = cx + int(round(-np.cos(a) * 0))
            pz = 128 + int(round(np.sin(a) * 0))
            box(v, px, px + 1, 10, 23, pz, pz + 1, K('marb_an_m'))
    #! 床（3 マスぶん通す）と欄干。
    box(v, 0, W, 6, 26, 150, 158, K('marb_an_md'))
    box(v, 0, W, 6, 26, 156, 158, K('marb_an_m'))
    balustrade(v, 0, W, 6, 26, 158, height=16, pitch=8)
    #! 屋根を支える細い柱（4 本おき）と、低い切妻の屋根。
    for px in range(4, W, 14):
        box(v, px, px + 3, 8, 11, 174, 190, K('marb_an_m'))
        box(v, px, px + 3, 21, 24, 174, 190, K('marb_an_m'))
    box(v, 0, W, 6, 26, 188, 192, K('marb_an_m'))
    hip_roof(v, 0, W, 4, 28, 192, pitch=2.6)
    #! 吊り灯（床の下。夜、回廊の線が読める）。
    for px in range(10, W, 20):
        box(v, px, px + 4, 14, 18, 142, 150, K('gold_an'))
        box(v, px + 1, px + 3, 15, 17, 143, 149, K('fl_an_pale'))
    return one(hollow(v, 4))


def colonnade_ns_ang(name):
    """高い回廊（南北へ渡る）。**`rotate_parts_90` は 1 マスの素材専用**なので、
    3 マスぶんの体積を自分で 90 度回す（再サンプリング無しなので形は崩れない）。"""
    return [(nm, np.ascontiguousarray(np.rot90(vv, k=1, axes=(0, 1))), org)
            for (nm, vv, org) in colonnade_ew_ang(name)]


def statue_ang(name):
    """**石の像**（1 マス・3.6 マス）— 台座に立つ白石のエルフ。掲げた手に金の星。

    初版は八角の錐に横の彫りを入れたので、**輪を積んだ柱**に見えた（絵で判じた。§9）。
    彫りを縦にし、肩・頭・腕をはっきり出して、人の形に読めるようにした。
    """
    rng = rng_for(name)
    H = 116
    v = vol(h=H)
    #! 台座（八角。3 段）。
    octagon(v, 16, 16, 11, 0, 5, K('marb_an_s'))
    octagon(v, 16, 16, 10, 4, 8, K('marb_an_md'))
    octagon(v, 16, 16, 9, 7, 22, K('marb_an_m'))
    octagon(v, 16, 16, 10, 21, 24, K('marb_an_md'))
    box(v, 6, 27, 6, 27, 22, 23, K('gold_an_d'))
    #! 衣（裾は広く腰で締まる。**彫りは縦**——横に入れると輪を積んだ柱に見える）。
    #! **裾を思い切り広く**取る（r 13 → 5）。細い錐は柱にしか見えない（絵で判じた）。
    for z in range(24, 68):
        t = (z - 24) / 44.0
        r = 13.0 - (8.0 * (t ** 0.7))
        octagon(v, 16, 16, r, z, z + 1, K('marb_an_m'))
    for a in (0.4, 1.2, 2.0, 2.8, 3.6, 4.4, 5.2, 6.0):        # 縦の襞
        for z in range(26, 66):
            t = (z - 24) / 44.0
            r = 12.4 - (8.0 * (t ** 0.7))
            px = 16 + int(round(np.cos(a) * r))
            py = 16 + int(round(np.sin(a) * r))
            v[px, py, z] = K('marb_an_md')
    #! 胴と肩（前へ少し出す＝正面が読める）。
    box(v, 11, 22, 12, 21, 66, 76, K('marb_an_m'))
    box(v, 8, 25, 10, 23, 72, 78, K('marb_an_m'))             # 肩（胴より広く張る）
    box(v, 8, 25, 10, 23, 77, 78, K('gold_an'))               # 襟の金
    box(v, 12, 21, 21, 23, 66, 76, K('marb_an_md'))           # 衣の前身頃
    #! 首と頭（**細い首で切る**——切らないと肩から上が 1 本の柱に見える）。
    box(v, 14, 19, 14, 19, 78, 82, K('marb_an_md'))
    ball(v, 16, 16, 88, 6, K('marb_an_m'), squash=1.2)
    box(v, 10, 23, 11, 22, 93, 96, K('gold_an'))              # 冠
    box(v, 15, 18, 11, 13, 94, 101, K('gold_an_l'))           # 冠の飾り
    #! 掲げた右腕（東へ張り出して上へ）と、その先の金の星。
    box(v, 22, 26, 14, 19, 70, 76, K('marb_an_m'))
    box(v, 23, 27, 14, 19, 74, 94, K('marb_an_m'))
    box(v, 21, 26, 13, 20, 92, 96, K('marb_an_md'))           # 手
    box(v, 22, 25, 15, 18, 96, 102, K('gold_an'))
    box(v, 20, 27, 13, 20, 100, 104, K('gold_an_l'))          # 星
    box(v, 22, 25, 15, 18, 102, 112, K('gold_an_l'))
    box(v, 20, 27, 15, 18, 105, 107, K('gold_an_l'))
    #! 下ろした左腕（衣に沿わせる）。
    box(v, 5, 9, 13, 20, 52, 74, K('marb_an_m'))
    box(v, 5, 9, 13, 20, 72, 74, K('gold_an_d'))
    for _ in range(int(rng.integers(2, 4))):                  # 根方の苔
        x, y = int(rng.integers(4, 24)), int(rng.integers(4, 24))
        blk = v[x:x + 5, y:y + 5, 0:5]
        blk[blk != 0] = K('moss_an_d')
    return one(hollow(v, 3))


def gatehouse_ang(name):
    """**森の関門**（`features`。2 マス幅・9 マス）— 道をまたぐ白石の門楼。

    大きなアーチの上に、灯る窓を並べた詰所と急な屋根を載せ、両端に小さな尖塔を立てる。
    前は丸太の門楼（7.2 マス）で、森の色に沈んで街道の目印にならなかった（§9）。
    """
    rng = rng_for(name)
    W, D, H = 64, 32, 296
    v = np.zeros((W, D, H), np.int16)
    box(v, 0, 32, 0, D, 0, 2, K('marb_an_s'))                 # anchor のマスの座布団
    box(v, 32, W, 0, D, 0, 2, K('marb_an_s'))
    #! 2 本の脚（東西の端。大きなアーチを支える）。
    for cx in (2, 50):
        box(v, cx, cx + 12, 4, 28, 0, 130, K('marb_an_m'))
        box(v, cx + 1, cx + 11, 4, 28, 0, 130, K('marb_an_md'))
        box(v, cx + 2, cx + 10, 4, 28, 0, 130, K('marb_an_m'))
        box(v, cx - 2, cx + 14, 2, 30, 0, 6, K('marb_an_s'))
        box(v, cx - 2, cx + 14, 2, 30, 124, 130, K('gold_an_d'))
    #! アーチ（南北にくぐる。**64 段で刻む**）。
    for k in range(64):
        t = k / 63.0
        a = t * np.pi
        px = 32 - int(round(np.cos(a) * 18))
        pz = 130 + int(round(np.sin(a) * 22))
        box(v, px - 4, px + 4, 4, 28, pz, pz + 6, K('marb_an_m'))
        box(v, px - 4, px + 4, 4, 6, pz, pz + 6, K('marb_an_md'))
        box(v, px - 4, px + 4, 26, 28, pz, pz + 6, K('marb_an_md'))
    box(v, 0, W, 4, 28, 152, 160, K('marb_an_m'))             # アーチの上の帯
    box(v, 0, W, 4, 28, 158, 160, K('gold_an'))
    #! 詰所（灯る窓が 4 つ）。
    box(v, 4, 60, 2, 30, 160, 216, K('marb_an_m'))
    box(v, 6, 58, 4, 28, 160, 216, K('marb_an_md'))
    for px in range(10, 56, 14):
        lit_window(v, px, px + 8, 28, 30, 176, 200)
        lit_window(v, px, px + 8, 2, 4, 176, 200)
    box(v, 2, 62, 0, D, 216, 224, K('marb_an_m'))             # 蛇腹
    box(v, 2, 62, 0, D, 222, 224, K('gold_an'))
    hip_roof(v, 4, 60, 2, 30, 224, pitch=2.6)
    #! 両端の小さな尖塔。
    for cx in (8, 56):
        octagon(v, cx, 16, 8, 160, 244, K('marb_an_m'))
        octagon(v, cx, 16, 10, 244, 250, K('marb_an_md'))
        spire(v, cx, 16, 250, 288, 9.0, 0.8, ribs=4)
    #! 門の下に吊る角灯（夜の目印）。
    for cx in (18, 46):
        box(v, cx - 1, cx + 2, 15, 18, 132, 152, K('gold_an_d'))
        box(v, cx - 4, cx + 5, 12, 21, 122, 132, K('gold_an'))
        box(v, cx - 3, cx + 4, 13, 20, 123, 131, K('fl_an_pale'))
    for _ in range(int(rng.integers(3, 6))):                  # 根方の苔
        x, y = int(rng.integers(0, W - 6)), int(rng.integers(0, D - 6))
        blk = v[x:x + 6, y:y + 6, 0:6]
        blk[blk != 0] = K('moss_an_d')
    return one(hollow(v, 4))

def great_tree_ang(name):
    """**母なる大樹** — 館の北に立つ 5×5 マス・11 マスの巨木。

    幹に螺旋の段、途中に 2 段の樹上の家、頂に見張り台、幹から吊り灯。
    **接地するのは真ん中のマスだけ**（footprint は anchor の 1 マス）
    ——だから根も家も z=1 から上に置き、真ん中のマスの z=0 にだけ座布団を敷く。

    初版は 3×3 マス・14 マスで、幹の半径が 13 ボクセル（0.4 マス）しかなく
    **細い塔に見えた**。幹を 1.6 マス径まで太らせ、樹冠を 4.5 マスまで広げ、
    高さを 11 マスへ下げて釣り合いを取り直した（前の担当の記録 §7.1）。
    **2026-09-05 に幹を白く・樹冠を金にした**（§9）。
    """
    rng = rng_for(name)
    S = 160
    H = 352
    v = np.zeros((S, S, H), np.int16)
    cx = cy = 80
    #! 座布団（anchor のマス x64..96 / y64..96 だけ z=0 を持つ）。
    box(v, 64, 96, 64, 96, 0, 1, K('bark_an_sd'))
    #! 板根（円錐に広がる裾と、6 方向へ這う根）。
    for k in range(16):
        disc(v, cx, cy, 44 - (k * 1.3), 1 + k, 2 + k, K('bark_an_sd'))
    for angle in (0.4, 1.9, 3.4, 4.9, 2.7, 5.8):
        for t in range(28):
            px = cx + int(round(np.cos(angle) * (20 + t)))
            py = cy + int(round(np.sin(angle) * (20 + t)))
            hgt = max(1, 26 - t)
            box(v, px - 4, px + 4, py - 4, py + 4, 1, hgt, K('bark_an_sd'))
            box(v, px - 3, px + 3, py - 3, py + 3, 1, max(1, hgt - 2), K('bark_an_s'))
    #! 幹（半径 26 → 12。白い樹皮に縦の裂け目）。
    for z in range(1, 236):
        r = 26.0 - (14.0 * (z / 236.0))
        disc(v, cx, cy, r, z, z + 1, K('bark_an_s'))
        disc(v, cx, cy, r - 5.0, z, z + 1, K('bark_an_sl'))
    for z in range(8, 228, 11):
        box(v, cx + 14, cx + 22, cy - 4, cy + 4, z, z + 5, K('bark_an_scar'))
        box(v, cx - 22, cx - 14, cy - 3, cy + 5, z + 5, z + 10, K('bark_an_scar'))
    #! 螺旋の段（幹を巻いて上がる。踏み板と手すりの支柱）。
    for k in range(50):
        a = k * 0.42
        rr = 30.0 - (k * 0.16)
        px = cx + int(round(np.cos(a) * rr))
        py = cy + int(round(np.sin(a) * rr))
        z = 12 + (k * 4)
        box(v, px - 5, px + 5, py - 5, py + 5, z, z + 3, K('bark_an_wd'))
        box(v, px - 4, px + 4, py - 4, py + 4, z + 2, z + 3, K('bark_an_w'))
        if (k % 3) == 0:
            box(v, px - 1, px + 2, py - 1, py + 2, z + 3, z + 15, K('bark_an_sd'))
    #! 樹上の家（2 段。下は大きく南向き、上は小さく西向き）。
    for (hx, hy, hz, hw, hd, hh) in ((52, 96, 116, 52, 40, 40), (30, 52, 182, 40, 34, 34)):
        box(v, hx, hx + hw, hy, hy + hd, hz, hz + hh, K('bark_an_sd'))
        box(v, hx + 1, hx + hw - 1, hy + 1, hy + hd - 1, hz + 1, hz + hh, K('dark'))
        for p in range(hx, hx + hw, 5):
            box(v, p, p + 4, hy, hy + 1, hz, hz + hh, K('bark_an_s'))
            box(v, p, p + 4, hy + hd - 1, hy + hd, hz, hz + hh, K('bark_an_s'))
        for p in range(hy, hy + hd, 5):
            box(v, hx, hx + 1, p, p + 4, hz, hz + hh, K('bark_an_s'))
            box(v, hx + hw - 1, hx + hw, p, p + 4, hz, hz + hh, K('bark_an_s'))
        #! 床（張り出した濡れ縁）と手すり。
        box(v, hx - 5, hx + hw + 5, hy - 5, hy + hd + 5, hz - 4, hz, K('bark_an_wd'))
        box(v, hx - 5, hx + hw + 5, hy - 5, hy + hd + 5, hz - 1, hz, K('bark_an_w'))
        for (a0, a1) in ((hx - 5, hx - 2), (hx + hw + 2, hx + hw + 5)):
            box(v, a0, a1, hy - 5, hy + hd + 5, hz, hz + 9, K('bark_an_wd'))
        for (a0, a1) in ((hy - 5, hy - 2), (hy + hd + 2, hy + hd + 5)):
            box(v, hx - 5, hx + hw + 5, a0, a1, hz, hz + 9, K('bark_an_wd'))
        #! 窓（南面。灯りの色をそのまま置く＝この 1 個で昼も夜も形が読める）。
        box(v, hx + (hw // 2) - 8, hx + (hw // 2) + 8, hy + hd - 1, hy + hd, hz + 10, hz + 24,
            K('lit'))
        #! 屋根（急な切妻。棟は東西。銀青のスレート）。
        for k in range(14):
            box(v, hx - 6 + k, hx + hw + 6 - k, hy - 6, hy + hd + 6, hz + hh + (k * 2),
                hz + hh + (k * 2) + 3, K('tile_an'))
            box(v, hx - 6 + k, hx + hw + 6 - k, hy - 6, hy + hd + 6, hz + hh + (k * 2) + 2,
                hz + hh + (k * 2) + 3, K('tile_an_l'))
    #! 頂の見張り台（幹の頭。手すりだけの円い床）。
    disc(v, cx, cy, 26.0, 236, 241, K('bark_an_wd'))
    disc(v, cx, cy, 24.0, 240, 241, K('bark_an_w'))
    for k in range(14):
        a = k * 0.449
        px = cx + int(round(np.cos(a) * 23))
        py = cy + int(round(np.sin(a) * 23))
        box(v, px - 2, px + 2, py - 2, py + 2, 241, 256, K('bark_an_sd'))
    disc(v, cx, cy, 26.0, 254, 257, K('gold_an'))
    #! 大枝（6 本。樹冠を支える）。
    for angle in (0.3, 1.35, 2.4, 3.45, 4.5, 5.55):
        for t in range(30):
            px = cx + int(round(np.cos(angle) * (14 + (t * 1.9))))
            py = cy + int(round(np.sin(angle) * (14 + (t * 1.9))))
            z = 208 + int(round(t * 2.0))
            r = max(3, 10 - (t // 4))
            box(v, px - r, px + r, py - r, py + r, z, z + r, K('bark_an_sl'))
    #! 樹冠（塊ごとに 1 色。上ほど明るい金）。
    shades = [K('leaf_an_d'), K('leaf_an_m'), K('leaf_an_l'), K('leaf_an_h')]
    for _ in range(260):
        bx = cx + rng.normal(0, 34)
        by = cy + rng.normal(0, 34)
        bz = 276 + rng.normal(0, 34)
        bw, bd = rng.uniform(8, 17), rng.uniform(8, 17)
        bh = rng.uniform(5, 11)
        idx = int(min(3, max(0, rng.normal(1.4 + ((bz - 250) / 60.0), 0.8))))
        block = v[max(0, int(bx - bw)):int(bx + bw), max(0, int(by - bd)):int(by + bd),
                  max(0, int(bz - bh)):int(bz + bh)]
        if block.size == 0:
            continue
        block[block == 0] = shades[idx]
    #! 樹冠の底を椀に削る（四角い塊のままだと影も四角い）。
    xs = np.arange(S)[:, None, None]
    ys = np.arange(S)[None, :, None]
    zs = np.arange(H)[None, None, :]
    mask = np.isin(v, shades)
    far = (((xs - cx) ** 2) + ((ys - cy) ** 2)) > (72 ** 2)
    v[mask & far] = 0
    bowl = (zs < 268) & ((((xs - cx) ** 2) + ((ys - cy) ** 2)) > (38 ** 2))
    v[np.isin(v, shades) & bowl] = 0
    #! 吊り灯（幹の周りに 6 つ。**夜の目印**）。
    for k in range(6):
        a = k * 1.05
        px = cx + int(round(np.cos(a) * 34))
        py = cy + int(round(np.sin(a) * 34))
        z = 96 + ((k % 3) * 38)
        box(v, px - 2, px + 3, py - 2, py + 3, z + 10, z + 20, K('bark_an_wi'))
        box(v, px - 5, px + 6, py - 5, py + 6, z, z + 10, K('gold_an'))
        box(v, px - 4, px + 5, py - 4, py + 5, z + 1, z + 9, K('fl_an_pale'))
    return one(hollow(v, 5))


def great_tree_glow_ang(name):
    """母なる大樹の吊り灯の光（`features` の相方。夜は自発光がつく）。"""
    S = 160
    v = np.zeros((S, S, 220), np.int16)
    cx = cy = 80
    for k in range(6):
        a = k * 1.05
        px = cx + int(round(np.cos(a) * 34))
        py = cy + int(round(np.sin(a) * 34))
        z = 96 + ((k % 3) * 38)
        box(v, px - 4, px + 5, py - 4, py + 5, z + 1, z + 9, K('fl_an_core'))
    return one(v)


def rope_bridge_ew_ang(name):
    """**樹上の吊り橋**（東西へ渡る。3×1 マス）— 2 本の木の間に張った縄と板。

    綱と踏み板が**振り子で揺れる**。橋は歩けるマスの上を高く跨ぐので、
    当たり判定は 1 マスも増えない（「踏破できるなら何を置いてもよい」と決めた）。
    """
    rng = rng_for(name)
    S = 96
    H = 150
    frame = np.zeros((S, S, H), np.int16)
    deck = np.zeros((S, S, H), np.int16)
    #! 接地は anchor のマス（x32..64）の中だけ。両端の木は**その中に**立てる。
    box(frame, 32, 64, 32, 64, 0, 1, K('moss_an_d'))
    #! **木は左右の端のマスへ**（真ん中に 2 本並べた版は 1 本の木に見えた）。
    for (cx, cy) in ((13, 48), (83, 48)):
        for z in range(1, 118):
            r = 5.0 - (1.6 * (z / 118.0))
            disc(frame, cx, cy, r, z, z + 1, K('bark_an_s'))
            disc(frame, cx, cy, r - 2.0, z, z + 1, K('bark_an_sl'))
    #! 受けの櫓（橋を掛ける台）。
    for cx in (13, 83):
        box(frame, cx - 8, cx + 8, 42, 54, 96, 100, K('bark_an_wd'))
        box(frame, cx - 8, cx + 8, 42, 54, 99, 100, K('bark_an_w'))
        for (a0, a1) in ((42, 44), (52, 54)):
            box(frame, cx - 8, cx + 8, a0, a1, 100, 110, K('bark_an_sd'))
    #! 綱（東西いっぱい。たわむ）と踏み板。
    for x in range(0, S):
        t = (x - 48) / 48.0
        sag = int(round(6 * (1 - (t * t))))
        z = 100 - sag
        deck[x, 42:54, z - 2:z] = K('bark_an_wd') if ((x // 4) % 2) else K('bark_an_w')
        deck[x, 41:42, z + 2:z + 4] = K('bark_an_wi')     # 手綱（北）
        deck[x, 54:55, z + 2:z + 4] = K('bark_an_wi')     # 手綱（南）
        if (x % 9) == 0:
            deck[x, 41:42, z:z + 12] = K('bark_an_wi')
            deck[x, 54:55, z:z + 12] = K('bark_an_wi')
            deck[x, 41:55, z + 11:z + 13] = K('bark_an_wi')
    #! 木の頭の葉（橋を森に馴染ませる）。
    for _ in range(int(rng.integers(14, 20))):
        cx = int(rng.choice([13, 83]))
        bx = cx + int(rng.integers(-14, 15))
        by = 48 + int(rng.integers(-14, 15))
        bz = int(rng.integers(112, H - 8))
        box(frame, bx, bx + int(rng.integers(6, 11)), by, by + int(rng.integers(5, 10)), bz, bz + 5,
            pick(rng, [K('leaf_an_m'), K('leaf_an_l'), K('leaf_an_d')], [0.4, 0.32, 0.28]))
    return [('frame', frame, (0, 0, 0)), ('deck', deck, (0, 0, 0))]


def rope_bridge_ns_ang(name):
    """吊り橋（南北へ渡る）。**`rotate_parts_90` は 1 マスの素材専用**なので、
    ここは 3 マスぶんの体積を自分で 90 度回す（再サンプリング無しなので形は崩れない）。"""
    return [(nm, np.ascontiguousarray(np.rot90(vv, k=1, axes=(0, 1))), org)
            for (nm, vv, org) in rope_bridge_ew_ang(name)]


def bridge_parts():
    """吊り橋の部品宣言。踏み板と綱（`deck`）だけが**ゆっくり揺れる**。"""
    return [
        {'name': 'frame', 'voxels': 'frame', 'grounded': True,
         'motion': {'kind': 'static'}, 'wind_k': 0.0},
        {'name': 'deck', 'voxels': 'deck', 'parent': 'frame', 'grounded': False,
         'pivot': [48.0, 48.0, 110.0],
         'motion': {'kind': 'pendulum', 'axis': 'y', 'amplitude': 2.2, 'period': 4.2,
                    'phase': 0.0, 'damping': 0.0},
         'wind_k': 0.0},
    ]


def mill_ang(name):
    """**森番の高倉**（目印の建物 2 番＝`mill`。4×3 マス）。

    町の北の口に立つ、高床の倉と物見。丸太の脚・梯子・干した獣皮と角・鐘。
    水車小屋の枠（4×3）をそのまま使うが、**この町に水は 1 マスも無い**ので倉にした。
    """
    rng = rng_for(name)
    W, D, H = 128, 96, 214
    v = np.zeros((W, D, H), np.int16)
    #! 接地の座布団（anchor のマス x0..32 / y0..32 だけ）。
    box(v, 0, 32, 0, 32, 0, 1, K('dirt_an_pd'))
    #! 丸太の脚（8 本。ねずみ返しの円盤つき）。
    for px in (14, 46, 78, 110):
        for py in (16, 78):
            disc(v, px, py, 5.0, 1, 40, K('bark_an_s'))
            disc(v, px, py, 2.4, 1, 40, K('bark_an_sl'))
            disc(v, px, py, 9.0, 38, 41, K('bark_an_w'))     # ねずみ返し
    #! 倉の床と胴（縦板）。
    box(v, 6, 122, 8, 88, 41, 46, K('bark_an_wd'))
    box(v, 6, 122, 8, 88, 44, 46, K('bark_an_w'))
    box(v, 10, 118, 12, 84, 46, 104, K('bark_an_sd'))
    box(v, 12, 116, 14, 82, 46, 104, K('dark'))
    for p in range(10, 118, 5):
        box(v, p, p + 4, 12, 14, 46, 104, K('bark_an_s'))
        box(v, p, p + 4, 82, 84, 46, 104, K('bark_an_s'))
    for p in range(14, 82, 5):
        box(v, 10, 12, p, p + 4, 46, 104, K('bark_an_s'))
        box(v, 116, 118, p, p + 4, 46, 104, K('bark_an_s'))
    box(v, 10, 118, 12, 84, 100, 104, K('bark_an_w'))        # 軒下の彫りの帯
    #! 戸（南面の真ん中）。
    box(v, 52, 76, 82, 84, 46, 78, K('bark_an_w'))
    box(v, 63, 65, 82, 84, 46, 78, K('bark_an_wd'))
    #! 梯子（南面。地面から床へ）。
    box(v, 56, 59, 86, 89, 1, 46, K('bark_an_wd'))
    box(v, 69, 72, 86, 89, 1, 46, K('bark_an_wd'))
    for z in range(4, 46, 5):
        box(v, 56, 72, 86, 89, z, z + 1, K('bark_an_w'))
    #! 屋根（急な切妻。棟は東西。銀青のスレート）。
    for k in range(22):
        y0 = 4 + k
        y1 = 92 - k
        z = 104 + (k * 2)
        box(v, 4, 124, y0, y1, z, z + 3, K('tile_an') if (k % 2) else K('tile_an_l'))
        if (k % 3) == 0:
            box(v, 4, 124, y0, y1, z + 2, z + 3, K('tile_an_d'))
    box(v, 4, 124, 44, 52, 148, 152, K('gold_an'))           # 棟木
    #! 物見（東の端に小さな台。**倉の屋根より高い**）。
    for (a0, a1) in ((100, 104), (120, 124)):                # 物見の 4 本柱
        for (b0, b1) in ((30, 34), (58, 62)):
            box(v, a0, a1, b0, b1, 104, 176, K('bark_an_s'))
    box(v, 100, 124, 30, 62, 176, 180, K('bark_an_wd'))
    for (a0, a1) in ((100, 103), (121, 124)):
        box(v, a0, a1, 30, 62, 180, 192, K('bark_an_sd'))
    for (a0, a1) in ((30, 33), (59, 62)):
        box(v, 100, 124, a0, a1, 180, 192, K('bark_an_sd'))
    for k in range(9):                                       # 四角錐の小屋根
        box(v, 98 + k, 126 - k, 28 + k, 64 - k, 192 + (k * 2), 195 + (k * 2), K('tile_an'))
    #! 干した獣皮と角（南面の壁に掛ける）。**「森番」の印**。
    for (px, w) in ((16, 14), (34, 12), (92, 13)):
        box(v, px, px + w, 84, 86, 62, 90, K('fur_an_deer'))
        box(v, px + 2, px + w - 2, 84, 86, 66, 86, K('fur_an_deerl'))
    for px in (86, 104):
        for k in range(7):
            box(v, px + k, px + k + 2, 84, 86, 92 + (k * 2), 94 + (k * 2), K('bone'))
            box(v, px + 6 - k, px + 8 - k, 84, 86, 92 + (k * 2), 94 + (k * 2), K('bone'))
    #! 鐘（西の軒下に吊る）。
    box(v, 12, 26, 44, 52, 100, 102, K('bark_an_wd'))
    for k in range(7):
        disc(v, 19, 48, 6 - (k * 0.5), 88 + (k * 2), 90 + (k * 2), K('gold_d'))
    for _ in range(int(rng.integers(3, 6))):                 # 積んだ薪と籠（脚の間）
        px = int(rng.integers(20, 100))
        box(v, px, px + 12, 60, 74, 1, 12, K('bark_an_sd'))
        box(v, px + 1, px + 11, 61, 73, 10, 12, K('bark_an_scar'))
    return one(hollow(v, 4))


def moot_ring_ang(name):
    """**集いの環**（`features`。3×3 マス）— 白い立石の環と、中央の炉と丸太の長椅子。

    **上から見て円**であることが遠目の印になる。
    """
    rng = rng_for(name)
    S = 96
    v = np.zeros((S, S, 96), np.int16)
    box(v, 32, 64, 32, 64, 0, 1, K('dirt_an_pd'))
    cx = cy = 48
    #! 立石（9 本。高さと傾きを変える）。
    for k in range(9):
        a = k * 0.698
        px = cx + int(round(np.cos(a) * 34))
        py = cy + int(round(np.sin(a) * 34))
        hgt = int(rng.integers(40, 78))
        w = int(rng.integers(4, 7))
        lean = int(rng.integers(-2, 3))
        for z in range(1, hgt):
            t = z / float(hgt)
            box(v, px - w + int(round(lean * t)), px + w + int(round(lean * t)),
                py - w, py + w, z, z + 1,
                K('marb_an_md') if ((z // 6) % 2) else K('marb_an_m'))
        box(v, px - w, px + w, py - w, py + w, hgt - 2, hgt, K('gold_an_d'))
        box(v, px - w - 1, px + w + 1, py - w - 1, py + w + 1, 1, 4, K('moss_an_d'))
    #! 中央の炉（石の輪と灰と燃え残り）。
    disc(v, cx, cy, 11, 1, 5, K('marb_an_md'))
    disc(v, cx, cy, 8, 1, 4, K('ash'))
    disc(v, cx, cy, 8, 3, 4, K('soot'))
    for k in range(5):
        a = k * 1.26
        box(v, cx + int(round(np.cos(a) * 4)) - 1, cx + int(round(np.cos(a) * 4)) + 2,
            cy + int(round(np.sin(a) * 4)) - 1, cy + int(round(np.sin(a) * 4)) + 2, 4, 9,
            K('char'))
    #! 丸太の長椅子（4 つ。炉を囲む）。
    for a in (0.4, 1.9, 3.5, 5.0):
        px = cx + int(round(np.cos(a) * 20))
        py = cy + int(round(np.sin(a) * 20))
        if abs(np.cos(a)) > abs(np.sin(a)):
            log_y(v, px, 6, 5.0, py - 12, py + 12, K('bark_an_s'))
            box(v, px - 5, px + 5, py - 12, py + 12, 9, 11, K('bark_an_wd'))
        else:
            log_x(v, py, 6, 5.0, px - 12, px + 12, K('bark_an_s'))
            box(v, px - 12, px + 12, py - 5, py + 5, 9, 11, K('bark_an_wd'))
    return one(v)


def forest_altar_ang(name):
    """**森の祭壇**（`features` / 前庭）— 根に抱かれた月の石の卓。夜に淡く光る。"""
    rng = rng_for(name)
    v = vol(h=60)
    box(v, 0, V, 0, V, 0, 1, K('moss_an_d'))
    #! 抱く根（4 方向から巻き上がる）。
    for a in (0.5, 2.1, 3.6, 5.2):
        for t in range(11):
            px = 16 + int(round(np.cos(a) * (13 - t)))
            py = 16 + int(round(np.sin(a) * (13 - t)))
            box(v, px - 2, px + 3, py - 2, py + 3, 1 + t, 4 + t, K('bark_an_sd'))
    #! 卓（月の石の平板）。
    octagon(v, 16, 16, 9, 12, 16, K('marb_an_md'))
    octagon(v, 16, 16, 10, 15, 18, K('marb_an_m'))
    #! 供物（葉と実と、灯した燭）。
    for _ in range(int(rng.integers(3, 6))):
        px, py = int(rng.integers(10, 21)), int(rng.integers(10, 21))
        v[px, py, 18] = pick(rng, [K('leaf_an_l'), K('fl_w'), K('fl_y'), K('fl_r')])
    box(v, 14, 18, 14, 18, 18, 26, K('marb_an_m'))
    box(v, 15, 17, 15, 17, 26, 32, K('fl_an_moon'))
    #! 背後の立石（北。祭壇の背に 1 枚）。
    box(v, 8, 24, 2, 6, 1, 46, K('marb_an_md'))
    box(v, 9, 23, 2, 6, 1, 44, K('marb_an_m'))
    for k in range(5):                                       # 頭を丸める
        box(v, 9 + k, 23 - k, 2, 6, 44 + k, 45 + k, K('marb_an_m'))
    box(v, 13, 19, 5, 6, 20, 36, K('gold_an'))               # 刻んだ葉の印
    box(v, 15, 17, 5, 6, 16, 40, K('gold_an_d'))
    box(v, 8, 24, 2, 6, 1, 5, K('moss_an_d'))
    return one(v)


def forest_altar_flame_ang(name):
    """森の祭壇の燭の火（夜。置く側が自発光をつけて重ねる）。"""
    v = vol(h=40)
    box(v, 14, 18, 14, 18, 26, 34, K('fl_an_core'))
    return one(v)

# ------------------------------------------------------------------ 小物（1 マス）
#
# **2026-09-05 に作り直した。**`patch.py` の終わりの目印を間違えて元の 22 個を消して
# しまい（§9 の罠）、`__pycache__` に残っていた `.pyc` で形と部品の宣言（`laundry_parts`
# の振れ幅・`banner_parts` の支点）を確かめながら書き直した。意匠は裂け谷に合わせて
# **白木と金と白い布**へ寄せてある。
def _barrel(v, cx, cy, r, z0, z1, body, hoop):
    """立てた樽（胴に 2 本のたが）。`arrow_barrel_ang` も呼ぶ。"""
    disc(v, cx, cy, r, z0, z1, body)
    disc(v, cx, cy, r, z0 + 1, z0 + 3, hoop)
    disc(v, cx, cy, r, z1 - 3, z1 - 1, hoop)
    disc(v, cx, cy, r - 1.2, z1 - 1, z1, hoop)


def barrel_ang(name):
    """樽 2 つ（大小）。白木にたがは金。"""
    v = vol(h=24)
    _barrel(v, 11, 13, 6.5, 0, 20, K('bark_an_w'), K('gold_an_d'))
    _barrel(v, 22, 21, 5.0, 0, 15, K('bark_an_wd'), K('gold_an_d'))
    return one(v)


def ale_barrels_ang(name):
    """寝かせた酒樽 2 本（台に載せる）。"""
    v = vol(h=22)
    for (cy, cz, r, x0, x1) in ((10, 9, 6.0, 4, 28), (22, 8, 5.0, 8, 26)):
        log_x(v, cy, cz, r, x0, x1, K('bark_an_w'))
        log_x(v, cy, cz, r, x0 + 2, x0 + 4, K('gold_an_d'))
        log_x(v, cy, cz, r, x1 - 4, x1 - 2, K('gold_an_d'))
        box(v, x0, x1, cy - 2, cy + 2, 0, 3, K('bark_an_wd'))   # 枕木
    box(v, 12, 15, 6, 10, 14, 17, K('steel_an_l'))              # 呑み口
    return one(v)


def crate_ang(name):
    """木箱の積み（3 つ。白木の枠に薄板）。"""
    rng = rng_for(name)
    v = vol(h=26)
    for (x0, y0, z0, w, h) in ((3, 6, 0, 12, 11), (16, 4, 0, 13, 12), (6, 18, 0, 11, 9)):
        box(v, x0, x0 + w, y0, y0 + w, z0, z0 + h, K('bark_an_wd'))
        box(v, x0 + 1, x0 + w - 1, y0 + 1, y0 + w - 1, z0, z0 + h, K('bark_an_w'))
        for (a0, a1, b0, b1) in ((x0, x0 + 2, y0, y0 + w), (x0 + w - 2, x0 + w, y0, y0 + w),
                                 (x0, x0 + w, y0, y0 + 2), (x0, x0 + w, y0 + w - 2, y0 + w)):
            box(v, a0, a1, b0, b1, z0, z0 + h, K('bark_an_wd'))
        box(v, x0, x0 + w, y0, y0 + w, z0 + h - 2, z0 + h, K('bark_an_wd'))
    box(v, 17, 28, 5, 16, 12, 20, K('bark_an_wd'))              # 上に載せた小箱
    box(v, 18, 27, 6, 15, 12, 20, K('bark_an_w'))
    if rng.random() < 0.5:
        box(v, 19, 26, 7, 14, 19, 20, K('gold_an_d'))
    return one(v)


def basket_ang(name):
    """編み籠 3 つ（口の開いたもの 2 つと、蓋つき 1 つ）。"""
    v = vol(h=20)
    for (cx, cy, r, h) in ((9, 10, 5.5, 12), (21, 8, 4.5, 10), (16, 22, 6.0, 13)):
        disc(v, cx, cy, r, 0, h, K('bark_an_wi'))
        disc(v, cx, cy, r - 1.5, 2, h, K('dark'))
        for z in range(1, h, 3):                                # 編み目（1 ボクセルの筋）
            disc(v, cx, cy, r, z, z + 1, K('bark_an_wid'))
        disc(v, cx, cy, r, h - 1, h, K('bark_an_wid'))
    disc(v, 16, 22, 6.0, 12, 14, K('bark_an_w'))                # 蓋
    return one(v)


def sack_ang(name):
    """麻袋 3 つ（口を縛ったもの）。"""
    rng = rng_for(name)
    v = vol(h=22)
    for (cx, cy, r, h) in ((10, 11, 6.0, 14), (21, 9, 5.0, 12), (17, 22, 5.5, 13)):
        for z in range(h):
            t = z / float(h)
            disc(v, cx, cy, r * (1.0 - (0.35 * t * t)), z, z + 1, K('cloth_an_wd'))
        disc(v, cx, cy, r * 0.62, 2, h - 2, K('cloth_an_w'))
        box(v, cx - 2, cx + 2, cy - 2, cy + 2, h, h + 3, K('cloth_an_wd'))   # 縛った口
        box(v, cx - 1, cx + 1, cy - 3, cy + 3, h - 2, h, K('gold_an_d'))     # 紐
    if rng.random() < 0.4:
        box(v, 4, 10, 24, 30, 0, 4, K('leaf_an_l'))             # こぼれた麦
    return one(v)


def bench_ang(name):
    """白石の長椅子（脚は 2 つ、背は無い）。"""
    v = vol(h=16)
    box(v, 3, 29, 11, 21, 10, 14, K('marb_an_m'))
    box(v, 3, 29, 11, 21, 13, 14, K('marb_an_md'))
    for cx in (5, 23):
        box(v, cx, cx + 4, 12, 20, 0, 11, K('marb_an_md'))
        box(v, cx - 1, cx + 5, 11, 21, 0, 2, K('marb_an_s'))
    box(v, 3, 29, 11, 12, 10, 11, K('gold_an_d'))               # 縁の線
    return one(v)


def table_bench_ang(name):
    """外の卓と長椅子（白木の卓に、両側の腰掛け）。"""
    rng = rng_for(name)
    v = vol(h=22)
    box(v, 4, 28, 10, 22, 15, 19, K('bark_an_w'))               # 天板
    box(v, 4, 28, 10, 22, 18, 19, K('bark_an_wl'))
    for cx in (6, 24):
        box(v, cx, cx + 3, 11, 21, 0, 15, K('bark_an_wd'))
    for cy in (5, 24):
        box(v, 5, 27, cy, cy + 4, 8, 11, K('bark_an_w'))        # 腰掛け
        for cx in (7, 23):
            box(v, cx, cx + 2, cy, cy + 4, 0, 8, K('bark_an_wd'))
    box(v, 12, 20, 13, 19, 19, 21, K('cloth_an_w'))             # 卓の上の布
    if rng.random() < 0.6:
        disc(v, 14, 16, 2.0, 19, 23, K('gold_an'))              # 金の杯
    return one(v)


def woodpile_ang(name):
    """薪の山（割った丸太を交互に積む）。"""
    rng = rng_for(name)
    v = vol(h=26)
    for k, z in enumerate(range(0, 22, 4)):
        shade = pick(rng, [K('bark_an_s'), K('bark_an_sd'), K('bark_an_scar')], [0.44, 0.34, 0.22])
        if (k % 2) == 0:
            for cy in range(7, 26, 5):
                log_x(v, cy, z + 2, 2.4, 4, 28, shade)
        else:
            for cx in range(7, 26, 5):
                log_y(v, cx, z + 2, 2.4, 4, 28, shade)
    box(v, 4, 28, 4, 28, 0, 1, K('bark_an_scar'))
    return one(v)


def well_ang(name):
    """井戸（白石の井筒に、白木の屋根と滑車と桶）。"""
    v = vol(h=44)
    octagon(v, 16, 16, 10, 0, 12, K('marb_an_m'))
    octagon(v, 16, 16, 10, 10, 12, K('marb_an_md'))
    octagon(v, 16, 16, 7, 2, 12, K('dark'))
    octagon(v, 16, 16, 7, 2, 5, K('wa_an_c'))                   # 水面
    octagon(v, 16, 16, 6, 4, 5, K('wa_an_cl'))
    for cx in (6, 24):                                          # 2 本の柱
        box(v, cx, cx + 3, 15, 18, 12, 32, K('bark_an_w'))
    log_x(v, 16, 32, 2.0, 5, 27, K('bark_an_wd'))               # 桁と滑車
    disc(v, 16, 16, 3.0, 30, 34, K('gold_an_d'))
    box(v, 15, 18, 15, 18, 22, 30, K('bark_an_wi'))             # 綱
    _barrel(v, 16, 16, 4.0, 16, 23, K('bark_an_w'), K('gold_an_d'))
    for k in range(6):                                          # 小さな切妻屋根
        box(v, 3, 29, 8 + k, 25 - k, 32 + (k * 2), 35 + (k * 2), K('tile_an'))
    box(v, 3, 29, 15, 18, 42, 44, K('gold_an'))
    return one(v)


def rain_barrel_ang(name):
    """雨水の桶（水が張ってある）と、脇の柄杓。"""
    v = vol(h=24)
    _barrel(v, 14, 15, 8.0, 0, 19, K('bark_an_w'), K('gold_an_d'))
    disc(v, 14, 15, 6.6, 15, 17, K('wa_an_c'))
    disc(v, 14, 15, 5.4, 16, 17, K('wa_an_cl'))
    box(v, 24, 26, 14, 16, 0, 20, K('bark_an_wd'))              # 柄杓の柄
    disc(v, 25, 15, 3.0, 20, 23, K('bark_an_w'))
    return one(v)


def cart_ang(name):
    """二輪の荷車（白木の荷台に籠と麻袋）。"""
    rng = rng_for(name)
    v = vol(h=26)
    box(v, 3, 29, 9, 23, 9, 13, K('bark_an_w'))                 # 荷台
    box(v, 3, 29, 9, 23, 12, 13, K('bark_an_wl'))
    for (a0, a1, b0, b1) in ((3, 29, 9, 11), (3, 29, 21, 23), (3, 5, 9, 23), (27, 29, 9, 23)):
        box(v, a0, a1, b0, b1, 13, 19, K('bark_an_wd'))         # あおり
    for cy in (6, 25):                                          # 車輪
        for x in range(32):
            for z in range(26):
                d2 = ((x - 14) ** 2) + ((z - 8) ** 2)
                if 36 <= d2 <= 64:
                    box(v, x, x + 1, cy, cy + 2, z, z + 1, K('bark_an_wd'))
        box(v, 13, 16, cy, cy + 2, 7, 10, K('gold_an_d'))       # こしき
        for a in (0.0, 1.05, 2.1, 3.15, 4.2, 5.25):             # 輻
            for t in range(7):
                px = 14 + int(round(np.cos(a) * t))
                pz = 8 + int(round(np.sin(a) * t))
                box(v, px, px + 1, cy, cy + 2, pz, pz + 1, K('bark_an_w'))
    box(v, 26, 32, 14, 18, 13, 16, K('bark_an_wd'))             # 梶棒
    box(v, 8, 18, 12, 20, 19, 24, K('bark_an_wi'))              # 積んだ籠
    if rng.random() < 0.6:
        box(v, 19, 26, 13, 20, 19, 23, K('cloth_an_wd'))        # 麻袋
    return one(v)


def drying_rack_ang(name):
    """干し棚（薬草と茸を吊るす。白木の枠）。"""
    rng = rng_for(name)
    v = vol(h=34)
    for cx in (5, 26):
        box(v, cx, cx + 3, 13, 16, 0, 30, K('bark_an_w'))
        box(v, cx - 1, cx + 4, 12, 17, 0, 2, K('bark_an_wd'))
    for z in (16, 23, 29):
        log_x(v, 14, z, 1.6, 5, 29, K('bark_an_wd'))
    for _ in range(int(rng.integers(8, 14))):                   # 吊るした束
        px = int(rng.integers(6, 26))
        z0 = int(rng.choice([16, 23, 29]))
        ln = int(rng.integers(4, 9))
        shade = pick(rng, [K('leaf_an_g'), K('leaf_an_m'), K('leaf_an_f'), K('fur_an_deer')],
                     [0.34, 0.28, 0.22, 0.16])
        box(v, px, px + 3, 13, 16, z0 - ln, z0, shade)
    return one(v)


def hitching_post_ang(name):
    """馬つなぎ（白石の 2 本の杭に横木。金の環）。"""
    v = vol(h=26)
    for cx in (6, 25):
        octagon(v, cx, 16, 2.4, 0, 20, K('marb_an_m'))
        octagon(v, cx, 16, 3.4, 0, 3, K('marb_an_s'))
        octagon(v, cx, 16, 2.8, 18, 21, K('gold_an'))
    log_x(v, 16, 17, 2.0, 5, 27, K('bark_an_wd'))
    for px in (11, 20):                                         # 金の環
        for t in range(10):
            a = t * 0.628
            box(v, px + int(round(np.cos(a) * 2)), px + int(round(np.cos(a) * 2)) + 1,
                15, 17, 12 + int(round(np.sin(a) * 2)), 13 + int(round(np.sin(a) * 2)),
                K('gold_an_d'))
    return one(v)


def laundry_ang(name):
    """洗濯物（白い布が風にゆっくり揺れる）。`laundry_parts` が振り子で振る。"""
    rng = rng_for(name)
    frame = vol(h=36)
    sheet = vol(h=36)
    for cx in (4, 27):
        box(frame, cx, cx + 3, 15, 18, 0, 30, K('bark_an_w'))
        box(frame, cx - 1, cx + 4, 14, 19, 0, 2, K('bark_an_wd'))
    log_x(frame, 16, 29, 1.4, 4, 30, K('bark_an_wi'))
    for _ in range(int(rng.integers(3, 6))):                    # 干した布
        px = int(rng.integers(5, 24))
        w = int(rng.integers(4, 8))
        ln = int(rng.integers(8, 17))
        shade = pick(rng, [K('cloth_an_w'), K('cloth_an_wd'), K('cloth_an_g'), K('cloth_an_y')],
                     [0.44, 0.26, 0.18, 0.12])
        box(sheet, px, px + w, 15, 17, 28 - ln, 29, shade)
        box(sheet, px, px + w, 15, 17, 28 - ln, 29 - ln, K('gold_an_d'))
    return [('frame', frame, (0, 0, 0)), ('sheet', sheet, (0, 0, 0))]


def laundry_parts():
    return swing_parts([('sheet', (16.0, 16.0, 29.0))], amplitude=5.0, period=3.0)


def banner_ang(name):
    """**エルフの幟**（風でなびく）— 白木の竿に、白と青の長い旗。金の縁取り。"""
    rng = rng_for(name)
    frame = vol(h=54)
    cloth = vol(h=54)
    octagon(frame, 16, 16, 2.2, 0, 46, K('bark_an_wl'))
    octagon(frame, 16, 16, 3.6, 0, 3, K('marb_an_s'))
    box(frame, 14, 19, 14, 19, 44, 47, K('gold_an'))            # 竿の頭
    box(frame, 15, 18, 15, 18, 47, 52, K('gold_an_l'))
    box(frame, 17, 26, 15, 17, 42, 44, K('bark_an_wd'))         # 横木
    body = K('cloth_an_w') if rng.random() < 0.55 else K('cloth_an_g')
    box(cloth, 18, 26, 15, 17, 16, 43, body)
    box(cloth, 18, 26, 15, 17, 41, 43, K('gold_an'))            # 上端の金
    box(cloth, 18, 26, 15, 17, 16, 18, K('gold_an_d'))          # 下端の房
    box(cloth, 20, 24, 15, 17, 26, 36, K('gold_an'))            # 紋
    box(cloth, 21, 23, 15, 17, 24, 38, K('gold_an_l'))
    return [('frame', frame, (0, 0, 0)), ('cloth', cloth, (0, 0, 0))]


def banner_parts():
    return [
        {'name': 'frame', 'voxels': 'frame', 'grounded': True,
         'motion': {'kind': 'static'}, 'wind_k': 0.0},
        {'name': 'cloth', 'voxels': 'cloth', 'parent': 'frame', 'grounded': False,
         'pivot': [17.0, 16.0, 33.0],
         'motion': {'kind': 'wind'}, 'wind_k': 1.4},
    ]


def vine_ang(name):
    """壁を這う蔓（淡い緑の葉と、ところどころ白い花）。"""
    rng = rng_for(name)
    v = vol(h=40)
    for _ in range(int(rng.integers(3, 6))):
        x = int(rng.integers(4, 28))
        tall = int(rng.integers(20, 38))
        for z in range(tall):
            xx = min(V - 1, max(0, x + int(round(2.5 * np.sin(z * 0.22)))))
            v[xx, 29, z] = K('leaf_an_gd')
            v[xx, 30, z] = K('leaf_an_g')
            if (z % 5) == 2:
                v[min(V - 1, xx + 1), 30, z] = K('leaf_an_g')
                v[max(0, xx - 1), 30, z] = K('leaf_an_gd')
            if (z % 11) == 4:
                v[xx, 31, z] = K('fl_w')
    return one(v)


def lantern_post_ang(name):
    """庭の吊り灯（前庭に立てる小さな角灯。**火は別体**）。"""
    v = vol(h=36)
    octagon(v, 16, 16, 2.2, 0, 20, K('marb_an_m'))
    octagon(v, 16, 16, 3.6, 0, 3, K('marb_an_s'))
    box(v, 12, 21, 12, 21, 19, 21, K('gold_an_d'))
    for (x0, x1) in ((12, 14), (19, 21)):
        for (y0, y1) in ((12, 14), (19, 21)):
            box(v, x0, x1, y0, y1, 21, 30, K('gold_an'))
    box(v, 12, 21, 12, 21, 30, 32, K('gold_an_l'))
    box(v, 15, 18, 15, 18, 32, 35, K('gold_an_l'))
    return one(v)


def lantern_flame_ang(name):
    """庭の吊り灯の火（`lantern_post_ang` と同じマスへ。1 枚が振り子で揺れる）。"""
    a = np.zeros((6, 6, 9), np.int16)
    a[1:5, 1:5, 0:7] = K('fl_an_pale')
    a[2:4, 2:4, 3:9] = K('fl_an_core')
    return [('flame_a', a, (13, 13, 22))]


def lantern_flame_parts():
    return [
        {'name': 'flame_a', 'voxels': 'flame_a', 'grounded': False,
         'pivot': [16.0, 16.0, 22.0],
         'motion': {'kind': 'pendulum', 'axis': 'x', 'amplitude': 6.0, 'period': 1.1,
                    'phase': 0.0, 'damping': 0.0},
         'wind_k': 0.0},
    ]


# ---- 林床の小物（`turf_props` と区画の `props`）----
def fern_ang(name):
    """羊歯の茂み（林床でいちばん多い小物。株より背が高い）。"""
    rng = rng_for(name)
    v = vol(h=26)
    for _ in range(int(rng.integers(7, 12))):
        bx, by = int(rng.integers(7, V - 7)), int(rng.integers(7, V - 7))
        tall = int(rng.integers(10, 22))
        angle = rng.random() * 6.283
        lean = rng.uniform(4.0, 8.0)
        blade = pick(rng, [K('stem_an_f'), K('stem_an_fl'), K('leaf_an_m')], [0.4, 0.36, 0.24])
        for z in range(tall):
            t = z / float(tall)
            x = min(V - 1, max(0, bx + int(round(np.cos(angle) * lean * t))))
            y = min(V - 1, max(0, by + int(round(np.sin(angle) * lean * t))))
            v[x, y, z] = blade
            if ((z % 3) == 1) and (z > 3):
                v[min(V - 1, x + 1), y, z] = blade
                v[max(0, x - 1), y, z] = blade
                v[x, min(V - 1, y + 1), z] = blade
    return one(v)


def mushrooms_ang(name):
    """茸の群れ（倒木や根方に。傘は塊で 1 色）。"""
    rng = rng_for(name)
    v = vol(h=16)
    cap = pick(rng, [K('cor'), K('cream'), K('fl_y'), K('clay')])
    cap_d = pick(rng, [K('cor_d'), K('cream_d'), K('wilt'), K('clay_d')])
    for _ in range(int(rng.integers(5, 9))):
        x, y = int(rng.integers(5, V - 6)), int(rng.integers(5, V - 6))
        h = int(rng.integers(3, 9))
        r = int(rng.integers(2, 4))
        box(v, x, x + 2, y, y + 2, 0, h, K('cream'))
        box(v, x - r, x + r + 2, y - r, y + r + 2, h, h + 2, cap_d)
        box(v, x - r + 1, x + r + 1, y - r + 1, y + r + 1, h + 1, h + 3, cap)
    return one(v)


def fallen_log_ang(name):
    """倒木（苔と茸に覆われ、片端が折れている）。"""
    rng = rng_for(name)
    v = vol(h=16)
    log_x(v, 17, 6, 6.0, 1, 30, K('bark_an_sd'))
    log_x(v, 17, 6, 4.4, 1, 30, K('bark_an_s'))
    box(v, 1, 3, 11, 24, 0, 12, K('bark_an_scar'))
    for _ in range(int(rng.integers(4, 8))):
        x = int(rng.integers(3, 27))
        blk = v[x:x + 5, 11:24, 9:13]
        blk[blk != 0] = K('moss_an') if rng.random() < 0.6 else K('moss_an_l')
    for _ in range(int(rng.integers(2, 5))):
        x = int(rng.integers(4, 26))
        y = int(rng.integers(11, 22))
        box(v, x, x + 2, y, y + 2, 11, 13, K('cream'))
        box(v, x - 1, x + 3, y - 1, y + 3, 13, 14, K('cor_d'))
    box(v, 0, V, 9, 26, 0, 1, K('moss_an_d'))
    return one(v)


def boulder_ang(name):
    """苔むした岩（森の中の露岩。塊を重ねる）。"""
    rng = rng_for(name)
    v = vol(h=20)
    for _ in range(int(rng.integers(4, 7))):
        cx, cy = int(rng.integers(8, 24)), int(rng.integers(8, 24))
        w, d, h = (int(rng.integers(4, 10)) for _ in range(3))
        box(v, cx - w // 2, cx + w // 2, cy - d // 2, cy + d // 2, 0, h,
            pick(rng, [K('rock'), K('rock_l'), K('rock_d')], [0.44, 0.26, 0.30]))
    #! 天面と北面に苔（陽の当たらない側）。
    top = v.max(axis=2)
    for x in range(V):
        for y in range(V):
            if top[x, y] == 0:
                continue
            col = v[x, y, :]
            hz = int(np.max(np.nonzero(col)))
            #! **苔は 2 割だけ**（半分に載せた版は岩が緑の塊に見えた）。
            if rng.random() < 0.20:
                col[hz] = K('moss_an') if rng.random() < 0.6 else K('moss_an_d')
    return one(v)


def stump_ang(name):
    """切り株（年輪と斧の跡。周りに削りかすと若芽）。"""
    rng = rng_for(name)
    v = vol(h=16)
    h = int(rng.integers(7, 12))
    disc(v, 15, 16, 8.0, 0, h, K('bark_an_sd'))
    disc(v, 15, 16, 6.4, 0, h, K('bark_an_s'))
    for k in range(4):                                       # 年輪
        disc(v, 15, 16, 6.4 - (k * 1.6), h - 1, h, K('bark_an_w') if (k % 2) else K('bark_an_wd'))
    box(v, 12, 19, 8, 12, h - 3, h, K('bark_an_scar'))       # 斧の跡
    for _ in range(int(rng.integers(3, 7))):                 # 削りかす
        x, y = int(rng.integers(2, 29)), int(rng.integers(2, 29))
        v[x, y, 0] = K('bark_an_w')
    for k in range(int(rng.integers(1, 4))):                 # 根方の若芽
        x = 6 + (k * 8)
        box(v, x, x + 2, 24, 26, 0, 5, K('stem_an_f'))
        box(v, x - 1, x + 3, 23, 27, 5, 7, K('leaf_an_l'))
    return one(v)


def sapling_ang(name):
    """若木（膝丈の細い苗。周りを枝で囲って守る）。"""
    v = vol(h=34)
    disc(v, 16, 16, 1.2, 0, 24, K('bark_an_sl'))
    for k in range(3):
        z = 14 + (k * 5)
        box(v, 12 - k, 21 + k, 12 - k, 21 + k, z, z + 4, K('leaf_an_m'))
        box(v, 13 - k, 20 + k, 13 - k, 20 + k, z + 2, z + 5, K('leaf_an_l'))
    for k in range(5):                                       # 守りの枝の囲い
        a = k * 1.26
        px = 16 + int(round(np.cos(a) * 9))
        py = 16 + int(round(np.sin(a) * 9))
        box(v, px, px + 2, py, py + 2, 0, 11, K('bark_an_wi'))
    return one(v)


def flowers_ang(name):
    """林の花（日の差す所に群れる。色は 1 群 1 色）。"""
    rng = rng_for(name)
    v = vol(h=14)
    petal = pick(rng, [K('fl_w'), K('fl_y'), K('alp_fw'), K('cor_l')])
    for _ in range(int(rng.integers(9, 16))):
        x, y = int(rng.integers(3, V - 3)), int(rng.integers(3, V - 3))
        h = int(rng.integers(4, 9))
        box(v, x, x + 1, y, y + 1, 0, h, K('stem_an_f'))
        box(v, x - 1, x + 2, y - 1, y + 2, h, h + 2, petal)
    return one(v)


def bramble_ang(name):
    """茨の茂み（黒い実つき。踏み込めない所の印）。"""
    rng = rng_for(name)
    v = vol(h=22)
    for _ in range(int(rng.integers(8, 13))):
        cx, cy = int(rng.integers(5, 27)), int(rng.integers(5, 27))
        w, d = int(rng.integers(4, 9)), int(rng.integers(4, 9))
        h = int(rng.integers(6, 17))
        box(v, cx - w // 2, cx + w // 2, cy - d // 2, cy + d // 2, max(0, h - 5), h,
            pick(rng, [K('leaf_an_d'), K('leaf_an_m'), K('bark_an_scar')], [0.44, 0.36, 0.20]))
    for _ in range(int(rng.integers(4, 9))):
        x, y = int(rng.integers(4, 28)), int(rng.integers(4, 28))
        z = int(rng.integers(8, 20))
        if v[x, y, z] != 0:
            v[x, y, z] = K('ink')
    return one(v)


def deer_ang(name):
    """鹿（林床に立つ。首を下げて草を食む）。"""
    v = vol(h=30)
    box(v, 9, 23, 13, 20, 14, 21, K('fur_an_deer'))          # 胴
    box(v, 9, 23, 13, 20, 19, 21, K('fur_an_deerl'))
    for (x, y) in ((10, 13), (10, 18), (20, 13), (20, 18)):  # 脚
        box(v, x, x + 2, y, y + 2, 0, 15, K('fur_an_deer'))
    box(v, 6, 11, 14, 19, 16, 26, K('fur_an_deer'))          # 首
    box(v, 2, 8, 14, 19, 22, 27, K('fur_an_deerl'))          # 頭
    box(v, 1, 3, 15, 18, 23, 25, K('bark_an_scar'))          # 鼻面
    for (x, dz) in ((4, 0), (6, 1)):                         # 角
        box(v, x, x + 1, 14, 15, 27, 30 + dz, K('bone'))
        box(v, x, x + 1, 18, 19, 27, 30 + dz, K('bone'))
    box(v, 22, 25, 15, 18, 18, 22, K('fur_an_deerl'))        # 尾
    return one(v)


def rabbit_ang(name):
    """兎（耳を立てて坐る）。"""
    v = vol(h=16)
    ball(v, 16, 17, 6, 5.0, K('fur_an_owl'), squash=1.1)
    ball(v, 14, 12, 9, 3.4, K('fur_an_owl'))
    box(v, 13, 14, 10, 12, 11, 16, K('fur_an_owl'))          # 耳
    box(v, 15, 16, 10, 12, 11, 16, K('fur_an_owl'))
    box(v, 13, 17, 9, 11, 9, 11, K('cream'))                 # 顔
    ball(v, 20, 19, 5, 2.6, K('cream'))                      # 尾
    return one(v)


def owl_perch_ang(name):
    """梟の止まり木（枝を渡した柱に、梟が 1 羽）。"""
    v = vol(h=40)
    disc(v, 16, 16, 2.4, 0, 30, K('bark_an_s'))
    disc(v, 16, 16, 3.6, 0, 3, K('rub_d'))
    log_x(v, 16, 30, 1.8, 5, 27, K('bark_an_sd'))
    ball(v, 10, 16, 35, 4.2, K('fur_an_owl'), squash=1.3)
    ball(v, 10, 16, 38, 3.0, K('fur_an_owl'))
    box(v, 8, 12, 13, 15, 37, 39, K('gold_d'))               # 目
    box(v, 8, 12, 17, 19, 37, 39, K('gold_d'))
    box(v, 8, 10, 15, 17, 36, 38, K('bark_an_scar'))         # 嘴
    box(v, 7, 9, 14, 18, 39, 41, K('fur_an_owl'))            # 羽角
    return one(v)


def beehive_ang(name):
    """蜂の巣箱（藁を巻いた円い巣と、飛ぶ蜂）。"""
    v = vol(h=26)
    box(v, 5, 27, 6, 26, 0, 3, K('bark_an_wd'))
    for k in range(6):
        disc(v, 16, 16, 9.5 - (k * 1.2), 3 + (k * 3), 6 + (k * 3), K('straw'))
        disc(v, 16, 16, 9.5 - (k * 1.2), 5 + (k * 3), 6 + (k * 3), K('straw_d'))
    box(v, 14, 18, 24, 26, 4, 7, K('dark'))                  # 出入りの口
    for (px, py, pz) in ((7, 22, 14), (24, 8, 18), (10, 6, 20)):
        v[px, py, pz] = K('fl_y')
    return one(v)


def garden_plot_ang(name):
    """菜園（編み枝で囲った小さな畝。豆の支柱つき）。"""
    rng = rng_for(name)
    v = vol(h=26)
    box(v, 3, 29, 4, 28, 0, 4, K('dirt_an_pd'))
    for x in range(4, 28, 4):
        box(v, x, x + 3, 5, 27, 3, 6, K('dirt_an_p'))
        box(v, x, x + 3, 5, 27, 5, 6, K('dirt_an_pl'))
        for y in range(6, 26, 4):
            box(v, x, x + 2, y, y + 2, 6, 8, K('stem_an_f'))
            box(v, x - 1, x + 3, y - 1, y + 3, 8, 10, K('stem_an_fl'))
    for (a0, a1) in ((3, 5), (27, 29)):                      # 編み枝の囲い
        box(v, 3, 29, a0, a1, 0, 9, K('bark_an_wi'))
        box(v, a0, a1, 3, 29, 0, 9, K('bark_an_wi'))
    for k in range(3):                                       # 豆の支柱（三脚）
        x = 8 + (k * 8)
        for (dx, dy) in ((0, 0), (3, 3), (-3, 3)):
            box(v, x + dx, x + dx + 2, 12 + dy, 14 + dy, 4, 22, K('bark_an_sd'))
        #! **葉は支柱に沿った細い塊**（9x7 の箱で巻いた版は緑の板になった）。
        for (dx, dy) in ((0, 0), (3, 3), (-3, 3)):
            for z in range(6, 22, 3):
                box(v, x + dx - 1, x + dx + 3, 12 + dy - 1, 12 + dy + 3, z, z + 2, K('leaf_an_m'))
                box(v, x + dx, x + dx + 2, 12 + dy, 12 + dy + 2, z + 1, z + 3, K('leaf_an_l'))
    _ = rng
    return one(v)


# ---- 店ごとの小物 ----
def goods_stall_ang(name):
    """雑貨の売り台（布の日除けと、並べた壺と籠）。"""
    rng = rng_for(name)
    v = vol(h=38)
    for x in (4, 26):
        box(v, x, x + 3, 6, 9, 0, 30, K('bark_an_s'))
        box(v, x, x + 3, 23, 26, 0, 30, K('bark_an_s'))
    box(v, 3, 29, 5, 27, 30, 33, K('cloth_an_g'))            # 日除け
    box(v, 3, 29, 5, 27, 32, 33, K('cloth_an_y'))
    box(v, 4, 28, 10, 24, 12, 15, K('bark_an_wd'))           # 台
    box(v, 4, 28, 10, 24, 14, 15, K('bark_an_w'))
    for px in range(6, 26, 5):
        h = int(rng.integers(3, 7))
        disc(v, px + 2, 14, 2.4, 15, 15 + h, pick(rng, [K('clay'), K('clay_d'), K('bark_an_wi')]))
        disc(v, px + 2, 20, 2.0, 15, 15 + h - 1, pick(rng, [K('bark_an_wi'), K('clay')]))
    return one(v)


def armour_stand_ang(name):
    """革と木の鎧を掛けた立て台（エルフの軽い胴鎧）。"""
    v = vol(h=36)
    box(v, 14, 19, 14, 19, 0, 4, K('bark_an_wd'))
    box(v, 15, 18, 15, 18, 3, 24, K('bark_an_sd'))
    box(v, 8, 25, 12, 21, 22, 32, K('lair_dd'))              # 胴（濃い革）
    box(v, 9, 24, 20, 21, 24, 31, K('steel_an_l'))           # 胸当て（鋼。南を向く）
    box(v, 11, 22, 20, 21, 26, 29, K('leaf_an_m'))           # 胸の葉の紋
    box(v, 10, 23, 12, 21, 30, 32, K('bark_an_w'))           # 肩の板
    box(v, 6, 10, 14, 19, 24, 30, K('lair'))                 # 袖
    box(v, 23, 27, 14, 19, 24, 30, K('lair'))
    box(v, 13, 20, 13, 20, 32, 36, K('steel_an_l'))          # 兜
    return one(v)


def shield_tree_ang(name):
    """盾を掛けた枯木（エルフの木の盾。円く、葉の紋）。"""
    rng = rng_for(name)
    v = vol(h=40)
    disc(v, 16, 16, 3.0, 0, 34, K('bark_an_sd'))
    disc(v, 16, 16, 4.2, 0, 3, K('rub_d'))
    for k in range(4):
        a = k * 1.57
        px = 16 + int(round(np.cos(a) * 5))
        py = 16 + int(round(np.sin(a) * 5))
        box(v, px - 1, px + 2, py - 1, py + 2, 20 + (k * 3), 30 + (k * 2), K('bark_an_s'))
    for (px, py, z, r) in ((7, 16, 20, 6.0), (25, 16, 26, 5.4), (16, 25, 14, 5.0)):
        disc(v, px, py, r, z, z + 3, K('bark_an_wd'))
        disc(v, px, py, r - 1.5, z, z + 3, K('bark_an_w'))
        disc(v, px, py, 1.8, z, z + 4, K('steel_an_l'))
        box(v, px - 1, px + 2, py - int(r), py + int(r), z + 2, z + 3, K('leaf_an_m'))
    _ = rng
    return one(v)


def forge_ang(name):
    """森の鍛冶床（石を組んだ炉と、樺の炭俵。火は `forge_fire_ang`）。"""
    v = vol(h=34)
    box(v, 4, 26, 6, 26, 0, 12, K('rub'))
    box(v, 5, 25, 7, 25, 10, 12, K('rub_d'))
    box(v, 8, 22, 10, 22, 10, 14, K('char'))
    box(v, 9, 21, 11, 21, 12, 14, K('emb'))
    for x in (4, 24):                                        # 煙出しの柱
        box(v, x, x + 3, 8, 11, 12, 30, K('bark_an_s'))
    box(v, 3, 27, 6, 12, 30, 33, K('bark_an_sd'))            # 煙返しの板
    box(v, 26, 30, 18, 28, 0, 9, K('char'))                  # 炭俵
    box(v, 26, 30, 18, 28, 8, 9, K('soot'))
    box(v, 2, 10, 26, 31, 0, 6, K('bark_an_wd'))             # 鞴の箱
    box(v, 3, 9, 26, 31, 5, 6, K('lair'))
    return one(v)


def forge_fire_ang(name):
    """鍛冶床の火（**昼も光る**。置く側が自発光をつけて重ねる）。"""
    v = vol(h=24)
    box(v, 10, 20, 12, 20, 12, 18, K('fl_mid'))
    box(v, 12, 18, 14, 18, 14, 21, K('fl_core'))
    return one(v)


def anvil_ang(name):
    """金床（樫の切り株の台に据える。槌と鉄片）。"""
    v = vol(h=24)
    disc(v, 13, 16, 8.0, 0, 10, K('bark_an_sd'))
    disc(v, 13, 16, 6.6, 0, 10, K('bark_an_s'))
    box(v, 6, 21, 12, 21, 10, 13, K('iron'))
    box(v, 8, 18, 14, 19, 13, 16, K('iron'))
    box(v, 6, 22, 13, 20, 16, 19, K('iron_l'))
    box(v, 22, 27, 15, 18, 16, 19, K('iron'))                # 角
    box(v, 24, 30, 22, 26, 0, 4, K('bark_an_wd'))            # 槌
    box(v, 26, 30, 22, 26, 3, 7, K('iron'))
    return one(v)


def weapon_rack_ang(name):
    """武器立て（長弓と槍を並べて掛ける）。"""
    rng = rng_for(name)
    v = vol(h=38)
    box(v, 3, 29, 22, 26, 0, 3, K('bark_an_wd'))
    for x in (4, 27):
        box(v, x, x + 2, 22, 26, 0, 30, K('bark_an_sd'))
    box(v, 3, 29, 22, 26, 28, 30, K('bark_an_sd'))
    for px in range(6, 27, 4):
        kind = int(rng.integers(0, 3))
        if kind == 0:                                        # 槍
            box(v, px, px + 2, 23, 25, 2, 32, K('bark_an_s'))
            box(v, px, px + 2, 23, 25, 30, 36, K('steel_an_l'))
        elif kind == 1:                                      # 長弓
            for k in range(14):
                z = 4 + (k * 2)
                dx = int(round(2.5 * np.sin(k / 13.0 * 3.14)))
                box(v, px + dx, px + dx + 2, 23, 25, z, z + 2, K('bark_an_wd'))
            box(v, px, px + 1, 23, 25, 4, 32, K('bark_an_wi'))
        else:                                                # 剣
            box(v, px, px + 2, 23, 25, 4, 24, K('steel_an_l'))
            box(v, px - 2, px + 4, 23, 25, 24, 26, K('gold_d'))
            box(v, px, px + 2, 23, 25, 26, 31, K('lair'))
    return one(v)


def bow_rack_ang(name):
    """弓と矢筒の棚（アーチャーの酒場と武器屋）。"""
    rng = rng_for(name)
    v = vol(h=32)
    box(v, 3, 29, 21, 27, 0, 4, K('bark_an_wd'))
    box(v, 3, 29, 21, 23, 4, 26, K('bark_an_sd'))
    for px in range(5, 27, 5):
        for k in range(11):
            z = 5 + (k * 2)
            dx = int(round(2.0 * np.sin(k / 10.0 * 3.14)))
            box(v, px + dx, px + dx + 2, 23, 25, z, z + 2, K('bark_an_wd'))
    for px in (6, 20):                                       # 矢筒
        disc(v, px + 2, 25, 3.0, 4, 16, K('lair'))
        for k in range(4):
            box(v, px + k, px + k + 1, 24, 26, 16, 22 + k, K('bark_an_wi'))
            box(v, px + k, px + k + 1, 24, 26, 21 + k, 23 + k, K('cloth_an_w'))
    _ = rng
    return one(v)


def arrow_barrel_ang(name):
    """矢の束を挿した樽。"""
    rng = rng_for(name)
    v = vol(h=32)
    _barrel(v, 15, 16, 7.0, 0, 16, K('bark_an_wd'), K('bark_an_wi'))
    for _ in range(int(rng.integers(9, 15))):
        x = 15 + int(rng.integers(-4, 5))
        y = 16 + int(rng.integers(-4, 5))
        h = int(rng.integers(20, 30))
        box(v, x, x + 1, y, y + 1, 14, h, K('bark_an_wi'))
        box(v, x, x + 1, y, y + 1, h - 3, h, K('cloth_an_w'))
    return one(v)


def butts_ang(name):
    """弓の的（藁を巻いた円盤と、刺さった矢）。"""
    rng = rng_for(name)
    v = vol(h=34)
    for x in (8, 21):
        box(v, x, x + 3, 20, 23, 0, 22, K('bark_an_sd'))
    #! 的は南を向いた面（y 21..24）。円は x-z 面で描く。
    for x in range(V):
        for z in range(16, 34):
            d = ((x - 16) ** 2 + (z - 26) ** 2) ** 0.5
            if d > 11:
                continue
            ring = int(d // 2.5)
            v[x, 21:24, z] = [K('cloth_an_w'), K('straw'), K('cloth_an_w'),
                              K('straw'), K('cor')][min(4, ring)]
    for _ in range(int(rng.integers(2, 5))):
        x = 16 + int(rng.integers(-8, 9))
        z = 26 + int(rng.integers(-7, 8))
        box(v, x, x + 1, 24, 31, z, z + 1, K('bark_an_wi'))
        box(v, x, x + 1, 29, 31, z, z + 2, K('cloth_an_w'))
    return one(v)


def pell_ang(name):
    """打ち込みの杭（戦士の集会所。傷だらけの丸太と、立てた木剣）。"""
    rng = rng_for(name)
    v = vol(h=44)
    disc(v, 14, 16, 5.0, 0, 38, K('bark_an_s'))
    disc(v, 14, 16, 3.2, 0, 38, K('bark_an_sl'))
    disc(v, 14, 16, 7.0, 0, 4, K('rub_d'))
    for _ in range(int(rng.integers(8, 14))):
        z = int(rng.integers(12, 36))
        a = rng.random() * 6.283
        px = 14 + int(round(np.cos(a) * 4))
        py = 16 + int(round(np.sin(a) * 4))
        box(v, px, px + 2, py, py + 2, z, z + 2, K('bark_an_scar'))
    box(v, 24, 27, 22, 25, 0, 26, K('bark_an_wd'))           # 立てた木剣
    box(v, 22, 29, 21, 26, 20, 22, K('bark_an_w'))
    return one(v)


def moon_shrine_ang(name):
    """月の祠（寺院と聖所）— 白い石の龕に月の輪と、灯した燭。"""
    v = vol(h=40)
    box(v, 5, 27, 10, 24, 0, 5, K('marb_an_md'))
    box(v, 6, 26, 11, 23, 4, 5, K('marb_an_m'))
    for x in (7, 22):
        octagon(v, x + 1, 17, 3.0, 5, 28, K('marb_an_m'))
    box(v, 5, 27, 10, 24, 28, 32, K('marb_an_md'))
    for k in range(6):
        box(v, 5 + k, 27 - k, 10, 24, 32 + k, 33 + k, K('marb_an_m'))
    #! 月の輪（背の板に彫った三日月）。
    #! **暗い石の背板**（白い石だと三日月が浮かなかった）。
    box(v, 10, 22, 10, 13, 8, 28, K('ash_d'))
    for x in range(11, 22):
        for z in range(9, 27):
            d = (((x - 16) ** 2) + ((z - 18) ** 2)) ** 0.5
            d2 = (((x - 19) ** 2) + ((z - 18) ** 2)) ** 0.5
            if (7 >= d >= 4.5) and (d2 > 6.5):
                v[x, 10:13, z] = K('fl_an_pale')
    box(v, 14, 19, 18, 22, 5, 8, K('marb_an_m'))             # 燭台
    box(v, 15, 18, 19, 21, 8, 15, K('cream'))
    box(v, 16, 17, 19, 21, 15, 18, K('fl_an_pale'))
    box(v, 6, 26, 22, 25, 5, 7, K('marb_an_md'))             # 供物の段
    box(v, 9, 13, 22, 25, 7, 10, K('leaf_an_l'))
    box(v, 18, 23, 22, 25, 7, 9, K('fl_w'))
    return one(v)


def shrine_flame_ang(name):
    """月の祠の燭の火。"""
    v = vol(h=26)
    box(v, 15, 18, 19, 21, 15, 22, K('fl_an_core'))
    return one(v)


def offering_ang(name):
    """供物の卓（葉に載せた果実と木の器と、焚いた香）。"""
    rng = rng_for(name)
    v = vol(h=20)
    box(v, 5, 27, 10, 23, 8, 11, K('bark_an_w'))
    for x in (6, 24):
        box(v, x, x + 3, 11, 22, 0, 8, K('bark_an_sd'))
    for _ in range(int(rng.integers(4, 8))):
        px, py = int(rng.integers(7, 25)), int(rng.integers(11, 22))
        v[px, py, 11] = pick(rng, [K('fl_r'), K('fl_y'), K('leaf_an_l'), K('cream')])
        v[px, py, 12] = K('leaf_an_m')
    disc(v, 10, 16, 3.0, 11, 14, K('bark_an_wd'))
    disc(v, 10, 16, 2.0, 12, 14, K('wa_an_c'))
    box(v, 21, 23, 15, 17, 11, 18, K('bark_an_wi'))          # 香
    box(v, 21, 23, 15, 17, 17, 19, K('soot'))
    return one(v)


def bell_post_ang(name):
    """鐘の柱（若木の門形に、青銅の鐘。**鐘が振り子で揺れる**）。"""
    frame = vol(h=46)
    bell = vol(h=46)
    for x in (4, 25):
        disc(frame, x + 1, 16, 2.6, 0, 38, K('bark_an_s'))
        disc(frame, x + 1, 16, 3.6, 0, 3, K('rub_d'))
    log_x(frame, 16, 38, 2.6, 3, 29, K('bark_an_sd'))
    box(frame, 14, 19, 14, 19, 34, 38, K('bark_an_wi'))
    #! **椀形は 1 色で**（段ごとに色を変えた版は縞の柱に見えた）。
    for k in range(9):
        r = 2.6 + (k * 0.85)
        disc(bell, 16, 16, r, 34 - (k * 2), 36 - (k * 2), K('gold_d'))
    disc(bell, 16, 16, 9.0, 16, 19, K('gold_d'))             # 口の輪（いちばん太い）
    disc(bell, 16, 16, 7.4, 16, 19, 0)                       # 中を刳る
    disc(bell, 16, 16, 2.0, 12, 18, K('verd'))               # 舌
    box(bell, 15, 18, 15, 18, 34, 38, K('bark_an_wi'))       # 吊り革
    return [('frame', frame, (0, 0, 0)), ('bell', bell, (0, 0, 0))]


def bell_parts():
    return swing_parts([('bell', (16.0, 16.0, 38.0))], amplitude=7.0, period=2.2)


def still_ang(name):
    """蒸留の釜（錬金術店。銅の釜と螺旋の管、下に炭火）。"""
    v = vol(h=40)
    box(v, 4, 28, 6, 26, 0, 8, K('rub'))
    box(v, 8, 24, 10, 22, 6, 9, K('char'))
    disc(v, 15, 16, 9.0, 8, 22, K('verd'))
    disc(v, 15, 16, 7.4, 9, 22, K('gold_d'))
    for k in range(6):
        disc(v, 15, 16, 8.0 - (k * 1.1), 22 + (k * 2), 24 + (k * 2), K('verd'))
    for k in range(10):                                      # 螺旋の管
        a = k * 0.7
        px = 15 + int(round(np.cos(a) * 11))
        py = 16 + int(round(np.sin(a) * 11))
        box(v, px, px + 2, py, py + 2, 30 - (k * 2), 33 - (k * 2), K('gold_d'))
    disc(v, 26, 24, 3.4, 0, 8, K('glass'))                   # 受けの瓶
    disc(v, 26, 24, 2.4, 1, 6, K('slime'))
    return one(v)


def still_fire_ang(name):
    """蒸留の釜の炭火（**昼も光る**）。"""
    v = vol(h=16)
    box(v, 9, 23, 11, 21, 6, 10, K('fl_mid'))
    box(v, 12, 20, 13, 19, 7, 12, K('fl_core'))
    return one(v)


def herb_rack_ang(name):
    """薬草の干し台（束ねた草を逆さに吊るす）。"""
    rng = rng_for(name)
    v = vol(h=34)
    for x in (4, 26):
        box(v, x, x + 3, 13, 16, 0, 30, K('bark_an_s'))
        box(v, x, x + 3, 20, 23, 0, 30, K('bark_an_s'))
    for cy in (14, 21):
        log_x(v, cy, 29, 1.8, 3, 29, K('bark_an_sd'))
        for px in range(6, 26, 4):
            h = int(rng.integers(8, 17))
            shade = pick(rng, [K('stem_an_f'), K('leaf_an_m'), K('wilt'), K('alp_fw')],
                         [0.34, 0.28, 0.22, 0.16])
            box(v, px, px + 3, cy - 1, cy + 2, 29 - h, 28, shade)
            box(v, px, px + 3, cy - 1, cy + 2, 27, 29, K('bark_an_wi'))
    return one(v)


def star_stone_ang(name):
    """星の石（魔法屋と賢者の塔。三脚に載せた光る石。**昼も光る**）。"""
    v = vol(h=38)
    for k in range(3):
        a = k * 2.09
        px = 16 + int(round(np.cos(a) * 8))
        py = 16 + int(round(np.sin(a) * 8))
        for t in range(20):
            qx = px + int(round((16 - px) * t / 19.0))
            qy = py + int(round((16 - py) * t / 19.0))
            box(v, qx - 1, qx + 2, qy - 1, qy + 2, t, t + 2, K('bark_an_sd'))
    octagon(v, 16, 16, 5.0, 19, 23, K('marb_an_md'))
    ball(v, 16, 16, 29, 6.5, K('marb_an_m'))
    ball(v, 16, 16, 29, 4.5, K('fl_an_pale'))
    for k in range(4):                                       # 周りを巡る小さな石
        a = k * 1.57
        px = 16 + int(round(np.cos(a) * 10))
        py = 16 + int(round(np.sin(a) * 10))
        box(v, px - 1, px + 2, py - 1, py + 2, 32 + k, 35 + k, K('marb_an_m'))
    return one(v)


def star_glow_ang(name):
    """星の石の光（昼も夜も。置く側が自発光をつけて重ねる）。"""
    v = vol(h=38)
    ball(v, 16, 16, 29, 4.0, K('fl_an_core'))
    return one(v)


def rune_post_ang(name):
    """印を刻んだ石柱（魔法の店と塔の前。青く光る溝）。"""
    rng = rng_for(name)
    v = vol(h=40)
    octagon(v, 16, 16, 7.0, 0, 4, K('marb_an_md'))
    octagon(v, 16, 16, 5.0, 3, 32, K('marb_an_m'))
    for k in range(5):
        z = 6 + (k * 5)
        box(v, 10, 22, 10, 12, z, z + 3, K('fl_an_pale'))
        box(v, 10, 12, 10, 22, z, z + 3, K('fl_an_pale'))
    for k in range(4):
        octagon(v, 16, 16, 6.0 - (k * 1.2), 32 + k, 33 + k, K('marb_an_md'))
    for _ in range(int(rng.integers(2, 5))):
        x, y = int(rng.integers(10, 22)), int(rng.integers(10, 22))
        blk = v[x:x + 4, y:y + 4, 1:6]
        blk[blk != 0] = K('moss_an_d')
    return one(v)


def tarp_stall_ang(name):
    """幌の露店（闇市。継ぎはぎの布と、蓋の空いた木箱）。"""
    rng = rng_for(name)
    v = vol(h=32)
    for (x, y) in ((4, 6), (26, 6), (4, 24), (26, 24)):
        box(v, x, x + 2, y, y + 2, 0, 24, K('bark_an_sd'))
    for k in range(6):                                       # たるんだ幌
        sag = int(round(3 * np.sin(k / 5.0 * 3.14)))
        box(v, 3, 29, 5 + (k * 4), 9 + (k * 4), 24 - sag, 26 - sag,
            pick(rng, [K('cloth_an_gd'), K('rag'), K('cloth_an_wd')], [0.4, 0.34, 0.26]))
    box(v, 6, 26, 12, 22, 8, 12, K('bark_an_wd'))
    box(v, 6, 26, 12, 22, 11, 12, K('bark_an_w'))
    for px in range(8, 25, 6):
        box(v, px, px + 4, 14, 20, 12, 16, K('bark_an_wi'))
        v[px + 1, 16, 16] = K('gold')
    return one(v)


def chest_ang(name):
    """錠のかかった長持（盗賊のアジトと闇市）。"""
    v = vol(h=20)
    box(v, 4, 28, 8, 24, 0, 12, K('bark_an_wd'))
    for x in (7, 15, 23):
        box(v, x, x + 3, 8, 24, 0, 12, K('iron'))
    for k in range(6):                                       # 蒲鉾形の蓋
        box(v, 4 + k, 28 - k, 8, 24, 12 + k, 13 + k, K('bark_an_w'))
    box(v, 14, 18, 22, 25, 6, 12, K('gold_d'))               # ロック
    box(v, 15, 17, 23, 25, 8, 10, K('dark'))
    return one(v)


def brazier_ang(name):
    """篝火の鉄鉢（三脚。火は `brazier_fire_ang`）。"""
    v = vol(h=30)
    for k in range(3):
        a = k * 2.09
        px = 16 + int(round(np.cos(a) * 9))
        py = 16 + int(round(np.sin(a) * 9))
        for t in range(18):
            qx = px + int(round((16 - px) * t / 17.0))
            qy = py + int(round((16 - py) * t / 17.0))
            box(v, qx - 1, qx + 2, qy - 1, qy + 2, t, t + 2, K('iron'))
    disc(v, 16, 16, 8.0, 17, 24, K('iron'))
    disc(v, 16, 16, 6.4, 18, 24, K('emb'))
    disc(v, 16, 16, 6.4, 22, 24, K('char'))
    return one(v)


def brazier_fire_ang(name):
    """篝火の火。"""
    v = vol(h=38)
    disc(v, 16, 16, 5.4, 23, 30, K('fl_mid'))
    disc(v, 16, 16, 3.0, 25, 34, K('fl_core'))
    return one(v)


def scroll_rack_ang(name):
    """巻物の棚（書店と図書館。筒に丸めた巻物）。"""
    rng = rng_for(name)
    v = vol(h=32)
    box(v, 3, 29, 20, 27, 0, 3, K('bark_an_wd'))
    box(v, 3, 5, 20, 27, 0, 28, K('bark_an_sd'))
    box(v, 27, 29, 20, 27, 0, 28, K('bark_an_sd'))
    for z in (10, 19, 27):
        box(v, 3, 29, 20, 27, z, z + 2, K('bark_an_wd'))
    for z0 in (3, 12, 21):
        px = 6
        while px < 27:
            w = int(rng.integers(2, 4))
            disc(v, px + 1, 24, 2.6, z0, z0 + 6, pick(rng, [K('cream'), K('bark_an_w'), K('cloth_an_wd')]))
            px += w + 1
    return one(v)


def lectern_ang(name):
    """書見台（傾いた板に開いた本と、羽根ペン）。"""
    v = vol(h=32)
    disc(v, 16, 18, 4.0, 0, 4, K('bark_an_wd'))
    box(v, 14, 19, 16, 21, 3, 20, K('bark_an_sd'))
    for k in range(9):
        box(v, 5, 27, 12 + k, 14 + k, 20 + k, 23 + k, K('bark_an_w'))
    box(v, 7, 25, 14, 22, 26, 28, K('cream'))                # 開いた本
    box(v, 15, 17, 14, 22, 26, 29, K('lair'))
    box(v, 22, 24, 10, 12, 27, 33, K('cloth_an_w'))          # 羽根ペン
    return one(v)


def book_stack_ang(name):
    """積んだ本と巻物（図書館の前庭）。"""
    rng = rng_for(name)
    v = vol(h=22)
    z = 0
    for _ in range(int(rng.integers(4, 8))):
        w = int(rng.integers(10, 17))
        d = int(rng.integers(8, 13))
        x = int(rng.integers(3, 29 - w))
        y = int(rng.integers(6, 26 - d))
        h = int(rng.integers(2, 4))
        box(v, x, x + w, y, y + d, z, z + h, pick(rng, [K('lair'), K('drape'), K('cloth_an_g'), K('ai')]))
        box(v, x, x + w, y, y + d, z + h - 1, z + h, K('cream'))
        z += h
    disc(v, 25, 8, 2.6, 0, 12, K('cream'))
    return one(v)


def monument_ang(name):
    """先祖の石柱（荘園の前。彫った名と、絡む蔦）。"""
    v = vol(h=48)
    box(v, 6, 26, 8, 24, 0, 5, K('marb_an_md'))
    box(v, 7, 25, 9, 23, 4, 5, K('marb_an_m'))
    box(v, 10, 22, 12, 20, 5, 40, K('marb_an_m'))
    for z in range(9, 38, 5):                                # 彫った名（帯で出す）
        box(v, 11, 21, 12, 13, z, z + 2, K('marb_an_md'))
    for k in range(5):
        box(v, 10 + k, 22 - k, 12, 20, 40 + k, 41 + k, K('marb_an_m'))
    box(v, 14, 18, 12, 20, 45, 48, K('fl_an_pale'))          # 頂の月の石
    for z in range(2, 26, 3):                                # 蔦
        dx = int(round(2 * np.sin(z * 0.3)))
        box(v, 9 + dx, 12 + dx, 18, 21, z, z + 3, K('leaf_an_m'))
    box(v, 6, 26, 8, 24, 0, 3, K('moss_an_d'))
    return one(v)


def beast_cage_ang(name):
    """獣の檻（モンスター仙人。編み枝の檻に藁と骨）。"""
    rng = rng_for(name)
    v = vol(h=28)
    box(v, 3, 29, 5, 27, 0, 3, K('bark_an_wd'))
    box(v, 4, 28, 6, 26, 2, 3, K('straw_d'))
    for p in range(4, 29, 3):                                # 縦の格子
        box(v, p, p + 2, 5, 7, 3, 24, K('bark_an_s'))
        box(v, p, p + 2, 25, 27, 3, 24, K('bark_an_s'))
    for p in range(6, 27, 3):
        box(v, 3, 5, p, p + 2, 3, 24, K('bark_an_s'))
        box(v, 27, 29, p, p + 2, 3, 24, K('bark_an_s'))
    for z in (10, 22):
        box(v, 3, 29, 5, 27, z, z + 2, K('bark_an_wi'))
        box(v, 4, 28, 6, 26, z, z + 2, 0)
    box(v, 3, 29, 5, 27, 24, 27, K('bark_an_sd'))            # 板の蓋
    for p in range(4, 28, 4):                                # 板の継ぎ目
        box(v, p, p + 1, 5, 27, 26, 27, K('bark_an_scar'))
    box(v, 3, 6, 5, 27, 26, 27, K('moss_an_d'))              # 苔は縁だけ
    for _ in range(int(rng.integers(2, 5))):                 # 骨
        x, y = int(rng.integers(6, 25)), int(rng.integers(8, 24))
        box(v, x, x + 5, y, y + 2, 3, 5, K('bone'))
    return one(v)


def feed_trough_ang(name):
    """飼い葉桶（割った丸太を刳る。干し草と水）。"""
    v = vol(h=14)
    log_x(v, 16, 5, 7.0, 2, 30, K('bark_an_sd'))
    log_x(v, 16, 5, 5.4, 2, 30, K('bark_an_s'))
    box(v, 3, 29, 11, 21, 6, 11, 0)
    box(v, 3, 29, 11, 21, 6, 8, K('wa_an_c'))
    box(v, 4, 14, 11, 21, 8, 11, K('hay'))
    box(v, 5, 13, 12, 20, 10, 12, K('hay_d'))
    for x in (4, 26):
        box(v, x, x + 3, 12, 15, 0, 4, K('bark_an_sd'))
        box(v, x, x + 3, 17, 20, 0, 4, K('bark_an_sd'))
    return one(v)


def junk_ang(name):
    """がらくた（盗賊のアジトの隅。割れた籠・空き樽・打ち捨てた布）。"""
    rng = rng_for(name)
    v = vol(h=18)
    _barrel(v, 8, 9, 5.0, 0, 11, K('bark_an_scar'), K('rust'))
    box(v, 6, 11, 6, 12, 9, 11, 0)
    disc(v, 22, 12, 5.4, 0, 5, K('bark_an_wid'))
    box(v, 20, 27, 8, 12, 3, 5, 0)
    for _ in range(int(rng.integers(3, 7))):
        x, y = int(rng.integers(4, 26)), int(rng.integers(16, 28))
        box(v, x, x + int(rng.integers(4, 9)), y, y + int(rng.integers(3, 6)), 0, 3,
            pick(rng, [K('rag'), K('rot'), K('cloth_an_wd'), K('bark_an_scar')],
                 [0.3, 0.26, 0.24, 0.20]))
    box(v, 24, 30, 20, 24, 0, 8, K('bark_an_sd'))
    return one(v)


def market_stall_ang(name):
    """市の屋台（森の市。枝の骨組みに布、台に果実と籠）。"""
    rng = rng_for(name)
    v = vol(h=40)
    for (x, y) in ((3, 5), (27, 5), (3, 25), (27, 25)):
        disc(v, x, y, 2.0, 0, 30, K('bark_an_s'))
    roof = pick(rng, [K('cloth_an_g'), K('cloth_an_y'), K('cloth_an_w')])
    for k in range(8):                                       # 山形の布屋根
        box(v, 2, 30, 4 + (k * 3), 7 + (k * 3), 30 + min(k, 7 - k) * 2, 33 + min(k, 7 - k) * 2, roof)
    box(v, 4, 28, 10, 24, 12, 15, K('bark_an_wd'))
    box(v, 4, 28, 10, 24, 14, 15, K('bark_an_w'))
    for px in range(6, 26, 5):
        disc(v, px + 2, 14, 2.6, 15, 20, K('bark_an_wi'))
        for _ in range(4):
            v[px + int(rng.integers(0, 4)), 12 + int(rng.integers(0, 5)), 20] = pick(
                rng, [K('fl_r'), K('fl_y'), K('leaf_an_l'), K('cor')])
    return one(v)


# ------------------------------------------------------------------ 登録
def register(g):
    """`gen_prefabs.py` の名前空間 `g` へアングウィルの素材を足す。

    **`TOWN_STYLES` に `'ang'` を足す**——`_slice_of` が名前から意匠を切り出せないと、
    `house_wall_nw_ang` のスライスが `'ang'` に化けて 9 スライスが全部同じ形になる。
    """
    global G, C, V
    G = g
    C = g['C']
    V = g['V']
    g['TOWN_STYLES'] = tuple(g['TOWN_STYLES']) + ('ang',)
    _palette()
    cat = g['CATALOG']
    static = g['static_part']
    wind = static(motion={'kind': 'wind'}, wind_k=0.7)
    wind_soft = static(motion={'kind': 'wind'}, wind_k=0.4)

    # ---- 家（9 スライス）----
    for sl in g['HOUSE_SLICES']:
        cat['house_wall_%s_ang' % sl] = (house_wall_ang, static(),
                                         'アングウィルの壁（丸太の高床と樹皮の板。%s）' % sl)
        cat['house_roof_%s_ang' % sl] = (house_roof_ang, static(),
                                         'アングウィルの屋根（苔の板葺き。%s）' % sl)
    cat['house_entrance_ang'] = (house_entrance_ang, static(), 'アングウィルの戸口（葉を彫った白木の戸）')
    cat['house_light_ang'] = (house_light_ang, static(), '窓の灯り（アングウィルの窓の高さ）')
    cat['chimney_ang'] = (chimney_ang, static(), '煙出し（roof_vent。板の笠と苔）')

    # ---- 柵・門・街灯 ----
    for side in ('n', 's', 'w', 'e'):
        cat['fence_%s_ang' % side] = (fence_ang, static(), '編み枝の垣（%s 側）' % side)
        cat['hedge_ang_%s' % side] = (hedge_ang, static(), '区画の生垣（%s 側）' % side)
    cat['arch_ang'] = (arch_ang, static(), '若木を曲げた門（南北にくぐる）')
    cat['arch_ang_ew'] = (arch_ang_ew, static(), '若木を曲げた門（東西にくぐる）')
    cat['arch_flame_ang'] = (arch_flame_ang, static(), '門の角灯の火（夜。gate_flame）')
    cat['arch_flame_ang_ew'] = (arch_flame_ang_ew, static(), '門の角灯の火（東西）')
    cat['lamp_post_ang'] = (lamp_post_ang, static(), '道端の吊り灯（lamp_step）')
    cat['lamp_flame_ang'] = (lamp_flame_ang, lamp_flame_parts(), '道端の吊り灯の火')

    # ---- 地面 ----
    for i in range(1, 5):
        cat['ground_path_ang_%02d' % i] = (ground_path_ang, static(), '森の小径（落ち葉と根と木道）')
        cat['ground_turf_ang_%02d' % i] = (ground_turf_ang, static(), '林床（苔と落ち葉と下草）')
    for i in range(1, 4):
        cat['ground_trail_ang_%02d' % i] = (ground_trail_ang, static(), '集落の踏み跡（DIRT の差し替え）')
    for i in range(1, 3):
        cat['ground_glade_ang_%02d' % i] = (ground_glade_ang, static(), '日の差す空き地の草')
        cat['ground_field_ang_%02d' % i] = (ground_field_ang, static(), '畑の畝と芽')
    cat['yard_ground_ang'] = (yard_ground_ang, static(), '前庭の地面（踏み固めた土と木くず）')
    cat['grass_ang'] = (grass_ang, wind, 'シダの株（tuft）')
    cat['grass_edge_ang'] = (grass_edge_ang, wind, '道の縁の下草（path_edge）')

    # ---- 塀・櫓 ----
    for i in range(1, 4):
        cat['palisade_ang_%02d' % i] = (palisade_ang, static(), '生きた木を編んだ塀')
    cat['watchtower_ang'] = (watchtower_ang, static(), '大木の上の見張り台（4.4 マス）')

    # ---- 森の木立（grove）----
    for i in range(1, 7):
        cat['tree_ang_%02d' % i] = (tree_ang, wind_soft, 'アングウィルの木立（針葉樹。白い幹に段の円錐。変種 %d/6）' % i)

    # ---- ダイナミックな大物 ----
    cat['great_tree_ang'] = (great_tree_ang, wind_soft, '母なる大樹（5×5 マス・11 マス。樹上の家 2 段）')
    cat['great_tree_glow_ang'] = (great_tree_glow_ang, static(), '母なる大樹の吊り灯の光（夜）')
    cat['rope_bridge_ew_ang'] = (rope_bridge_ew_ang, bridge_parts(), '樹上の吊り橋（東西。揺れる）')
    cat['rope_bridge_ns_ang'] = (rope_bridge_ns_ang, bridge_parts(), '樹上の吊り橋（南北。揺れる）')
    cat['tower_sage_ang'] = (tower_sage_ang, static(), '賢者の塔（3×3 マス・14 マスの白い尖塔）')
    cat['tower_sage_glow_ang'] = (tower_sage_glow_ang, static(), '賢者の塔の頂の光（夜）')
    cat['tower_trump_ang'] = (tower_trump_ang, static(), 'トランプ魔術の塔（2×2 マス・13 マスの尖塔）')
    cat['tower_trump_glow_ang'] = (tower_trump_glow_ang, static(), 'トランプ魔術の塔の月の石の光（夜）')
    cat['mill_ang'] = (mill_ang, static(), '森番の高倉（目印 mill。4×3 マス）')
    cat['gatehouse_ang'] = (gatehouse_ang, static(), '森の関門（2 マス幅・9 マスの白石の門楼）')
    cat['moot_ring_ang'] = (moot_ring_ang, static(), '集いの環（3×3 マスの立石と炉）')
    cat['forest_altar_ang'] = (forest_altar_ang, static(), '森の祭壇（月の石の卓）')
    cat['forest_altar_flame_ang'] = (forest_altar_flame_ang, static(), '森の祭壇の燭の火（夜）')

    # ---- 裂け谷の骨（2026-09-05。§9）----
    cat['manor_ang'] = (manor_ang, static(), '最後の憩いの館（7×5 マス・12 マス。landmarks BUILDING_1）')
    cat['manor_lights_ang'] = (manor_lights_ang, static(), '館の回廊の灯り（features の相方）')
    cat['manor_lights_flame_ang'] = (manor_lights_flame_ang, static(), '館の回廊の灯りの芯（夜）')
    cat['fountain_ang'] = (fountain_ang, fountain_parts(), '泉（3×3 マス。3 段の水盤。水が明滅で落ちる）')
    cat['cascade_ang'] = (cascade_ang, cascade_parts(), '段々の水盤（1 マス。水が明滅で落ちる）')
    cat['colonnade_ew_ang'] = (colonnade_ew_ang, static(), '高い回廊（東西。3 マス・6.8 マス）')
    cat['colonnade_ns_ang'] = (colonnade_ns_ang, static(), '高い回廊（南北）')
    cat['statue_ang'] = (statue_ang, static(), '石の像（白石のエルフ。3.6 マス）')

    # ---- 小物（1 マス）----
    props = {
        'barrel': (barrel_ang, '樽 2 つ'),
        'ale_barrels': (ale_barrels_ang, '寝かせた酒樽'),
        'crate': (crate_ang, '木箱と籠の積み'),
        'basket': (basket_ang, '編み籠 3 つ'),
        'sack': (sack_ang, '麻袋 3 つ'),
        'bench': (bench_ang, '割った丸太の長椅子'),
        'table_bench': (table_bench_ang, '外の卓と長椅子'),
        'woodpile': (woodpile_ang, '薪の山'),
        'well': (well_ang, '井戸（滑車と桶）'),
        'rain_barrel': (rain_barrel_ang, '雨水の桶'),
        'cart': (cart_ang, '二輪の荷車'),
        'drying_rack': (drying_rack_ang, '干し棚'),
        'hitching_post': (hitching_post_ang, '馬つなぎ'),
        'lantern_post': (lantern_post_ang, '庭の吊り灯'),
        'fallen_log': (fallen_log_ang, '倒木（苔と茸）'),
        'stump': (stump_ang, '切り株'),
        'sapling': (sapling_ang, '若木の苗'),
        'bramble': (bramble_ang, '茨の茂み'),
        'deer': (deer_ang, '鹿'),
        'rabbit': (rabbit_ang, '兎'),
        'owl_perch': (owl_perch_ang, '梟の止まり木'),
        'beehive': (beehive_ang, '蜂の巣箱'),
        'garden_plot': (garden_plot_ang, '菜園の畝'),
        'goods_stall': (goods_stall_ang, '雑貨の売り台'),
        'armour_stand': (armour_stand_ang, '鎧の立て台'),
        'shield_tree': (shield_tree_ang, '盾を掛けた枯木'),
        'forge': (forge_ang, '森の鍛冶床。火は forge_fire_ang'),
        'forge_fire': (forge_fire_ang, '鍛冶床の火'),
        'anvil': (anvil_ang, '金床と槌'),
        'weapon_rack': (weapon_rack_ang, '武器立て'),
        'bow_rack': (bow_rack_ang, '弓と矢筒の棚'),
        'arrow_barrel': (arrow_barrel_ang, '矢の束を挿した樽'),
        'butts': (butts_ang, '弓の的'),
        'pell': (pell_ang, '打ち込みの杭'),
        'moon_shrine': (moon_shrine_ang, '月の祠。火は shrine_flame_ang'),
        'shrine_flame': (shrine_flame_ang, '月の祠の燭の火'),
        'offering': (offering_ang, '供物の卓'),
        'still': (still_ang, '蒸留の釜。火は still_fire_ang'),
        'still_fire': (still_fire_ang, '蒸留の釜の炭火'),
        'herb_rack': (herb_rack_ang, '薬草の干し台'),
        'star_stone': (star_stone_ang, '星の石。光は star_glow_ang'),
        'star_glow': (star_glow_ang, '星の石の光'),
        'rune_post': (rune_post_ang, '印を刻んだ石柱'),
        'tarp_stall': (tarp_stall_ang, '幌の露店'),
        'chest': (chest_ang, '錠のかかった長持'),
        'brazier': (brazier_ang, '篝火の鉄鉢。火は brazier_fire_ang'),
        'brazier_fire': (brazier_fire_ang, '篝火の火'),
        'scroll_rack': (scroll_rack_ang, '巻物の棚'),
        'lectern': (lectern_ang, '書見台'),
        'book_stack': (book_stack_ang, '積んだ本'),
        'monument': (monument_ang, '先祖の石柱'),
        'beast_cage': (beast_cage_ang, '獣の檻'),
        'feed_trough': (feed_trough_ang, '飼い葉桶'),
        'junk': (junk_ang, 'がらくた'),
        'market_stall': (market_stall_ang, '市の屋台'),
    }
    for key, (builder, note) in props.items():
        cat[key + '_ang'] = (builder, static(), 'アングウィルの小物: ' + note)
    cat['lantern_flame_ang'] = (lantern_flame_ang, lantern_flame_parts(), '庭の吊り灯の火')
    cat['laundry_ang'] = (laundry_ang, laundry_parts(), '洗濯物（布が揺れる）')
    cat['banner_ang'] = (banner_ang, banner_parts(), 'エルフの幟（風でなびく）')
    cat['vine_ang'] = (vine_ang, wind, '壁を這う蔓')
    cat['bell_post_ang'] = (bell_post_ang, bell_parts(), '鐘の柱（鐘が揺れる）')

    # ---- 変種のある小物 ----
    for i in range(1, 4):
        cat['fern_ang_%02d' % i] = (fern_ang, wind, 'アングウィルの小物: 羊歯の茂み')
    for i in range(1, 3):
        cat['mushrooms_ang_%02d' % i] = (mushrooms_ang, static(), 'アングウィルの小物: 茸の群れ')
        cat['boulder_ang_%02d' % i] = (boulder_ang, static(), 'アングウィルの小物: 苔むした岩')
        cat['flowers_ang_%02d' % i] = (flowers_ang, wind, 'アングウィルの小物: 林の花')
