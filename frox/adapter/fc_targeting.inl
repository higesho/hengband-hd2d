/* Included by the reproducible xtra2.c overlay. Access to static target helpers
 * keeps native eligibility and buff-target state in their original module.
 * Preview callbacks never call project(), consume RNG, or commit target state.
 */
typedef struct _fc_target_context {
    int mode, eight_way, range, point_only, repeat_input;
} _fc_target_context;
#include "bridge/targeting_scatter.h"
/* The native save stores target_who, but not a stationary target's coordinates.
 * Do not treat the zero-initialized pair after loading as a remembered point. */
static int _fc_target_confirmed_point = FALSE;
#define FC_PREVIEW_GUIDE 0x40000000 /* Private to the read-only extracted path. */
typedef struct _fc_target_rng {
    bool quick;u32b value;u16b place;u32b state[RAND_DEG];
} _fc_target_rng;
static _fc_target_rng _fc_target_rng_save(void)
{
    _fc_target_rng saved;
    saved.quick=Rand_quick;saved.value=Rand_value;saved.place=Rand_place;
    memcpy(saved.state,Rand_state,sizeof(saved.state));return saved;
}
static void _fc_target_rng_check(const char *name,const _fc_target_rng *saved)
{
    hb_target_trace_invariant(name,saved->quick==Rand_quick && saved->value==Rand_value
        && saved->place==Rand_place && memcmp(saved->state,Rand_state,sizeof(saved->state))==0);
}

extern int fc_target_mapped_direction(void);
static int _fc_target_movement_pending(void)
{
    const char *site=hb_target_profile()->site;
    return site && strncmp(site,"movement:",9)==0 && fc_target_mapped_direction()!=0;
}

