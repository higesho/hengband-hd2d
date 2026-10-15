"""Build a reproducible source overlay; never modify the imported core tree.

Every replacement must match once. Generated files preserve original encoding
and line endings. The manifest records input/output hashes for build auditing.
"""
from pathlib import Path
import argparse, hashlib, json, re
from profiles import annotate
from legacy_overlay import genso_xtra,heng_cpp,sil_xtra,sil_path
from heng_geometry import generate as heng_geometry
from genso_geometry import generate as genso_geometry
from projection_helpers import load_helpers
from frox_menu import prepare as frox_menu
from item_display import prepare as item_display
from book_menu import prepare as book_menu
import line_anchor as anchor
import attack_fx_overlay as attack_fx  # 攻撃の視覚効果の観測（幻想蛮怒・Frox・Sil-Q）
import native_fx_overlay  # 攻撃の視覚効果の観測（Umoria・Rogue 3.6・Rogue 5.4・RogueClone2。patched-src の写しに足す）

# Hashes of original lines (see line_anchor.py). The original text itself is not quoted here.
ANCHOR = {
    'fire_dir': '2b915581a575', 'aim_dir': '1646ee2901b3', 'target_set': '091d0251c710', 'tgt_pt': '0049d1853663',
    'reserve_decl': '54a40608b9c0', 'hide_decl': '5674728eefd0', 'case_R': '2a8d8cc4ddfc', 'case_u': 'c1f95355324c',
    'examine': 'fa432aa16ae1', 'hidden_if': '5c17135b694c', 'ct_line': 'b55a88b2cceb',
    'ret_pos': '1bd538d81917', 'ret_max': '71620a133ee3', 'impact_x': 'f3bdc7e1204e', 'impact_y': '1056bf39cdfb',
    'count_def': '2d37ded1a3b3', 'u_if': 'ab13f89634c4', 'u_show': 'cd297379a0b6', 'u_else': '7dd530c4d36d', 'u_hide': 'e8b65b8586fd',
    'used_old': '633d85721d3d', 'top1': 'edebe3302494', 'slot_page': 'f8205fc185ea', 'obj_slot': 'ce7b03ad4d4e',
    'obj_null_ret': 'fbb5c1daf672', 'desc_name': 'c64765594fe9', 'obj_inv': 'e3a2ba1e613e',
    'quake_frox': 'c3b4598df430', 'quake_genso': 'f7a554e10a1a', 'quake_heng': '801823703ca9',
    'unlite_heng': '501c2ee88078', 'unlite_other': '5b9d3c0922ef', 'wand_aux': '84c4b3afbdc9',
    'positions_empty': '46bfeb9afcc4', 'knock_x': '3a22fb170808',
    # 変愚 3.0.2.5 の display_store_inventory() の宣言の行と、その次の開き括弧の行
    'store_inventory_sig': '3501d5c40760', 'open_brace': '021fb596db81',
    # 攻撃の視覚効果の記録（射撃の命中・投擲の 1 歩・投擲の命中・ブーメランの戻り）
    'shot_fear': '990c0f364103', 'throw_step': 'ea493bea90aa', 'throw_hit': 'ecfed16e56b9', 'throw_return': 'ea7b462c1e1d',
    # 状態のアイコン（prt_status() の「長さを数える」コメントの行。幻想蛮怒・Frox の xtra1.c に 1 本ずつある）
    'status_bar_genso': '3b8447ea713e', 'status_bar_frox': 'a4d63c260a3b',
    # 短愚（変愚 3.0.2.4 系）の print_status() の「長さを数える」手前の行（TERM_LEN col = 0, num = 0;）
    'status_bar_tang': 'ccab7cfcf9a3',
}

def digest(value):
    return hashlib.sha256(value).hexdigest()

# 攻撃の視覚効果の記録（決めたこと 2026-10-08）。
# 記録するだけで、原作の変数・乱数・画面には触れない（presentation/bridge/attack_fx_recorder.h に積む）。
# 変愚（3.0.2.5）と短愚（3.0.2.4）の両方に同じ書き方で当たる。
# project()：経路（path_g の先頭 path_n マス）と効果範囲（positions）が決まった直後。
FX_PROJECT=('    {std::vector<int> hb_pa,hb_ar;int hb_i=0;'
    'for(const auto &hb_p:path_g){if(hb_i++>=path_n)break;hb_pa.push_back(hb_p.y);hb_pa.push_back(hb_p.x);}'
    'for(const auto &[hb_d,hb_q]:positions){hb_ar.push_back(hb_d);hb_ar.push_back(hb_q.y);hb_ar.push_back(hb_q.x);}'
    'hb_attack_fx_projection(src_idx,pos_source.y,pos_source.x,pos_impact.y,pos_impact.x,static_cast<int>(typ),rad,static_cast<int>(flag),'
    'is_blind?1:0,breath?1:0,hb_pa.data(),static_cast<int>(hb_pa.size()/2),hb_ar.data(),static_cast<int>(hb_ar.size()/3));}')
# 射撃：1 歩ごと。1 歩目だけ、属性付き（炎の矢など）かを調べる。
# 原作より情報を増やさないため、属性は「識別済みの旗」（get_flags_known）と、プレイヤ自身が選んだスナイパーの技（snipe_type）からだけ決める。
# 未識別の矢弾・弓の属性は見せない（shot_attribute() は未識別の旗も見るので使わない）。理力は原作と同じ MP の条件（csp > msp / 30）も見る。
FX_SHOT=('\n            {int hb_brand=-1;if(cur_dis==1){const auto hb_kf=j_ptr->get_flags_known()|fire_item.get_flags_known();'
    'if(snipe_type==SP_LITE)hb_brand=static_cast<int>(AttributeType::LITE);'
    'else if(snipe_type==SP_FIRE)hb_brand=static_cast<int>(AttributeType::FIRE);'
    'else if(snipe_type==SP_COLD)hb_brand=static_cast<int>(AttributeType::COLD);'
    'else if(snipe_type==SP_ELEC)hb_brand=static_cast<int>(AttributeType::ELEC);'
    'else if(snipe_type==SP_KILL_WALL)hb_brand=static_cast<int>(AttributeType::KILL_WALL);'
    'else if(snipe_type==SP_FINAL)hb_brand=static_cast<int>(AttributeType::MANA);'
    'else if(hb_kf.has(TR_BRAND_FIRE))hb_brand=static_cast<int>(AttributeType::FIRE);'
    'else if(hb_kf.has(TR_BRAND_COLD))hb_brand=static_cast<int>(AttributeType::COLD);'
    'else if(hb_kf.has(TR_BRAND_ELEC))hb_brand=static_cast<int>(AttributeType::ELEC);'
    'else if(hb_kf.has(TR_BRAND_ACID))hb_brand=static_cast<int>(AttributeType::ACID);'
    'else if(hb_kf.has(TR_BRAND_POIS))hb_brand=static_cast<int>(AttributeType::POIS);'
    'else if(hb_kf.has(TR_FORCE_WEAPON)&&player_ptr->csp>(player_ptr->msp/30))hb_brand=static_cast<int>(AttributeType::MANA);}'
    'hb_attack_fx_shot(cur_dis,x,y,prev_y,prev_x,fire_item.bi_id,static_cast<int>(fire_item.bi_key.tval()),hb_brand);}')

