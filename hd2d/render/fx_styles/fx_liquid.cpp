/*!
 * @file fx_liquid.cpp
 * @brief 液体の系統の見た目。毒・酸・溶岩流・水・水流・血の呪い。
 *
 * ## 決まっている見た目（§5.1。決めたこと 2026-10-08）
 * | 形 | 見た目 | 実装 |
 * |---|---|---|
 * | 飛んでいる間 | 液体のかたまりが**雫を垂らしながら**飛ぶ。垂れた雫は床に落ちて小さく弾けて消える | `emit_blob`（かたまり）＋ `emit_flight_drips`（雫） |
 * | ボルト | 対象に液体が**ドバっとかかり**、体を伝って**滴り**、床に小さな水たまりができて消える | `emit_drench`（弾け・体の覆い・滴り・水たまり） |
 * | ボール | 効果範囲の**マスごとに**液体が降りかかる（中心から外へ少しずつ遅れて）。各マスに小さな水たまり | `emit_area`（降りかかり・水たまり） |
 * | ブレス | 撃った者から**霧が広がる**（範囲のマスの中で） | `liquid_breath` |
 * | ビーム | 経路に沿って液体の筋が走り、筋から雫が落ちて床で弾ける。終点の対象はボルトと同じにかかる | `liquid_beam` |
 * | ジャンプ（直接） | 範囲のマスの上から液体が降りかかる（ボールと同じ） | `liquid_burst` |
 * | 飛ばない魔法 | 対象に上から降りかかる（ボールの 1 マス）。足元だけなら足元に水たまりが広がる。視界内の全員は光の波 | `liquid_glow` |
 *
 * | 属性 | 色・違い |
 * |---|---|
 * | `pois` | 紫。基本の形 |
 * | `acid` | 黄緑。かかった所から**黄緑の煙**が立つ（**対象の体と床の水たまりの両方**から） |
 * | `lava_flow` | 毒の形を赤で。重くて粘る（雫が重い・水たまりが大きく長く残る）。熱く光り、冷えて黒くなる。水たまりから黒い煙 |
 * | `water`・`water_flow` | 毒の形を水色で。透き通ってさらさら。床に波紋が広がる（水流は量が多く波紋が大きい） |
 * | `blood_curse` | 濃い赤の液体。かかった所に**どくろ**が浮かぶ（共有の `skull` の絵） |
 *
 * ## 作りの方針
 * - **体にかかる液体は、画面に出ている実体のタイルの影絵に沿う**（`spec.tile_at`）。引けない（その場に実体がいない）ときは
 *   体を覆わず、床の弾けと水たまりだけにする。**画面に出ていないものは描かない**ので、原作より情報が増えない。
 * - **効いたか・耐えたかで絵を変えない**（§5 の約束）。属性と形だけで決まる。
 * - 爆発の粒・滴り・水たまり・煙・どくろは、範囲のマスの中に収める（ボール・ジャンプ・ブレス）。
 * - すべて**状態を持たない**。同じ（撃ち方、経過秒）に同じ絵を返す（雫の落ちる所・弾ける時刻も式で決まる）。
 * - ボクセルの立方体だけで作る（新しいテクスチャも GL の部品も使わない。どくろは `fx_emit_sprite` が粒に直す）。
 */
#include "render/fx_styles/fx_parts.h"
#include "render/fx_styles/fx_style.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace hd2d {

