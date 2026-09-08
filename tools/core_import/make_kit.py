"""原作を含まない配布用インポート定義を生成する（開発者用）。"""
import argparse
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
sys.path.insert(0,str(ROOT/'tools/core_sources'))
from reconstruction_recipe import build as build_source_recipe

def canonical(data):
    if b'\0' in data:return data
    return data.removeprefix(b'\xef\xbb\xbf').replace(b'\r\n',b'\n')

def digest(data):return hashlib.sha256(data).hexdigest()

def originals(name,commit):
    repo=(Path(os.environ.get('HENGBAND_UPSTREAM_ROOT',ROOT.parent/'roguelike-cores/upstream'))/name).resolve()
    data=subprocess.check_output(['git','-C',str(repo),'-c','safe.directory='+repo.as_posix(),'archive','--format=tar',commit,'src'])
    with tarfile.open(fileobj=io.BytesIO(data)) as t:return {m.name[4:]:t.extractfile(m).read() for m in t if m.isfile()}

def build(output):
    source=Path(os.environ.get('HENGBAND_CORE_SOURCE_ROOT',ROOT.parent/'roguelike-cores/build-sources')).resolve()
    manifest=json.loads((ROOT/'tools/core_sources/manifest.json').read_text(encoding='utf-8'))
    versions={'hengband':('Hengband','3.0.2.4-Beta','HengbandCore.exe'),'tangband':('Tangband','26.0.4','TangbandCore.exe'),'gensoband':('Gensoband','2.1.6','GensobandCore.exe'),'silq':('Sil-Q','1.5.1.0-beta2','SilCore.exe'),'frox':('FroxComposband','7.3.pipari.2','FroxCore.exe')}
    catalog={'schema':2,'targets':{}}
    kit=output/'sdk';kit.mkdir(parents=True,exist_ok=True)
    for folder in ['presentation','platform','gensoband/adapter','silq/adapter','frox/adapter','third_party/cpp']:
        shutil.copytree(ROOT/folder,kit/folder,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','.*','hd2d_main.cpp','hd2d_entry_android.cpp'))
    for core in ['gensoband','silq']:
        for f in (kit/core/'adapter').rglob('*'):
            if f.suffix in ('.cpp','.c','.h'):
                data,_=transcode(f.read_bytes(),source_encoding='utf8');f.write_bytes(data)
    for core,(name,version,exe) in versions.items():
        spec=next(x for x in manifest['sources'] if x['name']==('hengband' if core=='tangband' else core))
        commit=spec['commit'] if core!='tangband' else '8d8e1f2cd74fe467a153d7dbdb0b2c5ed0ab72e2'
        base=originals(core,commit)
        archive_inputs={'source':{'src/'+p:data for p,data in base.items()}}
        archive_specs={'source':{'name':name,'version':version,'commit':commit}}
        if core=='tangband':
            # The historical Tangband target uses the shared Hengband source tree.
            # Missing original material must come from another user-provided ZIP.
            shared=originals('hengband',spec['commit'])
            archive_inputs['hengband']={'src/'+p:data for p,data in shared.items()}
            archive_specs['hengband']={'name':'Hengband','version':'3.0.2.4-Beta','commit':spec['commit']}
        targets={};preferred={}
        for path,info in spec['files'].items():
            target=(source/spec['output']/path).read_bytes()
            if core=='gensoband' and Path(path).suffix in ('.c','.h'):
                target,_=transcode(target,source_encoding='cp932')
            output_path=spec['output']+'/'+path
            targets[output_path]=canonical(target);preferred[output_path]=('source','src/'+path)
        source_recipe,source_report=build_source_recipe(archive_inputs,targets,preferred,
            {'source':'cp932-c'} if core=='gensoband' else {})
        for archive,desc in archive_specs.items():
            paths=archive_inputs[archive]
            preferred=['src/angband.h'] if 'src/angband.h' in paths else []
            candidates=preferred+[p for p in sorted(paths) if p not in preferred]
            probe=next((p for p in candidates if all(p not in other or digest(canonical(paths[p]))!=digest(canonical(other[p])) for key,other in archive_inputs.items() if key!=archive)),candidates[0])
            desc.update(probe=probe,sha256=digest(canonical(paths[probe])))
        source_recipe['archives']=archive_specs
        print(core,'range reconstruction',source_report['unique_addition_bytes'],'addition bytes',flush=True)
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
        recipe=dict(source_recipe,platform='windows',probe=archive_specs['source']['probe'],output=exe,
            commands=commands,responses={'link.rsp':'\n'.join('"'+s+'"' for s in objects+linkflags)})
        raw=json.dumps(recipe,ensure_ascii=False,separators=(',',':')).encode()
        (output/(core+'.json')).write_bytes(raw)
        catalog['targets'][core]={'name':name,'version':version,'recipe':core+'.json','sha256':digest(raw),'commit':commit,'sources':[v['name']+' '+v['version'] for v in archive_specs.values()]}
        print(core,len(recipe['files']),len(commands),len(raw),flush=True)
    catalog['sdk_sha256']={p.relative_to(output).as_posix():digest(p.read_bytes()) for p in sorted(kit.rglob('*')) if p.is_file() and not any(part.startswith('.') for part in p.relative_to(output).parts)}
    (output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n',encoding='utf-8',newline='\n')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();build(a.output.resolve())
