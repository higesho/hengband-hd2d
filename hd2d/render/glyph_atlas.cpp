/*!
 * @file glyph_atlas.cpp
 * @brief `glyph_atlas.h` の実装。
 */
#include "render/glyph_atlas.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace hd2d::gl;

namespace hd2d {

namespace {

/*!
 * @brief 探すフォント。**等幅を先に**（`render/text_overlay.cpp` と同じ並び）。
 * @note 同じ表を 2 か所に持っているのは、あちらが画面の字・こちらが 3D の字で
 * 求める大きさが違い、片方の都合で並びを変えたくないためである。
 * **綴りを変えるときは両方見ること。**
 */
const char *const kFontCandidates[] = {
    "assets/fonts/placeholder_mono.ttf", //!< 同梱物があればそれを最優先
    "C:\\Windows\\Fonts\\consola.ttf", //!< **等幅を先に**（字の幅が揃うと並びが読みやすい）
    "C:\\Windows\\Fonts\\lucon.ttf",
    "C:\\Windows\\Fonts\\msgothic.ttc",
    "C:\\Windows\\Fonts\\MS Gothic.ttf",
    "/system/fonts/DroidSansMono.ttf", //!< Android
    "/system/fonts/RobotoMono-Regular.ttf",
    "/system/fonts/DroidSans.ttf",
};

} // namespace

bool GlyphAtlas::init(int px, std::string &err)
{
    if (TTF_WasInit() == 0) {
        if (TTF_Init() != 0) {
            err = std::string("TTF_Init failed: ") + TTF_GetError();
            return false;
        }
    }
    const int size = (px > 0) ? px : kDefaultPx;
    for (const char *const path : kFontCandidates) {
        this->font_ = TTF_OpenFont(path, size);
        if (this->font_ != nullptr) {
            std::fprintf(stderr, "[hd2d] glyph font: %s (%dpx)\n", path, size);
            break;
        }
    }
    if (this->font_ == nullptr) {
        err = "アスキー実体のフォントが見つかりませんでした（consola.ttf / msgothic.ttc）。";
        return false;
    }

    glGenTextures(1, &this->atlas_);
    glBindTexture(GL_TEXTURE_2D, this->atlas_);
    {
        //! 空は**透明**（α = 0）。焼いていない所を触っても何も出ない。
        const std::vector<std::uint8_t> blank(
            static_cast<std::size_t>(kAtlasSide) * static_cast<std::size_t>(kAtlasSide) * 4u, 0u);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), kAtlasSide, kAtlasSide, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, blank.data());
    }
    /*
     * **`GL_NEAREST` にしない。**ボクセルの材は `GL_NEAREST` で眠らせないのが約束だが
     * （設計書 §11）、字は輪郭が斜めの線でできているので、拡大したときに
     * 最近傍だと階段がそのまま見える。64px を 100px 前後へ引き伸ばす使い方なので
     * 線形のほうが読みやすい。**縁取りがあるので滲んで消えることはない。**
     */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glBindTexture(GL_TEXTURE_2D, 0);

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "アスキー実体のアトラスで GL エラー: " + gl_err;
        return false;
    }
    return true;
}

