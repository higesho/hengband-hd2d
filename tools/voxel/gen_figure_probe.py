# -*- coding: utf-8 -*-
"""**試作**: キャラクターとモンスターをボクセルの**駒**にしてみる（検討用）。

定義の置き場の候補: —— ただし **§11 はいま「ビルボード。
ポリゴンにしない」と書いてある**。これはその決定を覆すかどうかを決めるための試作で、
まだ契約ではない。だから `gen_prefabs.py`（地形・町の定義の置き場）には 1 行も足していない。

## 目標
参考にした D&D のミニチュアの写真（2026-08-13）——「**土台があって、その上で
ポーズを取る**」。棒立ちの人形ではない。決めたこと:

| # | 決定 |
|---|---|
| 1 | 土台は**石の円盤**（全部の駒で共通） |
| 2 | **1 体 1 ポーズで固定**（待機・歩き・攻撃は持たない） |
| 3 | 作るのは 1 体ずつ（案 C）。押し出しも原型の使い回しも採らない |

## 作るもの
| プレハブ | 元絵 | 見どころ |
|---|---|---|
| `figure_human` | `P0_0_0.png` 人間の冒険者 | 剣と盾。基本の型 |
| `figure_archer` | `R116` 見習アーチャー | **型の使い回し**（人型 `p` は 174 種ある） |
| `figure_red_dragon` | `R589` レッド・ドラゴン | **1 マスに収まらない相手**（翼が土台からはみ出す） |
| `figure_yeek` | `R141` ブラウン・イーク | **小ささ**。同じ土台に乗ることで背丈が比較になる |

## 土台が構造を助ける（この試作でいちばん効いた発見）
土台を `grounded` の**唯一のパーツ**にすると、footprint の定義の置き場（§7.1-1）は円盤になる。
すると**脚は接地の宣言から外れる**ので、踏み出しても・跳ねても・浮いても検査は通る。
棒立ちを強いていたのは「脚が footprint を持っていたから」だった。

翼や剣が土台からはみ出すのは**構わない**——当たり判定を持つのは円盤だけで、
これは §9.2 の装飾層と同じ扱いである。**駒はマス目に立っている**と絵で言えている。

## 向きの決め事（`gen_prefabs.py` §2121 と同じ）
世界は **x = 東・y = 南・z = 上**で、カメラは注視点の **+y から**見下ろす。
つまり**顔は y の大きい側**へ向ける。ここを間違えると後頭部しか見えない。

## 輪郭は 2 通りで作る
| | 作り方 | 理由 |
|---|---|---|
| 胴・頭 | `FRONT_*` の正面図（1 文字 = 1 ボクセル） | 左右対称で、正面から見た釣り合いが命 |
| 手足・武器・翼・尾 | `limb()` で 3 次元に引く | ポーズは正面図では書けない |

**回した立方体を使わない。**§8.3 で「なめらか」に決めたのは*動き*の話で、素材の姿勢まで
斜めにすると格子が崩れて地形と馴染まない。ポーズは**階段で彫る**。動かすときは関節の
ピボットまわりに回す（そのための親子はそのまま残してある）。

## 作って初めて分かったこと（順に潰した破綻）
1. 肩当ての天面を広くすると**「机」**に見える。この縮尺では縦に細いほうが人に見える
2. 髪や前髪を前へ出すと**顔が窪みの底**になって影で潰れる。顔は 1 枚の面に彫る
3. 頭を胴と同じ幅にすると**頭と肩が 1 つの箱**になる。頭は肩より狭く
4. 天面を **x だけ**内側へ寄せて y を寄せ忘れると、角が**2 本の突起**になる
5. `limb()` で立方体を置くと斜めの腕が**大きな階段の塊**に見える。断面を置く
6. 刀身は **x を 1 でも振ると**段ごとに天面が出て「積み木の棒」に見える。真っ直ぐ立てる
7. 拳だけでは茶色の塊。**丸い盾**を持たせて初めて輪郭が立つ
8. 土台を黒にすると暗いダンジョンで**影と見分けが付かない**。縁を 1 段明るく＋まだら

## 実行
```
python tools\\voxel\\gen_figure_probe.py
.\\HengbandHd2d.exe --prefab-check=figure_yeek          # 窓なし。**パイプに通さないこと**
.\\HengbandHd2d.exe --prefab=figure_yeek --windowed=900x900 --shot=shots\\y.bmp
```
向きは `HD2D_PREFAB_AZIMUTH=90` が世界と同じ側（既定の 0.9rad は斜め）。
"""
import importlib.util
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

spec = importlib.util.spec_from_file_location('export_vox', os.path.join(HERE, 'export_vox.py'))
ex = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ex)

V = 32  # 1 マス = 32 ボクセル（設計書 §5）

# --------------------------------------------------------------------- パレット
# 元絵を量子化して抜いた値を基にしている。**無彩色の段を増やさない**——この縮尺で
# 形を読ませるのは色数ではなく塊の切れ目である。**1 つの .vox に全色を入れる**
# （駒ごとに分けても得はなく、使っていない索引は greedy に 1 面も出さない）。
PAL = {}
C = {}


def reg(name, rgb):
    PAL[len(PAL) + 1] = rgb
    C[name] = len(PAL)


