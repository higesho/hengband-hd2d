/*!
 * @file light_shaft.cpp
 * @brief `light_shaft.h` の実装。
 */
#include "render/light_shaft.h"

#include "render/gl_program.h"

#include <cmath>
#include <cstddef>
#include <string>

namespace hd2d {

using namespace hd2d::gl;

namespace {

/*
 * 面は原点の `[-1, 1] × [0, 1]`（横 × 縦）。横は `x`、縦は `y` で、
 * 縦は 0 が床・1 が上端。**縦に立っている**（`ground_ring` は寝ていた）。
 *
 * 形（満ち／抜け／縁）は**全部フラグメントで作る**。頂点を増やして刻むと、
 * 「下 2 ブロックまで満ちる」を高さの比で書けなくなる（マスごとに高さが違いうる）。
 */
constexpr float kCorners[] = {
    -1.f, 0.f, 1.f, 0.f, 1.f, 1.f,
    -1.f, 0.f, 1.f, 1.f, -1.f, 1.f,
};

const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec2 a_corner;    // x = -1..1 / y = 0..1
layout(location = 1) in vec3 a_base;      // マスの中心・床の高さ（マス単位）
layout(location = 2) in vec3 a_size;      // (幅の半分, 高さ, 満ちている高さ)
layout(location = 3) in vec3 a_slant;     // (上端の水平のずれ x, y, 上端の幅の比)
layout(location = 4) in vec4 a_color;
uniform mat4 u_view_projection;
uniform vec2 u_right;                     // カメラの方位から作った「横」の向き（XY）
out vec2 v_local;
out float v_full;                         // 満ちている高さ（0..1 の比）
out vec4 v_color;
void main()
{
    /*
     * 幅は床でマスいっぱい、**上へ行くほど少しだけ狭い**（`a_slant.z`）。
     * 完全に平行にすると柵の板に見える（2026-08-22 に決めた）。
     */
    float w = mix(1.0, a_slant.z, a_corner.y);
    vec2 side = u_right * (a_corner.x * a_size.x * w);
    /*
     * **斜めに落とす。** 上へ行くほど横へずらす。まっすぐ立てると柵に見えるが、
     * 傾けると「差し込んでいる」ように読める。
     */
    vec2 lean = a_slant.xy * a_corner.y;
    vec3 world = a_base + vec3(side + lean, a_corner.y * a_size.y);
    gl_Position = u_view_projection * vec4(world, 1.0);
    v_local = a_corner;
    v_full = (a_size.y > 0.0) ? clamp(a_size.z / a_size.y, 0.0, 1.0) : 0.0;
    v_color = a_color;
}
)";

const char *const kFragmentSource = R"(#version 460 core
in vec2 v_local;
in float v_full;
in vec4 v_color;
out vec4 o_color;
void main()
{
    float h = clamp(v_local.y, 0.0, 1.0);
    float across = clamp(abs(v_local.x), 0.0, 1.0);

    /*
     * **下 `v_full` までは満ちている**（`drain` が 0）。そこから上で中央が抜けていく
     * ——`drain` が 1 に近づくほど、中央だけ薄くなって左右の縁が残る（「減水」）。
     */
    float drain = smoothstep(v_full, 1.0, h);
    /*
     * 残す量。`drain` が 0 なら一様（＝満ちている）、1 なら**縁だけ**。
     * `0.45` から立ち上げるので、抜けきった所では左右に幅の 1/4 ずつが残って筋に見える。
     */
    float keep = mix(1.0, smoothstep(0.45, 0.92, across), drain);

    /*
     * 上端は 0 まで落とす。**切り口を見せない**のがいちばん大事で、
     * ここを落とさないと宙に平行四辺形の切り口が浮く。
     *
     * ただし**落とすのは最後の 1 割だけ**にする。ここを早くから効かせると、
     * 上のほうが全部消えて「左右を残して中央が減水」（そう決めた）が見えなくなる
     * ——初版は 0.80 から落としていて、縁の筋が残らなかった。
     */
    float top = 1.0 - smoothstep(0.90, 1.0, h);
    /*
     * 左右の外側もほんの少しぼかす。角を立てると光ではなく板の輪郭に見える。
     * 床の際も少しだけ抜く——真下は床の `emissive` が担当なので、
     * ここまで濃いと二重に明るくなって白飛びする。
     */
    float edge = 1.0 - smoothstep(0.92, 1.0, across);
    float bottom = smoothstep(0.0, 0.06, h);

    float a = v_color.a * keep * top * edge * bottom;
    /*
     * **加算で混ぜる**ので、色に α を掛けてから出す（`GL_SRC_ALPHA, GL_ONE`）。
     * α をそのまま返すと混ぜ方に依らず同じ濃さになってしまう。
     */
    o_color = vec4(v_color.rgb * a, a);
}
)";

} // namespace

