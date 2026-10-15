"""Direct project() calls whose native origin and destination are established.

PROJECT keeps the original STOP/JUMP flags. In particular it does not inherit
fire_ball's target-through-monsters or fire_ball_jump's LOS precondition.
"""
import re
from pure_expressions import NumericScope
from projection_helpers import resolve_flags

def aimed_raw_project(source,query,owner,core,projections):
    from profiles import ELEMENTS,after_input_conditions,effect_kind
    if core not in ('frox','genso') or len(projections)!=1:return None
    projection=projections[0]
    if projection.group(1)!='project':return None
    args=source.args(projection.end()-1)
    if len(args)!=(7 if core=='frox' else 8) or args[0]!='0':return None
    if core=='genso' and args[-1] not in ('-1','0'):return None
    if not args[5].startswith('GF_') or args[5][3:] not in ELEMENTS:return None
    numeric=NumericScope(source,query.start(),owner,core,projection.start())
    if not numeric(args[1]):return None
    flags=resolve_flags(args[6],numeric)
    allowed={'PROJECT_'+s for s in 'STOP THRU BEAM GRID ITEM KILL JUMP HIDE DISI LOS REFLECTABLE FAST FULL_DAM NO_PAIN LONG_RANGE'.split()}
    if flags is None or any(s not in allowed for s in re.findall(r'PROJECT_\w+',flags)):return None
    if after_input_conditions(source,query,projection.start(),owner)!=source.conditions(query.start(),owner):return None
    parameters=source.args(query.end()-1)
    if not parameters or not re.fullmatch(r'&\w+',parameters[0]):return None
    direction=parameters[0][1:]
    eight=query.group(1) in ('get_rep_dir','get_rep_dir2')
    between=source.mask[source.ends[query.end()-1]+1:projection.start()]
    between=re.sub(r'^\s*\)\s*return(?:\s+(?:FALSE|false|NULL))?\s*;','',between)
    if eight:between=re.sub(r'\bif\s*\(\s*'+direction+r'\s*==\s*5\s*\)\s*return(?:\s+(?:FALSE|false|NULL))?\s*;','',between)
    scale=None
    for var,origin,delta,target in [(args[2],'py','ddy','target_row'),(args[3],'px','ddx','target_col')]:
        if not re.fullmatch(r'\w+',var):return None
        pattern=r'\b'+var+r'\s*=\s*'+origin+r'\s*\+\s*(?:(99)\s*\*\s*)?'+delta+r'\['+direction+r'\]\s*;'
        matches=list(re.finditer(pattern,between))
        if len(matches)!=1:return None
        current=99 if matches[0].group(1) else 1
        if scale is not None and scale!=current:return None
        scale=current
        between=re.sub(pattern,'',between)
    if not eight:
        # Exact native target substitution. Do not treat ddx[5]/ddy[5] as the
        # selected point in an action that never reads the target coordinates.
        target_block=(r'if\s*\(\s*\(?\s*'+direction+r'\s*==\s*5\s*\)?\s*&&\s*target_okay\(\)\s*\)\s*\{\s*'
                      +re.escape(args[3])+r'\s*=\s*target_col\s*;\s*'+re.escape(args[2])+r'\s*=\s*target_row\s*;\s*\}')
        if len(re.findall(target_block,between))!=1:return None
        between=re.sub(target_block,'',between)
    if re.search(r'\b(?:if|else|for|while|do|switch|return|get_\w*dir|hack_project_start_\w+)\b',between):return None
    for variable in (args[2],args[3]):
        if re.search(r'\b'+variable+r'\s*(?:[+*/%&|^\-]?=(?!=)|\+\+|--)|(?:\+\+|--|&)\s*'+variable+r'\b',between):return None
    if re.search(r'(?:->|\.)\s*\w+\s*(?:[+*/%&|^\-]?=(?!=)|\+\+|--)|\]\s*(?:[+*/%&|^\-]?=(?!=)|\+\+|--)',between):return None
    # Remaining calls are presentation or damage-only RNG; they are not replayed.
    if any(call not in {'msg_print','msg_format','randint1','randint0','damroll'} for call in re.findall(r'\b(\w+)\s*\(',between)):return None
    # arg1 is the native direction delta multiplier, not the effect radius.
    return f'hb_target_profile_effect(NATIVE_SHAPE_PROJECT,{args[5]},{args[1]},0,{flags},0,{scale},0)'+effect_kind(args[5])

