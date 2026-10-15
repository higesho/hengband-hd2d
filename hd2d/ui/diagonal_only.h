/*!
 * @file diagonal_only.h
 * @brief 斜め移動限定（`kActionDiagonalOnly`。`ui/key_binds.h`）の判定の部品。
 *
 * ## 約束（不思議のダンジョンの R ボタンのような操作）
 * - **押している間だけ**効く。離せば元どおり。
 * - 効くのは**地図の上の移動（歩く・Shift＋矢印の走り）だけ**。メニュー・窓・狙い・方向を聞く問い・
 *   文字入力の間は何も変えない（効く場面の判定は `app/input_controller.cpp` の `diagonal_only_applies()`）。
 * - 縦横の 1 方向だけの移動は**捨てる**（動かない・ターンを使わない・向きも変えない）。
 * - 斜めを入れる手段は、矢印・十字キーの 2 方向の同時押し、テンキーの 7・9・1・3、ローグライク配置の
 *   y・u・b・n など、もともと斜めの入力。アナログスティックは倒した向きにいちばん近い斜めへ寄せる。
 * - 視点を回したとき（見下ろしの 45 度刻み）や一人称のときは、**画面の向きに合わせた今までの変換の後**で
 *   斜めかどうかを見る。
 *
 * ここは SDL を知らない純粋な関数だけ（検査が窓なしで呼べる）。
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace hd2d {

//! 縦横の 1 方向だけの 1 歩か（(0,0) と斜めは偽）。
constexpr bool is_orthogonal_step(int dx, int dy)
{
    return (dx == 0) != (dy == 0);
}

/*!
 * @name 押している矢印（キーボード）
 * @details 矢印キーは 1 つずつ別の出来事で届くので、押している組を覚えて「上＋左」を作る。
 * テンキー（NumLock 切）の 8・2・4・6 も同じ印を使う。
 * @{
 */
constexpr unsigned kArrowUp = 1U;
constexpr unsigned kArrowDown = 2U;
constexpr unsigned kArrowLeft = 4U;
constexpr unsigned kArrowRight = 8U;
/*! @} */

//! 押している矢印の組 → 向き（右が +x・下が +y。向かい合う 2 つは打ち消し合う）。
inline void arrows_to_step(unsigned mask, int &dx, int &dy)
{
    dx = ((mask & kArrowRight) != 0U ? 1 : 0) - ((mask & kArrowLeft) != 0U ? 1 : 0);
    dy = ((mask & kArrowDown) != 0U ? 1 : 0) - ((mask & kArrowUp) != 0U ? 1 : 0);
}

/*!
 * @name スティックを斜めに寄せる角度
 * @details 軸（真上・真下・真左・真右）からの角度で見る。
 * 0 度＝ちょうど軸の上・45 度＝ちょうど斜め。**軸の前後 20 度（入るとき）は動かない帯**にして、
 * ほぼ真上・真横に倒した指の揺れで斜めに飛ばないようにする。斜めになっている間は 16 度まで
 * 帯を狭めて（ヒステリシス）、境目でがたつかないようにする（`game_pad.h` の約束 3 と同じ理由）。
 * 斜めの側は 20〜70 度（軸から見て）で、1 つの斜めあたり 50 度の幅になる。
 * @{
 */
constexpr float kDiagonalAxisBandEnterDeg = 20.F; //!< 斜めへ入るには、軸からこれ以上離れる
constexpr float kDiagonalAxisBandLeaveDeg = 16.F; //!< 斜めから戻るのは、軸からこれ未満になったとき
/*! @} */

/*!
 * @brief スティックの生の値を、斜め 4 方向のどれか（または動かない）にする。
 * @param raw_x,raw_y 軸の生の値（−32768〜32767。y は下が正）。
 * @param was_diagonal 直前も斜めを出していたか（ヒステリシス）。
 * @param enter_mag,leave_mag 倒した量のしきい値（入る・戻る。軸の生の値と同じ単位。2 軸の合成の長さで見る）。
 * @param[out] dx,dy 斜めなら ±1 の組。動かないなら (0,0)。
 */
inline void stick_to_diagonal(int raw_x, int raw_y, bool was_diagonal, int enter_mag, int leave_mag, int &dx, int &dy)
{
    dx = 0;
    dy = 0;
    const double ax = std::abs(static_cast<double>(raw_x));
    const double ay = std::abs(static_cast<double>(raw_y));
    const double length = std::sqrt((ax * ax) + (ay * ay));
    if (length < static_cast<double>(was_diagonal ? leave_mag : enter_mag)) {
        return;
    }
    const double lo = std::min(ax, ay);
    const double hi = std::max(ax, ay);
    constexpr double kPi = 3.14159265358979323846;
    const double off_axis_deg = std::atan2(lo, hi) * 180.0 / kPi; //!< 近いほうの軸からの角度（0〜45）
    const double band = static_cast<double>(was_diagonal ? kDiagonalAxisBandLeaveDeg : kDiagonalAxisBandEnterDeg);
    if (off_axis_deg < band) {
        return;
    }
    dx = (raw_x > 0) ? 1 : -1;
    dy = (raw_y > 0) ? 1 : -1;
}

} // namespace hd2d
