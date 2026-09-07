/*!
 * @file app_clock.cpp
 * @brief 時計の中身。切っていれば素の SDL をそのまま呼ぶだけ。
 */
#include "app/app_clock.h"

#include <cstdlib>

namespace hd2d::clock {

namespace {

//! 決め打ちのときの 1 周ぶんの進み（ミリ秒）。`kFrameIntervalMs` と同じにしてある。
constexpr Uint64 kStepMs = 16;

//! 決め打ちのときの起点（ミリ秒）。0 だと「まだ 1 度も測っていない」と区別できない所がある。
constexpr Uint64 kEpochMs = 100000;

//! 決め打ちのときの分解能。実機の値に近い切りのよい数。
constexpr Uint64 kFixedFreq = 10000000;

//! いま何周目か。決め打ちでなくても数える（記録の突き合わせに使う）。
Uint64 g_frame = 0;

bool env_on(const char *name)
{
    const char *const raw = std::getenv(name);
    return (raw != nullptr) && (raw[0] != '\0') && (raw[0] != '0');
}

} // namespace

bool fixed()
{
    //! 1 度だけ調べて覚える。走っている途中で変わることはない。
    static const bool on = env_on("HD2D_FIXED_CLOCK");
    return on;
}

void advance_frame()
{
    ++g_frame;
}

Uint64 frame_index()
{
    return g_frame;
}

Uint64 ticks64()
{
    if (!fixed()) {
        return SDL_GetTicks64();
    }
    return kEpochMs + (g_frame * kStepMs);
}

Uint32 ticks()
{
    if (!fixed()) {
        return SDL_GetTicks();
    }
    return static_cast<Uint32>(kEpochMs + (g_frame * kStepMs));
}

Uint64 perf()
{
    if (!fixed()) {
        return SDL_GetPerformanceCounter();
    }
    /*
     * **1 周の中では動かない。**そうすると `ms_decode` や `ms_frame` のような
     * 「始まりと終わりの差」がすべて 0 になる。時間の計測は速さを見るものであって、
     * 振る舞いが変わっていないかの突き合わせには邪魔なので、狙ってそうしている。
     */
    return (g_frame * kFixedFreq) / 60;
}

Uint64 perf_freq()
{
    return fixed() ? kFixedFreq : SDL_GetPerformanceFrequency();
}

void delay(Uint32 ms)
{
    if (fixed()) {
        return; //!< 待たない。検査が速く終わる
    }
    SDL_Delay(ms);
}

} // namespace hd2d::clock
