/*!
 * @file fx_lightning.cpp
 * @brief 稲妻の系統と光線の仲間の見た目（の分解、§5.4 の死の光線、§5.8 のシーカーレイ・スーパーレイ）。
 *
 * ## 決まっている見た目
 * ジグザグの光の線が、撃った者から対象へ一瞬で走る。着弾した所で火花が散る。毎回形が変わり、短く明滅して消える。
 *
 * | 属性 | 違い（実装） |
 * |---|---|
 * | `elec` | 基本の形。黄色がかった白の芯と淡い青白の光 |
 * | `plasma` | 電撃の形を白で |
 * | `mana` | 電撃の形を青で |
 * | `lite` | 稲妻を 4 本束ねる。`lite_weak` は 2 本で淡い |
 * | `dark` | 閃光（束）を黒で。暗い場所でも見えるよう縁にごく薄い紫の光。`dark_weak` は 2 本で淡い |
 * | `disintegrate` | グレーの光線が飛び、対象のタイルと同じ形のグレーの板が対象の表面で崩れて落ちる（`fx_emit_tile_ghost`。対象そのものは崩さない） |
 * | `death_ray` | 黒い光線。対象が黒い靄に包まれる |
 * | `seeker`・`super_ray` | 太い光線が、経路に沿って折れ曲がりながら進む |
 *
 * ## 形ごとの出し方（稲妻）
 * | 形 | 見た目 |
 * |---|---|
 * | ボルト | 撃った者から着弾点へ稲妻が走り、着弾点で火花・閃光・地面の輪 |
 * | ボール | ボルトに加えて、着弾点から効果範囲のマスへ枝分かれの稲妻が親のマスから子のマスへ広がる（範囲の外へ出さない） |
 * | ビーム | 経路に沿って太い稲妻。終点で火花 |
 * | ブレス | 撃った者から範囲のマスへ扇状に何本も走る（3 回に分けて撃つ）。範囲の全マスに火花 |
 * | ジャンプ | 真上から稲妻が落ち、着弾点から範囲へ枝分かれ |
 * | 飛ばない魔法 | 対象の上に落雷（足元だけなら小さな放電）。視界内の全員は光の波 |
 *
 * ## 作りの方針
 * - 稲妻は「一瞬で走る」ので、玉が飛ぶ時間（`orb_speed`）とは切り離し、ボルト・ボールの `flight` と `impact` の両方から
 *   同じ関数（撃ってからの秒 `ctx.t` で絵が決まる）を呼ぶ。全体の長さは `impact_seconds` で足りない分を返す。
 * - 範囲のマスの外へ粒を出さない（火花・靄・輪は `confine` で 1 マスの中へ押し込む）。
 * - 光線の仲間（`RayKind`）は稲妻と同じ関数の口に、別の描き方（尾を引いて飛ぶ光線）と別の終わり方（板が崩れる・黒い靄・火花の輪）を結ぶ。
 * - 原作より情報を増やさない：効いたか耐えたかで絵を変えない。分解の板は、対象のマスに実体がいるときだけ出す
 *   （実体の絵が引けないマスは粉塵だけ）。
 */
#include "render/fx_styles/fx_parts.h"
#include "render/fx_styles/fx_style.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hd2d {

