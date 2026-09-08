"""Generate reference recipes from pinned originals and verified project sources."""
from __future__ import annotations
import argparse
import io
import json
from pathlib import Path
import subprocess
import tarfile
from reconstruction_recipe import build, materialize
from reconstruction import sha

HERE=Path(__file__).resolve().parent

def original_files(upstream, source):
    repo=Path(upstream)/source['upstream_repo']
    raw=subprocess.check_output(['git','-C',str(repo),'-c','safe.directory='+repo.as_posix(),'archive','--format=tar',source['commit'],'src'])
    with tarfile.open(fileobj=io.BytesIO(raw)) as t:
        return {m.name:t.extractfile(m).read() for m in t if m.isfile()}

def generate(source,upstream,prepared):
    originals=original_files(upstream,source)
    target_root=Path(prepared)/source['output']
    targets={name:(target_root/name).read_bytes() for name in source['files']}
    for name,data in targets.items():
        if sha(data)!=source['files'][name]['sha256']:raise ValueError('Unexpected baseline source: '+name)
    preferred={name:('source','src/'+name) for name in targets}
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
