# -*- coding: utf-8 -*-
"""DCSS の地形を HD2D の地形番号へ仕分けて `feature_map.csv` を作る／照合する。

設計と数は `dcss/terrain/README.md`。

    python -u dcss/terrain/build_feature_map.py            # 照合（既存の CSV と作り直した結果を比べる）
    python -u dcss/terrain/build_feature_map.py --write    # CSV を作り直す
    python -u dcss/terrain/build_feature_map.py --stats    # 数を出す（README の数字の出どころ）
    python -u dcss/terrain/build_feature_map.py --src D:\\path\\crawl-ja   # 上流の写しの場所を替える

## 何を読むか（どれも書き換えない）

| 元 | 取るもの |
| --- | --- |
| `crawl-ref/source/dungeon-feature-type.h` | 地形の定義名と番号（`TAG_MAJOR_VERSION==34`。`tag-version.h` の既定） |
| `crawl-ref/source/feature-data.h` | 英名・原作の旗（`FFT_*`）・地図の種別（`MF_*`）。マクロ（`TRAP`・`ALTAR`・`PORTAL_*`・`BRANCH_*`・`FOUNTAIN`・`STONE_STAIRS_*`）を展開する |
| `crawl-ref/source/tilepick.cc` | 地形 → タイル名（`tileidx_feature_base`） |
| `crawl-ref/source/rltiles/dc-feat.txt` `dc-wall.txt` `dc-floor.txt` | タイル名 → PNG（`%sdir` と、名前の無い行＝直前のタイルの変種） |
| `crawl-ref/source/dat/database/ja/jtrans/data-features.txt` | 日本語名（キーは英名） |
| `lib/edit/TerrainDefinitions.jsonc`（この開発ツリー） | 寄せ先の変愚の地形の存在確認 |
| `assets/voxel/terrain_prefabs.jsonc`（この開発ツリー） | 予約帯が他のコアの番号とぶつからないことの確認 |

**仕分けの決め（下の `put(...)` の列）はこのファイルが正本**で、CSV は生成物である。
手で CSV を直したら、ここも直すこと（照合が食い違いを出す）。
"""
from __future__ import annotations

import argparse
import csv
import io
import json
import os
import re
import sys
from collections import Counter, OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT_CSV = os.path.join(HERE, 'feature_map.csv')
DEFAULT_SRC = os.path.join(os.path.dirname(REPO), 'roguelike-cores', 'build-sources', 'dcss')
TAG = 34

#: 予約帯。b・c の行の番号は 2000 + DCSS の地形の値（0〜220）。
BAND = 2000
BAND_END = 2220
#: 派生番号（店の種別・条件つきのタイルなど。地形の値を持たない行）。2300〜2399 を予約。
DERIVED = 2300
DERIVED_END = 2399


# ============================================================ 上流を読む
def strip_tag(text):
    """`#if TAG_MAJOR_VERSION ==/> N` ... `#else` ... `#endif` を TAG の値で解く。"""
    out, stack = [], []
    for line in text.split('\n'):
        s = line.strip()
        m = re.match(r'#if\s+TAG_MAJOR_VERSION\s*(==|>)\s*(\d+)', s)
        if m:
            n = int(m.group(2))
            stack.append((TAG == n) if m.group(1) == '==' else (TAG > n))
            continue
        if s.startswith('#else'):
            stack[-1] = not stack[-1]
            continue
        if s.startswith('#endif'):
            stack.pop()
            continue
        if all(stack):
            out.append(line)
    return '\n'.join(out)


def read_enum(src):
    """dungeon_feature_type の (名前, 値) の並び。"""
    text = strip_tag(open(os.path.join(src, 'crawl-ref', 'source', 'dungeon-feature-type.h'), encoding='utf-8').read())
    names, val, inside = [], 0, False
    for line in text.split('\n'):
        s = re.sub(r'//.*', '', line).strip()
        if s.startswith('enum dungeon_feature_type'):
            inside = True
            continue
        if not inside or not s or s.startswith('#') or s in ('{', '};'):
            continue
        m = re.match(r'(\w+)\s*(?:=\s*(\d+))?\s*,?$', s)
        if not m:
            continue
        if m.group(2):
            val = int(m.group(2))
        if m.group(1) == 'NUM_FEATURES':
            return names, val
        names.append((m.group(1), val))
        val += 1
    raise RuntimeError('NUM_FEATURES が見つからない')


def split_args(s):
    out, cur, depth, quoted = [], '', 0, False
    for ch in s:
        if ch == '"':
            quoted = not quoted
        if not quoted:
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            elif ch == ',' and depth == 0:
                out.append(cur.strip())
                cur = ''
                continue
        cur += ch
    out.append(cur.strip())
    return out


MACROS = {  # 名前 -> (旗, 地図の種別)
    'TRAP': ('FFT_TRAP', 'MF_TRAP'),
    'PORTAL_ENTRANCE': ('FFT_NOTABLE', 'MF_PORTAL'),
    'PORTAL_EXIT': ('FFT_NONE', 'MF_PORTAL'),
    'BRANCH_ENTRANCE': ('FFT_NOTABLE', 'MF_STAIR_BRANCH'),
    'BRANCH_EXIT': ('FFT_NONE', 'MF_STAIR_UP'),
    'ALTAR': ('FFT_NOTABLE', 'MF_FEATURE'),
    'FOUNTAIN': ('FFT_NONE', 'MF_FLOOR'),
}


def read_feature_data(src):
    """定義名 -> {name, vault, flags, mf}。"""
    text = strip_tag(open(os.path.join(src, 'crawl-ref', 'source', 'feature-data.h'), encoding='utf-8').read())
    text = re.sub(r'#define[^\n]*(?:\\\n[^\n]*)*', '', text)
    text = re.sub(r'//[^\n]*', '', text)
    out = {}
    for m in re.finditer(r'\{\s*(DNGN_\w+),\s*"([^"]*)",\s*"([^"]*)",(.*?)\n\},', text, re.S):
        fl = re.search(r'\n\s*([^\n]*?FFT_[^\n]*?),\s*(MF_\w+),', m.group(4))
        out[m.group(1)] = dict(name=m.group(2), vault=m.group(3),
                               flags=fl.group(1).strip() if fl else '', mf=fl.group(2) if fl else '')
    for m in re.finditer(r'^(TRAP|PORTAL_ENTRANCE|PORTAL_EXIT|BRANCH_ENTRANCE|BRANCH_EXIT|ALTAR|FOUNTAIN)\((.*?)\),\s*$',
                         text, re.S | re.M):
        a = split_args(m.group(2))
        fl, mf = MACROS[m.group(1)]
        out[a[0]] = dict(name=a[1].strip('"'), vault=a[2].strip('"'), flags=fl, mf=mf)
    for m in re.finditer(r'^STONE_STAIRS_(DOWN|UP)\((\w+),\s*(\w+)\),', text, re.M):
        d = m.group(1)
        out['DNGN_STONE_STAIRS_%s_%s' % (d, m.group(2))] = dict(
            name='stone staircase leading %s' % d.lower(), vault='stone_stairs_%s_%s' % (d.lower(), m.group(3)),
            flags='FFT_NONE', mf='MF_STAIR_' + d)
    return out


