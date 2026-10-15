/*!
 * @file fx_elemental.cpp
 * @brief その他のダメージの属性の見た目。
 *
 * | 属性 | 見た目 |
 * |---|---|
 * | `fire` | 試作の火の玉と爆発（§4。**今の見た目を保つ**） |
 * | `cold` | 火炎の色違い（水色〜白。玉も爆発も火炎と同じ作りで色だけ変える） |
 * | `ice` | **雹**が飛ぶ（氷の粒が何個も飛び、当たって砕ける） |
 * | `shards` | 雹をグレーの**石礫**で |
 * | `sound` | 輪の形の衝撃波がいくつも広がり、対象が小刻みに震え、**画面全体も短く揺れる** |
 * | `force` | 透明な空気のゆがみの球が飛んで当たり、対象が一瞬のけぞる |
 * | `meteor` | **上から岩が落ちてくる**（効果範囲のマスへ） |
 * | `gravity` | 対象を**上下に押しつぶす** |
 * | `inertial` | **下向きの矢印**が上から下りてくる |
 * | `missile` | 矢に**光の尾**をつける |
 * | `psy_spear` | 対象の上から**黄色い剣が振り下ろされる** |
 * | `chaos` | 色が刻々と変わる玉。着弾するとねじれた渦が広がる |
 * | `nether` | 灰色がかった紫の玉。着弾点から半透明の亡霊の影が立ち上って消える |
 * | `disenchant` | 対象の周りで光る粒が剥がれ落ち、色を失って消える |
 * | `nexus` | 着弾点に空間の裂け目が開いて閉じる。対象が一瞬ずれて二重に見える |
 * | `time` | 対象の上に時計の文字盤の輪が出て、逆回りして消える |
 * | `void_magic` | 黒い球が着弾し、周りの粒を中心へ吸い込んで消える |
 * | `abyss` | 対象の足元に黒い穴が開き、影の触手が這い上がって消える |
 * | `holy_fire` | 白金色の炎。上から光の柱が降りる |
 * | `hell_fire` | 黒と赤の炎。下から噴き上がる |
 * | `rocket` | ボクセルの小さなロケットが煙を引いて飛び、大きな爆発 |
 * | `nuke` | 緑に光る玉。着弾後に緑の霧が少し残って明滅する |
 *
 * ## このファイルの作り
 * 21 属性は、次の 3 つの部品を共有する。属性ごとの違いは `Look`（下の表）に集めてある。
 * - **飛んでいる間**（`Look::flight`）：玉・雹・矢などの本体と尾。無ければ汎用の玉（属性の色の組）。
 * - **マスごとの効果**（`Look::cell`）：着弾のあと、効果範囲の**マス 1 つぶん**を作る関数。ボールは範囲のマスごとに
 *   中心からの距離（`ring`）に応じて遅らせて呼ばれ、ボルト・ビームの終点・飛ばない魔法は着弾点の 1 マスだけ、
 *   ブレスは撃った者に近いマスから順に呼ばれる。**作った粒は、そのマスの外へ出ないよう押し込む**（`clamp_to_cell`）。
 * - **対象の見た目の変化**（`Look::mod`）：押しつぶし・震え・のけぞり・二重。マスごとに呼ばれる。
 *
 * 範囲が広いとき（ブレス）は、マスあたりの量を `Hit::density` で減らす。画面の揺れは `Look::shake` を
 * `fx_add_shake` に通す（cfg の `fx_screen_shake` が効く）。`fire` だけは今の見た目のまま（汎用へ流す）。
 */
#include "render/fx_styles/fx_parts.h"
#include "render/fx_styles/fx_style.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace hd2d {

namespace {

constexpr float kPi = 3.14159265f;

/* ============================================================ 小さな道具 */

float ease_out(float u)
{
    u = fx_clamp01(u);
    return 1.f - ((1.f - u) * (1.f - u));
}

float ease_in(float u)
{
    u = fx_clamp01(u);
    return u * u;
}

FxColour col(float r, float g, float b)
{
    return FxColour{ r, g, b };
}

//! 色相（0〜1 で一周）から明るい色を作る。少し白へ寄せる。
FxColour hue_colour(float h)
{
    h -= std::floor(h);
    const float x = h * 6.f;
    const int i = static_cast<int>(x);
    const float f = x - static_cast<float>(i);
    float r = 1.f;
    float g = 0.f;
    float b = 0.f;
    switch (i) {
    case 0:
        r = 1.f;
        g = f;
        b = 0.f;
        break;
    case 1:
        r = 1.f - f;
        g = 1.f;
        b = 0.f;
        break;
    case 2:
        r = 0.f;
        g = 1.f;
        b = f;
        break;
    case 3:
        r = 0.f;
        g = 1.f - f;
        b = 1.f;
        break;
    case 4:
        r = f;
        g = 0.f;
        b = 1.f;
        break;
    default:
        r = 1.f;
        g = 0.f;
        b = 1.f - f;
        break;
    }
    return fx_mix(FxColour{ r, g, b }, FxColour{ 1.f, 1.f, 1.f }, 0.18f);
}

//! 単位ベクトル `d` に直角な単位ベクトルを 2 つ。
void perp(const Vec3 &d, Vec3 &a, Vec3 &b)
{
    const Vec3 helper = (std::fabs(d.z) < 0.9f) ? Vec3{ 0.f, 0.f, 1.f } : Vec3{ 1.f, 0.f, 0.f };
    a = normalize(cross(d, helper));
    b = normalize(cross(d, a));
}

float length_of(const Vec3 &v)
{
    return std::sqrt(dot(v, v));
}

void put_cube(FxFrame &out, const Vec3 &p, float size, const FxColour &c, float emissive, float alpha, FxPart part, float yaw = 0.f, float pitch = 0.f)
{
    if ((alpha <= 0.004f) || (size <= 0.002f)) {
        return;
    }
    FxVoxel v;
    v.center = p;
    v.size = Vec3{ size, size, size };
    v.r = c.r;
    v.g = c.g;
    v.b = c.b;
    v.alpha = alpha;
    v.emissive = emissive;
    v.yaw = yaw;
    v.pitch = pitch;
    v.part = part;
    out.voxels.push_back(v);
}

//! 始点 `a` から終点 `b` までの細長い箱（太さ `thick` の四角い断面）。
void put_bar(FxFrame &out, const Vec3 &a, const Vec3 &b, float thick, const FxColour &c, float emissive, float alpha, FxPart part)
{
    const Vec3 d = b - a;
    const float len = length_of(d);
    if ((len < 1e-4f) || (alpha <= 0.004f)) {
        return;
    }
    FxVoxel v;
    v.center = a + (d * 0.5f);
    v.size = Vec3{ len, thick, thick };
    v.r = c.r;
    v.g = c.g;
    v.b = c.b;
    v.alpha = alpha;
    v.emissive = emissive;
    fx_direction_angles(d * (1.f / len), v.yaw, v.pitch);
    v.part = part;
    out.voxels.push_back(v);
}

//! 球（立方体の集まり）。
void put_ball(FxFrame &out, const Vec3 &centre, float radius, float voxel, const FxColour &c, float emissive, float alpha, FxPart part)
{
    if (radius < voxel * 0.5f) {
        put_cube(out, centre, std::max(radius * 1.6f, 0.02f), c, emissive, alpha, part);
        return;
    }
    const int n = static_cast<int>(std::ceil(radius / voxel));
    for (int i = -n; i <= n; ++i) {
        for (int j = -n; j <= n; ++j) {
            for (int k = -n; k <= n; ++k) {
                const float d = std::sqrt(static_cast<float>((i * i) + (j * j) + (k * k))) * voxel;
                if (d > radius) {
                    continue;
                }
                put_cube(out, centre + Vec3{ static_cast<float>(i) * voxel, static_cast<float>(j) * voxel, static_cast<float>(k) * voxel }, voxel * 1.02f, c,
                    emissive, alpha, part);
            }
        }
    }
}

/* ============================================================ 飛んでいる間の部品 */

//! 玉の進む向き（正規化）。
Vec3 flight_dir(const FxStyleCtx &ctx, float s)
{
    const Vec3 a = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(s - 0.03f));
    const Vec3 b = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(s + 0.03f));
    const Vec3 d = b - a;
    return (length_of(d) < 1e-5f) ? Vec3{ 1.f, 0.f, 0.f } : normalize(d);
}

float flight_s(const FxStyleCtx &ctx)
{
    return fx_clamp01(ctx.t / std::max(ctx.fly, 1e-4f));
}

//! 魔法の玉（球の形のボクセル）の見た目。
struct OrbLook {
    FxColour c0{ 1.f, 1.f, 1.f };
    FxColour c1{ 0.8f, 0.8f, 0.8f };
    FxColour c2{ 0.4f, 0.4f, 0.4f };
    float e0{ 3.f };
    float e1{ 1.8f };
    float e2{ 0.8f };
    float alpha{ 1.f };
    //! `orb_radius`・`orb_voxel` への倍率。
    float radius{ 1.f };
    float voxel{ 1.f };
    //! 0 より大きければ、中心からこの割合までは抜く（殻）。
    float hole{ 0.f };
    float flicker_hz{ 24.f };
    bool rainbow{ false };
    FxColour light{ 1.f, 1.f, 1.f };
    float light_k{ 1.f };
};

//! 玉の本体。`offset` でずらした複製を描ける。`primary` が真のときだけ点光源と本体の印を足す。
void emit_orb(const FxStyleCtx &ctx, const OrbLook &o, const Vec3 &offset, float alpha_mul, bool primary, FxFrame &out)
{
    const FxSpec &spec = ctx.spec;
    const VoxelFxParams &params = ctx.params;
    const float s = flight_s(ctx);
    const Vec3 centre = fx_flight_position(spec, params, s) + offset;
    const std::uint32_t frame = static_cast<std::uint32_t>(std::floor(ctx.t * o.flicker_hz));
    const float voxel = params.orb_voxel * o.voxel;
    const float radius = params.orb_radius * (spec.explode ? 1.f : params.bolt_orb_scale) * o.radius;
    const float reach = radius / voxel;
    const int n = static_cast<int>(std::ceil(reach));
    for (int i = -n; i <= n; ++i) {
        for (int j = -n; j <= n; ++j) {
            for (int k = -n; k <= n; ++k) {
                const float d = std::sqrt(static_cast<float>((i * i) + (j * j) + (k * k)));
                if (d > reach) {
                    continue;
                }
                const float u = d / reach;
                if ((o.hole > 0.f) && (u < o.hole)) {
                    continue;
                }
                const std::uint32_t cell = static_cast<std::uint32_t>(((i + 8) * 289) + ((j + 8) * 17) + (k + 8));
                if (fx_rand(spec.seed, cell, frame, 1u) < (std::max(u - 0.45f, 0.f) * 0.9f)) {
                    continue;
                }
                FxColour c;
                float e = 0.f;
                if (u < 0.5f) {
                    c = fx_mix(o.c0, o.c1, u / 0.5f);
                    e = fx_lerp(o.e0, o.e1, u / 0.5f);
                } else {
                    c = fx_mix(o.c1, o.c2, (u - 0.5f) / 0.5f);
                    e = fx_lerp(o.e1, o.e2, (u - 0.5f) / 0.5f);
                }
                if (o.rainbow) {
                    c = hue_colour((ctx.t * 1.3f) + (u * 0.4f) + (fx_rand(spec.seed, cell, frame / 3u, 9u) * 0.35f));
                }
                const float grow = 0.82f + (0.30f * fx_rand(spec.seed, cell, frame, 5u));
                const Vec3 p = centre
                    + Vec3{ (static_cast<float>(i) * voxel) + ((fx_rand(spec.seed, cell, frame, 2u) - 0.5f) * 0.012f),
                          (static_cast<float>(j) * voxel) + ((fx_rand(spec.seed, cell, frame, 3u) - 0.5f) * 0.012f),
                          (static_cast<float>(k) * voxel) + ((fx_rand(spec.seed, cell, frame, 4u) - 0.5f) * 0.012f) };
                put_cube(out, p, voxel * grow, c, e, o.alpha * alpha_mul, FxPart::OrbCore);
            }
        }
    }
    if (primary) {
        PointLight light;
        light.x = centre.x;
        light.y = centre.y;
        light.z = centre.z + 0.35f;
        light.radius = params.orb_light_radius;
        light.r = o.light.r;
        light.g = o.light.g;
        light.b = o.light.b;
        light.intensity = params.orb_light_intensity * o.light_k * (0.85f + (0.30f * fx_rand(spec.seed, frame, 7u)));
        out.lights.push_back(light);
        out.projectile_visible = true;
        out.projectile_pos = centre;
        out.projectile_dir = flight_dir(ctx, s);
    }
}

//! 飛んだ跡に残る粒（尾）。
struct TrailLook {
    float rate{ 50.f };
    float life{ 0.4f };
    float spread{ 0.10f };
    float drift{ 0.2f };
    float rise{ 0.f };
    float gravity{ 0.f };
    float size0{ 0.07f };
    float size1{ 0.02f };
    FxColour c0{ 1.f, 1.f, 1.f };
    FxColour c1{ 0.5f, 0.5f, 0.5f };
    float e0{ 2.f };
    float e1{ 0.2f };
    float a0{ 1.f };
    float a1{ 0.f };
    FxPart part{ FxPart::Particle };
    //! 出す位置を、本体のどれだけ後ろ（マス）にするか。
    float back{ 0.f };
};

void emit_trail(const FxStyleCtx &ctx, const TrailLook &tl, FxFrame &out)
{
    const float fly = std::max(ctx.fly, 1e-4f);
    const int first = std::max(0, static_cast<int>(std::ceil((ctx.t - tl.life) * tl.rate)));
    const int last = static_cast<int>(std::floor(std::min(ctx.t, fly) * tl.rate));
    for (int i = first; i <= last; ++i) {
        const float born = static_cast<float>(i) / tl.rate;
        const float s = fx_clamp01(born / fly);
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        Vec3 origin = fx_flight_position(ctx.spec, ctx.params, s);
        if (tl.back != 0.f) {
            origin = origin - (flight_dir(ctx, s) * tl.back);
        }
        FxParticle p;
        p.pos = origin
            + Vec3{ (fx_rand(ctx.spec.seed, id, 11u) - 0.5f) * tl.spread, (fx_rand(ctx.spec.seed, id, 12u) - 0.5f) * tl.spread,
                  (fx_rand(ctx.spec.seed, id, 13u) - 0.5f) * tl.spread };
        p.vel = Vec3{ (fx_rand(ctx.spec.seed, id, 14u) - 0.5f) * tl.drift, (fx_rand(ctx.spec.seed, id, 15u) - 0.5f) * tl.drift, tl.rise };
        p.gravity = tl.gravity;
        p.born = born;
        p.life = tl.life * (0.8f + (0.4f * fx_rand(ctx.spec.seed, id, 16u)));
        p.colour0 = tl.c0;
        p.colour1 = tl.c1;
        p.size0 = tl.size0 * (0.8f + (0.4f * fx_rand(ctx.spec.seed, id, 17u)));
        p.size1 = tl.size1;
        p.emissive0 = tl.e0;
        p.emissive1 = tl.e1;
        p.alpha0 = tl.a0;
        p.alpha1 = tl.a1;
        p.part = tl.part;
        (void)fx_emit_particle(p, ctx.t, out);
    }
}

/* ============================================================ 着弾の文脈 */

//! 効果範囲のマス 1 つ。
struct Cell {
    int gx{ 0 };
    int gy{ 0 };
    int ring{ 0 };
    float cx{ 0.f };
    float cy{ 0.f };
    //! 効果が始まる時刻（着弾からの秒）。
    float start{ 0.f };
    int index{ 0 };
    //! 着弾の中心のマスか（ブレスでは偽）。
    bool centre{ false };
    //! 大きさの係数（中心 1・周り 0.85・ブレス 0.75）。
    float k{ 1.f };
    std::uint32_t id{ 0 };
};

//! 着弾（開始）からの `te` 秒の、効果範囲の全体。
struct Hit {
    const FxSpec &spec;
    const VoxelFxParams &params;
    float te{ 0.f };
    bool area{ false };
    bool breath{ false };
    bool small{ false };
    bool glow{ false };
    bool beam{ false };
    float ring_delay{ 0.05f };
    float density{ 1.f };
    int max_ring{ 0 };
    std::vector<Cell> cells;

