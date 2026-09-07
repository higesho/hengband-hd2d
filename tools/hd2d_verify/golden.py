#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""検査モードの**出力を丸ごと基準として保存し、前後で突き合わせる**。

## 何のためのものか

`hd2d/app/hd2d_app.cpp` を分割するとき、**振る舞いが変わっていないこと**を
機械で言えるようにするためのもの。合否の宣言（`RESULT: PASS`）だけを見ると、
「PASS のまま中身が変わった」を取りこぼす。ここでは出力の全行を突き合わせる。

## 使い方

    python tools/hd2d_verify/golden.py --record    # いまの状態を基準として保存
    python tools/hd2d_verify/golden.py --check     # 基準と突き合わせる

基準は `tools/hd2d_verify/golden/<検査名>.txt` に置く。

## 覆い隠しているもの

出力には走らせるたびに変わる字が混じる。そのままだと毎回差分が出るので、
下の `MASKS` で伏せ字にしている。**伏せるのは環境と時間に由来するものだけ**で、
検査が数えた値は 1 つも伏せていない（伏せると回帰を見落とすため）。

## 入れていない検査

- `--vr-check` … **VR の実機が要る**。繋がっていないと
  `XR_ERROR_FORM_FACTOR_UNAVAILABLE` で必ず FAIL になる（環境要因であって
  コードの不具合ではない）。実機で見るときは手で走らせること。
- `--town-view` / `--prefab-view` … 検査ではなく人が見るためのもの。

## この網が**見ていない所**（2026-09-06 に実測した）

**検査は `run_*_check` の中しか通らない。** `run()`（ゲームループ・5,829 行）と
`run_town_view()` / `run_prefab_view()`（人が見るモード）は 1 行も通らない。

確かめ方はこうした——`lamp_night_factor()`（呼び出しは `run_town_view` と `run()` だけ）
の値をわざと変えて組み直しても、**16 件すべてが「一致」と言った**。
いっぽう `distance_for_cell_px()`（5 つの検査が呼ぶ）を 2% 変えると、
**world・terrain・post・cutaway・combat_fx の 5 件が差分を出した**。

つまりこの網は:

- **検査モードを外へ出す作業には十分効く**（動かす対象がまさに検査だから）
- **`run()` を割る作業には足りない**。そちらは実機で遊んで確かめるほかない

## 罠

- **実行体はリポジトリ直下のものを使う**（`Dist/` の写しではない）。
  分割の前後を比べるので、いま組んだものでないと意味がない。
