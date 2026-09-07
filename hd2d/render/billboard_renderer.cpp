/*!
 * @file billboard_renderer.cpp
 * @brief `billboard_renderer.h` の実装。
 */
#include "render/billboard_renderer.h"

#include "render/gl_program.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>

namespace hd2d {

using namespace hd2d::gl;

namespace {

/*
 * 板の 4 隅は「右方向 u ∈ [-0.5, 0.5]」「上方向 v ∈ [0, 1]」で持つ。
 * v の下端が 0 なのは、**インスタンスの位置を足元にする**ため（マスの上に立たせるとき、
 * 中心を渡すより足元のほうが間違えにくい）。
 */
const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec2 a_corner;           // (u, v) = (-0.5..0.5, 0..1)
layout(location = 1) in vec3 a_foot;             // 足元の世界座標（マス単位）
layout(location = 2) in vec2 a_size;             // 幅・高さ（マス単位）
layout(location = 3) in vec4 a_uv;               // アトラスの矩形（テクセル）
layout(location = 4) in vec3 a_tint;
uniform mat4 u_view_projection;
uniform vec3 u_right;     // カメラの水平右方向（**水平面内に寝かせてある**）
uniform vec3 u_eye;       // カメラ位置（深度の下駄をカメラ方向へ履かせるのに使う）
uniform float u_depth_bias;
uniform float u_atlas_side;
/*
 * **かきわりを向ける先**（2026-08-14 に決めた「HMD に正対ではなく、キャラの
 * 座標に対して正対させる」）。`u_face_point.z` が 1 以上ならこの点（xy）へ向け、
 * そうでなければ `u_right`（＝カメラの向き）を使う。
 *
 * VR で頭に正対させると、**頭を動かすたびに世界じゅうの板が回る**。ジオラマは
 * 盤の上の出来事なので、キャラの居る所を向いていれば絵として成り立つ。
 */
uniform vec3 u_face_point;
out vec2 v_uv;
out vec3 v_tint;
out vec3 v_world;
out vec3 v_facing;   // 板の法線（水平・カメラを向く）
void main()
{
    // 軸拘束: 横は「向ける先への垂直」、縦は**常に世界の上**。板は寝ない。
    vec3 up = vec3(0.0, 0.0, 1.0);
    vec3 right = u_right;
    vec3 to_face = vec3(u_face_point.xy - a_foot.xy, 0.0);
    if ((u_face_point.z > 0.5) && (dot(to_face, to_face) > 1e-6)) {
        //! 向ける先への向きに**垂直**な水平方向が板の横になる。
        vec3 dir = normalize(to_face);
        right = vec3(dir.y, -dir.x, 0.0);
    }
    vec3 world = a_foot + (right * (a_corner.x * a_size.x)) + (up * (a_corner.y * a_size.y));
    /*
     * **深度の下駄。**足元を地面に置いた垂直な板は、見下ろすカメラでは足元側が
     * 地面に飲まれる（P4 で実際に踏んだ。深度を切ると全身が出た）。板を**カメラの方へ**
     * わずかに寄せて、立っているマスの地面より確実に手前に来るようにする。
     * 寄せるのは深度のためだけなので、見た目の位置はほとんど動かない。
     */
    world += normalize(u_eye - world) * u_depth_bias;
    gl_Position = u_view_projection * vec4(world, 1.0);
    v_uv = mix(a_uv.xy, a_uv.zw, vec2(a_corner.x + 0.5, 1.0 - a_corner.y)) / u_atlas_side;
    v_tint = a_tint;
    /*
     * 光を当てるのは**下駄を履かせる前**の位置で。下駄はカメラの方へ寄せるためのもので、
     * ここで寄せた位置を使うと、カメラを動かしただけで影の落ち方が変わる。
     */
    v_world = a_foot + (right * (a_corner.x * a_size.x)) + (up * (a_corner.y * a_size.y));
    /*
     * 板の法線。**足元から視点へ向かう水平方向**である。`u_right` から外積で作ってもよいが、
     * それだと符号（どちら側が表か）をこちらで決めることになる。視点との差から作れば、
     * カメラを回しても（P6 以降）向きが裏返らない。
     */
    vec3 to_eye = u_eye - a_foot;
    if ((u_face_point.z > 0.5) && (dot(to_face, to_face) > 1e-6)) {
        //! 向ける先へ向けたときは、法線もそちらへ（光の当たり方と表裏を合わせる）。
        to_eye = vec3(to_face.xy, 0.0);
    }
    v_facing = normalize(vec3(to_eye.xy, 0.0001));
}
)";

/*
 * 影のパス。**頂点の式は上と同じ。**違うのは
 *   - `u_right` にカメラではなく**光**に垂直な向きが来る（板を光へ向ける）
 *   - `u_depth_bias` が 0（影の地図に前後関係の細工は要らない）
 * だけなので、専用の頂点シェーダは持たず上のものを使い回す。
 */
const char *const kDepthFragmentSource = R"(#version 460 core
in vec2 v_uv;
in vec3 v_tint;
in vec3 v_world;
in vec3 v_facing;
uniform sampler2D u_atlas;
void main()
{
    // 影の形はスプライトの輪郭そのもの。**本描画と同じしきい値**でなければ影がずれる。
    if (texture(u_atlas, v_uv).a < 0.5) {
        discard;
    }
}
)";

