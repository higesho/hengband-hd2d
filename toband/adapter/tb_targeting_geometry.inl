/* Native geometry functions are supplied by the adjacent generated include.
 *
 * 幻想蛮怒の `gb_targeting_geometry.inl` と同じ流れで、TOband（変愚蛮怒 1.5.1 系）の project()
 * の幾何を、プレイヤが知っている地形・見えているモンスターだけで写す。違いは次のとおり。
 *   - 地形の旗（FF_*）が無い。壁の判定は `cave_floor_bold`（見かけの地形番号の 0x20 ビット）。
 *   - 弾の止まり方・分解の止まり方は原作のマクロ（stoppable_bold・cave_stop_disintegration）から
 *     `tb_targeting_path.generated.inl` が作る。
 *   - 球の範囲は los()（分解は in_disintegration_range()）。ブレスは breath_shape()。
 *   - PROJECT_LOS・PROJECT_LONG_RANGE・PROJECT_MASTERSPARK・鏡の光線・弾幕の散らばりは TOband に無い。 */
#include "bridge/targeting_scatter.h"
static sint _tb_preview_project_path(u16b*,int,int,int,int,int,u32b);
static bool _tb_preview_los(int,int,int,int);
static bool _tb_preview_disintegration_range(int,int,int,int);
static void _tb_preview_breath_shape(u16b*,int,int*,byte*,byte*,u16b*,int*,int,int,int,int,int,bool,bool);
static int _tb_preview_stop_disintegration(int,int);
static int _tb_preview_stoppable(int,int,u32b);
static int _tb_preview_known(int y,int x)
{
    cave_type *cell;
    if(!in_bounds2(y,x))return FALSE;
    cell=&cave[y][x];
    return (cell->info&CAVE_MARK)||player_can_see_bold(y,x);
}
static int _tb_preview_monster(int y,int x)
{
    int index;
    if(!_tb_preview_known(y,x)||p_ptr->image)return 0;
    index=cave[y][x].m_idx;
    return index>0&&index<m_max&&m_list[index].ml?index:0;
}
/* 見かけの地形（cave.c の map_info() の冒頭と同じ式）。知らないマスは床として扱う。 */
static int _tb_preview_feat(int y,int x)
{
    cave_type *cell=&cave[y][x];
    if(!_tb_preview_known(y,x))return FEAT_FLOOR;
    return cell->mimic?cell->mimic:f_info[cell->feat].mimic;
}
/* cave_floor_bold() を見かけの地形で（原作のマクロ cave_floor_grid をそのまま使う）。
 * 知らないマスの壁は無いものとして扱う（見直し済みの決まり）。 */
static int _tb_preview_floor(int y,int x)
{
    cave_type shown;
    if(!in_bounds2(y,x))return FALSE;
    if(!_tb_preview_known(y,x))return TRUE;
    shown=cave[y][x];shown.feat=_tb_preview_feat(y,x);
    return cave_floor_grid(&shown);
}
static int _tb_preview_passable(int y,int x,u32b flags)
{
    if(!in_bounds2(y,x))return FALSE;
    if(flags&PROJECT_DISI)return !_tb_preview_stop_disintegration(y,x);
    return _tb_preview_floor(y,x);
}
static int _tb_preview_projectable(int sy,int sx,int y,int x)
{
    u16b path[512];int count=_tb_preview_project_path(path,project_length?project_length:MAX_RANGE,sy,sx,y,x,0);
    if(!count)return FALSE; /* No grid is ever projectable from itself (原作の projectable と同じ) */
    return GRID_Y(path[count-1])==y&&GRID_X(path[count-1])==x;
}
/* 球・ブレスの爆発の範囲に入るか（原作の project() が使う判定と同じ）。 */
static int _tb_preview_area_reachable(int cy,int cx,int y,int x,int element)
{
    if(!in_bounds2(y,x))return FALSE;
    if(element==GF_DISINTEGRATE)return _tb_preview_disintegration_range(cy,cx,y,x);
    return _tb_preview_los(cy,cx,y,x);
}
static void _tb_preview_impact(int y,int x,u32b flags)
{
    int index=_tb_preview_monster(y,x);
    if(index&&(flags&PROJECT_KILL))hb_target_affected(index,x,y,TRUE);
}
/* 射撃・投擲の飛び方（cmd2.c の do_cmd_fire_aux() / do_cmd_throw_aux() の歩き方）。
 * 目標のマスで止まる（貫通の射撃を除く）・壁で止まる・最初のモンスターで当たりを試す。
 * 当たったあとの動き（外れて飛び続ける・貫通・ロケットの爆発）は予測しない。
 * effect->arg1: 貫通・連射など、最初の当たりのあとも続く可能性がある  effect->arg2: 目標のマスのモンスターにしか当たらない */
