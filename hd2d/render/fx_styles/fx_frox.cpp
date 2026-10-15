/*!
 * @file fx_frox.cpp
 * @brief Frox（FroxComposband）にあって変愚に無い属性の見た目（`fc_` で始まる名前。23 個）。
 *
 * 属性の名前と、原作（`frox/src/gf.c` の `gf_info`）の説明・色の対応は `presentation/bridge/attack_fx_core_attr_aliases.h`。
 * 変愚と同じ名前の属性・寄せる属性（`arrow`→`missile` など）はここへ来ない。
 *
 * | 属性 | 原作の説明（色） | 見た目 |
 * |---|---|---|
 * | `fc_storm` | 嵐の風（青） | 白青の粒が螺旋を描いて昇る竜巻と、横に走る風の筋。玉は風をまとって回る |
 * | `fc_rock` | 岩（薄茶） | 上から岩が落ちて砕ける。玉は回る岩 |
 * | `fc_amnesia` | 記憶喪失（暗） | ？ が頭の上へ昇って灰になる |
 * | `fc_blind` | 盲目（暗） | 目が閉じて、黒い墨が頭を包む |
 * | `fc_paralysis` | 麻痺（紫） | 体の周りに紫の電気が走って、ぴたりと止まる（床に紫の輪） |
 * | `fc_antimagic` | 反魔法（赤） | 赤い禁止の印（円に斜線）。魔力の粒が吸われて消える |
 * | `fc_unholy_word` | 不浄の言葉（暗） | 床に逆五芒星の紋様、黒紫の炎の柱 |
 * | `fc_unlife` | 反生命（暗） | 青緑のどくろと、立ちのぼる亡霊の靄。体から生気が抜ける |
 * | `fc_web` | 蜘蛛の巣（灰） | 白い糸の放射状の巣が体を覆う。玉は糸の玉 |
 * | `fc_isolation` | 隔離（水色） | 球殻（ガラスの泡）が上から閉じて、体を閉じ込める |
 * | `fc_quake` | 地震（薄茶） | 床にひびが走り、岩が跳ね、画面が揺れる |
 * | `fc_death_touch` | 死の接触（暗） | 灰色の手が触れて、体が灰色にしぼむ |
 * | `fc_steal` | 盗み（白） | 硬貨が飛び出して、撃った者へ弧を描いて飛ぶ |
 * | `fc_chicken` | にわとり（黄） | 白い煙と羽根、頭の上ににわとり |
 * | `fc_bomb` | 爆弾（灰） | 爆弾が落ちて導火線が燃え、爆発（黒い煙と破片） |
 * | `fc_air` | 空気（水色） | 透き通った空気の球が広がり、水平の風の筋が走って体がのけぞる |
 * | `fc_gain_exp` | 経験値（緑） | 金緑の星が二重の螺旋で昇り、光の柱が立つ |
 * | `fc_eldritch` と `_stun` `_drain` `_dispel` `_confuse` `_howl` | 名状しがたい恐怖（紫ほか） | 頭の周りに目がいくつも開き、紫の渦。派生ごとに星・吸収・輪・渦・轟きが重なる |
 *
 * **原作より情報を増やさない**（効いたか・耐えたかで絵を変えない）。
 */
#include "render/fx_styles/fx_look_motifs.h"

#include <algorithm>
#include <cmath>

namespace hd2d {

namespace {

using namespace looks;

//! 撃った者から対象への水平の向き（実体の「のけぞり」に使う）。
void away_from_source(const Look &L, float &ax, float &ay)
{
    const float dx = static_cast<float>(L.gx - L.spec.src_gx);
    const float dy = static_cast<float>(L.gy - L.spec.src_gy);
    const float len = std::sqrt((dx * dx) + (dy * dy));
    if (len < 1e-3f) {
        ax = 0.f;
        ay = 1.f;
    } else {
        ax = dx / len;
        ay = dy / len;
    }
}

//! 画面を揺らす（範囲の周りの小さな版では揺らさない）。
void shake_if(const Look &L, FxFrame &out, float start, float seconds, float strength)
{
    if (!L.mini) {
        fx_add_shake(out, fx_shake_envelope(L.te, start, seconds, strength));
    }
}

/* ============================================================ 飛んでいる姿 */

//! 玉のまわりを小さな粒が回る。
void orbit_flight(const FxStyleCtx &ctx, const Entry &e, int n, float radius, float speed, float size, const FxColour &c, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    const FxColour core = fx_mix(FxColour{ 1.f, 1.f, 1.f }, e.base, 0.5f);
    put(out, f.pos, 0.13f, core, 3.4f, 1.f, FxPart::OrbCore);
    const Vec3 side = unit_or(cross(f.dir, Vec3{ 0.f, 0.f, 1.f }), Vec3{ 0.f, 1.f, 0.f });
    const Vec3 up = unit_or(cross(side, f.dir), Vec3{ 0.f, 0.f, 1.f });
    for (int i = 0; i < n; ++i) {
        const float ang = (ctx.t * speed) + (2.f * kPi * static_cast<float>(i) / static_cast<float>(n));
        const Vec3 p = f.pos + (side * (std::cos(ang) * radius)) + (up * (std::sin(ang) * radius * 0.8f));
        put(out, p, size, c, 2.4f, 0.95f, FxPart::OrbCore);
        const float ang2 = ang - 0.55f;
        put(out, f.pos + (side * (std::cos(ang2) * radius)) + (up * (std::sin(ang2) * radius * 0.8f)), size * 0.7f, e.base, 1.4f, 0.5f, FxPart::OrbTail);
    }
    add_light(out, f.pos, 2.8f, e.base, 1.3f);
    mark_projectile(out, f.pos, f.dir);
}

void flight_storm(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    orbit_flight(ctx, e, 6, 0.2f, 22.f, 0.06f, FxColour{ 0.85f, 0.95f, 1.f }, out);
}

void flight_rock(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    rock_cluster(out, f.pos, 0.34f, ctx.t * 14.f, e.base, e.tint, 1.f, 0.9f, ctx.spec.seed, FxPart::OrbCore);
    for (int k = 1; k <= 5; ++k) {
        const float tt = ctx.t - (0.03f * static_cast<float>(k));
        if (tt <= 0.f) {
            break;
        }
        const Vec3 p = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(tt / std::max(ctx.fly, 1e-4f)));
        const float fr = 1.f - (static_cast<float>(k) / 6.f);
        put(out, p + Vec3{ 0.f, 0.f, 0.05f * static_cast<float>(k) }, 0.07f * fr, e.tint, 0.2f, 0.7f * fr, FxPart::OrbTail);
    }
    mark_projectile(out, f.pos, f.dir);
}

void flight_web(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    put(out, f.pos, 0.1f, FxColour{ 0.95f, 0.95f, 1.f }, 2.2f, 1.f, FxPart::OrbCore);
    for (int i = 0; i < 9; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float a = (ctx.t * 8.f) + (2.f * kPi * fx_rand(ctx.spec.seed, id, 1u));
        const float b = 2.f * kPi * fx_rand(ctx.spec.seed, id, 2u);
        const Vec3 d{ std::cos(a) * std::cos(b), std::sin(a) * std::cos(b), std::sin(b) };
        put_bar(out, f.pos + (d * 0.1f), d, 0.2f, 0.02f, e.tint, 1.4f, 0.9f, FxPart::OrbCore);
    }
    for (int k = 1; k <= 6; ++k) {
        const float tt = ctx.t - (0.025f * static_cast<float>(k));
        if (tt <= 0.f) {
            break;
        }
        const Vec3 p = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(tt / std::max(ctx.fly, 1e-4f)));
        put(out, p, 0.035f, e.tint, 1.f, 0.6f * (1.f - (static_cast<float>(k) / 7.f)), FxPart::OrbTail);
    }
    add_light(out, f.pos, 2.2f, e.tint, 0.8f);
    mark_projectile(out, f.pos, f.dir);
}

