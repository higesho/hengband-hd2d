/* Record the native look call and its screen depth, without changing the core. */
#define do_cmd_look sq_original_do_cmd_look
#include "cmd3.c"
#undef do_cmd_look
#include "bridge/native_store_data.h"
#include "sq_unaware_category.h"

static char *sq_item_info_buffer;
static size_t sq_item_info_used;
static void sq_item_info_text(byte color,cptr str)
{
    size_t n=strlen(str),room=8191-sq_item_info_used;
    (void)color;if(n>room)n=room;
    memcpy(sq_item_info_buffer+sq_item_info_used,str,n);sq_item_info_used+=n;sq_item_info_buffer[sq_item_info_used]=0;
}
void sq_native_describe_item(const void *source,native_store_item *out)
{
    const object_type *obj=(const object_type *)source;
    void (*old_text)(byte,cptr)=text_out_hook;
    void (*old_flags)(const object_type*,u32b*,u32b*,u32b*)=object_info_out_flags;
    memset(out,0,sizeof(*out));out->available=1;out->selection=-1;out->count=obj->number;
    if(!obj->k_idx)return;
    out->kind_id=(object_aware_p(obj)||object_known_p(obj))?obj->k_idx:0;
    out->unaware_category=sq_unaware_category(obj);
    out->glyph[0]=object_char(obj);out->glyph_color=object_attr(obj);
    object_desc(out->name,sizeof(out->name),obj,TRUE,3);
    snprintf(out->weight,sizeof(out->weight),"%d.%d lb",obj->weight/10,obj->weight%10);
    sq_item_info_buffer=out->description;sq_item_info_used=0;
    if(object_known_p(obj)&&obj->name1&&!adult_rand_artefacts&&a_info[obj->name1].text)sq_item_info_text(0,a_text+a_info[obj->name1].text);
    else if(object_aware_p(obj)&&k_info[obj->k_idx].text)sq_item_info_text(0,k_text+k_info[obj->k_idx].text);
    if(sq_item_info_used)sq_item_info_text(0,"\n\n");
    text_out_hook=sq_item_info_text;object_info_out_flags=object_flags_known;
    object_info_out(obj);
    object_info_out_flags=old_flags;text_out_hook=old_text;sq_item_info_buffer=NULL;
    native_item_command(out,'x');native_item_command(out,'{');native_item_command(out,'u');
    native_item_command(out,'d');native_item_command(out,'k');native_item_command(out,'v');
    if(item_tester_hook_wear(obj))native_item_command(out,'w');
    if(obj->tval==TV_POTION)native_item_command(out,'q');
    if(obj->tval==TV_FOOD)native_item_command(out,'E');
    if(obj->tval==TV_STAFF)native_item_command(out,'a');
    if(obj->tval==TV_HORN)native_item_command(out,'p');
    if(obj->tval==TV_ARROW)native_item_command(out,'f');
}
int sq_native_owned_item(int index,native_store_item *out)
{
    int slot,seen=0;
    for(slot=0;slot<INVEN_TOTAL;++slot)if(inventory[slot].k_idx&&seen++==index){
        sq_native_describe_item(&inventory[slot],out);out->slot=slot;out->location=slot>=INVEN_WIELD?1:0;out->key=index_to_label(slot);
        /* 装備中の品は wield（w）と破壊（k）の選択肢に載らない（持ち物と床だけ）。載せると
         * 持ち物メニューが原作の選択肢の中で行を探し続けて諦める（2026-09-25 の監査）。 */
        if(out->location==1){native_item_command(out,'r');native_item_drop_command(out,'w');native_item_drop_command(out,'k');}
        return 1;
    }
    return 0;
}

static int sq_look_active;
static int sq_look_screen_depth;

void do_cmd_look(void)
{
    const int previous_active = sq_look_active;
    const int previous_depth = sq_look_screen_depth;
    sq_look_active = 1;
    sq_look_screen_depth = character_icky;
    sq_original_do_cmd_look();
    sq_look_active = previous_active;
    sq_look_screen_depth = previous_depth;
}

int sq_screen_depth(void)
{
    return (int)character_icky;
}

int sq_native_look_map_active(void)
{
    return sq_look_active && character_icky == sq_look_screen_depth;
}
