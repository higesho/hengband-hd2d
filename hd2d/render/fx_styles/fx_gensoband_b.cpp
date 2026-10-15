/*!
 * @file fx_gensoband_b.cpp
 * @brief 幻想蛮怒の見た目（後半）：心・面・海割り・紅い魔法・槍・皿・流星・斬撃・指紋・要石・蒸気・超自我・石油・酒・落とし穴・狂戦士・罠
 *        （`gb_satori` から `gb_activ_trap` まで 20 個）。
 *
 * 表と見た目の説明は `fx_gensoband.cpp`。
 */
#include "render/fx_styles/fx_gensoband_looks.h"

#include <algorithm>
#include <cmath>

namespace hd2d {
namespace looks {
namespace gb {

namespace {

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

Look any_look(const FxStyleCtx &ctx, const Entry &e)
{
    return make_look(ctx.spec, e, ctx.spec.centre_gx, ctx.spec.centre_gy, ctx.t, 1.f, false, ctx.spec.seed);
}

//! 画面に正対した、細く長い光の帯（斬撃）。中心 `c`、画面上の角度 `theta`、長さ `len`。`visible`（0〜1）まで伸びる。
void slash_line(const Look &L, FxFrame &out, const Vec3 &c, float theta, float len, float w, const FxColour &col, float emissive, float alpha, float visible)
{
    const Vec3 d = on_screen(L.spec, std::cos(theta), std::sin(theta));
    const float shown = len * fx_clamp01(visible);
    put_bar(out, c + (d * ((-0.5f * len) + (0.5f * shown))), d, shown, w, col, emissive, alpha, FxPart::Spark);
}

} // namespace

/* ============================================================ gb_satori */

void look_satori(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.1f, 0.85f, 0.5f);
    const int stage = (te < 0.15f) ? 2 : ((te < 0.25f) ? 1 : ((te < 1.0f) ? 0 : 1));
    const Vec3 eye{ L.front.x, L.front.y, 1.35f * L.scale };
    // 第三の目（範囲の周りの小さな版では出さない）。
    if (!L.mini) {
        glyph(L, out, shape_eye(stage), eye, 0.08f * L.scale, alpha, FxColour{ 0.7f, 0.3f, 0.85f }, 1.0f, 2, 0.f, "oO", { FxColour{ 1.f, 0.92f, 1.f }, FxColour{ 0.9f, 0.05f, 0.5f } });
    }
    // 目から頭へ伸びる管（線）。
    for (int k = 0; k < (L.mini ? 1 : 3); ++k) {
        const float ph = static_cast<float>(k) * 2.1f;
        for (int i = 0; i < 9; ++i) {
            const float f = static_cast<float>(i) / 8.f;
            const Vec3 p = eye - Vec3{ 0.f, 0.f, 0.55f * f * L.scale } + on_screen(L.spec, std::sin((f * 5.f) + ph + (te * 4.f)) * 0.08f * L.scale, 0.f);
            put(out, p, 0.035f, FxColour{ 0.95f, 0.4f, 0.75f }, 1.8f, alpha * fx_ramp(te, 0.1f + (0.05f * static_cast<float>(k)), 0.4f));
        }
    }
    // トラウマ：ハートが脈打って、割れて散る。
    const Vec3 heart{ L.front.x, L.front.y, 0.8f * L.scale };
    if (te < 0.75f) {
        glyph(L, out, shape_heart(), heart, 0.025f * L.scale * (1.f + (0.12f * std::sin(te * 22.f))), fx_ramp(te, 0.15f, 0.3f), FxColour{ 1.f, 0.35f, 0.65f }, 2.2f, 2, 0.f);
    } else {
        for (int i = 0; i < count_of(L, 16); ++i) {
            const std::uint32_t id = static_cast<std::uint32_t>(i);
            const float age = te - 0.75f;
            const float a = 2.f * kPi * fx_rand(L.seed, id, 1u);
            const float sp = 0.4f + (0.8f * fx_rand(L.seed, id, 2u));
            put(out, heart + on_screen(L.spec, std::cos(a) * sp * age, (std::sin(a) * sp * age) - (0.4f * age * age)), 0.045f, (i % 2 == 0) ? FxColour{ 1.f, 0.4f, 0.7f } : FxColour{ 0.7f, 0.3f, 1.f }, 2.4f, 1.f - fx_ramp(age, 0.2f, 0.5f));
        }
    }
    // 過去の姿の残像。
    if (!L.mini) {
        glyph(L, out, shape_ghost(), Vec3{ L.front.x, L.front.y, 0.55f * L.scale } + on_screen(L.spec, -0.4f * L.scale, 0.f), 0.03f * L.scale, 0.35f * alpha, FxColour{ 0.8f, 0.6f, 1.f }, 1.4f, 1);
    }
    ring_at(L, out, false, Vec3{ L.front.x, L.front.y, 0.8f * L.scale }, 0.75f * L.scale, 0.15f, 0.25f, 0.5f, 0.035f, FxColour{ 0.85f, 0.4f, 1.f }, 2.0f, 0.85f, 26);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 1.0f }, 3.4f, FxColour{ 0.85f, 0.4f, 1.f }, 1.3f * alpha);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.02f * fx_envelope(te - 0.3f, 0.1f, 0.4f, 0.3f));
        mod.tremble_phase = te * 70.f;
    }
}

/* ============================================================ gb_kokoro */

void look_kokoro(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.1f, 0.85f, 0.4f);
    static const FxColour cols[3] = { { 0.95f, 0.15f, 0.15f }, { 0.95f, 0.95f, 0.9f }, { 0.3f, 0.4f, 0.85f } };
    const Vec3 centre{ L.front.x, L.front.y, 0.75f * L.scale };
    const float settle = fx_ramp(te, 0.55f, 0.8f);
    const int n = L.mini ? 1 : 3;
    for (int i = 0; i < n; ++i) {
        const float ang = (te * 5.f) + (2.f * kPi * static_cast<float>(i) / 3.f);
        const float r = 0.45f * L.scale * (1.f - settle);
        // 最後に 1 つだけ顔に張りつく。
        const bool lands = (i == 1);
        const Vec3 p = centre + on_screen(L.spec, std::cos(ang) * r, std::sin(ang) * r * 0.55f);
        const float a = lands ? alpha : (alpha * (1.f - settle));
        glyph(L, out, shape_mask(), p, (0.032f + (lands ? 0.016f * settle : 0.f)) * L.scale, a, cols[i], 1.0f, 2, 0.4f * std::sin((te * 6.f) + static_cast<float>(i)), "", {});
    }
    for (int i = 0; i < count_of(L, 14); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float a = 2.f * kPi * fx_rand(L.seed, id, 1u);
        const float r = (0.2f + (0.4f * fx_rand(L.seed, id, 2u))) * L.scale;
        put(out, centre + on_screen(L.spec, std::cos(a) * r, std::sin(a) * r * 0.6f), 0.035f, cols[i % 3], 2.4f, alpha * (0.5f + (0.5f * std::sin((te * 11.f) + static_cast<float>(i)))));
    }
    if (!L.mini) {
        add_light(out, centre, 3.f, FxColour{ 1.f, 0.7f, 0.6f }, 1.1f * alpha);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.012f * alpha);
        mod.tremble_phase = te * 40.f;
    }
}

/* ============================================================ gb_moses */

namespace {

//! 水の壁の 1 本の柱（高さ `h`）。`seed` で小さくゆらす。
void water_column(FxFrame &out, const Vec3 &base, float h, float alpha, std::uint32_t seed, float t)
{
    if (h <= 0.02f) {
        return;
    }
    const int n = std::max(2, static_cast<int>(h / 0.1f));
    for (int i = 0; i < n; ++i) {
        const float f = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        const float wob = 0.02f * std::sin((t * 14.f) + (static_cast<float>(i) * 1.7f) + static_cast<float>(seed));
        put(out, base + Vec3{ wob, 0.f, h * f }, 0.11f, fx_mix(FxColour{ 0.2f, 0.5f, 1.f }, FxColour{ 0.65f, 0.88f, 1.f }, f), 0.9f + (0.8f * f), alpha * 0.82f, FxPart::Droplet);
    }
    // 頂の白い飛沫。
    put(out, base + Vec3{ 0.f, 0.f, h + 0.05f }, 0.08f, FxColour{ 1.f, 1.f, 1.f }, 2.2f, alpha * 0.9f, FxPart::Spark);
}

} // namespace