void flight_bomb(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    const Look L = make_look(ctx.spec, e, ctx.spec.centre_gx, ctx.spec.centre_gy, ctx.t, 1.f, false, ctx.spec.seed);
    glyph(L, out, shape_bomb(), f.pos, 0.026f, 1.f, FxColour{ 0.07f, 0.07f, 0.09f }, 0.5f, 2, ctx.t * 6.f, "yo", { FxColour{ 1.f, 0.7f, 0.2f }, FxColour{ 0.4f, 0.4f, 0.5f } });
    FxSpray sp;
    sp.origin = f.pos + on_screen(ctx.spec, 0.08f, 0.14f);
    sp.cone = kPi;
    sp.speed_min = 0.4f;
    sp.speed_max = 1.2f;
    sp.gravity = -1.f;
    sp.born = 0.f;
    sp.duration = std::max(ctx.fly, 0.1f);
    sp.life_min = 0.12f;
    sp.life_max = 0.22f;
    sp.count = 16;
    sp.colour0 = FxColour{ 1.f, 0.85f, 0.3f };
    sp.colour1 = FxColour{ 1.f, 0.3f, 0.05f };
    sp.size0 = 0.035f;
    sp.size1 = 0.01f;
    sp.emissive0 = 3.f;
    sp.emissive1 = 1.f;
    sp.seed = ctx.spec.seed + 5u;
    (void)fx_emit_spray(sp, ctx.t, out);
    add_light(out, f.pos, 2.2f, FxColour{ 1.f, 0.6f, 0.2f }, 0.9f);
    mark_projectile(out, f.pos, f.dir);
}

void flight_air(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    const Look L = make_look(ctx.spec, e, ctx.spec.centre_gx, ctx.spec.centre_gy, ctx.t, 1.f, false, ctx.spec.seed);
    sphere_shell(L, out, f.pos, 0.2f, 34, 0.04f, FxColour{ 0.8f, 0.95f, 1.f }, 1.8f, 0.8f, ctx.t * 9.f);
    put(out, f.pos, 0.07f, FxColour{ 1.f, 1.f, 1.f }, 3.f, 0.9f, FxPart::OrbCore);
    for (int k = 1; k <= 7; ++k) {
        const float tt = ctx.t - (0.02f * static_cast<float>(k));
        if (tt <= 0.f) {
            break;
        }
        const Vec3 p = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(tt / std::max(ctx.fly, 1e-4f)));
        put_bar(out, p + on_screen(ctx.spec, 0.f, 0.08f * std::sin(static_cast<float>(k) * 2.1f)), f.dir, 0.18f, 0.02f, e.tint, 1.4f, 0.55f * (1.f - (static_cast<float>(k) / 8.f)),
            FxPart::OrbTail);
    }
    add_light(out, f.pos, 2.4f, e.base, 0.9f);
    mark_projectile(out, f.pos, f.dir);
}

void flight_steal(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    const Look L = make_look(ctx.spec, e, ctx.spec.centre_gx, ctx.spec.centre_gy, ctx.t, 1.f, false, ctx.spec.seed);
    glyph(L, out, shape_coin(), f.pos, 0.034f, 1.f, e.base, 1.1f, 2, 0.f, "o", { e.tint });
    for (int k = 1; k <= 6; ++k) {
        const float tt = ctx.t - (0.025f * static_cast<float>(k));
        if (tt <= 0.f) {
            break;
        }
        const Vec3 p = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(tt / std::max(ctx.fly, 1e-4f)));
        put(out, p, 0.05f * (1.f - (static_cast<float>(k) / 7.f)), e.tint, 2.f, 0.7f, FxPart::OrbTail);
    }
    add_light(out, f.pos, 2.4f, e.tint, 1.0f);
    mark_projectile(out, f.pos, f.dir);
}

/* ============================================================ 見た目 */

void look_storm(const Look &L, FxFrame &out)
{
    const float te = L.te;
    WhirlParams w;
    w.height = 1.45f;
    w.radius0 = 0.05f;
    w.radius1 = 0.5f;
    w.speed = 13.f;
    w.count = 110;
    w.life = 1.3f;
    w.rise = 0.2f;
    w.duration = 0.5f;
    w.z0 = 0.02f;
    w.c0 = FxColour{ 0.9f, 0.97f, 1.f };
    w.c1 = L.e.base;
    w.emissive = 2.6f;
    w.size = 0.065f;
    whirl(L, out, w);
    // 横へ走る風の筋。
    const int n = L.mini ? 2 : 6;
    for (int k = 0; k < n; ++k) {
        const float ang = (te * 11.f) + (static_cast<float>(k) * 1.05f);
        const float h = (0.15f + (0.17f * static_cast<float>(k))) * L.scale;
        const float r = (0.2f + (0.035f * static_cast<float>(k))) * L.scale;
        const Vec3 p{ L.foot.x + (std::cos(ang) * r), L.foot.y + (std::sin(ang) * r), h };
        const Vec3 tangent{ -std::sin(ang), std::cos(ang), 0.f };
        put_bar(out, p, tangent, 0.3f * L.scale, 0.022f, FxColour{ 0.85f, 0.95f, 1.f }, 2.6f, fx_envelope(te - (0.05f * static_cast<float>(k)), 0.1f, 0.6f, 0.3f));
    }
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.8f }, 3.4f, L.e.base, 1.2f * fx_envelope(te, 0.15f, 0.6f, 0.4f));
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.02f * fx_envelope(te, 0.1f, 0.7f, 0.3f));
        mod.tremble_phase = te * 70.f;
    }
}

void look_rock(const Look &L, FxFrame &out)
{
    const int n = L.mini ? 1 : 3;
    for (int k = 0; k < n; ++k) {
        const std::uint32_t id = static_cast<std::uint32_t>(k);
        const float born = 0.12f * static_cast<float>(k);
        const float fall = 0.32f;
        const float age = L.te - born;
        const float ox = (k == 0) ? 0.f : (((k == 1) ? -0.17f : 0.17f) * L.scale);
        const float oy = (k == 0) ? 0.f : (((k == 1) ? 0.1f : -0.1f) * L.scale);
        if ((age >= 0.f) && (age < fall)) {
            const float u = age / fall;
            const float z = fx_lerp(2.3f, 0.34f, u * u) * L.scale;
            rock_cluster(out, Vec3{ L.foot.x + ox, L.foot.y + oy, z }, 0.38f * L.scale, age * 9.f, L.e.base, L.e.tint, 1.f, 0.9f, L.seed + id);
        } else if (age >= fall) {
            debris_burst(L, out, Vec3{ L.foot.x + ox, L.foot.y + oy, 0.3f }, 10, born + fall, 2.8f, 1.2f, 0.09f * L.scale, L.e.base, L.e.tint, 0.8f, 17u + id);
            FxSmoke smoke;
            smoke.origin = Vec3{ L.front.x + ox, L.front.y + oy, 0.1f };
            smoke.radius = 0.14f * L.scale;
            smoke.born = born + fall;
            smoke.duration = 0.08f;
            smoke.life = 0.7f;
            smoke.count = count_of(L, 4);
            smoke.rise = 0.3f;
            smoke.size = 0.13f * L.scale;
            smoke.grow = 2.f;
            smoke.colour = FxColour{ 0.6f, 0.5f, 0.38f };
            smoke.colour_end = FxColour{ 0.4f, 0.34f, 0.27f };
            smoke.alpha = 0.6f;
            smoke.emissive = 0.5f;
            smoke.seed = L.seed + id;
            (void)fx_emit_smoke(smoke, L.te, out);
        }
    }
    shake_if(L, out, 0.3f, 0.35f, 0.07f);
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.squash = std::max(mod.squash, 0.2f * fx_envelope(L.te - 0.3f, 0.04f, 0.1f, 0.3f));
    }
}

