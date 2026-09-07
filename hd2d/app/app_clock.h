/*!
 * @file app_clock.h
 * @brief 時計。**検査のときだけ、進み方を決め打ちにできる。**
 *
 * @details `run()` は時刻を 29 か所で読んでいる（`SDL_GetTicks64` 10・`SDL_GetTicks` 6・
 * `SDL_GetPerformanceCounter` 12・`SDL_GetPerformanceFrequency` 1）。素の SDL を直に
 * 呼ぶと、**同じ操作でも走らせるたびに違う値**が返るので、前後の突き合わせができない。
 *
 * ここを通すと、`HD2D_FIXED_CLOCK=1` のときだけ「1 周ごとに決まった分だけ進む」時計に
 * 化ける。切っていれば素の SDL をそのまま呼ぶだけなので、**遊ぶときの振る舞いは
 * 1 ビットも変わらない**（`tools/hd2d_verify/playthrough.py` で確かめること）。
 *
 * ## 使い方
 *
 * `run()` の中では `SDL_GetTicks64()` ではなく `clock::ticks64()` を呼ぶ。
 * 環の頭で `clock::advance_frame()` を 1 回だけ呼ぶ——これが「1 周ぶん進める」合図。
 *
 * ## なぜ環境変数なのか
 *
 * 起動の引数にすると `AppOptions` を通す必要があり、`run()` の頭より前に
 * 決まっていないと困る所（ライブラリの読み込みの計測）に間に合わない。
 * 調査用の口は既に `HD2D_PAD_PRESS` / `HD2D_FORCE_TIME` が環境変数なので、それに揃える。
 *
 * ## 決め打ちにしたときの値
 *
 * | 呼ぶもの | 返るもの |
 * | --- | --- |
 * | `ticks64()` / `ticks()` | `起点 + (周の番号 × 16 ms)`。16 ms は `kFrameIntervalMs` と同じ |
 * | `perf()` | `周の番号 × 周波数 / 60`。**1 周の中では動かない**ので、計測はすべて 0 ms になる |
 * | `perf_freq()` | 10,000,000（固定） |
 * | `delay()` | 何もしない（待たない。検査が速く終わる） |
 *
 * `perf()` が 1 周の中で動かないので、`ms_decode` や `ms_frame` は必ず `0.000ms` に
 * なる。**これは狙ってそうしている**——時間の計測は「速いか」を見るものであって、
 * 「振る舞いが変わっていないか」を見る突き合わせには邪魔でしかない。
 */
#pragma once

#include <SDL2/SDL.h>

namespace hd2d::clock {

/*!
 * @brief 決め打ちの時計になっているか（`HD2D_FIXED_CLOCK` が空でも 0 でもない）。
 * @details 1 度だけ調べて覚える。走っている途中で変わることはない。
 */
bool fixed();

/*! @brief 環を 1 周ぶん進める。**環の頭で 1 回だけ**呼ぶ。 */
void advance_frame();

/*! @brief いま何周目か（0 起点）。決め打ちでないときも数えている。 */
Uint64 frame_index();

/*! @brief `SDL_GetTicks64` の代わり。 */
Uint64 ticks64();

/*! @brief `SDL_GetTicks` の代わり。 */
Uint32 ticks();

/*! @brief `SDL_GetPerformanceCounter` の代わり。 */
Uint64 perf();

/*! @brief `SDL_GetPerformanceFrequency` の代わり。 */
Uint64 perf_freq();

/*! @brief `SDL_Delay` の代わり。決め打ちのときは待たない。 */
void delay(Uint32 ms);

} // namespace hd2d::clock
