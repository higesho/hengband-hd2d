/*!
 * @file ui_paint.cpp
 * @brief `ui_paint.h` の実装。
 */
#include "ui/ui_paint.h"

#include "render/gl_program.h"
#include "render/term_colors.h"

#include <algorithm>

namespace hd2d {

using namespace hd2d::gl;

namespace {

const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec2 a_pos;    // 画素座標（左上原点）
layout(location = 1) in vec4 a_color;
uniform vec2 u_screen;
out vec4 v_color;
void main()
{
    vec2 ndc = vec2(a_pos.x / u_screen.x * 2.0 - 1.0, 1.0 - a_pos.y / u_screen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = a_color;
}
)";

const char *const kFragmentSource = R"(#version 460 core
in vec4 v_color;
out vec4 o_color;
void main()
{
    o_color = v_color;
}
)";

} // namespace

PaintColor paint_from_term_color(std::uint8_t color_index, float alpha)
{
    const RgbColor rgb = term_color_to_rgb(color_index);
    return PaintColor{ static_cast<float>(rgb.r) / 255.f, static_cast<float>(rgb.g) / 255.f,
        static_cast<float>(rgb.b) / 255.f, alpha };
}

namespace {
//! VR の間だけ真（`set_panel_transparent`）。**下敷きの有無を決めるのはここ 1 か所。**
bool g_panel_transparent = false;
} // namespace

void set_panel_transparent(bool on)
{
    g_panel_transparent = on;
}

bool panel_transparent()
{
    return g_panel_transparent;
}

PanelStyle overlay_panel_style()
{
    PanelStyle style;
    style.fill = PaintColor{ 0.04f, 0.05f, 0.08f, g_panel_transparent ? 0.f : 0.82f };
    style.border = PaintColor{ 0.42f, 0.48f, 0.58f, 0.80f };
    return style;
}

PanelStyle solid_panel_style()
{
    PanelStyle style;
    style.fill = PaintColor{ 0.06f, 0.07f, 0.10f, g_panel_transparent ? 0.f : 1.f };
    style.border = PaintColor{ 0.30f, 0.34f, 0.42f, 1.f };
    return style;
}

bool UiPaint::init(std::string &err)
{
    this->program_ = compile_program(kVertexSource, kFragmentSource, err);
    if (this->program_ == 0) {
        return false;
    }
    this->loc_screen_ = glGetUniformLocation(this->program_, "u_screen");

    glGenVertexArrays(1, &this->vao_);
    glGenBuffers(1, &this->vbo_);
    glBindVertexArray(this->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void *>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void *>(offsetof(Vertex, r)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "UI の四角を描く用意で GL エラー: " + gl_err;
        return false;
    }
    return true;
}

void UiPaint::shutdown()
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

void UiPaint::begin(int screen_w, int screen_h)
{
    this->screen_w_ = (screen_w > 0) ? screen_w : 1;
    this->screen_h_ = (screen_h > 0) ? screen_h : 1;
    this->vertices_.clear();
}

void UiPaint::push_rect(float x0, float y0, float x1, float y1, const PaintColor &color)
{
    if ((x1 <= x0) || (y1 <= y0) || (color.a <= 0.f)) {
        return;
    }
    const Vertex a{ x0, y0, color.r, color.g, color.b, color.a };
    const Vertex b{ x1, y0, color.r, color.g, color.b, color.a };
    const Vertex c{ x1, y1, color.r, color.g, color.b, color.a };
    const Vertex d{ x0, y1, color.r, color.g, color.b, color.a };
    this->vertices_.push_back(a);
    this->vertices_.push_back(b);
    this->vertices_.push_back(c);
    this->vertices_.push_back(a);
    this->vertices_.push_back(c);
    this->vertices_.push_back(d);
}

void UiPaint::triangle(float x0, float y0, float x1, float y1, float x2, float y2, const PaintColor &color)
{
    if (color.a <= 0.f) {
        return;
    }
    this->vertices_.push_back(Vertex{ x0, y0, color.r, color.g, color.b, color.a });
    this->vertices_.push_back(Vertex{ x1, y1, color.r, color.g, color.b, color.a });
    this->vertices_.push_back(Vertex{ x2, y2, color.r, color.g, color.b, color.a });
}

void UiPaint::rect(const RectPx &r, const PaintColor &color)
{
    this->push_rect(static_cast<float>(r.x), static_cast<float>(r.y),
        static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h), color);
}

void UiPaint::frame(const RectPx &r, const PaintColor &color, int thickness)
{
    if (r.empty() || (thickness <= 0)) {
        return;
    }
    const auto t = static_cast<float>(std::min(thickness, std::min(r.w, r.h)));
    const auto x0 = static_cast<float>(r.x);
    const auto y0 = static_cast<float>(r.y);
    const auto x1 = static_cast<float>(r.x + r.w);
    const auto y1 = static_cast<float>(r.y + r.h);
    this->push_rect(x0, y0, x1, y0 + t, color); // 上
    this->push_rect(x0, y1 - t, x1, y1, color); // 下
    this->push_rect(x0, y0 + t, x0 + t, y1 - t, color); // 左
    this->push_rect(x1 - t, y0 + t, x1, y1 - t, color); // 右
}

void UiPaint::panel(const RectPx &r, const PanelStyle &style)
{
    if (r.empty()) {
        return;
    }
    this->rect(r, style.fill);
    this->frame(r, style.border, style.border_px);
}

void UiPaint::gauge(const RectPx &r, float ratio, const PaintColor &fill, const PaintColor &back)
{
    if (r.empty()) {
        return;
    }
    this->rect(r, back);
    const float clamped = std::clamp(ratio, 0.f, 1.f);
    RectPx filled = r;
    filled.w = static_cast<int>(static_cast<float>(r.w) * clamped);
    this->rect(filled, fill);
}

void UiPaint::flush()
{
    if (this->vertices_.empty() || (this->program_ == 0)) {
        return;
    }

    /*
     * **自分で viewport を張る。**呼び出し側の状態に頼ってはいけない。
     * 直前に走るのはポスト処理で、あれは 3D の矩形へ出すために `glViewport` を
     * その矩形に設定して戻さない。頼っていた頃は `Split`（3D が窓の一部）のとき
     * **UI が丸ごと 3D の矩形の中へ押し込まれ、画面の右と下に空白が残った**。
     * `begin()` で受け取った実寸と同じものを張るので、頂点シェーダの割り算とも必ず一致する。
     */
    if (!ui_break_enabled("viewport")) {
        glViewport(0, 0, this->screen_w_, this->screen_h_);
    } //!< `HD2D_BREAK_UI=viewport`。**検査が FAIL になるのが正しい。**
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(this->program_);
    glUniform2f(this->loc_screen_, static_cast<float>(this->screen_w_), static_cast<float>(this->screen_h_));

    glBindVertexArray(this->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    const std::size_t bytes = this->vertices_.size() * sizeof(Vertex);
    if (bytes > this->vbo_capacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), this->vertices_.data(), GL_STREAM_DRAW);
        this->vbo_capacity_ = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), this->vertices_.data());
    }
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(this->vertices_.size()));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    this->vertices_.clear();
}

} // namespace hd2d