void look_moses(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float rise = ease_out(te / 0.3f);
    const float fall = fx_ramp(te, 0.9f, 1.4f);
    const float h = (1.45f * rise * (1.f - (0.95f * fall))) * L.scale;
    const float alpha = 1.f - fx_ramp(te, 1.3f, 1.6f);
    for (int s = -1; s <= 1; s += 2) {
        for (int d = -1; d <= 1; ++d) {
            const Vec3 base = Vec3{ L.foot.x, L.foot.y, 0.f } + (L.spec.cam_right * (0.5f * static_cast<float>(s) * L.scale));
            const Vec3 depth = unit_or(cross(Vec3{ 0.f, 0.f, 1.f }, L.spec.cam_right), Vec3{ 0.f, 1.f, 0.f });
            water_column(out, base + (depth * (0.17f * static_cast<float>(d) * L.scale)), h, alpha, static_cast<std::uint32_t>((s + 2) * 3 + d), te);
        }
        // 足元の波しぶき。
        ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f } + (L.spec.cam_right * (0.5f * static_cast<float>(s) * L.scale)), 0.05f, 0.35f * L.scale, 0.05f, 0.5f, 0.03f, FxColour{ 0.6f, 0.85f, 1.f }, 1.6f, 0.8f, 18);
    }
    // 中の乾いた道と、崩れるときの飛沫。
    FxSpray sp;
    sp.origin = Vec3{ L.foot.x, L.foot.y, 0.6f };
    sp.cone = 1.f;
    sp.scatter = 0.45f * L.scale;
    sp.speed_min = 0.5f;
    sp.speed_max = 1.4f;
    sp.gravity = -4.f;
    sp.born = 0.9f;
    sp.duration = 0.4f;
    sp.life_min = 0.3f;
    sp.life_max = 0.6f;
    sp.count = count_of(L, 28);
    sp.colour0 = FxColour{ 0.8f, 0.95f, 1.f };
    sp.colour1 = FxColour{ 0.3f, 0.6f, 1.f };
    sp.size0 = 0.06f;
    sp.size1 = 0.02f;
    sp.emissive0 = 2.f;
    sp.emissive1 = 0.8f;
    sp.floor = FxFloor::Vanish;
    sp.seed = L.seed;
    (void)fx_emit_spray(sp, te, out);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.7f }, 3.6f, FxColour{ 0.4f, 0.7f, 1.f }, 1.2f * rise * (1.f - fall));
        shake_for(L, out, 0.f, 0.3f, 0.05f);
    }
}

void beam_moses(const FxStyleCtx &ctx, const Entry &e, float t_end, FxFrame &out)
{
    (void)e;
    Vec3 from;
    Vec3 to;
    beam_ends(ctx.spec, from, to);
    const Vec3 dir = unit_or(Vec3{ to.x - from.x, to.y - from.y, 0.f }, Vec3{ 1.f, 0.f, 0.f });
    const Vec3 side{ -dir.y, dir.x, 0.f };
    const std::size_t points = ctx.spec.route.size();
    const float per = (points > 1u) ? (std::max(t_end, 0.05f) / static_cast<float>(points - 1u)) : 0.05f;
    const float fall = fx_ramp(ctx.t, t_end + 0.5f, t_end + 1.0f);
    const float alpha = 1.f - fx_ramp(ctx.t, t_end + 0.9f, t_end + 1.2f);
    for (std::size_t i = 1; i < points; ++i) {
        const float age = ctx.t - (per * static_cast<float>(i));
        if (age < 0.f) {
            continue;
        }
        const float h = 1.2f * ease_out(age / 0.25f) * (1.f - (0.95f * fall));
        for (int s = -1; s <= 1; s += 2) {
            const Vec3 base{ ctx.spec.route[i].x + (side.x * 0.48f * static_cast<float>(s)), ctx.spec.route[i].y + (side.y * 0.48f * static_cast<float>(s)), 0.f };
            water_column(out, base, h, alpha, static_cast<std::uint32_t>(i * 2u + (s > 0 ? 1u : 0u)), ctx.t);
        }
    }
}

/* ============================================================ gb_redmagic */

void look_redmagic(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.06f, 0.8f, 0.5f);
    // 下から噴き上がる紅い業火。
    FxSmoke fire;
    fire.origin = Vec3{ L.front.x, L.front.y, 0.03f };
    fire.radius = 0.2f * L.scale;
    fire.born = 0.f;
    fire.duration = 0.6f;
    fire.life = 0.8f;
    fire.count = count_of(L, 22);
    fire.rise = 1.6f;
    fire.size = 0.18f * L.scale;
    fire.grow = 1.2f;
    fire.colour = FxColour{ 1.f, 0.2f, 0.18f };
    fire.colour_end = FxColour{ 0.35f, 0.02f, 0.08f };
    fire.alpha = 0.9f;
    fire.emissive = 2.8f;
    fire.seed = L.seed;
    (void)fx_emit_smoke(fire, te, out);
    FxSpray sp;
    sp.origin = Vec3{ L.front.x, L.front.y, 0.1f };
    sp.dir = Vec3{ 0.f, 0.f, 1.f };
    sp.cone = 0.5f;
    sp.scatter = 0.15f * L.scale;
    sp.speed_min = 1.5f;
    sp.speed_max = 3.2f;
    sp.gravity = -2.f;
    sp.born = 0.f;
    sp.duration = 0.5f;
    sp.life_min = 0.3f;
    sp.life_max = 0.6f;
    sp.count = count_of(L, 26);
    sp.colour0 = FxColour{ 1.f, 0.6f, 0.4f };
    sp.colour1 = FxColour{ 0.8f, 0.05f, 0.1f };
    sp.size0 = 0.05f;
    sp.size1 = 0.015f;
    sp.emissive0 = 3.f;
    sp.emissive1 = 0.8f;
    sp.seed = L.seed + 5u;
    (void)fx_emit_spray(sp, te, out);
    // 紅い蝙蝠が飛び出す。
    for (int i = 0; i < (L.mini ? 1 : 4); ++i) {
        const float born = 0.12f * static_cast<float>(i);
        const float age = te - born;
        if ((age < 0.f) || (age >= 0.9f)) {
            continue;
        }
        const float u = age / 0.9f;
        const float a = (kPi * 0.5f) + ((static_cast<float>(i) - 1.5f) * 0.7f);
        const Vec3 p = Vec3{ L.front.x, L.front.y, 0.4f * L.scale } + on_screen(L.spec, std::cos(a) * 0.9f * u * L.scale, (std::sin(a) * 0.9f * u * L.scale) + (0.2f * std::sin(u * 12.f)));
        glyph(L, out, shape_bat(), p, 0.018f * L.scale, alpha * (1.f - fx_ramp(u, 0.7f, 1.f)), FxColour{ 0.85f, 0.08f, 0.15f }, 1.6f, 2, 0.f);
    }
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.65f * L.scale, 0.f, 0.5f, 0.045f, FxColour{ 1.f, 0.2f, 0.2f }, 2.6f, 0.9f, 24);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.6f }, 4.f, FxColour{ 1.f, 0.2f, 0.2f }, 2.2f * alpha);
    }
}

/* ============================================================ gb_gungnir */

