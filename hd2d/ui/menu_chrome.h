#pragma once
#include "frame/game_frame.h"
#include <algorithm>
#include <string>
#include <vector>
namespace hd2d {
// Remove terminal-drawn decoration only. Keep byte offsets and display columns
// so the core's selection spans, text editing and hit testing still agree.
inline void erase_old_menu_chrome(std::vector<std::string> &lines,const std::vector<MenuChoice> &cursors) {
    bool box=false;
    for(auto &line:lines) {
        const bool rule=line.find_first_not_of(" +-=/|\t")==std::string::npos;
        for(size_t i=0;i<line.size();) {
            if(line[i]!='-'&&line[i]!='='){++i;continue;}
            const char mark=line[i];size_t end=i;while(end<line.size()&&line[end]==mark)++end;
            const bool left=i>0&&line[i-1]=='+';
            const bool right=end<line.size()&&line[end]=='+';
            if(end-i>=3&&(left||right||rule)) {
                box=box||left||right;
                std::fill(line.begin()+(left?i-1:i),line.begin()+(right?end+1:end),' ');
            }
            i=end;
        }
    }
    if(box)for(auto &line:lines)for(size_t i=0;i<line.size();++i)
        if(line[i]=='|'&&(i==0||line[i-1]!='(')&&(i+1==line.size()||line[i+1]!=')'))line[i]=' ';
    for(const auto &cursor:cursors) {
        if(cursor.line_index<0||cursor.line_index>=static_cast<int>(lines.size())||cursor.span_begin<0)continue;
        auto &line=lines[cursor.line_index];const size_t p=cursor.span_begin;
        if(p>=line.size())continue;
        if(line.compare(p,3,"》")==0)line.replace(p,3,"　");
        else if(line.compare(p,2,"> ")==0)line.replace(p,2,"  ");
    }
}
inline std::vector<std::string> modern_menu_text(const GameFrame &frame) {
    std::vector<std::string> lines;for(const auto &line:frame.menu_term_lines)lines.push_back(line.text_utf8);
    auto cursors=frame.menu_core_cursors;
    if(cursors.empty()&&frame.menu_core_cursor.line_index>=0)cursors.push_back(frame.menu_core_cursor);
    erase_old_menu_chrome(lines,cursors);return lines;
}
}