static int _fc_preview_known(int y,int x)
{
    cave_type *cell;
    if (!in_bounds2(y,x)) return FALSE;
    cell=&cave[y][x];
    if (cell->info&CAVE_MARK) return TRUE;
    if (p_ptr->blind) return FALSE;
    if (cell->info&(CAVE_MARK|CAVE_LITE|CAVE_MNLT)) return TRUE;
    return (cell->info&CAVE_VIEW) && (((cell->info&(CAVE_GLOW|CAVE_MNDK))==CAVE_GLOW)||p_ptr->see_nocto);
}
static int _fc_preview_passable(int y,int x,int flags)
{
    feature_type *feature;
    if (!in_bounds2(y,x)) return FALSE;
    if (!_fc_preview_known(y,x)) return TRUE; /* Reviewed rule: unknown walls are absent in the preview. */
    feature=&f_info[get_feat_mimic(&cave[y][x])];
    if (flags&PROJECT_DISI) return have_flag(feature->flags,FF_PROJECT)
        || (have_flag(feature->flags,FF_HURT_DISI)&&!have_flag(feature->flags,FF_PERMANENT));
    return have_flag(feature->flags,(flags&PROJECT_LOS)?FF_LOS:FF_PROJECT);
}
static int _fc_preview_los_cell(int y,int x) {return _fc_preview_passable(y,x,PROJECT_LOS);}
static int _fc_preview_stop_disintegration(int y,int x) {return !_fc_preview_passable(y,x,PROJECT_DISI);}
static bool _fc_preview_path_continue(int n,int y,int x,int flags)
{
    int index;
    if (!n) return TRUE;
    if (flags&FC_PREVIEW_GUIDE) return TRUE;
    if (!_fc_preview_passable(y,x,flags)) return FALSE;
    if(!_fc_preview_known(y,x))return TRUE;
    index=cave[y][x].m_idx;
    if ((flags&PROJECT_STOP) && (player_bold(y,x)||(index&&m_list[index].ml&&!p_ptr->image))) return FALSE;
    return TRUE;
}
static void _fc_preview_impact(int y,int x)
{
    int index;
    if (!_fc_preview_known(y,x)) return;
    index=cave[y][x].m_idx;
    if (index&&m_list[index].ml&&!p_ptr->image) hb_target_affected(index,x,y,TRUE);
}
static int _fc_preview_area_reachable(int cy,int cx,int y,int x,int element)
{
    u16b path[512];int count;
    if (!in_bounds2(y,x)) return FALSE;
    if (element==GF_LITE||element==GF_LITE_WEAK) return _fc_preview_los(cy,cx,y,x);
    if (element==GF_DISINTEGRATE) return _fc_preview_disintegration_range(cy,cx,y,x);
    /* Native project() clears project_length before blast visibility checks. */
    count=_fc_preview_project_path_new(path,MAX_RANGE,cy,cx,y,x,0);
    return !count || (GRID_Y(path[count-1])==y&&GRID_X(path[count-1])==x);
}
static void _fc_preview_area(int y,int x,int possible,int flags)
{
    if (!in_bounds2(y,x)) return;
    hb_target_area_cell(x,y,possible);if(flags&PROJECT_KILL&&_fc_preview_known(y,x))_fc_preview_impact(y,x);
}
static void _fc_preview_guide(int y,int x,int target_y,int target_x)
{
    u16b path[512];int i,count;
    count=_fc_preview_project_path_new(path,500,y,x,target_y,target_x,FC_PREVIEW_GUIDE);
    if (!count) return;
    hb_target_path_begin(NATIVE_PATH_GUIDE);hb_target_path_cell(x,y);
    for (i=0;i<count;++i) {
        int ny=GRID_Y(path[i]),nx=GRID_X(path[i]);
        hb_target_path_cell(nx,ny);
    }
}
static void _fc_preview_breath(u16b *path,int count,int cy,int cx,const native_target_effect *effect)
{
    int by=py,bx=px,brad=0,bdis=0,path_n=0;
    int rad=ABS(effect->radius),brev=rad*rad/count;
    int mdis=distance(py,px,cy,cx)+rad;
    while (bdis<=mdis) {
        int cdis,y,x;
        if (path_n<count) {
            int ny=GRID_Y(path[path_n]),nx=GRID_X(path[path_n]);
            if (bdis>=distance(ny,nx,py,px)) {by=ny;bx=nx;++path_n;}
        }
        for (cdis=0;cdis<=brad;++cdis) for (y=by-cdis;y<=by+cdis;++y) for (x=bx-cdis;x<=bx+cdis;++x) {
            if (!in_bounds(y,x)||distance(py,px,y,x)!=bdis||distance(by,bx,y,x)!=cdis) continue;
            if (_fc_preview_area_reachable(by,bx,y,x,effect->element)) _fc_preview_area(y,x,effect->possible,effect->flags);
        }
        brad=rad*(path_n+brev)/(count+brev);++bdis;
    }
}
static void _fc_preview_shot_blast(int cy,int cx,int radius)
{
    int r,y,x;
    for(r=0;r<=radius;++r)for(y=cy-r;y<=cy+r;++y)for(x=cx-r;x<=cx+r;++x)
        if(in_bounds2(y,x)&&distance(cy,cx,y,x)==r&&_fc_preview_area_reachable(cy,cx,y,x,GF_MISSILE))_fc_preview_area(y,x,TRUE,PROJECT_KILL);
}
static int _fc_preview_shot(const native_target_effect *effect,native_target_choice *choice)
{
    u16b path[512];int count,i,y=py,x=px,ty=choice->y,tx=choice->x;
    int mode=effect->arg1,toggle=effect->arg2;
    int piercing=mode==SHOOT_PIERCE||toggle==TOGGLE_PIERCING_ARROW;
    int pierce=mode==SP_PIERCE?p_ptr->concent:(piercing?5:0),possible=FALSE;
    int radius=effect->radius;
    if(effect->range<=0||effect->range>500)return FALSE;
    if(choice->direction!=5){ty=py+99*ddy[choice->direction];tx=px+99*ddx[choice->direction];}
    count=_fc_preview_project_path_new(path,effect->range,py,px,ty,tx,PROJECT_PATH|PROJECT_THRU|(mode==SHOOT_DISINTEGRATE?PROJECT_DISI:0));
    hb_target_path_begin(NATIVE_PATH_REACHABLE);hb_target_path_cell(px,py);
    for(i=0;i<count&&(piercing||y!=ty||x!=tx);++i){
        int ny=GRID_Y(path[i]),nx=GRID_X(path[i]),index,known=_fc_preview_known(ny,nx);
        feature_type *feature=known?&f_info[get_feat_mimic(&cave[ny][nx])]:NULL;
        index=known?cave[ny][nx].m_idx:0;if(index&&(!m_list[index].ml||p_ptr->image))index=0;
        if(mode==SP_KILL_WALL&&feature&&have_flag(feature->flags,FF_HURT_ROCK)&&!index){
            hb_target_path_cell(nx,ny);hb_target_area_cell(nx,ny,possible);y=ny;x=nx;break;
        }
        if(feature&&!have_flag(feature->flags,FF_PROJECT)&&mode!=SHOOT_DISINTEGRATE&&!index)break;
        y=ny;x=nx;hb_target_path_cell(x,y);
        if(mode==SP_LITE||mode==SP_KILL_TRAP||mode==SP_EVILNESS)hb_target_area_cell(x,y,possible);
        if(!index||((mode==SHOOT_ALL||mode==SHOOT_VOLLEY)&&(x!=tx||y!=ty)))continue;
        if(!melee_challenge&&(!(effect->flags&NATIVE_SHOT_AVOID_BIRDS)||r_info[m_list[index].ap_r_idx].d_char!='B')){
            hb_target_affected(index,x,y,TRUE);
            if(radius>0)_fc_preview_shot_blast(y,x,radius);
            if(mode==SP_HOLYNESS)hb_target_area_cell(x,y,TRUE);
        }
        if(pierce>0){
            --pierce;
            if(piercing){
                if(!have_flag(feature->flags,FF_PROJECT))break;
                possible=TRUE;hb_target_path_begin(NATIVE_PATH_POSSIBLE);hb_target_path_cell(x,y);
            }
            continue;
        }
        break;
    }
    if(effect->flags&NATIVE_SHOT_EXPLODE_ON_MISS)_fc_preview_shot_blast(y,x,radius);
    if((y!=ty||x!=tx)&&!piercing)_fc_preview_guide(y,x,ty,tx);
    return TRUE;
}
static int _fc_preview_effect(const native_target_effect *effect,native_target_choice *choice);
typedef struct _fc_scatter_context {native_target_effect effect;} _fc_scatter_context;
static int _fc_scatter_ray(int y,int x,void *data)
{
    _fc_scatter_context *context=data;native_target_choice choice={0};
    choice.x=x;choice.y=y;choice.direction=5;
    return _fc_preview_effect(&context->effect,&choice);
}
static int _fc_room_pass(int y,int x,int lite){return _fc_preview_passable(y,x,lite?PROJECT_LOS:0);}
static int _fc_room_open(int cy,int cx,int lite)
{
    int i,len=0,best=0;
    for(i=0;i<16;++i){int d=cdd[i%8];if(_fc_room_pass(cy+ddy[d],cx+ddx[d],lite))++len;else{best=MAX(best,len);len=0;}}
    return MAX(best,len);
}
static int _fc_room_area(const native_target_effect *effect)
{
    byte seen[MAX_HGT][MAX_WID]={{0}};s16b qy[MAX_HGT*MAX_WID],qx[MAX_HGT*MAX_WID];int head=0,tail=0;
    int lite=effect->arg1!=0,limit=MAX_RANGE*2;
    const int directions[]={2,8,6,4,3,7,9,1};
    if(_fc_preview_known(py,px)&&!(cave[py][px].info&CAVE_ROOM)){
        int walls=0,j;if(!lite)return TRUE;
        for(j=0;j<8;++j)if(!_fc_room_pass(py+ddy_ddd[j],px+ddx_ddd[j],lite))++walls;
        if(_fc_room_pass(py,px,lite)&&walls==6&&_fc_room_open(py,px,lite)<=1)return TRUE;
    }
    qy[tail]=py;qx[tail++]=px;seen[py][px]=TRUE;
    while(head<tail){
        int y=qy[head],x=qx[head],known=_fc_preview_known(y,x),i;++head;
        int room=known&&(cave[y][x].info&CAVE_ROOM),affected=lite||!known||!is_mirror_grid(&cave[y][x]);
        if(!lite&&(dun_level||!is_daytime()))for(i=0;i<9;++i){int ny=y+ddy_ddd[i],nx=x+ddx_ddd[i];if(_fc_preview_known(ny,nx)&&have_flag(f_info[get_feat_mimic(&cave[ny][nx])].flags,FF_GLOW))affected=FALSE;}
        if(affected)hb_target_area_cell(x,y,!known);
        if(!_fc_room_pass(y,x,lite))continue;
        for(i=0;i<8;++i){int ny=y+ddy[directions[i]],nx=x+ddx[directions[i]],walls=0,j,accept;
            if(!in_bounds2(ny,nx)||seen[ny][nx])continue;known=_fc_preview_known(ny,nx);room=known&&(cave[ny][nx].info&CAVE_ROOM);
            accept=room||(!known&&distance(py,px,ny,nx)<=limit);
            if(known&&!room&&lite&&distance(py,px,ny,nx)<=limit){for(j=0;j<8;++j)if(!_fc_room_pass(ny+ddy_ddd[j],nx+ddx_ddd[j],lite))++walls;accept=!(in_bounds(ny,nx)&&_fc_room_pass(ny,nx,lite)&&walls==6&&_fc_room_open(ny,nx,lite)<=1);}
            if(!accept||tail==TEMP_MAX)continue;seen[ny][nx]=TRUE;qy[tail]=ny;qx[tail++]=nx;
        }
    }
    return TRUE;
}
static void _fc_faded_turn(int y,int x)
{
    int direction;
    for(direction=1;direction<=9;++direction)if(direction!=5){
        int ny=y+ddy[direction],nx=x+ddx[direction];
        if(!_fc_preview_passable(ny,nx,0))continue;
        hb_target_path_begin(NATIVE_PATH_GUIDE);hb_target_path_cell(x,y);hb_target_path_cell(nx,ny);
    }
}
static int _fc_preview_effect(const native_target_effect *effect,native_target_choice *choice)
{
    if(effect->arg2==-1&&(d_info[dungeon_type].flags1&DF1_DARKNESS))return TRUE;
    if(effect->shape==NATIVE_SHAPE_ROOM_AREA)return _fc_room_area(effect);
    if(effect->shape==NATIVE_SHAPE_MELEE){
        int index,side;
        for(index=0;index<8&&cdd[index]!=choice->direction;++index){}
        if(index==8)return -NATIVE_TARGET_SELF_FORBIDDEN;
        for(side=-effect->arg1;side<=effect->arg1;++side){
            int direction=(index+side+8)%8,y=py+ddy_cdd[direction],x=px+ddx_cdd[direction];
        if(!in_bounds(y,x))continue;
            hb_target_path_begin(NATIVE_PATH_REACHABLE);hb_target_path_cell(px,py);hb_target_path_cell(x,y);
            _fc_preview_area(y,x,effect->possible,PROJECT_KILL);
        }
        return TRUE;
    }
    if(effect->shape==NATIVE_SHAPE_QUAKE||effect->shape==NATIVE_SHAPE_DESTRUCTION){
        int quake=effect->shape==NATIVE_SHAPE_QUAKE;
        int radius=quake?MIN(effect->radius,12):effect->radius,y,x;
        int tree=d_info[dungeon_type].wild_type==TERRAIN_TREES;
        if(!py_in_dungeon()||(!quake&&!quests_allow_all_spells()))return TRUE;
        if(radius<0||radius>100)return FALSE;
        for(y=choice->y-radius;y<=choice->y+radius;++y)for(x=choice->x-radius;x<=choice->x+radius;++x){
            int known=_fc_preview_known(y,x);
            if(!in_bounds(y,x)||distance(choice->y,choice->x,y,x)>radius)continue;
            if(quake&&((y==choice->y&&x==choice->x)||(known&&tree&&have_flag(f_info[get_feat_mimic(&cave[y][x])].flags,FF_WATER))))continue;
            hb_target_area_cell(x,y,quake||effect->possible);
            if(y!=choice->y||x!=choice->x)_fc_preview_impact(y,x);
        }
        return TRUE;
    }
    if(effect->shape==NATIVE_SHAPE_LOS){
        int y,x;
        for(y=0;y<cur_hgt;++y)for(x=0;x<cur_wid;++x)
            if(_fc_preview_los(py,px,y,x)&&_fc_preview_area_reachable(py,px,y,x,GF_MISSILE))
                _fc_preview_area(y,x,effect->possible,effect->flags);
        return TRUE;
    }
    u16b path[512];
    int i,count,flags=effect->flags;
    int mirror_ray=effect->shape==NATIVE_SHAPE_MIRROR_RAY;
    int beam=mirror_ray||effect->shape==NATIVE_SHAPE_BEAM||(flags&PROJECT_BEAM);
    int range=effect->range?effect->range:(project_length?project_length:MAX_RANGE);
    int target_y=choice->y,target_x=choice->x,cy=py,cx=px,passed_selected=FALSE;
    int raw=effect->shape==NATIVE_SHAPE_PROJECT;
    int radial=effect->shape==NATIVE_SHAPE_BALL||effect->shape==NATIVE_SHAPE_BREATH||effect->shape==NATIVE_SHAPE_ROCKET||raw;
    int radius=ABS(effect->radius);
    if(raw&&effect->radius<0)return FALSE;
    if(effect->shape==NATIVE_SHAPE_SHOT)return _fc_preview_shot(effect,choice);
    if(effect->shape==NATIVE_SHAPE_SELF_SCATTER){
        native_target_effect beam=*effect;int y,x;
        if(effect->radius<1||effect->radius>16)return FALSE;
        beam.shape=NATIVE_SHAPE_BEAM;beam.radius=0;beam.possible=TRUE;
        for(y=py-effect->radius;y<=py+effect->radius;++y)for(x=px-effect->radius;x<=px+effect->radius;++x){
            native_target_choice ray={x,y,0,5,0,0};
            if(!in_bounds(y,x)||distance(py,px,y,x)>effect->radius)continue;
            if(!(effect->arg1?_fc_preview_los(py,px,y,x):_fc_preview_area_reachable(py,px,y,x,GF_MISSILE)))continue;
            if(!_fc_preview_effect(&beam,&ray))return FALSE;
        }
        return TRUE;
    }
    if(effect->shape==NATIVE_SHAPE_BLAST){
        _fc_scatter_context context;context.effect=*effect;
        if(effect->arg2<=0)return TRUE;
        context.effect.shape=(flags&PROJECT_BEAM)?NATIVE_SHAPE_BEAM:NATIVE_SHAPE_BOLT;
        context.effect.possible=effect->possible||effect->arg1>0;
        context.effect.arg2=1; /* Internal child: show the possible path area, no desired-point guide. */
        return hb_target_scatter_points(py,px,choice->y,choice->x,choice->direction,effect->arg1,distance,_fc_scatter_ray,&context);
    }
    if (!beam && effect->shape!=NATIVE_SHAPE_BOLT && effect->shape!=NATIVE_SHAPE_BEAM
        && effect->shape!=NATIVE_SHAPE_SHOT && effect->shape!=NATIVE_SHAPE_THROW && !radial) return FALSE;
    if (range<=0||range>500||radius>31) return FALSE;
    if (beam||effect->shape==NATIVE_SHAPE_BREATH) {
        if (effect->element==GF_LITE||effect->element==GF_LITE_WEAK||effect->element==GF_DARK) flags|=PROJECT_LOS;
        if (effect->element==GF_DISINTEGRATE) flags|=PROJECT_DISI;
    }
    if (effect->element==GF_SOUND&&p_ptr->no_air) return TRUE;
    if (radial && !raw && effect->shape!=NATIVE_SHAPE_ROCKET && (choice->direction==5||effect->shape==NATIVE_SHAPE_BREATH)) flags&=~PROJECT_STOP;
    if (choice->direction!=5) {int scale=raw?effect->arg1:(radial?99:1);target_y=py+scale*ddy[choice->direction];target_x=px+scale*ddx[choice->direction];}
    if(target_x==px&&target_y==py)flags&=~PROJECT_THRU;
    if(raw&&(flags&PROJECT_JUMP)){
        int r,y,x;
        if(!in_bounds(target_y,target_x))return -NATIVE_TARGET_UNAVAILABLE;
        for(r=0;r<=radius;++r)for(y=target_y-r;y<=target_y+r;++y)for(x=target_x-r;x<=target_x+r;++x)
            if(in_bounds2(y,x)&&distance(target_y,target_x,y,x)==r&&_fc_preview_area_reachable(target_y,target_x,y,x,effect->element))
                _fc_preview_area(y,x,effect->possible,flags);
        return TRUE;
    }
    count=_fc_preview_project_path_new(path,range,py,px,target_y,target_x,flags);
    hb_target_path_begin(effect->possible?NATIVE_PATH_POSSIBLE:NATIVE_PATH_REACHABLE);
    hb_target_path_cell(px,py);
    if (beam) hb_target_area_cell(px,py,effect->possible);
    for (i=0;i<count;++i) {
        int y=GRID_Y(path[i]),x=GRID_X(path[i]);
        if (radius && !_fc_preview_passable(y,x,flags)) break;
        cy=y;cx=x;
        if (y==choice->y && x==choice->x) passed_selected=TRUE;
        hb_target_path_cell(x,y);
        if(effect->arg2)hb_target_area_cell(x,y,effect->possible);
        if (beam) hb_target_area_cell(x,y,effect->possible);
        if (beam && (flags&PROJECT_KILL)) _fc_preview_impact(y,x);
        if(!beam&&(flags&PROJECT_REFLECTABLE)&&_fc_preview_known(y,x)&&cave[y][x].m_idx){
            monster_type *monster=&m_list[cave[y][x].m_idx];
            if(monster->ml&&(r_info[monster->r_idx].r_flags2&RF2_REFLECTING))_fc_faded_turn(y,x);
        }
        if(mirror_ray&&_fc_preview_known(y,x)&&is_mirror_grid(&cave[y][x])){_fc_faded_turn(y,x);++i;break;}
    }
    count=i;
    if (radial && count<=MAX_RANGE) {
        if (effect->shape==NATIVE_SHAPE_BREATH && count) _fc_preview_breath(path,count,cy,cx,effect);
        else {
            int r,y,x;
            for (r=0;r<=radius;++r) for (y=cy-r;y<=cy+r;++y) for (x=cx-r;x<=cx+r;++x)
                if (in_bounds2(y,x)&&distance(cy,cx,y,x)==r&&_fc_preview_area_reachable(cy,cx,y,x,effect->element))
                    _fc_preview_area(y,x,effect->possible,flags);
        }
    } else if (!radial && count && !beam && (flags&PROJECT_KILL)) _fc_preview_impact(cy,cx);
    if (!effect->arg2 && choice->direction==5 && !passed_selected && count) _fc_preview_guide(cy,cx,choice->y,choice->x);
    return TRUE;
}
static int _fc_preview_direction_random(_fc_target_context *ctx)
{
    if (ctx->point_only) return FALSE;
    if (p_ptr->confused) return TRUE;
    if (ctx->repeat_input!=1) return FALSE;
    if (p_ptr->riding) {
        monster_type *mon=&m_list[p_ptr->riding];monster_race *race=&r_info[mon->r_idx];
        return MON_CONFUSED(mon) || (p_ptr->riding_ryoute && (race->flags1&(RF1_RAND_25|RF1_RAND_50)));
    }
    return p_ptr->move_random && !p_ptr->wild_mode;
}

