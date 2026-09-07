# -*- coding: utf-8 -*-
"""テルモラ（町 2 番）の「秩序と威厳」への作り直し — 素材の生成器
。

`gen_prefabs.py` の末尾から `register(globals())` で呼ばれ、接尾辞 `_telb` の素材を
`CATALOG` へ足す。**既存の `_tel`（`telmora_prefabs.py`）には 1 バイトも触らない**
——`yards` などで名指しした `_tel` の小物（樽・木箱・炉・幟・鐘楼・門楼・櫓など）は
そのまま使い続ける。この生成器が足すのは「秩序と威厳」の石造りの骨組みだけである。

## この意匠の材（§9.2 #3。「モリバントとは違う重い印象」と決めた）
- 石は**灰青の花崗岩**（`rock_granite*`）と**黒みの玄武岩**（`rock_basalt*`。礎・縁石・雨落ち）
- 屋根は**スレート**（`roof_slate*`。青灰）
- 金物は**黒い鉄**（`iron_black`）と**金**（`gold_gilt`）
- 樹冠は**刈り込んだ深緑**（`leaf_clipped*`）
- 切石（`ash` `flag`）・漆喰・赤茶の瓦のようなモリバントの材は使わない

## 色の名前について（`palette_materials.classify` との整合）
設計書 §9.4 は「名前は `stone_` `slate_` `iron_` `leaf_` `metal_` で始める」と書くが、
実際の `tools/voxel/palette_materials.py` の `KEYWORDS` には `stone` と `metal` という
語そのものは無い（`Stone` は `wall` `rock` `tile` `pav` `gravel` … を見る／`Metal` は
`iron` `steel` `gold` … を見る）。**意図（材質の宣言が正しく付くこと）を満たすため**、
実際に当たる語で名前を始める——花崗岩・玄武岩は `rock_`、スレートは `roof_`、
金は `gold_` を使う（`iron_` と `leaf_` は設計書の語がそのまま当たる）。値と用途は
設計書のとおりで、頭の語だけを実装の都合で変えてある（最終報告に理由を記す）。

## 作りの規律（`gen_prefabs.py` 冒頭・§9.2 #3 と同じ）
- 無作為な表面ノイズは入れない。彫りは 1 ボクセルの深さ。色は塊ごとに 1 色
- 接地層（z=0）の平面方向は縮めない。**+y は南＝カメラ側**。戸口は手前が y 大
- 柵の高さ（14）は変えない（遮蔽の約束）。**ただしテルモラの `fence_*_telb` だけは
  第 3 回の直し（§9.11 #3。決めたこと）でこの約束を外し、高さ 40（花崗岩の塀 32
  ＋鉄柵 8）にした**。理由と影響の範囲は `fence_telb()` の docstring に書く
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


def disc(v, cx, cy, r, z0, z1, colour, hollow=0.0):
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


def ball(v, cx, cy, cz, r, colour, squash=1.0):
    """球（`squash` < 1 で上下に潰す）。"""
    X, Y, Z = v.shape
    xs = np.arange(X)[:, None, None] + 0.5 - cx
    ys = np.arange(Y)[None, :, None] + 0.5 - cy
    zs = (np.arange(Z)[None, None, :] + 0.5 - cz) / squash
    v[(xs * xs) + (ys * ys) + (zs * zs) <= (r * r)] = colour


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
    parts = [{'name': 'frame', 'voxels': 'frame', 'grounded': True,
              'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    for name in names:
        parts.append({'name': name, 'voxels': name, 'parent': 'frame', 'grounded': False,
                      'motion': {'kind': 'wind'}, 'wind_k': float(wind_k)})
    return parts


# ------------------------------------------------------------------ 色
def _palette():
    rl = G['reg_local']
    #! 花崗岩（壁・柵・記念柱など）。3 階調。**`rock_` で始める**——`Stone` の実際の
    #! 当たり語は `rock` であって `stone` ではない（冒頭の註記）。**第 2 回の直しで
    #! 一段暗くした**（設計 §9.9 #5「建物が明るすぎる」。値は決めたことどおり）。
    rl('rock_granite_d', (0x60, 0x64, 0x68))
    rl('rock_granite',   (0x7A, 0x7E, 0x84))
    rl('rock_granite_l', (0x8E, 0x92, 0x98))
    #! 玄武岩（礎・縁石）。**第 2 回で礎をはっきり暗く**（§9.9 #5）。
    rl('rock_basalt',    (0x3E, 0x40, 0x44))
    rl('rock_basalt_l',  (0x50, 0x52, 0x56))
    #! 歩道・広場・前庭の大判の敷石（**第 2 回で新設**。§9.9 #1「地面が白っぽい」）。
    #! 壁の花崗岩（`rock_granite*`）とは別の名前——歩道の直しが壁の色を巻き込まないため。
    #! 中間の灰（平均 0x7E8286）。広場はこの `_l` を主にして「歩道より半段明るい」を作る。
    rl('rock_pave_d', (0x74, 0x78, 0x7C))
    rl('rock_pave',   (0x7E, 0x82, 0x86))
    rl('rock_pave_l', (0x8A, 0x8E, 0x92))
    #! 車道の小舗石（**第 2 回で新設**）。炭のような暗い灰（平均 0x44474B）。
    rl('rock_road_d', (0x3C, 0x3F, 0x43))
    rl('rock_road',   (0x44, 0x47, 0x4B))
    rl('rock_road_l', (0x4E, 0x51, 0x55))
    #! スレート（屋根）。**`roof_` で始める**——`Stone` の当たり語に `roof` がある。
    rl('roof_slate_d', (0x3A, 0x42, 0x50))
    rl('roof_slate',   (0x4A, 0x54, 0x62))
    rl('roof_slate_l', (0x5C, 0x66, 0x76))
    #! 鉄と金。`iron_` はそのまま当たる。金は**`gold_` で始める**（`Metal` の当たり語）。
    rl('iron_black', (0x26, 0x28, 0x2C))
    rl('gold_gilt',  (198, 168, 72))
    #! 街灯のランタンの硝子（**第 2 回で新設**）。明るい暖色。**`iron_` ではなく `glass_`
    #! で登録する**（決めたこと §9.9 #4——ガラスを鉄の色にすると夜に光って見えない）。
    rl('glass_amber', (255, 200, 120))
    #! 刈り込んだ樹冠（`leaf_` はそのまま当たる）。
    rl('leaf_clipped_d', (46, 74, 50))
    rl('leaf_clipped',   (60, 96, 64))
    #! 練兵場の白線（`Clean` に見えるよう `wa_` は避け、汚れてよいので `soil` 系にせず既存の白を借用）。
    rl('rock_line', (208, 206, 200))
    #! 旗竿の布（王の紋。`cloth_` で布の材質）。`telmora_prefabs.py` の色には依らない
    #! （読み込みの順に依存させない——この生成器だけで閉じる）。
    rl('cloth_tb_r', (168, 44, 40))
    #! 貴族の庭園（**第 3 回の直しで新設**。§9.11 #4）。花壇の花（赤の 1 色）。
    rl('flower_red', (176, 48, 44))
    #! 噴水盤の水面（青灰の 1 色。静止）。**`glass_` で始める**——`glass_amber` と
    #! 同じ理由（`palette_materials.classify` は `glass` を含む名前を `Clean` と見る）。
    rl('glass_water', (0x5C, 0x6C, 0x7A))


# ------------------------------------------------------------------ 共通の作り
WALL_H = 40   #!< 壁 1 層の高さ（§9.2 #6「壁 1 層は 40 ボクセルに」）
ROOF_H = 32   #!< 屋根の高さ
EAVE = 4
INSET = 2
PLINTH = 8    #!< 玄武岩の礎（§9.4 の「玄武岩の礎 8」）
#!< 敷地の塀（第 3 回の直し。§9.11 #3。こう決めた——「柵は 1 ブロックの高さの塀に
#!< してその上には侵入防止の先のとがった柵を」）。**このツリーの「柵の高さ 14 は
#!< 変えない」約束は、テルモラの `_telb` だけ外す**（決めたこと）。
FENCE_WALL_H = 32  #!< 花崗岩の塀（1 マス）
FENCE_RAIL_H = 8   #!< 鉄の槍頭の柵（塀の上）
FENCE_H = FENCE_WALL_H + FENCE_RAIL_H  # 40


def _plinth(v, rng, height=PLINTH):
    """玄武岩の礎。**マスいっぱい**（罠 5。土台に隙間を作らない）。面取りの目地を横へ通す。"""
    box(v, 0, V, 0, V, 0, height, K('rock_basalt'))
    for z in range(2, height - 1, 3):
        box(v, 0, V, 0, V, z, z + 1, K('rock_basalt_l'))
    box(v, 0, V, 0, V, height - 1, height, K('rock_basalt_l'))


def _ashlar_bands(v, x0, x1, y0, y1, z0, z1, bands=4):
    """花崗岩の切石の段（`bands` 段の帯で 2 色。§9.4）。段の境を 1 ボクセル彫って影にする。
    **1 階（`i == 0`）だけ 2 ボクセル幅の暗い目地を等間隔で足す**——粗石積みにする（§9.9 #5）。
    「建物が明るすぎて重さが出ない」ため。
    """
    total = z1 - z0
    for i in range(bands):
        a = z0 + int(round((i * total) / bands))
        b = z0 + int(round(((i + 1) * total) / bands))
        colour = K('rock_granite') if (i % 2 == 0) else K('rock_granite_l')
        box(v, x0, x1, y0, y1, a, b, colour)
        if i > 0:
            box(v, x0, x1, y0, y1, a, a + 1, K('rock_granite_d'))
        if i == 0:
            for z in range(a + 5, b - 1, 6):
                box(v, x0, x1, y0, y1, z, z + 2, K('rock_granite_d'))


def _face_slice(x0, x1, y0, y1, side):
    """外に面した 1 ボクセルの面を (x スライス, y スライス) で返す（`house_wall` と同じ流儀）。"""
    if side == 'n':
        return (slice(x0, x1), slice(y0, y0 + 1))
    if side == 's':
        return (slice(x0, x1), slice(y1 - 1, y1))
    if side == 'w':
        return (slice(x0, x0 + 1), slice(y0, y1))
    return (slice(x1 - 1, x1), slice(y0, y1))


def _pilaster(v, x0, x1, y0, y1, corner, z0, z1):
    """四隅の付け柱（隅石）。外へ 1 ボクセル張り出す（`INSET` の隙間へ）。"""
    cx = x0 if 'x0' in corner else x1
    cy = y0 if 'y0' in corner else y1
    ex = -1 if 'x0' in corner else 1
    ey = -1 if 'y0' in corner else 1
    px0, px1 = (cx - 3, cx) if ex < 0 else (cx, cx + 3)
    py0, py1 = (cy - 3, cy) if ey < 0 else (cy, cy + 3)
    box(v, px0, px1, py0, py1, z0, z1, K('rock_granite_l'))
    for z in range(z0 + 6, z1, 8):
        box(v, px0, px1, py0, py1, z, z + 1, K('rock_granite_d'))


def _window(v, side, x0, x1, y0, y1, at, wz0, wz1, width=6, depth=2):
    """細長いアーチ窓 1 つ。窓台・要石つき。**窓だけは彫りの規律の例外**——奥行き
    `depth`（既定 2 ボクセル）で開口を彫り、その奥へ硝子を置く（§9.9 #5 で決めた）。
    """
    fx, fy = _face_slice(x0, x1, y0, y1, side)
    span = fx if side in ('n', 's') else fy
    lo, hi = span.start, span.stop
    a0 = max(lo, min(hi - width, at - (width // 2)))
    a1 = a0 + width
    # 開口（外の面から `depth` 段だけ抜く）。
    if side == 'n':
        box(v, a0, a1, y0, y0 + depth, wz0, wz1, 0)
    elif side == 's':
        box(v, a0, a1, y1 - depth, y1, wz0, wz1, 0)
    elif side == 'w':
        box(v, x0, x0 + depth, a0, a1, wz0, wz1, 0)
    else:
        box(v, x1 - depth, x1, a0, a1, wz0, wz1, 0)
    # 硝子（開口のいちばん奥）。
    if side == 'n':
        box(v, a0, a1, y0 + depth, y0 + depth + 1, wz0, wz1, K('glass'))
    elif side == 's':
        box(v, a0, a1, y1 - depth - 1, y1 - depth, wz0, wz1, K('glass'))
    elif side == 'w':
        box(v, x0 + depth, x0 + depth + 1, a0, a1, wz0, wz1, K('glass'))
    else:
        box(v, x1 - depth - 1, x1 - depth, a0, a1, wz0, wz1, K('glass'))
    # 窓台（下に 1 段。明るい花崗岩）。
    if side in ('n', 's'):
        box(v, a0 - 1, a1 + 1, fy.start, fy.stop, wz0 - 2, wz0, K('rock_granite_l'))
    else:
        box(v, fx.start, fx.stop, a0 - 1, a1 + 1, wz0 - 2, wz0, K('rock_granite_l'))
    # 要石（上端の中央だけ張り出す）。
    mid0, mid1 = a0 + (width // 2) - 1, a0 + (width // 2) + 1
    if side in ('n', 's'):
        box(v, mid0, mid1, fy.start, fy.stop, wz1, wz1 + 2, K('rock_granite_l'))
    else:
        box(v, fx.start, fx.stop, mid0, mid1, wz1, wz1 + 2, K('rock_granite_l'))


def _cornice(v, x0, x1, y0, y1, faces, z0, z1):
    """軒の蛇腹。外へ 2 ボクセル張り出す帯（明るい花崗岩）。**下端に 1 ボクセルの暗い
    影の筋**を通す（§9.9 #5 で決めた「上端の蛇腹の下に暗い影の筋」）。
    """
    for side in faces:
        if side == 'n':
            box(v, x0 - 2, x1 + 2, max(0, y0 - 2), y0 + 1, z0 - 1, z0, K('rock_granite_d'))
            box(v, x0 - 2, x1 + 2, max(0, y0 - 2), y0 + 1, z0, z1, K('rock_granite_l'))
        elif side == 's':
            box(v, x0 - 2, x1 + 2, y1 - 1, min(V, y1 + 2), z0 - 1, z0, K('rock_granite_d'))
            box(v, x0 - 2, x1 + 2, y1 - 1, min(V, y1 + 2), z0, z1, K('rock_granite_l'))
        elif side == 'w':
            box(v, max(0, x0 - 2), x0 + 1, y0 - 2, y1 + 2, z0 - 1, z0, K('rock_granite_d'))
            box(v, max(0, x0 - 2), x0 + 1, y0 - 2, y1 + 2, z0, z1, K('rock_granite_l'))
        else:
            box(v, x1 - 1, min(V, x1 + 2), y0 - 2, y1 + 2, z0 - 1, z0, K('rock_granite_d'))
            box(v, x1 - 1, min(V, x1 + 2), y0 - 2, y1 + 2, z0, z1, K('rock_granite_l'))


# ------------------------------------------------------------------ 建物
def house_wall_telb(name):
    """石造りの壁 1 層。玄武岩の礎・花崗岩の切石（4 段 2 色）・隅の付け柱・
    面ごとの細長いアーチ窓 2 つ・軒の蛇腹。§9.4「house_wall_*_telb」。
    """
    north, south, west, east = _outward(name)
    v = vol(h=WALL_H)
    _plinth(v, rng_for(name))
    x0 = INSET if west else 0
    x1 = V - INSET if east else V
    y0 = INSET if north else 0
    y1 = V - INSET if south else V
    body0, body1 = PLINTH, WALL_H - 2
    _ashlar_bands(v, x0, x1, y0, y1, body0, body1)
    faces = [s for s, on in (('n', north), ('s', south), ('w', west), ('e', east)) if on]
    # 窓（面ごとに 2 つ）。'mid'（外に面した辺が無い）は素の躯体のまま。
    wz0, wz1 = body0 + 8, body1 - 6
    for side in faces:
        span = (x0, x1) if side in ('n', 's') else (y0, y1)
        lo, hi = span
        q = max(1, (hi - lo) // 4)
        _window(v, side, x0, x1, y0, y1, lo + q, wz0, wz1)
        _window(v, side, x0, x1, y0, y1, hi - q, wz0, wz1)
    # 隅の付け柱（外に面した辺が 2 つ交わる角だけ）。
    corners = [(west, north, 'x0y0'), (east, north, 'x1y0'),
               (west, south, 'x0y1'), (east, south, 'x1y1')]
    for a, b, key in corners:
        if a and b:
            _pilaster(v, x0, x1, y0, y1, key, PLINTH, body1)
    _cornice(v, x0, x1, y0, y1, faces, body1, WALL_H)
    return one(v)


def house_roof_telb(name):
    """スレートの寄棟。軒に石の胸壁、棟に鉄の飾り、南北の面に小さな屋根窓。§9.4。"""
    north, south, west, east = _outward(name)
    rng = rng_for(name)
    height, hip = G['_roof_field'](north, south, west, east, EAVE, ROOF_H, 1.0)
    v = vol(h=ROOF_H)
    for x in range(V):
        for y in range(V):
            h = int(height[x, y])
            for z in range(h):
                if z < EAVE:
                    #! 軒の胸壁（低い手すり。狭間を交互に抜く）。
                    v[x, y, z] = 0 if (((x + y) % 6) < 1) else K('rock_granite')
                else:
                    row = (z - EAVE) // 3
                    v[x, y, z] = K('roof_slate') if (row % 2 == 0) else K('roof_slate_d')
            if h > EAVE:
                v[x, y, h - 1] = K('roof_slate_l') if hip[x, y] else v[x, y, h - 1]
    # 棟の鉄の飾り（頂の 1 点だけ）。
    top = int(height.max())
    peak = np.argwhere(height == top)
    if len(peak) > 0:
        px, py = peak[len(peak) // 2]
        box(v, int(px) - 1, int(px) + 1, int(py) - 1, int(py) + 1, top, min(ROOF_H, top + 4), K('iron_black'))
    # 屋根窓（北と南の面。小さな出窓）。**局所の屋根の高さに合わせる**
    # （固定の高さで置くと、軒に近い列では屋根の外へ浮いた板になる）。
    for side, y_at in (('n', 6), ('s', V - 7)):
        if (side == 'n' and not north) or (side == 's' and not south):
            continue
        cx = V // 2
        local_h = int(height[cx, y_at])
        z0 = max(EAVE + 1, local_h - 6)
        z1 = local_h + 3
        box(v, cx - 2, cx + 2, y_at, y_at + 1, z0, z1, K('rock_granite_l'))
        box(v, cx - 1, cx + 1, y_at, y_at + 1, z0 + 1, z1 - 1, K('glass'))
    return one(v)


def house_light_telb(name):
    """窓の灯り（§9.4「house_wall_s_telbと同じ座標」）。南面の窓の高さに合わせる。"""
    v = vol(h=WALL_H)
    body0, body1 = PLINTH, WALL_H - 2
    wz0, wz1 = body0 + 8, body1 - 6
    q = max(1, (V - 2 * INSET) // 4)
    y1 = V - INSET
    for at in (INSET + q, (V - INSET) - q):
        a0, a1 = at - 3, at + 3
        box(v, a0, a1, y1 - 2, y1 - 1, wz0, wz1, K('lit'))
    return one(v)


def house_entrance_telb(name):
    """見た目だけの玄関。石段 3 段・鉄鋲の樫の両開き戸・石のアーチとペディメント・
    両脇にランタンの腕木。§9.4。"""
    v = vol(h=40)
    y1 = 31
    for i, z in enumerate((0, 1, 2)):
        box(v, 8 - i, 24 + i, y1 - i, y1 + 1, z, z + 1, K('rock_granite_l'))
    box(v, 7, 25, y1 - 1, y1 + 1, 3, 26, K('rock_granite'))       # アーチの枠
    for x in range(7, 25):
        t = (x - 15.5) / 9.5
        rise = int(round(3.0 * (1.0 - t * t)))
        box(v, x, x + 1, y1 - 1, y1 + 1, 20 + rise, 26, 0)
    box(v, 13, 19, y1 - 1, y1 + 1, 24, 27, K('rock_granite_l'))  # 要石
    box(v, 6, 26, y1 - 1, y1 + 1, 26, 29, K('rock_granite'))     # ペディメント（三角破風の代わりに帯）
    box(v, 9, 23, y1, y1 + 1, 3, 20, K('oak'))                   # 両開き戸
    box(v, 15, 17, y1, y1 + 1, 3, 20, K('oak_l'))                # 召し合わせ
    for z in (6, 12, 17):
        for x in (11, 13, 19, 21):
            v[x, y1, z] = K('iron_black')                        # 鉄鋲
    for x0, x1 in ((6, 7), (25, 26)):
        box(v, x0, x1, y1 - 1, y1 + 1, 20, 24, K('iron_black'))  # ランタンの腕木
        box(v, x0, x1, y1 - 1, y1 + 1, 24, 26, K('lit'))
    return one(v)


def fence_telb(name):
    """敷地の塀（**第 3 回の直し**。§9.11 #3。こう決めた——「建物の敷地を囲む柵は
    1 ブロックの高さの塀にしてその上には侵入防止の先のとがった柵を」）。

    **花崗岩の塀**（高さ `FENCE_WALL_H`＝32＝1 マス。切石 4 段＋笠石 1 段の張り出し）の
    上に**鉄の槍頭の柵**（高さ `FENCE_RAIL_H`＝8。2 ボクセルおきの穂先）。全体
    `FENCE_H`＝40。**§4／このモジュール冒頭の「柵の高さ 14 は変えない」約束は、
    テルモラの `_telb` だけ外す**（そう決めた）。向き（`side` で辺を選ぶ規則）と
    接地層（z=0 から塀が立ち上がる。地面に埋めない）は、直す前の `fence_telb` と
    同じ——`terrain_view.cpp` 側の柵の置き方（`townset.fence[i]` を `fence_at()` の
    ビットで選ぶ）は一切変えていない。
    """
    side = _slice(name)
    v = vol(h=FENCE_H)
    th = 3
    lo = 0 if side in ('n', 'w') else V - th
    hi = lo + th

    def put(a0, a1, z0, z1, colour):
        if side in ('n', 's'):
            box(v, a0, a1, lo, hi, z0, z1, colour)
        else:
            box(v, lo, hi, a0, a1, z0, z1, colour)

    def put_wide(a0, a1, z0, z1, colour, margin):
        wlo, whi = max(0, lo - margin), min(V, hi + margin)
        if side in ('n', 's'):
            box(v, a0, a1, wlo, whi, z0, z1, colour)
        else:
            box(v, wlo, whi, a0, a1, z0, z1, colour)

    # 花崗岩の塀（切石 4 段。2 色を交互に。段の境を 1 ボクセル彫って影にする）。
    band = FENCE_WALL_H // 4
    for i in range(4):
        z0, z1 = i * band, (i + 1) * band
        colour = K('rock_granite') if (i % 2 == 0) else K('rock_granite_l')
        put(0, V, z0, z1, colour)
        if i > 0:
            put(0, V, z0, z0 + 1, K('rock_granite_d'))
    # 笠石（1 段。左右へ 1 ボクセルずつ張り出す）。
    put_wide(0, V, FENCE_WALL_H - 4, FENCE_WALL_H, K('rock_granite_l'), margin=1)
    # 鉄の槍頭の柵（塀の上。2 ボクセルおきの穂先。横貫を 1 本）。
    rail0 = FENCE_WALL_H
    for p in range(1, V, 2):
        put(p, p + 1, rail0, rail0 + FENCE_RAIL_H - 2, K('iron_black'))
        put(p, p + 1, rail0 + FENCE_RAIL_H - 2, FENCE_H, K('iron_black'))  # 槍頭（先端）
    put(0, V, rail0 + 2, rail0 + 3, K('iron_black'))  # 横貫
    return one(v)


def watchtower_telb(name):
    """四隅の塔。円い石の塔身にマチコレーション（持ち送りの張り出し）とスレートの円錐屋根。
    高さは `watchtower_tel`（2.9 マス＝約 93 ボクセル）と同じ。"""
    height = 93
    v = vol(h=height)
    rng = rng_for(name)
    body_top = 72
    octagon(v, 16, 16, 13.5, 0, body_top, K('rock_granite'))
    #! 段の境（コース）を彫る。**既に石のある所だけ**を塗り替える
    #! （矩形で塗ると、円柱の外側の空のマスまで四角く塗られて八角が四角に化ける）。
    for z in range(0, body_top, 8):
        layer = v[:, :, z]
        layer[layer == K('rock_granite')] = K('rock_granite_d')
    octagon(v, 16, 16, 13.5, 0, 1, K('rock_granite_l'))  # 最下段は少し明るい礎
    # マチコレーション（持ち送りの張り出し）。
    octagon(v, 16, 16, 15.0, body_top, body_top + 4, K('rock_granite_l'))
    for x in range(2, V - 2, 5):
        box(v, x, x + 2, 2, V - 2, body_top, body_top + 4, 0)
        box(v, 2, V - 2, x, x + 2, body_top, body_top + 4, 0)
    octagon(v, 16, 16, 13.0, body_top + 4, body_top + 10, K('rock_granite'))
    for x in range(0, V, 6):
        box(v, x, x + 2, 15, 17, body_top + 4, body_top + 6, 0)
    # 円錐のスレート屋根。
    z0 = body_top + 10
    for k in range(height - z0):
        r = max(0.6, 12.0 - (k * 0.55))
        shade = K('roof_slate') if (k % 2 == 0) else K('roof_slate_d')
        disc(v, 16, 16, r, z0 + k, z0 + k + 1, shade)
    box(v, 15, 17, 15, 17, height - 2, height, K('iron_black'))
    hollow(v, depth=6)
    return one(v)


def yard_ground_telb(name):
    """前庭。**貴族の庭園（パルテール）**に作り直した（**第 3 回の直し**。§9.11 #4。
    「庭の中は貴族風の庭園に」と決めた）。淡い砂利の地に、マスの縁から
    2 ボクセル内側へ**刈り込んだ低い生垣の枠**（幅 2・高さ 4）、中央に**小さな花壇**
    （赤の 1 色）。マスが並ぶと生垣の升目が続けて見える——排水の鉄格子（第 2 回の
    直し）はやめた（前庭の意味は塀の外からは見えにくいので、パルテールの升目を
    優先した）。
    """
    rng = rng_for(name)
    v = vol(h=6)
    box(v, 0, V, 0, V, 0, 1, K('rock_basalt'))
    #! 淡い砂利（花崗岩の明るい階調を主に。歩道の `rock_pave` よりさらに淡くする）。
    layer = _bond_pavers(rng, [K('rock_granite_l'), K('rock_pave_l'), K('rock_pave')],
                         [0.5, 0.32, 0.18], K('rock_basalt'))
    v[:, :, 1] = layer

    def hedge_box(x0, x1, y0, y1):
        box(v, x0, x1, y0, y1, 2, 5, K('leaf_clipped'))
        box(v, x0, x1, y0, y1, 5, 6, K('leaf_clipped_d'))

    #! 刈り込んだ生垣の枠。縁から 2 ボクセル内側、幅 2、高さ 4（z 2..6）。
    inset, hw = 2, 2
    a0, a1 = inset, V - inset
    hedge_box(a0, a1, a0, a0 + hw)          # 北辺
    hedge_box(a0, a1, a1 - hw, a1)          # 南辺
    hedge_box(a0, a0 + hw, a0, a1)          # 西辺
    hedge_box(a1 - hw, a1, a0, a1)          # 東辺

    #! 中央の小さな花壇（赤の 1 色）。石の縁取りの中に花の色を敷く。
    fx0, fx1 = 13, 19
    box(v, fx0, fx1, fx0, fx1, 1, 2, K('rock_basalt_l'))
    box(v, fx0 + 1, fx1 - 1, fx0 + 1, fx1 - 1, 1, 2, K('flower_red'))
    return ground(v, 2)


def fountain_basin_telb(name):
    """噴水盤（**貴族の庭園の新設**。§9.11 #4）。円い石の水盤。水面は青灰の 1 色
    （`glass_water`。**`Clean` 材質に分類させるため `glass_` で始める**——冒頭の
    `_palette` の `glass_amber` と同じ理由）。水面は動かさない（静止でよい。
    決めたこと）。1 マス。
    """
    v = vol(h=8)
    disc(v, 16, 16, 12.0, 0, 3, K('rock_granite'))
    disc(v, 16, 16, 12.0, 2, 3, K('rock_granite_l'))
    disc(v, 16, 16, 9.5, 3, 7, K('rock_granite'), hollow=8.0)
    disc(v, 16, 16, 8.0, 3, 6, K('glass_water'))
    return one(v)


def chimney_telb(name):
    """屋根の煙突（`roof_vent`）。四角い花崗岩の煙突に笠石。"""
    v = vol(h=22)
    box(v, 11, 21, 11, 21, 0, 18, K('rock_granite'))
    for z in range(0, 18, 6):
        box(v, 11, 21, 11, 21, z, z + 1, K('rock_granite_d'))
    box(v, 10, 22, 10, 22, 18, 20, K('rock_granite_l'))
    box(v, 12, 20, 12, 20, 18, 22, K('rock_basalt'))
    box(v, 13, 19, 13, 19, 19, 22, K('soot'))
    return one(v)


def gate_pier_telb(name):
    """敷地の門。花崗岩の門柱 2 本（頂に壺）と開いた鉄の門扉。
    通れる幅は `arch_stone` と同じ（x 6..26 が開口。看板は別体）。
    **第 3 回の直しで塀（高さ 40）より頭ひとつ高く**（48。§9.11 #3。通れる幅は
    変えない——開口の x 6..26・門扉の高さは変えていない）。
    """
    v = vol(h=48)
    for x0, x1 in ((0, 6), (26, 32)):
        box(v, x0, x1, 13, 19, 0, 41, K('rock_granite'))
        for z in range(0, 41, 8):
            box(v, x0, x1, 13, 19, z, z + 1, K('rock_granite_d'))
        box(v, x0, x1, 13, 19, 41, 44, K('rock_granite_l'))   # 笠石
        cx = (x0 + x1) // 2
        box(v, cx - 2, cx + 2, 14, 18, 44, 48, K('gold_gilt'))  # 壺（簡略）
    # 開いた鉄の門扉（両脇の柱に沿わせて畳んである）。
    for x0, colour in ((6, K('iron_black')), (24, K('iron_black'))):
        box(v, x0, x0 + 2, 13, 19, 2, 30, colour)
        for z in range(6, 28, 6):
            box(v, x0, x0 + 2, 13, 19, z, z + 1, K('iron_black'))
    return one(v)


def gate_pier_telb_ew(name):
    return G['rotate_parts_90'](gate_pier_telb(name))


def lamp_post_telb(name):
    """街灯。花崗岩の台（8×8×8。上に面取り）に鉄の柱（3×3・高さ 56）と四角いランタン
    （7×7×9。鉄の枠と 4 面ガラス）。**第 2 回の直し**（§9.9 #4 で決めた「街灯が
    細く、夜に灯りが見えない」）——台と柱と傘を大きくし、ガラスは `glass_amber`
    （明るい暖色。`iron_` ではなく `glass_` で登録——冒頭の `_palette` 参照）にした。
    """
    v = vol(h=80)
    # 花崗岩の台（8×8×8）。上端を面取り（四隅を 1 段落とす）。
    box(v, 12, 20, 12, 20, 0, 8, K('rock_granite'))
    box(v, 12, 20, 12, 20, 7, 8, K('rock_granite_l'))
    for cx, cy in ((12, 12), (19, 12), (12, 19), (19, 19)):
        v[cx, cy, 7] = 0
    # 鉄の柱（3×3・高さ 56。z 8..64）。根本に飾り台、頂に腕。
    box(v, 15, 18, 15, 18, 8, 64, K('iron_black'))
    box(v, 14, 19, 14, 19, 8, 10, K('iron_black'))
    box(v, 12, 20, 12, 20, 62, 64, K('iron_black'))   # 腕（ランタン受け）
    # ランタン（7×7×9。z 64..73）。四隅に鉄柱、4 面に硝子、上下に笠。
    z0, z1 = 64, 73
    box(v, 12, 21, 12, 21, z0 - 1, z0, K('iron_black'))
    for cx, cy in ((13, 13), (19, 13), (13, 19), (19, 19)):
        box(v, cx, cx + 1, cy, cy + 1, z0, z1, K('iron_black'))
    box(v, 14, 19, 13, 14, z0 + 1, z1 - 1, K('glass_amber'))  # 北面
    box(v, 14, 19, 19, 20, z0 + 1, z1 - 1, K('glass_amber'))  # 南面
    box(v, 13, 14, 14, 19, z0 + 1, z1 - 1, K('glass_amber'))  # 西面
    box(v, 19, 20, 14, 19, z0 + 1, z1 - 1, K('glass_amber'))  # 東面
    box(v, 12, 21, 12, 21, z1, z1 + 1, K('iron_black'))
    return one(v)


def lamp_flame_telb(name):
    """街灯の火（別体。夜だけ置く側が自発光を足す）。**5×5×7 に拡げた**
    （§9.9 #4 で決めた——自発光の強さは置く側の固定値なので、体積と色の明るさで稼ぐ）。
    """
    v = vol(h=80)
    box(v, 14, 19, 14, 19, 65, 72, K('lit'))
    return one(v)


# ------------------------------------------------------------------ 街路と地面
# **第 2 回で作り直した**（決めたこと §9.9 #1「地面が白っぽく軽い。歩道の敷石に
# 放射（星形）の模様があって目に忙しい」）。8/16 幅を段違いに組む「破れ目地」
# （running bond）は縦横そろいの格子を作らない——格子を斜めから見たときに生じる
# 網目（星形）の錯視は、格子そのものをやめることで消える。
def _bond_pavers(rng, tones, weights, joint_colour):
    """大判の敷石を 8×8 と 8×16 で段違いに組んだ層を返す（歩道・広場・前庭で共通）。
    目地は 1 ボクセル（外周を `joint_colour` のまま残すことで作る）。
    """
    layer = np.full((V, V), joint_colour, np.int16)
    y = 0
    row_i = 0
    while y < V:
        y1 = min(V, y + 8)
        if (row_i % 2) == 0:
            xs, w = (0, 16), 16
        else:
            xs, w = (0, 8, 16, 24), 8
        for x in xs:
            x1 = min(V, x + w)
            layer[x + 1:x1, y + 1:y1] = pick(rng, tones, weights)
        y = y1
        row_i += 1
    return layer


def _sett_tile(rng, tones, weights, joint_colour):
    """車道の小舗石（小さな塊。歩道の大判とはっきり大きさを分ける）。目地 1 ボクセル。"""
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, joint_colour)
    layer = np.zeros((V, V), np.int16)
    p = 0
    while p < V:
        w = int(rng.integers(3, 6))
        layer[p:min(V, p + w), :] = pick(rng, tones, weights)
        p += w + 1
    q = 0
    while q < V:
        w = int(rng.integers(3, 6))
        joint = layer[:, min(V - 1, q + w)]
        joint[:] = joint_colour
        q += w + 1
    return v, layer


def ground_path_telb(name):
    """車道。小さな石畳（炭のような暗い灰。目地 1 ボクセル・3 階調）。
    放射の敷き方はやめ、`ground_path_x_telb`（交差点）もこの平らな小舗石を使う。
    """
    rng = rng_for(name)
    v, layer = _sett_tile(rng, [K('rock_road_d'), K('rock_road'), K('rock_road_l')],
                          [0.3, 0.4, 0.3], K('rock_road_d'))
    v[:, :, 1] = layer
    return ground(v, 2)


def ground_path_ew_telb(name):
    """向きのある車道（東西に通る）。**両縁の雨落ちはやめ**、道の中央に 1 本だけ
    継ぎ目なく通る排水の溝（§9.9 #1 で決めた）。中心の y はどの変種・どの
    マスでも同じなので、隣のマスと切れ目なくつながる。
    """
    rng = rng_for(name)
    v, layer = _sett_tile(rng, [K('rock_road_d'), K('rock_road'), K('rock_road_l')],
                          [0.3, 0.4, 0.3], K('rock_road_d'))
    cy = V // 2
    layer[:, cy - 1:cy + 1] = K('iron_black')
    v[:, :, 1] = layer
    return ground(v, 2)


def ground_path_ns_telb(name):
    """向きのある車道（南北に通る）。中央に縦の排水の溝を 1 本だけ通す。"""
    rng = rng_for(name)
    v, layer = _sett_tile(rng, [K('rock_road_d'), K('rock_road'), K('rock_road_l')],
                          [0.3, 0.4, 0.3], K('rock_road_d'))
    cx = V // 2
    layer[cx - 1:cx + 1, :] = K('iron_black')
    v[:, :, 1] = layer
    return ground(v, 2)


def ground_path_x_telb(name):
    """交差点。**放射の敷き方をやめ**、平らな小舗石にする（§9.9 #1 で決めた）。
    中央の溝は交わりで不自然になるので置かない——`ground_path_telb` と同じ絵。
    """
    return ground_path_telb(name)


def ground_turf_telb(name):
    """歩道。大判の敷石（8×8 と 8×16 を段違いに）。**放射（星形）の模様はやめ**、
    中間の灰 3 階調だけにする（§9.9 #1 で決めた）。
    """
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('rock_basalt'))
    layer = _bond_pavers(rng, [K('rock_pave_d'), K('rock_pave'), K('rock_pave_l')],
                         [0.22, 0.46, 0.32], K('rock_basalt'))
    v[:, :, 1] = layer
    return ground(v, 2)


def kerb_telb(name):
    """縁石（`path_edge`）。玄武岩の縁石と雨落ちの筋。**別体の小物**として置かれる
    （置く側は地面を差し替えず、`grass_edge_out` と同じ流儀で物として重ねる）ので、
    原点は z=0（地面の上に乗る）。回転せずに辺へ寄せて置くので、4 辺すべてに帯を持つ。"""
    v = vol(h=4)
    for a0, a1 in ((0, 5), (V - 5, V)):
        box(v, a0, a1, 0, V, 0, 3, K('rock_basalt_l'))
        box(v, 0, V, a0, a1, 0, 3, K('rock_basalt_l'))
    for a in (2, V - 3):
        box(v, a, a + 1, 0, V, 3, 4, K('iron_black'))  # 雨落ちの鉄格子（縁の筋）
        box(v, 0, V, a, a + 1, 3, 4, K('iron_black'))
    return one(v)


def ground_plaza_telb(name):
    """広場の敷石。歩道と同じ大判の敷石だが**半段明るい**（平均 0x8A8E92）。対角の
    暗い帯を**大きく 1 本だけ**（§9.9 #2 で決めた「縁石が広場の中に格子を描く」
    とは別の話——こちらは #1「対角の帯が多くて忙しい」）。帯はどの変種でも同じ
    位置（タイル座標の対角線）に置くので、並べたマスをまたいで続いて見える。
    """
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('rock_basalt'))
    layer = _bond_pavers(rng, [K('rock_pave'), K('rock_pave_l')], [0.35, 0.65], K('rock_basalt'))
    xs_idx = np.arange(V)[:, None]
    ys_idx = np.arange(V)[None, :]
    band = np.abs(xs_idx - ys_idx) < 8
    layer[band] = K('rock_pave_d')
    v[:, :, 1] = layer
    return ground(v, 2)


def ground_avenue_telb(name):
    """**大通りの敷石**（§9.13。2 本の南北の大通り x33〜37・x47〜51）。歩道と同じ大判の敷石に、
    **南北に走る暗い帯**（幅 8。タイル座標の中央）を通す。帯はどの変種でも同じ位置なので、
    並べたマスをまたいで 1 本の線になり、城へ向かう行列の道に読める。"""
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('rock_basalt'))
    layer = _bond_pavers(rng, [K('rock_pave'), K('rock_pave_l')], [0.4, 0.6], K('rock_basalt'))
    layer[12:20, :] = K('rock_pave_d')
    layer[15:17, :] = K('rock_basalt_l')
    v[:, :, 1] = layer
    return ground(v, 2)


def sign_base_telb(name):
    """**看板の台**（§9.13。`sign_base`）。塀（高さ 40）で吊り看板の板が隠れるので、看板の足の
    下に花崗岩の角柱（高さ 20）を置いて板を塀より上へ出す。看板の足は東寄り・奥（x 24〜30・
    y 11〜17）なので、柱もそこに合わせる。笠石は 1 段張り出す。"""
    v = vol(h=20)
    box(v, 21, 33, 8, 20, 0, 18, K('rock_granite_d'))
    box(v, 22, 32, 9, 19, 2, 18, K('rock_granite'))
    for z in (6, 12):
        box(v, 21, 33, 8, 20, z, z + 1, K('rock_granite_d'))
    box(v, 20, 34, 7, 21, 18, 20, K('rock_granite_l'))
    return one(v)


def ground_gravel_telb(name):
    """整形庭園の砂利。淡い灰。縁に石の見切り。"""
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('rock_basalt'))
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('rock_granite_l'), K('rock_granite'), K('rock_basalt_l')],
                [0.5, 0.32, 0.18])
    for a in (0, V - 1):
        layer[a, :] = K('rock_basalt')
        layer[:, a] = K('rock_basalt')
    v[:, :, 1] = layer
    return ground(v, 2)


def ground_parade_telb(name):
    """練兵場。踏み固めた灰の砂と白線。"""
    rng = rng_for(name)
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, K('rock_basalt'))
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('rock_basalt_l'), K('rock_granite_d'), K('rock_granite')],
                [0.5, 0.28, 0.22])
    if int(name[-2:]) % 2 == 0:
        layer[V // 2 - 1:V // 2 + 1, :] = K('rock_line')
    else:
        layer[:, V // 2 - 1:V // 2 + 1] = K('rock_line')
    v[:, :, 1] = layer
    return ground(v, 2)


# ------------------------------------------------------------------ 街路樹と装飾
def street_tree_telb(name):
    """街路樹。まっすぐな暗い幹（3×3・高さ 18）に、幅 18・高さ 22 の卵形の樹冠
    （下が少しすぼまる）。根元に 12×12 の鉄の木枠。全体の高さ 40 前後（1.25 マス）。
    **第 2 回で大きく作り直した**（§9.9 #3 で決めた「街路樹が小さすぎて棒付きの
    飴に見える」）。緑は 2 階調——暗い緑の塊に、上面と南面（+y）だけ明るい緑を差す。
    """
    v = vol(h=48)
    box(v, 10, 22, 10, 22, 0, 1, K('iron_black'))     # ツリーグレート（12×12）
    box(v, 15, 18, 15, 18, 1, 19, K('timber_d'))       # 幹（3×3・高さ 18）
    box(v, 16, 17, 16, 17, 1, 19, K('timber'))
    # 樹冠——下の球を細く、上の球を太く重ねて「下が少しすぼまる」卵形にする。
    ball(v, 16, 16, 24, 6.0, K('leaf_clipped_d'), squash=0.85)
    ball(v, 16, 16, 31, 9.0, K('leaf_clipped_d'), squash=1.15)
    # 上面と南面だけ明るい緑（暗い緑の塊に対して 2 階調）。
    dark_mask = (v == K('leaf_clipped_d'))
    light_mask = np.zeros_like(dark_mask)
    light_mask[:, 16:, :] = True                       # 南面（+y）
    occ_z = np.where(v[:, :, 19:].any(axis=(0, 1)))[0]
    if occ_z.size > 0:
        top_start = 19 + max(0, int(occ_z.max()) - 3)
        light_mask[:, :, top_start:] = True             # 上面
    v[dark_mask & light_mask] = K('leaf_clipped')
    return one(v)


def monument_telb(name):
    """記念柱。3 段の壇に花崗岩の柱と頂の金の像。高さ 4 マス。"""
    v = vol(h=128)
    box(v, 9, 23, 9, 23, 0, 4, K('rock_granite_l'))
    box(v, 11, 21, 11, 21, 4, 7, K('rock_granite'))
    box(v, 13, 19, 13, 19, 7, 9, K('rock_granite_l'))
    octagon(v, 16, 16, 4.0, 9, 110, K('rock_granite'))
    for z in range(9, 110, 16):
        box(v, 0, V, 0, V, z, z + 1, K('rock_granite_d'))
    ball(v, 16, 16, 116, 6.0, K('gold_gilt'), squash=1.4)
    box(v, 14, 18, 14, 18, 110, 116, K('gold_gilt'))
    hollow(v, depth=6)
    return one(v)


def statue_telb(name):
    """騎士の像。台座に立つ甲冑の騎士と盾。高さ 2.5 マス。"""
    v = vol(h=80)
    box(v, 9, 23, 9, 23, 0, 6, K('rock_granite'))
    box(v, 9, 23, 9, 23, 5, 6, K('rock_granite_l'))
    box(v, 12, 20, 12, 20, 6, 44, K('rock_granite_l'))    # 胴（甲冑。石像として彫る）
    box(v, 11, 21, 11, 21, 6, 10, K('rock_granite'))       # 沓
    box(v, 13, 19, 13, 19, 44, 52, K('rock_granite'))      # 頭と兜
    box(v, 9, 13, 20, 24, 10, 40, K('rock_granite_d'))     # 盾（西向き）
    box(v, 19, 21, 10, 34, 16, 44, K('iron_black'))        # 剣
    return one(v)


def topiary_telb(name, cone=False):
    """刈り込みの木（円錐と球。石の鉢）。"""
    v = vol(h=48)
    box(v, 11, 21, 11, 21, 0, 6, K('rock_granite'))
    box(v, 12, 20, 12, 20, 6, 7, K('rock_granite_l'))
    if cone:
        for k in range(28):
            r = max(0.5, 8.0 - (k * 0.28))
            disc(v, 16, 16, r, 7 + k, 8 + k, K('leaf_clipped') if (k % 4 < 3) else K('leaf_clipped_d'))
    else:
        ball(v, 16, 16, 22, 9.0, K('leaf_clipped'))
        ball(v, 16, 16, 30, 6.5, K('leaf_clipped_d'), squash=1.3)
    return one(v)


def topiary_telb_01(name):
    return topiary_telb(name, cone=True)


def topiary_telb_02(name):
    return topiary_telb(name, cone=False)


def urn_telb(name):
    """石の壺（台座つき）。"""
    v = vol(h=24)
    box(v, 12, 20, 12, 20, 0, 3, K('rock_granite'))
    disc(v, 16, 16, 3.0, 3, 6, K('rock_granite_l'))
    disc(v, 16, 16, 4.2, 6, 14, K('rock_granite'), hollow=3.2)
    disc(v, 16, 16, 3.4, 14, 17, K('rock_granite_l'))
    return one(v)


def tomb_telb(name, slab=True):
    """墓地の石棺と石の墓標。"""
    v = vol(h=32)
    if slab:
        box(v, 6, 26, 8, 24, 0, 8, K('rock_granite'))
        box(v, 6, 26, 8, 24, 7, 8, K('rock_granite_l'))
        box(v, 8, 24, 10, 22, 6, 7, K('rock_granite_d'))
    else:
        box(v, 12, 20, 13, 19, 0, 3, K('rock_granite_l'))
        box(v, 13, 19, 14, 18, 3, 20, K('rock_granite'))
        box(v, 13, 19, 14, 18, 19, 20, K('rock_granite_l'))
    return one(v)


def tomb_telb_01(name):
    return tomb_telb(name, slab=True)


def tomb_telb_02(name):
    return tomb_telb(name, slab=False)


def stone_bench_telb(name):
    """石の腰掛け。"""
    v = vol(h=14)
    box(v, 4, 28, 12, 20, 8, 11, K('rock_granite_l'))
    box(v, 6, 10, 12, 20, 0, 8, K('rock_granite'))
    box(v, 22, 26, 12, 20, 0, 8, K('rock_granite'))
    return one(v)


def balustrade_telb(name):
    """石の欄干（区画の `rim`）。高さ 14 以下。"""
    side = _slice(name)
    v = vol(h=13)
    th = 4
    lo = 0 if side in ('n', 'w') else V - th
    hi = lo + th

    def put(a0, a1, z0, z1, colour):
        if side in ('n', 's'):
            box(v, a0, a1, lo, hi, z0, z1, colour)
        else:
            box(v, lo, hi, a0, a1, z0, z1, colour)

    put(0, V, 0, 2, K('rock_granite'))
    put(0, V, 11, 13, K('rock_granite_l'))
    for p in range(1, V, 6):
        put(p, p + 2, 2, 11, K('rock_granite'))
    return one(v)


def hedge_telb(name):
    """刈り込んだ生垣（区画の `rim`）。角ばった箱形。高さ 14 以下。"""
    side = _slice(name)
    v = vol(h=12)
    th = 5
    lo = 0 if side in ('n', 'w') else V - th
    hi = lo + th
    if side in ('n', 's'):
        box(v, 0, V, lo, hi, 0, 12, K('leaf_clipped'))
        box(v, 0, V, lo, hi, 11, 12, K('leaf_clipped_d'))
    else:
        box(v, lo, hi, 0, V, 0, 12, K('leaf_clipped'))
        box(v, lo, hi, 0, V, 11, 12, K('leaf_clipped_d'))
    return one(v)


def flag_pole_telb(name):
    """旗竿（王の紋。風で揺れる）。**1 個の中に `frame`（静止）と `flag`（風）を持つ**
    （`telmora_prefabs.py` の `gatehouse_tel` と同じ流儀。`wind_parts(['flag'])` で宣言する）。"""
    frame = vol(h=96)
    box(frame, 14, 18, 14, 18, 0, 4, K('rock_granite'))
    box(frame, 15, 17, 15, 17, 4, 90, K('iron_black'))
    box(frame, 14, 18, 14, 18, 88, 90, K('gold_gilt'))
    flag = vol(h=96)
    box(flag, 15, 17, 78, 90, 60, 84, K('cloth_tb_r'))
    box(flag, 15, 17, 82, 90, 66, 78, K('gold_gilt'))  # 王の紋（簡略の金地）
    return [('frame', frame, (0, 0, 0)), ('flag', flag, (0, 0, 0))]


def banner_telb(name):
    """**王の紋の旗**（大通りの 2 列。2026-09-06 に決めた「整然と並ぶ大量の banner」）。

    鉄の柱の頂に横木を渡し、そこから深紅の旗を**垂らす**（幟ではなく紋章旗）。旗は
    金の縁と金の王冠、裾は燕尾。通りは南北に走るので、横木は東西に渡して旗の面を
    カメラ（南）へ向ける。`frame`（静止）と `flag`（風）の 2 部品。
    """
    frame = vol(h=96)
    box(frame, 12, 20, 12, 20, 0, 3, K('rock_granite_d'))      # 台
    box(frame, 13, 19, 13, 19, 3, 6, K('rock_granite'))
    box(frame, 15, 17, 15, 17, 6, 92, K('iron_black'))         # 柱
    box(frame, 6, 26, 15, 17, 86, 88, K('iron_black'))         # 横木（東西）
    box(frame, 6, 7, 15, 17, 84, 90, K('gold_gilt'))           # 横木の両端の金具
    box(frame, 25, 26, 15, 17, 84, 90, K('gold_gilt'))
    box(frame, 14, 18, 14, 18, 92, 95, K('gold_gilt'))         # 頂の飾り
    flag = vol(h=96)
    box(flag, 7, 25, 15, 17, 40, 86, K('cloth_tb_r'))          # 旗（幅 18・丈 46）
    box(flag, 7, 8, 15, 17, 40, 86, K('gold_gilt'))            # 金の縁
    box(flag, 24, 25, 15, 17, 40, 86, K('gold_gilt'))
    box(flag, 7, 25, 15, 17, 84, 86, K('gold_gilt'))
    for i in range(6):                                         # 裾の燕尾（中央を V に切る）
        box(flag, 12 + i, 20 - i, 15, 17, 40 + i, 41 + i, 0)
    box(flag, 7, 25, 15, 17, 46, 47, K('gold_gilt'))           # 裾の金の帯
    box(flag, 12, 20, 15, 17, 54, 58, K('gold_gilt'))          # 王冠の下の帯
    box(flag, 11, 21, 15, 17, 58, 62, K('gold_gilt'))          # 王冠の台
    for x in (11, 15, 19):                                     # 王冠の 3 つの尖り
        box(flag, x, x + 2, 15, 17, 62, 67, K('gold_gilt'))
    return [('frame', frame, (0, 0, 0)), ('flag', flag, (0, 0, 0))]


# ------------------------------------------------------------------ 登録
def register(g):
    """`gen_prefabs.py` の名前空間 `g` へテルモラ「秩序と威厳」の素材を足す。

    **`TOWN_STYLES` に `'telb'` を足す**——`_slice_of` が名前から意匠を切り出せないと、
    `house_wall_nw_telb` のスライスが `'telb'` に化けて 9 スライスが全部同じ形になる
    （`angwil_prefabs.py` の register と同じ理由）。
    """
    global G, C, V
    G = g
    C = g['C']
    V = g['V']
    g['TOWN_STYLES'] = tuple(g['TOWN_STYLES']) + ('telb',)
    _palette()
    cat = g['CATALOG']
    static = g['static_part']

    for sl in g['HOUSE_SLICES']:
        cat['house_wall_%s_telb' % sl] = (house_wall_telb, static(), '石造りの壁 1 層（花崗岩・%s）' % sl)
        cat['house_roof_%s_telb' % sl] = (house_roof_telb, static(), 'スレートの寄棟（%s）' % sl)
    cat['house_light_telb'] = (house_light_telb, static(), '窓の灯り（南面の窓と同じ座標）')
    cat['house_entrance_telb'] = (house_entrance_telb, static(), '石のアーチとペディメントの玄関')
    for side in ('n', 's', 'w', 'e'):
        cat['fence_%s_telb' % side] = (fence_telb, static(), '花崗岩の礎に鉄柵（%s 側）' % side)
    cat['watchtower_telb'] = (watchtower_telb, static(), '四隅の石塔（マチコレーションと円錐屋根）')
    cat['yard_ground_telb'] = (yard_ground_telb, static(), '前庭（貴族の庭園。生垣の枠と花壇）')
    cat['fountain_basin_telb'] = (fountain_basin_telb, static(), '噴水盤（円い石の水盤。水面は静止）')
    cat['chimney_telb'] = (chimney_telb, static(), '屋根の煙突（roof_vent）')
    cat['gate_pier_telb'] = (gate_pier_telb, static(), '敷地の門（南北にくぐる。花崗岩の門柱と開いた鉄扉）')
    cat['gate_pier_telb_ew'] = (gate_pier_telb_ew, static(), '敷地の門（東西にくぐる）')
    cat['lamp_post_telb'] = (lamp_post_telb, static(), '街灯（花崗岩の台に鉄の柱）')
    cat['lamp_flame_telb'] = (lamp_flame_telb, static(), '街灯の火（別体。夜だけ）')

    for i in range(1, 6):
        cat['ground_path_telb_%02d' % i] = (ground_path_telb, static(), '車道（玄武岩の小舗石）')
        cat['ground_turf_telb_%02d' % i] = (ground_turf_telb, static(), '歩道（花崗岩の大判の敷石）')
    for i in range(1, 4):
        cat['ground_path_ew_telb_%02d' % i] = (ground_path_ew_telb, static(), '車道（東西。両縁に雨落ち）')
        cat['ground_path_ns_telb_%02d' % i] = (ground_path_ns_telb, static(), '車道（南北。両縁に雨落ち）')
    for i in range(1, 3):
        cat['ground_path_x_telb_%02d' % i] = (ground_path_x_telb, static(), '交差点（放射の敷き方）')
        cat['ground_gravel_telb_%02d' % i] = (ground_gravel_telb, static(), '整形庭園の砂利')
        cat['ground_parade_telb_%02d' % i] = (ground_parade_telb, static(), '練兵場の砂と白線')
        cat['topiary_telb_%02d' % i] = ((topiary_telb_01 if i == 1 else topiary_telb_02),
                                         static(), '刈り込みの木（%s）' % ('円錐' if i == 1 else '球'))
        cat['tomb_telb_%02d' % i] = ((tomb_telb_01 if i == 1 else tomb_telb_02),
                                      static(), '墓地の石（%s）' % ('石棺' if i == 1 else '墓標'))
    for i in range(1, 4):
        cat['ground_plaza_telb_%02d' % i] = (ground_plaza_telb, static(), '広場の大判の敷石')
    for i in range(1, 3):
        cat['ground_avenue_telb_%02d' % i] = (ground_avenue_telb, static(), '大通りの敷石（南北の暗い帯）')
    for side in ('n', 's', 'w', 'e'):
        cat['balustrade_telb_%s' % side] = (balustrade_telb, static(), '石の欄干（%s 側）' % side)
        cat['hedge_telb_%s' % side] = (hedge_telb, static(), '刈り込んだ生垣（%s 側）' % side)

    #! 静止（設計に無い動きは足さない）。「整然と並ぶ」街路樹は微動もしないほうが秩序に見える。
    cat['street_tree_telb'] = (street_tree_telb, static(), '街路樹（刈り込んだ卵形の樹冠。1 種類）')
    cat['kerb_telb'] = (kerb_telb, static(), '縁石（path_edge）')
    cat['sign_base_telb'] = (sign_base_telb, static(), '看板の台（塀より上へ看板を出す）')
    cat['monument_telb'] = (monument_telb, static(), '記念柱（花崗岩の柱と頂の金の像）')
    cat['statue_telb'] = (statue_telb, static(), '騎士の像')
    cat['urn_telb'] = (urn_telb, static(), '石の壺')
    cat['stone_bench_telb'] = (stone_bench_telb, static(), '石の腰掛け')
    cat['flag_pole_telb'] = (flag_pole_telb, wind_parts(['flag'], 0.6), '旗竿（王の紋。旗は風）')
    cat['banner_telb'] = (banner_telb, wind_parts(['flag'], 0.5), '王の紋の旗（大通りの 2 列。旗は風）')

    # 石積みの塀（palisade_telb）は既存の 'tel' 用ラムダに倣い、base の `palisade()` と
    # 同じ高さ（46＝palisade_tel と同じ）を持つ独立の実装で上書きする。
    def _palisade_telb(name):
        rng = rng_for(name)
        height, course = 46, 8
        v = vol(h=height)
        v[:, :, :] = K('rock_basalt')
        shell = v
        for z in range(course - 1, height, course):
            z0 = max(0, z - course + 1)
            shade = pick(rng, [K('rock_granite'), K('rock_granite_l'), K('rock_granite_d')],
                        [0.5, 0.26, 0.24])
            box(shell, 0, V, 0, V, z0, z + 1, shade)
        for side in ('n', 's', 'w', 'e'):
            fx, fy = _face_slice(0, V, 0, V, side)
            for z in range(0, height, 10):
                shell[fx, fy, z:z + 1] = K('rock_granite_d')
        # 裾の傾斜（バッター）——最下段だけ厚みを持たせて明るく。
        box(shell, 0, V, 0, V, 0, 6, K('rock_basalt_l'))
        # 胸壁の狭間（天端）。
        for x in range(0, V, 6):
            box(shell, x, x + 2, 0, V, height - 3, height, 0)
        return one(shell)

    for i in range(1, 4):
        cat['palisade_telb_%02d' % i] = (_palisade_telb, static(), '石積みの城壁（大きな切石・胸壁の狭間）')
