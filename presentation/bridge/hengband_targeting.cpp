#include "bridge/hengband_targeting.h"
#include "bridge/targeting_geometry.h"
#include "core/speed-table.h"
#include "effect/attribute-types.h"
#include "effect/effect-characteristics.h"
#include "effect/spells-effect-util.h"
#include "floor/geometry.h"
#include "game-option/input-options.h"
#include "game-option/keymap-directory-getter.h"
#include "core/stuff-handler.h"
#include "io/input-key-acceptor.h"
#include "locale/language-switcher.h"
#include "monster/monster-describer.h"
#include "system/floor/floor-info.h"
#include "system/grid-type-definition.h"
#include "system/monster-entity.h"
#include "system/player-type-definition.h"
#include "system/terrain/terrain-definition.h"
#include "target/target-preparation.h"
#include "term/screen-processor.h"
#include "tracking/lore-tracker.h"
#include "view/display-lore.h"
#include "term/z-term.h"
#include "timed-effect/timed-effects.h"
#include "bridge/hengband_source_version.h"
#include "util/bit-flags-calculator.h"
#include "util/finalizer.h"
#include "spell/spells-execution.h"
#include "spell/spells-util.h"
#include "realm/realm-types.h"
#include "term/gameterm.h"
#include "system/item/item-entity.h"
#include <algorithm>
#include <array>
#include <cstdlib>

#if __has_include("target/heng_target_geometry.generated.inl")
#define HB_HENG_TARGETING_GEOMETRY 1
#include "bridge/hengband_targeting_geometry.inl"
#else
#define HB_HENG_TARGETING_GEOMETRY 0
#endif

