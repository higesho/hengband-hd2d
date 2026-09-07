# -*- coding: utf-8 -*-
"""辺境の地の第 2 段 — 村の暮らし。

2026-09-05 に決めた「中世ヨーロッパの辺境の村には何があるか。木・池・畑・家畜・
ぬかるみ・轍・馬を外した馬車・荷車。歩けるなら何を置いてもよい」。

置く側は町の表の `zones`（矩形の区画）と `features`（1 点）で引く。ここで作るのは
- 地面: 泥（`ground_mud_out_*`）・畑の畝（`ground_field_out_*`）・池（`water_pond_out`）
- 区画の柵: 編み垣（`wattle_fence_{n,s,w,e}_out`）
- 小物: 家畜・木・畑の物・水辺・墓地・作業場・広場・門楼・逆茂木
すべて 1 マスに収める（木と門楼と大木だけ、上のほうが隣へはみ出す）。
"""
from __future__ import annotations

import numpy as np

G = None
H = None
C = None
V = 32


def K(name):
    return C[name]


def _rng(name):
    return G['rng_for'](name)


def _box(*a):
    H['box'](*a)


def _disc(*a, **k):
    H['disc'](*a, **k)


def _ball(*a, **k):
    H['ball'](*a, **k)


def _log_x(*a):
    H['log_x'](*a)


def _log_y(*a):
    H['log_y'](*a)


def _cone(*a):
    H['cone'](*a)


def vol(h=32, w=None, d=None):
    return np.zeros((w or V, d or V, h), np.int16)


def one(v, z0=0):
    return [('main', v, (0, 0, z0))]


def _palette():
    rl = G['reg_local']
    rl('out_fur_pk', (214, 160, 148)); rl('out_fur_pkd', (176, 122, 112))   # 豚
    rl('out_fur_w', (232, 228, 214)); rl('out_fur_wd', (196, 190, 176))     # 羊毛・鵞鳥
    rl('out_fur_g', (150, 144, 134))                                      # 山羊
    rl('out_wa_mud', (86, 74, 58)); rl('out_wa_tan', (110, 84, 52))       # 泥水・なめし液
    rl('out_wa_dye', (52, 70, 140))                                       # 染め液
    rl('out_leaf_ap', (196, 60, 52)); rl('out_leaf_pe', (200, 184, 80))    # 林檎・梨（葉の材質だが実）
    rl('out_soil_fw', (88, 66, 46))                                       # 畝の溝
    rl('out_stalk_reed', (138, 150, 84)); rl('out_stalk_reedd', (104, 116, 60))
    rl('out_cloth_bw', (214, 222, 236))                                   # 市の日除けの青白
    #! 湿った泥（2026-09-05 に決めた「湿った泥になり地面の色がもっと濃く黒っぽく」）。
    rl('out_soil_wet', (62, 50, 38)); rl('out_soil_wetl', (78, 64, 48)); rl('out_soil_rut', (40, 32, 24))
    rl('out_wa_k', (30, 28, 28))                                          # 轍に溜まった黒い水


# ------------------------------------------------------------------ 地面
def _rut_curve(rng, base, amp):
    """マスを横切る轍の中心線。**縁では base**（隣のマスと揃う）、中で amp だけ膨らむ。"""
    return [int(round(base + amp * np.sin(np.pi * (i + 0.5) / V))) for i in range(V)]


