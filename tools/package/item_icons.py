"""Copy the existing item images referenced by each core's own tile mapping."""
from pathlib import Path
import argparse,csv,re,shutil

def item_icon_paths(root):
    root=Path(root).resolve()
    table=(root/'presentation/bridge/official_tile_table.h').read_text(encoding='utf8')
    paths=set(re.findall(r"\{\s*\d+,\s*'K',\s*\d+,\s*\"([^\"]+\.png)\"",table))
    for core in ('gensoband','frox'):
        with (root/core/'tilework/mapping.csv').open(encoding='utf-8-sig',newline='') as stream:
            rows=csv.DictReader(line for line in stream if not line.lstrip().startswith('#'))
            for row in rows:
                if row['kind']!='K':continue
                if row['source']=='tile':paths.add(row['value'])
                elif row['source']=='ascii' and core=='gensoband':paths.add(f"gensoband/tilework/ascii/GA{ord(row['value'])}_{row['color']}.png")
    paths.update(p.relative_to(root).as_posix() for p in (root/'silq/tilework/128').glob('SK*.png'))
    result=[]
    for name in sorted(paths):
        source=(root/name).resolve()
        if not source.is_relative_to(root):raise ValueError('Item image outside the source root')
        if source.is_file():result.append(name)
    return result

def copy_item_icons(root,destination):
    root=Path(root).resolve();destination=Path(destination).resolve();paths=item_icon_paths(root)
    for name in paths:
        target=destination/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/name,target)
    return paths

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();files=copy_item_icons(args.root,args.out)
    print('Item icons:',len(files),'files,',sum((args.out/name).stat().st_size for name in files),'bytes')
