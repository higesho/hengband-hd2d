# -*- coding: utf-8 -*-
"""3 体のボクセル駒（試作）。

1. 馬（R955）- やせこけた馬。四つ足・茶色
2. ドワーフ（P5_0_0）- 戦士。板金鎧・兜・顎髭・斧・外套
3. メリー（R1385）- 日本人形。パッツン・青い目・赤いワンピース
"""
import importlib.util
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

spec = importlib.util.spec_from_file_location('probe', os.path.join(HERE, 'gen_figure_probe.py'))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)

# Reuse from probe
ex = probe.ex
Part = probe.Part
crop = probe.crop
limb = probe.limb
make_base = probe.make_base
raise_front = probe.raise_front
zeros = probe.zeros
paint = probe.paint
BIG = probe.BIG

V = 32  # 1 マス = 32 ボクセル

# Use probe.PAL and probe.C directly
PAL = probe.PAL
C = probe.C

def reg(name, rgb):
    PAL[len(PAL) + 1] = rgb
    C[name] = len(PAL)

# ================== 色の定義 ==================

# ---- 土台（共通）（既存）
# 土台は gen_figure_probe.py で既に定義済み

# ---- 馬（R955）。茶色・たてがみと尾は濃い茶
reg('horse_body', (156, 108, 68))        # 標準の茶
reg('horse_body_l', (192, 140, 88))      # 明るい茶（日向）
reg('horse_body_d', (108, 68, 40))       # 暗い茶（影）
reg('horse_mane', (68, 38, 20))          # たてがみ（濃い茶）
reg('horse_hoof', (50, 40, 30))          # 蹄
reg('horse_eye', (60, 40, 20))           # 目（黒っぽい茶）
reg('horse_nose', (120, 80, 50))         # 鼻

# ---- ドワーフ（P5_0_0）。赤茶の顎髭・板金・赤い兜・赤い外套
reg('dwarf_skin', (210, 160, 110))       # 肌（やや暗い）
reg('dwarf_beard', (120, 70, 40))        # 顎髭（赤茶）
reg('dwarf_beard_d', (80, 45, 25))       # 顎髭（暗い）
reg('dwarf_hair', (60, 35, 20))          # 髪（黒）
reg('dwarf_helmet', (200, 60, 40))       # 前立て（赤）
reg('dwarf_helmet_rim', (100, 50, 30))   # 兜の巻き上げ
reg('dwarf_steel', (150, 155, 170))      # 板金鎧（鋼）
reg('dwarf_steel_d', (100, 105, 120))    # 板金（暗い）
reg('dwarf_steel_l', (190, 195, 210))    # 板金（明るい・光の面）
reg('dwarf_cape', (180, 60, 50))         # 外套（赤）
reg('dwarf_cape_d', (120, 40, 30))       # 外套（暗い）
reg('dwarf_axe_head', (140, 145, 160))   # 斧の刃（鋼）
reg('dwarf_axe_shaft', (100, 70, 40))    # 斧の柄（木）
reg('dwarf_boot', (50, 40, 30))          # ブーツ

# ---- メリー（R1385）。黒い長髪・パッツン・青い目・赤いワンピース
reg('mary_skin', (240, 190, 150))        # 肌（白い）
reg('mary_hair', (30, 20, 15))           # 髪（黒い）
reg('mary_eye', (80, 150, 200))          # 目（青）
reg('mary_eye_white', (250, 250, 250))   # 白目
reg('mary_dress', (220, 60, 60))         # ワンピース（赤）
reg('mary_dress_l', (240, 100, 100))     # ワンピース（明るい）

# ================== 1. 馬（R955）==================
# 四つ足・全体的に茶色・たてがみと尾は濃い茶・蹄は暗い

