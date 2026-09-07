/*!
 * @file input_event_adapter.h
 * @brief 抽象入力イベント（`input_event`、v1 §8.3）→ Angband キーバイト列の変換（コア側専用）。
 *
 * キープロトコルの抽象化（憲章 §5.1、Phase 2 P2-2）の受け皿:
 * で「フロントエンドから追い出す」とした Angband キー知識
 * （テンキー方向・KTRL・F キーのマクロトリガ列・askfor 編集プロトコル等）の新しい住処。
 * ここはコア側（bridge）にしか置けない知識であり、ui/ からは include しないこと。
 *
 * 展開結果は同じ操作を `keys` メッセージで送った場合と**バイト単位で等価**でなければ
 * ならない（v1 §8.3 の受け入れ基準）。順序も保存する。
 */
#pragma once

#include "frame/protocol_messages.h"

#include <vector>

namespace presentation {

/*!
 * @brief イベント列をキーバイト列へ展開して out の末尾へ足す。
 * @details 値域検査は decode_input_events 側で済んでいる前提（v1 §8.3）。
 * 展開規則は同節の表のとおり。text の UTF-8 → 内部文字コード変換はここでは
 * **しない**（keys 経路と同じく、term 層の従来の変換に任せる）。
 */
void append_input_event_keys(const InputEventsMessage &message, std::vector<int> &out);

} // namespace presentation
