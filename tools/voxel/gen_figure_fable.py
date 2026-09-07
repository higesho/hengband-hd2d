# -*- coding: utf-8 -*-
"""ボクセルの駒 3 体（fable 分担）。型は `gen_figure_probe.py`（1 行も触らない）。

定義の置き場:。土台・footprint・関節の決まりは probe の
docstring（罠 12 件を含む）に従う。

| プレハブ | 元絵 | 見た目 |
|---|---|---|
| `fable_horse` | `R955` | やせこけた馬。四つ足。茶色、たてがみと尾は濃い茶 |
| `fable_dwarf` | `P5_0_0` | ドワーフ戦士。板金鎧・赤い前立ての兜・赤茶の顎髭・背の両刃斧・赤い外套 |
| `fable_mary` | `R1385` | 忍び寄る人形『メリー』。日本人形風。黒髪パッツン・青い目・赤いワンピース |

## 型からの逸脱は 1 か所だけ（理由つき）
馬の胴は `FRONT_*` の正面図で書かない。正面図は「1 文字 = 1 ボクセル」を **y の一定幅へ
押し出す**表なので、奥行き 16 ボクセルの四つ足の胴（長さが y 方向）は表現できない。
胴・首はスライスと `limb()` で直接組む。人型 2 体は型どおり正面図で起こす。

## 実行
```
python tools\\voxel\\gen_figure_fable.py
.\\HengbandHd2d.exe --prefab-check=fable_horse     # パイプに通さないこと
```
"""
import importlib.util
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

spec = importlib.util.spec_from_file_location(
    'probe', os.path.join(HERE, 'gen_figure_probe.py'))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)

C = probe.C
BIG = probe.BIG
crop = probe.crop
limb = probe.limb
zeros = probe.zeros
paint = probe.paint


def reg(name, rgb):
    """probe のパレットの続きに色を足す（索引 0 は空なので len+1）。"""
    probe.PAL[len(probe.PAL) + 1] = rgb
    C[name] = len(probe.PAL)


# ---- やせこけた馬（R955）。茶の地に、たてがみ・尾は濃い茶。
reg('coat', (146, 88, 46))
reg('coat_l', (182, 116, 60))
reg('coat_d', (100, 60, 34))
reg('mane', (70, 40, 24))
reg('mane_d', (48, 27, 18))
reg('hoof', (54, 40, 34))
reg('blaze', (216, 196, 168))   # 額から鼻筋の白流星（元絵の読みの決め手）
reg('muzzle', (72, 50, 44))
# ---- ドワーフ戦士（P5_0_0）。鎧は probe の steel を使い回す。
reg('beard_r', (140, 64, 28))   # 赤茶の顎髭（明るくすると木の板に見える。実物で確認）
reg('beard_rd', (96, 42, 20))
reg('beard_rl', (180, 100, 48))
reg('crest', (188, 40, 42))     # 兜の赤い前立て
reg('eye_b', (72, 104, 188))    # 青い目（メリーと共用）
# ---- 忍び寄る人形『メリー』（R1385）。
reg('skin_w', (240, 228, 218))  # 白い顔
reg('hair_k', (24, 22, 26))     # 黒髪（真っ黒にしない。面の切れ目が読めなくなる）
reg('hair_k_l', (52, 50, 60))
reg('dress', (176, 32, 38))     # 赤いワンピース
reg('dress_d', (120, 20, 28))
reg('dress_l', (205, 64, 60))
reg('mouth', (150, 60, 62))
reg('cheek', (235, 170, 158))


