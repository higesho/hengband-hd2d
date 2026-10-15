/*!
 * @file voxel_fx.cpp
 * @brief `voxel_fx.h` の中身。矢・弾・投げた物・魔法の玉・爆発・ビーム・ブレスをボクセルの並びとして作る。
 */
#include "render/voxel_fx.h"

#include "render/fx_styles/fx_parts.h"
#include "render/fx_styles/fx_style.h"

#include <algorithm>
#include <cmath>

namespace hd2d {

namespace {

constexpr float kPi = 3.14159265f;

//! 0 以上 1 未満の擬似乱数（実体は `fx_parts.cpp` の `fx_rand`。同じ入力に同じ出力。`std::rand` は使わない）。
float rnd(std::uint32_t seed, std::uint32_t a, std::uint32_t b = 0, std::uint32_t c = 0)
{
    return fx_rand(seed, a, b, c);
}

float clamp01(float v)
{
    return std::min(std::max(v, 0.f), 1.f);
}

float lerp(float a, float b, float t)
{
    return a + ((b - a) * t);
}

float length_of(const Vec3 &v)
{
    return std::sqrt(dot(v, v));
}

FxColour mix(const FxColour &a, const FxColour &b, float t)
{
    return FxColour{ lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t) };
}

FxColour scale(const FxColour &a, float k)
{
    return FxColour{ a.r * k, a.g * k, a.b * k };
}

//! ローカルの向き（+x が前）を世界の向きへ。`FxVoxel::yaw/pitch` と同じ式（シェーダも同じ）。
Vec3 rotate_local(const Vec3 &v, float yaw, float pitch)
{
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float x1 = (v.x * cp) - (v.z * sp);
    const float z1 = (v.x * sp) + (v.z * cp);
    return Vec3{ (x1 * cy) - (v.y * sy), (x1 * sy) + (v.y * cy), z1 };
}

//! 向き `d`（正規化済み）の yaw と pitch。
void direction_angles(const Vec3 &d, float &yaw, float &pitch)
{
    yaw = std::atan2(d.y, d.x);
    pitch = std::asin(std::min(std::max(d.z, -1.f), 1.f));
}

//! 軸 `axis`（正規化済み）まわりに `ang` だけ回す（ロドリゲスの式）。
Vec3 rotate_axis(const Vec3 &v, const Vec3 &axis, float ang)
{
    const float c = std::cos(ang);
    const float s = std::sin(ang);
    return (v * c) + (cross(axis, v) * s) + (axis * (dot(axis, v) * (1.f - c)));
}

/* ------------------------------------------------------------------ 経路 */

float route_length(const FxSpec &spec)
{
    return spec.route_cum.empty() ? 0.f : spec.route_cum.back();
}

//! 経路の上の位置（`d` は始点からの道のり）。範囲の外は両端へ丸める。
Vec3 route_at(const FxSpec &spec, float d)
{
    const std::vector<Vec3> &p = spec.route;
    const std::vector<float> &c = spec.route_cum;
    if (p.empty()) {
        return Vec3{};
    }
    if ((p.size() == 1) || (d <= 0.f)) {
        return p.front();
    }
    if (d >= c.back()) {
        return p.back();
    }
    const auto it = std::upper_bound(c.begin(), c.end(), d);
    const std::size_t i = static_cast<std::size_t>(it - c.begin()); // c[i-1] <= d < c[i]
    const float seg = c[i] - c[i - 1];
    const float k = (seg > 1e-6f) ? ((d - c[i - 1]) / seg) : 0.f;
    return p[i - 1] + ((p[i] - p[i - 1]) * k);
}

//! 属性付きの矢弾の尾（火の粉）の寿命（秒）。
constexpr float kBrandTailLife = 0.40f;

bool is_dart(FxKind kind)
{
    return (kind == FxKind::Arrow) || (kind == FxKind::Bolt);
}

float speed_of(const FxSpec &spec, const VoxelFxParams &params)
{
    switch (spec.kind) {
    case FxKind::Arrow:
        return params.arrow_speed;
    case FxKind::Bolt:
        return params.bolt_speed;
    case FxKind::Shot:
        return params.shot_speed;
    case FxKind::Thrown:
        return params.thrown_speed;
    case FxKind::Boomerang:
        return params.boomerang_speed;
    default:
        return params.orb_speed;
    }
}

//! 弧の高さ（マス）。矢が基準で、弾・投げた物はそれより低く、他は 0（まっすぐ）。
float arc_height(const FxSpec &spec, const VoxelFxParams &params)
{
    float k = 0.f;
    switch (spec.kind) {
    case FxKind::Arrow:
        k = 1.f;
        break;
    case FxKind::Shot:
        k = params.arc_scale_shot;
        break;
    case FxKind::Thrown:
        k = params.arc_scale_thrown;
        break;
    default:
        break;
    }
    return std::min(params.arc_per_cell * route_length(spec), params.arc_max) * k;
}

//! 矢の長さ（マス）。矢じりが止まる所に刺さるよう、終点を長さの半分だけ手前へずらすのに使う。
float dart_length(const FxSpec &spec, const VoxelFxParams &params)
{
    return (spec.kind == FxKind::Arrow) ? params.arrow_length : 0.52f;
}

//! 本体の中心が進む道のりの長さ。矢とクロスボウの矢は、矢じりが `route` の終点に刺さるよう手前で止める。
float flight_length(const FxSpec &spec, const VoxelFxParams &params)
{
    const float full = route_length(spec);
    if (!is_dart(spec.kind)) {
        return full;
    }
    const float back = std::max(0.f, (dart_length(spec, params) * 0.5f) - 0.05f);
    return std::max(0.f, full - back);
}

//! 飛んでいる本体の向き（正規化）。前後 0.8 マスの位置の差で測るので、経路の角で急に振れない。
Vec3 flight_dir(const FxSpec &spec, const VoxelFxParams &params, float s)
{
    const float length = std::max(flight_length(spec, params), 1e-3f);
    const float delta = 0.8f / length;
    const float s0 = std::max(0.f, s - delta);
    const float s1 = std::min(1.f, s + delta);
    Vec3 d = fx_flight_position(spec, params, s1) - fx_flight_position(spec, params, s0);
    if (length_of(d) < 1e-5f) {
        d = fx_flight_position(spec, params, 1.f) - fx_flight_position(spec, params, 0.f);
    }
    const float len = length_of(d);
    return (len < 1e-5f) ? Vec3{ 1.f, 0.f, 0.f } : (d * (1.f / len));
}

/* ------------------------------------------------------------------ 矢・クロスボウの矢 */

struct DartPiece {
    float x, y, z;    //!< 矢の座標系での中心（+x が前）
    float sx, sy, sz; //!< 各軸の一辺
    float r, g, b;
    float emissive;
    FxPart part;
};

//! 弓の矢（長さ 0.8）。矢じり（金属）・矢柄（木）・羽根（白と赤）。全部で 21 個。**試作のまま**。
const DartPiece kArrowPieces[] = {
    // 矢じり：先端 1 + 十字 5 + 付け根 1
    { 0.37f, 0.f, 0.f, 0.07f, 0.07f, 0.07f, 0.82f, 0.84f, 0.88f, 0.55f, FxPart::ArrowHead },
    { 0.30f, 0.f, 0.f, 0.07f, 0.07f, 0.07f, 0.78f, 0.80f, 0.85f, 0.45f, FxPart::ArrowHead },
    { 0.30f, 0.065f, 0.f, 0.07f, 0.06f, 0.06f, 0.78f, 0.80f, 0.85f, 0.45f, FxPart::ArrowHead },
    { 0.30f, -0.065f, 0.f, 0.07f, 0.06f, 0.06f, 0.78f, 0.80f, 0.85f, 0.45f, FxPart::ArrowHead },
    { 0.30f, 0.f, 0.065f, 0.07f, 0.06f, 0.06f, 0.78f, 0.80f, 0.85f, 0.45f, FxPart::ArrowHead },
    { 0.30f, 0.f, -0.065f, 0.07f, 0.06f, 0.06f, 0.78f, 0.80f, 0.85f, 0.45f, FxPart::ArrowHead },
    { 0.235f, 0.f, 0.f, 0.06f, 0.06f, 0.06f, 0.58f, 0.60f, 0.64f, 0.25f, FxPart::ArrowHead },
    // 矢柄：長さ 0.1 の木を 6 本つなぐ（色を交互に変えてボクセルらしく）
    { 0.17f, 0.f, 0.f, 0.10f, 0.045f, 0.045f, 0.62f, 0.42f, 0.22f, 0.14f, FxPart::ArrowShaft },
    { 0.07f, 0.f, 0.f, 0.10f, 0.045f, 0.045f, 0.54f, 0.35f, 0.18f, 0.14f, FxPart::ArrowShaft },
    { -0.03f, 0.f, 0.f, 0.10f, 0.045f, 0.045f, 0.62f, 0.42f, 0.22f, 0.14f, FxPart::ArrowShaft },
    { -0.13f, 0.f, 0.f, 0.10f, 0.045f, 0.045f, 0.54f, 0.35f, 0.18f, 0.14f, FxPart::ArrowShaft },
    { -0.23f, 0.f, 0.f, 0.10f, 0.045f, 0.045f, 0.62f, 0.42f, 0.22f, 0.14f, FxPart::ArrowShaft },
    { -0.33f, 0.f, 0.f, 0.10f, 0.045f, 0.045f, 0.54f, 0.35f, 0.18f, 0.14f, FxPart::ArrowShaft },
    // 羽根：後ろ 2 か所 × 上下左右 4 枚。上の 1 枚だけ赤（見分けのつく目印）
    { -0.29f, 0.f, 0.045f, 0.07f, 0.02f, 0.07f, 0.85f, 0.10f, 0.08f, 0.18f, FxPart::ArrowFletch },
    { -0.29f, 0.f, -0.045f, 0.07f, 0.02f, 0.07f, 0.95f, 0.95f, 0.92f, 0.18f, FxPart::ArrowFletch },
    { -0.29f, 0.045f, 0.f, 0.07f, 0.07f, 0.02f, 0.95f, 0.95f, 0.92f, 0.18f, FxPart::ArrowFletch },
    { -0.29f, -0.045f, 0.f, 0.07f, 0.07f, 0.02f, 0.95f, 0.95f, 0.92f, 0.18f, FxPart::ArrowFletch },
    { -0.36f, 0.f, 0.045f, 0.07f, 0.02f, 0.07f, 0.85f, 0.10f, 0.08f, 0.18f, FxPart::ArrowFletch },
    { -0.36f, 0.f, -0.045f, 0.07f, 0.02f, 0.07f, 0.95f, 0.95f, 0.92f, 0.18f, FxPart::ArrowFletch },
    { -0.36f, 0.045f, 0.f, 0.07f, 0.07f, 0.02f, 0.95f, 0.95f, 0.92f, 0.18f, FxPart::ArrowFletch },
    { -0.36f, -0.045f, 0.f, 0.07f, 0.07f, 0.02f, 0.95f, 0.95f, 0.92f, 0.18f, FxPart::ArrowFletch },
};

/*!
 * クロスボウの矢（長さ 0.52）。**矢より短く太い**: 長さは 0.8 → 0.52、矢柄の太さは 0.045 → 0.075。
 * 矢じりは太い金属の塊、羽根は短い 3 枚の暗い板。全部で 11 個。
 */
const DartPiece kBoltPieces[] = {
    // 矢じり：先端・本体・付け根
    { 0.235f, 0.f, 0.f, 0.07f, 0.07f, 0.07f, 0.82f, 0.84f, 0.88f, 0.55f, FxPart::ArrowHead },
    { 0.170f, 0.f, 0.f, 0.09f, 0.11f, 0.11f, 0.74f, 0.77f, 0.82f, 0.40f, FxPart::ArrowHead },
    { 0.105f, 0.f, 0.f, 0.04f, 0.085f, 0.085f, 0.50f, 0.52f, 0.56f, 0.22f, FxPart::ArrowHead },
    // 矢柄：太い木を 3 本（色を交互に）
    { 0.040f, 0.f, 0.f, 0.09f, 0.075f, 0.075f, 0.46f, 0.31f, 0.17f, 0.12f, FxPart::ArrowShaft },
    { -0.050f, 0.f, 0.f, 0.09f, 0.075f, 0.075f, 0.38f, 0.26f, 0.14f, 0.12f, FxPart::ArrowShaft },
    { -0.140f, 0.f, 0.f, 0.09f, 0.075f, 0.075f, 0.46f, 0.31f, 0.17f, 0.12f, FxPart::ArrowShaft },
    // 羽根：短い 3 枚（上・左・右）
    { -0.215f, 0.f, 0.070f, 0.08f, 0.02f, 0.06f, 0.20f, 0.20f, 0.23f, 0.10f, FxPart::ArrowFletch },
    { -0.215f, 0.070f, 0.f, 0.08f, 0.06f, 0.02f, 0.20f, 0.20f, 0.23f, 0.10f, FxPart::ArrowFletch },
    { -0.215f, -0.070f, 0.f, 0.08f, 0.06f, 0.02f, 0.20f, 0.20f, 0.23f, 0.10f, FxPart::ArrowFletch },
    // 後ろの端の締め金
    { -0.200f, 0.f, 0.f, 0.03f, 0.085f, 0.085f, 0.42f, 0.44f, 0.48f, 0.15f, FxPart::ArrowHead },
};

/*!
 * @brief 属性付きの矢弾の光。**位置 `centre` の本体のまわり**に光の粒と点光源を足す（尾の火の粉は `fx_evaluate` が足す）。
 * @param fade 本体の薄れ具合（1 = 全部、0 = 無し）。矢が刺さって薄れるのに合わせる。
 */
void emit_brand(const FxSpec &spec, const VoxelFxParams &params, float t, const Vec3 &centre, const Vec3 &dir, float fade,
    FxFrame &out)
{
    if (!spec.has_brand || (fade <= 0.01f)) {
        return;
    }
    const FxPalette &pal = spec.brand;
    // 周りを回る光の粒 3 つ。
    Vec3 side = cross(dir, Vec3{ 0.f, 0.f, 1.f });
    if (length_of(side) < 1e-4f) {
        side = Vec3{ 1.f, 0.f, 0.f };
    }
    side = normalize(side);
    const Vec3 up = normalize(cross(side, dir));
    for (int i = 0; i < 3; ++i) {
        const float angle = (t * 26.f) + (static_cast<float>(i) * 2.0944f);
        FxVoxel v;
        v.center = centre + (dir * 0.12f) + (side * (std::cos(angle) * 0.075f)) + (up * (std::sin(angle) * 0.075f));
        v.size = Vec3{ 0.05f, 0.05f, 0.05f };
        v.r = pal.orb[0].r;
        v.g = pal.orb[0].g;
        v.b = pal.orb[0].b;
        v.alpha = fade;
        v.emissive = 2.4f;
        v.part = FxPart::BrandGlow;
        out.voxels.push_back(v);
    }
    PointLight light;
    light.x = centre.x;
    light.y = centre.y;
    light.z = centre.z + 0.25f;
    light.radius = params.brand_light_radius;
    light.r = pal.light_orb.r;
    light.g = pal.light_orb.g;
    light.b = pal.light_orb.b;
    light.intensity = params.brand_light_intensity * fade;
    out.lights.push_back(light);
}

void emit_dart(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out)
{
    const bool arrow = (spec.kind == FxKind::Arrow);
    const float fly = fx_fly_seconds(spec, params);
    const float stick = arrow ? params.arrow_stick_seconds : params.bolt_stick_seconds;
    const float fade_seconds = arrow ? params.arrow_fade_seconds : params.bolt_fade_seconds;
    if (t >= (fly + stick)) {
        return; //!< 本体は消えた（属性の尾は `fx_evaluate` が残りの寿命まで足す）
    }
    const bool flying = (t < fly);
    const float s = flying ? clamp01(t / std::max(fly, 1e-4f)) : 1.f;
    Vec3 centre = fx_flight_position(spec, params, s);
    const Vec3 dir = flight_dir(spec, params, s);
    float yaw = 0.f;
    float pitch = 0.f;
    direction_angles(dir, yaw, pitch);
    float alpha = 1.f;
    if (!flying) {
        const float since = t - fly;
        //! 刺さった直後に細かく震える（振幅 0.02 マスから減衰）。
        const float wobble = 0.02f * std::exp(-since * 28.f) * std::sin(since * 90.f);
        centre = centre + (dir * wobble);
        const float fade_start = stick - fade_seconds;
        if (since > fade_start) {
            alpha = clamp01((stick - since) / std::max(fade_seconds, 1e-4f));
        }
    }
    const float k = arrow ? (params.arrow_length / 0.8f) : 1.f;
    const DartPiece *pieces = arrow ? kArrowPieces : kBoltPieces;
    const std::size_t count = arrow ? (sizeof(kArrowPieces) / sizeof(kArrowPieces[0])) : (sizeof(kBoltPieces) / sizeof(kBoltPieces[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const DartPiece &piece = pieces[i];
        FxVoxel v;
        const Vec3 local{ piece.x * k, piece.y * k, piece.z * k };
        v.center = centre + rotate_local(local, yaw, pitch);
        v.size = Vec3{ piece.sx * k, piece.sy * k, piece.sz * k };
        v.r = piece.r;
        v.g = piece.g;
        v.b = piece.b;
        v.alpha = alpha;
        v.emissive = piece.emissive;
        v.yaw = yaw;
        v.pitch = pitch;
        v.part = piece.part;
        out.voxels.push_back(v);
    }
    out.projectile_visible = true;
    out.projectile_pos = centre;
    out.projectile_dir = dir;
    emit_brand(spec, params, t, centre, dir, alpha, out);
}

/* ------------------------------------------------------------------ 弾 */

void emit_shot(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out)
{
    const float fly = fx_fly_seconds(spec, params);
    if (t >= fly) {
        return;
    }
    const float s = clamp01(t / std::max(fly, 1e-4f));
    const Vec3 centre = fx_flight_position(spec, params, s);
    const Vec3 dir = flight_dir(spec, params, s);
    // 進む向きに直角な軸のまわりに転がる（前へ転がって見える）。
    Vec3 axis = cross(dir, Vec3{ 0.f, 0.f, 1.f });
    axis = (length_of(axis) < 1e-4f) ? Vec3{ 1.f, 0.f, 0.f } : normalize(axis);
    const float angle = (params.shot_spin * t) + (rnd(spec.seed, 1u) * 2.f * kPi);
    const float voxel = params.shot_voxel;
    //! 3×3×3 から角を除いた 19 個（丸い石）。粒ごとに灰色の濃さを変えるので、回ると模様が動いて見える。
    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
            for (int k = -1; k <= 1; ++k) {
                if ((std::abs(i) == 1) && (std::abs(j) == 1) && (std::abs(k) == 1)) {
                    continue;
                }
                const std::uint32_t id = static_cast<std::uint32_t>(((i + 1) * 9) + ((j + 1) * 3) + (k + 1));
                const float tone = 0.62f + (0.55f * (rnd(spec.seed, id, 3u) - 0.4f));
                FxVoxel v;
                v.center = centre + rotate_axis(Vec3{ static_cast<float>(i) * voxel, static_cast<float>(j) * voxel, static_cast<float>(k) * voxel },
                                        axis, angle);
                v.size = Vec3{ voxel * 1.02f, voxel * 1.02f, voxel * 1.02f };
                v.r = 0.58f * tone;
                v.g = 0.55f * tone;
                v.b = 0.51f * tone;
                v.emissive = 0.04f;
                v.part = FxPart::ShotStone;
                out.voxels.push_back(v);
            }
        }
    }
    out.projectile_visible = true;
    out.projectile_pos = centre;
    out.projectile_dir = dir;
    emit_brand(spec, params, t, centre, dir, 1.f, out);
}

/* ------------------------------------------------------------------ 投げた物 */

void emit_thrown(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out)
{
    const float fly = fx_fly_seconds(spec, params);
    if (t >= fly) {
        return;
    }
    const float s = clamp01(t / std::max(fly, 1e-4f));
    const Vec3 centre = fx_flight_position(spec, params, s);
    const Vec3 dir = flight_dir(spec, params, s);
    const float rate = (spec.kind == FxKind::Boomerang) ? params.boomerang_spin : params.thrown_spin;
    const float spin = (rate * t) + (rnd(spec.seed, 2u) * 2.f * kPi);
    if (spec.use_slab) {
        FxSlab slab;
        slab.item_id = spec.item_id;
        slab.centre = centre;
        slab.scale = params.slab_scale;
        slab.spin = spin;
        slab.face_yaw = spec.face_yaw;
        slab.face_pitch = spec.face_pitch;
        out.slabs.push_back(slab);
    } else {
        // 板が引けない物：小さな箱が前へ転がる（本体と、片側の目印の小さな箱）。
        float yaw = 0.f;
        float pitch = 0.f;
        direction_angles(dir, yaw, pitch);
        const float tumble = spin; //!< 進む向きに直角な水平軸まわりに前へ転がる（`pitch` は使わない）
        FxVoxel body;
        body.center = centre;
        body.size = Vec3{ params.thrown_box, params.thrown_box * 0.7f, params.thrown_box * 0.7f };
        body.r = 0.62f;
        body.g = 0.54f;
        body.b = 0.42f;
        body.emissive = 0.05f;
        body.yaw = yaw;
        body.pitch = tumble;
        body.part = FxPart::ThrownBox;
        out.voxels.push_back(body);
        FxVoxel cap = body;
        cap.center = centre + rotate_local(Vec3{ params.thrown_box * 0.5f, 0.f, 0.f }, yaw, tumble);
        cap.size = Vec3{ params.thrown_box * 0.3f, params.thrown_box * 0.5f, params.thrown_box * 0.5f };
        cap.r = 0.80f;
        cap.g = 0.78f;
        cap.b = 0.72f;
        out.voxels.push_back(cap);
    }
    out.projectile_visible = true;
    out.projectile_pos = centre;
    out.projectile_dir = dir;
    emit_brand(spec, params, t, centre, dir, 1.f, out);
}

/* ------------------------------------------------------------------ 魔法の玉 */

//! 玉の色。`u` は中心 0 〜 外縁 1。
void orb_colour(const FxPalette &pal, float u, float &r, float &g, float &b, float &emissive)
{
    const int i = (u < 0.5f) ? 0 : 1;
    const float k = (u < 0.5f) ? (u / 0.5f) : ((u - 0.5f) / 0.5f);
    r = lerp(pal.orb[i].r, pal.orb[i + 1].r, k);
    g = lerp(pal.orb[i].g, pal.orb[i + 1].g, k);
    b = lerp(pal.orb[i].b, pal.orb[i + 1].b, k);
    emissive = lerp(pal.orb_emissive[i], pal.orb_emissive[i + 1], k);
}

void emit_orb_body(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out)
{
    const float fly = fx_fly_seconds(spec, params);
    const float s = clamp01(t / std::max(fly, 1e-4f));
    const Vec3 centre = fx_flight_position(spec, params, s);
    const Vec3 dir = flight_dir(spec, params, s);
    const std::uint32_t frame_index = static_cast<std::uint32_t>(std::floor(t * params.orb_flicker_hz));
    const float voxel = params.orb_voxel;
    const float radius = params.orb_radius * (spec.explode ? 1.f : params.bolt_orb_scale);
    const float reach = radius / voxel; //!< ボクセル何個ぶんか（既定 3.27）

    // 本体：球の中のボクセル。外側ほどちらつく（毎フレーム、出たり消えたり大きさが変わる）。
    const int n = static_cast<int>(std::ceil(reach));
    for (int i = -n; i <= n; ++i) {
        for (int j = -n; j <= n; ++j) {
            for (int k = -n; k <= n; ++k) {
                const float d = std::sqrt(static_cast<float>((i * i) + (j * j) + (k * k)));
                if (d > reach) {
                    continue;
                }
                const float u = d / reach;
                const std::uint32_t cell = static_cast<std::uint32_t>(((i + 8) * 289) + ((j + 8) * 17) + (k + 8));
                const float h = rnd(spec.seed, cell, frame_index, 1u);
                // 外縁ほど落ちる確率が高い（中心は必ず残る）。
                if (h < (std::max(u - 0.45f, 0.f) * 0.9f)) {
                    continue;
                }
                FxVoxel v;
                const float wobble = 0.012f;
                v.center = centre
                    + Vec3{ (static_cast<float>(i) * voxel) + ((rnd(spec.seed, cell, frame_index, 2u) - 0.5f) * wobble),
                          (static_cast<float>(j) * voxel) + ((rnd(spec.seed, cell, frame_index, 3u) - 0.5f) * wobble),
                          (static_cast<float>(k) * voxel) + ((rnd(spec.seed, cell, frame_index, 4u) - 0.5f) * wobble) };
                const float grow = 0.82f + (0.30f * rnd(spec.seed, cell, frame_index, 5u));
                v.size = Vec3{ voxel * grow, voxel * grow, voxel * grow };
                orb_colour(spec.palette, u, v.r, v.g, v.b, v.emissive);
                v.part = FxPart::OrbCore;
                out.voxels.push_back(v);
            }
        }
    }
    // すぐ後ろの炎：進む向きと反対へ、小さくなりながら 4 個。
    for (int i = 0; i < 4; ++i) {
        const float back = radius + (0.10f * static_cast<float>(i + 1));
        const float jitter = (rnd(spec.seed, static_cast<std::uint32_t>(i), frame_index, 6u) - 0.5f) * 0.05f;
        FxVoxel v;
        v.center = centre - (dir * back) + Vec3{ 0.f, 0.f, jitter };
        const float size = voxel * (1.35f - (0.22f * static_cast<float>(i)));
        v.size = Vec3{ size, size, size };
        orb_colour(spec.palette, 0.55f + (0.12f * static_cast<float>(i)), v.r, v.g, v.b, v.emissive);
        v.part = FxPart::OrbTail;
        out.voxels.push_back(v);
    }
    // 飛んでいる玉の点光源（ちらつく）。
    PointLight light;
    light.x = centre.x;
    light.y = centre.y;
    light.z = centre.z + 0.35f;
    light.radius = params.orb_light_radius;
    light.r = spec.palette.light_orb.r;
    light.g = spec.palette.light_orb.g;
    light.b = spec.palette.light_orb.b;
    const float flicker = 0.85f + (0.30f * rnd(spec.seed, frame_index, 7u));
    light.intensity = params.orb_light_intensity * flicker;
    out.lights.push_back(light);

    out.projectile_visible = true;
    out.projectile_pos = centre;
    out.projectile_dir = dir;
}

//! 火の粉の尾：進んだ跡に残る小さなボクセル。小さくなって消える。
void emit_embers(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out, const FxPalette &pal, float rate,
    float life, float fly, float size_scale)
{
    const int first = std::max(0, static_cast<int>(std::ceil((t - life) * rate)));
    const int last = static_cast<int>(std::floor(std::min(t, fly) * rate));
    for (int i = first; i <= last; ++i) {
        const float born = static_cast<float>(i) / rate;
        const float age = t - born;
        if ((age < 0.f) || (age >= life)) {
            continue;
        }
        const float u = age / life;
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float s = clamp01(born / std::max(fly, 1e-4f));
        const Vec3 origin = fx_flight_position(spec, params, s) - (flight_dir(spec, params, s) * 0.05f);
        FxVoxel v;
        const float spread = 0.16f;
        v.center = origin
            + Vec3{ (rnd(spec.seed, id, 11u) - 0.5f) * spread, (rnd(spec.seed, id, 12u) - 0.5f) * spread,
                  ((rnd(spec.seed, id, 13u) - 0.5f) * spread) + (0.45f * age) };
        const float size = std::max(0.014f, (0.07f + (0.03f * rnd(spec.seed, id, 14u))) * (1.f - u) * size_scale);
        v.size = Vec3{ size, size, size };
        v.r = lerp(pal.ember[0].r, pal.ember[1].r, u);
        v.g = lerp(pal.ember[0].g, pal.ember[1].g, u);
        v.b = lerp(pal.ember[0].b, pal.ember[1].b, u);
        v.emissive = lerp(2.8f, 0.4f, u);
        v.alpha = clamp01(1.5f * (1.f - u));
        v.part = FxPart::Ember;
        out.voxels.push_back(v);
    }
}

/* ------------------------------------------------------------------ 爆発 */

//! 爆発の粒の色。`u` は 0（出たて）〜 1（消える）。
void burst_colour(const FxPalette &pal, float u, float &r, float &g, float &b, float &emissive)
{
    int i = 0;
    float k = 0.f;
    if (u < 0.2f) {
        i = 0;
        k = u / 0.2f;
    } else if (u < 0.6f) {
        i = 1;
        k = (u - 0.2f) / 0.4f;
    } else {
        i = 2;
        k = (u - 0.6f) / 0.4f;
    }
    r = lerp(pal.burst[i].r, pal.burst[i + 1].r, k);
    g = lerp(pal.burst[i].g, pal.burst[i + 1].g, k);
    b = lerp(pal.burst[i].b, pal.burst[i + 1].b, k);
    emissive = lerp(pal.burst_emissive[i], pal.burst_emissive[i + 1], k);
}

//! 粒の中心を、そのマスの中心 `c` から ±(0.5 − 粒の半分) の中へ押し込む。**爆発を範囲のマスに収める唯一の場所。**
float keep_in_cell(float value, float c, float size)
{
    const float half = std::max(0.f, 0.5f - (size * 0.5f) - 0.002f);
    return std::min(std::max(value, c - half), c + half);
}

//! 爆発の出し方（`small` は小さな弾け：ボルトの当たり・ビームの終点。粒の数・寿命を減らし、煙を出さない）。
using BurstMode = FxBurstMode;

//! 爆発の長さ（秒）：最後のマスが始まってから消えるまで。
float burst_seconds(const FxSpec &spec, const VoxelFxParams &params, const BurstMode &mode)
{
    if (mode.small) {
        return params.pop_life + 0.03f;
    }
    int last_ring = 0;
    for (const FxBurstCell &cell : spec.cells) {
        last_ring = std::max(last_ring, cell.ring);
    }
    const float last = static_cast<float>(last_ring) * mode.ring_delay;
    float total = std::max(last + 0.03f + params.fire_life, mode.flash ? params.flash_seconds : 0.f);
    if (spec.palette.smoke_alpha > 0.01f) {
        total = std::max(total, last + params.smoke_delay + 0.04f + params.smoke_life);
    }
    return total;
}

/*!
 * @brief 爆発（`te` は爆発の始まりからの秒）。範囲のマス（`spec.cells`）の中だけに粒を出す。
 * @param with_light 点光源を足すか。
 */
void emit_burst(const FxSpec &spec, const VoxelFxParams &params, float te, const BurstMode &mode, FxFrame &out, bool with_light)
{
    const FxPalette &pal = spec.palette;
    std::vector<FxBurstCell> fallback;
    const std::vector<FxBurstCell> *cells = &spec.cells;
    if (cells->empty()) {
        fallback.push_back(FxBurstCell{ spec.centre_gx, spec.centre_gy, 0 });
        cells = &fallback;
    }
    const float centre_x = static_cast<float>(spec.centre_gx) + 0.5f;
    const float centre_y = static_cast<float>(spec.centre_gy) + 0.5f;
    float extent = 0.f;
    const auto note = [&](const FxVoxel &v) {
        const float dx = v.center.x - centre_x;
        const float dy = v.center.y - centre_y;
        extent = std::max(extent, std::sqrt((dx * dx) + (dy * dy)));
        out.voxels.push_back(v);
    };
    const float fire_life = mode.small ? params.pop_life : params.fire_life;
    const float size_k = mode.small ? 0.7f : 1.f;
    int max_ring = 0;
    float sum_x = 0.f;
    float sum_y = 0.f;
    for (const FxBurstCell &cell : *cells) {
        max_ring = std::max(max_ring, cell.ring);
        sum_x += static_cast<float>(cell.gx) + 0.5f;
        sum_y += static_cast<float>(cell.gy) + 0.5f;
    }

    // 閃光：着弾の瞬間だけ、中心のマスに白い塊（十字の立体）が出て縮む。**マスの中に収まる大きさ**。
    if (mode.flash && (te >= 0.f) && (te < params.flash_seconds)) {
        const float u = te / params.flash_seconds;
        const float shrink = 1.f - (u * u);
        const float core = 0.40f * shrink;
        const float arm = 0.26f * shrink;
        const float arm_size = 0.16f * shrink;
        const Vec3 offsets[] = { Vec3{ 0.f, 0.f, 0.f }, Vec3{ arm, 0.f, 0.f }, Vec3{ -arm, 0.f, 0.f }, Vec3{ 0.f, arm, 0.f },
            Vec3{ 0.f, -arm, 0.f }, Vec3{ 0.f, 0.f, arm }, Vec3{ 0.f, 0.f, -arm } };
        for (std::size_t i = 0; i < 7; ++i) {
            FxVoxel v;
            v.center = Vec3{ centre_x, centre_y, 0.5f } + offsets[i];
            const float size = (i == 0) ? core : arm_size;
            v.size = Vec3{ size, size, size };
            v.r = lerp(pal.flash[0].r, pal.flash[1].r, u);
            v.g = lerp(pal.flash[0].g, pal.flash[1].g, u);
            v.b = lerp(pal.flash[0].b, pal.flash[1].b, u);
            v.emissive = lerp(3.6f, 1.6f, u);
            v.part = FxPart::BurstFlash;
            note(v);
        }
    }

    for (const FxBurstCell &cell : *cells) {
        const float cell_x = static_cast<float>(cell.gx) + 0.5f;
        const float cell_y = static_cast<float>(cell.gy) + 0.5f;
        const std::uint32_t cell_id = static_cast<std::uint32_t>(((cell.gx & 0x3ff) * 1031) + (cell.gy & 0x3ff));
        // 外へ向かう向き（中心のマスはばらばら）。粒の流れを少し外へ寄せるだけ（マスからは出ない）。
        float out_x = cell_x - centre_x;
        float out_y = cell_y - centre_y;
        if ((std::fabs(out_x) < 1e-4f) && (std::fabs(out_y) < 1e-4f)) {
            const float angle = rnd(spec.seed, cell_id, 21u) * 2.f * kPi;
            out_x = std::cos(angle);
            out_y = std::sin(angle);
        }
        const float out_len = std::sqrt((out_x * out_x) + (out_y * out_y));
        out_x /= out_len;
        out_y /= out_len;
        const float ring_start = static_cast<float>(cell.ring) * mode.ring_delay;
        const int fire_count = mode.small ? ((cell.ring == 0) ? params.pop_voxels : 0) : params.fire_per_cell;

        // 粒：外へゆっくり流れ、上へ昇り、小さくなって消える。**マスの外へは出さない**。
        for (int j = 0; j < fire_count; ++j) {
            const std::uint32_t id = static_cast<std::uint32_t>(j);
            const float born = ring_start + (0.03f * rnd(spec.seed, cell_id, id, 22u));
            const float age = te - born;
            if ((age < 0.f) || (age >= fire_life)) {
                continue;
            }
            const float u = age / fire_life;
            const float out_speed = 0.10f + (0.12f * rnd(spec.seed, cell_id, id, 23u));
            const float side = (rnd(spec.seed, cell_id, id, 24u) - 0.5f) * 0.12f;
            const float rise = 0.70f + (0.90f * rnd(spec.seed, cell_id, id, 25u));
            FxVoxel v;
            const float size = std::max(0.012f, (0.20f + (0.12f * rnd(spec.seed, cell_id, id, 29u))) * size_k * std::pow(1.f - u, 0.8f));
            v.size = Vec3{ size, size, size };
            float x = cell_x + ((rnd(spec.seed, cell_id, id, 26u) - 0.5f) * 0.64f);
            float y = cell_y + ((rnd(spec.seed, cell_id, id, 27u) - 0.5f) * 0.64f);
            x += ((out_x * out_speed) - (out_y * side)) * age;
            y += ((out_y * out_speed) + (out_x * side)) * age;
            v.center = Vec3{ keep_in_cell(x, cell_x, size), keep_in_cell(y, cell_y, size),
                0.05f + (0.50f * rnd(spec.seed, cell_id, id, 28u)) + (rise * age) };
            /*
             * 自己発光は 2 以下に抑える（トーンマップの露出 1.25 で 2.5 を超えると白に飽和して、
             * 色が見えなくなる。2026-10-08 の絵合わせ）。
             */
            burst_colour(pal, u, v.r, v.g, v.b, v.emissive);
            v.part = FxPart::BurstMain;
            note(v);
        }

        // 煙：少し遅れて、暗い灰色の大きめのボクセルが立ちのぼる（マスの中に収まる大きさ）。
        if (!mode.small && (pal.smoke_alpha > 0.01f)) {
            for (int j = 0; j < params.smoke_per_cell; ++j) {
                const std::uint32_t id = static_cast<std::uint32_t>(100 + j);
                const float born = ring_start + params.smoke_delay + (0.04f * rnd(spec.seed, cell_id, id, 31u));
                const float age = te - born;
                if ((age < 0.f) || (age >= params.smoke_life)) {
                    continue;
                }
                const float u = age / params.smoke_life;
                FxVoxel v;
                const float size = lerp(0.20f, 0.34f, u);
                v.size = Vec3{ size, size, size };
                const float x = cell_x + ((rnd(spec.seed, cell_id, id, 32u) - 0.5f) * 0.44f) + (out_x * 0.06f * age);
                const float y = cell_y + ((rnd(spec.seed, cell_id, id, 33u) - 0.5f) * 0.44f) + (out_y * 0.06f * age);
                v.center = Vec3{ keep_in_cell(x, cell_x, size), keep_in_cell(y, cell_y, size), 0.25f + (0.9f * age) };
                // 出はじめは焦げた暖色、すぐに灰色。
                const float warm = clamp01(1.f - (u * 3.f));
                v.r = lerp(pal.smoke_cool.r, pal.smoke_warm.r, warm);
                v.g = lerp(pal.smoke_cool.g, pal.smoke_warm.g, warm);
                v.b = lerp(pal.smoke_cool.b, pal.smoke_warm.b, warm);
                v.emissive = 0.30f * warm; //!< 出はじめだけ、火に照らされて赤く見える
                v.alpha = pal.smoke_alpha * std::min(1.f, u * 5.f) * (1.f - u);
                v.part = FxPart::BurstSmoke;
                if (v.alpha > 0.01f) {
                    note(v);
                }
            }
        }
    }

    // 点光源：立ち上がりは一瞬、そのあと急に弱まる。**強さも半径も範囲の広さに合わせる**。
    if (with_light) {
        const bool broad = (spec.kind == FxKind::Breath);
        const float reach = broad ? (static_cast<float>(max_ring) * 0.5f) : static_cast<float>(max_ring);
        const float decay_seconds = mode.small ? (params.pop_life * 1.5f) : params.burst_light_decay;
        float strength = 0.f;
        if (te < params.burst_light_rise) {
            strength = std::max(te, 0.f) / params.burst_light_rise;
        } else {
            const float decay = 1.f - ((te - params.burst_light_rise) / decay_seconds);
            strength = (decay > 0.f) ? (decay * decay) : 0.f;
        }
        if (strength > 0.f) {
            PointLight light;
            const float n = static_cast<float>(cells->size());
            light.x = broad ? (sum_x / n) : centre_x;
            light.y = broad ? (sum_y / n) : centre_y;
            light.z = 1.2f; //!< 床へ斜めに当てる（低いと足元だけが白く飛ぶ）
            light.radius = mode.small ? 2.5f : (reach + 1.f + params.burst_light_margin);
            light.r = pal.light_burst.r;
            light.g = pal.light_burst.g;
            light.b = pal.light_burst.b;
            const float peak = mode.small ? 1.6f : std::min(params.burst_light_max, params.burst_light_base + (params.burst_light_per_ring * reach));
            light.intensity = peak * strength;
            out.lights.push_back(light);
        }
    }
    out.explosion_extent = out.explosion_active ? std::max(out.explosion_extent, extent) : extent;
    out.explosion_active = true;
}

/* ------------------------------------------------------------------ ビーム */

void emit_beam(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out)
{
    const std::size_t segments = (spec.route.size() > 1) ? (spec.route.size() - 1) : 0;
    const FxPalette &pal = spec.palette;
    const std::uint32_t frame_index = static_cast<std::uint32_t>(std::floor(t * 30.f));
    for (std::size_t i = 0; i < segments; ++i) {
        const float start = static_cast<float>(i) * params.beam_cell_delay;
        const float age = t - start;
        if ((age < 0.f) || (age >= params.beam_life)) {
            continue;
        }
        const float fade = (age > (params.beam_life - params.beam_fade)) ? clamp01((params.beam_life - age) / params.beam_fade) : 1.f;
        const Vec3 a = spec.route[i];
        const Vec3 b = spec.route[i + 1];
        const Vec3 d = b - a;
        const float len = length_of(d);
        if (len < 1e-4f) {
            continue;
        }
        const Vec3 dir = d * (1.f / len);
        float yaw = 0.f;
        float pitch = 0.f;
        direction_angles(dir, yaw, pitch);
        const Vec3 mid = a + (d * 0.5f);
        const float flick = 0.85f + (0.30f * rnd(spec.seed, static_cast<std::uint32_t>(i), frame_index, 41u));
        // 周りの光（半透明）。
        FxVoxel glow;
        glow.center = mid;
        glow.size = Vec3{ len * 1.10f, 0.22f * flick, 0.22f * flick };
        glow.r = pal.orb[1].r;
        glow.g = pal.orb[1].g;
        glow.b = pal.orb[1].b;
        glow.alpha = 0.38f * fade;
        glow.emissive = 1.5f;
        glow.yaw = yaw;
        glow.pitch = pitch;
        glow.part = FxPart::BeamGlow;
        out.voxels.push_back(glow);
        // 芯（明るい）。
        FxVoxel core;
        core.center = mid;
        core.size = Vec3{ len * 1.04f, 0.075f * flick, 0.075f * flick };
        core.r = pal.orb[0].r;
        core.g = pal.orb[0].g;
        core.b = pal.orb[0].b;
        core.alpha = (fade >= 0.999f) ? 1.f : fade;
        core.emissive = 3.2f;
        core.yaw = yaw;
        core.pitch = pitch;
        core.part = FxPart::BeamCore;
        out.voxels.push_back(core);
    }
    // 点光源：ビームの真ん中。
    if ((segments > 0) && (t >= 0.f)) {
        const float life = (static_cast<float>(segments) * params.beam_cell_delay) + params.beam_life;
        const float u = clamp01(t / life);
        const float strength = (t < 0.03f) ? (t / 0.03f) : ((1.f - u) * (1.f - u));
        PointLight light;
        const Vec3 mid = route_at(spec, route_length(spec) * 0.5f);
        light.x = mid.x;
        light.y = mid.y;
        light.z = 0.9f;
        light.radius = std::min(7.f, 2.8f + (0.25f * static_cast<float>(segments)));
        light.r = pal.light_orb.r;
        light.g = pal.light_orb.g;
        light.b = pal.light_orb.b;
        light.intensity = 2.2f * strength;
        if (light.intensity > 0.01f) {
            out.lights.push_back(light);
        }
    }
}

/* ------------------------------------------------------------------ 飛ばない魔法の光 */

void emit_glow(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out)
{
    const FxPalette &pal = spec.palette;
    const float total = params.glow_seconds;
    const float u = clamp01(t / total);
    const float env = std::min(clamp01(t / 0.15f), clamp01((total - t) / 0.25f));
    const float cx = static_cast<float>(spec.centre_gx) + 0.5f;
    const float cy = static_cast<float>(spec.centre_gy) + 0.5f;
    const float zc = 0.55f + (0.35f * u);
    // 中心の小さな十字（光の玉）。
    const float core = 0.14f * env;
    const float arm = 0.10f * env;
    const float arm_size = 0.07f * env;
    const Vec3 offsets[] = { Vec3{ 0.f, 0.f, 0.f }, Vec3{ arm, 0.f, 0.f }, Vec3{ -arm, 0.f, 0.f }, Vec3{ 0.f, arm, 0.f },
        Vec3{ 0.f, -arm, 0.f }, Vec3{ 0.f, 0.f, arm }, Vec3{ 0.f, 0.f, -arm } };
    for (std::size_t i = 0; i < 7; ++i) {
        FxVoxel v;
        v.center = Vec3{ cx, cy, zc } + offsets[i];
        const float size = (i == 0) ? core : arm_size;
        v.size = Vec3{ size, size, size };
        if (size < 0.004f) {
            continue;
        }
        const FxColour &c = (i == 0) ? pal.orb[0] : pal.orb[1];
        v.r = c.r;
        v.g = c.g;
        v.b = c.b;
        v.emissive = (i == 0) ? 3.0f : 2.2f;
        v.part = FxPart::GlowMote;
        out.voxels.push_back(v);
    }
    // まわりを回りながら昇る光の粒 6 つ。
    for (int i = 0; i < 6; ++i) {
        const float angle = (kPi * 2.f * static_cast<float>(i) / 6.f) + (t * 4.2f);
        const float radius = 0.20f + (0.04f * std::sin((t * 7.f) + static_cast<float>(i)));
        const float size = (0.05f + (0.03f * rnd(spec.seed, static_cast<std::uint32_t>(i), 51u))) * env;
        FxVoxel v;
        v.center = Vec3{ cx + (std::cos(angle) * radius), cy + (std::sin(angle) * radius),
            0.30f + (0.75f * u) + (0.05f * std::sin((t * 9.f) + static_cast<float>(i) * 1.7f)) };
        v.size = Vec3{ size, size, size };
        v.r = pal.orb[1].r;
        v.g = pal.orb[1].g;
        v.b = pal.orb[1].b;
        v.emissive = 2.4f;
        v.part = FxPart::GlowMote;
        out.voxels.push_back(v);
    }
    PointLight light;
    light.x = cx;
    light.y = cy;
    light.z = zc + 0.2f;
    light.radius = 2.6f;
    light.r = pal.light_orb.r;
    light.g = pal.light_orb.g;
    light.b = pal.light_orb.b;
    light.intensity = 1.4f * env;
    if (light.intensity > 0.01f) {
        out.lights.push_back(light);
    }
}

} // namespace

