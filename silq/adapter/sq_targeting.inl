/* Native target selection adapter, included by the reproducible xtra2.c overlay. */
#include "sq_lang_c.h"
typedef struct _sq_target_context {int mode,range;} _sq_target_context;
typedef struct _sq_target_rng {bool quick;u32b value;u16b place;u32b state[RAND_DEG];} _sq_target_rng;
static _sq_target_rng _sq_target_rng_save(void)
{
    _sq_target_rng saved;saved.quick=Rand_quick;saved.value=Rand_value;saved.place=Rand_place;
    memcpy(saved.state,Rand_state,sizeof(saved.state));return saved;
}
static void _sq_target_rng_check(const char *name,const _sq_target_rng *saved)
{
    hb_target_trace_invariant(name,saved->quick==Rand_quick&&saved->value==Rand_value&&saved->place==Rand_place
        && memcmp(saved->state,Rand_state,sizeof(saved->state))==0);
}
extern int sq_target_mapped_direction(void);
extern int sq_target_project_path(u16b *path,int range,int y1,int x1,int *y2,int *x2,u32b flags);
extern int sq_target_grid_known(int y,int x);
extern int sq_target_grid_open(int y,int x);
extern int sq_target_visible_monster(int y,int x);
extern bool sq_target_los(int y1,int x1,int y2,int x2);
static int _sq_target_eight_direction(int target_x,int target_y)
{
    int dir,result=5;double best=-2.0,x=target_x-p_ptr->px,y=target_y-p_ptr->py;
    if(!x&&!y)return 5;
    for(dir=1;dir<=9;++dir)if(dir!=5){
        double score=(x*ddx[dir]+y*ddy[dir])/((ddx[dir]&&ddy[dir])?1.4142135623730951:1.0);
        if(score>best){best=score;result=dir;}
    }
    return result;
}
static int _sq_target_push(native_target_choice *choice,const native_target_effect *effect)
{
    int side,step;
    if(effect->shape!=NATIVE_SHAPE_PUSH||choice->direction<1||choice->direction>9)return FALSE;
    if(choice->direction==5)return TRUE;
    /* Horn of Force checks three straight grid rays, without a LOS test. */
    for(side=-1;side<=1;++side){
        int dir=cycle[chome[choice->direction]+side];
        hb_target_path_begin(NATIVE_PATH_REACHABLE);hb_target_path_cell(p_ptr->px,p_ptr->py);
        for(step=1;step<=3;++step){
            int x=p_ptr->px+step*ddx[dir],y=p_ptr->py+step*ddy[dir],index;
            if(!in_bounds_fully(y,x))continue;
            hb_target_path_cell(x,y);hb_target_area_cell(x,y,FALSE);
            index=sq_target_visible_monster(y,x);
            if(index>0)hb_target_affected(index,x,y,TRUE);
        }
    }
    return TRUE;
}
static int _sq_target_projection(native_target_choice *choice,const native_target_effect *effect)
{
    int sy=p_ptr->py,sx=p_ptr->px,tx=choice->x,ty=choice->y,y,x,grids=1;
    int radius=effect->radius,centerline=0;
    int arc=effect->shape==NATIVE_SHAPE_ARC,ball=effect->shape==NATIVE_SHAPE_BALL;
    int beam=effect->shape==NATIVE_SHAPE_BEAM,bolt=effect->shape==NATIVE_SHAPE_BOLT;
    if(!arc&&!ball&&!beam&&!bolt)return FALSE;
    if(arc&&(radius<1||radius>20||effect->arg1<=0||effect->arg1>=360))return FALSE;
    if(ball&&(radius<0||radius>9))return FALSE;
    if(choice->direction!=5){tx=sx+MAX_RANGE*ddx[choice->direction];ty=sy+MAX_RANGE*ddy[choice->direction];}
    if(arc)centerline=90-get_angle_to_grid[MAX(0,MIN(40,ty-sy+20))][MAX(0,MIN(40,tx-sx+20))];
    else {
        u16b path[512];int i,count,cx=sx,cy=sy;u32b flags=effect->flags;
        if(ball&&choice->direction!=5)flags|=PROJECT_STOP;
        count=sq_target_project_path(path,MAX_RANGE,sy,sx,&ty,&tx,flags);
        hb_target_path_begin(NATIVE_PATH_REACHABLE);hb_target_path_cell(sx,sy);
        for(i=0;i<count;++i){
            int nx=GRID_X(path[i]),ny=GRID_Y(path[i]),index;
            if(!in_bounds_fully(ny,nx)||(ball&&!sq_target_grid_open(ny,nx)))break;
            cx=nx;cy=ny;hb_target_path_cell(cx,cy);
            if(beam||(bolt&&i==count-1)){
                index=sq_target_visible_monster(cy,cx);
                if(index>0)hb_target_affected(index,cx,cy,TRUE);
            }
        }
        if(!ball)return TRUE;
        sx=cx;sy=cy;
    }
    /* project() always includes the origin, but the player controls this arc. */
    hb_target_area_cell(sx,sy,FALSE);
    if(ball){int index=sq_target_visible_monster(sy,sx);if(index>0)hb_target_affected(index,sx,sy,TRUE);}
    for(y=sy-radius;y<=sy+radius;++y)for(x=sx-radius;x<=sx+radius;++x){
        int angle,diff,index;
        if((x==sx&&y==sy)||!in_bounds_fully(y,x)||grids>=255)continue;
        if(!sq_target_grid_open(y,x)){
            int i,adjacent=FALSE;
            if(!(effect->flags&PROJECT_WALL)&&effect->element!=GF_KILL_WALL&&effect->element!=GF_KILL_DOOR&&effect->element!=GF_ACID)continue;
            for(i=0;i<8;++i){int yy=y+ddy_ddd[i],xx=x+ddx_ddd[i];
                if(in_bounds_fully(yy,xx)&&sq_target_los(sy,sx,yy,xx)){adjacent=TRUE;break;}}
            if(!adjacent)continue;
        }
        if(distance(sy,sx,y,x)>radius)continue;
        if(arc){
            angle=ABS(get_angle_to_grid[y-sy+20][x-sx+20]+centerline)%180;diff=ABS(90-angle);
            if(diff>=(effect->arg1+6)/4)continue;
        }
        if(!sq_target_los(sy,sx,y,x))continue;
        ++grids;hb_target_area_cell(x,y,FALSE);
        index=sq_target_visible_monster(y,x);
        if(index>0&&(effect->flags&PROJECT_KILL))hb_target_affected(index,x,y,TRUE);
    }
    return TRUE;
}
static int _sq_target_shot(native_target_choice *choice,const native_target_effect *effect,int *stochastic)
{
    u16b path[256];int count,i,possible=FALSE;
    int x=choice->x,y=choice->y;
    if ((effect->shape!=NATIVE_SHAPE_SHOT && effect->shape!=NATIVE_SHAPE_THROW) || effect->range<=0 || effect->range>MAX_RANGE) return FALSE;
    if (choice->direction!=5) {x=p_ptr->px+99*ddx[choice->direction];y=p_ptr->py+99*ddy[choice->direction];}
    count=sq_target_project_path(path,effect->range,p_ptr->py,p_ptr->px,&y,&x,effect->flags);
    hb_target_path_begin(NATIVE_PATH_REACHABLE);hb_target_path_cell(p_ptr->px,p_ptr->py);
    for (i=0;i<count;++i) {
        int index;
        y=GRID_Y(path[i]);x=GRID_X(path[i]);
        if (!sq_target_grid_open(y,x)) break; /* Arrows stop before wall squares. */
        hb_target_path_cell(x,y);
        if(effect->arg1)hb_target_area_cell(x,y,FALSE); /* Known Radiance bow lights the travelled grid. */
        index=sq_target_visible_monster(y,x);
        if (index>0 && !(r_info[mon_list[index].r_idx].flags1&RF1_PEACEFUL)) {
            hb_target_affected(index,x,y,TRUE);
            if (!possible && i+1<count) {
                possible=TRUE;*stochastic=TRUE;hb_target_path_begin(NATIVE_PATH_POSSIBLE);hb_target_path_cell(x,y);
            }
        }
    }
    return TRUE;
}
static int _sq_target_eligible(int index,_sq_target_context *ctx)
{
    if (index<=0||index>=mon_max||!target_able(index)) return FALSE;
    return ctx->range<=0 || distance(p_ptr->py,p_ptr->px,mon_list[index].fy,mon_list[index].fx)<=ctx->range;
}
static int _sq_target_location(int y,int x,_sq_target_context *ctx)
{
    if (!in_bounds_fully(y,x)) return FALSE;
    if (ctx->range>0 && distance(p_ptr->py,p_ptr->px,y,x)>ctx->range) return FALSE;
    return !sq_target_grid_known(y,x)||(cave_info[y][x]&(CAVE_FIRE|CAVE_WALL))!=0;
}
/* Free targeting may pick a grid beyond the range of the effect (original has no range limit on a chosen grid). */
static int _sq_target_location_any_range(int y,int x,_sq_target_context *ctx)
{
    _sq_target_context unlimited=*ctx;unlimited.range=0;return _sq_target_location(y,x,&unlimited);
}
static int _sq_target_eligible_any_range(int index,_sq_target_context *ctx)
{
    _sq_target_context unlimited=*ctx;unlimited.range=0;return _sq_target_eligible(index,&unlimited);
}
static void _sq_target_candidates(void *data)
{
    _sq_target_context *ctx=data;int index;
    _sq_target_rng saved=_sq_target_rng_save();
    for (index=mon_max-1;index>0;--index) {
        monster_type *mon=&mon_list[index];int gain,ticks;char name[160];
        if (!_sq_target_eligible(index,ctx)) continue;
        gain=extract_energy[mon->mspeed];
        ticks=gain<=0?1000000000:(mon->energy>=100?0:(100-mon->energy+gain-1)/gain);
        monster_desc(name,sizeof(name),mon,0);
        hb_target_candidate(index,mon->fx,mon->fy,distance(p_ptr->py,p_ptr->px,mon->fy,mon->fx),ticks,mon_max-index,name,TRUE);
    }
    _sq_target_rng_check("sil-candidates-rng",&saved);
}
/* Monster recall ('r') while choosing a target. The original target_set_interactive_aux() answers 'r'
 * on a monster by showing its recall (screen_roff()), except while raging; the native toolbar has no
 * recall button outside of the "look" mode, so a raw 'r' was ignored. It is offered as an "extra"
 * operation (key 'r', also reachable from the Actions list). The recall screen is the core's own
 * screen, so the targeting state is closed while it runs. */
