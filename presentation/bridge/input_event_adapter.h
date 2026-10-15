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

#include <string>
#include <vector>

struct NumericInput; // frame/game_frame.h

namespace presentation {

/*!
 * @brief イベント列をキーバイト列へ展開して out の末尾へ足す。
 * @details 値域検査は decode_input_events 側で済んでいる前提（v1 §8.3）。
 * 展開規則は同節の表のとおり。text の UTF-8 → 内部文字コード変換はここでは
 * **しない**（keys 経路と同じく、term 層の従来の変換に任せる）。
 */
void append_input_event_keys(const InputEventsMessage &message, std::vector<int> &out);

/*!
 * @brief 行が変愚系の「繰り返し回数」の入力（`0` を押した後の `回数: 12` / `Count: 12`）か。
 * @details 変愚・短愚（`src/io/input-key-requester.cpp` の `input_repeat_num()`）、
 * 幻想・Frox（`util.c` の `request_command()`）は、`0` の後の数字を `askfor` ではなく
 * **1 キーずつ `inkey()` で読む**。数字と退格（0x08／0x7F）だけを受け、それ以外のキーは
 * 回数の終わり兼コマンドとして扱う。行は `prt("回数: %d")` で行 0 に出る。
 * @param line 行の字（UTF-8。行末の空白はあってよい）
 * @param value いまの回数（空なら 0）
 * @param prompt_len 値の手前までのバイト数（`回数: ` の長さ）
 * @param value_len 値のバイト数（空なら 0）
 * @return 回数の入力の行なら true
 */
bool parse_repeat_count_line(const std::string &line, int &value, int &prompt_len, int &value_len);

/*!
 * @brief 行が回数の入力なら、数量の欄と同じ枠（`NumericInput`）を埋めて旗を立てる。
 * @details 範囲は原作どおり 0〜9999（0 のまま決めると原作が 99 回にする）。
 * 行 0 を読む各コアの関数（変愚・短愚・幻想・Frox）が、他の問いより先に呼ぶ。
 * 当たらなければ `out` は触らず旗を下ろす。
 * @return 回数の入力なら true
 */
bool fill_repeat_count_input(const std::string &line, NumericInput &out);

/*!
 * @brief いま画面へ送った画が「繰り返し回数」の入力か（ゲームスレッドが画を作るたびに書く）。
 * @details 立っている間は `set_number` の展開を回数の入力向け（退格＋数字）に替える。
 * 数量の欄向けの展開（`KTRL('B')`・`KTRL('D')`）は、回数の入力では**数字以外なので
 * 回数を打ち切り、そのキーをコマンドとして走らせてしまう**。
 * 受信スレッドが読むので atomic にしてある。
 */
void set_repeat_count_input_active(bool active);
bool repeat_count_input_active();

} // namespace presentation
