/*!
 * @file sq_pad_commands.h
 * @brief パッドへ割り当てられるコマンドの表（v1 §8）。
 *
 * 中身の根拠は `sq_pad_commands.cpp` の冒頭表。
 *
 * @note Sil-Q のキー配置は 4 つ（Sil・Sil＋hjkl・Angband 風・Angband 風＋hjkl）。
 * 表の字は Sil の標準の配置の字で持ち、送るときに**いまの配置で同じ動作を起こすキー**へ
 * 引き直す（`sq_pad_commands.cpp` の `resolve_keys`。2026-10-07 までは標準の字のまま送っていて、
 * hjkl の配置では「見る」が東へ歩いた）。`seq_rogue` には `seq_original` と同じ列を入れ、
 * `current_keymap` は hjkl の配置なら `"rogue"`、ほかは `"original"`。
 */
#pragma once

#include "frame/protocol_messages.h"

namespace sq {

//! M0 の最小表を組む。
presentation::PadCommandsMessage build_pad_commands();

} // namespace sq