bool LightShaftRenderer::init(std::string &err)
{
    this->program_ = compile_program(kVertexSource, kFragmentSource, err);
    if (this->program_ == 0) {
        return false;
    }
    this->loc_view_projection_ = glGetUniformLocation(this->program_, "u_view_projection");
    this->loc_right_ = glGetUniformLocation(this->program_, "u_right");

    glGenVertexArrays(1, &this->vao_);
    glGenBuffers(1, &this->vbo_);
    glGenBuffers(1, &this->instance_vbo_);
    glBindVertexArray(this->vao_);

    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(kCorners)), kCorners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, this->instance_vbo_);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LightShaftInstance),
        reinterpret_cast<const void *>(offsetof(LightShaftInstance, x)));
    glVertexAttribDivisor(1, 1);
    //! `radius` `height` `full` は**この順に並んでいる**（`vec3` で 1 度に採る）。
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(LightShaftInstance),
        reinterpret_cast<const void *>(offsetof(LightShaftInstance, radius)));
    glVertexAttribDivisor(2, 1);
    //! `slant_x` `slant_y` `taper` は**この順に並んでいる**（`vec3` で 1 度に採る）。
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(LightShaftInstance),
        reinterpret_cast<const void *>(offsetof(LightShaftInstance, slant_x)));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(LightShaftInstance),
        reinterpret_cast<const void *>(offsetof(LightShaftInstance, r)));
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "光の差し込みの初期化で GL エラー: " + gl_err;
        return false;
    }
    return true;
}

void LightShaftRenderer::shutdown()
{
    if (this->instance_vbo_ != 0) {
        glDeleteBuffers(1, &this->instance_vbo_);
        this->instance_vbo_ = 0;
    }
    if (this->vbo_ != 0) {
        glDeleteBuffers(1, &this->vbo_);
        this->vbo_ = 0;
    }
    if (this->vao_ != 0) {
        glDeleteVertexArrays(1, &this->vao_);
        this->vao_ = 0;
    }
    if (this->program_ != 0) {
        glDeleteProgram(this->program_);
        this->program_ = 0;
    }
    this->instance_capacity_ = 0;
}

void LightShaftRenderer::upload_instances(const LightShaftInstance *instances, std::size_t count)
{
    glBindBuffer(GL_ARRAY_BUFFER, this->instance_vbo_);
    const std::size_t bytes = count * sizeof(LightShaftInstance);
    if (bytes > this->instance_capacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), instances, GL_STREAM_DRAW);
        this->instance_capacity_ = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), instances);
    }
}

void LightShaftRenderer::draw(
    const Mat4 &view_projection, float azimuth, const LightShaftInstance *instances, std::size_t count)
{
    if ((this->program_ == 0) || (count == 0) || (instances == nullptr)) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    //! **深度を書かない。** 光が後ろのものを隠してはいけない（壁の向こうは深度で隠れる）。
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE); //!< 面なので裏から見ることがある（一人称・VR）
    glEnable(GL_BLEND);
    //! **加算。** 光なので重なるほど明るくなる（α 混合だと霧になり、床の色を殺す）。
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glUseProgram(this->program_);
    glUniformMatrix4fv(this->loc_view_projection_, 1, GL_FALSE, view_projection.m);
    /*
     * 面の「横」。カメラの方位に**直交**する向きを XY で作る（軸拘束ビルボード）。
     * 上は世界の +Z のままなので、見上げても光は倒れない。
     */
    glUniform2f(this->loc_right_, std::cos(azimuth), std::sin(azimuth));

    glBindVertexArray(this->vao_);
    this->upload_instances(instances, count);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(count));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    //! **元へ戻す。** この後に不透明を描く道があるので、深度書き込みは必ず起こす。
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

} // namespace hd2d
