/*!
 * @file camera.h
 * @brief 透視カメラ（注視点・距離・水平画角・見下ろし角）と、地面との逆写像。
 *
 * 段取りは P2 ①②④。
 *
 * ## シフトレンズは使わない（§4.1）
 * 旧 HD2D は「垂直線を垂直に保つ」ために画像平面を world 垂直に平行へ固定していた。
 * それをやめ、**普通の透視カメラ**にする。垂直線は画面端で傾くが、望遠寄りにすれば小さい。
 * 見返りにカメラ高が `d·tanθ` の縛りから外れ、遮蔽（§4.4）が大幅に改善する。
 *
 * ## 世界の単位は「マス」
 * x = 東、y = 南、z = 上。地面は z = 0。1 マス = 1.0（ボクセルは 1/32）。
 *
 * ## ここで必ず持つもの（§4.5・§14-6）
 * **往復検査**（マス中心 → 画素 → マス）。クリック移動がずれるのは仕様違反なので、
 * 投影と逆写像を別々に書いたら必ず突き合わせる。`camera_round_trip_check()`。
 */
#pragma once

#include "render/math3d.h"

#include <string>

namespace hd2d {

struct Camera {
    //! 注視点（マス単位・地面の上）。プレイヤに**丸めずに**追従させる（§14-3）。
    Vec3 target{ 0.f, 0.f, 0.f };
    //! カメラから注視点までの**水平**距離（マス）。
    float distance{ 22.f };
    //! 見下ろし角（ラジアン）。0 = 水平、π/2 = 真上。
    float pitch{ 0.80f };
    //! 水平画角（ラジアン）。望遠寄りにするほど垂直線の傾きが減り、正射影に漸近する（§4.3）。
    float fov_x{ 0.70f };
    int viewport_w{ 1 };
    int viewport_h{ 1 };
    /*!
     * @brief 方位（ラジアン）。**0 = 北を見る**（従来の見え方）・時計回りが正。
     * @details 設計時のカメラは「注視点の真南」に固定で、回る必要が無かった。
     * ここを動かすのは **① FPS モード（`ui/fps_mode.h`）と ② 見下ろしの 90° 視点回転**
     * の 2 つだけである。
     * どちらも `FpsMode::apply()` を通して当てる（当てる責任を 1 か所に閉じるため。
     * `FpsMode::base_yaw` が見下ろしのときの戻り先を持つ）。
     * **既定 0 のとき `eye()` は従来と 1 ビットも変わらない**（`sin 0 == 0` / `cos 0 == 1` は厳密）。
     */
    float yaw{ 0.f };
    /*!
     * @brief 一人称（FPS モード）。カメラを注視点そのものの位置へ置く。
     * @details 既定 false。true のときだけ `eye()`・`forward()`・`depth_range()` が
     * 別の枝を通る。切り替えるのは `FpsMode::apply()` だけにすること。
     */
    bool first_person{ false };
    //! 一人称のときの目の高さ（マス）。`first_person` が偽なら使わない。
    float eye_height{ 0.55f };

    //! カメラ位置。注視点の**南**（+y）に引いて、そのぶん持ち上げる（一人称では注視点そのもの）。
    Vec3 eye() const;
    //! 視線方向（正規化）。
    Vec3 forward() const;
    /*!
     * @brief 画面の「右」がどちらを向いているか（水平面内の角。ラジアン）。
     * @details 軸拘束ビルボードを向けるのに使う（§4.2）。板を回すのは水平方向だけなので、
     * カメラの右方向を水平面へ落とした角だけで足りる。
     */
    float azimuth() const;
    float tan_half_x() const;
    float tan_half_y() const;
    //! 深度範囲。可視範囲から機械的に決める（手で当てない）。
    void depth_range(float &z_near, float &z_far) const;
    Mat4 view() const;
    Mat4 projection() const;
    Mat4 view_projection() const;

    /*!
     * @brief 画面座標 → 高さ `plane_z` の水平面（**逆写像**）。
     * @param sx,sy 画素座標（左上原点・**連続値**。マウスなら中心を渡すこと）。
     * @param[out] out 交点（world）。
     * @return 交わったか。地平線より上を指していれば false。
     */
    bool unproject_to_plane(float sx, float sy, float plane_z, Vec3 &out) const;