const char *const kFragmentBody = R"(
in vec2 v_uv;
in vec3 v_tint;
in vec3 v_world;
in vec3 v_facing;
uniform sampler2D u_atlas;
/*!
 * **場の光を掛けないか**（1 で掛けない＝フルブライト）。アスキー実体のために足した
 * 字の色はコアの 16 色という**情報**なので、
 * 暗所で紫が黒ずむと毒と闇が見分けられなくなる。地形の明暗はボクセル側が描いている。
 * 既定 0 で従来のかきわりと 1 ビットも変わらない。
 */
uniform int u_fullbright;
out vec4 o_color;
void main()
{
    vec4 texel = texture(u_atlas, v_uv);
    // **α で抜く。**混ぜずに深度を正しく書くので、地形との前後関係が素直に決まる。
    if (texel.a < 0.5) {
        discard;
    }

    /*
     * **スプライトは既に陰影付きで描かれている。**そこへ `dot(n, l)` をそのまま掛けると
     * 二重に陰が付いて泥のようになるので、主光も影も点光源も**下限を高めに**取る。
     * ここがボクセルとの唯一の違いで、式そのものは同じものを使っている。
     */
    vec3 n = normalize(v_facing);
    float ndl = max(dot(n, u_light_dir), 0.0);
    float shadow = hd2d_key_shadow(v_world, ndl);
    vec3 direct = u_key_color * mix(0.55, 1.0, ndl) * mix(0.45, 1.0, shadow);
    vec3 indirect = hd2d_ambient(n) + hd2d_point_lights(v_world, n, 0.55);

    // **線形の HDR をそのまま書く。**トーンマップは合成の 1 か所（P7。`lighting.cpp` の注記）。
    vec3 lit = (u_fullbright != 0) ? vec3(1.0) : (direct + indirect);

    /*
     * **画調**。標準では素通りする。
     *
     * ボクセルと違って**線を引かない**（`line = 1.0`）。板は絵そのものが輪郭を持って
     * いるので、その上へさらに格子や稜線を重ねると字も絵も潰れる。板は
     * 「線でできた物」の側として、丸ごとネオンにする。
     */
    if (u_look_enabled != 0) {
        o_color = vec4(hd2d_look_apply(texel.rgb * v_tint, lit, vec3(0.0), 1.0), 1.0);
        return;
    }

    o_color = vec4(texel.rgb * v_tint * lit, 1.0);
}
)";

//! 板 1 枚ぶんの隅（三角形 2 枚ぶんの並び）。
const float kCorners[6][2] = {
    { -0.5f, 0.f }, { 0.5f, 0.f }, { 0.5f, 1.f },
    { -0.5f, 0.f }, { 0.5f, 1.f }, { -0.5f, 1.f }
};

} // namespace

