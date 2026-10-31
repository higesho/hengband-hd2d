/*!
 * @file nh_layout.h
 * @brief NetHack の専用の画面の配置の計算。原作の Windows の GUI 版（NetHackW）の `mswin_layout_main_window` の写し。
 *
 * ## 何のためのものか
 * 窓の大きさから、上にメッセージの窓、下にステータスの窓、
 * 残りを地図の区画（立体の地図を描く矩形）、地図の右寄せにメニュー・テキストの窓と持ち物の常設の窓の矩形を決める。
 * 描画にも通信にも触れない純粋な関数で、単体検査 `tools/hd2d_verify/test_nethack_layout.cpp` が数を固定する。
 *
 * ## 原作のどこを写したか（`win/win32/mhmain.c`）
 * - 上のメッセージの窓の高さ = 行数 × 字の行の高さ（既定 4 行。`vary_msgcount`）、下のステータスの窓 = 2 行（`NHSW_LINES`）。
 *   どちらも幅は使える範囲いっぱい。先にステータスを下から切り出し、次にメッセージを上から切り出す（`mhmain.c:633-717`）。
 * - 残りが地図の矩形（`rtMapWindow`）。
 * - メニュー・テキスト・持ち物の窓は、地図の右端から幅 `min(地図の幅 × 2/3, max(地図の幅 / 4, 中身の幅))`、高さは地図と同じ
 *   （`mhmain.c:746-757`）。`perm_invent` が有効なら地図の右端を持ち物の窓の左端まで縮める（`mhmain.c:759-761`）。
 *
 * ## 原作と違う所
 * - **コマンドの欄**（`cmdbar`。原作に無い。タッチだけの端末でもコマンドを打つための絵のボタン。決めたこと 2026-10-28）。
 *   縦長（高さ ＞ 幅）では使える範囲の一番下に段で、横長では右端に列で取り、残りを上の原作の配置に渡す。
 * - 窓が小さいとき（縦長・低い窓）に地図が潰れないよう、地図の高さが使える高さの 1/3 を割る間は、メッセージの行数を 1 行ずつ減らす
 *   （最小 1 行）。原作の配置は窓を手で大きくする前提で、この保護が無い。
 */
#pragma once

#include <string>

namespace hd2d::nethack {

//! 窓の座標の矩形。
struct NhRect {
    int x{ 0 };
    int y{ 0 };
    int w{ 0 };
    int h{ 0 };

    bool empty() const { return (w <= 0) || (h <= 0); }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool contains(int px, int py) const { return (px >= x) && (px < x + w) && (py >= y) && (py < y + h); }
    bool operator==(const NhRect &o) const { return (x == o.x) && (y == o.y) && (w == o.w) && (h == o.h); }
};

struct NhLayoutInput {
    //! 使える範囲（下の案内の帯を引いたあと）。
    int x{ 0 };
    int y{ 0 };
    int w{ 0 };
    int h{ 0 };
    //! 字の 1 行の高さ（画素）。
    int line_h{ 18 };
    //! メッセージの窓の行数（原作の既定は 4。`vary_msgcount`）。
    int msg_rows{ 4 };
    //! ステータスの窓の行数（原作は 2 固定。`NHSW_LINES`）。
    int status_rows{ 2 };
    //! 持ち物の常設の窓を出すか（`perm_invent`。既定は出さない。D15）。
    bool perm_invent{ false };
    //! メニューの窓の中身の希望の幅（0 = 未定。窓の幅は地図の 1/4 になる）。
    int menu_content_w{ 0 };
    //! コマンドの欄のボタンの数と、ボタン 1 つの大きさ（画素。正方形）。どちらかが 0 なら欄を取らない。
    int cmdbar_count{ 0 };
    int cmdbar_cell{ 0 };
};

struct NhLayout {
    bool ok{ false };
    std::string error;
    //! 実際に使うメッセージの窓の行数（窓が小さいときは入力より少なくなる）。
    int msg_rows{ 0 };
    int status_rows{ 0 };
    NhRect message;
    NhRect status;
    //! 立体の地図を描く矩形。`perm_invent` が有効なら持ち物の窓の左端までに縮む。
    NhRect map;
    //! 地図を縮める前の矩形（原作の `rtTextWindow`。全面のテキストの窓が重なる範囲）。
    NhRect map_full;
    //! メニューの窓（右寄せ。地図の高さいっぱい）。
    NhRect menu;
    //! 持ち物の常設の窓。`menu` と同じ矩形。`perm_invent` が無効でも計算しておく。
    NhRect invent;
    //! コマンドの欄（取らないときは空）。ボタンは左上から、縦長は横に、横長は縦に並べる（`cmdbar_vertical`）。
    NhRect cmdbar;
    int cmdbar_cols{ 0 };
    int cmdbar_rows{ 0 };
    bool cmdbar_vertical{ false };
};

//! 配置を計算する。`w`・`h`・`line_h` が正でない、または字の行が入らないほど小さいとき `ok` は偽。
NhLayout nh_compute_layout(const NhLayoutInput &in);

/*!
 * @brief 窓の大きさから字の大きさ（画素）を決める。
 * @details 原作の既定は 9 ポイント（96 dpi で 12 画素ほど）。HD2D は立体の地図と並ぶので窓の高さに比例させ、
 * 16〜32 画素に収める。720 → 16、900 → 19、1080 → 23、縦長 1600 → 32。
 */
int nh_font_px_for_window(int usable_w, int usable_h);

} // namespace hd2d::nethack
