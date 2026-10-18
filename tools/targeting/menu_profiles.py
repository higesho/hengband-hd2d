"""Compile data-only spell menu previews before any original action preamble.

Unrecognized callbacks return immediately in the private mode. A menu must
observe their capability marker during a normal name/info call before invoking
that mode, so an uninstrumented callback is never executed speculatively.
"""
import re
from profile_inventory import Source,PROJECT,CALL,conditions_conflict
from pure_expressions import NumericScope
from projection_helpers import literal_flags,resolve_flags

SIGNATURE=re.compile(r'\b(?:static\s+)?(void|cptr)\s+(\w+)\s*\(([^;{}]*)\)\s*\{')
HENG_SIGNATURE=re.compile(r'\b(tl::optional<std::string>)\s+(\w+)\s*\(([^;{}]*)\)\s*\{')

def self_effect(source,start,end,owner,core,helpers):
    calls=list(PROJECT.finditer(source.mask,start,end))
    matcher=helpers.matcher() if helpers else None
    if matcher:
        existing={c.start() for c in calls}
        calls.extend(c for c in matcher.finditer(source.mask,start,end) if c.start() not in existing)
    queries=list(CALL.finditer(source.mask,start,end));plans=[]
    for call in calls:
        conditions=source.conditions(call.start(),owner)
        # Multiple projections on the same execution path need a composite
        # model. Disjoint aimed/self branches retain their native condition.
        if any(not conditions_conflict(conditions,source.conditions(other.start(),owner)) for other in calls+queries if other.start()!=call.start()):continue
        if re.search(r'\b(?:for|while|do)\b',source.mask[start:call.start()]):continue
        code=self_effect_call(source,call,owner,core,helpers)
        if not code:continue
        numeric=NumericScope(source,call.start(),owner,core);guards=[]
        for condition in conditions:
            if re.sub(r'\s+','',condition) in ('cast','cmd==SPELL_CAST'):continue
            guard=numeric.expand(condition)
            if guard is None:break
            guards.append('('+guard+')')
        else:
            plans.append('('+ '&&'.join(guards)+'?('+code+',0):0)' if guards else code)
    return ', '.join(plans) if plans else None