static int _tb_preview_missile(const native_target_effect *effect,native_target_choice *choice)
{
    int y=py,x=px,ty=choice->y,tx=choice->x,steps,thru=(effect->flags&PROJECT_THRU)!=0;
    const int shot=effect->shape==NATIVE_SHAPE_SHOT;
    if(effect->range<=0||effect->range>500)return FALSE;
    if(choice->direction!=5){ty=py+99*ddy[choice->direction];tx=px+99*ddx[choice->direction];}
    hb_target_path_begin(effect->possible?NATIVE_PATH_POSSIBLE:NATIVE_PATH_REACHABLE);hb_target_path_cell(px,py);
    for(steps=0;steps<effect->range;++steps){
        int ny=y,nx=x,index;
        if(!thru&&y==ty&&x==tx)break;
        mmove2(&ny,&nx,py,px,ty,tx);
        if(!in_bounds2(ny,nx))break;
        index=_tb_preview_monster(ny,nx);
        if(!_tb_preview_floor(ny,nx)&&!index)break;
        y=ny;x=nx;hb_target_path_cell(x,y);
        if(index&&(!shot||!effect->arg2||(y==ty&&x==tx))){
            hb_target_affected(index,x,y,TRUE);
            /* Movement after the first hit is deliberately not forecast. */
            break;
        }
    }
    if(y!=ty||x!=tx){hb_target_path_begin(NATIVE_PATH_GUIDE);hb_target_path_cell(x,y);hb_target_path_cell(tx,ty);}
    return TRUE;
}
static int _tb_preview_effect(const native_target_effect *effect,native_target_choice *choice);
typedef struct _tb_scatter_context {native_target_effect effect;} _tb_scatter_context;
static int _tb_scatter_ray(int y,int x,void *data)
{
    _tb_scatter_context *context=data;native_target_choice choice={0};
    choice.x=x;choice.y=y;choice.direction=5;
    return _tb_preview_effect(&context->effect,&choice);
}
/* lite_room()・unlite_room()（spells2.c）が広げる範囲。cave_temp_room_aux() と同じ判定を、知っているマスだけで。
 * effect->arg1: 照らす（1）か暗くする（0）か。知らないマスは「部屋かもしれない」ので、見込み（possible）として出す。 */
