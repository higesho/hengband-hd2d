# -*- coding: utf-8 -*-
"""ボクセルの駒 3 体（sonnet 分担）。型は `gen_figure_probe.py`（1 行も変更しない）。

定義の置き場:。土台・footprint・関節の決まり、および
「作って初めて分かった」罠 12 件は probe の docstring に従う。

| プレハブ | 元絵 | 見た目 |
|---|---|---|
| `sonnet_horse` | `R955` | やせこけた馬。四つ足。茶色、たてがみと尾は濃い茶 |
| `sonnet_dwarf` | `P5_0_0` | ドワーフ戦士。板金鎧・赤い前立ての兜・大きな赤茶の顎髭・背に両刃斧・赤い外套 |
| `sonnet_mary` | `R1385` | 忍び寄る人形『メリー』。日本人形風。長い黒髪パッツン・大きな青い目・赤いワンピース |

## 型からの逸脱は 1 か所（理由つき）
馬は 4 本の脚が前後左右に分かれるので、`FRONT_*`（1 文字 = 1 ボクセルを y へ押し出す表）
では表現できない。**胴の長軸を y に取り**（顔は既に y の大きい側を向く）、胴・首・頭は
スライスと `limb()` で直接組む。ドワーフとメリーは型どおり正面図で起こす——ただし
**脚も正面図の中に含める**（左右対称な直立の脚は正面図で書ける。`kind_of()` で
`leg_l`/`leg_r` へ振り分ければ、パーツも関節ピボットもちゃんと別になる）。

## 実行
```
python tools\\voxel\\gen_figure_sonnet.py
.\\HengbandHd2d.exe --prefab-check=sonnet_horse     # 窓なし。パイプに通さないこと
.\\HengbandHd2d.exe --prefab-check=sonnet_dwarf
.\\HengbandHd2d.exe --prefab-check=sonnet_mary
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
    """probe のパレットの続きに色を足す。索引 0 は空なので `len(PAL) + 1`。"""
    probe.PAL[len(probe.PAL) + 1] = rgb
    C[name] = len(probe.PAL)


def sym(left):
    """左半分（外縁→中心の順）の文字列から、中心で折り返した対称な文字列を作る。

    手でパディングの文字数を数えると必ずどこかで間違える（この試みで一度やった）。
    `raise_front` は行の長さが 1 文字でも違うと落ちるので、パディングは
    `str.center` に任せて手で数えない。
    """
    return left + left[::-1]


def crow(width, content):
    """`content` を `width` へ中央寄せする（余白は `.`）。行の長さを手で数えない。"""
    return content.center(width, '.')


# ------------------------------------------------------------------------ 色
# ---- やせこけた馬（R955）。地は茶。たてがみ・尾・蹄は濃い茶。
reg('hide', (150, 92, 48))
reg('hide_l', (186, 122, 64))
reg('hide_d', (104, 62, 36))
reg('mane', (68, 40, 26))
reg('mane_d', (46, 26, 18))
reg('hoof', (52, 38, 32))
reg('blaze', (196, 168, 130))   # 額から鼻筋の白い流星。耳の影に落ちる場所なので
                                 # 白へ寄せすぎない（影の中で彩度を失って灰色に沈む——実物で確認）
reg('muzzle_d', (78, 54, 46))
reg('horse_eye', (30, 22, 18))
# ---- ドワーフ戦士（P5_0_0）。鎧は青みの鋼、髭は赤茶。
reg('dsteel', (128, 138, 156))
reg('dsteel_d', (78, 86, 102))
reg('dsteel_l', (176, 184, 198))
reg('plume', (196, 42, 46))
reg('helm_trim', (150, 30, 34))
reg('dwarf_eye', (58, 118, 196))
reg('beard', (150, 88, 44))
reg('beard_d', (104, 58, 26))
reg('beard_l', (188, 126, 68))
reg('dtunic', (44, 64, 112))
reg('dtunic_l', (68, 92, 148))
reg('dtunic_d', (28, 42, 78))
reg('dbelt', (92, 58, 32))
# ---- 忍び寄る人形『メリー』（R1385）。黒髪は真っ黒にしない（面の切れ目を読ませる）。
reg('m_hair', (26, 24, 30))
reg('m_hair_l', (54, 52, 66))
# 肌は白に寄せすぎない。影の中では明るい色ほど彩度を失って灰色に沈む
# （馬の頭の天で確認済みの現象と同じ）。人間の肌に近い暖かさを残す。
reg('m_skin', (246, 208, 176))
reg('m_skin_d', (212, 168, 138))
reg('m_eye', (52, 96, 176))
reg('m_dress', (188, 30, 38))
reg('m_dress_l', (216, 68, 66))
reg('m_dress_d', (132, 18, 26))
reg('m_blush', (232, 160, 148))
reg('mouth', (150, 60, 62))


# ==================================================================== やせこけた馬
# 四つ足なので胴の長軸を y に取る（顔は既に y の大きい側＝カメラ側を向く）。
# 「やせこけた」は形で言う——肩と腰だけ幅を張り、胴の中程を細く括り、
# 背骨の稜を 1 段立て、脇腹に肋の縞を彫る（実物で確認済みの手筋・fable 分担と同じ発想）。
def build_horse():
    b = np.zeros(BIG, np.uint8)
    b[13:19, 9:21, 14:20] = C['hide']            # 胴の芯（幅 6・細い）
    b[12:20, 18:22, 15:20] = C['hide']           # 肩の骨（幅 8 に張る。前寄り＝首の付け根側）
    b[12:20, 9:13, 15:20] = C['hide']            # 腰の骨（幅 8）
    b[13:19, 13:17, 14:15] = 0                   # 腹の括れ（中程の底を 1 段抜く。痩せの本体）
    b[15:17, 10:22, 20:21] = C['hide_d']         # 背骨の稜（面の中の色替えではなく 1 段立てる）
    paint(b, np.s_[12:20, 9:22, 18:20], 'hide_l')    # 背は明るく（面の向きで陰影）
    paint(b, np.s_[12:20, 9:22, 14:16], 'hide_d')    # 下腹は暗く
    paint(b, np.s_[12:20, 21:22, 15:19], 'hide_l')   # 胸の前面（光の乗る面）
    for xx in (12, 19):                              # 肋の縞（脇腹の面の中に彫る。前へは出さない）
        for yy in range(11, 20, 3):
            paint(b, np.s_[xx:xx + 1, yy:yy + 1, 15:19], 'hide_d')

    #! 首。頭を上げた「警戒」の姿勢（fable 分担のうなだれ姿勢とは差別化）。
    #  頭の骨（z=24 から）へ食い込ませない——2 パーツが同じボクセルを取り合うと
    #  継ぎ目で影の乗り方が食い違い、額が濁った灰色に見える罠がある（実物で確認）。
    n = np.zeros(BIG, np.uint8)
    limb(n, (16, 21, 19), (16, 24, 22), 5, C['hide'])
    limb(n, (16, 24, 22), (16, 26, 24), 4, C['hide'])
    #! たてがみ。首の中へ埋めると 1 ボクセルも見えない（罠と同種）。首の天より
    #  1 段上へ、背側（-y）へ寄せた立て板として乗せる。
    for i in range(6):
        y = 21 + i
        z = 19 + i
        n[15:17, y - 1:y + 1, z:z + 2] = C['mane']

    #! 頭。目は側面（馬の目は横に付く）。鼻面は y+ へ突き出し、額から鼻筋へ
    #  白い流星を通す（元絵の読みの決め手）。鼻先は暗く落として穴を言う。
    h = np.zeros(BIG, np.uint8)
    h[13:19, 25:29, 24:30] = C['hide']           # 頭骨
    h[14:18, 28:33, 22:26] = C['hide']           # 鼻面（+y へ突き出す）
    #! 頭の天は明るくしない。耳とたてがみの影に落ちる場所なので、明るい色を
    #  置くと逆に濁った灰色に沈む（実物で確認——影の中では明るい色ほど彩度を失う）。
    paint(h, np.s_[14:18, 32:33, 22:26], 'muzzle_d')     # 鼻先の面
    paint(h, np.s_[15:17, 27:29, 27:29], 'blaze')        # 額の流星
    paint(h, np.s_[15:17, 29:32, 25:26], 'blaze')        # 鼻筋へ続ける
    h[14:15, 32:33, 23:24] = C['dark']           # 鼻孔
    h[17:18, 32:33, 23:24] = C['dark']
    h[13:14, 26:27, 27:28] = C['horse_eye']      # 目（左の側面。馬の目は横）
    h[18:19, 26:27, 27:28] = C['horse_eye']      # （右）
    h[13:15, 26:27, 29:31] = C['hide_d']         # 耳（左）
    h[17:19, 26:27, 29:31] = C['hide_d']         # （右）
    h[15:17, 24:27, 29:30] = C['mane']           # 前髪の房（耳の間。天面のみ）

    #! 脚 4 本。細さ 2（痩せ馬）。x で左右、y で前後に分かれる——胴の長軸が
    #  y なので、4 本が本当の意味で前後左右に離れる（横向きの押し出しでごまかさない）。
    legs = {}
    for name, cx, cy in (('leg_fl', 13, 20), ('leg_fr', 19, 20),
                         ('leg_bl', 13, 10), ('leg_br', 19, 10)):
        v = np.zeros(BIG, np.uint8)
        limb(v, (cx, cy, 14), (cx, cy, 5), 2, C['hide_d'])
        v[cx - 1:cx + 2, cy - 1:cy + 2, 3:5] = C['hoof']    # 蹄（脚より 1 回り太く）
        legs[name] = v

    tail = np.zeros(BIG, np.uint8)               # 尾。太さ 3（2 では胴の陰に消える）
    limb(tail, (16, 8, 19), (16, 5, 10), 3, C['mane'])
    limb(tail, (16, 5, 10), (16, 5, 5), 2, C['mane_d'])

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
     'pivot': [16.0, 25.0, 23.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_fl', 'voxels': 'leg_fl', 'parent': 'body', 'grounded': False,
     'pivot': [13.0, 20.0, 14.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_fr', 'voxels': 'leg_fr', 'parent': 'body', 'grounded': False,
     'pivot': [19.0, 20.0, 14.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_bl', 'voxels': 'leg_bl', 'parent': 'body', 'grounded': False,
     'pivot': [13.0, 10.0, 14.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_br', 'voxels': 'leg_br', 'parent': 'body', 'grounded': False,
     'pivot': [19.0, 10.0, 14.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'tail', 'voxels': 'tail', 'parent': 'body', 'grounded': False,
     'pivot': [16.0, 8.0, 19.0], 'motion': {'kind': 'wind'}, 'wind_k': 0.25},
]


# ==================================================================== ドワーフ戦士
# ずんぐり: 幅 20 の胴に短い脚。脚も正面図に含め、`kind_of` で左右へ振り分ける
# （直立した左右対称の脚は正面図で書ける——`limb()` を強いられるのは斜めのポーズだけ）。
# 髭は 'D'/'d' の文字だけ別パーツへ回す（頭を振れば髭がついていく）。
FRONT_DWARF = [
    crow(20, sym('pp')),          # 27 兜の羽根飾り（先）
    crow(20, sym('ppp')),         # 26 羽根飾り
    crow(20, sym('MMr')),         # 25 兜の天・赤い筋
    crow(20, sym('MMMr')),        # 24 兜の鉢
    crow(20, sym('MMkk')),        # 23 眉のあたり（目は後で彫る）
    crow(20, sym('Mkkk')),        # 22 頬
    crow(20, sym('kkuu')),        # 21 口ひげ
    crow(20, sym('SSkkDD')),      # 20 肩当て・顎の下・髭の始まり
    crow(20, sym('SsskDDD')),     # 19 肩当て（全幅）
    crow(20, sym('SsskDDDD')),    # 18 髭が広がる
    crow(20, sym('SsskdDDD')),    # 17 髭の房（濃淡）
    crow(20, sym('sskDDDD')),     # 16 髭
    crow(20, sym('sTkDDD')),      # 15 胴衣が覗く
    crow(20, sym('TDWk')),        # 14 帯・髭の先端
    crow(20, sym('CTKk')),        # 13 裾・外套の覗き
    crow(20, sym('CTKK')),        # 12
    crow(20, sym('CCKK')),        # 11 裾の下端
    crow(20, sym('KKKK.')),       # 10 脚（上・ずんぐり）
    crow(20, sym('KKKK.')),       # 9
    crow(20, sym('OOOO.')),       # 8 長靴
    crow(20, sym('OOOO.')),       # 7
    crow(20, sym('OOOO.')),       # 6
    crow(20, sym('OOOO.')),       # 5
    crow(20, sym('OOOO.')),       # 4
    crow(20, sym('OOOO.')),       # 3
]

DEPTH_DWARF = {
    'p': ('plume', 13, 20), 'M': ('dsteel', 13, 20), 'r': ('helm_trim', 13, 20),
    'k': ('skin', 13, 20), 'u': ('beard', 15, 20),
    'S': ('dsteel_d', 12, 21), 's': ('dsteel_l', 12, 21),
    'D': ('beard', 12, 21), 'd': ('beard_d', 12, 21),
    'T': ('dtunic', 13, 20), 'W': ('dbelt', 13, 20), 'K': ('dtunic_d', 13, 20),
    'C': ('cape', 13, 20), 'O': ('boot', 13, 20),
}

DWARF_X0 = 6
DWARF_CENTER = DWARF_X0 + 10  # = 16


def _dwarf_kind(x, z, ch):
    if ch in ('D', 'd'):
        return 'beard'
    if z >= 21:
        return 'head'
    if z <= 10:
        return 'leg_l' if x < DWARF_CENTER else 'leg_r'
    return 'torso'


def build_dwarf():
    vols = zeros('head', 'torso', 'beard', 'leg_l', 'leg_r')
    probe.raise_front(FRONT_DWARF, DEPTH_DWARF, DWARF_X0, 27, _dwarf_kind, vols)
    h, t, bd = vols['head'], vols['torso'], vols['beard']

    paint(h, np.s_[11:21, 13:15, 21:25], 'dsteel')       # 後頭も兜（正面の肌は残る）
    paint(h, np.s_[14:18, 14:19, 27:28], 'dsteel_l')     # 兜の天の艶（縁は残す）
    h[13:14, 19:20, 23:24] = C['dwarf_eye']              # 目（左）
    h[18:19, 19:20, 23:24] = C['dwarf_eye']              # （右）
    #! 赤い前立て。天を y に走る鶏冠の立て板。中程を 1 段高くして板の輪郭を出す
    #  （罠 6 と同種——真上から一定にすると「赤い箱」に見える）。
    h[15:17, 12:20, 28:30] = C['plume']
    h[15:17, 13:18, 30:31] = C['plume']
    h[15:17, 10:13, 26:29] = C['plume']

    paint(t, np.s_[6:26, 13:14, 10:21], 'dsteel_d')      # 鎧の背面は暗く
    paint(t, np.s_[8:12, 19:20, 14:19], 'dsteel_l')      # 胸甲の前面（光の乗る面）
    paint(t, np.s_[20:24, 19:20, 14:19], 'dsteel_l')

    #! 髭。地を暗い赤茶に落とし、明るい房を長さ不揃いに走らせる（罠と同種——
    #  一様な暗い縞は「木の板の継ぎ目」に見える。実物で確認するまで直す）。
    for x, z0, z1 in ((13, 14, 18), (15, 13, 19), (17, 15, 20), (19, 14, 18)):
        paint(bd, np.s_[x:x + 1, 20:21, z0:z1], 'beard_l')
    paint(bd, np.s_[14:18, 20:21, 19:20], 'beard_d')     # 鼻の下の影
    bd[11:13, 20:22, 9:16] = C['beard_d']                # 編んだ房（左。帯の下まで）
    bd[19:21, 20:22, 9:16] = C['beard_d']                # （右）

    #! 腕。胴が幅 20 あるので、真横に付けると輪郭から出ない（実物で確認済みの
    #  手筋）。外へ振り出し、拳は籠手の茶で締める。
    arm_l = np.zeros(BIG, np.uint8)
    limb(arm_l, (7, 16, 19), (4, 17, 12), 3, C['dsteel'])
    arm_l[2:6, 15:19, 9:13] = C['boot']
    arm_r = np.zeros(BIG, np.uint8)
    limb(arm_r, (24, 16, 19), (27, 17, 12), 3, C['dsteel'])
    arm_r[26:30, 15:19, 9:13] = C['boot']

    #! 背中の両刃斧。柄は斜め掛け（`limb()`）。刃は y 一定の平面スラブ 2 枚——
    #  x を段ごとに振ると積み木の棒に見える罠があるので、傾きは柄だけが受け持つ。
    #  初版は刃の z 幅をほぼ一定のまま x へ広げたので「大きな一枚板」に見えた
    #  （実物で確認）。各段の z 幅を 2〜3 に抑え、外へ行くほど段を高くして
    #  三日月状の刃先を階段で作る。斧頭の全幅も胴より狭く収める。
    axe = np.zeros(BIG, np.uint8)
    limb(axe, (13, 10, 12), (19, 10, 22), 2, C['dbelt'])        # 柄（木）
    axe[17:21, 9:11, 21:23] = C['dsteel_d']                     # 斧頭の座
    for i, (z0, z1) in enumerate(((21, 24), (23, 26), (25, 29))):
        axe[19 - 2 - i * 2:19 - i * 2, 9:11, z0:z1] = C['dsteel']       # 左の刃
        axe[19 + 1 + i * 2:19 + 3 + i * 2, 9:11, z0:z1] = C['dsteel']   # 右の刃
    axe[13:15, 9:11, 27:30] = C['dsteel_l']                     # 刃先（左）
    axe[24:26, 9:11, 27:30] = C['dsteel_l']                     # （右）

    cape = np.zeros(BIG, np.uint8)               # 赤い外套。肩から背へ広がって垂れる
    for k in range(15):
        z = 20 - k
        w = 5 + (k * 6) // 14
        y0 = 12 - k // 6
        cape[16 - w:16 + w, y0:y0 + 2, z:z + 1] = C['cape']
        cape[16 - w:16 + w, y0:y0 + 1, z:z + 1] = C['cape_d']   # 裏地

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
     'pivot': [12.0, 16.0, 10.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'torso', 'grounded': False,
     'pivot': [19.0, 16.0, 10.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # 斧は背に固定（担いでいるのではなく背負っているので握りのピボットは持たない）。
    {'name': 'axe', 'voxels': 'axe', 'parent': 'torso', 'grounded': False,
     'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'cape', 'voxels': 'cape', 'parent': 'torso', 'grounded': False,
     'motion': {'kind': 'wind'}, 'wind_k': 0.3},
]


# ============================================================== 忍び寄る人形『メリー』
# 小ささと頭の大きさが読みの本体。顔は白 1 枚の面に彫る（罠 2: 前髪を前へ出さない
# ＝生え際は直線のパッツンで言う）。両腕は前へ差し出して「忍び寄る」姿勢。
FRONT_MARY = [
    crow(16, sym('hH')),          # 23 髪の天
    crow(16, sym('HHh')),         # 22
    crow(16, sym('HHHh')),        # 21 生え際（直線＝前髪パッツン）
    crow(16, sym('HFF')),         # 20 額
    crow(16, sym('HFFF')),        # 19 大きな目のあたり（目は後で彫る）
    crow(16, sym('HFFF')),        # 18 頬
    crow(16, sym('HFmF')),        # 17 口
    crow(16, sym('HFFF')),        # 16 顎
    crow(16, sym('HN')),          # 15 首（髪が両脇に垂れる）
    crow(16, sym('HrR')),         # 14 肩・袖
    crow(16, sym('rRRD')),        # 13 胸
    crow(16, sym('RRDD')),        # 12
    crow(16, sym('RRDR')),        # 11 ひだ
    crow(16, sym('RDDR')),        # 10
    crow(16, sym('RRDD')),        # 9
    crow(16, sym('rRDDR')),       # 8 裾が開き始める
    crow(16, sym('rRRDDr')),      # 7 裾（頭より広げないと真上からの絵で消える）
    crow(16, sym('OO.')),         # 6 小さな靴
    crow(16, sym('OO.')),         # 5
    crow(16, sym('OO.')),         # 4
    crow(16, sym('OO.')),         # 3 土台の天（z=2）に接する行。ここが無いと 1 段浮く
]

DEPTH_MARY = {
    'h': ('m_hair_l', 13, 20), 'H': ('m_hair', 13, 20),
    'F': ('m_skin', 13, 20), 'N': ('m_skin_d', 15, 19), 'm': ('mouth', 15, 19),
    'R': ('m_dress', 13, 20), 'r': ('m_dress_l', 13, 20), 'D': ('m_dress_d', 12, 21),
    'O': ('boot', 14, 19),
}

MARY_X0 = 9
MARY_CENTER = MARY_X0 + 8  # = 17


def build_mary():
    def kind(x, z, ch):
        if z >= 15:
            return 'head'
        if z <= 6:
            return 'leg_l' if x < MARY_CENTER else 'leg_r'
        return 'body'

    vols = zeros('head', 'body', 'leg_l', 'leg_r')
    probe.raise_front(FRONT_MARY, DEPTH_MARY, MARY_X0, 23, kind, vols)
    h, b = vols['head'], vols['body']

    paint(h, np.s_[11:23, 13:15, 13:22], 'm_hair')       # 後頭（F の背面は肌で残る）
    paint(h, np.s_[13:21, 14:20, 22:23], 'm_hair_l')     # 頭頂の艶（縁は残す）
    h[13:15, 19:20, 18:19] = C['dark']                   # まつ毛の線（目を大きく見せる決め手）
    h[19:21, 19:20, 18:19] = C['dark']
    h[13:15, 19:20, 19:20] = C['m_eye']                  # 大きな青い目
    h[19:21, 19:20, 19:20] = C['m_eye']
    h[12:13, 19:20, 16:17] = C['m_blush']                # 頬紅（日本人形の化粧）
    h[21:22, 19:20, 16:17] = C['m_blush']

    paint(b, np.s_[13:19, 19:20, 8:14], 'm_dress_l')     # 胴の前面（光の乗る面）
    paint(b, np.s_[9:23, 11:14, 5:15], 'm_dress_d')      # 背面は暗く

    #! 両腕。前へ差し出して「忍び寄る」。袖は太めに・下がり気味にして、見下ろしの
    #  カメラでも腕として繋がって見えるようにする（細いと頭の陰で千切れる）。
    arm_l = np.zeros(BIG, np.uint8)
    limb(arm_l, (12, 17, 13), (11, 22, 11), 3, C['m_dress'])
    arm_l[10:12, 22:24, 10:12] = C['m_skin']
    arm_r = np.zeros(BIG, np.uint8)
    limb(arm_r, (21, 17, 13), (22, 22, 11), 3, C['m_dress'])
    arm_r[21:23, 22:24, 10:12] = C['m_skin']

    #! 長い黒髪。後頭から背へ 1 枚、左右にも房を垂らして顔を縁取る（頭の子。
    #  風にわずかに揺れる）。裾は不揃いに切って「房」を言う。
    #  初版は背の房を z=6 まで（腰下）伸ばしたので、大きな黒い一枚板に見えた
    #  （実物で確認）。肩までで止め、横の房も頭の高さへ寄せて塊を分ける。
    hair = np.zeros(BIG, np.uint8)
    hair[11:23, 11:13, 10:21] = C['m_hair']      # 後頭から肩への房（腰までは垂らさない）
    hair[13:16, 11:13, 10:12] = 0                # 裾を不揃いに（房の切れ目）
    hair[19:22, 11:13, 10:12] = 0
    hair[15:19, 11:13, 9:10] = C['m_hair']       # 中央だけ 1 房長く
    #! 顔を縁取る横の房。初版は頭から離れた 1 本の角柱で、宙に浮いた柱に見えた
    #  （実物で確認）。頭のヘアラインへ寄せて隙間を詰め、天を細らせて丸みを出す。
    hair[10:13, 14:19, 15:23] = C['m_hair']      # 房の主体（左。頭に寄せる）
    hair[9:10, 15:18, 16:21] = C['m_hair']       # 外縁のふくらみ
    hair[9:11, 14:17, 11:15] = C['m_hair_l']     # 房の先（下）を細く・艶で締める
    hair[21:24, 14:19, 15:23] = C['m_hair']      # （右）
    hair[24:25, 15:18, 16:21] = C['m_hair']
    hair[23:25, 14:17, 11:15] = C['m_hair_l']

    return [crop('base', probe.make_base()), crop('body', b), crop('head', h),
            crop('arm_l', arm_l), crop('arm_r', arm_r), crop('hair', hair),
            crop('leg_l', vols['leg_l']), crop('leg_r', vols['leg_r'])]


META_MARY = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'body', 'voxels': 'body', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'body', 'grounded': False,
     'pivot': [17.0, 16.0, 15.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_l', 'voxels': 'arm_l', 'parent': 'body', 'grounded': False,
     'pivot': [12.0, 17.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_r', 'voxels': 'arm_r', 'parent': 'body', 'grounded': False,
     'pivot': [21.0, 17.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'hair', 'voxels': 'hair', 'parent': 'head', 'grounded': False,
     'motion': {'kind': 'wind'}, 'wind_k': 0.25},
    {'name': 'leg_l', 'voxels': 'leg_l', 'parent': 'body', 'grounded': False,
     'pivot': [15.0, 16.0, 6.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'body', 'grounded': False,
     'pivot': [19.0, 16.0, 6.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
]


# ------------------------------------------------------------------------ 本体
FIGURES = (
    ('sonnet_horse', build_horse, META_HORSE, 'やせこけた馬（R955）。四つ足。胴の長軸は y'),
    ('sonnet_dwarf', build_dwarf, META_DWARF, 'ドワーフ戦士（P5_0_0）。背に両刃斧・赤い外套'),
    ('sonnet_mary', build_mary, META_MARY, "忍び寄る人形『メリー』（R1385）。日本人形風"),
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
                                   note='試作（検討用・sonnet 分担）。' + note)
        total = sum(int((v > 0).sum()) for _, v, _ in parts)
        top = max(o[2] + v.shape[2] for _, v, o in parts)
        print('%-13s パーツ %2d / ボクセル %5d / 背丈 %2d / vox %6d B / footprint %s'
              % (name, len(parts), total, top, size, fp))
    return 0


if __name__ == '__main__':
    sys.exit(main())
