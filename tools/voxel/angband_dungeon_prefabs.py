# -*- coding: utf-8 -*-
"""Angband のダンジョンの素材の生成器。

`gen_prefabs.py` から `register(globals())` で呼ばれ、**階の作り方（原作の cave profile）ごと**の
接尾辞 `_agd_<略号>`（§3。cl / md / mo / lb / cv / lr / gt / hc）のプレハブを `CATALOG` へ足す。
**既存の素材には 1 バイトも触らない**（別の名前で足すだけ）。手本は町の `angband_prefabs.py`。

このファイルは**材（床・壁・岩盤）**と登録。小物は `angband_dungeon_props.py`（同じ流儀で
`register_props(namespace)` を持ち、ここから呼ぶ）。

## 名前
- 材は `floor_agd_cl_01` のように **`<種>_agd_<略号>_<番号>`**（床 8・壁 8・岩盤 4 の変種）。
  引く側（`hd2d/world/terrain_view.cpp` の `gather_material`）が `_01`〜`_10` を探すので、
  接尾辞の後ろに番号が来る。部屋と通路の床は**同じ材**を敷く（表 §2 が「床」を 1 つしか
  決めていないので、迷宮の `floor_lab` と同じ扱い）
- 小物は `rockfall_agd_cl` のように**末尾**に `_agd_<略号>`。火や光るものは本体と光を分ける
  （`torch_agd_cl` ＋ `torch_flame_agd_cl`。町の `forge_agb` / `forge_fire_agb` と同じ）

## 作りの規律（`gen_prefabs.py` 冒頭と同じ）
- 無作為な表面ノイズは入れない。彫りは 1〜2 ボクセルの深さ。色は塊ごとに 1 色
- 接地層（z=0）の平面方向は縮めない。壁と岩盤の天面は平ら（高さの揃いが地形の判別）
- +y は南＝カメラ側

## 色の名前
`palette_materials.classify` が**頭の語**で材質を当てるので、`rock_agd_cl` `timber_agd_mo` の
形（`<材質の語>_agd_<略号>[_l|_d]`）にする（町の註記と同じ理由）。
"""
from __future__ import annotations

import importlib.util
import os

import numpy as np

G = None   #: gen_prefabs の名前空間（register で入る）
C = None   #: 色の名前 → 索引
V = 32
FLOOR_H = 4
WALL_H = 32

HERE = os.path.dirname(os.path.abspath(__file__))

#: 作り方の略号（§3）。表の並び順のまま。
PROFILES = ('cl', 'md', 'mo', 'lb', 'cv', 'lr', 'gt', 'hc')
FLOOR_VARIANTS = 8
WALL_VARIANTS = 8
BEDROCK_VARIANTS = 4


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


def cone(v, cx, cy, z0, z1, r0, r1, colour, colour_tip=None):
    """z0 で半径 r0、z1 で半径 r1 の円錐台（鍾乳石・石筍・棘）。"""
    z0i, z1i = max(0, z0), min(v.shape[2], z1)
    for z in range(z0i, z1i):
        t = (z - z0) / float(max(1, z1 - z0 - 1))
        r = r0 + (r1 - r0) * t
        c = colour_tip if (colour_tip is not None and t > 0.75) else colour
        disc(v, cx, cy, max(0.6, r), z, z + 1, c)


def ball(v, cx, cy, cz, r, colour, squash=1.0):
    X, Y, Z = v.shape
    xs = np.arange(X)[:, None, None] + 0.5 - cx
    ys = np.arange(Y)[None, :, None] + 0.5 - cy
    zs = (np.arange(Z)[None, None, :] + 0.5 - cz) / squash
    v[(xs * xs) + (ys * ys) + (zs * zs) <= (r * r)] = colour


def rod(v, p0, p1, r0, r1, colour):
    """2 点を結ぶ棒。半径は p0 で r0、p1 で r1。"""
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


def profile_of(name):
    """`floor_agd_cl_03` / `rockfall_agd_cl` → 'cl'。"""
    parts = name.split('_')
    at = parts.index('agd')
    return parts[at + 1]