    Hit(const FxSpec &s, const VoxelFxParams &p, float t)
        : spec(s)
        , params(p)
        , te(t)
    {
        const bool orb = (s.kind == FxKind::Orb);
        this->breath = (s.kind == FxKind::Breath);
        this->area = (orb && s.explode) || this->breath || (s.kind == FxKind::Burst);
        this->glow = (s.kind == FxKind::Glow);
        this->beam = (s.kind == FxKind::Beam);
        this->small = !this->area;
        this->ring_delay = this->breath ? p.breath_ring_delay : p.ring_delay;
        const std::vector<FxBurstCell> single{ FxBurstCell{ s.centre_gx, s.centre_gy, 0 } };
        const std::vector<FxBurstCell> &src = (this->area && !s.cells.empty()) ? s.cells : single;
        int index = 0;
        for (const FxBurstCell &b : src) {
            Cell c;
            c.gx = b.gx;
            c.gy = b.gy;
            c.ring = b.ring;
            c.cx = static_cast<float>(b.gx) + 0.5f;
            c.cy = static_cast<float>(b.gy) + 0.5f;
            c.start = static_cast<float>(b.ring) * this->ring_delay;
            c.index = index++;
            c.centre = !this->breath && (b.gx == s.centre_gx) && (b.gy == s.centre_gy);
            c.k = this->small ? 1.f : (this->breath ? 0.75f : (c.centre ? 1.f : 0.85f));
            c.id = static_cast<std::uint32_t>(((b.gx & 0x3ff) * 1031) + (b.gy & 0x3ff));
            this->max_ring = std::max(this->max_ring, b.ring);
            this->cells.push_back(c);
        }
        const float n = static_cast<float>(this->cells.size());
        this->density = (n <= 9.f) ? 1.f : std::min(1.f, std::max(0.12f, 9.f / n));
    }
};

//! マス専用の乱数。
float rnd(const Hit &h, const Cell &c, std::uint32_t a, std::uint32_t b = 0)
{
    return fx_rand(h.spec.seed + (c.id * 7919u), a, b, 5u);
}

std::uint32_t seed_of(const Hit &h, const Cell &c, std::uint32_t salt)
{
    return h.spec.seed + (c.id * 131u) + (salt * 17u);
}

//! マスあたりの量（範囲が広いほど減らす）。
int amount(const Hit &h, const Cell &c, float base, int minimum = 1)
{
    const float k = h.density * (0.65f + (0.35f * c.k));
    return std::max(minimum, static_cast<int>(std::lround(base * k)));
}

//! 目立たせるマスか（着弾の中心・1 マスの効果は必ず。ほかは確率 `p`）。
bool prominent(const Hit &h, const Cell &c, float p)
{
    return c.centre || h.small || (rnd(h, c, 900u) < (h.breath ? (p * 0.5f) : p));
}

int ring_segments(const Hit &h)
{
    return (h.density >= 0.6f) ? 24 : 14;
}

//! 床の溜まりのボクセルの大きさ（マスが多いときは粗くして数を抑える）。
float puddle_voxel(const Hit &h)
{
    return (h.density >= 0.6f) ? 0.07f : 0.16f;
}

//! このマスから見た、対象が押される向き（水平。正規化）。
void away_dir(const Hit &h, const Cell &c, float &ax, float &ay)
{
    float dx = 0.f;
    float dy = 1.f;
    if (h.breath) {
        dx = c.cx - (static_cast<float>(h.spec.src_gx) + 0.5f);
        dy = c.cy - (static_cast<float>(h.spec.src_gy) + 0.5f);
    } else if (c.centre || h.small) {
        dx = static_cast<float>(h.spec.centre_gx - h.spec.src_gx);
        dy = static_cast<float>(h.spec.centre_gy - h.spec.src_gy);
    } else {
        dx = c.cx - (static_cast<float>(h.spec.centre_gx) + 0.5f);
        dy = c.cy - (static_cast<float>(h.spec.centre_gy) + 0.5f);
    }
    const float len = std::sqrt((dx * dx) + (dy * dy));
    if (len < 1e-4f) {
        ax = 0.f;
        ay = 1.f;
        return;
    }
    ax = dx / len;
    ay = dy / len;
}

/*! 属性ごとの見た目の定義。 */
struct Look {
    const char *attr{ "" };
    FxColour base{ 1.f, 1.f, 1.f };
    float smoke{ 0.2f };
    float emissive{ 1.f };
    FxPaletteFn palette{ nullptr };
    //! 飛んでいる間の本体と尾（着弾のあとも呼ばれ、尾の残りを描く）。`nullptr` なら汎用。
    void (*flight)(const FxStyleCtx &ctx, FxFrame &out){ nullptr };
    //! 飛んだあとに残る尾の寿命（秒）。
    float tail{ 0.f };
    //! 着弾のマス 1 つぶんの効果。`lt` はそのマスの効果が始まってからの秒。
    void (*cell)(const Hit &h, const Cell &c, float lt, FxFrame &out){ nullptr };
    //! マスの対象の見た目の変化。
    void (*mod)(const Hit &h, const Cell &c, float lt, FxEntityMod &m){ nullptr };
    //! 1 マスの効果の長さ（秒）。
    float seconds{ 0.8f };
    //! 作った粒を、そのマスへ押し込まない（効果が範囲全体にまたがる属性。自分で範囲の中に収める）。
    bool free{ false };
    //! 汎用の爆発（属性の色）を下に敷くか。
    bool underlay{ false };
    //! 点光源（`underlay` が無いときの光）。
    FxColour light{ 1.f, 1.f, 1.f };
    float light_peak{ 0.f };
    float light_decay{ 0.5f };
    float light_delay{ 0.f };
    float light_flicker_hz{ 0.f };
    //! 画面の揺れ（強さ。マス）と長さ（秒）。
    float shake{ 0.f };
    float shake_seconds{ 0.4f };
};

//! 作った粒を、そのマスの中へ押し込む。
void clamp_to_cell(FxFrame &out, std::size_t from, float cx, float cy)
{
    for (std::size_t i = from; i < out.voxels.size(); ++i) {
        FxVoxel &v = out.voxels[i];
        const float half = std::max(0.f, 0.5f - (std::max(v.size.x, v.size.y) * 0.5f) - 0.002f);
        v.center.x = std::min(std::max(v.center.x, cx - half), cx + half);
        v.center.y = std::min(std::max(v.center.y, cy - half), cy + half);
    }
}

FxBurstMode burst_mode(const Hit &h)
{
    FxBurstMode m;
    if (h.breath) {
        m.ring_delay = h.params.breath_ring_delay;
        m.flash = false;
    } else if (h.area) {
        m.ring_delay = h.params.ring_delay;
        m.flash = true;
    } else {
        m.small = true;
        m.flash = false;
    }
    return m;
}

//! 1 度の着弾が足すボクセルの上限（暴走したときの安全網）。
constexpr std::size_t kHitCap = 3200;

float hit_seconds(const Look &l, const FxSpec &spec, const VoxelFxParams &params)
{
    const Hit h(spec, params, 0.f);
    float last = 0.f;
    for (const Cell &c : h.cells) {
        last = std::max(last, c.start);
    }
    float s = last + l.seconds;
    if (l.underlay && !h.glow) {
        s = std::max(s, fx_burst_seconds(spec, params, burst_mode(h)));
    }
    return s;
}

void emit_hit(const Look &l, const Hit &h, FxFrame &out)
{
    const std::size_t start = out.voxels.size();
    if (l.underlay && !h.glow) {
        const FxBurstMode mode = burst_mode(h);
        if (h.te < fx_burst_seconds(h.spec, h.params, mode)) {
            fx_emit_burst(h.spec, h.params, h.te, mode, out, !h.beam);
        }
    }
    for (const Cell &c : h.cells) {
        const float lt = h.te - c.start;
        if ((lt < 0.f) || (lt >= l.seconds)) {
            continue;
        }
        if (l.cell != nullptr) {
            const std::size_t n0 = out.voxels.size();
            l.cell(h, c, lt, out);
            if (!l.free) {
                clamp_to_cell(out, n0, c.cx, c.cy);
            }
        }
        if (l.mod != nullptr) {
            l.mod(h, c, lt, fx_mod_for(out, c.gx, c.gy));
        }
    }
    if (l.shake > 0.f) {
        fx_add_shake(out, fx_shake_envelope(h.te, 0.f, l.shake_seconds, l.shake));
    }
    if (l.light_peak > 0.f) {
        const float te = h.te - l.light_delay;
        if (te >= 0.f) {
            constexpr float kRise = 0.03f;
            const float strength = (te < kRise) ? (te / kRise) : std::max(0.f, 1.f - ((te - kRise) / l.light_decay));
            if (strength > 0.01f) {
                PointLight light;
                float sx = 0.f;
                float sy = 0.f;
                for (const Cell &c : h.cells) {
                    sx += c.cx;
                    sy += c.cy;
                }
                const float n = static_cast<float>(std::max<std::size_t>(h.cells.size(), 1u));
                light.x = h.breath ? (sx / n) : (static_cast<float>(h.spec.centre_gx) + 0.5f);
                light.y = h.breath ? (sy / n) : (static_cast<float>(h.spec.centre_gy) + 0.5f);
                light.z = 1.1f;
                light.radius = 2.4f + (0.6f * static_cast<float>(h.max_ring) * (h.breath ? 0.5f : 1.f));
                light.r = l.light.r;
                light.g = l.light.g;
                light.b = l.light.b;
                float flick = 1.f;
                if (l.light_flicker_hz > 0.f) {
                    flick = 0.7f + (0.3f * std::sin(te * l.light_flicker_hz));
                }
                light.intensity = l.light_peak * strength * strength * flick;
                out.lights.push_back(light);
            }
        }
    }
    if (out.voxels.size() > (start + kHitCap)) {
        out.voxels.resize(start + kHitCap);
    }
}

/* ============================================================ 形（文字の格子） */

const FxShape &arrow_down_shape()
{
    static const FxShape s = fx_shape({ "..###..", "..###..", "..###..", "#######", ".#####.", "..###..", "...#..." });
    return s;
}

//! 刃が上を向いた剣（5 × 12）。回して使う。
const FxShape &sword_shape()
{
    static const FxShape s = fx_shape({ "..o..", ".o#o.", ".o#o.", ".o#o.", ".o#o.", ".o#o.", ".o#o.", ".o#o.", "hhhhh", "..h..", "..h..", ".hhh." });
    return s;
}

const FxShape &ghost_shape()
{
    static const FxShape s = fx_shape({ "..###..", ".#####.", "##.#.##", "#######", "#######", "#######", "#######", "#.#.#.#" });
    return s;
}

const FxShape &rift_shape(int width)
{
    static const FxShape thin = fx_shape({ ".#.", ".#.", ".o.", ".o.", ".o.", ".o.", ".o.", ".#.", ".#." });
    static const FxShape mid = fx_shape({ "..#..", ".#o#.", ".#o#.", "#ooo#", "#ooo#", "#ooo#", ".#o#.", ".#o#.", "..#.." });
    static const FxShape wide = fx_shape({ "...#...", "..#o#..", ".#ooo#.", "#ooooo#", "#ooooo#", "#ooooo#", ".#ooo#.", "..#o#..", "...#..." });
    return (width <= 0) ? thin : ((width == 1) ? mid : wide);
}

FxShapePlace billboard(const FxSpec &spec, const Vec3 &centre, float cell, float alpha, float emissive)
{
    FxShapePlace p;
    p.centre = centre;
    p.right = spec.cam_right;
    p.up = spec.cam_up;
    p.cell = cell;
    p.alpha = alpha;
    p.emissive = emissive;
    return p;
}

/* ============================================================ 雹・石礫（ice・shards） */

struct HailLook {
    FxColour bright;
    FxColour dark;
    float emissive;
    float size;
    FxColour mist0;
    FxColour mist1;
    float mist_alpha;
    float mist_emissive;
    FxColour light;
    bool stone;
};

const HailLook kIce{ { 0.88f, 0.98f, 1.00f }, { 0.45f, 0.75f, 0.95f }, 1.0f, 0.095f, { 0.82f, 0.94f, 1.00f }, { 0.62f, 0.82f, 0.96f }, 0.22f, 1.3f,
    { 0.55f, 0.85f, 1.00f }, false };
const HailLook kStone{ { 0.85f, 0.83f, 0.80f }, { 0.52f, 0.50f, 0.48f }, 0.45f, 0.11f, { 0.52f, 0.49f, 0.44f }, { 0.38f, 0.36f, 0.33f }, 0.30f, 0.0f,
    { 1.00f, 0.85f, 0.60f }, true };

void hail_flight(const FxStyleCtx &ctx, FxFrame &out, const HailLook &hl)
{
    const FxSpec &spec = ctx.spec;
    if (ctx.t < ctx.fly) {
        const float s = flight_s(ctx);
        const Vec3 centre = fx_flight_position(spec, ctx.params, s);
        const Vec3 dir = flight_dir(ctx, s);
        Vec3 b1;
        Vec3 b2;
        perp(dir, b1, b2);
        const int pellets = 8;
        const std::uint32_t frame = static_cast<std::uint32_t>(std::floor(ctx.t * 20.f));
        for (int i = 0; i < pellets; ++i) {
            const std::uint32_t id = static_cast<std::uint32_t>(i);
            const float ang = (static_cast<float>(i) * 2.f * kPi / static_cast<float>(pellets)) + (ctx.t * 8.f);
            const float rad = 0.05f + (0.11f * fx_rand(spec.seed, id, 1u));
            const float back = -0.22f * fx_rand(spec.seed, id, 2u);
            const Vec3 p = centre + (b1 * (std::cos(ang) * rad)) + (b2 * (std::sin(ang) * rad)) + (dir * back)
                + Vec3{ 0.f, 0.f, (fx_rand(spec.seed, id, frame, 3u) - 0.5f) * 0.012f };
            const float size = hl.size * (0.85f + (0.5f * fx_rand(spec.seed, id, 4u)));
            put_cube(out, p, size, fx_mix(hl.bright, hl.dark, fx_rand(spec.seed, id, 5u)), hl.emissive, 1.f, FxPart::Debris, (ctx.t * 12.f) + static_cast<float>(i),
                (ctx.t * 9.f) + (static_cast<float>(i) * 1.3f));
        }
        PointLight light;
        light.x = centre.x;
        light.y = centre.y;
        light.z = centre.z + 0.35f;
        light.radius = 3.2f;
        light.r = hl.light.r;
        light.g = hl.light.g;
        light.b = hl.light.b;
        light.intensity = hl.stone ? 0.7f : 1.3f;
        out.lights.push_back(light);
        out.projectile_visible = true;
        out.projectile_pos = centre;
        out.projectile_dir = dir;
    }
    // こぼれ落ちる粒（氷のかけら／石の粉）。
    TrailLook tl;
    tl.rate = 55.f;
    tl.life = 0.38f;
    tl.spread = 0.14f;
    tl.drift = 0.25f;
    tl.gravity = -5.f;
    tl.size0 = hl.size * 0.6f;
    tl.size1 = hl.size * 0.25f;
    tl.c0 = hl.bright;
    tl.c1 = hl.dark;
    tl.e0 = hl.emissive;
    tl.e1 = 0.f;
    tl.a0 = 1.f;
    tl.a1 = 0.f;
    tl.part = FxPart::Debris;
    emit_trail(ctx, tl, out);
}

void hail_cell(const Hit &h, const Cell &c, float lt, FxFrame &out, const HailLook &hl)
{
    const int stones = amount(h, c, (c.centre || h.small) ? 4.f : 3.f);
    const float fall = 0.15f;
    for (int i = 0; i < stones; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float born = (0.065f * static_cast<float>(i)) + (0.04f * rnd(h, c, id, 1u));
        const float age = lt - born;
        if (age < 0.f) {
            continue;
        }
        const float x = c.cx + ((rnd(h, c, id, 2u) - 0.5f) * 0.5f);
        const float y = c.cy + ((rnd(h, c, id, 3u) - 0.5f) * 0.5f);
        if (age < fall) {
            const float u = age / fall;
            const float z = 0.07f + (1.35f * (1.f - (u * u)));
            const float size = hl.size * (0.9f + (0.4f * rnd(h, c, id, 4u)));
            put_cube(out, Vec3{ x, y, z }, size, fx_mix(hl.bright, hl.dark, rnd(h, c, id, 5u)), hl.emissive, 1.f, FxPart::Debris, (age * 13.f) + static_cast<float>(i),
                age * 9.f);
            put_cube(out, Vec3{ x, y, z + 0.13f }, size * 0.5f, hl.bright, hl.emissive, 0.5f, FxPart::Debris);
        } else {
            FxSpray sp;
            sp.origin = Vec3{ x, y, 0.06f };
            sp.dir = Vec3{ 0.f, 0.f, 1.f };
            sp.cone = 1.25f;
            sp.speed_min = 0.5f;
            sp.speed_max = 1.2f;
            sp.gravity = -9.f;
            sp.born = born + fall;
            sp.life_min = 0.22f;
            sp.life_max = 0.42f;
            sp.count = std::max(3, static_cast<int>(std::lround(5.f * h.density)));
            sp.colour0 = fx_mix(hl.bright, hl.dark, 0.3f);
            sp.colour1 = fx_scale(hl.dark, 0.7f);
            sp.size0 = hl.size * 0.7f;
            sp.size1 = hl.size * 0.3f;
            sp.emissive0 = hl.emissive;
            sp.emissive1 = hl.emissive * 0.6f;
            sp.alpha0 = 1.f;
            sp.alpha1 = hl.stone ? 0.3f : 0.f;
            sp.floor = FxFloor::Stop;
            sp.floor_z = 0.f;
            sp.part = FxPart::Debris;
            sp.seed = seed_of(h, c, id + 3u);
            (void)fx_emit_spray(sp, lt, out);
        }
    }
    // 霜の靄／土煙。
    FxSmoke sm;
    sm.origin = Vec3{ c.cx, c.cy, 0.05f };
    sm.radius = 0.16f;
    sm.born = 0.1f;
    sm.duration = 0.2f;
    sm.life = 0.5f;
    sm.count = amount(h, c, 2.f);
    sm.rise = 0.2f;
    sm.size = 0.11f * (0.8f + (0.2f * c.k));
    sm.grow = 1.5f;
    sm.colour = hl.mist0;
    sm.colour_end = hl.mist1;
    sm.alpha = hl.mist_alpha;
    sm.emissive = hl.mist_emissive;
    sm.seed = seed_of(h, c, 99u);
    (void)fx_emit_smoke(sm, lt, out);
}

