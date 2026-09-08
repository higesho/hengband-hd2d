"""Prepare external core sources using immutable ranges and project additions."""
from __future__ import annotations
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid
from reconstruction_recipe import materialize
from generate_recipes import original_files

HERE=Path(__file__).resolve().parent
UI_ROOT=HERE.parent.parent
DEFAULT_UPSTREAM=UI_ROOT.parent/'roguelike-cores/upstream'
DEFAULT_OUTPUT=UI_ROOT.parent/'roguelike-cores/build-sources'

def digest(data):return hashlib.sha256(data).hexdigest()

def git(repo,*args,**kwargs):
    return subprocess.run(['git','-C',str(repo),'-c','safe.directory='+Path(repo).as_posix(),'-c','core.autocrlf=false',*args],check=True,**kwargs)

def transform(data,spec):
    if spec['newline']!='keep':
        data=data.replace(b'\r\n',b'\n')
        if spec['newline']=='crlf':data=data.replace(b'\n',b'\r\n')
    if spec['bom'] and not data.startswith(b'\xef\xbb\xbf'):data=b'\xef\xbb\xbf'+data
    elif not spec['bom'] and data.startswith(b'\xef\xbb\xbf'):data=data[3:]
    return data

def recipe_path(source):
    path=(HERE/source['recipe']).resolve()
    if not path.is_relative_to(HERE.resolve()) or path.suffix!='.json':raise ValueError('Unsafe source recipe path')
    return path

def manifest_fingerprint(manifest):
    if manifest.get('schema')!=2:raise ValueError('Source manifest must use range recipe schema 2')
    data=json.dumps(manifest,sort_keys=True).encode()
    for source in manifest['sources']:
        raw=recipe_path(source).read_bytes()
        if digest(raw)!=source['recipe_sha256']:raise ValueError('Source recipe hash mismatch: '+source['name'])
        recipe=json.loads(raw)
        if recipe.get('schema')!=2:raise ValueError('Unsupported source recipe schema')
        data+=raw
    return digest(data)

def verify_core(folder,source):
    expected=source['files'];actual={p.relative_to(folder).as_posix():p for p in folder.rglob('*') if p.is_file()}
    if actual.keys()!=expected.keys():raise ValueError('File list mismatch: '+source['name'])
    bad=[name for name,path in actual.items() if digest(path.read_bytes())!=expected[name]['sha256']]
    if bad:raise ValueError('Source hash mismatch: '+', '.join(bad[:5]))

@contextmanager
def staging_directory(parent):
    stage=parent/('.core-prepare-'+uuid.uuid4().hex);stage.mkdir()
    try:yield stage
    finally:
        if stage.exists():
            if stage.resolve().parent!=parent.resolve() or not stage.name.startswith('.core-prepare-'):raise ValueError('Unexpected staging cleanup path')
            shutil.rmtree(stage)

def prepare(upstream,output,verify_only=False):
    upstream,output=Path(upstream).resolve(),Path(output).resolve()
    if output==upstream or output.is_relative_to(upstream) or output.is_relative_to(UI_ROOT.resolve()):raise ValueError('Prepared sources must be outside the UI and upstream source directories')
    manifest=json.loads((HERE/'manifest.json').read_text(encoding='utf8'));fingerprint=manifest_fingerprint(manifest);marker=output/'.prepared.json'
    if marker.is_file() and json.loads(marker.read_text(encoding='utf8')).get('fingerprint')==fingerprint:
        for source in manifest['sources']:verify_core(output/source['output'],source)
        print('Prepared sources verified:',output);return
    if verify_only:raise ValueError('No matching prepared sources: '+str(output))
    if output.exists():raise ValueError('Output already exists; choose a new empty output path: '+str(output))
    output.parent.mkdir(parents=True,exist_ok=True)
    with staging_directory(output.parent) as stage:
        result=stage/'result';result.mkdir()
        for source in manifest['sources']:
            originals=original_files(upstream,source)
            recipe=json.loads(recipe_path(source).read_text(encoding='utf8'))
            files=materialize(recipe,lambda archive,path:originals[path] if archive=='source' else (_ for _ in ()).throw(ValueError('Unexpected source archive')))
            if files.keys()!=source['files'].keys():raise ValueError('Reconstruction file list mismatch')
            target=(result/source['output']).resolve()
            if not target.is_relative_to(result.resolve()):raise ValueError('Unsafe source output root')
            target.mkdir(parents=True)
            for name,data in files.items():
                path=(target/name).resolve()
                if not path.is_relative_to(target.resolve()):raise ValueError('Unsafe source output path')
                path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            verify_core(target,source)
            print(source['name']+': '+str(len(files))+' files match the baseline',flush=True)
        (result/'.prepared.json').write_text(json.dumps({'schema':2,'fingerprint':fingerprint,'baseline':manifest['baseline']},indent=2)+'\n',encoding='utf8',newline='\n')
        result.rename(output)
    print('Prepared sources:',output)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--upstream',type=Path,default=Path(os.environ.get('HENGBAND_UPSTREAM_ROOT',DEFAULT_UPSTREAM)));parser.add_argument('--output',type=Path,default=Path(os.environ.get('HENGBAND_CORE_SOURCE_ROOT',DEFAULT_OUTPUT)));parser.add_argument('--verify',action='store_true')
    args=parser.parse_args();prepare(args.upstream,args.output,args.verify)
if __name__=='__main__':main()