namespace {

constexpr float kPi = 3.14159265f;

//! 光線の仲間の種類。`None` なら稲妻。
enum class RayKind : std::uint8_t {
    None,
    Disintegrate, //!< 分解
    DeathRay,     //!< 死の光線
    Seeker,       //!< シーカーレイ
    SuperRay,     //!< スーパーレイ
};

//! 属性 1 つの見た目の値。
struct Look {
    const char *attr;
    RayKind ray;
    FxColour core;  //!< 芯の色
    FxColour glow;  //!< 周りの光の色
    FxColour spark0; //!< 火花（若い）。閃光の色にも使う
    FxColour spark1; //!< 火花（老いた）
    FxColour light;  //!< 点光源の色
    float emissive;      //!< 芯の自己発光
    float glow_emissive; //!< 周りの光の自己発光
    float glow_alpha;    //!< 周りの光の不透明度
    float core_w;        //!< 稲妻 1 本の芯の太さ（マス）
    float glow_w;        //!< 周りの光の太さ
    int strands;         //!< 束ねる本数
    float power;         //!< 弱い版は 1 未満（淡く細く）
    float light_k;       //!< 点光源の強さの倍率（暗黒は弱く）
    float ray_speed;     //!< 光線の頭が進む速さ（マス/秒）
    float ray_trail;     //!< 光線の尾の長さ（マス）。0 以下なら撃った者から頭までの全長
    float ray_wave;      //!< 光線の蛇行の振れ幅（マス）
};

// clang-format off
const Look kLooks[] = {
    // attr, ray, core, glow, spark0, spark1, light, emissive, glow_emissive, glow_alpha, core_w, glow_w, strands, power, light_k, ray_speed, ray_trail, ray_wave
    { "elec", RayKind::None, { 1.00f, 0.97f, 0.78f }, { 0.50f, 0.70f, 1.00f }, { 1.00f, 0.95f, 0.62f }, { 0.55f, 0.72f, 1.00f },
        { 0.88f, 0.92f, 1.00f }, 2.2f, 2.0f, 0.55f, 0.06f, 0.26f, 1, 1.0f, 1.0f, 0.f, 0.f, 0.f },
    { "plasma", RayKind::None, { 1.00f, 1.00f, 1.00f }, { 0.84f, 0.88f, 1.00f }, { 1.00f, 1.00f, 1.00f }, { 0.78f, 0.84f, 1.00f },
        { 1.00f, 1.00f, 1.00f }, 2.8f, 2.0f, 0.50f, 0.07f, 0.26f, 1, 1.0f, 1.0f, 0.f, 0.f, 0.f },
    { "mana", RayKind::None, { 0.70f, 0.80f, 1.00f }, { 0.16f, 0.28f, 1.00f }, { 0.55f, 0.68f, 1.00f }, { 0.18f, 0.30f, 1.00f },
        { 0.40f, 0.55f, 1.00f }, 2.2f, 2.2f, 0.60f, 0.06f, 0.26f, 1, 1.0f, 1.0f, 0.f, 0.f, 0.f },
    { "lite", RayKind::None, { 1.00f, 1.00f, 0.88f }, { 1.00f, 0.93f, 0.52f }, { 1.00f, 1.00f, 0.80f }, { 1.00f, 0.82f, 0.35f },
        { 1.00f, 0.96f, 0.70f }, 2.6f, 1.8f, 0.28f, 0.035f, 0.10f, 4, 1.0f, 1.0f, 0.f, 0.f, 0.f },
    { "lite_weak", RayKind::None, { 1.00f, 1.00f, 0.88f }, { 1.00f, 0.93f, 0.52f }, { 1.00f, 1.00f, 0.80f }, { 1.00f, 0.82f, 0.35f },
        { 1.00f, 0.96f, 0.70f }, 2.4f, 1.5f, 0.24f, 0.032f, 0.09f, 2, 0.70f, 0.7f, 0.f, 0.f, 0.f },
    // 暗黒：芯は黒（自己発光 0）。縁にごく薄い紫の光を付けて、暗い場所でも黒い束が読めるようにする。
    { "dark", RayKind::None, { 0.01f, 0.00f, 0.02f }, { 0.42f, 0.14f, 0.90f }, { 0.60f, 0.24f, 1.00f }, { 0.10f, 0.00f, 0.20f },
        { 0.50f, 0.20f, 0.90f }, 0.0f, 1.0f, 0.30f, 0.045f, 0.10f, 4, 1.0f, 0.30f, 0.f, 0.f, 0.f },
    { "dark_weak", RayKind::None, { 0.01f, 0.00f, 0.02f }, { 0.42f, 0.14f, 0.90f }, { 0.60f, 0.24f, 1.00f }, { 0.10f, 0.00f, 0.20f },
        { 0.50f, 0.20f, 0.90f }, 0.0f, 0.9f, 0.24f, 0.04f, 0.09f, 2, 0.70f, 0.20f, 0.f, 0.f, 0.f },
    // 光線の仲間
    { "disintegrate", RayKind::Disintegrate, { 0.93f, 0.95f, 0.97f }, { 0.52f, 0.55f, 0.60f }, { 0.78f, 0.80f, 0.82f }, { 0.34f, 0.35f, 0.38f },
        { 0.80f, 0.84f, 0.90f }, 2.6f, 1.2f, 0.45f, 0.10f, 0.28f, 1, 1.0f, 0.8f, 40.f, 3.5f, 0.f },
    { "death_ray", RayKind::DeathRay, { 0.00f, 0.00f, 0.00f }, { 0.34f, 0.08f, 0.46f }, { 0.50f, 0.16f, 0.70f }, { 0.06f, 0.00f, 0.12f },
        { 0.45f, 0.15f, 0.65f }, 0.0f, 1.3f, 0.38f, 0.14f, 0.34f, 1, 1.0f, 0.30f, 55.f, 0.f, 0.f },
    { "seeker", RayKind::Seeker, { 0.86f, 0.96f, 1.00f }, { 0.32f, 0.62f, 1.00f }, { 0.80f, 0.94f, 1.00f }, { 0.30f, 0.60f, 1.00f },
        { 0.55f, 0.78f, 1.00f }, 3.0f, 1.8f, 0.45f, 0.18f, 0.46f, 1, 1.0f, 1.0f, 26.f, 5.f, 0.30f },
    { "super_ray", RayKind::SuperRay, { 1.00f, 0.86f, 1.00f }, { 1.00f, 0.32f, 0.88f }, { 1.00f, 0.82f, 1.00f }, { 0.95f, 0.30f, 0.85f },
        { 1.00f, 0.55f, 0.95f }, 3.4f, 2.0f, 0.48f, 0.26f, 0.64f, 1, 1.0f, 1.2f, 30.f, 7.f, 0.36f },
};
// clang-format on

const Look &look_of(const FxSpec &spec)
{
    const std::string name = (spec.style != nullptr) ? std::string(spec.style->attr) : spec.attr;
    for (const Look &l : kLooks) {
        if (name == l.attr) {
            return l;
        }
    }
    return kLooks[0];
}

/* ============================================================ 小さな道具 */

float length_of(const Vec3 &v)
{
    return std::sqrt(dot(v, v));
}

//! 単位ベクトル `d` に直角な単位ベクトルを 2 つ作る。
void perpendiculars(const Vec3 &d, Vec3 &a, Vec3 &b)
{
    const Vec3 helper = (std::fabs(d.z) < 0.9f) ? Vec3{ 0.f, 0.f, 1.f } : Vec3{ 1.f, 0.f, 0.f };
    a = normalize(cross(d, helper));
    b = normalize(cross(d, a));
}

std::uint32_t cell_seed(std::uint32_t seed, int gx, int gy)
{
    return seed + (static_cast<std::uint32_t>(gx & 0x3ff) * 1031u) + (static_cast<std::uint32_t>(gy & 0x3ff) * 7919u);
}

//! 粒の中心を、マス (gx,gy) の中へ押し込む（`from` 番目以降のボクセル）。爆発の粒を範囲のマスに収める。
void confine(FxFrame &out, std::size_t from, int gx, int gy)
{
    const float cx = static_cast<float>(gx) + 0.5f;
    const float cy = static_cast<float>(gy) + 0.5f;
    for (std::size_t i = from; i < out.voxels.size(); ++i) {
        FxVoxel &v = out.voxels[i];
        const float half = std::max(0.f, 0.5f - (std::max(v.size.x, v.size.y) * 0.5f) - 0.002f);
        v.center.x = std::min(std::max(v.center.x, cx - half), cx + half);
        v.center.y = std::min(std::max(v.center.y, cy - half), cy + half);
    }
}

//! 立ち上がり `rise` 秒、そのあと `decay` 秒で 2 乗で弱まる光の強さ（0〜1）。
float pulse(float age, float rise, float decay)
{
    if (age < 0.f) {
        return 0.f;
    }
    if (age < rise) {
        return age / std::max(rise, 1e-4f);
    }
    const float d = 1.f - ((age - rise) / std::max(decay, 1e-4f));
    return (d > 0.f) ? (d * d) : 0.f;
}

void add_light(FxFrame &out, const Vec3 &p, float radius, const FxColour &c, float intensity)
{
    if (intensity < 0.03f) {
        return;
    }
    PointLight l;
    l.x = p.x;
    l.y = p.y;
    l.z = p.z;
    l.radius = radius;
    l.r = c.r;
    l.g = c.g;
    l.b = c.b;
    l.intensity = intensity;
    out.lights.push_back(l);
}

//! 折れ線（経路）。
struct Poly {
    std::vector<Vec3> p;
    std::vector<float> cum;

    float length() const { return cum.empty() ? 0.f : cum.back(); }

    //! 始点からの道のり `d` の位置。範囲の外は両端へ丸める。
    Vec3 at(float d) const
    {
        if (p.empty()) {
            return Vec3{};
        }
        if ((p.size() == 1u) || (d <= 0.f)) {
            return p.front();
        }
        if (d >= cum.back()) {
            return p.back();
        }
        const auto it = std::upper_bound(cum.begin(), cum.end(), d);
        const std::size_t i = static_cast<std::size_t>(it - cum.begin());
        const float seg = cum[i] - cum[i - 1];
        const float k = (seg > 1e-6f) ? ((d - cum[i - 1]) / seg) : 0.f;
        return p[i - 1] + ((p[i] - p[i - 1]) * k);
    }

