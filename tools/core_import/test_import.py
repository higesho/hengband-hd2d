"""実際の C++ インポーターで、不正 ZIP・変更ソース・キャンセル・既存登録の保持を検証する。"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile


def main():
    p=argparse.ArgumentParser();p.add_argument('--driver',type=Path,required=True);p.add_argument('--kit',type=Path,required=True);p.add_argument('--compiler',type=Path,required=True);p.add_argument('--source',type=Path,required=True);a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='hd2d-import-test-') as tmp:
        root=Path(tmp);store=root/'store';store.mkdir()
        preserved=b'{"old":{"path":"old-core.exe"}}\n';(store/'registry.json').write_bytes(preserved)
        def run(path,tag,cancel=False,kit=None,expected=None):
            args=[str(a.driver.resolve()),str((kit or a.kit).resolve()),str(store),str(a.compiler.resolve()),str(path),'silq','windows',str(a.compiler.resolve())]
            if cancel:args.append('--cancel')
            r=subprocess.run(args,capture_output=True,timeout=60)
            assert r.returncode==(3 if cancel else 1),(tag,r.returncode,r.stdout,r.stderr)
            state=json.loads(r.stdout.decode('utf-8').splitlines()[-1])
            assert state['phase']==('cancelled' if cancel else 'error'),(tag,state)
            if expected:assert expected in state['message'],(tag,state)
            assert (store/'registry.json').read_bytes()==preserved,tag
            print('PASS:',tag,flush=True)
        for tag,entries in [
            ('traversal',[('../escape','x')]),('absolute',[('/escape','x')]),
            ('windows-drive',[('C:/escape','x')]),('backslash',[('src\\escape','x')]),
            ('duplicate',[('src/angband.h','x'),('src/ANGBAND.h','x')]),
            ('unsupported-version',[('src/angband.h','modified')]),
        ]:
            path=root/(tag+'.zip')
            with zipfile.ZipFile(path,'w') as z:
                for name,data in entries:z.writestr(name,data)
            run(path,tag)
        path=root/'symlink.zip'
        with zipfile.ZipFile(path,'w') as z:
            info=zipfile.ZipInfo('src/angband.h');info.create_system=3;info.external_attr=(0o120777<<16);z.writestr(info,'/etc/passwd')
        run(path,'symlink')
        path=root/'truncated.zip';path.write_bytes(a.source.read_bytes()[:-20]);run(path,'truncated')
        catalog=json.loads((a.kit/'catalog.json').read_text(encoding='utf-8'))
        recipe=json.loads((a.kit/catalog['targets']['silq']['recipe']).read_text(encoding='utf-8'))
        first=next(spec['input'] for _,spec in sorted(recipe['files'].items()) if spec['input'])
        path=root/'bad-crc.zip'
        with zipfile.ZipFile(a.source) as source,zipfile.ZipFile(path,'w',compression=zipfile.ZIP_STORED) as dest:
            for entry in source.infolist():dest.writestr(entry.filename,source.read(entry))
        with zipfile.ZipFile(path) as z:offset=z.getinfo(first).header_offset
        raw=bytearray(path.read_bytes());start=offset+30+int.from_bytes(raw[offset+26:offset+28],'little')+int.from_bytes(raw[offset+28:offset+30],'little');raw[start]^=1;path.write_bytes(raw)
        run(path,'CRC mismatch',expected='checksum mismatch')
        path=root/'modified.zip'
        with zipfile.ZipFile(a.source) as source,zipfile.ZipFile(path,'w') as dest:
            for entry in source.infolist():dest.writestr(entry.filename,source.read(entry)+(b'changed' if entry.filename==first else b''))
        run(path,'source content hash mismatch',expected='Unsupported or modified source version')
        run(a.source.resolve(),'cancel during compilation',cancel=True)
        kit=root/'kit';kit.mkdir()
        catalog=json.loads((a.kit/'catalog.json').read_text(encoding='utf-8'))
        (kit/'catalog.json').write_text(json.dumps(catalog),encoding='utf-8')
        recipe=catalog['targets']['silq']['recipe'];(kit/recipe).write_bytes(b'{}')
        run(a.source.resolve(),'tampered build recipe',kit=kit)
        catalog['sdk_sha256']={'sdk/stub.h':hashlib.sha256(b'expected').hexdigest()}
        (kit/'catalog.json').write_text(json.dumps(catalog),encoding='utf-8')
        shutil.copy2(a.kit/recipe,kit/recipe)
        (kit/'sdk').mkdir();(kit/'sdk/stub.h').write_bytes(b'changed')
        run(a.source.resolve(),'tampered support SDK',kit=kit,expected='support file checksum mismatch')
        path=root/'limits.zip'
        with zipfile.ZipFile(path,'w') as z:z.writestr('src/angband.h','x')
        clean=path.read_bytes();at=clean.index(b'PK\x01\x02')
        raw=bytearray(clean);raw[at+24:at+28]=(64*1024*1024+1).to_bytes(4,'little');path.write_bytes(raw)
        run(path,'expanded-size limit',expected='expanded size')
        raw=bytearray(clean);raw[at+8]|=1;path.write_bytes(raw)
        run(path,'encrypted ZIP',expected='Encrypted ZIP')
        assert not (root/'escape').exists()
    print('PASS: rejection and cancellation leave the previous registry unchanged')

if __name__=='__main__':main()
