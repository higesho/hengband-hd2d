/*!
 * @file cloud_layer.cpp
 * @brief `cloud_layer.h` の実装。
 */
#include "render/cloud_layer.h"

#include "render/frustum.h"
#include "render/gl_program.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hd2d {

using namespace hd2d::gl;

namespace {

//! 模様の一辺（粒）。64 粒 × 2 マス = 128 マス周期で巻き付く。
constexpr int kPatternSide = 64;
//! 模様の周期（マス）。
constexpr float kPatternPeriod = static_cast<float>(kPatternSide) * kCloudTexelCells;
/*!
 * @brief 敷く半径（マス）。カメラからこの距離までタイルを敷く。
 * @details 一人称の遠クリップ（160。`camera.cpp` の一人称の枝）のすぐ内側まで。
 * 層は高さ 40 にあるので、ここを狭くすると**正面を向いたときの空の帯に雲が入らない**
 * ——88 で試したら最低仰角が 24° になり、中立の視界（上端 約 18°）に 1 枚も
 * 写らなかった。150 なら仰角 約 15° から上に雲が届く。
 */
constexpr float kCloudDrawRadius = 150.f;
/*!
 * @brief 覆いの割合。模様のうちこの割合だけが雲になる（しきい値は分位点で自動決定）。
 * @details 0.30 で試すと、層を浅い角度で見たときに遠くの塊が地平線際で重なり合って
 * **一枚の白い壁**になった。0.24 なら塊の間に空が抜ける（Minecraft もおよそ 1/4）。
 */
constexpr float kCloudCoverage = 0.24f;

//! splitmix64（`world/terrain_view.cpp` と同じ手口）。格子の値を種から引く。
std::uint64_t mix64(std::uint64_t z)
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

//! 巻き付く格子のノイズ値（0..1）。種は固定——起動のたびに同じ雲の形（星空と同じ流儀）。
float lattice01(int x, int y, int side, std::uint64_t salt)
{
    const int wx = ((x % side) + side) % side;
    const int wy = ((y % side) + side) % side;
    const std::uint64_t h = mix64((static_cast<std::uint64_t>(static_cast<std::uint32_t>(wx)) << 32)
        ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(wy)) ^ salt);
    return static_cast<float>(h >> 40) / static_cast<float>(1ull << 24);
}

//! 巻き付く双一次補間つきの値ノイズ。`cells` は模様をいくつの格子で割るか（8 = 大きな塊）。
float wrapped_noise(float u, float v, int cells, std::uint64_t salt)
{
    const float fx = u * static_cast<float>(cells);
    const float fy = v * static_cast<float>(cells);
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float a = lattice01(x0, y0, cells, salt);
    const float b = lattice01(x0 + 1, y0, cells, salt);
    const float c = lattice01(x0, y0 + 1, cells, salt);
    const float d = lattice01(x0 + 1, y0 + 1, cells, salt);
    const float top = a + ((b - a) * tx);
    const float bottom = c + ((d - c) * tx);
    return top + ((bottom - top) * ty);
}

/*!
 * @brief 雲の模様（wrap する 64×64 の詰まり具合）を焼く。
 * @details 2 つの周波数の値ノイズを重ね、**分位点でしきい値を決める**——係数を手で
 * 当てるのではなく「上位 30% が雲」と言い切れば、ノイズの式を触っても覆いの割合が動かない。
 * 低い周波数（8 格子）が塊の配置を、高い周波数（16 格子）が縁の欠けを作る。
 */
std::vector<std::uint8_t> bake_cloud_pattern()
{
    std::vector<float> values(static_cast<std::size_t>(kPatternSide) * kPatternSide);
    for (int y = 0; y < kPatternSide; ++y) {
        for (int x = 0; x < kPatternSide; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kPatternSide);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kPatternSide);
            const float coarse = wrapped_noise(u, v, 8, 0xC10DA1ull);
            const float fine = wrapped_noise(u, v, 16, 0xC10DB2ull);
            values[(static_cast<std::size_t>(y) * kPatternSide) + x] = (coarse * 0.68f) + (fine * 0.32f);
        }
    }
    std::vector<float> sorted = values;
    const auto nth = sorted.begin()
        + static_cast<std::ptrdiff_t>(static_cast<float>(sorted.size()) * (1.f - kCloudCoverage));
    std::nth_element(sorted.begin(), nth, sorted.end());
    const float threshold = *nth;

    std::vector<std::uint8_t> filled(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        filled[i] = (values[i] >= threshold) ? 1u : 0u;
    }
    return filled;
}

