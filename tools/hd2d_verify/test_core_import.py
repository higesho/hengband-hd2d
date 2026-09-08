"""Windows の実 UI で Sil-Q の ZIP 選択・ビルド・登録・起動を確認する。"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import uuid
import threading
import time

from runtime import CfgGuard, SaveGuard, run_process
from send_test_key import send_request


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('zip',type=Path,help='対応する Sil-Q の原作ソース ZIP')
    parser.add_argument('--app-dir',type=Path,default=Path(__file__).resolve().parents[2])
    args=parser.parse_args();app=args.app_dir.resolve();repo=Path(__file__).resolve().parents[2]
    work=repo/'scratch_old'/('core-import-ui-'+uuid.uuid4().hex);work.mkdir(parents=True)
    inbox=work/'input.jsonl';inbox.write_text('',encoding='utf-8');protocol=work/'protocol.jsonl'
    storage=work/'日本語 😀 storage';env=dict(os.environ,HENGBAND_IMPORT_HOME=str(storage))
    result=[];errors=[]
    def game():
        try:result.append(run_process([str(app/'HengbandHd2d.exe'),'--windowed=1280x720','--test-input-file='+str(inbox),
            '--protocol-log='+str(protocol),'--shot='+str(work/'imported-core.bmp'),'--shot-after=-30'],cwd=app,env=env,timeout=420))
        except BaseException as error:errors.append(error)
    with ExitStack() as guards:
        guards.enter_context(CfgGuard(app))
        for name in ['save','user','apex']:guards.enter_context(SaveGuard(app/'silq/lib'/name,None,None))
        # 検査の並びを既知のコア順にする。利用者の設定は終了時に復元する。
        cfg=app/'hd2d.cfg'
        if cfg.exists():
            lines=[line for line in cfg.read_text(encoding='utf-8').splitlines() if not line.startswith('cores=')]
            cfg.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        worker=threading.Thread(target=game);worker.start()
        try:
            assert send_request(inbox,{'key':'F8'},90)['phase']=='core-select'
            assert send_request(inbox,{'drop_file':str(args.zip.resolve())},30)['phase']=='core-import'
            catalog=json.loads((app/'core-import/catalog.json').read_text(encoding='utf-8'))
            for _ in range(sorted(catalog['targets']).index('silq')):send_request(inbox,{'key':'Down'},30)
            send_request(inbox,{'key':'Enter'},30)
            registry=storage/'registry.json';end=time.monotonic()+240
            while time.monotonic()<end and not registry.exists():
                if result or errors:raise RuntimeError('UI ended during import')
                time.sleep(.2)
            assert registry.exists(),'registration timed out'
            data=json.loads(registry.read_text(encoding='utf-8'))['silq']
            assert hashlib.sha256(Path(data['path']).read_bytes()).hexdigest()==data['sha256']
            print('PASS: source ZIP compiled and registered through the UI',flush=True)
            send_request(inbox,{'key':'Escape'},30)
            previous=['HengbandCore.exe','TangbandCore.exe','GensobandCore.exe']
            count=sum((app/name).is_file() or (app/'cores'/name).is_file() for name in previous)
            for _ in range(count):send_request(inbox,{'key':'Down'},30)
            send_request(inbox,{'key':'Enter'},30)
            worker.join(120)
            assert not worker.is_alive() and not errors,errors
            assert result[0].returncode==0,result[0].returncode
            assert (work/'imported-core.bmp').exists()
            rows=[json.loads(line) for line in protocol.read_text(encoding='utf-8').splitlines()]
            assert any(row.get('t')=='frame' and row.get('dir')=='in' for row in rows)
            assert data['path'].replace('\\','/') in result[0].stdout.decode('utf-8','replace').replace('\\','/')
            print('PASS: imported executable started and rendered a core frame',flush=True)
        finally:
            worker.join()
            if result:(work/'run.log').write_bytes(result[0].stdout)
            elif errors and getattr(errors[0],'output',None):(work/'run.log').write_bytes(errors[0].output)
            print('Evidence:',work,flush=True)


if __name__=='__main__':main()
