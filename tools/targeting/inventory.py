"""Inventory original-core target-input call sites without changing their sources."""
from pathlib import Path
import argparse,hashlib,json,re
ROOT=Path(__file__).resolve().parents[2]
TOKENS=re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',re.S)
CALL=re.compile(r'\b(get_buff_mon_dir|get_capture_mon_dir|get_fire_dir_aux|get_fire_dir|get_aim_dir_aux|get_aim_dir|get_rep_dir2|get_rep_dir|get_direction|get_hack_dir|get_direct_target|target_set_interactive|target_set|tgt_pt|point_target)\s*\(')
SHAPE=re.compile(r'\b(project|fire_[A-Za-z_0-9]+|[A-Za-z_0-9]*breath[A-Za-z_0-9]*|teleport_swap|teleport_monster|project_hook|py_attack|py_throw|explosion)\s*\(')
FUNC=re.compile(r'\b([A-Za-z_]\w*(?:::\w+)*)\s*\([^;{}]*\)\s*(?:const\s*)?\{')
def blank(m):return ''.join('\n' if c=='\n' else ' ' for c in m.group())

def mask_inactive_constant_branches(clean):
 """Exclude literal #if 0 code, keeping platform-dependent branches visible."""
 stack=[];active=True;result=[]
 for line in clean.splitlines(keepends=True):
  directive=re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)',line)
  if directive:
   kind,value=directive.groups();value=value.strip()
   constant=False if value=='0' else True if value=='1' else None
   if kind in ('if','ifdef','ifndef'):
    parent=active;certain=kind=='if' and constant is True
    stack.append([parent,certain]);active=parent and not (kind=='if' and constant is False)
   elif kind=='elif' and stack:
    parent,certain=stack[-1];active=parent and not certain and constant is not False
    stack[-1][1]=certain or constant is True
   elif kind=='else' and stack:
    parent,certain=stack[-1];active=parent and not certain;stack[-1][1]=True
   elif kind=='endif' and stack:active=stack.pop()[0]
   result.append(line)
  elif active:result.append(line)
  else:result.append(''.join('\n' if c=='\n' else ' ' for c in line))
 return ''.join(result)
def scan(core,root):
 rows=[]
 for p in sorted(root.rglob('*')):
  if p.suffix not in ('.c','.cpp'):continue
  raw=p.read_bytes();s=raw.decode('latin1');clean=mask_inactive_constant_branches(TOKENS.sub(blank,s))
  stack=[];ends={}
  for i,c in enumerate(clean):
   if c=='{':stack.append(i)
   elif c=='}' and stack:ends[stack.pop()]=i
  funcs=[]
  for m in FUNC.finditer(clean):
   if m.group(1) in ('if','for','while','switch','catch'):continue
   start=m.end()-1
   if start in ends:funcs.append((start,ends[start],m.group(1)))
  for m in CALL.finditer(clean):
   owners=[f for f in funcs if f[0]<m.start()<f[1]]
   if not owners:continue
   begin,end,function=min(owners,key=lambda f:f[1]-f[0])
   if function.split('::')[-1]==m.group(1):continue
   cases=list(re.finditer(r'\bcase\s+([^:]+):',clean[begin:m.start()]))
   case=cases[-1].group(1).strip() if cases else ''
   next_calls=[n.group(1) for n in SHAPE.finditer(clean,m.end(),end)]
   rows.append({'id':core+':'+p.relative_to(root).as_posix()+':'+str(s.count('\n',0,m.start())+1),'file':p.relative_to(root).as_posix(),'line':s.count('\n',0,m.start())+1,'function':function,'query':m.group(1),'case':case,'following_calls':list(dict.fromkeys(next_calls))[:30],'sha256':hashlib.sha256(raw).hexdigest()})
 return rows
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('--sources',type=Path,default=ROOT.parent/'roguelike-cores/build-sources');ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
 result={core:scan(core,a.sources/folder) for core,folder in [('heng-tang','src'),('frox','frox/src'),('genso','gensoband/src'),('sil','silq/src')]}
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2),encoding='utf8')
 print(json.dumps({k:{'sites':len(v),'without_following_projection':sum(not r['following_calls'] for r in v),'queries':{n:sum(r['query']==n for r in v) for n in sorted(set(r['query'] for r in v))}} for k,v in result.items()},indent=2))