static int _sq_target_recall_ready=FALSE;
static int _sq_target_recall_index(const native_target_choice *choice)
{
    int index;
    if (!choice||choice->id<=0||choice->id>=mon_max) return 0;
    index=choice->id;
    if (!mon_list[index].r_idx||!mon_list[index].ml||p_ptr->image||p_ptr->rage) return 0;
    return index;
}
static void _sq_target_emit_extras(const native_target_choice *choice,const _sq_target_context *ctx)
{
    /* Direction-only prompts have no creature cursor. */
    if (ctx->mode) {_sq_target_recall_ready=FALSE;return;}
    _sq_target_recall_ready=_sq_target_recall_index(choice)!=0;
    hb_target_extra('r',sq_tr("Display monster recall"),_sq_target_recall_ready);
}
static int _sq_target_extra_screen(int key)
{
    return key=='r'&&_sq_target_recall_ready;
}
static int _sq_target_extra(int key,native_target_choice *choice,void *data)
{
    int index;
    (void)data;
    if (key!='r') return NATIVE_EXTRA_UNKNOWN;
    index=_sq_target_recall_index(choice);
    if (!index) return NATIVE_EXTRA_NONE;
    monster_race_track(mon_list[index].r_idx);
    health_track(index);
    handle_stuff();
    inkey_base=FALSE;
    screen_save();
    screen_roff(mon_list[index].r_idx);
    Term_addstr(-1,TERM_WHITE,"  [r]");
    (void)inkey();
    screen_load();
    return NATIVE_EXTRA_NONE;
}
static void _sq_target_preview(native_target_choice *choice,void *data)
{
    _sq_target_context *ctx=data;const native_target_profile *profile=hb_target_profile();
    _sq_target_rng saved=_sq_target_rng_save();
    int d=distance(p_ptr->py,p_ptr->px,choice->y,choice->x),valid=TRUE,reason=NATIVE_TARGET_OK,index=0,stochastic=FALSE;
    char name[160]="";
    if (!in_bounds_fully(choice->y,choice->x)) {valid=FALSE;reason=NATIVE_TARGET_OUTSIDE;}
    else {
        index=sq_target_grid_known(choice->y,choice->x)?cave_m_idx[choice->y][choice->x]:0;
        if (index<0 || (index>0&&(!mon_list[index].ml||p_ptr->image))) index=0;
        choice->id=index?index:(!d?-1:0);
        if (index) monster_desc(name,sizeof(name),&mon_list[index],0);
        if (!ctx->mode && !_sq_target_location_any_range(choice->y,choice->x,ctx)) {valid=FALSE;reason=NATIVE_TARGET_UNAVAILABLE;}
        if (!ctx->mode&&index&&!_sq_target_eligible_any_range(index,ctx)) {valid=FALSE;reason=NATIVE_TARGET_UNAVAILABLE;}
        if (!ctx->mode&&!d) {valid=FALSE;reason=NATIVE_TARGET_SELF_FORBIDDEN;}
    }
    if (hb_target_preview_enabled()&&valid&&!profile->supported) reason=NATIVE_TARGET_PROFILE_MISSING;
    if(!d&&profile->count==1&&profile->effects[0].shape==NATIVE_SHAPE_PUSH){valid=FALSE;reason=NATIVE_TARGET_SELF_FORBIDDEN;}
    choice->direction=5;
    if (ctx->mode && d) {
        choice->direction=_sq_target_eight_direction(choice->x,choice->y);
    }
    if (hb_target_preview_enabled()&&valid&&profile->supported&&profile->effect_known) {
        int i;for (i=0;i<profile->count;++i) {
            const native_target_effect *effect=&profile->effects[i];
            int supported=effect->shape==NATIVE_SHAPE_PUSH?_sq_target_push(choice,effect):
                (effect->shape==NATIVE_SHAPE_SHOT||effect->shape==NATIVE_SHAPE_THROW)?_sq_target_shot(choice,effect,&stochastic):_sq_target_projection(choice,effect);
            if(!supported){reason=NATIVE_TARGET_PROFILE_MISSING;break;}
        }
    }
    hb_target_status(valid,reason,d,name,stochastic,profile->effect_known);
    _sq_target_emit_extras(choice,ctx);
    _sq_target_rng_check("sil-preview-rng",&saved);
}
static int _sq_target_read(void *data) {(void)data;inkey_base=TRUE;return (unsigned char)inkey();}
static int _sq_target_direction(int key,void *data) {(void)data;return target_dir((char)key);}
static void _sq_target_refresh(void *data) {(void)data;Term_fresh();}
static int _sq_choose_target(int mode,int range,int *direction)
{
    _sq_target_context ctx={mode,range};native_target_request request={0};native_target_choice choice={0};
    native_target_hooks hooks={_sq_target_candidates,_sq_target_preview,_sq_target_read,_sq_target_direction,_sq_target_refresh};
    const native_target_profile *profile=hb_target_profile();
    int force=profile->count==1&&profile->effects[0].shape==NATIVE_SHAPE_PUSH;
    hooks.extra=_sq_target_extra;hooks.extra_screen=_sq_target_extra_screen;
    if(force&&profile->effect_known){mode=1;ctx.mode=1;}
    request.width=p_ptr->cur_map_wid;request.height=p_ptr->cur_map_hgt;request.source_x=p_ptr->px;request.source_y=p_ptr->py;
    request.mode=mode?NATIVE_TARGET_DIRECTION8:NATIVE_TARGET_POINT;request.kind=profile->kind;request.label=profile->label;
    if (p_ptr->target_set) {
        if (p_ptr->target_who>0 && _sq_target_eligible(p_ptr->target_who,&ctx)) {
            request.previous_valid=TRUE;request.previous_id=p_ptr->target_who;
            request.previous_x=mon_list[p_ptr->target_who].fx;request.previous_y=mon_list[p_ptr->target_who].fy;
        } else if (!p_ptr->target_who && _sq_target_location(p_ptr->target_row,p_ptr->target_col,&ctx)) {
            request.previous_valid=TRUE;request.previous_x=p_ptr->target_col;request.previous_y=p_ptr->target_row;
        }
    }
    if (!hb_target_choose(&request,&hooks,&ctx,&choice)) return FALSE;
    /* Force indexes chome/cycle and therefore needs an actual eight-way
       direction, never target sentinel 5. Conceal subtype until confirmation
       for unidentified horns; their preview remains neutral and point-based. */
    if(force){mode=1;choice.direction=_sq_target_eight_direction(choice.x,choice.y);}
    *direction=choice.direction;
    if (!mode) {
        if (choice.track_creature&&choice.id>0) {target_set_monster(choice.id);health_track(choice.id);}
        else {target_set_location(choice.y,choice.x);health_track(0);}
    }
    return TRUE;
}
