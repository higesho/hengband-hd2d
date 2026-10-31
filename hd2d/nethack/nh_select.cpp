/*!
 * @file nh_select.cpp
 * @brief `nh_select.h` の実装。
 */
#include "nethack/nh_select.h"

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

std::vector<NhSelOption> options_of(const json &m, const char *key)
{
    std::vector<NhSelOption> out;
    const auto list = m.find(key);
    if ((list == m.end()) || !list->is_array()) {
        return out;
    }
    for (const json &e : *list) {
        if (!e.is_object()) {
            continue;
        }
        NhSelOption o;
        o.index = int_of(e, "i", static_cast<int>(out.size()));
        o.name = str_of(e, "name");
        o.name_f = str_of(e, "name_f");
        o.adj = str_of(e, "adj");
        out.push_back(std::move(o));
    }
    return out;
}

bool contains_index(const std::vector<NhSelOption> &list, int index)
{
    return std::any_of(list.begin(), list.end(), [index](const NhSelOption &o) { return o.index == index; });
}

} // namespace

bool NhSelect::load(const std::string &payload)
{
    const json m = json::parse(payload, nullptr, false);
    if (m.is_discarded() || !m.is_object() || (str_of(m, "k") != "select")) {
        return false;
    }
    *this = NhSelect{};
    this->request_ = int_of(m, "r", 0);
    this->roles_ = options_of(m, "roles");
    this->races_ = options_of(m, "races");
    this->genders_ = options_of(m, "genders");
    this->aligns_ = options_of(m, "aligns");
    this->name_ = str_of(m, "name");
    const auto ok = m.find("ok");
    if ((ok != m.end()) && ok->is_array()) {
        for (const json &e : *ok) {
            if (!e.is_array() || (e.size() < 4)) {
                continue;
            }
            std::array<int, 4> c{ -1, -1, -1, -1 };
            bool good = true;
            for (std::size_t k = 0; k < 4; ++k) {
                if (!e[k].is_number_integer()) {
                    good = false;
                    break;
                }
                c[k] = static_cast<int>(e[k].get<long long>());
            }
            if (good) {
                this->combos_.push_back(c);
            }
        }
    }
    std::array<int, 4> pick{ -1, -1, -1, -1 };
    const auto p = m.find("pick");
    if ((p != m.end()) && p->is_object()) {
        pick[0] = int_of(*p, "role", -1);
        pick[1] = int_of(*p, "race", -1);
        pick[2] = int_of(*p, "gender", -1);
        pick[3] = int_of(*p, "align", -1);
    }
    this->pick_complete_ = (pick[0] >= 0) && (pick[1] >= 0) && (pick[2] >= 0) && (pick[3] >= 0)
        && contains_index(this->roles_, pick[0]) && contains_index(this->races_, pick[1]) && contains_index(this->genders_, pick[2])
        && contains_index(this->aligns_, pick[3]) && this->has_combo(pick[0], pick[1], pick[2], pick[3]);
    // 先に決まっている選びと矛盾しない先頭の組から埋める。
    bool found = false;
    for (const std::array<int, 4> &c : this->combos_) {
        bool match = true;
        for (std::size_t k = 0; k < 4; ++k) {
            if ((pick[k] >= 0) && (pick[k] != c[k])) {
                match = false;
            }
        }
        if (match) {
            this->sel_ = c;
            found = true;
            break;
        }
    }
    if (!found) {
        if (!this->combos_.empty()) {
            this->sel_ = this->combos_.front();
        } else {
            const std::vector<NhSelOption> *lists[4] = { &this->roles_, &this->races_, &this->genders_, &this->aligns_ };
            for (std::size_t k = 0; k < 4; ++k) {
                if ((pick[k] >= 0) && contains_index(*lists[k], pick[k])) {
                    this->sel_[k] = pick[k];
                } else if (!lists[k]->empty()) {
                    this->sel_[k] = lists[k]->front().index;
                }
            }
        }
    }
    return true;
}

bool NhSelect::has_combo(int role, int race, int gender, int align) const
{
    if (this->combos_.empty()) {
        return true; // 表が無いときは、すべて有効
    }
    for (const std::array<int, 4> &c : this->combos_) {
        if (((role < 0) || (c[0] == role)) && ((race < 0) || (c[1] == race)) && ((gender < 0) || (c[2] == gender))
            && ((align < 0) || (c[3] == align))) {
            return true;
        }
    }
    return false;
}

bool NhSelect::race_ok(int race) const
{
    return this->has_combo(this->sel_[0], race, -1, -1);
}

bool NhSelect::gender_ok(int gender) const
{
    return this->has_combo(this->sel_[0], this->sel_[1], gender, -1);
}

bool NhSelect::align_ok(int align) const
{
    return this->has_combo(this->sel_[0], this->sel_[1], -1, align);
}

int NhSelect::first_race_for_role(int role) const
{
    for (const NhSelOption &r : this->races_) {
        if (this->has_combo(role, r.index, -1, -1)) {
            return r.index;
        }
    }
    return -1;
}

