/*!
 * @file dcss_prompt.cpp
 * @brief `dcss_prompt.h` の実装。
 */
#include "dcss/dcss_prompt.h"

#include <algorithm>

namespace hd2d::dcss {

namespace {

constexpr int kEnter = 13;

//! 全角の 2 桁目（詰め物）か。
bool is_filler(const DcssTextGrid &g, int col, int row)
{
    return (g.at(col, row) == kWideFiller) && (col > 0) && (dcss_cell_width(g.at(col - 1, row)) == 2);
}

bool is_ascii_key(char32_t c)
{
    return (c > U' ') && (c < 0x7F) && (c != U'(') && (c != U')');
}

//! 行の最後の空白でない桁の次（空の行は 0）。最後の字が全角ならその 2 桁目（詰め物）まで含める。
int row_end(const DcssTextGrid &g, int row)
{
    for (int col = g.cols - 1; col >= 0; --col) {
        const char32_t c = g.at(col, row);
        if ((c == U' ') || (c == kWideFiller)) {
            continue;
        }
        return std::min(g.cols, col + ((dcss_cell_width(c) == 2) ? 2 : 1));
    }
    return 0;
}

std::string text_of(const DcssTextGrid &g, int row, int begin, int end)
{
    std::string s;
    for (int col = begin; col < end; ++col) {
        const char32_t c = g.at(col, row);
        if ((c == kWideFiller) && is_filler(g, col, row)) {
            continue;
        }
        s += dcss_encode_utf8((c == kWideFiller) ? U' ' : c);
    }
    return s;
}

//! `(キー)ラベル` を行から探す。
void find_tokens(const DcssTextGrid &g, int row, std::vector<DcssPromptHit> &out)
{
    const int end_of_row = row_end(g, row);
    for (int col = 0; col + 2 < end_of_row; ++col) {
        if ((g.at(col, row) != U'(') || !is_ascii_key(g.at(col + 1, row)) || (g.at(col + 2, row) != U')')) {
            continue;
        }
        int label_end = col + 3;
        while (label_end < end_of_row) {
            const char32_t c = g.at(label_end, row);
            if ((c == U'/') || (c == U')') || (c == U'(') || (c == U' ')) {
                break;
            }
            ++label_end;
        }
        if (label_end <= (col + 3)) {
            continue;
        }
        DcssPromptHit hit;
        hit.kind = DcssPromptHit::Kind::Token;
        hit.row = row;
        hit.col_begin = col;
        hit.col_end = label_end;
        hit.keys = { static_cast<int>(g.at(col + 1, row)) };
        hit.label = text_of(g, row, col, label_end);
        out.push_back(std::move(hit));
        col = label_end - 1;
    }
}

//! `"単語"`（打たせる確認の単語）を行から探す。
void find_words(const DcssTextGrid &g, int row, std::vector<DcssPromptHit> &out)
{
    const int end_of_row = row_end(g, row);
    for (int col = 0; col < end_of_row; ++col) {
        if (g.at(col, row) != U'"') {
            continue;
        }
        int end = col + 1;
        std::vector<int> keys;
        while ((end < end_of_row) && (keys.size() <= 16)) {
            const char32_t c = g.at(end, row);
            const bool word = ((c >= U'a') && (c <= U'z')) || ((c >= U'A') && (c <= U'Z')) || ((c >= U'0') && (c <= U'9')) || (c == U'_') || (c == U'-');
            if (!word) {
                break;
            }
            keys.push_back(static_cast<int>(c));
            ++end;
        }
        if (keys.empty() || (end >= end_of_row) || (g.at(end, row) != U'"')) {
            continue;
        }
        DcssPromptHit hit;
        hit.kind = DcssPromptHit::Kind::Word;
        hit.row = row;
        hit.col_begin = col;
        hit.col_end = end + 1;
        hit.keys = keys;
        for (const int k : keys) {
            hit.word.push_back(static_cast<char>(k));
        }
        hit.label = text_of(g, row, col, end + 1);
        out.push_back(std::move(hit));
        col = end;
    }
}

} // namespace

DcssPromptView dcss_prompt_view(const DcssTextGrid &grid, const DcssPromptInput &in)
{
    DcssPromptView view;
    if (in.window_open || (grid.cols <= 0) || (grid.rows <= 0)) {
        return view;
    }
    if (in.more) {
        // 最終行の `--more--`（原作 `message_window::more`）。押すとどこでも進む（強調はこの字だけ）。
        const int row = grid.rows - 1;
        int begin = -1;
        for (int col = 0; col < grid.cols; ++col) {
            if ((grid.at(col, row) != U' ') && (grid.at(col, row) != kWideFiller)) {
                begin = col;
                break;
            }
        }
        const int end = row_end(grid, row);
        if ((begin >= 0) && (end > begin)) {
            DcssPromptHit hit;
            hit.kind = DcssPromptHit::Kind::More;
            hit.row = row;
            hit.col_begin = begin;
            hit.col_end = end;
            hit.keys = { kEnter };
            hit.label = text_of(grid, row, begin, end);
            view.hits.push_back(std::move(hit));
        }
        return view; // 右クリックは何もしない（原作は無視する）
    }
    const bool prompt_mode = (in.input_mode == 7) || (in.input_mode == 8);
    if (!prompt_mode && !in.text_input) {
        return view;
    }
    view.right_escape = true;
    if (in.first_row < 0) {
        return view;
    }
    const int first = in.first_row;
    const int last = std::min(grid.rows - 1, first + std::max(0, in.row_count) - 1);
    if (in.text_input) {
        view.block_first_row = first;
        view.block_rows = std::max(0, last - first + 1);
        for (int row = first; row <= last; ++row) {
            find_words(grid, row, view.hits);
        }
    } else {
        for (int row = first; row <= last; ++row) {
            find_tokens(grid, row, view.hits);
        }
    }
    return view;
}

int dcss_grid_col_x(const DcssTextGrid &grid, int row, int col, int half)
{
    int x = 0;
    for (int c = 0; (c < col) && (c < grid.cols); ++c) {
        const char32_t ch = grid.at(c, row);
        if (ch == kWideFiller) {
            if ((c > 0) && (dcss_cell_width(grid.at(c - 1, row)) == 2)) {
                continue; // 全角の 2 桁目は 1 桁目が幅を持つ
            }
            x += half;
            continue;
        }
        x += (dcss_cell_width(ch) == 2) ? (half * 2) : half;
    }
    return x;
}

int dcss_grid_x_col(const DcssTextGrid &grid, int row, int x, int half)
{
    if ((x < 0) || (half <= 0)) {
        return -1;
    }
    int acc = 0;
    for (int c = 0; c < grid.cols; ++c) {
        const char32_t ch = grid.at(c, row);
        int adv = half;
        if (ch == kWideFiller) {
            if ((c > 0) && (dcss_cell_width(grid.at(c - 1, row)) == 2)) {
                continue; // 全角の 2 桁目
            }
        } else if (dcss_cell_width(ch) == 2) {
            adv = half * 2;
        }
        if (x < (acc + adv)) {
            return c;
        }
        acc += adv;
    }
    return -1;
}

} // namespace hd2d::dcss
