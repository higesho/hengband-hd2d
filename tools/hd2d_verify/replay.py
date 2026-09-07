#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""**コアを止めて**ゲームループを回し、1 ビットも違わないことを見る（案 B）。

## `playthrough.py`（案 A）との違い

案 A は本物のコアを起こす。コアは `realtime` を名乗って実時間で世界を進めるので、
どの周までに何通届くかが走らせるたびに変わり、**絵が 92 万画素中 13〜28 画素だけ
揺れる**（各成分 1 段）。だから案 A は升の平均を指紋にしていて、升より細かい変化は
見えない。

ここではコアを起こさない。**採っておいた会話を `CoreLink` が 1 周に 1 通ずつ配る**
（`HD2D_REPLAY_LOG`）。時計も決め打ちにする（`HD2D_FIXED_CLOCK`）。すると

  - 別スレッドが無い（配るのは主スレッド）
  - 届く順が走らせるたびに変わらない
  - 時刻が周の番号だけで決まる

ので、**絵が 3 回とも完全に一致する**（実測）。升の平均ではなく SHA-256 で見られる。

## 使い方

    # 会話を採り直す（シナリオを変えたときだけ。本物のコアを起こす）
    python tools/hd2d_verify/replay.py --record-core

    # 採ってある会話を再生して基準を作る
    python tools/hd2d_verify/replay.py --record

    # 突き合わせる（ふだんはこれ）
    python tools/hd2d_verify/replay.py --check

会話は `tools/hd2d_verify/replay/<名前>.jsonl.gz` に置く（3.4 MB → 261 KB）。

## 何を捕まえないか

- **本物のコアとのやり取り**——記録を配るだけなので、画面が送ったものに対して
  コアがどう応じるかは見ていない。そこは案 A の担当
- **記録が通らなかった場面**——採った 1 回ぶんの道しか歩かない
- 実時間・実機の入力装置・音・VR・窓の操作・Android の分岐

## **絵の正確さはここが持つ**（2026-09-07 に決めた）

同じ `run()` に固定の入力（記録した会話＋決め打ちの時計）を流すので、
**絵の SHA-256 が完全に一致する**。シェーダ・なめらか移動・風・光を変えれば
必ずここに出る。

`playthrough.py` は絵の色を突き合わせない——あちらはコアが実時間で走るため、
撮る周までに届いているフレームの数が毎回違い、**絵について決定的になり得ない**
（画面側の時計を止めても消えないことを実測で確かめた）。あちらは
「世界が出たか・真っ暗でないか」だけを見る。

## 罠

- **再生と本物では配られ方が違う。**本物は 1 周に何通も届くことがあるが、ここは
  1 周 1 通である。**本物の再現ではなく、再生どうしの一致**を見る道具である
