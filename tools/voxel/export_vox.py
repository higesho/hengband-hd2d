# -*- coding: utf-8 -*-
"""捨て試作（tools/voxel_probe/）のボクセルを `.vox` ＋ `.jsonc` の対として書き出す。

定義の置き場: （プレハブ形式）/ P1

`vox_art.py` と `mill.py` は numpy 配列と PNG しか吐かないので、**本編が読む形**へ移す
のがこのスクリプトの仕事である。**試作の側は 1 行も触らない**（あちらは記録なので）。

| 出力 | パーツ | 由来 |
|---|---|---|
| `assets/voxel/shop.{vox,jsonc}` | 1（main） | `vox_art.shop()` |
| `assets/voxel/tree.{vox,jsonc}` | 1（main） | `vox_art.tree()` |
| `assets/voxel/rock.{vox,jsonc}` | 1（main） | `vox_art.rock()` |
| `assets/voxel/mill.{vox,jsonc}` | **2（hut ＋ wheel）** | `mill.hut()` / `mill.wheel(0)` |

`mill` だけがパーツ 2 つで、**シーングラフとピボットの経路を通すため**にある
（水車は §8 の「回転」。P6 で動かす）。

## 何をどちらのファイルに書くか（§7 の線引き）
- **`.vox`** … 実体（ボクセルとパレット）と**配置**（シーングラフの平行移動）
- **`.jsonc`** … 意味づけ（footprint・親子・ピボット・動き・接地・風の係数）

配置を 2 か所に書かない。`.jsonc` の `offset` は**省略可**で、書いた場合は `.vox` と
一致することをロード時に照合する（食い違いは検査で落とす）。

## 実行
```
cd tools/voxel_probe          # 試作は互いを相対 import するので cwd はここ
python ..\voxel\export_vox.py
```
"""
import importlib.util
import json
import os
import struct
import time
import sys

import numpy as np

VOXELS_PER_CELL = 32  # 設計書 §5「1 マス = 32×32×32」。タイル板だけ 64 を渡してくる（tile_to_slab.py）


# --------------------------------------------------------------- .vox 書き出し
def _chunk(chunk_id, content=b'', children=b''):
    return chunk_id + struct.pack('<ii', len(content), len(children)) + content + children


def _vox_string(text):
    raw = text.encode('utf-8')
    return struct.pack('<i', len(raw)) + raw


def _vox_dict(pairs):
    out = struct.pack('<i', len(pairs))
    for key, value in pairs:
        out += _vox_string(key) + _vox_string(value)
    return out


VOX_CLASSIC_MAX_SIDE = 256  # `XYZI` は座標が 1 バイト（基準は hd2d/voxel/vox_file.h）


def _xyzi(vol):
    """密な配列から座標と索引の並びへ。**索引 0 は空**なので落とす。

    戻り値は `(チャンク名, 中身)`。一辺が 256 までは従来どおり **`XYZI`**（1 バイト座標）、
    超えたら **`XYZ2`**（16 ビット座標）で書く。

    **切り詰めて書かないこと。**`np.uint8` へ入れると x=256 が x=0 に化けて、
    板の右半分が左半分に重なった絵が黙って出る（512px の板で実際に踏んだ。
    読む側も 1 バイトで読むので、どこにも警告が出ない）。
    """
    idx = np.argwhere(vol > 0).astype(np.int32)
    colors = vol[idx[:, 0], idx[:, 1], idx[:, 2]].astype(np.uint8)
    out = bytearray(struct.pack('<I', len(idx)))
    if max(vol.shape) > 65536:
        raise ValueError('一辺 %d は .vox で表せません（16 ビット座標の上限）' % max(vol.shape))
    if max(vol.shape) > VOX_CLASSIC_MAX_SIDE:
        packed = np.zeros((len(idx), 8), np.uint8)
        for axis in range(3):
            packed[:, axis * 2] = idx[:, axis] & 0xFF
            packed[:, (axis * 2) + 1] = (idx[:, axis] >> 8) & 0xFF
        packed[:, 6] = colors
        out += packed.tobytes()
        return b'XYZ2', bytes(out)
    packed = np.empty((len(idx), 4), np.uint8)
    packed[:, 0] = idx[:, 0]
    packed[:, 1] = idx[:, 1]
    packed[:, 2] = idx[:, 2]
    packed[:, 3] = colors
    out += packed.tobytes()
    return b'XYZI', bytes(out)


