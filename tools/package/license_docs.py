#!/usr/bin/env python3
"""配布物の licenses/ に入れる使用許諾の文書を、すべての配布物（Windows・Linux・Android）で同じ作り方で用意する。

入れるもの
  * GPL-2.0.txt                GNU 一般公衆利用許諾書 第 2 版の全文（Angband を GPL v2 で配るため）。
  * windows-runtime/*.txt      Windows の配布物に同梱する実行時ライブラリ（.dll）の著作権と使用許諾の原文。
                               vcpkg の share/<パッケージ>/copyright をそのまま写したもの（OpenAL Soft は LGPL v2 の全文を含む）。
  * RogueClone2/               RogueClone2 の原作の資料（原作の配布条件 (2)「すべてのドキュメントを付属させること」）。
                               原作は EUC-JP なので UTF-8 に写す（TOband の jlicense.txt と同じ扱い）。
  * Umoria-LICENSE.txt        Umoria の GPL v3（原作の LICENSE。build_windows.py --data-only が umoria/data/ へ写したもの）。
  * Toband-*                   TOband の Moria/Angband 使用許諾（readme_angband）・日本語の使用許諾（jlicense.txt。原作は EUC-JP なので UTF-8 に写す）・
                               クエストの地図の元になった NetHack の使用許諾（LICENSE §1.3）。
  * Dcss-*                     DCSS（GPL v2 以降）の使用許諾・著作者名簿・CC0・付属ライブラリ（lua・pcre・sqlite・zlib）の通知と、
                               タイルの来歴の記録（--dcss で再構成した原作の置き場を渡したとき。 4.8）。

使い方
  python tools/package/license_docs.py --dest <licenses の出力先> [--windows-runtime | --only SDL2 freetype ...] [--no-rogue2] [--dcss <build-sources/dcss>]
"""
from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LICENSE_ROOT = REPO / 'third_party' / 'licenses'
WINDOWS_RUNTIME = LICENSE_ROOT / 'windows-runtime'

# RogueClone2 の原作（固定版）のうち、資料にあたるもの。配布条件 (2) が「すべてのドキュメント」を求める。
# Makefile・configure・ソース・メッセージ表（mesg*）は資料ではないので入れない。
ROGUE2_DOCS = [
    'README', 'COPYING', 'AUTHORS', 'NEWS', 'INSTALL', 'rogue.doc',
    'man/rogue.man',
    'doc/options.doc', 'doc/rogue.doc', 'doc/rogue.txt', 'doc/rogue.jax',
    'doc/rogue-guide.txt', 'doc/rogue-guide.jax', 'doc/rogue-man.txt', 'doc/rogue-man.jax',
    'doc/rogue_s/README', 'doc/rogue_s/README.1st', 'doc/rogue_s/README.doc',
    'doc/rogue_s/options.doc', 'doc/rogue_s/rogue.doc', 'doc/rogue_s/rogue.man',
]


#: DCSS（再構成した原作 build-sources/dcss の下の道）。配布物の名前 -> 原作の中の道。原作は GPL v2 以降で、
#: 付属ライブラリ（lua は MIT、pcre は BSD、sqlite は公有、zlib は zlib 使用許諾）とタイル（主に CC0）は別の条件のため、通知を添える。
#: 字（BIZUDGothic）の OFL は assets/fonts/dcss/OFL.txt に別にある。
DCSS_LICENSES = [
    ('Dcss-LICENSE.txt', 'LICENSE'),
    ('Dcss-CREDITS.txt', 'crawl-ref/CREDITS.txt'),
    ('Dcss-CC0.txt', 'crawl-ref/'),
    ('Dcss-lua-LICENSE.txt', 'crawl-ref/'),
    ('Dcss-worley.txt', 'crawl-ref/'),
    ('Dcss-pcre-LICENCE.txt', 'crawl-ref/source/contrib/pcre/LICENCE'),
    ('Dcss-sqlite-README.txt', 'crawl-ref/source/contrib/sqlite/README'),
    ('Dcss-zlib-README.txt', 'crawl-ref/source/contrib/zlib/README'),
]
#: タイルの来歴の記録（このリポジトリの dcss/assets。tools/dcss/tile_provenance.py が書く）。CC0 と確認できない絵を使っていることの根拠。
DCSS_PROVENANCE = [
    ('Dcss-tiles-used-provenance.tsv', 'dcss/assets/tiles_used_provenance.tsv'),
    ('Dcss-tiles-unclear-provenance.tsv', 'dcss/assets/tile_provenance_unclear.tsv'),
]