- 会話を採り直したら基準も採り直すこと（`--record-core` のあと `--record`）
"""
from __future__ import annotations

import argparse
import difflib
import gzip
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding='utf-8', errors='replace')
    except (AttributeError, ValueError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from playthrough import (  # noqa: E402
    ROOT, EXE, CORE, CfgGuard, SaveGuard, kill_leftovers, decode, mask, SCENARIOS)

REPLAY_DIR = os.path.join(HERE, 'replay')

#: 再生で絵を撮る周。記録を配り終える前に撮る必要がある。
#: 記録は 30〜44 通。1 周 1 通なので、配り終えるのはその周。そこから
#: `kReplayGraceLoops`（120 周）ぶん回るので、**配り終えた後**の落ち着いた絵を撮る。
SHOT_AT = {
    'title': -100,
    'load': -120,
    'pad': -110,
}


def conversation_path(name: str) -> str:
    return os.path.join(REPLAY_DIR, name + '.jsonl.gz')


def record_core(sc: dict) -> None:
    """本物のコアを起こして会話を採る。"""
    tmp = tempfile.mkdtemp(prefix='hd2d_rec_')
    plog = os.path.join(tmp, 'p.jsonl')
    env = dict(os.environ)
    for k in ('HD2D_REPLAY_LOG', 'HD2D_FIXED_CLOCK', 'HENGBAND_SDL2_INJECT_KEYS',
              'HD2D_PAD_PRESS', 'HD2D_PAD_PRESS_AT', 'HD2D_PAD_PRESS_STEP'):
        env.pop(k, None)
    if sc['keys']:
        env['HENGBAND_SDL2_INJECT_KEYS'] = sc['keys']
    env.update(sc.get('env', {}))
    try:
        with CfgGuard(), SaveGuard(sc['save']):
            p = subprocess.Popen(
                [EXE, '--core-path=' + CORE, '--windowed=1280x720',
                 '--shot=' + os.path.join(tmp, 's.bmp'),
                 '--shot-after=%d' % sc['shot_after'],
                 '--protocol-log=' + plog],
                cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                p.communicate(timeout=300)
            except subprocess.TimeoutExpired:
                kill_leftovers()
                p.communicate(timeout=20)
        kill_leftovers()
        os.makedirs(REPLAY_DIR, exist_ok=True)
        with open(plog, 'rb') as src:
            raw = src.read()
        with gzip.open(conversation_path(sc['name']), 'wb', compresslevel=9) as dst:
            dst.write(raw)
        print('  採った %-8s %5d 行 / 生 %.1f MB → %.0f KB'
              % (sc['name'], raw.count(b'\n'), len(raw) / 1048576.0,
                 os.path.getsize(conversation_path(sc['name'])) / 1024.0))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def run_replay(sc: dict, timeout: int = 180) -> str:
    """採ってある会話を再生し、観測したものを 1 つの字にまとめて返す。"""
    conv = conversation_path(sc['name'])
    if not os.path.exists(conv):
        return '[!! 会話の記録が無い] %s%s' % (os.path.relpath(conv, ROOT), chr(10))
    tmp = tempfile.mkdtemp(prefix='hd2d_rp_')
    shot = os.path.join(tmp, 's.bmp')
    plain = os.path.join(tmp, 'conv.jsonl')
    out_log = os.path.join(tmp, 'out.jsonl')
    out = []
    try:
        with gzip.open(conv, 'rb') as src, open(plain, 'wb') as dst:
            shutil.copyfileobj(src, dst)
        env = dict(os.environ)
        for k in ('HENGBAND_SDL2_INJECT_KEYS', 'HD2D_PAD_PRESS',
                  'HD2D_PAD_PRESS_AT', 'HD2D_PAD_PRESS_STEP'):
            env.pop(k, None)
        env['HD2D_REPLAY_LOG'] = plain
        env['HD2D_FIXED_CLOCK'] = '1'
        env.update(sc.get('env', {}))
        #: `lib/save/` は触らないが、念のため空にして走らせる（再生はセーブを読まない）。
        with CfgGuard(), SaveGuard(None):
            p = subprocess.Popen(
                [EXE, '--core-path=' + CORE, '--windowed=1280x720',
                 '--shot=' + shot, '--shot-after=%d' % SHOT_AT[sc['name']],
                 '--protocol-log=' + out_log],
                cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try:
                raw, _ = p.communicate(timeout=timeout)
                code = p.returncode
            except subprocess.TimeoutExpired:
                kill_leftovers()
                try:
                    raw, _ = p.communicate(timeout=20)
                except subprocess.TimeoutExpired:
                    p.kill()
                    raw = b''
                code = -9
                out.append('[!! 時間切れ %d 秒]' % timeout)
        kill_leftovers()

        out.append('[終了コード] %d' % code)
        #: **絵は SHA-256 で見る。**再生は完全に決まるので、升の平均に落とす必要が無い。
        if os.path.exists(shot):
            with open(shot, 'rb') as f:
                data = f.read()
            out.append('[絵] %d バイト sha256=%s'
                       % (len(data), hashlib.sha256(data).hexdigest()))
        else:
            out.append('[絵] 撮れなかった')
        #: **画面が送ったものだけ**を見る（`out`）。配ったぶん（`in`）は記録そのものなので
        #: 見ても意味が無い。
        out.append('[画面が送ったもの]')
        if os.path.exists(out_log):
            import json
            for line in decode(open(out_log, 'rb').read()).splitlines():
                line = line.strip()
                if not line:
                    continue
                try:
                    d = json.loads(line)
                except ValueError:
                    continue
                if d.get('dir') != 'out':
                    continue
                d.pop('time', None)
                payload = d.get('payload')
                if isinstance(payload, str) and len(payload) > 200:
                    d['payload'] = ('<%d バイト sha256=%s>'
                                    % (len(payload),
                                       hashlib.sha256(payload.encode('utf-8')).hexdigest()[:32]))
                out.append('  ' + mask(json.dumps(d, ensure_ascii=False, sort_keys=True)))
        out.append('[知らせ]')
        for line in mask(decode(raw or b'').replace(chr(13) + chr(10), chr(10))).splitlines():
            if line.startswith('[hd2d]') or line.startswith('[core]') or line.startswith('[sdl'):
                out.append('  ' + line)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return chr(10).join(out) + chr(10)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--record-core', action='store_true',
                    help='本物のコアを起こして会話を採り直す（シナリオを変えたときだけ）')
    ap.add_argument('--record', action='store_true', help='再生して基準を作る')
    ap.add_argument('--check', action='store_true', help='再生して基準と突き合わせる')
    ap.add_argument('--only', help='この名前のシナリオだけ（カンマ区切り）')
    args = ap.parse_args()
    if not (args.record_core or args.record or args.check):
        ap.error('--record-core / --record / --check のどれかを指定すること')
    if not os.path.exists(EXE):
        print('!! 画面の実行体が無い: %s' % EXE)
        return 2

    targets = [s for s in SCENARIOS if s['name'] in SHOT_AT]
    if args.only:
        want = {s.strip() for s in args.only.split(',')}
        targets = [s for s in targets if s['name'] in want]
        if not targets:
            print('!! その名前のシナリオは無い: %s' % args.only)
            return 2

    if args.record_core:
        os.makedirs(REPLAY_DIR, exist_ok=True)
        for sc in targets:
            record_core(sc)
        print()
        print('会話を %s に置いた。続けて --record で基準を作ること。'
              % os.path.relpath(REPLAY_DIR, ROOT))
        return 0

    os.makedirs(REPLAY_DIR, exist_ok=True)
    failed = []
    for sc in targets:
        text = run_replay(sc)
        path = os.path.join(REPLAY_DIR, sc['name'] + '.txt')
        if args.record:
            with open(path, 'wb') as f:
                f.write(text.encode('utf-8'))
            print('  記録 %-8s %4d 行' % (sc['name'], text.count(chr(10))))
            continue
        if not os.path.exists(path):
            print('  !! 基準なし %-8s （先に --record すること）' % sc['name'])
            failed.append(sc['name'])
            continue
        with open(path, 'rb') as f:
            want_text = f.read().decode('utf-8')
        if want_text == text:
            print('  一致 %-8s %4d 行' % (sc['name'], text.count(chr(10))))
        else:
            diff = list(difflib.unified_diff(want_text.splitlines(), text.splitlines(),
                                             fromfile='基準', tofile='いま', lineterm='', n=1))
            n = sum(1 for d in diff if d[:1] in '+-' and d[:3] not in ('+++', '---'))
            print('  !! 差分 %-6s %d 行ちがう' % (sc['name'], n))
            for d in diff[:30]:
                print('      ' + d)
            failed.append(sc['name'])

    print()
    if args.record:
        print('基準を %s に保存した。' % os.path.relpath(REPLAY_DIR, ROOT))
        return 0
    if failed:
        print('!! 一致しなかったシナリオ: %s' % ', '.join(failed))
        print('   意図した変更ならば --record で基準を取り直すこと。')
        return 1
    print('全部一致した（%d 件）。ゲームループの振る舞いは 1 ビットも変わっていない。'
          % len(targets))
    return 0


if __name__ == '__main__':
    sys.exit(main())