def _compact(parts, palette):
    """**その 1 個で実際に使った色だけ**を 1..255 へ詰め直す（2026-08-20）。

    `.vox` の索引は 1〜255 しか無いが、**パレットはファイルごと**である——書く側は
    ファイルごとに RGBA チャンクを持ち、読む側も**プレハブごとに 256×1 のテクスチャ**を
    上げている（`hd2d/render/voxel_renderer.cpp`）。中の編集器も同じ。
    つまり「全プレハブ合わせて 254 色まで」は**生成器が 1 つの表を共有していただけ**の
    縛りで、形式にもエンジンにも根拠が無い（2026-08-20 に気づいた:
    「256色のパレットは個別に分ける話もあるので、足らずに無理がでる場合は言って」）。

    ここで詰め直すので、**呼ぶ側は 255 を超える索引を使ってよい**（`reg_local`）。
    配列は `np.int16` で作ること——`np.uint8` には 256 以上が入らない。
    """
    order = []
    seen = set()
    for _, vol, _ in parts:
        for value in np.unique(vol):
            index = int(value)
            if (index <= 0) or (index in seen):
                continue
            seen.add(index)
            order.append(index)
    order.sort()
    if len(order) > 255:
        raise ValueError('1 個で %d 色使っています（.vox の上限は 255）' % len(order))
    missing = [i for i in order if i not in palette]
    if missing:
        raise ValueError('パレットに無い索引を使っています: %s' % missing)
    remap = {old: new for new, old in enumerate(order, start=1)}
    out_parts = []
    for name, vol, origin in parts:
        dense = np.zeros(vol.shape, np.uint8)
        for old, new in remap.items():
            dense[vol == old] = new
        out_parts.append((name, dense, origin))
    return out_parts, {new: palette[old] for old, new in remap.items()}, {new: old for old, new in remap.items()}


