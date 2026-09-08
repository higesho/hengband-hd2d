"""Review or record external source changes as range recipes, never body patches."""
from __future__ import annotations
import argparse,copy,json
from pathlib import Path
from prepare import HERE,DEFAULT_UPSTREAM,DEFAULT_OUTPUT,digest
from generate_recipes import original_files
from reconstruction_recipe import build,materialize


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source-root',type=Path,default=DEFAULT_OUTPUT);ap.add_argument('--upstream',type=Path,default=DEFAULT_UPSTREAM);ap.add_argument('--core');ap.add_argument('--record',action='store_true');args=ap.parse_args()
    manifest=json.loads((HERE/'manifest.json').read_text(encoding='utf8'));changed=False
    for source in manifest['sources']:
        if args.core and source['name']!=args.core:continue
        folder=(args.source_root/source['output']).resolve()
        targets={p.relative_to(folder).as_posix():p.read_bytes() for p in folder.rglob('*') if p.is_file()}
        originals=original_files(args.upstream,source)
        recipe,report=build({'source':originals},targets,{name:('source','src/'+name) for name in targets})
        assert materialize(recipe,lambda archive,path:originals[path])==targets
        raw=(json.dumps(recipe,ensure_ascii=False,indent=2)+'\n').encode('utf8')
        same=digest(raw)==source['recipe_sha256']
        before=source['files'];added=set(targets)-set(before);removed=set(before)-set(targets)
        modified=[name for name in set(targets)&set(before) if digest(targets[name])!=before[name]['sha256']]
        print(source['name'], 'unchanged' if same else 'changed',f'added={len(added)} modified={len(modified)} removed={len(removed)}',flush=True)
        if not same and args.record:
            expected={}
            for name,data in targets.items():
                entry=copy.deepcopy(before.get(name,{}));original=originals.get('src/'+name)
                entry.update(sha256=digest(data),upstream=original is not None,bom=data.startswith(b'\xef\xbb\xbf'),newline='crlf' if b'\r\n' in data else 'lf')
                if original is not None:entry['upstream_sha256']=digest(original)
                else:entry.pop('upstream_sha256',None)
                expected[name]=entry
            source['files']=expected;source['recipe_sha256']=digest(raw)
            (HERE/source['recipe']).write_bytes(raw);changed=True
    if args.core and args.core not in [s['name'] for s in manifest['sources']]:raise ValueError('Unknown core')
    if changed:(HERE/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8',newline='\n')
    if not args.record:print('Review only. Recipes and manifest were not changed.')
if __name__=='__main__':main()
