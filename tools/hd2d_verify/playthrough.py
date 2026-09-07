#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""実際に**遊ばせて**、通し 1 回ぶんの振る舞いを基準として突き合わせる（案 A）。

## 何のためのものか

`tools/hd2d_verify/golden.py` は検査モード（`--*-check`）の出力を見るが、
**`run()`（ゲームループ・5,827 行）を 1 行も通らない**。遊ぶ経路はそこにある。

ここでは実際に起動して、起動 → 握手 → セーブの読み込み → 描画 → 終了まで
歩かせ、そのあいだに観測できるものを丸ごと基準にする。

  1. 絵が出ているか（`--shot=` の BMP。**色は突き合わせない**。下記）
  2. コアとの会話（`--protocol-log=` の JSON Lines。時刻は伏せる）
  3. 画面が stderr に出した `[hd2d]` の行

## 絵の色は突き合わせない（2026-09-07 に測り直した）

**この検査は絵について決定的になり得ない。**コアが別プロセスで実時間で走るので、
撮る周までに届いているフレームの数が毎回違う（`image_health()` に詳しく書いた）。
`HD2D_FIXED_CLOCK=1` で画面側の時計を止めても消えないことを実測で確かめてある。

だからここは「世界が出たか・真っ暗でないか」だけを見る。
**絵の正確さは `replay.py` が持つ**——あちらは同じ `run()` に固定の入力
（記録した会話＋決め打ちの時計）を流すので、SHA-256 が完全に一致する。

## 決定的な部分（2026-09-07 に実測）

- `hd2d/` に乱数は 1 つも無い（`rand` / `mt19937` / `random_device` が 0 件）
- 会話ログは**時刻を伏せれば 3 回とも完全一致**した
- **`--shot` で終わる経路は cfg を書き換えない**（設定を書くのは環の中の 2 か所で、
  `--shot` はその手前で `break` する）ので、cfg の揺らぎは起きない

**乱数が無いことは決定的であることの証明にならない**——実時計が入力に入っている
以上、同じ操作でも入力そのものが毎回違う。ここを取り違えていた。

## 使い方

    python tools/hd2d_verify/playthrough.py --record   # 基準を採る
    python tools/hd2d_verify/playthrough.py --check    # 突き合わせる
    python tools/hd2d_verify/playthrough.py --check --only title

## セーブの扱い（**ここが一番危ない**）

どのシナリオも `lib/save/` を空にしてから走らせ、**終わったら必ず戻す**
（失敗しても・中断されても戻す）。利用者の遊びの記録を巻き添えにしないため。

固定セーブを読ませるシナリオは**まだ無い**。`HENGBAND_SDL2_INJECT_KEYS` で
タイトルからロードさせる道を試したが、キー列が題名画面に噛み合わず 300 秒
待っても地形が描かれなかった（フレームは 29 通届いていた）。コア側の話なので
別途調べる。いまは代わりに `pad` が**画面側の入力経路**を通している。

## この網が見ていない所

実時間（`SDL_Delay`・垂直同期）、実機の入力装置、音の聴感、VR 実機、
窓の操作、Android の分岐、そして**シナリオが歩かなかった場面**。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
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
ROOT = os.path.dirname(os.path.dirname(HERE))
EXE = os.path.join(ROOT, 'HengbandHd2d.exe')
CORE = os.path.join(ROOT, 'HengbandCore.exe')
SAVE_DIR = os.path.join(ROOT, 'lib', 'save')
USER_DIR = os.path.join(ROOT, 'lib', 'user')
FIXTURES = os.path.join(ROOT, 'tools', 'diff_test', 'fixtures')
GOLDEN_DIR = os.path.join(HERE, 'playthrough')