namespace {

constexpr float kPi = 3.14159265f;

/* ============================================================ 属性ごとの見た目の表 */

//! 属性 1 つぶんの見た目の違い。形の作りは共通で、色・重さ・量・煙・どくろだけが違う。
struct Look {
    const char *attr;
    FxColour core;  //!< 液体の基本の色
    FxColour light; //!< 光る面・しぶき
    FxColour deep;  //!< 影・底
    FxColour crust; //!< 冷えたときの色（`cools` が真のとき）
    float emissive; //!< 自己発光（暗い部屋でも色が読めるように）
    float alpha;    //!< 液体の不透明度（水は透ける）
    float thick;    //!< 0 でさらさら（水）〜 1 で粘る（溶岩）
    float volume;   //!< 量の倍率（かたまり・水たまりの大きさ）
    float droplet;  //!< 雫の大きさの倍率
    float coat_life; //!< 体にかかった液体が残る秒
    bool cools;     //!< 時間とともに冷えて暗くなる（溶岩）
    bool acid;      //!< 対象の体からも煙が立つ（酸）
    bool skull;     //!< かかった所にどくろが浮かぶ（血の呪い）
    float ripple;   //!< 床の波紋の大きさ（0 で無し。水・水流）
    FxColour mist0; //!< 霧・煙の出はじめの色
    FxColour mist1; //!< 霧・煙の終わりの色
    float mist_alpha;
    float mist_emissive;
    float puddle_smoke; //!< 水たまりから立つ煙の濃さ（0 で無し）
    FxColour glow;      //!< 点光源の色
    float glow_k;       //!< 点光源の強さの倍率
};

const Look kLooks[] = {
    // 毒：紫（基本の形）
    { "pois", { 0.52f, 0.16f, 0.80f }, { 0.85f, 0.55f, 1.00f }, { 0.26f, 0.06f, 0.42f }, { 0.26f, 0.06f, 0.42f }, 0.85f, 1.0f, 0.35f, 1.0f,
        1.0f, 1.5f, false, false, false, 0.f, { 0.65f, 0.35f, 0.90f }, { 0.35f, 0.15f, 0.55f }, 0.50f, 0.55f, 0.f,
        { 0.70f, 0.35f, 1.00f }, 1.0f },
    // 酸：黄緑。体と水たまりの両方から黄緑の煙
    { "acid", { 0.55f, 0.90f, 0.14f }, { 0.88f, 1.00f, 0.40f }, { 0.28f, 0.52f, 0.05f }, { 0.28f, 0.52f, 0.05f }, 1.0f, 1.0f, 0.15f, 1.0f, 0.9f,
        1.5f, false, true, false, 0.f, { 0.62f, 0.95f, 0.22f }, { 0.40f, 0.62f, 0.12f }, 0.90f, 1.5f, 1.0f, { 0.60f, 0.95f, 0.20f }, 1.0f },
    // 溶岩流：赤。重くて粘り、熱く光って冷える
    { "lava_flow", { 1.00f, 0.24f, 0.02f }, { 1.00f, 0.58f, 0.12f }, { 0.55f, 0.07f, 0.01f }, { 0.14f, 0.03f, 0.02f }, 1.15f, 1.0f, 1.0f, 1.25f,
        1.25f, 2.1f, true, false, false, 0.f, { 1.00f, 0.45f, 0.12f }, { 0.25f, 0.18f, 0.16f }, 0.50f, 1.0f, 0.6f, { 1.00f, 0.40f, 0.08f }, 1.3f },
    // 水：水色。透き通ってさらさら。波紋
    { "water", { 0.22f, 0.52f, 1.00f }, { 0.75f, 0.92f, 1.00f }, { 0.08f, 0.28f, 0.80f }, { 0.08f, 0.28f, 0.80f }, 0.75f, 0.85f, 0.0f, 1.0f, 1.0f,
        1.3f, false, false, false, 1.0f, { 0.70f, 0.88f, 1.00f }, { 0.45f, 0.70f, 1.00f }, 0.50f, 0.50f, 0.f, { 0.40f, 0.70f, 1.00f }, 0.9f },
    // 水流：水よりも量が多く、波紋が大きい
    { "water_flow", { 0.28f, 0.72f, 1.00f }, { 0.82f, 0.97f, 1.00f }, { 0.10f, 0.42f, 0.92f }, { 0.10f, 0.42f, 0.92f }, 0.80f, 0.85f, 0.0f, 1.3f,
        1.1f, 1.4f, false, false, false, 1.4f, { 0.75f, 0.95f, 1.00f }, { 0.40f, 0.75f, 1.00f }, 0.50f, 0.50f, 0.f, { 0.45f, 0.80f, 1.00f }, 1.0f },
    // 血の呪い：濃い赤。かかった所にどくろ
    { "blood_curse", { 0.50f, 0.03f, 0.05f }, { 0.85f, 0.12f, 0.12f }, { 0.22f, 0.00f, 0.02f }, { 0.22f, 0.00f, 0.02f }, 0.60f, 1.0f, 0.6f, 1.0f,
        1.0f, 1.6f, false, false, true, 0.f, { 0.55f, 0.05f, 0.07f }, { 0.20f, 0.02f, 0.04f }, 0.50f, 0.30f, 0.f, { 0.70f, 0.08f, 0.10f }, 0.9f },
};

const Look &look_of(const FxSpec &spec)
{
    for (const Look &look : kLooks) {
        if (spec.attr == look.attr) {
            return look;
        }
    }
    return kLooks[0];
}

/* ============================================================ 小さな道具 */

float rnd(std::uint32_t seed, int a, int b = 0, int c = 0)
{
    return fx_rand(seed, static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(c));
}

float length_of(const Vec3 &v)
{
    return std::sqrt(dot(v, v));
}

//! 水平な単位ベクトル（長さが 0 に近ければ `fallback`）。
Vec3 horizontal_unit(const Vec3 &v, const Vec3 &fallback)
{
    const float len = std::sqrt((v.x * v.x) + (v.y * v.y));
    return (len < 1e-4f) ? fallback : Vec3{ v.x / len, v.y / len, 0.f };
}

std::uint32_t cell_seed(std::uint32_t seed, int gx, int gy)
{
    return seed ^ (static_cast<std::uint32_t>(gx) * 73856093u) ^ (static_cast<std::uint32_t>(gy) * 19349663u);
}

//! 影（0）→ 基本（0.5）→ 光る面（1）の色。
FxColour liquid_colour(const Look &look, float shade)
{
    shade = fx_clamp01(shade);
    return (shade < 0.5f) ? fx_mix(look.deep, look.core, shade * 2.f) : fx_mix(look.core, look.light, (shade - 0.5f) * 2.f);
}

//! 熱の残り（溶岩だけ、`u`＝0〜1 の経過で冷える）。ほかは常に 1。
float heat_of(const Look &look, float u)
{
    return look.cools ? (1.f - (0.9f * fx_ramp(u, 0.15f, 1.f))) : 1.f;
}

FxColour cooled(const Look &look, const FxColour &c, float u)
{
    return look.cools ? fx_mix(c, look.crust, fx_ramp(u, 0.1f, 0.9f)) : c;
}

//! 点光源を足す。`x,y` は光の中心、`peak` は最大の強さ、`decay` は弱まる秒。
void add_pulse_light(const Look &look, float x, float y, float z, float radius, float peak, float te, float decay, FxFrame &out)
{
    float strength = 0.f;
    if (te < 0.f) {
        return;
    }
    if (te < 0.04f) {
        strength = te / 0.04f;
    } else {
        const float k = 1.f - ((te - 0.04f) / std::max(decay, 1e-3f));
        strength = (k > 0.f) ? (k * k) : 0.f;
    }
    if (strength <= 0.01f) {
        return;
    }
    PointLight light;
    light.x = x;
    light.y = y;
    light.z = z;
    light.radius = radius;
    light.r = look.glow.r;
    light.g = look.glow.g;
    light.b = look.glow.b;
    light.intensity = peak * look.glow_k * strength;
    out.lights.push_back(light);
}

/* ============================================================ 水たまり・煙・波紋・どくろ */

//! 水たまりの全体の長さ（広がる＋保つ＋薄れる）。粘るほど広がるのも消えるのも遅い。
struct PuddleTiming {
    float spread;
    float hold;
    float fade;
    float total() const { return spread + hold + fade; }
};

PuddleTiming puddle_timing(const Look &look)
{
    return PuddleTiming{ 0.25f + (0.20f * look.thick), 0.45f + (0.55f * look.thick) + (look.cools ? 0.30f : 0.f), 0.45f + (0.35f * look.thick) };
}

//! 床の水たまり 1 つ。溶岩は熱から冷えて暗くなり、薄れる。
void emit_puddle_look(const Look &look, float cx, float cy, float radius, float born, const PuddleTiming &timing, float alpha, float voxel,
    std::uint32_t seed, float t, FxFrame &out)
{
    FxPuddle p;
    p.cx = cx;
    p.cy = cy;
    p.z = 0.f;
    p.radius = radius;
    p.born = born;
    p.spread = timing.spread;
    p.hold = timing.hold;
    p.fade = timing.fade;
    const float u = fx_clamp01((t - born) / std::max(timing.total(), 1e-3f));
    p.colour = cooled(look, fx_mix(look.core, look.deep, 0.35f), u);
    p.emissive = look.emissive * 0.75f * heat_of(look, u);
    p.alpha = std::min(1.f, look.alpha * alpha);
    p.voxel = voxel;
    p.thickness = 0.025f;
    p.ragged = 0.25f;
    p.seed = seed;
    p.part = FxPart::Puddle;
    (void)fx_emit_puddle(p, t, out);
}

//! 煙・霧 1 かたまり。色は属性の表から。
void emit_mist_look(const Look &look, const Vec3 &origin, float radius, float born, float duration, float life, int count, float rise, float size,
    float alpha_scale, std::uint32_t seed, float t, FxFrame &out)
{
    FxSmoke s;
    s.origin = origin;
    s.radius = radius;
    s.born = born;
    s.duration = duration;
    s.life = life;
    s.count = count;
    s.rise = rise;
    s.size = size;
    s.grow = 1.8f;
    s.colour = look.mist0;
    s.colour_end = look.mist1;
    s.alpha = std::min(0.9f, look.mist_alpha * alpha_scale);
    s.emissive = look.mist_emissive;
    s.seed = seed;
    (void)fx_emit_smoke(s, t, out);
}

//! 水（水流）の波紋：水たまりの上に輪が広がる。マスの中に収める（半径 0.42 まで）。
void emit_ripple(const Look &look, float cx, float cy, float born, float t, std::uint32_t seed, int max_rings, FxFrame &out)
{
    if (look.ripple <= 0.f) {
        return;
    }
    for (int k = 0; k < std::min(max_rings, (look.ripple > 1.f) ? 2 : 1); ++k) {
        FxRing r;
        r.centre = Vec3{ cx, cy, 0.04f };
        r.r0 = 0.06f;
        r.r1 = 0.30f + (0.12f * std::min(look.ripple - 0.4f, 1.f)) - (0.06f * static_cast<float>(k));
        r.born = born + (0.12f * static_cast<float>(k));
        r.life = 0.55f;
        r.thickness = 0.035f;
        r.segments = 20;
        r.colour = look.light;
        r.emissive = 0.55f;
        r.alpha = 0.6f;
        r.seed = seed + static_cast<std::uint32_t>(k);
        (void)fx_emit_ring(r, t, out);
    }
}

//! どくろ（血の呪い）：かかった所に浮かんで、昇りながら消える。
void emit_skull(const FxSpec &spec, int gx, int gy, float born, float te, FxFrame &out)
{
    const float age = te - born;
    constexpr float kLife = 1.2f;
    if ((age < 0.f) || (age >= kLife)) {
        return;
    }
    const float env = fx_envelope(age, 0.18f, 0.55f, 0.47f);
    FxSprite sp;
    sp.name = "skull";
    sp.centre = fx_cell_centre(gx, gy, 0.95f + (0.30f * fx_ramp(age, 0.f, kLife))) + (fx_toward_camera(spec) * 0.15f);
    sp.size = 0.40f + (0.04f * env);
    sp.tint = FxColour{ 1.0f, 0.38f, 0.32f };
    sp.alpha = 0.92f * env;
    sp.emissive = 1.4f;
    sp.resolution = 14;
    (void)fx_emit_sprite(spec, sp, out);
}

/* ============================================================ 雫（落ちて、床で小さく弾けて消える） */

constexpr float kSplashSeconds = 0.38f;

//! 雫 1 つ。`pos`（出た所）から `vel` で動き、重力で床まで落ちる。床に着くと小さな水たまりと数粒の跳ねになって消える。
struct Drop {
    Vec3 pos{};
    Vec3 vel{};
    float born{ 0.f };
    float gravity{ -13.f };
    float size{ 0.05f };
    float splash{ 1.f }; //!< 弾けの大きさの倍率
    std::uint32_t seed{ 1 };
};

//! 床（z = 0）に着くまでの秒。
float drop_land_age(const Drop &d)
{
    const float dz = d.pos.z;
    if (dz <= 0.f) {
        return 0.f;
    }
    const float disc = (d.vel.z * d.vel.z) - (2.f * d.gravity * dz);
    return (-d.vel.z - std::sqrt(std::max(disc, 0.f))) / d.gravity;
}

void emit_drop(const Look &look, const Drop &d, float t, FxFrame &out)
{
    const float age = t - d.born;
    if (age < 0.f) {
        return;
    }
    const float land = drop_land_age(d);
    if (age < land) {
        // 落ちている最中：速いほど縦に伸びる。
        const float vz = d.vel.z + (d.gravity * age);
        const float stretch = 1.f + (0.9f * std::min(1.f, std::fabs(vz) / 5.f));
        FxVoxel v;
        v.center = Vec3{ d.pos.x + (d.vel.x * age), d.pos.y + (d.vel.y * age), d.pos.z + (d.vel.z * age) + (0.5f * d.gravity * age * age) };
        v.size = Vec3{ d.size, d.size, d.size * stretch };
        const FxColour c = liquid_colour(look, 0.55f + (0.35f * rnd(d.seed, 1)));
        v.r = c.r;
        v.g = c.g;
        v.b = c.b;
        v.emissive = look.emissive;
        v.alpha = look.alpha;
        v.part = FxPart::Droplet;
        out.voxels.push_back(v);
        return;
    }
    const float since = age - land;
    if (since >= kSplashSeconds) {
        return;
    }
    const float lx = d.pos.x + (d.vel.x * land);
    const float ly = d.pos.y + (d.vel.y * land);
    // 床に小さく広がって消える。
    PuddleTiming timing{ 0.05f, 0.04f, 0.22f };
    emit_puddle_look(look, lx, ly, 0.10f * d.splash * std::sqrt(look.volume), d.born + land, timing, 0.9f, 0.06f, d.seed, t, out);
    // 数粒が跳ねる。
    FxSpray s;
    s.origin = Vec3{ lx, ly, 0.03f };
    s.dir = Vec3{ 0.f, 0.f, 1.f };
    s.cone = 1.1f;
    s.speed_min = 0.5f * (1.f - (0.4f * look.thick));
    s.speed_max = 1.3f * (1.f - (0.4f * look.thick));
    s.gravity = -10.f;
    s.drag = 1.0f;
    s.born = d.born + land;
    s.life_min = 0.15f;
    s.life_max = 0.28f;
    s.count = 3;
    s.colour0 = look.light;
    s.colour1 = look.core;
    s.size0 = 0.035f * d.splash;
    s.size1 = 0.015f;
    s.emissive0 = look.emissive;
    s.emissive1 = look.emissive * 0.5f;
    s.alpha0 = look.alpha;
    s.alpha1 = 0.f;
    s.floor = FxFloor::Vanish;
    s.floor_z = 0.f;
    s.part = FxPart::Droplet;
    s.seed = d.seed + 17u;
    (void)fx_emit_spray(s, t, out);
}

/* ============================================================ 飛んでいる間：液体のかたまり＋雫 */

//! 飛ぶ向き（進む道の接線）。
Vec3 flight_direction(const FxSpec &spec, const VoxelFxParams &params, float s)
{
    constexpr float kEps = 0.03f;
    const Vec3 a = fx_flight_position(spec, params, fx_clamp01(s - kEps));
    const Vec3 b = fx_flight_position(spec, params, fx_clamp01(s + kEps));
    const Vec3 d = b - a;
    const float len = length_of(d);
    return (len < 1e-5f) ? Vec3{ 1.f, 0.f, 0.f } : (d * (1.f / len));
}

float blob_radius(const FxSpec &spec, const Look &look)
{
    return (spec.explode ? 0.27f : 0.20f) * std::sqrt(look.volume);
}

//! 液体のかたまり：進む向きに少し伸びたしずく形。縁が揺れて、後ろに細い尾を引く。
void emit_blob(const FxStyleCtx &ctx, const Look &look, FxFrame &out)
{
    const FxSpec &spec = ctx.spec;
    const float fly = std::max(ctx.fly, 1e-4f);
    const float s = fx_clamp01(ctx.t / fly);
    const Vec3 centre = fx_flight_position(spec, ctx.params, s);
    const Vec3 dir = flight_direction(spec, ctx.params, s);
    const int frame_index = static_cast<int>(std::floor(ctx.t * 18.f));
    const float radius = blob_radius(spec, look);
    const float v = radius / 3.0f;
    constexpr float kStretch = 1.35f;
    const int n = static_cast<int>(std::ceil((radius * kStretch) / v)) + 1;
    for (int i = -n; i <= n; ++i) {
        for (int j = -n; j <= n; ++j) {
            for (int k = -n; k <= n; ++k) {
                const Vec3 p{ static_cast<float>(i) * v, static_cast<float>(j) * v, static_cast<float>(k) * v };
                const float along = dot(p, dir);
                const float perp2 = std::max(dot(p, p) - (along * along), 0.f);
                const float d = std::sqrt(((along / kStretch) * (along / kStretch)) + perp2) / radius;
                if (d > 1.f) {
                    continue;
                }
                const int cell = ((i + 9) * 361) + ((j + 9) * 19) + (k + 9);
                // 外縁ほど出たり消えたりして、表面が波打つ。
                if (rnd(spec.seed, cell, frame_index, 1) < (std::max(d - 0.55f, 0.f) * 1.2f)) {
                    continue;
                }
                const float grow = 0.85f + (0.28f * rnd(spec.seed, cell, frame_index, 2));
                FxVoxel vx;
                vx.center = centre + p;
                vx.size = Vec3{ v * grow, v * grow, v * grow };
                const float shade = 0.40f + (0.30f * (p.z / radius)) + (0.25f * (rnd(spec.seed, cell, frame_index, 3) - 0.5f)) - (0.15f * d);
                const FxColour c = (rnd(spec.seed, cell, frame_index, 4) < 0.10f) ? look.light : liquid_colour(look, shade);
                vx.r = c.r;
                vx.g = c.g;
                vx.b = c.b;
                vx.emissive = look.emissive * (0.85f + (0.3f * rnd(spec.seed, cell, frame_index, 5)));
                vx.alpha = look.alpha;
                vx.part = FxPart::OrbCore;
                out.voxels.push_back(vx);
            }
        }
    }
    // 後ろに引く細い尾（重みで少し垂れる）。
    for (int m = 0; m < 4; ++m) {
        const float back = (radius * kStretch) + (0.08f * static_cast<float>(m + 1));
        const float size = v * (1.25f - (0.22f * static_cast<float>(m)));
        FxVoxel vx;
        vx.center = centre - (dir * back) + Vec3{ 0.f, 0.f, -0.012f * static_cast<float>((m + 1) * (m + 1)) };
        vx.size = Vec3{ size, size, size };
        const FxColour c = liquid_colour(look, 0.45f);
        vx.r = c.r;
        vx.g = c.g;
        vx.b = c.b;
        vx.emissive = look.emissive;
        vx.alpha = look.alpha;
        vx.part = FxPart::OrbTail;
        out.voxels.push_back(vx);
    }
    add_pulse_light(look, centre.x, centre.y, centre.z + 0.35f, 3.4f, 1.3f * (0.85f + (0.3f * rnd(spec.seed, frame_index, 7))), 0.05f, 10.f, out);
    out.projectile_visible = true;
    out.projectile_pos = centre;
    out.projectile_dir = dir;
}

//! かたまりから垂れる雫。着弾のあとも落ち続けるので、`impact` からも呼ぶ。
void emit_flight_drips(const FxStyleCtx &ctx, const Look &look, FxFrame &out)
{
    const FxSpec &spec = ctx.spec;
    const float fly = ctx.fly;
    if (fly <= 0.f) {
        return;
    }
    const float rate = 24.f * (1.f - (0.35f * look.thick));
    const float radius = blob_radius(spec, look);
    const int last = static_cast<int>(std::floor(std::min(ctx.t, fly) * rate));
    for (int i = 0; i <= last; ++i) {
        const float born = static_cast<float>(i) / rate;
        if ((ctx.t - born) > 0.95f) {
            continue;
        }
        const float s = fx_clamp01(born / std::max(fly, 1e-4f));
        const Vec3 at = fx_flight_position(spec, ctx.params, s);
        Drop d;
        d.pos = at + Vec3{ (rnd(spec.seed, i, 31) - 0.5f) * radius * 0.8f, (rnd(spec.seed, i, 32) - 0.5f) * radius * 0.8f, -radius * 0.55f };
        d.vel = Vec3{ 0.f, 0.f, -0.2f };
        d.born = born;
        d.gravity = -12.f * (1.f - (0.3f * look.thick));
        d.size = (0.06f + (0.03f * rnd(spec.seed, i, 33))) * look.droplet;
        d.splash = 0.75f;
        d.seed = spec.seed + static_cast<std::uint32_t>(i * 7919);
        emit_drop(look, d, ctx.t, out);
    }
}

/* ============================================================ 体にかかる（ドバっ → 伝って滴る） */

constexpr int kCoatCols = 12;
constexpr int kCoatRows = 12;
//! 体の覆いを、タイルの面より画面のほうへ浮かせる量（マス。画面の向きに沿うので画面上のずれは出ない）。
constexpr float kCoatOffset = 0.30f;
//! 1 回の爆発で覆いをかける実体の最大数（ボクセルの数を抑える）。
constexpr int kMaxCoats = 6;

//! 対象のマスにいる実体の影絵（タイルの不透明な所）。
struct Body {
    bool present{ false };
    float width{ 0.8f };
    float height{ 0.8f };
    std::vector<std::uint8_t> mask; //!< 上の行から。`kCoatCols × kCoatRows`
};

Body sample_body(const FxSpec &spec, int gx, int gy)
{
    Body b;
    b.mask.assign(static_cast<std::size_t>(kCoatCols * kCoatRows), 0);
    if (!spec.tile_at) {
        // 引く手段が無い作り：楕円で代用する。
        for (int j = 0; j < kCoatRows; ++j) {
            for (int i = 0; i < kCoatCols; ++i) {
                const float x = ((static_cast<float>(i) + 0.5f) / static_cast<float>(kCoatCols) * 2.f) - 1.f;
                const float y = ((static_cast<float>(kCoatRows - j) - 0.5f) / static_cast<float>(kCoatRows) * 2.f) - 1.f;
                if (((x * x) + (y * y)) <= 1.f) {
                    b.mask[static_cast<std::size_t>((j * kCoatCols) + i)] = 1;
                }
            }
        }
        b.present = true;
        return b;
    }
    FxTileImage img;
    if (!spec.tile_at(gx, gy, img) || (img.width <= 0) || (img.height <= 0)
        || (img.rgba.size() < (static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4u))) {
        return b;
    }
    b.width = img.width_cells;
    b.height = img.height_cells;
    int any = 0;
    for (int j = 0; j < kCoatRows; ++j) {
        for (int i = 0; i < kCoatCols; ++i) {
            int hit = 0;
            for (int sy = 0; sy < 3; ++sy) {
                for (int sx = 0; sx < 3; ++sx) {
                    const int px = std::min(img.width - 1, ((((i * 3) + sx) * img.width) + (img.width / 2)) / (kCoatCols * 3));
                    const int py = std::min(img.height - 1, ((((j * 3) + sy) * img.height) + (img.height / 2)) / (kCoatRows * 3));
                    const std::size_t at = ((static_cast<std::size_t>(py) * static_cast<std::size_t>(img.width)) + static_cast<std::size_t>(px)) * 4u;
                    hit += (img.rgba[at + 3] >= 96) ? 1 : 0;
                }
            }
            if (hit >= 4) {
                b.mask[static_cast<std::size_t>((j * kCoatCols) + i)] = 1;
                ++any;
            }
        }
    }
    b.present = any > 0;
    return b;
}

//! 体の覆い：液体がドバっと広がってかぶさり、上から縁が下がって（伝って）、足元から滴る。`te` はかかった瞬間からの秒。
void emit_coat(const FxSpec &spec, const Look &look, const Body &body, int gx, int gy, float te, std::uint32_t seed, FxFrame &out)
{
    const float life = look.coat_life;
    const Vec3 toward = fx_toward_camera(spec);
    const Vec3 right = horizontal_unit(spec.cam_right, Vec3{ 1.f, 0.f, 0.f });
    const float yaw = std::atan2(right.y, right.x);
    const Vec3 foot = fx_cell_centre(gx, gy, 0.f) + (toward * kCoatOffset);
    const float cw = body.width / static_cast<float>(kCoatCols);
    const float rh = body.height / static_cast<float>(kCoatRows);
    const float fade = 1.f - fx_ramp(te, life - 0.3f, life);
    for (int i = 0; i < kCoatCols; ++i) {
        const float x = ((static_cast<float>(i) + 0.5f) - (static_cast<float>(kCoatCols) * 0.5f)) * cw;
        const float r1 = rnd(seed, i, 101);
        const float r2 = rnd(seed, i, 102);
        const float hold = 0.18f + (0.30f * r1);
        const float speed = (0.70f + (0.60f * r2)) * (1.f - (0.6f * look.thick));
        const float level = body.height - (std::max(0.f, te - hold) * speed); //!< 覆いの上端の高さ（足元から）
        int bottom_row = -1;
        for (int j = 0; j < kCoatRows; ++j) {
            if (body.mask[static_cast<std::size_t>((j * kCoatCols) + i)] == 0) {
                continue;
            }
            bottom_row = j;
            if ((te < 0.f) || (te >= life)) {
                continue;
            }
            const float zc = (static_cast<float>(kCoatRows - j) - 0.5f) * rh;
            if (zc > level) {
                continue; // 伝って流れ落ちた
            }
            const float dist = std::hypot(x / (body.width * 0.5f), (zc / body.height) - 0.55f);
            const float delay = (0.17f * dist) + (0.03f * rnd(seed, i, j, 103));
            if (te < delay) {
                continue; // まだ届いていない（当たった所から広がる）
            }
            const float over = 1.f + (0.45f * (1.f - fx_ramp(te - delay, 0.f, 0.09f))); //!< ドバっと膨らんで落ち着く
            const float shade = 0.30f + (0.55f * (zc / body.height)) + (0.25f * (rnd(seed, i, j, 104) - 0.5f));
            const FxColour c = cooled(look, (rnd(seed, i, j, 105) < 0.14f) ? look.light : liquid_colour(look, shade), te / life);
            FxVoxel v;
            v.center = foot + (right * x) + Vec3{ 0.f, 0.f, zc } + (toward * (0.02f * (rnd(seed, i, j, 106) - 0.5f)));
            v.size = Vec3{ cw * 1.08f * over, 0.07f * over, rh * 1.08f * over };
            v.yaw = yaw;
            v.r = c.r;
            v.g = c.g;
            v.b = c.b;
            v.emissive = look.emissive * heat_of(look, te / life) * (0.85f + (0.3f * rnd(seed, i, j, 107)));
            v.alpha = look.alpha * fade;
            v.part = FxPart::Droplet;
            if (v.alpha > 0.01f) {
                out.voxels.push_back(v);
            }
        }
        // 足元から滴る：ふくらんで、ちぎれて落ちる。
        if ((bottom_row >= 0) && (rnd(seed, i, 108) < 0.6f)) {
            const float zb = static_cast<float>(kCoatRows - bottom_row - 1) * rh;
            for (int k = 0; k < 3; ++k) {
                const float born = 0.14f + (0.35f * r1) + (static_cast<float>(k) * (0.24f + (0.14f * rnd(seed, i, k, 109))));
                if (born > (life - 0.35f)) {
                    continue;
                }
                const float hang = born - te;
                if ((hang > 0.f) && (hang < 0.14f)) {
                    const float u = 1.f - (hang / 0.14f);
                    FxVoxel v;
                    v.center = foot + (right * x) + Vec3{ 0.f, 0.f, zb - (0.5f * (0.02f + (0.05f * u))) };
                    v.size = Vec3{ 0.04f + (0.015f * u), 0.05f, 0.03f + (0.07f * u) };
                    v.yaw = yaw;
                    const FxColour c = liquid_colour(look, 0.6f);
                    v.r = c.r;
                    v.g = c.g;
                    v.b = c.b;
                    v.emissive = look.emissive * heat_of(look, te / life);
                    v.alpha = look.alpha;
                    v.part = FxPart::Droplet;
                    out.voxels.push_back(v);
                }
                Drop d;
                d.pos = foot + (right * x) + Vec3{ 0.f, 0.f, zb };
                d.vel = Vec3{ 0.f, 0.f, 0.f };
                d.born = born;
                d.gravity = -11.f * (1.f - (0.3f * look.thick));
                d.size = (0.05f * look.droplet) * (0.85f + (0.3f * rnd(seed, i, k, 110)));
                d.splash = 0.8f;
                d.seed = seed + static_cast<std::uint32_t>((i * 31) + k);
                emit_drop(look, d, te, out);
            }
        }
    }
}

//! 着弾の弾け：当たった所から液体が四方へ散る（マスの中に収まる）。体があれば体の高さ、無ければ床の近くから。
void emit_splash_spray(const FxSpec &spec, const Look &look, int gx, int gy, bool on_body, float te, std::uint32_t seed, FxFrame &out)
{
    const Vec3 toward = fx_toward_camera(spec);
    const Vec3 hdir = horizontal_unit(toward, Vec3{ 0.f, 1.f, 0.f });
    FxSpray s;
    s.origin = fx_cell_centre(gx, gy, on_body ? 0.50f : 0.12f) + (hdir * 0.12f);
    s.dir = Vec3{ 0.f, 0.f, 1.f };
    s.cone = on_body ? (kPi * 0.8f) : 1.2f;
    s.scatter = 0.05f;
    s.speed_min = 0.7f * (1.f - (0.4f * look.thick));
    s.speed_max = 1.5f * (1.f - (0.4f * look.thick));
    s.gravity = -12.f;
    s.drag = 2.5f;
    s.born = 0.f;
    s.duration = 0.05f;
    s.life_min = 0.22f;
    s.life_max = 0.40f;
    s.count = on_body ? 22 : 26;
    s.colour0 = look.light;
    s.colour1 = look.core;
    s.size0 = 0.06f * look.droplet;
    s.size1 = 0.025f;
    s.emissive0 = look.emissive * 1.1f;
    s.emissive1 = look.emissive * 0.6f;
    s.alpha0 = look.alpha;
    s.alpha1 = 0.2f;
    s.floor = FxFloor::Vanish;
    s.floor_z = 0.f;
    s.part = FxPart::Droplet;
    s.seed = seed + 5u;
    (void)fx_emit_spray(s, te, out);
}

//! 床の水たまり（体の足元の少し手前）と波紋。
void emit_floor_puddle(const FxSpec &spec, const Look &look, int gx, int gy, bool shifted, float born, float te, std::uint32_t seed, int max_rings,
    FxFrame &out)
{
    const Vec3 hdir = horizontal_unit(fx_toward_camera(spec), Vec3{ 0.f, 1.f, 0.f });
    const float cx = static_cast<float>(gx) + 0.5f + (shifted ? (hdir.x * 0.12f) : 0.f);
    const float cy = static_cast<float>(gy) + 0.5f + (shifted ? (hdir.y * 0.12f) : 0.f);
    emit_puddle_look(look, cx, cy, std::min(0.32f, 0.28f * look.volume), born, puddle_timing(look), 0.95f, 0.09f, seed + 11u, te, out);
    emit_ripple(look, cx, cy, born + 0.03f, te, seed + 12u, max_rings, out);
}

//! 酸などの煙：床の水たまりから（と、`on_body` なら対象の体からも）立つ。
void emit_acid_smoke(const FxSpec &spec, const Look &look, int gx, int gy, bool on_body, float body_born, float puddle_born, float te, std::uint32_t seed,
    FxFrame &out)
{
    if (look.puddle_smoke > 0.f) {
        emit_mist_look(look, fx_cell_centre(gx, gy, 0.05f), 0.2f, puddle_born, 0.8f, 0.8f, static_cast<int>(std::lround(8.f * look.puddle_smoke)), 0.35f,
            0.13f, look.puddle_smoke, seed + 21u, te, out);
    }
    if (look.acid && on_body) {
        const Vec3 origin = fx_cell_centre(gx, gy, 0.45f) + (fx_toward_camera(spec) * 0.15f);
        emit_mist_look(look, origin, 0.18f, body_born, 0.9f, 0.9f, 10, 0.5f, 0.14f, 1.0f, seed + 22u, te, out);
    }
}

//! かかってから「すべて消えるまで」の秒（床に着いた瞬間から数える）。
float tail_seconds(const Look &look)
{
    float s = std::max(look.coat_life + 0.5f, puddle_timing(look).total() + 0.30f);
    if (look.acid) {
        s = std::max(s, 2.0f);
    }
    if (look.skull) {
        s = std::max(s, 1.5f);
    }
    return s;
}

//! ボルト：対象に液体がかかる（弾け・体の覆い・滴り・水たまり・煙・どくろ）。`te` は着弾からの秒。
void emit_drench(const FxSpec &spec, const Look &look, int gx, int gy, float te, FxFrame &out)
{
    const std::uint32_t seed = cell_seed(spec.seed, gx, gy);
    bool body_present = false;
    Body body;
    if ((te >= 0.f) && (te < tail_seconds(look))) {
        body = sample_body(spec, gx, gy);
        body_present = body.present;
    }
    emit_splash_spray(spec, look, gx, gy, body_present, te, seed, out);
    if (body_present) {
        emit_coat(spec, look, body, gx, gy, te, seed, out);
    }
    emit_floor_puddle(spec, look, gx, gy, body_present, 0.24f, te, seed, 2, out);
    emit_acid_smoke(spec, look, gx, gy, body_present, 0.1f, 0.30f, te, seed, out);
    if (look.skull) {
        emit_skull(spec, gx, gy, 0.25f, te, out);
    }
    const float cx = static_cast<float>(gx) + 0.5f;
    const float cy = static_cast<float>(gy) + 0.5f;
    add_pulse_light(look, cx, cy, 1.0f, 3.2f, 1.8f, te, 0.55f * (1.f + look.thick), out);
}

/* ============================================================ 範囲のマスごとに降りかかる（ボール・ジャンプ・飛ばない魔法） */

//! 降りかかってから床に着くまでの秒。
constexpr float kPourSeconds = 0.28f;
//! 1 回の爆発でどくろを出す最大数。
constexpr int kMaxSkulls = 24;

//! 範囲のマスの並び。無ければ中心の 1 マスだけ。
std::vector<FxBurstCell> area_cells(const FxSpec &spec)
{
    if (!spec.cells.empty()) {
        return spec.cells;
    }
    return { FxBurstCell{ spec.centre_gx, spec.centre_gy, 0 } };
}

float area_seconds(const FxSpec &spec, const Look &look, float ring_delay)
{
    int last = 0;
    for (const FxBurstCell &c : area_cells(spec)) {
        last = std::max(last, c.ring);
    }
    return (static_cast<float>(last) * ring_delay) + 0.03f + kPourSeconds + tail_seconds(look);
}

//! マスごとに液体が上から降りかかり、床で弾けて水たまりになる（中心から外へ `ring_delay` ずつ遅れる）。`te` は始まりからの秒。
void emit_area(const FxSpec &spec, const Look &look, const std::vector<FxBurstCell> &cells, float ring_delay, float te, FxFrame &out)
{
    const bool in_window = te < area_seconds(spec, look, ring_delay);
    int bodies = 0;
    int skulls = 0;
    int max_ring = 0;
    float sum_x = 0.f;
    float sum_y = 0.f;
    for (const FxBurstCell &c : cells) {
        max_ring = std::max(max_ring, c.ring);
        sum_x += static_cast<float>(c.gx) + 0.5f;
        sum_y += static_cast<float>(c.gy) + 0.5f;
    }
    // 体にもかかる実体：中心に近いマスから、画面に出ているものを最大 `kMaxCoats` 体まで。
    std::vector<Body> bodies_at(cells.size());
    if (in_window) {
        std::vector<std::size_t> order(cells.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::stable_sort(order.begin(), order.end(), [&cells](std::size_t a, std::size_t b) { return cells[a].ring < cells[b].ring; });
        for (const std::size_t i : order) {
            if (bodies >= kMaxCoats) {
                break;
            }
            Body body = sample_body(spec, cells[i].gx, cells[i].gy);
            if (body.present) {
                ++bodies;
                bodies_at[i] = std::move(body);
            }
        }
    }
    for (std::size_t ci = 0; ci < cells.size(); ++ci) {
        const FxBurstCell &c = cells[ci];
        const std::uint32_t cs = cell_seed(spec.seed, c.gx, c.gy);
        const float cx = static_cast<float>(c.gx) + 0.5f;
        const float cy = static_cast<float>(c.gy) + 0.5f;
        const float start = (static_cast<float>(c.ring) * ring_delay) + (0.03f * rnd(cs, 1));
        const float land = start + kPourSeconds;
        // 上から降りかかる液体のかたまり（マスの中に収める）。
        for (int k = 0; k < 4; ++k) {
            Drop d;
            d.pos = Vec3{ cx + ((rnd(cs, k, 2) - 0.5f) * 0.46f), cy + ((rnd(cs, k, 3) - 0.5f) * 0.46f), 1.0f + (0.3f * rnd(cs, k, 4)) };
            d.vel = Vec3{ 0.f, 0.f, -2.2f };
            d.born = start + (static_cast<float>(k) * 0.02f) + (0.05f * rnd(cs, k, 5));
            d.gravity = -15.f * (1.f - (0.3f * look.thick));
            d.size = (0.075f + (0.03f * rnd(cs, k, 6))) * look.droplet;
            d.splash = 1.1f;
            d.seed = cs + static_cast<std::uint32_t>(k * 131);
            emit_drop(look, d, te, out);
        }
        // 着いた瞬間の跳ね。
        FxSpray s;
        s.origin = Vec3{ cx, cy, 0.05f };
        s.dir = Vec3{ 0.f, 0.f, 1.f };
        s.cone = 0.9f;
        s.scatter = 0.12f;
        s.speed_min = 0.5f * (1.f - (0.4f * look.thick));
        s.speed_max = 1.3f * (1.f - (0.4f * look.thick));
        s.gravity = -10.f;
        s.drag = 2.5f;
        s.born = land;
        s.life_min = 0.25f;
        s.life_max = 0.40f;
        s.count = 5;
        s.colour0 = look.light;
        s.colour1 = look.core;
        s.size0 = 0.05f * look.droplet;
        s.size1 = 0.02f;
        s.emissive0 = look.emissive;
        s.emissive1 = look.emissive * 0.6f;
        s.alpha0 = look.alpha;
        s.alpha1 = 0.2f;
        s.floor = FxFloor::Vanish;
        s.floor_z = 0.f;
        s.part = FxPart::Droplet;
        s.seed = cs + 5u;
        (void)fx_emit_spray(s, te, out);
        // 床の小さな水たまり（と、水なら波紋）。
        emit_floor_puddle(spec, look, c.gx, c.gy, false, land, te, cs, (c.ring == 0) ? 2 : 0, out);
        // 画面に出ている実体には、体にもかかる。
        const bool on_body = bodies_at[ci].present;
        if (on_body && (te >= land)) {
            emit_coat(spec, look, bodies_at[ci], c.gx, c.gy, te - land, cs, out);
        }
        emit_acid_smoke(spec, look, c.gx, c.gy, on_body, land + 0.1f, land + 0.3f, te, cs, out);
        if (look.skull && (skulls < kMaxSkulls)) {
            ++skulls;
            emit_skull(spec, c.gx, c.gy, land + 0.05f, te, out);
        }
    }
    // 点光源：範囲の広さに合わせて強く・大きく（粘る溶岩は長く残る）。
    if (!cells.empty()) {
        const float n = static_cast<float>(cells.size());
        add_pulse_light(look, sum_x / n, sum_y / n, 1.2f, static_cast<float>(max_ring) + 3.2f, 1.5f + (0.6f * static_cast<float>(max_ring)), te - kPourSeconds + 0.1f,
            0.55f * (1.f + look.thick), out);
    }
}

/* ============================================================ 段階の関数 */

//! 飛んでいる間（ボルト・ボール）：液体のかたまりが雫を垂らしながら飛ぶ。
void liquid_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    const Look &look = look_of(ctx.spec);
    emit_blob(ctx, look, out);
    emit_flight_drips(ctx, look, out);
}

//! 着弾のあと：ボルトは対象にかかる。ボールは範囲のマスごとに降りかかる。飛んでいる間に垂れた雫は、ここでも落ち続ける。
void liquid_impact(const FxStyleCtx &ctx, FxFrame &out)
{
    const Look &look = look_of(ctx.spec);
    emit_flight_drips(ctx, look, out);
    if (ctx.spec.explode) {
        emit_area(ctx.spec, look, area_cells(ctx.spec), ctx.params.ring_delay, ctx.te, out);
    } else {
        emit_drench(ctx.spec, look, ctx.spec.centre_gx, ctx.spec.centre_gy, ctx.te, out);
    }
}

float liquid_impact_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Look &look = look_of(spec);
    if (spec.explode) {
        return area_seconds(spec, look, params.ring_delay);
    }
    return std::max(tail_seconds(look) + 0.1f, 1.0f);
}

