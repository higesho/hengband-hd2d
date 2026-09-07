/*!
 * @file billboard_renderer.h
 * @brief 軸拘束ビルボード（P4 ②）。**シフトレンズを捨てられた理由そのもの。**
 *
 * 基準はボクセル HD2D の設計 §4.2・§11。
 *
 * ## 軸拘束とは（§4.2）
 * 旧 HD2D は「かきわり（＝軸に固定した板）が上すぼまりに歪む」のを避けるために
 * シフトレンズを使っていた。**世界の垂直軸まわりにだけ回り、常にカメラの方位を向く板**に
 * すれば、どの角度からでも正対した長方形として写るので**歪みようがない**。
 * これが普通の透視カメラへ移れた理由である。
 *
 * ## 完全ビルボードにしない理由
 * カメラの方向へ完全に向けると、見下ろしたとき板が寝てしまう（地面に貼りついて見える）。
 * 回すのは水平方向だけにする。
 *
 * ## 描き方
 * - 1 枚 = 4 頂点。インスタンスごとに世界座標・大きさ・アトラスの矩形・色を渡す
 * - `GL_NEAREST`（ドット絵を眠らせない。§11）
 * - **α で抜く**（`discard`）。半透明で混ぜず、深度も正しく書く
 *
 * ## 影（P5 ①）— **落とすし、受ける**
 * 設計書 §13 は「接地影・建物の落とす影」を 1 行に並べていて、P5 の完了条件も
 * 「**接地影が落ち**」である。したがってビルボードも影の地図へ書く。
 *
 * ただし**影のパスでは板をカメラではなく光へ向ける**（`draw_depth` の `right`）。
 * カメラ向きのまま書くと、**カメラを回すたびに影の形が回る**（人の影が伸び縮みする）。
 * 光に対して垂直に立てておけば、影の形はカメラに依らず決まる。
 *
 * 光の式はボクセルと同じ `render/lighting.h` を使う。ただし
 * **絵はもともと陰影付きで描かれている**ので、光に背を向けた側を真っ黒にはしない。
 */
#pragma once

#include "render/gl_core.h"
#include "render/lighting.h"
#include "render/math3d.h"
#include "render/scene_look.h"

#include <cstddef>
#include <string>

namespace hd2d {

//! ビルボード 1 枚。位置は**足元**（板はここから上へ立つ）。
struct BillboardInstance {
    float x{};
    float y{};
    float z{};
    float width{ 1.f }; //!< マス単位
    float height{ 1.f };
    //! アトラス上の矩形（テクセル）。
    float u0{};
    float v0{};
    float u1{};
    float v1{};
    float r{ 1.f };
    float g{ 1.f };
    float b{ 1.f };
};

class BillboardRenderer {
public:
    bool init(std::string &err);
    void shutdown();

    /*!
     * @brief このフレームの**画調**。
     * @details 既定は標準（`enabled = 0`）なので、呼ばなければ従来と 1 ビットも変わらない。
     * @note **ボクセルにも同じものを渡すこと。**片方だけ TRON にすると、
     * 同じ場所に立つ壁と人物で画調が食い違う。
     */
    void set_look(const LookParams &look);

    /*!
     * @brief まとめて描く。
     * @param camera_azimuth 板を向ける方位（ラジアン）。カメラの水平方向。
     * @param atlas スプライトのアトラス（`GL_RGBA8`）。
     * @param atlas_side アトラスの辺（テクセル座標を正規化するのに使う）。
     * @param shadow_texture 影の深度テクスチャ。0 なら影を受けない。
     * @param face_point **ここへ正対させる**（世界座標。`nullptr` なら `camera_azimuth`）。
     * @details 2026-08-14 に決めた「HMD に正対ではなく、キャラの座標に対して
     * 正対させる」。VR で頭に正対させると、首を振るたびに世界じゅうの板が回る。
     * @param fullbright 真なら**場の光を掛けない**。
     *   アスキー実体のための枝で、既定の偽では従来と 1 ビットも変わらない。
     */
    void draw(const Mat4 &view_projection, const SceneLighting &lighting, float camera_azimuth, const Vec3 &eye,
        const Mat4 &light_view_projection, gl::GLuint shadow_texture, int shadow_side,
        gl::GLuint atlas, int atlas_side, const BillboardInstance *instances, std::size_t count,
        const Vec3 *face_point = nullptr, bool fullbright = false);

    /*!
     * @brief 影の地図へ書く（P5 ①）。
     * @param light_dir 面から光源へ向かう向き。**板はこれに垂直に立てる**（ヘッダの注記）。
     * @details α で抜くので、影の形はスプライトの輪郭そのものになる。
     * 板は裏からも書くので面の除去はしない。
     */
    void draw_depth(const Mat4 &light_view_projection, const Vec3 &light_dir,
        gl::GLuint atlas, int atlas_side, const BillboardInstance *instances, std::size_t count);

private:
    void upload_instances(const BillboardInstance *instances, std::size_t count);

    gl::GLuint program_{ 0 };
    gl::GLuint vao_{ 0 };
    gl::GLuint vbo_{ 0 };
    gl::GLuint instance_vbo_{ 0 };
    std::size_t instance_capacity_{ 0 };
    gl::GLint loc_view_projection_{ -1 };
    gl::GLint loc_right_{ -1 };
    //! かきわりを向ける先（`u_face_point`。z ≧ 1 でその点へ正対）。
    gl::GLint loc_face_point_{ -1 };
    gl::GLint loc_eye_{ -1 };
    gl::GLint loc_depth_bias_{ -1 };
    gl::GLint loc_atlas_{ -1 };
    gl::GLint loc_atlas_side_{ -1 };
    gl::GLint loc_facing_{ -1 };
    //! 場の光を掛けないか（アスキー実体。`u_fullbright`）。
    gl::GLint loc_fullbright_{ -1 };
    LightUniforms light_loc_;
    LookUniforms look_loc_;
    //! 画調（`set_look`）。**既定は標準**。
    LookParams look_;

    gl::GLuint depth_program_{ 0 };
    gl::GLint depth_loc_view_projection_{ -1 };
    gl::GLint depth_loc_right_{ -1 };
    //! 影のパスでは使わない（0 を入れて「向ける先なし」にする）。
    gl::GLint depth_loc_face_point_{ -1 };
    gl::GLint depth_loc_eye_{ -1 };
    gl::GLint depth_loc_depth_bias_{ -1 };
    gl::GLint depth_loc_atlas_{ -1 };
    gl::GLint depth_loc_atlas_side_{ -1 };
};

} // namespace hd2d
