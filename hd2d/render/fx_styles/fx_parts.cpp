/*!
 * @file fx_parts.cpp
 * @brief `fx_parts.h` の中身。
 */
#include "render/fx_styles/fx_parts.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>

namespace hd2d {

namespace {

constexpr float kPi = 3.14159265f;

std::uint32_t mix_bits(std::uint32_t h)
{
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

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

} // namespace

/* ============================================================ 基本 */

float fx_rand(std::uint32_t seed, std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
    std::uint32_t h = mix_bits(seed + 0x9e3779b9u);
    h = mix_bits(h ^ (a * 0x85ebca6bu));
    h = mix_bits(h ^ (b * 0xc2b2ae35u));
    h = mix_bits(h ^ (c * 0x27d4eb2fu));
    return static_cast<float>(h >> 8) / 16777216.f;
}

float fx_clamp01(float v)
{
    return std::min(std::max(v, 0.f), 1.f);
}

float fx_lerp(float a, float b, float t)
{
    return a + ((b - a) * t);
}

FxColour fx_mix(const FxColour &a, const FxColour &b, float t)
{
    return FxColour{ fx_lerp(a.r, b.r, t), fx_lerp(a.g, b.g, t), fx_lerp(a.b, b.b, t) };
}

FxColour fx_scale(const FxColour &a, float k)
{
    return FxColour{ a.r * k, a.g * k, a.b * k };
}

float fx_ramp(float t, float t0, float t1)
{
    if (t1 <= t0) {
        return (t >= t1) ? 1.f : 0.f;
    }
    const float u = fx_clamp01((t - t0) / (t1 - t0));
    return u * u * (3.f - (2.f * u));
}

float fx_envelope(float t, float attack, float hold, float release)
{
    if (t < 0.f) {
        return 0.f;
    }
    if (t < attack) {
        return (attack > 1e-6f) ? (t / attack) : 1.f;
    }
    if (t < (attack + hold)) {
        return 1.f;
    }
    const float r = t - attack - hold;
    return (r < release) ? (1.f - (r / std::max(release, 1e-6f))) : 0.f;
}

Vec3 fx_rotate_local(const Vec3 &v, float yaw, float pitch)
{
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float x1 = (v.x * cp) - (v.z * sp);
    const float z1 = (v.x * sp) + (v.z * cp);
    return Vec3{ (x1 * cy) - (v.y * sy), (x1 * sy) + (v.y * cy), z1 };
}

void fx_direction_angles(const Vec3 &d, float &yaw, float &pitch)
{
    yaw = std::atan2(d.y, d.x);
    pitch = std::asin(std::min(std::max(d.z, -1.f), 1.f));
}

Vec3 fx_cell_centre(int gx, int gy, float z)
{
    return Vec3{ static_cast<float>(gx) + 0.5f, static_cast<float>(gy) + 0.5f, z };
}

Vec3 fx_target_centre(const FxSpec &spec, float z)
{
    return fx_cell_centre(spec.centre_gx, spec.centre_gy, z);
}

Vec3 fx_source_centre(const FxSpec &spec, float z)
{
    return fx_cell_centre(spec.src_gx, spec.src_gy, z);
}

Vec3 fx_toward_camera(const FxSpec &spec)
{
    const Vec3 n = cross(spec.cam_up, spec.cam_right);
    return (length_of(n) < 1e-4f) ? Vec3{ 0.f, 1.f, 0.5f } : normalize(n);
}

/* ============================================================ 粒 */

namespace {

Vec3 particle_at(const FxParticle &p, float a)
{
    Vec3 r;
    if (p.drag < 1e-4f) {
        r = p.pos + (p.vel * a);
        r.z += 0.5f * p.gravity * a * a;
        return r;
    }
    const float k = p.drag;
    const float h = (1.f - std::exp(-k * a)) / k;
    r.x = p.pos.x + (p.vel.x * h);
    r.y = p.pos.y + (p.vel.y * h);
    //! 終端速度 g/k へ指数で近づく：v(a) = g/k + (v0 - g/k) e^(-k a)。位置はその積分。
    r.z = p.pos.z + ((p.vel.z - (p.gravity / k)) * h) + ((p.gravity / k) * a);
    return r;
}

} // namespace

Vec3 fx_particle_position(const FxParticle &p, float age, bool *hit, float *hit_age)
{
    if (hit != nullptr) {
        *hit = false;
    }
    age = std::max(age, 0.f);
    if (p.floor == FxFloor::None) {
        return particle_at(p, age);
    }
    if (p.pos.z <= p.floor_z) {
        Vec3 r = p.pos;
        r.z = p.floor_z;
        if (hit != nullptr) {
            *hit = true;
        }
        if (hit_age != nullptr) {
            *hit_age = 0.f;
        }
        return r;
    }
    constexpr int kSteps = 24;
    float prev_a = 0.f;
    float prev_z = p.pos.z;
    for (int i = 1; i <= kSteps; ++i) {
        const float a = age * static_cast<float>(i) / static_cast<float>(kSteps);
        const Vec3 now = particle_at(p, a);
        if (now.z <= p.floor_z) {
            const float span = prev_z - now.z;
            const float k = (span > 1e-6f) ? ((prev_z - p.floor_z) / span) : 1.f;
            const float ha = prev_a + ((a - prev_a) * k);
            Vec3 r = particle_at(p, ha);
            r.z = p.floor_z;
            if (hit != nullptr) {
                *hit = true;
            }
            if (hit_age != nullptr) {
                *hit_age = ha;
            }
            return r;
        }
        prev_a = a;
        prev_z = now.z;
    }
    return particle_at(p, age);
}

bool fx_emit_particle(const FxParticle &p, float t, FxFrame &out)
{
    const float age = t - p.born;
    if ((age < 0.f) || (age >= p.life)) {
        return false;
    }
    const float u = age / std::max(p.life, 1e-6f);
    bool hit = false;
    Vec3 pos = fx_particle_position(p, age, &hit);
    if (hit && (p.floor == FxFloor::Vanish)) {
        return false;
    }
    FxVoxel v;
    const float size = std::max(0.004f, fx_lerp(p.size0, p.size1, u));
    v.size = Vec3{ size, size, size };
    if (hit) {
        pos.z = p.floor_z + (size * 0.5f);
    }
    v.center = pos;
    const FxColour c = fx_mix(p.colour0, p.colour1, u);
    v.r = c.r;
    v.g = c.g;
    v.b = c.b;
    v.emissive = fx_lerp(p.emissive0, p.emissive1, u);
    v.alpha = fx_lerp(p.alpha0, p.alpha1, u);
    v.part = p.part;
    if (v.alpha <= 0.002f) {
        return false;
    }
    out.voxels.push_back(v);
    return true;
}

int fx_emit_spray(const FxSpray &s, float t, FxFrame &out)
{
    int emitted = 0;
    Vec3 b1;
    Vec3 b2;
    const Vec3 dir = (length_of(s.dir) < 1e-5f) ? Vec3{ 0.f, 0.f, 1.f } : normalize(s.dir);
    perpendiculars(dir, b1, b2);
    for (int i = 0; i < s.count; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        FxParticle p;
        p.born = s.born + ((s.duration > 0.f) ? (s.duration * static_cast<float>(i) / static_cast<float>(std::max(s.count, 1))) : 0.f);
        p.life = fx_lerp(s.life_min, s.life_max, fx_rand(s.seed, id, 1u));
        const float age = t - p.born;
        if ((age < 0.f) || (age >= p.life)) {
            continue;
        }
        const float cone = std::min(s.cone, kPi);
        const float theta = cone * std::sqrt(fx_rand(s.seed, id, 2u));
        const float phi = 2.f * kPi * fx_rand(s.seed, id, 3u);
        const Vec3 d = (dir * std::cos(theta)) + (((b1 * std::cos(phi)) + (b2 * std::sin(phi))) * std::sin(theta));
        const float speed = fx_lerp(s.speed_min, s.speed_max, fx_rand(s.seed, id, 4u));
        p.vel = d * speed;
        const float sr = s.scatter * std::sqrt(fx_rand(s.seed, id, 5u));
        const float sa = 2.f * kPi * fx_rand(s.seed, id, 6u);
        p.pos = s.origin + Vec3{ sr * std::cos(sa), sr * std::sin(sa), 0.f };
        p.gravity = s.gravity;
        p.drag = s.drag;
        p.colour0 = s.colour0;
        p.colour1 = s.colour1;
        p.size0 = s.size0 * (0.8f + (0.4f * fx_rand(s.seed, id, 7u)));
        p.size1 = s.size1;
        p.emissive0 = s.emissive0;
        p.emissive1 = s.emissive1;
        p.alpha0 = s.alpha0;
        p.alpha1 = s.alpha1;
        p.floor = s.floor;
        p.floor_z = s.floor_z;
        p.part = s.part;
        if (fx_emit_particle(p, t, out)) {
            ++emitted;
        }
    }
    return emitted;
}

/* ============================================================ 床の溜まり・煙 */

int fx_emit_puddle(const FxPuddle &p, float t, FxFrame &out)
{
    const float age = t - p.born;
    const float total = p.spread + p.hold + p.fade;
    if ((age < 0.f) || (age >= total) || (p.radius <= 0.f)) {
        return 0;
    }
    const float grow_u = (p.spread > 1e-6f) ? fx_clamp01(age / p.spread) : 1.f;
    const float grow = 1.f - ((1.f - grow_u) * (1.f - grow_u));
    const float radius = p.radius * grow;
    const float fade = (age > (p.spread + p.hold)) ? fx_clamp01(1.f - ((age - p.spread - p.hold) / std::max(p.fade, 1e-6f))) : 1.f;
    const float pitch = std::max(p.voxel, p.radius / 9.f);
    const int n = static_cast<int>(std::ceil(p.radius * 1.5f / pitch));
    const float ph1 = fx_rand(p.seed, 91u) * 2.f * kPi;
    const float ph2 = fx_rand(p.seed, 92u) * 2.f * kPi;
    const float ph3 = fx_rand(p.seed, 93u) * 2.f * kPi;
    int emitted = 0;
    for (int i = -n; i <= n; ++i) {
        for (int j = -n; j <= n; ++j) {
            const float x = static_cast<float>(i) * pitch;
            const float y = static_cast<float>(j) * pitch;
            const float d = std::sqrt((x * x) + (y * y));
            const float ang = std::atan2(y, x);
            const float wobble = 1.f + (p.ragged * 0.5f * (std::sin((2.f * ang) + ph1) + (0.6f * std::sin((3.f * ang) + ph2)) + (0.4f * std::sin((5.f * ang) + ph3))) / 2.f);
            const float edge = radius * wobble;
            if (d > edge) {
                continue;
            }
            FxVoxel v;
            const float thick = p.thickness * (0.7f + (0.6f * fx_rand(p.seed, static_cast<std::uint32_t>(i + 64), static_cast<std::uint32_t>(j + 64))));
            v.center = Vec3{ p.cx + x, p.cy + y, p.z + (thick * 0.5f) };
            v.size = Vec3{ pitch * 1.04f, pitch * 1.04f, thick };
            v.r = p.colour.r;
            v.g = p.colour.g;
            v.b = p.colour.b;
            v.emissive = p.emissive;
            v.alpha = p.alpha * fade * ((d > (edge * 0.78f)) ? 0.7f : 1.f);
            v.part = p.part;
            if (v.alpha > 0.01f) {
                out.voxels.push_back(v);
                ++emitted;
            }
        }
    }
    return emitted;
}

int fx_emit_smoke(const FxSmoke &s, float t, FxFrame &out)
{
    int emitted = 0;
    for (int i = 0; i < s.count; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float born = s.born + ((s.duration > 0.f) ? (s.duration * static_cast<float>(i) / static_cast<float>(std::max(s.count, 1))) : 0.f);
        const float age = t - born;
        if ((age < 0.f) || (age >= s.life)) {
            continue;
        }
        const float u = age / std::max(s.life, 1e-6f);
        const float rr = s.radius * std::sqrt(fx_rand(s.seed, id, 1u));
        const float ra = 2.f * kPi * fx_rand(s.seed, id, 2u);
        FxVoxel v;
        const float size = s.size * fx_lerp(1.f, s.grow, u) * (0.8f + (0.4f * fx_rand(s.seed, id, 3u)));
        v.size = Vec3{ size, size, size };
        v.center = s.origin + Vec3{ (rr * std::cos(ra)) + (s.drift_x * age), (rr * std::sin(ra)) + (s.drift_y * age), s.rise * age * (0.8f + (0.4f * fx_rand(s.seed, id, 4u))) };
        v.yaw = fx_rand(s.seed, id, 5u) * kPi * 0.5f;
        const FxColour c = fx_mix(s.colour, s.colour_end, u);
        v.r = c.r;
        v.g = c.g;
        v.b = c.b;
        v.emissive = s.emissive * (1.f - u);
        v.alpha = s.alpha * std::min(1.f, u * 5.f) * (1.f - u);
        v.part = s.part;
        if (v.alpha > 0.01f) {
            out.voxels.push_back(v);
            ++emitted;
        }
    }
    return emitted;
}

/* ============================================================ 稲妻 */

std::vector<Vec3> fx_zigzag_points(const Vec3 &a, const Vec3 &b, int segments, float jitter, std::uint32_t seed, std::uint32_t frame)
{
    segments = std::max(segments, 1);
    std::vector<Vec3> pts;
    pts.reserve(static_cast<std::size_t>(segments) + 1u);
    const Vec3 d = b - a;
    const float len = length_of(d);
    Vec3 n1{ 0.f, 0.f, 1.f };
    Vec3 n2{ 1.f, 0.f, 0.f };
    if (len > 1e-5f) {
        perpendiculars(d * (1.f / len), n1, n2);
    }
    pts.push_back(a);
    for (int i = 1; i < segments; ++i) {
        const float k = static_cast<float>(i) / static_cast<float>(segments);
        const float taper = std::sin(kPi * k);
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float o1 = ((fx_rand(seed, id, frame, 1u) * 2.f) - 1.f) * jitter * taper;
        const float o2 = ((fx_rand(seed, id, frame, 2u) * 2.f) - 1.f) * jitter * taper * 0.6f;
        pts.push_back(a + (d * k) + (n1 * o1) + (n2 * o2));
    }
    pts.push_back(b);
    return pts;
}

namespace {

int emit_polyline(const std::vector<Vec3> &pts, float core, float glow, const FxColour &core_c, const FxColour &glow_c, float emissive,
    float glow_alpha, float alpha, FxFrame &out)
{
    int emitted = 0;
    for (std::size_t i = 0; (i + 1) < pts.size(); ++i) {
        const Vec3 d = pts[i + 1] - pts[i];
        const float len = length_of(d);
        if (len < 1e-4f) {
            continue;
        }
        float yaw = 0.f;
        float pitch = 0.f;
        fx_direction_angles(d * (1.f / len), yaw, pitch);
        const Vec3 mid = pts[i] + (d * 0.5f);
        FxVoxel g;
        g.center = mid;
        g.size = Vec3{ len * 1.08f, glow, glow };
        g.r = glow_c.r;
        g.g = glow_c.g;
        g.b = glow_c.b;
        g.alpha = glow_alpha * alpha;
        g.emissive = emissive * 0.5f;
        g.yaw = yaw;
        g.pitch = pitch;
        g.part = FxPart::BoltGlow;
        out.voxels.push_back(g);
        FxVoxel c = g;
        c.size = Vec3{ len * 1.04f, core, core };
        c.r = core_c.r;
        c.g = core_c.g;
        c.b = core_c.b;
        c.alpha = alpha;
        c.emissive = emissive;
        c.part = FxPart::Bolt;
        out.voxels.push_back(c);
        emitted += 2;
    }
    // 折れ目のすきまを埋める小さな立方体。
    for (std::size_t i = 1; (i + 1) < pts.size(); ++i) {
        FxVoxel j;
        j.center = pts[i];
        j.size = Vec3{ core * 1.15f, core * 1.15f, core * 1.15f };
        j.r = core_c.r;
        j.g = core_c.g;
        j.b = core_c.b;
        j.alpha = alpha;
        j.emissive = emissive;
        j.part = FxPart::Bolt;
        out.voxels.push_back(j);
        ++emitted;
    }
    return emitted;
}

} // namespace

int fx_emit_lightning(const FxLightning &l, float t, FxFrame &out)
{
    const std::uint32_t frame = static_cast<std::uint32_t>(std::max(0.f, std::floor(t * l.flicker_hz)));
    const std::vector<Vec3> pts = fx_zigzag_points(l.a, l.b, l.segments, l.jitter, l.seed, frame);
    int emitted = emit_polyline(pts, l.core, l.glow, l.core_colour, l.glow_colour, l.emissive, l.glow_alpha, l.alpha, out);
    const Vec3 span = l.b - l.a;
    const float span_len = length_of(span);
    for (int b = 0; b < l.branches; ++b) {
        if (pts.size() < 4u) {
            break;
        }
        const std::uint32_t id = static_cast<std::uint32_t>(100 + b);
        const std::size_t k = 1u + static_cast<std::size_t>(fx_rand(l.seed, id, frame, 1u) * static_cast<float>(pts.size() - 3u));
        Vec3 n1{ 0.f, 0.f, 1.f };
        Vec3 n2{ 1.f, 0.f, 0.f };
        if (span_len > 1e-5f) {
            perpendiculars(span * (1.f / span_len), n1, n2);
        }
        const float ang = 2.f * kPi * fx_rand(l.seed, id, frame, 2u);
        const Vec3 side = (n1 * std::cos(ang)) + (n2 * std::sin(ang));
        const Vec3 end = pts[k] + (side * (span_len * 0.18f)) + (span * 0.12f);
        const std::vector<Vec3> branch = fx_zigzag_points(pts[k], end, 3, l.jitter * 0.5f, l.seed + 7u, frame + id);
        emitted += emit_polyline(branch, l.core * 0.65f, l.glow * 0.6f, l.core_colour, l.glow_colour, l.emissive * 0.8f, l.glow_alpha, l.alpha * 0.9f, out);
    }
    return emitted;
}

/* ============================================================ 輪 */

int fx_emit_ring(const FxRing &r, float t, FxFrame &out)
{
    const float age = t - r.born;
    if ((age < 0.f) || (age >= r.life)) {
        return 0;
    }
    const float u = age / std::max(r.life, 1e-6f);
    const float radius = fx_lerp(r.r0, r.r1, u);
    const float alpha = r.alpha * (r.fade_out ? (1.f - (u * u)) : 1.f);
    if ((alpha <= 0.01f) || (radius <= 0.005f)) {
        return 0;
    }
    const Vec3 n = (length_of(r.normal) < 1e-5f) ? Vec3{ 0.f, 0.f, 1.f } : normalize(r.normal);
    Vec3 e1;
    Vec3 e2;
    perpendiculars(n, e1, e2);
    const int segs = std::max(r.segments, 6);
    const float step = 2.f * kPi / static_cast<float>(segs);
    const float chord = 2.f * radius * std::sin(step * 0.5f);
    int emitted = 0;
    for (int i = 0; i < segs; ++i) {
        if ((r.dash > 1) && ((i % r.dash) != 0)) {
            continue;
        }
        const float a0 = (step * static_cast<float>(i)) + r.spin;
        const float a1 = a0 + step;
        const Vec3 p0 = r.centre + (((e1 * std::cos(a0)) + (e2 * std::sin(a0))) * radius);
        const Vec3 p1 = r.centre + (((e1 * std::cos(a1)) + (e2 * std::sin(a1))) * radius);
        const Vec3 d = p1 - p0;
        const float len = length_of(d);
        if (len < 1e-5f) {
            continue;
        }
        float yaw = 0.f;
        float pitch = 0.f;
        fx_direction_angles(d * (1.f / len), yaw, pitch);
        FxVoxel v;
        v.center = p0 + (d * 0.5f);
        v.size = Vec3{ std::max(chord, len) * 1.08f, r.thickness, r.thickness };
        v.r = r.colour.r;
        v.g = r.colour.g;
        v.b = r.colour.b;
        v.alpha = alpha;
        v.emissive = r.emissive;
        v.yaw = yaw;
        v.pitch = pitch;
        v.part = r.part;
        out.voxels.push_back(v);
        ++emitted;
    }
    return emitted;
}

/* ============================================================ 画面の揺れ・光の波 */

float fx_shake_envelope(float t, float start, float seconds, float strength)
{
    if ((t < start) || (t >= (start + seconds)) || (seconds <= 0.f)) {
        return 0.f;
    }
    const float u = (t - start) / seconds;
    return strength * (1.f - u) * (1.f - u);
}

void fx_add_shake(FxFrame &out, float strength)
{
    out.shake = std::max(out.shake, std::max(strength, 0.f));
}

Vec3 fx_shake_offset(float strength, std::uint32_t time_ms)
{
    if (strength <= 0.f) {
        return Vec3{};
    }
    const std::uint32_t frame = time_ms / 33u; //!< 約 30 回/秒で向きを変える
    return Vec3{ ((fx_rand(0x5a4eu, frame, 1u) * 2.f) - 1.f) * strength, ((fx_rand(0x5a4eu, frame, 2u) * 2.f) - 1.f) * strength, 0.f };
}

void fx_add_screen_wave(FxFrame &out, const FxColour &colour, float position, float width, float alpha, float angle)
{
    FxScreenWave w;
    w.colour = colour;
    w.position = position;
    w.width = width;
    w.alpha = alpha;
    w.angle = angle;
    out.screen_waves.push_back(w);
}

void fx_emit_all_sight_wave(const FxStyleCtx &ctx, FxFrame &out, const FxColour &colour, float seconds)
{
    const float u = ctx.t / std::max(seconds, 1e-3f);
    if ((u < 0.f) || (u > 1.25f)) {
        return;
    }
    const float envelope = std::sin(kPi * fx_clamp01(u / 1.25f));
    fx_add_screen_wave(out, colour, (u * 1.3f) - 0.15f, 0.22f, 0.42f * envelope, 0.f);
    fx_add_screen_wave(out, colour, (u * 1.3f) - 0.30f, 0.10f, 0.22f * envelope, 0.f);
}

/* ============================================================ 対象の見た目の変化 */

FxEntityMod &fx_mod_for(FxFrame &out, int gx, int gy)
{
    for (FxEntityMod &m : out.entity_mods) {
        if ((m.gx == gx) && (m.gy == gy)) {
            return m;
        }
    }
    FxEntityMod m;
    m.gx = gx;
    m.gy = gy;
    out.entity_mods.push_back(m);
    return out.entity_mods.back();
}

/* ============================================================ PNG */

bool fx_load_png(const std::string &path, FxTileImage &out)
{
    SDL_Surface *const raw = IMG_Load(path.c_str());
    if (raw == nullptr) {
        return false;
    }
    SDL_Surface *const rgba = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(raw);
    if (rgba == nullptr) {
        return false;
    }
    out.width = rgba->w;
    out.height = rgba->h;
    out.rgba.assign(static_cast<std::size_t>(rgba->w) * static_cast<std::size_t>(rgba->h) * 4u, 0);
    const auto *src = static_cast<const std::uint8_t *>(rgba->pixels);
    for (int y = 0; y < rgba->h; ++y) {
        std::copy(src + (static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->pitch)),
            src + (static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->pitch)) + (static_cast<std::size_t>(rgba->w) * 4u),
            out.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->w) * 4u));
    }
    SDL_FreeSurface(rgba);
    return true;
}

