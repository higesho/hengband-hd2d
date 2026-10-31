"""NetHack の原作の再構成レシピと manifest の項を作る。

原作は 2 つ（どちらも ../roguelike-cores/upstream に写しがあり、roguelike-cores の manifest で固定してある）。

  - nethack    公式の NetHack（`NetHack-5.0` ブランチの 34e91f177）。英語のコア
  - nethackjp  非公式の日本語化 NetHackJP（タグ NetHackJP-5.0.1-20261024）。日本語のコア

固定コミットのリポジトリの根から、組み立てと実行に要るファイルを選び、各ファイルを丸ごと 1 回の範囲コピー（copy）で
再構成する。**原作の行の書き換えは 0 件。足す物も 0 件。**原作を書き換えずに組めることは M0 の下調べで確かめた
（設計書 §6。画面と入口はアダプタ `nethack/adapter/` が持つ）。

持つ物（選別は `wanted`）: `src/`・`include/`・Lua（`nhlua/lua/src` とライセンス文）・`util/` の C（makedefs ほか）・
`dat/`（Lua の階の定義・makedefs の入力・ヘルプほか）・`sys/share` と `sys/unix` と `sys/windows` の C とヘッダ
（Windows の `sysconf.template`・`porthelp` と Linux の `sys/unix/sysconf` を含む）・最上位のライセンス文・
タイルの絵（`win/share/` の `monsters.txt`・`objects.txt`・`other.txt`・`decals.txt`）と、その絵を 1 枚の画像と
glyph → 絵の番号の表にする原作の道具のソース（`tilemap.c`・`tiletxt.c`・`tiletext.c`・`tile2bmp.c`・`tile.h`）。
タイルの絵は `tools/nethack/build_tiles.py` が読み、16×16 の絵を並べた PNG と表を作る（設計書 NETHACK_UI_DESIGN.md §5）。
持たない物: 画面の実装（`win/` のうち上の `win/share/` の 9 本以外）・音（`sound/`・`sounds/`）・文書・試験・
ほかの OS の枠・Visual Studio の枠・タイルのほかの形式の読み書き（`bmptiles.c`・`pngtiles.c`・`gifread.c` など）。

    python tools/core_sources/nethack_make_recipe.py            # recipes/nethack*.json を書き、manifest.json の項を足す・更新する
    python tools/core_sources/nethack_make_recipe.py --check    # 書かずに、今のレシピと manifest が固定版から作れるか確かめる
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from dcss_make_recipe import UPSTREAM, plan_file  # noqa: E402
from generate_recipes import original_files, upstream_prefix  # noqa: E402
from reconstruction import canonical, sha  # noqa: E402
from reconstruction_recipe import encode_addition, materialize  # noqa: E402

SOURCES = [
    {'name': 'nethack', 'repo': 'nethack', 'commit': '34e91f177c2686426e46f5e34847183b35a440e8'},
    {'name': 'nethackjp', 'repo': 'nethackjp', 'commit': 'e140552de54de15f40d92087e3489ba0c46ba895'},
]

BINARY_EXT = ('.png', '.gif', '.ico', '.bmp', '.wav', '.ogg', '.mp3', '.uu', '.xpm', '.ttf', '.pdf')
TOP_FILES = ('README', 'README.md', 'README.JP', 'THIRD_PARTY_NOTICES')
SYS_DIRS = ('sys/share', 'sys/unix', 'sys/windows')
# タイルの絵（テキスト）と、それを画像と表にする原作の道具のソース（`win/share/` の直下）
TILE_FILES = ('monsters.txt', 'objects.txt', 'other.txt', 'decals.txt',
              'tilemap.c', 'tiletxt.c', 'tiletext.c', 'tile2bmp.c', 'tile.h')


def wanted(path: str) -> bool:
    """path はリポジトリの根からの相対。"""
    parts = path.split('/')
    base = parts[-1]
    if base.endswith(BINARY_EXT) or base.startswith('.'):
        return False
    if parts[:2] == ['win', 'share']:
        return len(parts) == 3 and base in TILE_FILES
    if len(parts) == 1:
        return base in TOP_FILES
    top = parts[0]
    if top in ('src', 'include'):
        return len(parts) == 2 and base.endswith(('.c', '.h', '.cpp'))
    if top == 'dat':
        return len(parts) == 2
    if top == 'util':
        return len(parts) == 2 and base.endswith(('.c', '.h'))
    if top == 'nhlua':
        if parts[1:3] == ['lua', 'src']:
            return len(parts) == 4 and base.endswith(('.c', '.h'))
        return parts[1:] == ['lua', 'README'] or parts[1:] == ['README_NHLUA']
    if len(parts) == 3 and '/'.join(parts[:2]) in SYS_DIRS:
        if base.endswith(('.c', '.h', '.cpp')):
            return True
        # 管理者の設定の雛形（Windows は sysconf.template、Linux は sys/unix/sysconf）とポートのヘルプ
        return (parts[1] == 'windows' and base in ('sysconf.template', 'porthelp')) or (parts[1] == 'unix' and base == 'sysconf')
    return False


def build(src):
    source = {'name': src['name'], 'upstream_repo': src['repo'], 'upstream_path': '.', 'commit': src['commit']}
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
    used = {op[1] for spec in files.values() for op in spec['operations'] if op[0] == 'copy'}
    recipe = {'schema': 2, 'inputs': {k: inputs[k] for k in sorted(used)},
              'additions': {k: encode_addition(v) for k, v in sorted(additions.items())},
              'files': dict(sorted(files.items()))}
    if materialize(recipe, lambda archive, path: originals[path]) != targets:
        raise SystemExit('レシピから作った写しが目標と違う: ' + src['name'])
    text = json.dumps(recipe, ensure_ascii=False, indent=2) + '\n'
    entry = {
        'name': src['name'], 'output': src['name'], 'upstream_repo': src['repo'], 'upstream_path': '.',
        'commit': src['commit'],
        'recipe': f"recipes/{src['name']}.json", 'recipe_sha256': hashlib.sha256(text.encode('utf-8')).hexdigest(),
        'files': {name: {'sha256': sha(data)} for name, data in sorted(targets.items())},
    }
    report = {'files': len(files), 'copy_ops': sum(op[0] == 'copy' for s in files.values() for op in s['operations']),
              'additions': len(additions), 'bytes': sum(map(len, targets.values())),
              'recipe_bytes': len(text.encode('utf-8'))}
    return text, entry, report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    manifest_path = HERE / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    ok = True
    for src in SOURCES:
        text, entry, report = build(src)
        recipe_path = HERE / 'recipes' / f"{src['name']}.json"
        old = next((s for s in manifest['sources'] if s['name'] == src['name']), None)
        if args.check:
            same = recipe_path.is_file() and recipe_path.read_text(encoding='utf-8') == text and old == entry
            ok = ok and same
            print(src['name'], '一致' if same else '不一致', report)
            continue
        recipe_path.write_text(text, encoding='utf-8', newline='\n')
        if old is None:
            manifest['sources'].append(entry)
        else:
            manifest['sources'][manifest['sources'].index(old)] = entry
        print(src['name'], report)
    if args.check:
        raise SystemExit(0 if ok else 1)
    # manifest.json は ASCII のまま（既存の書式を崩さない）
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=True, indent=2) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
