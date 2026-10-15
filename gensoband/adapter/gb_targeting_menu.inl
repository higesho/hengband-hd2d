/* Existing book selection shares the ordinary targeting geometry. */
typedef struct _gb_menu_book {int realm,spell;} _gb_menu_book;
void gb_target_preview_fixed(void)
{
    _gb_target_context context={0,0,TRUE,0};
    native_target_request request={0};native_target_hooks hooks={0};
    const native_target_profile *profile=hb_target_profile();
    request.width=cur_wid;request.height=cur_hgt;request.source_x=px;request.source_y=py;
    request.mode=NATIVE_TARGET_FIXED;request.kind=profile->kind;request.label=profile->label;hooks.preview=_gb_target_preview;
    hb_target_preview_fixed(&request,&hooks,&context);
}
void gb_target_item_preview(void *actor,int action,const void *object)
{
    _gb_target_rng saved=_gb_target_rng_save();int length=project_length;(void)actor;
    gb_target_item_profile(action,object);gb_target_preview_fixed();
    _gb_target_rng_check("genso-item-plan-rng",&saved);
    hb_target_trace_invariant("genso-item-plan-range",length==project_length);
}
static void _gb_build_menu_book(void *data)
{
    const _gb_menu_book *entry=data;
    _gb_target_rng saved=_gb_target_rng_save();int length=project_length;
    if(hb_target_has_plan((native_target_plan_symbol)do_spell)){
        do_spell(entry->realm,entry->spell,HB_TARGET_PLAN_MODE);
        gb_target_preview_fixed();
    }
    _gb_target_rng_check("genso-menu-plan-rng",&saved);
    hb_target_trace_invariant("genso-menu-plan-range",length==project_length);
}
void gb_target_menu_book_row(int key,int row,int column,const char *rendered,const char *name,int realm,int spell,int eligible,int selected)
{
    _gb_menu_book entry={realm,spell};
    if(Term==angband_term[0]){
        hb_target_menu_row(key,row,column,rendered,eligible?_gb_build_menu_book:NULL,&entry,sizeof(entry));
        hb_target_menu_row_text(gb_tr(name),rendered,selected);
    }
}