    //! 進む向き（正規化）。
    Vec3 dir_at(float d) const
    {
        if (p.size() < 2u) {
            return Vec3{ 1.f, 0.f, 0.f };
        }
        const float eps = 0.12f;
        const Vec3 d1 = at(d + eps) - at(d - eps);
        const float l = length_of(d1);
        return (l < 1e-5f) ? Vec3{ 1.f, 0.f, 0.f } : (d1 * (1.f / l));
    }
};

Poly make_poly(std::vector<Vec3> pts)
{
    Poly poly;
    poly.p = std::move(pts);
    poly.cum.assign(poly.p.size(), 0.f);
    for (std::size_t i = 1; i < poly.p.size(); ++i) {
        poly.cum[i] = poly.cum[i - 1] + length_of(poly.p[i] - poly.p[i - 1]);
    }
    return poly;
}

//! 撃った者から対象までの経路。経路が無い形（足元だけ・視界内の全員など）は撃った者のマスから着弾点へ。
Poly route_poly(const FxSpec &spec)
{
    if (spec.route.size() >= 2u) {
        return make_poly(spec.route);
    }
    return make_poly({ fx_cell_centre(spec.src_gx, spec.src_gy, 0.55f), fx_cell_centre(spec.centre_gx, spec.centre_gy, 0.5f) });
}

int max_ring(const FxSpec &spec)
{
    int r = 0;
    for (const FxBurstCell &c : spec.cells) {
        r = std::max(r, c.ring);
    }
    return r;
}

/* ============================================================ 稲妻の線 */

constexpr float kBoltSpeed = 70.f;   //!< 稲妻が走る速さ（マス/秒）。「一瞬で走る」
constexpr float kStrikeLife = 0.26f; //!< 稲妻 1 本が見えている長さ（秒）
constexpr float kBranchLife = 0.22f; //!< 枝の長さ
constexpr float kAreaStep = 0.04f;   //!< 範囲の距離 1 ごとの遅れ（ボール・ジャンプ）
constexpr float kFlicker = 30.f;     //!< 形が変わる頻度（回/秒）

float hit_time(float len, float speed)
{
    return std::max(len / std::max(speed, 1.f), 0.02f);
}

//! 線の太さと色。
struct Pen {
    FxColour core;
    FxColour glow;
    float core_w;
    float glow_w;
    float emissive;
    float glow_emissive;
    float glow_alpha;
    float alpha;
};

//! 折れ線を `reveal`（始点からの道のり）まで描く。足したボクセルの数を返す。
int emit_segments(const std::vector<Vec3> &pts, const Pen &pen, float reveal, bool joints, FxFrame &out)
{
    int emitted = 0;
    float travelled = 0.f;
    for (std::size_t i = 0; (i + 1) < pts.size(); ++i) {
        const Vec3 d = pts[i + 1] - pts[i];
        const float len = length_of(d);
        if (len < 1e-4f) {
            continue;
        }
        if (travelled >= reveal) {
            break;
        }
        const float use = std::min(len, reveal - travelled);
        const Vec3 dir = d * (1.f / len);
        const Vec3 mid = pts[i] + (dir * (use * 0.5f));
        float yaw = 0.f;
        float pitch = 0.f;
        fx_direction_angles(dir, yaw, pitch);
        FxVoxel g;
        g.center = mid;
        g.size = Vec3{ use * 1.08f, pen.glow_w, pen.glow_w };
        g.r = pen.glow.r;
        g.g = pen.glow.g;
        g.b = pen.glow.b;
        g.alpha = pen.glow_alpha * pen.alpha;
        g.emissive = pen.glow_emissive;
        g.yaw = yaw;
        g.pitch = pitch;
        g.part = FxPart::BoltGlow;
        out.voxels.push_back(g);
        FxVoxel c = g;
        c.size = Vec3{ use * 1.04f, pen.core_w, pen.core_w };
        c.r = pen.core.r;
        c.g = pen.core.g;
        c.b = pen.core.b;
        c.alpha = pen.alpha;
        c.emissive = pen.emissive;
        c.part = FxPart::Bolt;
        out.voxels.push_back(c);
        emitted += 2;
        // 折れ目のすきまを埋める小さな立方体（太い線だけ）。
        if (joints && (i > 0) && (travelled <= reveal)) {
            FxVoxel j = c;
            j.center = pts[i];
            j.size = Vec3{ pen.core_w * 1.2f, pen.core_w * 1.2f, pen.core_w * 1.2f };
            j.yaw = 0.f;
            j.pitch = 0.f;
            out.voxels.push_back(j);
            ++emitted;
        }
        travelled += len;
    }
    return emitted;
}

Pen pen_of(const Look &l, float width, float alpha)
{
    Pen p;
    const float wk = width * (0.75f + (0.25f * l.power));
    p.core = l.core;
    p.glow = l.glow;
    p.core_w = l.core_w * wk;
    p.glow_w = l.glow_w * wk;
    p.emissive = l.emissive;
    p.glow_emissive = l.glow_emissive;
    p.glow_alpha = l.glow_alpha;
    p.alpha = alpha * l.power;
    return p;
}

/*!
 * @brief 稲妻 1 回（束ねる本数ぶん）を `a` から `b` へ。`age` は走り始めてからの秒、`life` は見えている長さ。
 * @details 形は毎秒 `kFlicker` 回変わり、ときどき暗くなって明滅する。`reveal_speed` で端から走る。
 */
int emit_bolt(const Look &l, const Vec3 &a, const Vec3 &b, std::uint32_t seed, float age, float life, float width, float jitter_scale,
    int strands, float reveal_speed, bool branches, FxFrame &out)
{
    if ((age < 0.f) || (age >= life)) {
        return 0;
    }
    const Vec3 span = b - a;
    const float len = length_of(span);
    if (len < 1e-3f) {
        return 0;
    }
    const std::uint32_t frame = static_cast<std::uint32_t>(std::floor(age * kFlicker));
    const float bright = (fx_rand(seed, frame, 91u) < 0.18f) ? 0.55f : 1.f;
    const float hold = life * 0.5f;
    const float env = (age < hold) ? 1.f : (1.f - ((age - hold) / std::max(life - hold, 1e-4f)));
    Pen pen = pen_of(l, width, env * bright);
    const int segs = std::min(std::max(static_cast<int>(std::lround(len * 1.5f)), 3), 14);
    const float jitter = std::min(std::max(0.14f + (0.035f * len), 0.14f), 0.40f) * jitter_scale;
    const float reveal = age * reveal_speed;
    const bool joints = pen.core_w >= 0.07f;
    int emitted = 0;
    Vec3 n1{ 0.f, 0.f, 1.f };
    Vec3 n2{ 1.f, 0.f, 0.f };
    const Vec3 unit = span * (1.f / len);
    perpendiculars(unit, n1, n2);
    for (int k = 0; k < strands; ++k) {
        const std::uint32_t s = seed + (977u * static_cast<std::uint32_t>(k)) + 11u;
        const std::vector<Vec3> pts = fx_zigzag_points(a, b, segs, jitter, s, frame);
        emitted += emit_segments(pts, pen, reveal, joints, out);
        if (branches && (k == 0) && (pts.size() >= 4u) && (len > 1.2f)) {
            const int count = (len > 4.f) ? 2 : 1;
            for (int j = 0; j < count; ++j) {
                const std::uint32_t id = static_cast<std::uint32_t>(100 + j);
                const std::size_t kv = 1u + static_cast<std::size_t>(fx_rand(seed, id, frame, 1u) * static_cast<float>(pts.size() - 3u));
                if ((static_cast<float>(kv) / static_cast<float>(segs)) * len > reveal) {
                    continue;
                }
                const float ang = 2.f * kPi * fx_rand(seed, id, frame, 2u);
                const Vec3 side = (n1 * std::cos(ang)) + (n2 * std::sin(ang));
                const Vec3 end = pts[kv] + (side * std::min(len * 0.16f, 0.9f)) + (unit * std::min(len * 0.10f, 0.6f));
                const std::vector<Vec3> bp = fx_zigzag_points(pts[kv], end, 3, jitter * 0.5f, s + 7u, frame + id);
                Pen bpen = pen;
                bpen.core_w *= 0.65f;
                bpen.glow_w *= 0.6f;
                bpen.alpha *= 0.9f;
                emitted += emit_segments(bp, bpen, 1e9f, false, out);
            }
        }
    }
    return emitted;
}

/*!
 * @brief 経路（折れ線）に沿った稲妻。経路の 1 マスごとに 3 つに折って、毎回形が変わる。
 */
int emit_bolt_along(const Look &l, const Poly &poly, std::uint32_t seed, float age, float life, float width, int strands, float reveal_speed,
    FxFrame &out)
{
    if ((age < 0.f) || (age >= life) || (poly.p.size() < 2u)) {
        return 0;
    }
    const std::uint32_t frame = static_cast<std::uint32_t>(std::floor(age * kFlicker));
    const float bright = (fx_rand(seed, frame, 91u) < 0.18f) ? 0.55f : 1.f;
    const float hold = life * 0.5f;
    const float env = (age < hold) ? 1.f : (1.f - ((age - hold) / std::max(life - hold, 1e-4f)));
    Pen pen = pen_of(l, width, env * bright);
    const float reveal = age * reveal_speed;
    const bool joints = pen.core_w >= 0.07f;
    int emitted = 0;
    for (int k = 0; k < strands; ++k) {
        const std::uint32_t s = seed + (977u * static_cast<std::uint32_t>(k)) + 11u;
        std::vector<Vec3> pts;
        for (std::size_t i = 0; (i + 1) < poly.p.size(); ++i) {
            const std::vector<Vec3> part = fx_zigzag_points(poly.p[i], poly.p[i + 1], (strands > 1) ? 3 : 4, (strands > 1) ? 0.30f : 0.36f, s + static_cast<std::uint32_t>(i), frame);
            pts.insert(pts.end(), part.begin() + ((i == 0) ? 0 : 1), part.end());
        }
        emitted += emit_segments(pts, pen, reveal, joints, out);
    }
    return emitted;
}

/* ============================================================ 着弾 */

//! 火花（マス (gx,gy) の中に収める）。
void emit_sparks(const Look &l, const Vec3 &origin, int gx, int gy, float born, float t, int count, float scale, std::uint32_t seed,
    FxFrame &out)
{
    FxSpray s;
    s.origin = origin;
    s.dir = Vec3{ 0.f, 0.f, 1.f };
    s.cone = 1.7f;
    s.scatter = 0.05f;
    s.speed_min = 0.6f * scale;
    s.speed_max = 2.0f * scale;
    s.gravity = -7.f;
    s.drag = 2.0f;
    s.born = born;
    s.life_min = 0.20f;
    s.life_max = 0.45f;
    s.count = count;
    s.colour0 = l.spark0;
    s.colour1 = l.spark1;
    s.size0 = 0.12f;
    s.size1 = 0.03f;
    s.emissive0 = 3.0f;
    s.emissive1 = 0.6f;
    s.alpha0 = 1.f;
    s.alpha1 = 0.f;
    s.floor = FxFloor::Vanish;
    s.floor_z = 0.f;
    s.part = FxPart::Spark;
    s.seed = seed;
    const std::size_t from = out.voxels.size();
    (void)fx_emit_spray(s, t, out);
    confine(out, from, gx, gy);
}

//! 着弾の閃光（小さな核）と地面の輪。マスの中に収める。
void emit_hit_flash(const Look &l, int gx, int gy, float z, float born, float t, float scale, FxFrame &out)
{
    const float age = t - born;
    if (age < 0.f) {
        return;
    }
    const Vec3 c = fx_cell_centre(gx, gy, z);
    const std::size_t from = out.voxels.size();
    if (age < 0.14f) {
        const float u = age / 0.14f;
        const float k = 1.f - (u * u);
        FxVoxel v;
        v.center = c;
        const float size = 0.30f * scale * k;
        v.size = Vec3{ size, size, size };
        v.r = l.spark0.r;
        v.g = l.spark0.g;
        v.b = l.spark0.b;
        v.emissive = 4.0f;
        v.part = FxPart::Spark;
        if (size > 0.01f) {
            out.voxels.push_back(v);
        }
    }
    FxRing ring;
    ring.centre = fx_cell_centre(gx, gy, 0.05f);
    ring.r0 = 0.08f;
    ring.r1 = 0.40f * scale;
    ring.born = born;
    ring.life = 0.26f;
    ring.thickness = 0.04f;
    ring.segments = 20;
    ring.colour = l.glow;
    ring.emissive = 2.2f;
    ring.alpha = 0.9f * l.power;
    (void)fx_emit_ring(ring, t, out);
    confine(out, from, gx, gy);
}

//! 範囲のマスの床を、短く光らせる（範囲の広がりが読めるように）。
void emit_cell_glow(const Look &l, int gx, int gy, float born, float t, float life, float strength, FxFrame &out)
{
    const float age = t - born;
    if ((age < 0.f) || (age >= life)) {
        return;
    }
    const float u = age / life;
    FxVoxel v;
    v.center = fx_cell_centre(gx, gy, 0.02f);
    v.size = Vec3{ 0.84f, 0.84f, 0.03f };
    v.r = l.glow.r;
    v.g = l.glow.g;
    v.b = l.glow.b;
    v.alpha = 0.22f * strength * (1.f - u) * l.power;
    v.emissive = (1.4f * (1.f - u)) + 0.2f;
    v.part = FxPart::BoltGlow;
    if (v.alpha > 0.01f) {
        out.voxels.push_back(v);
    }
}

/* ============================================================ 光線 */

//! 撃った者から `b` への直線の光線に使う折れ線。
Poly straight_poly(const Vec3 &a, const Vec3 &b)
{
    return make_poly({ a, b });
}

//! 光線の頭がいま進んでいる所まで、尾を引く光線を描く。足したボクセルの数を返す。
int emit_ray(const Look &l, const Poly &poly, std::uint32_t seed, float age, float width, float speed, FxFrame &out)
{
    const float len = poly.length();
    if ((age < 0.f) || (len < 1e-3f)) {
        return 0;
    }
    const bool full = l.ray_trail <= 0.f;
    const float t_arr = len / speed;
    const float head = age * speed;
    float fade = 1.f;
    float tail = 0.f;
    if (full) {
        fade = 1.f - fx_ramp(age, t_arr + 0.08f, t_arr + 0.30f);
    } else {
        tail = std::max(0.f, head - l.ray_trail);
    }
    if ((fade <= 0.01f) || (tail >= len)) {
        return 0;
    }
    const float vis_head = std::min(head, len);
    const float span = vis_head - tail;
    if (span < 1e-3f) {
        return 0;
    }
    const int n = std::min(std::max(static_cast<int>(std::ceil(span / 0.45f)), 1), 64);
    const float phase = fx_rand(seed, 71u) * 2.f * kPi;
    const auto offset_at = [&](float d) {
        if (l.ray_wave <= 0.f) {
            return Vec3{ 0.f, 0.f, 0.f };
        }
        Vec3 s1;
        Vec3 s2;
        perpendiculars(poly.dir_at(d), s1, s2);
        const float taper = std::max(0.f, std::min({ 1.f, d / 0.9f, (len - d) / 0.9f }));
        const float ph = (d * 2.0f) - (age * 14.f) + phase;
        return (s1 * (std::sin(ph) * l.ray_wave * taper)) + (s2 * (std::cos(ph * 0.8f) * l.ray_wave * 0.45f * taper));
    };
    std::vector<Vec3> pts;
    pts.reserve(static_cast<std::size_t>(n) + 1u);
    for (int j = 0; j <= n; ++j) {
        const float d = tail + (span * static_cast<float>(j) / static_cast<float>(n));
        pts.push_back(poly.at(d) + offset_at(d));
    }
    int emitted = 0;
    for (int j = 0; j < n; ++j) {
        const float dmid = tail + (span * (static_cast<float>(j) + 0.5f) / static_cast<float>(n));
        float f = 1.f;
        if (!full) {
            f = std::min(std::max((dmid - (head - l.ray_trail)) / l.ray_trail, 0.f), 1.f);
        } else {
            f = 0.6f + (0.4f * dmid / len);
        }
        const Vec3 d = pts[static_cast<std::size_t>(j) + 1u] - pts[static_cast<std::size_t>(j)];
        const float dl = length_of(d);
        if (dl < 1e-4f) {
            continue;
        }
        float yaw = 0.f;
        float pitch = 0.f;
        fx_direction_angles(d * (1.f / dl), yaw, pitch);
        const Vec3 mid = pts[static_cast<std::size_t>(j)] + (d * 0.5f);
        const float shimmer = 0.9f + (0.2f * fx_rand(seed, static_cast<std::uint32_t>(j), static_cast<std::uint32_t>(std::floor(age * kFlicker)), 72u));
        const float w = width * (0.45f + (0.55f * f)) * shimmer;
        FxVoxel g;
        g.center = mid;
        g.size = Vec3{ dl * 1.02f, l.glow_w * w, l.glow_w * w };
        g.r = l.glow.r;
        g.g = l.glow.g;
        g.b = l.glow.b;
        g.alpha = l.glow_alpha * fade * f * l.power;
        g.emissive = l.glow_emissive;
        g.yaw = yaw;
        g.pitch = pitch;
        g.part = FxPart::BoltGlow;
        out.voxels.push_back(g);
        FxVoxel c = g;
        c.size = Vec3{ dl * 1.02f, l.core_w * w, l.core_w * w };
        c.r = l.core.r;
        c.g = l.core.g;
        c.b = l.core.b;
        c.alpha = (fade >= 0.999f && f >= 0.7f) ? 1.f : (fade * std::min(1.f, 0.3f + f));
        c.emissive = l.emissive;
        c.part = FxPart::Bolt;
        out.voxels.push_back(c);
        emitted += 2;
    }
    // 頭：まだ着いていない間、明るい核と周りの光。
    if (head <= (len + 0.15f)) {
        const Vec3 hp = pts.back();
        FxVoxel hv;
        hv.center = hp;
        const float hs = l.core_w * width * 2.0f;
        hv.size = Vec3{ hs, hs, hs };
        hv.r = l.core.r;
        hv.g = l.core.g;
        hv.b = l.core.b;
        hv.emissive = l.emissive * 1.3f;
        hv.part = FxPart::Bolt;
        out.voxels.push_back(hv);
        FxVoxel hg = hv;
        const float gs = l.glow_w * width * 1.5f;
        hg.size = Vec3{ gs, gs, gs };
        hg.r = l.glow.r;
        hg.g = l.glow.g;
        hg.b = l.glow.b;
        hg.alpha = 0.40f * l.power;
        hg.emissive = l.glow_emissive;
        hg.part = FxPart::BoltGlow;
        out.voxels.push_back(hg);
        emitted += 2;
    }
    // 頭に付いて動く点光源。
    add_light(out, Vec3{ pts.back().x, pts.back().y, 0.7f }, 3.0f, l.light, 1.8f * l.light_k * fade);
    return emitted;
}

//! 光線の頭が着いてから、尾が消えるまでの秒（撃ってから）。
float ray_end(const Look &l, float len)
{
    if (l.ray_trail <= 0.f) {
        return (len / l.ray_speed) + 0.30f;
    }
    return (len + l.ray_trail) / l.ray_speed;
}

/* ============================================================ 光線の終わり方 */

bool entity_at(const FxSpec &spec, int gx, int gy)
{
    FxTileImage img;
    return spec.tile_at && spec.tile_at(gx, gy, img) && (img.width > 0) && (img.height > 0);
}

//! 終わり方の長さ（秒）。
float terminal_life(const Look &l)
{
    switch (l.ray) {
    case RayKind::None:
        return 0.45f;
    case RayKind::Disintegrate:
        return 1.15f;
    case RayKind::DeathRay:
        return 1.55f;
    case RayKind::Seeker:
        return 0.55f;
    case RayKind::SuperRay:
        return 0.70f;
    }
    return 0.5f;
}

/*!
 * @brief マス (gx,gy) に光が着いたあとの絵。`born` は着いた時刻、`primary` は中心（対象）のマスか。
 * @details 稲妻は火花・閃光・床の輪、分解はグレーの板が崩れる、死の光線は黒い靄、シーカーレイ・スーパーレイは火花の輪。
 */
void emit_terminal(const Look &l, const FxSpec &spec, int gx, int gy, float born, float t, bool primary, FxFrame &out)
{
    const float age = t - born;
    if ((age < 0.f) || (age >= terminal_life(l))) {
        return;
    }
    const std::uint32_t seed = cell_seed(spec.seed, gx, gy);
    const Vec3 chest = fx_cell_centre(gx, gy, 0.5f);
    switch (l.ray) {
    case RayKind::None:
        emit_cell_glow(l, gx, gy, born, t, 0.34f, primary ? 1.f : 0.7f, out);
        emit_sparks(l, chest, gx, gy, born, t, primary ? 18 : 6, primary ? 1.f : 0.7f, seed, out);
        if (primary) {
            emit_hit_flash(l, gx, gy, 0.5f, born, t, 1.f, out);
        }
        break;
    case RayKind::Disintegrate: {
        // 粉塵（グレーの欠片）。
        FxSpray s;
        s.origin = fx_cell_centre(gx, gy, 0.45f);
        s.cone = 1.4f;
        s.scatter = 0.12f;
        s.speed_min = 0.3f;
        s.speed_max = 1.1f;
        s.gravity = -5.f;
        s.drag = 1.8f;
        s.born = born;
        s.life_min = 0.35f;
        s.life_max = 0.80f;
        s.count = primary ? 10 : 5;
        s.colour0 = l.spark0;
        s.colour1 = l.spark1;
        s.size0 = 0.08f;
        s.size1 = 0.03f;
        s.emissive0 = 0.6f;
        s.emissive1 = 0.f;
        s.floor = FxFloor::Stop;
        s.floor_z = 0.f;
        s.part = FxPart::Debris;
        s.seed = seed;
        const std::size_t from = out.voxels.size();
        (void)fx_emit_spray(s, t, out);
        confine(out, from, gx, gy);
        if (primary) {
            emit_hit_flash(l, gx, gy, 0.5f, born, t, 0.8f, out);
        }
        // 対象のタイルと同じ形のグレーの板が、表面で崩れて落ちる。実体がいるマスだけ（いなければ粉塵だけ）。
        if (entity_at(spec, gx, gy)) {
            FxGhost g;
            g.gx = gx;
            g.gy = gy;
            g.colour = FxColour{ 0.74f, 0.76f, 0.80f };
            g.tint = 1.0f;
            g.grid = primary ? 16 : 8;
            g.offset = 0.35f;
            g.born = born;
            g.hold = 0.10f;
            g.crumble = 0.45f;
            g.fall_life = 0.50f;
            g.emissive = 0.55f;
            g.seed = seed;
            (void)fx_emit_tile_ghost(spec, g, t, out);
        }
        break;
    }
    case RayKind::DeathRay: {
        // 黒い靄が対象を包む。
        FxSmoke sm;
        sm.origin = fx_cell_centre(gx, gy, 0.10f);
        sm.radius = 0.26f;
        sm.born = born;
        sm.duration = 0.45f;
        sm.life = 1.05f;
        sm.count = primary ? 16 : 5;
        sm.rise = 0.45f;
        sm.size = 0.22f;
        sm.grow = 1.9f;
        sm.colour = FxColour{ 0.03f, 0.00f, 0.05f };
        sm.colour_end = FxColour{ 0.12f, 0.03f, 0.20f };
        sm.alpha = 0.88f;
        sm.emissive = 1.0f;
        sm.seed = seed;
        const std::size_t from = out.voxels.size();
        (void)fx_emit_smoke(sm, t, out);
        confine(out, from, gx, gy);
        emit_sparks(l, chest, gx, gy, born, t, primary ? 6 : 2, 0.7f, seed + 5u, out);
        break;
    }
    case RayKind::Seeker:
    case RayKind::SuperRay: {
        const bool super = l.ray == RayKind::SuperRay;
        emit_sparks(l, chest, gx, gy, born, t, primary ? (super ? 16 : 11) : 4, 1.f, seed, out);
        if (primary) {
            emit_hit_flash(l, gx, gy, 0.5f, born, t, super ? 1.25f : 1.f, out);
            if (super) {
                emit_hit_flash(l, gx, gy, 0.5f, born + 0.07f, t, 0.9f, out);
            }
        }
        emit_cell_glow(l, gx, gy, born, t, 0.40f, primary ? 1.f : 0.6f, out);
        break;
    }
    }
}

/* ============================================================ 範囲（ボール・ジャンプ） */

float area_born(float th, int ring)
{
    return th + ((ring > 0) ? 0.03f : 0.f) + (static_cast<float>(ring) * kAreaStep);
}

float area_end(const Look &l, const FxSpec &spec, float th)
{
    return area_born(th, max_ring(spec)) + terminal_life(l);
}

/*!
 * @brief 範囲のマスの全部に、着いたあとの絵を出す。稲妻はさらに、親のマスから子のマスへ枝分かれの稲妻を走らせる。
 * @param hub 枝の根元（着弾点）。`th` は中心のマスに光が着く時刻。
 */
void emit_area(const Look &l, const FxSpec &spec, const Vec3 &hub, float th, float t, FxFrame &out)
{
    const std::vector<FxBurstCell> &cells = spec.cells;
    const std::size_t n = cells.size();
    if (n == 0u) {
        emit_terminal(l, spec, spec.centre_gx, spec.centre_gy, th, t, true, out);
        return;
    }
    const bool dense = n > 60u;
    for (std::size_t i = 0; i < n; ++i) {
        const FxBurstCell &c = cells[i];
        const float born = area_born(th, c.ring);
        if (t < born) {
            continue;
        }
        emit_terminal(l, spec, c.gx, c.gy, born, t, c.ring == 0, out);
        if ((l.ray != RayKind::None) || (c.ring == 0)) {
            continue;
        }
        if (dense && (fx_rand(spec.seed, static_cast<std::uint32_t>(c.gx), static_cast<std::uint32_t>(c.gy), 5u) > (60.f / static_cast<float>(n)))) {
            continue;
        }
        // 親：自分より中心に近いマスのうち、いちばん近いもの。
        Vec3 from = hub;
        float best = 1e9f;
        for (std::size_t j = 0; j < n; ++j) {
            if (cells[j].ring >= c.ring) {
                continue;
            }
            const float dx = static_cast<float>(cells[j].gx - c.gx);
            const float dy = static_cast<float>(cells[j].gy - c.gy);
            const float d2 = (dx * dx) + (dy * dy);
            if (d2 < best) {
                best = d2;
                from = (cells[j].ring == 0) ? hub : fx_cell_centre(cells[j].gx, cells[j].gy, 0.42f);
            }
        }
        const Vec3 to = fx_cell_centre(c.gx, c.gy, 0.35f + (0.15f * fx_rand(spec.seed, static_cast<std::uint32_t>(c.gx), static_cast<std::uint32_t>(c.gy), 6u)));
        emit_bolt(l, from, to, cell_seed(spec.seed, c.gx, c.gy) + 3u, t - born, kBranchLife, 0.7f, 0.5f, 1, 40.f, false, out);
    }
}

/* ============================================================ 形ごとの全体 */

//! 稲妻の「走る」光（点光源）。中ほどと着弾点。
void strike_lights(const Look &l, const Poly &poly, float th, float t, float reach, FxFrame &out)
{
    const Vec3 a = poly.p.front();
    const Vec3 b = poly.p.back();
    const float len = poly.length();
    const float k = l.light_k * l.power;
    add_light(out, Vec3{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, 1.0f }, std::min(6.f, 2.8f + (0.3f * len)), l.light,
        2.4f * k * pulse(t, 0.03f, 0.26f));
    add_light(out, Vec3{ b.x, b.y, 0.9f }, 2.5f + reach, l.light, std::min(6.5f, 1.8f + (0.7f * reach)) * l.light_k * pulse(t - th, 0.03f, 0.40f));
}

void draw_orb(const Look &l, const FxSpec &spec, float t, FxFrame &out)
{
    const Poly poly = route_poly(spec);
    const Vec3 a = poly.p.front();
    const Vec3 b = poly.p.back();
    const float len = poly.length();
    const float reach = spec.explode ? static_cast<float>(max_ring(spec)) : 0.f;
    if (l.ray == RayKind::None) {
        const float th = hit_time(len, kBoltSpeed);
        (void)emit_bolt(l, a, b, spec.seed, t, kStrikeLife, 1.f, 1.f, l.strands, kBoltSpeed, true, out);
        emit_area(l, spec, b, th, t, out);
        strike_lights(l, poly, th, t, reach, out);
    } else {
        const float th = hit_time(len, l.ray_speed);
        const float width = (l.ray == RayKind::SuperRay) ? 0.8f : 0.7f;
        (void)emit_ray(l, poly, spec.seed, t, width, l.ray_speed, out);
        emit_area(l, spec, b, th, t, out);
        add_light(out, Vec3{ b.x, b.y, 0.9f }, 2.5f + reach, l.light, std::min(5.f, 1.6f + (0.6f * reach)) * l.light_k * pulse(t - th, 0.03f, 0.40f));
    }
}

float total_orb(const Look &l, const FxSpec &spec)
{
    const float len = route_poly(spec).length();
    if (l.ray == RayKind::None) {
        const float th = hit_time(len, kBoltSpeed);
        return std::max(kStrikeLife, area_end(l, spec, th));
    }
    const float th = hit_time(len, l.ray_speed);
    return std::max(ray_end(l, len), area_end(l, spec, th));
}

void draw_beam(const Look &l, const FxSpec &spec, float t, FxFrame &out)
{
    const Poly poly = route_poly(spec);
    const Vec3 b = poly.p.back();
    const float len = poly.length();
    if (l.ray == RayKind::None) {
        const float th = hit_time(len, kBoltSpeed);
        (void)emit_bolt_along(l, poly, spec.seed, t, kStrikeLife + 0.04f, (l.strands > 1) ? 1.f : 1.25f, l.strands, kBoltSpeed, out);
        emit_terminal(l, spec, spec.centre_gx, spec.centre_gy, th, t, true, out);
        const float k = l.light_k * l.power;
        const Vec3 mid = poly.at(len * 0.5f);
        add_light(out, Vec3{ mid.x, mid.y, 1.0f }, std::min(7.f, 2.8f + (0.3f * len)), l.light, 2.4f * k * pulse(t, 0.03f, 0.30f));
        add_light(out, Vec3{ b.x, b.y, 0.9f }, 2.5f, l.light, 1.8f * k * pulse(t - th, 0.03f, 0.40f));
    } else {
        const float th = hit_time(len, l.ray_speed);
        const float width = (l.ray == RayKind::SuperRay) ? 1.0f : 0.9f;
        (void)emit_ray(l, poly, spec.seed, t, width, l.ray_speed, out);
        emit_terminal(l, spec, spec.centre_gx, spec.centre_gy, th, t, true, out);
        add_light(out, Vec3{ b.x, b.y, 0.9f }, 2.5f, l.light, 1.6f * l.light_k * pulse(t - th, 0.03f, 0.40f));
    }
}

float total_beam(const Look &l, const FxSpec &spec)
{
    const float len = route_poly(spec).length();
    if (l.ray == RayKind::None) {
        const float th = hit_time(len, kBoltSpeed);
        return std::max(kStrikeLife + 0.04f, th + terminal_life(l));
    }
    const float th = hit_time(len, l.ray_speed);
    return std::max(ray_end(l, len), th + terminal_life(l));
}

/* ---- ブレス：撃った者から範囲のマスへ扇状に何本も走る ---- */

constexpr int kBreathVolleys = 3;
constexpr float kBreathVolleyStep = 0.15f;
constexpr float kBreathBoltLife = 0.22f;
constexpr float kBreathRingStep = 0.03f;

float breath_born(int ring)
{
    return 0.05f + (static_cast<float>(ring) * kBreathRingStep);
}

//! 稲妻を向ける先のマス（遠いマスを優先して `count` 個。波ごとに選び直す）。
std::vector<std::size_t> pick_tips(const FxSpec &spec, std::size_t count, std::uint32_t salt)
{
    const std::size_t n = spec.cells.size();
    const int farthest = std::max(max_ring(spec), 1);
    std::vector<std::pair<float, std::size_t>> scored;
    scored.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const FxBurstCell &c = spec.cells[i];
        const float score = (0.6f * fx_rand(spec.seed, static_cast<std::uint32_t>(c.gx), static_cast<std::uint32_t>(c.gy), 20u + salt))
            + (0.8f * static_cast<float>(c.ring) / static_cast<float>(farthest));
        scored.emplace_back(-score, i);
    }
    const std::size_t take = std::min(count, n);
    std::partial_sort(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(take), scored.end());
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < take; ++i) {
        result.push_back(scored[i].second);
    }
    return result;
}

