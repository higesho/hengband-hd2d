/* Included by the generated xtra2.c; original gameplay tails remain intact. */
typedef struct _gb_target_context { int mode,eight_way,point_only,range; } _gb_target_context;
/* This core's xorshift RNG uses Rand_state[0..3]; no Rand_quick/value mode exists. */
typedef struct _gb_target_rng {u16b place;u32b state[RAND_DEG];} _gb_target_rng;
static _gb_target_rng _gb_target_rng_save(void)
{
    _gb_target_rng saved;saved.place=Rand_place;
    memcpy(saved.state,Rand_state,sizeof(saved.state));return saved;
}
static void _gb_target_rng_check(const char *name,const _gb_target_rng *saved)
{
    hb_target_trace_invariant(name,saved->place==Rand_place
        &&memcmp(saved->state,Rand_state,sizeof(saved->state))==0);
}
extern int gb_target_mapped_direction(void);
static int _gb_target_movement_pending(void)
{
    const char *site=hb_target_profile()->site;
    return site && strncmp(site,"movement:",9)==0 && gb_target_mapped_direction()!=0;
}
static int _gb_target_eligible(int index,_gb_target_context *ctx)
{
    if (index<=0 || index>=m_max || !target_able(index)) return FALSE;
    (void)ctx;
    return TRUE;
}
static int _gb_target_ticks(int index)
{
    monster_type *mon=&m_list[index];int speed,gain,ticks;
    if (CLASS_RIDING_BACKDANCE && p_ptr->riding==index) {
        speed=mon->mspeed;
        if (ironman_nightmare) speed+=5;
        if (MON_FAST(mon)) speed+=10;
        if (MON_SLOW(mon)) speed-=10;
        if ((p_ptr->pclass==CLASS_MAI || p_ptr->pclass==CLASS_SATONO) && speed<p_ptr->pspeed)
            speed+=MIN(10,(p_ptr->pspeed-speed)/2);
    } else if (p_ptr->riding==index) speed=p_ptr->pspeed;
    else {
        speed=mon->mspeed;
        if (ironman_nightmare) speed+=5;
        if (MON_FAST(mon)) speed+=10;
        if (MON_SLOW(mon)) speed-=10;
    }
    gain=SPEED_TO_ENERGY(speed);
    ticks=gain<=0?1000000000:(mon->energy_need<=0?0:(mon->energy_need+gain-1)/gain);
    if (mon->mflag&MFLAG_BORN) ++ticks;
    return ticks;
}
static void _gb_target_candidates(void *data)
{
    _gb_target_context *ctx=data;int index;
    _gb_target_rng rng=_gb_target_rng_save();
    for (index=m_max-1;index>0;--index) {
        monster_type *mon=&m_list[index];char name[160];
        if (!_gb_target_eligible(index,ctx)) continue;
        if (mon->r_idx==MON_MASTER_KAGUYA) continue;
        if (hb_target_profile()->kind==NATIVE_TARGET_SUPPORT) {
            if (!is_pet(mon) && !is_friendly(mon) && index!=p_ptr->riding) continue;
        } else if (!target_pet && is_pet(mon)) continue; /* original: only pets are dropped; friendly monsters stay */
        {
            /* original target_set_prepare(TARGET_KILL): MAX_RANGE box, double-distance order, scan order for ties */
            int kx=ABS(mon->fx-px),ky=ABS(mon->fy-py);
            if (kx>MAX_RANGE || ky>MAX_RANGE) continue;
            monster_desc(name,mon,0);
            hb_target_candidate(index,mon->fx,mon->fy,(kx>ky)?(kx+kx+ky):(ky+ky+kx),0,mon->fy*cur_wid+mon->fx,name,TRUE);
        }
    }
    _gb_target_rng_check("genso-candidates-rng",&rng);
}
/* Monster recall ('r') while choosing a target. The original target_set_aux() answers 'r' on a
 * monster by showing its recall (screen_roff()); the native toolbar has no recall button outside of
 * the "look" mode, so a raw 'r' was ignored. It is offered as an "extra" operation (key 'r', also
 * reachable from the Actions list). The recall screen is the core's own screen, so the targeting
 * state is closed while it runs. */
