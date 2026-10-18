/*!
 * @file weather_fx.cpp
 * @brief 天候の見せ方（`weather_fx.h`）の中身。
 */
#include "render/weather_fx.h"

#include "render/gl_program.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace hd2d::gl;

namespace hd2d {

namespace {

constexpr float kPi = 3.14159265358979f;

//! 値が目標へ寄るまでの時間（秒）。天候の値は整数で飛ぶので、粒の量・光はこの長さでなだらかに変える。
constexpr float kSmoothSeconds = 1.2f;
//! 降り始め・降り止みの時間（秒）。
constexpr float kPrecipSmoothSeconds = 0.8f;

float approach(float current, float target, float dt, float seconds)
{
    const float k = std::min(1.f, dt / std::max(seconds, 1e-3f));
    return current + ((target - current) * k);
}

Vec3 lerp(const Vec3 &a, const Vec3 &b, float t)
{
    return a + ((b - a) * t);
}

float saturate01(float v)
{
    return std::clamp(v, 0.f, 1.f);
}

/*
 * 雨の筋（インスタンス描画）。1 粒 = 四角形 2 枚（頂点 6 つ）。頭（下）と尾（上）の画面上の位置から
 * 筋の向きを出し、垂直な向きに太さを広げる。尾は風の向きに逆らう側にあるので、風があるほど斜めになる。
 */
const char *const kRainVertex = R"(#version 460 core
layout(location = 0) in vec4 a_seed;   //!< xy = 箱の中の置き場所（0〜1）・z = 高さ（0〜1）・w = 個体差

uniform mat4 u_view_projection;
uniform vec3 u_center;       //!< 箱の中心（xy）。z は地面の高さ
uniform float u_span;        //!< 箱の水平の一辺（マス）
uniform float u_height;      //!< 箱の高さ（マス）
uniform vec3 u_offset;       //!< xy = 風で流れた量・z = 落ちた量（積み上げ。CPU が持つ）
uniform vec3 u_velocity;     //!< 粒の速度（マス/秒。xy = 風・z = -落ちる速さ）
uniform float u_streak;      //!< 筋の長さ（進む秒数）
uniform vec2 u_viewport;     //!< 画面（画素）
uniform float u_width_px;    //!< 筋の太さ（画素）

out float v_alpha;
out float v_side;

void main()
{
    vec3 raw = vec3(a_seed.xy * u_span, a_seed.z * u_height);
    raw.xy += u_offset.xy;
    raw.z -= u_offset.z;
    float z = mod(raw.z, u_height);
    vec2 rel = mod(raw.xy - u_center.xy + (u_span * 0.5), vec2(u_span)) - (u_span * 0.5);
    vec3 head = vec3(u_center.xy + rel, u_center.z + z);
    vec3 tail = head - (u_velocity * u_streak);

    int corner = gl_VertexID;   // 0..5。(頭,-)(頭,+)(尾,-) / (頭,+)(尾,+)(尾,-)
    bool is_tail = (corner == 2) || (corner == 4) || (corner == 5);
    float side = ((corner == 1) || (corner == 3) || (corner == 4)) ? 1.0 : -1.0;

    vec4 ch = u_view_projection * vec4(head, 1.0);
    vec4 ct = u_view_projection * vec4(tail, 1.0);
    if ((ch.w < 0.05) || (ct.w < 0.05)) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        v_alpha = 0.0;
        v_side = 0.0;
        return;
    }
    vec2 sh = ch.xy / ch.w;
    vec2 st = ct.xy / ct.w;
    vec2 dir_px = (st - sh) * (u_viewport * 0.5);
    float len = length(dir_px);
    vec2 d = (len > 1e-3) ? (dir_px / len) : vec2(0.0, 1.0);
    vec2 perp = vec2(-d.y, d.x);
    vec4 c = is_tail ? ct : ch;
    vec2 ndc = c.xy / c.w;
    ndc += perp * side * (u_width_px / u_viewport);
    gl_Position = vec4(ndc * c.w, c.z, c.w);

