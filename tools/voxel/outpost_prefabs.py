# -*- coding: utf-8 -*-
"""辺境の地（Outpost）の作り替え — 構造物の生成器。

`gen_prefabs.py` から `register(globals())` で呼ばれ、接尾辞 `_out` / `_outb` の
プレハブを `CATALOG` へ足す。**既存の素材には 1 バイトも触らない**（別の名前で足すだけ）。

## 作りの規律（`gen_prefabs.py` 冒頭と同じ）
- 無作為な表面ノイズは入れない。彫りは 1 ボクセルの深さ。色は塊ごとに 1 色
- 接地層（z=0）の平面方向は縮めない。+y は南＝カメラ側。戸口は手前が y 大
- 柵の高さ（14）は変えない（遮蔽の約束）
- 色は共通の登録簿（`C`）を使い、足りない色だけ `reg_local`（名前に材質の断片を含める）

小物は `outpost_props.py`、目印の建物（穀物蔵・水車小屋）は `outpost_landmarks.py`。
"""
from __future__ import annotations

import importlib.util
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))

G = None   #: gen_prefabs の名前空間（register で入る）
C = None   #: 色の名前 → 索引
V = 32


# ------------------------------------------------------------------ 形の道具
def vol(w=None, d=None, h=32):
    """int16 の空の体積（`reg_local` の 1000 番台が入る）。"""
    return np.zeros((w or V, d or V, h), np.int16)


def box(v, x0, x1, y0, y1, z0, z1, colour):
    """直方体を塗る（範囲は配列に切り詰める）。"""
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


def post(v, x, y, z0, z1, colour, w=2):
    box(v, x, x + w, y, y + w, z0, z1, colour)


def rng_for(name):
    return G['rng_for'](name)


def pick(rng, shades, weights=None):
    return int(rng.choice(shades, p=weights))


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
    rl = G['reg_local']
    #! 土壁（骨組みの間の塗り）。`plas` より黄土が強い。名前の `plas` で漆喰の材質になる。
    rl('out_plas', (190, 172, 134)); rl('out_plas_l', (208, 192, 156)); rl('out_plas_d', (164, 146, 112))
    #! 灰色に灼けたこけら板（`shin` で木の材質）。
    rl('out_shin_w', (150, 142, 128)); rl('out_shin_wl', (172, 164, 150))
    #! 水晶（`glass` で汚さない材質）。
    rl('out_glass_b', (120, 168, 250)); rl('out_glass_bl', (196, 222, 255))
    #! 黒い鉄（`iron` で金属）。
    rl('out_iron_k', (44, 44, 50))
    #! 布（`cloth` で布の材質）。白いシーツ・赤い旗・藍の布。
    rl('out_cloth_w', (236, 232, 222)); rl('out_cloth_ws', (206, 202, 194))
    rl('out_cloth_r', (170, 52, 44)); rl('out_cloth_rd', (128, 38, 34))
    rl('out_cloth_b', (58, 78, 138)); rl('out_cloth_bd', (40, 56, 104))
    #! 花（`fl` で汚さない）。既存の白・黄・赤に紫と桃を足す。
    rl('out_fl_p', (168, 108, 200)); rl('out_fl_pk', (232, 150, 180))
    #! 野菜の葉（`leaf` は木の葉の描き込みが入るので `verd`＝草の材質で）。
    rl('out_verd_c', (108, 150, 70)); rl('out_verd_cl', (140, 178, 92))
    #! 竜の頭骨（`bone` で漆喰系＝汚しが弱い）。
    rl('out_bone_y', (200, 186, 150))


def K(name):
    return C[name]


# ------------------------------------------------------------------ 建物
HOUSE_WALL_H = 34
HOUSE_ROOF_H = 30
HOUSE_EAVE = 4
INSET = 2
PLINTH = 5


def _slice(name):
    return G['_slice_of'](name)


def _outward(name):
    return G['SLICE_OUT'][_slice(name)]


def _fieldstone_plinth(v, rng, height=PLINTH):
    """野石の土台。**マスいっぱい**（罠 5）。石は横に並べる（縦縞に見せない）。"""
    box(v, 0, V, 0, V, 0, height, K('rub_d'))
    for z in range(1, height - 1):
        row = np.zeros(V, np.int16)
        p = int(rng.integers(0, 3))
        while p < V:
            w = int(rng.integers(4, 9))
            row[p:p + w] = pick(rng, [K('rub'), K('rub_l'), K('rub_d')], [0.5, 0.26, 0.24])
            p += w + 1  # 目地 1 ボクセル（暗い土台の色が覗く）
        v[:, 0, z] = row
        v[:, V - 1, z] = row[::-1]
        v[0, :, z] = row[::-1]
        v[V - 1, :, z] = row
    box(v, 0, V, 0, V, height - 1, height, K('rub_l'))


