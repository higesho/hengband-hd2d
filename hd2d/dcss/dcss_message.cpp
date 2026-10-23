/*!
 * @file dcss_message.cpp
 * @brief `dcss_message.h` の実装。
 */
#include "dcss/dcss_message.h"

#include <algorithm>

namespace hd2d::dcss {

DcssStyledChar DcssMessageWindow::prefix_glyph(Prefix p)
{
    switch (p) {
    case Prefix::TurnStart:
        return { U'-', kLightGrey };
    case Prefix::TurnEnd:
    case Prefix::NewTurn:
        return { U'_', kLightGrey };
    case Prefix::NewCmd:
        return { U'_', kDarkGrey };
    case Prefix::None:
    default:
        return { U' ', kLightGrey };
    }
}

void DcssMessageWindow::clear_lines()
{
    this->lines_.assign(static_cast<std::size_t>(std::max(this->rows_, 1)), DcssStyledLine{});
    this->next_line_ = 0;
    this->input_line_ = 0;
    this->prompt_ = Prefix::None;
}

void DcssMessageWindow::resize(int cols, int rows)
{
    if ((cols == this->cols_) && (rows == this->rows_)) {
        return;
    }
    this->cols_ = std::max(0, cols);
    this->rows_ = std::max(0, rows);
    this->clear_lines();
    // 作り直しは次の `sync` が持っているメッセージから行う。
    this->consumed_serial_ = 0;
    this->rollback_seen_ = 0;
    this->last_input_mode_ = -1;
    ++this->version_;
}

void DcssMessageWindow::scroll(int n)
{
    n = std::min(n, this->next_line_);
    if (n <= 0) {
        return;
    }
    const int height = static_cast<int>(this->lines_.size());
    for (int i = 0; i < height - n; ++i) {
        this->lines_[static_cast<std::size_t>(i)] = std::move(this->lines_[static_cast<std::size_t>(i + n)]);
    }
    for (int i = height - n; i < height; ++i) {
        this->lines_[static_cast<std::size_t>(i)].clear();
    }
    this->next_line_ -= n;
    this->input_line_ -= n;
}

void DcssMessageWindow::make_space(int n)
{
    int space = this->out_height() - this->next_line_;
    if (space >= n) {
        return;
    }
    if (this->input_line_ > 0) {
        const int s = std::min(this->input_line_, n - space);
        this->scroll(s);
        space += s;
    }
    if (space >= n) {
        return;
    }
    // 原作は `-more-` を出したあと、欄を全部消して上から書き直す（`clear_messages`・`show_more` の既定）。
    this->clear_lines();
}

void DcssMessageWindow::add_item(const DcssMessage &message)
{
    this->prompt_ = Prefix::None;
    const int out_width = std::max(1, this->cols_ - 1); // 先頭の 1 桁は印の字
    const DcssStyledLine styled = dcss_parse_tagged(message.text, kLightGrey);
    for (DcssStyledLine &piece : dcss_wrap_line(styled, out_width)) {
        this->make_space(1);
        DcssStyledLine line;
        line.reserve(piece.size() + 1);
        line.push_back(prefix_glyph(Prefix::None));
        line.insert(line.end(), piece.begin(), piece.end());
        if (this->next_line_ < static_cast<int>(this->lines_.size())) {
            this->lines_[static_cast<std::size_t>(this->next_line_)] = std::move(line);
            ++this->next_line_;
        }
    }
    this->last_message_turn_ = message.turn;
}

void DcssMessageWindow::output_prefix(Prefix p, bool new_turn_glyph)
{
    (void)new_turn_glyph;
    if (static_cast<int>(p) <= static_cast<int>(this->prompt_)) {
        return;
    }
    this->prompt_ = p;
    if (this->next_line_ > 0) {
        DcssStyledLine &line = this->lines_[static_cast<std::size_t>(this->next_line_ - 1)];
        if (!line.empty()) {
            line[0] = prefix_glyph(p);
        }
    }
}

void DcssMessageWindow::rebuild(const DcssUiModel &model)
{
    this->clear_lines();
    const std::vector<DcssMessage> &all = model.messages();
    // 欄に収まる分だけ後ろから数える（古いものは押し出されるので、全部を足す必要が無い）。
    std::size_t first = all.size();
    int lines_needed = 0;
    const int out_width = std::max(1, this->cols_ - 1);
    while (first > 0) {
        const int n = static_cast<int>(dcss_wrap_line(dcss_parse_tagged(all[first - 1].text, kLightGrey), out_width).size());
        if ((lines_needed + n) > this->out_height()) {
            break;
        }
        lines_needed += n;
        --first;
    }
    for (std::size_t i = first; i < all.size(); ++i) {
        this->add_item(all[i]);
    }
}

void DcssMessageWindow::sync(const DcssUiModel &model)
{
    if ((this->cols_ <= 0) || (this->rows_ <= 0)) {
        return;
    }
    bool changed = false;
    if ((model.rollback_serial() != this->rollback_seen_) || (this->consumed_serial_ == 0 && model.message_serial() > 0)) {
        // 取り消しがあった、または欄を作り直した直後。持っているメッセージから作り直す。
        this->rebuild(model);
        this->rollback_seen_ = model.rollback_serial();
        this->consumed_serial_ = model.message_serial();
        changed = true;
    } else if (model.message_serial() > this->consumed_serial_) {
        const std::uint64_t fresh = model.message_serial() - this->consumed_serial_;
        const std::vector<DcssMessage> &all = model.messages();
        const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(fresh, all.size()));
        for (std::size_t i = all.size() - count; i < all.size(); ++i) {
            this->add_item(all[i]);
        }
        this->consumed_serial_ = model.message_serial();
        changed = true;
    }
    // 指令待ちの出入り: 入ったら最後の行に `_`、出たらそこまでを既読にする。
    const int mode = model.input_mode();
    if (mode != this->last_input_mode_) {
        if ((mode == kInputCommand) && (this->last_input_mode_ != kInputCommand)) {
            const bool new_turn = (model.player().turn > this->last_message_turn_);
            this->output_prefix(new_turn ? Prefix::NewTurn : Prefix::NewCmd, new_turn);
            changed = true;
        } else if ((this->last_input_mode_ == kInputCommand) && (mode != kInputCommand)) {
            this->input_line_ = this->next_line_;
        }
        this->last_input_mode_ = mode;
    }
    if (changed) {
        ++this->version_;
    }
}

