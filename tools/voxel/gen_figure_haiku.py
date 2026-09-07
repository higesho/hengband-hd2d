# -*- coding: utf-8 -*-
"""ハイク用フィギュア 3 体：馬・ドワーフ・メリー。

元絵の型を参考にしながら、gen_figure_probe.py のパターンで実装。
土台は make_base() を必ず使う。脚・手は limb() で 3 次元に引く。
正面図と奥行き表で輪郭を作る。ポーズは 1 体 1 つで固定。

罠 12 件に気をつけること:
  1. 肩当ての天面を広くすると「机」に見える
  2. 髪を前へ出すと顔が窪みで影で潰れる
  3. 頭を胴と同じ幅にすると「1 つの箱」に見える
  4. 天面を x だけ内側へ寄せて y を寄せ忘れると角が突起になる
  5. 腕を立方体で置くと大きな階段に見える
  6. 刀身は x を 1 でも振ると積み木の棒に見える
  7. 拳だけでは茶色の塊。得物を持たせて初めて輪郭が立つ
  8. 土台を黒にするとダンジョンで影と見分けが付かない
  （以下省略）

色は量子化して抜いた値を基にしている。本ファイル冒頭で reg() で登録。
"""
import importlib.util
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

# probe.py を import する
spec = importlib.util.spec_from_file_location('probe', os.path.join(HERE, 'gen_figure_probe.py'))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)

V = 32  # 1 マス = 32 ボクセル

def reg(name, rgb):
    """色を probe.PAL と probe.C に登録する。"""
    probe.PAL[len(probe.PAL) + 1] = rgb
    probe.C[name] = len(probe.PAL)

# ===== 色登録 =====
# 共通土台（probe.py から引き継ぎ）
reg('base_side', (44, 42, 48))
reg('base_top', (86, 84, 92))
reg('base_top_d', (68, 66, 74))
reg('base_rim', (124, 122, 130))

# ---- 馬（R955）。茶色・濃い茶・薄茶・眼・蹄
reg('horse_body', (139, 90, 50))         # 胴体（濃い茶）
reg('horse_body_l', (178, 116, 68))      # 胴体（薄茶。面の向きで分ける）
reg('horse_mane', (89, 50, 28))          # たてがみ・尾（濃い茶）
reg('horse_face', (168, 110, 68))        # 顔（薄茶）
reg('horse_eye', (46, 132, 146))         # 眼
reg('horse_hoof', (44, 30, 20))          # 蹄

# ---- ドワーフ（P5_0_0）。赤い兜・板金鎧・大顎髭・赤い外套・斧
reg('dwarf_skin', (233, 173, 122))       # 肌
reg('dwarf_skin_d', (196, 136, 92))      # 肌暗
reg('dwarf_face', (233, 173, 122))       # 顔（肌と同じ）
reg('dwarf_beard', (128, 72, 40))        # 大顎髭（赤茶）
reg('dwarf_helmet', (180, 50, 50))       # 兜（赤）
reg('dwarf_helmet_l', (220, 80, 80))     # 兜（明るい赤）
reg('dwarf_helmet_d', (120, 35, 35))     # 兜暗
reg('dwarf_crest', (220, 220, 220))      # 前立て（白）
reg('dwarf_armor', (141, 148, 160))      # 板金鎧（鋼色）
reg('dwarf_armor_d', (84, 92, 104))      # 鎧暗
reg('dwarf_cape', (150, 40, 40))         # 外套（赤）
reg('dwarf_cape_d', (100, 25, 25))       # 外套暗
reg('dwarf_axe', (84, 52, 30))           # 斧柄（茶）
reg('dwarf_axe_head', (160, 160, 160))   # 斧頭（鋼色）
reg('dwarf_trouser', (31, 48, 62))       # ズボン
reg('dwarf_boot', (45, 21, 24))          # 靴

# ---- メリー（R1385）。黒髪・青い目・白い肌・赤いワンピース
reg('mary_hair', (20, 12, 14))           # 黒髪
reg('mary_hair_front', (35, 20, 24))     # 前髪（わずか明るく）
reg('mary_face', (240, 200, 160))        # 顔（白系）
reg('mary_eye', (50, 100, 160))          # 眼（青）
reg('mary_dress', (180, 30, 40))         # ワンピース（赤）
reg('mary_dress_d', (120, 20, 30))       # ワンピース暗
reg('mary_leg', (220, 190, 150))         # 脚（肌色）

