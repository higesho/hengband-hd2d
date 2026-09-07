/*!
 * @file terrain_memory.h
 * @brief 地形の細別（`terrain_id`）の**記憶**（P10・設計書 §9 ①の残り半分）。
 *
 * ## なぜ要るのか
 * 意味づけ層（`FloorMeaning`）はミニマップ（フロア全域・1 マス 1 バイト）から作るが、
 * ミニマップの種別は 7 つしかない。「草か石畳か」「水か溶岩か」は
 * **可視窓のマス（`GameFrame::cells`）の `terrain_id`** にしか無く、これは
 * 画面に写った範囲しか運ばれない。
 *
 * そこで、**間近で見たマスの `terrain_id` と `feature_flags` をフロア単位で蓄積する**。
 * §9 ① の永続化はミニマップ採用で一度やめたが（`floor_meaning.h` の注記）、
 * 細別だけはこの蓄積でしか得られないので、ここに限って復活させる。
 *
 * ## 約束
 * - **見ていないマスは 0 のまま**（見えていない地形を捏造しない。描く側は役割の既定へ落とす）
 * - 未視認セルは Bridge が `terrain_id = 0` で送ってくる（情報は漏れない）ので、
 *   `CELL_FEAT_KNOWN` が立ち `terrain_id != 0` のセルだけ覚える
 * - フロア識別子が変わったら全部捨てる（別の階の記憶は意味を持たない）
 * - 一度覚えた値は上書きで更新する（扉が開くと `terrain_id` が変わる。古い記憶より新しい目）
 */
#pragma once

#include "frame/game_frame.h"

#include <cstdint>
#include <vector>

namespace hd2d {

struct TerrainMemory {
    FloorIdentity identity{};
    int width{ 0 };
    int height{ 0 };
    std::vector<std::uint16_t> ids; //!< `terrain_id`（MIMIC。0 = まだ間近で見ていない）
    std::vector<std::uint16_t> flags; //!< `feature_flags`
    /*!
     * @name **マスの記号と前景色**の記憶
     *
     * @details `terrain_id` とまったく同じ理由でここに置く——記号（`ascii_fallback`）も
     * 前景色も**可視窓のマスにしか無い**ので、覚えておかないと「近くの壁には字があるのに
     * 遠くの覚えた壁には無い」になる。フロアが変われば `ids` と一緒に捨てられる。
     *
     * `ascii` は 0 なら「まだ見ていない」。`fg` はコアの 16 色の索引で、
     * 実際の RGB は描く直前に `term_color_to_rgb()` で引く（色表は `&` や pref で変わるので、
     * **索引のまま覚えて、引くのは毎回**にする）。
     *
     * @note 記号は「上の記号」の意味論をそのまま持つ（実体が乗っているマスはその実体の記号）。
     * 決めたことで承知のうえである。
     * @{
     */
    std::vector<std::uint8_t> ascii;
    std::vector<std::uint8_t> fg;
    /*! @} */

    bool valid() const { return (this->width > 0) && (this->height > 0); }

    //! フレームを取り込む（フロアが変われば捨てて作り直す）。
    void update(const GameFrame &frame);

    //! そのマスの `terrain_id`。範囲外・未見は 0。
    std::uint16_t id_at(int gx, int gy) const;
    //! そのマスの `feature_flags`。範囲外・未見は 0。
    std::uint16_t flags_at(int gx, int gy) const;
    //! そのマスの記号。範囲外・未見は 0（**捏造しない**）。
    std::uint8_t ascii_at(int gx, int gy) const;
    //! そのマスの前景色の索引。範囲外・未見は 0。
    std::uint8_t fg_at(int gx, int gy) const;
};

} // namespace hd2d
