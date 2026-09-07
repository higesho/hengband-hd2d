/*!
 * @file xr_fake.cpp
 * @brief `xr_fake.h` の実装。
 */
#include "xr/xr_fake.h"

#include <algorithm>

namespace hd2d::xr {

namespace {

/*!
 * @brief 疑似の目の高さ（メートル・床から）。成人が立ったときの目の高さ。
 * @details **一人称 VR の縮尺がここから出る**。
 * 1.65m ÷ 0.55 マス ＝ 1 マス 3m。ジオラマは全部が頭からの相対なので影響しない。
 */
constexpr float kFakeEyeHeight = 1.65f;

} // namespace

Frame make_fake_frame(int eye_w, int eye_h, float ipd_m)
{
    Frame frame;
    frame.view_count = 2;
    frame.views_valid = true;
    frame.predicted_display_time = 0;

    const float half = ipd_m * 0.5f;
    for (int eye = 0; eye < 2; ++eye) {
        EyeView &view = frame.views[eye];
        view.width = std::max(1, eye_w);
        view.height = std::max(1, eye_h);
        /*
         * 頭は -z を向いて**立っている**。左目が -x 側。
         *
         * **高さを持たせるのが要点**（`kFakeEyeHeight`）。ジオラマは全部が頭からの
         * 相対なので 0 でも絵は同じだが、**一人称では床からの高さが縮尺を決める**
         * （§17。目の高さ 0.55 マス ＝ 実際の身長）。0 のままだと 1 マスが既定へ落ち、
         * しかも床に寝た視点になる。
         */
        view.position[0] = (eye == 0) ? -half : half;
        view.position[1] = kFakeEyeHeight;
        view.position[2] = 0.f;
        view.orientation[0] = 0.f;
        view.orientation[1] = 0.f;
        view.orientation[2] = 0.f;
        view.orientation[3] = 1.f;
        /*
         * **わざと非対称**（Quest 2 級の実測に近い値）。鼻側が狭く外側が広い、という
         * 実機の形をなぞってある。対称な値で試すと、非対称視錐台の実装が間違っていても
         * 絵が正しく見えてしまう。
         */
        const float inner = 0.6981f; //!< 40 度。鼻側
        const float outer = 0.7854f; //!< 45 度。外側
        view.fov_left = (eye == 0) ? -outer : -inner;
        view.fov_right = (eye == 0) ? inner : outer;
        view.fov_up = 0.7854f;
        view.fov_down = -0.8203f; //!< 47 度。下のほうが広い（実機も足元寄りが広い）
    }
    return frame;
}

} // namespace hd2d::xr