# 状態のアイコン（決めたこと 2026-10-11）。
# 幻想蛮怒と Frox の prt_status() は static で、旗の配列 bar_flags と名札の表 bar[] を関数の中で組む。
# 旗が出そろった所（長さを数える前）に 1 行足し、立っている旗の番号・名札・色をアダプタへ渡す。
# 記録するだけで、旗にも画面にも乱数にも触れない。名札は原作が実際に画面へ出す字（言語の切り替え・
# 実行中に書き換わる札を含む）をそのまま渡すので、アダプタに名前の表を持たない。
# 呼び先（gb_status_record / fc_status_record）はアダプタの gb_shim.c / fc_shim.c にある。
STATUS_BAR_HOOK={
    'genso':('\t{extern void gb_status_record(int,const char *,int);int hb_i;gb_status_record(-1,"",(int)(sizeof(bar)/sizeof(bar[0]))-1);'
        'for(hb_i=0;bar[hb_i].sstr;hb_i++)if(IS_FLG(hb_i))gb_status_record(hb_i,BAR_LSTR(hb_i),bar[hb_i].attr);}'),
    'frox':('    {extern void fc_status_record(int,const char *,int);int hb_i;fc_status_record(-1,"",(int)(sizeof(bar)/sizeof(bar[0]))-1);'
        'for(hb_i=0;bar[hb_i].sstr;hb_i++)if(IS_FLG(hb_i))fc_status_record(hb_i,fc_bar(bar[hb_i].lstr),bar[hb_i].attr);}'),
}

def observe_status_bars(raw,core):
    """prt_status() の旗が出そろった所へ、状態の記録の呼び出しを 1 行足す（幻想蛮怒は genso、Frox は frox）。"""
    text=raw.decode('latin1');newline='\r\n' if '\r\n' in text else '\n';normalized=text.replace('\r\n','\n')
    normalized=anchor.insert_before(normalized,ANCHOR['status_bar_'+core],STATUS_BAR_HOOK[core],core+' native status bar observation site')
    return normalized.replace('\n',newline).encode('latin1')

# 短愚（変愚 3.0.2.4 系）の状態のアイコン（決めたこと 2026-10-11）。
# 短愚の print_status()（window/main-window-stat-poster.cpp）は、旗の配列 bar_flags を関数の中で組む。
# 旗が出そろった所（長さを数える前）に 1 行足し、配列そのものをアダプタ側（presentation/bridge/presentation_bridge.cpp）
# へ渡す。記録するだけで、旗にも画面にも乱数にも触れない。変愚（3.0.2.5）には当てない（変愚は原作の
# collect_status_bar_flags を呼べる）。
TANG_STATUS_HOOK=('    {extern void hb_status_flags_record(const BIT_FLAGS *,int);'
    'hb_status_flags_record(bar_flags,(int)(sizeof(bar_flags)/sizeof(bar_flags[0])));}')

# 装備の呪いの印（常時の効果の「呪い」）。短愚の process_cursed_equipment_characteristics() は static で、結果を
# 関数内の構造体 char_stat に入れる。既存の行は変えず、display-characteristic.cpp の末尾に「同じ関数を呼んで
# 耐性（+）・免疫（*）の有無を返すだけの関数」を足す（幻想蛮怒の gb_native_player_flags と同じやり方）。
TANG_CURSE_EXPORT=('void hb_equipment_curse_flags(PlayerType *player_ptr,bool *has_res,bool *has_imm);'
    'void hb_equipment_curse_flags(PlayerType *player_ptr,bool *has_res,bool *has_imm)'
    '{char_stat st;process_cursed_equipment_characteristics(player_ptr,0,st);*has_res=st.has_res;*has_imm=st.has_imm;}')

def observe_tang_status(raw):
    """短愚の print_status() に旗の記録を 1 行足す。"""
    text=raw.decode('latin1');newline='\r\n' if '\r\n' in text else '\n';normalized=text.replace('\r\n','\n')
    normalized=anchor.insert_before(normalized,ANCHOR['status_bar_tang'],TANG_STATUS_HOOK,'tangband native status bar observation site')
    return normalized.replace('\n',newline).encode('latin1')

def export_tang_curse_flags(raw):
    """display-characteristic.cpp の末尾に、static な process_cursed_equipment_characteristics を呼ぶだけの関数を足す。"""
    newline=b'\r\n' if b'\r\n' in raw else b'\n'
    if b'process_cursed_equipment_characteristics' not in raw:raise ValueError('tangband curse characteristic function changed')
    if not raw.endswith(b'\n'):raw+=newline
    return raw+TANG_CURSE_EXPORT.encode('latin1')+newline

