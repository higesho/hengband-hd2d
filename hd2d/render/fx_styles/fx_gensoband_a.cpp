/*!
 * @file fx_gensoband_a.cpp
 * @brief 幻想蛮怒の見た目（前半）：嵐・風・ビーム・炎・魂・列車・宝塔（`gb_tsukumo_storm` から `gb_houtou` まで 21 個）。
 *
 * 表と見た目の説明は `fx_gensoband.cpp`。原作は `gensoband/src/defines.h` の `GF_xxx`、色は `gensoband/lib/pref/spell-xx.prf`、
 * 技の元ネタは東方 Project のスペルカード・弾幕。
 */
#include "render/fx_styles/fx_gensoband_looks.h"

#include <algorithm>
#include <cmath>

namespace hd2d {
namespace looks {
namespace gb {

namespace {

//! 飛んでいる玉の尾（小さな粒を、少し前の位置へ並べる）。
void flight_trail(const FxStyleCtx &ctx, FxFrame &out, const FxColour &c, float size, float emissive, int n = 8)
{
    const float fly = std::max(ctx.fly, 1e-4f);
    for (int k = 1; k <= n; ++k) {
        const float tt = ctx.t - (0.02f * static_cast<float>(k));
        if (tt <= 0.f) {
            break;
        }
        const Vec3 p = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(tt / fly));
        const float fr = 1.f - (static_cast<float>(k) / static_cast<float>(n + 1));
        put(out, p, size * fr, c, emissive * fr, 0.6f * fr, FxPart::OrbTail);
    }
}

//! 玉の中心（小さな光る玉）。
void flight_core(FxFrame &out, const Vec3 &pos, const FxColour &c, float size, float emissive)
{
    put(out, pos, size, fx_mix(FxColour{ 1.f, 1.f, 1.f }, c, 0.45f), emissive * 1.5f, 1.f, FxPart::OrbCore);
}

//! 文字の格子を置くための見た目（対象のマスではなく、任意の点に置くとき）。
Look any_look(const FxStyleCtx &ctx, const Entry &e)
{
    return make_look(ctx.spec, e, ctx.spec.centre_gx, ctx.spec.centre_gy, ctx.t, 1.f, false, ctx.spec.seed);
}

//! 画面の上の向き `dir`（世界座標）から、文字の格子の回転（ラジアン）。格子の上が進む向きになる。
float screen_angle(const FxSpec &spec, const Vec3 &dir)
{
    return std::atan2(dot(dir, spec.cam_up), dot(dir, spec.cam_right)) - (0.5f * kPi);
}

/* ============================================================ 道具の形（付喪神） */

const FxShape &tool_shape(int i)
{
    return (i % 3 == 0) ? shape_umbrella() : ((i % 3 == 1) ? shape_drum() : shape_note());
}

void tool_glyph(const Look &L, FxFrame &out, int i, const Vec3 &pos, float cell, float alpha, float rot)
{
    static const FxColour base[3] = { { 0.65f, 0.3f, 0.95f }, { 0.9f, 0.45f, 0.15f }, { 0.4f, 0.95f, 0.75f } };
    glyph(L, out, tool_shape(i), pos, cell, alpha, base[i % 3], 1.5f, 2, rot, "o", { FxColour{ 1.f, 0.85f, 0.6f } });
}

} // namespace

/* ============================================================ gb_tsukumo_storm */

void look_tsukumo_storm(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float env = fx_envelope(te, 0.1f, 0.85f, 0.4f);
    WhirlParams w;
    w.height = 1.3f;
    w.radius0 = 0.15f;
    w.radius1 = 0.45f;
    w.speed = 8.f;
    w.count = 44;
    w.life = 1.2f;
    w.rise = 0.15f;
    w.c0 = FxColour{ 0.95f, 0.8f, 1.f };
    w.c1 = FxColour{ 0.4f, 0.8f, 1.f };
    w.emissive = 2.2f;
    w.size = 0.06f;
    whirl(L, out, w);
    const int n = L.mini ? 2 : 4;
    for (int i = 0; i < n; ++i) {
        const float ang = (te * 5.f) + (2.f * kPi * static_cast<float>(i) / static_cast<float>(n));
        const float r = 0.42f * L.scale;
        const float z = (0.55f + (0.3f * std::sin((te * 3.f) + static_cast<float>(i)))) * L.scale;
        tool_glyph(L, out, i, Vec3{ L.foot.x + (std::cos(ang) * r), L.foot.y + (std::sin(ang) * r), z }, 0.03f * L.scale, env, 0.4f * std::sin((te * 4.f) + static_cast<float>(i)));
    }
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.8f }, 3.4f, L.e.base, 1.2f * env);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.02f * env);
        mod.tremble_phase = te * 70.f;
    }
}

void all_tsukumo_storm(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const float t = ctx.t;
    const float u = t / 0.95f;
    static const FxColour cols[4] = { { 1.f, 1.f, 0.4f }, { 0.3f, 0.6f, 1.f }, { 0.4f, 1.f, 0.5f }, { 1.f, 0.6f, 0.2f } };
    static const float ang[4] = { 0.f, 0.25f, -0.25f, 0.1f };
    if ((u >= 0.f) && (u <= 1.5f)) {
        const float env = std::sin(kPi * fx_clamp01(u / 1.5f));
        for (int i = 0; i < 4; ++i) {
            fx_add_screen_wave(out, cols[i], (u * 1.3f) - 0.15f - (0.13f * static_cast<float>(i)), 0.16f, 0.36f * env, ang[i]);
        }
    }
    // 付喪神の道具が、撃った者のまわりを回る。
    const Look L = make_look(ctx.spec, e, ctx.spec.src_gx, ctx.spec.src_gy, t, 1.f, false, ctx.spec.seed);
    const float al = fx_envelope(t, 0.15f, 0.6f, 0.35f);
    for (int i = 0; i < 6; ++i) {
        const float a = (t * 4.f) + (2.f * kPi * static_cast<float>(i) / 6.f);
        const float r = 1.3f + (0.3f * std::sin((t * 2.f) + static_cast<float>(i)));
        tool_glyph(L, out, i, Vec3{ L.foot.x + (std::cos(a) * r), L.foot.y + (std::sin(a) * r), 0.75f + (0.25f * std::sin((t * 3.f) + static_cast<float>(i))) }, 0.045f, al,
            0.4f * std::sin((t * 4.f) + static_cast<float>(i)));
    }
}

/* ============================================================ gb_yukari_storm */

void look_yukari_storm(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float open = fx_ramp(te, 0.f, 0.28f);
    const float a = open * (1.f - fx_ramp(te, 0.95f, 1.25f));
    if (a > 0.01f) {
        const Vec3 c{ L.front.x, L.front.y, (0.62f * L.scale) + 0.1f };
        const float cell = 0.05f * L.scale * (0.25f + (0.75f * ease_out(open)));
        if (!L.mini) {
            glyph(L, out, shape_gap(), c, cell, a, L.e.tint, 1.6f, 2, 0.f, "oe", { FxColour{ 0.05f, 0.f, 0.09f }, FxColour{ 1.f, 0.9f, 0.25f } });
        }
        // 縁のリボンのような赤い火花と、白と紫の市松のきらめき。
        for (int i = 0; i < count_of(L, 16); ++i) {
            const float ang = (2.f * kPi * static_cast<float>(i) / 16.f) + (te * 1.5f);
            const FxColour col = (i % 2 == 0) ? FxColour{ 1.f, 1.f, 1.f } : L.e.tint2;
            put(out, c + on_screen(L.spec, std::cos(ang) * 0.22f * L.scale, std::sin(ang) * 0.42f * L.scale), 0.04f, col, 2.4f, a * (0.5f + (0.5f * std::sin((te * 18.f) + static_cast<float>(i)))));
        }
        ring_at(L, out, false, c, 0.12f, 0.62f * L.scale, 0.1f, 0.5f, 0.03f, L.e.tint, 1.8f, 0.8f, 26);
        if (!L.mini) {
            add_light(out, c, 3.4f, L.e.tint, 1.3f * a);
            FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
            mod.tremble = std::max(mod.tremble, 0.015f * a);
            mod.tremble_phase = te * 60.f;
        }
    }
}

void all_yukari_storm(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const float t = ctx.t;
    const float u = t / 0.95f;
    if ((u >= 0.f) && (u <= 1.5f)) {
        const float env = std::sin(kPi * fx_clamp01(u / 1.5f));
        for (int i = 0; i < 6; ++i) {
            const FxColour c = (i % 2 == 0) ? FxColour{ 1.f, 1.f, 1.f } : e.tint2;
            fx_add_screen_wave(out, c, (u * 1.3f) - 0.15f - (0.1f * static_cast<float>(i)), 0.08f, 0.34f * env, (i % 2 == 0) ? 0.7f : -0.7f);
        }
    }
    // 撃った者の前にスキマが開く。
    const Look L = make_look(ctx.spec, e, ctx.spec.src_gx, ctx.spec.src_gy, t, 1.f, false, ctx.spec.seed);
    look_yukari_storm(L, out);
}

/* ============================================================ gb_kinkakuji */

