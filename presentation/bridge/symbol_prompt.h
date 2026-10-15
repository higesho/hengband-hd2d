#pragma once

#include "frame/game_frame.h"

#include <string>
#include <string_view>

namespace presentation {

inline bool is_monster_symbol_prompt(std::string_view text)
{
    const bool english = text.find("monster race (by symbol)") != std::string_view::npos
        || text.find("monster species (by symbol") != std::string_view::npos;
    const bool japanese = text.find(reinterpret_cast<const char *>(u8"モンスター")) != std::string_view::npos
        && text.find(reinterpret_cast<const char *>(u8"文字")) != std::string_view::npos;
    return english || japanese;
}

inline void expose_ascii_symbol_prompt(GameFrame &frame, const std::string &prompt)
{
    frame.menu_open = true;
    frame.prompt = {};
    frame.numeric = {};
    frame.menu_core_cursor = {};
    frame.menu_core_cursors.clear();
    frame.menu_choices.clear();

    for (int key = 33; key <= 126; ++key) {
        MenuChoice choice{};
        choice.line_index = -1;
        choice.key = key;
        choice.key_len = 1;
        choice.label_utf8 = std::string(1, static_cast<char>(key));
        choice.description_utf8 = prompt;
        frame.menu_choices.push_back(std::move(choice));
    }
}

} // namespace presentation