void ice_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    hail_flight(ctx, out, kIce);
}

void shards_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    hail_flight(ctx, out, kStone);
}

void ice_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    hail_cell(h, c, lt, out, kIce);
}

void shards_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    hail_cell(h, c, lt, out, kStone);
}

/* ============================================================ 轟音（sound） */

void sound_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        OrbLook o;
        o.c0 = col(1.00f, 0.98f, 0.82f);
        o.c1 = col(1.00f, 0.88f, 0.50f);
        o.c2 = col(0.85f, 0.65f, 0.25f);
        o.radius = 0.5f;
        o.light = col(1.f, 0.9f, 0.55f);
        o.light_k = 0.8f;
        emit_orb(ctx, o, Vec3{}, 1.f, true, out);
    }
    // 玉から広がる衝撃波の輪（進む向きに直角）。
    const float fly = std::max(ctx.fly, 1e-4f);
    const float life = 0.34f;
    const float step = 0.055f;
    const int first = std::max(0, static_cast<int>(std::ceil((ctx.t - life) / step)));
    const int last = static_cast<int>(std::floor(std::min(ctx.t, fly) / step));
    for (int i = first; i <= last; ++i) {
        const float born = static_cast<float>(i) * step;
        const float s = fx_clamp01(born / fly);
        FxRing r;
        r.centre = fx_flight_position(ctx.spec, ctx.params, s);
        r.normal = flight_dir(ctx, s);
        r.r0 = 0.06f;
        r.r1 = 0.42f;
        r.born = born;
        r.life = life;
        r.thickness = 0.03f;
        r.segments = 18;
        r.colour = col(1.00f, 0.92f, 0.62f);
        r.emissive = 1.7f;
        r.alpha = 0.8f;
        r.seed = ctx.spec.seed;
        (void)fx_emit_ring(r, ctx.t, out);
    }
}

//! ボクセルが、効果範囲のどれかのマスの中にあるものだけを残す（`from` 以降）。
void keep_inside_cells(const Hit &h, FxFrame &out, std::size_t from)
{
    std::size_t w = from;
    for (std::size_t i = from; i < out.voxels.size(); ++i) {
        bool inside = false;
        for (const Cell &c : h.cells) {
            if ((std::fabs(out.voxels[i].center.x - c.cx) <= 0.5f) && (std::fabs(out.voxels[i].center.y - c.cy) <= 0.5f)) {
                inside = true;
                break;
            }
        }
        if (inside) {
            out.voxels[w++] = out.voxels[i];
        }
    }
    out.voxels.resize(w);
}

//! 広がりの中心（ブレスは撃った者。ほかは着弾点）のマスの中心。
Vec3 wave_origin(const Hit &h, float z)
{
    return h.breath ? fx_source_centre(h.spec, z) : fx_target_centre(h.spec, z);
}

//! 中心から外へ広がる輪（床の上）。効果範囲の外へ出た部分は捨てる。範囲の広いボールとブレスだけで使う。
void emit_ground_waves(const Hit &h, FxFrame &out, const FxColour &colour, int count, float gap, float speed, float thickness, float emissive, float alpha)
{
    const std::size_t from = out.voxels.size();
    const float rmax = static_cast<float>(h.max_ring) + 0.6f;
    for (int i = 0; i < count; ++i) {
        const float r = (h.te - (gap * static_cast<float>(i))) * speed;
        if ((r < 0.06f) || (r > rmax)) {
            continue;
        }
        FxRing ring;
        ring.centre = wave_origin(h, 0.07f);
        ring.r0 = ring.r1 = r;
        ring.life = 1.f;
        ring.thickness = thickness;
        ring.segments = std::min(110, std::max(18, static_cast<int>(r * 34.f)));
        ring.colour = colour;
        ring.emissive = emissive;
        ring.alpha = alpha * (1.f - (0.7f * (r / rmax)));
        ring.fade_out = false;
        ring.seed = h.spec.seed + static_cast<std::uint32_t>(i);
        (void)fx_emit_ring(ring, 0.f, out);
    }
    keep_inside_cells(h, out, from);
}

void sound_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    const FxColour colour = col(1.00f, 0.93f, 0.62f);
    if (h.small) {
        // 1 マス：床の輪が 3 つと、画面に向いた輪が 2 つ。
        const std::size_t from = out.voxels.size();
        for (int i = 0; i < 3; ++i) {
            FxRing r;
            r.centre = Vec3{ c.cx, c.cy, 0.07f };
            r.r0 = 0.05f;
            r.r1 = 0.44f;
            r.born = 0.1f * static_cast<float>(i);
            r.life = 0.4f;
            r.thickness = 0.035f;
            r.segments = 24;
            r.colour = colour;
            r.emissive = 1.8f;
            r.alpha = 0.85f;
            r.seed = seed_of(h, c, static_cast<std::uint32_t>(i));
            (void)fx_emit_ring(r, lt, out);
        }
        for (int i = 0; i < 2; ++i) {
            FxRing r;
            r.centre = Vec3{ c.cx, c.cy, 0.55f };
            r.normal = fx_toward_camera(h.spec);
            r.r0 = 0.05f;
            r.r1 = 0.38f;
            r.born = 0.03f + (0.12f * static_cast<float>(i));
            r.life = 0.38f;
            r.thickness = 0.032f;
            r.segments = 24;
            r.colour = colour;
            r.emissive = 1.6f;
            r.alpha = 0.75f;
            r.seed = seed_of(h, c, 10u + static_cast<std::uint32_t>(i));
            (void)fx_emit_ring(r, lt, out);
        }
        clamp_to_cell(out, from, c.cx, c.cy);
        return;
    }
    // 範囲のあるとき：中心（ブレスは撃った者）から輪が何重にも広がる。範囲の外へは出さない。
    if (c.index == 0) {
        emit_ground_waves(h, out, colour, 4, 0.11f, 7.5f, 0.04f, 1.8f, 0.9f);
        if (!h.breath) {
            for (int i = 0; i < 2; ++i) {
                FxRing r;
                r.centre = fx_target_centre(h.spec, 0.55f);
                r.normal = fx_toward_camera(h.spec);
                r.r0 = 0.05f;
                r.r1 = 0.40f;
                r.born = 0.03f + (0.12f * static_cast<float>(i));
                r.life = 0.38f;
                r.thickness = 0.032f;
                r.segments = 24;
                r.colour = colour;
                r.emissive = 1.6f;
                r.alpha = 0.75f;
                r.seed = h.spec.seed + 10u + static_cast<std::uint32_t>(i);
                (void)fx_emit_ring(r, h.te, out);
            }
        }
    }
}

void sound_mod(const Hit &, const Cell &, float lt, FxEntityMod &m)
{
    const float env = fx_envelope(lt, 0.03f, 0.4f, 0.2f);
    m.tremble = std::max(m.tremble, 0.05f * env);
    m.tremble_phase = lt * 70.f;
}

/* ============================================================ フォース（force） */

void force_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        // ほぼ透明な殻の球と、画面に向いた輪（空気のゆがみ）。
        OrbLook o;
        o.c0 = col(0.90f, 0.90f, 1.00f);
        o.c1 = col(0.82f, 0.80f, 1.00f);
        o.c2 = col(0.70f, 0.70f, 0.95f);
        o.e0 = 0.4f;
        o.e1 = 0.4f;
        o.e2 = 0.6f;
        o.alpha = 0.17f;
        o.radius = 0.85f;
        o.voxel = 0.7f;
        o.hole = 0.55f;
        o.flicker_hz = 14.f;
        o.light = col(0.82f, 0.82f, 1.f);
        o.light_k = 0.55f;
        emit_orb(ctx, o, Vec3{}, 1.f, true, out);
        const Vec3 centre = out.projectile_pos;
        for (int i = 0; i < 2; ++i) {
            FxRing r;
            r.centre = centre;
            r.normal = fx_toward_camera(ctx.spec);
            const float pulse = 0.5f + (0.5f * std::sin((ctx.t * 14.f) + (static_cast<float>(i) * 2.f)));
            r.r0 = r.r1 = 0.17f + (0.13f * static_cast<float>(i)) + (0.03f * pulse);
            r.born = 0.f;
            r.life = 1.f;
            r.thickness = 0.03f;
            r.segments = 22;
            r.colour = col(0.92f, 0.92f, 1.f);
            r.emissive = 0.9f;
            r.alpha = 0.45f;
            r.fade_out = false;
            r.seed = ctx.spec.seed;
            (void)fx_emit_ring(r, ctx.t, out);
        }
    }
    TrailLook tl;
    tl.rate = 45.f;
    tl.life = 0.3f;
    tl.spread = 0.2f;
    tl.drift = 0.15f;
    tl.size0 = 0.045f;
    tl.size1 = 0.012f;
    tl.c0 = col(0.95f, 0.95f, 1.f);
    tl.c1 = col(0.7f, 0.7f, 0.95f);
    tl.e0 = 0.9f;
    tl.e1 = 0.2f;
    tl.a0 = 0.45f;
    tl.a1 = 0.f;
    emit_trail(ctx, tl, out);
}

void force_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    const FxColour colour = col(0.90f, 0.90f, 1.00f);
    if (!h.small) {
        // 範囲のあるとき：中心から淡い輪が広がる（範囲の外へは出さない）。ゆがみの球は着弾の中心にだけ。
        if (c.index == 0) {
            emit_ground_waves(h, out, colour, 3, 0.10f, 6.5f, 0.045f, 0.9f, 0.6f);
        }
        if (!c.centre) {
            return;
        }
    }
    // 淡い輪：画面に向いたものと、床に広がるもの。
    FxRing a;
    a.centre = Vec3{ c.cx, c.cy, 0.55f };
    a.normal = fx_toward_camera(h.spec);
    a.r0 = 0.06f;
    a.r1 = 0.40f * (0.75f + (0.25f * c.k));
    a.life = 0.30f;
    a.thickness = 0.04f;
    a.segments = ring_segments(h);
    a.colour = colour;
    a.emissive = 0.9f;
    a.alpha = 0.6f;
    a.seed = seed_of(h, c, 1u);
    (void)fx_emit_ring(a, lt, out);
    FxRing b = a;
    b.centre = Vec3{ c.cx, c.cy, 0.06f };
    b.normal = Vec3{ 0.f, 0.f, 1.f };
    b.r1 = 0.45f;
    b.born = 0.03f;
    b.life = 0.32f;
    (void)fx_emit_ring(b, lt, out);
    // ゆがみの球：ほぼ透明な粒の殻が広がる。
    const float u = fx_clamp01(lt / 0.28f);
    const float shell = fx_lerp(0.08f, 0.38f, ease_out(u)) * (0.75f + (0.25f * c.k));
    const int points = std::max(10, static_cast<int>(std::lround(34.f * h.density)));
    for (int i = 0; i < points; ++i) {
        const float zf = 1.f - ((2.f * (static_cast<float>(i) + 0.5f)) / static_cast<float>(points));
        const float rr = std::sqrt(std::max(0.f, 1.f - (zf * zf)));
        const float ang = static_cast<float>(i) * 2.399963f;
        put_cube(out, Vec3{ c.cx + (std::cos(ang) * rr * shell), c.cy + (std::sin(ang) * rr * shell), 0.5f + (zf * shell) }, 0.09f, colour, 0.6f,
            0.2f * (1.f - u), FxPart::Particle);
    }
}

void force_mod(const Hit &h, const Cell &c, float lt, FxEntityMod &m)
{
    // 一瞬で押され、そのあとゆっくり戻る。
    const float pulse = (lt < 0.07f) ? (lt / 0.07f) : std::max(0.f, 1.f - ((lt - 0.07f) / 0.3f));
    float ax = 0.f;
    float ay = 1.f;
    away_dir(h, c, ax, ay);
    if (pulse * 0.9f > m.recoil) {
        m.recoil = pulse * 0.9f;
        m.away_x = ax;
        m.away_y = ay;
    }
}

/* ============================================================ 隕石（meteor） */

void meteor_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        OrbLook o;
        o.c0 = col(1.00f, 0.78f, 0.40f);
        o.c1 = col(0.90f, 0.35f, 0.08f);
        o.c2 = col(0.25f, 0.10f, 0.06f);
        o.e0 = 3.f;
        o.e1 = 1.8f;
        o.e2 = 0.3f;
        o.radius = 0.75f;
        o.light = col(1.f, 0.5f, 0.2f);
        emit_orb(ctx, o, Vec3{}, 1.f, true, out);
    }
    TrailLook fire;
    fire.rate = 60.f;
    fire.life = 0.4f;
    fire.spread = 0.14f;
    fire.rise = 0.25f;
    fire.size0 = 0.08f;
    fire.size1 = 0.02f;
    fire.c0 = col(1.f, 0.6f, 0.2f);
    fire.c1 = col(0.6f, 0.1f, 0.03f);
    fire.e0 = 2.2f;
    fire.e1 = 0.3f;
    fire.part = FxPart::Ember;
    emit_trail(ctx, fire, out);
    TrailLook smoke;
    smoke.rate = 24.f;
    smoke.life = 0.6f;
    smoke.spread = 0.12f;
    smoke.rise = 0.3f;
    smoke.size0 = 0.1f;
    smoke.size1 = 0.2f;
    smoke.c0 = col(0.32f, 0.28f, 0.26f);
    smoke.c1 = col(0.16f, 0.15f, 0.15f);
    smoke.e0 = 0.f;
    smoke.e1 = 0.f;
    smoke.a0 = 0.5f;
    smoke.a1 = 0.f;
    smoke.part = FxPart::Mist;
    emit_trail(ctx, smoke, out);
}

