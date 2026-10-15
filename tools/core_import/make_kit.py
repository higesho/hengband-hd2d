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
import tempfile
from probe_compiler import ROOT, windows_recipe, LINK_LIBS, DEFAULT_LINK_LIBS
sys.path.insert(0,str(ROOT/'tools'))
from transcode_cp932_src import transcode
sys.path.insert(0,str(ROOT/'tools/core_sources'))
from reconstruction_recipe import build as build_source_recipe
from generate_recipes import original_files, upstream_prefix

VERSION_DEFINITIONS = Path(__file__).with_name('supported_versions.json')

def canonical(data):
    if b'\0' in data:return data
    return data.removeprefix(b'\xef\xbb\xbf').replace(b'\r\n',b'\n')

def digest(data):return hashlib.sha256(data).hexdigest()

def validate_supported_versions(data):
    if data.get('schema')!=1 or not isinstance(data.get('targets'),list):
        raise ValueError('Unsupported core import version definition schema')
    required={'id','core','install_id','name','version','output','repo','commit'}
    ids=set();versions=set()
    for target in data['targets']:
        missing=required-set(target)
        if missing:raise ValueError('Missing supported version fields: '+', '.join(sorted(missing)))
        if target['id'] in ids:raise ValueError('Duplicate import target ID: '+target['id'])
        ids.add(target['id'])
        version_key=(target['core'],target['version'])
        if version_key in versions:raise ValueError('Duplicate supported core version: '+target['core']+' '+target['version'])
        versions.add(version_key)
        if '/' in target['id'] or '\\' in target['id'] or ':' in target['id']:
            raise ValueError('Import target ID must be a single path component: '+target['id'])
        if '/' in target['install_id'] or '\\' in target['install_id'] or ':' in target['install_id']:
            raise ValueError('Install ID must be a single path component: '+target['install_id'])
    return data['targets']

def supported_targets():
    return validate_supported_versions(json.loads(VERSION_DEFINITIONS.read_text(encoding='utf-8')))

def originals(name,commit):
    repo=(Path(os.environ.get('HENGBAND_UPSTREAM_ROOT',ROOT.parent/'roguelike-cores/upstream'))/name).resolve()
    data=subprocess.check_output(['git','-C',str(repo),'-c','safe.directory='+repo.as_posix(),'archive','--format=tar',commit,'src'])
    with tarfile.open(fileobj=io.BytesIO(data)) as t:return {m.name[4:]:t.extractfile(m).read() for m in t if m.isfile()}

def upstream_file(name,commit,path):
    repo=(Path(os.environ.get('HENGBAND_UPSTREAM_ROOT',ROOT.parent/'roguelike-cores/upstream'))/name).resolve()
    return subprocess.check_output(['git','-C',str(repo),'-c','safe.directory='+repo.as_posix(),'show',commit+':'+path])

MODERN_HEADERS={'modern_item_details':'src/perception/identification.cpp','modern_artifact_lore':'lib/edit/ArtifactDefinitions.jsonc'}

def generate_modern_headers(source,definitions_bytes,work,variant='hengband'):
    """変愚・短愚のコアが使う生成ヘッダを作る（リポジトリには置かない）。

    出力は再構成ツリー内の `generated/presentation/bridge/` の下。原作の文はキットへ入れず、
    他のソースと同じく、インポート時に ZIP の範囲コピーで再構成される（preferred の原作ファイルが入力）。
    """
    definitions=work/'ArtifactDefinitions.jsonc';definitions.write_bytes(definitions_bytes)
    out=work/'generated'
    # 短愚蛮怒は自分のソース（再構成先の tangband/src）から作る。キットの中の置き場は変愚と同じ generated/presentation。
    subprocess.run([sys.executable,str(ROOT/'tools/core_sources/generate_modern_item_details.py'),'--source-root',str(source),'--output-root',str(out),'--variant',variant],check=True)
    subprocess.run([sys.executable,str(ROOT/'tools/core_sources/generate_modern_artifact_lore.py'),'--source-root',str(source),'--output-root',str(out),'--definitions',str(definitions),'--variant',variant],check=True)
    made=out/('tangband' if variant=='tangband' else '')/'presentation/bridge'
    return {'generated/presentation/bridge/'+name+'.generated.h':(made/(name+'.generated.h')).read_bytes() for name in MODERN_HEADERS}

