/*!
 * @file tb_window_extras.cpp
 * @brief `tb_window_extras.h` の実装。
 */
#include "tb_window_extras.h"

#include <string>

namespace tb {

namespace {

constexpr int kElementKey = 'E';

//! 直近に足した操作が有効か（窓が組み替わるたびに更新する）。
bool g_map_element_action = false;

bool mentions_element_map(const GameFrame &frame)
{
    for (const auto &line : frame.menu_term_lines) {
        const std::string &text = line.text_utf8;
        if (text.find(reinterpret_cast<const char *>(u8"エレメント分布")) != std::string::npos
            || text.find("display elements") != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

void apply_window_extras(GameFrame &frame)
{
    g_map_element_action = false;
    ModernWindow &w = frame.modern;
    if (w.kind != "dialog" || w.stage != "detail" || w.content_type != "map" || !mentions_element_map(frame)) {
        return;
    }
    const bool ja = w.language == "ja";
    for (const auto &action : w.actions) {
        if (action.key == kElementKey) {
            return;
        }
    }
    w.actions.push_back({ "key:" + std::to_string(kElementKey),
        ja ? std::string(reinterpret_cast<const char *>(u8"エレメントの分布を表示")) : std::string("Show element distribution"), "", true, kElementKey });
    g_map_element_action = true;
}

bool window_extras_input(const presentation::InputEventWire &event, std::vector<int> &keys, presentation::NativeModernMenu &menu)
{
    if (!g_map_element_action || event.e != "ui_action" || event.action != "window_action"
        || event.target != "key:" + std::to_string(kElementKey)) {
        return false;
    }
    // 版・要求番号の管理は NativeModernMenu に任せる（同じ要求を「続ける」にして通し、受理されたときだけキーを返す）。
    presentation::InputEventWire ledger = event;
    ledger.action = "continue";
    ledger.target.clear();
    std::vector<int> accepted;
    menu.input(ledger, accepted);
    if (!accepted.empty()) {
        keys.push_back(kElementKey);
    }
    return true;
}

} // namespace tb
