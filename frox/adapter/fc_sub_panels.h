/*!
 * @file fc_sub_panels.h
 * @brief サブパネル 7 枚（設計 §4.4）。
 *
 * Frox のサブウィンドウ（`window_flag[]` × `window_stuff()`）を画面側の
 * `GameFrame::sub_panels` に写す層。Term そのものの世話は C 側（`fc_sub_terms.c`）で、
 * ここは「種類の一覧を申告する」「画面の希望を当てる」「文字を積む」の 3 つ。
 *
 * 手本は `silq/adapter/sq_sub_panels.{h,cpp}`。**共用はできない**——あちらは
 * Sil-Q の `op_ptr->window_flag` を読む（Frox は素の大域 `window_flag[8]`）。
 *
 * ## 持ち主は画面の cfg
 * コアは自分の都合で `window_flag[]` を丸ごと書き戻す場所を持っている
 * （起動時・セーブの読み込み・`toggle_inven_equip()`）。どちらの向きの変更かを
 * Frox は名乗れない（**`frox/src` を触れないので世代計数器を入れられない**）から、
 * Sil-Q と同じく**希望を押し続ける**（実効値が希望と違えば毎フレーム書き直す）。
 *
 * ## 矢筒（**Frox 固有**。設計 §3.4 / §4.4）
 * `FC_SUB_KIND_QUIVER`（1000）は `window_flag` のビットではなく、
 * **アダプタが自分で描く種**である。選ばれた枚はコアの Term を使わず、
 * `fc_read_item_line(FC_ITEMS_QUIVER, ...)` の行を直接積む。
 * 落とすと制約 7（オリジナルの要素を落とさない）に触る。
 */
#pragma once

#include "frame/game_frame.h"
#include "frame/protocol_messages.h"

namespace fc {

/*!
 * @brief 画面へ申告する「サブパネルに映せるもの」の一覧（v1 §8.1）。
 * @details 先頭は必ず `flag = -1`（UI 既定）。中身はコアの `window_flag_desc` から
 * 組み、**描画関数のある種類だけ**を載せ、末尾に矢筒（1000）を足す。
 * 一覧を画面に持たせない——コアが種類を増やしたら黙って追従する（設計 §1 制約 3）。
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
 * @details セーブの読み込みが `window_flag[]` を丸ごと書き戻すので、
 * 起動直後に当てた希望はそこで消える。ここで戻さないと**画面側の割り当てが
 * 起動のたびにセーブの値に負ける**（Sil-Q で実測した穴と同じ）。
 */
void reset_sub_panel_apply();

/*!
 * @brief `frame.sub_panels[]` を埋める。**ゲームスレッドから**呼ぶこと。
 * @details 種類を当て → 大きさを当て → 変えたなら「描き直せ」の印を立て →
 * いま Term に入っている文字を積む、の順。**描くのはコア**なので、
 * 種類を変えた直後の 1 フレームは前の中身が出る（次の `handle_stuff()` で入れ替わる）。
 */
void fill_sub_panels(GameFrame &frame);

} // namespace fc
