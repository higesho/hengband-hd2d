"""実行中の HD2D のテスト入力ファイルに追記し、SDL キューへの登録応答を待つ。"""
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


def send_request(path: Path, request: dict, timeout: float = 10) -> dict:
    request = dict(request, id=uuid.uuid4().hex)
    raw = (json.dumps(request, ensure_ascii=False) + '\n').encode('utf-8')
    if len(raw) > 4096:
        raise ValueError('入力は UTF-8 で 4096 バイト以内にしてください')
    # 存在しないファイルを作らない。起動引数の指定間違いを検出する。
    fd = os.open(path, os.O_WRONLY | os.O_APPEND | getattr(os, 'O_BINARY', 0))
    try:
        if os.write(fd, raw) != len(raw):
            raise OSError('入力の追記が完了しませんでした。自動で再送しないでください')
    finally:
        os.close(fd)
    deadline = time.monotonic() + timeout
    ack_path = Path(str(path) + '.ack.jsonl')
    while time.monotonic() < deadline:
        try:
            lines = ack_path.read_text(encoding='utf-8').splitlines()
        except FileNotFoundError:
            lines = []
        for line in reversed(lines):
            try:
                reply = json.loads(line)
            except json.JSONDecodeError:
                continue  # 追記途中の応答は次回読む。
            if reply.get('id') == request['id']:
                if reply.get('status') != 'queued':
                    raise RuntimeError(f"入力を拒否しました: {reply.get('error')} ({reply})")
                return reply
        time.sleep(0.05)
    raise TimeoutError('登録応答を確認できません。未実行とは限らないため、自動で再送しないでください')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--file', required=True, type=Path)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--init', action='store_true', help='起動前に空の入力ファイルを作成する（既存ファイルは上書きしない）')
    action.add_argument('--drop-file', type=Path, help='インポート用の ZIP ドロップイベント')
    action.add_argument('--key', help='Enter, Escape, F10, x, KP_8 等')
    action.add_argument('--text', help='確定済みの UTF-8 文字列。改行等はキーで送る')
    parser.add_argument('--key-text', help='Shift+記号など、key に対応する確定文字列を明示する')
    for flag in ('ctrl', 'shift', 'alt', 'numlock', 'repeat'):
        parser.add_argument('--' + flag, action='store_true')
    parser.add_argument('--timeout', type=float, default=10)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout は正の秒数にしてください")
    flags = {flag: True for flag in ('ctrl', 'shift', 'alt', 'numlock', 'repeat') if getattr(args, flag)}
    if not args.key and (flags or args.key_text is not None):
        parser.error('修飾キーと --key-text は --key と組み合わせてください')
    if args.init:
        args.file.parent.mkdir(parents=True, exist_ok=True)
        with args.file.open('xb'):
            pass
        print(args.file.resolve())
        return
    request = dict(key=args.key, **flags) if args.key else dict(text=args.text)
    if args.drop_file:
        request = {'drop_file': str(args.drop_file.resolve())}
    if args.key_text is not None:
        request['text'] = args.key_text
    print(json.dumps(send_request(args.file, request, args.timeout), ensure_ascii=True))


if __name__ == '__main__':
    main()
