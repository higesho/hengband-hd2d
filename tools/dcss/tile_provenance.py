# -*- coding: utf-8 -*-
"""DCSS のタイルの来歴を、`github.com/crawl/tiles` の一覧と今の木（rltiles）を突き合わせて調べる。

決めたこと（2026-10-21）: 権利が確認できない絵も、原作で実際に使われているのでそのまま使う。
その代わりに「使った絵の一覧と来歴」を残す（配布物の使用許諾の文書に載せる材料）。このスクリプトがそれを作る。

読むもの:
  * `--tiles-repo <dir>`: `git clone https://github.com/crawl/tiles` した所（`TILES_UNDER_UNKNOWN_LICENSE.md` と `ARTISTS.md`）。
    一覧は「2010 年時点で持ち主が不明、または作者に連絡が付かなかった」絵で、配布のパッケージから外されている。
    書式は 3 通り（無題の節＝ファイル名だけ／`rltiles/...` の相対パス／`crawl-ref/source/rltiles/...`）。節の見出しは作者名か `# コメント`。
  * `--rltiles <dir>`: 今の木（既定は crawl-ja の `rltiles`）。一覧のパスは旧い置き場（`dc-dngn/` など）で書かれた物があるので、
    `dc-dngn→dngn` のように読み替えて探す。見つからなければファイル名だけで探す（同名であって同じ絵とは限らない）。
  * `dcss/assets/voxel/tiles_used.tsv`・`dcss/assets/slab/tiles_used.tsv`: 道具（`gen_dcss_terrain.py`・`dcss_tiles_to_slabs.py`）が書く、使った絵。

書くもの（どちらも `dcss/assets/`）:
  * `tile_provenance_unclear.tsv`: 一覧の全項目（節・記載・今の木での対応・原作の定義が使うか・本案件で使うか）
  * `tiles_used_provenance.tsv`: 本案件が使った絵ごとの来歴の区分

使い方:
  python -u tools/dcss/tile_provenance.py --tiles-repo <crawl/tiles の写し>
"""
import argparse
import collections
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import map_tile_defs  # noqa: E402

DEFAULT_RLTILES = os.path.join(os.path.dirname(ROOT), 'roguelike-cores', 'build-sources', 'dcss', 'crawl-ref', 'source', 'rltiles')
ASSETS = os.path.join(ROOT, 'dcss', 'assets')
DIRMAP = {'dc-dngn': 'dngn', 'dc-mon': 'mon', 'dc-item': 'item', 'dc-misc': 'misc', 'dc-player': 'player',
          'dc-gui': 'gui', 'dc-spells': 'spells', 'dc-UNUSED': 'UNUSED'}


def parse_list(path):
    """(節, 記載) の並び。節は作者名・コメント・無題。"""
    entries = []
    sec = '(無題の節)'
    for i, line in enumerate(open(path, encoding='utf-8').read().split('\n')):
        if not line.strip() or i < 2 and not line.startswith('-'):
            continue
        if line.startswith('# '):
            sec = line[2:].strip()[:80]
            continue
        m = re.match(r'^(\t*)- (.*)$', line)
        if m:
            v = m.group(2).strip()
            if re.search(r'\.(png|PNG|jpg|txt|ttf|ogg)$', v) or '/' in v:
                entries.append((sec, v))
            else:
                sec = v
            continue
        sec = line.strip()
    return entries


def norm(p):
    p = p.replace('\\', '/')
    for pre in ('crawl-ref/source/rltiles/', 'rltiles/', 'crawl-ref/source/dat/', 'dat/'):
        if p.startswith(pre):
            p = p[len(pre):]
            break
    parts = p.split('/')
    if parts[0] in DIRMAP:
        parts[0] = DIRMAP[parts[0]]
    return '/'.join(parts)


def read_used(path):
    used = {}
    if os.path.exists(path):
        for line in open(path, encoding='utf-8'):
            if line.startswith('#') or not line.strip():
                continue
            rel, names = line.rstrip('\n').split('\t')
            used[rel] = names
    return used


RE_MAIL = re.compile(r'([A-Za-z0-9._+-]+)@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+')


