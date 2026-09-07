/*!
 * @file text_overlay.cpp
 * @brief `text_overlay.h` の実装。
 */
#include "render/text_overlay.h"

#include "render/gl_program.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <cstdio>

namespace hd2d {

using namespace hd2d::gl;

namespace {

/*!
 * @brief 探すフォント。**MS ゴシックが第一候補**。
 * @details 半角＝全角の半分という刻みがコアの Term と同じなので、文字グリッドを
 * そのまま画面へ写せる。等幅の欧文フォントは日本語を持たないので、後ろの候補は
 * 「MS ゴシックが無い機械でも ASCII だけは読める」ための保険にすぎない。
 * @note 旧 2D UI のフォント選びも同じ結論の並びを持っていたが、あちらの**コードは
 * 引かない**（設計書 必守制約 6）。同じ機械を相手にしているので候補が揃うだけである。
 */
const char *const kFontCandidates[] = {
    "assets/fonts/placeholder_mono.ttf", //!< 同梱物があればそれを最優先
    "C:\\Windows\\Fonts\\msgothic.ttc",
    "C:\\Windows\\Fonts\\MS Gothic.ttf",
    "C:\\Windows\\Fonts\\consola.ttf", //!< 以下は日本語を持たない（`?` になる）
    "C:\\Windows\\Fonts\\lucon.ttf",
};

const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec2 a_pos;    // 画素座標（左上原点）
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform vec2 u_screen;
out vec2 v_uv;
out vec4 v_color;
void main()
{
    vec2 ndc = vec2(a_pos.x / u_screen.x * 2.0 - 1.0, 1.0 - a_pos.y / u_screen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = a_uv;
    v_color = a_color;
}
)";

const char *const kFragmentSource = R"(#version 460 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_atlas;
out vec4 o_color;
void main()
{
    // アトラスはグリフの被覆率（α）だけを持つ 1 チャンネル。色は頂点から来る。
    float coverage = texture(u_atlas, v_uv).r;
    o_color = vec4(v_color.rgb, v_color.a * coverage);
}
)";

//! 符号位置を UTF-8 バイト列へ（SDL_ttf へ 1 文字だけ渡すため）。
std::string encode_utf8(std::uint32_t cp)
{
    std::string out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

} // namespace

std::uint32_t TextOverlay::next_codepoint(std::string_view text, std::size_t &pos)
{
    const auto lead = static_cast<unsigned char>(text[pos]);
    std::size_t extra = 0;
    std::uint32_t cp = 0;
    if (lead < 0x80) {
        ++pos;
        return lead;
    } else if ((lead & 0xE0) == 0xC0) {
        extra = 1;
        cp = lead & 0x1Fu;
    } else if ((lead & 0xF0) == 0xE0) {
        extra = 2;
        cp = lead & 0x0Fu;
    } else if ((lead & 0xF8) == 0xF0) {
        extra = 3;
        cp = lead & 0x07u;
    } else {
        ++pos; // 継続バイトが先頭に来た＝壊れている
        return 0xFFFD;
    }
    if ((pos + extra) >= text.size()) {
        ++pos; // 続きのバイトが足りない（末尾で切れている）
        return 0xFFFD;
    }
    for (std::size_t i = 1; i <= extra; ++i) {
        const auto byte = static_cast<unsigned char>(text[pos + i]);
        if ((byte & 0xC0) != 0x80) {
            ++pos;
            return 0xFFFD;
        }
        cp = (cp << 6) | (byte & 0x3Fu);
    }
    pos += extra + 1;
    return cp;
}

bool TextOverlay::init(int px, std::string &err)
{
    if (TTF_WasInit() == 0) {
        if (TTF_Init() != 0) {
            err = std::string("TTF_Init failed: ") + TTF_GetError();
            return false;
        }
    }

    for (const char *const path : kFontCandidates) {
        this->font_ = TTF_OpenFont(path, px);
        if (this->font_ != nullptr) {
            std::fprintf(stderr, "[hd2d] text font: %s (%dpx)\n", path, px);
            break;
        }
    }
    if (this->font_ == nullptr) {
        err = "文字を描くフォントが見つかりませんでした（msgothic.ttc / consola.ttf）。";
        return false;
    }

    this->cell_h_ = TTF_FontHeight(this->font_);
    int advance = 0;
    if ((TTF_GlyphMetrics(this->font_, static_cast<Uint16>('M'), nullptr, nullptr, nullptr, nullptr, &advance) != 0) || (advance <= 0)) {
        advance = px / 2;
    }
    this->cell_w_ = advance;
    if ((this->cell_w_ <= 0) || (this->cell_h_ <= 0)) {
        err = "フォントの寸法を取れませんでした。";
        return false;
    }

    // --- 空のアトラス（中身は使われたときに 1 字ずつ焼く） ---
    glGenTextures(1, &this->atlas_);
    glBindTexture(GL_TEXTURE_2D, this->atlas_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); //!< 1 チャンネルなので既定の 4 では行がずれる
    {
        const std::vector<std::uint8_t> blank(static_cast<std::size_t>(kAtlasSide) * static_cast<std::size_t>(kAtlasSide), 0);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R8), kAtlasSide, kAtlasSide, 0, GL_RED, GL_UNSIGNED_BYTE, blank.data());
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glBindTexture(GL_TEXTURE_2D, 0);

