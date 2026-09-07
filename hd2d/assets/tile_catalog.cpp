/*!
 * @file tile_catalog.cpp
 * @brief `tile_catalog.h` の実装。
 */
#include "assets/tile_catalog.h"

#include <cstdio>

namespace hd2d {

namespace {

std::string join(const std::string &root, const std::string &relative)
{
    if (root.empty()) {
        return relative; // 空 = ui の cwd 基準（v1 §8.2）
    }
    const char last = root.back();
    const std::string separator = ((last == '/') || (last == '\\')) ? "" : "/";
    return root + separator + relative;
}

} // namespace

void TileCatalog::apply(const presentation::AssetManifestMessage &message)
{
    this->entries_.clear();
    this->entries_.reserve(message.assets.size() + message.aliases.size());

    for (const auto &wire : message.assets) {
        if ((wire.index <= 0) || (wire.index > 65535)) {
            continue; // 索引 0 は「未登録」の意味なので運ばれない（v1 §8.2）
        }
        TileEntry entry;
        entry.kind = wire.kind.empty() ? 0 : wire.kind[0];
        entry.id = wire.id;
        entry.path = join(message.root, wire.path);
        this->entries_[static_cast<std::uint16_t>(wire.index)] = std::move(entry);
    }

    /*
     * 別名（`aliases`）は**新しい索引を作らない**。既存の索引へ別の ID を対応づけるだけ
     * （擬態など）。ここでは索引 → 絵の対応しか使わないので、共有元が目録にあることだけ
     * 確かめて数える。
     */
    int dangling = 0;
    for (const auto &alias : message.aliases) {
        if (this->entries_.find(static_cast<std::uint16_t>(alias.index)) == this->entries_.end()) {
            ++dangling;
        }
    }
    if (dangling > 0) {
        std::fprintf(stderr, "[hd2d] asset_manifest: %d aliases point at unknown indices\n", dangling);
    }
    this->present_ = true;
    std::fprintf(stderr, "[hd2d] tile catalog: %zu entries (root=%s)\n",
        this->entries_.size(), message.root.empty() ? "<cwd>" : message.root.c_str());
}

const TileEntry *TileCatalog::find(std::uint16_t tile_index) const
{
    const auto found = this->entries_.find(tile_index);
    return (found == this->entries_.end()) ? nullptr : &found->second;
}

} // namespace hd2d
