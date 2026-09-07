/*!
 * @file ground_ring.h
 * @brief 足元に敷く**平たいリング**。
 *
 * ## 何のためのものか
 * 2026-08-22 に決めた「板の足元に円形の、警戒度に応じた色を変えるリングを
 * 表示させる」。Sil-Q の隠密は「まだこちらに気づいていない敵」を見分けられるかで決まるが、
 * コアはそれを **attr の背景セット**（`attr = 前景 + 32 × 背景セット`）に載せており、
 * ボクセルの画には**受け皿が無い**——地の色を塗るという表現がそもそも無い。
 * 足元のリングなら、板でもアスキー実体でも、見下ろしでも一人称でも同じように読める。
 *
 * ## 描き方
 * - 1 枚 = 地面に**寝かせた**四角（XY 平面）。頂点は原点中心の単位四角で、
 *   インスタンスが中心・半径・色を渡す。**カメラの向きに依らない**（ビルボードではない）
 * - 円環は**フラグメントで作る**（中心からの距離で内外を抜く）。テクスチャを持たないので
 *   目録も焼きも要らない
 * - **深度は読むが書かない。**床の上に貼る飾りなので、後ろのものを隠してはいけない。
 *   床との z 争いを避けるために少しだけ浮かせる（`z` に下駄を履かせるのは呼び出し側）
 * - α で混ぜる（`GL_SRC_ALPHA`）。抜きにすると縁が階段状になり、輪が汚く見える
 *
 * ## 光を掛けない
 * リングは**情報**であって物ではない。暗がりで見えなくなると、隠密の読み合いが
 * 「明るい所でしか働かない」ことになる。アスキー実体の字と同じ理屈（`§5.3`）。
 */
#pragma once

#include "render/gl_core.h"
#include "render/math3d.h"

#include <cstddef>
#include <string>

namespace hd2d {

//! リング 1 枚。位置は**輪の中心**（＝マスの中心。足元の高さ）。
struct GroundRingInstance {
    float x{};
    float y{};
    float z{};
    float radius{ 0.45f }; //!< 外周の半径（マス単位）
    float r{ 1.f };
    float g{ 1.f };
    float b{ 1.f };
    float a{ 1.f };
};

class GroundRingRenderer {
public:
    bool init(std::string &err);
    void shutdown();
    bool ready() const { return this->program_ != 0; }

    /*!
     * @brief まとめて描く。
     * @param inner_ratio 内周（外周に対する比）。0.7 くらいで細い輪になる
     * @details **不透明を全部描いた後**に呼ぶこと（深度を読んで書かないので、
     * 先に描くと後ろの床に隠されない代わりに前の物へ乗ってしまう）。
     */
    void draw(const Mat4 &view_projection, const GroundRingInstance *instances, std::size_t count,
        float inner_ratio = 0.68f);

private:
    void upload_instances(const GroundRingInstance *instances, std::size_t count);

    gl::GLuint program_{ 0 };
    gl::GLuint vao_{ 0 };
    gl::GLuint vbo_{ 0 };
    gl::GLuint instance_vbo_{ 0 };
    std::size_t instance_capacity_{ 0 };
    gl::GLint loc_view_projection_{ -1 };
    gl::GLint loc_inner_{ -1 };
};

} // namespace hd2d