bool GlyphAtlas::bake_sheet(std::string &err)
{
    if (this->sheet_ != 0) {
        return true; //!< 2 度目は何もしない
    }
    if (this->font_ == nullptr) {
        err = "文字シートを焼く前に GlyphAtlas::init が要ります。";
        return false;
    }
    /*
     * マスの大きさ。**いちばん大きい字が入る正方形**を採る（等間隔の格子なので、
     * 1 つでもはみ出すと隣の字を汚す）。`W` と `_` と `|` で幅・高さの上限を測る。
     */
    int cell = 0;
    for (int code = kSheetFirst; code < (kSheetFirst + (kSheetCols * kSheetRows)); ++code) {
        int w = 0;
        int h = 0;
        const char text[2] = { static_cast<char>(code), '\0' };
        if (TTF_SizeUTF8(this->font_, text, &w, &h) == 0) {
            cell = std::max({ cell, w, h });
        }
    }
    if (cell <= 0) {
        err = "文字シートの枡の大きさを測れませんでした。";
        return false;
    }
    /*
     * **マスに余白を持たせる**。
     * ネオンのにじみはシェーダが字のまわりを何点か余分に舐めて作るので、
     * 字がマスいっぱいだと**舐めた先が隣の字**になる（`E` の右に `F` の縦棒が出る）。
     * 1.5 倍にすれば、どの向きにもマスの 1/6 ぶんの空きが残る。
     * @note **これは字を小さくすることではない。**面に貼るときの倍率
     * （`LookParams::glyph_scale`）は「マスの大きさ」なので、字はその 2/3 に見える。
     */
    cell = ((cell * 3) + 1) / 2;
    this->sheet_cell_ = cell;
    const int sheet_w = cell * kSheetCols;
    const int sheet_h = cell * kSheetRows;
    //! 覆いだけ（`GL_R8`）。色はマスごとにコアの 16 色を渡すので、作る必要が無い。
    std::vector<std::uint8_t> sheet(static_cast<std::size_t>(sheet_w) * static_cast<std::size_t>(sheet_h), 0u);

    for (int idx = 0; idx < (kSheetCols * kSheetRows); ++idx) {
        const int code = kSheetFirst + idx;
        const char text[2] = { static_cast<char>(code), '\0' };
        SDL_Surface *const surface = TTF_RenderUTF8_Blended(this->font_, text, SDL_Color{ 255, 255, 255, 255 });
        if (surface == nullptr) {
            continue; //!< 焼けない字はそのマスが空のまま（**捏造しない**）
        }
        SDL_Surface *converted = surface;
        if (surface->format->format != SDL_PIXELFORMAT_ARGB8888) {
            converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
        }
        if (converted != nullptr) {
            //! **マスの中央へ寄せる。**左上に置くと、細い字（`|`）だけ面の隅に出る。
            const int ox = (cell - converted->w) / 2;
            const int oy = (cell - converted->h) / 2;
            const int base_x = (idx % kSheetCols) * cell;
            const int base_y = (idx / kSheetCols) * cell;
            const auto *const pixels = static_cast<const std::uint8_t *>(converted->pixels);
            for (int y = 0; y < converted->h; ++y) {
                const int dy = base_y + oy + y;
                if ((dy < 0) || (dy >= sheet_h)) {
                    continue;
                }
                for (int x = 0; x < converted->w; ++x) {
                    const int dx = base_x + ox + x;
                    if ((dx < base_x) || (dx >= (base_x + cell))) {
                        continue; //!< マスからはみ出す字は切る（隣を汚さない）
                    }
                    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(converted->pitch))
                        + (static_cast<std::size_t>(x) * 4u);
                    sheet[(static_cast<std::size_t>(dy) * static_cast<std::size_t>(sheet_w))
                        + static_cast<std::size_t>(dx)]
                        = pixels[at + 3u];
                }
            }
            if (converted != surface) {
                SDL_FreeSurface(converted);
            }
        }
        SDL_FreeSurface(surface);
    }

    glGenTextures(1, &this->sheet_);
    glBindTexture(GL_TEXTURE_2D, this->sheet_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R8), sheet_w, sheet_h, 0, GL_RED, GL_UNSIGNED_BYTE,
        sheet.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    /*
     * **必ず端で止める。**繰り返すと、マスの縁を跨いだ補間で隣の字の欠片が入る
     * ——「`E` のはずが右端に `F` の縦棒が見える」になる。
     */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glBindTexture(GL_TEXTURE_2D, 0);

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "文字シートで GL エラー: " + gl_err;
        return false;
    }
    std::fprintf(stderr, "[hd2d] glyph sheet: %dx%d (%dpx cells, %d glyphs)\n", sheet_w, sheet_h, cell,
        kSheetCols * kSheetRows);
    return true;
}

void GlyphAtlas::shutdown()
{
    if (this->sheet_ != 0) {
        glDeleteTextures(1, &this->sheet_);
        this->sheet_ = 0;
    }
    if (this->atlas_ != 0) {
        glDeleteTextures(1, &this->atlas_);
        this->atlas_ = 0;
    }
    if (this->font_ != nullptr) {
        TTF_CloseFont(this->font_);
        this->font_ = nullptr;
    }
    this->glyphs_.clear();
    this->pen_x_ = 0;
    this->pen_y_ = 0;
    this->shelf_h_ = 0;
    this->warned_full_ = false;
}

