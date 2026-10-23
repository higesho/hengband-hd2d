/*!
 * @file dcss_clouds.h
 * @brief DCSS の雲を、立体の半透明のボクセルにする。段 3（D14 決め直し）。
 *
 * ## 決めたこと
 * 雲は**種類で分ける**。
 * - **視界を遮る雲**（原作の `is_opaque_cloud` が真。煙・蒸気・墨など。コアが `marks` の `c` の 4 つ目で教える）:
 *   マスの中を床から人の背くらいまで濃く埋めるボクセルの塊。
 * - **遮らない雲**（炎・冷気・毒など）: 床を這う低く薄い層。
 * どちらも空中には浮かせない。色と模様は原作の雲のタイル（`assets/ui/dcss/atlas/clouds.tsv`。`tools/dcss/gen_dcss_clouds.py` が作る。
 * タイルを 8x8 の区画にして、区画ごとの色と不透明度）に合わせる。
 *
 * ## 描き方
 * 攻撃の視覚効果のボクセル（`FxVoxel`。色・不透明度・自己発光を持つ立方体）に足し、`VoxelFxRenderer` が描く。半透明なので奥から手前の順に混ぜ、
 * 深度は読むが書かない（壁の向こうの雲は壁に隠れ、雲のマスにいる怪物・プレイヤーは透けて見える。原作の雲も怪物の上に不透明度 0.6 で重なる）。
 */
#pragma once

#include "dcss/dcss_marks.h"
#include "render/voxel_fx.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hd2d::dcss {

//! 雲のタイル 1 枚の 8x8 区画（色と不透明度）。
struct DcssCloudPattern {
    static constexpr int kGrid = 8;
    bool emissive{ false }; //!< 火・エネルギー系（自己発光で光らせる）
    std::array<std::array<std::uint8_t, 4>, kGrid * kGrid> block{}; //!< r, g, b, a（左上から右・下へ）
};

class DcssClouds {
public:
    //! `clouds.tsv` を読む（`dir` は `assets/ui/dcss/atlas`）。
    bool load(const std::string &dir, std::string &err);
    bool loaded() const { return !this->patterns_.empty(); }
    const DcssCloudPattern *pattern(int tile) const;

    /*!
     * @brief 雲のマスから立体の雲のボクセルを作って `out` に足す。足した数を返す。
     * @param seconds 起動からの秒（ゆるやかな揺れの位相）
     */
    std::size_t append_voxels(std::vector<FxVoxel> &out, const std::vector<DcssCloudCell> &cells, float seconds) const;

    // ---- 寸法（マス単位。作って見せてから詰める。単体の検査もこの値を見る）----
    //! 遮る雲の高さ（床から。人の背くらい）と、1 本の柱の層の厚み・横幅。
    static constexpr float kThickHeight = 1.1f;
    static constexpr float kThickLayer = 0.22f;
    static constexpr float kThickSide = 0.25f;
    //! 遮らない雲の層の厚みと、箱の横幅（区画 1 つぶん）。床から少し浮かせる量。
    static constexpr float kThinHeight = 0.10f;
    static constexpr float kThinSide = 0.125f;
    static constexpr float kThinLift = 0.02f;

private:
    std::map<int, DcssCloudPattern> patterns_;
};

} // namespace hd2d::dcss