#: 走らせるシナリオ。
#:   keys       … コアへ流すキー列（`HENGBAND_SDL2_INJECT_KEYS`）。空なら流さない
#:   save       … 置く固定セーブ。None なら `lib/save/` は空にする
#:   shot_after … 絵を撮るまで待つ数。負なら環の回数、正なら地形の出たフレーム数
SCENARIOS = [
    {
        'name': 'title',
        'note': 'タイトル画面まで。起動・握手・環・描画・後片付けを一通り歩く。',
        'keys': '',
        'env': {},
        'save': None,
        'shot_after': -120,
    },
    {
        'name': 'load',
        'note': '固定セーブ DBG を読み込み、**世界を描く**ところまで（地形・光・影）。',
        #: タイトルの実物は `> N) New Game / L) Load / Q) Quit` の縦並びで、
        #: `2`（下）→ Enter で Load。次の「Load Game」も `> a) DBG` が既に選ばれた
        #: 縦並びなので **Enter で決める**。
        #: **旧 `tools/diff_test/scenarios/` の `a` は効かない**——あちらは消えた
        #: SDL2 UI 向けで、いまの画面は字を打つのではなくカーソルで選ぶ。
        'keys': r'\.\.\.\.2\r\.\.\r\.\.\.\.\.\.\.\.\.\.',
        #: **時刻を夕方へ固定する。**`lamp_night_factor()` は 17:00〜18:30 の傾きで
        #: 灯りを点けるので、そこを通らないと光の計算を 1 度も試せない。固定しないと
        #: 走らせた時刻しだいで「通ったり通らなかったり」する検査になる。
        'env': {'HD2D_FORCE_TIME': '17:30'},
        'save': 'DBG',
        #: 正の値は**地形を描いた回数**で数える。ターン制なので状態が変わったときしか
        #: フレームが来ない——大きくしすぎると届かない（60 にして 300 秒待った）。
        'shot_after': 10,
    },
    {
        'name': 'pad',
        'note': '**画面側の入力経路**（route_pad_direction / route_pad_press）を通す。',
        'keys': '',
        #: `HD2D_PAD_PRESS` は**実機と同じ `route_pad_press` を通る**（別の道を作って
        #: いない）。コアへのキー注入（`HENGBAND_SDL2_INJECT_KEYS`）はコア側の Term に
        #: 直に入るので、**画面側の入力の道は 1 行も通らない**。ここだけが通せる。
        'env': {
            'HD2D_PAD_PRESS': 'Right,Down,Left,Up',
            'HD2D_PAD_PRESS_AT': '60',
            'HD2D_PAD_PRESS_STEP': '15',
        },
        'save': None,
        'shot_after': -200,
    },
]

#: 走らせるたびに変わる字。伏せてから突き合わせる。
MASKS = [
    (re.compile(r'"time"\s*:\s*"[^"]*"'), '"time":"<時刻>"'),
    (re.compile(r'\d+\.\d+\s*ms'), '<時間>ms'),
    (re.compile(r'\d+\.\d+\s*秒'), '<時間>秒'),
    (re.compile(r'^\[hd2d\] GL .*$', re.M), '[hd2d] GL <環境>'),
    (re.compile(r'^\[hd2d\] XR_RUNTIME .*$', re.M), '[hd2d] XR_RUNTIME <環境>'),
    (re.compile(r'\(pid \d+\)'), '(pid <番号>)'),
    #: **バックスラッシュを含めて**採る。除くと `C:\Users\<ユーザー名>` で止まり、
    #: 続きの `\AppData\Local\Temp\hd2d_play_<でたらめ>\shot.bmp` が残って毎回ちがう。
    (re.compile(r'[A-Za-z]:[\\/][^\s"]*'), '<パス>'),
    #: **コアが「画面が消えた」と気づいた場所**は、そのときコアが何をしていたかで
    #: 変わる（`pump_input` / `before_capture` を実測）。`--shot` は絵を撮ったら
    #: すぐ終わるので、コアから見れば画面が突然消える。画面側の話ではない。
    (re.compile(r'the ui is gone \([a-z_]+\)'), 'the ui is gone (<場所>)'),
    (re.compile(r'読み込み \d+ ms'), '読み込み <時間> ms'),
    (re.compile(r'GPU 転送 \d+ ms'), 'GPU 転送 <時間> ms'),
]

#: 会話の中身をそのまま残す上限（バイト）。これを超えたらハッシュにする。
kPayloadInline = 200

#: 丸ごと落とす行（回数が走らせるたびに変わる）。
DROPS = [
    re.compile(r'^\[hd2d\]\[gl\] .*$'),
]


def mask(text: str) -> str:
    lines = [l for l in text.split(chr(10)) if not any(p.match(l) for p in DROPS)]
    text = chr(10).join(lines)
    for pat, rep in MASKS:
        text = pat.sub(rep, text)
    return text