void draw_breath(const Look &l, const FxSpec &spec, float t, FxFrame &out)
{
    const Vec3 src = fx_cell_centre(spec.src_gx, spec.src_gy, 0.6f);
    const bool ray = l.ray != RayKind::None;
    const int volleys = ray ? 2 : kBreathVolleys;
    const std::size_t tips = ray ? 5u : ((l.strands >= 4) ? 6u : 8u);
    for (int v = 0; v < volleys; ++v) {
        const std::vector<std::size_t> picks = pick_tips(spec, tips, static_cast<std::uint32_t>(v) * 7u);
        for (std::size_t j = 0; j < picks.size(); ++j) {
            const FxBurstCell &c = spec.cells[picks[j]];
            const float born = (static_cast<float>(v) * kBreathVolleyStep) + (0.06f * fx_rand(spec.seed, static_cast<std::uint32_t>(v), static_cast<std::uint32_t>(j), 30u));
            const float age = t - born;
            if (age < 0.f) {
                continue;
            }
            const std::uint32_t id = (static_cast<std::uint32_t>(v) * 31u) + static_cast<std::uint32_t>(j);
            const Vec3 target = fx_cell_centre(c.gx, c.gy, 0.5f)
                + Vec3{ (fx_rand(spec.seed, id, 31u) - 0.5f) * 0.4f, (fx_rand(spec.seed, id, 32u) - 0.5f) * 0.4f, 0.f };
            if (!ray) {
                (void)emit_bolt(l, src, target, spec.seed + (id * 131u), age, kBreathBoltLife, 0.85f, 1.f, std::min(l.strands, 2), 50.f, false, out);
            } else {
                (void)emit_ray(l, straight_poly(src, target), spec.seed + (id * 131u), age, 0.6f, l.ray_speed, out);
            }
        }
    }
    int farthest = 0;
    float sx = 0.f;
    float sy = 0.f;
    for (const FxBurstCell &c : spec.cells) {
        farthest = std::max(farthest, c.ring);
        sx += static_cast<float>(c.gx) + 0.5f;
        sy += static_cast<float>(c.gy) + 0.5f;
        const float born = breath_born(c.ring) + (ray ? (static_cast<float>(c.ring) / l.ray_speed) : 0.f);
        emit_terminal(l, spec, c.gx, c.gy, born, t, false, out);
    }
    if (!spec.cells.empty()) {
        const float cnt = static_cast<float>(spec.cells.size());
        add_light(out, Vec3{ sx / cnt, sy / cnt, 1.2f }, 3.f + (0.5f * static_cast<float>(farthest)), l.light,
            std::min(5.5f, 2.0f + (0.4f * static_cast<float>(farthest))) * l.light_k * l.power * pulse(t, 0.05f, 0.55f));
    }
}

