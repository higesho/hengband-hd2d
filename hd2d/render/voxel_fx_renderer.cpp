/*!
 * @file voxel_fx_renderer.cpp
 * @brief `voxel_fx_renderer.h` の中身。
 */
#include "render/voxel_fx_renderer.h"

#include "render/gl_program.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

using namespace hd2d::gl;

namespace hd2d {

namespace {

/*
 * インスタンス 1 個は float 14 個: 中心(3) 大きさ(3) 色+不透明度(4) 自己発光・yaw・pitch・予備(4)。
 * 立方体の向きは `yaw`（z 軸まわり）と `pitch`（上向きが正）の 2 つだけで、
 * `voxel_fx.cpp` の `rotate_local()` と**同じ式**で回す。
 */
const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec3 a_pos;      // 立方体の頂点（±0.5）
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 i_center;   // 世界座標（マス）
layout(location = 3) in vec3 i_size;    // 各軸の一辺（マス）
layout(location = 4) in vec4 i_color;    // rgb = 色, a = 不透明度
layout(location = 5) in vec4 i_misc;     // x = 自己発光, y = yaw, z = pitch
uniform mat4 u_view_projection;
out vec3 v_normal;
out vec3 v_world;
out vec4 v_color;
out float v_emissive;

vec3 fx_rotate(vec3 v, float yaw, float pitch)
{
    float cp = cos(pitch);
    float sp = sin(pitch);
    float cy = cos(yaw);
    float sy = sin(yaw);
    float x1 = (v.x * cp) - (v.z * sp);
    float z1 = (v.x * sp) + (v.z * cp);
    return vec3((x1 * cy) - (v.y * sy), (x1 * sy) + (v.y * cy), z1);
}

void main()
{
    vec3 world = i_center + fx_rotate(a_pos * i_size, i_misc.y, i_misc.z);
    gl_Position = u_view_projection * vec4(world, 1.0);
    v_normal = fx_rotate(a_normal, i_misc.y, i_misc.z);
    v_world = world;
    v_color = i_color;
    v_emissive = i_misc.x;
}
)";

/*
 * 光の式は `kLightingGlsl` を連結する（地形・ビルボードと同じ式でなければ、同じ場所の壁と
 * 火の玉の明るさが食い違う）。自己発光は光を掛けずに足す——自分で光っているものに遮蔽は関係ない。
 */
const char *const kFragmentBody = R"(
in vec3 v_normal;
in vec3 v_world;
in vec4 v_color;
in float v_emissive;
out vec4 o_color;
void main()
{
    vec3 n = normalize(v_normal);
    float ndl = max(dot(n, u_light_dir), 0.0);
    float shadow = hd2d_key_shadow(v_world, ndl);
    vec3 direct = u_key_color * ndl * shadow;
    vec3 indirect = hd2d_ambient(n) + hd2d_point_lights(v_world, n, 0.10);
    /*
     * 自分で光るものは、周りの光で白く飛ばさない（火のボクセルが爆発の光を浴びて黄白に飽和した）。
     * 自己発光が強いほど、受ける光を割り引く。0 なら地形と同じ式のまま。
     */
    vec3 lit = (direct + indirect) / (1.0 + (1.5 * v_emissive));
    o_color = vec4((v_color.rgb * lit) + (v_color.rgb * v_emissive), v_color.a);
}
)";

constexpr std::size_t kFloatsPerInstance = 14;

//! 立方体 1 個（24 頂点：位置 3 + 法線 3、36 添字）。巻きは `VoxelRenderer` のメッシュと同じ向き。
struct CubeMesh {
    float vertices[24 * 6];
    std::uint16_t indices[36];
};

CubeMesh make_cube()
{
    CubeMesh mesh{};
    // 面ごとの法線 n と接線 u, v（u × v = n。右手系で外から見て反時計回り）。
    struct Face {
        float n[3];
        float u[3];
        float v[3];
    };
    const Face faces[6] = {
        { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
        { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } },
        { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { 0, 0, -1 }, { 0, 1, 0 }, { 1, 0, 0 } },
    };
    const float su[4] = { -0.5f, 0.5f, 0.5f, -0.5f };
    const float sv[4] = { -0.5f, -0.5f, 0.5f, 0.5f };
    for (int f = 0; f < 6; ++f) {
        for (int c = 0; c < 4; ++c) {
            float *dst = &mesh.vertices[((f * 4) + c) * 6];
            for (int a = 0; a < 3; ++a) {
                dst[a] = (faces[f].n[a] * 0.5f) + (faces[f].u[a] * su[c]) + (faces[f].v[a] * sv[c]);
                dst[3 + a] = faces[f].n[a];
            }
        }
        const std::uint16_t base = static_cast<std::uint16_t>(f * 4);
        const std::uint16_t order[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < 6; ++i) {
            mesh.indices[(f * 6) + i] = static_cast<std::uint16_t>(base + order[i]);
        }
    }
    return mesh;
}

