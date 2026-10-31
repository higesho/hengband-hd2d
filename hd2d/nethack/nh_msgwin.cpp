/*!
 * @file nh_msgwin.cpp
 * @brief `nh_msgwin.h` の実装。
 */
#include "nethack/nh_msgwin.h"

#include "dcss/dcss_cells.h"

#include <algorithm>

namespace hd2d::nethack {

namespace {

bool is_blank(const std::string &text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r'); });
}

bool ends_with_newline(const std::string &text)
{
    return !text.empty() && (text.back() == '\n');
}

//! 折り返す。`dcss_wrap_line` は字の色を持つ列を受けるので、色は使わずに包んで戻す。
std::vector<std::string> wrap_text(const std::string &text, int cols)
{
    using hd2d::dcss::DcssStyledChar;
    using hd2d::dcss::DcssStyledLine;
    DcssStyledLine line;
    for (const char32_t cp : hd2d::dcss::dcss_decode_utf8(text)) {
        line.push_back(DcssStyledChar{ cp, 0 });
    }
    std::vector<std::string> out;
    for (const DcssStyledLine &piece : hd2d::dcss::dcss_wrap_line(line, std::max(2, cols))) {
        std::string s;
        for (const DcssStyledChar &c : piece) {
            s += hd2d::dcss::dcss_encode_utf8(c.ch);
        }
        out.push_back(std::move(s));
    }
    if (out.empty()) {
        out.emplace_back();
    }
    return out;
}

} // namespace

std::string nh_strip_newline(std::string text)
{
    while (!text.empty() && ((text.back() == '\n') || (text.back() == '\r'))) {
        text.pop_back();
    }
    return text;
}

bool NhMsgWindow::can_append(int attr, const std::string &text) const
{
    if (this->lines_not_seen_ == 0) {
        return false; // そのターンの最初の行（`can_append_text`）
    }
    if (this->lines_.empty()) {
        return false;
    }
    const NhMsgLine &last = this->lines_.back();
    if (last.attr != attr) {
        return false;
    }
    if (ends_with_newline(last.text)) {
        return false;
    }
    const std::string joined = nh_strip_newline(last.text + "  " + text);
    return hd2d::dcss::dcss_text_width(joined) + kMoreWidth <= this->cols_;
}

void NhMsgWindow::begin_nohistory()
{
    if (this->nohistory_active_) {
        return;
    }
    if (this->lines_.empty()) {
        this->lines_.emplace_back();
    }
    this->saved_line_ = this->lines_.back();
    this->saved_last_turn_ = this->lines_last_turn_;
    this->saved_not_seen_ = this->lines_not_seen_;
    this->nohistory_active_ = true;
    this->lines_.back().text.clear();
}

void NhMsgWindow::end_nohistory()
{
    if (!this->nohistory_active_) {
        return;
    }
    if (!this->lines_.empty()) {
        this->lines_.back() = this->saved_line_;
    }
    this->lines_last_turn_ = this->saved_last_turn_;
    this->lines_not_seen_ = this->saved_not_seen_;
    this->nohistory_active_ = false;
}

void NhMsgWindow::put(int attr, const std::string &text, bool nohistory)
{
    const bool no_history = nohistory || ((attr & kAtrNoHistory) != 0);
    const int base_attr = attr & ~(kAtrNoHistory | kAtrUrgent);
    if (!no_history && this->nohistory_active_) {
        this->end_nohistory();
    }
    if (no_history) {
        this->begin_nohistory();
        this->lines_.back().attr = base_attr;
        this->lines_.back().text = text;
        return;
    }
    if (this->can_append(base_attr, text)) {
        this->lines_.back().text += "  " + text;
        return;
    }
    // 最終行が空白だけなら上書き、そうでなければ末尾に足す（原作は固定長の配列を詰める）。
    if (this->lines_.empty() || !is_blank(this->lines_.back().text)) {
        this->lines_.emplace_back();
    }
    this->lines_.back().attr = base_attr;
    this->lines_.back().text = text;
    while (static_cast<int>(this->lines_.size()) > kMaxLines) {
        this->lines_.pop_front();
    }
    ++this->lines_not_seen_;
    ++this->lines_last_turn_;
}

void NhMsgWindow::clear()
{
    this->end_nohistory();
    this->lines_last_turn_ = 0;
    this->lines_not_seen_ = 0;
}

std::vector<NhMsgWindow::Row> NhMsgWindow::rows(int max_rows, const std::string &more_token) const
{
    std::vector<Row> out;
    if (max_rows <= 0) {
        return out;
    }
    const int n = static_cast<int>(this->lines_.size());
    for (int idx = n - 1; (idx >= 0) && (static_cast<int>(out.size()) < max_rows); --idx) {
        const NhMsgLine &line = this->lines_[static_cast<std::size_t>(idx)];
        std::string text = nh_strip_newline(line.text);
        if ((idx == n - 1) && !more_token.empty()) {
            text += more_token;
        }
        if (text.empty() && (idx != n - 1)) {
            continue; // 空の行は詰める（原作は空の行も 1 行を取るが、履歴の行は空にならない）
        }
        const std::vector<std::string> pieces = wrap_text(text, this->cols_);
        const bool current = idx >= n - this->lines_last_turn_;
        for (int k = static_cast<int>(pieces.size()) - 1; (k >= 0) && (static_cast<int>(out.size()) < max_rows); --k) {
            out.push_back(Row{ pieces[static_cast<std::size_t>(k)], line.attr, current });
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace hd2d::nethack