def read_tile_switch(src):
    """地形 -> タイル名の並び（`tileidx_feature_base`。`cond ? A : B` は A・B 両方）。"""
    text = open(os.path.join(src, 'crawl-ref', 'source', 'tilepick.cc'), encoding='utf-8').read()
    a = text.index('tileidx_t tileidx_feature_base')
    b = text.index('bool is_torch_tile')
    body = strip_tag(text[a:b])
    out, pend = {}, []
    for m in re.finditer(r'case (DNGN_\w+):|return([^;]*);', body):
        if m.group(1):
            pend.append(m.group(1))
        else:
            tiles = re.findall(r'TILE_\w+', m.group(2))
            if pend and tiles:
                for p in pend:
                    out[p] = tiles
            pend = []
    return out


def read_tile_files(src):
    """タイル名 -> PNG（rltiles からの相対）の並び。"""
    rl = os.path.join(src, 'crawl-ref', 'source', 'rltiles')
    res = {}
    for fn in ('dc-feat.txt', 'dc-wall.txt', 'dc-floor.txt'):
        sdir, cur = '', []
        for line in open(os.path.join(rl, fn), encoding='utf-8'):
            line = line.rstrip()
            if not line.strip() or line.startswith('#'):
                continue
            if line.startswith('%sdir'):
                sdir = line.split(None, 1)[1].strip()
                continue
            if line.startswith('%'):
                continue
            parts = line.split()
            names = [p for p in parts[1:] if re.match(r'^[A-Z0-9_]+$', p)]
            if names:
                cur = ['TILE_' + p for p in names]
                for c in cur:
                    res.setdefault(c, [])
            if not cur:
                continue
            # rltiles のツールは「sdir/名前」を先に試し、無ければ rltiles 直下の「名前」を試す
            cands = [(sdir + '/' + parts[0] + '.png') if sdir else parts[0] + '.png', parts[0] + '.png']
            path = next((c for c in cands if os.path.exists(os.path.join(rl, c))), cands[0])
            for c in cur:
                res[c].append(path)
    return res


def read_ja_names(src):
    path = os.path.join(src, 'crawl-ref', 'source', 'dat', 'database', 'ja', 'jtrans', 'data-features.txt')
    text = open(path, encoding='utf-8').read()
    out = {}
    for m in re.finditer(r'%%%%\n\[feature\](.*?)\n\n(.*?)(?=\n%%%%|\Z)', text, re.S):
        out[m.group(1).strip()] = m.group(2).strip()
    return out


def read_hengband():
    """変愚の地形 id -> キー。"""
    text = open(os.path.join(REPO, 'lib', 'edit', 'TerrainDefinitions.jsonc'), encoding='utf-8').read()
    text = re.sub(r'^\s*//.*$', '', text, flags=re.M)
    text = re.sub(r',(\s*[}\]])', r'\1', text)
    return {a['id']: a['key'] for a in json.loads(text)['terrains']}


# ============================================================ 仕分け
A, B, C = 'a', 'b', 'c'
DOOR = 'DOOR'
STAIRS = 'STAIRS|PASSABLE'
NOWALK = '(WALL も PASSABLE も立てない)'
R = {}


def put(names, cls, hb, prefab, flags, kind, note=''):
    """形: 名前（空白区切り）-> (仕分け, 変愚の番号, プレハブ案, セル旗, 地図の種別, 備考)。"""
    for n in names.split():
        R['DNGN_' + n] = (cls, hb, prefab, flags, kind, note)


# ---- 疑似・旧版
put('UNSEEN EXPLORE_HORIZON TRAVEL_TRAIL', A, 0, '', '(なし。KNOWN も立てない)', 'Unknown', '地図に出ない疑似地形。描かない')
put('FLOOR', A, 1, '(役割の既定。ブランチごとの床は dungeon_styles)', 'KNOWN|PASSABLE', 'Floor',
    '床の見た目は役割の既定。DCSS に部屋と通路の区別は無いので CELL_FEAT_ROOM は立てない（README「共通の部分」）。ブランチごとの材は README の「ブランチごとの床と壁」')
put('UNDISCOVERED_TRAP BADLY_SEALED_DOOR', A, 1, '', 'KNOWN|PASSABLE', 'Floor',
    '旧版のセーブ互換用（TAG_MAJOR_VERSION==34 だけ）。新規の階には出ない。出たら床')
# ---- 扉
put('CLOSED_DOOR', A, 32, 'door_closed', DOOR, 'Door')
put('OPEN_DOOR', A, 4, 'door_open', 'DOOR|DOOR_OPEN|PASSABLE', 'Door')
put('BROKEN_DOOR', A, 5, 'door_broken', 'DOOR|DOOR_OPEN|PASSABLE', 'Door')
put('RUNED_DOOR', B, None, 'object=door_warded_sq_b(+_ew)。Sil-Q の護符の扉（青）', DOOR, 'Door', 'ルーンの扉。原作の色は薄い青')
put('SEALED_DOOR', B, None, 'object=door_warded_sq_g(+_ew)。Sil-Q の護符の扉（緑）', DOOR, 'Door', '封印の扉。原作の色は薄い緑')
put('CLOSED_CLEAR_DOOR', A, 202, 'door_glass_closed', DOOR, 'Door', '半透明の扉＝変愚のガラスの扉')
put('OPEN_CLEAR_DOOR', A, 200, 'door_glass_open', 'DOOR|DOOR_OPEN|PASSABLE', 'Door')
put('BROKEN_CLEAR_DOOR', A, 201, 'door_glass_broken', 'DOOR|DOOR_OPEN|PASSABLE', 'Door')
put('RUNED_CLEAR_DOOR', B, None, 'object=door_glass_closed(+_ew)、tint=[0.6,0.85,1.25]', DOOR, 'Door', '色違い（薄い水色）')
put('SEALED_CLEAR_DOOR', B, None, 'object=door_glass_closed(+_ew)、tint=[0.7,1.15,0.7]、emissive 弱', DOOR, 'Door', '色違い（薄い緑）')
# ---- 木
put('TREE', A, 96, 'tree_01〜10（変愚の木。地面は草）', 'TREE', 'Tree',
    '変愚の TREE の規則は地面が草。DCSS の木は床の上に立つので、床の種類までは再現しない')