//! ビーム：経路に沿って液体の筋が走り、筋から雫が落ちて床で弾ける。終点の対象にはボルトと同じようにかかる。
void liquid_beam(const FxStyleCtx &ctx, FxFrame &out)
{
    const FxSpec &spec = ctx.spec;
    const Look &look = look_of(spec);
    const float delay = ctx.params.beam_cell_delay;
    const std::size_t segments = (spec.route.size() > 1) ? (spec.route.size() - 1) : 0;
    constexpr int kPerSegment = 6;
    constexpr float kStreamLife = 0.20f;
    for (std::size_t i = 0; i < segments; ++i) {
        const Vec3 a = spec.route[i];
        const Vec3 b = spec.route[i + 1];
        for (int k = 0; k < kPerSegment; ++k) {
            const float frac = (static_cast<float>(k) + 0.5f) / static_cast<float>(kPerSegment);
            const float start = (static_cast<float>(i) + frac) * delay;
            const float age = ctx.t - start;
            if (age < 0.f) {
                continue;
            }
            const int id = static_cast<int>((i * 16u) + static_cast<std::size_t>(k));
            const Vec3 p = a + ((b - a) * frac);
            if (age < kStreamLife) {
                // 筋：うねりながら流れる。芯（濃い）と周りの液（薄い）。
                const float wave = std::sin((age * 28.f) + (static_cast<float>(id) * 1.3f));
                const float fade = 1.f - fx_ramp(age, kStreamLife - 0.08f, kStreamLife);
                const float sag = 0.6f * age * age;
                FxVoxel core;
                core.center = p + Vec3{ 0.f, 0.f, (0.04f * wave) - sag };
                const float size = 0.12f * std::sqrt(look.volume);
                core.size = Vec3{ size * 1.5f, size, size };
                const Vec3 d = b - a;
                float yaw = 0.f;
                float pitch = 0.f;
                if (length_of(d) > 1e-4f) {
                    fx_direction_angles(d * (1.f / length_of(d)), yaw, pitch);
                }
                core.yaw = yaw;
                core.pitch = pitch;
                const FxColour c = liquid_colour(look, 0.55f + (0.3f * rnd(spec.seed, id, 41)));
                core.r = c.r;
                core.g = c.g;
                core.b = c.b;
                core.emissive = look.emissive * 1.15f;
                core.alpha = look.alpha * fade;
                core.part = FxPart::BeamCore;
                if (core.alpha > 0.01f) {
                    out.voxels.push_back(core);
                }
                if ((k & 1) == 0) {
                    FxVoxel halo = core;
                    halo.size = Vec3{ size * 2.0f, size * 1.9f, size * 1.9f };
                    halo.r = look.light.r;
                    halo.g = look.light.g;
                    halo.b = look.light.b;
                    halo.alpha = 0.30f * fade;
                    halo.emissive = look.emissive * 0.8f;
                    halo.part = FxPart::BeamGlow;
                    if (halo.alpha > 0.01f) {
                        out.voxels.push_back(halo);
                    }
                }
            } else if ((k & 1) == 0) {
                // 筋から垂れた雫：床へ落ちて弾ける。
                Drop d;
                d.pos = p + Vec3{ 0.f, 0.f, -0.6f * kStreamLife * kStreamLife };
                d.vel = Vec3{ 0.f, 0.f, 0.f };
                d.born = start + kStreamLife;
                d.gravity = -12.f * (1.f - (0.3f * look.thick));
                d.size = 0.06f * look.droplet;
                d.splash = 1.0f;
                d.seed = spec.seed + static_cast<std::uint32_t>(id * 977);
                emit_drop(look, d, ctx.t, out);
            }
        }
    }
    // 終点の対象にかかる。
    const float te_end = ctx.t - (static_cast<float>(segments) * delay);
    emit_drench(spec, look, spec.centre_gx, spec.centre_gy, te_end, out);
    // 筋の点光源（真ん中）。
    if (segments > 0) {
        const float life = (static_cast<float>(segments) * delay) + 0.25f;
        const float u = fx_clamp01(ctx.t / life);
        const Vec3 mid = (spec.route.front() + spec.route.back()) * 0.5f;
        PointLight light;
        light.x = mid.x;
        light.y = mid.y;
        light.z = 0.9f;
        light.radius = std::min(7.f, 2.8f + (0.25f * static_cast<float>(segments)));
        light.r = look.glow.r;
        light.g = look.glow.g;
        light.b = look.glow.b;
        light.intensity = 1.6f * look.glow_k * ((ctx.t < 0.03f) ? (ctx.t / 0.03f) : ((1.f - u) * (1.f - u)));
        if (light.intensity > 0.01f) {
            out.lights.push_back(light);
        }
    }
}

