/*!
 * @file xr_room.cpp
 * @brief `xr_room.h` の実装。
 *
 * ## 部屋と卓は「ボクセルの小屋」である（2026-08-14 に決めた）
 * 「ジオラマのテーブルはボクセルで木目調の表面にして。部屋の構造は大きなボクセル
 * ブロックで丸太風の壁とガラス窓、壁に松明、天井は木目調ブロック」。
 *
 * | 物 | 作り |
 * | --- | --- |
 * | 壁 | 0.5m 角のブロックを積む。柄は**丸太**（横に寝かせた丸太の断面の陰影） |
 * | 窓 | 壁のブロックを**ガラス**の柄に差し替える（夜空の色を薄く通す） |
 * | 松明 | 壁に台座と炎。**炎は HDR で明るく置く**のでブルームが拾う |
 * | 天井・卓 | **木目**（板の継ぎ目と木理） |
 * | 床 | 石畳 |
 *
 * ## 柄はその場で焼く（`sky_dome` と同じ流儀）
 * 画像を配らずに済ませたいので、128×128 の絵を 4 つに割った**アトラス**を
 * `init()` で作る。種は固定なので、起動のたびに同じ木目になる。
 *
 * ## 光は焼き込む
 * この部屋は世界（マス）の光の計算に載っていない。松明の暖色は**組み立てのときに
 * 頂点色へ足す**——動かない光なので、これで十分に見える（そのぶん実行時は 1 パス）。
 */
#include "xr/xr_room.h"

#include "render/gl_program.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hd2d::xr {

using namespace hd2d::gl;

namespace {

//! 壁と天井のブロックの一辺（メートル）。**「大きなボクセルブロック」**（決めたこと）。
constexpr float kBlockM = 0.52f;
//! 天板の厚み（板 1 枚ぶん）。
constexpr float kTableTopThickM = 0.06f;
//! 脚の太さと、天板の縁からの引っ込み。
constexpr float kLegSideM = 0.09f;
constexpr float kLegInsetM = 0.10f;

//! 柄（アトラスの 4 枚）。**並びはここだけで決める。**
enum class Tile : int {
    Wood = 0, //!< 木目（天井・卓）
    Log, //!< 丸太（壁）
    Glass, //!< ガラス窓
    Stone, //!< 石畳（床）
};

constexpr int kAtlasTile = 64; //!< 1 枚の画素
constexpr int kAtlasSide = kAtlasTile * 2; //!< 2×2 で 128

//! splitmix64（`cloud_layer.cpp` と同じ手口。種を固定して毎回同じ柄にする）。
std::uint64_t mix64(std::uint64_t z)
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

float noise01(int x, int y, std::uint64_t salt)
{
    const std::uint64_t h = mix64((static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32)
        ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) ^ salt);
    return static_cast<float>(h >> 40) / static_cast<float>(1ull << 24);
}

void put(std::vector<std::uint8_t> &rgb, int tile_x, int tile_y, int x, int y, float r, float g, float b)
{
    const int px = (tile_x * kAtlasTile) + x;
    const int py = (tile_y * kAtlasTile) + y;
    const std::size_t at = ((static_cast<std::size_t>(py) * kAtlasSide) + px) * 3;
    const auto to8 = [](float v) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f));
    };
    rgb[at] = to8(r);
    rgb[at + 1] = to8(g);
    rgb[at + 2] = to8(b);
}

/*!
 * @brief 柄のアトラスを焼く。
 * @details 4 枚とも**継ぎ目が上下左右で巻く**必要はない（1 ブロック 1 枚で貼るため）。
 * 代わりに**縁を暗くする**——ブロックの境目が見えないと、積んだ形が読めない。
 */
