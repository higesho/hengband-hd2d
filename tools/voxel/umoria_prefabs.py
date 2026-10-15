# -*- coding: utf-8 -*-
"""Umoria の町（ドゥリンの扉の前の交易町）の素材の生成器。

`gen_prefabs.py` から `register(globals())` で呼ばれ、接尾辞 `_umo` のプレハブを `CATALOG` へ足す。
**既存の素材には触らない**（別の名前で足すだけ）。手本は Angband の町の `angband_prefabs.py`。

## 作りの規律（`gen_prefabs.py` 冒頭と同じ）
- 無作為な表面ノイズは入れない。彫りは 1 ボクセルの深さ。色は塊ごとに 1 色
- 接地層（z=0）の平面方向は縮めない。+y は南＝カメラ側。戸口は手前が y 大

## 色の名前
`palette_materials.classify` は**頭の語**で材質を当てるので、`<材質の語>_umo[_l|_d]` の形にする
（`rock_umo` は石、`board_umo` は木、`leaf_umo` は葉、`marb_umo` は白い石、`lit_umo` は光）。

## 指輪物語の要素（ゲーム外の設定。意匠の着想にだけ使う）
銘文の文字列や挿絵の形は写さない。「絶壁の門・柊・夜に光る線・金床と槌・星の冠・2 本の木」を
こちらで単純な形に起こす（設計書 §1 の著作権の註）。
"""
from __future__ import annotations

import numpy as np

G = None   #: gen_prefabs の名前空間（register で入る）
C = None   #: 色の名前 → 索引
V = 32


# ------------------------------------------------------------------ 形の道具
def vol(w=None, d=None, h=32):
    return np.zeros((w or V, d or V, h), np.int16)


def box(v, x0, x1, y0, y1, z0, z1, colour):
    X, Y, Z = v.shape
    x0, x1 = max(0, x0), min(X, x1)
    y0, y1 = max(0, y0), min(Y, y1)
    z0, z1 = max(0, z0), min(Z, z1)
    if (x0 < x1) and (y0 < y1) and (z0 < z1):
        v[x0:x1, y0:y1, z0:z1] = colour


def ball(v, cx, cy, cz, r, colour, squash=1.0):
    X, Y, Z = v.shape
    xs = np.arange(X)[:, None, None] + 0.5 - cx
    ys = np.arange(Y)[None, :, None] + 0.5 - cy
    zs = (np.arange(Z)[None, None, :] + 0.5 - cz) / squash
    v[(xs * xs) + (ys * ys) + (zs * zs) <= (r * r)] = colour


def rod(v, p0, p1, r0, r1, colour):
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


def _variant(name):
    tail = name.rsplit('_', 1)[-1]
    return int(tail) if tail.isdigit() else 0


# ------------------------------------------------------------------ 色
def _palette():
    rl = G['reg_local']
    #! ドワーフの石（店の土台・煙突・扉の前の石畳）。青みのある灰色の花崗岩。
    rl('rock_umo', (118, 120, 126)); rl('rock_umo_l', (142, 144, 150)); rl('rock_umo_d', (96, 98, 104))
    rl('rock_umo_j', (64, 66, 72))           #: 目地（石のどの色よりも暗い）
    #! 霧ふり山脈の絶壁と扉の岩肌。店の石より暗く冷たい。
    rl('rock_umo_cliff', (88, 90, 98)); rl('rock_umo_cliff_l', (108, 110, 118)); rl('rock_umo_cliff_d', (70, 72, 80))
    #! 扉の門の面（磨いた岩。昼はほとんど岩肌と同じに見える）。
    rl('rock_umo_gate', (96, 98, 108)); rl('rock_umo_gate_l', (104, 106, 116))
    #! 扉の線（昼は彫りが少し明るく見えるだけ。夜はこの写しが光る）。
    rl('rock_umo_rune', (126, 130, 142))
    rl('lit_umo_rune', (225, 240, 255))      #: 夜に光る線（イシルディンの銀）
    #! 木組み（濃い茶の梁）と漆喰の壁、板葺きの屋根。
    rl('timber_umo', (82, 58, 40)); rl('timber_umo_d', (60, 42, 30))
    #! 漆喰は煤と雨で黄ばんだ灰（第 2 回。新しい白は「整った町」に見えた——気づいたこと「廃れた町感が無い」）。
    rl('plas_umo', (176, 166, 144)); rl('plas_umo_d', (150, 140, 120)); rl('plas_umo_g', (128, 120, 104))
    rl('timber_umo_l', (112, 90, 66))        #: 漆喰の剥げた所の木舞
    rl('board_umo', (96, 76, 58)); rl('board_umo_l', (116, 94, 72)); rl('board_umo_d', (74, 58, 44))
    #! 柊（濃い緑の照りのある葉と赤い実）。
    rl('leaf_umo', (40, 74, 46)); rl('leaf_umo_l', (58, 100, 62)); rl('leaf_umo_d', (28, 54, 34))
    rl('leaf_umo_berry', (176, 34, 34))
    rl('bark_umo', (70, 62, 54)); rl('bark_umo_l', (92, 82, 72))
    #! 柊郷のエルフの石（白っぽい石。廃墟）。苔の筋。
    rl('marb_umo', (196, 194, 184)); rl('marb_umo_l', (218, 216, 206)); rl('marb_umo_d', (164, 162, 152))
    rl('moss_umo', (92, 112, 70)); rl('moss_umo_d', (66, 84, 52))
    rl('leaf_umo_ivy', (52, 86, 48)); rl('leaf_umo_ivy_l', (72, 110, 60))   #: 壁を這う蔦
    #! 地面（湿った土と草）。
    rl('dirt_umo', (112, 98, 78)); rl('dirt_umo_l', (132, 116, 94)); rl('dirt_umo_d', (92, 80, 64))
    #! 店の小物（第 4 回）。炉の火・銀の灯り・光る石・薬。
    rl('lit_umo_fire', (255, 150, 60)); rl('lit_umo_fire_y', (255, 210, 110))
    rl('lit_umo_silver', (210, 225, 255)); rl('lit_umo_green', (150, 255, 170))
    rl('glass_umo_g', (90, 150, 110)); rl('glass_umo_r', (150, 60, 60)); rl('glass_umo_b', (70, 90, 150))
    rl('iron_umo', (70, 70, 74)); rl('iron_umo_r', (112, 72, 50))
    rl('grass_umo', (86, 118, 64)); rl('grass_umo_l', (108, 140, 78)); rl('grass_umo_d', (66, 94, 50))


def _stone():
    return [K('rock_umo'), K('rock_umo_l'), K('rock_umo_d')]


STONE_W = [0.55, 0.25, 0.20]
COURSE = 6


def _stone_face(v, rng, fx, fy, bx, by, along_x, lo, hi, z0, z1, shades, weights):
    """壁の外に面した 1 面へ石を描く（段ごとに継ぎ目を半枚ずらし、目地は 1 ボクセル彫る）。"""
    def paint(a, b, za, zb, colour):
        if a < b and za < zb:
            if along_x:
                v[a:b, fy, za:zb] = colour
            else:
                v[fx, a:b, za:zb] = colour

    def carve(a, b, za, zb):
        if a < b and za < zb:
            if along_x:
                v[a:b, fy, za:zb] = 0
                v[a:b, by, za:zb] = K('rock_umo_j')
            else:
                v[fx, a:b, za:zb] = 0
                v[bx, a:b, za:zb] = K('rock_umo_j')

    row = 0
    for zc in range(z0, z1, COURSE):
        ztop = min(z1, zc + COURSE)
        zj = ztop - 1 if ztop < z1 else ztop
        p = lo - (row % 2) * 4
        while p < hi:
            w = int(rng.integers(6, 11))
            paint(max(lo, p), min(hi, p + w), zc, zj, pick(rng, shades, weights))
            if lo <= p + w < hi:
                carve(p + w, p + w + 1, zc, zj)
            p += w + 1
        if ztop < z1:
            carve(lo, hi, zj, ztop)
        row += 1


# ------------------------------------------------------------------ 店（ドワーフの石の土台に木組みの上屋）
WALL_H = 34
ROOF_H = 30
EAVE = 4
INSET = 2
STONE_TOP = 16     #: 石積みの土台の天端。ここから上が木組みと漆喰


def _slice(name):
    for part in name.split('_'):
        if part in G['HOUSE_SLICES']:
            return part
    return G['_slice_of'](name)


