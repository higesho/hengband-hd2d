/*!
 * @file gb_pad_commands.h
 * @brief 幻想蛮怒のコマンド体系の pad 表（設計 §3 の `gb_pad_commands.cpp` 行）。
 *
 * v1 §8 の `pad_commands` メッセージを組む。M0 は最小表:
 * 移動・階段・拾う・J 特技・持ち物・装備・魔法・休憩・探索・セーブ。
 */
#pragma once

#include "frame/protocol_messages.h"

namespace gb {

/*!
 * @brief いまのキー配列に合わせて `pad_commands` を組む。
 * @details `current_keymap` は `rogue_like_commands` から採る。切り替わったら
 * 呼び直して送り直すこと（v1 §8）。
 */
presentation::PadCommandsMessage build_pad_commands();

} // namespace gb
