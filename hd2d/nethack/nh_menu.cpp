/*!
 * @file nh_menu.cpp
 * @brief `nh_menu.h` の実装。
 */
#include "nethack/nh_menu.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace hd2d::nethack {

namespace {

using nlohmann::json;

std::string str_of(const json &o, const char *key)
{
    const auto it = o.find(key);
    return ((it != o.end()) && it->is_string()) ? it->get<std::string>() : std::string();
}

int int_of(const json &o, const char *key, int fallback)
{
    const auto it = o.find(key);
    if ((it == o.end()) || !it->is_number()) {
        return fallback;
    }
    return it->is_number_integer() ? static_cast<int>(it->get<long long>()) : static_cast<int>(it->get<double>());
}

//! 選ぶ字（1 文字の文字列。数のときはその字）。
std::string char_of(const json &o, const char *key)
{
    const auto it = o.find(key);
    if (it == o.end()) {
        return {};
    }
    if (it->is_string()) {
        return it->get<std::string>().substr(0, 1);
    }
    if (it->is_number_integer()) {
        const long long v = it->get<long long>();
        if ((v > 0) && (v < 128)) {
            return std::string(1, static_cast<char>(v));
        }
    }
    return {};
}

bool is_digit(char c)
{
    return (c >= '0') && (c <= '9');
}

bool is_letter(char c)
{
    return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'));
}

} // namespace

bool NhMenu::load(const std::string &payload)
{
    const json m = json::parse(payload, nullptr, false);
    if (m.is_discarded() || !m.is_object() || (str_of(m, "k") != "menu")) {
        return false;
    }
    *this = NhMenu{};
    this->request_ = int_of(m, "r", 0);
    this->window_id_ = int_of(m, "id", -1);
    this->how_ = std::clamp(int_of(m, "how", 0), 0, 2);
    this->prompt_ = str_of(m, "prompt");
    const auto items = m.find("items");
    if ((items != m.end()) && items->is_array()) {
        for (const json &e : *items) {
            if (!e.is_object()) {
                continue;
            }
            NhMenuItem item;
            item.index = std::max(0, int_of(e, "i", 0));
            item.ch = char_of(e, "ch");
            item.gch = char_of(e, "gch");
            item.attr = int_of(e, "attr", 0);
            item.clr = int_of(e, "clr", 8);
            const auto sel = e.find("sel");
            item.preselected = (sel != e.end()) && sel->is_boolean() && sel->get<bool>();
            item.glyph = int_of(e, "glyph", -1);
            item.tile = int_of(e, "tile", -1);
            item.text = str_of(e, "text");
            if (!item.gch.empty() && item.selectable()) {
                this->group_chars_ += item.gch;
            }
            this->items_.push_back(std::move(item));
        }
    }
    this->counts_.assign(this->items_.size(), 0);
    for (std::size_t i = 0; i < this->items_.size(); ++i) {
        if (this->items_[i].selectable() && this->items_[i].preselected && (this->how_ != 0)) {
            this->counts_[i] = -1;
        }
    }
    this->cursor_ = std::max(0, this->next_selectable(-1, 1));
    if (this->cursor_ >= this->size()) {
        this->cursor_ = 0;
    }
    return true;
}

int NhMenu::count_of(int pos) const
{
    return ((pos >= 0) && (pos < static_cast<int>(this->counts_.size()))) ? this->counts_[static_cast<std::size_t>(pos)] : 0;
}

int NhMenu::selected_total() const
{
    int n = 0;
    for (const int c : this->counts_) {
        if (c != 0) {
            ++n;
        }
    }
    return n;
}

void NhMenu::set_page_rows(int rows)
{
    this->page_rows_ = std::max(1, rows);
    this->clamp_top();
}

bool NhMenu::bottom_visible() const
{
    return this->top_ + this->page_rows_ >= this->size();
}

void NhMenu::clamp_top()
{
    this->top_ = std::clamp(this->top_, 0, std::max(0, this->size() - this->page_rows_));
}

void NhMenu::ensure_visible(int pos)
{
    if (pos < this->top_) {
        this->top_ = pos;
    } else if (pos >= this->top_ + this->page_rows_) {
        this->top_ = pos - this->page_rows_ + 1;
    }
    this->clamp_top();
}

int NhMenu::next_selectable(int from, int step) const
{
    for (int i = from + step; (i >= 0) && (i < this->size()); i += step) {
        if (this->items_[static_cast<std::size_t>(i)].selectable()) {
            return i;
        }
    }
    return -1;
}