void look_amnesia(const Look &L, FxFrame &out)
{
    const int n = L.mini ? 1 : 3;
    for (int k = 0; k < n; ++k) {
        const float born = 0.14f * static_cast<float>(k);
        const float life = 1.1f;
        const float age = L.te - born;
        if ((age < 0.f) || (age >= life)) {
            continue;
        }
        const float u = age / life;
        const float x = ((k - 1) * 0.18f) + (0.05f * std::sin((age * 7.f) + static_cast<float>(k)));
        const Vec3 c = Vec3{ L.front.x, L.front.y, head_z(L) + (0.38f * u) } + on_screen(L.spec, x * L.scale, 0.f);
        const float alpha = fx_envelope(age, 0.15f, 0.5f, 0.45f);
        glyph(L, out, shape_question(), c, 0.04f * L.scale, alpha * (1.f - (0.5f * fx_ramp(u, 0.6f, 1.f))), (k % 2 == 0) ? L.e.base : L.e.tint, 1.7f);
        // 後半は灰になってほどける。
        if (u > 0.55f) {
            for (int j = 0; j < 6; ++j) {
                const std::uint32_t id = static_cast<std::uint32_t>((k * 8) + j);
                const float a2 = 2.f * kPi * fx_rand(L.seed, id, 1u);
                const float r = (u - 0.55f) * 0.5f * (0.4f + fx_rand(L.seed, id, 2u));
                put(out, c + on_screen(L.spec, std::cos(a2) * r, std::sin(a2) * r - (0.2f * (u - 0.55f))), 0.03f, L.e.tint, 1.2f, 1.f - fx_ramp(u, 0.7f, 1.f));
            }
        }
    }
    // 頭から抜けていく灰色の粒。
    WhirlParams w;
    w.z0 = 0.7f;
    w.height = 0.5f;
    w.radius0 = 0.12f;
    w.radius1 = 0.3f;
    w.speed = -4.f;
    w.rise = 0.35f;
    w.count = 16;
    w.life = 1.0f;
    w.c0 = L.e.tint;
    w.c1 = L.e.base;
    w.size = 0.04f;
    w.emissive = 1.2f;
    whirl(L, out, w);
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.012f * fx_envelope(L.te, 0.2f, 0.5f, 0.4f));
        mod.tremble_phase = L.te * 24.f;
    }
}

void look_blind(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const int stage = (te < 0.28f) ? 0 : ((te < 0.4f) ? 1 : 2);
    const float alpha = fx_envelope(te, 0.06f, 0.75f, 0.35f);
    const Vec3 c{ L.front.x, L.front.y, head_z(L) + 0.3f };
    glyph(L, out, shape_eye(stage), c, 0.085f * L.scale, alpha, FxColour{ 0.6f, 0.55f, 0.7f }, 0.9f, 2, 0.f, "oO", { FxColour{ 0.95f, 0.95f, 0.85f }, FxColour{ 0.8f, 0.04f, 0.04f } });
    // 閉じたあとの黒い墨。
    FxSmoke smoke;
    smoke.origin = Vec3{ L.front.x, L.front.y, head_z(L) - 0.05f };
    smoke.radius = 0.16f * L.scale;
    smoke.born = 0.36f;
    smoke.duration = 0.35f;
    smoke.life = 0.85f;
    smoke.count = count_of(L, 12);
    smoke.rise = 0.12f;
    smoke.size = 0.2f * L.scale;
    smoke.grow = 1.8f;
    smoke.colour = FxColour{ 0.06f, 0.04f, 0.1f };
    smoke.colour_end = FxColour{ 0.02f, 0.02f, 0.05f };
    smoke.alpha = 0.92f;
    smoke.emissive = 0.35f;
    smoke.seed = L.seed;
    (void)fx_emit_smoke(smoke, te, out);
    // 視界がすぼまる合図：縮んでいく輪。
    ring_at(L, out, false, Vec3{ L.front.x, L.front.y, 0.7f }, 0.7f * L.scale, 0.08f, 0.2f, 0.5f, 0.04f, fx_scale(L.e.tint, 1.3f), 1.0f, 0.8f, 26);
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.grey = std::max(mod.grey, 0.45f * fx_envelope(te, 0.3f, 0.5f, 0.4f));
    }
}

void look_paralysis(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float crackle = fx_envelope(te, 0.04f, 0.4f, 0.12f);
    const int n = L.mini ? 2 : 5;
    for (int k = 0; k < n; ++k) {
        const float a = (2.f * kPi * static_cast<float>(k) / static_cast<float>(n)) + (0.4f * static_cast<float>(k));
        const float r = 0.27f * L.scale;
        FxLightning bolt;
        bolt.a = Vec3{ L.foot.x + (std::cos(a) * r), L.foot.y + (std::sin(a) * r), 0.05f };
        bolt.b = Vec3{ L.foot.x + (std::cos(a + 0.5f) * r), L.foot.y + (std::sin(a + 0.5f) * r), 0.95f * L.scale };
        bolt.segments = 7;
        bolt.jitter = 0.16f;
        bolt.core = 0.035f;
        bolt.glow = 0.12f;
        bolt.core_colour = FxColour{ 1.f, 0.95f, 1.f };
        bolt.glow_colour = L.e.base;
        bolt.emissive = 3.f;
        bolt.alpha = crackle;
        bolt.branches = 0;
        bolt.flicker_hz = 24.f;
        bolt.seed = L.seed + static_cast<std::uint32_t>(k);
        if (crackle > 0.01f) {
            (void)fx_emit_lightning(bolt, te, out);
        }
    }
    // 動きが止まったあとの床の輪と、固まった印の粒。
    const float held = fx_envelope(te - 0.45f, 0.1f, 0.5f, 0.45f);
    if (held > 0.01f) {
        ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.03f }, 0.42f * L.scale, 0.42f * L.scale, 0.f, 2.f, 0.03f, L.e.base, 1.8f, held, 22, 2);
        for (int i = 0; i < count_of(L, 8); ++i) {
            const float a = (2.f * kPi * static_cast<float>(i) / 8.f) + 0.3f;
            put(out, Vec3{ L.foot.x + (std::cos(a) * 0.3f * L.scale), L.foot.y + (std::sin(a) * 0.3f * L.scale), (0.2f + (0.1f * static_cast<float>(i % 3))) * L.scale }, 0.04f,
                L.e.tint, 2.f, held * 0.8f);
        }
    }
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.6f }, 3.f, L.e.base, 1.6f * (crackle + (0.5f * held)));
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.035f * crackle);
        mod.tremble_phase = te * 130.f;
        mod.grey = std::max(mod.grey, 0.3f * held);
    }
}

void look_antimagic(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.1f, 0.6f, 0.3f);
    const Vec3 c{ L.front.x, L.front.y, 0.62f * L.scale };
    const float R = 0.36f * L.scale;
    // 禁止の印：赤い輪と斜線。
    ring_at(L, out, false, c, R, R, 0.f, 1.2f, 0.04f, L.e.base, 2.2f * alpha, alpha, 28, 1);
    const Vec3 diag = normalize(on_screen(L.spec, 1.f, -1.f));
    const float grow = ease_out(te / 0.18f);
    put_bar(out, c, diag, 2.f * R * grow, 0.05f, L.e.base, 2.4f, alpha);
    // 魔力の粒が輪へ吸われて、はじけて消える。
    const int n = count_of(L, 18);
    for (int i = 0; i < n; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float born = 0.02f * static_cast<float>(i % 9) + 0.05f * fx_rand(L.seed, id, 1u);
        const float age = te - born;
        const float life = 0.5f;
        if ((age < 0.f) || (age >= life)) {
            continue;
        }
        const float u = age / life;
        const float ang = 2.f * kPi * fx_rand(L.seed, id, 2u);
        const float rr = fx_lerp(1.1f, 0.95f, u * u) * R * 1.5f * (1.f - (0.7f * u * u));
        const FxColour col = (i % 2 == 0) ? L.e.tint : L.e.tint2;
        put(out, c + on_screen(L.spec, std::cos(ang) * rr, std::sin(ang) * rr), 0.045f, col, 2.2f, 1.f - fx_ramp(u, 0.7f, 1.f));
    }
    // 打ち消されたときの赤い火花。
    FxSpray sp;
    sp.origin = c;
    sp.cone = kPi;
    sp.speed_min = 0.5f;
    sp.speed_max = 1.4f;
    sp.gravity = -2.f;
    sp.drag = 2.f;
    sp.born = 0.45f;
    sp.life_min = 0.15f;
    sp.life_max = 0.3f;
    sp.count = count_of(L, 12);
    sp.colour0 = FxColour{ 1.f, 0.7f, 0.6f };
    sp.colour1 = L.e.base;
    sp.size0 = 0.04f;
    sp.size1 = 0.01f;
    sp.emissive0 = 2.8f;
    sp.emissive1 = 0.5f;
    sp.seed = L.seed;
    (void)fx_emit_spray(sp, te, out);
    if (!L.mini) {
        add_light(out, c, 3.f, L.e.base, 1.4f * alpha);
    }
}