def house_wall_out(name):
    """**辺境の壁** — 野石の土台、丸太組みの腰、骨組みを見せた土壁の上半分。

    丸太は 4 段（1 段 4 ボクセル）。段の境に 1 ボクセルの溝を彫るので、面が平らでも
    「積んだ丸太」に読める。角のスライスでは丸太の木口が**交互に**突き出す（校倉の角）。
    上半分は土壁に柱と筋交い。窓は南面だけ、鎧戸と花箱つき。
    """
    north, south, west, east = _outward(name)
    rng = rng_for(name)
    v = vol(h=HOUSE_WALL_H)
    _fieldstone_plinth(v, rng)
    x0 = INSET if west else 0
    x1 = V - INSET if east else V
    y0 = INSET if north else 0
    y1 = V - INSET if south else V
    log_top = 21
    # 丸太の腰（4 段）。
    shades = [K('log'), K('log_l'), K('log_d')]
    for i, z in enumerate(range(PLINTH, log_top, 4)):
        box(v, x0, x1, y0, y1, z, z + 4, shades[(i + int(rng.integers(0, 3))) % 3])
        box(v, x0, x1, y0, y1, z, z + 1, K('log_d'))   # 段の境（下 1 列を暗く）
    faces = []
    if north:
        faces.append('n')
    if south:
        faces.append('s')
    if west:
        faces.append('w')
    if east:
        faces.append('e')

    def face(side):
        if side == 'n':
            return (slice(x0, x1), slice(y0, y0 + 1))
        if side == 's':
            return (slice(x0, x1), slice(y1 - 1, y1))
        if side == 'w':
            return (slice(x0, x0 + 1), slice(y0, y1))
        return (slice(x1 - 1, x1), slice(y0, y1))

    # 段の境を**溝**にする（外に面した面だけ。1 ボクセル深さ）。
    for side in faces:
        fx, fy = face(side)
        for z in range(PLINTH + 4, log_top, 4):
            v[fx, fy, z:z + 1] = 0
    # 校倉の角（木口）。外に面した 2 辺が交わる角だけ。
    corners = [(west, north, 'x0y0'), (east, north, 'x1y0'), (west, south, 'x0y1'), (east, south, 'x1y1')]
    for a, b, key in corners:
        if not (a and b):
            continue
        cx = 0 if 'x0' in key else V - INSET
        cy = 0 if 'y0' in key else V - INSET
        for i, z in enumerate(range(PLINTH, log_top, 4)):
            end = K('log_end')
            if i % 2 == 0:
                # x 方向に走る丸太の木口が y の面から突き出す
                box(v, cx, cx + INSET, (y0 if 'y0' in key else y1 - 5), (y0 + 5 if 'y0' in key else y1),
                    z + 1, z + 4, end)
            else:
                box(v, (x0 if 'x0' in key else x1 - 5), (x0 + 5 if 'x0' in key else x1), cy, cy + INSET,
                    z + 1, z + 4, end)
    # 土壁（上半分）。むらは塊で。
    box(v, x0, x1, y0, y1, log_top, HOUSE_WALL_H, K('out_plas'))
    for _ in range(4):
        bx = int(rng.integers(x0, max(x0 + 1, x1 - 6)))
        bz = int(rng.integers(log_top + 1, HOUSE_WALL_H - 6))
        shade = K('out_plas_l') if rng.random() < 0.5 else K('out_plas_d')
        block = v[bx:bx + 6, y0:y1, bz:bz + 4]
        block[block == K('out_plas')] = shade
    # 骨組み（外に面した面へ描く。面の中なので三角形は増えない）。
    for side in faces:
        fx, fy = face(side)
        v[fx, fy, log_top:log_top + 2] = K('timber_d')           # 台輪
        v[fx, fy, HOUSE_WALL_H - 3:HOUSE_WALL_H] = K('timber')   # 軒桁
        span = fx if side in ('n', 's') else fy
        lo, hi = span.start, span.stop
        posts = list(range(lo + 2, hi - 2, 10))
        for p in posts:
            if side in ('n', 's'):
                v[p:p + 2, fy, log_top:HOUSE_WALL_H - 3] = K('timber')
            else:
                v[fx, p:p + 2, log_top:HOUSE_WALL_H - 3] = K('timber')
        # 筋交い（柱の間に 1 本。向きは交互）
        top = HOUSE_WALL_H - 3
        for k, (a, b) in enumerate(zip(posts, posts[1:])):
            run = max(1, b - a)
            rise = top - (log_top + 2)
            up = (k % 2) == 0
            for t in range(run):
                z = log_top + 2 + int(round(rise * ((t / run) if up else (1.0 - (t / run)))))
                pos = a + t
                zz0, zz1 = max(log_top + 2, z - 1), min(top, z + 1)
                if zz0 >= zz1:
                    continue
                if side in ('n', 's'):
                    v[pos:pos + 1, fy, zz0:zz1] = K('timber_l')
                else:
                    v[fx, pos:pos + 1, zz0:zz1] = K('timber_l')
    # 窓（南面だけ）。鎧戸を開いて留めた形＋花箱。
    if south:
        wx = ((x0 + x1) // 2) - 4
        wz = log_top + 3
        box(v, wx - 1, wx + 9, y1 - 2, y1, wz - 1, wz + 8, K('timber_l'))    # 枠
        box(v, wx, wx + 8, y1 - 2, y1, wz, wz + 7, K('glass'))
        box(v, wx + 3, wx + 5, y1 - 2, y1, wz, wz + 7, K('timber_d'))        # 中桟
        box(v, wx, wx + 8, y1 - 2, y1, wz + 3, wz + 4, K('timber_d'))
        box(v, wx - 5, wx - 1, y1 - 1, y1, wz - 1, wz + 8, K('board_d'))     # 鎧戸（左右）
        box(v, wx + 9, wx + 13, y1 - 1, y1, wz - 1, wz + 8, K('board_d'))
        box(v, wx - 4, wx - 2, y1 - 1, y1, wz + 1, wz + 6, K('board'))
        box(v, wx + 10, wx + 12, y1 - 1, y1, wz + 1, wz + 6, K('board'))
        # 花箱（窓の下。壁の面から 2 ボクセル手前へ）
        box(v, wx - 1, wx + 9, y1, min(V, y1 + 2), wz - 4, wz - 1, K('timber'))
        box(v, wx, wx + 8, y1, min(V, y1 + 2), wz - 1, wz, K('turf'))
        for fx_ in range(wx, wx + 8, 2):
            v[fx_, min(V - 1, y1), wz] = pick(rng, [K('fl_r'), K('fl_y'), K('out_fl_p'), K('fl_w')])
    return [('main', v, (0, 0, 0))]


def house_wall_outb(name):
    """**納屋・厩の壁** — 野石の土台に縦板張り。窓は無く、腰に横桟。"""
    north, south, west, east = _outward(name)
    rng = rng_for(name)
    v = vol(h=HOUSE_WALL_H)
    _fieldstone_plinth(v, rng)
    x0 = INSET if west else 0
    x1 = V - INSET if east else V
    y0 = INSET if north else 0
    y1 = V - INSET if south else V
    box(v, x0, x1, y0, y1, PLINTH, HOUSE_WALL_H, K('board'))
    faces = [s for s, on in (('n', north), ('s', south), ('w', west), ('e', east)) if on]
    for side in faces:
        if side == 'n':
            fx, fy = slice(x0, x1), slice(y0, y0 + 1)
        elif side == 's':
            fx, fy = slice(x0, x1), slice(y1 - 1, y1)
        elif side == 'w':
            fx, fy = slice(x0, x0 + 1), slice(y0, y1)
        else:
            fx, fy = slice(x1 - 1, x1), slice(y0, y1)
        span = fx if side in ('n', 's') else fy
        lo, hi = span.start, span.stop
        p = lo
        while p < hi:
            w = int(rng.integers(3, 5))
            shade = pick(rng, [K('board'), K('board_l'), K('board_d')], [0.5, 0.25, 0.25])
            if side in ('n', 's'):
                v[p:min(hi, p + w), fy, PLINTH:HOUSE_WALL_H] = shade
                v[min(hi - 1, p + w):min(hi, p + w + 1), fy, PLINTH:HOUSE_WALL_H] = 0  # 板の隙間（溝）
            else:
                v[fx, p:min(hi, p + w), PLINTH:HOUSE_WALL_H] = shade
                v[fx, min(hi - 1, p + w):min(hi, p + w + 1), PLINTH:HOUSE_WALL_H] = 0
            p += w + 1
        v[fx, fy, HOUSE_WALL_H - 3:HOUSE_WALL_H] = K('beam')     # 軒桁
        v[fx, fy, 17:19] = K('beam_l')                           # 腰の横桟
    return [('main', v, (0, 0, 0))]


def house_roof_out(name):
    """**こけら葺きの寄棟** — 割り板を 3 段ずつ、列を半枚ずらして葺く。苔と灰色に灼けた板。"""
    north, south, west, east = _outward(name)
    rng = rng_for(name)
    height, hip = G['_roof_field'](north, south, west, east, HOUSE_EAVE, HOUSE_ROOF_H, 1.0)
    v = vol(h=HOUSE_ROOF_H)
    plank = rng.choice([K('shin'), K('shin_l'), K('shin_d'), K('out_shin_w'), K('out_shin_wl')],
                       size=(V // 3 + 2, V // 3 + 2, HOUSE_ROOF_H // 3 + 1),
                       p=[0.36, 0.22, 0.18, 0.14, 0.10])
    for x in range(V):
        for y in range(V):
            h = int(height[x, y])
            for z in range(h):
                if z < HOUSE_EAVE:
                    v[x, y, z] = K('shin_d')
                else:
                    row = z // 3
                    off = (row % 2) * 2
                    v[x, y, z] = plank[(x + off) // 3, (y + off) // 3, row]
            if (h % 3) == 0:
                v[x, y, h - 1] = K('shin_d')          # 板の重なりの影
            if hip[x, y] and (h < HOUSE_ROOF_H):
                v[x, y, h - 1] = K('ridge')           # 隅棟
            if h >= HOUSE_ROOF_H:
                v[x, y, h - 1] = K('ridge')           # 棟
    # 苔（天面に塊で）。辺境の屋根は湿っているので少し多め。
    for _ in range(int(rng.integers(4, 8))):
        cx, cy = int(rng.integers(0, V - 4)), int(rng.integers(0, V - 4))
        w, d = int(rng.integers(3, 8)), int(rng.integers(2, 6))
        shade = K('moss_d') if rng.random() < 0.5 else K('moss')
        for x in range(cx, min(cx + w, V)):
            for y in range(cy, min(cy + d, V)):
                v[x, y, height[x, y] - 1] = shade
    # 反った板（1 ボクセル持ち上がる）。数枚だけ。
    for _ in range(int(rng.integers(2, 5))):
        cx, cy = int(rng.integers(1, V - 4)), int(rng.integers(1, V - 3))
        for x in range(cx, cx + 3):
            for y in range(cy, cy + 2):
                base = int(height[x, y])
                if base < HOUSE_ROOF_H:
                    v[x, y, base] = K('out_shin_w')
    return [('main', v, (0, 0, 0))]


def house_roof_outb(name):
    """納屋・厩の屋根 — 藁葺き（テルモラの生成器をそのまま借りる。色は表の tint で振る）。"""
    return G['_house_roof_thatch'](name)


def house_entrance_out(name):
    """見た目だけの戸口 — 板戸に鉄の帯と丸い把手。踏み石つき。壁の南面の手前に置く別体。"""
    v = vol(h=32)
    y0, y1 = 29, 31
    box(v, 9, 23, 25, 31, 0, 3, K('rub'))
    box(v, 9, 23, 25, 31, 2, 3, K('rub_l'))
    box(v, 8, 24, y0, y1 + 1, 3, 28, K('timber_d'))          # 枠
    box(v, 10, 22, y1, y1 + 1, 4, 26, K('board'))            # 戸
    for x in range(11, 22, 3):
        box(v, x, x + 1, y1, y1 + 1, 4, 26, K('board_d'))    # 板の継ぎ目
    for z in (8, 20):
        box(v, 10, 22, y1, y1 + 1, z, z + 2, K('iron'))      # 鉄の帯
    box(v, 18, 20, y1, y1 + 1, 14, 16, K('iron_l'))          # 把手
    return [('main', v, (0, 0, 0))]


def house_entrance_outb(name):
    """納屋の両開き戸 — 幅いっぱい、斜めの筋交いを打った板戸。"""
    v = vol(h=32)
    y1 = 31
    box(v, 4, 28, 26, 32, 0, 2, K('pack_d'))
    box(v, 3, 29, 29, 32, 2, 29, K('timber_d'))
    for lx0 in (5, 17):
        box(v, lx0, lx0 + 10, y1, y1 + 1, 3, 27, K('board'))
        for x in range(lx0 + 2, lx0 + 10, 3):
            box(v, x, x + 1, y1, y1 + 1, 3, 27, K('board_d'))
        for t in range(10):                                  # 筋交い（Z 型）
            z = 4 + int(round(t * 2.2))
            box(v, lx0 + t, lx0 + t + 1, y1, y1 + 1, z, z + 2, K('beam'))
        box(v, lx0, lx0 + 10, y1, y1 + 1, 4, 6, K('beam'))
        box(v, lx0, lx0 + 10, y1, y1 + 1, 24, 26, K('beam'))
    box(v, 15, 17, y1, y1 + 1, 3, 27, K('timber_d'))         # 召し合わせ
    return [('main', v, (0, 0, 0))]


def house_light_out(name):
    """窓の灯り（辺境の壁の窓の高さに合わせた別体。置く側が壁の +0.40 マスへ置く）。

    壁の窓は z 24..31（`house_wall_out`）。置く側が 0.40 マス（12.8 ボクセル）持ち上げるので、
    ここでは z 11..19 に置くとちょうど窓に重なる。基の `house_light`（z 1..13）は低すぎた。
    """
    v = vol(h=20)
    box(v, 12, 20, 29, 32, 11, 19, K('lit'))
    return [('main', v, (0, 0, 0))]


def chimney_out(name):
    """石の煙突。屋根の棟の真ん中へ載せる（`roof_vent`）。煙は置く側が蒸気の口へ積む。"""
    rng = rng_for(name)
    v = vol(h=22)
    box(v, 11, 21, 11, 21, 0, 18, K('rub_d'))
    for z in range(0, 18, 3):
        for (x0, x1, y0, y1) in ((11, 21, 11, 12), (11, 21, 20, 21), (11, 12, 11, 21), (20, 21, 11, 21)):
            p = 0
            while p < 10:
                w = int(rng.integers(3, 6))
                shade = pick(rng, [K('rub'), K('rub_l'), K('rub_d')], [0.5, 0.28, 0.22])
                if y1 - y0 == 1:
                    box(v, x0 + p, min(x1, x0 + p + w), y0, y1, z, z + 2, shade)
                else:
                    box(v, x0, x1, y0 + p, min(y1, y0 + p + w), z, z + 2, shade)
                p += w + 1
    box(v, 10, 22, 10, 22, 18, 20, K('rub_l'))               # 笠石
    box(v, 12, 20, 12, 20, 18, 22, K('rub_d'))
    box(v, 13, 19, 13, 19, 19, 22, K('soot'))                # 煙道（煤）
    return [('main', v, (0, 0, 0))]


# ------------------------------------------------------------------ 柵・門
FENCE_H = 14


def fence_out(name):
    """割り木の横桟の柵（split rail）。高さは既存と同じ 14。桟は 1 本ずつ色を変える。"""
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

    for z0, z1 in ((4, 7), (9, 12)):
        p = 0
        while p < V:
            w = int(rng.integers(9, 16))
            shade = pick(rng, [K('oak'), K('oak_l'), K('timber_d')], [0.5, 0.3, 0.2])
            put(p, min(V, p + w), z0 + int(rng.integers(0, 2)), z1, shade)
            p += w
    for p in range(1, V, 10):                                # 柱（少し傾く）
        put(p, p + 3, 0, FENCE_H - 1, K('timber'))
        put(p, p + 3, FENCE_H - 1, FENCE_H, K('timber_l'))
    return [('main', v, (0, 0, 0))]


def arch_out(name):
    """丸太の門（南北にくぐる）— 2 本の丸太の柱に横木、中央に吊りランタン。"""
    v = vol(h=50)
    for cx in (5, 27):
        disc(v, cx, 16, 3.2, 0, 44, K('log'))
        disc(v, cx, 16, 1.6, 0, 44, K('log_d'))
        disc(v, cx, 16, 4.0, 0, 3, K('rub_d'))               # 沓石
    log_x(v, 16, 43, 3.0, 0, V, K('log_l'))                  # 横木
    box(v, 3, 8, 14, 18, 44, 48, K('log_end'))
    box(v, 24, 29, 14, 18, 44, 48, K('log_end'))
    for (a, b) in ((7, 11), (21, 25)):                      # 方杖
        for k in range(5):
            box(v, a + k, a + k + 1, 15, 17, 36 + (k * 1), 38 + (k * 1), K('timber_d'))
            box(v, b - k, b - k + 1, 15, 17, 36 + (k * 1), 38 + (k * 1), K('timber_d'))
    box(v, 15, 17, 15, 17, 34, 40, K('iron'))                # 吊り金具
    box(v, 12, 20, 12, 20, 25, 27, K('out_iron_k'))          # ランタン（下枠）
    box(v, 12, 20, 12, 20, 33, 35, K('out_iron_k'))          # 上枠
    for (x0, x1) in ((12, 14), (18, 20)):
        for (y0, y1) in ((12, 14), (18, 20)):
            box(v, x0, x1, y0, y1, 27, 33, K('out_iron_k'))
    box(v, 11, 21, 11, 21, 35, 36, K('out_iron_k'))          # 笠
    return [('main', v, (0, 0, 0))]


def arch_out_ew(name):
    return G['rotate_parts_90'](arch_out(name))


def arch_flame_out(name):
    """門のランタンの火（夜だけ置く側が自発光をつけて重ねる）。"""
    a = vol(h=50)
    box(a, 14, 18, 14, 18, 27, 32, K('fl_mid'))
    box(a, 15, 17, 15, 17, 29, 33, K('fl_core'))
    return [('main', a, (0, 0, 0))]


def arch_flame_out_ew(name):
    return G['rotate_parts_90'](arch_flame_out(name))


# ------------------------------------------------------------------ 地面
def _tile(h):
    return vol(h=h)



def _relief(layer, height, under):
    """**平面の地面**（凹凸は 2026-09-05 に撤回した。`height` は読まない）。"""
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, under)
    v[:, :, 1] = layer
    return [('main', v, (0, 0, -2))]


def ground_path_out(name):
    """轍と足跡の土の道。踏み固めた土に、二筋の轍と蹄の跡、小石。"""
    rng = rng_for(name)
    v = _tile(2)
    box(v, 0, V, 0, V, 0, 1, K('pack_d'))
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('pack'), K('pack_l'), K('pack_d'), K('dirt')], [0.42, 0.26, 0.22, 0.10])
    # 二筋の轍（幅 2。車輪の間隔 12〜14）。蛇行する。
    x = int(rng.integers(6, 12))
    gap = int(rng.integers(12, 15))
    for y in range(V):
        for w in (x, x + gap):
            layer[max(0, min(V - 2, w)):max(0, min(V - 2, w)) + 2, y] = K('pack_d')
        x += int(rng.integers(-1, 2))
    # 蹄と足の跡（2×2 の暗い塊を疎らに）。
    for _ in range(int(rng.integers(3, 7))):
        px, py = int(rng.integers(0, V - 2)), int(rng.integers(0, V - 2))
        layer[px:px + 2, py:py + 2] = K('dirt_d')
    G['_grain'](rng, layer, layer > 0, [K('grit'), K('pebble')], [0.05, 0.03], block=2)
    #! 轍と足跡は 1 段沈む（2026-09-05 に決めた「凹凸でディテールを表現してもいい」）。
    height = np.full((V, V), 4, np.int16)
    height[np.isin(layer, [K('pack_d'), K('dirt_d')])] = 3
    return _relief(layer, height, K('pack_d'))


