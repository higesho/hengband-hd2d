/*!
 * @file sky_dome.cpp
 * @brief `sky_dome.h` の実装。
 */
#include "render/sky_dome.h"

#include "render/gl_program.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hd2d {

using namespace hd2d::gl;

namespace {

constexpr float kPi = 3.14159265358979323846f;

//! 色の線形補間。`lighting.cpp` にも同じものがあるが、あちらは翻訳単位の中に閉じている。
Vec3 lerp(const Vec3 &a, const Vec3 &b, float t)
{
    return Vec3{ a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t) };
}

/*!
 * @name 空の色（線形 HDR。ポスト処理のトーンマップを通る）
 *
 * @details 決めたことは「日中の青空／朝夕のオレンジ／夜は紺から黒」。
 * `render/lighting.cpp` の `sun_colors()` が**環境光**の色を持っているのに対し、
 * こちらは**見える空そのもの**の色なので別に持つ。数が 2 か所にあるのは承知のうえで、
 * 役割が違う（あちらは物を照らす光、こちらは背景の絵）。
 * @{
 */
const Vec3 kNoonZenith{ 0.10f, 0.26f, 0.72f }; //!< 真上の濃い青
const Vec3 kNoonHorizon{ 0.55f, 0.72f, 0.95f }; //!< 地平線際の白っぽい青
const Vec3 kDuskZenith{ 0.10f, 0.11f, 0.32f }; //!< 朝夕。天頂はもう紺
const Vec3 kDuskHorizon{ 1.05f, 0.42f, 0.14f }; //!< 同 地平線のオレンジ（1 を超えて焼ける）
const Vec3 kNightZenith{ 0.004f, 0.006f, 0.018f }; //!< ほぼ黒
const Vec3 kNightHorizon{ 0.020f, 0.030f, 0.070f }; //!< 紺
/*! @} */

//! 地平線より下（遠景のかすみ）。地平線の色を落としたもの。
Vec3 ground_haze(const Vec3 &horizon)
{
    return Vec3{ horizon.x * 0.22f, horizon.y * 0.22f, horizon.z * 0.24f };
}