FRONT_HORSE = [
    '..mmmm..',     # 22 たてがみ（天）
    '..mmmm..',     # 21
    '..HHHH..',     # 20 頭
    '..HHHH..',     # 19
    '..EEEE..',     # 18 目・耳
    '.HHHHHH.',     # 17 頸
    '.BBBBBB.',     # 16 肩
    'BBBBBBBB',     # 15 胸
    'BBBBBBBB',     # 14
    'BBBBBBBB',     # 13 腹
    '.BBBBBB.',     # 12
    '.BBBBBB.',     # 11
    '..BBBB..',     # 10 腰
    '..BBBB..',     # 9
    '.TT..TT.',     # 8 脚が分かれる（前後）
    '.TT..TT.',     # 7
    '.TT..TT.',     # 6
    '.TT..TT.',     # 5
    '.TT..TT.',     # 4
    '.TT..TT.',     # 3
    '.PP..PP.',     # 2 蹄
    '.PP..PP.',     # 1
]

DEPTH_HORSE = {
    'm': ('horse_mane', 16, 23),
    'H': ('horse_body', 15, 23), 'E': ('horse_eye', 15, 23),
    'B': ('horse_body', 14, 23),
    'T': ('horse_body', 13, 22),
    'P': ('horse_hoof', 14, 22),
}

def build_horse():
    def kind(x, z, ch):
        if z >= 16:
            return 'head'
        if z < 9:
            return 'leg_l' if x <= 8 else 'leg_r'
        return 'body'

    vols = zeros('head', 'body', 'leg_l', 'leg_r')
    raise_front(FRONT_HORSE, DEPTH_HORSE, 5, 22, kind, vols)
    h, b = vols['head'], vols['body']

    # 頭の側面・背面
    h[4:11, 14:17, 18:24] = C['horse_body']  # 後頭
    h[5:10, 16:17, 19:23] = C['horse_body_l']  # 天（光の面）
    h[7:8, 16:17, 17:19] = C['horse_eye']  # 目の周り

    # 胴の天面と側面（もやもやした陰影は付けない・形だけを読ませる）
    b[5:10, 16:17, 13:16] = C['horse_body_l']  # 背の天
    b[4:11, 14:15, 10:15] = C['horse_body_d']  # 腹（影）

    # たてがみ（頸から後ろへ）
    for i in range(5):
        mane = np.zeros(BIG, np.uint8)
        z = 22 - i
        mane[4:11, 15:16, z:z+1] = C['horse_mane']
        mane[4:11, 16:17, z:z+1] = C['horse_mane']

    # 尾（短く。後ろの脚の間）
    tail = np.zeros(BIG, np.uint8)
    limb(tail, (7, 13, 12), (7, 11, 8), 2, C['horse_mane'])

    return [crop('base', make_base()), crop('body', b), crop('head', h),
            crop('leg_l_f', vols['leg_l']), crop('leg_r_f', vols['leg_r']),
            crop('tail', tail)]