namespace {
using RngSnapshot=std::array<uint32_t,xso::rng32::word_count()>;
RngSnapshot rng_snapshot(){RngSnapshot state{};AngbandSystem::get_instance().get_rng().get_state(state.begin());return state;}
struct Context {PlayerType *player;int mode;bool point;int range;};
bool eligible(Context &ctx,int index) {
    auto &floor=*ctx.player->current_floor_ptr;
    return index>0 && index<floor.m_max && target_able(ctx.player,static_cast<MONSTER_IDX>(index));
}
// Original target_set_prepare(TARGET_KILL): the candidate list is the box of max_range around the player,
// not the range of the spell. Order is calc_double_distance, then scan order (y then x) for ties.
bool in_candidate_box(Context &ctx,const Pos2D &pos) {
    const auto origin=ctx.player->get_position();const auto max_range=AngbandSystem::get_instance().get_max_range();
    return std::abs(pos.x-origin.x)<=max_range&&std::abs(pos.y-origin.y)<=max_range;
}
int double_distance(const Pos2D &origin,const Pos2D &pos) {
    const int dx=std::abs(pos.x-origin.x),dy=std::abs(pos.y-origin.y);
    return dx>dy?2*dx+dy:2*dy+dx;
}
void candidates(void *data) {
    const auto rng=rng_snapshot();
    auto &ctx=*static_cast<Context *>(data);auto &floor=*ctx.player->current_floor_ptr;
    for(int index=floor.m_max-1;index>0;--index) {
        if(!eligible(ctx,index))continue;
        const auto &monster=floor.m_list[index];
        if(hb_target_profile()->kind==NATIVE_TARGET_SUPPORT) {
            if(!monster.is_pet()&&!monster.is_friendly()&&!monster.is_riding())continue;
        } else if(!target_pet&&monster.is_pet())continue; // original: only pets are dropped, friendly monsters stay
        const auto pos=monster.get_position();
        if(!in_candidate_box(ctx,pos))continue;
        const auto name=monster_desc(ctx.player,monster,0);
        hb_target_candidate(index,pos.x,pos.y,double_distance(ctx.player->get_position(),pos),0,pos.y*floor.width+pos.x,name.c_str(),true);
    }
    hb_target_trace_invariant("heng-candidates-rng",rng==rng_snapshot());
}
// Monster recall ('r') while choosing a target. The original describe_grid_monster() answers 'r' on a
// monster by showing its recall (screen_roff()); the native toolbar has no recall button outside of the
// "look" mode, so a raw 'r' was ignored. It is offered as an "extra" operation (key 'r', also reachable
// from the Actions list). The recall screen is the core's own screen, so the targeting state is closed
// while it runs.
bool recall_ready=false;
int recall_index(Context &ctx,const native_target_choice *choice) {
    if(!choice||choice->id<=0)return 0;
    const auto &floor=*ctx.player->current_floor_ptr;
    if(choice->id>=floor.m_max)return 0;
    // `preview()` already leaves `id` at 0 or -1 for an unseen or hallucinated creature.
    return floor.m_list[choice->id].ml?choice->id:0;
}
void emit_extras(native_target_choice *choice,Context &ctx) {
    // Direction-only prompts (tunnel, open ...) have no creature cursor.
    if(ctx.mode){recall_ready=false;return;}
    recall_ready=recall_index(ctx,choice)!=0;
    hb_target_extra('r',_("モンスターの思い出","Monster Recall"),recall_ready);
}
int extra_screen(int key) {return key=='r'&&recall_ready;}
int extra(int key,native_target_choice *choice,void *data) {
    if(key!='r')return NATIVE_EXTRA_UNKNOWN;
    auto &ctx=*static_cast<Context *>(data);
    const int index=recall_index(ctx,choice);
    if(!index)return NATIVE_EXTRA_NONE;
    const auto &monster=ctx.player->current_floor_ptr->m_list[index];
    LoreTracker::get_instance().set_trackee(monster.ap_r_idx);
    health_track(ctx.player,static_cast<short>(index));
    handle_stuff(ctx.player);
    inkey_base=false;
    screen_save();
    screen_roff(ctx.player,monster.ap_r_idx,MONSTER_LORE_NORMAL);
    term_addstr(-1,TERM_WHITE,std::string("  [r]"));
    (void)inkey();
    screen_load();
    return NATIVE_EXTRA_NONE;
}
void preview(native_target_choice *choice,void *data) {
    const auto rng=rng_snapshot();const auto original_length=project_length;
    auto &ctx=*static_cast<Context *>(data);auto &floor=*ctx.player->current_floor_ptr;
    const auto origin=ctx.player->get_position();const auto *profile=hb_target_profile();
    bool valid=true,stochastic=false;int reason=NATIVE_TARGET_OK;std::string name;
    const Pos2D selected{choice->y,choice->x};const int d=Grid::calc_distance(origin,selected);
    if(choice->x<0||choice->y<0||choice->x>=floor.width||choice->y>=floor.height){valid=false;reason=NATIVE_TARGET_OUTSIDE;}
    else {
        int index=floor.grid_array[choice->y][choice->x].m_idx;
#if HB_HENGBAND_SOURCE_3025
        if(index && (!floor.m_list[index].ml||ctx.player->effects()->hallucination().is_active()))index=0;
#else
        if(index && (!floor.m_list[index].ml||ctx.player->effects()->hallucination().is_hallucinated()))index=0;
#endif
        choice->id=index?index:(d==0?-1:0);
        if(index)name=monster_desc(ctx.player,floor.m_list[index],0);
        // Free targeting (original 't' on any grid) may pick any grid: a creature that cannot be a creature target is a plain point.
        if(!ctx.mode&&index&&!eligible(ctx,index))choice->id=0;
        if(d==0&&ctx.mode==1){valid=false;reason=NATIVE_TARGET_SELF_FORBIDDEN;}
    }
    if(hb_target_preview_enabled()&&valid&&!profile->supported)reason=NATIVE_TARGET_PROFILE_MISSING;
    if(!d)for(int i=0;i<profile->count;++i)if(profile->effects[i].shape==NATIVE_SHAPE_SHOT){valid=false;reason=NATIVE_TARGET_SELF_FORBIDDEN;}
    choice->direction=ctx.mode?presentation::targeting_nearest_direction({origin.x,origin.y},{choice->x,choice->y}):5;
    if(hb_target_preview_enabled()&&valid&&profile->supported&&profile->effect_known)for(int i=0;i<profile->count;++i){
#if HB_HENG_TARGETING_GEOMETRY
        if(!heng_target_effect(*ctx.player,*choice,profile->effects[i],stochastic))
#endif
        {reason=NATIVE_TARGET_PROFILE_MISSING;break;}
    }
    hb_target_status(valid,reason,d,name.c_str(),stochastic,profile->effect_known);
    emit_extras(choice,ctx);
    hb_target_trace_invariant("heng-preview-rng",rng==rng_snapshot());
    hb_target_trace_invariant("heng-preview-range",original_length==project_length);
}
int read_key(void *) {inkey_base=true;return static_cast<unsigned char>(inkey(true));}
int direction_key(int key,void *) {return get_keymap_dir(static_cast<char>(key)).dir();}
void refresh(void *) {term_fresh();}
bool choose(PlayerType *player,int mode,bool point,native_target_choice &choice) {
    Context ctx{player,mode,point,0};native_target_request request{};
    const auto origin=player->get_position();const auto &floor=*player->current_floor_ptr;
    const auto *profile=hb_target_profile();const auto previous=Target::get_last_target();
    for(int i=0;i<profile->count;++i)ctx.range=std::max(ctx.range,profile->effects[i].range>0?profile->effects[i].range:(project_length?project_length:AngbandSystem::get_instance().get_max_range()));
    request.width=floor.width;request.height=floor.height;request.source_x=origin.x;request.source_y=origin.y;
    request.mode=mode?NATIVE_TARGET_DIRECTION8:NATIVE_TARGET_POINT;request.kind=profile->kind;request.label=profile->label;
    if(const auto pos=previous.get_position();pos && (!previous.get_m_idx()||eligible(ctx,*previous.get_m_idx()))
        &&pos->x>=0&&pos->y>=0&&pos->x<floor.width&&pos->y<floor.height) {
        request.previous_valid=true;request.previous_x=pos->x;request.previous_y=pos->y;
        request.previous_id=previous.get_m_idx().value_or(0);
    }
    const native_target_hooks hooks{candidates,preview,read_key,direction_key,refresh,nullptr,extra,extra_screen};
    return hb_target_choose(&request,&hooks,&ctx,&choice)!=0;
}
Target as_target(PlayerType *player,const native_target_choice &choice) {
    return choice.track_creature&&choice.id>0
        ? Target::create_monster_target(player,static_cast<MONSTER_IDX>(choice.id))
        : Target::create_grid_target(player,{choice.y,choice.x});
}
}
bool hb_heng_mapped_direction() {
    return inkey_next&&*inkey_next&&!inkey_xtra&&get_keymap_dir(*inkey_next).dir()!=0;
}
Direction hb_heng_choose_direction(PlayerType *player,int mode) {
    native_target_choice choice{};if(!choose(player,mode,false,choice))return Direction::none();
    if(mode)return Direction(choice.direction);
    const auto target=as_target(player,choice);Target::set_last_target(target);return Direction::targetting(target);
}
Target hb_heng_choose_target(PlayerType *player) {
    native_target_choice choice{};if(!choose(player,0,false,choice))return Target::none();
    const auto target=as_target(player,choice);Target::set_last_target(target);return target;
}
tl::optional<Pos2D> hb_heng_choose_point(PlayerType *player) {
    native_target_choice choice{};if(!choose(player,0,true,choice))return tl::nullopt;
    return Pos2D{choice.y,choice.x};
}
namespace {
struct MenuBook {PlayerType *player;int realm,spell;};
void preview_fixed(PlayerType *player) {
    Context context{player,0,true,0};native_target_request request{};native_target_hooks hooks{};
    const auto &floor=*player->current_floor_ptr;const auto source=player->get_position();const auto *profile=hb_target_profile();
    request.width=floor.width;request.height=floor.height;request.source_x=source.x;request.source_y=source.y;
    request.mode=NATIVE_TARGET_FIXED;request.kind=profile->kind;request.label=profile->label;hooks.preview=preview;
    hb_target_preview_fixed(&request,&hooks,&context);
}
void build_menu_book(void *data) {
    const auto &entry=*static_cast<MenuBook *>(data);
    const auto rng=rng_snapshot();const auto length=project_length;
    if(hb_target_has_plan(hb_target_plan_key(exe_spell))){
        exe_spell(entry.player,static_cast<RealmType>(entry.realm),static_cast<SPELL_IDX>(entry.spell),static_cast<SpellProcessType>(HB_TARGET_PLAN_MODE));
        preview_fixed(entry.player);
    }
    hb_target_trace_invariant("heng-menu-plan-rng",rng==rng_snapshot());
    hb_target_trace_invariant("heng-menu-plan-range",length==project_length);
}
}
void hb_target_heng_item_preview(void *actor,int action,const void *object) {
    const auto rng=rng_snapshot();const auto length=project_length;
    auto *player=static_cast<PlayerType *>(actor);const auto &item=*static_cast<const ItemEntity *>(object);
    if(player&&item.is_valid()&&item.is_aware()){
        if(action=='u'&&item.bi_key.tval()==ItemKindType::STAFF)hb_target_heng_staff_profile(player,*item.bi_key.sval(),0,0);
        if(action=='z'&&item.bi_key.tval()==ItemKindType::ROD)hb_target_heng_rod_self_profile(player,*item.bi_key.sval(),0,0);
        preview_fixed(player);
    }
    hb_target_trace_invariant("heng-item-plan-rng",rng==rng_snapshot());
    hb_target_trace_invariant("heng-item-plan-range",length==project_length);
}
void hb_heng_menu_book_row(PlayerType *player,int key,int row,int column,const char *rendered,const char *name,int realm,int spell,bool can_cast,int selected) {
    const MenuBook entry{player,realm,spell};
    if(game_term==angband_terms[0]){
        hb_target_menu_row(key,row,column,rendered,can_cast?build_menu_book:nullptr,&entry,sizeof(entry));
        hb_target_menu_row_text(name,rendered,selected);
    }
}