bool BillboardRenderer::init(std::string &err)
{
    // 光の式はボクセルと同じ 1 本（`render/lighting.h`）。
    const std::string fragment
        = std::string("#version 460 core\n") + kLightingGlsl + kSceneLookGlsl + kFragmentBody;
    this->program_ = compile_program(kVertexSource, fragment.c_str(), err);
    if (this->program_ == 0) {
        return false;
    }
    this->loc_view_projection_ = glGetUniformLocation(this->program_, "u_view_projection");
    this->loc_right_ = glGetUniformLocation(this->program_, "u_right");
    this->loc_face_point_ = glGetUniformLocation(this->program_, "u_face_point");
    this->loc_eye_ = glGetUniformLocation(this->program_, "u_eye");
    this->loc_depth_bias_ = glGetUniformLocation(this->program_, "u_depth_bias");
    this->loc_atlas_ = glGetUniformLocation(this->program_, "u_atlas");
    this->loc_atlas_side_ = glGetUniformLocation(this->program_, "u_atlas_side");
    this->light_loc_.locate(this->program_);
    this->look_loc_.locate(this->program_);
    this->loc_fullbright_ = glGetUniformLocation(this->program_, "u_fullbright");

    // 影のパス。**頂点シェーダは同じもの**（板の向きと下駄だけ呼び出し側で変える）。
    this->depth_program_ = compile_program(kVertexSource, kDepthFragmentSource, err);
    if (this->depth_program_ == 0) {
        return false;
    }
    this->depth_loc_view_projection_ = glGetUniformLocation(this->depth_program_, "u_view_projection");
    this->depth_loc_right_ = glGetUniformLocation(this->depth_program_, "u_right");
    //! 影のパスは**光へ向ける**（向ける先は使わない）。0 を入れておく口。
    this->depth_loc_face_point_ = glGetUniformLocation(this->depth_program_, "u_face_point");
    this->depth_loc_eye_ = glGetUniformLocation(this->depth_program_, "u_eye");
    this->depth_loc_depth_bias_ = glGetUniformLocation(this->depth_program_, "u_depth_bias");
    this->depth_loc_atlas_ = glGetUniformLocation(this->depth_program_, "u_atlas");
    this->depth_loc_atlas_side_ = glGetUniformLocation(this->depth_program_, "u_atlas_side");

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
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(BillboardInstance),
        reinterpret_cast<const void *>(offsetof(BillboardInstance, x)));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(BillboardInstance),
        reinterpret_cast<const void *>(offsetof(BillboardInstance, width)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(BillboardInstance),
        reinterpret_cast<const void *>(offsetof(BillboardInstance, u0)));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(BillboardInstance),
        reinterpret_cast<const void *>(offsetof(BillboardInstance, r)));
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "ビルボードの初期化で GL エラー: " + gl_err;
        return false;
    }
    return true;
}

void BillboardRenderer::shutdown()
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
    if (this->depth_program_ != 0) {
        glDeleteProgram(this->depth_program_);
        this->depth_program_ = 0;
    }
    if (this->program_ != 0) {
        glDeleteProgram(this->program_);
        this->program_ = 0;
    }
}

//! インスタンスを GPU へ。**`vao_` を結んだあとに呼ぶこと。**
void BillboardRenderer::upload_instances(const BillboardInstance *instances, std::size_t count)
{
    glBindBuffer(GL_ARRAY_BUFFER, this->instance_vbo_);
    const std::size_t bytes = count * sizeof(BillboardInstance);
    if (bytes > this->instance_capacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), instances, GL_STREAM_DRAW);
        this->instance_capacity_ = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), instances);
    }
}

