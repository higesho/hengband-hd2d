# -*- coding: utf-8 -*-
"""Angband のダンジョンの**小物**（の表の ①〜④）。

`angband_dungeon_prefabs.py` の `register()` から `register_props(namespace)` で呼ばれる
（形の道具と色はあちらの名前空間から借りる）。名前は末尾に `_agd_<略号>`。

枠の意味（`hd2d/world/terrain_view.cpp` の装飾層）:
  ① dead_end … 行き止まりに必ず置く塚
  ② wall_side … 通路の壁際 3 種（40 / 30 / 30）。**壁がどちら側かは分からない**ので、
     壁付けの物もマスの中央に自立する形で作る（`sconce` と同じ理由）
  ③ pillar … 部屋の縁の柱。fire_body / fire_flame … 火の 2 体。**炎は別体**（自発光は
     インスタンス単位なので、本体と一緒だと本体まで光る）。炎の自発光の色は橙のべた書き
     （`emit_prop(..., 1.35, 0.85, 0.30)`）なので、青白い灯・緑の茸は**色の明るさ**で言う
  ④ room_props … 部屋の中 3 種（40 / 35 / 25）。自発光は渡らない

**背は低く**（高い装飾は視線を遮る扱い）。柱だけは `pillar_stone` と同じ丈 54。
"""
from __future__ import annotations

import numpy as np

G = C = None
V = 32
vol = box = disc = cone = ball = rod = rng_for = pick = K = one = None


def _bind(ns):
    global G, C, V, vol, box, disc, cone, ball, rod, rng_for, pick, K, one
    G, C, V = ns['G'], ns['C'], ns['V']
    vol, box, disc, cone, ball, rod = ns['vol'], ns['box'], ns['disc'], ns['cone'], ns['ball'], ns['rod']
    rng_for, pick, K, one = ns['rng_for'], ns['pick'], ns['K'], ns['one']


# ------------------------------------------------------------------ 共通の形
def heap(v, rng, n, shades, weights, zcap, xr=(8, 24), yr=(8, 24), span=(3, 8)):
    """塊を重ねた山（瓦礫・落盤・結晶・鍾乳石の欠片）。高さは `zcap` で抑える。"""
    h = np.zeros_like(v)
    for _ in range(n):
        cx, cy = int(rng.integers(*xr)), int(rng.integers(*yr))
        w, d = int(rng.integers(*span)), int(rng.integers(*span))
        hh = int(rng.integers(2, 6))
        z0 = int(rng.integers(0, 3)) if max(abs(cx - 16), abs(cy - 16)) > 5 else int(rng.integers(2, 6))
        box(h, cx - w // 2, cx + (w + 1) // 2, cy - d // 2, cy + (d + 1) // 2, max(0, z0 - hh), z0 + hh,
            pick(rng, shades, weights))
    h[:, :, zcap:] = 0
    v[h > 0] = h[h > 0]


def bone_scatter(v, rng, n, bone, bone_d, zr=(0, 2)):
    for _ in range(n):
        x, y = int(rng.integers(3, 22)), int(rng.integers(3, 22))
        ln = int(rng.integers(5, 10))
        z = int(rng.integers(*zr))
        if rng.random() < 0.5:
            box(v, x, x + ln, y, y + 1, z, z + 1, bone_d)
            box(v, x - 1, x + 1, y - 1, y + 1, z, z + 2, bone)
            box(v, x + ln - 1, x + ln + 1, y - 1, y + 1, z, z + 2, bone)
        else:
            box(v, x, x + 1, y, y + ln, z, z + 1, bone_d)
            box(v, x - 1, x + 1, y - 1, y + 1, z, z + 2, bone)
            box(v, x - 1, x + 1, y + ln - 1, y + ln + 1, z, z + 2, bone)


def skull(v, x, y, z, bone, bone_d, dark):
    box(v, x, x + 7, y, y + 6, z, z + 5, bone)
    box(v, x + 1, x + 6, y + 1, y + 5, z + 5, z + 6, bone)
    box(v, x, x + 7, y + 3, y + 6, z - 2, z, bone_d)             # 顎（手前＝+y へ出す）
    box(v, x + 1, x + 3, y + 5, y + 6, z + 2, z + 4, dark)         # 眼窩（+y ＝ カメラ側の面）
    box(v, x + 4, x + 6, y + 5, y + 6, z + 2, z + 4, dark)


