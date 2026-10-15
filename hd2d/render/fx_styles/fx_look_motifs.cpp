/*!
 * @file fx_look_motifs.cpp
 * @brief `fx_look_motifs.h` の中身。文字の格子の形と、渦・球殻・岩・流れ・光線・破片の模様。
 */
#include "render/fx_styles/fx_look_motifs.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace hd2d {
namespace looks {

namespace {

//! 行の並びから形を作る（行の幅がそろっていなくてもよい）。
FxShape shape_of(const std::vector<std::string> &rows)
{
    FxShape s;
    s.rows = rows;
    s.height = static_cast<int>(rows.size());
    for (const std::string &r : rows) {
        s.width = std::max(s.width, static_cast<int>(r.size()));
    }
    return s;
}

void mark_outline(std::vector<std::string> &g)
{
    const std::vector<std::string> src = g;
    const int h = static_cast<int>(src.size());
    for (int y = 0; y < h; ++y) {
        const int w = static_cast<int>(src[static_cast<std::size_t>(y)].size());
        for (int x = 0; x < w; ++x) {
            if (src[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != '#') {
                continue;
            }
            const auto empty_at = [&](int xx, int yy) {
                return (yy < 0) || (yy >= h) || (xx < 0) || (xx >= static_cast<int>(src[static_cast<std::size_t>(yy)].size()))
                    || (src[static_cast<std::size_t>(yy)][static_cast<std::size_t>(xx)] == '.');
            };
            if (empty_at(x - 1, y) || empty_at(x + 1, y) || empty_at(x, y - 1) || empty_at(x, y + 1)) {
                g[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = 'o';
            }
        }
    }
}

void fill_rect(std::vector<std::string> &g, int x0, int y0, int x1, int y1, char ch)
{
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if ((y >= 0) && (y < static_cast<int>(g.size())) && (x >= 0) && (x < static_cast<int>(g[static_cast<std::size_t>(y)].size()))) {
                g[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = ch;
            }
        }
    }
}

} // namespace

/* ============================================================ 文字の格子の形 */

const FxShape &shape_star5()
{
    static const FxShape s = fx_shape({ "....#....", "....#....", "...###...", "#########", ".#######.", "..#####..", "..##.##..", ".##...##.", ".#.....#." });
    return s;
}

const FxShape &shape_star4()
{
    static const FxShape s = fx_shape({ "...#...", "...#...", "..###..", "#######", "..###..", "...#...", "...#..." });
    return s;
}

const FxShape &shape_heart()
{
    static const FxShape s = fx_shape({ ".##...##.", "####.####", "#########", "#########", ".#######.", "..#####..", "...###...", "....#...." });
    return s;
}

const FxShape &shape_question()
{
    static const FxShape s = fx_shape({ ".#####.", "##...##", "##...##", "....##.", "...##..", "...##..", ".......", "...##..", "...##.." });
    return s;
}

const FxShape &shape_exclaim()
{
    static const FxShape s = fx_shape({ "###", "###", "###", ".#.", ".#.", "...", "###", "###" });
    return s;
}

const FxShape &shape_bang_anger()
{
    static const FxShape s = fx_shape({ "..#...#..", ".##...##.", "##.....##", ".........", ".........", "##.....##", ".##...##.", "..#...#.." });
    return s;
}

const FxShape &shape_eye(int stage)
{
    static const FxShape open = fx_shape({ "...#####...", ".##oOOOo##.", "#oooOOOooo#", ".##oOOOo##.", "...#####..." });
    static const FxShape half = fx_shape({ ".#########.", "#ooOOOOOoo#", ".#########." });
    static const FxShape closed = fx_shape({ "###########", ".#.#.#.#.#." });
    return (stage <= 0) ? open : ((stage == 1) ? half : closed);
}

const FxShape &shape_hand(bool fist)
{
    static const FxShape open = [] {
        std::vector<std::string> g(17, std::string(19, '.'));
        const int lens[4] = { 6, 8, 9, 7 };
        const int palm_top = 9;
        fill_rect(g, 3, palm_top, 17, 16, '#');
        for (int i = 0; i < 4; ++i) {
            const int x0 = 3 + (i * 4);
            fill_rect(g, x0, palm_top - lens[i], x0 + 2, palm_top, '#');
        }
        fill_rect(g, 1, 11, 3, 14, '#');
        fill_rect(g, 0, 9, 2, 12, '#');
        fill_rect(g, 3, 16, 4, 16, '.');
        fill_rect(g, 16, 16, 17, 16, '.');
        mark_outline(g);
        return shape_of(g);
    }();
    static const FxShape closed = [] {
        std::vector<std::string> g(11, std::string(13, '.'));
        fill_rect(g, 1, 3, 11, 9, '#');
        fill_rect(g, 1, 1, 2, 3, '#');
        fill_rect(g, 4, 1, 5, 3, '#');
        fill_rect(g, 7, 1, 8, 3, '#');
        fill_rect(g, 10, 1, 11, 3, '#');
        fill_rect(g, 1, 9, 1, 9, '.');
        fill_rect(g, 11, 9, 11, 9, '.');
        fill_rect(g, 0, 4, 0, 7, '#');
        mark_outline(g);
        return shape_of(g);
    }();
    return fist ? closed : open;
}

const FxShape &shape_crescent()
{
    static const FxShape s = [] {
        std::vector<std::string> g(15, std::string(15, '.'));
        for (int y = 0; y < 15; ++y) {
            for (int x = 0; x < 15; ++x) {
                const float dx = static_cast<float>(x) - 7.f;
                const float dy = static_cast<float>(y) - 7.f;
                const float ex = static_cast<float>(x) - 11.2f;
                if (((dx * dx) + (dy * dy) <= 7.3f * 7.3f) && ((ex * ex) + (dy * dy) > 6.1f * 6.1f)) {
                    g[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = '#';
                }
            }
        }
        mark_outline(g);
        return shape_of(g);
    }();
    return s;
}

const FxShape &shape_snowflake()
{
    static const FxShape s = fx_shape({ "#..#..#", ".#.#.#.", "..###..", "#######", "..###..", ".#.#.#.", "#..#..#" });
    return s;
}

const FxShape &shape_skull()
{
    static const FxShape s = fx_shape({ ".#####.", "#######", "##...##", "##.#.##", "#######", ".##.##.", ".#####.", ".#.#.#." });
    return s;
}

const FxShape &shape_chicken()
{
    // r = とさか、y = くちばしと足、e = 目、# = 体
    static const FxShape s = fx_shape({ "...rr......", "..rrr......", "..###.yy...", "..#e##y....", "...###.....", "..#####....", ".#######...", ".########..", "..######...", "...y..y...." });
    return s;
}

const FxShape &shape_bird()
{
    static const FxShape s = fx_shape({ "#...........#", "##....#....##", ".###..#..###.", "..#########..", "...#######...", ".....###.....", ".....#.#....." });
    return s;
}

const FxShape &shape_pagoda()
{
    static const FxShape s = fx_shape({ "....#....", "....#....", "...###...", ".#######.", "...###...", "..#####..", "#########", "..#####..", ".#######.", "#########", "..#####.." });
    return s;
}

const FxShape &shape_umbrella()
{
    static const FxShape s = fx_shape({ "..#####..", ".#######.", "#########", "#########", "....#....", "....#....", "....#....", "....#....", "...##....", "..#......" });
    return s;
}

const FxShape &shape_drum()
{
    static const FxShape s = fx_shape({ ".#####.", "#ooooo#", "#ooooo#", "#ooooo#", "#ooooo#", ".#####." });
    return s;
}

const FxShape &shape_plate()
{
    static const FxShape s = fx_shape({ "..#####..", ".#ooooo#.", "#ooooooo#", ".#ooooo#.", "..#####.." });
    return s;
}

const FxShape &shape_mask()
{
    static const FxShape s = fx_shape({ ".#####.", "#######", "##.#.##", "#######", "###.###", "#.###.#", "#.....#", ".#####.", "..###.." });
    return s;
}

const FxShape &shape_bat()
{
    static const FxShape s = fx_shape({ "#.......#", "##.#.#.##", "#########", ".#######.", "..#...#.." });
    return s;
}

const FxShape &shape_ghost()
{
    static const FxShape s = fx_shape({ "..###..", ".#####.", "#######", "##.#.##", "#######", "#######", "#######", "#.#.#.#" });
    return s;
}

const FxShape &shape_bomb()
{
    // y = 導火線、# = 本体、o = つや
    static const FxShape s = fx_shape({ "......yy.", ".....y...", "..#####..", ".#oo####.", "#o#######", "#########", "#########", ".#######.", "..#####.." });
    return s;
}

const FxShape &shape_coin()
{
    static const FxShape s = fx_shape({ "..###..", ".#ooo#.", "#oo#oo#", "#o###o#", "#oo#oo#", ".#ooo#.", "..###.." });
    return s;
}

const FxShape &shape_spear()
{
    static const FxShape s = fx_shape({ ".#.", "###", "###", ".#.", ".#.", ".#.", ".#.", ".#.", ".#.", ".#.", ".#.", "###" });
    return s;
}

const FxShape &shape_sword()
{
    static const FxShape s = fx_shape({ "..#..", "..#..", ".###.", ".###.", ".###.", ".###.", ".###.", "#####", "..#..", "..#..", ".###." });
    return s;
}

const FxShape &shape_roof()
{
    static const FxShape s = fx_shape({ ".....#.....", "....###....", "..#######..", "...#####...", ".#########.", "..#######..", "###########", ".#########." });
    return s;
}

const FxShape &shape_cup()
{
    static const FxShape s = fx_shape({ ".#####.", "#ooooo#", ".#ooo#.", "..###.." });
    return s;
}

const FxShape &shape_gap()
{
    static const FxShape s = fx_shape({ "...###...", "..#ooo#..", ".#ooooo#.", ".#oeooo#.", "#ooooooo#", "#oooeooo#", "#ooooooo#", "#oeooooo#", "#ooooooo#", ".#ooooo#.", ".#oooeo#.", "..#ooo#..", "...###..." });
    return s;
}

const FxShape &shape_head()
{
    static const FxShape s = fx_shape({ "..rrrrr..", ".rrrrrrr.", "rrwwwwwrr", "rwwwwwwwr", "rwewwwewr", "rwwwwwwwr", ".rwwwwwr.", "..rwwwr.." });
    return s;
}

const FxShape &shape_hitodama()
{
    static const FxShape s = fx_shape({ "...#...", "..###..", ".#####.", ".#####.", "#######", "#######", ".#####.", "..###..", "..#.#..", "...#..." });
    return s;
}

const FxShape &shape_note()
{
    static const FxShape s = fx_shape({ "..####", "..#..#", "..#..#", "..#..#", "###.###", "####.##" });
    return s;
}

const FxShape &shape_z()
{
    static const FxShape s = fx_shape({ "#####", "...#.", "..#..", ".#...", "#####" });
    return s;
}

/* ============================================================ 模様 */

void whirl(const Look &L, FxFrame &out, const WhirlParams &p)
{
    const int n = count_of(L, p.count);
    for (int i = 0; i < n; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i) + (p.salt * 1000u);
        const float f = fx_rand(L.seed, id, 1u);
        const float born = p.born + (p.duration * fx_rand(L.seed, id, 2u));
        const float age = L.te - born;
        if ((age < 0.f) || (age >= p.life)) {
            continue;
        }
        const float h = (p.z0 + (f * p.height) + (p.rise * age)) * L.scale;
        const float r = fx_lerp(p.radius0, p.radius1, f) * L.scale;
        const float ang = (2.f * kPi * fx_rand(L.seed, id, 3u)) + (age * p.speed * (1.35f - (0.65f * f)));
        const Vec3 pos{ L.foot.x + (std::cos(ang) * r), L.foot.y + (std::sin(ang) * r), h };
        const float alpha = fx_envelope(age, 0.12f, std::max(p.life - 0.42f, 0.05f), 0.3f);
        const float size = p.size * (0.7f + (0.6f * fx_rand(L.seed, id, 4u))) * (L.mini ? 0.8f : 1.f);
        put(out, pos, size, lerp_colour(p.c0, p.c1, f), p.emissive, alpha, p.part);
    }
}

void sphere_shell(const Look &L, FxFrame &out, const Vec3 &centre, float radius, int count, float size, const FxColour &c, float emissive, float alpha,
    float spin, float fraction, std::uint32_t salt)
{
    (void)L;
    (void)salt;
    const float golden = 2.39996323f;
    for (int i = 0; i < count; ++i) {
        const float y = 1.f - (2.f * (static_cast<float>(i) + 0.5f) / static_cast<float>(count));
        const float top = (1.f - y) * 0.5f;
        if (top > fraction) {
            continue;
        }
        const float r = std::sqrt(std::max(0.f, 1.f - (y * y)));
        const float phi = (static_cast<float>(i) * golden) + spin;
        put(out, centre + Vec3{ std::cos(phi) * r * radius, std::sin(phi) * r * radius, y * radius }, size, c, emissive, alpha, FxPart::Particle);
    }
}

void rock_cluster(FxFrame &out, const Vec3 &centre, float size, float yaw, const FxColour &a, const FxColour &b, float alpha, float emissive,
    std::uint32_t seed, FxPart part)
{
    if (alpha <= 0.004f) {
        return;
    }
    const float cs = std::cos(yaw);
    const float sn = std::sin(yaw);
    for (int i = 0; i < 8; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        Vec3 o{ ((fx_rand(seed, id, 1u) - 0.5f) * 0.55f * size), ((fx_rand(seed, id, 2u) - 0.5f) * 0.55f * size), ((fx_rand(seed, id, 3u) - 0.5f) * 0.55f * size) };
        if (i == 0) {
            o = Vec3{};
        }
        const Vec3 rot{ (o.x * cs) - (o.y * sn), (o.x * sn) + (o.y * cs), o.z };
        const float s = size * ((i == 0) ? 0.62f : (0.3f + (0.2f * fx_rand(seed, id, 4u))));
        FxVoxel v;
        v.center = centre + rot;
        v.size = Vec3{ s, s, s };
        const FxColour col = fx_mix(a, b, fx_rand(seed, id, 5u));
        v.r = col.r;
        v.g = col.g;
        v.b = col.b;
        v.emissive = emissive;
        v.alpha = std::min(alpha, 1.f);
        v.yaw = yaw + (fx_rand(seed, id, 6u) * 0.8f);
        v.part = part;
        out.voxels.push_back(v);
    }
}

void stream_to(const Look &L, FxFrame &out, const Vec3 &from_centre, const Vec3 &to, const StreamParams &p)
{
    const int n = count_of(L, p.count);
    for (int i = 0; i < n; ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i) + (p.salt * 1000u);
        const float born = p.born + (p.spread * static_cast<float>(i) / static_cast<float>(n));
        const float age = L.te - born;
        if ((age < 0.f) || (age >= p.flight)) {
            continue;
        }
        const float u = age / p.flight;
        const float phi = 2.f * kPi * fx_rand(L.seed, id, 1u);
        const float rr = (0.08f + (0.17f * fx_rand(L.seed, id, 2u))) * L.scale;
        const Vec3 start{ from_centre.x + (std::cos(phi) * rr), from_centre.y + (std::sin(phi) * rr), from_centre.z + ((fx_rand(L.seed, id, 3u) - 0.5f) * 0.6f * L.scale) };
        const Vec3 d = to - start;
        const Vec3 side = unit_or(cross(d, Vec3{ 0.f, 0.f, 1.f }), Vec3{ 1.f, 0.f, 0.f });
        const float bend = ((fx_rand(L.seed, id, 4u) * 2.f) - 1.f) * p.bend;
        const auto at = [&](float k) {
            const float e = std::pow(fx_clamp01(k), 1.7f);
            Vec3 pt = start + (d * e);
            pt = pt + (side * (bend * std::sin(kPi * fx_clamp01(k))));
            pt.z += 0.25f * std::sin(kPi * fx_clamp01(k));
            return pt;
        };
        const FxColour col = lerp_colour(p.c0, p.c1, u);
        const float fade = 1.f - fx_ramp(u, 0.82f, 1.f);
        const float size = p.size * (1.f - (0.45f * u)) * L.scale;
        put(out, at(u), size, col, p.emissive, fade);
        put(out, at(u - 0.09f), size * 0.7f, fx_scale(col, 0.7f), p.emissive * 0.6f, 0.45f * fade);
    }
}

void radial_rays(const Look &L, FxFrame &out, const Vec3 &centre, int n, float r0, float len, float w, const FxColour &c, float emissive, float alpha,
    float spin)
{
    for (int i = 0; i < n; ++i) {
        const float ang = spin + (2.f * kPi * static_cast<float>(i) / static_cast<float>(n));
        const Vec3 d = on_screen(L.spec, std::cos(ang), std::sin(ang));
        put_bar(out, centre + (d * (r0 + (len * 0.5f))), d, len, w, c, emissive, alpha, FxPart::Spark);
    }
}

void debris_burst(const Look &L, FxFrame &out, const Vec3 &origin, int count, float born, float up_speed, float spread, float size, const FxColour &c0,
    const FxColour &c1, float emissive, std::uint32_t salt, float gravity)
{
    FxSpray s;
    s.origin = origin;
    s.dir = Vec3{ 0.f, 0.f, 1.f };
    s.cone = spread;
    s.speed_min = up_speed * 0.45f;
    s.speed_max = up_speed;
    s.gravity = gravity;
    s.born = born;
    s.life_min = 0.55f;
    s.life_max = 0.95f;
    s.count = count_of(L, count);
    s.colour0 = c0;
    s.colour1 = c1;
    s.size0 = size;
    s.size1 = size * 0.8f;
    s.emissive0 = emissive;
    s.emissive1 = emissive * 0.5f;
    s.alpha0 = 1.f;
    s.alpha1 = 0.f;
    s.floor = FxFloor::Stop;
    s.floor_z = 0.f;
    s.part = FxPart::Debris;
    s.seed = L.seed + salt;
    (void)fx_emit_spray(s, L.te, out);
}

} // namespace looks
} // namespace hd2d