namespace {

//! 画素の平均（領域 [x0,x1)×[y0,y1)。α で重みを付け、α の平均も返す）。
struct AveragePixel {
    float r{ 0.f };
    float g{ 0.f };
    float b{ 0.f };
    float a{ 0.f };
};

AveragePixel average_region(const FxTileImage &img, int x0, int y0, int x1, int y1)
{
    AveragePixel out;
    float weight = 0.f;
    int count = 0;
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(img.width, std::max(x1, x0 + 1));
    y1 = std::min(img.height, std::max(y1, y0 + 1));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::size_t i = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width)) + static_cast<std::size_t>(x)) * 4u;
            const float a = static_cast<float>(img.rgba[i + 3]) / 255.f;
            out.r += static_cast<float>(img.rgba[i]) / 255.f * a;
            out.g += static_cast<float>(img.rgba[i + 1]) / 255.f * a;
            out.b += static_cast<float>(img.rgba[i + 2]) / 255.f * a;
            out.a += a;
            weight += a;
            ++count;
        }
    }
    if (weight > 1e-4f) {
        out.r /= weight;
        out.g /= weight;
        out.b /= weight;
    }
    out.a = (count > 0) ? (out.a / static_cast<float>(count)) : 0.f;
    return out;
}

float srgb_to_linear(float c)
{
    return std::pow(std::max(c, 0.f), 2.2f);
}

} // namespace

