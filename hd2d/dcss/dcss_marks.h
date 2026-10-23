/*!
 * @file dcss_marks.h
 * @brief DCSS の地図の上の印のうち、画面の部品が持つ分: 態度と強さの足元の輪（D16・D23）と、立体の雲の材料（D14）。段 3。
 *
 * ## 何のためのものか
 * 原作のタイル版は、怪物のマスに態度（友好・中立・平和）と強さの段階（弱い〜危険）を色の枠で示す（`cell_renderer.js` の `draw_background`）。
 * このコアでは足元の輪（`render/ground_ring.*`）で示す（D23 で**半透明**に決めた）。色は原作の枠の絵の主な色。
 * 輪はなめらかな位置に置くので `EntityTracker::build` が積む（ここは色を `RingTint` で渡すだけ。渡さなければ従来どおり）。
 *
 * **状態のアイコン**（眠り・混乱・毒・麻痺ほか）と**悪魔の等級の数字**は、ここでは扱わない。コアのアダプタが怪物の板に焼き込む
 * （`dcss/adapter/dcss_frame.cpp` の `append_icon_layers`）ので、板と同じ位置・向きで立体の中に立ち、壁の向こうでは隠れる（D23）。
 *
 * ## 材料
 * コアのアダプタ（`marks_json`）が、見えている怪物とプレイヤーのマスの前景の旗と、見えているマスの雲を
 * `{"t":"dcss_ui","k":"marks","m":[[gx, gy, 旗の下位32ビット, 旗の上位32ビット, [アイコン…], 召喚者(0|1), プレイヤー(0|1)], …],
 *  "c":[[gx, gy, 雲のタイル番号, 視界を遮るか(0|1)], …],
 *  "f":[[gx, gy, [タイル番号…], プレイヤーの行動の最中か], …], "fl":[[gx, gy, 色の番号, 濃さ], …], "p":[gx, gy]}` で送る。輪の色の判定は原作の `cell_renderer.js` と同じ規則。
 * `f`・`fl` は攻撃の演出（ビーム・矢・爆発・閃光。段 4。`dcss_fx.h`）で、原作が `delay` のたびに出す途中の絵のうち「いまある物」。
 */
#pragma once

#include "world/entity_view.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hd2d::dcss {

//! 原作の前景の旗（`tile-flags.h`）。
namespace fgflag {
constexpr std::uint64_t kAttMask = 0x00030000ULL;
constexpr std::uint64_t kPet = 0x00010000ULL;
constexpr std::uint64_t kGoodNeutral = 0x00020000ULL;
constexpr std::uint64_t kNeutral = 0x00030000ULL;
constexpr std::uint64_t kThreatMask = 0xE000000000000000ULL;
constexpr std::uint64_t kTrivial = 0x2000000000000000ULL;
constexpr std::uint64_t kEasy = 0x4000000000000000ULL;
constexpr std::uint64_t kTough = 0x6000000000000000ULL;
constexpr std::uint64_t kNasty = 0x8000000000000000ULL;
constexpr std::uint64_t kUnusual = 0xE000000000000000ULL;
constexpr std::uint64_t kGhost = 0x10000000000000ULL;
} // namespace fgflag

//! 1 マスの印（コアが送った物）。座標は画面のフレームのマス（`MapCellView::gx/gy`）。
struct DcssMark {
    int gx{ 0 };
    int gy{ 0 };
    std::uint64_t flags{ 0 }; //!< 前景の旗（タイル番号の下位 16 ビットは除いてある）
    bool summoner{ false };
    bool player{ false };
};

//! 見えているマスの雲 1 つ。`opaque` は原作の `is_opaque_cloud`（視界を遮る雲）。
struct DcssCloudCell {
    int gx{ 0 };
    int gy{ 0 };
    int tile{ 0 }; //!< 雲のタイル番号（`t.cloud` の下位 16 ビット）
    bool opaque{ false };
};

//! 攻撃の演出の絵 1 マスぶん（コアが `t.ov` から拾った、画像ページ main のタイル。ビームの弾・矢・投げた物・爆発）。
struct DcssFxCell {
    int gx{ 0 };
    int gy{ 0 };
    std::vector<int> tiles;
    //! 届いたとき、プレイヤー自身の行動の最中だったか（撃ったのがプレイヤーなら、弾はプレイヤーの側から飛ぶ。怪物・味方の行動なら逆）。
    bool by_player{ true };
};

//! 閃光 1 マス（原作の `flc`・`fla`）。`colour` は原作の色番号（1〜15）、`alpha` は 0〜255（0 = 色の表の既定の濃さ）。
struct DcssFlashCell {
    int gx{ 0 };
    int gy{ 0 };
    int colour{ 0 };
    int alpha{ 0 };
};

class DcssMarks {
public:
    //! `{"t":"dcss_ui","k":"marks",…}` を取り込む。変わったら真。
    bool ingest_json(const std::string &payload);
    //! 画面の部品が決めた足元の輪（半透明）。`EntityTracker::build` に渡す。
    const std::vector<RingTint> &ring_tints() const { return this->rings_; }
    const std::vector<DcssMark> &marks() const { return this->marks_; }
    const std::vector<DcssCloudCell> &clouds() const { return this->clouds_; }
    //! 攻撃の演出（段 4）。`delay` のたびに「いまある物のすべて」が届く。
    const std::vector<DcssFxCell> &fx_cells() const { return this->fx_cells_; }
    const std::vector<DcssFlashCell> &flash_cells() const { return this->flash_cells_; }
    //! プレイヤーのマス（`p`。無ければ負）。攻撃の演出で弾の飛ぶ向きを決める手がかり。
    int player_gx() const { return this->player_gx_; }
    int player_gy() const { return this->player_gy_; }
    std::uint64_t serial() const { return this->serial_; }

    // ---- 原作の規則（単体で検査できる）----
    //! 態度・強さの輪（原作の枠の絵の主な色）。`mark.player` なら空。
    static std::vector<RingTint> rings_for(const DcssMark &mark);

private:
    std::vector<DcssMark> marks_;
    std::vector<RingTint> rings_;
    std::vector<DcssCloudCell> clouds_;
    std::vector<DcssFxCell> fx_cells_;
    std::vector<DcssFlashCell> flash_cells_;
    int player_gx_{ -1 };
    int player_gy_{ -1 };
    std::uint64_t serial_{ 0 };
};

} // namespace hd2d::dcss