void meteor_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    const int rocks = amount(h, c, (c.centre || h.small) ? 3.f : 2.f);
    const float fall = 0.2f;
    const FxColour rock_dark = col(0.30f, 0.23f, 0.19f);
    const FxColour rock_mid = col(0.50f, 0.40f, 0.33f);
    const FxColour hot = col(1.00f, 0.45f, 0.08f);
    for (int i = 0; i < rocks; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float born = (0.07f * static_cast<float>(i)) + (0.05f * rnd(h, c, id, 1u));
        const float age = lt - born;
        if (age < 0.f) {
            continue;
        }
        const float x = c.cx + ((rnd(h, c, id, 2u) - 0.5f) * 0.4f);
        const float y = c.cy + ((rnd(h, c, id, 3u) - 0.5f) * 0.4f);
        const float size = (0.22f + (0.08f * rnd(h, c, id, 4u))) * (0.75f + (0.25f * c.k));
        if (age < fall) {
            const float u = age / fall;
            const float z = 0.10f + (3.0f * (1.f - (u * u)));
            const float spin = (age * 11.f) + static_cast<float>(i);
            // 岩のかたまり（立方体 4 つ。1 つは熱い面）と、上へ伸びる火の尾。
            put_cube(out, Vec3{ x, y, z }, size, rock_mid, 0.4f, 1.f, FxPart::Debris, spin, spin * 0.7f);
            put_cube(out, Vec3{ x + (0.5f * size), y, z - (0.3f * size) }, size * 0.62f, rock_dark, 0.3f, 1.f, FxPart::Debris, spin * 1.3f, 0.f);
            put_cube(out, Vec3{ x - (0.45f * size), y + (0.3f * size), z + (0.35f * size) }, size * 0.55f, rock_dark, 0.f, 1.f, FxPart::Debris, spin, spin);
            put_cube(out, Vec3{ x, y - (0.4f * size), z - (0.5f * size) }, size * 0.5f, hot, 1.6f, 1.f, FxPart::Debris, spin * 0.5f, spin);
            const int tail = (h.density >= 0.6f) ? 5 : 3;
            for (int j = 1; j <= tail; ++j) {
                const float f = static_cast<float>(j) / static_cast<float>(tail);
                put_cube(out, Vec3{ x, y, z + (size * 0.9f * static_cast<float>(j)) }, size * 0.45f * (1.f - (0.6f * f)), fx_mix(hot, col(0.5f, 0.08f, 0.03f), f), 2.2f - f,
                    0.85f - (0.6f * f), FxPart::Ember);
            }
        } else {
            const float la = age - fall;
            if (la < 0.14f) {
                const float u = la / 0.14f;
                put_cube(out, Vec3{ x, y, 0.2f }, 0.20f * (1.f - u) * (0.7f + (0.3f * c.k)), col(1.f, 0.55f, 0.2f), 0.9f * (1.f - u), 1.f, FxPart::BurstFlash);
            }
            FxSpray sparks;
            sparks.origin = Vec3{ x, y, 0.1f };
            sparks.cone = 1.45f;
            sparks.speed_min = 0.7f;
            sparks.speed_max = 1.5f;
            sparks.gravity = -8.f;
            sparks.life_min = 0.25f;
            sparks.life_max = 0.5f;
            sparks.count = std::max(3, static_cast<int>(std::lround(7.f * h.density)));
            sparks.colour0 = col(1.f, 0.7f, 0.25f);
            sparks.colour1 = col(0.6f, 0.1f, 0.03f);
            sparks.size0 = 0.06f;
            sparks.size1 = 0.015f;
            sparks.emissive0 = 2.4f;
            sparks.emissive1 = 0.f;
            sparks.floor = FxFloor::Stop;
            sparks.part = FxPart::Spark;
            sparks.seed = seed_of(h, c, id + 1u);
            (void)fx_emit_spray(sparks, la, out);
            FxSpray rubble = sparks;
            rubble.speed_min = 0.3f;
            rubble.speed_max = 0.9f;
            rubble.gravity = -9.f;
            rubble.life_min = 0.5f;
            rubble.life_max = 0.9f;
            rubble.count = std::max(2, static_cast<int>(std::lround(4.f * h.density)));
            rubble.colour0 = rock_mid;
            rubble.colour1 = rock_dark;
            rubble.size0 = 0.09f;
            rubble.size1 = 0.06f;
            rubble.emissive0 = 0.f;
            rubble.alpha1 = 0.3f;
            rubble.part = FxPart::Debris;
            rubble.seed = seed_of(h, c, id + 5u);
            (void)fx_emit_spray(rubble, la, out);
            FxSmoke dust;
            dust.origin = Vec3{ x, y, 0.1f };
            dust.radius = 0.1f;
            dust.duration = 0.1f;
            dust.life = 0.7f;
            dust.count = (h.density >= 0.6f) ? 2 : 1;
            dust.rise = 0.25f;
            dust.size = 0.17f;
            dust.grow = 1.8f;
            dust.colour = col(0.50f, 0.44f, 0.38f);
            dust.colour_end = col(0.35f, 0.32f, 0.30f);
            dust.alpha = 0.45f;
            dust.seed = seed_of(h, c, id + 9u);
            (void)fx_emit_smoke(dust, la, out);
            // 床に残った岩。
            const float rest = 1.f - fx_ramp(la, 0.6f, 0.95f);
            put_cube(out, Vec3{ x, y, size * 0.22f }, size * 0.6f, rock_dark, 0.f, rest, FxPart::Debris, static_cast<float>(i));
        }
    }
}

/* ============================================================ 重力（gravity） */

void gravity_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        OrbLook o;
        o.c0 = col(0.05f, 0.02f, 0.09f);
        o.c1 = col(0.30f, 0.17f, 0.45f);
        o.c2 = col(0.62f, 0.45f, 0.82f);
        o.e0 = 0.1f;
        o.e1 = 0.7f;
        o.e2 = 1.4f;
        o.radius = 0.8f;
        o.light = col(0.6f, 0.4f, 0.8f);
        o.light_k = 0.7f;
        emit_orb(ctx, o, Vec3{}, 1.f, true, out);
    }
    TrailLook tl;
    tl.rate = 40.f;
    tl.life = 0.45f;
    tl.spread = 0.16f;
    tl.drift = 0.1f;
    tl.gravity = -4.f;
    tl.size0 = 0.07f;
    tl.size1 = 0.03f;
    tl.c0 = col(0.45f, 0.30f, 0.55f);
    tl.c1 = col(0.12f, 0.08f, 0.18f);
    tl.e0 = 0.7f;
    tl.e1 = 0.f;
    tl.part = FxPart::Debris;
    emit_trail(ctx, tl, out);
}

void gravity_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    const float K = c.k;
    // 床に押しつけられた暗い円盤。
    FxPuddle p;
    p.cx = c.cx;
    p.cy = c.cy;
    p.z = 0.012f;
    p.radius = 0.43f * K;
    p.born = 0.02f;
    p.spread = 0.1f;
    p.hold = 0.2f;
    p.fade = 0.3f;
    p.colour = col(0.12f, 0.06f, 0.18f);
    p.emissive = 0.15f;
    p.alpha = 0.55f;
    p.voxel = puddle_voxel(h);
    p.thickness = 0.025f;
    p.ragged = 0.2f;
    p.seed = seed_of(h, c, 1u);
    (void)fx_emit_puddle(p, lt, out);
    // 内へ縮む輪。
    FxRing r;
    r.centre = Vec3{ c.cx, c.cy, 0.05f };
    r.r0 = 0.46f * K;
    r.r1 = 0.10f;
    r.life = 0.3f;
    r.thickness = 0.04f;
    r.segments = ring_segments(h);
    r.colour = col(0.62f, 0.42f, 0.85f);
    r.emissive = 1.3f;
    r.alpha = 0.85f;
    r.seed = seed_of(h, c, 2u);
    (void)fx_emit_ring(r, lt, out);
    // 上から押しつける筋。
    const int lines = amount(h, c, (c.centre || h.small) ? 6.f : 3.f, 2);
    for (int i = 0; i < lines; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float age = lt - (0.03f * static_cast<float>(i));
        if ((age < 0.f) || (age > 0.45f)) {
            continue;
        }
        const float x = c.cx + ((rnd(h, c, id, 2u) - 0.5f) * 0.6f * K);
        const float y = c.cy + ((rnd(h, c, id, 3u) - 0.5f) * 0.6f * K);
        const float head = fx_lerp(1.6f, 0.06f, ease_in(age / 0.25f));
        const float shrink = 1.f - ease_out((age - 0.25f) / 0.2f);
        const float alpha = 0.7f * ((age > 0.3f) ? (1.f - ((age - 0.3f) / 0.15f)) : 1.f);
        put_bar(out, Vec3{ x, y, head }, Vec3{ x, y, head + (0.30f * shrink) }, 0.035f, col(0.55f, 0.38f, 0.80f), 1.0f, alpha, FxPart::Spark);
    }
}

void gravity_mod(const Hit &, const Cell &, float lt, FxEntityMod &m)
{
    m.squash = std::max(m.squash, 0.85f * fx_envelope(lt, 0.1f, 0.35f, 0.3f));
}

/* ============================================================ 遅鈍（inertial） */

void inertial_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        OrbLook o;
        o.c0 = col(0.92f, 0.95f, 1.00f);
        o.c1 = col(0.65f, 0.75f, 0.95f);
        o.c2 = col(0.30f, 0.36f, 0.55f);
        o.e0 = 2.2f;
        o.e1 = 1.3f;
        o.e2 = 0.5f;
        o.radius = 0.8f;
        o.flicker_hz = 8.f;
        o.light = col(0.65f, 0.75f, 1.f);
        o.light_k = 0.8f;
        emit_orb(ctx, o, Vec3{}, 1.f, true, out);
    }
    // ゆっくり沈むしずく。
    TrailLook tl;
    tl.rate = 20.f;
    tl.life = 0.7f;
    tl.spread = 0.14f;
    tl.drift = 0.08f;
    tl.rise = -0.12f;
    tl.size0 = 0.065f;
    tl.size1 = 0.025f;
    tl.c0 = col(0.72f, 0.82f, 1.f);
    tl.c1 = col(0.40f, 0.45f, 0.60f);
    tl.e0 = 1.2f;
    tl.e1 = 0.1f;
    tl.a0 = 0.9f;
    tl.a1 = 0.f;
    emit_trail(ctx, tl, out);
}

void inertial_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    const float cell_size = 0.062f * (0.8f + (0.2f * c.k));
    const int arrows = ((c.centre || h.small) && (h.density >= 0.5f)) ? 2 : 1;
    const float dur = 0.6f;
    FxShapeColours colours;
    colours.base = col(0.72f, 0.84f, 1.00f);
    for (int a = 0; a < arrows; ++a) {
        const float age = lt - (0.28f * static_cast<float>(a));
        if ((age < 0.f) || (age >= dur)) {
            continue;
        }
        const float z = fx_lerp(1.55f, 0.72f, ease_out(age / dur));
        const float alpha = 0.95f * fx_envelope(age, 0.1f, dur - 0.1f - 0.25f, 0.25f);
        (void)fx_emit_shape(arrow_down_shape(), colours, billboard(h.spec, Vec3{ c.cx, c.cy, z }, cell_size, alpha, 0.85f), out);
    }
    // 矢印が着いたところに、淡い輪。
    FxRing r;
    r.centre = Vec3{ c.cx, c.cy, 0.06f };
    r.r0 = 0.05f;
    r.r1 = 0.32f * c.k;
    r.born = 0.45f;
    r.life = 0.3f;
    r.thickness = 0.03f;
    r.segments = ring_segments(h);
    r.colour = col(0.65f, 0.76f, 1.f);
    r.emissive = 1.1f;
    r.alpha = 0.6f;
    r.seed = seed_of(h, c, 1u);
    (void)fx_emit_ring(r, lt, out);
}

/* ============================================================ マジック・ミサイル（missile） */

void missile_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    const float fly = std::max(ctx.fly, 1e-4f);
    if (ctx.t < ctx.fly) {
        const float s = flight_s(ctx);
        const Vec3 centre = fx_flight_position(ctx.spec, ctx.params, s);
        const Vec3 dir = flight_dir(ctx, s);
        Vec3 b1;
        Vec3 b2;
        perp(dir, b1, b2);
        // 光る矢：矢柄・矢じり・羽根。
        put_bar(out, centre - (dir * 0.20f), centre + (dir * 0.14f), 0.04f, col(0.75f, 0.85f, 1.f), 2.4f, 1.f, FxPart::OrbCore);
        put_bar(out, centre + (dir * 0.12f), centre + (dir * 0.25f), 0.075f, col(1.f, 1.f, 1.f), 3.2f, 1.f, FxPart::OrbCore);
        const Vec3 rear = centre - (dir * 0.19f);
        put_bar(out, rear - (b1 * 0.065f) - (dir * 0.04f), rear + (b1 * 0.065f), 0.025f, col(0.7f, 0.8f, 1.f), 2.0f, 1.f, FxPart::OrbCore);
        put_bar(out, rear - (b2 * 0.065f) - (dir * 0.04f), rear + (b2 * 0.065f), 0.025f, col(0.7f, 0.8f, 1.f), 2.0f, 1.f, FxPart::OrbCore);
        PointLight light;
        light.x = centre.x;
        light.y = centre.y;
        light.z = centre.z + 0.3f;
        light.radius = 3.4f;
        light.r = 0.7f;
        light.g = 0.8f;
        light.b = 1.f;
        light.intensity = 1.6f;
        out.lights.push_back(light);
        out.projectile_visible = true;
        out.projectile_pos = centre;
        out.projectile_dir = dir;
    }
    // 光の尾：過去の位置に、小さくなり薄れる光の粒を並べる。
    constexpr int kTail = 28;
    constexpr float kStep = 0.008f;
    for (int j = 1; j <= kTail; ++j) {
        const float tj = ctx.t - (static_cast<float>(j) * kStep);
        if ((tj < 0.f) || (tj > ctx.fly)) {
            continue;
        }
        const float f = static_cast<float>(j) / static_cast<float>(kTail);
        const Vec3 p = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(tj / fly));
        put_cube(out, p, fx_lerp(0.085f, 0.012f, f), fx_mix(col(0.95f, 0.98f, 1.f), col(0.35f, 0.5f, 1.f), f), fx_lerp(3.f, 0.6f, f), std::pow(1.f - f, 1.3f),
            FxPart::OrbTail);
    }
    TrailLook sp;
    sp.rate = 70.f;
    sp.life = 0.28f;
    sp.spread = 0.07f;
    sp.drift = 0.3f;
    sp.size0 = 0.045f;
    sp.size1 = 0.01f;
    sp.c0 = col(0.9f, 0.95f, 1.f);
    sp.c1 = col(0.3f, 0.45f, 1.f);
    sp.e0 = 2.4f;
    sp.e1 = 0.3f;
    sp.back = 0.2f;
    sp.part = FxPart::Spark;
    emit_trail(ctx, sp, out);
}

void missile_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    FxSpray sp;
    sp.origin = Vec3{ c.cx, c.cy, 0.55f };
    sp.cone = kPi;
    sp.speed_min = 0.8f;
    sp.speed_max = 1.8f;
    sp.gravity = -3.f;
    sp.drag = 2.f;
    sp.life_min = 0.18f;
    sp.life_max = 0.32f;
    sp.count = amount(h, c, 10.f, 5);
    sp.colour0 = col(1.f, 1.f, 1.f);
    sp.colour1 = col(0.35f, 0.5f, 1.f);
    sp.size0 = 0.055f;
    sp.size1 = 0.012f;
    sp.emissive0 = 3.f;
    sp.emissive1 = 0.4f;
    sp.part = FxPart::Spark;
    sp.seed = seed_of(h, c, 1u);
    (void)fx_emit_spray(sp, lt, out);
    FxRing r;
    r.centre = Vec3{ c.cx, c.cy, 0.07f };
    r.r0 = 0.05f;
    r.r1 = 0.34f;
    r.life = 0.22f;
    r.thickness = 0.03f;
    r.segments = ring_segments(h);
    r.colour = col(0.7f, 0.82f, 1.f);
    r.emissive = 2.0f;
    r.alpha = 0.9f;
    r.seed = seed_of(h, c, 2u);
    (void)fx_emit_ring(r, lt, out);
    if (lt < 0.1f) {
        const float u = lt / 0.1f;
        put_cube(out, Vec3{ c.cx, c.cy, 0.55f }, 0.3f * (1.f - u), col(0.9f, 0.95f, 1.f), 3.4f * (1.f - u), 1.f, FxPart::BurstFlash);
    }
}

/* ============================================================ 光の剣（psy_spear） */

//! 剣の姿：`lt` 秒の高さと向き（平面内の回転）。刃が下を向く。
void sword_pose(float lt, float &z, float &rotate)
{
    constexpr float kStrike = 0.16f;
    if (lt < kStrike) {
        const float u = lt / kStrike;
        z = fx_lerp(2.6f, 1.15f, u * u);
        rotate = kPi + (0.9f * (1.f - u));
    } else {
        z = 1.15f - (0.04f * ease_out((lt - kStrike) / 0.1f));
        rotate = kPi;
    }
}

void spear_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    constexpr float kStrike = 0.16f;
    const float cell_size = 0.072f * (0.8f + (0.2f * c.k));
    FxShapeColours colours;
    colours.base = col(1.00f, 0.88f, 0.30f);
    colours.keys = "oh";
    colours.colours = { col(1.00f, 0.98f, 0.70f), col(0.85f, 0.60f, 0.15f) };
    const float hold_alpha = 1.f - fx_ramp(lt, kStrike + 0.12f, kStrike + 0.37f);
    const auto draw = [&](float at, float alpha) {
        float z = 0.f;
        float rotate = 0.f;
        sword_pose(at, z, rotate);
        FxShapePlace place = billboard(h.spec, Vec3{ c.cx, c.cy, z }, cell_size, alpha, 2.0f);
        place.rotate = rotate;
        (void)fx_emit_shape(sword_shape(), colours, place, out);
    };
    const bool big = c.centre || h.small;
    if (big) {
        draw(lt, hold_alpha);
    }
    if (big && (lt < kStrike + 0.05f)) {
        // 振り下ろす間の残像。
        for (int g = 1; g <= 3; ++g) {
            const float at = lt - (0.022f * static_cast<float>(g));
            if ((at >= 0.f) && (at < kStrike)) {
                draw(at, 0.38f / static_cast<float>(g));
            }
        }
    }
    if (lt >= kStrike) {
        const float la = lt - kStrike;
        // 斬撃の閃き：横一文字の光と、火花、床の輪。
        if (big && (la < 0.15f)) {
            const float f = 1.f - (la / 0.15f);
            const Vec3 mid{ c.cx, c.cy, 0.8f };
            put_bar(out, mid - (h.spec.cam_right * (0.3f * c.k)), mid + (h.spec.cam_right * (0.3f * c.k)), 0.045f, col(1.f, 0.95f, 0.55f), 3.4f, f, FxPart::Spark);
        }
        FxSpray sp;
        sp.origin = Vec3{ c.cx, c.cy, 0.8f };
        sp.cone = kPi;
        sp.speed_min = 0.8f;
        sp.speed_max = 1.8f;
        sp.gravity = -4.f;
        sp.drag = 1.f;
        sp.life_min = 0.2f;
        sp.life_max = 0.35f;
        sp.count = amount(h, c, 8.f, 4);
        sp.colour0 = col(1.f, 0.95f, 0.5f);
        sp.colour1 = col(1.f, 0.65f, 0.15f);
        sp.size0 = 0.05f;
        sp.size1 = 0.012f;
        sp.emissive0 = 3.f;
        sp.emissive1 = 0.4f;
        sp.part = FxPart::Spark;
        sp.seed = seed_of(h, c, 1u);
        (void)fx_emit_spray(sp, la, out);
        FxRing r;
        r.centre = Vec3{ c.cx, c.cy, 0.07f };
        r.r0 = 0.05f;
        r.r1 = 0.42f * (0.8f + (0.2f * c.k));
        r.life = 0.2f;
        r.thickness = 0.035f;
        r.segments = ring_segments(h);
        r.colour = col(1.f, 0.9f, 0.4f);
        r.emissive = 2.2f;
        r.seed = seed_of(h, c, 2u);
        (void)fx_emit_ring(r, la, out);
    }
}

