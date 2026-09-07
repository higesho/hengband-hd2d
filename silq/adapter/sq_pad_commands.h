/*!
 * @file sq_pad_commands.h
 * @brief パッドへ割り当てられるコマンドの表（v1 §8）。
 *
 * 中身の根拠は `sq_pad_commands.cpp` の冒頭表。
 *
 * @note Sil-Q に**キー配列の切り替えは無い**（`rogue_like_commands` に当たる
 * オプションが存在しない）。`seq_rogue` には `seq_original` と同じ列を入れ、
 * `current_keymap` は常に `"original"` を名乗る。
 */
#pragma once

#include "frame/protocol_messages.h"

namespace sq {

//! M0 の最小表を組む。
presentation::PadCommandsMessage build_pad_commands();

} // namespace sq