void look_kinkakuji(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float fall = 0.38f;
    const float alpha = 1.f - fx_ramp(te, 1.0f, 1.4f);
    const float z = (te < fall) ? fx_lerp(2.6f, 0.95f, (te / fall) * (te / fall)) : 0.95f;
    glyph(L, out, shape_roof(), Vec3{ L.front.x, L.front.y, z * L.scale }, 0.062f * L.scale, alpha, L.e.base, 1.1f, 2, 0.f);
    if (te >= fall) {
        const float tb = te - fall;
        ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.7f * L.scale, fall, 0.4f, 0.05f, L.e.tint, 2.4f, 0.9f, 24);
        FxSpray sp;
        sp.origin = Vec3{ L.front.x, L.front.y, 0.5f * L.scale };
        sp.cone = kPi;
        sp.speed_min = 0.8f;
        sp.speed_max = 2.2f;
        sp.gravity = -3.f;
        sp.drag = 1.5f;
        sp.born = fall;
        sp.life_min = 0.3f;
        sp.life_max = 0.6f;
        sp.count = count_of(L, 30);
        sp.colour0 = FxColour{ 1.f, 0.95f, 0.6f };
        sp.colour1 = L.e.base;
        sp.size0 = 0.06f;
        sp.size1 = 0.02f;
        sp.emissive0 = 3.f;
        sp.emissive1 = 0.8f;
        sp.seed = L.seed;
        (void)fx_emit_spray(sp, te, out);
        shake_for(L, out, fall, 0.4f, 0.1f);
        if (!L.mini) {
            FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
            mod.squash = std::max(mod.squash, 0.35f * fx_envelope(tb, 0.04f, 0.45f, 0.35f));
            add_light(out, Vec3{ L.foot.x, L.foot.y, 0.9f }, 4.f, L.e.base, 2.0f * fx_envelope(tb, 0.03f, 0.2f, 0.6f));
        }
    }
}

void all_kinkakuji(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const float t = ctx.t;
    const int sx = ctx.spec.src_gx;
    const int sy = ctx.spec.src_gy;
    // 撃った者を中心にした 7 マス四方が、金色に光ってほどける（原作の「中心の 7 マス四方」）。
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            const int d = std::max(std::abs(dx), std::abs(dy));
            const float age = t - (0.07f * static_cast<float>(d));
            if ((age < 0.f) || (age > 0.95f)) {
                continue;
            }
            const float env = fx_envelope(age, 0.08f, 0.45f, 0.4f);
            FxVoxel v;
            v.center = fx_cell_centre(sx + dx, sy + dy, 0.03f);
            v.size = Vec3{ 0.9f, 0.9f, 0.03f };
            const float shade = (((dx + dy) & 1) != 0) ? 1.f : 0.82f;
            v.r = e.base.r * shade;
            v.g = e.base.g * shade;
            v.b = e.base.b * shade;
            v.emissive = 1.5f;
            v.alpha = 0.55f * env;
            v.part = FxPart::Puddle;
            out.voxels.push_back(v);
            if (age > 0.3f) {
                const std::uint32_t id = static_cast<std::uint32_t>(((dy + 3) * 7) + (dx + 3));
                const float k = age - 0.3f;
                put(out, Vec3{ static_cast<float>(sx + dx) + 0.2f + (0.6f * fx_rand(ctx.spec.seed, id, 1u)), static_cast<float>(sy + dy) + 0.2f + (0.6f * fx_rand(ctx.spec.seed, id, 2u)),
                              0.1f + (1.2f * k) }, 0.05f, e.tint, 2.6f, 1.f - fx_ramp(k, 0.3f, 0.65f), FxPart::Spark);
            }
        }
    }
    // 屋根が降りてくる。
    const Look L = make_look(ctx.spec, e, sx, sy, t, 1.f, false, ctx.spec.seed);
    const float z = fx_lerp(3.6f, 2.3f, ease_out(t / 0.6f));
    glyph(L, out, shape_roof(), Vec3{ L.foot.x, L.foot.y, z }, 0.14f, fx_envelope(t, 0.1f, 0.55f, 0.4f), e.base, 1.7f, 2, 0.f);
    add_light(out, Vec3{ L.foot.x, L.foot.y, 1.2f }, 6.f, e.base, 1.6f * fx_envelope(t, 0.1f, 0.5f, 0.5f));
    fx_emit_all_sight_wave(ctx, out, e.tint, 0.9f);
}

/* ============================================================ gb_tornado */

void look_tornado(const Look &L, FxFrame &out)
{
    const float te = L.te;
    WhirlParams w;
    w.height = 1.5f;
    w.radius0 = 0.05f;
    w.radius1 = 0.52f;
    w.speed = 12.f;
    w.count = 100;
    w.life = 1.3f;
    w.rise = 0.2f;
    w.duration = 0.5f;
    w.z0 = 0.02f;
    w.c0 = FxColour{ 0.88f, 1.f, 0.88f };
    w.c1 = L.e.base;
    w.emissive = 2.2f;
    w.size = 0.065f;
    whirl(L, out, w);
    // 巻き上げられる木の葉。
    for (int i = 0; i < count_of(L, 12); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float born = 0.4f * fx_rand(L.seed, id, 1u);
        const float age = te - born;
        if ((age < 0.f) || (age >= 1.1f)) {
            continue;
        }
        const float f = fx_rand(L.seed, id, 2u);
        const float ang = (2.f * kPi * fx_rand(L.seed, id, 3u)) + (age * 9.f);
        const float r = (0.15f + (0.4f * f)) * L.scale;
        FxVoxel v;
        v.center = Vec3{ L.foot.x + (std::cos(ang) * r), L.foot.y + (std::sin(ang) * r), (0.1f + (1.2f * f) + (0.2f * age)) * L.scale };
        v.size = Vec3{ 0.11f, 0.06f, 0.015f };
        v.r = 0.35f;
        v.g = 0.85f;
        v.b = 0.3f;
        v.emissive = 0.9f;
        v.alpha = 1.f - fx_ramp(age, 0.8f, 1.1f);
        v.yaw = ang + (age * 6.f);
        v.pitch = 0.7f * std::sin((age * 8.f) + static_cast<float>(i));
        v.part = FxPart::Debris;
        out.voxels.push_back(v);
    }
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.5f * L.scale, 0.5f * L.scale, 0.f, 1.3f, 0.03f, L.e.tint, 1.4f, 0.6f * fx_envelope(te, 0.1f, 0.8f, 0.4f), 22, 3, te * 8.f);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.8f }, 3.4f, L.e.base, 1.1f * fx_envelope(te, 0.15f, 0.7f, 0.4f));
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.02f * fx_envelope(te, 0.1f, 0.7f, 0.3f));
        mod.tremble_phase = te * 70.f;
    }
}

void flight_tornado(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    flight_core(out, f.pos, e.base, 0.13f, 2.2f);
    satellites(out, f.pos, f.dir, 6, 0.2f, 22.f, ctx.t, 0.06f, FxColour{ 0.85f, 1.f, 0.85f }, 2.4f);
    flight_trail(ctx, out, e.base, 0.09f, 1.6f);
    add_light(out, f.pos, 2.8f, e.base, 1.3f);
    mark_projectile(out, f.pos, f.dir);
}

/* ============================================================ gb_windcutter */

void look_windcutter(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const Vec3 c{ L.front.x, L.front.y, 0.55f * L.scale };
    static const float theta[3] = { 0.55f, -0.45f, 0.1f };
    for (int k = 0; k < (L.mini ? 1 : 3); ++k) {
        const float born = 0.08f * static_cast<float>(k);
        const float life = 0.3f;
        const float age = te - born;
        const Vec3 d = on_screen(L.spec, std::cos(theta[k]), std::sin(theta[k]));
        if ((age >= 0.f) && (age < life)) {
            const float u = age / life;
            for (int tr = (L.mini ? 0 : 2); tr >= 0; --tr) {
                const float uu = u - (0.06f * static_cast<float>(tr));
                if (uu < 0.f) {
                    continue;
                }
                const Vec3 p = c + (d * ((-0.55f + (1.1f * uu)) * L.scale));
                glyph(L, out, shape_crescent(), p, 0.04f * L.scale * (1.f - (0.08f * static_cast<float>(tr))), (tr == 0) ? 1.f : (0.5f / static_cast<float>(tr)),
                    (tr == 0) ? L.e.base : fx_scale(L.e.base, 0.8f), 1.3f, ((tr == 0) && !L.mini) ? 2 : 1, theta[k] + kPi, "o", { L.e.tint });
            }
        }
        // 通った切り口。
        if (age > 0.1f) {
            const float u = fx_clamp01((age - 0.1f) / 0.3f);
            put_bar(out, c, d, 1.15f * L.scale, 0.03f * L.scale, fx_mix(FxColour{ 1.f, 1.f, 1.f }, L.e.base, 0.4f), 3.f, 1.f - u, FxPart::Spark);
        }
    }
    // 切り刻まれた木の葉と風。
    FxSpray sp;
    sp.origin = c;
    sp.cone = kPi;
    sp.speed_min = 0.8f;
    sp.speed_max = 2.f;
    sp.gravity = -1.5f;
    sp.drag = 1.8f;
    sp.born = 0.1f;
    sp.life_min = 0.3f;
    sp.life_max = 0.6f;
    sp.count = count_of(L, 16);
    sp.colour0 = L.e.tint;
    sp.colour1 = L.e.base;
    sp.size0 = 0.05f;
    sp.size1 = 0.02f;
    sp.emissive0 = 2.4f;
    sp.emissive1 = 0.6f;
    sp.seed = L.seed;
    (void)fx_emit_spray(sp, te, out);
    if (!L.mini) {
        add_light(out, c, 3.f, L.e.base, 1.4f * fx_envelope(te, 0.04f, 0.2f, 0.4f));
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        float ax = 0.f;
        float ay = 1.f;
        facing_away(L, ax, ay);
        mod.recoil = std::max(mod.recoil, 0.35f * fx_envelope(te, 0.05f, 0.15f, 0.3f));
        mod.away_x = ax;
        mod.away_y = ay;
    }
}

void flight_windcutter(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    const Look L = any_look(ctx, e);
    glyph(L, out, shape_crescent(), f.pos, 0.032f, 1.f, e.base, 1.4f, 2, ctx.t * 28.f, "o", { e.tint });
    flight_trail(ctx, out, e.base, 0.07f, 1.8f);
    add_light(out, f.pos, 2.6f, e.base, 1.2f);
    mark_projectile(out, f.pos, f.dir);
}

/* ============================================================ gb_banki_beam・gb_banki_beam2 */