# ---- 人間の冒険者（P0_0_0）----
reg('skin', (233, 173, 122))
reg('skin_d', (196, 136, 92))
reg('hair', (93, 48, 32))
reg('hair_d', (63, 29, 26))
reg('hair_l', (121, 60, 32))
reg('leather', (134, 72, 42))
reg('leather_d', (71, 41, 33))
reg('leather_l', (160, 88, 51))
reg('steel', (141, 148, 160))
reg('steel_d', (84, 92, 104))
reg('steel_l', (186, 192, 202))
reg('cloth_g', (62, 88, 66))
reg('cloth_g_l', (84, 116, 88))
reg('trouser', (31, 48, 62))
reg('trouser_l', (52, 74, 92))
reg('boot', (45, 21, 24))
reg('boot_l', (76, 44, 34))
reg('eye', (46, 132, 146))
reg('dark', (6, 7, 15))
reg('brass', (168, 128, 62))
reg('cape', (118, 56, 58))
reg('cape_d', (82, 38, 42))
reg('shield', (108, 74, 46))
reg('shield_l', (146, 104, 66))
# ---- 土台（共通）。**黒にしない**——暗いダンジョンの床では影と見分けが付かない。
# 縁を 1 段明るくして「円い台」を読ませる（この縮尺では形ではなく明暗が輪郭を作る）。
reg('base_side', (44, 42, 48))
reg('base_top', (86, 84, 92))
reg('base_top_d', (68, 66, 74))
reg('base_rim', (124, 122, 130))
# ---- 見習アーチャー（R116）。緑の胴衣に**明るい縁取り**が入るのが読みの決め手。
reg('tunic', (48, 80, 60))
reg('tunic_l', (72, 110, 78))
reg('trim', (126, 170, 98))
reg('bow', (96, 62, 34))
reg('bow_l', (132, 92, 56))
reg('string', (214, 206, 186))
reg('feather', (206, 200, 188))
# ---- レッド・ドラゴン（R589）。**腹は赤の反対側の色**（クリーム）で、
# これが無いと「赤い塊」にしか見えない。翼膜は体より暗く沈める。
reg('drag', (168, 44, 38))
reg('drag_d', (112, 26, 26))
reg('drag_l', (208, 76, 54))
reg('belly', (222, 178, 116))
reg('belly_d', (184, 138, 86))
reg('memb', (124, 30, 36))
reg('memb_d', (86, 20, 26))
reg('horn', (214, 204, 178))
reg('horn_d', (162, 150, 126))
reg('eye_y', (240, 202, 66))
reg('claw', (238, 232, 214))
# ---- ブラウン・イーク（R141）。**茶色の毛むくじゃら**（記憶 monster-symbol の指摘）。
reg('fur', (108, 74, 48))
reg('fur_d', (72, 48, 30))
reg('fur_l', (144, 104, 68))
reg('paw', (58, 40, 26))
reg('nose', (44, 30, 24))


class Part:
    """原点（プレハブ座標）と密な配列の対。**添字はプレハブ座標のまま書ける。**

    手で `- origin` を書くとどこかで必ず間違えるので、`__setitem__` で吸収する。
    """

    def __init__(self, name, origin, size):
        self.name = name
        self.origin = tuple(int(v) for v in origin)
        self.vol = np.zeros(tuple(int(v) for v in size), np.uint8)

    def __setitem__(self, key, value):
        gx, gy, gz = key
        self.vol[self._shift(gx, 0), self._shift(gy, 1), self._shift(gz, 2)] = value

    def _shift(self, sl, axis):
        o = self.origin[axis]
        if isinstance(sl, slice):
            return slice(None if sl.start is None else sl.start - o,
                         None if sl.stop is None else sl.stop - o, sl.step)
        return sl - o

    def tuple(self):
        return (self.name, self.vol, self.origin)


BIG = (44, 44, 52)  # 作業用の広い箱。最後に外接直方体へ切り詰める
#: 土台。**1 マスに収める**（はみ出すと隣のマスの駒とぶつかる）。
BASE_CX, BASE_CY, BASE_R, BASE_Z = 15.5, 15.5, 12.5, 3


def crop(name, vol):
    """外接直方体へ切り詰めて `Part` にする。**原点を手で書かない**（必ずどこかで間違える）。"""
    idx = np.argwhere(vol > 0)
    lo = idx.min(0)
    hi = idx.max(0) + 1
    p = Part.__new__(Part)
    p.name = name
    p.origin = tuple(int(v) for v in lo)
    p.vol = vol[lo[0]:hi[0], lo[1]:hi[1], lo[2]:hi[2]].copy()
    return p


def limb(vol, p0, p1, thick, colour):
    """`p0` から `p1` へ太さ `thick` の手足を**階段状に**引く。

    **置くのは立方体ではなく「進行方向に垂直な 1 枚の断面」**である。立方体を 1 ボクセル
    刻みで置いた初版は、斜めの腕が**大きな階段の塊**に見えた（実物を撮って気づいた）。
    刻みも 3 倍に細かくする——粗いと細い刀身が飛び石になって切れる。
    **回転で resample しない**（薄い腕は千切れる）。
    """
    delta = [b - a for a, b in zip(p0, p1)]
    axis = max(range(3), key=lambda k: abs(delta[k]))
    steps = max(1, int(max(abs(d) for d in delta) * 3))
    half = thick // 2
    for i in range(steps + 1):
        t = i / steps
        c = [int(round(a + d * t)) for a, d in zip(p0, delta)]
        lo = [c[k] - half for k in range(3)]
        size = [thick, thick, thick]
        size[axis] = 1                      # 進行方向は 1 枚
        lo[axis] = c[axis]
        vol[lo[0]:lo[0] + size[0], lo[1]:lo[1] + size[1], lo[2]:lo[2] + size[2]] = colour


def make_base():
    """石の円盤（決定 1）。**全部の駒で共通・これだけが `grounded`。**"""
    base = np.zeros(BIG, np.uint8)
    for x in range(BIG[0]):
        for y in range(BIG[1]):
            d = math.hypot(x - BASE_CX, y - BASE_CY)
            if d > BASE_R:
                continue
            base[x, y, 0:BASE_Z] = C['base_side']
            base[x, y, BASE_Z - 1:BASE_Z] = C['base_top']
            if ((x * 7 + y * 5) % 11) < 3:
                base[x, y, BASE_Z - 1:BASE_Z] = C['base_top_d']  # まだら（平らな円盤に見せない）
            if d > BASE_R - 1.3:
                base[x, y, BASE_Z - 1:BASE_Z] = C['base_rim']    # 縁を 1 段明るく（円を読ませる）
    return base


