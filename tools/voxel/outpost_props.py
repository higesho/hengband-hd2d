# -*- coding: utf-8 -*-
"""辺境の地の小物。

`outpost_prefabs.register` から呼ばれる。形の道具（`box` `disc` `log_x` …）は
あちらの名前空間 `H` を借りる。**小物は 1 マスに収める**（接地する部分は z=0 の
マスの中。上のほうがはみ出すのは可）。火の相方（`*_fire_out`）は別体で、置く側が
自発光をつけて同じ位置へ重ねる（罠 39）。
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


# ------------------------------------------------------------------ 共通の小物
def well_out(name):
    """井戸 — 野石の井筒に、丸太の櫓と滑車、吊るした桶。"""
    v = vol(46)
    rng = _rng(name)
    for z in range(0, 12, 3):
        ang0 = rng.random() * 6.28
        _disc(v, 16, 16, 9.0, z, z + 3, K('rub'), hollow=6.0)
        # 石の継ぎ目（輪を扇形に割る）
        for k in range(6):
            a = ang0 + (k * 1.047)
            x, y = int(16 + 7.5 * np.cos(a)), int(16 + 7.5 * np.sin(a))
            _box(v, x, x + 1, y, y + 1, z, z + 3, K('rub_d'))
    _disc(v, 16, 16, 9.0, 11, 12, K('rub_l'), hollow=6.0)
    _disc(v, 16, 16, 6.0, 0, 2, K('wa_dd'))
    for x in (7, 24):                                        # 櫓の柱
        _box(v, x, x + 3, 15, 18, 0, 38, K('timber'))
    _log_x(v, 16, 39, 2.0, 5, 27, K('log_l'))                # 桁
    _box(v, 4, 28, 12, 20, 41, 43, K('board_d'))             # 小屋根
    _box(v, 6, 26, 13, 19, 43, 45, K('board'))
    _disc(v, 16, 16, 2.5, 36, 39, K('iron'))                 # 滑車
    _box(v, 15, 17, 15, 17, 22, 36, K('lash'))               # 縄
    _box(v, 13, 19, 13, 19, 18, 22, K('oak_l'))              # 桶
    _box(v, 14, 18, 14, 18, 21, 22, K('wa_d'))
    return one(v)


def cart_out(name):
    """荷車 — 幌無しの二輪。荷台に袋と樽。梶棒は地面に下ろしてある。"""
    v = vol(20)
    _box(v, 6, 26, 9, 23, 7, 10, K('oak'))                   # 荷台
    _box(v, 6, 26, 9, 10, 10, 14, K('oak_l'))                # あおり
    _box(v, 6, 26, 22, 23, 10, 14, K('oak_l'))
    _box(v, 6, 7, 9, 23, 10, 14, K('oak_l'))
    for cy in (7, 24):                                       # 車輪（輻つき）
        _log_y(v, 12, 6, 6.0, cy, cy + 2, K('timber'))
        _log_y(v, 12, 6, 4.4, cy, cy + 2, 0)
        _box(v, 11, 13, cy, cy + 2, 0, 12, K('timber_d'))
        _box(v, 6, 18, cy, cy + 2, 5, 7, K('timber_d'))
        _log_y(v, 12, 6, 1.5, cy - 1, cy + 3, K('iron'))
    _log_x(v, 16, 6, 1.2, 8, 24, K('iron_l'))                # 車軸
    _box(v, 25, 31, 12, 14, 1, 3, K('timber_l'))             # 梶棒（下ろしてある）
    _box(v, 25, 31, 18, 20, 1, 3, K('timber_l'))
    _box(v, 9, 15, 12, 20, 10, 15, K('cloth_d'))             # 袋
    _box(v, 10, 14, 13, 19, 15, 16, K('cloth'))
    _disc(v, 20, 16, 3.5, 10, 18, K('timber_l'))             # 樽
    _disc(v, 20, 16, 3.5, 12, 13, K('iron'))
    _disc(v, 20, 16, 3.5, 16, 17, K('iron'))
    return one(v)


def haystack_out(name):
    """干し草の山 — 芯棒に積んだ円錐。段ごとに色を変え、足元に散らばり。"""
    rng = _rng(name)
    v = vol(22)
    for z in range(20):
        r = 12.0 * (1.0 - (z / 21.0) ** 1.6)
        _disc(v, 16, 16, r, z, z + 1, K('hay') if ((z // 3) % 2 == 0) else K('hay_d'))
    _box(v, 15, 17, 15, 17, 0, 22, K('timber'))
    for _ in range(4):
        x, y = int(rng.integers(2, 26)), int(rng.integers(2, 26))
        _box(v, x, x + 4, y, y + 2, 0, 1, K('straw_d'))
    return one(v)


def barrel_out(name):
    """樽（既存の樽と同じ作り。中身の見える開いた樽もある）。"""
    rng = _rng(name)
    v = vol(16)
    for z in range(15):
        bulge = 1.0 - (((z - 7.5) / 7.5) ** 2) * 0.18
        _disc(v, 16, 16, 6.6 * bulge, z, z + 1, K('timber') if (z % 4) else K('timber_l'))
    for z in (2, 12):
        _disc(v, 16, 16, 6.9, z, z + 2, K('iron'), hollow=5.6)
    if rng.random() < 0.5:
        _disc(v, 16, 16, 5.6, 14, 15, K('wa_d'))             # 水（雨水）
    else:
        _disc(v, 16, 16, 5.6, 14, 16, K('timber_d'))         # 蓋
    return one(v)


def crate_out(name):
    """木箱 — 2 つ重ね。上の箱は少しずれる。"""
    v = vol(22)
    _box(v, 6, 22, 8, 24, 0, 12, K('board'))
    for z in (0, 5, 10):
        _box(v, 6, 22, 8, 24, z, z + 1, K('board_d'))
    _box(v, 6, 7, 8, 24, 0, 12, K('timber_d'))
    _box(v, 21, 22, 8, 24, 0, 12, K('timber_d'))
    _box(v, 10, 24, 10, 22, 12, 21, K('board_l'))
    _box(v, 10, 24, 10, 22, 16, 17, K('board_d'))
    _box(v, 10, 11, 10, 22, 12, 21, K('timber_d'))
    _box(v, 23, 24, 10, 22, 12, 21, K('timber_d'))
    return one(v)


def sack_out(name):
    """麻袋 — 口を縛った袋 3 つ。"""
    v = vol(14)
    for (cx, cy, r) in ((11, 12, 6), (21, 18, 5.5), (13, 22, 5)):
        _ball(v, cx, cy, 5, r, K('cloth_d'), squash=0.9)
        _box(v, cx - 2, cx + 2, cy - 2, cy + 2, 9, 12, K('cloth'))
        _box(v, cx - 3, cx + 3, cy - 3, cy + 3, 10, 11, K('lash'))
    v[:, :, 0][v[:, :, 0] == 0] = 0
    return one(v)


def bench_out(name):
    """腰掛け — 割った丸太の半割りを 2 本の杭に載せる。"""
    v = vol(12)
    _log_x(v, 16, 9, 3.0, 4, 28, K('log_l'))
    _box(v, 4, 28, 12, 20, 9, 12, 0)                         # 上を平らに（半割り）
    _box(v, 4, 28, 13, 19, 8, 9, K('log_end'))
    for x in (7, 23):
        _box(v, x, x + 3, 14, 18, 0, 8, K('timber'))
    return one(v)


def woodpile_out(name):
    """薪の山 — 割った薪を 2 本の杭の間に積む。木口を手前（南）へ。"""
    rng = _rng(name)
    v = vol(18)
    for x in (3, 27):
        _box(v, x, x + 2, 8, 24, 0, 18, K('timber'))
    for z in range(0, 15, 3):
        p = 5
        while p < 27:
            w = int(rng.integers(3, 5))
            _box(v, p, p + w, 8, 24, z, z + 3, K('oak'))
            _box(v, p, p + w, 23, 24, z, z + 3, K('log_end'))
            _box(v, p + w, p + w + 1, 8, 24, z, z + 3, K('oak_l'))
            p += w + 1
    return one(v)


def chopping_block_out(name):
    """薪割り台 — 太い切り株に斧が刺さっている。脇に割った薪。"""
    v = vol(22)
    _disc(v, 14, 16, 7.0, 0, 11, K('bark'))
    _disc(v, 14, 16, 6.0, 10, 11, K('log_end'))
    _box(v, 13, 15, 14, 18, 11, 13, K('steel'))              # 斧の刃
    _box(v, 14, 15, 15, 17, 12, 22, K('timber_d'))           # 柄（斜め）
    for k in range(9):
        _box(v, 15 + k, 16 + k, 15, 17, 13 + k, 15 + k, K('timber_d'))
    _log_x(v, 26, 2, 2.0, 4, 14, K('oak'))                   # 割った薪
    _log_x(v, 29, 2, 1.8, 6, 16, K('oak_l'))
    return one(v)


def bucket_out(name):
    """桶 — 2 つ。1 つは倒れている。"""
    v = vol(10)
    _disc(v, 11, 12, 4.5, 0, 8, K('oak_l'))
    _disc(v, 11, 12, 3.5, 2, 8, K('wa_d'))
    _disc(v, 11, 12, 4.8, 3, 4, K('iron'), hollow=4.2)
    _disc(v, 11, 12, 4.8, 7, 8, K('iron'), hollow=4.2)
    _log_y(v, 22, 4, 4.0, 14, 24, K('oak'))                  # 倒れた桶
    _log_y(v, 22, 4, 3.0, 15, 23, 0)
    return one(v)


def rain_barrel_out(name):
    """雨水の樽 — 壁際に置く大きめの樽。縁に苔、上に雨樋の板。"""
    v = vol(24)
    for z in range(18):
        bulge = 1.0 - (((z - 9) / 9.0) ** 2) * 0.15
        _disc(v, 16, 18, 8.0 * bulge, z, z + 1, K('timber_d') if (z % 5) else K('timber'))
    for z in (3, 14):
        _disc(v, 16, 18, 8.3, z, z + 2, K('iron'), hollow=7.0)
    _disc(v, 16, 18, 7.0, 17, 18, K('wa_d'))
    _box(v, 8, 12, 12, 20, 12, 15, K('moss_d'))
    _box(v, 14, 18, 2, 20, 20, 23, K('board_d'))             # 雨樋
    _box(v, 15, 17, 2, 20, 21, 23, K('board'))
    return one(v)


def lantern_post_out(name):
    """木の柱に掛けたランタン。火は `lantern_flame_out`。"""
    v = vol(40)
    _box(v, 14, 18, 14, 18, 0, 3, K('rub_d'))
    _box(v, 15, 18, 15, 18, 3, 34, K('timber'))
    _box(v, 10, 18, 15, 17, 30, 32, K('timber_d'))           # 腕木
    _box(v, 10, 12, 15, 17, 24, 30, K('iron'))               # 吊り
    _box(v, 8, 14, 13, 19, 16, 18, K('out_iron_k'))          # ランタン
    _box(v, 8, 14, 13, 19, 23, 25, K('out_iron_k'))
    for (x0, x1) in ((8, 10), (12, 14)):
        for (y0, y1) in ((13, 15), (17, 19)):
            _box(v, x0, x1, y0, y1, 18, 23, K('out_iron_k'))
    return one(v)


def lantern_flame_out(name):
    v = vol(40)
    _box(v, 10, 12, 15, 17, 18, 22, K('fl_mid'))
    _box(v, 10, 12, 15, 17, 19, 21, K('fl_core'))
    return one(v)


# ------------------------------------------------------------------ 草地
def shrub_out(name):
    """低木 — 葉の塊を 3〜5 つ重ねる。幹は見えない。"""
    rng = _rng(name)
    v = vol(20)
    shades = [K('leaf_d'), K('leaf_m'), K('leaf_l')]
    for _ in range(int(rng.integers(3, 6))):
        cx, cy = int(rng.integers(10, 22)), int(rng.integers(10, 22))
        r = float(rng.uniform(4.5, 7.0))
        _ball(v, cx, cy, int(rng.integers(4, 8)), r, shades[int(rng.integers(0, 3))], squash=0.75)
    v[:, :, 0] = 0
    _box(v, 14, 18, 14, 18, 0, 4, K('bark'))
    if int(name[-2:]) == 3:                                  # 実の付いた木
        for _ in range(8):
            x, y = int(rng.integers(8, 24)), int(rng.integers(8, 24))
            z = int(v[x, y, :].nonzero()[0].max()) if v[x, y, :].any() else 6
            v[x, y, z] = K('fl_r')
    return one(v)


def flowers_out(name):
    """花の群れ — 茎の先に 2×2 の花。色は名前で決める。"""
    rng = _rng(name)
    v = vol(12)
    colour = [K('fl_w'), K('fl_y'), K('out_fl_p')][(int(name[-2:]) - 1) % 3]
    for _ in range(int(rng.integers(6, 11))):
        x, y = int(rng.integers(6, 24)), int(rng.integers(6, 24))
        h = int(rng.integers(4, 9))
        _box(v, x, x + 1, y, y + 1, 0, h, K('stem'))
        _box(v, x - 1, x + 2, y - 1, y + 2, h, h + 1, colour)
        _box(v, x, x + 1, y, y + 1, h, h + 1, K('fl_y') if colour != K('fl_y') else K('fl_w'))
        _box(v, x + 1, x + 3, y, y + 1, 1, 2, K('turf'))
    return one(v)


def stump_out(name):
    """切り株 — 根を張り、上面に年輪。脇にきのこ。"""
    v = vol(10)
    _disc(v, 15, 16, 7.5, 0, 8, K('bark'))
    for (dx, dy) in ((8, 0), (-8, 0), (0, 8), (0, -8)):
        _box(v, 15 + dx - 2, 15 + dx + 2, 16 + dy - 2, 16 + dy + 2, 0, 3, K('bark_l'))
    _disc(v, 15, 16, 6.5, 7, 8, K('log_end'))
    _disc(v, 15, 16, 4.0, 7, 8, K('log_end'), hollow=3.0)
    _disc(v, 15, 16, 1.5, 7, 8, K('log_d'))
    _box(v, 24, 26, 20, 22, 0, 3, K('stem'))
    _box(v, 23, 27, 19, 23, 3, 4, K('mbr'))
    return one(v)


def boulder_out(name):
    """岩 — 潰した球を 2 つ、苔と地衣。"""
    rng = _rng(name)
    v = vol(16)
    _ball(v, 15, 16, 4, 9.0 if name.endswith('01') else 7.0, K('rock'), squash=0.6)
    _ball(v, 21, 12, 3, 5.0, K('rock_l'), squash=0.7)
    v[:, :, 0][v[:, :, 0] == 0] = 0
    for _ in range(int(rng.integers(2, 4))):
        x, y = int(rng.integers(8, 22)), int(rng.integers(8, 22))
        for xx in range(x, x + 3):
            for yy in range(y, y + 3):
                col = v[xx, yy, :]
                nz = col.nonzero()[0]
                if len(nz):
                    col[nz.max()] = K('moss') if rng.random() < 0.6 else K('lichen')
    return one(v)


def mushrooms_out(name):
    """きのこの群れ（茶）。"""
    rng = _rng(name)
    v = vol(8)
    for _ in range(int(rng.integers(3, 6))):
        x, y = int(rng.integers(8, 24)), int(rng.integers(8, 24))
        h = int(rng.integers(2, 5))
        _box(v, x, x + 2, y, y + 2, 0, h, K('stem'))
        _disc(v, x + 1, y + 1, 2.5, h, h + 2, K('mbr'))
        _disc(v, x + 1, y + 1, 1.0, h + 1, h + 2, K('mbr_d'))
    return one(v)


# ------------------------------------------------------------------ 雑貨屋
def goods_stall_out(name):
    """店先の売り台 — 板の台に壺・籠・布。上に日除けの布。"""
    v = vol(34)
    _box(v, 3, 29, 8, 24, 10, 13, K('board'))
    for x in (4, 26):
        _box(v, x, x + 2, 9, 23, 0, 10, K('timber'))
    _disc(v, 8, 14, 3.0, 13, 19, K('clay'))                  # 壺
    _disc(v, 8, 14, 2.0, 18, 19, K('clay_d'))
    _box(v, 14, 22, 11, 19, 13, 17, K('hay_d'))              # 籠
    _box(v, 15, 21, 12, 18, 16, 17, K('fl_r'))               # 中身（林檎）
    _box(v, 23, 28, 10, 20, 13, 16, K('out_cloth_b'))         # 畳んだ布
    _box(v, 23, 28, 10, 20, 16, 18, K('out_cloth_r'))
    for x in (3, 27):
        _box(v, x, x + 2, 22, 24, 13, 30, K('timber_d'))
    _box(v, 2, 30, 4, 26, 30, 32, K('out_cloth_ws'))          # 日除け（布）
    for x in range(2, 30, 6):
        _box(v, x, x + 3, 4, 26, 30, 32, K('out_cloth_r'))
    return one(v)


def rope_coil_out(name):
    """縄の輪と束。"""
    v = vol(8)
    _disc(v, 12, 14, 8.0, 0, 3, K('lash'), hollow=5.0)
    _disc(v, 12, 14, 7.0, 3, 5, K('lash'), hollow=5.5)
    _log_x(v, 24, 2, 2.2, 6, 26, K('lash'))
    return one(v)


# ------------------------------------------------------------------ 防具屋
def armour_stand_out(name):
    """鎧の立て台 — 十字の台に胸当てと兜。"""
    v = vol(40)
    _box(v, 12, 20, 12, 20, 0, 2, K('timber_d'))
    _box(v, 15, 17, 15, 17, 2, 32, K('timber'))
    _box(v, 6, 26, 15, 17, 24, 26, K('timber'))              # 肩木
    _box(v, 10, 22, 12, 20, 12, 26, K('steel_d'))            # 胸当て
    _box(v, 11, 21, 19, 21, 14, 24, K('steel'))
    _box(v, 13, 19, 19, 21, 17, 19, K('iron'))               # 帯金
    _ball(v, 16, 16, 30, 4.5, K('steel'))                    # 兜
    _box(v, 13, 19, 18, 21, 27, 30, K('steel_d'))
    _box(v, 15, 17, 20, 22, 26, 31, K('out_cloth_r'))         # 前立ての飾り
    return one(v)


def shield_rack_out(name):
    """盾の掛け台 — 木の枠に丸盾 2 枚と凧盾 1 枚。"""
    v = vol(30)
    _box(v, 2, 30, 14, 17, 0, 3, K('timber_d'))
    for x in (3, 27):
        _box(v, x, x + 2, 14, 17, 0, 28, K('timber'))
    _box(v, 3, 29, 14, 17, 26, 28, K('timber'))
    _log_y(v, 9, 16, 6.0, 17, 19, K('oak'))                  # 丸盾
    _log_y(v, 9, 16, 2.0, 18, 20, K('iron'))
    _log_y(v, 21, 15, 5.0, 17, 19, K('out_cloth_b'))          # 丸盾（藍）
    _log_y(v, 21, 15, 5.0, 18, 19, K('iron_l'))
    _log_y(v, 21, 15, 3.6, 18, 20, K('out_cloth_b'))
    _box(v, 13, 19, 17, 19, 6, 22, K('steel_d'))             # 凧盾
    _box(v, 14, 18, 18, 20, 8, 20, K('out_cloth_r'))
    _box(v, 15, 17, 18, 20, 8, 20, K('gold'))
    return one(v)


def helm_post_out(name):
    """兜掛け — 杭の先に兜、脇に籠手。"""
    v = vol(34)
    _box(v, 12, 20, 12, 20, 0, 2, K('rub_d'))
    _box(v, 15, 17, 15, 17, 2, 26, K('timber'))
    _ball(v, 16, 16, 28, 4.5, K('iron_l'))
    _box(v, 12, 20, 15, 18, 25, 28, K('iron'))               # 目庇
    _box(v, 15, 17, 18, 21, 26, 31, K('steel'))              # 鼻当て
    _box(v, 22, 28, 10, 22, 0, 4, K('fur_d'))                # 籠手（皮）
    _box(v, 23, 27, 11, 21, 4, 6, K('steel_d'))
    return one(v)


def hide_frame_out(name):
    """皮の張り枠 — 木の枠に張った獣皮（防具屋も猟師も使う）。"""
    v = vol(30)
    for x in (3, 27):
        _box(v, x, x + 2, 15, 17, 0, 28, K('timber'))
    _box(v, 3, 29, 15, 17, 26, 28, K('timber'))
    _box(v, 3, 29, 15, 17, 2, 4, K('timber'))
    _box(v, 6, 26, 15, 17, 5, 25, K('fur'))                  # 皮
    _box(v, 8, 24, 15, 17, 7, 23, K('fur_l'))
    for z in range(6, 26, 4):                                # 縄で枠へ結ぶ
        _box(v, 5, 7, 15, 17, z, z + 1, K('lash'))
        _box(v, 25, 27, 15, 17, z, z + 1, K('lash'))
    return one(v)


# ------------------------------------------------------------------ 武器屋
def forge_out(name):
    """鍛冶の炉 — 野石の炉に鞴（ふいご）と石の煙突。火は `forge_fire_out`。"""
    rng = _rng(name)
    v = vol(44)
    _box(v, 2, 30, 6, 30, 0, 14, K('rub_d'))                 # 炉体
    for z in range(1, 13, 3):
        p = 2
        while p < 30:
            w = int(rng.integers(4, 8))
            shade = int(rng.choice([K('rub'), K('rub_l'), K('rub_d')], p=[0.5, 0.28, 0.22]))
            _box(v, p, p + w, 29, 30, z, z + 2, shade)
            _box(v, p, p + w, 6, 7, z, z + 2, shade)
            _box(v, 2, 3, 6 + p, 6 + p + w, z, z + 2, shade)
            _box(v, 29, 30, 6 + p, 6 + p + w, z, z + 2, shade)
            p += w + 1
    _box(v, 6, 26, 10, 26, 14, 16, K('soot'))                # 火床（炭）
    _box(v, 8, 24, 12, 24, 15, 17, K('char'))
    _box(v, 6, 26, 6, 9, 14, 20, K('rub_l'))                 # 前の火屋の縁
    _box(v, 10, 22, 22, 30, 14, 40, K('rub_d'))              # 煙突（後ろ）
    for z in range(16, 38, 3):
        _box(v, 10, 22, 22, 23, z, z + 2, K('rub') if (z // 3) % 2 else K('rub_l'))
    _box(v, 9, 23, 21, 31, 40, 42, K('rub_l'))
    _box(v, 12, 20, 24, 28, 40, 44, K('soot'))
    _box(v, 26, 32, 12, 22, 14, 20, K('fur_d'))              # 鞴（皮の袋）
    _box(v, 26, 32, 13, 21, 20, 22, K('board'))
    _box(v, 30, 32, 16, 18, 22, 30, K('timber_d'))           # 鞴の柄
    return one(v)


def forge_fire_out(name):
    v = vol(44)
    _box(v, 10, 22, 13, 23, 16, 19, K('fl_mid'))
    _box(v, 13, 19, 15, 21, 17, 21, K('fl_core'))
    _box(v, 8, 24, 12, 24, 16, 17, K('emb_l'))
    return one(v)


def anvil_out(name):
    """金床 — 切り株に載せた鉄の金床。脇に金槌。"""
    v = vol(24)
    _disc(v, 14, 16, 6.5, 0, 9, K('bark'))
    _disc(v, 14, 16, 5.5, 8, 9, K('log_end'))
    _box(v, 9, 19, 12, 20, 9, 12, K('iron'))                 # 台
    _box(v, 10, 18, 13, 19, 12, 15, K('iron_l'))
    _box(v, 6, 24, 13, 19, 15, 18, K('steel_d'))             # 面
    _box(v, 6, 24, 13, 19, 17, 18, K('steel'))
    _box(v, 24, 30, 15, 17, 16, 18, K('steel_d'))            # 角（ホーン）
    _box(v, 28, 31, 15, 17, 16, 17, K('steel_d'))
    _box(v, 24, 26, 24, 26, 0, 12, K('timber_d'))            # 金槌（立て掛け）
    _box(v, 22, 28, 22, 28, 12, 15, K('iron'))
    return one(v)


def quench_out(name):
    """水桶 — 焼き入れの桶。湯気の代わりに水面を明るく。脇にやっとこ。"""
    v = vol(16)
    for z in range(13):
        _disc(v, 15, 16, 7.5, z, z + 1, K('oak_l') if (z % 4) else K('oak'))
    for z in (2, 10):
        _disc(v, 15, 16, 7.8, z, z + 2, K('iron'), hollow=6.6)
    _disc(v, 15, 16, 6.6, 11, 13, K('wa'))
    _disc(v, 15, 16, 2.5, 12, 13, K('wa_l'))
    _box(v, 22, 30, 14, 16, 13, 15, K('iron'))               # やっとこ
    _box(v, 28, 30, 13, 17, 13, 15, K('iron_l'))
    return one(v)


def weapon_rack_out(name):
    """武器立て — 板の背に剣 3 本と斧、槍。"""
    v = vol(40)
    _box(v, 2, 30, 18, 21, 0, 3, K('timber_d'))
    _box(v, 2, 30, 18, 20, 3, 30, K('board'))
    _box(v, 2, 30, 18, 20, 29, 32, K('timber'))
    for x in (6, 12, 18):                                    # 剣
        _box(v, x, x + 1, 20, 21, 4, 22, K('steel'))
        _box(v, x - 2, x + 3, 20, 21, 20, 22, K('gold_d'))
        _box(v, x, x + 1, 20, 21, 22, 26, K('timber_d'))
    _box(v, 24, 25, 20, 21, 4, 34, K('timber_d'))            # 斧
    _box(v, 21, 28, 20, 22, 28, 34, K('steel_d'))
    _box(v, 28, 29, 21, 22, 4, 40, K('timber_d'))            # 槍
    _box(v, 27, 30, 21, 22, 36, 40, K('steel'))
    return one(v)


def coal_heap_out(name):
    """炭の山 — 黒い塊の山と、脇の籠。"""
    rng = _rng(name)
    v = vol(12)
    for z in range(9):
        _disc(v, 13, 16, 9.5 * (1.0 - z / 10.0), z, z + 1, K('char'))
    for _ in range(12):
        x, y = int(rng.integers(5, 22)), int(rng.integers(8, 24))
        col = v[x, y, :]
        nz = col.nonzero()[0]
        if len(nz):
            col[nz.max()] = K('soot')
    _box(v, 22, 30, 10, 22, 0, 8, K('hay_d'))                # 籠
    _box(v, 23, 29, 11, 21, 6, 9, K('char'))
    return one(v)


def grindstone_out(name):
    """砥石 — 木の架台に円い砥石、踏み板と水受け。"""
    v = vol(20)
    for x in (6, 22):
        _box(v, x, x + 3, 10, 22, 0, 12, K('timber'))
    _box(v, 6, 25, 10, 22, 0, 2, K('timber_d'))
    _log_x(v, 16, 12, 6.5, 12, 18, K('rock_l'))              # 砥石
    _log_x(v, 16, 12, 1.5, 4, 26, K('iron'))                 # 軸
    _box(v, 26, 30, 14, 18, 0, 4, K('oak'))                  # 水受け
    _box(v, 27, 29, 15, 17, 3, 4, K('wa_d'))
    _box(v, 8, 20, 20, 26, 2, 4, K('board'))                 # 踏み板
    return one(v)


# ------------------------------------------------------------------ 寺院
def shrine_out(name):
    """小さな祠 — 石の台に木の厨子、中に金の像。屋根は板。"""
    v = vol(44)
    _box(v, 4, 28, 6, 28, 0, 6, K('rub'))
    _box(v, 5, 27, 7, 27, 5, 6, K('rub_l'))
    _box(v, 8, 24, 10, 24, 6, 30, K('timber_d'))
    _box(v, 10, 22, 12, 24, 8, 28, K('board'))               # 厨子（正面は開く）
    _box(v, 11, 21, 22, 24, 9, 27, 0)                        # 開口
    _box(v, 11, 21, 12, 22, 9, 27, K('dark'))                # 中の闇
    _ball(v, 16, 16, 18, 3.5, K('gold'))                     # 像（頭）
    _box(v, 13, 19, 14, 19, 10, 18, K('gold_d'))             # 像（体）
    _box(v, 6, 26, 8, 26, 30, 33, K('shin_d'))               # 屋根
    _box(v, 8, 24, 9, 25, 33, 36, K('shin'))
    _box(v, 11, 21, 10, 24, 36, 39, K('shin_l'))
    _box(v, 15, 17, 15, 19, 39, 44, K('gold'))               # 頂の飾り
    return one(v)


def bell_post_out(name):
    """鐘の柱 — 2 本の柱に横木、青銅の鐘。"""
    v = vol(46)
    for x in (6, 24):
        _box(v, x, x + 3, 15, 18, 0, 38, K('timber'))
    _log_x(v, 16, 39, 2.5, 4, 29, K('log_l'))
    _box(v, 4, 29, 13, 19, 42, 44, K('board_d'))             # 小屋根
    _box(v, 15, 17, 15, 17, 30, 37, K('iron'))
    _cone(v, 16, 16, 18, 30, 6.5, 4.5, K('gold_d'))          # 鐘
    _disc(v, 16, 16, 5.0, 30, 31, K('gold'))
    _disc(v, 16, 16, 1.5, 15, 19, K('iron'))                 # 舌
    return one(v)


def votive_out(name):
    """灯明の台 — 石の台に蝋燭を並べる。火は `votive_flame_out`。"""
    v = vol(20)
    _box(v, 6, 26, 10, 22, 0, 8, K('rub'))
    _box(v, 7, 25, 11, 21, 7, 8, K('rub_l'))
    for (x, y) in ((9, 13), (15, 13), (21, 13), (12, 18), (18, 18)):
        _box(v, x, x + 2, y, y + 2, 8, 14, K('bone'))
    return one(v)


def votive_flame_out(name):
    v = vol(20)
    for (x, y) in ((9, 13), (15, 13), (21, 13), (12, 18), (18, 18)):
        _box(v, x, x + 2, y, y + 2, 14, 17, K('fl_mid'))
        _box(v, x, x + 2, y, y + 2, 15, 16, K('fl_core'))
    return one(v)


def offering_out(name):
    """供え物 — 石の小台に花と果物、水の器。"""
    v = vol(12)
    _box(v, 8, 24, 10, 22, 0, 5, K('rub_l'))
    _box(v, 10, 14, 12, 16, 5, 9, K('fl_w'))
    _box(v, 11, 13, 13, 15, 5, 7, K('stem'))
    _box(v, 15, 19, 12, 16, 5, 8, K('fl_r'))
    _disc(v, 19, 18, 2.5, 5, 8, K('clay'))
    _disc(v, 19, 18, 1.5, 7, 8, K('wa'))
    _box(v, 16, 22, 17, 21, 5, 6, K('out_fl_p'))
    return one(v)


# ------------------------------------------------------------------ 錬金術店
def cauldron_out(name):
    """大釜 — 三脚の鉄釜、下に薪と火（火は別体）。中身は緑に泡立つ。"""
    v = vol(28)
    for z in range(8, 20):
        r = 8.5 - (abs(z - 14) * 0.25)
        _disc(v, 16, 16, r, z, z + 1, K('out_iron_k'))
    _disc(v, 16, 16, 7.5, 18, 20, K('slime'))
    _disc(v, 16, 16, 2.0, 19, 21, K('slime_l'))
    _disc(v, 16, 16, 9.0, 17, 19, K('out_iron_k'), hollow=8.0)   # 縁
    for (x, y) in ((8, 10), (24, 10), (16, 26)):             # 脚
        _box(v, x - 1, x + 1, y - 1, y + 1, 0, 9, K('iron'))
    _log_x(v, 16, 2, 2.0, 6, 26, K('oak'))                   # 薪
    _log_y(v, 16, 2, 2.0, 6, 26, K('oak_l'))
    _box(v, 6, 8, 14, 18, 20, 24, K('iron'))                 # 取っ手
    _box(v, 24, 26, 14, 18, 20, 24, K('iron'))
    return one(v)


def cauldron_fire_out(name):
    v = vol(28)
    _box(v, 11, 21, 11, 21, 3, 6, K('fl_mid'))
    _box(v, 13, 19, 13, 19, 5, 8, K('fl_core'))
    _disc(v, 16, 16, 2.0, 21, 24, K('slime_l'))              # 泡
    _disc(v, 12, 13, 1.0, 21, 23, K('slime_l'))
    return one(v)


def herb_rack_out(name):
    """薬草の干し台 — 横木に束ねた草を吊るす。"""
    v = vol(36)
    for x in (4, 26):
        _box(v, x, x + 2, 15, 17, 0, 30, K('timber'))
    _box(v, 3, 29, 15, 17, 29, 32, K('timber_d'))
    for x, colour in ((7, K('verd')), (12, K('wilt')), (17, K('stalk_d')), (22, K('out_verd_c'))):
        _box(v, x, x + 1, 15, 17, 24, 29, K('lash'))
        _box(v, x - 1, x + 3, 14, 18, 16, 24, colour)
        _box(v, x, x + 2, 15, 17, 12, 16, colour)
    return one(v)


def flask_shelf_out(name):
    """薬瓶の棚 — 背板と 3 段の棚板だけの開いた棚。瓶は南（手前）から見える。"""
    v = vol(30)
    _box(v, 4, 28, 12, 15, 0, 28, K('timber_d'))            # 背板
    for x in (4, 26):
        _box(v, x, x + 2, 12, 24, 0, 28, K('timber'))        # 側板
    for z in (1, 10, 19):
        _box(v, 4, 28, 12, 24, z, z + 1, K('board'))         # 棚板
    _box(v, 4, 28, 12, 24, 27, 28, K('board'))
    colours = (K('out_glass_b'), K('slime_l'), K('fl_r'), K('gold'), K('out_fl_p'), K('wa_l'))
    for i, colour in enumerate(colours):
        x = 7 + (i % 3) * 6
        z = 2 if i < 3 else 11
        _box(v, x, x + 4, 16, 21, z, z + 5, colour)
        _box(v, x + 1, x + 3, 17, 20, z + 5, z + 7, K('glass_l'))
    _box(v, 8, 12, 15, 21, 20, 26, K('clay'))                # 上段の壺
    _box(v, 15, 23, 15, 21, 20, 23, K('cream'))              # 上段の本
    return one(v)


def mushroom_bed_out(name):
    """きのこの苗床 — 板囲いの土に光るきのこ。"""
    rng = _rng(name)
    v = vol(10)
    _box(v, 4, 28, 6, 26, 0, 4, K('timber_d'))
    _box(v, 6, 26, 8, 24, 0, 4, K('soil'))
    for _ in range(int(rng.integers(5, 9))):
        x, y = int(rng.integers(7, 24)), int(rng.integers(9, 22))
        h = int(rng.integers(2, 5))
        _box(v, x, x + 1, y, y + 1, 4, 4 + h, K('stem'))
        _disc(v, x, y, 1.8, 4 + h, 6 + h, K('mgl'))
    return one(v)


def mortar_out(name):
    """乳鉢と薬研 — 石の乳鉢に杵、脇に薬研。"""
    v = vol(14)
    _cone(v, 10, 14, 0, 8, 4.0, 6.0, K('rock_l'))
    _disc(v, 10, 14, 4.5, 7, 8, K('rock_d'))
    _box(v, 9, 11, 13, 15, 6, 13, K('timber'))
    _box(v, 18, 30, 12, 20, 0, 5, K('rock'))                 # 薬研
    _box(v, 19, 29, 15, 17, 3, 5, K('rock_dd'))
    _log_y(v, 24, 6, 3.0, 14, 18, K('iron'))
    return one(v)


# ------------------------------------------------------------------ 魔法屋
def crystal_out(name):
    """水晶 — 石の台座に大きな青い結晶。光は `crystal_glow_out`。"""
    v = vol(40)
    _disc(v, 16, 16, 8.0, 0, 6, K('rock_d'))
    _disc(v, 16, 16, 7.0, 5, 6, K('rock_l'))
    _cone(v, 16, 16, 6, 24, 5.0, 3.0, K('out_glass_b'))
    _cone(v, 16, 16, 24, 34, 3.0, 0.5, K('out_glass_bl'))
    _cone(v, 10, 13, 6, 18, 2.5, 0.5, K('out_glass_b'))
    _cone(v, 22, 19, 6, 16, 2.5, 0.5, K('out_glass_b'))
    return one(v)


def crystal_glow_out(name):
    v = vol(40)
    _box(v, 15, 17, 15, 17, 14, 30, K('out_glass_bl'))
    return one(v)


def rune_stones_out(name):
    """符石 — 環に立てた 5 つの立石、印を彫る。"""
    v = vol(18)
    for k in range(5):
        a = k * 1.2566
        x, y = int(16 + 9 * np.cos(a)), int(16 + 9 * np.sin(a))
        _box(v, x - 2, x + 2, y - 2, y + 2, 0, 10 + (k % 2) * 4, K('rock'))
        _box(v, x - 1, x + 1, y + 1, y + 3, 4, 8, K('out_glass_b'))
    _disc(v, 16, 16, 3.0, 0, 1, K('out_glass_b'))
    return one(v)


def owl_perch_out(name):
    """梟の止まり木 — 枝つきの杭に梟。"""
    v = vol(36)
    _box(v, 15, 17, 15, 17, 0, 26, K('bark'))
    _box(v, 9, 23, 15, 17, 24, 26, K('bark_l'))
    _ball(v, 11, 16, 29, 3.0, K('fur_l'))                    # 梟
    _ball(v, 11, 16, 33, 2.5, K('fur'))
    _box(v, 10, 12, 17, 18, 33, 34, K('gold'))               # 目
    _box(v, 9, 10, 16, 17, 33, 34, K('gold'))
    _box(v, 12, 13, 16, 17, 33, 34, K('gold'))
    return one(v)


def star_lantern_out(name):
    """星の灯籠 — 鉄の枠に星形の窓。火は `star_flame_out`。"""
    v = vol(40)
    _box(v, 14, 18, 14, 18, 0, 3, K('rock_d'))
    _box(v, 15, 17, 15, 17, 3, 24, K('out_iron_k'))
    _box(v, 10, 22, 10, 22, 24, 26, K('out_iron_k'))
    _box(v, 10, 22, 10, 22, 34, 36, K('out_iron_k'))
    for (x0, x1) in ((10, 12), (20, 22)):
        for (y0, y1) in ((10, 12), (20, 22)):
            _box(v, x0, x1, y0, y1, 26, 34, K('out_iron_k'))
    _box(v, 12, 20, 21, 22, 26, 34, K('out_cloth_b'))         # 藍の紙
    _box(v, 15, 17, 21, 22, 28, 32, K('gold'))               # 星
    _box(v, 13, 19, 21, 22, 29, 31, K('gold'))
    _cone(v, 16, 16, 36, 40, 7.0, 1.0, K('out_iron_k'))
    return one(v)


def star_flame_out(name):
    v = vol(40)
    _box(v, 14, 18, 14, 18, 27, 33, K('fl_core'))
    return one(v)


def book_stand_out(name):
    """書見台 — 開いた本を載せた斜めの台。"""
    v = vol(24)
    _box(v, 12, 20, 12, 20, 0, 2, K('timber_d'))
    _box(v, 15, 17, 15, 17, 2, 16, K('timber'))
    for k in range(8):
        _box(v, 8, 24, 10 + k, 11 + k, 16 + k, 18 + k, K('timber_d'))
        _box(v, 9, 23, 10 + k, 11 + k, 18 + k, 19 + k, K('cream'))
    _box(v, 15, 17, 10, 18, 18, 27, K('cream_d'))
    return one(v)


# ------------------------------------------------------------------ 闇市
def wagon_out(name):
    """幌馬車 — 布の幌を掛けた荷馬車。梶棒は北へ。"""
    v = vol(36)
    _box(v, 3, 29, 6, 26, 8, 11, K('oak_d') if 'oak_d' in C else K('oak'))
    _box(v, 3, 29, 6, 26, 10, 11, K('oak_l'))
    for cy in (4, 26):
        _log_y(v, 9, 6, 6.0, cy, cy + 2, K('timber'))
        _log_y(v, 9, 6, 4.4, cy, cy + 2, 0)
        _log_y(v, 23, 6, 6.0, cy, cy + 2, K('timber'))
        _log_y(v, 23, 6, 4.4, cy, cy + 2, 0)
        _box(v, 8, 10, cy, cy + 2, 0, 12, K('timber_d'))
        _box(v, 22, 24, cy, cy + 2, 0, 12, K('timber_d'))
    _box(v, 3, 29, 7, 8, 11, 15, K('oak'))                   # あおり
    _box(v, 3, 29, 24, 25, 11, 15, K('oak'))
    for y in range(8, 26):                                   # 幌（半円）
        pass
    for x in range(3, 29):
        t = (x - 15.5) / 12.5
        top = 15 + int(round(14.0 * (1.0 - t * t) ** 0.5))
        _box(v, x, x + 1, 7, 25, 15, top, K('tent'))
        _box(v, x, x + 1, 7, 25, top - 1, top, K('tent_d'))
    _box(v, 4, 28, 8, 24, 15, 29, 0)                         # 中を空ける
    _box(v, 4, 28, 8, 24, 15, 16, K('board_d'))
    _box(v, 8, 24, 7, 8, 15, 27, K('dark'))                  # 後ろの口（闇）
    _box(v, 12, 20, 24, 25, 15, 25, K('tent_d'))             # 前の垂れ
    return one(v)


def chest_out(name):
    """鉄帯の宝箱 — 鎖で杭につないである。"""
    v = vol(16)
    _box(v, 8, 24, 10, 22, 0, 9, K('timber_d'))
    _box(v, 9, 23, 10, 22, 1, 8, K('board_d'))
    for x in (10, 16, 21):
        _box(v, x, x + 1, 9, 23, 0, 12, K('iron'))
    for x in range(8, 24):
        t = (x - 15.5) / 8.0
        _box(v, x, x + 1, 10, 22, 9, 9 + int(round(3.0 * (1.0 - t * t))), K('timber'))
    _box(v, 15, 17, 21, 23, 4, 7, K('gold'))                 # ロック
    _box(v, 26, 29, 14, 17, 0, 8, K('timber'))               # 杭
    for k in range(6):
        _box(v, 24 + k // 2, 25 + k // 2, 15, 16, 4 + (k % 2), 5 + (k % 2), K('iron_l'))
    return one(v)


def brazier_out(name):
    """火鉢 — 鉄の三脚に皿。火は `brazier_fire_out`。"""
    v = vol(22)
    for (x, y) in ((9, 11), (23, 11), (16, 25)):
        _box(v, x - 1, x + 1, y - 1, y + 1, 0, 12, K('iron'))
    _disc(v, 16, 16, 7.5, 11, 14, K('out_iron_k'))
    _disc(v, 16, 16, 6.0, 13, 14, K('char'))
    return one(v)


def brazier_fire_out(name):
    v = vol(22)
    _disc(v, 16, 16, 4.0, 14, 17, K('fl_mid'))
    _disc(v, 16, 16, 2.0, 16, 20, K('fl_core'))
    return one(v)


def tarp_out(name):
    """布を掛けた荷 — 何かを隠した山（縄で縛る）。"""
    v = vol(16)
    _ball(v, 15, 16, 2, 11.0, K('tent_d'), squash=0.7)
    _ball(v, 19, 12, 4, 6.0, K('tent'), squash=0.8)
    v[:, :, 0][v[:, :, 0] != 0] = K('tent_d')
    for y in (10, 20):
        for x in range(4, 28):
            col = v[x, y, :]
            nz = col.nonzero()[0]
            if len(nz):
                col[nz.max()] = K('lash')
    return one(v)


# ------------------------------------------------------------------ 我が家
def laundry_out(name):
    """洗濯物 — 2 本の柱に張った縄にシーツと服。布は振り子で揺れる。"""
    frame = vol(40)
    for x in (2, 28):
        _box(frame, x, x + 2, 15, 17, 0, 34, K('timber'))
        _box(frame, x - 1, x + 3, 14, 18, 0, 2, K('rub_d'))
    _box(frame, 4, 28, 15, 16, 32, 33, K('lash'))
    sheet = vol(40)
    _box(sheet, 5, 15, 15, 16, 12, 32, K('out_cloth_w'))
    _box(sheet, 5, 15, 15, 16, 12, 14, K('out_cloth_ws'))
    _box(sheet, 9, 11, 15, 16, 12, 32, K('out_cloth_ws'))     # 折り目
    shirt = vol(40)
    _box(shirt, 17, 26, 15, 16, 20, 32, K('out_cloth_b'))
    _box(shirt, 15, 17, 15, 16, 27, 32, K('out_cloth_b'))
    _box(shirt, 26, 28, 15, 16, 27, 32, K('out_cloth_b'))
    _box(shirt, 20, 23, 15, 16, 20, 32, K('out_cloth_bd'))
    return [('frame', frame, (0, 0, 0)), ('sheet', sheet, (0, 0, 0)), ('shirt', shirt, (0, 0, 0))]


def laundry_parts():
    return H['swing_parts']([('sheet', (10, 15.5, 32)), ('shirt', (21, 15.5, 32))], amplitude=5.0, period=2.8)


def vegetable_plot_out(name):
    """野菜の畝 — 3 本の畝に葉物と、支柱に絡む蔓。"""
    rng = _rng(name)
    v = vol(18)
    for y in (6, 15, 24):
        _box(v, 2, 30, y, y + 5, 0, 3, K('soil'))
        _box(v, 2, 30, y + 1, y + 4, 2, 3, K('dirt'))
        for x in range(4, 29, 5):
            _ball(v, x, y + 2, 4, 2.5, K('out_verd_c') if rng.random() < 0.6 else K('out_verd_cl'), squash=0.8)
    for x in (8, 20):                                        # 支柱と蔓
        _box(v, x, x + 1, 24, 25, 0, 16, K('timber_d'))
        for z in range(3, 15, 3):
            _box(v, x - 1, x + 2, 23, 26, z, z + 2, K('leaf_m'))
        _box(v, x - 1, x, 23, 24, 8, 10, K('fl_r'))
    return one(v)


def doghouse_out(name):
    """犬小屋 — 切妻の小さな小屋、前に餌の皿。"""
    v = vol(24)
    _box(v, 6, 26, 8, 26, 0, 14, K('board'))
    _box(v, 6, 26, 8, 26, 6, 7, K('board_d'))
    _box(v, 12, 20, 25, 26, 2, 12, K('dark'))                # 入口
    for z in range(14, 24):
        d = (z - 14) * 2
        _box(v, 4 + d, 28 - d, 6, 28, z, z + 1, K('shin') if (z % 2) else K('shin_d'))
    _disc(v, 28, 28, 3.0, 0, 2, K('iron_l'))                 # 皿
    _box(v, 20, 26, 26, 27, 0, 3, K('bone'))                 # 骨
    return one(v)


# ------------------------------------------------------------------ 書店
def book_stack_out(name):
    """本の山 — 色の違う本を斜めに積む。"""
    v = vol(20)
    cols = [K('out_cloth_r'), K('out_cloth_b'), K('fur'), K('cream'), K('rust'), K('out_cloth_bd')]
    z = 0
    for i in range(6):
        w = 12 - (i % 3)
        x0 = 8 + (i % 2) * 2
        _box(v, x0, x0 + w, 10, 22, z, z + 3, cols[i])
        _box(v, x0 + 1, x0 + w - 1, 10, 22, z + 1, z + 2, K('cream'))
        z += 3
    _box(v, 22, 30, 12, 20, 0, 3, K('cream'))                # 開いた本
    _box(v, 22, 30, 15, 17, 0, 4, K('timber_d'))
    return one(v)


def scroll_rack_out(name):
    """巻物の棚 — 枡目の棚に巻物。"""
    v = vol(30)
    _box(v, 4, 28, 18, 26, 0, 28, K('timber_d'))
    for z in range(2, 28, 6):
        for x in range(6, 26, 6):
            _box(v, x, x + 5, 19, 26, z, z + 5, 0)
            _log_y(v, x + 2, z + 2, 1.8, 19, 26, K('cream'))
            _box(v, x + 1, x + 4, 25, 26, z + 1, z + 3, K('lash'))
    return one(v)


def lectern_out(name):
    """外の読み台 — 傾いた台と、その上の本と燭台。"""
    v = vol(26)
    _box(v, 10, 22, 12, 20, 0, 2, K('timber_d'))
    _box(v, 14, 18, 14, 18, 2, 18, K('timber'))
    for k in range(8):
        _box(v, 7, 25, 11 + k, 12 + k, 18 + k, 20 + k, K('timber_d'))
    _box(v, 9, 23, 13, 17, 21, 23, K('cream'))
    _box(v, 15, 17, 13, 17, 22, 24, K('cream_d'))
    _box(v, 22, 24, 18, 20, 20, 26, K('iron'))               # 燭台
    _box(v, 22, 24, 18, 20, 26, 27, K('bone'))
    return one(v)


# ------------------------------------------------------------------ 博物館
def dragon_skull_out(name):
    """竜の頭骨 — 石の台に載せた大きな頭骨。角と牙。"""
    v = vol(36)
    _box(v, 3, 29, 6, 28, 0, 6, K('rub'))
    _box(v, 4, 28, 7, 27, 5, 6, K('rub_l'))
    _ball(v, 15, 14, 16, 9.0, K('out_bone_y'), squash=0.8)  # 頭蓋
    _box(v, 8, 22, 14, 30, 8, 16, K('out_bone_y'))           # 吻
    _box(v, 9, 21, 15, 30, 10, 14, K('bone_d'))
    _box(v, 10, 13, 22, 30, 6, 10, 0)                        # 口の隙間
    _box(v, 17, 20, 22, 30, 6, 10, 0)
    for x in (9, 13, 17, 21):                                # 牙
        _box(v, x, x + 1, 29, 30, 8, 12, K('bone'))
        _box(v, x, x + 1, 29, 30, 4, 8, K('bone'))
    _box(v, 10, 13, 16, 20, 17, 20, K('dark'))               # 眼窩
    _box(v, 18, 21, 16, 20, 17, 20, K('dark'))
    for (x, dx) in ((7, -1), (23, 1)):                       # 角
        for k in range(10):
            _box(v, x + dx * k, x + dx * k + 2, 10, 13, 20 + k, 23 + k, K('bone_d'))
    return one(v)


def plinth_out(name):
    """台座の遺物 — 石の柱に金の杯と、説明の札。"""
    v = vol(34)
    _box(v, 10, 22, 10, 22, 0, 4, K('rub'))
    _box(v, 12, 20, 12, 20, 4, 22, K('rock_l'))
    _box(v, 11, 21, 11, 21, 22, 24, K('rub_l'))
    _cone(v, 16, 16, 24, 27, 3.0, 1.5, K('gold_d'))
    _cone(v, 16, 16, 27, 33, 2.0, 4.0, K('gold'))
    _box(v, 13, 19, 21, 23, 8, 16, K('board_l'))             # 札
    return one(v)


def statue_out(name):
    """石像 — 剣を地に突いた戦士の像。台座つき。"""
    v = vol(50)
    _box(v, 8, 24, 8, 24, 0, 6, K('rub'))
    _box(v, 9, 23, 9, 23, 5, 6, K('rub_l'))
    _box(v, 12, 20, 12, 20, 6, 30, K('rock_l'))              # 体
    _box(v, 10, 22, 13, 19, 22, 30, K('rock_l'))             # 肩
    _ball(v, 16, 16, 34, 4.0, K('rock'))                     # 頭
    _box(v, 20, 24, 14, 18, 6, 30, K('rock'))                # 腕（剣を持つ）
    _box(v, 25, 27, 15, 17, 6, 36, K('rock_d'))              # 剣
    _box(v, 23, 29, 15, 17, 28, 30, K('rock_d'))
    _box(v, 11, 21, 19, 21, 6, 22, K('rock_d'))              # マント（前）
    return one(v)


def fossil_out(name):
    """化石 — 石板に埋まった大きな骨。"""
    v = vol(10)
    _box(v, 4, 28, 8, 26, 0, 5, K('rock'))
    _box(v, 5, 27, 9, 25, 4, 5, K('rock_l'))
    _log_x(v, 17, 5, 1.5, 8, 24, K('bone_d'))                # 背骨
    for x in range(9, 24, 3):
        _box(v, x, x + 1, 12, 22, 5, 6, K('bone_d'))         # 肋骨
    _ball(v, 26, 17, 5, 3.0, K('bone'), squash=0.5)          # 頭
    return one(v)


# ------------------------------------------------------------------ 宿屋
def trough_out(name):
    """飼い葉桶 — 長い木の桶に水と干し草。"""
    v = vol(12)
    _box(v, 2, 30, 10, 22, 0, 10, K('oak'))
    _box(v, 3, 29, 11, 21, 3, 10, 0)
    _box(v, 3, 29, 11, 21, 3, 8, K('wa_d'))
    _box(v, 3, 12, 11, 21, 8, 10, K('hay'))
    for x in (2, 15, 28):
        _box(v, x, x + 2, 9, 23, 0, 11, K('timber_d'))
    return one(v)


def hitching_post_out(name):
    """馬つなぎ — 横木つきの杭に鉄の輪と縄。"""
    v = vol(30)
    for x in (5, 25):
        _box(v, x, x + 3, 14, 17, 0, 24, K('timber'))
    _log_x(v, 15, 22, 2.0, 3, 29, K('log_l'))
    _disc(v, 15, 15, 2.5, 16, 18, K('iron'), hollow=1.5)
    _box(v, 14, 16, 15, 16, 18, 22, K('iron'))
    _box(v, 15, 16, 15, 17, 4, 16, K('lash'))                # 縄
    return one(v)


def ale_barrels_out(name):
    """麦酒の樽 — 横倒しの樽を架台に 2 段。栓と桶。"""
    v = vol(26)
    _box(v, 3, 29, 8, 24, 0, 3, K('timber_d'))
    for (cy, cz) in ((11, 8), (21, 8), (16, 18)):
        _log_x(v, cy, cz, 5.5, 5, 27, K('timber_l'))
        _log_x(v, cy, cz, 5.8, 7, 9, K('iron'))
        _log_x(v, cy, cz, 5.8, 23, 25, K('iron'))
        _box(v, 27, 29, cy - 1, cy + 1, cz - 1, cz + 1, K('timber_d'))   # 栓
    _box(v, 3, 5, 12, 20, 3, 14, K('timber'))
    _box(v, 27, 29, 12, 20, 3, 14, K('timber'))
    return one(v)


def table_bench_out(name):
    """外の卓 — 板の卓と長椅子、上に酒器。"""
    v = vol(18)
    _box(v, 4, 28, 12, 20, 12, 15, K('board'))
    for x in (6, 24):
        _box(v, x, x + 2, 13, 19, 0, 12, K('timber'))
    for y in (4, 26):
        _box(v, 4, 28, y, y + 3, 7, 9, K('oak'))
        for x in (6, 24):
            _box(v, x, x + 2, y, y + 3, 0, 7, K('timber_d'))
    _disc(v, 10, 16, 2.0, 15, 19, K('clay'))                 # 酒器
    _disc(v, 20, 15, 1.5, 15, 18, K('oak_l'))
    _box(v, 14, 18, 15, 17, 15, 16, K('cream'))              # パン
    return one(v)


# ------------------------------------------------------------------ 村長
def banner_pole_out(name):
    """旗竿 — 高い竿に横木、赤い旗（振り子で揺れる）。"""
    frame = vol(60)
    _box(frame, 13, 19, 13, 19, 0, 4, K('rub_d'))
    _box(frame, 15, 17, 15, 17, 4, 58, K('timber'))
    _box(frame, 15, 17, 15, 17, 58, 60, K('gold'))
    _box(frame, 4, 17, 15, 16, 54, 56, K('timber_d'))
    flag = vol(60)
    _box(flag, 5, 15, 15, 16, 30, 54, K('out_cloth_r'))
    _box(flag, 5, 15, 15, 16, 30, 34, K('out_cloth_rd'))
    _box(flag, 8, 12, 15, 16, 38, 48, K('gold'))             # 紋
    _box(flag, 9, 11, 15, 16, 36, 50, K('gold'))
    return [('frame', frame, (0, 0, 0)), ('flag', flag, (0, 0, 0))]


def banner_parts():
    return H['swing_parts']([('flag', (10, 15.5, 55))], amplitude=4.0, period=3.2)


def monument_out(name):
    """記念碑 — 石の台に立石。銘と花輪。"""
    v = vol(40)
    _box(v, 6, 26, 8, 24, 0, 5, K('rub'))
    _box(v, 7, 25, 9, 23, 4, 5, K('rub_l'))
    _box(v, 11, 21, 12, 20, 5, 34, K('rock_l'))
    _box(v, 12, 20, 12, 20, 34, 37, K('rock'))
    _box(v, 13, 19, 19, 21, 14, 28, K('rock_d'))             # 銘の面
    for z in range(16, 27, 3):
        _box(v, 14, 18, 20, 21, z, z + 1, K('rock_l'))
    _disc(v, 16, 21, 5.0, 8, 10, K('leaf_m'), hollow=3.5)    # 花輪
    _box(v, 15, 17, 25, 27, 8, 10, K('fl_r'))
    return one(v)


# ------------------------------------------------------------------ ハンター事務所
def pelt_frame_out(name):
    """毛皮の干し枠 — 大きな枠に狼の皮。"""
    v = vol(34)
    for x in (2, 28):
        _box(v, x, x + 2, 15, 17, 0, 32, K('timber'))
    _box(v, 2, 30, 15, 17, 30, 32, K('timber'))
    _box(v, 2, 30, 15, 17, 3, 5, K('timber'))
    _box(v, 5, 27, 15, 17, 8, 28, K('fur_d'))
    _box(v, 7, 25, 15, 17, 10, 26, K('fur'))
    _box(v, 12, 20, 15, 17, 12, 24, K('fur_l'))              # 腹の色
    for z in range(9, 29, 4):
        _box(v, 4, 6, 15, 17, z, z + 1, K('lash'))
        _box(v, 26, 28, 15, 17, z, z + 1, K('lash'))
    _box(v, 14, 18, 15, 17, 28, 31, K('fur_d'))              # 頭
    return one(v)


def antler_rack_out(name):
    """枝角の飾り — 柱に掛けた鹿の頭骨と枝角。"""
    v = vol(40)
    _box(v, 14, 18, 14, 18, 0, 3, K('rub_d'))
    _box(v, 15, 18, 15, 18, 3, 34, K('timber'))
    _box(v, 13, 20, 17, 20, 22, 30, K('bone'))               # 頭骨
    _box(v, 14, 19, 19, 21, 18, 23, K('bone_d'))
    for (sx, dx) in ((12, -1), (20, 1)):                     # 枝角
        for k in range(8):
            _box(v, sx + dx * k, sx + dx * k + 2, 17, 19, 28 + k, 30 + k, K('bone_d'))
        for k in range(4):
            _box(v, sx + dx * (3 + k), sx + dx * (3 + k) + 1, 17, 19, 33 + k, 35 + k, K('bone_d'))
    return one(v)


def target_out(name):
    """弓の的 — 藁の的に矢が刺さっている。三脚。"""
    v = vol(30)
    _log_y(v, 16, 16, 9.0, 14, 18, K('hay'))
    _log_y(v, 16, 16, 6.0, 17, 18, K('hay_d'))
    _log_y(v, 16, 16, 3.5, 17, 18, K('out_cloth_r'))
    _log_y(v, 16, 16, 1.5, 17, 18, K('gold'))
    for (x, y) in ((6, 10), (26, 10), (16, 24)):
        _box(v, x - 1, x + 1, y - 1, y + 1, 0, 12, K('timber_d'))
    for (x, z) in ((14, 18), (19, 15), (17, 21)):            # 矢
        _box(v, x, x + 1, 18, 26, z, z + 1, K('timber_d'))
        _box(v, x, x + 1, 25, 27, z - 1, z + 2, K('out_cloth_r'))
    return one(v)


def game_hang_out(name):
    """吊るした獲物 — 横木から兎と鳥を吊るす。"""
    v = vol(40)
    for x in (4, 26):
        _box(v, x, x + 2, 15, 17, 0, 34, K('timber'))
    _log_x(v, 16, 35, 1.8, 3, 29, K('log_l'))
    for x in (9, 20):
        _box(v, x, x + 1, 15, 16, 26, 34, K('lash'))
    _ball(v, 9, 16, 22, 3.0, K('fur_l'))                     # 兎
    _box(v, 8, 11, 15, 17, 14, 22, K('fur_l'))
    _box(v, 8, 10, 15, 17, 10, 14, K('fur'))
    _ball(v, 20, 16, 22, 3.5, K('fur_d'))                    # 鳥
    _box(v, 19, 22, 15, 17, 16, 22, K('fur_d'))
    _box(v, 20, 21, 15, 17, 12, 16, K('gold_d'))
    return one(v)


def campfire_out(name):
    """焚き火 — 石を環に組み、薪を組む。火は `campfire_fire_out`。脇に丸太の腰掛け。"""
    v = vol(12)
    _disc(v, 14, 16, 8.0, 0, 3, K('rock'), hollow=6.0)
    _disc(v, 14, 16, 6.0, 0, 1, K('char'))
    _log_x(v, 16, 3, 1.8, 8, 20, K('oak'))
    _log_y(v, 14, 3, 1.8, 10, 22, K('oak_l'))
    _log_x(v, 26, 3, 3.0, 4, 28, K('log'))                   # 腰掛けの丸太
    return one(v)


def campfire_fire_out(name):
    v = vol(16)
    _disc(v, 14, 16, 3.5, 4, 8, K('fl_mid'))
    _disc(v, 14, 16, 2.0, 7, 12, K('fl_core'))
    _disc(v, 14, 16, 5.0, 3, 4, K('emb_l'))
    return one(v)


def arrow_barrel_out(name):
    """矢の樽 — 樽に矢を立て、脇に弓。"""
    v = vol(30)
    for z in range(14):
        _disc(v, 14, 16, 6.0, z, z + 1, K('timber') if (z % 4) else K('timber_l'))
    for z in (2, 11):
        _disc(v, 14, 16, 6.3, z, z + 2, K('iron'), hollow=5.0)
    for (x, y) in ((12, 14), (16, 15), (14, 18), (11, 18), (17, 12)):
        _box(v, x, x + 1, y, y + 1, 14, 28, K('timber_d'))
        _box(v, x - 1, x + 2, y, y + 1, 26, 29, K('out_cloth_r'))
    for k in range(20):                                      # 弓（弧）
        t = (k - 10) / 10.0
        x = 25 + int(round(4.0 * (1.0 - t * t)))
        _box(v, x, x + 1, 15, 17, 2 + k, 3 + k, K('timber_d'))
    _box(v, 25, 26, 15, 16, 2, 22, K('lash'))
    return one(v)


# ------------------------------------------------------------------ 登録
def register(g, h):
    global G, H, C, V
    G, H, C, V = g, h, g['C'], g['V']
    cat = g['CATALOG']
    static = g['static_part']
    single = {
        'well': (well_out, '井戸。丸太の櫓と滑車'),
        'cart': (cart_out, '荷車。袋と樽'),
        'haystack': (haystack_out, '干し草の山'),
        'barrel': (barrel_out, '樽'),
        'crate': (crate_out, '木箱 2 つ'),
        'sack': (sack_out, '麻袋 3 つ'),
        'bench': (bench_out, '丸太の腰掛け'),
        'woodpile': (woodpile_out, '薪の山'),
        'chopping_block': (chopping_block_out, '薪割り台と斧'),
        'bucket': (bucket_out, '桶 2 つ'),
        'rain_barrel': (rain_barrel_out, '雨水の樽'),
        'lantern_post': (lantern_post_out, 'ランタンの柱。火は lantern_flame_out'),
        'lantern_flame': (lantern_flame_out, 'ランタンの火'),
        'stump': (stump_out, '切り株'),
        'mushrooms': (mushrooms_out, 'きのこの群れ'),
        'goods_stall': (goods_stall_out, '雑貨屋の売り台'),
        'rope_coil': (rope_coil_out, '縄の輪'),
        'armour_stand': (armour_stand_out, '鎧の立て台'),
        'shield_rack': (shield_rack_out, '盾の掛け台'),
        'helm_post': (helm_post_out, '兜掛け'),
        'hide_frame': (hide_frame_out, '皮の張り枠'),
        'forge': (forge_out, '鍛冶の炉。火は forge_fire_out'),
        'forge_fire': (forge_fire_out, '炉の火'),
        'anvil': (anvil_out, '金床'),
        'quench': (quench_out, '焼き入れの水桶'),
        'weapon_rack': (weapon_rack_out, '武器立て'),
        'coal_heap': (coal_heap_out, '炭の山'),
        'grindstone': (grindstone_out, '砥石'),
        'shrine': (shrine_out, '小さな祠'),
        'bell_post': (bell_post_out, '鐘の柱'),
        'votive': (votive_out, '灯明の台。火は votive_flame_out'),
        'votive_flame': (votive_flame_out, '灯明の火'),
        'offering': (offering_out, '供え物'),
        'cauldron': (cauldron_out, '大釜。火は cauldron_fire_out'),
        'cauldron_fire': (cauldron_fire_out, '大釜の火と泡'),
        'herb_rack': (herb_rack_out, '薬草の干し台'),
        'flask_shelf': (flask_shelf_out, '薬瓶の棚'),
        'mushroom_bed': (mushroom_bed_out, 'きのこの苗床'),
        'mortar': (mortar_out, '乳鉢と薬研'),
        'crystal': (crystal_out, '水晶。光は crystal_glow_out'),
        'crystal_glow': (crystal_glow_out, '水晶の光'),
        'rune_stones': (rune_stones_out, '符石の環'),
        'owl_perch': (owl_perch_out, '梟の止まり木'),
        'star_lantern': (star_lantern_out, '星の灯籠。火は star_flame_out'),
        'star_flame': (star_flame_out, '星の灯籠の火'),
        'book_stand': (book_stand_out, '書見台'),
        'wagon': (wagon_out, '幌馬車'),
        'chest': (chest_out, '鉄帯の宝箱'),
        'brazier': (brazier_out, '火鉢。火は brazier_fire_out'),
        'brazier_fire': (brazier_fire_out, '火鉢の火'),
        'tarp': (tarp_out, '布を掛けた荷'),
        'vegetable_plot': (vegetable_plot_out, '野菜の畝'),
        'doghouse': (doghouse_out, '犬小屋'),
        'book_stack': (book_stack_out, '本の山'),
        'scroll_rack': (scroll_rack_out, '巻物の棚'),
        'lectern': (lectern_out, '読み台'),
        'dragon_skull': (dragon_skull_out, '竜の頭骨'),
        'plinth': (plinth_out, '台座の遺物'),
        'statue': (statue_out, '戦士の石像'),
        'fossil': (fossil_out, '化石の石板'),
        'trough': (trough_out, '飼い葉桶'),
        'hitching_post': (hitching_post_out, '馬つなぎ'),
        'ale_barrels': (ale_barrels_out, '麦酒の樽'),
        'table_bench': (table_bench_out, '外の卓と長椅子'),
        'monument': (monument_out, '記念碑'),
        'pelt_frame': (pelt_frame_out, '毛皮の干し枠'),
        'antler_rack': (antler_rack_out, '枝角の飾り'),
        'target': (target_out, '弓の的'),
        'game_hang': (game_hang_out, '吊るした獲物'),
        'campfire': (campfire_out, '焚き火。火は campfire_fire_out'),
        'campfire_fire': (campfire_fire_out, '焚き火の火'),
        'arrow_barrel': (arrow_barrel_out, '矢の樽と弓'),
    }
    for key, (builder, note) in single.items():
        cat[key + '_out'] = (builder, static(), '辺境の小物: ' + note)
    for i in range(1, 4):
        cat['shrub_out_%02d' % i] = (shrub_out, static(motion={'kind': 'wind'}, wind_k=0.4), '低木')
        cat['flowers_out_%02d' % i] = (flowers_out, static(motion={'kind': 'wind'}, wind_k=0.6), '花の群れ')
    for i in range(1, 3):
        cat['boulder_out_%02d' % i] = (boulder_out, static(), '岩')
    cat['laundry_out'] = (laundry_out, laundry_parts(), '洗濯物（布は振り子で揺れる）')
    cat['banner_pole_out'] = (banner_pole_out, banner_parts(), '旗竿（旗は振り子で揺れる）')