    this->program_ = compile_program(kVertexSource, kFragmentSource, err);
    if (this->program_ == 0) {
        return false;
    }
    this->loc_screen_ = glGetUniformLocation(this->program_, "u_screen");
    this->loc_atlas_ = glGetUniformLocation(this->program_, "u_atlas");

    glGenVertexArrays(1, &this->vao_);
    glGenBuffers(1, &this->vbo_);
    glBindVertexArray(this->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void *>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void *>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void *>(offsetof(Vertex, r)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "文字描画の初期化で GL エラー: " + gl_err;
        return false;
    }
    return true;
}

void TextOverlay::shutdown()
{
    if (this->vbo_ != 0) {
        glDeleteBuffers(1, &this->vbo_);
        this->vbo_ = 0;
    }
    if (this->vao_ != 0) {
        glDeleteVertexArrays(1, &this->vao_);
        this->vao_ = 0;
    }
    if (this->atlas_ != 0) {
        glDeleteTextures(1, &this->atlas_);
        this->atlas_ = 0;
    }
    if (this->program_ != 0) {
        glDeleteProgram(this->program_);
        this->program_ = 0;
    }
    if (this->font_ != nullptr) {
        TTF_CloseFont(this->font_);
        this->font_ = nullptr;
    }
    this->glyphs_.clear();
    /*
     * **GL の器を消したら、それを指している数も 0 へ戻す**（2026-08-11 に踏んだ）。
     *
     * ここを消し忘れていたので、`shutdown()` → `init()` と作り直したとき
     * `vbo_capacity_` だけが**前の器の大きさ**を覚えていた。新しい VBO は 0 バイトなのに
     * `flush()` が「入るはず」と読んで `glBufferSubData` を撃ち、
     * **GL_INVALID_VALUE（Invalid offset and/or size）で落ちた**
     * （2026-08-11 に気づいた:「はじめの辺境のカットインの際にウインドウサイズを
     * 変更すると落ちる」。カットインは地図の縦幅から字の大きさを決めるので、
     * 窓が変わるたびにここを通る）。
     *
     * 棚詰めの位置（`pen_*` / `shelf_h_`）も同じ話——アトラスは作り直されて真っ新なのに、
     * 前の続きから詰め始めると隙間が死ぬ。**この関数は「作る前の状態へ戻す」ものである。**
     *
     * @note P8 までは 1 度作ったら終わりだったので、この抜けは表に出ようが無かった。
     */
    this->pen_x_ = 0;
    this->pen_y_ = 0;
    this->shelf_h_ = 0;
    this->atlas_full_warned_ = false;
    this->vbo_capacity_ = 0;
    this->vertices_.clear();
}

const TextOverlay::Glyph *TextOverlay::glyph_for(std::uint32_t codepoint)
{
    const auto found = this->glyphs_.find(codepoint);
    if (found != this->glyphs_.end()) {
        return &found->second;
    }

    Glyph glyph;
    const std::string utf8 = encode_utf8(codepoint);
    SDL_Surface *surface = TTF_RenderUTF8_Blended(this->font_, utf8.c_str(), SDL_Color{ 255, 255, 255, 255 });
    if ((surface == nullptr) || (surface->w <= 0) || (surface->h <= 0)) {
        // 焼けない字（空白・フォントに無い字）。マスだけは進めたいので幅は推定して持つ。
        if (surface != nullptr) {
            SDL_FreeSurface(surface);
        }
        int advance = 0;
        if ((codepoint <= 0xFFFF)
            && (TTF_GlyphMetrics(this->font_, static_cast<Uint16>(codepoint), nullptr, nullptr, nullptr, nullptr, &advance) == 0)
            && (advance > 0)) {
            glyph.w = advance;
        } else {
            glyph.w = this->cell_w_;
        }
        glyph.h = this->cell_h_;
        glyph.drawable = false;
        return &this->glyphs_.emplace(codepoint, glyph).first->second;
    }

    // --- 棚詰め（左から右へ、埋まったら次の段へ） ---
    if ((this->pen_x_ + surface->w) > kAtlasSide) {
        this->pen_x_ = 0;
        this->pen_y_ += this->shelf_h_;
        this->shelf_h_ = 0;
    }
    if ((this->pen_y_ + surface->h) > kAtlasSide) {
        if (!this->atlas_full_warned_) {
            std::fprintf(stderr, "[hd2d] the glyph atlas is full; new characters will not be drawn\n");
            this->atlas_full_warned_ = true;
        }
        glyph.w = surface->w;
        glyph.h = surface->h;
        glyph.drawable = false;
        SDL_FreeSurface(surface);
        return &this->glyphs_.emplace(codepoint, glyph).first->second;
    }

    // α だけを抜き出して上げる（色は頂点が持つので RGB は要らない）。
    std::vector<std::uint8_t> coverage(static_cast<std::size_t>(surface->w) * static_cast<std::size_t>(surface->h));
    {
        const auto *const pixels = static_cast<const std::uint8_t *>(surface->pixels);
        for (int y = 0; y < surface->h; ++y) {
            const auto *row = reinterpret_cast<const std::uint32_t *>(pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch));
            for (int x = 0; x < surface->w; ++x) {
                coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->w) + static_cast<std::size_t>(x)]
                    = static_cast<std::uint8_t>((row[x] >> 24) & 0xFFu); // ARGB8888 の A
            }
        }
    }

    glBindTexture(GL_TEXTURE_2D, this->atlas_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, this->pen_x_, this->pen_y_, surface->w, surface->h, GL_RED, GL_UNSIGNED_BYTE, coverage.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    const auto side = static_cast<float>(kAtlasSide);
    glyph.u0 = static_cast<float>(this->pen_x_) / side;
    glyph.v0 = static_cast<float>(this->pen_y_) / side;
    glyph.u1 = static_cast<float>(this->pen_x_ + surface->w) / side;
    glyph.v1 = static_cast<float>(this->pen_y_ + surface->h) / side;
    glyph.w = surface->w;
    glyph.h = surface->h;
    glyph.drawable = true;

    this->pen_x_ += surface->w;
    if (surface->h > this->shelf_h_) {
        this->shelf_h_ = surface->h;
    }
    SDL_FreeSurface(surface);
    return &this->glyphs_.emplace(codepoint, glyph).first->second;
}