def ground_turf_out(name):
    """短い草。土が透け、ところどころ小さな花。"""
    rng = rng_for(name)
    v = _tile(2)
    box(v, 0, V, 0, V, 0, 1, K('pack_d'))
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('turf'), K('turf_d'), K('turf_l'), K('pack')], [0.36, 0.28, 0.20, 0.16])
    for _ in range(int(rng.integers(2, 5))):                 # 土が覗く所（塊）
        x, y = int(rng.integers(0, V - 5)), int(rng.integers(0, V - 5))
        layer[x:x + int(rng.integers(3, 6)), y:y + int(rng.integers(3, 6))] = K('pack')
    if int(name[-2:]) % 2 == 0:
        for _ in range(int(rng.integers(2, 5))):             # 花（2×1 の点）
            x, y = int(rng.integers(0, V - 2)), int(rng.integers(0, V - 1))
            layer[x:x + 2, y] = pick(rng, [K('fl_w'), K('fl_y'), K('out_fl_p')])
    #! 草の株は 1 段盛り、土が覗く所は 1 段沈む。
    height = np.full((V, V), 4, np.int16)
    height[layer == K('pack')] = 3
    for _ in range(int(rng.integers(4, 9))):
        x, y = int(rng.integers(0, V - 2)), int(rng.integers(0, V - 2))
        if layer[x, y] in (K('turf'), K('turf_l')):
            layer[x:x + 2, y:y + 2] = K('turf_l')
            height[x:x + 2, y:y + 2] = 5
    return _relief(layer, height, K('pack_d'))


