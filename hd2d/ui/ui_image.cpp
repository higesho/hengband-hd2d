/*!
 * @file ui_image.cpp
 * @brief `ui_image.h` の実装。
 */
#include "ui/ui_image.h"

#include "render/gl_program.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <algorithm>
#include <cstdio>

namespace hd2d {

using namespace hd2d::gl;

namespace {

const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec2 a_pos;    // 画素座標（左上原点）
layout(location = 1) in vec2 a_uv;
uniform vec2 u_screen;
out vec2 v_uv;
void main()
{
    vec2 ndc = vec2(a_pos.x / u_screen.x * 2.0 - 1.0, 1.0 - a_pos.y / u_screen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = a_uv;
}
)";

const char *const kFragmentSource = R"(#version 460 core
in vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_alpha;
out vec4 o_color;
void main()
{
    vec4 c = texture(u_tex, v_uv);
    o_color = vec4(c.rgb, c.a * u_alpha);
}
)";

} // namespace

bool UiImage::load(const std::string &path, std::string &err)
{
    this->unload();
    SDL_Surface *const raw = IMG_Load(path.c_str());
    if (raw == nullptr) {
        err = path + " を読めませんでした: " + IMG_GetError();
        return false;
    }
    SDL_Surface *const rgba = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(raw);
    if (rgba == nullptr) {
        err = path + " を RGBA へ直せませんでした: " + SDL_GetError();
        return false;
    }

    glGenTextures(1, &this->texture_);
    glBindTexture(GL_TEXTURE_2D, this->texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), rgba->w, rgba->h, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
    /*
     * **拡大縮小しない前提**（等倍で切り出す）なので、拡大の補間は要らない。
     * タイトル画だけは覆うために伸びるので、そこは線形のほうがよい。両方に効かせるため
     * 線形にしておく（等倍で貼るときは画素の中心が一致するので、線形でもぼけない）。
     */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glBindTexture(GL_TEXTURE_2D, 0);

    this->w_ = rgba->w;
    this->h_ = rgba->h;
    SDL_FreeSurface(rgba);

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = path + " を GPU へ載せるときに GL エラー: " + gl_err;
        this->unload();
        return false;
    }
    std::fprintf(stderr, "[hd2d] ui image: %s (%dx%d)\n", path.c_str(), this->w_, this->h_);
    return true;
}

void UiImage::unload()
{
    if (this->texture_ != 0) {
        glDeleteTextures(1, &this->texture_);
        this->texture_ = 0;
    }
    this->w_ = 0;
    this->h_ = 0;
}

bool UiImagePainter::init(std::string &err)
{
    this->program_ = compile_program(kVertexSource, kFragmentSource, err);
    if (this->program_ == 0) {
        return false;
    }
    this->loc_screen_ = glGetUniformLocation(this->program_, "u_screen");
    this->loc_tex_ = glGetUniformLocation(this->program_, "u_tex");
    this->loc_alpha_ = glGetUniformLocation(this->program_, "u_alpha");

    glGenVertexArrays(1, &this->vao_);
    glGenBuffers(1, &this->vbo_);
    glBindVertexArray(this->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(float) * 4 * 6), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<const void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<const void *>(sizeof(float) * 2));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "1 枚絵の用意で GL エラー: " + gl_err;
        return false;
    }
    return true;
}

void UiImagePainter::shutdown()
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

void UiImagePainter::begin(int screen_w, int screen_h)
{
    this->screen_w_ = (screen_w > 0) ? screen_w : 1;
    this->screen_h_ = (screen_h > 0) ? screen_h : 1;
}

