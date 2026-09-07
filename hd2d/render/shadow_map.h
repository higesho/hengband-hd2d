/*!
 * @file shadow_map.h
 * @brief 方向光のシャドウマップ（P5 ①）。
 *
 * 段取りは P5 ①。
 *
 * ## 作り
 * 深度だけの FBO へ、光の側から幾何をもう一度描く。設計書 §13 の注は
 * 「**影のパスで幾何を 2〜3 回描く**ので予算に余裕を見ること」と言っている。
 * いま（P4 時点）の実測は `build=0.05ms / submit=0.01ms` なので余裕はあるが、
 * **測ってから言う**（P5 の検証項目そのもの）。
 *
 * ## 当てはめ（`fit`）が肝
 * 方向光には収束点が無いので、直方体をどこに置くかは自分で決める。
 *
 * | 決めること | どう決めるか |
 * |---|---|
 * | 覆う範囲 | **カメラに写る地面の外接矩形**（`derive_view_window` と同じ四隅の光線） |
 * | 深度の範囲 | その範囲の上下に、立ち上がるものの高さぶんの余裕 |
 * | 位置の刻み | **テクセル 1 個ぶんに丸める**（下記） |
 *
 * ### なぜテクセルに丸めるのか
 * カメラはプレイヤに**丸めずに**追従する（§14-3）。当てはめもそのまま連続に動かすと、
 * 影の縁を作るテクセルの位置が毎フレームずれて**縁がちらつく**（shadow swimming）。
 * 光の空間での原点をテクセルの整数倍へ吸着させると、影は「動かないもの」として写る。
 *
 * ## 検査（§14-4「わざと壊れた入力で反応することを確かめる」）
 * `shadow_fit_covers_view()` が「当てはめた直方体が可視範囲を本当に覆っているか」を見る。
 * `--light-check --shrink-shadow=N` で**わざと狭めて FAIL になること**を確かめられる。
 */
#pragma once

#include "render/camera.h"
#include "render/gl_core.h"
#include "render/math3d.h"

#include <string>

namespace hd2d {

//! 当てはめの結果（行列と、検査が読む内訳）。
struct ShadowFit {
    Mat4 view_projection{ Mat4::identity() };
    //! 覆っている地面の外接矩形（world・マス単位）。検査と記録に出す。
    float min_x{ 0.f };
    float max_x{ 0.f };
    float min_y{ 0.f };
    float max_y{ 0.f };
    //! 直方体の一辺（world・マス単位）。1 テクセルあたりのマス数は `extent / side`。
    float extent{ 0.f };
    //! 当てはめに使ったシャドウマップの一辺（テクセル）。記録に出すためだけに持つ。
    int side{ 0 };
};

/*!
 * @brief カメラの可視範囲を覆う光の直方体を作る。
 *
 * @param light_dir 面から光源へ向かう向き（`SceneLighting::sun_dir`）。
 * @param max_height 立ち上がるものの高さ（マス）。深度の範囲に効く。
 * @param side シャドウマップの一辺（テクセル）。位置の丸めに使う。
 * @param shrink **検査用。**覆う範囲を四方から N マス狭める。0 が通常。
 */
ShadowFit fit_shadow(const Camera &camera, const Vec3 &light_dir, float max_height, int side, float shrink);

/*!
 * @brief 覆う範囲を**直に指定して**光の直方体を作る。
 * @details `fit_shadow()` が中で使っているのと同じ計算。カメラを持たない場面
 * （`--prefab` の素材見物）でも影を出せるようにするために分けてある。
 */
ShadowFit fit_shadow_bounds(float min_x, float max_x, float min_y, float max_y,
    float max_height, const Vec3 &light_dir, int side);

/*!
 * @brief 当てはめた直方体が、カメラに写る地面を覆っているか。
 * @param[out] report 人が読む結果。
 * @return 覆っていれば true。
 * @details 可視範囲の四隅（と中間点）を光のクリップ空間へ写し、[-1,1] に収まるかを見る。
 * **これが落ちるということは、画面の一部で影が突然消えるということ**である。
 */
bool shadow_fit_covers_view(const ShadowFit &fit, const Camera &camera, float max_height, std::string &report);

class ShadowMap {
public:
    /*!
     * @brief 深度だけの FBO を作る。
     * @param side 一辺（テクセル）。2048 なら 60 マスの視野で 1 マス 34 テクセル
     *   ＝ボクセル 1 個あたり 1 テクセル程度になる。
     */
    bool init(int side, std::string &err);
    void shutdown();

    //! 深度を書き始める（FBO を結び、ビューポートを合わせ、消す）。
    void begin();
    //! 元のフレームバッファへ戻す。`viewport_w/h` は戻したあとのビューポート。
    void end(int viewport_w, int viewport_h);

    gl::GLuint texture() const { return this->texture_; }
    int side() const { return this->side_; }
    bool ready() const { return this->fbo_ != 0; }

private:
    gl::GLuint fbo_{ 0 };
    gl::GLuint texture_{ 0 };
    int side_{ 0 };
};

} // namespace hd2d
