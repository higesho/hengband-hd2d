"""Conservative call-site metadata. Unsupported sites remain explicit in the audit.

Only expressions whose availability and lack of side effects are established
are evaluated before input. Damage expressions (including RNG) are never read.
Original action calls and their control flow remain unchanged.
"""
import re
import line_anchor as anchor
from profile_inventory import Source,PROJECT,CALL,conditions_conflict
from pure_expressions import NumericScope
from projection_helpers import literal_flags,resolve_flags

# Hashes of original lines (see line_anchor.py); the original text is not quoted here.
ELEMENT_TYPE_LINE='a6a995bef30e'
BALROG_PICK_LINE='32a30d71c8ac'
THREE_WAY_LINES=('3fdeff77175e','cea2068ac267','b3553abc9b82')

class _Elements(set):
    """見直し済みの属性の表。TOband を処理している間だけ、TOband の表（TOBAND_ELEMENTS）も「見直し済み」として見る。

    TOband の project() で範囲の形に効く属性は GF_DISINTEGRATE（分解の範囲）と GF_ATTACK（突進）だけで、それ以外の属性は
    当たった先の効果（ダメージ・状態・地形）が違うだけで経路・範囲は同じ（spells1.c の project() を読んで確認）。
    ほかのコアの表は変えない（`core` を 'toband' にするのは annotate() の先頭だけ）。"""
    core=None

    def __contains__(self,name):
        return set.__contains__(self,name) or (_Elements.core=='toband' and name in TOBAND_ELEMENTS)

    def __or__(self,other):
        return _Elements(set.__or__(self,other))

# TOband にあって上の表に無い属性のうち、形を変えないもの（GF_ATTACK は突進するので入れない）。
TOBAND_ELEMENTS=frozenset(('STONE PHYSICAL BLUNT EDGED VOLCANIC_BOMB GODLY_SPEAR PURE_FIRE PURE_AQUA PURE_EARTH PURE_WIND MAKE_TRAP MAKE_TREE '
    'MAKE_GLYPH OLD_STONE STAR_HEAL NEW_SLOW NEW_DRAIN SILENT CRUSADE CONTROL_BEAST CONTROL_DRAGON GENOCIDE_UNDEAD ANIM_DEAD STRIKE_NOVA '
    'WORD_OF_PAIN SHINING SPECIAL_STONE DUAL_DRAIN DRAIN_SOUL ERASE_ELEM WATER_FLOW LAVA_FLOW BRAINSTORM UMBRA').split())
ELEMENTS=_Elements(('ACID ELEC FIRE COLD POIS MISSILE PLASMA NETHER WATER CHAOS SHARDS SOUND DISENCHANT FORCE INERTIA GRAVITY TIME LITE LITE_WEAK DARK DISINTEGRATE NUKE MANA HOLY_FIRE HELL_FIRE ROCKET ICE').split())
ELEMENTS.update(('INERTIAL CONFUSION NEXUS METEOR DARK_WEAK KILL_WALL KILL_DOOR KILL_TRAP JAM_DOOR OLD_DRAIN HYPODYNAMIA OLD_CLONE OLD_POLY OLD_HEAL OLD_SPEED OLD_SLOW OLD_CONF OLD_SLEEP AWAY_UNDEAD AWAY_EVIL AWAY_ALL TURN_UNDEAD TURN_EVIL TURN_ALL DISP_UNDEAD DISP_EVIL DISP_ALL DISP_DEMON DISP_LIVING DISP_GOOD STASIS STASIS_EVIL DEATH_RAY STUN CHARM CONTROL_UNDEAD CONTROL_DEMON CONTROL_ANIMAL CONTROL_LIVING CHARM_LIVING PSI PSI_DRAIN TELEKINESIS DOMINATION DRAIN_MANA MIND_BLAST BRAIN_SMASH CAUSE_1 CAUSE_2 CAUSE_3 CAUSE_4 HAND_DOOM').split())
ELEMENTS.update(('VOID_MAGIC ABYSS MONSTER_SHOOT PLAYER_SHOOT PSY_SPEAR').split())
ELEMENTS.update(('ARROW INACT PUNISH_1 PUNISH_2 PUNISH_3 PUNISH_4 GENOCIDE COSMIC_HORROR E_GENOCIDE CAPTURE').split())
ELEMENTS.update(('POLLUTE DISTORTION TORNADO').split())
ELEMENTS.update(('WOUNDS PHOTO INERT STORM WATER2 ROCK AMNESIA PSI_STORM PSI_BRAIN_SMASH PSI_EGO_WHIP ANTIMAGIC MANA_CLASH ELDRITCH ELDRITCH_STUN ELDRITCH_DRAIN ELDRITCH_DISPEL ELDRITCH_CONFUSE').split())
ELEMENTS.update(('SEEKER SUPER_RAY').split())
SUPPORT_ELEMENTS={'OLD_HEAL','OLD_SPEED','STAR_HEAL'}

def effect_kind(element):
    name=element.removeprefix('GF_').removeprefix('AttributeType::')
    return ', hb_target_profile_plain(NATIVE_TARGET_SUPPORT)' if name in SUPPORT_ELEMENTS else ''

def pure_integer(value):
    return bool(re.fullmatch(r'\s*-?\d+\s*',value))

def after_input_conditions(source,query,position,owner):
    conditions=source.conditions(position,owner)
    # The preview runs only after this input succeeds. The enclosing
    # if(get_fire_dir(...)) is therefore not an additional gameplay branch.
    # Remove one occurrence; a second input still needs its own decision.
    original=source.mask[query.start():source.ends[query.end()-1]+1].strip()
    if original in conditions:conditions.remove(original)
    return conditions

def blast_effect_code(args,core,safe):
    if len(args)!=(7 if core=='genso' else 6):return None
    attribute=args[1] if core=='heng' else args[0]
    name=attribute.removeprefix('GF_').removeprefix('AttributeType::')
    if name not in ELEMENTS|{'ARROW','PSY_SPEAR'}:return None
    count,deviation=args[4:6]
    if not safe(count) or not safe(deviation):return None
    element='static_cast<int>('+attribute+')' if core=='heng' else attribute
    flags='PROJECT_FAST|PROJECT_THRU|PROJECT_STOP|PROJECT_KILL|PROJECT_GRID'
    if core!='genso' or name!='ARROW':flags+='|PROJECT_REFLECTABLE'
    if core=='genso' and not re.fullmatch(r'\s*0[Ll]?\s*',args[6]):
        if not literal_flags(args[6]):return None
        flags=args[6]
    return f'hb_target_profile_effect(NATIVE_SHAPE_BLAST,{element},0,0,{flags},({deviation})>0,{deviation},{count})'