# 範囲レシピ（tools/core_sources/recipes）で組み立てるコア。原作の文はキットへ入れない。
RECIPE_CORES=('angband','rogue36','rogue54','rogue2','umoria')
# 原作のリポジトリからゲームデータとして読むフォルダー・ファイル（ソースの範囲の外）。
RECIPE_EXTRA_PATHS={'angband':['lib/gamedata','lib/customize','lib/screens','lib/help'],'rogue2':['mesg_J'],'umoria':['data','LICENSE']}
#: Umoria の `data/*.in` に埋める版（tools/umoria/build_windows.py の UMORIA_VERSION と同じ。原作の CMake の置換）。
UMORIA_VERSION='5.7.15'
ANGBAND_LIB_DIRS=('gamedata','customize','screens','help')

def upstream_root():
    return Path(os.environ.get('HENGBAND_UPSTREAM_ROOT',ROOT.parent/'roguelike-cores/upstream'))

def recipe_originals(spec,core):
    """固定コミットの原作から、ソース一式とデータを（ZIP 内と同じ相対パスで）取り出す。"""
    files=dict(original_files(upstream_root(),spec))
    repo=(upstream_root()/spec['upstream_repo']).resolve()
    extra=RECIPE_EXTRA_PATHS.get(core)
    if extra:
        data=subprocess.check_output(['git','-C',str(repo),'-c','safe.directory='+repo.as_posix(),'archive','--format=tar',spec['commit'],*extra])
        with tarfile.open(fileobj=io.BytesIO(data)) as t:files.update({m.name:t.extractfile(m).read() for m in t if m.isfile()})
    return files

def recipe_extra_outputs(core,paths,existing):
    """ソースの外で、インポート時に実行ファイルの隣へ置くデータ（原作 ZIP から範囲コピー）。"""
    out={}
    if core=='rogue2':
        out['rogue2/mesg']='mesg_J'
    elif core=='angband':
        for path in paths:
            parts=path.split('/')
            if parts[0]=='lib' and len(parts)>2 and parts[1] in ANGBAND_LIB_DIRS and not parts[-1].startswith('Makefile'):
                out['angband/'+path]=path
    elif core=='umoria':
        # tools/umoria/build_windows.py の install_data() と同じ: data/ の文章と LICENSE を `umoria/data/` へ。
        # `.in` は名前から外し、版を埋める（load_recipe_extra が中身を作る）。スコアは data_dir() 側に作るので除く。
        for path in paths:
            parts=path.split('/')
            if parts[0]=='data' and len(parts)==2:
                name=parts[1].removesuffix('.in')
                if name!='scores.dat':out['umoria/data/'+name]=path
            elif path=='LICENSE':out['umoria/data/LICENSE']=path
    return out

def recipe_extra_data(core,origin,data):
    """範囲コピーの出力の中身。Umoria の `.in` だけ版を埋める（原作の CMake の configure_file と同じ）。"""
    if core=='umoria' and origin.endswith('.in'):return data.replace(b'${umoria_version}',UMORIA_VERSION.encode())
    return data

def stage_angband_project_data(kit):
    """UI が持つ Angband 用のデータ。原作 ZIP からは得られない物をキットへ置く（SHA-256 で照合される）。"""
    dest=kit/'install-data'
    ignore=shutil.ignore_patterns('__pycache__','.*')
    for source,target in [('angband/lang','angband/lang'),('angband/assets/slab','angband/assets/slab'),
                          ('angband/lib/hd2d','angband/lib/hd2d'),('angband/tilework/mapping.csv','angband/tilework/mapping.csv'),
                          ('tilework/tile_mapping.csv','tilework/tile_mapping.csv'),
                          ('lib/edit/BaseitemDefinitions.jsonc','lib/edit/BaseitemDefinitions.jsonc')]:
        src=ROOT/source;dst=dest/'angband'/target
        if src.is_dir():shutil.copytree(src,dst,dirs_exist_ok=True,ignore=ignore)
        else:
            dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)

def stage_umoria_project_data(kit):
    """UI が持つ Umoria 用のデータ（町の地図。um_town.cpp が `<exe>/umoria/hd2d/town.txt` を読む）。"""
    for folder in ('hd2d','lang'):  # lang は日本語の訳の表
        shutil.copytree(ROOT/'umoria'/folder,kit/'install-data'/'umoria'/'umoria'/folder,dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('__pycache__','.*'))

