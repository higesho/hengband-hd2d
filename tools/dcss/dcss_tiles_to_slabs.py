# -*- coding: utf-8 -*-
"""DCSS のタイル（rltiles の 32x32 PNG）をボクセルの板にする。

既存の `tools/voxel/tile_to_slab.py` をそのまま呼ぶ薄い包み。ほかのコアの板と同じ形式
（`.vox` ＋ `.jsonc`、絵の面を +y 側へ向けた x-z 平面の板）で `dcss/assets/slab/` へ出す。

やること:
  1. 名簿（SPECS）の各絵を読む。**権利が確認できていない絵も、原作で実際に使われているのでそのまま使う**
。作らずに止まる作りは外し、**使った絵を `dcss/assets/slab/tiles_used.tsv` に記録する**
     （来歴の突き合わせは `tools/dcss/tile_provenance.py`）。
  2. 絵が複数のタイルの重ね合わせ（アイテムの見た目）なら、原作と同じ順に重ねて 1 枚にする。
     **ランダムな見た目のアイテム（薬・巻物・ワンド・指輪など）は、原作が「見た目の下地」の上に「種類の札（i-*）」を重ねて描く**
     （`tilepick.cc` の `tileidx_known_base_item`・`tiledgnbuf.cc` の `add_main_tile`）。
     札（`i-ambrosia` など）だけの絵は「札の印」であって薬の絵ではないので、下地（薬なら瓶）と重ねる。
     薬の札は `%back i-label`（`dc-item.txt`）なので瓶・`i-label`・札の順、巻物は `scroll`・`scroll-<色>`・札の順。
  3. 32x32 を最近傍で 2 倍して 64x64 にし、一時の置き場へ書く（他コアの板は 64 が標準。1 画素 = 2x2 ボクセル）。
     `--side 32` にすると 1 画素 = 1 ボクセル（板は同じ 1 マスに収まる）。
  4. tile_to_slab.py を `--src 一時の置き場 --px-dir 一時の置き場` で呼ぶ。

使い方:
  python -u tools/dcss/dcss_tiles_to_slabs.py [--side 64] [--stage <一時フォルダ>] [--out dcss/assets/slab]
  python -u tools/dcss/dcss_tiles_to_slabs.py --list      # 名簿の確認だけ
"""
import argparse
import importlib.util
import os
import tempfile
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
RLTILES = os.path.join(os.path.dirname(ROOT), 'roguelike-cores', 'build-sources', 'dcss', 'crawl-ref', 'source', 'rltiles')
DEFAULT_OUT = os.path.join(ROOT, 'dcss', 'assets', 'slab')
DEFAULT_STAGE = os.path.join(tempfile.gettempdir(), 'dcss_art', 'stage')

#: (板の名前, rltiles からの相対パス, 何の物か)。列挙名は rltiles/dc-*.txt の定義（tools/dcss/map_tile_defs.py）
SPECS = [
    ('dc_mons_jackal', 'mon/animals/jackal', 'MONS_JACKAL ジャッカル'),
    ('dc_mons_bat', 'mon/animals/bat', 'MONS_BAT コウモリ'),
    ('dc_mons_adder', 'mon/animals/adder', 'MONS_ADDER アダー（蛇）'),
    ('dc_mons_orc', 'mon/humanoids/orcs/orc', 'MONS_ORC オーク'),
    ('dc_mons_dwarf', 'mon/humanoids/dwarf', 'MONS_DWARF ドワーフ'),
    ('dc_mons_ogre', 'mon/humanoids/ogre', 'MONS_OGRE オーガ'),
    ('dc_wpn_dagger', 'item/weapon/dagger', 'WPN_DAGGER 短剣'),
    ('dc_arm_chain_mail', 'item/armour/chain_mail1', 'ARM_CHAIN_MAIL 鎖かたびら'),
    ('dc_scr_blinking', ('item/scroll/scroll', 'item/scroll/scroll-purple', 'item/scroll/i-blinking'), '巻物（ブリンク。下地・紫の地・札）'),
    ('dc_wand_flame', ('item/wand/gem_iron', 'item/wand/i-flame'), 'WAND_FLAME 炎の杖（鉄の宝石の下地に札）'),
    ('dc_pot_brown', 'item/potion/brown', '薬（茶色の瓶。未鑑定の見た目）'),
    ('dc_pot_ambrosia', ('item/potion/brown', 'item/potion/i-label', 'item/potion/i-ambrosia'), 'POT_AMBROSIA 薬（茶色の瓶に札を重ねた鑑定済みの見た目）'),
    ('dc_food_apple', 'item/food/apple', '食べ物（りんご）'),
]


def parts_of(rel):
    return [rel] if isinstance(rel, str) else list(rel)


def compose(rel):
    """タイルを下から順に重ねて 1 枚（RGBA）にする。"""
    out = None
    for r in parts_of(rel):
        im = Image.open(os.path.join(RLTILES, r.replace('/', os.sep) + '.png')).convert('RGBA')
        if im.size != (32, 32):
            raise SystemExit('32x32 でない絵: %s %s' % (r, im.size))
        out = im if out is None else Image.alpha_composite(out, im)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--side', type=int, default=64, choices=(32, 64))
    ap.add_argument('--stage', default=DEFAULT_STAGE)
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--list', action='store_true')
    opt = ap.parse_args()
    for n, r, what in SPECS:
        print('%-18s %-34s %s' % (n, '+'.join(parts_of(r)), what))
    if opt.list:
        return 0
    os.makedirs(opt.stage, exist_ok=True)
    for n, r, _ in SPECS:
        im = compose(r)
        if opt.side == 64:
            im = im.resize((64, 64), Image.NEAREST)
        im.save(os.path.join(opt.stage, n + '.png'))
    used = {}
    for n, r, _ in SPECS:
        for p in parts_of(r):
            used.setdefault(p, set()).add(n)
    os.makedirs(opt.out, exist_ok=True)
    with open(os.path.join(opt.out, 'tiles_used.tsv'), 'w', encoding='utf-8', newline='\n') as fp:
        fp.write('# 使った絵の一覧（rltiles からの相対パス、使った名前）。道具が書く。来歴の突き合わせは tools/dcss/tile_provenance.py。\n')
        for p in sorted(used):
            fp.write('%s\t%s\n' % (p, ','.join(sorted(used[p]))))
    spec = importlib.util.spec_from_file_location('tile_to_slab', os.path.join(ROOT, 'tools', 'voxel', 'tile_to_slab.py'))
    t2s = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(t2s)
    argv = ['--src', opt.stage, '--px-dir', opt.stage, '--out', opt.out, '--side', str(opt.side),
            '--ids', ','.join(n for n, _, _ in SPECS), '--force']
    return t2s.main(argv)


if __name__ == '__main__':
    sys.exit(main())
