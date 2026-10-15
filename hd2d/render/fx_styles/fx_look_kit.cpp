/*!
 * @file fx_look_kit.cpp
 * @brief `fx_look_kit.h` の中身。表（`Entry`）から登録表の段階の関数を結ぶ呼び分けと、見た目の関数が使う小さな道具。
 */
#include "render/fx_styles/fx_look_kit.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace hd2d {
namespace looks {

namespace {

//! 登録した表（`register_entries` が足す。登録表を作る 1 回だけ。以降は読むだけ）。
std::vector<std::pair<const Entry *, std::size_t>> &tables()
{
    static std::vector<std::pair<const Entry *, std::size_t>> t;
    return t;
}

const Entry *entry_of(const FxSpec &spec)
{
    for (const auto &table : tables()) {
        for (std::size_t i = 0; i < table.second; ++i) {
            if (spec.attr == table.first[i].attr) {
                return &table.first[i];
            }
        }
    }
    return nullptr;
}

} // namespace

/* ============================================================ 小さな道具 */

Look make_look(const FxSpec &spec, const Entry &e, int gx, int gy, float te, float scale, bool mini, std::uint32_t seed, float forward)
{
    const Vec3 toward = fx_toward_camera(spec);
    Vec3 flat{ toward.x, toward.y, 0.f };
    const float len = std::sqrt(dot(flat, flat));
    flat = (len < 1e-4f) ? Vec3{ 0.f, 1.f, 0.f } : (flat * (1.f / len));
    const Vec3 foot = fx_cell_centre(gx, gy, 0.f);
    return Look{ spec, e, gx, gy, foot, foot + (flat * forward), te, scale, mini, seed };
}

int count_of(const Look &L, int n)
{
    return L.mini ? std::max(2, n / 4) : n;
}

void put(FxFrame &out, const Vec3 &pos, float size, const FxColour &c, float emissive, float alpha, FxPart part)
{
    if (alpha <= 0.004f) {
        return;
    }
    FxVoxel v;
    v.center = pos;
    v.size = Vec3{ size, size, size };
    v.r = c.r;
    v.g = c.g;
    v.b = c.b;
    v.emissive = emissive;
    v.alpha = std::min(alpha, 1.f);
    v.part = part;
    out.voxels.push_back(v);
}

void put_bar(FxFrame &out, const Vec3 &centre, const Vec3 &dir, float len, float w, const FxColour &c, float emissive, float alpha, FxPart part)
{
    if (alpha <= 0.004f) {
        return;
    }
    FxVoxel v;
    v.center = centre;
    v.size = Vec3{ len, w, w };
    v.r = c.r;
    v.g = c.g;
    v.b = c.b;
    v.emissive = emissive;
    v.alpha = std::min(alpha, 1.f);
    fx_direction_angles(dir, v.yaw, v.pitch);
    v.part = part;
    out.voxels.push_back(v);
}

Vec3 on_screen(const FxSpec &spec, float x, float y)
{
    return (spec.cam_right * x) + (spec.cam_up * y);
}

void add_light(FxFrame &out, const Vec3 &p, float radius, const FxColour &c, float intensity)
{
    if (intensity <= 0.01f) {
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

float ease_out(float u)
{
    u = fx_clamp01(u);
    return 1.f - ((1.f - u) * (1.f - u));
}

FxColour lerp_colour(const FxColour &a, const FxColour &b, float t)
{
    return fx_mix(a, b, fx_clamp01(t));
}

Vec3 unit_or(const Vec3 &v, const Vec3 &fallback)
{
    return (dot(v, v) < 1e-8f) ? fallback : normalize(v);
}

void glyph(const Look &L, FxFrame &out, const FxShape &shape, const Vec3 &centre, float cell, float alpha, const FxColour &base, float emissive, int layers,
    float rotate, const std::string &keys, std::initializer_list<FxColour> colours)
{
    if (alpha <= 0.004f) {
        return;
    }
    FxShapePlace place;
    place.centre = centre;
    place.right = L.spec.cam_right;
    place.up = L.spec.cam_up;
    place.cell = cell;
    place.layers = layers;
    place.depth = cell * 0.9f;
    place.rotate = rotate;
    place.alpha = alpha;
    place.emissive = emissive;
    FxShapeColours sc;
    sc.base = base;
    sc.keys = keys;
    sc.colours.assign(colours.begin(), colours.end());
    (void)fx_emit_shape(shape, sc, place, out);
}

void ring_at(const Look &L, FxFrame &out, bool flat, const Vec3 &centre, float r0, float r1, float born, float life, float thickness, const FxColour &c,
    float emissive, float alpha, int segments, int dash, float spin)
{
    FxRing ring;
    ring.centre = centre;
    ring.normal = flat ? Vec3{ 0.f, 0.f, 1.f } : fx_toward_camera(L.spec);
    ring.r0 = r0;
    ring.r1 = r1;
    ring.born = born;
    ring.life = life;
    ring.thickness = thickness;
    ring.segments = segments;
    ring.colour = c;
    ring.emissive = emissive;
    ring.alpha = alpha;
    ring.dash = dash;
    ring.spin = spin;
    ring.seed = L.seed;
    (void)fx_emit_ring(ring, L.te, out);
}

void cell_mist(const FxSpec &spec, const Entry &e, int gx, int gy, int index, float te, FxFrame &out)
{
    FxSmoke smoke;
    smoke.origin = fx_cell_centre(gx, gy, 0.1f);
    smoke.radius = 0.22f;
    smoke.born = 0.f;
    smoke.duration = 0.2f;
    smoke.life = 0.7f;
    smoke.count = 2;
    smoke.rise = 0.4f;
    smoke.size = 0.15f;
    smoke.grow = 1.5f;
    smoke.colour = e.base;
    smoke.colour_end = e.tint;
    smoke.alpha = 0.55f;
    smoke.emissive = 0.9f;
    smoke.seed = spec.seed + static_cast<std::uint32_t>(index * 131);
    (void)fx_emit_smoke(smoke, te, out);
}

FlightPose flight_pose(const FxStyleCtx &ctx)
{
    const float fly = std::max(ctx.fly, 1e-4f);
    FlightPose f;
    f.s = fx_clamp01(ctx.t / fly);
    f.pos = fx_flight_position(ctx.spec, ctx.params, f.s);
    const Vec3 prev = fx_flight_position(ctx.spec, ctx.params, std::max(0.f, f.s - 0.02f));
    f.dir = unit_or(f.pos - prev, Vec3{ 1.f, 0.f, 0.f });
    return f;
}

void mark_projectile(FxFrame &out, const Vec3 &pos, const Vec3 &dir)
{
    out.projectile_visible = true;
    out.projectile_pos = pos;
    out.projectile_dir = dir;
}

void small_orb(const FxStyleCtx &ctx, const Entry &e, float big, FxFrame &out)
{
    const FlightPose f = flight_pose(ctx);
    const float fly = std::max(ctx.fly, 1e-4f);
    const FxColour core = fx_mix(FxColour{ 1.f, 1.f, 1.f }, e.base, 0.45f);
    put(out, f.pos, 0.14f * big, core, 3.6f, 1.f, FxPart::OrbCore);
    const float o = 0.085f * big;
    const Vec3 offs[6] = { { o, 0.f, 0.f }, { -o, 0.f, 0.f }, { 0.f, o, 0.f }, { 0.f, -o, 0.f }, { 0.f, 0.f, o }, { 0.f, 0.f, -o } };
    for (const Vec3 &d : offs) {
        put(out, f.pos + d, 0.085f * big, e.base, 2.2f, 0.9f, FxPart::OrbCore);
    }
    for (int k = 1; k <= 8; ++k) {
        const float tt = ctx.t - (0.02f * static_cast<float>(k));
        if (tt <= 0.f) {
            break;
        }
        const Vec3 p = fx_flight_position(ctx.spec, ctx.params, fx_clamp01(tt / fly));
        const float fr = 1.f - (static_cast<float>(k) / 9.f);
        put(out, p, 0.09f * big * fr, e.base, 1.6f * fr, 0.6f * fr, FxPart::OrbTail);
    }
    add_light(out, f.pos, 2.6f, e.base, 1.2f);
    mark_projectile(out, f.pos, f.dir);
}

Vec3 floor_point(const Look &L, float z)
{
    return Vec3{ L.foot.x, L.foot.y, z };
}

void floor_dots(FxFrame &out, const Vec3 &a, const Vec3 &b, float fraction, float step, const FxColour &c, float emissive, float alpha)
{
    const Vec3 d = b - a;
    const float len = std::sqrt(dot(d, d));
    const int n = std::max(1, static_cast<int>(len / step));
    const int shown = static_cast<int>(std::ceil(fx_clamp01(fraction) * static_cast<float>(n)));
    for (int i = 0; i <= shown && i <= n; ++i) {
        FxVoxel v;
        v.center = a + (d * (static_cast<float>(i) / static_cast<float>(n)));
        v.size = Vec3{ step * 1.1f, step * 1.1f, 0.02f };
        v.r = c.r;
        v.g = c.g;
        v.b = c.b;
        v.emissive = emissive;
        v.alpha = std::min(alpha, 1.f);
        v.part = FxPart::Puddle;
        out.voxels.push_back(v);
    }
}

void beam_bar(FxFrame &out, const Vec3 &a, const Vec3 &b, float w, const FxColour &c, float emissive, float alpha, float visible, FxPart part)
{
    const Vec3 d = b - a;
    const float len = std::sqrt(dot(d, d));
    if ((len < 1e-4f) || (visible <= 0.f)) {
        return;
    }
    const Vec3 dir = d * (1.f / len);
    const float shown = len * fx_clamp01(visible);
    put_bar(out, a + (dir * (shown * 0.5f)), dir, shown, w, c, emissive, alpha, part);
}

void beam_ends(const FxSpec &spec, Vec3 &from, Vec3 &to)
{
    from = spec.route.empty() ? fx_source_centre(spec, 0.5f) : spec.route.front();
    to = spec.route.empty() ? fx_target_centre(spec, 0.5f) : spec.route.back();
}

float beam_length(const FxSpec &spec)
{
    Vec3 a;
    Vec3 b;
    beam_ends(spec, a, b);
    const Vec3 d = b - a;
    return std::sqrt(dot(d, d));
}

void facing_away(const Look &L, float &ax, float &ay)
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

void shake_for(const Look &L, FxFrame &out, float start, float seconds, float strength)
{
    if (!L.mini) {
        fx_add_shake(out, fx_shake_envelope(L.te, start, seconds, strength));
    }
}

void vertical_column(FxFrame &out, float x, float y, float z0, float z1, float size, const FxColour &c, float emissive, float alpha, float fade_top, FxPart part)
{
    const int n = std::max(1, static_cast<int>(std::ceil((z1 - z0) / std::max(size, 1e-3f))));
    for (int i = 0; i < n; ++i) {
        const float k = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        put(out, Vec3{ x, y, z0 + ((z1 - z0) * k) }, size, c, emissive, alpha * (1.f - (fade_top * k)), part);
    }
}

void dotted_line(FxFrame &out, const Vec3 &a, const Vec3 &b, float step, float size, const FxColour &c, float emissive, float alpha, float visible, FxPart part)
{
    const Vec3 d = b - a;
    const float len = std::sqrt(dot(d, d));
    const int n = std::max(1, static_cast<int>(len / std::max(step, 1e-3f)));
    const int shown = static_cast<int>(std::ceil(fx_clamp01(visible) * static_cast<float>(n)));
    for (int i = 0; i <= shown && i <= n; ++i) {
        put(out, a + (d * (static_cast<float>(i) / static_cast<float>(n))), size, c, emissive, alpha, part);
    }
}

/* ============================================================ 呼び分け */

namespace {

float look_seconds(const Entry &e)
{
    return e.seconds;
}

float max_ring(const FxSpec &spec)
{
    int r = 0;
    for (const FxBurstCell &c : spec.cells) {
        r = std::max(r, c.ring);
    }
    return static_cast<float>(r);
}

bool use_mist(const FxSpec &spec, const Entry &e)
{
    return (spec.cells.size() > 25u) && ((e.flags & kNoMistFallback) == 0u);
}

float area_look_seconds(const FxSpec &spec, const Entry &e)
{
    return std::max(look_seconds(e), use_mist(spec, e) ? 0.95f : 0.f);
}

void emit_look(const Look &L, FxFrame &out)
{
    if (L.e.look != nullptr) {
        L.e.look(L, out);
    }
}

//! 範囲のマス全部（ボール・ジャンプ・ブレス）。中心は全部、周りは小さな版を距離ぶん遅らせる。
void emit_area(const FxStyleCtx &ctx, const Entry &e, float te, float ring_delay, bool centre_full, FxFrame &out)
{
    const FxSpec &spec = ctx.spec;
    if (spec.cells.empty()) {
        emit_look(make_look(spec, e, spec.centre_gx, spec.centre_gy, te, 1.f, false, spec.seed), out);
        return;
    }
    const bool many = use_mist(spec, e);
    const bool whole = (e.flags & kWholeArea) != 0u;
    int index = 0;
    for (const FxBurstCell &cell : spec.cells) {
        ++index;
        const float tc = te - (static_cast<float>(cell.ring) * ring_delay);
        if (tc < 0.f) {
            continue;
        }
        const bool centre = (centre_full && (cell.ring == 0)) || whole;
        if (many && !centre) {
            cell_mist(spec, e, cell.gx, cell.gy, index, tc, out);
            continue;
        }
        emit_look(make_look(spec, e, cell.gx, cell.gy, tc, centre ? 1.f : 0.62f, !centre, spec.seed + static_cast<std::uint32_t>(index * 977)), out);
    }
}

Look target_look(const FxStyleCtx &ctx, const Entry &e, float te)
{
    return make_look(ctx.spec, e, ctx.spec.centre_gx, ctx.spec.centre_gy, te, 1.f, false, ctx.spec.seed);
}

FxColour wave_colour(const Entry &e)
{
    return fx_mix(e.base, FxColour{ 1.f, 1.f, 1.f }, 0.12f);
}

//! ビームの終点に見た目が始まる秒。
float beam_end_time(const FxSpec &spec, const VoxelFxParams &params)
{
    const float segments = static_cast<float>((spec.route.size() > 1) ? (spec.route.size() - 1) : 0);
    return segments * params.beam_cell_delay;
}

void kit_flight(const FxStyleCtx &ctx, FxFrame &out)
{
    const Entry *e = entry_of(ctx.spec);
    if (e == nullptr) {
        fx_generic_flight(ctx, out);
        return;
    }
    if (e->flight != nullptr) {
        e->flight(ctx, *e, out);
    } else {
        small_orb(ctx, *e, 1.f, out);
    }
}

void kit_impact(const FxStyleCtx &ctx, FxFrame &out)
{
    const Entry *e = entry_of(ctx.spec);
    if (e == nullptr) {
        fx_generic_impact(ctx, out);
        return;
    }
    if (ctx.spec.explode) {
        emit_area(ctx, *e, ctx.te, ctx.params.ring_delay, true, out);
    } else {
        emit_look(target_look(ctx, *e, ctx.te), out);
    }
}

float kit_impact_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Entry *e = entry_of(spec);
    if (e == nullptr) {
        return fx_generic_impact_seconds(spec, params);
    }
    return (spec.explode ? area_look_seconds(spec, *e) : look_seconds(*e)) + (spec.explode ? (max_ring(spec) * params.ring_delay) : 0.f);
}

void kit_beam(const FxStyleCtx &ctx, FxFrame &out)
{
    const Entry *e = entry_of(ctx.spec);
    if (e == nullptr) {
        fx_generic_beam(ctx, out);
        return;
    }
    const float t_end = beam_end_time(ctx.spec, ctx.params);
    if ((e->flags & kNoBeamLine) == 0u) {
        fx_emit_beam_line(ctx.spec, ctx.params, ctx.t, out);
    }
    if (e->beam != nullptr) {
        e->beam(ctx, *e, t_end, out);
    }
    const float te = ctx.t - t_end;
    if (te >= 0.f) {
        emit_look(target_look(ctx, *e, te), out);
    }
}

float kit_beam_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Entry *e = entry_of(spec);
    if (e == nullptr) {
        return fx_generic_beam_seconds(spec, params);
    }
    const float t_end = beam_end_time(spec, params);
    const float line = ((e->flags & kNoBeamLine) == 0u) ? params.beam_life : 0.f;
    return t_end + std::max(std::max(look_seconds(*e), line), (e->beam != nullptr) ? e->beam_seconds : 0.f);
}

void kit_breath(const FxStyleCtx &ctx, FxFrame &out)
{
    const Entry *e = entry_of(ctx.spec);
    if (e == nullptr) {
        fx_generic_breath(ctx, out);
        return;
    }
    emit_area(ctx, *e, ctx.t, ctx.params.breath_ring_delay, false, out);
}

float kit_breath_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Entry *e = entry_of(spec);
    if (e == nullptr) {
        return fx_generic_breath_seconds(spec, params);
    }
    return area_look_seconds(spec, *e) + (max_ring(spec) * params.breath_ring_delay);
}

void kit_burst(const FxStyleCtx &ctx, FxFrame &out)
{
    const Entry *e = entry_of(ctx.spec);
    if (e == nullptr) {
        fx_generic_burst(ctx, out);
        return;
    }
    emit_area(ctx, *e, ctx.t, ctx.params.ring_delay, true, out);
}

float kit_burst_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Entry *e = entry_of(spec);
    if (e == nullptr) {
        return fx_generic_burst_seconds(spec, params);
    }
    return area_look_seconds(spec, *e) + (max_ring(spec) * params.ring_delay);
}