float total_breath(const Look &l, const FxSpec &spec)
{
    const int farthest = max_ring(spec);
    const bool ray = l.ray != RayKind::None;
    float end = 0.f;
    if (!ray) {
        end = (static_cast<float>(kBreathVolleys - 1) * kBreathVolleyStep) + 0.06f + kBreathBoltLife;
    } else {
        end = kBreathVolleyStep + 0.06f + ray_end(l, static_cast<float>(farthest) + 1.5f);
    }
    const float terminal = breath_born(farthest) + (ray ? (static_cast<float>(farthest) / l.ray_speed) : 0.f) + terminal_life(l);
    return std::max(end, terminal);
}

/* ---- ジャンプ：その場に落ちる ---- */

void draw_burst(const Look &l, const FxSpec &spec, float t, FxFrame &out)
{
    const Vec3 hub = fx_cell_centre(spec.centre_gx, spec.centre_gy, 0.5f);
    const float th = 0.05f;
    if (l.ray == RayKind::None) {
        const Vec3 sky = fx_cell_centre(spec.centre_gx, spec.centre_gy, 2.6f) + Vec3{ 0.18f, 0.f, 0.f };
        (void)emit_bolt(l, sky, hub, spec.seed, t, kStrikeLife, 1.f, 1.f, l.strands, 60.f, true, out);
    }
    emit_area(l, spec, hub, th, t, out);
    const float reach = static_cast<float>(max_ring(spec));
    add_light(out, Vec3{ hub.x, hub.y, 1.0f }, 2.5f + reach, l.light, std::min(6.5f, 1.8f + (0.7f * reach)) * l.light_k * pulse(t - th, 0.03f, 0.40f));
}

