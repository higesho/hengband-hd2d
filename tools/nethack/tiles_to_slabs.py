# -*- coding: utf-8 -*-
"""NetHack の原作のタイル（16x16）を、ボクセルの板（`.vox` ＋ `.jsonc`）にする（本番の道具）。

決定:
- D19: 怪物・アイテム・キャラクタは、原作のタイルの絵から作るボクセルの板で出す。
- D22: 板は原作の絵の粒のまま（斜めの縁を丸めない）。粒はほかのコアと同じ 64（1 画素 = 4x4 ボクセル・厚み 4）。
- D25: タイルの絵の黒い縁取りは原作どおり残す（この道具は縁取りに手を付けない）。

板の作り方の正は `tools/voxel/tile_to_slab.py`（DCSS も同じ）。色は 255 まで・alpha のしきい値 110・厚み 4・接地（絵の底を z = 0 へ）。
最近傍の 4 倍（1 画素 → 4x4）にしてから、`tile_to_slab.py` の `build_slab`・`trim_volume` と `export_vox.py` の書き出しをそのまま呼ぶ
（試作の `s64` と `.vox` が 1 バイトも違わない。`--compare-prototype` が確かめる）。

## 何を板にするか

`tiles.json` の `tile_ranges`（両端を含む）のうち、次の 3 つ。地形ほか（`other`）と印（`decals`）は板にしない（地形はブロックの担当が別に作る）。

| 種類 | 範囲 | 中身 |
| --- | --- | --- |
| monster | 0〜788 | 怪物（男・女の 2 個ずつ）・プレイヤーの職業・見えない怪物の印 |
| object | 789〜1271 | アイテム（岩・重い鉄球・岩塊など床に出る物を含む） |
| statue | 1515〜2303 | 石像 |

**全く同じ絵（alpha を 0/255 に振り、透明な画素の色をそろえた後で同一）は 1 枚の板にまとめる。**板の名前は `NH` ＋ まとめた絵のうち**最も小さい番号**
（例 `NH0`）。表（`slabs.json`・`slabs.tsv`）が、絵の番号 → 板の名前を引く。名前の頭 `NH` は、ほかのコアの板
（`R` `K` `P` `RL`、Frox の `FR` `FK` `FP`、幻想蛮怒の `GA` `GK` `GP` `GR`、Angband の `AK` `AR`、DCSS の `dc` `tiles`）と重ならない。
全く透明な絵（不透明な画素が 0）は板にならない（表の `empty_tiles` に載る）。

## 出力（既定 `nethack/data/slab/`。`.gitignore` の `/nethack/data/` に入る＝追跡しない）

- `NH<番号>.vox` と `NH<番号>.jsonc`（板の本体。`voxels_per_cell` = 64）。
- `slabs.json`: 詳細（細かさ・絵のハッシュ・個数・`tile_to_slab`（絵の番号で引く配列。板にしない絵は null）・板ごとの統計）。
- `slabs.tsv`: コアが読む軽い表。`#` の行は注釈。1 行 = `絵の番号<TAB>板の名前`（板にする絵だけ）。
- `.slab_stamp.json`: 作り直しの要否の控え（絵の内容と設定と `tile_to_slab.py`・`export_vox.py` のハッシュ）。2 回目からは変わった板だけ書く。

原作のタイルの絵は NetHack General Public License の下にあり、板はその派生物なので**リポジトリでは追跡しない**。
組み立てのたびに `build_tiles.py` が作る `nethack/data/<言語>/tiles.png`（日英で絵は同じ）から作る。

## 使い方（numpy・scipy・Pillow の入った Python が要る。この環境では `python`（3.10）。`py`（3.13）には numpy が無い）

  python tools/nethack/tiles_to_slabs.py                       # 全部作る（既定 nethack/data/slab/）
  python tools/nethack/tiles_to_slabs.py --out <フォルダ>      # 出力先を変える
  python tools/nethack/tiles_to_slabs.py --tiles nethack/data/ja   # 絵の置き場（tiles.png と tiles.json のあるフォルダ。既定 nethack/data/en）
  python tools/nethack/tiles_to_slabs.py --force               # 控えを無視して全部作り直す
  python tools/nethack/tiles_to_slabs.py --ids 698,68 --out <フォルダ>   # 指定した絵の板だけ（表は書かない）
  python tools/nethack/tiles_to_slabs.py --list                # まとめ方を表示（板の名前・まとめた絵の番号）
  python tools/nethack/tiles_to_slabs.py --check               # 作った物が今の tiles.png から作れるか・数が合うか（作り直しはしない）
  python tools/nethack/tiles_to_slabs.py --check --deep        # さらに、全部を一時の場所へ作り直して .vox/.jsonc をバイト単位で比べる
  python tools/nethack/tiles_to_slabs.py --compare-prototype <試作 s64 のフォルダ>   # 試作（nh_*）の .vox と同じ絵の板を比べる

終了コード: 0 = 成功／合格、1 = 失敗。
"""
import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
import os
import re
import shutil
import struct
import sys
import time

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import numpy as np  # noqa: E402