# 幻想蛮怒の常時の効果（状態のアイコン）。C 画面の耐性・能力の表が使う
# player_flags() ほか 4 つ（種族・職業・変身などの固有の旗）は files.c の static 関数で、外から呼べない。
# 既存の行は 1 行も変えず、files.c の末尾に「同じ関数を呼ぶだけの関数」を 1 つ足す（util.c の末尾に足す
# gb_target_mapped_direction と同じやり方）。旗の判定にも画面にも乱数にも触れない。
GENSO_PLAYER_FLAGS_EXPORT=('void gb_native_player_flags(u32b *player,u32b *tim,u32b *imm,u32b *tim_imm)'
    '{player_flags(player);tim_player_flags(tim);player_immunity(imm);tim_player_immunity(tim_imm);}')

def export_genso_player_flags(raw):
    """files.c の末尾に、static な player_flags 群を呼ぶだけの関数 gb_native_player_flags を足す。"""
    newline=b'\r\n' if b'\r\n' in raw else b'\n'
    if not raw.endswith(b'\n'):raw+=newline
    return raw+GENSO_PLAYER_FLAGS_EXPORT.encode('latin1')+newline

def observe_shot_hit(raw):
    """射撃が当たった（test_hit_fire が真）所で、記録に「当たった」の印を付ける。"""
    text=raw.decode('latin1');newline='\r\n' if '\r\n' in text else '\n';normalized=text.replace('\r\n','\n')
    normalized=anchor.insert_after(normalized,ANCHOR['shot_fear'],'                    hb_attack_fx_hit();','Native arrow hit observation site')
    return normalized.replace('\n',newline).encode('latin1')

def observe_throw(raw):
    """投擲の 1 歩・当たった・戻る、の 3 か所。経路の配列 ny/nx が private なので、投擲を実装するクラスの中で記録する。"""
    text=raw.decode('latin1');newline='\r\n' if '\r\n' in text else '\n';normalized=text.replace('\r\n','\n')
    normalized=anchor.insert_after(normalized,ANCHOR['throw_step'],'    hb_attack_fx_throw(this->cur_dis,this->x,this->y,this->prev_y,this->prev_x,this->q_ptr->bi_id,this->return_when_thrown?1:0);','Native throw step observation site')
    normalized=anchor.insert_before(normalized,ANCHOR['throw_hit'],'    hb_attack_fx_hit();','Native throw hit observation site')
    normalized=anchor.insert_before(normalized,ANCHOR['throw_return'],'    hb_attack_fx_throw_return();','Native throw return observation site')
    if '#include "bridge/native_targeting_api.h"' not in normalized:normalized='#include "bridge/native_targeting_api.h"\n'+normalized
    return normalized.replace('\n',newline).encode('latin1')

def observe_shot_path(raw,core):
    text=raw.decode('latin1')
    if core=='heng':
        marker=None
        trace='\n            hb_target_trace_area(static_cast<int>(AttributeType::PLAYER_SHOOT),x,y);'+FX_SHOT
    elif core=='frox':
        marker='            /* Save the new location */\n            x = nx;\n            y = ny;'
        trace='\n            hb_target_trace_area(GF_ARROW,x,y);'
    else:
        marker='\t\t/* Save the new location */\n\t\tx = nx;\n\t\ty = ny;'
        trace='\n\t\thb_target_trace_area(GF_ARROW,x,y);'
    newline='\r\n' if '\r\n' in text else '\n';normalized=text.replace('\r\n','\n')
    if marker is None:
        try:normalized=anchor.insert_after(normalized,[ANCHOR['impact_x'],ANCHOR['impact_y']],trace[1:],'heng arrow path')
        except ValueError:raise ValueError('Native arrow path observation site changed: '+core)
        return normalized.replace('\n',newline).encode('latin1')
    if normalized.count(marker)!=1:raise ValueError('Native arrow path observation site changed: '+core)
    return normalized.replace(marker,marker+trace).replace('\n',newline).encode('latin1')

def observe_store_screen(raw):
    """変愚 3.0.2.5 の店の一覧を描く関数の頭で、描いた瞬間の値を画面寄りのコードへ写す（presentation/bridge/store_screen_snapshot.h）。

    3.0.2.5 は店の大域変数をやめ、表示中のページなどを StoreScreen にまとめたので、外からはここでしか読めない。
    短愚の 3.0.2.4 には StoreScreen が無い（関数の形が違う）ので差さない。3.0.2.5 の形なのに宣言の行が
    見つからなければ止める（原作が変わったので見直す）。
    """
    text=raw.decode('latin1')
    if 'StoreScreen' not in text:
        return raw
    newline='\r\n' if '\r\n' in text else '\n';text=text.replace('\r\n','\n')
    hook='    hb_store_screen_drawn(screen.get_store(), screen.get_town_index(), screen.get_page_top(), screen.get_page_size(), screen.get_page_item_count());'
    text=anchor.insert_after(text,[ANCHOR['store_inventory_sig'],ANCHOR['open_brace']],hook,'heng store inventory display site')
    return ('#include "bridge/store_screen_snapshot.h"\n'+text).replace('\n',newline).encode('latin1')

def observe_earthquake(raw,core):
    markers={
        'frox':('quake_frox','hb_target_trace_area(-2,xx,yy);'),
        'genso':('quake_genso','hb_target_trace_area(-2,xx,yy);'),
        'heng':('quake_heng','if(m_idx==0)for(const auto &pos:pos_collapses)hb_target_trace_area(-2,pos.x,pos.y);')}
    marker,trace=markers[core]
    text=anchor.append_to_line(raw.decode('latin1'),ANCHOR[marker],' '+trace,core+' native earthquake observation site')
    return b'#include "bridge/native_targeting_api.h"\n'+text.encode('latin1')

def observe_genso_sight_area(raw):
    text=raw.decode('latin1');newline='\r\n' if '\r\n' in text else '\n'
    normalized=text.replace('\r\n','\n');body=native_function(raw,'project_hack2')
    marker='\tTerm_fresh();'
    trace='\tfor(y=1;y<cur_hgt-1;++y)for(x=1;x<cur_wid-1;++x)if(flag_in_sight[y][x])hb_target_trace_area(-3,x,y);\n'
    if marker not in body:raise ValueError('Genso frozen sight observation site changed')
    return normalized.replace(body,body.replace(marker,trace+marker,1),1).replace('\n',newline).encode('latin1')