def sil_effect_code(projection,args,safe=pure_integer):
    if not args or args[0] not in ('GF_FIRE','GF_COLD','GF_ELEC','GF_ACID'):return None
    if projection in ('fire_beam','fire_bolt') and len(args)==5:
        shape='BEAM' if projection=='fire_beam' else 'BOLT'
        flags='PROJECT_THRU|PROJECT_KILL|'+('PROJECT_BEAM' if shape=='BEAM' else 'PROJECT_STOP')
        return f'hb_target_profile_effect(NATIVE_SHAPE_{shape},{args[0]},0,MAX_RANGE,{flags},0,0,0)'
    if projection=='fire_ball' and len(args)==6 and safe(args[5]):
        return f'hb_target_profile_effect(NATIVE_SHAPE_BALL,{args[0]},{args[5]},MAX_RANGE,PROJECT_BOOM|PROJECT_GRID|PROJECT_ITEM|PROJECT_KILL,0,0,0)'
    return None

def heng_effect_code(projection,args,safe=pure_integer,possible_override=None):
    if projection=='fire_ball' and len(args)==6:args=args[:5]
    if projection=='fire_blast':return blast_effect_code(args,'heng',safe)
    if projection=='lite_line' and len(args)==3:
        return 'hb_target_profile_effect(NATIVE_SHAPE_BEAM,static_cast<int>(AttributeType::LITE_WEAK),0,0,PROJECT_THRU|PROJECT_BEAM|PROJECT_GRID|PROJECT_KILL,0,0,0)'
    offset=2 if projection=='fire_bolt_or_beam' else 1
    if len(args)<=offset:return None
    attribute=args[offset]
    if not attribute.startswith('AttributeType::') or attribute.split('::')[-1] not in ELEMENTS:return None
    element='static_cast<int>('+attribute+')'
    bolt='PROJECT_THRU|PROJECT_STOP|PROJECT_KILL|PROJECT_GRID|PROJECT_REFLECTABLE'
    if attribute=='AttributeType::MONSTER_SHOOT':bolt='PROJECT_THRU|PROJECT_STOP|PROJECT_KILL|PROJECT_GRID'
    beam='PROJECT_THRU|PROJECT_BEAM|PROJECT_KILL|PROJECT_GRID|PROJECT_ITEM'
    ball='PROJECT_STOP|PROJECT_KILL|PROJECT_GRID|PROJECT_ITEM'
    def emit(shape,radius,flags,possible='0'):
        if possible_override is not None:possible=f'({possible})||({possible_override})'
        return f'hb_target_profile_effect(NATIVE_SHAPE_{shape},{element},{radius},0,{flags},{possible},0,0)'+effect_kind(attribute)
    if projection=='fire_bolt' and len(args)==4:return emit('BOLT','0',bolt)
    if projection=='fire_beam' and len(args)==4:return emit('MIRROR_RAY' if attribute in ('AttributeType::SEEKER','AttributeType::SUPER_RAY') else 'BEAM','0',beam)
    if projection=='project_hook' and len(args)==5:
        flags=resolve_flags(args[4],safe)
        if flags:return emit('BEAM' if 'PROJECT_BEAM' in flags else 'BOLT','0','PROJECT_THRU|('+flags+')')
        return None
    if projection in ('fire_ball','fire_ball_hide','fire_breath','fire_rocket') and len(args)==5 and safe(args[4]):
        if pure_integer(args[4]) and int(args[4])<0:return None
        shape={'fire_ball':'BALL','fire_ball_hide':'BALL','fire_breath':'BREATH','fire_rocket':'ROCKET'}[projection]
        return emit(shape,args[4],ball+('|PROJECT_BREATH' if shape=='BREATH' else '')+('|PROJECT_HIDE' if projection=='fire_ball_hide' else ''))
    if projection=='fire_bolt_or_beam' and len(args)==5 and safe(args[1]):
        chance=args[1]
        return f'(({chance})>0?({emit("BEAM","0",beam,f"({chance})<100")},0):0), (({chance})<100?({emit("BOLT","0",bolt,f"({chance})>0")},0):0)'
    return None

def heng_selected_native_switch(source,query,owner,path,helpers):
    if path.name!='cmd-mane.cpp' or owner[2]!='use_mane' or not helpers:return None
    table=re.search(r'\bAIMING_SPELLS\s*=\s*\{([^{}]+)\}',source.mask)
    if not table:return None
    level_definition=re.search(r'\bPLAYER_LEVEL\s+plev\s*=\s*player_ptr->lev\s*;',source.mask[owner[0]:query.start()])
    if not level_definition:return None
    if re.search(r'\bplev\s*(?:[+*/%&|^\-]?=(?!=)|\+\+|--)|&\s*plev\b',source.mask[owner[0]+level_definition.end():owner[1]]):return None
    choices=re.findall(r'MonsterAbilityType::\w+',table.group(1))
    cases=[]
    for choice in choices:
        label=next((c for c in source.cases if owner[0]<c.start()<owner[1] and c.group().strip()=='case '+choice+':'),None)
        if label is None:raise ValueError('Aimed imitation has no native case: '+choice)
        region=source.region(label.end()+1)
        calls=list(PROJECT.finditer(source.mask,label.end(),region[1]))
        matcher=helpers.matcher()
        if matcher:
            seen={c.start() for c in calls}
            calls.extend(c for c in matcher.finditer(source.mask,label.end(),region[1]) if c.start() not in seen)
        if len(calls)!=1:return None
        call=calls[0]
        prefix=source.mask[label.end():call.start()]
        if re.search(r'\b(?:if|else|switch|for|while|do|get_\w*dir)\b',prefix):return None
        name=call.group(1);args=source.args(call.end()-1)
        expanded=helpers.expand(name,args)
        if expanded:name,args=expanded
        code=heng_effect_code(name,args,NumericScope(source,query.start(),owner,'heng',call.start(),{'plev'}))
        if not code:return None
        cases.append('case '+choice+':'+code+';break;')
    return '([&](){switch(spell){'+''.join(cases)+'default:break;}}())'

def heng_android_weapon(source,query,owner,projections,path):
    if path.name!='racial-android.cpp' or owner[2]!='android_inside_weapon':return None
    parts=[]
    for projection in projections:
        conditions=source.conditions(projection.start(),owner)
        if len(conditions)>1 or conditions and not re.fullmatch(r'player_ptr->lev\s*<\s*\d+',conditions[0]):return None
        code=heng_effect_code(projection.group(1),source.args(projection.end()-1),NumericScope(source,query.start(),owner,'heng',projection.start()))
        if not code:return None
        end=source.ends[projection.end()-1]+1
        if not re.match(r'\s*;\s*return true;',source.mask[end:]):return None
        parts.append(('if('+conditions[0]+'){'+code+';return;}') if conditions else code+';')
    return '([&](){'+''.join(parts)+'}())' if parts else None

