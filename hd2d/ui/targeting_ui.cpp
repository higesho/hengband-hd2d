#include "ui/targeting_ui.h"
#include "ui/ui_paint.h"
#include "ui/menu_scale.h"
#include "ui/modern_skin.h"
#include "render/text_overlay.h"
#include "i18n/lang.h"

namespace hd2d {
void TargetingUi::draw(UiPaint &paint,TextOverlay &text,const UiLayout &physical,int theme)const {
    buttons_.clear();if(!active())return;
    // Flush map glyphs before painting an opaque toolbar over the scene.
    paint.flush();text.flush();
    MenuScaleScope scale(paint,text,menu_font_scale(font_level_));const auto layout=menu_logical_layout(physical,scale.scale,&text);
    const auto tr=[](const char *ja,const char *en){return std::string(i18n::current()=="ja"?ja:en);};
    const auto skin=modern_skin(theme);const auto area=layout.scene.w>0?layout.scene:layout.content;
    // Mode "look" (design doc section 8.5.1): a "detail" button is inserted
    // instead of Confirm: a look session never places a target, so there is
    // no "Finish" button (the cancel key ends it) and the decision key opens
    // the detail (AH-47).
    const bool look=state_.mode=="look";
    const bool cycle=state_.mode!="fixed"&&!state_.candidates.empty();
    const bool has_detail=look&&!state_.target_name.empty();
    struct Button{std::string label,action;int value{};bool enabled{};};
    std::vector<Button> plan{
        {tr("LB 前の対象","LB Previous"),"aim_cycle",0,cycle},
        {tr("RB 次の対象","RB Next"),"aim_cycle",1,cycle},
    };
    if(look)plan.push_back({tr("詳細","Detail"),"aim_recall",0,has_detail&&!pending_});
    if(!look)plan.push_back({tr("決定","Confirm"),"aim_confirm",0,state_.can_confirm&&!pending_});
    if(!state_.extras.empty())plan.push_back({tr("操作 (Start/F4)","Actions (Start/F4)"),"aim_extras",0,!pending_});
    plan.push_back({tr("取消","Cancel"),"aim_cancel",0,true});
    const int width=std::max(1,area.w-24),columns=width>=500?4:2,rows=(int(plan.size())+columns-1)/columns;
    const int unit=text.cell_h()+4;const bool reason=!notice_.empty()||!state_.reason.empty();
    const int height=rows*36+unit*(reason?3:2)+24;
    const RectPx box{area.x+12,area.y+std::max(0,area.h-height-12),width,height};
    toolbar_=menu_scaled_rect(box,scale.scale);paint_modern_window(paint,box,theme);
    std::string label=state_.label.empty()?(look?tr("見る","Look"):tr("狙いを指定","Choose target")):state_.label;
    if(state_.stochastic)label+="  "+tr("範囲にランダム性あり","Variable coverage");
    if(!state_.effect_known)label+="  "+tr("効果は未判明","Effect not identified");
    // "kind" (design doc section 8.5.1): a short label for whatever the
    // cursor is currently on, ahead of its one-line "detail" (below, folded
    // into target_name by the core -- see an_look_preview()).
    if(look) {
        const auto found=std::find_if(state_.candidates.begin(),state_.candidates.end(),
            [&](const auto &c){return c.point==state_.selected&&c.id==state_.selected_id;});
        if(found!=state_.candidates.end()) {
            const auto &kind=found->kind;
            const char *ja=kind=="monster"?"敵":kind=="object"?"アイテム":kind=="feature"?"地形":kind=="trap"?"罠":nullptr;
            const char *en=kind=="monster"?"Creature":kind=="object"?"Item":kind=="feature"?"Terrain":kind=="trap"?"Trap":nullptr;
            if(ja)label+="  ["+tr(ja,en)+"]";
        }
    }
    text.draw(box.x+10,box.y+8,label,skin.text,box.w-20);
    auto target=state_.target_name.empty()?tr("指定位置","Selected point"):state_.target_name;
    target+="  "+tr("距離 ","Distance ")+std::to_string(state_.distance);
    text.draw(box.x+10,box.y+8+unit,target,skin.text,box.w-20);
    if(reason)text.draw(box.x+10,box.y+8+unit*2,notice_.empty()?state_.reason:notice_,skin.muted,box.w-20);
    const int top=box.y+12+unit*(reason?3:2),cell_width=(box.w-20)/columns;
    for(size_t i=0;i<plan.size();++i) {
        const auto &b=plan[i];const bool confirm_btn=b.action=="aim_confirm"||(look&&b.action=="aim_recall"); // look: the decision key opens the detail (AH-47)
        const RectPx button{box.x+10+int(i%columns)*cell_width,top+int(i/columns)*36,cell_width-6,30};
        paint_modern_inset(paint,button,theme);if(confirm_btn&&b.enabled)paint_modern_selection(paint,button,theme);
        text.draw(button.x+8,button.y+6,b.label,b.enabled?(confirm_btn?skin.selected:skin.text):skin.muted,button.w-16);
        buttons_.push_back({menu_scaled_rect(button,scale.scale),b.action,b.value,b.enabled});
    }
    // The extra operations (AH-54): a list above the toolbar, cursor and decision key like every other list.
    if(extras_open_&&!state_.extras.empty()) {
        const int count=int(state_.extras.size()),row_h=text.cell_h()+14;
        const int list_h=count*row_h+16,list_w=std::min(width,std::max(320,box.w/2));
        const RectPx list{box.x,std::max(area.y,box.y-list_h-6),list_w,list_h};
        paint_modern_window(paint,list,theme);
        for(int i=0;i<count;++i) {
            const auto &e=state_.extras[i];
            const RectPx row{list.x+8,list.y+8+i*row_h,list.w-16,row_h-4};
            paint_modern_inset(paint,row,theme);
            const bool current=i==extras_cursor_;
            if(current&&e.enabled)paint_modern_selection(paint,row,theme);
            //! Ctrl＋英字のキー（Angband のローグライクのキー配置の「無視する」= ^D）は ^ を付けて見せる。
            const std::string key_name=(e.key<32)?std::string("^")+char('@'+e.key):std::string(1,char(e.key));
            const std::string line="("+key_name+") "+e.label;
            text.draw(row.x+8,row.y+(row.h-text.cell_h())/2,line,e.enabled?(current?skin.selected:skin.text):skin.muted,row.w-16);
            buttons_.push_back({menu_scaled_rect(row,scale.scale),"aim_extra",e.key,e.enabled});
        }
        const auto scaled=menu_scaled_rect(list,scale.scale);
        // (The names must differ from the function-scope `top` above: C4456.)
        const int union_x0=std::min(toolbar_.x,scaled.x),union_y0=std::min(toolbar_.y,scaled.y);
        const int union_x1=std::max(toolbar_.x+toolbar_.w,scaled.x+scaled.w),union_y1=std::max(toolbar_.y+toolbar_.h,scaled.y+scaled.h);
        toolbar_=RectPx{union_x0,union_y0,union_x1-union_x0,union_y1-union_y0};
    }
}

void TargetingUi::draw_map(UiPaint &paint,const TargetingMapProjection &map,double time)const {
    if(!visible()||map.cell_w<=0||map.cell_h<=0)return;
    const auto rect=[&](int x,int y,int w,int h,PaintColor color) {
        const int right=std::min(x+w,map.x+map.cols*map.cell_w),bottom=std::min(y+h,map.y+map.rows*map.cell_h);
        x=std::max(x,map.x);y=std::max(y,map.y);
        if(right>x&&bottom>y)paint.rect({x,y,right-x,bottom-y},color);
    };
    const auto x=[&](float gx){return map.x+(gx-map.cam_x+map.cols/2)*map.cell_w;};
    const auto y=[&](float gy){return map.y+(gy-map.cam_y+map.rows/2)*map.cell_h;};
    const auto visual=build_targeting_visual(state_,{0,0,100},{},time);
    for(const auto &q:visual.area) {
        const int left=int(x(q.center.x-.5f)),top=int(y(q.center.y-.5f));
        const PaintColor color{q.r,q.g,q.b,q.a};
        if(q.style==2.f) {
            // Sparse diagonal hatching distinguishes possible random coverage.
            for(int row=0;row<map.cell_h;++row)for(int col=(row%6);col<map.cell_w;col+=6)
                rect(left+col,top+row,2,1,{q.r,q.g,q.b,.3f});
        } else rect(left,top,map.cell_w,map.cell_h,color);
    }
    for(const auto &q:visual.dots)
        rect(int(x(q.center.x))-1,int(y(q.center.y))-1,2,2,{q.r,q.g,q.b,q.a});
    for(size_t i=0;i<state_.markers.size();++i) {
        const auto &m=state_.markers[i];const auto &q=visual.reticles[i];
        const float cx=x(m.point.x+.5f),cy=y(m.point.y+.5f);
        const float rx=map.cell_w*.43f,ry=map.cell_h*.43f;
        const PaintColor color{q.r,q.g,q.b,q.a};
        constexpr float tau=6.283185307f;
        for(int n=0;n<64;++n) {
            if(!m.valid&&(n/4)%2)continue;
            const float a=tau*n/64;
            rect(int(std::round(cx+rx*std::cos(a))),int(std::round(cy+ry*std::sin(a))),1,1,color);
        }
        for(int sign:{-1,1}) {
            rect(int(cx+sign*rx*.62f)-(sign<0?int(rx*.3f):0),int(cy),std::max(1,int(rx*.3f)),1,color);
            rect(int(cx),int(cy+sign*ry*.62f)-(sign<0?int(ry*.3f):0),1,std::max(1,int(ry*.3f)),color);
        }
        if(m.selected)for(int sx:{-1,1})for(int sy:{-1,1}) {
            const int left=int(cx+sx*rx),top=int(cy+sy*ry);
            const PaintColor selection{1,1,1,.95f};
            rect(left-(sx>0?2:0),top,3,1,selection);
            rect(left,top-(sy>0?2:0),1,3,selection);
        }
    }
}
}
