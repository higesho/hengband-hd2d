#include "bridge/native_targeting_runtime.h"
#include "bridge/native_targeting_api.h"
#include "bridge/attack_fx_recorder.h"
#include "bridge/targeting_geometry.h"
#include "bridge/modern_content.h"
#include "frame/game_frame.h"
#include <map>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <set>
#include <memory>
#include <cstring>
#include <unordered_set>

namespace {
presentation::NativeTargeting bridge;
std::string language="en";
std::function<std::string(const char *)> converter;
std::string site,label;
std::vector<native_target_effect> effects;
native_target_profile profile{};
TargetingState staging;
std::vector<presentation::NativeTargetPriority> priorities;
std::map<std::pair<int,int>,bool> coverage;
bool emitting{};
bool preview_overflow{},candidate_overflow{},path_open{};
bool extras_enabled{}; // The running request has an `extra` hook (Angband look / target).
std::string notice_text; // Why the last extra operation did nothing (untranslated).
int preview_reason{NATIVE_TARGET_OK};
size_t path_cells{};
TargetingPath pending_path;
std::set<std::vector<int>> path_keys;
TargetingState menu_preview;
struct MenuPreviewRow {
    int key{},row{},column{},selected{-1};
    std::string rendered,name,detail,item_id;
    native_target_menu_builder builder{};
    std::shared_ptr<void> context;
};
struct MenuPreviewScope {
    int id{},generation{},cached_row{-1},cached_generation{-1};
    int x{},y{},width{},height{};
    bool selecting_action{true},legacy_columns{true};
    int item_action{},item_depth{};
    native_target_item_builder item_builder{};
    void *item_actor{};
    std::vector<MenuPreviewRow> rows;
    TargetingState cached;
};
std::vector<MenuPreviewScope> menu_scopes;
std::unordered_set<native_target_plan_symbol> plan_symbols;
int menu_scope_serial{};

void finish_path() {
    if(!path_open)return;
    path_open=false;
    if(pending_path.points.empty())return;
    std::vector<int> key;key.reserve(pending_path.points.size()*2+1);
    key.push_back(pending_path.guide?2:(pending_path.possible?1:0));
    for(const auto &point:pending_path.points){key.push_back(point.x);key.push_back(point.y);}
    if(!path_keys.insert(std::move(key)).second)return;
    if(staging.paths.size()>=256||path_cells+pending_path.points.size()>65536){preview_overflow=true;return;}
    path_cells+=pending_path.points.size();staging.paths.push_back(std::move(pending_path));
}

std::string text(const char *s){if(!s)return {};return converter?converter(s):std::string(s);}
std::string reason_text(int reason) {
    const bool ja=language=="ja";
    switch(reason) {
    case NATIVE_TARGET_OK:return {};
    case NATIVE_TARGET_OUTSIDE:return ja?reinterpret_cast<const char*>(u8"マップの外です。"):"Outside the map.";
    case NATIVE_TARGET_SELF_FORBIDDEN:return ja?reinterpret_cast<const char*>(u8"自分の位置には指定できません。"):"You cannot select your own position.";
    case NATIVE_TARGET_CREATURE_REQUIRED:return ja?reinterpret_cast<const char*>(u8"対象となる相手を選んでください。"):"Select an eligible creature.";
    case NATIVE_TARGET_UNAVAILABLE:return ja?reinterpret_cast<const char*>(u8"この行動では指定できない対象です。"):"This target is not eligible for this action.";
    case NATIVE_TARGET_OUT_OF_RANGE:return ja?reinterpret_cast<const char*>(u8"射程外です。"):"Out of range.";
    case NATIVE_TARGET_EFFECT_UNKNOWN:return ja?reinterpret_cast<const char*>(u8"効果が未判明です。"):"The effect is not identified.";
    case NATIVE_TARGET_PREVIEW_OVERFLOW:return ja?reinterpret_cast<const char*>(u8"照準情報が表示上限を超えました。"):"Targeting preview exceeds the display limit.";
    case NATIVE_TARGET_PROFILE_MISSING:return ja?reinterpret_cast<const char*>(u8"この行動の射線・範囲表示は未対応です。"):"Trajectory and area preview is unavailable for this action.";
    default:return ja?reinterpret_cast<const char*>(u8"この行動の照準情報が登録されていません。"):"Targeting information is not registered for this action.";
    }
}
bool inside(int x,int y){return x>=0&&y>=0&&x<staging.width&&y<staging.height;}
void prepare_preview(const native_target_hooks &hooks,void *data,native_target_choice &choice,bool selected_marker=true) {
    staging.selected={choice.x,choice.y};staging.selected_id=choice.id;
    staging.paths.clear();staging.area.clear();staging.markers.clear();coverage.clear();
    staging.reason.clear();staging.target_name.clear();staging.can_confirm=false;
    staging.stochastic=false;staging.effect_known=profile.effect_known!=0;staging.extras.clear();staging.info.clear();
    preview_overflow=candidate_overflow;path_cells=0;path_open=false;pending_path={};path_keys.clear();
    preview_reason=NATIVE_TARGET_OK;
    emitting=true;hooks.preview(&choice,data);finish_path();emitting=false;
    if(preview_reason==NATIVE_TARGET_PROFILE_MISSING) {
        // The native target rules still own confirmation. An unavailable
        // model must neither invent a partial footprint nor disable the action.
        staging.paths.clear();staging.area.clear();staging.markers.clear();coverage.clear();
        staging.stochastic=false;
    }
    if(!hb_target_preview_enabled()) {
        staging.paths.clear();staging.area.clear();staging.markers.clear();coverage.clear();
        staging.stochastic=false;preview_overflow=candidate_overflow;
    }
    if(preview_overflow) {
        staging.paths.clear();coverage.clear();staging.markers.clear();
        staging.can_confirm=false;staging.reason=reason_text(NATIVE_TARGET_PREVIEW_OVERFLOW);
    }
    choice.can_confirm=staging.can_confirm?1:0;
    staging.selected={choice.x,choice.y};staging.selected_id=choice.id;staging.direction=choice.direction;
    for(const auto &[point,possible]:coverage)staging.area.push_back({{point.first,point.second},possible});
    if(!selected_marker)return;
    auto selected=std::find_if(staging.markers.begin(),staging.markers.end(),[&](const auto &m){return m.point==staging.selected&&m.id==choice.id;});
    if(selected!=staging.markers.end()){selected->selected=true;selected->valid=staging.can_confirm;}
    else {
        bool reaches=staging.kind=="neutral"||(!hb_target_preview_enabled()&&staging.can_confirm);
        // Crossing a creature does not imply that this action affects it.
        // Creature effects must be emitted explicitly by the native model.
        if(!choice.id) {
            reaches|=coverage.count({choice.x,choice.y})!=0;
            for(const auto &path:staging.paths)if(!path.guide&&std::find(path.points.begin(),path.points.end(),staging.selected)!=path.points.end())reaches=true;
        }
        // AH-26 の直し。`choice.id!=0` だと、モンスターではないアイテム・地形の
        // 目印（an_look_candidates() が付ける、モンスター id と衝突しない合成の
        // 負の id）まで creature=true になり、敵の高さに浮いていた。
        // 実在のモンスター（正の id）と自分（-1）だけを creature 扱いにする。
        staging.markers.push_back({choice.id,staging.selected,choice.id>0||choice.id==-1,true,reaches,staging.can_confirm});
    }
}
std::string compact_menu_text(std::string value) {
    value.erase(std::remove_if(value.begin(),value.end(),[](unsigned char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}),value.end());
    return value;
}
bool matches_menu_row(const GameFrame &frame,const MenuPreviewRow &row) {
    if(!row.item_id.empty())return std::any_of(frame.modern.rows.begin(),frame.modern.rows.end(),[&](const auto &item){return item.id==row.item_id;});
    const auto expected=compact_menu_text(row.rendered);
    return !expected.empty()&&std::any_of(frame.menu_term_lines.begin(),frame.menu_term_lines.end(),[&](const auto &line){
        return line.source_row==row.row&&compact_menu_text(line.text_utf8).find(expected)!=std::string::npos;
    });
}
}

