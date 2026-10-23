"""Generate reference recipes from pinned originals and verified project sources."""
from __future__ import annotations
import argparse
import io
import json
from pathlib import Path
import subprocess
import tarfile
from reconstruction_recipe import build, materialize, safe_path
from reconstruction import sha

HERE=Path(__file__).resolve().parent

def upstream_path(source):
    """Folder of the upstream repository holding the sources ('src' unless the manifest says otherwise; '.' is the repository root)."""
    value=source.get('upstream_path','src')
    if value in ('','.'):return '.'
    if not safe_path(value) :raise ValueError('Unsafe upstream_path')
    return value

def upstream_prefix(source):
    """Prefix of the names returned by original_files ('src/', or '' for the repository root)."""
    path=upstream_path(source)
    return '' if path=='.' else path+'/'

def original_files(upstream, source):
    repo=Path(upstream)/source['upstream_repo']
    path=upstream_path(source)
    raw=subprocess.check_output(['git','-C',str(repo),'-c','safe.directory='+repo.as_posix(),'archive','--format=tar',source['commit'],*([] if path=='.' else [path])])
    with tarfile.open(fileobj=io.BytesIO(raw)) as t:
        files={m.name:t.extractfile(m).read() for m in t if m.isfile()}
    # 原作が git のサブモジュールで持つ付属ライブラリ（DCSS の contrib/lua など）。親のコミットの archive には入らないので、
    # 項目に 'submodules' があるときだけ、サブモジュール自身のリポジトリ（upstream からの相対）の固定コミットを足す。
    # 名前は親の中の位置（upstream_prefix + path + '/' + ファイル名）。'submodules' の無い項目には何も起きない。
    for sub in source.get('submodules',[]):
        if not safe_path(sub['path']) or not safe_path(sub['repo']):raise ValueError('Unsafe submodule path')
        subrepo=Path(upstream)/sub['repo']
        raw=subprocess.check_output(['git','-C',str(subrepo),'-c','safe.directory='+subrepo.as_posix(),'archive','--format=tar',sub['commit']])
        with tarfile.open(fileobj=io.BytesIO(raw)) as t:
            for m in t:
                if m.isfile():files[upstream_prefix(source)+sub['path']+'/'+m.name]=t.extractfile(m).read()
    return files

def generate(source,upstream,prepared):
    originals=original_files(upstream,source)
    target_root=Path(prepared)/source['output']
    targets={name:(target_root/name).read_bytes() for name in source['files']}
    for name,data in targets.items():
        if sha(data)!=source['files'][name]['sha256']:raise ValueError('Unexpected baseline source: '+name)
    prefix=upstream_prefix(source)
    preferred={name:('source',prefix+name) for name in targets}
    recipe,report=build({'source':originals},targets,preferred)
    result=materialize(recipe,lambda archive,path: originals[path])
    if result!=targets:raise ValueError('Generated recipe differs from baseline')
    return recipe,report

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--upstream',type=Path,required=True);ap.add_argument('--prepared',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();manifest=json.loads((HERE/'manifest.json').read_text(encoding='utf8'))
    args.output.mkdir(parents=True,exist_ok=True);reports={}
    for source in manifest['sources']:
        recipe,report=generate(source,args.upstream,args.prepared)
        path=args.output/(source['name']+'.json');path.write_text(json.dumps(recipe,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')
        reports[source['name']]=report
        print(source['name'],report['files'],'files',report['unique_addition_bytes'],'addition bytes; hashes match',flush=True)
    (args.output/'generation-report.json').write_text(json.dumps(reports,indent=2)+'\n',encoding='utf8',newline='\n')
if __name__=='__main__':main()
