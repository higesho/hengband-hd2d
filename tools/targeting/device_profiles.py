"""Derive device previews from their native effect switches, before use or RNG."""
import re
from profile_inventory import PROJECT,CALL
from pure_expressions import NumericScope
from menu_profiles import function_regions,self_effect_call
from projection_helpers import literal_flags

def injections(source,core,helpers,path_name):
    if core=='frox':return frox_injections(source,helpers,path_name)
    if core not in ('heng','genso'):return [],[]
    if core=='heng' and path_name not in ('cmd-zapwand.cpp','cmd-zaprod.cpp','cmd-usestaff.cpp'):return [],[]
    if core=='genso' and path_name!='cmd6.c':return [],[]
    from profiles import effect_code,heng_effect_code,ELEMENTS
    edits=[];audit=[]
    for owner in source.functions:
        name=owner[2]
        if name not in ('wand_effect','rod_effect','staff_effect'):continue
        kind=name.removesuffix('_effect');cases=[];body='';self_cases=[];self_body=''
        for labels,start,end in function_regions(source,owner,'sval'):
            calls=list(PROJECT.finditer(source.mask,start,end));matcher=helpers.matcher()
            if matcher:
                present={c.start() for c in calls}
                calls.extend(c for c in matcher.finditer(source.mask,start,end) if c.start() not in present)
            code=None
            fixed=None
            if len(calls)==1 and not list(CALL.finditer(source.mask,start,end)):
                call=calls[0]
                numeric=NumericScope(source,call.start(),owner,core,known_inputs={'powerful','magic','sval'})
                fixed=self_effect_call(source,call,owner,core,helpers,numeric)
                if fixed:
                    guards=[]
                    for condition in source.conditions(call.start(),owner):
                        guard=numeric.expand(condition)
                        if guard is None:fixed=None;break
                        guards.append('('+guard+')')
                    if fixed and guards:fixed='if('+ '&&'.join(guards)+'){'+fixed+';}'
                    elif fixed:fixed+=';'
            if not fixed and core=='genso' and labels==['SV_STAFF_STARLITE']:
                from self_profiles import scatter_plan,scatter_code
                scatter=scatter_plan(source,start,end,owner,core)
                if scatter is None:raise ValueError('Native Genso staff starlight changed')
                fixed=scatter_code(scatter,core)+';'
            self_cases.extend({'case':label,'profile':fixed} for label in labels)
            if fixed:self_body+=''.join('    case '+label+':\n' for label in labels)+'        '+fixed+' break;\n'
            # Powerful disarming/destruction also has a self-centred effect.
            # Keep that unresolved variant explicit until its composite model
            # is connected, while retaining ordinary device previews.
            normal_only=bool(re.search(r'\b(?:disarm_traps_touch|destroy_doors_touch)\s*\(',source.mask[start:end]))
            selected=[c for c in calls if not c.group(1).endswith('_touch')] if normal_only else calls
            if len(selected)==1 and not list(CALL.finditer(source.mask,start,end)):
                call=selected[0];projection=call.group(1);args=source.args(call.end()-1)
                expanded=helpers.expand(projection,args)
                if expanded:projection,args=expanded
                numeric=NumericScope(source,call.start(),owner,core,known_inputs={'powerful','magic','sval'})
                values=list(args)
                # Only geometry values are expanded. Damage expressions stay
                # in the original action and never enter this function.
                indices=[]
                if projection in ('fire_ball','fire_ball_hide','fire_ball_aux','fire_rocket','fire_breath','fire_ball_jump','fire_spark'):indices=[4 if core=='heng' else 3]
                elif projection=='fire_bolt_or_beam':indices=[1 if core=='heng' else 0]
                good=True
                for index in indices:
                    if index>=len(values):good=False;break
                    value=numeric.expand(values[index])
                    if value is None:good=False;break
                    values[index]=value
                if good:
                    code=(heng_effect_code if core=='heng' else effect_code)(projection,values,numeric)
                    if not code and labels==['SV_WAND_DRAGON_BREATH']:
                        original=source.mask[start:end]
                        if core=='heng':
                            match=re.search(r'constexpr\s+std::array<std::pair<int,\s*AttributeType>,\s*(\d+)>\s+candidates\s*=\s*\{\s*\{(.*?)\}\s*\};',original,re.S)
                            elements=re.findall(r'\{\s*\d+\s*,\s*(AttributeType::\w+)\s*\}',match.group(2)) if match else []
                            valid=match and len(elements)==int(match.group(1)) and 'rand_choice(candidates)' in original
                        else:
                            match=re.search(r'\bswitch\s*\(randint1\((\d+)\)\)\s*\{',original)
                            elements=re.findall(r'\btyp\s*=\s*(GF_\w+)\s*;',original)
                            valid=match and len(elements)==int(match.group(1)) and elements and 'default:' in original
                        if not valid or any(e.removeprefix('AttributeType::').removeprefix('GF_') not in ELEMENTS for e in elements):
                            raise ValueError('Native dragon wand element table needs review')
                        plans=[]
                        for element in elements:
                            variant=list(values);variant[1 if core=='heng' else 0]=element
                            plan=(heng_effect_code if core=='heng' else effect_code)(projection,variant,numeric,'1')
                            if plan is None:break
                            plans.append(plan)
                        if len(plans)==len(elements):code=', '.join(plans)
                if code:
                    guards=[]
                    for condition in source.conditions(call.start(),owner):
                        guard=numeric.expand(condition)
                        if guard is None:code=None;break
                        guards.append('('+guard+')')
                    if normal_only:guards.append('!powerful')
                    if code and guards:code='if('+ '&&'.join(guards)+'){'+code+';}'
                    elif code:code+=';'
            cases.extend({'case':label,'profile':code} for label in labels)
            if code:
                body+=''.join('    case '+label+':\n' for label in labels)+'        '+code+' break;\n'
        variants=[(kind,body,cases)] if kind=='wand' else [(kind+'_self' if kind=='rod' else kind,self_body,self_cases)]
        if kind=='rod':variants.append((kind,body,cases))
        generated=''
        for suffix,body,entries in variants:
            symbol=f'hb_target_{core}_{suffix}_profile'
            if core=='heng':
                code=f'\nvoid {symbol}(void *opaque,int sval,int powerful,int magic)\n{{\n    auto *player_ptr=static_cast<PlayerType *>(opaque);\n    (void)player_ptr;(void)powerful;(void)magic;\n'
            else:code=f'\nstatic void {symbol}(int sval,int powerful,int magic)\n{{\n    (void)powerful;(void)magic;\n'
            code+='    switch(sval){\n'+body+'    default:break;\n    }\n}\n';generated+=code
            audit.append({'core':core,'function':symbol,'cases':entries})
        edits.append((owner[1]+1,owner[1]+1,generated))
    return edits,audit