//! 列優先の回転行列（式は `voxel_fx.cpp` の `rotate_local()` とシェーダと同じ向き）。
Mat4 rotation_z(float a)
{
    Mat4 m = Mat4::identity();
    m.m[0] = std::cos(a);
    m.m[1] = std::sin(a);
    m.m[4] = -std::sin(a);
    m.m[5] = std::cos(a);
    return m;
}

Mat4 rotation_x(float a)
{
    Mat4 m = Mat4::identity();
    m.m[5] = std::cos(a);
    m.m[6] = std::sin(a);
    m.m[9] = -std::sin(a);
    m.m[10] = std::cos(a);
    return m;
}

Mat4 rotation_y(float a)
{
    Mat4 m = Mat4::identity();
    m.m[0] = std::cos(a);
    m.m[2] = -std::sin(a);
    m.m[8] = std::sin(a);
    m.m[10] = std::cos(a);
    return m;
}

} // namespace

bool VoxelFxRenderer::init(std::string &err)
{
    const std::string fragment = std::string("#version 460 core\n") + kLightingGlsl + kFragmentBody;
    this->program_ = compile_program(kVertexSource, fragment.c_str(), err);
    if (this->program_ == 0) {
        return false;
    }
    this->loc_view_projection_ = glGetUniformLocation(this->program_, "u_view_projection");
    this->light_loc_.locate(this->program_);

    const CubeMesh mesh = make_cube();
    glGenVertexArrays(1, &this->vao_);
    glGenBuffers(1, &this->cube_vbo_);
    glGenBuffers(1, &this->cube_ebo_);
    glGenBuffers(1, &this->instance_vbo_);
    glBindVertexArray(this->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, this->cube_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(mesh.vertices)), mesh.vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void *>(3 * sizeof(float)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->cube_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(mesh.indices)), mesh.indices, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, this->instance_vbo_);
    const GLsizei stride = static_cast<GLsizei>(kFloatsPerInstance * sizeof(float));
    const struct {
        GLuint location;
        GLint components;
        std::size_t offset;
    } attributes[] = { { 2, 3, 0 }, { 3, 3, 3 }, { 4, 4, 6 }, { 5, 4, 10 } };
    for (const auto &a : attributes) {
        glEnableVertexAttribArray(a.location);
        glVertexAttribPointer(a.location, a.components, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<const void *>(a.offset * sizeof(float)));
        glVertexAttribDivisor(a.location, 1);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void VoxelFxRenderer::shutdown()
{
    if (this->instance_vbo_ != 0) {
        glDeleteBuffers(1, &this->instance_vbo_);
        this->instance_vbo_ = 0;
    }
    if (this->cube_ebo_ != 0) {
        glDeleteBuffers(1, &this->cube_ebo_);
        this->cube_ebo_ = 0;
    }
    if (this->cube_vbo_ != 0) {
        glDeleteBuffers(1, &this->cube_vbo_);
        this->cube_vbo_ = 0;
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

std::size_t VoxelFxRenderer::draw(const Mat4 &view_projection, const Vec3 &eye, const SceneLighting &lighting,
    const Mat4 &light_view_projection, GLuint shadow_texture, int shadow_side, const FxVoxel *voxels, std::size_t count)
{
    if ((this->program_ == 0) || (voxels == nullptr) || (count == 0)) {
        return 0;
    }
    // 不透明を先に、半透明を後に（奥から手前の順）。不透明の中の並びは問わない（深度が決める）。
    std::vector<std::size_t> opaque;
    std::vector<std::pair<float, std::size_t>> translucent;
    opaque.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const FxVoxel &v = voxels[i];
        if (v.alpha <= 0.002f) {
            continue;
        }
        if (v.alpha >= 0.999f) {
            opaque.push_back(i);
        } else {
            const Vec3 d = v.center - eye;
            translucent.emplace_back(dot(d, d), i);
        }
    }
    std::stable_sort(translucent.begin(), translucent.end(),
        [](const auto &a, const auto &b) { return a.first > b.first; });
    const std::size_t total = opaque.size() + translucent.size();
    if (total == 0) {
        return 0;
    }

    this->scratch_.resize(total * kFloatsPerInstance);
    std::size_t at = 0;
    const auto put = [&](const FxVoxel &v) {
        float *f = &this->scratch_[at * kFloatsPerInstance];
        f[0] = v.center.x;
        f[1] = v.center.y;
        f[2] = v.center.z;
        f[3] = v.size.x;
        f[4] = v.size.y;
        f[5] = v.size.z;
        f[6] = v.r;
        f[7] = v.g;
        f[8] = v.b;
        f[9] = v.alpha;
        f[10] = v.emissive;
        f[11] = v.yaw;
        f[12] = v.pitch;
        f[13] = 0.f;
        ++at;
    };
    for (const std::size_t i : opaque) {
        put(voxels[i]);
    }
    for (const auto &entry : translucent) {
        put(voxels[entry.second]);
    }

    glUseProgram(this->program_);
    glUniformMatrix4fv(this->loc_view_projection_, 1, GL_FALSE, view_projection.m);
    upload_lighting(this->light_loc_, lighting, light_view_projection, shadow_texture, shadow_side, 2);

    glBindVertexArray(this->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, this->instance_vbo_);
    const std::size_t bytes = total * kFloatsPerInstance * sizeof(float);
    if (bytes > this->instance_capacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), this->scratch_.data(), GL_STREAM_DRAW);
        this->instance_capacity_ = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), this->scratch_.data());
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW); //!< 世界が左手系で視点行列が鏡映を含む。`VoxelRenderer` と同じ（その注記を見よ）
    if (!opaque.empty()) {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDrawElementsInstanced(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, nullptr, static_cast<GLsizei>(opaque.size()));
    }
    if (!translucent.empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        // 半透明は先頭の `opaque.size()` 個を飛ばして描く（インスタンスの開始位置は属性の基準を動かして指す）。
        const GLsizei stride = static_cast<GLsizei>(kFloatsPerInstance * sizeof(float));
        const std::size_t base_bytes = opaque.size() * kFloatsPerInstance * sizeof(float);
        const struct {
            GLuint location;
            GLint components;
            std::size_t offset;
        } attributes[] = { { 2, 3, 0 }, { 3, 3, 3 }, { 4, 4, 6 }, { 5, 4, 10 } };
        for (const auto &a : attributes) {
            glVertexAttribPointer(a.location, a.components, GL_FLOAT, GL_FALSE, stride,
                reinterpret_cast<const void *>(base_bytes + (a.offset * sizeof(float))));
        }
        glDrawElementsInstanced(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, nullptr, static_cast<GLsizei>(translucent.size()));
        // 次の呼び出しのために先頭へ戻す。
        for (const auto &a : attributes) {
            glVertexAttribPointer(a.location, a.components, GL_FLOAT, GL_FALSE, stride,
                reinterpret_cast<const void *>(a.offset * sizeof(float)));
        }
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    glDisable(GL_CULL_FACE);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    return total;
}