/* ============================================================ カオス（chaos） */

void chaos_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        OrbLook o;
        o.rainbow = true;
        o.e0 = 3.f;
        o.e1 = 2.0f;
        o.e2 = 1.2f;
        o.radius = 0.9f;
        o.flicker_hz = 30.f;
        o.light = hue_colour(ctx.t * 1.3f);
        emit_orb(ctx, o, Vec3{}, 1.f, true, out);
    }
    // 色の変わる粒の尾。
    const float fly = std::max(ctx.fly, 1e-4f);
    const float rate = 60.f;
    const float life = 0.4f;
    const int first = std::max(0, static_cast<int>(std::ceil((ctx.t - life) * rate)));
    const int last = static_cast<int>(std::floor(std::min(ctx.t, fly) * rate));
    for (int i = first; i <= last; ++i) {
        const float born = static_cast<float>(i) / rate;
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        FxParticle p;
        p.pos = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(born / fly))
            + Vec3{ (fx_rand(ctx.spec.seed, id, 11u) - 0.5f) * 0.18f, (fx_rand(ctx.spec.seed, id, 12u) - 0.5f) * 0.18f, (fx_rand(ctx.spec.seed, id, 13u) - 0.5f) * 0.18f };
        p.vel = Vec3{ (fx_rand(ctx.spec.seed, id, 14u) - 0.5f) * 0.4f, (fx_rand(ctx.spec.seed, id, 15u) - 0.5f) * 0.4f, 0.2f };
        p.gravity = 0.f;
        p.born = born;
        p.life = life;
        const float hue = (born * 1.3f) + (fx_rand(ctx.spec.seed, id, 16u) * 0.3f);
        p.colour0 = hue_colour(hue);
        p.colour1 = hue_colour(hue + 0.4f);
        p.size0 = 0.07f;
        p.size1 = 0.015f;
        p.emissive0 = 2.4f;
        p.emissive1 = 0.3f;
        p.part = FxPart::Spark;
        (void)fx_emit_particle(p, ctx.t, out);
    }
}

void chaos_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    // ねじれた渦：下が広く上がすぼまる漏斗の形に、色の変わる粒が巻きついて回る。
    const bool rich = (h.density >= 0.6f);
    const int arms = rich ? 3 : 2;
    const int pts = rich ? 15 : 8;
    const float grow = ease_out(lt / 0.3f);
    const float life = fx_envelope(lt, 0.05f, 0.4f, 0.4f);
    const float R0 = 0.44f * c.k;
    for (int a = 0; a < arms; ++a) {
        for (int j = 0; j < pts; ++j) {
            const float h01 = static_cast<float>(j) / static_cast<float>(pts - 1);
            const float ang = (static_cast<float>(a) * 2.f * kPi / static_cast<float>(arms)) + (lt * 9.f) + (h01 * 5.f);
            const float rad = R0 * grow * (1.f - (0.7f * h01));
            const float z = 0.06f + (0.8f * h01 * ease_out(lt / 0.5f));
            const float hue = (h01 * 0.9f) + (lt * 1.8f) + (static_cast<float>(a) / static_cast<float>(arms));
            put_cube(out, Vec3{ c.cx + (std::cos(ang) * rad), c.cy + (std::sin(ang) * rad), z }, 0.065f * (1.f - (0.35f * h01)) * (0.8f + (0.2f * c.k)),
                hue_colour(hue), 1.7f, life, FxPart::Spark);
        }
    }
    if (lt < 0.2f) {
        const float u = lt / 0.2f;
        put_cube(out, Vec3{ c.cx, c.cy, 0.4f }, 0.22f * (1.f - u) * c.k, hue_colour(lt * 5.f), 1.8f, 1.f, FxPart::BurstFlash);
    }
}

/* ============================================================ 地獄（nether） */

void nether_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        fx_emit_orb_body(ctx.spec, ctx.params, ctx.t, out);
    }
    // 暗い煙の尾。
    TrailLook tl;
    tl.rate = 32.f;
    tl.life = 0.6f;
    tl.spread = 0.12f;
    tl.drift = 0.12f;
    tl.rise = 0.15f;
    tl.size0 = 0.1f;
    tl.size1 = 0.2f;
    tl.c0 = col(0.30f, 0.22f, 0.38f);
    tl.c1 = col(0.10f, 0.08f, 0.14f);
    tl.e0 = 0.2f;
    tl.e1 = 0.f;
    tl.a0 = 0.5f;
    tl.a1 = 0.f;
    tl.part = FxPart::Mist;
    emit_trail(ctx, tl, out);
}

void nether_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    if (!prominent(h, c, 0.5f)) {
        return;
    }
    const int ghosts = (c.centre || h.small) ? 2 : 1;
    const float dur = 0.9f;
    FxShapeColours colours;
    colours.base = col(0.62f, 0.56f, 0.78f);
    for (int g = 0; g < ghosts; ++g) {
        const float age = lt - (0.25f * static_cast<float>(g));
        if ((age < 0.f) || (age >= dur)) {
            continue;
        }
        const float u = age / dur;
        const float cell_size = ((g == 0) ? 0.062f : 0.05f) * (0.8f + (0.2f * c.k));
        const float sway = 0.05f * std::sin((age * 7.f) + (static_cast<float>(g) * 2.f));
        const Vec3 pos = Vec3{ c.cx, c.cy, fx_lerp(0.30f, 1.15f, ease_out(u)) } + (h.spec.cam_right * sway);
        (void)fx_emit_shape(ghost_shape(), colours, billboard(h.spec, pos, cell_size, 0.5f * fx_envelope(age, 0.18f, 0.2f, 0.5f), 0.55f), out);
    }
    FxSmoke sm;
    sm.origin = Vec3{ c.cx, c.cy, 0.1f };
    sm.radius = 0.16f;
    sm.duration = 0.3f;
    sm.life = 0.8f;
    sm.count = amount(h, c, 3.f);
    sm.rise = 0.5f;
    sm.size = 0.14f;
    sm.grow = 1.6f;
    sm.colour = col(0.28f, 0.20f, 0.36f);
    sm.colour_end = col(0.10f, 0.08f, 0.14f);
    sm.alpha = 0.5f;
    sm.emissive = 0.2f;
    sm.seed = seed_of(h, c, 1u);
    (void)fx_emit_smoke(sm, lt, out);
}

/* ============================================================ 劣化（disenchant） */

void disenchant_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        OrbLook o;
        o.c0 = col(0.95f, 1.00f, 0.98f);
        o.c1 = col(0.55f, 0.88f, 0.82f);
        o.c2 = col(0.22f, 0.45f, 0.45f);
        o.radius = 0.75f;
        o.light = col(0.6f, 1.f, 0.9f);
        emit_orb(ctx, o, Vec3{}, 1.f, true, out);
    }
    TrailLook tl;
    tl.rate = 70.f;
    tl.life = 0.5f;
    tl.spread = 0.2f;
    tl.drift = 0.25f;
    tl.gravity = -2.5f;
    tl.size0 = 0.05f;
    tl.size1 = 0.02f;
    tl.c0 = col(1.f, 1.f, 0.9f);
    tl.c1 = col(0.45f, 0.45f, 0.45f);
    tl.e0 = 2.5f;
    tl.e1 = 0.f;
    tl.part = FxPart::Spark;
    emit_trail(ctx, tl, out);
}

void disenchant_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    const int motes = std::max(5, static_cast<int>(std::lround(16.f * h.density * (0.7f + (0.3f * c.k)))));
    for (int j = 0; j < motes; ++j) {
        const std::uint32_t id = static_cast<std::uint32_t>(j);
        const float ang = 2.f * kPi * rnd(h, c, id, 1u);
        const float rad = (0.20f + (0.14f * rnd(h, c, id, 2u))) * c.k;
        const float hgt = 0.12f + (0.85f * rnd(h, c, id, 3u));
        const float peel = 0.05f + (0.5f * rnd(h, c, id, 4u));
        const Vec3 pos0{ c.cx + (std::cos(ang) * rad), c.cy + (std::sin(ang) * rad), hgt };
        if (lt < peel) {
            // まだ張り付いて光っている粒。
            const float flick = 0.75f + (0.25f * std::sin((lt * 30.f) + (static_cast<float>(j) * 2.1f)));
            put_cube(out, pos0, 0.068f, col(1.f, 0.97f, 0.75f), 2.4f * flick, fx_clamp01(lt / 0.08f), FxPart::Spark);
        } else {
            FxParticle p;
            p.pos = pos0;
            p.vel = Vec3{ std::cos(ang) * 0.14f, std::sin(ang) * 0.14f, 0.05f };
            p.gravity = -2.4f;
            p.drag = 0.5f;
            p.born = peel;
            p.life = 0.55f + (0.25f * rnd(h, c, id, 5u));
            p.colour0 = col(1.f, 0.96f, 0.72f);
            p.colour1 = col(0.50f, 0.50f, 0.50f);
            p.size0 = 0.068f;
            p.size1 = 0.03f;
            p.emissive0 = 2.2f;
            p.emissive1 = 0.1f;
            p.alpha0 = 1.f;
            p.alpha1 = 0.f;
            p.floor = FxFloor::Stop;
            p.floor_z = 0.f;
            p.part = FxPart::Debris;
            (void)fx_emit_particle(p, lt, out);
        }
    }
}

/* ============================================================ 因果混乱（nexus） */

void nexus_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        OrbLook o;
        o.c0 = col(1.00f, 0.92f, 0.78f);
        o.c1 = col(0.78f, 0.45f, 0.90f);
        o.c2 = col(0.85f, 0.50f, 0.18f);
        o.radius = 0.8f;
        o.light = col(0.85f, 0.55f, 0.9f);
        const float s = flight_s(ctx);
        Vec3 b1;
        Vec3 b2;
        perp(flight_dir(ctx, s), b1, b2);
        // ずれて二重・三重に見える複製。
        const float wob = 0.15f * std::sin(ctx.t * 17.f);
        emit_orb(ctx, o, Vec3{}, 1.f, true, out);
        emit_orb(ctx, o, b1 * wob, 0.3f, false, out);
        emit_orb(ctx, o, b1 * -wob + (b2 * 0.5f * wob), 0.25f, false, out);
    }
    TrailLook tl;
    tl.rate = 40.f;
    tl.life = 0.35f;
    tl.spread = 0.22f;
    tl.drift = 0.35f;
    tl.size0 = 0.055f;
    tl.size1 = 0.015f;
    tl.c0 = col(0.95f, 0.65f, 1.f);
    tl.c1 = col(0.9f, 0.5f, 0.2f);
    tl.e0 = 2.2f;
    tl.e1 = 0.2f;
    tl.part = FxPart::Spark;
    emit_trail(ctx, tl, out);
}

void nexus_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    // 裂け目が開いて閉じる：細い → 中くらい → 広い → 中くらい → 細い。
    int width = -1;
    if (lt < 0.1f) {
        width = 0;
    } else if (lt < 0.2f) {
        width = 1;
    } else if (lt < 0.55f) {
        width = 2;
    } else if (lt < 0.65f) {
        width = 1;
    } else if (lt < 0.75f) {
        width = 0;
    }
    const float alpha = 1.f - fx_ramp(lt, 0.7f, 0.85f);
    if (!prominent(h, c, 0.3f)) {
        return;
    }
    if (width >= 0) {
        FxShapeColours colours;
        colours.base = col(1.00f, 0.60f, 0.20f);
        colours.keys = "o";
        colours.colours = { col(0.45f, 0.12f, 0.85f) };
        const float cell_size = 0.065f * (0.8f + (0.2f * c.k));
        FxShapePlace place = billboard(h.spec, Vec3{ c.cx, c.cy, 0.62f } + (fx_toward_camera(h.spec) * 0.28f), cell_size, 0.95f * alpha, 1.7f);
        (void)fx_emit_shape(rift_shape(width), colours, place, out);
    }
    // 裂け目のふちを走る火花（中心と単独のときだけ）。
    if ((c.centre || h.small) && (lt > 0.08f) && (lt < 0.7f)) {
        for (int i = 0; i < 2; ++i) {
            FxLightning l;
            const float side = (i == 0) ? 1.f : -1.f;
            l.a = Vec3{ c.cx, c.cy, 0.62f } + (fx_toward_camera(h.spec) * 0.28f) + (h.spec.cam_right * (0.03f * side));
            l.b = l.a + (h.spec.cam_right * (0.30f * side)) + (h.spec.cam_up * (0.12f * (static_cast<float>(i) - 0.5f)));
            l.segments = 5;
            l.jitter = 0.12f;
            l.core = 0.028f;
            l.glow = 0.08f;
            l.core_colour = col(1.f, 0.9f, 1.f);
            l.glow_colour = col(0.7f, 0.3f, 1.f);
            l.emissive = 2.6f;
            l.branches = 0;
            l.flicker_hz = 20.f;
            l.alpha = alpha;
            l.seed = seed_of(h, c, static_cast<std::uint32_t>(i));
            (void)fx_emit_lightning(l, lt, out);
        }
    }
}

void nexus_mod(const Hit &, const Cell &, float lt, FxEntityMod &m)
{
    // 一瞬ずれて二重に見える（0.15〜0.6 秒でふくらんで戻る）。
    const float u = fx_clamp01((lt - 0.12f) / 0.5f);
    const float pulse = std::sin(kPi * u);
    const float amount_x = 0.16f * pulse;
    if (amount_x > std::fabs(m.echo_x)) {
        m.echo_x = amount_x;
        m.echo_y = 0.04f * pulse;
    }
}

/* ============================================================ 時間逆転（time） */

void time_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        OrbLook o;
        o.c0 = col(0.85f, 0.88f, 1.00f);
        o.c1 = col(0.38f, 0.40f, 0.88f);
        o.c2 = col(0.14f, 0.14f, 0.42f);
        o.radius = 0.75f;
        o.light = col(0.45f, 0.5f, 1.f);
        emit_orb(ctx, o, Vec3{}, 1.f, true, out);
        // 逆回りに回る破線の輪。
        FxRing r;
        r.centre = out.projectile_pos;
        r.normal = fx_toward_camera(ctx.spec);
        r.r0 = r.r1 = 0.30f;
        r.life = 1.f;
        r.thickness = 0.03f;
        r.segments = 20;
        r.dash = 2;
        r.spin = ctx.t * 6.f;
        r.colour = col(0.7f, 0.75f, 1.f);
        r.emissive = 1.6f;
        r.alpha = 0.8f;
        r.fade_out = false;
        r.seed = ctx.spec.seed;
        (void)fx_emit_ring(r, ctx.t, out);
    }
    TrailLook tl;
    tl.rate = 36.f;
    tl.life = 0.4f;
    tl.spread = 0.14f;
    tl.drift = 0.15f;
    tl.size0 = 0.05f;
    tl.size1 = 0.015f;
    tl.c0 = col(0.7f, 0.75f, 1.f);
    tl.c1 = col(0.25f, 0.25f, 0.7f);
    tl.e0 = 2.f;
    tl.e1 = 0.2f;
    tl.part = FxPart::Spark;
    emit_trail(ctx, tl, out);
}

