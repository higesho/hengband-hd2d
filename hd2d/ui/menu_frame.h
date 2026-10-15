#pragma once
#include "ui/ui_layout.h"
#include <algorithm>
namespace hd2d {
inline RectPx fixed_menu_rect(const UiLayout &layout) {
    const auto &area=layout.content;
    const int width=std::max(1,std::min(1480,area.w-32));
    const int height=std::max(1,std::min(740,area.h-32));
    return {area.x+(area.w-width)/2,area.y+(area.h-height)/2,width,height};
}
inline void layout_effect_preview(UiLayout &layout,float font_scale) {
    const auto area=layout.content,original=layout.scene;
    if(area.w<96||area.h<160||original.empty())return;
    const auto intersect=[](RectPx a,RectPx b){
        const int x=std::max(a.x,b.x),y=std::max(a.y,b.y);
        return RectPx{x,y,std::max(0,std::min(a.x+a.w,b.x+b.w)-x),std::max(0,std::min(a.y+a.h,b.y+b.h)-y)};
    };
    RectPx menu,map;
    const int minimum_menu=int(760*font_scale+.5f);
    if(area.w>=area.h&&original.w>=minimum_menu+220) {
        const int width=std::max(minimum_menu,area.w*43/100);
        const int height=std::min(area.h-24,int(740*font_scale+.5f));
        menu={area.x+12,area.y+(area.h-height)/2,width,height};
        map=intersect(original,{menu.x+menu.w+12,area.y+12,std::max(1,area.x+area.w-menu.x-menu.w-24),area.h-24});
    } else {
        const int height=std::clamp(std::max(area.h*56/100,int(200*font_scale+.5f)),1,std::max(1,area.h-132));
        const int map_height=area.h-height-36;
        const int width=std::min(area.w-24,int(1480*font_scale+.5f));
        menu={area.x+(area.w-width)/2,area.y+map_height+24,width,height};
        map=intersect(original,{area.x+12,area.y+12,area.w-24,map_height});
    }
    if(map.w<32||map.h<32)return;
    layout.preview_menu=menu;layout.scene=map;
}
inline RectPx menu_text_area(const UiLayout &layout,bool full,int theme) {
    return theme>=0?fixed_menu_rect(layout):(full?layout.term_full:layout.term_overlay);
}
}
