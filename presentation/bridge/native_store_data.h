#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct native_store_info {
    int active,kind,top,page_size,own_page_size,selection_style,root_frame,giving;
    int get_key,put_key,remove_key,previous_key,next_key;
    int buy_all_key,buy_all_total,show_hidden_key,hidden_count;
    char title[256];
    char heading[1024];
} native_store_info;
typedef struct native_store_item {
    int tab,slot,position,selection,location,key,kind_id,count,price,price_known,available,glyph_color;
    char glyph[8],name[1024],weight[64],description[8192],reason[256];
    char commands[32]; /* Canonical commands applicable to this object; resolved through the core keymap. */
    /*!
     * 正体の分からないアイテムの分類。0 = 対象外
     * （識別済み・8 分類の外）、1..8 は `presentation::UnawareItemCategory` と同じ並び
     * （1=薬 2=巻物 3=指輪 4=アミュレット 5=ワンド 6=杖 7=ロッド 8=キノコ）。
     * `kind_id` が 0（未鑑定で隠す従来の形）でも、こちらは立てておく——
     * 呼ぶ側（C++ アダプタ）が代表タイルを引けるようにするため。
     */
    int unaware_category;
} native_store_item;
static inline void native_item_command(native_store_item *item,int command) {
    int i;for(i=0;i<31&&item->commands[i];++i)if((unsigned char)item->commands[i]==command)return;
    if(i<31){item->commands[i]=(char)command;item->commands[i+1]=0;}
}
/* Remove a command the original selector cannot apply to this row (e.g. equipped items). */
static inline void native_item_drop_command(native_store_item *item,int command) {
    int i,j;for(i=0,j=0;i<31&&item->commands[i];++i)if((unsigned char)item->commands[i]!=command)item->commands[j++]=item->commands[i];
    item->commands[j]=0;
}
#ifdef __cplusplus
}
#endif
