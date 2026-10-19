/*!
 * @file tb_skill_level.h
 * @brief 「現在のスキル」の画面（`cmd4.c` の `do_cmd_skill_level()`。知識 `~` の 2 ページ目 `b`）を、新しい画面の矢印で操作できる窓にする。
 *
 * 原作の画面は、左に分類（武器・その他・魔法）、右にその分類の技能とレベルを並べた 2 列の一覧で、
 * キーは `browser_cursor()`（知識のモンスター・アイテムの一覧と同じ関数）: `4`/`6` で列（分類・技能）を替え、
 * `2`/`8` でその列の中を動かし、斜め（`3`/`9`）で 1 画面ぶん動かす。ESC で閉じる。
 * 新しい画面は、これを共用部分の知識の一覧（`modern_browser_content()`。題が `知識 -` で始まる画面だけを見分ける）と
 * 同じ形の窓（`browser`）にするが、TOband のこの画面は題・見出し・操作の案内の形が違うので、見分けられず、
 * 「決定だけの詳細画面」になり矢印が原作へ届かなかった（キーボードの `2/8/4/6` だけが効いた）。
 * 共用部分は変えずに、アダプタが `capture()` の後で窓を組み替える（能力値の窓 `tb_birth_stats.cpp` と同じ作り）。
 * 表示する字（分類の名前・技能の名前・レベル）は原作が描いたものを読むだけで、遊びの規則は変えない。
 */
#pragma once

#include "bridge/input_event_adapter.h"
#include "bridge/native_modern.h"
#include "frame/game_frame.h"

#include <vector>

namespace tb {

/*!
 * @brief 「現在のスキル」の画面なら、`frame.modern`（`NativeModernMenu::capture()` が作った窓）を組み替える。
 * @return 組み替えたら true。そうでなければ false で、窓には触らない
 * @details 画面の見分けは Term ミラーの形で行う: 行 2 に `現在のスキル`（英語は `Current Skill`）、
 * 行 4 に `グループ`・`スキル`・`レベル`（英語は `Group`・`Skill`・`Level`）、行 5 が `=` の罫線、行 6 以降が一覧。
 */
bool apply_skill_level_window(GameFrame &frame);

/*!
 * @brief 画面から届いた `ui_action`（`focus`・`navigate`・`continue`・`document_page`）を、原作の方向キーへ翻訳する。
 * @param menu 窓の版・要求番号の管理を持つ `NativeModernMenu`（受理の記録に使う）
 * @return この窓の操作として扱ったら true（`keys` に積んだ分が原作へ届く。拒否のときは何も積まない）。
 * `cancel`・`native_key` は扱わず false を返し、共用の入力（ESC・文字キーをそのまま原作へ）に任せる
 */
bool skill_level_input(const presentation::InputEventWire &event, std::vector<int> &keys, presentation::NativeModernMenu &menu);

//! 窓の組み立てと、操作の翻訳の自己検査。失敗の数を返す。
int skill_level_selftest();

} // namespace tb
