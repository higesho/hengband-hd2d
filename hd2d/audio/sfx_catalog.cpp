/*!
 * @file sfx_catalog.cpp
 * @brief `sfx_catalog.h` の実装。
 */
#include "audio/sfx_catalog.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace hd2d::audio {

bool SfxCatalog::load(const std::string &dir, std::string *log)
{
    this->table_.clear();
    this->dir_.clear();
    const std::string sep = (dir.empty() || (dir.back() == '/') || (dir.back() == '\\')) ? "" : "/";
    const std::string path = dir + sep + "sfx.jsonc";
    std::ifstream file(path);
    if (!file) {
        if (log != nullptr) {
            *log = path + " が無い（効果音は鳴らない）";
        }
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    nlohmann::json root;
    try {
        //! 第 4 引数 = 注釈を無視（`ambience.jsonc` と同じ読み方）。
        root = nlohmann::json::parse(buffer.str(), nullptr, true, true);
    } catch (const std::exception &e) {
        if (log != nullptr) {
            *log = std::string("sfx.jsonc を解釈できません: ") + e.what();
        }
        return false;
    }
    if (const auto it = root.find("dir"); (it != root.end()) && it->is_string()) {
        this->dir_ = it->get<std::string>();
    }
    const auto sounds = root.find("sounds");
    if ((sounds == root.end()) || !sounds->is_object()) {
        if (log != nullptr) {
            *log = "sfx.jsonc に sounds がありません";
        }
        return false;
    }
    for (const auto &[name, value] : sounds->items()) {
        std::vector<std::string> files;
        if (value.is_string()) {
            files.push_back(value.get<std::string>());
        } else if (value.is_array()) {
            for (const auto &one : value) {
                if (one.is_string()) {
                    files.push_back(one.get<std::string>());
                }
            }
        }
        if (!files.empty()) {
            this->table_[name] = std::move(files);
        }
    }
    if (log != nullptr) {
        std::ostringstream note;
        note << "sfx.jsonc: " << this->table_.size() << " 種 / 素材は " << this->dir_;
        *log = note.str();
    }
    return true;
}

void SfxCatalog::set_entry(const std::string &name, std::vector<std::string> files)
{
    this->table_[name] = std::move(files);
}

std::string SfxCatalog::path_for(const std::string &name, std::uint32_t pick) const
{
    const auto it = this->table_.find(name);
    if ((it == this->table_.end()) || it->second.empty()) {
        return {}; //!< 知らない音。**鳴らさないだけ**（落ちない）
    }
    const std::vector<std::string> &files = it->second;
    const std::string &file = files[pick % files.size()];
    //! `:` を含むならフルパス（`music.cfg` の流儀に合わせる）。
    if (file.find(':') != std::string::npos) {
        return file;
    }
    const std::string sep
        = (this->dir_.empty() || (this->dir_.back() == '/') || (this->dir_.back() == '\\')) ? "" : "/";
    return this->dir_ + sep + file;
}

} // namespace hd2d::audio