int DcssMessageWindow::line_count_for_overlay() const
{
    int n = 0;
    for (int i = 0; i < static_cast<int>(this->lines_.size()); ++i) {
        if (!this->lines_[static_cast<std::size_t>(i)].empty()) {
            n = i + 1;
        }
    }
    return n;
}

void DcssMessageWindow::build(const DcssUiModel &model, DcssTextGrid &grid) const
{
    grid.resize(this->cols_, this->rows_);
    for (int i = 0; (i < this->rows_) && (i < static_cast<int>(this->lines_.size())); ++i) {
        grid.goto_xy(1, i + 1);
        grid.put_styled(this->lines_[static_cast<std::size_t>(i)]);
    }
    if (model.more() && (this->rows_ > 0)) {
        // 最終行の 2 桁目から `--more--`（PROMPT チャンネルの色 CYAN）。
        grid.goto_xy(1, this->rows_);
        grid.textcolour(kLightGrey);
        grid.clear_to_end_of_line();
        grid.goto_xy(2, this->rows_);
        grid.textcolour(kCyan);
        grid.putstr(model.more_text().empty() ? std::string("--more--") : model.more_text());
    }
    if (model.text_cursor()) {
        // 文字入力のカーソル（最後の行の終わりの `_`）。
        const int row = std::max(0, this->next_line_ - 1);
        int col = 0;
        if (row < static_cast<int>(this->lines_.size())) {
            col = dcss_line_width(this->lines_[static_cast<std::size_t>(row)]);
        }
        if (col < this->cols_) {
            grid.goto_xy(col + 1, row + 1);
            grid.textcolour(kLightGrey);
            grid.putstr("_");
        }
    }
}

} // namespace hd2d::dcss
