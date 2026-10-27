"""DCSS（日本語版）の原作の再構成レシピと manifest の項を作る（「原作の再構成と Linux」）。

原作は日本語版のフォーク crawl-ja（タグ 0.34.1-ja6）。固定コミットのリポジトリの根から必要なファイル（`crawl-ref/` の大半と
`ja-tools/gen_apt_ja.py`・`LICENSE`）を選び、
各ファイルを丸ごと 1 回の範囲コピー（copy）で再構成する。**原作の行の書き換えは 0 件。**
足す物は 2 つだけで、どちらも原作の文ではない（レシピの 'additions'）。

  - `crawl-ref/source/util/release_ver`  版の文字列 1 行。原作の `util/gen_ver.pl` が git の無い場所で読む（原作の配布物も同じ名前の
                                  ファイルを持つ）。無いと build.h が作れない
  - 改行 1 バイト（\\r）           `dat/database/ja/jtrans/decks.txt` だけ、CRLF と LF の行が混ざっている。バイト単位で
                                  同じに戻すための印（`canonical` が改行を LF にそろえて読むため）

付属ライブラリ（lua・pcre・sqlite・zlib）は原作の git のサブモジュール。親のコミットの archive には入らないので、
項目の `submodules`（generate_recipes.original_files が読む）で、サブモジュール自身の固定コミットを足す。
ファイル数が共通の上限（10,000）を超えるので、項目の `max_files` で引き上げる（prepare.py が materialize へ渡す。ほかのコアは既定のまま）。

持つ物（選別は `wanted`）: 原作の C++ ソース・ヘッダ・生成スクリプト・タイル定義と絵（rltiles/ の PNG。tilegen が名前の PNG
が在るかを確かめるので、無いと表が作れない）・実行時のデータ（dat/ の tiles 以外、docs/、settings/）・付属ライブラリの
C ソースとライセンス文。持たない物: dat/tiles（画面表示用の画像）、ウェブサーバ、Android・Mac・Debian の枠、試験、フォント、
サブモジュールの文書と試験データなど。

    python tools/core_sources/dcss_make_recipe.py            # recipes/dcss.json を書き、manifest.json の dcss の項を足す・更新する
    python tools/core_sources/dcss_make_recipe.py --check    # 書かずに、今のレシピと manifest が固定版から作れるか確かめる
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from generate_recipes import original_files, upstream_prefix  # noqa: E402
from reconstruction import canonical, sha  # noqa: E402
from reconstruction_recipe import encode_addition, materialize  # noqa: E402

NAME = 'dcss'
REPO = 'crawl-ja'
UPSTREAM_PATH = '.'   # リポジトリの根（crawl-ref/ と ja-tools/ を両方持つため）
TREE = 'crawl-ref'
COMMIT = '190a5a5e2bc0e7ea1bd6047d2c66656b5d8ad5d8'  # タグ 0.34.1-ja6
VERSION = '0.34.1-ja6'
MAX_FILES = 12000   # 絵（PNG 8,400 個）を含めて 11,000 個を超える。共通の上限 10,000 を、この項だけ引き上げる（manifest の 'max_files'）
SUBMODULES = ['lua', 'pcre', 'sqlite', 'zlib']
UPSTREAM = Path(os.environ.get('HENGBAND_UPSTREAM_ROOT', HERE.parent.parent.parent / 'roguelike-cores' / 'upstream'))

RELEASE_VER = 'crawl-ref/source/util/release_ver'
BINARY_EXT = ('.png', '.pdf', '.psd', '.gif', '.woff', '.woff2', '.icns', '.ico', '.ttf')
LICENSE_NAMES = ('COPYING', 'LICENCE', 'LICENSE', 'README', 'AUTHORS')

# crawl-ref/source の直下で持たない物（開発用の枠・エディタ用・別の組み立て）
TOP_SKIP_EXT = ('.doxy', '.m', '.py', '.gitignore')
# crawl-ref/source の下で、まるごと持たないフォルダ
SOURCE_SKIP_DIRS = ('webserver', 'android-project', 'test', 'catch2-tests', 'debian', 'mac', 'Crawl.xcodeproj',
                    'xdg-data', 'fonts-ja', 'misc', 'scripts', 'contrib', 'MSVC')
DOCS_SKIP_DIRS = ('develop', 'obsolete')


def submodule_pins():
    """親のコミットが指すサブモジュールの固定コミット（git ls-tree）。"""
    repo = UPSTREAM / REPO
    out = subprocess.check_output(['git', '-C', str(repo), '-c', 'safe.directory=' + repo.as_posix(), 'ls-tree', COMMIT,
                                   *[f'{TREE}/source/contrib/{m}' for m in SUBMODULES]]).decode()
    pins = {}
    for line in out.splitlines():
        meta, path = line.split('\t')
        pins[path.rsplit('/', 1)[-1]] = meta.split()[2]
    if sorted(pins) != sorted(SUBMODULES):
        raise SystemExit('サブモジュールの固定コミットが取れない: ' + str(pins))
    return [{'path': f'{TREE}/source/contrib/{m}', 'repo': f'{REPO}/{TREE}/source/contrib/{m}', 'commit': pins[m]}
            for m in SUBMODULES]


def wanted(path: str) -> bool:
    """path はリポジトリの根からの相対。"""
    # 生成の入力に要る日本語版の道具（util/gen-all.py が ../../ja-tools/gen_apt_ja.py を呼ぶ）と、ライセンス文
    if path in ('LICENSE', 'ja-tools/gen_apt_ja.py'):
        return True
    if not path.startswith(TREE + '/'):
        return False
    name = path[len(TREE) + 1:]       # crawl-ref からの相対（source/..., docs/..., settings/..., CREDITS.txt）
    parts = name.split('/')
    base = parts[-1]
    if name == 'CREDITS.txt':
        return True
    if parts[0] == 'settings':
        return True
    if parts[0] == 'docs':
        return len(parts) > 1 and parts[1] not in DOCS_SKIP_DIRS and not name.endswith(BINARY_EXT)
    if parts[0] != 'source' or len(parts) < 2:
        return False
    if len(parts) == 2:                                    # source 直下のファイル
        return not name.endswith(TOP_SKIP_EXT)
    sub = parts[1]
    if sub == 'MSVC':                                      # Windows 用の互換ヘッダ（sys/param.h）だけ
        return len(parts) > 3 and parts[2] == 'include' and name.endswith('.h')
    if sub == 'contrib':                                   # サブモジュールの C ソースとライセンス文だけ
        if len(parts) < 4 or parts[2] not in SUBMODULES:
            return False
        rest = parts[3:]
        if len(rest) == 1:
            return rest[0].endswith(('.c', '.h')) or rest[0] in LICENSE_NAMES
        return rest[0] == 'src' and len(rest) == 2 and rest[1].endswith(('.c', '.h'))   # lua だけ src/ に入っている
    if sub in SOURCE_SKIP_DIRS:
        return False
    if sub == 'rltiles' and name.endswith('.png'):         # タイルの絵。tilegen が表を作るとき、名前の PNG が在るかを開いて確かめる
        return True
    if name.endswith(BINARY_EXT) or base == '.gitignore':
        return False
    if sub == 'dat' and len(parts) > 3 and parts[2] == 'tiles':   # 絵は持たない（別の担当が板にする）
        return False
    return True


def plan_file(key: str, canon: bytes, target: bytes, add):
    """target をコピーの並びにする。ふつうは全体を 1 回の copy。改行が混在するときだけ \\r の印を足す。"""
    bom = target.startswith(b'\xef\xbb\xbf') and b'\0' not in target
    body = target[3:] if bom else target
    spec = {'sha256': sha(target)}
    if b'\0' in target or b'\r' not in body:
        if canon != body:
            raise ValueError('canonical と本文が違う: ' + key)
        spec['operations'] = [['copy', key, 0, len(canon)]] if canon else []
    elif body.count(b'\n') == body.count(b'\r\n') == body.count(b'\r'):   # 全部 CRLF
        spec['operations'] = [['copy', key, 0, len(canon)]] if canon else []
        spec['newline'] = 'crlf'
    else:
        # 混在: 行ごとに、CRLF の行は本文の後ろに \r を足す
        ops = []
        start = pos = 0
        for line in body.split(b'\n'):
            text_len = len(line.rstrip(b'\r'))
            if line.endswith(b'\r'):
                pos += text_len
                ops.append(['copy', key, start, pos - start])
                ops.append(['add', add(b'\r')])
                start = pos
                pos += 1  # \n
            else:
                pos += len(line) + 1
        ops.append(['copy', key, start, len(canon) - start])
        spec['operations'] = [op for op in ops if not (op[0] == 'copy' and op[3] == 0)]
    if bom:
        spec['bom'] = True
    return spec


def build():
    submodules = submodule_pins()
    source = {'name': NAME, 'upstream_repo': REPO, 'upstream_path': UPSTREAM_PATH, 'commit': COMMIT, 'submodules': submodules}
    originals = original_files(UPSTREAM, source)
    prefix = upstream_prefix(source)
    names = sorted(p[len(prefix):] for p in originals if wanted(p[len(prefix):]))
    additions: dict[str, bytes] = {}

    def add(data):
        ident = sha(data)
        additions.setdefault(ident, data)
        return ident

    inputs, files, targets = {}, {}, {}
    for i, name in enumerate(names):
        raw = originals[prefix + name]
        canon = canonical(raw)
        key = f'i{i:05d}'
        inputs[key] = {'archive': 'source', 'path': prefix + name, 'sha256': sha(canon), 'transform': 'raw',
                       'transformed_sha256': sha(canon)}
        files[name] = plan_file(key, canon, raw, add)
        targets[name] = raw
    # 版の文字列（原作の文ではない）
    ver = (VERSION + '\n').encode()
    files[RELEASE_VER] = {'sha256': sha(ver), 'operations': [['add', add(ver)]]}
    targets[RELEASE_VER] = ver
    if RELEASE_VER in names:
        raise SystemExit('原作に release_ver がある')
    used = {op[1] for spec in files.values() for op in spec['operations'] if op[0] == 'copy'}
    recipe = {'schema': 2, 'inputs': {k: inputs[k] for k in sorted(used)},
              'additions': {k: encode_addition(v) for k, v in sorted(additions.items())},
              'files': dict(sorted(files.items()))}
    if materialize(recipe, lambda archive, path: originals[path], max_files=MAX_FILES) != targets:
        raise SystemExit('レシピから作った写しが目標と違う')
    text = json.dumps(recipe, ensure_ascii=False, indent=2) + '\n'
    entry = {
        'name': NAME, 'output': NAME, 'upstream_repo': REPO, 'upstream_path': UPSTREAM_PATH, 'commit': COMMIT,
        'submodules': submodules, 'max_files': MAX_FILES,
        'recipe': f'recipes/{NAME}.json', 'recipe_sha256': hashlib.sha256(text.encode('utf-8')).hexdigest(),
        'files': {name: {'sha256': sha(data)} for name, data in sorted(targets.items())},
    }
    report = {'files': len(files), 'copy_ops': sum(op[0] == 'copy' for s in files.values() for op in s['operations']),
              'additions': len(additions), 'addition_bytes': sum(map(len, additions.values())),
              'bytes': sum(map(len, targets.values())), 'recipe_bytes': len(text.encode('utf-8'))}
    return text, entry, report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    text, entry, report = build()
    recipe_path = HERE / 'recipes' / f'{NAME}.json'
    manifest_path = HERE / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    old = next((s for s in manifest['sources'] if s['name'] == NAME), None)
    if args.check:
        ok = recipe_path.is_file() and recipe_path.read_text(encoding='utf-8') == text and old == entry
        print('一致' if ok else '不一致', report)
        raise SystemExit(0 if ok else 1)
    recipe_path.write_text(text, encoding='utf-8', newline='\n')
    if old is None:
        manifest['sources'].append(entry)
    else:
        manifest['sources'][manifest['sources'].index(old)] = entry
    # manifest.json は ASCII のまま（既存の書式を崩さない）
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=True, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(NAME, report)


if __name__ == '__main__':
    main()