# ------------------------------------------------------------------ 色
def _palette():
    rl = G['reg_local']
    # ---- classic（cl）: 切り出した石。煤で黒ずむ ----
    #! 壁は床（pav_agd_cl）より**明度で 2 割以上暗く**する（気づいたこと 2026-09-24「床と壁の明度が近い」。§4.7）。
    rl('rock_agd_cl', (88, 82, 76)); rl('rock_agd_cl_l', (110, 104, 96)); rl('rock_agd_cl_d', (66, 62, 58))
    rl('rock_agd_cl_j', (42, 40, 38))                                       # 目地の底
    rl('soot_agd_cl', (58, 54, 52)); rl('soot_agd_cl_l', (76, 72, 68))       # 煤
    rl('pav_agd_cl', (128, 114, 96)); rl('pav_agd_cl_l', (148, 134, 114)); rl('pav_agd_cl_d', (104, 92, 78))
    rl('pav_agd_cl_j', (72, 62, 52))
    rl('iron_agd_cl', (60, 58, 62)); rl('iron_agd_cl_l', (92, 90, 96))
    rl('timber_agd_cl', (86, 62, 42)); rl('timber_agd_cl_d', (60, 42, 30)); rl('timber_agd_cl_l', (112, 84, 58))
    rl('bone_agd_cl', (190, 178, 152)); rl('bone_agd_cl_d', (150, 138, 116))
    # ---- modified（md）: 崩れかけた古い石積み ----
    rl('rock_agd_md', (90, 84, 76)); rl('rock_agd_md_l', (112, 104, 94)); rl('rock_agd_md_d', (68, 64, 58))
    rl('rock_agd_md_j', (40, 38, 36))
    rl('pav_agd_md', (132, 120, 104)); rl('pav_agd_md_l', (156, 144, 126)); rl('pav_agd_md_d', (106, 96, 82))
    rl('pav_agd_md_j', (60, 54, 48))
    rl('bark_agd_md', (74, 56, 40)); rl('bark_agd_md_l', (98, 76, 54))         # 木の根
    rl('damp_agd_md', (58, 60, 62)); rl('damp_agd_md_l', (78, 82, 86))         # 水の染み
    rl('ash_agd_md', (92, 88, 84)); rl('char_agd_md', (40, 36, 34))
    rl('hay_agd_md', (150, 132, 88)); rl('cloth_agd_md', (120, 104, 84)); rl('cloth_agd_md_d', (90, 78, 64))
    rl('rust_agd_md', (112, 66, 40)); rl('rust_agd_md_l', (146, 92, 58))
    rl('timber_agd_md', (92, 68, 46))
    # ---- moria（mo）: 坑道 ----
    rl('rock_agd_mo', (84, 78, 68)); rl('rock_agd_mo_l', (104, 96, 84)); rl('rock_agd_mo_d', (64, 60, 52))
    rl('rock_agd_mo_dd', (48, 44, 40))
    rl('timber_agd_mo', (84, 60, 38)); rl('timber_agd_mo_l', (104, 78, 52)); rl('timber_agd_mo_d', (58, 42, 28))
    rl('dirt_agd_mo', (134, 112, 82)); rl('dirt_agd_mo_l', (154, 130, 98)); rl('dirt_agd_mo_d', (108, 88, 64))
    rl('grit_agd_mo', (164, 158, 146)); rl('grit_agd_mo_d', (134, 128, 118))   # 砂利
    rl('iron_agd_mo', (70, 66, 66)); rl('iron_agd_mo_l', (104, 100, 100))
    rl('rust_agd_mo', (124, 76, 44))
    rl('gold_agd_mo', (170, 134, 52)); rl('gold_agd_mo_d', (124, 96, 38))        # 鉱脈・鉱石の光る面
    rl('qz_agd_mo', (206, 200, 190))
    rl('fl_agd_mo', (250, 208, 110)); rl('lit_agd_mo', (236, 190, 96))
    # ---- labyrinth（lb）: 黒い切石 ----
    #! 黒い切石。**青みを足して画面の消去色（暗い藍）と同じ比率にしない**——影に入った天面が
    #! 消去色 ±1 に落ちて、--terrain-check の四隅の欠けの検査が「欠け」と数えた（実測 6.3%）。
    rl('obs_agd_lb', (38, 40, 52)); rl('obs_agd_lb_l', (56, 60, 76)); rl('obs_agd_lb_d', (28, 30, 40))
    rl('obs_agd_lb_j', (24, 24, 34))                                       # 目地
    rl('flag_agd_lb', (118, 120, 134)); rl('flag_agd_lb_d', (66, 68, 84))    # 市松の明・暗
    rl('flag_agd_lb_j', (40, 40, 54))
    rl('iron_agd_lb', (58, 58, 64)); rl('iron_agd_lb_l', (96, 96, 106))
    rl('glass_agd_lb', (170, 200, 236)); rl('glass_agd_lb_l', (220, 236, 252))    # 青白い魔法の灯
    rl('wa_agd_lb', (48, 62, 80))
    rl('marb_agd_lb', (156, 152, 148)); rl('marb_agd_lb_d', (120, 116, 112))     # 台座の像の残り
    # ---- cavern（cv）: 湿った自然の岩 ----
    rl('rock_agd_cv', (96, 96, 92)); rl('rock_agd_cv_l', (124, 124, 118)); rl('rock_agd_cv_d', (72, 72, 70))
    rl('rock_agd_cv_dd', (52, 52, 52)); rl('damp_agd_cv', (86, 94, 100))
    #! 壁の岩は床の岩と**別の色**（床と同じ色を使っていて明度が並んだ）。
    rl('rock_agd_cv_w', (68, 68, 66)); rl('rock_agd_cv_wl', (88, 88, 84)); rl('rock_agd_cv_wd', (50, 50, 48))
    rl('rock_agd_cv_wdd', (36, 36, 36)); rl('damp_agd_cv_w', (60, 68, 74))
    rl('moss_agd_cv', (78, 108, 62)); rl('moss_agd_cv_d', (56, 82, 46))
    rl('wa_agd_cv', (40, 62, 78)); rl('wa_agd_cv_l', (66, 96, 112))
    rl('mgl_agd_cv', (132, 216, 184)); rl('mgl_agd_cv_d', (80, 150, 124)); rl('stem_agd_cv', (196, 196, 176))
    rl('bone_agd_cv', (204, 194, 172)); rl('bone_agd_cv_d', (160, 150, 130))
    # ---- lair（lr）: 泥と骨 ----
    #! 泥壁は暗くしすぎない（--terrain-check の (8) 画調の検査が「標準の面が沈みすぎ」で落ちた）。差は床を明るくして付ける。
    rl('clay_agd_lr', (92, 72, 54)); rl('clay_agd_lr_l', (112, 90, 66)); rl('clay_agd_lr_d', (68, 52, 40))
    rl('clay_agd_lr_dd', (50, 38, 30))
    rl('bone_agd_lr', (212, 200, 176)); rl('bone_agd_lr_d', (168, 156, 132))
    rl('hay_agd_lr', (176, 152, 92)); rl('hay_agd_lr_d', (138, 118, 70))
    rl('fur_agd_lr', (92, 72, 54)); rl('fur_agd_lr_l', (124, 100, 76))
    rl('dirt_agd_lr', (134, 110, 82)); rl('dirt_agd_lr_l', (154, 130, 98)); rl('dirt_agd_lr_d', (108, 88, 66))
    rl('bark_agd_lr', (80, 60, 42)); rl('bark_agd_lr_l', (106, 82, 58))
    rl('crimson_agd_lr', (120, 40, 36)); rl('rot_agd_lr', (140, 96, 84))     # 獲物の残骸（肉）
    rl('fl_agd_lr', (246, 186, 96)); rl('emb_agd_lr', (150, 84, 40))          # 獣脂の火（黄みが強く低い）
    rl('cream_agd_lr', (222, 210, 176)); rl('cream_agd_lr_d', (182, 168, 138))  # 卵の殻
    # ---- gauntlet（gt）: 鉄帯で補強した城壁 ----
    rl('rock_agd_gt', (96, 90, 82)); rl('rock_agd_gt_l', (116, 110, 100)); rl('rock_agd_gt_d', (76, 70, 64))
    rl('rock_agd_gt_j', (44, 40, 38))
    rl('pav_agd_gt', (140, 134, 122)); rl('pav_agd_gt_l', (160, 154, 140)); rl('pav_agd_gt_d', (112, 106, 96))
    rl('pav_agd_gt_j', (52, 48, 44)); rl('wa_agd_gt', (44, 50, 56))            # 排水の溝の底の濡れ
    rl('iron_agd_gt', (62, 60, 64)); rl('iron_agd_gt_l', (98, 96, 102)); rl('iron_agd_gt_d', (40, 38, 42))
    rl('rust_agd_gt', (118, 70, 44))
    rl('timber_agd_gt', (96, 70, 46)); rl('timber_agd_gt_l', (122, 94, 62)); rl('timber_agd_gt_d', (66, 48, 32))
    rl('steel_agd_gt', (188, 194, 204)); rl('steel_agd_gt_d', (140, 146, 158))
    rl('cloth_agd_gt', (150, 60, 52)); rl('cloth_agd_gt_d', (108, 42, 38))   # 詰め所の旗・布
    rl('fl_agd_gt', (252, 210, 110))
    # ---- hard centre（hc）: 黒曜石と赤い光 ----
    #! 黒曜石。**真っ黒にしない**（合成フレームで壁の形が消えた。暗い所では明度が形の全て）。
    #! 壁の割れ面・稜（_l / _h）は**地の色に近づける**（気づいたこと 2026-09-24「線が目立ちすぎ」）。
    rl('obs_agd_hc', (36, 32, 42)); rl('obs_agd_hc_l', (46, 42, 54)); rl('obs_agd_hc_h', (56, 50, 66))
    rl('obs_agd_hc_d', (26, 22, 30))
    #! 床は壁より明るい黒灰（同じ黒だと床と壁の明度が並ぶ）。赤い亀裂はそのまま。
    rl('obs_agd_hc_f', (88, 78, 98)); rl('obs_agd_hc_fl', (110, 98, 122)); rl('obs_agd_hc_fd', (66, 58, 74))
    rl('lava_agd_hc', (232, 96, 34)); rl('lava_agd_hc_l', (250, 170, 60)); rl('lava_agd_hc_d', (160, 48, 24))
    rl('iron_agd_hc', (96, 80, 76)); rl('iron_agd_hc_l', (140, 118, 108))    # 溶けた金属
    rl('fl_agd_hc', (250, 120, 50)); rl('fl_agd_hc_l', (252, 196, 90))
    rl('glass_agd_hc', (120, 30, 40))                                        # 魔法陣の刻み（赤黒）


