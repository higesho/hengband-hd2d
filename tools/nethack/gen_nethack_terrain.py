# -*- coding: utf-8 -*-
"""NetHack の地形（壁・床・扉・階段・祭壇ほか）の原作のタイル（16x16）を、ボクセルのプレハブ（`.vox` ＋ `.jsonc`）にする（本番の形）。

決めたこと:
  D20 壁・床などのブロックは、原作のタイルの図案を元にボクセルのオブジェクトを作る。DCSS（`tools/dcss/gen_dcss_terrain.py`・`gen_dcss_objects.py`）と同じ考え方。
  D23 壁のブロックの側面と天面の煉瓦は「横の壁」の絵（煉瓦の正面）で統一する。迷宮の系統（通常・鉱山・ゲヘナ・ノックス・ソコバン）ごとに作る。
  D26（2026-10-28）原作の壁の特徴は、天面の縁に白・灰の短い柱が点線のように並ぶこと（柵のように見える）。ブロックの上面の縁から**柵を盛り上げ**、壁の向き
  （表示記号 11 種: 縦・横・角 4・十字・T 字 4）ごとに柵の位置を変えて作り分ける（`WALL_ARMS`・`rim_mask`・`build_wall_rim`）。系統 5 × 向き 11 = 55 個。
  D27（2026-10-28）交差（角 4・T 字 4・十字の 9 向き）の上面の中央に、原作の交差の絵の一番上の部分を立体にした頭を載せる（`junction_head`）。
  通常・ゲヘナ・ソコバンは縁の立った穴あきの台、鉱山・ノックスは手前に出っ張りのある丸みのある山。材の色は絵の光の当たった左の面の色。縁の柵は残す。
  D24 床の地の色は暗い青灰 RGB(40,44,56)。D25 タイルの絵の黒い縁取りは原作どおり残す。
  （2026-10-28）凹凸の量を約 1/3 に減らし、平らな部分を増やす。1 マス 32 ボクセル・深さ 0〜2 は変えず、**凹凸のある画素（出っ張り・へこみ）の数**を、DCSS と同じ判定
  （`dcss_vox.relief_levels`）の約 1/3 にする（`nh_relief_levels`・`RELIEF_KEEP`）。残りは基準の面になる。煉瓦の暗い目地（へこみ）は残るほうが煉瓦らしいので、
  明るい出っ張りから先に減らす（`RELIEF_PROUD_SHARE`）。DCSS の `relief_levels` を呼ぶ関数は、この道具の中で `gt.relief_levels` を差し替えて使う
  （DCSS のファイルは書き換えない。DCSS のブロックは変わらない）。床の点・祭壇の十字と滴り・罠の落とし戸の枠・墓の草・木の葉の明暗も同じ約 1/3。

**作ったプレハブ（`.vox`・`.jsonc`）はリポジトリで追跡しない**（原作のタイルの絵は NetHack General Public License の下にあり、プレハブはその派生物になる。
 の「原作のコードを持たず…ライセンスを避ける」に合わせ、組み立てのときに原作から作る）。出力先の既定は `assets/voxel/`（画面が読むのはそこだけ）で、
名前は必ず `nh_` で始まり、`.gitignore` の `assets/voxel/nh_*` で外してある。規則（地形の番号 -> プレハブの名前）は `gen_nethack_terrain_rules.py` が
`assets/voxel/terrain_prefabs.jsonc` へ書く（番号と名前だけで絵を含まない。こちらは追跡する）。

入力: `nethack/data/<言語>/tiles.png`・`tiles.json`（`tools/nethack/build_tiles.py` が原作〔`build-sources`〕から作る。追跡しない）。地形の絵は英語版と日本語版で同じ。

## 作る物（種類ごとの作り方）

| 種類 | 作り方（DCSS と同じ所は DCSS の関数を呼ぶ。DCSS の道具は書き換えず、読み込みの関数だけを差し替える） |
| --- | --- |
| 壁（wall）5 系統 × 向き 11 種 | **DCSS の `build_wall`** に**柵**を足す（`build_wall_rim`）。横の壁の絵を 1 マス 32 ボクセルのブロックの 4 側面へ写し（天面は柵の色と黒を地の色に替え、目地のへこみ 1 段だけにする。`wall_top`）、明るさの差で 3 段（出っ張り・基準・へこみ）の約 1 ボクセルの凹凸を付ける（凹凸のある画素は約 1/3 に減らす。上の 2026-10-28）。16x16 を最近傍で 2 倍にして渡す。その上面の縁に、壁が伸びない側の縁に沿って短い柱を点線のように並べ（太さ 4・高さ 4・周期 16）、角には角の柱を立てる。背は 32 → 36 |
| 床・通路・出入口（floor） | **この道具の `build_floor_dots`**。厚み 8 の板。原作の床の絵は透明な地に点が 1 個（通路は輪の点）なので、地を `FLOOR_BG` で埋め、地より明るい画素（点・刻み文字）を 1 ボクセル持ち上げ、暗い画素（暗い部屋の市松）はへこませる（どちらも約 1/3 に間引く）。縁は必ず基準の高さ |
| 水・溶岩・氷・空・雲・跳ね橋（ground） | **DCSS の `build_floor`**（液体は天面を 2 下げ、明るさで ±1）。透明な地は水なら深い青で埋める |
| 扉（door）3 状態 × 向き 2 | **DCSS の `build_door`**。枠は厚み 12、扉板は 4 引っ込め、明るさで ±1（凹凸のある画素は約 1/3）。開いた扉は透明な所と暗い所を抜く。罠付きの扉は閉じた扉の形 |
| 階段（stairs）8 種 | **一から作る**（2026-10-28。`build_stairs_up_nh`・`build_stairs_down_nh`）。絵は段の絵ではなく枠と矢印の記号なので、形は作り、色だけ絵から。上り = 床の上に 5 段が奥へ上がる物、下り = 床に穴を開けて奥へ 3 段下りる（床の板の中だけ）。どちらも空中に矢印（上り = 上向き・下り = 下向き）を浮かべる。分岐の階段は絵の色（黄・黄緑の矢印）が違うだけ |
| 梯子（ladder）4 種 | **一から作る**。絵の梯子は棒と横木の記号なので、上り = 2 本の縦棒と横木が立つ物、下り = 床に四角い穴を彫り、縁から棒が出て穴の中に横木が見える。色は絵（棒の色・横木の橙）から拾う |
| 祭壇（altar）5 種 | **一から作る**（`gen_dcss_objects.py` の祭壇と同じ考え方）。絵の祭壇は低い石の台に血痕などの赤い模様。三段の台の石の色を絵の主な色（無属性は黒・混沌は暗灰・中立は青灰・秩序は白・その他は桃）から、天面の十字と前面の滴りの色を絵の最も鮮やかな色から取る |
| 墓（grave）・玉座（throne）・流し（sink）・噴水（fountain）・木（tree）・鉄格子（bars） | **一から作る**。墓 = 土の盛り・丸い頭の墓石・彫り込んだ RIP の字。玉座 = 金の肘掛けと背もたれ・赤い座面と背の赤い面・青い十字。流し = 白い柱の上の水盤・水・2 つの蛇口。噴水 = 円い水盤・青い水・中央の柱と吹き上がる水。木 = 幹と 4 つの葉の塊。鉄格子 = 5 本の縦棒と上下の桟。色はどれも絵の画素から取る |
| 罠 24 種（trap） | 床に貼る浮き彫り（**DCSS の `build_decal`**。1〜3 ボクセルの厚み）。落とし穴・とげの穴・落とし戸・穴は床を彫る（**この道具の `build_pit_nh`**。縁の黒い枠は絵のまま、中は暗く、とげの穴は白いとげ 4 本） |
| 上がった跳ね橋（structure） | **DCSS の `build_wall`**（板を縦に並べた門の壁） |

**DCSS の道具へ差し替える所**（DCSS のファイルは書き換えない。`gen_dcss_terrain` のモジュールの名前を差し替える）: `dcss_vox.read_rgba`（NetHack のタイルを返す）・
`write_pair`（作った配列を `BUILT` へ控える。検査と撮影用の部屋が使う）・`lift_to`（暗い絵の明るさの持ち上げは外す。NetHack の絵は黒の縁取りが多く、持ち上げると
開いた扉が黄色に・階段が橙に飽和する）。

使い方:
  python tools/nethack/gen_nethack_terrain.py                  # 全部を assets/voxel/nh_*.vox・.jsonc へ（確認用は .taskwork/nethack_terrain2/out/view）
  python tools/nethack/gen_nethack_terrain.py nh_wall_main     # 名前を指定
  python tools/nethack/gen_nethack_terrain.py --out <フォルダ> --no-view
  python tools/nethack/gen_nethack_terrain.py --list
  python tools/nethack/gen_nethack_terrain.py --check          # 名前の取り違え・形（穴・浮き・縁の高さ）の検査。書かない（一時フォルダへ作る）
"""
import argparse
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'dcss'))

import numpy as np  # noqa: E402
from scipy import ndimage  # noqa: E402

import nh_tile_common as C  # noqa: E402
import dcss_vox as dv  # noqa: E402
import gen_dcss_terrain as gt  # noqa: E402
from dcss_vox import V, FLOOR_H, BURIED, Palette, Canvas, load_tile, shade, mix, lum  # noqa: E402

REPO = C.ROOT
WORK2 = os.path.join(REPO, '.taskwork', 'nethack_terrain2')
#: 出力先の既定。画面は `assets/voxel/` の 1 か所だけを読むので、ここへ置く（`nh_*` は .gitignore で外してある）。
DEFAULT_OUT = os.path.join(REPO, 'assets', 'voxel')
DEFAULT_VIEW = os.path.join(WORK2, 'out', 'view')
DEFAULT_REPORT = os.path.join(WORK2, 'out')

#: 床・階段の透明な地を埋める地の色（D24。原作の GUI 版は透明を黒で表示する。3D の明かりで黒が潰れるので暗い青灰にした）。
FLOOR_BG = (40, 44, 56)
#: 池の絵（透明な地に青い波線）の地を埋める深い青。
WATER_BG = (10, 20, 150)
Z0 = BURIED            # 物の見える底（これより下は地面に埋まる）

# ------------------------------------------------------------------ 壁の向きと柵（2026-10-28 D26）
#: 壁の系統: (名前, 横の壁の絵の番号, 絵の名前, 柵の柱の色, 柱の頭の色)。柱の色は**原作のタイルの上縁（上 8 行）にある白・灰の短い柱の色**
#: （通常・ソコバンは灰 RGB(215)〔ソコバンは水色〕＋白、ゲヘナは赤＋薄い桃、鉱山・ノックスは橙＋薄い桃）。`run_check` が絵にその色があることを確かめる。
WALL_SYSTEMS = [
    ('main', 1274, 'main walls horizontal', (215, 215, 215), (255, 255, 255)),
    ('mines', 1472, 'mines walls horizontal', (255, 108, 0), (255, 182, 145)),
    ('gehennom', 1483, 'gehennom walls horizontal', (255, 0, 0), (255, 182, 145)),
    ('knox', 1494, 'knox walls horizontal', (255, 108, 0), (255, 182, 145)),
    ('sokoban', 1505, 'sokoban walls horizontal', (0, 182, 255), (255, 255, 255)),
]
#: 壁の向き（表示記号 `S_vwall`〜`S_trwall`。`defsym.h` の順＝地形番号の順）-> 壁が伸びる向き（N 北・E 東・S 南・W 西）。
#: 原作 `rm.h` の図（TLCORNER = `+-` の下に `|`〔東と南へ伸びる〕ほか）どおり。**画面の北は +y の反対側**（`gt.build_wall` の南の面が y = V-1）。
WALL_ARMS = {
    'vwall': 'NS', 'hwall': 'EW',
    'tlcorn': 'ES', 'trcorn': 'WS', 'blcorn': 'NE', 'brcorn': 'NW',
    'crwall': 'NESW',
    'tuwall': 'NEW', 'tdwall': 'ESW', 'tlwall': 'NSW', 'trwall': 'NES',
}
WALL_SPECS = [('nh_wall_%s_%s' % (sysname, kind), 'wall', idx, expect, None, {'arms': arms, 'rim': (body, cap), 'sys': sysname, 'kind': kind})
              for sysname, idx, expect, body, cap in WALL_SYSTEMS for kind, arms in WALL_ARMS.items()]

