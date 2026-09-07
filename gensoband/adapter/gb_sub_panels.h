/*!
 * @file gb_sub_panels.h
 * @brief サブパネル 7 枚。
 *
 * 幻想蛮怒のサブウィンドウ（`window_flag` × `window_stuff()`）を画面側の
 * `GameFrame::sub_panels` に写す層。Term そのものの世話は C 側（`gb_sub_terms.c`）で、
 * ここは「種類の一覧を申告する」「画面の希望を当てる」「文字を UTF-8 で積む」の 3 つ。
 *
 * 変愚の `presentation/term/sdl_sub_window_terms.cpp` に当たる位置だが、**共用はできない**
 * ——あちらはコアの `EnumClassFlagGroup<SubWindowRedrawingFlag>` と `TermChar` を直に読む。
 */
#pragma once

#include "frame/game_frame.h"
#include "frame/protocol_messages.h"

namespace gb {

/*!
 * @brief 画面へ申告する「サブパネルに映せるもの」の一覧（v1 §8.1）。
 * @details 先頭は必ず `flag = -1`（UI 既定）。中身はコアの `window_flag_desc` から
 * 組み、**描画関数のある種類だけ**を載せる（`gb_window_flag_name()` が選り分ける）。
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
 * @brief `frame.sub_panels[]` を埋める。**ゲームスレッドから**呼ぶこと。
 * @details 種類を当て → 大きさを当て → 変えたなら「描き直せ」の印を立て →
 * いま Term に入っている文字を積む、の順。**描くのはコア**なので、
 * 種類を変えた直後の 1 フレームは前の中身が出る（次の `handle_stuff()` で入れ替わる）。
 */
void fill_sub_panels(GameFrame &frame);

} // namespace gb