def write_vox(path, parts, palette, source_index_out=None):
    """MagicaVoxel `.vox` を書く。

    parts   … [(名前, 配列, 最小隅のプレハブ座標(x,y,z))]
    palette … {索引: (r, g, b)}。**索引は 255 を超えてよい**（`_compact` が詰め直す）
    source_index_out … 渡すと `{このファイルの索引: 元の登録簿の索引}` を詰める。
                       **材質を書き出すのに要る**（`gen_prefabs.py`。色から当てるのをやめた）

    シーングラフは `nTRN(root) → nGRP → [nTRN(名前つき) → nSHP]` の形にする。
    平行移動 `_t` は **MagicaVoxel の約束どおり「模型の中心の位置」**で書く
    （最小隅 = `_t - floor(size/2)`）。人が MagicaVoxel で開いて直せることが R13 の前提なので、
    自前の都合で約束を変えない。
    """
    parts, palette, source_index = _compact(parts, palette)
    if source_index_out is not None:
        source_index_out.clear()
        source_index_out.update(source_index)
    models = b''
    for _, vol, _ in parts:
        size = struct.pack('<iii', *vol.shape)
        chunk_id, content = _xyzi(vol)
        models += _chunk(b'SIZE', size) + _chunk(chunk_id, content)

    # --- シーングラフ ---
    # id 0 = 根の nTRN、id 1 = nGRP、以降 パーツごとに nTRN と nSHP を 2 個ずつ。
    graph = _chunk(b'nTRN', struct.pack('<i', 0) + _vox_dict([])
                   + struct.pack('<iiii', 1, -1, 0, 1) + _vox_dict([]))
    child_ids = [2 + i * 2 for i in range(len(parts))]
    graph += _chunk(b'nGRP', struct.pack('<i', 1) + _vox_dict([])
                    + struct.pack('<i', len(child_ids))
                    + b''.join(struct.pack('<i', c) for c in child_ids))
    for i, (name, vol, origin) in enumerate(parts):
        trn_id = 2 + i * 2
        shp_id = trn_id + 1
        center = [int(origin[k] + vol.shape[k] // 2) for k in range(3)]
        graph += _chunk(b'nTRN',
                        struct.pack('<i', trn_id) + _vox_dict([('_name', name)])
                        + struct.pack('<iiii', shp_id, -1, 0, 1)
                        + _vox_dict([('_t', '%d %d %d' % tuple(center))]))
        graph += _chunk(b'nSHP',
                        struct.pack('<i', shp_id) + _vox_dict([])
                        + struct.pack('<i', 1) + struct.pack('<i', i) + _vox_dict([]))

    # --- パレット（256 個固定。索引 i の色は rgba[i-1]） ---
    rgba = bytearray(256 * 4)
    for i in range(1, 256):
        r, g, b = palette.get(i, (0, 0, 0))
        rgba[(i - 1) * 4:(i - 1) * 4 + 4] = bytes((r, g, b, 255))
    pal = _chunk(b'RGBA', bytes(rgba))

    body = models + graph + pal
    data = b'VOX ' + struct.pack('<i', 150) + _chunk(b'MAIN', b'', body)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    #: **書き込みが稀に弾かれる**（Windows の `OSError: [Errno 22]`。2,511 個を続けて
    #: 書くと数回に 1 度当たる——走査中の常駐物が掴んでいるらしい）。ここで落ちると
    #: 生成が途中で止まり、**`.jsonc` と `.vox` の対が食い違ったまま残る**ので、数回試す。
    for attempt in range(4):
        try:
            with open(path, 'wb') as fp:
                fp.write(data)
            break
        except OSError:
            if attempt == 3:
                raise
            time.sleep(0.25)
    return len(data)


# ------------------------------------------------------------ .jsonc 書き出し
def derive_footprint(parts, voxels_per_cell=VOXELS_PER_CELL):
    """**地面の高さ（z = 0）**のボクセルが乗っているマスを集める。

    設計書 §7.1-1 は「footprint は宣言し、ロード時に照合する。自動導出は間違っていても
    気づけない」と定めている。手続き生成では**書き出す側が作者**なので、ここが宣言の
    出どころになる。ロード時の照合は「`.vox` と `.jsonc` が食い違っていないか」を見る検査
    として働き、片方だけ直した／作り直したときに落ちる。

    `voxels_per_cell` は書き出す prefab と同じ値を渡すこと。板（64）を既定の 32 で
    割るとマス数が 2×2 に化け、ロード時の照合で落ちる。
    """
    cells = set()
    for _, vol, origin in parts:
        z0 = int(round(origin[2]))
        if z0 != 0:
            continue  # 地面に接していないパーツは接地シルエットに寄与しない
        layer = vol[:, :, 0]
        for x, y in np.argwhere(layer > 0):
            cells.add(((int(x) + int(origin[0])) // voxels_per_cell,
                       (int(y) + int(origin[1])) // voxels_per_cell))
    return sorted(cells)


def write_prefab(path, name, parts, extra_parts=None, note='', voxels_per_cell=VOXELS_PER_CELL,
                 palette_entries=None, hide_unseen_from_above=False):
    """プレハブ定義（`.jsonc`）を書く。

    `palette_entries` … **このプレハブだけのパレット**。
    `[{'i': 索引, 'rgb': 'RRGGBB', 'name': '色の名前', 'material': 'wood'}, …]`。
    材質は**面の汚しと木の葉**が使う（`hd2d/render/surface_wear.h`）。渡さなければ書かない。

    **色から材質を当てるのはやめた**（2026-08-23 に決めた:「パレットの色で汚しを
    入れるのをやめよう。パレットの共有もやめて、ベースのパレットのコピーを作り個別に
    使うように」）。パレットはライブラリじゅうで色を使い回しているので、色は材を指さない。
    """
    footprint = derive_footprint(parts, voxels_per_cell)
    lines = []
    lines.append('// %s — プレハブ定義（設計書 §7）。`%s.vox` と対で使う。' % (name, name))
    lines.append('// tools/voxel/export_vox.py が書き出したもの。手で直してよいが、')
    lines.append('// footprint を偽ると読み込み時の照合で落ちる（§7.2）。')
    if note:
        lines.append('// %s' % note)
    body = {
        'name': name,
        'voxels_per_cell': voxels_per_cell,
        # 接地するマス。当たり判定はこれで決まる（§7.1-1）。
        'footprint': [list(c) for c in footprint],
        # 格子のどこをプレハブ原点にするか（マス単位）。
        'anchor': [0, 0],
        'parts': extra_parts if extra_parts is not None else [
            {'name': 'main', 'voxels': 'main', 'grounded': True,
             'motion': {'kind': 'static'}, 'wind_k': 1.0},
        ],
        'variants': [],
    }
    if hide_unseen_from_above:
        #: **空から見えない面を作らない**（`hd2d/voxel/greedy_mesher.h` の `mesh_model()`）。
        #: 深淵のように**床より下へ掘る**プレハブだけに立てる。地形は空に浮いた板なので、
        #: 掘った部分の外側は隣が空になり、下から見ると殻が垂れて見える。
        body['hide_unseen_from_above'] = True
    if palette_entries:
        #: **このプレハブのパレット**（索引はこのファイルの `.vox` のもの）。
        body['palette'] = palette_entries
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as fp:
        fp.write('\n'.join(lines) + '\n')
        json.dump(body, fp, ensure_ascii=False, indent=2)
        fp.write('\n')
    return footprint


# ------------------------------------------------------------------------ 本体
def _load(module_path, name):
    spec = importlib.util.spec_from_file_location(name, module_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, '..', '..'))
    probe = os.path.join(root, 'tools', 'voxel_probe')
    out_dir = os.path.join(root, 'assets', 'voxel')

    # 試作は互いを相対パスで import しているので cwd を移す。
    os.chdir(probe)
    va = _load(os.path.join(probe, 'vox_art.py'), 'va')
    mill = _load(os.path.join(probe, 'mill.py'), 'mill')

    # --- 1 パーツのもの ---
    # `motion` と `wind_k` は素材ごとに決める（設計書 §8.1・§8.2）。
    #   木   … 葉は風で揺らすが、幹まで曲がるとゴムに見えるので係数は控えめ（§8.2-3）。
    #          しなやかさは高さから自動で焼かれるので、根元は係数に関わらず動かない
    #   草   … 風の主役。係数は 1.0
    #   店・岩 … 剛体。風で曲げない
    singles = (
        ('shop', va.shop(), {'kind': 'static'}, 0.0),
        ('tree', va.tree(), {'kind': 'wind'}, 0.45),
        ('rock', va.rock(), {'kind': 'static'}, 0.0),
        ('grass', va.grass(), {'kind': 'wind'}, 1.0),
    )
    for name, vol, motion, wind_k in singles:
        parts = [('main', vol, (0, 0, 0))]
        extra = [{'name': 'main', 'voxels': 'main', 'grounded': True,
                  'motion': motion, 'wind_k': wind_k}]
        size = write_vox(os.path.join(out_dir, name + '.vox'), parts, va.PAL)
        fp = write_prefab(os.path.join(out_dir, name + '.jsonc'), name, parts, extra)
        print('%-5s %-14s voxels=%-8d vox=%7d B  footprint=%d マス  motion=%s k=%.2f'
              % (name, 'x'.join(map(str, vol.shape)), int((vol > 0).sum()), size, len(fp),
                 motion['kind'], wind_k))

    # --- 水車小屋（パーツ 2 つ。シーングラフとピボットの経路を通す） ---
    hut = mill.hut()
    wheel = mill.wheel(0.0)
    # mill.py は水車を「中心が HUB に来るように」置いている（`collect()` の rot 経路）。
    # .vox は最小隅で持つので、ここで直す。
    wheel_origin = tuple(int(round(mill.HUB[k] - wheel.shape[k] / 2.0)) for k in range(3))
    parts = [('hut', hut, (0, 0, 0)), ('wheel', wheel, wheel_origin)]
    mill_parts = [
        {'name': 'hut', 'voxels': 'hut', 'grounded': True,
         'motion': {'kind': 'static'}, 'wind_k': 1.0},
        {'name': 'wheel', 'voxels': 'wheel', 'parent': 'hut', 'grounded': False,
         # ピボットは**プレハブ原点から**（＝親のローカル座標。§7 の「親からの相対」）。
         # 検査は offset を引いてパーツの範囲に入るかを見る（§7.2）。
         'pivot': [round(float(mill.HUB[0]), 1), round(float(mill.HUB[1]), 1),
                   round(float(mill.HUB[2]), 1)],
         'motion': {'kind': 'rotate', 'axis': 'x', 'speed': 0.55, 'phase': 0.0},
         'wind_k': 0.0},
    ]
    size = write_vox(os.path.join(out_dir, 'mill.vox'), parts, mill.PAL)
    fp = write_prefab(os.path.join(out_dir, 'mill.jsonc'), 'mill', parts, mill_parts,
                      note='水車（wheel）は回転パーツ。ピボットは mill.py の HUB と同じ。')
    print('%-5s %-14s voxels=%-8d vox=%7d B  footprint=%d マス  wheel_origin=%s'
          % ('mill', 'x'.join(map(str, hut.shape)),
             int((hut > 0).sum() + (wheel > 0).sum()), size, len(fp), wheel_origin))

    # --- 吊り看板（P6 の振り子＋親子）---
    post, board, board_origin, hang = va.sign()
    parts = [('post', post, (0, 0, 0)), ('board', board, board_origin)]
    sign_parts = [
        {'name': 'post', 'voxels': 'post', 'grounded': True,
         'motion': {'kind': 'static'}, 'wind_k': 0.0},
        {'name': 'board', 'voxels': 'board', 'parent': 'post', 'grounded': False,
         # 吊り点。**プレハブ原点から**（§7 の「親からの相対」）。板の範囲の中に無いと
         # `load_prefab` の検査（§7.2-2）で落ちる。
         'pivot': [round(hang[0], 1), round(hang[1], 1), round(hang[2], 1)],
         # 腕木は +x へ伸びているので、板は x 軸まわりに振れる（＝面が向く ±y へ揺れる）。
         # 振幅は度、周期は秒。減衰は 0（看板は揺れ続けてほしい。`part_motion.cpp` の注記）。
         'motion': {'kind': 'pendulum', 'axis': 'x', 'amplitude': 11.0,
                    'period': 2.3, 'phase': 0.0, 'damping': 0.0},
         'wind_k': 0.0},
    ]
    size = write_vox(os.path.join(out_dir, 'sign.vox'), parts, va.PAL)
    fp = write_prefab(os.path.join(out_dir, 'sign.jsonc'), 'sign', parts, sign_parts,
                      note='板（board）は柱から吊った振り子。ピボットは腕木の先の金具。')
    print('%-5s %-14s voxels=%-8d vox=%7d B  footprint=%d マス  hang=%s'
          % ('sign', 'x'.join(map(str, post.shape)),
             int((post > 0).sum() + (board > 0).sum()), size, len(fp), hang))
    return 0


if __name__ == '__main__':
    sys.exit(main())
