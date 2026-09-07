/*!
 * @file dust_motes.h
 * @brief **空中に浮かぶ埃**（2026-08-23 に決めた「空中に浮かぶ埃表現（パーティクル）」）。
 *
 * ## 何を作るか
 * カメラを中心とした箱の中に点を撒き、ゆっくり漂わせる。**箱はカメラに追い掛けさせる**
 * ので、どこへ行っても密度が変わらない。箱の外へ出た点は反対側から入り直す（巻き戻し）。
 *
 * | | |
 * |---|---|
 * | 描き方 | `GL_POINTS` 1 回。`gl_PointCoord` で丸く落とす |
 * | 深度 | **読むが書かない。**壁の向こうの埃は隠れるが、埃どうしは重なってよい |
 * | 混ぜ方 | 加算。**線形の HDR へ描く**ので、ブルームと被写界深度が後から掛かる |
 * | 光 | 環境光の色を掛ける（夜は青く、松明の間では暖かく見える） |
 *
 * ## なぜ 1 回の描画で済むのか
 * 点の位置は**種から毎フレーム計算する**（頂点シェーダの中）。CPU 側は何も動かさない
 * ので、10,000 粒でも送るものは無い。動きは時刻と種だけで決まるから、
 * スクリプトから撮るとき（`--motion-time=`）も同じ絵が出る。
 *
 * ## 既定
 * `DustParams{}` は `amount = 0`（描かない）。強さは `Hd2dSettings::dust` が運ぶ。
 */
#pragma once

#include "render/gl_core.h"
#include "render/math3d.h"
#include "render/post_process.h" //!< `DofView`（粒は同じ錯乱円で広がる）

#include <string>

namespace hd2d {

//! 埃の見え方。**1 フレーム 1 個。**
struct DustParams {
    //! 強さ（0 = 描かない。**ここで早く抜ける**）。密度と明るさの両方に掛かる。
    float amount{ 0.f };
    //! 撒く箱の一辺（マス）。カメラを中心に置く。
    float span{ 26.f };
    /*!
     * @brief **1 マスあたりの粒の数**（2026-08-23 に決めた「粒子は 1 マス辺りで密度を決めて」）。
     * @details 描く数は `per_cell × span³ × amount`（`max_motes` で頭打ち）。
     * 箱の大きさを変えても**見た目の詰まり方が変わらない**のがこの数え方の要点である。
     *
     * **2026-09-01 に 0.7 → 0.233（1/3）へ落とした**（こう決めた——「全てのコアで
     * パーティクルの量を 1/3 にして」）。**`amount`（cfg の `dust=`）では下げていない**
     * ——あちらは `gain = brightness × amount` で**明るさにも掛かる**ので、
     * 1/3 にすると量だけでなく見え方まで変わる。ここは数にしか掛からない。
     * **コアごとの cfg は 0.35 のまま**で、この既定値が全コアに効く。
     */
    float per_cell{ 0.233f };
    /*!
     * @brief **手前を疎にする距離**（マス。2026-08-23 に決めた「手前は疎になるように」）。
     * @details カメラからこの距離までは粒を間引く（薄くするのではなく**数を減らす**）。
     * 近い粒は画面で大きく写るので、同じ密度のままだと視界を塞ぐ。
     */
    float near_thin{ 5.f };
    //! 粒の大きさ（画素）。**ピントが合っているとき**の大きさで、ぼけると広がる。
    float size_px{ 4.2f };
    /*!
     * @brief ぼけて広がるときの**上限**（画素）。
     * @details 点光源のぼけは実際どこまでも大きくなるが、埃でそれをやると画面いっぱいの
     * 丸い膜になる（実際に 1 度そうなった）。
     */
    float max_bokeh_px{ 7.f };
    //! 漂う速さ（マス/秒）。上へゆっくり上がりながら横へ揺れる。
    float speed{ 0.16f };
    //! 明るさ（線形の HDR。環境光の色に掛ける）。
    float brightness{ 2.4f };
    //! 粒の数の上限（密度から出した数がこれを超えたら頭打ち）。
    int max_motes{ 16384 };
};

/*!
 * @brief 埃を描く。**世界を描き終えた後・後処理の前**に呼ぶ。
 * @details 線形の HDR の FBO へ加算で描くので、ブルームと被写界深度が後から掛かる。
 */
class DustMotes {
public:
    bool init(std::string &err);
    void shutdown();
    bool ready() const { return this->program_ != 0; }

    /*!
     * @brief 1 フレーム描く。
     * @param view_projection 視点の行列
     * @param camera_pos カメラの位置（マス）。**箱の中心**になる
     * @param tint 粒に掛ける色（環境光の色を渡す）
     * @param time 秒（`--motion-time=` と同じ時計を渡すこと。撮り比べが揃う）
     * @param params 見え方。`amount` が 0 なら**何も描かない**
     * @param dof 被写界深度の測り（`measure_dof()`）。**粒は自分でぼけた大きさへ広がる**
     * @param focus 焦点（キャラの水平位置。マス）
     * @param screen_h 画面の高さ（画素）。点の大きさを画素で決めるのに要る
     */
    void draw(const Mat4 &view_projection, const Vec3 &camera_pos, const Vec3 &tint, float time,
        const DustParams &params, const DofView &dof, const Vec3 &focus, int screen_h);

private:
    gl::GLuint program_{ 0 };
    gl::GLuint vao_{ 0 };
    gl::GLuint vbo_{ 0 };
    int mote_count_{ 0 };
    gl::GLint loc_view_projection_{ -1 };
    gl::GLint loc_camera_{ -1 };
    gl::GLint loc_span_{ -1 };
    gl::GLint loc_size_px_{ -1 };
    gl::GLint loc_speed_{ -1 };
    gl::GLint loc_time_{ -1 };
    gl::GLint loc_color_{ -1 };
    gl::GLint loc_viewport_h_{ -1 };
    gl::GLint loc_per_cell_{ -1 };
    gl::GLint loc_near_thin_{ -1 };
    gl::GLint loc_focus_{ -1 };
    gl::GLint loc_dof_{ -1 };
    gl::GLint loc_forward_{ -1 };
    gl::GLint loc_max_bokeh_{ -1 };
};

} // namespace hd2d
