"""新しい入力ファイル経由で実コアを操作する。終了後にセーブと設定を復元する。"""
import json
import os
from pathlib import Path
import tempfile
import threading
import time

from playthrough import ROOT, EXE, CfgGuard, SaveGuard, decode
from runtime import run_process, SaveGuard as DirectoryGuard
from send_test_key import send_request


def main():
    with tempfile.TemporaryDirectory(prefix='hd2d-live-keyboard-') as tmp, CfgGuard(), SaveGuard('DBG'), DirectoryGuard(Path(ROOT) / 'lib/apex', None, None):
        tmp = Path(tmp)
        inbox, protocol = tmp / 'input.jsonl', tmp / 'protocol.jsonl'
        inbox.touch()
        # コア選択から始める。既存の設定は外側の CfgGuard が復元する。
        cfg = Path(ROOT) / 'hd2d.cfg'
        lines = cfg.read_text(encoding='utf-8').splitlines()
        lines = ['last_core=HengbandCore.exe' if line.startswith('last_core=') else line for line in lines]
        cfg.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        env = dict(os.environ)
        for k in ('HD2D_REPLAY_LOG', 'HD2D_FIXED_CLOCK', 'HENGBAND_SDL2_INJECT_KEYS', 'HD2D_PAD_PRESS'):
            env.pop(k, None)
        result, errors = [], []
        def game():
            try:
                result.append(run_process([EXE, '--windowed=1280x720', '--test-input-file=' + str(inbox),
                                           '--protocol-log=' + str(protocol)], cwd=ROOT, env=env, timeout=180))
            except BaseException as e:
                errors.append(e)
        worker = threading.Thread(target=game)
        worker.start()
        def records():
            try:
                raw = protocol.read_text(encoding='utf-8')
            except FileNotFoundError:
                return []
            out = []
            for line in raw.splitlines():
                try:
                    out.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
            return out
        def latest():
            frames = [json.loads(row['payload']) for row in records() if row.get('dir') == 'in' and row.get('t') == 'frame']
            return frames[-1] if frames else {}
        def wait_for(predicate, label, timeout=90):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if predicate():
                    print('PASS:', label, flush=True)
                    return
                if errors or result:
                    raise RuntimeError(f'Game ended during {label}: {errors or result[0].returncode}')
                time.sleep(0.05)
            raise TimeoutError(label)
        def text():
            return '\n'.join(line.get('text_utf8', '') for line in latest().get('menu_term_lines', []))
        def send(key, **kw):
            reply = send_request(inbox, dict(key=key, **kw), timeout=90)
            print('QUEUED:', key, reply['phase'], flush=True)
            return reply
        def input_count():
            return sum(row.get('dir') == 'out' and row.get('t') == 'input_event' for row in records())
        try:
            assert send('Enter')['phase'] == 'core-select'
            wait_for(lambda: 'New Game' in text(), 'core selected and title displayed')
            previous = latest().get('frame_id')
            send('Down')
            wait_for(lambda: latest().get('frame_id') != previous, 'title cursor moved')
            send('Enter')
            wait_for(lambda: 'Load Game' in text(), 'Enter opens save picker')
            send('Enter')
            wait_for(lambda: bool(latest().get('cells')) and not latest().get('menu_open', False), 'Enter loads DBG')
            pos = (latest().get('player_gx'), latest().get('player_gy'))
            send('Left')
            wait_for(lambda: (latest().get('player_gx'), latest().get('player_gy')) != pos, 'arrow moves player')
            send('F10')
            time.sleep(0.2)
            count = input_count()
            send('x')
            time.sleep(0.3)
            assert input_count() == count, 'feature menu did not consume text input'
            print('PASS: F10 menu consumes keyboard input', flush=True)
            send('Escape')
            time.sleep(0.2)
            send('F4')
            core_cfg = Path(ROOT) / 'hd2d-HengbandCore.cfg'
            wait_for(lambda: 'main_panel=ascii' in core_cfg.read_text(encoding='utf-8'), 'Escape closes menu and F4 selects ASCII')
            send('F4')
            wait_for(lambda: 'main_panel=hd2d' in core_cfg.read_text(encoding='utf-8'), 'F4 returns to 3D')
            send('x', ctrl=True)
            wait_for(lambda: latest().get('menu_open', False) and 'ESC' in text(), 'save completed prompt')
            send('Enter')
            wait_for(lambda: latest().get('menu_open', False) and len(latest().get('menu_term_lines', [])) > 2 and 'ESC' in text(), 'score display after saving')
            send('Escape')
            wait_for(lambda: any(row.get('dir') == 'in' and row.get('t') == 'exit' for row in records()), 'Ctrl+X saves and exits core')
            # 同じ入力ファイルを再利用する。再起動後に以前の命令を再送しないことも確認。
            assert send('Escape')['phase'] == 'core-select'
            worker.join(timeout=30)
            assert not worker.is_alive(), 'UI did not finish'
            assert not errors, errors
            assert result and result[0].returncode == 0, decode(result[0].stdout)[-2000:]
            print('PASS: restarted core selection and clean UI exit', flush=True)
        finally:
            # run_process の Job が子孫を終了させてから、外側のガードで復元する。
            worker.join()
            log_dir = Path(ROOT) / 'scratch_old' / 'test_keyboard_live'
            log_dir.mkdir(parents=True, exist_ok=True)
            if result:
                (log_dir / 'run.log').write_bytes(result[0].stdout)
            elif errors and getattr(errors[0], 'output', None):
                (log_dir / 'run.log').write_bytes(errors[0].output)
            for path in (inbox, Path(str(inbox) + '.ack.jsonl'), protocol):
                if path.exists():
                    (log_dir / path.name).write_bytes(path.read_bytes())


if __name__ == '__main__':
    main()
