/*!
 * @file dcss_windows_internal.h
 * @brief `DcssWindows` の実装ファイル（`dcss_windows*.cpp`）が共有する内部の型。ほかからは使わない。
 */
#pragma once

#include "dcss/dcss_windows.h"

namespace hd2d::dcss {

//! 窓 1 枚の直前の描画の配置（当たり判定・捲りの計算に使う）。窓の通し番号（`generation`）で引く。
struct DcssWindows::View {
    DcssPopupGeometry geo;
    //! 捲れない窓（作成の格子・文字画面・開始メニュー）が、枠の余白も含めて全部収まるのに要る窓の部品の層の高さ（字の大きさで決まる論理の高さ。0 = 収まりを問わない）。
    int need_h{ 0 };

    // メニュー
    RectPx title_rect;
    RectPx viewport;
    RectPx more_rect;
    DcssMenuLayout menu;
    int menu_total{ 0 };

    // 捲れる字の窓（捲れる文書・説明）
    RectPx body_viewport;
    int body_total{ 0 };
    int line_h{ 15 };

    //! 押せる所（作成の格子のボタン・説明の下の操作など）。
    struct Hit {
        RectPx rect;
        int kind{ 0 }; //!< 1 = 作成の格子のボタン、2 = 開始メニューの項目、3 = 説明の下の操作の文
        int index{ 0 };
        int hotkey{ 0 };
    };
    std::vector<Hit> hits;
};

//! 捲れる字の窓の中身（捲れる文書・説明の窓ほか）。
struct DcssWindows::ScrollContent {
    std::string title; //!< 書式つき。空なら題の行を出さない
    std::vector<int> tiles; //!< 題の絵（重ねて描く。空なら無し）
    bool title_centred{ false };
    std::string body; //!< 書式つき（改行で行を切る）
    std::string footer; //!< 書式つき。空なら出さない
    int body_colour{ kLightGrey };
    bool start_at_end{ false };
    bool footer_actions{ false }; //!< 下の案内が「(q)セットする、(u)…」の操作の文（各操作を押せる）
};

//! 文字画面（`txt`）の 1 マス（全角は 2 マスで、後ろが `kWideFiller`）。
struct CrtCell {
    char32_t ch{ U' ' };
    std::uint8_t fg{ kLightGrey };
    std::uint8_t bg{ 0 };
};

//! `txt` の 1 行（`<span class="fgN bgN">…</span>` の並び）をマスの並びにする。
std::vector<CrtCell> parse_crt_line(const std::string &html);

inline TextColor dcss_text_rgb(int colour)
{
    const DcssRgb &c = dcss_term_rgb(colour);
    return TextColor{ static_cast<float>(c.r) / 255.f, static_cast<float>(c.g) / 255.f, static_cast<float>(c.b) / 255.f, 1.f };
}

inline PaintColor dcss_paint_rgb(int r, int g, int b, int a = 255)
{
    return PaintColor{ static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f, static_cast<float>(b) / 255.f, static_cast<float>(a) / 255.f };
}

} // namespace hd2d::dcss