def raise_front(front, depth, x0, z_top, kind_of, vols):
    """正面図（1 文字 = 1 ボクセル）を厚みへ起こす。`kind_of(x, z, ch)` がパーツを決める。

    ## 検査 1: 行の長さを揃える（**入れていなかったせいで実害が出た**。2026-08-14）
    行の長さが 1 文字でも違うと、**その行だけ列が横へずれる**。頭と胴の中心が
    1 ボクセルずれた駒が、`--prefab-check` も footprint の照合も**通ったまま**出てくる
    ——絵にしないと気づけない種類の破綻である。だから**ここで落とす**。

    ## 検査 2: 知らない材を黙って捨てない
    `depth` に無い文字は `KeyError` になるが、それだと**どの行のどこか**が分からない。
    行と列を添えて投げ直す。

    どちらも `--self-check` で**わざと壊した入力に反応することを確かめてある**
    （設計書 §7.2「検出したいものを検出できない検査は、無いより悪い」）。
    """
    widths = sorted({len(line) for line in front})
    if len(widths) != 1:
        bad = ['  行 %d（z=%d）: %d 文字 "%s"' % (i, z_top - i, len(line), line)
               for i, line in enumerate(front) if len(line) != widths[-1]]
        raise ValueError('正面図の行の長さが揃っていません（%s 文字が混在）。'
                         'その行だけ列が横へずれ、頭と胴の中心が食い違います。\n%s'
                         % ('/'.join(str(w) for w in widths), '\n'.join(bad)))
    for row, line in enumerate(front):
        z = z_top - row
        for col, ch in enumerate(line):
            if ch == '.':
                continue
            if ch not in depth:
                raise KeyError('正面図の行 %d（z=%d）の %d 文字目 "%s" が奥行き表にありません'
                               % (row, z, col, ch))
            colour, y0, y1 = depth[ch]
            vols[kind_of(x0 + col, z, ch)][x0 + col, y0:y1, z] = C[colour]


def zeros(*names):
    return {n: np.zeros(BIG, np.uint8) for n in names}


def paint(vol, sl, colour):
    """既にあるボクセルだけ塗り替える（形は変えない）。"""
    vol[sl] = np.where(vol[sl] > 0, C[colour], 0)


# ==================================================================== 人間の冒険者
#: 胴と頭の正面図。x = 8..23（16 列）・z = 32..13（20 行）。手足は含まない。
FRONT_HUMAN = [
    '.....hhhhhh.....',  # 32 髪（天）
    '.....HHHHHH.....',  # 31
    '.....HHHHHH.....',  # 30
    '.....HFFFFH.....',  # 29 額
    '.....HFFFFH.....',  # 28
    '.....HeFFeH.....',  # 27 目
    '.....HFFFFH.....',  # 26
    '......FFFF......',  # 25 顎
    '......NNNN......',  # 24 首
    '..ssTTGGGGTTss..',  # 23 襟と肩当ての天
    '..SSTTTGGTTTSS..',  # 22 肩当て
    '..SSTTTGGTTTSS..',  # 21
    '....TTTGGTTT....',  # 20 胸
    '....TTTGGTTT....',  # 19
    '....TTTTTTTT....',  # 18
    '....TTTTTTTT....',  # 17
    '...BBBBBBBBBB...',  # 16 帯
    '...BBBBUUBBBB...',  # 15 尾ロック
    '....TTTTTTTT....',  # 14 胴衣の裾
    '...TTTTTTTTTT...',  # 13 裾の折り返し
]

#: 材 → (色, 奥行き y0:y1)。**前面は必ず y = 19**（面を揃えないと影が濁る）。
DEPTH_HUMAN = {
    'h': ('hair_l', 13, 20), 'H': ('hair', 13, 20),
    'F': ('skin', 13, 20), 'e': ('skin', 13, 20),
    'N': ('skin_d', 15, 19),
    'S': ('steel', 15, 20), 's': ('steel_l', 15, 20),
    'G': ('cloth_g', 13, 20), 'T': ('leather', 13, 20),
    'B': ('leather_d', 12, 21), 'U': ('steel_l', 12, 21),
}


def _human_head(h, hair_top, hair, hair_dark, skin_dark, eye):
    """人型の頭の側面・背面と顔の彫り。**アーチャーと共用**（型の使い回しはここが本体）。"""
    h[13:19, 13:15, 25:33] = C[hair]         # 後ろ髪
    h[13:19, 13:20, 31:33] = C[hair]         # 頭頂の塊（**天面を刳らない**。x だけ内側へ
                                             #  寄せて y を寄せ忘れると角が 2 本の突起になる）
    h[14:18, 14:19, 32:33] = C[hair_top]     # 天面（縁は残す）
    h[13:14, 13:20, 26:33] = C[hair]         # 揉み上げ（左）
    h[18:19, 13:20, 26:33] = C[hair]         # （右）
    h[14:18, 19:20, 30:31] = C[hair_dark]    # 前髪（面の中に彫る）
    h[14:18, 19:20, 29:30] = C[skin_dark]    # 額の影
    h[14:15, 19:20, 27:28] = C[eye]          # 目
    h[17:18, 19:20, 27:28] = C[eye]
    h[15:17, 19:20, 25:26] = C[skin_dark]    # 口もと


