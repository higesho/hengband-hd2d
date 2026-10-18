"""Join source inputs to generated profiles without counting them as tested actions."""
from pathlib import Path
import argparse,collections,json
from profile_inventory import audit

def build(sources,overlay):
    result={}
    for core,folder,filename in [('frox','frox/src','profiles.json'),('genso','gensoband/src','genso-profiles.json'),('heng','src','heng-profiles.json'),('sil','silq/src','sil-profiles.json'),
                                 ('toband','toband/src','toband-profiles.json')]:
        profiles=json.loads((overlay/filename).read_text(encoding='utf8'))
        indexed={p['site'].removeprefix('movement:'):p for p in profiles}
        rows=audit(sources/folder)
        for row in rows:
            site=f"{core}:{Path(row['file']).name}:{row['line']}"
            row['site']=site
            row['profile']=indexed.get(site)
        missing=[r for r in rows if r['profile'] and not r['profile']['supported']]
        result[core]={'input_sites':rows,'summary':{
            'inventory_inputs':len(rows),'annotated_inputs':len(profiles),
            'profiles_defined':sum(p['supported'] for p in profiles),'profiles_missing':len(missing),
            'missing_scoped_projection_calls':dict(collections.Counter(p['function'] for r in missing for p in r['projections'])),
            'missing_input_owners':dict(collections.Counter(r['function'] for r in missing))}}
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--sources',type=Path,required=True);parser.add_argument('--overlay',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=build(args.sources,args.overlay)
    args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps({core:entry['summary'] for core,entry in result.items()},indent=2))