def image_health(bmp: bytes) -> list:
    """絵が**出ているか**だけを見る。色の一致は見ない。

    ## なぜ色を突き合わせないのか（2026-09-07 に測り直した）

    **この検査は絵について決定的になり得ない。**コアは別プロセスで実時間で走り、
    受信は別スレッドが inbox へ積む（`hd2d/net/core_link.cpp` の `receive_loop`）。
    画面は待たずに最新を取るだけなので、**撮る周までにコアのフレームが何通
    届いているか**が走るたびに違う。撮る瞬間は `frames_with_terrain`
    （ループの周回数。`hd2d/app/hd2d_app.cpp`）で決まるので、周の数が同じでも
    そのとき描いている世界の進み具合が揃わない。

    実測で裏を取った——`HD2D_FIXED_CLOCK=1` を渡して**画面側の時計を止めても
    3 回のうち 1 回はずれた**（止めない 3 回も 1 回は一致した）。つまり画面側の
    時間項（埃・風・警戒リング・部屋の混ぜ）を止めても消えない。

    以前ここには「GPU の丸めの揺れ」と書いてあったが**誤診**だった。`replay.py`
    は同じ描画経路に固定の入力を流して SHA-256 が完全一致する。GPU は決定的で、
    揺れていたのは入力のほうである。

    ## だからここは健全性だけ見る

    「世界が出たか」「真っ暗でないか」——生きたコアと画面が噛み合ったことを言う。
    **絵の正確さは `replay.py` が持つ**（同じ `run()` に固定の入力を流すので、
    シェーダ・なめらか移動・風・光の変更は SHA-256 に出る）。
    """
    import struct
    if len(bmp) < 54 or bmp[:2] != b'BM':
        return ['（BMP として読めない）']
    off = struct.unpack_from('<I', bmp, 10)[0]
    w = struct.unpack_from('<i', bmp, 18)[0]
    h_raw = struct.unpack_from('<i', bmp, 22)[0]
    h = abs(h_raw)
    bpp = struct.unpack_from('<H', bmp, 28)[0] // 8
    if w <= 0 or h <= 0 or bpp < 3:
        return ['（大きさが読めない: %dx%d %d バイト/画素）' % (w, h, bpp)]
    stride = ((w * bpp) + 3) // 4 * 4
    #: **粗く間引いて数える。**1 画素ずつ見る必要は無い（見たいのは
    #: 「真っ暗か」「一色か」だけ）。8 画素ごとなら 92 万画素が 1.4 万画素で済む。
    step = 8
    dark = 0
    seen = 0
    lo = 255
    hi = 0
    for y in range(0, h, step):
        base = off + (y * stride)
        for x in range(0, w, step):
            q = base + (x * bpp)
            v = (bmp[q] + bmp[q + 1] + bmp[q + 2]) // 3
            seen += 1
            if v < 8:
                dark += 1
            lo = min(lo, v)
            hi = max(hi, v)
    if seen == 0:
        return ['（画素を 1 つも読めなかった）']
    #: **段階で言う。**実数のままだと 1 段の違いで字が変わり、突き合わせが落ちる。
    dark_pct = (dark * 100) // seen
    return [
        '大きさ %dx%d' % (w, h),
        '真っ暗な画素 %s' % ('ほぼ全面' if dark_pct >= 95
                             else ('多い' if dark_pct >= 50 else 'わずか')),
        '明暗の幅 %s' % ('無い（一色）' if (hi - lo) < 8 else 'ある'),
    ]


def decode(raw: bytes) -> str:
    for enc in ('utf-8', 'cp932'):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode('utf-8', errors='replace')


class CfgGuard:
    """リポジトリ直下の `*.cfg` を退避して、**必ず**戻す。

    ## なぜ要るのか（2026-09-07 に踏んだ）

    `--shot` で終わる経路は設定を書かない——と思っていたが、**環の中には設定が
    変わったときに書く道がある**（`run()` の (d'')）。網を回している最中に、
    生きている窓へ**物理的なマウスのホイールが届く**と、そこで倍率が変わって
    cfg に書かれる。

    実際に `camera_cell_px` が 71.1328 → 60.9947（ホイール下 2 刻みぶん）へ動き、
    網が赤くなった。**コードは 1 行も変わっていないのに赤い**ので、原因を探すのに
    時間を取られる。しかも**利用者の設定を勝手に書き換えている**ほうが害が大きい。

    セーブと同じく、走らせる前に退避して、終わったら必ず戻す。
    """

    def __enter__(self):
        self.saved = {}
        for name in os.listdir(ROOT):
            if not name.endswith('.cfg'):
                continue
            path = os.path.join(ROOT, name)
            if os.path.isfile(path):
                with open(path, 'rb') as f:
                    self.saved[name] = f.read()
        return self

    def __exit__(self, *exc):
        for name, data in self.saved.items():
            path = os.path.join(ROOT, name)
            try:
                with open(path, 'rb') as f:
                    if f.read() == data:
                        continue  #: 変わっていない。触らない
            except OSError:
                pass
            with open(path, 'wb') as f:
                f.write(data)
        return False