void look_gungnir(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float fall = 0.26f;
    const float alpha = 1.f - fx_ramp(te, 0.85f, 1.2f);
    const float z = (te < fall) ? fx_lerp(3.2f, 0.78f, (te / fall) * (te / fall)) : 0.78f;
    // 上から降る紅い槍（穂先を下へ）。
    glyph(L, out, shape_spear(), Vec3{ L.front.x, L.front.y, z * L.scale }, 0.06f * L.scale, alpha, FxColour{ 1.f, 0.15f, 0.2f }, 2.6f, 2, kPi);
    if (te < fall) {
        for (int k = 1; k <= 5; ++k) {
            put(out, Vec3{ L.front.x, L.front.y, (z + (0.3f * static_cast<float>(k))) * L.scale }, 0.08f * (1.f - (0.15f * static_cast<float>(k))), FxColour{ 1.f, 0.3f, 0.3f }, 2.f, 0.5f);
        }
    } else {
        ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.7f * L.scale, fall, 0.4f, 0.05f, FxColour{ 1.f, 0.2f, 0.25f }, 2.6f, 0.9f, 24);
        FxSpray sp;
        sp.origin = Vec3{ L.front.x, L.front.y, 0.4f * L.scale };
        sp.cone = kPi;
        sp.speed_min = 0.8f;
        sp.speed_max = 2.2f;
        sp.gravity = -3.f;
        sp.drag = 1.5f;
        sp.born = fall;
        sp.life_min = 0.25f;
        sp.life_max = 0.5f;
        sp.count = count_of(L, 24);
        sp.colour0 = FxColour{ 1.f, 0.7f, 0.6f };
        sp.colour1 = FxColour{ 0.9f, 0.08f, 0.15f };
        sp.size0 = 0.06f;
        sp.size1 = 0.02f;
        sp.emissive0 = 3.f;
        sp.emissive1 = 0.8f;
        sp.seed = L.seed;
        (void)fx_emit_spray(sp, te, out);
        shake_for(L, out, fall, 0.35f, 0.08f);
        if (!L.mini) {
            FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
            mod.squash = std::max(mod.squash, 0.18f * fx_envelope(te - fall, 0.03f, 0.2f, 0.3f));
            add_light(out, Vec3{ L.foot.x, L.foot.y, 0.8f }, 4.f, FxColour{ 1.f, 0.2f, 0.25f }, 2.2f * fx_envelope(te - fall, 0.03f, 0.2f, 0.6f));
        }
    }
}

void flight_gungnir(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    put_bar(out, f.pos, f.dir, 0.75f, 0.055f, FxColour{ 1.f, 0.2f, 0.25f }, 2.6f, 1.f, FxPart::OrbCore);
    put(out, f.pos + (f.dir * 0.4f), 0.11f, FxColour{ 1.f, 0.85f, 0.85f }, 4.f, 1.f, FxPart::OrbCore);
    put_bar(out, f.pos - (f.dir * 0.3f), f.dir, 0.2f, 0.11f, FxColour{ 0.8f, 0.1f, 0.15f }, 2.f, 1.f, FxPart::OrbCore);
    flight_trail(ctx, out, e.base, 0.09f, 2.f, 10);
    add_light(out, f.pos, 3.f, e.base, 1.5f);
    mark_projectile(out, f.pos, f.dir);
}

/* ============================================================ gb_throw_plate */

void look_throw_plate(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float break_t = 0.2f;
    const Vec3 c{ L.front.x, L.front.y, 0.55f * L.scale };
    if (te < break_t) {
        glyph(L, out, shape_plate(), c, 0.04f * L.scale, 1.f, FxColour{ 0.35f, 0.8f, 0.4f }, 1.2f, 2, te * 30.f, "o", { FxColour{ 1.f, 1.f, 0.95f } });
    } else {
        // 皿が割れて、白い破片が飛ぶ。
        for (int i = 0; i < count_of(L, 18); ++i) {
            const std::uint32_t id = static_cast<std::uint32_t>(i);
            const float age = te - break_t;
            const float a = 2.f * kPi * fx_rand(L.seed, id, 1u);
            const float sp = 1.2f + (1.8f * fx_rand(L.seed, id, 2u));
            const float up = 0.6f + (1.8f * fx_rand(L.seed, id, 3u));
            Vec3 p{ c.x + (std::cos(a) * sp * 0.35f * (1.f - std::exp(-4.f * age))), c.y + (std::sin(a) * sp * 0.35f * (1.f - std::exp(-4.f * age))), c.z + (up * age) - (4.8f * age * age) };
            p.z = std::max(p.z, 0.03f);
            FxVoxel v;
            v.center = p;
            v.size = Vec3{ 0.1f, 0.075f, 0.02f };
            const FxColour col = (i % 3 == 0) ? FxColour{ 0.35f, 0.8f, 0.4f } : FxColour{ 1.f, 1.f, 0.95f };
            v.r = col.r;
            v.g = col.g;
            v.b = col.b;
            v.emissive = 0.9f;
            v.alpha = 1.f - fx_ramp(age, 0.6f, 0.95f);
            v.yaw = (9.f * age) + (6.f * fx_rand(L.seed, id, 4u));
            v.pitch = 10.f * age * (fx_rand(L.seed, id, 5u) - 0.5f);
            v.part = FxPart::Debris;
            out.voxels.push_back(v);
        }
        glyph(L, out, shape_star4(), c, 0.02f, fx_envelope(te - break_t, 0.01f, 0.05f, 0.15f), FxColour{ 1.f, 1.f, 1.f }, 3.4f);
        ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.5f * L.scale, break_t, 0.3f, 0.035f, FxColour{ 0.7f, 1.f, 0.75f }, 2.0f, 0.8f, 20);
    }
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        float ax = 0.f;
        float ay = 1.f;
        facing_away(L, ax, ay);
        mod.recoil = std::max(mod.recoil, 0.4f * fx_envelope(te - break_t, 0.02f, 0.08f, 0.3f));
        mod.away_x = ax;
        mod.away_y = ay;
    }
}

void flight_throw_plate(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    const Look L = any_look(ctx, e);
    glyph(L, out, shape_plate(), f.pos, 0.026f, 1.f, FxColour{ 0.35f, 0.8f, 0.4f }, 1.2f, 2, ctx.t * 26.f, "o", { FxColour{ 1.f, 1.f, 0.95f } });
    flight_trail(ctx, out, FxColour{ 0.8f, 1.f, 0.85f }, 0.05f, 1.2f, 5);
    add_light(out, f.pos, 2.2f, e.base, 0.7f);
    mark_projectile(out, f.pos, f.dir);
}

/* ============================================================ gb_blazingstar */

void look_blazingstar(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const Vec3 c{ L.front.x, L.front.y, 0.55f * L.scale };
    static const FxColour cols[3] = { { 1.f, 0.95f, 0.3f }, { 0.35f, 0.6f, 1.f }, { 0.4f, 1.f, 0.5f } };
    // 大きな星が回りながら開く。
    const float open = ease_out(te / 0.25f);
    if (!L.mini) {
        glyph(L, out, shape_star5(), c, 0.045f * L.scale * open, fx_envelope(te, 0.02f, 0.2f, 0.4f), cols[0], 3.0f, 2, te * 4.f);
    }
    // 星屑が四方へ飛ぶ。
    for (int i = 0; i < count_of(L, 24); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float a = 2.f * kPi * fx_rand(L.seed, id, 1u);
        const float sp = 0.8f + (1.4f * fx_rand(L.seed, id, 2u));
        const float age = te - (0.03f * fx_rand(L.seed, id, 3u));
        if ((age < 0.f) || (age > 0.8f)) {
            continue;
        }
        const float r = sp * 0.4f * (1.f - std::exp(-4.f * age));
        const Vec3 p = c + on_screen(L.spec, std::cos(a) * r, std::sin(a) * r * 0.8f);
        glyph(L, out, shape_star4(), p, 0.011f * (1.f - (0.5f * age)), 1.f - fx_ramp(age, 0.45f, 0.8f), cols[i % 3], 3.2f);
        put(out, c + on_screen(L.spec, std::cos(a) * r * 0.7f, std::sin(a) * r * 0.56f), 0.035f, cols[i % 3], 2.2f, 0.7f * (1.f - fx_ramp(age, 0.45f, 0.8f)));
    }
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.85f * L.scale, 0.03f, 0.45f, 0.045f, cols[1], 2.6f, 0.9f, 26);
    ring_at(L, out, false, c, 0.1f, 0.9f * L.scale, 0.f, 0.4f, 0.035f, cols[0], 2.8f, 0.9f, 28);
    shake_for(L, out, 0.f, 0.35f, 0.08f);
    if (!L.mini) {
        add_light(out, c, 4.6f, FxColour{ 1.f, 0.95f, 0.6f }, 2.6f * fx_envelope(te, 0.02f, 0.2f, 0.5f));
    }
}

