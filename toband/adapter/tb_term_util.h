/*!
 * @file tb_term_util.h
 * @brief Term ミラー（`GameFrame::menu_term_lines`）を読むための小さな補助（TOband のアダプタの窓の組み立て用）。
 *
 * 能力値の窓（`tb_birth_stats.cpp`）が自分の無名名前空間に持っているものと同じ働きの関数を、
 * スキルレベル・建物の結果ページの窓（`tb_skill_level.cpp`・`tb_building_page.cpp`）が使えるように切り出したもの。
 * 字は UTF-8。桁（列）は端末の桁で、全角は 2 桁と数える（共用部分の `presentation::modern_column_byte()` と同じ）。
 */
#pragma once

#include "bridge/modern_content.h"
#include "frame/game_frame.h"

#include <string>
#include <string_view>
#include <vector>

namespace tb::term {

/*!
 * @brief UTF-8 の文字列リテラル（C++20 の `char8_t`）を `const char *` にする。
 * @note **呼ぶ側は `u8c(u8"…")` と、リテラルの字面に `u8` を書くこと。** Linux・Android の組み立ては、アダプタの UTF-8 のソースを
 * CP932 の変換ツリーへ写してから組む（`tools/transcode_cp932_src.py`）。写すときは、`u8` の接頭辞があるリテラルだけを UTF-8 のまま保ち、
 * 接頭辞の無い文字列は CP932 のバイト列にする。マクロの中で `u8##literal` と貼り合わせると、写す時点では接頭辞の無い文字列に見え、
 * Linux・Android だけ日本語が CP932 のバイト列になって、画面の字（UTF-8）と食い違う（Windows は通る）。
 */
inline const char *u8c(const char8_t *literal)
{
    return reinterpret_cast<const char *>(literal);
}

inline std::string trim(const std::string &text)
{
    size_t begin = 0, end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

//! 原作の行番号（`source_row`）で行を探す。無ければ nullptr。
inline const TermMirrorLine *line_at_row(const GameFrame &frame, int source_row)
{
    for (const auto &line : frame.menu_term_lines) {
        if (line.source_row == source_row) {
            return &line;
        }
    }
    return nullptr;
}

//! 行の字を、原作の行番号で取る（無ければ空）。
inline std::string text_at_row(const GameFrame &frame, int source_row)
{
    const TermMirrorLine *line = line_at_row(frame, source_row);
    return line ? line->text_utf8 : std::string();
}

//! 端末の桁 `from` から `to`（含まない）までの字。`to` が負なら行末まで。全角を 2 桁と数える。
inline std::string slice_columns(const std::string &text, int from, int to = -1)
{
    const presentation::ModernColumnScope scope("legacy");
    const size_t begin = presentation::modern_column_byte(text, from);
    const size_t end = to < 0 ? text.size() : presentation::modern_column_byte(text, to);
    if (begin >= text.size() || end <= begin) {
        return std::string();
    }
    return text.substr(begin, end - begin);
}

//! バイト位置を端末の桁に直す。
inline int column_of_byte(const std::string &text, size_t byte)
{
    const presentation::ModernColumnScope scope("legacy");
    int column = 0;
    while (presentation::modern_column_byte(text, column) < byte && column < static_cast<int>(text.size()) * 2) {
        ++column;
    }
    return column;
}

//! 空白で区切った語。
inline std::vector<std::string> tokens_of(const std::string &text)
{
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (c == ' ' || c == '\t') {
            if (!current.empty()) {
                out.push_back(std::move(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        out.push_back(std::move(current));
    }
    return out;
}

//! 先頭が `prefix` か。
inline bool starts_with(const std::string &text, const char *prefix)
{
    return text.rfind(prefix, 0) == 0;
}

//! 部分文字列を含むか。
inline bool contains(const std::string &text, const char *needle)
{
    return text.find(needle) != std::string::npos;
}

//! その行の先頭の字の色（`color_spans` が空なら行の色）。
inline int first_char_color(const TermMirrorLine &line)
{
    const size_t first = line.text_utf8.find_first_not_of(' ');
    if (first == std::string::npos) {
        return line.color & 15;
    }
    for (const auto &span : line.color_spans) {
        if (static_cast<int>(first) >= span.begin && static_cast<int>(first) < span.begin + span.len) {
            return span.color & 15;
        }
    }
    return line.color & 15;
}

} // namespace tb::term