float total_burst(const Look &l, const FxSpec &spec)
{
    const float th = 0.05f;
    return std::max((l.ray == RayKind::None) ? kStrikeLife : 0.f, area_end(l, spec, th));
}

/* ---- 飛ばない魔法 ---- */

/*!
 * @brief 対象の上で効果を出す魔法・足元だけに効く魔法・視界内の全員にかかる魔法。
 * @details 視界内の全員にかかる魔法は土台の光の波（`fx_generic_glow`）に任せる。それ以外は汎用の小さな光を出さず、
 * 稲妻の放電（対象の上に短い落雷・足元の小さな放電）や光線の終わり方と、対象の上の点光源 1 つだけで描く。
 */
void draw_glow(const FxStyleCtx &ctx, FxFrame &out)
{
    const FxSpec &spec = ctx.spec;
    const Look &l = look_of(spec);
    const float t = ctx.t;
    if (spec.glow_mode == FxGlowMode::AllInSight) {
        fx_generic_glow(ctx, out);
        return;
    }
    add_light(out, fx_cell_centre(spec.centre_gx, spec.centre_gy, 0.9f), 2.5f, l.light, 1.6f * l.light_k * l.power * pulse(t, 0.03f, 0.40f));
    if (l.ray != RayKind::None) {
        emit_terminal(l, spec, spec.centre_gx, spec.centre_gy, 0.f, t, true, out);
        return;
    }
    const Vec3 o = fx_cell_centre(spec.centre_gx, spec.centre_gy, 0.5f);
    if (spec.glow_mode == FxGlowMode::Target) {
        // 対象の上に短い落雷。
        const Vec3 sky = fx_cell_centre(spec.centre_gx, spec.centre_gy, 2.0f) + Vec3{ 0.15f, 0.f, 0.f };
        (void)emit_bolt(l, sky, o, spec.seed, t, 0.14f, 0.9f, 0.9f, std::min(l.strands, 2), 60.f, true, out);
    } else {
        // 足元だけ：マスの中で小さな放電が四方に走る。
        for (int i = 0; i < 4; ++i) {
            const float born = 0.06f * static_cast<float>(i);
            const float ang = (2.f * kPi * ((static_cast<float>(i) + (0.6f * fx_rand(spec.seed, static_cast<std::uint32_t>(i), 40u))) / 4.f));
            const Vec3 end = o + Vec3{ std::cos(ang) * 0.38f, std::sin(ang) * 0.38f, -0.22f + (0.5f * fx_rand(spec.seed, static_cast<std::uint32_t>(i), 41u)) };
            (void)emit_bolt(l, o, end, spec.seed + static_cast<std::uint32_t>(i * 53), t - born, 0.20f, 0.6f, 0.5f, 1, 30.f, false, out);
        }
    }
    emit_terminal(l, spec, spec.centre_gx, spec.centre_gy, 0.04f, t, true, out);
}