const GlyphAtlas::Entry *GlyphAtlas::entry_for(char ch)
{
    if ((this->font_ == nullptr) || (this->atlas_ == 0)) {
        return nullptr;
    }
    const auto byte = static_cast<unsigned char>(ch);
    if ((byte < 0x21) || (byte > 0x7E)) {
        //! 空白と制御文字は**描くものが無い**（空白の実体は無い）。
        return nullptr;
    }
    const auto found = this->glyphs_.find(ch);
    if (found != this->glyphs_.end()) {
        return found->second.valid() ? &found->second : nullptr;
    }

    const char text[2] = { ch, '\0' };
    SDL_Surface *const surface = TTF_RenderUTF8_Blended(this->font_, text, SDL_Color{ 255, 255, 255, 255 });
    if (surface == nullptr) {
        this->glyphs_[ch] = Entry{}; //!< 焼けなかったことも覚える（毎フレーム試さない）
        return nullptr;
    }

    /*
     * **縁のぶんだけ広げて焼く。**`SDL_ttf` の `TTF_SetFontOutline()` を使うと
     * フォントの状態が変わり、同じフォントを使う他の描画（この実行体では別物だが）に
     * 影響しうる。ここは覆いを膨らませて自分で作る——**中身と縁を 1 枚で作れる**ので
     * 2 度作る必要も無い。
     */
    const int gw = surface->w + (kOutline * 2);
    const int gh = surface->h + (kOutline * 2);
    if ((gw > kAtlasSide) || (gh > kAtlasSide)) {
        SDL_FreeSurface(surface);
        this->glyphs_[ch] = Entry{};
        return nullptr;
    }
    if ((this->pen_x_ + gw) > kAtlasSide) {
        this->pen_x_ = 0;
        this->pen_y_ += this->shelf_h_;
        this->shelf_h_ = 0;
    }
    if ((this->pen_y_ + gh) > kAtlasSide) {
        //! 埋まった。**警告は 1 度だけ**（`text_overlay.h` と同じ作法）。
        if (!this->warned_full_) {
            this->warned_full_ = true;
            std::fprintf(stderr, "[hd2d] アスキー実体のアトラス（%d×%d）が埋まりました。"
                                 "これ以降の字は描かれません\n",
                kAtlasSide, kAtlasSide);
        }
        SDL_FreeSurface(surface);
        this->glyphs_[ch] = Entry{};
        return nullptr;
    }

    //! 覆い（α）を素直な 1 バイト／画素へ写す。`SDL_ttf` は 32bit で返す。
    std::vector<std::uint8_t> cover(static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh), 0u);
    {
        SDL_Surface *converted = surface;
        if (surface->format->format != SDL_PIXELFORMAT_ARGB8888) {
            converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
        }
        if (converted != nullptr) {
            const auto *const pixels = static_cast<const std::uint8_t *>(converted->pixels);
            for (int y = 0; y < converted->h; ++y) {
                for (int x = 0; x < converted->w; ++x) {
                    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(converted->pitch))
                        + (static_cast<std::size_t>(x) * 4u);
                    //! ARGB8888 をリトルエンディアンで読むと α は 4 バイト目。
                    cover[(static_cast<std::size_t>(y + kOutline) * static_cast<std::size_t>(gw))
                        + static_cast<std::size_t>(x + kOutline)]
                        = pixels[at + 3u];
                }
            }
            if (converted != surface) {
                SDL_FreeSurface(converted);
            }
        }
    }
    SDL_FreeSurface(surface);

    /*
     * 本体（白）と縁（暗い色）を 1 枚の RGBA へ組む。
     *   - 覆いが濃い所 … 白。上から `v_tint` で色が乗る
     *   - その周り `kOutline` 画素 … **暗い色**。色を掛けても暗いまま残る
     *   - それ以外 … 透明（ビルボードのシェーダが `α < 0.5` で捨てる）
     */
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh) * 4u, 0u);
    for (int y = 0; y < gh; ++y) {
        for (int x = 0; x < gw; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(gw))
                + static_cast<std::size_t>(x);
            const std::uint8_t self = cover[at];
            std::uint8_t dilated = self; //!< `near` は windows.h の旧マクロと衝突しうるので使わない
            for (int dy = -kOutline; dy <= kOutline; ++dy) {
                for (int dx = -kOutline; dx <= kOutline; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if ((nx < 0) || (ny < 0) || (nx >= gw) || (ny >= gh)) {
                        continue;
                    }
                    dilated = std::max(dilated,
                        cover[(static_cast<std::size_t>(ny) * static_cast<std::size_t>(gw))
                            + static_cast<std::size_t>(nx)]);
                }
            }
            const std::size_t out_at = at * 4u;
            if (self >= 128u) {
                rgba[out_at + 0u] = 255u;
                rgba[out_at + 1u] = 255u;
                rgba[out_at + 2u] = 255u;
                rgba[out_at + 3u] = 255u;
            } else if (dilated >= 128u) {
                //! 縁。真っ黒にはしない（真っ黒は暗所で地形と同化して輪郭が消える）。
                rgba[out_at + 0u] = 24u;
                rgba[out_at + 1u] = 24u;
                rgba[out_at + 2u] = 32u;
                rgba[out_at + 3u] = 255u;
            }
        }
    }

    glBindTexture(GL_TEXTURE_2D, this->atlas_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, this->pen_x_, this->pen_y_, gw, gh, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    Entry entry;
    //! **テクセルで返す**（`BillboardRenderer::draw` が `atlas_side` で割る約束）。
    entry.u0 = static_cast<float>(this->pen_x_);
    entry.v0 = static_cast<float>(this->pen_y_);
    entry.u1 = static_cast<float>(this->pen_x_ + gw);
    entry.v1 = static_cast<float>(this->pen_y_ + gh);
    entry.w = gw;
    entry.h = gh;

    this->pen_x_ += gw;
    this->shelf_h_ = std::max(this->shelf_h_, gh);

    const auto inserted = this->glyphs_.emplace(ch, entry);
    return &inserted.first->second;
}

} // namespace hd2d
