"""原作を含まない配布用インポート定義を生成する（開発者用）。"""
import argparse
import difflib
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
from probe_compiler import ROOT, windows_recipe
sys.path.insert(0,str(ROOT/'tools'))
from transcode_cp932_src import transcode

def canonical(data):
    if b'\0' in data:return data
    return data.removeprefix(b'\xef\xbb\xbf').replace(b'\r\n',b'\n')

def digest(data):return hashlib.sha256(data).hexdigest()

def edits(a,b):
    aa,bb=a.splitlines(keepends=True),b.splitlines(keepends=True)
    offsets=[0]
    for line in aa:offsets.append(offsets[-1]+len(line))
    return [[offsets[i],offsets[j]-offsets[i],b''.join(bb[k:l]).hex()] for op,i,j,k,l in difflib.SequenceMatcher(None,aa,bb,autojunk=False).get_opcodes() if op!='equal']

def originals(name,commit):
    repo=(Path(os.environ.get('HENGBAND_UPSTREAM_ROOT',ROOT.parent/'roguelike-cores/upstream'))/name).resolve()
    data=subprocess.check_output(['git','-C',str(repo),'-c','safe.directory='+repo.as_posix(),'archive','--format=tar',commit,'src'])
    with tarfile.open(fileobj=io.BytesIO(data)) as t:return {m.name[4:]:t.extractfile(m).read() for m in t if m.isfile()}

def build(output):
    source=Path(os.environ.get('HENGBAND_CORE_SOURCE_ROOT',ROOT.parent/'roguelike-cores/build-sources')).resolve()
    manifest=json.loads((ROOT/'tools/core_sources/manifest.json').read_text(encoding='utf-8'))
    versions={'hengband':('Hengband','3.0.2.4-Beta','HengbandCore.exe'),'tangband':('Tangband','26.0.4','TangbandCore.exe'),'gensoband':('Gensoband','2.1.6','GensobandCore.exe'),'silq':('Sil-Q','1.5.1.0-beta2','SilCore.exe'),'frox':('FroxComposband','7.3.pipari.2','FroxCore.exe')}
    catalog={'schema':1,'targets':{}}
    kit=output/'sdk';kit.mkdir(parents=True,exist_ok=True)
    for folder in ['presentation','platform','gensoband/adapter','silq/adapter','frox/adapter','third_party/cpp']:
        shutil.copytree(ROOT/folder,kit/folder,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','.*'))
    for core in ['gensoband','silq']:
        for f in (kit/core/'adapter').rglob('*'):
            if f.suffix in ('.cpp','.c','.h'):
                data,_=transcode(f.read_bytes(),source_encoding='utf8');f.write_bytes(data)
    for core,(name,version,exe) in versions.items():
        spec=next(x for x in manifest['sources'] if x['name']==('hengband' if core=='tangband' else core))
        commit=spec['commit'] if core!='tangband' else '8d8e1f2cd74fe467a153d7dbdb0b2c5ed0ab72e2'
        base=originals(core,commit)
        files={}
        for path,info in spec['files'].items():
            incoming=canonical(base.get(path,b''))
            target=(source/spec['output']/path).read_bytes()
            if core=='gensoband' and Path(path).suffix in ('.c','.h'):
                target,_=transcode(target,source_encoding='cp932')
            target=canonical(target)
            files[spec['output']+'/'+path]={'input':'src/'+path if path in base else None,'input_sha256':digest(incoming),'sha256':digest(target),'edits':edits(incoming,target)}
        def template(s):
            # Replace the shared JSON include for legacy cores with the UI-owned generic library.
            if core not in ('hengband','tangband'):s=s.replace(str(source/'src/external-lib/include'),str(ROOT/'third_party/cpp/include'))
            return s.replace(str(source),'{source}').replace(str(ROOT),'{kit}/sdk').replace('\\','/')
        commands=[];objects=[]
        for i,(file,flags) in enumerate(windows_recipe(core,source)):
            obj='{work}/'+str(i)+'.o';objects.append(obj)
            commands.append(['{compiler}',*[template(f) for f in flags],'-c',template(str(file)),'-o',obj])
        linkflags=['--target=i686-w64-windows-gnu','-static','-lwinmm','-lgdi32','-lgdiplus','-lcomdlg32','-lws2_32','-ldbghelp','-o','{output}']
        commands.append(['{compiler}','@{work}/link.rsp'])
        recipe={'schema':1,'platform':'windows','probe':next(x['input'] for x in files.values() if x['input'] and x['input'].endswith('angband.h')),'output':exe,'files':files,'commands':commands,'responses':{'link.rsp':'\n'.join('"'+s+'"' for s in objects+linkflags)}}
        raw=json.dumps(recipe,ensure_ascii=False,separators=(',',':')).encode()
        (output/(core+'.json')).write_bytes(raw)
        catalog['targets'][core]={'name':name,'version':version,'recipe':core+'.json','sha256':digest(raw),'commit':commit}
        print(core,len(files),len(commands),len(raw),flush=True)
    catalog['sdk_sha256']={p.relative_to(output).as_posix():digest(p.read_bytes()) for p in sorted(kit.rglob('*')) if p.is_file() and not any(part.startswith('.') for part in p.relative_to(output).parts)}
    (output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n',encoding='utf-8',newline='\n')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();build(a.output.resolve())
