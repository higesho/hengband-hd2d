# -*- coding: utf-8 -*-
"""Umoria のダンジョンの素材の生成器（「M2 の残り: ダンジョンの意匠」）。

`gen_prefabs.py` から `register(globals())` で呼ばれる。名前は **`_umd_<略号>`**:

- 深さの帯 `a`〜`f`（§1）: a 館の名残 / b 荒れた坑道 / c ミスリルの鉱脈 / d 焼けた岩 /
  e 名も無き者の地底 / f ドゥリンの禍の淵
- 部屋の種類 `vt` / `ph` / `mz` / `fr`（§2）: 宝物庫 / 柱の間 / 崩れた坑道 / ドワーフの居室。
  区域の意匠は壁と小物だけを差し替え、床は帯のまま（`terrain_view.cpp` の区域の上書き）。

材は `floor_umd_a_01` のように `<種>_umd_<略号>_<番号>`（引く側が `_01`〜`_10` を探す）。
小物は末尾に `_umd_<略号>`。火や光るものは本体と光を分ける。

## 床と壁の明度（決めたこと 2026-10-03「床と壁の明度に差をつけて識別しやすく」）
どの帯でも**壁は暗く、床は明るく**する。目安は L*（CIELAB）で 25 以上の差（§4）。
色の名前は `<材質の語>_umd_<略号>[_l|_d]`（`palette_materials.classify` が頭の語で材質を当てる）。

形の道具（`block_wall`・`rock_wall`・`paved`・`mottle`・`crack`）は Angband のダンジョンの
生成器から借りる（同じ規律で作る。別の名前で足すだけで、あちらの素材には触らない）。
"""
from __future__ import annotations

import importlib.util
import os

import numpy as np

G = None
C = None
V = 32
FLOOR_H = 4
WALL_H = 32
HERE = os.path.dirname(os.path.abspath(__file__))
A = None  #: Angband のダンジョンの生成器（形の道具を借りる）

BANDS = ('a', 'b', 'c', 'd', 'e', 'f')
ZONES = ('vt', 'ph', 'mz')   # 壁を差し替える区域（fr は小物だけ）
FLOOR_VARIANTS = 4
WALL_VARIANTS = 4
BEDROCK_VARIANTS = 2


def K(name):
    return C[name]


def rng_for(name):
    return G['rng_for'](name)


def vol(h=32):
    return np.zeros((V, V, h), np.int16)


def box(v, x0, x1, y0, y1, z0, z1, colour):
    A.box(v, x0, x1, y0, y1, z0, z1, colour)


def disc(v, cx, cy, r, z0, z1, colour, hollow=0.0):
    A.disc(v, cx, cy, r, z0, z1, colour, hollow)


def ball(v, cx, cy, cz, r, colour, squash=1.0):
    A.ball(v, cx, cy, cz, r, colour, squash)


def rod(v, p0, p1, r0, r1, colour):
    A.rod(v, p0, p1, r0, r1, colour)


def one(v):
    return [('main', v, (0, 0, 0))]


def pick(rng, shades, weights=None):
    return int(rng.choice(shades, p=weights))


def code_of(name):
    """`floor_umd_a_03` / `pillar_umd_ph` → 'a' / 'ph'。"""
    parts = name.split('_')
    return parts[parts.index('umd') + 1]


# ------------------------------------------------------------------ 色
def _palette():
    rl = G['reg_local']
    # a 館の名残: 青みの灰の切石（暗）／淡い敷石（明）
    rl('rock_umd_a', (70, 72, 80)); rl('rock_umd_a_l', (88, 90, 98)); rl('rock_umd_a_d', (54, 56, 62))
    rl('rock_umd_a_j', (34, 34, 40)); rl('rock_umd_a_c', (100, 102, 112))          # 彫りの帯
    rl('pav_umd_a', (156, 150, 138)); rl('pav_umd_a_l', (176, 170, 156)); rl('pav_umd_a_d', (130, 124, 114))
    rl('pav_umd_a_j', (92, 88, 80))
    rl('brass_umd_a', (120, 96, 52)); rl('brass_umd_a_d', (84, 66, 36))
    rl('cloth_umd_a', (110, 40, 36)); rl('cloth_umd_a_d', (74, 30, 28))
    # b 荒れた坑道: 茶の岩肌（暗）／土と砂利（明）
    rl('rock_umd_b', (80, 68, 54)); rl('rock_umd_b_l', (98, 84, 68)); rl('rock_umd_b_d', (46, 38, 30))
    rl('rock_umd_b_dd', (38, 32, 26))
    rl('dirt_umd_b', (156, 134, 102)); rl('dirt_umd_b_l', (176, 154, 120)); rl('dirt_umd_b_d', (128, 108, 80))
    rl('grit_umd_b', (170, 164, 150))
    rl('timber_umd_b', (92, 66, 42)); rl('timber_umd_b_l', (116, 86, 58)); rl('timber_umd_b_d', (62, 44, 28))
    rl('iron_umd_b', (72, 68, 68)); rl('iron_umd_b_l', (108, 104, 104)); rl('rust_umd_b', (120, 72, 42))
    rl('fl_umd_b', (250, 206, 110))
    # c ミスリルの鉱脈: 青黒い岩（暗）に銀の筋／灰の砕石（明）
    rl('rock_umd_c', (54, 58, 70)); rl('rock_umd_c_l', (68, 72, 86)); rl('rock_umd_c_d', (40, 42, 52))
    rl('rock_umd_c_dd', (30, 32, 40))
    rl('mith_umd_c', (196, 212, 236)); rl('mith_umd_c_l', (232, 240, 252)); rl('mith_umd_c_d', (140, 152, 176))
    rl('grit_umd_c', (134, 136, 144)); rl('grit_umd_c_l', (156, 158, 166)); rl('grit_umd_c_d', (110, 112, 120))
    rl('lit_umd_c', (210, 226, 255))
    # d 焼けた岩: 煤けた黒（暗）／灰（明）
    rl('soot_umd_d', (44, 38, 36)); rl('soot_umd_d_l', (58, 50, 46)); rl('soot_umd_d_d', (32, 28, 26))
    rl('ash_umd_d', (132, 124, 116)); rl('ash_umd_d_l', (152, 144, 134)); rl('ash_umd_d_d', (108, 100, 94))
    rl('ember_umd_d', (200, 82, 30)); rl('ember_umd_d_l', (246, 150, 60))
    rl('bone_umd_d', (150, 132, 108)); rl('bone_umd_d_d', (96, 82, 66)); rl('char_umd_d', (30, 26, 24))
    rl('iron_umd_d', (80, 70, 66))
    # e 名も無き者の地底: 濡れた黒緑（暗）／淡い粘土（明）
    rl('rock_umd_e', (36, 42, 42)); rl('rock_umd_e_l', (52, 62, 62)); rl('rock_umd_e_d', (26, 30, 30))
    rl('slime_umd_e', (70, 92, 74)); rl('slime_umd_e_l', (104, 132, 100))
    rl('clay_umd_e', (118, 124, 114)); rl('clay_umd_e_l', (138, 144, 132)); rl('clay_umd_e_d', (96, 102, 94))
    rl('wa_umd_e', (62, 84, 92)); rl('wa_umd_e_l', (92, 118, 128))
    rl('pale_umd_e', (196, 204, 184)); rl('pale_umd_e_d', (150, 160, 140)); rl('bone_umd_e', (184, 180, 160))
    rl('lit_umd_e', (150, 230, 200))
    # f ドゥリンの禍の淵: 赤黒い溶けた岩（暗）／黒曜の床（明るい黒灰）
    rl('obs_umd_f', (50, 30, 30)); rl('obs_umd_f_l', (66, 40, 38)); rl('obs_umd_f_d', (36, 22, 22))
    rl('obs_umd_f_f', (100, 90, 106)); rl('obs_umd_f_fl', (122, 110, 130)); rl('obs_umd_f_fd', (78, 70, 84))
    rl('lava_umd_f', (230, 96, 34)); rl('lava_umd_f_l', (252, 172, 64)); rl('lava_umd_f_d', (150, 46, 22))
    rl('slag_umd_f', (84, 70, 66))
    # 区域: 宝物庫（鉄の帯）・柱の間（彫りの柱）・崩れた坑道・居室
    rl('rock_umd_vt', (58, 56, 60)); rl('rock_umd_vt_l', (74, 72, 76)); rl('rock_umd_vt_j', (30, 30, 32))
    rl('iron_umd_vt', (48, 46, 50)); rl('iron_umd_vt_l', (96, 94, 100)); rl('gold_umd_vt', (212, 170, 64))
    rl('gold_umd_vt_d', (150, 116, 40)); rl('gem_umd_vt', (90, 170, 210)); rl('gem_umd_vt_r', (200, 60, 70))
    rl('timber_umd_vt', (84, 58, 36))
    rl('rock_umd_ph', (78, 76, 82)); rl('rock_umd_ph_l', (98, 96, 104)); rl('rock_umd_ph_d', (56, 54, 60))
    rl('rock_umd_ph_c', (118, 116, 124))
    rl('rock_umd_mz', (64, 56, 48)); rl('rock_umd_mz_l', (82, 72, 62)); rl('rock_umd_mz_d', (46, 40, 34))
    rl('timber_umd_mz', (90, 64, 40)); rl('timber_umd_mz_d', (60, 42, 26))
    rl('rock_umd_fr', (112, 106, 98)); rl('rock_umd_fr_d', (84, 80, 74)); rl('cloth_umd_fr', (96, 72, 52))
    rl('fur_umd_fr', (116, 94, 70)); rl('char_umd_fr', (34, 30, 28)); rl('timber_umd_fr', (100, 72, 46))


