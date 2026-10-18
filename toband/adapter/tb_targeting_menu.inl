/* Existing book selection shares the ordinary targeting geometry.
 * 幻想蛮怒の gb_targeting_menu.inl と同じ作り。TOband の呪文の振り分けは do_spell() で、
 * 中の do_spell_aux() が計画の合図（hb_target_mark_plan）を出す（tools/targeting/menu_profiles.py）。 */
typedef struct _tb_menu_book {int realm,spell;} _tb_menu_book;
void tb_target_preview_fixed(void)
{
    _tb_target_context context={0,0,TRUE,0,0};
    native_target_request request={0};native_target_hooks hooks={0};
    const native_target_profile *profile=hb_target_profile();
    request.width=cur_wid;request.height=cur_hgt;request.source_x=px;request.source_y=py;
    request.mode=NATIVE_TARGET_FIXED;request.kind=profile->kind;request.label=profile->label;hooks.preview=_tb_target_preview;
    hb_target_preview_fixed(&request,&hooks,&context);
}
void tb_target_item_preview(void *actor,int action,const void *object)
{
    _tb_target_rng saved=_tb_target_rng_save();int length=project_length;(void)actor;
    tb_target_item_profile(action,object);tb_target_preview_fixed();
    _tb_target_rng_check("toband-item-plan-rng",&saved);
    hb_target_trace_invariant("toband-item-plan-range",length==project_length);
}
static void _tb_build_menu_book(void *data)
{
    const _tb_menu_book *entry=data;
    _tb_target_rng saved=_tb_target_rng_save();int length=project_length;
    if(hb_target_has_plan((native_target_plan_symbol)do_spell)){
        do_spell(entry->realm,entry->spell,HB_TARGET_PLAN_MODE);
        tb_target_preview_fixed();
    }
    _tb_target_rng_check("toband-menu-plan-rng",&saved);
    hb_target_trace_invariant("toband-menu-plan-range",length==project_length);
}
void tb_target_menu_book_row(int key,int row,int column,const char *rendered,const char *name,int realm,int spell,int eligible,int selected)
{
    _tb_menu_book entry={realm,spell};
    if(Term==angband_term[0]){
        hb_target_menu_row(key,row,column,rendered,eligible?_tb_build_menu_book:NULL,&entry,sizeof(entry));
        hb_target_menu_row_text(tb_tr(name),rendered,selected);
    }
}
