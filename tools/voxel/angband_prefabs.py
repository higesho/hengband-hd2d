# -*- coding: utf-8 -*-
"""Angband の町（廃墟と溶岩のある朽ちかけた村）の素材の生成器。

`gen_prefabs.py` から `register(globals())` で呼ばれ、接尾辞 `_agb` のプレハブを `CATALOG` へ足す。
**既存の素材には 1 バイトも触らない**（別の名前で足すだけ）。手本は辺境の地の
`outpost_prefabs.py` / `outpost_props.py`。

## 作りの規律（`gen_prefabs.py` 冒頭と同じ）
- 無作為な表面ノイズは入れない。彫りは 1 ボクセルの深さ。色は塊ごとに 1 色
- 接地層（z=0）の平面方向は縮めない。+y は南＝カメラ側。戸口は手前が y 大
- 廃墟は**歩けるかどうかと高さを合わせる**: 通れない瓦礫（`ruin_wall_*`）は高く（z 14〜28）、
  通れる瓦礫（`ruin_low_*`）は低く（z 8 以下）

## 色の名前の付け方（手本と違う所）
辺境の `out_plas` などは `palette_materials.classify` が**頭の語**で材質を当てるため、
`out_` が頭に来ると材質が `default` に落ちていた（`house_wall_nw_out.jsonc` の `out_plas` は
`"material": "default"`）。ここでは **`<材質の語>_agb[_l|_d]`**（`rock_agb_l` `board_agb_d`）の形にして、
石・木・土・布・金属の材質が宣言に載るようにする。

## 名前
- 9 スライスの壁・屋根は `house_wall_nw_agb` のように**末尾**に `_agb`
- 変種のあるものは辺境と同じく `ruin_wall_agb_01` のように**番号の前**に `_agb`
- 火の相方（`campfire_fire_agb` など）は別体。置く側が自発光をつけて同じ位置へ重ねる
"""
from __future__ import annotations

import numpy as np

G = None   #: gen_prefabs の名前空間（register で入る）
C = None   #: 色の名前 → 索引
V = 32


# ------------------------------------------------------------------ 形の道具
def vol(w=None, d=None, h=32):
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
    X, Y, Z = v.shape
    ys = np.arange(Y)[:, None] + 0.5 - cy
    zs = np.arange(Z)[None, :] + 0.5 - cz
    m = (ys * ys) + (zs * zs) <= (r * r)
    for x in range(max(0, x0), min(X, x1)):
        v[x][m] = colour


def log_y(v, cx, cz, r, y0, y1, colour):
    X, Y, Z = v.shape
    xs = np.arange(X)[:, None] + 0.5 - cx
    zs = np.arange(Z)[None, :] + 0.5 - cz
    m = (xs * xs) + (zs * zs) <= (r * r)
    for y in range(max(0, y0), min(Y, y1)):
        v[:, y, :][m] = colour


def ball(v, cx, cy, cz, r, colour, squash=1.0):
    X, Y, Z = v.shape
    xs = np.arange(X)[:, None, None] + 0.5 - cx
    ys = np.arange(Y)[None, :, None] + 0.5 - cy
    zs = (np.arange(Z)[None, None, :] + 0.5 - cz) / squash
    v[(xs * xs) + (ys * ys) + (zs * zs) <= (r * r)] = colour


def rod(v, p0, p1, r0, r1, colour):
    """2 点を結ぶ棒（枝・梁・三脚の脚）。半径は p0 で r0、p1 で r1。"""
    X, Y, Z = v.shape
    n = int(max(abs(np.array(p1, float) - np.array(p0, float)).max() * 2, 6))
    xs = np.arange(X)[:, None] + 0.5
    ys = np.arange(Y)[None, :] + 0.5
    for i in range(n + 1):
        t = i / float(n)
        px, py, pz = [p0[k] + (p1[k] - p0[k]) * t for k in range(3)]
        r = r0 + (r1 - r0) * t
        m = ((xs - px) ** 2) + ((ys - py) ** 2) < (r * r)
        zi = int(round(pz))
        for dz in range(-int(r), int(r) + 1):
            if 0 <= zi + dz < Z and abs(dz) < max(1.0, r):
                v[:, :, zi + dz][m] = colour


def rng_for(name):
    return G['rng_for'](name)


def pick(rng, shades, weights=None):
    return int(rng.choice(shades, p=weights))


def K(name):
    return C[name]


def one(v, z0=0):
    return [('main', v, (0, 0, z0))]


# ------------------------------------------------------------------ 色
def _palette():
    rl = G['reg_local']
    #! 焦げた石（家・廃墟・煙突）。変愚の `rub` より灰が強く、暗い。
    rl('rock_agb', (104, 98, 92)); rl('rock_agb_l', (130, 124, 116)); rl('rock_agb_d', (86, 82, 78))
    #! 目地（彫った底）。**石のどの色よりも暗く、石の色には使わない**——初版は暗い石と同じ色にしていて、
    #! 石が離れて浮いた塊に見えた（一覧の画像で気づいた）。
    rl('rock_agb_j', (62, 58, 56))
    #! 煤けた石（土台と溶岩のそば）。
    rl('soot_agb', (50, 46, 46)); rl('soot_agb_l', (68, 64, 64))
    #! 灰色に灼けた板（継ぎはぎ・屋根・戸）。
    rl('board_agb', (118, 106, 90)); rl('board_agb_l', (144, 132, 114)); rl('board_agb_d', (88, 78, 66))
    #! 焼けて炭になった木（梁・枯れ木の折れ口）。
    rl('char_agb', (36, 32, 32)); rl('char_agb_l', (56, 50, 50))
    #! 帆布（屋根の当て布・荷車の覆い）。
    #! 板より明るく白っぽくしないと屋根の上で当て布が読めない（一覧の画像で気づいた）。
    rl('cloth_agb', (204, 194, 168)); rl('cloth_agb_d', (164, 154, 130))
    #! 枯れ木の黒い樹皮。
    rl('bark_agb', (42, 38, 36)); rl('bark_agb_l', (64, 58, 54))
    #! 灰（地面）。モリバントの `ash` は切石の色なので別に持つ。
    rl('ash_agb', (150, 144, 134)); rl('ash_agb_l', (174, 168, 158)); rl('ash_agb_d', (120, 114, 106))
    #! 錆びた鉄の輪・釜。
    rl('rust_agb', (118, 70, 44))
    #! 泥に溜まった水（第 4 回）。`wa_dd` の青は灰の泥の上で紙吹雪のように散った（実機の絵で確かめた）。
    #! 名前は `wa_` で始めて汚さない材質（clean）に。
    rl('wa_agb', (34, 40, 48)); rl('wa_agb_l', (50, 58, 68))


# ------------------------------------------------------------------ 石積みの道具
COURSE = 6   #: 石 1 段の高さ（目地を含む）


def _stone_face(v, rng, fx, fy, bx, by, along_x, lo, hi, z0, z1, shades, weights):
    """壁の外に面した 1 面へ石を描く。段ごとに継ぎ目を半枚ずらし、**目地は 1 ボクセル彫る**
    （面を 0 にして 1 つ内側を暗い石にする。色だけで線を引くと AO と重なって黒くなる）。"""
    def paint(a, b, za, zb, colour):
        if a >= b or za >= zb:
            return
        if along_x:
            v[a:b, fy, za:zb] = colour
        else:
            v[fx, a:b, za:zb] = colour

    def carve(a, b, za, zb):
        if a >= b or za >= zb:
            return
        if along_x:
            v[a:b, fy, za:zb] = 0
            v[a:b, by, za:zb] = K('rock_agb_j')
        else:
            v[fx, a:b, za:zb] = 0
            v[bx, a:b, za:zb] = K('rock_agb_j')

    row = 0
    for zc in range(z0, z1, COURSE):
        ztop = min(z1, zc + COURSE)
        zj = ztop - 1 if ztop < z1 else ztop
        off = (row % 2) * 4
        p = lo - off
        while p < hi:
            w = int(rng.integers(5, 10))
            paint(max(lo, p), min(hi, p + w), zc, zj, pick(rng, shades, weights))
            j = p + w
            if lo <= j < hi:
                carve(j, j + 1, zc, zj)
            p += w + 1
        if ztop < z1:
            carve(lo, hi, zj, ztop)
        row += 1


def _masonry_fill(v, rng, mask, heights, shades, weights):
    """自立した石の壁（廃墟）。`mask`（V×V）の柱を `heights` まで石で埋め、
    段と継ぎ目を 3 次元の格子で割り当てて、**外に露出した目地だけを 1 ボクセル彫る**。"""
    X, Y, Z = v.shape
    xx, yy, zz = np.meshgrid(np.arange(X), np.arange(Y), np.arange(Z), indexing='ij')
    fill = mask[:, :, None] & (zz < heights[:, :, None])
    table = rng.choice(shades, size=(8, 8, 8), p=weights)
    r = zz // COURSE
    sx = xx + (r % 2) * 3
    sy = yy + (r % 2) * 3
    sw = 7
    ix, iy = sx // sw, sy // sw
    joint = (zz % COURSE == COURSE - 1) | (sx % sw == sw - 1) | (sy % sw == sw - 1)
    colour = table[r % 8, ix % 8, iy % 8]
    v[fill] = colour[fill]
    v[fill & joint] = K('rock_agb_j')
    empty = ~fill
    exposed = np.zeros_like(fill)
    exposed[1:, :, :] |= empty[:-1, :, :]
    exposed[:-1, :, :] |= empty[1:, :, :]
    exposed[:, 1:, :] |= empty[:, :-1, :]
    exposed[:, :-1, :] |= empty[:, 1:, :]
    exposed[0, :, :] = True
    exposed[-1, :, :] = True
    exposed[:, 0, :] = True
    exposed[:, -1, :] = True
    top = zz == (heights[:, :, None] - 1)
    #! 彫るのは**段の目地（水平）だけ**。縦の継ぎ目まで彫ると、薄い壁が別々の石塊の山に見えた
    #! （一覧の画像で気づいた）。縦の継ぎ目は目地の色だけで読ませる。
    course_joint = zz % COURSE == COURSE - 1
    v[fill & exposed & course_joint & ~top & (zz > 0)] = 0


def _lumps(v, rng, n, xr, yr, zcap, shades, weights, span=(3, 8)):
    """岩塊を重ねた瓦礫の山。高さは `zcap` で抑える（通れる瓦礫は低く）。
    **山だけを別の体積に描いてから合わせる**——`v` 全体を切ると、先に立てた壁まで消える（1 度そうなった）。"""
    heap = np.zeros_like(v)
    for _ in range(n):
        cx, cy = int(rng.integers(*xr)), int(rng.integers(*yr))
        w, d = int(rng.integers(*span)), int(rng.integers(*span))
        h = int(rng.integers(2, 6))
        z0 = int(rng.integers(0, 3))
        box(heap, cx - w // 2, cx + (w + 1) // 2, cy - d // 2, cy + (d + 1) // 2, max(0, z0 - 1), z0 + h,
            pick(rng, shades, weights))
    heap[:, :, zcap:] = 0
    v[heap > 0] = heap[heap > 0]


STONE = None   #: 石の色（register の後で埋める）
STONE_W = [0.58, 0.26, 0.16]


def _stone():
    return [K('rock_agb'), K('rock_agb_l'), K('rock_agb_d')]


# ------------------------------------------------------------------ 使える建物（店・我が家）
WALL_H = 34
ROOF_H = 30
EAVE = 4
INSET = 2
PLINTH = 5


def _slice(name):
    """名前からスライス（nw/n/…）を取る。`house_roof_nw_agb_03` のように**番号つきの変種**でも
    拾えるよう、`_slice_of`（末尾の語を見る）ではなく語の並びから探す。"""
    for part in name.split('_'):
        if part in G['HOUSE_SLICES']:
            return part
    return G['_slice_of'](name)


def _variant(name):
    """名前の末尾の番号（無ければ 0）。"""
    tail = name.rsplit('_', 1)[-1]
    return int(tail) if tail.isdigit() else 0


def _outward(name):
    return G['SLICE_OUT'][_slice(name)]


def _face_of(x0, x1, y0, y1, side):
    """外側 1 ボクセルの面と、その 1 つ内側（彫った目地の底）。"""
    if side == 'n':
        return (slice(x0, x1), slice(y0, y0 + 1)), (slice(x0, x1), slice(y0 + 1, y0 + 2))
    if side == 's':
        return (slice(x0, x1), slice(y1 - 1, y1)), (slice(x0, x1), slice(y1 - 2, y1 - 1))
    if side == 'w':
        return (slice(x0, x0 + 1), slice(y0, y1)), (slice(x0 + 1, x0 + 2), slice(y0, y1))
    return (slice(x1 - 1, x1), slice(y0, y1)), (slice(x1 - 2, x1 - 1), slice(y0, y1))


def _proud(x0, x1, y0, y1, side, a, b, z0, z1):
    """面から 1 ボクセル手前へ出る帯（桟・桁）の箱の範囲。外に面した辺は INSET があるので出せる。"""
    if side == 'n':
        return (a, b, y0 - 1, y0, z0, z1)
    if side == 's':
        return (a, b, y1, y1 + 1, z0, z1)
    if side == 'w':
        return (x0 - 1, x0, a, b, z0, z1)
    return (x1, x1 + 1, a, b, z0, z1)