def build_human():
    vols = zeros('head', 'torso')
    raise_front(FRONT_HUMAN, DEPTH_HUMAN, 8, 32,
                lambda x, z, ch: 'head' if z >= 25 else 'torso', vols)
    h, t = vols['head'], vols['torso']
    _human_head(h, 'hair_l', 'hair', 'hair_d', 'skin_d', 'eye')

    #! 胴の面の意匠。**すべて面の中の色替え**で、前へは出さない。
    t[12:20, 19:20, 19:23] = C['leather_l']  # 胸の前面（光の乗る面）
    t[12:20, 19:20, 13:15] = C['leather_l']  # 裾の前面
    t[15:17, 19:20, 22:24] = C['cloth_g_l']  # 胸元の合わせ
    for k in range(5):                       # 斜め掛けの帯
        t[13 + k, 19:20, 22 - k:23 - k] = C['leather_d']
    t[10:11, 15:20, 21:23] = C['steel_d']    # 肩当ての外側の影（左）
    t[21:22, 15:20, 21:23] = C['steel_d']    # （右）

    #! 手足。**ここがポーズ**。右は剣を振りかぶり、左は盾を構える。脚は踏み込みの前後。
    arm_r = np.zeros(BIG, np.uint8)
    limb(arm_r, (21, 17, 22), (25, 16, 26), 3, C['cloth_g'])   # 上腕（緑の袖）
    limb(arm_r, (25, 16, 26), (26, 15, 31), 3, C['leather'])   # 前腕（篭手）
    arm_r[25:28, 14:17, 30:32] = C['leather_d']                # 拳

    arm_l = np.zeros(BIG, np.uint8)
    limb(arm_l, (11, 17, 22), (8, 19, 19), 3, C['cloth_g'])
    limb(arm_l, (8, 19, 19), (9, 22, 16), 3, C['leather'])     # 前へ差し出す
    arm_l[8:11, 22:25, 15:17] = C['leather_d']                 # 拳

    #! 円い盾。**輪郭を作るのはこれ**——手だけだと拳が茶色の塊に見える（実物で確認）。
    shield = np.zeros(BIG, np.uint8)
    for x in range(BIG[0]):
        for z in range(BIG[2]):
            d = math.hypot(x - 8, z - 18)
            if d > 5.4:
                continue
            shield[x, 21:24, z] = C['shield']
            shield[x, 23:24, z] = C['shield_l']                # 表（光の乗る面）
            if d > 4.2:
                shield[x, 21:24, z] = C['steel']               # 鉄の縁
    shield[6:11, 23:25, 16:21] = C['steel']                    # 臍（中央の金具）
    shield[7:10, 24:25, 17:20] = C['brass']

    leg_r = np.zeros(BIG, np.uint8)                            # 踏み込む前脚
    limb(leg_r, (19, 16, 13), (19, 19, 8), 3, C['trouser'])
    limb(leg_r, (19, 19, 8), (19, 21, 5), 3, C['trouser'])
    leg_r[17:21, 20:25, 3:6] = C['boot']
    leg_r[17:21, 23:25, 5:6] = C['boot_l']

    leg_l = np.zeros(BIG, np.uint8)                            # 蹴り出す後脚
    limb(leg_l, (13, 16, 13), (12, 12, 8), 3, C['trouser'])
    limb(leg_l, (12, 12, 8), (11, 10, 5), 3, C['trouser'])
    leg_l[9:13, 8:12, 3:6] = C['boot']
    leg_l[9:13, 8:10, 5:6] = C['boot_l']

    #! 振りかぶった剣。**拳を親にする**ので、腕を動かせば剣もついていく。
    #  **真っ直ぐ立てる。**x を 1 でも振ると、段ごとに天面（＝いちばん明るい面）が
    #  現れて別々の塊が積み上がって見える。傾きは腕の斜めが受け持てばよい。
    sword = np.zeros(BIG, np.uint8)
    sword[25:28, 14:17, 29:31] = C['leather_d']                # 柄
    sword[24:29, 13:18, 31:32] = C['steel']                    # 鍔
    limb(sword, (26, 15, 32), (26, 15, 43), 3, C['steel'])     # 刀身
    paint(sword, np.s_[24:30, 16:17, 32:44], 'steel_l')        # 鎬は**前面だけ**
    sword[25:28, 14:17, 28:29] = C['brass']                    # 柄頭

    scabbard = np.zeros(BIG, np.uint8)                         # 空の鞘（剣を抜いている）
    limb(scabbard, (14, 12, 21), (10, 11, 12), 2, C['leather_d'])

    cape = np.zeros(BIG, np.uint8)
    for k in range(13):                                        # 肩から後ろへ翻る
        y, z, w = 13 - k // 3, 23 - k, 3 + k // 4
        cape[16 - w:16 + w, y:y + 2, z:z + 1] = C['cape']
        cape[16 - w:16 + w, y:y + 1, z:z + 1] = C['cape_d']    # 裏地

    return [crop('base', make_base()), crop('torso', t), crop('head', h),
            crop('arm_l', arm_l), crop('arm_r', arm_r), crop('shield', shield),
            crop('leg_l', leg_l), crop('leg_r', leg_r),
            crop('sword', sword), crop('scabbard', scabbard), crop('cape', cape)]


