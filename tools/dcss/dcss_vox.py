# -*- coding: utf-8 -*-
"""DCSS の地形・物のプレハブを作る道具の共通部分（絵の読み込み・ボクセルの画布・書き出し・使った絵の記録）。

`gen_dcss_terrain.py`（壁・床・扉・階段・門・罠）と `gen_dcss_objects.py`（祭壇・噴水・像・木ほか、絵を解釈して一から作る物）が使う。

約束は TOband の `tools/toband/gen_to_terrain.py` と同じ: 1 マス = 32 ボクセル、+x = 東、+y = 南、+z = 上、
床は天面を z = 0 に置く。物は地面へ 2.56 ボクセル沈めて置かれるので、根元を BURIED = 3 ボクセル足す。

権利: 確認できない絵も原作で実際に使われているのでそのまま使う。
代わりに**使った絵を記録する**（`record_use` → `tiles_used.tsv`）。来歴の突き合わせは `tools/dcss/tile_provenance.py`。
"""
import importlib.util
import os

import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
RLTILES = os.path.join(os.path.dirname(ROOT), 'roguelike-cores', 'build-sources', 'dcss', 'crawl-ref', 'source', 'rltiles')
OUT = os.path.join(ROOT, 'dcss', 'assets', 'voxel')

V = 32                 # 1 マス = 32 ボクセル
FLOOR_H = 8            # 床の板の厚み（TOband と同じ。隣のマスとの段差ですき間が空かない厚み）
BURIED = 3             # 物が地面へ沈む 2.56 ボクセルを切り上げたもの
MAX_COLOURS = 100      # 1 枚の絵を詰める上限（奥の面の暗い色を足して 2 倍になっても 255 に収める。床・水は 128 まで許す）
LINE_DARK = 0.10       # 周り 5x5 の平均よりこの割合だけ暗ければ「線」（TOband と同じ）
MATERIALS = ('stone', 'soil', 'wood', 'metal', 'plaster', 'fabric', 'plant', 'clean', 'leaf')

REDUCED = []           # (名前, 元の色数) 詰めた枚の申告
CROPPED = []           # (名前, 絵, 元の寸法) 32x32 に切った枚の申告
USED = {}              # 絵（rltiles からの相対パス）-> {使った名前}


