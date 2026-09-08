"""Audit schema-2 recipes for range validity and accidental original text payloads.

The text screen is a mechanical packaging guard, not a copyright assessment.
It checks complete source lines (24+ bytes) and quoted strings (12+ bytes).
"""
from __future__ import annotations
import argparse,json,re,hashlib
from pathlib import Path
from reconstruction_recipe import decode_addition,materialize
from reconstruction import canonical,variant

def audit(recipe,read):
    allowed={'schema','inputs','additions','files','archives','platform','probe','output','commands','responses'}
    if recipe.get('schema')!=2 or set(recipe)-allowed:raise ValueError('Unexpected recipe format or fields')
    anchors={};input_bytes=0
    quoted=re.compile(rb'"(?:\\.|[^"\\])*"')
    for spec in recipe['inputs'].values():
        if set(spec)!={'archive','path','sha256','transform','transformed_sha256'}:raise ValueError('Unexpected input fields')
        raw=canonical(read(spec['archive'],spec['path']));data=variant(raw,spec['transform']);input_bytes+=len(data)
        for line in data.splitlines():
            text=line.strip()
            if len(text)>=24:anchors.setdefault(text[:12],set()).add(text)
        for match in quoted.finditer(data):
            text=match[0]
            if len(text)>=12:anchors.setdefault(text[:12],set()).add(text)
    count=0;addition_bytes=0
    for key,spec in recipe['additions'].items():
        if set(spec) not in ({'encoding','text'},{'encoding','values'}):raise ValueError('Unexpected addition fields')
        data=decode_addition(spec);addition_bytes+=len(data)
        if hashlib.sha256(data).hexdigest()!=key:raise ValueError('Addition hash mismatch')
        for i in range(max(0,len(data)-11)):
            for text in anchors.get(data[i:i+12],()):
                if data.startswith(text,i):raise ValueError('Original source text remains in addition '+key)
    for spec in recipe['files'].values():
        if set(spec)-{'sha256','operations','newline','bom'}:raise ValueError('Unexpected output fields')
        for op in spec['operations']:
            if op[0] not in ('copy','add'):raise ValueError('Unsupported operation')
            count+=op[0]=='copy'
    result=materialize(recipe,read)
    return {'files':len(result),'copy_operations':count,'unique_addition_bytes':addition_bytes,'source_text_hits':0,'output_hashes_verified':True}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('recipe',type=Path);ap.add_argument('inputs',type=Path);ap.add_argument('--report',type=Path);a=ap.parse_args()
    recipe=json.loads(a.recipe.read_text(encoding='utf8'))
    root=a.inputs.resolve()
    def read(archive,path):
        p=(root/archive/path).resolve()
        if not p.is_relative_to(root):raise ValueError('Unsafe input')
        return p.read_bytes()
    result=audit(recipe,read)
    if a.report:a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps(result))
if __name__=='__main__':main()
