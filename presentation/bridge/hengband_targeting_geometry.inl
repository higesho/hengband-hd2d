// Read-only views supplied to the source-derived native geometry below.
// Terrain mimics are the player's visible terrain, including hidden doors.
#include "bridge/targeting_scatter.h"
#include "mind/snipe-types.h"
#include "dungeon/quest.h"
#include "system/dungeon/quest-definition.h"
#include "grid/grid.h"
#include "monster-race/race-misc-flags.h"
#include "system/dungeon/dungeon-definition.h"
#include "system/monrace/monrace-definition.h"
#include "world/world.h"
#include <deque>
#include <set>
class HengTargetGrid {
public:
    HengTargetGrid(PlayerType &player,const Pos2D &pos):player_(&player),pos_(pos){}
    bool inside()const {return player_->current_floor_ptr->contains(pos_,FloorBoundary::OUTER_WALL_INCLUSIVE);}
    bool interior()const {return player_->current_floor_ptr->contains(pos_,FloorBoundary::OUTER_WALL_EXCLUSIVE);}
    bool known()const {
        const auto &floor=*player_->current_floor_ptr;
        return inside()&&(floor.get_grid(pos_).is_mark()||player_can_see_bold(player_,pos_.y,pos_.x));
    }
    bool has(TerrainCharacteristics flag)const {
        if(!inside())return false;
        if(!known())return interior()&&(flag==TerrainCharacteristics::PROJECTION||flag==TerrainCharacteristics::LOS||flag==TerrainCharacteristics::PLACE);
        return player_->current_floor_ptr->get_grid(pos_).get_terrain(TerrainKind::MIMIC).has(flag);
    }
    bool can_block_disintegration()const {
        return !inside()||(!known()?!interior():!has(TerrainCharacteristics::PROJECTION)
            &&!(has(TerrainCharacteristics::CAN_DISINTEGRATE)&&!has(TerrainCharacteristics::PERMANENT)));
    }
    bool has_los_terrain()const {return inside()&&(!known()?interior():player_->current_floor_ptr->get_grid(pos_).has_los_terrain(TerrainKind::MIMIC));}
    bool is_mirror()const {return known()&&player_->current_floor_ptr->get_grid(pos_).is_mirror();}
    bool is_room()const {return known()&&any_bits(player_->current_floor_ptr->get_grid(pos_).info,CAVE_ROOM);}
    int visible_monster()const {
        const auto &floor=*player_->current_floor_ptr;
#if HB_HENGBAND_SOURCE_3025
        if(!known()||player_->effects()->hallucination().is_active())return 0;
#else
        if(!known()||player_->effects()->hallucination().is_hallucinated())return 0;
#endif
        const int index=floor.get_grid(pos_).m_idx;
        return index>0&&index<floor.m_max&&floor.m_list[index].ml?index:0;
    }
    bool has_monster()const {return visible_monster()!=0;}
    bool empty_push_destination()const {
        return inside()&&!has_monster()&&(!known()||has(TerrainCharacteristics::PLACE)
            &&player_can_enter(player_,player_->current_floor_ptr->get_grid(pos_).get_terrain_id(TerrainKind::MIMIC),0));
    }
private:
    PlayerType *player_;
    Pos2D pos_;
};
class HengTargetFloor {
public:
    explicit HengTargetFloor(PlayerType &player):player_(&player){}
    HengTargetGrid get_grid(const Pos2D &pos)const {return {*player_,pos};}
    bool contains(const Pos2D &pos,FloorBoundary boundary)const {return player_->current_floor_ptr->contains(pos,boundary);}
    bool can_block_disintegration_at(const Pos2D &pos)const {return get_grid(pos).can_block_disintegration();}
    bool has_los_terrain_at(const Pos2D &pos)const {return get_grid(pos).has_los_terrain();}
private:
    PlayerType *player_;
};

#include "target/heng_target_geometry.generated.inl"

