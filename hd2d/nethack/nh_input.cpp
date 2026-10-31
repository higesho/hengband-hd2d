/*!
 * @file nh_input.cpp
 * @brief `nh_input.h` の実装。
 */
#include "nethack/nh_input.h"

#include <algorithm>
#include <cstring>

namespace hd2d::nethack {

namespace {

std::size_t utf8_len_of(unsigned char lead)
{
    if (lead >= 0xF0) {
        return 4;
    }
    if (lead >= 0xE0) {
        return 3;
    }
    if (lead >= 0xC0) {
        return 2;
    }
    return 1;
}

char lower_ascii(char c)
{
    return ((c >= 'A') && (c <= 'Z')) ? static_cast<char>(c - 'A' + 'a') : c;
}

} // namespace

int NhTextField::insert(const std::string &utf8)
{
    int added = 0;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        const std::size_t len = std::min(utf8_len_of(c), utf8.size() - i);
        if ((c < 0x20) || (c == 0x7F)) {
            i += len;
            continue;
        }
        if (static_cast<int>(this->text_.size() + len) > this->max_bytes_) {
            break;
        }
        this->text_.append(utf8, i, len);
        added += static_cast<int>(len);
        i += len;
    }
    return added;
}

bool NhTextField::backspace()
{
    if (this->text_.empty()) {
        return false;
    }
    while (!this->text_.empty()) {
        const bool lead = (static_cast<unsigned char>(this->text_.back()) & 0xC0) != 0x80;
        this->text_.pop_back();
        if (lead) {
            break;
        }
    }
    return true;
}

void NhTextField::clear()
{
    this->text_.clear();
    this->preedit_.clear();
}

bool nh_resolve_yn(int key, const std::string &choices, char def, char &result)
{
    if (choices.empty()) {
        result = static_cast<char>(key); // 何でも受ける（原作の tty も同じ）
        return true;
    }
    if (key == 0x1B) {
        if (choices.find('q') != std::string::npos) {
            result = 'q';
        } else if (choices.find('n') != std::string::npos) {
            result = 'n';
        } else {
            result = def;
        }
        return true;
    }
    if ((key == ' ') || (key == '\r') || (key == '\n')) {
        result = def;
        return true;
    }
    if (key <= 0 || key > 0x7E) {
        return false;
    }
    const char c = static_cast<char>(key);
    const char lower = lower_ascii(c);
    const char upper = ((c >= 'a') && (c <= 'z')) ? static_cast<char>(c - 'a' + 'A') : c;
    for (const char candidate : { c, lower, upper }) {
        if (choices.find(candidate) != std::string::npos) {
            result = candidate;
            return true;
        }
    }
    return false;
}

std::string nh_yn_prompt_text(const std::string &query, const std::string &choices, char def)
{
    std::string out = query;
    if (!choices.empty()) {
        out += " [" + choices + "]";
        if (def != 0) {
            out += std::string(" (") + def + ")";
        }
    }
    return out;
}

std::vector<int> nh_ext_matches(const std::vector<std::string> &cmds, const std::string &typed)
{
    std::vector<int> out;
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        const std::string &name = cmds[i];
        if (name.size() < typed.size()) {
            continue;
        }
        bool match = true;
        for (std::size_t k = 0; k < typed.size(); ++k) {
            if (lower_ascii(name[k]) != lower_ascii(typed[k])) {
                match = false;
                break;
            }
        }
        if (match) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

std::string nh_ext_common_prefix(const std::vector<std::string> &cmds, const std::vector<int> &matches)
{
    if (matches.empty()) {
        return {};
    }
    const std::string &base = cmds[static_cast<std::size_t>(matches.front())];
    std::size_t k = base.size();
    for (const int m : matches) {
        const std::string &s = cmds[static_cast<std::size_t>(m)];
        std::size_t i = 0;
        while ((i < k) && (i < s.size()) && (lower_ascii(base[i]) == lower_ascii(s[i]))) {
            ++i;
        }
        k = i;
    }
    // 文字の途中で切れていたら、文字の切れ目まで戻す
    while ((k > 0) && (k < base.size()) && ((static_cast<unsigned char>(base[k]) & 0xC0) == 0x80)) {
        --k;
    }
    return base.substr(0, k);
}

std::size_t nh_utf8_prefix_bytes(const std::string &s, std::size_t chars)
{
    std::size_t i = 0;
    while ((i < s.size()) && (chars > 0)) {
        i = std::min(s.size(), i + utf8_len_of(static_cast<unsigned char>(s[i])));
        --chars;
    }
    return i;
}

} // namespace hd2d::nethack
