#!/usr/bin/env python3
"""配布物の licenses/ に入れる使用許諾の文書を、すべての配布物（Windows・Linux・Android）で同じ作り方で用意する。

入れるもの
  * GPL-2.0.txt                GNU 一般公衆利用許諾書 第 2 版の全文（Angband を GPL v2 で配るため）。
  * windows-runtime/*.txt      Windows の配布物に同梱する実行時ライブラリ（.dll）の著作権と使用許諾の原文。
                               vcpkg の share/<パッケージ>/copyright をそのまま写したもの（OpenAL Soft は LGPL v2 の全文を含む）。
  * RogueClone2/               RogueClone2 の原作の資料（原作の配布条件 (2)「すべてのドキュメントを付属させること」）。
                               原作は EUC-JP なので UTF-8 に写す（TOband の jlicense.txt と同じ扱い）。

使い方
  python tools/package/license_docs.py --dest <licenses の出力先> [--windows-runtime | --only SDL2 freetype ...] [--no-rogue2]
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


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dest', type=Path, required=True)
    ap.add_argument('--windows-runtime', action='store_true', help='Windows の実行時ライブラリの使用許諾を全部入れる')
    ap.add_argument('--only', nargs='*', help='入れる実行時ライブラリを名前で絞る（例: SDL2 SDL2_image SDL2_ttf freetype）')
    ap.add_argument('--no-rogue2', action='store_true', help='RogueClone2 の資料を入れない')
    ap.add_argument('--no-gpl', action='store_true')
    ap.add_argument('--upstream', type=Path)
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
    for name in done:
        print(name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
