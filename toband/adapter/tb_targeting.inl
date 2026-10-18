/* Included by the generated xtra2.c; original gameplay tails remain intact.
 *
 * TOband（変愚蛮怒 1.5.1 系）の「狙いの画面」の受け口。幻想蛮怒の `gb_targeting.inl` と同じ作りで、
 * TOband の原作に合わせて次の所だけ違う。
 *   - 候補は原作の target_set_prepare() と同じく「いまの画面（パネル）の中の見えるモンスター」。
 *   - ironman_nightmare・輝夜・踊り子の騎乗は TOband に無いので持たない。
 *   - tgt_pt() の allow_player が偽なら、自分のマスは選べない。
 * 乱数・ゲームの状態には触れない（プレビューの前後で乱数の状態を比べる）。 */
#include "tb_lang_c.h"
typedef struct _tb_target_context { int mode,eight_way,point_only,range,forbid_self; } _tb_target_context;
/* This core's xorshift RNG uses Rand_state[0..RAND_DEG-1]; no Rand_quick/value mode is used here. */
typedef struct _tb_target_rng {u16b place;u32b state[RAND_DEG];} _tb_target_rng;
static _tb_target_rng _tb_target_rng_save(void)
{
    _tb_target_rng saved;saved.place=Rand_place;
    memcpy(saved.state,Rand_state,sizeof(saved.state));return saved;
}
static void _tb_target_rng_check(const char *name,const _tb_target_rng *saved)
{
    hb_target_trace_invariant(name,saved->place==Rand_place
        &&memcmp(saved->state,Rand_state,sizeof(saved->state))==0);
}
extern int tb_target_mapped_direction(void);
static int _tb_target_movement_pending(void)
{
    const char *site=hb_target_profile()->site;
    return site && strncmp(site,"movement:",9)==0 && tb_target_mapped_direction()!=0;
}
static int _tb_target_eligible(int index,_tb_target_context *ctx)
{
    if (index<=0 || index>=m_max || !target_able(index)) return FALSE;
    (void)ctx;
    return TRUE;
}
static void _tb_target_candidates(void *data)
{
    _tb_target_context *ctx=data;int index;
    _tb_target_rng rng=_tb_target_rng_save();
    for (index=m_max-1;index>0;--index) {
        monster_type *mon=&m_list[index];char name[160];
        if (!_tb_target_eligible(index,ctx)) continue;
        if (hb_target_profile()->kind==NATIVE_TARGET_SUPPORT) {
            if (!is_pet(mon) && !is_friendly(mon) && index!=p_ptr->riding) continue;
        } else if (!target_pet && is_pet(mon)) continue; /* original: only pets are dropped; friendly monsters stay */
        {
            /* original target_set_prepare(TARGET_KILL): current panel, double-distance order, scan order for ties */
            int kx,ky;
            if (mon->fy<panel_row_min || mon->fy>panel_row_max || mon->fx<panel_col_min || mon->fx>panel_col_max) continue;
            if (!target_set_accept(mon->fy,mon->fx)) continue;
            kx=ABS(mon->fx-px);ky=ABS(mon->fy-py);
            monster_desc(name,mon,0);
            hb_target_candidate(index,mon->fx,mon->fy,(kx>ky)?(kx+kx+ky):(ky+ky+kx),0,mon->fy*cur_wid+mon->fx,name,TRUE);
        }
    }
    _tb_target_rng_check("toband-candidates-rng",&rng);
}
/* Monster recall ('r') while choosing a target. The original target_set_aux() answers 'r' on a
 * monster by showing its recall (screen_roff()); the native toolbar has no recall button outside of
 * the "look" mode, so a raw 'r' was ignored. It is offered as an "extra" operation (key 'r', also
 * reachable from the Actions list). The recall screen is the core's own screen, so the targeting
 * state is closed while it runs. */
