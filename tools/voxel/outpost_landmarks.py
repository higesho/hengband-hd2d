# -*- coding: utf-8 -*-
"""辺境の地の目印の建物。

- `shop_out` … **穀物蔵**（4×4 マス）。石の束柱に載せた丸太組みの高床の蔵、南に荷降ろしの台
- `mill_out` … **水車小屋**（4×3 マス）。東面に水車（回転パーツ）。小川（`brook_ns_out`）が
  東隣のマスの西寄り（x 2..11）を流れるので、水車はそこへ掛かる大きさにしてある

置く側は `townset.landmark[1]`（shop）と `[2]`（mill）を `styled()` で引く。
"""
from __future__ import annotations

import numpy as np

G = None
H = None
C = None
V = 32


def K(name):
    return C[name]


def _log_courses(v, x0, x1, y0, y1, z0, z1, rng, course=5):
    """丸太を積んだ壁。段の境を 1 段暗くし、外面に溝を彫る。"""
    shades = [K('log'), K('log_l'), K('log_d')]
    for i, z in enumerate(range(z0, z1, course)):
        H['box'](v, x0, x1, y0, y1, z, min(z1, z + course), shades[(i + int(rng.integers(0, 3))) % 3])
        H['box'](v, x0, x1, y0, y1, z, z + 1, K('log_d'))
    for z in range(z0 + course, z1, course):
        v[x0:x1, y0, z] = 0
        v[x0:x1, y1 - 1, z] = 0
        v[x0, y0:y1, z] = 0
        v[x1 - 1, y0:y1, z] = 0


