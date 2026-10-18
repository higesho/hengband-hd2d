"""Read simple native projection wrappers without executing their gameplay code.

Only wrappers with literal flags, an unconditional projection and unchanged
arguments qualify. Damage expressions are passed to the metadata compiler,
which deliberately never evaluates them. Every derived wrapper keeps its source.
"""
from pathlib import Path
import re
from profile_inventory import Source,PROJECT

FLAGS=re.compile(r'\b(?:const\s+)?(?:int|u32b|BIT_FLAGS)\s+(\w+)\s*=\s*([^;]+);')
ROOTS={'project_hook','project_hack','project_all_los','fire_ball','fire_ball_hide','fire_bolt','fire_beam'}

def literal_flags(value):
    names=re.findall(r'\b[A-Za-z_]\w*\b',value)
    return bool(names) and all(n.startswith('PROJECT_') for n in names) and not re.search(r'[^\s|()0-9]',re.sub(r'PROJECT_\w+','',value))

def resolve_flags(value,numeric):
    value=re.sub(r'/\*.*?\*/|//[^\r\n]*',' ',value,flags=re.S)
    if literal_flags(value):return value
    from pure_expressions import NumericScope
    if not isinstance(numeric,NumericScope):return None
    scope=NumericScope(numeric.source,numeric.query,numeric.owner,numeric.core,numeric.until,numeric.known_inputs,('PROJECT_',))
    expanded=scope.expand(value)
    return expanded if expanded is not None and literal_flags(expanded) else None

class ProjectionHelpers:
    def __init__(self):self.entries={}

    def read(self,path):
        source=Source(path)
        for owner in source.functions:
            start,end,name=owner
            if name in ROOTS:continue
            calls=[call for call in PROJECT.finditer(source.mask,start+1,end) if call.group(1) in ROOTS]
            if len(calls)!=1:continue
            call=calls[0]
            prefix=source.mask[start+1:call.start()]
            if re.search(r'\b(?:if|else|switch|for|while|do)\b',prefix):continue
            aliases={m.group(1):m.group(2).strip() for m in FLAGS.finditer(prefix) if literal_flags(m.group(2))}
            rest=prefix
            for match in reversed(list(FLAGS.finditer(prefix))):
                if match.group(1) in aliases:rest=rest[:match.start()]+rest[match.end():]
            # An initializer may receive the returned result; everything else
            # before the call must be flags and the return expression.
            rest=re.sub(r'\b(?:bool|int)\s+\w+\s*=\s*','',rest)
            if re.sub(r'\breturn\b|[\s()]','',rest):continue
            declarations=[m for m in re.finditer(r'\b'+re.escape(name)+r'\s*\(',source.mask[:start]) if source.ends.get(m.end()-1,-1)<start]
            if not declarations:continue
            declaration=declarations[-1]
            parameters=[]
            for arg in source.args(declaration.end()-1):
                match=re.search(r'(\w+)\s*$',arg)
                if not match:break
                parameters.append(match.group(1))
            else:
                arguments=source.args(call.end()-1)
                arguments=[re.sub(r'\b[A-Za-z_]\w*\b',lambda m:'('+aliases[m.group()]+')' if m.group() in aliases else m.group(),arg) for arg in arguments]
                entry={'function':call.group(1),'arguments':arguments,'parameters':parameters,'source':str(path),'line':source.text.count('\n',0,start)+1}
                if name in self.entries and self.entries[name]!=entry:raise ValueError('Ambiguous projection wrapper: '+name)
                self.entries[name]=entry

    def expand(self,name,arguments,seen=frozenset()):
        if name in seen:raise ValueError('Recursive projection helper: '+name)
        entry=self.entries.get(name)
        if not entry or len(arguments)!=len(entry['parameters']):return None
        values=dict(zip(entry['parameters'],arguments))
        expanded=[re.sub(r'\b[A-Za-z_]\w*\b',lambda m:values.get(m.group(),m.group()),arg) for arg in entry['arguments']]
        return self.expand(entry['function'],expanded,seen|{name}) or (entry['function'],expanded)

    def matcher(self):
        return re.compile(r'\b('+ '|'.join(sorted(self.entries,key=len,reverse=True))+r')\s*\(') if self.entries else None

def load_helpers(root,core):
    helpers=ProjectionHelpers()
    spells2={'frox':'frox/src/spells2.c','toband':'toband/src/spells2.c'}.get(core,'gensoband/src/spells2.c')
    paths=sorted((root/'src/spell-kind').glob('*.cpp')) if core=='heng' else [root/spells2]
    if core=='heng':paths.append(root/'src/spell/spells-status.cpp')
    for path in paths:helpers.read(path)
    if core=='heng':
        from self_profiles import register_heng
        register_heng(helpers,root)
    return helpers
