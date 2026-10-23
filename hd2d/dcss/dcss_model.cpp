/*!
 * @file dcss_model.cpp
 * @brief `dcss_model.h` の実装。
 */
#include "dcss/dcss_model.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace hd2d::dcss {

namespace {

using json = nlohmann::json;

struct DcssNameEntry {
    const char *english;
    const char *japanese;
};

#include "dcss/dcss_ui_names_ja.inc"

template <std::size_t N>
std::string lookup_name(const DcssNameEntry (&table)[N], std::string_view english)
{
    for (const DcssNameEntry &e : table) {
        if (english == e.english) {
            return e.japanese;
        }
    }
    return std::string();
}

std::int64_t jint(const json &o, const char *key, std::int64_t fallback = 0)
{
    const auto it = o.find(key);
    if ((it == o.end()) || it->is_null()) {
        return fallback;
    }
    if (it->is_boolean()) {
        return it->get<bool>() ? 1 : 0;
    }
    if (it->is_number_integer()) {
        return it->get<std::int64_t>();
    }
    if (it->is_number_unsigned()) {
        return static_cast<std::int64_t>(it->get<std::uint64_t>());
    }
    if (it->is_number_float()) {
        return static_cast<std::int64_t>(it->get<double>());
    }
    return fallback;
}

std::string jstr(const json &o, const char *key)
{
    const auto it = o.find(key);
    if ((it == o.end()) || !it->is_string()) {
        return std::string();
    }
    return it->get<std::string>();
}

//! 不正な UTF-8 を置換文字に直す（nlohmann は不正な字で例外を投げる）。
std::string sanitize(const std::string &in)
{
    const std::vector<char32_t> chars = dcss_decode_utf8(in);
    std::string out;
    out.reserve(in.size());
    for (const char32_t c : chars) {
        out += dcss_encode_utf8(c);
    }
    return out;
}

void apply_rgb(const json &options, const char *key, DcssRgb &out)
{
    const auto it = options.find(key);
    if ((it == options.end()) || !it->is_object()) {
        return;
    }
    out.r = static_cast<std::uint8_t>(std::clamp<std::int64_t>(jint(*it, "r"), 0, 255));
    out.g = static_cast<std::uint8_t>(std::clamp<std::int64_t>(jint(*it, "g"), 0, 255));
    out.b = static_cast<std::uint8_t>(std::clamp<std::int64_t>(jint(*it, "b"), 0, 255));
}

void apply_threshold(const json &options, const char *key, std::vector<std::pair<int, int>> &out)
{
    const auto it = options.find(key);
    if ((it == options.end()) || !it->is_array()) {
        return;
    }
    out.clear();
    for (const json &e : *it) {
        if (!e.is_object()) {
            continue;
        }
        out.emplace_back(static_cast<int>(jint(e, "value")), dcss_colour_from_name(jstr(e, "colour"), kLightGrey));
    }
}

} // namespace

std::string dcss_species_name_ja(std::string_view english)
{
    const std::string ja = lookup_name(kDcssSpeciesNamesJa, english);
    return ja.empty() ? std::string(english) : ja;
}

std::string dcss_god_name_ja(std::string_view english)
{
    std::string ja = lookup_name(kDcssGodNamesJa, english);
    if (!ja.empty()) {
        return ja;
    }
    // ジーヴァは `Jiyva <ランダムな名>` で来る。1 つ目だけ訳し、2 つ目は英字のまま「・」でつなぐ（原作 `_redraw_title`）。
    const std::size_t space = english.find(' ');
    if (space != std::string_view::npos) {
        const std::string head = lookup_name(kDcssGodNamesJa, english.substr(0, space));
        if (!head.empty()) {
            return head + "・" + std::string(english.substr(space + 1));
        }
    }
    return std::string(english);
}