def upstream_root() -> Path:
    env = os.environ.get('HENGBAND_UPSTREAM_ROOT')
    return Path(env) if env else REPO.parent / 'roguelike-cores' / 'upstream'


def _to_utf8(data: bytes) -> bytes:
    """UTF-8 ならそのまま、そうでなければ EUC-JP として UTF-8 に直す。読めなければ止める（黙って壊さない）。"""
    try:
        data.decode('utf-8')
        return data
    except UnicodeDecodeError:
        return data.decode('euc_jp').encode('utf-8')


def copy_gpl(dest: Path) -> list[str]:
    src = LICENSE_ROOT / 'GPL-2.0.txt'
    if not src.is_file():
        raise FileNotFoundError(src)
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dest / 'GPL-2.0.txt')
    return ['GPL-2.0.txt']


def copy_windows_runtime(dest: Path, only: list[str] | None = None, subdir: str = 'windows-runtime') -> list[str]:
    """only は SDL2・freetype のようなファイル名（拡張子なし）の一覧。None なら全部。"""
    names = sorted(p.stem for p in WINDOWS_RUNTIME.glob('*.txt'))
    if not names:
        raise FileNotFoundError(WINDOWS_RUNTIME)
    if only is not None:
        missing = [n for n in only if n not in names]
        if missing:
            raise FileNotFoundError('実行時ライブラリの使用許諾が見つかりません: ' + ', '.join(missing))
        names = [n for n in names if n in only]
    out = dest / subdir
    out.mkdir(parents=True, exist_ok=True)
    for n in names:
        shutil.copyfile(WINDOWS_RUNTIME / (n + '.txt'), out / (n + '.txt'))
    return [subdir + '/' + n + '.txt' for n in names]


def copy_openal(dest: Path) -> list[str]:
    """OpenAL Soft（LGPL v2。共有ライブラリとして同梱）の COPYING の全文。vcpkg の写しは COPYING の全文と pffft の通知を含む。"""
    src = WINDOWS_RUNTIME / 'openal-soft.txt'
    if not src.is_file():
        raise FileNotFoundError(src)
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dest / 'OpenAL-Soft-COPYING.txt')
    return ['OpenAL-Soft-COPYING.txt']


def copy_rogue2_docs(dest: Path, upstream: Path | None = None) -> list[str]:
    root = (upstream or upstream_root()) / 'rogue2'
    out = dest / 'RogueClone2'
    copied = []
    for rel in ROGUE2_DOCS:
        src = root / rel
        if not src.is_file():
            raise FileNotFoundError('RogueClone2 の資料がありません: ' + str(src))
        dst = out / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(_to_utf8(src.read_bytes()))
        copied.append('RogueClone2/' + rel)
    return copied


#: TOband の使用許諾の文書（原作の置き場の下の道）。配布物の名前 -> (原作の中の道, 原作の文字コード)。LICENSE §1.3。
#: Windows の配布物（Build-Hd2dPackage.ps1）・Linux の配布物（build_linux_package.py）が入れるのと同じ 3 つ。jlicense.txt は原作が EUC-JP。
TOBAND_LICENSES = [
    ('Toband-readme_angband.txt', 'readme_angband', None),
    ('Toband-nethack-license.txt', 'lib/file/nethack-license.txt', None),
    ('Toband-jlicense.txt', 'lib/help/jlicense.txt', 'euc_jp'),
]


