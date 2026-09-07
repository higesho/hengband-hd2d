/*!
 * @file terrain_memory.cpp
 * @brief `terrain_memory.h` の実装。
 */
#include "world/terrain_memory.h"

#include "frame/cell_feature_bits.h"
#include "world/floor_meaning.h"

namespace hd2d {

void TerrainMemory::update(const GameFrame &frame)
{
    // 寸法はミニマップから採る（フロア全域の広さの基準はあちら）。
    const int w = frame.minimap.width;
    const int h = frame.minimap.height;
    if ((w <= 0) || (h <= 0)) {
        return; // まだフロアが来ていない。**捨てもしない**（次のフレームを待つ）
    }
    if (!same_floor(this->identity, frame.floor) || (w != this->width) || (h != this->height)) {
        this->identity = frame.floor;
        this->width = w;
        this->height = h;
        this->ids.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        this->flags.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        this->ascii.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        this->fg.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    }
    for (const auto &cell : frame.cells) {
        if ((cell.feature_flags & CELL_FEAT_KNOWN) == 0u) {
            continue; // 未視認。Bridge が terrain_id = 0 にしている（捏造しない）
        }
        if ((cell.gx < 0) || (cell.gy < 0) || (cell.gx >= w) || (cell.gy >= h)) {
            continue;
        }
        const std::size_t at = (static_cast<std::size_t>(cell.gy) * static_cast<std::size_t>(w))
            + static_cast<std::size_t>(cell.gx);
        /*
         * **記号は `terrain_id` の有無に依らず覚える。**空白でない印字文字だけを採る
         * ——空白は「そこに出す記号が無い」であって、記憶する価値が無い。
         */
        const auto glyph = static_cast<std::uint8_t>(cell.ascii_fallback);
        if ((glyph > 0x20u) && (glyph < 0x7Fu)) {
            this->ascii[at] = glyph;
            this->fg[at] = cell.fg_color;
        }
        if (cell.terrain_id == 0) {
            continue;
        }
        this->ids[at] = cell.terrain_id;
        this->flags[at] = cell.feature_flags;
    }
}

std::uint16_t TerrainMemory::id_at(int gx, int gy) const
{
    if ((gx < 0) || (gy < 0) || (gx >= this->width) || (gy >= this->height)) {
        return 0;
    }
    return this->ids[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(this->width)) + static_cast<std::size_t>(gx)];
}

std::uint8_t TerrainMemory::ascii_at(int gx, int gy) const
{
    if ((gx < 0) || (gy < 0) || (gx >= this->width) || (gy >= this->height)) {
        return 0;
    }
    return this->ascii[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(this->width))
        + static_cast<std::size_t>(gx)];
}

std::uint8_t TerrainMemory::fg_at(int gx, int gy) const
{
    if ((gx < 0) || (gy < 0) || (gx >= this->width) || (gy >= this->height)) {
        return 0;
    }
    return this->fg[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(this->width))
        + static_cast<std::size_t>(gx)];
}

std::uint16_t TerrainMemory::flags_at(int gx, int gy) const
{
    if ((gx < 0) || (gy < 0) || (gx >= this->width) || (gy >= this->height)) {
        return 0;
    }
    return this->flags[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(this->width)) + static_cast<std::size_t>(gx)];
}

} // namespace hd2d