def heng_native_element_variants(source,query,owner,projections,path):
    if len(projections)!=1:return None
    projection=projections[0];name=projection.group(1);args=source.args(projection.end()-1)
    attribute_index=2 if name=='fire_bolt_or_beam' else 1
    if len(args)<=attribute_index:return None
    numeric=NumericScope(source,query.start(),owner,'heng',projection.start())
    def emit(element,possible='1'):
        values=list(args);values[attribute_index]='AttributeType::FIRE'
        result=heng_effect_code(name,values,numeric,possible)
        return result.replace('static_cast<int>(AttributeType::FIRE)','static_cast<int>('+element+')') if result else None
    if path.name=='mind-elementalist.cpp' and owner[2]=='cast_element_spell' and args[attribute_index]=='typ':
        region=source.region(query.start());body=source.text[region[0]:region[1]]
        if not anchor.count(body,ELEMENT_TYPE_LINE):return None
        table=next(f for f in source.functions if f[2]=='build_element_types')
        unknown=set(re.findall(r'AttributeType::(\w+)',source.mask[table[0]:table[1]]))-ELEMENTS
        if unknown:raise ValueError('Elementalist table added unreviewed spatial elements: '+str(unknown))
        code=emit('hb_type','hb_possible')
        if not code:return None
        return ('([&](){const auto &hb_realm=element_types.at(player_ptr->element_realm);'
                'const auto hb_base=hb_realm.type.at(power.elem);const auto hb_extra=hb_realm.extra.find(hb_base);'
                'const bool hb_possible=hb_extra!=hb_realm.extra.end()&&player_ptr->lev*2>0&&player_ptr->lev*2<100;'
                'const auto hb_emit=[&](AttributeType hb_type){'+code+';};'
                'if(hb_extra==hb_realm.extra.end()||player_ptr->lev*2<100)hb_emit(hb_base);'
                'if(hb_extra!=hb_realm.extra.end()&&player_ptr->lev*2>0)hb_emit(hb_extra->second);}())')
    if path.name=='racial-balrog.cpp' and owner[2]=='demonic_breath' and args[attribute_index]=='type':
        if not anchor.count(source.text[owner[0]:query.start()],BALROG_PICK_LINE):raise ValueError('Balrog breath selection changed')
        parts=[emit('AttributeType::'+element) for element in ('NETHER','FIRE')]
        return ', '.join(parts) if all(parts) else None
    if path.name=='realm-arcane.cpp' and owner[2]=='do_arcane_spell' and args[attribute_index]=='type':
        tail=source.mask[query.end():projection.start()]
        found=re.search(r'constexpr static auto element_types\s*=\s*\{([^{}]+)\};\s*const auto type = rand_choice\(element_types\);',tail)
        if not found:return None
        elements=re.findall(r'AttributeType::(\w+)',found.group(1))
        if not elements or any(e not in ELEMENTS for e in elements):return None
        if re.sub(r'AttributeType::\w+|[\s,]','',found.group(1)):return None
        parts=[emit('AttributeType::'+element) for element in elements]
        return ', '.join(parts) if all(parts) else None
    if path.name=='activation-breath.cpp' and owner[2]=='activate_dragon_breath' and args[attribute_index]=='breath.first':
        body=source.text[owner[0]:owner[1]]
        if 'DragonBreaths::get_breaths(flags)' not in body or 'rand_choice(breaths)' not in body:raise ValueError('Dragon armour breath selection changed')
        code=emit('hb_breath.first','hb_breaths.size()>1')
        if not code:return None
        return ('([&](){if(!item.is_fully_known()){hb_target_profile_plain(NATIVE_TARGET_NEUTRAL);hb_target_profile_known(0);return;}'
                'const auto hb_breaths=DragonBreaths::get_breaths(item.get_flags_known());'
                'for(const auto &hb_breath:hb_breaths){'+code+';}}())')
    return None

def effect_code(projection,args,safe=pure_integer,possible_override=None,core=None):
    core=core or getattr(safe,'core',None)
    # TOband の fire_ball・fire_ball_hide・fire_rocket は末尾に no_reduce（威力を距離で減らさない）を取る。形には関係しない。
    if core=='toband' and projection in ('fire_ball','fire_ball_hide','fire_rocket') and len(args)==5:args=args[:4]
    if projection=='fire_blast':return blast_effect_code(args,'frox' if len(args)==6 else 'genso',safe)
    if projection=='fire_spark' and len(args)==4 and args[0].startswith('GF_') and args[0][3:] in ELEMENTS and safe(args[3]):
        flags='PROJECT_GRID|PROJECT_ITEM|PROJECT_KILL|PROJECT_THRU|PROJECT_FAST|PROJECT_MASTERSPARK'
        if args[0]=='GF_DISINTEGRATE':flags+='|PROJECT_DISI'
        return f'hb_target_profile_effect(NATIVE_SHAPE_SPARK,{args[0]},{args[3]},0,{flags},0,0,0)'
    if projection not in ('fire_bolt','fire_beam','fire_bolt_or_beam','fire_ball','fire_ball_aux','fire_ball_hide','fire_ball_jump','fire_rocket','project_hook'):return None
    offset=1 if projection=='fire_bolt_or_beam' else 0
    if len(args)<=offset or not args[offset].startswith('GF_') or args[offset][3:] not in ELEMENTS:return None
    element=args[offset]
    bolt='PROJECT_THRU|PROJECT_STOP|PROJECT_KILL|PROJECT_REFLECTABLE|PROJECT_GRID'
    if getattr(safe,'core','frox')=='genso' and element=='GF_ARROW':bolt='PROJECT_THRU|PROJECT_STOP|PROJECT_KILL|PROJECT_GRID'
    beam='PROJECT_THRU|PROJECT_BEAM|PROJECT_KILL|PROJECT_GRID|PROJECT_ITEM'
    ball='PROJECT_STOP|PROJECT_KILL|PROJECT_GRID|PROJECT_ITEM'
    def emit(shape,radius,flags,possible='0'):
        if possible_override is not None:possible=f'({possible})||({possible_override})'
        return f'hb_target_profile_effect(NATIVE_SHAPE_{shape},{element},{radius},0,{flags},{possible},0,0)'+effect_kind(element)
    if projection=='project_hook':
        if len(args)!=4:return None
        flags=resolve_flags(args[3],safe)
        if flags is None:return None
        names=re.findall(r'[A-Za-z_]\w*',flags)
        allowed={'PROJECT_'+part for part in ('BEAM THRU STOP GRID ITEM KILL HIDE DISI REFLECTABLE NO_PAIN FAST LOS FULL_DAM'+(' AVOIDABLE NO_REDUCE' if core=='toband' else '')).split()}
        if not names or any(name not in allowed for name in names) or re.sub(r'PROJECT_\w+|[\s|()]','',flags):return None
        return emit('BEAM' if 'PROJECT_BEAM' in names else 'BOLT','0','PROJECT_THRU|('+flags+')')
    if projection=='fire_bolt':return emit('BOLT','0',bolt)
    if projection=='fire_beam':return emit('MIRROR_RAY' if element in ('GF_SEEKER','GF_SUPER_RAY') else 'BEAM','0',beam)
    if projection in ('fire_ball','fire_ball_aux','fire_ball_hide','fire_ball_jump','fire_rocket'):
        if len(args)!=(5 if projection in ('fire_ball_aux','fire_ball_jump') else 4) or not safe(args[3]):return None
        if projection=='fire_ball_aux':
            flags=resolve_flags(args[4],safe)
            if flags is None:return None
            ball+='|('+flags+')'
        if projection=='fire_ball_hide':ball+='|PROJECT_HIDE'
        if projection=='fire_ball_jump':ball+='|PROJECT_JUMP'
        radius=args[3]
        if projection=='fire_rocket':return emit('ROCKET',radius,ball)
        if pure_integer(radius):return emit('BREATH' if int(radius)<0 else 'BALL',radius,ball)
        return f'(({radius})<0?({emit("BREATH",radius,ball)},0):({emit("BALL",radius,ball)},0))'
    chance=args[0]
    if not safe(chance):return None
    # beam_chance() only reads class and level; do-spell.c is tracked by overlay manifest.
    return f'(({chance})>0?({emit("BEAM","0",beam,f"({chance})<100")},0):0), (({chance})<100?({emit("BOLT","0",bolt,f"({chance})>0")},0):0)'

