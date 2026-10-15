#include "bridge/native_targeting_runtime.h"
#include "bridge/native_targeting_api.h"
#include "frame/targeting_codec.h"
#include "frame/game_frame.h"
#include "ui/targeting_ui.h"
#include <iostream>
#include <cstdlib>
#include <stdexcept>

struct Probe { int previews{},reads{}; bool overflow{},crossing{},cancel{},disable{},single_wake{},duplicates{},missing{},invalid{}; TargetingState frame; };
static void candidates(void *) {
    hb_target_candidate(1,7,3,5,3,2,"Far",1);
    hb_target_candidate(2,5,3,3,1,1,"Near",1);
}
static void preview(native_target_choice *choice,void *data) {
    auto &p=*static_cast<Probe *>(data);++p.previews;
    hb_target_status(!p.invalid,p.missing?NATIVE_TARGET_PROFILE_MISSING:NATIVE_TARGET_OK,3,"Target",0,1);
    hb_target_path_begin(NATIVE_PATH_REACHABLE);
    hb_target_path_cell(2,3);hb_target_path_cell(5,3);hb_target_path_cell(7,3);
    if(!p.crossing)hb_target_affected(choice->id,choice->x,choice->y,choice->id!=0);
    if(p.missing)hb_target_area_cell(choice->x,choice->y,0);
    if(p.overflow)for(int i=0;i<257;++i){hb_target_path_begin(NATIVE_PATH_REACHABLE);hb_target_path_cell(i%20,i/20);}
    if(p.duplicates)for(int i=0;i<1000;++i){hb_target_path_begin(NATIVE_PATH_REACHABLE);hb_target_path_cell(2,3);hb_target_path_cell(5,3);hb_target_path_cell(7,3);}
}
static int read_key(void *data) {
    auto &p=*static_cast<Probe *>(data);++p.reads;
    p.frame=presentation::native_targeting().state();
    if(p.disable) {
        presentation::configure_native_targeting(false,"en");
        auto &target=presentation::native_targeting();
        p.single_wake=target.take_disabled_wake()&&!target.take_disabled_wake();
    }
    return p.cancel||p.overflow?27:13;
}
struct MenuProbe {int builds{},previews{},reads{};};
static void menu_preview(native_target_choice *,void *data){
    auto &probe=*static_cast<MenuProbe *>(data);++probe.previews;
    hb_target_status(1,NATIVE_TARGET_OK,0,"",1,1);hb_target_area_cell(3,3,1);
}
static void menu_builder(void *data){
    auto *probe=*static_cast<MenuProbe **>(data);++probe->builds;
    hb_target_profile_begin("test:menu-area");hb_target_profile_effect(NATIVE_SHAPE_BALL,1,2,0,0,1,0,0);
    native_target_request request{20,20,2,3,NATIVE_TARGET_FIXED,NATIVE_TARGET_ATTACK,0,0,0,0,"Pulse"};
    native_target_hooks hooks{nullptr,menu_preview,nullptr,nullptr,nullptr};
    hb_target_preview_fixed(&request,&hooks,probe);
}
int main() {
    int checks=0;auto check=[&](bool ok,const char *why){if(!ok)throw std::runtime_error(why);++checks;};
    presentation::configure_native_targeting(true,"en");
    native_target_hooks hooks{candidates,preview,read_key,nullptr,nullptr};
    native_target_request request{20,20,2,3,NATIVE_TARGET_POINT,NATIVE_TARGET_ATTACK,0,0,0,0,"Bolt"};
    native_target_choice choice{};Probe p;
    check(hb_target_preview_enabled(),"distribution enables registered effect prediction");
#ifdef _WIN32
    _putenv_s("HD2D_TARGETING_PREVIEW","0");
#else
    setenv("HD2D_TARGETING_PREVIEW","0",1);
#endif
    check(!hb_target_preview_enabled(),"diagnostic override disables effect prediction");
    check(hb_target_choose(&request,&hooks,&p,&choice)==1&&p.frame.paths.empty()&&p.frame.area.empty()
        &&p.frame.markers.size()==1&&p.frame.markers[0].affected,"UI-only mode shows a solid selected reticle without predicted paths or targets");
#ifdef _WIN32
    _putenv_s("HD2D_TARGETING_PREVIEW","1");
#else
    setenv("HD2D_TARGETING_PREVIEW","1",1);
#endif
    p={};
    hb_target_profile_begin("test:bolt");hb_target_profile_effect(NATIVE_SHAPE_BOLT,1,0,12,0,0,0,0);
    check(hb_target_choose(&request,&hooks,&p,&choice)==1&&choice.id==2,"nearest candidate is confirmed");
    check(p.previews==2,"confirmation revalidates the preview");
    check(!hb_target_active()&&hb_target_profile()->count==0,"scope and effect metadata are cleared after completion");
    p={};p.crossing=true;
    check(hb_target_choose(&request,&hooks,&p,&choice)==1&&!p.frame.markers[0].affected,"crossing a creature never invents an effect");
    p={};p.crossing=true;request.previous_valid=1;request.previous_x=5;request.previous_y=3;request.previous_id=0;
    check(hb_target_choose(&request,&hooks,&p,&choice)==1&&p.frame.markers[0].affected,"a reachable selected ground point is solid even when the projectile has no area effect");
    request.previous_valid=0;
    p={};p.overflow=true;
    check(hb_target_choose(&request,&hooks,&p,&choice)==0&&!p.frame.can_confirm&&p.frame.paths.empty()&&!p.frame.reason.empty(),"overflow is explicit and cannot confirm partial coverage");
    p={};p.duplicates=true;
    check(hb_target_choose(&request,&hooks,&p,&choice)==1&&p.frame.paths.size()==1,"equivalent scatter endpoints do not overflow or redraw the same path");
    p={};p.missing=true;
    check(hb_target_choose(&request,&hooks,&p,&choice)==1&&p.frame.can_confirm&&p.frame.paths.empty()&&p.frame.area.empty()
        &&p.frame.markers.size()==1&&!p.frame.markers.front().affected&&!p.frame.reason.empty(),"missing coverage preserves native confirmation without a partial footprint or false impact marker");
    p={};p.missing=true;p.invalid=true;p.cancel=true;
    check(hb_target_choose(&request,&hooks,&p,&choice)==0&&!p.frame.can_confirm,"missing coverage never overrides a native rejection");
    p={};p.missing=true;p.overflow=true;
    check(hb_target_choose(&request,&hooks,&p,&choice)==0&&!p.frame.can_confirm&&p.frame.reason.find("display limit")!=std::string::npos,"a missing effect model does not erase an already detected display overflow");
    p={};p.cancel=true;request.previous_valid=1;request.previous_id=1;request.previous_x=7;request.previous_y=3;
    check(hb_target_choose(&request,&hooks,&p,&choice)==0&&p.frame.selected_id==1,"existing valid target precedes nearest candidate");
    std::string error;TargetingState decoded;
    check(decode_targeting(encode_targeting(p.frame),decoded,error),"native frame satisfies wire contract");
    hd2d::TargetingUi ui;ui.sync(p.frame);presentation::InputEventsMessage out;
    ui.select({9,3},out);ui.confirm(out);
    check(out.events.size()==1&&out.events[0].action=="aim_select","UI cannot confirm an unacknowledged cursor");
    ui.cancel(out);check(out.events.size()==1,"cancel waits for outstanding preview acknowledgement");
    auto ack=p.frame;ack.request_id=out.events[0].request_id;ack.revision++;ack.result="accepted";ui.sync(ack);ui.advance(out);
    check(out.events.size()==2&&out.events[1].action=="aim_cancel","queued cancellation follows acknowledgement");
    float yaw=0,facing=0,pitch=5;
    ui.sync({});ui.sync(p.frame);
    for(int i=0;i<60;++i)ui.aim_camera(2.5f,3.5f,.55f,1.f/60.f,true,yaw,facing,pitch);
    check(std::abs(yaw-1.5707963f)<.002f&&std::abs(pitch)<.1f,"first-person view smoothly reaches the selected creature");
    p={};p.disable=true;
    check(hb_target_choose(&request,&hooks,&p,&choice)==0&&!hb_target_active()&&p.previews==1,"disabling during input cancels even when a confirm key arrives");
    check(p.single_wake,"polling sends exactly one cancellation key before the native input loop consumes it");
    presentation::configure_native_targeting(true,"en");
    const int scope=hb_target_menu_begin();MenuProbe menu;auto *menu_ptr=&menu;
    hb_target_menu_row('b',6,0," b) Pulse  1  2",menu_builder,&menu_ptr,sizeof(menu_ptr));
    GameFrame menu_frame;menu_frame.menu_open=true;menu_frame.modern.version=1;menu_frame.modern.stage="choices";menu_frame.modern.cursor=0;
    menu_frame.modern.subtitle="Spells";menu_frame.modern.rows.emplace_back();menu_frame.modern.rows.back().key='b';
    menu_frame.menu_term_lines.emplace_back();menu_frame.menu_term_lines.back().source_row=6;menu_frame.menu_term_lines.back().text_utf8=" b) Pulse  1  2";
    presentation::apply_native_targeting_menu_frame(menu_frame);
    check(menu_frame.targeting.preview_only&&!menu_frame.targeting.active&&menu_frame.targeting.area.size()==1&&menu_frame.targeting.markers.empty()
        &&menu_frame.menu_open&&menu_frame.menu_over_map,"native menu preview keeps the menu and map without adding a targeting request");
    check(menu.builds==1&&menu.previews==1&&!hb_target_active(),"menu coverage is computed without reading or committing a key");
    check(decode_targeting(encode_targeting(menu_frame.targeting),decoded,error)&&!decoded.can_confirm,"passive coverage satisfies the wire contract");
    ui.sync(menu_frame.targeting);presentation::InputEventsMessage passive_input;ui.confirm(passive_input);ui.move(6,passive_input);
    check(ui.visible()&&!ui.active()&&passive_input.events.empty(),"passive preview leaves menu keyboard and confirmation ownership unchanged");
    presentation::apply_native_targeting_menu_frame(menu_frame);check(menu.builds==1,"unchanged menu focus reuses the read-only preview");
    menu_frame.menu_term_lines.back().text_utf8=" b) Different menu";presentation::apply_native_targeting_menu_frame(menu_frame);
    check(!menu_frame.targeting.preview_only&&menu.builds==1,"a foreign menu cannot reuse an action with the same shortcut");
    hb_target_menu_end(scope);menu_frame.menu_term_lines.back().text_utf8=" b) Pulse  1  2";presentation::apply_native_targeting_menu_frame(menu_frame);
    check(!menu_frame.targeting.preview_only,"leaving the native chooser expires its preview callbacks");
    const int cursor_scope=hb_target_menu_begin();hb_target_menu_geometry(0,1,40,3,0);
    hb_target_menu_row('a',1,0," > Pulse",menu_builder,&menu_ptr,sizeof(menu_ptr));hb_target_menu_row_text("Pulse","level 1",1);
    hb_target_menu_row('b',2,0,"   Unlearned",nullptr,nullptr,0);hb_target_menu_row_text("Unlearned","level 50",0);
    GameFrame cursor_menu;cursor_menu.menu_open=true;
    cursor_menu.menu_term_lines.emplace_back();cursor_menu.menu_term_lines.back().source_row=1;cursor_menu.menu_term_lines.back().text_utf8=" > Pulse";
    cursor_menu.menu_term_lines.emplace_back();cursor_menu.menu_term_lines.back().source_row=2;cursor_menu.menu_term_lines.back().text_utf8="   Unlearned";
    presentation::prepare_native_targeting_menu_frame(cursor_menu);
    check(cursor_menu.menu_choices.size()==2&&cursor_menu.menu_choices[0].selected&&cursor_menu.menu_choices[0].key==13
        &&!cursor_menu.menu_choices[1].available&&cursor_menu.menu_choices[1].label_utf8=="Unlearned",
        "native cursor spell selection keeps unavailable rows and confirms with the native Enter key");
    hb_target_menu_end(cursor_scope);
    MenuProbe first_item,second_item;
    const int item_scope=hb_target_item_menu_begin('u',[](void *,int action,const void *object){
        if(action=='u'){auto *probe=const_cast<MenuProbe *>(static_cast<const MenuProbe *>(object));menu_builder(&probe);}
    },nullptr);
    hb_target_item_menu_display(1);
    hb_target_item_menu_row("item:1",&first_item);hb_target_item_menu_row("item:2",&second_item);
    GameFrame item_menu;item_menu.menu_open=true;item_menu.modern.version=1;item_menu.modern.stage="choices";item_menu.modern.cursor=0;
    item_menu.modern.rows.resize(2);item_menu.modern.rows[0].id="item:1";item_menu.modern.rows[1].id="item:2";
    item_menu.modern.rows[0].key=item_menu.modern.rows[1].key=13;
    item_menu.modern.preview_target="item:2";
    presentation::apply_native_targeting_menu_frame(item_menu);
    check(second_item.builds==1&&!first_item.builds&&item_menu.targeting.preview_only,"item preview uses the actual item identity when multiple rows share Enter");
    hb_target_item_menu_row("item:1",&first_item);hb_target_item_menu_row("item:2",&second_item);
    presentation::apply_native_targeting_menu_frame(item_menu);
    check(second_item.builds==1,"reading the same item rows does not recompute a cached preview");
    hb_target_item_menu_display(0);hb_target_item_menu_display(1);hb_target_item_menu_row("item:2",&second_item);
    presentation::apply_native_targeting_menu_frame(item_menu);
    check(!item_menu.targeting.preview_only&&second_item.builds==1,"a later item request inside the command cannot inherit the consumed item's effect");
    hb_target_menu_end(item_scope);
    std::cout<<"PASS "<<checks<<" native targeting runtime checks\n";
}