void flight_blazingstar(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    const Look L = any_look(ctx, e);
    static const FxColour cols[3] = { { 1.f, 0.95f, 0.3f }, { 0.35f, 0.6f, 1.f }, { 0.4f, 1.f, 0.5f } };
    glyph(L, out, shape_star5(), f.pos, 0.024f, 1.f, cols[0], 3.2f, 2, ctx.t * 8.f);
    const float fly = std::max(ctx.fly, 1e-4f);
    for (int k = 1; k <= 9; ++k) {
        const float tt = ctx.t - (0.022f * static_cast<float>(k));
        if (tt <= 0.f) {
            break;
        }
        const Vec3 p = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(tt / fly));
        const float jx = 0.12f * std::sin((static_cast<float>(k) * 2.3f) + (ctx.t * 20.f));
        const float fr = 1.f - (static_cast<float>(k) / 10.f);
        glyph(L, out, shape_star4(), p + on_screen(ctx.spec, jx, 0.1f * std::cos(static_cast<float>(k) * 1.7f)), 0.008f + (0.006f * fr), fr, cols[k % 3], 3.f);
        put(out, p, 0.09f * fr, cols[k % 3], 2.f * fr, 0.6f * fr, FxPart::OrbTail);
    }
    add_light(out, f.pos, 3.4f, FxColour{ 1.f, 0.95f, 0.6f }, 1.7f);
    mark_projectile(out, f.pos, f.dir);
}

/* ============================================================ gb_soulsculpture */

void look_soulsculpture(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.05f, 0.7f, 0.3f);
    const Vec3 c{ L.front.x, L.front.y, 0.5f * L.scale };
    // 回る三日月の刃。
    const int n = L.mini ? 1 : 3;
    const int trails = L.mini ? 0 : 2;
    for (int i = 0; i < n; ++i) {
        const float ang = (te * 14.f) + (2.f * kPi * static_cast<float>(i) / static_cast<float>(n));
        const float r = 0.42f * L.scale;
        const Vec3 p = c + on_screen(L.spec, std::cos(ang) * r, std::sin(ang) * r * 0.6f);
        for (int tr = trails; tr >= 0; --tr) {
            const float a2 = ang - (0.25f * static_cast<float>(tr));
            const Vec3 q = c + on_screen(L.spec, std::cos(a2) * r, std::sin(a2) * r * 0.6f);
            glyph(L, out, shape_crescent(), (tr == 0) ? p : q, 0.03f * L.scale, alpha * ((tr == 0) ? 1.f : (0.45f / static_cast<float>(tr))),
                (tr == 0) ? FxColour{ 1.f, 0.9f, 0.9f } : FxColour{ 1.f, 0.25f, 0.25f }, 2.0f, 1, ang + (0.5f * kPi), "o", { FxColour{ 1.f, 0.3f, 0.3f } });
        }
    }
    // 円を描く斬撃の筋。
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.35f * L.scale }, 0.48f * L.scale, 0.55f * L.scale, 0.f, 0.7f, 0.04f, FxColour{ 1.f, 0.35f, 0.35f }, 2.4f, 0.8f * alpha, 28, 3, te * 12.f);
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.05f }, 0.1f, 0.7f * L.scale, 0.15f, 0.4f, 0.035f, FxColour{ 1.f, 0.9f, 0.9f }, 2.2f, 0.8f, 24);
    if (!L.mini) {
        add_light(out, c, 3.2f, FxColour{ 1.f, 0.35f, 0.35f }, 1.4f * alpha);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.02f * alpha);
        mod.tremble_phase = te * 90.f;
    }
}

/* ============================================================ gb_yaku */

void look_yaku(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.1f, 0.8f, 0.5f);
    // 厄の黒い帯が、上から螺旋を描いて体へ巻き付く。
    const int ribbons = L.mini ? 3 : 7;
    for (int r = 0; r < ribbons; ++r) {
        const float born = 0.05f * static_cast<float>(r);
        const float prog = fx_ramp(te - born, 0.f, 0.55f);
        const int n = 12;
        for (int i = 0; i < n; ++i) {
            const float f = static_cast<float>(i) / static_cast<float>(n);
            if (f > prog) {
                break;
            }
            const float ang = (2.f * kPi * static_cast<float>(r) / static_cast<float>(ribbons)) + (f * 5.f);
            const float rad = fx_lerp(0.8f, 0.15f, f) * L.scale;
            const Vec3 p{ L.foot.x + (std::cos(ang) * rad), L.foot.y + (std::sin(ang) * rad), (1.5f - (1.0f * f)) * L.scale };
            const Vec3 tangent = unit_or(Vec3{ -std::sin(ang), std::cos(ang), -0.35f }, Vec3{ 1.f, 0.f, 0.f });
            put_bar(out, p, tangent, 0.2f * L.scale, 0.05f, fx_mix(FxColour{ 0.5f, 0.06f, 0.2f }, FxColour{ 0.2f, 0.02f, 0.12f }, f), 1.4f, alpha, FxPart::Debris);
            if ((i % 4) == 0) {
                put(out, p, 0.04f, FxColour{ 1.f, 0.25f, 0.45f }, 2.4f, alpha * 0.8f);
            }
        }
    }
    FxSmoke mist;
    mist.origin = Vec3{ L.front.x, L.front.y, 0.4f * L.scale };
    mist.radius = 0.2f * L.scale;
    mist.born = 0.4f;
    mist.duration = 0.4f;
    mist.life = 0.8f;
    mist.count = count_of(L, 10);
    mist.rise = 0.4f;
    mist.size = 0.17f * L.scale;
    mist.grow = 1.7f;
    mist.colour = FxColour{ 0.4f, 0.05f, 0.2f };
    mist.colour_end = FxColour{ 0.1f, 0.02f, 0.08f };
    mist.alpha = 0.8f;
    mist.emissive = 0.9f;
    mist.seed = L.seed;
    (void)fx_emit_smoke(mist, te, out);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.7f }, 3.f, FxColour{ 0.9f, 0.1f, 0.35f }, 1.1f * alpha);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.grey = std::max(mod.grey, 0.4f * fx_envelope(te - 0.3f, 0.1f, 0.5f, 0.4f));
        mod.tremble = std::max(mod.tremble, 0.015f * alpha);
        mod.tremble_phase = te * 50.f;
    }
}

/* ============================================================ gb_special_shot */

void look_special_shot(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const Vec3 c{ L.front.x, L.front.y, 0.55f * L.scale };
    if (te < 0.18f) {
        const float u = te / 0.18f;
        put(out, c, ((0.26f * (1.f - u)) + 0.04f) * L.scale, FxColour{ 1.f, 1.f, 0.9f }, 3.6f, 1.f - u, FxPart::BurstFlash);
        radial_rays(L, out, c, L.mini ? 4 : 8, 0.06f, 0.4f * (1.f - u) * L.scale, 0.025f, FxColour{ 1.f, 0.95f, 0.7f }, 3.4f, 1.f - u, 0.4f);
    }
    // 跳ねる薬莢色の火花。
    FxSpray sp;
    sp.origin = c;
    sp.cone = kPi;
    sp.speed_min = 1.f;
    sp.speed_max = 3.f;
    sp.gravity = -6.f;
    sp.drag = 1.f;
    sp.life_min = 0.2f;
    sp.life_max = 0.45f;
    sp.count = count_of(L, 14);
    sp.colour0 = FxColour{ 1.f, 0.95f, 0.6f };
    sp.colour1 = FxColour{ 1.f, 0.55f, 0.15f };
    sp.size0 = 0.035f;
    sp.size1 = 0.01f;
    sp.emissive0 = 3.f;
    sp.emissive1 = 0.8f;
    sp.seed = L.seed;
    (void)fx_emit_spray(sp, te, out);
    ring_at(L, out, false, c, 0.05f, 0.55f * L.scale, 0.f, 0.35f, 0.03f, FxColour{ 1.f, 0.95f, 0.8f }, 2.6f, 0.9f, 20);
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.05f, 0.5f * L.scale, 0.05f, 0.35f, 0.03f, FxColour{ 1.f, 0.8f, 0.4f }, 2.2f, 0.8f, 20);
    FxSmoke puff;
    puff.origin = Vec3{ L.front.x, L.front.y, 0.5f * L.scale };
    puff.radius = 0.08f;
    puff.duration = 0.05f;
    puff.life = 0.45f;
    puff.count = count_of(L, 4);
    puff.rise = 0.3f;
    puff.size = 0.1f * L.scale;
    puff.grow = 1.8f;
    puff.colour = FxColour{ 0.8f, 0.8f, 0.78f };
    puff.colour_end = FxColour{ 0.5f, 0.5f, 0.5f };
    puff.alpha = 0.5f;
    puff.emissive = 0.6f;
    puff.seed = L.seed;
    (void)fx_emit_smoke(puff, te, out);
    if (!L.mini) {
        add_light(out, c, 3.f, FxColour{ 1.f, 0.9f, 0.6f }, 1.8f * fx_envelope(te, 0.01f, 0.08f, 0.3f));
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        float ax = 0.f;
        float ay = 1.f;
        facing_away(L, ax, ay);
        mod.recoil = std::max(mod.recoil, 0.3f * fx_envelope(te, 0.01f, 0.06f, 0.2f));
        mod.away_x = ax;
        mod.away_y = ay;
    }
}

