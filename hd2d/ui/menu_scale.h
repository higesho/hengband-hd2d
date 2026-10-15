#pragma once
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"
#include "ui/ui_image.h"
#include "render/text_overlay.h"
#include <algorithm>
namespace hd2d {
inline float menu_font_scale(int level){constexpr float scales[]={1.f,1.125f,1.25f,1.5f,1.75f};return scales[std::clamp(level,0,4)];}
inline RectPx menu_scaled_rect(RectPx r,float scale){return {int(r.x*scale+.5f),int(r.y*scale+.5f),int(r.w*scale+.5f),int(r.h*scale+.5f)};}
inline UiLayout menu_logical_layout(UiLayout l,float scale,TextOverlay *text=nullptr) {
    const float inverse=1.f/scale;
    l.screen_w=int(l.screen_w*inverse);l.screen_h=int(l.screen_h*inverse);l.origin_x=int(l.origin_x*inverse);l.origin_y=int(l.origin_y*inverse);
    for(auto *r:{&l.content,&l.scene,&l.preview_menu,&l.term_full,&l.term_overlay,&l.prompt_bar,&l.bottom_bar})*r=menu_scaled_rect(*r,inverse);
    if(text){l.cell_w=text->cell_w();l.cell_h=text->cell_h();}
    return l;
}
inline UiLayout panel_logical_layout(const UiLayout &physical,float scale,TextOverlay &text) {
    auto l=menu_logical_layout(physical,scale,&text);
    for(auto *r:{&l.status_col,&l.hud,&l.message_bar,&l.minimap,&l.minimap_area})*r=menu_scaled_rect(*r,1.f/scale);
    for(auto &r:l.sub)r=menu_scaled_rect(r,1.f/scale);
    for(auto &r:l.grip)r=menu_scaled_rect(r,1.f/scale);
    return l;
}
struct MenuScaleScope {
    UiPaint &paint;TextOverlay &text;float old_paint,old_text,scale;
    MenuScaleScope(UiPaint &p,TextOverlay &t,float requested):paint(p),text(t),old_paint(p.layout_scale()),old_text(t.layout_scale()) {
        text.set_layout_scale(requested);scale=text.layout_scale();paint.set_layout_scale(scale);
    }
    ~MenuScaleScope(){paint.set_layout_scale(old_paint);text.set_layout_scale(old_text);}
};
struct MenuImageScaleScope {
    const UiImagePainter &paint;TextOverlay *text;float old_paint,old_text;
    MenuImageScaleScope(const UiImagePainter &p,TextOverlay *t,float scale):paint(p),text(t),old_paint(p.layout_scale()),old_text(t?t->layout_scale():1.f){paint.set_layout_scale(scale);if(text)text->set_layout_scale(scale);}
    ~MenuImageScaleScope(){paint.set_layout_scale(old_paint);if(text)text->set_layout_scale(old_text);}
};
}
