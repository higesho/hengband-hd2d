// CPU-only test of the NetHack screen layout: `hd2d/nethack/nh_layout.{h,cpp}` .
//
// Build and run (the bundled LLVM-MinGW; SDL and GL are not needed):
//   core-import\toolchain\bin\clang++.exe --target=i686-w64-windows-gnu -std=c++20 -static -Wall -Wextra -Wshadow ^
//     -Wno-missing-field-initializers -I hd2d ^
//     tools/hd2d_verify/test_nethack_layout.cpp hd2d/nethack/nh_layout.cpp ^
//     -o test_nethack_layout.exe
//   test_nethack_layout.exe
//
// What it pins down (the expected numbers come from the formulas of the original `mswin_layout_main_window`
// (win/win32/mhmain.c:582-786) worked out by hand, not from the code under test):
//   - message window on top (rows x line height), status window at the bottom (2 rows), the map gets the rest;
//   - the right-aligned menu window: min(map_w * 2/3, max(map_w / 4, content_w)); perm_invent shrinks the map to its left edge;
//   - the same shapes at 1280x720, 1600x900 (with an origin), 1920x1080 and a portrait window;
//   - the small-window protection (fewer message rows when the map would drop below 1/3 of the height) and the rejection of bad input;
//   - the font size by window height.
#include "nethack/nh_layout.h"

#include <iostream>
#include <string>

using namespace hd2d::nethack;

namespace {

int g_checks = 0;
int g_failed = 0;

void check(bool ok, const std::string &what)
{
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::cerr << "FAILED: " << what << std::endl;
    }
}

void check_eq(long long actual, long long expected, const std::string &what)
{
    ++g_checks;
    if (actual != expected) {
        ++g_failed;
        std::cerr << "FAILED: " << what << ": expected " << expected << ", got " << actual << std::endl;
    }
}

void check_rect(const NhRect &r, int x, int y, int w, int h, const std::string &what)
{
    ++g_checks;
    if (!(r == NhRect{ x, y, w, h })) {
        ++g_failed;
        std::cerr << "FAILED: " << what << ": expected {" << x << "," << y << "," << w << "," << h << "}, got {" << r.x << "," << r.y << "," << r.w
                  << "," << r.h << "}" << std::endl;
    }
}

NhLayoutInput input(int x, int y, int w, int h, int line_h)
{
    NhLayoutInput in;
    in.x = x;
    in.y = y;
    in.w = w;
    in.h = h;
    in.line_h = line_h;
    return in;
}

//! どの大きさでも成り立つ性質: 3 つの帯が重ならず使える範囲を過不足なく埋める。
void check_tiling(const NhLayoutInput &in, const NhLayout &l, const std::string &tag)
{
    check(l.ok, tag + ": ok");
    check_eq(l.message.h + l.map_full.h + l.status.h, in.h, tag + ": heights add up");
    check_eq(l.message.y, in.y, tag + ": message at the top");
    check_eq(l.map_full.y, l.message.bottom(), tag + ": map under the message");
    check_eq(l.status.y, l.map_full.bottom(), tag + ": status under the map");
    check_eq(l.status.bottom(), in.y + in.h, tag + ": status at the bottom");
    check_eq(l.message.w, in.w, tag + ": message full width");
    check_eq(l.status.w, in.w, tag + ": status full width");
    check_eq(l.map_full.w, in.w, tag + ": map full width");
    check_eq(l.menu.right(), l.map_full.right(), tag + ": menu on the right edge");
    check_eq(l.menu.h, l.map_full.h, tag + ": menu as tall as the map");
    check(l.menu == l.invent, tag + ": invent equals menu");
}

} // namespace