void time_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    const float K = c.k;
    const float alpha = fx_envelope(lt, 0.08f, 0.5f, 0.3f);
    if ((alpha <= 0.01f) || !prominent(h, c, 0.3f)) {
        return;
    }
    const FxColour face = col(0.60f, 0.66f, 1.00f);
    const Vec3 centre = Vec3{ c.cx, c.cy, 0.72f } + (fx_toward_camera(h.spec) * 0.25f);
    const Vec3 right = h.spec.cam_right;
    const Vec3 up = h.spec.cam_up;
    const bool rich = (h.density >= 0.6f);
    const float R0 = 0.38f * K;
    // 文字盤の輪（逆回りに回る破線）と、外側の細い輪。
    FxRing ring;
    ring.centre = centre;
    ring.normal = fx_toward_camera(h.spec);
    ring.r0 = ring.r1 = R0;
    ring.life = 1.f;
    ring.thickness = 0.035f;
    ring.segments = rich ? 28 : 14;
    ring.dash = rich ? 3 : 1;
    ring.spin = lt * 9.f;
    ring.colour = face;
    ring.emissive = 1.2f;
    ring.alpha = alpha;
    ring.fade_out = false;
    ring.seed = seed_of(h, c, 1u);
    (void)fx_emit_ring(ring, lt, out);
    if (rich) {
        FxRing outer = ring;
        outer.r0 = outer.r1 = R0 * 1.18f;
        outer.dash = 1;
        outer.spin = 0.f;
        outer.thickness = 0.02f;
        outer.alpha = alpha * 0.55f;
        (void)fx_emit_ring(outer, lt, out);
        // 目盛り（12 個。12・3・6・9 時は大きい）。
        for (int i = 0; i < 12; ++i) {
            const float a = static_cast<float>(i) * kPi / 6.f;
            const float tr = R0 * 0.82f;
            put_cube(out, centre + (right * (std::cos(a) * tr)) + (up * (std::sin(a) * tr)), ((i % 3) == 0) ? 0.05f : 0.03f, face, 1.8f, alpha, FxPart::Shape);
        }
    }
    // 針：長針と短針が逆回り（反時計回り）に回る。
    const float spin = ease_out(lt / 0.8f);
    const float a_min = (kPi * 0.5f) + (2.f * kPi * 1.5f * spin);
    const float a_hour = (kPi * 0.5f) + (2.f * kPi * 0.125f * spin) + 0.9f;
    put_bar(out, centre, centre + (right * (std::cos(a_min) * R0 * 0.78f)) + (up * (std::sin(a_min) * R0 * 0.78f)), 0.032f, col(0.9f, 0.93f, 1.f), 1.5f, alpha, FxPart::Shape);
    put_bar(out, centre, centre + (right * (std::cos(a_hour) * R0 * 0.5f)) + (up * (std::sin(a_hour) * R0 * 0.5f)), 0.042f, col(0.75f, 0.8f, 1.f), 1.4f, alpha, FxPart::Shape);
    put_cube(out, centre, 0.06f, col(1.f, 1.f, 1.f), 1.8f, alpha, FxPart::Shape);
}

/* ============================================================ 虚無（void_magic） */

void void_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        OrbLook o;
        o.c0 = col(0.01f, 0.00f, 0.02f);
        o.c1 = col(0.04f, 0.00f, 0.09f);
        o.c2 = col(0.45f, 0.15f, 0.80f);
        o.e0 = 0.f;
        o.e1 = 0.2f;
        o.e2 = 1.6f;
        o.radius = 0.75f;
        o.light = col(0.5f, 0.2f, 0.85f);
        o.light_k = 0.5f;
        emit_orb(ctx, o, Vec3{}, 1.f, true, out);
    }
    TrailLook tl;
    tl.rate = 40.f;
    tl.life = 0.35f;
    tl.spread = 0.3f;
    tl.drift = 0.f;
    tl.size0 = 0.06f;
    tl.size1 = 0.01f;
    tl.c0 = col(0.55f, 0.25f, 0.95f);
    tl.c1 = col(0.08f, 0.0f, 0.2f);
    tl.e0 = 1.6f;
    tl.e1 = 0.2f;
    tl.part = FxPart::Spark;
    emit_trail(ctx, tl, out);
}

void void_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    if (!prominent(h, c, 0.4f)) {
        return;
    }
    const float K = c.k * ((c.centre || h.small) ? 1.f : 0.6f);
    const Vec3 centre = Vec3{ c.cx, c.cy, 0.5f } + (fx_toward_camera(h.spec) * 0.25f);
    // 黒い球：0.12 秒で膨らみ、0.52 秒まで保ち、0.2 秒で縮んで消える。
    const float swell = (lt < 0.12f) ? ease_out(lt / 0.12f) : ((lt < 0.52f) ? 1.f : (1.f - ease_in((lt - 0.52f) / 0.2f)));
    const float rr = 0.19f * K * swell;
    if (rr > 0.02f) {
        put_ball(out, centre, rr, (h.density >= 0.6f) ? 0.075f : 0.11f, col(0.01f, 0.f, 0.02f), 0.f, 1.f, FxPart::Shape);
        FxRing rim;
        rim.centre = centre;
        rim.normal = fx_toward_camera(h.spec);
        rim.r0 = rim.r1 = (rr * 1.35f) + 0.02f;
        rim.life = 1.f;
        rim.thickness = 0.03f;
        rim.segments = ring_segments(h);
        rim.dash = 2;
        rim.spin = lt * 9.f;
        rim.colour = col(0.55f, 0.20f, 0.95f);
        rim.emissive = 2.0f;
        rim.alpha = 0.8f;
        rim.fade_out = false;
        rim.seed = seed_of(h, c, 1u);
        (void)fx_emit_ring(rim, lt, out);
    }
    // 周りの粒が中心へ吸い込まれる。
    const int motes = std::max(5, static_cast<int>(std::lround(11.f * h.density * (0.7f + (0.3f * K)))));
    for (int j = 0; j < motes; ++j) {
        const std::uint32_t id = static_cast<std::uint32_t>(j);
        const float ang = 2.f * kPi * rnd(h, c, id, 1u);
        const Vec3 start{ c.cx + (std::cos(ang) * 0.42f * K), c.cy + (std::sin(ang) * 0.42f * K), 0.12f + (0.8f * rnd(h, c, id, 2u)) };
        const float born = 0.04f + (0.22f * rnd(h, c, id, 3u));
        const float u = (lt - born) / 0.42f;
        if (u >= 1.f) {
            continue;
        }
        const float e = ease_in(u);
        const Vec3 pos = (u < 0.f) ? start : (start + ((centre - start) * e));
        const float uu = fx_clamp01(u);
        put_cube(out, pos, 0.055f * (1.f - (0.5f * uu)), fx_mix(col(0.85f, 0.55f, 1.f), col(0.2f, 0.0f, 0.4f), uu), 2.2f, fx_clamp01(lt / 0.06f), FxPart::Spark);
    }
}

/* ============================================================ 深淵（abyss） */

void abyss_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        OrbLook o;
        o.c0 = col(0.01f, 0.00f, 0.015f);
        o.c1 = col(0.03f, 0.00f, 0.05f);
        o.c2 = col(0.20f, 0.08f, 0.32f);
        o.e0 = 0.f;
        o.e1 = 0.1f;
        o.e2 = 0.8f;
        o.radius = 0.8f;
        o.light = col(0.35f, 0.15f, 0.55f);
        o.light_k = 0.4f;
        emit_orb(ctx, o, Vec3{}, 1.f, true, out);
    }
    // 下へたれる影。
    TrailLook tl;
    tl.rate = 36.f;
    tl.life = 0.5f;
    tl.spread = 0.16f;
    tl.drift = 0.08f;
    tl.gravity = -3.f;
    tl.size0 = 0.07f;
    tl.size1 = 0.03f;
    tl.c0 = col(0.08f, 0.02f, 0.14f);
    tl.c1 = col(0.01f, 0.f, 0.02f);
    tl.e0 = 0.4f;
    tl.e1 = 0.f;
    tl.part = FxPart::Debris;
    emit_trail(ctx, tl, out);
}

void abyss_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    const float K = c.k;
    // 足元の黒い穴とふち。
    FxPuddle hole;
    hole.cx = c.cx;
    hole.cy = c.cy;
    hole.z = 0.015f;
    hole.radius = 0.40f * K;
    hole.born = 0.f;
    hole.spread = 0.12f;
    hole.hold = 0.6f;
    hole.fade = 0.25f;
    hole.colour = col(0.01f, 0.f, 0.015f);
    hole.emissive = 0.f;
    hole.alpha = 0.97f;
    hole.voxel = puddle_voxel(h);
    hole.thickness = 0.03f;
    hole.ragged = 0.3f;
    hole.seed = seed_of(h, c, 1u);
    (void)fx_emit_puddle(hole, lt, out);
    FxRing rim;
    rim.centre = Vec3{ c.cx, c.cy, 0.05f };
    rim.r0 = 0.30f * K;
    rim.r1 = 0.40f * K;
    rim.life = 0.97f;
    rim.thickness = 0.03f;
    rim.segments = ring_segments(h);
    rim.colour = col(0.40f, 0.10f, 0.60f);
    rim.emissive = 1.0f;
    rim.alpha = 0.8f;
    rim.seed = seed_of(h, c, 2u);
    (void)fx_emit_ring(rim, lt, out);
    // 影の触手が這い上がる。
    const int tentacles = amount(h, c, (c.centre || h.small) ? 4.f : 3.f, 1);
    constexpr int kNodes = 9;
    for (int t = 0; t < tentacles; ++t) {
        const std::uint32_t id = static_cast<std::uint32_t>(t);
        const float phi = (2.f * kPi * (static_cast<float>(t) + rnd(h, c, id, 1u) * 0.6f)) / static_cast<float>(tentacles);
        const float rho = (0.15f + (0.13f * rnd(h, c, id, 2u))) * K;
        const float delay = 0.12f + (0.08f * rnd(h, c, id, 3u));
        const float len = 0.9f + (0.3f * rnd(h, c, id, 4u));
        float g = fx_ramp(lt, delay, delay + 0.35f);
        if (lt > 0.65f) {
            g *= 1.f - fx_ramp(lt, 0.65f, 0.95f);
        }
        if (g <= 0.01f) {
            continue;
        }
        for (int j = 0; j < kNodes; ++j) {
            const float f = static_cast<float>(j) / static_cast<float>(kNodes - 1);
            const float sway = 0.07f * f * K * std::sin((lt * 6.f) + (phi * 3.f) + (static_cast<float>(j) * 0.6f));
            const float z = 0.04f + (static_cast<float>(j) * 0.1f * g * len);
            put_cube(out, Vec3{ c.cx + (std::cos(phi) * rho) + sway, c.cy + (std::sin(phi) * rho) + (sway * 0.6f), z }, 0.09f * (1.f - (0.6f * f)) * (0.8f + (0.2f * K)),
                fx_mix(col(0.06f, 0.f, 0.11f), col(0.40f, 0.12f, 0.65f), f), 0.5f + (1.3f * f * f), 1.f, FxPart::Shape);
        }
    }
}

/* ============================================================ 聖なる炎（holy_fire） */

void holy_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    if (!prominent(h, c, 0.35f)) {
        return;
    }
    const float w = ((c.centre || h.small) ? 0.28f : 0.14f) * (0.8f + (0.2f * c.k));
    constexpr float kArrive = 0.14f;
    const float top = 3.4f;
    float bottom = 0.f;
    float alpha = 0.55f;
    float width = w;
    if (lt < kArrive) {
        bottom = fx_lerp(3.2f, 0.f, ease_in(lt / kArrive));
    } else {
        const float la = lt - kArrive;
        bottom = 0.f;
        const float fade = fx_ramp(la, 0.15f, 0.5f);
        alpha *= 1.f - fade;
        width = w * (1.f - (0.5f * fade));
    }
    const float mid = (top + bottom) * 0.5f;
    const float height = std::max(0.02f, top - bottom);
    const auto column = [&](float wd, const FxColour &colour, float emissive, float a) {
        if (a <= 0.004f) {
            return;
        }
        FxVoxel v;
        v.center = Vec3{ c.cx, c.cy, mid };
        v.size = Vec3{ wd, wd, height };
        v.r = colour.r;
        v.g = colour.g;
        v.b = colour.b;
        v.alpha = a;
        v.emissive = emissive;
        v.part = FxPart::Shape;
        out.voxels.push_back(v);
    };
    column(width * 1.5f, col(1.0f, 0.85f, 0.40f), 0.9f, alpha * 0.22f);
    column(width, col(1.0f, 0.92f, 0.55f), 1.3f, alpha * 0.8f);
    column(width * 0.45f, col(1.f, 0.98f, 0.85f), 2.0f, std::min(1.f, alpha * 1.2f));
    if (lt >= kArrive) {
        const float la = lt - kArrive;
        FxRing r;
        r.centre = Vec3{ c.cx, c.cy, 0.07f };
        r.r0 = 0.05f;
        r.r1 = 0.45f * (0.8f + (0.2f * c.k));
        r.life = 0.22f;
        r.thickness = 0.04f;
        r.segments = ring_segments(h);
        r.colour = col(1.f, 0.9f, 0.5f);
        r.emissive = 2.2f;
        r.seed = seed_of(h, c, 1u);
        (void)fx_emit_ring(r, la, out);
        FxSpray sp;
        sp.origin = Vec3{ c.cx, c.cy, 0.08f };
        sp.dir = Vec3{ 0.f, 0.f, 1.f };
        sp.cone = 0.7f;
        sp.scatter = 0.18f;
        sp.speed_min = 0.5f;
        sp.speed_max = 1.2f;
        sp.gravity = -0.8f;
        sp.life_min = 0.4f;
        sp.life_max = 0.7f;
        sp.count = amount(h, c, 7.f, 3);
        sp.colour0 = col(1.f, 0.97f, 0.7f);
        sp.colour1 = col(1.f, 0.8f, 0.3f);
        sp.size0 = 0.045f;
        sp.size1 = 0.015f;
        sp.emissive0 = 2.6f;
        sp.emissive1 = 0.4f;
        sp.part = FxPart::Spark;
        sp.seed = seed_of(h, c, 2u);
        (void)fx_emit_spray(sp, la, out);
    }
}

/* ============================================================ 地獄の劫火（hell_fire） */

void hell_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    const float K = c.k;
    // 床の割れ目の赤い光。
    FxPuddle p;
    p.cx = c.cx;
    p.cy = c.cy;
    p.z = 0.012f;
    p.radius = 0.40f * K;
    p.spread = 0.08f;
    p.hold = 0.5f;
    p.fade = 0.3f;
    p.colour = col(0.55f, 0.04f, 0.02f);
    p.emissive = 1.4f;
    p.alpha = 0.9f;
    p.voxel = puddle_voxel(h);
    p.thickness = 0.025f;
    p.ragged = 0.5f;
    p.seed = seed_of(h, c, 1u);
    (void)fx_emit_puddle(p, lt, out);
    // 噴き上がる炎の柱：赤と黒の立方体が積み重なって揺れる。
    const float rise = (lt < 0.2f) ? ease_out(lt / 0.2f) : ((lt < 0.4f) ? 1.f : (1.f - ease_in((lt - 0.4f) / 0.35f)));
    const float H = 1.6f * rise;
    if (H > 0.05f) {
        const int segs = std::max(4, static_cast<int>(std::lround(9.f * h.density)));
        const std::uint32_t frame = static_cast<std::uint32_t>(std::floor(lt * 24.f));
        for (int s = 0; s < segs; ++s) {
            const float f = static_cast<float>(s) / static_cast<float>(segs - 1);
            const std::uint32_t id = static_cast<std::uint32_t>(s);
            const float jx = (rnd(h, c, id, frame) - 0.5f) * 0.10f;
            const float jy = (rnd(h, c, id, frame + 1000u) - 0.5f) * 0.10f;
            const bool red = ((s % 2) == 0);
            put_cube(out, Vec3{ c.cx + jx, c.cy + jy, 0.1f + (H * f) }, (0.30f - (0.13f * f)) * K,
                red ? fx_mix(col(1.f, 0.35f, 0.08f), col(0.8f, 0.08f, 0.03f), f) : col(0.05f, 0.f, 0.f), red ? (1.2f - (0.5f * f)) : 0.1f, 0.95f - (0.35f * f),
                red ? FxPart::Ember : FxPart::Debris);
        }
    }
    // 火の粉と黒い塊が噴き上がる。
    FxSpray em;
    em.origin = Vec3{ c.cx, c.cy, 0.05f };
    em.dir = Vec3{ 0.f, 0.f, 1.f };
    em.cone = 0.35f;
    em.scatter = 0.2f * K;
    em.speed_min = 2.2f;
    em.speed_max = 3.6f;
    em.gravity = -4.5f;
    em.drag = 0.2f;
    em.duration = 0.25f;
    em.life_min = 0.35f;
    em.life_max = 0.6f;
    em.count = amount(h, c, 14.f, 5);
    em.colour0 = col(1.f, 0.3f, 0.06f);
    em.colour1 = col(0.05f, 0.f, 0.f);
    em.size0 = 0.10f;
    em.size1 = 0.04f;
    em.emissive0 = 1.4f;
    em.emissive1 = 0.15f;
    em.part = FxPart::Spark;
    em.seed = seed_of(h, c, 2u);
    (void)fx_emit_spray(em, lt, out);
    FxSpray ch = em;
    ch.speed_min = 1.5f;
    ch.speed_max = 2.8f;
    ch.count = amount(h, c, 5.f, 2);
    ch.colour0 = col(0.05f, 0.f, 0.f);
    ch.colour1 = col(0.02f, 0.f, 0.f);
    ch.size0 = 0.09f;
    ch.size1 = 0.06f;
    ch.emissive0 = 0.f;
    ch.emissive1 = 0.f;
    ch.alpha1 = 0.2f;
    ch.part = FxPart::Debris;
    ch.seed = seed_of(h, c, 3u);
    (void)fx_emit_spray(ch, lt, out);
}

