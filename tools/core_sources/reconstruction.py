"""Reconstruct files from immutable, verified input ranges and explicit additions.

This format separates copied source bytes from project additions. It does not
certify authorship or grant a license for any addition.
"""
from __future__ import annotations
import difflib
import hashlib
import re
from pathlib import Path
import sys

MAX_FILE = 64 * 1024 * 1024
MAX_OPERATIONS = 200000
VARIANTS = ('raw', 'cp932-c')

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def canonical(data: bytes) -> bytes:
    return data if b'\0' in data else data.removeprefix(b'\xef\xbb\xbf').replace(b'\r\n', b'\n')

def variant(data: bytes, kind: str) -> bytes:
    if kind == 'raw': return data
    if kind == 'cp932-c':
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        from transcode_cp932_src import transcode
        return canonical(transcode(data, source_encoding='cp932')[0])
    raise ValueError('Unsupported source transform: ' + kind)

def integer(value):
    if type(value) is not int or value < 0: raise ValueError('Expected a nonnegative integer')
    return value

def evaluate(spec, inputs, additions):
    """inputs: {key: bytes}; all ranges refer to unchanged input variants."""
    if not isinstance(spec, dict) or not isinstance(spec.get('operations'), list):
        raise ValueError('Invalid reconstruction file')
    if len(spec['operations']) > MAX_OPERATIONS: raise ValueError('Too many reconstruction operations')
    chunks=[];size=0
    for op in spec['operations']:
        if not isinstance(op,list) or not op:raise ValueError('Invalid reconstruction operation')
        if op[0]=='copy' and len(op)==4:
            source=inputs[op[1]];at=integer(op[2]);count=integer(op[3])
            if at>len(source) or count>len(source)-at:raise ValueError('Source range exceeds input')
            part=source[at:at+count]
        elif op[0]=='add' and len(op)==2:
            part=additions[op[1]]
            if not isinstance(part,bytes):raise ValueError('Addition must be bytes')
        else:raise ValueError('Unsupported reconstruction operation')
        size+=len(part)
        if size>MAX_FILE:raise ValueError('Reconstructed file exceeds limit')
        chunks.append(part)
    result=b''.join(chunks)
    if spec.get('newline','keep')=='crlf':result=result.replace(b'\n',b'\r\n')
    elif spec.get('newline','keep')!='keep':raise ValueError('Unsupported output newline transform')
    if spec.get('bom',False):result=b'\xef\xbb\xbf'+result
    if len(result)>MAX_FILE:raise ValueError('Reconstructed file exceeds limit')
    if sha(result)!=spec['sha256']:raise ValueError('Reconstructed source checksum mismatch')
    return result

class Planner:
    """Build copy operations. Insertions remain explicit and auditable.

    Corresponding lines are first matched; changed lines preserve their shared
    prefix/suffix as input ranges. Whole input lines can also be reused from
    another file, but no arbitrary single-byte dictionary is used to encode text.
    """
    def __init__(self, inputs):
        self.inputs=inputs
        self.lines={};self.offsets={};self.lookup={};self.stripped={};self.strings={};self.anchors={}
        self.string_pattern=re.compile(rb'"(?:\\.|[^"\\])*"')
        for key,data in sorted(inputs.items()):
            lines=data.splitlines(keepends=True);offsets=[0]
            for line in lines:offsets.append(offsets[-1]+len(line))
            self.lines[key]=lines;self.offsets[key]=offsets
            for i,line in enumerate(lines):
                if len(line.strip())>=12:
                    self.lookup.setdefault(line,(key,offsets[i],len(line)))
                    lead=len(line)-len(line.lstrip())
                    self.stripped.setdefault(line.strip(),(key,offsets[i]+lead,len(line.strip())))
            for match in self.string_pattern.finditer(data):
                if len(match[0])>=12:self.strings.setdefault(match[0],(key,match.start(),len(match[0])))
        for text,reference in list(self.stripped.items())+list(self.strings.items()):
            minimum=12 if text.startswith(b'"') else 24
            if len(text)>=minimum:self.anchors.setdefault(text[:12],[]).append((text,reference))
        for candidates in self.anchors.values():candidates.sort(key=lambda x:(-len(x[0]),x[1]))
        self.additions={}
    def plan(self, key, target):
        ops=[]
        def copy(source,at,count):
            if not count:return
            if ops and ops[-1][0]=='copy' and ops[-1][1]==source and ops[-1][2]+ops[-1][3]==at:
                ops[-1][3]+=count
            else:ops.append(['copy',source,at,count])
        def emit_add(data):
            if not data:return
            ident=sha(data);self.additions.setdefault(ident,data)
            ops.append(['add',ident])
        def add(data):
            cursor=0;i=0
            while i+12<=len(data):
                match=next(((text,ref) for text,ref in self.anchors.get(data[i:i+12],[]) if data.startswith(text,i)),None)
                if match:
                    text,ref=match;emit_add(data[cursor:i]);copy(*ref);i+=len(text);cursor=i
                else:i+=1
            emit_add(data[cursor:])
        def existing_line(line):
            hit=self.lookup.get(line)
            if hit:copy(*hit);return True
            hit=self.stripped.get(line.strip())
            if hit:
                lead=len(line)-len(line.lstrip());end=len(line.rstrip())
                add(line[:lead]);copy(*hit);add(line[end:]);return True
            return False
        def changed(a,b,at):
            # A changed line may contain multiple insertions. Copy its unchanged
            # interior too, rather than storing the whole middle as new text.
            if a.isascii() and b.isascii() and len(a)+len(b)<=8192:
                for tag,i,j,k,l in difflib.SequenceMatcher(None,a,b,autojunk=False).get_opcodes():
                    if tag=='equal':copy(key,at+i,j-i)
                    elif tag!='delete':add(b[k:l])
                return
            left=0
            while left<min(len(a),len(b)) and a[left]==b[left]:left+=1
            right=0
            while right<min(len(a)-left,len(b)-left) and a[len(a)-1-right]==b[len(b)-1-right]:right+=1
            copy(key,at,left)
            add(b[left:len(b)-right if right else len(b)])
            if right:copy(key,at+len(a)-right,right)
        def inserted(lines):
            pending=[]
            def flush():
                if pending:add(b''.join(pending));pending.clear()
            for line in lines:
                if line in self.lookup or line.strip() in self.stripped:
                    flush();existing_line(line)
                else:pending.append(line)
            flush()
        a=self.lines.get(key,[]);b=target.splitlines(keepends=True);offsets=self.offsets.get(key,[0])
        for tag,i,j,k,l in difflib.SequenceMatcher(None,a,b,autojunk=False).get_opcodes():
            if tag=='equal':copy(key,offsets[i],offsets[j]-offsets[i])
            elif tag=='delete':continue
            elif tag=='replace' and j-i==l-k:
                for ai,bi in zip(range(i,j),range(k,l)):
                    if not existing_line(b[bi]):changed(a[ai],b[bi],offsets[ai])
            else:inserted(b[k:l])
        spec={'sha256':sha(target),'operations':ops}
        assert evaluate(spec,self.inputs,self.additions)==target
        return spec