def _ruts(layer, rng, along_x, colours, base_pair=(10, 21), width=3):
    """2 本の轍を彫る（along_x なら x 方向に走る）。色は (轍, 轍の中の水)。"""
    rut, water = colours
    for base in base_pair:
        amp = float(rng.choice([-3.0, -2.0, 2.0, 3.0]))
        line = _rut_curve(rng, base, amp)
        for i in range(V):
            c = line[i]
            for d in range(-(width // 2), (width // 2) + 1):
                q = c + d
                if 0 <= q < V:
                    if along_x:
                        layer[i, q] = rut
                    else:
                        layer[q, i] = rut
        # 轍に溜まった水（切れ切れに）
        p = int(rng.integers(0, 6))
        while p < V:
            ln = int(rng.integers(3, 8))
            for i in range(p, min(V, p + ln)):
                c = line[i]
                if 0 <= c < V:
                    if along_x:
                        layer[i, c] = water
                    else:
                        layer[c, i] = water
            p += ln + int(rng.integers(4, 12))


def _mud_base(rng, layer, wet=True):
    if wet:
        G['_mottle'](rng, layer, [K('out_soil_wet'), K('out_soil_wetl'), K('dirt_d'), K('soil')], [0.46, 0.24, 0.20, 0.10])
    else:
        G['_mottle'](rng, layer, [K('pack'), K('pack_l'), K('pack_d'), K('dirt')], [0.42, 0.26, 0.22, 0.10])


def _hoof_marks(rng, layer, n, colour):
    for _ in range(n):
        px, py = int(rng.integers(0, V - 2)), int(rng.integers(0, V - 3))
        layer[px:px + 2, py:py + 3] = colour



def _relief(layer, height, under, water=None):
    """**平面の地面**（2 段・origin z=-2）。凹凸は 2026-09-05 に「やりすぎだった。撤回」と決めたので
    やめた。`height` は読まない（呼ぶ側を残すための引数）。"""
    v = vol(2)
    _box(v, 0, V, 0, V, 0, 1, under)
    v[:, :, 1] = layer
    return one(v, -2)


def _height_from(layer, sunk, raised, base=4):
    """色が `sunk` の所を 1 段沈め、`raised` の所を 1 段盛った高さの層。"""
    h = np.full((V, V), base, np.int16)
    for c in sunk:
        h[layer == c] = base - 1
    for c in raised:
        h[layer == c] = base + 1
    return h


def ground_mud_out(name):
    """ぬかるみ（轍の無い所）— 湿った黒っぽい泥に水たまりと足跡。大通りの脇に敷く。"""
    rng = _rng(name)
    v = vol(2)
    _box(v, 0, V, 0, V, 0, 1, K('out_soil_rut'))
    layer = np.zeros((V, V), np.int16)
    #! 中央の列は**黒めの茶色**（2026-09-05 に決めた）。
    G['_mottle'](rng, layer, [K('out_soil_rut'), K('out_soil_wet'), K('out_soil_wetl'), K('dirt_d')], [0.40, 0.34, 0.14, 0.12])
    for _ in range(int(rng.integers(1, 3))):
        x, y = int(rng.integers(0, V - 9)), int(rng.integers(0, V - 7))
        w, d = int(rng.integers(6, 11)), int(rng.integers(4, 8))
        layer[x:x + w, y:y + d] = K('out_soil_rut')
        layer[x + 1:x + w - 1, y + 1:y + d - 1] = K('out_wa_k')
        layer[x + 2:x + 4, y + 2:y + 3] = K('wa_dd')                # 空の照り返し
    _hoof_marks(rng, layer, int(rng.integers(6, 12)), K('out_soil_rut'))
    for _ in range(int(rng.integers(2, 5))):                 # 乾きかけの土の塊（盛る）
        x, y = int(rng.integers(0, V - 3)), int(rng.integers(0, V - 3))
        layer[x:x + 2, y:y + 2] = K('soil')
    height = _height_from(layer, (K('out_soil_rut'), K('out_wa_k'), K('wa_dd')), (K('soil'),))
    return _relief(layer, height, K('out_soil_wet'))


def _churn(layer, rng, rut, water, n_blobs, n_marks, frag=True):
    """交差点の踏み荒らし — 轍は短い切れ端だけ、泥の塊と足跡だらけ。**格子に見せない**
    （広い道の面では全マスが交差点になるので、轍を全長で引くと格子になる）。"""
    if frag:
        for _ in range(int(rng.integers(3, 6))):
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


def _mud_dir(name, kind):
    """向きつきのぬかるみ。`ew`＝東西の轍、`ns`＝南北、`x`＝交差点（踏み荒らされてぐちゃぐちゃ）。"""
    rng = _rng(name)
    v = vol(2)
    _box(v, 0, V, 0, V, 0, 1, K('out_soil_rut'))
    layer = np.zeros((V, V), np.int16)
    _mud_base(rng, layer)
    cols = (K('out_soil_rut'), K('out_wa_k'))
    if kind == 'ew':
        _ruts(layer, rng, True, cols)
        _hoof_marks(rng, layer, int(rng.integers(4, 9)), K('out_soil_rut'))
    elif kind == 'ns':
        _ruts(layer, rng, False, cols)
        _hoof_marks(rng, layer, int(rng.integers(4, 9)), K('out_soil_rut'))
    else:
        _churn(layer, rng, K('out_soil_rut'), K('out_wa_k'), int(rng.integers(3, 5)), int(rng.integers(14, 22)))
    for _ in range(int(rng.integers(1, 4))):                 # 轍の脇に押し出された土の塊（盛る）
        x, y = int(rng.integers(0, V - 3)), int(rng.integers(0, V - 3))
        if layer[x, y] not in (K('out_soil_rut'), K('out_wa_k')):
            layer[x:x + 2, y:y + 2] = K('soil')
    height = _height_from(layer, (K('out_soil_rut'), K('out_wa_k')), (K('soil'),))
    return _relief(layer, height, K('out_soil_wet'))


def _path_dir(name, kind):
    """向きつきの乾いた道（村じゅうの小路）。轍は暗い土、交差点は踏み荒らされる。"""
    rng = _rng(name)
    v = vol(2)
    _box(v, 0, V, 0, V, 0, 1, K('pack_d'))
    layer = np.zeros((V, V), np.int16)
    _mud_base(rng, layer, wet=False)
    cols = (K('dirt_d'), K('out_soil_wet'))
    if kind == 'ew':
        _ruts(layer, rng, True, cols)
        _hoof_marks(rng, layer, int(rng.integers(3, 7)), K('dirt_d'))
    elif kind == 'ns':
        _ruts(layer, rng, False, cols)
        _hoof_marks(rng, layer, int(rng.integers(3, 7)), K('dirt_d'))
    else:
        _churn(layer, rng, K('dirt_d'), K('out_soil_wet'), int(rng.integers(1, 3)), int(rng.integers(8, 14)))
    G['_grain'](rng, layer, layer > 0, [K('grit'), K('pebble')], [0.04, 0.03], block=2)
    v[:, :, 1] = layer
    return one(v, -2)


def _mud_edge(name, side):
    """ぬかるみの縁 — **外側の縁は草地、中央側の縁は泥**で、マスの中で泥の割合が増えていく。
    2026-09-05 に決めた「3 マス幅の上下に緑がきて中央に向けて茶色が来る」。
    `side` は**草地のある側**（`n` なら北の縁が草、南の縁が泥）。割合の曲線は名前ごとに違い、
    2×2 の塊で乱択するので斑になる。塊の並びは横方向にうねる。"""
    rng = _rng(name)
    v = vol(2)
    _box(v, 0, V, 0, V, 0, 1, K('pack_d'))
    mud = np.zeros((V, V), np.int16)
    G['_mottle'](rng, mud, [K('out_soil_wet'), K('out_soil_rut'), K('out_soil_wetl'), K('dirt_d')], [0.40, 0.26, 0.20, 0.14])
    turf = np.zeros((V, V), np.int16)
    G['_mottle'](rng, turf, [K('turf'), K('turf_d'), K('turf_l'), K('pack')], [0.38, 0.28, 0.22, 0.12])
    #! 泥の割合 p(t): t=0（草の縁）で 0、t=1（泥の縁）で 1。名前ごとに曲線の中点（0.35〜0.65）と
    #! 幅（0.25〜0.6）を変え、横方向に正弦でうねらせる。
    mid = float(rng.uniform(0.35, 0.65))
    width = float(rng.uniform(0.25, 0.6))
    amp = float(rng.uniform(0.05, 0.14))
    phase = float(rng.uniform(0.0, 6.28))
    layer = np.zeros((V, V), np.int16)
    for bi in range(0, V, 2):          # 2×2 の塊
        for bj in range(0, V, 2):
            # along = 縁に沿う向き、across = 草→泥の向き
            along, across = (bi, bj) if side in ('n', 's') else (bj, bi)
            tt = (across + 1.0) / V
            if side in ('s', 'e'):
                tt = 1.0 - tt
            m = mid + amp * np.sin((2.0 * np.pi * (along + 1.0) / V) + phase)
            pm = (tt - (m - width / 2.0)) / width
            pm = 0.0 if pm < 0.0 else (1.0 if pm > 1.0 else pm)
            pm = pm * pm * (3.0 - 2.0 * pm)
            src = mud if rng.random() < pm else turf
            layer[bi:bi + 2, bj:bj + 2] = src[bi:bi + 2, bj:bj + 2]
    #! 縁の 2 列は必ず揃える（外側は草、中央側は泥）。隣のマスと繋がって見えるように。
    if side == 'n':
        layer[:, 0:2] = turf[:, 0:2]
        layer[:, V - 2:V] = mud[:, V - 2:V]
    elif side == 's':
        layer[:, V - 2:V] = turf[:, V - 2:V]
        layer[:, 0:2] = mud[:, 0:2]
    elif side == 'w':
        layer[0:2, :] = turf[0:2, :]
        layer[V - 2:V, :] = mud[V - 2:V, :]
    else:
        layer[V - 2:V, :] = turf[V - 2:V, :]
        layer[0:2, :] = mud[0:2, :]
    #! 泥の側に足跡と小さな水たまり。
    for _ in range(int(rng.integers(2, 5))):
        x, y = int(rng.integers(0, V - 2)), int(rng.integers(0, V - 3))
        block = layer[x:x + 2, y:y + 3]
        block[np.isin(block, [K('out_soil_wet'), K('out_soil_wetl'), K('dirt_d')])] = K('out_soil_rut')
    v[:, :, 1] = layer
    return one(v, -2)


def ground_field_out(name):
    """畑の畝 — 南北に走る畝と溝。名前の番号で、素の土／芽／育った葉に分かれる。"""
    rng = _rng(name)
    stage = int(name[-2:]) % 3
    v = vol(3)
    _box(v, 0, V, 0, V, 0, 2, K('out_soil_fw'))
    layer = np.zeros((V, V), np.int16)
    for x in range(V):
        ridge = ((x + 2) % 8) < 5
        layer[x, :] = K('soil') if ridge else 0
    for _ in range(6):                                       # 畝の土のむら
        x, y = int(rng.integers(0, V - 3)), int(rng.integers(0, V - 4))
        block = layer[x:x + 3, y:y + 4]
        block[block == K('soil')] = K('dirt')
    if stage >= 1:
        for x in range(1, V, 8):
            for y in range(int(rng.integers(0, 3)), V, 3):
                layer[x + 1:x + 3, y:y + 2] = K('out_verd_cl') if stage == 2 else K('verd')
    #! 畝（土の色）は 1 段盛り、溝（0 = 下の土が覗く）は沈める。芽は畝の上。
    height = np.full((V, V), 3, np.int16)
    height[layer != 0] = 5 if stage == 0 else 4
    height[np.isin(layer, [K('verd'), K('out_verd_cl')])] = 5
    layer[layer == 0] = K('out_soil_fw')
    return _relief(layer, height, K('out_soil_fw'))


def water_pond_out(name):
    """池 — 天面を地面より 1 段低くした水。睡蓮の葉を浮かべる。歩けるマスに置く。"""
    rng = _rng(name)
    v = vol(2)
    _box(v, 0, V, 0, V, 0, 1, K('wa_d'))
    layer = np.zeros((V, V), np.int16)
    G['_mottle'](rng, layer, [K('wa'), K('wa_l'), K('wa_d')], [0.55, 0.15, 0.30])
    for _ in range(int(rng.integers(1, 4))):                 # 睡蓮の葉
        x, y = int(rng.integers(2, V - 5)), int(rng.integers(2, V - 5))
        layer[x:x + 4, y:y + 3] = K('leaf_l')
        layer[x + 1, y + 1] = K('leaf_d')
    v[:, :, 1] = layer
    return one(v, -3)


def wattle_fence_out(name):
    """編み垣 — 杭に細い枝を編んだ低い垣。高さは敷地の柵と同じ 14。"""
    side = name.rsplit('_', 1)[-1]
    rng = _rng(name)
    th = 3
    v = vol(14)
    lo = 0 if side in ('n', 'w') else V - th
    hi = lo + th

    def put(a0, a1, z0, z1, colour):
        if side in ('n', 's'):
            _box(v, a0, a1, lo, hi, z0, z1, colour)
        else:
            _box(v, lo, hi, a0, a1, z0, z1, colour)

    for z in range(2, 13):                                   # 編んだ枝（段ごとに明暗を替え、半分ずらす）
        off = (z % 2) * 3
        p = -off
        while p < V:
            w = 5
            shade = K('bark_l') if ((z + p) // 5) % 2 else K('bark')
            put(max(0, p), min(V, p + w), z, z + 1, shade)
            p += w + 1
    for p in range(2, V, 8):                                 # 杭
        put(p, p + 2, 0, 14, K('timber_d'))
    return one(v)


# ------------------------------------------------------------------ 泥と道具
def puddle_plank_out(name):
    """水たまりに渡した板 — 暗い水の膜と、その上の板 2 枚。"""
    v = vol(4)
    _box(v, 3, 29, 8, 24, 0, 1, K('out_wa_mud'))
    _box(v, 5, 27, 10, 22, 0, 1, K('wa_d'))
    _box(v, 2, 30, 12, 16, 1, 3, K('board'))
    _box(v, 4, 28, 17, 21, 1, 3, K('board_d'))
    return one(v)


def stepping_stones_out(name):
    """飛び石 — 泥の上に平らな石を 4 つ。"""
    v = vol(4)
    _box(v, 2, 30, 6, 26, 0, 1, K('out_wa_mud'))
    for (x, y, w) in ((5, 12, 6), (13, 10, 7), (21, 13, 6), (26, 18, 5)):
        _box(v, x, x + w, y, y + 5, 0, 3, K('rock_l'))
        _box(v, x + 1, x + w - 1, y + 1, y + 4, 2, 3, K('rock'))
    return one(v)


def wagon_empty_out(name):
    """馬を外した荷馬車 — 四輪、幌無し、梶棒を地面に下ろし、荷台に藁くずだけ。"""
    v = vol(22)
    _box(v, 5, 27, 8, 24, 8, 11, K('oak'))
    _box(v, 5, 27, 8, 24, 10, 11, K('oak_l'))
    _box(v, 5, 27, 8, 9, 11, 15, K('oak'))
    _box(v, 5, 27, 23, 24, 11, 15, K('oak'))
    _box(v, 5, 6, 8, 24, 11, 15, K('oak'))
    _box(v, 8, 24, 11, 21, 11, 12, K('hay_d'))
    for cx, r in ((9, 5.0), (23, 6.0)):
        for cy in (6, 25):
            _log_y(v, cx, r + 1, r, cy, cy + 2, K('timber'))
            _log_y(v, cx, r + 1, r - 1.6, cy, cy + 2, 0)
            _box(v, cx - 1, cx + 1, cy, cy + 2, 1, int(r * 2), K('timber_d'))
    _log_x(v, 16, 6, 1.0, 7, 25, K('iron_l'))
    for y in (11, 19):                                       # 梶棒（下ろしてある）
        for k in range(8):
            _box(v, 26 + (k // 2), 27 + (k // 2), y, y + 2, 8 - k, 10 - k, K('timber_l'))
    return one(v)


def broken_cart_out(name):
    """壊れた荷車 — 片輪が外れて傾き、外れた車輪が脇に転がっている。"""
    v = vol(18)
    for k in range(20):                                      # 傾いた荷台
        z = 3 + (k // 4)
        _box(v, 6 + k, 7 + k, 10, 22, z, z + 3, K('oak'))
    _box(v, 6, 26, 10, 11, 4, 9, K('oak_l'))
    _log_y(v, 22, 8, 5.5, 22, 24, K('timber'))               # 残った車輪
    _log_y(v, 22, 8, 4.0, 22, 24, 0)
    _box(v, 21, 23, 22, 24, 2, 14, K('timber_d'))
    _disc(v, 9, 27, 5.5, 0, 2, K('timber'), hollow=4.0)      # 外れた車輪（倒れている）
    _box(v, 8, 10, 22, 32, 0, 2, K('timber_d'))
    _box(v, 4, 14, 26, 28, 0, 2, K('timber_d'))
    _box(v, 25, 31, 14, 16, 1, 3, K('timber_l'))             # 梶棒
    _box(v, 25, 31, 18, 20, 1, 3, K('timber_l'))
    return one(v)


def wheelbarrow_out(name):
    """手押し車 — 一輪、二本の柄、積んだ堆肥。"""
    v = vol(16)
    _box(v, 8, 22, 10, 22, 6, 9, K('board'))
    _box(v, 8, 22, 10, 11, 9, 13, K('board_d'))
    _box(v, 8, 22, 21, 22, 9, 13, K('board_d'))
    _box(v, 8, 9, 10, 22, 9, 13, K('board_d'))
    _ball(v, 15, 16, 12, 5.5, K('soil'), squash=0.7)
    _log_y(v, 5, 4, 4.0, 15, 17, K('timber'))
    _log_y(v, 5, 4, 2.6, 15, 17, 0)
    _box(v, 22, 30, 12, 14, 5, 7, K('timber_l'))
    _box(v, 22, 30, 18, 20, 5, 7, K('timber_l'))
    _box(v, 20, 22, 11, 13, 0, 6, K('timber_d'))
    _box(v, 20, 22, 19, 21, 0, 6, K('timber_d'))
    return one(v)


def ladder_out(name):
    """立てかけた梯子 — 少し傾けて、上を杭で受ける。"""
    v = vol(40)
    for k in range(34):
        x = 8 + (k // 5)
        for lx in (0, 9):
            _box(v, x + lx, x + lx + 2, 14, 16, k, k + 2, K('timber'))
        if k % 5 == 0:
            _box(v, x, x + 11, 14, 16, k, k + 1, K('timber_l'))
    _box(v, 20, 23, 12, 18, 0, 36, K('log'))
    return one(v)


def basket_pile_out(name):
    """籠の山 — 編んだ籠を 3 つ、1 つは倒れている。"""
    v = vol(14)
    for (cx, cy, r, h) in ((10, 12, 5.0, 8), (21, 14, 4.5, 7)):
        _disc(v, cx, cy, r, 0, h, K('hay_d'))
        _disc(v, cx, cy, r - 1.2, 1, h, K('straw_d'))
        for z in range(0, h, 2):
            _disc(v, cx, cy, r, z, z + 1, K('hay'), hollow=r - 1.0)
        _box(v, cx - 1, cx + 1, int(cy - r), int(cy + r), h, h + 2, K('hay_d'))   # 取っ手
    _log_y(v, 14, 3, 3.5, 20, 28, K('hay'))
    _log_y(v, 14, 3, 2.3, 21, 28, 0)
    return one(v)


# ------------------------------------------------------------------ 畑と果樹
def scarecrow_out(name):
    """案山子 — 十字の棒にぼろ布と麦わら帽子。"""
    v = vol(40)
    _box(v, 15, 17, 15, 17, 0, 34, K('timber'))
    _box(v, 6, 26, 15, 17, 26, 28, K('timber'))
    _box(v, 11, 21, 14, 18, 16, 29, K('rag'))
    _box(v, 6, 12, 14, 18, 25, 28, K('rag'))
    _box(v, 20, 26, 14, 18, 25, 28, K('rag'))
    _ball(v, 16, 16, 32, 3.5, K('hay'))
    _disc(v, 16, 16, 6.0, 34, 36, K('straw'))
    _disc(v, 16, 16, 3.5, 36, 39, K('straw_d'))
    _box(v, 14, 15, 18, 19, 32, 33, K('dark'))
    _box(v, 17, 18, 18, 19, 32, 33, K('dark'))
    return one(v)


def bean_poles_out(name):
    """豆の支柱 — 三脚に組んだ棒に蔓と豆の莢。"""
    rng = _rng(name)
    v = vol(34)
    for (cx, cy) in ((10, 12), (22, 18)):
        for (dx, dy) in ((-4, -3), (4, -3), (0, 4)):
            for k in range(28):
                t = 1.0 - (k / 28.0)
                _box(v, int(cx + dx * t), int(cx + dx * t) + 1, int(cy + dy * t), int(cy + dy * t) + 1, k, k + 1, K('timber_d'))
        for k in range(3, 26, 3):
            _box(v, cx - 3, cx + 3, cy - 2, cy + 3, k, k + 2, K('leaf_m') if rng.random() < 0.7 else K('leaf_l'))
            if rng.random() < 0.5:
                _box(v, cx + int(rng.integers(-3, 3)), cx + int(rng.integers(-3, 3)) + 1, cy + 3, cy + 4, k, k + 3, K('verd'))
    return one(v)


def cabbage_row_out(name):
    """キャベツの列 — 畝に並んだ丸い玉。"""
    v = vol(8)
    _box(v, 2, 30, 6, 26, 0, 1, K('soil'))
    for x in range(5, 30, 7):
        for y in (10, 22):
            _ball(v, x, y, 3, 3.5, K('out_verd_c'), squash=0.7)
            _ball(v, x, y, 3, 2.0, K('out_verd_cl'), squash=0.9)
    return one(v)


def compost_heap_out(name):
    """堆肥の山 — 暗い土と藁の山、突き立てた鋤。"""
    rng = _rng(name)
    v = vol(16)
    _ball(v, 15, 16, 2, 11.0, K('dirt_d'), squash=0.6)
    for _ in range(14):
        x, y = int(rng.integers(6, 26)), int(rng.integers(6, 26))
        col = v[x, y, :]
        nz = col.nonzero()[0]
        if len(nz):
            col[nz.max()] = K('straw_d') if rng.random() < 0.6 else K('soil')
    _box(v, 22, 24, 10, 12, 0, 15, K('timber_d'))
    _box(v, 20, 26, 9, 13, 3, 5, K('iron'))
    return one(v)


def beehive_out(name):
    """蜂の巣（スケップ）— 藁を巻いた鐘形の巣を木の台に載せる。"""
    v = vol(22)
    _box(v, 9, 23, 9, 23, 0, 4, K('timber_d'))
    _box(v, 10, 22, 10, 22, 3, 4, K('board'))
    for z in range(4, 18):
        r = 6.5 - ((z - 4) / 14.0) ** 2 * 5.5
        _disc(v, 16, 16, r, z, z + 1, K('straw') if (z % 2) else K('straw_d'))
    _box(v, 15, 17, 21, 23, 6, 8, K('dark'))
    return one(v)


def _tree_round(name, trunk_c, canopy_r, height, leaf, fruit=None, span=64):
    """果樹 — 幹をマスの角へ寄せ、樹冠が隣へはみ出す。"""
    rng = _rng(name)
    v = np.zeros((span, span, height), np.int16)
    cx, cy = trunk_c
    H['disc'](v, cx, cy, 2.5, 0, int(height * 0.45), K('bark'))
    H['disc'](v, cx, cy, 1.5, 0, int(height * 0.45), K('bark_l'))
    cz = int(height * 0.62)
    shades = [K('leaf_d'), K('leaf_m'), K('leaf_l')]
    for _ in range(12):
        bx = cx + rng.normal(0, canopy_r * 0.45)
        by = cy + rng.normal(0, canopy_r * 0.45)
        bz = cz + rng.normal(0, height * 0.08)
        H['ball'](v, bx, by, bz, float(rng.uniform(canopy_r * 0.45, canopy_r * 0.7)), shades[int(rng.integers(0, 3))], squash=0.8)
    if fruit is not None:
        for _ in range(14):
            x, y = int(cx + rng.integers(-canopy_r, canopy_r)), int(cy + rng.integers(-canopy_r, canopy_r))
            if 0 <= x < span and 0 <= y < span:
                col = v[x, y, :]
                nz = col.nonzero()[0]
                if len(nz) and nz.max() > cz - 6:
                    col[nz.max()] = fruit
    v[:, :, 0] = 0
    H['disc'](v, cx, cy, 2.5, 0, 2, K('bark'))
    return [('main', v, (0, 0, 0))]


def apple_tree_out(name):
    return _tree_round(name, (24, 24), 14, 44, K('leaf_m'), fruit=K('out_leaf_ap'))


def pear_tree_out(name):
    return _tree_round(name, (24, 24), 12, 50, K('leaf_l'), fruit=K('out_leaf_pe'))


def willow_out(name):
    """柳 — 傾いた幹から、垂れ下がる細い枝葉。"""
    rng = _rng(name)
    span, height = 64, 56
    v = np.zeros((span, span, height), np.int16)
    for k in range(30):
        x = 24 + int(k * 0.3)
        H['disc'](v, x, 24, 2.5 - (k * 0.03), k, k + 1, K('bark'))
    H['ball'](v, 33, 24, 36, 9.0, K('leaf_d'), squash=0.6)
    for _ in range(40):
        a = rng.random() * 6.28
        r = rng.uniform(6, 15)
        x, y = int(33 + r * np.cos(a)), int(24 + r * np.sin(a))
        top = int(rng.integers(30, 40))
        bottom = int(rng.integers(6, 18))
        if 0 <= x < span and 0 <= y < span:
            v[x, y, bottom:top] = K('leaf_l') if rng.random() < 0.5 else K('leaf_m')
    return [('main', v, (0, 0, 0))]


def village_tree_out(name):
    """村の大木 — 3×3 マスに枝を張る樫。幹は太く、根元に石の輪。"""
    rng = _rng(name)
    span, height = 96, 110
    v = np.zeros((span, span, height), np.int16)
    cx = cy = 48
    for k in range(50):
        r = 7.0 - (k * 0.06)
        H['disc'](v, cx, cy, r, k, k + 1, K('bark'))
    H['disc'](v, cx, cy, 10.0, 0, 3, K('rub'), hollow=7.5)
    shades = [K('leaf_d'), K('leaf_m'), K('leaf_l'), K('leaf_h')]
    tips = []
    for k in range(6):
        a = k * 1.047 + rng.uniform(-0.3, 0.3)
        ln = rng.uniform(16, 26)
        ex, ey, ez = cx + ln * np.cos(a), cy + ln * np.sin(a), 44 + rng.uniform(8, 20)
        for t in range(24):
            f = t / 24.0
            H['disc'](v, cx + (ex - cx) * f, cy + (ey - cy) * f, 3.0 - (2.0 * f), int(40 + (ez - 40) * f), int(40 + (ez - 40) * f) + 2, K('bark_l'))
        tips.append((ex, ey, ez))
    tips.append((cx, cy, 70))
    for (tx, ty, tz) in tips:
        for _ in range(9):
            bx, by, bz = tx + rng.normal(0, 8), ty + rng.normal(0, 8), tz + rng.normal(6, 5)
            H['ball'](v, bx, by, bz, float(rng.uniform(7, 12)), shades[min(3, max(0, int(rng.normal(1.5, 1.0))))], squash=0.75)
    v[:, :, 0] = 0
    H['disc'](v, cx, cy, 7.0, 0, 3, K('bark'))
    H['disc'](v, cx, cy, 10.0, 0, 3, K('rub'), hollow=7.5)
    return [('main', v, (0, 0, 0))]


# ------------------------------------------------------------------ 水辺
def reeds_out(name):
    """葦の茂み — 細い茎を密に立てる。"""
    rng = _rng(name)
    v = vol(30)
    for _ in range(int(rng.integers(18, 28))):
        x, y = int(rng.integers(6, 26)), int(rng.integers(6, 26))
        h = int(rng.integers(14, 28))
        lean = int(rng.integers(-2, 3))
        for z in range(h):
            xx = min(V - 1, max(0, x + int(round(lean * z / h))))
            v[xx, y, z] = K('out_stalk_reed') if rng.random() < 0.6 else K('out_stalk_reedd')
    return one(v)


def cattail_out(name):
    """蒲 — 葦より少なく、先に茶色の穂。"""
    rng = _rng(name)
    v = vol(34)
    for _ in range(int(rng.integers(5, 9))):
        x, y = int(rng.integers(8, 24)), int(rng.integers(8, 24))
        h = int(rng.integers(18, 30))
        v[x, y, 0:h] = K('out_stalk_reedd')
        _box(v, x - 1, x + 2, y - 1, y + 2, h - 6, h, K('fur'))
        v[x, y, h:h + 3] = K('out_stalk_reed')
    return one(v)


def duck_out(name):
    """鴨 — 茶の体に緑の頭。水面の高さに浮かべる。"""
    v = vol(12)
    _ball(v, 14, 16, 2, 4.0, K('fur'), squash=0.6)
    _box(v, 10, 12, 15, 17, 2, 5, K('fur_d'))                # 尾
    _ball(v, 19, 16, 5, 2.5, K('verd'))
    _box(v, 21, 24, 15, 17, 4, 6, K('gold'))                 # 嘴
    return one(v)


def washing_stones_out(name):
    """洗い場 — 水際の平石に洗濯板と桶、石に広げた布。"""
    v = vol(14)
    _box(v, 2, 30, 4, 28, 0, 3, K('rock_l'))
    _box(v, 4, 28, 6, 26, 2, 3, K('rock'))
    _box(v, 6, 14, 8, 20, 3, 5, K('out_cloth_w'))
    for k in range(6):                                       # 洗濯板（斜め）
        _box(v, 18 + k, 20 + k, 8, 18, 3 + k, 5 + k, K('board'))
    _disc(v, 24, 22, 3.5, 3, 9, K('oak_l'))
    _disc(v, 24, 22, 2.5, 7, 9, K('wa'))
    return one(v)


# ------------------------------------------------------------------ 家畜
def pig_out(name):
    """豚 — 桃色の丸い体、鼻、耳、4 本の脚。"""
    v = vol(16)
    for (x0, y0) in ((10, 12), (10, 18), (19, 12), (19, 18)):
        _box(v, x0, x0 + 2, y0, y0 + 2, 0, 5, K('out_fur_pkd'))
    _ball(v, 15, 16, 9, 6.0, K('out_fur_pk'), squash=0.8)
    _ball(v, 23, 16, 9, 3.8, K('out_fur_pk'))
    _box(v, 26, 28, 15, 18, 8, 10, K('out_fur_pkd'))          # 鼻
    _box(v, 22, 24, 12, 14, 12, 14, K('out_fur_pkd'))         # 耳
    _box(v, 22, 24, 18, 20, 12, 14, K('out_fur_pkd'))
    _box(v, 8, 9, 15, 16, 10, 13, K('out_fur_pkd'))           # 尾
    return one(v)


def pig_mud_out(name):
    """泥に寝そべる豚 — 暗い泥だまりの中に体だけ。"""
    v = vol(12)
    _ball(v, 15, 16, 1, 11.0, K('out_wa_mud'), squash=0.15)
    _ball(v, 15, 15, 5, 6.5, K('out_fur_pk'), squash=0.6)
    _ball(v, 22, 14, 5, 3.5, K('out_fur_pk'))
    _box(v, 25, 27, 13, 16, 4, 6, K('out_fur_pkd'))
    v[:, :, 0][v[:, :, 0] == 0] = 0
    return one(v)


def sheep_out(name):
    """羊 — 白い羊毛の塊に黒い顔と脚。"""
    v = vol(16)
    for (x0, y0) in ((10, 12), (10, 18), (19, 12), (19, 18)):
        _box(v, x0, x0 + 2, y0, y0 + 2, 0, 5, K('dark'))
    _ball(v, 15, 16, 9, 6.5, K('out_fur_w'), squash=0.85)
    _ball(v, 12, 16, 10, 4.0, K('out_fur_wd'), squash=0.9)
    _box(v, 21, 25, 14, 18, 8, 12, K('dark'))                # 顔
    _box(v, 22, 24, 12, 14, 11, 13, K('dark'))               # 耳
    _box(v, 22, 24, 18, 20, 11, 13, K('dark'))
    return one(v)


def goat_tethered_out(name):
    """つながれた山羊 — 灰色の体に角と髭、杭に縄。"""
    v = vol(20)
    for (x0, y0) in ((9, 13), (9, 18), (17, 13), (17, 18)):
        _box(v, x0, x0 + 2, y0, y0 + 2, 0, 7, K('out_fur_g'))
    _box(v, 8, 20, 12, 20, 7, 13, K('out_fur_g'))
    _box(v, 19, 24, 14, 18, 10, 16, K('out_fur_g'))          # 首
    _box(v, 22, 27, 14, 18, 14, 18, K('out_fur_g'))          # 頭
    _box(v, 26, 27, 15, 17, 12, 14, K('out_fur_wd'))         # 髭
    for y in (13, 18):                                       # 角
        for k in range(4):
            _box(v, 23 - k, 24 - k, y, y + 1, 18 + k, 19 + k, K('bone_d'))
    _box(v, 28, 30, 25, 27, 0, 12, K('timber_d'))            # 杭
    for k in range(8):
        _box(v, 24 + (k // 2), 25 + (k // 2), 18 + k, 19 + k, 12, 13, K('lash'))
    return one(v)


def chicken_out(name):
    """鶏 — 2 羽。茶の体、赤い鶏冠、黄色い嘴。"""
    v = vol(10)
    for (cx, cy, c) in ((10, 12, K('fur')), (21, 20, K('fur_l'))):
        _box(v, cx, cx + 1, cy, cy + 1, 0, 3, K('gold'))
        _ball(v, cx, cy, 5, 2.8, c, squash=0.8)
        _ball(v, cx + 3, cy, 7, 1.6, c)
        _box(v, cx + 3, cx + 4, cy, cy + 1, 8, 10, K('fl_r'))
        _box(v, cx + 5, cx + 6, cy, cy + 1, 6, 7, K('gold'))
        _box(v, cx - 3, cx - 2, cy, cy + 1, 6, 8, K('fur_d'))
    return one(v)


def goose_out(name):
    """鵞鳥 — 白い体に長い首、橙の嘴。"""
    v = vol(16)
    _box(v, 14, 15, 15, 16, 0, 4, K('gold'))
    _box(v, 17, 18, 15, 16, 0, 4, K('gold'))
    _ball(v, 15, 16, 6, 4.0, K('out_fur_w'), squash=0.8)
    _box(v, 19, 21, 15, 17, 7, 14, K('out_fur_w'))           # 首
    _ball(v, 20, 16, 14, 1.8, K('out_fur_w'))
    _box(v, 22, 25, 15, 17, 13, 15, K('gold'))               # 嘴
    _box(v, 10, 12, 15, 17, 7, 9, K('out_fur_wd'))            # 尾
    return one(v)


def dog_out(name):
    """寝そべる犬 — 茶の体を横たえ、頭を前足に載せる。"""
    v = vol(10)
    _log_x(v, 16, 3, 3.2, 8, 22, K('fur_d'))
    _box(v, 8, 22, 13, 19, 0, 1, K('fur_d'))
    _ball(v, 24, 16, 4, 3.0, K('fur_d'))
    _box(v, 26, 29, 15, 18, 2, 4, K('fur'))                  # 鼻づら
    _box(v, 22, 24, 13, 14, 5, 7, K('fur_d'))                # 耳
    _box(v, 22, 24, 18, 19, 5, 7, K('fur_d'))
    _box(v, 4, 8, 15, 17, 1, 3, K('fur_d'))                  # 尾
    return one(v)


def hen_house_out(name):
    """鶏小屋 — 脚つきの板の小屋に渡り板、屋根に鶏。"""
    v = vol(30)
    for (x0, y0) in ((6, 8), (6, 22), (24, 8), (24, 22)):
        _box(v, x0, x0 + 2, y0, y0 + 2, 0, 8, K('timber_d'))
    _box(v, 5, 27, 7, 25, 8, 10, K('board_d'))
    _box(v, 6, 26, 8, 24, 10, 22, K('board'))
    _box(v, 13, 19, 23, 25, 11, 18, K('dark'))               # 出入り口
    for k in range(10):                                      # 渡り板
        _box(v, 14, 18, 25 + k, 26 + k, 10 - k, 12 - k, K('board_l'))
    for z in range(22, 29):
        d = (z - 22) * 2
        _box(v, 4 + d, 28 - d, 6, 26, z, z + 1, K('shin_d') if (z % 2) else K('shin'))
    _ball(v, 10, 12, 24, 2.4, K('fur_l'), squash=0.8)
    _box(v, 12, 13, 12, 13, 26, 28, K('fl_r'))
    return one(v)


# ------------------------------------------------------------------ 墓地
def grave_out(name):
    """墓 — 土の盛りに木の十字架。変種で新しい土・傾いた十字架・石を分ける。"""
    k = int(name[-2:]) % 3
    v = vol(24)
    _ball(v, 16, 18, 0, 8.0, K('soil') if k == 1 else K('dirt_d'), squash=0.35)
    v[:, :, 0][v[:, :, 0] == 0] = 0
    if k == 2:
        _box(v, 12, 20, 6, 10, 0, 10, K('rock_l'))
        _box(v, 13, 19, 6, 8, 9, 10, K('rock'))
    else:
        lean = 1 if k == 0 else 0
        for z in range(18):
            _box(v, 15 + (z // 9) * lean, 17 + (z // 9) * lean, 7, 9, z, z + 1, K('timber_d'))
        _box(v, 11, 22, 7, 9, 12, 14, K('timber_d'))
    return one(v)


def bone_pile_out(name):
    """骨の山 — 獣の骨と頭骨。"""
    rng = _rng(name)
    v = vol(8)
    for _ in range(8):
        x, y = int(rng.integers(6, 22)), int(rng.integers(8, 24))
        _log_x(v, y, 1, 1.0, x, x + int(rng.integers(5, 10)), K('bone') if rng.random() < 0.6 else K('bone_d'))
    _ball(v, 18, 12, 3, 3.5, K('bone'), squash=0.8)
    _box(v, 20, 23, 11, 14, 1, 3, K('dark'))
    return one(v)


def wayside_cross_out(name):
    """道端の十字架 — 石の台に木の十字架、小さな屋根。"""
    v = vol(44)
    _box(v, 10, 22, 12, 22, 0, 5, K('rub'))
    _box(v, 11, 21, 13, 21, 4, 5, K('rub_l'))
    _box(v, 15, 17, 16, 18, 5, 38, K('timber_d'))
    _box(v, 8, 24, 16, 18, 28, 31, K('timber_d'))
    _box(v, 5, 27, 13, 21, 38, 40, K('shin_d'))
    _box(v, 7, 25, 14, 20, 40, 42, K('shin'))
    _box(v, 11, 21, 15, 19, 42, 44, K('shin_l'))
    _box(v, 14, 18, 18, 19, 20, 24, K('fl_y'))               # 供えた花
    return one(v)


def crow_post_out(name):
    """烏の止まる杭 — 古い杭に烏が 2 羽。"""
    v = vol(40)
    _box(v, 14, 18, 14, 18, 0, 30, K('timber_d'))
    _box(v, 6, 26, 15, 17, 28, 30, K('timber_d'))
    for cx in (8, 23):
        _ball(v, cx, 16, 33, 2.4, K('dark'), squash=0.8)
        _ball(v, cx + 2, 16, 35, 1.4, K('dark'))
        _box(v, cx + 3, cx + 5, 16, 17, 35, 36, K('gold_d'))
    return one(v)


def skull_stake_out(name):
    """頭骨の杭 — 塀の外に立てた杭に怪物の頭骨。"""
    v = vol(44)
    _box(v, 15, 17, 15, 17, 0, 32, K('timber_d'))
    _ball(v, 16, 16, 35, 5.0, K('bone_d'), squash=0.9)
    _box(v, 13, 19, 19, 22, 31, 35, K('bone'))               # 顎
    _box(v, 13, 15, 20, 22, 36, 38, K('dark'))
    _box(v, 17, 19, 20, 22, 36, 38, K('dark'))
    for (x, dx) in ((11, -1), (20, 1)):                      # 角
        for k in range(5):
            _box(v, x + dx * k, x + dx * k + 2, 15, 17, 38 + k, 40 + k, K('bone_d'))
    return one(v)


def stakes_out(name):
    """逆茂木 — 交差させた尖り杭の列。"""
    rng = _rng(name)
    v = vol(24)
    _log_x(v, 16, 4, 2.0, 2, 30, K('log_d'))
    for x in range(4, 30, 6):
        for (dy, dz) in ((-1, 1), (1, 1)):
            for k in range(20):
                y = 16 + int(dy * k * 0.6)
                z = 2 + int(dz * k * 0.95)
                _box(v, x, x + 2, y - 1, y + 1, z, z + 2, K('log') if k < 16 else K('log_end'))
    return one(v)


# ------------------------------------------------------------------ 作業場
def lumber_stack_out(name):
    """板の山 — 桟木を挟んで積んだ板。"""
    v = vol(16)
    for z in range(0, 15, 3):
        _box(v, 3, 29, 8, 24, z, z + 2, K('board') if (z // 3) % 2 else K('board_l'))
        _box(v, 6, 8, 8, 24, z + 2, z + 3, K('timber_d'))
        _box(v, 24, 26, 8, 24, z + 2, z + 3, K('timber_d'))
    return one(v)


def charcoal_clamp_out(name):
    """炭焼きの山 — 土をかぶせた丸い山、頂に煙の穴、根元に丸太。"""
    v = vol(20)
    for z in range(16):
        r = 12.0 * (1.0 - (z / 17.0) ** 1.5)
        _disc(v, 16, 16, r, z, z + 1, K('soot') if (z % 4 == 0) else K('dirt_d'))
    _disc(v, 16, 16, 1.5, 12, 17, K('dark'))
    _log_x(v, 28, 2, 2.0, 4, 20, K('oak'))
    _log_x(v, 30, 2, 1.8, 8, 24, K('oak_l'))
    return one(v)


def tannery_vat_out(name):
    """革なめしの桶 — 地面に埋めた木の桶に茶色い液、縁に掛けた皮。"""
    v = vol(14)
    _box(v, 4, 28, 6, 26, 0, 8, K('timber_d'))
    _box(v, 6, 26, 8, 24, 1, 8, K('out_wa_tan'))
    _box(v, 6, 26, 8, 24, 7, 8, K('out_wa_tan'))
    _box(v, 22, 30, 4, 27, 7, 10, K('fur'))                  # 掛けた皮
    _box(v, 24, 28, 2, 4, 3, 10, K('fur_d'))
    return one(v)


def dye_vat_out(name):
    """染めの桶 — 藍の液の桶と、横木に干した藍の布。"""
    v = vol(34)
    for z in range(12):
        _disc(v, 12, 16, 7.0, z, z + 1, K('oak_l') if (z % 4) else K('oak'))
    _disc(v, 12, 16, 6.0, 10, 12, K('out_wa_dye'))
    for x in (22, 29):
        _box(v, x, x + 2, 15, 17, 0, 30, K('timber'))
    _box(v, 21, 31, 15, 17, 29, 31, K('timber_d'))
    _box(v, 23, 29, 15, 16, 12, 29, K('out_cloth_b'))
    _box(v, 23, 29, 15, 16, 12, 16, K('out_cloth_bd'))
    return one(v)


def sawpit_out(name):
    """鋸引きの穴 — 暗い穴に渡した丸太と、突き立てた大鋸。"""
    v = vol(30)
    _box(v, 6, 26, 6, 26, 0, 1, K('dark'))
    _box(v, 4, 28, 4, 6, 0, 3, K('timber_d'))
    _box(v, 4, 28, 26, 28, 0, 3, K('timber_d'))
    _log_x(v, 16, 6, 3.5, 2, 30, K('log'))
    _box(v, 15, 17, 15, 17, 6, 28, K('steel'))               # 鋸（縦）
    _box(v, 13, 19, 14, 18, 26, 30, K('timber_d'))           # 柄
    for z in range(8, 26, 3):
        _box(v, 17, 18, 15, 17, z, z + 1, K('steel_d'))
    return one(v)


def smokehouse_out(name):
    """燻製小屋 — 小さな板の小屋、煙の穴、軒下に吊るした魚。"""
    v = vol(36)
    _box(v, 6, 26, 8, 26, 0, 20, K('board_d'))
    for x in range(7, 26, 3):
        _box(v, x, x + 1, 25, 26, 0, 20, K('board'))
    _box(v, 12, 20, 25, 26, 2, 16, K('dark'))
    for z in range(20, 30):
        d = (z - 20) * 2
        _box(v, 4 + d, 28 - d, 6, 28, z, z + 1, K('shin_d') if (z % 2) else K('shin'))
    _box(v, 15, 17, 15, 17, 28, 34, K('soot'))
    for x in (8, 12, 22):                                    # 吊るした魚
        _box(v, x, x + 1, 27, 28, 12, 18, K('lash'))
        _box(v, x - 1, x + 2, 27, 28, 6, 12, K('steel_d'))
    return one(v)


def bread_oven_out(name):
    """共同のパン窯 — 石を積んだ丸い窯、暗い口、上に石の煙突。脇に薪と天板。"""
    rng = _rng(name)
    v = vol(30)
    _box(v, 4, 28, 6, 28, 0, 6, K('rub_d'))
    _box(v, 5, 27, 7, 27, 5, 6, K('rub_l'))
    for z in range(6, 22):
        r = 10.5 * (1.0 - ((z - 6) / 18.0) ** 2) ** 0.5 + 1.0
        _disc(v, 15, 15, r, z, z + 1, K('clay') if ((z + int(rng.integers(0, 2))) % 3) else K('clay_d'))
    _box(v, 12, 18, 22, 27, 6, 12, K('dark'))                # 口
    _box(v, 11, 19, 25, 27, 12, 14, K('rub_l'))              # 口の楣
    _box(v, 13, 17, 13, 17, 20, 28, K('rub'))                # 煙突
    _box(v, 14, 16, 14, 16, 26, 29, K('soot'))
    _log_x(v, 4, 2, 2.0, 6, 24, K('oak'))
    _box(v, 22, 28, 22, 30, 6, 7, K('board'))                # 天板（パン）
    _ball(v, 25, 26, 7, 2.0, K('cream_d'))
    return one(v)


# ------------------------------------------------------------------ 広場
def stone_bench_out(name):
    """石の腰掛け — 2 つの石の脚に板石。"""
    v = vol(10)
    _box(v, 5, 9, 12, 20, 0, 6, K('rub'))
    _box(v, 23, 27, 12, 20, 0, 6, K('rub'))
    _box(v, 3, 29, 11, 21, 6, 9, K('rock_l'))
    return one(v)


def pillory_out(name):
    """さらし台 — 柱に載せた穴あきの板、足元に腐ったキャベツ。"""
    v = vol(36)
    _box(v, 15, 18, 14, 18, 0, 26, K('timber'))
    _box(v, 5, 27, 14, 18, 22, 32, K('board'))
    _box(v, 5, 27, 14, 18, 26, 27, K('timber_d'))            # 上下の板の合わせ目
    for x in (9, 15, 21):
        _box(v, x, x + 3, 14, 18, 25, 28, K('dark'))
    _box(v, 24, 27, 15, 17, 22, 32, K('iron'))               # 蝶番とロック
    _ball(v, 8, 24, 2, 2.5, K('wilt'))
    return one(v)


def notice_board_out(name):
    """掲示板 — 2 本の柱に板、紙が何枚も留めてある。"""
    v = vol(36)
    for x in (4, 26):
        _box(v, x, x + 2, 15, 17, 0, 32, K('timber'))
    _box(v, 4, 28, 15, 17, 12, 30, K('board_d'))
    _box(v, 3, 29, 14, 18, 30, 33, K('shin_d'))
    for (x, z, w, h) in ((6, 22, 6, 6), (13, 19, 7, 9), (21, 23, 5, 5), (8, 14, 5, 6), (15, 13, 4, 4)):
        _box(v, x, x + w, 17, 18, z, z + h, K('cream'))
        _box(v, x + 1, x + w - 1, 17, 18, z + 1, z + 2, K('cream_d'))
    return one(v)


def market_stall_out(name):
    """市の屋台 — 板の台に袋と壺と果物、青白の日除け。"""
    v = vol(34)
    _box(v, 3, 29, 8, 24, 10, 13, K('board'))
    for x in (4, 26):
        _box(v, x, x + 2, 9, 23, 0, 10, K('timber'))
        _box(v, x, x + 2, 22, 24, 13, 30, K('timber_d'))
    _ball(v, 9, 14, 15, 3.5, K('cloth_d'), squash=0.9)
    _disc(v, 16, 14, 2.5, 13, 18, K('clay'))
    for k in range(6):
        _box(v, 20 + (k % 3) * 3, 22 + (k % 3) * 3, 11 + (k // 3) * 5, 13 + (k // 3) * 5, 13, 15, K('fl_r') if k % 2 else K('out_leaf_pe'))
    _box(v, 2, 30, 4, 26, 30, 32, K('out_cloth_bw'))
    for x in range(2, 30, 6):
        _box(v, x, x + 3, 4, 26, 30, 32, K('out_cloth_b'))
    return one(v)


def gatehouse_out(name):
    """門楼 — 街道が塀を抜ける 2 マスに架かる木の櫓門。両端の柱、上の歩廊と屋根。"""
    W = V * 2
    v = np.zeros((W, V, 80), np.int16)
    for x0 in (0, W - 8):
        _box(v, x0, x0 + 8, 8, 24, 0, 60, K('log'))
        _box(v, x0 + 2, x0 + 6, 10, 22, 0, 60, K('log_d'))
        for z in range(10, 58, 8):
            _box(v, x0, x0 + 8, 8, 24, z, z + 1, K('lash'))
    _box(v, 0, W, 6, 26, 44, 48, K('timber'))                # 歩廊の床
    _box(v, 0, W, 6, 8, 48, 58, K('board'))                  # 手すりの板（外側は狭間）
    _box(v, 0, W, 24, 26, 48, 58, K('board'))
    for x in range(6, W - 6, 10):
        _box(v, x, x + 4, 6, 8, 54, 58, 0)
    for z in range(60, 72):
        d = (z - 60) * 1
        _box(v, 0, W, 4 + d, 28 - d, z, z + 1, K('shin_d') if (z % 3 == 0) else K('shin'))
    _box(v, 0, W, 15, 17, 71, 74, K('ridge'))
    _box(v, 28, 36, 9, 11, 30, 40, K('out_cloth_r'))          # 掲げた布
    _box(v, 30, 34, 9, 11, 32, 38, K('gold'))
    return [('main', v, (0, 0, 0))]


# ------------------------------------------------------------------ 登録
def register(g, h):
    global G, H, C, V
    G, H, C, V = g, h, g['C'], g['V']
    _palette()
    cat = g['CATALOG']
    static = g['static_part']
    wind = static(motion={'kind': 'wind'}, wind_k=0.6)
    for i in range(1, 5):
        cat['ground_mud_out_%02d' % i] = (ground_mud_out, static(), 'ぬかるみ（轍の無い所。水たまりと足跡）')
    #! 轍のタイル（`ground_mud_out_ew/ns/x`）と小路の轍は作らない——格子に見えた（2026-09-05 に気づいた）。
    #! 大通りは「縁 8 種のグラデーション ＋ 中央の黒めの泥」だけで表す。
    for side in ('n', 's', 'w', 'e'):
        for i in range(1, 9):
            cat['ground_mud_out_edge_%s_%02d' % (side, i)] = ((lambda s: (lambda n: _mud_edge(n, s)))(side), static(),
                                                             'ぬかるみの縁（草地が %s 側）' % side)
    for i in range(1, 4):
        cat['ground_field_out_%02d' % i] = (ground_field_out, static(), '畑の畝')
    cat['water_pond_out'] = (water_pond_out, static(), '池（歩けるマスに置く水）')
    for side in ('n', 's', 'w', 'e'):
        #! 名前は置く側の `rim` の引き方（`<rim>_n`）に合わせて **`wattle_fence_out_n`**。
        cat['wattle_fence_out_%s' % side] = (wattle_fence_out, static(), '編み垣（%s 側）' % side)
    single = {
        'puddle_plank': (puddle_plank_out, '水たまりの板'), 'stepping_stones': (stepping_stones_out, '飛び石'),
        'wagon_empty': (wagon_empty_out, '馬を外した荷馬車'), 'broken_cart': (broken_cart_out, '壊れた荷車'),
        'wheelbarrow': (wheelbarrow_out, '手押し車'), 'ladder': (ladder_out, '梯子'),
        'basket_pile': (basket_pile_out, '籠の山'), 'scarecrow': (scarecrow_out, '案山子'),
        'bean_poles': (bean_poles_out, '豆の支柱'), 'cabbage_row': (cabbage_row_out, 'キャベツの列'),
        'compost_heap': (compost_heap_out, '堆肥の山'), 'beehive': (beehive_out, '蜂の巣'),
        'pear_tree': (pear_tree_out, '梨の木'), 'willow': (willow_out, '柳'),
        'village_tree': (village_tree_out, '村の大木（3×3 マスに枝を張る）'),
        'cattail': (cattail_out, '蒲'), 'duck': (duck_out, '鴨'), 'washing_stones': (washing_stones_out, '洗い場'),
        'pig': (pig_out, '豚'), 'pig_mud': (pig_mud_out, '泥に寝る豚'), 'sheep': (sheep_out, '羊'),
        'goat_tethered': (goat_tethered_out, 'つながれた山羊'), 'chicken': (chicken_out, '鶏'),
        'goose': (goose_out, '鵞鳥'), 'dog': (dog_out, '寝そべる犬'), 'hen_house': (hen_house_out, '鶏小屋'),
        'bone_pile': (bone_pile_out, '骨の山'), 'wayside_cross': (wayside_cross_out, '道端の十字架'),
        'crow_post': (crow_post_out, '烏の杭'), 'skull_stake': (skull_stake_out, '頭骨の杭'),
        'stakes': (stakes_out, '逆茂木'), 'lumber_stack': (lumber_stack_out, '板の山'),
        'charcoal_clamp': (charcoal_clamp_out, '炭焼きの山'), 'tannery_vat': (tannery_vat_out, '革なめしの桶'),
        'dye_vat': (dye_vat_out, '染めの桶'), 'sawpit': (sawpit_out, '鋸引きの穴'),
        'smokehouse': (smokehouse_out, '燻製小屋'), 'bread_oven': (bread_oven_out, '共同のパン窯'),
        'stone_bench': (stone_bench_out, '石の腰掛け'), 'pillory': (pillory_out, 'さらし台'),
        'notice_board': (notice_board_out, '掲示板'), 'market_stall': (market_stall_out, '市の屋台'),
        'gatehouse': (gatehouse_out, '門楼（2 マス幅）'),
    }
    for key, (builder, note) in single.items():
        cat[key + '_out'] = (builder, static(), '村の暮らし: ' + note)
    for i in range(1, 3):
        cat['apple_tree_out_%02d' % i] = (apple_tree_out, wind, '林檎の木')
        cat['reeds_out_%02d' % i] = (reeds_out, wind, '葦の茂み')
    for i in range(1, 4):
        cat['grave_out_%02d' % i] = (grave_out, static(), '墓')
