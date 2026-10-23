// DCSS コア: 原作（ウェブタイル版）が出す「色つきの字」を、HD2D のフレームの字＋色の区間に直す部品。
// UTF-8 の BOM は付けない（プロトコルの TU は /source-charset:utf-8 で組む）。外のライブラリ・原作のヘッダに依存しない。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dcss_text {

//! DCSS の色（0〜15。defines.h の COLOURS）→ HD2D の `TERM_*` の番号（既定の `term_palette`）。
//! 0 黒・1 青・2 緑・3 青緑・4 赤・5 赤紫・6 茶・7 明るい灰・8 暗い灰・9 明るい青・10 明るい緑・11 明るい青緑・12 明るい赤・13 明るい赤紫・14 黄・15 白。
inline std::uint8_t term_color(int dcss_color)
{
    static const std::uint8_t kMap[16] = {
        0,  // 黒 → TERM_DARK
        6,  // 青 → TERM_BLUE
        5,  // 緑 → TERM_GREEN
        14, // 青緑 → TERM_L_BLUE（水色）
        4,  // 赤 → TERM_RED
        10, // 赤紫 → TERM_VIOLET
        7,  // 茶 → TERM_UMBER
        9,  // 明るい灰（普通の字の色）→ TERM_L_WHITE
        2,  // 暗い灰 → TERM_SLATE
        6,  // 明るい青 → TERM_BLUE
        13, // 明るい緑 → TERM_L_GREEN
        14, // 明るい青緑 → TERM_L_BLUE
        12, // 明るい赤 → TERM_L_RED
        10, // 明るい赤紫 → TERM_VIOLET
        11, // 黄 → TERM_YELLOW
        1,  // 白 → TERM_WHITE
    };
    return kMap[dcss_color & 15];
}

//! 色の名前（書式の `<red>` など）→ DCSS の色番号。知らない名前（`<w>` `<h>` や元素の色）は -1。
inline int color_by_name(const std::string &name)
{
    static const char *const kNames[16] = { "black", "blue", "green", "cyan", "red", "magenta", "brown", "lightgrey",
        "darkgrey", "lightblue", "lightgreen", "lightcyan", "lightred", "lightmagenta", "yellow", "white" };
    for (int i = 0; i < 16; ++i) {
        if (name == kNames[i]) return i;
    }
    if (name == "lightgray") return 7;
    if (name == "darkgray") return 8;
    return -1;
}

struct Span {
    int begin = 0; //!< UTF-8 のバイト位置
    int len = 0;
    std::uint8_t color = 1; //!< HD2D の TERM_* の番号
};

struct Colored {
    std::string text;
    std::vector<Span> spans; //!< 隙間なく並ぶ（空なら全体が 1 色）。2 区間以上のときだけ使う
    std::uint8_t color = 1;  //!< 行の代表色（最初に色が付いた字の色。無ければ既定）
};

namespace detail {
inline void append_span(Colored &out, int begin, std::uint8_t color)
{
    if (begin >= static_cast<int>(out.text.size())) return;
    // 直前の区間と同じ色ならつなぐ。
    if (!out.spans.empty() && out.spans.back().color == color && out.spans.back().begin + out.spans.back().len == begin) {
        out.spans.back().len = static_cast<int>(out.text.size()) - out.spans.back().begin;
        return;
    }
    out.spans.push_back(Span{ begin, static_cast<int>(out.text.size()) - begin, color });
}

//! 区間の整理: 隙間を既定色で埋め、1 区間だけなら代表色にして捨てる。
inline void finish(Colored &out, std::uint8_t default_color)
{
    // 抜けている区間（色の指定が無かった所）を既定色で埋める。
    std::vector<Span> filled;
    int pos = 0;
    for (const Span &s : out.spans) {
        if (s.begin > pos) filled.push_back(Span{ pos, s.begin - pos, default_color });
        filled.push_back(s);
        pos = s.begin + s.len;
    }
    if (pos < static_cast<int>(out.text.size()) && !out.spans.empty()) {
        filled.push_back(Span{ pos, static_cast<int>(out.text.size()) - pos, default_color });
    }
    // 同じ色の隣をつなぐ。
    std::vector<Span> merged;
    for (const Span &s : filled) {
        if (!merged.empty() && merged.back().color == s.color && merged.back().begin + merged.back().len == s.begin) {
            merged.back().len += s.len;
        } else {
            merged.push_back(s);
        }
    }
    out.spans = std::move(merged);
    out.color = out.spans.empty() ? default_color : out.spans.front().color;
    if (out.spans.size() <= 1) out.spans.clear();
}
} // namespace detail

/*!
 * DCSS の書式つきの字（`<red>…</red>`、`<` は `<<`）を字と色の区間に直す。
 * 閉じていないタグも、`</…>` が多すぎる場合も落ちない。知らないタグ（`<w>` など）は色を変えずに捨てる。
 */
