/*!
 * @file fc_pad_commands.h
 * @brief Frox のコマンド体系の pad 表（設計 §3 の `fc_pad_commands.cpp` 行）。
 *
 * v1 §8 の `pad_commands` メッセージを組む。中身はコアの `menu_info` の平坦化
 * （`fc_read_pad_commands()`）で、**表を手で持たない**——Frox は現役なので、
 * 先方がコマンドを増やしたら黙って追従する（設計 §1 制約 3）。
 */
#pragma once

#include "frame/protocol_messages.h"

namespace fc {

/*!
 * @brief いまのキー配列に合わせて `pad_commands` を組む。
 * @details `current_keymap` は `rogue_like_commands` から採る。切り替わったら
 * 呼び直して送り直すこと（v1 §8）。
 */
presentation::PadCommandsMessage build_pad_commands();

} // namespace fc
