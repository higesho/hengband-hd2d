# -*- coding: utf-8 -*-
"""DCSS の祭壇・噴水・像・木・店・隠し場所・台座・鉄格子を、**絵を解釈して一から**ボクセルの物にする。

タイルの模様を板に写すのではなく、絵に描かれた物（祭壇なら台と飾り、泉なら水盤と水）を立体として組み立てる。
色は絵から拾う（`tile_colours`・`clusters`）ので、神ごと・種類ごとの色の違いは絵のとおりになる。
形は私が絵を見て決めた（祭壇 29 柱ぶんの飾りの解釈は `ALTARS` の各関数。何を見立てたかは各関数の 1 行目に書いた）。

共通の約束（`dcss_vox.py`）: 1 マス = 32 ボクセル、+x = 東、+y = 南（正面）、+z = 上。物は地面へ 3 ボクセル沈む（z = 0〜2 は埋まる）。
`gen_dcss_terrain.py` が呼ぶ（`BUILDERS` に種類 -> 作る関数を登録）。
"""
import os

import numpy as np
from PIL import Image
from scipy import ndimage

import dcss_vox as dv
from dcss_vox import (t2s, V, BURIED, Palette, Canvas, load_tile, lum, shade, mix, read_rgba, tile_colours, write_pair)

Z0 = BURIED          # 物の見える底（これより下は地面に埋まる）


def mat_any(c):
    """絵から作る物の材質の当て方: 緑 = plant、金 = metal、茶 = wood、明るい青緑 = clean、ほかは stone。"""
    r, g, b = c
    sat, mx = dv.hsv(c)
    if g > r + 25 and g > b + 15:
        return 'plant'
    if sat > 0.45 and r > 150 and g > 100 and b < 90 and r >= g:
        return 'metal'
    if sat > 0.35 and r > g + 25 and r > b + 25 and mx < 0.75:
        return 'wood'
    if b > r + 50 and mx > 0.6:
        return 'clean'
    return 'stone'


def clusters(rel, box=None, n=4, minlum=0):
    """絵（または (x0,y0,x1,y1)）の主な色を n 色、暗い順に返す（メディアンカット）。"""
    a = read_rgba(rel)
    if box:
        x0, y0, x1, y1 = box
        a = a[y0:y1, x0:x1]
    m = a[..., 3] >= 128
    px = a[..., :3][m]
    if minlum:
        px = px[lum(px.astype(float)) >= minlum]
    if px.shape[0] == 0:
        return [(128, 128, 128)] * n
    im = Image.fromarray(px.reshape(-1, 1, 3).astype(np.uint8), 'RGB')
    k = min(n, max(1, len({tuple(p) for p in px})))
    q = im.quantize(colors=k, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = np.asarray(q.getpalette()[:k * 3], np.uint8).reshape(-1, 3)
    used = sorted({int(i) for i in np.asarray(q).ravel()})
    cols = [tuple(int(v) for v in pal[i]) for i in used]
    cols.sort(key=lambda c: 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2])
    while len(cols) < n:
        cols.append(cols[-1])
    return cols


def rng_for(name):
    return np.random.default_rng(sum(ord(ch) * (i + 7) for i, ch in enumerate(name)))


def octa(c, cx, cy, cz, r, col, mat=None, ry=None):
    X, Y, Z = c._g
    ry = r if ry is None else ry
    m = (abs(X + 0.5 - cx) / r + abs(Y + 0.5 - cy) / ry + abs(Z + 0.5 - cz) / r) <= 1.0
    c.a[m] = c.pal(col, mat)
    return m


def new_canvas(h=36):
    pal = Palette(mat_any)
    return Canvas(V, V, h, pal), pal


def finish(name, c, pal, note):
    size, ncol, nfp = write_pair(name, [('main', c.a, (0, 0, 0))], pal, note)
    return '%dx%dx%d' % c.a.shape, ncol, size, '', nfp