put('MANGROVE', C, None, 'object=dc_tree_mangrove（tile 板）', 'TREE', 'Tree', '沼のブランチの木')
put('PETRIFIED_TREE', C, None, 'object=dc_tree_petrified（tile 板）', 'TREE', 'Tree', '石化した木。ゾットの像（未発見）の絵にもなる')
put('DEMONIC_TREE', C, None, 'object=dc_tree_demonic（tile 板）', 'TREE', 'Tree', 'アビス・パンデモニウムの木。原作の色は毎回ランダム（ETC_RANDOM）')
# ---- 壁
put('ROCK_WALL', A, 56, '(役割の既定＝structural_wall。ブランチごとの材は dungeon_styles の wall)', 'WALL', 'Wall',
    '原作の壁の絵はブランチごとに変わる（tileview.cc の tile_default_flv）。README の「ブランチごとの床と壁」')
put('STONE_WALL', A, 57, '(同上)', 'WALL', 'Wall', '積み石の壁。ブランチごとの絵が 20 種ほど（tilepick.cc の _apply_branch_tile_overrides）。役割の既定で描く')
put('PERMAROCK_WALL', A, 60, 'wall_perm_01〜10', 'WALL|PERMANENT', 'Wall')
put('CLEAR_ROCK_WALL CLEAR_STONE_WALL', A, 218, '(変愚のガラスの壁と同じ＝役割の既定の壁)', 'WALL', 'Wall',
    '半透明の壁。変愚のガラスの壁には専用の規則が無く、通常の壁と同じ絵になる')
put('CLEAR_PERMAROCK_WALL', A, 219, '(同上)', 'WALL|PERMANENT', 'Wall')
put('METAL_WALL', C, None, 'structure=dc_wall_metal_01〜（tile 板を立方体の面に貼る）', 'WALL', 'Wall', '金属の壁。ブランチの違い（宝物庫・深層・ディース）は後回し')
put('CRYSTAL_WALL', C, None, 'structure=dc_wall_crystal_01〜', 'WALL', 'Wall', '水晶の壁（緑）')
put('SLIMY_WALL', B, None, 'structure=wall_stone_01〜10、tint=[0.7,1.1,0.5]', 'WALL', 'Wall', 'スライムに覆われた壁。色違いで足りる')
put('FRIGID_WALL', B, None, 'structure=wall_ice_fc_01〜03（Frox の氷の壁）', 'WALL', 'Wall', '極寒の壁')
put('SPIKE_LAUNCHER', C, None, 'structure=dc_wall_spike_launcher（tile 板）', 'WALL', 'Wall', '棘を撃つ壁の罠')
put('GRATE', C, None, 'object=dc_grate（tile 板。向こうが見える）', '(WALL も PASSABLE も立てない)', 'Wall',
    '鉄格子。歩けないが視線は通る。壁ブロックにはしない（README「共通の部分」）')
# ---- 海・溶岩・水
put('OPEN_SEA', A, 83, 'water_deep', 'WATER', 'Water', 'ブランチの縁。歩けない（PASSABLE を立てない）')
put('LAVA_SEA', A, 85, 'lava_crust+lava_bubble', 'LAVA|GLOW', 'Lava', 'ブランチの縁。歩けない')
put('ENDLESS_SALT', C, None, 'ground=dc_floor_salt_01〜03（tile から色を取る）', 'KNOWN', 'Floor', '砂漠ブランチの縁。歩けない。白い塩の床')
put('LAVA', A, 85, 'lava_crust+lava_bubble', 'LAVA|GLOW|PASSABLE', 'Lava')
put('DEEP_WATER', A, 83, 'water_deep', 'WATER|PASSABLE', 'Water')
put('SHALLOW_WATER', A, 84, 'water_shallow', 'WATER|PASSABLE', 'Water')
put('TOXIC_BOG', B, None, 'ground=water_shallow、tint=[0.55,0.85,0.35]（変愚の沼地 194 より濃い緑）', 'WATER|PASSABLE', 'Water', '毒沼。浅い水と同じ作りで色を替える')
put('MUD', B, None, 'ground=ground_mud_out_01〜04（Outpost の泥）', 'PASSABLE', 'Floor', '泥')
put('DECORATIVE_FLOOR', C, None, 'ground=dc_floor_decor_01〜03（tile から作る）', 'PASSABLE', 'Floor', '装飾された床')
# ---- 像
put('ORCISH_IDOL', C, None, 'object=dc_statue_orc_idol（tile 板）', NOWALK, 'Wall', '歩けない物。壁ブロックにはしない')
put('GRANITE_STATUE', B, None, 'ground=floor_room_01、object=statue_worn', NOWALK, 'Wall', '花崗岩の像。歩けない物')
put('METAL_STATUE', B, None, 'ground=floor_room_01、object=statue_worn、tint=[0.7,0.85,1.1]', NOWALK, 'Wall', '金属の像。色違い')
put('ZOT_STATUE', C, None, 'object=dc_statue_zot_fire（派生 3 種＋石化した木の絵）', NOWALK, 'Wall',
    'どのオーブの怪物かで絵が分かれる（tilepick.cc）。基本の行は火の絵。未発見の間は石化した木の絵')