namespace {

void head_glyph(const Look &L, FxFrame &out, const Vec3 &pos, float cell, float alpha, float rot = 0.f)
{
    glyph(L, out, shape_head(), pos, cell, alpha, FxColour{ 1.f, 1.f, 1.f }, 1.0f, 2, rot, "rwe", { FxColour{ 0.9f, 0.1f, 0.12f }, FxColour{ 1.f, 0.9f, 0.8f }, FxColour{ 0.1f, 0.05f, 0.05f } });
}

//! 首を伸ばして飛んでいく頭と、その首（`count` は並んで伸びる頭の数）。
void banki_necks(const FxStyleCtx &ctx, const Entry &e, float t_end, FxFrame &out, int count)
{
    Vec3 from;
    Vec3 to;
    beam_ends(ctx.spec, from, to);
    const float travel = std::max(0.25f, t_end + 0.1f);
    const float al = 1.f - fx_ramp(ctx.t, travel + 0.15f, travel + 0.4f);
    if (al <= 0.01f) {
        return;
    }
    const Look L = any_look(ctx, e);
    const Vec3 d = to - from;
    const Vec3 side = unit_or(cross(d, Vec3{ 0.f, 0.f, 1.f }), Vec3{ 0.f, 1.f, 0.f });
    for (int k = 0; k < count; ++k) {
        const float off = (count == 1) ? 0.f : ((k == 0) ? -0.28f : 0.28f);
        const float delay = 0.06f * static_cast<float>(k);
        const float u = ease_out(fx_clamp01((ctx.t - delay) / travel));
        const Vec3 a = Vec3{ from.x, from.y, 0.5f } + (side * (off * 0.4f));
        const Vec3 b = Vec3{ from.x + (d.x * u), from.y + (d.y * u), 0.58f } + (side * off * (0.4f + (0.6f * u)));
        const float wob = 0.03f * std::sin((ctx.t * 24.f) + static_cast<float>(k));
        beam_bar(out, a, b + Vec3{ 0.f, 0.f, wob }, 0.16f, FxColour{ 1.f, 0.9f, 0.45f }, 1.0f, 0.3f * al, 1.f, FxPart::BeamGlow);
        beam_bar(out, a, b + Vec3{ 0.f, 0.f, wob }, 0.065f, FxColour{ 1.f, 0.97f, 0.8f }, 2.0f, 0.95f * al, 1.f, FxPart::BeamCore);
        // 首の赤いリボンの目印。
        const Vec3 nd = b - a;
        const float nl = std::sqrt(dot(nd, nd));
        for (int j = 1; (static_cast<float>(j) * 0.5f) < nl; ++j) {
            put(out, a + (nd * (static_cast<float>(j) * 0.5f / std::max(nl, 1e-3f))), 0.09f, FxColour{ 0.95f, 0.1f, 0.15f }, 1.4f, 0.9f * al);
        }
        head_glyph(L, out, b, 0.032f, al, 0.f);
        if (count > 1) {
            // 頭のあとを追う小さな弾。
            for (int j = 1; j <= 5; ++j) {
                const float uu = std::max(0.f, u - (0.05f * static_cast<float>(j)));
                put(out, Vec3{ from.x + (d.x * uu), from.y + (d.y * uu), 0.58f } + (side * off * (0.4f + (0.6f * uu))) + Vec3{ 0.f, 0.f, 0.14f }, 0.06f, FxColour{ 1.f, 0.4f, 0.3f }, 2.4f,
                    0.8f * al * (1.f - (0.15f * static_cast<float>(j))));
            }
        }
    }
}

void banki_bite(const Look &L, FxFrame &out, int heads)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.04f, 0.4f, 0.4f);
    const float pulse = 1.f + (0.12f * std::sin(te * 26.f));
    for (int k = 0; k < (L.mini ? 1 : heads); ++k) {
        const float off = (heads == 1) ? 0.f : ((k == 0) ? -0.3f : 0.3f);
        head_glyph(L, out, Vec3{ L.front.x, L.front.y, 0.62f * L.scale } + on_screen(L.spec, off * L.scale, 0.f), 0.05f * L.scale * pulse, alpha, 0.f);
    }
    FxSpray sp;
    sp.origin = Vec3{ L.front.x, L.front.y, 0.6f * L.scale };
    sp.cone = kPi;
    sp.speed_min = 0.8f;
    sp.speed_max = 2.f;
    sp.gravity = -2.f;
    sp.drag = 2.f;
    sp.born = 0.f;
    sp.life_min = 0.25f;
    sp.life_max = 0.5f;
    sp.count = count_of(L, 18);
    sp.colour0 = FxColour{ 1.f, 0.6f, 0.4f };
    sp.colour1 = FxColour{ 0.9f, 0.1f, 0.12f };
    sp.size0 = 0.05f;
    sp.size1 = 0.015f;
    sp.emissive0 = 2.6f;
    sp.emissive1 = 0.6f;
    sp.seed = L.seed;
    (void)fx_emit_spray(sp, te, out);
    ring_at(L, out, false, Vec3{ L.front.x, L.front.y, 0.62f * L.scale }, 0.1f, 0.7f * L.scale, 0.f, 0.4f, 0.035f, FxColour{ 1.f, 0.9f, 0.5f }, 2.2f, 0.9f, 24);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.7f }, 3.2f, FxColour{ 1.f, 0.8f, 0.4f }, 1.5f * alpha);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        float ax = 0.f;
        float ay = 1.f;
        facing_away(L, ax, ay);
        mod.recoil = std::max(mod.recoil, 0.3f * fx_envelope(te, 0.04f, 0.1f, 0.3f));
        mod.away_x = ax;
        mod.away_y = ay;
    }
}

} // namespace

void look_banki_beam(const Look &L, FxFrame &out)
{
    banki_bite(L, out, 1);
}

void look_banki_beam2(const Look &L, FxFrame &out)
{
    banki_bite(L, out, 2);
}

void flight_banki_beam(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    const Look L = any_look(ctx, e);
    const Vec3 src = fx_source_centre(ctx.spec, 0.5f);
    beam_bar(out, src, f.pos, 0.16f, FxColour{ 1.f, 0.9f, 0.45f }, 1.0f, 0.3f, 1.f, FxPart::BeamGlow);
    beam_bar(out, src, f.pos, 0.065f, FxColour{ 1.f, 0.97f, 0.8f }, 2.0f, 0.95f, 1.f, FxPart::BeamCore);
    head_glyph(L, out, f.pos, 0.03f, 1.f, 0.f);
    add_light(out, f.pos, 2.6f, FxColour{ 1.f, 0.85f, 0.5f }, 1.1f);
    mark_projectile(out, f.pos, f.dir);
}

void beam_banki_beam(const FxStyleCtx &ctx, const Entry &e, float t_end, FxFrame &out)
{
    banki_necks(ctx, e, t_end, out, 1);
}

void beam_banki_beam2(const FxStyleCtx &ctx, const Entry &e, float t_end, FxFrame &out)
{
    banki_necks(ctx, e, t_end, out, 2);
}

/* ============================================================ gb_seiran_beam */

void look_seiran_beam(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const Vec3 c{ L.front.x, L.front.y, 0.55f * L.scale };
    // 射抜かれた閃光と、舞う羽。
    if (te < 0.3f) {
        const float u = te / 0.3f;
        put(out, c, ((0.3f * (1.f - u)) + 0.05f) * L.scale, FxColour{ 0.9f, 0.97f, 1.f }, 3.4f, 1.f - u, FxPart::BurstFlash);
        radial_rays(L, out, c, L.mini ? 4 : 8, 0.1f, 0.5f * (1.f - u) * L.scale, 0.035f, L.e.tint, 3.f, 1.f - u, 0.2f);
    }
    for (int i = 0; i < count_of(L, 14); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float age = te - (0.02f * fx_rand(L.seed, id, 1u));
        if ((age < 0.f) || (age >= 0.9f)) {
            continue;
        }
        const float a = 2.f * kPi * fx_rand(L.seed, id, 2u);
        const float sp = 0.6f + (1.2f * fx_rand(L.seed, id, 3u));
        const Vec3 p{ c.x + (std::cos(a) * sp * 0.4f * (1.f - std::exp(-3.f * age))), c.y + (std::sin(a) * sp * 0.4f * (1.f - std::exp(-3.f * age))),
            std::max(0.03f, c.z + (0.5f * age) - (1.1f * age * age)) };
        FxVoxel v;
        v.center = p;
        v.size = Vec3{ 0.12f, 0.03f, 0.02f };
        v.r = 0.7f;
        v.g = 0.88f;
        v.b = 1.f;
        v.emissive = 1.8f;
        v.alpha = 1.f - fx_ramp(age, 0.55f, 0.9f);
        v.yaw = (5.f * age) + (6.f * fx_rand(L.seed, id, 4u));
        v.part = FxPart::Debris;
        out.voxels.push_back(v);
    }
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.6f * L.scale, 0.05f, 0.4f, 0.04f, L.e.base, 2.2f, 0.85f, 22);
    if (!L.mini) {
        add_light(out, c, 3.4f, L.e.base, 1.8f * fx_envelope(te, 0.03f, 0.12f, 0.4f));
    }
}

void flight_seiran_beam(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    const Look L = any_look(ctx, e);
    glyph(L, out, shape_bird(), f.pos, 0.036f, 1.f, FxColour{ 0.55f, 0.75f, 1.f }, 1.2f, 2, screen_angle(ctx.spec, f.dir), "", {});
    flight_trail(ctx, out, e.base, 0.08f, 2.f);
    add_light(out, f.pos, 2.6f, e.base, 1.3f);
    mark_projectile(out, f.pos, f.dir);
}