    // 箱の縁・上端・地面すれすれで消す。尾は頭より薄くして、筋に向きを持たせる。
    float edge = 1.0 - smoothstep(0.34, 0.5, max(abs(rel.x), abs(rel.y)) / u_span);
    float top = 1.0 - smoothstep(u_height - 3.0, u_height, z);
    float ground = smoothstep(0.0, 0.8, z);
    float variance = 0.55 + (0.45 * fract(a_seed.w * 13.7));
    v_alpha = edge * top * ground * variance * (is_tail ? 0.15 : 1.0);
    v_side = side;
}
)";

const char *const kRainFragment = R"(#version 460 core
in float v_alpha;
in float v_side;
uniform vec3 u_color;
out vec4 o_color;

void main()
{
    float profile = 1.0 - smoothstep(0.35, 1.0, abs(v_side));
    float a = v_alpha * profile;
    if (a <= 0.002) {
        discard;
    }
    o_color = vec4(u_color * a, 1.0);
}
)";

/*
 * 雪（点）。ゆっくり落ちながら左右に揺れる。風は積み上げた流れ量で横に押す。
 */
const char *const kSnowVertex = R"(#version 460 core
layout(location = 0) in vec4 a_seed;

uniform mat4 u_view_projection;
uniform vec3 u_center;
uniform float u_span;
uniform float u_height;
uniform vec3 u_offset;
uniform float u_time;
uniform float u_size_px;

out float v_alpha;

void main()
{
    vec3 raw = vec3(a_seed.xy * u_span, a_seed.z * u_height);
    raw.xy += u_offset.xy;
    raw.z -= u_offset.z;
    raw.x += sin((u_time * 0.9) + (a_seed.w * 31.4)) * 0.7;
    raw.y += cos((u_time * 0.7) + (a_seed.w * 17.1)) * 0.7;
    float z = mod(raw.z, u_height);
    vec2 rel = mod(raw.xy - u_center.xy + (u_span * 0.5), vec2(u_span)) - (u_span * 0.5);
    vec3 world = vec3(u_center.xy + rel, u_center.z + z);
    gl_Position = u_view_projection * vec4(world, 1.0);

    float edge = 1.0 - smoothstep(0.34, 0.5, max(abs(rel.x), abs(rel.y)) / u_span);
    float top = 1.0 - smoothstep(u_height - 3.0, u_height, z);
    float ground = smoothstep(0.0, 0.6, z);
    float twinkle = 0.75 + (0.25 * sin((u_time * 2.3) + (a_seed.w * 25.1)));
    gl_PointSize = u_size_px * (0.6 + (0.8 * fract(a_seed.w * 7.31)));
    v_alpha = edge * top * ground * twinkle;
}
)";

const char *const kSnowFragment = R"(#version 460 core
in float v_alpha;
uniform vec3 u_color;
out vec4 o_color;

void main()
{
    vec2 d = gl_PointCoord - vec2(0.5);
    float r = length(d) * 2.0;
    float mask = 1.0 - smoothstep(0.4, 1.0, r);
    float a = mask * v_alpha;
    if (a <= 0.002) {
        discard;
    }
    o_color = vec4(u_color * a, 1.0);
}
)";

float hashed(std::uint32_t &state)
{
    state = (state * 1664525u) + 1013904223u;
    return static_cast<float>((state >> 8) & 0xFFFFFFu) / 16777216.f;
}

//! 色相だけ借りる（最大成分で割る）。暗い所で粒が消えないよう、明るさは借りない。
Vec3 hue_of(const Vec3 &tint)
{
    const float peak = std::max({ tint.x, tint.y, tint.z });
    const float norm = (peak > 0.001f) ? (1.f / peak) : 1.f;
    return Vec3{ tint.x * norm, tint.y * norm, tint.z * norm };
}

} // namespace

float WeatherView::wind_speed() const
{
    return this->wind * kWindCellsPerSecondPerUnit;
}