def frox_native_element_variants(source,query,owner,projections,path):
    if len(projections)!=1:return None
    projection=projections[0];name=projection.group(1);args=source.args(projection.end()-1)
    if not args or name not in ('fire_ball','fire_bolt','fire_beam'):return None
    numeric=NumericScope(source,query.start(),owner,'frox',projection.start())
    region=source.region(query.start());body=source.mask[region[0]:region[1]]
    elements=[]
    if path.name=='devices.c' and owner[2]=='do_effect' and args[0]=='_choices[which].type':
        table=re.search(r'\b_choices\[(\d+)\]\s*=\s*\{',body)
        if not table:return None
        opening=region[0]+table.end()-1;closing=source.ends[opening]
        elements=re.findall(r'\{\s*(GF_\w+)\s*,[^{}]*\}',source.mask[opening+1:closing])
        count=int(table.group(1))
        if count!=len(elements) or count<1 or count>32:return None
        draw=re.search(r'\bint\s+which\s*=\s*randint0\('+str(count)+r'\)\s*;',source.mask[closing:query.start()])
        if not draw:return None
        if re.search(r'\b_choices\[[^]]+\]\.(?:type|desc)\s*=(?!=)|\bwhich\s*(?:[+*/%&|^\-]?=(?!=)|\+\+|--)|(?:\+\+|--|&)\s*which\b',source.mask[closing+draw.end():projection.start()]):return None
    elif path.name=='spells_c.c' and owner[2]=='demon_breath_spell' and args[0]=='type':
        native=re.search(r'\bint\s+type\s*=\s*\(one_in_\(2\)\s*\?\s*(GF_\w+)\s*:\s*(GF_\w+)\)\s*;',body)
        if not native:return None
        elements=list(native.groups())
        if re.search(r'\btype\s*(?:[+*/%&|^\-]?=(?!=)|\+\+|--)|&\s*type\b',body[native.end():projection.start()-region[0]]):return None
    if not elements or any(e[3:] not in ELEMENTS for e in elements):return None
    parts=[]
    for element in dict.fromkeys(elements):
        values=list(args);values[0]=element
        code=effect_code(name,values,numeric,'1' if len(elements)>1 else '0')
        if not code:return None
        parts.append(code)
    return ', '.join(parts)

def native_random_beam(source,query,owner,core,projections,path):
    if len(projections)!=2:return None
    if (core,path.name,owner[2]) not in {('heng','mind-mindcrafter.cpp','cast_mindcrafter_spell'),('frox','mindcrafter.c','_neural_blast_spell')}:return None
    first,second=projections
    if first.group(1)!='fire_beam' or second.group(1)!='fire_ball':return None
    a=after_input_conditions(source,query,first.start(),owner);b=after_input_conditions(source,query,second.start(),owner)
    baseline=source.conditions(query.start(),owner);a=[c for c in a if c not in baseline];b=[c for c in b if c not in baseline]
    if len(a)!=1 or b!=['!('+a[0]+')']:return None
    test=re.fullmatch(r'randint1\(100\)\s*<\s*(.+)',a[0])
    if not test:return None
    numeric=NumericScope(source,query.start(),owner,core,second.start())
    limit=test.group(1)
    if not numeric(limit):return None
    possible=f'({limit})>1&&({limit})<=100'
    emitter=heng_effect_code if core=='heng' else effect_code
    beam=emitter(first.group(1),source.args(first.end()-1),numeric,possible)
    ball=emitter(second.group(1),source.args(second.end()-1),numeric,possible)
    if not beam or not ball:return None
    return f'(({limit})>1?({beam},0):0), (({limit})<=100?({ball},0):0)'

def genso_monspell_values(source,query,owner,projections,path):
    if path.name!='mspells3.c' or owner[2]!='cast_monspell_new_aux' or len(projections)!=1:return None
    projection=projections[0];args=source.args(projection.end()-1);name=projection.group(1)
    if not args or name not in ('fire_ball','fire_ball_hide','fire_ball_jump','fire_bolt','fire_beam','fire_spark'):return None
    region=source.region(query.start());start=region[0]
    # num is the selected spell. powerful is computed once from the caster's
    # class/form before this switch without a random call. Only these reviewed
    # inputs may select a local attribute or radius here.
    numeric=NumericScope(source,query.start(),owner,'genso',projection.start(),{'num','powerful'})
    def local_value(variable,element=False):
        before=source.mask[start:query.start()];tail=source.mask[query.end():projection.start()]
        if not re.search(r'\bint\s+'+variable+r'\s*(?:=|;)',before):return False
        if re.search(r'\b'+variable+r'\s*(?:[+*/%&|^\-]?=(?!=)|\+\+|--)|&\s*'+variable+r'\b',tail):return False
        writes=list(re.compile(r'\b'+variable+r'\s*=(?!=)\s*([^;{}]+);').finditer(source.mask,start,query.start()))
        if not writes:return False
        for write in writes:
            value=write.group(1).strip()
            if element:
                if not value.startswith('GF_') or value[3:] not in ELEMENTS:return False
            elif not numeric(value):return False
            if any(not numeric(c) for c in source.conditions(write.start(),owner)):return False
        remainder=re.sub(r'\b'+variable+r'\s*=(?!=)\s*[^;{}]+;','',before)
        return not re.search(r'\b'+variable+r'\s*(?:[+*/%&|^\-]=|\+\+|--)|&\s*'+variable+r'\b',remainder)
    values=list(args);dynamic_element=args[0]=='typ'
    if dynamic_element:
        if not local_value('typ',True):return None
        values[0]='GF_FIRE'
    if len(args)>=4 and args[3]=='rad':
        if not local_value('rad'):return None
        numeric.known_inputs=numeric.known_inputs|{'rad'}
    code=effect_code(name,values,numeric)
    if code and dynamic_element:
        code=code.replace('GF_FIRE','typ')
        code=code.replace('PROJECT_REFLECTABLE','((typ==GF_ARROW||typ==GF_SPECIAL_SHOT)?0:PROJECT_REFLECTABLE)')
    return code

