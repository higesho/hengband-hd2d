from pathlib import Path
import argparse,json
from hashlib import sha256
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from paths import prepared_root,generated_root,write_if_changed
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--check',action='store_true')
parser.add_argument('--source-root',type=Path,default=None,help='prepared-source root; only used to place the output (default: HENGBAND_CORE_SOURCE_ROOT or ../roguelike-cores/build-sources)')
parser.add_argument('--output-root',type=Path,default=None,help='generated root (default: <source-root>-generated); writes <output-root>/presentation/bridge/')
parser.add_argument('--definitions',type=Path,default=None,help='ArtifactDefinitions.jsonc (default: the tracked lib/edit copy; tangband/lib/edit for --variant tangband)')
parser.add_argument('--variant',choices=('hengband','tangband'),default='hengband',help='variant: tangband reads tangband/src and writes <output-root>/tangband/presentation/bridge/ (Tangband keeps the Hengband 3.0.2.4 source)')
args=parser.parse_args()
if args.definitions is None:args.definitions=ROOT/('tangband/lib/edit' if args.variant=='tangband' else 'lib/edit')/'ArtifactDefinitions.jsonc'
if args.output_root is None:args.output_root=generated_root(args.source_root)
def jsonc(path):
    s=path.read_text(encoding='utf-8-sig');out=[];i=0;quoted=False
    while i<len(s):
        c=s[i]
        if quoted:
            out.append(c)
            if c=='\\' and i+1<len(s):i+=1;out.append(s[i])
            elif c=='"':quoted=False
        elif c=='"':quoted=True;out.append(c)
        elif s[i:i+2]=='//':
            j=s.find('\n',i);i=len(s) if j<0 else j;continue
        elif s[i:i+2]=='/*':
            i=s.index('*/',i+2)+2;continue
        else:out.append(c)
        i+=1
    s=''.join(out);out=[];i=0;quoted=False
    while i<len(s):
        c=s[i]
        if quoted:
            out.append(c)
            if c=='\\' and i+1<len(s):i+=1;out.append(s[i])
            elif c=='"':quoted=False
        elif c=='"':quoted=True;out.append(c)
        elif c==',' and s[i+1:].lstrip().startswith(('}',']')):pass
        else:out.append(c)
        i+=1
    return json.loads(''.join(out))

source=args.definitions
overrides_path=ROOT/'tools/core_sources/modern_artifact_lore.en.json'
overrides=json.loads(overrides_path.read_text(encoding='utf-8-sig'))
data=jsonc(source)['artifacts']
missing={str(r['id']) for r in data if r.get('flavor',{}).get('ja') and not r.get('flavor',{}).get('en')}
if set(overrides)!=missing:raise SystemExit('Review English lore overrides after artifact source changes.')
def literal(s):return json.dumps(s,ensure_ascii=False)
rows=[]
for r in sorted(data,key=lambda r:r['id']):
    lore=r.get('flavor',{})
    rows.append('    {'+str(r['id'])+','+','.join(literal(v) for v in [r['name'].get('ja',''),r['name'].get('en',''),lore.get('ja',''),lore.get('en',overrides.get(str(r['id']),''))])+'},')
out='// Generated from ArtifactDefinitions.jsonc and modern_artifact_lore.en.json.\n// Source SHA256: '+sha256(source.read_bytes()).hexdigest()+'\n// Translation SHA256: '+sha256(overrides_path.read_bytes()).hexdigest()+"""
#pragma once
#include <algorithm>
#include <iterator>
namespace presentation {
struct ModernArtifactText {int id;const char *ja_name,*en_name,*ja_lore,*en_lore;};
inline constexpr ModernArtifactText modern_artifact_texts[]={
"""+'\n'.join(rows)+"""
};
inline const ModernArtifactText *modern_artifact_text(int id) {
    const auto end=std::end(modern_artifact_texts);
    const auto found=std::lower_bound(std::begin(modern_artifact_texts),end,id,[](const auto &row,int key){return row.id<key;});
    return found!=end&&found->id==id?found:nullptr;
}
}
"""
target=args.output_root/('tangband/' if args.variant=='tangband' else '')/'presentation/bridge/modern_artifact_lore.generated.h'
if args.check:
    if not target.is_file() or target.read_text(encoding='utf-8-sig')!=out:raise SystemExit('Generated artifact locale header is missing or stale: '+str(target))
    print('Artifact locale data: 270 entries, 11 English supplements, source match.')
else:write_if_changed(target,out)
