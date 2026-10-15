#pragma once
#include "frame/minimap_snapshot.h"
#include "ui/modern_skin.h"
#include "ui/minimap_markers.h"
#include "render/text_overlay.h"
#include <array>
#include <algorithm>
#include <cmath>
namespace hd2d {
inline PaintColor modern_map_color(MinimapKind kind) {
    switch(kind) {
    case MinimapKind::Floor:return {.24f,.31f,.36f,1};
    case MinimapKind::Wall:case MinimapKind::Mountain:return {.56f,.62f,.67f,1};
    case MinimapKind::Door:return {.77f,.48f,.22f,1};
    case MinimapKind::Stairs:return {.75f,.4f,.91f,1};
    case MinimapKind::Item:return {.35f,.8f,1,1};
    case MinimapKind::Monster:return {1,.2f,.22f,1};
    case MinimapKind::Player:return {1,1,1,1};
    case MinimapKind::Water:return {.12f,.4f,.76f,1};
    case MinimapKind::Tree:return {.12f,.36f,.19f,1};
    case MinimapKind::Grass:return {.26f,.5f,.3f,1};
    case MinimapKind::Lava:return {.9f,.25f,.1f,1};
    case MinimapKind::Snow:return {.79f,.87f,.92f,1};
    default:return {.025f,.04f,.065f,1};
    }
}
inline RectPx draw_modern_map(UiPaint &paint,TextOverlay &text,RectPx area,const MinimapSnapshot &map,int theme,bool japanese) {
    if(!map.valid()||area.w<16||area.h<16)return {};
    const auto skin=modern_skin(theme);paint_modern_inset(paint,area,theme);
    const int legend_rows=area.w<650?3:2,legend_h=legend_rows*(text.cell_h()+8);
    const RectPx inner{area.x+8,area.y+8,area.w-16,std::max(1,area.h-legend_h-16)};
    const float scale=std::min(float(inner.w)/map.width,float(inner.h)/map.height);
    const int width=int(std::ceil(map.width*scale)),height=int(std::ceil(map.height*scale));
    const RectPx grid{inner.x+(inner.w-width)/2,inner.y+(inner.h-height)/2,width,height};paint.rect(grid,modern_map_color(MinimapKind::Unknown));
    for(int y=0;y<map.height;++y)for(int x=0;x<map.width;++x) {
        const auto kind=map.at(x,y);const auto marker=map.marker_at(x,y);if(kind==MinimapKind::Unknown&&marker==MinimapMarker::None)continue;
        const int left=grid.x+int(x*scale),top=grid.y+int(y*scale),right=grid.x+int((x+1)*scale),bottom=grid.y+int((y+1)*scale);
        const RectPx cell{left,top,std::max(1,right-left),std::max(1,bottom-top)};
        paint.rect(cell,modern_map_color(kind));
        if(marker!=MinimapMarker::None){const int size=std::max(1,std::min(cell.w,cell.h)/2);paint.rect({cell.x+(cell.w-size)/2,cell.y+(cell.h-size)/2,size,size},minimap_marker_color(marker));}
    }
    if(map.player_gx>=0&&map.player_gy>=0&&map.player_gx<map.width&&map.player_gy<map.height) {
        const int x=grid.x+int((map.player_gx+.5f)*scale),y=grid.y+int((map.player_gy+.5f)*scale),r=std::clamp(int(scale),3,7);
        paint.frame({x-r,y-r,2*r+1,2*r+1},{1,1,1,1},1);paint.rect({x-1,y-1,3,3},{1,1,1,1});
    }
    struct Legend {MinimapKind kind;const char *ja,*en;};
    const std::array<Legend,6> legend{{{MinimapKind::Player,"現在地","You"},{MinimapKind::Wall,"壁","Wall"},{MinimapKind::Door,"扉","Door"},{MinimapKind::Stairs,"階段","Stairs"},{MinimapKind::Item,"アイテム","Items"},{MinimapKind::Monster,"敵","Enemies"}}};
    const int columns=legend_rows==3?2:3,cell_w=(area.w-16)/columns;
    for(size_t i=0;i<legend.size();++i){const int x=area.x+8+int(i%columns)*cell_w,y=area.y+area.h-legend_h+int(i/columns)*(text.cell_h()+8);paint.rect({x,y+3,10,10},modern_map_color(legend[i].kind));text.draw(x+18,y,japanese?legend[i].ja:legend[i].en,skin.text,cell_w-24);}
    return grid;
}
}