/* ------------------------------------------------------------------ 公開 */

int fx_grid_distance(int dx, int dy)
{
    const int ax = std::abs(dx);
    const int ay = std::abs(dy);
    return (ax > ay) ? (ax + (ay / 2)) : (ay + (ax / 2));
}

FxPalette fx_palette_from_base(const FxColour &base, float smoke_alpha, float emissive_scale)
{
    FxPalette p;
    const FxColour white{ 1.f, 1.f, 1.f };
    p.orb[0] = mix(base, white, 0.70f);
    p.orb[1] = base;
    p.orb[2] = scale(base, 0.55f);
    p.burst[0] = mix(base, white, 0.45f);
    p.burst[1] = mix(base, white, 0.15f);
    p.burst[2] = scale(base, 0.75f);
    p.burst[3] = scale(base, 0.25f);
    p.flash[0] = mix(base, white, 0.85f);
    p.flash[1] = mix(base, white, 0.35f);
    p.smoke_warm = scale(base, 0.55f);
    p.smoke_cool = mix(FxColour{ 0.30f, 0.29f, 0.28f }, scale(base, 0.45f), 0.35f);
    p.smoke_alpha = smoke_alpha;
    p.ember[0] = mix(base, white, 0.30f);
    p.ember[1] = scale(base, 0.50f);
    FxColour light = mix(base, white, 0.15f);
    const float peak = std::max({ light.r, light.g, light.b, 1e-3f });
    light = scale(light, 1.f / peak);
    p.light_orb = light;
    p.light_burst = light;
    for (float &e : p.orb_emissive) {
        e *= emissive_scale;
    }
    for (float &e : p.burst_emissive) {
        e *= emissive_scale;
    }
    p.is_fire = false;
    p.known = true;
    return p;
}

