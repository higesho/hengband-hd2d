/*!
 * @file weather_fx.h
 * @brief 天候（雨・雪・風・日中の光）の見せ方。。
 *
 * ## 決まったこと
 * 1. 雨と雪は粒で出す。数値に応じて量を変える。
 * 2. 風の強さは、粒が**横に流れる速さ**で表す。
 * 3. 雨と雪が降っていない間（快晴・曇り）は、いつもの粒（埃。`dust_motes.h`）を風で横に流す。
 * 4. 日中は天候に応じた光にする。夜は天候に関係なく暗い（夜の暗さは今の作りのまま）。
 * 5. ダンジョンの中でも、粒と日中の天候の光を出す。
 * 6. **コアが天候を申告したときだけ効く**（`WeatherState::declared`）。申告の無いコアは
 *    `WeatherView::presence` が厳密に 0 のままで、ここの関数はどれも何も触らない。
 *
 * ## 画面側のなめらかな値
 * コアの値は 0〜15 の整数で、天候が変わると一度に飛ぶ。そのまま使うと粒の量・光が 1 フレームで
 * 切り替わるので、`WeatherView` が約 1 秒かけて寄せる。風で流れた量（`wind_x`・`wind_y`）は
 * 速さを**積み上げた位置**で持つ（時計に速さを掛けると、風が変わった瞬間に粒が飛ぶ）。
 */
#pragma once

#include "frame/game_frame.h"
#include "frame/weather_state.h"
#include "render/gl_core.h"
#include "render/lighting.h"
#include "render/math3d.h"

#include <string>

namespace hd2d {

/*!
 * @brief 画面側が持つ、なめらかにした天候。
 * @details 既定値のままなら（申告が一度も無いなら）何も起こさない。
 */
struct WeatherView {
    //! 申告の有無へ寄せる重み（0 = 申告なし／1 = 申告あり）。**0 なら以下の関数は何も触らない。**
    float presence{ 0.f };
    //! なめらかにした値（コアの 0〜15 と同じ単位）。
    float rain{ 0.f };
    float wind{ 0.f };
    float temp{ 7.5f };
    //! 雨の量・雪の量（0〜1。降り始めと降り止みでなだらかに増減する）。
    float rain_amount{ 0.f };
    float snow_amount{ 0.f };
    //! 風で流れた量（世界のマス。積み上げ。画面の右向きを正とする方向に流れる）。
    double wind_x{ 0.0 };
    double wind_y{ 0.0 };
    //! 落ちた量（マス。積み上げ）。雨と雪は落ちる速さが違う。
    double fall_rain{ 0.0 };
    double fall_snow{ 0.0 };
    //! 画面の右が世界のどちらを向くか（水平面内の単位ベクトル）。
    float right_x{ 1.f };
    float right_y{ 0.f };

    //! 何かが効いているか（`presence` が 0 より大きい）。
    bool active() const { return this->presence > 0.f; }
    //! 粒の降るもの全体の量（0〜1）。
    float precipitation() const { return this->rain_amount + this->snow_amount; }
    //! 風の速さ（マス/秒）。
    float wind_speed() const;
};

//! 風の速さ（マス/秒）。値 0〜15 に比例（暴風 15 で毎秒 6 マス）。
constexpr float kWindCellsPerSecondPerUnit = 0.40f;
//! 粒の落ちる速さ（マス/秒）。
constexpr float kRainFallCellsPerSecond = 16.f;
constexpr float kSnowFallCellsPerSecond = 1.3f;

/*!
 * @brief 1 フレーム進める。
 * @param state コアが運んできた天候（`declared` が偽なら重みが 0 へ寄る）
 * @param dt 秒（0.25 で頭打ちにして使う）
 * @param right_x,right_y 画面の右が世界のどちらを向くか（`Camera::azimuth()` の cos・sin）
 */
void update_weather_view(WeatherView &view, const WeatherState &state, float dt, float right_x, float right_y);

/*!
 * @brief 日中の光を天候で変える。**夜は変えない**（夜の暗さは今の作りのまま）。
 * @param light_state 時刻（`day_minute`）。太陽の高度は `make_scene_lighting()` と同じ式で出す
 * @details 曇り・雨ほど太陽が弱まり（影も薄くなり）、空と照り返しが灰青に寄る。暑いと暖かく、寒いと冷たく振れる。
 * 地下でも同じ式（地下の光を同じ割合で掛ける。時刻は地上と同じ時計）。
 * `view.active()` が偽なら何も触らない。
 */
void apply_weather_light(SceneLighting &lighting, const WeatherView &view, const LightingState &light_state);

//! 昼の割合（0 = 夜・1 = 昼。`make_scene_lighting()` の薄明の帯と同じ式）。検査と見本の絵が使う。
float weather_daylight_factor(int day_minute);

//! 粒の調整値。
struct WeatherParticleParams {
    //! 撒く箱の水平の一辺と高さ（マス）。箱は注視点の真下の地面に置く。
    float span{ 36.f };
    float height{ 18.f };
    //! 密度が 1 のときの 1 マスあたりの粒の数。
    float rain_per_cell{ 0.50f };
    float snow_per_cell{ 0.60f };
    //! 落ちる速さ（マス/秒）。
    float rain_fall{ kRainFallCellsPerSecond };
    float snow_fall{ kSnowFallCellsPerSecond };
    //! 雨の筋の長さ（進む秒数。長さ = 速さ × これ）と太さ（画素）。
    float rain_streak_seconds{ 0.06f };
    float rain_width_px{ 1.6f };
    //! 雪の大きさ（画素）。
    float snow_size_px{ 7.0f };
    //! 明るさ（線形の HDR）。
    float rain_brightness{ 0.85f };
    float snow_brightness{ 2.2f };
    //! 粒の数の上限。
    int max_particles{ 24576 };
};

/*!
 * @brief 雨と雪を描く。**世界を描き終えた後・後処理の前**に呼ぶ（埃と同じ段）。
 * @details 位置は種から毎フレーム頂点シェーダの中で作る（CPU は何も動かさない）。雨は 1 粒を細い四角形
 * （風で斜めになる筋）に、雪は点にして描く。深度は読むが書かない。加算で線形の HDR へ混ぜる。
 */
class WeatherParticles {
public:
    bool init(std::string &err);
    void shutdown();
    bool ready() const { return this->rain_program_ != 0; }

    /*!
     * @param view_projection 視点の行列
     * @param focus 箱の中心（注視点。地面の高さは 0 とみなす）
     * @param tint 粒に掛ける色（環境光の色。最大成分で割って色相だけ借りる）
     * @param time 秒（雪の揺れ用）
     * @param view なめらかにした天候（`rain_amount`・`snow_amount` が 0 なら何も描かない）
     * @param viewport_w,viewport_h 画面の大きさ（画素）。雨の筋の太さに使う
     */
    void draw(const Mat4 &view_projection, const Vec3 &focus, const Vec3 &tint, float time, const WeatherView &view,
        const WeatherParticleParams &params, int viewport_w, int viewport_h);

private:
    gl::GLuint rain_program_{ 0 };
    gl::GLuint snow_program_{ 0 };
    gl::GLuint vao_rain_{ 0 };
    gl::GLuint vao_snow_{ 0 };
    gl::GLuint vbo_{ 0 };
    int particle_count_{ 0 };
};

} // namespace hd2d