# ------------------------------------------------------------------ 床（第 2 版。2026-10-03）
#
# 第 1 版は 2 ピクセルのまだらか大きな平らな板で、起伏のある壁（第 3 版）の隣では紙を敷いたように
# 見えた（気づいたこと「床との組み合わせがイマイチ」）。第 2 版は床にも**塊と継ぎ目**の骨組みを与える:
#   - 面を塊（ボロノイか矩形）に分け、継ぎ目を 1〜2 ボクセル彫る（影が線を作る）
#   - 塊ごとに高さを変える（沈んだ板）
#   - 継ぎ目の底に**壁と同じ材**を見せる（a 目地の暗い石 / b 砂利 / c 銀 / d 燠 / e ぬめり / f 溶岩）
# 床の地の色は壁より明るいまま（§4 の L* 25 以上は変えない）。


def _floor_slabs(rng, n, jitter, slab_cols, slab_w, joint_col, joint_depth, gap_w, sunk_p, sunk_col=None):
    """塊に割った床。返り値は (体積, 塊の番号の地図, 継ぎ目までの差, 塊の数)。"""
    v = np.zeros((V, V, FLOOR_H), np.int16)
    top = FLOOR_H - 1
    v[:, :, :top] = joint_col
    near, d0, gap, cnt = _voronoi(rng, V, n, n, jitter)
    cols = [pick(rng, slab_cols, slab_w) for _ in range(cnt)]
    sunk = [rng.random() < sunk_p for _ in range(cnt)]
    for x in range(V):
        for y in range(V):
            k = int(near[x, y])
            if gap[x, y] < gap_w:
                for z in range(top, top - joint_depth, -1):
                    v[x, y, z] = 0
            elif sunk[k]:
                v[x, y, top] = 0
                v[x, y, top - 1] = cols[k] if sunk_col is None else sunk_col
            else:
                v[x, y, top] = cols[k]
    return v, near, gap, cnt


def _floor_a(rng):
    """館の名残 — 大判の敷石 2×2。継ぎ目は 2 幅・2 深で暗い石。板ごとに沈み、中央はすり減って窪む。
    3 割の板に彫りの線（縁から 2 内側の枠）。1 枚はひびで割れている。"""
    v = np.zeros((V, V, FLOOR_H), np.int16)
    top = FLOOR_H - 1
    v[:, :, :top] = K('pav_umd_a_j')
    v[:, :, 0] = K('rock_umd_a_j')
    step = 16
    for bx in range(0, V, step):
        for by in range(0, V, step):
            xa, xb, ya, yb = bx + 1, bx + step - 1, by + 1, by + step - 1   # 継ぎ目 2 幅
            shade = pick(rng, [K('pav_umd_a'), K('pav_umd_a_l'), K('pav_umd_a_d')], [0.5, 0.25, 0.25])
            z = top - (1 if rng.random() < 0.25 else 0)                     # 沈んだ板
            v[xa:xb, ya:yb, z] = shade
            if z == top - 1:
                v[xa:xb, ya:yb, top] = 0
            if rng.random() < 0.45:                                          # すり減った中央（丸い窪み）
                cx, cy = (xa + xb) / 2.0 + float(rng.uniform(-2, 2)), (ya + yb) / 2.0 + float(rng.uniform(-2, 2))
                rx, ry = float(rng.uniform(3.5, 5.5)), float(rng.uniform(3.5, 5.5))
                xs = np.arange(V)[:, None] + 0.5 - cx
                ys = np.arange(V)[None, :] + 0.5 - cy
                m = ((xs / rx) ** 2 + (ys / ry) ** 2) <= 1.0
                m[:xa, :] = m[xb:, :] = False
                m[:, :ya] = m[:, yb:] = False
                v[:, :, z][m] = 0
                v[:, :, z - 1][m] = K('pav_umd_a_d')
            elif rng.random() < 0.25:                                        # 彫りの枠線（たまに）
                v[xa + 2:xb - 2, ya + 2, z] = K('rock_umd_a_c')
                v[xa + 2:xb - 2, yb - 3, z] = K('rock_umd_a_c')
                v[xa + 2, ya + 2:yb - 2, z] = K('rock_umd_a_c')
                v[xb - 3, ya + 2:yb - 2, z] = K('rock_umd_a_c')
    # 継ぎ目を彫る（2 深）。
    for bx in range(0, V, step):
        v[bx:bx + 1, :, top] = 0
        v[bx:bx + 1, :, top - 1] = 0
        v[:, bx:bx + 1, top] = 0
        v[:, bx:bx + 1, top - 1] = 0
        v[bx + step - 1:bx + step, :, top] = 0
        v[bx + step - 1:bx + step, :, top - 1] = 0
        v[:, bx + step - 1:bx + step, top] = 0
        v[:, bx + step - 1:bx + step, top - 1] = 0
    if rng.random() < 0.6:                                                   # ひび（1 枚の板を横切る）
        x, y = int(rng.integers(2, 14)), int(rng.integers(2, 30))
        for _ in range(14):
            if 0 <= x < V and 0 <= y < V:
                v[x, y, top] = 0
                v[x, y, top - 1] = K('pav_umd_a_j')
            x += 1
            y += int(rng.integers(-1, 2))
    return [('main', v, (0, 0, -FLOOR_H))]


def _floor_b(rng):
    """坑道 — 踏み固めた土と岩の塊。継ぎ目には砂利が溜まる（1 深・明るい）。沈んだ塊は土。"""
    v, near, gap, cnt = _floor_slabs(rng, 3, 4.0,
                                     [K('dirt_umd_b'), K('dirt_umd_b_l'), K('rock_umd_b_l'), K('dirt_umd_b_d')],
                                     [0.4, 0.25, 0.2, 0.15], K('grit_umd_b'), 1, 1.0, 0.3, K('dirt_umd_b_d'))
    v[:, :, 0:2] = K('dirt_umd_b_d')
    v[:, :, 2][v[:, :, 2] == K('grit_umd_b')] = K('grit_umd_b')
    if rng.random() < 0.3:                                                   # 枕木の跡（1 本）
        y = int(rng.integers(4, V - 6))
        v[4:28, y:y + 2, FLOOR_H - 1] = K('timber_umd_b_d')
    return [('main', v, (0, 0, -FLOOR_H))]


def _floor_c(rng):
    """ミスリル — 灰青の砕石の塊。継ぎ目は 2 深で底に銀が光る。沈んだ塊の上に銀の結晶が 1 段立つ。"""
    v, near, gap, cnt = _floor_slabs(rng, 3, 3.0,
                                     [K('grit_umd_c'), K('grit_umd_c_l'), K('grit_umd_c_d')], [0.5, 0.3, 0.2],
                                     K('mith_umd_c_d'), 2, 1.1, 0.3)
    v[:, :, 0] = K('rock_umd_c_dd')
    top = FLOOR_H - 1
    for _ in range(int(rng.integers(1, 3))):                                 # 結晶（2x2。沈んだ所に立つ）
        x, y = int(rng.integers(2, V - 4)), int(rng.integers(2, V - 4))
        if (v[x, y, top] == 0) and (v[x, y, top - 1] != 0):
            v[x:x + 2, y:y + 2, top] = K('mith_umd_c_l')
            v[x:x + 2, y:y + 2, top - 1] = K('mith_umd_c')
    return [('main', v, (0, 0, -FLOOR_H))]


def _floor_d(rng):
    """焼けた岩 — 灰の積もった岩の板（壁と同じ板割り）。板の隙間は 2 深で燠が見える。灰の吹き溜まり。"""
    v, near, gap, cnt = _floor_slabs(rng, 4, 3.0,
                                     [K('ash_umd_d'), K('ash_umd_d_l'), K('ash_umd_d_d')], [0.5, 0.25, 0.25],
                                     K('char_umd_d'), 2, 1.2, 0.25)
    top = FLOOR_H - 1
    ember = (gap < 1.2) & (np.random.default_rng(int(rng.integers(0, 1 << 30))).random((V, V)) < 0.45)
    v[:, :, top - 2][ember] = K('ember_umd_d')
    for _ in range(2):                                                       # 灰の吹き溜まり（面一）
        k = int(rng.integers(0, cnt))
        m = (near == k) & (gap >= 1.2)
        v[:, :, top][m & (v[:, :, top] != 0)] = K('ash_umd_d_l')
    return [('main', v, (0, 0, -FLOOR_H))]


def _floor_e(rng):
    """地底 — 淡い粘土の塊。継ぎ目にぬめり（1 深・緑）。冷たい水たまり（2 深・縁は暗い水）。"""
    v, near, gap, cnt = _floor_slabs(rng, 3, 4.0,
                                     [K('clay_umd_e'), K('clay_umd_e_l'), K('clay_umd_e_d')], [0.5, 0.3, 0.2],
                                     K('slime_umd_e'), 1, 1.0, 0.0)
    v[:, :, 0:2] = K('clay_umd_e_d')
    top = FLOOR_H - 1
    for _ in range(int(rng.integers(1, 3))):
        cx, cy, r = int(rng.integers(6, 26)), int(rng.integers(6, 26)), float(rng.uniform(2.5, 5.0))
        xs = np.arange(V)[:, None] + 0.5 - cx
        ys = np.arange(V)[None, :] + 0.5 - cy
        d2 = xs * xs + ys * ys
        m = d2 <= r * r
        rim = (d2 <= (r + 1.2) ** 2) & ~m
        v[:, :, top][m] = 0
        v[:, :, top - 1][m] = 0
        v[:, :, top - 2][m] = K('wa_umd_e_l')
        v[:, :, top][rim] = 0
        v[:, :, top - 1][rim] = K('wa_umd_e')
    return [('main', v, (0, 0, -FLOOR_H))]