def frox_injections(source,helpers,path_name):
    if path_name!='devices.c':return [],[]
    owner=next((f for f in source.functions if f[2]=='do_effect'),None)
    extra=next((f for f in source.functions if f[2]=='_extra'),None)
    if owner is None or extra is None:raise ValueError('Frox device effect functions changed')
    if re.sub(r'\s+','',source.mask[extra[0]+1:extra[1]])!='intresult=effect->extra;if(!result)result=def;returnresult;':
        raise ValueError('Frox device radius override needs review')
    body='';cases=[]
    class DeviceNumeric(NumericScope):
        def expand(self,value):
            match=re.fullmatch(r'_extra\(effect,\s*(\d+)\)',value)
            if match:return '(effect->extra?effect->extra:'+match.group(1)+')'
            return super().expand(value)
    for labels,start,end in function_regions(source,owner,'effect->type'):
        calls=list(PROJECT.finditer(source.mask,start,end));matcher=helpers.matcher()
        if matcher:
            present={c.start() for c in calls}
            calls.extend(c for c in matcher.finditer(source.mask,start,end) if c.start() not in present)
        fixed=None
        if len(calls)==1 and not list(CALL.finditer(source.mask,start,end)):
            call=calls[0]
            if not re.search(r'\b(?:for|while|do)\b',source.mask[start:call.start()]):
                numeric=DeviceNumeric(source,call.start(),owner,'frox')
                fixed=self_effect_call(source,call,owner,'frox',helpers,numeric)
                if fixed:
                    guards=[]
                    for condition in source.conditions(call.start(),owner):
                        if re.sub(r'\s+','',condition)=='cast':continue
                        guard=numeric.expand(condition)
                        if guard is None:fixed=None;break
                        guards.append('('+guard+')')
                    if fixed and guards:fixed='if('+ '&&'.join(guards)+'){'+fixed+';}'
                    elif fixed:fixed+=';'
        if not fixed and labels==['EFFECT_STARLITE']:
            from self_profiles import scatter_plan,scatter_code
            scatter=scatter_plan(source,start,end,owner,'frox')
            if scatter is None:raise ValueError('Native Frox device starlight changed')
            fixed=scatter_code(scatter,'frox')+';'
        cases.extend({'case':label,'profile':fixed} for label in labels)
        if fixed:body+=''.join('    case '+label+':\n' for label in labels)+'        '+fixed+' break;\n'
    code='\nvoid fc_target_device_profile(const void *source)\n{\n    const effect_t *effect=(const effect_t *)source;\n    switch(effect->type){\n'+body+'    default:break;\n    }\n}\n'
    return [(owner[1]+1,owner[1]+1,code)],[{'core':'frox','function':'fc_target_device_profile','cases':cases}]
