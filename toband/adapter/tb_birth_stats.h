/*!
 * @file tb_birth_stats.h
 * @brief 誕生の能力値の割り振り（`birth.c` の `player_birth_aux_2()`）を、新しい画面で操作できる窓にする。
 *
 * 原作の画面は 6 つの能力値の表で、`2/8/j/k` で行を移し、`4/6/h/l` で値を増減し、`Enter` で決める。
 * 新しい画面では左の一覧に能力値の行（− と ＋ の対）を並べ、上下で行を選び、左右で値を変え、決定で次へ進む
 * （初期オプションの窓と同じ操作。Sil-Q の能力値の窓 `sil_allocation` と同じ窓の種類）。
 * 数値はすべて原作が画面に描いたものを読む。コストの計算・上限・紙幣への変換は原作のまま（ここでは決めない）。
 */
#pragma once

#include "bridge/input_event_adapter.h"
#include "bridge/native_modern.h"
#include "frame/game_frame.h"

#include <vector>

namespace tb {

/*!
 * @brief 能力値の割り振りの画面なら、`frame.modern`（`NativeModernMenu::capture()` が作った窓）を組み替える。
 * @return 組み替えたら true。そうでなければ false で、窓には触らない
 * @details 画面の見分けは Term ミラーの形で行う: 行 11 に `2/8/j/k` と `4/6/h/l`（操作の案内）があり、
 * 行 14〜19 が能力値の 6 行。日本語でも英語でも同じ形。
 */
bool apply_birth_stats_window(GameFrame &frame);

/*!
 * @brief 画面から届いた `ui_action` を、能力値の割り振りに効くキーへ翻訳する。
 * @param menu 窓の版・要求番号の管理を持つ `NativeModernMenu`（受理の記録に使う）
 * @return この窓の操作として扱ったら true（`keys` に積んだ分が原作へ届く。拒否のときは何も積まない）
 */
bool birth_stats_input(const presentation::InputEventWire &event, std::vector<int> &keys, presentation::NativeModernMenu &menu);

//! 能力値の窓の組み立てと、操作の翻訳の自己検査。失敗の数を返す。
int birth_stats_selftest();

} // namespace tb
