/*!
 * @file sub_panel_kinds.h
 * @brief サブパネルに割り当てられる「コアのサブウインドウの種類」一覧（K-26）
 *
 * 種類の**名前と番号はコアの表** `window_flag_desc`（`src/term/gameterm.cpp:110`）と
 * `SubWindowRedrawingFlag`（`src/system/redrawing-flags-updater.h:33`）から作る。
 * ここに独自の一覧を持たない（コアが種類を増やしたら黙って追従する）。
 *
 * 宣言だけを presentation/frame に置き、実装はコアを include できる
 * `presentation/term/sdl_sub_window_terms.cpp` に置く（ui はこのヘッダのみ見る）。
 */
#pragma once

#include <string>
#include <vector>

//! 割り当てられる種類 1 件。
struct SubPanelKindEntry {
    /*!
     * @brief コアの `SubWindowRedrawingFlag` の番号。**-1 は「UI 既定」**。
     * @details -1 のときは従来の作り込み表示（Sub1=メッセージ / Sub4=ステータス /
     * Sub2=装備 / Sub3=視界モンスター / Sub5=持ち物）をそのまま出す。
     */
    int flag{ -1 };
    std::string label_utf8;
};

/*!
 * @brief 一覧（先頭は必ず flag = -1 の「UI 既定」）。
 * @details 初回呼び出し時にコアの表から組み立てて以後は使い回す。
 * @note コアの表にあっても**描画関数が無い種類は載せない**（記念撮影＝SNAPSHOT）。
 * `window_stuff()`（`src/core/window-redrawer.cpp:246`）に対応する `fix_*` が無く、
 * 選ばせても永久に空のままになるため。
 */
const std::vector<SubPanelKindEntry> &sub_panel_kind_entries();

//! flag 番号の表示名（未知の番号は「UI 既定」の名前）。
const std::string &sub_panel_kind_label(int flag);

//! flag 番号 → 一覧上の位置。載っていなければ 0（UI 既定）。
int sub_panel_kind_index_of(int flag);

//! 一覧上の位置 → flag 番号。範囲外は -1（UI 既定）。
int sub_panel_kind_flag_at(int index);