def yard_ground_out(name):
    """前庭の地面 — 踏み固めた土に藁くずと小石。**街路より暗い**（序列を守る）。"""
    rng = rng_for(name)
    v = _tile(2)
    box(v, 0, V, 0, V, 0, 1, K('pack_d'))
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('pack_d'), K('pack'), K('dirt_d'), K('soil')], [0.40, 0.26, 0.20, 0.14])
    G['_grain'](rng, layer, layer > 0, [K('hay_d'), K('straw_d'), K('pebble')], [0.07, 0.05, 0.04], block=3)
    v[:, :, 1] = layer
    return [('main', v, (0, 0, -2))]


def grass_out(name):
    """草の株（`tuft`）。葉を 1 本ずつ立てる。高さ 0.5 マス。"""
    rng = rng_for(name)
    v = vol(h=16)
    for _ in range(int(rng.integers(10, 16))):
        bx, by = int(rng.integers(8, V - 8)), int(rng.integers(8, V - 8))
        tall = int(rng.integers(6, 15))
        lean_x, lean_y = int(rng.integers(-3, 4)), int(rng.integers(-3, 4))
        blade = pick(rng, [K('turf_l'), K('turf'), K('turf_d')], [0.4, 0.4, 0.2])
        for z in range(tall):
            t = z / float(tall)
            x = min(V - 1, max(0, bx + int(round(lean_x * t))))
            y = min(V - 1, max(0, by + int(round(lean_y * t))))
            v[x, y, z] = blade
    return [('main', v, (0, 0, 0))]


