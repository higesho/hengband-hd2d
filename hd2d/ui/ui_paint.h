/*!
 * @file ui_paint.h
 * @brief UI の下敷き・枠・ゲージ（P8）— 色つきの四角を積んで 1 回で流す。
 *
 * ## 何のためのものか
 * `TextOverlay` は文字しか描けない。パネルの下敷き・枠・選択枠・HP のゲージ・
 * ミニマップの点は**色つきの四角**であり、それを置く場所がここである。
 *
 * ## 作り
 * `TextOverlay` と同じ形にしてある（画素座標・左上原点・1 フレームぶんを積んで 1 draw call）。
 * 違いはテクスチャを持たないことだけ。**文字より先に流す**こと（下敷きなので）。
 *
 * @note **ポスト処理は通らない。**合成の後、既定のフレームバッファへ直に描く
 * （`post_process.h` の順序の表。通すと文字も枠もにじんでぼける）。
 */
#pragma once

#include "render/gl_core.h"
#include "ui/ui_layout.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hd2d {

//! 色（0〜1）。`TextOverlay` の `TextColor` と同じ並び。
struct PaintColor {
    float r{ 1.f };
    float g{ 1.f };
    float b{ 1.f };
    float a{ 1.f };
};

//! `TERM_COLOR`（0〜15）から。α は呼び出し側が決める。
PaintColor paint_from_term_color(std::uint8_t color_index, float alpha = 1.f);

/*!
 * @brief パネルの見た目。**3 つの作りで下敷きの濃さだけが変わる。**
 * @details 重なる作り（`Full` / `Hybrid`）では下が 3D なので透ける下敷きにし、
 * 枠の中に収まる作り（`Split`）では不透明にする。**同じ描画関数に濃さを渡す**形にして、
 * 作りごとに別の描き方を持たないようにしてある（増えると必ず食い違う）。
 */
struct PanelStyle {
    PaintColor fill{ 0.05f, 0.06f, 0.09f, 0.92f };
    PaintColor border{ 0.35f, 0.40f, 0.50f, 0.85f };
    int border_px{ 1 };
};

//! 重なる板（3D の上）の既定。
PanelStyle overlay_panel_style();
//! 枠の中に収まる板の既定。
PanelStyle solid_panel_style();

/*!
 * @name 板の下敷きを出さない（**VR**。2026-08-14 に決めた「VR モードの場合は
 * パネル背景は無し。背景は透過させて」）
 *
 * @details VR では文字 UI が空中の板に載る。そこへ下敷きを塗ると、
 * **世界の前に不透明な壁が浮く**ことになる。字だけ浮かせたいので、
 * 塗りの α を 0 にする——**決めるのはここ 1 か所**（`overlay_panel_style()` と
 * `solid_panel_style()` を通らない塗りは、そもそも下敷きではない）。
 * 枠線は残す（どこまでが 1 枚か分からなくなるため）。
 * @{
 */
void set_panel_transparent(bool on);
bool panel_transparent();
/*! @} */

class UiPaint {
public:
    bool init(std::string &err);
    void shutdown();

    //! このフレームの積み上げを空にする。画面の実寸を渡す（正射影の分母）。
    void begin(int screen_w, int screen_h);

    //! 塗りつぶした四角。
    void rect(const RectPx &r, const PaintColor &color);
    //! 枠だけ（内側は塗らない）。
    void frame(const RectPx &r, const PaintColor &color, int thickness = 1);
    /*!
     * @brief 三角 1 枚（画素座標・頂点 3 つ）。
     * @details 四角しか無かった所へ足したもの。**向きのある印**（ミニマップの自分）に要る
     * ——四角では「どちらを向いているか」を出しようが無い（2026-08-11 に決めた）。
     * 巻き方向は問わない（この描画は面の裏表を見ない）。
     */
    void triangle(float x0, float y0, float x1, float y1, float x2, float y2, const PaintColor &color);
    //! 下敷き＋枠。パネル 1 枚はこれで足りる。
    void panel(const RectPx &r, const PanelStyle &style);
    /*!
     * @brief ゲージ（HP・SP など）。
     * @param ratio 0〜1。範囲外は丸める。
     * @details 下地を必ず描く（0 のときに「何も無い」のと区別がつかなくなる）。
     */
    void gauge(const RectPx &r, float ratio, const PaintColor &fill, const PaintColor &back);

    //! 積んだものを 1 回で流す。**文字より先に呼ぶこと。**
    void flush();

private:
    struct Vertex {
        float x{};
        float y{};
        float r{};
        float g{};
        float b{};
        float a{};
    };

    void push_rect(float x0, float y0, float x1, float y1, const PaintColor &color);

    gl::GLuint program_{ 0 };
    gl::GLuint vao_{ 0 };
    gl::GLuint vbo_{ 0 };
    gl::GLint loc_screen_{ -1 };
    int screen_w_{ 0 };
    int screen_h_{ 0 };
    std::size_t vbo_capacity_{ 0 };
    std::vector<Vertex> vertices_;
};

} // namespace hd2d
