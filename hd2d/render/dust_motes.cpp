/*!
 * @file dust_motes.cpp
 * @brief 空中に浮かぶ埃（`dust_motes.h`）の中身。
 */
#include "render/dust_motes.h"

#include "render/gl_program.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace hd2d::gl;

namespace hd2d {

namespace {

/*
 * 点 1 粒あたりの持ち物は**種だけ**である（位置は毎フレーム頂点シェーダが作る）。
 * `seed.xyz` = 箱の中の置き場所（0〜1）・`seed.w` = 揺れと瞬きの位相。
 */
const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec4 a_seed;

uniform mat4 u_view_projection;
uniform vec3 u_camera;      //!< 箱の中心（マス）
uniform float u_span;       //!< 箱の一辺（マス）
uniform float u_size_px;    //!< ピントが合っているときの粒の大きさ（画素）
uniform float u_speed;      //!< 漂う速さ（マス/秒）
uniform float u_time;       //!< 秒
uniform float u_near_thin;  //!< この距離までは間引く（マス）
uniform vec2 u_focus;       //!< 焦点（キャラの水平位置。マス）
uniform vec2 u_forward;     //!< 奥行きを測る軸（正規化済み）
uniform vec3 u_dof;         //!< x = inner, y = span, z = radius_px（`measure_dof()`）
uniform float u_max_bokeh;  //!< 広がりの上限（画素）

out float v_fade;
out float v_defocus;   //!< 0 = ピントが合っている / 1 = いちばんぼけている

void main()
{
    /*
     * **箱をカメラに追い掛けさせる。**種の位置に漂いを足したものを、カメラを中心とした
     * 一辺 `u_span` の箱へ巻き戻す（出た側の反対から入り直す）。
     * こうすると、どこへ移動しても密度が変わらず、粒が湧いて見える瞬間も無い。
     */
    vec3 drift = vec3(
        sin((u_time * 0.37) + (a_seed.w * 6.283)) * 0.6,
        cos((u_time * 0.29) + (a_seed.w * 4.712)) * 0.6,
        u_time * u_speed);
    vec3 raw = (a_seed.xyz * u_span) + drift;
    vec3 rel = mod(raw - u_camera + (u_span * 0.5), vec3(u_span)) - (u_span * 0.5);
    vec3 world = u_camera + rel;

    /*
     * **手前は疎にする**（2026-08-23 に決めた）。薄くするのではなく**数を減らす**——
     * 近い粒は画面で大きく写るので、薄くしただけでは視界を塞ぐ靄になる。
     * 種から作った擬似乱数と距離のしきい値を比べて、越えなかった粒は画面の外へ捨てる。
     */
    float dist = length(world - u_camera);
    float keep = smoothstep(u_near_thin * 0.25, u_near_thin, dist);
    float lottery = fract(a_seed.w * 7.31 + a_seed.x * 3.17);
    if (lottery > keep) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   //!< クリップの外＝描かれない
        gl_PointSize = 1.0;
        v_fade = 0.0;
        v_defocus = 0.0;
        return;
    }

    vec4 clip = u_view_projection * vec4(world, 1.0);
    gl_Position = clip;

    /*
     * **粒は自分でぼけた大きさへ広がる**（2026-08-23 に決めた「ボケた粒子の中央が
     * 白くくっきりしてるので全体的にぼけるようにして」）。
     *
     * 後処理のぼかしは**近所の画素を混ぜる**ものなので、元が 1 点だと中央に芯が残る
     * （点はどれだけ混ぜても点である）。そこで錯乱円のぶんだけ**先に広げて**おき、
     * 明るさは面積で薄める（総量を保つ）。式は `measure_dof()` と同じものを使う。
     */
    float along = abs(dot(world.xy - u_focus, u_forward));
    float a = clamp((along - u_dof.x) / max(u_dof.y, 1e-3), 0.0, 1.0);
    /*
     * **広がりには上限を置く。**点光源のぼけは実際どこまでも大きくなるが、埃でそれを
     * やると画面いっぱいの丸い膜になる（1 度そうなった）。`u_max_bokeh` 画素で頭打ち。
     */
    float coc_px = min(a * u_dof.z, u_max_bokeh);
    float size = u_size_px + (2.0 * coc_px);
    gl_PointSize = size;
    v_defocus = a;