def conditional_aimed_effects(source,query,owner,core,projections,helpers,known_inputs=frozenset()):
    if not projections:return None
    query_conditions=source.conditions(query.start(),owner)
    paths=[after_input_conditions(source,query,p.start(),owner) for p in projections]
    if any(not conditions_conflict(a,b) for i,a in enumerate(paths) for b in paths[i+1:]):return None
    between=source.mask[source.ends[query.end()-1]+1:projections[-1].start()]
    if re.search(r'\b(?:for|while|do|get_\w*dir)\b',between):return None
    # Retain the standard cancellation branch; other early returns need their
    # own native condition before a forecast may be emitted.
    if core=='heng':
        assignment=re.search(r'\b(?:const\s+auto|auto|Direction)\s+(\w+)\s*=\s*$',source.mask[owner[0]:query.start()])
        if assignment:
            cancel=r'\bif\s*\(\s*!\s*'+re.escape(assignment.group(1))+r'\s*\)\s*\{?\s*return\s+(?:false|FALSE|nullptr|NULL|tl::nullopt|std::nullopt)\s*;\s*\}?'
            between=re.sub(cancel,'',between)
    else:
        between=re.sub(r'^\s*\)\s*return(?:\s+(?:false|FALSE|NULL))?\s*;','',between)
    if re.search(r'\breturn\b',between):return None
    plans=[]
    for projection,conditions in zip(projections,paths):
        # A call inside an if condition may be skipped by && or ||. A direct
        # if(fire_...()) still executes the projection exactly once.
        for test in re.compile(r'\bif\s*\(').finditer(source.mask,query.end(),projection.start()):
            opening=test.end()-1
            if source.ends.get(opening,opening)>projection.start():
                prefix=source.mask[opening+1:projection.start()]
                if re.sub(r'[\s!()]','',prefix):return None
        numeric=NumericScope(source,query.start(),owner,core,projection.start(),known_inputs)
        name=projection.group(1);args=source.args(projection.end()-1)
        expanded=helpers.expand(name,args) if helpers else None
        if expanded:name,args=expanded
        code=({'sil':sil_effect_code,'heng':heng_effect_code}.get(core,effect_code))(name,args,numeric)
        if not code:return None
        guards=[]
        for condition in conditions:
            if condition in query_conditions:continue
            if not numeric(condition):return None
            guards.append('('+condition+')')
        plans.append('('+ '&&'.join(guards)+'?('+code+',0):0)' if guards else code)
    return 'hb_target_profile_plain(NATIVE_TARGET_ATTACK), '+', '.join(plans)

def adjacent_melee(source,query,owner,core,projections):
    if core not in ('frox','genso','heng','toband') or not projections:return None
    attack='do_cmd_attack' if core=='heng' else 'py_attack'
    if any(p.group(1)!=attack for p in projections):return None
    if query.group(1) not in ('get_rep_dir','get_rep_dir2','get_direction'):return None
    region=source.region(query.start());body=source.mask[query.end():region[1]]
    safe_modes={'0','HISSATSU_NONE','HISSATSU_3WAY','HISSATSU_FIRE','HISSATSU_MINEUCHI','HISSATSU_POISON','HISSATSU_ZANMA','HISSATSU_HAGAN','HISSATSU_COLD','HISSATSU_KYUSHO','HISSATSU_MAJIN','HISSATSU_SUTEMI','HISSATSU_ELEC','HISSATSU_DRAIN','HISSATSU_UNDEAD','ROGUE_ASSASSINATE'}
    arguments=[source.args(p.end()-1) for p in projections]
    if any(a[-1] not in safe_modes for a in arguments):return None
    three=owner[2]=='_three_way_attack_spell' or owner[2]=='do_hissatsu_spell' and region[2]=='case 1:'
    if three:
        if core=='heng':
            if not all(anchor.count(body,h) for h in THREE_WAY_LINES):return None
        elif not all(s in body for s in ('ddy_cdd[cdir]','ddy_cdd[(cdir + 7) % 8]','ddy_cdd[(cdir + 1) % 8]')):return None
    elif core=='heng':
        if not all(a[1:3]==['pos.y','pos.x'] for a in arguments):return None
        if not re.search(r'const auto pos\s*=\s*player_ptr->get_neighbor\(dir\);',body):return None
        if re.search(r'\b(?:for|while|do)\b',body):return None
    else:
        if not all(a[:2]==['y','x'] for a in arguments):return None
        for variable,origin,delta in [('y','py','ddy'),('x','px','ddx')]:
            pattern=r'\b'+variable+r'\s*=\s*'+origin+r'\s*\+\s*'+delta+r'\[dir\]\s*;'
            if len(re.findall(pattern,body))!=1:return None
            # Additional coordinate writes before the final hit require a
            # movement or push model, rather than a stationary melee cell.
            before=body[:projections[-1].start()-query.end()]
            if len(re.findall(r'\b'+variable+r'\s*(?:[+\-*/]?=|\+\+|--)',before))!=1:return None
        if re.search(r'\b(?:for|while|do)\b',body):return None
    return f'hb_target_profile_effect(NATIVE_SHAPE_MELEE,0,0,1,0,0,{1 if three else 0},0)'

def heng_mspell_projection_code(pointer,breath_radius):
    """変愚 3.0.2.5 の青魔法・ものまねのブレス・ボール・ボルト（`mspell/mspell-projection-table.cpp` の表の行）。

    表の行（`MspellProjection`）を指すポインタの式 `pointer` から、形・属性・半径・旗を実行時に選ぶ。
    旗と半径は 3.0.2.4 の `cast_blue_magic_{breath,ball,bolt}` と `use_mane` の各 case と同じ。
    ブレスの半径だけは青魔法とものまねで求め方が違うので、呼ぶ側が渡す。
    """
    shape='hb_p->type==MspellProjectionType::BREATH?NATIVE_SHAPE_BREATH:hb_p->type==MspellProjectionType::BALL?NATIVE_SHAPE_BALL:NATIVE_SHAPE_BOLT'
    radius=f'hb_p->type==MspellProjectionType::BREATH?({breath_radius}):hb_p->type==MspellProjectionType::BALL?hb_p->radius:0'
    flags=('hb_p->type==MspellProjectionType::BREATH?(PROJECT_STOP|PROJECT_KILL|PROJECT_GRID|PROJECT_ITEM|PROJECT_BREATH):'
           'hb_p->type==MspellProjectionType::BALL?(PROJECT_STOP|PROJECT_KILL|PROJECT_GRID|PROJECT_ITEM):'
           '(PROJECT_THRU|PROJECT_STOP|PROJECT_KILL|PROJECT_GRID|PROJECT_REFLECTABLE)')
    return (f'if(const MspellProjection *hb_p={pointer}){{hb_target_profile_effect(({shape}),static_cast<int>(hb_p->attribute),'
            f'({radius}),0,({flags}),0,0,0);return;}}')


