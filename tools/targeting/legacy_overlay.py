"""Byte-preserving input hooks for legacy cores. Gameplay tails are retained."""
import re
import line_anchor as anchor

# Hashes of original lines (see line_anchor.py); the original text is not quoted here.
H={
    'genso_target_set':'091d0251c710','genso_tgt_pt':'fdb05aedc7f0','heng_target_set':'0c3109a0f107',
    'heng_point_target':'8578fd904816','sil_target_set':'233e56e0583e','open_brace':'021fb596db81',
}
def after_header(text,key,new,what):
    """Insert `new` right after the function header line named by H[key] and its opening brace."""
    return anchor.insert_after(text,[H[key],H['open_brace']],new,what)

def genso_xtra(source):
    newline='\r\n' if b'\r\n' in source else '\n'
    text=source.decode('latin1').replace('\r\n','\n')
    def once(old,new):
        nonlocal text
        if text.count(old)!=1:raise ValueError('Genso hook changed: '+old[:90])
        text=text.replace(old,new,1)
    once('#include "angband.h"','#include "angband.h"\n#include "bridge/native_targeting_api.h"\nstatic int _gb_choose_target(int mode,int eight_way,int *direction,int *x,int *y);\nstatic int _gb_target_movement_pending(void);')
    for name,signature,eight in [('get_aim_dir','int *dp',0),('get_rep_dir','int *dp, bool under',2),('get_rep_dir2','int *dp',1),('get_hack_dir','int *dp',0)]:
        start=text.index('bool '+name+'('+signature+')\n{');stop=text.index('\n}',start)
        section=text[start:stop];old='\twhile (!dir)'
        if section.count(old)!=1:raise ValueError('Genso input loop changed: '+name)
        mode='under ? 2 : 1' if eight==2 else str(eight)
        condition='(!dir || dir == 5)' if not eight else '!dir'
        hook=f'\tif (hb_target_enabled() && {condition} && !_gb_target_movement_pending())\n\t{{\n\t\tif (!_gb_choose_target(TARGET_KILL,{mode},&dir,NULL,NULL)) dir=0;\n\t}}\n\telse while (!dir)'
        text=text[:start]+section.replace(old,hook)+text[stop:]
    text=after_header(text,'genso_target_set','    if (hb_target_enabled() && !(mode & TARGET_LOOK)) {\n        int dir;hb_target_profile_begin("genso:standalone-target");hb_target_profile_plain(NATIVE_TARGET_NEUTRAL);\n        return _gb_choose_target(mode,0,&dir,NULL,NULL);\n    }','Genso target_set')
    text=after_header(text,'genso_tgt_pt','    if (hb_target_enabled()) {int dir;return _gb_choose_target(0,0,&dir,x_ptr,y_ptr);}','Genso tgt_pt')
    text+='\n#include "gb_targeting_geometry.inl"\n#include "gb_targeting_path.generated.inl"\n#include "gb_targeting.inl"\n#include "gb_targeting_menu.inl"\n'
    return text.replace('\n',newline).encode('latin1')

def heng_cpp(source,kind):
    newline='\r\n' if b'\r\n' in source else '\n'
    text=source.decode('latin1').replace('\r\n','\n')
    header={'getter':'target/target-getter.h','setter':'target/target-setter.h','point':'target/grid-selector.h'}[kind]
    old='#include "'+header+'"'
    if text.count(old)!=1:raise ValueError('Hengband include changed: '+header)
    text=text.replace(old,old+'\n#include "bridge/hengband_targeting.h"',1)
    if kind=='getter':
        for name,signature,mode in [('get_aim_dir','PlayerType *player_ptr, bool enable_repeat','0'),('get_direction','PlayerType *player_ptr','1'),('get_rep_dir','PlayerType *player_ptr, bool under','under ? 2 : 1')]:
            start=text.index('Direction '+name+'('+signature+')\n{');stop=text.index('\n}',start)
            section=text[start:stop];old='    while (!dir) {'
            if section.count(old)!=1:raise ValueError('Hengband input loop changed: '+name)
            condition='(!dir || dir.dir() == 5)' if mode=='0' else '(!dir && !hb_heng_mapped_direction())'
            cancel_prefix=('project_length='+'0; ') if mode=='0' else ''
            hook=f'    if (hb_target_enabled() && {condition}) {{\n        dir=hb_heng_choose_direction(player_ptr,{mode});\n        if (!dir) {{ {cancel_prefix}return Direction::none(); }}\n    }} else while (!dir) {{'
            text=text[:start]+section.replace(old,hook)+text[stop:]
    elif kind=='setter':
        text=after_header(text,'heng_target_set','    if (hb_target_enabled() && !(mode & TARGET_LOOK)) {\n        hb_target_profile_begin("heng:standalone-target");hb_target_profile_plain(NATIVE_TARGET_NEUTRAL);\n        return hb_heng_choose_target(player_ptr);\n    }','Hengband target_set')
    else:
        text=after_header(text,'heng_point_target','    if (hb_target_enabled()) return hb_heng_choose_point(player_ptr);','Hengband point_target')
    return text.replace('\n',newline).encode('latin1')