FxPalette fx_palette_for(const std::string &attr)
{
    const FxStyle *style = fx_find_style(attr);
    if (style != nullptr) {
        if (style->palette != nullptr) {
            return style->palette(*style);
        }
        return fx_palette_from_base(style->base, style->smoke, style->emissive);
    }
    FxPalette white = fx_palette_from_base(FxColour{ 1.f, 1.f, 1.f }, 0.20f, 1.f);
    white.known = false;
    return white;
}

void fx_set_route(FxSpec &spec, const std::vector<Vec3> &outbound, const std::vector<Vec3> &back)
{
    spec.route = outbound;
    spec.turn_index = outbound.empty() ? 0 : (outbound.size() - 1);
    spec.route.insert(spec.route.end(), back.begin(), back.end());
    spec.route_cum.assign(spec.route.size(), 0.f);
    for (std::size_t i = 1; i < spec.route.size(); ++i) {
        spec.route_cum[i] = spec.route_cum[i - 1] + length_of(spec.route[i] - spec.route[i - 1]);
    }
}

float fx_fly_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    if (spec.route.empty()) {
        return 0.f;
    }
    return std::max(params.min_fly_seconds, flight_length(spec, params) / std::max(speed_of(spec, params), 1e-3f));
}

float fx_outbound_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const float fly = fx_fly_seconds(spec, params);
    const float full = route_length(spec);
    if ((spec.route.empty()) || (full < 1e-5f) || (spec.turn_index + 1 >= spec.route.size())) {
        return fly;
    }
    return fly * (spec.route_cum[spec.turn_index] / full);
}

