"""Source-derived, non-aimed spatial helpers with reviewed side effects."""
import re
from profile_inventory import Source,PROJECT
from projection_helpers import literal_flags

def scatter_plan(source,start,end,owner,core):
    text=source.mask[start:end]
    calls=list(re.compile(r'\bscatter\s*\(').finditer(source.mask,start,end))
    projects=[c for c in PROJECT.finditer(source.mask,start,end) if c.group(1)=='project']
    if len(calls)!=1 or len(projects)!=1:return None
    if not re.search(r'\bnum\s*=\s*(?:Dice::roll|damroll)\(5,\s*3\)',text):return None
    if not re.search(r'for\s*\(\s*(?:auto\s+)?k\s*=\s*0\s*;\s*k\s*<\s*num\s*;\s*k\+\+\s*\)',text):return None
    if not re.search(r'\battempts\s*=\s*1000\s*;',text) or not re.search(r'while\s*\(attempts--\)',text):return None
    args=source.args(calls[0].end()-1);project=source.args(projects[0].end()-1)
    if core=='heng':
        if args!=['*player_ptr->current_floor_ptr','p_pos','4','PROJECT_LOS']:return None
        if len(project)!=8 or project[:5]!=['player_ptr','0','0','pos.y','pos.x']:return None
        if project[6]!='AttributeType::LITE_WEAK':return None
        flags=project[7]
        if flags=='flags':
            match=re.search(r'constexpr\s+uint\s+flags\s*=\s*([^;]+);',text)
            if not match:return None
            flags=match.group(1)
        if not literal_flags(flags):return None
        return ['AttributeType::LITE_WEAK','4',flags,'1']
    if args!=['&y','&x','py','px','4','0']:return None
    if len(project)!=(7 if core=='frox' else 8) or project[:4]!=['0','0','y','x'] or project[5]!='GF_LITE_WEAK':return None
    if core=='genso' and project[7]!='-1':return None
    if core=='toband' and not project[7].startswith('MODIFY_ELEM_MODE_'):return None
    if not literal_flags(project[6]):return None
    # 最後の 1 が、scatter() が視線（los）を要求するかどうか。幻想蛮怒は projectable、TOband は los()（cave.c の scatter）。
    return ['GF_LITE_WEAK','4',project[6],'1' if core=='toband' else '0']

def scatter_code(args,core):
    element,radius,flags,los=args
    if core=='heng':element='static_cast<int>('+element+')'
    return f'hb_target_profile_effect(NATIVE_SHAPE_SELF_SCATTER,{element},{radius},0,{flags},1,{los},0)'

def register_heng(helpers,root):
    paths={'spell/spells-staff-only.cpp':{
        'cleansing_nova':{'dispel_evil','randint1','improvement','bss','set_protection','mod_protection','has_effect','set_poison','set_fear','hp_player','set_stun','set_cut','if'},
        'unleash_mana_storm':{'msg_print','_F','project','randint1','PlayerClass','is_wizard','take_hit','_','if'}},
        'spell-kind/spells-lite.cpp':{'starlight':None}}
    for relative,names in paths.items():
        source=Source(root/'src'/relative)
        for name,allowed in names.items():
            owner=next((f for f in source.functions if f[2]==name),None)
            if owner is None:raise ValueError('Missing reviewed self effect: '+name)
            start,end,_=owner
            declaration=list(re.finditer(r'\b'+name+r'\s*\(',source.mask[:start]))[-1]
            parameters=[re.search(r'(\w+)\s*$',a).group(1) for a in source.args(declaration.end()-1)]
            if name=='starlight':
                arguments=scatter_plan(source,start,end,owner,'heng')
                if arguments is None:raise ValueError('Native starlight geometry changed')
                function='native_self_scatter'
            else:
                found=set(re.findall(r'\b(\w+)\s*\(',source.mask[start+1:end]))
                if found-allowed:raise ValueError('Review new calls in '+name+': '+str(found-allowed))
                calls=list(PROJECT.finditer(source.mask,start+1,end))
                if len(calls)!=1 or source.conditions(calls[0].start(),owner):raise ValueError('Reviewed self projection changed: '+name)
                function=calls[0].group(1);arguments=source.args(calls[0].end()-1)
            helpers.entries[name]={'function':function,'arguments':arguments,'parameters':parameters,'source':str(root/'src'/relative),'line':source.text.count('\n',0,start)+1}