std::string dcss_branch_name_ja(std::string_view english)
{
    std::string_view name = english;
    for (const std::string_view article : { "The ", "the ", "An ", "an ", "A ", "a " }) {
        if (name.substr(0, article.size()) == article) {
            name.remove_prefix(article.size());
            break;
        }
    }
    std::string ja = lookup_name(kDcssBranchNamesJa, name);
    if (ja.empty()) {
        ja = lookup_name(kDcssBranchNamesJa, english);
    }
    return ja.empty() ? std::string(name) : ja;
}

DcssUiModel::DcssUiModel()
{
    // 原作の既定（`options` が届く前に使う。値は `initfile.cc`・`tilereg-map.cc`）。
    auto &c = this->options_.map_colour;
    c[kMfUnseen] = { 0, 0, 0 };
    c[kMfFloor] = { 51, 51, 51 };
    c[kMfWall] = { 102, 102, 102 };
    c[kMfMapFloor] = { 34, 34, 102 };
    c[kMfMapWall] = { 68, 68, 153 };
    c[kMfDoor] = { 119, 85, 68 };
    c[kMfItem] = { 0, 85, 68 };
    c[kMfMonsFriendly] = { 238, 92, 238 };
    c[kMfMonsPeaceful] = { 238, 92, 238 };
    c[kMfMonsNeutral] = { 238, 92, 238 };
    c[kMfMonsHostile] = { 102, 0, 0 };
    c[kMfMonsNoExp] = { 68, 102, 51 };
    c[kMfStairUp] = { 0, 255, 255 };
    c[kMfStairDown] = { 255, 0, 255 };
    c[kMfStairBranch] = { 255, 119, 136 };
    c[kMfFeature] = { 153, 119, 0 };
    c[kMfWater] = { 17, 68, 85 };
    c[kMfLava] = { 85, 34, 17 };
    c[kMfTrap] = { 170, 102, 68 };
    c[kMfExclRoot] = { 85, 34, 102 };
    c[kMfExcl] = { 85, 34, 102 };
    c[kMfPlayer] = { 255, 255, 255 };
    c[kMfDeepWater] = { 0, 17, 34 };
    c[kMfPortal] = { 255, 221, 0 };
    c[kMfTransporter] = { 0, 0, 255 };
    c[kMfTransporterLanding] = { 82, 0, 170 };
    c[kMfExploreHorizon] = { 107, 48, 27 };
}

bool DcssUiModel::ingest_line(const std::string &raw)
{
    if (raw.empty() || (raw[0] == '*')) {
        return false;
    }
    const json m = json::parse(sanitize(raw), nullptr, false);
    if (m.is_discarded() || !m.is_object()) {
        return false;
    }
    const std::string kind = jstr(m, "msg");
    if (kind == "player") {
        this->on_player(&m);
    } else if (kind == "msgs") {
        this->on_msgs(&m);
    } else if (kind == "map") {
        this->on_map(&m);
    } else if (kind == "options") {
        this->on_options(&m);
    } else if (kind == "input_mode") {
        const int mode = static_cast<int>(jint(m, "mode"));
        if ((mode == kInputCommand) && (this->input_mode_ != kInputCommand)) {
            this->time_at_prev_input_ = this->time_at_last_input_;
            this->time_at_last_input_ = this->player_.time;
        }
        this->input_mode_ = mode;
    } else if (kind == "ui_state") {
        this->ui_state_ = static_cast<int>(jint(m, "state"));
    } else if (kind == "text_cursor") {
        this->text_cursor_ = (jint(m, "enabled") != 0);
    } else {
        return false;
    }
    return true;
}