void BillboardRenderer::draw_depth(const Mat4 &light_view_projection, const Vec3 &light_dir,
    GLuint atlas, int atlas_side, const BillboardInstance *instances, std::size_t count)
{
    if ((this->depth_program_ == 0) || (count == 0) || (instances == nullptr) || (atlas == 0)) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE); //!< 板は表裏どちらからでも影を作る
    glDisable(GL_BLEND);

    glUseProgram(this->depth_program_);
    glUniformMatrix4fv(this->depth_loc_view_projection_, 1, GL_FALSE, light_view_projection.m);
    /*
     * **板を光へ向ける**（ヘッダの注記）。光の水平方向に垂直な向きを板の横方向にすると、
     * 影の形はカメラに依らず決まる。カメラ向きのまま影を書くと、カメラを回しただけで
     * 人の影が伸びたり縮んだりする。
     */
    const Vec3 dir = normalize(light_dir);
    Vec3 right{ -dir.y, dir.x, 0.f };
    if ((std::abs(right.x) + std::abs(right.y)) < 1e-4f) {
        right = Vec3{ 1.f, 0.f, 0.f }; // 光が真上（水平成分が無い）
    }
    right = normalize(right);
    glUniform3f(this->depth_loc_right_, right.x, right.y, 0.f);
    //! 影は**光へ向けた板**の形。向ける先は使わない。
    glUniform3f(this->depth_loc_face_point_, 0.f, 0.f, 0.f);
    /*
     * 深度の下駄は影の地図には要らないので 0。**視点は遠くへ置く。**
     * 頂点シェーダが `normalize(u_eye - world)` を取るので、視点が板と重なると
     * `normalize(vec3(0))` が NaN になり、**0 を掛けても NaN のまま**位置が壊れる。
     */
    glUniform3f(this->depth_loc_eye_, 0.f, 0.f, 10000.f);
    glUniform1f(this->depth_loc_depth_bias_, 0.f);
    glUniform1i(this->depth_loc_atlas_, 0);
    glUniform1f(this->depth_loc_atlas_side_, static_cast<float>(atlas_side));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);

    glBindVertexArray(this->vao_);
    this->upload_instances(instances, count);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(count));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void BillboardRenderer::set_look(const LookParams &look)
{
    this->look_ = look;
}

void BillboardRenderer::draw(const Mat4 &view_projection, const SceneLighting &lighting, float camera_azimuth,
    const Vec3 &eye, const Mat4 &light_view_projection, GLuint shadow_texture, int shadow_side,
    GLuint atlas, int atlas_side, const BillboardInstance *instances, std::size_t count, const Vec3 *face_point,
    bool fullbright)
{
    if ((this->program_ == 0) || (count == 0) || (instances == nullptr) || (atlas == 0)) {
        return;
    }

    if (std::getenv("HD2D_BILLBOARD_NO_DEPTH") != nullptr) {
        glDisable(GL_DEPTH_TEST); // 切り分け用: 深度で消えているのか形が違うのかを見る
    } else {
        glEnable(GL_DEPTH_TEST);
    }
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE); //!< 板は裏からも見える（回り込んだときに消えない）
    glDisable(GL_BLEND); //!< α は `discard` で抜く

    glUseProgram(this->program_);
    glUniformMatrix4fv(this->loc_view_projection_, 1, GL_FALSE, view_projection.m);
    // カメラの方位から**水平面内の右方向**を作る（縦は常に世界の上なので渡さない）。
    glUniform3f(this->loc_right_, std::cos(camera_azimuth), std::sin(camera_azimuth), 0.f);
    /*
     * 向ける先（2026-08-14 に決めた）。渡されたときは**そこへ正対**する
     * ——VR で頭に正対させると、首を振るたびに世界じゅうの板が回ってしまう。
     */
    if (face_point != nullptr) {
        glUniform3f(this->loc_face_point_, face_point->x, face_point->y, 1.f);
    } else {
        glUniform3f(this->loc_face_point_, 0.f, 0.f, 0.f);
    }
    glUniform3f(this->loc_eye_, eye.x, eye.y, eye.z);
    // 0.5 マス。1 マス 32 ボクセルの世界では、見た目の位置はほとんど動かない大きさである。
    glUniform1f(this->loc_depth_bias_, 0.5f);
    glUniform1i(this->loc_atlas_, 0);
    glUniform1f(this->loc_atlas_side_, static_cast<float>(atlas_side));
    //! フルブライト（アスキー実体。設計書 §5.3）。既定の偽で従来と同じ枝を通る。
    glUniform1i(this->loc_fullbright_, fullbright ? 1 : 0);
    //! 画調（軸 E）。**ボクセルと同じものが入っていること**（`set_look` の注記）。
    upload_look(this->look_loc_, this->look_);
    // 影は 1 番（0 はスプライトのアトラス）。ボクセル側の 2 番と違ってよい。
    upload_lighting(this->light_loc_, lighting, light_view_projection, shadow_texture, shadow_side, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);

    glBindVertexArray(this->vao_);
    this->upload_instances(instances, count);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(count));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

} // namespace hd2d