void UiImagePainter::draw(const UiImage &image, const RectPx &dst,
    float u0, float v0, float u1, float v1, float alpha) const
{
    if (!image.valid() || dst.empty() || (this->program_ == 0) || (alpha <= 0.f)) {
        return;
    }
    const auto x0 = static_cast<float>(dst.x);
    const auto y0 = static_cast<float>(dst.y);
    const auto x1 = static_cast<float>(dst.x + dst.w);
    const auto y1 = static_cast<float>(dst.y + dst.h);
    const float vertices[6][4] = {
        { x0, y0, u0, v0 },
        { x1, y0, u1, v0 },
        { x1, y1, u1, v1 },
        { x0, y0, u0, v0 },
        { x1, y1, u1, v1 },
        { x0, y1, u0, v1 },
    };

    //! **自分で viewport を張る**（罠 28。直前のポスト処理が 3D の矩形を残している）。
    glViewport(0, 0, this->screen_w_, this->screen_h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(this->program_);
    glUniform2f(this->loc_screen_, static_cast<float>(this->screen_w_), static_cast<float>(this->screen_h_));
    glUniform1i(this->loc_tex_, 0);
    glUniform1f(this->loc_alpha_, alpha);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, image.texture());

    glBindVertexArray(this->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(vertices)), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void UiImagePainter::draw_crop(const UiImage &image, const RectPx &dst,
    int sx, int sy, int sw, int sh, float alpha) const
{
    if (!image.valid() || (sw <= 0) || (sh <= 0)) {
        return;
    }
    const auto tw = static_cast<float>(image.width());
    const auto th = static_cast<float>(image.height());
    this->draw(image, dst, static_cast<float>(sx) / tw, static_cast<float>(sy) / th,
        static_cast<float>(sx + sw) / tw, static_cast<float>(sy + sh) / th, alpha);
}

void UiImagePainter::draw_cover(const UiImage &image, const RectPx &dst, float alpha) const
{
    if (!image.valid() || dst.empty()) {
        return;
    }
    /*
     * 縦横比を保ったまま `dst` を覆う。**はみ出す側を中央で切る**ので、
     * 絵の真ん中は必ず見える（端が切れるのは許容する）。
     */
    const double want = static_cast<double>(dst.w) / static_cast<double>(dst.h);
    const double have = static_cast<double>(image.width()) / static_cast<double>(image.height());
    float u0 = 0.f;
    float v0 = 0.f;
    float u1 = 1.f;
    float v1 = 1.f;
    if (have > want) {
        // 絵のほうが横長。左右を切る。
        const auto keep = static_cast<float>(want / have);
        u0 = (1.f - keep) * 0.5f;
        u1 = u0 + keep;
    } else if (have < want) {
        const auto keep = static_cast<float>(have / want);
        v0 = (1.f - keep) * 0.5f;
        v1 = v0 + keep;
    }
    this->draw(image, dst, u0, v0, u1, v1, alpha);
}

int panel_backdrop_slot(int panel)
{
    //! 枠の番号は**場所で固定**（`ui_layout.h`）。枚数が変わっても分かれ目は動かない。
    if ((panel >= kSubBottomSlot) && (panel < (kSubBottomSlot + kSubBottomMax))) {
        return 0; // 下段の帯
    }
    if ((panel >= kSubRightSlot) && (panel < (kSubRightSlot + kSubRightMax))) {
        return 1; // 右列
    }
    return -1;
}

bool panel_backdrop_source(const UiLayout &layout, int panel, int master_w, int master_h,
    int &sx, int &sy, int &sw, int &sh)
{
    const int slot = panel_backdrop_slot(panel);
    if (slot < 0) {
        return false;
    }
    const RectPx &area = layout.sub[panel];
    if (area.empty()) {
        return false;
    }
    /*
     * 帯・列を**1 枚の絵**と見なし、その中の自分の位置を切り出す。
     * パネルごとに絵を割り当てると、境界を掴んで動かすたびに柄が切れてずれる。
     */
    const RectPx &origin = (slot == 0) ? layout.sub[kSubBottomSlot] : layout.sub[kSubRightSlot];
    if (origin.empty()) {
        return false;
    }
    sx = (slot == 0) ? (area.x - 0) : (area.x - origin.x);
    sy = (slot == 0) ? (area.y - origin.y) : (area.y - 0);
    sw = area.w;
    sh = area.h;
    if ((sx < 0) || (sy < 0)) {
        return false;
    }
    //! **マスタより大きいパネルは賄えない。**足りない所は下地色が残る（黙って伸ばさない）。
    return ((sx + sw) <= master_w) && ((sy + sh) <= master_h);
}

} // namespace hd2d