float liquid_beam_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Look &look = look_of(spec);
    const float segments = static_cast<float>((spec.route.size() > 1) ? (spec.route.size() - 1) : 0);
    const float flow = 0.20f + 0.60f + kSplashSeconds; //!< 筋 → 雫が落ちる → 弾ける
    return (segments * params.beam_cell_delay) + std::max(std::max(tail_seconds(look) + 0.1f, 1.0f), flow);
}

//! ブレス：撃った者から霧が広がる（範囲のマスの中で、近いマスから順に）。霧は少し昇り、凝った雫が落ちて、床に薄く濡れる。
void liquid_breath(const FxStyleCtx &ctx, FxFrame &out)
{
    const FxSpec &spec = ctx.spec;
    const Look &look = look_of(spec);
    const std::vector<FxBurstCell> cells = area_cells(spec);
    const float delay = ctx.params.breath_ring_delay;
    int max_ring = 0;
    float sum_x = 0.f;
    float sum_y = 0.f;
    for (const FxBurstCell &c : cells) {
        max_ring = std::max(max_ring, c.ring);
        sum_x += static_cast<float>(c.gx) + 0.5f;
        sum_y += static_cast<float>(c.gy) + 0.5f;
        const std::uint32_t cs = cell_seed(spec.seed, c.gx, c.gy);
        const float cx = static_cast<float>(c.gx) + 0.5f;
        const float cy = static_cast<float>(c.gy) + 0.5f;
        const float start = static_cast<float>(c.ring) * delay;
        // 霧のかたまり（1 マスに 9 つ。大きくなっても縁がマスの中）。
        emit_mist_look(look, Vec3{ cx, cy, 0.10f }, 0.20f, start, 0.30f, 0.95f, 9, 0.22f * (1.f - (0.3f * look.thick)), 0.17f, 1.9f, cs, ctx.t, out);
        // 床が薄く濡れる。
        emit_puddle_look(look, cx, cy, 0.25f, start + 0.18f, PuddleTiming{ 0.20f, 0.20f, 0.40f }, 0.55f, 0.10f, cs + 3u, ctx.t, out);
        // 霧から凝った雫が落ちる。
        for (int k = 0; k < 2; ++k) {
            Drop d;
            d.pos = Vec3{ cx + ((rnd(cs, k, 8) - 0.5f) * 0.4f), cy + ((rnd(cs, k, 9) - 0.5f) * 0.4f), 0.45f + (0.25f * rnd(cs, k, 10)) };
            d.vel = Vec3{ 0.f, 0.f, 0.f };
            d.born = start + 0.25f + (0.30f * rnd(cs, k, 11));
            d.gravity = -5.f;
            d.size = 0.035f * look.droplet;
            d.splash = 0.6f;
            d.seed = cs + static_cast<std::uint32_t>(k * 17);
            emit_drop(look, d, ctx.t, out);
        }
        // 酸・溶岩の水たまりから立つ煙（薄く）。
        if (look.puddle_smoke > 0.f) {
            emit_mist_look(look, Vec3{ cx, cy, 0.05f }, 0.13f, start + 0.3f, 0.4f, 0.6f, 1, 0.3f, 0.11f, look.puddle_smoke, cs + 9u, ctx.t, out);
        }
    }
    if (!cells.empty()) {
        const float n = static_cast<float>(cells.size());
        const float spread = static_cast<float>(max_ring) * delay;
        add_pulse_light(look, sum_x / n, sum_y / n, 1.2f, (static_cast<float>(max_ring) * 0.5f) + 3.5f, 1.0f + (0.4f * static_cast<float>(max_ring) * 0.5f),
            ctx.t - (spread * 0.5f), 0.7f + spread, out);
    }
}

