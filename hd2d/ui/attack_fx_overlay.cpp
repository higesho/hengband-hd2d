/*!
 * @file attack_fx_overlay.cpp
 * @brief `attack_fx_overlay.h` の中身。
 */
#include "ui/attack_fx_overlay.h"

#include <algorithm>
#include <cmath>

namespace hd2d {

void draw_attack_fx_waves(UiPaint &paint, const std::vector<FxScreenWave> &waves, const RectPx &scene)
{
    if (waves.empty() || scene.empty()) {
        return;
    }
    constexpr int kStrips = 48; //!< 帯の縁をなめらかに見せる細かさ
    for (const FxScreenWave &w : waves) {
        if (w.alpha <= 0.002f) {
            continue;
        }
        const float centre = static_cast<float>(scene.x) + (w.position * static_cast<float>(scene.w));
        const float half = std::max(1.f, w.width * static_cast<float>(scene.w) * 0.5f);
        const float strip = (half * 2.f) / static_cast<float>(kStrips);
        for (int i = 0; i < kStrips; ++i) {
            const float x0 = centre - half + (strip * static_cast<float>(i));
            const float mid = (static_cast<float>(i) + 0.5f) / static_cast<float>(kStrips); //!< 0〜1
            const float profile = 1.f - std::fabs((mid * 2.f) - 1.f); //!< 三角形（中央が最も濃い）
            const float a = w.alpha * profile * profile;
            // 地図の矩形に切る。
            const float left = std::max(x0, static_cast<float>(scene.x));
            const float right = std::min(x0 + strip, static_cast<float>(scene.x + scene.w));
            if ((right <= left) || (a <= 0.002f)) {
                continue;
            }
            paint.rect_f(left, static_cast<float>(scene.y), right, static_cast<float>(scene.y + scene.h),
                PaintColor{ std::min(w.colour.r, 1.f), std::min(w.colour.g, 1.f), std::min(w.colour.b, 1.f), std::min(a, 1.f) });
        }
    }
}

} // namespace hd2d