namespace {

//! 撃ち方のスタイル。登録が無ければ、すべての段階が `nullptr` の汎用のスタイル。
const FxStyle &style_of(const FxSpec &spec)
{
    static const FxStyle kGeneric{};
    return (spec.style != nullptr) ? *spec.style : kGeneric;
}

} // namespace

float fx_total_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const float fly = fx_fly_seconds(spec, params);
    const FxStyle &style = style_of(spec);
    switch (spec.kind) {
    case FxKind::Arrow:
        return fly + std::max(params.arrow_stick_seconds, spec.has_brand ? kBrandTailLife : 0.f);
    case FxKind::Bolt:
        return fly + std::max(params.bolt_stick_seconds, spec.has_brand ? kBrandTailLife : 0.f);
    case FxKind::Shot:
    case FxKind::Thrown:
    case FxKind::Boomerang:
        return fly + (spec.has_brand ? kBrandTailLife : 0.f);
    case FxKind::Orb:
        return fly + ((style.impact_seconds != nullptr) ? style.impact_seconds(spec, params) : fx_generic_impact_seconds(spec, params));
    case FxKind::Beam:
        return (style.beam_seconds != nullptr) ? style.beam_seconds(spec, params) : fx_generic_beam_seconds(spec, params);
    case FxKind::Breath:
        return (style.breath_seconds != nullptr) ? style.breath_seconds(spec, params) : fx_generic_breath_seconds(spec, params);
    case FxKind::Burst:
        return (style.burst_seconds != nullptr) ? style.burst_seconds(spec, params) : fx_generic_burst_seconds(spec, params);
    case FxKind::Glow:
        return (style.glow_seconds != nullptr) ? style.glow_seconds(spec, params) : fx_generic_glow_seconds(spec, params);
    }
    return 0.f;
}

