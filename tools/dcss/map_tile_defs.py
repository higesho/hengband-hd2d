# -*- coding: utf-8 -*-
"""rltiles の定義（dc-*.txt）を読み、PNG とゲームの物（列挙名）の対応表を作る。読むだけ。

定義の書式（`rltiles/tool/tile_list_processor.cc` が読む）:
  %sdir <相対ディレクトリ>   以降の絵の置き場（rltiles 直下から）
  <ファイル名（拡張子なし）> [<列挙名> ...]   1 行 1 枚。列挙名が無い行は直前の列挙名の別の絵（見た目の差）
  %include / %compose / %hue / %variation などは数だけ数える

使い方:
  python -u tools/dcss/map_tile_defs.py [--src <rltiles>] [--tsv <出力>] [--find <部分文字列>]
出力の TSV: png(rltiles 直下からの相対) / 定義ファイル / 最後に見えた列挙名
"""
import argparse
import collections
import os
import sys

DEFAULT_SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), 'roguelike-cores', 'build-sources', 'dcss', 'crawl-ref', 'source', 'rltiles')


def parse(src):
    seen = collections.OrderedDict()   # png 相対 -> (定義ファイル, 列挙名)
    missing = []
    directive_counts = collections.Counter()
    for fn in sorted(os.listdir(src)):
        if not (fn.startswith('dc-') and fn.endswith('.txt')):
            continue
        sdir = ''
        last_enum = ''
        with open(os.path.join(src, fn), encoding='utf-8', errors='replace') as fp:
            for raw in fp:
                line = raw.strip()
                if not line or line.startswith('#'):
                    continue
                tok = line.split()
                if line.startswith('%'):
                    directive_counts[tok[0]] += 1
                    if tok[0] == '%sdir' and len(tok) > 1:
                        sdir = tok[1]
                    elif tok[0] == '%sdir':
                        sdir = ''
                    continue
                name = tok[0]
                cand = [os.path.join(sdir, name + '.png'), name + '.png']
                found = None
                for c in cand:
                    if os.path.exists(os.path.join(src, c)):
                        found = c.replace(os.sep, '/')
                        break
                if len(tok) > 1:
                    last_enum = tok[1]
                if found is None:
                    missing.append((fn, sdir, name))
                    continue
                #: 同じ絵を死体（dc-corpse）やゾンビ（dc-zombie）が先に指すことがある。本体の定義を優先する。
                if found not in seen or (seen[found][0] in ('dc-corpse.txt', 'dc-zombie.txt')
                                         and fn not in ('dc-corpse.txt', 'dc-zombie.txt')):
                    seen[found] = (fn, last_enum)
    return seen, missing, directive_counts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default=DEFAULT_SRC)
    ap.add_argument('--tsv', default='')
    ap.add_argument('--find', default='')
    opt = ap.parse_args()
    seen, missing, dc = parse(opt.src)
    allpng = []
    for r, _d, files in os.walk(opt.src):
        for f in files:
            if f.lower().endswith('.png'):
                allpng.append(os.path.relpath(os.path.join(r, f), opt.src).replace(os.sep, '/'))
    allset = set(allpng)
    unref = sorted(p for p in allset if p not in seen)
    print('PNG 全体 %d 枚 / 定義が指す PNG %d 枚 / 定義に載らない PNG %d 枚 / 定義が指す先に実在しない行 %d'
          % (len(allset), len(seen), len(unref), len(missing)))
    by = collections.Counter(p.split('/')[0] for p in unref)
    print('定義に載らない PNG の 1 段目:', dict(by))
    byfile = collections.Counter(v[0] for v in seen.values())
    print('定義ファイルごとの PNG 数:')
    for k, v in sorted(byfile.items()):
        print('  %-20s %5d' % (k, v))
    print('指令の数:', dict(dc))
    if opt.find:
        for p, (fn, en) in seen.items():
            if opt.find in p or opt.find in en:
                print('%s\t%s\t%s' % (p, fn, en))
    if opt.tsv:
        with open(opt.tsv, 'w', encoding='utf-8') as fp:
            for p, (fn, en) in seen.items():
                fp.write('%s\t%s\t%s\n' % (p, fn, en))
    return 0


if __name__ == '__main__':
    sys.exit(main())
