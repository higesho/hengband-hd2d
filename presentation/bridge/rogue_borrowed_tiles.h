/*!
 * @file rogue_borrowed_tiles.h
 * @brief Rogue 系のコア（Rogue 3.6・5.4・RogueClone2）が FroxComposband の実体の板を借りる表。
 *
 * 変愚にいない種族（emu・quagga・RogueClone2 の大つのじか）に、Frox 用に作った
 * 板（`frox/assets/slab/FR*.vox`）を当てる。仕組みは短愚蛮怒の追加表
 * （`official_tile_table_tang.h`）と同じで、
 * - 目録（`asset_manifest`）へ kOfficialTileCount の続き番号で行を足す
 * - `hello_ack.asset_roots.slab` に Frox の板の置き場を申告する
 * の 2 つだけ。画面側は［申告 → 既定］の 2 段で板を探すので、変愚の `R955` などは
 * 従来どおり既定側で、借りた `FR1327` などは申告側で見つかる。Frox の板は名前の頭に
 * `F` が付いているので、変愚の板を横取りしない（fc_main.cpp の frox_slab_dir() の説明）。
 *
 * 番号の id は **Frox の r_idx**（変愚の番号ではない。変愚の 1327 は Piranha Plant）。
 *
 * 2026-09-28 に 2 種を足した:
 * - **Frox のプレイヤー種族の板**（`FP{prace}`）。centaur に Frox の種族 Centaur（FP51）を当てる。
 *   板の名前は PNG のパスで決まるので、FR 系と同じく申告側（`frox/assets/slab`）で見つかる。
 * - **Rogue 用に生成した板**（aquator。どのコアにも無い姿なので生成した）。原画は
 *   `rogue_legacy/tilework/{sfc,64}/RLaquator.png`、板は**既定の置き場**
 *   `assets/voxel/slab/RLaquator.vox`（Windows・Linux・APK のどれにも `assets/voxel` ごと入る）。
 *   名前の頭 `RL` は変愚の `R{数}` と重ならない。
 * - 2026-09-29 に medusa も同じ流れで生成した（`RLmedusa`。蛇の髪の女性。それまで当てていた
 *   変愚の Gorgon（R475）は牛頭の姿だった。決めたこと）。
 *
 * コアの表（rl_frame.cpp・r2_frame.cpp）は変愚の番号を正の数で書き、借りる板は
 * 負の数で書く: `frox_monster(r_idx)`・`frox_player(prace)`・`rogue_own_tile(n)`。
 */
#pragma once

#include "bridge/asset_manifest_builder.h"
#include "frame/protocol_messages.h"
#include "portable/legacy_os.h"

#include <cstdint>
#include <string>
#include <utility>

namespace presentation {

enum class RogueBorrowedSource : uint8_t {
    FroxMonster, //!< Frox の r_idx（`FR{id}`）
    FroxPlayer,  //!< Frox のプレイヤー種族（`FP{prace}`）
    RogueOwn,    //!< Rogue 用に生成した板（`RL*`）
};

struct RogueBorrowedTile {
    uint16_t tile_index;
    RogueBorrowedSource source;
    uint16_t id; //!< source ごとの番号（r_idx・prace・生成した板の通し番号）
    const char *path;
};

//! コアの表に書く負の数の帯。`-(帯 + id)` で source と id を運ぶ。
inline constexpr int kRogueBorrowedPlayerBand = 100000;
inline constexpr int kRogueOwnTileBand = 200000;

constexpr int frox_monster(int r_idx) { return -r_idx; }
constexpr int frox_player(int prace) { return -(kRogueBorrowedPlayerBand + prace); }
constexpr int rogue_own_tile(int n) { return -(kRogueOwnTileBand + n); }

inline constexpr RogueBorrowedTile kRogueBorrowedTiles[] = {
    { static_cast<uint16_t>(kOfficialTileCount + 1), RogueBorrowedSource::FroxMonster, 1327, "frox/tilework/64/FR1327.png" }, // Cassowary（走鳥）
    { static_cast<uint16_t>(kOfficialTileCount + 2), RogueBorrowedSource::FroxMonster, 1334, "frox/tilework/64/FR1334.png" }, // Brumby（野生の馬）
    { static_cast<uint16_t>(kOfficialTileCount + 3), RogueBorrowedSource::FroxMonster, 1081, "frox/tilework/64/FR1081.png" }, // Reindeer（トナカイ）
    { static_cast<uint16_t>(kOfficialTileCount + 4), RogueBorrowedSource::FroxPlayer, 51, "frox/tilework/64/FP51.png" },      // 種族 Centaur（ケンタウロス）
    { static_cast<uint16_t>(kOfficialTileCount + 5), RogueBorrowedSource::RogueOwn, 1, "rogue_legacy/tilework/64/RLaquator.png" }, // aquator（生成）
    { static_cast<uint16_t>(kOfficialTileCount + 6), RogueBorrowedSource::RogueOwn, 2, "rogue_legacy/tilework/64/RLmedusa.png" },  // medusa（生成。蛇の髪の女性）
};

//! コアの表の負の数（`frox_monster` など）から借りた板の索引を引く。無ければ 0。
inline uint16_t rogue_borrowed_tile(int encoded)
{
    if (encoded >= 0) return 0;
    const int magnitude = -encoded;
    RogueBorrowedSource source = RogueBorrowedSource::FroxMonster;
    int id = magnitude;
    if (magnitude >= kRogueOwnTileBand) {
        source = RogueBorrowedSource::RogueOwn;
        id = magnitude - kRogueOwnTileBand;
    } else if (magnitude >= kRogueBorrowedPlayerBand) {
        source = RogueBorrowedSource::FroxPlayer;
        id = magnitude - kRogueBorrowedPlayerBand;
    }
    for (const auto &entry : kRogueBorrowedTiles) {
        if ((entry.source == source) && (entry.id == id)) return entry.tile_index;
    }
    return 0;
}

//! 変愚の表に借りた板の行を足した目録。
inline AssetManifestMessage build_rogue_asset_manifest()
{
    AssetManifestMessage message = build_official_asset_manifest("");
    for (const auto &entry : kRogueBorrowedTiles) {
        AssetManifestWireEntry wire;
        wire.index = static_cast<int>(entry.tile_index);
        wire.kind = (entry.source == RogueBorrowedSource::FroxPlayer) ? "P" : "R";
        wire.id = static_cast<int>(entry.id);
        wire.path = entry.path;
        message.assets.push_back(std::move(wire));
    }
    return message;
}

//! `hello_ack.asset_roots.slab` に載せる Frox の板の置き場（`<data_dir>/frox/assets/slab`）。
inline std::string rogue_borrowed_slab_dir()
{
    const std::string base = portable::data_dir();
    if (base.empty()) return std::string();
    // パスは ANSI で持つ。JSON に載せるときだけ UTF-8 にする（日本語の名前のフォルダで握手が落ちていた。2026-10-06）
    return portable::path_to_utf8(base + "frox" + portable::kPathSep + "assets" + portable::kPathSep + "slab");
}

} // namespace presentation