void look_unholy_word(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.05f, 0.9f, 0.55f);
    if (alpha <= 0.01f) {
        return;
    }
    const float R = 0.72f * L.scale;
    const Vec3 c{ L.foot.x, L.foot.y, 0.03f };
    const float draw = fx_ramp(te, 0.f, 0.4f);
    const float flick = 1.f + (0.2f * std::sin(te * 22.f));
    // 外の輪。
    const int segs = L.mini ? 18 : 40;
    const int shown = static_cast<int>(std::ceil(draw * static_cast<float>(segs)));
    for (int i = 0; i < shown; ++i) {
        const float a = 2.f * kPi * static_cast<float>(i) / static_cast<float>(segs);
        FxVoxel v;
        v.center = c + Vec3{ std::cos(a) * R, std::sin(a) * R, 0.f };
        v.size = Vec3{ 0.08f, 0.08f, 0.02f };
        v.r = L.e.tint.r;
        v.g = L.e.tint.g;
        v.b = L.e.tint.b;
        v.emissive = 2.f * flick;
        v.alpha = alpha;
        v.part = FxPart::Puddle;
        out.voxels.push_back(v);
    }
    // 逆五芒星（頂点が下）。
    Vec3 pts[5];
    for (int k = 0; k < 5; ++k) {
        const float a = (-0.5f * kPi) + (2.f * kPi * static_cast<float>(k) / 5.f);
        pts[k] = c + Vec3{ std::cos(a) * R * 0.95f, std::sin(a) * R * 0.95f, 0.f };
    }
    if (!L.mini) {
        for (int k = 0; k < 5; ++k) {
            floor_dots(out, pts[k], pts[(k + 2) % 5], fx_ramp(te, 0.15f + (0.05f * static_cast<float>(k)), 0.5f), 0.07f, L.e.tint2, 2.8f * flick, alpha);
        }
    }
    // 頂点から黒紫の炎の柱。
    for (int k = 0; k < (L.mini ? 1 : 5); ++k) {
        FxSmoke smoke;
        smoke.origin = Vec3{ pts[k].x, pts[k].y, 0.1f };
        smoke.radius = 0.04f;
        smoke.born = 0.35f + (0.03f * static_cast<float>(k));
        smoke.duration = 0.4f;
        smoke.life = 0.55f;
        smoke.count = 6;
        smoke.rise = 1.3f;
        smoke.size = 0.14f;
        smoke.grow = 0.7f;
        smoke.colour = L.e.tint;
        smoke.colour_end = FxColour{ 0.3f, 0.05f, 0.4f };
        smoke.alpha = 0.85f;
        smoke.emissive = 2.4f;
        smoke.seed = L.seed + static_cast<std::uint32_t>(k * 3);
        (void)fx_emit_smoke(smoke, te, out);
    }
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.5f }, 3.4f, L.e.tint, 1.6f * alpha * flick);
    }
}

void look_unlife(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.25f, 0.55f, 0.45f);
    const float size = 0.46f * L.scale;
    const Vec3 c{ L.front.x, L.front.y, head_z(L) + 0.1f + (0.2f * fx_clamp01(te / 1.2f)) };
    if (alpha > 0.01f) {
        FxSprite sprite;
        sprite.name = "skull";
        sprite.centre = c;
        sprite.size = size;
        sprite.tint = FxColour{ 0.35f, 1.f, 0.75f };
        sprite.alpha = alpha * 0.9f;
        sprite.emissive = 2.0f;
        sprite.resolution = L.mini ? 9 : 14;
        if (fx_emit_sprite(L.spec, sprite, out) == 0) {
            glyph(L, out, shape_skull(), c, size / 7.f, alpha * 0.85f, L.e.tint, 1.2f);
        }
    }
    // 立ちのぼる亡霊の靄（青緑）。
    WhirlParams w;
    w.z0 = 0.05f;
    w.height = 1.1f;
    w.radius0 = 0.22f;
    w.radius1 = 0.12f;
    w.speed = 3.f;
    w.rise = 0.6f;
    w.count = 30;
    w.life = 1.2f;
    w.duration = 0.7f;
    w.c0 = L.e.base;
    w.c1 = L.e.tint;
    w.size = 0.075f;
    w.emissive = 1.3f;
    w.part = FxPart::Mist;
    whirl(L, out, w);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.7f }, 3.f, L.e.base, 0.9f * alpha);
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.grey = std::max(mod.grey, 0.5f * fx_envelope(te, 0.3f, 0.5f, 0.5f));
    }
}

void look_web(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float spokes = fx_ramp(te, 0.f, 0.22f);
    const float rings = fx_ramp(te, 0.1f, 0.42f);
    const float alpha = fx_envelope(te, 0.02f, 0.85f, 0.5f);
    if (alpha <= 0.01f) {
        return;
    }
    const float R = 0.52f * L.scale;
    const Vec3 c{ L.front.x, L.front.y, 0.5f * L.scale - (0.06f * fx_ramp(te, 0.5f, 1.3f)) };
    const int n = 8;
    Vec3 tips[8];
    for (int k = 0; k < n; ++k) {
        const float a = (kPi / 8.f) + (2.f * kPi * static_cast<float>(k) / static_cast<float>(n));
        const Vec3 d = on_screen(L.spec, std::cos(a), std::sin(a));
        tips[k] = c + (d * R);
        beam_bar(out, c, c + (d * (R * spokes)), 0.022f, L.e.tint, 1.4f, alpha, 1.f, FxPart::Shape);
    }
    // 同心の八角形（糸の渡し）。
    for (int ring = 1; ring <= 3; ++ring) {
        const float rr = R * static_cast<float>(ring) / 3.f;
        for (int k = 0; k < n; ++k) {
            const float a0 = (kPi / 8.f) + (2.f * kPi * static_cast<float>(k) / static_cast<float>(n));
            const float a1 = a0 + (2.f * kPi / static_cast<float>(n));
            const Vec3 p0 = c + (on_screen(L.spec, std::cos(a0), std::sin(a0)) * rr);
            const Vec3 p1 = c + (on_screen(L.spec, std::cos(a1), std::sin(a1)) * rr);
            // 糸はたるむ。
            const Vec3 sag{ 0.f, 0.f, -0.03f * rr };
            const float vis = fx_clamp01((rings * 3.f) - static_cast<float>(ring - 1));
            beam_bar(out, p0 + sag, p1 + sag, 0.018f, L.e.tint, 1.2f, alpha * 0.9f, vis, FxPart::Shape);
        }
    }
    (void)tips;
    // 粘る雫。
    for (int i = 0; i < count_of(L, 8); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float a = 2.f * kPi * fx_rand(L.seed, id, 1u);
        const float rr = R * (0.2f + (0.7f * fx_rand(L.seed, id, 2u)));
        const float drip = fx_ramp(te, 0.45f + (0.2f * fx_rand(L.seed, id, 3u)), 1.3f) * 0.12f;
        put(out, c + on_screen(L.spec, std::cos(a) * rr, (std::sin(a) * rr) - drip), 0.035f, FxColour{ 0.85f, 0.9f, 1.f }, 1.6f, alpha);
    }
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.015f * fx_envelope(te - 0.3f, 0.1f, 0.4f, 0.4f));
        mod.tremble_phase = te * 50.f;
    }
}

