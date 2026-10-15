#pragma once
#include "frame/game_frame.h"
#include <string>
#include <string_view>
namespace presentation {
inline std::string strip_message_repeat_suffix(std::string text) {
    size_t start=std::string::npos,first=0,last=text.size();
    if(!text.empty()&&text.back()==')') {
        start=text.rfind(" (");
        if(start!=std::string::npos) {
            first=start+2;last=text.size()-1;
            if(text.compare(first,2,"\xc3\x97")==0)first+=2;
            else if(first<last&&text[first]=='x')++first;
            else start=std::string::npos;
        }
    } else if(!text.empty()&&text.back()=='>') {start=text.rfind(" <x");first=start==std::string::npos?0:start+3;last=text.size()-1;}
    if(start!=std::string::npos&&first<last&&text.find_first_not_of("0123456789",first)>=last)text.resize(start);
    return text;
}
inline std::string modern_prompt_text(const GameFrame &frame) {
    std::string prefix;
    if(!frame.prompt.choices.empty()&&frame.prompt.line_index>0&&frame.prompt.line_index<int(frame.menu_term_lines.size())) {
        int first=frame.prompt.line_index;
        while(first>0&&!frame.menu_term_lines[first-1].text_utf8.empty())--first;
        for(int i=first;i<frame.prompt.line_index;++i)prefix+=frame.menu_term_lines[i].text_utf8+"\n";
    }
    return prefix+frame.prompt.text_utf8;
}
inline bool message_more_suffix(std::string_view text) {
    const auto end=text.find_last_not_of(" \t\r\n");if(end==std::string_view::npos)return false;text=text.substr(0,end+1);
    return text.ends_with("-more-")||text.ends_with(reinterpret_cast<const char*>(u8"-続く-"))
        ||text.ends_with(reinterpret_cast<const char*>(u8"［続く］"));
}
inline bool message_more_pending(const GameFrame &frame) {
    // Multi-line message owners supply their complete current text here.
    if(frame.prompt.choices.empty()&&message_more_suffix(frame.prompt.text_utf8))return true;
    // A reference to the marker inside prose is not an input wait.
    for(const auto &line:frame.menu_term_lines)if(line.source_row==0&&message_more_suffix(line.text_utf8))return true;
    return false;
}
}