float weather_daylight_factor(int day_minute)
{
    const float phase = static_cast<float>(day_minute) / 1440.f;
    const float hour_angle = (phase - 0.25f) * 2.f * kPi;
    const float altitude = std::sin(hour_angle);
    //! `make_scene_lighting()` の薄明の帯（高度 0 〜 -0.15 で夜へ）と同じ。
    const float night_t = std::clamp(-altitude / 0.15f, 0.f, 1.f);
    return 1.f - night_t;
}

void update_weather_view(WeatherView &view, const WeatherState &state, float dt, float right_x, float right_y)
{
    dt = std::clamp(dt, 0.f, 0.25f);
    if (!state.declared && (view.presence <= 0.f)) {
        return; //!< 申告したことが無いコアは、ここで何も変えない
    }

    const float presence_target = state.declared ? 1.f : 0.f;
    if (state.declared) {
        if (view.presence <= 0.f) {
            //! 初めて届いた値へは一気に合わせる（起動直後に雨がゆっくり降り始めない）
            view.rain = static_cast<float>(state.rain);
            view.wind = static_cast<float>(state.wind);
            view.temp = static_cast<float>(state.temp);
            view.presence = 1.f;
            const float d0 = saturate01((view.rain - 7.f) / 8.f);
            const float snow0 = saturate01(view.temp - 7.f);
            view.rain_amount = d0 * (1.f - snow0);
            view.snow_amount = d0 * snow0;
        } else {
            view.rain = approach(view.rain, static_cast<float>(state.rain), dt, kSmoothSeconds);
            view.wind = approach(view.wind, static_cast<float>(state.wind), dt, kSmoothSeconds);
            view.temp = approach(view.temp, static_cast<float>(state.temp), dt, kSmoothSeconds);
        }
    }
    view.presence = approach(view.presence, presence_target, dt, kSmoothSeconds);
    if (view.presence < 0.001f) {
        view = WeatherView{};
        return;
    }

    //! 降る量は雨の値 7（曇りの上端）から 15 へ。雨と雪は気温の値 7 → 8 で入れ替わる（原作の気温の段 2 以上が雪）。
    const float density = saturate01((view.rain - 7.f) / 8.f) * view.presence;
    const float snow_mix = saturate01(view.temp - 7.f);
    view.rain_amount = approach(view.rain_amount, density * (1.f - snow_mix), dt, kPrecipSmoothSeconds);
    view.snow_amount = approach(view.snow_amount, density * snow_mix, dt, kPrecipSmoothSeconds);

    view.right_x = right_x;
    view.right_y = right_y;
    //! 風は画面の右向きへ流す（横に流れる速さが風の強さ）。流れた量は積み上げる。
    const double speed = static_cast<double>(view.wind_speed());
    view.wind_x += static_cast<double>(right_x) * speed * static_cast<double>(dt);
    view.wind_y += static_cast<double>(right_y) * speed * static_cast<double>(dt);
    view.fall_rain += static_cast<double>(kRainFallCellsPerSecond) * static_cast<double>(dt);
    view.fall_snow += static_cast<double>(kSnowFallCellsPerSecond) * static_cast<double>(dt);
}

