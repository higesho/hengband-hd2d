"""Native/Python reconstruction contract and CP932 conversion parity tests."""
import argparse,copy,hashlib,json,subprocess,sys,tempfile
from pathlib import Path
from reconstruction_recipe import build,materialize
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from transcode_cp932_src import transcode

def digest(b):return hashlib.sha256(b).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--driver',required=True,type=Path);ap.add_argument('--report',type=Path);a=ap.parse_args();driver=a.driver.resolve();results=[]
    with tempfile.TemporaryDirectory(prefix='source-ranges-') as tmp:
        root=Path(tmp);(root/'source').mkdir();(root/'source/a.txt').write_bytes(b'ABCDEF\r\n')
        data=b'ABCDEF\n';text=b'xyz';ident=digest(text)
        base={'schema':2,'inputs':{'a':{'archive':'source','path':'a.txt','sha256':digest(data),'transform':'raw','transformed_sha256':digest(data)}},'additions':{ident:{'encoding':'utf8','text':'xyz'}},'files':{'src/result.cpp':{'sha256':digest(b'DEFxyzABC'),'operations':[['copy','a',3,3],['add',ident],['copy','a',0,3]]}}}
        def case(name,edit=None,ok=False):
            spec=copy.deepcopy(base)
            if edit:edit(spec)
            path=root/'recipe.json';path.write_text(json.dumps(spec),encoding='utf8')
            r=subprocess.run([str(driver),str(path),str(root)],capture_output=True,timeout=20)
            assert r.returncode==(0 if ok else 1),(name,r.returncode,r.stderr)
            results.append({'name':name,'passed':True,'message':r.stderr.decode('utf8','replace').strip()});print('PASS',name,flush=True)
        case('immutable input / reordered ranges / CRLF normalization',ok=True)
        file=lambda s:s['files']['src/result.cpp']
        def formatted(s):
            f=file(s);f['operations']=[['copy','a',0,len(data)]];f['newline']='crlf';f['bom']=True
            f['sha256']=digest(b'\xef\xbb\xbfABCDEF\r\n')
        case('output CRLF and BOM are operations',formatted,ok=True)
        case('unknown output transform',lambda s:file(s).update(newline='unknown'))
        case('negative offset',lambda s:file(s)['operations'][0].__setitem__(2,-1))
        case('overflow offset',lambda s:file(s)['operations'][0].__setitem__(2,2**64))
        case('boolean offset',lambda s:file(s)['operations'][0].__setitem__(2,True))
        case('range past end',lambda s:file(s)['operations'][0].__setitem__(3,500))
        case('unknown operation',lambda s:file(s)['operations'][0].__setitem__(0,'shell'))
        case('unknown input',lambda s:file(s)['operations'][0].__setitem__(1,'absent'))
        case('unknown addition',lambda s:file(s)['operations'][1].__setitem__(1,'absent'))
        case('input hash mismatch',lambda s:s['inputs']['a'].update(sha256='0'*64))
        case('transformed hash mismatch',lambda s:s['inputs']['a'].update(transformed_sha256='0'*64))
        case('addition hash mismatch',lambda s:s['additions'][ident].update(text='modified'))
        case('output hash mismatch',lambda s:file(s).update(sha256='0'*64))
        case('unknown transform',lambda s:s['inputs']['a'].update(transform='command'))
        case('unknown encoding',lambda s:s['additions'][ident].update(encoding='command'))
        case('unsafe input',lambda s:s['inputs']['a'].update(path='../a.txt'))
        case('unsafe archive',lambda s:s['inputs']['a'].update(archive='../source'))
        case('unsafe output',lambda s:s['files'].update({'../outside':s['files'].pop('src/result.cpp')}))
        case('old body-patch recipe rejected',lambda s:s.update(schema=1))
        case('too many operations',lambda s:file(s).update(operations=[['copy','a',0,0]]*200001))
        samples={
          'CP932 strings and escapes':'/* 表ソ */\nchar *s="表ソ\\n"; // 日本語\nchar c=\'あ\';\n'.encode('cp932'),
          'all non-NUL non-LF CP932 byte pairs':b''.join(b'// '+bytes([x,y])+b'\n' for x in range(1,256) for y in range(1,256) if x!=10 and y!=10),
        }
        for name,source in samples.items():
            src=root/'convert.c';dst=root/'converted.c';src.write_bytes(source)
            expected=transcode(source,source_encoding='cp932')[0]
            r=subprocess.run([str(driver),'--transform','cp932-c',str(src),str(dst)],capture_output=True,timeout=20)
            actual=dst.read_bytes() if dst.exists() else b''
            assert r.returncode==0 and actual==expected,(name,r.stderr,next((i for i,(x,y) in enumerate(zip(actual,expected)) if x!=y),None),len(actual),len(expected))
            results.append({'name':name,'passed':True});print('PASS',name,flush=True)
        source={'source':{'a.c':b'int first = 1;\nint second = 2;\n','b.c':b'const char *name="reused original text";\n'}}
        targets={'out.c':b'  int first = 1;\nint third = 3;\nconst char *name="reused original text";\n'}
        plan,_=build(source,targets,{'out.c':('source','a.c')})
        assert materialize(plan,lambda archive,path:source[archive][path])==targets
        original=b'call(old_left, preserved_interior_value, old_right);\n'
        changed=b'call(new_left, preserved_interior_value, new_right);\n'
        plan,_=build({'source':{'a.c':original}},{'out.c':changed},{'out.c':('source','a.c')})
        assert materialize(plan,lambda archive,path:original)=={'out.c':changed}
        assert any(op[0]=='copy' and b'preserved_interior_value' in original[op[2]:op[2]+op[3]] for op in plan['files']['out.c']['operations'])
        results.append({'name':'two edits preserve unchanged line interior','passed':True})
        results.append({'name':'generated range plan round trip','passed':True});print('PASS generated range plan',flush=True)
    if a.report:a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(results,indent=2),encoding='utf8')
    print(len(results),'checks passed')
if __name__=='__main__':main()