def redact(text):
    """上流の一覧の節名に載っている作者の連絡先のメールアドレスを伏せる（`name@(略)`）。配布物の使用許諾の文書に個人の連絡先を載せないため。"""
    return RE_MAIL.sub(lambda m: m.group(1) + '@(略)', text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tiles-repo', required=True)
    ap.add_argument('--rltiles', default=DEFAULT_RLTILES)
    opt = ap.parse_args()
    entries = parse_list(os.path.join(opt.tiles_repo, 'TILES_UNDER_UNKNOWN_LICENSE.md'))
    alltiles = set()
    for dp, _dn, fn in os.walk(opt.rltiles):
        for f in fn:
            if f.lower().endswith('.png'):
                alltiles.add(os.path.relpath(os.path.join(dp, f), opt.rltiles).replace(os.sep, '/'))
    bybase = collections.defaultdict(list)
    for r in alltiles:
        bybase[os.path.basename(r)].append(r)
    seen, _missing, _dc = map_tile_defs.parse(opt.rltiles)       # 原作の定義が使う絵
    used = {}
    for sub in ('voxel', 'slab'):
        for rel, names in read_used(os.path.join(ASSETS, sub, 'tiles_used.tsv')).items():
            used.setdefault(rel + '.png', set()).update(names.split(','))
    rows = []
    matched = {}           # 今の木の絵 -> (節, 区分)
    for sec, raw in entries:
        if '/' in raw:
            n = norm(raw)
            if n in alltiles:
                kind, hits = '同じパス', [n]
            else:
                hits = bybase.get(os.path.basename(n), [])
                kind = '同名のみ' if hits else '今の木に無い'
        else:
            hits = bybase.get(raw, [])
            kind = '同名のみ（無題の節は名前だけ）' if hits else '今の木に無い'
        for h in hits:
            if h not in matched or kind == '同じパス':
                matched[h] = (sec, kind)
        rows.append((sec, raw, kind, hits))
    out1 = os.path.join(ASSETS, 'tile_provenance_unclear.tsv')
    nethack = [h for h, (s, k) in matched.items() if 'nethack' in s.lower()]
    with open(out1, 'w', encoding='utf-8', newline='\n') as fp:
        fp.write('# 来歴が CC0 と確認できていない絵の一覧（github.com/crawl/tiles の TILES_UNDER_UNKNOWN_LICENSE.md の全項目と、今の木の突き合わせ）。\n')
        fp.write('# 道具: tools/dcss/tile_provenance.py。一覧の主旨: 「2010 年時点で持ち主が不明、または作者に連絡が付かなかった」絵（配布のパッケージから外されている）。\n')
        fp.write('# 区分: 同じパス＝読み替えたパスが今の木にある／同名のみ＝ファイル名だけ一致（同じ絵とは限らない）／今の木に無い＝消された・改名された。\n')
        fp.write('# 原作で使う＝今の木の rltiles/dc-*.txt の定義が指す絵（map_tile_defs.py の読み取り。%compose などは追っていない）。\n')
        fp.write('# 節\t一覧の記載\t区分\t今の木の絵\t原作で使う\t本案件で使う\n')
        for sec, raw, kind, hits in rows:
            fp.write('%s\t%s\t%s\t%s\t%s\t%s\n' % (
                redact(sec), redact(raw), kind, ' | '.join(hits),
                ' | '.join('はい' if h in seen else 'いいえ' for h in hits),
                ' | '.join(','.join(sorted(used[h])) if h in used else '' for h in hits)))
    out2 = os.path.join(ASSETS, 'tiles_used_provenance.tsv')
    nflag = 0
    with open(out2, 'w', encoding='utf-8', newline='\n') as fp:
        fp.write('# 本案件のプレハブ・板が使った絵の来歴の区分（tools/dcss/tile_provenance.py が書く）。\n')
        fp.write('# 区分: 一覧に載る＝CC0 と確認できていない（原作で実際に使われているのでそのまま使う。決めたこと 2026-10-21）／'
                 '載らない＝一覧に無い（CC0 または公有の RLTiles 由来の見込み。作者の表記の義務はどこにも書かれていない）\n')
        fp.write('# 絵\t区分\t節\t一致の種類\t使った名前\n')
        for rel in sorted(used):
            if rel in matched:
                sec, kind = matched[rel]
                nflag += 1
                fp.write('%s\t一覧に載る\t%s\t%s\t%s\n' % (rel, redact(sec), kind, ','.join(sorted(used[rel]))))
            else:
                fp.write('%s\t載らない\t\t\t%s\n' % (rel, ','.join(sorted(used[rel]))))
    sections = collections.Counter(s for s, _r, _k, _h in rows)
    print('一覧の項目 %d（節 %d）' % (len(rows), len(sections)))
    print('今の木の絵に対応した数（重複を除く）: %d（同じパス %d・同名のみ %d）' % (
        len(matched), sum(1 for v in matched.values() if v[1] == '同じパス'), sum(1 for v in matched.values() if v[1] != '同じパス')))
    print('  うち原作の定義が使う絵: %d' % sum(1 for h in matched if h in seen))
    print('  NetHack 由来（公有でない。NetHack のライセンス）: %d  %s' % (len(nethack), ' '.join(nethack)))
    print('今の木に無い項目: %d' % sum(1 for r in rows if r[2] == '今の木に無い'))
    print('本案件が使った絵 %d 枚のうち、一覧に載る: %d 枚' % (len(used), nflag))
    for rel in sorted(used):
        if rel in matched:
            print('  載る: %s  (%s / %s)' % (rel, matched[rel][0], matched[rel][1]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
