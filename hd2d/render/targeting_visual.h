#pragma once
#include "frame/game_frame.h"
#include "frame/targeting.h"
#include "render/math3d.h"
#include <functional>
#include <span>
#include <vector>

namespace hd2d {
struct TargetingMapProjection {
    int x{},y{},cols{},rows{},cell_w{},cell_h{},cam_x{},cam_y{};
    bool pick(int sx,int sy,TargetingPoint &point)const {
        if(cell_w<=0||cell_h<=0||sx<x||sy<y||sx>=x+cols*cell_w||sy>=y+rows*cell_h)return false;
        point={cam_x+(sx-x)/cell_w-cols/2,cam_y+(sy-y)/cell_h-rows/2};return true;
    }
};
struct TargetingEntity {
    int id{},gx{},gy{};
    float x{},y{},z{},width{1.f},height{1.f};
    Vec3 normal{};
    bool glyph{};
};
struct TargetingQuad {
    Vec3 center,right,up;
    float r{1.f},g{1.f},b{1.f},a{1.f};
    float style{},selected{},dashed{}; // 0 reticle, 1 fixed area, 2 possible area, 3 ray dot
};
struct TargetingVisual {
    std::vector<TargetingQuad> area,dots,reticles;
};
TargetingVisual build_targeting_visual(const TargetingState &,Vec3 eye,std::span<const TargetingEntity>,double time);
// TOband's element scope: one translucent square per cell, a little above the ground, in the colour the core read from the
// original's map_info() (a TERM_COLOR, turned into RGB with the frame's colour table). `ground_lift(gx,gy)` is the height of
// the ground at the cell (the ground relief the core declared; 0 for a flat ground). Empty input -> empty output.
std::vector<TargetingQuad> build_element_scope_quads(std::span<const ElementScopeCell>,const std::function<float(int,int)> &ground_lift);
// Returns a visible billboard/slab's grid point. Geometry is taken from the
// actual rendered entity, not from a ground-plane inverse projection.
bool pick_targeting_entity(Vec3 ray_origin,Vec3 ray_direction,std::span<const TargetingEntity>,TargetingPoint &point);
}
