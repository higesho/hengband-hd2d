/*!
 * @file nh_style.cpp
 * @brief `nh_style.h` の実装。
 */
#include "nethack/nh_style.h"

namespace hd2d::nethack {

namespace {

const NhRgb kColors[16] = {
    { 0.38f, 0.38f, 0.40f }, // CLR_BLACK（原作は 0x55。暗い下地で読める明るさ）
    { 1.00f, 0.25f, 0.25f }, // CLR_RED
    { 0.20f, 0.72f, 0.28f }, // CLR_GREEN
    { 0.78f, 0.48f, 0.22f }, // CLR_BROWN
    { 0.40f, 0.45f, 1.00f }, // CLR_BLUE
    { 1.00f, 0.30f, 1.00f }, // CLR_MAGENTA
    { 0.20f, 0.90f, 0.90f }, // CLR_CYAN
    { 0.75f, 0.75f, 0.75f }, // CLR_GRAY
    { 0.93f, 0.94f, 0.96f }, // NO_COLOR（既定の字の色）
    { 1.00f, 0.65f, 0.00f }, // CLR_ORANGE
    { 0.25f, 1.00f, 0.25f }, // CLR_BRIGHT_GREEN
    { 1.00f, 1.00f, 0.25f }, // CLR_YELLOW
    { 0.30f, 0.72f, 1.00f }, // CLR_BRIGHT_BLUE
    { 1.00f, 0.55f, 1.00f }, // CLR_BRIGHT_MAGENTA
    { 0.55f, 1.00f, 1.00f }, // CLR_BRIGHT_CYAN
    { 1.00f, 1.00f, 1.00f }, // CLR_WHITE
};

} // namespace

NhRgb nh_rgb(int nhcolor)
{
    return kColors[((nhcolor >= 0) && (nhcolor < 16)) ? nhcolor : 8];
}

TextColor nh_text_color(int nhcolor, float alpha)
{
    const NhRgb c = nh_rgb(nhcolor);
    return TextColor{ c.r, c.g, c.b, alpha };
}

PaintColor nh_paint_color(int nhcolor, float alpha)
{
    const NhRgb c = nh_rgb(nhcolor);
    return PaintColor{ c.r, c.g, c.b, alpha };
}

} // namespace hd2d::nethack
