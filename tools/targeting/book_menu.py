"""Connect the existing Heng/Genso spell lists to passive action previews."""
import re
from inventory import TOKENS,blank
import line_anchor as anchor

# Hashes of original lines (see line_anchor.py); the original text is not quoted here.
H={
    # print_spells の宣言の行。3.0.2.5 で引数 is_command_list が増えた（短愚は 3.0.2.4 のまま）ので両方を持つ
    'heng_print_spells':'b5f6896b76d8','heng_print_spells_3025':'6146565e1ebb',
    'open_brace':'021fb596db81','heng_row_ok':'4dd23d70d375','heng_row_dark':'763ce1617241',
    'genso_print_spells':'cb0a99a3b538','genso_spell_name':'db0abe8fdff4',
    # TOband の print_spells() の局所変数の宣言の最後の 2 行（char skill_lev_str_short[5]; / char buf[256];）
    'toband_skill_str':'3ea86a1a5ab8','toband_buf':'76b9a4c6ec00',
}

def prepare(raw,core,filename):
    text=raw.decode('latin1');newline='\r\n' if '\r\n' in text else '\n';text=text.replace('\r\n','\n')
    def once(old,new):
        nonlocal text
        if text.count(old)!=1:raise ValueError(core+' book menu changed: '+filename+': '+old[:70])
        text=text.replace(old,new,1)
    header={'heng':'bridge/hengband_targeting.h','toband':'tb_targeting_menu.h'}.get(core,'gb_targeting_menu.h')
    text='#include "bridge/native_targeting_api.h"\n#include "'+header+'"\n'+text
    if core=='heng':text='#include "term/gameterm.h"\n#include "term/z-term.h"\n'+text
    if filename in ('cmd-spell.cpp','cmd5.c'):
        # Only the actual cast command enables the preview, never browse/study.
        mask=TOKENS.sub(blank,text)
        signature=re.search(r'static int get_spell\(([^;{}]*)\)\s*\{',mask)
        if not signature:raise ValueError(core+' native spell chooser missing')
        params=text[signature.start(1):signature.end(1)]
        # 変愚 3.0.2.6 から get_spell に既定値つきの引数（bool browse = false）がある。既定値の後ろには
        # 既定値の無い引数を置けないので、そのときだけ旗を先頭の引数にする（呼ぶ所も先頭に足す）。
        # 既定値の無い版（短愚・幻想蛮怒・TOband）は従来どおり末尾に足す（出力を変えない）。
        flag_first='=' in mask[signature.start(1):signature.end(1)]
        arguments=[re.search(r'(\w+)\s*$',arg.split('=')[0].strip()).group(1) for arg in params.split(',')]
        start=signature.end()-1;depth=1;stop=start+1
        while depth:
            if mask[stop]=='{':depth+=1
            elif mask[stop]=='}':depth-=1
            stop+=1
        wrapper_params=('int hb_selecting, '+params) if flag_first else (params+', int hb_selecting')
        wrapper='\nstatic int get_spell('+wrapper_params+')\n{\n    int hb_scope=hb_target_menu_begin();\n    int hb_result;\n    hb_target_menu_action(hb_selecting);\n    hb_result=hb_native_get_spell('+','.join(arguments)+');\n    hb_target_menu_end(hb_scope);\n    return hb_result;\n}\n'
        text=text[:stop]+wrapper+text[stop:]
        text=text[:signature.start()]+text[signature.start():signature.end()].replace('get_spell','hb_native_get_spell',1)+text[signature.end():]
        # Parse the unchanged call sites, retaining both compiled locale paths.
        mask=TOKENS.sub(blank,text);stack=[];ends={}
        for i,c in enumerate(mask):
            if c in '({[':stack.append(i)
            elif c in ')}]' and stack:ends[stack.pop()]=i
        # TOband の do_cmd_cast() は仲間の呪文の持ち主を選ぶ薄い関数で、本を選んで唱えるのは static な cast_selected_book()
        cast_function='cast_selected_book' if core=='toband' else 'do_cmd_cast'
        cast=re.search(r'\b(?:bool|void) '+cast_function+r'\([^;{}]*\)\s*\{',mask)
        if not cast:raise ValueError(core+' native cast command missing')
        cast_start=cast.end()-1;cast_end=ends[cast_start]
        edits=[]
        for match in re.finditer(r'\bget_spell\s*\(',mask):
            closing=ends[match.end()-1]
            tail=mask[closing+1:].lstrip()
            if tail.startswith('{'):continue
            flag='1' if cast_start<match.start()<cast_end else '0'
            edits.append((match.end(),flag+', ') if flag_first else (closing,', '+flag))
        if len(edits)<3:raise ValueError(core+' native spell calls changed')
        for at,value in reversed(edits):text=text[:at]+value+text[at:]
        if core=='genso':
            # 持ち物の窓の本の行に「読む・唱える・覚える」を出す（2026-10-06 の報告「魔導書から呪文を覚えようとすると
            # 上手く動作しない」。幻想蛮怒だけ本の行にこの 3 つが無かった）。**原作が本を選ばせるときの条件をそのまま使う**——
            # 選択肢に載らない本に命令を出すと、持ち物の窓（NativeInventoryMenu）が対象を探して空回りする。条件の関数は
            # このファイルの static なので、アダプタからは呼べない。だからここ（ファイルの末尾）に足す。原作の行は変えない。
            # 読む do_cmd_browse・覚える do_cmd_study は item_tester_learn_spell、唱える do_cmd_cast は item_tester_cast_spell。
            text+=('\n#include "bridge/native_store_data.h"\n'
                   'void gb_native_item_book_commands(const void *source,native_store_item *out) {\n'
                   '    object_type *obj=(object_type *)source;\n'
                   '    if(!obj||!obj->k_idx)return;\n'
                   "    if(item_tester_learn_spell(obj))native_item_command(out,'b');\n"
                   "    if(item_tester_cast_spell(obj))native_item_command(out,'m');\n"
                   "    if(item_tester_learn_spell(obj))native_item_command(out,'G');\n"
                   '}\n')
    elif core=='heng' and filename=='spell-info.cpp':
        signature=next((h for h in (H['heng_print_spells'],H['heng_print_spells_3025']) if anchor.count(text,[h,H['open_brace']])==1),H['heng_print_spells'])
        text=anchor.insert_after(text,[signature,H['open_brace']],'    if(angband_terms[0]==game_term){hb_target_menu_clear_rows();hb_target_menu_geometry(x,y,65,num+1,true);}','heng print_spells')
        text=anchor.insert_after(text,H['heng_row_ok'],'        hb_heng_menu_book_row(player_ptr,I2A(i),y+i+1,x,out_val.c_str(),spell_name.data(),static_cast<int>(use_realm),spell_id,spell.slevel<=player_ptr->lev && !realm_status.is_forgotten(spell_id) && (pc.is_every_magic()||realm_status.is_learned(spell_id)),use_menu&&target_spell_id?(i==target_spell_id-1):-1);','heng row')
        text=anchor.insert_after(text,H['heng_row_dark'],'            hb_heng_menu_book_row(player_ptr,I2A(i),y+i+1,x,out_val.c_str(),out_val.c_str(),static_cast<int>(use_realm),spell_id,false,use_menu&&target_spell_id?(i==target_spell_id-1):-1);','heng dark row')
    elif core=='genso' and filename=='spells3.c':
        # Declarations precede statements in the original C compilation mode.
        once('\tbool max = FALSE;\n','\tcptr hb_spell_name = NULL;\n\tbool max = FALSE;\n\tif(Term==angband_term[0]){hb_target_menu_clear_rows();hb_target_menu_geometry(x,y,65,num+1,TRUE);}\n')
        if anchor.count(text,[H['genso_print_spells'],H['open_brace']])!=1:raise ValueError('Genso spell list signature changed')
        text=anchor.edit_exactly(text,H['genso_spell_name'],
            lambda lines:[lines[0].replace('do_spell(','(hb_spell_name = do_spell(',1).replace('SPELL_NAME)','SPELL_NAME))',1)],2,'Genso native spell names')
        text=anchor.insert_after(text,H['heng_row_ok'],'\t\tgb_target_menu_book_row(I2A(i),y+i+1,x,out_val,hb_spell_name,use_realm,spell,spell_okay(spell,TRUE,FALSE,use_realm),use_menu&&target_spell?(i==target_spell-1):-1);','genso row')
        text=anchor.insert_after(text,H['heng_row_dark'],'\t\t\t\tgb_target_menu_book_row(I2A(i),y+i+1,x,out_val,out_val,use_realm,spell,FALSE,use_menu&&target_spell?(i==target_spell-1):-1);','genso dark row')
    elif core=='toband' and filename=='spells3.c':
        # 幻想蛮怒と同じ作り。TOband の print_spells() は局所変数の宣言のあとに文が続くので、宣言の最後の行の次に足す。
        # 呪文名の行は 1 か所だけ（幻想蛮怒は 2 か所）。spell_okay() は引数 2 つ。
        if anchor.count(text,[H['genso_print_spells'],H['open_brace']])!=1:raise ValueError('TOband spell list signature changed')
        text=anchor.insert_after(text,[H['toband_skill_str'],H['toband_buf']],'\tcptr hb_spell_name = NULL;\n\tif(Term==angband_term[0]){hb_target_menu_clear_rows();hb_target_menu_geometry(x,y,65,num+1,TRUE);}','TOband print_spells locals')
        text=anchor.edit_exactly(text,H['genso_spell_name'],
            lambda lines:[lines[0].replace('do_spell(','(hb_spell_name = do_spell(',1).replace('SPELL_NAME)','SPELL_NAME))',1)],1,'TOband native spell names')
        text=anchor.insert_after(text,H['heng_row_ok'],'\t\ttb_target_menu_book_row(I2A(i),y+i+1,x,out_val,hb_spell_name,use_realm,spell,spell_okay(spell,use_realm),use_menu&&target_spell?(i==target_spell-1):-1);','TOband row')
        text=anchor.insert_after(text,H['heng_row_dark'],'\t\t\t\ttb_target_menu_book_row(I2A(i),y+i+1,x,out_val,out_val,use_realm,spell,FALSE,use_menu&&target_spell?(i==target_spell-1):-1);','TOband dark row')
    return text.replace('\n',newline).encode('latin1')