# ------------------------------------------------------------------ 共通の彫り道具
def _face_setter(face):
    if face == 0:
        return lambda u, w, d: (u, d, w)
    if face == 1:
        return lambda u, w, d: (u, V - 1 - d, w)
    if face == 2:
        return lambda u, w, d: (d, u, w)
    return lambda u, w, d: (V - 1 - d, u, w)


def carve_relief(v, face, u0, u1, w0, w1, depth):
    """側面の矩形を `depth` ボクセルえぐる。接地層と天面はえぐらない（罠 5・高さの揃い）。"""
    at = _face_setter(face)
    top = v.shape[2] - 1
    for w in range(max(w0, 1), min(w1, top)):
        for u in range(max(u0, 0), min(u1, V)):
            for d in range(depth):
                v[at(u, w, d)] = 0


def paint_face(v, face, u0, u1, w0, w1, depth, colour):
    at = _face_setter(face)
    for w in range(max(w0, 0), min(w1, v.shape[2])):
        for u in range(max(u0, 0), min(u1, V)):
            v[at(u, w, depth)] = colour


def block_wall(rng, h, joint, courses, shade_fn, recess_choice, recess_p, cap_shades, cap_step=16):
    """石ブロックの壁。`courses` は `(z0, z1, 横ずらし, 石の幅の範囲)` の並び（段ごとに違えられる）。
    `shade_fn(face, course_i, u)` が石の色を返す。目地は貼り残して彫る（`_block_wall` と同じ流儀）。"""
    v = np.full((V, V, h), joint, np.int16)
    for face in range(4):
        for ci, (z0, z1, off, wr) in enumerate(courses):
            w0, w1 = z0, min(z1 - 1, h)   # 上端 1 列は目地
            u = -off
            while u < V:
                bw = int(rng.integers(*wr))
                shade = shade_fn(face, ci, u)
                recess = int(rng.choice(recess_choice, p=recess_p))
                ua, ub = max(u, 0), min(u + bw - 1, V)   # 右 1 列は目地
                if ua < ub:
                    if recess > 0:
                        carve_relief(v, face, ua, ub, w0, w1, recess)
                    paint_face(v, face, ua, ub, max(w0, 1), min(w1, h - 1), recess, shade)
                u += bw
    shell = np.zeros((V, V, h), bool)
    shell[:, 0, :] = shell[:, V - 1, :] = shell[0, :, :] = shell[V - 1, :, :] = True
    carve = shell & (v == joint)
    carve[:, :, 0] = False
    carve[:, :, h - 1] = False
    v[carve] = 0
    top = np.zeros((V, V), np.int16)
    for bx in range(0, V, cap_step):
        for by in range(0, V, cap_step):
            top[bx:bx + cap_step, by:by + cap_step] = int(rng.choice(cap_shades))
    v[:, :, h - 1] = top
    return v


