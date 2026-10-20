/*!
 * @file tb_menu.h
 * @brief コアのカーソルとプロンプト（ （幻想蛮怒）` S2 後半・S4）。
 *
 * どちらも**文字列の照合だけ**でできている。判定の理屈は変愚の
 * `presentation/bridge/presentation_bridge.cpp` から写したが、
 * 文言は幻想蛮怒の `src/` を実読して合わせてある。
 */
#pragma once

#include "frame/game_frame.h"

namespace tb {

/*!
 * @brief コアのカーソル `》`（英語層では原作の英語の枝の `> `）と、作成画面の選択中の黄色い項目を Term ミラーから探す。
 * @return 見つかったら true（`frame.menu_core_cursor` / `menu_core_cursors` を埋める）
 * @details 英語層（`tb_lang_enabled()` が真）では `> ` も印として探す（行頭・空白・枠線の直後だけ）。`》` は英語層でも探す
 * （訳のカタログが引けず日本語のまま残った行があるため）。日本語層では `> ` を見ない。
 *
 * 見つかることには 2 つの意味がある。
 * 1. **描画**: 画面側が同じ枠を重ねられる（コアの画面もカーソル選択式に見える）。
 * 2. **調停**: 「この画面はコアがカーソルを持っている」の判定そのもの。
 *    グローバルな `use_menu` で判定してはいけない——**店のアイテム選択のように
 *    コアにカーソルが無い画面**まで「持っている」と誤判定して、画面側のカーソルが消える。
 */
bool fill_core_cursor(GameFrame &frame);

/*!
 * @brief コアのカーソルで選ぶ名指しのメニュー（ガンナーの技術メニュー）を行ごとの選択肢にする。
 * @return 作ったら true
 * @details `fill_core_cursor()` が真を返した画面だけで呼ぶ。札は `key == 13`（Enter）で、現在の行が `selected`。
 */
bool fill_cursor_menu_choices(GameFrame &frame);

/*!
 * @brief Term 行 0 を読んで `frame.prompt` / `frame.numeric` を埋める。
 * @details `[y/n]` と数値入力は `screen_save()` を通らない＝ icky にならないので、
 * **Term ミラーには 1 行も出てこない**。行 0 を直に読まないと、パッドでは
 * 「はい」と答える手段も個数を入れる手段も無い。
 */
void fill_row0_prompt(GameFrame &frame);

/*!
 * @brief 店・建物・品物選びの `a)` を走査して `frame.menu_choices` へ積む（R3）。
 * @return 1 件でも積んだら true
 * @details この 3 つの画面は**コアがカーソルを持たない**（`》` を書かない）。
 * 数え上げてやらないと、パッドでもクリックでも「商品を買う」を選べない
 * ——キーボードで `p` を打つ以外に道が無かった。
 * @note **`fill_core_cursor()` が真を返した画面では呼ばない。**コアのカーソルと
 * 画面側のカーソルが二重になる。
 */
bool fill_store_menu_choices(GameFrame &frame);

/*!
 * @brief 自動拾いエディタの検索語の入力窓（`autopick.c:3553`）の札と入力欄を直す。
 * @details 問い `検索(^I:持ち物 ^L:破壊された物): ` は途中に `:` を含むので、共用の窓が札を `検索(^I` で切ってしまう。
 * `NativeModernMenu::capture()` の後で呼ぶ（窓の種類・版は変えず、表示の字だけを直す）。
 */
void fix_search_input_window(GameFrame &frame);

//! Rare command-screen grammars used by the HD2D command/UI regression suite.
int menu_semantics_selftest();

} // namespace tb