Vec3 fx_flight_position(const FxSpec &spec, const VoxelFxParams &params, float s)
{
    const float clamped = clamp01(s);
    Vec3 p = route_at(spec, flight_length(spec, params) * clamped);
    p.z += arc_height(spec, params) * 4.f * clamped * (1.f - clamped);
    return p;
}

void fx_evaluate(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out)
{
    if (t < 0.f) {
        return;
    }
    if (t >= fx_total_seconds(spec, params)) {
        out.finished = true;
        return;
    }
    const FxStyle &style = style_of(spec);
    const float fly = fx_fly_seconds(spec, params);
    FxStyleCtx ctx{ spec, params, t, fly, t - fly };
    switch (spec.kind) {
    case FxKind::Arrow:
    case FxKind::Bolt:
        emit_dart(spec, params, t, out);
        break;
    case FxKind::Shot:
        emit_shot(spec, params, t, out);
        break;
    case FxKind::Thrown:
    case FxKind::Boomerang:
        emit_thrown(spec, params, t, out);
        break;
    case FxKind::Orb:
        if (t < fly) {
            (style.flight != nullptr ? style.flight : fx_generic_flight)(ctx, out);
        } else {
            (style.impact != nullptr ? style.impact : fx_generic_impact)(ctx, out);
        }
        break;
    case FxKind::Beam:
        ctx.fly = 0.f;
        ctx.te = t;
        (style.beam != nullptr ? style.beam : fx_generic_beam)(ctx, out);
        break;
    case FxKind::Breath:
        ctx.fly = 0.f;
        ctx.te = t;
        (style.breath != nullptr ? style.breath : fx_generic_breath)(ctx, out);
        break;
    case FxKind::Burst:
        ctx.fly = 0.f;
        ctx.te = t;
        (style.burst != nullptr ? style.burst : fx_generic_burst)(ctx, out);
        break;
    case FxKind::Glow:
        ctx.fly = 0.f;
        ctx.te = t;
        (style.glow != nullptr ? style.glow : fx_generic_glow)(ctx, out);
        break;
    }
    // 属性付きの矢弾の尾：飛んだ跡に、属性の色の火の粉を残す。
    if (spec.has_brand && (spec.kind != FxKind::Orb) && (spec.kind != FxKind::Beam) && (spec.kind != FxKind::Breath)
        && (spec.kind != FxKind::Burst) && (spec.kind != FxKind::Glow)) {
        emit_embers(spec, params, t, out, spec.brand, 40.f, kBrandTailLife, fx_fly_seconds(spec, params), 0.7f);
    }
}