void TextOverlay::begin(int screen_w, int screen_h)
{
    this->screen_w_ = (screen_w > 0) ? screen_w : 1;
    this->screen_h_ = (screen_h > 0) ? screen_h : 1;
    this->vertices_.clear();
}

int TextOverlay::draw(int x, int y, std::string_view utf8, const TextColor &color, int max_width)
{
    float pen_x = static_cast<float>(x);
    const float pen_y = static_cast<float>(y);
    const float limit = (max_width > 0) ? (static_cast<float>(x) + static_cast<float>(max_width)) : 0.f;
    std::size_t pos = 0;
    while (pos < utf8.size()) {
        const std::uint32_t cp = next_codepoint(utf8, pos);
        if (cp == '\t') {
            pen_x += static_cast<float>(this->cell_w_ * 4);
            continue;
        }
        const Glyph *const glyph = this->glyph_for(cp);
        // **字の右端で切る。**途中まで描くと半分の字が残って読めない。
        if ((max_width > 0) && ((pen_x + static_cast<float>(glyph->w)) > limit)) {
            break;
        }
        if (glyph->drawable) {
            const float x0 = pen_x;
            const float y0 = pen_y;
            const float x1 = pen_x + static_cast<float>(glyph->w);
            const float y1 = pen_y + static_cast<float>(glyph->h);
            const Vertex a{ x0, y0, glyph->u0, glyph->v0, color.r, color.g, color.b, color.a };
            const Vertex b{ x1, y0, glyph->u1, glyph->v0, color.r, color.g, color.b, color.a };
            const Vertex c{ x1, y1, glyph->u1, glyph->v1, color.r, color.g, color.b, color.a };
            const Vertex d{ x0, y1, glyph->u0, glyph->v1, color.r, color.g, color.b, color.a };
            this->vertices_.push_back(a);
            this->vertices_.push_back(b);
            this->vertices_.push_back(c);
            this->vertices_.push_back(a);
            this->vertices_.push_back(c);
            this->vertices_.push_back(d);
        }
        pen_x += static_cast<float>(glyph->w);
    }
    return static_cast<int>(pen_x) - x;
}

