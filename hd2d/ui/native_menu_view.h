#pragma once
#include "ui/menu_chrome.h"
#include "render/text_overlay.h"
namespace hd2d {
// A display-only viewport. Choice vector order and native keys never change.
inline GameFrame native_menu_view(const GameFrame &source,TextOverlay &text,int first,int column,int rows,int width) {
    auto view=source;const auto cleaned=modern_menu_text(source);
    std::vector<int> skips(source.menu_term_lines.size(),0);
    for(size_t i=0;i<view.menu_term_lines.size();++i) {
        auto &line=view.menu_term_lines[i];const auto &raw=cleaned[i];
        // fit_bytes(width=0) means unlimited, not zero bytes to skip.
        const int skip=column>0?static_cast<int>(text.fit_bytes(raw,column*text.cell_w())):0;skips[i]=skip;
        const int take=static_cast<int>(text.fit_bytes(raw.substr(skip),width));
        const auto crop=[&](int &begin,int &length) {
            const int end=std::clamp(begin+length-skip,0,take);begin=std::clamp(begin-skip,0,take);length=std::max(0,end-begin);
        };
        line.text_utf8=raw.substr(skip,take);crop(line.highlight_begin,line.highlight_len);
        for(auto &span:line.color_spans)crop(span.begin,span.len);
    }
    const auto choice=[&](MenuChoice &c){
        const int original=c.line_index;c.line_index-=first;
        if(original<0||original>=static_cast<int>(skips.size())){c.line_index=-1;return;}
        const int skip=skips[original],length=static_cast<int>(view.menu_term_lines[original].text_utf8.size());
        const int end=std::clamp(c.span_begin+c.span_len-skip,0,length);
        c.span_begin=std::clamp(c.span_begin-skip,0,length);c.span_len=std::max(0,end-c.span_begin);
        const int key_end=std::clamp(c.key_begin+c.key_len-skip,0,length);
        c.key_begin=std::clamp(c.key_begin-skip,0,length);c.key_len=std::max(0,key_end-c.key_begin);
    };
    for(auto &c:view.menu_choices)choice(c);for(auto &c:view.menu_core_cursors)choice(c);choice(view.menu_core_cursor);
    if(view.prompt.line_index>=0&&view.prompt.line_index<static_cast<int>(skips.size())) {
        const int skip=skips[view.prompt.line_index];view.prompt.line_index-=first;
        for(auto &c:view.prompt.choices)c.begin=std::max(0,c.begin-skip);
    }
    if(view.numeric.line_index>=0&&view.numeric.line_index<static_cast<int>(skips.size())) {
        view.numeric.value_begin=std::max(0,view.numeric.value_begin-skips[view.numeric.line_index]);view.numeric.line_index-=first;
    }
    view.menu_term_curs_row-=first;view.menu_term_curs_col-=column;
    const int end=std::min(static_cast<int>(view.menu_term_lines.size()),first+rows);
    view.menu_term_lines=std::vector<TermMirrorLine>(view.menu_term_lines.begin()+first,view.menu_term_lines.begin()+end);
    return view;
}
}