# ===== 馬（R955）=====
# 四つ足で立っている。正面から見ると体が細長い。
FRONT_HORSE = [
    '....MM....',  # 18 首
    '...MMMM...',  # 17 胴の前
    '..MMMMMM..',  # 16
    '..MMMMMM..',  # 15
    '.MMMMMMMM.',  # 14
    '.MMMMMMMM.',  # 13 胴の後
    '..MMMMMM..',  # 12 腰
    '...MMMM...',  # 11
]

DEPTH_HORSE = {
    'M': ('horse_body', 12, 20),
}

def build_horse():
    """馬。四つ足で立つ。細い脚。"""
    vols = probe.zeros('body', 'head', 'leg_fl', 'leg_fr', 'leg_bl', 'leg_br')

    # 胴体の正面図
    probe.raise_front(FRONT_HORSE, DEPTH_HORSE, 10, 18,
                      lambda x, z, ch: 'body', vols)

    b = vols['body']

    # 胴の面の意匠（暗い面・明るい面）
    b[9:13, 12:16, 12:18] = probe.C['horse_body_l']  # 側面（明るく）
    b[11:12, 12:20, 14:18] = probe.C['horse_body_l']  # 背中の上面

    # 頭（正面図の延長）
    h = vols['head']
    h[9:13, 12:20, 18:24] = probe.C['horse_body']    # 後頭
    h[10:12, 19:20, 18:24] = probe.C['horse_face']   # 顔（前面）
    h[10:12, 19:20, 21:22] = probe.C['horse_eye']    # 眼
    h[9:10, 19:20, 20:22] = probe.C['horse_mane']    # たてがみ左
    h[12:13, 19:20, 20:22] = probe.C['horse_mane']   # たてがみ右

    # 前脚左（立っている）
    leg_fl = vols['leg_fl']
    probe.limb(leg_fl, (10, 16, 12), (10, 19, 5), 2, probe.C['horse_body'])
    leg_fl[9:11, 18:21, 3:6] = probe.C['horse_hoof']

    # 前脚右
    leg_fr = vols['leg_fr']
    probe.limb(leg_fr, (12, 16, 12), (12, 19, 5), 2, probe.C['horse_body'])
    leg_fr[11:13, 18:21, 3:6] = probe.C['horse_hoof']

    # 後脚左
    leg_bl = vols['leg_bl']
    probe.limb(leg_bl, (10, 14, 11), (10, 21, 5), 2, probe.C['horse_body'])
    leg_bl[9:11, 20:23, 3:6] = probe.C['horse_hoof']

    # 後脚右
    leg_br = vols['leg_br']
    probe.limb(leg_br, (12, 14, 11), (12, 21, 5), 2, probe.C['horse_body'])
    leg_br[11:13, 20:23, 3:6] = probe.C['horse_hoof']

    # 尾（背中から後ろへ）
    tail = vols['body']
    probe.limb(tail, (11, 13, 13), (11, 8, 10), 2, probe.C['horse_mane'])

    return [probe.crop('base', probe.make_base()),
            probe.crop('head', h), probe.crop('body', b),
            probe.crop('leg_fl', leg_fl), probe.crop('leg_fr', leg_fr),
            probe.crop('leg_bl', leg_bl), probe.crop('leg_br', leg_br)]

