# -*- coding: utf-8 -*-
"""タイル（`tilework/sfc/*.png`）を**ボクセルの板**へ焼く。

2026-08-15 に決めた: **フィギュア化は工数が重いので断念。代わりにタイルを
ボクセルの板にする。**将来タイルを**ピクセルパーフェクトなものへ差し替える**ので、
そのときに焼き直せる道具として残す ——これがこのファイルの存在理由である。

## 何をするか

```
64px の正（`tilework/64`。ピクセルパーフェクト） ──（1 画素 = 1 ボクセル・厚みを付ける）──> .vox ＋ .jsonc
640px の原本（`--src`。64px が無い名前の緊急用） ──（sprite_atlas.cpp と同じ規則で縮小）──> 同上
```

**1 画素が 1 ボクセルに 1 対 1 で対応する。**だから差し替え後も同じ道具で焼き直せば、
新しい絵がそのまま板になる。ドットの位置がずれる余地が無い。

一辺は `--side`（既定 64）。`.jsonc` の `voxels_per_cell` にも同じ値を書くから、
側を変えても板は **1 マスに収まったまま**ボクセルだけ細かくなる
（描画側は 1/voxels_per_cell に縮める。C++ の改修は要らない）。

## ピクセルパーフェクトなタイルは**再標本化しない**

入力が既に `--side` と同寸なら、**平均も減色もしない**でそのまま使う（`--px-dir` か、
`--src` の中の同寸）。ピクセルパーフェクトな絵に箱平均を掛けたら意味が無い。
640px の原本のときだけ下の規則で落とす。

## 色は 1 枚 255 色まで（`.vox` の形式上の上限）

64px のピクセルパーフェクトなタイルは **1,729/1,744 枚が 255 色を超える**（最大 2,816 色。
2026-08-16 に実測）。形＝ドットの格子は 1:1 のまま、色だけメディアンカットで 255 へ寄せる。
**詰めた枚は必ず申告する**（黙って絵を変えない）。色の完全一致まで要るなら `.vox` を
やめて PNG 直読みの別経路を作るしかない（未着手。設計書 §11）。

## 縮小規則の基準は `hd2d/assets/sprite_atlas.cpp`

| 段 | 中身 |
|---|---|
| 1 | 出力画素ごとに原本の矩形を**α で重みを付けた**箱平均（透明画素の色を混ぜると輪郭が濁る） |
| 2 | 矩形内の α の**単純平均**が `kAlphaCutoff` 未満なら**完全に透明**（階段状の輪郭の本体） |
| 3 | 各成分を `kColourLevels` 段に丸める（減色） |

**定数をここに写しているので、片方だけ変えると絵が食い違う。**`--self-check` が
`sprite_atlas.cpp` を読んで**実際に一致しているか照合する**（食い違いは FAIL）。
設計書 §7.2「検出したいものを検出できない検査は、無いより悪い」と同じ流儀。

## 向き

世界は **x = 東・y = 南・z = 上**で、カメラは **+y から**見下ろす（`gen_prefabs.py` §2121）。
板は **x-z 平面**に立て、**絵の面を +y 側**へ向ける。厚みは y に持つ。

- 絵の**列** → x（左が小さい x。画面の左右と一致する）
- 絵の**行** → z を反転（絵の上が z の大きい側）

## 出力

`assets/voxel/slab/<名前>.{vox,jsonc}`。見るときは

```
.\\HengbandHd2d.exe --prefab-check=R955 --voxel-dir=assets/voxel/slab
.\\HengbandHd2d.exe --prefab=R955 --voxel-dir=assets/voxel/slab --windowed=900x900 --shot=s.bmp
```

**`--prefab-check` をパイプに通さないこと**（exit code が隠れる）。
**検査を通す前に `--prefab=` を実行しないこと**（失敗するとモーダルで固まる）。

## 使い方

```
python tools\\voxel\\tile_to_slab.py --self-check          # 検査の検査（**最初にこれ**）
python tools\\voxel\\tile_to_slab.py --ids R955,K1,P0_0_0  # 名指し
python tools\\voxel\\tile_to_slab.py --all                 # 全部（1,744 枚）
python tools\\voxel\\tile_to_slab.py --all --side 32 --px-dir ""  # 旧 32px の焼き方
python tools\\voxel\\tile_to_slab.py --all --sheet shots/slab_inputs.png
```

`--all` は既に焼いたものを**飛ばす**（**使った絵**の内容と設定のハッシュを控えてある。
`tilework/64` だけ差し替えても焼き直しが走る）。全部やり直すなら `--force`。
接地（`--trim-bottom`）は**既定で入り**（実機は `entity_view.cpp` が z=0 に置く＝接地前提）、
外すときだけ `--no-trim-bottom`。
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
import os
import re
import sys

import numpy as np

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("Pillow が要ります: py -m pip install pillow")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

#: `.vox` / `.jsonc` の書き出しは `export_vox.py` のものを使う（**形を決める場所を 2 つにしない**）。
_spec = importlib.util.spec_from_file_location('export_vox', os.path.join(HERE, 'export_vox.py'))
ex = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ex)

#: 板の一辺（ボクセル＝画素）の既定。§5 の「1 マス = 32」とは独立で、
#: 同じ値を `voxels_per_cell` に書くことで側をいくつにしても 1 マスに収まる。
DEFAULT_SIDE = 64
#: 受け入れる一辺の段（2026-08-21 に決めた「人物・アイテム・モンスターは
#: 8/16/32/64/128/256/512 を受け入れられるように。コアは問わずエンジンとして」）。
#: **エンジン側の基準は `hd2d/voxel/prefab.h` の `kVoxelsPerCellLadder`** で、
#: `HengbandHd2d.exe --slab-ladder-check` が 7 段すべてを書いて読み戻して確かめる。
#: 256 を超える板は `.vox` の座標が 1 バイトに収まらないので、`export_vox.py` が
#: 16 ビット座標の `XYZ2` で書く（`hd2d/voxel/vox_file.h`）。
SIDE_LADDER = (8, 16, 32, 64, 128, 256, 512)
ATLAS_CPP = os.path.join(ROOT, 'hd2d', 'assets', 'sprite_atlas.cpp')

# ---- `sprite_atlas.cpp` から写した定数。`--self-check` が照合する ----
ALPHA_CUTOFF = 110   # constexpr int kAlphaCutoff
COLOUR_LEVELS = 24   # constexpr int kColourLevels


def cpp_constants():
    """`sprite_atlas.cpp` の定数を読む。写し間違い・片側だけの変更を捕まえるため。"""
    with open(ATLAS_CPP, encoding='utf-8', errors='replace') as fp:
        text = fp.read()
    out = {}
    for name in ('kAlphaCutoff', 'kColourLevels', 'kSpritePx'):
        m = re.search(r'constexpr int %s\s*=\s*(\d+)' % name, text)
        if m:
            out[name] = int(m.group(1))
    m = re.search(r'static constexpr int kSpritePx\s*=\s*(\d+)', text)  # 宣言はヘッダ側
    if m:
        out['kSpritePx'] = int(m.group(1))
    return out


# ============================================================ 640px → side×side
def reduce_to(im: Image.Image, side: int) -> np.ndarray:
    """`sprite_atlas.cpp::blit()` と**同じ規則**で side×side の RGBA へ落とす。

    戻り値は `(side, side, 4)` の uint8（行・列・RGBA）。α は 0 か 255 しか出ない。
    """
    src = np.asarray(im.convert('RGBA'), dtype=np.float32)
    h, w = src.shape[0], src.shape[1]
    out = np.zeros((side, side, 4), np.uint8)
    step = 255.0 / (COLOUR_LEVELS - 1)
    for ty in range(side):
        y0 = (ty * h) // side
        y1 = max(y0 + 1, ((ty + 1) * h) // side)
        for tx in range(side):
            x0 = (tx * w) // side
            x1 = max(x0 + 1, ((tx + 1) * w) // side)
            box = src[y0:y1, x0:x1]
            alpha = box[..., 3]
            weight = float(alpha.sum())
            mean_a = float(alpha.mean()) if alpha.size else 0.0
            #! **しきい値で振る**のが階段状の輪郭の本体（半透明の縁を残さない）。
            if weight <= 0.0 or mean_a < ALPHA_CUTOFF:
                continue
            #! **α で重みを付けた**平均（透明画素の色を混ぜると輪郭が暗く濁る）。
            rgb = (box[..., :3] * alpha[..., None]).sum(axis=(0, 1)) / weight
            out[ty, tx, :3] = np.clip(np.round(np.round(rgb / step) * step), 0, 255).astype(np.uint8)
            out[ty, tx, 3] = 255
    return out


def load_tile(path: str, px_dir: str | None, name: str, side: int) -> tuple[np.ndarray, str]:
    """side×side の RGBA を得る。戻り値は (画素, 由来)。

    **既に side×side のものは 1 画素も触らない**（ピクセルパーフェクトを保つ）。
    ただし半透明だけは 0/255 へ振る——板は α で抜くので、中途半端な α は縁を濁らせる。
    """
    if px_dir:
        cand = os.path.join(px_dir, name + '.png')
        if os.path.exists(cand):
            im = Image.open(cand).convert('RGBA')
            if im.size != (side, side):
                raise ValueError('%s は %s。--px-dir の絵は %d×%d でなければならない'
                                 % (cand, im.size, side, side))
            a = np.asarray(im, np.uint8).copy()
            a[..., 3] = np.where(a[..., 3] >= ALPHA_CUTOFF, 255, 0)
            a[a[..., 3] == 0] = 0
            return a, 'px(そのまま)'
    im = Image.open(path).convert('RGBA')
    if im.size == (side, side):
        a = np.asarray(im, np.uint8).copy()
        a[..., 3] = np.where(a[..., 3] >= ALPHA_CUTOFF, 255, 0)
        a[a[..., 3] == 0] = 0
        return a, 'src(そのまま)'
    return reduce_to(im, side), 'src(縮小)'


# ============================================================ 32x32 → ボクセル
def depth_map(alpha: np.ndarray, depth: int, profile: str) -> np.ndarray:
    """画素ごとの厚み（0 = 空）。`flat` は一様、`dome` は輪郭から遠いほど厚い。"""
    solid = alpha > 0
    if profile == 'flat':
        return np.where(solid, depth, 0).astype(np.int32)
    #! かまぼこ。輪郭からの距離（4 近傍）を数えて、1 + 距離 を厚みにする。
    dist = np.full(alpha.shape, -1, np.int32)
    from collections import deque
    queue = deque()
    for j in range(alpha.shape[0]):
        for i in range(alpha.shape[1]):
            if not solid[j, i]:
                dist[j, i] = 0
                queue.append((j, i))
    while queue:
        j, i = queue.popleft()
        for dj, di in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nj, ni = j + dj, i + di
            if 0 <= nj < alpha.shape[0] and 0 <= ni < alpha.shape[1] and dist[nj, ni] < 0:
                dist[nj, ni] = dist[j, i] + 1
                queue.append((nj, ni))
    out = np.where(solid, np.minimum(depth, 1 + dist), 0)
    #! 縁が 1 ボクセルだと影の中で消えるので、下限は 2 にする。
    return np.where(solid, np.maximum(out, min(2, depth)), 0).astype(np.int32)


def build_slab(pix: np.ndarray, depth: int, profile: str, trim_bottom: bool):
    """side×side の RGBA から、ボクセル配列とパレットを作る。一辺は絵の寸法から取る。

    戻り値: (配列(side, side, side), パレット{索引: (r,g,b)}, 統計)
    """
    if pix.shape[0] != pix.shape[1]:
        raise ValueError('絵が正方形でない: %s' % (pix.shape,))
    side = int(pix.shape[0])
    alpha = pix[..., 3]
    solid = alpha > 0
    if not solid.any():
        raise ValueError('不透明な画素が 1 つも無い')

    #! パレット。**色の数を数えてから**索引を振る。
    #! `.vox` のパレットは 255 色が上限で、64px のピクセルパーフェクトなタイルは
    #! **1,729/1,744 枚が 255 色を超える**（最大 2,816 色。2026-08-16 実測）。
    #! 落とすのではなくメディアンカットで 255 へ詰める。形＝ドットの格子は変えず、
    #! **潰したことは必ず数えて報告する**（黙って絵を変えない）。
    rgb = pix[..., :3].reshape(-1, 3)
    keep = solid.reshape(-1)
    uniq, inverse = np.unique(rgb[keep], axis=0, return_inverse=True)
    reduced_from = 0
    if len(uniq) > 255:
        reduced_from = len(uniq)
        quant = Image.fromarray(pix[..., :3], 'RGB').quantize(
            colors=255, method=Image.MEDIANCUT, dither=Image.NONE)
        flat = quant.getpalette()[:255 * 3]
        index = (np.asarray(quant, np.uint8).reshape(-1).astype(np.int32) + 1)
        index[~keep] = 0
        index = index.astype(np.uint8).reshape(alpha.shape)
        palette = {i + 1: (flat[i * 3], flat[i * 3 + 1], flat[i * 3 + 2]) for i in range(255)}
    else:
        index = np.zeros(rgb.shape[0], np.uint8)
        index[keep] = (inverse + 1).astype(np.uint8)
        index = index.reshape(alpha.shape)
        palette = {i + 1: tuple(int(v) for v in uniq[i]) for i in range(len(uniq))}

    thick = depth_map(alpha, depth, profile)
    max_thick = int(thick.max())
    #! y は 1 マスの中で中央に置く。前面（+y 側）を揃えると板ごとに面がずれない。
    y_face = (side + max_thick) // 2          # 面の 1 つ外側
    vol = np.zeros((side, side, side), np.uint8)

    rows = np.nonzero(solid.any(axis=1))[0]
    bottom = int(rows.max())                   # 絵のいちばん下の行（行は上から数える）
    #! 接地（trim_bottom）は実機の前提である（`entity_view.cpp` が z=0 に置く）。
    #! 外すのは見比べ用の焼きだけ。
    #!
    #! **符号を 1 度間違えた。**詰めない版の z は `side-1 - 行` なので、いちばん下の行は
    #! `side-1 - bottom` に居る。これを 0 へ下ろすには**引く**（足すと上へ飛んで、
    #! はみ出したぶんが黙って消える）。実物を `--prefab-check` に掛けて
    #! footprint が 0 のままだったので気づいた。
    shift = -(side - 1 - bottom) if trim_bottom else 0

    for j in range(side):
        for i in range(side):
            t = int(thick[j, i])
            if t <= 0:
                continue
            z = (side - 1 - j) + shift
            if (z < 0) or (z >= side):
                continue
            #! 前面を揃え、厚みは奥（-y）へ伸ばす。
            vol[i, y_face - t:y_face, z] = index[j, i]

    stat = {
        'voxels': int((vol > 0).sum()),
        'colours': len(palette),
        'reduced_from': reduced_from,
        'pixels': int(solid.sum()),
        'max_thick': max_thick,
    }
    return vol, palette, stat


# ============================================================ 三角形を数える
def greedy_quads(vol: np.ndarray) -> int:
    """`hd2d/voxel/greedy_mesher.cpp` と同じ数え方（**色は見ない**＝テクスチャへ移す作り）。

    実行体を起こさずにコストが分かるようにするため。`tools/voxel_probe/greedy.py` の
    第 3 列と同じ算法である。
    """
    merged = 0
    for axis in (0, 1, 2):
        for sign in (+1, -1):
            v = np.moveaxis(vol, axis, 0)
            nxt = np.zeros_like(v)
            if sign > 0:
                nxt[:-1] = v[1:]
            else:
                nxt[1:] = v[:-1]
            face = ((v > 0) & (nxt == 0)).astype(np.uint8)
            for s in range(face.shape[0]):
                m = face[s].copy()
                hgt, wid = m.shape
                for j in range(hgt):
                    i = 0
                    while i < wid:
                        if m[j, i] == 0:
                            i += 1
                            continue
                        w = 1
                        while i + w < wid and m[j, i + w] == 1:
                            w += 1
                        h = 1
                        while j + h < hgt and (m[j + h, i:i + w] == 1).all():
                            h += 1
                        m[j:j + h, i:i + w] = 0
                        merged += 1
                        i += w
    return merged


# ============================================================ 1 枚ぶん
def trim_volume(vol: np.ndarray):
    """空の縁を落として、最小隅を返す（`(詰めた配列, (x, y, z))`）。

    **128px の板を受け入れるために要る**（2026-08-21）。`build_slab()` は一辺³ の
    立方体を作るが、実際に埋まるのは絵の形と厚みぶんだけである。C++ 側の
    `VoxModel::voxels` は**密な `x*y*z` 配列**なので、宣言した大きさがそのまま
    メモリになる——128³ なら 1 枚 2MB、300 枚で 600MB。詰めれば 1/10 以下になる。

    位置は `origin`（プレハブ原点からの最小隅）が持つので、**見た目は 1 画素も動かない**。
    接地（`--trim-bottom`）で絵の底が z=0 に居るから、詰めても origin.z は 0 のまま。
    """
    nz = np.nonzero(vol)
    if len(nz[0]) == 0:
        raise ValueError('ボクセルが 1 つも無い')
    lo = tuple(int(a.min()) for a in nz)
    hi = tuple(int(a.max()) + 1 for a in nz)
    return vol[lo[0]:hi[0], lo[1]:hi[1], lo[2]:hi[2]], lo


def bake_one(name: str, src_path: str, out_dir: str, opt) -> dict:
    pix, origin = load_tile(src_path, opt.px_dir, name, opt.side)
    vol, palette, stat = build_slab(pix, opt.depth, opt.profile, opt.trim_bottom)
    vol, at = trim_volume(vol)
    stat['size'] = 'x'.join(str(n) for n in vol.shape)
    parts = [('main', vol, at)]
    size = ex.write_vox(os.path.join(out_dir, name + '.vox'), parts, palette)
    meta = [{'name': 'main', 'voxels': 'main', 'grounded': True,
             'motion': {'kind': 'static'}, 'wind_k': 0.0}]
    fp = ex.write_prefab(os.path.join(out_dir, name + '.jsonc'), name, parts, meta,
                         note='タイル %s をボクセルの板にしたもの（tools/voxel/tile_to_slab.py）。'
                              '一辺 %d / 厚み %d / %s / 由来 %s'
                              % (name, opt.side, opt.depth, opt.profile, origin),
                         voxels_per_cell=opt.side)
    quads = greedy_quads(vol) if not opt.no_count else 0
    return {'name': name, 'origin': origin, 'bytes': size, 'footprint': len(fp),
            'quads': quads, **stat}


def used_tile_path(name: str, opt) -> str:
    """実際に使う絵の道。px 側があれば px、無ければ `--src` の原本（`load_tile` と同じ序列）。"""
    if opt.px_dir:
        cand = os.path.join(opt.px_dir, name + '.png')
        if os.path.exists(cand):
            return cand
    return os.path.join(opt.src, name + '.png')


def source_key(path: str, opt) -> str:
    """**使う絵**の中身と設定のハッシュ。差し替えたものだけ焼き直すため。

    以前は `--src`（640px）側しか見ていなかったので、`tilework/64` だけ差し替えても
    焼き直しが走らなかった。いまは `used_tile_path()` の絵そのものを噛ませる。
    """
    h = hashlib.sha1()
    with open(path, 'rb') as fp:
        h.update(fp.read())
    h.update(('%d|%d|%s|%d' % (opt.side, opt.depth, opt.profile,
                               int(opt.trim_bottom))).encode())
    return h.hexdigest()


# ============================================================ 検査の検査
def self_check() -> int:
    """わざと壊した入力に反応することを確かめる（設計書 §7.2 / 罠 4）。"""
    import tempfile

    ok = True
    side = DEFAULT_SIDE  # 本番の寸法で検査する。寸法に依らないことは (3) が両方の側で見る

    # (1) `sprite_atlas.cpp` の定数と一致しているか。**食い違いは FAIL。**
    #     （640px の原本を縮小する緊急用の経路だけが使う。px 側はそのまま通る）
    got = cpp_constants()
    for name, mine in (('kAlphaCutoff', ALPHA_CUTOFF), ('kColourLevels', COLOUR_LEVELS)):
        if name not in got:
            print('NG: %s を sprite_atlas.cpp から読めなかった' % name); ok = False
        elif got[name] != mine:
            print('NG: %s が食い違う（C++ %d / この道具 %d）。'
                  '縮小の絵が実機と変わる' % (name, got[name], mine)); ok = False
        else:
            print('OK: %s = %d で一致' % (name, mine))

    # (2) 1 画素 = 1 ボクセル。数が合うか。
    pix = np.zeros((side, side, 4), np.uint8)
    pix[10:20, 5:15] = (200, 100, 50, 255)
    pix[side - 1, 16] = (10, 20, 30, 255)            # いちばん下の行に 1 画素
    vol, pal, stat = build_slab(pix, 4, 'flat', False)
    want = (10 * 10 + 1) * 4
    print('%s: ボクセル数 %d（期待 %d）' % ('OK' if stat['voxels'] == want else 'NG',
                                            stat['voxels'], want))
    ok &= stat['voxels'] == want
    print('%s: 色数 %d（期待 2）' % ('OK' if stat['colours'] == 2 else 'NG', stat['colours']))
    ok &= stat['colours'] == 2

    # (3) 左右と上下の向き。絵の左上の画素が x 小・z 大に出るか。**64 と 32 の両方**で
    #     見る（一辺を変えても向きの式が崩れないこと）。
    for s in (side, 32):
        pix2 = np.zeros((s, s, 4), np.uint8)
        pix2[0, 0] = (255, 0, 0, 255)                # 絵の左上
        v2, _, _ = build_slab(pix2, 2, 'flat', False)
        xs, _, zs = np.nonzero(v2)
        if xs.min() == 0 and zs.min() == s - 1:
            print('OK: 一辺 %d で絵の左上 → x=0 / z=%d' % (s, s - 1))
        else:
            print('NG: 向きが違う（一辺 %d で x=%d z=%d）' % (s, xs.min(), zs.min())); ok = False

    # (4) しきい値。α が cutoff 未満の画素は落ちるか。
    pix3 = np.zeros((side, side, 4), np.uint8)
    pix3[5, 5] = (255, 255, 255, ALPHA_CUTOFF - 1)
    pix3[6, 6] = (255, 255, 255, ALPHA_CUTOFF)
    with tempfile.TemporaryDirectory() as tmp:
        p = os.path.join(tmp, 'T.png')
        Image.fromarray(pix3, 'RGBA').save(p)
        got_pix, origin = load_tile(p, None, 'T', side)
        n = int((got_pix[..., 3] > 0).sum())
        print('%s: しきい値で 1 画素だけ残った（%d 画素・由来 %s）'
              % ('OK' if n == 1 else 'NG', n, origin))
        ok &= n == 1

    # (5) **同寸の入力を再標本化しないこと。**1 画素の細部が生き残るか。
    pix4 = np.zeros((side, side, 4), np.uint8)
    pix4[16, 16] = (7, 11, 13, 255)                  # 減色に載らない半端な色
    with tempfile.TemporaryDirectory() as tmp:
        p = os.path.join(tmp, 'PP.png')
        Image.fromarray(pix4, 'RGBA').save(p)
        got_pix, origin = load_tile(p, None, 'PP', side)
        same = tuple(got_pix[16, 16, :3]) == (7, 11, 13)
        print('%s: ピクセルパーフェクトの色が素通りした（%s / 由来 %s）'
              % ('OK' if same else 'NG', tuple(int(v) for v in got_pix[16, 16, :3]), origin))
        ok &= same

    # (6) 色が 255 を超えたら**詰めたうえで申告する**（黙って絵を変えない）。
    pix5 = np.zeros((side, side, 4), np.uint8)
    k = 0
    for j in range(side):
        for i in range(side):
            pix5[j, i] = (k % 256, (k // 256) % 256, (k // 7) % 256, 255)
            k += 1
    _, pal5, st5 = build_slab(pix5, 2, 'flat', False)
    if len(pal5) <= 255 and st5['reduced_from'] > 255:
        print('OK: 色が %d 種 → 255 へ詰めて、詰めたことを申告した' % st5['reduced_from'])
    else:
        print('NG: 色を詰めたのに申告しなかった（パレット %d / 申告 %d）'
              % (len(pal5), st5['reduced_from'])); ok = False
    #! 詰める必要が無いときに**詰めたと言わない**こと（偽の申告も同じくらい悪い）。
    _, _, st6 = build_slab(pix, 2, 'flat', False)
    if st6['reduced_from'] == 0:
        print('OK: 色が少ないときは詰めたと言わない')
    else:
        print('NG: 詰めていないのに申告した'); ok = False

    # (8) **接地**（trim_bottom）。絵の下端が z = 0 に来ること。
    pix6 = np.zeros((side, side, 4), np.uint8)
    pix6[8:14, 10:16] = (99, 99, 99, 255)          # 上のほうに浮かせた四角
    v_no, _, _ = build_slab(pix6, 2, 'flat', False)
    v_tr, _, st_tr = build_slab(pix6, 2, 'flat', True)
    z_no = int(np.nonzero(v_no)[2].min())
    z_tr = int(np.nonzero(v_tr)[2].min())
    if z_no > 0 and z_tr == 0:
        print('OK: 詰めない z=%d / 詰める z=0' % z_no)
    else:
        print('NG: 接地しなかった（詰めない z=%d / 詰める z=%d）' % (z_no, z_tr)); ok = False
    if int((v_tr > 0).sum()) == int((v_no > 0).sum()):
        print('OK: 詰めてもボクセルは 1 つも消えていない')
    else:
        print('NG: 詰めたらボクセルが減った（%d → %d）'
              % (int((v_no > 0).sum()), int((v_tr > 0).sum()))); ok = False

    # (7) 空の絵は落ちる。
    try:
        build_slab(np.zeros((side, side, 4), np.uint8), 2, 'flat', False)
        print('NG: 空の絵が通った'); ok = False
    except ValueError:
        print('OK: 不透明な画素が無い → 落ちた')

    # (9) `voxels_per_cell` が `.jsonc` へ出るか。板（64）を既定の 32 で割ると
    #     footprint が 2×2 に化けるので、**footprint が (0,0) の 1 マスであること**まで見る。
    vol9 = np.zeros((side, side, side), np.uint8)
    vol9[40, 0, 0] = 1                               # x=40: 32 で割ると隣のマスに化ける位置
    with tempfile.TemporaryDirectory() as tmp:
        p = os.path.join(tmp, 'VPC.jsonc')
        fp9 = ex.write_prefab(p, 'VPC', [('main', vol9, (0, 0, 0))], None,
                              voxels_per_cell=side)
        with open(p, encoding='utf-8') as f:
            body = json.loads(''.join(l for l in f if not l.lstrip().startswith('//')))
        good = (body.get('voxels_per_cell') == side) and (fp9 == [(0, 0)])
        print('%s: voxels_per_cell=%s / footprint=%s（期待 %d / [(0, 0)]）'
              % ('OK' if good else 'NG', body.get('voxels_per_cell'), fp9, side))
        ok &= good
        fp32 = ex.derive_footprint([('main', vol9, (0, 0, 0))])
        if fp32 == [(1, 0)]:
            print('OK: 既定（32）で割ると footprint が化ける＝口が実際に効いている')
        else:
            print('NG: 既定の footprint が %s（期待 [(1, 0)]）' % (fp32,)); ok = False

    # (10) **64px と 128px を混ぜても同じ大きさに出る**（2026-08-21 の受け入れ）。
    #      同じ絵を両方の寸法で焼き、マス単位の大きさ（ボクセル数 / voxels_per_cell）と
    #      footprint が一致することを見る。ここが崩れると、片方の板だけ大きく（小さく）
    #      描かれるのに、どちらも「1 マスに収まっている」ので気づけない。
    def cell_extent(px_side: int):
        px = np.zeros((px_side, px_side, 4), np.uint8)
        q = px_side // 4
        px[q:px_side - q, q:px_side - q] = (180, 90, 40, 255)      # 中央に正方形
        px[px_side - 1, px_side // 2] = (10, 20, 30, 255)          # 接地の 1 画素
        #! 厚みは**本番と同じ既定**（`side // 16`）で測る。ここに固定値を書くと、
        #! 「128px の板だけ実寸が半分の薄さになる」という本物の食い違いを見逃す。
        vol, _pal, _st = build_slab(px, max(1, px_side // 16), 'flat', True)
        tight, at = trim_volume(vol)
        fp = ex.derive_footprint([('main', tight, at)], voxels_per_cell=px_side)
        return tuple(round(n / px_side, 4) for n in tight.shape), tuple(fp)

    e64, e128 = cell_extent(64), cell_extent(128)
    if e64 == e128:
        print('OK: 64px と 128px で大きさが一致（マス単位 %s / footprint %s）' % e64)
    else:
        print('NG: 64px %s と 128px %s で食い違う。混ぜると片方だけ大きさが変わる'
              % (e64, e128)); ok = False

    # (11) 詰めても位置が動かない。**詰めた配列 + origin** が、詰める前と同じマスを占める。
    px = np.zeros((64, 64, 4), np.uint8)
    px[40:60, 44:58] = (200, 100, 50, 255)      # わざと右下に寄せる
    vol, _pal, _st = build_slab(px, 4, 'flat', True)
    fp_full = ex.derive_footprint([('main', vol, (0, 0, 0))], voxels_per_cell=64)
    tight, at = trim_volume(vol)
    fp_tight = ex.derive_footprint([('main', tight, at)], voxels_per_cell=64)
    if fp_full == fp_tight:
        print('OK: 詰めても占めるマスが変わらない（%s）' % (fp_tight,))
    else:
        print('NG: 詰めたら占めるマスが動いた（%s → %s）' % (fp_full, fp_tight)); ok = False

    print('self-check: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


# ============================================================ 本体
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description='タイルをボクセルの板へ焼く')
    ap.add_argument('--src', default=os.path.join(ROOT, 'tilework', 'sfc'),
                    help='640px の原本。--px-dir に無い名前だけ縮小して使う（緊急用）')
    ap.add_argument('--px-dir', default=os.path.join(ROOT, 'tilework', '64'),
                    help='ピクセルパーフェクトな side×side の置き場。あればこちらを'
                         '**そのまま**使う。空文字で無効化')
    ap.add_argument('--out', default=os.path.join(ROOT, 'assets', 'voxel', 'slab'))
    ap.add_argument('--ids', default='', help='名指し（例 R955,K1,P0_0_0）')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--kind', default='RKP',
                    help='--all のときに焼く種別。F（地形）はボクセルへ置き換わるので既定から外してある')
    ap.add_argument('--side', type=int, default=DEFAULT_SIDE, choices=SIDE_LADDER,
                    help='板の一辺（ボクセル＝画素）。voxels_per_cell も同じ値で書くので'
                         '側をいくつにしても 1 マスに収まる。受け入れる段は %s'
                         % '/'.join(str(n) for n in SIDE_LADDER))
    ap.add_argument('--depth', type=int, default=0,
                    help='厚み（ボクセル）。0 = side/16（2026-08-16 に決めた「厚みは従来の半分」。'
                         '64 なら 4 ボクセル ＝ 実寸で旧 32px 板の厚み 4 の半分）')
    ap.add_argument('--profile', choices=('flat', 'dome'), default='flat')
    ap.add_argument('--trim-bottom', action=argparse.BooleanOptionalAction, default=True,
                    help='絵の下の空きを詰めて接地させる。実機は接地前提'
                         '（`entity_view.cpp` が z=0 に置く）なので既定で入り')
    ap.add_argument('--force', action='store_true', help='焼き直しを飛ばさない')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 4) - 1))
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--no-count', action='store_true', help='三角形を数えない（速い）')
    ap.add_argument('--sheet', default='', help='入力の 32px を 1 枚に並べた PNG を書く')
    ap.add_argument('--self-check', action='store_true')
    opt = ap.parse_args(argv)
    if opt.depth <= 0:
        opt.depth = max(1, opt.side // 16)  # 64 なら 4 ＝ 実寸で旧（4/32）の半分（指定した値）

    if opt.self_check:
        return self_check()

    #! **焼く前に定数の一致だけは必ず見る。**食い違ったまま 1,700 枚焼くと全部やり直しになる。
    got = cpp_constants()
    if got.get('kAlphaCutoff') != ALPHA_CUTOFF or got.get('kColourLevels') != COLOUR_LEVELS:
        print('sprite_atlas.cpp と定数が食い違っています（--self-check を見ること）')
        return 1

    names = []
    if opt.ids:
        names = [s.strip() for s in opt.ids.split(',') if s.strip()]
    elif opt.all:
        for fn in sorted(os.listdir(opt.src)):
            if not fn.endswith('.png') or fn.count('.') != 1:
                continue          # `.bak` 等は相手にしない
            if fn[0] in opt.kind:
                names.append(fn[:-4])
    else:
        ap.error('--ids か --all のどちらかが要ります')
    if opt.limit:
        names = names[:opt.limit]

    os.makedirs(opt.out, exist_ok=True)
    stamp_path = os.path.join(opt.out, '.slab_manifest.json')
    stamps = {}
    #! **控えは `--force` でも読む。**書き戻すときは `stamps` を丸ごと書くので、
    #! 読まずに始めると**焼かなかった板の控えが消える**——次に走らせたとき、絵が
    #! 1 画素も変わっていないのに全数を焼き直すことになる（実測 2026-08-27。
    #! `--force FP1` の 1 枚を作った次の回で 472 枚が「古い」と出た）。
    #! `--force` が飛ばすのは**焼くかどうかの判断**であって、控えではない。
    if os.path.exists(stamp_path):
        try:
            with open(stamp_path, encoding='utf-8') as fp:
                stamps = json.load(fp)
        except Exception:
            stamps = {}

    todo = []
    for name in names:
        used = used_tile_path(name, opt)
        if not os.path.exists(used):
            print('  飛ばす（絵が無い）: %s' % name)
            continue
        key = source_key(used, opt)
        if (not opt.force) and stamps.get(name) == key \
                and os.path.exists(os.path.join(opt.out, name + '.vox')):
            continue
        todo.append((name, os.path.join(opt.src, name + '.png'), key))

    print('対象 %d 枚 / 焼くのは %d 枚（残りは前回のまま）' % (len(names), len(todo)))
    done, failed, quads_total, reduced = 0, [], 0, []
    with concurrent.futures.ThreadPoolExecutor(max_workers=opt.jobs) as pool:
        futs = {pool.submit(bake_one, n, s, opt.out, opt): (n, k) for n, s, k in todo}
        for fut in concurrent.futures.as_completed(futs):
            name, key = futs[fut]
            try:
                info = fut.result()
            except Exception as exc:                 # **捏造しない。落ちたものは数える**
                failed.append((name, str(exc)))
                continue
            stamps[name] = key
            quads_total += info['quads']
            done += 1
            if info['reduced_from']:
                reduced.append((name, info['reduced_from']))
            if done <= 8 or done % 200 == 0:
                print('  %-10s 画素 %4d / ボクセル %5d / 色 %3d / 四角形 %5d'
                      ' / 大きさ %-14s / footprint %d / %s'
                      % (name, info['pixels'], info['voxels'], info['colours'],
                         info['quads'], info.get('size', '?'), info['footprint'],
                         info['origin']))

    with open(stamp_path, 'w', encoding='utf-8') as fp:
        json.dump(stamps, fp)

    print('焼けた %d 枚 / 失敗 %d 枚' % (done, len(failed)))
    if reduced:
        #! **黙って絵を変えない。**255 色へ詰めた枚は名前まで出す。
        print('パレット 255 へ詰めた %d 枚（元の色数）:' % len(reduced))
        for name, n in sorted(reduced, key=lambda kv: -kv[1])[:30]:
            print('    %-10s %d 色' % (name, n))
        if len(reduced) > 30:
            print('    …ほか %d 枚' % (len(reduced) - 30))
    if not opt.no_count and done:
        print('四角形の合計 %d（三角形 %d）／ 1 枚あたり平均 %.1f 四角形'
              % (quads_total, quads_total * 2, quads_total / done))
    if failed:
        import collections as _c
        kinds = _c.Counter(why.split('。')[0] for _, why in failed)
        for why, n in kinds.most_common():
            print('  失敗 %d 枚: %s' % (n, why))
        for name, why in failed[:20]:
            print('    %-10s %s' % (name, why))

    if opt.sheet:
        cols = 32
        rows = (len(names) + cols - 1) // cols
        sheet = Image.new('RGBA', (cols * opt.side, max(1, rows) * opt.side), (0, 0, 0, 0))
        for i, name in enumerate(names):
            if not os.path.exists(used_tile_path(name, opt)):
                continue
            try:
                pix, _ = load_tile(os.path.join(opt.src, name + '.png'), opt.px_dir, name, opt.side)
            except Exception:
                continue
            sheet.paste(Image.fromarray(pix, 'RGBA'), ((i % cols) * opt.side, (i // cols) * opt.side))
        os.makedirs(os.path.dirname(os.path.abspath(opt.sheet)) or '.', exist_ok=True)
        sheet.save(opt.sheet)
        print('接触シート: %s' % opt.sheet)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