# ==================================================================== やせこけた馬
# 四つ足なので胴の長軸は y。顔は y の大きい側（カメラ側）へ。
# 「やせこけた」は形で言う: 肩と腰の骨だけ幅を広げ、胴の中程を細く括り、
# 背骨の稜を天へ 1 段立て、脇腹にあばらの縞を彫る。
def build_horse():
    b = np.zeros(BIG, np.uint8)
    b[13:19, 7:23, 14:21] = C['coat']        # 胴の芯（幅 6。痩せて細い）
    b[12:20, 18:22, 15:20] = C['coat']       # 肩の骨（幅 8 に張る）
    b[12:20, 8:12, 15:20] = C['coat']        # 腰の骨（同）
    b[15:17, 9:21, 21:22] = C['coat_d']      # 背骨の稜（天へ 1 段。骨張って見せる）
    b[13:19, 12:18, 14:15] = 0               # 腹の括れ（中程の底を 1 段抜く。痩せの本体）
    b[13:14, 12:18, 15:21] = 0               # 脇腹を削ぐ（肩と腰の骨だけ幅を残すと
    b[18:19, 12:18, 15:21] = 0               #  「痩せこけ」が輪郭に出る。実物で確認）
    paint(b, np.s_[12:20, 7:23, 19:21], 'coat_l')    # 背は明るく（面の向きで陰影）
    paint(b, np.s_[12:20, 7:23, 14:16], 'coat_d')    # 下腹は暗く
    paint(b, np.s_[13:19, 21:23, 15:19], 'coat_l')   # 胸の前面（光の乗る面）
    for yy in range(12, 18, 2):                       # あばらの縞（削いだ脇腹の面に彫る）
        paint(b, np.s_[14:15, yy:yy + 1, 15:19], 'coat_d')
        paint(b, np.s_[17:18, yy:yy + 1, 15:19], 'coat_d')

    #! 首。うなだれ気味に前へ落とす（痩せ馬の姿勢）。たてがみは首の上後ろへ沿わせる。
    n = np.zeros(BIG, np.uint8)
    limb(n, (16, 21, 19), (16, 26, 24), 4, C['coat'])
    #! たてがみ。首の中へ埋めると 1 ボクセルも見えない（初版で確認）。
    #  首の天（z = y-1）の上へ 2 段の立て板として乗せる。y=24 から先は頭骨と
    #  重なって Z ファイトするので、その手前で止める。
    for y in range(20, 24):
        n[15:17, y - 1:y + 1, y:y + 2] = C['mane']
    n[15:17, 17:20, 20:22] = C['mane']                  # き甲の房
    #! たてがみの垂れ（元絵と同じ片流し）。首の右脇へ幕として垂らす。
    #  z は 22 で止める（上げると頭骨と重なる）。
    for y in range(23, 27):
        z1 = min(y - 1, 22)
        n[17:19, y:y + 1, z1 - 4:z1 + 1] = C['mane']

    #! 頭。目は側面（馬の目は横に付く）、額から鼻筋に白流星、鼻先は暗く。
    h = np.zeros(BIG, np.uint8)
    h[13:19, 24:29, 23:29] = C['coat']       # 頭骨
    h[14:18, 28:32, 23:26] = C['coat']       # 鼻面（+y へ突き出す）
    paint(h, np.s_[13:19, 24:32, 27:29], 'coat_l')      # 頭の天
    paint(h, np.s_[14:18, 31:32, 23:26], 'muzzle')      # 鼻先の面
    paint(h, np.s_[15:17, 28:29, 26:29], 'blaze')       # 額の流星（面の中の色替え）
    paint(h, np.s_[15:17, 28:31, 25:26], 'blaze')       # 鼻筋へ続ける
    h[14:15, 31:32, 24:25] = C['dark']       # 鼻孔
    h[17:18, 31:32, 24:25] = C['dark']
    h[13:14, 26:27, 27:28] = C['dark']       # 目（左の側面）
    h[18:19, 26:27, 27:28] = C['dark']       # （右）
    h[13:15, 25:27, 29:31] = C['coat']       # 耳（左）
    h[17:19, 25:27, 29:31] = C['coat']       # （右）
    h[15:17, 24:27, 29:30] = C['mane']       # 前髪（耳の間。天面に置き、顔は前へ出さない）

    #! 脚 4 本。細さ 2（痩せ馬）。胴の底 z=14 に届く z=13 から蹄まで真っ直ぐ。
    legs = {}
    for name, cx, cy in (('leg_fl', 13, 21), ('leg_fr', 18, 21),
                         ('leg_bl', 13, 9), ('leg_br', 18, 9)):
        v = np.zeros(BIG, np.uint8)
        limb(v, (cx, cy, 13), (cx, cy, 5), 2, C['coat_d'])
        v[cx - 1:cx + 2, cy - 1:cy + 2, 3:5] = C['hoof']   # 蹄（脚より 1 回り太く）
        legs[name] = v

    tail = np.zeros(BIG, np.uint8)           # 尾。太さ 3（2 では胴の陰に消える。実物で確認）
    limb(tail, (16, 7, 19), (16, 4, 9), 3, C['mane'])
    limb(tail, (16, 4, 9), (16, 4, 5), 2, C['mane_d'])

    return [crop('base', probe.make_base()), crop('body', b), crop('neck', n),
            crop('head', h), crop('leg_fl', legs['leg_fl']), crop('leg_fr', legs['leg_fr']),
            crop('leg_bl', legs['leg_bl']), crop('leg_br', legs['leg_br']), crop('tail', tail)]