/* ------------------------------------------------------------------ 汎用の見た目（スタイルの既定。スタイルの中から呼んでよい） */

void fx_emit_orb_body(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out)
{
    emit_orb_body(spec, params, t, out);
}

void fx_emit_embers(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out, const FxPalette &pal, float rate, float life,
    float fly, float size_scale)
{
    emit_embers(spec, params, t, out, pal, rate, life, fly, size_scale);
}

void fx_emit_burst(const FxSpec &spec, const VoxelFxParams &params, float te, const FxBurstMode &mode, FxFrame &out, bool with_light)
{
    emit_burst(spec, params, te, mode, out, with_light);
}

float fx_burst_seconds(const FxSpec &spec, const VoxelFxParams &params, const FxBurstMode &mode)
{
    return burst_seconds(spec, params, mode);
}

void fx_emit_beam_line(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out)
{
    emit_beam(spec, params, t, out);
}

void fx_emit_glow_mote(const FxSpec &spec, const VoxelFxParams &params, float t, FxFrame &out)
{
    emit_glow(spec, params, t, out);
}

namespace {

//! ボルト・ボールの着弾の爆発の出し方（ボールは閃光つきの大きな爆発、ボルトは小さな弾け）。
FxBurstMode orb_impact_mode(const FxSpec &spec, const VoxelFxParams &params)
{
    FxBurstMode mode;
    mode.ring_delay = params.ring_delay;
    mode.small = !spec.explode;
    mode.flash = spec.explode;
    return mode;
}

FxBurstMode beam_end_mode()
{
    FxBurstMode mode;
    mode.small = true;
    mode.flash = false;
    return mode;
}

FxBurstMode breath_mode(const VoxelFxParams &params)
{
    FxBurstMode mode;
    mode.ring_delay = params.breath_ring_delay;
    mode.flash = false;
    return mode;
}

FxBurstMode jump_mode(const VoxelFxParams &params)
{
    FxBurstMode mode;
    mode.ring_delay = params.ring_delay;
    mode.flash = true;
    return mode;
}

} // namespace