def grass_edge_out(name):
    """道の縁の草 — 道のわきへ寄せて置く密な株。低め・広め（帯に見えるように）。"""
    rng = rng_for(name)
    v = vol(h=12)
    for _ in range(int(rng.integers(22, 30))):
        bx, by = int(rng.integers(4, V - 4)), int(rng.integers(9, V - 9))
        tall = int(rng.integers(4, 11))
        lean_x, lean_y = int(rng.integers(-2, 3)), int(rng.integers(-2, 3))
        blade = pick(rng, [K('turf_l'), K('turf'), K('grass_l')], [0.4, 0.4, 0.2])
        for z in range(tall):
            t = z / float(tall)
            x = min(V - 1, max(0, bx + int(round(lean_x * t))))
            y = min(V - 1, max(0, by + int(round(lean_y * t))))
            v[x, y, z] = blade
    return [('main', v, (0, 0, 0))]


def ground_bridge_out(name):
    """堀を渡る板の橋。`_ns`（水が北と南）／`_ew`（水が東と西）。板は渡る向きと直交に並べ、
    水の側の縁に低い手すり。地面の 1 枚として置く（origin z=-2）。"""
    #! 名前は置く側の呼び方（`_ns` = 水が**北と南**＝東西へ渡る）。
    ew = name.startswith('ground_bridge_ns')
    rng = rng_for(name)
    v = _tile(10)
    box(v, 0, V, 0, V, 0, 8, K('timber_d'))                  # 橋の桁（厚み）
    layer = np.zeros((V, V), np.int16)
    p = 0
    while p < V:
        w = int(rng.integers(3, 5))
        shade = pick(rng, [K('board'), K('board_l'), K('board_d')], [0.5, 0.25, 0.25])
        if ew:
            layer[p:p + w, :] = shade
            layer[min(V - 1, p + w), :] = K('timber_d')
        else:
            layer[:, p:p + w] = shade
            layer[:, min(V - 1, p + w)] = K('timber_d')
        p += w + 1
    v[:, :, 8] = layer
    v[:, :, 9] = layer
    # 手すり（水の側の 2 辺）。地面より上へ出るぶんは z 10 以降＝別の高さが要るので、
    # ここでは**縁の桁を 1 段高く**するだけ（低い縁石ふう）。
    if ew:
        box(v, 0, V, 0, 2, 8, 10, K('log_d'))
        box(v, 0, V, V - 2, V, 8, 10, K('log_d'))
    else:
        box(v, 0, 2, 0, V, 8, 10, K('log_d'))
        box(v, V - 2, V, 0, V, 8, 10, K('log_d'))
    return [('main', v, (0, 0, -10))]