static int _tb_room_pass(int y,int x){return _tb_preview_floor(y,x);}
static int _tb_room_open(int cy,int cx)
{
    int i,len=0,best=0;
    for(i=0;i<16;++i){int d=cdd[i%8];if(_tb_room_pass(cy+ddy[d],cx+ddx[d]))++len;else{best=MAX(best,len);len=0;}}
    return MAX(best,len);
}
static int _tb_room_area(const native_target_effect *effect)
{
    static byte seen[MAX_HGT][MAX_WID];s16b qy[TEMP_MAX],qx[TEMP_MAX];int head=0,tail=0;
    int lite=effect->arg1!=0,limit=MAX_RANGE;
    const int directions[]={2,8,6,4,3,7,9,1};
    memset(seen,0,sizeof(seen));
    /* 開始のマスは原作でも cave_temp_room_aux() を通る。部屋の外から暗くしても何も起きない。 */
    if(_tb_preview_known(py,px)&&!(cave[py][px].info&CAVE_ROOM)){
        int walls=0,j;if(!lite)return TRUE;
        for(j=0;j<8;++j)if(!_tb_room_pass(py+ddy_ddd[j],px+ddx_ddd[j]))++walls;
        if(in_bounds(py,px)&&_tb_room_pass(py,px)&&walls==6&&_tb_room_open(py,px)<=1)return TRUE;
    }
    qy[tail]=py;qx[tail++]=px;seen[py][px]=TRUE;
    while(head<tail){
        int y=qy[head],x=qx[head],known=_tb_preview_known(y,x),i;++head;
        hb_target_area_cell(x,y,!known);
        if(!_tb_room_pass(y,x))continue;
        for(i=0;i<8;++i){int ny=y+ddy[directions[i]],nx=x+ddx[directions[i]],walls=0,j,accept;
            if(!in_bounds2(ny,nx)||seen[ny][nx])continue;known=_tb_preview_known(ny,nx);
            accept=known&&(cave[ny][nx].info&CAVE_ROOM);
            if(!accept&&!known)accept=distance(py,px,ny,nx)<=limit;
            else if(!accept&&lite&&distance(py,px,ny,nx)<=limit){
                for(j=0;j<8;++j)if(!_tb_room_pass(ny+ddy_ddd[j],nx+ddx_ddd[j]))++walls;
                accept=!(in_bounds(ny,nx)&&_tb_room_pass(ny,nx)&&walls==6&&_tb_room_open(ny,nx)<=1);
            }
            if(!accept||tail==TEMP_MAX)continue;seen[ny][nx]=TRUE;qy[tail]=ny;qx[tail++]=nx;
        }
    }
    return TRUE;
}
/* 反射するモンスターに当たった弾は、周りのどこかへ曲がる（原作の project() の跳ね返り）。 */
static void _tb_faded_turn(int y,int x)
{
    int direction;
    for(direction=1;direction<=9;++direction)if(direction!=5){
        int ny=y+ddy[direction],nx=x+ddx[direction];
        if(!_tb_preview_passable(ny,nx,0))continue;
        hb_target_path_begin(NATIVE_PATH_GUIDE);hb_target_path_cell(x,y);hb_target_path_cell(nx,ny);
    }
}
static int _tb_preview_effect(const native_target_effect *effect,native_target_choice *choice)
{
    u32b flags=(u32b)effect->flags;
    if(effect->shape==NATIVE_SHAPE_ROOM_AREA)return _tb_room_area(effect);
    if(effect->shape==NATIVE_SHAPE_MELEE){
        int index,side;
        for(index=0;index<8&&cdd[index]!=choice->direction;++index){}
        if(index==8)return -NATIVE_TARGET_SELF_FORBIDDEN;
        for(side=-effect->arg1;side<=effect->arg1;++side){
            int direction=(index+side+8)%8,y=py+ddy_cdd[direction],x=px+ddx_cdd[direction];
            if(!in_bounds(y,x))continue;
            hb_target_path_begin(NATIVE_PATH_REACHABLE);hb_target_path_cell(px,py);hb_target_path_cell(x,y);
            hb_target_area_cell(x,y,effect->possible);_tb_preview_impact(y,x,PROJECT_KILL);
        }
        return TRUE;
    }
    if(effect->shape==NATIVE_SHAPE_QUAKE||effect->shape==NATIVE_SHAPE_DESTRUCTION){
        /* earthquake_aux()・destroy_area()（spells2.c）。固定クエストの階と町では何も起きない。 */
        int quake=effect->shape==NATIVE_SHAPE_QUAKE;
        int radius=quake?MIN(effect->radius,12):effect->radius,y,x;
        if((p_ptr->inside_quest&&quest_is_fixed(p_ptr->inside_quest))||!dun_level)return TRUE;
        if(radius<0||radius>100)return FALSE;
        for(y=choice->y-radius;y<=choice->y+radius;++y)for(x=choice->x-radius;x<=choice->x+radius;++x){
            if(!in_bounds(y,x)||distance(choice->y,choice->x,y,x)>radius)continue;
            if(y==choice->y&&x==choice->x)continue;
            hb_target_area_cell(x,y,quake||effect->possible);
            _tb_preview_impact(y,x,PROJECT_KILL);
        }
        return TRUE;
    }
    if(effect->shape==NATIVE_SHAPE_LOS){
        /* project_hack()（spells2.c）：視界（CAVE_VIEW）にいるモンスターへ PROJECT_JUMP。 */
        int y,x;
        for(y=1;y<cur_hgt-1;++y)for(x=1;x<cur_wid-1;++x){
            if(player_has_los_bold(y,x)){hb_target_area_cell(x,y,effect->possible);if(y!=py||x!=px)_tb_preview_impact(y,x,PROJECT_KILL);}
        }
        return TRUE;
    }
    u16b path[512];int i,count,path_n,cy=py,cx=px;
    int range=effect->range?effect->range:(project_length?project_length:MAX_RANGE);
    int raw=effect->shape==NATIVE_SHAPE_PROJECT;
    int radial=effect->shape==NATIVE_SHAPE_BALL||effect->shape==NATIVE_SHAPE_BREATH||effect->shape==NATIVE_SHAPE_ROCKET||raw;
    int beam=effect->shape==NATIVE_SHAPE_BEAM||(flags&PROJECT_BEAM),radius=ABS(effect->radius);
    int tx=choice->x,ty=choice->y,selected_passed=FALSE;
    if(raw&&effect->radius<0)return FALSE;
    if(effect->shape==NATIVE_SHAPE_SHOT||effect->shape==NATIVE_SHAPE_THROW)return _tb_preview_missile(effect,choice);
    if(effect->shape==NATIVE_SHAPE_SELF_SCATTER){
        /* スターライトの杖：scatter(…,4,0) が選ぶ視線の通るマスへ、それぞれ PROJECT_THRU の光線。 */
        native_target_effect beam=*effect;int y,x;
        if(effect->radius<1||effect->radius>16)return FALSE;
        beam.shape=NATIVE_SHAPE_BEAM;beam.radius=0;beam.possible=TRUE;
        for(y=py-effect->radius;y<=py+effect->radius;++y)for(x=px-effect->radius;x<=px+effect->radius;++x){
            native_target_choice ray={x,y,0,5,0,0};
            if(!in_bounds(y,x)||distance(py,px,y,x)>effect->radius)continue;
            if(!_tb_preview_los(py,px,y,x)||!_tb_preview_floor(y,x)||(y==py&&x==px))continue;
            if(!_tb_preview_effect(&beam,&ray))return FALSE;
        }
        return TRUE;
    }
    if(effect->shape==NATIVE_SHAPE_BLAST){
        /* fire_blast()：偏差の円の中の各点へ、PROJECT_THRU の弾を撃つ。 */
        _tb_scatter_context context;context.effect=*effect;
        if(effect->arg2<=0)return TRUE;
        context.effect.shape=(flags&PROJECT_BEAM)?NATIVE_SHAPE_BEAM:NATIVE_SHAPE_BOLT;
        context.effect.possible=effect->possible||effect->arg1>0;
        context.effect.arg2=1;
        return hb_target_scatter_points(py,px,choice->y,choice->x,choice->direction,effect->arg1,distance,_tb_scatter_ray,&context);
    }
    if(!beam&&!radial&&effect->shape!=NATIVE_SHAPE_BOLT)return FALSE;
    /* project() は breath を「半径が負」で受け取る。分解のブレスだけ PROJECT_DISI が立つ。 */
    if(effect->shape==NATIVE_SHAPE_BREATH&&effect->element==GF_DISINTEGRATE)flags|=PROJECT_DISI;
    if(range<=0||range>500||radius>31)return FALSE;
    if(radial&&!raw&&effect->shape!=NATIVE_SHAPE_ROCKET&&choice->direction==5)flags&=~PROJECT_STOP;
    if(choice->direction!=5){int scale=raw?effect->arg1:99;tx=px+scale*ddx[choice->direction];ty=py+scale*ddy[choice->direction];}
    if(tx==px&&ty==py)flags&=~PROJECT_THRU;
    if(flags&PROJECT_JUMP){
        int r,y,x;
        if(!in_bounds(ty,tx))return -NATIVE_TARGET_UNAVAILABLE;
        for(r=0;r<=radius;++r)for(y=ty-r;y<=ty+r;++y)for(x=tx-r;x<=tx+r;++x)
            if(in_bounds2(y,x)&&distance(ty,tx,y,x)==r&&_tb_preview_area_reachable(ty,tx,y,x,effect->element)){
                hb_target_area_cell(x,y,effect->possible);_tb_preview_impact(y,x,flags);
            }
        return TRUE;
    }
    path_n=_tb_preview_project_path(path,range,py,px,ty,tx,flags);
    count=path_n;
    hb_target_path_begin(effect->possible?NATIVE_PATH_POSSIBLE:NATIVE_PATH_REACHABLE);hb_target_path_cell(px,py);
    if(beam)hb_target_area_cell(px,py,effect->possible);
    for(i=0;i<path_n;++i){
        int y=GRID_Y(path[i]),x=GRID_X(path[i]);
        if(radius&&!_tb_preview_passable(y,x,flags))break;
        cy=y;cx=x;hb_target_path_cell(x,y);
        if(effect->arg2)hb_target_area_cell(x,y,effect->possible);
        if(x==choice->x&&y==choice->y)selected_passed=TRUE;
        if(beam){hb_target_area_cell(x,y,effect->possible);_tb_preview_impact(y,x,flags);}
        if(!beam&&(flags&PROJECT_REFLECTABLE)&&_tb_preview_known(y,x)&&cave[y][x].m_idx){
            monster_type *monster=&m_list[cave[y][x].m_idx];
            if(monster->ml&&(r_info[monster->r_idx].r_flags2&RF2_REFLECTING))_tb_faded_turn(y,x);
        }
    }
    count=i;
    if(radial&&path_n<=MAX_RANGE){
        if(effect->shape==NATIVE_SHAPE_BREATH&&path_n&&(cy!=py||cx!=px)){
            /* breath_shape()：epicenter は経路の最後に進めた所。dist には打ち切る前の経路の長さを渡す（原作のまま）。 */
            byte gx[2048],gy[2048];u16b gm[32];int grids=0,gm_rad=radius;
            _tb_preview_breath_shape(path,path_n,&grids,gx,gy,gm,&gm_rad,radius,py,px,cy,cx,effect->element==GF_DISINTEGRATE,FALSE);
            for(i=0;i<grids;++i)if(in_bounds2(gy[i],gx[i])){
                hb_target_area_cell(gx[i],gy[i],effect->possible);_tb_preview_impact(gy[i],gx[i],flags);
            }
        }else{
            int r,y,x;
            for(r=0;r<=radius;++r)for(y=cy-r;y<=cy+r;++y)for(x=cx-r;x<=cx+r;++x)
                if(in_bounds2(y,x)&&distance(cy,cx,y,x)==r&&_tb_preview_area_reachable(cy,cx,y,x,effect->element)){
                    hb_target_area_cell(x,y,effect->possible);_tb_preview_impact(y,x,flags);
                }
        }
    }else if(!radial&&!beam&&count)_tb_preview_impact(cy,cx,flags);
    if(!effect->arg2&&choice->direction==5&&!selected_passed&&count){
        count=_tb_preview_project_path(path,500,cy,cx,choice->y,choice->x,PROJECT_PATH);
        hb_target_path_begin(NATIVE_PATH_GUIDE);hb_target_path_cell(cx,cy);
        for(i=0;i<count;++i){int y=GRID_Y(path[i]),x=GRID_X(path[i]);hb_target_path_cell(x,y);}
    }
    return TRUE;
}