put('MALIGN_GATEWAY', C, None, 'object=dc_gate_malign（tile 板）', NOWALK, 'Wall', '怪物を呼ぶ門（歩けない。SOLID）')
# ---- 罠
put('TRAP_MECHANICAL TRAP_BOLT', C, None, 'object=dc_trap_bolt（床に平らな板）', 'PASSABLE', 'Floor', 'クロスボウの罠。TRAP_MECHANICAL は旧版でこれと同じ絵')
put('TRAP_SPEAR', C, None, 'object=dc_trap_spear', 'PASSABLE', 'Floor', '槍の罠')
put('TRAP_NET', C, None, 'object=dc_trap_net', 'PASSABLE', 'Floor', '網の罠')
put('TRAP_PLATE', C, None, 'object=dc_trap_plate', 'PASSABLE', 'Floor', '足踏みスイッチ')
put('TRAP_TELEPORT', C, None, 'object=dc_trap_teleport', 'PASSABLE', 'Floor')
put('TRAP_TELEPORT_PERMANENT', C, None, 'object=dc_trap_teleport_perm', 'PASSABLE', 'Floor')
put('TRAP_SHAFT', B, None, 'ground=ground_chasm_sq_01〜03（Sil-Q の深淵。3 マス下まで掘ってある）', 'PASSABLE', 'Floor', '縦穴。落ちる穴なので穴の絵は嘘でない')
put('TRAP_WEB', B, None, 'object=web_sq(+_ew)（Sil-Q・Frox と同じ蜘蛛の巣）', 'PASSABLE', 'Floor', 'Frox の FC_WEB と同じ寄せ方')
put('TRAP_ALARM', C, None, 'object=dc_trap_alarm', 'PASSABLE', 'Floor')
put('TRAP_ZOT', C, None, 'object=dc_trap_zot', 'PASSABLE', 'Floor')
put('PASSAGE_OF_GOLUBRIA', C, None, 'object=dc_trap_golubria', 'PASSABLE', 'Floor', 'ゴルブリアの通り道（TELEPORTER も同じ絵）')
put('TRAP_DISPERSAL', C, None, 'object=dc_trap_dispersal', 'PASSABLE', 'Floor')
put('TRAP_DISPERSAL_INACTIVE', C, None, 'object=dc_trap_dispersal_off', 'PASSABLE', 'Floor', '充填中')
put('TRAP_TYRANT', C, None, 'object=dc_trap_tyrant', 'PASSABLE', 'Floor')
put('TRAP_ARCHMAGE', C, None, 'object=dc_trap_archmage', 'PASSABLE', 'Floor')
put('TRAP_HARLEQUIN', C, None, 'object=dc_trap_harlequin', 'PASSABLE', 'Floor')
put('TRAP_DEVOURER', C, None, 'object=dc_trap_devourer', 'PASSABLE', 'Floor')
put('TRAP_SHADOW TRAP_SHADOW_DORMANT', A, 1, '', 'PASSABLE', 'Floor', '旧版のセーブ互換用。新規の階には出ない。出たら床')
# ---- 階段
put('STONE_STAIRS_DOWN_I STONE_STAIRS_DOWN_II STONE_STAIRS_DOWN_III', A, 7, 'stairs_down', STAIRS, 'Stairs', '3 種は同じ絵（原作でも同じタイル）')
put('STONE_STAIRS_UP_I STONE_STAIRS_UP_II STONE_STAIRS_UP_III', A, 6, 'stairs_up', STAIRS, 'Stairs', '3 種は同じ絵')
put('EXIT_DUNGEON', A, 6, 'stairs_up', STAIRS, 'Stairs', 'D:1 の出口（登ると冒険を終える）')
put('ESCAPE_HATCH_DOWN', C, None, 'object=dc_hatch_down（tile 板）', STAIRS, 'Stairs', '床の脱出ハッチ')
put('ESCAPE_HATCH_UP', C, None, 'object=dc_hatch_up（tile 板）', STAIRS, 'Stairs', '天井の脱出ハッチ。EXIT_GAUNTLET も同じ絵')
put('SEALED_STAIRS_UP', B, None, 'object=stairs_up、tint=[0.6,1.15,0.6]、emissive 弱', STAIRS, 'Stairs', '封印された上り経路')
put('SEALED_STAIRS_DOWN', B, None, 'object=stairs_down、tint=[0.6,1.15,0.6]、emissive 弱', STAIRS, 'Stairs', '封印された下り経路')
# ---- 店・門・その他
put('ENTER_SHOP', C, None, 'object=dc_shop_general（基本の行は雑貨屋。店の種別ごとに派生 7 種）', 'PASSABLE', 'Stairs',
    '店の入口。店の種別ごとに別タイル（派生番号は SHOP_* の行）')
put('ABANDONED_SHOP', B, None, 'ground=floor_room_01、object=arch_stone', 'PASSABLE', 'Floor', '放棄された店。出入りできない')
put('STONE_ARCH', B, None, 'ground=floor_room_01、object=arch_stone', 'PASSABLE', 'Floor', '石のアーチ。地獄の前室は別タイル（派生 1 種）')
put('EXPIRED_PORTAL', B, None, 'ground=floor_room_01、object=rubble_pile', 'PASSABLE', 'Floor', '崩壊した入口')
put('TELEPORTER', C, None, 'object=dc_trap_golubria', 'PASSABLE', 'Floor', '旧版。ゴルブリアの通り道と同じ絵')
put('TRANSPORTER', C, None, 'object=dc_transporter（tile 板）', 'PASSABLE', 'Stairs', '転送装置')
put('TRANSPORTER_LANDING', C, None, 'object=dc_transporter_landing（床に平らな板）', 'PASSABLE', 'Floor', '転送装置の着地点')
put('UNKNOWN_PORTAL', C, None, 'object=dc_gate_unknown（tile 板）', 'PASSABLE', 'Stairs', '感知しただけの店かポータル')
put('ENTER_LABYRINTH', C, None, 'object=dc_gate_portal（共通の門のタイル）', 'PASSABLE', 'Stairs', '旧版のセーブ互換用。新規の階には出ない')
put('EXIT_LABYRINTH', C, None, 'object=dc_hatch_up（天井のハッチと同じタイル）', STAIRS, 'Stairs', '旧版のセーブ互換用。新規の階には出ない')
# ---- 噴水・隠し場所ほか
put('FOUNTAIN_BLUE', B, None, 'ground=floor_room_01、object=fountain_basin_telb、tint=[0.8,0.95,1.25]', 'PASSABLE', 'Floor', '澄んだ水の噴水')
put('FOUNTAIN_SPARKLING', B, None, 'ground=floor_room_01、object=fountain_basin_telb、tint=[0.75,1.1,1.3]、emissive 弱', 'PASSABLE', 'Floor', 'きらめく水の噴水')
put('FOUNTAIN_BLOOD', B, None, 'ground=floor_room_01、object=fountain_basin_telb、tint=[1.3,0.55,0.55]', 'PASSABLE', 'Floor', '血の噴水')
put('FOUNTAIN_EYES', B, None, 'ground=floor_room_01、object=fountain_basin_telb、tint=[1.15,0.6,1.15]', 'PASSABLE', 'Floor', '目玉の噴水')
put('DRY_FOUNTAIN DRY_FOUNTAIN_BLUE DRY_FOUNTAIN_SPARKLING DRY_FOUNTAIN_BLOOD', B, None,
    'ground=floor_room_01、object=fountain_pillar、tint=[0.7,0.7,0.72]', 'PASSABLE', 'Floor', '涸れた噴水。旧版の 3 種も同じ絵')
put('CACHE_OF_FRUIT', C, None, 'object=dc_cache_fruit（床の物。tile 板）', 'PASSABLE', 'Floor', '果物の隠し場所')
put('CACHE_OF_MEAT', C, None, 'object=dc_cache_meat', 'PASSABLE', 'Floor', '肉の隠し場所')
put('CACHE_OF_BAKED_GOODS', C, None, 'object=dc_cache_baked', 'PASSABLE', 'Floor', '焼き菓子の隠し場所')
put('ORB_DAIS', B, None, 'ground=floor_room_01、object=pedestal_agd_lb', 'PASSABLE', 'Floor', 'オーブの台座')
put('RUNELIGHT', C, None, 'object=dc_rune_light（床に平らな板）、emissive', 'PASSABLE', 'Floor', 'ルーンの灯。光らせるのは規則の emissive（GLOW の旗は付けない。下の祭壇の注を参照）')
put('BINDING_SIGIL', C, None, 'object=dc_rune_binding_sigil（床に平らな板）', 'PASSABLE', 'Floor', '束縛の印')

