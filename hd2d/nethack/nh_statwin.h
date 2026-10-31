/*!
 * @file nh_statwin.h
 * @brief NetHack のステータスの窓の中身（2 行・項目の順・色と属性・題の欄の体力バー）。描画にも通信にも触れない。
 *
 * ## 何のためのものか
 * 原作の Windows の GUI 版 `win/win32/mhstatus.h`・`mhstatus.c` の項目の並びを写す。
 * - **2 行固定**。1 行目: 題・筋力・器用・耐久・知力・賢明・魅力・属性・得点。2 行目: 階の名前・所持金・HP・HP 最大・Pw・Pw 最大・AC・
 *   レベル・経験値・HD・時間・空腹・荷重・状態異常。
 * - 項目の色（`CLR_*` の 0〜15。8 は指定なし）と属性（`HL_*`）はコアが項目ごとに渡す。状態異常は 1 つずつ色と属性を持つ。
 * - **体力バー**: 題の欄の文字の上に、HP の割合の幅だけ HP の色の帯を塗り、帯の上は背景色の字で重ねる（`mhstatus.c:404-455`）。
 *   HP の色が指定なしのときは緑。割合は HP の項目の `pct`、無ければ HP ÷ HP 最大。
 */
#pragma once

#include "nethack/nh_model.h"

#include <string>
#include <vector>

namespace hd2d::nethack {

struct NhStatRun {
    std::string text;
    int color{ kNoColor };
    int attr{ 0 };            //!< `HL_*` の位
    bool space_before{ true }; //!< 前の字との間に半角 1 つ空けるか（`(`・`/` で始まる欄は前へ付ける）
    int field{ -1 };           //!< 元の項目の番号（状態異常は `kBlCondition`）
};

struct NhStatusView {
    std::vector<NhStatRun> rows[2];
    //! 題の欄の体力バー。`bar_pct` が 0 以下なら塗らない。
    int bar_pct{ 0 };
    int bar_color{ 2 };
};

//! 1 行目・2 行目の項目の順（`-1` で終わる）。
const int *nh_status_order(int row);

//! モデルから 2 行ぶんの欄を作る。字が空の項目は落とす。
NhStatusView nh_build_status(const NhModel &model);

} // namespace hd2d::nethack