/*!
 * @brief 面を 1 枚出す（三角形 2 つ・4 floats/頂点 = 位置 + 明るさ）。
 * @details 巻き方は `voxel/greedy_mesher.cpp` の `plane_axes` / `winding_sign` と**同じ式**。
 * この世界は左手系で表面 = 時計回り（`math3d.h`）。表裏を取り違えると背面カリングで
 * 雲が丸ごと消えるので、式を写して実物で確かめる。
 */
void emit_face(std::vector<float> &out, int axis, int sign, const float lo[3], const float hi[3], float shade)
{
    const int p = (axis == 0) ? 1 : 0;
    const int q = (axis == 2) ? 1 : 2;
    const int wind = (axis == 1) ? +1 : -1;
    const float plane = (sign > 0) ? hi[axis] : lo[axis];
    //! (q, p) 平面の 4 隅。(0,0) → (w,0) → (w,h) → (0,h)（あちらの `corners` と同じ並び）。
    const float qs[4] = { lo[q], hi[q], hi[q], lo[q] };
    const float ps[4] = { lo[p], lo[p], hi[p], hi[p] };
    float corner[4][3];
    for (int c = 0; c < 4; ++c) {
        corner[c][axis] = plane;
        corner[c][q] = qs[c];
        corner[c][p] = ps[c];
    }
    const int forward[4] = { 0, 1, 2, 3 };
    const int reversed[4] = { 3, 2, 1, 0 };
    const int *order = (wind == sign) ? forward : reversed;
    const int tri[6] = { 0, 1, 2, 0, 2, 3 };
    for (const int t : tri) {
        const float *v = corner[order[t]];
        out.push_back(v[0]);
        out.push_back(v[1]);
        out.push_back(v[2]);
        out.push_back(shade);
    }
}

/*!
 * @brief 模様から雲の網（1 タイルぶんの頂点）を組む。
 * @details 粒どうしが接する内側の面は出さない（半透明なので、内側の面が残ると
 * 塊の中に暗い筋が見える）。隣の判定は**巻き付けて**行う——タイルの境目で面を出すと、
 * 敷き詰めたときに 128 マスごとに縦の筋が入る。
 *
 * 面の明るさは Minecraft と同じ考え方（上 = 白・横 = やや暗い・下 = 暗い）。
 * 光の計算は要らない——雲は太陽より上等の存在ではないが、板 1 色だと立体に見えない。
 */
std::vector<float> build_cloud_mesh(const std::vector<std::uint8_t> &filled)
{
    const auto at = [&filled](int x, int y) {
        const int wx = ((x % kPatternSide) + kPatternSide) % kPatternSide;
        const int wy = ((y % kPatternSide) + kPatternSide) % kPatternSide;
        return filled[(static_cast<std::size_t>(wy) * kPatternSide) + wx] != 0u;
    };
    std::vector<float> mesh;
    mesh.reserve(48000);
    for (int y = 0; y < kPatternSide; ++y) {
        for (int x = 0; x < kPatternSide; ++x) {
            if (!at(x, y)) {
                continue;
            }
            const float lo[3] = { static_cast<float>(x) * kCloudTexelCells,
                static_cast<float>(y) * kCloudTexelCells, kCloudAltitudeCells };
            const float hi[3] = { lo[0] + kCloudTexelCells, lo[1] + kCloudTexelCells,
                kCloudAltitudeCells + kCloudThicknessCells };
            emit_face(mesh, 2, +1, lo, hi, 1.00f); //!< 上面。見下ろしで見えるのはほぼこれ
            emit_face(mesh, 2, -1, lo, hi, 0.72f); //!< 下面。一人称で見上げたときの主役
            if (!at(x - 1, y)) {
                emit_face(mesh, 0, -1, lo, hi, 0.86f);
            }
            if (!at(x + 1, y)) {
                emit_face(mesh, 0, +1, lo, hi, 0.86f);
            }
            if (!at(x, y - 1)) {
                emit_face(mesh, 1, -1, lo, hi, 0.92f);
            }
            if (!at(x, y + 1)) {
                emit_face(mesh, 1, +1, lo, hi, 0.92f);
            }
        }
    }
    return mesh;
}