float liquid_breath_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    int last = 0;
    for (const FxBurstCell &c : area_cells(spec)) {
        last = std::max(last, c.ring);
    }
    return (static_cast<float>(last) * params.breath_ring_delay) + 1.35f;
}

//! 直接（ジャンプ）：範囲のマスの上から液体が降りかかる（ボールの降りかかりと同じ）。
void liquid_burst(const FxStyleCtx &ctx, FxFrame &out)
{
    emit_area(ctx.spec, look_of(ctx.spec), area_cells(ctx.spec), ctx.params.ring_delay, ctx.t, out);
}

float liquid_burst_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    return area_seconds(spec, look_of(spec), params.ring_delay);
}

//! 飛ばない魔法・視界内の全員・足元だけ。
void liquid_glow(const FxStyleCtx &ctx, FxFrame &out)
{
    const FxSpec &spec = ctx.spec;
    const Look &look = look_of(spec);
    switch (spec.glow_mode) {
    case FxGlowMode::AllInSight:
        // 視界内の全員：属性の色（液体の色）の光の波が画面の端から端へ走る。
        fx_emit_all_sight_wave(ctx, out, look.light);
        break;
    case FxGlowMode::Touch: {
        // 足元だけ：足元に水たまりが広がり、周りへ液体が跳ねる。
        const int gx = spec.centre_gx;
        const int gy = spec.centre_gy;
        const std::uint32_t cs = cell_seed(spec.seed, gx, gy);
        const Vec3 hdir = horizontal_unit(fx_toward_camera(spec), Vec3{ 0.f, 1.f, 0.f });
        const float cx = static_cast<float>(gx) + 0.5f + (hdir.x * 0.14f); //!< 足元の手前（実体の陰に隠れないように）
        const float cy = static_cast<float>(gy) + 0.5f + (hdir.y * 0.14f);
        emit_puddle_look(look, cx, cy, std::min(0.36f, 0.34f * std::sqrt(look.volume)), 0.f, puddle_timing(look), 0.95f, 0.075f, cs + 11u, ctx.t, out);
        emit_ripple(look, cx, cy, 0.03f, ctx.t, cs + 12u, 2, out);
        FxSpray s;
        s.origin = Vec3{ cx + (hdir.x * 0.10f), cy + (hdir.y * 0.10f), 0.05f }; //!< 実体の前の床から跳ねる
        s.dir = Vec3{ 0.f, 0.f, 1.f };
        s.cone = 0.6f;
        s.scatter = 0.06f;
        s.speed_min = 1.6f * (1.f - (0.4f * look.thick));
        s.speed_max = 2.2f * (1.f - (0.4f * look.thick));
        s.gravity = -6.f;
        s.drag = 1.5f;
        s.born = 0.f;
        s.duration = 0.12f;
        s.life_min = 0.30f;
        s.life_max = 0.40f;
        s.count = 24;
        s.colour0 = look.light;
        s.colour1 = look.core;
        s.size0 = 0.07f * look.droplet;
        s.size1 = 0.025f;
        s.emissive0 = look.emissive;
        s.emissive1 = look.emissive * 0.6f;
        s.alpha0 = look.alpha;
        s.alpha1 = 0.2f;
        s.floor = FxFloor::Vanish;
        s.floor_z = 0.f;
        s.part = FxPart::Droplet;
        s.seed = cs + 5u;
        (void)fx_emit_spray(s, ctx.t, out);
        emit_acid_smoke(spec, look, gx, gy, false, 0.f, 0.15f, ctx.t, cs, out);
        if (look.skull) {
            emit_skull(spec, gx, gy, 0.15f, ctx.t, out);
        }
        add_pulse_light(look, cx, cy, 0.9f, 2.8f, 1.3f, ctx.t, 0.55f * (1.f + look.thick), out);
        break;
    }
    case FxGlowMode::Target:
    default:
        // 飛ばない魔法：対象の上から降りかかる（ボールの 1 マスと同じ）。
        emit_area(spec, look, area_cells(spec), ctx.params.ring_delay, ctx.t, out);
        break;
    }
}