def observe_room_area(raw,core):
    for name,element in (('lite',-4),('unlite',-5)):
        if name=='unlite':
            section=re.search(rb'^static void cave_temp_room_unlite\([^\n]*\)\r?\n\{.*?^\}',raw,re.M|re.S)
            if not section:raise ValueError(core+' native unlight function changed')
            marker=ANCHOR['unlite_heng'] if core=='heng' else ANCHOR['unlite_other']
            trace='hb_target_trace_area(-5,pos.x,pos.y); ' if core=='heng' else 'hb_target_trace_area(-5,x,y); '
            body=anchor.prepend_to_line(section.group().decode('latin1'),marker,trace,core+' native unlight application')
            raw=raw[:section.start()]+body.encode('latin1')+raw[section.end():]
            continue
        if core=='heng':
            marker=f'    cave_temp_room_{name}(player_ptr, positions);'.encode()
            trace=f'    for(const auto &pos:positions)hb_target_trace_area({element},pos.x,pos.y);\n'.encode()
        else:
            marker=f'cave_temp_room_{name}();'.encode()
            trace=f'{{int trace_i;for(trace_i=0;trace_i<temp_n;++trace_i)hb_target_trace_area({element},temp_x[trace_i],temp_y[trace_i]);}}\n'.encode()
        if raw.count(marker)!=1:raise ValueError(core+' native room observation site changed: '+name)
        raw=raw.replace(marker,trace+marker)
    return b'#include "bridge/native_targeting_api.h"\n'+raw

def native_function(source, name):
    text=source.decode('latin1').replace('\r\n','\n')
    found=re.search(r'^bool '+name+r'\([^\n]*\)\n\{.*?^\}',text,re.M|re.S)
    if not found:raise ValueError('Missing native function '+name)
    return found.group()

def frox_visibility(spells,cave):
    los=native_function(cave,'los').replace('bool los(', 'static bool _fc_preview_los(').replace('cave_los_bold(', '_fc_preview_los_cell(')
    disi=native_function(spells,'in_disintegration_range').replace('bool in_disintegration_range(', 'static bool _fc_preview_disintegration_range(').replace('cave_stop_disintegration(', '_fc_preview_stop_disintegration(')
    return ('/* Generated native visibility geometry with knowledge-only terrain predicates. */\n'
            'static int _fc_preview_los_cell(int y,int x);\nstatic int _fc_preview_stop_disintegration(int y,int x);\n'+los+'\n'+disi+'\n').encode('latin1')

def frox_path(source):
    text=source.decode('latin1').replace('\r\n','\n')
    start=text.index('static bool _project_path_x(')
    stop=text.index('\nsint project_path(',start)
    section=text[start:stop]
    for name in ('_project_path_x','_project_path_y','_copy_path','project_path_new'):
        section=re.sub(r'\b'+name+r'\b','_fc_preview_'+name.lstrip('_'),section)
    section=section.replace('_path_continue(', '_fc_preview_path_continue(')
    return ('/* Generated from spells1.c; traversal and alternate-path selection are unchanged. */\n'
            'static bool _fc_preview_path_continue(int n,int y,int x,int flags);\n'+section+'\n').encode('latin1')

def frox_xtra(source):
    newline = '\r\n' if b'\r\n' in source else '\n'
    text = source.decode('latin1').replace('\r\n', '\n')
    def once(old, new):
        nonlocal text
        count = text.count(old)
        if count != 1:
            raise ValueError(f'frox/xtra2.c: expected one hook, found {count}: {old[:90]!r}')
        text = text.replace(old, new, 1)
    once('#include "angband.h"', '#include "angband.h"\n#include "bridge/native_targeting_api.h"\nstatic int _fc_choose_target(int mode, int eight_way, int range, int *direction, int *x, int *y);\nstatic int _fc_target_movement_pending(void);')
    text = anchor.insert_after(text, ANCHOR['fire_dir'], '    if (hb_target_enabled()) return get_aim_dir_aux(dp, target_mode);', 'frox/get_fire_dir_aux', skip=1, expect_brace=True)
    text = anchor.replace(text, ANCHOR['used_old'],
         '    used_old_target = !hb_target_enabled() && old_target_okay_mode(TARGET_BUFF);', 'frox/get_buff_mon_dir')
    start = anchor.line_start_offset(text, ANCHOR['aim_dir'], 'frox/get_aim_dir_aux')
    stop = text.index('    /* No direction */', start)
    section = text[start:stop]
    old = '    /* Ask until satisfied */\n    while (!dir)'
    if section.count(old) != 1:
        raise ValueError('frox/get_aim_dir_aux input loop changed')
    text = text[:start] + section.replace(old,
         '    /* The original confusion/repeat tail below remains authoritative. */\n'
         '    if (hb_target_enabled() && (!dir || dir == 5) && !_fc_target_movement_pending())\n    {\n'
         '        if (!_fc_choose_target(target_mode, 0, 0, &dir, NULL, NULL))\n'
         '        { project_length = 0; return FALSE; }\n'
         '    }\n    else while (!dir)') + text[stop:]
    # Only replace the repeat-direction function's own loop.
    start = re.search(r'^int get_rep_dir\(int \*dp, bool under\)', text, re.M).start()
    stop = text.index('    /* Prevent weirdness */', start)
    section = text[start:stop]
    old = '    /* Get a direction */\n    while (!dir)'
    if section.count(old) != 1:
        raise ValueError('frox/get_rep_dir input loop changed')
    text = text[:start] + section.replace(old,
        '    if (hb_target_enabled() && !dir)\n    {\n'
        '        if (!_fc_choose_target(TARGET_KILL, under ? 2 : 1, 0, &dir, NULL, NULL)) return FALSE;\n'
        '    }\n    else while (!dir)') + text[stop:]
    for name,eight in [('get_rep_dir2',3),('get_hack_dir',0)]:
        start=text.index('bool '+name+'(int *dp)\n{');stop=text.index('\n}',start)
        section=text[start:stop];old='    while (!dir)'
        if section.count(old)!=1:raise ValueError('Frox input loop changed: '+name)
        hook=f'    if (hb_target_enabled() && !dir && !_fc_target_movement_pending())\n    {{\n        if (!_fc_choose_target(TARGET_KILL,{eight},0,&dir,NULL,NULL)) return FALSE;\n    }}\n    else while (!dir)'
        text=text[:start]+section.replace(old,hook)+text[stop:]
    text = anchor.insert_after(text, ANCHOR['target_set'],
         '    if (hb_target_enabled() && !(mode & TARGET_LOOK))\n'
         '    {\n        int dir;\n        hb_target_profile_begin("standalone-target");\n'
         '        hb_target_profile_plain(NATIVE_TARGET_NEUTRAL);\n'
         '        return _fc_choose_target(mode, 0, 0, &dir, NULL, NULL);\n    }', 'frox/target_set', skip=1, expect_brace=True)
    text = anchor.insert_after(text, ANCHOR['tgt_pt'],
         '    if (hb_target_enabled() && rng >= 0)\n'
         '    { int dir; return _fc_choose_target(0, 0, rng, &dir, x_ptr, y_ptr); }', 'frox/tgt_pt', skip=1, expect_brace=True)
    text += '\n#include "fc_targeting_path.generated.inl"\n#include "fc_targeting_visibility.generated.inl"\n#include "fc_targeting.inl"\n#include "fc_targeting_menu.inl"\n'
    return text.replace('\n', newline).encode('latin1')