# ------------------------------------------------------------------ 作る物の一覧
#: (プレハブ名, 種類, 絵の番号, 期待する絵の名前, 透明な所の扱い, 追加の指定)
#:  透明の扱い: None = 透明のまま / 'floor' = FLOOR_BG で埋める / 'water' = WATER_BG で埋める
#:  （閉じた扉の絵にも透明な帯がある〔板の継ぎ目〕。抜くと向こうが見えてしまうので、閉じた扉は埋める）
SPECS = [
    # 壁 5 系統 × 向き 11 種（D23: 側面は横の壁の絵で統一。D26: 上面の縁に柵を立て、壁の向きで柵の位置を変える。下の `WALL_SYSTEMS`・`WALL_ARMS`）
    *WALL_SPECS,
    # 床・通路・出入口
    ('nh_floor_room', 'floor', 1291, 'floor of a room', 'floor', {}),
    ('nh_floor_dark', 'floor', 1292, 'dark part of a room', 'floor', {}),
    ('nh_floor_room_engr', 'floor', 1293, 'engraving in a room', 'floor', {}),
    ('nh_floor_corridor', 'floor', 1294, 'corridor', 'floor', {}),
    ('nh_floor_corridor_lit', 'floor', 1295, 'lit corridor', 'floor', {}),
    ('nh_floor_corridor_engr', 'floor', 1296, 'engraving in a corridor', 'floor', {}),
    ('nh_floor_doorway', 'floor', 1284, 'no door', 'floor', {}),
    # 扉（南北に通る向き = 絵の「横」の扉、東西に通る向き `_ew` = 絵の「縦」の扉）
    ('nh_door_open', 'door', 1286, 'horizontal open door', None, {'mode': 'open', 'ew': False}),
    ('nh_door_open_ew', 'door', 1285, 'vertical open door', None, {'mode': 'open', 'ew': True}),
    ('nh_door_closed', 'door', 1288, 'horizontal closed door', 'floor', {'mode': 'closed', 'ew': False}),
    ('nh_door_closed_ew', 'door', 1287, 'vertical closed door', 'floor', {'mode': 'closed', 'ew': True}),
    ('nh_door_trapped', 'door', 1348, 'trapped door', 'floor', {'mode': 'closed', 'ew': False}),
    ('nh_door_trapped_ew', 'door', 1348, 'trapped door', 'floor', {'mode': 'closed', 'ew': True}),
    # 鉄格子・木
    ('nh_bars', 'bars', 1289, 'iron bars', None, {}),
    ('nh_tree', 'tree', 1290, 'tree', None, {}),
    # 階段・梯子（分岐の絵は色が違うだけ）
    ('nh_stairs_up', 'stairs_up', 1297, 'staircase up', 'floor', {}),
    ('nh_stairs_down', 'stairs_down', 1298, 'staircase down', 'floor', {}),
    ('nh_ladder_up', 'ladder_up', 1299, 'ladder up', None, {}),
    ('nh_ladder_down', 'ladder_down', 1300, 'ladder down', None, {}),
    ('nh_stairs_up_branch', 'stairs_up', 1301, 'branch staircase up', 'floor', {}),
    ('nh_stairs_down_branch', 'stairs_down', 1302, 'branch staircase down', 'floor', {}),
    ('nh_ladder_up_branch', 'ladder_up', 1303, 'branch ladder up', None, {}),
    ('nh_ladder_down_branch', 'ladder_down', 1304, 'branch ladder down', None, {}),
    # 祭壇 5 種（glyph が持つ属性。unaligned・chaotic・neutral・lawful・other）
    ('nh_altar_unaligned', 'altar', 1305, 'unaligned altar', None, {}),
    ('nh_altar_chaotic', 'altar', 1306, 'chaotic altar', None, {}),
    ('nh_altar_neutral', 'altar', 1307, 'neutral altar', None, {}),
    ('nh_altar_lawful', 'altar', 1308, 'lawful altar', None, {}),
    ('nh_altar_other', 'altar', 1309, 'other altar', None, {}),
    ('nh_grave', 'grave', 1310, 'grave', None, {}),
    ('nh_throne', 'throne', 1311, 'throne', None, {}),
    ('nh_sink', 'sink', 1312, 'sink', None, {}),
    ('nh_fountain', 'fountain', 1313, 'fountain', None, {}),
    # 水・氷・溶岩・空・雲・跳ね橋
    ('nh_pool', 'ground', 1314, 'pool', 'water', {'liquid': True}),
    ('nh_ice', 'ground', 1315, 'ice', 'floor', {'style': 'stone'}),
    ('nh_lava', 'ground', 1316, 'molten lava', None, {'liquid': True}),
    ('nh_lava_wall', 'ground', 1317, 'wall of lava', None, {'liquid': True}),
    ('nh_air', 'ground', 1322, 'air', None, {'style': 'stone'}),
    ('nh_cloud', 'ground', 1323, 'cloud', None, {'style': 'stone'}),
    ('nh_water', 'ground', 1324, 'water', None, {'liquid': True}),
    ('nh_bridge_open_v', 'ground', 1318, 'vertical open drawbridge', None, {'style': 'stone'}),
    ('nh_bridge_open_h', 'ground', 1319, 'horizontal open drawbridge', None, {'style': 'stone'}),
    ('nh_bridge_raised_v', 'wall', 1320, 'vertical closed drawbridge', None, {}),
    ('nh_bridge_raised_h', 'wall', 1321, 'horizontal closed drawbridge', None, {}),
    # 罠 24 種（床に貼る浮き彫り。穴の 4 種は床を彫る）
    ('nh_trap_arrow', 'decal', 1325, 'arrow trap', None, {}),
    ('nh_trap_dart', 'decal', 1326, 'dart trap', None, {}),
    ('nh_trap_falling_rock', 'decal', 1327, 'falling rock trap', None, {}),
    ('nh_trap_squeaky_board', 'decal', 1328, 'squeaky board', None, {}),
    ('nh_trap_bear', 'decal', 1329, 'bear trap', None, {}),
    ('nh_trap_land_mine', 'decal', 1330, 'land mine', None, {}),
    ('nh_trap_rolling_boulder', 'decal', 1331, 'rolling boulder trap', None, {}),
    ('nh_trap_sleeping_gas', 'decal', 1332, 'sleeping gas trap', None, {}),
    ('nh_trap_rust', 'decal', 1333, 'rust trap', None, {}),
    ('nh_trap_fire', 'decal', 1334, 'fire trap', None, {}),
    ('nh_trap_pit', 'pit', 1335, 'pit', None, {'mode': 'pit'}),
    ('nh_trap_spiked_pit', 'pit', 1336, 'spiked pit', None, {'mode': 'spiked'}),
    ('nh_trap_hole', 'pit', 1337, 'hole', None, {'mode': 'hole'}),
    ('nh_trap_trapdoor', 'pit', 1338, 'trap door', None, {'mode': 'trapdoor'}),
    ('nh_trap_teleport', 'decal', 1339, 'teleportation trap', None, {}),
    ('nh_trap_level_teleporter', 'decal', 1340, 'level teleporter', None, {}),
    ('nh_trap_magic_portal', 'decal', 1341, 'magic portal', None, {}),
    ('nh_trap_web', 'decal', 1342, 'web', None, {}),
    ('nh_trap_statue', 'decal', 1343, 'statue trap', None, {}),
    ('nh_trap_magic', 'decal', 1344, 'magic trap', None, {}),
    ('nh_trap_anti_magic', 'decal', 1345, 'anti-magic field', None, {}),
    ('nh_trap_polymorph', 'decal', 1346, 'polymorph trap', None, {}),
    ('nh_trap_vibrating_square', 'decal', 1347, 'vibrating square', None, {}),
    ('nh_trap_chest', 'decal', 1349, 'trapped chest', None, {}),
]
NAMES = [s[0] for s in SPECS]
#: 床の板として作る（原点の z が -FLOOR_H。確認用の出力では床の厚みだけ持ち上げる）種類
GROUND_KINDS = ('floor', 'ground', 'stairs_down', 'ladder_down', 'pit')

_SHEET = None
_TILES = {}          # 鍵 -> 32x32 の RGBA（16x16 を最近傍で 2 倍）
BUILT = {}           # 作った物 -> (parts, {索引: rgb}, {索引: 材質})。検査と撮影用の部屋が使う
ORIG_WRITE_PAIR = dv.write_pair


def _capture_write_pair(name, parts, pal, note, extra_parts=None):
    BUILT[name] = ([(n, v.copy(), o) for n, v, o in parts], dict(pal.table()), dict(pal.materials()))
    return ORIG_WRITE_PAIR(name, parts, pal, note, extra_parts)


# DCSS の道具は `from dcss_vox import write_pair` で結んでいるので、gt 側の名前も差し替える（DCSS のファイルは書き換えない）
dv.write_pair = _capture_write_pair
gt.write_pair = _capture_write_pair


def set_data_dir(path):
    """原作のタイルの置き場を替える（既定は `nethack/data/en`）。"""
    global _SHEET
    C.DATA_EN = path
    _SHEET = None
    _TILES.clear()


def _sheet():
    global _SHEET
    if _SHEET is None:
        _SHEET = C.load_sheet(C.DATA_EN)
    return _SHEET


def register(idx, bg=None):
    """絵の番号 idx を DCSS の道具が読める鍵（'nh:番号:扱い'）で登録して鍵を返す。32x32（最近傍で 2 倍）。"""
    key = 'nh:%d:%s' % (idx, bg)
    if key not in _TILES:
        t = C.upscale(C.tile_rgba(_sheet(), idx), 2)
        if bg is not None:
            rgb = {'floor': FLOOR_BG, 'water': WATER_BG}[bg]
            t = t.copy()
            m = t[..., 3] == 0
            t[m, :3] = rgb
            t[m, 3] = 255
        _TILES[key] = t
    return key


def nh_read_rgba(rel):
    """`dcss_vox.read_rgba`（rltiles の PNG を読む）の代わり。鍵 'nh:...' なら NetHack のタイルを返す。"""
    return _TILES[rel].copy()


dv.read_rgba = nh_read_rgba


def _no_lift(rgb, opaque, target_mean):
    """DCSS の `lift_to`（暗すぎる絵の明るさを掛け算で持ち上げる）の代わり。原作の色のまま使う（上の説明）。"""
    return rgb


gt.lift_to = _no_lift          # build_door・build_stairs_down・build_decal が呼ぶ（gt の名前だけ差し替える）


# ------------------------------------------------------------------ 凹凸のある画素を減らす（2026-10-28）
#: 決めたこと「凹凸の量を 1/3 に。今よりフラットな部分を増やして」。深さ（0〜2 ボクセル）と 1 マスのボクセル数（32）は変えず、
#: **凹凸のある画素（出っ張り・へこみ）の数を、DCSS と同じ判定（`dcss_vox.relief_levels`）の結果の約 1/3 にする**。残りは基準の面（平ら）になる。
RELIEF_KEEP = 1.0 / 3.0
#: 残す凹凸のうち出っ張りの割合の上限。煉瓦の目地のような**暗い継ぎ目（へこみ）は残るほうが煉瓦らしい**ので、明るい出っ張りから先に減らす
#: （残すのは出っ張り 2 割・へこみ 8 割。へこみが足りないときだけ出っ張りで埋める）。
RELIEF_PROUD_SHARE = 0.2


def _hash2(y, x):
    return ((int(y) * 73856093) ^ (int(x) * 19349663)) & 0xFFFF


def thin_mask(mask, score, k, mode):
    """mask（bool の 2 次元）の真の画素を、約 k 個だけ残した mask を返す。score は大きいほど優先（強い凹凸・明るい点）。
    mode:
      'strength' … score の大きい順に画素を残す（同点は座標の並びで安定）。
      'compact'  … score の大きい順、同点は塊の中心に近い順（点・ごく小さい塊を中心から縮める）。同点の組はまとめて残す・外す（左右対称に縮む）。
      'patches'  … 塊（8 近傍でつながった画素の集まり）ごとに残す・外す（市松の暗い升・刻み文字の小片を、形を保ったまま間引く）。
                    塊が少ない・個数が合わないときは 'compact' で縮める。"""
    n = int(mask.sum())
    if n == 0 or k >= n:
        return mask.copy()
    if k <= 0:
        return np.zeros_like(mask)
    lab, nc = ndimage.label(mask, structure=np.ones((3, 3)))
    idx = list(range(1, nc + 1))
    cents = ndimage.center_of_mass(mask, lab, idx)
    if mode == 'patches' and nc >= 6:
        sizes = ndimage.sum(mask, lab, idx)
        means = ndimage.mean(score, lab, idx)
        order = sorted(range(nc), key=lambda i: (-round(float(means[i]), 3), _hash2(cents[i][0], cents[i][1])))
        out = np.zeros_like(mask)
        tot = 0
        for i in order:
            if abs(tot + sizes[i] - k) <= abs(tot - k):
                out |= lab == (i + 1)
                tot += int(sizes[i])
            else:
                break
        if 0.6 * k <= tot <= 1.4 * k:
            return out
        mode = 'compact'
    ys, xs = np.nonzero(mask)
    sc = np.round(score[ys, xs].astype(float), 3)
    if mode == 'strength':
        key = [(-sc[i], _hash2(ys[i], xs[i])) for i in range(len(ys))]
    else:
        cy = np.array([cents[lab[y, x] - 1][0] for y, x in zip(ys, xs)])
        cx = np.array([cents[lab[y, x] - 1][1] for y, x in zip(ys, xs)])
        dist = np.round(np.hypot(ys - cy, xs - cx), 2)
        key = [(-sc[i], dist[i]) for i in range(len(ys))]
    order = sorted(range(len(ys)), key=lambda i: key[i])
    out = np.zeros_like(mask)
    tot = 0
    i = 0
    while i < len(order):
        j = i
        while j < len(order) and (mode == 'strength' and j == i or mode != 'strength' and key[order[j]] == key[order[i]]):
            j += 1
        if abs(tot + (j - i) - k) <= abs(tot - k):
            for q in order[i:j]:
                out[ys[q], xs[q]] = True
            tot += j - i
        else:
            break
        i = j
    return out


