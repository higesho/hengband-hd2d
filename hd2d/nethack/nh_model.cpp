/*!
 * @file nh_model.cpp
 * @brief `nh_model.h` の実装。
 */
#include "nethack/nh_model.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace hd2d::nethack {

namespace {

using nlohmann::json;

//! 型が合わないときは既定値を返す（`json::value` は型違いで例外を投げるので使わない）。
std::string get_str(const json &o, const char *key, const std::string &fallback = std::string())
{
    const auto it = o.find(key);
    return ((it != o.end()) && it->is_string()) ? it->get<std::string>() : fallback;
}

int get_int(const json &o, const char *key, int fallback = 0)
{
    const auto it = o.find(key);
    if ((it == o.end()) || !it->is_number()) {
        return fallback;
    }
    if (it->is_number_integer()) {
        return static_cast<int>(it->get<long long>());
    }
    return static_cast<int>(it->get<double>());
}

bool get_bool(const json &o, const char *key, bool fallback = false)
{
    const auto it = o.find(key);
    if (it == o.end()) {
        return fallback;
    }
    if (it->is_boolean()) {
        return it->get<bool>();
    }
    if (it->is_number()) {
        return it->get<double>() != 0.0;
    }
    return fallback;
}

//! 色の番号を 0〜15 に直す（範囲外・負は「色の指定なし」）。
int clamp_color(int c)
{
    return ((c >= 0) && (c < 16)) ? c : kNoColor;
}

//! 原作の既定の書式（`botl.c` の `initblstats`）。値だけが届いたときの補い。
const char *default_fmt(int field)
{
    switch (field) {
    case kBlStr: return "St:%s";
    case kBlDx: return "Dx:%s";
    case kBlCo: return "Co:%s";
    case kBlIn: return "In:%s";
    case kBlWi: return "Wi:%s";
    case kBlCh: return "Ch:%s";
    case kBlScore: return "S:%s";
    case kBlGold: return "$:%s";
    case kBlEne: return "Pw:%s";
    case kBlEneMax: return "(%s)";
    case kBlXp: return "Xp:%s";
    case kBlAc: return "AC:%s";
    case kBlHd: return "HD:%s";
    case kBlTime: return "T:%s";
    case kBlHp: return "HP:%s";
    case kBlHpMax: return "(%s)";
    case kBlExp: return "/%s";
    default: return nullptr;
    }
}

std::string apply_fmt(const std::string &fmt, const std::string &text)
{
    const std::size_t at = fmt.find("%s");
    if (at == std::string::npos) {
        return fmt;
    }
    return fmt.substr(0, at) + text + fmt.substr(at + 2);
}

std::string trim_left(std::string s)
{
    s.erase(0, s.find_first_not_of(' '));
    return s;
}

} // namespace

std::string nh_status_display_text(int field, const NhStatusField &value)
{
    if ((field == kBlGold) && (value.text.find(':') != std::string::npos)) {
        // 所持金は、金貨の glyph の符号（`\GXXXXNNNN`）に `:` と金額が続く。原作の GUI 版（`mswproc.c` の `BL_GOLD`）と同じに、
        // 金貨の字（`$`）と `:` から後ろを並べる。
        return "$" + value.text.substr(value.text.find(':'));
    }
    if (!value.fmt.empty()) {
        return trim_left(apply_fmt(value.fmt, value.text));
    }
    const char *fmt = default_fmt(field);
    if ((fmt != nullptr) && !value.text.empty()) {
        const char first = value.text.front();
        const bool looks_formatted = (value.text.find(':') != std::string::npos) || (first == '(') || (first == '/');
        if (!looks_formatted) {
            return apply_fmt(fmt, value.text);
        }
    }
    return value.text;
}

NhWindowInfo *NhModel::find_window(int id)
{
    const auto it = this->windows_.find(id);
    return (it == this->windows_.end()) ? nullptr : &it->second;
}

const NhWindowInfo *NhModel::window(int id) const
{
    const auto it = this->windows_.find(id);
    return (it == this->windows_.end()) ? nullptr : &it->second;
}

int NhModel::window_of_type(const std::string &type) const
{
    for (const auto &[id, info] : this->windows_) {
        if (info.type == type) {
            return id;
        }
    }
    return -1;
}