void apply_weather_light(SceneLighting &lighting, const WeatherView &view, const LightingState &light_state)
{
    if (!view.active()) {
        return;
    }
    const float k = weather_daylight_factor(light_state.day_minute) * view.presence;
    if (k <= 0.f) {
        return; //!< 夜は天候に関係なく、今の暗さのまま
    }

    //! 雲の量。雨の値 0〜15 に比例（快晴 ≒ 0 〜 豪雨 ≒ 1）。
    const float cloud = saturate01(view.rain / 15.f) * k;
    //! 気温。-1 = 酷暑（値 0）〜 +1 = 極寒（値 15）。
    const float cold = std::clamp((view.temp - 7.5f) / 7.5f, -1.f, 1.f) * k;

    if (cloud > 0.f) {
        //! 太陽は雲で弱まり、色は冷たい灰へ。
        const float sun = 1.f - (0.62f * cloud);
        lighting.key_color = Vec3{ lighting.key_color.x * sun * (1.f - (0.06f * cloud)),
            lighting.key_color.y * sun * (1.f - (0.01f * cloud)), lighting.key_color.z * sun * (1.f + (0.05f * cloud)) };
        const auto greyer = [&](const Vec3 &c, float dim, float grey) {
            const float lum = (0.299f * c.x) + (0.587f * c.y) + (0.114f * c.z);
            const Vec3 g{ lum * 0.92f, lum * 0.98f, lum * 1.10f };
            return lerp(c, g, grey) * dim;
        };
        lighting.sky_color = greyer(lighting.sky_color, 1.f - (0.28f * cloud), 0.65f * cloud);
        lighting.bounce_color = greyer(lighting.bounce_color, 1.f - (0.18f * cloud), 0.45f * cloud);
        lighting.shadow_strength *= 1.f - (0.72f * cloud);
    }
    if (std::fabs(cold) > 0.f) {
        //! 暑いと暖かく、寒いと冷たく振れる（控えめ）。
        const float warm = -cold;
        lighting.key_color = Vec3{ lighting.key_color.x * (1.f + (0.07f * warm)), lighting.key_color.y,
            lighting.key_color.z * (1.f - (0.08f * warm)) };
        lighting.sky_color = Vec3{ lighting.sky_color.x * (1.f + (0.05f * warm)), lighting.sky_color.y,
            lighting.sky_color.z * (1.f - (0.06f * warm)) };
    }
}

