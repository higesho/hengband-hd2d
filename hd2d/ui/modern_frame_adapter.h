#pragma once
#include "bridge/native_modern.h"
namespace hd2d {
// A mode change can leave one old-format frame in flight. Interpret that frame
// at the receiving boundary too; never paint a terminal inside a modern panel.
// Its controls become available when the core returns its own revision/model.
inline void prepare_modern_menu_frame(GameFrame &frame,bool enabled,const std::string &language) {
    if(!enabled)return;
    if(!frame.modern.stage.empty()) {
        if(frame.modern.version>=5||!frame.modern.content_type.empty())return;
        const auto original=frame.modern;frame.modern.language=language;
        presentation::interpret_modern_content(frame,frame.modern);
        if(original.stage=="cursor")frame.modern.rows=original.rows;
        else for(auto &row:frame.modern.rows) {
            const auto previous=std::find_if(original.rows.begin(),original.rows.end(),[&](const auto &r){return r.id==row.id;});
            if(previous!=original.rows.end())row.actions=previous->actions;
        }
        frame.modern.cursor_target.clear();
        for(auto &block:frame.modern.content)for(auto &cell:block.cells)cell.target.clear();
        // Older models keep their own request IDs and supported input actions.
        frame.modern.version=original.version;frame.modern.input_ready=true;
        return;
    }
    if(frame.prompt.lightweight)return;
    if(frame.menu_term_lines.empty()&&!frame.menu_open&&!frame.numeric.active&&!frame.text_input_active&&frame.prompt.choices.empty())return;
    if(frame.awaiting_command&&!frame.menu_open&&frame.prompt.choices.empty()&&!frame.numeric.active&&!frame.text_input_active)return;
    auto captured=frame;captured.modern={};captured.command_menu_keys.clear();captured.menu_open=true;
    presentation::NativeModernMenu adapter;adapter.set_enabled(true);adapter.set_language(language);adapter.capture(captured);
    captured.modern.input_ready=false;frame.modern=std::move(captured.modern);
}
}