void look_isolation(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float close = fx_ramp(te, 0.f, 0.4f);
    const float alpha = fx_envelope(te, 0.04f, 0.8f, 0.4f);
    const Vec3 c{ L.foot.x, L.foot.y, 0.46f * L.scale };
    const float R = 0.52f * L.scale;
    if (alpha > 0.01f) {
        // 上から閉じていく球殻。
        sphere_shell(L, out, c, R, L.mini ? 40 : 90, 0.045f, L.e.base, 2.0f, alpha * 0.85f, te * 0.7f, close);
        sphere_shell(L, out, c, R * 0.97f, L.mini ? 0 : 40, 0.03f, L.e.tint, 1.5f, alpha * 0.5f, -te * 0.5f, close);
        ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.03f }, R * 1.3f, R * 0.95f, 0.f, 0.35f, 0.03f, L.e.base, 2.f, alpha, 26);
    }
    // 閉じたあと、殻が割れて外へ散る。
    if (te > 0.85f) {
        const float u = fx_clamp01((te - 0.85f) / 0.35f);
        sphere_shell(L, out, c, R * (1.f + (0.6f * u)), L.mini ? 14 : 34, 0.05f * (1.f - u), L.e.tint, 2.2f, 0.9f * (1.f - u), 0.f);
    }
    if (!L.mini) {
        add_light(out, c, 3.4f, L.e.base, 1.3f * alpha);
    }
}

void look_quake(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float grow = fx_ramp(te, 0.f, 0.35f);
    const float alpha = fx_envelope(te, 0.02f, 1.0f, 0.55f);
    const int n = L.mini ? 3 : 4;
    const float flick = 1.f + (0.25f * std::sin(te * 18.f));
    for (int k = 0; k < n; ++k) {
        const std::uint32_t id = static_cast<std::uint32_t>(k);
        const float a = (2.f * kPi * static_cast<float>(k) / static_cast<float>(n)) + (0.6f * fx_rand(L.seed, id, 1u));
        const float len = (0.75f + (0.4f * fx_rand(L.seed, id, 2u))) * L.scale;
        const Vec3 p0 = Vec3{ L.foot.x, L.foot.y, 0.03f };
        // ひびは折れ曲がる。
        const Vec3 p1 = p0 + Vec3{ std::cos(a) * len * 0.45f, std::sin(a) * len * 0.45f, 0.f };
        const float b = a + ((fx_rand(L.seed, id, 3u) - 0.5f) * 1.4f);
        const Vec3 p2 = p1 + Vec3{ std::cos(b) * len * 0.55f, std::sin(b) * len * 0.55f, 0.f };
        floor_dots(out, p0, p1, grow * 2.f, 0.075f, FxColour{ 0.05f, 0.035f, 0.02f }, 0.f, alpha);
        floor_dots(out, p1, p2, (grow * 2.f) - 1.f, 0.07f, FxColour{ 0.05f, 0.035f, 0.02f }, 0.f, alpha);
        floor_dots(out, p0 + Vec3{ 0.f, 0.f, 0.012f }, p1, grow * 2.f, 0.04f, L.e.tint2, 2.2f * flick, alpha * 0.9f);
        floor_dots(out, p1 + Vec3{ 0.f, 0.f, 0.012f }, p2, (grow * 2.f) - 1.f, 0.035f, L.e.tint2, 2.2f * flick, alpha * 0.9f);
    }
    // 跳ね上がる岩と土煙。
    debris_burst(L, out, Vec3{ L.foot.x, L.foot.y, 0.05f }, 10, 0.05f + (0.1f * fx_rand(L.seed, 9u)), 2.8f, 0.9f, 0.09f * L.scale, FxColour{ 0.7f, 0.55f, 0.38f }, L.e.tint, 0.5f, 3u, -11.f);
    FxSmoke smoke;
    smoke.origin = Vec3{ L.front.x, L.front.y, 0.05f };
    smoke.radius = 0.2f * L.scale;
    smoke.born = 0.1f;
    smoke.duration = 0.3f;
    smoke.life = 0.8f;
    smoke.count = count_of(L, 5);
    smoke.rise = 0.35f;
    smoke.size = 0.15f * L.scale;
    smoke.grow = 2.f;
    smoke.colour = FxColour{ 0.55f, 0.45f, 0.33f };
    smoke.colour_end = FxColour{ 0.4f, 0.33f, 0.25f };
    smoke.alpha = 0.55f;
    smoke.emissive = 0.4f;
    smoke.seed = L.seed;
    (void)fx_emit_smoke(smoke, te, out);
    shake_if(L, out, 0.f, 0.9f, 0.14f);
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.04f * fx_envelope(te, 0.05f, 0.6f, 0.3f));
        mod.tremble_phase = te * 90.f;
    }
}

void quake_allsight(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    fx_add_shake(out, fx_shake_envelope(ctx.t, 0.f, 0.9f, 0.14f));
    const float u = ctx.t / 0.9f;
    if ((u >= 0.f) && (u <= 1.25f)) {
        const float env = std::sin(kPi * fx_clamp01(u / 1.25f));
        fx_add_screen_wave(out, e.base, (u * 1.3f) - 0.15f, 0.22f, 0.4f * env, 0.f);
        fx_add_screen_wave(out, e.tint, (u * 1.3f) - 0.3f, 0.1f, 0.22f * env, 0.f);
    }
}

void look_death_touch(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float reach = ease_out(te / 0.3f);
    const float release = 1.f - fx_ramp(te, 0.95f, 1.3f);
    const float appear = fx_ramp(te, 0.f, 0.15f) * release;
    const Vec3 c{ L.front.x, L.front.y, 0.5f * L.scale };
    const Vec3 from = c + on_screen(L.spec, -0.55f * L.scale, 0.35f * L.scale);
    const Vec3 pos = from + ((c - from) * reach);
    FxShapeColours colours;
    colours.base = FxColour{ 0.06f, 0.05f, 0.08f };
    colours.keys = "o";
    colours.colours = { fx_scale(L.e.tint, 1.1f) };
    FxShapePlace place;
    place.right = L.spec.cam_right;
    place.up = L.spec.cam_up;
    place.centre = pos;
    place.cell = 0.05f;
    place.scale = L.scale * 0.8f;
    place.layers = L.mini ? 1 : 2;
    place.depth = 0.04f;
    place.alpha = appear;
    place.emissive = 0.6f;
    place.rotate = -0.5f;
    if ((appear > 0.01f) && !L.mini) {
        (void)fx_emit_shape(shape_hand(false), colours, place, out);
    }
    // 触れたところから、冷たい灰が降りる。
    if (te > 0.28f) {
        FxSpray sp;
        sp.origin = Vec3{ L.front.x, L.front.y, 0.8f * L.scale };
        sp.dir = Vec3{ 0.f, 0.f, -1.f };
        sp.cone = 0.5f;
        sp.scatter = 0.18f * L.scale;
        sp.speed_min = 0.1f;
        sp.speed_max = 0.4f;
        sp.gravity = -0.9f;
        sp.born = 0.28f;
        sp.duration = 0.5f;
        sp.life_min = 0.6f;
        sp.life_max = 0.9f;
        sp.count = count_of(L, 24);
        sp.colour0 = L.e.tint;
        sp.colour1 = L.e.base;
        sp.size0 = 0.05f;
        sp.size1 = 0.03f;
        sp.emissive0 = 0.9f;
        sp.emissive1 = 0.2f;
        sp.seed = L.seed;
        (void)fx_emit_spray(sp, te, out);
    }
    ring_at(L, out, false, c, 0.1f, 0.5f * L.scale, 0.28f, 0.4f, 0.03f, L.e.tint, 1.1f, 0.7f, 22);
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        const float env = fx_envelope(te - 0.28f, 0.2f, 0.5f, 0.45f);
        mod.grey = std::max(mod.grey, 0.8f * env);
        mod.squash = std::max(mod.squash, 0.15f * env);
    }
}