void NhMenu::set_count(int pos, int count)
{
    if ((pos < 0) || (pos >= this->size()) || !this->items_[static_cast<std::size_t>(pos)].selectable()) {
        return;
    }
    if ((this->how_ == 1) && (count != 0)) {
        std::fill(this->counts_.begin(), this->counts_.end(), 0); // 1 つだけ選ぶ（`SelectMenuItem`）
    }
    this->counts_[static_cast<std::size_t>(pos)] = count;
    this->counting_ = false;
}

void NhMenu::move_cursor(int pos)
{
    if ((pos < 0) || (pos >= this->size())) {
        return;
    }
    this->cursor_ = pos;
    this->ensure_visible(pos);
    if ((this->how_ == 1) && this->items_[static_cast<std::size_t>(pos)].selectable()) {
        this->set_count(pos, -1); // 一つ選ぶ窓は、フォーカスに選択が従う
    }
}

void NhMenu::select_all_where(int from, int to, int mode)
{
    // mode: 1 選ぶ・2 外す・0 反転
    if (this->how_ != 2) {
        return;
    }
    this->counting_ = false;
    to = std::min(to, this->size());
    for (int i = std::max(0, from); i < to; ++i) {
        if (!this->items_[static_cast<std::size_t>(i)].selectable()) {
            continue;
        }
        const bool on = this->counts_[static_cast<std::size_t>(i)] != 0;
        if (mode == 1) {
            this->counts_[static_cast<std::size_t>(i)] = -1;
        } else if (mode == 2) {
            this->counts_[static_cast<std::size_t>(i)] = 0;
        } else {
            this->counts_[static_cast<std::size_t>(i)] = on ? 0 : -1;
        }
    }
}

NhMenuResult NhMenu::select_and_submit(int pos)
{
    this->set_count(pos, -1);
    this->cursor_ = pos;
    this->ensure_visible(pos);
    return NhMenuResult::Submit;
}

NhMenuResult NhMenu::page_down()
{
    const int page = this->page_rows_;
    this->top_ = std::min(this->top_ + page, std::max(0, this->size() - page));
    if ((this->how_ != 0) && !this->items_.empty()) {
        int target = std::min(this->cursor_ + page, this->size() - 1);
        if (!this->items_[static_cast<std::size_t>(target)].selectable()) {
            const int below = this->next_selectable(target, 1);
            const int above = this->next_selectable(target, -1);
            target = (below >= 0) ? below : above;
        }
        if (target >= 0) {
            this->cursor_ = target;
            if (this->how_ == 1) {
                this->set_count(target, -1);
            }
        }
    }
    return NhMenuResult::None;
}

void NhMenu::on_search_text(const std::string &utf8)
{
    if (this->searching_ && (this->search_text_.size() < 120)) {
        for (const char c : utf8) {
            if (static_cast<unsigned char>(c) >= 0x20) {
                this->search_text_.push_back(c);
            }
        }
    }
}

void NhMenu::apply_search()
{
    this->searching_ = false;
    if (this->search_text_.empty()) {
        return;
    }
    for (int i = 0; i < this->size(); ++i) {
        const NhMenuItem &item = this->items_[static_cast<std::size_t>(i)];
        if (!item.selectable() || (item.text.find(this->search_text_) == std::string::npos)) {
            continue;
        }
        if (this->how_ == 2) {
            this->counts_[static_cast<std::size_t>(i)] = (this->counts_[static_cast<std::size_t>(i)] != 0) ? 0 : -1;
        } else if (this->how_ == 1) {
            this->move_cursor(i);
            break;
        }
    }
}