float total_glow(const Look &l, const FxSpec &spec, const VoxelFxParams &params)
{
    if (spec.glow_mode == FxGlowMode::AllInSight) {
        return fx_generic_glow_seconds(spec, params);
    }
    return 0.05f + terminal_life(l);
}

/* ============================================================ 登録する関数 */

//! ボルト・ボール：飛んでいる間も着弾のあとも、同じ関数が撃ってからの秒で絵を決める（稲妻は一瞬で走るため）。
void lightning_orb(const FxStyleCtx &ctx, FxFrame &out)
{
    draw_orb(look_of(ctx.spec), ctx.spec, ctx.t, out);
}

float lightning_impact_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const float total = total_orb(look_of(spec), spec);
    return std::max(0.f, total - fx_fly_seconds(spec, params));
}

void lightning_beam(const FxStyleCtx &ctx, FxFrame &out)
{
    draw_beam(look_of(ctx.spec), ctx.spec, ctx.t, out);
}

float lightning_beam_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    (void)params;
    return total_beam(look_of(spec), spec);
}

void lightning_breath(const FxStyleCtx &ctx, FxFrame &out)
{
    draw_breath(look_of(ctx.spec), ctx.spec, ctx.t, out);
}

float lightning_breath_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    (void)params;
    return total_breath(look_of(spec), spec);
}