def self_effect_call(source,call,owner,core,helpers,numeric=None):
    from profiles import ELEMENTS,effect_code,heng_effect_code,effect_kind
    name=call.group(1);args=source.args(call.end()-1)
    expanded=helpers.expand(name,args) if helpers else None
    if expanded:name,args=expanded
    numeric=numeric or NumericScope(source,call.start(),owner,core)
    if name=='native_self_scatter':
        from self_profiles import scatter_code
        return scatter_code(args,core)
    if name in ('lite_area','unlite_area'):
        if core=='heng':
            if len(args)!=3 or args[0]!='player_ptr':return None
            radius=numeric.expand(args[2]);element='AttributeType::LITE_WEAK' if name=='lite_area' else 'AttributeType::DARK_WEAK'
            value='static_cast<int>('+element+')'
        else:
            if len(args)!=2:return None
            radius=numeric.expand(args[1]);element='GF_LITE_WEAK' if name=='lite_area' else 'GF_DARK_WEAK';value=element
        if radius is None:return None
        dark_guard=-1 if name=='lite_area' else 0
        return (f'hb_target_profile_effect(NATIVE_SHAPE_BALL,{value},{radius},0,PROJECT_GRID|PROJECT_KILL,0,0,{dark_guard}),'
            +f'hb_target_profile_effect(NATIVE_SHAPE_ROOM_AREA,{value},0,0,PROJECT_GRID,0,{1 if name=="lite_area" else 0},{dark_guard})')
    if core=='genso' and name=='project_hack2' and len(args)==4:
        # This native helper freezes its full footprint before changing any
        # terrain. Element and damage rolls do not change that footprint.
        element=args[0]
        if element=='0' or re.fullmatch(r'GF_[A-Z0-9_]+',element):
            square='1' if element=='GF_KINKAKUJI' else '0'
            return f'hb_target_profile_effect(NATIVE_SHAPE_LOS,0,0,0,PROJECT_GRID|PROJECT_ITEM|PROJECT_KILL,0,{square},1)'
    if name=='earthquake':
        radius=None
        if core=='heng' and len(args) in (3,4) and args[1]=='player_ptr->get_position()' and (len(args)==3 or args[3]=='0'):
            radius=numeric.expand(args[2])
        elif core in ('frox','genso','toband') and len(args)==3 and args[:2]==['py','px']:
            radius=numeric.expand(args[2])
        if radius is not None:return f'hb_target_profile_effect(NATIVE_SHAPE_QUAKE,0,{radius},0,0,1,0,0)'
    if name=='destroy_area':
        expression=None
        if core=='heng' and len(args)==5 and args[1:3]==['player_ptr->y','player_ptr->x'] and args[4]=='false':expression=args[3]
        elif core=='frox' and len(args)==4 and args[:2]==['py','px']:expression=args[2]
        elif core=='genso' and len(args)==6 and args[:2]==['py','px'] and args[3:] in (['FALSE','FALSE','FALSE'],['FALSE','TRUE','FALSE']):expression=args[2]
        elif core=='toband' and len(args)==3 and args[:2]==['py','px']:expression=args[2]
        if expression is not None:
            radius=numeric.expand(expression)
            if radius is not None:return f'hb_target_profile_effect(NATIVE_SHAPE_DESTRUCTION,0,{radius},0,0,0,0,0)'
            spread=re.fullmatch(r'(.+)\+\s*randint0\((\d+)\)',expression)
            if spread and 1<int(spread.group(2))<=100:
                base=numeric.expand(spread.group(1))
                if base is not None:
                    return f'hb_target_profile_effect(NATIVE_SHAPE_DESTRUCTION,0,{base},0,0,0,0,0), hb_target_profile_effect(NATIVE_SHAPE_DESTRUCTION,0,({base})+{int(spread.group(2))-1},0,0,1,0,0)'
    if name==('project_all_los' if core=='heng' else 'project_hack') and len(args)==(3 if core=='heng' else 2):
        element=args[1] if core=='heng' else args[0]
        if element.removeprefix('AttributeType::').removeprefix('GF_') not in ELEMENTS:return None
        value='static_cast<int>('+element+')' if core=='heng' else element
        return f'hb_target_profile_effect(NATIVE_SHAPE_LOS,{value},0,0,PROJECT_JUMP|PROJECT_KILL|PROJECT_HIDE,0,0,0)'+effect_kind(element)
    if core=='heng':
        if name in ('fire_ball','fire_ball_hide') and len(args)==5 and args[2]=='Direction::self()':
            radius=numeric.expand(args[4])
            if radius is None:return None
            args=list(args);args[4]=radius
            return heng_effect_code(name,args,numeric)
        if name=='project' and len(args)==8 and args[1]=='0' and args[3:5]==['player_ptr->y','player_ptr->x']:
            element=args[6];radius=numeric.expand(args[2]);flags=resolve_flags(args[7],numeric)
            if element.removeprefix('AttributeType::') not in ELEMENTS or radius is None or flags is None:return None
            return f'hb_target_profile_effect(NATIVE_SHAPE_BALL,static_cast<int>({element}),{radius},0,{flags},0,0,0)'+effect_kind(element)
        return None
    if name in ('fire_ball','fire_ball_aux','fire_ball_hide') and len(args)>=4 and args[1].strip()=='0':
        radius=numeric.expand(args[3])
        if radius is None:return None
        args=list(args);args[3]=radius
        return effect_code(name,args,lambda value:numeric(value),core=core if core=='toband' else None)
    toband_project=core=='toband' and len(args)==8 and args[7].startswith('MODIFY_ELEM_MODE_')
    if name=='project' and (len(args)==7 or core=='genso' and len(args)==8 and args[7]=='-1' or toband_project) and args[0]=='0' and args[2:4]==['py','px']:
        element=args[5];radius=numeric.expand(args[1]);flags=resolve_flags(args[6],numeric)
        if not element.startswith('GF_') or element[3:] not in ELEMENTS or radius is None or flags is None:return None
        return f'hb_target_profile_effect(NATIVE_SHAPE_BALL,{element},{radius},0,{flags},0,0,0)'+effect_kind(element)
    return None

def function_regions(source,owner,parameter):
    start,end,_=owner
    switches=list(re.finditer(r'\bswitch\s*\(\s*'+re.escape(parameter)+r'\s*\)\s*\{',source.mask[start:end]))
    if not switches:return []
    opening=start+switches[0].end()-1
    cases=[c for c in source.cases if opening<c.start()<source.ends[opening] and source.block(c.start())==opening]
    result=[];labels=[]
    for i,case in enumerate(cases):
        label=case.group().strip()
        if not label.startswith('case '):continue
        labels.append(label[5:-1].strip())
        stop=cases[i+1].start() if i+1<len(cases) else source.ends[opening]
        if not source.mask[case.end():stop].strip():continue
        result.append((labels,case.end(),stop));labels=[]
    return result

