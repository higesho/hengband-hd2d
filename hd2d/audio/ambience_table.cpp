/*!
 * @file ambience_table.cpp
 * @brief `ambience_table.h` の実装。**読むのと引くのだけ。**
 */
#include "audio/ambience_table.h"

#include "audio/ambience_mix.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace hd2d::audio {

namespace {

//! `when` の中の整数（無ければ空）。**知らないキーは黙って飛ばす**（版が進んでも落ちない）。
std::optional<int> take_int(const nlohmann::json &when, const char *key)
{
    const auto it = when.find(key);
    if ((it == when.end()) || !it->is_number_integer()) {
        return std::nullopt;
    }
    return it->get<int>();
}

std::optional<bool> take_bool(const nlohmann::json &when, const char *key)
{
    const auto it = when.find(key);
    if ((it == when.end()) || !it->is_boolean()) {
        return std::nullopt;
    }
    return it->get<bool>();
}

} // namespace

bool is_night(int day_minute)
{
    if (day_minute < 0) {
        return false; //!< 分からないものを夜にしない（§ヘッダの注記）
    }
    return (day_minute < (6 * 60)) || (day_minute >= (18 * 60));
}

bool AmbienceTable::load(const std::string &dir, std::string *log)
{
    this->rules_.clear();
    const std::string sep = (dir.empty() || (dir.back() == '/') || (dir.back() == '\\')) ? "" : "/";
    const std::string path = dir + sep + "ambience.jsonc";
    std::ifstream file(path);
    if (!file) {
        if (log != nullptr) {
            *log = path + " が無い（環境音は鳴らない）";
        }
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    nlohmann::json root;
    try {
        //! 第 4 引数 = 注釈を無視（`dungeon_styles.jsonc` と同じ読み方）。
        root = nlohmann::json::parse(buffer.str(), nullptr, true, true);
    } catch (const std::exception &e) {
        if (log != nullptr) {
            *log = std::string("ambience.jsonc を解釈できません: ") + e.what();
        }
        return false;
    }
    const auto rules = root.find("rules");
    if ((rules == root.end()) || !rules->is_array()) {
        if (log != nullptr) {
            *log = "ambience.jsonc に rules がありません";
        }
        return false;
    }
    for (const auto &item : *rules) {
        if (!item.is_object()) {
            continue;
        }
        AmbienceRule rule;
        const auto bed = item.find("bed");
        if ((bed != item.end()) && bed->is_string()) {
            rule.bed = bed->get<std::string>();
        }
        const auto when = item.find("when");
        if ((when != item.end()) && when->is_object()) {
            rule.floor_kind = take_int(*when, "floor");
            rule.dungeon_id = take_int(*when, "dungeon");
            rule.town_id = take_int(*when, "town");
            rule.depth_min = take_int(*when, "depth_min");
            rule.depth_max = take_int(*when, "depth_max");
            rule.wild = take_bool(*when, "wild");
            rule.night = take_bool(*when, "night");
            if (const auto g = when->find("ground"); (g != when->end()) && g->is_string()) {
                const auto kind = ground_from_key(g->get<std::string>());
                if (kind != GroundKind::Unknown) {
                    rule.ground = static_cast<int>(kind);
                }
            }
            if (const auto tb = when->find("time"); (tb != when->end()) && tb->is_string()) {
                const std::string key = tb->get<std::string>();
                for (const auto band : { TimeBand::Dawn, TimeBand::Day, TimeBand::Dusk, TimeBand::Night }) {
                    if (key == time_band_key(band)) {
                        rule.time_band = static_cast<int>(band);
                        break;
                    }
                }
            }
        }
        this->rules_.push_back(std::move(rule));
    }
    if (log != nullptr) {
        std::ostringstream note;
        note << "ambience.jsonc: " << this->rules_.size() << " 本";
        *log = note.str();
    }
    return true;
}

std::string AmbienceTable::bed_for(const AmbienceScene &scene) const
{
    for (const AmbienceRule &rule : this->rules_) {
        if (rule.floor_kind && (*rule.floor_kind != scene.floor_kind)) {
            continue;
        }
        if (rule.dungeon_id && (*rule.dungeon_id != scene.dungeon_id)) {
            continue;
        }
        if (rule.town_id && (*rule.town_id != scene.town_id)) {
            continue;
        }
        if (rule.depth_min && (scene.depth < *rule.depth_min)) {
            continue;
        }
        if (rule.depth_max && (scene.depth > *rule.depth_max)) {
            continue;
        }
        if (rule.wild && (*rule.wild != scene.wild)) {
            continue;
        }
        if (rule.night && (*rule.night != is_night(scene.day_minute))) {
            continue;
        }
        if (rule.ground && (*rule.ground != scene.ground)) {
            continue;
        }
        if (rule.time_band && (*rule.time_band != scene.time_band)) {
            continue;
        }
        return rule.bed; //!< 空なら「ここで鳴らさない」（明示。ヘッダの注記）
    }
    return {};
}

} // namespace hd2d::audio
