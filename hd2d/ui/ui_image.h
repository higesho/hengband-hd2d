/*!
 * @file ui_image.h
 * @brief UI の 1 枚絵（P8）— タイトル画とパネルの背面。
 *
 * ## 何のためのものか
 * `UiPaint` は色つきの四角しか描けない。タイトル画とパネルの背面は**絵**なので、
 * テクスチャを貼る口がここに要る。枚数は 3 枚だけ（タイトル・下段の帯・右列）なので、
 * 1 枚ずつ即座に描く（積んで 1 回で流す作りにする意味が無い）。
 *
 * ## 置き方の規則（`assets/ui/` の素材が前提にしているもの）
 * - **パネルの背面は拡大縮小しない。**帯・列を 1 枚の絵と見なし、各パネルはその中の
 *   自分の位置を**左上起点で切り出す**。パネルごとに絵を割り当てると、
 *   境界を動かすたびに柄が切れてずれる（境界は掴んで動かせる）
 * - 拡大しないので、**マスタより大きいパネルは賄えない**。足りない所は下地色が残る
 * - **タイトル画だけは拡大してよい**（画の中身に意味があるので、画面を覆う）
 *
 * @note 素材と規則は旧 2D UI の背景画と同じもの（`tools/gen_panel_bg_comfy.py`
 * が同じ前提で作っている）。**コードは引かない**（必守制約 6）。同じ絵を同じ約束で貼るので
 * 規則が揃うだけである。
 */
#pragma once

#include "render/gl_core.h"
#include "ui/ui_layout.h"

#include <string>

namespace hd2d {

//! 素材の置き場所。`assets/` は配布時にそのまま同梱される。
constexpr const char *kTitleImagePath = "assets/ui/title.png";
constexpr const char *kPanelBgBottomPath = "assets/ui/panel_bg_bottom.png";
constexpr const char *kPanelBgRightPath = "assets/ui/panel_bg_right.png";

//! 原寸で持つ 1 枚絵。**無くても遊べる**ので、読めなくても致命ではない。
class UiImage {
public:
    /*!
     * @brief 読む。失敗したら理由を返して `valid()` は偽のまま。
     * @details 縮小しない（`sprite_atlas` と逆）。切り出して等倍で貼る前提なので、
     * ここで縮めると規則が成り立たなくなる。
     */
    bool load(const std::string &path, std::string &err);
    void unload();

    bool valid() const { return this->texture_ != 0; }
    int width() const { return this->w_; }
    int height() const { return this->h_; }
    gl::GLuint texture() const { return this->texture_; }

private:
    gl::GLuint texture_{ 0 };
    int w_{ 0 };
    int h_{ 0 };
};

//! 1 枚絵を貼る。**自分で viewport を張る**（罠 28 と同じ理由）。
class UiImagePainter {
public:
    bool init(std::string &err);
    void shutdown();

    //! 画面の実寸を渡す（正射影の分母と viewport）。
    void begin(int screen_w, int screen_h);

    //! マスタ内 `(sx,sy,sw,sh)` を `dst` へ**等倍で**貼る。
    void draw_crop(const UiImage &image, const RectPx &dst, int sx, int sy, int sw, int sh, float alpha) const;
    /*!
     * @brief `dst` を覆うように貼る（縦横比は保ち、はみ出した側を中央で切る）。
     * @details タイトル画のように「画面いっぱいに 1 枚出す」用途。
     */
    void draw_cover(const UiImage &image, const RectPx &dst, float alpha) const;

private:
    void draw(const UiImage &image, const RectPx &dst, float u0, float v0, float u1, float v1, float alpha) const;

    gl::GLuint program_{ 0 };
    gl::GLuint vao_{ 0 };
    gl::GLuint vbo_{ 0 };
    gl::GLint loc_screen_{ -1 };
    gl::GLint loc_tex_{ -1 };
    gl::GLint loc_alpha_{ -1 };
    int screen_w_{ 0 };
    int screen_h_{ 0 };
};

/*!
 * @brief パネルの背面を切り出す矩形を求める（左上起点）。
 * @param panel 0..4（Sub1〜Sub5）。
 * @return 切り出せるとき true。対象外・マスタが小さすぎるときは false。
 * @details 帯の原点は下段の左上（`{0, sub[0].y}`）、列の原点は右列の左上（`{sub[3].x, 0}`）。
 * パネルの原点からの差がそのままマスタ内の起点になる。
 */
bool panel_backdrop_source(const UiLayout &layout, int panel, int master_w, int master_h,
    int &sx, int &sy, int &sw, int &sh);

//! そのパネルが使うマスタ（0 = 下段の帯 / 1 = 右列 / -1 = 背面を持たない）。
int panel_backdrop_slot(int panel);

} // namespace hd2d