void flight_special_shot(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    (void)e;
    const FlightPose f = flight_pose(ctx);
    put_bar(out, f.pos, f.dir, 0.55f, 0.03f, FxColour{ 1.f, 1.f, 0.9f }, 4.f, 1.f, FxPart::OrbCore);
    put_bar(out, f.pos - (f.dir * 0.4f), f.dir, 0.4f, 0.015f, FxColour{ 1.f, 0.9f, 0.5f }, 2.4f, 0.6f, FxPart::OrbTail);
    add_light(out, f.pos, 2.4f, FxColour{ 1.f, 0.9f, 0.6f }, 1.1f);
    mark_projectile(out, f.pos, f.dir);
}

void beam_special_shot(const FxStyleCtx &ctx, const Entry &e, float t_end, FxFrame &out)
{
    (void)e;
    Vec3 from;
    Vec3 to;
    beam_ends(ctx.spec, from, to);
    const float vis = fx_ramp(ctx.t, 0.f, t_end + 0.03f);
    const float al = 1.f - fx_ramp(ctx.t, t_end + 0.05f, t_end + 0.25f);
    beam_bar(out, from, to, 0.04f, FxColour{ 1.f, 1.f, 0.92f }, 4.f, al, vis, FxPart::BeamCore);
    beam_bar(out, from, to, 0.1f, FxColour{ 1.f, 0.85f, 0.45f }, 1.6f, 0.3f * al, vis, FxPart::BeamGlow);
    // 銃口の閃光。
    if (ctx.t < 0.08f) {
        put(out, from, 0.2f * (1.f - (ctx.t / 0.08f)) + 0.05f, FxColour{ 1.f, 0.95f, 0.7f }, 3.6f, 1.f - (ctx.t / 0.08f), FxPart::BurstFlash);
    }
}

/* ============================================================ gb_youmu */

void look_youmu(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const Vec3 c{ L.front.x, L.front.y, 0.55f * L.scale };
    // 六根清浄斬：六本の斬撃が順に走る。
    static const float theta[6] = { 0.35f, 1.9f, -0.5f, 1.2f, -1.15f, 0.05f };
    for (int k = 0; k < 6; ++k) {
        const float born = 0.07f * static_cast<float>(k);
        const float age = te - born;
        if ((age < 0.f) || (age > 0.55f)) {
            continue;
        }
        const FxColour col = (k == 5) ? FxColour{ 1.f, 0.3f, 0.3f } : ((k % 2 == 0) ? FxColour{ 0.6f, 0.8f, 1.f } : FxColour{ 0.85f, 0.92f, 1.f });
        const float fade = 1.f - fx_ramp(age, 0.15f, 0.55f);
        slash_line(L, out, c, theta[k], 1.6f * L.scale, 0.055f * L.scale, col, 3.4f, fade, ease_out(age / 0.1f));
        slash_line(L, out, c, theta[k], 1.6f * L.scale, 0.14f * L.scale, col, 1.2f, 0.3f * fade, ease_out(age / 0.1f));
    }
    // 最後に十字の衝撃波。
    const float tb = te - 0.42f;
    if (tb > 0.f) {
        ring_at(L, out, false, c, 0.1f, 0.95f * L.scale, 0.42f, 0.4f, 0.04f, FxColour{ 0.8f, 0.9f, 1.f }, 2.8f, 0.9f, 28);
        FxSpray sp;
        sp.origin = c;
        sp.cone = kPi;
        sp.speed_min = 0.8f;
        sp.speed_max = 2.4f;
        sp.gravity = -2.f;
        sp.drag = 2.f;
        sp.born = 0.42f;
        sp.life_min = 0.25f;
        sp.life_max = 0.5f;
        sp.count = count_of(L, 18);
        sp.colour0 = FxColour{ 1.f, 1.f, 1.f };
        sp.colour1 = FxColour{ 0.4f, 0.55f, 1.f };
        sp.size0 = 0.045f;
        sp.size1 = 0.012f;
        sp.emissive0 = 3.f;
        sp.emissive1 = 0.8f;
        sp.seed = L.seed;
        (void)fx_emit_spray(sp, te, out);
        shake_for(L, out, 0.42f, 0.3f, 0.08f);
    }
    if (!L.mini) {
        add_light(out, c, 4.f, FxColour{ 0.6f, 0.8f, 1.f }, 2.0f * fx_envelope(te, 0.05f, 0.4f, 0.5f));
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.03f * fx_envelope(te, 0.05f, 0.35f, 0.2f));
        mod.tremble_phase = te * 110.f;
    }
}

/* ============================================================ gb_brain_finger_print */

void look_brain_finger_print(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.08f, 0.8f, 0.5f);
    const Vec3 c{ L.front.x, L.front.y, 0.9f * L.scale };
    // 指紋の渦巻き（途切れた輪を重ねる）。
    for (int k = 0; k < (L.mini ? 3 : 6); ++k) {
        const float r = (0.1f + (0.065f * static_cast<float>(k))) * L.scale * 1.6f;
        ring_at(L, out, false, c, r, r, 0.f, 1.2f, 0.028f, (k % 2 == 0) ? L.e.base : L.e.tint, 1.8f, alpha * 0.9f, 22 + (3 * k), 3 + (k % 2), ((k % 2 == 0) ? 1.f : -1.f) * te * (0.8f + (0.2f * static_cast<float>(k))));
    }
    // 走査線が上から下へ。
    const float scan = fx_ramp(te, 0.1f, 0.75f);
    if ((scan > 0.f) && (scan < 1.f)) {
        const Vec3 p = c + on_screen(L.spec, 0.f, (0.5f - scan) * 0.9f * L.scale);
        put_bar(out, p, L.spec.cam_right, 0.8f * L.scale, 0.03f, FxColour{ 0.7f, 1.f, 0.7f }, 3.4f, 0.9f * alpha, FxPart::Spark);
        put_bar(out, p, L.spec.cam_right, 0.85f * L.scale, 0.1f, L.e.base, 1.2f, 0.25f * alpha, FxPart::Spark);
    }
    // 読み取った記憶の断片（小さな緑の点）。
    for (int i = 0; i < count_of(L, 12); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float a = 2.f * kPi * fx_rand(L.seed, id, 1u);
        const float r = (0.25f + (0.2f * fx_rand(L.seed, id, 2u))) * L.scale;
        const float age = te - (0.5f * fx_rand(L.seed, id, 3u));
        if ((age < 0.f) || (age > 0.6f)) {
            continue;
        }
        put(out, c + on_screen(L.spec, std::cos(a) * r, (std::sin(a) * r) + (0.3f * age)), 0.04f, L.e.tint, 2.4f, 1.f - fx_ramp(age, 0.3f, 0.6f));
    }
    if (!L.mini) {
        add_light(out, c, 3.f, L.e.base, 1.1f * alpha);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.012f * alpha);
        mod.tremble_phase = te * 45.f;
    }
}