void beam_seiran_beam(const FxStyleCtx &ctx, const Entry &e, float t_end, FxFrame &out)
{
    Vec3 from;
    Vec3 to;
    beam_ends(ctx.spec, from, to);
    const float travel = std::max(0.25f, t_end + 0.1f);
    const float u = ease_out(fx_clamp01(ctx.t / travel));
    const float al = 1.f - fx_ramp(ctx.t, travel + 0.1f, travel + 0.35f);
    if (al <= 0.01f) {
        return;
    }
    const Vec3 hawk = from + ((to - from) * u);
    beam_bar(out, from, hawk, 0.2f, FxColour{ 0.4f, 0.65f, 1.f }, 1.4f, 0.3f * al, 1.f, FxPart::BeamGlow);
    beam_bar(out, from, hawk, 0.1f, FxColour{ 0.55f, 0.8f, 1.f }, 2.4f, 0.9f * al, 1.f, FxPart::BeamCore);
    beam_bar(out, from, hawk, 0.04f, FxColour{ 1.f, 1.f, 1.f }, 4.f, al, 1.f, FxPart::BeamCore);
    const Look L = any_look(ctx, e);
    const Vec3 dir = unit_or(to - from, Vec3{ 1.f, 0.f, 0.f });
    glyph(L, out, shape_bird(), hawk + Vec3{ 0.f, 0.f, 0.05f }, 0.05f, al, FxColour{ 0.55f, 0.75f, 1.f }, 1.2f, 2, screen_angle(ctx.spec, dir), "", {});
}

/* ============================================================ gb_rainbow */

void look_rainbow(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float draw = ease_out(te / 0.4f);
    const float alpha = fx_envelope(te, 0.05f, 0.85f, 0.5f);
    const Vec3 c{ L.front.x, L.front.y, 0.15f };
    for (int k = 0; k < 7; ++k) {
        const float r = (0.62f + (0.075f * static_cast<float>(k))) * L.scale;
        const int steps = std::max(10, static_cast<int>(r * (L.mini ? 14.f : 26.f)));
        for (int j = 0; j <= steps; ++j) {
            const float f = static_cast<float>(j) / static_cast<float>(steps);
            if (f > draw) {
                break;
            }
            const float ang = kPi * (1.f - f);
            put(out, c + on_screen(L.spec, std::cos(ang) * r, std::sin(ang) * r * 0.95f), 0.06f, rainbow_colour(6 - k), 1.8f, alpha, FxPart::Particle);
        }
    }
    // 両端のきらめき。
    for (int s = 0; s < 2; ++s) {
        const float x = ((s == 0) ? -1.f : 1.f) * 0.8f * L.scale;
        for (int i = 0; i < (L.mini ? 1 : 3); ++i) {
            const float ph = (te * 7.f) + (1.7f * static_cast<float>(i)) + static_cast<float>(s);
            glyph(L, out, shape_star4(), c + on_screen(L.spec, x + (0.08f * std::cos(ph)), 0.08f + (0.3f * static_cast<float>(i) * 0.4f)), 0.012f * (0.6f + (0.4f * std::sin(ph))), alpha,
                FxColour{ 1.f, 1.f, 1.f }, 3.f);
        }
    }
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.9f }, 3.6f, FxColour{ 1.f, 0.95f, 0.9f }, 1.2f * alpha);
    }
}

void flight_rainbow(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    (void)e;
    const FlightPose f = flight_pose(ctx);
    put(out, f.pos, 0.14f, FxColour{ 1.f, 1.f, 1.f }, 3.6f, 1.f, FxPart::OrbCore);
    const Vec3 side = unit_or(cross(f.dir, Vec3{ 0.f, 0.f, 1.f }), Vec3{ 0.f, 1.f, 0.f });
    const Vec3 up = unit_or(cross(side, f.dir), Vec3{ 0.f, 0.f, 1.f });
    for (int i = 0; i < 7; ++i) {
        const float ang = (ctx.t * 14.f) + (2.f * kPi * static_cast<float>(i) / 7.f);
        put(out, f.pos + (side * (std::cos(ang) * 0.2f)) + (up * (std::sin(ang) * 0.17f)), 0.08f, rainbow_colour(i), 2.6f, 0.95f, FxPart::OrbCore);
    }
    const float fly = std::max(ctx.fly, 1e-4f);
    for (int k = 1; k <= 7; ++k) {
        const float tt = ctx.t - (0.022f * static_cast<float>(k));
        if (tt <= 0.f) {
            break;
        }
        const Vec3 p = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(tt / fly));
        put(out, p, 0.085f, rainbow_colour(k), 2.f * (1.f - (static_cast<float>(k) / 8.f)), 0.7f * (1.f - (static_cast<float>(k) / 8.f)), FxPart::OrbTail);
    }
    add_light(out, f.pos, 2.8f, FxColour{ 1.f, 0.95f, 0.9f }, 1.3f);
    mark_projectile(out, f.pos, f.dir);
}

void beam_rainbow(const FxStyleCtx &ctx, const Entry &e, float t_end, FxFrame &out)
{
    (void)e;
    Vec3 from;
    Vec3 to;
    beam_ends(ctx.spec, from, to);
    const float vis = fx_ramp(ctx.t, 0.f, t_end + 0.05f);
    const float al = 1.f - fx_ramp(ctx.t, t_end + 0.1f, t_end + 0.5f);
    for (int k = 0; k < 7; ++k) {
        const Vec3 off{ 0.f, 0.f, (static_cast<float>(k) - 3.f) * 0.055f };
        beam_bar(out, from + off, to + off, 0.055f, rainbow_colour(6 - k), 2.2f, 0.92f * al, vis, FxPart::BeamCore);
    }
    beam_bar(out, from, to, 0.3f, FxColour{ 1.f, 1.f, 1.f }, 1.0f, 0.18f * al, vis, FxPart::BeamGlow);
}

void all_rainbow(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    (void)e;
    const float u = ctx.t / 0.95f;
    if ((u >= 0.f) && (u <= 1.5f)) {
        const float env = std::sin(kPi * fx_clamp01(u / 1.5f));
        for (int i = 0; i < 7; ++i) {
            fx_add_screen_wave(out, rainbow_colour(6 - i), (u * 1.3f) - 0.15f - (0.07f * static_cast<float>(i)), 0.09f, 0.36f * env, 0.12f);
        }
    }
}

/* ============================================================ gb_lunatic_torch */

void look_lunatic_torch(const Look &L, FxFrame &out)
{
    const float te = L.te;
    FxSmoke fire;
    fire.origin = Vec3{ L.front.x, L.front.y, 0.1f };
    fire.radius = 0.12f * L.scale;
    fire.born = 0.f;
    fire.duration = 0.55f;
    fire.life = 0.8f;
    fire.count = count_of(L, 28);
    fire.rise = 1.3f;
    fire.size = 0.24f * L.scale;
    fire.grow = 1.3f;
    fire.colour = FxColour{ 1.f, 0.3f, 0.15f };
    fire.colour_end = FxColour{ 0.65f, 0.12f, 0.85f };
    fire.alpha = 0.85f;
    fire.emissive = 2.8f;
    fire.seed = L.seed;
    (void)fx_emit_smoke(fire, te, out);
    FxSpray sp;
    sp.origin = Vec3{ L.front.x, L.front.y, 0.2f };
    sp.dir = Vec3{ 0.f, 0.f, 1.f };
    sp.cone = 0.7f;
    sp.scatter = 0.12f * L.scale;
    sp.speed_min = 1.f;
    sp.speed_max = 2.6f;
    sp.gravity = -1.f;
    sp.born = 0.f;
    sp.duration = 0.5f;
    sp.life_min = 0.3f;
    sp.life_max = 0.6f;
    sp.count = count_of(L, 24);
    sp.colour0 = FxColour{ 1.f, 0.7f, 0.4f };
    sp.colour1 = FxColour{ 0.7f, 0.2f, 0.9f };
    sp.size0 = 0.05f;
    sp.size1 = 0.015f;
    sp.emissive0 = 3.f;
    sp.emissive1 = 0.9f;
    sp.seed = L.seed + 3u;
    (void)fx_emit_spray(sp, te, out);
    // 狂気の星（紅と紫が回る）。
    const float al = fx_envelope(te, 0.1f, 0.6f, 0.4f);
    for (int i = 0; i < (L.mini ? 2 : 5); ++i) {
        const float ang = (te * 6.f) + (2.f * kPi * static_cast<float>(i) / 5.f);
        glyph(L, out, shape_star5(), Vec3{ L.front.x, L.front.y, 0.9f * L.scale } + on_screen(L.spec, std::cos(ang) * 0.4f * L.scale, std::sin(ang) * 0.25f * L.scale), 0.013f * L.scale, al,
            (i % 2 == 0) ? FxColour{ 1.f, 0.25f, 0.2f } : FxColour{ 0.7f, 0.2f, 1.f }, 2.8f);
    }
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.7f }, 3.8f, FxColour{ 1.f, 0.35f, 0.5f }, 2.0f * fx_envelope(te, 0.05f, 0.4f, 0.5f));
    }
}

void flight_lunatic_torch(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    (void)e;
    const FlightPose f = flight_pose(ctx);
    // 回る松明（棒と、その先の炎）。
    const float ang = ctx.t * 22.f;
    const Vec3 bar_dir = normalize(on_screen(ctx.spec, std::cos(ang), std::sin(ang)));
    put_bar(out, f.pos, bar_dir, 0.34f, 0.045f, FxColour{ 0.45f, 0.28f, 0.12f }, 0.4f, 1.f, FxPart::OrbCore);
    const Vec3 head = f.pos + (bar_dir * 0.17f);
    put(out, head, 0.12f, FxColour{ 1.f, 0.85f, 0.5f }, 3.6f, 1.f, FxPart::OrbCore);
    put(out, head + (bar_dir * 0.07f), 0.09f, FxColour{ 1.f, 0.35f, 0.2f }, 3.f, 1.f, FxPart::OrbCore);
    flight_trail(ctx, out, FxColour{ 0.95f, 0.3f, 0.45f }, 0.1f, 2.2f, 10);
    add_light(out, head, 3.f, FxColour{ 1.f, 0.4f, 0.4f }, 1.6f);
    mark_projectile(out, f.pos, f.dir);
}

