/*!
 * @file orb_profile.h
 * @brief HP/MP の玉の、コアごとの配置（`assets/ui/hud/<コア>/orb_layout.json`）。
 *
 * 玉の素材（6 層）は**コアごとに別の意匠**で、玉の中心・半径・名札と数字の高さ・絵の見えている下端も
 * 意匠ごとに違う。値は素材の設計の単位（760x250）で持つ。**変愚は今の素材（`assets/ui/hud/` 直下）のまま**
 * で、既定値（このヘッダの `kOrb*`）がそのまま変愚の値になる（1 画素も変えない）。
 *
 * 画面は 1 回の起動で 1 つのコアにつながる（コアを替えるときは起こし直す）ので、握手でコア名が届いた後に
 * `set_orb_profile()` で 1 度だけ覚えさせる（`set_status_col_cols()` と同じ形）。検査は呼ばないので、
 * 既定のまま呼べば従来どおり（変愚）の値になる。
 */
#pragma once

#include <string>

namespace hd2d {

/*!
 * @name 変愚の玉の寸法（設計の単位。`tools/hud/gen_orb_hud.py` と同じ値）
 * @details `OrbProfile` の既定値。コアごとの配置が無い（変愚・知らないコア）ときはこの値になる。
 * @{
 */
constexpr double kOrbHpCenterX = 238.0;
constexpr double kOrbMpCenterX = 522.0;
constexpr double kOrbCenterY = 128.0;
constexpr double kOrbRadius = 74.0;
//! 数字の帯の中心 y。
constexpr double kOrbNumberY = 217.0;
//! 「HP」「MP」の字の**下端** y（とげの先より上。とげの先は 43 あたり）。
constexpr double kOrbLabelBottomY = 41.0;
/*!
 * 絵の見えている下端（設計の単位）。これより下は影がだんだん薄くなるだけの部分（素材の 306/333 行より下。
 * 不透明度は 90/255 未満）で、**地図の下縁より下へはみ出させて描かない**（決めたこと 2026-10-09
 * 「もっとギリギリ下にして」）。
 */
constexpr int kOrbArtBottomY = 230;
/*! @} */

//! 玉の中央の飾り（短愚の終末の太陽）の位置と大きさ。`present` が偽なら、そのコアに飾りは無い。
struct OrbEmblem {
    bool present{ false };
    double x{ 0.0 }; //!< 中心（設計の単位）
    double y{ 0.0 };
    double r{ 0.0 }; //!< 太陽の円の半径（絵の中央の黒い円と同じ大きさ）
};

//! 玉の配置。単位はすべて素材の設計の単位（760x250）。
struct OrbProfile {
    //! 素材の組の名前（`assets/ui/hud/<ここ>/`）。空なら `assets/ui/hud/` 直下（変愚）。
    std::string asset_key;
    double hp_cx{ kOrbHpCenterX };
    double mp_cx{ kOrbMpCenterX };
    double cy{ kOrbCenterY };
    double r{ kOrbRadius };
    double number_y{ kOrbNumberY };
    double label_bottom_y{ kOrbLabelBottomY };
    int art_bottom_y{ kOrbArtBottomY };
    OrbEmblem emblem;
};

//! いま覚えている配置（既定は変愚）。
const OrbProfile &orb_profile();
//! 配置を覚える。**握手でコア名が届いた後に 1 度だけ呼ぶこと**（検査は切り替えて描いてもよい）。
void set_orb_profile(const OrbProfile &profile);

} // namespace hd2d