def _gable_roof(v, x0, x1, y0, y1, z0, rise, rng, shade_pairs):
    """東西に棟を通した切妻。y の中央が棟。段ごとにこけら板の色を替える。"""
    cy = (y0 + y1) / 2.0
    half = (y1 - y0) / 2.0
    for y in range(y0, y1):
        t = 1.0 - (abs((y + 0.5) - cy) / half)
        top = z0 + int(round(rise * t))
        for z in range(z0, top + 1):
            row = (z - z0) // 3
            H['box'](v, x0, x1, y, y + 1, z, z + 1, shade_pairs[row % len(shade_pairs)])
        H['box'](v, x0, x1, y, y + 1, top, top + 1, K('shin_d') if ((top - z0) % 3 == 0) else shade_pairs[((top - z0) // 3) % len(shade_pairs)])
    H['box'](v, x0, x1, int(cy) - 1, int(cy) + 1, z0 + rise, z0 + rise + 2, K('ridge'))


def shop_out(name):
    """穀物蔵 — 4×4 マス。束柱（野石）の上に丸太組みの蔵。南に荷降ろしの台と階段、袋と樽。"""
    rng = G['rng_for'](name)
    W = V * 4
    v = np.zeros((W, W, 96), np.int16)
    box = H['box']
    # 束柱（16 本）。地面に接するのはここだけ。
    for x in range(10, W - 10, 33):
        for y in range(10, W - 10, 33):
            box(v, x, x + 8, y, y + 8, 0, 12, K('rub_d'))
            box(v, x - 1, x + 9, y - 1, y + 9, 10, 12, K('rub_l'))       # ねずみ返し
    # 床（丸太の根太と板）。
    box(v, 6, W - 6, 6, W - 6, 12, 15, K('timber_d'))
    box(v, 8, W - 8, 8, W - 8, 15, 16, K('board'))
    # 壁（丸太組み）。北側を少し狭くして南に荷降ろしの台を空ける。
    wx0, wx1, wy0, wy1 = 8, W - 8, 8, W - 30
    _log_courses(v, wx0, wx1, wy0, wy1, 16, 56, rng)
    # 校倉の角の木口。
    for i, z in enumerate(range(16, 56, 5)):
        for (cx, cy) in ((wx0, wy0), (wx1 - 6, wy0), (wx0, wy1 - 6), (wx1 - 6, wy1 - 6)):
            if i % 2 == 0:
                box(v, cx - 2 if cx == wx0 else cx + 6, cx if cx == wx0 else cx + 8, cy, cy + 6, z + 1, z + 5, K('log_end'))
            else:
                box(v, cx, cx + 6, cy - 2 if cy == wy0 else cy + 6, cy if cy == wy0 else cy + 8, z + 1, z + 5, K('log_end'))
    # 南面の戸口（両開き）と小さな窓。
    box(v, 52, 76, wy1 - 1, wy1, 17, 44, K('board_d'))
    box(v, 63, 65, wy1 - 1, wy1, 17, 44, K('timber_d'))
    for x in (55, 60, 68, 73):
        box(v, x, x + 1, wy1 - 1, wy1, 17, 44, K('board'))
    for x in (20, 100):
        box(v, x, x + 8, wy1 - 1, wy1, 34, 42, K('timber_l'))
        box(v, x + 1, x + 7, wy1 - 1, wy1, 35, 41, K('dark'))
        box(v, x + 1, x + 7, wy1 - 1, wy1, 35, 41, K('timber_d'))
    # 荷降ろしの台（南の張り出し）と階段。
    box(v, 8, W - 8, wy1, W - 8, 12, 16, K('board'))
    for k in range(6):
        box(v, 58, 70, W - 8 + (k * 1), W - 8 + (k * 1) + 1, 0, 16 - (k * 2), K('timber'))
    for y in range(W - 20, W - 8, 4):
        box(v, 8, W - 8, y, y + 1, 15, 16, K('board_d'))
    # 袋と樽（台の上）。
    for (cx, cy) in ((22, W - 20), (32, W - 18), (100, W - 20)):
        H['ball'](v, cx, cy, 20, 5.5, K('cloth_d'), squash=0.85)
        box(v, cx - 2, cx + 2, cy - 2, cy + 2, 24, 27, K('cloth'))
    for cx in (88, 110):
        H['disc'](v, cx, W - 18, 5.0, 16, 30, K('timber_l'))
        H['disc'](v, cx, W - 18, 5.3, 19, 21, K('iron'), hollow=4.4)
        H['disc'](v, cx, W - 18, 5.3, 26, 28, K('iron'), hollow=4.4)
    # 屋根（切妻。東西に棟。大きく張り出す）。
    _gable_roof(v, 2, W - 2, 2, W - 2, 56, 34, rng, [K('shin'), K('shin_l'), K('out_shin_w'), K('shin_d')])
    # 妻の壁（東西）は丸太で埋める。
    cy = W / 2.0
    for y in range(2, W - 2):
        t = 1.0 - (abs((y + 0.5) - cy) / ((W - 4) / 2.0))
        top = 56 + int(round(34 * t))
        box(v, wx0, wx0 + 3, y, y + 1, 56, max(56, top - 3), K('log_l'))
        box(v, wx1 - 3, wx1, y, y + 1, 56, max(56, top - 3), K('log_l'))
    # 起重の梁（棟から南へ突き出す腕木と滑車）。
    box(v, 62, 66, W - 14, W + 0, 86, 90, K('timber_d'))
    H['disc'](v, 64, W - 2, 3.0, 82, 86, K('iron'))
    box(v, 63, 65, W - 3, W - 1, 30, 82, K('lash'))
    return [('main', v, (0, 0, 0))]


HUB = (V * 4 + 8.0, 48.0, 32.0)   #: 水車の軸（x はマスの外＝東隣のマスの x 8）


def mill_out(name):
    """水車小屋 — 4×3 マス。丸太組みの小屋に切妻、東面に大きな水車と樋。粉袋と石臼。"""
    rng = G['rng_for'](name)
    W, D = V * 4, V * 3
    hut = np.zeros((W + 24, D, 92), np.int16)   # 東へ 24 ボクセルはみ出す（水車と樋）
    box = H['box']
    # 土台（野石）。マスいっぱい。
    box(hut, 0, W, 0, D, 0, 8, K('rub_d'))
    for z in range(1, 7, 3):
        p = 0
        while p < W:
            w = int(rng.integers(5, 10))
            shade = int(rng.choice([K('rub'), K('rub_l'), K('rub_d')], p=[0.5, 0.28, 0.22]))
            box(hut, p, p + w, 0, 1, z, z + 2, shade)
            box(hut, p, p + w, D - 1, D, z, z + 2, shade)
            p += w + 1
    box(hut, 0, W, 0, D, 7, 8, K('rub_l'))
    # 壁（丸太組み）。
    wx0, wx1, wy0, wy1 = 3, W - 3, 3, D - 3
    _log_courses(hut, wx0, wx1, wy0, wy1, 8, 52, rng)
    # 南面の戸口（開いていて中が暗い）と窓 2 つ。
    box(hut, 50, 70, wy1 - 1, wy1, 9, 38, K('dark'))
    box(hut, 48, 50, wy1 - 1, wy1, 9, 40, K('timber_d'))
    box(hut, 70, 72, wy1 - 1, wy1, 9, 40, K('timber_d'))
    box(hut, 48, 72, wy1 - 1, wy1, 38, 40, K('timber_d'))
    for x in (18, 92):
        box(hut, x, x + 10, wy1 - 1, wy1, 26, 36, K('timber_l'))
        box(hut, x + 1, x + 9, wy1 - 1, wy1, 27, 35, K('glass'))
        box(hut, x + 4, x + 6, wy1 - 1, wy1, 27, 35, K('timber_d'))
    # 屋根（切妻。東西に棟）。
    _gable_roof(hut, 0, W + 4, 0, D, 52, 30, rng, [K('shin_d'), K('shin'), K('out_shin_w'), K('shin_l')])
    cy = D / 2.0
    for y in range(0, D):
        t = 1.0 - (abs((y + 0.5) - cy) / (D / 2.0))
        top = 52 + int(round(30 * t))
        box(hut, wx0, wx0 + 3, y, y + 1, 52, max(52, top - 3), K('log_l'))
        box(hut, wx1 - 3, wx1, y, y + 1, 52, max(52, top - 3), K('log_l'))
    # 東面の軸受けと軸（水車へ）。
    H['log_x'](hut, HUB[1], HUB[2], 2.5, W - 10, W + 14, K('log_d'))
    box(hut, W - 6, W, int(HUB[1]) - 5, int(HUB[1]) + 5, int(HUB[2]) - 5, int(HUB[2]) + 5, K('timber'))
    # 樋（屋根の東端から水車の上へ水を落とす板の樋）。
    box(hut, W - 20, W + 22, 8, 16, 60, 64, K('board_d'))
    box(hut, W - 18, W + 22, 10, 14, 62, 64, K('wa'))
    box(hut, W + 18, W + 22, 6, 18, 58, 66, K('board'))
    for x in (W - 16, W + 4):
        box(hut, x, x + 3, 9, 15, 8, 60, K('timber'))
    # 粉袋と石臼（南の戸口の脇）。
    for (cx, cy2) in ((26, D - 10), (34, D - 8)):
        H['ball'](hut, cx, cy2, 4, 5.0, K('cream_d'), squash=0.85)
        box(hut, cx - 2, cx + 2, cy2 - 2, cy2 + 2, 8, 10, K('cloth_d'))
    H['disc'](hut, 92, D - 10, 7.0, 0, 5, K('rock_l'))
    H['disc'](hut, 92, D - 10, 2.0, 0, 6, K('timber_d'))
    # 水車（回転パーツ。原点はプレハブ原点と同じ）。**南から見ると縁しか見えない**ので、
    # 輪を太く（x 12）・羽根を長く（x 20）して、斜めから見ても太鼓のように読めるようにする。
    wheel = np.zeros((W + 24, D, 92), np.int16)
    cxw, cyw, czw = HUB
    r_out, r_in = 26.0, 21.0
    for x in range(int(cxw) - 6, int(cxw) + 6):
        for y in range(D):
            for z in range(0, 60):
                d2 = ((y + 0.5 - cyw) ** 2) + ((z + 0.5 - czw) ** 2)
                if (r_in * r_in) <= d2 <= (r_out * r_out):
                    wheel[x, y, z] = K('timber') if (abs(x - cxw) < 2.5) else K('timber_l')
    for k in range(8):                                        # 輻（や）
        a = k * 0.7854
        for t in range(0, 22):
            y = int(round(cyw + (t * np.sin(a))))
            z = int(round(czw + (t * np.cos(a))))
            box(wheel, int(cxw) - 2, int(cxw) + 2, y - 1, y + 1, z - 1, z + 1, K('timber_d'))
    for k in range(12):                                       # 羽根（水受け）
        a = k * 0.5236
        y = cyw + (r_out - 2) * np.sin(a)
        z = czw + (r_out - 2) * np.cos(a)
        box(wheel, int(cxw) - 10, int(cxw) + 10, int(y) - 2, int(y) + 2, int(z) - 2, int(z) + 2, K('board_d'))
    H['log_x'](wheel, cyw, czw, 3.0, int(cxw) - 12, int(cxw) + 12, K('iron'))   # 軸
    return [('hut', hut, (0, 0, 0)), ('wheel', wheel, (0, 0, 0))]


def mill_parts():
    return [
        {'name': 'hut', 'voxels': 'hut', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
        {'name': 'wheel', 'voxels': 'wheel', 'parent': 'hut', 'grounded': False,
         'pivot': [float(HUB[0]), float(HUB[1]), float(HUB[2])],
         'motion': {'kind': 'rotate', 'axis': 'x', 'speed': 0.5, 'phase': 0.0}, 'wind_k': 0.0},
    ]


def register(g, h):
    global G, H, C, V
    G, H, C, V = g, h, g['C'], g['V']
    cat = g['CATALOG']
    cat['shop_out'] = (shop_out, g['static_part'](), '穀物蔵（4×4 マス。目印 1）')
    cat['mill_out'] = (mill_out, mill_parts(), '水車小屋（4×3 マス。目印 2。水車は回転パーツ）')