class SaveGuard:
    """`lib/save/` を退避して、**必ず**戻す。"""

    def __init__(self, fixture: str | None):
        self.fixture = fixture
        self.stash = None

    def __enter__(self):
        self.stash = tempfile.mkdtemp(prefix='hd2d_save_')
        if os.path.isdir(SAVE_DIR):
            for name in os.listdir(SAVE_DIR):
                src = os.path.join(SAVE_DIR, name)
                if os.path.isfile(src):
                    shutil.move(src, os.path.join(self.stash, name))
        else:
            os.makedirs(SAVE_DIR, exist_ok=True)
        if self.fixture:
            src = os.path.join(FIXTURES, self.fixture)
            if not os.path.exists(src):
                raise SystemExit('固定セーブが無い: %s' % src)
            shutil.copy2(src, os.path.join(SAVE_DIR, self.fixture))
            panels = src + '.sdl2panels'
            if os.path.exists(panels):
                shutil.copy2(panels, os.path.join(SAVE_DIR, self.fixture + '.sdl2panels'))
        return self

    def __exit__(self, *exc):
        # 走らせたぶんを捨てて、退避したものを戻す
        if os.path.isdir(SAVE_DIR):
            for name in os.listdir(SAVE_DIR):
                p = os.path.join(SAVE_DIR, name)
                if os.path.isfile(p):
                    os.remove(p)
        for name in os.listdir(self.stash):
            shutil.move(os.path.join(self.stash, name), os.path.join(SAVE_DIR, name))
        os.rmdir(self.stash)
        return False