/* ============================================================ gb_redeye */

void look_redeye(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.1f, 0.85f, 0.4f);
    const int stage = (te < 0.12f) ? 2 : ((te < 0.22f) ? 1 : ((te < 1.0f) ? 0 : 1));
    const Vec3 c{ L.front.x, L.front.y, (1.25f * L.scale) };
    glyph(L, out, shape_eye(stage), c, 0.09f * L.scale, alpha, FxColour{ 0.7f, 0.12f, 0.14f }, 1.0f, 2, 0.f, "oO", { FxColour{ 1.f, 0.92f, 0.88f }, FxColour{ 1.f, 0.f, 0.08f } });
    // 見つめられて広がる赤い波紋。
    for (int k = 0; k < (L.mini ? 1 : 3); ++k) {
        ring_at(L, out, false, c, 0.12f, 0.75f * L.scale, 0.22f + (0.18f * static_cast<float>(k)), 0.5f, 0.035f, FxColour{ 1.f, 0.15f, 0.15f }, 2.2f, 0.85f, 28);
    }
    radial_rays(L, out, c, L.mini ? 6 : 12, 0.4f * L.scale, 0.16f * L.scale, 0.03f, FxColour{ 1.f, 0.2f, 0.2f }, 2.6f, alpha * (0.5f + (0.5f * std::sin(te * 24.f))), te * 0.8f);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.9f }, 3.4f, FxColour{ 1.f, 0.2f, 0.2f }, 1.5f * alpha);
        // 幻覚で姿が二重にぶれる。
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        const float env = fx_envelope(te - 0.2f, 0.1f, 0.6f, 0.3f);
        mod.echo_x = std::max(mod.echo_x, 0.16f * env * std::sin(te * 18.f));
        mod.echo_y = std::max(mod.echo_y, 0.02f * env);
    }
}

/* ============================================================ gb_kyuttoshitedoka_n */

void look_kyuttoshitedoka_n(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float squeeze = L.mini ? 0.f : 0.42f;
    const Vec3 c{ L.foot.x, L.foot.y, 0.5f * L.scale };
    // きゅっ：球が一点へ縮む。
    if (te < squeeze) {
        const float u = te / squeeze;
        const float r = fx_lerp(0.62f, 0.03f, u * u) * L.scale;
        sphere_shell(L, out, c, r, 64, 0.045f, FxColour{ 1.f, 0.2f, 0.25f }, 2.2f, 0.9f, te * 6.f);
        put(out, c, 0.05f + (0.1f * u), FxColour{ 1.f, 0.9f, 0.9f }, 3.6f, 1.f, FxPart::BurstFlash);
    }
    // ドカーン：色とりどりの欠片。
    const float tb = te - squeeze;
    if (tb >= 0.f) {
        if (tb < 0.2f) {
            const float u = tb / 0.2f;
            put(out, c, ((0.4f * (1.f - u)) + 0.06f) * L.scale, FxColour{ 1.f, 1.f, 1.f }, 3.4f, 1.f - u, FxPart::BurstFlash);
        }
        const int n = count_of(L, 40);
        for (int i = 0; i < n; ++i) {
            const std::uint32_t id = static_cast<std::uint32_t>(i);
            if (tb > 0.7f) {
                break;
            }
            const float th = 2.f * kPi * fx_rand(L.seed, id, 1u);
            const float ph = std::acos((2.f * fx_rand(L.seed, id, 2u)) - 1.f);
            const Vec3 d{ std::sin(ph) * std::cos(th), std::sin(ph) * std::sin(th), std::cos(ph) };
            const float sp = 1.2f + (1.6f * fx_rand(L.seed, id, 3u));
            const float dist = sp * 0.3f * (1.f - std::exp(-5.f * tb));
            Vec3 p = c + (d * (dist * L.scale));
            p.z = std::max(p.z, 0.03f);
            FxVoxel v;
            v.center = p;
            v.size = Vec3{ 0.13f * L.scale, 0.035f, 0.035f };
            const FxColour col = hue_colour(fx_rand(L.seed, id, 4u));
            v.r = col.r;
            v.g = col.g;
            v.b = col.b;
            v.emissive = 2.6f;
            v.alpha = 1.f - fx_ramp(tb, 0.4f, 0.7f);
            fx_direction_angles(d, v.yaw, v.pitch);
            v.part = FxPart::Debris;
            out.voxels.push_back(v);
        }
        ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.8f * L.scale, squeeze, 0.4f, 0.05f, FxColour{ 1.f, 0.6f, 0.7f }, 2.4f, 0.9f, 26);
        ring_at(L, out, false, c, 0.1f, 0.9f * L.scale, squeeze + 0.03f, 0.4f, 0.04f, FxColour{ 0.8f, 0.7f, 1.f }, 2.2f, 0.8f, 26);
        shake_for(L, out, squeeze, 0.4f, 0.12f);
        if (!L.mini) {
            add_light(out, c, 4.4f, FxColour{ 1.f, 0.6f, 0.7f }, 2.6f * fx_envelope(tb, 0.03f, 0.15f, 0.5f));
        }
    }
}

void flight_kyuttoshitedoka_n(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    (void)e;
    const FlightPose f = flight_pose(ctx);
    const FxColour c = hue_colour(ctx.t * 2.f);
    put(out, f.pos, 0.14f, FxColour{ 1.f, 1.f, 1.f }, 3.4f, 1.f, FxPart::OrbCore);
    satellites(out, f.pos, f.dir, 7, 0.2f, 16.f, ctx.t, 0.065f, c, 2.8f);
    const float fly = std::max(ctx.fly, 1e-4f);
    for (int k = 1; k <= 8; ++k) {
        const float tt = ctx.t - (0.02f * static_cast<float>(k));
        if (tt <= 0.f) {
            break;
        }
        const Vec3 p = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(tt / fly));
        put(out, p, 0.09f * (1.f - (static_cast<float>(k) / 9.f)), hue_colour((ctx.t * 2.f) - (0.07f * static_cast<float>(k))), 2.f, 0.7f, FxPart::OrbTail);
    }
    add_light(out, f.pos, 2.8f, c, 1.3f);
    mark_projectile(out, f.pos, f.dir);
}

/* ============================================================ gb_ryuuri */

void look_ryuuri(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.15f, 0.7f, 0.5f);
    // 昇る青いひとだま。
    for (int i = 0; i < (L.mini ? 1 : 4); ++i) {
        const float born = 0.1f * static_cast<float>(i);
        const float age = te - born;
        if ((age < 0.f) || (age >= 1.2f)) {
            continue;
        }
        const float u = age / 1.2f;
        const float ang = (2.f * kPi * static_cast<float>(i) / 4.f) + (age * 3.f);
        const float r = (0.3f - (0.1f * u)) * L.scale;
        const Vec3 p{ L.foot.x + (std::cos(ang) * r), L.foot.y + (std::sin(ang) * r), (0.1f + (1.1f * u)) * L.scale };
        glyph(L, out, shape_hitodama(), p, 0.03f * L.scale, alpha * (1.f - fx_ramp(u, 0.7f, 1.f)), L.e.base, 1.2f, 2, 0.f);
        for (int k = 1; k <= 4; ++k) {
            put(out, p - Vec3{ 0.f, 0.f, 0.07f * static_cast<float>(k) }, 0.05f * (1.f - (0.2f * static_cast<float>(k))), L.e.tint, 1.6f, 0.5f * alpha);
        }
    }
    WhirlParams w;
    w.z0 = 0.05f;
    w.height = 1.0f;
    w.radius0 = 0.28f;
    w.radius1 = 0.12f;
    w.speed = 4.f;
    w.rise = 0.5f;
    w.count = 26;
    w.life = 1.2f;
    w.duration = 0.6f;
    w.c0 = L.e.base;
    w.c1 = L.e.tint;
    w.size = 0.07f;
    w.emissive = 1.8f;
    w.part = FxPart::Mist;
    whirl(L, out, w);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.7f }, 3.2f, L.e.base, 1.1f * alpha);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.grey = std::max(mod.grey, 0.3f * alpha);
    }
}

void beam_ryuuri(const FxStyleCtx &ctx, const Entry &e, float t_end, FxFrame &out)
{
    Vec3 from;
    Vec3 to;
    beam_ends(ctx.spec, from, to);
    const float travel = std::max(0.35f, t_end + 0.15f);
    const float prog = ease_out(fx_clamp01(ctx.t / travel));
    const float al = 1.f - fx_ramp(ctx.t, travel + 0.1f, travel + 0.4f);
    if (al <= 0.01f) {
        return;
    }
    const Vec3 d = to - from;
    const Vec3 side = unit_or(cross(d, Vec3{ 0.f, 0.f, 1.f }), Vec3{ 0.f, 1.f, 0.f });
    const int n = 22;
    for (int k = 0; k < n; ++k) {
        const float s = prog * (1.f - (static_cast<float>(k) / static_cast<float>(n)));
        const float wave = std::sin((static_cast<float>(k) * 0.9f) - (ctx.t * 11.f));
        const Vec3 p = from + (d * s) + (side * (wave * 0.14f)) + Vec3{ 0.f, 0.f, 0.5f + (0.08f * wave) };
        const float f = 1.f - (static_cast<float>(k) / static_cast<float>(n));
        put(out, p, 0.09f * (0.4f + (0.6f * f)), fx_mix(e.tint, e.base, f), 2.2f, al * (0.4f + (0.6f * f)), FxPart::BeamCore);
    }
    const Look L = any_look(ctx, e);
    const Vec3 head = from + (d * prog) + Vec3{ 0.f, 0.f, 0.62f };
    glyph(L, out, shape_hitodama(), head, 0.022f, al, e.base, 2.6f, 1, 0.f);
    add_light(out, head, 2.8f, e.base, 1.2f * al);
}

