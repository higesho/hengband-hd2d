/*!
 * @file xr_room.h
 * @brief VR の**部屋と卓**、そして疑似 HMD 用の板の実体（2026-08-14 に決めた）。
 *
 *
 * ## 何を描くか
 * 「**ジオラマは VR 空間に畳 1 畳分くらいのテーブルがあり、そこにジオラマのマップが
 * ある**」「**何も無い方向は暗い部屋で埋める**」。ここはその 2 つだけを描く。
 *
 * | 物 | いつ出るか |
 * | --- | --- |
 * | 暗い部屋（床・壁・天井） | ジオラマのときだけ |
 * | 卓（畳 1 畳・脚 4 本） | 同上 |
 * | 板（テクスチャ付きの矩形） | **疑似 HMD（`--vr-fake`）のときだけ** |
 *
 * 一人称では**どれも出さない**。あちらは「完全にゲーム空間（全天球）に入り込む」
 * ので、部屋を置くと世界が二重になる。
 *
 * ## ここは「ステージ空間」を描く唯一の場所である
 * 世界（マス）を描く他の描き手と違い、**メートルのステージ座標をそのまま**受け取る
 * （`world_to_stage` を通さない）。呼ぶ側が渡す行列も `射影 × 目の視点行列` であって、
 * 盤の写像は掛かっていない。深度バッファは世界の幾何と**共用**する——どちらも
 * 目から見たメートルなので、卓の縁が盤に正しく前後する。
 *
 * ## 板を「実体」で描くのは疑似 HMD だけ
 * 実物の VR では板はクワッドレイヤ（コンポジタが直接標本化する）で、シーンには
 * 混ぜない（設計書 §7）。疑似 HMD にはコンポジタが無いので、**同じ置き場所の
 * 矩形をシーンの中に描いて代用する**。文字のくっきり度だけは実機と差が出る。
 */
#pragma once

#include "render/gl_core.h"
#include "render/math3d.h"
#include "xr/xr_math.h"

#include <string>

namespace hd2d::xr {

/*!
 * @brief 部屋・卓・板を描く。
 * @details 網は**据え方が変わったときだけ**組み直す（毎フレームではない）。
 * 卓の高さは頭の高さから決まるので、再中心のたびに 1 回組み直ることになる。
 */
class RoomRenderer {
public:
    bool init(std::string &err);
    void shutdown();

    /*!
     * @brief 部屋と卓を描く。
     * @param stage_view_projection `射影 × 目の視点行列`（**盤の写像を掛けない**）。
     * @param room 据え方。`visible` が偽なら何もしない。
     * @details 深度は読み書きとも有効のまま、面の裏表は見ない（部屋は内側から見るので、
     * 世界と同じ裏面カリングの約束を持ち込むと壁が消える）。
     */
    void draw(const Mat4 &stage_view_projection, const RoomPlacement &room);

    /*!
     * @brief 板を 1 枚描く（**疑似 HMD 専用**）。
     * @param stage_view_projection 同上。
     * @param texture UI を作ったテクスチャ。
     * @param pose 板の据わり方（`place_hud_panel` の答え）。
     * @param u0,v0,u1,v1 テクスチャの中のどこを写すか（0〜1。**v は下が 0**）。
     * @details α で抜くので、描かなかった所は透ける。深度は**書かない**
     * （板どうしが重なったときに手前の板が奥の板を消さないように）。
     */
    void draw_panel(const Mat4 &stage_view_projection, gl::GLuint texture, const PanelPose &pose,
        float u0, float v0, float u1, float v1);

private:
    void rebuild(const RoomPlacement &room);

    gl::GLuint program_{ 0 };
    gl::GLuint vao_{ 0 };
    gl::GLuint vbo_{ 0 };
    //! 柄（木目・丸太・ガラス・石畳）の 2×2 アトラス。**`init()` がその場で焼く。**
    gl::GLuint atlas_{ 0 };
    gl::GLsizei vertex_count_{ 0 };
    gl::GLuint panel_program_{ 0 };
    gl::GLuint panel_vao_{ 0 };
    gl::GLuint panel_vbo_{ 0 };
    //! 最後に組んだ据え方（これと違うときだけ組み直す）。
    RoomPlacement built_{};
    bool has_built_{ false };

    struct Uniforms {
        gl::GLint view_projection{ -1 };
        gl::GLint atlas{ -1 };
    } loc_;
    struct PanelUniforms {
        gl::GLint view_projection{ -1 };
        gl::GLint center{ -1 };
        gl::GLint right{ -1 };
        gl::GLint up{ -1 };
        gl::GLint uv0{ -1 };
        gl::GLint uv1{ -1 };
        gl::GLint image{ -1 };
    } panel_loc_;
};

/*!
 * @brief UI を焼いておく面（**疑似 HMD 専用**）。
 * @details 実物の VR では板の中身はクワッドレイヤの swapchain へ直に描くが、
 * 疑似 HMD にはそれが無いので、自前のテクスチャへ 1 枚焼いて板に貼る。
 *
 * @note **1 フレーム遅れる。** UI は目の絵より後に描かれるので、貼るのは前の
 * フレームに作ったものになる。静止画（`--shot`）で置き場所を見るには十分で、
 * ここを直すには主ループの順序を入れ替えることになる（実機には要らない工事）。
 */
class UiSurface {
public:
    //! 大きさが変わっていたら作り直す。作れたら true。
    bool ensure(int w, int h);
    void shutdown();
    //! 描き先にする（透明で消す）。0 なら作れていない。
    gl::GLuint bind();
    gl::GLuint texture() const { return this->texture_; }
    int width() const { return this->width_; }
    int height() const { return this->height_; }

private:
    gl::GLuint fbo_{ 0 };
    gl::GLuint texture_{ 0 };
    int width_{ 0 };
    int height_{ 0 };
};

} // namespace hd2d::xr
