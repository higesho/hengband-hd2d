"""Recognize deterministic numeric inputs available before a direction query.

Only reviewed player fields, arithmetic and immutable local aliases qualify.
An expression evaluated by the original action after input is not hoisted.
"""
import re

DECLARATION=re.compile(r'\b(?:const\s+)?(?:int|s16b|s32b|byte|auto|POSITION|PERCENTAGE|PLAYER_LEVEL)\s+(\w+)\s*=\s*([^;{}]+);')
LOCAL_DECLARATIONS=re.compile(r'\b(?:const\s+)?(?:int|s16b|s32b|byte|auto|POSITION|PERCENTAGE|PLAYER_LEVEL)\s+([^;{}]+);')
ASSIGNMENT=re.compile(r'(?<![\w>.])\b(\w+)\s*=(?!=)\s*([^;{}]+);')
FIELDS=re.compile(r'\b(?:p_ptr|player_ptr)\s*->\s*(?:lev|pclass|chp|mhp|csp|msp)\b')
# TOband は呪文の技量と能力値を party_spell_caster()（仲間が唱える間はその人）から読む。読むだけの関数で、フィールドを返す。
FIELDS_TOBAND=re.compile(r'\b(?:p_ptr|player_ptr)\s*->\s*(?:lev|pclass|chp|mhp|csp|msp)\b|\bparty_spell_caster\s*\(\s*\)\s*->\s*(?:magic_exp|stat_use)\s*\[[^\]]*\]')
# These native display functions take their numeric inputs by value. Their
# signatures are in cmd-action/cmd-spell.h, tracked with the source overlay.
HENG_VALUE_CALLS={'info_damage','info_range','info_power','info_radius'}

