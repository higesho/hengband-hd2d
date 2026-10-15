#define home_ui fc_original_home_ui
#define museum_ui fc_original_museum_ui
#include "home.c"
#undef home_ui
#undef museum_ui
#include "fc_native_store_common.h"
static _ui_context_ptr fc_modern_home_context=NULL;
void home_ui(void){_ui_context_t context={0};context.inv=_home;context.top=1;fc_modern_home_context=&context;_ui(&context);fc_modern_home_context=NULL;}
void museum_ui(void){_ui_context_t context={0};context.inv=_museum;context.top=1;fc_modern_home_context=&context;_ui(&context);fc_modern_home_context=NULL;}
int fc_native_home_info(native_store_info *out){
    _ui_context_ptr c=fc_modern_home_context;memset(out,0,sizeof(*out));if(!c)return 0;
    out->active=1;out->kind=inv_loc(c->inv)==INV_MUSEUM?3:2;out->top=c->top-1;out->page_size=MAX(1,c->page_size);out->selection_style=2;out->root_frame=fc_native_root(c->doc);
    out->get_key=out->kind==3?'r':'g';out->put_key='d';out->remove_key='r';out->previous_key='-';out->next_key=' ';
    snprintf(out->title,sizeof(out->title),"%s",fc_shop(inv_name(c->inv)));return 1;
}
int fc_native_home_item(int index,native_store_item *out){
    _ui_context_ptr c=fc_modern_home_context;int max,slot,loc;obj_ptr obj;if(!c)return 0;
    max=inv_last(c->inv,obj_exists);
    if(index<max){slot=index+1;obj=inv_obj(c->inv,slot);if(!obj){memset(out,0,sizeof(*out));out->slot=index;return 1;}
        fc_native_item(obj,out);out->tab=0;out->slot=index;out->position=slot;
        out->key=slot>=c->top&&slot<c->top+c->page_size?slot_label(slot-c->top+1):0;return 1;}
    obj=fc_native_own(index-max,&loc,&slot);if(!obj)return 0;
    fc_native_item(obj,out);out->tab=1;out->location=loc;out->slot=(loc+1)*1000+slot;out->position=slot;out->key=obj_label(obj);if(!out->key)out->key=slot_label(slot);return 1;
}