//! 種を 1 回進める（`world/terrain_view.cpp` と同じ手口の素朴な混ぜ方）。
std::uint32_t next_u32(std::uint32_t &state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float next01(std::uint32_t &state)
{
    return static_cast<float>(next_u32(state) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

/*!
 * @brief 星のテクスチャ（正距円筒図法）を焼く。
 * @details **球の上で一様**に星を撒いてから uv へ落とす。uv の上で一様に撒くと
 * 極のまわりが密になり、回転の中心だけ白く固まって見える。
 */
GLuint bake_star_texture()
{
    constexpr int kW = 2048;
    constexpr int kH = 1024;
    constexpr int kStars = 2600;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kW) * kH * 4, 0);

    const auto add = [&pixels](int x, int y, float r, float g, float b) {
        if ((y < 0) || (y >= kH)) {
            return;
        }
        const int wrapped = ((x % kW) + kW) % kW; //!< 経度は回り込む
        const std::size_t at = ((static_cast<std::size_t>(y) * kW) + wrapped) * 4;
        const auto put = [](std::uint8_t &dst, float v) {
            const int sum = static_cast<int>(dst) + static_cast<int>(std::lround(v * 255.f));
            dst = static_cast<std::uint8_t>(std::min(255, sum));
        };
        put(pixels[at + 0], r);
        put(pixels[at + 1], g);
        put(pixels[at + 2], b);
        pixels[at + 3] = 255;
    };

    std::uint32_t seed = 0x5EED51A7u; //!< 固定。起動のたびに星座が変わらないように
    for (int i = 0; i < kStars; ++i) {
        // 球面上で一様: z を一様に、経度を一様に。
        const float z = (next01(seed) * 2.f) - 1.f;
        const float lon = next01(seed) * 2.f * kPi;
        const float u = lon / (2.f * kPi);
        const float v = (std::asin(std::clamp(z, -1.f, 1.f)) / kPi) + 0.5f;
        const int px = static_cast<int>(u * static_cast<float>(kW));
        const int py = static_cast<int>((1.f - v) * static_cast<float>(kH));

        // 明るさは偏らせる（ほとんどは暗く、たまに明るいのが混じるほうが星空に見える）。
        const float t = next01(seed);
        const float mag = 0.18f + (0.82f * t * t * t);
        // 色味。青白い星と橙の星を少し混ぜる。
        const float tone = next01(seed);
        const float r = mag * (0.85f + (0.30f * tone));
        const float g = mag * 0.92f;
        const float b = mag * (1.15f - (0.30f * tone));

        add(px, py, r, g, b);
        if (mag > 0.55f) {
            // 明るい星だけ十字ににじませる（点 1 つだと縮小で消える）。
            add(px - 1, py, r * 0.35f, g * 0.35f, b * 0.35f);
            add(px + 1, py, r * 0.35f, g * 0.35f, b * 0.35f);
            add(px, py - 1, r * 0.35f, g * 0.35f, b * 0.35f);
            add(px, py + 1, r * 0.35f, g * 0.35f, b * 0.35f);
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); //!< 経度は回り込む
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

//! 太陽の円盤。中心は白熱、外へ橙のにじみ。**1 を超える値**を入れてブルームに掴ませる。
GLuint bake_sun_texture()
{
    constexpr int kSide = 128;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kSide) * kSide * 4, 0);
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const float fx = ((static_cast<float>(x) + 0.5f) / kSide * 2.f) - 1.f;
            const float fy = ((static_cast<float>(y) + 0.5f) / kSide * 2.f) - 1.f;
            const float r = std::sqrt((fx * fx) + (fy * fy));
            //! 芯（r < 0.34）は真っ白、そこから外へ滑らかに落とす。
            const float core = 1.f - std::clamp((r - 0.30f) / 0.06f, 0.f, 1.f);
            const float halo = std::pow(std::clamp(1.f - r, 0.f, 1.f), 2.6f);
            const float a = std::clamp(core + (halo * 0.55f), 0.f, 1.f);
            const std::size_t at = ((static_cast<std::size_t>(y) * kSide) + x) * 4;
            pixels[at + 0] = 255;
            pixels[at + 1] = static_cast<std::uint8_t>(std::lround(std::clamp(0.72f + (core * 0.28f), 0.f, 1.f) * 255.f));
            pixels[at + 2] = static_cast<std::uint8_t>(std::lround(std::clamp(0.35f + (core * 0.62f), 0.f, 1.f) * 255.f));
            pixels[at + 3] = static_cast<std::uint8_t>(std::lround(a * 255.f));
        }
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSide, kSide, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

//! 月の円盤。淡い灰白に、種で撒いた「海」の斑。
GLuint bake_moon_texture()
{
    constexpr int kSide = 128;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kSide) * kSide * 4, 0);

    // 斑（暗い円）を先に決めておく。位置と大きさは固定の種から。
    struct Blot {
        float x;
        float y;
        float r;
        float depth;
    };
    std::uint32_t seed = 0x9E3779B9u;
    Blot blots[7]{};
    for (auto &blot : blots) {
        blot.x = (next01(seed) * 1.3f) - 0.65f;
        blot.y = (next01(seed) * 1.3f) - 0.65f;
        blot.r = 0.10f + (next01(seed) * 0.22f);
        blot.depth = 0.10f + (next01(seed) * 0.18f);
    }

    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const float fx = ((static_cast<float>(x) + 0.5f) / kSide * 2.f) - 1.f;
            const float fy = ((static_cast<float>(y) + 0.5f) / kSide * 2.f) - 1.f;
            const float r = std::sqrt((fx * fx) + (fy * fy));
            //! 縁は 1 画素で切らずに少しぼかす（小さく写るので階段が目立つ）。
            const float a = 1.f - std::clamp((r - 0.78f) / 0.08f, 0.f, 1.f);
            float shade = 1.f;
            for (const Blot &blot : blots) {
                const float dx = fx - blot.x;
                const float dy = fy - blot.y;
                const float d = std::sqrt((dx * dx) + (dy * dy));
                shade -= blot.depth * (1.f - std::clamp(d / blot.r, 0.f, 1.f));
            }
            //! 縁を少し落として球に見せる。
            shade *= 1.f - (0.28f * std::clamp(r / 0.78f, 0.f, 1.f));
            shade = std::clamp(shade, 0.f, 1.f);
            const std::size_t at = ((static_cast<std::size_t>(y) * kSide) + x) * 4;
            pixels[at + 0] = static_cast<std::uint8_t>(std::lround(shade * 250.f));
            pixels[at + 1] = static_cast<std::uint8_t>(std::lround(shade * 250.f));
            pixels[at + 2] = static_cast<std::uint8_t>(std::lround(shade * 235.f));
            pixels[at + 3] = static_cast<std::uint8_t>(std::lround(a * 255.f));
        }
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSide, kSide, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