/* ============================================================ ロケット（rocket） */

void rocket_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    if (ctx.t < ctx.fly) {
        const float s = flight_s(ctx);
        const Vec3 centre = fx_flight_position(ctx.spec, ctx.params, s);
        const Vec3 dir = flight_dir(ctx, s);
        Vec3 b1;
        Vec3 b2;
        perp(dir, b1, b2);
        // 胴・白い帯・先端・尾翼 2 枚（実物の 1.6 倍）。
        const float sc = 1.6f;
        put_bar(out, centre - (dir * (0.18f * sc)), centre + (dir * (0.10f * sc)), 0.085f * sc, col(0.85f, 0.12f, 0.08f), 0.25f, 1.f, FxPart::Other);
        put_bar(out, centre - (dir * (0.05f * sc)), centre, 0.095f * sc, col(0.9f, 0.9f, 0.92f), 0.2f, 1.f, FxPart::Other);
        put_bar(out, centre + (dir * (0.10f * sc)), centre + (dir * (0.19f * sc)), 0.06f * sc, col(0.9f, 0.9f, 0.92f), 0.3f, 1.f, FxPart::Other);
        put_cube(out, centre + (dir * (0.215f * sc)), 0.035f * sc, col(0.9f, 0.9f, 0.92f), 0.3f, 1.f, FxPart::Other);
        const Vec3 rear = centre - (dir * (0.16f * sc));
        put_bar(out, rear - (b1 * (0.075f * sc)), rear + (b1 * (0.075f * sc)), 0.028f * sc, col(0.25f, 0.25f, 0.28f), 0.f, 1.f, FxPart::Other);
        put_bar(out, rear - (b2 * (0.075f * sc)), rear + (b2 * (0.075f * sc)), 0.028f * sc, col(0.25f, 0.25f, 0.28f), 0.f, 1.f, FxPart::Other);
        // 噴き出す炎。
        const std::uint32_t frame = static_cast<std::uint32_t>(std::floor(ctx.t * 30.f));
        for (int j2 = 0; j2 < 4; ++j2) {
            const std::uint32_t id = static_cast<std::uint32_t>(j2);
            const float f = static_cast<float>(j2) / 3.f;
            const Vec3 p = centre - (dir * ((0.20f + (0.07f * static_cast<float>(j2))) * sc)) + (b1 * ((fx_rand(ctx.spec.seed, id, frame, 1u) - 0.5f) * 0.05f))
                + (b2 * ((fx_rand(ctx.spec.seed, id, frame, 2u) - 0.5f) * 0.05f));
            put_cube(out, p, (0.085f - (0.015f * static_cast<float>(j2))) * sc, fx_mix(col(1.f, 0.95f, 0.6f), col(1.f, 0.4f, 0.08f), f), 3.0f - (0.5f * static_cast<float>(j2)), 1.f,
                FxPart::Ember);
        }
        PointLight light;
        light.x = centre.x;
        light.y = centre.y;
        light.z = centre.z + 0.3f;
        light.radius = 3.6f;
        light.r = 1.f;
        light.g = 0.6f;
        light.b = 0.25f;
        light.intensity = 1.8f;
        out.lights.push_back(light);
        out.projectile_visible = true;
        out.projectile_pos = centre;
        out.projectile_dir = dir;
    }
    TrailLook smoke;
    smoke.rate = 70.f;
    smoke.life = 1.0f;
    smoke.spread = 0.06f;
    smoke.drift = 0.15f;
    smoke.rise = 0.2f;
    smoke.size0 = 0.11f;
    smoke.size1 = 0.27f;
    smoke.c0 = col(0.78f, 0.78f, 0.78f);
    smoke.c1 = col(0.40f, 0.40f, 0.40f);
    smoke.e0 = 0.1f;
    smoke.e1 = 0.f;
    smoke.a0 = 0.55f;
    smoke.a1 = 0.f;
    smoke.part = FxPart::Mist;
    smoke.back = 0.3f;
    emit_trail(ctx, smoke, out);
    TrailLook sparks;
    sparks.rate = 50.f;
    sparks.life = 0.25f;
    sparks.spread = 0.08f;
    sparks.drift = 0.4f;
    sparks.size0 = 0.05f;
    sparks.size1 = 0.012f;
    sparks.c0 = col(1.f, 0.8f, 0.3f);
    sparks.c1 = col(0.8f, 0.2f, 0.05f);
    sparks.e0 = 2.6f;
    sparks.e1 = 0.3f;
    sparks.back = 0.25f;
    sparks.part = FxPart::Ember;
    emit_trail(ctx, sparks, out);
}

void rocket_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    const float K = c.k;
    // 焦げ跡。
    FxPuddle scorch;
    scorch.cx = c.cx;
    scorch.cy = c.cy;
    scorch.z = 0.012f;
    scorch.radius = 0.44f * K;
    scorch.born = 0.03f;
    scorch.spread = 0.1f;
    scorch.hold = 0.35f;
    scorch.fade = 0.4f;
    scorch.colour = col(0.06f, 0.05f, 0.04f);
    scorch.alpha = 0.6f;
    scorch.voxel = (h.density >= 0.6f) ? 0.07f : 0.1f;
    scorch.thickness = 0.02f;
    scorch.ragged = 0.3f;
    scorch.seed = seed_of(h, c, 1u);
    if (h.density > 0.2f) {
        (void)fx_emit_puddle(scorch, lt, out);
    }
    // 地面を走る衝撃波。
    FxRing ring;
    ring.centre = Vec3{ c.cx, c.cy, 0.07f };
    ring.r0 = 0.08f;
    ring.r1 = 0.46f * K;
    ring.life = 0.22f;
    ring.thickness = 0.04f;
    ring.segments = ring_segments(h);
    ring.colour = col(1.f, 0.85f, 0.5f);
    ring.emissive = 2.2f;
    ring.seed = seed_of(h, c, 2u);
    (void)fx_emit_ring(ring, lt, out);
    // 火花と破片。
    FxSpray sp;
    sp.origin = Vec3{ c.cx, c.cy, 0.12f };
    sp.cone = 1.5f;
    sp.speed_min = 1.2f;
    sp.speed_max = 2.6f;
    sp.gravity = -8.f;
    sp.drag = 0.5f;
    sp.life_min = 0.25f;
    sp.life_max = 0.5f;
    sp.count = amount(h, c, 10.f, 5);
    sp.colour0 = col(1.f, 0.9f, 0.4f);
    sp.colour1 = col(0.8f, 0.15f, 0.04f);
    sp.size0 = 0.07f;
    sp.size1 = 0.02f;
    sp.emissive0 = 2.4f;
    sp.emissive1 = 0.2f;
    sp.floor = FxFloor::Stop;
    sp.part = FxPart::Spark;
    sp.seed = seed_of(h, c, 3u);
    (void)fx_emit_spray(sp, lt, out);
    FxSpray de = sp;
    de.speed_min = 0.8f;
    de.speed_max = 1.8f;
    de.life_min = 0.4f;
    de.life_max = 0.8f;
    de.count = amount(h, c, 4.f, 2);
    de.colour0 = col(0.18f, 0.16f, 0.15f);
    de.colour1 = col(0.08f, 0.07f, 0.07f);
    de.size0 = 0.09f;
    de.size1 = 0.06f;
    de.emissive0 = 0.f;
    de.emissive1 = 0.f;
    de.alpha1 = 0.3f;
    de.part = FxPart::Debris;
    de.seed = seed_of(h, c, 4u);
    (void)fx_emit_spray(de, lt, out);
    // 煙の柱。
    FxSmoke sm;
    sm.origin = Vec3{ c.cx, c.cy, 0.15f };
    sm.radius = 0.2f;
    sm.born = 0.06f;
    sm.duration = 0.25f;
    sm.life = 1.0f;
    sm.count = amount(h, c, 3.f);
    sm.rise = 0.75f;
    sm.size = 0.21f;
    sm.grow = 1.9f;
    sm.colour = col(0.32f, 0.30f, 0.29f);
    sm.colour_end = col(0.18f, 0.17f, 0.17f);
    sm.alpha = 0.6f;
    sm.emissive = 0.3f;
    sm.seed = seed_of(h, c, 5u);
    (void)fx_emit_smoke(sm, lt, out);
}

/* ============================================================ 放射性廃棄物（nuke） */

void nuke_cell(const Hit &h, const Cell &c, float lt, FxFrame &out)
{
    const float K = c.k;
    FxSmoke fog;
    fog.origin = Vec3{ c.cx, c.cy, 0.12f };
    fog.radius = 0.24f * K;
    fog.born = 0.08f;
    fog.duration = 0.35f;
    fog.life = 1.5f;
    fog.count = amount(h, c, 6.f, 2);
    fog.rise = 0.10f;
    fog.size = 0.20f;
    fog.grow = 1.15f;
    fog.colour = col(0.35f, 1.0f, 0.22f);
    fog.colour_end = col(0.18f, 0.60f, 0.12f);
    fog.alpha = 0.42f;
    fog.emissive = 1.1f;
    fog.seed = seed_of(h, c, 1u);
    const std::size_t n0 = out.voxels.size();
    (void)fx_emit_smoke(fog, lt, out);
    // 明滅：マスごとの位相に、塊ごとの遅れを足して濃さを揺らす。
    const float phase = rnd(h, c, 1u) * 6.28f;
    for (std::size_t i = n0; i < out.voxels.size(); ++i) {
        const float f01 = 0.5f + (0.5f * std::sin((lt * 13.f) + phase + (static_cast<float>(i - n0) * 1.3f)));
        out.voxels[i].alpha *= 0.45f + (0.55f * f01);
        out.voxels[i].emissive *= 0.6f + (0.8f * f01);
    }
    // 昇っていく放射の粒。
    FxSpray sp;
    sp.origin = Vec3{ c.cx, c.cy, 0.1f };
    sp.dir = Vec3{ 0.f, 0.f, 1.f };
    sp.cone = 0.5f;
    sp.scatter = 0.2f * K;
    sp.speed_min = 0.3f;
    sp.speed_max = 0.7f;
    sp.gravity = 0.f;
    sp.drag = 0.8f;
    sp.duration = 0.6f;
    sp.life_min = 0.6f;
    sp.life_max = 1.0f;
    sp.count = amount(h, c, 5.f, 2);
    sp.colour0 = col(0.6f, 1.f, 0.4f);
    sp.colour1 = col(0.1f, 0.5f, 0.05f);
    sp.size0 = 0.045f;
    sp.size1 = 0.015f;
    sp.emissive0 = 2.f;
    sp.emissive1 = 0.2f;
    sp.part = FxPart::Spark;
    sp.seed = seed_of(h, c, 2u);
    (void)fx_emit_spray(sp, lt, out);
}

/* ============================================================ 色の組 */

FxPalette make_palette(const FxColour (&orb)[3], const FxColour (&burst)[4], const FxColour (&flash)[2], const FxColour &smoke_warm, const FxColour &smoke_cool,
    float smoke_alpha, const FxColour (&ember)[2], const FxColour &light_orb, const FxColour &light_burst, float emissive_scale)
{
    FxPalette p; // 火の試作と同じ自己発光の段（色だけ入れ替える）
    for (int i = 0; i < 3; ++i) {
        p.orb[i] = orb[i];
        p.orb_emissive[i] *= emissive_scale;
    }
    for (int i = 0; i < 4; ++i) {
        p.burst[i] = burst[i];
        p.burst_emissive[i] *= emissive_scale;
    }
    p.flash[0] = flash[0];
    p.flash[1] = flash[1];
    p.smoke_warm = smoke_warm;
    p.smoke_cool = smoke_cool;
    p.smoke_alpha = smoke_alpha;
    p.ember[0] = ember[0];
    p.ember[1] = ember[1];
    p.light_orb = light_orb;
    p.light_burst = light_burst;
    p.is_fire = false;
    p.known = true;
    return p;
}

//! 火炎の色違い：水色〜白。
FxPalette cold_palette(const FxStyle &)
{
    const FxColour orb[3] = { col(0.92f, 0.99f, 1.00f), col(0.40f, 0.78f, 1.00f), col(0.10f, 0.30f, 0.85f) };
    const FxColour burst[4] = { col(0.85f, 0.97f, 1.00f), col(0.55f, 0.88f, 1.00f), col(0.20f, 0.50f, 0.95f), col(0.05f, 0.12f, 0.45f) };
    const FxColour flash[2] = { col(0.95f, 1.00f, 1.00f), col(0.60f, 0.88f, 1.00f) };
    const FxColour ember[2] = { col(0.80f, 0.95f, 1.00f), col(0.15f, 0.40f, 0.90f) };
    return make_palette(orb, burst, flash, col(0.35f, 0.50f, 0.62f), col(0.30f, 0.34f, 0.38f), 0.55f, ember, col(0.50f, 0.85f, 1.00f), col(0.45f, 0.80f, 1.00f), 1.0f);
}

//! 灰色がかった紫。
FxPalette nether_palette(const FxStyle &)
{
    const FxColour orb[3] = { col(0.70f, 0.65f, 0.80f), col(0.45f, 0.35f, 0.60f), col(0.18f, 0.12f, 0.25f) };
    const FxColour burst[4] = { col(0.42f, 0.33f, 0.52f), col(0.30f, 0.22f, 0.40f), col(0.18f, 0.12f, 0.26f), col(0.06f, 0.04f, 0.10f) };
    const FxColour flash[2] = { col(0.60f, 0.55f, 0.70f), col(0.38f, 0.30f, 0.50f) };
    const FxColour ember[2] = { col(0.65f, 0.55f, 0.80f), col(0.20f, 0.12f, 0.30f) };
    return make_palette(orb, burst, flash, col(0.22f, 0.16f, 0.30f), col(0.12f, 0.10f, 0.16f), 0.30f, ember, col(0.55f, 0.40f, 0.80f), col(0.50f, 0.38f, 0.75f), 0.55f);
}

//! 白金色の炎。
FxPalette holy_palette(const FxStyle &)
{
    const FxColour orb[3] = { col(1.00f, 1.00f, 0.90f), col(1.00f, 0.88f, 0.45f), col(0.85f, 0.60f, 0.15f) };
    const FxColour burst[4] = { col(1.00f, 0.98f, 0.85f), col(1.00f, 0.90f, 0.55f), col(0.95f, 0.75f, 0.25f), col(0.50f, 0.35f, 0.10f) };
    const FxColour flash[2] = { col(1.00f, 1.00f, 0.95f), col(1.00f, 0.90f, 0.60f) };
    const FxColour ember[2] = { col(1.00f, 0.95f, 0.70f), col(0.80f, 0.60f, 0.20f) };
    return make_palette(orb, burst, flash, col(0.60f, 0.50f, 0.30f), col(0.40f, 0.36f, 0.30f), 0.35f, ember, col(1.00f, 0.92f, 0.60f), col(1.00f, 0.90f, 0.55f), 0.7f);
}

//! 黒と赤の炎。
FxPalette hell_palette(const FxStyle &)
{
    const FxColour orb[3] = { col(1.00f, 0.55f, 0.25f), col(0.70f, 0.06f, 0.03f), col(0.12f, 0.00f, 0.00f) };
    const FxColour burst[4] = { col(0.90f, 0.15f, 0.06f), col(0.60f, 0.06f, 0.03f), col(0.22f, 0.02f, 0.02f), col(0.03f, 0.00f, 0.00f) };
    const FxColour flash[2] = { col(0.95f, 0.35f, 0.25f), col(0.60f, 0.08f, 0.05f) };
    const FxColour ember[2] = { col(1.00f, 0.30f, 0.08f), col(0.20f, 0.00f, 0.00f) };
    return make_palette(orb, burst, flash, col(0.25f, 0.03f, 0.02f), col(0.05f, 0.04f, 0.04f), 0.90f, ember, col(1.00f, 0.20f, 0.10f), col(1.00f, 0.20f, 0.08f), 0.9f);
}

//! ロケットの爆発：黄白 → 橙 → 赤 → 黒い煙。
FxPalette rocket_palette(const FxStyle &)
{
    const FxColour orb[3] = { col(1.00f, 0.92f, 0.60f), col(1.00f, 0.55f, 0.15f), col(0.80f, 0.15f, 0.05f) };
    const FxColour burst[4] = { col(1.00f, 0.95f, 0.60f), col(1.00f, 0.65f, 0.20f), col(0.90f, 0.28f, 0.05f), col(0.30f, 0.05f, 0.02f) };
    const FxColour flash[2] = { col(1.00f, 0.98f, 0.80f), col(1.00f, 0.75f, 0.35f) };
    const FxColour ember[2] = { col(1.00f, 0.80f, 0.30f), col(0.80f, 0.15f, 0.04f) };
    return make_palette(orb, burst, flash, col(0.40f, 0.30f, 0.22f), col(0.20f, 0.19f, 0.18f), 0.85f, ember, col(1.00f, 0.60f, 0.25f), col(1.00f, 0.55f, 0.20f), 0.8f);
}