class NumericScope:
    def __init__(self,source,query,owner,core,until=None,known_inputs=frozenset(),literal_prefixes=()):
        self.source=source;self.query=query;self.owner=owner;self.core=core;self.declarations=None;self.path_cache={}
        self.query_conditions=source.conditions(query,owner)
        self.until=query if until is None else until
        self.known_inputs=known_inputs
        self.literal_prefixes=literal_prefixes
        self.calls={'MIN','MAX','ABS'}
        self.fields=FIELDS_TOBAND if core=='toband' else FIELDS
        if core=='frox':self.calls.update(('spell_power','device_power','spell_cap','beam_chance'))
        if core=='genso':self.calls.update(('calc_spell_caster_level','beam_chance'))

    def __call__(self,expression,seen=frozenset()):
        if '++' in expression or '--' in expression or re.search(r'(?<![=!<>])=(?!=)',expression):return False
        tail=re.sub(r'\s+','',self.source.mask[self.query:self.until])
        for field in self.fields.finditer(expression):
            name=re.escape(re.sub(r'\s+','',field.group()))
            if re.search(name+r'(?:\+\+|--|[+*/%&|^\-]?=(?!=))|(?:\+\+|--|&)'+name,tail):return False
        text=self.fields.sub('0',expression)
        if self.core=='heng':
            text=text.replace('std::min','MIN').replace('std::max','MAX').replace('std::abs','ABS')
            # spells-diceroll.cpp reads only the player's class and level.
            text=re.sub(r'\bbeam_chance\s*\(\s*player_ptr\s*\)','0',text)
        for name in re.findall(r'\b(\w+)\s*\(',text):
            if name not in self.calls:return False
        for name in set(re.findall(r'\b[A-Za-z_]\w*\b',text)):
            if name in self.calls or name in self.known_inputs or name in ('MAX_RANGE','TRUE','FALSE','true','false','int') or re.fullmatch(r'CLASS_[A-Z0-9_]+',name):continue
            if any(name.startswith(prefix) and re.fullmatch(r'[A-Z0-9_]+',name) for prefix in self.literal_prefixes):continue
            if name in seen or not self.immutable_local(name,seen|{name}):return False
        text=re.sub(r'\b[A-Za-z_]\w*\b','0',text)
        return not re.search(r'[^\d\s()+\-*/%?:<>=!&|^~,]',text)

    def immutable_local(self,name,seen):
        match=self.local_declaration(name)
        if match is None:return False
        rest=self.source.mask[match.end():self.until]
        modifications=(r'\b'+re.escape(name)+r'\s*(?:\+\+|--|(?:[+*/%&|^\-]|<<|>>)?=(?!=))',
            r'(?:\+\+|--)\s*\b'+re.escape(name)+r'\b',r'&\s*\b'+re.escape(name)+r'\b')
        for pattern in modifications:
            if any(self.may_reach_query(match.end()+m.start()) for m in re.finditer(pattern,rest)):return False
        if self.core=='heng' and not match.group().startswith('const '):
            # A C++ call can change a non-const local through an implicit
            # reference. Accept only known value calls and arithmetic here.
            for call in re.compile(r'\b(\w+)\s*\(').finditer(self.source.mask,match.end(),self.until):
                closing=self.source.ends.get(call.end()-1,call.end()-1)
                if closing>=self.until:continue
                if not self.may_reach_query(call.start()):continue
                if not re.search(r'\b'+re.escape(name)+r'\b',self.source.mask[call.end():closing]):continue
                if call.group(1) not in self.calls|HENG_VALUE_CALLS|{'if','switch'}:return False
        return self(match.group(2),seen)

    def may_reach_query(self,position):
        if self.query<=position<self.until:
            from profile_inventory import conditions_conflict
            if conditions_conflict(self.source.conditions(position,self.owner),self.source.conditions(self.until,self.owner)):return False
        region=self.source.region(position)
        if region and region[2] and region[1]<self.query:
            body=self.source.mask[region[0]:region[1]]
            if re.search(r'\b(?:break|return\b[^;]*)\s*;\s*\}*\s*$',body):return False
        # A conditional write before the prompt can still change the value.
        return True

    def on_path(self,position):
        if position in self.path_cache:return self.path_cache[position]
        block=self.source.block(position);region=self.source.region(position)
        valid=block>=0 and self.source.ends[block]>self.query
        if valid and region and region[2]:valid=region[0]<=self.query<region[1]
        if valid:valid=all(c in self.query_conditions for c in self.source.conditions(position,self.owner))
        if valid:
            # A completed loop need not have run at all. Its assignment is not
            # a dominating definition for a later prompt.
            for loop in re.compile(r'\b(?:for|while)\s*\(').finditer(self.source.mask,self.owner[0],position):
                opening=loop.end()-1;closing=self.source.ends.get(opening,opening)
                end=self.source.statement_end(closing+1)
                if loop.start()<=position<end and not loop.start()<=self.query<end:valid=False;break
        self.path_cache[position]=valid;return valid

    def local_declaration(self,name):
        if self.declarations is None:
            self.declarations=[]
            declarations=list(DECLARATION.finditer(self.source.mask,self.owner[0],self.query))
            for match in declarations:
                block=self.source.block(match.start())
                if self.on_path(match.start()):self.declarations.append((block,match))
            locals={}
            for declaration in LOCAL_DECLARATIONS.finditer(self.source.mask,self.owner[0],self.query):
                if not self.on_path(declaration.start()):continue
                for part in declaration.group(1).split(','):
                    declared=re.match(r'\s*(\w+)\s*(?:=|$)',part)
                    if declared:locals.setdefault(declared.group(1),[]).append(declaration)
            for assignment in ASSIGNMENT.finditer(self.source.mask,self.owner[0],self.query):
                if assignment.group(1) not in locals or not self.on_path(assignment.start()):continue
                if any(d.start()<=assignment.start()<d.end() for d in declarations):continue
                if not any(d.end()<=assignment.start() for d in locals[assignment.group(1)]):continue
                self.declarations.append((self.source.block(assignment.start()),assignment))
        matches=[entry for entry in self.declarations if entry[1].group(1)==name]
        if not matches:return None
        block,match=max(matches,key=lambda entry:(entry[0],entry[1].start()))
        return match

    def expand(self,expression):
        if not self(expression):return None
        fields=list(self.fields.finditer(expression))
        def replace(match):
            if any(field.start()<=match.start()<field.end() for field in fields):return match.group()
            declaration=self.local_declaration(match.group())
            if declaration is None:return match.group()
            expanded=self.expand(declaration.group(2))
            if expanded is None:raise ValueError('Previously validated numeric alias changed: '+match.group())
            return '('+expanded+')'
        return re.sub(r'\b[A-Za-z_]\w*\b',replace,expression)