static int _gb_target_recall_ready=FALSE;
static int _gb_target_recall_index(const native_target_choice *choice)
{
    int index;
    if (!choice||choice->id<=0||choice->id>=m_max) return 0;
    index=choice->id;
    if (!m_list[index].r_idx||!m_list[index].ml||p_ptr->image) return 0;
    return index;
}
static void _gb_target_emit_extras(const native_target_choice *choice,const _gb_target_context *ctx)
{
    /* Direction-only prompts (tunnel, open ...) have no creature cursor. */
    if (ctx->eight_way) {_gb_target_recall_ready=FALSE;return;}
    _gb_target_recall_ready=_gb_target_recall_index(choice)!=0;
    /* gb_lang_enabled() is the English layer; the original text is Japanese (CP932, written as bytes
     * because this file is not compiled as CP932): "monster no omoide". */
    hb_target_extra('r',gb_lang_enabled()?"Monster Recall"
        :"X^[Ìv¢o",_gb_target_recall_ready);
}
static int _gb_target_extra_screen(int key)
{
    return key=='r'&&_gb_target_recall_ready;
}
static int _gb_target_extra(int key,native_target_choice *choice,void *data)
{
    int index;
    (void)data;
    if (key!='r') return NATIVE_EXTRA_UNKNOWN;
    index=_gb_target_recall_index(choice);
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
static void _gb_target_preview(native_target_choice *choice,void *data)
{
    _gb_target_context *ctx=data;const native_target_profile *profile=hb_target_profile();
    _gb_target_rng rng=_gb_target_rng_save();int original_length=project_length,stochastic=FALSE;
    int valid=TRUE,reason=NATIVE_TARGET_OK,index=0,d=distance(py,px,choice->y,choice->x);
    char name[160]="";
    if (!in_bounds2(choice->y,choice->x)) {valid=FALSE;reason=NATIVE_TARGET_OUTSIDE;}
    else {
        index=cave[choice->y][choice->x].m_idx;
        if (index && (!m_list[index].ml || p_ptr->image)) index=0;
        choice->id=index?index:(!d?-1:0);
        if (index) monster_desc(name,&m_list[index],0);
        /* free targeting: any grid may be picked; a creature that cannot be a creature target is a plain point */
        if (!ctx->eight_way && index && !_gb_target_eligible(index,ctx)) choice->id=0;
        if (!d && ctx->eight_way==1) {valid=FALSE;reason=NATIVE_TARGET_SELF_FORBIDDEN;}
    }
    if (hb_target_preview_enabled()&&valid&&!profile->supported) reason=NATIVE_TARGET_PROFILE_MISSING;
    if(!d){int i;for(i=0;i<profile->count;++i)if(profile->effects[i].shape==NATIVE_SHAPE_SHOT){valid=FALSE;reason=NATIVE_TARGET_SELF_FORBIDDEN;}}
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
            int result=_gb_preview_effect(&profile->effects[i],choice);
            if(result<=0){if(result<0)valid=FALSE;reason=result<0?-result:NATIVE_TARGET_PROFILE_MISSING;break;}
        }
    }
    hb_target_status(valid,reason,d,name,stochastic,profile->effect_known);
    _gb_target_emit_extras(choice,ctx);
    _gb_target_rng_check("genso-preview-rng",&rng);
    hb_target_trace_invariant("genso-preview-range",original_length==project_length);
}
static int _gb_target_read(void *data) {(void)data;inkey_base=TRUE;return (unsigned char)inkey();}
static int _gb_target_direction(int key,void *data) {(void)data;return get_keymap_dir((char)key);}
static void _gb_target_refresh(void *data) {(void)data;Term_fresh();}
static int _gb_choose_target(int mode,int eight_way,int *direction,int *x,int *y)
{
    _gb_target_context ctx={mode,eight_way,x!=NULL,0};
    native_target_request request={0};native_target_choice choice={0};
    native_target_hooks hooks={_gb_target_candidates,_gb_target_preview,_gb_target_read,_gb_target_direction,_gb_target_refresh};
    const native_target_profile *profile=hb_target_profile();
    hooks.extra=_gb_target_extra;hooks.extra_screen=_gb_target_extra_screen;
    if(profile->count){
        int i;for(i=0;i<profile->count;++i){
            int range=profile->effects[i].range?profile->effects[i].range:(project_length?project_length:MAX_RANGE);
            if(range>ctx.range)ctx.range=range;
        }
    }
    request.width=cur_wid;request.height=cur_hgt;request.source_x=px;request.source_y=py;
    request.mode=eight_way?NATIVE_TARGET_DIRECTION8:NATIVE_TARGET_POINT;request.kind=profile->kind;request.label=profile->label;
    if (target_who>0 && _gb_target_eligible(target_who,&ctx)) {
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