# ---- 祭壇 29 種
# 光る祭壇に `GLOW` の旗は付けない（2026-10-21 遊ぶ人「祭壇が白く光りすぎる」）。`GLOW` は画面側（共通の部分）が
# 「マスいっぱいの白い自発光の平板 + 半径 3 の点光源」を足す旗で、店の入口と溶岩のために作られている。祭壇に付けると平板が金色の絵を覆って白く飛ぶ。
# 光り方は規則の `emissive`（`tools/dcss/gen_dcss_terrain_rules.py` の `GLOW_EMISSIVE`。色つきの弱い光）で出す。
GLOWING_ALTARS = ()
for n in ('ZIN SHINING_ONE KIKUBAAQUDGHA YREDELEMNUL XOM VEHUMET OKAWARU MAKHLEB SIF_MUNA TROG NEMELEX_XOBEH '
          'ELYVILON LUGONU BEOGH JIYVA FEDHAS CHEIBRIADOS ASHENZARI DITHMENOS GOZAG QAZLAL RU ECUMENICAL '
          'PAKELLAS USKAYAW HEPLIAKLQANA WU_JIAN IGNIS').split():
    note = '神ごとに別の絵（原作でも 1 柱 1 タイル）'
    if n == 'PAKELLAS':
        note = '旧版のセーブ互換用（TAG_MAJOR_VERSION==34 だけ）。新規の階には出ない'
    put('ALTAR_' + n, C, None, 'object=dc_altar_%s（tile 板）' % n.lower(),
        'PASSABLE|GLOW' if n in GLOWING_ALTARS else 'PASSABLE', 'Floor', note)
put('UNKNOWN_ALTAR', C, None, 'object=dc_altar_unknown（tile 板）', 'PASSABLE', 'Floor', '感知しただけの祭壇')


# ---- 入口・出口
def gate(names, note='', prefab=None):
    for n in names.split():
        put(n, C, None, prefab or 'object=dc_gate_%s（tile 板）' % n.lower(), STAIRS, 'Stairs', note)


gate('ENTER_DIS ENTER_GEHENNA ENTER_COCYTUS ENTER_TARTARUS ENTER_HELL', '地獄の入口（原作は門の絵。色は地獄ごとに違う）')
gate('EXIT_HELL', '地獄からダンジョンへ戻る門')
put('EXIT_DIS EXIT_GEHENNA EXIT_COCYTUS EXIT_TARTARUS', C, None, 'object=dc_gate_return_vestibule（4 つで共通のタイル）', STAIRS, 'Stairs',
    '前室の出口。4 つとも同じタイル（原作の色の違いは記号色だけ）')
gate('ENTER_ABYSS')
put('EXIT_THROUGH_ABYSS', C, None, 'object=dc_gate_enter_abyss（ENTER_ABYSS と同じタイル）', STAIRS, 'Stairs')
gate('EXIT_ABYSS ABYSSAL_STAIR ENTER_PANDEMONIUM EXIT_PANDEMONIUM TRANSIT_PANDEMONIUM')
gate('ENTER_VAULTS', '宝物庫の入口')
gate('ENTER_ZOT', 'ゾットの門。基本は閉じた絵、ゾットの 1 階へ一度入った後は開いた絵（派生 1 種）')
gate('EXIT_ZOT', 'ゾットの門の出口')
gate('ENTER_ORC ENTER_LAIR ENTER_SLIME ENTER_CRYPT ENTER_TEMPLE ENTER_SNAKE ENTER_ELF ENTER_TOMB ENTER_SWAMP ENTER_SHOALS '
     'ENTER_SPIDER ENTER_DEPTHS', '枝のダンジョンの入口')
gate('EXIT_ORC EXIT_LAIR EXIT_SLIME EXIT_CRYPT EXIT_TEMPLE EXIT_SNAKE EXIT_ELF EXIT_TOMB EXIT_SWAMP EXIT_SHOALS '
     'EXIT_SPIDER EXIT_DEPTHS', '枝のダンジョンの出口')
put('ENTER_DWARF ENTER_BLADE ENTER_FOREST', C, None, 'object=dc_gate_enter_legacy（3 つで共通のタイル）', STAIRS, 'Stairs', '旧版のセーブ互換用。新規の階には出ない')
put('EXIT_DWARF EXIT_BLADE EXIT_FOREST', C, None, 'object=dc_gate_return_legacy（3 つで共通のタイル）', STAIRS, 'Stairs', '旧版のセーブ互換用。新規の階には出ない')
gate('ENTER_ZIGGURAT ENTER_BAZAAR ENTER_TROVE ENTER_SEWER ENTER_OSSUARY ENTER_BAILEY ENTER_ICE_CAVE ENTER_VOLCANO '
     'ENTER_WIZLAB ENTER_DESOLATION ENTER_GAUNTLET ENTER_NECROPOLIS', 'ポータルの入口（原作ではダンジョンの枝ではなく部屋ごとの別マップ）')
put('ENTER_PORTAL_VAULT', C, None, 'object=dc_gate_portal（共通の門のタイル）', STAIRS, 'Stairs', '旧版のセーブ互換用。共通の門のタイル')
put('ENTER_ARENA ENTER_CRUCIBLE', C, None, 'object=dc_gate_portal（共通の門のタイル）', STAIRS, 'Stairs', '共通の門で代用')
put('EXIT_ZIGGURAT EXIT_BAZAAR EXIT_TROVE EXIT_ARENA EXIT_PORTAL_VAULT', C, None, 'object=dc_gate_portal（共通の門のタイル）', STAIRS, 'Stairs',
    'ポータルの出口。複数の出口が同じタイル')
gate('EXIT_SEWER EXIT_OSSUARY EXIT_BAILEY EXIT_VOLCANO EXIT_DESOLATION EXIT_NECROPOLIS EXIT_CRUCIBLE', 'ポータルの出口')
put('EXIT_ICE_CAVE', C, None, 'object=dc_gate_enter_ice_cave（入口と同じタイル）', STAIRS, 'Stairs', '氷の洞穴の出口は入口と同じタイル')
put('EXIT_WIZLAB', C, None, 'object=dc_gate_enter_wizlab（入口と同じタイル）', STAIRS, 'Stairs', '魔術師の研究所の出口は入口と同じタイル')
put('EXIT_GAUNTLET', C, None, 'object=dc_hatch_up（天井のハッチと同じタイル）', STAIRS, 'Stairs', '原作は天井の脱出ハッチと同じ絵を使う')
put('EXIT_VAULTS', C, None, 'object=dc_gate_exit_vaults', STAIRS, 'Stairs', '基本は開いた絵。宝物庫が閉じている間は閉じた絵（派生 1 種）')