int main()
{
    // ---- 1280x720, line height 18（原作の画面で測った 18 画素 x 4 行 / 2 行と同じ形） ----
    {
        const NhLayoutInput in = input(0, 0, 1280, 720, 18);
        const NhLayout l = nh_compute_layout(in);
        check_tiling(in, l, "1280x720");
        check_eq(l.msg_rows, 4, "1280x720: 4 message rows");
        check_eq(l.status_rows, 2, "1280x720: 2 status rows");
        check_rect(l.message, 0, 0, 1280, 72, "1280x720: message");
        check_rect(l.status, 0, 684, 1280, 36, "1280x720: status");
        check_rect(l.map, 0, 72, 1280, 612, "1280x720: map");
        check_rect(l.map_full, 0, 72, 1280, 612, "1280x720: map_full");
        // 幅 = min(1280*2/3 = 853, max(1280/4 = 320, 0)) = 320
        check_rect(l.menu, 960, 72, 320, 612, "1280x720: menu is a quarter of the map");
    }
    // ---- 右寄せの窓の幅の式（中身の幅で 1/4 〜 2/3 に収まる） ----
    {
        NhLayoutInput in = input(0, 0, 1280, 720, 18);
        in.menu_content_w = 500;
        check_rect(nh_compute_layout(in).menu, 780, 72, 500, 612, "menu content 500 -> 500 wide");
        in.menu_content_w = 100;
        check_rect(nh_compute_layout(in).menu, 960, 72, 320, 612, "menu content 100 -> clamped to a quarter");
        in.menu_content_w = 1000;
        check_rect(nh_compute_layout(in).menu, 427, 72, 853, 612, "menu content 1000 -> clamped to two thirds");
    }
    // ---- perm_invent: 地図が持ち物の窓の左端までに縮む。全面の矩形は縮まない ----
    {
        NhLayoutInput in = input(0, 0, 1280, 720, 18);
        in.perm_invent = true;
        const NhLayout l = nh_compute_layout(in);
        check_tiling(in, l, "perm_invent");
        check_rect(l.map, 0, 72, 960, 612, "perm_invent: map shrinks to the invent window");
        check_rect(l.map_full, 0, 72, 1280, 612, "perm_invent: map_full stays");
        check_rect(l.invent, 960, 72, 320, 612, "perm_invent: invent window");
        check_eq(l.map.right(), l.invent.x, "perm_invent: map touches invent");
    }
    // ---- 1600x900、使える範囲の左上が (10,20)・高さ 860（下の案内の帯を引いた分）、行の高さ 20 ----
    {
        const NhLayoutInput in = input(10, 20, 1600, 860, 20);
        const NhLayout l = nh_compute_layout(in);
        check_tiling(in, l, "1600x900");
        check_rect(l.message, 10, 20, 1600, 80, "1600x900: message");
        check_rect(l.status, 10, 840, 1600, 40, "1600x900: status");
        check_rect(l.map, 10, 100, 1600, 740, "1600x900: map");
        check_rect(l.menu, 1210, 100, 400, 740, "1600x900: menu is a quarter (400)");
    }
    // ---- 1920x1080、行の高さ 24 ----
    {
        const NhLayoutInput in = input(0, 0, 1920, 1080, 24);
        const NhLayout l = nh_compute_layout(in);
        check_tiling(in, l, "1920x1080");
        check_rect(l.message, 0, 0, 1920, 96, "1920x1080: message");
        check_rect(l.status, 0, 1032, 1920, 48, "1920x1080: status");
        check_rect(l.map, 0, 96, 1920, 936, "1920x1080: map");
        check_rect(l.menu, 1440, 96, 480, 936, "1920x1080: menu is a quarter (480)");
    }
    // ---- 縦長 900x1600、行の高さ 32 ----
    {
        const NhLayoutInput in = input(0, 0, 900, 1600, 32);
        const NhLayout l = nh_compute_layout(in);
        check_tiling(in, l, "900x1600");
        check_rect(l.message, 0, 0, 900, 128, "900x1600: message");
        check_rect(l.status, 0, 1536, 900, 64, "900x1600: status");
        check_rect(l.map, 0, 128, 900, 1408, "900x1600: map");
        check_rect(l.menu, 675, 128, 225, 1408, "900x1600: menu is a quarter (225)");
    }
    // ---- 行数を変えたとき（vary_msgcount） ----
    {
        NhLayoutInput in = input(0, 0, 1280, 720, 18);
        in.msg_rows = 6;
        const NhLayout l = nh_compute_layout(in);
        check_tiling(in, l, "6 rows");
        check_eq(l.msg_rows, 6, "6 rows: kept");
        check_rect(l.message, 0, 0, 1280, 108, "6 rows: message");
        check_rect(l.map, 0, 108, 1280, 576, "6 rows: map");
        in.msg_rows = 0;
        check_eq(nh_compute_layout(in).msg_rows, 1, "0 rows -> at least 1");
    }
    // ---- 小さい窓の保護: 地図が使える高さの 1/3 を割るとメッセージの行を減らす ----
    {
        NhLayoutInput in = input(0, 0, 400, 150, 18);
        const NhLayout l = nh_compute_layout(in);
        check_tiling(in, l, "400x150");
        // 4 行: (150-36-72)*3 = 126 < 150 → 3 行: (150-36-54)*3 = 180 >= 150 で止まる
        check_eq(l.msg_rows, 3, "400x150: message rows reduced to 3");
        check_rect(l.map, 0, 54, 400, 60, "400x150: map");
        in.h = 100;
        const NhLayout l2 = nh_compute_layout(in);
        // 3 行: (100-36-54)*3 = 30 < 100、2 行: (100-36-36)*3 = 84 < 100、1 行: (100-36-18)*3 = 138 >= 100
        check_eq(l2.msg_rows, 1, "400x100: message rows reduced to 1");
        check_rect(l2.map, 0, 18, 400, 46, "400x100: map");
    }
    // ---- 入らない・壊れた入力 ----
    {
        const NhLayout l = nh_compute_layout(input(0, 0, 400, 60, 18));
        check(!l.ok, "400x60: too small");
        check(!l.error.empty(), "400x60: error text");
        check(!nh_compute_layout(input(0, 0, 0, 720, 18)).ok, "width 0");
        check(!nh_compute_layout(input(0, 0, 1280, 0, 18)).ok, "height 0");
        check(!nh_compute_layout(input(0, 0, 1280, 720, 0)).ok, "line height 0");
        NhLayoutInput in = input(0, 0, 1280, 720, 18);
        in.perm_invent = true;
        in.menu_content_w = 100000; // 2/3 に抑えられるので地図は残る
        const NhLayout wide = nh_compute_layout(in);
        check(wide.ok, "huge content width still leaves the map");
        check_eq(wide.map.w, 1280 - 853, "huge content width: map is one third");
    }
    // ---- 字の大きさ ----
    check_eq(nh_font_px_for_window(1280, 720), 16, "font px at 720 (clamped up)");
    check_eq(nh_font_px_for_window(1600, 900), 19, "font px at 900");
    check_eq(nh_font_px_for_window(1920, 1080), 23, "font px at 1080");
    check_eq(nh_font_px_for_window(900, 1600), 32, "font px at 1600 (clamped down)");
    check_eq(nh_font_px_for_window(300, 200), 16, "font px at 200");

    std::cout << "test_nethack_layout: " << g_checks << " checks, " << g_failed << " failed" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
