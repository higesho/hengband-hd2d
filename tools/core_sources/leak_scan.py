"""原作ソースの文がリポジトリへ漏れていないかを検査する（行の照合）。

原作（manifest の各ソースの固定コミット）の行を空白除去で索引にし、追跡中のファイルと突き合わせる。
.py は文字列定数だけを見る（パッチ用スクリプトに原作の文を埋め込む例があったため）。
"""
import ast
import fnmatch
import subprocess
import sys
import json
import os
from pathlib import Path
from generate_recipes import original_files

HERE = Path(__file__).resolve().parent
UI_ROOT = HERE.parent.parent
CODE_EXT = ('.c', '.h', '.cpp', '.hpp', '.cc', '.inc')
MIN_CODE = 25
MIN_PY = 15
RUN_LIMIT = 6
LINES_LIMIT = 40
PY_LINES_LIMIT = 20
SKIP_PREFIX = (b'#include', b'//', b'/*', b'*')
_WS = bytes(range(9, 14)) + b' '

# 既知の誤検出。path -> (許す一致行数, 許す最長の連続, 理由)。しきい値を上げるだけで、丸ごとの除外はしない。
ALLOWLIST = {
    'gensoband/adapter/gb_null_term.c': (LINES_LIMIT - 1, 8, 'enum の case ラベルが原作と同じ並びになる（実装が同じ列挙を処理するため）'),
    'hd2d/ui/game_pad.cpp': (LINES_LIMIT - 1, 8, 'enum の case ラベルが原作と同じ並びになる'),
    'angband/adapter/an_lore_ja.c': (150, 6, '原作の API 呼び出しを使った再実装で、散在する一致が多い'),
    'frox/adapter/fc_shim.c': (60, 6, '原作の API の決まった書き方（m_ptr = &m_list[m_idx] や p_ptr->update |= PU_MON_LITE など）が散在して一致する。続けて一致するのは最長 2 行（2026-10-18、検査用の敵の行動の入口を足して 33 → 42 行）'),
}


def norm(line):
    return line.translate(None, _WS)


def build_index(upstream):
    manifest = json.loads((HERE / 'manifest.json').read_text(encoding='utf8'))
    index = set()
    for source in manifest['sources']:
        for name, data in original_files(upstream, source).items():
            if not name.lower().endswith(CODE_EXT):
                continue
            for line in data.splitlines():
                n = norm(line)
                if len(n) >= MIN_PY and not n.startswith(SKIP_PREFIX):
                    index.add(n)
    return index


def measure(lines, index, minlen):
    """lines: 正規化済みの行列。(一致行数, 最長の連続) を返す。"""
    total = run = best = 0
    for n in lines:
        if len(n) < minlen or n.startswith(SKIP_PREFIX):
            continue
        if n in index:
            total += 1
            run += 1
            best = max(best, run)
        else:
            run = 0
    return total, best


def py_strings(data):
    try:
        tree = ast.parse(data)
    except (SyntaxError, ValueError):
        return []
    out = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, str) and '\n' in node.value:
            out.append(node.value)
    return out


def py_all_strings(data):
    """1 行だけの文字列と bytes も含め、すべての文字列定数。"""
    try:
        tree = ast.parse(data)
    except (SyntaxError, ValueError):
        return []
    out = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, (str, bytes)):
            out.append(node.value if isinstance(node.value, str) else node.value.decode('latin1'))
    return out


def count_targeting_matches(data, index):
    """tools/targeting/*.py は原作の行を 1 行も持たない方針。1 行の文字列も数える。"""
    total = 0
    for s in py_all_strings(data):
        lines = [norm(l.encode('utf8', 'replace')) for l in s.split('\n')]
        total += measure(lines, index, MIN_PY)[0]
    return total


def scan_file(path, data, index):
    """(一致行数, 最長の連続, 上限を超えたか, 種別) を返す。"""
    if path.endswith('.py'):
        total = best = 0
        for s in py_strings(data):
            lines = [norm(l.encode('utf8', 'replace')) for l in s.split('\n')]
            t, b = measure(lines, index, MIN_PY)
            total += t
            best = max(best, b)
        return total, best, PY_LINES_LIMIT, 'py'
    lines = [norm(l) for l in data.splitlines()]
    t, b = measure(lines, index, MIN_CODE)
    return t, b, LINES_LIMIT, 'code'


def tracked_files():
    raw = subprocess.check_output(['git', '-C', str(UI_ROOT), '-c', 'safe.directory=' + UI_ROOT.as_posix(),
                                   'ls-files', '-z'])
    return [p for p in raw.decode('utf8', 'replace').split('\0') if p]


def run(upstream, extra_files=()):
    """問題の一覧（文字列のリスト）を返す。"""
    problems = []
    files = tracked_files()
    for p in files:
        if p.startswith('presentation/') and fnmatch.fnmatch(p, '*.generated.h'):
            problems.append(f'{p}: presentation/ に生成ヘッダが追跡されている')
    index = build_index(upstream)
    targets = [(p, UI_ROOT / p) for p in files
               if not p.startswith('third_party/') and p.lower().endswith(CODE_EXT + ('.py',))]
    targets += [(Path(e).name, Path(e)) for e in extra_files]
    for rel, full in targets:
        if not full.is_file():
            continue
        if rel.startswith('tools/targeting/') and rel.endswith('.py'):
            n = count_targeting_matches(full.read_bytes(), index)
            if n:
                problems.append(f'{rel}: 原作と一致する行が {n} 行ある（tools/targeting は 0 行が方針。行のハッシュで位置を探す）')
        total, best, lim, kind = scan_file(rel, full.read_bytes(), index)
        max_lines, max_run = lim, RUN_LIMIT
        allow = ALLOWLIST.get(rel)
        if allow:
            max_lines, max_run = allow[0] + 1, allow[1] + 1
        if best >= max_run or total >= max_lines:
            problems.append(f'{rel}: 一致 {total} 行 / 最長の連続 {best} 行'
                            f'（上限 {max_lines} 行・連続 {max_run} 行、{kind}）')
    return problems