//! 球の三角形（頂点は**単位方向**そのもの）。半径はシェーダで掛ける。
std::vector<float> build_dome_mesh(int rings, int segments)
{
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(rings) * segments * 6 * 3);
    const auto dir_at = [rings, segments](int ring, int seg) {
        const float v = static_cast<float>(ring) / static_cast<float>(rings); // 0..1
        const float u = static_cast<float>(seg) / static_cast<float>(segments);
        const float polar = (v * kPi) - (kPi * 0.5f); // -90°..+90°
        const float lon = u * 2.f * kPi;
        const float cz = std::sin(polar);
        const float cr = std::cos(polar);
        return Vec3{ cr * std::cos(lon), cr * std::sin(lon), cz };
    };
    const auto push = [&out](const Vec3 &d) {
        out.push_back(d.x);
        out.push_back(d.y);
        out.push_back(d.z);
    };
    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            const Vec3 a = dir_at(ring, seg);
            const Vec3 b = dir_at(ring + 1, seg);
            const Vec3 c = dir_at(ring + 1, seg + 1);
            const Vec3 d = dir_at(ring, seg + 1);
            push(a);
            push(b);
            push(c);
            push(a);
            push(c);
            push(d);
        }
    }
    return out;
}

/*
 * 天球は**遠クリップ際に貼り付ける**（2026-08-17。§15.5）。
 *
 * 半径 50 マスのままだと深度は 0.999 級で、遠くの地形（一人称の遠クリップは 160 マス）
 * より手前になってしまう。不透明を全部描いた後に LEQUAL で「まだ誰も塗っていない画素」
 * だけを通すには、**どの幾何よりも奥**でなければならない。
 *
 * `1 - 1e-6` は 24 ビットの深度で最大値の 17 段下（一人称なら遠クリップ手前 0.5 マス）に
 * あたる。1.0 ちょうどにしないのは遠クリップ面と同一平面になるのを避けるためで、
 * 逆に大きく引く（1e-5 級）と**遠クリップ際の地形を空が上書きする帯**が広がる。
 * `v_dir` は元の向きなので、**貼り付けても色は 1 ビットも変わらない**。
 */
const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec3 a_dir;
uniform mat4 u_view_projection;
uniform vec3 u_eye;
uniform float u_radius;
out vec3 v_dir;
//! 遠クリップのすぐ内側（GLES でも highp。`gl_program.cpp` が前置きで宣言する）。
const float kSkyDepth = 0.999999;
void main()
{
    v_dir = a_dir;
    //! **`gl_Position` は読み返さない**（GLES では書き専の扱いをする実装がある）。
    vec4 clip = u_view_projection * vec4(u_eye + (a_dir * u_radius), 1.0);
    clip.z = clip.w * kSkyDepth;
    gl_Position = clip;
}
)";

/*
 * 3 層をこの 1 本で重ねる。順は下から「空の色 → 星 → 太陽・月」。
 * 星と太陽・月は**足す**（背景の上に光るものが乗る、という素直な合成）。
 */
const char *const kFragmentSource = R"(#version 460 core
in vec3 v_dir;
out vec4 frag_color;

uniform vec3 u_zenith;
uniform vec3 u_horizon;
uniform vec3 u_ground;
uniform vec3 u_sun_dir;
uniform vec3 u_moon_dir;
uniform vec3 u_sun_tan_x;
uniform vec3 u_sun_tan_y;
uniform vec3 u_moon_tan_x;
uniform vec3 u_moon_tan_y;
uniform vec3 u_star_row0;
uniform vec3 u_star_row1;
uniform vec3 u_star_row2;
uniform float u_star_amount;
uniform float u_sun_glow;
uniform sampler2D u_stars;
uniform sampler2D u_sun;
uniform sampler2D u_moon;

const float kPi = 3.14159265358979323846;
//! 太陽・月の見かけの半径（方向余弦の単位）。板の広がりの半分。
const float kSunSize = 0.055;
const float kMoonSize = 0.048;

//! 天球の板から 1 枚ぶんの色を取る。裏（z <= 0）は空。
vec4 disc(sampler2D tex, vec3 dir, vec3 dir_center, vec3 tan_x, vec3 tan_y, float size)
{
    float z = dot(dir, dir_center);
    if (z <= 0.0) {
        return vec4(0.0);
    }
    vec2 uv = vec2(dot(dir, tan_x), dot(dir, tan_y)) / (2.0 * size) + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return vec4(0.0);
    }
    return texture(tex, uv);
}