/* ============================================================ gb_kanameishi */

void look_kanameishi(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float fall = 0.32f;
    const float alpha = 1.f - fx_ramp(te, 1.15f, 1.5f);
    const float z = (te < fall) ? fx_lerp(3.4f, 0.55f, (te / fall) * (te / fall)) : 0.55f;
    // 縄（注連縄）を巻いた大きな岩が落ちる。
    rock_cluster(out, Vec3{ L.front.x, L.front.y, z * L.scale }, 0.8f * L.scale, 0.3f, FxColour{ 0.58f, 0.57f, 0.6f }, FxColour{ 0.4f, 0.4f, 0.44f }, alpha, 0.5f, L.seed, FxPart::Debris);
    ring_at(L, out, true, Vec3{ L.front.x, L.front.y, (z + 0.02f) * L.scale }, 0.34f * L.scale, 0.34f * L.scale, 0.f, 2.f, 0.05f, FxColour{ 0.95f, 0.95f, 0.9f }, 1.2f, alpha, 20);
    if (te >= fall) {
        const float tb = te - fall;
        ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.9f * L.scale, fall, 0.45f, 0.06f, FxColour{ 0.9f, 0.8f, 0.6f }, 1.6f, 0.9f, 28);
        debris_burst(L, out, Vec3{ L.foot.x, L.foot.y, 0.3f }, 16, fall, 3.f, 1.3f, 0.08f * L.scale, FxColour{ 0.6f, 0.58f, 0.55f }, FxColour{ 0.4f, 0.38f, 0.36f }, 0.6f, 11u);
        FxSmoke dust;
        dust.origin = Vec3{ L.front.x, L.front.y, 0.1f };
        dust.radius = 0.3f * L.scale;
        dust.born = fall;
        dust.duration = 0.25f;
        dust.life = 0.9f;
        dust.count = count_of(L, 10);
        dust.rise = 0.4f;
        dust.size = 0.24f * L.scale;
        dust.grow = 2.f;
        dust.colour = FxColour{ 0.7f, 0.62f, 0.5f };
        dust.colour_end = FxColour{ 0.5f, 0.45f, 0.38f };
        dust.alpha = 0.65f;
        dust.emissive = 0.5f;
        dust.seed = L.seed;
        (void)fx_emit_smoke(dust, te, out);
        shake_for(L, out, fall, 0.5f, 0.15f);
        if (!L.mini) {
            FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
            mod.squash = std::max(mod.squash, 0.25f * fx_envelope(tb, 0.03f, 0.2f, 0.3f));
        }
    }
}

/* ============================================================ gb_steam */

void look_steam(const Look &L, FxFrame &out)
{
    const float te = L.te;
    FxSmoke puff;
    puff.origin = Vec3{ L.front.x, L.front.y, 0.15f };
    puff.radius = 0.25f * L.scale;
    puff.born = 0.f;
    puff.duration = 0.6f;
    puff.life = 1.0f;
    puff.count = count_of(L, 16);
    puff.rise = 0.8f;
    puff.size = 0.24f * L.scale;
    puff.grow = 2.2f;
    puff.colour = FxColour{ 1.f, 1.f, 1.f };
    puff.colour_end = FxColour{ 0.75f, 0.82f, 0.88f };
    puff.alpha = 0.72f;
    puff.emissive = 1.0f;
    puff.drift_x = 0.1f;
    puff.seed = L.seed;
    (void)fx_emit_smoke(puff, te, out);
    // 噴き出す筋と熱い水滴。
    for (int i = 0; i < count_of(L, 8); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float age = te - (0.3f * fx_rand(L.seed, id, 1u));
        if ((age < 0.f) || (age > 0.5f)) {
            continue;
        }
        const float a = 2.f * kPi * fx_rand(L.seed, id, 2u);
        const float r = 0.2f * fx_rand(L.seed, id, 3u) * L.scale;
        put_bar(out, Vec3{ L.front.x + (std::cos(a) * r), L.front.y + (std::sin(a) * r), (0.2f + (1.3f * age)) * L.scale }, Vec3{ 0.f, 0.f, 1.f }, 0.3f, 0.025f, FxColour{ 1.f, 1.f, 1.f }, 2.2f,
            1.f - fx_ramp(age, 0.3f, 0.5f), FxPart::Spark);
    }
    FxSpray sp;
    sp.origin = Vec3{ L.front.x, L.front.y, 0.3f };
    sp.cone = 1.2f;
    sp.dir = Vec3{ 0.f, 0.f, 1.f };
    sp.speed_min = 0.6f;
    sp.speed_max = 1.8f;
    sp.gravity = -4.f;
    sp.born = 0.05f;
    sp.duration = 0.4f;
    sp.life_min = 0.3f;
    sp.life_max = 0.6f;
    sp.count = count_of(L, 16);
    sp.colour0 = FxColour{ 0.8f, 0.95f, 1.f };
    sp.colour1 = FxColour{ 0.4f, 0.7f, 1.f };
    sp.size0 = 0.04f;
    sp.size1 = 0.015f;
    sp.emissive0 = 1.8f;
    sp.emissive1 = 0.6f;
    sp.part = FxPart::Droplet;
    sp.seed = L.seed + 9u;
    (void)fx_emit_spray(sp, te, out);
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        const float env = fx_envelope(te, 0.1f, 0.5f, 0.4f);
        mod.tremble = std::max(mod.tremble, 0.018f * env);
        mod.tremble_phase = te * 80.f;
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.8f }, 3.f, FxColour{ 1.f, 0.95f, 0.9f }, 0.9f * env);
    }
}

void flight_steam(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    for (int i = 0; i < 6; ++i) {
        const float ph = (ctx.t * 12.f) + static_cast<float>(i);
        put(out, f.pos + Vec3{ 0.07f * std::cos(ph), 0.07f * std::sin(ph * 1.3f), 0.06f * std::sin(ph) }, 0.12f + (0.04f * std::sin(ph * 2.f)), FxColour{ 1.f, 1.f, 1.f }, 2.0f, 0.85f, FxPart::OrbCore);
    }
    const float fly = std::max(ctx.fly, 1e-4f);
    for (int k = 1; k <= 7; ++k) {
        const float tt = ctx.t - (0.03f * static_cast<float>(k));
        if (tt <= 0.f) {
            break;
        }
        const Vec3 p = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(tt / fly));
        put(out, p + Vec3{ 0.f, 0.f, 0.03f * static_cast<float>(k) }, (0.1f + (0.02f * static_cast<float>(k))), FxColour{ 0.9f, 0.95f, 1.f }, 0.8f, 0.45f * (1.f - (static_cast<float>(k) / 8.f)), FxPart::Mist);
    }
    add_light(out, f.pos, 2.4f, e.base, 0.8f);
    mark_projectile(out, f.pos, f.dir);
}

/* ============================================================ gb_super_ego */

