/*!
 * @file sdl_sub_window_terms.h
 * @brief サブパネル用の画素なし Term 群（K-26。設計書 §4.6 / §16）
 *
 * コアは「サブウインドウ」を `angband_terms[1..7]` に対して描く仕組みを持っている
 * （`window_stuff()` → `fix_*` → `display_sub_windows()`。`src/core/window-redrawer.cpp:246`）。
 * SDL2 UI は窓を 1 つしか持たないので、**画素を出さない Term を 5 枚**
 * `angband_terms[1..5]` に置き、`g_window_flags[1+i]` に利用者が選んだ種類を入れる。
 * あとはコアが勝手にその Term へ描くので、Bridge がその文字グリッドを読んで
 * `GameFrame::sub_panels` に積む（コア非改変・Term 非描画）。
 *
 * term_type / angband_terms / g_window_flags を触るのはこの TU と sdl_null_term だけ。
 */
#pragma once

#include "frame/game_frame.h"

namespace presentation {

/*!
 * @brief サブパネル用 Term を `angband_terms[1..5]` に設置する。
 * @details `SdlNullTerm::install`（`angband_terms[0]`）の**後**、`init_angband` の前に呼ぶ。
 * 置くだけで activate はしない（本線 Term を奪わない）。
 */
void install_sub_window_terms();

/*!
 * @brief パネルに収まる文字セル数を伝える（Term のサイズになる）。
 * @param panel 0..4（`PanelId::Sub1`〜`Sub5`）
 * @details コアの表示関数は Term のサイズを見て行数・桁数を決めるため、
 * **パネルの大きさをそのまま Term のサイズにする**必要がある。フォントの字送りを
 * 知っているのは ui だけなので、`seam.before_capture` から platform が渡す（H1 と同じ構図）。
 * 実際の `term_resize` は次の `fill_sub_panels` で行う（描画中に足元を変えない）。
 */
void set_sub_panel_term_cells(int panel, int cols, int rows);

/*!
 * @brief 選択中の種類を `g_window_flags` へ反映し、各 Term の中身を GameFrame へ写す。
 * @details Bridge::capture から毎フレーム呼ぶ。種類が変わった枚があれば
 * サブウインドウの再描画フラグを立て直す（次の `handle_stuff` でコアが描く）。
 * @note **キャラクターごとの構成の読み書きもここで行う**。セーブファイルの隣の
 * `<セーブ名>.sdl2panels` が構成の持ち主で、無ければ `sdl2_ui_options.cfg` の値を使い、
 * 変わったら書き出す。コアのセーブ形式は触らない（必守制約 1）。
 */
void fill_sub_panels(GameFrame &frame);

} // namespace presentation
