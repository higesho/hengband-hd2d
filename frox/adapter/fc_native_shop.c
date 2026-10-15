/* Compile the unmodified core translation unit through an adapter entry point. */
#define shop_ui fc_original_shop_ui
#include "shop.c"
#undef shop_ui
#include "fc_native_store_common.h"
static _ui_context_ptr fc_modern_shop_context=NULL;
void shop_ui(shop_ptr shop){
    _ui_context_t context={0};
    store_hack=TRUE;_maintain(shop);context.shop=shop;context.top=1;
    fc_modern_shop_context=&context;_loop(&context);fc_modern_shop_context=NULL;store_hack=FALSE;
}
int fc_native_shop_info(native_store_info *out){
    _ui_context_ptr c=fc_modern_shop_context;
    slot_t slot,max;
    memset(out,0,sizeof(*out));if(!c)return 0;
    out->active=1;out->kind=1;out->top=c->top-1;out->page_size=MAX(1,c->page_size);out->root_frame=fc_native_root(c->doc);
    out->own_page_size=MAX(1,MIN(26,ui_shop_rect().cy-6));out->selection_style=1;out->giving=no_selling?1:0;
    out->get_key='p';out->put_key='s';out->previous_key='-';out->next_key=' ';
    out->buy_all_key='B';out->show_hidden_key='U';out->hidden_count=_shop_unwanted_count(c->shop);
    max=inv_last(c->shop->inv,obj_exists);
    for(slot=1;slot<=max;++slot){obj_ptr obj=inv_obj(c->shop->inv,slot);if(obj)out->buy_all_total+=_sell_price(c->shop,obj_value(obj))*obj->number;}
    snprintf(out->title,sizeof(out->title),"%s",fc_shop(c->shop->type->name));
    snprintf(out->heading,sizeof(out->heading),"%s (%d)\n%s (%s)",out->title,c->shop->owner->purse,
        fc_shop_owner(c->shop->owner->name),fc_tr(get_race_aux(c->shop->owner->race_id,0)->name));return 1;
}
int fc_native_shop_item(int index,native_store_item *out){
    _ui_context_ptr c=fc_modern_shop_context;int max,pos,loc,slot,j,ct;
    obj_ptr obj;_sell_choice_t choices[PACK_MAX+QUIVER_MAX];
    if(!c)return 0;max=_shop_page_max(c->shop);
    if(index<max){
        pos=index+1;slot=_shop_page_slot(c->shop,pos);obj=slot?inv_obj(c->shop->inv,slot):NULL;if(!obj)return 0;
        fc_native_item(obj,out);out->tab=0;out->slot=index;out->position=slot;
        if(!(obj->marked&OM_RESERVED))native_item_command(out,'R');
        native_item_command(out,'H');
        out->key=pos>=c->top&&pos<c->top+c->page_size?slot_label(pos-c->top+1):0;
        out->price=_sell_price(c->shop,obj_value(obj));out->price_known=1;return 1;
    }
    obj=fc_native_own(index-max,&loc,&slot);if(!obj)return 0;
    fc_native_item(obj,out);out->tab=1;out->location=loc;out->slot=(loc+1)*1000+slot;out->position=slot;
    out->available=0;out->key=slot_label(slot);
    ct=_build_sell_choices(c->shop,choices);
    {int size=MAX(1,MIN(26,ui_shop_rect().cy-6));for(j=0;j<ct;j+=size)_label_sell_choice_page(choices,j,MIN(ct,j+size));}
    for(j=0;j<ct;++j)if(choices[j].obj==obj){out->available=1;out->selection=j;out->key=choices[j].label;out->price=no_selling?0:choices[j].price;out->price_known=1;break;}
    if(!out->available)snprintf(out->reason,sizeof(out->reason),"%s",fc_tr("You have nothing to sell."));
    return 1;
}

/* Explicit diagnostic entry; normal gameplay never calls it. */
int fc_native_test_open(int kind){
    town_ptr town;shop_ptr shop;
    if(kind==-2){home_ui();return 1;}if(kind==-3){museum_ui();return 1;}
    town=towns_current_town();if(!town)return 0;
    shop=town_get_shop(town,kind);if(!shop)return 0;shop_ui(shop);return 1;
}