/* ============================================================ gb_slow_twine */

void look_slow_twine(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float tight = fx_ramp(te, 0.f, 0.55f);
    const float alpha = fx_envelope(te, 0.05f, 0.95f, 0.45f);
    for (int strand = 0; strand < 2; ++strand) {
        const int n = L.mini ? 8 : 20;
        for (int i = 0; i < n; ++i) {
            const float f = static_cast<float>(i) / static_cast<float>(n);
            if (f > (tight + 0.05f)) {
                break;
            }
            const float ang = (f * 4.f * kPi) + (kPi * static_cast<float>(strand)) + (te * 1.5f);
            const float r = fx_lerp(0.4f, 0.27f, tight) * L.scale;
            const Vec3 p{ L.foot.x + (std::cos(ang) * r), L.foot.y + (std::sin(ang) * r), (0.05f + (0.6f * f)) * L.scale };
            const Vec3 tangent{ -std::sin(ang), std::cos(ang), 0.2f };
            put_bar(out, p, normalize(tangent), 0.13f * L.scale, 0.045f, (strand == 0) ? L.e.base : L.e.tint, 0.7f, alpha, FxPart::Debris);
        }
    }
    // 足の甲を縛る輪と、はみ出た葉。
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.06f }, 0.34f * L.scale, 0.14f * L.scale, 0.f, 0.6f, 0.04f, L.e.base, 0.8f, alpha, 18);
    for (int i = 0; i < count_of(L, 6); ++i) {
        const float a = 2.f * kPi * fx_rand(L.seed, static_cast<std::uint32_t>(i), 1u);
        put(out, Vec3{ L.foot.x + (std::cos(a) * 0.2f), L.foot.y + (std::sin(a) * 0.2f), (0.1f + (0.5f * fx_rand(L.seed, static_cast<std::uint32_t>(i), 2u))) * L.scale }, 0.05f,
            FxColour{ 0.3f, 0.7f, 0.2f }, 0.8f, alpha * tight, FxPart::Debris);
    }
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.012f * fx_envelope(te - 0.1f, 0.1f, 0.5f, 0.4f));
        mod.tremble_phase = te * 30.f;
    }
}

/* ============================================================ gb_possession */

void look_possession(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float dive = fx_ramp(te, 0.1f, 0.55f);
    const float ghost_a = fx_envelope(te - 0.05f, 0.1f, 0.35f, 0.15f);
    const Vec3 c{ L.front.x, L.front.y, 0.55f * L.scale };
    // 上から憑りつく幽霊。
    const Vec3 p = c + Vec3{ 0.f, 0.f, 1.4f * (1.f - (dive * dive)) } + on_screen(L.spec, 0.3f * (1.f - dive), 0.f);
    glyph(L, out, shape_ghost(), p, 0.04f * L.scale * (1.f - (0.5f * dive)), ghost_a, FxColour{ 1.f, 0.8f, 0.85f }, 0.9f, 2, 0.2f * std::sin(te * 10.f));
    // 入ったあとの赤い靄と、二重にずれる姿。
    const float inside = fx_envelope(te - 0.5f, 0.1f, 0.5f, 0.45f);
    FxSmoke smoke;
    smoke.origin = Vec3{ L.front.x, L.front.y, 0.4f * L.scale };
    smoke.radius = 0.18f * L.scale;
    smoke.born = 0.5f;
    smoke.duration = 0.4f;
    smoke.life = 0.8f;
    smoke.count = count_of(L, 10);
    smoke.rise = 0.5f;
    smoke.size = 0.16f * L.scale;
    smoke.grow = 1.6f;
    smoke.colour = FxColour{ 1.f, 0.35f, 0.4f };
    smoke.colour_end = FxColour{ 0.5f, 0.1f, 0.25f };
    smoke.alpha = 0.7f;
    smoke.emissive = 1.6f;
    smoke.seed = L.seed;
    (void)fx_emit_smoke(smoke, te, out);
    if (te > 0.5f) {
        ring_at(L, out, false, c, 0.1f, 0.55f * L.scale, 0.5f, 0.4f, 0.035f, FxColour{ 1.f, 0.5f, 0.55f }, 2.f, 0.85f, 22);
    }
    if (!L.mini) {
        add_light(out, c, 3.f, FxColour{ 1.f, 0.4f, 0.45f }, 1.3f * (ghost_a + inside));
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.echo_x = std::max(mod.echo_x, 0.12f * inside * std::sin(te * 16.f));
        mod.tremble = std::max(mod.tremble, 0.02f * inside);
        mod.tremble_phase = te * 80.f;
    }
}

/* ============================================================ gb_soulsteal */

void look_soulsteal(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const Vec3 start{ L.front.x, L.front.y, 0.5f * L.scale };
    const Vec3 dst = fx_source_centre(L.spec, 0.85f);
    // 体から魂が引き抜かれ、撃った者へ飛んでいく。
    const float pull = fx_ramp(te, 0.05f, 0.4f);
    const float fly_u = fx_clamp01((te - 0.4f) / 0.6f);
    Vec3 p;
    if (te < 0.4f) {
        p = start + Vec3{ 0.f, 0.f, 0.5f * pull * L.scale };
    } else {
        const Vec3 from = start + Vec3{ 0.f, 0.f, 0.5f * L.scale };
        p = from + ((dst - from) * fly_u);
        p.z += 0.5f * std::sin(kPi * fly_u);
    }
    const float al = (te < 0.05f) ? 0.f : (1.f - fx_ramp(te, 1.0f, 1.15f));
    if (!L.mini) {
        glyph(L, out, shape_hitodama(), p, 0.042f, al, FxColour{ 1.f, 0.25f, 0.2f }, 1.3f, 2, 0.f);
        for (int k = 1; k <= 8; ++k) {
            const float tt = te - (0.025f * static_cast<float>(k));
            if (tt < 0.05f) {
                break;
            }
            Vec3 q;
            if (tt < 0.4f) {
                q = start + Vec3{ 0.f, 0.f, 0.5f * fx_ramp(tt, 0.05f, 0.4f) * L.scale };
            } else {
                const Vec3 from = start + Vec3{ 0.f, 0.f, 0.5f * L.scale };
                const float uu = fx_clamp01((tt - 0.4f) / 0.6f);
                q = from + ((dst - from) * uu);
                q.z += 0.5f * std::sin(kPi * uu);
            }
            put(out, q, 0.07f * (1.f - (static_cast<float>(k) / 9.f)), FxColour{ 1.f, 0.4f, 0.3f }, 2.f, 0.7f * al);
        }
    }
    // 抜かれた跡の赤い火花。
    FxSpray sp;
    sp.origin = start;
    sp.cone = kPi;
    sp.speed_min = 0.4f;
    sp.speed_max = 1.2f;
    sp.gravity = 0.4f;
    sp.drag = 2.f;
    sp.born = 0.1f;
    sp.duration = 0.3f;
    sp.life_min = 0.3f;
    sp.life_max = 0.6f;
    sp.count = count_of(L, 20);
    sp.colour0 = FxColour{ 1.f, 0.5f, 0.4f };
    sp.colour1 = FxColour{ 0.7f, 0.1f, 0.15f };
    sp.size0 = 0.05f;
    sp.size1 = 0.015f;
    sp.emissive0 = 2.6f;
    sp.emissive1 = 0.5f;
    sp.seed = L.seed;
    (void)fx_emit_spray(sp, te, out);
    if (te > 0.95f) {
        ring_at(L, out, false, dst, 0.05f, 0.45f, 0.95f, 0.25f, 0.03f, FxColour{ 1.f, 0.4f, 0.35f }, 2.2f, 0.8f, 18);
    }
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.grey = std::max(mod.grey, 0.5f * fx_envelope(te, 0.15f, 0.55f, 0.45f));
        add_light(out, p, 2.6f, FxColour{ 1.f, 0.3f, 0.25f }, 1.2f * al);
    }
}

/* ============================================================ gb_nightmare */

void look_nightmare(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.15f, 0.9f, 0.5f);
    // 頭の上の三日月と、またたく星。
    if (!L.mini) {
        glyph(L, out, shape_crescent(), Vec3{ L.front.x, L.front.y, 1.3f * L.scale } + on_screen(L.spec, 0.f, 0.03f * std::sin(te * 3.f)), 0.036f * L.scale, alpha, FxColour{ 0.65f, 0.75f, 1.f },
            2.0f, 2, 0.6f, "o", { FxColour{ 1.f, 1.f, 0.9f } });
    }
    for (int i = 0; i < count_of(L, 6); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float a = 2.f * kPi * fx_rand(L.seed, id, 1u);
        const float r = (0.3f + (0.3f * fx_rand(L.seed, id, 2u))) * L.scale;
        glyph(L, out, shape_star4(), Vec3{ L.front.x, L.front.y, 1.15f * L.scale } + on_screen(L.spec, std::cos(a) * r, std::sin(a) * r * 0.5f), 0.01f, alpha * (0.5f + (0.5f * std::sin((te * 9.f) + static_cast<float>(i)))),
            FxColour{ 1.f, 1.f, 0.9f }, 3.f);
    }
    // 降りてくる暗い雲。
    FxSmoke cloud;
    cloud.origin = Vec3{ L.front.x, L.front.y, 1.25f * L.scale };
    cloud.radius = 0.28f * L.scale;
    cloud.born = 0.f;
    cloud.duration = 0.5f;
    cloud.life = 1.0f;
    cloud.count = count_of(L, 12);
    cloud.rise = -0.5f;
    cloud.size = 0.2f * L.scale;
    cloud.grow = 1.7f;
    cloud.colour = FxColour{ 0.15f, 0.16f, 0.45f };
    cloud.colour_end = FxColour{ 0.06f, 0.05f, 0.2f };
    cloud.alpha = 0.8f;
    cloud.emissive = 0.9f;
    cloud.seed = L.seed;
    (void)fx_emit_smoke(cloud, te, out);
    // 眠りの Z。
    for (int i = 0; i < (L.mini ? 1 : 3); ++i) {
        const float born = 0.15f * static_cast<float>(i);
        const float age = te - born;
        if ((age < 0.f) || (age >= 1.1f)) {
            continue;
        }
        const float u = age / 1.1f;
        glyph(L, out, shape_z(), Vec3{ L.front.x, L.front.y, (0.75f + (0.4f * u)) * L.scale } + on_screen(L.spec, (0.22f + (0.08f * static_cast<float>(i)) + (0.04f * std::sin(age * 6.f))) * L.scale, 0.f),
            (0.014f + (0.004f * static_cast<float>(i))) * L.scale, fx_envelope(age, 0.15f, 0.5f, 0.45f), FxColour{ 0.6f, 0.7f, 1.f }, 2.0f);
    }
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 1.0f }, 3.4f, FxColour{ 0.4f, 0.45f, 1.f }, 1.2f * alpha);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.grey = std::max(mod.grey, 0.3f * alpha);
        mod.tremble = std::max(mod.tremble, 0.012f * alpha);
        mod.tremble_phase = te * 20.f;
    }
}