int TextOverlay::draw_cell(int origin_x, int origin_y, int col, int row, std::string_view utf8, const TextColor &color)
{
    return this->draw(origin_x + (col * this->cell_w_), origin_y + (row * this->cell_h_), utf8, color);
}

std::size_t TextOverlay::fit_bytes(std::string_view utf8, int max_width)
{
    if (max_width <= 0) {
        return utf8.size();
    }
    float pen_x = 0.f;
    const auto limit = static_cast<float>(max_width);
    std::size_t pos = 0;
    std::size_t fits = 0;
    while (pos < utf8.size()) {
        const std::uint32_t cp = next_codepoint(utf8, pos);
        const float advance = (cp == '\t') ? static_cast<float>(this->cell_w_ * 4)
                                           : static_cast<float>(this->glyph_for(cp)->w);
        // **字の右端で切る**（`draw()` と同じ規則。途中まで描くと半分の字が残る）。
        if ((pen_x + advance) > limit) {
            return fits;
        }
        pen_x += advance;
        fits = pos; //!< ここまでは入る（`pos` は次の字の頭）
    }
    return fits;
}

int TextOverlay::measure(std::string_view utf8)
{
    int width = 0;
    std::size_t pos = 0;
    while (pos < utf8.size()) {
        const std::uint32_t cp = next_codepoint(utf8, pos);
        width += (cp == '\t') ? (this->cell_w_ * 4) : this->glyph_for(cp)->w;
    }
    return width;
}

void TextOverlay::flush()
{
    if (this->vertices_.empty() || (this->program_ == 0)) {
        return;
    }

    /*
     * **自分で viewport を張る**（`ui_paint.cpp` の同じ所と同じ理由）。
     * 直前に走るポスト処理は 3D の矩形へ出すために `glViewport` をそこへ設定して戻さない。
     * 呼び出し側の順序に頼っていた頃は、3D が窓の一部になる作り（`Split`）で
     * **文字が丸ごと 3D の矩形の中へ押し込まれた**。
     */
    glViewport(0, 0, this->screen_w_, this->screen_h_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(this->program_);
    glUniform2f(this->loc_screen_, static_cast<float>(this->screen_w_), static_cast<float>(this->screen_h_));
    glUniform1i(this->loc_atlas_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, this->atlas_);

    glBindVertexArray(this->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    const std::size_t bytes = this->vertices_.size() * sizeof(Vertex);
    if (bytes > this->vbo_capacity_) {
        // 伸ばすときだけ確保し直す（この量なら毎フレーム orphan するより素直で十分速い）。
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