# tilepick.cc の分岐つき return（自動抽出では最初の 1 つしか取れない物）と、専用タイルの無い物の代用
TILE_OVERRIDE = {
    'DNGN_STONE_ARCH': ['TILE_DNGN_STONE_ARCH', 'TILE_DNGN_STONE_ARCH_HELL'],
    'DNGN_ZOT_STATUE': ['TILE_DNGN_ZOT_FIRE_STATUE', 'TILE_DNGN_ZOT_WINTER_STATUE', 'TILE_DNGN_ZOT_ENTROPY_STATUE', 'TILE_DNGN_PETRIFIED_TREE'],
    'DNGN_ENTER_ZOT': ['TILE_DNGN_ENTER_ZOT_CLOSED', 'TILE_DNGN_ENTER_ZOT_OPEN'],
    'DNGN_EXIT_VAULTS': ['TILE_DNGN_EXIT_VAULTS_OPEN', 'TILE_DNGN_EXIT_VAULTS_CLOSED'],
}
TILE_FALLBACK = {
    'DNGN_ENTER_ARENA': 'TILE_DNGN_PORTAL',
    'DNGN_ENTER_CRUCIBLE': 'TILE_DNGN_PORTAL',
    'DNGN_ENTER_LABYRINTH': 'TILE_DNGN_PORTAL',
    'DNGN_EXIT_LABYRINTH': 'TILE_DNGN_ESCAPE_HATCH_UP',
    'DNGN_DRY_FOUNTAIN_BLUE': 'TILE_DNGN_DRY_FOUNTAIN',
    'DNGN_DRY_FOUNTAIN_SPARKLING': 'TILE_DNGN_DRY_FOUNTAIN',
    'DNGN_DRY_FOUNTAIN_BLOOD': 'TILE_DNGN_DRY_FOUNTAIN',
}
JA_OVERRIDE = {'DNGN_UNSEEN': '(未探索のマス)', 'DNGN_BADLY_SEALED_DOOR': '(名前なし。旧版の封印ドア)'}

#: 派生の行（地形の値を持たない。`DERIVED + 並びの番号`）
SHOPS = [('WEAPON', '武器屋', 'shop_weapon', 'SHOP_WEAPON・SHOP_WEAPON_ANTIQUE'),
         ('ARMOUR', '防具屋', 'shop_armour', 'SHOP_ARMOUR・SHOP_ARMOUR_ANTIQUE'),
         ('JEWELLERY', '宝飾店', 'shop_jewellery', 'SHOP_JEWELLERY'),
         ('BOOK', '書店', 'shop_books', 'SHOP_BOOK'),
         ('SCROLL', '巻物屋', 'shop_scrolls', 'SHOP_SCROLL'),
         ('POTION', '薬屋', 'shop_potions', 'SHOP_DISTILLERY'),
         ('GENERAL', '雑貨屋', 'shop_general', 'SHOP_GENERAL・SHOP_GENERAL_ANTIQUE')]


def derived_rows():
    rows = []

    def add(i, name, ja, cls, prefab, png, note, flags='PASSABLE', kind='Stairs'):
        rows.append([name, '', ja, cls, str(DERIVED + i), 'DC_' + name.replace('/', '_'), png, '派生: ' + note, prefab, flags, kind, '', ''])

    for i, (k, ja, f, src_name) in enumerate(SHOPS):
        add(i, 'ENTER_SHOP/%s' % k, '店（%s）' % ja, C, 'object=dc_shop_%s（tile 板）' % k.lower(),
            'dngn/shops/%s.png' % f, 'DNGN_ENTER_SHOP を店の種別で割る（%s）' % src_name)
    add(7, 'STONE_ARCH/HELL', '石のアーチ（地獄の前室）', B, 'ground=floor_room_01、object=arch_stone、tint=[1.2,0.7,0.6]',
        'dngn/gateways/stone_arch_hell.png', 'ブランチが地獄の前室のときの絵', 'PASSABLE', 'Floor')
    add(8, 'ENTER_ZOT/OPEN', 'ゾットの門（開）', C, 'object=dc_gate_enter_zot_open', 'dngn/gateways/enter_zot_open.png',
        'ゾットの 1 階へ一度入った後の絵（level_visited）。基本の DNGN_ENTER_ZOT 行は閉じた絵')
    add(9, 'EXIT_VAULTS/CLOSED', '宝物庫の門（閉）', C, 'object=dc_gate_enter_vaults_closed', 'dngn/gateways/enter_vaults_closed.png',
        '宝物庫が閉じている間の絵（vaults_is_locked）。基本の DNGN_EXIT_VAULTS 行は開いた絵')
    add(10, 'ZOT_STATUE/FIRE', 'ゾットの像（火）', C, 'object=dc_statue_zot_fire', 'dngn/statues/zot_fire_orb_statue.png',
        '火のオーブの怪物が分かった後（基本の DNGN_ZOT_STATUE 行と同じ絵）', NOWALK, 'Wall')
    add(11, 'ZOT_STATUE/WINTER', 'ゾットの像（冬）', C, 'object=dc_statue_zot_winter', 'dngn/statues/zot_winter_orb_statue.png',
        '冬のオーブの怪物が分かった後', NOWALK, 'Wall')
    add(12, 'ZOT_STATUE/ENTROPY', 'ゾットの像（エントロピー）', C, 'object=dc_statue_zot_entropy', 'dngn/statues/zot_entropy_orb_statue.png',
        'エントロピーのオーブの怪物が分かった後', NOWALK, 'Wall')
    add(13, 'ZOT_STATUE/UNKNOWN', 'ゾットの像（未発見）', C, 'object=dc_tree_petrified（石化した木と同じ）', 'dngn/trees/tree_petrified1.png',
        'まだ怪物が分からない間は石化した木の絵', NOWALK, 'Wall')
    return rows


#: 地形の絵を DCSS 風に作り直す先として、試作（`dcss/assets/voxel/`）が既にある物。a・b の行を後から c へ上げるときの目印。
UPGRADE = {
    'DNGN_CLOSED_DOOR': 'dc_door_closed',
    'DNGN_STONE_STAIRS_DOWN_I': 'dc_stairs_down', 'DNGN_STONE_STAIRS_DOWN_II': 'dc_stairs_down', 'DNGN_STONE_STAIRS_DOWN_III': 'dc_stairs_down',
    'DNGN_STONE_STAIRS_UP_I': 'dc_stairs_up', 'DNGN_STONE_STAIRS_UP_II': 'dc_stairs_up', 'DNGN_STONE_STAIRS_UP_III': 'dc_stairs_up',
    'DNGN_TREE': 'dc_tree',
    'DNGN_FOUNTAIN_BLUE': 'dc_fountain_blue',
}