/* ============================================================ gb_train */

namespace {

//! 幽霊列車（機関車と客車 2 両）。`pos` は先頭の位置（水平）、`dir` は進む向き、`k` は大きさ。
void draw_train(FxFrame &out, const Vec3 &pos, const Vec3 &dir_in, float k, float alpha, float t)
{
    if (alpha <= 0.01f) {
        return;
    }
    const Vec3 dir = unit_or(Vec3{ dir_in.x, dir_in.y, 0.f }, Vec3{ 1.f, 0.f, 0.f });
    const Vec3 side{ -dir.y, dir.x, 0.f };
    const FxColour body{ 0.12f, 0.34f, 0.22f };
    const FxColour trim{ 0.45f, 1.f, 0.65f };
    const FxColour glass{ 0.8f, 1.f, 0.85f };
    const float z0 = 0.1f * k;
    const auto at = [&](float fwd, float z) { return Vec3{ pos.x + (dir.x * fwd * k), pos.y + (dir.y * fwd * k), z0 + (z * k) }; };
    // 機関車：ボイラーと運転室、煙突、前灯。
    box(out, at(0.1f, 0.18f), dir, 0.5f * k, 0.24f * k, 0.2f * k, body, 0.35f, 0.92f * alpha);
    box(out, at(-0.22f, 0.22f), dir, 0.24f * k, 0.3f * k, 0.28f * k, body, 0.35f, 0.92f * alpha);
    box(out, at(-0.22f, 0.4f), dir, 0.28f * k, 0.34f * k, 0.04f * k, trim, 1.4f, alpha);
    box(out, at(0.26f, 0.34f), dir, 0.07f * k, 0.07f * k, 0.14f * k, FxColour{ 0.06f, 0.06f, 0.08f }, 0.1f, alpha);
    put(out, at(0.38f, 0.22f), 0.09f * k, FxColour{ 1.f, 0.95f, 0.6f }, 4.f, alpha, FxPart::Shape);
    for (int s = -1; s <= 1; s += 2) {
        const Vec3 off = side * (0.13f * k * static_cast<float>(s));
        box(out, at(-0.22f, 0.26f) + off, dir, 0.1f * k, 0.01f * k, 0.1f * k, glass, 2.2f, alpha);
        for (int w = 0; w < 3; ++w) {
            box(out, at(-0.15f + (0.2f * static_cast<float>(w)), 0.06f) + (side * (0.12f * k * static_cast<float>(s))), dir, 0.1f * k, 0.02f * k, 0.1f * k, FxColour{ 0.05f, 0.05f, 0.06f }, 0.1f, alpha);
        }
    }
    // 客車。
    for (int c = 1; c <= 2; ++c) {
        const float back = -0.85f * static_cast<float>(c);
        box(out, at(back, 0.2f), dir, 0.62f * k, 0.28f * k, 0.28f * k, body, 0.35f, 0.9f * alpha);
        box(out, at(back, 0.36f), dir, 0.64f * k, 0.3f * k, 0.03f * k, trim, 1.4f, alpha);
        for (int s = -1; s <= 1; s += 2) {
            for (int w = 0; w < 3; ++w) {
                box(out, at(back - 0.2f + (0.2f * static_cast<float>(w)), 0.24f) + (side * (0.145f * k * static_cast<float>(s))), dir, 0.1f * k, 0.01f * k, 0.1f * k, glass, 2.2f * (0.7f + (0.3f * std::sin((t * 20.f) + static_cast<float>(w + c)))), alpha);
            }
        }
    }
    // 煙突の煙。
    for (int i = 0; i < 4; ++i) {
        const float tail = 0.15f * static_cast<float>(i + 1);
        put(out, at(0.26f - (0.55f * tail), 0.5f + (0.35f * tail)), (0.1f + (0.05f * static_cast<float>(i))) * k, FxColour{ 0.85f, 1.f, 0.9f }, 0.8f, 0.5f * alpha * (1.f - (0.2f * static_cast<float>(i))), FxPart::Mist);
    }
    // 車輪の火花。
    for (int i = 0; i < 4; ++i) {
        const float ph = fx_rand(static_cast<std::uint32_t>(t * 100.f), static_cast<std::uint32_t>(i));
        put(out, at(-1.4f + (1.9f * ph), 0.02f), 0.04f * k, FxColour{ 0.6f, 1.f, 0.7f }, 2.4f, 0.8f * alpha, FxPart::Spark);
    }
}

} // namespace

void beam_train(const FxStyleCtx &ctx, const Entry &e, float t_end, FxFrame &out)
{
    (void)e;
    Vec3 from;
    Vec3 to;
    beam_ends(ctx.spec, from, to);
    const float travel = std::max(0.4f, t_end + 0.2f);
    const float u = fx_clamp01(ctx.t / travel);
    const Vec3 d = to - from;
    const Vec3 dir = unit_or(d, Vec3{ 1.f, 0.f, 0.f });
    const float al = 1.f - fx_ramp(ctx.t, travel + 0.05f, travel + 0.35f);
    // 先頭は撃った者の手前から対象の少し先まで走る。
    const Vec3 head = from + (d * (u * 1.1f)) + (dir * -1.6f * (1.f - u));
    draw_train(out, head, dir, 1.f, al, ctx.t);
    // 線路代わりの淡い緑の光の筋。
    beam_bar(out, from + Vec3{ 0.f, 0.f, -0.45f }, to + Vec3{ 0.f, 0.f, -0.45f }, 0.25f, FxColour{ 0.3f, 1.f, 0.5f }, 0.8f, 0.2f * al, fx_clamp01(u * 1.2f), FxPart::BeamGlow);
    add_light(out, head + Vec3{ 0.f, 0.f, 0.4f }, 3.4f, FxColour{ 0.5f, 1.f, 0.7f }, 1.3f * al);
}

void flight_train(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    (void)e;
    const FlightPose f = flight_pose(ctx);
    draw_train(out, Vec3{ f.pos.x, f.pos.y, 0.f }, f.dir, 0.55f, 1.f, ctx.t);
    add_light(out, f.pos, 2.8f, FxColour{ 0.5f, 1.f, 0.7f }, 1.2f);
    mark_projectile(out, f.pos, f.dir);
}

void look_train(const Look &L, FxFrame &out)
{
    const float te = L.te;
    // ぶつかった瞬間の閃光と、蒸気。
    if (te < 0.25f) {
        const float u = te / 0.25f;
        put(out, Vec3{ L.front.x, L.front.y, 0.45f * L.scale }, ((0.32f * (1.f - u)) + 0.05f) * L.scale, FxColour{ 0.85f, 1.f, 0.9f }, 3.2f, 1.f - u, FxPart::BurstFlash);
    }
    FxSmoke steam;
    steam.origin = Vec3{ L.front.x, L.front.y, 0.3f };
    steam.radius = 0.2f * L.scale;
    steam.born = 0.f;
    steam.duration = 0.3f;
    steam.life = 0.9f;
    steam.count = count_of(L, 10);
    steam.rise = 0.5f;
    steam.size = 0.2f * L.scale;
    steam.grow = 2.f;
    steam.colour = FxColour{ 0.9f, 1.f, 0.95f };
    steam.colour_end = FxColour{ 0.55f, 0.75f, 0.65f };
    steam.alpha = 0.75f;
    steam.emissive = 0.8f;
    steam.seed = L.seed;
    (void)fx_emit_smoke(steam, te, out);
    FxSpray sp;
    sp.origin = Vec3{ L.front.x, L.front.y, 0.4f * L.scale };
    sp.cone = kPi;
    sp.speed_min = 1.f;
    sp.speed_max = 2.6f;
    sp.gravity = -3.f;
    sp.drag = 1.5f;
    sp.life_min = 0.2f;
    sp.life_max = 0.45f;
    sp.count = count_of(L, 20);
    sp.colour0 = FxColour{ 0.9f, 1.f, 0.9f };
    sp.colour1 = FxColour{ 0.3f, 0.9f, 0.5f };
    sp.size0 = 0.05f;
    sp.size1 = 0.015f;
    sp.emissive0 = 2.8f;
    sp.emissive1 = 0.6f;
    sp.seed = L.seed;
    (void)fx_emit_spray(sp, te, out);
    shake_for(L, out, 0.f, 0.4f, 0.1f);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.6f }, 3.6f, FxColour{ 0.5f, 1.f, 0.7f }, 1.8f * fx_envelope(te, 0.03f, 0.15f, 0.5f));
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        float ax = 0.f;
        float ay = 1.f;
        facing_away(L, ax, ay);
        mod.recoil = std::max(mod.recoil, 0.6f * fx_envelope(te, 0.03f, 0.1f, 0.4f));
        mod.away_x = ax;
        mod.away_y = ay;
    }
}