def _outward(name):
    return G['SLICE_OUT'][_slice(name)]


def _face_of(x0, x1, y0, y1, side):
    if side == 'n':
        return (slice(x0, x1), slice(y0, y0 + 1)), (slice(x0, x1), slice(y0 + 1, y0 + 2))
    if side == 's':
        return (slice(x0, x1), slice(y1 - 1, y1)), (slice(x0, x1), slice(y1 - 2, y1 - 1))
    if side == 'w':
        return (slice(x0, x0 + 1), slice(y0, y1)), (slice(x0 + 1, x0 + 2), slice(y0, y1))
    return (slice(x1 - 1, x1), slice(y0, y1)), (slice(x1 - 2, x1 - 1), slice(y0, y1))


def _proud(x0, x1, y0, y1, side, a, b, z0, z1):
    if side == 'n':
        return (a, b, y0 - 1, y0, z0, z1)
    if side == 's':
        return (a, b, y1, y1 + 1, z0, z1)
    if side == 'w':
        return (x0 - 1, x0, a, b, z0, z1)
    return (x1, x1 + 1, a, b, z0, z1)


def house_wall_umo(name):
    """**店の壁** — 下半分はドワーフの切石（目地を彫る）、上半分は濃い梁の木組みに漆喰。
    梁は柱を 10 ボクセルおきに立て、間に斜めの筋交いを 1 本ずつ。窓は南面だけ。"""
    north, south, west, east = _outward(name)
    rng = rng_for(name)
    v = vol(h=WALL_H)
    x0 = INSET if west else 0
    x1 = V - INSET if east else V
    y0 = INSET if north else 0
    y1 = V - INSET if south else V
    box(v, x0, x1, y0, y1, 0, STONE_TOP, K('rock_umo'))
    box(v, x0, x1, y0, y1, STONE_TOP, WALL_H, K('plas_umo'))
    faces = [s for s, on in (('n', north), ('s', south), ('w', west), ('e', east)) if on]
    for side in faces:
        (fx, fy), (bx, by) = _face_of(x0, x1, y0, y1, side)
        along_x = side in ('n', 's')
        lo, hi = (x0, x1) if along_x else (y0, y1)
        _stone_face(v, rng, fx, fy, bx, by, along_x, lo, hi, 0, STONE_TOP, _stone(), STONE_W)
        # 土台の笠石（1 ボクセル手前へ出る明るい帯）。
        box(v, *_proud(x0, x1, y0, y1, side, lo, hi, STONE_TOP - 2, STONE_TOP), K('rock_umo_l'))
        # 木組み: 土台の上の横木・軒桁・柱・筋交い（面から 1 ボクセル手前）。
        for za, zb in ((STONE_TOP, STONE_TOP + 2), (WALL_H - 3, WALL_H)):
            box(v, *_proud(x0, x1, y0, y1, side, lo, hi, za, zb), K('timber_umo'))
        posts = list(range(lo + 1, hi - 1, 10))
        for p in posts:
            box(v, *_proud(x0, x1, y0, y1, side, p, p + 2, STONE_TOP, WALL_H), K('timber_umo_d'))
        for i, p in enumerate(posts[:-1]):
            span = posts[i + 1] - p - 2
            rise = WALL_H - 3 - (STONE_TOP + 2)
            for t in range(span):
                z = STONE_TOP + 2 + int(round(t * rise / max(1, span)))
                a = p + 2 + (t if (i % 2) == 0 else span - 1 - t)
                box(v, *_proud(x0, x1, y0, y1, side, a, a + 1, z, min(WALL_H - 3, z + 2)), K('timber_umo'))
    variant = _variant(name)
    if south:                                                # 窓（南面）
        wx = ((x0 + x1) // 2) - 4
        wz = 21
        box(v, wx - 1, wx + 9, y1 - 2, y1, wz - 1, wz + 8, K('timber_umo_d'))
        if variant == 4:                                     # 板でふさいだ窓（斜めの板）
            box(v, wx, wx + 8, y1 - 2, y1, wz, wz + 7, K('dark'))
            for t in range(8):
                box(v, wx + t, wx + t + 1, y1, y1 + 1, wz + t // 2 + 1, wz + t // 2 + 3, K('board_umo_d'))
            box(v, wx - 1, wx + 9, y1, y1 + 1, wz + 2, wz + 4, K('board_umo'))
        else:
            box(v, wx, wx + 8, y1 - 2, y1, wz, wz + 7, K('glass'))
            box(v, wx + 3, wx + 5, y1 - 2, y1, wz, wz + 7, K('timber_umo_d'))
    _weather_wall(v, rng, variant, faces, x0, x1, y0, y1)
    return one(v)


def _face_cells(v, side, x0, x1, y0, y1):
    """面の外側 1 ボクセルの (x, y) の並び。"""
    if side == 'n':
        return [(x, y0) for x in range(x0, x1)]
    if side == 's':
        return [(x, y1 - 1) for x in range(x0, x1)]
    if side == 'w':
        return [(x0, y) for y in range(y0, y1)]
    return [(x1 - 1, y) for y in range(y0, y1)]


def _weather_wall(v, rng, variant, faces, x0, x1, y0, y1):
    """**古びた壁**（案 3。気づいたこと 2026-10-03「廃れた町感が無い」→「3 で」）。
    どの変種も: 土台の下の段に苔、漆喰に雨だれの汚れの筋。
    01 汚れだけ / 02 漆喰が剥げて木舞が見える / 03 蔦が土台から這い上がる / 04 板でふさいだ窓とひび。"""
    for side in faces:
        cells = _face_cells(v, side, x0, x1, y0, y1)
        n = len(cells)
        for _ in range(3):                                   # 苔（土台の下の段に塊で）
            a = int(rng.integers(0, max(1, n - 6)))
            w = int(rng.integers(3, 8))
            for (x, y) in cells[a:a + w]:
                for z in range(0, int(rng.integers(2, 6))):
                    if v[x, y, z] != 0 and v[x, y, z] != K('rock_umo_j'):
                        v[x, y, z] = K('moss_umo') if (z % 3) else K('moss_umo_d')
        for _ in range(int(rng.integers(3, 6))):             # 雨だれの汚れの筋
            a = int(rng.integers(0, n))
            ztop = WALL_H - 3
            zlen = int(rng.integers(5, 14))
            for (x, y) in cells[a:a + int(rng.integers(1, 3))]:
                for z in range(ztop - zlen, ztop):
                    if v[x, y, z] == K('plas_umo'):
                        v[x, y, z] = K('plas_umo_d')
        #! 第 3 回: 剥げはどの変種にも入れる（基・01 は 1 か所、02 は 3〜5 か所で大きく）。
        n_flake = int(rng.integers(3, 6)) if variant == 2 else 1
        for _ in range(n_flake):
            if True:
                a = int(rng.integers(2, max(3, n - 10)))
                w = int(rng.integers(5, 11)) if variant == 2 else int(rng.integers(3, 6))
                hgt = int(rng.integers(5, 10)) if variant == 2 else int(rng.integers(3, 6))
                z0 = int(rng.integers(STONE_TOP + 3, WALL_H - 3 - hgt))
                for i, (x, y) in enumerate(cells[a:a + w]):
                    for z in range(z0, z0 + hgt):
                        if v[x, y, z] in (K('plas_umo'), K('plas_umo_d')):
                            v[x, y, z] = K('timber_umo_l') if ((z + i) % 2) else K('plas_umo_g')
        if variant == 3:                                     # 蔦（土台から斜めに這い上がる帯。第 3 回で 2 本）
          for a in (int(rng.integers(0, max(1, n // 2))), int(rng.integers(n // 2, max(n // 2 + 1, n - 6)))):
            for z in range(0, WALL_H - 1):
                c = a + int(round(z * 0.35)) + int(rng.integers(-1, 2))
                for (x, y) in cells[max(0, c - 2):min(n, c + 2)]:
                    if v[x, y, z] != 0 and rng.random() < 0.8:
                        v[x, y, z] = K('leaf_umo_ivy') if rng.random() < 0.6 else K('leaf_umo_ivy_l')
        if variant in (4, 5, 6):                             # 漆喰のひび（2 本）
            for _ in range(2):
                a = int(rng.integers(3, max(4, n - 3)))
                z = WALL_H - 4
                while z > STONE_TOP + 1 and 0 <= a < n:
                    x, y = cells[a]
                    if v[x, y, z] in (K('plas_umo'), K('plas_umo_d')):
                        v[x, y, z] = K('plas_umo_g')
                    z -= 1
                    a += int(rng.integers(-1, 2))
        if variant == 5:                                     # 漆喰が崩れ落ちた穴（2 ボクセル彫って暗がり）
            a = int(rng.integers(3, max(4, n - 10)))
            w, hgt = int(rng.integers(5, 9)), int(rng.integers(5, 9))
            z0 = int(rng.integers(STONE_TOP + 2, WALL_H - 3 - hgt))
            for i, (x, y) in enumerate(cells[a:a + w]):
                for z in range(z0, z0 + hgt):
                    edge = i in (0, w - 1) or z in (z0, z0 + hgt - 1)
                    if edge and rng.random() < 0.5:
                        continue
                    v[x, y, z] = 0
                    xi, yi = x, y
                    if side == 'n':
                        yi = y + 1
                    elif side == 's':
                        yi = y - 1
                    elif side == 'w':
                        xi = x + 1
                    else:
                        xi = x - 1
                    v[xi, yi, z] = K('dark')
        if variant == 6:                                     # 角の石が崩れた壁（端から階段状に欠ける）
            for i, (x, y) in enumerate(cells[:10]):
                cut = STONE_TOP + 2 - int(i * 1.6) - int(rng.integers(0, 3))
                for k in range(3):
                    xi = x + (k if side == 'w' else (-k if side == 'e' else 0))
                    yi = y + (k if side == 'n' else (-k if side == 's' else 0))
                    if cut > 0:
                        v[xi, yi, max(0, cut):STONE_TOP] = 0
        # 煤の帯（第 3 回。軒下 3 段を暗く）。
        for (x, y) in cells:
            for z in range(WALL_H - 6, WALL_H - 3):
                if v[x, y, z] == K('plas_umo'):
                    v[x, y, z] = K('plas_umo_d')


def house_roof_umo(name):
    """**板葺きの屋根** — 濃い茶の板を段に葺き、棟は暗い板。変種で板の明るさの塊を揺らす。"""
    north, south, west, east = _outward(name)
    rng = rng_for(name)
    height, hip = G['_roof_field'](north, south, west, east, EAVE, ROOF_H, 1.0)
    v = vol(h=ROOF_H)
    shades = [K('board_umo'), K('board_umo_l'), K('board_umo_d')]
    plank = rng.choice(shades, size=(V // 3 + 2, V // 3 + 2, ROOF_H // 3 + 1), p=[0.5, 0.25, 0.25])
    for x in range(V):
        for y in range(V):
            h = int(height[x, y])
            for z in range(h):
                if z < EAVE:
                    v[x, y, z] = K('board_umo_d')
                else:
                    row = z // 3
                    off = (row % 2) * 2
                    v[x, y, z] = plank[(x + off) // 3, (y + off) // 3, row]
            if (h % 3) == 0 and h > 0:
                v[x, y, h - 1] = K('board_umo_d')
            if hip[x, y] and (h < ROOF_H):
                v[x, y, h - 1] = K('ridge')
            if h >= ROOF_H and (north or south or west or east):
                v[x, y, h - 1] = K('ridge')
    _weather_roof(v, rng, _variant(name), height, keep_center=(_slice(name) == 'mid'))
    return one(v)


def _keep_clear(cx, cy, w, d, keep_center):
    """煙突の足元（中央 16×16）にかかるなら真。画面側は越屋根（`roof_vent`）を建物の真ん中のマス＝`mid` の
    屋根の上に載せるので、そこに穴やたわみがあると煙突が宙に浮く。"""
    return keep_center and (cx < 24) and (cx + w > 8) and (cy < 24) and (cy + d > 8)


def _weather_roof(v, rng, variant, height, keep_center=False):
    """**古びた屋根**（案 3）。どの変種も苔の塊。
    01〜02 苔だけ / 03 苔が広い / 04 板が数枚抜ける / 05 大きく抜けて垂木が見える / 06 たわんで 1 段落ちた区画。"""
    n_moss = 9 if variant == 3 else int(rng.integers(4, 8))      # 第 3 回で倍に
    for _ in range(n_moss):
        cx, cy = int(rng.integers(2, V - 8)), int(rng.integers(2, V - 8))
        w, d = int(rng.integers(3, 8)), int(rng.integers(3, 7))
        for x in range(cx, cx + w):
            for y in range(cy, cy + d):
                h = int(height[x, y])
                if h > EAVE and v[x, y, h - 1] != 0 and rng.random() < 0.85:
                    v[x, y, h - 1] = K('moss_umo') if rng.random() < 0.6 else K('moss_umo_d')
    holes = []
    if variant in (1, 2):                                    # 第 3 回: 苔の変種にも小さな抜けを 1〜2
        holes = [(int(rng.integers(2, 4)), 3) for _ in range(int(rng.integers(1, 3)))]
    elif variant == 4:
        holes = [(int(rng.integers(3, 7)), 3) for _ in range(int(rng.integers(3, 6)))]
    elif variant == 5:
        holes = [(int(rng.integers(10, 15)), 9), (int(rng.integers(3, 6)), 3)]
    elif variant == 7:                                       # 屋根の半分近くが落ちた
        holes = [(int(rng.integers(16, 22)), 15)]
    for (w, d) in holes:
        cx, cy = int(rng.integers(3, V - 3 - w)), int(rng.integers(3, V - 3 - d))
        for _ in range(20):                                  # 煙突の足元を避けて置き直す
            if not _keep_clear(cx, cy, w, d, keep_center):
                break
            cx, cy = int(rng.integers(1, max(2, V - 1 - w))), int(rng.integers(1, max(2, V - 1 - d)))
        if _keep_clear(cx, cy, w, d, keep_center):
            continue                                         # 大きすぎて避けられない穴は開けない
        for x in range(cx, cx + w):
            for y in range(cy, cy + d):
                h = int(height[x, y])
                nt = max(2, h - 3)
                v[x, y, nt:] = 0
                v[x, y, nt - 1] = K('dark')
        if w >= 9:                                           # 垂木
            for yr in range(cy + 1, cy + d, 4):
                for x in range(cx, cx + w):
                    nt = max(2, int(height[x, yr]) - 3)
                    v[x, yr, nt - 1:nt + 1] = K('timber_umo_d')
    if variant == 6:                                         # たわんだ区画
        cx, cy = int(rng.integers(4, V - 14)), int(rng.integers(4, V - 12))
        if _keep_clear(cx, cy, 10, 8, keep_center):
            cx, cy = (0, 0) if rng.random() < 0.5 else (V - 10, V - 8)
        for x in range(cx, cx + 10):
            for y in range(cy, cy + 8):
                h = int(height[x, y])
                if h > EAVE + 2:
                    top = v[x, y, h - 1]
                    v[x, y, h - 1] = 0
                    v[x, y, h - 2] = top


def house_entrance_umo(name):
    """**石のアーチの戸口** — 切石の半円アーチに、鉄の帯を打った木の扉。踏み石 1 段。"""
    v = vol(h=32)
    box(v, 8, 24, 26, 32, 0, 2, K('rock_umo_l'))             # 踏み石
    box(v, 7, 25, 29, 32, 2, 28, K('rock_umo'))              # アーチの石
    xs = np.arange(V)[:, None] + 0.5 - 16
    zs = np.arange(32)[None, :] + 0.5 - 18
    arch = ((xs * xs) / 36.0 + (zs * zs) / 36.0) <= 1.0
    opening = np.zeros((V, 32), bool)
    opening[10:22, 2:18] = True
    opening |= arch & (np.arange(32)[None, :] >= 18)
    for y in range(29, 32):
        sl = v[:, y, :]
        sl[opening] = 0
    door = opening.copy()
    v[:, 30, :][door] = K('board_umo')                       # 扉（1 ボクセル引っ込む）
    for z in (6, 13):
        box(v, 10, 22, 30, 31, z, z + 1, K('iron'))          # 鉄の帯
    box(v, 18, 20, 30, 31, 9, 11, K('iron_l'))               # 把手
    for z in range(2, 28, 6):                                # アーチの石の目地
        box(v, 7, 9, 31, 32, z, z + 1, K('rock_umo_j'))
        box(v, 23, 25, 31, 32, z, z + 1, K('rock_umo_j'))
    return one(v)


def house_light_umo(name):
    v = vol(h=20)
    box(v, 12, 20, 29, 32, 11, 16, K('lit'))
    return one(v)


def chimney_umo(name):
    """ドワーフの切石の煙突（四角、笠石つき）。"""
    rng = rng_for(name)
    H = 34
    v = vol(h=H)
    box(v, 10, 22, 10, 22, 0, H - 2, K('rock_umo'))
    for z in range(0, H - 2, COURSE):
        box(v, 10, 22, 10, 22, z + COURSE - 1, z + COURSE, K('rock_umo_j'))
        box(v, 10, 22, 10, 22, z, z + COURSE - 1, pick(rng, _stone(), STONE_W))
    box(v, 9, 23, 9, 23, H - 2, H, K('rock_umo_l'))           # 笠石
    box(v, 13, 19, 13, 19, H - 6, H, K('soot'))               # 煙道
    return one(v)


# ------------------------------------------------------------------ 絶壁（霧ふり山脈）
def crag_umo(name):
    """霧ふり山脈の絶壁。形は共通の `crag()`、色は冷たい灰。Angband の岩山と同じく高めにする。"""
    tier = name.split('_')[1]
    heights = {'low': 72, 'mid': 100, 'high': 128, 'peak': 160}
    #! **共通の色だけを使う**。`crag()` は色の番号を uint8 で持つので、ここで登録した色（番号 256 以上）は
    #! 桁あふれして別の色に化けた（生成時の警告で気づいた。Angband の岩山の註と同じ理由）。
    bands = [K('rock_dd'), K('rock_d'), K('rock'), K('rock_d'), K('soot')]
    face_mix = ([K('rock_d'), K('rock_dd'), K('rock')], [0.5, 0.3, 0.2])
    return G['crag'](name, h_override=heights[tier], bands=bands, face_mix=face_mix)


def alpine_umo(name):
    """絶壁の天面の草の株。"""
    rng = rng_for(name)
    v = vol(h=8)
    for _ in range(5):
        x, y = int(rng.integers(10, 22)), int(rng.integers(10, 22))
        box(v, x, x + 2, y, y + 2, 0, int(rng.integers(3, 8)), pick(rng, [K('grass_umo'), K('grass_umo_l'), K('grass_umo_d')]))
    return one(v)


# ------------------------------------------------------------------ ドゥリンの扉
GATE_H = 96        #: 門の岩肌の高さ（絶壁の low 段より高く、mid 段より低い）
FACE_Y = 6         #: 門の面（この y が手前の面）


def durin_gate_umo(name):
    """**ドゥリンの扉**（下り階段のマスの絵。地面ごと置き換える）。北の絶壁に刻まれた滑らかな門の面に、
    2 本の柱と半円のアーチで囲んだ黒い口（開いた扉）。足元は扉の前の平らな石畳。
    昼は岩肌に見える——線は `durin_sigil_umo`（昼の薄い彫り）と `durin_sigil_glow_umo`（夜に光る写し）が受ける。"""
    rng = rng_for(name)
    v = vol(h=GATE_H)
    # 足元の石畳（扉の前を平らに削った所）。
    for x in range(0, V, 8):
        for y in range(FACE_Y, V, 8):
            box(v, x, x + 7, y, y + 7, 0, 1, pick(rng, _stone(), STONE_W))
    # 門の岩肌（北側の y 0..FACE_Y）。上ほどわずかに奥へ。
    for z in range(1, GATE_H):
        back = min(FACE_Y - 1, z // 32)
        box(v, 0, V, 0, FACE_Y - back, z, z + 1, K('rock_umo_gate'))
    # 磨いた門の面（柱の内側の平らな板）。
    box(v, 0, 32, FACE_Y - 1, FACE_Y, 1, 82, K('rock_umo_gate_l'))   # 線を受ける板（第 4 回で上へ広げた）
    # 開いた口（黒）。高さ 40 のアーチ。
    xs = np.arange(V)[:, None] + 0.5 - 16
    zs = np.arange(GATE_H)[None, :] + 0.5 - 30
    arch = ((xs * xs) + (zs * zs)) <= 81.0
    mouth = np.zeros((V, GATE_H), bool)
    mouth[7:25, 1:30] = True
    mouth |= arch & (np.arange(GATE_H)[None, :] >= 30)
    for y in range(0, FACE_Y):
        sl = v[:, y, :]
        sl[mouth] = 0
    v[:, 0, :][mouth] = K('dark')
    # 2 本の柱（面から 1 ボクセル手前へ出る）。
    for (xa, xb) in ((4, 7), (25, 28)):
        box(v, xa, xb, FACE_Y, FACE_Y + 1, 1, 44, K('rock_umo_gate_l'))
    box(v, 4, 28, FACE_Y, FACE_Y + 1, 44, 47, K('rock_umo_gate_l'))   # 楣
    return one(v)


def _sigil_mask():
    """扉の線（x, z の格子）。アーチの縁・柱の外の 2 本の木・楣の上の金床と槌・その上の星・頂の冠と七つの星の弧。
    単純な幾何形に起こしたもの（設計書 §1 の著作権の註）。第 4 回: 金床と槌がアーチの黒い口の中に入っていたので、
    楣（z 44〜47）の上の板へ移した。"""
    m = np.zeros((V, GATE_H), bool)
    xs = np.arange(V)[:, None] + 0.5 - 16
    zs = np.arange(GATE_H)[None, :] + 0.5 - 30
    d = np.sqrt((xs * xs) + (zs * zs))
    m |= (np.abs(d - 11.0) < 0.6) & (np.arange(GATE_H)[None, :] >= 30)   # アーチの縁
    m[5, 2:44] = True                                                    # 柱の縁の線
    m[26, 2:44] = True
    for x, out in ((2, -1), (29, 1)):                                    # 2 本の木（柱の外側。幹と枝）
        m[x, 2:60] = True
        for k in range(1, 4):
            for z0 in (30, 38, 46, 54):
                xx = x - out * k
                if 0 <= xx < V:
                    m[xx, z0 + k] = True
    m[12:21, 50] = True                                                  # 金床（楣の上）
    m[14:19, 51:53] = True
    m[11:22, 53] = True
    m[22, 50:56] = True                                                  # 槌（柄と頭）
    m[20:25, 56:58] = True
    for k in range(-2, 3):                                               # 金床の上の星
        m[16 + k, 61] = True
        m[16, 61 + k] = True
    for x in range(11, 22):                                              # 冠
        m[x, 66] = True
    for x in (11, 13, 16, 19, 21):
        m[x, 67:70] = True
    for (sx, sz) in ((6, 70), (8, 74), (11, 77), (16, 78), (21, 77), (24, 74), (26, 70)):
        m[sx, sz] = True                                                 # 七つの星の弧
    return m


def _sigil(colour, y):
    v = vol(h=GATE_H)
    m = _sigil_mask()
    v[:, y, :][m] = colour
    return one(v)


def durin_sigil_umo(name):
    """扉の線（昼）。門の面から 1 ボクセル手前に、少し明るい石の彫り。"""
    return _sigil(K('rock_umo_rune'), FACE_Y)


def durin_sigil_glow_umo(name):
    """扉の線（夜に光る写し）。`durin_sigil_umo` と同じ位置・同じ形。置く側が夜だけ自発光をつける。"""
    #! 昼の線より 1 ボクセル手前に置く（同じ位置だと昼の線と重なって光が見えなかった。2026-10-03 の実機の絵）。
    return _sigil(K('lit_umo_rune'), FACE_Y + 1)


# ------------------------------------------------------------------ 柊
def holly_umo(name):
    """**柊** — 濃い緑の照りのある葉の、裾の広い卵形の常緑樹に赤い実。番号が大きいほど高い
    （01 = 1.6 マス・02 = 2.2 マス・03 = 2.8 マス・04 = 3.6 マスの大木）。"""
    rng = rng_for(name)
    step = _variant(name) or 2
    h = (52, 70, 90, 116)[min(3, step - 1)]
    #! 樹冠は**幹のマスの中に収める**（気づいたこと 2026-10-03「木の左上に立つと透過が発生」）。
    #! 画面側はプレハブのボクセルが覆うマス全部を「視線を遮るマス」として木の高さで記録するので、
    #! 隣のマスへ 3 ボクセルはみ出しただけで、木の北西に立つと自機の南のマスが遮る扱いになった。
    span = V
    v = np.zeros((span, span, h), np.int16)
    cx = cy = V // 2
    r_base = max(2.0, h * 0.035)
    rod(v, (cx, cy, 0), (cx, cy, h * 0.55), r_base, r_base * 0.6, K('bark_umo'))
    crown_bot = int(h * 0.18)
    #! 第 2 回: 細身にし（0.30→0.22）、縁を大きく揺らして徒長枝の塊を足す（伸び放題で不揃い。案 3）。
    crown_r = 11.0                                           # 揺らぎ（最大 1.34 倍）を足しても 15 ボクセルに収まる
    xs = np.arange(span)[:, None] + 0.5 - cx
    ys = np.arange(span)[None, :] + 0.5 - cy
    d2 = (xs * xs) + (ys * ys)
    ang = np.arctan2(ys, xs)
    lean = float(rng.uniform(0, 6.28))
    for z in range(crown_bot, h):
        t = (z - crown_bot) / float(max(1, h - crown_bot))
        r = crown_r * (np.sin(np.pi * (0.15 + 0.85 * t)) ** 0.8) * (1.0 - 0.35 * t)
        if r < 1.0:
            continue
        wob = 1.0 + 0.22 * np.sin(3 * ang + lean + z * 0.13) + 0.12 * np.sin(7 * ang - z * 0.31)
        m = d2 <= (r * wob) ** 2
        inner = d2 <= (r * 0.55) ** 2
        sl = v[:, :, z]
        shade = K('leaf_umo_d') if t < 0.35 else (K('leaf_umo') if t < 0.75 else K('leaf_umo_l'))
        sl[m & ~inner & (sl == 0)] = shade
        sl[inner & (sl == 0)] = K('leaf_umo_d')
    #! 徒長枝（樹冠から外へ突き出た枝と葉の塊）は外した（気づいたこと 2026-10-03「飛び出る枝は不要」）。
    # 赤い実（外側の葉の上に点々）。
    for _ in range(int(h * 0.5)):
        z = int(rng.integers(crown_bot + 4, h - 2))
        a = rng.uniform(0, 6.28)
        t = (z - crown_bot) / float(max(1, h - crown_bot))
        r = crown_r * (np.sin(np.pi * (0.15 + 0.85 * t)) ** 0.8) * (1.0 - 0.35 * t)
        x = int(round(cx + np.cos(a) * r * 0.95))
        y = int(round(cy + np.sin(a) * r * 0.95))
        if 0 <= x < span and 0 <= y < span and v[x, y, z] != 0:
            v[x, y, z] = K('leaf_umo_berry')
    return one(v)


# ------------------------------------------------------------------ 柊郷の廃墟（エルフの白い石）
def elf_ruin_umo(name):
    """**柊郷の廃墟**（通れない瓦礫のマス）。01 折れた柱 / 02 崩れたアーチの片脚 / 03 低い壁の残り /
    04 倒れた柱の胴と台座。白っぽい石に苔の筋。"""
    rng = rng_for(name)
    variant = _variant(name) or 1
    v = vol(h=72)
    marb = [K('marb_umo'), K('marb_umo_l'), K('marb_umo_d')]
    box(v, 4, 28, 4, 28, 0, 3, K('marb_umo_d'))              # 台座
    if variant == 1:
        top = int(rng.integers(40, 64))
        for z in range(3, top):
            r = 6.0 if (z % 12) else 6.6
            ball(v, 16, 16, z, 0, 0)
            xs = np.arange(V)[:, None] + 0.5 - 16
            ys = np.arange(V)[None, :] + 0.5 - 16
            v[:, :, z][(xs * xs + ys * ys) <= r * r] = marb[(z // 12) % 3]
        for _ in range(6):                                   # 折れ口
            x, y = int(rng.integers(10, 22)), int(rng.integers(10, 22))
            v[x:x + 3, y:y + 3, top - int(rng.integers(1, 6)):top] = 0
        box(v, 18, 28, 20, 30, 3, 8, K('marb_umo_d'))       # 足元の欠片
    elif variant == 2:
        box(v, 6, 14, 10, 22, 3, 56, K('marb_umo'))          # 片脚
        for t in range(14):                                  # アーチの残り（東へ曲がって途切れる）
            z = 56 + int(round(np.sin(t / 14.0 * 1.4) * 10))
            box(v, 6 + t, 9 + t, 10, 22, z, z + 6, marb[t % 3])
        box(v, 20, 28, 12, 24, 3, 7, K('marb_umo_d'))
    elif variant == 3:
        for x in range(2, 30):
            top = 22 + int(8 * np.sin(x * 0.5)) + int(rng.integers(-3, 3))
            box(v, x, x + 1, 12, 20, 3, top, marb[(x // 5) % 3])
        for x in range(2, 30, 6):
            box(v, x, x + 1, 12, 20, 3, 30, K('marb_umo_d'))  # 目地
    else:
        box(v, 4, 14, 4, 14, 3, 18, K('marb_umo'))           # 立った台座の残り
        rod(v, (12, 20, 6), (30, 26, 6), 5.0, 5.0, K('marb_umo_l'))   # 倒れた胴
    # 苔の筋（北面と天面の一部）。
    for _ in range(8):
        x, y = int(rng.integers(2, 30)), int(rng.integers(2, 30))
        zs = np.nonzero(v[x, y, :])[0]
        if len(zs):
            v[x, y, zs[-1]] = K('moss_umo')
    return one(v)


# ------------------------------------------------------------------ 地面と草
def _ground(layer, under):
    v = vol(h=2)
    box(v, 0, V, 0, V, 0, 1, under)
    v[:, :, 1] = layer
    return [('main', v, (0, 0, -2))]


def ground_dirt_umo(name):
    """町の地面 — 湿った土に草の斑。"""
    rng = rng_for(name)
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('dirt_umo'), K('dirt_umo_d'), K('dirt_umo_l'), K('grass_umo_d')], [0.42, 0.24, 0.16, 0.18])
    G['_grain'](rng, layer, layer > 0, [K('grass_umo'), K('rock_umo_d')], [0.04, 0.02], block=2)
    return _ground(layer, K('dirt_umo_d'))


def ground_flagstone_umo(name):
    """扉の前と並木道の**古い石畳** — 3 割ほどの石が抜けて土と草、残った石も欠けて苔が乗る。
    目地と抜けた所に草が立つ（案 3。整った石畳は「廃れた町感が無い」と言われた）。"""
    rng = rng_for(name)
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('dirt_umo_d'), K('grass_umo_d'), K('dirt_umo')], [0.45, 0.35, 0.2])
    placed = np.zeros((V, V), bool)
    stone = np.zeros((V, V), bool)
    for _ in range(140):
        w, d = int(rng.integers(4, 8)), int(rng.integers(4, 7))
        x, y = int(rng.integers(0, V - w + 1)), int(rng.integers(0, V - d + 1))
        if placed[max(0, x - 1):x + w + 1, max(0, y - 1):y + d + 1].any():
            continue
        placed[x:x + w, y:y + d] = True
        if rng.random() < 0.5:                               # 抜けた石（第 3 回で 3 割→5 割）
            continue
        layer[x:x + w, y:y + d] = pick(rng, _stone(), STONE_W)
        stone[x:x + w, y:y + d] = True
        if rng.random() < 0.5:                               # 欠けた角
            cx = x + (w - 1 if rng.random() < 0.5 else 0)
            cy = y + (d - 1 if rng.random() < 0.5 else 0)
            layer[cx, cy] = K('dirt_umo_d')
            stone[cx, cy] = False
        if rng.random() < 0.25:                              # 苔
            layer[x:x + 2, y:y + 2] = K('moss_umo')
    #! 高さは 3 まで（草は 1 段）。地面より 2 段上へ出ると、画面側が「地面」（天面 0.05 マス以下）と
    #! みなさず、カットアウェイで抜いて背景が透けた（気づいたこと 2026-10-03「木の横に立って…」）。
    v = vol(h=3)
    box(v, 0, V, 0, V, 0, 1, K('dirt_umo_d'))
    v[:, :, 1] = layer
    gaps = np.argwhere(~stone)
    for _ in range(min(len(gaps), 28)):                      # 目地と抜けた所の草
        x, y = gaps[int(rng.integers(0, len(gaps)))]
        v[x, y, 2] = K('grass_umo') if rng.random() < 0.6 else K('grass_umo_l')
    return [('main', v, (0, 0, -2))]


def ground_trodden_umo(name):
    """**踏み固めた土の道**（通りの端と脇道。決めたこと 2026-10-03「通りの中央だけを石畳にして、端を土と草に」）。
    湿った土に轍のような暗い筋と、伸びた草の斑。"""
    rng = rng_for(name)
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('dirt_umo'), K('dirt_umo_d'), K('grass_umo_d'), K('grass_umo')], [0.38, 0.27, 0.2, 0.15])
    for _ in range(int(rng.integers(1, 3))):                 # 踏み跡の暗い筋
        y = int(rng.integers(4, V - 4))
        for x in range(V):
            if rng.random() < 0.85:
                layer[x, y:y + 2] = K('dirt_umo_d')
            y = int(np.clip(y + int(rng.integers(-1, 2)) * (rng.random() < 0.2), 1, V - 3))
    v = vol(h=3)                                             # 高さは 3 まで（上の石畳と同じ理由）
    box(v, 0, V, 0, V, 0, 1, K('dirt_umo_d'))
    v[:, :, 1] = layer
    for _ in range(18):                                      # 草の立ち上がり
        x, y = int(rng.integers(0, V)), int(rng.integers(0, V))
        if layer[x, y] in (K('grass_umo'), K('grass_umo_d')):
            v[x, y, 2] = K('grass_umo_l')
    return [('main', v, (0, 0, -2))]


def ground_grass_umo(name):
    """草地の地面。"""
    rng = rng_for(name)
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('grass_umo'), K('grass_umo_d'), K('grass_umo_l'), K('dirt_umo')], [0.45, 0.25, 0.2, 0.1])
    return _ground(layer, K('dirt_umo_d'))


def grass_tuft_umo(name):
    """草の株（散らし）。"""
    rng = rng_for(name)
    v = vol(h=10)
    for _ in range(7):
        x, y = int(rng.integers(8, 24)), int(rng.integers(8, 24))
        box(v, x, x + 1, y, y + 1, 0, int(rng.integers(4, 10)), pick(rng, [K('grass_umo'), K('grass_umo_l'), K('grass_umo_d')]))
    return one(v)


FENCE_H = 12


def fence_umo(name):
    """前庭の低い石垣（切石を 2 段積み、笠石）。名前は `fence_<n|s|w|e>_umo[_NN]`。"""
    side = name.split('_')[1]
    rng = rng_for(name)
    th = 4
    v = vol(h=FENCE_H)
    lo = 0 if side in ('n', 'w') else V - th
    for z0 in (0, 5):
        p = -int(rng.integers(0, 6)) if z0 else 0
        while p < V:
            w = int(rng.integers(6, 11))
            a, b = max(0, p), min(V, p + w)
            c = pick(rng, _stone(), STONE_W)
            if side in ('n', 's'):
                box(v, a, b - 1, lo, lo + th, z0, z0 + 4, c)
            else:
                box(v, lo, lo + th, a, b - 1, z0, z0 + 4, c)
            p += w
    if side in ('n', 's'):
        box(v, 0, V, lo, lo + th, 9, 11, K('rock_umo_l'))
    else:
        box(v, lo, lo + th, 0, V, 9, 11, K('rock_umo_l'))
    return one(v)


def house_wall_ruin_umo(name):
    """**半壊した店の崩れた側** — 切石の土台だけが不揃いな高さで残り、上の木組みは落ちている。
    床は土で、崩れた石・折れた梁・草。"""
    north, south, west, east = _outward(name)
    rng = rng_for(name)
    v = vol(h=WALL_H)
    x0 = INSET if west else 0
    x1 = V - INSET if east else V
    y0 = INSET if north else 0
    y1 = V - INSET if south else V
    #! 床は 4 ボクセルの厚み。画面側は壁の 1 枚目を 0.08 マス（約 2.6 ボクセル）沈めて置く（`kFootingDepth`）ので、
    #! 1 ボクセルの床は地面の下に隠れ、敷地の灰色の地面が見えていた（2026-10-03 の実機の絵）。
    FLOOR = 4
    box(v, x0, x1, y0, y1, 0, FLOOR, K('dirt_umo_d'))
    top = np.zeros((V, V), np.int16)
    G['_mottle'](rng, top, [K('dirt_umo_d'), K('dirt_umo'), K('grass_umo_d'), K('rock_umo_d')], [0.4, 0.25, 0.2, 0.15])
    v[x0:x1, y0:y1, FLOOR - 1] = top[x0:x1, y0:y1]
    th = 4
    for side, on in (('n', north), ('s', south), ('w', west), ('e', east)):
        if not on:
            continue
        cells = _face_cells(v, side, x0, x1, y0, y1)
        phase = rng.uniform(0, 3)
        for i, (x, y) in enumerate(cells):
            top = int(np.clip(8 + 6 * np.sin(i * 0.4 + phase) + rng.integers(-2, 3), 2, STONE_TOP))
            for k in range(th):
                xx = x + (k if side == 'w' else (-k if side == 'e' else 0))
                yy = y + (k if side == 'n' else (-k if side == 's' else 0))
                v[xx, yy, 0:top] = pick(rng, _stone(), STONE_W)
                if rng.random() < 0.4:
                    v[xx, yy, top - 1] = K('moss_umo')
    for _ in range(int(rng.integers(3, 6))):
        x, y = int(rng.integers(4, V - 6)), int(rng.integers(4, V - 6))
        box(v, x, x + int(rng.integers(3, 6)), y, y + int(rng.integers(3, 5)), FLOOR, FLOOR + int(rng.integers(2, 5)), pick(rng, _stone(), STONE_W))
    a = rng.uniform(0, 3.14)
    rod(v, (16 - np.cos(a) * 12, 16 - np.sin(a) * 12, FLOOR + 1), (16 + np.cos(a) * 12, 16 + np.sin(a) * 12, FLOOR + 8), 1.4, 1.4, K('timber_umo_d'))
    for _ in range(14):
        x, y = int(rng.integers(2, V - 2)), int(rng.integers(2, V - 2))
        if v[x, y, FLOOR] == 0:
            v[x, y, FLOOR:FLOOR + int(rng.integers(2, 6))] = K('grass_umo')
    return one(v)


def house_roof_ruin_umo(name):
    """半壊した店の崩れた側の屋根 — 屋根は無く、垂木が 1 本斜めに残ることがある。"""
    rng = rng_for(name)
    v = vol(h=ROOF_H)
    if rng.random() < 0.6:
        y = int(rng.integers(6, V - 6))
        rod(v, (2, y, 2), (V - 3, y + int(rng.integers(-4, 5)), int(rng.integers(6, 16))), 1.2, 1.2, K('timber_umo_d'))
    return one(v)


def weeds_umo(name):
    """**伸びた雑草**（散らし）。01 背の高い草の株 / 02 穂の出た草 / 03 蔓と落ち葉。"""
    rng = rng_for(name)
    variant = _variant(name) or 1
    v = vol(h=18)
    shades = [K('grass_umo'), K('grass_umo_l'), K('grass_umo_d'), K('moss_umo')]
    if variant == 3:
        for _ in range(30):
            x, y = int(rng.integers(4, 28)), int(rng.integers(4, 28))
            v[x, y, 0] = pick(rng, [K('leaf_umo_d'), K('board_umo_d'), K('grass_umo_d')])
        for _ in range(3):
            x, y = int(rng.integers(6, 26)), int(rng.integers(6, 26))
            rod(v, (x, y, 0), (x + int(rng.integers(-6, 7)), y + int(rng.integers(-6, 7)), 1), 0.8, 0.8, K('leaf_umo_ivy'))
        return one(v)
    for _ in range(int(rng.integers(10, 16))):
        x, y = int(rng.integers(8, 24)), int(rng.integers(8, 24))
        top = int(rng.integers(6, 17))
        lx, ly = int(rng.integers(-2, 3)), int(rng.integers(-2, 3))
        rod(v, (x, y, 0), (x + lx, y + ly, top), 0.6, 0.5, pick(rng, shades, [0.4, 0.25, 0.25, 0.1]))
        if variant == 2 and rng.random() < 0.6:
            box(v, x + lx, x + lx + 1, y + ly, y + ly + 1, top, min(18, top + 2), K('dirt_umo_l'))
    return one(v)


def stone_chunk_umo(name):
    """**崩れた切石の塊**（散らし）。01 店の青灰の石 / 02 エルフの白い石 / 03 苔むした石の山。"""
    rng = rng_for(name)
    variant = _variant(name) or 1
    v = vol(h=10)
    shades = [K('marb_umo'), K('marb_umo_l'), K('marb_umo_d')] if variant == 2 else _stone()
    for _ in range(int(rng.integers(2, 5)) if variant != 3 else 6):
        x, y = int(rng.integers(6, 22)), int(rng.integers(6, 22))
        w, d, hgt = int(rng.integers(3, 7)), int(rng.integers(3, 6)), int(rng.integers(2, 6))
        z0 = int(rng.integers(0, 2))
        box(v, x, x + w, y, y + d, z0, z0 + hgt, pick(rng, shades))
        if variant == 3 or rng.random() < 0.3:
            box(v, x, x + w, y, y + d, z0 + hgt - 1, z0 + hgt, K('moss_umo'))
    return one(v)


# ------------------------------------------------------------------ 店ごとの小物（第 4 回。どれも古びた形）
def forge_umo(name):
    """ドワーフの鍛冶場の炉 — 切石の炉床と、角の欠けた石の煙突。火は `forge_fire_umo`（昼も光る）。"""
    rng = rng_for(name)
    v = vol(h=40)
    box(v, 6, 26, 8, 24, 0, 12, K('rock_umo'))
    for z in range(0, 12, COURSE):
        box(v, 6, 26, 8, 24, z + COURSE - 1, z + COURSE, K('rock_umo_j'))
    box(v, 10, 22, 14, 24, 6, 12, 0)                         # 火口（南に開く）
    box(v, 10, 22, 14, 15, 6, 12, K('soot'))
    box(v, 12, 20, 8, 14, 12, 38, K('rock_umo_d'))           # 煙突
    box(v, 12, 15, 8, 11, 34, 38, 0)                         # 欠けた角
    box(v, 6, 9, 20, 24, 10, 12, K('moss_umo'))              # 苔
    box(v, 22, 26, 8, 12, 0, 5, K('moss_umo_d'))
    return one(v)


def forge_fire_umo(name):
    v = vol(h=40)
    box(v, 11, 21, 15, 23, 6, 9, K('lit_umo_fire'))
    box(v, 13, 19, 16, 22, 9, 11, K('lit_umo_fire_y'))
    return one(v)


def anvil_umo(name):
    """錆の浮いた金床（丸太の台に載る）。"""
    v = vol(h=16)
    box(v, 11, 21, 11, 21, 0, 7, K('bark_umo'))
    box(v, 9, 23, 13, 19, 7, 9, K('iron_umo'))
    box(v, 7, 25, 12, 20, 9, 12, K('iron_umo'))
    box(v, 23, 27, 14, 18, 10, 12, K('iron_umo'))           # 角
    box(v, 9, 13, 12, 15, 11, 12, K('iron_umo_r'))          # 錆
    box(v, 18, 22, 17, 20, 9, 11, K('iron_umo_r'))
    return one(v)


def weapon_rack_umo(name):
    """錆びた武器立て（剣 2 本と斧 1 本。1 本は倒れている）。"""
    v = vol(h=28)
    for x in (6, 25):
        box(v, x, x + 2, 14, 18, 0, 22, K('timber_umo_d'))
    box(v, 6, 27, 14, 18, 18, 20, K('timber_umo'))
    box(v, 6, 27, 14, 18, 6, 8, K('timber_umo'))
    for x in (10, 14):
        box(v, x, x + 1, 15, 17, 2, 24, K('iron_umo'))
        box(v, x - 1, x + 2, 15, 17, 18, 19, K('iron_umo_r'))
    box(v, 19, 20, 15, 17, 2, 22, K('timber_umo_d'))
    box(v, 17, 22, 15, 17, 18, 23, K('iron_umo'))
    rod(v, (8, 24, 1), (26, 28, 1), 0.7, 0.7, K('iron_umo_r'))   # 倒れた剣
    return one(v)


def silver_lamp_umo(name):
    """エルフの銀の灯り — 白い石の細い柱に、葉の形の傘。灯は `silver_lamp_glow_umo`（夜に光る）。"""
    v = vol(h=56)
    box(v, 13, 19, 13, 19, 0, 3, K('marb_umo_d'))
    box(v, 15, 17, 15, 17, 3, 44, K('marb_umo'))
    for t in range(6):                                       # 傘（左右へ垂れる葉）
        box(v, 15 - t, 17 + t, 15, 17, 48 - t // 2, 49 - t // 2, K('marb_umo_l'))
    box(v, 14, 18, 14, 18, 44, 47, K('marb_umo_l'))
    box(v, 15, 17, 15, 17, 30, 34, K('moss_umo'))            # 柱の苔
    return one(v)


def silver_lamp_glow_umo(name):
    v = vol(h=56)
    box(v, 14, 18, 14, 18, 40, 44, K('lit_umo_silver'))
    return one(v)


def glow_stone_umo(name):
    """光る石の載った台（魔法屋の前）。光は `glow_stone_glow_umo`。"""
    v = vol(h=20)
    box(v, 10, 22, 10, 22, 0, 8, K('marb_umo_d'))
    box(v, 11, 21, 11, 21, 8, 9, K('marb_umo'))
    return one(v)


def glow_stone_glow_umo(name):
    v = vol(h=20)
    ball(v, 16, 16, 12.5, 3.6, K('lit_umo_silver'))
    return one(v)


def stone_shrine_umo(name):
    """苔むした古い石の祠（寺院の前庭）。屋根石の片側が落ちている。"""
    rng = rng_for(name)
    v = vol(h=40)
    box(v, 6, 26, 8, 26, 0, 4, K('rock_umo_d'))
    box(v, 9, 23, 10, 22, 4, 24, K('rock_umo'))
    box(v, 12, 20, 21, 22, 6, 18, K('dark'))                # 龕（南）
    box(v, 14, 18, 20, 21, 7, 11, K('marb_umo'))            # 龕の中の小さな像
    box(v, 7, 20, 8, 24, 24, 27, K('rock_umo_l'))           # 屋根石（東の端が落ちる）
    box(v, 21, 27, 18, 24, 0, 3, K('rock_umo_l'))           # 落ちた屋根石
    for _ in range(10):
        x, y = int(rng.integers(7, 21)), int(rng.integers(8, 24))
        v[x, y, 26] = K('moss_umo')
    box(v, 9, 12, 10, 22, 4, 9, K('moss_umo_d'))
    return one(v)


def alembic_umo(name):
    """錬金術の蒸留器 — 石の台の上の丸い硝子の瓶と曲がった管。瓶の中が淡く光る（`alembic_glow_umo`。昼も光る）。"""
    v = vol(h=36)
    box(v, 8, 24, 10, 22, 0, 10, K('rock_umo'))
    ball(v, 13, 16, 15, 4.5, K('glass_umo_g'))
    rod(v, (13, 16, 19), (24, 16, 24), 0.8, 0.8, K('iron_umo_r'))
    rod(v, (24, 16, 24), (24, 16, 10), 0.8, 0.8, K('iron_umo_r'))
    box(v, 21, 27, 13, 19, 10, 13, K('glass_umo_b'))
    return one(v)


def alembic_glow_umo(name):
    v = vol(h=36)
    ball(v, 13, 16, 14, 3.0, K('lit_umo_green'))
    return one(v)


def potion_shelf_umo(name):
    """傾いた薬瓶の棚（瓶の半分は割れて無い）。"""
    rng = rng_for(name)
    v = vol(h=30)
    for x in (6, 26):
        box(v, x, x + 2, 14, 20, 0, 26, K('board_umo_d'))
    for z in (2, 12, 22):
        box(v, 6, 28, 14, 20, z, z + 2, K('board_umo'))
        for x in range(9, 25, 4):
            if rng.random() < 0.55:
                box(v, x, x + 2, 16, 18, z + 2, z + 2 + int(rng.integers(3, 6)),
                    pick(rng, [K('glass_umo_g'), K('glass_umo_r'), K('glass_umo_b')]))
    return one(v)


def barrel_umo(name):
    """古い樽 2 つ（1 つは箍が外れて板が開いている）。"""
    v = vol(h=20)
    xs = np.arange(V)[:, None] + 0.5
    ys = np.arange(V)[None, :] + 0.5
    for (cx, cy, broken) in ((11, 14, False), (22, 19, True)):
        m = ((xs - cx) ** 2 + (ys - cy) ** 2) <= 25
        for z in range(0, 16):
            sl = v[:, :, z]
            sl[m] = K('board_umo') if (z % 5) else K('iron_umo')
        inner = ((xs - cx) ** 2 + (ys - cy) ** 2) <= 9
        v[:, :, 15][inner] = K('board_umo_d')
        if broken:
            box(v, cx + 2, cx + 6, cy - 1, cy + 2, 6, 16, 0)
            rod(v, (cx + 6, cy, 0), (cx + 10, cy + 3, 0), 1.0, 1.0, K('board_umo_d'))
    return one(v)


def sack_umo(name):
    """麻袋 3 つ（1 つは破れて中身がこぼれる）。"""
    v = vol(h=14)
    for (cx, cy, h) in ((10, 12, 10), (19, 13, 12), (14, 21, 8)):
        ball(v, cx, cy, h / 2.0, 5.0, K('dirt_umo_l'), squash=1.3)
        box(v, cx - 1, cx + 1, cy - 1, cy + 1, h, h + 2, K('dirt_umo'))
    box(v, 18, 26, 20, 26, 0, 1, K('grass_umo_d'))          # こぼれた穀物
    return one(v)


def register(g):
    """`gen_prefabs.py` の名前空間 `g` へ Umoria の町の素材を足す。"""
    global G, C, V
    G = g
    C = g['C']
    V = g['V']
    g['TOWN_STYLES'] = tuple(g['TOWN_STYLES']) + ('umo',)
    _palette()
    cat = g['CATALOG']
    static = g['static_part']

    for sl in g['HOUSE_SLICES']:
        cat['house_wall_%s_umo' % sl] = (house_wall_umo, static(), 'Umoria の町の壁（ドワーフの切石の土台に木組みと漆喰。%s）' % sl)
        cat['house_roof_%s_umo' % sl] = (house_roof_umo, static(), 'Umoria の町の屋根（濃い茶の板葺き。%s）' % sl)
    # 第 2 回（案 3）: 傷みの変種（壁 4・屋根 6）と半壊した店の組・散らし。
    for sl in g['HOUSE_SLICES']:
        for i in range(1, 7):
            cat['house_wall_%s_umo_%02d' % (sl, i)] = (house_wall_umo, static(), 'Umoria の町の古びた壁（01 汚れ / 02 漆喰の剥げ / 03 蔦 / 04 ふさいだ窓とひび。%s）' % sl)
        for i in range(1, 8):
            cat['house_roof_%s_umo_%02d' % (sl, i)] = (house_roof_umo, static(), 'Umoria の町の古びた屋根（01〜03 苔 / 04 抜けた板 / 05 大きな穴 / 06 たわみ。%s）' % sl)
        cat['house_wall_ruin_%s_umo' % sl] = (house_wall_ruin_umo, static(), '半壊した店の崩れた側（石の土台だけ残る。%s）' % sl)
        cat['house_roof_ruin_%s_umo' % sl] = (house_roof_ruin_umo, static(), '半壊した店の崩れた側の屋根（垂木の残り。%s）' % sl)
    for i in range(1, 4):
        cat['weeds_umo_%02d' % i] = (weeds_umo, static(motion={'kind': 'wind'}, wind_k=0.6), '伸びた雑草（01 株 / 02 穂 / 03 蔓と落ち葉）')
        cat['stone_chunk_umo_%02d' % i] = (stone_chunk_umo, static(), '崩れた石の塊（01 店の石 / 02 エルフの白い石 / 03 苔むした山）')
    # 第 4 回: 店ごとの小物（どれも古びた形。火・灯りは別体で、置く側が自発光をつける）。
    props = {
        'forge': (forge_umo, 'ドワーフの鍛冶場の炉（防具屋・武器屋）。火は forge_fire_umo'),
        'forge_fire': (forge_fire_umo, '炉の火（昼も光る）'),
        'anvil': (anvil_umo, '錆の浮いた金床'),
        'weapon_rack': (weapon_rack_umo, '錆びた武器立て'),
        'silver_lamp': (silver_lamp_umo, 'エルフの銀の灯り（魔法屋）。灯は silver_lamp_glow_umo'),
        'silver_lamp_glow': (silver_lamp_glow_umo, '銀の灯り（夜に光る）'),
        'glow_stone': (glow_stone_umo, '光る石の台（魔法屋）。光は glow_stone_glow_umo'),
        'glow_stone_glow': (glow_stone_glow_umo, '光る石'),
        'stone_shrine': (stone_shrine_umo, '苔むした古い石の祠（寺院）'),
        'alembic': (alembic_umo, '蒸留器（錬金術店）。光は alembic_glow_umo'),
        'alembic_glow': (alembic_glow_umo, '蒸留器の瓶の淡い光'),
        'potion_shelf': (potion_shelf_umo, '傾いた薬瓶の棚（錬金術店）'),
        'barrel': (barrel_umo, '古い樽（雑貨屋）'),
        'sack': (sack_umo, '麻袋（雑貨屋）'),
    }
    for stem, (fn, note) in props.items():
        cat['%s_umo' % stem] = (fn, static(), note)
    cat['house_entrance_umo'] = (house_entrance_umo, static(), 'Umoria の町の戸口（石のアーチに鉄の帯の木の扉）')
    cat['house_light_umo'] = (house_light_umo, static(), '窓の灯り（Umoria の町の窓の高さ）')
    cat['chimney_umo'] = (chimney_umo, static(), 'ドワーフの切石の煙突（roof_vent）')
    for tier in ('low', 'mid', 'high', 'peak'):
        for i in range(1, 4):
            cat['crag_%s_umo_%02d' % (tier, i)] = (crag_umo, static(), '霧ふり山脈の絶壁（%s 段）' % tier)
    for i in range(1, 3):
        cat['alpine_umo_%02d' % i] = (alpine_umo, static(motion={'kind': 'wind'}, wind_k=0.6), '絶壁の天面の草の株')
    cat['durin_gate_umo'] = (durin_gate_umo, static(), 'ドゥリンの扉（下り階段のマス。北の絶壁の門の面と黒い口。地面ごと置き換える）')
    cat['durin_sigil_umo'] = (durin_sigil_umo, static(), 'ドゥリンの扉の線（昼の薄い彫り。アーチの縁・2 本の木・冠と七つの星・金床と槌）')
    cat['durin_sigil_glow_umo'] = (durin_sigil_glow_umo, static(), 'ドゥリンの扉の線（夜に光る写し）')
    for i in range(1, 5):
        cat['holly_umo_%02d' % i] = (holly_umo, static(motion={'kind': 'wind'}, wind_k=0.15), '柊（濃い緑の葉と赤い実。01 低い〜04 大木）')
    for i in range(1, 5):
        cat['elf_ruin_umo_%02d' % i] = (elf_ruin_umo, static(), '柊郷の廃墟（白っぽい石。01 折れた柱 / 02 崩れたアーチ / 03 低い壁 / 04 倒れた柱）')
    for i in range(1, 6):
        cat['ground_turf_umo_%02d' % i] = (ground_dirt_umo, static(), '町の地面（湿った土に草の斑）')
    for i in range(1, 4):
        cat['ground_path_umo_%02d' % i] = (ground_trodden_umo, static(), '道（踏み固めた土と草。通りの端と脇道）')
        cat['ground_flagstone_umo_%02d' % i] = (ground_flagstone_umo, static(), '古い石畳（通りの中央と扉の前だけ。町の節の zones で敷く）')
        cat['ground_grass_umo_%02d' % i] = (ground_grass_umo, static(), '草地の地面')
        cat['palisade_umo_%02d' % i] = (fence_umo, static(), '町の塀（低い石垣。fence と同じ絵）')
    cat['yard_ground_umo'] = (ground_dirt_umo, static(), '前庭の地面')
    for i in range(1, 3):
        cat['grass_umo_%02d' % i] = (grass_tuft_umo, static(motion={'kind': 'wind'}, wind_k=0.6), '草の株')
    for side in ('n', 's', 'w', 'e'):
        cat['fence_%s_umo' % side] = (fence_umo, static(), '前庭の低い石垣（%s 側）' % side)
