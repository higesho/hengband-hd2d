/*!
 * @file nh_layout.cpp
 * @brief `nh_layout.h` の実装。
 */
#include "nethack/nh_layout.h"

#include <algorithm>

namespace hd2d::nethack {

namespace {

//! 原作の配置（メッセージ・ステータス・地図・右寄せの窓）。コマンドの欄を取ったあとの範囲で呼ぶ。
NhLayout compute_original(const NhLayoutInput &in)
{
    NhLayout out;
    if ((in.w <= 0) || (in.h <= 0) || (in.line_h <= 0)) {
        out.error = "窓の大きさか字の高さが 0 以下です。";
        return out;
    }
    const int status_rows = std::max(1, in.status_rows);
    const int status_h = status_rows * in.line_h;
    int msg_rows = std::max(1, in.msg_rows);
    // 窓が小さいときの保護（原作に無い）。地図が使える高さの 1/3 を割るあいだ、メッセージの行を減らす。
    while ((msg_rows > 1) && ((in.h - status_h - (msg_rows * in.line_h)) * 3 < in.h)) {
        --msg_rows;
    }
    const int msg_h = msg_rows * in.line_h;
    if (in.h < status_h + msg_h + in.line_h) {
        out.error = "窓の高さが、メッセージ・ステータス・地図の 1 行を並べるのに足りません。";
        return out;
    }
    out.msg_rows = msg_rows;
    out.status_rows = status_rows;

    // 先にステータスを下から、次にメッセージを上から切り出す（原作 `ALIGN_BOTTOM` → `ALIGN_TOP` の順）。
    out.status = NhRect{ in.x, in.y + in.h - status_h, in.w, status_h };
    out.message = NhRect{ in.x, in.y, in.w, msg_h };
    out.map_full = NhRect{ in.x, in.y + msg_h, in.w, in.h - status_h - msg_h };

    // 右寄せの窓: map_width/4 < menu_width < map_width*2/3（原作の式をそのまま）。
    const int menu_w = std::min((out.map_full.w * 2) / 3, std::max(out.map_full.w / 4, std::max(0, in.menu_content_w)));
    out.menu = NhRect{ out.map_full.right() - menu_w, out.map_full.y, menu_w, out.map_full.h };
    out.invent = out.menu;

    out.map = out.map_full;
    if (in.perm_invent) {
        out.map.w = out.menu.x - out.map.x;
    }
    out.ok = !out.map.empty();
    if (!out.ok) {
        out.error = "地図の区画の幅が 0 になりました。";
    }
    return out;
}

} // namespace

NhLayout nh_compute_layout(const NhLayoutInput &in)
{
    if ((in.cmdbar_count <= 0) || (in.cmdbar_cell <= 0) || (in.w <= 0) || (in.h <= 0)) {
        return compute_original(in);
    }
    NhLayoutInput rest = in;
    NhRect bar;
    int cols = 0;
    int rows = 0;
    const bool vertical = (in.h <= in.w); // 横長は右端に縦に並べる
    const int cell = in.cmdbar_cell;
    if (!vertical) {
        cols = std::max(1, in.w / cell);
        rows = (in.cmdbar_count + cols - 1) / cols;
        bar = NhRect{ in.x, in.y + in.h - (rows * cell), in.w, rows * cell };
        rest.h = in.h - bar.h;
    } else {
        rows = std::max(1, in.h / cell);
        cols = (in.cmdbar_count + rows - 1) / rows;
        bar = NhRect{ in.x + in.w - (cols * cell), in.y, cols * cell, in.h };
        rest.w = in.w - bar.w;
    }
    NhLayout out = compute_original(rest);
    if (!out.ok) {
        return out;
    }
    out.cmdbar = bar;
    out.cmdbar_cols = cols;
    out.cmdbar_rows = rows;
    out.cmdbar_vertical = vertical;
    return out;
}

int nh_font_px_for_window(int usable_w, int usable_h)
{
    (void)usable_w;
    const int px = (usable_h + 24) / 48; // 720 → 15、900 → 19、1080 → 23
    return std::clamp(px, 16, 32);
}

} // namespace hd2d::nethack