static int _tb_target_recall_ready=FALSE;
static int _tb_target_recall_index(const native_target_choice *choice)
{
    int index;
    if (!choice||choice->id<=0||choice->id>=m_max) return 0;
    index=choice->id;
    if (!m_list[index].r_idx||!m_list[index].ml||p_ptr->image) return 0;
    return index;
}
static void _tb_target_emit_extras(const native_target_choice *choice,const _tb_target_context *ctx)
{
    /* Direction-only prompts (tunnel, open ...) have no creature cursor. */
    if (ctx->eight_way) {_tb_target_recall_ready=FALSE;return;}
    _tb_target_recall_ready=_tb_target_recall_index(choice)!=0;
    /* tb_lang_enabled() is the English layer; the original text is Japanese (CP932), written as
     * escapes because this file is read as ASCII: "monster no omoide". */
    hb_target_extra('r',tb_lang_enabled()?"Monster Recall"
        :"\x83\x82\x83\x93\x83\x58\x83\x5e\x81\x5b\x82\xcc\x8e\x76\x82\xa2\x8f\x6f",_tb_target_recall_ready);
}
static int _tb_target_extra_screen(int key)
{
    return key=='r'&&_tb_target_recall_ready;
}
static int _tb_target_extra(int key,native_target_choice *choice,void *data)
{
    int index;
    (void)data;
    if (key!='r') return NATIVE_EXTRA_UNKNOWN;
    index=_tb_target_recall_index(choice);
    if (!index) return NATIVE_EXTRA_NONE;
    monster_race_track(m_list[index].ap_r_idx);
    health_track(index);
    handle_stuff();
    inkey_base=FALSE;
    screen_save();
    screen_roff(m_list[index].ap_r_idx,0);
    Term_addstr(-1,TERM_WHITE,"  [r]");
    (void)inkey();
    screen_load();
    return NATIVE_EXTRA_NONE;
}
/* カーソルのマスのポップアップ。中身は tb_shim.c の tb_elem_info が組む（原作の target_set_aux が
 * 調べる・狙うときの 1 行目に出すものと同じ。見る `l` の帯のポップアップ〔tb_frame.cpp〕と同じ関数を使う）。ここは受け口を共通の
 * API（hb_target_info_span / hb_target_info_newline）につなぐだけ。 */
/* 原作の target_set_aux() が敵の行の頭に出す「[あと何回]」の字（static な evaluate_monster_exp()）。xtra2.c の中（この .inl）からだけ呼べるので、
 * tb_shim.c の tb_elem_info() へ向けた入口を置く。buf は 10 バイト以上。 */