def frox_shop(source):
    """Keep Frox shop-only convenience actions item-scoped and atomic."""
    newline='\r\n' if b'\r\n' in source else '\n'
    text=source.decode('latin1').replace('\r\n','\n')
    def once(old,new):
        nonlocal text
        count=text.count(old)
        if count!=1:raise ValueError(f'frox/shop.c: expected one site, found {count}: {old[:90]!r}')
        text=text.replace(old,new,1)
    A = ANCHOR
    def own(key):
        # A few inserted lines equal original lines; take them from the file being patched.
        return anchor.original_line(text, A[key])
    text = anchor.insert_after(text, A['reserve_decl'], 'static void _hide_item(_ui_context_ptr context);', 'frox/shop reserve declaration')
    text = anchor.insert_after(text, A['hide_decl'], '#define OM_SHOP_HIDDEN 0x4000', 'frox/shop filter flag')
    text = anchor.insert_after(text, A['case_R'], "            case 'H': _hide_item(context); break;", 'frox/shop key dispatch')
    # Replace the body of the Unwanted toggle key; the case label line itself stays.
    start = anchor.line_end_offset(text, A['case_u'], 'frox/shop toggle key')
    stop = re.compile(r'^ +case SKEY_PGDOWN:', re.M).search(text, start).start()
    replacement = '''                {
                    slot_t slot, last = inv_last(context->shop->inv, obj_exists);
                    int shown = 0;
                    for (slot = 1; slot <= last; slot++)
                    {
                        obj_ptr obj = inv_obj(context->shop->inv, slot);
                        if (obj && (obj->marked & OM_SHOP_HIDDEN))
                        {
                            obj->marked &= ~OM_SHOP_HIDDEN;
                            shown++;
                        }
                    }
                    @TOP@
                    if (shown) msg_format("Restored %d hidden store item%s.", shown, shown == 1 ? "" : "s");
                    else msg_print("There are no hidden store items.");
                }
                break;
'''
    text = text[:start] + replacement.replace('@TOP@', own('top1')) + text[stop:]
    text = anchor.replace(text, [A['u_if'], A['u_show'], A['u_else'], A['u_hide']],
        '''        doc_printf(doc, "<color:w>[</color><color:o>U</color><color:w>] Show Hidden Items</color> <color:y>(%d)</color>", unwanted);''', 'frox/shop footer')
    insert = '''
static void _hide_item(_ui_context_ptr context)
{
    char cmd, name[MAX_NLEN];
    slot_t slot;
    obj_ptr obj;
    if (!msg_command("<color:y>Hide which item <color:w>(<color:keypress>Esc</color> to cancel)</color>?</color>", &cmd)) return;
    if (cmd < 'a' || cmd > 'z') return;
    @SLOT_PAGE@
    @OBJ_SLOT@
    @OBJ_NULL_RET@
    @DESC_NAME@
    if (obj->marked & OM_SHOP_HIDDEN)
    {
        msg_format("%s is already hidden.", name);
        return;
    }
    obj->marked |= OM_SHOP_HIDDEN;
    msg_format("Hidden %s. Use Show Hidden Items to restore it.", name);
}
'''
    insert = (insert.replace('@SLOT_PAGE@', own('slot_page')).replace('@OBJ_SLOT@', own('obj_slot'))
              .replace('@OBJ_NULL_RET@', own('obj_null_ret')).replace('@DESC_NAME@', own('desc_name')))
    # The definition is the only line with this text followed by an opening brace.
    lines = text.split('\n')
    at = [i for i in range(len(lines) - 1) if anchor.line_hash(lines[i]) == A['examine'] and lines[i + 1].strip() == '{']
    if len(at) != 1:
        raise ValueError(f'frox/shop.c: expected one site, found {len(at)}: _examine definition')
    lines[at[0]:at[0]] = insert.split('\n')
    text = '\n'.join(lines)
    text = anchor.insert_before(text, A['hidden_if'],
        '    ' + own('obj_inv') + '''
    if (obj && (obj->marked & OM_SHOP_HIDDEN)) return TRUE;''', 'frox/shop hidden predicate')
    # Compact the count function: no blank lines, single spaces inside statements.
    lines = text.split('\n')
    at = [i for i in range(len(lines) - 1) if anchor.line_hash(lines[i]) == A['count_def'] and lines[i + 1].strip() == '{']
    if len(at) != 1:
        raise ValueError(f'frox/shop.c: expected one site, found {len(at)}: hidden count definition')
    end = next(j for j in range(at[0], len(lines)) if lines[j] == '}')
    lines[at[0]:end] = [re.sub(r'(?<=\S)[ \t]+', ' ', l) for l in lines[at[0]:end] if l.strip()]
    text = '\n'.join(lines)
    text = anchor.replace(text, A['ct_line'],
        '    {\n        ' + own('obj_inv') + '''
        if (obj && (obj->marked & OM_SHOP_HIDDEN)) ct++;
    }''', 'frox/shop hidden count')
    # With the filter off the page position is no longer the slot number: drop the shortcut
    # and the blank line after it.
    text = anchor.delete(text, [A['ret_pos'], anchor.line_hash('')], 'frox/shop page slot')
    text = anchor.delete(text, [A['ret_max'], anchor.line_hash('')], 'frox/shop page max')
    return text.replace('\n',newline).encode('latin1')

