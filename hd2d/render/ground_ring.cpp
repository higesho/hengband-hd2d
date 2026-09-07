/*!
 * @file ground_ring.cpp
 * @brief `ground_ring.h` の実装。
 */
#include "render/ground_ring.h"

#include "render/gl_program.h"

#include <cstddef>
#include <string>

namespace hd2d {

using namespace hd2d::gl;

namespace {

/*
 * 四角は原点中心の `[-1, 1]²`（XY 平面に**寝ている**）。半径を掛けて中心へ寄せる。
 * `a_corner` はそのままフラグメントへ渡り、中心からの距離が輪の内外を決める。
 */
constexpr float kCorners[] = {
    -1.f, -1.f, 1.f, -1.f, 1.f, 1.f,
    -1.f, -1.f, 1.f, 1.f, -1.f, 1.f,
};

const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec2 a_corner;    // (-1..1, -1..1)
layout(location = 1) in vec3 a_center;    // 輪の中心（マス単位）
layout(location = 2) in float a_radius;   // 外周（マス単位）
layout(location = 3) in vec4 a_color;
uniform mat4 u_view_projection;
out vec2 v_local;
out vec4 v_color;
void main()
{
    // **寝かせる。**z は動かさない（地面に貼る飾りなので、傾けると読みにくくなる）。
    vec3 world = a_center + vec3(a_corner * a_radius, 0.0);
    gl_Position = u_view_projection * vec4(world, 1.0);
    v_local = a_corner;
    v_color = a_color;
}
)";

const char *const kFragmentSource = R"(#version 460 core
in vec2 v_local;
in vec4 v_color;
uniform float u_inner;   // 内周（外周に対する比）
out vec4 o_color;
void main()
{
    float d = length(v_local);
    if ((d > 1.0) || (d < u_inner)) {
        discard;
    }
    /*
     * 縁をなめらかに。**画素の大きさに合わせて**ぼかす（`fwidth`）ので、
     * 遠くの小さい輪でも近くの大きい輪でも同じ細さに見える。
     * 固定幅でぼかすと、遠景で輪が消えるか近景でにじむかのどちらかになる。
     */
    float soft = max(fwidth(d), 1e-4);
    float edge = smoothstep(1.0, 1.0 - soft, d) * smoothstep(u_inner, u_inner + soft, d);
    o_color = vec4(v_color.rgb, v_color.a * edge);
}
)";

} // namespace

bool GroundRingRenderer::init(std::string &err)
{
    this->program_ = compile_program(kVertexSource, kFragmentSource, err);
    if (this->program_ == 0) {
        return false;
    }
    this->loc_view_projection_ = glGetUniformLocation(this->program_, "u_view_projection");
    this->loc_inner_ = glGetUniformLocation(this->program_, "u_inner");

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
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GroundRingInstance),
        reinterpret_cast<const void *>(offsetof(GroundRingInstance, x)));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(GroundRingInstance),
        reinterpret_cast<const void *>(offsetof(GroundRingInstance, radius)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(GroundRingInstance),
        reinterpret_cast<const void *>(offsetof(GroundRingInstance, r)));
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "足元のリングの初期化で GL エラー: " + gl_err;
        return false;
    }
    return true;
}

void GroundRingRenderer::shutdown()
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
}

void GroundRingRenderer::upload_instances(const GroundRingInstance *instances, std::size_t count)
{
    glBindBuffer(GL_ARRAY_BUFFER, this->instance_vbo_);
    const std::size_t bytes = count * sizeof(GroundRingInstance);
    if (bytes > this->instance_capacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), instances, GL_STREAM_DRAW);
        this->instance_capacity_ = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), instances);
    }
}

void GroundRingRenderer::draw(
    const Mat4 &view_projection, const GroundRingInstance *instances, std::size_t count, float inner_ratio)
{
    if ((this->program_ == 0) || (count == 0) || (instances == nullptr)) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    //! **深度を書かない。**床の上に貼る飾りなので、後ろのものを隠してはいけない。
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE); //!< 寝ているので裏から見ることがある（一人称・VR）
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(this->program_);
    glUniformMatrix4fv(this->loc_view_projection_, 1, GL_FALSE, view_projection.m);
    glUniform1f(this->loc_inner_, inner_ratio);

    glBindVertexArray(this->vao_);
    this->upload_instances(instances, count);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(count));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    //! **元へ戻す。**この後に不透明を描く道があるので、深度書き込みは必ず起こす。
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace hd2d