COLUMNS = ['DCSSの定義名', '番号', '日本語名', '仕分け', '寄せ先', '寄せ先のキー', '元のタイル画像', '備考', 'プレハブ案', 'feature_flags案',
           'MinimapKind案', '権利の注意', '試作']
HEADER = [
    '# dcss/terrain/feature_map.csv — DCSS の地形 → HD2D の地形番号・プレハブ（設計は dcss/terrain/README.md）',
    '# 生成物: dcss/terrain/build_feature_map.py --write。仕分けの決めはそのスクリプトが正本（手で直したらスクリプトも直す）。',
    '# 元: roguelike-cores/upstream/crawl-ja（タグ 0.34.1-ja6）の crawl-ref/source の dungeon-feature-type.h・feature-data.h・tilepick.cc・rltiles/dc-*.txt。',
    '# 番号: TAG_MAJOR_VERSION==34（tag-version.h の既定）で数えた dungeon_feature_type の値（0〜220。20〜29 は穴）。',
    '# 定義名が正で、番号は照合用（上流が動いたら定義名で引き直す）。派生の行は地形の値を持たない（番号が空）。',
    '# 仕分け: a=変愚の既存の地形へそのまま寄せる / b=既存のプレハブを色・組み合わせで流用 / c=新しいプレハブを作る。',
    '# 寄せ先: aは変愚の地形番号（lib/edit/TerrainDefinitions.jsonc）。b・cは予約帯の新しい番号 = 2000 + 番号。派生行は 2300〜。',
    '# 元のタイル画像: crawl-ref/source/rltiles/ からの相対パス。複数コマ・変種があるものは先頭の 1 枚。',
    '# タイル板: タイル画像 1 枚を 1 画素 1 ボクセルで立てた板（tools/voxel/tile_to_slab.py の出力と同じ作り）。床に平らな板は同じ作りで寝かせたもの。',
    '# プレハブ案の名前は dc_ で始める（試作 dcss/assets/voxel/ の dc_altar_zin などと同じ。TOband の tb_ と同じ流儀）。',
    '# feature_flags案: アダプタが MapCellView::feature_flags に載せる CELL_FEAT_* の案。MinimapKind案: 地図の種別の案。',
    '# 権利の注意: そのタイルが来歴の一覧（github.com/crawl/tiles の TILES_UNDER_UNKNOWN_LICENSE.md）に載る行。原作で実際に使われているのでそのまま使う。突き合わせは dcss/assets/tiles_used_provenance.tsv。',
    '# 試作: dcss/assets/voxel/ に作ってあるプレハブの名前（tools/dcss/gen_dcss_terrain.py の出力。manifest.tsv）。a・b の行も DCSS のタイルから作った物にそろえてあり、そのとき仕分けは c になる。',
]


RIGHTS_NOTE = ('来歴の一覧に載る（CC0 と確認できない。原作で実際に使われているのでそのまま使う。'
               'dcss/assets/tiles_used_provenance.tsv）')


def read_listed():
    """来歴の一覧（github.com/crawl/tiles の TILES_UNDER_UNKNOWN_LICENSE.md）に載る絵のうち、本案件が使う絵の集合（rltiles からの相対パス）。
    `dcss/assets/tiles_used_provenance.tsv`（tools/dcss/tile_provenance.py が書く）から読む。載っても原作で実際に使われているので、
    そのまま使う。"""
    listed = set()
    path = os.path.join(REPO, 'dcss', 'assets', 'tiles_used_provenance.tsv')
    if os.path.exists(path):
        for line in open(path, encoding='utf-8'):
            if line.startswith('#') or not line.strip():
                continue
            cols = line.rstrip('\n').split('\t')
            if len(cols) > 1 and cols[1] == '一覧に載る':
                listed.add(cols[0])
    return listed


def read_made():
    """`dcss/assets/voxel/manifest.tsv`（tools/dcss/gen_dcss_terrain.py が書く）: 地形の定義名 -> (プレハブ名, 種類)。
    DCSS のタイルから作ったプレハブがある行は、仕分けの a・b も c（新しく作った物）にそろえる。"""
    made = {}
    path = os.path.join(REPO, 'dcss', 'assets', 'voxel', 'manifest.tsv')
    if os.path.exists(path):
        for line in open(path, encoding='utf-8'):
            if line.startswith('#') or not line.strip():
                continue
            cols = line.rstrip('\n').split('\t')
            if len(cols) >= 4:
                for d in cols[3].split():
                    made[d] = (cols[0], cols[1])
    return made


def made_prefab(pname, pkind):
    lead = {'wall': 'structure=', 'floor': 'ground=', 'liquid': 'ground=', 'pit': 'ground='}.get(pkind, 'object=')
    return '%s%s（DCSS のタイルから作った。%s）' % (lead, pname, pkind)


def read_prototypes():
    d = os.path.join(REPO, 'dcss', 'assets', 'voxel')
    if not os.path.isdir(d):
        return set()
    names = {os.path.splitext(f)[0] for f in os.listdir(d) if f.endswith('.jsonc')}
    return names | {re.sub(r'_\d+$', '', n) for n in names}       # 変種の _01 を落とした名前でも引ける