META_HORSE = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'body', 'voxels': 'body', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'body', 'grounded': False,
     'pivot': [11.0, 16.0, 19.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_fl', 'voxels': 'leg_fl', 'parent': 'body', 'grounded': False,
     'pivot': [10.0, 16.0, 12.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_fr', 'voxels': 'leg_fr', 'parent': 'body', 'grounded': False,
     'pivot': [12.0, 16.0, 12.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_bl', 'voxels': 'leg_bl', 'parent': 'body', 'grounded': False,
     'pivot': [10.0, 14.0, 11.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_br', 'voxels': 'leg_br', 'parent': 'body', 'grounded': False,
     'pivot': [12.0, 14.0, 11.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
]

# ===== ドワーフ（P5_0_0）=====
# ずんぐりした体。赤い兜・白い前立て・大きな顎髭・板金鎧・赤い外套・背中に斧
FRONT_DWARF = [
    '.....HH.....',  # 27 兜（赤）
    '....HHHH....',  # 26
    '....HCCCH....',  # 25 前立て（白い三角）
    '....HFFFFH....',  # 24 顔
    '....HFFFFH....',  # 23
    '...BBBBBBBB...',  # 22 顎髭（大きく横へ）
    '...BBBBBBBB...',  # 21
    '..AAAAAAAAAA..',  # 20 肩当て
    '..AAAAAAAAAA..',  # 19
    '..AAAAAAAAAA..',  # 18 胸
    '...AAAAAAA...',  # 17
    '...AAAAAAA...',  # 16
    '...TTTTTT...',  # 15 腰（帯）
    '...TTTTTT...',  # 14
    '...RRRRRR...',  # 13 ズボン（赤い外套）
    '..RRRRRRRR..',  # 12
    '..SSSS.SSSS..',  # 11 脚が分かれる
    '..SSSS.SSSS..',  # 10
    '..SSSS.SSSS..',  # 9 靴
]

DEPTH_DWARF = {
    'H': ('dwarf_helmet', 12, 20),
    'C': ('dwarf_crest', 12, 20),
    'F': ('dwarf_face', 12, 20),
    'B': ('dwarf_beard', 11, 20),
    'A': ('dwarf_armor', 12, 20),
    'T': ('dwarf_armor_d', 12, 20),
    'R': ('dwarf_cape', 12, 20),
    'S': ('dwarf_boot', 12, 20),
}

def build_dwarf():
    """ドワーフ戦士。ずんぐりした体。赤い兜・顎髭・板金鎧・外套・背中に斧。"""
    vols = probe.zeros('body', 'arm_l', 'arm_r', 'leg_l', 'leg_r', 'axe')

    # 胴体と頭の正面図
    probe.raise_front(FRONT_DWARF, DEPTH_DWARF, 8, 27,
                      lambda x, z, ch: 'body', vols)

    b = vols['body']

    # 兜の天（わずか）と後ろ
    b[10:14, 12:15, 25:27] = probe.C['dwarf_helmet']
    b[10:14, 11:12, 24:27] = probe.C['dwarf_helmet_d']  # 後ろ暗く

    # 板金鎧の面の意匠
    b[8:16, 11:12, 18:24] = probe.C['dwarf_armor_d']  # 背中
    b[8:16, 20:21, 18:24] = probe.C['dwarf_armor']    # 前

    # 腰の装飾
    b[10:14, 12:20, 14:16] = probe.C['dwarf_armor_d']

    # 手。ずんぐりとした拳。
    arm_l = vols['arm_l']
    probe.limb(arm_l, (8, 17, 20), (6, 20, 18), 3, probe.C['dwarf_armor'])
    arm_l[4:8, 19:22, 16:19] = probe.C['dwarf_armor_d']  # 拳

    arm_r = vols['arm_r']
    probe.limb(arm_r, (16, 17, 20), (18, 20, 18), 3, probe.C['dwarf_armor'])
    arm_r[16:20, 19:22, 16:19] = probe.C['dwarf_armor_d']

    # 脚
    leg_l = vols['leg_l']
    probe.limb(leg_l, (10, 16, 11), (10, 19, 5), 2, probe.C['dwarf_trouser'])
    leg_l[9:11, 18:21, 3:6] = probe.C['dwarf_boot']

    leg_r = vols['leg_r']
    probe.limb(leg_r, (14, 16, 11), (14, 19, 5), 2, probe.C['dwarf_trouser'])
    leg_r[13:15, 18:21, 3:6] = probe.C['dwarf_boot']

    # 背中の斧（両刃。柄と頭）
    axe = vols['axe']
    probe.limb(axe, (12, 11, 22), (12, 8, 28), 2, probe.C['dwarf_axe'])  # 柄
    # 斧頭（柄の上に）
    axe[10:14, 7:10, 26:30] = probe.C['dwarf_axe_head']

    # 外套（肩から後ろへ翻る）
    for k in range(8):
        y, z = 12 - k // 2, 20 - k
        cape_w = 3 + k // 3
        b[12 - cape_w:12 + cape_w, y:y + 1, z:z + 1] = probe.C['dwarf_cape']

    return [probe.crop('base', probe.make_base()),
            probe.crop('body', b),
            probe.crop('arm_l', arm_l), probe.crop('arm_r', arm_r),
            probe.crop('leg_l', leg_l), probe.crop('leg_r', leg_r),
            probe.crop('axe', axe)]

META_DWARF = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'body', 'voxels': 'body', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_l', 'voxels': 'arm_l', 'parent': 'body', 'grounded': False,
     'pivot': [8.0, 17.0, 20.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_r', 'voxels': 'arm_r', 'parent': 'body', 'grounded': False,
     'pivot': [16.0, 17.0, 20.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_l', 'voxels': 'leg_l', 'parent': 'body', 'grounded': False,
     'pivot': [10.0, 16.0, 11.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'body', 'grounded': False,
     'pivot': [14.0, 16.0, 11.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'axe', 'voxels': 'axe', 'parent': 'body', 'grounded': False,
     'pivot': [12.0, 11.0, 22.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
]

# ===== メリー（R1385）=====
# 小ぶりな人形。長い黒髪・前髪パッツン・大きな青い目・赤いワンピース
FRONT_MARY = [
    '.HHHHHHHH.',  # 19 髪の天
    'HHHHHHHHHH',  # 18
    'HH.HH.HHH.',  # 17 前髪パッツン
    'HHHFFFFFHH',  # 16 顔
    'HHHFEFFHH',  # 15 目（大きく）
    'HHHFFFFHH',  # 14 顎
    '.HHFFFFH.',  # 13 首
    '.RRRRRR.',  # 12 ワンピ（赤）
    '.RRRRRR.',  # 11
    '.RRRRRR.',  # 10
    '..RRRR..',  # 9
    '..LLLL..',  # 8 脚（肌色）
    '..LLLL..',  # 7
]

DEPTH_MARY = {
    'H': ('mary_hair', 12, 20),
    'F': ('mary_face', 12, 20),
    'E': ('mary_eye', 12, 20),
    'R': ('mary_dress', 12, 20),
    'L': ('mary_leg', 12, 20),
}

def build_mary():
    """メリー。日本人形風。長い黒髪・前髪パッツン・青い目・赤いワンピース。小ぶり。"""
    vols = probe.zeros('head', 'body', 'leg_l', 'leg_r')

    def kind(x, z, ch):
        if z >= 13:
            return 'head'
        if z < 8:
            return 'leg_l' if x < 16 else 'leg_r'
        return 'body'

    probe.raise_front(FRONT_MARY, DEPTH_MARY, 9, 19, kind, vols)
    h, b = vols['head'], vols['body']

    # 後ろ髪
    h[8:14, 11:18, 14:19] = probe.C['mary_hair']
    h[9:13, 11:14, 17:19] = probe.C['mary_hair']  # 後頭

    # 髪の天（わずか）
    h[10:12, 11:15, 18:19] = probe.C['mary_hair']
    h[10:12, 17:19, 16:18] = probe.C['mary_hair_front']  # 前髪の根

    # 顔の陰影（面の中）
    h[11:12, 19:20, 14:15] = probe.C['mary_face']  # 光が当たる面
    h[11:12, 19:20, 13:14] = probe.C['dwarf_skin_d']  # 顎の影

    # ワンピースの天（わずか）
    b[10:12, 12:18, 12:13] = probe.C['mary_dress_d']

    # 脚
    leg_l = vols['leg_l']
    probe.limb(leg_l, (11, 16, 8), (11, 19, 3), 2, probe.C['mary_leg'])

    leg_r = vols['leg_r']
    probe.limb(leg_r, (13, 16, 8), (13, 19, 3), 2, probe.C['mary_leg'])

    return [probe.crop('base', probe.make_base()),
            probe.crop('head', h), probe.crop('body', b),
            probe.crop('leg_l', leg_l), probe.crop('leg_r', leg_r)]

META_MARY = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'body', 'voxels': 'body', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'body', 'grounded': False,
     'pivot': [11.0, 16.0, 16.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_l', 'voxels': 'leg_l', 'parent': 'body', 'grounded': False,
     'pivot': [11.0, 16.0, 8.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'body', 'grounded': False,
     'pivot': [13.0, 16.0, 8.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
]

# ===== 本体 =====
FIGURES = (
    ('figure_horse', build_horse, META_HORSE, 'やせこけた馬（R955）。四つ足'),
    ('figure_dwarf', build_dwarf, META_DWARF, 'ドワーフ戦士（P5_0_0）。赤い兜・顎髭・背中に斧'),
    ('figure_mary', build_mary, META_MARY, 'メリー（R1385）。日本人形風・長い黒髪・赤いワンピース'),
)

def main():
    # probe.ex にアクセスして write_vox/write_prefab を呼ぶ
    out_dir = os.path.join(ROOT, 'assets', 'voxel')
    for name, builder, meta, note in FIGURES:
        parts = [p.tuple() for p in builder()]
        got = {p[0] for p in parts}
        want = {m['voxels'] for m in meta}
        if got != want:
            print('%s: パーツの顔ぶれが食い違っています %s' % (name, got ^ want))
            return 1
        size = probe.ex.write_vox(os.path.join(out_dir, name + '.vox'), parts, probe.PAL)
        fp = probe.ex.write_prefab(os.path.join(out_dir, name + '.jsonc'), name, parts, meta,
                                   note=note)
        total = sum(int((v > 0).sum()) for _, v, _ in parts)
        top = max(o[2] + v.shape[2] for _, v, o in parts)
        print('%-18s パーツ %2d / ボクセル %5d / 背丈 %2d / vox %6d B / footprint %s'
              % (name, len(parts), total, top, size, fp))
    return 0

if __name__ == '__main__':
    sys.exit(main())