std::size_t draw_fx_slabs(VoxelRenderer &renderer, const std::vector<FxSlab> &slabs, const FxSlabResolver &resolve)
{
    std::size_t drawn = 0;
    for (const FxSlab &slab : slabs) {
        FxSlabSource source;
        if (!resolve || !resolve(slab.item_id, source) || (source.gpu == nullptr) || source.gpu->parts.empty()) {
            continue;
        }
        const Vec3 c = (source.minimum + source.maximum) * 0.5f;
        /*
         * 回す順は「面内の回転 → 画面への傾き → 向き」。画面へ正対した床の板（`entity_view.cpp` /
         * `voxel_renderer.cpp` の `a_instance_pitch` の枝）と同じ法線になる。回す中心は板の中心 `c`。
         */
        const Mat4 rotate = rotation_z(slab.face_yaw) * rotation_x(slab.face_pitch) * rotation_y(slab.spin);
        const Mat4 motion = translation(c) * rotate * translation(c * -1.f);
        int max_index = 0;
        for (const GpuPart &part : source.gpu->parts) {
            max_index = std::max(max_index, part.prefab_part_index);
        }
        const std::vector<Mat4> motions(static_cast<std::size_t>(max_index) + 1u, motion);

        InstanceData inst;
        inst.sx = slab.scale;
        inst.sy = slab.scale;
        inst.sz = slab.scale;
        // 回しても中心は動かない：拡大のあとの中心 `scale × c` が `slab.centre` に来るよう置く。
        inst.x = slab.centre.x - (slab.scale * c.x);
        inst.y = slab.centre.y - (slab.scale * c.y);
        inst.z = slab.centre.z - (slab.scale * c.z);
        renderer.set_cutaway_scale(0.f);
        renderer.draw_instanced(*source.gpu, &inst, 1, motions.data(), motions.size());
        ++drawn;
    }
    return drawn;
}

} // namespace hd2d
