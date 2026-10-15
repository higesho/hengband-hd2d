/* Native geometry functions are supplied by the adjacent generated include. */
#include "bridge/targeting_scatter.h"
static sint _gb_preview_project_path(u16b*,int,int,int,int,int,int);
static bool _gb_preview_los(int,int,int,int);
static bool _gb_preview_disintegration_range(int,int,int,int);
static void _gb_preview_breath_shape(u16b*,int,int*,byte*,byte*,byte*,int*,int,int,int,int,int,int,bool);
static int _gb_preview_known(int y,int x)
{
    cave_type *cell;
    if(!in_bounds2(y,x))return FALSE;
    cell=&cave[y][x];
    return (cell->info&CAVE_MARK)||player_can_see_bold(y,x);
}
static int _gb_preview_monster(int y,int x)
{
    int index;
    if(!_gb_preview_known(y,x)||p_ptr->image)return 0;
    index=cave[y][x].m_idx;
    return index>0&&index<m_max&&m_list[index].ml?index:0;
}
static int _gb_preview_passable(int y,int x,int flags)
{
    feature_type *feature;
    if(!in_bounds2(y,x))return FALSE;
    if(!_gb_preview_known(y,x))return TRUE; /* Reviewed rule: unknown walls are absent in the preview. */
    feature=&f_info[get_feat_mimic(&cave[y][x])];
    if(flags&PROJECT_DISI)return have_flag(feature->flags,FF_PROJECT)
        ||(have_flag(feature->flags,FF_HURT_DISI)&&!have_flag(feature->flags,FF_PERMANENT));
    return have_flag(feature->flags,(flags&PROJECT_LOS)?FF_LOS:FF_PROJECT);
}
static int _gb_preview_project_cell(int y,int x){return _gb_preview_passable(y,x,0);}
static int _gb_preview_los_cell(int y,int x){return _gb_preview_passable(y,x,PROJECT_LOS);}
static int _gb_preview_stop_disintegration(int y,int x){return !_gb_preview_passable(y,x,PROJECT_DISI);}
static int _gb_preview_projectable(int sy,int sx,int y,int x)
{
    u16b path[512];int count=_gb_preview_project_path(path,MAX_RANGE,sy,sx,y,x,0);
    return !count||(GRID_Y(path[count-1])==y&&GRID_X(path[count-1])==x);
}
static int _gb_preview_area_reachable(int sy,int sx,int y,int x,int element)
{
    if(!in_bounds2(y,x))return FALSE;
    if(element==GF_LITE||element==GF_LITE_WEAK)return _gb_preview_los(sy,sx,y,x);
    if(element==GF_DISINTEGRATE)return _gb_preview_disintegration_range(sy,sx,y,x);
    return _gb_preview_projectable(sy,sx,y,x);
}
static void _gb_preview_impact(int y,int x,int flags)
{
    int index=_gb_preview_monster(y,x);
    if(index&&(flags&PROJECT_KILL))hb_target_affected(index,x,y,TRUE);
}
static int _gb_preview_shot(const native_target_effect *effect,native_target_choice *choice)
{
    u16b path[512];int count,i,y=py,x=px,ty=choice->y,tx=choice->x;
    int mode=effect->arg1,pierce=(mode==SP_PIERCE&&p_ptr->pclass==CLASS_SNIPER)?effect->arg2:0;
    if(effect->range<=0||effect->range>500)return FALSE;
    if(choice->direction!=5){ty=py+99*ddy[choice->direction];tx=px+99*ddx[choice->direction];}
    count=_gb_preview_project_path(path,effect->range,py,px,ty,tx,PROJECT_PATH|PROJECT_THRU);
    hb_target_path_begin(NATIVE_PATH_REACHABLE);hb_target_path_cell(px,py);
    for(i=0;i<count&&(y!=ty||x!=tx);++i){
        int ny=y,nx=x,index,known;feature_type *feature;
        mmove2(&ny,&nx,py,px,ty,tx);
        if(!in_bounds2(ny,nx))break;
        known=_gb_preview_known(ny,nx);index=_gb_preview_monster(ny,nx);feature=known?&f_info[get_feat_mimic(&cave[ny][nx])]:NULL;
        if(mode==SP_KILL_WALL&&feature&&have_flag(feature->flags,FF_HURT_ROCK)&&!index){
            hb_target_path_cell(nx,ny);hb_target_area_cell(nx,ny,FALSE);y=ny;x=nx;break;
        }
        if(feature&&!have_flag(feature->flags,FF_PROJECT)&&!index)break;
        y=ny;x=nx;hb_target_path_cell(x,y);
        if(mode==SP_LITE||mode==SP_KILL_TRAP||mode==SP_EVILNESS)hb_target_area_cell(x,y,FALSE);
        if(!index)continue;
        hb_target_affected(index,x,y,TRUE);
        /* Movement after the first hit is deliberately not forecast. */
        if(mode==SP_HOLYNESS)hb_target_area_cell(x,y,TRUE);
        if(mode==SP_EXPLODE&&p_ptr->pclass==CLASS_SNIPER){
            int radius=(effect->arg2+1)/2+1,r,ay,ax;
            for(r=0;r<=radius;++r)for(ay=y-r;ay<=y+r;++ay)for(ax=x-r;ax<=x+r;++ax)
                if(in_bounds2(ay,ax)&&distance(y,x,ay,ax)==r&&_gb_preview_area_reachable(y,x,ay,ax,GF_MISSILE)){
                    hb_target_area_cell(ax,ay,TRUE);_gb_preview_impact(ay,ax,PROJECT_KILL);
                }
        }
        if(pierce>0){--pierce;continue;}
        break;
    }
    if(y!=ty||x!=tx){hb_target_path_begin(NATIVE_PATH_GUIDE);hb_target_path_cell(x,y);hb_target_path_cell(tx,ty);}
    return TRUE;
}
static int _gb_preview_effect(const native_target_effect *effect,native_target_choice *choice);
typedef struct _gb_scatter_context {native_target_effect effect;} _gb_scatter_context;
static int _gb_scatter_ray(int y,int x,void *data)
{
    _gb_scatter_context *context=data;native_target_choice choice={0};
    choice.x=x;choice.y=y;choice.direction=5;
    return _gb_preview_effect(&context->effect,&choice);
}
static int _gb_room_pass(int y,int x,int lite){return _gb_preview_passable(y,x,lite?PROJECT_LOS:0);}
static int _gb_room_open(int cy,int cx,int lite)
{
    int i,len=0,best=0;
    for(i=0;i<16;++i){int d=cdd[i%8];if(_gb_room_pass(cy+ddy[d],cx+ddx[d],lite))++len;else{best=MAX(best,len);len=0;}}
    return MAX(best,len);
}
static int _gb_room_area(const native_target_effect *effect)
{
    byte seen[MAX_HGT][MAX_WID]={{0}};s16b qy[MAX_HGT*MAX_WID],qx[MAX_HGT*MAX_WID];int head=0,tail=0;
    int lite=effect->arg1!=0,limit=MAX_RANGE;
    const int directions[]={2,8,6,4,3,7,9,1};
    if(_gb_preview_known(py,px)&&!(cave[py][px].info&CAVE_ROOM)){
        int walls=0,j;if(!lite)return TRUE;
        for(j=0;j<8;++j)if(!_gb_room_pass(py+ddy_ddd[j],px+ddx_ddd[j],lite))++walls;
        if(_gb_room_pass(py,px,lite)&&walls==6&&_gb_room_open(py,px,lite)<=1)return TRUE;
    }
    qy[tail]=py;qx[tail++]=px;seen[py][px]=TRUE;
    while(head<tail){
        int y=qy[head],x=qx[head],known=_gb_preview_known(y,x),i;++head;
        int room=known&&(cave[y][x].info&CAVE_ROOM),affected=lite||!known||!is_mirror_grid(&cave[y][x]);
        if(!lite&&(dun_level||!is_daytime()))for(i=0;i<9;++i){int ny=y+ddy_ddd[i],nx=x+ddx_ddd[i];if(_gb_preview_known(ny,nx)&&have_flag(f_info[get_feat_mimic(&cave[ny][nx])].flags,FF_GLOW))affected=FALSE;}
        if(affected)hb_target_area_cell(x,y,!known);
        if(!_gb_room_pass(y,x,lite))continue;
        for(i=0;i<8;++i){int ny=y+ddy[directions[i]],nx=x+ddx[directions[i]],walls=0,j,accept;
            if(!in_bounds2(ny,nx)||seen[ny][nx])continue;known=_gb_preview_known(ny,nx);room=known&&(cave[ny][nx].info&CAVE_ROOM);
            accept=room||(!known&&distance(py,px,ny,nx)<=limit);
            if(known&&!room&&lite&&distance(py,px,ny,nx)<=limit){for(j=0;j<8;++j)if(!_gb_room_pass(ny+ddy_ddd[j],nx+ddx_ddd[j],lite))++walls;accept=!(in_bounds(ny,nx)&&_gb_room_pass(ny,nx,lite)&&walls==6&&_gb_room_open(ny,nx,lite)<=1);}
            if(!accept||tail==TEMP_MAX)continue;seen[ny][nx]=TRUE;qy[tail]=ny;qx[tail++]=nx;
        }
    }
    return TRUE;
}
static void _gb_faded_turn(int y,int x)
{
    int direction;
    for(direction=1;direction<=9;++direction)if(direction!=5){
        int ny=y+ddy[direction],nx=x+ddx[direction];
        if(!_gb_preview_passable(ny,nx,0))continue;
        hb_target_path_begin(NATIVE_PATH_GUIDE);hb_target_path_cell(x,y);hb_target_path_cell(nx,ny);
    }
}
static int _gb_preview_effect(const native_target_effect *effect,native_target_choice *choice)
{
    if(effect->arg2==-1&&(d_info[dungeon_type].flags1&DF1_DARKNESS))return TRUE;
    if(effect->shape==NATIVE_SHAPE_ROOM_AREA)return _gb_room_area(effect);
    if(effect->shape==NATIVE_SHAPE_MELEE){
        int index,side;
        for(index=0;index<8&&cdd[index]!=choice->direction;++index){}
        if(index==8)return -NATIVE_TARGET_SELF_FORBIDDEN;
        for(side=-effect->arg1;side<=effect->arg1;++side){
            int direction=(index+side+8)%8,y=py+ddy_cdd[direction],x=px+ddx_cdd[direction];
            if(!in_bounds(y,x))continue;
            hb_target_path_begin(NATIVE_PATH_REACHABLE);hb_target_path_cell(px,py);hb_target_path_cell(x,y);
            hb_target_area_cell(x,y,effect->possible);_gb_preview_impact(y,x,PROJECT_KILL);
        }
        return TRUE;
    }
    if(effect->shape==NATIVE_SHAPE_QUAKE||effect->shape==NATIVE_SHAPE_DESTRUCTION){
        int quake=effect->shape==NATIVE_SHAPE_QUAKE;
        int radius=quake?MIN(effect->radius,30):effect->radius,y,x;
        if((p_ptr->inside_quest&&is_fixed_quest_idx(p_ptr->inside_quest))||!dun_level||EXTRA_QUEST_FLOOR)return TRUE;
        if(radius<0||radius>100)return FALSE;
        for(y=choice->y-radius;y<=choice->y+radius;++y)for(x=choice->x-radius;x<=choice->x+radius;++x){
            if(!in_bounds(y,x)||distance(choice->y,choice->x,y,x)>radius)continue;
            if(quake&&y==choice->y&&x==choice->x)continue;
            hb_target_area_cell(x,y,quake||effect->possible);
            if(y!=choice->y||x!=choice->x)_gb_preview_impact(y,x,PROJECT_KILL);
        }
        return TRUE;
    }
    if(effect->shape==NATIVE_SHAPE_LOS){
        int y,x;
        for(y=1;y<cur_hgt-1;++y)for(x=1;x<cur_wid-1;++x){
            int in_area;
            if(effect->arg2&&effect->arg1)in_area=ABS(y-py)<=3&&ABS(x-px)<=3;
            else{
                in_area=_gb_preview_los(py,px,y,x)&&_gb_preview_projectable(py,px,y,x);
                if(effect->arg2&&_gb_preview_known(y,x)){
                    feature_type *feature=&f_info[get_feat_mimic(&cave[y][x])];
                    if(have_flag(feature->flags,FF_PERMANENT)&&!have_flag(feature->flags,FF_PROJECT))in_area=FALSE;
                }
            }
            if(in_area){hb_target_area_cell(x,y,effect->possible);if(y!=py||x!=px)_gb_preview_impact(y,x,PROJECT_KILL);}
        }
        return TRUE;
    }
    u16b path[512];int i,count,flags=effect->flags,cy=py,cx=px;
    int range=effect->range?effect->range:(project_length?project_length:MAX_RANGE);
    int spark=effect->shape==NATIVE_SHAPE_SPARK;
    int raw=effect->shape==NATIVE_SHAPE_PROJECT;
    int radial=effect->shape==NATIVE_SHAPE_BALL||effect->shape==NATIVE_SHAPE_BREATH||effect->shape==NATIVE_SHAPE_ROCKET||spark||raw;
    int mirror_ray=effect->shape==NATIVE_SHAPE_MIRROR_RAY;
    int beam=mirror_ray||effect->shape==NATIVE_SHAPE_BEAM||(flags&PROJECT_BEAM),radius=ABS(effect->radius);
    int tx=choice->x,ty=choice->y,selected_passed=FALSE;
    if(raw&&effect->radius<0)return FALSE;
    if(effect->shape==NATIVE_SHAPE_SHOT)return _gb_preview_shot(effect,choice);
    if(effect->shape==NATIVE_SHAPE_SELF_SCATTER){
        native_target_effect beam=*effect;int y,x;
        if(effect->radius<1||effect->radius>16)return FALSE;
        beam.shape=NATIVE_SHAPE_BEAM;beam.radius=0;beam.possible=TRUE;
        for(y=py-effect->radius;y<=py+effect->radius;++y)for(x=px-effect->radius;x<=px+effect->radius;++x){
            native_target_choice ray={x,y,0,5,0,0};
            if(!in_bounds(y,x)||distance(py,px,y,x)>effect->radius)continue;
            if(!(effect->arg1?_gb_preview_los(py,px,y,x):_gb_preview_projectable(py,px,y,x)))continue;
            if(!_gb_preview_effect(&beam,&ray))return FALSE;
        }
        return TRUE;
    }
    if(effect->shape==NATIVE_SHAPE_BLAST){
        _gb_scatter_context context;context.effect=*effect;
        if(effect->arg2<=0)return TRUE;
        context.effect.shape=(flags&PROJECT_BEAM)?NATIVE_SHAPE_BEAM:NATIVE_SHAPE_BOLT;
        context.effect.possible=effect->possible||effect->arg1>0;
        context.effect.arg2=1;
        return hb_target_scatter_points(py,px,choice->y,choice->x,choice->direction,effect->arg1,distance,_gb_scatter_ray,&context);
    }
    if(!beam&&!radial&&effect->shape!=NATIVE_SHAPE_BOLT)return FALSE;
    if(effect->shape==NATIVE_SHAPE_BREATH)radius=MIN(3,radius);
    if(spark)radius=MAX(1,MIN(2,radius));
    if(flags&PROJECT_LONG_RANGE)range=255;
    if(range<=0||range>500||radius>31)return FALSE;
    if(beam||effect->shape==NATIVE_SHAPE_BREATH){
        if(effect->element==GF_LITE||effect->element==GF_LITE_WEAK)flags|=PROJECT_LOS;
        if(effect->element==GF_DISINTEGRATE)flags|=PROJECT_DISI;
    }
    if(radial&&!raw&&effect->shape!=NATIVE_SHAPE_ROCKET&&choice->direction==5)flags&=~PROJECT_STOP;
    if(choice->direction!=5){int scale=raw?effect->arg1:99;tx=px+scale*ddx[choice->direction];ty=py+scale*ddy[choice->direction];}
    if(tx==px&&ty==py)flags&=~PROJECT_THRU;
    if(flags&PROJECT_JUMP){
        int r,y,x;
        if(!in_bounds(ty,tx))return -NATIVE_TARGET_UNAVAILABLE;
        if(!raw&&!_gb_preview_projectable(py,px,ty,tx))return -NATIVE_TARGET_UNAVAILABLE;
        if(!raw&&radius>0&&_gb_preview_known(ty,tx)&&have_flag(f_info[get_feat_mimic(&cave[ty][tx])].flags,FF_WALL))return -NATIVE_TARGET_UNAVAILABLE;
        for(r=0;r<=radius;++r)for(y=ty-r;y<=ty+r;++y)for(x=tx-r;x<=tx+r;++x)
            if(in_bounds2(y,x)&&distance(ty,tx,y,x)==r&&_gb_preview_area_reachable(ty,tx,y,x,effect->element)){
                hb_target_area_cell(x,y,effect->possible);_gb_preview_impact(y,x,flags);
            }
        return TRUE;
    }
    count=_gb_preview_project_path(path,range,py,px,ty,tx,flags);
    if(spark&&count<2)spark=FALSE;
    hb_target_path_begin(effect->possible?NATIVE_PATH_POSSIBLE:NATIVE_PATH_REACHABLE);hb_target_path_cell(px,py);
    if(beam)hb_target_area_cell(px,py,effect->possible);
    for(i=0;i<count;++i){
        int y=GRID_Y(path[i]),x=GRID_X(path[i]);
        if(radius&&!_gb_preview_passable(y,x,flags))break;
        cy=y;cx=x;hb_target_path_cell(x,y);
        if(effect->arg2)hb_target_area_cell(x,y,effect->possible);
        if(x==choice->x&&y==choice->y)selected_passed=TRUE;
        if(beam){hb_target_area_cell(x,y,effect->possible);_gb_preview_impact(y,x,flags);}
        if(!beam&&(flags&PROJECT_REFLECTABLE)&&_gb_preview_known(y,x)&&cave[y][x].m_idx){
            monster_type *monster=&m_list[cave[y][x].m_idx];
            if(monster->ml&&(r_info[monster->r_idx].r_flags2&RF2_REFLECTING))_gb_faded_turn(y,x);
        }
        if(mirror_ray&&_gb_preview_known(y,x)&&is_mirror_grid(&cave[y][x])){_gb_faded_turn(y,x);++i;break;}
    }
    count=i;
    if(radial&&(count<=MAX_RANGE||(flags&PROJECT_LONG_RANGE))){
        if((spark||effect->shape==NATIVE_SHAPE_BREATH)&&count){
            byte gx[65536],gy[65536],gm[1024];int grids=0,gm_rad=0;
            _gb_preview_breath_shape(path,count,&grids,gx,gy,gm,&gm_rad,radius,py,px,cy,cx,effect->element,spark);
            for(i=0;i<grids;++i)if(in_bounds2(gy[i],gx[i])){
                hb_target_area_cell(gx[i],gy[i],effect->possible);_gb_preview_impact(gy[i],gx[i],flags);
            }
        }else{
            int r,y,x;
            for(r=0;r<=radius;++r)for(y=cy-r;y<=cy+r;++y)for(x=cx-r;x<=cx+r;++x)
                if(in_bounds2(y,x)&&distance(cy,cx,y,x)==r&&_gb_preview_area_reachable(cy,cx,y,x,effect->element)){
                    hb_target_area_cell(x,y,effect->possible);_gb_preview_impact(y,x,flags);
                }
        }
    }else if(!radial&&!beam&&count)_gb_preview_impact(cy,cx,flags);
    if(!effect->arg2&&choice->direction==5&&!selected_passed&&count){
        count=_gb_preview_project_path(path,500,cy,cx,choice->y,choice->x,PROJECT_PATH);
        hb_target_path_begin(NATIVE_PATH_GUIDE);hb_target_path_cell(cx,cy);
        for(i=0;i<count;++i){int y=GRID_Y(path[i]),x=GRID_X(path[i]);hb_target_path_cell(x,y);}
    }
    return TRUE;
}