//! 緑に光る玉と爆発。
FxPalette nuke_palette(const FxStyle &)
{
    const FxColour orb[3] = { col(0.85f, 1.00f, 0.70f), col(0.40f, 1.00f, 0.25f), col(0.10f, 0.50f, 0.10f) };
    const FxColour burst[4] = { col(0.75f, 1.00f, 0.50f), col(0.45f, 1.00f, 0.25f), col(0.15f, 0.65f, 0.10f), col(0.04f, 0.20f, 0.04f) };
    const FxColour flash[2] = { col(0.90f, 1.00f, 0.80f), col(0.50f, 1.00f, 0.30f) };
    const FxColour ember[2] = { col(0.70f, 1.00f, 0.40f), col(0.10f, 0.50f, 0.05f) };
    return make_palette(orb, burst, flash, col(0.20f, 0.50f, 0.10f), col(0.10f, 0.20f, 0.08f), 0.50f, ember, col(0.45f, 1.00f, 0.30f), col(0.40f, 1.00f, 0.28f), 1.1f);
}

//! マジック・ミサイル：青白い光。
FxPalette missile_palette(const FxStyle &)
{
    const FxColour orb[3] = { col(1.00f, 1.00f, 1.00f), col(0.80f, 0.88f, 1.00f), col(0.35f, 0.50f, 1.00f) };
    const FxColour burst[4] = { col(1.00f, 1.00f, 1.00f), col(0.80f, 0.88f, 1.00f), col(0.40f, 0.55f, 1.00f), col(0.10f, 0.15f, 0.50f) };
    const FxColour flash[2] = { col(1.00f, 1.00f, 1.00f), col(0.70f, 0.82f, 1.00f) };
    const FxColour ember[2] = { col(0.90f, 0.95f, 1.00f), col(0.30f, 0.45f, 1.00f) };
    return make_palette(orb, burst, flash, col(0.40f, 0.45f, 0.60f), col(0.30f, 0.32f, 0.38f), 0.10f, ember, col(0.70f, 0.82f, 1.00f), col(0.70f, 0.82f, 1.00f), 1.0f);
}

/* ============================================================ 属性の表 */

std::deque<Look> build_looks()
{
    std::deque<Look> v;
    const auto add = [&v](const char *attr, const FxColour &base, float smoke, float emissive) -> Look & {
        v.emplace_back();
        Look &l = v.back();
        l.attr = attr;
        l.base = base;
        l.smoke = smoke;
        l.emissive = emissive;
        return l;
    };

    {
        Look &l = add("cold", col(0.55f, 0.85f, 1.00f), 0.25f, 1.0f);
        l.palette = cold_palette; // 火炎の色違い：玉も爆発も汎用（火炎と同じ作り）
    }
    {
        Look &l = add("ice", col(0.55f, 0.85f, 1.00f), 0.25f, 1.0f);
        l.flight = ice_flight;
        l.tail = 0.45f;
        l.cell = ice_cell;
        l.seconds = 1.0f;
        l.light = col(0.55f, 0.85f, 1.0f);
        l.light_peak = 1.6f;
        l.light_decay = 0.5f;
    }
    {
        Look &l = add("shards", col(0.75f, 0.65f, 0.50f), 0.30f, 1.0f);
        l.flight = shards_flight;
        l.tail = 0.45f;
        l.cell = shards_cell;
        l.seconds = 1.0f;
        l.light = col(1.0f, 0.85f, 0.6f);
        l.light_peak = 0.8f;
        l.light_decay = 0.4f;
    }
    {
        Look &l = add("sound", col(0.90f, 0.80f, 0.50f), 0.15f, 1.0f);
        l.flight = sound_flight;
        l.tail = 0.4f;
        l.cell = sound_cell;
        l.mod = sound_mod;
        l.seconds = 1.1f;
        l.free = true;
        l.light = col(1.0f, 0.92f, 0.6f);
        l.light_peak = 1.0f;
        l.light_decay = 0.4f;
        l.shake = 0.08f;
        l.shake_seconds = 0.45f;
    }
    {
        Look &l = add("force", col(0.80f, 0.75f, 0.95f), 0.10f, 1.0f);
        l.flight = force_flight;
        l.tail = 0.35f;
        l.cell = force_cell;
        l.mod = force_mod;
        l.seconds = 0.9f;
        l.free = true;
        l.light = col(0.85f, 0.85f, 1.0f);
        l.light_peak = 0.9f;
        l.light_decay = 0.35f;
    }
    {
        Look &l = add("meteor", col(1.00f, 0.50f, 0.20f), 0.60f, 1.0f);
        l.flight = meteor_flight;
        l.tail = 0.7f;
        l.cell = meteor_cell;
        l.seconds = 1.35f;
        l.light = col(1.0f, 0.55f, 0.2f);
        l.light_peak = 2.0f;
        l.light_decay = 0.55f;
        l.light_delay = 0.2f;
    }
    {
        Look &l = add("gravity", col(0.60f, 0.45f, 0.35f), 0.40f, 0.8f);
        l.flight = gravity_flight;
        l.tail = 0.55f;
        l.cell = gravity_cell;
        l.mod = gravity_mod;
        l.seconds = 0.9f;
        l.light = col(0.6f, 0.4f, 0.8f);
        l.light_peak = 0.8f;
        l.light_decay = 0.45f;
    }
    {
        Look &l = add("inertial", col(0.70f, 0.70f, 0.75f), 0.20f, 0.9f);
        l.flight = inertial_flight;
        l.tail = 0.85f;
        l.cell = inertial_cell;
        l.seconds = 1.1f;
        l.light = col(0.65f, 0.76f, 1.0f);
        l.light_peak = 0.7f;
        l.light_decay = 0.6f;
    }
    {
        Look &l = add("missile", col(0.85f, 0.88f, 1.00f), 0.05f, 1.0f);
        l.palette = missile_palette;
        l.flight = missile_flight;
        l.tail = 0.25f;
        l.cell = missile_cell;
        l.seconds = 0.45f;
        l.underlay = true;
    }
    {
        Look &l = add("psy_spear", col(1.00f, 0.90f, 0.30f), 0.05f, 1.2f);
        l.cell = spear_cell;
        l.seconds = 0.7f;
        l.light = col(1.0f, 0.9f, 0.35f);
        l.light_peak = 2.2f;
        l.light_decay = 0.35f;
        l.light_delay = 0.16f;
    }
    {
        Look &l = add("chaos", col(0.95f, 0.45f, 0.95f), 0.35f, 1.0f);
        l.flight = chaos_flight;
        l.tail = 0.5f;
        l.cell = chaos_cell;
        l.seconds = 0.85f;
        l.light = col(0.9f, 0.5f, 1.0f);
        l.light_peak = 1.4f;
        l.light_decay = 0.5f;
    }
    {
        Look &l = add("nether", col(0.45f, 0.42f, 0.62f), 0.55f, 0.55f);
        l.palette = nether_palette;
        l.flight = nether_flight;
        l.tail = 0.75f;
        l.cell = nether_cell;
        l.seconds = 1.15f;
        l.underlay = true;
    }
    {
        Look &l = add("disenchant", col(0.55f, 0.75f, 0.70f), 0.30f, 1.0f);
        l.flight = disenchant_flight;
        l.tail = 0.65f;
        l.cell = disenchant_cell;
        l.seconds = 1.4f;
        l.light = col(0.8f, 1.0f, 0.95f);
        l.light_peak = 0.9f;
        l.light_decay = 0.8f;
    }
    {
        Look &l = add("nexus", col(0.80f, 0.60f, 0.30f), 0.30f, 1.0f);
        l.flight = nexus_flight;
        l.tail = 0.5f;
        l.cell = nexus_cell;
        l.mod = nexus_mod;
        l.seconds = 0.9f;
        l.light = col(0.85f, 0.5f, 1.0f);
        l.light_peak = 1.2f;
        l.light_decay = 0.7f;
    }
    {
        Look &l = add("time", col(0.40f, 0.40f, 0.75f), 0.20f, 0.8f);
        l.flight = time_flight;
        l.tail = 0.5f;
        l.cell = time_cell;
        l.seconds = 0.95f;
        l.light = col(0.5f, 0.55f, 1.0f);
        l.light_peak = 1.0f;
        l.light_decay = 0.7f;
    }
    {
        Look &l = add("void_magic", col(0.20f, 0.10f, 0.32f), 0.50f, 0.6f);
        l.flight = void_flight;
        l.tail = 0.45f;
        l.cell = void_cell;
        l.seconds = 0.8f;
        l.light = col(0.5f, 0.2f, 0.85f);
        l.light_peak = 0.7f;
        l.light_decay = 0.6f;
    }
    {
        Look &l = add("abyss", col(0.14f, 0.09f, 0.22f), 0.50f, 0.5f);
        l.flight = abyss_flight;
        l.tail = 0.6f;
        l.cell = abyss_cell;
        l.seconds = 1.0f;
        l.light = col(0.4f, 0.12f, 0.6f);
        l.light_peak = 0.6f;
        l.light_decay = 0.8f;
    }
    {
        Look &l = add("holy_fire", col(1.00f, 0.95f, 0.60f), 0.10f, 1.2f);
        l.palette = holy_palette;
        l.cell = holy_cell;
        l.seconds = 0.8f;
        l.underlay = true;
    }
    {
        Look &l = add("hell_fire", col(0.85f, 0.15f, 0.10f), 0.70f, 0.9f);
        l.palette = hell_palette;
        l.cell = hell_cell;
        l.seconds = 1.0f;
        l.underlay = true;
    }
    {
        Look &l = add("rocket", col(1.00f, 0.55f, 0.15f), 0.70f, 1.0f);
        l.palette = rocket_palette;
        l.flight = rocket_flight;
        l.tail = 1.1f;
        l.cell = rocket_cell;
        l.seconds = 1.35f;
        l.underlay = true;
    }
    {
        Look &l = add("nuke", col(0.55f, 1.00f, 0.35f), 0.40f, 1.1f);
        l.palette = nuke_palette;
        l.cell = nuke_cell;
        l.seconds = 1.95f;
        l.underlay = true;
        l.light = col(0.45f, 1.0f, 0.3f);
        l.light_peak = 0.7f;
        l.light_decay = 1.6f;
        l.light_delay = 0.3f;
        l.light_flicker_hz = 14.f;
    }
    return v;
}

const std::deque<Look> &looks()
{
    static const std::deque<Look> kLooks = build_looks();
    return kLooks;
}

//! 撃ち方の属性に対応する定義。無ければ（火炎など）`nullptr`＝汎用。
const Look *look_of(const FxSpec &spec)
{
    const char *name = (spec.style != nullptr) ? spec.style->attr : spec.attr.c_str();
    for (const Look &l : looks()) {
        if (std::strcmp(l.attr, name) == 0) {
            return &l;
        }
    }
    return nullptr;
}

//! 汎用のまま使う定義か（色だけ変えた属性）。
bool is_generic(const Look *l)
{
    return (l == nullptr) || ((l->cell == nullptr) && (l->flight == nullptr) && (l->mod == nullptr));
}

/* ============================================================ 段階の関数 */

void elemental_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    const Look *l = look_of(ctx.spec);
    if ((l == nullptr) || (l->flight == nullptr)) {
        fx_generic_flight(ctx, out);
        return;
    }
    l->flight(ctx, out);
}

void elemental_impact(const FxStyleCtx &ctx, FxFrame &out)
{
    const Look *l = look_of(ctx.spec);
    if (is_generic(l)) {
        fx_generic_impact(ctx, out);
        return;
    }
    if (l->flight != nullptr) {
        l->flight(ctx, out); // 尾の残り（本体は飛んでいる間だけ）
    } else {
        fx_emit_embers(ctx.spec, ctx.params, ctx.t, out, ctx.spec.palette, ctx.params.ember_rate, ctx.params.ember_life, ctx.fly, 1.f);
    }
    emit_hit(*l, Hit(ctx.spec, ctx.params, ctx.te), out);
}

float elemental_impact_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Look *l = look_of(spec);
    if (is_generic(l)) {
        return fx_generic_impact_seconds(spec, params);
    }
    const float tail = (l->flight != nullptr) ? l->tail : params.ember_life;
    return std::max(hit_seconds(*l, spec, params), tail);
}

void elemental_beam(const FxStyleCtx &ctx, FxFrame &out)
{
    const Look *l = look_of(ctx.spec);
    if (is_generic(l)) {
        fx_generic_beam(ctx, out);
        return;
    }
    fx_emit_beam_line(ctx.spec, ctx.params, ctx.t, out);
    const float segments = static_cast<float>((ctx.spec.route.size() > 1) ? (ctx.spec.route.size() - 1) : 0);
    const float te = ctx.t - (segments * ctx.params.beam_cell_delay);
    if (te >= 0.f) {
        emit_hit(*l, Hit(ctx.spec, ctx.params, te), out);
    }
}

float elemental_beam_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Look *l = look_of(spec);
    if (is_generic(l)) {
        return fx_generic_beam_seconds(spec, params);
    }
    const float segments = static_cast<float>((spec.route.size() > 1) ? (spec.route.size() - 1) : 0);
    return (segments * params.beam_cell_delay) + std::max(params.beam_life, hit_seconds(*l, spec, params));
}

void elemental_breath(const FxStyleCtx &ctx, FxFrame &out)
{
    const Look *l = look_of(ctx.spec);
    if (is_generic(l)) {
        fx_generic_breath(ctx, out);
        return;
    }
    emit_hit(*l, Hit(ctx.spec, ctx.params, ctx.t), out);
}

float elemental_breath_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Look *l = look_of(spec);
    return is_generic(l) ? fx_generic_breath_seconds(spec, params) : hit_seconds(*l, spec, params);
}

void elemental_burst(const FxStyleCtx &ctx, FxFrame &out)
{
    const Look *l = look_of(ctx.spec);
    if (is_generic(l)) {
        fx_generic_burst(ctx, out);
        return;
    }
    emit_hit(*l, Hit(ctx.spec, ctx.params, ctx.t), out);
}

float elemental_burst_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Look *l = look_of(spec);
    return is_generic(l) ? fx_generic_burst_seconds(spec, params) : hit_seconds(*l, spec, params);
}

//! 飛ばない魔法・足元だけ：着弾点の 1 マスに効果。視界内の全員は汎用（光の波）。
void elemental_glow(const FxStyleCtx &ctx, FxFrame &out)
{
    const Look *l = look_of(ctx.spec);
    if (is_generic(l) || (l->cell == nullptr) || (ctx.spec.glow_mode == FxGlowMode::AllInSight)) {
        fx_generic_glow(ctx, out);
        return;
    }
    emit_hit(*l, Hit(ctx.spec, ctx.params, ctx.t), out);
}

float elemental_glow_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Look *l = look_of(spec);
    if (is_generic(l) || (l->cell == nullptr) || (spec.glow_mode == FxGlowMode::AllInSight)) {
        return fx_generic_glow_seconds(spec, params);
    }
    return hit_seconds(*l, spec, params);
}

//! 火炎の色の組：試作で決めた値（`FxPalette` の既定値）をそのまま返す。
FxPalette fire_palette(const FxStyle &)
{
    FxPalette fire;
    fire.is_fire = true;
    return fire;
}

} // namespace

void fx_register_elemental(FxStyleRegistry &registry)
{
    // 火炎：今の見た目のまま（汎用の火の玉と爆発）。
    {
        FxStyle &s = registry.add("fire", col(1.00f, 0.58f, 0.14f), 0.80f, 1.0f);
        s.palette = fire_palette;
        s.flight = elemental_flight;
        s.impact = elemental_impact;
        s.impact_seconds = elemental_impact_seconds;
        s.beam = elemental_beam;
        s.beam_seconds = elemental_beam_seconds;
        s.breath = elemental_breath;
        s.breath_seconds = elemental_breath_seconds;
        s.burst = elemental_burst;
        s.burst_seconds = elemental_burst_seconds;
        s.glow = elemental_glow;
        s.glow_seconds = elemental_glow_seconds;
    }
    for (const Look &l : looks()) {
        FxStyle &s = registry.add(l.attr, l.base, l.smoke, l.emissive);
        s.palette = l.palette;
        s.flight = elemental_flight;
        s.impact = elemental_impact;
        s.impact_seconds = elemental_impact_seconds;
        s.beam = elemental_beam;
        s.beam_seconds = elemental_beam_seconds;
        s.breath = elemental_breath;
        s.breath_seconds = elemental_breath_seconds;
        s.burst = elemental_burst;
        s.burst_seconds = elemental_burst_seconds;
        s.glow = elemental_glow;
        s.glow_seconds = elemental_glow_seconds;
    }
}

} // namespace hd2d