void look_super_ego(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.1f, 0.8f, 0.4f);
    const Vec3 halo{ L.front.x, L.front.y, 1.3f * L.scale };
    // 頭上の後光。
    ring_at(L, out, false, halo, 0.3f * L.scale, 0.3f * L.scale, 0.f, 1.3f, 0.04f, FxColour{ 1.f, 0.95f, 0.6f }, 2.8f, alpha, 28);
    ring_at(L, out, false, halo, 0.22f * L.scale, 0.22f * L.scale, 0.f, 1.3f, 0.025f, L.e.tint, 2.4f, alpha * 0.9f, 24, 2, te * 3.f);
    radial_rays(L, out, halo, L.mini ? 8 : 16, 0.36f * L.scale, 0.3f * L.scale * (0.7f + (0.3f * std::sin(te * 8.f))), 0.03f, FxColour{ 1.f, 0.95f, 0.7f }, 2.8f, alpha, te * 0.6f);
    // 上から押さえつける青い輪。
    for (int k = 0; k < (L.mini ? 1 : 3); ++k) {
        ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.f } + Vec3{ 0.f, 0.f, 0.f }, 0.6f * L.scale, 0.2f * L.scale, 0.12f * static_cast<float>(k), 0.5f, 0.035f, L.e.base, 2.2f, 0.9f, 24);
    }
    for (int i = 0; i < count_of(L, 12); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float age = te - (0.5f * fx_rand(L.seed, id, 1u));
        if ((age < 0.f) || (age > 0.7f)) {
            continue;
        }
        const float a = 2.f * kPi * fx_rand(L.seed, id, 2u);
        const float r = 0.3f * L.scale;
        put(out, Vec3{ L.foot.x + (std::cos(a) * r), L.foot.y + (std::sin(a) * r), (1.3f - (1.0f * age)) * L.scale }, 0.04f, L.e.base, 2.6f, 1.f - fx_ramp(age, 0.4f, 0.7f));
    }
    if (!L.mini) {
        add_light(out, halo, 3.6f, FxColour{ 0.8f, 0.9f, 1.f }, 1.6f * alpha);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.squash = std::max(mod.squash, 0.2f * fx_envelope(te - 0.3f, 0.1f, 0.3f, 0.3f));
    }
}

/* ============================================================ gb_dig_oil */

void look_dig_oil(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const FxColour oil{ 0.04f, 0.03f, 0.025f };
    // 噴き上がる石油。
    FxSpray sp;
    sp.origin = Vec3{ L.front.x, L.front.y, 0.05f };
    sp.dir = Vec3{ 0.f, 0.f, 1.f };
    sp.cone = 0.3f;
    sp.scatter = 0.08f * L.scale;
    sp.speed_min = 3.5f;
    sp.speed_max = 5.6f;
    sp.gravity = -9.f;
    sp.born = 0.f;
    sp.duration = 0.5f;
    sp.life_min = 0.7f;
    sp.life_max = 1.0f;
    sp.count = count_of(L, 44);
    sp.colour0 = oil;
    sp.colour1 = oil;
    sp.size0 = 0.08f;
    sp.size1 = 0.06f;
    sp.emissive0 = 0.35f;
    sp.emissive1 = 0.2f;
    sp.alpha0 = 1.f;
    sp.alpha1 = 1.f;
    sp.floor = FxFloor::Stop;
    sp.floor_z = 0.02f;
    sp.part = FxPart::Droplet;
    sp.seed = L.seed;
    (void)fx_emit_spray(sp, te, out);
    // つやの反射（琥珀色の小さな粒）。
    FxSpray gloss = sp;
    gloss.colour0 = FxColour{ 0.7f, 0.45f, 0.1f };
    gloss.colour1 = FxColour{ 0.4f, 0.25f, 0.05f };
    gloss.size0 = 0.035f;
    gloss.size1 = 0.02f;
    gloss.emissive0 = 1.4f;
    gloss.emissive1 = 0.6f;
    gloss.count = count_of(L, 20);
    gloss.seed = L.seed + 4u;
    (void)fx_emit_spray(gloss, te, out);
    // 床に広がる黒い水たまり。
    FxPuddle pd;
    pd.cx = L.foot.x;
    pd.cy = L.foot.y;
    pd.z = 0.02f;
    pd.radius = 0.46f * L.scale;
    pd.born = 0.25f;
    pd.spread = 0.4f;
    pd.hold = 0.7f;
    pd.fade = 0.5f;
    pd.colour = FxColour{ 0.1f, 0.075f, 0.04f };
    pd.emissive = 0.3f;
    pd.alpha = 0.95f;
    pd.seed = L.seed;
    (void)fx_emit_puddle(pd, te, out);
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.035f }, 0.1f, 0.5f * L.scale, 0.25f, 0.5f, 0.03f, FxColour{ 0.8f, 0.5f, 0.15f }, 1.2f, 0.7f, 22);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.7f }, 2.8f, FxColour{ 0.9f, 0.55f, 0.2f }, 0.7f * fx_envelope(te, 0.05f, 0.6f, 0.5f));
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.recoil = std::max(mod.recoil, 0.15f * fx_envelope(te, 0.04f, 0.15f, 0.3f));
        mod.away_x = 0.f;
        mod.away_y = 1.f;
    }
}

/* ============================================================ gb_alcohol */

void look_alcohol(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.1f, 0.9f, 0.5f);
    // 頭の上で傾く盃から、琥珀色の雫が降る。
    const Vec3 cup{ L.front.x, L.front.y, 1.3f * L.scale };
    glyph(L, out, shape_cup(), cup, 0.055f * L.scale, alpha, FxColour{ 0.85f, 0.2f, 0.2f }, 1.3f, 2, -0.5f * std::sin(te * 3.f) - 0.3f, "o", { FxColour{ 1.f, 0.85f, 0.4f } });
    FxSpray sp;
    sp.origin = cup + on_screen(L.spec, 0.12f * L.scale, -0.05f);
    sp.dir = Vec3{ 0.f, 0.f, -1.f };
    sp.cone = 0.4f;
    sp.speed_min = 0.4f;
    sp.speed_max = 1.0f;
    sp.gravity = -5.f;
    sp.born = 0.1f;
    sp.duration = 0.6f;
    sp.life_min = 0.4f;
    sp.life_max = 0.7f;
    sp.count = count_of(L, 28);
    sp.colour0 = FxColour{ 1.f, 0.8f, 0.35f };
    sp.colour1 = FxColour{ 0.9f, 0.55f, 0.15f };
    sp.size0 = 0.08f;
    sp.size1 = 0.05f;
    sp.emissive0 = 1.8f;
    sp.emissive1 = 0.8f;
    sp.floor = FxFloor::Vanish;
    sp.floor_z = 0.f;
    sp.part = FxPart::Droplet;
    sp.seed = L.seed;
    (void)fx_emit_spray(sp, te, out);
    // ふわふわ昇る泡と、ほろ酔いのハート。
    for (int i = 0; i < count_of(L, 10); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float age = te - (0.5f * fx_rand(L.seed, id, 1u));
        if ((age < 0.f) || (age > 1.0f)) {
            continue;
        }
        const float u = age / 1.0f;
        const float a = 2.f * kPi * fx_rand(L.seed, id, 2u);
        const float r = (0.1f + (0.25f * fx_rand(L.seed, id, 3u))) * L.scale;
        const Vec3 p{ L.foot.x + (std::cos(a) * r) + (0.06f * std::sin(age * 7.f)), L.foot.y + (std::sin(a) * r), (0.2f + (1.0f * u)) * L.scale };
        put(out, p, 0.05f + (0.03f * fx_rand(L.seed, id, 4u)), FxColour{ 1.f, 0.85f, 0.7f }, 1.8f, 0.7f * (1.f - fx_ramp(u, 0.7f, 1.f)), FxPart::Mist);
    }
    if (!L.mini) {
        glyph(L, out, shape_heart(), Vec3{ L.front.x, L.front.y, 0.9f * L.scale } + on_screen(L.spec, 0.28f * L.scale + (0.04f * std::sin(te * 5.f)), 0.1f * te), 0.012f, fx_envelope(te - 0.2f, 0.1f, 0.4f, 0.4f),
            FxColour{ 1.f, 0.5f, 0.6f }, 2.2f);
        // ほろ酔いで体がゆっくり揺れる。
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        const float env = fx_envelope(te - 0.1f, 0.2f, 0.8f, 0.4f);
        mod.tremble = std::max(mod.tremble, 0.05f * env);
        mod.tremble_phase = te * 5.f;
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.9f }, 2.8f, FxColour{ 1.f, 0.75f, 0.4f }, 0.9f * alpha);
    }
}

/* ============================================================ gb_pit_fall */