void kit_glow(const FxStyleCtx &ctx, FxFrame &out)
{
    const Entry *e = entry_of(ctx.spec);
    if (e == nullptr) {
        fx_generic_glow(ctx, out);
        return;
    }
    if (ctx.spec.glow_mode == FxGlowMode::AllInSight) {
        if (e->allsight != nullptr) {
            e->allsight(ctx, *e, out);
        } else {
            fx_emit_all_sight_wave(ctx, out, wave_colour(*e));
        }
        return;
    }
    emit_look(target_look(ctx, *e, ctx.t), out);
}

float kit_glow_seconds(const FxSpec &spec, const VoxelFxParams &params)
{
    const Entry *e = entry_of(spec);
    if (e == nullptr) {
        return fx_generic_glow_seconds(spec, params);
    }
    if (spec.glow_mode == FxGlowMode::AllInSight) {
        return (e->allsight != nullptr) ? e->allsight_seconds : 1.05f;
    }
    return look_seconds(*e);
}

} // namespace

void register_entries(FxStyleRegistry &registry, const Entry *entries, std::size_t count)
{
    tables().emplace_back(entries, count);
    for (std::size_t i = 0; i < count; ++i) {
        const Entry &e = entries[i];
        FxStyle &s = registry.add(e.attr, e.base, e.smoke, e.emissive);
        s.flight = kit_flight;
        s.impact = kit_impact;
        s.impact_seconds = kit_impact_seconds;
        s.beam = kit_beam;
        s.beam_seconds = kit_beam_seconds;
        s.breath = kit_breath;
        s.breath_seconds = kit_breath_seconds;
        s.burst = kit_burst;
        s.burst_seconds = kit_burst_seconds;
        s.glow = kit_glow;
        s.glow_seconds = kit_glow_seconds;
    }
}

} // namespace looks
} // namespace hd2d
