/*!
 * @file fps_mode.cpp
 * @brief `fps_mode.h` の実装。
 */
#include "ui/fps_mode.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace hd2d {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.f;
constexpr float kDegToRad = kPi / 180.f;

//! 角を [−π, +π) へ丸める。差を取ってから使う（回り続けても値が育たない）。
float wrap_signed(float rad)
{
    float a = std::fmod(rad + kPi, kTwoPi);
    if (a < 0.f) {
        a += kTwoPi;
    }
    return a - kPi;
}

//! 最寄りの 45° 刻みへ寄せる（8 方位から決してずらさないための唯一の式）。
float snap_to_octant(float rad)
{
    const float step = kFpsYawStepDeg * kDegToRad;
    return std::round(rad / step) * step;
}

} // namespace

void FpsMode::set_active(bool on)
{
    this->active = on;
    this->turn_dir = 0; //!< 押しっぱなしのまま切り替えても回り続けないように
    /*
     * **見下ろしで向いていた方角（`base_yaw`）へ揃える。**入るときも出るときも同じ値で
     * よい——出入りで絵が回らないのが正しい振る舞いだからである。
     *
     * 視点回転を入れる前はここが `0.f` のべた書きだった（本線のカメラが必ず北だったので
     * それで正しかった）。90° 回せるようになった今、0 のままにすると
     * **一人称へ入って出ただけで地図が北へ戻る**。
     */
    this->yaw = this->base_yaw;
    this->facing = this->base_yaw;
    //! 上下も中立へ。**入るときも出るときも**（上を向いたまま入り直すと何も見えない）。
    this->pitch_deg = kFpsPitchDeg;
}

void FpsMode::turn_hold_begin(int dir)
{
    if (dir == 0) {
        return;
    }
    this->turn_dir = dir;
    this->turn_from = this->facing;
}

void FpsMode::turn_hold_end(int dir)
{
    if ((dir == 0) || (this->turn_dir != dir)) {
        return; // 別の向きを押している最中の離し。いま回っている方を続ける
    }
    this->turn_dir = 0;

    const float step = kFpsYawStepDeg * kDegToRad;
    const float turned = std::fabs(wrap_signed(this->facing - this->turn_from));
    if (turned < (step * 0.5f)) {
        /*
         * ちょん押し。**ちょうど 1 刻み**回す（押していた分は捨てる）。
         * 「押した時間だけ回って半端な角で止まる」を避けたい。指を離した回数と
         * 回った刻みの数が一致するほうが、格子の游戯では読みやすい。
         */
        this->facing = this->turn_from + (static_cast<float>(dir) * step);
        return;
    }
    // 長押し。いま向いている所から**最寄りの刻み**へ寄せる（8 方位からずらさない）。
    this->facing = snap_to_octant(this->facing);
}

void FpsMode::look(int dx_px, int dy_px)
{
    if (dx_px != 0) {
        this->facing += static_cast<float>(dx_px) * kFpsMouseYawDegPerPx * kDegToRad;
        //! マウスは連続入力なので、見た目を遅らせない（`turn_analog` と同じ理由）。
        this->yaw = this->facing;
    }
    if (!this->vertical_look || (dy_px == 0)) {
        return;
    }
    /*
     * **画面の下へ動かしたら下を向く**（`pitch` は正が下）。ここを反転させると
     * 「マウスを下げたら空を見る」になる。反転の好みは分かれるが、
     * 横（右へ動かしたら右を向く）と揃えるならこちら。
     */
    this->pitch_deg = std::clamp(this->pitch_deg + (static_cast<float>(dy_px) * kFpsMousePitchDegPerPx),
        -kFpsPitchMaxUpDeg, kFpsPitchMaxDownDeg);
}

void FpsMode::level_view()
{
    //! **方位は触らない**（水平に戻すのは上下の話。§`level_view()` の doc）。
    this->pitch_deg = kFpsPitchDeg;
}

void FpsMode::adjust_fov(int steps)
{
    if (steps == 0) {
        return;
    }
    this->fov_deg = std::clamp(this->fov_deg + (static_cast<float>(steps) * kFpsFovStepDeg),
        kFpsFovMinDeg, kFpsFovMaxDeg);
}