    /*!
     * @brief world → 画面座標。
     * @param[out] sx,sy 画素座標（左上原点・連続値）。
     * @return カメラの前にあるか。
     */
    bool project(const Vec3 &world, float &sx, float &sy) const;
};

//! コアへ要求する可視窓（`ui_state.view_cells`）。**マスの数**。
struct ViewWindow {
    int cols{ 0 };
    int rows{ 0 };
    //! 導出の内訳（画面と記録に出す）。注視点から見た各方向の必要マス数。
    float north{ 0.f };
    float south{ 0.f };
    float west{ 0.f };
    float east{ 0.f };
};

/*!
 * @brief 視錐台と地面（および高さ `max_height` の面）の交わりから可視窓を導く。
 *
 * @param max_height 立ち上がるものの高さ（マス）。**奥のマスは背が高いぶん先に見えてくる**ので、
 *   地面だけで測ると足りない。
 * @param max_distance 地平線側の打ち切り（マス）。見下ろし角が浅いと交点が無限へ飛ぶ。
 *
 * ## 継承した罠
 * - **§14-1: 可視範囲は奥ほど広い台形。**列数も角度で振れる。四隅を全部見て外接矩形を採る
 * - **§14-2: 可視帯は注視点に対し非対称**（北＝奥が長い）。プロトコルの窓は
 *   `ox = px - view_w/2` で**注視点を中心にした対称な矩形**なので（`presentation_bridge.cpp:2846`）、
 *   **長い方の 2 倍**を要求しないと奥が欠ける
 */
ViewWindow derive_view_window(const Camera &camera, float max_height, float max_distance);

/*!
 * @brief 往復検査（マス中心 → 画素 → マス）。**投影と逆写像が食い違っていないか。**
 * @param[out] report 人が読む結果（`checked=... failed=...`）。
 * @return 失敗が 0 なら true。
 * @details ・§14-6。画面の外へ出たマスは数えない
 * （検査の対象は「画面に写っているマスを指し直せるか」なので）。
 */
bool camera_round_trip_check(const Camera &camera, const ViewWindow &window, std::string &report);

/*!
 * @brief **向きの検査** — 東が画面の右に、南が画面の下に出るか。
 * @param[out] report 人が読む結果。
 * @return 正しければ true。
 *
 * @details 往復検査は投影と逆写像が**互いに**合っているかしか見ないので、
 * **世界が丸ごと鏡像になっていても通ってしまう**。ここはそれを捕まえるための別の検査である。
 *
 * この世界は x = 東・y = 南・z = 上で、**左手系**である（東 × 南 = 下）。普通の
 * 右手系の視点行列をそのまま当てると東西が入れ替わるので、必ず実物で確かめる。
 */
bool camera_orientation_check(const Camera &camera, std::string &report);

/*!
 * @name 見下ろしの 45° 視点回転
 *
 * @details 回すのは**カメラであって素材ではない**（同 §3.7）。プレハブは世界に固定された
 * メッシュなので、`yaw` を変えれば裏も横も既に描ける。ロード時 4 姿勢は要らない。
 * @{
 */
/*!
 * @brief 回転の段数。**1 段 45°** で 8 段（2026-09-06 に決めた）。
 * @details 0=北・1=北東・2=東・3=南東・4=南・5=南西・6=西・7=北西 を「見る」。
 * 2026-09-06 まで 4 段（90°）だった。8 方向の移動とちょうど噛み合うので、
 * 45° 回した画面でも**画面の 8 方向が世界の 8 方向へ 1 対 1 で写る**。
 */
inline constexpr int kViewTurnCount = 8;

//! 1 段ぶんの角（ラジアン）。45°。
inline constexpr float kViewTurnStep = 0.78539816f;

//! `turn` 段（0..7）に対応する `Camera::yaw`（ラジアン）。範囲外は 0..7 へ丸める。
float view_turn_yaw(int turn);

/*!
 * @brief **画面基準**の方向（dx,dy）を**世界基準**へ回す。回転の規約はここが唯一の定義である。
 *
 * @param turn 0..7（それ以外は 0..7 へ丸める）。
 * @param[in,out] dx,dy 画面基準で入れて、世界基準（x=東・y=南）で受け取る。
 *
 * @details 見下ろしのカメラは `yaw = turn × 45°` の方角を**見ている**ので、
 * 画面の「奥」（dy = -1）はその方角そのものである。
 *
 * | turn | 見ている方角 | 画面の奥 (0,-1) が指す先 | 画面の右 (1,0) が指す先 |
 * |---|---|---|---|
 * | 0 | 北 | 北 (0,-1) | 東 (1,0) |
 * | 1 | 北東 | 北東 (1,-1) | 南東 (1,1) |
 * | 2 | 東 | 東 (1,0) | 南 (0,1) |
 * | 3 | 南東 | 南東 (1,1) | 南西 (-1,1) |
 * | 4 | 南 | 南 (0,1) | 西 (-1,0) |
 * | 5 | 南西 | 南西 (-1,1) | 北西 (-1,-1) |
 * | 6 | 西 | 西 (-1,0) | 北 (0,-1) |
 * | 7 | 北西 | 北西 (-1,-1) | 北東 (1,-1) |
 *
 * ＝ `turn` 段ぶん **8 方向の輪を時計回りに 1 つずつ送った**もの
 * （90° の 2 段ぶんは従来どおり `(dx,dy) → (-dy,dx)` の 1 回に等しい）。
 *
 * **8 近傍の単位方向でないもの**（ミニマップの相対表示が渡す長い矢）は、
 * 45° の回転では格子に載らないので**丸めた値**になる。長さの要る用途は
 * 90° の段（`turn` が偶数）へ寄せてから渡すこと（`game_hud.cpp` がそうしている）。
 *
 * @note **移動は画面基準**という決定（設計書 §3.6）を実現するのがこの関数である。
 * 画面基準の入力を出す経路（矢印キー・十字）は**必ずここを通すこと**。
 * 逆に、クリック移動・ミニマップ・一人称の `direction_delta` は
 * **もともと世界基準**なので通してはいけない（二重に回る）。
 */
void rotate_screen_delta(int turn, int &dx, int &dy);
/*! @} */

} // namespace hd2d
