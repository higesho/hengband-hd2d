#pragma once
#include "ui/ui_paint.h"
#include "render/text_overlay.h"
#include <algorithm>

namespace hd2d {
struct ModernSkin {
    PaintColor background,inset,border,trim,selection;
    TextColor text,muted,strong,selected;
    UiMaterial material;
};
inline ModernSkin modern_skin(int theme) {
    if(theme==1)return {{.91f,.835f,.67f,1},{.935f,.87f,.735f,1},{.38f,.25f,.13f,1},{.65f,.46f,.23f,1},{.79f,.66f,.44f,1},
        {.22f,.155f,.095f,1},{.32f,.28f,.24f,1},{.29f,.16f,.075f,1},{.20f,.11f,.05f,1},UiMaterial::Parchment};
    if(theme==2)return {{.055f,.095f,.13f,1},{.068f,.115f,.155f,1},{.34f,.245f,.16f,1},{.76f,.48f,.29f,1},{.37f,.16f,.16f,1},
        {.93f,.91f,.82f,1},{.40f,.46f,.47f,1},{.96f,.81f,.58f,1},{1.f,.94f,.81f,1},UiMaterial::Washi};
    return {{.028f,.105f,.225f,1},{.018f,.067f,.145f,1},{.22f,.48f,.72f,1},{.53f,.83f,1.f,1},{.07f,.28f,.49f,1},
        {.88f,.95f,1.f,1},{.36f,.48f,.58f,1},{.73f,.90f,1.f,1},{.96f,.99f,1.f,1},UiMaterial::Glass};
}
inline RectPx skin_inset(RectPx r,int n) {return {r.x+n,r.y+n,std::max(0,r.w-2*n),std::max(0,r.h-2*n)};}
inline void skin_diamond(UiPaint &p,int x,int y,int radius,PaintColor c) {
    p.triangle(float(x-radius),float(y),float(x),float(y-radius),float(x+radius),float(y),c);
    p.triangle(float(x-radius),float(y),float(x+radius),float(y),float(x),float(y+radius),c);
}
// Decorations stay inside the frame and never participate in hit testing.
inline void paint_modern_window(UiPaint &p,RectPx r,int theme,bool popup=false,bool background=true,float opacity=1.f) {
    if(r.empty())return;
    const auto s=modern_skin(theme);const int bevel=popup?3:7;
    if(theme==0) {
        if(background){auto fill=s.background;fill.a*=opacity;p.surface(r,fill,UiMaterial::Glass);}
        p.rounded_frame(r,{.16f,.43f,.71f,.35f},10,1);
        p.rounded_frame(skin_inset(r,1),{.52f,.81f,1.f,.9f},9,1);
        p.rounded_frame(skin_inset(r,3),{.24f,.57f,.85f,.23f},7,1);
        if(r.w>70)p.rect({r.x+20,r.y+3,r.w-40,1},{.70f,.90f,1.f,.38f});
        return;
    }
    if(background){auto fill=s.background;fill.a*=opacity;p.surface(r,fill,s.material);}
    p.frame(r,{.015f,.018f,.022f,1},1);
    if(theme==2) {
        const PaintColor wood{.20f,.125f,.075f,1};
        p.surface({r.x+1,r.y+1,r.w-2,bevel},wood,UiMaterial::Wood);
        p.surface({r.x+1,r.y+r.h-bevel-1,r.w-2,bevel},wood,UiMaterial::Wood);
        p.surface({r.x+1,r.y+1,bevel,r.h-2},wood,UiMaterial::Wood);
        p.surface({r.x+r.w-bevel-1,r.y+1,bevel,r.h-2},wood,UiMaterial::Wood);
    }
    p.frame(skin_inset(r,2),s.border,1);
    p.frame(skin_inset(r,bevel),{s.trim.r,s.trim.g,s.trim.b,.65f},1);
    if(r.w<80||r.h<70)return;
    if(!popup&&background) {
        const PaintColor shade=theme==1?PaintColor{.37f,.23f,.10f,.065f}:PaintColor{0,0,0,.20f};
        p.rect({r.x+bevel+1,r.y+bevel+1,r.w-2*bevel-2,37},shade);
        p.rect({r.x+bevel+1,r.y+r.h-43,r.w-2*bevel-2,35},shade);
    }
    const int corner=popup?5:12;
    for(int sx:{-1,1})for(int sy:{-1,1}) {
        const int x=sx<0?r.x+bevel:r.x+r.w-bevel;
        const int y=sy<0?r.y+bevel:r.y+r.h-bevel;
        const int dx=-sx,dy=-sy;
        if(theme==1) {
            // Paired printer's rules with a small leaf-shaped diamond.
            p.rect({std::min(x,x+dx*corner),y,corner,1},s.trim);
            p.rect({x,std::min(y,y+dy*corner),1,corner},s.trim);
            skin_diamond(p,x+dx*6,y+dy*6,3,s.border);
            p.rect({x+dx*10,std::min(y+dy*3,y+dy*12),1,9},s.trim);
        } else {
            // Stepped joinery and a vermilion knot at each timber corner.
            p.rect({std::min(x,x+dx*corner),y,corner,2},s.trim);
            p.rect({x,std::min(y,y+dy*corner),2,corner},s.trim);
            const int qx=x+dx*3,qy=y+dy*3;
            p.frame({std::min(qx,qx+dx*4),std::min(qy,qy+dy*4),4,4},{.65f,.21f,.16f,1},2);
        }
    }
    if(!popup&&r.w>240&&r.h>120) {
        const int cx=r.x+r.w/2,cy=r.y+r.h/2;
        if(theme==1) {
            skin_diamond(p,cx,r.y+6,3,s.border);
            for(int d:{-1,1})skin_diamond(p,cx+d*9,r.y+6,2,s.trim);
            for(int x:{r.x+4,r.x+r.w-5}) {
                p.rect({x,cy-18,1,36},s.trim);skin_diamond(p,x,cy,2,s.border);
            }
        } else {
            p.rect({cx-9,r.y+3,18,3},{.68f,.23f,.17f,1});
            for(int x:{r.x+3,r.x+r.w-7})p.frame({x,cy-10,4,20},s.trim,1);
        }
    }
    if(!popup&&r.w>240) {
        const int y=r.y+r.h-7,cx=r.x+r.w/2;
        p.rect({cx-44,y,32,1},s.trim);p.rect({cx+12,y,32,1},s.trim);
        skin_diamond(p,cx,y,theme==1?4:3,theme==2?PaintColor{.74f,.24f,.18f,1}:s.trim);
    }
}
inline void paint_modern_inset(UiPaint &p,RectPx r,int theme) {
    const auto s=modern_skin(theme);
    if(theme==0) {
        p.surface(r,s.inset,UiMaterial::Glass);p.rounded_frame(r,{.28f,.62f,.88f,.68f},10,1);return;
    }
    p.surface(r,s.inset,s.material);p.frame(r,s.border,1);
    if(r.w>12&&r.h>12)p.frame(skin_inset(r,3),{s.trim.r,s.trim.g,s.trim.b,.18f},1);
}
inline void paint_modern_selection(UiPaint &p,RectPx r,int theme) {
    const auto s=modern_skin(theme);
    if(theme==0) {
        p.surface(r,s.selection,UiMaterial::GlassHighlight);
        p.rounded_frame(r,{.31f,.74f,1.f,.45f},4,3);
        p.rounded_frame(skin_inset(r,1),{.64f,.91f,1.f,1},3,1);
        return;
    }
    p.rect(r,s.selection);
    if(r.w<8||r.h<8)return;
    p.rect({r.x,r.y,3,r.h},theme==2?PaintColor{.85f,.35f,.25f,1}:s.trim);
    p.rect({r.x+3,r.y,r.w-3,1},{s.trim.r,s.trim.g,s.trim.b,.45f});
    if(theme==1)p.rect({r.x+3,r.y+r.h-1,r.w-3,1},{s.border.r,s.border.g,s.border.b,.45f});
    else if(r.w>30)skin_diamond(p,r.x+r.w-6,r.y+r.h/2,2,s.trim);
}
inline void paint_modern_row_rules(UiPaint &p,RectPx r,int height,int theme) {
    if(height<=0)return;
    const auto skin=modern_skin(theme);
    const PaintColor ink{skin.border.r,skin.border.g,skin.border.b,theme==0?.30f:.24f};
    for(int y=r.y+height-1;y<r.y+r.h;y+=height)p.rect({r.x,y,r.w,1},ink);
}
inline void paint_modern_rule(UiPaint &p,RectPx r,int theme) {
    const auto s=modern_skin(theme);p.rect(r,s.border);
    if(theme==0){p.rect({r.x,r.y+1,r.w,1},{.36f,.72f,1.f,.14f});return;}
    if(r.w>120) {
        const int cx=r.x+r.w/2;
        if(theme==1){skin_diamond(p,cx,r.y,4,s.trim);for(int d:{-1,1})skin_diamond(p,cx+d*11,r.y,2,s.border);}
        else if(theme==2){p.frame({cx-3,r.y-3,7,7},{.74f,.24f,.18f,1},1);}
        else {skin_diamond(p,cx,r.y,3,s.trim);p.rect({cx-18,r.y-2,8,1},s.border);p.rect({cx+10,r.y+2,8,1},s.border);}
    }
}
}