def bridge_rail_out(name):
    """橋の手すり（`ground_bridge_*` と同じマスへ置く別体。低い柵）。"""
    ew = name.endswith('_ew')
    v = vol(h=12)
    for a0 in (0, V - 3):
        if ew:
            box(v, 0, V, a0, a0 + 3, 6, 9, K('oak'))
            for p in range(1, V, 10):
                box(v, p, p + 3, a0, a0 + 3, 0, 12, K('timber'))
        else:
            box(v, a0, a0 + 3, 0, V, 6, 9, K('oak'))
            for p in range(1, V, 10):
                box(v, a0, a0 + 3, p, p + 3, 0, 12, K('timber'))
    return [('main', v, (0, 0, 0))]


# ------------------------------------------------------------------ 小川
BROOK_X = (2, 11)   #: 南北の流れはマスの**西寄り**（x 2..11）。水車がここへ掛かる
BROOK_Y = (2, 11)   #: 東西の流れはマスの**北寄り**（y 2..11）。敷地の柵の足元を流れる


def _brook_tile(name, conn, plank=False):
    """小川のマス。`conn` はつながる辺の集合（'n' 'e' 's' 'w'）。地面は 4 段（origin -4）。
    水面は地面より 1 段低く、岸に石を並べる。歩ける側は踏み固めた土。"""
    rng = rng_for(name)
    v = _tile(4)
    box(v, 0, V, 0, V, 0, 2, K('dirt_d'))
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('pack'), K('turf_d'), K('pack_d'), K('turf')], [0.36, 0.26, 0.22, 0.16])
    v[:, :, 2] = layer
    v[:, :, 3] = layer
    chan = np.zeros((V, V), bool)
    x0, x1 = BROOK_X
    y0, y1 = BROOK_Y
    if ('n' in conn) or ('s' in conn):
        ya = 0 if 'n' in conn else y0
        yb = V if 's' in conn else y1
        chan[x0:x1, ya:yb] = True
    if ('e' in conn) or ('w' in conn):
        xa = 0 if 'w' in conn else x0
        xb = V if 'e' in conn else x1
        chan[xa:xb, y0:y1] = True
    # 川底と水面。
    v[:, :, 3][chan] = 0
    v[:, :, 2][chan] = K('wa')
    v[:, :, 1][chan] = K('wa_d')
    # 流れの筋（明るい水を細く）。
    for _ in range(3):
        px, py = int(rng.integers(0, V)), int(rng.integers(0, V))
        for k in range(6):
            qx, qy = min(V - 1, px + (k if ('e' in conn or 'w' in conn) else 0)), min(V - 1, py + (k if ('n' in conn or 's' in conn) else 0))
            if chan[qx, qy]:
                v[qx, qy, 2] = K('wa_l')
    # 岸の石（水際の 1 列に塊で）。
    edge = np.zeros((V, V), bool)
    edge[1:-1, 1:-1] = (~chan[1:-1, 1:-1]) & (chan[:-2, 1:-1] | chan[2:, 1:-1] | chan[1:-1, :-2] | chan[1:-1, 2:])
    for x, y in np.argwhere(edge):
        if rng.random() < 0.55:
            v[x, y, 3] = pick(rng, [K('rub'), K('rub_l'), K('pebble')], [0.4, 0.3, 0.3])
    if plank:
        # 板を渡す（流れと直交）。厚み 1、幅 8。
        if ('n' in conn) or ('s' in conn):
            for x in range(x0 - 2, x1 + 2):
                for y in range(12, 20):
                    v[x, y, 3] = K('board') if ((y - 12) % 4) != 3 else K('board_d')
        else:
            for y in range(y0 - 2, y1 + 2):
                for x in range(12, 20):
                    v[x, y, 3] = K('board') if ((x - 12) % 4) != 3 else K('board_d')
    return [('main', v, (0, 0, -4))]


