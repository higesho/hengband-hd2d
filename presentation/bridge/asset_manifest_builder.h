/*!
 * @file asset_manifest_builder.h
 * @brief official_tile_table から `asset_manifest`（v1 §8.2）を組む（コア側専用）。
 *
 * タイル供給の逆転（憲章 §5.1、Phase 2 P2-1）: 表の持ち主はコアで、フロントエンドは
 * 目録を受けて描くだけ。ここは**コア側にしか置けない**（official_tile_table.h を
 * include するため）。画面側からはこのヘッダを include しないこと。
 *
 * 使い手は `core_main.cpp` だけ——encode してワイヤへ流す（分割）。
 *
 * 旧 SDL2 の入口（削除済み）は合成でそのまま渡していた。
 * 送信側・受信側が同じ変換を通ることが等価性の根拠である（v1 §8.2）。
 */
#pragma once

#include "bridge/official_tile_table.h"
#ifdef TANGBAND
#include "bridge/official_tile_table_tang.h"
#endif
#include "frame/protocol_messages.h"

#include <string>
#include <utility>

namespace presentation {

//! @param root 相対 path の基準ディレクトリ（絶対パス）。空 = 受け手の cwd 基準。
inline AssetManifestMessage build_official_asset_manifest(std::string root)
{
    AssetManifestMessage message;
    message.root = std::move(root);
    message.assets.reserve(kOfficialTileCount);
    for (const auto &entry : kOfficialTiles) {
        AssetManifestWireEntry wire;
        wire.index = static_cast<int>(entry.tile_index);
        wire.kind = std::string(1, entry.kind);
        wire.id = static_cast<int>(entry.id);
        wire.path = entry.path;
        message.assets.push_back(std::move(wire));
    }
#ifdef TANGBAND
    // 短愚蛮怒の追加実体（official_tile_table_tang.h）。変愚ビルドの目録は不変。
    for (const auto &entry : kTangbandTiles) {
        AssetManifestWireEntry wire;
        wire.index = static_cast<int>(entry.tile_index);
        wire.kind = std::string(1, entry.kind);
        wire.id = static_cast<int>(entry.id);
        wire.path = entry.path;
        message.assets.push_back(std::move(wire));
    }
#endif
    message.aliases.reserve(kOfficialTileAliasCount);
    for (const auto &alias : kOfficialTileAliases) {
        AssetManifestAliasWireEntry wire;
        wire.kind = std::string(1, alias.kind);
        wire.id = static_cast<int>(alias.id);
        wire.index = static_cast<int>(alias.tile_index);
        message.aliases.push_back(std::move(wire));
    }
    return message;
}

} // namespace presentation