- **`--prefab-check` はパイプで受けない**。
- 検査を足したら `CHECKS` に 1 行足すこと。
"""
from __future__ import annotations

import argparse
import difflib
import os
import re
import subprocess
import sys

# 端末が cp932 でも差分を出せるようにする（出力に落ちない字が混じる）
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding='utf-8', errors='replace')
    except (AttributeError, ValueError):
        pass

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(ROOT, 'HengbandHd2d.exe')
GOLDEN_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'golden')

#: 走らせる検査。(基準の名前, 引数, パイプで受けるか)
CHECKS = [
    ('world',       ['--world-check'],        True),
    ('town',        ['--town-check'],         True),
    ('motion',      ['--motion-check'],       True),
    ('pad',         ['--pad-check'],          True),
    ('terrain',     ['--terrain-check'],      True),
    ('post',        ['--post-check'],         True),
    ('cutaway',     ['--cutaway-check'],      True),
    ('ui',          ['--ui-check'],           True),
    ('edit',        ['--edit-check'],         True),
    ('core_select', ['--core-select-check'],  True),
    ('combat_fx',   ['--combat-fx-check'],    True),
    ('vr_math',     ['--vr-math-check'],      True),
    ('slab_ladder', ['--slab-ladder-check'],  True),
    ('material',    ['--material-check'],     True),
    # プレハブは代表を 3 つ。彫れているか（quads）を見る
    ('prefab_ajito',  ['--prefab-check=ajito_git'],      False),
    ('prefab_barrel', ['--prefab-check=ale_barrels_ang'], False),
]

#: 走らせるたびに変わる字。(正規表現, 置き換える字)
MASKS = [
    # 時間の計測（--post-check の合成のコストなど）
    (re.compile(r'\d+\.\d+\s*ms'), '<時間>ms'),
    (re.compile(r'\d+\.\d+\s*秒'), '<時間>秒'),
    # GPU と driver の申告（機械が変われば変わる）
    (re.compile(r'^\[hd2d\] GL .*$', re.M), '[hd2d] GL <環境>'),
    (re.compile(r'^\[hd2d\] XR_RUNTIME .*$', re.M), '[hd2d] XR_RUNTIME <環境>'),
    # 一時ディレクトリの絶対パス（ユーザー名が入る）
    (re.compile(r'[A-Za-z]:[\/]Users[\/][^\s"]+'), '<一時パス>'),
    (re.compile(r'[A-Za-z]:[\/]Project2[\/][^\s"]+'), '<パス>'),
]


#: 丸ごと落とす行。**回数が走らせるたびに変わる**ので伏せ字では足りない。
DROPS = [
    # NVIDIA のドライバが出す性能の注意（--cutaway-check で回数が変わる）
    re.compile(r'^\[hd2d\]\[gl\] .*$'),
]


def decode(raw: bytes) -> str:
    """実行体の出力を文字にする。**UTF-8 で出している**（cp932 ではない）。"""
    for enc in ('utf-8', 'cp932'):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode('utf-8', errors='replace')


def mask(text: str) -> str:
    lines = [l for l in text.split(chr(10))
             if not any(p.match(l) for p in DROPS)]
    text = chr(10).join(lines)
    for pat, rep in MASKS:
        text = pat.sub(rep, text)
    return text


def run_one(args: list[str], piped: bool, timeout: int = 600) -> str:
    """検査を 1 つ走らせ、伏せ字を当てた出力を返す。"""
    try:
        proc = subprocess.run(
            [EXE] + args,
            cwd=ROOT,
            input=b'' if piped else None,
            stdin=None if piped else subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
        out = decode(proc.stdout)
        out += '\n[終了コード] %d\n' % proc.returncode
    except subprocess.TimeoutExpired:
        out = '[!! 時間切れ %d 秒]\n' % timeout
    return mask(out.replace('\r\n', '\n'))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--record', action='store_true', help='いまの出力を基準として保存する')
    ap.add_argument('--check', action='store_true', help='基準と突き合わせる')
    ap.add_argument('--only', help='この名前の検査だけ走らせる（カンマ区切り）')
    args = ap.parse_args()

    if not args.record and not args.check:
        ap.error('--record か --check のどちらかを指定すること')
    if not os.path.exists(EXE):
        print('!! 実行体が無い: %s' % EXE)
        print('   先に組むこと。')
        return 2

    os.makedirs(GOLDEN_DIR, exist_ok=True)
    targets = CHECKS
    if args.only:
        want = {s.strip() for s in args.only.split(',')}
        targets = [c for c in CHECKS if c[0] in want]
        if not targets:
            print('!! その名前の検査は無い: %s' % args.only)
            return 2

    failed = []
    for name, argv, piped in targets:
        out = run_one(argv, piped)
        path = os.path.join(GOLDEN_DIR, name + '.txt')
        if args.record:
            with open(path, 'wb') as f:
                f.write(out.encode('utf-8'))
            print('  記録 %-14s %5d 行' % (name, out.count('\n')))
            continue

        if not os.path.exists(path):
            print('  !! 基準なし %-14s （先に --record すること）' % name)
            failed.append(name)
            continue
        with open(path, 'rb') as f:
            want_text = f.read().decode('utf-8')
        if want_text == out:
            print('  一致 %-14s %5d 行' % (name, out.count('\n')))
        else:
            diff = list(difflib.unified_diff(
                want_text.splitlines(), out.splitlines(),
                fromfile='基準', tofile='いま', lineterm='', n=1))
            print('  !! 差分 %-12s %d 行ちがう' % (name, sum(
                1 for d in diff if d[:1] in '+-' and d[:3] not in ('+++', '---'))))
            #: **終了コードは差分に埋もれても必ず見せる。**差分は先頭 40 行で
            #: 切り詰めるので、末尾にある `[終了コード]` が落ちる。落ちると
            #: 「途中で止まったのか、値が違うだけなのか」が読めない——2026-09-07 に
            #: `--edit-check` が一過性で途中終了したとき、まさにそれで原因を追えなかった。
            def _code(text):
                for l in text.splitlines():
                    if l.startswith('[終了コード]'):
                        return l
                return '（終了コードの行が無い）'
            want_code = _code(want_text)
            now_code = _code(out)
            if want_code != now_code:
                print('      ** 終了コード 基準=%s / いま=%s **'
                      % (want_code.replace('[終了コード]', '').strip(),
                         now_code.replace('[終了コード]', '').strip()))
            print('      基準 %d 行 / いま %d 行' % (want_text.count(chr(10)), out.count(chr(10))))
            for d in diff[:40]:
                print('      ' + d)
            failed.append(name)

    print()
    if args.record:
        print('基準を %s に保存した。' % os.path.relpath(GOLDEN_DIR, ROOT))
        return 0
    if failed:
        print('!! 一致しなかった検査: %s' % ', '.join(failed))
        print('   意図した変更ならば --record で基準を取り直すこと。')
        return 1
    print('全部一致した（%d 件）。振る舞いは変わっていない。' % len(targets))
    return 0


if __name__ == '__main__':
    sys.exit(main())