static bool heng_target_shot(PlayerType &player,const native_target_choice &choice,const native_target_effect &effect,bool &stochastic)
{
    const int mode=effect.arg1;
    if(effect.range<=0||effect.range>4096)return false;
    HengTargetFloor floor(player);const auto source=player.get_position();Pos2D target{choice.y,choice.x},pos=source;
    if(choice.direction!=5){const auto delta=presentation::targeting_direction_delta(choice.direction);target={source.y+99*delta.y,source.x+99*delta.x};}
    const HengTargetPath native_path(floor,effect.range,source,source,target,PROJECT_PATH|PROJECT_THRU);
    int pierce=mode==SP_PIERCE?effect.arg2:0;
    hb_target_path_begin(NATIVE_PATH_REACHABLE);hb_target_path_cell(source.x,source.y);
    for(int step=0;step<native_path.path_num()&&pos!=target;++step){
        const auto next=mmove2(pos,source,target);const auto grid=floor.get_grid(next);
        if(!grid.inside())break;
        const int index=grid.visible_monster();
        if(mode==SP_KILL_WALL&&grid.has(TerrainCharacteristics::STONE)&&!index){
            hb_target_path_cell(next.x,next.y);hb_target_area_cell(next.x,next.y,false);pos=next;break;
        }
        if(!grid.has(TerrainCharacteristics::PROJECTION)&&!index)break;
        pos=next;hb_target_path_cell(pos.x,pos.y);
        if(mode==SP_LITE||mode==SP_KILL_TRAP||mode==SP_EVILNESS)hb_target_area_cell(pos.x,pos.y,false);
        if(!index)continue;
        hb_target_affected(index,pos.x,pos.y,true);
        // Movement after the first hit is deliberately not forecast.
        if(mode==SP_HOLYNESS){hb_target_area_cell(pos.x,pos.y,true);stochastic=true;}
        if(mode==SP_EXPLODE){
            for(const auto &[distance,cell]:heng_target_ball_shape(floor,pos,(effect.arg2+1)/2+1,AttributeType::MISSILE)){
                const auto area_grid=floor.get_grid(cell);if(!area_grid.inside())continue;
                hb_target_area_cell(cell.x,cell.y,true);
                if(const int affected=area_grid.visible_monster())hb_target_affected(affected,cell.x,cell.y,true);
            }
            stochastic=true;
        }
        if(pierce>0){--pierce;continue;}
        break;
    }
    if(pos!=target){hb_target_path_begin(NATIVE_PATH_GUIDE);hb_target_path_cell(pos.x,pos.y);hb_target_path_cell(target.x,target.y);}
    return true;
}