# Windows のファイル名・文字列の変換（変愚・短愚の main-win/main-win-utils.h の to_wchar・to_multibyte）。
# 原作は変換のコードページを 932（CP932）に決め打ちしているが、この案件のコアは内部が UTF-8（UTF8 を定義）なので、
# 日本語の名前のセーブのファイル名が化けて書かれる（F-hengband-01・F-tangband-01。修-51。遊ぶ人の許可 2026-10-12）。
# 4 か所の 932 を、内部の符号に合わせたコードページの名前（HB_INTERNAL_CODEPAGE）に置き換え、その定義を windows.h の直後に 1 つ足す。
# 変換の家は 1 つのまま（ヘッダは 1 個。クラスの二重定義にならない）。原作の文は引用しない。
CODEPAGE_DEFINE=('\n#if defined(UTF8) || !defined(JP)\n#define HB_INTERNAL_CODEPAGE CP_UTF8\n#else\n#define HB_INTERNAL_CODEPAGE 932\n#endif\n')

def internal_codepage_for_wide_conversion(raw):
    """main-win-utils.h の MultiByteToWideChar(932,…)・WideCharToMultiByte(932,…) の 932 を HB_INTERNAL_CODEPAGE に替える。"""
    text=raw.decode('latin1');newline='\r\n' if '\r\n' in text else '\n'
    text,count=re.subn(r'(MultiByteToWideChar|WideCharToMultiByte)\(932,',r'\1(HB_INTERNAL_CODEPAGE,',text)
    if count!=4:raise ValueError('main-win-utils.h code page sites changed: '+str(count))
    include='#include <windows.h>'
    if text.count(include)!=1:raise ValueError('main-win-utils.h windows.h include changed')
    text=text.replace(include,include+CODEPAGE_DEFINE.replace('\n',newline))
    return text.encode('latin1')

def build_heng(root,out_root,prefix,ui_only,helpers,menu_audit,manifest,tangband=False):
    """変愚系のソース（root/src）に書き換えを当てて out_root へ書く。manifest の path には prefix を付ける。

    tangband=True は短愚（3.0.2.4 系）だけに効く挿入（状態のアイコン）を足す。"""
    heng_profiles=[]
    targets={'target-getter.cpp':'getter','target-setter.cpp':'setter','grid-selector.cpp':'point'}
    for source in sorted((root/'src').rglob('*')):
        if not source.is_file() or source.suffix not in ('.h','.hpp','.inl','.cc'):continue
        relative=source.relative_to(root).as_posix();before=source.read_bytes();destination=out_root/relative
        destination.parent.mkdir(parents=True,exist_ok=True)
        after=internal_codepage_for_wide_conversion(before) if relative=='src/main-win/main-win-utils.h' else before
        if not destination.exists() or destination.read_bytes()!=after:destination.write_bytes(after)
        manifest['files'].append({'path':prefix+relative,'source_sha256':digest(before),'overlay_sha256':digest(after)})
    for source in sorted((root/'src').rglob('*.cpp')):
        relative=source.relative_to(root).as_posix();before=source.read_bytes()
        if source.parent.name=='target' and source.name in targets:after=heng_cpp(before,targets[source.name])
        else:after,rows=annotate(source,'heng',ui_only,helpers,menu_audit);heng_profiles.extend(rows)
        if not ui_only and relative in ('src/cmd-action/cmd-spell.cpp','src/spell/spell-info.cpp'):after=book_menu(after,'heng',source.name)
        if relative=='src/effect/effect-processor.cpp':
            trace='    if(src_idx==0){for(const auto &[distance,pos]:positions)hb_target_trace_area(static_cast<int>(typ),pos.x,pos.y);}'
            text=anchor.insert_before(after.decode('latin1'),ANCHOR['positions_empty'],trace,'Heng native area observation site')
            text=anchor.insert_before(text,ANCHOR['positions_empty'],FX_PROJECT,'Heng attack effect projection site')
            after=b'#include "bridge/native_targeting_api.h"\n'+text.encode('latin1')
        if relative=='src/combat/shoot.cpp':after=observe_shot_hit(observe_shot_path(after,'heng'))
        if relative=='src/object-use/throw-execution.cpp':after=observe_throw(after)
        if relative=='src/spell-kind/earthquake.cpp':after=observe_earthquake(after,'heng')
        if relative=='src/spell-kind/spells-lite.cpp':after=observe_room_area(after,'heng')
        if relative=='src/view/display-store.cpp':after=observe_store_screen(after)
        if tangband and relative=='src/window/main-window-stat-poster.cpp':after=observe_tang_status(after)
        if tangband and relative=='src/view/display-characteristic.cpp':after=export_tang_curse_flags(after)
        after=item_display(after,relative)
        destination=out_root/relative;destination.parent.mkdir(parents=True,exist_ok=True)
        if not destination.exists() or destination.read_bytes()!=after:destination.write_bytes(after)
        manifest['files'].append({'path':prefix+relative,'source_sha256':digest(before),'overlay_sha256':digest(after)})
    generated,geometry_sources=heng_geometry(root/'src')
    destination=out_root/'src/target/heng_target_geometry.generated.inl'
    if not destination.exists() or destination.read_bytes()!=generated:destination.write_bytes(generated)
    manifest['files'].append({'path':prefix+'src/target/heng_target_geometry.generated.inl','source_sha256':{name:digest(raw) for name,raw in geometry_sources.items()},'overlay_sha256':digest(generated)})
    return heng_profiles

def check_menu_audit(menu_audit):
    books={(entry['core'],entry['function']) for entry in menu_audit if entry.get('book')}
    for entry in menu_audit:
        missing={(entry['core'],name) for name in entry.get('dispatcher_requires',[])}-books
        if missing:raise ValueError('Menu preview dispatcher has uninstrumented callbacks: '+str(missing))