    /*
     * 箱の縁で消す。巻き戻しの継ぎ目が「粒が突然現れる」形で見えないようにする。
     * ついでに瞬き（種ごとに違う周期）と、手前の間引きの縁のなだらかさを掛ける。
     * **面積で薄める**のはここ——広がった粒がそのぶん暗くなる。
     */
    float edge = 1.0 - smoothstep(0.32, 0.5, max(max(abs(rel.x), abs(rel.y)), abs(rel.z)) / u_span);
    float twinkle = 0.55 + (0.45 * sin((u_time * 1.7) + (a_seed.w * 12.566)));
    /*
     * 広がったぶん薄める。**面積で割らない**——総量を厳密に保つと、ぼけた粒が
     * 27 分の 1 の明るさになって消えてしまう（1 度そうなった）。長さの比で薄めると、
     * ぼけても「そこに在る」ことは読めたまま、芯の明るさだけが落ちる。
     */
    float spread = u_size_px / max(size, 1e-3);
    v_fade = edge * twinkle * keep * spread;
}
)";

const char *const kFragmentSource = R"(#version 460 core
in float v_fade;
in float v_defocus;
uniform vec3 u_color;   //!< 線形の HDR。環境光の色相 × 明るさ
out vec4 o_color;

void main()
{
    vec2 d = gl_PointCoord - vec2(0.5);
    float r = length(d) * 2.0;
    /*
     * **ぼけるほど平らな円盤にする。**ピントが合っているうちは中心が明るい粒でよいが、
     * ぼけた粒の中心が明るいままだと「白い芯のある丸」に見える（2026-08-23 に気づいた）。
     * 実際のぼけは**縁のなだらかな一様の円盤**なので、`v_defocus` で縁の立ち方を変える。
     */
    float softness = mix(0.85, 0.30, clamp(v_defocus, 0.0, 1.0));
    float mask = 1.0 - smoothstep(1.0 - softness, 1.0, r);
    //! ピントが合っている粒だけ、中心をわずかに持ち上げる（点らしさ）。
    mask *= mix(1.0, 1.0 - (0.35 * r * r), 1.0 - clamp(v_defocus, 0.0, 1.0));
    if ((mask * v_fade) <= 0.002) {
        discard;
    }
    //! **加算で混ぜる**ので、色をそのまま返して alpha を掛ける（混ぜ方は呼ぶ側が立てる）。
    o_color = vec4(u_color * (mask * v_fade), 1.0);
}
)";

//! 種を作る（`std::rand` を使わない——同じ絵が毎回出るように自前の擬似乱数で回す）。
float hashed(std::uint32_t &state)
{
    state = (state * 1664525u) + 1013904223u;
    return static_cast<float>((state >> 8) & 0xFFFFFFu) / 16777216.f;
}

} // namespace

bool DustMotes::init(std::string &err)
{
    this->program_ = compile_program(kVertexSource, kFragmentSource, err);
    if (this->program_ == 0) {
        return false;
    }
    this->loc_view_projection_ = glGetUniformLocation(this->program_, "u_view_projection");
    this->loc_camera_ = glGetUniformLocation(this->program_, "u_camera");
    this->loc_span_ = glGetUniformLocation(this->program_, "u_span");
    this->loc_size_px_ = glGetUniformLocation(this->program_, "u_size_px");
    this->loc_speed_ = glGetUniformLocation(this->program_, "u_speed");
    this->loc_time_ = glGetUniformLocation(this->program_, "u_time");
    this->loc_color_ = glGetUniformLocation(this->program_, "u_color");
    this->loc_near_thin_ = glGetUniformLocation(this->program_, "u_near_thin");
    this->loc_focus_ = glGetUniformLocation(this->program_, "u_focus");
    this->loc_forward_ = glGetUniformLocation(this->program_, "u_forward");
    this->loc_dof_ = glGetUniformLocation(this->program_, "u_dof");
    this->loc_max_bokeh_ = glGetUniformLocation(this->program_, "u_max_bokeh");

    /*
     * 種は**起動時に 1 度だけ**作る（上限のぶん）。実際に描く数は `amount` で決めるので、
     * 強さを変えても VBO は作り直さない。
     */
    DustParams defaults;
    this->mote_count_ = defaults.max_motes;
    std::vector<float> seeds(static_cast<std::size_t>(this->mote_count_) * 4u);
    std::uint32_t state = 0x9E3779B9u;
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        seeds[i] = hashed(state);
    }
    glGenVertexArrays(1, &this->vao_);
    glGenBuffers(1, &this->vbo_);
    glBindVertexArray(this->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(seeds.size() * sizeof(float)), seeds.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glBindVertexArray(0);
    return true;
}