void DcssUiModel::on_player(const void *p)
{
    const json &m = *static_cast<const json *>(p);
    DcssPlayer &pl = this->player_;
    pl.name = m.contains("name") ? jstr(m, "name") : pl.name;
    const auto set_str = [&m](const char *key, std::string &out) {
        if (m.contains(key) && m[key].is_string()) {
            out = m[key].get<std::string>();
        }
    };
    const auto set_int = [&m](const char *key, int &out) {
        if (m.contains(key)) {
            out = static_cast<int>(jint(m, key, out));
        }
    };
    set_str("title", pl.title);
    set_str("species", pl.species);
    set_str("god", pl.god);
    set_str("place", pl.place);
    set_str("quiver_desc", pl.quiver_desc);
    set_str("unarmed_attack", pl.unarmed_attack);
    set_int("depth", pl.depth);
    set_int("wizard", pl.wizard);
    set_int("explore", pl.explore);
    set_int("penance", pl.penance);
    set_int("piety_rank", pl.piety_rank);
    set_int("ostracism_pips", pl.ostracism_pips);
    set_int("form", pl.form);
    set_int("hp", pl.hp);
    set_int("hp_max", pl.hp_max);
    set_int("real_hp_max", pl.real_hp_max);
    set_int("mp", pl.mp);
    set_int("mp_max", pl.mp_max);
    set_int("dd_real_mp_max", pl.dd_real_mp_max);
    set_int("poison_survival", pl.poison_survival);
    set_int("ac", pl.ac);
    set_int("ev", pl.ev);
    set_int("sh", pl.sh);
    set_int("ac_mod", pl.ac_mod);
    set_int("ev_mod", pl.ev_mod);
    set_int("sh_mod", pl.sh_mod);
    set_int("str", pl.str);
    set_int("int", pl.intel);
    set_int("dex", pl.dex);
    set_int("doom", pl.doom);
    set_int("contam", pl.contam);
    set_int("xl", pl.xl);
    set_int("progress", pl.progress);
    set_int("gold", pl.gold);
    set_int("noise", pl.noise);
    set_int("adjusted_noise", pl.adjusted_noise);
    set_int("time", pl.time);
    set_int("turn", pl.turn);
    set_int("weapon_index", pl.weapon_index);
    set_int("offhand_index", pl.offhand_index);
    set_int("unarmed_attack_colour", pl.unarmed_attack_colour);
    if (m.contains("pos") && m["pos"].is_object()) {
        this->player_x_ = static_cast<int>(jint(m["pos"], "x"));
        this->player_y_ = static_cast<int>(jint(m["pos"], "y"));
        this->player_pos_known_ = true;
    }
    if (m.contains("status") && m["status"].is_array()) {
        pl.status.clear();
        for (const json &s : m["status"]) {
            if (!s.is_object()) {
                continue;
            }
            DcssStatusLight light;
            light.light = jstr(s, "light");
            light.text = jstr(s, "text");
            light.colour = static_cast<int>(jint(s, "col", kLightGrey)) & 0x0F;
            pl.status.push_back(std::move(light));
        }
    }
    if (m.contains("inv") && m["inv"].is_object()) {
        for (auto it = m["inv"].begin(); it != m["inv"].end(); ++it) {
            const int idx = std::atoi(it.key().c_str());
            const json &item = it.value();
            if (!item.is_object()) {
                continue;
            }
            DcssInvItem &slot = pl.inv[idx];
            if (item.contains("quantity")) {
                slot.quantity = static_cast<int>(jint(item, "quantity"));
            }
            if (item.contains("letter")) {
                slot.letter = static_cast<int>(jint(item, "letter"));
            }
            if (item.contains("col")) {
                slot.colour = static_cast<int>(jint(item, "col", -1));
            }
            if (item.contains("name") && item["name"].is_string()) {
                slot.name = item["name"].get<std::string>();
            }
        }
    }
    pl.seen = !pl.name.empty();
    ++pl.version;
}