inline Colored parse_formatted(const std::string &input, int default_dcss_color = 7)
{
    const std::uint8_t base = term_color(default_dcss_color);
    Colored out;
    std::vector<std::uint8_t> stack;
    std::uint8_t current = base;
    int span_begin = 0;
    const auto flush = [&]() {
        detail::append_span(out, span_begin, current);
        span_begin = static_cast<int>(out.text.size());
    };
    for (std::size_t i = 0; i < input.size();) {
        const char c = input[i];
        if (c == '<') {
            if (i + 1 < input.size() && input[i + 1] == '<') {
                out.text.push_back('<');
                i += 2;
                continue;
            }
            const std::size_t close = input.find('>', i + 1);
            if (close == std::string::npos) { // 閉じていない '<' は字として扱う
                out.text.push_back(c);
                ++i;
                continue;
            }
            std::string tag = input.substr(i + 1, close - i - 1);
            i = close + 1;
            flush();
            if (!tag.empty() && tag[0] == '/') {
                if (!stack.empty()) {
                    current = stack.back();
                    stack.pop_back();
                }
            } else {
                const int col = color_by_name(tag);
                stack.push_back(current);
                if (col >= 0) current = term_color(col);
            }
            continue;
        }
        out.text.push_back(c);
        ++i;
    }
    flush();
    detail::finish(out, base);
    return out;
}

//! 書式のタグを取り除いた字だけ。
inline std::string strip_formatted(const std::string &input) { return parse_formatted(input).text; }

namespace detail {
inline void decode_entities(std::string &s)
{
    static const struct {
        const char *from;
        const char *to;
    } kEntities[] = { { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&#39;", "'" }, { "&nbsp;", " " }, { "&amp;", "&" } };
    for (const auto &e : kEntities) {
        std::size_t pos = 0;
        const std::string from = e.from;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), e.to);
            pos += 1;
        }
    }
}
} // namespace detail

/*!
 * 文字画面（`txt`）の 1 行の HTML（`<span class="fgN bgN">…</span>` の並び）を字と色の区間に直す。
 * 先頭が span で始まらない字（クラスの無い字）は既定色。行末の空白は落とす。
 */
inline Colored parse_html_line(const std::string &input)
{
    const std::uint8_t base = term_color(7);
    Colored out;
    std::uint8_t current = base;
    int span_begin = 0;
    std::string pending;
    const auto flush = [&]() {
        detail::decode_entities(pending);
        out.text += pending;
        pending.clear();
        detail::append_span(out, span_begin, current);
        span_begin = static_cast<int>(out.text.size());
    };
    for (std::size_t i = 0; i < input.size();) {
        if (input[i] == '<') {
            const std::size_t close = input.find('>', i + 1);
            if (close == std::string::npos) {
                pending.push_back(input[i]);
                ++i;
                continue;
            }
            const std::string tag = input.substr(i + 1, close - i - 1);
            i = close + 1;
            flush();
            if (tag.rfind("span", 0) == 0) {
                const std::size_t fg = tag.find("fg");
                if (fg != std::string::npos) {
                    current = term_color(std::atoi(tag.c_str() + fg + 2));
                } else {
                    current = base;
                }
            } else if (tag == "/span") {
                current = base;
            }
            continue;
        }
        pending.push_back(input[i]);
        ++i;
    }
    flush();
    while (!out.text.empty() && out.text.back() == ' ') out.text.pop_back();
    // 行末の空白を落としたぶん、区間も縮める。
    while (!out.spans.empty() && out.spans.back().begin >= static_cast<int>(out.text.size())) out.spans.pop_back();
    if (!out.spans.empty()) {
        Span &last = out.spans.back();
        last.len = static_cast<int>(out.text.size()) - last.begin;
    }
    detail::finish(out, base);
    return out;
}

//! UTF-8 で 1 文字の長さ（バイト）。
inline int utf8_len(unsigned char lead)
{
    if (lead < 0x80) return 1;
    if ((lead >> 5) == 0x6) return 2;
    if ((lead >> 4) == 0xE) return 3;
    if ((lead >> 3) == 0x1E) return 4;
    return 1;
}

//! UTF-8 の文字列 → 符号点の並び。
inline std::vector<std::uint32_t> utf8_to_codepoints(const std::string &s)
{
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const int n = utf8_len(c);
        std::uint32_t cp = 0;
        if (n == 1) cp = c;
        else if (n == 2) cp = c & 0x1F;
        else if (n == 3) cp = c & 0x0F;
        else cp = c & 0x07;
        for (int k = 1; k < n && i + static_cast<std::size_t>(k) < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
        out.push_back(cp);
        i += static_cast<std::size_t>(n);
    }
    return out;
}

} // namespace dcss_text