META_HUMAN = [
    # 土台。**唯一の `grounded`。**footprint を決めるのはこの円盤である。
    # 親を持たないので、胴が動いても台は動かない。
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'torso', 'voxels': 'torso', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'torso', 'grounded': False,
     'pivot': [16.0, 17.0, 25.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_l', 'voxels': 'arm_l', 'parent': 'torso', 'grounded': False,
     'pivot': [11.0, 17.0, 22.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_r', 'voxels': 'arm_r', 'parent': 'torso', 'grounded': False,
     'pivot': [21.0, 17.0, 22.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # 盾と剣は**拳が親**。腕を振れば得物がついていく（親子 3 段を通す実物）。
    {'name': 'shield', 'voxels': 'shield', 'parent': 'arm_l', 'grounded': False,
     'pivot': [9.0, 22.0, 16.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # 脚。**土台が footprint を持つので、踏み出しても検査は通る。**
    {'name': 'leg_l', 'voxels': 'leg_l', 'parent': 'torso', 'grounded': False,
     'pivot': [13.0, 16.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'torso', 'grounded': False,
     'pivot': [19.0, 16.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'sword', 'voxels': 'sword', 'parent': 'arm_r', 'grounded': False,
     'pivot': [26.0, 15.0, 30.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'scabbard', 'voxels': 'scabbard', 'parent': 'torso', 'grounded': False,
     'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # **しなやかさは「パーツの底面から」焼かれる**（`greedy_mesher.cpp:353`）ので、
    # 肩から吊った外套ではここが**逆**になる（裾が固く、肩がよく動く）。
    {'name': 'cape', 'voxels': 'cape', 'parent': 'torso', 'grounded': False,
     'motion': {'kind': 'wind'}, 'wind_k': 0.35},
]


# ================================================================== 見習アーチャー
# **型の使い回しの実験**（人型 `p` は 174 種ある）。頭は `_human_head()` をそのまま呼び、
# 正面図は胴衣の意匠だけ差し替える。手足は弓を引く姿勢に彫り直す。
FRONT_ARCHER = [
    '.....hhhhhh.....',  # 32
    '.....HHHHHH.....',  # 31
    '.....HHHHHH.....',  # 30
    '.....HFFFFH.....',  # 29
    '.....HFFFFH.....',  # 28
    '.....HeFFeH.....',  # 27
    '.....HFFFFH.....',  # 26
    '......FFFF......',  # 25
    '......NNNN......',  # 24 首
    '....VVVVVVVV....',  # 23 襟（明るい縁取り。**元絵の読みの決め手**）
    '....TTTTTTTT....',  # 22
    '....TTTVVTTT....',  # 21 前立て
    '....TTTVVTTT....',  # 20
    '....TTTVVTTT....',  # 19
    '....TTTVVTTT....',  # 18
    '....TTTTTTTT....',  # 17
    '...BBBBBBBBBB...',  # 16 帯
    '...BBBBUUBBBB...',  # 15 尾ロック
    '....TTTTTTTT....',  # 14
    '...TTTTTTTTTT...',  # 13
]

DEPTH_ARCHER = {
    'h': ('hair_l', 13, 20), 'H': ('hair', 13, 20),
    'F': ('skin', 13, 20), 'e': ('skin', 13, 20),
    'N': ('skin_d', 15, 19),
    'V': ('trim', 13, 20), 'T': ('tunic', 13, 20),
    'B': ('leather_d', 12, 21), 'U': ('brass', 12, 21),
}


def build_archer():
    vols = zeros('head', 'torso')
    raise_front(FRONT_ARCHER, DEPTH_ARCHER, 8, 32,
                lambda x, z, ch: 'head' if z >= 25 else 'torso', vols)
    h, t = vols['head'], vols['torso']
    _human_head(h, 'hair_l', 'hair', 'hair_d', 'skin_d', 'eye')

    t[12:20, 19:20, 17:23] = C['tunic_l']    # 胸の前面（光の乗る面）
    t[12:20, 19:20, 13:15] = C['tunic_l']
    for k in range(6):                       # 矢筒の負い革（斜め掛け）
        t[12 + k, 19:20, 17 + k:18 + k] = C['leather']

    #! ポーズ: **弓を引き絞る。**左腕をまっすぐ左へ、右手は頬まで引く。
    arm_l = np.zeros(BIG, np.uint8)
    limb(arm_l, (11, 17, 22), (5, 18, 21), 3, C['tunic'])      # 押し手（まっすぐ）
    limb(arm_l, (8, 18, 22), (5, 18, 21), 3, C['leather'])     # 篭手
    arm_l[3:6, 17:20, 20:23] = C['leather_d']                  # 拳

    arm_r = np.zeros(BIG, np.uint8)
    limb(arm_r, (21, 17, 22), (26, 12, 22), 3, C['tunic'])     # 上腕（肘を張る）
    limb(arm_r, (26, 12, 22), (21, 13, 24), 3, C['leather'])   # 前腕（顎の下まで引く）
    arm_r[20:23, 12:15, 23:26] = C['leather_d']                # 引き手

    #! 弓。**輪郭を作るのはこれ**（人間の駒における盾に当たる）。
    #  握りを中心に、上下の弭（はず）が**射手の側＝ +x へ**反る。
    bow = np.zeros(BIG, np.uint8)
    for sign in (+1, -1):
        for i in range(40):
            u = i / 39.0
            z = int(round(21 + sign * 13 * u))
            x = int(round(5 + 4.2 * u * u))
            bow[x:x + 2, 18:20, z:z + 1] = C['bow']
            bow[x:x + 2, 19:20, z:z + 1] = C['bow_l']          # 前面だけ明るく
    bow[4:7, 17:21, 19:24] = C['leather_d']                    # 握り革
    # 弦。**弭から引き手まで**の 2 本（引き絞っている形）。
    limb(bow, (9, 19, 34), (21, 14, 24), 1, C['string'])
    limb(bow, (9, 19, 8), (21, 14, 24), 1, C['string'])

    arrow = np.zeros(BIG, np.uint8)                            # 番えた矢
    limb(arrow, (21, 14, 24), (2, 19, 21), 1, C['bow_l'])
    arrow[1:4, 18:21, 20:23] = C['steel_l']                    # 鏃
    arrow[20:23, 13:16, 23:26] = C['feather']                  # 矢羽

    quiver = np.zeros(BIG, np.uint8)                           # 背中の矢筒
    limb(quiver, (21, 12, 14), (23, 11, 24), 4, C['leather'])
    for dx, dz in ((0, 0), (2, 1), (-1, 2)):
        limb(quiver, (22 + dx, 11, 24 + dz), (23 + dx, 10, 28 + dz), 1, C['bow_l'])
        quiver[22 + dx:24 + dx, 9:12, 28 + dz:30 + dz] = C['feather']

    leg_l = np.zeros(BIG, np.uint8)                            # 踏ん張る（左右に開く）
    limb(leg_l, (13, 16, 13), (11, 15, 6), 3, C['trouser'])
    leg_l[9:13, 13:19, 3:6] = C['boot']
    leg_l[9:13, 17:19, 5:6] = C['boot_l']

    leg_r = np.zeros(BIG, np.uint8)
    limb(leg_r, (19, 16, 13), (21, 17, 6), 3, C['trouser'])
    leg_r[19:23, 15:21, 3:6] = C['boot']
    leg_r[19:23, 19:21, 5:6] = C['boot_l']

    return [crop('base', make_base()), crop('torso', t), crop('head', h),
            crop('arm_l', arm_l), crop('arm_r', arm_r),
            crop('bow', bow), crop('arrow', arrow), crop('quiver', quiver),
            crop('leg_l', leg_l), crop('leg_r', leg_r)]


META_ARCHER = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'torso', 'voxels': 'torso', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'torso', 'grounded': False,
     'pivot': [16.0, 17.0, 25.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_l', 'voxels': 'arm_l', 'parent': 'torso', 'grounded': False,
     'pivot': [11.0, 17.0, 22.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_r', 'voxels': 'arm_r', 'parent': 'torso', 'grounded': False,
     'pivot': [21.0, 17.0, 22.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'bow', 'voxels': 'bow', 'parent': 'arm_l', 'grounded': False,
     'pivot': [5.0, 19.0, 21.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # 矢は**引き手が親**。放つ動きを入れるならここが動く。
    {'name': 'arrow', 'voxels': 'arrow', 'parent': 'arm_r', 'grounded': False,
     'pivot': [21.0, 14.0, 24.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'quiver', 'voxels': 'quiver', 'parent': 'torso', 'grounded': False,
     'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_l', 'voxels': 'leg_l', 'parent': 'torso', 'grounded': False,
     'pivot': [13.0, 16.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'torso', 'grounded': False,
     'pivot': [19.0, 16.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
]


# ================================================================ レッド・ドラゴン
# **1 マスに収まらない相手。**翼は土台からはみ出す（当たり判定は円盤だけなので構わない）。
# 元絵は「ずんぐりした二足の赤竜・腹はクリーム・翼膜は暗い赤・黄色い目」。
FRONT_DRAGON = [
    '......dddddddd......',  # 34 頭の天
    '......dDDDDDDd......',  # 33
    '......DDDDDDDD......',  # 32
    '......DEEDDEED......',  # 31 目（**黄色は 2 ボクセルで足りる**）
    '......DDDDDDDD......',  # 30
    '.......DDDDDD.......',  # 29 鼻面
    '.......DYYYYD.......',  # 28 口
    '........DDDD........',  # 27 顎
    '........DDDD........',  # 26 首（**ここで括れないと頭が胴に埋まる**）
    '.......DDDDDD.......',  # 25 首の付け根
    '.....DDDDYYDDDD.....',  # 24 胸
    '....DDDDYYYYDDDD....',  # 23
    '...DDDDDYYYYDDDDD...',  # 22
    '...DDDDDYYYYDDDDD...',  # 21
    '...DDDDDYYYYDDDDD...',  # 20
    '...DDDDDYYYYDDDDD...',  # 19
    '...DDDDDYYYYDDDDD...',  # 18
    '...DDDDDYYYYDDDDD...',  # 17
    '....DDDDYYYYDDDD....',  # 16
    '....DDDDYYYYDDDD....',  # 15
    '.....DDDDyyDDDD.....',  # 14
    '.....DDDDDDDDDD.....',  # 13
    '.....DDDD..DDDD.....',  # 12 腿が分かれる
    '.....DDDD..DDDD.....',  # 11
    '....DDDDD..DDDDD....',  # 10
    '....DDDD....DDDD....',  # 9
    '....DDDD....DDDD....',  # 8
    '...DDDDD....DDDDD...',  # 7
    '...DDDDD....DDDDD...',  # 6
    '...CCCCC....CCCCC...',  # 5 爪（**白い爪が「竜」を言う**）
    '...CCCCC....CCCCC...',  # 4
    '...CCCCC....CCCCC...',  # 3
]

DEPTH_DRAGON = {
    'D': ('drag', 11, 22), 'd': ('drag_l', 11, 22), 'E': ('eye_y', 11, 22),
    'Y': ('belly', 11, 22), 'y': ('belly_d', 11, 22), 'C': ('claw', 11, 22),
}


def build_dragon():
    def kind(x, z, ch):
        if z >= 25:
            return 'head'
        if z < 13:
            return 'leg_l' if x < 16 else 'leg_r'
        return 'body'

    vols = zeros('head', 'body', 'leg_l', 'leg_r')
    raise_front(FRONT_DRAGON, DEPTH_DRAGON, 6, 34, kind, vols)
    h, b = vols['head'], vols['body']

    #! 腹は**前面だけ**にする（横や背まで回すと「白い竜」になる）。
    for z in range(13, 25):
        paint(b, np.s_[12:20, 11:17, z:z + 1], 'drag')
    b[9:23, 11:13, 13:25] = np.where(b[9:23, 11:13, 13:25] > 0, C['drag_d'], 0)  # 背は暗く
    b[9:23, 11:23, 23:25] = np.where(b[9:23, 11:23, 23:25] > 0, C['drag_l'], 0)  # 肩の天は明るく
    for z in range(14, 24, 3):                 # 腹の横筋（元絵の節）
        paint(b, np.s_[13:19, 21:22, z:z + 1], 'belly_d')

    h[12:20, 11:14, 25:35] = np.where(h[12:20, 11:14, 25:35] > 0, C['drag_d'], 0)  # 後頭
    # 頭の天は正面図の 'd' の 1 段だけにする。2 段を明るくしたら**頭が桃色の箱**になった。
    h[13:19, 21:22, 28:29] = C['dark']         # 口の線
    h[13:14, 21:22, 29:30] = C['dark']         # 鼻孔
    h[18:19, 21:22, 29:30] = C['dark']
    #! 角。**後ろへ倒す**（真上に立てると耳に見える）。
    for x0, dx in ((13, -1), (18, +1)):
        limb(h, (x0, 14, 33), (x0 + dx * 3, 10, 36), 2, C['horn_d'])
        limb(h, (x0 + dx * 3, 10, 36), (x0 + dx * 4, 9, 37), 1, C['horn'])

    #! 翼。**輪郭の主役。**前縁を引いてから、そこから体側へ膜を垂らす。
    #  膜を体より暗く沈めるのは、明るいと「布」に見えるため。
    wings = {}
    for name, sx in (('wing_l', -1), ('wing_r', +1)):
        w = np.zeros(BIG, np.uint8)
        sh = (16 + sx * 6, 13, 23)             # 付け根（肩）
        tip = (16 + sx * 17, 9, 37)            # 翼端
        limb(w, sh, tip, 2, C['drag_d'])       # 前縁の骨
        for i in range(1, 26):                 # 膜（前縁から体側へ垂らす）
            u = i / 25.0
            px = int(round(sh[0] + (tip[0] - sh[0]) * u))
            py = int(round(sh[1] + (tip[1] - sh[1]) * u))
            pz = int(round(sh[2] + (tip[2] - sh[2]) * u))
            bottom = int(round(23 - 11 * u * u))
            w[px:px + 1, py:py + 1, bottom:pz] = C['memb']
            if i % 7 == 0:                     # 指の骨（3 本）
                limb(w, (px, py, pz), (px, py, bottom), 1, C['memb_d'])
        wings[name] = w

    #! 前肢（小さい）と尾。
    arm_l = np.zeros(BIG, np.uint8)
    limb(arm_l, (10, 16, 21), (7, 19, 17), 3, C['drag'])
    arm_l[5:9, 19:22, 15:18] = C['claw']
    arm_r = np.zeros(BIG, np.uint8)
    limb(arm_r, (21, 16, 21), (24, 19, 17), 3, C['drag'])
    arm_r[23:27, 19:22, 15:18] = C['claw']

    tail = np.zeros(BIG, np.uint8)
    limb(tail, (15, 12, 14), (18, 7, 11), 5, C['drag'])
    limb(tail, (18, 7, 11), (23, 5, 13), 4, C['drag'])
    limb(tail, (23, 5, 13), (27, 7, 19), 3, C['drag_d'])
    limb(tail, (27, 7, 19), (28, 9, 23), 2, C['drag_l'])

    return [crop('base', make_base()), crop('body', b), crop('head', h),
            crop('wing_l', wings['wing_l']), crop('wing_r', wings['wing_r']),
            crop('arm_l', arm_l), crop('arm_r', arm_r),
            crop('leg_l', vols['leg_l']), crop('leg_r', vols['leg_r']), crop('tail', tail)]


META_DRAGON = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'body', 'voxels': 'body', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'body', 'grounded': False,
     'pivot': [16.0, 16.0, 26.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    # 翼は**羽ばたきを入れるならここ**（肩のピボットまわりの `pendulum`）。いまは静止。
    {'name': 'wing_l', 'voxels': 'wing_l', 'parent': 'body', 'grounded': False,
     'pivot': [10.0, 13.0, 23.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'wing_r', 'voxels': 'wing_r', 'parent': 'body', 'grounded': False,
     'pivot': [22.0, 13.0, 23.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_l', 'voxels': 'arm_l', 'parent': 'body', 'grounded': False,
     'pivot': [10.0, 16.0, 21.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_r', 'voxels': 'arm_r', 'parent': 'body', 'grounded': False,
     'pivot': [21.0, 16.0, 21.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_l', 'voxels': 'leg_l', 'parent': 'body', 'grounded': False,
     'pivot': [12.0, 16.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'body', 'grounded': False,
     'pivot': [20.0, 16.0, 13.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'tail', 'voxels': 'tail', 'parent': 'body', 'grounded': False,
     'pivot': [15.0, 12.0, 14.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
]


# ================================================================ ブラウン・イーク
# **小ささが読みの本体。**背丈 20 ボクセル（人間は 31）で、同じ土台に乗るから比較になる。
# 元絵は「茶色の毛むくじゃらの小動物・丸い耳と鼻先・黄色い目」（記憶の指摘どおり）。
FRONT_YEEK = [
    '..UU......UU..',  # 22 耳
    '..UU......UU..',  # 21
    '..UUFFFFFFUU..',  # 20 頭の天と耳の付け根
    '..FFFFFFFFFF..',  # 19
    '..FFEEFFEEFF..',  # 18 目
    '..FFFFNNFFFF..',  # 17 鼻
    '...FFFFFFFF...',  # 16 顎（**首は無い**。丸い頭が胴に乗る）
    '..FFFFFFFFFF..',  # 15 肩
    '.FFFFFFFFFFFF.',  # 14
    '.FFFFFFFFFFFF.',  # 13
    '.FFFFFFFFFFFF.',  # 12
    '.FFFFFFFFFFFF.',  # 11
    '.FFFFFFFFFFFF.',  # 10
    '..FFFFFFFFFF..',  # 9
    '..FFFFFFFFFF..',  # 8
    '..FFFF..FFFF..',  # 7 脚が分かれる
    '..FFFF..FFFF..',  # 6
    '..PPPP..PPPP..',  # 5 足
    '..PPPP..PPPP..',  # 4
    '..PPPP..PPPP..',  # 3
]

DEPTH_YEEK = {
    'F': ('fur', 12, 21), 'U': ('fur_d', 13, 20), 'E': ('eye_y', 12, 21),
    'N': ('nose', 12, 21), 'P': ('paw', 12, 21),
}


def build_yeek():
    def kind(x, z, ch):
        if z >= 16:
            return 'head'
        if z < 8:
            return 'leg_l' if x < 16 else 'leg_r'
        return 'body'

    vols = zeros('head', 'body', 'leg_l', 'leg_r')
    raise_front(FRONT_YEEK, DEPTH_YEEK, 9, 22, kind, vols)
    h, b = vols['head'], vols['body']

    #! 毛並み。**まだらにしない**（§6.2(c) の無作為ノイズと同じ理由で、汚れに見える）。
    #  明暗は**面の向きで**付ける——天は明るく、下腹は暗く。
    paint(h, np.s_[9:23, 12:21, 19:21], 'fur_l')   # 頭の天
    paint(b, np.s_[9:23, 12:21, 14:16], 'fur_l')   # 肩の天
    paint(b, np.s_[9:23, 12:21, 8:10], 'fur_d')    # 下腹
    paint(h, np.s_[9:23, 12:14, 16:21], 'fur_d')   # 後頭
    h[13:14, 20:21, 18:19] = C['dark']             # 瞳
    h[18:19, 20:21, 18:19] = C['dark']
    h[15:17, 20:21, 16:17] = C['fur_d']            # 口もと

    #! ポーズ: **怯えて手を挙げている。**イークは弱いので、そう見えるほうが正しい。
    arm_l = np.zeros(BIG, np.uint8)
    limb(arm_l, (11, 16, 14), (8, 19, 17), 3, C['fur'])
    arm_l[6:10, 18:21, 16:19] = C['paw']
    arm_r = np.zeros(BIG, np.uint8)
    limb(arm_r, (20, 16, 14), (23, 19, 17), 3, C['fur'])
    arm_r[21:25, 18:21, 16:19] = C['paw']

    tail = np.zeros(BIG, np.uint8)                 # 短い尻尾
    limb(tail, (16, 12, 9), (17, 8, 11), 3, C['fur_d'])

    return [crop('base', make_base()), crop('body', b), crop('head', h),
            crop('arm_l', arm_l), crop('arm_r', arm_r),
            crop('leg_l', vols['leg_l']), crop('leg_r', vols['leg_r']), crop('tail', tail)]


META_YEEK = [
    {'name': 'base', 'voxels': 'base', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'body', 'voxels': 'body', 'grounded': False, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'head', 'voxels': 'head', 'parent': 'body', 'grounded': False,
     'pivot': [16.0, 16.0, 16.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_l', 'voxels': 'arm_l', 'parent': 'body', 'grounded': False,
     'pivot': [11.0, 16.0, 14.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'arm_r', 'voxels': 'arm_r', 'parent': 'body', 'grounded': False,
     'pivot': [20.0, 16.0, 14.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_l', 'voxels': 'leg_l', 'parent': 'body', 'grounded': False,
     'pivot': [12.0, 16.0, 8.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'leg_r', 'voxels': 'leg_r', 'parent': 'body', 'grounded': False,
     'pivot': [20.0, 16.0, 8.0], 'motion': {'kind': 'static'}, 'wind_k': 0.0},
    {'name': 'tail', 'voxels': 'tail', 'parent': 'body', 'grounded': False,
     'motion': {'kind': 'static'}, 'wind_k': 0.0},
]


# ------------------------------------------------------------------------ 本体
FIGURES = (
    ('figure_human', build_human, META_HUMAN, '人間の冒険者（P0_0_0.png）。剣と盾'),
    ('figure_archer', build_archer, META_ARCHER, '見習アーチャー（R116）。弓を引き絞る'),
    ('figure_red_dragon', build_dragon, META_DRAGON, 'レッド・ドラゴン（R589）。翼は土台からはみ出す'),
    ('figure_yeek', build_yeek, META_YEEK, 'ブラウン・イーク（R141）。背丈は人間の 2/3'),
)


def self_check():
    """**検査の検査**（設計書 §7.2 / 罠 4）。わざと壊した入力に反応することを確かめる。

    「検出したいものを検出できない検査は、無いより悪い」。`--self-check` で走る。
    """
    ok = True
    good = ['..FF..', '.FFFF.', '..FF..']
    depth = {'F': ('skin', 13, 20)}

    def run(front, dep=depth):
        raise_front(front, dep, 8, 20, lambda x, z, ch: 'a', zeros('a'))

    try:                                   # 揃っている → 通る
        run(good)
    except Exception as exc:
        print('NG: 正しい正面図が落ちた: %s' % exc); ok = False
    for name, front in (('行が 1 文字短い', ['..FF..', '.FFFF', '..FF..']),
                        ('行が 1 文字長い', ['..FF..', '.FFFFF.', '..FF..'])):
        try:
            run(front)
            print('NG: %s のに検査が反応しなかった' % name); ok = False
        except ValueError:
            print('OK: %s → 落ちた' % name)
    try:                                   # 奥行き表に無い材
        run(['..FX..', '.FFFF.', '..FF..'])
        print('NG: 知らない材が黙って通った'); ok = False
    except KeyError:
        print('OK: 奥行き表に無い材 → 落ちた')
    print('self-check: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


def main():
    if '--self-check' in sys.argv:
        return self_check()
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
                             note='試作（検討用）。' + note)
        total = sum(int((v > 0).sum()) for _, v, _ in parts)
        top = max(o[2] + v.shape[2] for _, v, o in parts)
        print('%-18s パーツ %2d / ボクセル %5d / 背丈 %2d / vox %6d B / footprint %s'
              % (name, len(parts), total, top, size, fp))
    return 0


if __name__ == '__main__':
    sys.exit(main())