def _load_mod(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ex = _load_mod('export_vox', os.path.join(ROOT, 'tools', 'voxel', 'export_vox.py'))
t2s = _load_mod('tile_to_slab', os.path.join(ROOT, 'tools', 'voxel', 'tile_to_slab.py'))


def set_out(path):
    global OUT
    OUT = path


def record_use(rel, label):
    USED.setdefault(rel, set()).add(label)


def write_used(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as fp:
        fp.write('# 使った絵の一覧（rltiles からの相対パス、使った名前）。道具が書く。来歴の突き合わせは tools/dcss/tile_provenance.py。\n')
        for rel in sorted(USED):
            fp.write('%s\t%s\n' % (rel, ','.join(sorted(USED[rel]))))


# ------------------------------------------------------------------ 絵の読み込み
def read_rgba(rel):
    im = Image.open(os.path.join(RLTILES, rel.replace('/', os.sep) + '.png')).convert('RGBA')
    return np.asarray(im, np.uint8).copy()


def load_tile(rel, label, max_colours=MAX_COLOURS, size=(V, V)):
    """(rgb[行,列,3] int, opaque[行,列]) を返す。多色ならメディアンカットで詰める。使った絵として記録する。"""
    record_use(rel, label)
    a = read_rgba(rel)
    if (a.shape[1], a.shape[0]) != size and a.shape[1] >= size[0] and a.shape[0] >= size[1] and a.shape[1] - size[0] <= 2 and a.shape[0] - size[1] <= 2:
        CROPPED.append((label, rel, (a.shape[1], a.shape[0])))       # 1〜2 画素だけ大きい絵（原作の寸法の誤差）は左上の 32x32 を使う
        a = a[:size[1], :size[0]].copy()
    if (a.shape[1], a.shape[0]) != size:
        raise SystemExit('%s は %s。%s でなければならない' % (rel, (a.shape[1], a.shape[0]), size))
    opaque = a[..., 3] >= t2s.ALPHA_CUTOFF
    rgb = a[..., :3].copy()
    rgb[~opaque] = 0
    n = len({tuple(p) for p in rgb[opaque]})
    if n > max_colours:
        REDUCED.append((label, n, max_colours))
        q = Image.fromarray(rgb, 'RGB').quantize(colors=max_colours, method=Image.MEDIANCUT, dither=Image.NONE)
        pal = np.asarray(q.getpalette()[:max_colours * 3], np.uint8).reshape(-1, 3)
        rgb = pal[np.asarray(q, np.uint8)]
        rgb[~opaque] = 0
    return rgb.astype(int), opaque


def lum(rgb):
    return rgb @ np.array([0.299, 0.587, 0.114])


def px(rel, x, y):
    """絵の 1 画素の色（見て決めた色を絵から拾うのに使う）。"""
    a = read_rgba(rel)
    return tuple(int(v) for v in a[y, x, :3])


def tile_colours(rel, box=None, minalpha=128, n=8):
    """絵（または (x0,y0,x1,y1) の範囲）の主な色を、多い順に n 色返す。"""
    a = read_rgba(rel)
    if box:
        x0, y0, x1, y1 = box
        a = a[y0:y1, x0:x1]
    m = a[..., 3] >= minalpha
    px_ = a[..., :3][m]
    if px_.size == 0:
        return []
    cols, cnt = np.unique(px_.reshape(-1, 3), axis=0, return_counts=True)
    order = np.argsort(-cnt)
    return [tuple(int(v) for v in cols[i]) for i in order[:n]]


def requantize(rgb, opaque, n):
    """色を作り直した絵（色調を合わせた後など）を、メディアンカットで n 色へ詰め直す。"""
    if len({tuple(p) for p in rgb[opaque]}) <= n:
        return rgb
    q = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), 'RGB').quantize(colors=n, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = np.asarray(q.getpalette()[:n * 3], np.uint8).reshape(-1, 3)
    out = pal[np.asarray(q, np.uint8)].astype(int)
    out[~opaque] = 0
    return out


def shade(c, k):
    return tuple(int(max(0, min(255, round(x * k)))) for x in c)


def mix(a, b, t):
    return tuple(int(round(a[i] * (1 - t) + b[i] * t)) for i in range(3))


def hsv(c):
    r, g, b = (x / 255.0 for x in c)
    mx, mn = max(r, g, b), min(r, g, b)
    d = mx - mn
    sat = d / mx if mx > 0 else 0.0
    return sat, mx


def lift_to(rgb, opaque, target_mean):
    """暗すぎる絵（床・水など）の明るさを持ち上げる。平均の明るさが target_mean に満たなければ掛け算で揃える。"""
    l = lum(rgb)[opaque]
    if l.size == 0 or l.mean() >= target_mean:
        return rgb
    k = target_mean / max(l.mean(), 1.0)
    out = np.clip(rgb * k, 0, 255).astype(int)
    out[~opaque] = 0
    return out


def line_mask(rgb, wrap):
    """周り 5x5 の平均より 1 割ほど暗い画素（石の継ぎ目・ひび）。1 画素だけ孤立した所は外す（TOband と同じ）。"""
    l = lum(rgb)
    local = ndimage.uniform_filter(l, 5, mode='wrap' if wrap else 'nearest')
    m = l < local - LINE_DARK * max(np.median(l), 20.0)
    k4 = np.array([[0, 1, 0], [1, 0, 1], [0, 1, 0]])
    n4 = ndimage.convolve(m.astype(int), k4, mode='wrap' if wrap else 'constant')
    return (m & (n4 > 0)) | (~m & (n4 == 4))


def relief_levels(rgb, wrap=True, thr=0.12):
    """凹凸の 3 段（0 = 出っ張り、1 = 基準、2 = へこみ）。周り 5x5 の平均との明るさの差で決める。
    暗い継ぎ目・ひびはへこみ、周りより明るい面（石の面・斜面の光る側）は出っ張る。1 ボクセルほどの凹凸。"""
    l = lum(rgb)
    local = ndimage.uniform_filter(l, 5, mode='wrap' if wrap else 'nearest')
    ref = max(np.median(l), 30.0)
    d = np.ones(l.shape, int)
    d[l > local + thr * ref] = 0
    d[l < local - thr * ref] = 2
    # 1 画素だけ孤立したへこみ・出っ張りは均す（ノイズ）
    for lv in (0, 2):
        m = d == lv
        k4 = np.array([[0, 1, 0], [1, 0, 1], [0, 1, 0]])
        n4 = ndimage.convolve(m.astype(int), k4, mode='wrap' if wrap else 'constant')
        d[m & (n4 == 0)] = 1
    return d


# ------------------------------------------------------------------ パレットと画布
class Palette:
    """(色, 材質) -> 索引。材質は色相から当てる関数 mat_fn（既定は stone）か、呼び出しごとの指定。"""

    def __init__(self, mat_fn=None):
        self.index = {}
        self.mat_of = {}
        self.mat_fn = mat_fn or (lambda c: 'stone')

    def __call__(self, rgb, mat=None):
        rgb = tuple(int(max(0, min(255, c))) for c in rgb)
        m = mat or self.mat_fn(rgb)
        key = (rgb, m)
        if key not in self.index:
            self.index[key] = len(self.index) + 1
        return self.index[key]

    def table(self):
        return {i: k[0] for k, i in self.index.items()}

    def materials(self):
        return {i: k[1] for k, i in self.index.items()}


class Canvas:
    """ボクセルの画布。配列は [x, y, z]、値は索引（0 = 空）。図形は最後に置いた物が勝つ。"""

    def __init__(self, sx, sy, sz, pal):
        self.a = np.zeros((sx, sy, sz), np.int16)
        self.pal = pal
        self.shape = (sx, sy, sz)
        self._g = np.ogrid[:sx, :sy, :sz]

    def col(self, c, mat=None):
        return self.pal(c, mat)

    def box(self, x0, y0, z0, x1, y1, z1, c, mat=None):
        """x0 <= x < x1 の範囲を塗る（範囲外は切る）。"""
        sx, sy, sz = self.shape
        x0, y0, z0 = max(0, int(round(x0))), max(0, int(round(y0))), max(0, int(round(z0)))
        x1, y1, z1 = min(sx, int(round(x1))), min(sy, int(round(y1))), min(sz, int(round(z1)))
        if x1 > x0 and y1 > y0 and z1 > z0:
            self.a[x0:x1, y0:y1, z0:z1] = self.pal(c, mat)

    def mask(self, m, c, mat=None):
        self.a[m] = self.pal(c, mat)

    def ell(self, cx, cy, cz, rx, ry, rz, c, mat=None, clip_z=None):
        X, Y, Z = self._g
        m = ((X + 0.5 - cx) / rx) ** 2 + ((Y + 0.5 - cy) / ry) ** 2 + ((Z + 0.5 - cz) / rz) ** 2 <= 1.0
        if clip_z is not None:
            m &= (Z >= clip_z)
        self.a[m] = self.pal(c, mat)
        return m

    def cyl(self, cx, cy, z0, z1, r0, r1=None, c=(128, 128, 128), mat=None, hollow=0.0):
        """z0 <= z < z1 の円柱（r0 から r1 へ細る）。hollow > 0 なら内側 hollow の半径を空ける。"""
        r1 = r0 if r1 is None else r1
        X, Y, Z = self._g
        t = np.clip((Z + 0.5 - z0) / max(z1 - z0, 1e-6), 0, 1)
        r = r0 + (r1 - r0) * t
        d2 = (X + 0.5 - cx) ** 2 + (Y + 0.5 - cy) ** 2
        m = (d2 <= r ** 2) & (Z >= z0) & (Z < z1)
        if hollow > 0:
            m &= d2 >= (r - hollow) ** 2
        self.a[m] = self.pal(c, mat)
        return m

    def ring(self, cx, cy, z, R, r, c, mat=None, axis='z'):
        """トーラス（環）。axis='z' は水平な環、'y' は南向きに立てた環、'x' は東向きに立てた環。"""
        X, Y, Z = self._g
        if axis == 'z':
            d = np.sqrt((X + 0.5 - cx) ** 2 + (Y + 0.5 - cy) ** 2)
            m = (d - R) ** 2 + (Z + 0.5 - z) ** 2 <= r ** 2
        elif axis == 'y':
            d = np.sqrt((X + 0.5 - cx) ** 2 + (Z + 0.5 - z) ** 2)
            m = (d - R) ** 2 + (Y + 0.5 - cy) ** 2 <= r ** 2
        else:
            d = np.sqrt((Y + 0.5 - cy) ** 2 + (Z + 0.5 - z) ** 2)
            m = (d - R) ** 2 + (X + 0.5 - cx) ** 2 <= r ** 2
        self.a[m] = self.pal(c, mat)
        return m

    def line(self, p0, p1, r, c, mat=None):
        """太さ r の線分（球を連ねる）。"""
        p0, p1 = np.array(p0, float), np.array(p1, float)
        n = int(max(abs(p1 - p0)) * 2) + 1
        X, Y, Z = self._g
        m = np.zeros(self.shape, bool)
        for i in range(n + 1):
            p = p0 + (p1 - p0) * i / n
            m |= (X + 0.5 - p[0]) ** 2 + (Y + 0.5 - p[1]) ** 2 + (Z + 0.5 - p[2]) ** 2 <= r * r
        self.a[m] = self.pal(c, mat)
        return m

    def clear(self, m):
        self.a[m] = 0

    def get(self, x, y, z):
        return self.a[x, y, z]


# ------------------------------------------------------------------ 書き出し
def write_pair(name, parts, pal, note, extra_parts=None):
    path = os.path.join(OUT, name)
    source_index = {}
    table = pal.table()
    mats = pal.materials()
    size = ex.write_vox(path + '.vox', parts, table, source_index_out=source_index)
    entries = []
    for new in sorted(source_index):
        old = source_index[new]
        rgb = table[old]
        mat = mats[old]
        if mat not in MATERIALS:
            raise RuntimeError('%s: 材質の綴りが違う: %r' % (name, mat))
        entries.append({'i': new, 'rgb': '%02X%02X%02X' % tuple(rgb), 'material': mat})
    fp = ex.write_prefab(path + '.jsonc', name, parts, extra_parts=extra_parts, note=note,
                         voxels_per_cell=V, palette_entries=entries)
    return size, len(entries), len(fp)
