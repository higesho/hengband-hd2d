#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DCSS（日本語版）の組み立ての前処理（Linux・Android のホスト用）。Windows は dcss/adapter/prepare_gen.py が同じことをする。

原作は、組む前に Perl と Python のスクリプトでヘッダを生成し、タイルの表（rltiles/tiledef-*.cc）を専用の道具 tilegen で作る。
これらは原作の crawl-ref/source の中へ書き出す作りなので、再構成した原作（build-sources/dcss）を汚さないように、次の手順にする。

  1. <source> を <work>/mirror に複製する（PNG はシンボリックリンク。残りは約 40 MB）。生成スクリプトの作業場所になる。
  2. build.h（版。util/release_ver を読む）・compflag.h・config.h を作り、util/gen-all.py の生成部分を呼ぶ。
  3. 組んでおいた tilegen で、タイル定義 8 本から tiledef-*.{cc,h} を作る（USE_TILE を立てない版なので PNG は存在の確認だけに使う）。
  4. mirror の中の「原作に無かった .h」と rltiles/tiledef-*.{cc,h} だけを <out> に集める。

組み立ての側は <out> をインクルードパスの先頭に足し、tiledef-*.cc を組む。<source> 自体は書き換えない。

    python3 dcss_generate.py --source <build-sources>/dcss --tilegen <tilegen> --work <作業> --out <出力>

要るもの: Python 3 + PyYAML、Perl 5、C++ コンパイラ（config.h の確認用。--cxx）。
"""
import argparse
import filecmp
import os
import shutil
import subprocess
import sys

TILE_LISTS = ('main', 'dngn', 'floor', 'wall', 'feat', 'player', 'gui', 'icons')


def run(cmd, cwd, env):
    print('+', ' '.join(cmd), '  (cwd=%s)' % cwd, flush=True)
    r = subprocess.run(cmd, cwd=cwd, env=env)
    if r.returncode != 0:
        sys.exit('失敗（終了コード %d）: %s' % (r.returncode, ' '.join(cmd)))


def link_png_copy_rest(src, dst):
    # 絵（PNG 8,000 個超）は tilegen が存在を確かめるだけで読まない。コピーせずリンクにして時間を縮める
    if src.endswith('.png'):
        os.symlink(os.path.abspath(src), dst)
    else:
        shutil.copy2(src, dst)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--source', required=True, help='再構成した原作の根（crawl-ref/ と ja-tools/ を持つ build-sources/dcss）')
    ap.add_argument('--tilegen', required=True, help='組んでおいた tilegen（rltiles/tool）')
    ap.add_argument('--work', required=True, help='作業フォルダ（mirror を作る）')
    ap.add_argument('--out', required=True, help='生成物の出力先')
    ap.add_argument('--cxx', default=os.environ.get('CXX', 'g++'), help='config.h の確認に使うコンパイラ')
    ap.add_argument('--std', default='-std=gnu++14')
    ap.add_argument('--host', default='x86_64-linux-gnu', help='compflag.h に書く文字列（動作には影響しない）')
    ap.add_argument('--perl', default=shutil.which('perl'))
    a = ap.parse_args()
    if not a.perl:
        sys.exit('perl が見つかりません')
    up_src = os.path.join(a.source, 'crawl-ref', 'source')
    if not os.path.isfile(os.path.join(up_src, 'AppHdr.cc')):
        sys.exit('原作が見つかりません: ' + up_src)
    tilegen = os.path.abspath(a.tilegen)
    mirror = os.path.join(os.path.abspath(a.work), 'mirror')
    msrc = os.path.join(mirror, 'crawl-ref', 'source')
    out = os.path.abspath(a.out)

    env = dict(os.environ)
    # 版は util/release_ver から読ませる。上の階層に別の git が在っても拾わせない（git describe が別の版を返す）
    env['GIT_CEILING_DIRECTORIES'] = os.path.dirname(mirror)

    # 1. 写し
    if os.path.isdir(mirror):
        shutil.rmtree(mirror)
    shutil.copytree(a.source, mirror, copy_function=link_png_copy_rest)   # ハードリンクにはしない（生成物が原作側を書き換えないように）

    # 2. 版・コンパイラ設定・config.h・util/gen-all.py の生成部分
    run([a.perl, 'util/gen_ver.pl', 'build.h', ''], msrc, env)
    run([a.perl, 'util/gen-cflg.pl', 'compflag.h', '<UNKNOWN>', '<UNKNOWN>', a.host, a.host.split('-')[0]], msrc, env)
    run([sys.executable, '-I', 'util/configure.py', a.cxx, a.std], msrc, env)
    code = ("import sys, shutil, importlib.util;"
            "spec = importlib.util.spec_from_file_location('genall', 'util/gen-all.py');"
            "m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m);"
            "m.build_rtiles = lambda: None;"
            "m.gen_all(shutil.which('perl'))")
    run([sys.executable, '-I', '-c', code], msrc, env)

    # 3. タイルの表
    rl = os.path.join(msrc, 'rltiles')
    for t in TILE_LISTS:
        run([tilegen, 'dc-%s.txt' % t], rl, env)
    run([sys.executable, '-I', 'util/status-icon-sizes-gen.py', 'rltiles/icon-sizes.txt'], msrc, env)

    # 4. 生成物だけを out に集める
    if os.path.isdir(out):
        shutil.rmtree(out)
    n = 0
    changed = []
    for dirpath, _dirs, files in os.walk(msrc):
        rel_dir = os.path.relpath(dirpath, msrc)
        for fn in files:
            rel = os.path.normpath(os.path.join(rel_dir, fn))
            in_root = rel_dir == '.'
            in_rl = rel_dir == 'rltiles'
            wanted = ((in_root and fn.endswith('.h')) or
                      (in_rl and fn.startswith('tiledef-') and fn.endswith(('.cc', '.h'))) or
                      (in_rl and fn.startswith('status-icon-sizes') and fn.endswith('.h')) or
                      (in_rl and fn == 'tiledef_defines.h'))
            if not wanted:
                continue
            orig = os.path.join(up_src, rel)
            if os.path.isfile(orig):
                if not filecmp.cmp(orig, os.path.join(msrc, rel), shallow=False):
                    changed.append(rel)
                continue
            dst = os.path.join(out, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(msrc, rel), dst)
            n += 1
    if changed:
        sys.exit('原作と違う内容になった既存のファイルがあります（想定外）: %s' % changed)
    print('生成物 %d 個を %s に集めました。' % (n, out))


if __name__ == '__main__':
    main()
