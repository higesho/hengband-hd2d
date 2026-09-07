/*!
 * @file sprite_atlas.cpp
 * @brief `sprite_atlas.h` の実装。
 */
#include "assets/sprite_atlas.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace hd2d {

using namespace hd2d::gl;

namespace {

//! α がこれ未満の画素は**完全に透明**にする。半透明の縁を残すと「ぼけた小さい絵」になる。
constexpr int kAlphaCutoff = 110;
//! 各成分をこの段数に丸める（減色）。段を粗くしすぎると色が飛ぶ。
constexpr int kColourLevels = 24;

std::uint8_t quantize(float value)
{
    const float clamped = std::clamp(value, 0.f, 255.f);
    const float step = 255.f / static_cast<float>(kColourLevels - 1);
    return static_cast<std::uint8_t>(std::lround(std::round(clamped / step) * step));
}

} // namespace

bool SpriteAtlas::init(std::string &err)
{
    const int wanted = IMG_INIT_PNG;
    if ((IMG_Init(wanted) & wanted) != wanted) {
        err = std::string("IMG_Init(PNG) failed: ") + IMG_GetError();
        return false;
    }

    glGenTextures(1, &this->texture_);
    glBindTexture(GL_TEXTURE_2D, this->texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    {
        const std::vector<std::uint8_t> blank(
            static_cast<std::size_t>(kAtlasSide) * static_cast<std::size_t>(kAtlasSide) * 4, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), kAtlasSide, kAtlasSide, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, blank.data());
    }
    // **`GL_NEAREST`**（設計書 §11「ドット絵を眠らせない」）。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glBindTexture(GL_TEXTURE_2D, 0);

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "スプライトのアトラスで GL エラー: " + gl_err;
        return false;
    }
    return true;
}

void SpriteAtlas::shutdown()
{
    if (this->texture_ != 0) {
        glDeleteTextures(1, &this->texture_);
        this->texture_ = 0;
    }
    this->rects_.clear();
    IMG_Quit();
}

bool SpriteAtlas::blit(const std::string &path, int at_x, int at_y)
{
    SDL_Surface *const raw = IMG_Load(path.c_str());
    if (raw == nullptr) {
        return false;
    }
    SDL_Surface *const src = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(raw);
    if (src == nullptr) {
        return false;
    }

    std::vector<std::uint8_t> out(static_cast<std::size_t>(kSpritePx) * static_cast<std::size_t>(kSpritePx) * 4, 0);
    const auto *const pixels = static_cast<const std::uint8_t *>(src->pixels);
    for (int ty = 0; ty < kSpritePx; ++ty) {
        for (int tx = 0; tx < kSpritePx; ++tx) {
            // 元画像のどの矩形を平均するか（辺の長さが割り切れなくてもよいように端で丸める）。
            const int x0 = (tx * src->w) / kSpritePx;
            const int x1 = std::max(x0 + 1, ((tx + 1) * src->w) / kSpritePx);
            const int y0 = (ty * src->h) / kSpritePx;
            const int y1 = std::max(y0 + 1, ((ty + 1) * src->h) / kSpritePx);

            float sum_r = 0.f;
            float sum_g = 0.f;
            float sum_b = 0.f;
            float sum_a = 0.f;
            float weight = 0.f;
            int samples = 0;
            for (int y = y0; y < y1; ++y) {
                const auto *row = reinterpret_cast<const std::uint32_t *>(
                    pixels + (static_cast<std::size_t>(y) * static_cast<std::size_t>(src->pitch)));
                for (int x = x0; x < x1; ++x) {
                    const std::uint32_t texel = row[x];
                    const auto a = static_cast<float>((texel >> 24) & 0xFFu);
                    // **α で重みを付ける。**透明画素の色を混ぜると輪郭が暗く濁る。
                    sum_r += static_cast<float>(texel & 0xFFu) * a;
                    sum_g += static_cast<float>((texel >> 8) & 0xFFu) * a;
                    sum_b += static_cast<float>((texel >> 16) & 0xFFu) * a;
                    sum_a += a;
                    weight += a;
                    ++samples;
                }
            }
            const std::size_t at = ((static_cast<std::size_t>(ty) * static_cast<std::size_t>(kSpritePx))
                                       + static_cast<std::size_t>(tx))
                * 4;
            const float mean_a = (samples > 0) ? (sum_a / static_cast<float>(samples)) : 0.f;
            if ((weight <= 0.f) || (mean_a < static_cast<float>(kAlphaCutoff))) {
                continue; // 透明のまま（**しきい値で振る**のが階段状の輪郭の本体）
            }
            out[at + 0] = quantize(sum_r / weight);
            out[at + 1] = quantize(sum_g / weight);
            out[at + 2] = quantize(sum_b / weight);
            out[at + 3] = 255;
        }
    }
    SDL_FreeSurface(src);

    glBindTexture(GL_TEXTURE_2D, this->texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, at_x, at_y, kSpritePx, kSpritePx, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

const SpriteRect &SpriteAtlas::acquire(std::uint16_t tile_index, const std::string &path)
{
    const auto found = this->rects_.find(tile_index);
    if (found != this->rects_.end()) {
        return found->second;
    }

    SpriteRect rect;
    if (path.empty()) {
        ++this->failed_;
        return this->rects_.emplace(tile_index, rect).first->second;
    }
    if ((this->pen_y_ + kSpritePx) > kAtlasSide) {
        if (!this->full_warned_) {
            std::fprintf(stderr, "[hd2d] the sprite atlas is full; new tiles will not be drawn\n");
            this->full_warned_ = true;
        }
        ++this->failed_;
        return this->rects_.emplace(tile_index, rect).first->second;
    }

    if (!this->blit(path, this->pen_x_, this->pen_y_)) {
        std::fprintf(stderr, "[hd2d] could not load tile %u: %s\n", static_cast<unsigned>(tile_index), path.c_str());
        ++this->failed_;
        return this->rects_.emplace(tile_index, rect).first->second;
    }

    rect.x = this->pen_x_;
    rect.y = this->pen_y_;
    rect.w = kSpritePx;
    rect.h = kSpritePx;
    rect.valid = true;
    this->pen_x_ += kSpritePx;
    if ((this->pen_x_ + kSpritePx) > kAtlasSide) {
        this->pen_x_ = 0;
        this->pen_y_ += kSpritePx;
    }
    ++this->loaded_;
    return this->rects_.emplace(tile_index, rect).first->second;
}

} // namespace hd2d