void tb_monster_exp_text(char *buf,int m_idx)
{
    if (m_idx<=0 || m_idx>=m_max) {buf[0]='\0';return;}
    evaluate_monster_exp(buf,&m_list[m_idx]);
}
extern void tb_elem_info(int y,int x,void (*span)(void *,int,const char *),void (*newline)(void *),void *user);
static void _tb_info_span(void *user,int color,const char *text) {(void)user;hb_target_info_span(color,text);}
static void _tb_info_newline(void *user) {(void)user;hb_target_info_newline();}
static void _tb_target_preview(native_target_choice *choice,void *data)
{
    _tb_target_context *ctx=data;const native_target_profile *profile=hb_target_profile();
    _tb_target_rng rng=_tb_target_rng_save();int original_length=project_length,stochastic=FALSE;
    int valid=TRUE,reason=NATIVE_TARGET_OK,index=0,d=distance(py,px,choice->y,choice->x);
    char name[160]="";
    if (!in_bounds2(choice->y,choice->x)) {valid=FALSE;reason=NATIVE_TARGET_OUTSIDE;}
    else {
        index=cave[choice->y][choice->x].m_idx;
        if (index && (!m_list[index].ml || p_ptr->image)) index=0;
        choice->id=index?index:(!d?-1:0);
        if (index) monster_desc(name,&m_list[index],0);
        /* free targeting: any grid may be picked; a creature that cannot be a creature target is a plain point */
        if (!ctx->eight_way && index && !_tb_target_eligible(index,ctx)) choice->id=0;
        if (!d && (ctx->eight_way==1 || ctx->forbid_self)) {valid=FALSE;reason=NATIVE_TARGET_SELF_FORBIDDEN;}
    }
    if (in_bounds2(choice->y,choice->x)) tb_elem_info(choice->y,choice->x,_tb_info_span,_tb_info_newline,NULL);
    if (hb_target_preview_enabled()&&valid&&!profile->supported) reason=NATIVE_TARGET_PROFILE_MISSING;
    if(!d){int i;for(i=0;i<profile->count;++i)if(profile->effects[i].shape==NATIVE_SHAPE_SHOT||profile->effects[i].shape==NATIVE_SHAPE_THROW){valid=FALSE;reason=NATIVE_TARGET_SELF_FORBIDDEN;}}
    choice->direction=5;
    if (ctx->eight_way && d) {
        int dir;double best=-2.0;
        double x=choice->x-px,y=choice->y-py;
        for (dir=1;dir<=9;++dir) if (dir!=5) {
            double score=(x*ddx[dir]+y*ddy[dir])/((ddx[dir]&&ddy[dir])?1.4142135623730951:1.0);
            if (score>best) {best=score;choice->direction=dir;}
        }
    }
    if(hb_target_preview_enabled()&&valid&&profile->supported&&profile->effect_known){
        int i;for(i=0;i<profile->count;++i){
            stochastic|=profile->effects[i].possible;
            int result=_tb_preview_effect(&profile->effects[i],choice);
            if(result<=0){if(result<0)valid=FALSE;reason=result<0?-result:NATIVE_TARGET_PROFILE_MISSING;break;}
        }
    }
    hb_target_status(valid,reason,d,name,stochastic,profile->effect_known);
    _tb_target_emit_extras(choice,ctx);
    _tb_target_rng_check("toband-preview-rng",&rng);
    hb_target_trace_invariant("toband-preview-range",original_length==project_length);
}
static int _tb_target_read(void *data) {(void)data;inkey_base=TRUE;return (unsigned char)inkey();}
static int _tb_target_direction(int key,void *data) {(void)data;return get_keymap_dir((char)key);}
static void _tb_target_refresh(void *data) {(void)data;Term_fresh();}
static int _tb_choose_target(int mode,int eight_way,int *direction,int *x,int *y)
{
    _tb_target_context ctx={mode,eight_way,x!=NULL,0,_tb_target_forbid_self};
    native_target_request request={0};native_target_choice choice={0};
    native_target_hooks hooks={_tb_target_candidates,_tb_target_preview,_tb_target_read,_tb_target_direction,_tb_target_refresh};
    const native_target_profile *profile=hb_target_profile();
    _tb_target_forbid_self=FALSE;
    hooks.extra=_tb_target_extra;hooks.extra_screen=_tb_target_extra_screen;
    if(profile->count){
        int i;for(i=0;i<profile->count;++i){
            int range=profile->effects[i].range?profile->effects[i].range:(project_length?project_length:MAX_RANGE);
            if(range>ctx.range)ctx.range=range;
        }
    }
    request.width=cur_wid;request.height=cur_hgt;request.source_x=px;request.source_y=py;
    request.mode=eight_way?NATIVE_TARGET_DIRECTION8:NATIVE_TARGET_POINT;request.kind=profile->kind;request.label=profile->label;
    if (target_who>0 && _tb_target_eligible(target_who,&ctx)) {
        request.previous_valid=TRUE;request.previous_id=target_who;
        request.previous_x=m_list[target_who].fx;request.previous_y=m_list[target_who].fy;
    } else if (target_who<0 && in_bounds2(target_row,target_col)) {
        request.previous_valid=TRUE;request.previous_x=target_col;request.previous_y=target_row;
    }
    if (!hb_target_choose(&request,&hooks,&ctx,&choice)) return FALSE;
    *direction=choice.direction;
    if (x&&y) {*x=choice.x;*y=choice.y;}
    else if (!eight_way) {
        target_who=choice.track_creature&&choice.id>0?choice.id:-1;
        target_col=choice.x;target_row=choice.y;
        if (choice.id>0) {monster_race_track(m_list[choice.id].ap_r_idx);health_track(choice.id);}
    }
    return TRUE;
}