def sil_xtra(source):
    newline='\r\n' if b'\r\n' in source else '\n'
    text=source.decode('latin1').replace('\r\n','\n')
    old='#include "angband.h"'
    if text.count(old)!=1:raise ValueError('Sil include changed')
    text=text.replace(old,old+'\n#include "bridge/native_targeting_api.h"\nstatic int _sq_choose_target(int mode,int range,int *direction);\nextern int sq_target_mapped_direction(void);',1)
    for name,signature,mode,rng in [('get_aim_dir','int* dp, int range',0,'range'),('get_rep_dir','int* dp',1,'0')]:
        start=text.index('bool '+name+'('+signature+')\n{');stop=text.index('\n}',start)
        section=text[start:stop];old='    while (!dir)'
        if section.count(old)!=1:raise ValueError('Sil input loop changed: '+name)
        if name=='get_aim_dir':
            first_return=section.index('            return (TRUE);')
            section=section[:first_return]+section[first_return:].replace('            return (TRUE);','            if (!hb_target_enabled()) return (TRUE);',1)
        condition='(!dir || dir == 5)' if not mode else '(!dir && !sq_target_mapped_direction())'
        hook=f'    if (hb_target_enabled() && {condition}) {{\n        if (!_sq_choose_target({mode},{rng},&dir)) dir=0;\n    }} else while (!dir)'
        text=text[:start]+section.replace(old,hook)+text[stop:]
    text=after_header(text,'sil_target_set','    if (hb_target_enabled() && !(mode & (TARGET_LOOK|TARGET_WIZ))) {\n        int dir;hb_target_profile_begin("sil:standalone-target");hb_target_profile_plain(NATIVE_TARGET_NEUTRAL);\n        return _sq_choose_target(0,range,&dir);\n    }','Sil standalone target')
    text+='\n#include "sq_targeting.inl"\n'
    return text.replace('\n',newline).encode('latin1')

def sil_path(source):
    text=source.decode('latin1').replace('\r\n','\n')
    start=re.search(r'^int project_path\(\n',text,re.M).start();stop=text.index('\n}',start)+2
    function=text[start:stop].replace('project_path(','sq_target_project_path(',1)
    function=re.sub(r'cave_info\[([^\]]+)\]\[([^\]]+)\]',r'sq_target_known_info(\1,\2)',function)
    function=re.sub(r'cave_feat\[([^\]]+)\]\[([^\]]+)\]',r'sq_target_known_feat(\1,\2)',function)
    function=re.sub(r'cave_m_idx\[([^\]]+)\]\[([^\]]+)\]',r'sq_target_visible_monster(\1,\2)',function)
    function=function.replace('cave_floor_bold(', 'sq_target_grid_open(')
    marker='    /* Accept last grid as the new endpoint */'
    if function.count(marker)!=1:raise ValueError('Sil projection endpoint changed')
    function=function.replace(marker,'    if (step<=0) return 0;\n'+marker)
    los_start=re.search(r'^bool los\(int y1, int x1, int y2, int x2\)\n',text,re.M).start();los_stop=text.index('\n}',los_start)+2
    los_function=text[los_start:los_stop].replace('los(', 'sq_target_los(',1).replace('cave_floor_bold(', 'sq_target_grid_open(')
    return ('/* Native vinfo traversal and LOS; predicates only expose known terrain and visible creatures. */\n'+function+'\n'+los_function+'\n').encode('latin1')
