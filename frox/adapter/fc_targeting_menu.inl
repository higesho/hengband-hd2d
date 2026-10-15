/* Included after fc_targeting.inl so menu and aiming use the same geometry. */
typedef struct _fc_menu_spell {ang_spell spell;} _fc_menu_spell;
typedef struct _fc_menu_book {int realm,spell;} _fc_menu_book;

static void _fc_preview_fixed(void)
{
    _fc_target_context context={0,0,0,TRUE,0};
    native_target_request request={0};native_target_hooks hooks={0};
    const native_target_profile *profile=hb_target_profile();
    request.width=cur_wid;request.height=cur_hgt;request.source_x=px;request.source_y=py;
    request.mode=NATIVE_TARGET_FIXED;request.kind=profile->kind;request.label=profile->label;
    hooks.preview=_fc_target_preview;
    hb_target_preview_fixed(&request,&hooks,&context);
}
void fc_target_item_preview(void *actor,int action,const void *source)
{
    obj_ptr obj=(obj_ptr)source;
    _fc_target_rng saved=_fc_target_rng_save();int length=project_length;(void)actor;
    if(obj && obj->k_idx && object_is_known(obj)
        && ((action=='u' && obj->tval==TV_STAFF)||(action=='z' && obj->tval==TV_ROD)||action=='A')){
        effect_t effect=obj_get_effect(obj);
        fc_target_device_profile(&effect);_fc_preview_fixed();
    }
    _fc_target_rng_check("frox-item-plan-rng",&saved);
    hb_target_trace_invariant("frox-item-plan-range",length==project_length);
}
static void _fc_build_menu_spell(void *data)
{
    const _fc_menu_spell *entry=data;
    _fc_target_rng saved=_fc_target_rng_save();int length=project_length;
    if(hb_target_has_plan((native_target_plan_symbol)entry->spell)){
        variant result;var_init(&result);
        entry->spell(HB_TARGET_PLAN_MODE,&result);_fc_preview_fixed();var_clear(&result);
    }
    _fc_target_rng_check("frox-menu-plan-rng",&saved);
    hb_target_trace_invariant("frox-menu-plan-range",length==project_length);
}
static void _fc_build_menu_book(void *data)
{
    const _fc_menu_book *entry=data;
    _fc_target_rng saved=_fc_target_rng_save();int length=project_length;
    if(hb_target_has_plan((native_target_plan_symbol)do_spell)){
        do_spell(entry->realm,entry->spell,HB_TARGET_PLAN_MODE);_fc_preview_fixed();
    }
    _fc_target_rng_check("frox-menu-plan-rng",&saved);
    hb_target_trace_invariant("frox-menu-plan-range",length==project_length);
}
/*
 * 呪文の一覧の行の位置の控え（唱えられるかに関わらず全部の行）。覚える（G）・調べる（b）の一覧は
 * 唱えられる行が 1 つも無いので、プレビューの行（hb_target_menu_row）が登録されない。そのため
 * アダプタのカーソル探し（fc_menu.cpp の fill_core_cursor）が一覧の `>` を見つけられず、画面の別の字
 * （「(1)」）を選択肢と取り違えて、パッドやマウスでは呪文を選べなかった（遊ぶ人に届いた報告 2026-10-06）。
 * 先頭の行（キー a）で控えを空にする——一覧は描き直すたびに先頭から全部の行を描く。
 */
#define FC_BOOK_LIST_MAX 64
static int _fc_book_list_rows[FC_BOOK_LIST_MAX];
static int _fc_book_list_count;
static void _fc_book_list_note(int key,int row)
{
    if(Term!=angband_term[0])return;
    if(key=='a')_fc_book_list_count=0;
    if(_fc_book_list_count<FC_BOOK_LIST_MAX)_fc_book_list_rows[_fc_book_list_count++]=row;
}
int fc_book_list_count(void){return _fc_book_list_count;}
int fc_book_list_row(int index){return (index>=0&&index<_fc_book_list_count)?_fc_book_list_rows[index]:-1;}
void fc_target_menu_spell_row(int key,int row,int column,const char *rendered,ang_spell spell,int eligible)
{
    _fc_menu_spell entry={spell};
    _fc_book_list_note(key,row);
    if(eligible&&Term==angband_term[0])hb_target_menu_row(key,row,column,rendered,_fc_build_menu_spell,&entry,sizeof(entry));
}
void fc_target_menu_book_row(int key,int row,int column,const char *rendered,int realm,int spell,int eligible)
{
    _fc_menu_book entry={realm,spell};
    _fc_book_list_note(key,row);
    if(eligible&&Term==angband_term[0])hb_target_menu_row(key,row,column,rendered,_fc_build_menu_book,&entry,sizeof(entry));
}