void look_steal(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const Vec3 start = Vec3{ L.front.x, L.front.y, 0.55f * L.scale };
    const Vec3 dst = fx_source_centre(L.spec, 0.8f);
    // 瞬間の光（つかみ取る）。
    if (te < 0.22f) {
        const float u = te / 0.22f;
        put(out, start, ((0.35f * (1.f - u)) + 0.06f) * L.scale, FxColour{ 1.f, 1.f, 0.95f }, 3.6f, 1.f - u);
        radial_rays(L, out, start, L.mini ? 4 : 8, 0.1f, 0.3f * (1.f - u) * L.scale, 0.03f, L.e.tint, 2.8f, 1.f - u, 0.3f);
    }
    // 硬貨が弧を描いて撃った者へ。
    const float fly = 0.65f;
    const float age = te - 0.15f;
    if ((age >= 0.f) && (age < fly + 0.1f)) {
        const float u = fx_clamp01(age / fly);
        const auto at = [&](float k) {
            Vec3 p = start + ((dst - start) * k);
            p.z += 0.6f * std::sin(kPi * k);
            return p;
        };
        const float alpha = 1.f - fx_ramp(age, fly, fly + 0.1f);
        if (!L.mini) {
            glyph(L, out, shape_coin(), at(u), 0.04f, alpha, L.e.base, 1.1f, 2, 0.f, "o", { L.e.tint });
        }
        for (int k = 1; k <= 7; ++k) {
            const float uu = u - (0.04f * static_cast<float>(k));
            if (uu <= 0.f) {
                break;
            }
            put(out, at(uu), 0.045f * (1.f - (static_cast<float>(k) / 8.f)), L.e.tint, 2.f, 0.7f * alpha);
        }
        if (u >= 1.f) {
            ring_at(L, out, false, dst, 0.05f, 0.4f, fly + 0.15f, 0.22f, 0.03f, L.e.tint, 2.f, 0.8f, 18);
        }
    }
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        float ax = 0.f;
        float ay = 1.f;
        away_from_source(L, ax, ay);
        mod.recoil = std::max(mod.recoil, 0.35f * fx_envelope(te, 0.04f, 0.1f, 0.3f));
        mod.away_x = ax;
        mod.away_y = ay;
    }
}

void look_chicken(const Look &L, FxFrame &out)
{
    const float te = L.te;
    FxSmoke smoke;
    smoke.origin = Vec3{ L.front.x, L.front.y, 0.5f * L.scale };
    smoke.radius = 0.16f * L.scale;
    smoke.born = 0.f;
    smoke.duration = 0.1f;
    smoke.life = 0.75f;
    smoke.count = count_of(L, 13);
    smoke.rise = 0.3f;
    smoke.size = 0.2f * L.scale;
    smoke.grow = 2.3f;
    smoke.colour = FxColour{ 1.f, 1.f, 0.9f };
    smoke.colour_end = FxColour{ 0.8f, 0.8f, 0.78f };
    smoke.alpha = 0.85f;
    smoke.emissive = 0.7f;
    smoke.seed = L.seed;
    (void)fx_emit_smoke(smoke, te, out);
    // 羽根がゆれながら落ちる。
    for (int i = 0; i < count_of(L, 22); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float born = 0.05f * fx_rand(L.seed, id, 1u);
        const float life = 1.3f;
        const float age = te - born;
        if ((age < 0.f) || (age >= life)) {
            continue;
        }
        const float a = 2.f * kPi * fx_rand(L.seed, id, 2u);
        const float sp = 0.5f + (0.8f * fx_rand(L.seed, id, 3u));
        Vec3 p{ L.foot.x + (std::cos(a) * sp * 0.5f * (1.f - std::exp(-3.f * age))), L.foot.y + (std::sin(a) * sp * 0.5f * (1.f - std::exp(-3.f * age))),
            0.55f + (1.2f * age * (1.f - age)) - (0.2f * age) };
        p.x += 0.05f * std::sin((age * 8.f) + static_cast<float>(i));
        p.z = std::max(p.z, 0.02f);
        FxVoxel v;
        v.center = p;
        v.size = Vec3{ 0.11f, 0.035f, 0.02f };
        const FxColour col = (i % 3 == 0) ? FxColour{ 1.f, 0.85f, 0.3f } : FxColour{ 1.f, 0.98f, 0.9f };
        v.r = col.r;
        v.g = col.g;
        v.b = col.b;
        v.emissive = 0.9f;
        v.alpha = 1.f - fx_ramp(age, 0.9f, life);
        v.yaw = (4.f * age) + (6.f * fx_rand(L.seed, id, 4u));
        v.pitch = 0.6f * std::sin((age * 7.f) + static_cast<float>(i));
        v.part = FxPart::Debris;
        out.voxels.push_back(v);
    }
    // 頭の上のにわとり。
    if (!L.mini) {
        const float bob = std::sin(te * 14.f) * 0.025f;
        const float alpha = fx_envelope(te - 0.12f, 0.08f, 0.6f, 0.35f);
        glyph(L, out, shape_chicken(), Vec3{ L.front.x, L.front.y, 1.15f + bob }, 0.04f, alpha, FxColour{ 1.f, 0.98f, 0.92f }, 1.0f, 2, 0.f, "rye",
            { FxColour{ 1.f, 0.15f, 0.1f }, FxColour{ 1.f, 0.7f, 0.15f }, FxColour{ 0.05f, 0.05f, 0.05f } });
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.squash = std::max(mod.squash, 0.18f * fx_envelope(te, 0.04f, 0.08f, 0.2f));
    }
}

void look_bomb(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float fall = 0.28f;
    const float fuse = 0.34f;
    const float boom = fall + fuse;
    const Vec3 c{ L.foot.x, L.foot.y, 0.0f };
    if (!L.mini && (te < boom)) {
        const float z = (te < fall) ? fx_lerp(1.9f, 0.28f, (te / fall) * (te / fall)) : (0.28f + (0.03f * std::sin((te - fall) * 40.f)));
        glyph(L, out, shape_bomb(), Vec3{ L.front.x, L.front.y, z }, 0.034f, 1.f, FxColour{ 0.07f, 0.07f, 0.09f }, 0.6f, 2, 0.f, "yo",
            { FxColour{ 1.f, 0.7f, 0.2f }, FxColour{ 0.45f, 0.45f, 0.55f } });
        // 導火線の火花。
        FxSpray sp;
        sp.origin = Vec3{ L.front.x + 0.1f, L.front.y, z + 0.17f };
        sp.cone = kPi;
        sp.speed_min = 0.4f;
        sp.speed_max = 1.4f;
        sp.gravity = -2.f;
        sp.born = 0.f;
        sp.duration = boom;
        sp.life_min = 0.1f;
        sp.life_max = 0.2f;
        sp.count = 20;
        sp.colour0 = FxColour{ 1.f, 0.9f, 0.4f };
        sp.colour1 = FxColour{ 1.f, 0.3f, 0.05f };
        sp.size0 = 0.035f;
        sp.size1 = 0.01f;
        sp.emissive0 = 3.2f;
        sp.emissive1 = 1.f;
        sp.seed = L.seed;
        (void)fx_emit_spray(sp, te, out);
    }
    const float tb = te - (L.mini ? 0.f : boom);
    if (tb >= 0.f) {
        // 閃光、輪、黒い煙、破片。
        if (tb < 0.2f) {
            const float u = tb / 0.2f;
            put(out, Vec3{ L.front.x, L.front.y, 0.4f * L.scale }, ((0.16f * (1.f - u)) + 0.04f) * L.scale, FxColour{ 1.f, 0.9f, 0.6f }, 1.8f, 1.f - u, FxPart::BurstFlash);
        }
        // 火の玉の粒（橙から赤へ）。
        FxSpray fire;
        fire.origin = Vec3{ L.front.x, L.front.y, 0.4f * L.scale };
        fire.cone = kPi;
        fire.speed_min = 0.8f;
        fire.speed_max = 2.4f;
        fire.gravity = -1.5f;
        fire.drag = 2.f;
        fire.born = L.mini ? 0.f : boom;
        fire.life_min = 0.25f;
        fire.life_max = 0.5f;
        fire.count = count_of(L, 30);
        fire.colour0 = FxColour{ 1.f, 0.8f, 0.3f };
        fire.colour1 = FxColour{ 0.8f, 0.15f, 0.03f };
        fire.size0 = 0.1f * L.scale;
        fire.size1 = 0.04f;
        fire.emissive0 = 3.f;
        fire.emissive1 = 0.8f;
        fire.seed = L.seed + 31u;
        (void)fx_emit_spray(fire, te, out);
        ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.55f * L.scale, tb, 0.3f, 0.05f, FxColour{ 1.f, 0.6f, 0.2f }, 2.4f, 0.9f, 24);
        for (int i = 0; i < count_of(L, 12); ++i) {
            const std::uint32_t id = static_cast<std::uint32_t>(i);
            const float a = 2.f * kPi * fx_rand(L.seed, id, 1u);
            const float sp2 = 1.6f + (1.8f * fx_rand(L.seed, id, 2u));
            const float age = tb - 0.f;
            if ((age < 0.f) || (age > 0.8f)) {
                continue;
            }
            Vec3 p{ L.foot.x + (std::cos(a) * sp2 * 0.28f * (1.f - std::exp(-5.f * age))), L.foot.y + (std::sin(a) * sp2 * 0.28f * (1.f - std::exp(-5.f * age))), 0.3f + (1.8f * age) - (4.5f * age * age) };
            p.z = std::max(p.z, 0.03f);
            put(out, p, 0.07f * L.scale, FxColour{ 0.18f, 0.17f, 0.2f }, 0.2f, 1.f - fx_ramp(age, 0.5f, 0.8f), FxPart::Debris);
        }
        FxSmoke smoke;
        smoke.origin = Vec3{ L.front.x, L.front.y, 0.25f };
        smoke.radius = 0.2f * L.scale;
        smoke.born = (L.mini ? 0.f : boom) + 0.03f;
        smoke.duration = 0.25f;
        smoke.life = 0.9f;
        smoke.count = count_of(L, 10);
        smoke.rise = 0.6f;
        smoke.size = 0.22f * L.scale;
        smoke.grow = 2.f;
        smoke.colour = FxColour{ 0.14f, 0.13f, 0.15f };
        smoke.colour_end = FxColour{ 0.05f, 0.05f, 0.06f };
        smoke.alpha = 0.85f;
        smoke.emissive = 0.3f;
        smoke.seed = L.seed;
        (void)fx_emit_smoke(smoke, te, out);
        if (!L.mini) {
            add_light(out, Vec3{ L.foot.x, L.foot.y, 0.5f }, 4.6f, FxColour{ 1.f, 0.65f, 0.25f }, 3.f * fx_envelope(tb, 0.03f, 0.1f, 0.4f));
        }
    }
    shake_if(L, out, boom, 0.4f, 0.1f);
    (void)c;
}