void main()
{
    vec3 dir = normalize(v_dir);

    /* ---- 層 1: 空の色。地平線から天頂へ。下は遠景のかすみ。 */
    vec3 sky;
    if (dir.z >= 0.0) {
        //! 素直な線形補間だと地平線の帯が細すぎる。累乗で地平線側を厚くする。
        float t = pow(clamp(dir.z, 0.0, 1.0), 0.55);
        sky = mix(u_horizon, u_zenith, t);
    } else {
        float t = clamp(-dir.z / 0.25, 0.0, 1.0);
        sky = mix(u_horizon, u_ground, t);
    }

    /* ---- 層 2: 星。天の北極まわりに回した向きで引く（正距円筒図法）。 */
    if (u_star_amount > 0.001) {
        vec3 s = vec3(dot(u_star_row0, dir), dot(u_star_row1, dir), dot(u_star_row2, dir));
        vec2 uv = vec2((atan(s.y, s.x) / (2.0 * kPi)) + 0.5,
                       0.5 - (asin(clamp(s.z, -1.0, 1.0)) / kPi));
        vec3 stars = texture(u_stars, uv).rgb;
        //! 地平線のすぐ上は薄れさせる（かすみに沈む）。地平線より下には出さない。
        float fade = smoothstep(-0.02, 0.16, dir.z);
        sky += stars * (u_star_amount * fade * 1.6);
    }

    /* ---- 層 3: 太陽と月。 */
    vec4 moon = disc(u_moon, dir, u_moon_dir, u_moon_tan_x, u_moon_tan_y, kMoonSize);
    sky = mix(sky, moon.rgb * 1.30, moon.a);

    vec4 sun = disc(u_sun, dir, u_sun_dir, u_sun_tan_x, u_sun_tan_y, kSunSize);
    sky += sun.rgb * (sun.a * u_sun_glow);

    /* 太陽のまわりの広い暈（テクスチャの外まで届く）。夕焼けを空側へ回り込ませる。 */
    float around = max(dot(dir, u_sun_dir), 0.0);
    sky += u_horizon * (pow(around, 24.0) * u_sun_glow * 0.12);

    frag_color = vec4(sky, 1.0);
}
)";

//! `dir` に直交する 2 本を作る（板の uv 軸）。
void tangent_frame(const Vec3 &dir, Vec3 &tan_x, Vec3 &tan_y)
{
    //! 天頂・天底と平行にならない参照軸を選ぶ（外積が 0 になると板が消える）。
    const Vec3 reference = (std::fabs(dir.z) > 0.90f) ? Vec3{ 0.f, 1.f, 0.f } : Vec3{ 0.f, 0.f, 1.f };
    tan_x = normalize(cross(reference, dir));
    tan_y = normalize(cross(dir, tan_x));
}

} // namespace

SkyState sky_state_from(const LightingState &state, const FloorIdentity &floor)
{
    SkyState out;
    out.visible = (floor.kind == static_cast<int>(FloorKind::Surface));
    if (!out.visible) {
        return out; // 地下に空は無い
    }

    /*
     * **`render/lighting.cpp` の `make_scene_lighting()` と同じ時角**を通す。
     * `day_minute` は 0 = 真夜中・720 = 正午。6 時に東・12 時に真上・18 時に西。
     */
    const float phase = static_cast<float>(state.day_minute) / 1440.f;
    const float hour_angle = (phase - 0.25f) * 2.f * kPi;
    const float altitude = std::sin(hour_angle);
    const float horizontal = std::cos(hour_angle); //!< 6 時 +1（東）→ 18 時 −1（西）
    out.altitude = altitude;

    /*
     * 太陽の向き。**高度の頭打ち（`lighting.cpp` の 0.85）は掛けない**（ヘッダの注記）。
     * 南（+y）へ 0.35 倒すのはあちらと同じ。真東西の空を通ると、
     * 見下ろしの画面ではほとんどの時間 太陽が画面の外へ出てしまう。
     */
    out.sun_dir = normalize(Vec3{ horizontal, 0.35f, altitude });
    //! 月は太陽の反対側（満月の配置）。これで「夜に東から昇って西へ沈む」が自動で成り立つ。
    out.moon_dir = normalize(Vec3{ -horizontal, 0.35f, -altitude });

    //! 星は 1 日で 1 周。太陽と同じ向き（東 → 西）へ回す。
    out.star_angle = -phase * 2.f * kPi;

    /*
     * 星の濃さ。**薄明の間に消し切る。**高度 0（地平線）で 0、−0.18 で 1。
     * `sun_colors()` が薄明を −0.15 で切っているのと歩調を合わせてある。
     */
    out.star_amount = std::clamp(-altitude / 0.18f, 0.f, 1.f);

    /*
     * 空の色。高度で 3 つの組を繋ぐ。
     *   +0.35 以上 … 真昼
     *   0 〜 +0.35 … 朝夕へ落ちていく（地平線がオレンジ）
     *   0 〜 −0.18 … 夜へ落ちていく（紺 → 黒）
     */
    if (altitude >= 0.f) {
        const float t = std::clamp(altitude / 0.35f, 0.f, 1.f);
        out.zenith = lerp(kDuskZenith, kNoonZenith, t);
        out.horizon = lerp(kDuskHorizon, kNoonHorizon, t);
    } else {
        const float t = std::clamp(-altitude / 0.18f, 0.f, 1.f);
        out.zenith = lerp(kDuskZenith, kNightZenith, t);
        out.horizon = lerp(kDuskHorizon, kNightHorizon, t);
    }
    out.ground = ground_haze(out.horizon);
    return out;
}

