"""実行中の HD2D のテスト入力ファイルに追記し、SDL キューへの登録応答を待つ。

キー・文字・ドロップに加えて、画面の状態の問い合わせ（``--query-ui``）と、部品を名前で押す入口（``--tap``）が使える
（``・`` §2）。
"""
from __future__ import annotations
import argparse
import json
import os
import sys
from pathlib import Path
import time
import uuid


for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, 'reconfigure'):
        stream.reconfigure(encoding='utf-8', errors='replace')


#: 画面が返す応答の状態。``queued`` は SDL キューへ登録した（キー・タップ）、``ok`` は問い合わせに答えた。
OK_STATUSES = ('queued', 'ok')


class PeerExited(ConnectionError):
    """応答を待つ間に、画面のプロセスが終了した（``alive`` が偽を返した）。上限の時間を待たずに知らせる。"""


def send_request(path: Path, request: dict, timeout: float = 10, alive=None) -> dict:
    """1 行を追記し、同じ ``id`` の応答（``<入力ファイル>.ack.jsonl`` の 1 行）を返す。

    問い合わせの応答は 1 行が大きい（描いた字の一覧）ので、応答ファイルは**自分が追記した位置から**だけ読む。

    ``alive`` ＝ 画面のプロセスが生きているかを返す関数（省略可）。応答が来ないまま偽になったら、上限の時間を待たず
    ``PeerExited`` を投げる（コアが落ちて画面が終わったときに、検査が上限まで待ち続けないため）。
    """
    request = dict(request, id=uuid.uuid4().hex)
    raw = (json.dumps(request, ensure_ascii=False) + '\n').encode('utf-8')
    if len(raw) > 4096:
        raise ValueError('入力は UTF-8 で 4096 バイト以内にしてください')
    ack_path = Path(str(path) + '.ack.jsonl')
    try:
        pos = ack_path.stat().st_size
    except FileNotFoundError:
        pos = 0
    # 存在しないファイルを作らない。起動引数の指定間違いを検出する。
    fd = os.open(path, os.O_WRONLY | os.O_APPEND | getattr(os, 'O_BINARY', 0))
    try:
        if os.write(fd, raw) != len(raw):
            raise OSError('入力の追記が完了しませんでした。自動で再送しないでください')
    finally:
        os.close(fd)
    deadline = time.monotonic() + timeout
    tail = b''
    peer_gone = False
    while time.monotonic() < deadline:
        try:
            with open(ack_path, 'rb') as f:
                f.seek(pos)
                chunk = f.read()
        except FileNotFoundError:
            chunk = b''
        if chunk:
            pos += len(chunk)
            tail += chunk
            *lines, tail = tail.split(b'\n')  # 末尾の書きかけの行は次回に回す
            for line in lines:
                try:
                    reply = json.loads(line.decode('utf-8', errors='replace'))
                except json.JSONDecodeError:
                    continue
                if reply.get('id') == request['id']:
                    if reply.get('status') not in OK_STATUSES:
                        raise RuntimeError(f"入力を拒否しました: {reply.get('error')} ({reply})")
                    return reply
        if alive is not None and not alive():
            if not chunk:
                if peer_gone:
                    raise PeerExited('応答を待つ間に画面のプロセスが終了しました')
                peer_gone = True    # 終了の直前に書かれた応答を取りこぼさないよう、もう 1 度だけ読んでから知らせる
                continue
        time.sleep(0.03)
    raise TimeoutError('登録応答を確認できません。未実行とは限らないため、自動で再送しないでください')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--file', required=True, type=Path)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--init', action='store_true', help='起動前に空の入力ファイルを作成する（既存ファイルは上書きしない）')
    action.add_argument('--drop-file', type=Path, help='インポート用の ZIP ドロップイベント')
    action.add_argument('--key', help='Enter, Escape, F10, x, KP_8 等')
    action.add_argument('--text', help='確定済みの UTF-8 文字列。改行等はキーで送る')
    action.add_argument('--query-ui', action='store_true', help='画面が直前に出したもの（相・字・部品・窓・HUD・地図…）を JSON で表示する')
    action.add_argument('--query-cell', metavar='X,Y', help='マス (gx,gy) の画面座標を JSON で表示する')
    action.add_argument('--tap', metavar='LABEL', help='押せる部品を名前で押す（直前のフレームの部品の中心をクリック）')
    action.add_argument('--tap-id', metavar='ID', help='押せる部品を言語に依存しない名前（id）で押す')
    action.add_argument('--wheel', metavar='X,Y[@PX,PY]', help='マウスのホイール（例 0,-1 や 0,-1@640,300）')
    action.add_argument('--quit', action='store_true', help='窓を閉じる（SDL_QUIT）')
    parser.add_argument('--key-text', help='Shift+記号など、key に対応する確定文字列を明示する')
    for flag in ('ctrl', 'shift', 'alt', 'numlock', 'repeat'):
        parser.add_argument('--' + flag, action='store_true')
    parser.add_argument('--contains', action='store_true', help='--tap：名前を「含む」部品を探す')
    parser.add_argument('--index', type=int, help='--tap：同じ名前の n 番目（0 から）')
    parser.add_argument('--kind', help='--tap：部品の種類で絞る（footer・tab・row・menu-row・core・lang・pad など）')
    parser.add_argument('--via', choices=('click', 'touch', 'auto'), help='--tap：入れ方（既定 auto。指でしか押せない部品はタッチ）')
    parser.add_argument('--map', choices=('none', 'near', 'full'), help='--query-ui：地形・実体の要約の範囲（既定 near）')
    parser.add_argument('--detail', action='store_true', help='--query-ui：マスごとの詳細（地形番号・絵の名前・色・光・町の意味づけ）を付ける')
    parser.add_argument('--timeout', type=float, default=10)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout は正の秒数にしてください")
    flags = {flag: True for flag in ('ctrl', 'shift', 'alt', 'numlock', 'repeat') if getattr(args, flag)}
    if not args.key and (flags or args.key_text is not None):
        parser.error('修飾キーと --key-text は --key と組み合わせてください')
    if not (args.tap or args.tap_id) and (args.contains or args.index is not None or args.kind or args.via):
        parser.error('--contains・--index・--kind・--via は --tap か --tap-id と組み合わせてください')
    if args.init:
        args.file.parent.mkdir(parents=True, exist_ok=True)
        with args.file.open('xb'):
            pass
        print(args.file.resolve())
        return
    if args.query_cell:
        cx, cy = (int(v) for v in args.query_cell.split(','))
        request = {'query': 'cell', 'x': cx, 'y': cy}
    elif args.query_ui:
        request = {'query': 'ui'}
        if args.map:
            request['map'] = args.map
        if args.detail:
            request['detail'] = True
    elif args.quit:
        request = {'quit': True}
    elif args.wheel:
        spec, _, at = args.wheel.partition('@')
        wx, wy = (int(v) for v in spec.split(','))
        request = {'wheel': {'x': wx, 'y': wy}}
        if at:
            request['wheel']['at'] = [int(v) for v in at.split(',')]
    elif args.tap or args.tap_id:
        tap = {'label': args.tap} if args.tap else {'id': args.tap_id}
        if args.contains:
            tap['contains'] = True
        if args.index is not None:
            tap['index'] = args.index
        if args.kind:
            tap['kind'] = args.kind
        if args.via:
            tap['via'] = args.via
        request = {'tap': tap}
    else:
        request = dict(key=args.key, **flags) if args.key else dict(text=args.text)
    if args.drop_file:
        request = {'drop_file': str(args.drop_file.resolve())}
    if args.key_text is not None:
        request['text'] = args.key_text
    reply = send_request(args.file, request, args.timeout)
    # 問い合わせの応答は大きいので、改行つきの読みやすい形で出す（ASCII のみ）。
    print(json.dumps(reply, ensure_ascii=True, indent=2 if (args.query_ui or args.query_cell) else None))


if __name__ == '__main__':
    main()