namespace presentation {
NativeTargeting &native_targeting(){return bridge;}
void configure_native_targeting(bool enabled,const std::string &lang,std::function<std::string(const char *)> convert) {
    bridge.set_enabled(enabled);language=lang=="ja"?"ja":"en";if(convert)converter=std::move(convert);
    if(!enabled)menu_preview={};
}
void apply_native_targeting_frame(GameFrame &frame) {
    if(!bridge.active())return;
    frame.targeting=bridge.state();frame.menu_open=false;frame.menu_over_map=false;
    frame.awaiting_command=false;frame.text_input_active=false;frame.numeric={};frame.prompt={};frame.modern={};
    frame.menu_term_lines.clear();frame.menu_choices.clear();frame.menu_core_cursor={};frame.menu_core_cursors.clear();frame.command_menu_keys.clear();
    frame.target_gx=-1;frame.target_gy=-1;
}
void apply_native_targeting_menu_frame(GameFrame &frame) {
    if(frame.targeting.preview_only)frame.targeting={};
    if(bridge.active()||!bridge.enabled()||!hb_target_preview_enabled()||menu_scopes.empty()||frame.pre_game_menu||frame.awaiting_command)return;
    auto &window=frame.modern;window.can_preview=false;
    if(!menu_scopes.back().selecting_action)return;
    if(!window.version||(window.stage!="choices"&&window.stage!="cursor"))return;
    int native_row=-1,key=0;std::string item_id;
    if(frame.menu_core_cursor.line_index>=0&&frame.menu_core_cursor.line_index<int(frame.menu_term_lines.size()))native_row=frame.menu_term_lines[frame.menu_core_cursor.line_index].source_row;
    if(window.cursor>=0&&window.cursor<int(window.rows.size())){key=window.rows[window.cursor].key;item_id=window.rows[window.cursor].id;}
    if(!window.preview_target.empty())for(const auto &row:window.rows)if(row.id==window.preview_target){key=row.key;item_id=row.id;}
    auto &scope=menu_scopes.back();int selected=-1;
    for(size_t i=0;i<scope.rows.size();++i){
        const auto &row=scope.rows[i];
        const bool matches=matches_menu_row(frame,row);
        if(matches){
            window.can_preview=true;
            if(!row.item_id.empty()?row.item_id==item_id:(native_row>=0?row.row==native_row:row.key==key))selected=int(i);
        }
    }
    if(selected<0)return;
    if(scope.cached_row!=selected||scope.cached_generation!=scope.generation){
        menu_preview={};
        const auto row=scope.rows[selected];
        if(row.builder)row.builder(row.context.get());
        scope.cached=menu_preview;scope.cached_row=selected;scope.cached_generation=scope.generation;
        hb_target_profile_begin("");
    }
    if(scope.cached.preview_only){
        frame.targeting=scope.cached;frame.menu_over_map=true;
        if(scope.cached.stochastic){
            const char *note=language=="ja"?reinterpret_cast<const char*>(u8"範囲にランダム性があります"):"Coverage can vary";
            if(frame.modern.subtitle.find(note)==std::string::npos){
                if(!frame.modern.subtitle.empty())frame.modern.subtitle+="  ";
                frame.modern.subtitle+=note;
            }
        }
    }
}
void prepare_native_targeting_menu_frame(GameFrame &frame) {
    if(!bridge.enabled()||bridge.active()||menu_scopes.empty()||!hb_target_preview_enabled()||frame.pre_game_menu)return;
    const auto &scope=menu_scopes.back();
    if(!scope.selecting_action||scope.width<=0||scope.height<=0)return;
    if(!std::any_of(scope.rows.begin(),scope.rows.end(),[&](const auto &row){return matches_menu_row(frame,row);}))return;
    ModernColumnScope columns(scope.legacy_columns?"legacy":"unicode");
    for(const auto &row:scope.rows)if(row.selected==1&&matches_menu_row(frame,row)){
        const auto line=std::find_if(frame.menu_term_lines.begin(),frame.menu_term_lines.end(),[&](const auto &entry){return entry.source_row==row.row;});
        if(line==frame.menu_term_lines.end())continue;
        MenuChoice cursor;cursor.line_index=int(line-frame.menu_term_lines.begin());cursor.span_begin=int(modern_column_byte(line->text_utf8,row.column));
        cursor.span_len=int(line->text_utf8.size())-cursor.span_begin;cursor.key=13;cursor.selected=true;
        frame.menu_core_cursor=cursor;frame.menu_core_cursors.assign(1,cursor);
    }
    for(auto &line:frame.menu_term_lines){
        if(line.source_row==0)continue;
        if(line.source_row<scope.y||line.source_row>=scope.y+scope.height){line.text_utf8.clear();line.color_spans.clear();line.highlight_begin=0;line.highlight_len=0;continue;}
        const auto stop=modern_column_byte(line.text_utf8,scope.x+scope.width);
        line.text_utf8.resize(stop);
        line.color_spans.erase(std::remove_if(line.color_spans.begin(),line.color_spans.end(),[&](const auto &span){return span.begin>=int(stop);}),line.color_spans.end());
        for(auto &span:line.color_spans)span.len=std::min(span.len,int(stop)-span.begin);
        line.highlight_len=std::max(0,std::min(line.highlight_len,int(stop)-line.highlight_begin));
    }
    /*
     * 消すのは「上で消した行（窓の外）を指す札」だけ。**行に結び付かない札（`line_index < 0`）は残す**
     * （2026-10-07）——コアのアダプタが足す補助の札（幻想蛮怒の「一覧を見る `*`」・Frox の `?` 閲覧・`!` 名札など）
     * である。以前はこれも消していて、標的の下見のある呪文・特殊能力の一覧では、パッドやタッチで
     * 一覧を出す・説明を見る手段が無くなった（Frox のアーチャーの `U` で見つかった）。
     */
    frame.menu_choices.erase(std::remove_if(frame.menu_choices.begin(),frame.menu_choices.end(),[&](const auto &choice){
        if(choice.line_index<0)return false;
        return choice.line_index>=int(frame.menu_term_lines.size())||frame.menu_term_lines[choice.line_index].text_utf8.empty();
    }),frame.menu_choices.end());
    // Cursor-based spell lists don't print letter choices. The native list
    // supplies every row here, including entries that cannot be selected.
    if(frame.menu_choices.empty())for(const auto &row:scope.rows){
        if(!matches_menu_row(frame,row))continue;
        const auto line=std::find_if(frame.menu_term_lines.begin(),frame.menu_term_lines.end(),[&](const auto &entry){return entry.source_row==row.row;});
        if(line==frame.menu_term_lines.end())continue;
        MenuChoice choice;choice.line_index=int(line-frame.menu_term_lines.begin());
        choice.span_begin=int(modern_column_byte(line->text_utf8,row.column));choice.span_len=int(line->text_utf8.size())-choice.span_begin;
        choice.selected=choice.line_index==frame.menu_core_cursor.line_index;
        choice.key=frame.menu_core_cursor.line_index>=0?13:row.key;choice.available=row.builder!=nullptr;
        choice.label_utf8=row.name.empty()?row.rendered:row.name;choice.description_utf8=row.detail;
        frame.menu_choices.push_back(std::move(choice));
    }
}
}
extern "C" {
int hb_target_menu_begin(void){
    if(!bridge.enabled()||!hb_target_preview_enabled())return 0;
    if(menu_scope_serial>=1900000000)menu_scope_serial=0;
    MenuPreviewScope scope;scope.id=++menu_scope_serial;menu_scopes.push_back(std::move(scope));return menu_scope_serial;
}
int hb_target_item_menu_begin(int action,native_target_item_builder builder,void *actor){
    const int id=hb_target_menu_begin();
    if(id){auto &scope=menu_scopes.back();scope.item_action=action;scope.item_builder=builder;scope.item_actor=actor;}
    return id;
}
void hb_target_item_menu_display(int enter){
    if(menu_scopes.empty()||!menu_scopes.back().item_builder)return;
    auto &scope=menu_scopes.back();
    if(enter)++scope.item_depth;
    else if(scope.item_depth>0&&!--scope.item_depth){
        hb_target_menu_clear_rows();scope.selecting_action=false;scope.item_builder=nullptr;
    }
}
void hb_target_item_menu_clear(void){
    if(!menu_scopes.empty()&&menu_scopes.back().item_builder&&menu_scopes.back().item_depth>0)hb_target_menu_clear_rows();
}
void hb_target_item_menu_row(const char *id,const void *object){
    if(!id||!*id||!object||menu_scopes.empty())return;
    auto &scope=menu_scopes.back();
    if(!scope.item_builder||scope.item_depth<=0)return;
    struct Item {native_target_item_builder builder;void *actor;const void *object;int action;};
    const auto found=std::find_if(scope.rows.begin(),scope.rows.end(),[&](const auto &row){return row.item_id==id;});
    if(found!=scope.rows.end()){
        auto &entry=*static_cast<Item *>(found->context.get());
        if(entry.object!=object){entry.object=object;++scope.generation;}
        return;
    }
    if(scope.rows.size()>=1024)return;
    const Item entry{scope.item_builder,scope.item_actor,object,scope.item_action};
    hb_target_menu_row(0,0,0,"",[](void *data){const auto &item=*static_cast<Item *>(data);hb_target_profile_begin("item-menu");item.builder(item.actor,item.action,item.object);},&entry,sizeof(entry));
    if(!scope.rows.empty())scope.rows.back().item_id=id;
}
void hb_target_menu_end(int scope){
    if(scope<=0)return;
    const auto found=std::find_if(menu_scopes.begin(),menu_scopes.end(),[&](const auto &entry){return entry.id==scope;});
    if(found!=menu_scopes.end())menu_scopes.erase(found,menu_scopes.end());
    menu_preview={};
}
void hb_target_menu_clear_rows(void){
    if(menu_scopes.empty())return;
    auto &scope=menu_scopes.back();scope.rows.clear();scope.cached={};scope.cached_row=-1;++scope.generation;
}
void hb_target_menu_action(int selecting_action){if(!menu_scopes.empty())menu_scopes.back().selecting_action=selecting_action!=0;}
void hb_target_menu_geometry(int x,int y,int width,int height,int legacy_columns){
    if(menu_scopes.empty())return;auto &scope=menu_scopes.back();
    scope.x=x;scope.y=y;scope.width=width;scope.height=height;scope.legacy_columns=legacy_columns!=0;
}
void hb_target_menu_row(int key,int row,int column,const char *rendered,native_target_menu_builder builder,const void *context,unsigned int size){
    if(menu_scopes.empty()||!rendered||size>4096||(size&&!context))return;
    auto &scope=menu_scopes.back();if(scope.rows.size()>=1024)return;
    MenuPreviewRow entry;entry.key=key;entry.row=row;entry.column=column;entry.rendered=text(rendered);entry.builder=builder;
    if(size){
        void *copy=std::malloc(size);if(!copy)return;
        std::memcpy(copy,context,size);entry.context=std::shared_ptr<void>(copy,std::free);
    }
    scope.rows.push_back(std::move(entry));
}
void hb_target_menu_row_text(const char *name,const char *detail,int selected){
    if(menu_scopes.empty()||menu_scopes.back().rows.empty())return;
    auto &row=menu_scopes.back().rows.back();row.name=text(name);row.detail=text(detail);row.selected=selected;
}
void hb_target_mark_plan(native_target_plan_symbol symbol){if(symbol)plan_symbols.insert(symbol);}
int hb_target_has_plan(native_target_plan_symbol symbol){return plan_symbols.contains(symbol)?1:0;}
void hb_target_preview_fixed(const native_target_request *request,const native_target_hooks *hooks,void *data){
    menu_preview={};extras_enabled=false;
    if(!request||!hooks||!hooks->preview||bridge.active()||menu_scopes.empty()||!profile.supported||!profile.effect_known||!hb_target_preview_enabled())return;
    staging={};staging.width=request->width;staging.height=request->height;staging.source={request->source_x,request->source_y};
    if(!inside(request->source_x,request->source_y)||request->kind<0||request->kind>2)return;
    constexpr const char *kinds[]={"neutral","attack","support"};
    staging.kind=kinds[request->kind];staging.mode="fixed";staging.label=text(request->label);
    staging.scope=menu_scopes.back().id;staging.revision=menu_scopes.back().generation+1;
    candidate_overflow=false;native_target_choice choice{request->source_x,request->source_y,0,5,0,0};
    prepare_preview(*hooks,data,choice,false);
    if(!staging.can_confirm||preview_overflow||(staging.area.empty()&&staging.markers.empty()))return;
    staging.active=false;staging.preview_only=true;staging.can_confirm=false;staging.candidates.clear();
    for(auto &marker:staging.markers)marker.selected=false;
    menu_preview=staging;
}
void hb_target_trace_invariant(const char *name,int unchanged) {
    static const bool enabled=std::getenv("HD2D_TARGETING_TRACE")!=nullptr;
    if(enabled)std::fprintf(stderr,"[targeting-invariant] %s %d\n",name,unchanged);
}
void hb_target_trace_area(int element,int x,int y) {
    static const bool enabled=std::getenv("HD2D_TARGETING_TRACE")!=nullptr;
    if(enabled)std::fprintf(stderr,"[targeting-area] %d %d %d\n",element,x,y);
}
// 攻撃の視覚効果の記録。積むだけで、原作の状態には触れない。
void hb_attack_fx_projection(int src_idx,int src_y,int src_x,int impact_y,int impact_x,int attr,int rad,int flags,int blind,int breath,
    const int *path,int fx_path_n,const int *area,int fx_area_n) {
    attack_fx::RawProjection raw;
    raw.src_idx=src_idx;raw.src_y=src_y;raw.src_x=src_x;raw.impact_y=impact_y;raw.impact_x=impact_x;
    raw.attr_id=attr;raw.rad=rad;raw.flags=static_cast<uint32_t>(flags);raw.blind=blind!=0;raw.breath=breath!=0;
    if(path)for(int i=0;i<fx_path_n&&raw.path.size()<attack_fx::kMaxCells;++i){
        FxCell cell;cell.y=static_cast<int16_t>(path[i*2]);cell.x=static_cast<int16_t>(path[i*2+1]);raw.path.push_back(cell);
    }
    if(area)for(int i=0;i<fx_area_n&&raw.area.size()<attack_fx::kMaxCells;++i){
        FxCell cell;cell.dist=static_cast<uint8_t>(area[i*3]<0?0:(area[i*3]>255?255:area[i*3]));
        cell.y=static_cast<int16_t>(area[i*3+1]);cell.x=static_cast<int16_t>(area[i*3+2]);raw.area.push_back(cell);
    }
    attack_fx::Recorder::instance().add_projection(std::move(raw));
}
void hb_attack_fx_shot(int cur_dis,int x,int y,int prev_y,int prev_x,int item_id,int tval,int brand){
    attack_fx::Recorder::instance().shot_step(cur_dis,x,y,prev_y,prev_x,item_id,tval,brand);
}
void hb_attack_fx_throw(int cur_dis,int x,int y,int prev_y,int prev_x,int item_id,int returns){
    attack_fx::Recorder::instance().throw_step(cur_dis,x,y,prev_y,prev_x,item_id,returns!=0);
}
void hb_attack_fx_hit(void){attack_fx::Recorder::instance().mark_hit();}
void hb_attack_fx_throw_return(void){attack_fx::Recorder::instance().throw_returns();}
int hb_target_enabled(void){return bridge.enabled()?1:0;}
int hb_target_preview_enabled(void){const char *value=std::getenv("HD2D_TARGETING_PREVIEW");return !value||std::string_view(value)!="0";}
void hb_target_trace_hit(int hit,int x,int y){if(std::getenv("HD2D_TARGETING_TRACE"))std::fprintf(stderr,"[targeting-hit] %d %d %d\n",hit,x,y);}
int hb_target_active(void){return bridge.active()?1:0;}
void hb_target_profile_begin(const char *value){site=value?value:"";label.clear();effects.clear();profile={};profile.site=site.c_str();profile.effect_known=1;}
void hb_target_profile_label(const char *value){label=value?value:"";profile.label=label.c_str();}
void hb_target_profile_plain(int kind){profile.supported=1;profile.kind=kind;}
void hb_target_profile_effect(int shape,int element,int radius,int range,int flags,int possible,int arg1,int arg2) {
    effects.push_back({shape,element,radius,range,flags,possible,arg1,arg2});profile.supported=1;profile.kind=NATIVE_TARGET_ATTACK;
    profile.count=int(effects.size());profile.effects=effects.data();
}
void hb_target_profile_known(int known){profile.effect_known=known;}
const native_target_profile *hb_target_profile(void){return &profile;}
void hb_target_candidate(int id,int x,int y,int distance,int ticks,int order,const char *name,int eligible) {
    if(!id||!eligible||!inside(x,y))return;
    if(priorities.size()>=4096){candidate_overflow=true;return;}
    priorities.push_back({{id,{x,y},text(name)},distance,ticks,order,eligible!=0});
}
void hb_target_candidate_look(int id,int x,int y,int distance,int ticks,int order,const char *name,int eligible,int kind,const char *detail) {
    if(!id||!eligible||!inside(x,y))return;
    if(priorities.size()>=4096){candidate_overflow=true;return;}
    constexpr const char *kinds[]={"","monster","object","feature","trap","player"};
    const char *kind_str=(kind>=0&&kind<=5)?kinds[kind]:"";
    TargetingCandidate c{id,{x,y},text(name),kind_str,text(detail)};
    priorities.push_back({std::move(c),distance,ticks,order,true});
}
void hb_target_extra(int key,const char *text_label,int enabled) {
    // The parameter must not be called `label`: the file-scope `label` (line 20) would be hidden (C4459, an error under /WX).
    if(!emitting||!extras_enabled||key<1||key>126||(key>26&&key<33)||!text_label||staging.extras.size()>=32)return;
    staging.extras.push_back({key,text(text_label),enabled!=0});
}
void hb_target_notice(const char *value){notice_text=value?value:"";}
void hb_target_info_span(int color,const char *value) {
    if(!emitting||!value||!*value||color<0||color>15||staging.mode=="fixed")return;
    if(staging.info.empty())staging.info.emplace_back();
    auto &line=staging.info.back();
    if(line.spans.size()>=16)return;
    line.spans.push_back({text(value),color});
}
void hb_target_info_newline(void) {
    if(!emitting||staging.info.empty()||staging.info.back().spans.empty()||staging.info.size()>=8)return;
    staging.info.emplace_back();
}
void hb_target_status(int can_confirm,int reason,int distance,const char *name,int stochastic,int known) {
    preview_reason=reason;
    staging.can_confirm=can_confirm!=0;staging.reason=reason_text(reason);staging.distance=distance;
    staging.target_name=text(name);staging.stochastic=stochastic!=0;staging.effect_known=known!=0;
}
void hb_target_path_begin(int mode){
    if(!emitting)return;finish_path();
    pending_path={{},mode==NATIVE_PATH_POSSIBLE,mode==NATIVE_PATH_GUIDE};path_open=true;
}
void hb_target_path_cell(int x,int y){
    if(!emitting||!path_open||!inside(x,y))return;
    if(pending_path.points.size()>=4096){preview_overflow=true;return;}
    pending_path.points.push_back({x,y});
}
void hb_target_area_cell(int x,int y,int possible) {
    if(!emitting||!inside(x,y))return;
    const auto key=std::pair{x,y};const auto found=coverage.find(key);
    if(found==coverage.end()) {
        if(coverage.size()>=65536){preview_overflow=true;return;}
        coverage.emplace(key,possible!=0);
    }else found->second&=possible!=0;
}
void hb_target_affected(int id,int x,int y,int creature) {
    if(!emitting||!inside(x,y))return;
    const auto found=std::find_if(staging.markers.begin(),staging.markers.end(),[&](const auto &m){return m.id==id&&m.point==TargetingPoint{x,y};});
    if(found==staging.markers.end()) {
        if(staging.markers.size()>=4096){preview_overflow=true;return;}
        staging.markers.push_back({id,{x,y},creature!=0,false,true,true});
    }
}
int hb_target_choose(const native_target_request *request,const native_target_hooks *hooks,void *data,native_target_choice *out) {
    if(!request||!hooks||!hooks->preview||!hooks->read_key||!out||!bridge.enabled())return 0;
    struct ResetProfile { ~ResetProfile(){hb_target_profile_begin("");extras_enabled=false;} } reset_profile;
    out->ended_by_extra=0;extras_enabled=hooks->extra!=nullptr;notice_text.clear();
    staging={};staging.active=true;staging.width=request->width;staging.height=request->height;
    staging.source={request->source_x,request->source_y};staging.label=text(request->label);
    constexpr const char *modes[]={"point","direction8","creature","fixed","look"};constexpr const char *kinds[]={"neutral","attack","support"};
    if(request->mode<0||request->mode>4||request->kind<0||request->kind>2||!inside(request->source_x,request->source_y))return 0;
    staging.mode=modes[request->mode];staging.kind=kinds[request->kind];
    candidate_overflow=false;priorities.clear();if(hooks->candidates)hooks->candidates(data);
    staging.candidates=presentation::targeting_order_candidates(std::move(priorities));
    native_target_choice choice{request->source_x,request->source_y,0,0,0,0};
    if(request->mode!=NATIVE_TARGET_FIXED) {
        if(request->previous_valid&&inside(request->previous_x,request->previous_y))choice={request->previous_x,request->previous_y,request->previous_id,0,0,request->previous_id!=0};
        else if(!staging.candidates.empty()){const auto &c=staging.candidates.front();choice={c.point.x,c.point.y,c.id,0,0,1};}
    }
    prepare_preview(*hooks,data,choice);bridge.begin(staging);
    for(;;) {
        if(hooks->refresh)hooks->refresh(data);
        const int key=hooks->read_key(data);
        if(key<0||!bridge.enabled()){bridge.end();return 0;}
        presentation::NativeTargetingInput input;
        if(key==presentation::native_targeting_wake) {
            if(!bridge.take(input))continue;
        } else if(key==27)input.kind=presentation::NativeTargetingInput::Cancel;
        else if(key==13||key==' ')input.kind=presentation::NativeTargetingInput::Confirm;
        else {
            const int direction=hooks->direction_key?hooks->direction_key(key,data):0;
            if(direction>=1&&direction<=9&&direction!=5){input.kind=presentation::NativeTargetingInput::Move;input.value=direction;}
            //! 追加操作は印字できる字か Ctrl＋英字（Angband のローグライクのキー配置の「無視する」= ^D。2026-10-07）。
            else if(hooks->extra&&((key>32&&key<127)||(key>=1&&key<=26))){input.kind=presentation::NativeTargetingInput::Extra;input.value=key;}
            else continue;
        }
        if(input.kind==presentation::NativeTargetingInput::Cancel){bridge.end();return 0;}
        if(input.kind==presentation::NativeTargetingInput::Recall) {
            // "detail": the core answers via its own message channel (design
            // doc section 8.5.1, revised 2026-09-23). Neither the selection
            // nor the look session changes, so just run the hook and keep
            // waiting for the next key -- no prepare_preview/publish.
            if(hooks->recall)hooks->recall(&choice,data);
            continue;
        }
        if(input.kind==presentation::NativeTargetingInput::Extra) {
            // Angband '<' '>' 'x' 'p' 'm' 'g' 'k' (see native_target_hooks::extra).
            if(!hooks->extra)continue;
            notice_text.clear();
            const bool screen=hooks->extra_screen&&hooks->extra_screen(input.value)!=0;
            if(screen)bridge.end(); // The core opens a screen of its own; it must not hide behind the targeting UI.
            const auto before=choice;
            const int result=hooks->extra(input.value,&choice,data);
            choice.cursor_by_direction_key=0;
            if(result==NATIVE_EXTRA_END){if(!screen)bridge.end();out->ended_by_extra=1;return 0;}
            if(screen) {
                candidate_overflow=false;priorities.clear();if(hooks->candidates)hooks->candidates(data);
                staging.candidates=presentation::targeting_order_candidates(std::move(priorities));
                prepare_preview(*hooks,data,choice);bridge.begin(staging);continue;
            }
            if(result==NATIVE_EXTRA_UNKNOWN)continue;
            prepare_preview(*hooks,data,choice);
            if(result==NATIVE_EXTRA_NONE&&!notice_text.empty())staging.reason=text(notice_text.c_str());
            bridge.publish(staging,before.x!=choice.x||before.y!=choice.y||before.id!=choice.id);continue;
        }
        if(input.kind==presentation::NativeTargetingInput::Confirm) {
            prepare_preview(*hooks,data,choice);
            if(choice.can_confirm){*out=choice;bridge.end();return 1;}
            bridge.publish(staging,false);continue;
        }
        const auto previous=choice;
        if(request->mode!=NATIVE_TARGET_FIXED) {
            if(input.kind==presentation::NativeTargetingInput::Select){choice.x=input.point.x;choice.y=input.point.y;choice.id=0;choice.track_creature=1;choice.cursor_by_direction_key=0;}
            else if(input.kind==presentation::NativeTargetingInput::Move) {
                const auto delta=presentation::targeting_direction_delta(input.value);
                choice.x=std::clamp(choice.x+delta.x,0,request->width-1);choice.y=std::clamp(choice.y+delta.y,0,request->height-1);choice.id=0;choice.track_creature=1;choice.cursor_by_direction_key=1;
            } else if(input.kind==presentation::NativeTargetingInput::Cycle&&!staging.candidates.empty()) {
                const auto found=std::find_if(staging.candidates.begin(),staging.candidates.end(),[&](const auto &c){return c.id==choice.id;});
                const int count=int(staging.candidates.size()),at=found==staging.candidates.end()?(input.value?count-1:0):int(found-staging.candidates.begin());
                const auto &c=staging.candidates[(at+(input.value?1:-1)+count)%count];choice.x=c.point.x;choice.y=c.point.y;choice.id=c.id;choice.track_creature=1;choice.cursor_by_direction_key=0;
            }
        }
        prepare_preview(*hooks,data,choice);bridge.publish(staging,previous.x!=choice.x||previous.y!=choice.y||previous.id!=choice.id);
    }
}
}