void DustMotes::shutdown()
{
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

void DustMotes::draw(const Mat4 &view_projection, const Vec3 &camera_pos, const Vec3 &tint, float time,
    const DustParams &params, const DofView &dof, const Vec3 &focus, int screen_h)
{
    (void)screen_h;
    if ((this->program_ == 0) || (params.amount <= 0.f)) {
        return; //!< **切ってあるときは 1 命令も払わない**
    }
    /*
     * **数は 1 マスあたりの密度から出す**（2026-08-23 に決めた）。
     * 箱の大きさを変えても詰まり方が変わらないのがこの数え方の要点である。
     */
    const float cells = params.span * params.span * params.span;
    const float wanted = params.per_cell * cells * params.amount;
    int count = static_cast<int>(wanted);
    if (count > this->mote_count_) {
        count = this->mote_count_;
    }
    if (count <= 0) {
        return;
    }

    glUseProgram(this->program_);
    glUniformMatrix4fv(this->loc_view_projection_, 1, GL_FALSE, view_projection.m);
    glUniform3f(this->loc_camera_, camera_pos.x, camera_pos.y, camera_pos.z);
    glUniform1f(this->loc_span_, params.span);
    glUniform1f(this->loc_size_px_, params.size_px);
    glUniform1f(this->loc_speed_, params.speed);
    glUniform1f(this->loc_time_, time);
    glUniform1f(this->loc_near_thin_, params.near_thin);
    glUniform2f(this->loc_focus_, focus.x, focus.y);
    glUniform2f(this->loc_forward_, dof.forward_x, dof.forward_y);
    //! **同じ錯乱円の式**を使う（`measure_dof()`）。ここで別の式を書くと粒だけ違うぼけ方をする。
    glUniform3f(this->loc_dof_, dof.inner, dof.span, dof.radius_px);
    glUniform1f(this->loc_max_bokeh_, params.max_bokeh_px);
    /*
     * **色みは環境光から借りるが、明るさは借りない。**そのまま掛けると、暗い所ほど
     * 埃も暗くなって消える——いちばん見せたい洞の中で見えなくなるので、色を最大成分で
     * 割って「色相だけ」を採り、明るさは `brightness` が決める。
     */
    const float peak = (tint.x > tint.y) ? ((tint.x > tint.z) ? tint.x : tint.z)
                                         : ((tint.y > tint.z) ? tint.y : tint.z);
    const float norm = (peak > 0.001f) ? (1.f / peak) : 1.f;
    const float gain = params.brightness * params.amount * norm;
    glUniform3f(this->loc_color_, tint.x * gain, tint.y * gain, tint.z * gain);

    /*
     * **深度は読むが書かない。**壁の向こうの埃は隠れるが、深度バッファは触らない。
     *
     * ここは 1 度書く側へ倒して、戻した（2026-08-23 の同じ日）。
     * 「粒子は深度でぼかしをかけて」を**後処理の被写界深度に任せる**なら深度を書く必要が
     * あるが、後処理のぼかしは近所の画素を混ぜるものなので、**元が 1 点だと中央に芯が残る**
     * （「ボケた粒子の中央が白くくっきりしてる」に気づいた）。そこで粒の側で
     * 錯乱円のぶんだけ広げることにした——**広げた円盤が深度も書くと、背景に大きな
     * 深度の穴が開いて、その中だけ後処理のぼかしが変わる**（丸い切り抜きが見えた）。
     *
     * したがっていまは「粒が自分で広がる・深度は書かない」の 1 本立てである。
     */
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); //!< 加算。線形の HDR へ足す
#if !defined(HENGBAND_GLES)
    //! GLSL ES では `gl_PointSize` は常に有効（立てると `GL_INVALID_ENUM` になる）。
    glEnable(GL_PROGRAM_POINT_SIZE);
#endif

    glBindVertexArray(this->vao_);
    glDrawArrays(GL_POINTS, 0, count);
    glBindVertexArray(0);

#if !defined(HENGBAND_GLES)
    glDisable(GL_PROGRAM_POINT_SIZE);
#endif
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glUseProgram(0);
}

} // namespace hd2d
