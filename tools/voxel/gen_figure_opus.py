# -*- coding: utf-8 -*-
"""ボクセルの駒を 3 体（馬・ドワーフ戦士・人形メリー）。

定義の置き場: / **型は `tools/voxel/gen_figure_probe.py`**。
あちらは 1 行も触らず、`importlib` で読んで部品（`limb` / `raise_front` / `make_base` /
`crop` / `paint`）だけ借りている。

| プレハブ | 元絵 | 見どころ |
|---|---|---|
| `opus_horse` | `R955` | **四つ足**。正面図で書けるのは頭だけで、胴は輪切りで彫る |
| `opus_dwarf` | `P5_0_0` | 板金・赤い前立て・**大きな顎髭**・背中の両刃斧。ずんぐり |
| `opus_mary` | `R1385` | 日本人形。**小ささ**と黒髪の量。顔は 6 幅に大きな青い目 |

## 型から借りた決め事（`gen_figure_probe.py` の冒頭がすべての出どころ）
- 土台は `make_base()` の石の円盤。**`grounded` はこれだけ**（footprint は 1 マス）
- 世界は x = 東・y = 南・z = 上。カメラは +y から。**顔は y の大きい側**
- 土台の上面は z = 2。駒は **z = 3 から**立てる
- ポーズは 1 体 1 つで固定。ただし関節は別パーツに分けて `pivot` を置く
- 回した立方体を使わない。斜めは `limb()`（断面を置く）で引く

## 罠 12 件をどう避けたか（あちらの冒頭の並びに合わせる）
1. 肩当ての天面を広くしない —— ドワーフの肩は x に 4・z に 5 で**縦に細く**取った
2. 髪を前へ出さない —— メリーの前髪も馬の前髪もドワーフの髭も**顔と同じ面 (y=19)**
   に彫った。唯一の例外は髭の下端（z<=16。目より下なので顔に影を落とさない）
3. 頭は肩より狭く —— メリーは頭 10・肩 6（髪の量で頭が勝つ）。ドワーフは兜 8・肩 16
4. 天面を x だけ寄せない —— 兜も頭も、x を寄せた段では y も一緒に寄せている
5. 斜めは断面で引く —— 馬の脚・尾、斧の柄、メリーの腕はすべて `limb()`
6. 刃は x を振らない —— **斧の柄は `limb()` の 1 本**で、刃は z ごとに x へ広げた
   板（x-z 平面の 1 枚）。柄と刃で軸を混ぜない
7. 拳だけにしない —— ドワーフは斧、メリーは髪、馬は鬣と尾が輪郭を作る
8. 土台は共通の石（`make_base()`）

残りは `raise_front()` の docstring にある 2 つの検査（行の長さを揃える／奥行き表に
無い材で落とす）と、パレット・土台まわりの注記である。**あちらの本文を正とする。**

## 私が作って初めて分かったこと（3 回撮り直して潰した分）
- **四つ足は正面図では書けない。**胴は y ごとの楕円の輪切りで彫るしかない。ただし
  頭だけは正面図が効く（顔は左右対称で、正面から見た釣り合いが命なのは人型と同じ）。
  それでも**真正面から見た四つ足は読めない**——奥行きに畳まれるので、良し悪しは
  斜め（`AZIMUTH=50`）でしか判断できない。これは駒の側では直せない
- **パーツが重なると面が二重になる。**別々のメッシュなので同じマスに 2 枚の面が出て
  ちらつく。`resolve_overlaps()` で**優先順に後勝ちを消す**。優先は「小さい部品が勝つ」
  ——胴に負けると首や脚の外接直方体が縮み、`pivot` が範囲外になって読み込みで落ちる
- **`pivot` は切り詰めた後の範囲で見られる**（`prefab.cpp:243`）。重なりを消してから
  照合しないと、絵にする前ではなく読み込み時に落ちる
- **色替えは `paint()` で。**直に代入すると、せっかく付けた面取りが埋まる（肩当てで踏んだ）
- **首より高い髪飾り（鬣）を作ると、正面から頭が消える。**輪郭の主役を 2 つ置かない
- **顔の上に肌色の段を残すと鉢巻に見える。**前髪は目のすぐ上まで下ろす（メリー）
- **吊り物は体に貼り付ける。**外套を体から 1 マス離して幅も広げたら、斜めから見て
  体と繋がっていない**赤い幟**になった
- **上下に尖った刃は木槌に見える。**斧は逆で、柄のところがくびれて刃先で上下に張る

## 実行
```
python tools\\voxel\\gen_figure_opus.py
.\\HengbandHd2d.exe --prefab-check=opus_horse         # **パイプに通さないこと**
set HD2D_PREFAB_AZIMUTH=90
.\\HengbandHd2d.exe --prefab=opus_horse --windowed=900x1100 --shot=shots\\figure\\h.bmp
```
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

C = probe.C
BIG = probe.BIG
ex = probe.ex
limb = probe.limb
paint = probe.paint
crop = probe.crop
zeros = probe.zeros
raise_front = probe.raise_front


def reg(name, rgb):
    """色を足す。**試作のパレットの続きに入れる**（1 つの .vox に全色を入れる方針）。"""
    probe.PAL[len(probe.PAL) + 1] = rgb
    probe.C[name] = len(probe.PAL)


# ---- やせこけた馬（R955）。茶に橙の照り・鬣と尾は濃い茶 ----
reg('hs', (120, 62, 34))
reg('hs_l', (172, 90, 40))
reg('hs_d', (74, 38, 22))
reg('mane', (64, 32, 20))
reg('mane_d', (42, 22, 15))
reg('hoof', (56, 52, 56))
reg('hoof_l', (94, 90, 94))
reg('blaze', (228, 216, 198))
reg('muzz', (78, 56, 44))
reg('eye_am', (204, 140, 44))
# ---- ドワーフ戦士（P5_0_0）。紺の外衣・鋼・赤い前立てと外套・赤茶の髭 ----
reg('coat', (52, 60, 84))
reg('coat_l', (74, 84, 112))
reg('beard', (152, 76, 32))
reg('beard_d', (100, 46, 20))
reg('beard_l', (190, 106, 46))
reg('crest', (188, 34, 40))
reg('crest_d', (126, 22, 30))
reg('eye_bl', (96, 150, 206))
reg('dboot', (126, 78, 44))
reg('dboot_l', (162, 106, 62))
# ---- 忍び寄る人形メリー（R1385）。黒髪・白い顔・大きな青い目・赤いワンピース ----
reg('kuro', (26, 24, 32))
reg('kuro_l', (60, 58, 74))
reg('pale', (246, 232, 222))
reg('pale_d', (206, 176, 168))
reg('blue', (72, 124, 196))
reg('blue_d', (28, 52, 108))
reg('kuchi', (172, 46, 54))
reg('hoo', (238, 158, 158))
reg('dress', (196, 40, 46))
reg('dress_d', (134, 24, 34))
reg('dress_l', (224, 76, 74))
reg('shoe', (74, 44, 32))


# ------------------------------------------------------------------ 共通の道具
def resolve_overlaps(vols, order):
    """**同じマスを 2 つのパーツが持たないようにする。**先に来たものが勝つ。

    パーツごとに別のメッシュを焼くので、重なったマスは**面が 2 枚**出る（ちらつく）。
    優先は「小さい部品が勝つ」——胴に負けると首や脚が縮み、`pivot` が外接直方体から
    はみ出して `load_prefab` の検査（§7.2-2）で落ちる。
    """
    claimed = np.zeros(BIG, bool)
    dropped = {}
    for name in order:
        vol = vols[name]
        dup = (vol > 0) & claimed
        n = int(dup.sum())
        if n:
            vol[dup] = 0
            dropped[name] = n
        claimed |= (vol > 0)
    return dropped


def check_pivots(parts, meta):
    """`pivot` が**切り詰めた後の**範囲に入っているかを見る（`prefab.cpp:243` と同じ式）。

    絵にする前に落としたい。読み込みで落ちると `--prefab=` が窓を出して止まる。
    """
    box = {p.name: (p.origin, p.vol.shape) for p in parts}
    for m in meta:
        if 'pivot' not in m:
            continue
        origin, size = box[m['voxels']]
        for k in range(3):
            local = m['pivot'][k] - origin[k]
            if (local < 0) or (local > size[k]):
                raise ValueError('%s の pivot が範囲外（軸 %d: %.1f が 0..%d の外）'
                                 % (m['name'], k, local, size[k]))


# ============================================================== やせこけた馬 R955
# **正面図で書けるのは頭だけ。**四つ足の胴は y ごとの輪切りで彫る。
# 顔は y の大きい側（＝こちらを見ている）。元絵も正面を向いているので都合がよい。
FRONT_HORSE = [
    '..k....k..',  # 36 耳の先（**内へ倒す**。外へ倒すと牛の角に見える）
    '.kk....kk.',  # 35 耳
    '.AAAAAAAA.',  # 34 頭頂
    '.AAAAAAAA.',  # 33 額
    '.AAAAAAAA.',  # 32 目のある段
    '.AABBBBAA.',  # 31 頬（ここから前へ出る）
    '..BBBBBB..',  # 30 鼻梁
    '..BBBBBB..',  # 29
    '...CCCC...',  # 28 鼻面（いちばん前）
    '...CCCC...',  # 27
    '...nnnn...',  # 26 口
]

#: 材 → (色, 奥行き y0:y1)。**段ごとに前面を変えて楔にする**（A=27 / B=29 / C=30）。
#  馬面は「前へ長い」ので、人型のように 1 枚の面では作れない。
DEPTH_HORSE = {
    'k': ('mane_d', 24, 28),
    'A': ('hs', 23, 28),
    'B': ('hs', 24, 30),
    'C': ('hs', 25, 31),
    'n': ('muzz', 26, 31),
}
FACE_Y = {'A': 27, 'B': 29, 'C': 30}   # 段ごとの前面（彫り込みはここへ置く）


def build_horse():
    vols = zeros('head', 'neck', 'mane', 'body', 'tail',
                 'leg_fl', 'leg_fr', 'leg_rl', 'leg_rr')
    raise_front(FRONT_HORSE, DEPTH_HORSE, 11, 36, lambda x, z, ch: 'head', vols)
    h = vols['head']

    #! 顔の彫り。**前へ出さない**（罠 2）。全部その段の前面の中で色を替える。
    #  **流星を 1 本の長い帯にしない**——鋼の棒に見えた（1 回目の実物）。額と鼻面で切る。
    for z in (33, 32):
        h[15:17, FACE_Y['A'], z] = C['blaze']      # 額の星
    h[15, 29, 31] = C['blaze']
    h[14:18, 30, 27] = C['blaze']                  # 鼻面（口の上だけ白い）
    h[14, 30, 28] = C['muzz']                      # 鼻孔
    h[17, 30, 28] = C['muzz']
    h[12, 27, 32] = C['eye_am']                    # 目（頭の角。馬の目は横に付く）
    h[19, 27, 32] = C['eye_am']
    h[12, 27, 33] = C['mane_d']                    # 眉の影（目を 1 マスで読ませるため）
    h[19, 27, 33] = C['mane_d']
    h[14:18, 27, 34] = C['mane']                   # 前髪（面の中に彫る）
    paint(h, np.s_[11:21, 23:25, 27:35], 'hs_d')   # 後頭は暗く

    #! 首。**輪切りで彫る。**上へ行くほど前へ出て細くなる。
    #  1 回目は短くて太かったので、正面から**頭が肩に埋まって熊に見えた**。
    n = vols['neck']
    for z in range(23, 34):
        t = (z - 23) / 10.0
        y0 = int(round(19.5 + 3.5 * t))
        y1 = int(round(25.0 + 2.0 * t))
        hx = 3.4 - 0.9 * t
        for x in range(11, 21):
            if abs(x - 15.5) <= hx:
                n[x, y0:y1, z] = C['hs']
    paint(n, np.s_[11:21, 0:44, 31:34], 'hs_l')

    #! 鬣。**頭より高くしない**（1 回目は鬣の天が頭と同じ高さで、正面から頭が消えた）。
    #  首の背に沿う細い房にして、肩へ落とす。
    m = vols['mane']
    for z in range(22, 34):
        t = (z - 22) / 11.0
        y0 = int(round(19.0 + 3.8 * t))
        half = 3.0 - 1.2 * t
        for x in range(12, 20):
            if abs(x - 15.5) > half:
                continue
            m[x, y0 - 2:y0 + 1, z] = C['mane']
            m[x, y0 - 2:y0 - 1, z] = C['mane_d']
    for x in (13, 15, 17):                         # 房の毛先（肩へ落ちる）
        m[x, 17:20, 20:23] = C['mane_d']

    #! 胴。y ごとの楕円。**背は少し落ち、腹は上がる**（やせこけている）。
    #  胸は細く（前脚の間に隙間を作らないと、正面が茶色の壁になる）。
    b = vols['body']
    for y in range(7, 25):
        t = (y - 7) / 17.0
        hx = 4.0 - 1.1 * t * t                     # **痩せているので細い**
        zc = 20.2 + 0.9 * t
        hz = 4.9 - 0.5 * t
        for x in range(10, 22):
            for z in range(14, 27):
                if ((x - 15.5) / hx) ** 2 + ((z - zc) / hz) ** 2 <= 1.0:
                    b[x, y, z] = C['hs']
    paint(b, np.s_[10:22, 7:25, 14:17], 'hs_d')    # 下腹
    for y in (12, 14, 16, 18):                     # 肋（**無作為のまだらにしない**）
        paint(b, np.s_[10:13, y:y + 1, 17:23], 'hs_d')
        paint(b, np.s_[19:22, y:y + 1, 17:23], 'hs_d')
    for y in (9, 11, 13, 15, 17, 19):              # 照り（元絵の橙の流れ）。**天面には置かない**
        paint(b, np.s_[10:12, y:y + 1, 20:25], 'hs_l')
        paint(b, np.s_[20:22, y:y + 1, 20:25], 'hs_l')

    #! 尾。付け根から後ろへ垂らす（土台からはみ出してよい）。
    #  **尻の高いところから出す。**1 回目は胴の陰に沈んで斜めからも見えなかった。
    tl = vols['tail']
    limb(tl, (15, 6, 24), (15, 4, 16), 6, C['mane'])
    limb(tl, (15, 4, 16), (15, 3, 9), 5, C['mane_d'])
    limb(tl, (15, 3, 9), (15, 4, 5), 4, C['mane'])
    for x in (13, 15, 17):                         # 房の筋（黒い塊にしない）
        paint(tl, np.s_[x:x + 1, 0:44, 6:24], 'hs_d')

    #! 脚 4 本。前は真っ直ぐ・後ろは飛節で折る。**細く**（やせこけ）。
    for name, x0, front in (('leg_fl', 13, True), ('leg_fr', 18, True),
                            ('leg_rl', 13, False), ('leg_rr', 18, False)):
        lg = vols[name]
        if front:
            limb(lg, (x0, 21, 18), (x0, 21, 11), 3, C['hs'])
            limb(lg, (x0, 21, 11), (x0, 21, 6), 3, C['hs_d'])
            lg[x0 - 1:x0 + 2, 19:24, 5:7] = C['hs']
            lg[x0 - 2:x0 + 2, 19:24, 3:5] = C['hoof']
            lg[x0 - 2:x0 + 2, 22:24, 4:5] = C['hoof_l']
        else:
            limb(lg, (x0, 10, 19), (x0, 8, 12), 5, C['hs'])
            limb(lg, (x0, 8, 12), (x0, 11, 7), 3, C['hs_d'])
            lg[x0 - 1:x0 + 2, 9:14, 5:8] = C['hs']
            lg[x0 - 2:x0 + 2, 9:14, 3:5] = C['hoof']
            lg[x0 - 2:x0 + 2, 12:14, 4:5] = C['hoof_l']

    order = ['head', 'mane', 'neck', 'leg_fl', 'leg_fr', 'leg_rl', 'leg_rr', 'tail', 'body']
    resolve_overlaps(vols, order)
    return [crop('base', probe.make_base())] + [crop(k, vols[k]) for k in order]


META_HORSE = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'body', 'voxels': 'body', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'neck', 'voxels': 'neck', 'parent': 'body', 'grounded': False,
     'pivot': [15.5, 22.0, 25.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'neck', 'grounded': False,
     'pivot': [15.5, 25.0, 31.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'mane', 'voxels': 'mane', 'parent': 'neck', 'grounded': False,
     'pivot': [15.5, 20.0, 24.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'tail', 'voxels': 'tail', 'parent': 'body', 'grounded': False,
     'pivot': [15.5, 7.0, 22.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_fl', 'voxels': 'leg_fl', 'parent': 'body', 'grounded': False,
     'pivot': [13.0, 21.0, 17.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_fr', 'voxels': 'leg_fr', 'parent': 'body', 'grounded': False,
     'pivot': [18.0, 21.0, 17.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_rl', 'voxels': 'leg_rl', 'parent': 'body', 'grounded': False,
     'pivot': [13.0, 10.0, 18.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_rr', 'voxels': 'leg_rr', 'parent': 'body', 'grounded': False,
     'pivot': [18.0, 10.0, 18.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
]


# ========================================================== ドワーフ戦士 P5_0_0
# ずんぐり: 背丈 26（人間は 32）に対して肩幅 16（人間は 14）。**低くて広い**。
# 髭は顔と同じ面 (y=19) から始める（罠 2）。前へ出すのは目より下の段だけ。
FRONT_DWARF = [
    '.......NNNN.......',  # 26 兜の天（**x と一緒に y も寄せる**。罠 4）
    '.....MMMMMMMM.....',  # 25
    '.....MMMMMMMM.....',  # 24
    '.....mmmmmmmm.....',  # 23 眉庇（暗い鋼）
    '.....MFFFFFFM.....',  # 22 額（両脇は頬当て）
    '.....MFeFFeFM.....',  # 21 目
    '.....MFFFFFFM.....',  # 20 頬
    '...ssbbbbbbbbss...',  # 19 髭の始まり＋肩当ての天（**天面を広くしない**。罠 1）
    '..SSSbbbbbbbbSSS..',  # 18 肩当て（外へ向かって下がる）
    '.SSSSbbbbbbbbSSSS.',  # 17
    '.SSSbbbbbbbbbbSSS.',  # 16 髭がいちばん広い
    '..SSbbbbbbbbbbSS..',  # 15
    '...TTTbbbbbbTTT...',  # 14 髭の裾（丸く落とす）
    '....LLLLLLLLLL....',  # 13 帯
    '....LLLLUULLLL....',  # 12 尾ロック
    '....TTTTTTTTTT....',  # 11 草摺
    '...TTTTTTTTTTTT...',  # 10
    '....pppp..pppp....',  # 9 脚（短い）
    '....pppp..pppp....',  # 8
    '....pppp..pppp....',  # 7
    '...OOOOO..OOOOO...',  # 6 長靴
    '...OOOOO..OOOOO...',  # 5
    '...OOOOO..OOOOO...',  # 4
    '...OOOOO..OOOOO...',  # 3
]

DEPTH_DWARF = {
    'N': ('steel_l', 14, 19),    # 兜の天。**y を狭める**——広いと机の天板に見える
    'M': ('steel', 12, 20), 'm': ('steel_d', 12, 20),
    'F': ('skin', 12, 20), 'e': ('skin', 12, 20),
    'b': ('beard', 16, 20),      # 髭は前 4 マスだけ。奥は板金（別に埋める）
    's': ('steel_l', 14, 20), 'S': ('steel', 13, 20),   # 肩当ては**浅く**（罠 1）
    'T': ('coat', 11, 20),
    'L': ('leather_d', 10, 21), 'U': ('brass', 10, 21),
    'p': ('trouser', 12, 19), 'O': ('dboot', 11, 20),
}


def build_dwarf():
    def kind(x, z, ch):
        if ch == 'b':
            return 'beard'
        if ch in 'NMmFe':
            return 'head'
        if ch in 'pO':
            return 'leg_l' if x < 15.5 else 'leg_r'
        return 'torso'

    vols = zeros('head', 'beard', 'torso', 'leg_l', 'leg_r',
                 'arm_l', 'arm_r', 'axe', 'cloak')
    raise_front(FRONT_DWARF, DEPTH_DWARF, 7, 26, kind, vols)
    h, bd, t = vols['head'], vols['beard'], vols['torso']

    #! 兜と顔。**すべて面 (y=19) の中の彫り。**
    h[15:17, 19, 21:24] = C['steel_l']       # 鼻当て（兜から下りる 1 本）
    h[14, 19, 21] = C['eye_bl']              # 目
    h[17, 19, 21] = C['eye_bl']
    h[15:17, 19, 20] = C['skin_d']           # 鼻の影
    h[13, 19, 22] = C['skin_d']              # 眉の影（**顔全体を暗くしない**）
    h[18, 19, 22] = C['skin_d']
    #! 前立て。**兜の天に載せて後ろへ垂らす。**
    #  1 回目は z=27,28 に平らな板を置いたので、兜の後ろで宙に浮いた赤い棒に見えた。
    for y, z0, z1 in ((19, 27, 29), (18, 27, 29), (17, 27, 29), (16, 27, 29),
                      (15, 27, 29), (14, 26, 29), (13, 26, 28), (12, 25, 28),
                      (11, 25, 27), (10, 24, 27), (9, 23, 26), (8, 23, 25)):
        h[15:17, y, z0:z1] = C['crest'] if y >= 13 else C['crest_d']
    h[12, 12:20, 20:24] = C['steel_d']       # 頬当ての外側の影
    h[19, 12:20, 20:24] = C['steel_d']

    #! 髭。**編み込みは面の中の色替え**（前へ出すと顔が窪みの底になる）。
    bd[13, 19, 14:19] = C['beard_d']
    bd[18, 19, 14:19] = C['beard_d']
    bd[15:17, 19, 15:19] = C['beard_d']      # 中央の編み
    bd[12:20, 19, 19] = C['beard_l']         # 口髭（いちばん明るい段）
    bd[11:21, 19, 16] = C['beard_l']
    bd[12:20, 20, 14:17] = C['beard']        # 下端だけ 1 マス前へ出す（目より下）

    #! 胴。髭の奥は正面図に書けないので、ここで板金の箱を埋める。
    t[11:21, 11:16, 10:20] = C['coat']
    t[12:20, 14:16, 15:20] = C['steel']      # 胸当て（髭の奥。肩の下から覗く）
    t[11:21, 11:12, 10:20] = C['coat_l']
    paint(t, np.s_[7:25, 19:20, 10:12], 'steel')     # 帯から下がる板
    t[14:18, 19:20, 10:12] = C['leather_d']          # 中央は革の袋
    #  **色替えは `paint()` で。**直に代入すると肩当ての面取りが埋まる（1 度やった）。
    paint(t, np.s_[8:12, 19:20, 15:20], 'steel_l')   # 肩当ての前面（光の乗る面）
    paint(t, np.s_[20:24, 19:20, 15:20], 'steel_l')
    paint(t, np.s_[8:9, 11:20, 15:20], 'steel_d')
    paint(t, np.s_[23:24, 11:20, 15:20], 'steel_d')

    #! 腕。**両方とも下ろす**（元絵どおり）。肘で 1 度折り、拳は帯の高さ。
    for name, sx in (('arm_l', -1), ('arm_r', +1)):
        a = vols[name]
        sh = 15 + sx * 6       # 肩の x（10 / 20）
        wr = 15 + sx * 7       # 手首の x（8 / 22）
        limb(a, (sh, 15, 18), (wr, 16, 14), 4, C['coat'])
        limb(a, (wr, 16, 14), (wr, 17, 11), 4, C['steel'])
        a[wr - 1:wr + 2, 16:19, 9:12] = C['skin']
        a[wr - 1:wr + 2, 16:19, 12:13] = C['steel_l']

    #! 背中の両刃斧。**柄は `limb()` の 1 本・刃は x-z 平面の板**（軸を混ぜない）。
    ax = vols['axe']
    limb(ax, (19, 7, 12), (25, 7, 27), 3, C['leather'])
    #  **砂時計の輪郭にする。**1 回目は「z ごとに x へ広げたレンズ」だったので、
    #  上下が尖って左右が丸い＝**木槌**にしか見えなかった。斧は逆で、
    #  柄のところがくびれ、刃先で上下に張る。刃先は y を薄くして「刃」を言う。
    for dx in range(-5, 6):
        u = abs(dx) / 5.0
        hz = 1.4 + 2.4 * u * u
        z0, z1 = int(round(29 - hz)), int(round(29 + hz)) + 1
        y0, y1 = (6, 9) if u < 0.45 else ((6, 8) if u < 0.85 else (6, 7))
        ax[25 + dx, y0:y1, z0:z1] = C['steel']
        if u > 0.85:
            ax[25 + dx, y0:y1, z0:z1] = C['steel_l']     # 刃先
    ax[25, 6:9, 27:31] = C['steel_d']        # 柄の通る頭（**細く**。太いと刃が割れて見える）
    ax[24:27, 5:6, 28:30] = C['brass']

    #! 赤い外套。肩から吊る。**胴より広く**取って、脇から赤が覗くようにする。
    #  **胴の背に貼り付ける（y = 10..11）。**1 回目は y = 9,10 で幅も広げすぎたので、
    #  斜めから見ると体から離れた**赤い幟**に見えた。
    cl = vols['cloak']
    for z in range(6, 20):
        u = (19 - z) / 13.0
        half = 5.5 + 1.6 * u
        for x in range(3, 29):
            #! 裾は**位相の決まった波**で切る（無作為に切ると汚れに見える）。
            if (abs(x - 15.5) > half) or (z < 6 + int(round(1.2 * (1.0 + math.cos(x * 0.9))))):
                continue
            cl[x, 10:12, z] = C['crest_d']
            cl[x, 11, z] = C['crest']

    order = ['head', 'beard', 'arm_l', 'arm_r', 'leg_l', 'leg_r', 'axe', 'cloak', 'torso']
    resolve_overlaps(vols, order)
    return [crop('base', probe.make_base())] + [crop(k, vols[k]) for k in order]


META_DWARF = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'torso', 'voxels': 'torso', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'torso', 'grounded': False,
     'pivot': [15.5, 16.0, 20.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # 髭は**頭が親**。首を振れば髭がついていく（親子 3 段目）。
    {'name': 'beard', 'voxels': 'beard', 'parent': 'head', 'grounded': False,
     'pivot': [15.5, 18.0, 19.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_l', 'voxels': 'arm_l', 'parent': 'torso', 'grounded': False,
     'pivot': [10.0, 15.0, 18.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_r', 'voxels': 'arm_r', 'parent': 'torso', 'grounded': False,
     'pivot': [21.0, 15.0, 18.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_l', 'voxels': 'leg_l', 'parent': 'torso', 'grounded': False,
     'pivot': [12.5, 16.0, 10.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'torso', 'grounded': False,
     'pivot': [18.5, 16.0, 10.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # 斧は**握りにピボット**。いまは背に負っているが、振るならここが動く。
    {'name': 'axe', 'voxels': 'axe', 'parent': 'torso', 'grounded': False,
     'pivot': [19.0, 7.0, 12.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # 外套は風。**しなやかさはパーツの底面から焼かれる**ので、吊り物では裾が固い（§11.5-7）。
    {'name': 'cloak', 'voxels': 'cloak', 'parent': 'torso', 'grounded': False,
     'motion': {'kind': 'wind'}, 'wind_k': 0.3},
]


# ====================================================== 忍び寄る人形メリー R1385
# **小ささが読みの本体**（背丈 21。人間は 32・イークは 22）。頭が体の半分近くある。
# 顔は 6 幅で、そのうち 4 マスが目。**目を大きく取るほど人形に見える。**
FRONT_MARY = [
    '.....hhhhhh.....',  # 21 髪の天（**丸める**。平らな広い天は箱に見える）
    '....hhhhhhhh....',  # 20
    '...hhhhhhhhhh...',  # 19
    '...hhffffffhh...',  # 18 前髪（パッツン。**面の中に彫る**）
    '...hhffffffhh...',  # 17 ——額を前髪の上に出すと、**灰色の鉢巻**に見えた
    '...hheeWWeehh...',  # 16 目（上）
    '...hhEEWWEEhh...',  # 15 目（下）
    '...hhbFFFFbhh...',  # 14 頬紅
    '...hhFFmmFFhh...',  # 13 口
    '...hhhFFFFhhh...',  # 12 顎
    '...hhhhNNhhhh...',  # 11 首
    '..hhhRRRRRRhhh..',  # 10 肩（髪が肩の前へ落ちる）
    '..hhhRRRRRRhhh..',  # 9
    '..hhhRRRRRRhhh..',  # 8
    '..hhhRRRRRRhhh..',  # 7
    '..hhRRRRRRRRhh..',  # 6 裾が広がる
    '..hRRRRRRRRRRh..',  # 5 裾
    '.....LL..LL.....',  # 4 脚
    '....SSS..SSS....',  # 3 靴
]

DEPTH_MARY = {
    'h': ('kuro', 15, 20), 'f': ('kuro', 13, 20),
    'F': ('pale', 13, 20), 'W': ('pale', 13, 20),
    'e': ('pale', 13, 20), 'E': ('pale', 13, 20),
    'b': ('hoo', 13, 20), 'm': ('kuchi', 13, 20),
    'N': ('pale_d', 14, 19),
    'R': ('dress', 13, 20),
    'L': ('pale_d', 16, 19), 'S': ('shoe', 16, 20),   # 足は**浅く**（深いと棒が前へ出る）
}


def build_mary():
    def kind(x, z, ch):
        if ch in 'hf':
            return 'hair'
        if ch in 'LS':
            return 'leg_l' if x < 15.5 else 'leg_r'
        if ch == 'R':
            return 'torso'
        return 'head'

    vols = zeros('head', 'hair', 'torso', 'arm_l', 'arm_r', 'leg_l', 'leg_r')
    raise_front(FRONT_MARY, DEPTH_MARY, 8, 21, kind, vols)
    h, hr, t = vols['head'], vols['hair'], vols['torso']

    #! 目。**面 (y=19) の中に彫る。**白目は取れない（顔が 6 幅しかない）ので、
    #  青を主にして黒目を 1 マスだけ落とす。1 回目は上段を濃紺にしたら前髪と繋がって
    #  **青い眉**にしか見えなかった。
    for x0 in (13, 17):
        h[x0:x0 + 2, 19, 15:17] = C['blue']
        h[x0 + 1, 19, 15] = C['blue_d']      # 瞳
        h[x0, 19, 16] = C['pale']            # 光（1 マスだけ）
    h[15:17, 19, 15:17] = C['pale']          # 鼻筋
    h[15, 19, 14] = C['pale_d']
    h[15:17, 19, 13] = C['kuchi']

    #! 髪。**量が輪郭を作る**（元絵は顔より髪のほうが広い）。
    #  後ろの塊は y = 8..12。胴（y >= 13）と重ならない位置に置く。
    #  1 回目は「幅 14・奥行き 5 の直方体」だったので、斜めから見ると**黒い箱**だった。
    #  x でも y でも丸める（罠 4 と同じ話）。
    for z in range(5, 22):
        u = max(0.0, (16.0 - z) / 11.0)
        half = 4.9 + 0.9 * u
        if z >= 18:                          # 頭頂は丸屋根に落とす
            half = 5.0 * math.sqrt(max(0.05, 1.0 - ((z - 17.5) / 4.4) ** 2))
        for x in range(8, 24):
            dx = abs(x - 15.5) / max(half, 0.1)
            if dx > 1.0:
                continue
            #! 毛先を**位相の決まった波**で切る。真横一直線だと箱の底に見える。
            if z < 6 + int(round(1.4 * (1.0 + math.cos(x * 1.1)))):
                continue
            # **下へ行くほど薄い幕にする**（1 回目は下まで同じ厚みの塊だったので、
            # 斜めから見ると人形より髪のほうが大きい**黒い箱**だった）。
            # 段で切ると横から見て階段になるので、z で連続に細らせる。
            back = 0.7 + 1.9 * max(0.0, (z - 5) / 16.0)
            depth = 1.0 + back * math.sqrt(max(0.0, 1.0 - dx * dx))
            hr[x, int(round(12.5 - depth)):13, z] = C['kuro']
    for x in (11, 20):                       # 顔の脇の髪に縦の筋（1 本ずつ）
        hr[x, 19, 9:18] = C['kuro_l']

    #! ワンピース。**縦の襞**（無作為のまだらにしない）。
    paint(t, np.s_[8:24, 13:20, 5:6], 'dress_d')     # 裾の際だけ落とす
    for x in (13, 16, 19):
        paint(t, np.s_[x:x + 1, 19:20, 5:9], 'dress_d')
    paint(t, np.s_[8:24, 19:20, 8:11], 'dress_l')    # 胸元は明るく

    #! 腕。**髪の前 (y = 17..19) に置く。**1 回目は髪と同じ層に引いたので、
    #  肩から赤い羽根が生えて手が宙に浮いた白い塊になった。体側へ真っ直ぐ垂らす。
    for name, sx in (('arm_l', -1), ('arm_r', +1)):
        a = vols[name]
        wr = 15 + sx * 4                     # 腕の x（11 / 19）
        a[wr - 1:wr + 2, 16:20, 7:10] = C['dress']                 # 膨らんだ袖
        a[wr - 1:wr + 2, 16:20, 9:10] = C['dress_d']               # 肩と袖を**値で切る**
        limb(a, (wr, 18, 7), (wr, 18, 5), 3, C['pale'])            # 前腕
        a[wr - 1 + (sx > 0):wr + 1 + (sx > 0), 17:20, 4:6] = C['pale_d']   # 手（小さく）

    order = ['head', 'arm_l', 'arm_r', 'hair', 'leg_l', 'leg_r', 'torso']
    resolve_overlaps(vols, order)
    return [crop('base', probe.make_base())] + [crop(k, vols[k]) for k in order]


META_MARY = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'torso', 'voxels': 'torso', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'torso', 'grounded': False,
     'pivot': [15.5, 16.0, 11.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # 髪は**頭が親**。首を傾げれば髪がついていく。
    {'name': 'hair', 'voxels': 'hair', 'parent': 'head', 'grounded': False,
     'pivot': [15.5, 12.0, 19.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_l', 'voxels': 'arm_l', 'parent': 'torso', 'grounded': False,
     'pivot': [12.0, 16.0, 10.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_r', 'voxels': 'arm_r', 'parent': 'torso', 'grounded': False,
     'pivot': [18.0, 16.0, 10.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_l', 'voxels': 'leg_l', 'parent': 'torso', 'grounded': False,
     'pivot': [13.5, 16.0, 5.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'torso', 'grounded': False,
     'pivot': [17.5, 16.0, 5.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
]


# ------------------------------------------------------------------------ 本体
FIGURES = (
    ('opus_horse', build_horse, META_HORSE, 'やせこけた馬（R955）。四つ足'),
    ('opus_dwarf', build_dwarf, META_DWARF, 'ドワーフ戦士（P5_0_0）。背に両刃斧'),
    ('opus_mary', build_mary, META_MARY, '忍び寄る人形メリー（R1385）。小さい'),
)


def main():
    out_dir = os.path.join(ROOT, 'assets', 'voxel')
    for name, builder, meta, note in FIGURES:
        parts = builder()
        got = {p.name for p in parts}
        want = {m['voxels'] for m in meta}
        if got != want:
            print('%s: parts mismatch %s' % (name, got ^ want))
            return 1
        check_pivots(parts, meta)
        tuples = [p.tuple() for p in parts]
        size = ex.write_vox(os.path.join(out_dir, name + '.vox'), tuples, probe.PAL)
        fp = ex.write_prefab(os.path.join(out_dir, name + '.jsonc'), name, tuples, meta,
                             note=note)
        total = sum(int((v > 0).sum()) for _, v, _ in tuples)
        top = max(o[2] + v.shape[2] for _, v, o in tuples)
        print('%-12s parts %2d / voxels %5d / height %2d / vox %6d B / footprint %s'
              % (name, len(parts), total, top, size, fp))
    return 0


if __name__ == '__main__':
    sys.exit(main())