/* ============================================================ gb_northern */

void look_northern(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.1f, 0.9f, 0.5f);
    // 舞い降りる雪の結晶。
    for (int i = 0; i < count_of(L, 10); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float born = 0.5f * fx_rand(L.seed, id, 1u);
        const float age = te - born;
        if ((age < 0.f) || (age >= 1.1f)) {
            continue;
        }
        const float u = age / 1.1f;
        const float a = 2.f * kPi * fx_rand(L.seed, id, 2u);
        const float r = (0.1f + (0.38f * fx_rand(L.seed, id, 3u))) * L.scale;
        const Vec3 p{ L.foot.x + (std::cos(a) * r) + (0.05f * std::sin(age * 5.f)), L.foot.y + (std::sin(a) * r), (1.6f - (1.5f * u)) * L.scale };
        glyph(L, out, shape_snowflake(), p, 0.012f, alpha * (1.f - fx_ramp(u, 0.8f, 1.f)), FxColour{ 0.9f, 0.97f, 1.f }, 2.2f, 1, age * 2.f);
    }
    // 床から氷の柱が伸びる。
    for (int i = 0; i < (L.mini ? 3 : 7); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float a = (2.f * kPi * static_cast<float>(i) / 7.f) + (0.3f * fx_rand(L.seed, id, 1u));
        const float r = (0.26f + (0.1f * fx_rand(L.seed, id, 2u))) * L.scale;
        const float grow = fx_ramp(te, 0.15f + (0.04f * static_cast<float>(i)), 0.45f + (0.04f * static_cast<float>(i)));
        const float h = (0.35f + (0.25f * fx_rand(L.seed, id, 3u))) * grow * L.scale;
        const Vec3 base{ L.foot.x + (std::cos(a) * r), L.foot.y + (std::sin(a) * r), 0.f };
        for (int j = 0; j < 4; ++j) {
            const float f = static_cast<float>(j) / 4.f;
            put(out, base + Vec3{ std::cos(a) * 0.04f * f, std::sin(a) * 0.04f * f, h * (0.12f + f) }, 0.07f * (1.f - (0.18f * static_cast<float>(j))), FxColour{ 0.75f, 0.92f, 1.f }, 1.4f, alpha * 0.95f, FxPart::Debris);
        }
    }
    FxSmoke mist;
    mist.origin = Vec3{ L.front.x, L.front.y, 0.1f };
    mist.radius = 0.3f * L.scale;
    mist.born = 0.1f;
    mist.duration = 0.5f;
    mist.life = 1.0f;
    mist.count = count_of(L, 10);
    mist.rise = 0.25f;
    mist.size = 0.2f * L.scale;
    mist.grow = 1.6f;
    mist.colour = FxColour{ 0.9f, 0.97f, 1.f };
    mist.colour_end = FxColour{ 0.6f, 0.78f, 0.95f };
    mist.alpha = 0.6f;
    mist.emissive = 0.9f;
    mist.seed = L.seed;
    (void)fx_emit_smoke(mist, te, out);
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.grey = std::max(mod.grey, 0.4f * alpha);
        mod.tremble = std::max(mod.tremble, 0.015f * alpha);
        mod.tremble_phase = te * 60.f;
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.8f }, 3.f, FxColour{ 0.7f, 0.85f, 1.f }, 1.0f * alpha);
    }
}

/* ============================================================ gb_n_e_p */

void look_n_e_p(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float env = fx_envelope(te, 0.04f, 0.35f, 0.5f);
    // 白い光の柱と、走る欠損の帯（水平に、位置がずれながら瞬く）。
    vertical_column(out, L.foot.x, L.foot.y, 0.05f, 2.2f * L.scale, 0.26f * L.scale, FxColour{ 1.f, 1.f, 1.f }, 3.2f, 0.55f * env, 0.3f);
    for (int i = 0; i < count_of(L, 9); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const std::uint32_t frame = static_cast<std::uint32_t>(std::floor(te * 22.f));
        const float z = (0.1f + (1.5f * fx_rand(L.seed, id, frame, 1u))) * L.scale;
        const float off = (fx_rand(L.seed, id, frame, 2u) - 0.5f) * 0.7f * L.scale;
        const float len = (0.3f + (0.7f * fx_rand(L.seed, id, frame, 3u))) * L.scale;
        put_bar(out, Vec3{ L.front.x, L.front.y, z } + (L.spec.cam_right * off), L.spec.cam_right, len, 0.025f, (i % 2 == 0) ? FxColour{ 0.5f, 1.f, 1.f } : FxColour{ 1.f, 1.f, 1.f }, 3.4f,
            env, FxPart::Spark);
    }
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.9f * L.scale, 0.f, 0.5f, 0.05f, FxColour{ 1.f, 1.f, 1.f }, 3.f, 0.9f, 26);
    ring_at(L, out, false, Vec3{ L.front.x, L.front.y, 0.6f * L.scale }, 0.1f, 1.f * L.scale, 0.12f, 0.5f, 0.035f, FxColour{ 0.6f, 1.f, 1.f }, 2.6f, 0.8f, 28);
    shake_for(L, out, 0.f, 0.6f, 0.1f);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 1.0f }, 5.f, FxColour{ 1.f, 1.f, 1.f }, 3.2f * env);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.grey = std::max(mod.grey, 0.6f * fx_envelope(te - 0.1f, 0.1f, 0.3f, 0.3f));
        mod.echo_x = std::max(mod.echo_x, 0.15f * env * std::sin(te * 40.f));
    }
}

void all_n_e_p(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    (void)e;
    const float t = ctx.t;
    // 画面が白く飛ぶ。
    const float env = fx_envelope(t, 0.05f, 0.12f, 0.6f);
    if (env > 0.01f) {
        fx_add_screen_wave(out, FxColour{ 1.f, 1.f, 1.f }, 0.5f, 3.1f, 0.55f * env, 0.f);
        fx_add_screen_wave(out, FxColour{ 0.6f, 1.f, 1.f }, 0.5f, 4.3f, 0.3f * env, 0.f);
    }
    fx_add_shake(out, fx_shake_envelope(t, 0.f, 0.6f, 0.1f));
}

/* ============================================================ gb_houtou */

void look_houtou(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.1f, 0.7f, 0.5f);
    // 頭上の宝塔から光が降りる。
    glyph(L, out, shape_pagoda(), Vec3{ L.front.x, L.front.y, 1.5f * L.scale }, 0.045f * L.scale, alpha, FxColour{ 1.f, 0.8f, 0.25f }, 1.1f, 2, 0.f);
    vertical_column(out, L.foot.x, L.foot.y, 0.05f, 1.35f * L.scale, 0.2f * L.scale, FxColour{ 1.f, 0.9f, 0.45f }, 2.2f, 0.5f * fx_envelope(te, 0.08f, 0.35f, 0.4f), 0.2f);
    radial_rays(L, out, Vec3{ L.front.x, L.front.y, 1.5f * L.scale }, L.mini ? 6 : 12, 0.3f * L.scale, 0.25f * L.scale, 0.03f, FxColour{ 1.f, 0.9f, 0.4f }, 2.8f, alpha, te * 1.2f);
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.65f * L.scale, 0.08f, 0.45f, 0.04f, FxColour{ 1.f, 0.9f, 0.4f }, 2.4f, 0.9f, 24);
    for (int i = 0; i < count_of(L, 16); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float age = te - (0.4f * fx_rand(L.seed, id, 1u));
        if ((age < 0.f) || (age >= 0.7f)) {
            continue;
        }
        const float a = 2.f * kPi * fx_rand(L.seed, id, 2u);
        const float r = 0.35f * fx_rand(L.seed, id, 3u) * L.scale;
        put(out, Vec3{ L.foot.x + (std::cos(a) * r), L.foot.y + (std::sin(a) * r), 0.1f + (1.0f * age) }, 0.05f, FxColour{ 1.f, 0.95f, 0.6f }, 2.8f, 1.f - fx_ramp(age, 0.4f, 0.7f));
    }
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.9f }, 4.2f, FxColour{ 1.f, 0.85f, 0.4f }, 2.2f * alpha);
    }
}

void beam_houtou(const FxStyleCtx &ctx, const Entry &e, float t_end, FxFrame &out)
{
    Vec3 from;
    Vec3 to;
    beam_ends(ctx.spec, from, to);
    const float vis = fx_ramp(ctx.t, 0.f, t_end + 0.06f);
    const float al = 1.f - fx_ramp(ctx.t, t_end + 0.25f, t_end + 0.6f);
    if (al <= 0.01f) {
        return;
    }
    const float pulse = 1.f + (0.15f * std::sin(ctx.t * 60.f));
    beam_bar(out, from, to, 0.28f * pulse, FxColour{ 1.f, 0.8f, 0.25f }, 1.2f, 0.32f * al, vis, FxPart::BeamGlow);
    beam_bar(out, from, to, 0.15f * pulse, FxColour{ 1.f, 0.92f, 0.5f }, 2.6f, 0.9f * al, vis, FxPart::BeamCore);
    beam_bar(out, from, to, 0.06f, FxColour{ 1.f, 1.f, 1.f }, 4.2f, al, vis, FxPart::BeamCore);
    // 撃った者の手元に宝塔。
    const Look L = any_look(ctx, e);
    glyph(L, out, shape_pagoda(), from + Vec3{ 0.f, 0.f, 0.5f }, 0.028f, al, FxColour{ 1.f, 0.85f, 0.3f }, 2.4f, 2, 0.f);
    add_light(out, from + Vec3{ 0.f, 0.f, 0.4f }, 3.4f, FxColour{ 1.f, 0.85f, 0.4f }, 1.4f * al);
}

} // namespace gb
} // namespace looks
} // namespace hd2d
