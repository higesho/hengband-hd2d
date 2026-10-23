/*!
 * @file dcss_cells.cpp
 * @brief `dcss_cells.h` の実装。
 */
#include "dcss/dcss_cells.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace hd2d::dcss {

namespace {

// 原作 `tilefont.cc` の `term_colours`。
constexpr std::array<DcssRgb, kColourCount> kTermColours{ {
    { 0, 0, 0 },       // BLACK
    { 0, 82, 255 },    // BLUE
    { 100, 185, 70 },  // GREEN
    { 0, 180, 180 },   // CYAN
    { 255, 48, 0 },    // RED
    { 205, 33, 205 },  // MAGENTA
    { 165, 91, 0 },    // BROWN
    { 162, 162, 162 }, // LIGHTGREY
    { 82, 82, 82 },    // DARKGREY
    { 82, 102, 255 },  // LIGHTBLUE
    { 82, 255, 82 },   // LIGHTGREEN
    { 82, 255, 255 },  // LIGHTCYAN
    { 255, 82, 82 },   // LIGHTRED
    { 255, 88, 255 },  // LIGHTMAGENTA
    { 255, 255, 82 },  // YELLOW
    { 255, 255, 255 }, // WHITE
} };

constexpr const char *kColourNames[kColourCount] = {
    "black", "blue", "green", "cyan", "red", "magenta", "brown", "lightgrey",
    "darkgrey", "lightblue", "lightgreen", "lightcyan", "lightred", "lightmagenta", "yellow", "white",
};

//! 行頭に置けない字・行末に置けない字（原作 `wide_char_break_ok`）。
bool is_no_line_start(char32_t c)
{
    static const char32_t kSet[] = U"、。，．・：；？！ー…‥）〕］｝〉》」』】々゛゜ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ.,:;!?)]}'\"%";
    for (const char32_t *p = kSet; *p != 0; ++p) {
        if (*p == c) {
            return true;
        }
    }
    return false;
}

bool is_no_line_end(char32_t c)
{
    static const char32_t kSet[] = U"（〔［｛〈《「『【([{";
    for (const char32_t *p = kSet; *p != 0; ++p) {
        if (*p == c) {
            return true;
        }
    }
    return false;
}

bool wide_char_break_ok(char32_t prev, char32_t next)
{
    if ((prev == 0) || (next == 0) || (prev == U' ') || (next == U' ')) {
        return false;
    }
    if ((dcss_cell_width(prev) != 2) && (dcss_cell_width(next) != 2)) {
        return false;
    }
    if (is_no_line_start(next) || is_no_line_end(prev)) {
        return false;
    }
    return true;
}

} // namespace

const DcssRgb &dcss_term_rgb(int colour)
{
    if ((colour < 0) || (colour >= kColourCount)) {
        return kTermColours[kLightGrey];
    }
    return kTermColours[static_cast<std::size_t>(colour)];
}

int dcss_colour_from_name(std::string_view name, int fallback)
{
    for (int i = 0; i < kColourCount; ++i) {
        if (name == kColourNames[i]) {
            return i;
        }
    }
    if (name == "lightgray") {
        return kLightGrey;
    }
    if (name == "darkgray") {
        return kDarkGrey;
    }
    return fallback;
}

int dcss_cell_width(char32_t cp)
{
    if (cp == 0) {
        return 0;
    }
    if ((cp < 32) || ((cp >= 0x7F) && (cp < 0xA0))) {
        return 0;
    }
    if (((cp >= 0x0300) && (cp <= 0x036F)) || (cp == 0x200B) || (cp == 0x200C) || (cp == 0x200D) || (cp == 0xFEFF)) {
        return 0;
    }
    if ((cp >= 0x1100) && ((cp <= 0x115F) || (cp == 0x2329) || (cp == 0x232A)
                              || ((cp >= 0x2E80) && (cp <= 0xA4CF) && (cp != 0x303F))
                              || ((cp >= 0xAC00) && (cp <= 0xD7A3))
                              || ((cp >= 0xF900) && (cp <= 0xFAFF))
                              || ((cp >= 0xFE30) && (cp <= 0xFE6F))
                              || ((cp >= 0xFF00) && (cp <= 0xFF60))
                              || ((cp >= 0xFFE0) && (cp <= 0xFFE6))
                              || ((cp >= 0x20000) && (cp <= 0x3FFFD)))) {
        return 2;
    }
    return 1;
}