const NhStatusField &NhModel::status(int field) const
{
    static const NhStatusField kEmpty{};
    return ((field >= 0) && (field < kBlMax)) ? this->fields_[field] : kEmpty;
}

bool NhModel::pref_bool(const std::string &name, bool fallback) const
{
    const auto it = this->prefs_.find(name);
    if (it == this->prefs_.end()) {
        return fallback;
    }
    const std::string &v = it->second;
    return (v == "1") || (v == "true") || (v == "on") || (v == "yes");
}

std::string NhModel::pref_string(const std::string &name) const
{
    const auto it = this->prefs_.find(name);
    return (it == this->prefs_.end()) ? std::string() : it->second;
}

bool NhModel::ingest(const std::string &payload)
{
    const json m = json::parse(payload, nullptr, false);
    if (m.is_discarded() || !m.is_object() || (get_str(m, "t") != "nethack_ui")) {
        return false;
    }
    const std::string k = get_str(m, "k");
    if (k.empty()) {
        return false;
    }
    bool handled = true;
    if (k == "wait") {
        this->wait_.reason = get_str(m, "reason");
        this->wait_.r = get_int(m, "r");
        ++this->wait_serial_;
        this->pos_active_ = (this->wait_.reason == "pos");
    } else if (k == "hover") {
        // 待ちの途中でコアが答える物なので、待ちは終わらせない
        this->hover_x_ = get_int(m, "x", -1);
        this->hover_y_ = get_int(m, "y", -1);
        this->hover_text_ = get_str(m, "text");
        ++this->hover_serial_;
    } else if (k == "win") {
        this->wait_ = NhWait{};
        const std::string op = get_str(m, "op");
        const int id = get_int(m, "id", -1);
        if (op == "create") {
            NhWindowInfo info;
            info.id = id;
            info.type = get_str(m, "type");
            info.blocking = get_bool(m, "blocking");
            if (info.type == "invent") {
                this->invent_window_ = id;
            }
            this->windows_[id] = std::move(info);
        } else if (NhWindowInfo *w = this->find_window(id); w != nullptr) {
            if (op == "clear") {
                w->lines.clear();
                if (w->type == "message") {
                    this->msgwin_.clear();
                }
            } else if (op == "display") {
                w->displayed = true;
                w->blocking = get_bool(m, "blocking", w->blocking);
            } else if (op == "destroy") {
                if (id == this->invent_window_) {
                    this->invent_window_ = -1;
                    this->invent_payload_.clear();
                    ++this->invent_version_;
                }
                this->windows_.erase(id);
            } else {
                handled = false;
            }
        } else if ((op != "clear") && (op != "display") && (op != "destroy")) {
            handled = false;
        }
    } else if (k == "putstr") {
        this->wait_ = NhWait{};
        const int id = get_int(m, "id", -1);
        const int attr = get_int(m, "attr");
        const std::string text = get_str(m, "text");
        const bool nohistory = get_bool(m, "nohistory");
        NhWindowInfo *w = this->find_window(id);
        if ((id < 0) || ((w != nullptr) && (w->type == "message"))) {
            this->msgwin_.put(attr, text, nohistory); // `raw_print`（id = -1）もメッセージの窓へ
        } else if (w != nullptr) {
            if (w->lines.size() < 20000) {
                w->lines.push_back(NhMsgLine{ text, attr & ~(kAtrNoHistory | kAtrUrgent) });
            }
        }
    } else if (k == "status") {
        this->wait_ = NhWait{};
        if (get_bool(m, "reset")) {
            for (NhStatusField &f : this->fields_) {
                f = NhStatusField{};
            }
            this->conditions_.clear();
        }
        const auto fields = m.find("fields");
        if ((fields != m.end()) && fields->is_array()) {
            for (const json &e : *fields) {
                if (!e.is_object()) {
                    continue;
                }
                const int f = get_int(e, "f", -1);
                if ((f < 0) || (f >= kBlMax)) {
                    continue;
                }
                NhStatusField &dst = this->fields_[f];
                dst.present = true;
                dst.text = get_str(e, "text");
                dst.fmt = get_str(e, "fmt");
                dst.color = clamp_color(get_int(e, "color", kNoColor));
                dst.attr = get_int(e, "attr");
                dst.pct = get_int(e, "pct", -1);
            }
        }
        const auto cond = m.find("cond");
        if ((cond != m.end()) && cond->is_array()) {
            this->conditions_.clear();
            for (const json &e : *cond) {
                if (!e.is_object()) {
                    continue;
                }
                NhStatusCond c;
                c.name = get_str(e, "name");
                c.color = clamp_color(get_int(e, "color", kNoColor));
                c.attr = get_int(e, "attr");
                if (!c.name.empty()) {
                    this->conditions_.push_back(std::move(c));
                }
            }
        }
        this->status_seen_ = true;
        ++this->status_version_;
    } else if (k == "curs") {
        this->wait_ = NhWait{};
        // 地図のカーソルだけを覚える（ほかの窓の `curs` は地図の印にならない）。地図の窓がまだ無い間は区別できないので覚える。
        const NhWindowInfo *target = this->window(get_int(m, "id", -1));
        if ((target == nullptr) || (target->type == "map")) {
            this->cursor_x_ = get_int(m, "x");
            this->cursor_y_ = get_int(m, "y");
            this->cursor_known_ = true;
        }
    } else if (k == "cliparound") {
        this->wait_ = NhWait{};
        this->clip_x_ = get_int(m, "x");
        this->clip_y_ = get_int(m, "y");
        this->clip_known_ = true;
    } else if ((k == "menu") || (k == "text") || (k == "ask") || (k == "select") || (k == "rip")) {
        this->wait_ = NhWait{};
        if (!((k == "menu") && (get_int(m, "r") == 0))) {
            this->pos_active_ = false; // 窓・問いが開く（持ち物の常設の窓の更新は別）
        }
        this->last_request_kind_ = (k == "ask") ? ("ask:" + get_str(m, "kind")) : k;
        this->last_request_r_ = get_int(m, "r");
        if ((k == "menu") && (this->last_request_r_ == 0)) {
            // 持ち物の常設の窓の中身（返事は要らない）。
            this->invent_payload_ = payload;
            ++this->invent_version_;
        } else {
            this->request_.kind = k;
            this->request_.r = this->last_request_r_;
            this->request_.payload = payload;
            this->request_.serial = ++this->request_serial_;
            this->request_open_ = true;
        }
    } else if (k == "pref") {
        this->wait_ = NhWait{};
        const std::string name = get_str(m, "name");
        const auto v = m.find("value");
        if (!name.empty() && (v != m.end())) {
            this->prefs_[name] = v->is_string() ? v->get<std::string>() : (v->is_boolean() ? (v->get<bool>() ? "1" : "0") : v->dump());
        }
    } else if (k == "cmdbar") {
        // コマンドの欄。待ちは変えない（ゲームの始めに 1 回届く）
        this->cmdbar_.clear();
        const auto items = m.find("items");
        if ((items != m.end()) && items->is_array()) {
            for (const json &it : *items) {
                if (!it.is_object()) {
                    continue;
                }
                NhCmdItem c;
                c.id = get_int(it, "id");
                const auto keys = it.find("keys");
                if ((keys != it.end()) && keys->is_array()) {
                    for (const json &key : *keys) {
                        if (key.is_number_integer()) {
                            c.keys.push_back(key.get<int>());
                        }
                    }
                }
                c.ext = get_str(it, "ext");
                c.glyph = get_int(it, "glyph", -1);
                c.text = get_str(it, "text");
                c.en = get_str(it, "en");
                c.ja = get_str(it, "ja");
                if (!c.keys.empty() || !c.ext.empty()) {
                    this->cmdbar_.push_back(std::move(c));
                }
            }
        }
    } else if (k == "bell") {
        this->wait_ = NhWait{};
    } else if (k == "exit") {
        this->wait_ = NhWait{};
        this->exited_ = true;
        this->pos_active_ = false;
        this->exit_text_ = get_str(m, "text");
        this->request_open_ = false;
    } else {
        handled = false; // 知らない種類は捨てる（将来の追補で増えてもこの画面は止まらない）
    }
    if (handled) {
        ++this->version_;
    }
    return handled;
}

} // namespace hd2d::nethack