import nh_tile_common as C  # noqa: E402

PREFIX = 'NH'
SIDE = 64               # 板の一辺（ボクセル）。voxels_per_cell にも同じ値を書く
SCALE = SIDE // C.TILE  # 1 画素 = SCALE x SCALE ボクセル（4）
DEPTH = 4               # 厚み（ボクセル）。SIDE // 16
TABLE_FORMAT = 1
REV = 1                 # この道具の作り方を変えたら上げる（控えに入る）
DEFAULT_OUT = os.path.join(C.ROOT, 'nethack', 'data', 'slab')
SCRATCH = os.path.join(C.ROOT, '.taskwork', 'nethack_slabs')
NAME_RE = re.compile(r'^%s(\d+)\.(vox|jsonc)$' % PREFIX)

#: (絵の番号, 期待する絵の名前の一部, 何の物か)。取り違えの検査（名前の表が作れるときだけ）。偶数が男の絵。
CHECK_SPECS = [
    (698, 'valkyrie,male', 'プレイヤー: ヴァルキリー'),
    (700, 'wizard,male', 'プレイヤー: 魔法使い'),
    (68, 'kitten,male', '怪物: 子猫'),
    (24, 'jackal,male', '怪物: ジャッカル'),
    (144, 'goblin,male', '怪物: ゴブリン'),
    (300, 'red dragon,male', '怪物: 赤いドラゴン'),
    (843, 'long sword', 'アイテム: 長剣'),
    (1106, 'black / full healing', 'アイテム: 薬（黒い瓶）'),
    (1157, 'parchment / dig', 'アイテム: 巻物（羊皮紙）'),
    (977, 'ruby / conflict', 'アイテム: 指輪（ルビー）'),
    (1539, 'statue: jackal,male', '石像: ジャッカル'),
]


def slab_name(tile):
    return '%s%d' % (PREFIX, tile)


# ------------------------------------------------------------------ 試作との互換（`build_nethack_scene.py`（試作のシーン）が使う）
#: 試作の細かさ比べの定義（D22 で s64 に決まった。本番の道具は s64 だけを作る）。シーンの試作がまだ読むので残してある。
LEVELS = [('s16', 16, 'nearest'), ('s32', 32, 'nearest'), ('s64', 64, 'nearest'),
          ('s32_smooth', 32, 'smooth'), ('s64_smooth', 64, 'smooth')]
#: 試作の名簿（板の名前, 絵の番号, 期待する絵の名前, 何の物か）。
SPECS = [
    ('nh_pc_valkyrie', 698, 'valkyrie,male', 'プレイヤー: ヴァルキリー'),
    ('nh_pc_wizard', 700, 'wizard,male', 'プレイヤー: 魔法使い'),
    ('nh_mon_kitten', 68, 'kitten,male', '怪物: 子猫'),
    ('nh_mon_jackal', 24, 'jackal,male', '怪物: ジャッカル'),
    ('nh_mon_goblin', 144, 'goblin,male', '怪物: ゴブリン'),
    ('nh_mon_red_dragon', 300, 'red dragon,male', '怪物: 赤いドラゴン'),
    ('nh_obj_long_sword', 843, 'long sword', 'アイテム: 長剣'),
    ('nh_obj_potion_black', 1106, 'black / full healing', 'アイテム: 薬（黒い瓶。原作の見た目の名前は black）'),
    ('nh_obj_scroll_parchment', 1157, 'parchment / dig', 'アイテム: 巻物（羊皮紙。見た目の名前は parchment）'),
    ('nh_obj_ring_ruby', 977, 'ruby / conflict', 'アイテム: 指輪（ルビー。見た目の名前は ruby）'),
]