def check_mspell_projection_table(path):
    """表に出てくる属性が、狙いのプレビューで形を確かめ済みのものかを見る（青魔法の表の検査と同じ）。"""
    root=next(p for p in path.parents if p.name=='src')
    table=(root/'mspell/mspell-projection-table.cpp').read_text(encoding='latin1')
    unknown=set(re.findall(r'AttributeType::(\w+)',table))-ELEMENTS
    if unknown:raise ValueError('Monster spell projection table added unreviewed spatial elements: '+str(unknown))


def annotate(path,core='frox',ui_only=False,helpers=None,menu_audit=None):
    _Elements.core=core
    source=Source(path);edits=[];audit=[]
    for query in CALL.finditer(source.mask):
        owner=source.owner(query.start());region=source.region(query.start())
        if not owner or not region or owner[2].startswith(('get_','target_')):continue
        if query.group(1) not in ('get_buff_mon_dir','get_capture_mon_dir','get_fire_dir','get_fire_dir_aux','get_aim_dir','get_aim_dir_aux','get_rep_dir','get_rep_dir2','get_hack_dir','get_direction','tgt_pt'):continue
        projections=list(PROJECT.finditer(source.mask,query.end(),region[1]))
        if helpers:
            helper_matcher=helpers.matcher()
            if helper_matcher:
                existing={p.start() for p in projections}
                projections.extend(p for p in helper_matcher.finditer(source.mask,query.end(),region[1]) if p.start() not in existing)
                projections.sort(key=lambda p:p.start())
        query_conditions=source.conditions(query.start(),owner)
        projections=[p for p in projections if not conditions_conflict(query_conditions,source.conditions(p.start(),owner))]
        code=None
        if len(projections)==1:
            projection=projections[0]
            between=source.mask[source.ends[query.end()-1]+1:projection.start()]
            # Do not pair with another query or an intervening branch/loop. The
            # ordinary cancellation return after input is allowed.
            conditions=after_input_conditions(source,query,projection.start(),owner)
            # A projection used as the condition itself still executes once.
            # Do not admit a preceding condition such as flag && projection().
            between=re.sub(r'\bif\s*\(\s*!?\s*$','',between)
            if core=='heng':
                assignment=re.search(r'\b(?:const\s+auto|auto|Direction)\s+(\w+)\s*=\s*$',source.mask[owner[0]:query.start()])
                if assignment:
                    variable=assignment.group(1)
                    cancel=r'\bif\s*\(\s*!\s*'+variable+r'\s*\)\s*\{?\s*return\s+(?:false|FALSE|nullptr|NULL|tl::nullopt|std::nullopt)\s*;\s*\}?'
                    between=re.sub(cancel,'',between)
            if conditions==query_conditions and not re.search(r'\b(?:if|else|switch|for|while|do|get_\w*dir)\b',between):
                projection_name=projection.group(1);projection_args=source.args(projection.end()-1)
                expanded=helpers.expand(projection_name,projection_args) if helpers else None
                if expanded:projection_name,projection_args=expanded
                code=({'sil':sil_effect_code,'heng':heng_effect_code}.get(core,effect_code))(projection_name,projection_args,NumericScope(source,query.start(),owner,core,projection.start()))
        known_inputs={'num','powerful'} if core=='genso' and path.name=='mspells3.c' and owner[2]=='cast_monspell_new_aux' else frozenset()
        if code is None:code=conditional_aimed_effects(source,query,owner,core,projections,helpers,known_inputs)
        if code is None and core=='heng':code=heng_native_element_variants(source,query,owner,projections,path)
        if code is None and core=='frox':code=frox_native_element_variants(source,query,owner,projections,path)
        if code is None:code=native_random_beam(source,query,owner,core,projections,path)
        if code is None and core=='heng':code=heng_selected_native_switch(source,query,owner,path,helpers)
        if code is None and core=='heng':code=heng_android_weapon(source,query,owner,projections,path)
        if code is None and core=='genso':code=genso_monspell_values(source,query,owner,projections,path)
        if code is None:code=adjacent_melee(source,query,owner,core,projections)
        if code is None:
            from raw_profiles import aimed_raw_project,first_impact_project
            code=aimed_raw_project(source,query,owner,core,projections)
            if code is None:code=first_impact_project(source,query,owner,core,projections)
        if core=='heng' and path.name=='mind-mirror-master.cpp' and owner[2]=='cast_mirror_spell':
            case=region[2]
            mirror={'case MindMirrorMasterType::SEEKER_RAY:':'AttributeType::SEEKER','case MindMirrorMasterType::SUPER_RAY:':'AttributeType::SUPER_RAY'}.get(case)
            if mirror:
                suffix='seeker_ray' if mirror.endswith('SEEKER') else 'super_ray'
                tail=source.mask[source.ends[query.end()-1]+1:region[1]]
                if not re.search(r'SpellsMirrorMaster\s*\(\s*player_ptr\s*\)\s*\.'+suffix+r'\s*\(\s*dir\s*,',tail):
                    raise ValueError('Heng mirror ray call changed: '+case)
                code=f'hb_target_profile_effect(NATIVE_SHAPE_MIRROR_RAY,static_cast<int>({mirror}),0,0,PROJECT_THRU|PROJECT_BEAM|PROJECT_KILL|PROJECT_GRID|PROJECT_ITEM,0,0,0)'
        if core=='frox' and path.name=='r_dragon.c' and owner[2]=='_split_beam_spell':
            before=source.mask[region[0]:query.start()]
            if not re.search(r'\bget_fire_dir\s*\(',before):
                # The reviewed boundary is the first beam. Its random element
                # affects damage, not the path, and is deliberately not read.
                code='hb_target_profile_effect(NATIVE_SHAPE_BEAM,GF_MISSILE,0,0,PROJECT_THRU|PROJECT_BEAM|PROJECT_KILL|PROJECT_GRID|PROJECT_ITEM,1,0,0)'
        line=source.text.count('\n',0,query.start())+1
        if core not in ('frox','sil','heng','genso','toband'):code=None
        if core=='heng' and path.name in ('blue-magic-breath.cpp','blue-magic-ball-bolt.cpp','blue-magic-spirit-curse.cpp') and query.group(1)=='get_aim_dir':
            entries={
                'cast_blue_magic_breath':('BLUE_MAGIC_BREATH_TABLE','BREATH','bmc_ptr->plev>40?3:2','hb_magic->second.attribute_type','PROJECT_STOP|PROJECT_KILL|PROJECT_GRID|PROJECT_ITEM|PROJECT_BREATH'),
                'cast_blue_magic_ball':('BLUE_MAIGC_BALL_TABLE','BALL','hb_magic->second.radius','hb_magic->second.attribute_type','PROJECT_STOP|PROJECT_KILL|PROJECT_GRID|PROJECT_ITEM'),
                'cast_blue_magic_bolt':('BLUE_MAGIC_BOLT_TABLE','BOLT','0','hb_magic->second.attribute_type','PROJECT_THRU|PROJECT_STOP|PROJECT_KILL|PROJECT_GRID|PROJECT_REFLECTABLE'),
                'cast_blue_magic_spirit_curse':('BLUE_MAGIC_SPIRIT_CURSE_TABLE','BALL','0','hb_magic->second','PROJECT_STOP|PROJECT_KILL|PROJECT_GRID|PROJECT_ITEM|PROJECT_HIDE')}
            if owner[2] in entries:
                unknown=set(re.findall(r'AttributeType::(\w+)',source.mask))-ELEMENTS
                if unknown:raise ValueError('Blue magic table added unreviewed spatial elements: '+str(unknown))
                table,shape,radius,element,flags=entries[owner[2]]
                if table+'.find(bmc_ptr->spell)' not in source.text[owner[0]:owner[1]]:raise ValueError('Blue magic native table lookup changed')
                code=f'([&](){{const auto hb_magic={table}.find(bmc_ptr->spell);if(hb_magic!={table}.end())hb_target_profile_effect(NATIVE_SHAPE_{shape},static_cast<int>({element}),({radius}),0,{flags},0,0,0);}}())'
        if core=='heng' and path.name=='monster-ball.cpp' and owner[2]=='capture_monster' and len(projections)==1:
            projection=projections[0];args=source.args(projection.end()-1)
            if projection.group(1)=='fire_ball' and args==['player_ptr','AttributeType::CAPTURE','dir','0','0','&cap_mon']:
                code=heng_effect_code('fire_ball',args)
        if core=='frox' and owner[2]=='do_cmd_fire_aux1' and path.name=='cmd2.c' and query.group(1)=='get_fire_dir':
            exploding='(object_is_known(arrows)&&arrows->name2==EGO_AMMO_EXPLODING)'
            toggle='weaponmaster_get_toggle()'
            radius=f'(shoot_hack==SP_EXPLODE?(p_ptr->concent+1)/2+1:{exploding}?3:{toggle}==TOGGLE_EXPLODING_BOLT?2+p_ptr->lev/40:0)'
            flags=f'(({exploding}?NATIVE_SHOT_EXPLODE_ON_MISS:0)|((object_is_known(bow)&&bow->name1==ART_TUBER)?NATIVE_SHOT_AVOID_BIRDS:0))'
            code=f'hb_target_profile_effect(NATIVE_SHAPE_SHOT,GF_ARROW,{radius},project_length,{flags},({radius}>0||shoot_hack==SHOOT_PIERCE||{toggle}==TOGGLE_PIERCING_ARROW),shoot_hack,{toggle})'
        if core=='heng' and owner[2]=='exe_fire' and path.name=='shoot.cpp' and query.group(1)=='get_aim_dir':
            code='hb_target_profile_effect(NATIVE_SHAPE_SHOT,static_cast<int>(AttributeType::PLAYER_SHOOT),0,project_length,0,snipe_type==SP_EXPLODE||snipe_type==SP_RUSH||snipe_type==SP_DOUBLE,snipe_type,sniper_concent)'
        if core=='genso' and owner[2]=='do_cmd_fire_aux' and path.name=='cmd2.c' and query.group(1)=='get_aim_dir':
            code='hb_target_profile_effect(NATIVE_SHAPE_SHOT,GF_ARROW,0,project_length,0,snipe_type==SP_EXPLODE||snipe_type==SP_RUSH||snipe_type==SP_DOUBLE,snipe_type,p_ptr->concent)'
        if core=='toband' and owner[2]=='do_cmd_fire_aux' and path.name=='cmd2.c' and query.group(1)=='get_aim_dir':
            # 射撃（銃・ロケット・散弾を含む）。貫通・連射・爆発は最初の当たりのあとの動きなので予測しない（見込み＝ possible で出す）。
            # project_length はこの問いの直前に tdis+1 に決まる。dcfa_thru・penetrate・as_beam・direct は関数の頭で決まった局所変数。
            code=('hb_target_profile_effect(NATIVE_SHAPE_SHOT,GF_MISSILE,0,project_length,dcfa_thru?PROJECT_THRU:0,'
                  '(penetrate>0||as_beam||p_ptr->tval_ammo==TV_ROCKET||p_ptr->tval_ammo==TV_SHELL),penetrate>0||as_beam,direct)')
        if core=='toband' and owner[2]=='do_cmd_throw_aux' and path.name=='cmd2.c' and query.group(1)=='get_aim_dir':
            # 投擲。流れ星（shooting_star）は壁を壊して進むので、形を出さない。
            code=('(shooting_star?(hb_target_profile_plain(NATIVE_TARGET_NEUTRAL),hb_target_profile_known(0),0)'
                  ':(hb_target_profile_effect(NATIVE_SHAPE_THROW,GF_MISSILE,0,project_length,0,0,0,0),0))')
        if core=='sil' and owner[2]=='do_cmd_fire' and query.group(1)=='get_aim_dir' and source.args(query.end()-1)==['&dir','tdis']:
            # Radiance changes every traversed grid. Require a known bow so the
            # preview cannot identify an undiscovered artifact or ego power.
            code='hb_target_profile_effect(NATIVE_SHAPE_SHOT,GF_ARROW,0,tdis,PROJECT_THRU,0,(object_known_p(j_ptr)&&(f2&TR2_RADIANCE))!=0,0)'
        if core=='sil' and owner[2]=='do_cmd_throw' and query.group(1)=='get_aim_dir' and source.args(query.end()-1)==['&dir','tdis']:
            # Native throwing stops at the selected square (unlike archery).
            # tdis is already computed by throwing_range before this query.
            code='hb_target_profile_effect(NATIVE_SHAPE_THROW,GF_HURT,0,tdis,0,0,0,0)'
        if core=='sil' and owner[2]=='play_instrument' and query.group(1)=='get_aim_dir':
            flags='PROJECT_ARC|PROJECT_BOOM|PROJECT_GRID|PROJECT_ITEM|PROJECT_KILL'
            force='hb_target_profile_effect(NATIVE_SHAPE_PUSH,GF_NOTHING,0,3,0,0,0,0)'
            code=f'(!object_aware_p(o_ptr)?(o_ptr->sval==SV_HORN_FORCE?({force},0):0,hb_target_profile_plain(NATIVE_TARGET_NEUTRAL),hb_target_profile_known(0),0):'
            for subtype,element in [('TERROR','FEAR'),('THUNDER','SOUND'),('BLASTING','KILL_WALL')]:
                code+=f'o_ptr->sval==SV_HORN_{subtype}?(hb_target_profile_effect(NATIVE_SHAPE_ARC,GF_{element},3,0,{flags},0,90,0),0):'
            code+='o_ptr->sval==SV_HORN_FORCE?(hb_target_profile_effect(NATIVE_SHAPE_PUSH,GF_NOTHING,0,3,0,0,0,0),0):'
            code+='0)'
        # 変愚 3.0.2.5 は青魔法・ものまねのブレス・ボール・ボルトを表（find_mspell_projection / fire_mspell_projection）へまとめた。
        # 表の行から形を選ぶ式にする（短愚の 3.0.2.4 にはこの形が無いので、ここは通らない）。
        if core=='heng' and path.name=='blue-magic-caster.cpp' and owner[2]=='cast_blue_magic_projection' and query.group(1)=='get_aim_dir':
            body=source.mask[owner[0]:owner[1]]
            if not re.search(r'fire_mspell_projection\(\s*player_ptr\s*,\s*projection\s*,\s*dir\s*,\s*damage\s*,\s*\(\s*bmc_ptr->plev\s*>\s*40\s*\?\s*3\s*:\s*2\s*\)\s*\)',body):
                raise ValueError('Blue magic projection call changed')
            check_mspell_projection_table(path)
            code='([&](){'+heng_mspell_projection_code('&projection','bmc_ptr->plev>40?3:2')+'}())'
        if core=='heng' and path.name=='cmd-mane.cpp' and owner[2]=='use_mane' and query.group(1)=='get_aim_dir' and 'find_mspell_projection' in source.mask[owner[0]:owner[1]]:
            body=source.mask[owner[0]:owner[1]]
            if not re.search(r'const\s+auto\s+projection\s*=\s*find_mspell_projection\(\s*spell\s*\)',body) or \
               not re.search(r'fire_mspell_projection\(\s*player_ptr\s*,\s*\*projection\s*,\s*dir\s*,\s*damage\s*,\s*\(\s*plev\s*>\s*35\s*\?\s*3\s*:\s*2\s*\)\s*\)',body):
                raise ValueError('Imitator projection call changed')
            if code is None:raise ValueError('Imitator native spell profile missing')
            check_mspell_projection_table(path)
            code='([&](){'+heng_mspell_projection_code('projection?&*projection:nullptr','plev>35?3:2')+code+';}())'
        site=f'{core}:{path.name}:{line}'
        if owner[2] in ('do_cmd_open','do_cmd_close','do_cmd_tunnel','do_cmd_disarm','do_cmd_bash','do_cmd_alter','exe_alter','do_cmd_spike','do_cmd_exchange','do_riding','do_cmd_riding'):
            # These original adjacent interactions have no projectile or area
            # forecast. Their direction/target prompt remains neutral.
            code='hb_target_profile_plain(NATIVE_TARGET_NEUTRAL)'
        if owner[2] in ('do_cmd_walk','do_cmd_run'):
            site='movement:'+site
            code='hb_target_profile_plain(NATIVE_TARGET_NEUTRAL)'
        if core=='heng' and owner[2] in ('ObjectZapWandEntity::execute','ObjectZapRodEntity::execute'):
            device='wand' if 'Wand' in owner[2] else 'rod'
            code=f'(item->is_aware()?(hb_target_heng_{device}_profile(this->player_ptr,*item->bi_key.sval(),0,0),0):(hb_target_profile_plain(NATIVE_TARGET_NEUTRAL),hb_target_profile_known(0),0))'
        if core=='heng' and owner[2]=='do_cmd_magic_eater' and query.group(1)=='get_aim_dir':
            kind=re.fullmatch(r'case ItemKindType::(WAND|ROD):',region[2])
            if kind and kind.group(1)=='WAND':code='hb_target_heng_wand_profile(player_ptr,*sval,powerful,1)'
            elif kind and kind.group(1)=='ROD':code='hb_target_heng_rod_profile(player_ptr,*sval,powerful,1)'
        if core=='genso' and owner[2] in ('do_cmd_aim_wand_aux','do_cmd_zap_rod_aux'):
            device='wand' if 'wand' in owner[2] else 'rod'
            code=f'(object_is_aware(o_ptr)?(hb_target_genso_{device}_profile(o_ptr->sval,hb_target_powerful_device(JKF1_MASTER_MAGITEM),FALSE),0):(hb_target_profile_plain(NATIVE_TARGET_NEUTRAL),hb_target_profile_known(0),0))'
        if core=='toband' and owner[2] in ('do_cmd_aim_wand_aux','do_cmd_zap_rod_aux'):
            # TOband の魔法棒・ロッドは do_cmd_*_aux() の頭で品物を調べ、効果は wand_effect()/rod_effect() の switch(sval) に集まる
            # （device_profiles.py が同じ表から hb_target_toband_{wand,rod}_profile を作る）。未鑑定の品物は形を出さない。
            device='wand' if 'wand' in owner[2] else 'rod'
            code=f'(object_is_aware(o_ptr)?(hb_target_toband_{device}_profile(o_ptr->sval,0,0),0):(hb_target_profile_plain(NATIVE_TARGET_NEUTRAL),hb_target_profile_known(0),0))'
        effect_profile=code
        if ui_only and code and not site.startswith('movement:') and not (core=='sil' and owner[2]=='play_instrument'):
            code='hb_target_profile_plain(NATIVE_TARGET_ATTACK)'
        audit.append({'site':site,'function':owner[2],'query':query.group(1),'supported':code is not None,'profile':code})
        if ui_only:audit[-1]['effect_profile']=effect_profile
        prefix=f'hb_target_profile_begin("{site}"), '
        if core=='frox' and path.name=='devices.c':
            known='('+code+', 0)' if code else '0'
            prefix+=f'(device_known?{known}:(hb_target_profile_plain(NATIVE_TARGET_NEUTRAL),hb_target_profile_known(0),0)), '
        elif code:prefix+=code+', '
        end=source.ends[query.end()-1]+1
        original=source.text[query.start():end]
        edits.append((query.start(),end,'('+prefix+original+')'))
    if not ui_only and menu_audit is not None:
        from menu_profiles import injections
        menu_edits,rows=injections(source,core,helpers,path.name);edits.extend(menu_edits);menu_audit.extend(rows)
    if core in ('heng','genso','frox','toband') and helpers is not None:
        from device_profiles import injections as device_injections
        device_edits,device_rows=device_injections(source,core,helpers,path.name);edits.extend(device_edits)
        if menu_audit is not None:
            for row in device_rows:row['device']=True
            menu_audit.extend(device_rows)
    text=source.text
    for start,end,replacement in sorted(edits,reverse=True):text=text[:start]+replacement+text[end:]
    if edits:
        newline='\r\n' if '\r\n' in text else '\n'
        text='#include "bridge/native_targeting_api.h"'+newline+text
        if core=='heng':
            text='#include "effect/attribute-types.h"'+newline+'#include "effect/effect-characteristics.h"'+newline+text
    return text.encode('latin1'),audit