def _floor_f(rng):
    """淵 — 黒曜の板（壁の塊と同じ割り）。継ぎ目は 2 深で底に溶岩。沈んだ板。"""
    v, near, gap, cnt = _floor_slabs(rng, 3, 3.0,
                                     [K('obs_umd_f_f'), K('obs_umd_f_fl'), K('obs_umd_f_fd')], [0.5, 0.3, 0.2],
                                     K('lava_umd_f_d'), 2, 1.2, 0.3)
    top = FLOOR_H - 1
    hot = (gap < 1.2) & (np.random.default_rng(int(rng.integers(0, 1 << 30))).random((V, V)) < 0.5)
    v[:, :, top - 2][hot] = K('lava_umd_f')
    v[:, :, 0] = K('obs_umd_f_d')
    return [('main', v, (0, 0, -FLOOR_H))]


def floor_umd(name):
    b = code_of(name)
    rng = rng_for(name)
    return {'a': _floor_a, 'b': _floor_b, 'c': _floor_c, 'd': _floor_d, 'e': _floor_e, 'f': _floor_f}[b](rng)


# ------------------------------------------------------------------ 壁・岩盤（第 3 版。2026-10-03）
#
# 第 2 版は彫りが 1〜2 ボクセルで、面に模様を貼っただけに見えた（気づいたこと「どの壁も平面の模様
# だけだからボツ」「鉱脈がただの線なのはダメ」）。第 3 版は**面ごとに奥行きの地図 D[u, w] を持ち、
# 最大 8 ボクセルまで彫り込む**。石・坑木・結晶・岩の塊が本当に前へ出たり奥へ引っ込んだりする。
#
# ブロックの輪郭は保つ（決めたこと 2026-08-09「ブロック状を維持」「高さを統一」）:
#   - 接地の 1 段（w=0）と天面の 2 段（w=h-2, h-1）は彫らない
#   - 各面の端 2 列（u<2, u>=V-2）は 1 ボクセルまでしか彫らない（角の柱が残る）
#   - 天面は平らな 1 色
#
# 帯ごとの骨組み:
#   a 奥へ引いた壁体に、前へ出た切石（1 つずつ奥行きが違う）・中央の付け柱・張り出した蛇腹（階段文様の切り欠き）
#   b 層ごとに奥行きの違う層理（中の層が深く引っ込み、上の層が庇になる）・深い落盤の穴・前へ立つ坑木の柱と梁
#   c 青黒い層理に、斜めの深い溝（鉱脈の袋）と、その中に立ち並ぶ大きさの違う銀の結晶の塊。岩肌の銀のこぶ
#   d 焼けて剥がれかけた岩の板（板ごとに前後へずれる）・板の隙間は深く、底に燠。抜け落ちた板の穴
#   e 奥へ引いた濡れ岩に、前へ垂れ下がる太い滴りの柱（下で玉になる）・腰の濡れ帯
#   f 溶けた岩の塊が丸く膨らみ（中心が前、縁が奥）、塊の継ぎ目は深い溶岩の溝
#   vt 奥へ引いた切石に、前へ出た鉄の帯と、さらに前へ出た鋲
#   ph 丸い断面の深い縦溝（柱面）・前へ出た柱頭と柱礎
#   mz 層理に、深い落盤の穴と、前へ出た折れた坑木（斜め）と支柱の根元
#
# 奥行きの地図を体積へ写すのは `_relief`。表面の色は `Cm[u, w]`、表面の後ろ 4 ボクセルも同じ色で
# 塗る（前へ出た形の側面が地の色にならないように）。


def _maps(h, depth, colour):
    D = np.full((V, h), depth, int)
    Cm = np.full((V, h), colour, int)
    return D, Cm