def _drop_isolated(lev, wrap):
    """1 画素だけ孤立した凹凸を均す（`dcss_vox.relief_levels` と同じ）。"""
    k4 = np.array([[0, 1, 0], [1, 0, 1], [0, 1, 0]])
    for lv in (0, 2):
        m = lev == lv
        n4 = ndimage.convolve(m.astype(int), k4, mode='wrap' if wrap else 'constant')
        lev[m & (n4 == 0)] = 1
    return lev


def nh_relief_levels(rgb, wrap=True, thr=0.12):
    """`dcss_vox.relief_levels`（0 = 出っ張り・1 = 基準・2 = へこみ）と同じ判定をしてから、凹凸のある画素を約 `RELIEF_KEEP` に減らす。
    周りとの明るさの差が大きい画素（くっきりした目地・いちばん明るい面）から残し、出っ張りは `RELIEF_PROUD_SHARE` まで。
    孤立した 1 画素の凹凸は均すので、数が減る分は残す数を少し増やして、均した後の数が目標に届くようにする。"""
    old = dv.relief_levels(rgb, wrap=wrap, thr=thr)
    l = dv.lum(rgb)
    local = ndimage.uniform_filter(l, 5, mode='wrap' if wrap else 'nearest')
    s = (l - local) / max(np.median(l), 30.0)               # 正 = 周りより明るい、負 = 暗い
    up_m, dn_m = old == 0, old == 2
    n_up, n_dn = int(up_m.sum()), int(dn_m.sum())
    target = int(round((n_up + n_dn) * RELIEF_KEEP))
    lev = np.ones(old.shape, int)
    if target == 0:
        return lev
    for factor in np.arange(1.0, 2.01, 0.05):
        t = min(int(round(target * factor)), n_up + n_dn)
        k_up = min(n_up, int(round(t * RELIEF_PROUD_SHARE)))
        k_dn = min(n_dn, t - k_up)
        k_up = min(n_up, t - k_dn)
        lev = np.ones(old.shape, int)
        lev[thin_mask(up_m, s, k_up, 'strength')] = 0
        lev[thin_mask(dn_m, -s, k_dn, 'strength')] = 2
        lev = _drop_isolated(lev, wrap)
        if int((lev != 1).sum()) >= target:
            break
    return lev


gt.relief_levels = nh_relief_levels     # build_wall・build_door・build_floor・build_decal が呼ぶ（gt の名前だけ差し替える。DCSS のファイルは書き換えない）


# ------------------------------------------------------------------ 絵から色を拾う（16x16 の元の絵）
def tile_colours(idx, box=None, n=4, skip_black=True):
    """元の絵（または (x0,y0,x1,y1)）の不透明な画素の色を、多い順に n 色返す（黒は既定で除く）。"""
    t = C.tile_rgba(_sheet(), idx)
    if box:
        x0, y0, x1, y1 = box
        t = t[y0:y1, x0:x1]
    px = t[t[..., 3] > 0][:, :3]
    if skip_black:
        px = px[px.sum(axis=1) > 24]
    if px.size == 0:
        return [(128, 128, 128)] * n
    cols, cnt = np.unique(px, axis=0, return_counts=True)
    order = np.argsort(-cnt, kind='stable')
    out = [tuple(int(v) for v in cols[i]) for i in order[:n]]
    while len(out) < n:
        out.append(out[-1])
    return out


def vivid(cols):
    """色の並びから最も鮮やかな（彩度 x 明度が最大の）色を返す。"""
    return max(cols, key=lambda c: dv.hsv(c)[0] * dv.hsv(c)[1])


# ------------------------------------------------------------------ 床・通路（透明な地に点の絵）
def build_floor_dots(name, key, what):
    img, opaque = load_tile(key, name, max_colours=32)
    pal = Palette(gt.mat_stone)
    h = FLOOR_H
    base = h - 2
    bgc = np.array(FLOOR_BG)
    lum_bg = float(bgc @ np.array([0.299, 0.587, 0.114]))
    lm = img @ np.array([0.299, 0.587, 0.114])
    # 地より明るい画素（点・十字・刻み文字）は 1 ボクセル持ち上げ、暗い画素（暗い部屋の市松の黒・出入口の黒い縁）は 1 ボクセルへこませる
    d = np.where(lm > lum_bg + 8, 1, np.where(lm < lum_bg - 8, -1, 0))
    # 凹凸のある画素は約 1/3 に減らす（`RELIEF_KEEP`。点は中心を残して小さく、市松・刻み文字は形を保ったまま間引く）
    for sign, score in ((1, lm - lum_bg), (-1, lum_bg - lm)):
        m = d == sign
        keep = thin_mask(m, score, int(round(int(m.sum()) * RELIEF_KEEP)), 'patches')
        d[m & ~keep] = 0
    d[0, :] = d[-1, :] = 0
    d[:, 0] = d[:, -1] = 0                           # 縁は基準の高さ（隣のマスと継ぎ目で段差を出さない）
    side = pal(shade(FLOOR_BG, 0.75))
    v = np.zeros((V, V, h), np.int16)
    for y in range(V):
        for x in range(V):
            zt = base + int(d[y, x])
            v[x, y, :zt] = side
            v[x, y, zt] = pal(tuple(img[y, x]))
    size, ncol, nfp = _capture_write_pair(name, [('main', v, (0, 0, -h))], pal,
                                          'NetHack の地面 %s。透明な地を FLOOR_BG で埋め、地より明るい画素を 1 ボクセル持ち上げ、暗い画素を 1 ボクセルへこませた（どちらも元の約 1/3 に間引いた。tools/nethack/gen_nethack_terrain.py）' % what)
    return '%dx%dx%d' % v.shape, ncol, size, '出 %d へこみ %d' % (int((d > 0).sum()), int((d < 0).sum())), nfp


# ------------------------------------------------------------------ 階段（上り）
# ------------------------------------------------------------------ 階段（2026-10-28。段を一から作り、空中に矢印を浮かべる）
#: 遊ぶ人「階段がおかしいね」→「段を一から作る」「立体の階段に空中に浮かんだ矢印をつけよう」。原作の階段の絵は段の絵ではなく、
#: 茶の枠・赤と橙の横縞・白（分岐は黄と黄緑）の矢印の記号なので、絵を貼ると階段に見えなかった。形は一から作り、色だけ絵から拾う:
#:   上り = 床の上に 5 段（1 段の高さ 3・奥行き 4）が奥（北）へ上がる。蹴込み（手前を向く面）は赤と橙を交互、段の上面は矢印の色、横は茶。
#:   下り = 床に四角い穴を開け、奥へ 3 段下りる（床の板の厚さの中だけ。床より下へは彫らない）。段の上面は赤と橙を交互、段の先の縁は矢印の色、穴の縁は茶。
#:   矢印 = マスの上の空中（床から 1 マス強。立っている人の頭より上）に、上りは上向き・下りは下向きの平たい矢印（三角の頭と軸。厚み 3）。色は絵の矢印の色（明るい色を頭、次の色を軸）。
#:     接地しない別の部品にして、縦の軸でゆっくり回す（`motion` の `rotate`）。切り替えの名前 `stair_arrows`（機能メニューの「階段の矢印」で消せる）。地形の本体（最初の部品）は今までどおり浮かず、下り階段は床より上に出ない。
STAIR_ARROW_LIFT = 34   # 矢印の下端の床からの高さ（ボクセル。立っている人・怪物の板〔1 マス = 32〕の頭より上）
STAIR_ARROW_H = 13      # 矢印の高さ（軸 6・頭 7）


def _stair_colours(idx):
    """(茶の枠, 赤, 橙, 矢印の頭の色, 矢印の軸の色)。矢印は茶・赤・橙系・青灰を除いた明るい色の上位 2 つ。"""
    cols = tile_colours(idx, n=8)
    frame = (145, 71, 0)
    warm = {(145, 71, 0), (204, 79, 0), (255, 0, 0), (255, 108, 0), (108, 145, 182)}
    arrow = [c for c in dict.fromkeys(cols) if c not in warm]
    arrow.sort(key=lambda c: -lum(np.array(c, float)))
    head = arrow[0] if arrow else (255, 255, 255)
    shaft = arrow[1] if len(arrow) > 1 else shade(head, 0.8)
    return frame, (255, 0, 0), (255, 108, 0), head, shaft


STAIR_ARROW_SPIN = 0.25  # 矢印の回る速さ（1 秒あたりの回転数。縦の軸）


def _stair_arrow(pal, up, head, shaft):
    """マスの中央の空中に浮かべる平たい矢印（三角の頭と四角い軸。厚み 3）の部品。配列は [x, y, z]（高さ STAIR_ARROW_H）。
    縦の軸で回すので、正面では矢印の形・横からでは細い板に見え、回っているのが分かる。"""
    a = np.zeros((V, V, STAIR_ARROW_H), np.int16)
    cx = V // 2
    ys = slice(cx - 1, cx + 2)                      # 厚み 3（y）
    head_h = 7
    shaft_h = STAIR_ARROW_H - head_h
    for k in range(head_h):                         # k = 0 が頭の付け根（いちばん広い）
        half = 7 - k
        z = shaft_h + k if up else head_h - 1 - k
        a[cx - half:cx + half, ys, z] = pal(head if k else shade(head, 0.85))
    z0, z1 = (0, shaft_h) if up else (head_h, STAIR_ARROW_H)
    a[cx - 2:cx + 2, ys, z0:z1] = pal(shaft)
    return a


def _stair_parts(main, main_origin, arrow, arrow_z):
    """(部品, 部品の定義)。本体は接地する静止の部品、矢印は接地しない・縦の軸で回る部品（地形の本体の検査は本体だけを見る）。"""
    parts = [('main', main, main_origin), ('arrow', arrow, (0, 0, arrow_z))]
    defs = [
        {'name': 'main', 'voxels': 'main', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0},
        {'name': 'arrow', 'voxels': 'arrow', 'grounded': False,
         'pivot': [V / 2.0, V / 2.0, arrow_z + STAIR_ARROW_H / 2.0],
         'motion': {'kind': 'rotate', 'axis': 'z', 'speed': STAIR_ARROW_SPIN, 'phase': 0.0}, 'wind_k': 0.0,
         'toggle': 'stair_arrows'},  # 機能メニューの「階段の矢印」で消せる（PrefabPart::toggle）
    ]
    return parts, defs


def build_stairs_up_nh(name, idx, what):
    """床の上に置く 5 段の上り階段（奥へ上がる）と、空中で回る上向きの矢印。"""
    frame, red, orange, head, shaft = _stair_colours(idx)
    pal = Palette(gt.mat_stone)
    steps, rise, run = 5, 3, 4
    x0, x1 = 7, V - 7
    front = V - 5                                   # 手前の段の手前の端（y の上限。この 1 つ手前まで）
    v = np.zeros((V, V, Z0 + rise * steps), np.int16)
    body = pal(shade(frame, 0.8))
    for k in range(steps):
        y_hi = front - k * run
        y_lo = y_hi - run
        top = Z0 + rise * (k + 1)
        v[x0:x1, y_lo:y_hi, 0:top] = body
        v[x0:x1, y_hi - 1, Z0 + rise * k:top] = pal(red if k % 2 == 0 else orange)    # 蹴込み（手前を向く面）
        v[x0:x1, y_lo:y_hi, top - 1] = pal(head)                                          # 段の上面
        v[x0, y_lo:y_hi, 0:top - 1] = pal(frame)                                          # 横（茶）
        v[x1 - 1, y_lo:y_hi, 0:top - 1] = pal(frame)
    parts, defs = _stair_parts(v, (0, 0, 0), _stair_arrow(pal, True, head, shaft), Z0 + STAIR_ARROW_LIFT)
    size, ncol, nfp = _capture_write_pair(name, parts, pal,
                                          'NetHack の上り階段 %s。段を一から作り（5 段・奥へ上がる）、空中で回る上向きの矢印を浮かべた。色は絵から（tools/nethack/gen_nethack_terrain.py）' % what,
                                          extra_parts=defs)
    return '%dx%dx%d' % v.shape, ncol, size, '段の最高 %d' % (Z0 + rise * steps), nfp


