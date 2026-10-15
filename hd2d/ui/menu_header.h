#pragma once
#include "ui/modern_skin.h"
#include "app/ui_probe.h"
#include <vector>
namespace hd2d {
inline std::string menu_key_label(int key) {
    if(key<=0||key>127)return {};
    if(key==9)return "Tab";if(key==8)return "Backspace";if(key==13)return "Enter";if(key==27)return "Esc";if(key==32)return "Space";
    if(key<27)return std::string("^")+static_cast<char>('A'+key-1);
    return std::string(1,static_cast<char>(key));
}
inline std::string menu_key_prefix(int key){const auto s=menu_key_label(key);return s.empty()?s:s+") ";}
inline bool menu_has_key_prefix(const std::string &label,int key) {
    const auto code=menu_key_label(key);if(code.empty())return false;
    const auto start=label.find_first_not_of(" \t");if(start==std::string::npos)return false;
    if(label.compare(start,code.size(),code)==0&&start+code.size()<label.size()&&label[start+code.size()]==')')return true;
    if((label[start]=='('||label[start]=='[')&&label.compare(start+1,code.size(),code)==0) {
        const auto end=start+1+code.size();return end<label.size()&&label[end]==(label[start]=='('?')':']');
    }
    return false;
}
inline std::string menu_label_with_key(int key,const std::string &label) {return menu_has_key_prefix(label,key)?label:menu_key_prefix(key)+label;}
struct MenuHeaderLayout {RectPx previous,next;std::vector<RectPx> tabs;};
inline MenuHeaderLayout menu_header_layout(TextOverlay &text,RectPx box,int y,const std::vector<std::string> &labels) {
    MenuHeaderLayout h;const int button=std::min(48,std::max(24,box.w/5));
    h.previous={box.x+6,y,button,28};h.next={box.x+box.w-button-6,y,button,28};
    const int left=h.previous.x+h.previous.w+10,right=h.next.x-10,room=std::max(1,right-left);
    int wanted=0;for(const auto &label:labels)wanted+=text.measure(label)+28;
    const int total=std::min(room,wanted);int x=left+(room-total)/2,used=0;
    for(size_t i=0;i<labels.size();++i) {
        const int width=i+1==labels.size()?total-used:std::max(1,(text.measure(labels[i])+28)*total/std::max(1,wanted));
        h.tabs.push_back({x,y,width,28});x+=width;used+=width;
    }
    return h;
}
// Keep every label at its measured width. Only a single overlong label is clipped.
inline std::pair<int,int> menu_header_visible_range(TextOverlay &text,RectPx box,
    const std::vector<std::string> &labels,int selected,int previous_first=0) {
    if(labels.empty())return {0,0};
    const int button=std::min(48,std::max(24,box.w/5));
    const int room=std::max(1,box.w-2*(button+16));
    selected=std::clamp(selected,0,static_cast<int>(labels.size())-1);
    int best_first=selected,best_count=1;
    for(int first=0;first<=selected;++first) {
        int used=0,count=0;
        for(int i=first;i<static_cast<int>(labels.size());++i) {
            const int width=text.measure(labels[i])+28;
            if(used+width>room)break;
            used+=width;++count;
        }
        if(first+count<=selected)continue;
        if(count>best_count || (count==best_count && std::abs(first-previous_first)<std::abs(best_first-previous_first))) {
            best_first=first;best_count=count;
        }
    }
    return {best_first,best_count};
}
inline void paint_shoulder_button(UiPaint &p,TextOverlay &text,RectPx r,const char *label,int theme,bool enabled) {
    const auto skin=modern_skin(theme);auto fill=skin.selection,edge=skin.trim;auto ink=skin.strong;
    if(!enabled){fill.a=.38f;edge.a=.35f;ink=skin.muted;}
    p.rect({r.x+5,r.y,r.w-10,r.h},fill);p.rect({r.x,r.y+6,r.w,r.h-12},fill);
    p.triangle(float(r.x),float(r.y+6),float(r.x+5),float(r.y),float(r.x+5),float(r.y+6),fill);
    p.triangle(float(r.x+r.w-5),float(r.y),float(r.x+r.w),float(r.y+6),float(r.x+r.w-5),float(r.y+6),fill);
    p.rounded_frame(r,edge,6,2);p.rect({r.x+8,r.y+3,r.w-16,1},{edge.r,edge.g,edge.b,.35f});
    text.draw(r.x+(r.w-text.measure(label))/2,r.y+(r.h-text.cell_h())/2,label,ink,r.w-4);
}
inline MenuHeaderLayout draw_menu_header(UiPaint &p,TextOverlay &text,RectPx box,int y,const std::vector<std::string> &labels,int selected,int theme,bool enabled=true) {
    const auto h=menu_header_layout(text,box,y,labels);const auto skin=modern_skin(theme);
    if(ui_probe().enabled()) {
        //! 検査用の問い合わせ：押せる矩形（見出しの矩形は論理座標。字の倍率をかけて窓の画素にする）。
        const float s=text.layout_scale();
        const auto px=[s](const RectPx &r){return ProbeRect{int(r.x*s+.5f),int(r.y*s+.5f),int(r.w*s+.5f),int(r.h*s+.5f)};};
        ui_probe().button("LB",px(h.previous),enabled,false,"tab-prev",false,"tab-prev");
        ui_probe().button("RB",px(h.next),enabled,false,"tab-next",false,"tab-next");
        for(size_t i=0;i<labels.size();++i)ui_probe().button(labels[i],px(h.tabs[i]),enabled,static_cast<int>(i)==selected,"tab",false,"tab:"+std::to_string(i));
    }
    paint_shoulder_button(p,text,h.previous,"LB",theme,enabled);paint_shoulder_button(p,text,h.next,"RB",theme,enabled);
    for(size_t i=0;i<labels.size();++i) {
        const auto &r=h.tabs[i];const int available=std::max(1,r.w-16);
        const auto label=labels[i].substr(0,text.fit_bytes(labels[i],available));
        text.draw(r.x+(r.w-text.measure(label))/2,r.y+6,label,static_cast<int>(i)==selected?skin.strong:skin.muted,available);
        if(static_cast<int>(i)==selected)p.rect({r.x+8,r.y+r.h-2,std::max(1,r.w-16),2},skin.trim);
        if(i&&r.w>10)p.rect({r.x,r.y+6,1,14},{skin.border.r,skin.border.g,skin.border.b,.55f});
    }
    return h;
}
}
