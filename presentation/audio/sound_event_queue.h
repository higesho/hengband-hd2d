#pragma once
/*!
 * @file sound_event_queue.h
 * @brief コアが「鳴らす代わりに書き留める」音の待ち行列。
 *
 * ## なぜ書き留めるのか
 * 音を鳴らす担当を画面側へ移した（2026-08-21 に決めた）。コアは位置も向きも
 * 持たない（カメラを知らない）ので、**出来事だけを言って**フレームに載せる。
 * 形は `CombatFeedback`（`src/core/combat-feedback.h`）と同じ——**汲んだら消える**。
 *
 * ## いつ書き留めるか
 * 画面側が `ui_state.audio.sound_events` を立てたときだけ（`sound_events_wanted`）。
 * 立てていなければコアが従来どおり自分で鳴らす。**移行の途中でも二重に鳴らない。**
 *
 * @note `use_sound` は**立てたままにする**。コアの `sound()` はあれが偽だと
 * `Term_xtra` すら呼ばないので、切ると**書き留める機会ごと消える**。
 * 「利用者が効果音を切った」は `sound_on` の側で表す。
 */

#include "frame/sound_event.h"

#include <vector>

namespace presentation {

//! 画面側が「出来事を送れ」と言っているか。**偽ならコアが自分で鳴らす。**
bool sound_events_wanted();
//! 上を立てる（`ui_state` を受けた所から呼ぶ）。
void set_sound_events_wanted(bool wanted);

/*!
 * @brief 音を 1 つ書き留める。
 * @param name 音の名前（`sound_names` の綴り）。空なら捨てる
 * @param y,x 鳴ったマス。**位置が分からない音は聞き手（プレイヤ）のマス**を入れる
 * @details 溜まりすぎたら**古いほうから捨てる**（1 フレームに何百も鳴らない）。
 */
void push_sound_event(const std::string &name, int y, int x);

//! 溜まっているものを汲む。**汲んだら消える**（残すと同じ音を鳴らし続ける）。
std::vector<SoundEvent> take_sound_events();

} // namespace presentation