def plinth(c, cols, tiers=((26, 20, 3), (22, 17, 3), (18, 14, 2)), cx=16, cy=16):
    """段々の台（下ほど広い）。cols = (暗, 中, 明)。台の天面の z を返す。埋まる部分（z < Z0）は最下段の幅で満たす。"""
    dark, mid, light = cols[0], cols[len(cols) // 2], cols[-1]
    w0, d0, _ = tiers[0]
    c.box(cx - w0 / 2, cy - d0 / 2, 0, cx + w0 / 2, cy + d0 / 2, Z0, shade(mid, 0.8))
    z = Z0
    for k, (w, d, h) in enumerate(tiers):
        col = mid if k % 2 == 0 else light
        c.box(cx - w / 2, cy - d / 2, z, cx + w / 2, cy + d / 2, z + h, col)
        # 前縁の暗い線（段の陰）
        c.box(cx - w / 2, cy + d / 2 - 1, z, cx + w / 2, cy + d / 2, z + 1, dark)
        z += h
    return z


def base_cols(rel):
    return clusters(rel, (0, 22, 32, 32), 3)


# =================================================================== 祭壇 29 柱
def alt_zin(c, zt, cl, rel):                      # ジン: 銀色の台座に、棘のある星形の光（手のひらを掲げた浮き彫りを星に見立てた）
    s = (205, 215, 225)
    cz = zt + 10
    c.ell(16, 16, cz, 5, 3.5, 5, s)
    for k in range(8):
        a = k * np.pi / 4
        c.line((16, 16, cz), (16 + 12 * np.cos(a), 16, cz + 12 * np.sin(a)), 1.7, shade(s, 1.0 if k % 2 else 0.85))
    c.ell(16, 18, cz, 2.5, 1.5, 2.5, (245, 250, 255))
    for (x, z) in ((6, 4), (26, 3)):
        octa(c, x, 16, zt + z + 9, 2, (255, 255, 255))


def alt_shining_one(c, zt, cl, rel):              # 輝ける者: 金の台座に、翼を広げた鷲と「1」の印
    g = cl[-1]
    gd = shade(g, 0.7)
    c.cyl(16, 16, zt, zt + 12, 3.2, 2.2, g)
    c.ell(16, 16, zt + 13, 3.2, 3, 3, g)
    for sgn in (-1, 1):
        c.line((16 + sgn * 2, 16, zt + 12), (16 + sgn * 11, 16, zt + 18), 1.6, g)
        c.line((16 + sgn * 11, 16, zt + 18), (16 + sgn * 13, 16, zt + 13), 1.3, gd)
        c.line((16 + sgn * 6, 16, zt + 14), (16 + sgn * 11, 16, zt + 12), 1.2, gd)
    c.box(15, 18, zt + 3, 17, 19.5, zt + 11, shade(g, 1.15))     # 「1」の印
    c.box(13.5, 18, zt + 10, 15, 19.5, zt + 11.5, shade(g, 1.15))


def alt_kikubaaqudgha(c, zt, cl, rel):            # キクバークッガー: 髑髏の山
    bone = (214, 208, 190)
    dk = (40, 36, 34)

    def skull(x, y, z, r=3.2):
        c.ell(x, y, z, r, r, r, bone)
        for sx in (-1.3, 1.3):
            c.ell(x + sx, y + r - 0.6, z + 0.6, 0.95, 0.9, 1.0, dk)
        c.box(x - 0.5, y + r - 0.6, z - 1.2, x + 0.5, y + r + 0.4, z - 0.6, dk)

    for k in range(6):
        a = k * np.pi / 3
        skull(16 + 8 * np.cos(a), 16 + 5 * np.sin(a), zt + 3.2)
    for k in range(4):
        a = k * np.pi / 2 + 0.6
        skull(16 + 4.5 * np.cos(a), 16 + 3 * np.sin(a), zt + 8.3)
    skull(16, 16, zt + 12.6)


def alt_yredelemnul(c, zt, cl, rel):              # イレデルヌル: 紫の縁取りの黒いオベリスクと髑髏の印
    blk, pur = (36, 32, 44), (160, 64, 220)
    for k in range(22):
        t = k / 21.0
        w = 12 - 4 * t
        d = 6 - 2 * t
        c.box(16 - w / 2, 16 - d / 2, zt + k, 16 + w / 2, 16 + d / 2, zt + k + 1, blk)
    c.box(10.2, 19, zt, 10.9, 19.6, zt + 18, pur)
    c.box(21.1, 19, zt, 21.8, 19.6, zt + 18, pur)
    c.ell(16, 18.5, zt + 9, 2.4, 1.0, 2.4, pur)
    c.box(14.8, 18.8, zt + 7, 15.4, 19.6, zt + 8.2, blk)
    c.box(16.6, 18.8, zt + 7, 17.2, 19.6, zt + 8.2, blk)
    c.box(15.6, 18.8, zt + 5.4, 16.4, 19.6, zt + 6.4, blk)


def alt_xom(c, zt, cl, rel):                      # ゾム: 笑う石の顔
    st = (150, 150, 152)
    c.ell(16, 16, zt + 10, 9, 8, 9, st)
    c.ell(16, 16, zt + 10, 9, 8, 9, st)
    dk = (36, 36, 38)
    for sx in (-3.6, 3.6):
        c.ell(16 + sx, 23.2, zt + 12, 2.0, 1.4, 1.8, dk)
        c.box(16 + sx - 2.4, 23.0, zt + 14.2, 16 + sx + 2.4, 24.2, zt + 15.2, shade(st, 0.7))
    c.box(15, 23.8, zt + 9, 17, 25, zt + 12, shade(st, 1.1))
    for i in range(-5, 6):                                   # 笑った口の弧
        c.box(16 + i, 23.0, zt + 6 + (i * i) // 7, 17 + i, 24.2, zt + 7 + (i * i) // 7, dk)


def alt_vehumet(c, zt, cl, rel):                  # ヴェフメット: 光る水晶の板（格子の文様のある青緑の板）
    cy_ = (90, 235, 225)
    c.box(7, 15, zt + 1, 25, 17.5, zt + 23, (28, 96, 104))
    for x in (7, 24):
        c.box(x, 14.5, zt + 1, x + 1, 18, zt + 23, cy_)
    for z in (1, 22):
        c.box(7, 14.5, zt + z, 25, 18, zt + z + 1, cy_)
    for x in range(10, 24, 3):
        c.box(x, 17, zt + 3, x + 1, 18, zt + 21, (150, 255, 245))
    for z in range(5, 21, 3):
        c.box(9, 17, zt + z, 24, 18, zt + z + 1, (150, 255, 245))


def alt_okawaru(c, zt, cl, rel):                  # オカワル: 台に立てた剣と盾
    st, dk = (190, 196, 206), (110, 115, 125)
    c.box(15, 15, zt, 17, 17, zt + 20, st)
    c.box(14, 15, zt + 19, 18, 17, zt + 21, shade(st, 1.1))
    c.box(10, 15, zt + 14, 22, 17, zt + 16, dk)
    c.box(14, 15, zt + 11, 18, 17, zt + 14, (120, 80, 50))
    c.ell(16, 16, zt + 10, 1.8, 1.8, 1.8, dk)
    c.box(10, 19, zt, 22, 21, zt + 8, dk)                    # 盾
    c.box(11, 21, zt + 1, 21, 22, zt + 7, st)
    c.box(15, 21, zt + 1, 17, 22.5, zt + 7, (150, 40, 40))


def alt_makhleb(c, zt, cl, rel):                  # マクレブ: 暗い岩の上で燃える炎
    rock = (86, 56, 60)
    c.ell(16, 16, zt + 1, 11, 8, 4, rock)
    c.ell(10, 16, zt + 4, 5, 5, 4, shade(rock, 1.2))
    c.ell(22, 17, zt + 3, 5, 5, 3.5, shade(rock, 0.9))
    for (x, y, h, r) in ((16, 16, 20, 5.5), (10, 16, 13, 3.6), (22, 16, 15, 4.2)):
        c.cyl(x, y, zt + 4, zt + 4 + h, r, 0.3, (240, 90, 20), mat='clean')
        c.cyl(x, y, zt + 4, zt + 4 + h * 0.7, r * 0.6, 0.2, (255, 170, 40), mat='clean')
        c.cyl(x, y, zt + 4, zt + 4 + h * 0.4, r * 0.3, 0.2, (255, 235, 120), mat='clean')


def alt_sif_muna(c, zt, cl, rel):                 # シフ・ムナ: 青い台座の上に浮かぶ氷の結晶と水のしずく
    bl, ic = (40, 80, 200), (150, 210, 255)
    c.cyl(16, 16, zt, zt + 4, 7, 5, bl)
    octa(c, 16, 16, zt + 11, 8, ic, mat='clean', ry=6)
    octa(c, 15, 18, zt + 12, 3, (235, 250, 255), mat='clean')
    for (x, y, z, r) in ((6, 16, 13, 1.6), (26, 15, 9, 1.3), (22, 20, 19, 1.4), (9, 14, 20, 1.1)):
        c.ell(x, y, zt + z, r, r, r, (120, 190, 255), mat='clean')


def alt_trog(c, zt, cl, rel):                     # トログ: 血に濡れた台と、斧を振り上げる赤い衣の戦士
    red, skin = (180, 30, 34), (238, 198, 168)
    c.ell(16, 16, zt + 0.5, 11, 7, 1.5, (150, 20, 24))
    c.box(11, 16, zt + 1, 14, 19, zt + 7, red)
    c.box(17, 16, zt + 1, 20, 19, zt + 7, red)
    c.box(10.5, 15, zt + 7, 20.5, 20, zt + 14, red)
    c.ell(15.5, 17.5, zt + 16.5, 2.6, 2.6, 2.7, skin)
    c.ell(15.5, 17.5, zt + 17.6, 2.8, 2.8, 1.9, (30, 24, 24))
    c.line((20, 17, zt + 12), (24, 17, zt + 21), 1.0, (120, 80, 50))
    c.box(22.5, 16, zt + 20, 27, 19, zt + 24, (180, 186, 196))


def alt_nemelex(c, zt, cl, rel):                  # ネメレクス・ゾベー: 金で縁取った箱から扇に広がるカード
    wd, gold = (70, 46, 34), (232, 190, 62)
    c.box(8, 12, zt, 24, 21, zt + 7, wd)
    c.box(8, 12, zt + 7, 24, 21, zt + 8, gold)
    for x in (8, 23):
        c.box(x, 12, zt, x + 1, 21, zt + 8, gold)
    c.box(8, 20, zt, 24, 21, zt + 1, gold)
    c.ell(16, 21, zt + 4, 1.8, 1.0, 1.8, (160, 60, 210))
    for ang, col in ((-38, (210, 54, 54)), (-13, (64, 172, 72)), (13, (62, 96, 206)), (38, (244, 214, 62))):
        a = np.radians(ang)
        c.line((16, 16.5, zt + 8), (16 + 11 * np.sin(a), 16.5, zt + 8 + 11 * np.cos(a)), 1.5, col)
    for (x, z) in ((6, 18), (27, 17), (16, 22)):
        octa(c, x, 16, zt + z, 1.8, (255, 240, 150))


def alt_elyvilon(c, zt, cl, rel):                 # エリヴィロン: 白い聖杯
    w = (236, 236, 242)
    c.cyl(16, 16, zt, zt + 2, 6, 5, w)
    c.cyl(16, 16, zt + 2, zt + 9, 1.8, 1.6, w)
    c.ell(16, 16, zt + 9, 3.2, 3.2, 1.4, w)
    c.cyl(16, 16, zt + 9, zt + 17, 3.4, 7.5, w, hollow=1.2)
    c.cyl(16, 16, zt + 9, zt + 11, 3, 3, w)


def alt_lugonu(c, zt, cl, rel):                   # ルゴヌ: 紫の渦が刻まれた白い大理石の柱
    mb, pu = (226, 226, 238), (130, 60, 192)
    c.box(11, 12, zt, 21, 20, zt + 24, mb)
    c.box(10, 11, zt + 23, 22, 21, zt + 25, shade(mb, 0.9))
    c.ell(16, 20, zt + 13, 3.4, 1.4, 3.4, shade(pu, 1.3))
    for t in np.linspace(0, 3.2 * np.pi, 40):
        r = 0.6 + t * 0.42
        c.box(16 + r * np.cos(t) * 0.9, 20, zt + 13 + r * np.sin(t), 17 + r * np.cos(t) * 0.9, 21, zt + 14 + r * np.sin(t), pu)
    for (x, z) in ((12, 5), (19, 19), (13, 20)):
        c.box(x, 20, zt + z, x + 2, 21, zt + z + 3, shade(pu, 0.7))


def alt_beogh(c, zt, cl, rel):                    # ベオ: 穴の開いた立石と、炎の燃える鉢
    st = cl[-1]
    c.box(8, 14, zt, 20, 19.5, zt + 19, st)
    c.box(9, 14, zt + 19, 19, 19.5, zt + 21, shade(st, 0.9))
    X, Y, Z = c._g
    c.clear((((X + 0.5 - 14) ** 2 + (Z + 0.5 - (zt + 12)) ** 2) <= 3.6 ** 2) & (Y >= 13))
    c.cyl(23, 22, zt, zt + 4, 4.5, 3.2, shade(st, 0.7))
    c.cyl(23, 22, zt + 4, zt + 11, 3.2, 0.3, (240, 100, 24), mat='clean')
    c.cyl(23, 22, zt + 4, zt + 8, 1.8, 0.3, (255, 200, 60), mat='clean')


def alt_jiyva(c, zt, cl, rel):                    # ジーヴァ: 緑の台座から盛り上がる粘液の球
    gr = (36, 130, 54)
    c.cyl(16, 16, zt, zt + 6, 8.5, 5.5, gr)
    c.cyl(16, 16, zt + 5, zt + 7, 6, 6, shade(gr, 0.8))
    c.ell(16, 16, zt + 12.5, 6.2, 6.2, 6.2, (64, 200, 78), mat='plant')
    c.ell(14, 18, zt + 15, 2.0, 1.6, 2.0, (200, 255, 170), mat='plant')
    for (x, y) in ((8, 20), (24, 19)):
        c.ell(x, y, zt + 2, 1.8, 1.8, 1.4, (64, 200, 78), mat='plant')


def alt_fedhas(c, zt, cl, rel):                   # フェダス: 蔦と果実に覆われた木の板と太陽の印
    wd = (108, 66, 54)
    c.box(7, 14, zt, 25, 19, zt + 22, wd)
    c.box(9, 14, zt + 22, 23, 19, zt + 24, shade(wd, 0.9))
    c.ring(16, 19.5, zt + 12, 4.2, 0.9, (232, 184, 64), axis='y')
    c.ell(16, 19.5, zt + 12, 1.5, 1.0, 1.5, (255, 220, 100))
    for k in range(40):
        t = k / 39.0
        c.ell(7 + 18 * t, 20, zt + 2 + 18 * t + 2.5 * np.sin(t * 14), 1.0, 1.0, 1.0, (60, 140, 62), mat='plant')
        c.ell(25 - 18 * t, 20, zt + 3 + 17 * t + 2.5 * np.cos(t * 12), 0.9, 0.9, 0.9, (46, 112, 52), mat='plant')
    for (x, z) in ((10, 5), (22, 8), (12, 18), (23, 20)):
        c.ell(x, 20.5, zt + z, 1.7, 1.7, 1.7, (150, 60, 170))


def alt_cheibriados(c, zt, cl, rel):               # チェイブリアドス: 砂時計と這うカタツムリ
    sand, fr = (232, 204, 124), (118, 80, 42)
    for (x, y) in ((8, 10), (24, 10), (8, 22), (24, 22)):
        c.cyl(x, y, zt, zt + 17, 1.0, 1.0, fr)
    c.box(7, 9, zt + 17, 25, 23, zt + 18.5, fr)
    c.cyl(16, 16, zt + 1, zt + 8.5, 6.5, 1.0, sand)
    c.cyl(16, 16, zt + 8.5, zt + 16, 1.0, 6.5, shade(sand, 1.1))
    c.ell(22, 24, zt + 3, 2.6, 2.6, 2.6, (186, 206, 78))
    c.box(18, 22.5, zt, 25, 26, zt + 1.6, (140, 170, 88))
    c.line((18, 24, zt + 1.6), (17, 25, zt + 3.6), 0.6, (140, 170, 88))


def alt_ashenzari(c, zt, cl, rel):                # アシェンザリ: 割れた石像のかたまりと、割れ目の赤い光
    st, cr = (126, 120, 120), (244, 96, 34)
    c.ell(13, 16, zt + 8, 5.5, 4.5, 7.5, st)
    c.ell(20, 17, zt + 7, 4.5, 4.2, 6.5, shade(st, 1.12))
    c.ell(16, 16, zt + 16, 4.2, 3.5, 3.2, shade(st, 0.9))
    c.line((9, 19.5, zt + 3), (13, 20, zt + 9), 0.9, cr, mat='clean')
    c.line((13, 20, zt + 9), (17, 20.5, zt + 7), 0.9, cr, mat='clean')
    c.line((17, 20.5, zt + 7), (21, 20.5, zt + 12), 0.9, cr, mat='clean')
    c.line((16, 19, zt + 14), (18, 19, zt + 18), 0.8, cr, mat='clean')


def alt_dithmenos(c, zt, cl, rel):                # ディスメノス: 暗い宝珠と紫の光輪
    c.cyl(16, 16, zt, zt + 8, 3.2, 2.4, cl[0])
    c.ell(16, 16, zt + 12.5, 7, 7, 7, (22, 14, 34))
    c.ring(16, 16, zt + 12.5, 9.5, 0.9, (176, 72, 224), axis='y', mat='clean')
    c.ring(16, 16, zt + 12.5, 8.0, 0.6, (120, 50, 170), axis='y', mat='clean')
    c.ell(13, 20, zt + 15, 1.6, 1.2, 1.6, (110, 84, 150))


def alt_gozag(c, zt, cl, rel):                    # ゴザーグ: 金の台座の上の大きなダイヤモンドと光輪
    g = (228, 182, 52)
    c.box(7, 11, zt, 25, 21, zt + 5, g)
    c.box(9, 12, zt + 5, 23, 20, zt + 7, shade(g, 1.1))
    octa(c, 16, 16, zt + 13, 8, (255, 224, 70), ry=4)
    octa(c, 15, 18, zt + 14, 3, (255, 248, 190))
    c.ring(16, 16, zt + 13, 11, 0.8, (255, 238, 150), axis='y', mat='clean')


def alt_qazlal(c, zt, cl, rel):                   # カザラル: 橙の二本の角と、その間を走る青い稲妻
    og = (232, 116, 52)
    for sgn in (-1, 1):
        pts = [(16 + sgn * 4, 16, zt), (16 + sgn * 8, 16, zt + 8), (16 + sgn * 5, 16, zt + 17)]
        c.line(pts[0], pts[1], 1.7, og)
        c.line(pts[1], pts[2], 1.5, og)
        c.line((16 + sgn * 8, 16, zt + 8), (16 + sgn * 11.5, 16, zt + 13), 1.1, og)
    c.line((11, 16, zt + 17), (16, 16, zt + 12), 0.9, (150, 200, 255), mat='clean')
    c.line((16, 16, zt + 12), (21, 16, zt + 17), 0.9, (150, 200, 255), mat='clean')
    c.line((16, 16, zt + 12), (16, 16, zt + 7), 0.9, (200, 230, 255), mat='clean')


def alt_ru(c, zt, cl, rel):                       # ルー: 天秤（赤い皿と青い皿）
    gr = (134, 140, 148)
    c.cyl(16, 16, zt, zt + 22, 1.3, 1.1, gr)
    c.box(4, 15, zt + 21, 28, 17, zt + 23, gr)
    c.ell(16, 16, zt + 23.5, 2.2, 2.2, 2.2, shade(gr, 1.2))
    for (x, z, col) in ((6, 12, (192, 52, 62)), (26, 14, (84, 124, 234))):
        c.line((x, 16, zt + 21), (x - 3, 16, zt + z + 3), 0.4, gr)
        c.line((x, 16, zt + 21), (x + 3, 16, zt + z + 3), 0.4, gr)
        c.cyl(x, 16, zt + z, zt + z + 3, 2.0, 4.8, col, hollow=1.0)
        c.cyl(x, 16, zt + z, zt + z + 1.5, 2.2, 2.2, col)


def alt_ecumenical(c, zt, cl, rel):               # 諸神共通: 灰色の墓石に金の板、両脇の蝋燭
    gy = (150, 150, 156)
    c.box(8, 12, zt, 24, 21, zt + 15, gy)
    c.box(9, 12, zt + 15, 23, 21, zt + 16.5, shade(gy, 0.9))
    c.box(13, 21, zt + 2, 19, 22, zt + 6, (242, 192, 62))
    for x in (4, 27):
        c.cyl(x, 17, zt, zt + 9, 1.3, 1.3, (236, 230, 220))
        c.ell(x, 17, zt + 10.5, 1.0, 1.0, 1.8, (255, 160, 40), mat='clean')


def alt_pakellas(c, zt, cl, rel):                 # パケラス: 金の天球儀に収まる桃色の宝珠
    gd = (222, 182, 62)
    c.ell(16, 16, zt + 12, 4.6, 4.6, 4.6, (232, 52, 124), mat='clean')
    c.ring(16, 16, zt + 12, 8.5, 0.9, gd, axis='y')
    c.ring(16, 16, zt + 12, 8.5, 0.9, gd, axis='x')
    c.ring(16, 16, zt + 12, 7, 0.9, gd, axis='z')
    for (x, y) in ((9, 10), (23, 10), (9, 22), (23, 22)):
        c.line((x, y, zt), (16 + (x - 16) * 0.4, 16 + (y - 16) * 0.4, zt + 9), 0.7, gd)


def alt_uskayaw(c, zt, cl, rel):                  # ウスカヤウ: 橙の面を持つ大きな太鼓と飾り房
    X, Y, Z = c._g
    cz = zt + 10
    body = ((X + 0.5 - 16) ** 2 + (Z + 0.5 - cz) ** 2 <= 9.5 ** 2) & (Y >= 9) & (Y < 23)
    c.a[body] = c.pal((122, 62, 32))
    face = ((X + 0.5 - 16) ** 2 + (Z + 0.5 - cz) ** 2 <= 8.0 ** 2) & (Y >= 22) & (Y < 23)
    c.a[face] = c.pal((236, 134, 44))
    rg = ((X + 0.5 - 16) ** 2 + (Z + 0.5 - cz) ** 2 <= 5.2 ** 2) & ((X + 0.5 - 16) ** 2 + (Z + 0.5 - cz) ** 2 >= 3.6 ** 2) & (Y >= 22) & (Y < 23)
    c.a[rg] = c.pal((176, 40, 34))
    for (x, col) in ((7, (210, 50, 50)), (25, (240, 200, 60))):
        c.ell(x, 22, zt + 3, 1.8, 1.8, 3, col)


def alt_hepliaklqana(c, zt, cl, rel):             # ヘプリアクラカナ: 苔むした石板と、立ちのぼる青緑の霊気
    c.box(11, 15, zt, 21, 19, zt + 18, (84, 98, 92))
    for (x, z, r) in ((11, 14, 4.5), (21, 16, 4), (16, 21, 4.2), (15, 10, 3)):
        c.ell(x, 17, zt + z, r, r * 0.8, r, (96, 220, 188), mat='clean')
    c.ell(14, 19, zt + 19, 2, 1.5, 2, (190, 255, 234), mat='clean')


def alt_wu_jian(c, zt, cl, rel):                  # 武剣: 赤い鳥居と、吊るした金の提灯
    rd, dk = (190, 40, 32), (74, 34, 30)
    for x in (6, 23):
        c.box(x, 15, zt, x + 3, 18, zt + 22, dk)
    c.box(3, 14, zt + 20, 29, 19, zt + 23, rd)
    c.box(5, 15, zt + 15, 27, 18, zt + 17, rd)
    c.line((11, 16, zt + 15), (11, 16, zt + 12), 0.4, (60, 50, 40))
    c.ell(11, 16, zt + 9.5, 3, 3, 3.6, (244, 184, 54))
    c.box(9, 14.5, zt + 12.5, 13, 17.5, zt + 13.5, (130, 90, 40))


def alt_ignis(c, zt, cl, rel):                    # イグニス: 淡い色の岩の崖と、縁で燃える火
    rock = (214, 190, 168)
    rng = rng_for('ignis')
    for x in range(4, 28):
        for y in range(9, 24):
            h = 3 + int(11 * (0.5 + 0.5 * np.sin(x * 0.55) * np.cos(y * 0.4)) * (1 - abs(x - 16) / 18.0))
            h += int(rng.integers(0, 3))
            if y > 20:
                h = int(h * 0.5)
            c.box(x, y, zt, x + 1, y + 1, zt + h, shade(rock, 0.8 + 0.4 * (h / 16.0)))
    for (x, h, r) in ((8, 8, 2.5), (14, 11, 3), (22, 7, 2.2)):
        c.cyl(x, 20, zt + 8, zt + 8 + h, r, 0.3, (255, 126, 30), mat='clean')
        c.cyl(x, 20, zt + 8, zt + 8 + h * 0.6, r * 0.55, 0.3, (255, 220, 90), mat='clean')


def alt_unknown(c, zt, cl, rel):                  # 未鑑定: 灰色の台に「?」
    bm = ['.###.', '#...#', '....#', '...#.', '..#..', '..#..', '.....', '..#..']
    for j, row in enumerate(bm):
        for i, ch in enumerate(row):
            if ch == '#':
                c.box(16 - 5 + i * 2, 15, zt + 2 + (len(bm) - 1 - j) * 2, 16 - 5 + i * 2 + 2, 18, zt + 4 + (len(bm) - 1 - j) * 2, (238, 238, 238))


ALTARS = {
    'zin1': alt_zin, 'shining_one': alt_shining_one, 'kikubaaqudgha': alt_kikubaaqudgha, 'yredelemnul': alt_yredelemnul,
    'xom0': alt_xom, 'vehumet1': alt_vehumet, 'okawaru': alt_okawaru, 'makhleb_flame1': alt_makhleb, 'sif_muna1': alt_sif_muna,
    'trog': alt_trog, 'nemelex1': alt_nemelex, 'elyvilon': alt_elyvilon, 'lugonu': alt_lugonu, 'beogh': alt_beogh,
    'jiyva01': alt_jiyva, 'fedhas': alt_fedhas, 'cheibriados': alt_cheibriados, 'ashenzari': alt_ashenzari,
    'dithmenos1': alt_dithmenos, 'gozag0': alt_gozag, 'qazlal0': alt_qazlal, 'ru': alt_ru, 'ecumenical': alt_ecumenical,
    'pakellas0': alt_pakellas, 'uskayaw': alt_uskayaw, 'hep0': alt_hepliaklqana, 'wu_jian': alt_wu_jian, 'ignis': alt_ignis,
    'unknown': alt_unknown,
}
#: 台座が段々でない・台座を別に持つ飾り（台を作らず飾りだけ）
NO_PLINTH = {'ignis'}


def build_altar(name, rel, what, p):
    stem = os.path.basename(rel)
    fn = ALTARS[stem]
    dv.record_use(rel, name)
    c, pal = new_canvas(38)
    cl = base_cols(rel)
    if stem in NO_PLINTH:
        c.box(4, 8, 0, 28, 24, Z0, shade(cl[1], 0.9))
        zt = Z0
    else:
        tiers = ((26, 20, 3), (22, 17, 3), (18, 14, 2))
        if stem in ('gozag0', 'shining_one'):
            tiers = ((26, 20, 3), (22, 17, 3))
        zt = plinth(c, cl, tiers)
    fn(c, zt, cl, rel)
    return finish(name, c, pal, 'DCSS の祭壇 %s（%s）を絵から解釈して一から作った（台と飾り。tools/dcss/gen_dcss_objects.py）' % (what, rel))


# =================================================================== 噴水
def build_fountain(name, rel, what, p):
    dv.record_use(rel, name)
    c, pal = new_canvas(32)
    dv.record_use('dngn/decor/dry_fountain', name)                # 石の色は涸れた噴水の絵（水の色が混ざらない）から取る
    stone = clusters('dngn/decor/dry_fountain', (0, 16, 32, 31), 4, minlum=40)
    stone = [shade(col, 1.25) for col in stone]                   # 絵の石は暗いので少し持ち上げる
    st_mid, st_light = stone[2], stone[-1]
    dry = 'dry' in rel
    # 水盤（縁の石）
    c.cyl(16, 16, 0, Z0 + 6, 14, 14, shade(st_light, 0.8))
    c.cyl(16, 16, Z0 + 5, Z0 + 7, 14, 13, st_light)
    X, Y, Z = c._g
    inner = ((X + 0.5 - 16) ** 2 + (Y + 0.5 - 16) ** 2 <= 11.5 ** 2) & (Z >= Z0 + 2)
    c.clear(inner)
    wc = None
    if 'blue' in rel:
        wc = (0, 196, 222)
    elif 'sparkling' in rel:
        wc = (96, 236, 255)
    elif 'blood' in rel:
        wc = (188, 14, 18)
    if dry:
        c.cyl(16, 16, Z0 + 1, Z0 + 2, 11.5, 11.5, shade(st_mid, 0.6))
    elif 'eyes' in rel:
        c.cyl(16, 16, Z0 + 1, Z0 + 3, 11.5, 11.5, (190, 190, 200))
        rng = rng_for(name)
        for k in range(24):
            a = rng.uniform(0, 2 * np.pi)
            r = rng.uniform(2.5, 9.5)
            x, y = 16 + r * np.cos(a), 16 + r * np.sin(a)
            c.ell(x, y, Z0 + 3.4, 2.3, 2.3, 2.3, (244, 242, 236))
            c.ell(x + 0.3, y + 1.4, Z0 + 3.8, 1.1, 1.0, 1.1, rng.choice([0, 1]) and (120, 60, 160) or (60, 120, 200))
            c.ell(x + 0.3, y + 2.1, Z0 + 3.9, 0.5, 0.5, 0.5, (20, 20, 24))
    else:
        c.cyl(16, 16, Z0 + 1, Z0 + 4.5, 11.5, 11.5, wc, mat='clean')
        c.cyl(16, 16, Z0 + 4, Z0 + 5, 9, 9, shade(wc, 1.15), mat='clean')
    # 絵の中央にあるのは怪物の像ではなく、くびれた石の柱の上に載る、縁が 3 つに波打つ浅い鉢。
    # 柱の途中の噴き出し口から細い水が真下に落ち、水面で泡立つ（涸れた噴水・目玉の噴水は口が暗い穴のまま）。
    foot, neck = stone[1], stone[-1]
    c.cyl(16, 16, Z0, Z0 + 3, 5.8, 4.2, st_mid)
    c.cyl(16, 16, Z0 + 3, Z0 + 10, 3.0, 2.3, neck)
    c.cyl(16, 16, Z0 + 10, Z0 + 12, 4.6, 4.6, st_mid)
    cup = c.cyl(16, 16, Z0 + 12, Z0 + 19, 4.2, 9.2, st_light, hollow=1.4)
    c.cyl(16, 16, Z0 + 12, Z0 + 14, 3.4, 3.4, shade(st_mid, 0.7))              # 鉢の底
    X, Y, Z = c._g
    for k in range(3):                                                         # 縁の波打ち（3 か所のくぼみ）
        a = np.pi / 2 + k * 2 * np.pi / 3
        c.clear(((X + 0.5 - (16 + 9.4 * np.cos(a))) ** 2 + (Y + 0.5 - (16 + 9.4 * np.sin(a))) ** 2 <= 2.6 ** 2) & (Z >= Z0 + 17.5))
    c.box(15, 18, Z0 + 7.5, 17, 20, Z0 + 10, shade(neck, 0.8))               # 噴き出し口（柱の前）
    c.box(15.5, 19.5, Z0 + 8, 16.5, 20.5, Z0 + 9, shade(stone[0], 0.9))
    if wc is not None:
        jet = mix(wc, (255, 255, 255), 0.55)                                   # 落ちる水は水面より白っぽい
        water_top = Z0 + 4.5
        c.box(15, 20, water_top, 17, 22, Z0 + 8, jet, mat='clean')           # 真下に落ちる細い水
        c.box(15, 20, Z0 + 6, 17, 22, Z0 + 8, mix(jet, (255, 255, 255), 0.5), mat='clean')
        foam = shade(wc, 1.4) if 'sparkling' not in rel else (255, 255, 255)
        for a in np.linspace(0, 2 * np.pi, 8, endpoint=False):                 # 水面の泡
            c.ell(16 + 3.2 * np.cos(a), 20.5 + 3.2 * np.sin(a) * 0.8, water_top + 0.3, 1.1, 1.1, 0.8, foam, mat='clean')
        rng = rng_for(name)
        for k in range(14):                                                    # 水面のきらめき
            a = rng.uniform(0, 2 * np.pi)
            r = rng.uniform(2, 9.5)
            c.box(16 + r * np.cos(a), 16 + r * np.sin(a), water_top, 17 + r * np.cos(a), 17 + r * np.sin(a), water_top + 1, shade(wc, 1.3), mat='clean')
        if 'sparkling' in rel:
            for (x, y, z) in ((8, 20, 8), (24, 19, 9), (20, 9, 8), (10, 11, 9), (22, 24, 8)):
                octa(c, x, y, Z0 + z, 1.4, (255, 255, 255), mat='clean')
    return finish(name, c, pal, 'DCSS の噴水 %s（%s）を絵から解釈して一から作った（水盤・水・くびれた柱と波打つ鉢・噴き出し口と落ちる水。tools/dcss/gen_dcss_objects.py）' % (what, rel))


# =================================================================== 台座
def build_dais(name, rel, what, p):
    dv.record_use(rel, name)
    c, pal = new_canvas(20)
    cl = clusters(rel, None, 4, minlum=40)
    c.box(2, 2, 0, 30, 30, Z0 + 3, cl[1])
    c.box(5, 5, Z0 + 3, 27, 27, Z0 + 6, cl[2])
    c.box(9, 9, Z0 + 6, 23, 23, Z0 + 9, cl[-1])
    c.box(11, 11, Z0 + 9, 21, 21, Z0 + 10, shade(cl[1], 0.8))        # オーブを載せるくぼみ
    return finish(name, c, pal, 'DCSS のオーブの台座 %s（%s）。三段の台（tools/dcss/gen_dcss_objects.py）' % (what, rel))


# =================================================================== 鉄格子
def build_grate(name, rel, what, p):
    dv.record_use(rel, name)
    c, pal = new_canvas(V)
    cl = clusters(rel, None, 4, minlum=40)
    for x in range(1, 31, 6):
        c.box(x, 14, 0, x + 2, 18, V - 1, cl[-1])
        c.box(x, 14, 0, x + 1, 18, V - 1, cl[-2])
    for z in (3, 15, 27):
        c.box(0, 14.5, z, V, 17.5, z + 3, cl[-2])
    return finish(name, c, pal, 'DCSS の鉄格子 %s（%s）。縦の棒と横の桟（向こうが見える。tools/dcss/gen_dcss_objects.py）' % (what, rel))


# =================================================================== 隠し場所（果物・肉・焼き菓子）
def build_cache(name, rel, what, p):
    dv.record_use(rel, name)
    c, pal = new_canvas(26)
    cl = clusters(rel, None, 5, minlum=50)
    wd = (118, 80, 48)
    c.box(5, 8, 0, 27, 24, Z0 + 4, wd)
    c.box(6, 9, Z0 + 3, 26, 23, Z0 + 4, (70, 46, 30))
    rng = rng_for(name)
    spots = [(11, 13), (21, 13), (16, 17), (10, 19), (22, 19), (16, 11), (16, 22)]
    for k, (x, y) in enumerate(spots):
        r = 4.2 if k < 5 else 3.4
        col = cl[(k + 1) % len(cl)]
        z = Z0 + 4 + (3.2 if k == 2 else 1.6)
        c.ell(x, y, z, r, r, r * 0.9, col)
        c.ell(x - 1, y + 1.5, z + r * 0.5, 1.2, 1.0, 1.0, shade(col, 1.3))
    c.ell(16, 16, Z0 + 11, 3.2, 3.0, 2.6, cl[-1])
    return finish(name, c, pal, 'DCSS の隠し場所 %s（%s）。木箱に山盛り（色は絵から。tools/dcss/gen_dcss_objects.py）' % (what, rel))


# =================================================================== 木
def build_tree(name, rel, what, p):
    dv.record_use(rel, name)
    c, pal = new_canvas(36)
    rng = rng_for(name)
    stem = os.path.basename(rel)
    can = clusters(rel, (0, 0, 32, 20), 6, minlum=25)
    if 'petrified' not in stem and 'demonic' not in stem:
        g = [col for col in can if col[1] >= col[0] and col[1] >= col[2]]       # 葉の色（緑）だけ
        can = g if len(g) >= 3 else can
    trunk = clusters(rel, (8, 20, 24, 32), 3, minlum=20)
    X, Y, Z = c._g
    if 'petrified' in stem:
        shape = 'round'
    elif 'mangrove' in stem:
        shape = 'mangrove'
    elif 'demonic' in stem:
        shape = 'twisted'
    else:
        shape = 'round'
    if shape == 'twisted':
        ph = 0.0
        prev = (16, 16, 0)
        for k in range(1, 24):
            t = k / 23.0
            pt = (16 + 3.2 * np.sin(t * 7), 16 + 3.2 * np.cos(t * 7), k * 1.1)
            c.line(prev, pt, 3.0 - 1.4 * t, can[1])
            prev = pt
        top = prev
        for k in range(5):
            a = k * 2 * np.pi / 5
            tip = (top[0] + 9 * np.cos(a), top[1] + 9 * np.sin(a), top[2] + 5 + 2 * (k % 2))
            c.line(top, tip, 1.5, can[-1])
            c.line(tip, (tip[0] + 2 * np.cos(a), tip[1] + 2 * np.sin(a), tip[2] + 4), 1.1, can[-2])
        c.ell(top[0], top[1], top[2] + 2, 3, 3, 3, can[-2])
    else:
        # 幹（根元が少し広がる）
        c.cyl(16, 16, 0, 16, 3.4, 2.0, trunk[1])
        c.cyl(16, 16, 0, 6, 4.4, 3.0, trunk[1])
        for k in range(3):
            a = k * 2.1 + 0.4
            c.line((16, 16, 5), (16 + 7 * np.cos(a), 16 + 7 * np.sin(a), 0.5), 1.1, trunk[-2 if k else -1])
        # 葉（ふくらみの集まり）
        blobs = [(16, 16, 22, 9.5, 8.5, 7), (10, 15, 19, 6, 6, 5), (22, 16, 19.5, 6, 6, 5), (16, 21, 20, 6.5, 5, 5), (15, 10, 21, 6, 5, 5)]
        if shape == 'mangrove':
            blobs = [(16, 16, 22, 12, 11, 5.5), (9, 16, 20, 6, 7, 4), (23, 16, 20, 6, 7, 4)]
        mask = np.zeros(c.shape, bool)
        for (cx, cy, cz, rx, ry, rz) in blobs:
            mask |= ((X + 0.5 - cx) / rx) ** 2 + ((Y + 0.5 - cy) / ry) ** 2 + ((Z + 0.5 - cz) / rz) ** 2 <= 1.0
        zs = np.nonzero(mask.any(axis=(0, 1)))[0]
        zmin, zmax = zs.min(), zs.max()
        idx = np.argwhere(mask)
        for (x, y, z) in idx:
            h = (z - zmin) / max(1, zmax - zmin)
            lit = 0.55 * h + 0.25 * ((x - 16) * -0.05 + (y - 16) * 0.05 + 0.5) + rng.uniform(-0.15, 0.15)
            k = int(np.clip(round(lit * (len(can) - 1) * 1.15), 0, len(can) - 1))
            c.a[x, y, z] = c.pal(can[k])
        if shape == 'mangrove':
            for a in np.linspace(0, 2 * np.pi, 14, endpoint=False):
                r = rng.uniform(8, 11)
                c.line((16 + r * np.cos(a), 16 + r * 0.9 * np.sin(a), 19), (16 + (r + 0.5) * np.cos(a), 16 + (r + 0.5) * 0.9 * np.sin(a), rng.uniform(7, 11)), 0.7, can[1])
            for a in np.linspace(0.3, 2 * np.pi + 0.3, 6, endpoint=False):
                c.line((16, 16, 8), (16 + 10 * np.cos(a), 16 + 10 * np.sin(a), 0.5), 1.2, trunk[-1])
    # 地面へ沈む根元を、足の幅で満たす
    for z in range(Z0):
        layer = c.a[:, :, Z0]
        c.a[:, :, z] = np.where(layer > 0, layer, c.a[:, :, z])
    return finish(name, c, pal, 'DCSS の木 %s（%s）を絵から解釈して一から作った（幹・枝・葉。色は絵から。tools/dcss/gen_dcss_objects.py）' % (what, rel))


# =================================================================== 像
def relief_figure(c, rel, box, z_floor, max_depth=9):
    """絵の人形（box の範囲）の輪郭を、丸み付きの浮き彫りの立体にして台に載せる。輪郭の黒は内側の色に置き換える。"""
    img, op = load_tile(rel, 'figure', max_colours=70)
    x0, y0, x1, y1 = box
    sub = np.zeros_like(op)
    sub[y0:y1] = op[y0:y1]
    img = dv.lift_to(img, sub, 80)
    l = lum(img)
    inner = ndimage.binary_erosion(sub, structure=np.ones((3, 3), bool), border_value=0)
    outline = sub & ~inner & (l < 45)
    good = sub & ~outline & (l >= 45)
    if outline.any() and good.any():
        idx = ndimage.distance_transform_edt(~good, return_distances=False, return_indices=True)
        img = img.copy()
        img[outline] = img[idx[0][outline], idx[1][outline]]
    alpha = np.where(sub, 255, 0).astype(np.uint8)
    thick = t2s.depth_map(alpha, max_depth, 'dome')
    rows = np.nonzero(sub.any(axis=1))[0]
    bottom = int(rows.max())
    y_face = 16 + int(thick.max()) // 2
    for j in range(int(rows.min()), bottom + 1):
        z = z_floor + (bottom - j)
        for i in range(V):
            t = int(thick[j, i])
            if t <= 0 or z >= c.shape[2]:
                continue
            col = tuple(int(v) for v in img[j, i])
            c.box(i, y_face - t, z, i + 1, y_face - 1, z + 1, shade(col, 0.8))
            c.box(i, y_face - 1, z, i + 1, y_face, z + 1, col)


def build_statue(name, rel, what, p):
    dv.record_use(rel, name)
    stem = os.path.basename(rel)
    c, pal = new_canvas(38)
    cl = clusters(rel, (0, 24, 32, 32), 3, minlum=40)
    if stem in ('zot_fire_orb_statue', 'zot_winter_orb_statue', 'zot_entropy_orb_statue'):
        zt = plinth(c, [shade(cl[0], 1.0), cl[1], cl[-1]], tiers=((26, 22, 3), (22, 18, 3)))
        if 'fire' in stem:                                   # 火の宝珠: 赤い球に橙の炎
            c.ell(16, 16, zt + 9, 8, 8, 8, (200, 36, 28))
            c.ell(13, 19, zt + 12, 2.5, 2, 2.5, (250, 170, 60))
            for a in np.linspace(0, 2 * np.pi, 7, endpoint=False):
                c.cyl(16 + 7 * np.cos(a), 16 + 7 * np.sin(a), zt + 9 + 3 * np.sin(a * 2), zt + 15 + 3 * np.sin(a * 2), 2.2, 0.3, (255, 150, 30), mat='clean')
        elif 'winter' in stem:                               # 冬の宝珠: 青白い氷の棘
            for a in np.linspace(0, 2 * np.pi, 8, endpoint=False):
                for (rr, hh) in ((6, 9), (3, 15)):
                    c.line((16, 16, zt + 7), (16 + rr * np.cos(a), 16 + rr * np.sin(a) * 0.8, zt + hh), 1.4, (150, 210, 250), mat='clean')
            octa(c, 16, 16, zt + 9, 6, (200, 235, 255), mat='clean')
            octa(c, 15, 18, zt + 10, 2.5, (250, 255, 255), mat='clean')
        else:                                                # エントロピーの宝珠: 金の受けに載る暗い青緑の球
            c.ell(16, 16, zt + 10, 7.5, 7.5, 7.5, (20, 96, 104))
            c.ell(13, 19, zt + 13, 2.5, 2, 2.5, (110, 190, 190))
            c.ring(16, 16, zt + 4, 8, 1.1, (232, 190, 60), axis='z')
            for a in (0.4, 2.5, 4.6):
                c.line((16 + 8 * np.cos(a), 16 + 8 * np.sin(a), zt + 3), (16 + 6 * np.cos(a), 16 + 6 * np.sin(a), zt + 12), 1.0, (232, 190, 60))
        return finish(name, c, pal, 'DCSS の像 %s（%s）。台と宝珠を一から作った（tools/dcss/gen_dcss_objects.py）' % (what, rel))
    if stem == 'metal_statue':                               # 金属の像: 三角形の枠（不可能図形風）
        zt = plinth(c, [shade(cl[0], 1.0), cl[1], cl[-1]], tiers=((26, 20, 3), (22, 16, 2)))
        mt = (150, 160, 176)
        pts = [(6, 16, zt + 1), (26, 16, zt + 1), (16, 16, zt + 26)]
        for k in range(3):
            c.line(pts[k], pts[(k + 1) % 3], 2.4, shade(mt, 1.0 - 0.12 * k))
        for k in range(3):
            c.line((pts[k][0], 18, pts[k][2]), (pts[(k + 1) % 3][0], 18, pts[(k + 1) % 3][2]), 1.0, (205, 214, 228))
        return finish(name, c, pal, 'DCSS の像 %s（%s）。台と三角の金属枠を一から作った（tools/dcss/gen_dcss_objects.py）' % (what, rel))
    zt = plinth(c, [shade(cl[0], 1.0), cl[1], cl[-1]], tiers=((26, 20, 3), (22, 16, 2)))
    relief_figure(c, rel, (0, 2, 32, 25), zt)
    return finish(name, c, pal, 'DCSS の像 %s（%s）。台を一から作り、人形は絵の輪郭を丸み付きの浮き彫りにして台に載せた（tools/dcss/gen_dcss_objects.py）' % (what, rel))


# =================================================================== 店（露店）
def build_shop(name, rel, what, p):
    dv.record_use(rel, name)
    a = read_rgba(rel)
    c, pal = new_canvas(36)
    # 看板の地の色（絵の字の板の色。暗い縁取りを除いて最も多い色）
    sg_cols = [col for col in tile_colours(rel, (0, 0, 26, 14), n=6) if lum(np.array(col, float)) > 80]
    sign_bg = sg_cols[0] if sg_cols else (200, 180, 90)
    wd = (128, 86, 50)
    c.box(3, 12, 0, 29, 24, Z0 + 8, wd)
    c.box(3, 12, Z0 + 8, 29, 24, Z0 + 9.5, (168, 120, 70))
    for x in (3, 27):
        c.box(x, 12, Z0 + 9, x + 2, 14, Z0 + 27, shade(wd, 0.8))
    for k in range(10):                                      # 縞の日除け（後ろが高く前へ下がる）
        y0 = 12 + k * 1.3
        col = sign_bg if (k % 2 == 0) else (236, 232, 222)
        c.box(2, y0, Z0 + 27 - k * 0.5, 30, y0 + 1.4, Z0 + 28.5 - k * 0.5, col)
    # 看板: 絵の字の板を 3/4 に縮めて、台の正面に掛ける
    sg = Image.fromarray(a[0:14, 0:26], 'RGBA').resize((20, 10), Image.NEAREST)
    sa = np.asarray(sg)
    for j in range(10):
        for i in range(20):
            if sa[j, i, 3] >= 128:
                c.box(6 + i, 24, Z0 + 8 - 1 - j * 0.8, 7 + i, 25.5, Z0 + 8 - j * 0.8, tuple(int(v) for v in sa[j, i, :3]))
    # 売り物: 絵の右下の物を台の上に立てた板にする
    it = a[12:32, 14:32]
    for j in range(it.shape[0]):
        for i in range(it.shape[1]):
            if it[j, i, 3] >= 128:
                col = tuple(int(v) for v in it[j, i, :3])
                zz = Z0 + 9.5 + (it.shape[0] - 1 - j) * 0.8
                c.box(8 + i * 0.9, 16, zz, 9 + i * 0.9, 20, zz + 0.9, col)
    return finish(name, c, pal, 'DCSS の店 %s（%s）。露店（台・柱・縞の日除け・看板・売り物。看板と売り物の色は絵から。tools/dcss/gen_dcss_objects.py）' % (what, rel))


BUILDERS = {
    'altar': build_altar, 'fountain': build_fountain, 'dais': build_dais, 'grate': build_grate,
    'cache': build_cache, 'tree': build_tree, 'statue': build_statue, 'shop': build_shop,
}
