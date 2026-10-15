/*!
 * @file fx_look_motifs.h
 * @brief Frox・幻想蛮怒の見た目が共有する「模様」（渦・目・球殻・流れ・岩・文字の格子の形）。
 *
 * 1 属性の絵は、ここの模様に色と大きさを渡して組み立てる（`fx_frox.cpp`・`fx_gensoband.cpp`）。どれも状態を持たない。
 */
#pragma once

#include "render/fx_styles/fx_look_kit.h"

namespace hd2d {
namespace looks {

/* ------------------------------------------------------------ 文字の格子の形 */

const FxShape &shape_star5();     //!< 5 つの角の星
const FxShape &shape_star4();     //!< 十字の星
const FxShape &shape_heart();     //!< ハート
const FxShape &shape_question();  //!< ？
const FxShape &shape_exclaim();   //!< ！
const FxShape &shape_bang_anger(); //!< 怒りの印（血管の十字）
const FxShape &shape_eye(int stage); //!< 目（0 開く・1 半分・2 閉じる）。`#` 縁・`o` 白目・`O` 瞳
const FxShape &shape_hand(bool fist);
const FxShape &shape_crescent();
const FxShape &shape_snowflake();
const FxShape &shape_skull();
const FxShape &shape_chicken();
const FxShape &shape_bird();       //!< 鳥（鷹）の影
const FxShape &shape_pagoda();     //!< 宝塔
const FxShape &shape_umbrella();
const FxShape &shape_drum();
const FxShape &shape_plate();      //!< 皿（正面）
const FxShape &shape_mask();       //!< 面
const FxShape &shape_bat();
const FxShape &shape_ghost();      //!< ひとだま・おばけ
const FxShape &shape_bomb();       //!< 爆弾
const FxShape &shape_coin();       //!< 硬貨
const FxShape &shape_spear();      //!< 槍（上が穂先）
const FxShape &shape_sword();      //!< 剣（上が切っ先）
const FxShape &shape_roof();       //!< 金の屋根（金閣）
const FxShape &shape_cup();        //!< 盃
const FxShape &shape_gap();        //!< スキマ（縦長の裂け目。`#` 縁・`o` 中・`e` 目）
const FxShape &shape_head();       //!< 赤い髪の首（`r` 髪・`w` 肌・`e` 目）
const FxShape &shape_hitodama();   //!< ひとだま（炎の玉と尾）
const FxShape &shape_note();       //!< 音符
const FxShape &shape_z();          //!< Z（眠り・悪夢）

/* ------------------------------------------------------------ 模様 */

//! 渦：粒が螺旋を描いて昇る。`radius0`（足元）から `radius1`（頂）へ広がる。
struct WhirlParams {
    float height{ 1.3f };
    float radius0{ 0.1f };
    float radius1{ 0.38f };
    float speed{ 9.f };      //!< 回る速さ（ラジアン/秒）
    float size{ 0.055f };
    float life{ 1.2f };
    float rise{ 0.f };       //!< 昇る速さ（マス/秒）
    float born{ 0.f };
    float duration{ 0.5f };  //!< 粒が出はじめてから出そろうまで
    int count{ 36 };
    FxColour c0{ 1.f, 1.f, 1.f };
    FxColour c1{ 0.6f, 0.8f, 1.f };
    float emissive{ 2.f };
    float z0{ 0.05f };       //!< 足元の高さ
    FxPart part{ FxPart::Particle };
    std::uint32_t salt{ 0 };
};
void whirl(const Look &L, FxFrame &out, const WhirlParams &p);

//! 球の表面に並べた点（球殻）。`fraction`（0〜1）の分だけ上から順に出る。`spin` で回る。
void sphere_shell(const Look &L, FxFrame &out, const Vec3 &centre, float radius, int count, float size, const FxColour &c, float emissive, float alpha,
    float spin, float fraction = 1.f, std::uint32_t salt = 0);

//! 岩の塊（小さな立方体の寄せ集め）。`yaw` で回す。
void rock_cluster(FxFrame &out, const Vec3 &centre, float size, float yaw, const FxColour &a, const FxColour &b, float alpha, float emissive,
    std::uint32_t seed, FxPart part = FxPart::Debris);

//! 対象から `to` へ向かう粒の流れ（吸収・盗み・魂）。`flight` 秒で届く。
struct StreamParams {
    int count{ 30 };
    float spread{ 0.45f };   //!< 出る時刻のばらつき
    float flight{ 0.5f };
    float size{ 0.09f };
    float emissive{ 2.2f };
    float bend{ 0.5f };
    FxColour c0{ 1.f, 1.f, 1.f };
    FxColour c1{ 1.f, 0.5f, 0.5f };
    float born{ 0.f };
    std::uint32_t salt{ 0 };
};
void stream_to(const Look &L, FxFrame &out, const Vec3 &from_centre, const Vec3 &to, const StreamParams &p);

//! 画面に正対した放射状の光線（星・後光）。`n` 本。長さ `len`、内側の半径 `r0`。
void radial_rays(const Look &L, FxFrame &out, const Vec3 &centre, int n, float r0, float len, float w, const FxColour &c, float emissive, float alpha,
    float spin = 0.f);

//! 破片の吹き出し（岩・陶器・氷のかけら）。床で止まる。
void debris_burst(const Look &L, FxFrame &out, const Vec3 &origin, int count, float born, float up_speed, float spread, float size, const FxColour &c0,
    const FxColour &c1, float emissive, std::uint32_t salt = 0, float gravity = -9.f);

//! 頭上の高さ（その実体の頭の少し上）。
inline float head_z(const Look &L)
{
    return (L.mini ? 0.85f : 1.1f) * (0.5f + (0.5f * L.scale));
}

} // namespace looks
} // namespace hd2d
