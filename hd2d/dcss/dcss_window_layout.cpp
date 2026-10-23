/*!
 * @file dcss_window_layout.cpp
 * @brief `dcss_window_layout.h` の実装。
 */
#include "dcss/dcss_window_layout.h"

#include <algorithm>
#include <climits>

namespace hd2d::dcss {

int DcssTextMeasure::cells_width(const DcssStyledLine &line) const
{
    return dcss_line_width(line) * this->half_w;
}

int DcssTextMeasure::width(std::string_view utf8) const
{
    return dcss_text_width(utf8) * this->half_w;
}

int dcss_popup_base_margin(int window_h, bool small_layout)
{
    if (small_layout) {
        return 0;
    }
    constexpr int screen_small = 800;
    constexpr int screen_large = 1000;
    constexpr int margin_small = 10;
    constexpr int margin_large = 50;
    const int clipped = std::max(screen_small, std::min(screen_large, window_h));
    return margin_small + ((clipped - screen_small) * (margin_large - margin_small) / (screen_large - screen_small));
}

DcssPopupGeometry dcss_popup_place(const RectPx &area, int nat_w, int nat_h, bool centred, int depth, bool small_layout, int min_h)
{
    const int pad = dcss_popup_base_margin(area.h, small_layout) + kPopupPadding;
    const int indent = small_layout ? 0 : (depth * kPopupDepthIndent);
    const int avail_w = std::max(0, area.w - (2 * pad));
    const int avail_h = std::max(0, area.h - (2 * pad) - (centred ? 0 : indent));
    const int cw = std::max(0, std::min(avail_w, nat_w));
    const int ch = std::max(std::max(0, min_h), std::max(0, std::min(avail_h, nat_h)));
    DcssPopupGeometry g;
    g.content.x = area.x + pad + ((area.w - (2 * pad) - cw) / 2);
    // 原作 `Popup::_allocate_region`: 中身の最小の高さが空きより大きいときは、枠が空きからはみ出す。上端を見せるため、上に寄せる。
    g.content.y = area.y + pad + (centred ? ((ch <= avail_h) ? ((area.h - (2 * pad) - ch) / 2) : 0) : indent);
    g.content.w = cw;
    g.content.h = ch;
    g.frame = RectPx{ g.content.x - kPopupPadding, g.content.y - kPopupPadding, cw + (2 * kPopupPadding), ch + (2 * kPopupPadding) };
    return g;
}

namespace {

//! 原作 `_has_hotkey_prefix`（先頭の 5 字が「 a - 」の形か）。
bool has_hotkey_prefix(const DcssStyledLine &line)
{
    if (line.size() < 5) {
        return false;
    }
    const auto ch = [&](std::size_t i) { return line[i].ch; };
    const bool let = ((ch(1) >= U'a') && (ch(1) <= U'z')) || ((ch(1) >= U'A') && (ch(1) <= U'Z'));
    const bool plus = (ch(3) == U'-') || (ch(3) == U'+') || (ch(3) == U'#');
    return let && plus && (ch(0) == U' ') && (ch(2) == U' ') && (ch(4) == U' ');
}

DcssStyledLine slice(const DcssStyledLine &line, std::size_t from, std::size_t to)
{
    DcssStyledLine out;
    for (std::size_t i = from; (i < to) && (i < line.size()); ++i) {
        out.push_back(line[i]);
    }
    return out;
}

//! 原作 `FontWrapper::split(text, width, max_height)`: `max_lines` 行までに折り返す（残りは捨てる）。
std::vector<DcssStyledLine> split_lines(const DcssStyledLine &line, int width_px, int max_lines, const DcssTextMeasure &m)
{
    const int cells = std::max(1, width_px / std::max(1, m.half_w));
    std::vector<DcssStyledLine> out = dcss_wrap_line(line, cells);
    if (out.empty()) {
        out.emplace_back();
    }
    if ((max_lines > 0) && (static_cast<int>(out.size()) > max_lines)) {
        out.resize(static_cast<std::size_t>(max_lines));
    }
    return out;
}

} // namespace

DcssMenuLayout dcss_layout_menu(const DcssMenuData &menu, int mw, int num_columns, const DcssTextMeasure &m)
{
    DcssMenuLayout out;
    out.num_columns = std::max(1, num_columns);
    const int text_height = m.line_h;
    const int max_column_width = (mw >= INT_MAX / 2) ? INT_MAX / 2 : (mw / out.num_columns);

    // 絵のある項目が 1 つでもあれば、字を絵の分だけ字下げする（`UIMenu::update_item_...` の `m_draw_tiles`）。
    out.draw_tiles = false;
    for (const DcssMenuItem &item : menu.items) {
        if (!item.tiles.empty()) {
            out.draw_tiles = true;
            break;
        }
    }

    out.entries.resize(menu.items.size());
    for (std::size_t i = 0; i < menu.items.size(); ++i) {
        const DcssMenuItem &item = menu.items[i];
        DcssMenuEntryLayout &e = out.entries[i];
        e.heading = (item.level == 0) || (item.level == 1);
        // 原作は項目の色で始め、閉じタグのあとは薄い灰色（`parse_string` の既定）に戻る。
        e.text = dcss_parse_window_text(item.placeholder ? std::string("...") : item.text, item.colour, kLightGrey);
        e.prefix_len = (!e.heading && has_hotkey_prefix(e.text)) ? 5 : 0;
    }

    int column = -1;
    int column_width = 0;
    int row_height = 0;
    int height = 0;
    int row_count = 0;
    out.row_heights.clear();
    out.row_heights.reserve(menu.items.size() + 1);

    for (std::size_t i = 0; i < menu.items.size(); ++i) {
        DcssMenuEntryLayout &e = out.entries[i];
        column = e.heading ? 0 : ((column + 1) % out.num_columns);
        if (column == 0) {
            row_height += (row_height == 0) ? 0 : (2 * kMenuItemPad);
            height += row_height;
            out.row_heights.push_back(height);
            ++row_count;
            row_height = 0;
        }
        const int text_width = m.cells_width(e.text);
        e.y = height;
        e.row = row_count - 1;
        e.column = column;

        if (e.heading) {
            e.x = 0;
            row_height = text_height + ((i == 0) ? 5 : 10);
            e.wrapped.clear();
            if (out.draw_tiles && (text_width > mw)) {
                e.wrapped = split_lines(e.text, mw, 0, m);
                row_height = std::max(row_height, static_cast<int>(e.wrapped.size()) * text_height);
            } else {
                e.wrapped.push_back(e.text);
            }
            column = out.num_columns - 1;
        } else {
            const int text_indent = out.draw_tiles ? kMenuTextIndent : 0;
            e.x = text_indent;
            int text_sx = text_indent;
            int item_height = std::max(text_height, menu.items[i].tiles.empty() ? 0 : kMenuIconSize);
            DcssStyledLine rest = e.text;
            if (e.prefix_len > 0) {
                text_sx += m.cells_width(slice(e.text, 0, 5));
                rest = slice(e.text, 5, e.text.size());
            }
            e.wrapped.clear();
            e.wrapped.push_back(rest);
            if (((menu.flags & kMfNoWrapRows) == 0) && (text_width > (max_column_width - e.x - kMenuPadRight))) {
                const int w = max_column_width - text_sx - kMenuPadRight;
                if (w > 0) {
                    e.wrapped = split_lines(rest, w, 2, m);
                    const int string_height = std::min(static_cast<int>(e.wrapped.size()) * text_height, text_height * 2);
                    item_height = std::max(item_height, string_height);
                }
            }
            column_width = std::max(column_width, text_sx + text_width + kMenuPadRight);
            row_height = std::max(row_height, item_height);
        }
    }
    row_height += (row_height == 0) ? 0 : (2 * kMenuItemPad);
    height += row_height;
    out.row_heights.push_back(height);
    column_width += 2 * kMenuItemPad;

    out.height = height;
    const int min_col = kMenuMinColumnWidth;
    out.nat_column_width = std::max(min_col, std::min(column_width, max_column_width));
    for (DcssMenuEntryLayout &e : out.entries) {
        const std::size_t r = static_cast<std::size_t>(std::max(0, e.row));
        if ((r + 1) < out.row_heights.size()) {
            e.height = out.row_heights[r + 1] - out.row_heights[r];
        }
    }
    return out;
}

int dcss_menu_preferred_width(const DcssMenuLayout &natural, const DcssTextMeasure &m)
{
    const int max_menu_width = std::min(93 * m.char_w, natural.nat_column_width * natural.num_columns);
    return max_menu_width;
}

bool dcss_menu_item_selectable(const DcssMenuData &menu, int index)
{
    if ((index < 0) || (index >= static_cast<int>(menu.items.size()))) {
        return false;
    }
    const DcssMenuItem &item = menu.items[static_cast<std::size_t>(index)];
    return (item.level == 2) && ((menu.tag == "use_item") || !item.hotkeys.empty());
}

std::vector<DcssStyledLine> dcss_wrap_text(std::string_view formatted, int max_w, int default_colour, const DcssTextMeasure &m)
{
    std::vector<DcssStyledLine> out;
    const DcssStyledLine all = dcss_parse_window_text(formatted, default_colour, default_colour);
    for (const DcssStyledLine &paragraph : dcss_split_styled_lines(all)) {
        if (max_w <= 0) {
            out.push_back(paragraph);
            continue;
        }
        const int cells = std::max(1, max_w / std::max(1, m.half_w));
        if (dcss_line_width(paragraph) <= cells) {
            out.push_back(paragraph);
            continue;
        }
        const std::vector<DcssStyledLine> wrapped = dcss_wrap_line(paragraph, cells);
        out.insert(out.end(), wrapped.begin(), wrapped.end());
    }
    return out;
}

int dcss_lines_width(const std::vector<DcssStyledLine> &lines, const DcssTextMeasure &m)
{
    int w = 0;
    for (const DcssStyledLine &l : lines) {
        w = std::max(w, m.cells_width(l));
    }
    return w;
}

} // namespace hd2d::dcss