void FpsMode::turn_analog(float axis, float dt)
{
    if ((axis == 0.f) || (dt <= 0.f)) {
        return;
    }
    this->facing += axis * kFpsYawRateDegPerSec * kDegToRad * dt;
    // スティックは連続入力なので、見た目を遅らせると「押した分だけ遅れて回る」感じになる。
    // 追随を待たずに即座へ寄せる（Q／E の刻みだけが滑らかに回る）。
    this->yaw = this->facing;
}

void FpsMode::advance(float dt)
{
    if (dt <= 0.f) {
        return;
    }
    /*
     * 上下の視点移動を**切った瞬間に中立へ戻す**。切ったのに上を向いたままだと、
     * 戻す手がマウスに無い（`look()` が伏角を無視するため）。
     */
    if (!this->vertical_look && (this->pitch_deg != kFpsPitchDeg)) {
        this->pitch_deg = kFpsPitchDeg;
    }
    if (this->turn_dir != 0) {
        // 押している間は刻みを無視して連続で回す。見た目も遅らせない（押した分だけ回る）。
        this->facing += static_cast<float>(this->turn_dir) * kFpsYawRateDegPerSec * kDegToRad * dt;
        this->yaw = this->facing;
        return;
    }
    const float diff = wrap_signed(this->facing - this->yaw);
    if (std::fabs(diff) < 1e-4f) {
        this->yaw = this->facing;
        return;
    }
    float t = dt * kFpsYawFollowRate;
    if (t > 1.f) {
        t = 1.f;
    }
    this->yaw += diff * t;
}

bool FpsMode::direction_delta(int forward, int strafe, int &dx, int &dy) const
{
    dx = 0;
    dy = 0;
    if ((forward == 0) && (strafe == 0)) {
        return false;
    }
    // 水平面だけの話なので伏角は使わない（前へ進んで沈まないように）。
    const float s = std::sin(this->facing);
    const float c = std::cos(this->facing);
    // 前 = (sin, −cos)（ヨー 0 で北）、右 = (cos, sin)。
    const float vx = (s * static_cast<float>(forward)) + (c * static_cast<float>(strafe));
    const float vy = (-c * static_cast<float>(forward)) + (s * static_cast<float>(strafe));
    if ((std::fabs(vx) < 1e-6f) && (std::fabs(vy) < 1e-6f)) {
        return false; // 前後同時押し・左右同時押しで打ち消えた
    }

    // 北を 0 として時計回りの角。北 = 0・東 = 90°・南 = 180°・西 = 270°。
    float deg = std::atan2(vx, -vy) / kDegToRad;
    if (deg < 0.f) {
        deg += 360.f;
    }
    int octant = static_cast<int>(std::floor((deg / 45.f) + 0.5f)) % 8;
    if (octant < 0) {
        octant += 8;
    }
    //                          北  北東 東  南東 南  南西 西  北西
    static const int kDx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    static const int kDy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    dx = kDx[octant];
    dy = kDy[octant];
    return true;
}

void FpsMode::apply(Camera &camera) const
{
    if (!this->active) {
        /*
         * **本線へ戻す。**`Camera` は使い回しの値なので、ここを素通りさせると
         * 一人称で回した `yaw` が残り、見下ろしへ戻った瞬間に地図が斜めを向く
         * （2026-08-11 に気づいた）。
         *
         * 戻り先は `base_yaw` ＝ **見下ろしの視点回転**
         * である。回転を入れる前はここが 0 のべた書きで、それが「本線は必ず北」の意味だった。
         * **`base_yaw` が 0 のままなら 1 ビットも変わらない。**
         */
        camera.first_person = false;
        camera.yaw = this->base_yaw;
        return;
    }
    camera.first_person = true;
    camera.yaw = this->yaw;
    camera.pitch = this->pitch_deg * kDegToRad;
    camera.fov_x = this->fov_deg * kDegToRad;
    camera.eye_height = kFpsEyeHeight;
}

} // namespace hd2d
