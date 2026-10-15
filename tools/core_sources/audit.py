"""UI の原作ソース除去と、再構成したソースのハッシュを検査する。"""
import argparse
import os
import subprocess
import sys
import xml.etree.ElementTree as ET
from paths import prepared_root
import leak_scan
import pch_safety_check
from prepare import UI_ROOT, DEFAULT_UPSTREAM, prepare, HERE

ORIGINAL_TREES = ('src', 'gensoband/src', 'silq/src', 'frox/src', 'hengband-3.0.2.4-Beta')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--extra-file', action='append', default=[], help='自己検査用: 追跡外のファイルも走査に足す')
    ap.add_argument('--leaks-only', action='store_true', help='原作の文の漏れ検査だけを行う')
    args = ap.parse_args()
    upstream = os.environ.get('HENGBAND_UPSTREAM_ROOT', str(DEFAULT_UPSTREAM))
    problems = leak_scan.run(upstream, args.extra_file)
    if problems:
        print('FAIL: 原作ソースの文が漏れている疑い（空白除去した行の照合）:', file=sys.stderr)
        for p in problems:
            print('  ' + p, file=sys.stderr)
        sys.exit(1)
    print('PASS: 原作の文の漏れなし（追跡ファイルを原作の行索引と照合）')
    if args.leaks_only:
        return
    # 共有プリコンパイル済みヘッダ（platform/shared/hengband_core_pch.h）の安全性。
    # ソースが先に定義するマクロで中身が変わるヘッダや、ガードの無いヘッダが一覧に入っていないか。
    pch_problems = pch_safety_check.run()
    if pch_problems:
        print('FAIL: 共有プリコンパイル済みヘッダが安全でない（tools/core_sources/pch_safety_check.py）:', file=sys.stderr)
        for p in pch_problems:
            print('  ' + p, file=sys.stderr)
        sys.exit(1)
    print('PASS: 共有プリコンパイル済みヘッダ。ソースが先に定義するマクロで中身が変わるヘッダは無く、全ヘッダにガードがある')
    if list((HERE / "patches").glob("*.patch")):
        raise RuntimeError("Old source-body patches remain in the active source tools")
    for path in ORIGINAL_TREES:
        if (UI_ROOT / path).exists() or (UI_ROOT / path).is_symlink():
            raise RuntimeError('Original source still exists inside UI: ' + path)
    tracked = subprocess.check_output(['git', '-C', str(UI_ROOT), '-c', 'safe.directory=' + UI_ROOT.as_posix(),
                                       'ls-files', '-z', '--', *ORIGINAL_TREES])
    if tracked:
        raise RuntimeError('Original source is still tracked by the UI repository')
    project = ET.parse(UI_ROOT / 'VisualStudio/HengbandHd2d/HengbandHd2d.vcxproj')
    dependencies = []
    for node in project.iter():
        kind = node.tag.rsplit('}', 1)[-1]
        if kind == 'AdditionalIncludeDirectories':
            dependencies.append(node.text or '')
        elif kind in ('ClCompile', 'ClInclude', 'ResourceCompile'):
            dependencies.append(node.attrib.get('Include', ''))
    if any('..\\..\\src' in value or 'HengbandCoreSourceRoot' in value for value in dependencies):
        raise RuntimeError('UI project still refers to core sources')
    prepare(DEFAULT_UPSTREAM, prepared_root(), verify_only=True)
    print('PASS: no original source trees in UI; external prepared source hashes match the baseline')


if __name__ == '__main__':
    main()
