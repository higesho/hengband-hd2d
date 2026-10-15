#include "frame/targeting_codec.h"
#include "bridge/native_targeting.h"
#include "bridge/targeting_geometry.h"
#include "bridge/targeting_scatter.h"
#include "render/targeting_visual.h"
#include "ui/targeting_ui.h"
#include <iostream>
#include <stdexcept>
#include <set>

static int scatter_distance(int sy,int sx,int ty,int tx){const int x=std::abs(tx-sx),y=std::abs(ty-sy);return std::max(x,y)+std::min(x,y)/2;}
static int scatter_visit(int y,int x,void *data){static_cast<std::set<std::pair<int,int>> *>(data)->insert({x,y});return 1;}

int main() {
    int checks=0;const auto check=[&](bool okay,const char *message){if(!okay)throw std::runtime_error(message);++checks;};
    TargetingState state;state.active=true;state.scope=1;state.revision=1;state.width=30;state.height=20;
    state.kind="attack";state.label="Fire";state.source={2,3};state.selected={12,3};state.selected_id=1;state.can_confirm=true;
    state.candidates={{1,{12,3},"Orc"},{2,{7,3},"Troll"}};
    state.paths={{{{2,3},{3,3},{4,3},{5,3},{6,3},{7,3}},false}};
    state.markers={{1,{12,3},true,true,false,true},{2,{7,3},true,false,true,true}};
    state.area={{{7,3},false},{{8,3},true}};
    TargetingState decoded;std::string error;auto json=encode_targeting(state);
    check(decode_targeting(json,decoded,error)&&decoded==state,"targeting roundtrip");
    auto bad=json;bad["selected"]={30,3};check(!decode_targeting(bad,decoded,error),"off-map selection rejected");
    bad=json;bad["candidates"].push_back(bad["candidates"][0]);check(!decode_targeting(bad,decoded,error),"duplicate candidate rejected");
    bad=json;bad["area"].push_back(bad["area"][0]);check(!decode_targeting(bad,decoded,error),"duplicate coverage rejected");
    bad=json;bad["markers"][1][4]=true;check(!decode_targeting(bad,decoded,error),"two selected markers rejected");
    bad=json;bad["version"]=2;check(!decode_targeting(bad,decoded,error),"unknown targeting version rejected");
    bad=json;bad["kind"]="enemy-intent";check(!decode_targeting(bad,decoded,error),"enemy intent is not a player targeting mode");
    for(int direction:{1,2,3,4,6,7,8,9}) {
        auto delta=presentation::targeting_direction_delta(direction);
        check(presentation::targeting_nearest_direction({10,10},{10+delta.x*5,10+delta.y*5})==direction,"eight directions agree with their grid vectors");
    }
    check(presentation::targeting_nearest_direction({2,2},{12,3})==6,"near-horizontal click chooses east, not southeast");
    check(presentation::targeting_nearest_direction({2,2},{3,12})==2,"near-vertical click chooses south");
    check(presentation::targeting_nearest_direction({2,2},{2,2})==5,"self stays distinct from an eight-way direction");
    std::set<std::pair<int,int>> scatter;
    check(hb_target_scatter_points(7,11,7,13,5,0,scatter_distance,scatter_visit,&scatter)&&scatter==std::set<std::pair<int,int>>{{51,7}},"zero deviation uses the native extended target without consuming randomness");
    scatter.clear();hb_target_scatter_points(7,11,7,13,5,3,scatter_distance,scatter_visit,&scatter);
    std::set<std::pair<int,int>> expected;
    for(int y=-6;y<=6;++y)for(int x=-6;x<=6;++x)if(scatter_distance(0,0,y,x)<=6)expected.insert({51+x,7+y});
    check(scatter==expected,"scatter includes exactly every endpoint accepted by the native rejection sampler");
    scatter.clear();hb_target_scatter_points(7,11,900,900,6,0,scatter_distance,scatter_visit,&scatter);
    check(scatter==std::set<std::pair<int,int>>{{31,7}},"eight-way scatter uses the native direction instead of the distant reference point");
    auto ordered=presentation::targeting_order_candidates({{{1,{3,3},"one"},4,2,4,true},{{2,{4,3},"two"},4,1,3,true},{{3,{5,3},"three"},4,1,2,true},{{4,{2,3},"hidden"},1,0,0,false}});
    check(ordered.size()==3&&ordered[0].id==3&&ordered[1].id==2&&ordered[2].id==1,"eligible candidates use opportunity then native order at equal distance");
    presentation::NativeTargeting bridge;bridge.set_enabled(true);bridge.begin(state);
    presentation::InputEventWire request;request.e="ui_action";request.action="aim_select";request.target="8,3";request.request_id=1;request.ui_revision=bridge.state().revision;
    std::vector<int> keys;check(bridge.input(request,keys)&&keys==std::vector<int>{29},"target selection wakes only the targeting loop");
    keys.clear();bridge.input(request,keys);check(keys.empty(),"duplicate request does not wake or repeat");
    request.request_id=2;request.action="aim_confirm";bridge.input(request,keys);
    check(bridge.state().result=="rejected","cannot confirm while a previous preview is queued");
    presentation::NativeTargetingInput event;check(bridge.take(event)&&event.kind==presentation::NativeTargetingInput::Select&&event.point==TargetingPoint{8,3},"selection delivered once");
    check(!bridge.take(event),"no confirmation leaked through the preview barrier");
    state.selected={8,3};state.selected_id=0;state.can_confirm=false;state.markers={{0,{8,3},false,true,false,false}};bridge.publish(state,true);
    request.ui_revision=bridge.state().revision;request.request_id=3;keys.clear();bridge.input(request,keys);
    check(bridge.state().result=="rejected"&&!bridge.take(event),"invalid target cannot execute");
    request.action="aim_cancel";request.request_id=4;keys.clear();bridge.input(request,keys);
    check(bridge.take(event)&&event.kind==presentation::NativeTargetingInput::Cancel,"invalid target remains cancellable");
    bridge.end();request.action="aim_confirm";request.request_id=5;keys.clear();
    check(bridge.input(request,keys)&&keys.empty(),"late confirmation never reaches gameplay");
    state.kind="attack";state.markers={{1,{12,3},true,true,false,true},{2,{7,3},true,false,true,true}};
    std::vector<hd2d::TargetingEntity> bodies={{1,12,3,12.4f,3.5f,0.f,1.f,1.5f},{2,7,3,7.5f,3.5f,0.f,1.f,1.f}};
    const auto visual=hd2d::build_targeting_visual(state,{2.5f,3.5f,1.f},bodies,0.);
    check(visual.reticles.size()==2&&visual.reticles[0].a<visual.reticles[1].a,"blocked selected target remains faint while first collision is opaque");
    check(visual.reticles[0].selected==1.f&&visual.reticles[1].selected==0.f,"selection has a separate visual channel");
    check(visual.reticles[0].up.z>0.f&&visual.reticles[0].center.x<bodies[0].x,"creature reticle stands in front of rendered body");
    const auto later=hd2d::build_targeting_visual(state,{2.5f,3.5f,1.f},bodies,.05);
    check(!visual.dots.empty()&&!later.dots.empty()&&later.dots[0].center.x>visual.dots[0].center.x,"ray dots travel forward");
    auto turn=state;turn.paths={{{{7,3},{8,3}},false,true}};
    const auto fading=hd2d::build_targeting_visual(turn,{2.5f,3.5f,1.f},bodies,0.);
    check(fading.dots.size()>1&&fading.dots.front().a>fading.dots.back().a&&fading.dots.back().a<.05f,"one-cell reflected continuation fades towards its end");
    auto look_state=state;look_state.source={2,3};look_state.selected={6,3};look_state.selected_id=0;
    hd2d::TargetingUi aiming;aiming.sync(look_state);float yaw=0,facing=0,pitch=0;
    aiming.aim_camera(2.5f,3.5f,1.f,1.f/60.f,true,yaw,facing,pitch);
    check(yaw>0&&yaw<1.5f&&facing==yaw,"first-person aim begins with a partial turn instead of snapping");
    for(int i=0;i<80;++i)aiming.aim_camera(2.5f,3.5f,1.f,1.f/60.f,true,yaw,facing,pitch);
    check(std::abs(yaw-1.5707963f)<.002f&&pitch>0,"first-person aim converges to the selected point");
    check(visual.area.size()==2&&visual.area[0].style!=visual.area[1].style,"possible area is visually distinct");
    TargetingPoint picked;
    check(hd2d::pick_targeting_entity({2.5f,3.5f,.5f},{1.f,0.f,0.f},bodies,picked)&&picked==TargetingPoint{7,3},"billboard picking returns nearest rendered creature");
    check(!hd2d::pick_targeting_entity({2.5f,3.5f,3.f},{1.f,0.f,0.f},bodies,picked),"click above creature does not hit its grid column");
    state.markers={{0,{8,3},false,true,false,false}};
    const auto ground=hd2d::build_targeting_visual(state,{2.5f,3.5f,1.f},bodies,0.);
    check(ground.reticles[0].up.z==0.f&&ground.reticles[0].dashed==1.f,"invalid point is a dashed ground reticle");
    const hd2d::TargetingMapProjection map{13,21,11,9,8,16,50,30};
    check(map.pick(13+5*8+4,21+4*16+8,picked)&&picked==TargetingPoint{50,30},"ASCII center selection agrees with displayed camera cell");
    check(map.pick(13,21,picked)&&picked==TargetingPoint{45,26},"ASCII top-left cell uses font dimensions and centered margin");
    check(!map.pick(12,21,picked)&&!map.pick(13+11*8,21,picked)&&!map.pick(13,21+9*16,picked),"ASCII margins and exclusive edges cannot select off-map cells");
    check(!hd2d::TargetingMapProjection{}.pick(0,0,picked),"undrawn ASCII map cannot reuse stale picking coordinates");
    bodies.resize(1);bodies[0].normal={0,1,0};
    check(!hd2d::pick_targeting_entity({2.5f,3.5f,.5f},{1,0,0},bodies,picked),"picking honors the actual fixed or snapped slab plane");
    state.markers={{1,{12,3},true,true,true,true}};
    const auto fixed=hd2d::build_targeting_visual(state,{2.5f,3.5f,1},bodies,0);
    check(fixed.reticles[0].right.y==0&&fixed.reticles[0].up.z>0,"reticle follows the rendered slab orientation");
    std::cout<<"PASS "<<checks<<" targeting contract checks\n";
}