std::vector<std::uint8_t> bake_atlas()
{
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(kAtlasSide) * kAtlasSide * 3, 0u);
    const float side = static_cast<float>(kAtlasTile);

    for (int y = 0; y < kAtlasTile; ++y) {
        for (int x = 0; x < kAtlasTile; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / side;
            const float v = (static_cast<float>(y) + 0.5f) / side;
            //! 縁の陰り（4 枚とも掛ける）。ブロックの継ぎ目に見える。
            const float edge = std::min(std::min(u, 1.f - u), std::min(v, 1.f - v));
            const float seam = std::clamp(edge * 14.f, 0.55f, 1.f);

            /* --- 木目（天井・卓）。板は横並び、木理は板ごとに向きが少し違う --- */
            {
                const int plank = static_cast<int>(v * 4.f); //!< 板 4 枚
                const float in_plank = (v * 4.f) - static_cast<float>(plank);
                const float grain = std::sin((u * 26.f) + (noise01(plank, 3, 0xA11Cull) * 6.f)
                                        + (in_plank * 2.f))
                    * 0.5f;
                const float knot = noise01(static_cast<int>(u * 12.f), plank, 0xB0B0ull);
                float shade = 0.78f + (grain * 0.10f) + ((knot > 0.94f) ? -0.22f : 0.f);
                //! 板の継ぎ目（横線）。
                if ((in_plank < 0.06f) || (in_plank > 0.94f)) {
                    shade *= 0.68f;
                }
                put(rgb, 0, 0, x, y, 0.52f * shade * seam, 0.34f * shade * seam, 0.19f * shade * seam);
            }

            /* --- 丸太（壁）。横に寝かせた丸太が 2 本。中央が明るく、上下の合わせ目が暗い --- */
            {
                const int logn = static_cast<int>(v * 2.f); //!< 丸太 2 本
                const float in_log = (v * 2.f) - static_cast<float>(logn);
                //! 丸みの陰影（真ん中が手前）。
                const float round_shade = std::sin(in_log * 3.14159265f);
                const float grain = std::sin((u * 18.f) + (noise01(logn, 7, 0xC0FFEEull) * 6.f)) * 0.5f;
                float shade = (0.55f + (round_shade * 0.42f)) + (grain * 0.05f);
                //! 丸太の合わせ目（苔むした目地）。
                if ((in_log < 0.05f) || (in_log > 0.95f)) {
                    shade *= 0.45f;
                }
                put(rgb, 1, 0, x, y, 0.44f * shade * seam, 0.29f * shade * seam, 0.17f * shade * seam);
            }

            /* --- ガラス窓。夜の外が透けている体で、青を薄く。桟を十字に入れる --- */
            {
                const bool frame = (std::fabs(u - 0.5f) < 0.045f) || (std::fabs(v - 0.5f) < 0.045f)
                    || (edge < 0.07f);
                //! 斜めの映り込み（1 本だけ）。ガラスに見える最小の仕掛け。
                const float gleam = std::clamp(1.f - (std::fabs((u + v) - 0.85f) * 7.f), 0.f, 1.f);
                if (frame) {
                    put(rgb, 0, 1, x, y, 0.20f * seam, 0.14f * seam, 0.09f * seam); //!< 木の桟
                } else {
                    const float sky = 0.10f + (v * 0.06f); //!< 上ほど明るい夜空
                    put(rgb, 0, 1, x, y, sky * 0.55f + (gleam * 0.22f), sky * 0.75f + (gleam * 0.26f),
                        sky * 1.35f + (gleam * 0.30f));
                }
            }

            /* --- 石畳（床）。粒の散らしと、縦横の目地 --- */
            {
                const int gx = static_cast<int>(u * 2.f);
                const int gy = static_cast<int>(v * 2.f);
                const float speck = noise01(x, y, 0xD00Dull) * 0.10f;
                const float base = 0.30f + (noise01(gx, gy, 0xE1E1ull) * 0.10f) + speck;
                const bool joint = (std::fabs((u * 2.f) - static_cast<float>(gx) - 0.5f) > 0.46f)
                    || (std::fabs((v * 2.f) - static_cast<float>(gy) - 0.5f) > 0.46f);
                const float shade = joint ? 0.55f : 1.f;
                put(rgb, 1, 1, x, y, base * shade * seam, base * shade * seam, (base + 0.01f) * shade * seam);
            }
        }
    }
    return rgb;
}