bool WeatherParticles::init(std::string &err)
{
    this->rain_program_ = compile_program(kRainVertex, kRainFragment, err);
    if (this->rain_program_ == 0) {
        return false;
    }
    this->snow_program_ = compile_program(kSnowVertex, kSnowFragment, err);
    if (this->snow_program_ == 0) {
        glDeleteProgram(this->rain_program_);
        this->rain_program_ = 0;
        return false;
    }
    WeatherParticleParams defaults;
    this->particle_count_ = defaults.max_particles;
    std::vector<float> seeds(static_cast<std::size_t>(this->particle_count_) * 4u);
    std::uint32_t state = 0x7F4A7C15u;
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        seeds[i] = hashed(state);
    }
    glGenBuffers(1, &this->vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(seeds.size() * sizeof(float)), seeds.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &this->vao_rain_);
    glBindVertexArray(this->vao_rain_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(0, 1); //!< 1 粒 = 1 インスタンス

    glGenVertexArrays(1, &this->vao_snow_);
    glBindVertexArray(this->vao_snow_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glBindVertexArray(0);
    return true;
}

void WeatherParticles::shutdown()
{
    if (this->vbo_ != 0) {
        glDeleteBuffers(1, &this->vbo_);
        this->vbo_ = 0;
    }
    if (this->vao_rain_ != 0) {
        glDeleteVertexArrays(1, &this->vao_rain_);
        this->vao_rain_ = 0;
    }
    if (this->vao_snow_ != 0) {
        glDeleteVertexArrays(1, &this->vao_snow_);
        this->vao_snow_ = 0;
    }
    if (this->rain_program_ != 0) {
        glDeleteProgram(this->rain_program_);
        this->rain_program_ = 0;
    }
    if (this->snow_program_ != 0) {
        glDeleteProgram(this->snow_program_);
        this->snow_program_ = 0;
    }
}

void WeatherParticles::draw(const Mat4 &view_projection, const Vec3 &focus, const Vec3 &tint, float time,
    const WeatherView &view, const WeatherParticleParams &params, int viewport_w, int viewport_h)
{
    if ((this->rain_program_ == 0) || (view.precipitation() <= 0.001f)) {
        return; //!< 降っていないときは 1 命令も払わない
    }
    const float volume = params.span * params.span * params.height;
    const Vec3 hue = hue_of(tint);

    //! 風で流れた量と落ちた量は、箱の大きさで巻き戻して渡す（倍精度の積み上げから単精度へ）。
    const float off_x = static_cast<float>(std::fmod(view.wind_x, static_cast<double>(params.span)));
    const float off_y = static_cast<float>(std::fmod(view.wind_y, static_cast<double>(params.span)));
    const float wind_x = view.right_x * view.wind_speed();
    const float wind_y = view.right_y * view.wind_speed();

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    if (view.rain_amount > 0.001f) {
        const int count = std::min(this->particle_count_, static_cast<int>(params.rain_per_cell * volume * view.rain_amount));
        if (count > 0) {
            const double fall = view.fall_rain;
            glUseProgram(this->rain_program_);
            glUniformMatrix4fv(glGetUniformLocation(this->rain_program_, "u_view_projection"), 1, GL_FALSE, view_projection.m);
            glUniform3f(glGetUniformLocation(this->rain_program_, "u_center"), focus.x, focus.y, 0.f);
            glUniform1f(glGetUniformLocation(this->rain_program_, "u_span"), params.span);
            glUniform1f(glGetUniformLocation(this->rain_program_, "u_height"), params.height);
            glUniform3f(glGetUniformLocation(this->rain_program_, "u_offset"), off_x, off_y,
                static_cast<float>(std::fmod(fall, static_cast<double>(params.height))));
            glUniform3f(glGetUniformLocation(this->rain_program_, "u_velocity"), wind_x, wind_y, -params.rain_fall);
            glUniform1f(glGetUniformLocation(this->rain_program_, "u_streak"), params.rain_streak_seconds);
            glUniform2f(glGetUniformLocation(this->rain_program_, "u_viewport"), static_cast<float>(std::max(1, viewport_w)),
                static_cast<float>(std::max(1, viewport_h)));
            glUniform1f(glGetUniformLocation(this->rain_program_, "u_width_px"), params.rain_width_px);
            //! 雨は青みを足して、環境光の色相と混ぜる（夜も昼もほぼ白い筋にならないように）。
            const Vec3 rain_color = lerp(hue, Vec3{ 0.78f, 0.88f, 1.0f }, 0.55f) * (params.rain_brightness * std::sqrt(view.rain_amount + 0.25f));
            glUniform3f(glGetUniformLocation(this->rain_program_, "u_color"), rain_color.x, rain_color.y, rain_color.z);
            glBindVertexArray(this->vao_rain_);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
        }
    }
    if (view.snow_amount > 0.001f) {
        const int count = std::min(this->particle_count_, static_cast<int>(params.snow_per_cell * volume * view.snow_amount));
        if (count > 0) {
            glUseProgram(this->snow_program_);
            glUniformMatrix4fv(glGetUniformLocation(this->snow_program_, "u_view_projection"), 1, GL_FALSE, view_projection.m);
            glUniform3f(glGetUniformLocation(this->snow_program_, "u_center"), focus.x, focus.y, 0.f);
            glUniform1f(glGetUniformLocation(this->snow_program_, "u_span"), params.span);
            glUniform1f(glGetUniformLocation(this->snow_program_, "u_height"), params.height);
            glUniform3f(glGetUniformLocation(this->snow_program_, "u_offset"), off_x, off_y,
                static_cast<float>(std::fmod(view.fall_snow, static_cast<double>(params.height))));
            glUniform1f(glGetUniformLocation(this->snow_program_, "u_time"), time);
            glUniform1f(glGetUniformLocation(this->snow_program_, "u_size_px"), params.snow_size_px);
            const Vec3 snow_color = lerp(hue, Vec3{ 0.92f, 0.96f, 1.0f }, 0.6f) * params.snow_brightness;
            glUniform3f(glGetUniformLocation(this->snow_program_, "u_color"), snow_color.x, snow_color.y, snow_color.z);
#if !defined(HENGBAND_GLES)
            glEnable(GL_PROGRAM_POINT_SIZE);
#endif
            glBindVertexArray(this->vao_snow_);
            glDrawArrays(GL_POINTS, 0, count);
#if !defined(HENGBAND_GLES)
            glDisable(GL_PROGRAM_POINT_SIZE);
#endif
        }
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glUseProgram(0);
}

} // namespace hd2d