def scale2x(img):
    """Scale2x（EPX）。(行, 列, 4) の RGBA を 2 倍にする（試作の smooth 用。本番では使わない。D22）。"""
    h, w = img.shape[:2]
    key = np.where(img[..., 3:4] > 0, img, 0).astype(np.int64)
    flat = key[..., 0] * 16777216 + key[..., 1] * 65536 + key[..., 2] * 256 + key[..., 3]
    pad = np.pad(flat, 1, mode='edge')
    A, D = pad[0:h, 1:w + 1], pad[2:h + 2, 1:w + 1]
    Cc, B = pad[1:h + 1, 0:w], pad[1:h + 1, 2:w + 2]
    src = np.where(img[..., 3:4] > 0, img, 0).astype(np.uint8)
    padimg = np.pad(src, ((1, 1), (1, 1), (0, 0)), mode='edge')
    imA, imD = padimg[0:h, 1:w + 1], padimg[2:h + 2, 1:w + 1]
    imC, imB = padimg[1:h + 1, 0:w], padimg[1:h + 1, 2:w + 2]
    out = np.zeros((h * 2, w * 2, 4), np.uint8)
    e0 = (Cc == A) & (Cc != D) & (A != B)
    e1 = (A == B) & (A != Cc) & (B != D)
    e2 = (D == Cc) & (D != B) & (Cc != A)
    e3 = (B == D) & (B != A) & (D != Cc)
    out[0::2, 0::2] = np.where(e0[..., None], imA, src)
    out[0::2, 1::2] = np.where(e1[..., None], imB, src)
    out[1::2, 0::2] = np.where(e2[..., None], imC, src)
    out[1::2, 1::2] = np.where(e3[..., None], imD, src)
    return out


def make_pixels(tile, side, scaler):
    """16x16 の絵 → side x side。scaler: 'nearest' / 'smooth'（試作用。本番は最近傍の 4 倍だけ）。"""
    k = side // C.TILE
    if scaler == 'nearest' or k == 1:
        return C.upscale(tile, k)
    out = tile
    for _ in range(k.bit_length() - 1):
        out = scale2x(out)
    return out