def build_stairs_down_nh(name, idx, what):
    """床に四角い穴を開け、奥へ 3 段下りる下り階段（床の板の中だけ）と、空中で回る下向きの矢印。床の板を自分で含む。"""
    frame, red, orange, head, shaft = _stair_colours(idx)
    pal = Palette(gt.mat_stone)
    h = FLOOR_H
    base = h - 2                                    # 床の天面の層（下り梯子と同じ）
    v = np.zeros((V, V, h), np.int16)
    side = pal(shade(FLOOR_BG, 0.75))
    lo_x, hi_x, lo_y, hi_y = 7, V - 7, 6, V - 6
    v[:, :, :base] = side
    v[:, :, base] = pal(FLOOR_BG)
    v[lo_x - 2:hi_x + 2, lo_y - 2:hi_y + 2, base] = pal(frame)                           # 穴の縁（茶の枠）
    bands = [(hi_y - 6, hi_y, base - 2), (hi_y - 13, hi_y - 6, base - 4), (lo_y, hi_y - 13, base - 6)]
    for n, (y_lo, y_hi, top) in enumerate(bands):
        v[lo_x:hi_x, y_lo:y_hi, top + 1:] = 0
        v[lo_x:hi_x, y_lo:y_hi, :top] = side
        v[lo_x:hi_x, y_lo:y_hi, top] = pal(red if n % 2 == 0 else orange)               # 段の上面
        v[lo_x:hi_x, y_hi - 1, top] = pal(head)                                           # 段の先の縁（手前の端）
    parts, defs = _stair_parts(v, (0, 0, -h), _stair_arrow(pal, False, head, shaft), (base - h + 1) + STAIR_ARROW_LIFT)
    size, ncol, nfp = _capture_write_pair(name, parts, pal,
                                          'NetHack の下り階段 %s。床に穴を開けて段を一から作り（3 段・奥へ下りる）、空中で回る下向きの矢印を浮かべた。色は絵から（tools/nethack/gen_nethack_terrain.py）' % what,
                                          extra_parts=defs)
    return '%dx%dx%d' % v.shape, ncol, size, '段の最深 %d' % (base - bands[-1][2]), nfp


