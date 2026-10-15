#define do_cmd_store gb_original_do_cmd_store
#include "store.c"
#undef do_cmd_store
#include "bridge/native_store_data.h"
#include "gb_unaware_category.h"
static int gb_modern_store_active=0;
void gb_native_item_use_commands(const void *,native_store_item *);
void gb_native_item_book_commands(const void *,native_store_item *); /* 本の行の読む・唱える・覚える（tools/targeting/book_menu.py が cmd5.c の末尾に足す） */
void gb_native_describe_item(const void *source,native_store_item *out){
    object_type copy=*(const object_type *)source;
    memset(out,0,sizeof(*out));out->available=1;out->selection=-1;out->count=copy.number;
    if(!copy.k_idx)return;
    out->kind_id=object_is_aware(&copy)?copy.k_idx:0;
    out->unaware_category=gb_unaware_category(&copy);
    out->glyph[0]=object_char(&copy);out->glyph_color=object_attr(&copy);object_desc(out->name,&copy,0);
#ifdef JP
    snprintf(out->weight,sizeof(out->weight),"%d.%d kg",lbtokg1(copy.weight),lbtokg2(copy.weight));
#else
    snprintf(out->weight,sizeof(out->weight),"%d.%d lb",copy.weight/10,copy.weight%10);
#endif
    if(object_is_known(&copy)&&copy.name1&&a_info[copy.name1].text)snprintf(out->description,sizeof(out->description),"%s",gb_tr(a_text+a_info[copy.name1].text));
    else if(object_is_aware(&copy)&&k_info[copy.k_idx].text)snprintf(out->description,sizeof(out->description),"%s",gb_tr(k_text+k_info[copy.k_idx].text));
    native_item_command(out,'I');native_item_command(out,'{');if(copy.inscription)native_item_command(out,'}');
    native_item_command(out,'d');native_item_command(out,'k');native_item_command(out,'v');
    if(item_tester_hook_wear((object_type *)source))native_item_command(out,'w');
    if(copy.tval==TV_BULLET||copy.tval==TV_ARROW||copy.tval==TV_BOLT)native_item_command(out,'f');
    if(copy.tval==TV_FLASK||copy.tval==TV_LITE)native_item_command(out,'F');
    gb_native_item_use_commands(source,out);
    gb_native_item_book_commands(source,out);
}
int gb_native_owned_item(int index,native_store_item *out){
    int slot,seen=0;
    for(slot=0;slot<INVEN_TOTAL;++slot)if(inventory[slot].k_idx&&seen++==index){
        gb_native_describe_item(&inventory[slot],out);out->slot=slot;out->location=slot>=INVEN_RARM?1:0;out->key=index_to_label(slot);
        /* 装備中の品は wield（w）と破壊（k）の選択肢に載らない（2026-09-25 の監査）。 */
        if(out->location==1){native_item_command(out,'t');native_item_drop_command(out,'w');native_item_drop_command(out,'k');}
        return 1;
    }
    return 0;
}
void do_cmd_store(void){gb_modern_store_active=1;gb_original_do_cmd_store();gb_modern_store_active=0;}
int gb_native_store_info(native_store_info *out){
    memset(out,0,sizeof(*out));if(!gb_modern_store_active||!st_ptr)return 0;
    out->active=1;out->kind=cur_store_num==STORE_HOME?2:(cur_store_num==STORE_MUSEUM?3:1);
    out->top=store_top;out->page_size=MAX(1,store_bottom);out->selection_style=3;out->root_frame=-1;
    out->get_key=out->kind==3?'r':(out->kind==2?'g':'p');out->put_key=out->kind==1?'s':'d';
    out->previous_key='-';out->next_key=' ';snprintf(out->title,sizeof(out->title),"%s",f_name+f_info[cur_store_feat].name);
    if(out->kind==1) {
        cptr name=gb_lang_enabled()?new_store_name_en[p_ptr->town_num][cur_store_num]:new_store_name[p_ptr->town_num][cur_store_num];
        cptr owner=gb_lang_enabled()?new_store_owner_name_en[p_ptr->town_num][cur_store_num]:new_store_owner_name[p_ptr->town_num][cur_store_num];
        snprintf(out->heading,sizeof(out->heading),"%s\n%s\n%s",name,owner,out->title);
    }
    return 1;
}
int gb_native_store_item(int index,native_store_item *out){
    object_type *obj;int slot,seen=0,own;long price=0;
    if(!gb_modern_store_active||!st_ptr)return 0;own=index>=st_ptr->stock_num;memset(out,0,sizeof(*out));out->selection=-1;
    if(!own){slot=index;obj=&st_ptr->stock[slot];out->tab=0;out->slot=slot;out->position=slot;
        out->key=slot>=store_top&&slot<store_top+store_bottom?'a'+slot-store_top:0;
        if(cur_store_num!=STORE_HOME&&cur_store_num!=STORE_MUSEUM){
            if(obj->ident&IDENT_FIXED)price=price_item(obj,min_inflate(),FALSE);
            else if(!manual_haggle){price=price_item(obj,min_inflate(),FALSE);if(!noneedtobargain(price))price+=price/10;}
            else price=price_item(obj,max_inflate(),FALSE);out->price_known=1;
        }
    }else{
        index-=st_ptr->stock_num;obj=NULL;
        for(slot=0;slot<INVEN_TOTAL;++slot)if(inventory[slot].k_idx&&seen++==index){obj=&inventory[slot];break;}
        if(!obj)return 0;out->tab=1;out->slot=slot;out->position=slot;out->location=slot>=INVEN_RARM?1:0;out->key=index_to_label(slot);
        if(cur_store_num!=STORE_HOME&&cur_store_num!=STORE_MUSEUM&&object_is_known(obj)){price=price_item(obj,min_inflate(),TRUE);out->price_known=1;}
    }
    out->count=obj->number;out->price=(int)price;out->available=own?store_will_buy(obj):1;
    /* 店の商品は IDENT_STORE でも種類の板にする（変愚・Frox と同じ。契約 §2-8）。 */
    out->kind_id=(object_is_aware(obj)||(obj->ident&IDENT_STORE))?obj->k_idx:0;out->unaware_category=gb_unaware_category(obj);
    out->glyph[0]=object_char(obj);out->glyph_color=object_attr(obj);
    object_desc(out->name,obj,0);
#ifdef JP
    snprintf(out->weight,sizeof(out->weight),"%d.%d kg",lbtokg1(obj->weight),lbtokg2(obj->weight));
#else
    snprintf(out->weight,sizeof(out->weight),"%d.%d lb",obj->weight/10,obj->weight%10);
#endif
    snprintf(out->description,sizeof(out->description),"%s",out->name);
    if(object_is_known(obj)&&k_info[obj->k_idx].text)snprintf(out->description,sizeof(out->description),"%s\n%s",out->name,k_text+k_info[obj->k_idx].text);
    return 1;
}

/* Explicit diagnostic entry, used only with a copied save and test data root. */
int gb_native_test_open(int kind){
    int i;int old_feat,old_mimic;
    cave_type *grid=&cave[py][px];
    if(kind==-2)kind=STORE_HOME;if(kind==-3)kind=STORE_MUSEUM;
    for(i=1;i<max_f_idx;++i)if(have_flag(f_info[i].flags,FF_STORE)&&f_info[i].subtype==kind)break;
    if(i==max_f_idx)return 0;
    old_feat=grid->feat;old_mimic=grid->mimic;grid->feat=i;grid->mimic=0;
    do_cmd_store();grid->feat=old_feat;grid->mimic=old_mimic;return 1;
}