//! 柄のアトラスの中の左上（0〜1）。
void tile_uv(Tile tile, float &u0, float &v0)
{
    switch (tile) {
    case Tile::Wood:
        u0 = 0.f;
        v0 = 0.f;
        break;
    case Tile::Log:
        u0 = 0.5f;
        v0 = 0.f;
        break;
    case Tile::Glass:
        u0 = 0.f;
        v0 = 0.5f;
        break;
    case Tile::Stone:
    default:
        u0 = 0.5f;
        v0 = 0.5f;
        break;
    }
}

//! 松明 1 本（位置と、そこから足す暖色の強さ）。
struct Torch {
    Vec3 at{ 0.f, 0.f, 0.f };
};

/*!
 * @brief その場所に松明の光をいくら足すか。
 * @details **組み立てのときに頂点色へ焼き込む。**部屋は動かないので実行時に計算する
 * 必要が無く、シェーダも 1 本で済む。距離の 2 乗で落とす（1/(1+d²) の素直な形）。
 */
Vec3 torch_light(const Vec3 &at, const std::vector<Torch> &torches)
{
    Vec3 sum{ 0.f, 0.f, 0.f };
    for (const Torch &t : torches) {
        const Vec3 d = at - t.at;
        const float dist2 = (d.x * d.x) + (d.y * d.y) + (d.z * d.z);
        //! **暖色の溜まりを作るだけ**にする。強く焼くと部屋ぜんぶが橙になり、
        //! 「暗い部屋」（決めたこと）でなくなる（1 度そうなった）。
        const float fall = 0.55f / (1.f + (dist2 * 3.2f));
        sum = sum + Vec3{ 1.00f * fall, 0.58f * fall, 0.22f * fall };
    }
    return sum;
}

//! 頂点 1 つ（位置 3・uv 2・色 3）。
void push_vertex(std::vector<float> &out, const Vec3 &p, float u, float v, const Vec3 &rgb)
{
    out.push_back(p.x);
    out.push_back(p.y);
    out.push_back(p.z);
    out.push_back(u);
    out.push_back(v);
    out.push_back(rgb.x);
    out.push_back(rgb.y);
    out.push_back(rgb.z);
}

/*!
 * @brief 面を 1 枚（四隅は一周の順）。
 * @param tile 柄。
 * @param tint 面の色（陰影を掛けたもの）。
 * @param torches 焼き込む光。
 * @details 裏表は見ない（部屋は内側から見るので、世界と同じ裏面カリングは持ち込まない）。
 */
void push_face(std::vector<float> &out, const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d, Tile tile,
    const Vec3 &tint, const std::vector<Torch> &torches)
{
    float u0 = 0.f;
    float v0 = 0.f;
    tile_uv(tile, u0, v0);
    //! アトラスの縁を踏まないよう、内側へ半画素ぶん寄せる（隣の柄が滲むのを防ぐ）。
    constexpr float kInset = 0.5f / static_cast<float>(kAtlasSide);
    const float lo_u = u0 + kInset;
    const float hi_u = u0 + 0.5f - kInset;
    const float lo_v = v0 + kInset;
    const float hi_v = v0 + 0.5f - kInset;

    const Vec3 corner[4] = { a, b, c, d };
    const float uu[4] = { lo_u, hi_u, hi_u, lo_u };
    const float vv[4] = { hi_v, hi_v, lo_v, lo_v };
    const int tri[6] = { 0, 1, 2, 0, 2, 3 };
    for (const int t : tri) {
        const Vec3 &p = corner[t];
        push_vertex(out, p, uu[t], vv[t], tint + torch_light(p, torches));
    }
}

/*!
 * @brief 直方体を 1 つ（面ごとに明るさを変える）。
 * @param half 半分の寸法（**回す前**）。
 * @param yaw y 軸まわり。
 */
