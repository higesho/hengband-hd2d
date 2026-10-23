#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DCSS コアの組み立ての前処理: 原作が組む前に作るヘッダ・タイルの表を、原作の写しを汚さずに作る。

原作（日本語版のフォーク crawl-ja）は、組む前に Perl と Python のスクリプトでヘッダを生成し、
タイルの表（rltiles/tiledef-*.cc）を専用の道具 tilegen で作る。これらは原作の
crawl-ref/source の中へ書き出す作りなので、原作の写しを書き換えないために、次の手順にする。

  1. 原作の crawl-ref と ja-tools を <work>/mirror に複製する（.git は除く）。
     ここが生成スクリプトの作業場所であり、実行時のデータ置き場（-dir/-datadir の dat/）でもある。
  2. tilegen を cl（MSVC）で組む。USE_TILE を立てないので libpng は要らず、表（.cc/.h/.js）だけ作れる。
  3. build.h（版）・compflag.h・config.h を作り、util/gen-all.py の生成部分を呼ぶ。
  4. mirror の中の「原作に無かった .h」と rltiles/tiledef-*.{cc,h} を <work>/gen に集める。
     DcssCore.vcxproj の DcssGenRoot はこの gen を指す。
     （mirror 全体を DcssGenRoot にしてはいけない。同じ名前のヘッダが原作と複製の両方から
      読まれ、二重定義になる。）

要るもの: Python 3 + PyYAML、Perl（Git for Windows の usr/bin/perl.exe でよい）、
Visual Studio の cl.exe（vcvars64.bat 経由）、git（版の取得。無ければ util/release_ver を作る）。
どれも PATH に無いことが多いので、場所は を見る。引数か環境変数で指す。

使い方:
  python dcss/adapter/prepare_gen.py --work <作業フォルダ>
  --upstream の既定は、再構成した原作（roguelike-cores/build-sources/dcss。tools/core_sources/prepare.py が作る。版は util/release_ver から読む）。
  原作の写し（upstream/crawl-ja）の作業ツリーは固定の版と限らないので、既定にしない（2026-10-22、ja4 へ上げたとき）。
  → <作業フォルダ>/gen と <作業フォルダ>/mirror ができる。