def build(src):
    names, num_features = read_enum(src)
    fd = read_feature_data(src)
    ts = read_tile_switch(src)
    tf = read_tile_files(src)
    ja = read_ja_names(src)
    hb = read_hengband()
    listed = read_listed()
    made = read_made()
    protos = read_prototypes()
    enum = OrderedDict((n, v) for n, v in names)
    problems = []
    for n in enum:
        if n not in fd:
            problems.append('feature-data.h に定義が無い: %s' % n)
        if n not in R:
            problems.append('仕分けが無い: %s' % n)
    for n in R:
        if n not in enum:
            problems.append('定義に無い名前を仕分けている: %s' % n)

    def tiles_of(name):
        t = TILE_OVERRIDE.get(name) or ts.get(name)
        if not t and name in TILE_FALLBACK:
            t = [TILE_FALLBACK[name]]
        return t

    rows = []
    for name, num in sorted(enum.items(), key=lambda kv: kv[1]):
        if name not in R or name not in fd:
            continue
        cls, hbid, prefab, flags, kind, note = R[name]
        f = fd[name]
        tl = tiles_of(name)
        png, nfile = '', 0
        if tl:
            files = tf.get(tl[0], [])
            png = files[0] if files else ''
            nfile = len(files)
            if not files:
                problems.append('タイル名に PNG が無い: %s %s' % (name, tl[0]))
        extra = []
        if tl and len(tl) > 1:
            extra.append('原作のタイルは条件で %d 種（%s）' % (len(tl), ' / '.join(tl)))
        if nfile > 1:
            extra.append('同じタイルの変種・コマ %d 枚' % nfile)
        if not tl:
            extra.append('原作に専用タイルが無い')
        elif name in TILE_FALLBACK:
            extra.append('原作に専用タイルが無い（TILE_DNGN_ERROR）。代用: %s' % TILE_FALLBACK[name])
        extra.append('原作の旗: %s / %s' % (f['flags'].replace('FFT_', '').replace(' ', '').strip('()') or 'NONE', f['mf'].replace('MF_', '')))
        text_note = '; '.join([x for x in [note] + extra if x])
        rights = RIGHTS_NOTE if (png and png in listed) else ''
        if name in made:                                  # DCSS のタイルから作ったプレハブがある行は c にそろえる
            prefab = made_prefab(*made[name])
            cls = C
        if cls == A:
            if hbid not in hb:
                problems.append('寄せ先の変愚の番号が無い: %s -> %s' % (name, hbid))
            dest, key = str(hbid), ('NONE（描かない）' if hbid == 0 else hb.get(hbid, ''))
        else:
            dest, key = str(BAND + num), 'DC_' + name.replace('DNGN_', '')
        jname = JA_OVERRIDE.get(name) or ja.get(f['name'], '')
        if not jname:
            problems.append('日本語名が無い: %s (%s)' % (name, f['name']))
        proto = ''
        pn = proto_names(prefab)
        if pn and all(x in protos for x in pn):
            proto = '試作済み: ' + ' '.join(pn)
        elif name in UPGRADE and UPGRADE[name] in protos:
            proto = '昇格先の試作: ' + UPGRADE[name]
        rows.append([name, num, jname, cls, dest, key, png, text_note, prefab, flags, kind, rights, proto])
    for r in derived_rows():
        d = r[0]
        if d in made:
            r[3] = C
            r[8] = made_prefab(*made[d])
            r[12] = '試作済み: ' + made[d][0]
        if r[6] and r[6] in listed:
            r[11] = RIGHTS_NOTE
        rows.append(r)
    return rows, num_features, problems, hb


def check_band(rows):
    """予約帯が、いまの地形の対応表（terrain_prefabs.jsonc）の id とぶつからないこと。"""
    text = open(os.path.join(REPO, 'assets', 'voxel', 'terrain_prefabs.jsonc'), encoding='utf-8').read()
    # DCSS の節（tools/dcss/gen_dcss_terrain_rules.py が書く）は自分の規則なので数えない。ほかのコアの id との衝突だけ見る
    a = text.find('// ---- DCSS（')
    b = text.find('// ---- DCSS ここまで ----')
    if a >= 0 and b > a:
        text = text[:a] + text[b:]
    used = {int(x) for x in re.findall(r'"id"\s*:\s*(\d+)', text)}
    mine = {int(r[4]) for r in rows if r[3] != A}
    bad = sorted(mine & used)
    out = []
    if bad:
        out.append('予約帯の番号が terrain_prefabs.jsonc の id とぶつかる: %s' % bad)
    if used & set(range(BAND, DERIVED_END + 1)):
        out.append('2000〜2399 に既に id がある: %s' % sorted(used & set(range(BAND, DERIVED_END + 1))))
    out.extend('番号が範囲外: %s' % r[0] for r in rows if r[3] != A and not (BAND <= int(r[4]) <= DERIVED_END))
    return out


def proto_names(prefab):
    """プレハブ案の文字列から、新しいプレハブの名前（dc_ で始まる。変種の _01 は落とす）を取り出す。"""
    out = []
    for m in re.findall(r'(?<![a-z0-9_])(dc_[a-z][a-z0-9_]*)', prefab):
        m = re.sub(r'_\d+$', '', m)
        if m not in out:
            out.append(m)
    return out


def new_prefabs(rows):
    """仕分け c の行が名指すプレハブ名を、行の並びのまま重複なしで返す。"""
    names = OrderedDict()
    for r in rows:
        if r[3] == C:
            for m in proto_names(r[8]):
                names.setdefault(m, []).append(r[0])
    return names


def render(rows):
    buf = io.StringIO()
    for h in HEADER:
        buf.write(h + '\n')
    w = csv.writer(buf, lineterminator='\n')
    w.writerow(COLUMNS)
    for r in rows:
        w.writerow(r)
    return buf.getvalue()


def stats(rows, num_features):
    base = [r for r in rows if r[1] != '']
    der = [r for r in rows if r[1] == '']
    print('地形の値の幅 %d（NUM_FEATURES）、定義名 %d、派生の行 %d、合計 %d 行' % (num_features, len(base), len(der), len(rows)))
    for label, sel in (('定義名', base), ('派生', der), ('合計', rows)):
        c = Counter(r[3] for r in sel)
        print('  %s: a=%d b=%d c=%d' % (label, c['a'], c['b'], c['c']))
    pre = new_prefabs(rows)
    groups = Counter(p.split('_')[1] for p in pre)
    print('新しいプレハブの名前 %d 種: %s' % (len(pre), ', '.join('%s %d' % kv for kv in sorted(groups.items(), key=lambda kv: -kv[1]))))
    tiles = {r[6] for r in rows if r[3] == C and r[6]}
    print('仕分け c の元タイル（先頭の 1 枚）の種類 %d' % len(tiles))
    lic = [r for r in rows if r[11]]
    print('権利の注意の行 %d（a %d・c %d）、試作がある行 %d' % (len(lic), len([r for r in lic if r[3] == A]), len([r for r in lic if r[3] == C]), len([r for r in rows if r[12]])))
    alt = [r for r in base if 'ALTAR' in r[0]]
    print('祭壇 %d、入口（ENTER_*、店を含む）%d、出口（EXIT_*）%d' % (
        len(alt), len([r for r in base if r[0].startswith('DNGN_ENTER_')]), len([r for r in base if r[0].startswith('DNGN_EXIT_')])))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default=DEFAULT_SRC, help='crawl-ja の写しの場所')
    ap.add_argument('--write', action='store_true')
    ap.add_argument('--stats', action='store_true')
    args = ap.parse_args(argv)
    rows, num_features, problems, _ = build(args.src)
    problems += check_band(rows)
    if problems:
        print('問題 %d 件:' % len(problems))
        for p in problems:
            print('  ' + p)
        return 1
    text = render(rows)
    if args.stats:
        stats(rows, num_features)
    if args.write:
        with open(OUT_CSV, 'w', encoding='utf-8', newline='') as fh:
            fh.write(text)
        print('書いた: %s（%d 行）' % (OUT_CSV, len(rows)))
        return 0
    if not os.path.exists(OUT_CSV):
        print('CSV が無い。--write で作る')
        return 1
    same = open(OUT_CSV, encoding='utf-8', newline='').read() == text
    print('feature_map.csv: %s（%d 行）' % ('最新' if same else '古い（--write で作り直す）', len(rows)))
    return 0 if same else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