void lightning_burst(const FxStyleCtx &ctx, FxFrame &out)
{
    draw_burst(look_of(ctx.spec), ctx.spec, ctx.t, out);
}

float lightning_burst_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    (void)params;
    return total_burst(look_of(spec), spec);
}

void lightning_glow(const FxStyleCtx &ctx, FxFrame &out)
{
    draw_glow(ctx, out);
}

float lightning_glow_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    return total_glow(look_of(spec), spec, params);
}

struct Entry {
    const char *attr;
    FxColour base;
    float smoke;
    float emissive;
};

//! このファイルが担当する属性。基準の色は、汎用の色の組（`fx_palette_for`）に使われる。
const Entry kEntries[] = {
    // §5.2 稲妻
    { "elec", { 1.00f, 0.92f, 0.35f }, 0.15f, 1.1f },
    { "plasma", { 0.95f, 0.95f, 1.00f }, 0.10f, 1.2f },
    { "mana", { 0.55f, 0.60f, 1.00f }, 0.10f, 1.1f },
    { "lite", { 1.00f, 1.00f, 0.85f }, 0.00f, 1.1f },
    { "lite_weak", { 1.00f, 1.00f, 0.85f }, 0.00f, 1.1f },
    { "dark", { 0.35f, 0.16f, 0.50f }, 0.60f, 0.55f },
    { "dark_weak", { 0.35f, 0.16f, 0.50f }, 0.60f, 0.55f },
    // 光線の仲間
    { "disintegrate", { 0.75f, 0.78f, 0.80f }, 0.30f, 1.0f },
    { "death_ray", { 0.25f, 0.10f, 0.35f }, 0.60f, 0.6f },
    { "seeker", { 0.65f, 0.85f, 1.00f }, 0.10f, 1.1f },
    { "super_ray", { 1.00f, 0.55f, 0.95f }, 0.10f, 1.1f },
};

} // namespace

void fx_register_lightning(FxStyleRegistry &registry)
{
    for (const Entry &e : kEntries) {
        FxStyle &s = registry.add(e.attr, e.base, e.smoke, e.emissive);
        s.flight = lightning_orb;
        s.impact = lightning_orb;
        s.impact_seconds = lightning_impact_seconds;
        s.beam = lightning_beam;
        s.beam_seconds = lightning_beam_seconds;
        s.breath = lightning_breath;
        s.breath_seconds = lightning_breath_seconds;
        s.burst = lightning_burst;
        s.burst_seconds = lightning_burst_seconds;
        s.glow = lightning_glow;
        s.glow_seconds = lightning_glow_seconds;
    }
}

} // namespace hd2d