"""
import argparse
import filecmp
import os
import shutil
import subprocess
import sys

DEFAULT_UPSTREAM = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), 'roguelike-cores', 'build-sources', 'dcss')
DEFAULT_VCVARS = (r'C:\Program Files\Microsoft Visual Studio\18\Community'
                  r'\VC\Auxiliary\Build\vcvars64.bat')
DEFAULT_PERL_DIR = r'C:\Program Files\Git\usr\bin'


def longpath(p):
    p = os.path.abspath(p)
    prefix = '\\\\?\\'
    if os.name == 'nt' and not p.startswith(prefix):
        return prefix + p
    return p


def run(cmd, cwd, env=None, shell=False):
    print('+', cmd if isinstance(cmd, str) else ' '.join(cmd), '  (cwd=%s)' % cwd, flush=True)
    r = subprocess.run(cmd, cwd=cwd, env=env, shell=shell)
    if r.returncode != 0:
        sys.exit('失敗（終了コード %d）: %s' % (r.returncode, cmd))


def vc_run(vcvars, line, cwd, env):
    """vcvars64.bat を通した環境で 1 行のコマンドを実行する。"""
    bat = os.path.join(cwd, '_dcss_vc.bat')
    with open(bat, 'w', encoding='ascii') as f:
        f.write('@echo off\r\ncall "%s" >nul 2>&1\r\ncd /d "%s"\r\n%s\r\n' % (vcvars, cwd, line))
    try:
        run(['cmd', '/c', bat], cwd, env)
    finally:
        os.remove(bat)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--upstream', default=os.environ.get('DCSS_UPSTREAM', DEFAULT_UPSTREAM),
                    help='原作（crawl-ja）のリポジトリの根')
    ap.add_argument('--work', required=True, help='出力先の作業フォルダ（mirror と gen を作る）')
    ap.add_argument('--vcvars', default=os.environ.get('VCVARS64', DEFAULT_VCVARS))
    ap.add_argument('--perl-dir', default=os.environ.get('PERL_DIR', DEFAULT_PERL_DIR),
                    help='perl.exe のあるフォルダ')
    ap.add_argument('--refresh', action='store_true', help='mirror が既にあっても作り直す')
    a = ap.parse_args()

    up_src = os.path.join(a.upstream, 'crawl-ref', 'source')
    if not os.path.isfile(os.path.join(up_src, 'AppHdr.cc')):
        sys.exit('原作が見つかりません: ' + up_src)
    mirror = os.path.join(a.work, 'mirror')
    gen = os.path.join(a.work, 'gen')
    msrc = os.path.join(mirror, 'crawl-ref', 'source')

    env = dict(os.environ)
    env['PATH'] = a.perl_dir + os.pathsep + env['PATH']
    perl = shutil.which('perl', path=env['PATH'])
    if not perl:
        sys.exit('perl が見つかりません。--perl-dir で perl.exe のあるフォルダを指してください。')

    # 1. 複製
    if a.refresh and os.path.isdir(mirror):
        shutil.rmtree(mirror)
    if not os.path.isdir(msrc):
        print('複製: %s -> %s' % (a.upstream, mirror), flush=True)
        # 作業フォルダが深いと、rltiles の奥のファイルが 260 文字の上限を越える。
        # 複製のときだけ \?\ 付きの長いパスにする。
        shutil.copytree(longpath(a.upstream), longpath(mirror), ignore=shutil.ignore_patterns('.git'))

    # tilegen は PNG の有無を調べるだけでも通常のパス（260 文字まで）で開く。
    # 原作の最長のパスは 118 文字（rltiles/mon/tentacles/...）なので、作業フォルダは短くする。
    longest = max(len(os.path.join(dp, f)) for dp, _d, fs in os.walk(longpath(mirror)) for f in fs) - 4
    if longest >= 255:
        sys.exit('作業フォルダが深すぎます（最長のパスが %d 文字）。短いフォルダを --work に指してください。' % longest)

    # 2. tilegen（USE_TILE なし＝libpng 不要）
    tg = os.path.join(msrc, 'tilegen.exe')
    if not os.path.isfile(tg):
        objdir = os.path.join(a.work, 'tilegen_obj')
        os.makedirs(objdir, exist_ok=True)
        vc_run(a.vcvars,
               'cd /d "%s" && cl /nologo /O2 /EHsc /std:c++17 /D_CRT_SECURE_NO_WARNINGS /utf-8 '
               'main.cc tile.cc tile_colour.cc tile_list_processor.cc tile_page.cc '
               '/Fe:"%s" /Fo:"%s/"' % (os.path.join(msrc, 'rltiles', 'tool'), tg, objdir.replace(os.sep, '/')),
               a.work, env)

    # 3. 版・コンパイラ設定・config.h
    #    版は git describe から。複製に .git が無いので、原作の側で実行して複製へ書き出す。
    run([perl, 'util/gen_ver.pl', os.path.join(msrc, 'build.h'), ''], up_src, env)
    run([perl, 'util/gen-cflg.pl', 'compflag.h', '<UNKNOWN>', '<UNKNOWN>',
         'x64-Windows-msvc', 'x64-msvc'], msrc, env)
    vc_run(a.vcvars,
           'python -I util/configure-msvc.py "%VCToolsInstallDir%bin\\Hostx64\\x64\\cl.exe" "%INCLUDE%"',
           msrc, env)

    # util/gen-all.py の生成部分。最後の build_rtiles()（PNG の取り込みまで含む）は
    # 使わず、下の tilegen の呼び出しに替える。
    code = ("import sys, shutil, importlib.util;"
            "spec = importlib.util.spec_from_file_location('genall', 'util/gen-all.py');"
            "m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m);"
            "m.build_rtiles = lambda: None;"
            "m.gen_all(shutil.which('perl'))")
    run([sys.executable, '-I', '-c', code], msrc, env)

    rl = os.path.join(msrc, 'rltiles')
    for t in ('main', 'dngn', 'floor', 'wall', 'feat', 'player', 'gui', 'icons'):
        run([tg, 'dc-%s.txt' % t], rl, env)
    run([sys.executable, '-I', 'util/status-icon-sizes-gen.py', 'rltiles/icon-sizes.txt'], msrc, env)

    # 4. 生成物だけを gen に集める
    if os.path.isdir(gen):
        shutil.rmtree(gen)
    n = 0
    changed = []
    for dirpath, _dirs, files in os.walk(msrc):
        rel_dir = os.path.relpath(dirpath, msrc)
        top = rel_dir.split(os.sep)[0]
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
            dst = os.path.join(gen, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(msrc, rel), dst)
            n += 1
    if changed:
        sys.exit('原作と違う内容になった既存のファイルがあります（想定外）: %s' % changed)
    print('生成物 %d 個を %s に集めました。' % (n, gen))
    print('組み立て: MSBuild DcssCore.vcxproj /p:DcssGenRoot=%s' % gen)
    print('実行時のデータ置き場: %s（dat/ を含む）' % msrc)


if __name__ == '__main__':
    main()
