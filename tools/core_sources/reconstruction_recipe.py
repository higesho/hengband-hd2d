"""Wire format and generation shared by external preparation and ZIP importing."""
from __future__ import annotations
import hashlib
from pathlib import Path
from reconstruction import Planner, canonical, variant, sha, evaluate

SCHEMA = 2

def safe_path(value, archive=False):
    if not isinstance(value,str) or not value or len(value.encode('utf8'))>512:return False
    if any(c in value for c in ('\\',':','\0')) or any(p in ('','.','..') for p in value.split('/')):return False
    return not archive or '/' not in value

def encode_addition(data):
    for encoding,codec in [('utf8','utf-8'),('cp932','cp932')]:
        try:
            text=data.decode(codec)
            if text.encode(codec)==data:return {'encoding':encoding,'text':text}
        except UnicodeError:pass
    if len(data)<=16:return {'encoding':'bytes','values':list(data)}
    raise ValueError('Non-text addition needs explicit review: '+sha(data))

def decode_addition(item):
    kind=item['encoding']
    if kind=='utf8':return item['text'].encode('utf8')
    if kind=='cp932':return item['text'].encode('cp932')
    if kind=='bytes':
        if len(item['values'])>16 or any(type(x) is not int or not 0<=x<=255 for x in item['values']):
            raise ValueError('Invalid literal syntax bytes')
        return bytes(item['values'])
    raise ValueError('Unsupported addition encoding')

def build(archives, targets, preferred, transforms=None):
    """archives: {archive_id: {path: raw bytes}}, targets: {output: bytes}.

    preferred maps each output to (archive_id, path). Supplemental sources are
    explicit archive inputs; generation never fetches or embeds their contents.
    """
    transforms=transforms or {}
    inputs={};defs={};keys={}
    for archive,paths in sorted(archives.items()):
        for path,raw in sorted(paths.items()):
            data=canonical(raw)
            kinds=['raw']
            if transforms.get(archive)=='cp932-c' and Path(path).suffix in ('.c','.h'):kinds.append('cp932-c')
            for kind in kinds:
                key=f'i{len(defs):05d}'
                body=variant(data,kind)
                inputs[key]=body;defs[key]={'archive':archive,'path':path,'sha256':sha(data),'transform':kind,'transformed_sha256':sha(body)}
                keys[(archive,path,kind)]=key
    planner=Planner(inputs);files={};usage={}
    for name,target in sorted(targets.items()):
        archive,path=preferred.get(name,('source',name))
        kind='cp932-c' if (archive,path,'cp932-c') in keys else 'raw'
        key=keys.get((archive,path,kind))
        if key is None:
            key=next((k for (a,p,t),k in keys.items() if p==path and t==kind),None)
        normalized=canonical(target)
        bom=target.startswith(b'\xef\xbb\xbf') and b'\0' not in target
        prefix=b'\xef\xbb\xbf' if bom else b''
        if prefix+normalized==target:
            content=normalized;newline='keep'
        elif prefix+normalized.replace(b'\n',b'\r\n')==target:
            content=normalized;newline='crlf'
        else:
            content=target;newline='keep';bom=False
        files[name]=planner.plan(key,content)
        if newline!='keep':files[name]['newline']=newline
        if bom:files[name]['bom']=True
        files[name]['sha256']=sha(target)
        usage[name]=sum(len(planner.additions[op[1]]) for op in files[name]['operations'] if op[0]=='add')
    used={op[1] for file in files.values() for op in file['operations'] if op[0]=='copy'}
    recipe={'schema':SCHEMA,'inputs':{key:defs[key] for key in sorted(used)},
        'additions':{key:encode_addition(data) for key,data in sorted(planner.additions.items())},'files':files}
    decoded={key:decode_addition(value) for key,value in recipe['additions'].items()}
    for key,data in decoded.items():
        if sha(data)!=key:raise ValueError('Addition encoding changed bytes')
    for name,spec in files.items():
        if evaluate(spec,inputs,decoded)!=targets[name]:raise ValueError('Source reconstruction changed output')
    report={'files':len(files),'input_ranges':sum(op[0]=='copy' for v in files.values() for op in v['operations']),
        'addition_occurrence_bytes':sum(usage.values()),'unique_addition_bytes':sum(len(x) for x in decoded.values()),
        'additions':len(decoded),'per_file_addition_bytes':usage,'input_archives':sorted({v['archive'] for v in recipe['inputs'].values()}),
        'all_source_hashes_match':True}
    return recipe,report

def materialize(recipe,read):
    if recipe['schema']!=SCHEMA:raise ValueError('Unsupported source recipe version')
    if len(recipe['inputs'])>10000 or len(recipe['additions'])>50000 or len(recipe['files'])>10000:raise ValueError('Source recipe count limit exceeded')
    if any(not safe_path(path) for path in recipe['files']):raise ValueError('Unsafe output path')
    inputs={}
    for key,spec in recipe['inputs'].items():
        if not safe_path(spec['archive'],archive=True) or not safe_path(spec['path']):raise ValueError('Unsafe input path')
        data=canonical(read(spec['archive'],spec['path']))
        if sha(data)!=spec['sha256']:raise ValueError('Source input checksum mismatch: '+spec['path'])
        data=variant(data,spec['transform'])
        if sha(data)!=spec['transformed_sha256']:raise ValueError('Source transform checksum mismatch')
        inputs[key]=data
    additions={key:decode_addition(value) for key,value in recipe['additions'].items()}
    for key,data in additions.items():
        if sha(data)!=key:raise ValueError('Addition checksum mismatch')
    return {path:evaluate(spec,inputs,additions) for path,spec in recipe['files'].items()}