static bool heng_target_effect(PlayerType &player,const native_target_choice &choice,const native_target_effect &effect,bool &stochastic);
struct HengScatterContext {PlayerType *player;native_target_effect effect;bool *stochastic;};
static int heng_scatter_distance(int sy,int sx,int ty,int tx){return Grid::calc_distance({sy,sx},{ty,tx});}
static bool heng_target_room(PlayerType &player,const native_target_effect &effect)
{
    const bool lite=effect.arg1!=0;const auto source=player.get_position();HengTargetFloor floor(player);
    const int limit=AngbandSystem::get_instance().get_max_range();std::deque<Pos2D> queue;std::set<std::pair<int,int>> seen;
    static constexpr int directions[]={2,3,6,9,8,7,4,1};
    const auto pass=[&](const Pos2D &pos){return floor.get_grid(pos).has(lite?TerrainCharacteristics::LOS:TerrainCharacteristics::PROJECTION);};
    const auto next_to_open=[&](const Pos2D &center){int len=0,best=0;for(int i=0;i<16;++i){const auto d=presentation::targeting_direction_delta(directions[i%8]);const Pos2D p{center.y+d.y,center.x+d.x};if(pass(p))++len;else{best=std::max(best,len);len=0;}}return std::max(best,len);};
    const auto addable=[&](const Pos2D &pos){
        const auto grid=floor.get_grid(pos);if(!grid.inside())return false;
        if(!grid.known())return Grid::calc_distance(source,pos)<=limit;
        if(grid.is_room())return true;if(!lite||Grid::calc_distance(source,pos)>limit)return false;
        int walls=0;for(int d:directions){const auto delta=presentation::targeting_direction_delta(d);if(!pass({pos.y+delta.y,pos.x+delta.x}))++walls;}
        return !(grid.interior()&&pass(pos)&&walls==6&&next_to_open(pos)<=1);
    };
    if(!addable(source))return true;seen.insert({source.y,source.x});queue.push_back(source);
    while(!queue.empty()){
        const auto pos=queue.front();queue.pop_front();const auto grid=floor.get_grid(pos);
        bool affected=lite||!grid.is_mirror();
        if(!lite&&(player.current_floor_ptr->is_underground()||!AngbandWorld::get_instance().is_daytime()))
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
                if(floor.get_grid({pos.y+dy,pos.x+dx}).has(TerrainCharacteristics::GLOW))affected=false;
        if(affected)hb_target_area_cell(pos.x,pos.y,!grid.known());
        if(!pass(pos))continue;
        for(int d:directions){const auto delta=presentation::targeting_direction_delta(d);const Pos2D next{pos.y+delta.y,pos.x+delta.x};
            if(seen.contains({next.y,next.x})||!addable(next))continue;seen.insert({next.y,next.x});queue.push_back(next);}
    }
    return true;
}
static void heng_faded_turn(const HengTargetFloor &floor,const Pos2D &at)
{
    for(const auto direction:{1,2,3,4,6,7,8,9}){
        const auto delta=presentation::targeting_direction_delta(direction);const Pos2D next{at.y+delta.y,at.x+delta.x};
        if(!floor.get_grid(next).has(TerrainCharacteristics::PROJECTION))continue;
        hb_target_path_begin(NATIVE_PATH_GUIDE);hb_target_path_cell(at.x,at.y);hb_target_path_cell(next.x,next.y);
    }
}
static int heng_scatter_ray(int y,int x,void *data)
{
    auto &context=*static_cast<HengScatterContext *>(data);native_target_choice choice{};
    choice.x=x;choice.y=y;choice.direction=5;
    return heng_target_effect(*context.player,choice,context.effect,*context.stochastic);
}
static bool heng_target_effect(PlayerType &player,const native_target_choice &choice,const native_target_effect &effect,bool &stochastic)
{
    if(effect.arg2==-1&&player.current_floor_ptr->get_dungeon_definition().flags.has(DungeonFeatureType::DARKNESS))return true;
    if(effect.shape==NATIVE_SHAPE_ROOM_AREA)return heng_target_room(player,effect);
    if(effect.shape==NATIVE_SHAPE_MELEE){
        static constexpr int directions[]={8,9,6,3,2,1,4,7};
        int index=0;while(index<8&&directions[index]!=choice.direction)++index;
        if(index==8)return false;
        const auto source=player.get_position();HengTargetFloor floor(player);
        for(int side=-effect.arg1;side<=effect.arg1;++side){
            const auto delta=presentation::targeting_direction_delta(directions[(index+side+8)%8]);
            const Pos2D pos{source.y+delta.y,source.x+delta.x};const auto grid=floor.get_grid(pos);
            if(!grid.inside())continue;
            hb_target_path_begin(NATIVE_PATH_REACHABLE);hb_target_path_cell(source.x,source.y);hb_target_path_cell(pos.x,pos.y);
            hb_target_area_cell(pos.x,pos.y,effect.possible);
            if(const int monster=grid.visible_monster())hb_target_affected(monster,pos.x,pos.y,true);
        }
        return true;
    }
    if(effect.shape==NATIVE_SHAPE_QUAKE||effect.shape==NATIVE_SHAPE_DESTRUCTION){
        const auto &native_floor=*player.current_floor_ptr;
        if((native_floor.is_in_quest()&&QuestType::is_fixed(native_floor.quest_number))||!native_floor.is_underground())return true;
        const bool quake=effect.shape==NATIVE_SHAPE_QUAKE;
        const int radius=quake?std::min(effect.radius,15):effect.radius;
        if(radius<0||radius>100)return false;
        const Pos2D center{choice.y,choice.x};HengTargetFloor floor(player);
        for(int y=center.y-radius;y<=center.y+radius;++y)for(int x=center.x-radius;x<=center.x+radius;++x){
            const Pos2D pos{y,x};const auto grid=floor.get_grid(pos);
            if(!native_floor.contains(pos,FloorBoundary::OUTER_WALL_EXCLUSIVE)||Grid::calc_distance(center,pos)>radius)continue;
            if(quake&&pos==center)continue;
            hb_target_area_cell(x,y,quake||effect.possible);
            if(pos!=center)if(const int index=grid.visible_monster())hb_target_affected(index,x,y,true);
        }
        stochastic|=quake||effect.possible;return true;
    }
    if(effect.shape==NATIVE_SHAPE_LOS){
        HengTargetFloor known_floor(player);const auto &native_floor=*player.current_floor_ptr;const auto source=player.get_position();
        for(int y=0;y<native_floor.height;++y)for(int x=0;x<native_floor.width;++x){
            const Pos2D pos{y,x};const auto grid=known_floor.get_grid(pos);
            if(!heng_target_los(known_floor,source,pos)||!heng_target_projectable(known_floor,source,pos))continue;
            hb_target_area_cell(x,y,effect.possible);
            if(const int index=grid.visible_monster())hb_target_affected(index,x,y,true);
        }
        stochastic|=effect.possible!=0;return true;
    }
    if(effect.shape==NATIVE_SHAPE_SHOT)return heng_target_shot(player,choice,effect,stochastic);
    if(effect.shape==NATIVE_SHAPE_SELF_SCATTER){
        if(effect.radius<1||effect.radius>16)return false;
        const auto source=player.get_position();HengTargetFloor floor(player);
        auto beam_effect=effect;beam_effect.shape=NATIVE_SHAPE_BEAM;beam_effect.radius=0;beam_effect.possible=1;beam_effect.arg2=1;
        hb_target_area_cell(source.x,source.y,true);
        for(int y=source.y-effect.radius;y<=source.y+effect.radius;++y)for(int x=source.x-effect.radius;x<=source.x+effect.radius;++x){
            const Pos2D pos{y,x};
            if(!floor.contains(pos,FloorBoundary::OUTER_WALL_EXCLUSIVE)||Grid::calc_distance(source,pos)>effect.radius)continue;
            if(!(effect.arg1?heng_target_los(floor,source,pos):heng_target_projectable(floor,source,pos)))continue;
            native_target_choice ray{x,y,0,5,0,0};
            if(!heng_target_effect(player,ray,beam_effect,stochastic))return false;
        }
        stochastic=true;return true;
    }
    if(effect.shape==NATIVE_SHAPE_BLAST){
        if(effect.arg2<=0)return true;
        HengScatterContext context{&player,effect,&stochastic};
        context.effect.shape=(effect.flags&PROJECT_BEAM)?NATIVE_SHAPE_BEAM:NATIVE_SHAPE_BOLT;
        context.effect.possible=effect.possible||effect.arg1>0;context.effect.arg2=1;
        const auto source=player.get_position();
        return hb_target_scatter_points(source.y,source.x,choice.y,choice.x,choice.direction,effect.arg1,heng_scatter_distance,heng_scatter_ray,&context)!=0;
    }
    const bool mirror_ray=effect.shape==NATIVE_SHAPE_MIRROR_RAY;
    const bool beam=mirror_ray||effect.shape==NATIVE_SHAPE_BEAM||(effect.flags&PROJECT_BEAM);
    const bool raw=effect.shape==NATIVE_SHAPE_PROJECT;
    const bool ball=raw||effect.shape==NATIVE_SHAPE_BALL||effect.shape==NATIVE_SHAPE_ROCKET;
    bool breath=effect.shape==NATIVE_SHAPE_BREATH;
    const bool area_effect=ball||breath;
    if(!beam&&!ball&&!breath&&effect.shape!=NATIVE_SHAPE_BOLT)return false;
    const int radius=effect.radius;
    if(radius<0||radius>100)return false;
    const auto &system=AngbandSystem::get_instance();
    const int range=effect.range>0?effect.range:(project_length?project_length:system.get_max_range());
    if(range<1||range>4096)return false;
    HengTargetFloor floor(player);
    const auto source=player.get_position();Pos2D target(choice.y,choice.x);
    if(choice.direction!=5){const auto delta=presentation::targeting_direction_delta(choice.direction);const int scale=raw?effect.arg1:99;target={source.y+scale*delta.y,source.x+scale*delta.x};}
    uint32_t flags=effect.flags;
    if((ball||breath)&&!raw&&effect.shape!=NATIVE_SHAPE_ROCKET&&choice.direction==5)flags&=~PROJECT_STOP;
    if(source==target)flags&=~PROJECT_THRU;
    const auto element=static_cast<AttributeType>(effect.element);
    if((element==AttributeType::LITE||element==AttributeType::LITE_WEAK)&&(beam||breath))flags|=PROJECT_LOS;
    if(element==AttributeType::DISINTEGRATE){flags|=PROJECT_GRID;if(beam||breath)flags|=PROJECT_DISI;}
    if(raw&&(flags&PROJECT_JUMP)){
        if(!floor.contains(target,FloorBoundary::OUTER_WALL_EXCLUSIVE))return false;
        for(const auto &[distance,pos]:heng_target_ball_shape(floor,target,radius,element)){
            const auto grid=floor.get_grid(pos);if(!grid.inside())continue;
            hb_target_area_cell(pos.x,pos.y,effect.possible);
            if(const int index=grid.visible_monster();index&&(flags&PROJECT_KILL))hb_target_affected(index,pos.x,pos.y,true);
        }
        stochastic=true;return true;
    }
    HengTargetPath path(floor,range,source,source,target,flags);
    auto impact=source;int traveled=0;bool selected_passed=source==target,mirror_changed=false;
    std::vector<std::pair<int,Pos2D>> positions;
    if(beam)positions.emplace_back(0,source);
    hb_target_path_begin(effect.possible?NATIVE_PATH_POSSIBLE:NATIVE_PATH_REACHABLE);hb_target_path_cell(source.x,source.y);
    for(const auto &pos:path){
        const auto grid=floor.get_grid(pos);
        if(!grid.inside())break;
        const bool blocked=(flags&PROJECT_DISI)?grid.can_block_disintegration():
            (flags&PROJECT_LOS)?!grid.has_los_terrain():!grid.has(TerrainCharacteristics::PROJECTION);
        if(radius>0&&blocked)break;
        impact=pos;++traveled;hb_target_path_cell(pos.x,pos.y);selected_passed|=pos==target;
        if(effect.arg2)hb_target_area_cell(pos.x,pos.y,effect.possible);
        if(beam)positions.emplace_back(0,pos);
        const int visible=grid.visible_monster();
        if(!beam&&(flags&PROJECT_REFLECTABLE)&&visible
            &&floor.get_grid(pos).known()&&floor.get_grid(pos).visible_monster()
            &&player.current_floor_ptr->m_list[visible].get_monrace().r_misc_flags.has(MonsterMiscType::REFLECTING))
            heng_faded_turn(floor,pos);
        if(mirror_ray&&grid.is_mirror()){mirror_changed=true;heng_faded_turn(floor,pos);break;}
    }
    if(breath&&!traveled)breath=false;
    if(traveled<=system.get_max_range()){
        if(beam&&!positions.empty()&&!mirror_changed)positions.pop_back();
        auto area=breath?heng_target_breath_shape(floor,path,traveled,radius,source,impact,element)
            :heng_target_ball_shape(floor,impact,radius,element);
        positions.insert(positions.end(),area.begin(),area.end());
    }
    for(const auto &[distance,pos]:positions){
        const auto grid=floor.get_grid(pos);if(!grid.inside())continue;
        if(area_effect)hb_target_area_cell(pos.x,pos.y,effect.possible);
        const int index=grid.visible_monster();
        if(index&&(flags&PROJECT_KILL))hb_target_affected(index,pos.x,pos.y,true);
    }
    if(!effect.arg2&&!beam&&!area_effect&&!selected_passed){
        // The faint continuation shows the requested point, not an extra hit.
        hb_target_path_begin(NATIVE_PATH_GUIDE);hb_target_path_cell(impact.x,impact.y);hb_target_path_cell(target.x,target.y);
    }
    stochastic|=effect.possible!=0;
    return true;
}