def first_impact_project(source,query,owner,core,projections):
    """Describe only the first native impact when later effects are complex.

    The reviewed UI deliberately stops at that boundary. The fallback never
    evaluates damage, random radius, later terrain changes, or chained shots.
    """
    from profiles import ELEMENTS
    if core not in ('frox','genso','heng'):return None
    if query.group(1) not in ('get_fire_dir','get_aim_dir','get_rep_dir','get_rep_dir2','get_direction','tgt_pt'):return None
    query_end=source.ends[query.end()-1]+1
    candidates=[p for p in projections if p.group(1)=='project' and p.start()>query_end]
    if not candidates:return None
    projection=candidates[0];args=source.args(projection.end()-1)
    if core=='heng':
        if len(args)!=8 or args[0]!='player_ptr' or args[1]!='0':return None
        radius_at,y_at,x_at,element_at,flags_at=2,3,4,6,7
        default_element='static_cast<int>(AttributeType::MISSILE)'
    else:
        if len(args)!=(7 if core=='frox' else 8) or args[0]!='0':return None
        if core=='genso' and args[7] not in ('-1','0'):return None
        radius_at,y_at,x_at,element_at,flags_at=1,2,3,5,6
        default_element='GF_MISSILE'
    # A self-centred projection after an unrelated prompt is not its impact.
    if (args[y_at],args[x_at]) in (('py','px'),('player_ptr->y','player_ptr->x')):return None
    between=source.mask[query_end:projection.start()]
    if re.search(r'\b(?:get_fire_dir|get_aim_dir|get_rep_dir2?|get_direction|tgt_pt)\s*\(',between):return None
    scale=impact_scale(source,query,owner,args[y_at],args[x_at],between,core)
    if scale is None:return None
    numeric=NumericScope(source,query.start(),owner,core,projection.start())
    flags=resolve_flags(args[flags_at],numeric)
    if flags is None:flags=local_flags(args[flags_at],between)
    allowed={'PROJECT_'+s for s in 'STOP THRU BEAM GRID ITEM KILL JUMP HIDE DISI LOS REFLECTABLE FAST FULL_DAM NO_PAIN LONG_RANGE'.split()}
    if flags is None or any(s not in allowed for s in re.findall(r'PROJECT_\w+',flags)):return None
    native_element=args[element_at].removeprefix('GF_').removeprefix('AttributeType::')
    if native_element in ELEMENTS:
        element=('static_cast<int>('+args[element_at]+')') if core=='heng' else args[element_at]
    else:element=default_element
    radius=numeric.expand(args[radius_at])
    if radius is None or not re.fullmatch(r'[\d\s()+\-*/%?:<>=!&|^~]+',re.sub(r'\b(?:p_ptr|player_ptr)\s*->\s*(?:lev|pclass|chp|mhp|csp|msp)\b','0',radius)):
        radius='0'
    if pure_negative(radius):radius='0'
    return ('hb_target_profile_plain(NATIVE_TARGET_ATTACK), '
        f'hb_target_profile_effect(NATIVE_SHAPE_PROJECT,{element},{radius},0,{flags},1,{scale},0)')

def pure_negative(value):
    return bool(re.fullmatch(r'\s*-\s*\d+\s*',value))

def local_flags(name,between):
    if not re.fullmatch(r'\w+',name):return None
    declaration=re.search(r'\b(?:constexpr\s+auto|const\s+(?:int|u32b|BIT_FLAGS)|int|u16b|u32b|BIT_FLAGS)\s+'
        +re.escape(name)+r'\s*=\s*([^;]+);',between)
    if not declaration:return None
    value=declaration.group(1).strip()
    if not re.search(r'PROJECT_',value) or not resolve_literal_flags(value):return None
    tail=between[declaration.end():]
    if re.search(r'\b'+re.escape(name)+r'\s*(?:[+*/%&|^\-]?=(?!=)|\+\+|--)|(?:\+\+|--|&)\s*'+re.escape(name)+r'\b',tail):return None
    return value

def resolve_literal_flags(value):
    names=re.findall(r'\b[A-Za-z_]\w*\b',value)
    return bool(names) and all(name.startswith('PROJECT_') for name in names) and not re.search(r'[^\s|()0-9]',re.sub(r'PROJECT_\w+','',value))

def impact_scale(source,query,owner,y,x,between,core):
    query_args=source.args(query.end()-1);name=query.group(1)
    if name=='tgt_pt':
        return '99' if len(query_args)>=2 and query_args[:2]==['&'+x,'&'+y] else None
    if (y,x) in (('m_ptr->fy','m_ptr->fx'),('monster.fy','monster.fx')):
        return '1' if name in ('get_rep_dir','get_rep_dir2','get_direction') else '99'
    direction=None
    if query_args:
        match=re.fullmatch(r'&?(\w+)',query_args[0])
        if match:direction=match.group(1)
    if core=='heng':
        assignment=re.search(r'\b(?:const\s+auto|auto|Direction)\s+(\w+)\s*=\s*$',source.mask[owner[0]:query.start()])
        if assignment:direction=assignment.group(1)
    if core=='heng' and (y,x)==('pos.y','pos.x') and direction:
        if re.search(r'\bpos\s*=\s*player_ptr->get_neighbor\(\s*'+direction+r'\s*\)',between):return '1'
        return None
    if core=='heng' and re.fullmatch(r'\w+',y) and re.fullmatch(r'\w+',x) and direction:
        pair=r'(?:const\s+auto|auto)\s*\[\s*'+re.escape(y)+r'\s*,\s*'+re.escape(x)+r'\s*\]\s*=\s*'+direction+r'\.get_target_position\([^;]+,\s*(99|1)\s*\)\s*;'
        match=re.search(pair,between)
        if match:return match.group(1)
    if not direction or not re.fullmatch(r'\w+',y) or not re.fullmatch(r'\w+',x):return None
    scales=[]
    for variable,origin,delta in ((y,'py','ddy'),(x,'px','ddx')):
        pattern=r'\b'+variable+r'\s*=\s*'+origin+r'\s*\+\s*(?:(99)\s*\*\s*)?'+delta+r'\['+direction+r'\]\s*;'
        matches=list(re.finditer(pattern,between))
        if len(matches)!=1:return None
        scales.append('99' if matches[0].group(1) else '1')
    if len(set(scales))!=1:return None
    for variable in (y,x):
        writes=list(re.finditer(r'\b'+variable+r'\s*(?:[+*/%&|^\-]?=(?!=)|\+\+|--)|(?:\+\+|--)\s*'+variable+r'\b',between))
        if len(writes)!=1:return None
    if name in ('get_aim_dir','get_fire_dir'):
        target=(r'if\s*\([^)]*'+direction+r'\s*==\s*5[^)]*\)\s*\{[^{}]*'
            +re.escape(x)+r'\s*=\s*target_col\s*;[^{}]*'+re.escape(y)+r'\s*=\s*target_row\s*;[^{}]*\}')
        if not re.search(target,between,re.S):return None
    return scales[0]