static int _fc_target_eligible(int index, _fc_target_context *ctx)
{
    monster_type *mon;
    if (index <= 0 || index >= max_m_idx) return FALSE;
    mon = &m_list[index];
    if (!target_able_aux(index, ctx->mode)) return FALSE;
    if ((ctx->mode & TARGET_BUFF) && !_buff_cycle_candidate(mon)) return FALSE;
    if ((ctx->mode & TARGET_CAPTURE) && !is_pet(mon)) return FALSE;
    if (ctx->range > 0 && distance(py, px, mon->fy, mon->fx) > ctx->range) return FALSE;
    return TRUE;
}
/* Free targeting may pick a grid beyond the range of the effect (original 't' has no range limit). */
static int _fc_target_eligible_any_range(int index, _fc_target_context *ctx)
{
    _fc_target_context unlimited = *ctx;
    unlimited.range = 0;
    return _fc_target_eligible(index, &unlimited);
}
static int _fc_target_ticks(monster_type *mon)
{
    int speed, gain;
    if (mon->id == p_ptr->riding) speed = p_ptr->pspeed;
    else {
        speed = mon->mspeed;
        if (ironman_nightmare) speed += 5;
        if (MON_FAST(mon)) speed += 10;
        speed -= monster_slow(mon);
        if (p_ptr->filibuster) speed -= SPEED_ADJ_FILIBUSTER;
    }
    gain = SPEED_TO_ENERGY(speed);
    return mon->energy_need <= 0 ? 0 : (mon->energy_need + gain - 1) / gain;
}
static void _fc_target_candidates(void *data)
{
    _fc_target_context *ctx = data;
    _fc_target_rng saved=_fc_target_rng_save();
    int i;
    for (i = max_m_idx - 1; i > 0; --i) {
        monster_type *mon = &m_list[i];
        char name[MAX_NLEN];
        if (!_fc_target_eligible(i, ctx)) continue;
        if (!_auto_target_monster_okay(i, ctx->mode)) continue;
        monster_desc(name, mon, 0);
        hb_target_candidate(i, mon->fx, mon->fy, distance(py, px, mon->fy, mon->fx),
            _fc_target_ticks(mon), max_m_idx - i, name, TRUE);
    }
    _fc_target_rng_check("frox-candidates-rng",&saved);
}
/* Monster recall ('r') while choosing a target. The original target_set_aux() answers 'r' on a
 * monster by showing its recall (mon_display_instance_target()) and then keeps the cursor where it
 * was; the native toolbar has no recall button outside of the "look" mode, so a raw 'r' was ignored.
 * It is offered as an "extra" operation (key 'r', also reachable from the Actions list). The recall
 * screen is the core's own screen, so the targeting state is closed while it runs. */
