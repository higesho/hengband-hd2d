/*!
 * @file sq_sub_panels.h
 * @brief サブパネル 7 枚。
 *
 * Sil-Q のサブウィンドウ（`op_ptr->window_flag` × `window_stuff()`）を画面側の
 * `GameFrame::sub_panels` に写す層。Term そのものの世話は C 側（`sq_sub_terms.c`）で、
 * ここは「種類の一覧を申告する」「画面の希望を当てる」「文字を UTF-8 で積む」の 3 つ。
 *
 * 手本は `gensoband/adapter/gb_sub_panels.{h,cpp}`。**共用はできない**——あちらは
 * 幻想蛮怒の `window_flag` を直に読む。
 *
 * ## 幻想蛮怒との違い — 世代計数器を持たない
 * あちらは「コアの `=`→窓の画面で変えたぶんを画面へ返す」ために `cmd4.c` へ 1 行入れて
 * 世代を数えている。**Sil-Q では `silq/src` を触れない**（必守制約 2）ので使えない。
 *
 * ## だから**画面の設定を持ち主と決めた**（2026-08-23。気づいたことで作り直した）
 *
 * 初版は「希望が変わったときだけ書く」で、コア側で変えたぶんを画面が写し返して
 * 収束する形にしていた。**これが利用者の設定を毎回食っていた**——コアは自分の都合で
 * `window_flag[]` を丸ごと書き戻す場所をいくつも持っている:
 *
 * | どこ | いつ |
 * |---|---|
 * | `init2.c` の `init_other()` | 起動時（`init_angband()`） |
 * | 同 `re_init_some_things()` | 局と局の間（タイトルへ戻ったとき） |
 * | `load.c` の `rd_options()` | セーブの読み込み |
 * | `toggle_inven_equip()` | 品を選ぶたび（持ち物と装備を全窓で入れ替える） |
 *
 * どれも「利用者がコア側で変えた」ではないのに、実効値としては同じ顔で流れてくる。
 * **どちらの向きかを Sil-Q は申告できない**（世代計数器が無い）ので、写し返す作りだと
 * cfg に書いた割り当てが起動やタイトル往復のたびに消える——**実測でそうなった。**
 *
 * いまは**希望を押し続ける**（実効値が希望と違えば毎フレーム書き直す）。
 * 隙が無くなるので、画面側が拾い間違える瞬間そのものが消える。
 *
 * **失うものは無い**（2026-08-23 に読み直した）。**Sil-Q に窓割りを編集する画面は無い**
 * ——設定メニューは `a) Interface / b) Visual / c) Challenge / d) Load a 'Pref' File /
 * e) Append Options to a 'Pref' File / f) Return to Game` だけである（`cmd4.c:7216`）。
 * 変愚の `=`→`w` に当たる頁が最初から無いので、押し続けて困る人がいない。
 *
 * 利用者の手で `window_flag[]` が動きうるのは 2 つだけ:
 *
 * | 何 | いまの振る舞い |
 * |---|---|
 * | `d) Load a 'Pref' File` の `W:` 行（`files.c:675`） | 戻る（拾っていない） |
 * | `Ctrl-E`（`toggle_inven_equip`。`dungeon.c:895`） | 戻る。**変愚も戻す側**（利用者の意図ではないと決めてある） |
 */
#pragma once

#include "frame/game_frame.h"
#include "frame/protocol_messages.h"

namespace sq {

/*!
 * @brief 画面へ申告する「サブパネルに映せるもの」の一覧（v1 §8.1）。
 * @details 先頭は必ず `flag = -1`（UI 既定）。中身はコアの `window_flag_desc` から
 * 組み、**描画関数のある種類だけ**を載せる（`sq_window_flag_name()` が選り分ける）。
 * 一覧を画面に持たせない——コアが種類を増やしたら黙って追従する（必守制約 1）。
 */
presentation::SubPanelKindsMessage build_sub_panel_kinds();

/*!
 * @brief 画面側が望む種類（`ui_state.sub_panel_kinds[i]`。-1 = UI 既定）を覚える。
 * @details 覚えるだけ。コアへ当てるのは `fill_sub_panels()` の中で、capture の
 * 1 か所に閉じ込めてある（Term の活性を掻き回す操作を散らさない）。
 */
void set_sub_panel_kind(int index, int flag);

//! 画面側が望む桁・行（`ui_state.sub_panel_cells[i]`）。0 は「申告なし」。
void set_sub_panel_cells(int index, int cols, int rows);

/*!
 * @brief 「まだ 1 度も書いていない」状態へ戻す。**@ が出来た直後に 1 回**呼ぶ。
 * @details `load.c:654` が**セーブから `op_ptr->window_flag[]` を丸ごと書き戻す**ので、
 * 起動直後に当てた希望はそこで消える。以後は希望が変わるまで書かない作りなので、
 * ここで戻さないと**画面側の割り当てが起動のたびにセーブの値に負ける**。
 * 呼び手は `sq_main.cpp` の `host_present`（`sq_prime_item_list_view()` と同じ 1 回）。
 */
void reset_sub_panel_apply();

/*!
 * @brief `frame.sub_panels[]` を埋める。**ゲームスレッドから**呼ぶこと。
 * @details 種類を当て → 大きさを当て → 変えたなら「描き直せ」の印を立て →
 * いま Term に入っている文字を積む、の順。**描くのはコア**なので、
 * 種類を変えた直後の 1 フレームは前の中身が出る（次の `handle_stuff()` で入れ替わる）。
 */
void fill_sub_panels(GameFrame &frame);

} // namespace sq