# ------------------------------------------------------------------ 穴（床を彫る）
def build_pit_nh(name, key, idx, what, mode):
    """落とし穴・とげの穴・落とし戸（四角い穴）・穴（絵の黒い染みの形の穴）。床の板を彫る。縁の黒い枠は絵のとおり（D25）。"""
    pal = Palette(gt.mat_stone)
    h = FLOOR_H
    base = h - 2
    side = pal(shade(FLOOR_BG, 0.75))
    ring_col = (0, 0, 0)
    if mode == 'hole':
        img, opaque = load_tile(key, name, max_colours=64)
        hole = ndimage.binary_dilation(opaque, iterations=1)
        hole &= ndimage.binary_erosion(np.ones((V, V), bool), iterations=3)
        dist = ndimage.distance_transform_edt(np.pad(hole, 1, constant_values=False))[1:-1, 1:-1]
        depth = np.where(hole, np.minimum(4, 1 + (dist * 1.5).astype(int)), 0)
        wall_col, bottom_col = shade(FLOOR_BG, 0.9), (6, 6, 10)
    else:
        load_tile(key, name, max_colours=64)                       # 使った絵として記録するだけ
        lo, hi = (8, 24) if mode != 'trapdoor' else (6, 26)
        hole = np.zeros((V, V), bool)
        hole[lo:hi, lo:hi] = True
        depth = np.where(hole, 4, 0)
        wall_col = (74, 98, 124)                                      # 絵の穴の側面の青灰（RGB 108,145,182 を暗くした）
        bottom_col = shade(FLOOR_BG, 0.55) if mode != 'trapdoor' else (4, 4, 8)
    ring = ndimage.binary_dilation(hole, iterations=2) & ~hole
    wood = [(145, 71, 0), (204, 79, 0)]
    v = np.zeros((V, V, h), np.int16)
    for y in range(V):
        for x in range(V):
            if hole[y, x]:
                zt = base - int(depth[y, x])
                v[x, y, :zt] = side
                v[x, y, zt] = pal(bottom_col)
            elif ring[y, x]:
                if mode == 'trapdoor':
                    zt = base + (1 if ((x // 4) + (y // 4)) % 3 == 0 else 0)   # 木の枠は 3 区画に 1 つだけ床の点と同じ高さまで出す（出っ張りを約 1/3 に）
                    col = wood[(x // 4) % 2]
                else:
                    zt = base
                    col = ring_col
                v[x, y, :zt] = pal(wall_col)
                v[x, y, zt] = pal(col)
            else:
                v[x, y, :base] = side
                v[x, y, base] = pal(FLOOR_BG)
    if mode == 'spiked':                                               # 絵の白い 4 点: 2x2 のとげ（高さ 4 の角錐）
        white = (237, 237, 237)
        for sx in (12, 18):
            for sy in (12, 18):
                zb = base - 4
                for k in range(4):
                    r = 1 if k < 2 else 0
                    v[sx - r:sx + 1 + r, sy - r:sy + 1 + r, zb + 1 + k] = pal(shade(white, 1.0 - 0.12 * (3 - k)))
    size, ncol, nfp = _capture_write_pair(name, [('main', v, (0, 0, -h))], pal,
                                          'NetHack の穴 %s（%s）。床の板を彫り、縁に黒い枠を残した（tools/nethack/gen_nethack_terrain.py）' % (what, mode))
    return '%dx%dx%d' % v.shape, ncol, size, '穴 %d' % int(hole.sum()), nfp


# ------------------------------------------------------------------ 一から作る物（祭壇・梯子ほか）
def new_canvas(h):
    pal = Palette(gt.mat_any)
    return Canvas(V, V, h, pal), pal


def finish(name, c, pal, note, origin=(0, 0, 0)):
    size, ncol, nfp = _capture_write_pair(name, [('main', c.a, origin)], pal, note)
    return '%dx%dx%d' % c.a.shape, ncol, size, '', nfp


def build_altar(name, idx, what):
    """低い三段の石の台。石の色と天面の十字・前面の滴りの色は絵から（絵の祭壇は台に赤などの模様）。"""
    cols = tile_colours(idx, n=6)
    body = cols[0]
    others = [c for c in cols[1:] if c != body]
    accent = vivid(others) if others else (255, 0, 0)
    c, pal = new_canvas(Z0 + 14)
    c.box(3, 7, 0, 29, 25, Z0 + 3, shade(body, 0.7))                           # 下の台（埋まる部分を含む）
    c.box(3, 24, Z0, 29, 25, Z0 + 1, shade(body, 0.45))                        # 前縁の影
    c.box(5, 9, Z0 + 3, 27, 23, Z0 + 8, body)                                  # 台の本体
    c.box(4, 8, Z0 + 8, 28, 24, Z0 + 10, shade(body, 1.12) if lum(np.array(body, float)) < 200 else shade(body, 0.92))   # 天板
    # 天面の十字（血痕・模様）と前面の滴り。浮き彫りの量を約 1/3 にする: 十字は幅 4 の面を天板に塗り（平ら）、出っ張るのは中心線（幅 1）だけ。滴りは前面に塗るだけ（出さない）
    c.box(14, 11, Z0 + 9, 18, 21, Z0 + 10, accent)
    c.box(10, 14, Z0 + 9, 22, 18, Z0 + 10, accent)
    c.box(16, 11, Z0 + 10, 17, 21, Z0 + 11, accent)
    c.box(10, 16, Z0 + 10, 22, 17, Z0 + 11, accent)
    for x in (9, 16, 23):
        c.box(x, 22, Z0 + 4, x + 2, 23, Z0 + 9, accent)
    return finish(name, c, pal, 'NetHack の祭壇 %s を絵から解釈して一から作った（三段の台・天面の十字・前面の滴り。tools/nethack/gen_nethack_terrain.py）' % what)


FONT = {
    'R': ('XX.', 'X.X', 'XX.', 'X.X', 'X.X'),
    'I': ('XXX', '.X.', '.X.', '.X.', 'XXX'),
    'P': ('XX.', 'X.X', 'XX.', 'X..', 'X..'),
}


def build_grave(name, idx, what):
    """土の盛りと、丸い頭の墓石に彫り込んだ RIP。色は絵（石の青灰・草の緑）から。"""
    cols = tile_colours(idx, n=5)
    stone = (108, 145, 182) if (108, 145, 182) in cols else cols[0]
    grass = (0, 145, 0) if (0, 145, 0) in cols else (40, 130, 40)
    dirt = (96, 64, 36)
    c, pal = new_canvas(Z0 + 28)
    c.ell(16, 18, Z0, 13, 8, 4, dirt, mat='soil')
    rng = np.random.default_rng(7)
    for _ in range(9):                                                           # 草の房（出っ張り）。前は 26 個、約 1/3 に減らした
        x, y = 16 + rng.uniform(-11, 11), 18 + rng.uniform(-6, 6)
        if ((x - 16) / 12) ** 2 + ((y - 18) / 7) ** 2 < 1:
            c.box(x, y, Z0 + 2, x + 1.5, y + 1.5, Z0 + 4, grass, mat='plant')
    X, Y, Z = c._g
    stone_m = (X >= 5) & (X < 27) & (Y >= 11) & (Y < 17) & (Z >= Z0) & (Z < Z0 + 25)
    top = (Z >= Z0 + 14) & ((((X + 0.5 - 16) / 11.0) ** 2 + ((Z + 0.5 - (Z0 + 14)) / 11.0) ** 2) > 1.0)   # 頭を丸く削る
    c.mask(stone_m & ~top, stone, mat='stone')
    c.mask(stone_m & ~top & ((X < 6) | (X >= 26)), shade(stone, 0.85), mat='stone')
    c.box(5, 17 - 1, Z0, 27, 17, Z0 + 2, shade(stone, 0.8))                    # 墓石の根元の影
    # RIP（3x5 の字を 2 倍。凹み 1 ボクセル）
    x0 = 6
    z_top = Z0 + 20
    for k, ch in enumerate('RIP'):
        for r, row in enumerate(FONT[ch]):
            for q, ink in enumerate(row):
                if ink == 'X':
                    xx = x0 + k * 7 + q * 2
                    zz = z_top - r * 2 - 1
                    c.box(xx, 16, zz, xx + 2, 17, zz + 2, (0, 0, 0))
                    c.box(xx, 15, zz, xx + 2, 16, zz + 2, (0, 0, 0))
    return finish(name, c, pal, 'NetHack の墓 %s を絵から解釈して一から作った（土の盛り・丸い頭の墓石・彫り込んだ RIP。tools/nethack/gen_nethack_terrain.py）' % what)


def build_throne(name, idx, what):
    """金の肘掛けと背もたれ・赤い座面・背の赤い面に青い十字。色は絵から。"""
    cols = tile_colours(idx, n=5)
    gold = (255, 255, 0) if (255, 255, 0) in cols else cols[0]
    orange = (255, 108, 0) if (255, 108, 0) in cols else cols[1]
    red = (255, 0, 0) if (255, 0, 0) in cols else cols[2]
    blue = (0, 0, 255) if (0, 0, 255) in cols else cols[3]
    gold = shade(gold, 0.86)
    c, pal = new_canvas(Z0 + 32)
    c.box(7, 11, 0, 25, 25, Z0 + 9, gold, mat='metal')                           # 座の台
    c.box(10, 24, Z0 + 1, 22, 25, Z0 + 7, orange)                                # 台の前面の橙の面
    c.box(9, 13, Z0 + 9, 23, 24, Z0 + 11, red)                                   # 座面
    c.box(7, 12, Z0 + 9, 10, 25, Z0 + 16, gold, mat='metal')                     # 肘掛け
    c.box(22, 12, Z0 + 9, 25, 25, Z0 + 16, gold, mat='metal')
    X, Y, Z = c._g
    back = (X >= 8) & (X < 24) & (Y >= 11) & (Y < 16) & (Z >= Z0 + 9) & (Z < Z0 + 30)
    round_top = (Z >= Z0 + 22) & ((((X + 0.5 - 16) / 8.0) ** 2 + ((Z + 0.5 - (Z0 + 22)) / 8.0) ** 2) > 1.0)
    c.mask(back & ~round_top, gold, mat='metal')
    panel = (X >= 11) & (X < 21) & (Y >= 15) & (Y < 16) & (Z >= Z0 + 12) & (Z < Z0 + 27) & ~round_top
    c.mask(panel, red)
    c.box(15, 15, Z0 + 14, 17, 16, Z0 + 25, blue)                                # 青い十字
    c.box(13, 15, Z0 + 19, 19, 16, Z0 + 21, blue)
    return finish(name, c, pal, 'NetHack の玉座 %s を絵から解釈して一から作った（金の肘掛けと背もたれ・赤い座面と背の面・青い十字。tools/nethack/gen_nethack_terrain.py）' % what)


def build_sink(name, idx, what):
    """白い柱の上の水盤・水・2 つの蛇口。色は絵（白・青灰・水色）から。"""
    cols = tile_colours(idx, n=5)
    white = (215, 215, 215) if (215, 215, 215) in cols else cols[0]
    metal = (108, 145, 182) if (108, 145, 182) in cols else cols[2]
    water = (0, 182, 255) if (0, 182, 255) in cols else cols[3]
    c, pal = new_canvas(Z0 + 28)
    c.cyl(16, 16, 0, Z0 + 11, 6.0, 4.5, white)                                   # 脚
    c.box(4, 9, Z0 + 10, 28, 25, Z0 + 18, white)                                 # 水盤
    X, Y, Z = c._g
    c.clear((X >= 6) & (X < 26) & (Y >= 11) & (Y < 23) & (Z >= Z0 + 14))      # 水盤の内側をくり抜き
    c.box(6, 11, Z0 + 14, 26, 23, Z0 + 17, water, mat='clean')                   # 水
    for x in (8, 21):                                                            # 蛇口 2 つ
        c.box(x, 9, Z0 + 18, x + 3, 12, Z0 + 25, metal, mat='metal')
        c.box(x, 11, Z0 + 23, x + 3, 15, Z0 + 25, metal, mat='metal')
    return finish(name, c, pal, 'NetHack の流し %s を絵から解釈して一から作った（白い脚・水盤・水・2 つの蛇口。tools/nethack/gen_nethack_terrain.py）' % what)


def build_fountain(name, idx, what):
    """円い水盤・青い水・中央の柱と吹き上がる水。色は絵（青・白・青灰）から。"""
    cols = tile_colours(idx, n=5)
    blue = (0, 0, 255) if (0, 0, 255) in cols else cols[0]
    stone = (108, 145, 182) if (108, 145, 182) in cols else cols[1]
    white = (215, 215, 215) if (215, 215, 215) in cols else cols[2]
    c, pal = new_canvas(Z0 + 28)
    c.cyl(16, 16, 0, Z0 + 7, 14, 14, white)                                      # 水盤の縁
    X, Y, Z = c._g
    c.clear(((X + 0.5 - 16) ** 2 + (Y + 0.5 - 16) ** 2 <= 11.5 ** 2) & (Z >= Z0 + 2))
    c.cyl(16, 16, Z0 + 1, Z0 + 5, 11.5, 11.5, blue, mat='clean')                 # 水
    c.cyl(16, 16, Z0, Z0 + 3, 5.5, 4.5, stone)                                   # 柱の台
    c.cyl(16, 16, Z0 + 3, Z0 + 13, 2.6, 2.0, white)                              # 柱
    c.cyl(16, 16, Z0 + 12, Z0 + 14, 4.0, 4.0, stone)
    spray = mix(blue, (255, 255, 255), 0.35)
    for a in np.linspace(0, 2 * np.pi, 6, endpoint=False):                       # 吹き上がる水（柱の先から外へ弧を描く）
        c.line((16, 16, Z0 + 14), (16 + 5.5 * np.cos(a), 16 + 5.5 * np.sin(a), Z0 + 19), 1.1, blue, mat='clean')
        c.line((16 + 5.5 * np.cos(a), 16 + 5.5 * np.sin(a), Z0 + 19), (16 + 9 * np.cos(a), 16 + 9 * np.sin(a), Z0 + 6), 0.9, spray, mat='clean')
    c.line((16, 16, Z0 + 14), (16, 16, Z0 + 23), 1.4, spray, mat='clean')
    return finish(name, c, pal, 'NetHack の噴水 %s を絵から解釈して一から作った（円い水盤・青い水・中央の柱・吹き上がる水。tools/nethack/gen_nethack_terrain.py）' % what)


def build_tree(name, idx, what):
    """幹と 4 つの葉の塊。色は絵（葉の緑・幹の茶）から。"""
    cols = tile_colours(idx, n=4)
    green = (0, 145, 0) if (0, 145, 0) in cols else cols[0]
    brown = (145, 71, 0) if (145, 71, 0) in cols else cols[1]
    lite = (204, 79, 0) if (204, 79, 0) in cols else shade(brown, 1.3)
    c, pal = new_canvas(Z0 + 34)
    c.cyl(16, 16, 0, Z0 + 16, 3.6, 3.0, brown, mat='wood')
    c.cyl(14.8, 15.2, Z0 + 2, Z0 + 14, 1.2, 1.0, lite, mat='wood')
    for a in (0.5, 2.2, 4.0):                                                    # 根の張り出し
        c.line((16, 16, Z0 + 1), (16 + 6 * np.cos(a), 16 + 6 * np.sin(a), Z0), 1.4, brown, mat='wood')
    for (x, y, z, rx, rz, k) in ((16, 16, Z0 + 21, 11.5, 8.0, 1.0), (9.5, 18, Z0 + 18, 7.5, 6.0, 0.9),
                                 (23, 15, Z0 + 18, 7.5, 6.0, 0.9), (16, 12, Z0 + 27, 7.0, 5.5, 1.12)):
        c.ell(x, y, z, rx, rx, rz, shade(green, k), mat='plant')
    rng = np.random.default_rng(11)
    for _ in range(10):                                                          # 葉の明暗（出っ張り）。前は 30 個、約 1/3 に減らした
        a, r = rng.uniform(0, 2 * np.pi), rng.uniform(2, 10)
        x, y, z = 16 + r * np.cos(a), 16 + r * np.sin(a), Z0 + 17 + rng.uniform(0, 12)
        if c.get(int(x), int(y), int(z)) != 0:
            c.box(x, y, z, x + 1.6, y + 1.6, z + 1.6, shade(green, 1.35), mat='plant')
    return finish(name, c, pal, 'NetHack の木 %s を絵から解釈して一から作った（幹と 4 つの葉の塊。tools/nethack/gen_nethack_terrain.py）' % what)


def build_bars(name, idx, what):
    """5 本の縦棒（絵と同じ位置・幅 4）と上下の桟。向こうが見える。色は絵（青灰・水色）から。"""
    cols = tile_colours(idx, n=3)
    iron = (108, 145, 182) if (108, 145, 182) in cols else cols[0]
    lite = (0, 182, 255) if (0, 182, 255) in cols else cols[1]
    c, pal = new_canvas(V - 1)
    for px_ in (1, 4, 7, 10, 13):                                                # 絵の棒の位置（2 画素幅）を 2 倍
        x = px_ * 2
        c.box(x, 14, 0, x + 2, 18, V - 3, iron, mat='metal')
        c.box(x + 2, 14, 0, x + 4, 18, V - 3, lite, mat='metal')
    for z in (4, 22):
        c.box(0, 15, z, V, 17, z + 3, shade(iron, 0.85), mat='metal')
    return finish(name, c, pal, 'NetHack の鉄格子 %s を絵から解釈して一から作った（5 本の縦棒と上下の桟。向こうが見える。tools/nethack/gen_nethack_terrain.py）' % what)


def _ladder_colours(idx):
    cols = tile_colours(idx, n=8)
    rung = (255, 108, 0) if (255, 108, 0) in cols else cols[1]
    rail = max((c for c in cols if c not in ((255, 0, 0), rung, (204, 79, 0), (145, 71, 0))), key=lambda c: lum(np.array(c, float)))
    return rail, rung


def build_ladder_up(name, idx, what):
    """2 本の縦棒と横木が立つ梯子。棒と横木の色は絵から。"""
    rail, rung = _ladder_colours(idx)
    c, pal = new_canvas(Z0 + 30)
    c.box(10, 14, 0, 13, 17, Z0 + 29, rail, mat='metal')
    c.box(19, 14, 0, 22, 17, Z0 + 29, rail, mat='metal')
    for z in range(Z0 + 3, Z0 + 28, 5):
        c.box(13, 14.5, z, 19, 16.5, z + 2, rung, mat='wood')
    return finish(name, c, pal, 'NetHack の上り梯子 %s を絵から解釈して一から作った（2 本の縦棒と横木。tools/nethack/gen_nethack_terrain.py）' % what)


def build_ladder_down(name, idx, what):
    """床に四角い穴を彫り、縁から棒が出て穴の中に横木が見える梯子。床の板を自分で含む。"""
    rail, rung = _ladder_colours(idx)
    pal = Palette(gt.mat_any)
    h = FLOOR_H
    base = h - 2
    side = pal(shade(FLOOR_BG, 0.75))
    v = np.zeros((V, V, h + 4), np.int16)
    lo, hi = 7, 25
    for y in range(V):
        for x in range(V):
            if lo <= x < hi and lo <= y < hi:
                zt = base - 4
                v[x, y, :zt] = side
                v[x, y, zt] = pal((6, 6, 10))
            elif lo - 2 <= x < hi + 2 and lo - 2 <= y < hi + 2:
                v[x, y, :base] = pal(shade(FLOOR_BG, 0.9))
                v[x, y, base] = pal((0, 0, 0))                                   # 穴の縁の黒い枠
            else:
                v[x, y, :base] = side
                v[x, y, base] = pal(FLOOR_BG)
    for x in (11, 19):                                                           # 縁から出る棒
        v[x:x + 2, 11:13, 0:base + 4] = pal(rail, 'metal')
    for z in (base - 3, base - 1, base + 1):                                     # 穴の中の横木
        v[13:19, 11:13, z:z + 1] = pal(rung, 'wood')
    size, ncol, nfp = _capture_write_pair(name, [('main', v, (0, 0, -h))], pal,
                                          'NetHack の下り梯子 %s を絵から解釈して一から作った（床の穴・縁から出る棒・穴の中の横木。tools/nethack/gen_nethack_terrain.py）' % what)
    return '%dx%dx%d' % v.shape, ncol, size, '', nfp


# ------------------------------------------------------------------ 壁（上面の縁の柵。2026-10-28 D26）
#: 原作の壁のタイルは、上から見た壁の天面の縁に**白・灰の短い柱が間をあけて並ぶ**（柵・銃眼のように見える）。縦の壁は左右の縁に（柱は 3 画素幅・縦に 5 画素＋すき間 3 画素の周期 8）、
#: 横の壁は上の縁と手前の縁の 2 段に（周期 8 画素に柱 6＋すき間 2）。角・T 字・十字は同じ絵を使い回している。これをブロックの上面から**盛り上げた柵**にする。
#:   周期 16 ボクセル（タイルの 2 倍＝1 マス 32 ボクセルに 2 周期）。柱の長さは縦・横の平均（5/8・6/8）に近い 10、すき間は 6。柱はマスの中心に対して左右（上下）対称で、
#:   マスの継ぎ目をまたぐすき間も 6（3＋3）になる。同じ向きの壁が続くと同じ周期でつながる。
#:   太さ 4・高さ 4（頭の 1 層は柱の頭の色）。原作の柱は 16 画素に 3 画素幅（32 ボクセルなら 6）だが、左右（上下）2 本で 12 ボクセルが埋まり天面の煉瓦がほとんど見えなくなるので 4 に細くした。
#:   高さは凹凸（深さ 0〜2）より目立ち、背が 1 マスの 1/8 しか伸びない（32→36）4 にした。縁から 2 ボクセル内側に立てる（隣のマスの側面に柱が接しない）。
#:   角の柱: 隣り合う 2 辺の柵の有る無しが同じとき（外側の角・内側の角・十字）だけ立てる。
RIM_PERIOD = 16
RIM_POST = (3, 12)      # 周期の中で柱がある範囲（含む）。マスの中心に対して対称
RIM_W = 4               # 柱の太さ（縁に直角な向き）
RIM_H = 4               # 柱の高さ（平らな上面 z = V-1 から上）
RIM_INSET = 2           # マスの縁から柱までの距離
WALL_H = V + RIM_H      # 壁のブロックの高さ（柱の頭の上端）。32 → 36
#: 上面（2026-10-28。遊ぶ人「壁の上面に柵以外の 2 ボクセル幅の白い模様が目立って点線のように見えている。これは無くしてブロック上面がもっとフラットに見えるようにして」
#: → 1 色の平らな面にしたら「フラットにしすぎ。中間くらいにできる？」）。DCSS の `build_wall` は横の壁の絵を天面にも写すので、絵の短い柱の列（白 1 画素＋薄い灰 2 画素）が
#: 上面に 2 ボクセル幅の白い点線として出ていた。そこで上面だけ絵を描き替える（`wall_top`）:
#:   * 柵の色（`WALL_SYSTEMS` の柱と柱の頭の色）と黒い縁取りの画素を、系統ごとの地の色 `WALL_TOP_FILL`（煉瓦の面の色）に置き換える。煉瓦と目地の模様は残る。
#:   * 凹凸は**目地のへこみ 1 段だけ**（`nh_relief_levels` がへこみと判じた画素を 1 下げる）。出っ張りは無い。上面の基準は z = V-1。側面の煉瓦はそのまま。
WALL_TOP_FILL = {'main': (108, 145, 182), 'mines': (145, 71, 0), 'gehennom': (72, 108, 108), 'knox': (145, 71, 0), 'sokoban': (0, 0, 255)}


def wall_top(key, sysname, rim):
    """上面の (色 [x, y] の RGB 配列, へこみ [x, y] の真偽)。配列の向きは DCSS の `build_wall` の天面と同じ（`np.rot90` の後で [y, x] を [x, y] に置く）。"""
    img = _TILES[key][..., :3].astype(int)
    drop = [tuple(rim[0]), tuple(rim[1]), (0, 0, 0)]
    m = np.zeros(img.shape[:2], bool)
    for c in drop:
        m |= (img == np.array(c)).all(axis=2)
    img = img.copy()
    img[m] = WALL_TOP_FILL[sysname]
    top = np.rot90(img)                                    # [y, x]
    lev = nh_relief_levels(top)
    return np.transpose(top, (1, 0, 2)), np.transpose(lev == 2)
_WALL_BASE = {}         # 絵の鍵 -> (配列, 色表, 材質表)。同じ系統の 11 種は側面と天面が同じなので 1 回だけ作る
RIM_TOP = WALL_H        # 柵の柱の頭の上端（柵だけの壁の背。35）

# ------------------------------------------------------------------ 交差の頭（2026-10-28 D27。同日に形と色を直した）
#: 原作の交差（角 4・T 字 4・十字。9 種とも同じ絵）の**一番上の部分**を立体にして、交差のブロックの上面の中央へ載せる。系統で絵が違うので形を 2 つに分ける（`JUNC_STYLE`）。
#: 原作の絵は光が左上から当たるように描かれている（左の面が明るく、手前の面は中間、右の面は暗い）。3D は画面が明かりで陰を付けるので、**材の色は光の当たった左の面の色**にする。
#:   台（basin。通常・ゲヘナ・ソコバン）: 絵の一番上は、明るい枠に囲まれた広い平らな面に、暗い穴（黒）と右寄りの小さな明るい四角がある。
#:     裾の四角錐台（2 層ごとに 1 ボクセル内側へ。6 層で底 20 四方 → 上面 16 四方）の上面の縁に、1 ボクセル幅・2 層の縁を立て、上面の中央に 10 四方・深さ 4 の穴をあける（遊ぶ人「穴ももっと大きく」）。
#:     色: 材（body）= 絵の左の面の色（通常は白に近い灰・ゲヘナは赤・ソコバンは水色）、縁の頭（hi）= 絵の左上の白い縁、穴の底（hole）= 黒、
#:     穴の西・北の内壁（shadow。光に背を向ける）= 絵の暗い色、穴の東・南の内壁（glint。光が当たる）= 絵の小さな明るい四角の色。
#:   山（mound。鉱山・ノックス）: 絵の一番上は四角錐ではなく、光の当たる稜線（薄い桃）のある岩山で、手前の下に小さな出っ張りがある。
#:     底いっぱいの丸みのある山（中心から離れるほど低く。高さ = 11 × (1 − (距離/半径)^2.4)。遊ぶ人「山はもっと大きく」）の、手前（南）の左寄りの斜面の下部へ
#:     小さな丸い出っ張りを盛る。稜線（薄い桃）は頂から左手前へ下る。色は稜線 = hi、ほかは面の向き（光は左上から）で lit・skirt・dark の 3 段。
#:   どちらも底はマスの縁から 6 ボクセル内側まで（柵の柱は縁から 2〜5 なので接しない）。高さは台が 8 層・山が 12 層まで（z = V〜V+11。柵の頭 z = V+2 より高い）。
#:   交差の縁の柵: **残す**（`JUNCTION_KEEP_RIM`）。柵は壁が伸びない側の縁にあり、隣の壁の柵と同じ縁でそのまま続く。外すと隣の壁の柵がマスの境で途切れる。
JUNC_X0 = 6             # 頭の底の始まり（マスの縁からのボクセル数）。底は JUNC_X0〜V-1-JUNC_X0
JUNC_Z0 = V             # 頭の最下層の z（凹凸の出っ張りの面 z = V-1 の上）
JUNC_LAYERS = 12        # 頭の層の数（山の高さ。台は 8 層）
JUNCTION_H = JUNC_Z0 + JUNC_LAYERS   # 交差の壁のブロックの高さ（44）
JUNCTION_KEEP_RIM = True      # 交差のブロックの縁にも、壁が伸びない側の柵を残す（隣の壁の柵と同じ縁でつながるため。上の説明）
JUNC_STYLE = {'main': 'basin', 'gehennom': 'basin', 'sokoban': 'basin', 'mines': 'mound', 'knox': 'mound'}
#: 系統ごとの頭の色（すべて原作の交差の絵の一番上の部分〔上 8 行〕にある色。`run_check` が確かめる）
JUNC_COLORS = {
    'main': {'body': (215, 215, 215), 'hi': (255, 255, 255), 'shadow': (72, 108, 108), 'glint': (0, 182, 255), 'hole': (0, 0, 0)},
    'gehennom': {'body': (255, 0, 0), 'hi': (255, 182, 145), 'shadow': (145, 71, 0), 'glint': (255, 108, 0), 'hole': (0, 0, 0)},
    'sokoban': {'body': (0, 182, 255), 'hi': (255, 255, 255), 'shadow': (72, 108, 108), 'glint': (255, 255, 255), 'hole': (0, 0, 0)},
    'mines': {'hi': (255, 182, 145), 'lit': (255, 108, 0), 'skirt': (204, 79, 0), 'dark': (145, 71, 0)},
    'knox': {'hi': (255, 182, 145), 'lit': (255, 108, 0), 'skirt': (204, 79, 0), 'dark': (145, 71, 0)},
}
#: 台の寸法: 裾の層の数（2 層ごとに 1 内側へ）・縁の層の数・穴の一辺と深さ
BASIN_SLOPE = 6
BASIN_LIP = 2
BASIN_HOLE = 10
BASIN_HOLE_DEPTH = 4
#: 山の寸法: (中心 x, 中心 y, 半径, 高さ, 丸みの指数。大きいほど頂が丸く平らに近い) と出っ張り (中心 x, 中心 y, 半径, 斜面から盛る高さ)。y は南が大きい（手前）
MOUND = (15.5, 15.5, 9.8, 11.0, 2.4)
MOUND_KNOB = (12.5, 22.5, 2.8, 3.0)
#: 稜線（原作の絵の頂から左下へ下る薄い桃の筋）: 頂から見た向き (x, y)・幅・盛り上げる高さ
MOUND_RIDGE = ((-1.0, 0.5), 1.2, 0.7)
#: 光の向き（原作の絵は正面〔南〕から見て左上から光が当たる）
MOUND_LIGHT = (-1.0, 0.2, 1.0)
JUNCTIONS = tuple(k for k in WALL_ARMS if k not in ('vwall', 'hwall'))     # 角 4・十字・T 字 4


def _basin_head(c):
    """台の頭: {(x, y, 層): 色}。"""
    out = {}
    lo, hi = JUNC_X0, V - 1 - JUNC_X0
    for layer in range(BASIN_SLOPE):                  # 裾の四角錐台。最上層が平らな上面
        a, b = lo + layer // 2, hi - layer // 2
        for x in range(a, b + 1):
            for y in range(a, b + 1):
                out[(x, y, layer)] = c['body']
    a, b = lo + (BASIN_SLOPE - 1) // 2, hi - (BASIN_SLOPE - 1) // 2        # 上面の外周
    for layer in range(BASIN_SLOPE, BASIN_SLOPE + BASIN_LIP):            # 上面の縁を立てる
        for x in range(a, b + 1):
            for y in range(a, b + 1):
                if x in (a, b) or y in (a, b):
                    out[(x, y, layer)] = c['hi'] if layer == BASIN_SLOPE + BASIN_LIP - 1 else c['body']
    h0 = (V - BASIN_HOLE) // 2
    h1 = h0 + BASIN_HOLE - 1
    floor = BASIN_SLOPE - 1 - BASIN_HOLE_DEPTH      # 穴の底の層
    for x in range(h0 - 1, h1 + 2):
        for y in range(h0 - 1, h1 + 2):
            inside = h0 <= x <= h1 and h0 <= y <= h1
            for layer in range(floor + 1, BASIN_SLOPE):
                if inside:
                    out.pop((x, y, layer), None)
                elif layer < BASIN_SLOPE - 1:     # 穴の内壁（上面の層は材の色のまま）
                    out[(x, y, layer)] = c['shadow'] if (x < h0 or y < h0) else c['glint']
            if inside:
                out[(x, y, floor)] = c['hole']
    return out


def _mound_ridge(x, y):
    """稜線の上なら True。頂から MOUND_RIDGE の向きへ、半径の手前まで。"""
    cx, cy, r = MOUND[0], MOUND[1], MOUND[2]
    (ux, uy), width, _ = MOUND_RIDGE
    n = np.hypot(ux, uy)
    ux, uy = ux / n, uy / n
    dx, dy = x - cx, y - cy
    t = dx * ux + dy * uy
    s = abs(dx * uy - dy * ux)
    return 0.5 < t < r * 0.85 and s < width


def _mound_height(x, y):
    """山の高さ（層。小数）。丸みのある山（稜線は少し盛り上げる）に、手前の出っ張りを盛ったもの。"""
    cx, cy, r, h, p = MOUND
    d = np.hypot(x - cx, y - cy) / r
    hm = h * (1.0 - d ** p) if d < 1.0 else 0.0
    if hm > 0 and _mound_ridge(x, y):
        hm += MOUND_RIDGE[2]
    kx, ky, kr, kh = MOUND_KNOB
    dk = np.hypot(x - kx, y - ky) / kr
    hk = kh * (1.0 - dk * dk) if dk < 1.0 else 0.0
    return hm + hk if hm > 0 else 0.0


def _mound_head(c):
    """山の頭: {(x, y, 層): 色}。稜線は hi、ほかは面の向きと光（`MOUND_LIGHT`）で 3 段（lit・skirt・dark）。"""
    out = {}
    light = np.array(MOUND_LIGHT) / np.linalg.norm(MOUND_LIGHT)
    lo, hi = JUNC_X0, V - 1 - JUNC_X0
    for x in range(lo, hi + 1):
        for y in range(lo, hi + 1):
            cx, cy = x + 0.5, y + 0.5
            n = int(round(_mound_height(cx, cy)))
            if n < 1:
                continue
            gx = (_mound_height(cx + 0.5, cy) - _mound_height(cx - 0.5, cy))
            gy = (_mound_height(cx, cy + 0.5) - _mound_height(cx, cy - 0.5))
            nv = np.array([-gx, -gy, 1.0])
            d = float(nv @ light) / float(np.linalg.norm(nv))
            if _mound_ridge(cx, cy) and n > 1:
                col = c['hi']
            else:
                col = c['lit'] if d >= 0.62 else c['skirt'] if d >= 0.40 else c['dark']
            for layer in range(min(n, JUNC_LAYERS)):
                out[(x, y, layer)] = col
    return out


def junction_head(sysname):
    """交差の頭: {(x, y, 層): 色}（層 0 が z = JUNC_Z0）。"""
    c = JUNC_COLORS[sysname]
    return _basin_head(c) if JUNC_STYLE[sysname] == 'basin' else _mound_head(c)


def junction_head_occ(sysname):
    """交差の頭のある所 [x, y, 層] の真偽と、底の範囲 [x, y]。"""
    occ = np.zeros((V, V, JUNC_LAYERS), bool)
    for (x, y, layer) in junction_head(sysname):
        occ[x, y, layer] = True
    return occ, occ.any(axis=2)


def rim_mask(arms):
    """柵の柱がある (x, y)（配列 [x, y]。北が y = 0 側）を返す。壁が伸びる向き arms（'NS' など）に**無い**側の縁に沿って柱を並べる。
    縦の壁（NS）は西と東の縁、横の壁（EW）は北と南の縁、角は伸びない 2 辺、T 字は伸びない 1 辺、十字は柵の辺が無い。"""
    m = np.zeros((V, V), bool)
    t = np.arange(V) % RIM_PERIOD
    on = (t >= RIM_POST[0]) & (t <= RIM_POST[1])
    lo = slice(RIM_INSET, RIM_INSET + RIM_W)
    hi = slice(V - RIM_INSET - RIM_W, V - RIM_INSET)
    if 'N' not in arms:
        m[:, lo] |= on[:, None]
    if 'S' not in arms:
        m[:, hi] |= on[:, None]
    if 'W' not in arms:
        m[lo, :] |= on[None, :]
    if 'E' not in arms:
        m[hi, :] |= on[None, :]
    # 角の柱: 隣り合う 2 辺がどちらも柵のある辺（外側の角）か、どちらも無い辺（内側のへこみの角・十字）のとき。片方だけ柵のある辺なら、柵の周期をそのまま通す
    for ex, xs in (('W', lo), ('E', hi)):
        for ey, ys in (('N', lo), ('S', hi)):
            if (ex not in arms) == (ey not in arms):
                m[xs, ys] = True
    return m


def build_wall_rim(name, key, what, arms, rim, sysname=None, kind=None):
    """DCSS の `build_wall`（4 側面と天面の煉瓦。凹凸は約 1/3）で作った 32x32x32 に、上面の縁の柵（`rim_mask`）を高さ `RIM_H` で足す。
    交差（角・T 字・十字）には、さらに中央へ頭（`junction_head`）を載せる。"""
    if key not in _WALL_BASE:
        rec = {}

        def _only_record(n, parts, pal, note, extra_parts=None):
            rec['v'] = parts[0][1].copy()
            rec['table'] = dict(pal.table())
            rec['mats'] = dict(pal.materials())
            return 0, 0, 0

        saved = gt.write_pair
        gt.write_pair = _only_record
        try:
            gt.build_wall(name, key, what)
        finally:
            gt.write_pair = saved
        _WALL_BASE[key] = (rec['v'], rec['table'], rec['mats'])
    else:
        dv.record_use(key, name)
    base, table, mats = _WALL_BASE[key]
    pal = Palette(gt.mat_stone)
    for i in sorted(table):
        pal.index[(tuple(table[i]), mats[i])] = i
    body, cap = rim
    junction = kind in JUNCTIONS
    v = np.zeros((V, V, JUNCTION_H if junction else WALL_H), np.int16)
    v[:, :, :V] = base
    tcol, tdent = wall_top(key, sysname, rim)       # 上面を描き替える（上の WALL_TOP_FILL）
    for x in range(V):
        for y in range(V):
            c = pal(tuple(int(t) for t in tcol[x, y]), 'stone')
            edge = x in (0, V - 1) or y in (0, V - 1)
            v[x, y, V - 1] = 0 if (tdent[x, y] and not edge) else c
            if not edge:
                v[x, y, V - 3:V - 1] = c
    c_body, c_cap = pal(body, 'stone'), pal(cap, 'stone')
    m = rim_mask(arms)
    if junction and not JUNCTION_KEEP_RIM:
        m = np.zeros((V, V), bool)
    xs, ys = np.nonzero(m)
    for z in range(V - 3, RIM_TOP):                 # 上面の下から立て、空いている所だけ埋める（上面より下は詰まっているので、実際は上面の上だけ）
        col = v[xs, ys, z]
        v[xs, ys, z] = np.where(col == 0, c_cap if z == RIM_TOP - 1 else c_body, col)
    extra = ''
    if junction:
        head = junction_head(sysname)
        foot_col = {}
        for (x, y, layer), col in head.items():
            v[x, y, JUNC_Z0 + layer] = pal(col, 'stone')
            if layer == 0:
                foot_col[(x, y)] = col
        for (x, y), col in foot_col.items():           # 頭の下の空きを詰める（上面は平らで詰まっているので、念のため）
            for z in range(V - 3, JUNC_Z0):
                if v[x, y, z] == 0:
                    v[x, y, z] = pal(col, 'stone')
        extra = ' 交差の頭 %s' % JUNC_STYLE[sysname]
    size, ncol, nfp = _capture_write_pair(
        name, [('main', v, (0, 0, 0))], pal,
        'NetHack の壁 %s（%s。壁が伸びる向き %s）。DCSS の壁と同じ 4 側面と天面に、上面の縁へ短い柱の柵（太さ %d・高さ %d・周期 %d）を盛り上げた%s（tools/nethack/gen_nethack_terrain.py）'
        % (what, key, arms, RIM_W, RIM_H, RIM_PERIOD,
           ('。交差なので中央に原作の交差の絵の一番上の部分を立体にした頭（%s）を載せた' % {'basin': '縁の立った穴あきの台', 'mound': '手前に出っ張りのある丸みのある山'}[JUNC_STYLE[sysname]]) if junction else ''))
    return '%dx%dx%d' % v.shape, ncol, size, '柵 %d 列 向き %s%s' % (int(m.sum()), arms, extra), nfp


# ------------------------------------------------------------------ 作る
def build_one(spec, names):
    name, kind, idx, expect, bg, opt = spec
    key = register(idx, bg)
    what = names[idx]
    if kind == 'wall':
        if 'arms' in opt:
            return build_wall_rim(name, key, what, opt['arms'], opt['rim'], opt['sys'], opt['kind'])
        return gt.build_wall(name, key, what)
    if kind == 'floor':
        return build_floor_dots(name, key, what)
    if kind == 'ground':
        return gt.build_floor(name, key, what, liquid=opt.get('liquid', False), style=opt.get('style', 'stone'))
    if kind == 'door':
        return gt.build_door(name, key, what, mode=opt['mode'], ew=opt['ew'])
    if kind == 'stairs_down':
        dv.record_use(key, name)
        return build_stairs_down_nh(name, idx, what)
    if kind == 'stairs_up':
        dv.record_use(key, name)
        return build_stairs_up_nh(name, idx, what)
    if kind == 'decal':
        return gt.build_decal(name, key, what)
    if kind == 'pit':
        return build_pit_nh(name, key, idx, what, opt['mode'])
    builders = {'altar': build_altar, 'grave': build_grave, 'throne': build_throne, 'sink': build_sink, 'fountain': build_fountain,
                'tree': build_tree, 'bars': build_bars, 'ladder_up': build_ladder_up, 'ladder_down': build_ladder_down}
    if kind in builders:
        dv.record_use(key, name)
        return builders[kind](name, idx, what)
    raise SystemExit('作り方が無い種類: %s' % kind)


def build_all(out_dir, raise_floors=False, only=None, quiet=False, report_dir=None):
    """全部（または名前の指定）を out_dir へ作る。戻り値: [(名前, 種類, 大きさ, 色数, バイト数, 補足, 接地マス数)]。
    raise_floors: 床の板の種類を床の厚みだけ持ち上げて書く（確認画面は地面の板が床を隠すため）。"""
    names = C.load_names()
    bad = C.check_tile_names([(s[0], s[2], s[3]) for s in SPECS], names)
    if bad:
        raise SystemExit('絵の名前が違う: %s' % bad)
    os.makedirs(out_dir, exist_ok=True)
    dv.set_out(out_dir)
    rows = []
    for spec in SPECS:
        if only and spec[0] not in only:
            continue
        size, ncol, nbytes, extra, nfp = build_one(spec, names)
        rows.append((spec[0], spec[1], size, ncol, nbytes, extra, nfp))
        if not quiet:
            print('%-26s %-12s %-10s 色 %3d  %6d B  %-14s 接地マス %d' % (spec[0], spec[1], size, ncol, nbytes, extra, nfp))
    if raise_floors:
        for spec in SPECS:
            if spec[1] in GROUND_KINDS and (not only or spec[0] in only):
                _rewrite_origin(spec[0], out_dir, FLOOR_H)
    if report_dir and not only:
        os.makedirs(report_dir, exist_ok=True)
        with open(os.path.join(report_dir, 'nh_terrain_manifest.tsv'), 'w', encoding='utf-8', newline='\n') as fp:
            fp.write('# 作ったプレハブ（tools/nethack/gen_nethack_terrain.py が書く）: 名前・種類・絵の番号・絵の名前\n')
            for s in SPECS:
                fp.write('%s\t%s\t%d\t%s\n' % (s[0], s[1], s[2], names[s[2]]))
        dv.write_used(os.path.join(report_dir, 'nh_terrain_tiles_used.tsv'))
    return rows


def _rewrite_origin(name, out_dir, raise_):
    """床の板の種類を、原点の z を持ち上げて書き直す（確認用。BUILT の配列から）。"""
    parts, table, mats = BUILT[name]
    pal = Palette(gt.mat_stone)
    for i in sorted(table):
        pal.index[(tuple(table[i]), mats[i])] = i
    n, v, o = parts[0]
    ORIG_WRITE_PAIR(name, [(n, v, (o[0], o[1], o[2] + raise_))], pal, 'NetHack の地形（確認用。原点を持ち上げた）')


# ------------------------------------------------------------------ 検査
def run_check(tmp_dir):
    ok = True
    names = C.load_names()
    sheet, meta = _sheet(), C.load_meta(C.DATA_EN)
    for p in C.check_sheet(sheet, meta):
        print('NG:', p)
        ok = False
    bad = C.check_tile_names([(s[0], s[2], s[3]) for s in SPECS], names)
    for b in bad:
        print('NG: 絵の番号 %d（%s）の名前が違う: 期待 %r / 実際 %r' % (b[1], b[0], b[2], b[3]))
        ok = False
    if not bad:
        print('OK: 作る物 %d 個の絵の名前が期待どおり' % len(SPECS))
    if len(set(NAMES)) != len(NAMES):
        print('NG: プレハブ名が重なる')
        ok = False
    t = register(1274, None)
    wall_body = int(((_TILES[t][..., :3] == np.array([72, 108, 108])).all(axis=2) & (_TILES[t][..., 3] > 0)).sum())
    if wall_body > 0:
        print('OK: 壁の地色 RGB(72,108,108) は不透明のまま残っている（%d 画素。透過色 RGB(71,108,108) と取り違えていない）' % wall_body)
    else:
        print('NG: 壁の地色が消えている（透過色と取り違えた疑い）')
        ok = False

    # 柵の柱の色は原作のタイルの上縁（上 8 行）にある色
    for sysname, idx, expect, body, cap in WALL_SYSTEMS:
        top = C.tile_rgba(_sheet(), idx)[:8]
        have = {tuple(int(c) for c in p[:3]) for p in top.reshape(-1, 4) if p[3] > 0}
        if body in have and cap in have:
            print('OK: %s の柵の色（柱 %s・頭 %s）は原作のタイルの上縁にある' % (sysname, body, cap))
        else:
            print('NG: %s の柵の色が原作のタイルの上縁に無い（柱 %s・頭 %s）' % (sysname, body, cap))
            ok = False
    # 交差の頭の色は、原作の交差の絵（9 種）の一番上の部分（上 8 行）にある色。色の役割がすべて使われている。底が柵の柱と接しない。柵より高い。形の特徴（台は縁・穴、山は出っ張り）がある
    for sysname, idx, expect, body, cap in WALL_SYSTEMS:
        jc = JUNC_COLORS[sysname]
        head = junction_head(sysname)
        used = set(head.values())
        for k, kind in enumerate(JUNCTIONS):
            top = C.tile_rgba(_sheet(), idx + 1 + k)[:8]
            have = {tuple(int(c) for c in p[:3]) for p in top.reshape(-1, 4) if p[3] > 0}
            miss = sorted(used - have)
            if miss:
                print('NG: %s の交差 %s の頭の色が原作の絵の一番上の部分に無い: %s' % (sysname, kind, miss))
                ok = False
        if used != set(jc.values()):
            print('NG: %s の交差の頭に使われていない色の役割がある %s' % (sysname, sorted(set(jc.values()) - used)))
            ok = False
        occ, foot = junction_head_occ(sysname)
        xs_, ys_ = np.nonzero(foot)
        lo, hi = int(min(xs_.min(), ys_.min())), int(max(xs_.max(), ys_.max()))
        if lo <= RIM_INSET + RIM_W - 1 or hi >= V - RIM_INSET - RIM_W:
            print('NG: %s の交差の頭の底（%d〜%d）が柵の柱（縁から %d まで）と接する' % (sysname, lo, hi, RIM_INSET + RIM_W - 1))
            ok = False
        top_layer = int(np.nonzero(occ.any(axis=(0, 1)))[0].max())
        if JUNC_Z0 + top_layer <= RIM_TOP - 1:
            print('NG: %s の交差の頭が柵より低い' % sysname)
            ok = False
        colh = np.where(foot, occ.shape[2] - np.argmax(occ[:, :, ::-1], axis=2), 0)      # 列ごとの高さ（層の数）
        if JUNC_STYLE[sysname] == 'basin':
            mid = V // 2
            lip_x = JUNC_X0 + (BASIN_SLOPE - 1) // 2               # 上面の外周（縁）
            hole_h, lip_h, plat_h = int(colh[mid, mid]), int(colh[lip_x, mid]), int(colh[lip_x + 2, mid])
            shape_ok = hole_h < plat_h < lip_h and head.get((mid, mid, hole_h - 1)) == jc['hole']
            what = '台: 穴の底 %d 層・上面 %d 層・縁 %d 層' % (hole_h, plat_h, lip_h)
        else:
            kx, ky = int(MOUND_KNOB[0]), int(MOUND_KNOB[1])
            peak = np.unravel_index(int(np.argmax(colh)), colh.shape)
            # 出っ張り: 中心が南（手前）と東西の隣（3 ボクセル先）より高い。山の頂は出っ張りより北（奥）
            shape_ok = colh[kx, ky] > max(colh[kx, ky + 2], colh[kx - 3, ky], colh[kx + 3, ky]) and peak[1] < ky
            what = '山: 頂 %d 層（y %d）・手前の出っ張り %d 層（y %d）' % (int(colh.max()), int(peak[1]), int(colh[kx, ky]), ky)
        if not shape_ok:
            print('NG: %s の交差の頭の形 %s' % (sysname, what))
            ok = False
        else:
            print('OK: %s の交差の頭 %s・底 %d〜%d（柵の柱は縁から %d まで）・頂の上端 z %d（柵の頭は z %d）' % (
                sysname, what, lo, hi, RIM_INSET + RIM_W - 1, JUNC_Z0 + top_layer, RIM_TOP - 1))
    # 壁の向きの表が原作の図どおり（伸びる向きの数: 縦横 2・角 2・T 字 3・十字 4）と、柵の位置が向きどおり
    want_n = {'vwall': 2, 'hwall': 2, 'tlcorn': 2, 'trcorn': 2, 'blcorn': 2, 'brcorn': 2, 'crwall': 4, 'tuwall': 3, 'tdwall': 3, 'tlwall': 3, 'trwall': 3}
    if list(WALL_ARMS) != list(want_n) or any(len(WALL_ARMS[k]) != n for k, n in want_n.items()):
        print('NG: 壁の向きの表が原作の図と合わない')
        ok = False
    masks = {k: rim_mask(a) for k, a in WALL_ARMS.items()}
    if len({m.tobytes() for m in masks.values()}) != len(masks):
        print('NG: 柵の位置が同じ向きがある（向きごとに作り分けていない）')
        ok = False
    else:
        print('OK: 壁 11 種の柵の位置がすべて違う')
    ew, ns = masks['hwall'], masks['vwall']
    inner = lambda m: int(m[RIM_INSET + RIM_W:V - RIM_INSET - RIM_W, RIM_INSET + RIM_W:V - RIM_INSET - RIM_W].sum())
    if inner(ew) or inner(ns) or inner(masks['crwall']):
        print('NG: 柵がマスの中ほどに出ている（縁に沿うはず）')
        ok = False
    # 同じ向きの壁を並べたとき柱の周期がつながる（柱 10・すき間 6 の繰り返しがマスの継ぎ目でも崩れない）
    col = ns[RIM_INSET, :]                      # 西の縁の柱（縦の壁。y に沿って）
    seq = ''.join('1' if b else '0' for b in np.concatenate([col, col, col]))
    segs = []
    cur, n = seq[0], 0
    for c in seq:
        if c == cur:
            n += 1
        else:
            segs.append((cur, n))
            cur, n = c, 1
    segs.append((cur, n))
    inner_segs = segs[1:-1]
    if {(c, n) for c, n in inner_segs} == {('1', RIM_POST[1] - RIM_POST[0] + 1), ('0', RIM_PERIOD - (RIM_POST[1] - RIM_POST[0] + 1))}:
        print('OK: 同じ向きの壁を続けて並べたとき、柱 %d・すき間 %d の周期がマスの継ぎ目でそろう' % (RIM_POST[1] - RIM_POST[0] + 1, RIM_PERIOD - (RIM_POST[1] - RIM_POST[0] + 1)))
    else:
        print('NG: 柱の周期がマスの継ぎ目でそろわない', inner_segs)
        ok = False
    # 凹凸のある画素の割合（壁の系統ごと）: DCSS と同じ判定の約 1/3 になっている
    for s in SPECS:
        if s[1] != 'wall' or not s[0].startswith('nh_wall_') or not s[0].endswith('_hwall'):
            continue
        img = _TILES[register(s[2], s[4])][..., :3].astype(int)
        n_old = int((dv.relief_levels(img) != 1).sum())
        n_new = int((nh_relief_levels(img) != 1).sum())
        if n_old and not (0.2 <= n_new / n_old <= 0.45):
            print('NG: %s の凹凸のある画素が元の %.0f%%（約 1/3 のはず）' % (s[0], 100.0 * n_new / n_old))
            ok = False
        else:
            print('OK: %s の凹凸のある画素 %.1f%% → %.1f%%（元の %.0f%%）' % (s[0], 100.0 * n_old / img[..., 0].size, 100.0 * n_new / img[..., 0].size, 100.0 * n_new / max(n_old, 1)))

    BUILT.clear()
    build_all(tmp_dir, raise_floors=False, quiet=True)
    edge_tops = {}
    n_float = 0
    for spec in SPECS:
        name, kind = spec[0], spec[1]
        parts, table, mats = BUILT[name]
        _, v, origin = parts[0]
        if len(table) > 255:
            print('NG: %s の色が 255 を超えた（%d）' % (name, len(table)))
            ok = False
        if not (v > 0).any():
            print('NG: %s が空' % name)
            ok = False
        for f in (name + '.vox', name + '.jsonc'):
            if not os.path.isfile(os.path.join(tmp_dir, f)):
                print('NG: %s を書いていない' % f)
                ok = False
        occ = v > 0
        # 浮き: 6 近傍でつながった塊のどれもが最下段（z = 0）に届く（空中に浮いた塊・離れ小島が無い。木の葉の張り出しや水盤の張り出しは幹・脚でつながっている）
        lab, nlab = ndimage.label(occ)
        islands = [i for i in range(1, nlab + 1) if not (lab[:, :, 0] == i).any()]
        if islands:
            n_island = sum(int((lab == i).sum()) for i in islands)
            print('NG: %s に浮いた塊が %d 個（%d ボクセル。最下段に届かない）' % (name, len(islands), n_island))
            ok = False
            n_float += len(islands)
        if kind == 'wall':
            jn = spec[5].get('kind') in JUNCTIONS
            want_shape = (V, V, JUNCTION_H if jn else WALL_H) if 'arms' in spec[5] else (V, V, V)
            if v.shape != want_shape:
                print('NG: %s の大きさが %s（%s のはず）' % (name, v.shape, want_shape))
                ok = False
            if 'arms' in spec[5]:
                # 柵: 平らな上面（z = V-1）より上は rim_mask と交差の頭の所だけにあり、柱は上面の下から頭まで詰まっていて、頭の層（最上段）が柱の頭の色
                above = v[:, :, V:] > 0
                m = rim_mask(spec[5]['arms'])
                if jn and not JUNCTION_KEEP_RIM:
                    m = np.zeros_like(m)
                hocc = np.zeros(above.shape, bool)
                foot = np.zeros((V, V), bool)
                if jn:
                    hocc_, foot = junction_head_occ(spec[5]['sys'])
                    hocc[:, :, JUNC_Z0 - V:JUNC_Z0 - V + JUNC_LAYERS] = hocc_
                zz = np.arange(V, v.shape[2])[None, None, :]
                want_above = (m[:, :, None] & (zz < RIM_TOP)) | hocc
                if not (above == want_above).all():
                    print('NG: %s の柵・交差の頭の位置が rim_mask・junction_head と違う' % name)
                    ok = False
                if not (v[m][:, V - 3:RIM_TOP] > 0).all():
                    print('NG: %s の柱が途切れている（柱の下に空きがある）' % name)
                    ok = False
                # 上面: 柵の柱と交差の頭の無い所に、柵の色（柱・柱の頭）と黒が無い。へこみは 1 段だけ（z = V-2 は内側が全部詰まっている）。出っ張りは無い（z = V はその所に無い）。
                #       模様が残っている（色が 2 つ以上）
                free = ~m & ~foot
                top_v = np.where(v[:, :, V - 1] > 0, v[:, :, V - 1], v[:, :, V - 2])
                top_cols = {tuple(table[int(i)]) for i in np.unique(top_v[free])}
                bad = top_cols & {tuple(spec[5]['rim'][0]), tuple(spec[5]['rim'][1]), (0, 0, 0)}
                if bad or len(top_cols) < 2 or not (v[1:V - 1, 1:V - 1, V - 2] > 0).all() or (v[:, :, V][free] > 0).any():
                    print('NG: %s の上面（柵の色・黒 %s／色の数 %d／へこみが 2 段以上か出っ張りがある）' % (name, sorted(bad), len(top_cols)))
                    ok = False
                cap = spec[5]['rim'][1]
                if not all(tuple(table[int(i)]) == tuple(cap) for i in np.unique(v[:, :, RIM_TOP - 1][m & ~foot])):
                    print('NG: %s の柱の頭の色が違う' % name)
                    ok = False
                if jn:
                    if (foot & m).any():
                        print('NG: %s の交差の頭の底が柵の柱と重なる' % name)
                        ok = False
                    # 頭のボクセルの色が junction_head の色どおり
                    head = junction_head(spec[5]['sys'])
                    bad_col = [k for k, col in head.items() if tuple(table[int(v[k[0], k[1], JUNC_Z0 + k[2]])]) != col]
                    if bad_col:
                        print('NG: %s の交差の頭の色が %d か所違う' % (name, len(bad_col)))
                        ok = False
            holes = int((~occ[:, :, :V].any(axis=1)).sum() + (~occ[:, :, :V].any(axis=0)).sum())        # 柵の上の段は数えない
            if holes:
                print('NG: %s に向こうが見える穴が %d 列' % (name, holes))
                ok = False
            front = occ[:, V - 1, :]
            carved = int((~front[1:V - 1, :V - 1]).sum())
            if carved == 0:
                print('NG: %s の南面にへこみが 1 つも無い（凹凸が付いていない）' % name)
                ok = False
        elif kind in GROUND_KINDS:
            if origin[2] + v.shape[2] > (4 if kind == 'ladder_down' else 0):       # 下り梯子は縁から棒が 4 ボクセル出る
                print('NG: %s の天面が地面より高い（原点 z %d / 厚み %d）' % (name, origin[2], v.shape[2]))
                ok = False
            e = np.concatenate([v[0, :, :], v[-1, :, :], v[:, 0, :], v[:, -1, :]])
            tops = {int(np.nonzero(r)[0].max()) for r in e}
            edge_tops[name] = tops
            if len(tops) != 1:
                print('NG: %s の縁の高さが揃っていない %s（隣のマスと段差が出る）' % (name, tops))
                ok = False
        elif kind == 'door':
            holes = int((~occ.any(axis=0 if spec[5]['ew'] else 1)).sum())
            if spec[5]['mode'] == 'open' and holes == 0:
                print('NG: %s は開いた扉なのに向こうが見えない' % name)
                ok = False
            if spec[5]['mode'] == 'closed' and holes:
                print('NG: %s は閉じた扉なのに向こうが見える列が %d' % (name, holes))
                ok = False
        if kind in ('altar', 'grave', 'throne', 'sink', 'fountain', 'tree', 'bars', 'ladder_up', 'decal', 'stairs_up'):
            if not occ[:, :, :BURIED].any():
                print('NG: %s の埋まる部分（z < %d）が空（地面との間にすき間ができる）' % (name, BURIED))
                ok = False
    if not n_float:
        print('OK: 浮いた塊が無い（%d 個すべて、つながった塊が最下段に届く）' % len(SPECS))
    groups = {}
    for n, t in edge_tops.items():
        kind = next(s[1] for s in SPECS if s[0] == n)
        key = 'floor' if kind == 'floor' else ('liquid' if next(s for s in SPECS if s[0] == n)[5].get('liquid') else kind)
        groups.setdefault(key, {})[n] = sorted(t)
    for g, d in groups.items():
        if g in ('floor', 'liquid') and len({tuple(x) for x in d.values()}) > 1:
            print('NG: %s の縁の高さが種類どうしで違う: %s' % (g, d))
            ok = False
    if 'floor' in groups:
        print('OK: 床・通路 %d 個の縁の高さがすべて同じ（%s）' % (len(groups['floor']), next(iter(groups['floor'].values()))))
    print('check: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description='NetHack の地形のタイルをボクセルのプレハブにする（本番）')
    ap.add_argument('names', nargs='*')
    ap.add_argument('--out', default=DEFAULT_OUT, help='出力先（既定 assets/voxel。名前は nh_ で始まる）')
    ap.add_argument('--view-out', default=DEFAULT_VIEW,
                    help='床の板の種類を床の厚みだけ持ち上げた確認用の出力。確認画面は地面の板が床を隠すため（DCSS の --raise-floors と同じ）')
    ap.add_argument('--report-dir', default=DEFAULT_REPORT, help='作った物の一覧（manifest）と使った絵の一覧を置く所')
    ap.add_argument('--lang', choices=('en', 'ja'), default='en', help='タイルの置き場 nethack/data/<言語>/（地形の絵は同じ）')
    ap.add_argument('--no-view', action='store_true')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--check', action='store_true')
    opt = ap.parse_args(argv)
    set_data_dir(os.path.join(REPO, 'nethack', 'data', opt.lang))
    if opt.list:
        names = C.load_names()
        for s in SPECS:
            print('%-28s %-12s 絵 %-5d %s' % (s[0], s[1], s[2], names[s[2]]))
        print(len(SPECS), '個')
        return 0
    if opt.check:
        return run_check(os.path.join(WORK2, 'out', '_check_terrain'))
    only = set(opt.names) or None
    bad = [n for n in (only or ()) if n not in set(NAMES)]
    if bad:
        ap.error('名前が無い: %s' % bad)
    build_all(opt.out, False, only, report_dir=opt.report_dir)
    if not opt.no_view:
        build_all(opt.view_out, True, only, quiet=True)
        print('確認用（床の板の種類を持ち上げた）→', opt.view_out)
    if dv.REDUCED:
        print('色を詰めた絵:', dv.REDUCED)
    return 0


if __name__ == '__main__':
    sys.exit(main())
