#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Called on the core thread while the original item selector owns its objects. */
void hb_ui_item_scope(int enter,int sources);
void hb_ui_item_begin(void);
void hb_ui_item_add(const void *object,int location,int slot,int key,int source_row,int selected);
void hb_ui_item_empty(int location,int slot,int key,int source_row,int selected,const char *label);
const void *hb_ui_item_take_choice(void);
#ifdef __cplusplus
}
struct NativeItemDisplayScope {
    explicit NativeItemDisplayScope(int sources){hb_ui_item_scope(1,sources);}
    ~NativeItemDisplayScope(){hb_ui_item_scope(0,0);}
};
#endif
