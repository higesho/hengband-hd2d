"""Attach preview lifetimes and native row identities to existing choosers."""
import re
from profile_inventory import Source
import line_anchor as anchor

# Hashes of original lines (see line_anchor.py); the original text is not quoted here.
H={
    'list_spells':'4bc6fbea5b7d','open_brace':'021fb596db81','for_ct':'6c59845b7589','letter_decl':'839e8eec421c',
    'book_print_spells':'b66c4eb21f66','dump_comment':'648e7381e3d4','for_num':'1a08df230b08','book_row':'731935cfbdae',
}

def prepare(raw,filename):
    text=raw.decode('latin1');newline='\r\n' if '\r\n' in text else '\n';text=text.replace('\r\n','\n')
    def once(old,new):
        nonlocal text
        if text.count(old)!=1:raise ValueError('Frox preview menu hook changed: '+filename+': '+old[:75])
        text=text.replace(old,new,1)
    def wrap(name):
        nonlocal text
        signature=re.search(r'static int '+re.escape(name)+r'\(([^;{}]*)\)\s*\{',text)
        if not signature:raise ValueError('Frox chooser signature changed: '+name)
        start=signature.end()-1;depth=1;stop=start+1
        # Mask strings/comments while finding the native function's closing brace.
        from inventory import TOKENS,blank
        mask=TOKENS.sub(blank,text)
        while depth:
            if mask[stop]=='{':depth+=1
            elif mask[stop]=='}':depth-=1
            stop+=1
        params=signature.group(1);arguments=[re.search(r'(\w+)\s*$',arg.strip()).group(1) for arg in params.split(',')]
        helper='_hb_menu_'+name.lstrip('_')
        selecting='learned&&!browse' if name=='get_spell' else '!force_browsing'
        wrapper='\nstatic int '+name+'('+params+')\n{\n    int scope=hb_target_menu_begin();\n    int result;\n    hb_target_menu_action('+selecting+');\n    result='+helper+'('+','.join(arguments)+');\n    hb_target_menu_end(scope);\n    return result;\n}\n'
        text=text[:stop]+wrapper+text[stop:]
        text=text[:signature.start()]+text[signature.start():signature.end()].replace(name,helper,1)+text[signature.end():]
    once('#include "angband.h"','#include "angband.h"\n#include "bridge/native_targeting_api.h"\n#include "fc_targeting_menu.h"')
    if filename=='cmd5.c':wrap('get_spell')
    elif filename=='spells.c':
        wrap('_choose_spell')
        text=anchor.insert_after(text,[H['list_spells'],H['open_brace']],'    if(Term==angband_term[0])hb_target_menu_clear_rows();','frox _list_spells')
        text=anchor.insert_before(text,[H['for_ct'],H['open_brace'],H['letter_decl']],'    if(Term==angband_term[0])hb_target_menu_geometry(display.x,display.y,display.cx,col_height+1,TRUE);','frox spell rows')
        for arguments in ['describe ? prompt2 : inscribe ? prompt3 : prompt1, &ch, FALSE','describe ? prompt2 : prompt1, &ch, FALSE', '"New label (\'!\' to unstick): ", &ch, FALSE']:
            call='get_com('+arguments+')'
            once(call,'(hb_target_menu_action(!describe&&!inscribe),'+call+')')
        for row,column in [('display.y + i + 1 - skipped','display.x'),('display.y + (i - col_height) + 1 - skipped','display.x + col_width')]:
            marker=f'            c_put_str(attr, temp, {row}, {column});'
            once(marker,marker+f'\n            fc_target_menu_spell_row(letter,{row},{column},temp,spell->fn,spell->level<=p_ptr->lev&&spell_cost<=max_cost&&spell->fail<100);')
    elif filename=='spells3.c':
        text=anchor.insert_after(text,[H['book_print_spells'],H['open_brace']],'    if(Term==angband_term[0])hb_target_menu_clear_rows();','frox print_spells')
        text=anchor.insert_before(text,[H['dump_comment'],H['for_num']],'    if(Term==angband_term[0])hb_target_menu_geometry(display.x,display.y,display.cx,num+1,TRUE);','frox book rows')
        text=anchor.insert_after(text,H['book_row'],'        fc_target_menu_book_row(I2A(i),display.y+i+1,display.x,out_val,use_realm,spell,spell_okay(spell,TRUE,FALSE,use_realm,FALSE));','frox book row')
    return text.replace('\n',newline).encode('latin1')