void DcssUiModel::on_msgs(const void *p)
{
    const json &m = *static_cast<const json *>(p);
    for (const char *key : { "rollback", "old_msgs" }) {
        if (m.contains(key)) {
            std::int64_t n = jint(m, key);
            while ((n-- > 0) && !this->messages_.empty()) {
                this->messages_.pop_back();
            }
            ++this->rollback_serial_;
        }
    }
    if (m.contains("messages") && m["messages"].is_array()) {
        for (const json &one : m["messages"]) {
            DcssMessage msg;
            msg.text = jstr(one, "text");
            msg.turn = static_cast<int>(jint(one, "turn"));
            msg.channel = static_cast<int>(jint(one, "channel"));
            this->messages_.push_back(std::move(msg));
            ++this->message_serial_;
            while (this->messages_.size() > kMaxMessages) {
                this->messages_.erase(this->messages_.begin());
            }
        }
    }
    if (m.contains("more")) {
        this->more_ = (jint(m, "more") != 0);
    }
    if (m.contains("more_text")) {
        this->more_text_ = jstr(m, "more_text");
    }
}

void DcssUiModel::on_map(const void *p)
{
    const json &m = *static_cast<const json *>(p);
    if (m.value("clear", false)) {
        this->map_.clear();
        // 自分の位置は消さない: 新しい階の原点は自分の足元で、位置（相対）は変わったときだけ送られてくる（同じ (0,0) なら送られない）。
        ++this->level_serial_;
        ++this->map_version_;
    }
    if (m.contains("vgrdc") && m["vgrdc"].is_object()) {
        this->view_x_ = static_cast<int>(jint(m["vgrdc"], "x"));
        this->view_y_ = static_cast<int>(jint(m["vgrdc"], "y"));
    }
    const auto cells = m.find("cells");
    if ((cells == m.end()) || !cells->is_array()) {
        return;
    }
    int x = 0;
    int y = 0;
    bool first = true;
    bool changed = false;
    for (const json &c : *cells) {
        if (!c.is_object()) {
            continue;
        }
        if (c.contains("x") || c.contains("y")) {
            x = static_cast<int>(jint(c, "x", x));
            y = static_cast<int>(jint(c, "y", y));
        } else if (!first) {
            ++x;
        }
        first = false;
        // 除外（旅の除外・その中心）の印は背景のタイルの旗に入っている（`tile-flags.h`）。`t.bg` を送ったときだけ更新する。
        int excl = -1;
        if (c.contains("t") && c["t"].is_object() && c["t"].contains("bg")) {
            const std::uint64_t bg = static_cast<std::uint64_t>(jint(c["t"], "bg"));
            excl = ((bg & 0x00800000ull) != 0) ? 1 : (((bg & 0x00400000ull) != 0) ? 2 : 0);
        }
        if (c.contains("mf")) {
            const int mf = static_cast<int>(jint(c, "mf"));
            if ((mf <= kMfUnseen) || (mf >= kMfCount)) {
                changed = (this->map_.erase(key(x, y)) > 0) || changed;
            } else {
                auto &slot = this->map_[key(x, y)];
                const int keep_excl = (excl >= 0) ? excl : (slot >> 5);
                const auto value = static_cast<std::uint8_t>(mf | (keep_excl << 5));
                if (slot != value) {
                    slot = value;
                    changed = true;
                }
            }
        } else if (excl >= 0) {
            const auto it = this->map_.find(key(x, y));
            if (it != this->map_.end()) {
                const auto value = static_cast<std::uint8_t>((it->second & 0x1F) | (excl << 5));
                if (it->second != value) {
                    it->second = value;
                    changed = true;
                }
            }
        }
    }
    if (changed) {
        ++this->map_version_;
    }
}

