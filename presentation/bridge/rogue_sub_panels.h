/*!
 * @file rogue_sub_panels.h
 * @brief Rogue 系のコア（RogueClone2 / Rogue 3.6 / Rogue 5.4）のサブパネルの種類。
 * @details Rogue 系はコアのサブウインドウを持たないので、画面側の既定の描き方 5 種
 * （メッセージ / 装備 / 視界の敵 / キャラクター / 持ち物）を種類として申告し、
 * 枠ごとに選ばせる（2026-09-23。気づいたこと「Rogue のサブパネルの割り当てが変更できない」）。
 * 番号は `kBuiltinSubPanelKindBase` から（`frame/game_frame.h`）。中身はフレームの既存の欄から
 * 画面が描くので、コアは選ばれた番号を実効値として返すだけでよい。
 */
#pragma once

#include "frame/game_frame.h"
#include "frame/protocol_messages.h"

#include <array>

namespace presentation {

/*!
 * サブパネルの種類の一覧（v1 §8.1）。名前は画面の言語に合わせる。日本語の名前は従来のまま。
 * 画面は届くたびに選択肢を入れ替えるので、言語が替わったらアダプタが送り直す。
 * @param english 画面の言語が日本語以外（英語）のとき true
 */
inline SubPanelKindsMessage rogue_sub_panel_kinds(bool english = false)
{
    SubPanelKindsMessage message;
    //! 先頭は必ず「UI 既定」（v1 §8.1）。枠ごとの既定の中身のまま。
    message.entries.push_back(SubPanelKindWireEntry{ -1, english ? "UI default" : "UI 既定" });
    static constexpr const char *kLabelsJa[kBuiltinSubPanelKindCount] = {
        "メッセージ", "装備", "視界の敵", "キャラクター", "持ち物",
    };
    static constexpr const char *kLabelsEn[kBuiltinSubPanelKindCount] = {
        "Messages", "Equipment", "Visible enemies", "Character", "Inventory",
    };
    const char *const *labels = english ? kLabelsEn : kLabelsJa;
    for (int i = 0; i < kBuiltinSubPanelKindCount; ++i) {
        message.entries.push_back(SubPanelKindWireEntry{ kBuiltinSubPanelKindBase + i, labels[i] });
    }
    return message;
}

//! 申告していない番号（前の版の変愚用の番号など）は「UI 既定」へ落とす。
inline int rogue_sub_panel_effective_kind(int kind)
{
    return ((kind >= kBuiltinSubPanelKindBase) && (kind < kBuiltinSubPanelKindBase + kBuiltinSubPanelKindCount))
        ? kind : -1;
}

/*!
 * 画面が送ってきた希望をフレームへ実効値として写す。画面は返ってきた値と送った値が
 * そろうまで自分の設定へ吸い上げない（`hd2d_app.cpp` の往復の確認）ので、
 * UI 既定（-1）の枠も含めて全部の枠を書く。
 */
inline void apply_rogue_sub_panel_kinds(GameFrame &frame, const std::array<int, kSubPanelCount> &kinds)
{
    for (int i = 0; i < kSubPanelCount; ++i) {
        SubPanelContent &content = frame.sub_panels[static_cast<std::size_t>(i)];
        content.kind = rogue_sub_panel_effective_kind(kinds[static_cast<std::size_t>(i)]);
        content.title_utf8.clear();
        content.lines.clear();
    }
}

} // namespace presentation