def brook_ns_out(name):
    return _brook_tile(name, {'n', 's'})


def brook_ew_out(name):
    return _brook_tile(name, {'e', 'w'})


def brook_plank_ns_out(name):
    return _brook_tile(name, {'n', 's'}, plank=True)


def brook_plank_ew_out(name):
    return _brook_tile(name, {'e', 'w'}, plank=True)


def _brook_bend(conn):
    return lambda name: _brook_tile(name, conn)


def brook_culvert_out(name):
    """塀の下の樋 — 丸太の柵列の西寄り（x 2..11）に石の口を開け、水を通す。"""
    parts = G['palisade'](name.replace('brook_culvert_out', 'palisade_out_02'))
    v = parts[0][1].astype(np.int16)
    x0, x1 = BROOK_X
    box(v, x0 - 1, x1 + 1, 0, V, 0, 12, 0)                   # 口
    box(v, x0 - 2, x0, 0, V, 0, 13, K('rub'))                 # 石の側壁
    box(v, x1, x1 + 2, 0, V, 0, 13, K('rub'))
    box(v, x0 - 2, x1 + 2, 0, V, 12, 15, K('rub_l'))          # 楣
    box(v, x0, x1, 0, V, 0, 1, K('wa_d'))                     # 水
    box(v, x0, x1, 0, V, 1, 2, K('wa'))
    return [('main', v, (0, 0, 0))]


# ------------------------------------------------------------------ 塀・櫓
def palisade_out(name):
    """辺境の丸太の塀 — 太さと高さの違う丸太を 4×4 に立て、縄で 2 段に結わえる。
    頭は尖らせ、根元に苔と草。木肌には縦の割れ目を 1 本ずつ彫る（面の中の線なので安い）。"""
    rng = rng_for(name)
    height = 64
    v = vol(h=height)
    step = 16
    for bx in range(0, V, step):
        for by in range(0, V, step):
            r = float(rng.choice([8.6, 8.2, 7.6]))
            cx, cy = bx + 8.0, by + 8.0
            top = int(rng.integers(height - 12, height))
            shade = pick(rng, [K('log'), K('log_l'), K('log_d')], [0.5, 0.25, 0.25])
            for x in range(bx, min(bx + step, V)):
                for y in range(by, min(by + step, V)):
                    d2 = ((x + 0.5 - cx) ** 2) + ((y + 0.5 - cy) ** 2)
                    if d2 > r * r:
                        continue
                    taper = min(11, int(round(7.0 * (d2 / (r * r)))))
                    v[x, y, 0:top - taper] = shade
                    v[x, y, top - taper - 1] = K('log_end')
            # 木肌の割れ目（縦 1 本。外周の 1 ボクセル）
            ang = rng.random() * 6.28
            fx = int(round(cx + (r - 1.0) * np.cos(ang)))
            fy = int(round(cy + (r - 1.0) * np.sin(ang)))
            if 0 <= fx < V and 0 <= fy < V:
                z0 = int(rng.integers(6, 20))
                col = v[fx, fy, :]
                col[z0:top - 14][col[z0:top - 14] != 0] = K('log_d')
    for z0 in (20, 44):                                      # 縄
        band = v[:, :, z0:z0 + 3]
        band[band != 0] = K('lash')
    for _ in range(int(rng.integers(2, 5))):                 # 根元の苔
        x, y = int(rng.integers(0, V - 5)), int(rng.integers(0, V - 5))
        block = v[x:x + 5, y:y + 5, 0:5]
        block[block != 0] = K('moss_d') if rng.random() < 0.5 else K('moss')
    return [('main', v, (0, 0, 0))]