def kill_leftovers() -> None:
    for name in ('HengbandHd2d.exe', 'HengbandCore.exe'):
        subprocess.run(['taskkill', '/F', '/IM', name],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def run_one(sc: dict, timeout: int = 300) -> str:
    """シナリオを 1 つ走らせ、観測したものを 1 つの字にまとめて返す。"""
    tmp = tempfile.mkdtemp(prefix='hd2d_play_')
    shot = os.path.join(tmp, 'shot.bmp')
    plog = os.path.join(tmp, 'protocol.jsonl')
    env = dict(os.environ)
    #: 前の回の残りが効かないよう、こちらが使う環境変数は必ず消してから入れ直す。
    for k in ('HENGBAND_SDL2_INJECT_KEYS', 'HD2D_PAD_PRESS',
              'HD2D_PAD_PRESS_AT', 'HD2D_PAD_PRESS_STEP', 'HD2D_PAD_MODS'):
        env.pop(k, None)
    if sc['keys']:
        env['HENGBAND_SDL2_INJECT_KEYS'] = sc['keys']
    for k, v in sc.get('env', {}).items():
        env[k] = v
    argv = [EXE, '--core-path=' + CORE, '--windowed=1280x720',
            '--shot=' + shot, '--shot-after=%d' % sc['shot_after'],
            '--protocol-log=' + plog]

    out = []
    try:
        with CfgGuard(), SaveGuard(sc['save']):
            #: **時間切れでも知らせは捨てない。**捨てると「なぜ止まったか」が
            #: 一切分からなくなる（実際に 2 回それで調べ直した）。
            proc = subprocess.Popen(argv, cwd=ROOT, env=env,
                                    stdin=subprocess.DEVNULL,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try:
                raw, _ = proc.communicate(timeout=timeout)
                code = proc.returncode
            except subprocess.TimeoutExpired:
                kill_leftovers()
                try:
                    raw, _ = proc.communicate(timeout=20)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    raw = b''
                code = -9
                out.append('[!! 時間切れ %d 秒]' % timeout)
            stderr_text = decode(raw or b'')
        kill_leftovers()

        out.append('[終了コード] %d' % code)
        # (1) 絵
        if os.path.exists(shot):
            with open(shot, 'rb') as f:
                data = f.read()
            out.append('[絵] %d バイト' % len(data))
            out.append('[絵が出ているか]')
            for line in image_health(data):
                out.append('  ' + line)
        else:
            out.append('[絵] 撮れなかった')
        # (2) コアとの会話（時刻を伏せて 1 行ずつ）
        out.append('[会話]')
        if os.path.exists(plog):
            with open(plog, 'rb') as f:
                for line in decode(f.read()).splitlines():
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        d = json.loads(line)
                    except ValueError:
                        out.append('  ' + mask(line))
                        continue
                    d.pop('time', None)
                    #: **長い中身はハッシュにする。**地形の `cells` は 1 通 50KB あり、
                    #: そのまま残すと基準が 1.8MB になって扱えない。ハッシュなら
                    #: 1 バイト変わっても差が出るので、見張る力は落ちない。
                    payload = d.get('payload')
                    #: **コアから来たもの（`in`）は中身を見ない。**コアは `realtime` を
                    #: 名乗っており（`hello_ack.features`）実時間で世界を進めるので、
                    #: 同じ操作でもフレームの中身がたまに変わる（22 通中 1 通だけ違う、
                    #: という揺れを実測した）。ここで試したいのは**画面側**なので、
                    #: コアの中身まで縛ると揺れるだけで得るものが無い。
                    #: 種別と長さは残す——通の並びが変わればそれは捕まえたい。
                    if d.get('dir') == 'in':
                        #: **長さも見ない。**コアは実時間で動くので、同じ操作でも
                        #: フレームの中身が 1 バイト前後ずれる（101241 と 101242 を実測）。
                        #: 種別と並びだけ見る。中身まで縛りたいときは `replay.py`（案 B）で
                        #: コアを止めて回すこと——あちらは絵まで完全一致する。
                        if isinstance(payload, str) and len(payload) > kPayloadInline:
                            d['payload'] = '<長い中身>'
                            d.pop('len', None)
                    elif isinstance(payload, str) and len(payload) > kPayloadInline:
                        #: **画面が送るもの（`out`）は中身まで見る。**ここが試したい当のもの。
                        d['payload'] = ('<%d バイト sha256=%s>'
                                        % (len(payload),
                                           hashlib.sha256(payload.encode('utf-8')).hexdigest()[:32]))
                    out.append('  ' + mask(json.dumps(d, ensure_ascii=False, sort_keys=True)))
        else:
            out.append('  （残っていない）')
        # (3) 画面が出した知らせ
        out.append('[知らせ]')
        for line in mask(stderr_text.replace(chr(13) + chr(10), chr(10))).splitlines():
            if line.startswith('[hd2d]') or line.startswith('[core]') or line.startswith('[sdl'):
                out.append('  ' + line)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return chr(10).join(out) + chr(10)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--record', action='store_true', help='いまの振る舞いを基準として保存する')
    ap.add_argument('--check', action='store_true', help='基準と突き合わせる')
    ap.add_argument('--only', help='この名前のシナリオだけ（カンマ区切り）')
    args = ap.parse_args()
    if not args.record and not args.check:
        ap.error('--record か --check のどちらかを指定すること')
    for path, what in ((EXE, '画面'), (CORE, 'コア')):
        if not os.path.exists(path):
            print('!! %sの実行体が無い: %s' % (what, path))
            print('   先に組むこと（docs/BUILD_ENV.md §1）。')
            return 2

    os.makedirs(GOLDEN_DIR, exist_ok=True)
    targets = SCENARIOS
    if args.only:
        want = {s.strip() for s in args.only.split(',')}
        targets = [s for s in SCENARIOS if s['name'] in want]
        if not targets:
            print('!! その名前のシナリオは無い: %s' % args.only)
            return 2

    failed = []
    for sc in targets:
        text = run_one(sc)
        path = os.path.join(GOLDEN_DIR, sc['name'] + '.txt')
        if args.record:
            with open(path, 'wb') as f:
                f.write(text.encode('utf-8'))
            print('  記録 %-10s %4d 行  %s' % (sc['name'], text.count(chr(10)), sc['note']))
            continue
        if not os.path.exists(path):
            print('  !! 基準なし %-10s （先に --record すること）' % sc['name'])
            failed.append(sc['name'])
            continue
        with open(path, 'rb') as f:
            want_text = f.read().decode('utf-8')
        if want_text == text:
            print('  一致 %-10s %4d 行' % (sc['name'], text.count(chr(10))))
        else:
            import difflib
            diff = list(difflib.unified_diff(want_text.splitlines(), text.splitlines(),
                                             fromfile='基準', tofile='いま', lineterm='', n=1))
            n = sum(1 for d in diff if d[:1] in '+-' and d[:3] not in ('+++', '---'))
            print('  !! 差分 %-8s %d 行ちがう' % (sc['name'], n))
            for d in diff[:30]:
                print('      ' + d)
            failed.append(sc['name'])

    print()
    if args.record:
        print('基準を %s に保存した。' % os.path.relpath(GOLDEN_DIR, ROOT))
        return 0
    if failed:
        print('!! 一致しなかったシナリオ: %s' % ', '.join(failed))
        print('   意図した変更ならば --record で基準を取り直すこと。')
        return 1
    print('全部一致した（%d 件）。遊ぶ経路の振る舞いは変わっていない。' % len(targets))
    return 0


if __name__ == '__main__':
    sys.exit(main())