# ------------------------------------------------------------------ tile_to_slab.py の読み込み
def load_t2s():
    spec = importlib.util.spec_from_file_location('tile_to_slab', os.path.join(C.ROOT, 'tools', 'voxel', 'tile_to_slab.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def tool_rev():
    """作り方の版。`tile_to_slab.py`・`export_vox.py` の中身と設定のハッシュ。変わったら全部作り直す。"""
    h = hashlib.sha1()
    for fn in ('tile_to_slab.py', 'export_vox.py'):
        with open(os.path.join(C.ROOT, 'tools', 'voxel', fn), 'rb') as fp:
            h.update(fp.read())
    h.update(('%d|%d|%d|%d|flat|trim' % (REV, SIDE, DEPTH, TABLE_FORMAT)).encode())
    return h.hexdigest()


def file_sha1(path):
    h = hashlib.sha1()
    with open(path, 'rb') as fp:
        h.update(fp.read())
    return h.hexdigest()


# ------------------------------------------------------------------ 絵のまとめ方
def group_tiles(sheet, meta):
    """板にする絵を同一の絵ごとにまとめる。戻り値: (まとめの一覧, 全く透明な絵の番号の一覧, 絵の番号 → 種類)。
    まとめ = {'name', 'tiles'（昇順）, 'key'（絵のハッシュ）, 'tile'（板にする 16x16 RGBA）, 'opaque'（不透明な画素数）}。名前は最小の番号から付ける。"""
    kinds = C.slab_tile_kinds(meta)
    by_key = {}
    empty = []
    for i in sorted(kinds):
        t = C.normalize_tile(C.tile_rgba(sheet, i))
        opaque = int((t[..., 3] > 0).sum())
        if opaque == 0:
            empty.append(i)
            continue
        key = hashlib.sha1(t.tobytes()).hexdigest()
        g = by_key.get(key)
        if g is None:
            by_key[key] = {'name': slab_name(i), 'tiles': [i], 'key': key, 'tile': t, 'opaque': opaque}
        else:
            g['tiles'].append(i)
    groups = sorted(by_key.values(), key=lambda g: g['tiles'][0])
    return groups, empty, kinds


# ------------------------------------------------------------------ 1 枚作る（別プロセスで走る）
_T2S = None


def _init_worker():
    global _T2S
    _T2S = load_t2s()


def bake_group(job):
    """job = (板の名前, 16x16 RGBA, 出力先)。`tile_to_slab.py` の `bake_one` と同じ手順（絵を PNG にせず直接渡す）。"""
    name, tile, out_dir = job
    t2s = _T2S
    pix = C.upscale(tile, SCALE)
    vol, palette, stat = t2s.build_slab(pix, DEPTH, 'flat', True)
    vol, at = t2s.trim_volume(vol)
    parts = [('main', vol, at)]
    size = t2s.ex.write_vox(os.path.join(out_dir, name + '.vox'), parts, palette)
    meta = [{'name': 'main', 'voxels': 'main', 'grounded': True, 'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    fp = t2s.ex.write_prefab(os.path.join(out_dir, name + '.jsonc'), name, parts, meta,
                             note='NetHack のタイル %s をボクセルの板にしたもの（tools/nethack/tiles_to_slabs.py）。'
                                  '一辺 %d / 厚み %d / flat' % (name[len(PREFIX):], SIDE, DEPTH),
                             voxels_per_cell=SIDE)
    return {'name': name, 'voxels': stat['voxels'], 'colours': stat['colours'], 'reduced_from': stat['reduced_from'],
            'size': 'x'.join(str(n) for n in vol.shape), 'bytes': size, 'footprint': len(fp)}


def bake_all(jobs, workers):
    """jobs を作る。戻り値: {名前: 統計}。"""
    if workers <= 1:
        _init_worker()
        return {r['name']: r for r in map(bake_group, jobs)}
    out = {}
    with concurrent.futures.ProcessPoolExecutor(max_workers=workers, initializer=_init_worker) as pool:
        for r in pool.map(bake_group, jobs, chunksize=16):
            out[r['name']] = r
    return out


# ------------------------------------------------------------------ 表
def build_table(groups, empty, kinds, stats, sheet_hash, tile_count):
    tile_to_slab = [None] * tile_count
    for g in groups:
        for i in g['tiles']:
            tile_to_slab[i] = g['name']
    by_kind = {}
    for i, k in kinds.items():
        by_kind.setdefault(k, {'tiles': 0, 'slabs': set()})
        by_kind[k]['tiles'] += 1
    for g in groups:
        for k in {kinds[i] for i in g['tiles']}:
            by_kind[k]['slabs'].add(g['name'])
    return {
        'format': TABLE_FORMAT,
        'generator': 'tools/nethack/tiles_to_slabs.py',
        'prefix': PREFIX,
        'side': SIDE,
        'depth': DEPTH,
        'tiles_sha1': sheet_hash,
        'counts': {
            'board_tiles': len(kinds),
            'empty_tiles': len(empty),
            'slabs': len(groups),
            'by_kind': {k: {'tiles': v['tiles'], 'slabs': len(v['slabs'])} for k, v in sorted(by_kind.items())},
        },
        'empty_tiles': empty,
        'tile_to_slab': tile_to_slab,
        'slabs': {g['name']: {'tile': g['tiles'][0], 'tiles': g['tiles'], 'voxels': stats[g['name']]['voxels'],
                              'colours': stats[g['name']]['colours'], 'size': stats[g['name']]['size'],
                              'bytes': stats[g['name']]['bytes']} for g in groups},
    }


def write_table(out_dir, table):
    with open(os.path.join(out_dir, 'slabs.json'), 'w', encoding='utf-8', newline='\n') as fp:
        json.dump(table, fp, ensure_ascii=False, indent=1)
        fp.write('\n')
    with open(os.path.join(out_dir, 'slabs.tsv'), 'w', encoding='utf-8', newline='\n') as fp:
        fp.write('# 絵の番号<TAB>板の名前（tools/nethack/tiles_to_slabs.py が書く。板にする絵だけ。板の置き場は同じフォルダ）\n')
        for i, n in enumerate(table['tile_to_slab']):
            if n:
                fp.write('%d\t%s\n' % (i, n))


# ------------------------------------------------------------------ 作る
def generate(opt):
    t0 = time.perf_counter()
    sheet, meta = C.load_sheet(opt.tiles), C.load_meta(opt.tiles)
    problems = C.check_sheet(sheet, meta)
    if problems:
        for p in problems:
            print('NG:', p)
        return 1
    groups, empty, kinds = group_tiles(sheet, meta)
    out = os.path.abspath(opt.out)
    os.makedirs(out, exist_ok=True)

    want = groups
    if opt.ids:
        ids = {int(s) for s in opt.ids.split(',') if s.strip()}
        want = [g for g in groups if ids & set(g['tiles'])]
        missing = ids - {i for g in want for i in g['tiles']}
        if missing:
            print('板にならない絵の番号: %s' % sorted(missing))
            return 1

    rev = tool_rev()
    stamp_path = os.path.join(out, '.slab_stamp.json')
    stamps = {}
    if os.path.isfile(stamp_path) and not opt.force:
        try:
            with open(stamp_path, encoding='utf-8') as fp:
                stamps = json.load(fp)
        except Exception:
            stamps = {}
    todo, kept = [], 0
    for g in want:
        key = hashlib.sha1((g['key'] + '|' + rev).encode()).hexdigest()
        g['stamp'] = key
        old = stamps.get(g['name'])
        if (isinstance(old, dict) and old.get('key') == key and os.path.isfile(os.path.join(out, g['name'] + '.vox'))
                and os.path.isfile(os.path.join(out, g['name'] + '.jsonc'))):
            g['stat'] = old['stat']
            kept += 1
        else:
            todo.append(g)
    print('板にする絵 %d 個 → 板 %d 枚（同じ絵をまとめた）。作るのは %d 枚（残り %d 枚は前回のまま）' % (len(kinds) - len(empty), len(groups), len(todo), kept))
    t1 = time.perf_counter()
    stats = bake_all([(g['name'], g['tile'], out) for g in todo], opt.workers) if todo else {}
    t2 = time.perf_counter()

    # 作り直さなかった板の統計は控えから取る（表は毎回書き直す）
    for g in want:
        if g['name'] not in stats:
            stats[g['name']] = g['stat']
    for g in want:
        stamps[g['name']] = {'key': g['stamp'], 'stat': stats[g['name']]}

    reduced = [(n, s['reduced_from']) for n, s in stats.items() if s['reduced_from']]
    if not opt.ids:
        # 要らなくなった板を消す（名前が板の形に合う物だけ。ほかのファイルは触らない）
        keep = {g['name'] for g in groups}
        removed = 0
        for fn in sorted(os.listdir(out)):
            m = NAME_RE.match(fn)
            if m and slab_name(int(m.group(1))) not in keep:
                os.remove(os.path.join(out, fn))
                removed += 1
        stamps = {k: v for k, v in stamps.items() if k in keep}
        table = build_table(groups, empty, kinds, stats, file_sha1(os.path.join(opt.tiles, 'tiles.png')), meta['tile_count'])
        write_table(out, table)
        if removed:
            print('要らなくなった板のファイルを消した: %d 本' % removed)
    with open(stamp_path, 'w', encoding='utf-8', newline='\n') as fp:
        json.dump(stamps, fp)

    nfiles = sum(1 for f in os.listdir(out) if NAME_RE.match(f))
    nbytes = sum(os.path.getsize(os.path.join(out, f)) for f in os.listdir(out) if NAME_RE.match(f))
    t3 = time.perf_counter()
    print('完了: %s' % out)
    print('  板 %d 枚 / .vox と .jsonc %d 本 / 合計 %.1f MB（%d バイト） / ボクセル合計 %d'
          % (len(want), nfiles, nbytes / 1048576.0, nbytes, sum(s['voxels'] for s in stats.values())))
    print('  時間: 絵の整理 %.2f 秒 / 板の作成 %.2f 秒（%d 並列・%d 枚） / 表と後始末 %.2f 秒 / 合計 %.2f 秒'
          % (t1 - t0, t2 - t1, opt.workers, len(todo), t3 - t2, t3 - t0))
    if reduced:
        print('  色を 255 へ詰めた板: %d 枚（元の色数 %s）' % (len(reduced), sorted(reduced, key=lambda x: -x[1])[:10]))
    else:
        print('  色を 255 へ詰めた板: 0 枚')
    return 0


# ------------------------------------------------------------------ .vox を読む（検査用。書き出しの道具を使わない）
def read_vox(path):
    """戻り値: (大きさ(x,y,z), 座標と索引の配列 (n,4), パレット(256,3))。"""
    with open(path, 'rb') as fp:
        data = fp.read()
    if data[:4] != b'VOX ':
        raise ValueError('先頭が VOX でない')
    pos = 8
    cid, csz, chsz = struct.unpack_from('<4sii', data, pos)
    if cid != b'MAIN':
        raise ValueError('MAIN が無い')
    pos += 12 + csz
    end = pos + chsz
    size, xyzi, pal = None, None, None
    while pos < end:
        cid, csz, chsz = struct.unpack_from('<4sii', data, pos)
        body = data[pos + 12:pos + 12 + csz]
        if cid == b'SIZE':
            size = struct.unpack('<iii', body)
        elif cid == b'XYZI':
            n = struct.unpack_from('<I', body, 0)[0]
            xyzi = np.frombuffer(body, np.uint8, count=n * 4, offset=4).reshape(n, 4)
        elif cid == b'XYZ2':
            raise ValueError('XYZ2（一辺 256 超）は想定外')
        elif cid == b'RGBA':
            pal = np.frombuffer(body, np.uint8).reshape(256, 4)[:, :3]
        pos += 12 + csz + chsz
    if size is None or xyzi is None or pal is None:
        raise ValueError('SIZE/XYZI/RGBA のどれかが無い')
    return size, xyzi, pal


def read_jsonc(path):
    with open(path, encoding='utf-8') as fp:
        return json.loads(''.join(l for l in fp if not l.lstrip().startswith('//')))


def verify_geometry(tile, size, xyzi, pal):
    """板の形と色を、元の絵だけから独立に確かめる。問題の文（無ければ None）。
    期待: 絵の不透明な画素 (行, 列) ごとに、x = 列 - 最左列、z = 最下行 - 行、y = 0..DEPTH-1 の DEPTH 個のボクセルが、絵の色で並ぶ（接地・最小隅へ詰め済み）。"""
    mask = np.kron(tile[..., 3] > 0, np.ones((SCALE, SCALE), bool))
    big = C.upscale(tile, SCALE)
    rows = np.nonzero(mask.any(axis=1))[0]
    cols = np.nonzero(mask.any(axis=0))[0]
    r1, c0 = int(rows.max()), int(cols.min())
    want_size = (int(cols.max()) - c0 + 1, DEPTH, r1 - int(rows.min()) + 1)
    if tuple(size) != want_size:
        return '大きさ %s / 期待 %s' % (tuple(size), want_size)
    exp = {}
    for r, c in zip(*np.nonzero(mask)):
        exp[(int(c) - c0, int(r1 - r))] = tuple(int(v) for v in big[r, c, :3])
    if len(xyzi) != len(exp) * DEPTH:
        return 'ボクセル数 %d / 期待 %d' % (len(xyzi), len(exp) * DEPTH)
    cnt = {}
    for x, y, z, idx in xyzi.tolist():
        if y >= DEPTH:
            return 'y=%d が厚みの外' % y
        col = (x, z)
        if col not in exp:
            return '絵に無い位置 (x=%d, z=%d) にボクセル' % col
        if tuple(int(v) for v in pal[idx - 1]) != exp[col]:
            return '位置 (x=%d, z=%d) の色が違う: 板 %s / 絵 %s' % (x, z, tuple(int(v) for v in pal[idx - 1]), exp[col])
        cnt[col] = cnt.get(col, 0) + 1
    if any(v != DEPTH for v in cnt.values()) or len(cnt) != len(exp):
        return '位置ごとの厚みが %d でない' % DEPTH
    return None


# ------------------------------------------------------------------ 検査
def run_check(opt):
    ok = True
    sheet, meta = C.load_sheet(opt.tiles), C.load_meta(opt.tiles)
    for p in C.check_sheet(sheet, meta):
        print('NG:', p)
        ok = False
    groups, empty, kinds = group_tiles(sheet, meta)
    print('絵: 板にする %d 個（怪物 %d・アイテム %d・石像 %d） / 全く透明 %d / まとめた板 %d 枚'
          % (len(kinds), sum(1 for k in kinds.values() if k == 'monster'), sum(1 for k in kinds.values() if k == 'object'),
             sum(1 for k in kinds.values() if k == 'statue'), len(empty), len(groups)))
    # 絵の名前（取り違え）。名前の表が作れないときは飛ばす
    names = None
    try:
        names = C.load_names()
    except Exception as exc:  # noqa: BLE001
        print('（絵の名前の表を作れなかったので、取り違えの検査は飛ばす: %s）' % exc)
    if names is not None:
        bad = [(i, e, names[i]) for i, e, _ in CHECK_SPECS if e not in names[i]]
        for b in bad:
            print('NG: 絵の番号 %d の名前が違う: 期待 %r / 実際 %r' % b)
            ok = False
        if not bad:
            print('OK: 名簿 %d 個の絵の名前が期待どおり' % len(CHECK_SPECS))
        # 範囲の端が合っているか（板にする範囲の先頭・末尾の名前）
        probe = [(0, 'giant ant'), (788, 'invisible monster'), (789, 'strange object'), (1271, 'splash of venom'),
                 (1272, 'stone'), (1514, 'sokoban walls'), (1515, 'statue: giant ant'), (2303, 'statue: invisible monster')]
        pb = [(i, e, names[i]) for i, e in probe if e not in names[i]]
        for b in pb:
            print('NG: 範囲の端 %d の名前が違う: 期待 %r / 実際 %r' % b)
            ok = False
        if not pb:
            print('OK: 板にする範囲の端（0・788・789・1271・1515・2303）と、板にしない範囲の端（1272・1514）の名前が期待どおり')
        # 男女で同じ絵の怪物が実際にまとまるか（まとめの数が 1 でないこと）
        multi = sum(1 for g in groups if len(g['tiles']) > 1)
        print('%s: 同じ絵をまとめた板 %d 枚（%d 個の絵が %d 枚に）' % ('OK' if multi else 'NG', multi,
              sum(len(g['tiles']) for g in groups if len(g['tiles']) > 1), multi))
        ok &= bool(multi)

    out = os.path.abspath(opt.out)
    tj = os.path.join(out, 'slabs.json')
    if not os.path.isfile(tj):
        print('NG: %s が無い（先に作ること）' % tj)
        print('check: FAIL')
        return 1
    with open(tj, encoding='utf-8') as fp:
        table = json.load(fp)
    sheet_hash = file_sha1(os.path.join(opt.tiles, 'tiles.png'))
    exp_t2s = [None] * meta['tile_count']
    for g in groups:
        for i in g['tiles']:
            exp_t2s[i] = g['name']
    probs = []
    if table.get('format') != TABLE_FORMAT:
        probs.append('format=%r' % table.get('format'))
    if (table.get('prefix'), table.get('side'), table.get('depth')) != (PREFIX, SIDE, DEPTH):
        probs.append('prefix/side/depth=%r' % ((table.get('prefix'), table.get('side'), table.get('depth')),))
    if table.get('tiles_sha1') != sheet_hash:
        probs.append('tiles_sha1 が今の tiles.png と違う（板が古い）')
    if table.get('tile_to_slab') != exp_t2s:
        diff = [i for i, (a, b) in enumerate(zip(table.get('tile_to_slab') or [], exp_t2s)) if a != b]
        probs.append('tile_to_slab が今の絵から作った物と違う（違う番号 %d 個。先頭 %s）' % (len(diff), diff[:5]))
    if table.get('empty_tiles') != empty:
        probs.append('empty_tiles が違う')
    if table.get('counts', {}).get('slabs') != len(groups) or len(table.get('slabs', {})) != len(groups):
        probs.append('板の数が違う: 表 %r / 期待 %d' % (table.get('counts', {}).get('slabs'), len(groups)))
    for p in probs:
        print('NG: 表:', p)
    ok &= not probs
    if not probs:
        print('OK: 表 slabs.json が今の tiles.png と一致（板 %d 枚・絵 %d 個）' % (len(groups), len(kinds) - len(empty)))
    # slabs.tsv が slabs.json と同じ内容か
    tsv = {}
    try:
        for line in open(os.path.join(out, 'slabs.tsv'), encoding='utf-8'):
            if line.startswith('#') or not line.strip():
                continue
            a, b = line.rstrip('\n').split('\t')
            tsv[int(a)] = b
    except Exception as exc:  # noqa: BLE001
        print('NG: slabs.tsv を読めない: %s' % exc)
        ok = False
    want_tsv = {i: n for i, n in enumerate(exp_t2s) if n}
    if tsv != want_tsv:
        print('NG: slabs.tsv が今の絵から作った物と違う（行 %d / 期待 %d）' % (len(tsv), len(want_tsv)))
        ok = False
    else:
        print('OK: slabs.tsv %d 行が表と一致' % len(tsv))

    # ファイルの過不足
    have = {m.group(0) for f in os.listdir(out) for m in [NAME_RE.match(f)] if m}
    need = {g['name'] + e for g in groups for e in ('.vox', '.jsonc')}
    if have != need:
        print('NG: 板のファイルの過不足: 足りない %d 本 %s / 余分 %d 本 %s' % (len(need - have), sorted(need - have)[:5], len(have - need), sorted(have - need)[:5]))
        ok = False
    else:
        print('OK: 板のファイル %d 本が過不足なく揃っている' % len(have))

    # 板ごとの中身（元の絵だけから独立に確かめる）
    bad = []
    nbytes = 0
    for g in groups:
        n = g['name']
        try:
            size, xyzi, pal = read_vox(os.path.join(out, n + '.vox'))
            nbytes += os.path.getsize(os.path.join(out, n + '.vox')) + os.path.getsize(os.path.join(out, n + '.jsonc'))
            why = verify_geometry(g['tile'], size, xyzi, pal)
            body = read_jsonc(os.path.join(out, n + '.jsonc'))
            if why is None and body.get('voxels_per_cell') != SIDE:
                why = 'voxels_per_cell=%r' % body.get('voxels_per_cell')
            if why is None and (body.get('name') != n or len(body.get('footprint') or []) != 1):
                why = 'name/footprint=%r/%r' % (body.get('name'), body.get('footprint'))
            sinfo = table.get('slabs', {}).get(n)
            if why is None and (sinfo is None or sinfo.get('voxels') != len(xyzi) or sinfo.get('tiles') != g['tiles']):
                why = '表の統計・まとめた絵の番号が違う'
        except Exception as exc:  # noqa: BLE001
            why = '読めない: %s' % exc
        if why:
            bad.append((n, why))
    for n, why in bad[:20]:
        print('NG: %s: %s' % (n, why))
    if len(bad) > 20:
        print('NG: ほか %d 枚' % (len(bad) - 20))
    ok &= not bad
    if not bad:
        print('OK: 板 %d 枚の形（大きさ・位置ごとの厚み 4・接地）と色が、元の絵と一致。voxels_per_cell=64・footprint 1 マス' % len(groups))
    print('大きさ: .vox と .jsonc の合計 %.1f MB（%d バイト）' % (nbytes / 1048576.0, nbytes))

    if opt.deep:
        tmp = os.path.join(SCRATCH, 'check_deep')
        if os.path.isdir(tmp):
            shutil.rmtree(tmp)
        os.makedirs(tmp)
        t0 = time.perf_counter()
        bake_all([(g['name'], g['tile'], tmp) for g in groups], opt.workers)
        diffs = []
        for g in groups:
            for e in ('.vox', '.jsonc'):
                a, b = os.path.join(out, g['name'] + e), os.path.join(tmp, g['name'] + e)
                if not os.path.isfile(a) or open(a, 'rb').read() != open(b, 'rb').read():
                    diffs.append(g['name'] + e)
        print('%s: 全部を作り直した物とバイト単位で比べた（%.1f 秒）: 違う %d 本 %s' % ('OK' if not diffs else 'NG', time.perf_counter() - t0, len(diffs), diffs[:5]))
        ok &= not diffs
        shutil.rmtree(tmp)
    print('check: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


def run_list(opt):
    sheet, meta = C.load_sheet(opt.tiles), C.load_meta(opt.tiles)
    groups, empty, kinds = group_tiles(sheet, meta)
    try:
        names = C.load_names()
    except Exception:  # noqa: BLE001
        names = None
    for g in groups:
        t = g['tiles']
        print('%-8s %-7s 画素 %3d 絵 %s%s' % (g['name'], kinds[t[0]], g['opaque'], t if len(t) < 6 else '%s…（%d 個）' % (t[:5], len(t)),
                                        '  ' + names[t[0]] if names else ''))
    print('板 %d 枚 / 絵 %d 個 / 全く透明 %d 個 %s' % (len(groups), sum(len(g['tiles']) for g in groups), len(empty), empty))
    return 0


def run_compare_prototype(opt):
    """試作の s64 の板（`nh_*`）と同じ絵の本番の板の `.vox` をバイト単位で比べる。"""
    proto = os.path.abspath(opt.compare_prototype)
    names = {}
    mf = os.path.join(proto, 'manifest.tsv')
    for line in open(mf, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        f = line.rstrip('\n').split('\t')
        names[f[0]] = int(f[1])
    sheet, meta = C.load_sheet(opt.tiles), C.load_meta(opt.tiles)
    groups, _, _ = group_tiles(sheet, meta)
    t2n = {i: g['name'] for g in groups for i in g['tiles']}
    ok = True
    for pname, tile in sorted(names.items(), key=lambda kv: kv[1]):
        a = open(os.path.join(proto, pname + '.vox'), 'rb').read()
        b = open(os.path.join(os.path.abspath(opt.out), t2n[tile] + '.vox'), 'rb').read()
        same = a == b
        ok &= same
        print('%s: 試作 %s ↔ 本番 %s（絵 %d）  .vox %d バイト%s' % ('OK' if same else 'NG', pname, t2n[tile], tile, len(a), '' if same else '（違う）'))
    print('compare: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description='NetHack のタイルをボクセルの板にする（本番）')
    ap.add_argument('--out', default=DEFAULT_OUT, help='板と表の出力先（既定 nethack/data/slab/）')
    ap.add_argument('--tiles', default=C.DATA_EN, help='tiles.png と tiles.json のあるフォルダ（既定 nethack/data/en）')
    ap.add_argument('--workers', type=int, default=max(1, min(8, (os.cpu_count() or 4) - 1)), help='並列の数')
    ap.add_argument('--ids', default='', help='指定した絵の番号（コンマ区切り）を含む板だけ作る（表は書かない）')
    ap.add_argument('--force', action='store_true', help='控えを無視して全部作り直す')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--deep', action='store_true', help='--check と一緒に。全部を作り直してバイト単位で比べる')
    ap.add_argument('--compare-prototype', default='', metavar='試作s64のフォルダ')
    opt = ap.parse_args(argv)
    opt.tiles = os.path.abspath(opt.tiles)
    if opt.list:
        return run_list(opt)
    if opt.check:
        return run_check(opt)
    if opt.compare_prototype:
        return run_compare_prototype(opt)
    return generate(opt)


if __name__ == '__main__':
    sys.exit(main())
