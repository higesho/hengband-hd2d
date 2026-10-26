"""共有のプリコンパイル済みヘッダ（platform/shared/hengband_core_pch.h）の安全性を検査する。

変愚蛮怒・短愚蛮怒のコアは、この 1 つのヘッダを 3 つの環境（Windows・Linux・Android）で
プリコンパイルして全ソースの先頭に強制インクルードする。プリコンパイル済みヘッダは
ソースより先に処理されるので、次の 2 つが起きると動作が黙って変わる:

1. ソースが `#define` / `#undef` した後にインクルードするヘッダの中身が、そのマクロで
   変わる（`#if` / `#ifdef` / `defined()` で見ている）場合。
2. インクルードガードの無いヘッダを入れた場合（二重に取り込まれる）。

この道具は、ヘッダ一覧に挙げたヘッダと、そこから推移的にインクルードされる原作のヘッダ
（以下「一覧の閉包」）が条件分岐で見ているマクロの集合を作り、各ソース（.cpp）を
インクルード順にたどって、一覧の閉包のヘッダへ最初に届くより前に定義・取り消された
マクロと突き合わせる。重なりがあれば失敗（終了コード 1）にする。
標準ライブラリのヘッダ（stdafx.h が前から取り込んでいる分）は対象外。

使い方:
    python tools/core_sources/pch_safety_check.py [--src <src の場所>]...

`--src` を省くと、Windows 用の targeting overlay（`<再構成先>-targeting-windows/src`）を、
無ければ overlay.py が作る木を、検査する。
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from paths import prepared_root

UI_ROOT = Path(__file__).resolve().parents[2]
PCH_HEADER = UI_ROOT / 'platform' / 'shared' / 'hengband_core_pch.h'
# コアのビルドから外れるソース（CMake の除外一覧と同じ）
EXCLUDE = re.compile(r'(^|/)(main\.cpp|main-win\.cpp|main-x11\.cpp|main-gcu\.cpp|main-cap\.cpp|maid-x11\.cpp)$'
                     r'|^main-win/|^main-unix/(x11-|unix-music\.cpp|unix-sound\.cpp|stack-trace-unix\.cpp)'
                     r'|^test/|^term/(z-term|screen-processor)\.cpp$')

DIRECTIVE = re.compile(r'^[ \t]*#[ \t]*(include|define|undef|ifdef|ifndef|if|elif|pragma)\b(.*)$', re.M)
INCLUDE_ARG = re.compile(r'^\s*"([^"]+)"')
IDENT = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
NOT_MACRO = {'defined', '__has_include', '__has_cpp_attribute', '__has_attribute', 'true', 'false'}


def strip_comments(text):
    text = text.replace('\r\n', '\n').replace('\\\n', ' ')
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


class Tree:
    """1 つの src の木。ファイルごとの指示行を 1 度だけ読む。"""

    def __init__(self, src):
        self.src = Path(src).resolve()
        self.roots = [self.src, self.src / 'external-lib' / 'include']
        self._cache = {}

    def directives(self, path):
        path = Path(path)
        got = self._cache.get(path)
        if got is None:
            text = strip_comments(path.read_text(encoding='utf-8', errors='replace'))
            got = [(m.group(1), m.group(2).strip(), text.count('\n', 0, m.start()) + 1) for m in DIRECTIVE.finditer(text)]
            self._cache[path] = got
        return got

    def resolve(self, including, name):
        for base in [Path(including).parent] + self.roots:
            cand = (base / name)
            if cand.is_file():
                return cand.resolve()
        return None

    def includes(self, path):
        for kind, arg, line in self.directives(path):
            if kind == 'include':
                m = INCLUDE_ARG.match(arg)
                if m:
                    yield m.group(1), line


def pch_list(tree=None):
    """共有ヘッダが `#include "…"` で挙げている原作のヘッダ名（stdafx.h を含む）。

    `#if __has_include("…")`・`#if !__has_include("…")` の塊だけは、`tree` でそのヘッダが見えるかで判じ、
    外れる側の `#include` を数えない（変愚 3.0.2.6 で消えた term/z-form.h を、3.0.2.4 の短愚の木でだけ含める。2026-10-27）。
    ほかの形の `#if` は従来どおり中の `#include` を全部数える。"""
    text = strip_comments(PCH_HEADER.read_text(encoding='utf-8-sig'))
    names, active = [], [True]
    for line in text.splitlines():
        m = re.match(r'^[ \t]*#[ \t]*if[ \t]+(!?)[ \t]*__has_include[ \t]*\([ \t]*"([^"]+)"[ \t]*\)[ \t]*$', line)
        if m:
            seen = tree is None or tree.resolve(tree.src / 'x', m.group(2)) is not None
            active.append(active[-1] and (seen != (m.group(1) == '!')))
            continue
        if re.match(r'^[ \t]*#[ \t]*if', line):
            active.append(active[-1])
            continue
        if re.match(r'^[ \t]*#[ \t]*endif', line) and len(active) > 1:
            active.pop()
            continue
        m = re.match(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', line)
        if m and active[-1]:
            names.append(m.group(1))
    return names


def has_guard(tree, path):
    ds = tree.directives(path)
    if any(k == 'pragma' and a.startswith('once') for k, a, _ in ds):
        return True
    if len(ds) >= 2 and ds[0][0] == 'ifndef' and ds[1][0] == 'define':
        return ds[0][1].split()[0] == ds[1][1].split()[0]
    return False


def guard_macro(tree, path):
    ds = tree.directives(path)
    if len(ds) >= 2 and ds[0][0] == 'ifndef' and ds[1][0] == 'define' and ds[0][1].split()[0] == ds[1][1].split()[0]:
        return ds[0][1].split()[0]
    return None


def tested_macros(tree, path):
    out = set()
    guard = guard_macro(tree, path)
    for kind, arg, _ in tree.directives(path):
        if kind in ('ifdef', 'ifndef'):
            names = arg.split()[:1]
        elif kind in ('if', 'elif'):
            names = [n for n in IDENT.findall(arg) if n not in NOT_MACRO]
        else:
            continue
        out.update(n for n in names if n != guard)
    return out


def order_problems(tree, members, closure):
    """一覧の並びで先に処理される側が、後から定義されるマクロを見ていないか。

    ソースは原作の順（たとえば h-config.h が SET_UID を定義してから h-system.h が見る）で読むが、
    共有ヘッダは一覧の並びで読む。並びが逆だと、同じマクロがソースでは定義済み・ヘッダでは未定義になる。
    """
    defining = {}
    for path in closure:
        for kind, arg, _ in tree.directives(path):
            if kind == 'define':
                m = IDENT.match(arg)
                if m:
                    defining.setdefault(m.group(0), set()).add(path)
    problems, defined, visited = [], set(), set()

    def walk(path):
        if path in visited:
            return
        visited.add(path)
        guard = guard_macro(tree, path)
        for kind, arg, line in tree.directives(path):
            if kind == 'include':
                m = INCLUDE_ARG.match(arg)
                sub = tree.resolve(path, m.group(1)) if m else None
                if sub in closure:
                    walk(sub)
            elif kind == 'define':
                m = IDENT.match(arg)
                if m:
                    defined.add(m.group(0))
            elif kind in ('ifdef', 'ifndef', 'if', 'elif'):
                if kind in ('ifdef', 'ifndef'):
                    names = arg.split()[:1]
                else:
                    names = [n for n in IDENT.findall(arg) if n not in NOT_MACRO]
                for n in names:
                    owners = defining.get(n)
                    if n != guard and owners and n not in defined and path not in owners:
                        problems.append(
                            f'一覧の並び: {path.relative_to(tree.src).as_posix()}:{line} が見る {n} は、後から'
                            f' {sorted(o.relative_to(tree.src).as_posix() for o in owners)[0]} が定義する'
                            f'（原作の読み順と逆）。一覧で定義する側を先に置く')
    for path in members:
        walk(path)
    return problems


def check_tree(src):
    tree = Tree(src)
    problems = []
    listed = pch_list(tree)
    if not listed or listed[0] != 'stdafx.h':
        problems.append(f'{PCH_HEADER.name}: 最初のインクルードは "stdafx.h" でなければならない')
    members = {}   # 一覧のヘッダ（解決済みの絶対パス）
    closure = set()
    stack = []
    for name in listed:
        path = tree.resolve(PCH_HEADER, name) or tree.resolve(tree.src / 'x', name)
        if path is None:
            problems.append(f'{PCH_HEADER.name}: "{name}" が {tree.src} から解決できない')
            continue
        members[path] = name
        stack.append(path)
    for path, name in members.items():
        if name != 'stdafx.h' and not has_guard(tree, path):
            problems.append(f'{name}: インクルードガード（#pragma once か #ifndef/#define の組）が無い')
    while stack:
        path = stack.pop()
        if path in closure:
            continue
        closure.add(path)
        for name, _ in tree.includes(path):
            sub = tree.resolve(path, name)
            if sub is not None and sub not in closure:
                stack.append(sub)
    for path in closure:
        if path not in members and not has_guard(tree, path):
            problems.append(f'{path.relative_to(tree.src).as_posix()}: 一覧の閉包に入るが、インクルードガードが無い')
    tested = set()
    where = {}
    for path in closure:
        for macro in tested_macros(tree, path):
            tested.add(macro)
            where.setdefault(macro, path.relative_to(tree.src).as_posix())
    problems += order_problems(tree, members, closure)
    reports = {}
    tus = 0
    for cpp in sorted(tree.src.rglob('*.cpp')):
        rel = cpp.relative_to(tree.src).as_posix()
        if rel.startswith('external-lib/') or EXCLUDE.search(rel):
            continue
        tus += 1
        defined = {}      # マクロ名 -> (どのファイルの何行目か)
        reached = set()
        visited = set()

        def visit(path):
            if path in visited:
                return
            visited.add(path)
            for kind, arg, line in tree.directives(path):
                if kind in ('define', 'undef'):
                    m = IDENT.match(arg)
                    if m:
                        defined.setdefault(m.group(0), (path, line, kind))
                elif kind == 'include':
                    m = INCLUDE_ARG.match(arg)
                    sub = tree.resolve(path, m.group(1)) if m else None
                    if sub is None:
                        continue
                    if sub in closure:
                        if sub not in reached:
                            reached.add(sub)
                            for macro, origin in list(defined.items()):
                                if macro in tested:
                                    reports.setdefault(macro, []).append((rel, origin, sub))
                    else:
                        visit(sub)

        visit(cpp.resolve())
    for macro, hits in sorted(reports.items()):
        rel, origin, sub = hits[0]
        problems.append(
            f'マクロ {macro}: {len(hits)} 個のソースが、一覧の閉包のヘッダ（{sub.relative_to(tree.src).as_posix()} など）へ届く前に'
            f' {origin[2]} している（例 {rel} ← {origin[0].relative_to(tree.src).as_posix()}:{origin[1]}）。'
            f'一方、閉包の {where[macro]} が条件分岐で見ている。ヘッダを一覧から外すか、定義を別に直す')
    return tus, len(closure), len(tested), problems


def source_trees(explicit):
    if explicit:
        return [Path(p) for p in explicit], None
    # 変愚（src）と、変愚 3.0.2.4 の写しで組む短愚（tangband/src）の両方を見る（同じ共有ヘッダを使うため）
    root = prepared_root()
    overlay = root.with_name(root.name + '-targeting-windows')
    if (overlay / 'src').is_dir():
        return [p for p in (overlay / 'src', overlay / 'tangband' / 'src') if p.is_dir()], None
    tmp = tempfile.TemporaryDirectory(prefix='pch-overlay-')
    subprocess.run([sys.executable, str(UI_ROOT / 'tools' / 'targeting' / 'overlay.py'),
                    '--sources', str(root), '--output', tmp.name], check=True, stdout=subprocess.DEVNULL)
    return [p for p in (Path(tmp.name) / 'src', Path(tmp.name) / 'tangband' / 'src') if p.is_dir()], tmp


def run(explicit=()):
    trees, keep = source_trees(list(explicit))
    all_problems = []
    for tree in trees:
        tus, files, macros, problems = check_tree(tree)
        print(f'pch_safety_check: {tree}: ソース {tus} 個、一覧の閉包 {files} ファイル、条件分岐で見るマクロ {macros} 個、問題 {len(problems)} 件')
        all_problems += problems
    if keep:
        keep.cleanup()
    return all_problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', action='append', default=[], help='検査する src の木（省略時は targeting overlay）')
    args = ap.parse_args()
    problems = run(args.src)
    if problems:
        print('FAIL: 共有プリコンパイル済みヘッダが安全でない:', file=sys.stderr)
        for p in problems:
            print('  ' + p, file=sys.stderr)
        sys.exit(1)
    print('PASS: 共有プリコンパイル済みヘッダ。ソースが先に定義するマクロで中身が変わるヘッダは無く、全ヘッダにガードがある')


if __name__ == '__main__':
    main()