/* ============================================================ 対象のタイルの写し */

int fx_emit_tile_ghost(const FxSpec &spec, const FxGhost &g, float t, FxFrame &out)
{
    const float age = t - g.born;
    const float total = g.hold + g.crumble + g.fall_life;
    if ((age < 0.f) || (age >= total)) {
        return 0;
    }
    FxTileImage img;
    bool have = spec.tile_at && spec.tile_at(g.gx, g.gy, img) && (img.width > 0) && (img.height > 0)
        && (img.rgba.size() >= (static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4u));
    if (!have) {
        // 引けないとき：無地の四角で代用する。
        img = FxTileImage{};
        img.width = 8;
        img.height = 8;
        img.rgba.assign(8u * 8u * 4u, 255);
    }
    const int n = std::max(4, g.grid);
    const float aspect = img.height_cells / std::max(img.width_cells, 1e-3f);
    const int m = std::max(2, static_cast<int>(std::lround(static_cast<float>(n) * aspect)));
    const float cell_w = img.width_cells / static_cast<float>(n);
    const float cell_h = img.height_cells / static_cast<float>(m);
    const Vec3 foot = fx_cell_centre(g.gx, g.gy, 0.f) + (fx_toward_camera(spec) * g.offset);
    int emitted = 0;
    for (int j = 0; j < m; ++j) {
        for (int i = 0; i < n; ++i) {
            const AveragePixel px = average_region(img, i * img.width / n, j * img.height / m, ((i + 1) * img.width) / n, ((j + 1) * img.height) / m);
            if (px.a < 0.4f) {
                continue;
            }
            const std::uint32_t id = static_cast<std::uint32_t>((j * 64) + i);
            const float fx = (static_cast<float>(i) + 0.5f) / static_cast<float>(n) - 0.5f;
            const float fz = (static_cast<float>(m - j) - 0.5f) / static_cast<float>(m); //!< 0 = 足元、1 = 頭
            const FxColour src{ srgb_to_linear(px.r), srgb_to_linear(px.g), srgb_to_linear(px.b) };
            const float shade = 0.75f + (0.25f * fx_rand(g.seed, id, 9u));
            const FxColour base = fx_scale(fx_mix(src, g.colour, g.tint), shade);
            const Vec3 rest = foot + (spec.cam_right * (fx * img.width_cells)) + Vec3{ 0.f, 0.f, fz * img.height_cells + 0.02f };
            const float trigger = g.hold + (g.crumble * ((0.65f * (1.f - fz)) + (0.35f * fx_rand(g.seed, id, 1u))));
            const float size = std::max(cell_w, cell_h) * 1.02f;
            if (age < trigger) {
                FxVoxel v;
                v.center = rest;
                v.size = Vec3{ size, size, size };
                v.r = base.r;
                v.g = base.g;
                v.b = base.b;
                v.emissive = g.emissive;
                v.part = FxPart::Ghost;
                out.voxels.push_back(v);
                ++emitted;
            } else {
                FxParticle p;
                p.pos = rest;
                p.vel = Vec3{ (fx_rand(g.seed, id, 2u) - 0.5f) * 0.5f, (fx_rand(g.seed, id, 3u) - 0.5f) * 0.5f, fx_rand(g.seed, id, 4u) * 0.3f };
                p.gravity = -7.f;
                p.drag = 0.3f;
                p.born = g.born + trigger;
                p.life = g.fall_life * (0.6f + (0.4f * fx_rand(g.seed, id, 5u)));
                p.colour0 = base;
                p.colour1 = fx_scale(base, 0.6f);
                p.size0 = size;
                p.size1 = size * 0.6f;
                p.emissive0 = g.emissive;
                p.emissive1 = 0.f;
                p.alpha0 = 1.f;
                p.alpha1 = 0.f;
                p.floor = FxFloor::Stop;
                p.floor_z = 0.f;
                p.part = FxPart::Ghost;
                if (fx_emit_particle(p, t, out)) {
                    ++emitted;
                }
            }
        }
    }
    return emitted;
}