META_HORSE = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'body', 'voxels': 'body', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'body', 'grounded': False,
     'pivot': [7.5, 16.0, 20.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_l_f', 'voxels': 'leg_l_f', 'parent': 'body', 'grounded': False,
     'pivot': [6.5, 16.0, 8.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r_f', 'voxels': 'leg_r_f', 'parent': 'body', 'grounded': False,
     'pivot': [10.5, 16.0, 8.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'tail', 'voxels': 'tail', 'parent': 'body', 'grounded': False,
     'motion': {'kind': 'static'}, 'wind_k': 0.0},
]

# ================== 2. ドワーフ（P5_0_0）==================
# 板金鎧・赤い兜・大きな赤茶の顎髭・背中に双刃の斧・赤い外套・ずんぐり

FRONT_DWARF = [
    '..hhhh..',      # 22 兜の前立て
    '..hhhh..',      # 21
    '....hh..',      # 20 兜の巻き上げ
    '..hhhh..',      # 19
    '.FFFFFF.',      # 18 顔（眉間から下）
    '.FFFFFF.',      # 17
    '.FFBBFF.',      # 16 顎髭（中央は髪）
    '.BBBBBB.',      # 15 顎髭
    '.BBBBBB.',      # 14 顎髭（もっさり）
    'SSSSSSSS',      # 13 肩当て天
    'SSSSSSSS',      # 12 肩当て
    'SSBBBSBS',      # 11 腕甲と胸
    'SSBBBSBS',      # 10
    '.SSSSSS.',      # 9 脇
    '.CCCCCC.',      # 8 外套（肩から落ちる）
    '.CCCCCC.',      # 7
    '..SSSS..',      # 6 腹（鎧）
    '..SSSS..',      # 5
    '.TTTTTT.',      # 4 脚
    '.TTTTTT.',      # 3
    '.TTTTTT.',      # 2
]

DEPTH_DWARF = {
    'h': ('dwarf_helmet', 15, 22),
    'F': ('dwarf_skin', 14, 22), 'B': ('dwarf_beard', 14, 22),
    'S': ('dwarf_steel', 13, 22),
    'C': ('dwarf_cape', 13, 22),
    'T': ('dwarf_boot', 12, 22),
}

def build_dwarf():
    def kind(x, z, ch):
        if z >= 16:
            return 'head'
        return 'torso'

    vols = zeros('head', 'torso')
    raise_front(FRONT_DWARF, DEPTH_DWARF, 6, 22, kind, vols)
    head, torso = vols['head'], vols['torso']

    # 顔の立体感
    head[8:15, 16:17, 16:20] = C['dwarf_skin']  # 前面
    head[7:16, 17:18, 15:22] = C['dwarf_skin']  # 側面（頬）

    # 兜は背側に折り返す
    head[6:17, 15:17, 20:22] = C['dwarf_helmet_rim']  # 兜の巻き

    # 顎髭は塊感が命。ただし顔の下へ出さない（罠 50）
    head[7:16, 16:19, 14:17] = C['dwarf_beard']  # 塊
    head[8:15, 17:18, 14:15] = C['dwarf_beard_d']  # 底の影

    # 肩当ての厚み（y だけでなく上も刳る）
    torso[6:17, 14:18, 12:14] = C['dwarf_steel']  # 肩の下地
    torso[6:17, 17:18, 12:14] = C['dwarf_steel_l']  # 光の面

    # 胸の前面（意匠）
    torso[10:13, 18:19, 9:12] = C['dwarf_steel_l']  # 光の乗る面

    # 外套（肩から後ろへ流す。厚みは 1〜2）
    cape = np.zeros(BIG, np.uint8)
    for i in range(8):
        z = 8 - i
        cape[7 - i//3:16 + i//3, 14 - i//4:16, z:z+1] = C['dwarf_cape']
        cape[8 - i//3:15 + i//3, 14 - i//4:15, z:z+1] = C['dwarf_cape_d']  # 裏地

    # 腕。上腕は広く。前腕は肘で絞る
    arm_l = np.zeros(BIG, np.uint8)
    limb(arm_l, (7, 16, 11), (6, 19, 8), 3, C['dwarf_steel'])
    limb(arm_l, (6, 19, 8), (5, 21, 5), 2, C['dwarf_steel_d'])
    arm_l[4:7, 20:22, 4:7] = C['dwarf_skin']  # 拳

    arm_r = np.zeros(BIG, np.uint8)
    limb(arm_r, (16, 16, 11), (17, 19, 8), 3, C['dwarf_steel'])
    limb(arm_r, (17, 19, 8), (18, 21, 5), 2, C['dwarf_steel_d'])
    arm_r[17:20, 20:22, 4:7] = C['dwarf_skin']  # 拳

    # 斧（背中に背負わせる）。柄と刃を分ける
    axe = np.zeros(BIG, np.uint8)
    # 柄は背中の上部から肩の高さまで
    limb(axe, (11.5, 11, 12), (11.5, 10, 8), 2, C['dwarf_axe_shaft'])
    # 刃は柄の上に。x を振らない（罠 6）
    axe[10:13, 9:11, 15:18] = C['dwarf_axe_head']  # 前の刃
    axe[10:13, 9:11, 16:18] = C['dwarf_axe_head']  # 背の刃（2 枚）
    axe[9:14, 10:11, 16:17] = C['dwarf_axe_head']  # 刃の幅

    # 脚。ずんぐり。跨ぎ足にしない（立ち姿）
    leg_l = np.zeros(BIG, np.uint8)
    limb(leg_l, (9, 15, 5), (8, 18, 2), 3, C['dwarf_steel_d'])
    leg_l[6:10, 18:20, 1:3] = C['dwarf_boot']

    leg_r = np.zeros(BIG, np.uint8)
    limb(leg_r, (14, 15, 5), (15, 18, 2), 3, C['dwarf_steel_d'])
    leg_r[13:17, 18:20, 1:3] = C['dwarf_boot']

    return [crop('base', make_base()), crop('torso', torso), crop('head', head),
            crop('arm_l', arm_l), crop('arm_r', arm_r),
            crop('axe', axe), crop('cape', cape),
            crop('leg_l', leg_l), crop('leg_r', leg_r)]

META_DWARF = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'torso', 'voxels': 'torso', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'torso', 'grounded': False,
     'pivot': [11.5, 16.0, 18.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_l', 'voxels': 'arm_l', 'parent': 'torso', 'grounded': False,
     'pivot': [7.0, 16.0, 11.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_r', 'voxels': 'arm_r', 'parent': 'torso', 'grounded': False,
     'pivot': [16.0, 16.0, 11.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'axe', 'voxels': 'axe', 'parent': 'torso', 'grounded': False,
     'pivot': [11.5, 10.0, 12.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'cape', 'voxels': 'cape', 'parent': 'torso', 'grounded': False,
     'motion': {'kind': 'wind'}, 'wind_k': 0.3},
    {'name': 'leg_l', 'voxels': 'leg_l', 'parent': 'torso', 'grounded': False,
     'pivot': [9.0, 15.0, 5.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'torso', 'grounded': False,
     'pivot': [14.5, 15.0, 5.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
]

# ================== 3. メリー（R1385）==================
# 日本人形風。小さい。黒い長髪・パッツン・大きな青い目・赤いワンピース

FRONT_MARY = [
    '...hhhh...',    # 19 髪の天
    '...hhhh...',    # 18
    '...hhhh...',    # 17 前髪パッツン
    '..hhhhhh..',    # 16
    '.hhFFFFhh.',    # 15 顔
    '.hhEEEEhh.',    # 14 目
    '.hhFFFFhh.',    # 13
    '..hFFFFh..',    # 12 鼻下
    '...FFFF...',    # 11 顎
    '...AAAA...',    # 10 首
    '..AADDAA..',    # 9 肩と袖
    '..AADDAA..',    # 8
    '.AADDDDDA.',    # 7 胸
    '.AADDDDDA.',    # 6
    '.AADDDDDA.',    # 5 腰
    '..AADDAA..',    # 4
    '..AADDAA..',    # 3 裾
    '..AADDAA..',    # 2
    '.TTTTTTTT.',    # 1 脚
]

DEPTH_MARY = {
    'h': ('mary_hair', 13, 20), 'F': ('mary_skin', 13, 20),
    'E': ('mary_eye', 13, 20),
    'A': ('mary_dress', 12, 20), 'D': ('mary_dress_l', 12, 20),
    'T': ('mary_skin', 12, 20),
}

def build_mary():
    def kind(x, z, ch):
        if z >= 12:
            return 'head'
        return 'body'

    vols = zeros('head', 'body')
    raise_front(FRONT_MARY, DEPTH_MARY, 5, 19, kind, vols)
    head, body = vols['head'], vols['body']

    # 顔の立体感。パッツン前髪だから面を彫らない
    head[7:12, 16:17, 14:17] = C['mary_skin']  # 前面だけ
    head[6:13, 17:18, 12:19] = C['mary_hair']  # 側面は髪

    # 目をしっかり。青と白を分ける
    head[8:9, 16:17, 14:15] = C['mary_eye_white']  # 左目の白目
    head[8:9, 16:17, 14:14] = C['mary_eye']  # 瞳
    head[11:12, 16:17, 14:15] = C['mary_eye_white']  # 右目
    head[11:12, 16:17, 14:14] = C['mary_eye']

    # 髪を背側・脇へ張ったように
    head[6:13, 14:16, 15:19] = C['mary_hair']  # 後ろ髪
    head[5:6, 14:18, 13:18] = C['mary_hair']  # 左の髪
    head[12:13, 14:18, 13:18] = C['mary_hair']  # 右の髪

    # ワンピース。前が明るく（光）、側が暗め
    body[7:12, 16:17, 4:10] = C['mary_dress_l']  # 前面（光）
    body[6:13, 14:16, 3:10] = C['mary_dress']  # 側面

    # 袖。短い
    arm_l = np.zeros(BIG, np.uint8)
    limb(arm_l, (6, 16, 8), (4, 18, 6), 2, C['mary_dress'])
    arm_l[3:6, 18:20, 5:8] = C['mary_skin']  # 手

    arm_r = np.zeros(BIG, np.uint8)
    limb(arm_r, (13, 16, 8), (15, 18, 6), 2, C['mary_dress'])
    arm_r[14:17, 18:20, 5:8] = C['mary_skin']  # 手

    # 脚。短く。素足（肌色）
    leg_l = np.zeros(BIG, np.uint8)
    limb(leg_l, (7, 16, 3), (7, 18, 0), 2, C['mary_skin'])
    leg_l[6:9, 18:20, 0:2] = C['mary_skin']  # 足

    leg_r = np.zeros(BIG, np.uint8)
    limb(leg_r, (12, 16, 3), (12, 18, 0), 2, C['mary_skin'])
    leg_r[11:14, 18:20, 0:2] = C['mary_skin']  # 足

    return [crop('base', make_base()), crop('body', body), crop('head', head),
            crop('arm_l', arm_l), crop('arm_r', arm_r),
            crop('leg_l', leg_l), crop('leg_r', leg_r)]

META_MARY = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'body', 'voxels': 'body', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'body', 'grounded': False,
     'pivot': [9.5, 16.0, 15.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_l', 'voxels': 'arm_l', 'parent': 'body', 'grounded': False,
     'pivot': [6.0, 16.0, 8.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_r', 'voxels': 'arm_r', 'parent': 'body', 'grounded': False,
     'pivot': [13.0, 16.0, 8.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_l', 'voxels': 'leg_l', 'parent': 'body', 'grounded': False,
     'pivot': [7.0, 16.0, 3.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'body', 'grounded': False,
     'pivot': [12.0, 16.0, 3.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
]

# ================== 本体 ==================

FIGURES = (
    ('haiku2_horse', build_horse, META_HORSE, 'やせこけた馬（R955）。四つ足・茶色'),
    ('haiku2_dwarf', build_dwarf, META_DWARF, 'ドワーフ戦士（P5_0_0）。板金鎧・兜・顎髭・斧・外套'),
    ('haiku2_mary', build_mary, META_MARY, '忍び寄る人形『メリー』（R1385）。日本人形風・黒髪・赤いワンピース'),
)

def main():
    out_dir = os.path.join(ROOT, 'assets', 'voxel')
    for name, builder, meta, note in FIGURES:
        parts = [p.tuple() for p in builder()]
        got = {p[0] for p in parts}
        want = {m['voxels'] for m in meta}
        if got != want:
            print('%s: パーツの顔ぶれが食い違っています %s' % (name, got ^ want))
            return 1
        size = ex.write_vox(os.path.join(out_dir, name + '.vox'), parts, PAL)
        fp = ex.write_prefab(os.path.join(out_dir, name + '.jsonc'), name, parts, meta,
                             note='haiku2（新試作）。' + note)
        total = sum(int((v > 0).sum()) for _, v, _ in parts)
        top = max(o[2] + v.shape[2] for _, v, o in parts)
        print('%-18s パーツ %2d / ボクセル %5d / 背丈 %2d / vox %6d B / footprint %s'
              % (name, len(parts), total, top, size, fp))
    return 0

if __name__ == '__main__':
    sys.exit(main())