def column(v, shades, r_base, r_shaft, h, cap_h=3, base_h=4, ring_every=0):
    """台座・柱身・柱頭の 3 段の丸柱（`pillar_stone` と同じ骨格）。"""
    base, mid, top = shades
    disc(v, 16, 16, r_base + 1.4, 0, base_h // 2, base)
    disc(v, 16, 16, r_base, base_h // 2, base_h, mid)
    for z in range(base_h, h - cap_h - 2):
        r = r_shaft - (z - base_h) * 0.02
        c = top if (ring_every and (z % ring_every == 0)) else mid
        disc(v, 16, 16, r, z, z + 1, c)
    disc(v, 16, 16, r_shaft + 1.0, h - cap_h - 2, h - cap_h, top)
    box(v, 16 - 7, 16 + 7, 16 - 7, 16 + 7, h - cap_h, h, base)


# ================================================================== classic（cl）
def rockfall_agd_cl(name):
    """① 崩れた石の山 — 切石が崩れ落ちた山（角の立った石。瓦礫より 1 個が大きい）。"""
    rng = rng_for(name)
    v = vol(h=14)
    heap(v, rng, 9, [K('rock_agd_cl'), K('rock_agd_cl_l'), K('rock_agd_cl_d'), K('soot_agd_cl_l')],
         [0.4, 0.2, 0.25, 0.15], 11, span=(4, 9))
    return one(v)


def sconce_dead_agd_cl(name):
    """② 壁の燭台（消えている）— 鉄の腕と受け皿。**炎は無い**。燃え残りの黒い芯だけ。"""
    v = vol(h=30)
    box(v, 14, 18, 14, 18, 0, 2, K('rock_agd_cl_d'))          # 落ちてきた台座石
    box(v, 15, 17, 15, 17, 2, 22, K('iron_agd_cl'))            # 支柱
    box(v, 13, 19, 13, 19, 22, 24, K('iron_agd_cl'))           # 受け皿
    box(v, 12, 20, 12, 20, 24, 26, K('iron_agd_cl_l'))         # 皿の縁
    box(v, 13, 19, 13, 19, 25, 26, K('soot_agd_cl'))           # 灰
    box(v, 15, 17, 15, 17, 26, 29, K('soot_agd_cl'))           # 燃え残りの芯
    return one(v)


def stone_fallen_agd_cl(name):
    """② 落ちた石 — 壁から抜け落ちた切石 1〜2 個。"""
    rng = rng_for(name)
    v = vol(h=12)
    box(v, 8, 20, 10, 20, 0, 8, K('rock_agd_cl'))
    box(v, 8, 20, 10, 20, 7, 8, K('rock_agd_cl_l'))
    if rng.random() < 0.7:
        box(v, 18, 27, 16, 24, 0, 6, K('rock_agd_cl_d'))
    box(v, 10, 12, 10, 11, 4, 8, K('soot_agd_cl'))
    return one(v)


def chain_ring_agd_cl(name):
    """② 鎖の輪 — 床に落ちた鉄の輪と、そこから伸びる鎖。"""
    v = vol(h=6)
    disc(v, 14, 16, 6.5, 0, 2, K('iron_agd_cl'), hollow=4.5)
    disc(v, 14, 16, 6.5, 1, 2, K('iron_agd_cl_l'), hollow=5.5)
    for k in range(6):                                          # 鎖（交互の輪）
        x = 20 + k * 2
        box(v, x, x + 3, 15 + (k % 2), 17 + (k % 2), 0, 2, K('iron_agd_cl') if k % 2 else K('iron_agd_cl_l'))
    return one(v)


def pillar_agd_cl(name):
    """③ 煤けた石の柱 — 切石の丸柱。上ほど煤で黒い。"""
    v = vol(h=54)
    column(v, (K('rock_agd_cl_d'), K('rock_agd_cl'), K('rock_agd_cl_l')), 7.2, 5.6, 54)
    for z in range(30, 49):                                     # 煤
        disc(v, 16, 16, 6.0, z, z + 1, K('soot_agd_cl') if z % 5 else K('soot_agd_cl_l'), hollow=4.6)
    return one(v)


def torch_agd_cl(name):
    """③ 松明（本体）— 鉄の輪で壁掛けにしていた松明を、床の石の受けに立てたもの。炎は `torch_flame_agd_cl`。"""
    v = vol(h=32)
    box(v, 12, 20, 12, 20, 0, 3, K('rock_agd_cl_d'))
    box(v, 13, 19, 13, 19, 3, 4, K('soot_agd_cl'))
    box(v, 15, 17, 15, 17, 4, 26, K('timber_agd_cl_d'))          # 柄
    box(v, 14, 18, 14, 18, 24, 27, K('iron_agd_cl'))             # 鉄の輪
    box(v, 14, 18, 14, 18, 27, 30, K('cloth_agd_md_d'))          # 布を巻いた頭（焦げ）
    return one(v)


def torch_flame_agd_cl(name):
    v = vol(h=42)
    box(v, 14, 18, 14, 18, 30, 35, K('fl_edge'))
    box(v, 15, 17, 15, 17, 32, 39, K('fl_mid'))
    box(v, 15, 17, 15, 17, 34, 37, K('fl_core'))
    return one(v)


def barrel_fallen_agd_cl(name):
    """④ 倒れた樽 — 横倒しで箍が外れ、板が 2 枚抜けている。"""
    v = vol(h=14)
    for x in range(6, 26):
        for y in range(V):
            for z in range(14):
                d2 = (y - 16 + 0.5) ** 2 + (z - 6.5) ** 2
                bulge = 1.0 - ((x - 15.5) / 10.0) ** 2 * 0.2
                r = 6.4 * bulge
                if d2 <= r * r:
                    ang = int((np.arctan2(z - 6.5, y - 16 + 0.5) + 3.15) / 0.9)
                    v[x, y, z] = K('timber_agd_cl_l') if ang % 2 else K('timber_agd_cl')
    v[6:26, 14:18, 13] = 0                                       # 上の板が抜けている
    v[6:26, 18:21, 11:13] = 0
    for x in (9, 22):                                            # 箍（1 本は外れて落ちた）
        for y in range(V):
            for z in range(14):
                d2 = (y - 16 + 0.5) ** 2 + (z - 6.5) ** 2
                if 5.6 * 5.6 <= d2 <= 6.9 * 6.9 and z < 12:
                    v[x, y, z] = K('iron_agd_cl')
    disc(v, 27, 22, 5.5, 0, 1, K('iron_agd_cl'), hollow=4.5)
    return one(v)


def crate_broken_agd_cl(name):
    """④ 割れた木箱 — 蓋が外れ、側板が 1 枚割れて中の藁が覗く。"""
    v = vol(h=16)
    box(v, 7, 25, 8, 24, 0, 12, K('timber_agd_cl'))
    box(v, 9, 23, 10, 22, 6, 12, K('hay_agd_md'))                # 中の藁
    box(v, 7, 25, 23, 24, 0, 12, K('timber_agd_cl_l'))          # 南面（+y ＝ カメラ側）の板
    box(v, 7, 25, 23, 24, 5, 6, K('timber_agd_cl_d'))
    box(v, 12, 18, 22, 24, 6, 12, 0)                             # 割れ
    box(v, 12, 18, 22, 23, 6, 12, K('hay_agd_md'))
    box(v, 7, 25, 8, 24, 3, 4, K('timber_agd_cl_d'))            # 帯
    box(v, 7, 25, 8, 24, 9, 10, K('timber_agd_cl_d'))
    box(v, 20, 30, 20, 30, 0, 2, K('timber_agd_cl_l'))          # 外れた蓋
    box(v, 20, 30, 20, 30, 1, 2, K('timber_agd_cl'))
    return one(v)


def bones_old_agd_cl(name):
    """④ 古い骨 — 黄ばんだ頭蓋と散った骨（既定の `bones` より暗い色。古い）。"""
    rng = rng_for(name)
    v = vol(h=8)
    skull(v, 9, 14, 2, K('bone_agd_cl'), K('bone_agd_cl_d'), K('dark'))
    bone_scatter(v, rng, 4, K('bone_agd_cl'), K('bone_agd_cl_d'))
    return one(v)


# ================================================================== modified（md）
def stele_shard_agd_md(name):
    """① ひびの入った石碑の欠片 — 斜めに倒れた石碑の上半分と、割れて散った欠片。刻んだ字の跡。"""
    rng = rng_for(name)
    v = vol(h=18)
    for k in range(14):                                         # 倒れかけた板（斜め）
        z = 2 + k
        y0 = 20 - k
        box(v, 8, 22, y0, y0 + 4, z, z + 1, K('rock_agd_md') if k % 4 else K('rock_agd_md_l'))
    for k in range(3, 13, 3):                                   # 刻んだ字の跡（暗い点）
        z = 2 + k
        y0 = 20 - k
        box(v, 10 + (k % 2) * 2, 12 + (k % 2) * 2, y0 + 4, y0 + 5, z, z + 1, K('rock_agd_md_j'))
        box(v, 15, 18, y0 + 4, y0 + 5, z, z + 1, K('rock_agd_md_j'))
    heap(v, rng, 5, [K('rock_agd_md'), K('rock_agd_md_d')], [0.6, 0.4], 5, xr=(6, 26), yr=(20, 28), span=(2, 5))
    return one(v)


def wall_hole_agd_md(name):
    """② 崩れた壁の穴 — 壁から崩れ落ちた石の低い山と、抜けた石を囲む黒い穴の縁の石。"""
    rng = rng_for(name)
    v = vol(h=12)
    heap(v, rng, 8, [K('rock_agd_md'), K('rock_agd_md_l'), K('rock_agd_md_d'), K('rock_agd_md_j')],
         [0.4, 0.15, 0.3, 0.15], 8, span=(3, 7))
    box(v, 13, 19, 13, 19, 0, 6, K('rock_agd_md_j'))             # 穴（暗い塊＝壁の奥の闇）
    box(v, 11, 21, 11, 21, 6, 8, K('rock_agd_md_d'))
    box(v, 12, 20, 12, 20, 8, 9, K('rock_agd_md_j'))
    return one(v)


def root_agd_md(name):
    """② 木の根 — 天井の割れ目から下りて床を這う根。**壁の面に沿わず**マスの中央から四方へ。"""
    v = vol(h=24)
    rod(v, (16, 16, 23), (16, 16, 8), 2.6, 2.0, K('bark_agd_md'))
    rod(v, (16, 16, 8), (4, 8, 1), 2.0, 1.0, K('bark_agd_md'))
    rod(v, (16, 16, 8), (27, 12, 1), 1.8, 0.9, K('bark_agd_md_l'))
    rod(v, (16, 16, 10), (12, 28, 1), 1.6, 0.8, K('bark_agd_md'))
    rod(v, (16, 16, 6), (24, 26, 1), 1.4, 0.7, K('bark_agd_md_l'))
    rod(v, (16, 16, 16), (22, 20, 12), 1.0, 0.6, K('bark_agd_md_l'))
    return one(v)


def damp_stain_agd_md(name):
    """② 水の染み — 床に広がった暗い染みと、滴が溜まった小さな水たまり。**接地層だけ**（低い）。"""
    rng = rng_for(name)
    v = vol(h=2)
    for _ in range(6):
        cx, cy = int(rng.integers(8, 24)), int(rng.integers(8, 24))
        disc(v, cx, cy, float(rng.uniform(3, 6)), 0, 1, K('damp_agd_md'))
    disc(v, 16, 16, 3.5, 0, 1, K('damp_agd_md_l'))
    disc(v, 16, 16, 2.0, 1, 2, K('damp_agd_md_l'))
    return one(v)


def pillar_broken_agd_md(name):
    """③ 折れた柱 — 途中で折れた石の丸柱。折れ口はぎざぎざ。火は無い。"""
    rng = rng_for(name)
    v = vol(h=34)
    column(v, (K('rock_agd_md_d'), K('rock_agd_md'), K('rock_agd_md_l')), 7.2, 5.6, 34, cap_h=0)
    v[:, :, 28:] = 0
    for z in range(22, 30):                                      # 折れ口（段々に欠ける）
        for _ in range(3):
            x, y = int(rng.integers(10, 22)), int(rng.integers(10, 22))
            v[x:x + int(rng.integers(2, 5)), y:y + int(rng.integers(2, 5)), z:34] = 0
    heap(v, rng, 4, [K('rock_agd_md'), K('rock_agd_md_d')], [0.6, 0.4], 4, xr=(4, 28), yr=(4, 28), span=(2, 5))
    return one(v)


def firepit_agd_md(name):
    """④ 焚き火の跡 — 黒い炭と灰。石を 1 個ずつ輪に並べた跡。火は消えている。"""
    rng = rng_for(name)
    v = vol(h=6)
    disc(v, 16, 16, 7.0, 0, 1, K('ash_agd_md'))
    disc(v, 16, 16, 4.0, 0, 2, K('char_agd_md'))
    for k in range(8):
        a = k * 0.785 + 0.3
        x, y = int(round(16 + 9.0 * np.cos(a))), int(round(16 + 9.0 * np.sin(a)))
        w = int(rng.integers(3, 5))
        box(v, x - w // 2, x - w // 2 + w, y - w // 2, y - w // 2 + w, 0, int(rng.integers(2, 4)), K('rock_agd_md_d'))
    rod(v, (10, 12, 1), (20, 20, 2), 1.3, 1.0, K('char_agd_md'))
    rod(v, (20, 10, 1), (12, 22, 2), 1.2, 1.0, K('char_agd_md'))
    return one(v)


def bedroll_agd_md(name):
    """④ 放置された寝床 — 藁を敷いて布を掛けた寝床。丸めた荷物が枕。"""
    v = vol(h=8)
    box(v, 5, 27, 10, 22, 0, 3, K('hay_agd_md'))
    box(v, 7, 25, 11, 21, 3, 5, K('cloth_agd_md'))
    box(v, 7, 25, 11, 12, 3, 5, K('cloth_agd_md_d'))
    box(v, 12, 18, 13, 19, 5, 6, K('cloth_agd_md_d'))            # 布の皺
    ball(v, 24, 16, 4, 4.0, K('cloth_agd_md_d'), squash=0.8)     # 枕
    return one(v)


def weapon_rusty_agd_md(name):
    """④ 錆びた武器 — 折れた剣と、錆びた斧の頭。"""
    v = vol(h=6)
    box(v, 6, 22, 15, 17, 0, 1, K('rust_agd_md'))                # 剣身
    box(v, 8, 20, 15, 17, 1, 2, K('rust_agd_md_l'))
    box(v, 22, 26, 13, 19, 0, 2, K('iron_agd_cl'))               # 鍔
    box(v, 26, 30, 15, 17, 0, 2, K('timber_agd_md'))             # 柄
    box(v, 10, 18, 22, 28, 0, 3, K('rust_agd_md'))               # 斧の頭
    box(v, 10, 18, 27, 28, 0, 3, K('rust_agd_md_l'))
    box(v, 4, 10, 24, 26, 0, 2, K('timber_agd_md'))
    return one(v)


# ================================================================== moria（mo）
def cavein_agd_mo(name):
    """① 落盤の岩と折れた坑木 — 岩の山に、折れた坑木が斜めに刺さる。"""
    rng = rng_for(name)
    v = vol(h=20)
    heap(v, rng, 10, [K('rock_agd_mo'), K('rock_agd_mo_l'), K('rock_agd_mo_d'), K('rock_agd_mo_dd')],
         [0.4, 0.15, 0.3, 0.15], 12, span=(4, 9))
    rod(v, (6, 10, 2), (22, 16, 18), 2.0, 2.0, K('timber_agd_mo'))
    rod(v, (24, 24, 1), (16, 20, 8), 1.8, 1.8, K('timber_agd_mo_d'))
    box(v, 21, 24, 15, 18, 17, 20, K('timber_agd_mo_l'))         # 折れ口（明るい木口）
    return one(v)


def mine_prop_agd_mo(name):
    """② 坑木の支柱 — 2 本の柱と梁（`pit_prop` と同じ骨格。木は明るい坑木の色）。"""
    rng = rng_for(name)
    v = vol(h=30)
    for x0 in (3, 25):
        box(v, x0, x0 + 4, 14, 18, 0, 26, K('timber_agd_mo'))
        box(v, x0, x0 + 4, 14, 18, 0, 2, K('timber_agd_mo_d'))
    box(v, 2, 30, 14, 18, 26, 30, K('timber_agd_mo_l'))
    box(v, 2, 30, 14, 18, 25, 26, K('timber_agd_mo_d'))
    for x0 in (7, 22):
        for k in range(4):
            box(v, x0 + (k if x0 == 7 else -k), x0 + (k if x0 == 7 else -k) + 1, 15, 17, 22 - k, 24 - k, K('timber_agd_mo_d'))
    if rng.random() < 0.5:
        box(v, 10, 20, 20, 22, 0, 2, K('timber_agd_mo_d'))
    return one(v)


def pickaxe_agd_mo(name):
    """② つるはしの立て掛け — 石に立て掛けた 2 本のつるはしと、手袋代わりの布。"""
    v = vol(h=26)
    box(v, 10, 22, 12, 20, 0, 6, K('rock_agd_mo_d'))             # 立て掛ける石
    rod(v, (12, 22, 1), (17, 16, 22), 1.1, 1.1, K('timber_agd_mo'))
    box(v, 12, 22, 14, 18, 21, 24, K('iron_agd_mo'))             # 頭（横）
    box(v, 12, 13, 14, 18, 20, 25, K('iron_agd_mo_l'))
    box(v, 21, 22, 14, 18, 20, 25, K('iron_agd_mo_l'))
    rod(v, (24, 8, 1), (19, 14, 18), 1.0, 1.0, K('timber_agd_mo_d'))
    box(v, 15, 25, 12, 16, 17, 20, K('rust_agd_mo'))             # 錆びた頭
    return one(v)


def ore_pile_agd_mo(name):
    """② 鉱石の積み — 選り分けて壁際に積んだ鉱石（金の光る面が混じる）。"""
    rng = rng_for(name)
    v = vol(h=12)
    for z in range(9):
        r = 8.0 * (1.0 - z / 9.0) + 1.0
        disc(v, 16, 16, r, z, z + 1, K('rock_agd_mo_d') if z % 2 else K('rock_agd_mo'))
    for _ in range(int(rng.integers(6, 10))):
        x, y, z = int(rng.integers(9, 22)), int(rng.integers(9, 22)), int(rng.integers(0, 8))
        if v[x, y, z] != 0:
            v[x:x + 2, y:y + 2, z:z + 2] = K('gold_agd_mo') if rng.random() < 0.6 else K('gold_agd_mo_d')
    return one(v)


def timber_frame_agd_mo(name):
    """③ 木組みの支柱 — 部屋の縁に立つ太い坑木の柱（横木で組んである）。丈は `pillar_stone` と同じ。"""
    v = vol(h=54)
    box(v, 11, 21, 11, 21, 0, 3, K('rock_agd_mo_d'))
    box(v, 13, 19, 13, 19, 3, 50, K('timber_agd_mo'))
    box(v, 13, 19, 18, 19, 3, 50, K('timber_agd_mo_l'))          # 南面（+y ＝ カメラ側）の明るい面
    for z in (14, 28, 42):                                       # 横木
        box(v, 9, 23, 12, 20, z, z + 3, K('timber_agd_mo_d'))
        box(v, 12, 20, 9, 23, z, z + 3, K('timber_agd_mo_d'))
    box(v, 10, 22, 10, 22, 50, 54, K('timber_agd_mo_l'))         # 笠木
    return one(v)


def lantern_agd_mo(name):
    """③ 吊りランタン（本体）— 坑木の腕から吊った鉄の籠。灯は `lantern_glow_agd_mo`（籠の中）。"""
    v = vol(h=44)
    box(v, 13, 19, 13, 19, 0, 2, K('rock_agd_mo_d'))
    box(v, 14, 18, 14, 18, 2, 34, K('timber_agd_mo'))            # 竿
    box(v, 14, 18, 14, 18, 34, 37, K('timber_agd_mo_d'))
    box(v, 15, 17, 15, 17, 37, 42, K('iron_agd_mo'))             # 吊り環
    box(v, 12, 20, 12, 20, 24, 26, K('iron_agd_mo'))             # 籠の底
    for x, y in ((12, 12), (12, 19), (19, 12), (19, 19)):
        box(v, x, x + 1, y, y + 1, 26, 35, K('iron_agd_mo_l'))
    box(v, 12, 20, 12, 20, 35, 37, K('iron_agd_mo'))             # 籠の天板
    return one(v)


def lantern_glow_agd_mo(name):
    v = vol(h=40)
    box(v, 14, 18, 14, 18, 26, 34, K('lit_agd_mo'))
    box(v, 15, 17, 15, 17, 28, 33, K('fl_agd_mo'))
    return one(v)


def minecart_agd_mo(name):
    """④ 鉱石を積んだトロッコと線路の切れ端 — 短い線路の上に鉄の箱。中に鉱石。"""
    v = vol(h=20)
    for y in (10, 22):                                          # レール
        box(v, 0, 32, y, y + 2, 0, 2, K('iron_agd_mo'))
    for x in range(1, 32, 6):                                   # 枕木
        box(v, x, x + 3, 8, 24, 0, 1, K('timber_agd_mo_d'))
    for x in (7, 22):                                           # 車輪
        for y in (9, 23):
            disc_local = np.zeros((V, V, 20), np.int16)
            for zz in range(0, 6):
                for xx in range(x - 3, x + 4):
                    if (xx - x) ** 2 + (zz - 3) ** 2 <= 9:
                        disc_local[xx, y, zz] = K('iron_agd_mo_l')
            v[disc_local > 0] = disc_local[disc_local > 0]
    box(v, 4, 28, 9, 23, 5, 15, K('iron_agd_mo'))               # 箱
    box(v, 5, 27, 10, 22, 8, 15, 0)
    box(v, 4, 28, 9, 23, 14, 15, K('iron_agd_mo_l'))
    box(v, 4, 28, 9, 10, 9, 10, K('rust_agd_mo'))               # 錆の帯
    for x, y, c in ((7, 12, K('rock_agd_mo_d')), (12, 16, K('rock_agd_mo')), (18, 12, K('gold_agd_mo')),
                    (22, 17, K('rock_agd_mo_d')), (15, 19, K('gold_agd_mo_d')), (9, 18, K('rock_agd_mo'))):
        box(v, x, x + 5, y, y + 4, 8, 17, c)
    return one(v)


def ore_heap_agd_mo(name):
    """④ 鉱石の山 — 掘り出したままの荒い山（②の「積み」より大きく、揃っていない）。"""
    rng = rng_for(name)
    v = vol(h=14)
    heap(v, rng, 12, [K('rock_agd_mo'), K('rock_agd_mo_d'), K('rock_agd_mo_dd'), K('gold_agd_mo_d'), K('gold_agd_mo')],
         [0.35, 0.3, 0.15, 0.1, 0.1], 11, span=(3, 7))
    return one(v)


def toolbox_agd_mo(name):
    """④ 道具箱 — 蓋の開いた木の箱。中にハンマーと楔。脇に置いたバケツ。"""
    v = vol(h=16)
    box(v, 6, 22, 10, 22, 0, 8, K('timber_agd_mo'))
    box(v, 7, 21, 11, 21, 3, 8, K('timber_agd_mo_d'))
    box(v, 6, 22, 10, 22, 0, 1, K('timber_agd_mo_d'))
    box(v, 8, 20, 12, 14, 5, 8, K('iron_agd_mo'))               # ハンマーの頭
    box(v, 9, 19, 16, 18, 5, 7, K('timber_agd_mo_l'))            # 柄
    box(v, 12, 15, 18, 20, 5, 9, K('iron_agd_mo_l'))             # 楔
    box(v, 6, 22, 22, 24, 0, 14, K('timber_agd_mo_l'))           # 開いた蓋（立っている）
    disc(v, 26, 16, 4.0, 0, 8, K('iron_agd_mo'), hollow=3.0)     # バケツ
    disc(v, 26, 16, 4.0, 0, 1, K('iron_agd_mo'))
    disc(v, 26, 16, 4.5, 7, 8, K('iron_agd_mo_l'), hollow=3.5)
    return one(v)


# ================================================================== labyrinth（lb）
def rune_stone_agd_lb(name):
    """① 石の袋小路に刻まれた記号 1 つ — 床に据えた黒い石板に、白い 1 つの記号を彫る。"""
    rng = rng_for(name)
    v = vol(h=8)
    box(v, 6, 26, 6, 26, 0, 4, K('obs_agd_lb'))
    box(v, 7, 25, 7, 25, 3, 4, K('obs_agd_lb_l'))
    #! 記号: 3 種のどれか（円・三角・十字）。**1 つだけ**。
    kind = int(rng.integers(0, 3))
    c = K('marb_agd_lb')
    if kind == 0:
        disc(v, 16, 16, 6.0, 3, 5, c, hollow=4.5)
    elif kind == 1:
        for k in range(7):
            box(v, 16 - k, 16 + k + 1, 10 + k * 2, 10 + k * 2 + 2, 3, 5, c if k in (0, 6) else 0)
            box(v, 16 - k, 16 - k + 1, 10 + k * 2, 10 + k * 2 + 2, 3, 5, c)
            box(v, 16 + k, 16 + k + 1, 10 + k * 2, 10 + k * 2 + 2, 3, 5, c)
        box(v, 10, 23, 22, 24, 3, 5, c)
    else:
        box(v, 10, 22, 15, 17, 3, 5, c)
        box(v, 15, 17, 10, 22, 3, 5, c)
    return one(v)


def relief_agd_lb(name):
    """② 壁の浮き彫り — 自立する黒い石板に、人の輪郭の浮き彫り（明るい面）。"""
    v = vol(h=30)
    box(v, 8, 24, 13, 19, 0, 28, K('obs_agd_lb'))
    box(v, 8, 24, 18, 19, 0, 28, K('obs_agd_lb_d'))              # 南面（+y ＝ カメラ側。浮き彫りの地）
    box(v, 14, 18, 18, 19, 20, 25, K('obs_agd_lb_l'))            # 頭
    box(v, 12, 20, 18, 19, 12, 20, K('obs_agd_lb_l'))            # 胴
    box(v, 10, 12, 18, 19, 14, 20, K('obs_agd_lb_l'))            # 腕
    box(v, 20, 22, 18, 19, 14, 20, K('obs_agd_lb_l'))
    box(v, 13, 15, 18, 19, 4, 12, K('obs_agd_lb_l'))             # 脚
    box(v, 17, 19, 18, 19, 4, 12, K('obs_agd_lb_l'))
    box(v, 8, 24, 13, 19, 27, 28, K('obs_agd_lb_l'))
    return one(v)


def grate_window_agd_lb(name):
    """② 鉄格子の覗き窓 — 黒い石の枠に鉄の格子。奥は闇。"""
    v = vol(h=30)
    box(v, 8, 24, 13, 19, 0, 28, K('obs_agd_lb'))
    box(v, 11, 21, 13, 19, 10, 22, K('dark'))                    # 窓の奥の闇
    for x in range(12, 21, 3):                                   # 格子は +y（カメラ側）の面
        box(v, x, x + 1, 18, 19, 10, 22, K('iron_agd_lb_l'))
    for z in range(12, 22, 4):
        box(v, 11, 21, 18, 19, z, z + 1, K('iron_agd_lb'))
    box(v, 10, 22, 18, 19, 9, 10, K('obs_agd_lb_l'))
    box(v, 10, 22, 18, 19, 22, 23, K('obs_agd_lb_l'))
    return one(v)


def floor_groove_agd_lb(name):
    """② 床の溝 — 床に切った直線の溝（水が細く流れる）。**接地層だけ**。"""
    v = vol(h=2)
    box(v, 4, 28, 14, 18, 0, 1, K('flag_agd_lb_j'))
    box(v, 5, 27, 15, 17, 0, 1, K('wa_agd_lb'))
    box(v, 4, 28, 13, 14, 0, 2, K('obs_agd_lb_l'))               # 溝の縁石
    box(v, 4, 28, 18, 19, 0, 2, K('obs_agd_lb_l'))
    return one(v)


def pillar_agd_lb(name):
    """③ 細い黒石の角柱 — 四角い柱。目地無し。上に小さな柱頭。"""
    v = vol(h=54)
    box(v, 10, 22, 10, 22, 0, 3, K('obs_agd_lb_d'))
    box(v, 12, 20, 12, 20, 3, 50, K('obs_agd_lb'))
    box(v, 12, 20, 19, 20, 3, 50, K('obs_agd_lb_l'))            # 南面（+y ＝ カメラ側）は磨きの明るい面
    box(v, 12, 20, 19, 20, 20, 21, K('obs_agd_lb'))
    box(v, 12, 20, 19, 20, 36, 37, K('obs_agd_lb'))
    box(v, 10, 22, 10, 22, 50, 54, K('obs_agd_lb_d'))
    return one(v)


def magelight_agd_lb(name):
    """③ 青白い魔法の灯（本体）— 黒石の細い台。灯は `magelight_glow_agd_lb`（台の上に浮く球）。"""
    v = vol(h=32)
    box(v, 12, 20, 12, 20, 0, 3, K('obs_agd_lb_d'))
    box(v, 14, 18, 14, 18, 3, 26, K('obs_agd_lb'))
    box(v, 14, 18, 17, 18, 3, 26, K('obs_agd_lb_l'))            # +y ＝ カメラ側の面
    box(v, 12, 20, 12, 20, 26, 28, K('obs_agd_lb_l'))            # 皿
    return one(v)


def magelight_glow_agd_lb(name):
    """③ 青白い光の球（自発光は橙のべた書きなので、**色の明るさ**で青白さを言う）。"""
    v = vol(h=40)
    ball(v, 16, 16, 33, 4.2, K('glass_agd_lb'))
    ball(v, 16, 16, 33, 2.4, K('glass_agd_lb_l'))
    return one(v)


def pedestal_agd_lb(name):
    """④ 台座だけ残った像 — 黒石の台座に、折れた足首から下だけ残る白い像。"""
    v = vol(h=20)
    box(v, 8, 24, 8, 24, 0, 4, K('obs_agd_lb_d'))
    box(v, 9, 23, 9, 23, 4, 10, K('obs_agd_lb'))
    box(v, 8, 24, 8, 24, 10, 12, K('obs_agd_lb_l'))
    box(v, 12, 15, 13, 19, 12, 16, K('marb_agd_lb'))             # 足
    box(v, 17, 20, 13, 19, 12, 17, K('marb_agd_lb'))
    box(v, 12, 15, 13, 19, 15, 16, K('marb_agd_lb_d'))           # 折れ口
    box(v, 17, 20, 13, 19, 16, 17, K('marb_agd_lb_d'))
    return one(v)


def basin_agd_lb(name):
    """④ 石の水盤 — 黒石の丸い盤に、澄んだ水。"""
    v = vol(h=12)
    disc(v, 16, 16, 9.5, 0, 2, K('obs_agd_lb_d'))
    disc(v, 16, 16, 8.5, 2, 9, K('obs_agd_lb'), hollow=6.5)
    disc(v, 16, 16, 6.5, 2, 4, K('obs_agd_lb'))
    disc(v, 16, 16, 9.0, 8, 10, K('obs_agd_lb_l'), hollow=6.5)
    disc(v, 16, 16, 6.5, 4, 8, K('wa_agd_lb'))
    disc(v, 16, 16, 3.0, 7, 8, K('glass_agd_lb'))               # 水面の映り
    return one(v)


def circle_agd_lb(name):
    """④ 床の模様の円 — 床に嵌めた明るい石の輪と、中の十字。**接地層だけ**。"""
    v = vol(h=2)
    disc(v, 16, 16, 12.0, 0, 1, K('flag_agd_lb'), hollow=10.0)
    disc(v, 16, 16, 7.0, 0, 1, K('marb_agd_lb_d'), hollow=5.5)
    box(v, 6, 26, 15, 17, 0, 1, K('marb_agd_lb_d'))
    box(v, 15, 17, 6, 26, 0, 1, K('marb_agd_lb_d'))
    return one(v)


# ================================================================== cavern（cv）
def stalactite_pile_agd_cv(name):
    """① 鍾乳石が折れて積もった山 — 折れた円錐が重なる（岩の山と違い**先が尖る**）。"""
    rng = rng_for(name)
    v = vol(h=14)
    for _ in range(7):
        x, y = int(rng.integers(6, 26)), int(rng.integers(6, 26))
        ln = int(rng.integers(6, 12))
        ang = float(rng.uniform(0, 6.28))
        x1, y1 = x + int(ln * np.cos(ang)), y + int(ln * np.sin(ang))
        z0 = int(rng.integers(0, 4))
        rod(v, (x, y, z0 + 2), (x1, y1, z0 + 1), 2.6, 0.6, K('rock_agd_cv_l') if rng.random() < 0.4 else K('rock_agd_cv'))
    v[:, :, 10:] = 0
    return one(v)


def stalagmite_agd_cv(name):
    """② 石筍 — 濡れて光る石筍 3 本（既定の `stalagmite` より細く高く、先が明るい）。"""
    v = vol(h=26)
    cone(v, 12, 12, 0, 24, 4.4, 0.8, K('rock_agd_cv'), K('rock_agd_cv_l'))
    cone(v, 21, 18, 0, 16, 3.4, 0.7, K('rock_agd_cv_d'), K('rock_agd_cv_l'))
    cone(v, 14, 22, 0, 10, 2.6, 0.6, K('rock_agd_cv'), K('damp_agd_cv'))
    return one(v)


def glowshroom_agd_cv(name):
    """② 光るキノコ — 岩の割れ目に群れる青緑の茸（自発光は無いので明るい色で言う）。"""
    rng = rng_for(name)
    v = vol(h=13)
    for cx, cy, h, r in ((9, 10, 10, 4.0), (20, 14, 12, 4.6), (13, 20, 8, 3.2), (23, 23, 6, 2.6), (8, 22, 7, 2.8)):
        box(v, cx - 1, cx + 1, cy - 1, cy + 1, 0, h - 3, K('stem_agd_cv'))
        disc(v, cx, cy, r, h - 3, h - 1, K('mgl_agd_cv'))
        disc(v, cx, cy, r * 0.5, h - 1, h, K('mgl_agd_cv_d'))
        disc(v, cx, cy, r + 0.5, h - 3, h - 2, K('mgl_agd_cv_d'), hollow=r - 0.5)
    return one(v)


def ledge_drip_agd_cv(name):
    """② 水の滴る岩棚 — 張り出した岩棚と、棚の下の水たまり。棚の縁から垂れる細い雫。"""
    v = vol(h=24)
    box(v, 6, 26, 8, 24, 0, 2, K('rock_agd_cv_dd'))
    disc(v, 16, 20, 5.0, 0, 1, K('wa_agd_cv'))
    disc(v, 16, 20, 2.5, 0, 2, K('wa_agd_cv_l'))
    box(v, 8, 24, 14, 24, 2, 14, K('rock_agd_cv_d'))             # 棚の脚（岩）
    box(v, 5, 27, 10, 26, 14, 20, K('rock_agd_cv'))              # 張り出した棚
    box(v, 5, 27, 10, 26, 19, 20, K('damp_agd_cv'))
    for x in (9, 16, 22):                                        # 雫（棚の +y ＝ カメラ側の縁から）
        box(v, x, x + 1, 25, 26, 8, 14, K('wa_agd_cv_l'))
    return one(v)


def column_agd_cv(name):
    """③ 石柱（鍾乳石と石筍がつながったもの）— 上下から伸びて中でくびれる柱。丈 54。"""
    v = vol(h=54)
    cone(v, 16, 16, 0, 30, 8.0, 3.2, K('rock_agd_cv'), K('rock_agd_cv_l'))
    cone(v, 16, 16, 24, 54, 3.0, 7.5, K('rock_agd_cv_d'))
    for z in range(6, 50, 7):                                    # 濡れの帯
        disc(v, 16, 16, 9.0, z, z + 1, K('damp_agd_cv'), hollow=2.0)
    for z in range(0, 54):
        disc(v, 16, 16, 12.0, z, z + 1, 0, hollow=8.6)           # はみ出しを切る（マスの中に収める）
    return one(v)


def shroom_cluster_agd_cv(name):
    """③ 火の枠の本体 — 光るキノコの群生の**柄と岩**（傘は `shroom_glow_agd_cv` に分けて自発光を受ける）。"""
    rng = rng_for(name)
    v = vol(h=20)
    heap(v, rng, 5, [K('rock_agd_cv_d'), K('rock_agd_cv_dd')], [0.6, 0.4], 6, span=(3, 7))
    for cx, cy, h in ((12, 12, 14), (20, 16, 18), (14, 22, 11), (22, 24, 9)):
        box(v, cx - 1, cx + 1, cy - 1, cy + 1, 0, h, K('stem_agd_cv'))
    return one(v)


def shroom_glow_agd_cv(name):
    v = vol(h=22)
    for cx, cy, h, r in ((12, 12, 14, 4.6), (20, 16, 18, 5.2), (14, 22, 11, 3.6), (22, 24, 9, 3.0)):
        disc(v, cx, cy, r, h, h + 2, K('mgl_agd_cv'))
        disc(v, cx, cy, r * 0.5, h + 2, h + 3, K('mgl_agd_cv_d'))
    return one(v)


def pool_agd_cv(name):
    """④ 地底の水たまり — 岩の縁に囲まれた水。**低い**（水面は接地層の上 1 段）。"""
    rng = rng_for(name)
    v = vol(h=5)
    disc(v, 16, 16, 11.0, 0, 3, K('rock_agd_cv_d'), hollow=8.5)
    for _ in range(8):
        a = float(rng.uniform(0, 6.28))
        x, y = int(16 + 10 * np.cos(a)), int(16 + 10 * np.sin(a))
        box(v, x - 1, x + 2, y - 1, y + 2, 0, int(rng.integers(2, 5)), K('rock_agd_cv'))
    disc(v, 16, 16, 8.5, 0, 1, K('wa_agd_cv'))
    disc(v, 16, 16, 8.5, 1, 2, K('wa_agd_cv'))
    disc(v, 14, 15, 3.0, 1, 2, K('wa_agd_cv_l'))
    return one(v)


def mossrock_agd_cv(name):
    """④ 苔むした岩 — 丸い岩の上面と北側が苔に覆われる。"""
    v = vol(h=14)
    ball(v, 16, 16, 4, 8.0, K('rock_agd_cv'), squash=0.8)
    ball(v, 16, 16, 4, 8.0, 0, squash=0.8)
    for x in range(V):
        for y in range(V):
            for z in range(14):
                d2 = (x - 16 + 0.5) ** 2 + (y - 16 + 0.5) ** 2 + ((z - 4) / 0.8) ** 2
                if d2 <= 64 and z >= 0:
                    v[x, y, z] = K('moss_agd_cv') if (z >= 7 or y < 12) else K('rock_agd_cv')
    box(v, 12, 20, 14, 18, 9, 11, K('moss_agd_cv_d'))
    return one(v)


def bones_beast_agd_cv(name):
    """④ 獣の骨 — 大きな肋骨が弧を描き、細長い頭蓋が転がる。"""
    v = vol(h=12)
    for k, x in enumerate(range(6, 26, 4)):
        hh = 8 - abs(k - 2) * 2
        for z in range(hh):
            r = 8.0 * np.sqrt(max(0.0, 1 - (z / float(hh)) ** 2))
            box(v, x, x + 2, int(16 - r), int(16 - r) + 2, z, z + 1, K('bone_agd_cv'))
            box(v, x, x + 2, int(16 + r) - 2, int(16 + r), z, z + 1, K('bone_agd_cv'))
        box(v, x, x + 2, 15, 17, hh - 1, hh, K('bone_agd_cv_d'))
    box(v, 24, 31, 9, 13, 0, 4, K('bone_agd_cv'))                # 細長い頭蓋
    box(v, 28, 31, 9, 13, 0, 3, K('bone_agd_cv_d'))
    box(v, 26, 27, 9, 10, 2, 3, K('dark'))
    return one(v)


# ================================================================== lair（lr）
def fur_drift_agd_lr(name):
    """① 骨と毛の吹き溜まり — 毛の塊（暗い茶の低い山）に骨が刺さる。"""
    rng = rng_for(name)
    v = vol(h=10)
    heap(v, rng, 10, [K('fur_agd_lr'), K('fur_agd_lr_l')], [0.6, 0.4], 6, span=(3, 8))
    bone_scatter(v, rng, 4, K('bone_agd_lr'), K('bone_agd_lr_d'), zr=(2, 5))
    return one(v)


def clawmarks_agd_lr(name):
    """② 爪痕の壁 — 爪痕を刻んだ泥の板（壁から剥がれ落ちた面）と、床に落ちた爪。"""
    v = vol(h=26)
    box(v, 7, 25, 13, 19, 0, 24, K('clay_agd_lr'))
    box(v, 7, 25, 18, 19, 0, 24, K('clay_agd_lr_l'))             # +y ＝ カメラ側の面
    for k, x in enumerate((10, 13, 16, 19)):                     # 4 本の爪痕（斜め・深い）
        for z in range(6, 20):
            xx = x + (z - 6) // 4
            box(v, xx, xx + 1, 17, 19, z, z + 1, K('clay_agd_lr_dd'))
    box(v, 4, 7, 20, 26, 0, 2, K('bone_agd_lr_d'))               # 落ちた爪
    return one(v)


def bones_gnawed_agd_lr(name):
    """② 食べ残しの骨 — 噛み砕かれた骨の山。肉の残りが少し。"""
    rng = rng_for(name)
    v = vol(h=8)
    bone_scatter(v, rng, 6, K('bone_agd_lr'), K('bone_agd_lr_d'))
    box(v, 12, 18, 12, 16, 0, 3, K('rot_agd_lr'))
    box(v, 13, 16, 13, 15, 3, 4, K('crimson_agd_lr'))
    skull(v, 18, 18, 2, K('bone_agd_lr'), K('bone_agd_lr_d'), K('dark'))
    return one(v)


def twig_bundle_agd_lr(name):
    """② 巣材の枝の束 — 束ねた枝を寄せて積んだもの。"""
    rng = rng_for(name)
    v = vol(h=12)
    for _ in range(9):
        x, y = int(rng.integers(4, 12)), int(rng.integers(6, 26))
        ln = int(rng.integers(14, 22))
        z = int(rng.integers(0, 6))
        rod(v, (x, y, z + 1), (x + ln, y + int(rng.integers(-3, 4)), z + 1), 1.2, 1.0,
            K('bark_agd_lr') if rng.random() < 0.6 else K('bark_agd_lr_l'))
    box(v, 12, 14, 8, 26, 0, 8, K('hay_agd_lr_d'))               # 縛った藁
    return one(v)


def bone_pillar_agd_lr(name):
    """③ 骨を組んだ柱 — 大きな骨を縦に組み、頭蓋を載せた柱。丈 54。"""
    v = vol(h=54)
    disc(v, 16, 16, 8.0, 0, 3, K('clay_agd_lr_d'))
    for x, y in ((12, 12), (20, 12), (12, 20), (20, 20)):       # 4 本の長い骨
        box(v, x - 1, x + 2, y - 1, y + 2, 3, 46, K('bone_agd_lr'))
        box(v, x - 2, x + 3, y - 2, y + 3, 3, 6, K('bone_agd_lr_d'))
        box(v, x - 2, x + 3, y - 2, y + 3, 43, 46, K('bone_agd_lr_d'))
    for z in (16, 30):                                           # 縛り
        box(v, 10, 22, 10, 22, z, z + 2, K('hay_agd_lr_d'))
    box(v, 10, 22, 10, 22, 46, 48, K('bone_agd_lr_d'))
    skull(v, 13, 13, 50, K('bone_agd_lr'), K('bone_agd_lr_d'), K('dark'))
    return one(v)


def tallow_fire_agd_lr(name):
    """③ 獣脂の火（本体）— 頭蓋を器にした獣脂の灯。火は `tallow_flame_agd_lr`。"""
    v = vol(h=20)
    heap_local = vol(h=20)
    disc(heap_local, 16, 16, 7.0, 0, 4, K('clay_agd_lr_d'))
    v[heap_local > 0] = heap_local[heap_local > 0]
    skull(v, 13, 13, 6, K('bone_agd_lr'), K('bone_agd_lr_d'), K('dark'))
    box(v, 14, 19, 14, 18, 11, 12, K('cream_agd_lr_d'))          # 頭頂に溜めた脂
    return one(v)


def tallow_flame_agd_lr(name):
    v = vol(h=22)
    box(v, 15, 18, 15, 17, 12, 17, K('emb_agd_lr'))
    box(v, 15, 18, 15, 17, 14, 20, K('fl_agd_lr'))
    box(v, 16, 17, 16, 17, 17, 21, K('fl_core'))
    return one(v)


def nest_agd_lr(name):
    """④ 巣（寝藁の窪み）— 藁と毛を円く敷き、中央が窪む。"""
    v = vol(h=8)
    disc(v, 16, 16, 12.0, 0, 3, K('hay_agd_lr'))
    disc(v, 16, 16, 12.0, 3, 5, K('hay_agd_lr_d'), hollow=6.5)
    disc(v, 16, 16, 9.0, 4, 6, K('hay_agd_lr'), hollow=7.0)
    disc(v, 16, 16, 6.0, 0, 2, K('fur_agd_lr'))
    return one(v)


def eggshell_agd_lr(name):
    """④ 卵の殻 — 割れた大きな卵の殻（下半分）と欠片。"""
    v = vol(h=12)
    ball(v, 14, 16, 3, 7.0, K('cream_agd_lr'), squash=1.0)
    ball(v, 14, 16, 3, 6.0, 0)
    v[:, :, 0] = 0
    v[:, :, 7:] = 0
    ball(v, 14, 16, 0, 7.0, K('cream_agd_lr_d'), squash=0.3)
    box(v, 24, 30, 8, 12, 0, 2, K('cream_agd_lr'))
    box(v, 22, 27, 20, 26, 0, 2, K('cream_agd_lr_d'))
    return one(v)


def carcass_agd_lr(name):
    """④ 獲物の残骸 — 肋骨の露出した獣の亡骸。"""
    v = vol(h=10)
    ball(v, 14, 16, 2, 8.0, K('fur_agd_lr'), squash=0.5)
    ball(v, 14, 16, 2, 6.0, K('rot_agd_lr'), squash=0.5)
    for x in range(9, 22, 3):
        box(v, x, x + 1, 11, 21, 3, 6, K('bone_agd_lr'))
    box(v, 22, 30, 13, 19, 0, 4, K('fur_agd_lr_l'))              # 頭
    box(v, 28, 30, 13, 15, 2, 3, K('dark'))
    return one(v)


# ================================================================== gauntlet（gt）
def portcullis_agd_gt(name):
    """① 落とし格子の下りた門 — 石の門柱の間に鉄の格子が下りている。奥は闇。"""
    v = vol(h=32)
    box(v, 2, 8, 12, 20, 0, 30, K('rock_agd_gt'))                # 門柱
    box(v, 24, 30, 12, 20, 0, 30, K('rock_agd_gt'))
    box(v, 2, 30, 12, 20, 28, 32, K('rock_agd_gt_d'))            # 楣
    box(v, 8, 24, 14, 18, 0, 28, K('dark'))                      # 奥の闇
    for x in range(9, 24, 3):                                    # 格子は +y（カメラ側）の面
        box(v, x, x + 1, 17, 19, 0, 28, K('iron_agd_gt'))
    for z in range(3, 28, 5):
        box(v, 8, 24, 17, 19, z, z + 1, K('iron_agd_gt_l'))
    for x in range(9, 24, 3):                                    # 尖った先端
        box(v, x, x + 1, 17, 19, 0, 1, K('iron_agd_gt_d'))
    return one(v)


def arrowslit_agd_gt(name):
    """② 矢狭間 — 縦長の細い穴を開けた石の板（壁から崩れた 1 枚）。"""
    v = vol(h=30)
    box(v, 8, 24, 13, 19, 0, 28, K('rock_agd_gt'))
    box(v, 8, 24, 18, 19, 0, 28, K('rock_agd_gt_l'))             # +y ＝ カメラ側の面
    box(v, 15, 17, 13, 19, 6, 24, K('dark'))                     # 狭間（貫通）
    box(v, 13, 19, 18, 19, 14, 16, K('dark'))                    # 十字の横
    box(v, 8, 24, 13, 19, 27, 28, K('rock_agd_gt_d'))
    return one(v)


def iron_sconce_agd_gt(name):
    """② 鉄の燭台（消えている）— 鉄の脚に受け皿。灰だけ。"""
    v = vol(h=28)
    for x, y in ((12, 12), (20, 12), (16, 21)):                  # 三脚
        rod(v, (x, y, 0), (16, 16, 18), 1.2, 1.0, K('iron_agd_gt'))
    box(v, 15, 17, 15, 17, 18, 22, K('iron_agd_gt'))
    box(v, 12, 20, 12, 20, 22, 24, K('iron_agd_gt'))
    box(v, 11, 21, 11, 21, 24, 26, K('iron_agd_gt_l'), )
    box(v, 12, 20, 12, 20, 25, 26, K('iron_agd_gt_d'))
    box(v, 13, 19, 13, 19, 25, 27, K('ash_agd_md'))
    return one(v)


def spear_rack_agd_gt(name):
    """② 槍立て — 木の枠に槍を数本立て掛けたもの。"""
    v = vol(h=40)
    box(v, 6, 26, 14, 18, 0, 3, K('timber_agd_gt'))
    box(v, 6, 26, 14, 18, 22, 25, K('timber_agd_gt_d'))
    box(v, 6, 8, 14, 18, 0, 25, K('timber_agd_gt_d'))
    box(v, 24, 26, 14, 18, 0, 25, K('timber_agd_gt_d'))
    for x in (10, 14, 18, 22):
        box(v, x, x + 1, 15, 17, 3, 34, K('timber_agd_gt_l'))
        box(v, x, x + 1, 15, 17, 34, 39, K('steel_agd_gt'))
        box(v, x - 1, x + 2, 15, 17, 33, 35, K('steel_agd_gt_d'))
    return one(v)


def pillar_agd_gt(name):
    """③ 鉄帯の石柱 — 太い角柱に鉄の帯を 3 本巻く。丈 54。"""
    v = vol(h=54)
    box(v, 9, 23, 9, 23, 0, 4, K('rock_agd_gt_d'))
    box(v, 11, 21, 11, 21, 4, 50, K('rock_agd_gt'))
    box(v, 11, 21, 20, 21, 4, 50, K('rock_agd_gt_l'))            # +y ＝ カメラ側の面
    for z in (12, 28, 44):
        box(v, 10, 22, 10, 22, z, z + 3, K('iron_agd_gt'))
        for x in (12, 16, 20):
            box(v, x, x + 1, 21, 22, z + 1, z + 2, K('iron_agd_gt_l'))
    box(v, 9, 23, 9, 23, 50, 54, K('rock_agd_gt_d'))
    return one(v)


def brazier_agd_gt(name):
    """③ かがり火（本体）— 鉄の脚と籠。火は `brazier_flame_agd_gt`（籠の中）。"""
    v = vol(h=32)
    box(v, 12, 20, 12, 20, 0, 2, K('rock_agd_gt_d'))
    for x, y in ((11, 11), (21, 11), (11, 21), (21, 21)):
        rod(v, (x, y, 0), (16, 16, 16), 1.2, 1.0, K('iron_agd_gt'))
    box(v, 15, 17, 15, 17, 14, 20, K('iron_agd_gt'))
    disc(v, 16, 16, 7.0, 20, 22, K('iron_agd_gt'))
    for k in range(8):                                           # 籠の骨
        a = k * 0.785
        x, y = int(round(16 + 6.5 * np.cos(a))), int(round(16 + 6.5 * np.sin(a)))
        box(v, x, x + 1, y, y + 1, 22, 30, K('iron_agd_gt_l'))
    disc(v, 16, 16, 7.5, 29, 31, K('iron_agd_gt'), hollow=6.0)
    disc(v, 16, 16, 6.0, 22, 24, K('char_agd_md'))               # 燃えている薪
    return one(v)


def brazier_flame_agd_gt(name):
    v = vol(h=40)
    disc(v, 16, 16, 4.5, 24, 30, K('fl_edge'))
    disc(v, 16, 16, 3.0, 26, 35, K('fl_mid'))
    disc(v, 16, 16, 1.6, 28, 38, K('fl_core'))
    return one(v)


def barricade_agd_gt(name):
    """④ バリケードの残骸 — 交差させた杭と倒れた板、割れた盾。"""
    v = vol(h=22)
    rod(v, (4, 10, 1), (28, 18, 20), 1.6, 1.6, K('timber_agd_gt'))
    rod(v, (4, 22, 1), (28, 12, 18), 1.6, 1.6, K('timber_agd_gt_d'))
    box(v, 26, 30, 10, 20, 17, 21, K('steel_agd_gt_d'))          # 杭の先の鉄
    box(v, 8, 24, 22, 26, 0, 3, K('timber_agd_gt_l'))            # 倒れた板
    box(v, 8, 24, 26, 28, 0, 2, K('timber_agd_gt'))
    disc(v, 22, 8, 5.0, 0, 2, K('steel_agd_gt'))
    box(v, 20, 24, 4, 12, 1, 2, K('cloth_agd_gt'))               # 盾の紋（布の赤）
    return one(v)


def armour_heap_agd_gt(name):
    """④ 武具の山 — 兜・胸当て・剣を積んだ山。"""
    rng = rng_for(name)
    v = vol(h=14)
    heap(v, rng, 6, [K('steel_agd_gt_d'), K('iron_agd_gt'), K('rust_agd_gt')], [0.5, 0.3, 0.2], 8, span=(4, 8))
    ball(v, 11, 12, 6, 4.5, K('steel_agd_gt'), squash=0.9)      # 兜
    ball(v, 11, 12, 6, 3.5, 0, squash=0.9)
    v[:, :, 0:2][v[:, :, 0:2] == 0] = 0
    box(v, 9, 14, 9, 10, 4, 6, K('dark'))                        # 兜の目の隙間
    box(v, 16, 26, 14, 24, 3, 6, K('steel_agd_gt'))              # 胸当て
    box(v, 18, 24, 15, 23, 6, 8, K('steel_agd_gt_d'))
    box(v, 4, 20, 22, 24, 0, 1, K('steel_agd_gt'))               # 剣
    box(v, 18, 22, 21, 25, 0, 2, K('iron_agd_gt_d'))
    return one(v)


def guard_desk_agd_gt(name):
    """④ 詰め所の机 — 板の机に、巻いた書付・蝋燭立て（消えている）・鍵束。脇に椅子。"""
    v = vol(h=16)
    box(v, 4, 24, 10, 22, 10, 13, K('timber_agd_gt'))
    box(v, 4, 24, 10, 22, 12, 13, K('timber_agd_gt_l'))
    for x0 in (5, 21):
        box(v, x0, x0 + 3, 11, 13, 0, 10, K('timber_agd_gt_d'))
        box(v, x0, x0 + 3, 19, 21, 0, 10, K('timber_agd_gt_d'))
    box(v, 7, 13, 13, 19, 13, 14, K('cream_agd_lr'))             # 書付
    box(v, 7, 13, 13, 19, 14, 15, K('cream_agd_lr_d'))
    box(v, 17, 19, 15, 17, 13, 16, K('iron_agd_gt'))             # 蝋燭立て（消えている）
    box(v, 14, 16, 12, 14, 13, 14, K('steel_agd_gt_d'))          # 鍵束
    box(v, 26, 31, 12, 20, 6, 8, K('timber_agd_gt'))             # 椅子
    box(v, 26, 27, 12, 20, 8, 14, K('timber_agd_gt_d'))
    box(v, 26, 31, 12, 13, 0, 6, K('timber_agd_gt_d'))
    box(v, 26, 31, 19, 20, 0, 6, K('timber_agd_gt_d'))
    return one(v)


# ================================================================== hard centre（hc）
def crystal_black_agd_hc(name):
    """① 黒い結晶の塊 — 角ばった黒曜石の結晶が斜めに突き出す。面の 1 つは明るい。"""
    rng = rng_for(name)
    v = vol(h=22)
    heap(v, rng, 4, [K('obs_agd_hc'), K('obs_agd_hc_d')], [0.6, 0.4], 5, span=(4, 8))
    for _ in range(4):
        x, y = int(rng.integers(9, 23)), int(rng.integers(9, 23))
        h = int(rng.integers(10, 20))
        tilt = (int(rng.integers(-4, 5)), int(rng.integers(-4, 5)))
        rod(v, (x, y, 1), (x + tilt[0], y + tilt[1], h), 3.0, 1.2, K('obs_agd_hc'))
        rod(v, (x + 1, y, 1), (x + tilt[0] + 1, y + tilt[1], h - 1), 1.6, 0.6, K('obs_agd_hc_h'))
    return one(v)


def crack_glow_agd_hc(name):
    """② 赤く光るひび — 床の黒い板に走る亀裂。底は溶岩の色（自発光は無いので明るい色で）。**低い**。"""
    rng = rng_for(name)
    v = vol(h=3)
    box(v, 3, 29, 3, 29, 0, 3, K('obs_agd_hc'))
    box(v, 4, 28, 4, 28, 2, 3, K('obs_agd_hc_l'))
    for _ in range(3):
        x, y = int(rng.integers(5, 27)), int(rng.integers(5, 27))
        dx, dy = (1, 1) if rng.random() < 0.5 else (1, -1)
        for _ in range(int(rng.integers(8, 16))):
            if 4 <= x < 28 and 4 <= y < 28:
                v[x, y, 2] = 0
                v[x, y, 1] = K('lava_agd_hc') if rng.random() < 0.6 else K('lava_agd_hc_l')
            x += dx if rng.random() < 0.75 else 0
            y += dy if rng.random() < 0.6 else 0
    return one(v)


def spike_rock_agd_hc(name):
    """② 黒い棘の生えた岩 — 岩の塊から黒い棘が四方へ伸びる。"""
    rng = rng_for(name)
    v = vol(h=24)
    ball(v, 16, 16, 4, 6.5, K('obs_agd_hc'), squash=0.7)
    for _ in range(7):
        a = float(rng.uniform(0, 6.28))
        up = float(rng.uniform(0.3, 1.0))
        ln = float(rng.uniform(8, 13))
        x1, y1, z1 = 16 + ln * np.cos(a) * (1 - up * 0.6), 16 + ln * np.sin(a) * (1 - up * 0.6), 4 + ln * up
        rod(v, (16, 16, 4), (x1, y1, min(22, z1)), 2.2, 0.5, K('obs_agd_hc_d') if rng.random() < 0.5 else K('obs_agd_hc_l'))
    return one(v)


def metal_drip_agd_hc(name):
    """② 溶けた金属の垂れ — 岩から垂れて固まった金属（鈍く光る）と、床に広がった溜まり。"""
    v = vol(h=22)
    box(v, 10, 22, 12, 20, 0, 20, K('obs_agd_hc'))
    box(v, 10, 22, 19, 20, 0, 20, K('obs_agd_hc_l'))             # +y ＝ カメラ側の面
    for x, z0 in ((12, 6), (15, 2), (18, 9), (20, 4)):          # 垂れ（カメラ側へ）
        box(v, x, x + 2, 20, 21, z0, 19, K('iron_agd_hc'))
        box(v, x, x + 2, 20, 21, z0, z0 + 2, K('iron_agd_hc_l'))
    box(v, 9, 23, 20, 22, 18, 20, K('iron_agd_hc_l'))
    disc(v, 16, 24, 6.0, 0, 2, K('iron_agd_hc'))                 # 床の溜まり
    disc(v, 16, 24, 3.0, 1, 2, K('iron_agd_hc_l'))
    box(v, 13, 19, 22, 24, 0, 2, K('lava_agd_hc_d'))             # まだ赤い芯
    return one(v)


def pillar_agd_hc(name):
    """③ 黒曜石の柱 — 黒い角柱。稜だけが明るく光る。丈 54。"""
    v = vol(h=54)
    box(v, 9, 23, 9, 23, 0, 3, K('obs_agd_hc_d'))
    box(v, 11, 21, 11, 21, 3, 50, K('obs_agd_hc'))
    for x, y in ((11, 11), (20, 11), (11, 20), (20, 20)):       # 稜
        box(v, x, x + 1, y, y + 1, 3, 50, K('obs_agd_hc_h'))
    box(v, 11, 21, 20, 21, 20, 32, K('obs_agd_hc_l'))            # 割れ面（+y ＝ カメラ側）
    box(v, 9, 23, 9, 23, 50, 54, K('obs_agd_hc_d'))
    box(v, 12, 20, 12, 20, 53, 54, K('lava_agd_hc_d'))           # 天面の赤い目地
    return one(v)


def obsidian_brazier_agd_hc(name):
    """③ 赤い炎の火皿（本体）— 黒曜石の皿。炎は `red_flame_agd_hc`。"""
    v = vol(h=30)
    box(v, 10, 22, 10, 22, 0, 3, K('obs_agd_hc_d'))
    box(v, 13, 19, 13, 19, 3, 22, K('obs_agd_hc'))
    for x, y in ((13, 13), (18, 13), (13, 18), (18, 18)):
        box(v, x, x + 1, y, y + 1, 3, 22, K('obs_agd_hc_h'))
    disc(v, 16, 16, 7.5, 22, 26, K('obs_agd_hc'), hollow=5.5)
    disc(v, 16, 16, 5.5, 22, 24, K('obs_agd_hc_d'))
    disc(v, 16, 16, 8.0, 25, 27, K('obs_agd_hc_l'), hollow=6.0)
    disc(v, 16, 16, 5.5, 24, 26, K('lava_agd_hc_d'))             # 皿の底の溶岩
    return one(v)


def red_flame_agd_hc(name):
    v = vol(h=42)
    disc(v, 16, 16, 4.5, 26, 32, K('fl_agd_hc'))
    disc(v, 16, 16, 3.0, 28, 37, K('fl_agd_hc'))
    disc(v, 16, 16, 1.6, 30, 40, K('fl_agd_hc_l'))
    return one(v)


def glyph_slab_agd_hc(name):
    """④ 魔法陣の刻まれた床板 — 黒い石板に赤黒い刻み（円と 5 本の線）。"""
    v = vol(h=4)
    box(v, 3, 29, 3, 29, 0, 3, K('obs_agd_hc'))
    box(v, 4, 28, 4, 28, 2, 3, K('obs_agd_hc_d'))
    disc(v, 16, 16, 11.0, 2, 3, K('glass_agd_hc'), hollow=9.5)
    disc(v, 16, 16, 6.0, 2, 3, K('lava_agd_hc_d'), hollow=5.0)
    for k in range(5):
        a0 = k * 1.2566 - 1.5708
        a1 = ((k + 2) % 5) * 1.2566 - 1.5708
        p0 = (16 + 9.5 * np.cos(a0), 16 + 9.5 * np.sin(a0), 2)
        p1 = (16 + 9.5 * np.cos(a1), 16 + 9.5 * np.sin(a1), 2)
        rod(v, p0, p1, 0.8, 0.8, K('glass_agd_hc'))
    v[:, :, 3] = 0
    return one(v)


def altar_agd_hc(name):
    """④ 黒い祭壇 — 黒曜石の台。天面に溝と、溝に溜まった赤。"""
    v = vol(h=18)
    box(v, 6, 26, 9, 23, 0, 3, K('obs_agd_hc_d'))
    box(v, 8, 24, 11, 21, 3, 14, K('obs_agd_hc'))
    box(v, 8, 24, 20, 21, 3, 14, K('obs_agd_hc_l'))             # +y ＝ カメラ側の面
    box(v, 6, 26, 9, 23, 14, 17, K('obs_agd_hc'))
    box(v, 6, 26, 9, 23, 16, 17, K('obs_agd_hc_l'))
    box(v, 8, 24, 15, 17, 16, 17, K('lava_agd_hc_d'))            # 溝
    box(v, 15, 17, 10, 22, 16, 17, K('lava_agd_hc_d'))
    box(v, 14, 18, 14, 18, 16, 17, K('lava_agd_hc'))
    return one(v)


def lava_pool_agd_hc(name):
    """④ 溶岩の小さな溜まり — 黒い岩の縁に囲まれた溶岩（自発光は無いので明るい色で言う）。低い。"""
    rng = rng_for(name)
    v = vol(h=5)
    disc(v, 16, 16, 11.0, 0, 3, K('obs_agd_hc'), hollow=8.0)
    for _ in range(8):
        a = float(rng.uniform(0, 6.28))
        x, y = int(16 + 10 * np.cos(a)), int(16 + 10 * np.sin(a))
        box(v, x - 1, x + 2, y - 1, y + 2, 0, int(rng.integers(2, 5)), K('obs_agd_hc_d'))
    disc(v, 16, 16, 8.0, 0, 2, K('lava_agd_hc'))
    disc(v, 16, 16, 8.0, 1, 2, K('lava_agd_hc_d'))
    for _ in range(5):
        x, y = int(rng.integers(10, 22)), int(rng.integers(10, 22))
        box(v, x, x + 3, y, y + 3, 1, 2, K('lava_agd_hc_l'))
    return one(v)


# ================================================================== 登録
#: (名前, 組み立て, 註記)。**火や光るものは本体と光を分ける**（名前の対はここで並べておく）。
PROPS = [
    # classic
    ('rockfall_agd_cl', rockfall_agd_cl, '① 崩れた石の山（classic）'),
    ('sconce_dead_agd_cl', sconce_dead_agd_cl, '② 壁の燭台（消えている。classic）'),
    ('stone_fallen_agd_cl', stone_fallen_agd_cl, '② 落ちた石（classic）'),
    ('chain_ring_agd_cl', chain_ring_agd_cl, '② 鎖の輪（classic）'),
    ('pillar_agd_cl', pillar_agd_cl, '③ 煤けた石の柱（classic）'),
    ('torch_agd_cl', torch_agd_cl, '③ 松明の本体（classic）。炎は torch_flame_agd_cl'),
    ('torch_flame_agd_cl', torch_flame_agd_cl, '③ 松明の炎（classic）'),
    ('barrel_fallen_agd_cl', barrel_fallen_agd_cl, '④ 倒れた樽（classic）'),
    ('crate_broken_agd_cl', crate_broken_agd_cl, '④ 割れた木箱（classic）'),
    ('bones_old_agd_cl', bones_old_agd_cl, '④ 古い骨（classic）'),
    # modified
    ('stele_shard_agd_md', stele_shard_agd_md, '① ひびの入った石碑の欠片（modified）'),
    ('wall_hole_agd_md', wall_hole_agd_md, '② 崩れた壁の穴（modified）'),
    ('root_agd_md', root_agd_md, '② 木の根（modified）'),
    ('damp_stain_agd_md', damp_stain_agd_md, '② 水の染み（modified）'),
    ('pillar_broken_agd_md', pillar_broken_agd_md, '③ 折れた柱（modified。火は無し）'),
    ('firepit_agd_md', firepit_agd_md, '④ 焚き火の跡（modified）'),
    ('bedroll_agd_md', bedroll_agd_md, '④ 放置された寝床（modified）'),
    ('weapon_rusty_agd_md', weapon_rusty_agd_md, '④ 錆びた武器（modified）'),
    # moria
    ('cavein_agd_mo', cavein_agd_mo, '① 落盤の岩と折れた坑木（moria）'),
    ('mine_prop_agd_mo', mine_prop_agd_mo, '② 坑木の支柱（moria）'),
    ('pickaxe_agd_mo', pickaxe_agd_mo, '② つるはしの立て掛け（moria）'),
    ('ore_pile_agd_mo', ore_pile_agd_mo, '② 鉱石の積み（moria）'),
    ('timber_frame_agd_mo', timber_frame_agd_mo, '③ 木組みの支柱（moria）'),
    ('lantern_agd_mo', lantern_agd_mo, '③ 吊りランタンの本体（moria）。灯は lantern_glow_agd_mo'),
    ('lantern_glow_agd_mo', lantern_glow_agd_mo, '③ 吊りランタンの灯（moria）'),
    ('minecart_agd_mo', minecart_agd_mo, '④ 鉱石を積んだトロッコと線路の切れ端（moria）'),
    ('ore_heap_agd_mo', ore_heap_agd_mo, '④ 鉱石の山（moria）'),
    ('toolbox_agd_mo', toolbox_agd_mo, '④ 道具箱（moria）'),
    # labyrinth
    ('rune_stone_agd_lb', rune_stone_agd_lb, '① 石の袋小路に刻まれた記号 1 つ（labyrinth）'),
    ('relief_agd_lb', relief_agd_lb, '② 壁の浮き彫り（labyrinth）'),
    ('grate_window_agd_lb', grate_window_agd_lb, '② 鉄格子の覗き窓（labyrinth）'),
    ('floor_groove_agd_lb', floor_groove_agd_lb, '② 床の溝（labyrinth）'),
    ('pillar_agd_lb', pillar_agd_lb, '③ 細い黒石の角柱（labyrinth）'),
    ('magelight_agd_lb', magelight_agd_lb, '③ 青白い魔法の灯の台（labyrinth）。光は magelight_glow_agd_lb'),
    ('magelight_glow_agd_lb', magelight_glow_agd_lb, '③ 青白い魔法の灯の光（labyrinth）'),
    ('pedestal_agd_lb', pedestal_agd_lb, '④ 台座だけ残った像（labyrinth）'),
    ('basin_agd_lb', basin_agd_lb, '④ 石の水盤（labyrinth）'),
    ('circle_agd_lb', circle_agd_lb, '④ 床の模様の円（labyrinth）'),
    # cavern
    ('stalactite_pile_agd_cv', stalactite_pile_agd_cv, '① 鍾乳石が折れて積もった山（cavern）'),
    ('stalagmite_agd_cv', stalagmite_agd_cv, '② 石筍（cavern）'),
    ('glowshroom_agd_cv', glowshroom_agd_cv, '② 光るキノコ（cavern）'),
    ('ledge_drip_agd_cv', ledge_drip_agd_cv, '② 水の滴る岩棚（cavern）'),
    ('column_agd_cv', column_agd_cv, '③ 石柱＝鍾乳石と石筍がつながったもの（cavern）'),
    ('shroom_cluster_agd_cv', shroom_cluster_agd_cv, '③ 火の枠の本体＝光るキノコの群生の柄と岩（cavern）。傘は shroom_glow_agd_cv'),
    ('shroom_glow_agd_cv', shroom_glow_agd_cv, '③ 光るキノコの傘（cavern。自発光を受ける側）'),
    ('pool_agd_cv', pool_agd_cv, '④ 地底の水たまり（cavern）'),
    ('mossrock_agd_cv', mossrock_agd_cv, '④ 苔むした岩（cavern）'),
    ('bones_beast_agd_cv', bones_beast_agd_cv, '④ 獣の骨（cavern）'),
    # lair
    ('fur_drift_agd_lr', fur_drift_agd_lr, '① 骨と毛の吹き溜まり（lair）'),
    ('clawmarks_agd_lr', clawmarks_agd_lr, '② 爪痕の壁（lair）'),
    ('bones_gnawed_agd_lr', bones_gnawed_agd_lr, '② 食べ残しの骨（lair）'),
    ('twig_bundle_agd_lr', twig_bundle_agd_lr, '② 巣材の枝の束（lair）'),
    ('bone_pillar_agd_lr', bone_pillar_agd_lr, '③ 骨を組んだ柱（lair）'),
    ('tallow_fire_agd_lr', tallow_fire_agd_lr, '③ 獣脂の火の本体（lair）。火は tallow_flame_agd_lr'),
    ('tallow_flame_agd_lr', tallow_flame_agd_lr, '③ 獣脂の火の炎（lair）'),
    ('nest_agd_lr', nest_agd_lr, '④ 巣＝寝藁の窪み（lair）'),
    ('eggshell_agd_lr', eggshell_agd_lr, '④ 卵の殻（lair）'),
    ('carcass_agd_lr', carcass_agd_lr, '④ 獲物の残骸（lair）'),
    # gauntlet
    ('portcullis_agd_gt', portcullis_agd_gt, '① 落とし格子の下りた門（gauntlet）'),
    ('arrowslit_agd_gt', arrowslit_agd_gt, '② 矢狭間（gauntlet）'),
    ('iron_sconce_agd_gt', iron_sconce_agd_gt, '② 鉄の燭台（消えている。gauntlet）'),
    ('spear_rack_agd_gt', spear_rack_agd_gt, '② 槍立て（gauntlet）'),
    ('pillar_agd_gt', pillar_agd_gt, '③ 鉄帯の石柱（gauntlet）'),
    ('brazier_agd_gt', brazier_agd_gt, '③ かがり火の本体（gauntlet）。火は brazier_flame_agd_gt'),
    ('brazier_flame_agd_gt', brazier_flame_agd_gt, '③ かがり火の炎（gauntlet）'),
    ('barricade_agd_gt', barricade_agd_gt, '④ バリケードの残骸（gauntlet）'),
    ('armour_heap_agd_gt', armour_heap_agd_gt, '④ 武具の山（gauntlet）'),
    ('guard_desk_agd_gt', guard_desk_agd_gt, '④ 詰め所の机（gauntlet）'),
    # hard centre
    ('crystal_black_agd_hc', crystal_black_agd_hc, '① 黒い結晶の塊（hard centre）'),
    ('crack_glow_agd_hc', crack_glow_agd_hc, '② 赤く光るひび（hard centre）'),
    ('spike_rock_agd_hc', spike_rock_agd_hc, '② 黒い棘の生えた岩（hard centre）'),
    ('metal_drip_agd_hc', metal_drip_agd_hc, '② 溶けた金属の垂れ（hard centre）'),
    ('pillar_agd_hc', pillar_agd_hc, '③ 黒曜石の柱（hard centre）'),
    ('obsidian_brazier_agd_hc', obsidian_brazier_agd_hc, '③ 赤い炎の火皿の本体（hard centre）。炎は red_flame_agd_hc'),
    ('red_flame_agd_hc', red_flame_agd_hc, '③ 赤い炎（hard centre）'),
    ('glyph_slab_agd_hc', glyph_slab_agd_hc, '④ 魔法陣の刻まれた床板（hard centre）'),
    ('altar_agd_hc', altar_agd_hc, '④ 黒い祭壇（hard centre）'),
    ('lava_pool_agd_hc', lava_pool_agd_hc, '④ 溶岩の小さな溜まり（hard centre）'),
]


def prop_names():
    return [n for n, _, _ in PROPS]


def register_props(ns):
    _bind(ns)
    cat = G['CATALOG']
    static = G['static_part']
    for name, builder, note in PROPS:
        cat[name] = (builder, static(), 'Angband のダンジョンの小物: %s' % note)
