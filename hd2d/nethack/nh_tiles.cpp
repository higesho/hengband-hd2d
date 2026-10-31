/*!
 * @file nh_tiles.cpp
 * @brief `nh_tiles.h` の実装。
 */
#include "nethack/nh_tiles.h"

#include <nlohmann/json.hpp>

namespace hd2d::nethack {

namespace {

int int_of(const nlohmann::json &o, const char *key, int fallback)
{
    const auto it = o.find(key);
    if ((it == o.end()) || !it->is_number()) {
        return fallback;
    }
    return it->is_number_integer() ? static_cast<int>(it->get<long long>()) : static_cast<int>(it->get<double>());
}

} // namespace

int NhTileTable::tile_for_glyph(int glyph) const
{
    if ((glyph < 0) || (glyph >= static_cast<int>(this->glyph2tile.size()))) {
        return -1;
    }
    const int tile = this->glyph2tile[static_cast<std::size_t>(glyph)];
    if ((tile < 0) || ((this->tile_count > 0) && (tile >= this->tile_count))) {
        return -1;
    }
    return tile;
}

bool NhTileTable::tile_origin(int tile, int &x, int &y) const
{
    if ((tile < 0) || (this->columns <= 0) || (this->tile_size <= 0)) {
        return false;
    }
    if ((this->tile_count > 0) && (tile >= this->tile_count)) {
        return false;
    }
    x = (tile % this->columns) * this->tile_size;
    y = (tile / this->columns) * this->tile_size;
    return true;
}

bool nh_tiles_parse(const std::string &json_text, NhTileTable &out, std::string &err)
{
    const nlohmann::json j = nlohmann::json::parse(json_text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        err = "tiles.json を読めません（JSON が壊れています）。";
        return false;
    }
    NhTileTable table;
    table.tile_size = int_of(j, "tile_size", 16);
    table.columns = int_of(j, "columns", 40);
    table.rows = int_of(j, "rows", 0);
    table.tile_count = int_of(j, "tile_count", 0);
    const auto map = j.find("glyph2tile");
    if ((map == j.end()) || !map->is_array() || map->empty()) {
        err = "tiles.json に glyph2tile がありません。";
        return false;
    }
    table.glyph2tile.reserve(map->size());
    for (const nlohmann::json &v : *map) {
        table.glyph2tile.push_back(v.is_number_integer() ? static_cast<int>(v.get<long long>()) : -1);
    }
    if ((table.tile_size <= 0) || (table.columns <= 0)) {
        err = "tiles.json の tile_size か columns が正しくありません。";
        return false;
    }
    out = std::move(table);
    return true;
}

bool nh_tile_uv(const NhTileTable &table, int tile, int tex_w, int tex_h, float &u0, float &v0, float &u1, float &v1)
{
    int x = 0;
    int y = 0;
    if ((tex_w <= 0) || (tex_h <= 0) || !table.tile_origin(tile, x, y)) {
        return false;
    }
    if ((x + table.tile_size > tex_w) || (y + table.tile_size > tex_h)) {
        return false;
    }
    const float w = static_cast<float>(tex_w);
    const float h = static_cast<float>(tex_h);
    constexpr float kInset = 0.02f; // 最近傍で貼る前提。隣の絵を拾わない最小の余り
    u0 = (static_cast<float>(x) + kInset) / w;
    v0 = (static_cast<float>(y) + kInset) / h;
    u1 = (static_cast<float>(x + table.tile_size) - kInset) / w;
    v1 = (static_cast<float>(y + table.tile_size) - kInset) / h;
    return true;
}

} // namespace hd2d::nethack