void look_air(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const Vec3 c{ L.foot.x, L.foot.y, 0.5f * L.scale };
    for (int k = 0; k < (L.mini ? 1 : 2); ++k) {
        const float born = 0.12f * static_cast<float>(k);
        const float age = te - born;
        const float life = 0.55f;
        if ((age < 0.f) || (age >= life)) {
            continue;
        }
        const float u = age / life;
        sphere_shell(L, out, c, fx_lerp(0.12f, 0.6f, ease_out(u)) * L.scale, 46, 0.045f * (1.f - (0.5f * u)), (k == 0) ? L.e.base : L.e.tint, 1.6f, 0.85f * (1.f - fx_ramp(u, 0.5f, 1.f)),
            u * 3.f);
    }
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.7f * L.scale, 0.f, 0.4f, 0.03f, L.e.tint, 2.f, 0.8f, 26);
    // 水平の風の筋（放射）。
    const int n = L.mini ? 4 : 10;
    for (int i = 0; i < n; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float a = (2.f * kPi * static_cast<float>(i) / static_cast<float>(n)) + (0.3f * fx_rand(L.seed, id, 1u));
        const float born = 0.04f * fx_rand(L.seed, id, 2u);
        const float age = te - born;
        if ((age < 0.f) || (age >= 0.5f)) {
            continue;
        }
        const float u = age / 0.5f;
        const Vec3 d{ std::cos(a), std::sin(a), 0.f };
        const float r = fx_lerp(0.15f, 0.65f, ease_out(u)) * L.scale;
        const float h = (0.15f + (0.7f * fx_rand(L.seed, id, 3u))) * L.scale;
        put_bar(out, Vec3{ L.foot.x, L.foot.y, h } + (d * r), d, 0.3f * L.scale * (1.f - (0.4f * u)), 0.022f, FxColour{ 0.9f, 0.98f, 1.f }, 2.2f, 1.f - fx_ramp(u, 0.5f, 1.f));
    }
    if (!L.mini) {
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        float ax = 0.f;
        float ay = 1.f;
        away_from_source(L, ax, ay);
        mod.recoil = std::max(mod.recoil, 0.5f * fx_envelope(te, 0.05f, 0.12f, 0.35f));
        mod.away_x = ax;
        mod.away_y = ay;
        add_light(out, c, 3.f, L.e.base, 0.9f * fx_envelope(te, 0.06f, 0.2f, 0.35f));
    }
}

void look_gain_exp(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.1f, 0.9f, 0.5f);
    // 二重の螺旋で昇る星。
    for (int h = 0; h < 2; ++h) {
        const int n = L.mini ? 3 : 8;
        for (int i = 0; i < n; ++i) {
            const float born = 0.07f * static_cast<float>(i);
            const float age = te - born;
            const float life = 1.0f;
            if ((age < 0.f) || (age >= life)) {
                continue;
            }
            const float u = age / life;
            const float ang = (kPi * static_cast<float>(h)) + (age * 6.f);
            const float r = 0.26f * L.scale * (1.f - (0.4f * u));
            const Vec3 p{ L.foot.x + (std::cos(ang) * r), L.foot.y + (std::sin(ang) * r), (0.05f + (1.5f * u)) * L.scale };
            glyph(L, out, shape_star4(), p, 0.027f * (1.f - (0.3f * u)), (1.f - fx_ramp(u, 0.7f, 1.f)), (h == 0) ? L.e.base : L.e.tint, 3.0f);
        }
    }
    // 光の柱と足元の輪。
    const float col = fx_envelope(te, 0.08f, 0.35f, 0.5f);
    if (!L.mini && (col > 0.01f)) {
        for (int k = 0; k < 12; ++k) {
            put(out, Vec3{ L.foot.x, L.foot.y, 0.1f + (0.15f * static_cast<float>(k)) }, 0.15f, L.e.base, 1.6f, 0.34f * col * (1.f - (0.06f * static_cast<float>(k))), FxPart::Wave);
        }
    }
    ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.03f }, 0.1f, 0.55f * L.scale, 0.f, 0.5f, 0.03f, L.e.base, 2.f, 0.85f * alpha, 22);
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.8f }, 3.6f, L.e.base, 1.7f * alpha);
    }
}