void push_box(std::vector<float> &out, const Vec3 &center, const Vec3 &half, float yaw, Tile top_tile,
    Tile side_tile, const Vec3 &color, const std::vector<Torch> &torches)
{
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    const auto at = [&](float lx, float ly, float lz) {
        return Vec3{ center.x + ((lx * c) + (lz * s)), center.y + ly, center.z + ((-lx * s) + (lz * c)) };
    };
    const Vec3 p000 = at(-half.x, -half.y, -half.z);
    const Vec3 p100 = at(+half.x, -half.y, -half.z);
    const Vec3 p110 = at(+half.x, +half.y, -half.z);
    const Vec3 p010 = at(-half.x, +half.y, -half.z);
    const Vec3 p001 = at(-half.x, -half.y, +half.z);
    const Vec3 p101 = at(+half.x, -half.y, +half.z);
    const Vec3 p111 = at(+half.x, +half.y, +half.z);
    const Vec3 p011 = at(-half.x, +half.y, +half.z);

    push_face(out, p010, p110, p111, p011, top_tile, color * 1.00f, torches); //!< 上
    push_face(out, p000, p100, p101, p001, side_tile, color * 0.45f, torches); //!< 下
    push_face(out, p001, p101, p111, p011, side_tile, color * 0.82f, torches); //!< 手前
    push_face(out, p000, p100, p110, p010, side_tile, color * 0.74f, torches); //!< 奥
    push_face(out, p000, p001, p011, p010, side_tile, color * 0.68f, torches); //!< 左
    push_face(out, p100, p101, p111, p110, side_tile, color * 0.68f, torches); //!< 右
}

const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_color;
uniform mat4 u_view_projection;
out vec2 v_uv;
out vec3 v_color;
void main()
{
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_view_projection * vec4(a_pos, 1.0);
}
)";

const char *const kFragmentSource = R"(#version 460 core
in vec2 v_uv;
in vec3 v_color;
uniform sampler2D u_atlas;
out vec4 frag_color;
void main()
{
    frag_color = vec4(texture(u_atlas, v_uv).rgb * v_color, 1.0);
}
)";

/*!
 * 板（疑似 HMD 専用）。頂点は 4 隅の (u, v) だけを持ち、位置は
 * `中心 + 右 × (u - 0.5) + 上 × (v - 0.5)` で組む——板の寸法と向きが毎フレーム変わるので、
 * 頂点バッファを作り直さずに済ませたい。
 */
const char *const kPanelVertexSource = R"(#version 460 core
layout(location = 0) in vec2 a_uv;
uniform mat4 u_view_projection;
uniform vec3 u_center;
uniform vec3 u_right;
uniform vec3 u_up;
uniform vec2 u_uv0;
uniform vec2 u_uv1;
out vec2 v_uv;
void main()
{
    v_uv = mix(u_uv0, u_uv1, a_uv);
    vec3 world = u_center + (u_right * (a_uv.x - 0.5)) + (u_up * (a_uv.y - 0.5));
    gl_Position = u_view_projection * vec4(world, 1.0);
}
)";

const char *const kPanelFragmentSource = R"(#version 460 core
in vec2 v_uv;
uniform sampler2D u_image;
out vec4 frag_color;
void main()
{
    frag_color = texture(u_image, v_uv);
}
)";

} // namespace