static int _fc_target_recall_ready = FALSE;
static int _fc_target_recall_index(const native_target_choice *choice)
{
    int index;
    if (!choice || choice->id <= 0 || choice->id >= m_max) return 0;
    index = choice->id;
    if (!m_list[index].r_idx || !m_list[index].ml || p_ptr->image) return 0;
    return index;
}
static void _fc_target_emit_extras(const native_target_choice *choice, const _fc_target_context *ctx)
{
    /* Direction-only prompts (tunnel, open ...) have no creature cursor. */
    if (ctx->eight_way) { _fc_target_recall_ready = FALSE; return; }
    _fc_target_recall_ready = _fc_target_recall_index(choice) != 0;
    hb_target_extra('r', fc_tr("Monster Recall"), _fc_target_recall_ready);
}
static int _fc_target_extra_screen(int key)
{
    return key == 'r' && _fc_target_recall_ready;
}
static int _fc_target_extra(int key, native_target_choice *choice, void *data)
{
    int index;
    (void)data;
    if (key != 'r') return NATIVE_EXTRA_UNKNOWN;
    index = _fc_target_recall_index(choice);
    if (!index) return NATIVE_EXTRA_NONE;
    mon_track(&m_list[index]);
    health_track(index);
    handle_stuff();
    inkey_base = FALSE;
    mon_display_instance(&m_list[index]);
    return NATIVE_EXTRA_NONE;
}
static void _fc_target_preview(native_target_choice *choice, void *data)
{
    _fc_target_context *ctx = data;
    _fc_target_rng saved=_fc_target_rng_save();
    const native_target_profile *profile = hb_target_profile();
    int valid = TRUE, reason = NATIVE_TARGET_OK;
    int index = 0, d = distance(py, px, choice->y, choice->x);
    char name[MAX_NLEN] = "";
    if (!in_bounds2(choice->y, choice->x)) { valid = FALSE; reason = NATIVE_TARGET_OUTSIDE; }
    else {
        index = cave[choice->y][choice->x].m_idx;
        if (index && (!m_list[index].ml || p_ptr->image)) index = 0;
        /* Display a visible body at a fixed point without converting that
         * stored point into a creature-following target. */
        choice->id = index ? index : (!d ? -1 : 0);
        if (index) monster_desc(name, &m_list[index], 0);
        if (_target_mode_needs_monster(ctx->mode) && !_fc_target_eligible_any_range(index, ctx)) {
            valid = FALSE; reason = NATIVE_TARGET_CREATURE_REQUIRED;
        }
        if (!ctx->eight_way && index && !_fc_target_eligible_any_range(index, ctx)) { valid = FALSE; reason = NATIVE_TARGET_UNAVAILABLE; }
        if (!d && ctx->eight_way == 1) { valid = FALSE; reason = NATIVE_TARGET_SELF_FORBIDDEN; }
    }
    /* Missing coverage is reported without replacing the native target rules. */
    if (hb_target_preview_enabled()&&valid&&!profile->supported) reason = NATIVE_TARGET_PROFILE_MISSING;
    if(!d){int i;for(i=0;i<profile->count;++i)if(profile->effects[i].shape==NATIVE_SHAPE_SHOT){valid=FALSE;reason=NATIVE_TARGET_SELF_FORBIDDEN;}}
    choice->direction = 5;
    if (ctx->eight_way && d) {
        double best = -2.0;
        int direction;
        const double x = choice->x - px, y = choice->y - py;
        for (direction = 1; direction <= 9; ++direction) {
            double score;
            if (direction == 5) continue;
            score = (x * ddx[direction] + y * ddy[direction]) / ((ddx[direction] && ddy[direction]) ? 1.4142135623730951 : 1.0);
            if (score > best) { best = score; choice->direction = direction; }
        }
    }
    if (hb_target_preview_enabled()&&valid&&profile->supported&&profile->effect_known && in_bounds2(choice->y,choice->x)) {
        int i;
        for (i=0;i<profile->count;++i) {
            if (_fc_preview_direction_random(ctx)) {
                int dir;
                native_target_effect possible=profile->effects[i];possible.possible=TRUE;
                for (dir=1;dir<=9;++dir) if (dir!=5) {
                    native_target_choice random_choice=*choice;random_choice.direction=dir;
                    if (!_fc_preview_effect(&possible,&random_choice)) {reason=NATIVE_TARGET_PROFILE_MISSING;break;}
                }
            } else if (!_fc_preview_effect(&profile->effects[i],choice)) {
                reason=NATIVE_TARGET_PROFILE_MISSING;break;
            }
        }
    }
    {
        int i,stochastic=profile->count && _fc_preview_direction_random(ctx);
        for (i=0;i<profile->count;++i) if (profile->effects[i].possible) stochastic=1;
        hb_target_status(valid, reason, d, name, stochastic, profile->effect_known);
    }
    _fc_target_emit_extras(choice, ctx);
    _fc_target_rng_check("frox-preview-rng",&saved);
}
static int _fc_target_read(void *unused)
{
    (void)unused;
    inkey_base = TRUE;
    return (unsigned char)inkey();
}
static int _fc_target_direction(int key, void *unused)
{
    (void)unused;
    return get_keymap_dir((char)key, FALSE);
}
static void _fc_target_refresh(void *unused)
{
    (void)unused;
    Term_fresh();
}
static int _fc_choose_target(int mode, int eight_way, int range, int *direction, int *x, int *y)
{
    _fc_target_context ctx = {mode, eight_way, range, x != NULL, eight_way != 0};
    native_target_request request = {0};
    native_target_choice choice = {0};
    native_target_hooks hooks = {_fc_target_candidates, _fc_target_preview, _fc_target_read, _fc_target_direction, _fc_target_refresh};
    const native_target_profile *profile = hb_target_profile();
    int previous = (mode & TARGET_BUFF) ? _buff_target_m_idx : target_who;
    hooks.extra = _fc_target_extra; hooks.extra_screen = _fc_target_extra_screen;
    if (eight_way==3) {ctx.eight_way=eight_way=1;ctx.repeat_input=2;}
    if (profile->site && strncmp(profile->site,"movement:",9)==0) ctx.eight_way=eight_way=1;
    if (ctx.range<=0 && profile->count) {
        int i;
        for (i=0;i<profile->count;++i) {
            int effect_range=profile->effects[i].range;
            if (!effect_range) effect_range=project_length?project_length:MAX_RANGE;
            if (effect_range>ctx.range) ctx.range=effect_range;
        }
    }
    request.width = cur_wid; request.height = cur_hgt;
    request.source_x = px; request.source_y = py;
    request.mode = eight_way ? NATIVE_TARGET_DIRECTION8 : (_target_mode_needs_monster(mode) ? NATIVE_TARGET_CREATURE : NATIVE_TARGET_POINT);
    request.kind = !profile->effect_known?NATIVE_TARGET_NEUTRAL:((mode & TARGET_BUFF) ? NATIVE_TARGET_SUPPORT : profile->kind);
    request.label = profile->label;
    if (previous > 0 && _fc_target_eligible(previous, &ctx)) {
        request.previous_valid = TRUE; request.previous_id = previous;
        request.previous_x = m_list[previous].fx; request.previous_y = m_list[previous].fy;
    } else if (previous < 0 && (target_row||target_col||_fc_target_confirmed_point)
        && !_target_mode_needs_monster(mode) && in_bounds2(target_row, target_col)
        && (!ctx.range || distance(py,px,target_row,target_col)<=ctx.range)) {
        request.previous_valid = TRUE; request.previous_x = target_col; request.previous_y = target_row;
    }
    if (!hb_target_choose(&request, &hooks, &ctx, &choice)) return FALSE;
    *direction = choice.direction;
    if (x && y) { *x = choice.x; *y = choice.y; }
    else if (!eight_way) {
        target_who = choice.track_creature && choice.id > 0 ? choice.id : -1;
        target_grab(choice.y, choice.x);
        _fc_target_confirmed_point = TRUE;
        if ((mode & TARGET_BUFF) && choice.id > 0) {
            _set_buff_target(&m_list[choice.id]);
            _set_buff_target_override(choice.y, choice.x);
        }
        p_ptr->redraw |= PR_HEALTH_BARS;
    }
    return TRUE;
}