bool SkyDome::init(std::string &err)
{
    this->program_ = compile_program(kVertexSource, kFragmentSource, err);
    if (this->program_ == 0) {
        return false;
    }

    const std::vector<float> mesh = build_dome_mesh(24, 48);
    this->vertex_count_ = static_cast<GLsizei>(mesh.size() / 3);

    glGenVertexArrays(1, &this->vao_);
    glBindVertexArray(this->vao_);
    glGenBuffers(1, &this->vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<std::ptrdiff_t>(mesh.size() * sizeof(float)), mesh.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    this->star_tex_ = bake_star_texture();
    this->sun_tex_ = bake_sun_texture();
    this->moon_tex_ = bake_moon_texture();

    const auto at = [this](const char *name) { return glGetUniformLocation(this->program_, name); };
    this->loc_.view_projection = at("u_view_projection");
    this->loc_.eye = at("u_eye");
    this->loc_.radius = at("u_radius");
    this->loc_.zenith = at("u_zenith");
    this->loc_.horizon = at("u_horizon");
    this->loc_.ground = at("u_ground");
    this->loc_.sun_dir = at("u_sun_dir");
    this->loc_.moon_dir = at("u_moon_dir");
    this->loc_.sun_tan_x = at("u_sun_tan_x");
    this->loc_.sun_tan_y = at("u_sun_tan_y");
    this->loc_.moon_tan_x = at("u_moon_tan_x");
    this->loc_.moon_tan_y = at("u_moon_tan_y");
    this->loc_.star_row0 = at("u_star_row0");
    this->loc_.star_row1 = at("u_star_row1");
    this->loc_.star_row2 = at("u_star_row2");
    this->loc_.star_amount = at("u_star_amount");
    this->loc_.sun_glow = at("u_sun_glow");
    this->loc_.stars = at("u_stars");
    this->loc_.sun = at("u_sun");
    this->loc_.moon = at("u_moon");
    return true;
}

void SkyDome::shutdown()
{
    if (this->vbo_ != 0) {
        glDeleteBuffers(1, &this->vbo_);
        this->vbo_ = 0;
    }
    if (this->vao_ != 0) {
        glDeleteVertexArrays(1, &this->vao_);
        this->vao_ = 0;
    }
    if (this->program_ != 0) {
        glDeleteProgram(this->program_);
        this->program_ = 0;
    }
    const GLuint textures[3] = { this->star_tex_, this->sun_tex_, this->moon_tex_ };
    for (const GLuint tex : textures) {
        if (tex != 0) {
            GLuint name = tex;
            glDeleteTextures(1, &name);
        }
    }
    this->star_tex_ = 0;
    this->sun_tex_ = 0;
    this->moon_tex_ = 0;
}

void SkyDome::draw(const Mat4 &view_projection, const Vec3 &eye, const SkyState &sky)
{
    if (!sky.visible || (this->program_ == 0)) {
        return;
    }

    glUseProgram(this->program_);
    glUniformMatrix4fv(this->loc_.view_projection, 1, GL_FALSE, view_projection.m);
    glUniform3f(this->loc_.eye, eye.x, eye.y, eye.z);
    glUniform1f(this->loc_.radius, kSkyDomeRadius);
    glUniform3f(this->loc_.zenith, sky.zenith.x, sky.zenith.y, sky.zenith.z);
    glUniform3f(this->loc_.horizon, sky.horizon.x, sky.horizon.y, sky.horizon.z);
    glUniform3f(this->loc_.ground, sky.ground.x, sky.ground.y, sky.ground.z);
    glUniform3f(this->loc_.sun_dir, sky.sun_dir.x, sky.sun_dir.y, sky.sun_dir.z);
    glUniform3f(this->loc_.moon_dir, sky.moon_dir.x, sky.moon_dir.y, sky.moon_dir.z);

    Vec3 tan_x{};
    Vec3 tan_y{};
    tangent_frame(sky.sun_dir, tan_x, tan_y);
    glUniform3f(this->loc_.sun_tan_x, tan_x.x, tan_x.y, tan_x.z);
    glUniform3f(this->loc_.sun_tan_y, tan_y.x, tan_y.y, tan_y.z);
    tangent_frame(sky.moon_dir, tan_x, tan_y);
    glUniform3f(this->loc_.moon_tan_x, tan_x.x, tan_x.y, tan_x.z);
    glUniform3f(this->loc_.moon_tan_y, tan_y.x, tan_y.y, tan_y.z);

    /*
     * 星の回転。**天の北極**（北の地平線から `kSkyPoleAltitudeDeg` 持ち上げた向き）
     * のまわりに `star_angle` だけ回す行列を、行 3 本に分けて送る
     * （`gl_core.h` に `UniformMatrix3fv` を足さずに済ませるため）。
     */
    const float pole_rad = kSkyPoleAltitudeDeg * kPi / 180.f;
    //! 北は −y。z は上。
    const Vec3 axis = normalize(Vec3{ 0.f, -std::cos(pole_rad), std::sin(pole_rad) });
    const float c = std::cos(sky.star_angle);
    const float s = std::sin(sky.star_angle);
    const float t = 1.f - c;
    const float rot[9] = {
        (t * axis.x * axis.x) + c, (t * axis.x * axis.y) - (s * axis.z), (t * axis.x * axis.z) + (s * axis.y),
        (t * axis.x * axis.y) + (s * axis.z), (t * axis.y * axis.y) + c, (t * axis.y * axis.z) - (s * axis.x),
        (t * axis.x * axis.z) - (s * axis.y), (t * axis.y * axis.z) + (s * axis.x), (t * axis.z * axis.z) + c
    };
    glUniform3f(this->loc_.star_row0, rot[0], rot[1], rot[2]);
    glUniform3f(this->loc_.star_row1, rot[3], rot[4], rot[5]);
    glUniform3f(this->loc_.star_row2, rot[6], rot[7], rot[8]);
    glUniform1f(this->loc_.star_amount, sky.star_amount);

    /*
     * 太陽の明るさ。**地平線の下では 0**（沈んだ太陽が空に残らないように）。
     * 昇り際・沈み際は弱める（低いほど大気で減る、という素朴な近似）。
     */
    const float above = std::clamp((sky.altitude + 0.03f) / 0.20f, 0.f, 1.f);
    glUniform1f(this->loc_.sun_glow, 1.20f + (5.0f * above));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, this->star_tex_);
    glUniform1i(this->loc_.stars, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, this->sun_tex_);
    glUniform1i(this->loc_.sun, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, this->moon_tex_);
    glUniform1i(this->loc_.moon, 2);

    /*
     * **深度は読むが書かない**（2026-08-17。§15.5）。頂点シェーダが z を遠クリップ際へ
     * 貼り付けてあるので、LEQUAL は「まだ誰も塗っていない画素」＝消去したままの深度
     * （1.0）だけを通す。不透明の描き手が塗った所は全部落ちる——これが無駄塗りの削減で、
     * **絵は 1 画素も変わらない**（不透明は全員が深度を書くため）。
     *
     * 書かないのは今までどおりで、こちらは絵の話ではない。深度は**ポスト処理のフォグと
     * 被写界深度が読む**（`post_process.cpp`）ので、空の画素は「いちばん奥」のままで
     * なければ、霧とぼけの掛かり方が変わってしまう。
     */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(this->vao_);
    glDrawArrays(GL_TRIANGLES, 0, this->vertex_count_);
    glBindVertexArray(0);

    //! 次に来る雲のために戻す（深度関数は LEQUAL のまま＝本描画と同じ）。
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
}

} // namespace hd2d
