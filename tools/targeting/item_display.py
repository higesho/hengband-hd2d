"""Declare the objects already shown by native item selectors without changing selection."""
import re
import line_anchor as anchor

# Hashes of original lines (see line_anchor.py); the original text is not quoted here.
H={
    'floor_sig':'4f9ccb515141','open_brace':'021fb596db81','put_head':'4114ebe6db53','put_tmp':'8b16ef5f0a30',
    'put_tmp_tight':'77ee3dda0dfb','slot_decl':'0ab61b40b999','blank':'e3b0c44298fc','disp_ctx':'ede33c296521',
    'cmd_handler':'e80063734b52','label_slot':'f9b0b244adb1','inv_display':'4e5403c2dee9','aware':'5d64b121206c',
    'name_decl':'b645b3d58255',
}

def prepare(raw, path):
    targets={
        'src/inventory/floor-item-getter.cpp','src/view/display-inventory.cpp',
        'src/window/main-window-equipments.cpp','src/floor/object-scanner.cpp',
        'gensoband/src/object1.c','silq/src/object1.c','frox/src/inv.c','toband/src/object1.c','toband/src/cmd6.c',
        'gensoband/src/cmd6.c','frox/src/cmd6.c','frox/src/obj_prompt.c',
        'src/cmd-item/cmd-usestaff.cpp','src/cmd-item/cmd-zaprod.cpp',
    }
    if path not in targets:return raw
    newline='\r\n' if b'\r\n' in raw else '\n'
    text=raw.decode('latin1').replace('\r\n','\n')
    def once(old,new):
        nonlocal text
        if text.count(old)!=1:raise ValueError(f'{path}: item display site changed: {old[:90]}')
        text=text.replace(old,new,1)
    def function(name, change):
        nonlocal text
        match=re.search(r'^\w+ '+name+r'\([^\n]*\)\n\{.*?^\}',text,re.M|re.S)
        if not match:raise ValueError(f'{path}: no function {name}')
        before=match.group();after=change(before)
        text=text[:match.start()]+after+text[match.end():]
    def display(name,marker,entry):
        def edit(body):
            body=body.replace('\n{','\n{\n    hb_ui_item_begin();',1)
            return anchor.insert_before(body,H[marker],entry,f'{path}/{name}: item row site')
        function(name,edit)
    def scope(name):
        def edit(body):
            signature=body[:body.index('\n{')]
            args=signature[signature.index('(')+1:signature.rindex(')')]
            names=[re.search(r'(\w+)\s*$',part).group(1) for part in args.split(',')]
            original='hb_original_'+name
            return (signature.replace(name,original,1)+';\n'+signature+'\n{\n'
                '    bool result; hb_ui_item_scope(1,mode);\n'
                f'    result={original}({", ".join(names)});\n'
                '    hb_ui_item_scope(0,0); return result;\n}\n'+body.replace(name,original,1))
        function(name,edit)
    def action_scope(name,action,core):
        if core=='heng':
            marker='void '+name+'(PlayerType *player_ptr)\n{'
            once(marker,marker+f"\n    const int preview_scope=hb_target_item_menu_begin('{action}',hb_target_heng_item_preview,player_ptr);\n"
                '    const auto preview_lifetime=util::make_finalizer([preview_scope]{hb_target_menu_end(preview_scope);});')
        else:
            def edit(body):
                signature=body[:body.index('\n{')];original='hb_original_'+name
                return ('static '+signature.replace(name,original,1)+';\n'+signature+'\n{\n'
                    f"    int scope=hb_target_item_menu_begin('{action}',{core}_target_item_preview,NULL);\n"
                    f'    {original}();\n    hb_target_menu_end(scope);\n}}\nstatic '+body.replace(name,original,1))
            function(name,edit)
    if path=='src/inventory/floor-item-getter.cpp':
        text=anchor.insert_after(text,[H['floor_sig'],H['open_brace']],'    NativeItemDisplayScope item_display_scope(mode);',
            f'{path}: get_item_floor')
    elif path=='src/view/display-inventory.cpp':
        display('show_inventory','put_head',
            '        hb_ui_item_add(&item,0,i,inven_label[i],j+1,use_menu&&target_item?(j==target_item-1):(-1));')
    elif path=='src/window/main-window-equipments.cpp':
        display('show_equipment','put_head',
            '        if(item.is_valid())hb_ui_item_add(&item,1,i,equip_label[i-INVEN_MAIN_HAND],j+1,use_menu&&target_item?(j==target_item-1):(-1));\n'
            '        else hb_ui_item_empty(1,i,equip_label[i-INVEN_MAIN_HAND],j+1,use_menu&&target_item?(j==target_item-1):(-1),(std::string(mention_use(player_ptr,i))+": "+out_desc[j]).c_str());')
    elif path=='src/floor/object-scanner.cpp':
        display('show_floor_items','put_head',
            '        hb_ui_item_add(&item,2,m,floor_label[j],j+1,use_menu&&target_item?(j==target_item-1):(-1));')
    elif path=='gensoband/src/object1.c':
        scope('get_item');scope('get_item_floor')
        display('show_inven','put_tmp','\t\thb_ui_item_add(o_ptr,0,i,inven_label[i],j+1,use_menu&&target_item?(j==target_item-1):(-1));')
        display('show_equip','put_tmp_tight','\t\tif(o_ptr->k_idx)hb_ui_item_add(o_ptr,1,i,equip_label[i-INVEN_RARM],j+1,use_menu&&target_item?(j==target_item-1):(-1));\n'
            '\t\telse {char slot_label[1024];snprintf(slot_label,sizeof(slot_label),"%s: %s",gb_tr(mention_use(i)),out_desc[j]);hb_ui_item_empty(1,i,equip_label[i-INVEN_RARM],j+1,use_menu&&target_item?(j==target_item-1):(-1),slot_label);}')
        display('show_floor','put_tmp','\t\thb_ui_item_add(o_ptr,2,i,floor_label[j],j+1,use_menu&&target_item?(j==target_item-1):(-1));')
    elif path=='toband/src/object1.c':
        # TOband（変愚蛮怒 1.5.1 系）の show_inven / show_equip / show_floor は、選択キーをその場で求める（幻想蛮怒の
        # prepare_label_string の配列は無い）。キーは原作が画面に出す字と同じ式：持ち物は inven_spellbook_label[i]、
        # 装備・床は index_to_label。装備の空き欄（両手持ちの左手など）の名前は言語層の tb_tr() を通す。
        scope('get_item');scope('get_item_floor')
        display('show_inven','put_tmp','\t\thb_ui_item_add(o_ptr,0,i,(i<=INVEN_PACK?inven_spellbook_label[i]:index_to_label(i)),j+1,use_menu&&target_item?(j==target_item-1):(-1));')
        display('show_equip','put_tmp_tight','\t\tif(o_ptr->k_idx)hb_ui_item_add(o_ptr,1,i,index_to_label(i),j+1,use_menu&&target_item?(j==target_item-1):(-1));\n'
            '\t\telse {char slot_label[1024];snprintf(slot_label,sizeof(slot_label),"%s: %s",tb_tr(mention_use(i)),out_desc[j]);hb_ui_item_empty(1,i,index_to_label(i),j+1,use_menu&&target_item?(j==target_item-1):(-1),slot_label);}')
        display('show_floor','put_tmp','\t\thb_ui_item_add(o_ptr,2,i,index_to_label(j),j+1,use_menu&&target_item?(j==target_item-1):(-1));')
    elif path=='silq/src/object1.c':
        scope('get_item')
        display('show_inven','put_tmp','        hb_ui_item_add(o_ptr,0,i,index_to_label(i),j+1,-1);')
        display('show_equip','put_tmp','        hb_ui_item_add(o_ptr,1,i,index_to_label(i),j+1,-1);')
        display('show_floor','put_tmp','        hb_ui_item_add(o_ptr,2,i,index_to_label(out_index[j]),j+1,-1);')
    elif path=='frox/src/obj_prompt.c':
        text=anchor.insert_after(text,[H['slot_decl'],H['blank'],H['disp_ctx']],'        const void        *ui_choice;','frox/obj_prompt: decl',skip=-2)
        text=anchor.replace(text,H['cmd_handler'],'        ui_choice = hb_ui_item_take_choice();\n        if (!ui_choice && prompt->cmd_handler)','frox/obj_prompt: handler')
        text=anchor.replace(text,H['label_slot'],'''        if (ui_choice)
        {
            for (slot = 1; slot <= inv_max(tab->inv); ++slot)
                if (inv_obj(tab->inv, slot) == ui_choice) break;
            if (slot > inv_max(tab->inv)) continue;
            cmd = inv_slot_label(tab->inv, slot);
        }
        else slot = inv_label_slot(tab->inv, cmd);''','frox/obj_prompt: label')
        text=anchor.insert_after(text,H['inv_display'],'''    /* Expose every already-filtered object in native order to the scrolling UI.
     * Text-page keys still refer only to the visible native page. */
    hb_ui_item_begin();
    for (i = 1; i <= inv_max(tab->inv); ++i)
    {
        obj_ptr obj = inv_obj(tab->inv, i);
        int key;
        if (!obj || (filter && !filter(obj))) continue;
        if (inv_loc(tab->inv) == INV_EQUIP && equip_is_empty_two_handed_slot(i)) continue;
        key = i >= start && i <= stop ? inv_slot_label(tab->inv, i) : 13;
        hb_ui_item_add(obj, inv_loc(tab->inv), i, key > 32 ? key : 13, -1, -1);
    }''','frox/obj_prompt: list')
        text=anchor.replace(text,H['aware'],'            if (tab->page == 0 && cmd > 32 && object_is_aware(obj))','frox/obj_prompt: aware')
    elif path=='frox/src/inv.c':
        display('inv_display','name_decl',
            '            hb_ui_item_add(obj,inv_loc(inv),slot,inv_slot_label(inv,slot),doc_cursor(doc).y,-1);')
    elif path=='frox/src/cmd6.c':
        action_scope('do_cmd_use_staff','u','fc');action_scope('do_cmd_zap_rod','z','fc');action_scope('do_cmd_activate','A','fc')
        text+='''
#include "bridge/native_store_data.h"
void fc_native_item_use_commands(const void *source,native_store_item *out) {
    object_type *obj=(object_type *)source;
    if(_can_eat(obj))native_item_command(out,'E');
    if(_can_quaff(obj))native_item_command(out,'q');
    if(_can_read(obj))native_item_command(out,'r');
    if(_activate_p(obj))native_item_command(out,'A');
    if(obj->tval==TV_STAFF)native_item_command(out,'u');
    if(obj->tval==TV_WAND)native_item_command(out,'a');
    if(obj->tval==TV_ROD)native_item_command(out,'z');
}
'''
    elif path=='gensoband/src/cmd6.c':
        action_scope('do_cmd_use_staff','u','gb');action_scope('do_cmd_zap_rod','z','gb')
        text+='''
#include "bridge/native_store_data.h"
void gb_target_item_profile(int action,const void *source) {
    object_type *obj=(object_type *)source;
    int powerful;
    if(!obj || !obj->k_idx || !object_is_aware(obj))return;
    powerful=hb_target_powerful_device(JKF1_MASTER_MAGITEM);
    if(action=='u' && obj->tval==TV_STAFF)hb_target_genso_staff_profile(obj->sval,powerful,FALSE);
    if(action=='z' && obj->tval==TV_ROD)hb_target_genso_rod_self_profile(obj->sval,powerful,FALSE);
}
void gb_native_item_use_commands(const void *source,native_store_item *out) {
    object_type *obj=(object_type *)source;
    if(object_is_eatable(obj))native_item_command(out,'E');
    if(item_tester_hook_quaff(obj))native_item_command(out,'q');
    if(item_tester_hook_readable(obj))native_item_command(out,'r');
    if(item_tester_hook_activate(obj))native_item_command(out,'A');
    if(use_command&&item_tester_hook_use(obj))native_item_command(out,rogue_like_commands?'z':'u');
    else {
        if(item_tester_hook_cmd_use(obj))native_item_command(out,'u');
        if(obj->tval==TV_WAND)native_item_command(out,'a');
        if(obj->tval==TV_ROD)native_item_command(out,'z');
    }
}
'''
    elif path=='toband/src/cmd6.c':
        # 杖とロッドの「持ち物の窓で選んだ品の下見」。商店・持ち物の窓の材料は toband/adapter/tb_native_store.c が出す（2026-10-17）。
        # 幻想蛮怒の gb_native_item_use_commands に当たるものは足さない——TOband の使う・飲む・読むの判定（object_is_food ほか）は
        # 公開の関数なので、アダプタ側で決める（原作に足す行を増やさない）。profile の関数は device_profiles.py が作る static 関数。
        action_scope('do_cmd_use_staff','u','tb');action_scope('do_cmd_zap_rod','z','tb')
        text+='''
void tb_target_item_profile(int action,const void *source) {
    object_type *obj=(object_type *)source;
    if(!obj || !obj->k_idx || !object_is_aware(obj))return;
    if(action=='u' && obj->tval==TV_STAFF)hb_target_toband_staff_profile(obj->sval,0,FALSE);
    if(action=='z' && obj->tval==TV_ROD)hb_target_toband_rod_self_profile(obj->sval,0,FALSE);
}
'''
    elif path in ('src/cmd-item/cmd-usestaff.cpp','src/cmd-item/cmd-zaprod.cpp'):
        action_scope('do_cmd_use_staff' if 'usestaff' in path else 'do_cmd_zap_rod','u' if 'usestaff' in path else 'z','heng')
        text='#include "util/finalizer.h"\n'+text
    if path.startswith('toband/'):text='#include "tb_targeting_menu.h"\n#include "tb_lang_c.h"\n'+text
    return ('#include "bridge/native_item_display.h"\n#include "bridge/native_targeting_api.h"\n'+text).replace('\n',newline).encode('latin1')
