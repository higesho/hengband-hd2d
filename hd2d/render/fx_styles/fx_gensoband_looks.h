/*!
 * @file fx_gensoband_looks.h
 * @brief 幻想蛮怒の見た目（`fx_gensoband.cpp` の表）が結ぶ関数の宣言と、2 つの実装ファイル（`fx_gensoband_a.cpp`・`fx_gensoband_b.cpp`）の共通の道具。
 *
 * 名前は `look_`（見た目 1 つ）・`flight_`（玉が飛ぶ間）・`beam_`（ビームの絵）・`all_`（視界内の全員）の後ろに属性の名前を付ける。
 */
#pragma once

#include "render/fx_styles/fx_look_motifs.h"

#include <algorithm>
#include <cmath>

namespace hd2d {
namespace looks {
namespace gb {

/* ------------------------------------------------------------ 共通の道具 */

//! 虹の 7 色（赤・橙・黄・緑・水色・青・紫）。
inline FxColour rainbow_colour(int index)
{
    static const FxColour cols[7] = { { 1.f, 0.15f, 0.12f }, { 1.f, 0.55f, 0.1f }, { 1.f, 0.95f, 0.2f }, { 0.25f, 0.95f, 0.3f }, { 0.2f, 0.9f, 1.f },
        { 0.25f, 0.35f, 1.f }, { 0.7f, 0.25f, 1.f } };
    return cols[((index % 7) + 7) % 7];
}

//! 色相（0〜1 で一周）から色。
inline FxColour hue_colour(float h)
{
    h = h - std::floor(h);
    const float k = h * 6.f;
    const int i = static_cast<int>(k);
    const float f = k - static_cast<float>(i);
    switch (i % 6) {
    case 0:
        return FxColour{ 1.f, f, 0.f };
    case 1:
        return FxColour{ 1.f - f, 1.f, 0.f };
    case 2:
        return FxColour{ 0.f, 1.f, f };
    case 3:
        return FxColour{ 0.f, 1.f - f, 1.f };
    case 4:
        return FxColour{ f, 0.f, 1.f };
    default:
        return FxColour{ 1.f, 0.f, 1.f - f };
    }
}

//! 向きのある直方体（水平の向き `dir` に長さ `len`、横 `wid`、高さ `hei`）。
inline void box(FxFrame &out, const Vec3 &centre, const Vec3 &dir, float len, float wid, float hei, const FxColour &c, float emissive, float alpha,
    FxPart part = FxPart::Shape)
{
    if (alpha <= 0.004f) {
        return;
    }
    FxVoxel v;
    v.center = centre;
    v.size = Vec3{ len, wid, hei };
    v.r = c.r;
    v.g = c.g;
    v.b = c.b;
    v.emissive = emissive;
    v.alpha = std::min(alpha, 1.f);
    fx_direction_angles(Vec3{ dir.x, dir.y, 0.f }, v.yaw, v.pitch);
    v.pitch = 0.f;
    v.part = part;
    out.voxels.push_back(v);
}

//! 玉のまわりを小さな粒が回る（飛んでいる姿）。
inline void satellites(FxFrame &out, const Vec3 &pos, const Vec3 &dir, int n, float radius, float speed, float t, float size, const FxColour &c, float emissive,
    FxPart part = FxPart::OrbCore)
{
    const Vec3 side = unit_or(cross(dir, Vec3{ 0.f, 0.f, 1.f }), Vec3{ 0.f, 1.f, 0.f });
    const Vec3 up = unit_or(cross(side, dir), Vec3{ 0.f, 0.f, 1.f });
    for (int i = 0; i < n; ++i) {
        const float ang = (t * speed) + (2.f * kPi * static_cast<float>(i) / static_cast<float>(n));
        put(out, pos + (side * (std::cos(ang) * radius)) + (up * (std::sin(ang) * radius * 0.8f)), size, c, emissive, 0.95f, part);
    }
}

/* ------------------------------------------------------------ 見た目（`fx_gensoband_a.cpp`） */

#define GB_DECLARE_LOOK(name) void look_##name(const Look &L, FxFrame &out);
#define GB_DECLARE_FLIGHT(name) void flight_##name(const FxStyleCtx &ctx, const Entry &e, FxFrame &out);
#define GB_DECLARE_BEAM(name) void beam_##name(const FxStyleCtx &ctx, const Entry &e, float t_end, FxFrame &out);
#define GB_DECLARE_ALL(name) void all_##name(const FxStyleCtx &ctx, const Entry &e, FxFrame &out);

GB_DECLARE_LOOK(tsukumo_storm)
GB_DECLARE_ALL(tsukumo_storm)
GB_DECLARE_LOOK(yukari_storm)
GB_DECLARE_ALL(yukari_storm)
GB_DECLARE_LOOK(kinkakuji)
GB_DECLARE_ALL(kinkakuji)
GB_DECLARE_LOOK(tornado)
GB_DECLARE_FLIGHT(tornado)
GB_DECLARE_LOOK(windcutter)
GB_DECLARE_FLIGHT(windcutter)
GB_DECLARE_LOOK(banki_beam)
GB_DECLARE_LOOK(banki_beam2)
GB_DECLARE_FLIGHT(banki_beam)
GB_DECLARE_BEAM(banki_beam)
GB_DECLARE_BEAM(banki_beam2)
GB_DECLARE_LOOK(seiran_beam)
GB_DECLARE_FLIGHT(seiran_beam)
GB_DECLARE_BEAM(seiran_beam)
GB_DECLARE_LOOK(rainbow)
GB_DECLARE_FLIGHT(rainbow)
GB_DECLARE_BEAM(rainbow)
GB_DECLARE_ALL(rainbow)
GB_DECLARE_LOOK(lunatic_torch)
GB_DECLARE_FLIGHT(lunatic_torch)
GB_DECLARE_LOOK(redeye)
GB_DECLARE_LOOK(kyuttoshitedoka_n)
GB_DECLARE_FLIGHT(kyuttoshitedoka_n)
GB_DECLARE_LOOK(ryuuri)
GB_DECLARE_BEAM(ryuuri)
GB_DECLARE_LOOK(slow_twine)
GB_DECLARE_LOOK(possession)
GB_DECLARE_LOOK(soulsteal)
GB_DECLARE_LOOK(nightmare)
GB_DECLARE_LOOK(train)
GB_DECLARE_FLIGHT(train)
GB_DECLARE_BEAM(train)
GB_DECLARE_LOOK(northern)
GB_DECLARE_LOOK(n_e_p)
GB_DECLARE_ALL(n_e_p)
GB_DECLARE_LOOK(houtou)
GB_DECLARE_BEAM(houtou)

/* ------------------------------------------------------------ 見た目（`fx_gensoband_b.cpp`） */

GB_DECLARE_LOOK(satori)
GB_DECLARE_LOOK(kokoro)
GB_DECLARE_LOOK(moses)
GB_DECLARE_BEAM(moses)
GB_DECLARE_LOOK(redmagic)
GB_DECLARE_LOOK(gungnir)
GB_DECLARE_FLIGHT(gungnir)
GB_DECLARE_LOOK(throw_plate)
GB_DECLARE_FLIGHT(throw_plate)
GB_DECLARE_LOOK(blazingstar)
GB_DECLARE_FLIGHT(blazingstar)
GB_DECLARE_LOOK(soulsculpture)
GB_DECLARE_LOOK(yaku)
GB_DECLARE_LOOK(special_shot)
GB_DECLARE_FLIGHT(special_shot)
GB_DECLARE_BEAM(special_shot)
GB_DECLARE_LOOK(youmu)
GB_DECLARE_LOOK(brain_finger_print)
GB_DECLARE_LOOK(kanameishi)
GB_DECLARE_LOOK(steam)
GB_DECLARE_FLIGHT(steam)
GB_DECLARE_LOOK(super_ego)
GB_DECLARE_LOOK(dig_oil)
GB_DECLARE_LOOK(alcohol)
GB_DECLARE_LOOK(pit_fall)
GB_DECLARE_LOOK(berserk)
GB_DECLARE_LOOK(activ_trap)

#undef GB_DECLARE_LOOK
#undef GB_DECLARE_FLIGHT
#undef GB_DECLARE_BEAM
#undef GB_DECLARE_ALL

} // namespace gb
} // namespace looks
} // namespace hd2d