void fx_generic_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    emit_orb_body(ctx.spec, ctx.params, ctx.t, out);
    emit_embers(ctx.spec, ctx.params, ctx.t, out, ctx.spec.palette, ctx.params.ember_rate, ctx.params.ember_life, ctx.fly, 1.f);
}

void fx_generic_impact(const FxStyleCtx &ctx, FxFrame &out)
{
    const FxBurstMode mode = orb_impact_mode(ctx.spec, ctx.params);
    if (ctx.te < burst_seconds(ctx.spec, ctx.params, mode)) {
        emit_burst(ctx.spec, ctx.params, ctx.te, mode, out, true);
    }
    emit_embers(ctx.spec, ctx.params, ctx.t, out, ctx.spec.palette, ctx.params.ember_rate, ctx.params.ember_life, ctx.fly, 1.f);
}

float fx_generic_impact_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    return std::max(burst_seconds(spec, params, orb_impact_mode(spec, params)), params.ember_life);
}

void fx_generic_beam(const FxStyleCtx &ctx, FxFrame &out)
{
    emit_beam(ctx.spec, ctx.params, ctx.t, out);
    const float segments = static_cast<float>((ctx.spec.route.size() > 1) ? (ctx.spec.route.size() - 1) : 0);
    const FxBurstMode mode = beam_end_mode();
    const float te = ctx.t - (segments * ctx.params.beam_cell_delay);
    if ((te >= 0.f) && (te < burst_seconds(ctx.spec, ctx.params, mode))) {
        emit_burst(ctx.spec, ctx.params, te, mode, out, false);
    }
}

