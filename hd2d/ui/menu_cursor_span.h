#pragma once
#include "render/text_overlay.h"
#include "ui/ui_layout.h"
#include <algorithm>
#include <string>
namespace hd2d {
// The span uses UTF-8 byte offsets in this displayed row. Pixel widths come
// from the same font as the text, including Japanese and enlarged menus.
inline RectPx menu_cursor_span_rect(TextOverlay &text,const std::string &line,const RectPx &clip,int origin_x,int begin,int length) {
    if(begin<0||length<=0||size_t(begin)>line.size()||size_t(length)>line.size()-size_t(begin))return {};
    int end=begin+length;
    const auto boundary=[&](int at){return size_t(at)==line.size()||(static_cast<unsigned char>(line[at])&0xc0)!=0x80;};
    if(!boundary(begin)||!boundary(end))return {};
    while(begin<end){if(line[begin]==' '||line[begin]=='\t'){++begin;continue;}if(end-begin>=3&&line.compare(begin,3,"　")==0){begin+=3;continue;}break;}
    while(end>begin){if(line[end-1]==' '||line[end-1]=='\t'){--end;continue;}if(end-begin>=3&&line.compare(end-3,3,"　")==0){end-=3;continue;}break;}
    if(begin==end)return {};
    const int x=origin_x+text.measure(line.substr(0,begin));
    const int width=text.measure(line.substr(begin,end-begin));
    const int left=std::max(clip.x,x-3),right=std::min(clip.x+clip.w,x+width+3);
    return {left,clip.y,std::max(0,right-left),clip.h};
}
}