/* ============================================================ 文字の格子から作る形 */

FxShape fx_shape(std::initializer_list<const char *> rows)
{
    FxShape s;
    for (const char *row : rows) {
        s.rows.emplace_back(row);
        s.width = std::max(s.width, static_cast<int>(s.rows.back().size()));
    }
    s.height = static_cast<int>(s.rows.size());
    return s;
}

int fx_emit_shape(const FxShape &shape, const FxShapeColours &colours, const FxShapePlace &place, FxFrame &out)
{
    int emitted = 0;
    const Vec3 normal = normalize(cross(place.right, place.up));
    const float cs = std::cos(place.rotate);
    const float sn = std::sin(place.rotate);
    const float cell = place.cell * place.scale;
    for (int y = 0; y < shape.height; ++y) {
        const std::string &row = shape.rows[static_cast<std::size_t>(y)];
        for (int x = 0; x < static_cast<int>(row.size()); ++x) {
            const char ch = row[static_cast<std::size_t>(x)];
            if ((ch == '.') || (ch == ' ')) {
                continue;
            }
            FxColour c = colours.base;
            const std::size_t k = colours.keys.find(ch);
            if ((k != std::string::npos) && (k < colours.colours.size())) {
                c = colours.colours[k];
            }
            const float px = (static_cast<float>(x) - (static_cast<float>(shape.width - 1) * 0.5f)) * cell;
            const float py = ((static_cast<float>(shape.height - 1) * 0.5f) - static_cast<float>(y)) * cell;
            const float rx = (px * cs) - (py * sn);
            const float ry = (px * sn) + (py * cs);
            for (int layer = 0; layer < std::max(place.layers, 1); ++layer) {
                const float depth_offset = (static_cast<float>(layer) - (static_cast<float>(std::max(place.layers, 1) - 1) * 0.5f)) * place.depth;
                FxVoxel v;
                v.center = place.centre + (place.right * rx) + (place.up * ry) + (normal * depth_offset);
                v.size = Vec3{ cell, cell, cell };
                v.r = c.r;
                v.g = c.g;
                v.b = c.b;
                v.alpha = place.alpha;
                v.emissive = place.emissive;
                v.part = place.part;
                out.voxels.push_back(v);
                ++emitted;
            }
        }
    }
    return emitted;
}