def _relief(v, face, D, Cm):
    """奥行きの地図を 1 面へ写す。D[u, w] だけ彫り、表面と後ろ 4 ボクセルを Cm[u, w] で塗る。"""
    at = A._face_setter(face)
    h = v.shape[2]
    for u in range(V):
        edge = (u < 2) or (u >= V - 2)
        for w in range(1, h - 2):
            d = int(D[u, w])
            if edge:
                d = min(d, 1)
            for k in range(d):
                v[at(u, w, k)] = 0
            c = int(Cm[u, w])
            for k in range(d, min(d + 4, V // 2)):
                v[at(u, w, k)] = c


def _top(v, rng, colour, patch=None):
    h = v.shape[2]
    v[:, :, h - 1] = colour
    if patch is not None:
        for _ in range(3):
            x0, y0 = int(rng.integers(0, V - 6)), int(rng.integers(0, V - 6))
            v[x0:x0 + int(rng.integers(4, 7)), y0:y0 + int(rng.integers(4, 7)), h - 1] = patch


def _rect(D, Cm, u0, u1, w0, w1, depth, colour):
    u0, u1 = max(0, u0), min(V, u1)
    w0, w1 = max(0, w0), min(D.shape[1], w1)
    if (u0 < u1) and (w0 < w1):
        D[u0:u1, w0:w1] = depth
        Cm[u0:u1, w0:w1] = colour


def _strata_maps(rng, h, layers):
    """層理。`layers` は `(z0, z1, 奥行き, 色)`。層の境は ±1 で波打つ。"""
    D, Cm = _maps(h, layers[0][2], layers[0][3])
    for z0, z1, d, c in layers:
        D[:, z0:z1] = d
        Cm[:, z0:z1] = c
    for z0, z1, d, c in layers[1:]:
        wob = 0
        for u in range(V):
            if rng.random() < 0.3:
                wob = int(np.clip(wob + int(rng.integers(-1, 2)), -1, 1))
            for w in range(max(0, z0 + wob), min(h, z1 + wob)):
                D[u, w] = d
                Cm[u, w] = c
    return D, Cm


def _pockets(rng, D, Cm, n, depth, colour, wr=(7, 13), hr=(4, 8), zr=None):
    """落盤の穴（深い）。"""
    h = D.shape[1]
    z0r = zr or (2, h - 7)
    for _ in range(n):
        du, dw = int(rng.integers(*wr)), int(rng.integers(*hr))
        u0 = int(rng.integers(0, V - du))
        w0 = int(rng.integers(z0r[0], max(z0r[0] + 1, z0r[1] - dw)))
        _rect(D, Cm, u0, u0 + du, w0, w0 + dw, depth, colour)
        #! 穴の縁を欠けさせる（四角い穴は人工に見える）。
        for _ in range(4):
            eu, ew = int(rng.integers(u0 - 1, u0 + du + 1)), int(rng.integers(w0 - 1, w0 + dw + 1))
            if (0 <= eu < V) and (0 <= ew < h) and (D[eu, ew] < depth):
                D[eu, ew] = max(D[eu, ew], depth - 2)


def _voronoi(rng, h, n_u=3, n_w=3, jitter=3.0):
    """面を塊に分ける。各セルの (いちばん近い塊の番号, 中心までの距離, 2 番目との差) を返す。"""
    cs = []
    for i in range(n_u):
        for j in range(n_w):
            cu = (i + 0.5) * V / n_u + float(rng.uniform(-jitter, jitter))
            cw = (j + 0.5) * h / n_w + float(rng.uniform(-jitter, jitter))
            cs.append((cu, cw))
    uu, ww = np.meshgrid(np.arange(V), np.arange(h), indexing='ij')
    dist = np.stack([np.hypot(uu - cu, ww - cw) for cu, cw in cs], axis=0)
    order = np.argsort(dist, axis=0)
    near = order[0]
    d0 = np.take_along_axis(dist, order[0:1], axis=0)[0]
    d1 = np.take_along_axis(dist, order[1:2], axis=0)[0]
    return near, d0, d1 - d0, len(cs)


# ---- a 館の名残
def _wall_a(rng, h, bedrock):
    v = np.full((V, V, h), K('rock_umd_a_d'), np.int16)
    for face in range(4):
        D, Cm = _maps(h, 5, K('rock_umd_a_j'))                 # 目地の底まで引く
        course = 10
        for ci, cz in enumerate(range(0, h, course)):
            off = (ci % 2) * 6
            u = -off
            while u < V:
                bw = int(rng.integers(10, 15))
                ua, ub = max(u, 0), min(u + bw - 1, V)
                if ua < ub:
                    depth = int(rng.choice([1, 2, 3], p=[0.35, 0.4, 0.25]))   # 石ごとに前後
                    shade = pick(rng, [K('rock_umd_a'), K('rock_umd_a_l'), K('rock_umd_a_d')], [0.55, 0.2, 0.25])
                    _rect(D, Cm, ua, ub, cz, cz + course - 1, depth, shade)
                    if rng.random() < 0.3:                       # 角の欠けた石
                        _rect(D, Cm, ub - 3, ub, cz + course - 4, cz + course - 1, depth + 2, K('rock_umd_a_d'))
                u += bw
        if not bedrock:
            #! 中央の付け柱（前へ出る）。
            pu = int(rng.integers(11, 17))
            _rect(D, Cm, pu, pu + 5, 1, h - 2, 0, K('rock_umd_a_l'))
            for z in range(6, h - 8, 9):
                _rect(D, Cm, pu, pu + 5, z, z + 1, 2, K('rock_umd_a_j'))
            #! 張り出した蛇腹（前へ出る）と、階段文様の切り欠き。
            _rect(D, Cm, 0, V, 20, 21, 6, K('rock_umd_a_j'))       # 蛇腹の下の影
            _rect(D, Cm, 0, V, 21, 29, 0, K('rock_umd_a_c'))
            for u in range(2, V - 3, 7):
                _rect(D, Cm, u, u + 3, 22, 27, 3, K('rock_umd_a_j'))
                _rect(D, Cm, u + 3, u + 5, 24, 27, 3, K('rock_umd_a_j'))
        _relief(v, face, D, Cm)
    _top(v, rng, K('rock_umd_a_d'), K('rock_umd_a_j'))
    return v


# ---- b 荒れた坑道
def _wall_b(rng, h, bedrock):
    v = np.full((V, V, h), K('rock_umd_b_d'), np.int16)
    tim = (K('timber_umd_b'), K('timber_umd_b_l'), K('timber_umd_b_d'))
    for face in range(4):
        D, Cm = _strata_maps(rng, h, [(0, 9, 2, K('rock_umd_b')), (9, 20, 5, K('rock_umd_b_d')),
                                      (20, h, 1, K('rock_umd_b'))])
        _pockets(rng, D, Cm, 2, 8, K('rock_umd_b_dd'), zr=(2, h - 9))
        if not bedrock:
            if rng.random() < 0.7:
                u0 = int(rng.integers(2, V - 8))
                _rect(D, Cm, u0, u0 + 4, 1, h - 7, 0, tim[0])      # 前へ立つ支柱
                _rect(D, Cm, u0 + 1, u0 + 3, 1, h - 7, 0, tim[1])  # 木目（中央が明るい）
                _rect(D, Cm, u0, u0 + 4, 11, 12, 1, tim[2])
                _rect(D, Cm, u0, u0 + 4, 21, 22, 1, tim[2])
            _rect(D, Cm, 0, V, h - 7, h - 2, 0, tim[0])             # 梁
            _rect(D, Cm, 0, V, h - 5, h - 4, 0, tim[1])
            for u in range(3, V, 9):                                 # 梁の楔
                _rect(D, Cm, u, u + 2, h - 7, h - 2, 1, tim[2])
        _relief(v, face, D, Cm)
    _top(v, rng, K('rock_umd_b_d'), K('rock_umd_b_dd'))
    return v


# ---- c ミスリルの鉱脈
def _wall_c(rng, h, bedrock):
    v = np.full((V, V, h), K('rock_umd_c_d'), np.int16)
    for face in range(4):
        D, Cm = _strata_maps(rng, h, [(0, 11, 1, K('rock_umd_c')), (11, 21, 4, K('rock_umd_c_d')),
                                      (21, h, 1, K('rock_umd_c'))])
        _pockets(rng, D, Cm, 1, 7, K('rock_umd_c_dd'), wr=(6, 10), hr=(4, 7))
        if (not bedrock) or (rng.random() < 0.5):
            #! 鉱脈の袋: 斜めの深い溝（幅 7・深さ 6）。
            slope = float(rng.uniform(0.35, 0.9)) * float(rng.choice([-1, 1]))
            c0 = float(rng.uniform(V * 0.3, V * 0.7))
            uu, ww = np.meshgrid(np.arange(V), np.arange(h), indexing='ij')
            dd = (uu - c0) - slope * (ww - h / 2.0)
            pocket = np.abs(dd) < 3.5
            D[pocket] = 6
            Cm[pocket] = K('rock_umd_c_dd')
            #! 結晶の塊: 袋の中に、大きさ 2〜5・奥行き 1〜4 の角張った塊を立ち並べる。
            #! 浅い（前へ出た）塊ほど明るい。袋の縁をはみ出す塊もある（岩肌へ食い込んだ鉱脈）。
            for w in range(2, h - 3, 2):
                uc = c0 + slope * (w - h / 2.0) + float(rng.uniform(-2.5, 2.5))
                if rng.random() < 0.8:
                    size = int(rng.integers(2, 6))
                    depth = int(rng.integers(1, 5))
                    u0 = int(uc) - size // 2
                    col = K('mith_umd_c_l') if depth <= 2 else (K('mith_umd_c') if depth == 3 else K('mith_umd_c_d'))
                    _rect(D, Cm, u0, u0 + size, w, w + int(rng.integers(2, 5)), depth, col)
                    if size >= 4:                                # 大きな塊は角を落として結晶らしく
                        _rect(D, Cm, u0, u0 + 1, w, w + 1, depth + 1, K('mith_umd_c_d'))
                        _rect(D, Cm, u0 + size - 1, u0 + size, w, w + 1, depth + 1, K('mith_umd_c_d'))
        #! 岩肌の銀のこぶ（前へ出る 3x3）。
        for _ in range(2):
            u0, w0 = int(rng.integers(2, V - 5)), int(rng.integers(3, h - 6))
            if D[u0, w0] >= 1 and D[u0, w0] <= 4:
                _rect(D, Cm, u0, u0 + 3, w0, w0 + 3, 0, K('mith_umd_c'))
                _rect(D, Cm, u0 + 1, u0 + 2, w0 + 1, w0 + 2, 0, K('mith_umd_c_l'))
        _relief(v, face, D, Cm)
    _top(v, rng, K('rock_umd_c_d'), K('rock_umd_c_dd'))
    return v


# ---- d 焼けた岩
def _wall_d(rng, h, bedrock):
    v = np.full((V, V, h), K('soot_umd_d_d'), np.int16)
    ember = K('char_umd_d') if bedrock else K('ember_umd_d')
    for face in range(4):
        near, d0, gap, n = _voronoi(rng, h, 3, 4, 3.5)
        plate_depth = [int(rng.choice([0, 1, 2, 4], p=[0.3, 0.3, 0.25, 0.15])) for _ in range(n)]
        plate_col = [K('soot_umd_d_l') if rng.random() < 0.3 else K('soot_umd_d') for _ in range(n)]
        D, Cm = _maps(h, 0, K('soot_umd_d'))
        for u in range(V):
            for w in range(h):
                k = int(near[u, w])
                D[u, w] = plate_depth[k]
                Cm[u, w] = plate_col[k] if w < 22 else K('soot_umd_d_d')   # 上は煤で暗い
                if gap[u, w] < 1.4:                              # 板の隙間: 深く、底に燠
                    D[u, w] = 6
                    Cm[u, w] = ember if gap[u, w] < 0.8 else K('char_umd_d')
        #! 抜け落ちた板（1 枚だけ穴）。
        k = int(rng.integers(0, n))
        mask = near == k
        if mask.sum() > 20:
            D[mask] = 7
            Cm[mask] = K('char_umd_d')
        _relief(v, face, D, Cm)
    _top(v, rng, K('soot_umd_d_d'), K('char_umd_d'))
    return v


# ---- e 名も無き者の地底
def _wall_e(rng, h, bedrock):
    v = np.full((V, V, h), K('rock_umd_e_d'), np.int16)
    for face in range(4):
        D, Cm = _strata_maps(rng, h, [(0, 6, 1, K('rock_umd_e_l')), (6, 17, 4, K('rock_umd_e')),
                                      (17, h, 2, K('rock_umd_e_d'))])
        _pockets(rng, D, Cm, 1, 7, K('rock_umd_e_d'), wr=(7, 12), hr=(5, 8), zr=(7, h - 5))
        _rect(D, Cm, 0, V, 1, 3, 1, K('wa_umd_e'))                # 腰の濡れ帯
        #! 前へ垂れ下がる滴りの柱: 上端で幅 3・奥行き 0、下で細り、玉で終わる。
        for _ in range(int(rng.integers(2, 4)) if not bedrock else 1):
            u0 = int(rng.integers(2, V - 5))
            length = int(rng.integers(10, 26))
            for k in range(length):
                w = h - 3 - k
                if w < 2:
                    break
                width = 3 if k < length * 0.5 else (2 if k < length * 0.8 else 1)
                _rect(D, Cm, u0, u0 + width, w, w + 1, 0, K('slime_umd_e'))
                if width == 3:
                    _rect(D, Cm, u0 + 1, u0 + 2, w, w + 1, 0, K('slime_umd_e_l'))
            w_end = max(2, h - 3 - length)
            _rect(D, Cm, u0 - 1, u0 + 3, w_end - 1, w_end + 2, 0, K('slime_umd_e_l'))
        _relief(v, face, D, Cm)
    _top(v, rng, K('rock_umd_e_d'), K('rock_umd_e'))
    return v


# ---- f ドゥリンの禍の淵
def _wall_f(rng, h, bedrock):
    v = np.full((V, V, h), K('obs_umd_f'), np.int16)
    seam = K('lava_umd_f_d') if bedrock else K('lava_umd_f')
    for face in range(4):
        near, d0, gap, n = _voronoi(rng, h, 3, 3, 3.0)
        lobe_col = [K('obs_umd_f_l') if rng.random() < 0.35 else K('obs_umd_f') for _ in range(n)]
        D, Cm = _maps(h, 0, K('obs_umd_f'))
        for u in range(V):
            for w in range(h):
                k = int(near[u, w])
                #! 塊は丸く膨らむ: 中心が前（0）、縁へ向かって奥（3）へ。
                r = float(d0[u, w]) / max(1.0, float(d0[u, w] + gap[u, w]))
                D[u, w] = int(min(3, round(r * 3.6)))
                Cm[u, w] = lobe_col[k] if D[u, w] < 2 else K('obs_umd_f_d')
                if gap[u, w] < 1.5:                              # 継ぎ目は深い溶岩の溝
                    D[u, w] = 6
                    Cm[u, w] = seam if gap[u, w] < 0.8 else K('lava_umd_f_d')
        _relief(v, face, D, Cm)
    _top(v, rng, K('obs_umd_f_d'), K('obs_umd_f'))
    return v


# ---- 区域: 宝物庫（vt）
def _wall_vt(rng, h):
    v = np.full((V, V, h), K('rock_umd_vt'), np.int16)
    for face in range(4):
        D, Cm = _maps(h, 5, K('rock_umd_vt_j'))
        course = 11
        for ci, cz in enumerate(range(0, h, course)):
            off = (ci % 2) * 7
            u = -off
            while u < V:
                bw = int(rng.integers(12, 18))
                ua, ub = max(u, 0), min(u + bw - 1, V)
                if ua < ub:
                    _rect(D, Cm, ua, ub, cz, cz + course - 1, 3, K('rock_umd_vt_l') if rng.random() < 0.3 else K('rock_umd_vt'))
                u += bw
        for z0 in (7, 19):                                       # 前へ出た鉄の帯と鋲
            _rect(D, Cm, 0, V, z0, z0 + 4, 1, K('iron_umd_vt'))
            for u in range(3, V - 1, 7):
                _rect(D, Cm, u, u + 2, z0 + 1, z0 + 3, 0, K('iron_umd_vt_l'))
        _relief(v, face, D, Cm)
    _top(v, rng, K('rock_umd_vt'), K('iron_umd_vt'))
    return v


# ---- 区域: 柱の間（ph）
def _wall_ph(rng, h):
    v = np.full((V, V, h), K('rock_umd_ph'), np.int16)
    for face in range(4):
        D, Cm = _maps(h, 0, K('rock_umd_ph'))
        for u in range(2, V - 2, 6):                              # 丸い断面の縦溝
            for i, d in enumerate((2, 4, 4, 2)):
                _rect(D, Cm, u + i, u + i + 1, 6, h - 8, d, K('rock_umd_ph_d') if d == 4 else K('rock_umd_ph'))
        _rect(D, Cm, 0, V, h - 8, h - 7, 5, K('rock_umd_ph_d'))     # 柱頭の下の影
        _rect(D, Cm, 0, V, h - 7, h - 2, 0, K('rock_umd_ph_c'))     # 柱頭（前へ出る）
        _rect(D, Cm, 0, V, 5, 6, 5, K('rock_umd_ph_d'))             # 柱礎の上の影
        _rect(D, Cm, 0, V, 1, 5, 0, K('rock_umd_ph_l'))             # 柱礎（前へ出る）
        _relief(v, face, D, Cm)
    _top(v, rng, K('rock_umd_ph_d'))
    return v


# ---- 区域: 崩れた坑道（mz）
def _wall_mz(rng, h):
    v = np.full((V, V, h), K('rock_umd_mz_d'), np.int16)
    tim = (K('timber_umd_mz'), K('timber_umd_mz_d'))
    for face in range(4):
        D, Cm = _strata_maps(rng, h, [(0, 8, 2, K('rock_umd_mz')), (8, 19, 5, K('rock_umd_mz_d')),
                                      (19, h, 1, K('rock_umd_mz'))])
        _pockets(rng, D, Cm, int(rng.integers(2, 4)), 8, K('rock_umd_mz_d'), wr=(8, 14), hr=(5, 9))
        if rng.random() < 0.7:                                    # 前へ出た折れた坑木（斜め）
            u0, sgn = int(rng.integers(4, V - 10)), int(rng.choice([-1, 1]))
            u = float(u0)
            for w in range(2, h - 4):
                u += 0.45 * sgn
                _rect(D, Cm, int(u), int(u) + 3, w, w + 1, 0, tim[0])
                _rect(D, Cm, int(u) + 1, int(u) + 2, w, w + 1, 0, tim[1])
            stub = int(rng.integers(2, V - 6))                     # 根元だけ残った支柱
            _rect(D, Cm, stub, stub + 4, 1, int(rng.integers(8, 14)), 0, tim[0])
            _rect(D, Cm, stub, stub + 4, int(rng.integers(8, 14)) - 1, int(rng.integers(8, 14)), 1, tim[1])
        _relief(v, face, D, Cm)
    _top(v, rng, K('rock_umd_mz_d'))
    return v


def _wall(name, bedrock):
    code = code_of(name)
    rng = rng_for(name)
    h = WALL_H
    if code in ('vt', 'ph', 'mz'):
        v = {'vt': _wall_vt, 'ph': _wall_ph, 'mz': _wall_mz}[code](rng, h)
    else:
        v = {'a': _wall_a, 'b': _wall_b, 'c': _wall_c, 'd': _wall_d, 'e': _wall_e, 'f': _wall_f}[code](rng, h, bedrock)
    return one(v)


def wall_umd(name):
    return _wall(name, False)


def bedrock_umd(name):
    return _wall(name, True)


# ------------------------------------------------------------------ 小物（共通の形）
def heap(v, rng, n, shades, zcap, span=(3, 8)):
    for _ in range(n):
        cx, cy = int(rng.integers(8, 24)), int(rng.integers(8, 24))
        w, d = int(rng.integers(*span)), int(rng.integers(*span))
        z1 = int(rng.integers(2, zcap))
        box(v, cx - w // 2, cx + (w + 1) // 2, cy - d // 2, cy + (d + 1) // 2, 0, z1, pick(rng, shades))


def column(v, base, mid, top, r_base, r_shaft, h):
    disc(v, 16, 16, r_base + 1.4, 0, 2, base)
    disc(v, 16, 16, r_base, 2, 4, mid)
    disc(v, 16, 16, r_shaft, 4, h - 5, mid)
    disc(v, 16, 16, r_shaft + 1.0, h - 5, h - 3, top)
    box(v, 9, 23, 9, 23, h - 3, h, base)


def bones(v, rng, bone, bone_d, n=4):
    for _ in range(n):
        x, y = int(rng.integers(5, 22)), int(rng.integers(5, 22))
        ln = int(rng.integers(5, 9))
        if rng.random() < 0.5:
            box(v, x, x + ln, y, y + 1, 0, 1, bone_d)
            box(v, x - 1, x + 1, y - 1, y + 1, 0, 2, bone)
        else:
            box(v, x, x + 1, y, y + ln, 0, 1, bone_d)
            box(v, x - 1, x + 1, y - 1, y + 1, 0, 2, bone)


def skull(v, x, y, z, bone, dark):
    box(v, x, x + 7, y, y + 6, z, z + 5, bone)
    box(v, x + 1, x + 3, y + 5, y + 6, z + 2, z + 4, dark)
    box(v, x + 4, x + 6, y + 5, y + 6, z + 2, z + 4, dark)


def flame(h, at_z, colours=('fl_edge', 'fl_mid', 'fl_core')):
    v = vol(h=h)
    box(v, 14, 18, 14, 18, at_z, at_z + 5, K(colours[0]))
    box(v, 15, 17, 15, 17, at_z + 2, at_z + 8, K(colours[1]))
    box(v, 15, 17, 15, 17, at_z + 3, at_z + 6, K(colours[2]))
    return one(v)


# ---- a 館の名残
def rubble_umd_a(name):
    """① 崩れた彫りの石（彫りの帯の欠片が混じる）。"""
    rng = rng_for(name)
    v = vol(h=14)
    heap(v, rng, 9, [K('rock_umd_a'), K('rock_umd_a_l'), K('rock_umd_a_d'), K('rock_umd_a_c')], 12)
    return one(v)


def candelabrum_umd_a(name):
    """③ 消えた燭台（真鍮の三叉。蝋は溶け落ちている）。"""
    v = vol(h=40)
    disc(v, 16, 16, 5, 0, 2, K('brass_umd_a_d'))
    box(v, 15, 17, 15, 17, 2, 30, K('brass_umd_a'))
    box(v, 8, 24, 15, 17, 28, 30, K('brass_umd_a'))
    for x in (8, 15, 22):
        box(v, x, x + 2, 15, 17, 30, 34, K('brass_umd_a_d'))
    return one(v)


def statue_shard_umd_a(name):
    """② 倒れたドワーフの像の頭（兜と髭）。"""
    v = vol(h=16)
    box(v, 9, 23, 10, 22, 0, 10, K('rock_umd_a_l'))
    box(v, 11, 21, 21, 24, 0, 8, K('rock_umd_a_l'))                  # 髭（手前）
    box(v, 8, 24, 9, 23, 10, 14, K('rock_umd_a'))                    # 兜
    box(v, 12, 14, 22, 23, 6, 8, K('rock_umd_a_j'))
    box(v, 18, 20, 22, 23, 6, 8, K('rock_umd_a_j'))
    return one(v)


def rune_slab_umd_a(name):
    """② 壁から外れた碑の板（ドワーフの直線の文字）。"""
    v = vol(h=24)
    box(v, 8, 24, 18, 22, 0, 22, K('rock_umd_a_l'))
    for z in range(4, 20, 4):
        box(v, 10, 22, 22, 23, z, z + 1, K('rock_umd_a_c'))
        box(v, 10 + (z % 3) * 4, 12 + (z % 3) * 4, 22, 23, z + 1, z + 3, K('rock_umd_a_c'))
    return one(v)


def pillar_umd_a(name):
    """③ 彫りのある角柱（ドワーフの館の柱）。"""
    v = vol(h=54)
    box(v, 8, 24, 8, 24, 0, 4, K('rock_umd_a_d'))
    box(v, 10, 22, 10, 22, 4, 48, K('rock_umd_a'))
    for z in range(10, 46, 9):
        box(v, 9, 23, 9, 23, z, z + 2, K('rock_umd_a_c'))
    box(v, 7, 25, 7, 25, 48, 54, K('rock_umd_a_l'))
    return one(v)


def bench_umd_a(name):
    """④ 石の長椅子（片脚が折れて傾く）。"""
    v = vol(h=12)
    box(v, 6, 9, 13, 19, 0, 6, K('rock_umd_a_d'))
    box(v, 4, 28, 12, 20, 6, 9, K('rock_umd_a_l'))
    box(v, 22, 28, 12, 20, 0, 4, K('rock_umd_a'))                    # 折れた脚
    return one(v)


def urn_umd_a(name):
    """④ 倒れた真鍮の壺。"""
    v = vol(h=14)
    ball(v, 16, 16, 6, 6, K('brass_umd_a'), squash=1.0)
    box(v, 22, 26, 13, 19, 3, 9, K('brass_umd_a_d'))
    return one(v)


def banner_umd_a(name):
    """④ 落ちて朽ちた旗（赤い布に真鍮の竿）。"""
    rng = rng_for(name)
    v = vol(h=6)
    rod(v, (4, 10, 1), (28, 22, 1), 0.9, 0.9, K('brass_umd_a_d'))
    for _ in range(14):
        x, y = int(rng.integers(8, 24)), int(rng.integers(8, 22))
        box(v, x, x + 3, y, y + 3, 0, 1, K('cloth_umd_a') if rng.random() < 0.6 else K('cloth_umd_a_d'))
    return one(v)


# ---- b 荒れた坑道
def cavein_umd_b(name):
    rng = rng_for(name)
    v = vol(h=20)
    heap(v, rng, 12, [K('rock_umd_b'), K('rock_umd_b_l'), K('rock_umd_b_d'), K('dirt_umd_b_d')], 18, (4, 9))
    rod(v, (4, 8, 4), (26, 22, 12), 1.2, 1.2, K('timber_umd_b'))      # 折れた坑木
    return one(v)


def pickaxe_umd_b(name):
    v = vol(h=6)
    rod(v, (6, 10, 1), (24, 22, 1), 0.8, 0.8, K('timber_umd_b_l'))
    rod(v, (20, 26, 1), (28, 16, 1), 1.0, 0.6, K('iron_umd_b'))
    return one(v)


def timber_post_umd_b(name):
    """② 壁際の坑木の支え（傾いている）。"""
    v = vol(h=36)
    rod(v, (16, 14, 0), (18, 14, 32), 1.6, 1.6, K('timber_umd_b'))
    box(v, 8, 26, 12, 17, 30, 34, K('timber_umd_b_d'))
    return one(v)


def ore_pile_umd_b(name):
    rng = rng_for(name)
    v = vol(h=10)
    heap(v, rng, 10, [K('rock_umd_b_l'), K('rust_umd_b'), K('grit_umd_b')], 8, (2, 5))
    return one(v)


def timber_frame_umd_b(name):
    """③ 坑木の門形の枠。"""
    v = vol(h=54)
    for x in (7, 23):
        box(v, x, x + 3, 14, 18, 0, 48, K('timber_umd_b'))
    box(v, 5, 28, 13, 19, 46, 51, K('timber_umd_b_d'))
    return one(v)


def lantern_umd_b(name):
    """③ 坑夫のランタン（杭に吊る）。灯は `lantern_glow_umd_b`。"""
    v = vol(h=40)
    box(v, 15, 17, 15, 17, 0, 34, K('timber_umd_b_d'))
    box(v, 15, 22, 15, 17, 32, 34, K('timber_umd_b_d'))
    box(v, 18, 23, 13, 19, 22, 30, K('iron_umd_b'))
    box(v, 19, 22, 14, 18, 23, 29, 0)
    return one(v)


def lantern_glow_umd_b(name):
    v = vol(h=40)
    box(v, 19, 22, 14, 18, 23, 29, K('fl_umd_b'))
    return one(v)


def minecart_umd_b(name):
    v = vol(h=16)
    box(v, 6, 26, 9, 23, 4, 14, K('iron_umd_b'))
    box(v, 8, 24, 11, 21, 6, 14, 0)
    box(v, 8, 24, 11, 21, 6, 10, K('rock_umd_b_l'))
    for x in (8, 21):
        for y in (8, 22):
            box(v, x, x + 3, y, y + 2, 0, 5, K('rust_umd_b'))
    return one(v)


def bucket_umd_b(name):
    v = vol(h=10)
    disc(v, 16, 16, 4.5, 0, 9, K('timber_umd_b'), hollow=3.0)
    disc(v, 16, 16, 3.0, 0, 1, K('timber_umd_b_d'))
    disc(v, 16, 16, 4.8, 3, 4, K('iron_umd_b'), hollow=4.0)
    return one(v)


# ---- c ミスリルの鉱脈
def rubble_umd_c(name):
    rng = rng_for(name)
    v = vol(h=14)
    heap(v, rng, 10, [K('rock_umd_c'), K('rock_umd_c_l'), K('grit_umd_c'), K('mith_umd_c_d')], 12)
    return one(v)


def vein_umd_c(name):
    """② 壁から突き出た銀の鉱脈の塊。光は `vein_glow_umd_c`。"""
    rng = rng_for(name)
    v = vol(h=20)
    heap(v, rng, 6, [K('rock_umd_c'), K('rock_umd_c_d')], 16)
    for _ in range(5):
        x, y, z = int(rng.integers(10, 22)), int(rng.integers(10, 22)), int(rng.integers(4, 14))
        box(v, x, x + 2, y, y + 2, z, z + 3, K('mith_umd_c'))
    return one(v)


def vein_glow_umd_c(name):
    rng = rng_for(name.replace('_glow', ''))
    v = vol(h=20)
    for _ in range(3):
        x, y, z = int(rng.integers(11, 21)), int(rng.integers(11, 21)), int(rng.integers(6, 14))
        box(v, x, x + 1, y, y + 1, z, z + 2, K('lit_umd_c'))
    return one(v)


def tools_umd_c(name):
    """② 放り出された採掘道具（つるはし・槌・桶）。"""
    v = vol(h=8)
    rod(v, (5, 8, 1), (22, 20, 1), 0.8, 0.8, K('timber_umd_b_l'))
    rod(v, (18, 24, 1), (26, 14, 1), 1.0, 0.6, K('iron_umd_b'))
    disc(v, 9, 22, 3.5, 0, 6, K('timber_umd_b'), hollow=2.4)
    return one(v)


def basket_umd_c(name):
    """② 鉱石の籠（中に銀の欠片）。"""
    v = vol(h=12)
    disc(v, 16, 16, 6, 0, 10, K('timber_umd_b_l'), hollow=4.6)
    disc(v, 16, 16, 4.6, 6, 9, K('mith_umd_c_d'))
    return one(v)


def pillar_umd_c(name):
    """③ 掘り残した岩の柱（銀の筋が走る）。"""
    rng = rng_for(name)
    v = vol(h=54)
    column(v, K('rock_umd_c_d'), K('rock_umd_c'), K('rock_umd_c_l'), 7.0, 6.0, 54)
    for _ in range(3):
        z0 = int(rng.integers(6, 40))
        for z in range(z0, z0 + 10):
            ang = (z - z0) * 0.35 + rng.uniform(0, 6.28)
            x, y = int(16 + 6 * np.cos(ang)), int(16 + 6 * np.sin(ang))
            box(v, x, x + 1, y, y + 1, z, z + 1, K('mith_umd_c'))
    return one(v)


def mithril_heap_umd_c(name):
    """④ 積まれた銀の原石。"""
    rng = rng_for(name)
    v = vol(h=10)
    heap(v, rng, 9, [K('mith_umd_c'), K('mith_umd_c_d'), K('rock_umd_c_l')], 8, (2, 5))
    return one(v)


# ---- d 焼けた岩
def rubble_umd_d(name):
    rng = rng_for(name)
    v = vol(h=14)
    heap(v, rng, 9, [K('soot_umd_d'), K('soot_umd_d_l'), K('char_umd_d'), K('ash_umd_d_d')], 12)
    return one(v)


def burnt_bones_umd_d(name):
    rng = rng_for(name)
    v = vol(h=8)
    bones(v, rng, K('bone_umd_d'), K('bone_umd_d_d'), 5)
    skull(v, 18, 16, 0, K('bone_umd_d'), K('char_umd_d'))
    return one(v)


def melted_axe_umd_d(name):
    """② 溶けて曲がったドワーフの斧。"""
    v = vol(h=6)
    rod(v, (6, 12, 1), (22, 18, 1), 0.8, 0.8, K('char_umd_d'))
    box(v, 20, 27, 14, 22, 0, 2, K('iron_umd_d'))
    return one(v)


def ash_heap_umd_d(name):
    rng = rng_for(name)
    v = vol(h=8)
    heap(v, rng, 8, [K('ash_umd_d'), K('ash_umd_d_l'), K('ash_umd_d_d')], 6, (4, 9))
    return one(v)


def pillar_umd_d(name):
    """③ 焦げた柱（表面が剥げ落ちた）。"""
    rng = rng_for(name)
    v = vol(h=54)
    column(v, K('soot_umd_d_d'), K('soot_umd_d'), K('soot_umd_d_l'), 7.0, 5.8, 54)
    for _ in range(8):
        z, a = int(rng.integers(6, 46)), rng.uniform(0, 6.28)
        x, y = int(16 + 5.6 * np.cos(a)), int(16 + 5.6 * np.sin(a))
        box(v, x - 1, x + 2, y - 1, y + 2, z, z + 3, 0)
    return one(v)


def embers_umd_d(name):
    """③ 燠の山（燃え残りの梁）。光は `embers_glow_umd_d`（部屋の明るさと関係なく灯る）。"""
    rng = rng_for(name)
    v = vol(h=10)
    heap(v, rng, 6, [K('char_umd_d'), K('soot_umd_d_d')], 6, (3, 7))
    rod(v, (6, 10, 2), (26, 20, 3), 1.2, 1.2, K('char_umd_d'))
    return one(v)


def embers_glow_umd_d(name):
    rng = rng_for(name.replace('_glow', ''))
    v = vol(h=10)
    for _ in range(6):
        x, y = int(rng.integers(10, 22)), int(rng.integers(10, 22))
        box(v, x, x + 2, y, y + 2, 5, 7, K('ember_umd_d_l') if rng.random() < 0.4 else K('ember_umd_d'))
    return one(v)


# ---- e 名も無き者の地底
def slime_rock_umd_e(name):
    rng = rng_for(name)
    v = vol(h=14)
    heap(v, rng, 8, [K('rock_umd_e'), K('rock_umd_e_l'), K('slime_umd_e')], 12)
    return one(v)


def pool_umd_e(name):
    """② 壁際の冷たい水たまり。"""
    v = vol(h=2)
    disc(v, 16, 16, 7, 0, 1, K('clay_umd_e_d'))
    disc(v, 16, 16, 6, 0, 1, K('wa_umd_e'))
    disc(v, 15, 15, 3, 0, 1, K('wa_umd_e_l'))
    return one(v)


def slime_trail_umd_e(name):
    """② 床を這った跡（何かが通ったぬめり）。"""
    v = vol(h=2)
    for t in range(40):
        x = 2 + t * 0.7
        y = 16 + 5 * np.sin(t * 0.3)
        box(v, int(x), int(x) + 2, int(y), int(y) + 3, 0, 1, K('slime_umd_e_l') if t % 4 else K('slime_umd_e'))
    return one(v)


def fungus_umd_e(name):
    """② 淡く光る地底のキノコ。光は `fungus_glow_umd_e`。"""
    v = vol(h=14)
    for (x, y, hh) in ((12, 14, 8), (19, 18, 11), (15, 21, 6)):
        box(v, x, x + 2, y, y + 2, 0, hh, K('pale_umd_e_d'))
        disc(v, x + 1, y + 1, 3.0, hh, hh + 2, K('pale_umd_e'))
    return one(v)


def fungus_glow_umd_e(name):
    v = vol(h=14)
    for (x, y, hh) in ((12, 14, 8), (19, 18, 11), (15, 21, 6)):
        disc(v, x + 1, y + 1, 1.6, hh + 1, hh + 2, K('lit_umd_e'))
    return one(v)


def pillar_umd_e(name):
    """③ 濡れた石筍の柱。"""
    v = vol(h=54)
    for z in range(54):
        r = 7.0 - 3.0 * np.sin(z / 54 * np.pi)
        disc(v, 16, 16, r, z, z + 1, K('rock_umd_e') if z % 7 else K('slime_umd_e'))
    return one(v)


def bones_umd_e(name):
    rng = rng_for(name)
    v = vol(h=8)
    bones(v, rng, K('bone_umd_e'), K('pale_umd_e_d'), 4)
    return one(v)


# ---- f ドゥリンの禍の淵
def shard_umd_f(name):
    rng = rng_for(name)
    v = vol(h=20)
    for _ in range(5):
        x, y = int(rng.integers(8, 24)), int(rng.integers(8, 24))
        hh = int(rng.integers(6, 18))
        for z in range(hh):
            r = 2.6 * (1 - z / hh) + 0.6
            disc(v, x, y, r, z, z + 1, K('obs_umd_f_l') if z % 4 else K('obs_umd_f'))
    return one(v)


def slag_umd_f(name):
    rng = rng_for(name)
    v = vol(h=8)
    heap(v, rng, 8, [K('slag_umd_f'), K('obs_umd_f_d'), K('lava_umd_f_d')], 6, (3, 7))
    return one(v)


def vent_umd_f(name):
    """③ 燃える割れ目の口（黒曜の縁）。炎は `vent_flame_umd_f`（部屋の明るさと関係なく灯る）。"""
    v = vol(h=10)
    disc(v, 16, 16, 7, 0, 4, K('obs_umd_f_l'))
    disc(v, 16, 16, 4, 2, 4, K('lava_umd_f_d'))
    disc(v, 16, 16, 2.4, 3, 4, K('lava_umd_f'))
    return one(v)


def vent_flame_umd_f(name):
    return flame(24, 4, ('lava_umd_f_d', 'lava_umd_f', 'lava_umd_f_l'))


def pillar_umd_f(name):
    """③ 黒曜の尖塔（溶けて垂れた岩）。"""
    v = vol(h=54)
    for z in range(54):
        r = 7.0 * (1 - z / 60.0)
        disc(v, 16, 16, r, z, z + 1, K('obs_umd_f') if z % 9 else K('lava_umd_f_d'))
    return one(v)


# ---- 区域: 宝物庫（vt）
def chest_umd_vt(name):
    """④ 鉄で綴じた宝箱（蓋が少し開き金貨がこぼれる）。"""
    v = vol(h=14)
    box(v, 8, 24, 11, 21, 0, 9, K('timber_umd_vt'))
    for x in (8, 15, 22):
        box(v, x, x + 2, 11, 21, 0, 10, K('iron_umd_vt'))
    box(v, 8, 24, 10, 20, 9, 12, K('timber_umd_vt'))
    box(v, 10, 22, 21, 26, 0, 1, K('gold_umd_vt'))
    return one(v)


def gold_heap_umd_vt(name):
    rng = rng_for(name)
    v = vol(h=8)
    heap(v, rng, 10, [K('gold_umd_vt'), K('gold_umd_vt_d'), K('gold_umd_vt')], 6, (2, 6))
    for _ in range(3):
        x, y = int(rng.integers(10, 22)), int(rng.integers(10, 22))
        box(v, x, x + 2, y, y + 2, 4, 6, K('gem_umd_vt') if rng.random() < 0.5 else K('gem_umd_vt_r'))
    return one(v)


def pedestal_umd_vt(name):
    """③④ 宝を載せる石の台座（上に宝石の杯）。"""
    v = vol(h=24)
    box(v, 9, 23, 9, 23, 0, 3, K('rock_umd_vt'))
    box(v, 11, 21, 11, 21, 3, 15, K('rock_umd_vt_l'))
    box(v, 9, 23, 9, 23, 15, 17, K('iron_umd_vt'))
    disc(v, 16, 16, 3, 17, 21, K('gold_umd_vt'), hollow=2)
    box(v, 15, 17, 15, 17, 17, 19, K('gem_umd_vt_r'))
    return one(v)


# ---- 区域: 柱の間（ph）
def great_pillar_umd_ph(name):
    """③ 柱の間の太い彫りの柱（縦の溝と柱頭）。"""
    v = vol(h=60)
    disc(v, 16, 16, 10, 0, 3, K('rock_umd_ph_d'))
    for z in range(3, 52):
        disc(v, 16, 16, 8.4, z, z + 1, K('rock_umd_ph'))
    for a in range(12):
        ang = a * np.pi / 6
        x, y = int(16 + 8 * np.cos(ang)), int(16 + 8 * np.sin(ang))
        box(v, x, x + 1, y, y + 1, 6, 50, K('rock_umd_ph_d'))
    disc(v, 16, 16, 11, 52, 56, K('rock_umd_ph_c'))
    box(v, 3, 29, 3, 29, 56, 60, K('rock_umd_ph_l'))
    return one(v)


def statue_fallen_umd_ph(name):
    """④ 倒れたドワーフの王の像（斧を抱く）。"""
    v = vol(h=12)
    box(v, 4, 26, 11, 21, 0, 8, K('rock_umd_ph_l'))
    box(v, 24, 30, 12, 20, 0, 9, K('rock_umd_ph'))
    box(v, 4, 10, 10, 22, 0, 10, K('rock_umd_ph_c'))
    rod(v, (8, 16, 8), (24, 16, 8), 0.8, 0.8, K('rock_umd_ph_d'))
    return one(v)


def brazier_cold_umd_ph(name):
    """④ 冷えた大火鉢（脚つきの石の鉢）。"""
    v = vol(h=20)
    for (x, y) in ((10, 10), (20, 10), (15, 20)):
        box(v, x, x + 3, y, y + 3, 0, 10, K('rock_umd_ph_d'))
    disc(v, 16, 15, 9, 10, 15, K('rock_umd_ph'), hollow=7)
    disc(v, 16, 15, 7, 10, 11, K('char_umd_fr'))
    return one(v)


# ---- 区域: 崩れた坑道（mz）
def cavein_umd_mz(name):
    rng = rng_for(name)
    v = vol(h=22)
    heap(v, rng, 14, [K('rock_umd_mz'), K('rock_umd_mz_l'), K('rock_umd_mz_d')], 20, (4, 10))
    rod(v, (3, 6, 6), (28, 26, 16), 1.3, 1.3, K('timber_umd_mz'))
    rod(v, (26, 4, 3), (6, 24, 10), 1.1, 1.1, K('timber_umd_mz_d'))
    return one(v)


def broken_timber_umd_mz(name):
    v = vol(h=8)
    rod(v, (4, 12, 2), (26, 18, 2), 1.3, 1.3, K('timber_umd_mz'))
    rod(v, (12, 4, 1), (16, 28, 4), 1.1, 1.1, K('timber_umd_mz_d'))
    return one(v)


def rocks_umd_mz(name):
    rng = rng_for(name)
    v = vol(h=10)
    heap(v, rng, 7, [K('rock_umd_mz'), K('rock_umd_mz_l'), K('rock_umd_mz_d')], 8, (3, 7))
    return one(v)


# ---- 区域: ドワーフの居室（fr）
def stone_bed_umd_fr(name):
    """④ 石の寝台（毛皮が掛かる）。"""
    v = vol(h=12)
    box(v, 5, 27, 9, 23, 0, 7, K('rock_umd_fr'))
    box(v, 5, 27, 9, 23, 7, 8, K('rock_umd_fr_d'))
    box(v, 9, 25, 10, 22, 8, 10, K('fur_umd_fr'))
    box(v, 5, 10, 10, 22, 8, 11, K('cloth_umd_fr'))
    return one(v)


def hearth_umd_fr(name):
    """④ 暖炉の跡（石組みと灰）。"""
    v = vol(h=20)
    box(v, 6, 26, 8, 14, 0, 18, K('rock_umd_fr_d'))
    box(v, 6, 9, 14, 22, 0, 14, K('rock_umd_fr'))
    box(v, 23, 26, 14, 22, 0, 14, K('rock_umd_fr'))
    box(v, 6, 26, 14, 22, 12, 15, K('rock_umd_fr'))
    box(v, 9, 23, 14, 22, 0, 1, K('char_umd_fr'))
    return one(v)


def stool_umd_fr(name):
    """④ 倒れた木の腰掛けと卓。"""
    v = vol(h=14)
    box(v, 6, 20, 6, 20, 10, 12, K('timber_umd_fr'))
    for (x, y) in ((7, 7), (17, 7), (7, 17), (17, 17)):
        box(v, x, x + 2, y, y + 2, 0, 10, K('timber_umd_fr'))
    box(v, 20, 28, 20, 26, 0, 3, K('timber_umd_fr'))
    return one(v)


# ------------------------------------------------------------------ 登録
PROPS = {
    # a
    'rubble_umd_a': (rubble_umd_a, '館の名残の崩れた彫りの石'),
    'candelabrum_umd_a': (candelabrum_umd_a, '消えた真鍮の燭台'),
    'statue_shard_umd_a': (statue_shard_umd_a, '倒れたドワーフの像の頭'),
    'rune_slab_umd_a': (rune_slab_umd_a, '外れた碑の板'),
    'pillar_umd_a': (pillar_umd_a, '彫りのある角柱'),
    'bench_umd_a': (bench_umd_a, '石の長椅子'),
    'urn_umd_a': (urn_umd_a, '倒れた真鍮の壺'),
    'banner_umd_a': (banner_umd_a, '落ちて朽ちた旗'),
    # b
    'cavein_umd_b': (cavein_umd_b, '落盤と折れた坑木'),
    'pickaxe_umd_b': (pickaxe_umd_b, 'つるはし'),
    'timber_post_umd_b': (timber_post_umd_b, '壁際の坑木の支え'),
    'ore_pile_umd_b': (ore_pile_umd_b, '鉱石の積み'),
    'timber_frame_umd_b': (timber_frame_umd_b, '坑木の門形の枠'),
    'lantern_umd_b': (lantern_umd_b, '坑夫のランタン'),
    'lantern_glow_umd_b': (lantern_glow_umd_b, 'ランタンの灯'),
    'minecart_umd_b': (minecart_umd_b, 'トロッコの残骸'),
    'bucket_umd_b': (bucket_umd_b, '桶'),
    # c
    'rubble_umd_c': (rubble_umd_c, '銀の欠片の混じる砕石'),
    'vein_umd_c': (vein_umd_c, '銀の鉱脈の塊'),
    'vein_glow_umd_c': (vein_glow_umd_c, '鉱脈の銀のきらめき'),
    'tools_umd_c': (tools_umd_c, '放り出された採掘道具'),
    'basket_umd_c': (basket_umd_c, '鉱石の籠'),
    'pillar_umd_c': (pillar_umd_c, '掘り残した岩の柱（銀の筋）'),
    'mithril_heap_umd_c': (mithril_heap_umd_c, '積まれた銀の原石'),
    # d
    'rubble_umd_d': (rubble_umd_d, '焼けた瓦礫'),
    'burnt_bones_umd_d': (burnt_bones_umd_d, '焼けた骨'),
    'melted_axe_umd_d': (melted_axe_umd_d, '溶けて曲がった斧'),
    'ash_heap_umd_d': (ash_heap_umd_d, '灰の山'),
    'pillar_umd_d': (pillar_umd_d, '焦げた柱'),
    'embers_umd_d': (embers_umd_d, '燠の山'),
    'embers_glow_umd_d': (embers_glow_umd_d, '燠の火'),
    # e
    'slime_rock_umd_e': (slime_rock_umd_e, 'ぬめる岩の山'),
    'pool_umd_e': (pool_umd_e, '冷たい水たまり'),
    'slime_trail_umd_e': (slime_trail_umd_e, '這った跡のぬめり'),
    'fungus_umd_e': (fungus_umd_e, '地底のキノコ'),
    'fungus_glow_umd_e': (fungus_glow_umd_e, 'キノコの淡い光'),
    'pillar_umd_e': (pillar_umd_e, '濡れた石筍の柱'),
    'bones_umd_e': (bones_umd_e, '白い骨'),
    # f
    'shard_umd_f': (shard_umd_f, '黒曜の破片の山'),
    'slag_umd_f': (slag_umd_f, '溶けた岩の滓'),
    'vent_umd_f': (vent_umd_f, '燃える割れ目の口'),
    'vent_flame_umd_f': (vent_flame_umd_f, '割れ目の炎'),
    'pillar_umd_f': (pillar_umd_f, '黒曜の尖塔'),
    # 区域
    'chest_umd_vt': (chest_umd_vt, '鉄で綴じた宝箱'),
    'gold_heap_umd_vt': (gold_heap_umd_vt, '金貨と宝石の山'),
    'pedestal_umd_vt': (pedestal_umd_vt, '宝の台座'),
    'great_pillar_umd_ph': (great_pillar_umd_ph, '柱の間の太い彫りの柱'),
    'statue_fallen_umd_ph': (statue_fallen_umd_ph, '倒れたドワーフの王の像'),
    'brazier_cold_umd_ph': (brazier_cold_umd_ph, '冷えた大火鉢'),
    'cavein_umd_mz': (cavein_umd_mz, '崩れた坑道の落盤'),
    'broken_timber_umd_mz': (broken_timber_umd_mz, '折れた坑木'),
    'rocks_umd_mz': (rocks_umd_mz, '落ちた岩'),
    'stone_bed_umd_fr': (stone_bed_umd_fr, '石の寝台'),
    'hearth_umd_fr': (hearth_umd_fr, '暖炉の跡'),
    'stool_umd_fr': (stool_umd_fr, '倒れた腰掛けと卓'),
}


def names():
    out = []
    for b in BANDS:
        out += ['floor_umd_%s_%02d' % (b, i) for i in range(1, FLOOR_VARIANTS + 1)]
        out += ['wall_umd_%s_%02d' % (b, i) for i in range(1, WALL_VARIANTS + 1)]
        out += ['bedrock_umd_%s_%02d' % (b, i) for i in range(1, BEDROCK_VARIANTS + 1)]
    for z in ZONES:
        out += ['wall_umd_%s_%02d' % (z, i) for i in range(1, WALL_VARIANTS + 1)]
    return out + list(PROPS)


def _load_angband():
    spec = importlib.util.spec_from_file_location('angband_dungeon_prefabs_umd', os.path.join(HERE, 'angband_dungeon_prefabs.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def register(g):
    """`gen_prefabs.py` の名前空間 `g` へ Umoria のダンジョンの素材を足す。"""
    global G, C, V, FLOOR_H, WALL_H, A
    G = g
    C = g['C']
    V = g['V']
    FLOOR_H = g['FLOOR_H']
    WALL_H = g['WALL_H']
    A = _load_angband()
    A.G, A.C, A.V, A.FLOOR_H, A.WALL_H = g, C, V, FLOOR_H, WALL_H
    _palette()
    cat = g['CATALOG']
    static = g['static_part']
    for b in BANDS:
        for i in range(1, FLOOR_VARIANTS + 1):
            cat['floor_umd_%s_%02d' % (b, i)] = (floor_umd, static(), 'Umoria のダンジョンの床（帯 %s）' % b)
        for i in range(1, WALL_VARIANTS + 1):
            cat['wall_umd_%s_%02d' % (b, i)] = (wall_umd, static(), 'Umoria のダンジョンの壁（帯 %s）' % b)
        for i in range(1, BEDROCK_VARIANTS + 1):
            cat['bedrock_umd_%s_%02d' % (b, i)] = (bedrock_umd, static(), 'Umoria のダンジョンの岩盤（帯 %s）' % b)
    for z in ZONES:
        for i in range(1, WALL_VARIANTS + 1):
            cat['wall_umd_%s_%02d' % (z, i)] = (wall_umd, static(), 'Umoria の部屋の種類の壁（%s）' % z)
    for stem, (fn, note) in PROPS.items():
        cat[stem] = (fn, static(), note)
