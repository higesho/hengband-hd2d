#pragma once
#include "bridge/native_store_data.h"
#include "fc_unaware_category.h"
static int fc_native_root(doc_ptr doc) {
    rect_t r=ui_shop_rect();int x,y,count=0;doc_pos_t end;
    if(!doc||!Term||!Term->scr)return 0;end=doc_cursor(doc);
    for(y=0;y<=end.y&&y<r.cy&&y+r.y<Term->hgt;++y)
        for(x=0;x<doc_width(doc)&&x+r.x<Term->wid;++x){
            doc_char_ptr c=doc_char(doc,doc_pos_create(x,y));unsigned char wanted=c&&c->c?(unsigned char)c->c:' ';
            if((unsigned char)Term->scr->c[y+r.y][x+r.x]!=wanted)return 0;
            if(wanted!=' ')++count;
        }
    return count>10;
}
static void fc_native_item(obj_ptr source,native_store_item *out) {
    obj_t copy=*source;
    doc_ptr doc;
    doc_pos_t end;
    int x,y,n=0;
    memset(out,0,sizeof(*out));out->available=1;out->selection=-1;
    out->count=source->number;out->kind_id=(object_is_aware(source)||(source->ident & IDENT_STORE))?source->k_idx:0;
    out->unaware_category=fc_unaware_category(source);
    object_desc(out->name,&copy,0);
    sprintf(out->weight,"%d.%d lb",source->weight/10,source->weight%10);
    out->glyph[0]=object_char(source);out->glyph_color=object_attr(source);
    doc=doc_alloc(72);obj_display_doc(&copy,doc);end=doc_cursor(doc);
    for(y=0;y<=end.y&&n<(int)sizeof(out->description)-2;++y){
        int last=n;
        for(x=0;x<72&&n<(int)sizeof(out->description)-2;++x){doc_char_ptr c=doc_char(doc,doc_pos_create(x,y));char v=c?c->c:' ';out->description[n++]=v?v:' ';if(v&&v!=' ')last=n;}
        n=last;out->description[n++]='\n';
    }
    out->description[n]=0;doc_free(doc);
}
static obj_ptr fc_native_own(int index,int *location,int *slot_out){
    int loc,slot,seen=0;
    for(loc=0;loc<3;++loc){int max=loc==0?pack_max():(loc==1?equip_max():quiver_max());
        for(slot=1;slot<=max;++slot){obj_ptr obj=loc==0?pack_obj(slot):(loc==1?equip_obj(slot):quiver_obj(slot));
            if(obj&&seen++==index){*location=loc;*slot_out=slot;return obj;}
        }
    }
    return NULL;
}