def build(sources, output,ui_only=False):
    helpers={core:load_helpers(sources,core) for core in ('frox','genso','heng')}
    menu_audit=[]
    files = {'frox/src/xtra2.c': frox_xtra}
    manifest = {'version': 1, 'mode':'ui-only' if ui_only else 'experimental-preview', 'files': []}
    for folder in ('frox/src','gensoband/src','silq/src'):
        for source in sorted((sources/folder).rglob('*')):
            if not source.is_file() or source.suffix not in ('.h','.hpp','.inl','.inc'):continue
            relative=source.relative_to(sources).as_posix();before=source.read_bytes();destination=output/relative
            destination.parent.mkdir(parents=True,exist_ok=True)
            if not destination.exists() or destination.read_bytes()!=before:destination.write_bytes(before)
            manifest['files'].append({'path':relative,'source_sha256':digest(before),'overlay_sha256':digest(before)})
    for relative, patch in files.items():
        source = sources / relative
        before = source.read_bytes()
        after = patch(before)
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists() or destination.read_bytes() != after:
            destination.write_bytes(after)
        manifest['files'].append({'path': relative, 'source_sha256': digest(before),
                                  'overlay_sha256': digest(after)})
    output.mkdir(parents=True, exist_ok=True)
    path_source=sources/'frox/src/spells1.c'
    before=path_source.read_bytes();after=frox_path(before)
    generated=output/'frox/src/fc_targeting_path.generated.inl'
    if not generated.exists() or generated.read_bytes()!=after:generated.write_bytes(after)
    manifest['files'].append({'path':'frox/src/fc_targeting_path.generated.inl','source':'frox/src/spells1.c',
                             'source_sha256':digest(before),'overlay_sha256':digest(after)})
    cave=(sources/'frox/src/cave.c').read_bytes()
    after=frox_visibility(before,cave);generated=output/'frox/src/fc_targeting_visibility.generated.inl'
    if not generated.exists() or generated.read_bytes()!=after:generated.write_bytes(after)
    manifest['files'].append({'path':'frox/src/fc_targeting_visibility.generated.inl',
                             'source_sha256':{'frox/src/spells1.c':digest(before),'frox/src/cave.c':digest(cave)},'overlay_sha256':digest(after)})
    profiles=[]
    for source in sorted((sources/'frox/src').glob('*.c')):
        if source.name=='xtra2.c':continue
        before=source.read_bytes();after,rows=annotate(source,ui_only=ui_only,helpers=helpers['frox'],menu_audit=menu_audit);profiles.extend(rows)
        if source.name=='cmd2.c':after=attack_fx.frox_cmd2(observe_shot_path(after,'frox'))
        if source.name=='py_throw.c':after=attack_fx.frox_throw(after)
        if source.name=='xtra1.c':after=observe_status_bars(after,'frox')
        if source.name=='shop.c':after=frox_shop(after)
        if source.name=='spells2.c':after=observe_room_area(observe_earthquake(after,'frox'),'frox')
        if not ui_only and source.name in ('cmd5.c','spells.c','spells3.c'):after=frox_menu(after,source.name)
        if source.name=='spells1.c':
            after=attack_fx.frox_project(after)
            marker=b'    /* Speed -- ignore "non-explosions" */'
            if after.count(marker)!=1:raise ValueError('Native projection area observation site changed')
            after=b'#include "bridge/native_targeting_api.h"\n'+after.replace(marker,
                b'    if (who == PROJECT_WHO_PLAYER) { int trace_i; for (trace_i=0;trace_i<grids;++trace_i) hb_target_trace_area(typ,gx[trace_i],gy[trace_i]); }\n'+marker)
        after=item_display(after,'frox/src/'+source.name)
        destination=output/'frox/src'/source.name
        if not destination.exists() or destination.read_bytes()!=after:destination.write_bytes(after)
        manifest['files'].append({'path':'frox/src/'+source.name,'source_sha256':digest(before),'overlay_sha256':digest(after)})
    (output/'profiles.json').write_text(json.dumps(profiles,indent=2)+'\n',encoding='utf8')
    manifest['profiles']={'total':len(profiles),'supported':sum(r['supported'] for r in profiles)}
    genso_profiles=[]
    for source in sorted((sources/'gensoband/src').glob('*.c')):
        before=source.read_bytes()
        if source.name=='xtra2.c':after=genso_xtra(before)
        else:
            after,rows=annotate(source,'genso',ui_only,helpers['genso'],menu_audit);genso_profiles.extend(rows)
        if source.name=='cmd6.c':
            native=(sources/'gensoband/src/cmd_junko.c').read_bytes()
            pure=native_function(native,'check_activated_nameless_arts')
            pure=pure.replace('bool check_activated_nameless_arts(', 'static bool hb_target_powerful_device(')
            pure,count=re.subn(r'msg_format\([^;]+;','',pure)
            if count!=1:raise ValueError('Nameless arts diagnostic branch changed')
            # The predicate otherwise only reads the player's active arts.
            text=anchor.insert_before(after.decode('latin1'),ANCHOR['wand_aux'],pure,'Genso device insertion point');after=text.encode('latin1')
        if not ui_only and source.name in ('cmd5.c','spells3.c'):after=book_menu(after,'genso',source.name)
        if source.name=='util.c':
            after+=b'\nint gb_target_mapped_direction(void) { if (!inkey_next || !*inkey_next || inkey_xtra) return 0; return get_keymap_dir(*inkey_next); }\n'
        if source.name=='cmd2.c':after=attack_fx.genso_cmd2(observe_shot_path(after,'genso'))
        if source.name=='xtra1.c':after=observe_status_bars(after,'genso')
        if source.name=='files.c':after=export_genso_player_flags(after)
        if source.name=='spells2.c':after=attack_fx.genso_hack2(observe_room_area(observe_genso_sight_area(observe_earthquake(after,'genso')),'genso'))
        if source.name=='spells1.c':
            after=attack_fx.genso_project(after)
            marker=b'\t/* Speed -- ignore "non-explosions" */'
            if after.count(marker)!=2:raise ValueError('Genso native area observation sites changed')
            after=b'#include "bridge/native_targeting_api.h"\n'+after.replace(marker,
                b'\tif(who==0){int trace_i;for(trace_i=0;trace_i<grids;++trace_i)hb_target_trace_area(typ,gx[trace_i],gy[trace_i]);}\n'+marker,1)
        after=item_display(after,'gensoband/src/'+source.name)
        destination=output/'gensoband/src'/source.name;destination.parent.mkdir(parents=True,exist_ok=True)
        if not destination.exists() or destination.read_bytes()!=after:destination.write_bytes(after)
        manifest['files'].append({'path':'gensoband/src/'+source.name,'source_sha256':digest(before),'overlay_sha256':digest(after)})
    (output/'genso-profiles.json').write_text(json.dumps(genso_profiles,indent=2)+'\n',encoding='utf8')
    manifest['genso_profiles']={'total':len(genso_profiles),'supported':sum(r['supported'] for r in genso_profiles)}
    genso_spells=(sources/'gensoband/src/spells1.c').read_bytes();genso_cave=(sources/'gensoband/src/cave.c').read_bytes()
    generated=genso_geometry(genso_spells,genso_cave);destination=output/'gensoband/src/gb_targeting_path.generated.inl'
    if not destination.exists() or destination.read_bytes()!=generated:destination.write_bytes(generated)
    manifest['files'].append({'path':'gensoband/src/gb_targeting_path.generated.inl','source_sha256':{'gensoband/src/spells1.c':digest(genso_spells),'gensoband/src/cave.c':digest(genso_cave)},'overlay_sha256':digest(generated)})
    heng_profiles=build_heng(sources,output,'',ui_only,helpers['heng'],menu_audit,manifest)
    (output/'heng-profiles.json').write_text(json.dumps(heng_profiles,indent=2)+'\n',encoding='utf8')
    manifest['heng_profiles']={'total':len(heng_profiles),'supported':sum(r['supported'] for r in heng_profiles)}
    # 短愚蛮怒は変愚 3.0.2.4 の写し（tangband/src）を自分のソースとして持つ（変愚の版上げで短愚を変えないため。
    # ）。tangband/ を根にして同じ書き換えを当てる。
    if (sources/'tangband/src').is_dir():
        tang_audit=[]
        tang_profiles=build_heng(sources/'tangband',output/'tangband','tangband/',ui_only,load_helpers(sources/'tangband','heng'),tang_audit,manifest,tangband=True)
        check_menu_audit(tang_audit)
        (output/'tang-profiles.json').write_text(json.dumps(tang_profiles,indent=2)+'\n',encoding='utf8')
        (output/'tang-menu-profiles.json').write_text(json.dumps(tang_audit,indent=2)+'\n',encoding='utf8')
        manifest['tang_profiles']={'total':len(tang_profiles),'supported':sum(r['supported'] for r in tang_profiles)}
    sil_profiles=[]
    for source in sorted((sources/'silq/src').glob('*.c')):
        before=source.read_bytes()
        if source.name=='xtra2.c':after=sil_xtra(before)
        else:after,rows=annotate(source,'sil',ui_only);sil_profiles.extend(rows)
        if source.name=='spells1.c':
            after=attack_fx.silq_project(after)
            marker=b'    /* Clear the "temp" array  XXX */'
            if after.count(marker)!=1:raise ValueError('Sil native area observation site changed')
            after=b'#include "bridge/native_targeting_api.h"\n'+after.replace(marker,
                b'    if(who==-1){int trace_i;for(trace_i=0;trace_i<grids;++trace_i)hb_target_trace_area(typ,gx[trace_i],gy[trace_i]);}\n'+marker)
        if source.name=='use-obj.c':
            text=anchor.append_to_line(after.decode('latin1'),ANCHOR['knock_x'],'\n                hb_target_trace_area(-1,x,y); /* Force uses no GF element. */','Sil force-horn area observation site')
            after=text.encode('latin1')
        if source.name=='cmd2.c':
            after=attack_fx.silq_cmd2(after)
            marker=b'                /* If it hit */'
            if after.count(marker)!=1:raise ValueError('Sil arrow hit observation site changed')
            after=after.replace(marker,b'                hb_target_trace_hit(hit_result>0,x,y);\n'+marker)
        if source.name=='util.c':
            after+=b'\nint sq_target_mapped_direction(void) { if (!inkey_next || !*inkey_next || inkey_xtra) return 0; return target_dir(*inkey_next); }\n'
        if source.name=='cave.c':
            after+=b'\n#include "sq_targeting_geometry.inl"\n#include "sq_targeting_path.generated.inl"\n'
            path_bytes=sil_path(before);generated=output/'silq/src/sq_targeting_path.generated.inl'
            generated.parent.mkdir(parents=True,exist_ok=True)
            if not generated.exists() or generated.read_bytes()!=path_bytes:generated.write_bytes(path_bytes)
            manifest['files'].append({'path':'silq/src/sq_targeting_path.generated.inl','source':'silq/src/cave.c','source_sha256':digest(before),'overlay_sha256':digest(path_bytes)})
        after=item_display(after,'silq/src/'+source.name)
        destination=output/'silq/src'/source.name;destination.parent.mkdir(parents=True,exist_ok=True)
        if not destination.exists() or destination.read_bytes()!=after:destination.write_bytes(after)
        manifest['files'].append({'path':'silq/src/'+source.name,'source_sha256':digest(before),'overlay_sha256':digest(after)})
    (output/'sil-profiles.json').write_text(json.dumps(sil_profiles,indent=2)+'\n',encoding='utf8')
    manifest['sil_profiles']={'total':len(sil_profiles),'supported':sum(r['supported'] for r in sil_profiles)}
    check_menu_audit(menu_audit)
    (output/'menu-profiles.json').write_text(json.dumps(menu_audit,indent=2)+'\n',encoding='utf8')
    manifest['menu_profiles']={'functions':len(menu_audit),'defined_cases':sum(bool(case.get('profile')) for entry in menu_audit for case in entry.get('cases',[]))}
    native_fx_overlay.build(sources, output, manifest)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf8')
    return manifest

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--sources', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ui-only',action='store_true')
    args = parser.parse_args()
    print(json.dumps(build(args.sources, args.output,args.ui_only), indent=2))
