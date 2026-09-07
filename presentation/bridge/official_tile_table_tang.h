/*!
 * @file official_tile_table_tang.h
 * @brief 短愚蛮怒（TANGBAND）専用の追加実体表。
 *
 * `official_tile_table.h` は tile_mapping.csv からの**生成物**なので直接足さない
 * （再生成で消える）。短愚蛮怒が独自に持つ実体はこの手書きの表に置き、
 * - 目録（asset_manifest_builder.h）
 * - セルの tile_index（presentation_bridge.cpp の lookup フォールバック）
 * の両方が TANGBAND ビルドでだけこれを連結する。変愚ビルドの目録・索引は不変。
 *
 * 索引は kOfficialTileCount（=1933）の**続き番号**を振る。本家の表が再生成で
 * 増えたら衝突するので、ここは定数直書きではなく kOfficialTileCount からの
 * 相対で書くこと。経緯と ID の振り直しは tangband/UPSTREAM.md。
 */
#pragma once

#ifdef TANGBAND

#include "bridge/official_tile_table.h"

namespace presentation {

inline constexpr OfficialTileEntry kTangbandTiles[] = {
    // 終末のサーペント（doomsday イベント専用。R862『混沌のサーペント』の灰燼色替え）
    { static_cast<uint16_t>(kOfficialTileCount + 1), 'R', 1417, "tilework/sfc/R1417.png" },
};

inline constexpr std::size_t kTangbandTileCount = sizeof(kTangbandTiles) / sizeof(kTangbandTiles[0]);

//! @brief 短愚蛮怒の追加モンスター索引。無ければ 0（本家表の後段フォールバック用）。
inline uint16_t tangband_lookup_monster_tile(uint16_t monrace_id)
{
    for (const auto &entry : kTangbandTiles) {
        if ((entry.kind == 'R') && (entry.id == monrace_id)) {
            return entry.tile_index;
        }
    }
    return 0;
}

} // namespace presentation

#endif // TANGBAND