//! 名状しがたい恐怖。`level`：0 基本・1 気絶・2 吸収・3 解呪・4 混乱・5 咆哮。
void look_eldritch(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const int lv = L.e.level;
    const float alpha = fx_envelope(te, 0.12f, 0.8f, 0.45f);
    // 紫の渦。
    WhirlParams w;
    w.height = 1.0f;
    w.radius0 = 0.3f;
    w.radius1 = 0.12f;
    w.speed = (lv == 4) ? 14.f : -4.f;
    w.rise = 0.3f;
    w.count = 22;
    w.life = 1.1f;
    w.duration = 0.5f;
    w.c0 = L.e.base;
    w.c1 = L.e.tint2;
    w.size = 0.06f;
    w.emissive = 1.5f;
    w.part = FxPart::Mist;
    whirl(L, out, w);
    // 頭のまわりに開く目。
    const int eyes = L.mini ? 1 : 3;
    const Vec3 head{ L.front.x, L.front.y, head_z(L) + 0.1f };
    for (int i = 0; i < eyes; ++i) {
        const float born = 0.08f * static_cast<float>(i);
        const float age = te - born;
        if ((age < 0.f) || (age > 1.05f)) {
            continue;
        }
        const int stage = (age < 0.1f) ? 2 : ((age < 0.2f) ? 1 : ((age < 0.85f) ? 0 : ((age < 0.95f) ? 1 : 2)));
        const float ang = (kPi * 0.5f) + ((static_cast<float>(i) - 1.f) * 0.95f) + (0.3f * std::sin(te * 2.f));
        const Vec3 p = head + on_screen(L.spec, std::cos(ang) * 0.4f * L.scale, (std::sin(ang) * 0.34f) - 0.02f);
        glyph(L, out, shape_eye(stage), p, 0.06f * L.scale, alpha, FxColour{ 0.55f, 0.4f, 0.75f }, 0.9f, 2, 0.f, "oO", { FxColour{ 0.95f, 0.92f, 0.8f }, FxColour{ 0.9f, 0.08f, 0.2f } });
    }
    // 派生ごとの重ね。
    if (lv == 1) {
        for (int i = 0; i < count_of(L, 5); ++i) {
            const float ang = (2.f * kPi * static_cast<float>(i) / 5.f) + (te * 8.f);
            glyph(L, out, shape_star4(), head + on_screen(L.spec, std::cos(ang) * 0.3f * L.scale, 0.3f + (std::sin(ang) * 0.06f)), 0.014f, alpha, L.e.tint, 2.6f);
        }
        if (!L.mini) {
            FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
            mod.tremble = std::max(mod.tremble, 0.02f * alpha);
            mod.tremble_phase = te * 100.f;
        }
    } else if (lv == 2) {
        StreamParams sp;
        sp.count = 24;
        sp.flight = 0.5f;
        sp.born = 0.25f;
        sp.c0 = L.e.base;
        sp.c1 = L.e.tint2;
        stream_to(L, out, Vec3{ L.foot.x, L.foot.y, 0.6f * L.scale }, fx_source_centre(L.spec, 0.6f), sp);
    } else if (lv == 3) {
        ring_at(L, out, false, Vec3{ L.front.x, L.front.y, 0.6f * L.scale }, 0.1f, 0.9f * L.scale, 0.2f, 0.45f, 0.04f, L.e.tint, 2.2f, 0.9f, 28);
        ring_at(L, out, true, Vec3{ L.foot.x, L.foot.y, 0.04f }, 0.1f, 0.8f * L.scale, 0.28f, 0.45f, 0.04f, L.e.base, 2.f, 0.8f, 28);
    } else if (lv == 4) {
        ring_at(L, out, false, head + on_screen(L.spec, 0.f, 0.f), 0.28f * L.scale, 0.28f * L.scale, 0.f, 1.1f, 0.03f, L.e.tint, 1.6f, 0.8f * alpha, 20, 2, te * 5.f);
        ring_at(L, out, false, head + on_screen(L.spec, 0.f, 0.f), 0.18f * L.scale, 0.18f * L.scale, 0.f, 1.1f, 0.03f, L.e.base, 1.6f, 0.8f * alpha, 16, 2, -te * 7.f);
    } else if (lv == 5) {
        for (int k = 0; k < (L.mini ? 2 : 4); ++k) {
            ring_at(L, out, false, head + on_screen(L.spec, 0.f, -0.2f), 0.08f, 0.85f * L.scale, 0.12f * static_cast<float>(k), 0.5f, 0.035f, (k % 2 == 0) ? L.e.tint : L.e.base, 1.8f, 0.85f,
                24);
        }
        shake_if(L, out, 0.1f, 0.5f, 0.06f);
    }
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.9f }, 3.2f, L.e.base, 1.0f * alpha);
    }
}

/* ============================================================ 表 */

constexpr FxColour kWhite{ 1.f, 1.f, 1.f };

const Entry kEntries[] = {
    // attr, base, smoke, emissive, tint, tint2, look, seconds, level, flight, beam, beam_seconds, allsight, allsight_seconds, flags
    { "fc_storm", { 0.35f, 0.62f, 1.f }, 0.15f, 1.2f, { 0.9f, 0.97f, 1.f }, kWhite, look_storm, 1.35f, 0, flight_storm, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_rock", { 0.66f, 0.5f, 0.34f }, 0.5f, 0.8f, { 0.42f, 0.32f, 0.22f }, kWhite, look_rock, 1.45f, 0, flight_rock, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_amnesia", { 0.5f, 0.45f, 0.62f }, 0.2f, 1.0f, { 0.8f, 0.75f, 0.95f }, kWhite, look_amnesia, 1.4f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_blind", { 0.25f, 0.2f, 0.4f }, 0.3f, 0.8f, { 0.55f, 0.5f, 0.8f }, kWhite, look_blind, 1.5f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_paralysis", { 0.72f, 0.3f, 1.f }, 0.1f, 1.2f, { 1.f, 0.85f, 1.f }, kWhite, look_paralysis, 1.5f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_antimagic", { 1.f, 0.2f, 0.2f }, 0.1f, 1.2f, { 0.5f, 0.6f, 1.f }, { 0.8f, 0.4f, 1.f }, look_antimagic, 1.3f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_unholy_word", { 0.4f, 0.1f, 0.5f }, 0.3f, 1.0f, { 0.6f, 0.15f, 0.85f }, { 0.9f, 0.1f, 0.2f }, look_unholy_word, 1.7f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_unlife", { 0.35f, 0.7f, 0.55f }, 0.3f, 1.0f, { 0.7f, 0.95f, 0.85f }, kWhite, look_unlife, 1.8f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_web", { 0.8f, 0.82f, 0.9f }, 0.1f, 1.0f, { 0.95f, 0.97f, 1.f }, kWhite, look_web, 1.9f, 0, flight_web, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_isolation", { 0.4f, 0.7f, 1.f }, 0.1f, 1.2f, { 0.85f, 0.95f, 1.f }, kWhite, look_isolation, 1.4f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_quake", { 0.55f, 0.42f, 0.3f }, 0.5f, 0.8f, { 0.4f, 0.32f, 0.24f }, { 1.f, 0.55f, 0.15f }, look_quake, 1.7f, 0, nullptr, nullptr, 0.f, quake_allsight, 1.15f, kWholeArea },
    { "fc_death_touch", { 0.2f, 0.17f, 0.25f }, 0.3f, 0.7f, { 0.6f, 0.55f, 0.7f }, kWhite, look_death_touch, 1.5f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_steal", { 1.f, 0.85f, 0.35f }, 0.1f, 1.2f, { 1.f, 0.97f, 0.7f }, kWhite, look_steal, 1.3f, 0, flight_steal, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_chicken", { 1.f, 0.85f, 0.2f }, 0.4f, 1.0f, { 1.f, 0.98f, 0.9f }, kWhite, look_chicken, 1.6f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_bomb", { 0.4f, 0.4f, 0.45f }, 0.5f, 1.0f, { 1.f, 0.6f, 0.2f }, kWhite, look_bomb, 1.85f, 0, flight_bomb, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_air", { 0.6f, 0.82f, 1.f }, 0.1f, 1.1f, { 0.9f, 0.98f, 1.f }, kWhite, look_air, 1.1f, 0, flight_air, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_gain_exp", { 0.45f, 1.f, 0.4f }, 0.05f, 1.2f, { 1.f, 0.95f, 0.5f }, kWhite, look_gain_exp, 1.5f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_eldritch", { 0.5f, 0.15f, 0.85f }, 0.2f, 1.0f, { 0.85f, 0.55f, 1.f }, { 0.35f, 0.1f, 0.6f }, look_eldritch, 1.35f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_eldritch_stun", { 0.35f, 0.5f, 1.f }, 0.2f, 1.0f, { 0.8f, 0.9f, 1.f }, { 0.3f, 0.2f, 0.7f }, look_eldritch, 1.35f, 1, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_eldritch_drain", { 0.3f, 0.25f, 0.4f }, 0.2f, 1.0f, { 0.7f, 0.55f, 0.85f }, { 0.4f, 0.3f, 0.55f }, look_eldritch, 1.7f, 2, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_eldritch_dispel", { 1.f, 0.4f, 0.4f }, 0.2f, 1.0f, { 1.f, 0.8f, 0.7f }, { 0.7f, 0.2f, 0.45f }, look_eldritch, 1.35f, 3, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_eldritch_confuse", { 0.85f, 0.65f, 0.3f }, 0.2f, 1.0f, { 1.f, 0.85f, 0.45f }, { 0.55f, 0.3f, 0.65f }, look_eldritch, 1.35f, 4, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "fc_eldritch_howl", { 0.25f, 0.2f, 0.35f }, 0.3f, 1.0f, { 0.65f, 0.5f, 0.85f }, { 0.3f, 0.15f, 0.5f }, look_eldritch, 1.35f, 5, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
};

} // namespace

void fx_register_frox(FxStyleRegistry &registry)
{
    looks::register_entries(registry, kEntries, sizeof(kEntries) / sizeof(kEntries[0]));
}

} // namespace hd2d