def injections(source,core,helpers,path_name=''):
    if core not in ('frox','genso','heng','toband'):return [],[]
    edits=[];audit=[];books=[]
    for match in (HENG_SIGNATURE if core=='heng' else SIGNATURE).finditer(source.mask):
        returned,name,parameters=match.groups()
        args=[arg.strip() for arg in parameters.split(',')]
        callback=core=='frox' and returned=='void' and len(args)==2 and re.fullmatch(r'int\s+cmd',args[0]) and re.fullmatch(r'(?:variant\s*\*|var_ptr\s+)\s*res',args[1])
        book=returned=='cptr' and len(args)==2 and re.fullmatch(r'int\s+spell',args[0]) and re.fullmatch(r'int\s+mode',args[1])
        if core=='heng':book=len(args)==3 and args[0]=='PlayerType *player_ptr' and re.fullmatch(r'(?:SPELL_IDX|spell_hex_type)\s+spell(?:_id)?',args[1]) and args[2]=='SpellProcessType mode'
        if not callback and not book:continue
        if match.end()-1 not in source.ends:raise ValueError('Unbalanced native spell body: '+core+':'+path_name+':'+name)
        owner=(match.end()-1,source.ends[match.end()-1],name)
        spell_parameter=args[1].split()[-1] if core=='heng' else 'spell'
        mode='cmd' if callback else 'static_cast<int>(mode)' if core=='heng' else 'mode';site='menu:'+core+':'+path_name+':'+name
        symbol='hb_target_plan_key('+name+')' if core=='heng' else '(native_target_plan_symbol)'+name
        code='\n    hb_target_mark_plan('+symbol+');\n    if('+mode+'==HB_TARGET_PLAN_MODE) {\n        hb_target_profile_begin("'+site+'");\n'
        entries=[]
        if callback:
            regions=[r for r in function_regions(source,owner,'cmd') if 'SPELL_CAST' in r[0]]
            effect=self_effect(source,regions[0][1],regions[0][2],owner,core,helpers) if len(regions)==1 else None
            if effect:code+='        '+effect+';\n'
            entries.append({'case':'SPELL_CAST','profile':effect})
        else:
            books.append(name);body=''
            for labels,start,end in function_regions(source,owner,spell_parameter):
                effect=self_effect(source,start,end,owner,core,helpers)
                entries.extend({'case':label,'profile':effect} for label in labels)
                if effect:
                    for label in labels:body+='        case '+label+':\n'
                    body+='            '+effect+'; break;\n'
            if body:code+='        switch('+spell_parameter+') {\n'+body+'        default: break;\n        }\n'
        code+='        return'+(' std::string{}' if core=='heng' else ' ""' if book else '')+';\n    }\n'
        edits.append((owner[0]+1,owner[0]+1,code))
        audit.append({'site':site,'core':core,'function':name,'callback':bool(callback),'book':bool(book),'cases':entries})
    # The realm dispatcher also bypasses its ordinary global/preamble writes.
    # TOband の do_spell() は仲間の呪文の対象の切り替えを包んだ薄い関数で、領域の振り分けは中の do_spell_aux()（static）にある。
    # 計画の合図の記号は、メニュー側（tb_targeting_menu.inl）が見る外向きの do_spell で出す。
    dispatcher=next((f for f in source.functions if f[2]==('exe_spell' if core=='heng' else 'do_spell_aux' if core=='toband' else 'do_spell')),None)
    if dispatcher:
        if core!='frox':
            name='do_spell' if core=='toband' else dispatcher[2]
            dispatch=source.mask[dispatcher[0]+1:dispatcher[1]]
            # Only direct native realm calls enter the private mode. The Genso
            # bard error branch and invalid-realm messages remain normal-only.
            calls=re.findall(r'case ([\w:]+):\s*return (\w+)\(([^;]+)\);',dispatch)
            if not calls:raise ValueError(core+' realm dispatcher changed')
            compare='static_cast<int>(mode)' if core=='heng' else 'mode'
            symbol='hb_target_plan_key('+name+')' if core=='heng' else '(native_target_plan_symbol)'+name
            code='\n    hb_target_mark_plan('+symbol+');\n    if('+compare+'==HB_TARGET_PLAN_MODE) {\n        switch(realm) {\n'
            required=[]
            for realm,function,arguments in calls:
                code+='        case '+realm+': return '+function+'('+arguments+');\n';required.append(function)
            if core=='genso':
                if not re.search(r'CLASS_BARD\)\s*return\s+do_new_spell_music2\(spell,\s*mode\)',source.text):raise ValueError('Genso music dispatcher changed')
                code+='        case TV_BOOK_MUSIC: if(p_ptr->pclass!=CLASS_BARD)return do_new_spell_music2(spell,mode); break;\n';required.append('do_new_spell_music2')
            code+='        default: break;\n        }\n        return '+('std::string{}' if core=='heng' else '""')+';\n    }\n'
            edits.append((dispatcher[0]+1,dispatcher[0]+1,code))
            audit.append({'site':'menu:'+core+':'+path_name+':'+name,'core':core,'function':name,'dispatcher_requires':required})
            return edits,audit
        dispatch=source.text[dispatcher[0]+1:dispatcher[1]]
        calls=re.findall(r'case (REALM_\w+):\s*result = (\w+)\(spell, mode\);\s*break;',dispatch)
        if not calls:raise ValueError('Frox realm dispatcher changed')
        code='\n    hb_target_mark_plan((native_target_plan_symbol)do_spell);\n    if(mode==HB_TARGET_PLAN_MODE) {\n        switch(realm) {\n'
        for realm,function in calls:code+='        case '+realm+': return '+function+'(spell,mode);\n'
        code+='        }\n        return "";\n    }\n'
        edits.append((dispatcher[0]+1,dispatcher[0]+1,code))
        audit.append({'site':'menu:frox:'+path_name+':do_spell','core':core,'function':'do_spell','dispatcher_requires':[function for realm,function in calls]})
    return edits,audit
