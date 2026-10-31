#pragma once

/*
 * NetHack の地形の表示記号（cmap。`include/defsym.h` の PCHAR の `S_*`）→ 画面へ渡す地形。
 *
 * 原作のヘッダ（`hack.h`）は読まない。`glyph_to_cmap()` が返す番号（0〜104）と、その glyph が持つ変種（壁の迷宮の系統・祭壇の属性）で引く。
 * 表の並びは `defsym.h` の PCHAR の番号どおりで、英語版（nethack）と日本語版（nethackjp）で同じ
 * （`tools/nethack/check_terrain_table.py` が照合する）。
 *
 * 送る地形番号（`cell.terrain_id`）は NetHack 専用の帯 20000〜20999（`nh_terrain_ids.inc`。規則 `assets/voxel/terrain_prefabs.jsonc` の
 * NetHack の節と同じ表から `tools/nethack/gen_nethack_terrain_rules.py` が作る）。表の `terrain` 欄に残っている変愚の番号は、
 * 2026-10-28 までの名残（`check_terrain_table.py` が照合しているため残してある）で、画面へは送らない。マスの性質
 * （`CELL_FEAT_*`・ミニマップの種別・罠か）は表のものを使う。
 */

#include "frame/minimap_snapshot.h"

#include <cstdint>

namespace nethack {

//! `defsym.h` の PCHAR の数（S_stone = 0 〜 S_expl_br = 104）。表の長さ。
constexpr int kCmapCount = 105;

//! 1 つの表示記号を画面へ渡すときの形。
struct TerrainLook {
    //! 画面へ渡す地形番号（`cell.terrain_id`）。`terrain_for_cmap` が NetHack の帯の番号を入れて返す。描かないときは 0。
    std::uint16_t terrain{ 0 };
    /*!
     * `CELL_FEAT_*`（`presentation/frame/cell_feature_bits.h`）。
     * **`CELL_FEAT_KNOWN` は含めない**（`known` が真のマスに呼び出し側が足す。Umoria と同じ）。
     * 怪物・アイテム・プレイヤーの印（`CELL_FEAT_PLAYER`）も呼び出し側が足す。
     */
    std::uint16_t flags{ 0 };
    //! ミニマップの種別。
    MinimapKind minimap{ MinimapKind::Unknown };
    //! 地図に描くか。偽のとき（未探索の石・効果の記号）は、ほかの欄も使わない。
    bool known{ false };
    //! 罠の記号か。真ならミニマップに `MinimapMarker::Trap` を重ねる（`terrain` は罠の絵か、無ければ床）。
    bool trap{ false };
};

/*!
 * @brief 表示記号の番号（`glyph_to_cmap()` の値）から、画面へ渡す形を引く。
 * @param cmap `S_stone`(0) 〜 `S_expl_br`(104)。範囲外は「描かない」（`known` が偽）を返す。
 * @param variant 壁（cmap 1〜11）なら迷宮の系統（0 通常・1 鉱山・2 ゲヘナ・3 ノックス・4 ソコバン）、祭壇（cmap 33）なら
 *                属性（0 無属性・1 混沌・2 中立・3 秩序・4 その他）。ほかは使わない。範囲外は 0 として扱う。
 */
TerrainLook terrain_for_cmap(int cmap, int variant = 0);

} // namespace nethack
