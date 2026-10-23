/*!
 * @file dcss_cells.h
 * @brief DCSS（Dungeon Crawl Stone Soup 日本語版）の画面の部品が使う、字の格子と色つきの文字列の道具。
 *
 * ## 何のためのものか
 * 原作のタイル版（`USE_TILE_LOCAL`）は、ステータス欄・メッセージ欄を**文字の格子**（`TextRegion`）として持ち、
 * 1 マスに「字 1 つ・前景 4 ビット・背景 4 ビットの 16 色」を置く。ここはその格子（`DcssTextGrid`）と、
 * 原作の文字列の処理（`<yellow>…</yellow>` の色つきの書式・幅の数え方・折り返し）を描画に触れずに写した物である。
 * 描くのは `dcss_ui.cpp`。単体で検査できるよう、GL にも SDL にも触れない。
 *
 * ## 原作のどこを写したか（`crawl-ref/source/`）
 * - `tilereg-text.cc` の `addstr_aux`・`clear_to_end_of_line`・`textcolour`・`textbackground`・`cgotoxy`
 *   （全角は 2 マスを使い、2 マス目に詰め物 `WIDE_CHAR_FILLER` を置く）
 * - `stringutil.cc` の `wordwrap_line`、`unicode.cc` の `wide_char_break_ok`（日本語の禁則）
 * - `tilefont.cc` の `term_colours`（16 色の RGB）、`colour.cc` の色の名前
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hd2d::dcss {

//! 原作 `COLOURS`（`defines.h`）。並びが原作の番号。
enum DcssColour : int {
    kBlack = 0,
    kBlue,
    kGreen,
    kCyan,
    kRed,
    kMagenta,
    kBrown,
    kLightGrey,
    kDarkGrey,
    kLightBlue,
    kLightGreen,
    kLightCyan,
    kLightRed,
    kLightMagenta,
    kYellow,
    kWhite,
    kColourCount,
};

struct DcssRgb {
    std::uint8_t r{ 0 };
    std::uint8_t g{ 0 };
    std::uint8_t b{ 0 };
};

//! タイル版の字の色（`tilefont.cc` の `term_colours`）。範囲外は LIGHTGREY。
const DcssRgb &dcss_term_rgb(int colour);
//! 色の名前（`lightgrey`・`lightgray` ほか）から番号。無ければ `fallback`。
int dcss_colour_from_name(std::string_view name, int fallback);

//! 原作の `wcwidth`（全角 = 2・結合文字 = 0・ほか 1）。
int dcss_cell_width(char32_t cp);
std::vector<char32_t> dcss_decode_utf8(std::string_view text);
std::string dcss_encode_utf8(char32_t cp);
//! 表示幅（マス数）。
int dcss_text_width(std::string_view utf8);
//! 原作 `chop_string`。`width` マスに切るか、`pad` が真なら空白で埋める（全角を半分だけ残すときは空白にする）。
std::string dcss_chop_string(std::string_view utf8, int width, bool pad = true);

//! 全角の 2 マス目に置く詰め物（原作 `WIDE_CHAR_FILLER`）。
inline constexpr char32_t kWideFiller = 0xE000u;

/*!
 * @brief 色つきの 1 字。
 * @details `colour` は前景（0〜15）。メッセージ欄は背景を使わない。
 */
struct DcssStyledChar {
    char32_t ch{ U' ' };
    std::uint8_t colour{ kLightGrey };
};
using DcssStyledLine = std::vector<DcssStyledChar>;

/*!
 * @brief 原作の書式つき文字列（`<yellow>…</yellow>`・`<lightgrey>`・`<<` で `<`）を色つきの字の列にする。
 * @details 開き `<名前>` は色を積み、閉じ `</名前>` は 1 つ戻す。名前が色でない開きは字として残さず捨てる。
 */
DcssStyledLine dcss_parse_tagged(std::string_view text, int default_colour = kLightGrey);
int dcss_line_width(const DcssStyledLine &line);

/*!
 * @brief 原作 `wordwrap_line` を繰り返して、`width` マスの行へ折り返す。
 * @details 空白と、日本語の禁則（`wide_char_break_ok`）で切れる所のうち**最後**で切る（無ければ字の途中）。
 * 改行字は行を分ける。行の頭の空白は捨てる。
 */
std::vector<DcssStyledLine> dcss_wrap_line(const DcssStyledLine &line, int width);

/*!
 * @brief 原作の文字格子（`TextRegion`）の中身。内部の座標は 0 始まり、`goto_xy` は原作どおり 1 始まり。
 */
struct DcssTextGrid {
    int cols{ 0 };
    int rows{ 0 };
    std::vector<char32_t> ch;
    std::vector<std::uint8_t> attr; //!< 背景 × 16 + 前景
    int print_x{ 0 };
    int print_y{ 0 };
    int text_col{ kLightGrey };

    void resize(int new_cols, int new_rows);
    void clear();
    void goto_xy(int x, int y) { this->print_x = x - 1; this->print_y = y - 1; }
    int where_x() const { return this->print_x + 1; }
    void textcolour(int colour) { this->text_col = colour; }
    void textbackground(int background) { this->text_col = (background * 16) + (this->text_col & 0xF); }
    //! 原作 `addstr_aux`。
    void putstr(std::string_view utf8);
    void putwch(char32_t cp);
    void put_styled(const DcssStyledLine &line);
    void clear_to_end_of_line();

    char32_t at(int x, int y) const { return this->ch[static_cast<std::size_t>(y) * static_cast<std::size_t>(this->cols) + static_cast<std::size_t>(x)]; }
    std::uint8_t attr_at(int x, int y) const { return this->attr[static_cast<std::size_t>(y) * static_cast<std::size_t>(this->cols) + static_cast<std::size_t>(x)]; }
};

} // namespace hd2d::dcss