const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec4 a_pos_shade;
uniform mat4 u_view_projection;
uniform vec2 u_offset;
//! 蒸気の塊の持ち上げと大きさ（雲と霧は 0 と 1 ＝従来と同じ式になる）。
uniform float u_rise;
uniform float u_scale;
out float v_shade;
void main()
{
    v_shade = a_pos_shade.w;
    vec3 world = vec3((a_pos_shade.xy * u_scale) + u_offset, (a_pos_shade.z * u_scale) + u_rise);
    gl_Position = u_view_projection * vec4(world, 1.0);
}
)";

const char *const kFragmentSource = R"(#version 460 core
in float v_shade;
uniform vec3 u_color;
uniform float u_alpha;
out vec4 frag_color;
void main()
{
    frag_color = vec4(u_color * v_shade, u_alpha);
}
)";

//! 色の線形補間（`sky_dome.cpp` と同じ。翻訳単位の中に閉じている）。
Vec3 lerp(const Vec3 &a, const Vec3 &b, float t)
{
    return Vec3{ a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t) };
}

} // namespace

bool CloudLayer::init(std::string &err)
{
    this->program_ = compile_program(kVertexSource, kFragmentSource, err);
    if (this->program_ == 0) {
        return false;
    }
    const std::vector<float> mesh = build_cloud_mesh(bake_cloud_pattern());
    this->vertex_count_ = static_cast<GLsizei>(mesh.size() / 4);

    glGenVertexArrays(1, &this->vao_);
    glBindVertexArray(this->vao_);
    glGenBuffers(1, &this->vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<std::ptrdiff_t>(mesh.size() * sizeof(float)), mesh.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    this->loc_.view_projection = glGetUniformLocation(this->program_, "u_view_projection");
    this->loc_.offset = glGetUniformLocation(this->program_, "u_offset");
    this->loc_.color = glGetUniformLocation(this->program_, "u_color");
    this->loc_.alpha = glGetUniformLocation(this->program_, "u_alpha");
    this->loc_.rise = glGetUniformLocation(this->program_, "u_rise");
    this->loc_.scale = glGetUniformLocation(this->program_, "u_scale");
    this->build_puff();
    return true;
}

/*!
 * @brief 蒸気の塊 1 つを焼く（原点まわりの**角ばった雲**。デザイン7 その2）。
 * @details 丸い煙にしない——この世界の雲は Minecraft の板であり、蒸気だけ球にすると
 * 別の絵に見える。0.5 マス角の立方体を 5 つ、少しずらして重ねた塊にする
 * （内側の面も出すが、塊が小さいので滲みは目に付かない）。
 */
void CloudLayer::build_puff()
{
    static constexpr float kBox[5][3] = {
        { 0.00f, 0.00f, 0.00f }, { -0.26f, 0.10f, 0.16f }, { 0.22f, -0.14f, 0.12f },
        { 0.06f, 0.24f, -0.10f }, { -0.10f, -0.22f, -0.14f }
    };
    static constexpr float kHalf[5] = { 0.30f, 0.20f, 0.22f, 0.18f, 0.19f };
    std::vector<float> mesh;
    for (int i = 0; i < 5; ++i) {
        const float lo[3] = { kBox[i][0] - kHalf[i], kBox[i][1] - kHalf[i], kBox[i][2] - kHalf[i] };
        const float hi[3] = { kBox[i][0] + kHalf[i], kBox[i][1] + kHalf[i], kBox[i][2] + kHalf[i] };
        emit_face(mesh, 2, +1, lo, hi, 1.00f);
        emit_face(mesh, 2, -1, lo, hi, 0.70f);
        emit_face(mesh, 0, -1, lo, hi, 0.86f);
        emit_face(mesh, 0, +1, lo, hi, 0.86f);
        emit_face(mesh, 1, -1, lo, hi, 0.92f);
        emit_face(mesh, 1, +1, lo, hi, 0.92f);
    }
    this->puff_count_ = static_cast<GLsizei>(mesh.size() / 4);
    glGenVertexArrays(1, &this->puff_vao_);
    glBindVertexArray(this->puff_vao_);
    glGenBuffers(1, &this->puff_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, this->puff_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<std::ptrdiff_t>(mesh.size() * sizeof(float)),
        mesh.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void CloudLayer::shutdown()
{
    if (this->puff_vbo_ != 0) {
        glDeleteBuffers(1, &this->puff_vbo_);
        this->puff_vbo_ = 0;
    }
    if (this->puff_vao_ != 0) {
        glDeleteVertexArrays(1, &this->puff_vao_);
        this->puff_vao_ = 0;
    }
    if (this->patch_vbo_ != 0) {
        glDeleteBuffers(1, &this->patch_vbo_);
        this->patch_vbo_ = 0;
    }
    if (this->patch_vao_ != 0) {
        glDeleteVertexArrays(1, &this->patch_vao_);
        this->patch_vao_ = 0;
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

void CloudLayer::draw(const Mat4 &view_projection, const Vec3 &eye, const SkyState &sky, float time_seconds)
{
    if (!sky.visible || (this->program_ == 0) || (this->vertex_count_ == 0)) {
        return;
    }
    /*
     * **視点が層より上なら描かない**（2026-08-11 に決めた「見下ろし時は雲が見えない
     * 高さに」）。ふつうの見下ろしは視線が空を向かないので視錐台が全タイルを弾くが、
     * 見下ろし角を 85° 近くまで立てて引くとカメラの高さが層（40）を超えることがあり、
     * そのときだけ雲が盤面の上に被さって見えてしまう。高さでは全設定を覆えないので、
     * 「上から見る形になったら描かない」を条件で言い切る。
     */
    if (eye.z >= kCloudAltitudeCells) {
        return;
    }

    /*
     * 雲の色。昼は白、夜はほとんど黒に近い灰（月明かりの雲）。
     * 朝夕は地平線の色（オレンジ）を少しだけ含ませる——空だけ焼けて雲が真っ白のままだと、
     * 別の絵を貼り合わせたように見える。
     */
    const float day = std::clamp((sky.altitude + 0.06f) / 0.30f, 0.f, 1.f);
    Vec3 color = lerp(Vec3{ 0.09f, 0.10f, 0.14f }, Vec3{ 0.98f, 0.99f, 1.00f }, day);
    const float dusk = std::clamp(1.f - (std::fabs(sky.altitude) / 0.30f), 0.f, 1.f);
    color = lerp(color, sky.horizon, dusk * 0.30f);

    glUseProgram(this->program_);
    glUniformMatrix4fv(this->loc_.view_projection, 1, GL_FALSE, view_projection.m);
    glUniform3f(this->loc_.color, color.x, color.y, color.z);
    /*
     * 半透明の度合い。1.0 の板にすると天井に見え、薄すぎると霞になる。
     * Minecraft の雲とおよそ同じ 0.8。
     */
    glUniform1f(this->loc_.alpha, 0.80f);
    //! 蒸気のための uniform（雲は持ち上げず等倍。**書かないと前の描画の値が残る**）。
    glUniform1f(this->loc_.rise, 0.f);
    glUniform1f(this->loc_.scale, 1.f);

    /*
     * 描き方の約束（ヘッダの注記）:
     * - 深度は**読むが書かない**（塔の向こうには隠れ、後から来る幾何は無い）
     * - 半透明なので**背面カリングで 1 粒 1 面**にする（表裏が重なると縁が濃く滲む）
     */
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW); //!< 左手系。`voxel_renderer.cpp` の本描画と同じ
    glCullFace(GL_BACK);
    glBindVertexArray(this->vao_);

    /*
     * 北（−y）へ流す。タイルの原点を drift ぶん北へ送り、周期で巻き戻す。
     * fmod なので世界のどこに居ても位相は同じ——場所で雲の形が変わったりはしない。
     */
    const float drift = std::fmod(time_seconds * kCloudDriftCellsPerSec, kPatternPeriod);
    const Frustum frustum = Frustum::from(view_projection);
    const int tx0 = static_cast<int>(std::floor((eye.x - kCloudDrawRadius) / kPatternPeriod));
    const int tx1 = static_cast<int>(std::floor((eye.x + kCloudDrawRadius) / kPatternPeriod));
    const int ty0 = static_cast<int>(std::floor((eye.y - kCloudDrawRadius + drift) / kPatternPeriod));
    const int ty1 = static_cast<int>(std::floor((eye.y + kCloudDrawRadius + drift) / kPatternPeriod));
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            const float ox = static_cast<float>(tx) * kPatternPeriod;
            const float oy = (static_cast<float>(ty) * kPatternPeriod) - drift;
            const Vec3 lo{ ox, oy, kCloudAltitudeCells };
            const Vec3 hi{ ox + kPatternPeriod, oy + kPatternPeriod, kCloudAltitudeCells + kCloudThicknessCells };
            if (!frustum.intersects(lo, hi)) {
                continue;
            }
            glUniform2f(this->loc_.offset, ox, oy);
            glDrawArrays(GL_TRIANGLES, 0, this->vertex_count_);
        }
    }

    glBindVertexArray(0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glUseProgram(0);
}

/*!
 * @brief **地表の霧**（デザイン8 その2）。空の雲の網を高さだけ変えて敷く。
 *
 * `draw()` との違いは 3 つだけ——**視点が層より高くても描く**（地表の霧は見下ろす
 * ものである）・高さを `u_rise` で下げる・敷く半径と流れの速さを控えめにする。
 * 網も色の式も流れの向き（北へ）も同じものを使う。
 */
void CloudLayer::draw_ground(const Mat4 &view_projection, const Vec3 &eye, const SkyState &sky,
    float time_seconds, float altitude, float alpha, float drift_scale)
{
    if (!sky.visible || (this->program_ == 0) || (this->vertex_count_ == 0) || (altitude <= 0.f)) {
        return;
    }
    //! 色は空の雲と同じ式（昼は白・夜は月明かりの灰・朝夕は地平線の色を少し）。
    const float day = std::clamp((sky.altitude + 0.06f) / 0.30f, 0.f, 1.f);
    Vec3 color = lerp(Vec3{ 0.09f, 0.10f, 0.14f }, Vec3{ 0.98f, 0.99f, 1.00f }, day);
    const float dusk = std::clamp(1.f - (std::fabs(sky.altitude) / 0.30f), 0.f, 1.f);
    color = lerp(color, sky.horizon, dusk * 0.30f);

    glUseProgram(this->program_);
    glUniformMatrix4fv(this->loc_.view_projection, 1, GL_FALSE, view_projection.m);
    glUniform3f(this->loc_.color, color.x, color.y, color.z);
    glUniform1f(this->loc_.alpha, alpha);
    glUniform1f(this->loc_.scale, 1.f);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glCullFace(GL_BACK);
    glBindVertexArray(this->vao_);

    /*
     * 敷く半径は空の雲（150）より狭い。**地表の霧は遠くほど地形に隠れる**ので、
     * 遠方まで敷いても描画呼び出しが増えるだけである。
     */
    constexpr float kGroundDrawRadius = 110.f;
    /*
     * **2 層を違う速さで流す。**網の覆いは 24%（空の雲の値）なので、1 層だと
     * 森の 3/4 が素通しになって「濃霧」に見えない。半周期ずらした 2 層目を
     * **少し高く・少し遅く**流すと、重なった所が濃くなり、層どうしが滑って
     * **霧が動いている**ことが一目で分かる（一様に平行移動するだけでは動きが読めない）。
     */
    struct Stratum {
        float lift; //!< 層の高さの差（マス）
        float speed; //!< 流れの速さの倍率
        float shift; //!< 網の位相のずらし（マス）
    };
    static constexpr Stratum kStrata[2] = { { 0.f, 1.f, 0.f },
        { 0.28f, 0.62f, kPatternPeriod * 0.5f } };
    const Frustum frustum = Frustum::from(view_projection);
    for (const Stratum &st : kStrata) {
        const float base = altitude + st.lift;
        //! 網は z = 雲の高さで焼いてあるので、差分を `u_rise` で下ろす（厚みは 1 マスのまま）。
        glUniform1f(this->loc_.rise, base - kCloudAltitudeCells);
        const float drift = std::fmod(
            (time_seconds * kCloudDriftCellsPerSec * drift_scale * st.speed) + st.shift,
            kPatternPeriod);
        const int tx0 = static_cast<int>(std::floor((eye.x - kGroundDrawRadius) / kPatternPeriod));
        const int tx1 = static_cast<int>(std::floor((eye.x + kGroundDrawRadius) / kPatternPeriod));
        const int ty0
            = static_cast<int>(std::floor((eye.y - kGroundDrawRadius + drift) / kPatternPeriod));
        const int ty1
            = static_cast<int>(std::floor((eye.y + kGroundDrawRadius + drift) / kPatternPeriod));
        for (int ty = ty0; ty <= ty1; ++ty) {
            for (int tx = tx0; tx <= tx1; ++tx) {
                const float ox = static_cast<float>(tx) * kPatternPeriod;
                const float oy = (static_cast<float>(ty) * kPatternPeriod) - drift;
                const Vec3 lo{ ox, oy, base };
                const Vec3 hi{ ox + kPatternPeriod, oy + kPatternPeriod,
                    base + kCloudThicknessCells };
                if (!frustum.intersects(lo, hi)) {
                    continue;
                }
                glUniform2f(this->loc_.offset, ox, oy);
                glDrawArrays(GL_TRIANGLES, 0, this->vertex_count_);
            }
        }
    }

    glBindVertexArray(0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glUseProgram(0);
}

void CloudLayer::draw_patch(const Mat4 &view_projection, const std::vector<std::array<int, 2>> &cells,
    float altitude, const SkyState &sky)
{
    if ((this->program_ == 0) || cells.empty() || (altitude <= 0.f)) {
        return;
    }

    /*
     * マスの集合が変わったときだけメッシュを組み直す。terrain_view は毎フレームマスを
     * 積み直すが、中身はフロアが変わるまで同じ——数と端のマスと高さで見分ければ足りる
     * （視錐台で数が揺れるのはマスを**視界に関わらず全マス**積んでいるので起きない）。
     */
    std::uint64_t stamp = mix64(static_cast<std::uint64_t>(cells.size()))
        ^ mix64((static_cast<std::uint64_t>(static_cast<std::uint32_t>(cells.front()[0])) << 32)
            ^ static_cast<std::uint32_t>(cells.front()[1]))
        ^ mix64((static_cast<std::uint64_t>(static_cast<std::uint32_t>(cells.back()[0])) << 32)
            ^ static_cast<std::uint32_t>(cells.back()[1]))
        ^ mix64(static_cast<std::uint64_t>(altitude * 64.f));
    if ((stamp != this->patch_stamp_) || (this->patch_vao_ == 0)) {
        this->patch_stamp_ = stamp;
        //! 隣の判定は**欠いた後の集合**で行う（穴の縁にも側面を出してモコモコにする）。
        const auto key = [](int x, int y) {
            return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32)
                ^ static_cast<std::uint32_t>(y);
        };
        std::vector<std::uint64_t> alive;
        alive.reserve(cells.size());
        for (const auto &cell : cells) {
            //! 種で 1/4 を欠く。べったり一枚布にすると湖の形の白いシートになる。
            if ((mix64(key(cell[0], cell[1]) ^ 0x4C614Bull) & 3ull) != 0ull) {
                alive.push_back(key(cell[0], cell[1]));
            }
        }
        std::sort(alive.begin(), alive.end());
        const auto has = [&alive, &key](int x, int y) {
            return std::binary_search(alive.begin(), alive.end(), key(x, y));
        };
        std::vector<float> mesh;
        mesh.reserve(alive.size() * 6 * 4 * 4);
        constexpr float kMistThickness = 0.55f;
        for (const auto &cell : cells) {
            const int gx = cell[0];
            const int gy = cell[1];
            if (!has(gx, gy)) {
                continue;
            }
            const float lo[3] = { static_cast<float>(gx), static_cast<float>(gy), altitude };
            const float hi[3] = { lo[0] + 1.f, lo[1] + 1.f, altitude + kMistThickness };
            emit_face(mesh, 2, +1, lo, hi, 1.00f);
            emit_face(mesh, 2, -1, lo, hi, 0.72f);
            if (!has(gx - 1, gy)) {
                emit_face(mesh, 0, -1, lo, hi, 0.86f);
            }
            if (!has(gx + 1, gy)) {
                emit_face(mesh, 0, +1, lo, hi, 0.86f);
            }
            if (!has(gx, gy - 1)) {
                emit_face(mesh, 1, -1, lo, hi, 0.92f);
            }
            if (!has(gx, gy + 1)) {
                emit_face(mesh, 1, +1, lo, hi, 0.92f);
            }
        }
        this->patch_count_ = static_cast<GLsizei>(mesh.size() / 4);
        if (this->patch_vao_ == 0) {
            glGenVertexArrays(1, &this->patch_vao_);
            glGenBuffers(1, &this->patch_vbo_);
        }
        glBindVertexArray(this->patch_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, this->patch_vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<std::ptrdiff_t>(mesh.size() * sizeof(float)),
            mesh.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    if (this->patch_count_ == 0) {
        return;
    }

    //! 色は空の雲と同じ式（昼は白・夜は月明かりの灰・朝夕は地平線の色を少し含む）。
    const float day = std::clamp((sky.altitude + 0.06f) / 0.30f, 0.f, 1.f);
    Vec3 color = lerp(Vec3{ 0.09f, 0.10f, 0.14f }, Vec3{ 0.98f, 0.99f, 1.00f }, day);
    const float dusk = std::clamp(1.f - (std::fabs(sky.altitude) / 0.30f), 0.f, 1.f);
    color = lerp(color, sky.horizon, dusk * 0.30f);

    glUseProgram(this->program_);
    glUniformMatrix4fv(this->loc_.view_projection, 1, GL_FALSE, view_projection.m);
    glUniform2f(this->loc_.offset, 0.f, 0.f);
    glUniform3f(this->loc_.color, color.x, color.y, color.z);
    //! 空の雲（0.8）より薄く。下の水が透けるのが「霧」の読みである。
    glUniform1f(this->loc_.alpha, 0.52f);
    glUniform1f(this->loc_.rise, 0.f);
    glUniform1f(this->loc_.scale, 1.f);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glCullFace(GL_BACK);
    glBindVertexArray(this->patch_vao_);
    glDrawArrays(GL_TRIANGLES, 0, this->patch_count_);
    glBindVertexArray(0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glUseProgram(0);
}

/*!
 * @brief 配管の蒸気（デザイン7 その2・2026-08-19。河童のバザー）。
 *
 * @details 1 つの口につき塊 3 つを**時間差で立ち上げる**。位相 t（0→1）で
 * 高さ・大きさ・薄さが決まる——上がるほど大きく薄くなって消える。揺れは
 * 口ごとに違う位相の横ずれで、**一様な風にはしない**（噴き出しのゆらぎである）。
 *
 * 塊 1 つが 1 描画呼び出しなので、**視錐台に入った口だけ**を近い順に `kSteamPuffBudget`
 * まで描く（町じゅうの継ぎ手をそのまま描くと呼び出しが数百になる）。
 */
void CloudLayer::draw_steam(const Mat4 &view_projection, const std::vector<std::array<float, 3>> &jets,
    const SkyState &sky, float time_seconds)
{
    if ((this->program_ == 0) || (this->puff_count_ == 0) || jets.empty()) {
        return;
    }
    //! 蒸気は白い水気。夜も昼も**空の色には寄せない**（雲と違って自分の熱で見える）。
    const float day = std::clamp((sky.altitude + 0.06f) / 0.30f, 0.f, 1.f);
    const Vec3 color = lerp(Vec3{ 0.52f, 0.55f, 0.60f }, Vec3{ 0.97f, 0.98f, 1.00f }, day);

    glUseProgram(this->program_);
    glUniformMatrix4fv(this->loc_.view_projection, 1, GL_FALSE, view_projection.m);
    glUniform3f(this->loc_.color, color.x, color.y, color.z);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glCullFace(GL_BACK);
    glBindVertexArray(this->puff_vao_);

    constexpr int kPuffs = 2; //!< 1 つの口から立ち上がる塊の数
    constexpr float kPeriod = 2.2f; //!< 1 つの塊が上がりきるまで（秒）
    constexpr float kReach = 2.1f; //!< 上がりきる高さ（マス）
    const Frustum frustum = Frustum::from(view_projection);
    int budget = kSteamPuffBudget;
    for (const auto &jet : jets) {
        if (budget <= 0) {
            break;
        }
        //! 口の上の柱を視錐台で見る（塊の広がりぶん余裕を持たせる）。
        const Vec3 lo{ jet[0] - 0.9f, jet[1] - 0.9f, jet[2] - 0.3f };
        const Vec3 hi{ jet[0] + 0.9f, jet[1] + 0.9f, jet[2] + kReach + 0.9f };
        if (!frustum.intersects(lo, hi)) {
            continue;
        }
        //! 口ごとの位相（座標から。**同じ口なら毎回同じ間合い**で噴く）。
        const std::uint64_t h = mix64((static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                                           static_cast<int>(jet[0] * 16.f)))
                                          << 32)
            ^ static_cast<std::uint32_t>(static_cast<int>(jet[1] * 16.f)));
        const float base_phase = static_cast<float>(h & 0xFFFFull) / 65536.f;
        const float sway_phase = static_cast<float>((h >> 16) & 0xFFFFull) / 65536.f;
        for (int i = 0; i < kPuffs; ++i) {
            if (budget <= 0) {
                break;
            }
            const float t = std::fmod((time_seconds / kPeriod) + base_phase
                    + (static_cast<float>(i) / static_cast<float>(kPuffs)),
                1.f);
            /*
             * 上がるほど**大きく・薄く**。出口では小さく濃い（噴き出しの読み）。
             * **大きくしすぎない**——初版（0.42 → 1.47）は塊どうしが繋がって、
             * 噴き出しではなく町を覆う霧に見えた（合成フレームで実測）。
             */
            const float scale = 0.24f + (t * 0.62f);
            const float alpha = 0.55f * (1.f - (t * t));
            if (alpha <= 0.02f) {
                continue;
            }
            const float ang = (sway_phase + (t * 0.9f)) * 6.2831853f;
            const float sway = t * 0.34f;
            glUniform2f(this->loc_.offset, jet[0] + (std::cos(ang) * sway),
                jet[1] + (std::sin(ang * 0.7f) * sway));
            glUniform1f(this->loc_.rise, jet[2] + (t * kReach));
            glUniform1f(this->loc_.scale, scale);
            glUniform1f(this->loc_.alpha, alpha);
            glDrawArrays(GL_TRIANGLES, 0, this->puff_count_);
            --budget;
        }
    }

    glBindVertexArray(0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glUseProgram(0);
}

} // namespace hd2d