float fx_generic_beam_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const float segments = static_cast<float>((spec.route.size() > 1) ? (spec.route.size() - 1) : 0);
    return (segments * params.beam_cell_delay) + std::max(params.beam_life, burst_seconds(spec, params, beam_end_mode()));
}

void fx_generic_breath(const FxStyleCtx &ctx, FxFrame &out)
{
    emit_burst(ctx.spec, ctx.params, ctx.t, breath_mode(ctx.params), out, true);
}

float fx_generic_breath_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    return burst_seconds(spec, params, breath_mode(params));
}

void fx_generic_burst(const FxStyleCtx &ctx, FxFrame &out)
{
    emit_burst(ctx.spec, ctx.params, ctx.t, jump_mode(ctx.params), out, true);
}

float fx_generic_burst_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    return burst_seconds(spec, params, jump_mode(params));
}

void fx_generic_glow(const FxStyleCtx &ctx, FxFrame &out)
{
    emit_glow(ctx.spec, ctx.params, ctx.t, out);
    if (ctx.spec.glow_mode == FxGlowMode::AllInSight) {
        // 視界内の全員にかかる魔法：属性の色の光が、画面の端から端へ走る。
        fx_emit_all_sight_wave(ctx, out, ctx.spec.palette.orb[1]);
    }
}

float fx_generic_glow_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    (void)spec;
    return params.glow_seconds;
}

} // namespace hd2d
