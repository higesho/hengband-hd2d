"""Extract scoped projection calls for the targeting coverage audit.

This is a source inventory, not permission to guess an unknown effect. The
result records the original expressions and branch conditions for review.
"""
from pathlib import Path
import argparse,json,re,sys
from inventory import TOKENS,blank,FUNC,CALL,mask_inactive_constant_branches
PROJECT=re.compile(r'\b(fire_[A-Za-z_0-9]+|project(?:_hook|_hack2?|_hacks|_all_los|_arc)?|lite_area|unlite_area|lite_line|light_line|explosion|breath_shape|dispel_[A-Za-z_0-9]+|earthquake|destroy_area|py_attack|do_cmd_attack|py_throw|teleport_swap|rush_attack)\s*\(')
CONTROL={'if','for','while','switch','catch'}
def conditions_conflict(first,second):
 compact=lambda value:re.sub(r'\s+','',value)
 a=[compact(value) for value in first];b=[compact(value) for value in second]
 return any(x=='!('+y+')' or y=='!('+x+')' for x in a for y in b)

class Source:
 def __init__(self,p):
  self.text=p.read_bytes().decode('latin1');self.mask=mask_inactive_constant_branches(TOKENS.sub(blank,self.text))
  self.mask=re.sub(r'^[ \t]*#(?:[^\n]*\\\r?\n)*[^\n]*',blank,self.mask,flags=re.M)
  self.ends={};self.parent={};stack=[]
  for i,c in enumerate(self.mask):
   if c in '({[':
    self.parent[i]=stack[-1] if stack else -1;stack.append(i)
   elif c in ')}]' and stack:self.ends[stack.pop()]=i
  self.functions=[]
  for m in FUNC.finditer(self.mask):
   at=m.end()-1
   if m.group(1) not in CONTROL and at in self.ends:self.functions.append((at,self.ends[at],m.group(1)))
  self.cases=list(re.finditer(r'\b(?:case\s+(?:[^:;{}]|::)+|default)\s*:',self.mask))
  self.block_cache={}
 def owner(self,pos):
  f=[f for f in self.functions if f[0]<pos<f[1]]
  return min(f,key=lambda f:f[1]-f[0]) if f else None
 def block(self,pos):
  if pos in self.block_cache:return self.block_cache[pos]
  b=[i for i,end in self.ends.items() if self.mask[i]=='{' and i<pos<end]
  result=max(b) if b else -1;self.block_cache[pos]=result;return result
 def region(self,pos):
  f=self.owner(pos)
  if not f:return None
  scopes=[]
  for c in self.cases:
   if c.start()<=f[0] or c.start()>=pos:continue
   block=self.block(c.start())
   if block>=0 and self.ends[block]>pos:scopes.append((block,c))
  if scopes:
   block,case=max(scopes,key=lambda v:(v[0],v[1].start()))
   following=[c.start() for c in self.cases if c.start()>case.start() and self.block(c.start())==block]
   return case.start(),min(following+[self.ends[block]]),case.group().strip()
  return f[0]+1,f[1],''
 def args(self,open):
  end=self.ends.get(open,open);parts=[];begin=open+1;i=begin
  while i<end:
   if self.mask[i] in '([{':i=self.ends.get(i,i)
   elif self.mask[i]==',':parts.append(self.text[begin:i].strip());begin=i+1
   i+=1
  if begin<end:parts.append(self.text[begin:end].strip())
  return parts
 def statement_end(self,start):
  while start<len(self.mask) and self.mask[start].isspace():start+=1
  if start>=len(self.mask):return start
  if self.mask[start]=='{':return self.ends[start]+1
  control=re.match(r'(if|for|while|switch)\s*(?:constexpr\s*)?\(',self.mask[start:])
  if control:
   opening=start+control.end()-1
   stop=self.statement_end(self.ends.get(opening,opening)+1)
   if control.group(1)=='if':
    tail=stop
    while tail<len(self.mask) and self.mask[tail].isspace():tail+=1
    if re.match(r'else\b',self.mask[tail:]):stop=self.statement_end(tail+4)
   return stop
  if re.match(r'do\b',self.mask[start:]):
   stop=self.statement_end(start+2)
   tail=re.match(r'\s*while\s*\(',self.mask[stop:])
   if tail:
    opening=stop+tail.end()-1
    return self.statement_end(self.ends.get(opening,opening)+1)
   return stop
  i=start
  while i<len(self.mask):
   if self.mask[i] in '([{':i=self.ends.get(i,i)
   elif self.mask[i]==';':return i+1
   i+=1
  return i
 def conditions(self,pos,f):
  result=[]
  for m in re.finditer(r'\bif\s*\(',self.mask[f[0]:pos]):
   open=f[0]+m.end()-1;end=self.ends.get(open,open);start=end+1
   while start<len(self.mask) and self.mask[start].isspace():start+=1
   stop=self.statement_end(start);condition=self.text[open+1:end].strip()
   if start<=pos<stop:result.append(condition);continue
   tail=stop
   while tail<len(self.mask) and self.mask[tail].isspace():tail+=1
   if self.mask.startswith('else',tail):
    begin=tail+4;finish=self.statement_end(begin)
    if begin<=pos<finish:result.append('!('+condition+')')
  return result

def audit(root):
 out=[]
 for p in sorted(root.rglob('*')):
  if p.suffix not in ('.c','.cpp'):continue
  source=Source(p)
  for m in CALL.finditer(source.mask):
   f=source.owner(m.start());region=source.region(m.start())
   if not f or not region or f[2].split('::')[-1]==m.group(1):continue
   projections=[]
   for hit in PROJECT.finditer(source.mask,m.end(),region[1]):
    projections.append({'function':hit.group(1),'arguments':source.args(hit.end()-1),'line':source.text.count('\n',0,hit.start())+1,'conditions':source.conditions(hit.start(),f)})
   out.append({'file':p.relative_to(root).as_posix(),'line':source.text.count('\n',0,m.start())+1,'function':f[2],'query':m.group(1),'query_arguments':source.args(m.end()-1),'case':region[2],'projections':projections})
 return out
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('--sources',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
 result={core:audit(a.sources/path) for core,path in [('heng-tang','src'),('frox','frox/src'),('genso','gensoband/src'),('sil','silq/src')]}
 a.output.write_text(json.dumps(result,indent=2),encoding='utf8')
 print(json.dumps({core:{'input_sites':len(rows),'with_scoped_projection':sum(bool(r['projections']) for r in rows),'projection_functions':sorted(set(p['function'] for r in rows for p in r['projections']))} for core,rows in result.items()},indent=2))