def build(output):
    """キットを作る。ソースは開発ビルドと同じターゲット指定のオーバーレイ（tools/targeting/overlay.py）を出力にする。

    Windows の開発ビルドは再構成済みソースを直接ではなく、このオーバーレイを組む。キットもオーバーレイを
    再構成の出力にすれば、原作の行は ZIP の範囲コピー、挿入した自作の行だけが追加内容になる。
    """
    source=Path(os.environ.get('HENGBAND_CORE_SOURCE_ROOT',ROOT.parent/'roguelike-cores/build-sources')).resolve()
    overlay=Path(tempfile.mkdtemp(prefix='hb-kit-overlay-'))
    try:
        subprocess.run([sys.executable,str(ROOT/'tools/targeting/overlay.py'),'--sources',str(source),'--output',str(overlay)],check=True,stdout=subprocess.DEVNULL)
        _build(output,overlay)
    finally:
        shutil.rmtree(overlay,ignore_errors=True)

def _build(output,overlay):
    source=Path(os.environ.get('HENGBAND_CORE_SOURCE_ROOT',ROOT.parent/'roguelike-cores/build-sources')).resolve()
    base_source=Path(os.environ.get('HENGBAND_BASE_CORE_SOURCE_ROOT',ROOT.parent/'roguelike-cores/build-sources')).resolve()
    manifest=json.loads((ROOT/'tools/core_sources/manifest.json').read_text(encoding='utf-8'))
    versions=supported_targets()
    catalog={'schema':2,'targets':{}}
    kit=output/'sdk';kit.mkdir(parents=True,exist_ok=True)
    for folder in ['presentation','platform','gensoband/adapter','silq/adapter','frox/adapter','rogue2/adapter','angband/adapter','umoria/adapter','rogue_legacy/adapter','rogue36/adapter','rogue54/adapter','third_party/cpp']:
        shutil.copytree(ROOT/folder,kit/folder,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','.*','hd2d_main.cpp','hd2d_entry_android.cpp'))
    stage_angband_project_data(kit)
    stage_umoria_project_data(kit)
    for core in ['gensoband','silq','frox']:
        for f in (kit/core/'adapter').rglob('*'):
            if f.suffix in ('.cpp','.c','.h','.inl'):
                data,_=transcode(f.read_bytes(),source_encoding='utf8');f.write_bytes(data)
    for target in versions:
        target_id=target['id'];core=target['core'];install_id=target['install_id']
        name=target['name'];version=target['version'];exe=target['output'];commit=target['commit']
        prefix='src/';install_files={}
        spec=next(x for x in manifest['sources'] if x['name']==target.get('manifest_source',core))
        # 原作ソースの一覧（manifest）の固定コミットが、この項目の ZIP（短愚は共有側の変愚 ZIP）と同じかを見る。
        # 2026-10-05 に変愚の一覧だけ 3.0.2.5 へ進んだとき、3.0.2.4 の項目が新しい一覧で黙って組まれる形になっていた。
        if spec['commit'] not in {commit,*(x['commit'] for x in target.get('shared_sources',[]))}:
            raise ValueError('Import target commit differs from the source manifest: '+target_id)
        if core in RECIPE_CORES:
            if spec['commit']!=commit:raise ValueError('Import target commit differs from the source manifest: '+target_id)
            base=recipe_originals(spec,core);prefix=upstream_prefix(spec)
            archive_inputs={'source':base}
        else:
            base=originals(target['repo'],commit)
            archive_inputs={'source':{'src/'+p:data for p,data in base.items()}}
        archive_specs={'source':{'name':name,'version':version,'commit':commit}}
        modern_headers={};modern_archive=None
        if core in ('hengband','tangband'):
            # 変愚の原作（短愚では共有側の変愚 ZIP）から、生成ヘッダの入力になる定義ファイルも範囲コピーの入力に足す。
            modern_archive='source' if core=='hengband' else 'hengband'
            modern_commit=commit if core=='hengband' else next(x['commit'] for x in target['shared_sources'] if x['archive']=='hengband')
        for shared_spec in target.get('shared_sources',[]):
            # Some historical targets use source shared with another core. Keep the
            # exact supporting source version in the version definition instead of
            # silently following whatever the newest manifest happens to contain.
            shared=originals(shared_spec['repo'],shared_spec['commit'])
            archive=shared_spec['archive']
            archive_inputs[archive]={'src/'+p:data for p,data in shared.items()}
            archive_specs[archive]={'name':shared_spec['name'],'version':shared_spec['version'],'commit':shared_spec['commit']}
        if modern_archive:
            definitions_bytes=upstream_file('hengband',modern_commit,MODERN_HEADERS['modern_artifact_lore'])
            archive_inputs[modern_archive][MODERN_HEADERS['modern_artifact_lore']]=definitions_bytes
            with tempfile.TemporaryDirectory() as tmp:modern_headers=generate_modern_headers(source,definitions_bytes,Path(tmp),core)
        if True:
            targets={};preferred={}
            for path,info in spec['files'].items():
                candidate=overlay/spec['output']/path
                if not candidate.is_file():candidate=source/spec['output']/path
                target_data=(candidate if candidate.is_file() else base_source/spec['output']/path).read_bytes()
                if core=='gensoband' and Path(path).suffix in ('.c','.h'):
                    target_data,_=transcode(target_data,source_encoding='cp932')
                output_path=spec['output']+'/'+path
                targets[output_path]=canonical(target_data);preferred[output_path]=('source',prefix+path)
            for extra in (overlay/spec['output']).rglob('*.generated.inl'):
                target_data=extra.read_bytes()
                if core=='gensoband':target_data,_=transcode(target_data,source_encoding='cp932')
                output_path=extra.relative_to(overlay).as_posix();targets[output_path]=canonical(target_data)
                origin='target/projection-path-calculator.cpp' if core in ('hengband','tangband') else 'cave.c' if core=='silq' else 'spells1.c'
                preferred[output_path]=('source','src/'+origin)
            for output_path,data in modern_headers.items():
                targets[output_path]=data
                preferred[output_path]=(modern_archive,MODERN_HEADERS[Path(output_path).name.removesuffix('.generated.h')])
            if core in RECIPE_CORES:
                for output_path,origin in recipe_extra_outputs(core,list(base),targets).items():
                    targets[output_path]=canonical(recipe_extra_data(core,origin,base[origin]));preferred[output_path]=('source',origin)
                    if output_path.startswith('angband/lib/') or output_path.startswith('umoria/data/'):install_files[output_path]=output_path
                    elif output_path=='rogue2/mesg':install_files[output_path]='mesg'
            source_recipe,source_report=build_source_recipe(archive_inputs,targets,preferred,
                {'source':'cp932-c'} if core=='gensoband' else {})
        for archive,desc in archive_specs.items():
            paths=archive_inputs[archive]
            preferred=['src/angband.h'] if 'src/angband.h' in paths else []
            # 再構成に使うファイルを先に、ZIP 内で接尾辞が重ならない物だけを探針にする（prefix() が 1 か所に決まるため）。
            used=sorted({v['path'] for v in source_recipe['inputs'].values() if v['archive']==archive})
            candidates=preferred+[p for p in used+sorted(paths) if p not in preferred]
            unique=[p for p in candidates if not any(o!=p and o.endswith('/'+p) for o in paths)]
            probe=next((p for p in unique if all(p not in other or digest(canonical(paths[p]))!=digest(canonical(other[p])) for key,other in archive_inputs.items() if key!=archive)),unique[0] if unique else candidates[0])
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
        linkflags=['--target=i686-w64-windows-gnu','-static',*LINK_LIBS.get(core,DEFAULT_LINK_LIBS),'-o','{output}']
        commands.append(['{compiler}','@{work}/link.rsp'])
        recipe=dict(source_recipe,platform='windows',probe=archive_specs['source']['probe'],output=exe,
            commands=commands,responses={'link.rsp':'\n'.join('"'+s+'"' for s in objects+linkflags)})
        if install_files:recipe['install_files']=install_files
        if core=='angband':recipe['install_kit_dirs']={'sdk/install-data/angband':''}
        if core=='umoria':recipe['install_kit_dirs']={'sdk/install-data/umoria':''}
        raw=json.dumps(recipe,ensure_ascii=False,separators=(',',':')).encode()
        recipe_name=target.get('recipe',target_id+'.json')
        (output/recipe_name).write_bytes(raw)
        catalog['targets'][target_id]={'core':core,'install_id':install_id,'name':name,'version':version,'recipe':recipe_name,'sha256':digest(raw),'commit':commit,'sources':[v['name']+' '+v['version'] for v in archive_specs.values()]}
        print(target_id,len(recipe['files']),len(commands),len(raw),flush=True)
    catalog['sdk_sha256']={p.relative_to(output).as_posix():digest(p.read_bytes()) for p in sorted(kit.rglob('*')) if p.is_file() and not any(part.startswith('.') for part in p.relative_to(output).parts)}
    (output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n',encoding='utf-8',newline='\n')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();build(a.output.resolve())
