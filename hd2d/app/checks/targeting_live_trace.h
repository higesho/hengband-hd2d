#pragma once
#include "ui/targeting_ui.h"
#include "render/camera.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
namespace hd2d {
inline void trace_targeting_ui(const TargetingUi &ui,const Camera &camera,const UiLayout &layout,bool ascii,bool fps) {
    const char *path=std::getenv("HD2D_TARGETING_UI_TRACE");if(!path||!*path)return;
    const auto &s=ui.state();const auto &map=ui.map_projection;
    nlohmann::json data{{"active",s.active},{"preview_only",s.preview_only},{"revision",s.revision},{"request_id",s.request_id},
        {"screen",{layout.screen_w,layout.screen_h}},{"selected",{s.selected.x,s.selected.y}},
        {"ascii",ascii},{"fps",fps},{"yaw",camera.yaw},{"pitch",camera.pitch},
        {"map",{map.x,map.y,map.cols,map.rows,map.cell_w,map.cell_h,map.cam_x,map.cam_y}}};
    data["buttons"]=nlohmann::json::array();
    for(const auto &b:ui.buttons())data["buttons"].push_back({{"action",b.action},{"value",b.value},{"enabled",b.enabled},{"rect",{b.rect.x,b.rect.y,b.rect.w,b.rect.h}}});
    data["points"]=nlohmann::json::array();
    if(s.active)for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx) {
        const TargetingPoint p{s.source.x+dx,s.source.y+dy};float x=0,y=0;
        if(ascii){x=float(map.x+(p.x-map.cam_x+map.cols/2)*map.cell_w+map.cell_w/2);y=float(map.y+(p.y-map.cam_y+map.rows/2)*map.cell_h+map.cell_h/2);}
        else {if(!camera.project({p.x+.5f,p.y+.5f,0.f},x,y))continue;x+=layout.scene.x;y+=layout.scene.y;}
        if(layout.scene.contains(int(x),int(y)))data["points"].push_back({{"grid",{p.x,p.y}},{"pixel",{x,y}}});
    }
    std::ofstream(path)<<data.dump();
}
}