void look_pit_fall(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float open = fx_ramp(te, 0.f, 0.3f);
    const float alpha = 1.f - fx_ramp(te, 1.0f, 1.4f);
    // 床の穴：黒い円盤と、縁の盛り上がった土。
    FxPuddle hole;
    hole.cx = L.foot.x;
    hole.cy = L.foot.y;
    hole.z = 0.025f;
    hole.radius = 0.46f * L.scale;
    hole.born = 0.f;
    hole.spread = 0.3f;
    hole.hold = 0.8f;
    hole.fade = 0.4f;
    hole.colour = FxColour{ 0.02f, 0.015f, 0.01f };
    hole.emissive = 0.f;
    hole.alpha = 1.f;
    hole.thickness = 0.03f;
    hole.seed = L.seed;
    (void)fx_emit_puddle(hole, te, out);
    const int n = L.mini ? 14 : 28;
    for (int i = 0; i < n; ++i) {
        const float a = (2.f * kPi * static_cast<float>(i) / static_cast<float>(n)) + (0.2f * fx_rand(L.seed, static_cast<std::uint32_t>(i), 1u));
        const float r = (0.46f + (0.04f * fx_rand(L.seed, static_cast<std::uint32_t>(i), 2u))) * L.scale * open;
        put(out, Vec3{ L.foot.x + (std::cos(a) * r), L.foot.y + (std::sin(a) * r), 0.05f }, 0.07f, FxColour{ 0.55f, 0.4f, 0.25f }, 0.7f, alpha, FxPart::Debris);
    }
    // 土煙と、穴へ落ちる土くれ。
    FxSmoke dust;
    dust.origin = Vec3{ L.front.x, L.front.y, 0.08f };
    dust.radius = 0.28f * L.scale;
    dust.born = 0.f;
    dust.duration = 0.3f;
    dust.life = 0.8f;
    dust.count = count_of(L, 9);
    dust.rise = 0.4f;
    dust.size = 0.18f * L.scale;
    dust.grow = 2.f;
    dust.colour = FxColour{ 0.65f, 0.5f, 0.35f };
    dust.colour_end = FxColour{ 0.45f, 0.36f, 0.26f };
    dust.alpha = 0.6f;
    dust.emissive = 0.6f;
    dust.seed = L.seed;
    (void)fx_emit_smoke(dust, te, out);
    debris_burst(L, out, Vec3{ L.foot.x, L.foot.y, 0.1f }, 8, 0.05f, 1.6f, 1.0f, 0.06f * L.scale, FxColour{ 0.6f, 0.45f, 0.3f }, FxColour{ 0.4f, 0.3f, 0.2f }, 0.5f, 6u);
    if (!L.mini) {
        // 体が穴へ沈み、這い上がる。
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        const float sink = fx_envelope(te, 0.2f, 0.4f, 0.4f);
        mod.lift = std::min(mod.lift, -0.42f * sink);
        mod.squash = std::max(mod.squash, 0.1f * sink);
    }
}

/* ============================================================ gb_berserk */

void look_berserk(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.05f, 0.8f, 0.4f);
    // 激しく昇る赤い気。
    WhirlParams w;
    w.height = 1.2f;
    w.radius0 = 0.22f;
    w.radius1 = 0.14f;
    w.speed = 9.f;
    w.rise = 1.0f;
    w.count = 44;
    w.life = 0.9f;
    w.duration = 0.6f;
    w.z0 = 0.02f;
    w.c0 = FxColour{ 1.f, 0.65f, 0.3f };
    w.c1 = FxColour{ 1.f, 0.12f, 0.1f };
    w.size = 0.065f;
    w.emissive = 2.8f;
    whirl(L, out, w);
    // 頭に浮かぶ怒りの印。
    for (int k = 0; k < (L.mini ? 1 : 2); ++k) {
        const float born = 0.15f * static_cast<float>(k);
        const float age = te - born;
        if ((age < 0.f) || (age > 0.7f)) {
            continue;
        }
        const float pop = 1.f + (0.35f * std::sin(age * 24.f) * (1.f - fx_ramp(age, 0.1f, 0.3f)));
        glyph(L, out, shape_bang_anger(), Vec3{ L.front.x, L.front.y, 1.3f * L.scale } + on_screen(L.spec, ((k == 0) ? 0.28f : -0.26f) * L.scale, ((k == 0) ? 0.05f : -0.08f)), 0.034f * L.scale * pop,
            fx_envelope(age, 0.04f, 0.4f, 0.25f), FxColour{ 1.f, 0.15f, 0.12f }, 2.0f, 2, 0.f);
    }
    // 頭から噴く蒸気。
    FxSmoke steam;
    steam.origin = Vec3{ L.front.x, L.front.y, 1.0f * L.scale };
    steam.radius = 0.1f * L.scale;
    steam.born = 0.1f;
    steam.duration = 0.4f;
    steam.life = 0.6f;
    steam.count = count_of(L, 7);
    steam.rise = 0.7f;
    steam.size = 0.12f * L.scale;
    steam.grow = 1.8f;
    steam.colour = FxColour{ 1.f, 0.9f, 0.85f };
    steam.colour_end = FxColour{ 0.8f, 0.5f, 0.45f };
    steam.alpha = 0.6f;
    steam.emissive = 1.0f;
    steam.seed = L.seed;
    (void)fx_emit_smoke(steam, te, out);
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.15f, 0.7f * L.scale, 0.f, 0.5f, 0.04f, FxColour{ 1.f, 0.2f, 0.15f }, 2.6f, 0.9f * alpha, 24);
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.15f, 0.7f * L.scale, 0.2f, 0.5f, 0.03f, FxColour{ 1.f, 0.55f, 0.2f }, 2.4f, 0.8f * alpha, 24);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.7f }, 3.4f, FxColour{ 1.f, 0.25f, 0.15f }, 1.8f * alpha);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.045f * alpha);
        mod.tremble_phase = te * 120.f;
    }
}

/* ============================================================ gb_activ_trap */

void look_activ_trap(const Look &L, FxFrame &out)
{
    const float te = L.te;
    // 床の仕掛け板が「カチッ」と沈んで、火花が飛ぶ。
    const float click = fx_envelope(te, 0.02f, 0.25f, 0.3f);
    const float half = 0.34f * L.scale;
    const Vec3 corner[4] = { { L.foot.x - half, L.foot.y - half, 0.03f }, { L.foot.x + half, L.foot.y - half, 0.03f }, { L.foot.x + half, L.foot.y + half, 0.03f },
        { L.foot.x - half, L.foot.y + half, 0.03f } };
    for (int s = 0; s < 4; ++s) {
        floor_dots(out, corner[s], corner[(s + 1) % 4], 1.f, 0.07f, FxColour{ 1.f, 0.85f, 0.3f }, 2.0f * click, 0.9f * click);
    }
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.6f * L.scale, 0.02f, 0.4f, 0.04f, FxColour{ 1.f, 0.85f, 0.3f }, 2.4f, 0.9f, 22);
    FxSpray sp;
    sp.origin = Vec3{ L.foot.x, L.foot.y, 0.08f };
    sp.dir = Vec3{ 0.f, 0.f, 1.f };
    sp.cone = 1.0f;
    sp.speed_min = 1.f;
    sp.speed_max = 2.6f;
    sp.gravity = -6.f;
    sp.born = 0.02f;
    sp.life_min = 0.2f;
    sp.life_max = 0.45f;
    sp.count = count_of(L, 14);
    sp.colour0 = FxColour{ 1.f, 1.f, 0.7f };
    sp.colour1 = FxColour{ 1.f, 0.55f, 0.15f };
    sp.size0 = 0.04f;
    sp.size1 = 0.012f;
    sp.emissive0 = 3.f;
    sp.emissive1 = 0.8f;
    sp.seed = L.seed;
    (void)fx_emit_spray(sp, te, out);
    // 頭の上の「！」が跳ねる。
    const float bounce = 0.06f * std::fabs(std::sin(te * 10.f)) * (1.f - fx_ramp(te, 0.3f, 0.7f));
    glyph(L, out, shape_exclaim(), Vec3{ L.front.x, L.front.y, (1.25f * L.scale) + bounce }, 0.045f * L.scale, fx_envelope(te - 0.05f, 0.04f, 0.4f, 0.3f), FxColour{ 1.f, 0.85f, 0.2f }, 2.6f, 2, 0.f);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.6f }, 2.8f, FxColour{ 1.f, 0.85f, 0.3f }, 1.3f * click);
    }
}

} // namespace gb
} // namespace looks
} // namespace hd2d