bool RoomRenderer::init(std::string &err)
{
    this->program_ = compile_program(kVertexSource, kFragmentSource, err);
    if (this->program_ == 0) {
        return false;
    }
    this->panel_program_ = compile_program(kPanelVertexSource, kPanelFragmentSource, err);
    if (this->panel_program_ == 0) {
        return false;
    }
    this->loc_.view_projection = glGetUniformLocation(this->program_, "u_view_projection");
    this->loc_.atlas = glGetUniformLocation(this->program_, "u_atlas");
    this->panel_loc_.view_projection = glGetUniformLocation(this->panel_program_, "u_view_projection");
    this->panel_loc_.center = glGetUniformLocation(this->panel_program_, "u_center");
    this->panel_loc_.right = glGetUniformLocation(this->panel_program_, "u_right");
    this->panel_loc_.up = glGetUniformLocation(this->panel_program_, "u_up");
    this->panel_loc_.uv0 = glGetUniformLocation(this->panel_program_, "u_uv0");
    this->panel_loc_.uv1 = glGetUniformLocation(this->panel_program_, "u_uv1");
    this->panel_loc_.image = glGetUniformLocation(this->panel_program_, "u_image");

    //! 柄はその場で焼く（資産を配らない。`sky_dome` と同じ流儀）。
    const std::vector<std::uint8_t> atlas = bake_atlas();
    glGenTextures(1, &this->atlas_);
    glBindTexture(GL_TEXTURE_2D, this->atlas_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGB8), kAtlasSide, kAtlasSide, 0, GL_RGB,
        GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenVertexArrays(1, &this->vao_);
    glBindVertexArray(this->vao_);
    glGenBuffers(1, &this->vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
        reinterpret_cast<const void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
        reinterpret_cast<const void *>(5 * sizeof(float)));

    //! 板の 4 隅（三角形 2 つ）。**中身は (u, v) だけ**（位置はシェーダで組む）。
    const float quad[12] = { 0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 0.f, 1.f };
    glGenVertexArrays(1, &this->panel_vao_);
    glBindVertexArray(this->panel_vao_);
    glGenBuffers(1, &this->panel_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, this->panel_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void RoomRenderer::shutdown()
{
    if (this->vbo_ != 0) {
        glDeleteBuffers(1, &this->vbo_);
        this->vbo_ = 0;
    }
    if (this->vao_ != 0) {
        glDeleteVertexArrays(1, &this->vao_);
        this->vao_ = 0;
    }
    if (this->panel_vbo_ != 0) {
        glDeleteBuffers(1, &this->panel_vbo_);
        this->panel_vbo_ = 0;
    }
    if (this->panel_vao_ != 0) {
        glDeleteVertexArrays(1, &this->panel_vao_);
        this->panel_vao_ = 0;
    }
    if (this->atlas_ != 0) {
        glDeleteTextures(1, &this->atlas_);
        this->atlas_ = 0;
    }
    if (this->program_ != 0) {
        glDeleteProgram(this->program_);
        this->program_ = 0;
    }
    if (this->panel_program_ != 0) {
        glDeleteProgram(this->panel_program_);
        this->panel_program_ = 0;
    }
    this->has_built_ = false;
    this->vertex_count_ = 0;
}

void RoomRenderer::rebuild(const RoomPlacement &room)
{
    std::vector<float> mesh;
    mesh.reserve(60000);

    const float c = std::cos(room.yaw);
    const float s = std::sin(room.yaw);
    //! 局所（x 右・z 手前・部屋の中心が原点）→ ステージ。
    const auto at = [&](float lx, float ly, float lz) {
        return Vec3{ room.center.x + ((lx * c) + (lz * s)), room.center.y + ly, room.center.z + ((-lx * s) + (lz * c)) };
    };

    const float hx = room.room_w * 0.5f;
    const float hz = room.room_d * 0.5f;
    //! ブロックの数（半端は端で詰める）。
    const int nx = std::max(2, static_cast<int>(std::lround(room.room_w / kBlockM)));
    const int nz = std::max(2, static_cast<int>(std::lround(room.room_d / kBlockM)));
    const int ny = std::max(2, static_cast<int>(std::lround(room.room_h / kBlockM)));
    const float bx = room.room_w / static_cast<float>(nx);
    const float bz = room.room_d / static_cast<float>(nz);
    const float by = room.room_h / static_cast<float>(ny);

    /*
     * 松明（「壁に松明」と決めた）。**4 面の真ん中に 1 本ずつ**、目の高さより
     * 少し上に置く。光は下の組み立てで頂点色へ焼き込む。
     */
    const float torch_y = room.center.y + std::min(room.room_h - 0.35f, 1.75f);
    std::vector<Torch> torches;
    torches.push_back(Torch{ at(0.f, torch_y - room.center.y, -hz + 0.16f) });
    torches.push_back(Torch{ at(0.f, torch_y - room.center.y, hz - 0.16f) });
    torches.push_back(Torch{ at(-hx + 0.16f, torch_y - room.center.y, 0.f) });
    torches.push_back(Torch{ at(hx - 0.16f, torch_y - room.center.y, 0.f) });

    /*
     * **暗い部屋**（2026-08-14 に決めた）。松明の溜まりが見えるよう、地の色は暗く。
     * ここを明るくすると、盤（ジオラマ）より部屋のほうが目立ってしまう。
     */
    const Vec3 kWallColor{ 0.30f, 0.29f, 0.28f };
    const Vec3 kGlassColor{ 0.70f, 0.74f, 0.85f };
    const Vec3 kFloorColor{ 0.24f, 0.24f, 0.26f };
    const Vec3 kCeilColor{ 0.20f, 0.19f, 0.18f };
    const Vec3 kTableColor{ 0.42f, 0.39f, 0.36f };

    /* ---- 床（石畳）と天井（木目）。どちらもブロックで敷く ---- */
    for (int iz = 0; iz < nz; ++iz) {
        for (int ix = 0; ix < nx; ++ix) {
            const float x0 = -hx + (static_cast<float>(ix) * bx);
            const float z0 = -hz + (static_cast<float>(iz) * bz);
            const float x1 = x0 + bx;
            const float z1 = z0 + bz;
            //! ブロックごとに少し色を散らす（一様だと「1 枚の板」に見える）。
            const float jitter = 0.90f + (noise01(ix, iz, 0x5A5Aull) * 0.20f);
            push_face(mesh, at(x0, 0.f, z0), at(x1, 0.f, z0), at(x1, 0.f, z1), at(x0, 0.f, z1), Tile::Stone,
                kFloorColor * jitter, torches);
            const float ceil_jitter = 0.90f + (noise01(ix, iz, 0x7B7Bull) * 0.20f);
            push_face(mesh, at(x0, room.room_h, z0), at(x1, room.room_h, z0), at(x1, room.room_h, z1),
                at(x0, room.room_h, z1), Tile::Wood, kCeilColor * ceil_jitter, torches);
        }
    }

    /* ---- 壁（丸太）。**目の高さの段だけ窓**にする ---- */
    const int window_row = std::clamp(static_cast<int>(1.35f / by), 1, ny - 1);
    for (int iy = 0; iy < ny; ++iy) {
        const float y0 = static_cast<float>(iy) * by;
        const float y1 = y0 + by;
        for (int ix = 0; ix < nx; ++ix) {
            const float x0 = -hx + (static_cast<float>(ix) * bx);
            const float x1 = x0 + bx;
            //! 窓は 3 ブロックおき（並べすぎると壁が抜けて「外」が主役になる）。
            const bool window = (iy == window_row) && ((ix % 3) == 1);
            const Tile tile = window ? Tile::Glass : Tile::Log;
            const Vec3 color = window ? kGlassColor : (kWallColor * (0.92f + (noise01(ix, iy, 0x1111ull) * 0.16f)));
            //! 奥の壁（-z 側）と手前の壁（+z 側）。内側から見るので法線は気にしない。
            push_face(mesh, at(x0, y0, -hz), at(x1, y0, -hz), at(x1, y1, -hz), at(x0, y1, -hz), tile,
                color * 0.96f, torches);
            push_face(mesh, at(x0, y0, hz), at(x1, y0, hz), at(x1, y1, hz), at(x0, y1, hz), tile, color * 0.88f,
                torches);
        }
        for (int iz = 0; iz < nz; ++iz) {
            const float z0 = -hz + (static_cast<float>(iz) * bz);
            const float z1 = z0 + bz;
            const bool window = (iy == window_row) && ((iz % 3) == 1);
            const Tile tile = window ? Tile::Glass : Tile::Log;
            const Vec3 color = window ? kGlassColor : (kWallColor * (0.92f + (noise01(iz, iy, 0x2222ull) * 0.16f)));
            push_face(mesh, at(-hx, y0, z0), at(-hx, y0, z1), at(-hx, y1, z1), at(-hx, y1, z0), tile,
                color * 0.84f, torches);
            push_face(mesh, at(hx, y0, z0), at(hx, y0, z1), at(hx, y1, z1), at(hx, y1, z0), tile, color * 0.84f,
                torches);
        }
    }

    /* ---- 松明の実体。台座（木）と炎（HDR で明るく置く。ブルームが拾う） ---- */
    for (const Torch &t : torches) {
        //! 台座。壁から少し出す。
        push_box(mesh, Vec3{ t.at.x, t.at.y - 0.10f, t.at.z }, Vec3{ 0.045f, 0.10f, 0.045f }, room.yaw, Tile::Wood,
            Tile::Wood, Vec3{ 0.55f, 0.45f, 0.35f }, {});
        /*
         * 炎。**色を 1 より大きく置く**——シーンは線形の HDR なので、
         * ここが 1 を超えているとブルームが拾って「光っている」ように見える。
         * 焼き込みの光は掛けない（自分自身を照らしても意味が無い）。
         */
        push_box(mesh, Vec3{ t.at.x, t.at.y + 0.06f, t.at.z }, Vec3{ 0.05f, 0.075f, 0.05f }, room.yaw, Tile::Wood,
            Tile::Wood, Vec3{ 6.0f, 3.1f, 1.1f }, {});
    }

    /* ---- 卓（畳 1 畳）。天板は木目、脚は同じ木の濃いの ---- */
    const float top_y = room.table_center.y;
    const Vec3 top_center{ room.table_center.x, top_y - (kTableTopThickM * 0.5f), room.table_center.z };
    push_box(mesh, top_center, Vec3{ room.table_w * 0.5f, kTableTopThickM * 0.5f, room.table_d * 0.5f }, room.yaw,
        Tile::Wood, Tile::Wood, kTableColor, torches);

    const float leg_top = top_y - kTableTopThickM;
    const float leg_h = std::max(0.05f, leg_top - room.center.y);
    const float leg_x = (room.table_w * 0.5f) - kLegInsetM - (kLegSideM * 0.5f);
    const float leg_z = (room.table_d * 0.5f) - kLegInsetM - (kLegSideM * 0.5f);
    for (int i = 0; i < 4; ++i) {
        const float lx = ((i & 1) != 0) ? leg_x : -leg_x;
        const float lz = ((i & 2) != 0) ? leg_z : -leg_z;
        const Vec3 leg_center{ room.table_center.x + ((lx * c) + (lz * s)), room.center.y + (leg_h * 0.5f),
            room.table_center.z + ((-lx * s) + (lz * c)) };
        push_box(mesh, leg_center, Vec3{ kLegSideM * 0.5f, leg_h * 0.5f, kLegSideM * 0.5f }, room.yaw, Tile::Wood,
            Tile::Wood, kTableColor * 0.62f, torches);
    }

    this->vertex_count_ = static_cast<GLsizei>(mesh.size() / 8);
    glBindVertexArray(this->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.size() * sizeof(float)), mesh.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    this->built_ = room;
    this->has_built_ = true;
}

void RoomRenderer::draw(const Mat4 &stage_view_projection, const RoomPlacement &room)
{
    if (!room.visible || (this->program_ == 0)) {
        return;
    }
    const bool same = this->has_built_ && (this->built_.center.x == room.center.x)
        && (this->built_.center.z == room.center.z) && (this->built_.yaw == room.yaw)
        && (this->built_.table_center.x == room.table_center.x)
        && (this->built_.table_center.y == room.table_center.y)
        && (this->built_.table_center.z == room.table_center.z) && (this->built_.room_w == room.room_w)
        && (this->built_.room_d == room.room_d) && (this->built_.room_h == room.room_h)
        && (this->built_.table_w == room.table_w) && (this->built_.table_d == room.table_d);
    if (!same) {
        this->rebuild(room);
    }
    if (this->vertex_count_ == 0) {
        return;
    }
    /*
     * **裏表は見ない。**部屋は内側から見るので、世界と同じ裏面カリング（左手系・
     * 表 = 時計回り）を持ち込むと壁が丸ごと消える。深度は読み書きとも有効のまま
     * ——卓の縁が盤に正しく前後する。
     */
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glUseProgram(this->program_);
    glUniformMatrix4fv(this->loc_.view_projection, 1, GL_FALSE, stage_view_projection.m);
    glUniform1i(this->loc_.atlas, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, this->atlas_);
    glBindVertexArray(this->vao_);
    glDrawArrays(GL_TRIANGLES, 0, this->vertex_count_);
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_CULL_FACE);
}

void RoomRenderer::draw_panel(const Mat4 &stage_view_projection, GLuint texture, const PanelPose &pose,
    float u0, float v0, float u1, float v1)
{
    if ((this->panel_program_ == 0) || (texture == 0)) {
        return;
    }
    const Vec3 right = quat_rotate(pose.orientation, Vec3{ 1.f, 0.f, 0.f }) * pose.width_m;
    const Vec3 up = quat_rotate(pose.orientation, Vec3{ 0.f, 1.f, 0.f }) * pose.height_m;

    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /*
     * **深度は読みも書きもしない。**実物のクワッドレイヤはコンポジタが
     * 深度に関係なくシーンの上へ重ねる（設計書 §7）ので、疑似 HMD でも同じにする。
     * 深度を見ると、この時点の枠バッファに入っている値（合成の出し先なので
     * シーンの深度ではない）で板が消えたり出たりして、実機と違う絵になる。
     */
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(this->panel_program_);
    glUniformMatrix4fv(this->panel_loc_.view_projection, 1, GL_FALSE, stage_view_projection.m);
    glUniform3f(this->panel_loc_.center, pose.center.x, pose.center.y, pose.center.z);
    glUniform3f(this->panel_loc_.right, right.x, right.y, right.z);
    glUniform3f(this->panel_loc_.up, up.x, up.y, up.z);
    glUniform2f(this->panel_loc_.uv0, u0, v0);
    glUniform2f(this->panel_loc_.uv1, u1, v1);
    glUniform1i(this->panel_loc_.image, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(this->panel_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

bool UiSurface::ensure(int w, int h)
{
    if ((w <= 0) || (h <= 0)) {
        return false;
    }
    if ((this->texture_ != 0) && (this->width_ == w) && (this->height_ == h)) {
        return true;
    }
    this->shutdown();
    glGenTextures(1, &this->texture_);
    glBindTexture(GL_TEXTURE_2D, this->texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &this->fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, this->fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->texture_, 0);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        this->shutdown();
        return false;
    }
    this->width_ = w;
    this->height_ = h;
    return true;
}

void UiSurface::shutdown()
{
    if (this->fbo_ != 0) {
        glDeleteFramebuffers(1, &this->fbo_);
        this->fbo_ = 0;
    }
    if (this->texture_ != 0) {
        glDeleteTextures(1, &this->texture_);
        this->texture_ = 0;
    }
    this->width_ = 0;
    this->height_ = 0;
}

GLuint UiSurface::bind()
{
    if (this->fbo_ == 0) {
        return 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, this->fbo_);
    glViewport(0, 0, this->width_, this->height_);
    //! **透明で消す。**描かなかった所は板が抜ける（実物の `bind_ui` と同じ約束）。
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    return static_cast<GLuint>(this->fbo_);
}

} // namespace hd2d::xr