void NhSelect::adjust_align()
{
    if (this->has_combo(this->sel_[0], this->sel_[1], this->sel_[2], this->sel_[3])) {
        return;
    }
    for (const NhSelOption &a : this->aligns_) {
        if (this->has_combo(this->sel_[0], this->sel_[1], this->sel_[2], a.index)) {
            this->sel_[3] = a.index;
            return;
        }
    }
}

void NhSelect::adjust_after_race()
{
    if (!this->has_combo(this->sel_[0], this->sel_[1], this->sel_[2], -1)) {
        for (const NhSelOption &g : this->genders_) {
            if (this->has_combo(this->sel_[0], this->sel_[1], g.index, -1)) {
                this->sel_[2] = g.index;
                break;
            }
        }
    }
    this->adjust_align();
}

void NhSelect::adjust_after_role()
{
    if (!this->has_combo(this->sel_[0], this->sel_[1], -1, -1)) {
        const int race = this->first_race_for_role(this->sel_[0]);
        if (race >= 0) {
            this->sel_[1] = race;
        }
    }
    this->adjust_after_race();
}

bool NhSelect::choose_role(int role)
{
    if (!contains_index(this->roles_, role) || !this->has_combo(role, -1, -1, -1)) {
        return false;
    }
    this->sel_[0] = role;
    this->adjust_after_role();
    return true;
}

bool NhSelect::choose_race(int race)
{
    if (!contains_index(this->races_, race) || !this->race_ok(race)) {
        return false;
    }
    this->sel_[1] = race;
    this->adjust_after_race();
    return true;
}

bool NhSelect::choose_gender(int gender)
{
    if (!contains_index(this->genders_, gender) || !this->gender_ok(gender)) {
        return false;
    }
    this->sel_[2] = gender;
    this->adjust_align();
    return true;
}

bool NhSelect::choose_align(int align)
{
    if (!contains_index(this->aligns_, align) || !this->align_ok(align)) {
        return false;
    }
    this->sel_[3] = align;
    return true;
}

std::string NhSelect::role_label(const NhSelOption &role) const
{
    // 原作の性別の番号: 0 男・1 女。
    if ((this->sel_[2] == 1) && !role.name_f.empty()) {
        return role.name_f;
    }
    return role.name;
}

void NhSelect::move_focus(int step)
{
    static const NhSelFocus order[] = { NhSelFocus::Roles, NhSelFocus::Races, NhSelFocus::Align, NhSelFocus::Gender,
        NhSelFocus::Play, NhSelFocus::Random, NhSelFocus::Quit };
    constexpr int n = static_cast<int>(sizeof(order) / sizeof(order[0]));
    int at = 0;
    for (int i = 0; i < n; ++i) {
        if (order[i] == this->focus_) {
            at = i;
        }
    }
    this->focus_ = order[(((at + step) % n) + n) % n];
}

void NhSelect::move_choice(int step)
{
    const auto walk = [step](const std::vector<NhSelOption> &list, int current, auto ok) -> int {
        int pos = -1;
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (list[i].index == current) {
                pos = static_cast<int>(i);
            }
        }
        for (int i = pos + step; (i >= 0) && (i < static_cast<int>(list.size())); i += step) {
            if (ok(list[static_cast<std::size_t>(i)].index)) {
                return list[static_cast<std::size_t>(i)].index;
            }
        }
        return -1;
    };
    switch (this->focus_) {
    case NhSelFocus::Roles: {
        const int next = walk(this->roles_, this->sel_[0], [this](int r) { return this->has_combo(r, -1, -1, -1); });
        if (next >= 0) {
            (void)this->choose_role(next);
        }
        break;
    }
    case NhSelFocus::Races: {
        const int next = walk(this->races_, this->sel_[1], [this](int r) { return this->race_ok(r); });
        if (next >= 0) {
            (void)this->choose_race(next);
        }
        break;
    }
    case NhSelFocus::Align: {
        const int next = walk(this->aligns_, this->sel_[3], [this](int a) { return this->align_ok(a); });
        if (next >= 0) {
            (void)this->choose_align(next);
        }
        break;
    }
    case NhSelFocus::Gender: {
        const int next = walk(this->genders_, this->sel_[2], [this](int g) { return this->gender_ok(g); });
        if (next >= 0) {
            (void)this->choose_gender(next);
        }
        break;
    }
    default:
        break;
    }
}

std::string NhSelect::play_json() const
{
    json out = json::object();
    out["t"] = "nethack_reply";
    out["r"] = this->request_;
    out["a"] = "select";
    out["role"] = this->sel_[0];
    out["race"] = this->sel_[1];
    out["gender"] = this->sel_[2];
    out["align"] = this->sel_[3];
    out["name"] = this->name_;
    return out.dump();
}

std::string NhSelect::random_json() const
{
    json out = json::object();
    out["t"] = "nethack_reply";
    out["r"] = this->request_;
    out["a"] = "random";
    return out.dump();
}

std::string NhSelect::quit_json() const
{
    json out = json::object();
    out["t"] = "nethack_reply";
    out["r"] = this->request_;
    out["a"] = "quit";
    return out.dump();
}

} // namespace hd2d::nethack