def house_wall_agb(name):
    """**朽ちかけた村の壁** — 煤けた石の土台、焦げた石積みの壁。石が崩れ落ちた所を
    灰色の板で継ぎはぎしてある（板の間は 1 ボクセルの溝、上下に横桟）。窓は南面だけで、
    下半分を板でふさぐ。軒桁は焦げた材。"""
    north, south, west, east = _outward(name)
    rng = rng_for(name)
    v = vol(h=WALL_H)
    box(v, 0, V, 0, V, 0, PLINTH, K('soot_agb'))
    box(v, 0, V, 0, V, PLINTH - 1, PLINTH, K('soot_agb_l'))
    x0 = INSET if west else 0
    x1 = V - INSET if east else V
    y0 = INSET if north else 0
    y1 = V - INSET if south else V
    stone_top = WALL_H - 3
    box(v, x0, x1, y0, y1, PLINTH, WALL_H, K('rock_agb'))
    faces = [s for s, on in (('n', north), ('s', south), ('w', west), ('e', east)) if on]
    shades = _stone()
    for side in faces:
        (fx, fy), (bx, by) = _face_of(x0, x1, y0, y1, side)
        along_x = side in ('n', 's')
        lo, hi = (x0, x1) if along_x else (y0, y1)
        _stone_face(v, rng, fx, fy, bx, by, along_x, lo, hi, PLINTH, stone_top, shades, STONE_W)
        # 煤の帯（土台のすぐ上。塊で 2 か所）。
        for _ in range(2):
            a = int(rng.integers(lo, max(lo + 1, hi - 5)))
            w = int(rng.integers(3, 7))
            if along_x:
                blk = v[a:min(hi, a + w), fy, PLINTH:PLINTH + 3]
            else:
                blk = v[fx, a:min(hi, a + w), PLINTH:PLINTH + 3]
            blk[blk > 0] = K('soot_agb_l')
        # 板の継ぎはぎ（石が崩れた所）。角の 4 ボクセルは石のまま残す（建物が石造だと読める）。
        #! 第 3 回: 変種 01〜04 で崩れ具合と継ぎはぎの位置を揺らす（01 継ぎはぎ無し / 02 1 か所 /
        #! 03 2 か所 / 04 1 か所＋石の抜けた穴）。番号無しは従来どおり乱択。
        variant = _variant(name)
        if variant == 0:
            n_patch = 1 if (hi - lo) < 30 else int(rng.integers(1, 3))
        else:
            n_patch = {1: 0, 2: 1, 3: 2, 4: 1}[min(variant, 4)]
        if variant == 4:                                     # 石の抜けた穴（2 ボクセル彫って暗く）
            hx = int(rng.integers(lo + 4, max(lo + 5, hi - 10)))
            hz = int(rng.integers(PLINTH + 3, 14))
            if along_x:
                v[hx:hx + 6, fy, hz:hz + 5] = 0
                v[hx:hx + 6, by, hz:hz + 5] = K('dark')
            else:
                v[fx, hx:hx + 6, hz:hz + 5] = 0
                v[bx, hx:hx + 6, hz:hz + 5] = K('dark')
        used = []
        for _ in range(n_patch):
            w = int(rng.integers(8, 12))
            a = int(rng.integers(lo + 4, max(lo + 5, hi - 4 - w)))
            if any(abs(a - u) < 12 for u in used):
                continue
            used.append(a)
            zb = int(rng.integers(13, 20))
            strip = 0
            for x in range(a, a + w):
                zt = zb + int(rng.integers(-2, 3))
                gap = (strip % 4) == 3
                colour = 0 if gap else pick(rng, [K('board_agb'), K('board_agb_l'), K('board_agb_d')], [0.5, 0.25, 0.25])
                if along_x:
                    v[x, fy, zt:stone_top] = colour
                    v[x, by, zt:stone_top] = K('board_agb_d')
                else:
                    v[fx, x, zt:stone_top] = colour
                    v[bx, x, zt:stone_top] = K('board_agb_d')
                strip += 1
            for zr in (zb + 3, stone_top - 5):
                box(v, *_proud(x0, x1, y0, y1, side, a - 1, a + w + 1, zr, zr + 2), K('timber_d'))
        # 軒桁（焦げた材。1 ボクセル手前へ）。
        box(v, *_proud(x0, x1, y0, y1, side, lo, hi, stone_top, WALL_H), K('char_agb'))
        if along_x:
            v[lo:hi, fy, stone_top:WALL_H] = K('timber_d')
        else:
            v[fx, lo:hi, stone_top:WALL_H] = K('timber_d')
    # 窓（南面だけ）。上半分は硝子、下半分を板でふさぐ。
    if south:
        wx = ((x0 + x1) // 2) - 4
        wz = 21
        box(v, wx - 1, wx + 9, y1 - 2, y1, wz - 1, wz + 8, K('timber_d'))     # 枠
        box(v, wx, wx + 8, y1 - 2, y1, wz, wz + 7, K('glass'))
        box(v, wx + 3, wx + 5, y1 - 2, y1, wz, wz + 7, K('timber_d'))         # 中桟
        for zb in (wz, wz + 3):                                             # ふさぐ板（手前へ 1）
            box(v, wx - 2, wx + 10, y1, y1 + 1, zb - 1, zb + 1, K('board_agb_l'))
        box(v, wx + 2, wx + 4, y1, y1 + 1, wz - 1, wz + 3, 0)                # 板の割れ目
    return one(v)


def house_roof_agb(name):
    """**穴のあいた板葺き** — 灰色の板を段に葺き、1〜2 か所に穴（垂木が覗く）、
    別の所に帆布の当て布（縄で押さえる）。反った板と欠けた板。"""
    north, south, west, east = _outward(name)
    rng = rng_for(name)
    height, hip = G['_roof_field'](north, south, west, east, EAVE, ROOF_H, 1.0)
    v = vol(h=ROOF_H)
    shades = [K('board_agb'), K('board_agb_l'), K('board_agb_d'), K('board_d')]
    plank = rng.choice(shades, size=(V // 3 + 2, V // 3 + 2, ROOF_H // 3 + 1), p=[0.40, 0.24, 0.22, 0.14])
    for x in range(V):
        for y in range(V):
            h = int(height[x, y])
            for z in range(h):
                if z < EAVE:
                    v[x, y, z] = K('board_agb_d')
                else:
                    row = z // 3
                    off = (row % 2) * 2
                    v[x, y, z] = plank[(x + off) // 3, (y + off) // 3, row]
            if (h % 3) == 0:
                v[x, y, h - 1] = K('board_agb_d')
            if hip[x, y] and (h < ROOF_H):
                v[x, y, h - 1] = K('ridge')
            #! 棟の色は稜線のあるスライスだけ。`mid`（外に面した辺が無い＝天面が棟の高さで平ら）は
            #! 板のままにする——初版は天面が全部 `ridge` の茶色い箱になった（レビュー担当の指摘）。
            if h >= ROOF_H and (north or south or west or east):
                v[x, y, h - 1] = K('ridge')
    #! 第 3 回: 変種で穴の有無・大きさ・位置を揺らす（決めたこと「屋根の穴も一定の間隔で
    #! 並んでいるため穴に見えない。もっと大きな穴をあけて、大小の揺らぎ、位置の揺らぎを」）。
    #! 01〜04 無地（03 は反った板・04 は欠けた板） / 05 小さな穴 1〜2（2〜4 vox） /
    #! 06 大きな穴（マスの半分ほど） / 07 帆布の当て布だけ / 08 大きな穴＋当て布。
    #! 番号無し（基）は無地。**無地が半分**なので穴のあるマスが規則正しく並ばない。
    variant = _variant(name)
    flip = rng.random() < 0.5
    holes = []
    if variant == 5:
        for _ in range(int(rng.integers(1, 3))):
            holes.append((int(rng.integers(2, 5)), int(rng.integers(2, 5))))
    elif variant in (6, 8):
        holes.append((int(rng.integers(12, 17)), int(rng.integers(10, 15))))
    #! 第 5 回: 穴は**板を何枚も抜いた不定形**にする（決めたこと「屋根の穴は四角くなってしまって
    #! いるので、複数枚を組み合わせた不定形な穴を」）。板は 3 ボクセル幅で東西に走るので、
    #! 3 幅の帯ごとに抜く長さと始まりを変え、帯を飛ばして板を残し、折れた板の端を穴へ垂らし、
    #! 縁を焦がす。大きさ（小・中・大）と位置の揺らぎ、無地の割合は第 3 回のまま。
    for (w, d) in holes:
        cx = int(rng.integers(2, max(3, V // 2 - w))) if flip else int(rng.integers(V // 2, max(V // 2 + 1, V - 2 - w)))
        cy = int(rng.integers(3, max(4, V - 3 - d)))
        mask = np.zeros((V, V), bool)
        big = w >= 6
        for ys in range(cy, cy + d, 3):                      # 板の帯ごとに抜く
            if big and rng.random() < 0.22:
                continue                                     # この板は残っている
            a0 = cx + int(rng.integers(0, max(1, w // 3)))
            b0 = cx + w - int(rng.integers(0, max(1, w // 3)))
            if b0 - a0 < 2:
                a0, b0 = cx, cx + max(2, w)
            mask[a0:b0, ys:min(V, ys + 3)] = True
            if big and rng.random() < 0.5:                   # 途中で折れて短い板（穴が食い込む）
                mask[b0:min(V, b0 + int(rng.integers(2, 6))), ys:min(V, ys + 3)] = True
        if big:                                              # 端の欠け（角を落として四角に見せない）
            for _ in range(int(rng.integers(2, 4))):
                ex, ey = int(rng.integers(cx - 2, cx + w)), int(rng.integers(cy - 1, cy + d))
                mask[max(0, ex):ex + int(rng.integers(2, 5)), max(0, ey):ey + int(rng.integers(1, 3))] = True
        mask[0:1, :] = False
        mask[V - 1:V, :] = False
        mask[:, 0:1] = False
        mask[:, V - 1:V] = False
        for x, y in np.argwhere(mask):
            h = int(height[x, y])
            nt = max(2, h - 3)
            v[x, y, nt:] = 0
            v[x, y, nt - 1] = K('dark')
        rim = np.zeros((V, V), bool)                         # 焦げた縁（穴に接する板の天面）
        rim[1:-1, 1:-1] = (~mask[1:-1, 1:-1]) & (mask[:-2, 1:-1] | mask[2:, 1:-1] | mask[1:-1, :-2] | mask[1:-1, 2:])
        for x, y in np.argwhere(rim):
            if rng.random() < 0.6:
                h = int(height[x, y])
                if 0 < h <= ROOF_H and v[x, y, h - 1] != 0:
                    v[x, y, h - 1] = K('char_agb_l')
        for ys in range(cy, cy + d, 3):                      # 折れた板の端が穴の上へ垂れ下がる
            xs = [x for x in range(V) if mask[x, ys]]
            if not xs or rng.random() < 0.5:
                continue
            x0 = min(xs) if rng.random() < 0.5 else max(xs)
            step = 1 if x0 == min(xs) else -1
            h = int(height[x0, ys])
            for k in range(int(rng.integers(2, 4))):
                x = x0 + step * k
                if 0 <= x < V and mask[x, ys]:
                    nt = max(2, h - 3)
                    box(v, x, x + 1, ys, min(V, ys + 3), nt + 1 - k, nt + 3 - k, K('board_agb_d'))
        if big:                                              # 垂木（穴の中に見える）
            for yr in range(cy + 1, cy + d, 4):
                for x in range(V):
                    if mask[x, yr]:
                        nt = max(2, int(height[x, yr]) - 3)
                        v[x, yr, nt - 1:nt + 1] = K('char_agb')
    if variant in (7, 8):                                    # 帆布の当て布（面から 1 段盛り上がる。縁は暗い布、縄を 1 本）
        w, d = int(rng.integers(7, 11)), int(rng.integers(6, 10))
        cx = int(rng.integers(V // 2, V - 2 - w)) if flip else int(rng.integers(2, V // 2 - w))
        cy = int(rng.integers(2, V - 2 - d))
        for x in range(cx, cx + w):
            for y in range(cy, cy + d):
                h = int(height[x, y])
                zt = min(h, ROOF_H - 1)
                edge = (x in (cx, cx + w - 1)) or (y in (cy, cy + d - 1))
                v[x, y, zt] = K('cloth_agb_d') if edge else K('cloth_agb')
                if y == cy + d // 2:
                    v[x, y, zt] = K('lash')
    if variant not in (3, 4):
        return one(v)
    # 反った板（1 ボクセル持ち上がる）と欠けた板（1 ボクセル落ちる）。
    for _ in range(int(rng.integers(2, 4)) if variant == 3 else 0):
        bx, by = int(rng.integers(1, V - 4)), int(rng.integers(1, V - 3))
        for x in range(bx, bx + 3):
            for y in range(by, by + 2):
                base = int(height[x, y])
                if base < ROOF_H and v[x, y, base] == 0 and v[x, y, base - 1] != K('dark'):
                    v[x, y, base] = K('board_agb_l')
    for _ in range(int(rng.integers(3, 6)) if variant == 4 else 0):
        bx, by = int(rng.integers(1, V - 4)), int(rng.integers(1, V - 3))
        for x in range(bx, bx + 3):
            for y in range(by, by + 2):
                base = int(height[x, y])
                if base > EAVE + 1 and v[x, y, base - 1] in shades:
                    v[x, y, base - 1] = 0
                    v[x, y, base - 2] = K('board_agb_d')
    return one(v)


def house_entrance_agb(name):
    """板で補強した戸口 — 焦げた枠、灰色の板戸に斜めの当て板 2 本と横板。踏み石は欠けている。"""
    v = vol(h=32)
    box(v, 9, 23, 25, 31, 0, 3, K('rock_agb'))
    box(v, 9, 23, 25, 31, 2, 3, K('rock_agb_l'))
    box(v, 20, 23, 25, 28, 1, 3, 0)                          # 欠けた角
    box(v, 8, 24, 29, 32, 3, 28, K('timber_d'))              # 枠
    box(v, 8, 24, 29, 32, 24, 28, K('char_agb'))             # 焦げた鴨居
    box(v, 10, 22, 29, 32, 4, 24, 0)                         # 戸の開口
    box(v, 10, 22, 30, 31, 4, 24, K('board_agb'))            # 戸（1 ボクセル引っ込む）
    for x in range(12, 22, 3):
        box(v, x, x + 1, 30, 31, 4, 24, K('board_agb_d'))    # 板の継ぎ目
    for t in range(12):                                      # 斜めの当て板（X）
        z = 5 + int(round(t * 1.5))
        box(v, 10 + t, 11 + t, 31, 32, z, z + 3, K('board_agb_l'))
        box(v, 21 - t, 22 - t, 31, 32, z, z + 3, K('board_agb_l'))
    box(v, 10, 22, 31, 32, 21, 23, K('board_agb_l'))         # 横板
    box(v, 18, 20, 31, 32, 12, 14, K('iron_l'))              # 把手
    return one(v)


def house_light_agb(name):
    """窓の灯り。壁の窓（硝子は z 24..28）に合わせ、置く側の 0.40 マスの持ち上げを引いて z 11..15。"""
    v = vol(h=20)
    box(v, 12, 20, 29, 32, 11, 16, K('lit'))
    return one(v)


def chimney_agb(name):
    """傾いた石の煙突 — 5 段ごとに東へ 1 ボクセルずれ、上の角が崩れている。笠石は半分無い。"""
    #! 第 2 回: 初版（高さ 18）は小さくて何か分からなかった（レビュー担当の指摘）。
    #! **壁と同じ高さ（34）の四角い石積み**にし、8 段ごとに東へ 1 ボクセル傾け、先端を欠けさせる。
    rng = rng_for(name)
    H = 34
    flat = vol(h=H)
    mask = np.zeros((V, V), bool)
    mask[10:22, 10:22] = True
    heights = np.full((V, V), H, np.int32)
    heights[10:22, 10:22] = 30 + np.repeat(np.repeat(rng.integers(0, 5, size=(3, 3)), 4, axis=0), 4, axis=1)
    heights[18:22, 10:14] = 26                               # 欠けた先端の角
    _masonry_fill(flat, rng, mask, heights, _stone(), STONE_W)
    box(flat, 13, 19, 13, 19, 24, H, K('soot_agb'))          # 煙道（煤）
    box(flat, 14, 18, 14, 18, 28, H, K('char_agb'))
    v = vol(h=H)
    for z in range(H):                                       # 傾き（層ごとに東へずらす）
        v[:, :, z] = np.roll(flat[:, :, z], z // 8, axis=0)
    for z in range(0, 20, 6):                                # 煤の垂れ（南面と西面。塊で）
        dx = z // 8
        blk = v[10 + dx:22 + dx, 21:22, z:z + 3]
        blk[blk > 0] = K('soot_agb_l')
    blk = v[10:11, 12:20, 4:12]
    blk[blk > 0] = K('soot_agb_l')
    return one(v)


# ------------------------------------------------------------------ 廃墟
def _ruin_heights(rng, n, lo, hi, seg=(3, 6)):
    """ぎざぎざの天端（3〜5 の区間ごとに高さを変える）。"""
    out = np.zeros(n, np.int32)
    p = 0
    while p < n:
        w = int(rng.integers(*seg))
        out[p:p + w] = int(rng.integers(lo, hi))
        p += w
    return out


def _soot_base(v, rng, count=3):
    """土台のそばの煤（塊で）。"""
    for _ in range(count):
        x, y = int(rng.integers(0, V - 6)), int(rng.integers(0, V - 6))
        blk = v[x:x + 6, y:y + 6, 0:5]
        blk[blk > 0] = K('soot_agb_l')


def _char_beam(v, rng, p0, p1, r=1.6):
    rod(v, p0, p1, r, r, K('char_agb'))
    mid = [(p0[k] + p1[k]) / 2.0 for k in range(3)]
    rod(v, mid, p1, r, r, K('char_agb_l') if rng.random() < 0.5 else K('char_agb'))


def ruin_wall_agb(name):
    """**屋根の落ちた石の家の壁**（通れない瓦礫のマス。高さ z 14〜28）。変種:
    01 南北の壁 / 02 角（L 字） / 03 南面の壁に戸口の穴と楣 / 04 2 本の柱の残骸と瓦礫の山。
    どれも足元に瓦礫と焼けた梁。"""
    rng = rng_for(name)
    idx = int(name[-2:])
    v = vol(h=30)
    mask = np.zeros((V, V), bool)
    heights = np.zeros((V, V), np.int32)
    shades = _stone()
    if idx == 1:
        mask[5:10, 1:31] = True
        hs = _ruin_heights(rng, V, 14, 28)
        heights[5:10, :] = hs[None, :]
        _masonry_fill(v, rng, mask, heights, shades, STONE_W)
        _lumps(v, rng, 9, (11, 26), (4, 28), 8, shades + [K('char_agb')], [0.4, 0.2, 0.2, 0.2])
        _char_beam(v, rng, (12, 6, 1), (28, 24, 2))
    elif idx == 2:
        mask[2:7, 2:30] = True
        mask[2:30, 2:7] = True
        hx = _ruin_heights(rng, V, 12, 27)
        hy = _ruin_heights(rng, V, 12, 27)
        heights[:, 2:7] = hx[:, None]
        heights[2:7, :] = np.maximum(heights[2:7, :], hy[None, :])
        heights[2:7, 2:7] = 27                                # 角は高く残る
        _masonry_fill(v, rng, mask, heights, shades, STONE_W)
        box(v, 14, 20, 2, 7, 8, 16, 0)                       # 窓の穴
        box(v, 14, 20, 2, 7, 16, 18, K('char_agb'))          # 焦げた楣（窓の上）
        _lumps(v, rng, 8, (10, 28), (10, 28), 7, shades, STONE_W)
        _char_beam(v, rng, (9, 26, 1), (26, 12, 1))
    elif idx == 3:
        mask[1:31, 23:28] = True
        hs = _ruin_heights(rng, V, 18, 28)
        heights[:, 23:28] = hs[:, None]
        _masonry_fill(v, rng, mask, heights, shades, STONE_W)
        box(v, 12, 19, 23, 28, 0, 20, 0)                     # 戸口の穴
        box(v, 11, 20, 23, 28, 20, 23, K('char_agb'))        # 焦げた楣
        _lumps(v, rng, 5, (12, 19), (24, 30), 6, shades, STONE_W, span=(2, 5))
        _lumps(v, rng, 6, (3, 29), (6, 20), 7, shades + [K('char_agb')], [0.4, 0.2, 0.2, 0.2])
        _char_beam(v, rng, (4, 8, 1), (24, 18, 4))
    else:
        mask[2:8, 2:8] = True
        mask[24:30, 22:28] = True
        heights[2:8, 2:8] = 26
        heights[24:30, 22:28] = 19
        _masonry_fill(v, rng, mask, heights, shades, STONE_W)
        box(v, 6, 8, 2, 5, 21, 26, 0)                        # 柱の頭の欠け
        box(v, 24, 27, 26, 28, 15, 19, 0)
        _lumps(v, rng, 14, (6, 28), (6, 26), 10, shades + [K('char_agb')], [0.38, 0.22, 0.2, 0.2])
        #! 梁は**東西寄り**に倒す。南東からのカメラでは奥行き方向の斜めが立った黒い塔に見える（一覧の画像で気づいた）。
        _char_beam(v, rng, (7, 6, 25), (29, 13, 7), r=1.8)   # 柱から落ちかけた梁
        _char_beam(v, rng, (4, 26, 1), (22, 4, 2))
    _soot_base(v, rng)
    return one(v)


def ruin_low_agb(name):
    """**低い瓦礫の山**（通れる瓦礫のマス。高さ z 8 以下）。石塊と焼けた板が散らばる。"""
    rng = rng_for(name)
    idx = int(name[-2:])
    v = vol(h=12)
    shades = _stone()
    _lumps(v, rng, 10 + idx, (5, 27), (5, 27), 8, shades + [K('soot_agb')], [0.42, 0.24, 0.2, 0.14])
    for _ in range(int(rng.integers(2, 4))):                 # 焼けた板（横たわる）
        x, y = int(rng.integers(3, 20)), int(rng.integers(3, 27))
        ln = int(rng.integers(6, 12))
        if rng.random() < 0.5:
            box(v, x, x + ln, y, y + 2, 0, 2, K('char_agb'))
        else:
            box(v, x, x + 2, y, min(V, y + ln), 0, 2, K('char_agb'))
    if idx == 2:
        box(v, 18, 26, 18, 25, 0, 2, K('rock_agb_l'))        # 割れた敷石
        box(v, 21, 22, 18, 25, 0, 2, 0)
    if idx == 3:
        ball(v, 8, 22, 1, 4.0, K('cloth_agb_d'), squash=0.6)   # 半分埋もれた布
    return one(v)


def ruin_beam_agb(name):
    """焼けた梁が斜めに刺さった瓦礫。梁は角から角へ z 28 まで立ち上がる（通れない瓦礫の高さ）。"""
    rng = rng_for(name)
    v = vol(h=32)
    shades = _stone()
    _lumps(v, rng, 12, (5, 27), (5, 27), 10, shades + [K('soot_agb')], [0.42, 0.24, 0.2, 0.14])
    #! 梁は**東西寄り**に立てる（奥行き方向の斜めは南東からのカメラで立った黒い塔に見える）。
    rod(v, (3, 22, 1), (27, 11, 28), 2.2, 1.8, K('char_agb'))
    rod(v, (15, 16, 15), (27, 11, 28), 1.8, 1.8, K('char_agb_l'))
    box(v, 24, 30, 8, 14, 26, 30, K('char_agb'))             # 折れ口（太い）
    rod(v, (6, 8, 1), (26, 14, 3), 1.4, 1.4, K('char_agb'))  # 倒れたもう 1 本
    _soot_base(v, rng, 2)
    return one(v)


# ------------------------------------------------------------------ 溶岩と地面
def _tile(h):
    return vol(h=h)


def _ground(layer, under):
    v = _tile(2)
    box(v, 0, V, 0, V, 0, 1, under)
    v[:, :, 1] = layer
    return [('main', v, (0, 0, -2))]


def _walk_cracks(rng, layer, count, length=(8, 18)):
    """ひび（折れ線）。`layer` の該当を 0 にする（下の層が覗く）。"""
    for _ in range(count):
        x, y = int(rng.integers(0, V)), int(rng.integers(0, V))
        dx = 1 if rng.random() < 0.5 else -1
        dy = 1 if rng.random() < 0.5 else -1
        for _ in range(int(rng.integers(*length))):
            if 0 <= x < V and 0 <= y < V:
                layer[x, y] = 0
            if rng.random() < 0.5:
                x += dx
            else:
                y += dy
            if rng.random() < 0.1:
                dx = -dx


def lava_crust_agb(name):
    """溶岩の縁の冷えて固まった黒い岩 — 第 2 回で凹凸を付けた。**厚み 4・原点 −4**（地形の板と同じ）の中で、
    橙の底（z −4）・暗い皮（z −3）・盛り上がった板（天面 z −1 か 0）を作り、割れ目は皮を彫って底の橙を見せる。
    天面は z 0 を超えない（超えると `--prefab=` の見本が地面扱いをやめて床の板で隠す＝`view_modes.cpp` の
    `prefab_is_ground`。町でも床より上へ出る）。彫るのはタイルの中だけで底の橙は残す（「床より下へ彫ると必ず見える」）。
    自発光は置く側がインスタンスで足す。"""
    rng = rng_for(name)
    v = _tile(4)
    v[:, :, 0] = K('lava_o')
    v[:, :, 1] = K('crust')
    top = np.zeros((V, V), np.int16)                         # 皮の色（塊）
    G['_lump_face'](rng, top, [K('crust_l'), K('crust'), K('soot_agb'), K('rock_agb_d')], [0.36, 0.3, 0.2, 0.14],
                    span=(4, 12), count=40)
    hgt = np.full((V, V), 2, np.int16)                       # 皮の天面の層の数（2〜4）
    for _ in range(int(rng.integers(10, 16))):               # 盛り上がった板（重ねる）
        x, y = int(rng.integers(-2, V - 3)), int(rng.integers(-2, V - 3))
        w, d = int(rng.integers(4, 11)), int(rng.integers(3, 9))
        hgt[max(0, x):x + w, max(0, y):y + d] = int(rng.integers(3, 5))
    for _ in range(int(rng.integers(2, 5))):                 # 皮の薄い所（暗い皮だけ）
        x, y = int(rng.integers(0, V - 5)), int(rng.integers(0, V - 5))
        hgt[x:x + int(rng.integers(3, 7)), y:y + int(rng.integers(3, 6))] = 2
    for x in range(V):
        for y in range(V):
            v[x, y, 1:int(hgt[x, y])] = top[x, y]
            if hgt[x, y] >= 4:
                v[x, y, 1] = K('crust')                      # 高い板の側面の下は暗い皮
    cracks = np.ones((V, V), np.int16)
    _walk_cracks(rng, cracks, int(rng.integers(3, 6)), (10, 22))
    for x, y in np.argwhere(cracks == 0):
        v[x, y, 1:] = 0                                      # 皮を彫って橙を見せる
        v[x, y, 0] = K('lava_y') if rng.random() < 0.3 else K('lava_o')
    return [('main', v, (0, 0, -4))]


def ground_ash_agb(name):
    """灰と乾いた土 — 灰色の斑に、灰の吹き溜まり（明るい塊）と炭の粒。"""
    rng = rng_for(name)
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('ash_agb'), K('ash_agb_d'), K('pack_d'), K('ash_agb_l')], [0.38, 0.26, 0.22, 0.14])
    for _ in range(int(rng.integers(2, 5))):                 # 吹き溜まり
        x, y = int(rng.integers(0, V - 6)), int(rng.integers(0, V - 6))
        layer[x:x + int(rng.integers(3, 7)), y:y + int(rng.integers(2, 6))] = K('ash_agb_l')
    G['_grain'](rng, layer, layer > 0, [K('grit'), K('char_agb'), K('rock_agb_d')], [0.05, 0.03, 0.02], block=2)
    return _ground(layer, K('ash_agb_d'))


def ground_scorched_agb(name):
    """焦げた地面 — 黒い煤と焦げの斑。ひび（1 段彫る）から下の橙が覗く。"""
    rng = rng_for(name)
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('soot_agb'), K('soot_agb_l'), K('rock_agb_d'), K('char_agb')], [0.40, 0.24, 0.20, 0.16])
    _walk_cracks(rng, layer, int(rng.integers(3, 6)), (10, 22))
    v = _tile(2)
    box(v, 0, V, 0, V, 0, 1, K('soot_agb'))
    v[:, :, 0][layer == 0] = K('lava_o')
    v[:, :, 1] = layer
    return [('main', v, (0, 0, -2))]


def ground_cobble_agb(name):
    """踏み固めた石畳の残り — 灰と土の上に、石畳が 2 か所の塊で残っている（縁は欠ける）。"""
    rng = rng_for(name)
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('pack_d'), K('ash_agb_d'), K('pack'), K('dirt_d')], [0.36, 0.28, 0.2, 0.16])
    keep = np.zeros((V, V), bool)
    for _ in range(2):
        x, y = int(rng.integers(0, V - 10)), int(rng.integers(0, V - 10))
        keep[x:x + int(rng.integers(10, 20)), y:y + int(rng.integers(10, 20))] = True
    #! 石は**形の不揃いな矩形をばらばらに**置く（第 2 回。格子に並べると鉄の格子蓋に見えた——
    #! レビュー担当の指摘）。目地は地面の色のまま（暗い格子にしない）。石どうしは 1 ボクセル空ける。
    stones = [K('rock_agb_l'), K('rock_agb'), K('ash_agb_l'), K('rub_l')]
    placed = np.zeros((V, V), bool)
    for _ in range(90):
        w, d = int(rng.integers(3, 7)), int(rng.integers(3, 6))
        x, y = int(rng.integers(0, V - w)), int(rng.integers(0, V - d))
        p = 0.85 if keep[x:x + w, y:y + d].all() else 0.06
        if rng.random() > p:
            continue
        if placed[max(0, x - 1):x + w + 1, max(0, y - 1):y + d + 1].any():
            continue
        layer[x:x + w, y:y + d] = pick(rng, stones, [0.4, 0.3, 0.15, 0.15])
        placed[x:x + w, y:y + d] = True
        if rng.random() < 0.5:                               # 欠けた角
            cx, cy = (x, x + w - 1)[int(rng.integers(0, 2))], (y, y + d - 1)[int(rng.integers(0, 2))]
            layer[cx, cy] = K('pack_d')
    return _ground(layer, K('pack_d'))


# ------------------------------------------------------------------ 植物
def _dead_tree(rng, h, span, r_base, n_br, split=False, hollow=False, charred=False, stubs_only=False):
    """枯れ木の本体。幹は体積の中心のマス（接地は 1 マス）、枝は体積いっぱいに張る。
    `split` は雷に打たれて裂けた梢、`hollow` は幹のうろ、`charred` は焼け焦げ。
    `stubs_only` なら大枝を張らず、付け根の短い折れ口だけ残す（第 7 回。巨木は枝無し）。"""
    v = np.zeros((span, span, h), np.int16)
    cx = cy = (((span // V) // 2) * V) + (V // 2)
    bark = K('char_agb') if charred else K('bark_agb')
    bark_l = K('char_agb_l') if charred else K('bark_agb_l')
    trunk_top = int(h * float(rng.uniform(0.5, 0.6)))
    rod(v, (cx, cy, 0), (cx + 1, cy - 1, trunk_top), r_base, r_base * 0.4, bark)
    n_root = 4 if r_base < 4 else (6 if r_base < 9 else 8)
    for k in range(n_root):                                  # 根の張り出し
        ang = k * 6.283 / n_root + rng.uniform(-0.3, 0.3)
        rr = r_base * 2.2
        rod(v, (cx + np.cos(ang) * rr, cy + np.sin(ang) * rr, 0), (cx, cy, r_base * 1.2), max(1.0, r_base * 0.3), r_base * 0.7, bark)
    if stubs_only:                                           # 枝の付け根の折れ口だけ（巨木）
        for k in range(4):
            ang = (k * 6.283 / 4) + rng.uniform(-0.4, 0.4)
            z0 = trunk_top * rng.uniform(0.45, 0.9)
            ln = r_base * rng.uniform(1.2, 1.8)
            ex = cx + 1 + np.cos(ang) * ln
            ey = cy - 1 + np.sin(ang) * ln
            rod(v, (cx + 1, cy - 1, z0), (ex, ey, z0 + ln * 0.5), r_base * 0.45, r_base * 0.35, bark)
            ball(v, ex, ey, z0 + ln * 0.5, r_base * 0.4, K('char_agb_l'))
        n_br = 0
    for k in range(n_br):                                    # 大枝（高い位置から張り出す）
        ang = (k * 6.283 / n_br) + rng.uniform(-0.4, 0.4)
        z0 = trunk_top * rng.uniform(0.55, 0.98)
        broken = rng.random() < 0.35
        reach = (span / 2.0 - 3)
        ln = reach * (rng.uniform(0.35, 0.55) if broken else rng.uniform(0.7, 1.0))
        up = ln * rng.uniform(0.45, 0.9)
        ex = cx + 1 + np.cos(ang) * ln
        ey = cy - 1 + np.sin(ang) * ln
        colour = bark_l if (k % 2) else bark
        r_br = r_base * 0.45
        rod(v, (cx + 1, cy - 1, z0), (ex, ey, min(h - 2, z0 + up)), r_br, max(0.6, r_br * 0.35), colour)
        if broken:
            ball(v, ex, ey, min(h - 2, z0 + up), r_br * 0.6 + 0.6, K('char_agb_l'))   # 折れ口
        else:
            for _ in range(3):                                # 小枝
                a2 = ang + rng.uniform(-1.1, 1.1)
                l2 = ln * rng.uniform(0.25, 0.45)
                rod(v, (ex, ey, min(h - 2, z0 + up)), (ex + np.cos(a2) * l2, ey + np.sin(a2) * l2, min(h - 1, z0 + up + l2 * rng.uniform(0.3, 0.8))),
                    max(0.6, r_br * 0.4), 0.5, colour)
    if split or charred:                                     # 幹の縦の裂け目・焦げの筋（南面と東面）
        for k in range(3 if r_base < 8 else 6):
            za = int(h * rng.uniform(0.05, 0.45))
            zb = za + int(h * rng.uniform(0.1, 0.3))
            ang = rng.uniform(-0.3, 1.9)                         # 南〜東
            sx = int(round(cx + 0.5 + np.cos(ang + 1.57) * r_base * 0.9))
            sy = int(round(cy - 0.5 + np.sin(ang + 1.57) * r_base * 0.9))
            for z in range(za, min(zb, trunk_top - 2)):
                col = v[max(0, sx - 1):sx + 2, max(0, sy - 1):sy + 2, z]
                col[col != 0] = K('char_agb_l') if (k % 2) else K('dark')
    if split:                                                # 裂けた梢（2 本に割れて開く）
        for sgn in (-1, 1):
            rod(v, (cx + 1, cy - 1, trunk_top - r_base), (cx + 1 + sgn * r_base * 2.5, cy - 1 + sgn * r_base, h - 2), r_base * 0.4, 0.6, bark)
        box(v, cx - int(r_base * 0.6), cx + int(r_base * 0.6) + 2, cy - int(r_base * 0.6) - 1, cy + int(r_base * 0.6) + 1,
            trunk_top - 2, trunk_top + 2, K('char_agb_l'))
    elif rng.random() < 0.3:                                 # 梢が折れている
        box(v, cx - 1, cx + 3, cy - 2, cy + 2, trunk_top - 3, trunk_top + 2, K('char_agb_l'))
    else:                                                    # 梢
        rod(v, (cx + 1, cy - 1, trunk_top), (cx + 2 + r_base * 0.5, cy - 2 - r_base * 0.5, h - 1), r_base * 0.4, 0.5, bark)
    if hollow:                                               # うろ（南面の幹に暗い穴）
        hz = int(h * rng.uniform(0.12, 0.25))
        hr = max(1.5, r_base * 0.45)
        xs = np.arange(span)[:, None] + 0.5 - (cx + 0.5)
        zs = np.arange(h)[None, :] + 0.5 - hz
        m = ((xs / hr) ** 2 + (zs / (hr * 1.6)) ** 2) <= 1.0
        for y in range(int(cy + r_base * 0.2), min(span, int(cy + r_base) + 2)):
            layer = v[:, y, :]
            layer[m & (layer != 0)] = K('dark')
    v[:, :, 0][v[:, :, 0] > 0] = bark
    return one(v)


def dead_tree_agb(name):
    """葉の無い黒い枯れ木。第 6 回でアングウィルの木（48〜188 vox）と同じ帯に（決めたこと
    「アングヴィルのような高い木の枯れ木が欲しい」）: 01〜03 が低い木（48・64・88）、04 が中（118）、
    05〜08 が高い木（146・168・180・190）。幹の太さは高さの 4%（`tree_ang` と同じ比率）、
    大枝は幹の上部から体積いっぱいに張る。折れた大枝・裂けた梢・うろで表情を付ける。"""
    rng = rng_for(name)
    idx = int(name[-2:])
    h = {1: 48, 2: 64, 3: 88, 4: 118, 5: 146, 6: 168, 7: 180, 8: 190}.get(idx, 88)
    span = 64 if h < 100 else (96 if h < 168 else 128)
    r_base = max(2.6, h * 0.04)
    #! 第 7 回: 4 マス（128 vox）以上の木は大枝を半分に（決めたこと「4以上の高さの枯れ木は枝を半分」。
    #! 05〜08: 5 本→3 本・6 本→3 本。残す枝は幹の周りに等間隔なので向きが偏らない）。
    n_br = 4 if h < 100 else (5 if h < 160 else 6)
    if h >= 128:
        n_br = (n_br + 1) // 2
    return _dead_tree(rng, h, span, r_base, n_br, split=(idx in (6, 8)), hollow=(idx in (3, 5, 7)))


def great_dead_tree_agb(name):
    """巨大な枯れ木の目印（アングウィルの `great_tree_ang` に当たるもの。5×5 マスの体積・高さ 300〜320）。
    01 は雷に打たれて梢が裂けた古木（うろつき）、02 は焼け焦げた巨木。接地は中心の 1 マス。"""
    rng = rng_for(name)
    idx = _variant(name)
    h = 320 if idx == 1 else 300
    #! 第 7 回: 巨木は枝無し。付け根の折れ口だけ残し、裂け目と焦げの筋で表情を付ける。
    return _dead_tree(rng, h, 160, 13.0, 0, split=(idx == 1), hollow=True, charred=(idx == 2), stubs_only=True)


def dead_stump_agb(name):
    """**立ち枯れた巨木の根株**（決めたこと 2026-09-24「高さ1～２の低い木のうち何本かは立ち枯れた
    巨木の根が残ったようなものに」）。高さ 1〜2 マス、幹は巨木並みに太く（半径 12〜14＝1 マス幅弱）、
    太い根が四方へ 3 マス幅まで張り出して地面を掴む。上端は 01 折れて裂けたぎざぎざ / 02 焼けて炭の断面 /
    03 二つに裂けた高い破片。うろ・焦げ・裂け目つき。接地は中心の 1 マスだけ（根は隣のマスでは
    地面から 1 ボクセル浮かせて、置く側の足跡を 1 マスに保つ）。"""
    rng = rng_for(name)
    idx = _variant(name)
    h = {1: 44, 2: 36, 3: 62}.get(idx, 44)
    span = 96
    v = np.zeros((span, span, h), np.int16)
    cx = cy = V + (V // 2)
    r_base = {1: 13.0, 2: 14.0, 3: 12.0}.get(idx, 13.0)
    charred = (idx == 2)
    bark = K('char_agb') if charred else K('bark_agb')
    top = h - 6
    rod(v, (cx, cy, 0), (cx + 1, cy - 1, top), r_base, r_base * 0.85, bark)
    # 根（8 本。太く、遠くまで。付け根は高く、先へ行くほど地面へ）
    for k in range(8):
        ang = k * 6.283 / 8 + rng.uniform(-0.25, 0.25)
        ln = rng.uniform(30, 44)
        ex, ey = cx + np.cos(ang) * ln, cy + np.sin(ang) * ln
        rod(v, (cx, cy, r_base * 0.9), (ex, ey, 1.5), r_base * 0.45, 1.5, bark)
        if rng.random() < 0.6:                                # 根の途中の瘤・枝根
            a2 = ang + rng.uniform(-0.6, 0.6)
            l2 = ln * rng.uniform(0.5, 0.8)
            rod(v, (cx + np.cos(ang) * ln * 0.45, cy + np.sin(ang) * ln * 0.45, 5), (cx + np.cos(a2) * l2, cy + np.sin(a2) * l2, 1.5), 2.5, 1.2, bark)
    # 上端
    xs = np.arange(span)[:, None] + 0.5 - (cx + 0.5)
    ys = np.arange(span)[None, :] + 0.5 - (cy - 0.5)
    d2 = xs * xs + ys * ys
    if idx == 1:                                              # 折れて裂けたぎざぎざ（柱ごとに高さが違う）
        jag = rng.integers(0, 9, size=((span + 3) // 4, (span + 3) // 4))
        jag = np.repeat(np.repeat(jag, 4, 0), 4, 1)[:span, :span]
        for x in range(cx - 16, cx + 17):
            for y in range(cy - 16, cy + 17):
                zt = top - int(jag[x, y]) - (0 if d2[x, y] < (r_base * 0.6) ** 2 else 3)
                v[x, y, zt:] = 0
                col = v[x, y, :]
                nz = col.nonzero()[0]
                if len(nz) and rng.random() < 0.5:
                    col[nz.max()] = K('char_agb_l')
    elif idx == 2:                                            # 焼けて炭になった断面（少し窪む）
        m = d2 <= (r_base * 0.85) ** 2
        for x, y in np.argwhere(m):
            v[x, y, top - 2:] = 0
            v[x, y, top - 3] = K('char_agb_l') if rng.random() < 0.7 else K('dark')
    else:                                                     # 二つに裂けた高い破片
        for x in range(cx - 16, cx + 17):
            for y in range(cy - 16, cy + 17):
                if d2[x, y] > (r_base * 0.85) ** 2:
                    continue
                side = (x - cx) - (y - cy)
                zt = top if abs(side) > 5 else top - 22 - int(abs(side))
                if side > 5:
                    zt = top - int(rng.integers(0, 6))
                elif side < -5:
                    zt = top - 12 - int(rng.integers(0, 6))
                v[x, y, zt:] = 0
                col = v[x, y, :]
                nz = col.nonzero()[0]
                if len(nz) and rng.random() < 0.6:
                    col[nz.max()] = K('char_agb_l')
    # うろ（南面）
    hz = int(h * rng.uniform(0.25, 0.45))
    hr = r_base * 0.35
    zs = np.arange(h)[None, :] + 0.5 - hz
    m = ((xs[:, :1] / hr) ** 2 + (zs / (hr * 1.4)) ** 2) <= 1.0
    for y in range(int(cy + r_base * 0.3), min(span, int(cy + r_base) + 2)):
        layer = v[:, y, :]
        layer[m & (layer != 0)] = K('dark')
    # 縦の裂け目・焦げの筋
    for k in range(5):
        za = int(h * rng.uniform(0.05, 0.4))
        zb = za + int(h * rng.uniform(0.2, 0.5))
        ang = rng.uniform(-0.6, 2.2)
        sx = int(round(cx + 0.5 + np.cos(ang + 1.57) * r_base * 0.92))
        sy = int(round(cy - 0.5 + np.sin(ang + 1.57) * r_base * 0.92))
        for z in range(za, min(zb, top - 1)):
            col = v[max(0, sx - 1):sx + 2, max(0, sy - 1):sy + 2, z]
            col[col != 0] = K('char_agb_l') if (k % 2) else K('dark')
    # 接地は中心のマスだけ（隣のマスの根は 1 ボクセル浮かせる）
    ground = v[:, :, 0]
    keep = np.zeros((span, span), bool)
    keep[V:2 * V, V:2 * V] = True
    ground[~keep] = 0
    v[:, :, 0][keep & (ground > 0)] = bark
    return one(v)


def dead_grass_agb(name):
    """灰色の枯れ草の株 — 葉は倒れかけ、先へ行くほど寝る。"""
    rng = rng_for(name)
    v = vol(h=12)
    for _ in range(int(rng.integers(9, 14))):
        bx, by = int(rng.integers(8, V - 8)), int(rng.integers(8, V - 8))
        tall = int(rng.integers(4, 10))
        lean_x, lean_y = int(rng.integers(-5, 6)), int(rng.integers(-5, 6))
        blade = pick(rng, [K('wilt'), K('wilt_d'), K('straw_g')], [0.45, 0.35, 0.2])
        for z in range(tall):
            t = (z / float(tall)) ** 1.6
            x = min(V - 1, max(0, bx + int(round(lean_x * t))))
            y = min(V - 1, max(0, by + int(round(lean_y * t))))
            v[x, y, z] = blade
    return one(v)


# ------------------------------------------------------------------ 小物
def broken_barrel_agb(name):
    """割れた樽 — 片側の板が上まで割れて無く、輪は錆びて片方外れている。脇に外れた板。"""
    rng = rng_for(name)
    v = vol(h=16)
    for z in range(14):
        bulge = 1.0 - (((z - 7) / 7.0) ** 2) * 0.18
        disc(v, 14, 16, 6.6 * bulge, z, z + 1, K('board_agb') if (z % 4) else K('board_agb_l'))
    disc(v, 14, 16, 5.4, 2, 14, 0)                           # 中を空ける
    disc(v, 14, 16, 5.4, 1, 2, K('wa_dd'))                   # 溜まった雨水
    for z in (2, 11):
        disc(v, 14, 16, 6.9, z, z + 2, K('rust_agb'), hollow=5.6)
    xs = np.arange(V)[:, None] + 0.5 - 14
    ys = np.arange(V)[None, :] + 0.5 - 16
    ang = np.arctan2(ys, xs)
    wedge = (ang > -0.3) & (ang < 1.5)
    for z in range(6, 16):
        jag = 6 + (z % 3)
        m = wedge & ((xs * xs) + (ys * ys) > 9) & (np.abs(ang * 4.0 + z) % 3 < 3)
        v[:, :, z][m & (z >= jag)] = 0
    log_x(v, 27, 1, 1.2, 4, 18, K('board_agb_l'))            # 外れた板
    log_y(v, 25, 1, 1.2, 20, 30, K('board_agb'))
    return one(v)


def patched_cart_agb(name):
    """継ぎはぎの荷車 — 片輪が外れて荷台が傾き、石で支えてある。板は色の違う板で当ててある。
    荷台に帆布を掛けた荷と麻袋。"""
    v = vol(h=20)
    for y in range(9, 24):
        z = 7 - int(round((y - 9) * 4.0 / 14.0))
        box(v, 6, 26, y, y + 1, z, z + 3, K('board_agb'))
    for (xa, xb) in ((9, 12), (18, 22)):                     # 当て板
        for y in range(9, 24):
            z = 7 - int(round((y - 9) * 4.0 / 14.0))
            box(v, xa, xb, y, y + 1, z + 2, z + 3, K('board_agb_l'))
    box(v, 6, 26, 9, 10, 10, 14, K('board_agb_d'))           # あおり（北。欠けている）
    box(v, 14, 18, 9, 10, 12, 14, 0)
    box(v, 6, 7, 9, 23, 7, 12, K('board_agb_d'))             # あおり（西）
    log_y(v, 12, 6, 6.0, 7, 9, K('timber'))                  # 残った車輪（北）
    log_y(v, 12, 6, 4.4, 7, 9, 0)
    box(v, 11, 13, 7, 9, 0, 12, K('timber_d'))
    box(v, 6, 18, 7, 9, 5, 7, K('timber_d'))
    log_y(v, 12, 6, 1.5, 6, 10, K('rust_agb'))
    box(v, 9, 15, 23, 26, 0, 4, K('rock_agb'))               # 支えの石（南。車輪が無い側）
    log_x(v, 24, 4, 1.2, 8, 16, K('rust_agb'))               # 折れた車軸
    box(v, 25, 31, 12, 14, 1, 3, K('board_agb_d'))           # 梶棒（下ろしてある）
    box(v, 25, 31, 18, 20, 1, 3, K('board_agb_d'))
    ball(v, 12, 15, 11, 4.5, K('cloth_d'), squash=0.8)       # 麻袋
    box(v, 16, 25, 11, 21, 9, 14, K('cloth_agb'))            # 帆布を掛けた荷
    box(v, 16, 25, 11, 21, 13, 14, K('cloth_agb_d'))
    box(v, 20, 21, 11, 21, 9, 15, K('lash'))
    return one(v)


def campfire_agb(name):
    """焚き火 — 焦げた石を環に組み、燃え残りの薪と新しい薪。脇に腰掛けの丸太。火は `campfire_fire_agb`。"""
    #! 第 2 回: 輪を 1 つの環にし腰掛けの丸太を添えた形は焚き火に読めなかった（レビュー担当の指摘）。
    #! **石を 1 個ずつ輪に並べ、中に灰を敷き、薪を交差させる**。火は中心 (16,16)。
    rng = rng_for(name)
    v = vol(h=14)
    disc(v, 16, 16, 7.0, 0, 1, K('ash_agb_d'))               # 灰
    disc(v, 16, 16, 4.0, 0, 2, K('char_agb'))                # 燃え跡
    for k in range(9):                                       # 石の輪（1 個ずつ）
        a = k * 0.698 + 0.2
        x, y = int(round(16 + 9.0 * np.cos(a))), int(round(16 + 9.0 * np.sin(a)))
        w, d, h = int(rng.integers(3, 5)), int(rng.integers(3, 5)), int(rng.integers(3, 5))
        box(v, x - w // 2, x - w // 2 + w, y - d // 2, y - d // 2 + d, 0, h, pick(rng, _stone(), STONE_W))
    log_x(v, 16, 3, 1.5, 9, 23, K('char_agb_l'))             # 交差した薪
    log_y(v, 16, 3, 1.5, 9, 23, K('char_agb'))
    rod(v, (10, 10, 2), (22, 22, 6), 1.4, 1.4, K('oak'))     # 新しい薪（斜めに載る）
    rod(v, (22, 10, 2), (10, 22, 6), 1.4, 1.4, K('oak_l'))
    return one(v)


def campfire_fire_agb(name):
    v = vol(h=18)
    disc(v, 16, 16, 3.5, 5, 9, K('fl_mid'))
    disc(v, 16, 16, 2.0, 8, 14, K('fl_core'))
    disc(v, 16, 16, 5.0, 4, 5, K('emb_l'))
    return one(v)


def boarded_well_agb(name):
    """板で補強した井戸 — 石の井筒に板を渡して蓋にし、櫓は片方の柱だけ残って桁が落ちかけている。"""
    rng = rng_for(name)
    v = vol(h=30)
    for z in range(0, 12, 3):
        disc(v, 16, 16, 9.0, z, z + 3, K('rock_agb'), hollow=6.0)
        ang0 = rng.random() * 6.28
        for k in range(6):
            a = ang0 + (k * 1.047)
            x, y = int(16 + 7.5 * np.cos(a)), int(16 + 7.5 * np.sin(a))
            box(v, x, x + 1, y, y + 1, z, z + 3, K('rock_agb_d'))
    disc(v, 16, 16, 9.0, 11, 12, K('rock_agb_l'), hollow=6.0)
    disc(v, 16, 16, 6.0, 0, 2, K('wa_dd'))
    for i, y in enumerate(range(8, 25, 4)):                  # 渡した板（1 枚は折れて短い）
        colour = K('board_agb_d') if (i == 1) else K('board_agb')
        x1 = 18 if (i == 3) else 26
        box(v, 6, x1, y, y + 3, 12, 14, colour)
    box(v, 7, 10, 15, 18, 0, 26, K('timber_d'))              # 残った柱
    box(v, 8, 10, 15, 18, 22, 26, 0)                         # 柱の頭の欠け
    box(v, 24, 27, 15, 18, 0, 7, K('timber_d'))              # 折れた柱の根
    for k in range(15):                                      # 落ちかけた桁（斜め）
        box(v, 10 + k, 12 + k, 15, 18, 25 - int(k * 1.3), 27 - int(k * 1.3), K('log_d'))
    box(v, 8, 9, 16, 17, 14, 24, K('lash'))                  # 縄
    log_y(v, 28, 3, 3.0, 22, 30, K('oak_l'))                 # 倒れた桶
    log_y(v, 28, 3, 2.0, 23, 30, 0)
    return one(v)


def forge_agb(name):
    """鍛冶の炉（武器屋の前庭） — 煤けた石の炉に傾いた煙突。崩れた所は鉄板で当ててある。
    鞴は皮が破れている。火は `forge_fire_agb`（昼も光る）。"""
    rng = rng_for(name)
    v = vol(h=44)
    shades = _stone()
    box(v, 2, 30, 6, 30, 0, 14, K('rock_agb_d'))             # 炉体
    for z in range(1, 13, 3):
        p = 2
        while p < 30:
            w = int(rng.integers(4, 8))
            shade = pick(rng, shades, STONE_W)
            box(v, p, p + w, 29, 30, z, z + 2, shade)
            box(v, p, p + w, 6, 7, z, z + 2, shade)
            box(v, 2, 3, 6 + p, 6 + p + w, z, z + 2, shade)
            box(v, 29, 30, 6 + p, 6 + p + w, z, z + 2, shade)
            p += w + 1
    box(v, 2, 30, 6, 30, 0, 4, K('soot_agb'))                # 煤けた足元
    box(v, 6, 26, 10, 26, 14, 16, K('soot_agb'))             # 火床
    box(v, 8, 24, 12, 24, 15, 17, K('char_agb'))
    box(v, 6, 26, 6, 9, 14, 20, K('rock_agb_l'))             # 前の火屋の縁
    box(v, 12, 20, 6, 9, 14, 20, 0)                          # 縁の欠け
    for z in range(14, 40):                                  # 煙突（東へ傾く）
        dx = (z - 14) // 8
        box(v, 10 + dx, 22 + dx, 22, 30, z, z + 1, K('rock_agb_d'))
    for z in range(16, 38, 3):
        dx = (z - 14) // 8
        box(v, 10 + dx, 22 + dx, 22, 23, z, z + 2, K('rock_agb') if (z // 3) % 2 else K('rock_agb_l'))
    box(v, 12, 20, 21, 22, 20, 28, K('iron'))                # 当てた鉄板（前面）
    box(v, 13, 15, 21, 22, 22, 26, K('rust_agb'))
    box(v, 19 + 3, 23 + 3, 22, 30, 34, 40, 0)                # 煙突の崩れた角
    box(v, 12 + 3, 20 + 3, 24, 28, 38, 42, K('soot_agb'))
    box(v, 26, 32, 12, 22, 14, 19, K('fur_d'))               # 鞴（皮が破れている）
    box(v, 28, 30, 14, 17, 16, 19, 0)
    box(v, 26, 32, 13, 21, 19, 21, K('board_agb'))
    box(v, 30, 32, 16, 18, 21, 29, K('timber_d'))            # 鞴の柄
    return one(v)


def forge_fire_agb(name):
    v = vol(h=44)
    box(v, 10, 22, 13, 23, 16, 19, K('fl_mid'))
    box(v, 13, 19, 15, 21, 17, 21, K('fl_core'))
    box(v, 8, 24, 12, 24, 16, 17, K('emb_l'))
    return one(v)


def cauldron_agb(name):
    """大釜（錬金術店の前庭） — 焦げた 3 本の棒の三脚から鎖で吊った錆びた鉄釜。中身は緑。
    火は `cauldron_fire_agb`。"""
    #! 第 2 回: 三脚から吊る形は人の形に見えた（レビュー担当の指摘）。**3 本脚の上に丸い鉄の鍋**へ。
    v = vol(h=24)
    for (x, y) in ((9, 11), (23, 11), (16, 23)):             # 短い 3 本脚
        box(v, x - 1, x + 1, y - 1, y + 1, 0, 8, K('iron'))
    R = 8.5
    for z in range(6, 21):                                   # 横から見て丸い鍋
        t = (z - 13.0) / R
        r = R * float(np.sqrt(max(0.0, 1.0 - t * t)))
        disc(v, 16, 16, r, z, z + 1, K('iron'))
    for z in range(15, 21):                                  # 中を空ける
        t = (z - 13.0) / R
        r = R * float(np.sqrt(max(0.0, 1.0 - t * t))) - 1.5
        disc(v, 16, 16, r, z, z + 1, 0)
    disc(v, 16, 16, 6.2, 16, 17, K('slime'))                 # 緑の液面（縁より 1 段下）
    disc(v, 16, 16, 2.0, 17, 18, K('slime_l'))
    disc(v, 16, 16, 7.4, 19, 21, K('iron'), hollow=6.0)      # 縁（少し張り出す）
    for (x, y, z) in ((10, 13, 9), (20, 18, 12), (13, 20, 14)):   # 錆（塊）
        blk = v[x:x + 3, y:y + 2, z:z + 3]
        blk[blk == K('iron')] = K('rust_agb')
    box(v, 6, 8, 15, 17, 14, 18, K('iron'))                  # 取っ手
    box(v, 24, 26, 15, 17, 14, 18, K('iron'))
    log_x(v, 16, 1, 1.4, 9, 23, K('char_agb'))               # 薪（燃え残り）
    log_y(v, 16, 1, 1.4, 9, 23, K('oak'))
    return one(v)


def cauldron_fire_agb(name):
    v = vol(h=24)
    disc(v, 16, 16, 4.0, 1, 4, K('fl_mid'))
    disc(v, 16, 16, 2.0, 3, 6, K('fl_core'))
    disc(v, 16, 16, 2.0, 17, 20, K('slime_l'))               # 泡
    disc(v, 12, 14, 1.0, 17, 19, K('slime_l'))
    return one(v)


# ------------------------------------------------------------------ 半壊した店（第 3 回）
RUIN_H = 18   #: 崩れた側の壁の体積の高さ。屋根（焦げた垂木）はこの高さに載る


def house_wall_ruin_agb(name):
    """**半壊した店の崩れた側の壁 1 マス**（決めたこと 2026-09-24「建物を半分壊した状態で、
    内部が見える」）。煤けた土台の上に床板、外に面した辺には低くぎざぎざに崩れた石の壁
    （z 7〜17）、中には瓦礫・落ちた梁・倒れた棚。内側（隣のマスに続く側）は開けておく。"""
    north, south, west, east = _outward(name)
    rng = rng_for(name)
    v = vol(h=RUIN_H)
    box(v, 0, V, 0, V, 0, PLINTH, K('soot_agb'))
    box(v, 0, V, 0, V, PLINTH - 1, PLINTH, K('soot_agb_l'))
    x0 = INSET if west else 0
    x1 = V - INSET if east else V
    y0 = INSET if north else 0
    y1 = V - INSET if south else V
    # 床板（東西に走る板。所々抜けて暗い）。
    box(v, x0, x1, y0, y1, PLINTH, PLINTH + 2, K('board_agb_d'))
    for y in range(y0, y1, 4):
        box(v, x0, x1, y, y + 3, PLINTH + 1, PLINTH + 2, K('board_agb') if (y // 4) % 2 else K('board_agb_l'))
    for _ in range(int(rng.integers(2, 4))):
        mx, my = int(rng.integers(x0, max(x0 + 1, x1 - 6))), int(rng.integers(y0, max(y0 + 1, y1 - 3)))
        box(v, mx, mx + int(rng.integers(3, 7)), my, my + 3, PLINTH, PLINTH + 2, K('dark'))
    # 崩れた低い壁（外に面した辺だけ。厚み 3。天端は 4 ボクセルごとに高さが違う）。
    faces = [s for s, on in (('n', north), ('s', south), ('w', west), ('e', east)) if on]
    shades = _stone()
    for side in faces:
        (fx, fy), (bx, by) = _face_of(x0, x1, y0, y1, side)
        along_x = side in ('n', 's')
        lo, hi = (x0, x1) if along_x else (y0, y1)
        if side == 'n':
            box(v, x0, x1, y0, y0 + 3, PLINTH, RUIN_H, K('rock_agb'))
        elif side == 's':
            box(v, x0, x1, y1 - 3, y1, PLINTH, RUIN_H, K('rock_agb'))
        elif side == 'w':
            box(v, x0, x0 + 3, y0, y1, PLINTH, RUIN_H, K('rock_agb'))
        else:
            box(v, x1 - 3, x1, y0, y1, PLINTH, RUIN_H, K('rock_agb'))
        _stone_face(v, rng, fx, fy, bx, by, along_x, lo, hi, PLINTH, RUIN_H - 1, shades, STONE_W)
        tops = _ruin_heights(rng, V, 7, RUIN_H - 1, seg=(3, 6))
        for p in range(lo, hi):
            top = int(tops[p])
            if along_x:
                ya, yb = (y0, y0 + 3) if side == 'n' else (y1 - 3, y1)
                v[p, ya:yb, top:] = 0
            else:
                xa, xb = (x0, x0 + 3) if side == 'w' else (x1 - 3, x1)
                v[xa:xb, p, top:] = 0
    # 中の瓦礫・落ちた梁・倒れた棚。
    _lumps(v, rng, int(rng.integers(3, 6)), (max(4, x0 + 3), min(28, x1 - 3)), (max(4, y0 + 3), min(28, y1 - 3)),
           PLINTH + 5, shades + [K('char_agb')], [0.4, 0.2, 0.2, 0.2], span=(2, 5))
    if rng.random() < 0.7:
        bx0, by0 = int(rng.integers(4, 12)), int(rng.integers(4, 28))
        rod(v, (bx0, by0, PLINTH + 1), (bx0 + int(rng.integers(12, 20)), int(rng.integers(4, 28)), PLINTH + 3), 1.5, 1.5, K('char_agb'))
    if _slice(name) in ('mid', 'n', 's', 'w', 'e') and rng.random() < 0.6:   # 倒れた棚（板 2 枚と横板）
        sx, sy = int(rng.integers(6, 18)), int(rng.integers(6, 20))
        box(v, sx, sx + 10, sy, sy + 2, PLINTH + 2, PLINTH + 4, K('board_agb'))
        box(v, sx, sx + 10, sy + 5, sy + 7, PLINTH + 2, PLINTH + 4, K('board_agb_d'))
        box(v, sx + 1, sx + 2, sy, sy + 7, PLINTH + 4, PLINTH + 6, K('board_agb_l'))
        box(v, sx + 8, sx + 9, sy, sy + 7, PLINTH + 4, PLINTH + 6, K('board_agb_l'))
    for _ in range(2):                                       # 床の焦げ
        cx, cy = int(rng.integers(x0, max(x0 + 1, x1 - 5))), int(rng.integers(y0, max(y0 + 1, y1 - 5)))
        blk = v[cx:cx + 5, cy:cy + 5, PLINTH + 1:PLINTH + 2]
        blk[blk > 0] = K('char_agb_l')
    return one(v)


def house_roof_ruin_agb(name):
    """半壊した店の崩れた側の屋根 — 焼けた垂木の残りだけ（低い壁の上に載る）。無いマスもある。"""
    rng = rng_for(name)
    v = vol(h=8)
    #! **空の体積は作らない**（読み込みで空の模型になる）。少なくとも 1 本は残す。
    keep = [rng.random() < 0.6 for _ in range(3)]
    if not any(keep):
        keep[int(rng.integers(0, 3))] = True
    for k, y in enumerate((5, 15, 25)):                      # 垂木（東西。折れて短いものも）
        if keep[k]:
            xa = int(rng.integers(0, 6))
            xb = int(rng.integers(xa + 8, V + 1))
            box(v, xa, xb, y, y + 2, 1, 3, K('char_agb') if rng.random() < 0.7 else K('char_agb_l'))
    if rng.random() < 0.5:                                   # 棟木の残り（南北）
        box(v, 15, 17, int(rng.integers(0, 8)), int(rng.integers(16, V + 1)), 3, 5, K('char_agb'))
    return one(v)


# ------------------------------------------------------------------ 岩山（第 3 回。ダンジョンの入口と外周の花崗岩）
def crag_agb(name):
    """焦げた灰色の岩山。形は既存の `crag()`（段は名前の 2 語目 low/mid/high/peak）で、
    色だけ煤けた灰にする（`crag()` は uint8 の配列なので共通の色を使う）。"""
    #! 段ごとの高さは変愚の岩山より**高く**する（low 2.0 マス〜peak 4.25 マス）。階段を囲む
    #! 馬蹄形は 1 マス幅なので全部が low 段になり、変愚の 1.4 マスでは廃墟の壁と見分けが
    #! つかなかった（実機の絵で確かめた）。色は廃墟の明るい石より暗い焦げ茶の岩。
    tier = name.split('_')[1]
    heights = {'low': 64, 'mid': 88, 'high': 112, 'peak': 136}
    bands = [K('obs'), K('rock_dd'), K('crust_l'), K('rock_d'), K('soot')]
    face_mix = ([K('rock_dd'), K('rock_d'), K('soot')], [0.5, 0.3, 0.2])
    return G['crag'](name, h_override=heights[tier], bands=bands, face_mix=face_mix)


def cave_mouth_agb(name):
    """**ダンジョンの入口（洞窟の口）** — 下り階段のマスの絵。地面ごと置き換える（原点 −8。
    `stairs_down` と同じ流儀で、段は**北へ向かって暗がりへ下りる**）。両脇に暗い岩の柱、
    上に岩の楣が渡り、その下は黒い口。南のカメラから黒い口と段が見える。
    決めたこと 2026-09-24「ダンジョンの入り口は周囲を瓦礫と岩山で囲み、はっきりわかるように」。"""
    rng = rng_for(name)
    H0 = 8
    v = vol(h=H0 + 40)
    box(v, 0, V, 0, V, 0, H0, K('soot_agb'))
    box(v, 0, V, 0, V, H0 - 1, H0, K('ash_agb_d'))
    steps = ((24, 32, 8), (18, 24, 6), (12, 18, 4), (6, 12, 2), (0, 6, 1))
    for y0, y1, top in steps:                                # 段（北へ下りる）
        box(v, 5, 27, y0, y1, top, H0, 0)
        if top > 1:
            box(v, 5, 27, y0, y1, top - 1, top, K('rock_agb'))
    box(v, 5, 27, 0, 6, 0, 1, K('dark'))                     # 最奥は闇
    # 両脇の岩の柱（層理の帯。天端はぎざぎざ）。
    bands = [K('obs'), K('rock_dd'), K('crust_l'), K('rock_d'), K('soot')]
    for (xa, xb) in ((0, 5), (27, 32)):
        for z in range(H0, H0 + 34):
            box(v, xa, xb, 0, 20, z, z + 1, bands[min(4, (z - H0) // 7)])
        for x in range(xa, xb):
            for y in range(0, 20, 3):
                cut = int(rng.integers(0, 5))
                box(v, x, x + 1, y, y + 3, H0 + 34 - cut, H0 + 34, 0)
    box(v, 0, V, 0, 14, H0 + 26, H0 + 36, K('rock_dd'))      # 楣（岩）
    for _ in range(6):                                       # 楣の天端の岩塊
        x, y = int(rng.integers(0, V - 5)), int(rng.integers(0, 12))
        box(v, x, x + int(rng.integers(3, 7)), y, y + int(rng.integers(2, 5)), H0 + 36, H0 + 36 + int(rng.integers(1, 4)), K('rock_d'))
    box(v, 5, 27, 0, 14, H0, H0 + 26, K('dark'))             # 黒い口
    box(v, 5, 27, 14, 15, H0, H0 + 26, K('dark'))
    for x in (6, 25):                                        # 口の縁の割れた岩
        box(v, x, x + 2, 12, 16, H0, H0 + 6, K('rock_dd'))
    box(v, 0, 5, 20, 26, H0, H0 + 5, K('rock_dd'))           # 足元の崩れ
    box(v, 27, 32, 18, 24, H0, H0 + 4, K('rock_d'))
    return [('main', v, (0, 0, -H0))]


# ------------------------------------------------------------------ 柵（第 4 回）
FENCE_H = 14


def fence_agb(name):
    """前庭の柵（割り木の横桟。辺境と同じ高さ 14）。変種 01〜06 で壊れ具合が違う
    （決めたこと 2026-09-24「柵は壊れた柵をつくり一部を差し替えよう」）:
    01〜03 傷んだだけ / 04 杭が 1 本傾き桟が 1 本外れて垂れる / 05 杭が折れて桟が地面へ落ちる /
    06 区画まるごと倒れて地面に横たわる。名前は `fence_<n|s|w|e>_agb[_NN]`。"""
    parts = name.split('_')
    side = parts[1]
    variant = _variant(name)
    rng = rng_for(name)
    th = 4
    v = vol(h=FENCE_H)
    lo = 0 if side in ('n', 'w') else V - th
    hi = lo + th

    def put(a0, a1, z0, z1, colour, off=0):
        if side in ('n', 's'):
            box(v, a0, a1, lo + off, hi + off, z0, z1, colour)
        else:
            box(v, lo + off, hi + off, a0, a1, z0, z1, colour)

    def rod_side(pa, pb, za, zb, colour, r=1.4):
        """柵の面に沿った棒（斜めに垂れた桟・倒れた杭）。"""
        c = lo + th / 2.0
        if side in ('n', 's'):
            rod(v, (pa, c, za), (pb, c, zb), r, r, colour)
        else:
            rod(v, (c, pa, za), (c, pb, zb), r, r, colour)

    shades = [K('board_agb'), K('board_agb_l'), K('board_agb_d')]
    posts = list(range(1, V, 10))
    if variant == 6:                                         # 倒れた柵（横たわる）
        off = 4 if side in ('n', 'w') else -4
        for z0 in (0, 2):
            p = 0
            while p < V:
                w = int(rng.integers(9, 16))
                put(p, min(V, p + w), z0, z0 + 2, pick(rng, shades), off=off + (z0 // 2) * 3)
                p += w
        for p in posts:
            put(p, p + 3, 0, 3, K('timber_d'), off=off * 2)
        put(6, 12, 0, 6, K('timber_d'))                      # 1 本だけ折れて立つ根元
        return one(v)
    fallen = 5 if variant == 5 else -1
    for k, (z0, z1) in enumerate(((4, 7), (9, 12))):
        p = 0
        while p < V:
            w = int(rng.integers(9, 16))
            shade = pick(rng, shades, [0.5, 0.3, 0.2])
            a, b = p, min(V, p + w)
            skip = False
            if (variant == 4) and (k == 1) and (a <= 16 < b):
                rod_side(a, b, z1, 1, K('board_agb_d'))      # 外れて垂れた桟
                skip = True
            if (variant == 5) and (k == 1) and (a <= 21 < b):
                rod_side(a, b, z1 - 1, 0, K('board_agb'))    # 折れた杭から落ちた桟
                skip = True
            if not skip:
                put(a, b, z0 + int(rng.integers(0, 2)), z1, shade)
            p += w
    for i, p in enumerate(posts):
        if (variant == 5) and (i == 2):
            put(p, p + 3, 0, 6, K('timber_d'))               # 折れた杭（根元だけ）
            put(p + 1, p + 3, 5, 6, K('char_agb_l'))
            continue
        if (variant == 4) and (i == 1):
            rod_side(p + 1, p + 4, 0, FENCE_H - 1, K('timber'), r=1.5)   # 傾いた杭
            continue
        put(p, p + 3, 0, FENCE_H - 1, K('timber'))
        put(p, p + 3, FENCE_H - 1, FENCE_H, K('timber_l'))
    if variant == 3:                                         # 桟が 1 本欠けている
        put(11, 20, 9, 12, 0)
    return one(v)


# ------------------------------------------------------------------ 泥と轍（第 4 回。辺境の `outpost_village.py` と同じ考え）
def _rut_curve(rng, base, amp):
    """マスの縁で `base`、中で ±amp うねる轍の中心線（隣のマスと縁で揃う）。"""
    return [base + int(round(amp * np.sin(np.pi * (i + 0.5) / V))) for i in range(V)]


def _ruts(layer, rng, along_x, colours, base_pair=(10, 21), width=3):
    rut, water = colours
    for base in base_pair:
        line = _rut_curve(rng, base, float(rng.choice([-3.0, -2.0, 2.0, 3.0])))
        for i in range(V):
            for d in range(-(width // 2), (width // 2) + 1):
                q = line[i] + d
                if 0 <= q < V:
                    if along_x:
                        layer[i, q] = rut
                    else:
                        layer[q, i] = rut
        p = int(rng.integers(0, 6))
        while p < V:                                         # 轍に溜まった黒い水（切れ切れ）
            ln = int(rng.integers(3, 8))
            for i in range(p, min(V, p + ln)):
                c = line[i]
                if 0 <= c < V:
                    if along_x:
                        layer[i, c] = water
                    else:
                        layer[c, i] = water
            p += ln + int(rng.integers(4, 12))


def _hoof_marks(rng, layer, n, colour):
    for _ in range(n):
        px, py = int(rng.integers(0, V - 2)), int(rng.integers(0, V - 3))
        layer[px:px + 2, py:py + 3] = colour


def _cobble_remains(rng, layer, n):
    """泥に埋もれ残った石畳の石（灰の町らしさ）。"""
    for _ in range(n):
        x, y = int(rng.integers(0, V - 4)), int(rng.integers(0, V - 3))
        layer[x:x + int(rng.integers(3, 5)), y:y + 3] = pick(rng, [K('rock_agb_l'), K('rock_agb')])


def _mud_layer(rng):
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('soot_agb_l'), K('ash_agb_d'), K('soot_agb'), K('rock_agb_j')], [0.42, 0.26, 0.18, 0.14])
    return layer


def _churn(layer, rng, rut, water, n_blobs, n_marks):
    for _ in range(int(rng.integers(3, 6))):                 # 短い切れ端の轍
        along_x = rng.random() < 0.5
        base = int(rng.integers(4, V - 4))
        p0 = int(rng.integers(0, V - 8))
        ln = int(rng.integers(6, 14))
        for i in range(p0, min(V, p0 + ln)):
            c = base + int(round(2.0 * np.sin(np.pi * (i - p0) / max(1, ln))))
            for d in (-1, 0, 1):
                q = c + d
                if 0 <= q < V:
                    if along_x:
                        layer[i, q] = rut
                    else:
                        layer[q, i] = rut
    for _ in range(n_blobs):
        x, y = int(rng.integers(0, V - 8)), int(rng.integers(0, V - 6))
        w, d = int(rng.integers(5, 10)), int(rng.integers(4, 8))
        layer[x:x + w, y:y + d] = rut
        if rng.random() < 0.7:
            layer[x + 1:x + w - 1, y + 1:y + d - 1] = water
    _hoof_marks(rng, layer, n_marks, rut)


MUD_RUT = None   #: 轍の色（黒い灰の泥）。register の後で埋める


def ground_mud_agb(name):
    """ぬかるみ（轍の無い所）— 黒っぽい灰の泥に水たまりと足跡、埋もれた石畳の石。大通りの脇。"""
    rng = rng_for(name)
    layer = _mud_layer(rng)
    for _ in range(int(rng.integers(0, 2))):                 # 水たまり（0〜1）
        x, y = int(rng.integers(0, V - 9)), int(rng.integers(0, V - 7))
        w, d = int(rng.integers(6, 11)), int(rng.integers(4, 8))
        layer[x:x + w, y:y + d] = K('soot_agb')
        layer[x + 1:x + w - 1, y + 1:y + d - 1] = K('wa_agb')
    _hoof_marks(rng, layer, int(rng.integers(6, 12)), K('soot_agb'))
    _cobble_remains(rng, layer, int(rng.integers(2, 5)))
    return _ground(layer, K('soot_agb'))


def _mud_dir(name, kind):
    """向きつきのぬかるみ。`ew`＝東西の轍、`ns`＝南北、`x`＝交差点（踏み荒らされる）。"""
    rng = rng_for(name)
    layer = _mud_layer(rng)
    cols = (K('soot_agb'), K('wa_agb'))
    if kind == 'ew':
        _ruts(layer, rng, True, cols)
        _hoof_marks(rng, layer, int(rng.integers(4, 9)), K('soot_agb'))
    elif kind == 'ns':
        _ruts(layer, rng, False, cols)
        _hoof_marks(rng, layer, int(rng.integers(4, 9)), K('soot_agb'))
    else:
        _churn(layer, rng, K('soot_agb'), K('wa_agb'), int(rng.integers(3, 5)), int(rng.integers(14, 22)))
    _cobble_remains(rng, layer, int(rng.integers(1, 4)))
    return _ground(layer, K('soot_agb'))


def _mud_edge(name, side):
    """ぬかるみの縁 — 外側は灰の地面、中央側は泥。マスの中で泥の割合が増える（辺境と同じ曲線。
    草地の代わりに灰）。`side` は**灰のある側**。"""
    rng = rng_for(name)
    mud = _mud_layer(rng)
    ash = np.zeros((V, V), np.int16)
    G['_mottle'](rng, ash, [K('ash_agb'), K('ash_agb_d'), K('pack_d'), K('ash_agb_l')], [0.38, 0.26, 0.22, 0.14])
    mid = float(rng.uniform(0.35, 0.65))
    width = float(rng.uniform(0.25, 0.6))
    amp = float(rng.uniform(0.05, 0.14))
    phase = float(rng.uniform(0.0, 6.28))
    layer = np.zeros((V, V), np.int16)
    for bi in range(0, V, 2):
        for bj in range(0, V, 2):
            along, across = (bi, bj) if side in ('n', 's') else (bj, bi)
            tt = (across + 1.0) / V
            if side in ('s', 'e'):
                tt = 1.0 - tt
            m = mid + amp * np.sin((2.0 * np.pi * (along + 1.0) / V) + phase)
            pm = (tt - (m - width / 2.0)) / width
            pm = 0.0 if pm < 0.0 else (1.0 if pm > 1.0 else pm)
            pm = pm * pm * (3.0 - 2.0 * pm)
            src = mud if rng.random() < pm else ash
            layer[bi:bi + 2, bj:bj + 2] = src[bi:bi + 2, bj:bj + 2]
    if side == 'n':
        layer[:, 0:2] = ash[:, 0:2]
        layer[:, V - 2:V] = mud[:, V - 2:V]
    elif side == 's':
        layer[:, V - 2:V] = ash[:, V - 2:V]
        layer[:, 0:2] = mud[:, 0:2]
    elif side == 'w':
        layer[0:2, :] = ash[0:2, :]
        layer[V - 2:V, :] = mud[V - 2:V, :]
    else:
        layer[V - 2:V, :] = ash[V - 2:V, :]
        layer[0:2, :] = mud[0:2, :]
    _hoof_marks(rng, layer, int(rng.integers(2, 5)), K('soot_agb'))
    return _ground(layer, K('soot_agb'))


def _path_dir(name, kind):
    """向きつきの道（店を結ぶ小路。石畳の残りに暗い轍）。"""
    rng = rng_for(name)
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('pack_d'), K('ash_agb_d'), K('pack'), K('dirt_d')], [0.36, 0.28, 0.2, 0.16])
    _cobble_remains(rng, layer, int(rng.integers(4, 8)))
    cols = (K('soot_agb_l'), K('soot_agb'))
    if kind == 'ew':
        _ruts(layer, rng, True, cols)
        _hoof_marks(rng, layer, int(rng.integers(3, 7)), K('soot_agb_l'))
    elif kind == 'ns':
        _ruts(layer, rng, False, cols)
        _hoof_marks(rng, layer, int(rng.integers(3, 7)), K('soot_agb_l'))
    else:
        _churn(layer, rng, K('soot_agb_l'), K('soot_agb'), int(rng.integers(1, 3)), int(rng.integers(8, 14)))
    return _ground(layer, K('pack_d'))


def mud_puddle_agb(name):
    """泥の溜まり（小物。店の前・交差点・井戸の周りに撒く）— 黒い水に泥の縁。
    03 は板が 1 枚渡してある。高さ 1〜2 ボクセル。"""
    rng = rng_for(name)
    v = vol(h=4)
    cx, cy = int(rng.integers(13, 19)), int(rng.integers(13, 19))
    rx, ry = float(rng.uniform(7, 11)), float(rng.uniform(5, 8))
    xs = np.arange(V)[:, None] + 0.5 - cx
    ys = np.arange(V)[None, :] + 0.5 - cy
    d = (xs / rx) ** 2 + (ys / ry) ** 2
    v[:, :, 0][d <= 1.35] = K('soot_agb')
    v[:, :, 0][d <= 1.0] = K('wa_agb')
    v[:, :, 1][(d > 1.0) & (d <= 1.35)] = K('soot_agb_l')     # 泥の縁（1 段盛る）
    for _ in range(3):
        x, y = int(rng.integers(cx - 8, cx + 6)), int(rng.integers(cy - 6, cy + 4))
        box(v, x, x + 2, y, y + 3, 1, 2, K('soot_agb'))      # 足跡
    if _variant(name) == 3:
        box(v, cx - 9, cx + 9, cy - 2, cy + 1, 1, 3, K('board_agb'))
        box(v, cx - 9, cx + 9, cy - 1, cy, 2, 3, K('board_agb_d'))
    return one(v)


# ------------------------------------------------------------------ 高い枯れ木（第 4 回）
def dead_tree_tall_agb(name):
    """背の高い枯れ木（2.5〜3 マス）。幹が太く、折れた大枝が張り出す。接地は 1 マス（3×3 マスの体積の中心）。"""
    rng = rng_for(name)
    idx = int(name[-2:])
    h = {1: 84, 2: 96, 3: 76}[idx]
    span = 96
    v = np.zeros((span, span, h), np.int16)
    cx = cy = V + (V // 2)
    trunk_top = int(h * 0.55)
    rod(v, (cx, cy, 0), (cx + 1, cy - 1, trunk_top), 5.2, 2.4, K('bark_agb'))
    for (dx, dy) in ((6, 0), (-6, 2), (0, 6), (2, -6), (-4, -4), (4, 4)):      # 根の張り出し
        rod(v, (cx + dx * 1.6, cy + dy * 1.6, 0), (cx, cy, 6), 1.2, 2.6, K('bark_agb'))
    n_br = 5
    for k in range(n_br):
        ang = (k * 6.283 / n_br) + rng.uniform(-0.4, 0.4)
        z0 = trunk_top * rng.uniform(0.5, 0.95)
        broken = (k % 2 == 1)
        ln = rng.uniform(10, 16) if broken else rng.uniform(18, 30)
        up = rng.uniform(4, 10) if broken else rng.uniform(10, 22)
        ex = cx + 1 + np.cos(ang) * ln
        ey = cy - 1 + np.sin(ang) * ln
        colour = K('bark_agb_l') if (k % 2) else K('bark_agb')
        rod(v, (cx + 1, cy - 1, z0), (ex, ey, z0 + up), 2.6, 1.2, colour)
        if broken:
            box(v, int(ex) - 2, int(ex) + 3, int(ey) - 2, int(ey) + 3, int(z0 + up) - 2, int(z0 + up) + 2, K('char_agb_l'))
        else:
            for _ in range(3):                                # 小枝
                a2 = ang + rng.uniform(-1.1, 1.1)
                l2 = rng.uniform(6, 12)
                rod(v, (ex, ey, z0 + up), (ex + np.cos(a2) * l2, ey + np.sin(a2) * l2, z0 + up + rng.uniform(2, 9)),
                    1.1, 0.6, colour)
    if idx == 3:                                              # 梢が折れて裂けている
        box(v, cx - 3, cx + 4, cy - 4, cy + 2, trunk_top - 4, trunk_top + 3, K('char_agb_l'))
        rod(v, (cx + 1, cy - 1, trunk_top), (cx + 2, cy - 1, trunk_top + 8), 2.0, 0.6, K('char_agb'))
    else:
        rod(v, (cx + 1, cy - 1, trunk_top), (cx + 3, cy - 3, h - 1), 2.2, 0.6, K('bark_agb'))
        rod(v, (cx + 1, cy - 1, h * 0.8), (cx + 9, cy + 5, h * 0.97), 1.2, 0.5, K('bark_agb_l'))
    v[:, :, 0][v[:, :, 0] > 0] = K('bark_agb')
    return one(v)


# ------------------------------------------------------------------ 地面の散らし（第 4 回）
def grass_green_agb(name):
    """緑の草の株（控えめ。枯れ草の間にところどころ）。"""
    rng = rng_for(name)
    v = vol(h=10)
    for _ in range(int(rng.integers(6, 10))):
        bx, by = int(rng.integers(10, V - 10)), int(rng.integers(10, V - 10))
        tall = int(rng.integers(3, 8))
        lean_x, lean_y = int(rng.integers(-3, 4)), int(rng.integers(-3, 4))
        blade = pick(rng, [K('turf'), K('turf_l'), K('turf_d')], [0.45, 0.3, 0.25])
        for z in range(tall):
            t = (z / float(tall)) ** 1.4
            v[min(V - 1, max(0, bx + int(round(lean_x * t)))), min(V - 1, max(0, by + int(round(lean_y * t)))), z] = blade
    return one(v)


def litter_agb(name):
    """ゴミ（6 種）: 01 割れた壺のかけら / 02 ぼろ布 / 03 空き瓶 / 04 木くず / 05 錆びた鉄片 / 06 骨のかけら。"""
    rng = rng_for(name)
    idx = _variant(name)
    v = vol(h=6)
    if idx == 1:
        for _ in range(5):
            x, y = int(rng.integers(8, 22)), int(rng.integers(8, 22))
            box(v, x, x + int(rng.integers(2, 5)), y, y + int(rng.integers(2, 4)), 0, 1, K('clay') if rng.random() < 0.6 else K('clay_d'))
        disc(v, 18, 16, 3.5, 0, 3, K('clay'), hollow=2.5)   # 壺の底
    elif idx == 2:
        ball(v, 14, 16, 0, 6.0, K('cloth_agb_d'), squash=0.35)
        box(v, 18, 26, 14, 17, 0, 1, K('cloth_agb_d'))
        box(v, 10, 13, 12, 14, 1, 2, K('cloth_agb'))
    elif idx == 3:
        log_x(v, 16, 1, 1.6, 9, 21, K('glass_l'))
        log_x(v, 16, 1, 1.0, 21, 24, K('glass_l'))
        log_y(v, 22, 1, 1.4, 8, 16, K('wa_l'))
    elif idx == 4:
        for _ in range(6):
            x, y = int(rng.integers(6, 24)), int(rng.integers(6, 24))
            if rng.random() < 0.5:
                box(v, x, x + int(rng.integers(4, 9)), y, y + 2, 0, 1, pick(rng, [K('board_agb_d'), K('char_agb_l')]))
            else:
                box(v, x, x + 2, y, y + int(rng.integers(4, 9)), 0, 1, pick(rng, [K('board_agb'), K('board_agb_d')]))
    elif idx == 5:
        box(v, 10, 20, 14, 18, 0, 1, K('rust_agb'))
        box(v, 14, 16, 12, 22, 0, 2, K('iron'))
        disc(v, 22, 12, 2.5, 0, 1, K('rust_agb'), hollow=1.5)
    else:
        rod(v, (8, 20, 0), (18, 14, 0), 1.0, 1.0, K('bone_d'))
        box(v, 20, 24, 16, 19, 0, 2, K('bone'))
        box(v, 12, 14, 8, 12, 0, 1, K('bone_d'))
    return one(v)


# ------------------------------------------------------------------ 城壁（第 2 回）
def town_wall_agb(name):
    """崩れかけた石の城壁（町の外周の永久壁・花崗岩の差し替え）。マスいっぱいの石積みで、
    天端は 4×4 の塊ごとに高さが違う（z 28〜44）。変種 02 は南面に縦の割れ目、03 は角がごっそり欠ける。"""
    rng = rng_for(name)
    idx = int(name[-2:])
    H = 46
    v = vol(h=H)
    mask = np.ones((V, V), bool)
    heights = 30 + np.repeat(np.repeat(rng.integers(0, 15, size=(8, 8)), 4, axis=0), 4, axis=1)
    heights = heights.astype(np.int32)
    if idx == 3:
        heights[20:32, 20:32] = 22                           # 欠けた角（南東）
        heights[24:32, 24:32] = 16
    _masonry_fill(v, rng, mask, heights, _stone(), STONE_W)
    if idx == 2:                                             # 縦の割れ目（南面。1 ボクセル彫る）
        x = int(rng.integers(8, 22))
        for z in range(2, 30):
            v[x, V - 1, z] = 0
            v[x, V - 2, z] = K('rock_agb_j')
            if rng.random() < 0.3:
                x = min(V - 2, max(1, x + int(rng.integers(-1, 2))))
    for _ in range(3):                                       # 煤（下のほう。塊で）
        x, y = int(rng.integers(0, V - 6)), int(rng.integers(0, V - 6))
        blk = v[x:x + 6, y:y + 6, 0:6]
        blk[blk > 0] = K('soot_agb_l')
    return one(v)


# ------------------------------------------------------------------ 小物（第 2 回）
def _swing_parts(names_pivots, amplitude=5.0, period=2.8):
    """`frame`（静止）＋ 吊り下げた部品（振り子）。辺境の `swing_parts` と同じ形。"""
    parts = [{'name': 'frame', 'voxels': 'frame', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    for i, (pname, pivot) in enumerate(names_pivots):
        parts.append({'name': pname, 'voxels': pname, 'parent': 'frame', 'grounded': False,
                      'pivot': [float(pivot[0]), float(pivot[1]), float(pivot[2])],
                      'motion': {'kind': 'pendulum', 'axis': 'x' if (i % 2 == 0) else 'y',
                                 'amplitude': float(amplitude), 'period': float(period) + (0.35 * i),
                                 'phase': 0.9 * i, 'damping': 0.0},
                      'wind_k': 0.0})
    return parts


def ash_pile_agb(name):
    """灰の山 — 円錐に積もった灰と、混じる炭。"""
    rng = rng_for(name)
    v = vol(h=10)
    for z in range(8):
        disc(v, 15, 16, 10.0 * (1.0 - z / 9.0), z, z + 1, K('ash_agb') if (z % 2) else K('ash_agb_l'))
    for _ in range(6):
        x, y = int(rng.integers(8, 22)), int(rng.integers(9, 23))
        col = v[x, y, :]
        nz = col.nonzero()[0]
        if len(nz):
            box(v, x, x + 2, y, y + 1, int(nz.max()), int(nz.max()) + 1, K('char_agb'))
    return one(v)


def bones_agb(name):
    """骨 — 頭骨と肋骨、散らばった長い骨。"""
    v = vol(h=8)
    ball(v, 11, 14, 3, 3.5, K('bone'))
    box(v, 9, 13, 16, 18, 1, 4, K('bone_d'))                 # 顎
    box(v, 9, 10, 13, 14, 3, 4, K('dark'))                   # 眼窩
    box(v, 12, 13, 13, 14, 3, 4, K('dark'))
    for k in range(4):                                       # 肋骨
        rod(v, (16 + k * 3, 12, 0), (17 + k * 3, 22, 3), 0.8, 0.8, K('bone'))
    rod(v, (6, 24, 0), (20, 27, 0), 1.0, 1.0, K('bone_d'))   # 長い骨
    rod(v, (22, 6, 0), (30, 12, 0), 1.0, 1.0, K('bone'))
    return one(v)


def goods_crate_agb(name):
    """雑貨屋の木箱 — 板が 1 枚外れ、蓋が斜めに載っている。"""
    v = vol(h=22)
    box(v, 6, 24, 8, 24, 0, 12, K('board_agb'))
    for z in (0, 5, 10):
        box(v, 6, 24, 8, 24, z, z + 1, K('board_agb_d'))
    box(v, 6, 7, 8, 24, 0, 12, K('timber_d'))
    box(v, 23, 24, 8, 24, 0, 12, K('timber_d'))
    box(v, 12, 16, 23, 24, 1, 10, 0)                         # 外れた板（南面の穴）
    box(v, 13, 15, 22, 23, 1, 10, K('dark'))
    for k in range(8):                                       # 蓋（斜めに載る）
        box(v, 7 + k * 2, 9 + k * 2, 7, 25, 12 + k, 14 + k, K('board_agb_l'))
    box(v, 12, 16, 26, 31, 0, 2, K('board_agb'))             # 落ちた板
    return one(v)


def rope_coil_agb(name):
    """縄の輪と束。"""
    v = vol(h=8)
    disc(v, 12, 14, 8.0, 0, 3, K('lash'), hollow=5.0)
    disc(v, 12, 14, 7.0, 3, 5, K('lash'), hollow=5.5)
    log_x(v, 24, 2, 2.2, 6, 26, K('lash'))
    return one(v)


def sack_agb(name):
    """麻袋 — 3 つ。1 つは破れて中身（穀物）がこぼれている。"""
    v = vol(h=14)
    for (cx, cy, r) in ((11, 12, 6), (21, 18, 5.5), (13, 22, 5)):
        ball(v, cx, cy, 5, r, K('cloth_d'), squash=0.9)
        box(v, cx - 2, cx + 2, cy - 2, cy + 2, 9, 12, K('cloth'))
        box(v, cx - 3, cx + 3, cy - 3, cy + 3, 10, 11, K('lash'))
    box(v, 23, 27, 16, 20, 3, 6, K('cloth_agb'))             # 破れ（当て布）
    box(v, 24, 30, 20, 26, 0, 2, K('hay_d'))                 # こぼれた穀物
    return one(v)


def dented_armour_stand_agb(name):
    """へこんだ鎧の立て台 — 胸当てに凹み（1 ボクセル彫る）と錆、兜は欠けている。"""
    v = vol(h=40)
    box(v, 12, 20, 12, 20, 0, 2, K('timber_d'))
    box(v, 15, 17, 15, 17, 2, 32, K('timber'))
    box(v, 6, 26, 15, 17, 24, 26, K('timber'))               # 肩木
    box(v, 10, 22, 12, 20, 12, 26, K('steel_d'))             # 胸当て
    box(v, 11, 21, 19, 21, 14, 24, K('steel'))
    box(v, 13, 15, 20, 21, 16, 19, 0)                        # 凹み
    box(v, 13, 15, 19, 20, 16, 19, K('iron'))
    box(v, 17, 20, 20, 21, 20, 22, K('rust_agb'))            # 錆
    box(v, 13, 19, 19, 21, 17, 19, K('iron'))                # 帯金
    ball(v, 16, 16, 30, 4.5, K('steel_d'))                   # 兜（欠け）
    box(v, 18, 21, 12, 17, 30, 35, 0)
    box(v, 13, 19, 18, 21, 27, 30, K('steel_d'))
    return one(v)


def cracked_shield_agb(name):
    """ひびの入った盾 — 杭に立て掛けた丸盾。ひびを 1 ボクセル彫る。"""
    v = vol(h=22)
    box(v, 14, 18, 8, 12, 0, 20, K('timber_d'))              # 杭
    for z in range(18):                                      # 盾（少し傾いて立つ）
        t = (z - 9.0) / 9.0
        r = 9.0 * float(np.sqrt(max(0.0, 1.0 - t * t)))
        y = 14 + int(round(z * 0.25))
        disc_y = np.arange(V)[:, None] + 0.5 - 16
        m = np.abs(disc_y) <= r
        v[:, y, z][m[:, 0]] = K('board_agb_d')
        v[:, y + 1, z][m[:, 0]] = K('board_agb')
    box(v, 14, 18, 14, 22, 8, 12, K('iron'))                 # 中央の鉄
    for k in range(9):                                       # ひび（斜め）
        x = 8 + k
        z = 3 + int(k * 1.6)
        y = 14 + int(round(z * 0.25)) + 1
        box(v, x, x + 1, y, y + 1, z, z + 2, 0)
    box(v, 24, 30, 8, 12, 0, 2, K('rust_agb'))               # 落ちた縁金
    return one(v)


def anvil_agb(name):
    """金床 — 切り株に載せた鉄の金床。錆と、脇の金槌。"""
    v = vol(h=24)
    disc(v, 14, 16, 6.5, 0, 9, K('bark_agb_l'))
    disc(v, 14, 16, 5.5, 8, 9, K('log_end'))
    box(v, 9, 19, 12, 20, 9, 12, K('iron'))
    box(v, 10, 18, 13, 19, 12, 15, K('iron_l'))
    box(v, 6, 24, 13, 19, 15, 18, K('steel_d'))
    box(v, 6, 24, 13, 19, 17, 18, K('steel'))
    box(v, 24, 30, 15, 17, 16, 18, K('steel_d'))             # 角
    box(v, 7, 11, 13, 14, 15, 18, K('rust_agb'))             # 錆
    box(v, 24, 26, 24, 26, 0, 12, K('timber_d'))             # 金槌
    box(v, 22, 28, 22, 28, 12, 15, K('iron'))
    return one(v)


def rusty_weapon_rack_agb(name):
    """錆びた武器立て — 板の背に剣 2 本（1 本は折れている）と槍。"""
    v = vol(h=40)
    box(v, 2, 30, 18, 21, 0, 3, K('timber_d'))
    box(v, 2, 30, 18, 20, 3, 30, K('board_agb'))
    box(v, 2, 30, 18, 20, 29, 32, K('timber_d'))
    box(v, 10, 11, 18, 20, 3, 30, 0)                         # 背板の抜け
    box(v, 6, 7, 20, 21, 4, 22, K('rust_agb'))               # 剣（錆）
    box(v, 4, 9, 20, 21, 20, 22, K('iron'))
    box(v, 6, 7, 20, 21, 22, 26, K('timber_d'))
    box(v, 16, 17, 20, 21, 12, 22, K('steel_d'))             # 折れた剣（上だけ残る）
    box(v, 14, 19, 20, 21, 20, 22, K('iron'))
    box(v, 16, 17, 20, 21, 22, 26, K('timber_d'))
    box(v, 12, 20, 22, 24, 0, 1, K('steel_d'))               # 折れた刃（地面）
    box(v, 26, 27, 21, 22, 4, 38, K('timber_d'))             # 槍
    box(v, 25, 28, 21, 22, 34, 38, K('rust_agb'))
    return one(v)


def charred_books_agb(name):
    """焼け焦げた本の山 — 縁が炭になった本を斜めに積む。"""
    v = vol(h=18)
    cols = [K('cloth_d'), K('crimson'), K('cream_d'), K('char_agb_l'), K('cream')]
    z = 0
    for i in range(5):
        w = 12 - (i % 3)
        x0 = 8 + (i % 2) * 2
        box(v, x0, x0 + w, 10, 22, z, z + 3, cols[i])
        box(v, x0 + 1, x0 + w - 1, 10, 22, z + 1, z + 2, K('cream'))
        box(v, x0 + w - 3, x0 + w, 10, 22, z, z + 3, K('char_agb'))   # 焦げた縁
        z += 3
    box(v, 22, 30, 12, 20, 0, 3, K('cream_d'))               # 開いた本（焦げ）
    box(v, 26, 30, 12, 20, 0, 3, K('char_agb'))
    return one(v)


def lectern_agb(name):
    """外の読み台 — 傾いた台と、その上の本。脚は継ぎはぎ。"""
    v = vol(h=26)
    box(v, 10, 22, 12, 20, 0, 2, K('timber_d'))
    box(v, 14, 18, 14, 18, 2, 18, K('board_agb_d'))
    box(v, 13, 19, 15, 16, 6, 12, K('board_agb_l'))          # 添え木
    for k in range(8):
        box(v, 7, 25, 11 + k, 12 + k, 18 + k, 20 + k, K('board_agb'))
    box(v, 9, 23, 13, 17, 21, 23, K('cream'))
    box(v, 15, 17, 13, 17, 22, 24, K('cream_d'))
    return one(v)


def cracked_flasks_agb(name):
    """割れた薬瓶 — 小さな棚に瓶が並び、1 つは割れて破片が落ちている。"""
    v = vol(h=24)
    box(v, 6, 26, 12, 15, 0, 22, K('board_agb_d'))           # 背板
    for x in (6, 24):
        box(v, x, x + 2, 12, 22, 0, 22, K('board_agb'))
    for z in (1, 11):
        box(v, 6, 26, 12, 22, z, z + 1, K('board_agb'))
    box(v, 6, 26, 12, 22, 21, 22, K('board_agb'))
    colours = (K('wa_l'), K('slime_l'), K('fl_r'), K('gold'), K('sky'))
    for i, colour in enumerate(colours):
        x = 9 + (i % 3) * 5
        z = 2 if i < 3 else 12
        box(v, x, x + 3, 16, 20, z, z + 5, colour)
        box(v, x + 1, x + 2, 17, 19, z + 5, z + 7, K('glass_l'))
    box(v, 20, 23, 16, 20, 14, 17, 0)                        # 割れた瓶（上が無い）
    box(v, 12, 16, 24, 28, 0, 1, K('glass_l'))               # 破片
    box(v, 18, 20, 26, 29, 0, 1, K('glass_l'))
    return one(v)


def chipped_crystal_agb(name):
    """欠けた水晶 — 石の台座に青い結晶。先端が折れ、欠片が台座の脇に落ちている。光は `chipped_crystal_glow_agb`。"""
    v = vol(h=36)
    disc(v, 16, 16, 8.0, 0, 6, K('rock_agb_d'))
    disc(v, 16, 16, 7.0, 5, 6, K('rock_agb_l'))
    for z in range(6, 26):
        disc(v, 16, 16, 5.0 - (z - 6) * 0.12, z, z + 1, K('wa_l'))
    box(v, 16, 21, 12, 21, 20, 26, 0)                        # 折れた先端（斜めに欠ける）
    box(v, 14, 21, 12, 21, 23, 26, 0)
    for z in range(6, 16):
        disc(v, 10, 13, 2.5 - (z - 6) * 0.2, z, z + 1, K('wa_l'))
    rod(v, (24, 22, 0), (30, 27, 3), 1.6, 0.6, K('sky'))     # 落ちた欠片
    return one(v)


def chipped_crystal_glow_agb(name):
    v = vol(h=36)
    box(v, 15, 17, 15, 17, 12, 22, K('sky'))
    return one(v)


def rune_stones_agb(name):
    """符石 — 環に立てた立石。1 つは倒れている。印は青い光の残り。"""
    v = vol(h=18)
    for k in range(5):
        a = k * 1.2566
        x, y = int(16 + 9 * np.cos(a)), int(16 + 9 * np.sin(a))
        if k == 2:
            box(v, x - 6, x + 2, y - 2, y + 2, 0, 3, K('rock_agb'))   # 倒れた石
            continue
        box(v, x - 2, x + 2, y - 2, y + 2, 0, 10 + (k % 2) * 4, K('rock_agb'))
        box(v, x - 1, x + 1, y + 1, y + 3, 4, 8, K('wa_l'))
    disc(v, 16, 16, 3.0, 0, 1, K('soot_agb_l'))
    return one(v)


def tattered_tarp_agb(name):
    """破れた幌 — 荷に掛けた帆布。穴があき、縄で縛ってある。"""
    v = vol(h=16)
    ball(v, 15, 16, 2, 11.0, K('cloth_agb_d'), squash=0.7)
    ball(v, 19, 12, 4, 6.0, K('cloth_agb'), squash=0.8)
    v[:, :, 0][v[:, :, 0] != 0] = K('cloth_agb_d')
    for (x, y) in ((8, 20), (20, 8)):                        # 破れ（下の荷＝木箱が覗く）
        col = v[x:x + 3, y:y + 3, :]
        col[col != 0] = K('board_agb_d')
    for y in (10, 20):
        for x in range(4, 28):
            col = v[x, y, :]
            nz = col.nonzero()[0]
            if len(nz):
                col[nz.max()] = K('lash')
    return one(v)


def chest_agb(name):
    """宝箱 — 蓋が開き、鉄の帯は錆びている。鎖で杭につないである。"""
    v = vol(h=20)
    box(v, 8, 24, 10, 22, 0, 9, K('board_agb_d'))
    box(v, 9, 23, 10, 22, 1, 8, K('board_agb'))
    box(v, 9, 23, 11, 21, 8, 9, K('dark'))                   # 開いた中
    for x in (10, 16, 21):
        box(v, x, x + 1, 9, 23, 0, 9, K('rust_agb'))
    for k in range(10):                                      # 開いた蓋（後ろへ立つ）
        box(v, 8, 24, 9 - k // 4, 10 - k // 4, 9 + k, 10 + k, K('board_agb'))
    box(v, 8, 24, 8, 10, 18, 19, K('rust_agb'))
    box(v, 26, 29, 14, 17, 0, 8, K('timber_d'))              # 杭
    for k in range(6):
        box(v, 24 + k // 2, 25 + k // 2, 15, 16, 4 + (k % 2), 5 + (k % 2), K('iron_l'))
    return one(v)


def brazier_agb(name):
    """火鉢 — 錆びた鉄の三脚に皿。火は `brazier_fire_agb`。"""
    v = vol(h=22)
    for (x, y) in ((9, 11), (23, 11), (16, 25)):
        box(v, x - 1, x + 1, y - 1, y + 1, 0, 12, K('iron'))
    disc(v, 16, 16, 7.5, 11, 14, K('iron'))
    disc(v, 16, 16, 7.5, 13, 14, K('rust_agb'), hollow=6.0)
    disc(v, 16, 16, 6.0, 13, 14, K('char_agb'))
    return one(v)


def brazier_fire_agb(name):
    v = vol(h=22)
    disc(v, 16, 16, 4.0, 14, 17, K('fl_mid'))
    disc(v, 16, 16, 2.0, 16, 20, K('fl_core'))
    return one(v)


def patched_laundry_agb(name):
    """継ぎはぎの洗濯物 — 2 本の柱に張った縄にシーツと服。当て布つき。布は振り子で揺れる。"""
    frame = vol(h=40)
    for x in (2, 28):
        box(frame, x, x + 2, 15, 17, 0, 34, K('timber_d'))
        box(frame, x - 1, x + 3, 14, 18, 0, 2, K('rock_agb_d'))
    box(frame, 4, 28, 15, 16, 32, 33, K('lash'))
    sheet = vol(h=40)
    box(sheet, 5, 15, 15, 16, 12, 32, K('cloth'))
    box(sheet, 5, 15, 15, 16, 12, 14, K('cloth_d'))
    box(sheet, 7, 11, 15, 16, 18, 24, K('cloth_agb_d'))      # 当て布
    box(sheet, 12, 14, 15, 16, 14, 17, 0)                    # 破れ
    shirt = vol(h=40)
    box(shirt, 17, 26, 15, 16, 20, 32, K('cloth_d'))
    box(shirt, 15, 17, 15, 16, 27, 32, K('cloth_d'))
    box(shirt, 26, 28, 15, 16, 27, 32, K('cloth_d'))
    box(shirt, 20, 24, 15, 16, 22, 26, K('cloth_agb'))       # 当て布
    return [('frame', frame, (0, 0, 0)), ('sheet', sheet, (0, 0, 0)), ('shirt', shirt, (0, 0, 0))]


def boarded_window_agb(name):
    """板でふさいだ窓 — 壁から外した鎧戸を杭に立て掛け、板を打ちつけてある（我が家の前庭の小物）。"""
    v = vol(h=28)
    box(v, 22, 26, 12, 16, 0, 24, K('timber_d'))             # 杭
    for k in range(20):                                      # 立て掛けた窓枠（斜め）
        y = 24 - k // 3
        box(v, 4, 20, y, y + 2, k, k + 1, K('timber_d') if (k < 2 or k > 17) else K('glass'))
        box(v, 4, 6, y, y + 2, k, k + 1, K('timber_d'))
        box(v, 18, 20, y, y + 2, k, k + 1, K('timber_d'))
    for z0 in (5, 12):                                       # 打ちつけた板（1 ボクセル手前）
        for k in range(3):
            y = 24 - (z0 + k) // 3 + 2
            box(v, 3, 21, y, y + 1, z0 + k, z0 + k + 1, K('board_agb_l'))
    return one(v)


def woodpile_agb(name):
    """薪の山 — 2 本の杭の間に半分だけ残った薪。木口を南へ。"""
    rng = rng_for(name)
    v = vol(h=18)
    for x in (3, 27):
        box(v, x, x + 2, 8, 24, 0, 18, K('timber_d'))
    for z in range(0, 12, 3):
        p = 5
        while p < (27 if z < 6 else 17):
            w = int(rng.integers(3, 5))
            box(v, p, p + w, 8, 24, z, z + 3, K('oak_l') if rng.random() < 0.6 else K('board_agb'))
            box(v, p, p + w, 23, 24, z, z + 3, K('log_end'))
            box(v, p + w, p + w + 1, 8, 24, z, z + 3, K('oak'))
            p += w + 1
    log_x(v, 27, 1, 1.5, 12, 22, K('oak'))                  # 転がった薪
    return one(v)


# ------------------------------------------------------------------ 登録
def register(g):
    """`gen_prefabs.py` の名前空間 `g` へ Angband の町の素材を足す。"""
    global G, C, V
    G = g
    C = g['C']
    V = g['V']
    g['TOWN_STYLES'] = tuple(g['TOWN_STYLES']) + ('agb',)
    _palette()
    cat = g['CATALOG']
    static = g['static_part']

    for sl in g['HOUSE_SLICES']:
        cat['house_wall_%s_agb' % sl] = (house_wall_agb, static(), 'Angband の町の壁 1 層（半壊した石積みを板で継ぎはぎ。%s）' % sl)
        cat['house_roof_%s_agb' % sl] = (house_roof_agb, static(), 'Angband の町の屋根（穴のあいた板葺きに帆布の当て布。%s）' % sl)
    # ---- 第 3 回: マスごとの変種（壁 4・屋根 8）と半壊した店の組、岩山 ----
    for sl in g['HOUSE_SLICES']:
        for i in range(1, 5):
            cat['house_wall_%s_agb_%02d' % (sl, i)] = (house_wall_agb, static(), 'Angband の町の壁の変種（崩れ具合と継ぎはぎの位置が違う。%s）' % sl)
        for i in range(1, 9):
            cat['house_roof_%s_agb_%02d' % (sl, i)] = (house_roof_agb, static(), 'Angband の町の屋根の変種（無地・小さな穴・大きな穴・当て布。%s）' % sl)
        cat['house_wall_ruin_%s_agb' % sl] = (house_wall_ruin_agb, static(), '半壊した店の崩れた側の壁（床・低い壁・瓦礫。%s）' % sl)
        cat['house_roof_ruin_%s_agb' % sl] = (house_roof_ruin_agb, static(), '半壊した店の崩れた側の屋根（焼けた垂木の残り。%s）' % sl)
    for tier in ('low', 'mid', 'high', 'peak'):
        for i in range(1, 4):
            cat['crag_%s_agb_%02d' % (tier, i)] = (crag_agb, static(), '焦げた岩山（%s 段。ダンジョンの入口と外周の花崗岩）' % tier)
    cat['cave_mouth_agb'] = (cave_mouth_agb, static(), 'ダンジョンの入口（岩の柱と楣に囲まれた黒い口と下り段。地面ごと置き換える）')
    #! 岩山の天面に置く高山植物（置く側が `alpine` を意匠つきで引く）。焦げた岩に緑の株は
    #! 合わないので、枯れ草の株を当てる（実機の絵で緑の株が載っていた）。
    for i in range(1, 3):
        cat['alpine_agb_%02d' % i] = (dead_grass_agb, static(motion={'kind': 'wind'}, wind_k=0.6), '岩山の天面の枯れ草')
    cat['house_entrance_agb'] = (house_entrance_agb, static(), 'Angband の町の戸口（板で補強した板戸）')
    cat['house_light_agb'] = (house_light_agb, static(), '窓の灯り（Angband の町の窓の高さ）')
    cat['chimney_agb'] = (chimney_agb, static(), '傾いた石の煙突（roof_vent。煙は置く側が積む）')
    for i in range(1, 5):
        cat['ruin_wall_agb_%02d' % i] = (ruin_wall_agb, static(), '屋根の落ちた石の家の壁（通れない瓦礫。高い）')
    for i in range(1, 4):
        cat['ruin_low_agb_%02d' % i] = (ruin_low_agb, static(), '低い瓦礫の山（通れる瓦礫）')
    cat['ruin_beam_agb'] = (ruin_beam_agb, static(), '焼けた梁が斜めに刺さった瓦礫（通れない瓦礫の高さ）')
    for i in range(1, 4):
        cat['lava_crust_agb_%02d' % i] = (lava_crust_agb, static(), '溶岩の縁の冷えて固まった黒い岩（lava_crust と同じ厚みと原点）')
    for i in range(1, 6):
        cat['ground_ash_agb_%02d' % i] = (ground_ash_agb, static(), '灰と乾いた土の地面')
    for i in range(1, 4):
        cat['ground_scorched_agb_%02d' % i] = (ground_scorched_agb, static(), '焦げた地面（ひびから赤い光）')
        cat['ground_cobble_agb_%02d' % i] = (ground_cobble_agb, static(), '踏み固めた石畳の残り')
    for i in range(1, 9):
        cat['dead_tree_agb_%02d' % i] = (dead_tree_agb, static(), '葉の無い黒い枯れ木（48〜190 vox の 8 変種。折れた枝・裂けた梢・うろ）')
    for i in range(1, 4):
        cat['dead_stump_agb_%02d' % i] = (dead_stump_agb, static(), '立ち枯れた巨木の根株（高さ 1〜2 マス。太い幹と四方へ張る根。01 裂けた上端 / 02 炭の断面 / 03 二つに裂けた破片）')
    for i in range(1, 3):
        cat['great_dead_tree_agb_%02d' % i] = (great_dead_tree_agb, static(), '巨大な枯れ木の目印（5×5 マス・高さ 300〜320。01 雷に裂けた古木 / 02 焼け焦げた巨木）')
    cat['dead_grass_agb'] = (dead_grass_agb, static(motion={'kind': 'wind'}, wind_k=0.6), '枯れ草の株')
    cat['broken_barrel_agb'] = (broken_barrel_agb, static(), '割れた樽（雨水が溜まる）')
    cat['patched_cart_agb'] = (patched_cart_agb, static(), '継ぎはぎの荷車（片輪が外れて石で支える）')
    cat['campfire_agb'] = (campfire_agb, static(), '焚き火。火は campfire_fire_agb')
    cat['campfire_fire_agb'] = (campfire_fire_agb, static(), '焚き火の火')
    cat['boarded_well_agb'] = (boarded_well_agb, static(), '板で補強した井戸')
    cat['forge_agb'] = (forge_agb, static(), '鍛冶の炉（傾いた煙突。武器屋の前庭）。火は forge_fire_agb')
    cat['forge_fire_agb'] = (forge_fire_agb, static(), '鍛冶の炉の火（昼も光る）')
    cat['cauldron_agb'] = (cauldron_agb, static(), '大釜（錬金術店の前庭）。火は cauldron_fire_agb')
    cat['cauldron_fire_agb'] = (cauldron_fire_agb, static(), '大釜の火と泡')
    # ---- 第 2 回 ----
    for i in range(1, 4):
        cat['town_wall_agb_%02d' % i] = (town_wall_agb, static(), '崩れかけた石の城壁（町の外周）')
    single = {
        'ash_pile': (ash_pile_agb, '灰の山'),
        'bones': (bones_agb, '骨（頭骨と肋骨）'),
        'goods_crate': (goods_crate_agb, '雑貨屋の木箱（板が外れている）'),
        'rope_coil': (rope_coil_agb, '縄の輪'),
        'sack': (sack_agb, '麻袋 3 つ（1 つは破れ）'),
        'dented_armour_stand': (dented_armour_stand_agb, 'へこんだ鎧の立て台'),
        'cracked_shield': (cracked_shield_agb, 'ひびの入った盾'),
        'anvil': (anvil_agb, '錆びた金床'),
        'rusty_weapon_rack': (rusty_weapon_rack_agb, '錆びた武器立て'),
        'charred_books': (charred_books_agb, '焼け焦げた本の山'),
        'lectern': (lectern_agb, '継ぎはぎの読み台'),
        'cracked_flasks': (cracked_flasks_agb, '割れた薬瓶の棚'),
        'chipped_crystal': (chipped_crystal_agb, '欠けた水晶。光は chipped_crystal_glow_agb'),
        'chipped_crystal_glow': (chipped_crystal_glow_agb, '欠けた水晶の光（昼も光る）'),
        'rune_stones': (rune_stones_agb, '符石（1 つ倒れている）'),
        'tattered_tarp': (tattered_tarp_agb, '破れた幌を掛けた荷'),
        'chest': (chest_agb, '蓋の開いた宝箱（錆びた帯）'),
        'brazier': (brazier_agb, '錆びた火鉢。火は brazier_fire_agb'),
        'brazier_fire': (brazier_fire_agb, '火鉢の火'),
        'boarded_window': (boarded_window_agb, '板でふさいだ窓（立て掛けた鎧戸）'),
        'woodpile': (woodpile_agb, '半分残った薪の山'),
    }
    for stem, (fn, note) in single.items():
        cat['%s_agb' % stem] = (fn, static(), note)
    cat['patched_laundry_agb'] = (patched_laundry_agb,
                                  _swing_parts([('sheet', (10, 15.5, 32)), ('shirt', (21, 15.5, 32))]),
                                  '継ぎはぎの洗濯物（布は振り子で揺れる）')
    # ---- 町の読みが決まった名前で探す素材（第 3 回。2026-09-24）----
    # 画面側（hd2d/world/terrain_view.cpp）は町の地面・道・草の株・塀を、
    # `ground_turf` `ground_path` `grass` `palisade` `yard_ground` に接尾辞を付けた名前で探す。
    # 絵は上で作った素材と同じ関数で作る（名前が違うだけ）。
    for i in range(1, 6):
        cat['ground_turf_agb_%02d' % i] = (ground_ash_agb, static(), '町の地面（灰と乾いた土。ground_ash と同じ絵）')
    for i in range(1, 4):
        cat['ground_path_agb_%02d' % i] = (ground_cobble_agb, static(), '店を結ぶ道（石畳の残り。ground_cobble と同じ絵）')
        cat['palisade_agb_%02d' % i] = (town_wall_agb, static(), '町の塀（崩れかけた石の城壁。town_wall と同じ絵）')
    cat['yard_ground_agb'] = (ground_ash_agb, static(), '前庭の地面（灰と乾いた土）')
    # ---- 第 4 回: 壊れた柵・泥と轍・高い枯れ木・散らし----
    for side in ('n', 's', 'w', 'e'):
        cat['fence_%s_agb' % side] = (fence_agb, static(), '前庭の柵（傷んだ割り木。%s 側）' % side)
        for i in range(1, 7):
            cat['fence_%s_agb_%02d' % (side, i)] = (fence_agb, static(), '前庭の柵の変種（01〜03 傷み / 04 垂れた桟 / 05 折れた杭 / 06 倒れた柵。%s 側）' % side)
    for i in range(1, 4):
        #! 素の泥にも東西の轍を入れる——置く側は道の役割のマスにしか _ew を選ばず、大通りの中央の列の
        #! 大半は素の泥だった（実機の絵で確かめた）。南北の通りは `ground_mudns_agb` の組。
        cat['ground_mud_agb_%02d' % i] = ((lambda n: _mud_dir(n, 'ew')), static(), '大通りのぬかるみ（東西の轍つきの素の泥）')
        cat['ground_mudns_agb_%02d' % i] = ((lambda n: _mud_dir(n, 'ns')), static(), '南北の通りのぬかるみ（南北の轍つき）')
        cat['ground_mudns_agb_edge_w_%02d' % i] = ((lambda n: _mud_edge(n, 'w')), static(), '南北の通りのぬかるみの縁（灰が西）')
        cat['ground_mudns_agb_edge_e_%02d' % i] = ((lambda n: _mud_edge(n, 'e')), static(), '南北の通りのぬかるみの縁（灰が東）')
        cat['ground_mud_agb_ew_%02d' % i] = ((lambda n: _mud_dir(n, 'ew')), static(), '大通りのぬかるみ（東西の轍）')
        cat['ground_path_ew_agb_%02d' % i] = ((lambda n: _path_dir(n, 'ew')), static(), '小路（東西の轍）')
        cat['ground_path_ns_agb_%02d' % i] = ((lambda n: _path_dir(n, 'ns')), static(), '小路（南北の轍）')
        cat['mud_puddle_agb_%02d' % i] = (mud_puddle_agb, static(), '泥の溜まり（小物。03 は板つき）')
        for side in ('n', 's', 'w', 'e'):
            cat['ground_mud_agb_edge_%s_%02d' % (side, i)] = ((lambda n, s=side: _mud_edge(n, s)), static(), '大通りのぬかるみの縁（灰のある側 %s）' % side)
    for i in range(1, 3):
        cat['ground_mud_agb_ns_%02d' % i] = ((lambda n: _mud_dir(n, 'ns')), static(), '大通りのぬかるみ（南北の轍）')
        cat['ground_mud_agb_x_%02d' % i] = ((lambda n: _mud_dir(n, 'x')), static(), '大通りのぬかるみ（交差点。踏み荒らし）')
        cat['ground_path_x_agb_%02d' % i] = ((lambda n: _path_dir(n, 'x')), static(), '小路（交差点）')
    for i in range(1, 4):
        cat['dead_tree_tall_agb_%02d' % i] = (dead_tree_tall_agb, static(), '背の高い枯れ木（2.5〜3 マス。太い幹と折れた大枝）')
    cat['grass_green_agb'] = (grass_green_agb, static(motion={'kind': 'wind'}, wind_k=0.7), '緑の草の株（控えめ）')
    for i in range(1, 7):
        cat['litter_agb_%02d' % i] = (litter_agb, static(), 'ゴミ（壺のかけら・ぼろ布・空き瓶・木くず・錆びた鉄片・骨のかけら）')
    cat['grass_agb'] = (dead_grass_agb, static(motion={'kind': 'wind'}, wind_k=0.6), '草の株（枯れ草。dead_grass と同じ絵）')


NAMES_HELP = """名前の一覧は `python tools/voxel/angband_prefabs.py` で標準出力に出す（gen_prefabs.py に渡す用）。"""

if __name__ == '__main__':
    import importlib.util
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location('gen_prefabs', os.path.join(here, 'gen_prefabs.py'))
    gp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gp)
    for key in sorted(gp.CATALOG):
        if key.endswith('_agb') or ('_agb_' in key):
            print(key)