def watchtower_out(name):
    """火皿つきの櫓 — 既存の木造のやぐらに、見張り台の鉄の火皿と槍立てを足す。"""
    parts = G['watchtower'](name)
    v = parts[0][1].astype(np.int16)
    # 火皿（鉄の輪に炭火）。床は z 72..76、手すりは 76..86。
    disc(v, 16, 16, 4.5, 76, 79, K('out_iron_k'))
    disc(v, 16, 16, 3.5, 77, 79, K('emb_l'))
    disc(v, 16, 16, 2.0, 78, 81, K('fl_mid'))
    disc(v, 16, 16, 1.0, 79, 82, K('fl_core'))
    box(v, 15, 17, 15, 17, 72, 76, K('out_iron_k'))          # 脚
    # 槍立て（隅の柱に立て掛けた槍 2 本）。
    for (x, y) in ((5, 5), (26, 26)):
        box(v, x, x + 1, y, y + 1, 76, 96, K('timber_d'))
        box(v, x, x + 1, y, y + 1, 96, 99, K('steel'))
    return [('main', v, (0, 0, 0))]


# ------------------------------------------------------------------ 登録
def register(g):
    """`gen_prefabs.py` の名前空間 `g` へ辺境の素材を足す。"""
    global G, C, V
    G = g
    C = g['C']
    V = g['V']
    g['TOWN_STYLES'] = tuple(g['TOWN_STYLES']) + ('out', 'outb')
    _palette()
    cat = g['CATALOG']
    static = g['static_part']

    for sl in g['HOUSE_SLICES']:
        cat['house_wall_%s_out' % sl] = (house_wall_out, static(), '辺境の壁 1 層（丸太組みと土壁。%s）' % sl)
        cat['house_roof_%s_out' % sl] = (house_roof_out, static(), '辺境の屋根（こけら葺きの寄棟。%s）' % sl)
        cat['house_wall_%s_outb' % sl] = (house_wall_outb, static(), '納屋の壁 1 層（縦板張り。%s）' % sl)
        cat['house_roof_%s_outb' % sl] = (house_roof_outb, static(), '納屋の屋根（藁葺き。%s）' % sl)
    cat['house_entrance_out'] = (house_entrance_out, static(), '辺境の戸口（板戸に鉄の帯）')
    cat['house_entrance_outb'] = (house_entrance_outb, static(), '納屋の両開き戸')
    cat['chimney_out'] = (chimney_out, static(), '石の煙突（roof_vent。煙は置く側が積む）')
    cat['house_light_out'] = (house_light_out, static(), '窓の灯り（辺境の窓の高さ）')
    for side in ('n', 's', 'w', 'e'):
        cat['fence_%s_out' % side] = (fence_out, static(), '割り木の横桟の柵（%s 側）' % side)
    cat['arch_out'] = (arch_out, static(), '丸太の門（南北にくぐる）。吊りランタンつき')
    cat['arch_out_ew'] = (arch_out_ew, static(), '丸太の門（東西にくぐる）')
    cat['arch_flame_out'] = (arch_flame_out, static(), '門のランタンの火（夜。gate_flame）')
    cat['arch_flame_out_ew'] = (arch_flame_out_ew, static(), '門のランタンの火（東西）')
    for i in range(1, 6):
        cat['ground_path_out_%02d' % i] = (ground_path_out, static(), '辺境の道（轍と足跡の土）')
        cat['ground_turf_out_%02d' % i] = (ground_turf_out, static(), '辺境の草地（短い草。土が透ける）')
    cat['yard_ground_out'] = (yard_ground_out, static(), '辺境の前庭の地面（踏み固めた土と藁くず）')
    cat['grass_out'] = (grass_out, static(motion={'kind': 'wind'}, wind_k=0.8), '草の株（tuft）')
    cat['grass_edge_out'] = (grass_edge_out, static(motion={'kind': 'wind'}, wind_k=0.8), '道の縁の草（path_edge）')
    cat['ground_bridge_ns_out'] = (ground_bridge_out, static(), '堀の板橋（水が北と南＝東西へ渡る）')
    cat['ground_bridge_ew_out'] = (ground_bridge_out, static(), '堀の板橋（水が東と西＝南北へ渡る）')
    cat['brook_ns_out'] = (brook_ns_out, static(), '小川（南北）')
    cat['brook_ew_out'] = (brook_ew_out, static(), '小川（東西）')
    cat['brook_plank_ns_out'] = (brook_plank_ns_out, static(), '小川（南北）に板を渡した所')
    cat['brook_plank_ew_out'] = (brook_plank_ew_out, static(), '小川（東西）に板を渡した所')
    for key in ('ne', 'nw', 'se', 'sw'):
        cat['brook_bend_%s_out' % key] = (_brook_bend(set(key)), static(), '小川の曲がり（%s）' % key)
    cat['brook_culvert_out'] = (brook_culvert_out, static(), '塀の下の樋（塀のマスの差し替え）')
    for i in range(1, 4):
        cat['palisade_out_%02d' % i] = (palisade_out, static(), '辺境の丸太の塀（苔つき）')
    cat['watchtower_out'] = (watchtower_out, static(), '火皿つきの櫓')

    for module in ('outpost_props', 'outpost_landmarks', 'outpost_village'):
        spec = importlib.util.spec_from_file_location(module, os.path.join(HERE, module + '.py'))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        mod.register(g, globals())
