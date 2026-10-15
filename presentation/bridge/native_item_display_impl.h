#pragma once
#include "bridge/native_item_display.h"
#include "bridge/native_store_data.h"
#include "bridge/native_modern.h"
#include "bridge/native_targeting_api.h"
#include <vector>

// Include once in each core adapter. These pointers never cross a thread or a frame.
namespace presentation {
struct NativeItemRef {const void *object;int location,slot,key,source_row,selected;std::string empty_label;};
static std::vector<int> native_item_sources;
static std::vector<NativeItemRef> native_display_items;
static std::vector<std::pair<std::string,const void *>> native_item_choices;
static const void *native_item_choice=nullptr;
inline bool select_native_item(const std::string &id) {
    for(const auto &entry:native_item_choices)if(entry.first==id&&entry.second){native_item_choice=entry.second;return true;}
    return false;
}
template<class Convert,class Tile>
ModernRow native_item_model(const native_store_item &raw,int slot,int location,int key,Convert convert,Tile tile) {
    ModernRow row;row.slot=slot;row.selection=location;row.key=row.hotkey=key;
    row.count=raw.count;row.price_known=false;row.available=raw.available!=0;row.equipped=location==1;
    row.label=convert(raw.name);row.description=convert(raw.description);row.weight=raw.weight;
    row.glyph=convert(raw.glyph);row.glyph_color=raw.glyph_color;
    // 正体の分からないアイテム: kind_id が 0 でも代表タイルを引く。
    // tile(kind_id, unaware_category) ―― kind_id!=0 なら種類の板、それ以外は unaware_category（0=無し）で決める。
    row.tile=tile(raw.kind_id,raw.unaware_category);
    unsigned long long hash=1469598103934665603ULL;for(unsigned char c:row.label){hash^=c;hash*=1099511628211ULL;}
    row.id="item:"+std::to_string(location)+":"+std::to_string(slot)+":"+std::to_string(hash)+":"+std::to_string(row.count);
    for(const char *c=raw.commands;*c;++c)row.actions.push_back({std::to_string(int(*c)),"","",true,int(*c)});
    return row;
}
template<class Read,class Convert,class Tile>
std::vector<NativeItemRow> read_native_item_rows(Read read,Convert convert,Tile tile,bool frox=false) {
    std::vector<NativeItemRow> rows;
    native_item_choices.clear();
    for(const auto &ref:native_display_items) {
        native_store_item raw{};
        if(ref.object)read(ref.object,&raw);
        else {raw.available=1;std::snprintf(raw.name,sizeof(raw.name),"%s",ref.empty_label.c_str());}
        if(!raw.count&&ref.empty_label.empty())continue;
        auto row=native_item_model(raw,frox?raw.slot:ref.slot,frox?raw.location:ref.location,ref.key,convert,tile);
        if(frox){native_item_choices.push_back({row.id,ref.object});if(ref.key==13)row.hotkey=0;}
        hb_target_item_menu_row(row.id.c_str(),ref.object);
        rows.push_back({std::move(row),ref.source_row,ref.selected,native_item_sources.empty()?0:native_item_sources.back()});
    }
    return rows;
}
template<class Read,class Convert,class Tile>
std::vector<ModernRow> read_native_owned_items(Read read,Convert convert,Tile tile) {
    std::vector<ModernRow> rows;
    for(int i=0;i<512;++i){native_store_item raw{};if(!read(i,&raw))break;if(raw.count)rows.push_back(native_item_model(raw,raw.slot,raw.location,raw.key,convert,tile));}
    return rows;
}
}
extern "C" void hb_ui_item_scope(int enter,int sources) {
    using namespace presentation;
    hb_target_item_menu_display(enter);
    if(enter)native_item_sources.push_back(sources);else if(!native_item_sources.empty())native_item_sources.pop_back();
    native_display_items.clear();
    native_item_choices.clear();native_item_choice=nullptr;
}
extern "C" const void *hb_ui_item_take_choice(void) {
    const auto result=presentation::native_item_choice;presentation::native_item_choice=nullptr;return result;
}
extern "C" void hb_ui_item_begin(void) {
    // A main selector and one or more inventory/equipment subwindows may be
    // painted before the adapter captures the frame.  Clearing here lets the
    // last subwindow erase the actual selectable list.  The surrounding item
    // scope owns the lifetime; individual paint passes update rows in place.
    hb_target_item_menu_clear();
}
extern "C" void hb_ui_item_add(const void *object,int location,int slot,int key,int source_row,int selected) {
    if(!presentation::native_item_sources.empty()&&object) {
        auto &items=presentation::native_display_items;
        const auto found=std::find_if(items.begin(),items.end(),[&](const auto &item){return item.location==location&&item.slot==slot;});
        presentation::NativeItemRef value{object,location,slot,key,source_row,selected,{}};
        if(found==items.end())items.push_back(std::move(value));else *found=std::move(value);
    }
}
extern "C" void hb_ui_item_empty(int location,int slot,int key,int source_row,int selected,const char *label) {
    if(!presentation::native_item_sources.empty()) {
        auto &items=presentation::native_display_items;
        const auto found=std::find_if(items.begin(),items.end(),[&](const auto &item){return item.location==location&&item.slot==slot;});
        presentation::NativeItemRef value{nullptr,location,slot,key,source_row,selected,label?label:""};
        if(found==items.end())items.push_back(std::move(value));else *found=std::move(value);
    }
}
