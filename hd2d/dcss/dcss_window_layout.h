/*!
 * @file dcss_window_layout.h
 * @brief DCSS の窓の部品の大きさと位置の計算（原作 `ui.cc`・`menu.cc` の移植）。描画にも通信にも触れない純粋な関数。
 *
 * ## 原作のどこを写したか（`crawl-ref/source/`）
 * - `ui.cc` の `Popup::_get_preferred_size`・`_allocate_region`・`base_margin`（窓の枠。内側の余白 23・窓の高さで決まる外側の余白・
 *   ゲーム中は上寄せで重ねるたびに 20 ずつ下げる）、`Scroller::_allocate_region`（捲り位置・影・捲り棒）
 * - `menu.cc` の `UIMenu::do_layout`（行の高さ・見出し・項目の絵 32 画素・字の字下げ 38）、
 *   `UIMenu::pack_buffers`（見出しの区切り線・強調）、`Menu::update_title` の余白、`UIMenuPopup::_allocate_region`
 *
 * ## 字の幅の約束
 * 原作の字（BIZ UDゴシック）は半角が半角分の幅、全角がその 2 倍の幅で並ぶ（`DcssTextMeasure::half_w` ごとに数える）。
 * 幅の数え方は `dcss_cell_width`（0・1・2）に従う。
 */
#pragma once

#include "dcss/dcss_cells.h"
#include "dcss/dcss_window.h"
#include "ui/ui_layout.h"

#include <string>
#include <string_view>
#include <vector>

namespace hd2d::dcss {

//! 字の測り（画素）。窓の字は CRT の字（`tile_font_crt_*`）。
struct DcssTextMeasure {
    int half_w{ 8 }; //!< 半角 1 字の送り（全角はこの 2 倍）
    int line_h{ 15 }; //!< 1 行の高さ（原作 `char_height()`）
    int char_w{ 15 }; //!< 原作 `char_width()`（`max_advance`。メニューの最大幅 93 字ぶんの計算に使う）

    int cells_width(const DcssStyledLine &line) const;
    int width(std::string_view utf8) const;
};

// ---- 窓の枠（Popup）----

inline constexpr int kPopupPadding = 23; //!< `Popup::m_padding`
inline constexpr int kPopupDepthIndent = 20; //!< `Popup::m_depth_indent`

//! 窓の高さから決まる外側の余白（`Popup::base_margin`。小さい配置は 0）。
int dcss_popup_base_margin(int window_h, bool small_layout);

struct DcssPopupGeometry {
    RectPx frame; //!< 枠の外側（金茶の縁まで）
    RectPx content; //!< 中身を置く矩形（枠から 23 画素内側）
};

/*!
 * @brief 窓の枠を置く。
 * @param area 窓の部品の層の矩形（ウィンドウ全体）
 * @param nat_w,nat_h 中身の本来の大きさ（`Popup::_get_preferred_size` の中身の分）
 * @param depth 積まれている順（0 が最初）。上寄せの窓だけ、重ねるたびに 20 画素下がる
 * @param min_h 中身の最小の高さ（原作 `SizeReq::min`。`Popup::_allocate_region` は `max(min, min(空き, nat))`）。空きが足りなくても枠はこの高さを取り、中身が枠の外へ出ない。
 *        空きより大きいときは上に寄せる（はみ出しは下の端から出る。Android はキーボードの上の端で切れる）
 */
DcssPopupGeometry dcss_popup_place(const RectPx &area, int nat_w, int nat_h, bool centred, int depth, bool small_layout, int min_h = 0);

// ---- メニュー ----

inline constexpr int kMenuItemPad = 2; //!< `UIMenu::item_pad`
inline constexpr int kMenuPadRight = 10; //!< `UIMenu::pad_right`
inline constexpr int kMenuMinColumnWidth = 400;
inline constexpr int kMenuIconSize = 32;
inline constexpr int kMenuTextIndent = 38; //!< 絵があるときの字の字下げ

struct DcssMenuEntryLayout {
    int x{ 0 };
    int y{ 0 };
    int row{ 0 };
    int column{ 0 };
    bool heading{ false };
    DcssStyledLine text; //!< 項目の字（先頭の「 a - 」を含む）
    int prefix_len{ 0 }; //!< 「 a - 」の字数（0 なら無し）
    std::vector<DcssStyledLine> wrapped; //!< 先頭を除いた本文の行（見出しは全体）
    int height{ 0 }; //!< 行の高さ（`row_heights` の差）
};

struct DcssMenuLayout {
    std::vector<DcssMenuEntryLayout> entries;
    std::vector<int> row_heights; //!< 各行の上端（最後は全体の高さ）
    int height{ 0 };
    int nat_column_width{ 0 };
    bool draw_tiles{ false };
    int num_columns{ 1 };
};

//! 原作 `UIMenu::do_layout`。`mw` はメニューの領域の幅。
DcssMenuLayout dcss_layout_menu(const DcssMenuData &menu, int mw, int num_columns, const DcssTextMeasure &m);

//! 原作 `UIMenu::_get_preferred_size(HORZ)`: メニューが欲しい幅。
int dcss_menu_preferred_width(const DcssMenuLayout &natural, const DcssTextMeasure &m);

//! ホットキーの字の有無を問わず、メニューの項目として選べるか（JS `item_selectable`）。
bool dcss_menu_item_selectable(const DcssMenuData &menu, int index);

// ---- 字の塊（Text）----

//! 色つきの字の塊を `max_w` 画素で折り返す。`\n` は段落を切る。
std::vector<DcssStyledLine> dcss_wrap_text(std::string_view formatted, int max_w, int default_colour, const DcssTextMeasure &m);
//! 行の列の幅（最も長い行）。
int dcss_lines_width(const std::vector<DcssStyledLine> &lines, const DcssTextMeasure &m);

} // namespace hd2d::dcss
