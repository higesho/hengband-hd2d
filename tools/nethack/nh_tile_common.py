# -*- coding: utf-8 -*-
"""NetHack の原作のタイルの絵を読む共通部分（`tiles_to_slabs.py`・`gen_nethack_terrain.py`・`build_nethack_scene.py` が使う）。

タイルの絵は `nethack/data/en/tiles.png`（16x16 の絵 2,307 個。1 行 40 個）と `tiles.json`。
`tools/nethack/build_tiles.py` が作る。**絵ごとの名前**（`tile_names.json`）は `build_tiles.py --names` が作る。
追跡している `nethack/data/en/` には名前の表が無いので、無いときは作業用のフォルダへ作って使う（このツリーのデータは書き換えない）。

透過色は RGB(71,108,108)。`tiles.png` では**その画素の alpha が 0** になっている。
**壁の地色は RGB(72,108,108)（透過色と 1 だけ違う）**なので、色の近さで透過を判定してはならない（alpha だけを見る）。
"""
import json
import os
import subprocess
import sys

sys.dont_write_bytecode = True   # tools/ の下へ __pycache__ を作らない

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
DATA_EN = os.path.join(ROOT, 'nethack', 'data', 'en')
WORK = os.path.join(ROOT, '.taskwork', 'nethack_voxel')
TILE = 16
COLUMNS = 40
TRANSPARENT_RGB = (71, 108, 108)


def load_sheet(data_dir=DATA_EN):
    """tiles.png を (行, 列, 4) の uint8 で返す。"""
    im = Image.open(os.path.join(data_dir, 'tiles.png')).convert('RGBA')
    return np.asarray(im, np.uint8).copy()


def load_meta(data_dir=DATA_EN):
    with open(os.path.join(data_dir, 'tiles.json'), encoding='utf-8') as fp:
        return json.load(fp)


def tile_rgba(sheet, index):
    """絵の番号 index の 16x16 RGBA。"""
    x, y = (index % COLUMNS) * TILE, (index // COLUMNS) * TILE
    return sheet[y:y + TILE, x:x + TILE].copy()


def load_names():
    """絵の番号 → 名前（原作の .txt の `# tile N (名前)`）。無ければ作業用フォルダへ作る。"""
    for cand in (os.path.join(DATA_EN, 'tile_names.json'),
                 os.path.join(WORK, 'tiles_data', 'en', 'tile_names.json')):
        if os.path.isfile(cand):
            with open(cand, encoding='utf-8') as fp:
                return json.load(fp)
    out = os.path.join(WORK, 'tiles_data')
    os.makedirs(out, exist_ok=True)
    subprocess.run([sys.executable, '-B', os.path.join(HERE, 'build_tiles.py'), '--lang', 'en', '--out', out, '--names'],
                   check=True, timeout=300, stdout=subprocess.DEVNULL)
    with open(os.path.join(out, 'en', 'tile_names.json'), encoding='utf-8') as fp:
        return json.load(fp)


def check_tile_names(specs, names):
    """specs = [(表の名前, 絵の番号, 期待する名前の一部, ...)]。名前が合わない行を返す（取り違えの検査）。"""
    bad = []
    for s in specs:
        name, idx, expect = s[0], s[1], s[2]
        if idx >= len(names) or expect not in names[idx]:
            bad.append((name, idx, expect, names[idx] if idx < len(names) else None))
    return bad


def check_sheet(sheet, meta):
    """絵の寸法・個数と、**不透明な画素に透過色が混ざっていない**ことを確かめる。問題の文の一覧を返す。"""
    problems = []
    h, w = sheet.shape[:2]
    if w != COLUMNS * TILE or h != meta['rows'] * TILE:
        problems.append('tiles.png の寸法が表と合わない: %dx%d / 表 %dx%d' % (w, h, COLUMNS * TILE, meta['rows'] * TILE))
    if list(meta['transparent_rgb']) != list(TRANSPARENT_RGB):
        problems.append('透過色が違う: %s' % (meta['transparent_rgb'],))
    opaque = sheet[..., 3] > 0
    leak = opaque & (sheet[..., :3] == np.array(TRANSPARENT_RGB, np.uint8)).all(axis=2)
    if leak.any():
        problems.append('不透明な画素に透過色が混ざっている: %d 画素' % int(leak.sum()))
    return problems


def upscale(img, k):
    """最近傍で k 倍（np 配列 (行, 列, ...)）。"""
    return np.repeat(np.repeat(img, k, axis=0), k, axis=1)


#: 板にする絵の範囲（`tiles.json` の `tile_ranges` の名前 → 表で使う種類の名前）。両端を含む。
#: 地形ほか（`other`）と印（`decals`）は板にしない（地形はブロックの担当が別に作る）。
SLAB_RANGES = (('monsters', 'monster'), ('objects', 'object'), ('statues', 'statue'))


def slab_tile_kinds(meta):
    """板にする絵の番号 → 種類（'monster' / 'object' / 'statue'）。`tiles.json` の範囲から作る。"""
    out = {}
    for key, kind in SLAB_RANGES:
        lo, hi = meta['tile_ranges'][key]
        for i in range(lo, hi + 1):
            out[i] = kind
    return out


def normalize_tile(tile, alpha_cutoff=110):
    """板にする絵（16x16 RGBA）を整える。alpha は 0 か 255 に振り、透明な画素の色は 0 にそろえる
    （`tools/voxel/tile_to_slab.py` の `load_tile` と同じ規則。絵の同一判定にも使う）。"""
    a = tile.copy()
    a[..., 3] = np.where(a[..., 3] >= alpha_cutoff, 255, 0)
    a[a[..., 3] == 0] = 0
    return a