void DcssUiModel::on_options(const void *p)
{
    const json &root = *static_cast<const json *>(p);
    const auto it = root.find("options");
    if ((it == root.end()) || !it->is_object()) {
        return;
    }
    const json &o = *it;
    apply_threshold(o, "hp_colour", this->options_.hp_colour);
    apply_threshold(o, "mp_colour", this->options_.mp_colour);
    apply_threshold(o, "stat_colour", this->options_.stat_colour);
    if (o.contains("show_game_time")) {
        this->options_.show_game_time = (jint(o, "show_game_time") != 0);
    }
    if (o.contains("always_show_doom_contam")) {
        this->options_.always_show_doom_contam = (jint(o, "always_show_doom_contam") != 0);
    }
    auto &c = this->options_.map_colour;
    apply_rgb(o, "tile_unseen_col", c[kMfUnseen]);
    apply_rgb(o, "tile_floor_col", c[kMfFloor]);
    apply_rgb(o, "tile_wall_col", c[kMfWall]);
    apply_rgb(o, "tile_mapped_floor_col", c[kMfMapFloor]);
    apply_rgb(o, "tile_mapped_wall_col", c[kMfMapWall]);
    apply_rgb(o, "tile_door_col", c[kMfDoor]);
    apply_rgb(o, "tile_item_col", c[kMfItem]);
    apply_rgb(o, "tile_monster_col", c[kMfMonsHostile]);
    apply_rgb(o, "tile_plant_col", c[kMfMonsNoExp]);
    apply_rgb(o, "tile_upstairs_col", c[kMfStairUp]);
    apply_rgb(o, "tile_downstairs_col", c[kMfStairDown]);
    apply_rgb(o, "tile_branchstairs_col", c[kMfStairBranch]);
    apply_rgb(o, "tile_feature_col", c[kMfFeature]);
    apply_rgb(o, "tile_water_col", c[kMfWater]);
    apply_rgb(o, "tile_lava_col", c[kMfLava]);
    apply_rgb(o, "tile_trap_col", c[kMfTrap]);
    apply_rgb(o, "tile_excl_centre_col", c[kMfExclRoot]);
    apply_rgb(o, "tile_excluded_col", c[kMfExcl]);
    apply_rgb(o, "tile_player_col", c[kMfPlayer]);
    apply_rgb(o, "tile_deep_water_col", c[kMfDeepWater]);
    apply_rgb(o, "tile_portal_col", c[kMfPortal]);
    apply_rgb(o, "tile_transporter_col", c[kMfTransporter]);
    apply_rgb(o, "tile_transporter_landing_col", c[kMfTransporterLanding]);
    apply_rgb(o, "tile_explore_horizon_col", c[kMfExploreHorizon]);
    apply_rgb(o, "tile_window_col", this->options_.window_colour);
    ++this->map_version_;
}

DcssUiModel::MapBounds DcssUiModel::map_bounds() const
{
    MapBounds b;
    for (const auto &entry : this->map_) {
        const int x = static_cast<int>(entry.first >> 32);
        const int y = static_cast<int>(static_cast<std::int32_t>(entry.first & 0xFFFFFFFFu));
        if (!b.valid) {
            b = { x, y, x, y, true };
        } else {
            b.min_x = std::min(b.min_x, x);
            b.min_y = std::min(b.min_y, y);
            b.max_x = std::max(b.max_x, x);
            b.max_y = std::max(b.max_y, y);
        }
    }
    return b;
}

int DcssUiModel::effective_feature(int x, int y, std::uint8_t stored) const
{
    int mf = stored & 0x1F;
    if (this->player_pos_known_ && (x == this->player_x_) && (y == this->player_y_)) {
        return kMfPlayer;
    }
    if ((mf == kMfFloor) || (mf == kMfMapFloor) || (mf == kMfWater) || (mf == kMfDeepWater) || (mf == kMfLava)) {
        const int excl = stored >> 5;
        if (excl == 1) {
            mf = kMfExclRoot;
        } else if (excl == 2) {
            mf = kMfExcl;
        }
    }
    return mf;
}

int DcssUiModel::map_feature_at(int x, int y) const
{
    const auto it = this->map_.find(key(x, y));
    return (it == this->map_.end()) ? static_cast<int>(kMfUnseen) : this->effective_feature(x, y, it->second);
}

} // namespace hd2d::dcss