def rock_wall(rng, h, bands, shades, weights, pits=10, stains=14, top_shades=None):
    """岩肌の壁（`bedrock` と同じ考え）。塊のえぐれと色の染み。天面は平ら。"""
    v = np.zeros((V, V, h), np.int16)
    for z in range(h):
        v[:, :, z] = bands[min(len(bands) - 1, z * len(bands) // h)]
    for face in range(4):
        for _ in range(pits):
            u0 = int(rng.integers(0, V - 5))
            w0 = int(rng.integers(1, h - 5))
            du = int(rng.integers(4, 10))
            dw = int(rng.integers(3, 8))
            depth = 1 if rng.random() < 0.62 else 2
            carve_relief(v, face, u0, u0 + du, w0, w0 + dw, depth)
            paint_face(v, face, u0, u0 + du, max(w0, 1), min(w0 + dw, h - 1), depth,
                       shades[-1] if depth == 2 else shades[-2])
        at = _face_setter(face)
        for _ in range(stains):
            u0 = int(rng.integers(0, V - 4))
            w0 = int(rng.integers(0, h - 4))
            shade = int(rng.choice(shades, p=weights))
            for u in range(u0, min(u0 + int(rng.integers(3, 8)), V)):
                for w in range(w0, min(w0 + int(rng.integers(3, 8)), h)):
                    for d in range(3):
                        pos = at(u, w, d)
                        if v[pos] != 0:
                            v[pos] = shade
                            break
    ts = top_shades or [shades[-2], shades[-1]]
    v[:, :, h - 1] = ts[0]
    for _ in range(7):
        x0, y0 = int(rng.integers(0, V - 5)), int(rng.integers(0, V - 5))
        v[x0:x0 + int(rng.integers(3, 6)), y0:y0 + int(rng.integers(3, 6)), h - 1] = ts[1]
    return v


def tile():
    return np.zeros((V, V, FLOOR_H), np.int16)


def paved(rng, face, step, stagger, shades, weights):
    for by in range(0, V, step):
        off = ((by // step) % 2) * stagger
        for bx in range(-step, V + step, step):
            x0 = bx + off
            shade = int(rng.choice(shades, p=weights))
            xa, xb = max(x0, 0), min(x0 + step - 1, V)
            ya, yb = max(by, 0), min(by + step - 1, V)
            if (xa < xb) and (ya < yb):
                face[xa:xb, ya:yb] = shade


def mottle(rng, layer, shades, weights, block=2):
    n = (V + block - 1) // block
    p = rng.choice(shades, size=(n, n), p=weights)
    big = np.repeat(np.repeat(p, block, axis=0), block, axis=1)
    layer[:, :] = big[:V, :V]


def crack(rng, v, top, length=(7, 14), deep_p=0.4):
    x, y = int(rng.integers(3, V - 3)), int(rng.integers(3, V - 3))
    dx, dy = (1, 1) if rng.random() < 0.5 else (1, -1)
    deep = rng.random() < deep_p
    for _ in range(int(rng.integers(*length))):
        if 0 <= x < V and 0 <= y < V:
            v[x, y, top] = 0
            if deep:
                v[x, y, top - 1] = 0
        x += dx if rng.random() < 0.8 else 0
        y += dy if rng.random() < 0.6 else 0


# ------------------------------------------------------------------ 床（8 種）
def floor_agd(name):
    p = profile_of(name)
    return {'cl': _floor_cl, 'md': _floor_md, 'mo': _floor_mo, 'lb': _floor_lb,
            'cv': _floor_cv, 'lr': _floor_lr, 'gt': _floor_gt, 'hc': _floor_hc}[p](name)


def _floor_cl(name):
    """classic — すり減った敷石。大判の敷石の**縁が丸く落ちて**目地が広く見える。亀裂は少ない。"""
    rng = rng_for(name)
    v = tile()
    top = FLOOR_H - 1
    for z in range(FLOOR_H):
        v[:, :, z] = K('pav_agd_cl_j') if z >= top - 1 else K('pav_agd_cl_d')
    face = np.zeros((V, V), np.int16)
    paved(rng, face, 16, 8, [K('pav_agd_cl'), K('pav_agd_cl_l'), K('pav_agd_cl_d')], [0.5, 0.2, 0.3])
    v[:, :, top] = face
    #! すり減り: 敷石の縁 1 列を 1 段落とす（目地が広がって見える＝踏まれて丸くなった縁）。
    for by in range(0, V, 16):
        off = ((by // 16) % 2) * 8
        for bx in range(-16, V + 16, 16):
            x0 = bx + off
            xa, xb = max(x0, 0), min(x0 + 15, V)
            ya, yb = max(by, 0), min(by + 15, V)
            if xa >= xb or ya >= yb:
                continue
            side = int(rng.integers(0, 4))
            if side == 0:
                v[xa:xb, ya, top] = 0
            elif side == 1:
                v[xa, ya:yb, top] = 0
            elif side == 2 and (xb - 1) < V:
                v[xb - 1, ya:yb, top] = 0
            #! 敷石の中央がわずかに窪む（人の通り道）。
            if rng.random() < 0.5:
                v[xa + 4:xb - 4, ya + 4:yb - 4, top] = K('pav_agd_cl_d')
    for _ in range(int(rng.integers(0, 2))):
        crack(rng, v, top, (5, 9), 0.2)
    return [('main', v, (0, 0, -FLOOR_H))]


def _floor_md(name):
    """modified — ひびの多い古い敷石。敷石はやや小さく、**亀裂が多く**、欠けと沈みで面が揃わない。"""
    rng = rng_for(name)
    v = tile()
    top = FLOOR_H - 1
    for z in range(FLOOR_H):
        v[:, :, z] = K('pav_agd_md_j') if z >= top - 1 else K('pav_agd_md_d')
    face = np.zeros((V, V), np.int16)
    paved(rng, face, 11, 5, [K('pav_agd_md'), K('pav_agd_md_l'), K('pav_agd_md_d')], [0.5, 0.2, 0.3])
    v[:, :, top] = face
    for _ in range(int(rng.integers(3, 6))):
        crack(rng, v, top, (6, 14), 0.5)
    #! 沈んだ敷石（1 段低い）と欠け。
    for _ in range(int(rng.integers(1, 3))):
        x, y = int(rng.integers(0, V - 8)), int(rng.integers(0, V - 8))
        v[x:x + 8, y:y + 8, top] = 0
        v[x + 1:x + 7, y + 1:y + 7, top - 1] = K('pav_agd_md_d')
    for _ in range(int(rng.integers(2, 5))):
        x, y = int(rng.integers(0, V - 3)), int(rng.integers(0, V - 3))
        v[x:x + int(rng.integers(2, 5)), y:y + int(rng.integers(2, 5)), top] = 0
    return [('main', v, (0, 0, -FLOOR_H))]


def _floor_mo(name):
    """moria — 踏み固めた土と砂利。土の斑の上に**砂利の粒**（2×2）を多く散らし、轍を浅く彫る。"""
    rng = rng_for(name)
    v = tile()
    top = FLOOR_H - 1
    for z in range(FLOOR_H):
        v[:, :, z] = K('dirt_agd_mo_d')
    layer = np.zeros((V, V), np.int16)
    mottle(rng, layer, [K('dirt_agd_mo'), K('dirt_agd_mo_l'), K('dirt_agd_mo_d')], [0.5, 0.25, 0.25])
    for _ in range(int(rng.integers(18, 28))):
        x, y = int(rng.integers(0, V - 1)), int(rng.integers(0, V - 1))
        layer[x:x + 2, y:y + 2] = K('grit_agd_mo') if rng.random() < 0.6 else K('grit_agd_mo_d')
    v[:, :, top] = layer
    #! 轍（トロッコの通った跡）: 変種の半分に、平行な 2 本の浅い溝。
    if int(name[-2:]) % 2 == 0:
        along_x = rng.random() < 0.5
        for c in (11, 20):
            if along_x:
                v[:, c:c + 2, top] = 0
            else:
                v[c:c + 2, :, top] = 0
    for _ in range(int(rng.integers(1, 3))):
        x, y = int(rng.integers(1, V - 6)), int(rng.integers(1, V - 6))
        v[x:x + int(rng.integers(3, 6)), y:y + int(rng.integers(3, 6)), top] = 0
    return [('main', v, (0, 0, -FLOOR_H))]


def _floor_lb(name):
    """labyrinth — 市松に並べた石板。16 マスの正方形を**明・暗で交互に**。目地は細く、ずらさない。
    亀裂も欠けも入れない（迷宮は乱れの無さで四角さを言う。`floor_lab` と同じ考え）。"""
    rng = rng_for(name)
    v = tile()
    top = FLOOR_H - 1
    for z in range(FLOOR_H):
        v[:, :, z] = K('flag_agd_lb_j')
    idx = int(name[-2:])
    for bx in range(0, V, 16):
        for by in range(0, V, 16):
            light = ((bx // 16 + by // 16 + idx) % 2) == 0
            v[bx:bx + 15, by:by + 15, top] = K('flag_agd_lb') if light else K('flag_agd_lb_d')
    #! 石板の 1 枚に**磨き跡**（同色系の細い帯）。無地だと板が塗りに見える。
    if rng.random() < 0.6:
        bx, by = int(rng.integers(0, 2)) * 16, int(rng.integers(0, 2)) * 16
        light = ((bx // 16 + by // 16 + idx) % 2) == 0
        v[bx + 3:bx + 12, by + 6:by + 8, top] = K('obs_agd_lb_l') if light else K('obs_agd_lb')
    return [('main', v, (0, 0, -FLOOR_H))]


def _floor_cv(name):
    """cavern — でこぼこの岩床。重ねた矩形の岩肌に、**水たまり**（1 段低く水の色）と苔。"""
    rng = rng_for(name)
    v = tile()
    top = FLOOR_H - 1
    for z in range(FLOOR_H):
        v[:, :, z] = K('rock_agd_cv_dd')
    layer = np.full((V, V), K('rock_agd_cv_d'), np.int16)
    for _ in range(70):
        w, d = int(rng.integers(3, 10)), int(rng.integers(3, 10))
        x, y = int(rng.integers(-3, V - 1)), int(rng.integers(-3, V - 1))
        layer[max(0, x):x + w, max(0, y):y + d] = int(rng.choice(
            [K('rock_agd_cv'), K('rock_agd_cv_d'), K('rock_agd_cv_l'), K('damp_agd_cv')], p=[0.4, 0.3, 0.15, 0.15]))
    v[:, :, top] = layer
    #! 岩の凸: 1 段高い塊は作らない（床の天面は z=0 に揃える）。代わりに窪みで凹凸を言う。
    for _ in range(int(rng.integers(2, 5))):
        x, y = int(rng.integers(0, V - 5)), int(rng.integers(0, V - 5))
        v[x:x + int(rng.integers(3, 6)), y:y + int(rng.integers(3, 6)), top] = 0
    if rng.random() < 0.55:
        cx, cy, r = int(rng.integers(8, 24)), int(rng.integers(8, 24)), float(rng.uniform(4, 7))
        disc(v, cx, cy, r, top, top + 1, 0)
        disc(v, cx, cy, r, top - 1, top, K('wa_agd_cv'))
        disc(v, cx, cy, r * 0.5, top - 1, top, K('wa_agd_cv_l'))
    for _ in range(int(rng.integers(2, 5))):
        x, y = int(rng.integers(0, V - 4)), int(rng.integers(0, V - 4))
        m = v[x:x + 4, y:y + 4, top] > 0
        v[x:x + 4, y:y + 4, top][m] = K('moss_agd_cv') if rng.random() < 0.6 else K('moss_agd_cv_d')
    return [('main', v, (0, 0, -FLOOR_H))]


def _floor_lr(name):
    """lair — 骨片と寝藁の散った土。土の斑に**藁の筋**（細い線）と**骨片**（明るい短い棒）。"""
    rng = rng_for(name)
    v = tile()
    top = FLOOR_H - 1
    for z in range(FLOOR_H):
        v[:, :, z] = K('dirt_agd_lr_d')
    layer = np.zeros((V, V), np.int16)
    mottle(rng, layer, [K('dirt_agd_lr'), K('dirt_agd_lr_l'), K('dirt_agd_lr_d')], [0.5, 0.2, 0.3])
    for _ in range(int(rng.integers(6, 12))):   # 藁
        x, y = int(rng.integers(0, V - 8)), int(rng.integers(0, V - 1))
        ln = int(rng.integers(4, 9))
        c = K('hay_agd_lr') if rng.random() < 0.6 else K('hay_agd_lr_d')
        if rng.random() < 0.5:
            layer[x:x + ln, y] = c
        else:
            layer[y, x:x + ln] = c
    for _ in range(int(rng.integers(3, 7))):    # 骨片
        x, y = int(rng.integers(0, V - 4)), int(rng.integers(0, V - 2))
        layer[x:x + int(rng.integers(2, 4)), y:y + 1] = K('bone_agd_lr') if rng.random() < 0.7 else K('bone_agd_lr_d')
    v[:, :, top] = layer
    for _ in range(int(rng.integers(1, 4))):    # 踏み荒らした窪み
        x, y = int(rng.integers(1, V - 6)), int(rng.integers(1, V - 6))
        v[x:x + int(rng.integers(3, 6)), y:y + int(rng.integers(3, 6)), top] = 0
    return [('main', v, (0, 0, -FLOOR_H))]


def _floor_gt(name):
    """gauntlet — 排水の溝を切った石畳。小さめの敷石に、変種の半分は**まっすぐな溝**（2 幅・2 深）。"""
    rng = rng_for(name)
    v = tile()
    top = FLOOR_H - 1
    for z in range(FLOOR_H):
        v[:, :, z] = K('pav_agd_gt_j') if z >= top - 1 else K('pav_agd_gt_d')
    face = np.zeros((V, V), np.int16)
    paved(rng, face, 8, 4, [K('pav_agd_gt'), K('pav_agd_gt_l'), K('pav_agd_gt_d')], [0.55, 0.2, 0.25])
    v[:, :, top] = face
    idx = int(name[-2:])
    if idx % 2 == 0:
        #! 溝。底は濡れている（水の色）。向きは変種で替える。
        if idx % 4 == 0:
            v[:, 15:17, top] = 0
            v[:, 15:17, top - 1] = 0
            v[:, 15:17, top - 2] = K('wa_agd_gt')
        else:
            v[15:17, :, top] = 0
            v[15:17, :, top - 1] = 0
            v[15:17, :, top - 2] = K('wa_agd_gt')
    for _ in range(int(rng.integers(0, 2))):
        crack(rng, v, top, (5, 9), 0.2)
    return [('main', v, (0, 0, -FLOOR_H))]


def _floor_hc(name):
    """hard centre — ひび割れから赤く光る黒い石。黒い一枚岩の天面に**亀裂**を彫り、
    亀裂の底を溶岩の色にする（自発光は渡せないので色の明るさで言う）。"""
    rng = rng_for(name)
    v = tile()
    top = FLOOR_H - 1
    for z in range(FLOOR_H):
        v[:, :, z] = K('obs_agd_hc_fd')
    layer = np.full((V, V), K('obs_agd_hc_f'), np.int16)
    for _ in range(30):
        w, d = int(rng.integers(4, 12)), int(rng.integers(4, 12))
        x, y = int(rng.integers(-3, V - 1)), int(rng.integers(-3, V - 1))
        layer[max(0, x):x + w, max(0, y):y + d] = int(rng.choice(
            [K('obs_agd_hc_f'), K('obs_agd_hc_fl'), K('obs_agd_hc_fd')], p=[0.5, 0.25, 0.25]))
    v[:, :, top] = layer
    v[:, :, top - 1] = K('lava_agd_hc_d')   # 亀裂の底
    for _ in range(int(rng.integers(2, 5))):
        x, y = int(rng.integers(2, V - 2)), int(rng.integers(2, V - 2))
        dx, dy = (1, 1) if rng.random() < 0.5 else (1, -1)
        bright = rng.random() < 0.5
        for _ in range(int(rng.integers(8, 16))):
            if 0 <= x < V and 0 <= y < V:
                v[x, y, top] = 0
                v[x, y, top - 1] = K('lava_agd_hc') if bright else K('lava_agd_hc_l')
            x += dx if rng.random() < 0.75 else 0
            y += dy if rng.random() < 0.6 else 0
    return [('main', v, (0, 0, -FLOOR_H))]


# ------------------------------------------------------------------ 壁（8 種）と岩盤
def wall_agd(name):
    p = profile_of(name)
    return {'cl': _wall_cl, 'md': _wall_md, 'mo': _wall_mo, 'lb': _wall_lb,
            'cv': _wall_cv, 'lr': _wall_lr, 'gt': _wall_gt, 'hc': _wall_hc}[p](name, False)


def bedrock_agd(name):
    """岩盤 — 壁と同じ材で**装飾を落とし、暗くする**（厚い壁の中身。通れないことは壁と同じ形で言う）。"""
    p = profile_of(name)
    return {'cl': _wall_cl, 'md': _wall_md, 'mo': _wall_mo, 'lb': _wall_lb,
            'cv': _wall_cv, 'lr': _wall_lr, 'gt': _wall_gt, 'hc': _wall_hc}[p](name, True)


def _courses(h, course_h, stagger, wr, rng=None, jitter=0):
    out = []
    for i, z in enumerate(range(0, h, course_h)):
        off = (i % 2) * stagger
        if rng is not None and jitter:
            off += int(rng.integers(0, jitter))
        out.append((z, min(z + course_h, h), off, wr))
    return out


def _wall_cl(name, bedrock):
    """classic — 切り出した石の積み壁。煤で黒ずむ（**上の段ほど煤**。火と煙は上へ行く）。"""
    rng = rng_for(name)
    h = WALL_H
    stone = [K('rock_agd_cl'), K('rock_agd_cl_l'), K('rock_agd_cl_d')]
    soot = [K('soot_agd_cl'), K('soot_agd_cl_l')]
    n_courses = h // 8

    def shade(face, ci, u):
        if bedrock:
            return int(rng.choice([stone[2], K('soot_agd_cl_l'), stone[0]], p=[0.5, 0.3, 0.2]))
        p_soot = 0.15 + 0.6 * (ci / float(max(1, n_courses - 1)))
        if rng.random() < p_soot:
            return int(rng.choice(soot, p=[0.65, 0.35]))
        return int(rng.choice(stone, p=[0.55, 0.2, 0.25]))

    v = block_wall(rng, h, K('rock_agd_cl_j'), _courses(h, 8, 6, (9, 14)), shade,
                   [0, 1, 2], [0.5, 0.35, 0.15], [stone[2], K('soot_agd_cl')] if not bedrock else [K('soot_agd_cl')])
    if not bedrock:
        #! 煤の垂れ: 天面の縁から下へ、外皮の色だけ黒くする（形は変えない）。
        for face in range(4):
            at = _face_setter(face)
            for _ in range(int(rng.integers(1, 4))):
                u0 = int(rng.integers(0, V - 3))
                for u in range(u0, u0 + int(rng.integers(2, 4))):
                    for w in range(h - int(rng.integers(6, 14)), h - 1):
                        for d in range(3):
                            pos = at(u, w, d)
                            if v[pos] != 0:
                                v[pos] = K('soot_agd_cl')
                                break
    return one(v)


def _wall_md(name, bedrock):
    """modified — 崩れかけて継ぎ目のずれた石積み。段ごとにずらしが違い、石の幅もまちまち、
    引っ込みが深い石がある（乱れ積み）。上の段は**石が抜けた穴**（2 深の暗い矩形）。"""
    rng = rng_for(name)
    h = WALL_H
    stone = [K('rock_agd_md'), K('rock_agd_md_l'), K('rock_agd_md_d')]

    def shade(face, ci, u):
        if bedrock:
            return int(rng.choice([stone[2], K('rock_agd_md_j'), stone[0]], p=[0.55, 0.25, 0.2]))
        return int(rng.choice(stone, p=[0.5, 0.2, 0.3]))

    v = block_wall(rng, h, K('rock_agd_md_j'), _courses(h, 7, 3, (6, 13), rng, 5), shade,
                   [0, 1, 2, 3], [0.35, 0.35, 0.2, 0.1], [stone[2], stone[0]] if not bedrock else [stone[2]])
    if not bedrock:
        for face in range(4):
            for _ in range(int(rng.integers(1, 3))):
                u0 = int(rng.integers(0, V - 8))
                w0 = int(rng.integers(h // 2, h - 8))
                du, dw = int(rng.integers(5, 9)), int(rng.integers(4, 6))
                carve_relief(v, face, u0, u0 + du, w0, w0 + dw, 3)
                paint_face(v, face, u0, u0 + du, w0, w0 + dw, 3, K('rock_agd_md_j'))
    return one(v)


def _wall_mo(name, bedrock):
    """moria — 荒削りの岩肌に**木の支柱と梁**。鉱脈の筋。岩盤は支柱を持たない素の岩肌。"""
    rng = rng_for(name)
    h = WALL_H
    shades = [K('rock_agd_mo'), K('rock_agd_mo_l'), K('rock_agd_mo_d'), K('rock_agd_mo_dd')]
    bands = [K('rock_agd_mo_d'), K('rock_agd_mo'), K('rock_agd_mo_d'), K('rock_agd_mo'), K('rock_agd_mo_l')]
    if bedrock:
        bands = [K('rock_agd_mo_dd'), K('rock_agd_mo_d'), K('rock_agd_mo_dd'), K('rock_agd_mo_d')]
    v = rock_wall(rng, h, bands, shades, [0.35, 0.1, 0.35, 0.2], top_shades=[K('rock_agd_mo_d'), K('rock_agd_mo_dd')])
    #! 鉱脈: 側面を横切る斜めの帯（`_vein` の考え）。金の筋は 1/3、石英は 1/3、無しが 1/3。
    kind = int(rng.integers(0, 3))
    if kind > 0:
        xs, ys, zs = np.meshgrid(np.arange(V), np.arange(V), np.arange(h), indexing='ij')
        shell = np.zeros((V, V, h), bool)
        shell[0, :, :] = shell[V - 1, :, :] = shell[:, 0, :] = shell[:, V - 1, :] = True
        shell[:, :, 0] = False
        shell[:, :, h - 1] = False
        vein_c = [K('gold_agd_mo'), K('gold_agd_mo_d')] if kind == 1 else [K('qz_agd_mo'), K('rock_agd_mo_l')]
        p2 = rng.choice(vein_c, size=((V + 1) // 2, (V + 1) // 2, (h + 1) // 2), p=[0.55, 0.45])
        pv = np.repeat(np.repeat(np.repeat(p2, 2, axis=0), 2, axis=1), 2, axis=2)[:V, :V, :h]
        nx, ny, nz = rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.3, 0.3)
        d = xs * nx + ys * ny + zs * nz
        c = float(rng.uniform(d.min() + 3, d.max() - 3))
        m = (np.abs(d - c) < 2.2) & shell & (v > 0)
        v[m] = pv[m]
    if not bedrock:
        #! 坑木: 4 面それぞれに支柱 1 本（位置は面で違う）と天端の梁。**外皮と面一**（張り出せない）。
        for face in range(4):
            at = _face_setter(face)
            u0 = int(rng.integers(2, V - 7))
            for u in range(u0, u0 + 4):
                for w in range(0, h - 4):
                    for d in range(2):
                        v[at(u, w, d)] = K('timber_agd_mo') if d == 0 else K('timber_agd_mo_d')
            for u in range(0, V):
                for w in range(h - 5, h - 1):
                    for d in range(2):
                        v[at(u, w, d)] = K('timber_agd_mo_l') if d == 0 else K('timber_agd_mo_d')
            for u in range(u0, u0 + 4):        # 支柱の木口の帯（暗い）
                v[at(u, 12, 0)] = K('timber_agd_mo_d')
                v[at(u, 24, 0)] = K('timber_agd_mo_d')
    return one(v)


def _wall_lb(name, bedrock):
    """labyrinth — 継ぎ目の細い、滑らかな黒い切石。**大きなブロック 2 段**・芋目地・引っ込み無し。"""
    rng = rng_for(name)
    h = WALL_H
    stone = [K('obs_agd_lb'), K('obs_agd_lb_l'), K('obs_agd_lb_d')]

    def shade(face, ci, u):
        if bedrock:
            return int(rng.choice([stone[2], K('obs_agd_lb_j')], p=[0.7, 0.3]))
        return int(rng.choice(stone, p=[0.7, 0.12, 0.18]))

    v = block_wall(rng, h, K('obs_agd_lb_j'), _courses(h, 16, 0, (16, 17)), shade,
                   [0, 1], [0.9, 0.1], [stone[2], stone[0]] if not bedrock else [stone[2]])
    if not bedrock:
        #! 磨いた面の反射: 石ごとに 1 本の斜めの明るい筋（塊単位の色だけ。形は変えない）。
        for face in range(4):
            at = _face_setter(face)
            if rng.random() < 0.7:
                u0 = int(rng.integers(2, V - 10))
                for k in range(8):
                    for w in range(2 + k, h - 2, 8):
                        pos = at(min(V - 1, u0 + k), w, 0)
                        if v[pos] == stone[0]:
                            v[pos] = stone[1]
    return one(v)


def _wall_cv(name, bedrock):
    """cavern — 湿った自然の岩。**上半分が張り出し、下がえぐれて、張り出しの裏から鍾乳石が垂れる**。
    面いっぱいの岩肌に、濡れた色の染み。岩盤は張り出しを持たない。"""
    rng = rng_for(name)
    h = WALL_H
    shades = [K('rock_agd_cv_w'), K('rock_agd_cv_wl'), K('rock_agd_cv_wd'), K('rock_agd_cv_wdd')]
    bands = [K('rock_agd_cv_wdd'), K('rock_agd_cv_wd'), K('rock_agd_cv_w'), K('rock_agd_cv_wd')]
    v = rock_wall(rng, h, bands, shades, [0.3, 0.1, 0.35, 0.25], pits=8 if bedrock else 5, stains=16,
                  top_shades=[K('rock_agd_cv_wd'), K('rock_agd_cv_wdd')])
    #! 濡れ: 面の下 1/3 に青灰の染み（水が伝う所）。
    for face in range(4):
        at = _face_setter(face)
        for _ in range(int(rng.integers(2, 5))):
            u0 = int(rng.integers(0, V - 3))
            for u in range(u0, u0 + int(rng.integers(2, 4))):
                for w in range(1, int(rng.integers(6, 14))):
                    for d in range(3):
                        pos = at(u, w, d)
                        if v[pos] != 0:
                            v[pos] = K('damp_agd_cv_w')
                            break
    if not bedrock:
        for face in range(4):
            #! えぐれ（下半分を 3 ボクセル引っ込める。接地層は残す）と鍾乳石。
            carve_relief(v, face, 0, V, 1, 17, 3)
            paint_face(v, face, 0, V, 1, 17, 3, K('rock_agd_cv_wdd'))
            at = _face_setter(face)
            for _ in range(int(rng.integers(2, 4))):
                u0 = int(rng.integers(2, V - 3))
                ln = int(rng.integers(4, 9))
                for k in range(ln):
                    w = 16 - k
                    half = max(0, 1 - (k * 2) // ln)
                    for uu in range(u0 - half, u0 + half + 1):
                        for d in range(0, 3):
                            if 0 <= uu < V and w >= 1:
                                v[at(uu, w, d)] = K('rock_agd_cv_wl') if k >= ln - 2 else K('rock_agd_cv_w')
    return one(v)


def _wall_lr(name, bedrock):
    """lair — 泥と骨をこね固めた壁。塊のえぐれの土壁に、**骨が埋まって顔を出す**（短い棒と頭蓋の丸）。"""
    rng = rng_for(name)
    h = WALL_H
    shades = [K('clay_agd_lr'), K('clay_agd_lr_l'), K('clay_agd_lr_d'), K('clay_agd_lr_dd')]
    bands = [K('clay_agd_lr_d'), K('clay_agd_lr'), K('clay_agd_lr_d'), K('clay_agd_lr')]
    if bedrock:
        bands = [K('clay_agd_lr_dd'), K('clay_agd_lr_d'), K('clay_agd_lr_dd')]
    v = rock_wall(rng, h, bands, shades, [0.4, 0.15, 0.3, 0.15], pits=12, stains=10,
                  top_shades=[K('clay_agd_lr_d'), K('clay_agd_lr_dd')])
    n_bones = int(rng.integers(1, 3)) if bedrock else int(rng.integers(3, 6))
    for face in range(4):
        at = _face_setter(face)
        for _ in range(n_bones):
            u0 = int(rng.integers(1, V - 8))
            w0 = int(rng.integers(3, h - 5))
            kind = rng.random()
            if kind < 0.6:      # 長い骨（横）
                for u in range(u0, u0 + int(rng.integers(5, 9))):
                    for w in (w0, w0 + 1):
                        v[at(u, w, 0)] = K('bone_agd_lr')
                v[at(u0, w0 - 1, 0)] = K('bone_agd_lr_d')
                v[at(u0, w0 + 2, 0)] = K('bone_agd_lr_d')
            else:               # 頭蓋（丸）
                for u in range(u0, u0 + 5):
                    for w in range(w0, w0 + 5):
                        if (u - u0 - 2) ** 2 + (w - w0 - 2) ** 2 <= 5:
                            v[at(u, w, 0)] = K('bone_agd_lr')
                v[at(u0 + 1, w0 + 2, 0)] = K('clay_agd_lr_dd')
                v[at(u0 + 3, w0 + 2, 0)] = K('clay_agd_lr_dd')
    return one(v)


def _wall_gt(name, bedrock):
    """gauntlet — 鉄の帯で補強した厚い城壁の石。大きな石を 1 ボクセル引っ込め、**鉄の帯 2 本**を
    外皮に残して鋲を打つ。岩盤は帯を持たない（厚い壁の中身）。"""
    rng = rng_for(name)
    h = WALL_H
    stone = [K('rock_agd_gt'), K('rock_agd_gt_l'), K('rock_agd_gt_d')]

    def shade(face, ci, u):
        if bedrock:
            return int(rng.choice([stone[2], K('rock_agd_gt_j'), stone[0]], p=[0.55, 0.25, 0.2]))
        return int(rng.choice(stone, p=[0.6, 0.2, 0.2]))

    v = block_wall(rng, h, K('rock_agd_gt_j'), _courses(h, 10, 8, (12, 18)), shade,
                   [1] if not bedrock else [0, 1], [1.0] if not bedrock else [0.6, 0.4],
                   [stone[2], stone[0]] if not bedrock else [stone[2]])
    if not bedrock:
        for face in range(4):
            at = _face_setter(face)
            for zb in (8, 21):
                for u in range(V):
                    for w in range(zb, zb + 3):
                        v[at(u, w, 0)] = K('iron_agd_gt')
                        v[at(u, w, 1)] = K('iron_agd_gt_d')
                    if u % 6 == 2:
                        v[at(u, zb + 1, 0)] = K('iron_agd_gt_l')   # 鋲
            #! 錆の垂れ（帯の下）。
            for _ in range(int(rng.integers(1, 3))):
                u0 = int(rng.integers(0, V - 1))
                zb = int(rng.choice([8, 21]))
                for w in range(max(1, zb - int(rng.integers(3, 6))), zb):
                    for d in range(2):
                        pos = at(u0, w, d)
                        if v[pos] != 0:
                            v[pos] = K('rust_agd_gt')
                            break
    return one(v)


def _wall_hc(name, bedrock):
    """hard centre — 黒曜石のような黒い壁。目地の無い一枚岩に**斜めの割れ面**（明るい面）と、
    赤く光る裂け目（1 深・底が溶岩の色）。岩盤は裂け目を持たない。"""
    rng = rng_for(name)
    h = WALL_H
    v = np.full((V, V, h), K('obs_agd_hc'), np.int16)
    v[:, :, h - 1] = K('obs_agd_hc_d')
    for face in range(4):
        at = _face_setter(face)
        #! 割れ面: 斜めの帯を明るい色で（形は変えない。黒曜石の貝殻状の割れ口）。
        for _ in range(int(rng.integers(2, 4))):
            u0 = int(rng.integers(-8, V))
            width = int(rng.integers(3, 7))
            slope = float(rng.choice([0.5, 1.0, 2.0]))
            c = K('obs_agd_hc_l') if rng.random() < 0.6 else K('obs_agd_hc_h')
            for w in range(1, h - 1):
                uc = int(u0 + w / slope)
                for u in range(uc, uc + width):
                    if 0 <= u < V:
                        v[at(u, w, 0)] = c
        #! 面の凹凸は 1 ボクセルの段だけ（滑らかさを保つ）。
        for _ in range(3):
            u0, w0 = int(rng.integers(0, V - 6)), int(rng.integers(2, h - 8))
            carve_relief(v, face, u0, u0 + int(rng.integers(4, 9)), w0, w0 + int(rng.integers(4, 8)), 1)
            paint_face(v, face, u0, u0 + 9, w0, w0 + 8, 1, K('obs_agd_hc_d'))
        #! 裂け目は控えめに（暗い赤だけ・4 割）。壁の線は地の色に近づける。
        if not bedrock and rng.random() < 0.4:
            u = int(rng.integers(3, V - 4))
            w0 = int(rng.integers(2, h - 12))
            for w in range(w0, w0 + int(rng.integers(6, 12))):
                v[at(u, w, 0)] = 0
                v[at(u, w, 1)] = K('lava_agd_hc_d')
                if rng.random() < 0.3:
                    u += int(rng.choice([-1, 1]))
                    u = min(V - 2, max(1, u))
    return one(v)


# ------------------------------------------------------------------ 登録
def names():
    """このファイルと小物のファイルが登録する名前の一覧（`angband_dungeon_names.txt` の元）。"""
    out = []
    for p in PROFILES:
        out += ['floor_agd_%s_%02d' % (p, i) for i in range(1, FLOOR_VARIANTS + 1)]
        out += ['wall_agd_%s_%02d' % (p, i) for i in range(1, WALL_VARIANTS + 1)]
        out += ['bedrock_agd_%s_%02d' % (p, i) for i in range(1, BEDROCK_VARIANTS + 1)]
    return out


def _load_props():
    spec = importlib.util.spec_from_file_location('angband_dungeon_props', os.path.join(HERE, 'angband_dungeon_props.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def register(g):
    """`gen_prefabs.py` の名前空間 `g` へ Angband のダンジョンの素材を足す。"""
    global G, C, V, FLOOR_H, WALL_H
    G = g
    C = g['C']
    V = g['V']
    FLOOR_H = g['FLOOR_H']
    WALL_H = g['WALL_H']
    _palette()
    cat = g['CATALOG']
    static = g['static_part']
    labels = {'cl': 'classic', 'md': 'modified', 'mo': 'moria', 'lb': 'labyrinth',
              'cv': 'cavern', 'lr': 'lair', 'gt': 'gauntlet', 'hc': 'hard centre'}
    for p in PROFILES:
        for i in range(1, FLOOR_VARIANTS + 1):
            cat['floor_agd_%s_%02d' % (p, i)] = (floor_agd, static(), 'Angband のダンジョンの床（%s。部屋・通路とも）' % labels[p])
        for i in range(1, WALL_VARIANTS + 1):
            cat['wall_agd_%s_%02d' % (p, i)] = (wall_agd, static(), 'Angband のダンジョンの壁（%s）' % labels[p])
        for i in range(1, BEDROCK_VARIANTS + 1):
            cat['bedrock_agd_%s_%02d' % (p, i)] = (bedrock_agd, static(), 'Angband のダンジョンの岩盤（%s。壁と同じ材で装飾を落とし暗く）' % labels[p])
    props = _load_props()
    props.register_props(globals())