def copy_umoria(dest: Path) -> list[str]:
    """Umoria の GPL v3（原作の LICENSE）。`python tools/umoria/build_windows.py --data-only` が umoria/data/ へ写している。"""
    src = REPO / 'umoria' / 'data' / 'LICENSE'
    if not src.is_file():
        raise FileNotFoundError('Umoria の使用許諾がありません（python tools/umoria/build_windows.py --data-only を先に）: ' + str(src))
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dest / 'Umoria-LICENSE.txt')
    return ['Umoria-LICENSE.txt']


def copy_toband(dest: Path, upstream: Path | None = None) -> list[str]:
    """TOband の使用許諾の文書（原作の固定版から）。原作が EUC-JP のもの（jlicense.txt）は UTF-8 に写す。読めなければ止める。"""
    root = (upstream or upstream_root()) / 'toband'
    dest.mkdir(parents=True, exist_ok=True)
    copied = []
    for out_name, rel, encoding in TOBAND_LICENSES:
        src = root / rel
        if not src.is_file():
            raise FileNotFoundError('TOband の使用許諾の文書がありません: ' + str(src))
        data = src.read_bytes()
        (dest / out_name).write_bytes(data.decode(encoding).encode('utf-8') if encoding else data)
        copied.append(out_name)
    return copied


def dcss_sources_root() -> Path:
    """再構成した DCSS の原作（tools/core_sources/prepare.py が作る build-sources/dcss）。HENGBAND_CORE_SOURCE_ROOT で替えられる。"""
    env = os.environ.get('HENGBAND_CORE_SOURCE_ROOT')
    base = Path(env) if env else REPO.parent / 'roguelike-cores' / 'build-sources'
    return base / 'dcss'


def copy_dcss(dest: Path, sources: Path | None = None) -> list[str]:
    """DCSS の使用許諾・著作者名簿・付属ライブラリの通知（再構成した原作から）と、タイルの来歴の記録（このリポジトリから）を写す。"""
    root = sources or dcss_sources_root()
    dest.mkdir(parents=True, exist_ok=True)
    copied = []
    for out_name, rel in DCSS_LICENSES:
        src = root / rel
        if not src.is_file():
            raise FileNotFoundError('DCSS の使用許諾の文書がありません（python tools/core_sources/prepare.py を先に）: ' + str(src))
        shutil.copyfile(src, dest / out_name)
        copied.append(out_name)
    for out_name, rel in DCSS_PROVENANCE:
        src = REPO / rel
        if not src.is_file():
            raise FileNotFoundError(src)
        shutil.copyfile(src, dest / out_name)
        copied.append(out_name)
    return copied


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dest', type=Path, required=True)
    ap.add_argument('--windows-runtime', action='store_true', help='Windows の実行時ライブラリの使用許諾を全部入れる')
    ap.add_argument('--only', nargs='*', help='入れる実行時ライブラリを名前で絞る（例: SDL2 SDL2_image SDL2_ttf freetype）')
    ap.add_argument('--no-rogue2', action='store_true', help='RogueClone2 の資料を入れない')
    ap.add_argument('--no-gpl', action='store_true')
    ap.add_argument('--upstream', type=Path)
    ap.add_argument('--dcss', nargs='?', const='', default=None,
                    help='DCSS の使用許諾を入れる。値は再構成した原作の置き場（省略時は ../roguelike-cores/build-sources/dcss）')
    args = ap.parse_args(argv)
    done: list[str] = []
    if not args.no_gpl:
        done += copy_gpl(args.dest)
    if args.windows_runtime:
        done += copy_windows_runtime(args.dest)
    elif args.only:
        done += copy_windows_runtime(args.dest, args.only)
    if not args.no_rogue2:
        done += copy_rogue2_docs(args.dest, args.upstream)
    if args.dcss is not None:
        done += copy_dcss(args.dest, Path(args.dcss) if args.dcss else None)
    for name in done:
        print(name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