std::vector<char32_t> dcss_decode_utf8(std::string_view text)
{
    std::vector<char32_t> out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        char32_t cp = 0xFFFD;
        std::size_t extra = 0;
        if (lead < 0x80) {
            cp = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            cp = lead & 0x1F;
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            cp = lead & 0x0F;
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            cp = lead & 0x07;
            extra = 3;
        }
        if ((extra > 0) && ((i + extra) < text.size())) {
            bool ok = true;
            for (std::size_t k = 1; k <= extra; ++k) {
                const auto b = static_cast<unsigned char>(text[i + k]);
                if ((b & 0xC0) != 0x80) {
                    ok = false;
                    break;
                }
                cp = (cp << 6) | (b & 0x3F);
            }
            if (!ok) {
                cp = 0xFFFD;
                extra = 0;
            }
        } else if (extra > 0) {
            cp = 0xFFFD;
            extra = 0;
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

std::string dcss_encode_utf8(char32_t cp)
{
    std::string out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

int dcss_text_width(std::string_view utf8)
{
    int w = 0;
    for (const char32_t c : dcss_decode_utf8(utf8)) {
        w += dcss_cell_width(c);
    }
    return w;
}

std::string dcss_chop_string(std::string_view utf8, int width, bool pad)
{
    std::string out;
    int used = 0;
    for (const char32_t c : dcss_decode_utf8(utf8)) {
        const int cw = dcss_cell_width(c);
        if ((used + cw) > width) {
            break;
        }
        out += dcss_encode_utf8(c);
        used += cw;
    }
    if (pad) {
        while (used < width) {
            out.push_back(' ');
            ++used;
        }
    }
    return out;
}

DcssStyledLine dcss_parse_tagged(std::string_view text, int default_colour)
{
    DcssStyledLine out;
    std::vector<int> stack;
    int current = default_colour;
    const std::vector<char32_t> chars = dcss_decode_utf8(text);
    for (std::size_t i = 0; i < chars.size(); ++i) {
        const char32_t c = chars[i];
        if (c == U'<') {
            if (((i + 1) < chars.size()) && (chars[i + 1] == U'<')) {
                out.push_back({ U'<', static_cast<std::uint8_t>(current) });
                ++i;
                continue;
            }
            // タグ。'>' までを読む。
            std::size_t j = i + 1;
            std::string name;
            bool closing = false;
            if ((j < chars.size()) && (chars[j] == U'/')) {
                closing = true;
                ++j;
            }
            while ((j < chars.size()) && (chars[j] != U'>')) {
                name += dcss_encode_utf8(chars[j]);
                ++j;
            }
            if (j >= chars.size()) {
                // 閉じていないタグ。字として出す（原作は誤りを表示するが、ここでは落とさない）。
                out.push_back({ c, static_cast<std::uint8_t>(current) });
                continue;
            }
            const int col = dcss_colour_from_name(name, -1);
            if (closing) {
                if (!stack.empty()) {
                    stack.pop_back();
                }
                current = stack.empty() ? default_colour : stack.back();
            } else if (col >= 0) {
                stack.push_back(col);
                current = col;
            }
            i = j;
            continue;
        }
        out.push_back({ c, static_cast<std::uint8_t>(current) });
    }
    return out;
}

int dcss_line_width(const DcssStyledLine &line)
{
    int w = 0;
    for (const DcssStyledChar &c : line) {
        w += dcss_cell_width(c.ch);
    }
    return w;
}

std::vector<DcssStyledLine> dcss_wrap_line(const DcssStyledLine &line, int width)
{
    std::vector<DcssStyledLine> out;
    if (width < 1) {
        return out;
    }
    std::size_t start = 0;
    const std::size_t n = line.size();
    if (n == 0) {
        out.emplace_back();
        return out;
    }
    while (start < n) {
        // 原作 `wordwrap_line`: 1 行ぶんを切り出す。
        int room = width;
        std::size_t cp = start;
        long space = -1; // 切れる位置（その字の手前で切る）
        bool seen_nonspace = false;
        char32_t prev = 0;
        bool newline = false;
        bool fits_all = true;
        while (cp < n) {
            const char32_t c = line[cp].ch;
            int cw = dcss_cell_width(c);
            if (c == U' ') {
                if (seen_nonspace) {
                    space = static_cast<long>(cp);
                }
            } else if (c == U'\n') {
                space = static_cast<long>(cp);
                newline = true;
                fits_all = false;
                break;
            } else {
                if (seen_nonspace && wide_char_break_ok(prev, c)) {
                    space = static_cast<long>(cp);
                }
                seen_nonspace = true;
            }
            if (cw > room) {
                fits_all = false;
                break;
            }
            if (cw >= 0) {
                room -= cw;
            }
            if (cw > 0) {
                prev = c;
            }
            ++cp;
        }
        if (fits_all) {
            out.emplace_back(line.begin() + static_cast<std::ptrdiff_t>(start), line.end());
            break;
        }
        std::size_t cut = cp;
        if (space >= 0) {
            cut = static_cast<std::size_t>(space);
        }
        if (cut == start) {
            cut = std::min(n, start + 1); // 1 字も入らない（幅が狭すぎる）。無限に回らないよう 1 字進める
        }
        out.emplace_back(line.begin() + static_cast<std::ptrdiff_t>(start), line.begin() + static_cast<std::ptrdiff_t>(cut));
        std::size_t next = cut;
        while ((next < n) && (line[next].ch == U' ')) {
            ++next;
        }
        if ((next < n) && (line[next].ch == U'\n')) {
            ++next;
        }
        (void)newline;
        start = next;
        if (start >= n) {
            break;
        }
    }
    return out;
}

void DcssTextGrid::resize(int new_cols, int new_rows)
{
    this->cols = std::max(0, new_cols);
    this->rows = std::max(0, new_rows);
    this->ch.assign(static_cast<std::size_t>(this->cols) * static_cast<std::size_t>(this->rows), U' ');
    this->attr.assign(this->ch.size(), static_cast<std::uint8_t>(kLightGrey));
    this->print_x = 0;
    this->print_y = 0;
    this->text_col = kLightGrey;
}

void DcssTextGrid::clear()
{
    std::fill(this->ch.begin(), this->ch.end(), U' ');
    std::fill(this->attr.begin(), this->attr.end(), static_cast<std::uint8_t>(kLightGrey));
    this->print_x = 0;
    this->print_y = 0;
    this->text_col = kLightGrey;
}

void DcssTextGrid::putstr(std::string_view utf8)
{
    if ((this->print_y < 0) || (this->print_y >= this->rows)) {
        // 行の外は書かない（位置だけ進める）。
        for (const char32_t c : dcss_decode_utf8(utf8)) {
            this->print_x += std::max(0, dcss_cell_width(c));
        }
        return;
    }
    const std::size_t row = static_cast<std::size_t>(this->print_y) * static_cast<std::size_t>(this->cols);
    const int x = this->print_x;
    // 全角の 2 マス目から上書きするときは、1 マス目の残りを空白にする。
    if ((x > 0) && (x < this->cols) && (this->ch[row + static_cast<std::size_t>(x)] == kWideFiller)) {
        this->ch[row + static_cast<std::size_t>(x) - 1] = U' ';
    }
    int cells = 0;
    for (const char32_t c : dcss_decode_utf8(utf8)) {
        const int w = dcss_cell_width(c);
        if (w <= 0) {
            continue;
        }
        const bool wide = (w == 2);
        const int cx = x + cells;
        cells += wide ? 2 : 1;
        if ((cx < 0) || (cx >= this->cols)) {
            continue;
        }
        if (wide && ((cx + 1) >= this->cols)) {
            this->ch[row + static_cast<std::size_t>(cx)] = U' ';
            this->attr[row + static_cast<std::size_t>(cx)] = static_cast<std::uint8_t>(this->text_col);
            continue;
        }
        this->ch[row + static_cast<std::size_t>(cx)] = c;
        this->attr[row + static_cast<std::size_t>(cx)] = static_cast<std::uint8_t>(this->text_col);
        if (wide) {
            this->ch[row + static_cast<std::size_t>(cx) + 1] = kWideFiller;
            this->attr[row + static_cast<std::size_t>(cx) + 1] = static_cast<std::uint8_t>(this->text_col);
        }
    }
    if (((x + cells) >= 0) && ((x + cells) < this->cols) && (this->ch[row + static_cast<std::size_t>(x + cells)] == kWideFiller)) {
        this->ch[row + static_cast<std::size_t>(x + cells)] = U' ';
    }
    this->print_x += cells;
}

void DcssTextGrid::putwch(char32_t cp)
{
    if (cp == 0) {
        cp = U' ';
    }
    this->putstr(dcss_encode_utf8(cp));
}

void DcssTextGrid::put_styled(const DcssStyledLine &line)
{
    const int saved = this->text_col;
    for (const DcssStyledChar &c : line) {
        this->text_col = (this->text_col & 0xF0) | (c.colour & 0x0F);
        this->putwch(c.ch);
    }
    this->text_col = saved;
}

void DcssTextGrid::clear_to_end_of_line()
{
    if ((this->print_y < 0) || (this->print_y >= this->rows)) {
        return;
    }
    const std::size_t row = static_cast<std::size_t>(this->print_y) * static_cast<std::size_t>(this->cols);
    const int cx = this->print_x;
    if ((cx > 0) && (cx < this->cols) && (this->ch[row + static_cast<std::size_t>(cx)] == kWideFiller)) {
        this->ch[row + static_cast<std::size_t>(cx) - 1] = U' ';
    }
    for (int i = std::max(0, cx); i < this->cols; ++i) {
        this->ch[row + static_cast<std::size_t>(i)] = U' ';
        this->attr[row + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(this->text_col);
    }
}

} // namespace hd2d::dcss