/* ============================================================ 絵の板 */

namespace {

std::string g_sprite_dir;
std::mutex g_sprite_mutex;
std::map<std::string, std::shared_ptr<FxTileImage>> g_sprite_cache; //!< 読めなかった絵は空の指し先

std::shared_ptr<FxTileImage> sprite_image(const std::string &name)
{
    std::lock_guard<std::mutex> lock(g_sprite_mutex);
    const auto it = g_sprite_cache.find(name);
    if (it != g_sprite_cache.end()) {
        return it->second;
    }
    std::shared_ptr<FxTileImage> image;
    if (!g_sprite_dir.empty()) {
        auto loaded = std::make_shared<FxTileImage>();
        if (fx_load_png(g_sprite_dir + "/" + name + ".png", *loaded)) {
            image = loaded;
        }
    }
    g_sprite_cache[name] = image;
    return image;
}

} // namespace

void fx_sprite_set_dir(const std::string &dir)
{
    std::lock_guard<std::mutex> lock(g_sprite_mutex);
    g_sprite_dir = dir;
    g_sprite_cache.clear();
}

const std::string &fx_sprite_dir()
{
    return g_sprite_dir;
}

bool fx_sprite_available(const char *name)
{
    return sprite_image(name) != nullptr;
}

int fx_emit_sprite(const FxSpec &spec, const FxSprite &sprite, FxFrame &out)
{
    const std::shared_ptr<FxTileImage> image = sprite_image(sprite.name);
    if (!image || (image->width <= 0) || (image->height <= 0) || (sprite.alpha <= 0.002f)) {
        return 0;
    }
    const int n = std::max(4, std::min(sprite.resolution, std::max(image->width, image->height)));
    const bool wide = image->width >= image->height;
    const int nx = wide ? n : std::max(1, static_cast<int>(std::lround(static_cast<float>(n) * static_cast<float>(image->width) / static_cast<float>(image->height))));
    const int ny = wide ? std::max(1, static_cast<int>(std::lround(static_cast<float>(n) * static_cast<float>(image->height) / static_cast<float>(image->width)))) : n;
    const float cell = sprite.size / static_cast<float>(wide ? nx : ny);
    const float cs = std::cos(sprite.rotate);
    const float sn = std::sin(sprite.rotate);
    int emitted = 0;
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const AveragePixel px = average_region(*image, i * image->width / nx, j * image->height / ny, ((i + 1) * image->width) / nx,
                ((j + 1) * image->height) / ny);
            if (px.a < 0.35f) {
                continue;
            }
            const float x = (static_cast<float>(i) + 0.5f - (static_cast<float>(nx) * 0.5f)) * cell;
            const float y = ((static_cast<float>(ny) * 0.5f) - (static_cast<float>(j) + 0.5f)) * cell;
            const float rx = (x * cs) - (y * sn);
            const float ry = (x * sn) + (y * cs);
            FxVoxel v;
            v.center = sprite.centre + (spec.cam_right * rx) + (spec.cam_up * ry);
            v.size = Vec3{ cell * 1.02f, cell * 1.02f, cell * 1.02f };
            v.r = srgb_to_linear(px.r) * sprite.tint.r;
            v.g = srgb_to_linear(px.g) * sprite.tint.g;
            v.b = srgb_to_linear(px.b) * sprite.tint.b;
            v.alpha = sprite.alpha * std::min(1.f, px.a * 1.4f);
            v.emissive = sprite.emissive;
            v.part = FxPart::Sprite;
            out.voxels.push_back(v);
            ++emitted;
        }
    }
    return emitted;
}

} // namespace hd2d