META_HORSE = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'body', 'voxels': 'body', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # 首 → 頭の 2 関節。頭を下げる・振る動きを後から入れるため。
    {'name': 'neck', 'voxels': 'neck', 'parent': 'body', 'grounded': False,
     'pivot': [16.0, 21.0, 19.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'neck', 'grounded': False,
     'pivot': [16.0, 26.0, 24.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_fl', 'voxels': 'leg_fl', 'parent': 'body', 'grounded': False,
     'pivot': [13.0, 21.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_fr', 'voxels': 'leg_fr', 'parent': 'body', 'grounded': False,
     'pivot': [18.0, 21.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_bl', 'voxels': 'leg_bl', 'parent': 'body', 'grounded': False,
     'pivot': [13.0, 9.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_br', 'voxels': 'leg_br', 'parent': 'body', 'grounded': False,
     'pivot': [18.0, 9.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'tail', 'voxels': 'tail', 'parent': 'body', 'grounded': False,
     'pivot': [16.0, 7.0, 19.0], 'motion': {'kind': 'wind'}, 'wind_k': 0.25},
]


# ==================================================================== ドワーフ戦士
# ずんぐり: 幅 20 の胴に短い脚。髭 B は胴の前面より 1 ボクセル前（y=20）へ垂らすが、
# 顔そのものは兜と同じ 1 枚の面に彫る（罠 2: 顔を窪みの底にしない）。
FRONT_DWARF = [
    '.......MMMMMM.......',  # 28 兜の天
    '......MMMMMMMM......',  # 27
    '.....MMMMMMMMMM.....',  # 26
    '.....MMMMMMMMMM.....',  # 25
    '.....MMMMMMMMMM.....',  # 24 兜の庇
    '.....MFeFMMFeFM.....',  # 23 目と鼻当て（中央の MM）
    '.....MFFFMMFFFM.....',  # 22 頬当ての間
    '.....MFFFFFFFFM.....',  # 21 鼻の下
    '..ssMMBBBBBBBBMMss..',  # 20 襟。髭が胸へかかる
    '.SSMMMBBBBBBBBMMMSS.',  # 19 肩当て
    '.SSMMMBBBBBBBBMMMSS.',  # 18
    '..MMMMBBBBBBBBMMMM..',  # 17 胸甲
    '..MMMMMBBBBBBMMMMM..',  # 16 髭が細る（すぼまりが無いと木の板に見える）
    '..MMMMMBBBBBBMMMMM..',  # 15
    '..MMMMMMBBBBMMMMMM..',  # 14
    '..EEEEEEEBBEEEEEEE..',  # 13 帯（髭の先がかかる）
    '..EEEEEEEUUEEEEEEE..',  # 12 尾ロック
    '..MMMMMMMMMMMMMMMM..',  # 11 草摺
    '...MMMMMMMMMMMMMM...',  # 10
    '...LLLLLL..LLLLLL...',  # 9 脚（短い。ずんぐりの本体）
    '...LLLLLL..LLLLLL...',  # 8
    '...LLLLLL..LLLLLL...',  # 7
    '...OOOOOO..OOOOOO...',  # 6 長靴
    '...OOOOOO..OOOOOO...',  # 5
    '..OOOOOOO..OOOOOOO..',  # 4
    '..OOOOOOO..OOOOOOO..',  # 3
]

DEPTH_DWARF = {
    'M': ('steel', 13, 20), 's': ('steel_l', 15, 20), 'S': ('steel', 14, 21),
    'F': ('skin', 13, 20), 'e': ('eye_b', 13, 20),
    'B': ('beard_r', 14, 21),   # 髭は前面より 1 ボクセル前へ（胸に垂れる厚み）
    'E': ('leather_d', 12, 21), 'U': ('brass', 12, 21),
    'L': ('leather', 13, 20), 'O': ('boot', 13, 20),
}


def build_dwarf():
    def kind(x, z, ch):
        if ch == 'B':
            return 'beard'
        if z >= 21:
            return 'head'
        if z <= 9:
            return 'leg_l' if x < 16 else 'leg_r'
        return 'torso'

    vols = zeros('head', 'torso', 'beard', 'leg_l', 'leg_r')
    probe.raise_front(FRONT_DWARF, DEPTH_DWARF, 6, 28, kind, vols)
    h, t, bd = vols['head'], vols['torso'], vols['beard']

    paint(h, np.s_[11:21, 13:15, 21:25], 'steel')       # 後頭も兜（F の背面が肌で残る）
    paint(h, np.s_[14:18, 14:19, 28:29], 'steel_l')     # 兜の天（縁は残す。罠 4: x も y も）
    #! 赤い前立て。兜の天を y に走る鶏冠の立て板。初版の 2 段では「赤い箱」に
    #  しか見えなかったので、中程を 1 段高くして板の輪郭を出す。
    h[15:17, 12:20, 29:31] = C['crest']                 # 峰の全長
    h[15:17, 13:18, 31:32] = C['crest']                 # 中程を高く
    h[15:17, 10:13, 27:30] = C['crest']                 # 後ろへ流れ落ちる裾

    paint(t, np.s_[6:26, 13:14, 10:21], 'steel_d')      # 鎧の背面は暗く
    paint(t, np.s_[8:12, 19:20, 14:19], 'steel_l')      # 胸甲の前面（光の乗る面）
    paint(t, np.s_[20:24, 19:20, 14:19], 'steel_l')

    #! 髭の艶。暗い縞は「木の板の継ぎ目」に見えた（実物で確認）。地を暗い赤茶に
    #  落とし、明るい房を長さ不揃いに走らせると毛に読める。
    for x, z0, z1 in ((13, 14, 18), (15, 13, 19), (17, 15, 20), (19, 14, 18)):
        paint(bd, np.s_[x:x + 1, 20:21, z0:z1], 'beard_rl')
    paint(bd, np.s_[14:18, 20:21, 19:20], 'beard_rd')   # 鼻の下の影（口ひげの段）
    bd[10:12, 20:22, 8:16] = C['beard_rd']              # 編んだ房（左。帯より下まで）
    bd[20:22, 20:22, 8:16] = C['beard_rd']              # （右）
    bd[10:12, 20:22, 10:11] = C['brass']                # 房の留め金
    bd[20:22, 20:22, 10:11] = C['brass']

    #! 腕。どっしり両脇で拳を握る（元絵の立ち姿）。胴が幅 20 あるので、
    #  胴の真横に付けると輪郭から 1 ボクセルも出ない（実物で確認）。外へ振り出す。
    #  前腕は革にする——鋼のままだと正面から肩当てと同化して、拳だけが
    #  浮いて見える（実物で確認）。色の柱で拳まで繋ぐ。
    #  拳は帯より下（腿の横）まで下げる——帯と同じ高さに置くと、正面で
    #  帯・前腕・拳が一直線に並んで「背中に棒を隠し持っている」絵になる（実物で確認）。
    arm_l = np.zeros(BIG, np.uint8)
    limb(arm_l, (7, 16, 19), (5, 17, 14), 3, C['steel'])
    limb(arm_l, (5, 17, 14), (4, 17, 10), 3, C['leather'])
    arm_l[2:6, 15:19, 6:10] = C['steel_d']              # 板金の籠手（帯と同色の革だと
                                                        #  正面で帯と繋がって見える）
    arm_r = np.zeros(BIG, np.uint8)
    limb(arm_r, (24, 16, 19), (26, 17, 14), 3, C['steel'])
    limb(arm_r, (26, 17, 14), (27, 17, 10), 3, C['leather'])
    arm_r[26:30, 15:19, 6:10] = C['steel_d']

    #! 背中の両刃斧。柄は背に斜め掛け。刃は y 一定の平面スラブ
    #  （罠 6: x を 1 でも振ると積み木になる。斜めは柄だけが受け持つ）。
    axe = np.zeros(BIG, np.uint8)
    limb(axe, (11, 10, 10), (21, 10, 27), 2, C['bow'])  # 柄（木）
    for i in range(6):                                  # 刃 2 枚。外へ行くほど背が高い
        z0, z1 = 26 - (i * 3) // 5, 29 + (i * 2) // 5
        axe[20 - i:21 - i, 9:11, z0:z1] = C['steel']
        axe[22 + i:23 + i, 9:11, z0:z1] = C['steel']
    axe[15:16, 9:11, 23:31] = C['steel_l']              # 刃先（左）
    axe[27:28, 9:11, 23:31] = C['steel_l']              # （右）
    axe[20:23, 9:11, 26:29] = C['steel_d']              # 斧頭の芯

    cape = np.zeros(BIG, np.uint8)                      # 赤い外套。肩から背へ広がって垂れる
    #  裾の幅は胴と同じまでにする——胴より広げたら、正面で拳の高さに
    #  「横一本の棒」が走って机の天面のように光った（罠 1 と同型。実物で確認）。
    for k in range(15):
        z = 20 - k
        w = 5 + (k * 5) // 14
        y0 = 12 - k // 6
        cape[16 - w:16 + w, y0:y0 + 2, z:z + 1] = C['cape']
        cape[16 - w:16 + w, y0:y0 + 1, z:z + 1] = C['cape_d']  # 裏地

    return [crop('base', probe.make_base()), crop('torso', t), crop('head', h),
            crop('beard', bd), crop('arm_l', arm_l), crop('arm_r', arm_r),
            crop('leg_l', vols['leg_l']), crop('leg_r', vols['leg_r']),
            crop('axe', axe), crop('cape', cape)]


META_DWARF = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'torso', 'voxels': 'torso', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'torso', 'grounded': False,
     'pivot': [16.0, 16.0, 21.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # 髭は頭の子。頭を振れば髭がついていく。
    {'name': 'beard', 'voxels': 'beard', 'parent': 'head', 'grounded': False,
     'pivot': [16.0, 18.0, 20.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_l', 'voxels': 'arm_l', 'parent': 'torso', 'grounded': False,
     'pivot': [7.0, 16.0, 19.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_r', 'voxels': 'arm_r', 'parent': 'torso', 'grounded': False,
     'pivot': [24.0, 16.0, 19.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_l', 'voxels': 'leg_l', 'parent': 'torso', 'grounded': False,
     'pivot': [12.0, 16.0, 9.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'torso', 'grounded': False,
     'pivot': [19.0, 16.0, 9.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # 斧は背に固定（握っていないのでピボードは持たない。probe の鞘と同じ扱い）。
    {'name': 'axe', 'voxels': 'axe', 'parent': 'torso', 'grounded': False,
     'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'cape', 'voxels': 'cape', 'parent': 'torso', 'grounded': False,
     'motion': {'kind': 'wind'}, 'wind_k': 0.3},
]


# ============================================================== 忍び寄る人形『メリー』
# 小ささと頭の大きさが人形の読み。顔は白 1 枚の面（罠 2: 前髪は前へ出さず、
# 生え際の直線＝パッツンで言う）。両腕は前へ伸ばして「忍び寄る」姿勢。
FRONT_MARY = [
    '..hhhhhhhhhh..',  # 21 髪の天
    '.hhhhhhhhhhhh.',  # 20
    '.hhhhhhhhhhhh.',  # 19 生え際（直線＝前髪パッツン）
    '.hhWWWWWWWWhh.',  # 18 額
    '.hhWWWWWWWWhh.',  # 17
    '.hhWEEWWEEWhh.',  # 16 大きな青い目（2×2）
    '.hhWEEWWEEWhh.',  # 15
    '.hhWWWmmWWWhh.',  # 14 口
    '.hhWWWWWWWWhh.',  # 13 顎
    '...RRRRRRRR...',  # 12 肩
    '...RRRRRRRR...',  # 11
    '..RRRRRRRRRR..',  # 10 スカートが開き始める
    '..RRRRRRRRRR..',  # 9
    '.RRRRRRRRRRRR.',  # 8
    '.RRRRRRRRRRRR.',  # 7
    'rrrrrrrrrrrrrr',  # 6 裾（暗い赤。頭より広げないと真上からの絵で消える）
    'rrrrrrrrrrrrrr',  # 5
    '...OO....OO...',  # 4 小さな靴
    '...OO....OO...',  # 3
]

DEPTH_MARY = {
    'h': ('hair_k', 13, 20), 'W': ('skin_w', 13, 20), 'E': ('eye_b', 13, 20),
    'm': ('mouth', 13, 20), 'R': ('dress', 13, 20),
    'r': ('dress_d', 11, 22),   # 裾は前後にも膨らむ（頭の影から出す）
    'O': ('boot', 14, 19),
}


def build_mary():
    vols = zeros('head', 'body')
    probe.raise_front(FRONT_MARY, DEPTH_MARY, 9, 21,
                      lambda x, z, ch: 'head' if z >= 13 else 'body', vols)
    h, b = vols['head'], vols['body']

    paint(h, np.s_[10:22, 13:15, 13:19], 'hair_k')      # 後頭（W の背面が肌で残る）
    paint(h, np.s_[12:20, 14:19, 21:22], 'hair_k_l')    # 天の艶（縁は残す。罠 4）
    h[13:15, 19:20, 17:18] = C['dark']                  # まつ毛の線（目を大きく見せる）
    h[17:19, 19:20, 17:18] = C['dark']
    h[12:13, 19:20, 15:16] = C['cheek']                 # 頬紅（日本人形の化粧）
    h[19:20, 19:20, 15:16] = C['cheek']

    paint(b, np.s_[13:19, 19:20, 7:12], 'dress_l')      # 胴の前面（光の乗る面）
    paint(b, np.s_[9:23, 11:14, 5:13], 'dress_d')       # 背面は暗く

    #! 両腕。前へ差し出して「忍び寄る」。初版の細い袖＋大きな手は、見下ろしの
    #  カメラで袖が頭に隠れ、白い手だけが浮いて見えた。袖を太く・下がり気味に、
    #  手は小さく（2 ボクセル角）して腕として繋がって見せる。
    arm_l = np.zeros(BIG, np.uint8)
    limb(arm_l, (12, 17, 11), (12, 22, 10), 3, C['dress'])
    arm_l[11:13, 22:24, 9:11] = C['skin_w']
    arm_r = np.zeros(BIG, np.uint8)
    limb(arm_r, (19, 17, 11), (19, 22, 10), 3, C['dress'])
    arm_r[18:20, 22:24, 9:11] = C['skin_w']

    #! 長い黒髪。後頭から背へ 1 枚、左右にも房を垂らす（頭の子。風に僅かに揺れる）。
    hair = np.zeros(BIG, np.uint8)
    hair[11:21, 11:13, 6:19] = C['hair_k']              # 背の髪
    hair[13:16, 11:13, 6:8] = 0                         # 裾を不揃いに（房の切れ目）
    hair[18:20, 11:13, 6:8] = 0
    hair[8:10, 13:18, 9:16] = C['hair_k']               # 横の房（左。顔を縁取る）
    hair[22:24, 13:18, 9:16] = C['hair_k']              # （右）

    return [crop('base', probe.make_base()), crop('body', b), crop('head', h),
            crop('arm_l', arm_l), crop('arm_r', arm_r), crop('hair', hair)]


META_MARY = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'body', 'voxels': 'body', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'body', 'grounded': False,
     'pivot': [16.0, 16.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_l', 'voxels': 'arm_l', 'parent': 'body', 'grounded': False,
     'pivot': [12.0, 17.0, 11.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_r', 'voxels': 'arm_r', 'parent': 'body', 'grounded': False,
     'pivot': [19.0, 17.0, 11.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'hair', 'voxels': 'hair', 'parent': 'head', 'grounded': False,
     'motion': {'kind': 'wind'}, 'wind_k': 0.25},
]


# ------------------------------------------------------------------------ 本体
FIGURES = (
    ('fable_horse', build_horse, META_HORSE, 'やせこけた馬（R955）。四つ足'),
    ('fable_dwarf', build_dwarf, META_DWARF, 'ドワーフ戦士（P5_0_0）。背に両刃斧'),
    ('fable_mary', build_mary, META_MARY, "忍び寄る人形『メリー』（R1385）。日本人形風"),
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
        size = probe.ex.write_vox(os.path.join(out_dir, name + '.vox'), parts, probe.PAL)
        fp = probe.ex.write_prefab(os.path.join(out_dir, name + '.jsonc'), name, parts, meta,
                                   note='試作（検討用）。' + note)
        total = sum(int((v > 0).sum()) for _, v, _ in parts)
        top = max(o[2] + v.shape[2] for _, v, o in parts)
        print('%-12s パーツ %2d / ボクセル %5d / 背丈 %2d / vox %6d B / footprint %s'
              % (name, len(parts), total, top, size, fp))
    return 0


if __name__ == '__main__':
    sys.exit(main())
