/*!
 * @file tb_building_page.h
 * @brief 建物の中で「何かキーを押す」まで結果を出し続ける画面を、後ろに建物の一覧が残らない結果のページにする。
 *
 * 原作は建物の画面（`show_building()`。行 19〜22 に行動の一覧、行 23 に `ESC) 建物を出る` と所持金）の上に、
 * 結果を直接描いて `inkey()` で任意のキーを待つ。新しい画面は、行 19〜23 を見て建物の選択肢の窓
 * （左に行動の一覧、右に結果）を作るので、結果の画面でも左に建物の一覧（`ESC) 建物を出る` だけ、
 * または押しても効かない行動の一覧）が残り、題も結果の 1 行（`15` など）になっていた。
 * 共用部分（`presentation/`・`hd2d/`）は変えずに、TOband のアダプタが、画面ごとの見分けで窓を組み替える。
 *
 * ## 対象（全数は  の表）
 * - 賞金首リストの頁送り（`bldg.c` の `shoukinkubi()`。行 0 に `何かキーを押してください`）
 * - 武器の比較（`compare_weapons()`。結果を出して `inkey()`）
 * - 防御について調べる（`eval_ac()`。同上）
 * - 勇者の殿堂（`scores.c` の `show_highclass()`。行 0 に `何かキーを押すとゲームに戻ります`）
 * - アイテム調査・鑑定の情報（`object1.c` の `screen_object()`。建物の画面の上に枠を描くので、枠の左と下に一覧の一部が残る）
 *
 * 結果の字は原作が画面に描いたものを読むだけで、遊びの規則は変えない。
 * 窓は「決定だけの詳細画面」（`detail`。決定・取消・文字キーのどれでも原作へ任意のキーが届く）で、
 * 入力の翻訳は共用の `NativeModernMenu::input()` がそのまま行う（窓の種類を変えないので、専用の翻訳は要らない）。
 */
#pragma once

#include "frame/game_frame.h"

namespace tb {

/*!
 * @brief 共用の窓の組み立て（`NativeModernMenu::capture()`）の**前**に呼ぶ。建物の結果のページなら、
 * 押しても効かない建物の選択肢（`frame.menu_choices`）を空にして、`capture()` が「決定だけの詳細画面」を作るようにする。
 * @return 対象の画面なら true（直後の `apply_building_page_window()` が窓を組み替える）
 */
bool prepare_building_page(GameFrame &frame);

/*!
 * @brief `capture()` の**後**に呼ぶ。`prepare_building_page()` が真だった画面の窓（`frame.modern`）を、
 * 題と結果だけの本文に組み替える（建物の一覧・主の名前は載せない）。
 * @return 組み替えたら true
 */
bool apply_building_page_window(GameFrame &frame);

//! 画面の見分けと窓の組み立ての自己検査。失敗の数を返す。
int building_page_selftest();

} // namespace tb