NhMenuResult NhMenu::on_char(char ch)
{
    if (this->searching_) {
        this->on_search_text(std::string(1, ch));
        return NhMenuResult::None;
    }
    bool is_accelerator = false;
    for (const NhMenuItem &item : this->items_) {
        if (item.selectable() && !item.ch.empty() && (item.ch[0] == ch)) {
            is_accelerator = true;
            break;
        }
    }
    if (!is_accelerator) {
        switch (ch) {
        case '^': {
            this->counting_ = false;
            this->top_ = 0;
            const int first = this->next_selectable(-1, 1);
            if (first >= 0) {
                this->move_cursor(first);
            }
            return NhMenuResult::None;
        }
        case '|': {
            this->counting_ = false;
            const int last = this->next_selectable(this->size(), -1);
            if (last >= 0) {
                this->move_cursor(last);
            }
            this->top_ = std::max(0, this->size() - this->page_rows_);
            return NhMenuResult::None;
        }
        case '>':
            this->counting_ = false;
            return this->page_down();
        case '<': {
            this->counting_ = false;
            this->top_ = std::max(0, this->top_ - this->page_rows_);
            if ((this->how_ != 0) && !this->items_.empty()) {
                int target = std::max(0, this->cursor_ - this->page_rows_);
                if (!this->items_[static_cast<std::size_t>(target)].selectable()) {
                    const int below = this->next_selectable(target, 1);
                    const int above = this->next_selectable(target, -1);
                    target = (below >= 0) ? below : above;
                }
                if (target >= 0) {
                    this->cursor_ = target;
                    if (this->how_ == 1) {
                        this->set_count(target, -1);
                    }
                }
            }
            return NhMenuResult::None;
        }
        case '.':
            this->select_all_where(0, this->size(), 1);
            return NhMenuResult::None;
        case '-':
            this->select_all_where(0, this->size(), 2);
            return NhMenuResult::None;
        case '@':
            this->select_all_where(0, this->size(), 0);
            return NhMenuResult::None;
        case ',':
            this->select_all_where(this->top_, this->top_ + this->page_rows_, 1);
            return NhMenuResult::None;
        case '\\':
            this->select_all_where(this->top_, this->top_ + this->page_rows_, 2);
            return NhMenuResult::None;
        case '~':
            this->select_all_where(this->top_, this->top_ + this->page_rows_, 0);
            return NhMenuResult::None;
        case ':':
            if (this->how_ != 0) {
                this->counting_ = false;
                this->searching_ = true;
                this->search_text_.clear();
            }
            return NhMenuResult::None;
        default:
            break;
        }
    }
    // グループの字
    if (!this->group_chars_.empty() && (this->group_chars_.find(ch) != std::string::npos) && !((ch == '0') && this->counting_)) {
        if (this->how_ == 0) {
            return NhMenuResult::None;
        }
        this->counting_ = false;
        for (int i = 0; i < this->size(); ++i) {
            const NhMenuItem &item = this->items_[static_cast<std::size_t>(i)];
            if (!item.selectable() || item.gch.empty() || (item.gch[0] != ch)) {
                continue;
            }
            if (this->how_ == 2) {
                this->counts_[static_cast<std::size_t>(i)] = (this->counts_[static_cast<std::size_t>(i)] != 0) ? 0 : -1;
            } else {
                return this->select_and_submit(i);
            }
        }
        return NhMenuResult::None;
    }
    // 個数
    if (is_digit(ch)) {
        if ((this->how_ != 0) && (this->cursor_ >= 0) && (this->cursor_ < this->size())
            && this->items_[static_cast<std::size_t>(this->cursor_)].selectable()) {
            int count = this->counts_[static_cast<std::size_t>(this->cursor_)];
            if (count < 0) {
                count = 0;
            }
            count = (count * 10) + (ch - '0');
            if (count != 0) { // 先頭の 0 は無視
                this->counting_ = true;
                this->counts_[static_cast<std::size_t>(this->cursor_)] = std::min(100000, count);
            }
        }
        return NhMenuResult::None;
    }
    if (is_letter(ch) || is_accelerator) {
        if ((this->how_ == 1) || (this->how_ == 2)) {
            const int n = this->size();
            for (int step = 0; step < n; ++step) {
                const int i = (this->top_ + step) % n; // 見えている先頭の行から順に探す
                const NhMenuItem &item = this->items_[static_cast<std::size_t>(i)];
                if (!item.selectable() || item.ch.empty() || (item.ch[0] != ch)) {
                    continue;
                }
                if (this->how_ == 2) {
                    this->counts_[static_cast<std::size_t>(i)] = (this->counts_[static_cast<std::size_t>(i)] != 0) ? 0 : -1;
                    this->counting_ = false;
                    this->cursor_ = i;
                    this->ensure_visible(i);
                    return NhMenuResult::None;
                }
                return this->select_and_submit(i);
            }
        }
    }
    this->counting_ = false;
    return NhMenuResult::None;
}