float liquid_glow_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Look &look = look_of(spec);
    switch (spec.glow_mode) {
    case FxGlowMode::AllInSight:
        return std::max(params.glow_seconds, 0.9f);
    case FxGlowMode::Touch:
        return tail_seconds(look) + 0.1f;
    case FxGlowMode::Target:
    default:
        return area_seconds(spec, look, params.ring_delay);
    }
}

struct Entry {
    const char *attr;
    FxColour base; //!< 汎用の玉と爆発の色（このファイルの見た目では使わない。登録表と検査のための基準）
    float smoke;
    float emissive;
};

//! このファイルが担当する属性（§5.1）。
const Entry kEntries[] = {
    { "pois", { 0.70f, 0.35f, 0.95f }, 0.60f, 1.0f },
    { "acid", { 0.62f, 0.95f, 0.22f }, 0.45f, 1.0f },
    { "lava_flow", { 1.00f, 0.30f, 0.05f }, 0.50f, 1.2f },
    { "water", { 0.25f, 0.55f, 1.00f }, 0.20f, 1.0f },
    { "water_flow", { 0.30f, 0.75f, 1.00f }, 0.20f, 1.0f },
    { "blood_curse", { 0.55f, 0.04f, 0.06f }, 0.50f, 0.8f },
};

} // namespace

void fx_register_liquid(FxStyleRegistry &registry)
{
    for (const Entry &e : kEntries) {
        FxStyle &s = registry.add(e.attr, e.base, e.smoke, e.emissive);
        s.flight = liquid_flight;
        s.impact = liquid_impact;
        s.impact_seconds = liquid_impact_seconds;
        s.beam = liquid_beam;
        s.beam_seconds = liquid_beam_seconds;
        s.breath = liquid_breath;
        s.breath_seconds = liquid_breath_seconds;
        s.burst = liquid_burst;
        s.burst_seconds = liquid_burst_seconds;
        s.glow = liquid_glow;
        s.glow_seconds = liquid_glow_seconds;
    }
}

} // namespace hd2d
