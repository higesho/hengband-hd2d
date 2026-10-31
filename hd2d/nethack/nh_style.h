/*!
 * @file nh_style.h
 * @brief NetHack の専用の画面が使う色（原作の 16 色）と、窓の下地の色。区画と窓の部品で共有する。
 *
 * 暗い下地で読める値にしてある。原作の GUI 版（`mswproc.c` の `regMapColors`）に近く、黒と青と緑だけ下地で沈まないよう明るくした。
 */
#pragma once

#include "render/text_overlay.h"
#include "ui/ui_paint.h"

namespace hd2d::nethack {

struct NhRgb {
    float r;
    float g;
    float b;
};

//! 窓の下地（`solid_panel_style` の塗りと同じ）。
inline constexpr NhRgb kNhTextBg{ 0.06f, 0.07f, 0.10f };

//! 原作の色の番号（0〜15。範囲外は「色の指定なし」の 8）→ RGB。
NhRgb nh_rgb(int nhcolor);
TextColor nh_text_color(int nhcolor, float alpha = 1.f);
PaintColor nh_paint_color(int nhcolor, float alpha = 1.f);

} // namespace hd2d::nethack