NhMenuResult NhMenu::on_key(NhMenuKey key)
{
    if (this->searching_) {
        switch (key) {
        case NhMenuKey::Enter:
            this->apply_search();
            break;
        case NhMenuKey::Escape:
            this->searching_ = false;
            break;
        case NhMenuKey::Backspace:
            while (!this->search_text_.empty()) {
                const bool lead = (static_cast<unsigned char>(this->search_text_.back()) & 0xC0) != 0x80;
                this->search_text_.pop_back();
                if (lead) {
                    break;
                }
            }
            break;
        case NhMenuKey::Space:
            this->on_search_text(" ");
            break;
        default:
            break;
        }
        return NhMenuResult::None;
    }
    switch (key) {
    case NhMenuKey::Escape:
        if (this->counting_ && (this->how_ != 0)) {
            if ((this->cursor_ >= 0) && (this->cursor_ < this->size())) {
                this->counts_[static_cast<std::size_t>(this->cursor_)] = 0; // 個数の入力を取り消す
            }
            this->counting_ = false;
            return NhMenuResult::None;
        }
        return NhMenuResult::Cancel;
    case NhMenuKey::Enter:
        if ((this->how_ == 1) && (this->selected_total() == 0) && (this->cursor_ >= 0) && (this->cursor_ < this->size())
            && this->items_[static_cast<std::size_t>(this->cursor_)].selectable()) {
            this->set_count(this->cursor_, -1);
        }
        this->counting_ = false;
        return NhMenuResult::Submit;
    case NhMenuKey::Space:
        if (this->bottom_visible()) {
            return NhMenuResult::Submit; // 最後のページで決定（NetHack モード）
        }
        this->counting_ = false;
        return this->page_down();
    case NhMenuKey::Up:
    case NhMenuKey::Down: {
        const int step = (key == NhMenuKey::Down) ? 1 : -1;
        this->counting_ = false;
        if (this->how_ == 0) {
            this->top_ += step;
            this->clamp_top();
            return NhMenuResult::None;
        }
        const int next = this->next_selectable(this->cursor_, step);
        if (next >= 0) {
            this->move_cursor(next);
        } else {
            this->top_ += step; // 端: 見出しの行まで画面だけ動かす
            this->clamp_top();
        }
        return NhMenuResult::None;
    }
    case NhMenuKey::PageDown:
        this->counting_ = false;
        return this->page_down();
    case NhMenuKey::PageUp:
        this->counting_ = false;
        return this->on_char('<');
    case NhMenuKey::Home:
        this->counting_ = false;
        if (this->how_ == 0) {
            this->top_ = 0;
            return NhMenuResult::None;
        }
        return this->on_char('^');
    case NhMenuKey::End:
        this->counting_ = false;
        if (this->how_ == 0) {
            this->top_ = std::max(0, this->size() - this->page_rows_);
            return NhMenuResult::None;
        }
        return this->on_char('|');
    case NhMenuKey::Backspace:
        break;
    }
    return NhMenuResult::None;
}

NhMenuResult NhMenu::click_row(int pos, bool double_click)
{
    if ((pos < 0) || (pos >= this->size()) || !this->items_[static_cast<std::size_t>(pos)].selectable() || (this->how_ == 0)) {
        return NhMenuResult::None;
    }
    this->counting_ = false;
    this->cursor_ = pos;
    if (this->how_ == 2) {
        this->counts_[static_cast<std::size_t>(pos)] = (this->counts_[static_cast<std::size_t>(pos)] != 0) ? 0 : -1;
        return NhMenuResult::None;
    }
    this->set_count(pos, -1);
    return double_click ? NhMenuResult::Submit : NhMenuResult::None;
}

void NhMenu::scroll(int rows)
{
    this->top_ += rows;
    this->clamp_top();
}

std::string NhMenu::submit_json() const
{
    json out = json::object();
    out["t"] = "nethack_reply";
    out["r"] = this->request_;
    if (this->how_ == 0) {
        out["a"] = "dismiss";
        return out.dump();
    }
    out["a"] = "menu";
    json sel = json::array();
    for (std::size_t i = 0; i < this->items_.size(); ++i) {
        const NhMenuItem &item = this->items_[i];
        if (!item.selectable() || (this->counts_[i] == 0)) {
            continue;
        }
        json e = json::object();
        e["i"] = item.index;
        e["n"] = this->counts_[i];
        sel.push_back(std::move(e));
        if (this->how_ == 1) {
            break;
        }
    }
    out["sel"] = std::move(sel);
    return out.dump();
}

std::string NhMenu::cancel_json() const
{
    json out = json::object();
    out["t"] = "nethack_reply";
    out["r"] = this->request_;
    out["a"] = (this->how_ == 0) ? "dismiss" : "cancel";
    return out.dump();
}

} // namespace hd2d::nethack
