// CPU-only test of the DCSS screen layout: `hd2d/dcss/dcss_layout.{h,cpp}` .
//
// Build and run (the bundled LLVM-MinGW; SDL is not needed):
//   core-import\toolchain\bin\clang++.exe --target=i686-w64-windows-gnu -std=c++20 -static -Wall -Wextra -Wshadow ^
//     -Wno-missing-field-initializers -I hd2d ^
//     tools/hd2d_verify/test_dcss_layout.cpp hd2d/dcss/dcss_layout.cpp ^
//     -o test_dcss_layout.exe
//   test_dcss_layout.exe
//
// What it pins down (expected values come from the formulas of the original `TilesFramework::do_layout`, not from
// the code under test):
//   - the table of font sizes by window size (`_screen_sizes`);
//   - the whole layout for windows 1920x1080, 1920x994 (measured on the original), 1920x900/768/720, 1500x700, and
//     1600x600 / 1600x480 where the message area becomes an overlay / the tiles shrink. The character width is the font
//     size (1 em), which is what was measured on the original tile version; one row keeps the half-em input;
//   - how the state the original keeps between layouts (which tab headings are still in the tab area) changes the result;
//   - fixed tabs for spells and abilities, and the intended (non-quirk) rows for the monster tab;
//   - the small layout (chosen automatically at 1280x720 with 1 em characters, and forced at 800x600);
//   - rejection of bad input.
//
// How the expected numbers were made: the widths and heights that were measured on the original (sidebar 694, 1920x994
// view 1222x854, message rows 10 / 7, small layout below 1490) are checked here; the other rows were computed with a
// separate script that writes the original formulas as flat arithmetic (no region objects), and the small-layout rows
// were traced by hand.
#include "dcss/dcss_layout.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace hd2d::dcss;

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

struct R {
    int x, y, w, h, cols, rows;
};
struct R4 {
    int x, y, w, h;
};
struct Fx {
    int tab;
    R r;
};

using Enabled = std::array<bool, kTabCount>;

struct Case {
    const char *name;
    int W, H;
    bool full_width; // true: character width = font size (1 em). false: half of the font size.
    int spells, abilities;
    bool quirk;
    bool force_overlay;
    Enabled enabled_in;
    DcssSmallLayoutMode mode;
    bool small;
    bool overlay;
    int div_x, div_y, sidebar;
    R dungeon;
    int cell, view_w, view_h;
    R msg, stat;
    bool has_map;
    R4 map;
    int map_cell;
    R tabs;
    std::vector<Fx> fixed;
    Enabled enabled_out;
};

DcssFontMetrics font(int px, bool full_width)
{
    DcssFontMetrics f;
    f.size_px = px;
    f.char_w = full_width ? px : px / 2;
    f.char_h = px;
    return f;
}

DcssLayoutInput make_input(const Case &c)
{
    const DcssScreenProfile p = dcss_screen_profile(c.W, c.H);
    DcssLayoutInput in;
    in.window_w = c.W;
    in.window_h = c.H;
    in.fonts.crt = font(p.crt_px, c.full_width);
    in.fonts.msg = font(p.msg_px, c.full_width);
    in.fonts.stat = font(p.stat_px, c.full_width);
    in.fonts.tip = font(p.tip_px, c.full_width);
    in.fonts.lbl = font(p.lbl_px, c.full_width);
    in.spell_count = c.spells;
    in.ability_count = c.abilities;
    in.unassigned_tab_ids_are_zero = c.quirk;
    in.force_overlay = c.force_overlay;
    in.tab_enabled = c.enabled_in;
    in.small_layout_mode = c.mode;
    return in;
}

void check_region(const std::string &name, const DcssRegion &r, const R &e)
{
    check_eq(r.x, e.x, name + ".x");
    check_eq(r.y, e.y, name + ".y");
    check_eq(r.w, e.w, name + ".w");
    check_eq(r.h, e.h, name + ".h");
    check_eq(r.cols, e.cols, name + ".cols");
    check_eq(r.rows, e.rows, name + ".rows");
}

void run_case(const Case &c)
{
    const std::string n = c.name;
    const DcssLayoutInput in = make_input(c);
    const DcssLayout out = DcssLayout::compute(in);
    check(out.ok, n + ": ok (" + out.error + ")");
    if (!out.ok) {
        return;
    }
    check_eq(out.small_layout, c.small, n + ": small_layout");
    check_eq(out.message_overlay, c.overlay, n + ": message_overlay");
    check_eq(out.stat_x_divider, c.div_x, n + ": stat_x_divider");
    check_eq(out.message_y_divider, c.div_y, n + ": message_y_divider");
    check_eq(out.sidebar_width, c.sidebar, n + ": sidebar_width");
    check_region(n + ": dungeon", out.dungeon, c.dungeon);
    check_eq(out.dungeon.cell_w, c.cell, n + ": dungeon.cell_w");
    check_eq(out.dungeon.cell_h, c.cell, n + ": dungeon.cell_h");
    check_eq(out.dungeon_view_w, c.view_w, n + ": dungeon_view_w");
    check_eq(out.dungeon_view_h, c.view_h, n + ": dungeon_view_h");
    check_region(n + ": message", out.message, c.msg);
    check_region(n + ": stat", out.stat, c.stat);
    check_eq(out.has_minimap, c.has_map, n + ": has_minimap");
    if (c.has_map && out.has_minimap) {
        check_region(n + ": minimap", out.minimap, R{ c.map.x, c.map.y, c.map.w, c.map.h, kMapGridCols, kMapGridRows });
        check_eq(out.minimap.cell_w, c.map_cell, n + ": minimap.cell_w");
        check_eq(out.minimap.cell_h, c.map_cell, n + ": minimap.cell_h");
    }
    check_region(n + ": tabs", out.tabs, c.tabs);
    check_eq(static_cast<long long>(out.fixed_tabs.size()), static_cast<long long>(c.fixed.size()), n + ": fixed_tabs.size");
    for (size_t i = 0; i < out.fixed_tabs.size() && i < c.fixed.size(); ++i) {
        const std::string fn = n + ": fixed[" + std::to_string(i) + "]";
        check_eq(out.fixed_tabs[i].tab_index, c.fixed[i].tab, fn + ".tab");
        check_region(fn, out.fixed_tabs[i].region, c.fixed[i].r);
    }
    for (int i = 0; i < kTabCount; ++i) {
        check_eq(out.tab_enabled[i], c.enabled_out[i], n + ": tab_enabled[" + std::to_string(i) + "]");
    }
    // 全画面の文字画面: 窓いっぱいを crt 字の桁・行で割る。
    check_region(n + ": crt", out.crt,
                 R{ 0, 0, (c.W / in.fonts.crt.char_w) * in.fonts.crt.char_w, (c.H / in.fonts.crt.char_h) * in.fonts.crt.char_h,
                    c.W / in.fonts.crt.char_w, c.H / in.fonts.crt.char_h });
    // 同じ入力から同じ結果。
    const DcssLayout again = DcssLayout::compute(in);
    check(again.ok == out.ok && again.dungeon.x == out.dungeon.x && again.tabs.y == out.tabs.y && again.fixed_tabs.size() == out.fixed_tabs.size(),
          n + ": deterministic");
}

const Case kCases[] = {
    // 字の格子の 1 桁は、原作の実測（別の担当が手元のタイル版を撮って測った）で最大送り幅 = 1 em（字の大きさと同じ）。
    // ステータス 16 px・メッセージ 14 px なら格子は 16 px・14 px。以下の表は特に断りが無い限りこの前提（full_width = true）。
    //
    // 1920x994（実測と一致を確かめた行）: サイドバーは 14*32-10 = 438（1920/3 = 640 以下）に 32 ずつ足して
    //   42 桁 * 16 = 672 に届く 694。縦線 x = 1920-694-4 = 1222。ダンジョンの上限 21*32 = 672 + メッセージ最小 98 <= 994 なので
    //   境目 y = max(672, 994-140) = 854。区画は (round_up(1222,64)+32) x (round_up(854,64)+32) = 1312 x 928 = 41 x 29 マス。
    //   メッセージは窓の高さ 900 以上で 10 行、720〜768 で 7 行（N2・N3・N4）。
    //
    // 1280x720 は字の幅が 1 em だと 窓幅 < 16*45 + 14*55 = 1490 で小さい配置になる（下の E）。
    // 半角が 0.5 em だった場合の入力（A）は、関数が字の幅を入力として受けていることの確かめとして 1 行だけ残した。
    //   A の手計算: サイドバー 214 → 42 桁 * 8 = 336 に届くまで 32 ずつ足して 342、縦線 x = 934、境目 y = 622、
    //   区画 992 x 672（左上 (-29, -25)）、見出しの欄は 10 列で、持ち物の欄は 1 回目が 8 行、ミニマップ（3 画素 * 70 + 4）を
    //   置いたあとの 2 回目は見出しの高さ 9*21-1 = 188 → 6 行（y = 528）、命令の固定表示（2 行）は y = 446。
    { "A_1280x720_half_width_input", 1280, 720, false, 0, 0, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ false, /*div*/ 934, 622, /*sidebar*/ 342,
      /*dungeon*/ { -29, -25, 992, 672, 31, 21 }, /*cell*/ 32, /*view*/ 934, 622,
      /*msg*/ { 0, 622, 931, 98, 133, 7 }, /*stat*/ { 938, 0, 336, 208, 42, 13 },
      /*map*/ true, { 938, 208, 336, 220 }, 3,
      /*tabs*/ { 938, 528, 360, 192, 10, 6 },
      /*fixed*/ { { 6, { 938, 446, 360, 64, 10, 2 } } },
      /*enabled out*/ { true, true, true, true, true, true, false, true, true } },
    { "B_1920x1080", 1920, 1080, true, 0, 0, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ false, /*div*/ 1222, 940, /*sidebar*/ 694,
      /*dungeon*/ { -45, -26, 1312, 992, 41, 31 }, /*cell*/ 32, /*view*/ 1222, 940,
      /*msg*/ { 0, 940, 1218, 140, 87, 10 }, /*stat*/ { 1226, 0, 688, 208, 43, 13 },
      /*map*/ true, { 1226, 208, 688, 498 }, 7,
      /*tabs*/ { 1226, 824, 712, 256, 21, 8 },
      /*fixed*/ { { 6, { 1226, 774, 712, 32, 21, 1 } }, { 4, { 1226, 724, 712, 32, 21, 1 } } },
      /*enabled out*/ { true, true, true, true, false, true, false, true, true } },
    { "N_1920x994_measured", 1920, 994, true, 0, 0, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ false, /*div*/ 1222, 854, /*sidebar*/ 694,
      /*dungeon*/ { -45, -37, 1312, 928, 41, 29 }, /*cell*/ 32, /*view*/ 1222, 854,
      /*msg*/ { 0, 854, 1218, 140, 87, 10 }, /*stat*/ { 1226, 0, 688, 208, 43, 13 },
      /*map*/ true, { 1226, 208, 688, 412 }, 5,
      /*tabs*/ { 1226, 738, 712, 256, 21, 8 },
      /*fixed*/ { { 6, { 1226, 688, 712, 32, 21, 1 } }, { 4, { 1226, 638, 712, 32, 21, 1 } } },
      /*enabled out*/ { true, true, true, true, false, true, false, true, true } },
    { "N2_1920x900", 1920, 900, true, 0, 0, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ false, /*div*/ 1222, 760, /*sidebar*/ 694,
      /*dungeon*/ { -45, -20, 1312, 800, 41, 25 }, /*cell*/ 32, /*view*/ 1222, 760,
      /*msg*/ { 0, 760, 1218, 140, 87, 10 }, /*stat*/ { 1226, 0, 688, 208, 43, 13 },
      /*map*/ true, { 1226, 208, 688, 318 }, 4,
      /*tabs*/ { 1226, 644, 712, 256, 21, 8 },
      /*fixed*/ { { 6, { 1226, 594, 712, 32, 21, 1 } }, { 4, { 1226, 544, 712, 32, 21, 1 } } },
      /*enabled out*/ { true, true, true, true, false, true, false, true, true } },
    { "N3_1920x768", 1920, 768, true, 0, 0, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ false, /*div*/ 1222, 670, /*sidebar*/ 694,
      /*dungeon*/ { -45, -33, 1312, 736, 41, 23 }, /*cell*/ 32, /*view*/ 1222, 670,
      /*msg*/ { 0, 670, 1218, 98, 87, 7 }, /*stat*/ { 1226, 0, 688, 208, 43, 13 },
      /*map*/ true, { 1226, 208, 688, 250 }, 3,
      /*tabs*/ { 1226, 576, 712, 192, 21, 6 },
      /*fixed*/ { { 6, { 1226, 526, 712, 32, 21, 1 } }, { 4, { 1226, 476, 712, 32, 21, 1 } } },
      /*enabled out*/ { true, true, true, true, false, true, false, true, true } },
    { "N4_1920x720", 1920, 720, true, 0, 0, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ false, /*div*/ 1222, 622, /*sidebar*/ 694,
      /*dungeon*/ { -45, -25, 1312, 672, 41, 21 }, /*cell*/ 32, /*view*/ 1222, 622,
      /*msg*/ { 0, 622, 1218, 98, 87, 7 }, /*stat*/ { 1226, 0, 688, 208, 43, 13 },
      /*map*/ true, { 1226, 208, 688, 252 }, 3,
      /*tabs*/ { 1226, 528, 712, 192, 21, 6 },
      /*fixed*/ { { 6, { 1226, 478, 712, 32, 21, 1 } } },
      /*enabled out*/ { true, true, true, true, true, true, false, true, true } },
    { "N5_1920x720_tabs_state", 1920, 720, true, 0, 0, true, false, { true, false, false, false, false, false, false, false, false }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ false, /*div*/ 1222, 622, /*sidebar*/ 694,
      /*dungeon*/ { -45, -25, 1312, 672, 41, 21 }, /*cell*/ 32, /*view*/ 1222, 622,
      /*msg*/ { 0, 622, 1218, 98, 87, 7 }, /*stat*/ { 1226, 0, 688, 208, 43, 13 },
      /*map*/ true, { 1226, 208, 688, 266 }, 3,
      /*tabs*/ { 1226, 592, 712, 128, 21, 4 },
      /*fixed*/ { { 6, { 1226, 542, 712, 32, 21, 1 } }, { 4, { 1226, 492, 712, 32, 21, 1 } } },
      /*enabled out*/ { true, true, false, true, false, false, false, false, false } },
    { "P_1500x700", 1500, 700, true, 0, 0, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ false, /*div*/ 802, 602, /*sidebar*/ 694,
      /*dungeon*/ { -31, -35, 864, 672, 27, 21 }, /*cell*/ 32, /*view*/ 802, 602,
      /*msg*/ { 0, 602, 798, 98, 57, 7 }, /*stat*/ { 806, 0, 688, 208, 43, 13 },
      /*map*/ false, { 0, 0, 0, 0 }, 0,
      /*tabs*/ { 806, 444, 712, 256, 21, 8 },
      /*fixed*/ { { 6, { 806, 394, 712, 32, 21, 1 } }, { 4, { 806, 344, 712, 32, 21, 1 } } },
      /*enabled out*/ { true, true, true, true, false, true, false, true, true } },
    { "Q_1600x600_overlay", 1600, 600, true, 0, 0, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ true, /*div*/ 998, 600, /*sidebar*/ 598,
      /*dungeon*/ { -29, -36, 1056, 672, 33, 21 }, /*cell*/ 32, /*view*/ 998, 600,
      /*msg*/ { 0, 0, 996, 84, 83, 7 }, /*stat*/ { 1002, 0, 588, 182, 42, 13 },
      /*map*/ false, { 0, 0, 0, 0 }, 0,
      /*tabs*/ { 1002, 344, 616, 256, 18, 8 },
      /*fixed*/ { { 6, { 1002, 296, 616, 32, 18, 1 } }, { 4, { 1002, 248, 616, 32, 18, 1 } } },
      /*enabled out*/ { true, true, true, true, false, true, false, true, true } },
    { "R_1600x480_shrink", 1600, 480, true, 0, 0, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ true, /*div*/ 1062, 480, /*sidebar*/ 534,
      /*dungeon*/ { -15, -26, 1092, 532, 39, 19 }, /*cell*/ 28, /*view*/ 1062, 480,
      /*msg*/ { 0, 0, 1060, 70, 106, 7 }, /*stat*/ { 1066, 0, 528, 156, 44, 13 },
      /*map*/ false, { 0, 0, 0, 0 }, 0,
      /*tabs*/ { 1066, 288, 552, 192, 16, 6 },
      /*fixed*/ { { 6, { 1066, 241, 552, 32, 16, 1 } }, { 4, { 1066, 194, 552, 32, 16, 1 } } },
      /*enabled out*/ { true, true, true, true, false, true, false, true, true } },
    { "J_1920x1080_spells", 1920, 1080, true, 20, 5, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ false, /*div*/ 1222, 940, /*sidebar*/ 694,
      /*dungeon*/ { -45, -26, 1312, 992, 41, 31 }, /*cell*/ 32, /*view*/ 1222, 940,
      /*msg*/ { 0, 940, 1218, 140, 87, 10 }, /*stat*/ { 1226, 0, 688, 208, 43, 13 },
      /*map*/ true, { 1226, 208, 688, 398 }, 5,
      /*tabs*/ { 1226, 824, 712, 256, 21, 8 },
      /*fixed*/ { { 6, { 1226, 774, 712, 32, 21, 1 } }, { 1, { 1226, 724, 712, 32, 21, 1 } }, { 3, { 1226, 674, 712, 32, 21, 1 } }, { 4, { 1226, 624, 712, 32, 21, 1 } } },
      /*enabled out*/ { true, false, true, false, false, true, false, true, true } },
    { "J2_1920x1080_spells_state", 1920, 1080, true, 20, 5, true, false, { true, false, true, false, false, true, false, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ false, /*div*/ 1222, 940, /*sidebar*/ 694,
      /*dungeon*/ { -45, -26, 1312, 992, 41, 31 }, /*cell*/ 32, /*view*/ 1222, 940,
      /*msg*/ { 0, 940, 1218, 140, 87, 10 }, /*stat*/ { 1226, 0, 688, 208, 43, 13 },
      /*map*/ true, { 1226, 208, 688, 398 }, 5,
      /*tabs*/ { 1226, 824, 712, 256, 21, 8 },
      /*fixed*/ { { 6, { 1226, 774, 712, 32, 21, 1 } }, { 1, { 1226, 724, 712, 32, 21, 1 } }, { 3, { 1226, 674, 712, 32, 21, 1 } }, { 4, { 1226, 624, 712, 32, 21, 1 } } },
      /*enabled out*/ { true, false, true, false, false, true, false, true, true } },
    { "K_1920x1080_noquirk", 1920, 1080, true, 20, 5, false, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ false, /*div*/ 1222, 940, /*sidebar*/ 694,
      /*dungeon*/ { -45, -26, 1312, 992, 41, 31 }, /*cell*/ 32, /*view*/ 1222, 940,
      /*msg*/ { 0, 940, 1218, 140, 87, 10 }, /*stat*/ { 1226, 0, 688, 208, 43, 13 },
      /*map*/ true, { 1226, 208, 688, 334 }, 4,
      /*tabs*/ { 1226, 824, 712, 256, 21, 8 },
      /*fixed*/ { { 6, { 1226, 774, 712, 32, 21, 1 } }, { 1, { 1226, 724, 712, 32, 21, 1 } }, { 3, { 1226, 674, 712, 32, 21, 1 } }, { 4, { 1226, 560, 712, 96, 21, 3 } } },
      /*enabled out*/ { true, false, true, false, false, true, false, true, true } },
    { "M_1600x600_short", 1600, 600, true, 3, 2, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ true, /*div*/ 998, 600, /*sidebar*/ 598,
      /*dungeon*/ { -29, -36, 1056, 672, 33, 21 }, /*cell*/ 32, /*view*/ 998, 600,
      /*msg*/ { 0, 0, 996, 84, 83, 7 }, /*stat*/ { 1002, 0, 588, 182, 42, 13 },
      /*map*/ false, { 0, 0, 0, 0 }, 0,
      /*tabs*/ { 1002, 344, 616, 256, 18, 8 },
      /*fixed*/ { { 6, { 1002, 296, 616, 32, 18, 1 } }, { 1, { 1002, 248, 616, 32, 18, 1 } } },
      /*enabled out*/ { true, false, true, true, true, true, false, true, true } },
    { "L_1920x1080_force_overlay", 1920, 1080, true, 0, 0, true, true, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ false, /*overlay*/ true, /*div*/ 1222, 1080, /*sidebar*/ 694,
      /*dungeon*/ { -45, -20, 1312, 1120, 41, 35 }, /*cell*/ 32, /*view*/ 1222, 1080,
      /*msg*/ { 0, 0, 1218, 98, 87, 7 }, /*stat*/ { 1226, 0, 688, 208, 43, 13 },
      /*map*/ true, { 1226, 208, 688, 498 }, 7,
      /*tabs*/ { 1226, 824, 712, 256, 21, 8 },
      /*fixed*/ { { 6, { 1226, 774, 712, 32, 21, 1 } }, { 4, { 1226, 724, 712, 32, 21, 1 } } },
      /*enabled out*/ { true, true, true, true, false, true, false, true, true } },
};

const Case kSmallCases[] = {
    // 1280x720（原作の実測どおり 1 em）: 窓幅 < 16*45 + 14*55 = 1490 なので小さい配置が自動で選ばれる（1480 は小さい、1500 は通常）。
    //   見出しの拡大率 = ((32-1)*720/9/20 - 1)/2 = 61、サイドバー = 20*61/32 + 9*16 = 182、縦線 x = 1098。
    //   ステータスは最後に (1280-1098-38)/16 = 9 桁 x 720/16 = 45 行。見出しは (1098+40) x (622-18) に収めて
    //   x = 1280-20 = 1260 に置く: (1138-40)/61 = 18 列 x 604/61 = 9 行。
    { "E_1280x720_full_width_auto_small", 1280, 720, true, 0, 0, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
      /*small*/ true, /*overlay*/ false, /*div*/ 1098, 622, /*sidebar*/ 182,
      /*dungeon*/ { -43, -25, 1184, 672, 37, 21 }, /*cell*/ 32, /*view*/ 1098, 622,
      /*msg*/ { 0, 622, 1092, 98, 78, 7 }, /*stat*/ { 1098, 0, 144, 720, 9, 45 },
      /*map*/ false, { 0, 0, 0, 0 }, 0,
      /*tabs*/ { 1260, 0, 1138, 549, 18, 9 },
      /*fixed*/ {},
      /*enabled out*/ { true, true, true, true, true, true, true, true, true } },
    // 800x600 で小さい配置を強制: 見出しの拡大率 = ((32-1)*600/9/20 - 1)/2 = 51、サイドバー = 20*51/32 + 9*5 = 76、
    //   縦線 x = 724。視野 17 マスが入らない（(600-84)/32 = 16）のでメッセージは重ね表示。
    //   ステータスは (800-724-31)/5 = 9 桁 x 600/11 = 54 行。見出しは x = 780、(764-40)/51 = 14 列 x (600-16)/51 = 11 行。
    { "F_800x600_forced_small", 800, 600, false, 0, 0, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::On,
      /*small*/ true, /*overlay*/ true, /*div*/ 724, 600, /*sidebar*/ 76,
      /*dungeon*/ { -38, -36, 800, 672, 25, 21 }, /*cell*/ 32, /*view*/ 724, 600,
      /*msg*/ { 0, 0, 720, 84, 120, 7 }, /*stat*/ { 724, 0, 45, 594, 9, 54 },
      /*map*/ false, { 0, 0, 0, 0 }, 0,
      /*tabs*/ { 780, 0, 754, 561, 14, 11 },
      /*fixed*/ {},
      /*enabled out*/ { true, true, true, true, true, true, true, true, true } },
};

void test_screen_profile()
{
    struct P {
        int w, h, map, crt, stat, msg, tip, lbl;
    };
    // 原作 `_screen_sizes` の行を窓の大きさで引く。どれにも収まらなければ最後の行。
    const P rows[] = {
        { 1920, 1080, 3, 15, 16, 14, 15, 14 }, { 1280, 720, 3, 15, 16, 14, 15, 14 }, { 1024, 700, 3, 15, 16, 14, 15, 14 },
        { 1024, 699, 2, 14, 14, 12, 13, 12 }, { 1024, 600, 2, 14, 14, 12, 13, 12 }, { 1023, 600, 2, 14, 11, 12, 13, 12 },
        { 800, 600, 2, 14, 11, 12, 13, 12 }, { 800, 599, 2, 13, 12, 10, 13, 11 }, { 800, 480, 2, 13, 12, 10, 13, 11 },
        { 640, 400, 2, 13, 12, 10, 13, 11 },
    };
    for (const P &e : rows) {
        const std::string n = "profile " + std::to_string(e.w) + "x" + std::to_string(e.h);
        const DcssScreenProfile p = dcss_screen_profile(e.w, e.h);
        check_eq(p.map_px, e.map, n + " map");
        check_eq(p.crt_px, e.crt, n + " crt");
        check_eq(p.stat_px, e.stat, n + " stat");
        check_eq(p.msg_px, e.msg, n + " msg");
        check_eq(p.tip_px, e.tip, n + " tip");
        check_eq(p.lbl_px, e.lbl, n + " lbl");
    }
}

void test_options_and_errors()
{
    Case base{ "base", 1920, 1080, true, 0, 0, true, false, { true, true, true, true, true, true, true, true, true }, DcssSmallLayoutMode::Auto,
               false, false, 0, 0, 0, { 0, 0, 0, 0, 0, 0 }, 0, 0, 0, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, false, { 0, 0, 0, 0 }, 0,
               { 0, 0, 0, 0, 0, 0 }, {}, { true, true, true, true, true, true, true, true, true } };

    // 字の幅が 0、窓が 0、ラベルの大きさが 0 は不正。
    {
        DcssLayoutInput in = make_input(base);
        in.fonts.stat.char_w = 0;
        check(!DcssLayout::compute(in).ok, "zero stat char width is rejected");
        in = make_input(base);
        in.window_w = 0;
        check(!DcssLayout::compute(in).ok, "zero window width is rejected");
        in = make_input(base);
        in.fonts.lbl.size_px = 0;
        check(!DcssLayout::compute(in).ok, "zero label size is rejected");
        in = make_input(base);
        in.tile_viewport_scale_pct = 0;
        check(!DcssLayout::compute(in).ok, "zero scale is rejected");
        in = make_input(base);
        in.window_w = 640;
        in.window_h = 16; // 視野 17 マスを縮めると 1 画素未満
        check(!DcssLayout::compute(in).ok, "a window shorter than the view diameter is rejected");
    }
    // 全体地図の画面は tile_map_scale（0.6）を使う: 32*60/100 = 19 画素。視野 17 マスが収まるので重ね表示にならない。
    {
        DcssLayoutInput in = make_input(base);
        in.map_mode = true;
        const DcssLayout out = DcssLayout::compute(in);
        check(out.ok, "map mode computes");
        check_eq(out.dungeon.cell_w, 19, "map mode cell size is 32 * 0.6 truncated");
        // 上限 max(21,17)*19 = 399 と最小 7*14 = 98 が収まるので、境目 = max(399, 1080 - 140) = 940。
        check_eq(out.message_y_divider, 940, "map mode message divider");
        check(!out.message_overlay, "map mode has no overlay");
    }
    // 重ね表示の不透明度: 255 * 40 / 100 = 102。
    {
        DcssLayoutInput in = make_input(base);
        in.force_overlay = true;
        const DcssLayout out = DcssLayout::compute(in);
        check(out.ok && out.message_overlay, "force_overlay turns the overlay on");
        check_eq(out.overlay_alpha, 102, "overlay alpha is 255 * 40 / 100");
        check_eq(out.message_y_divider, 1080, "overlay puts the divider at the window bottom");
    }
    // 小さい配置になる境目（原作の実測: 窓幅 1490 未満。1480 は小さい、1500 は通常）。stat 16 px・msg 14 px、1 em。
    {
        const int widths[] = { 1480, 1489, 1490, 1500 };
        const bool small[] = { true, true, false, false };
        for (int i = 0; i < 4; ++i) {
            DcssLayoutInput in = make_input(base);
            in.window_w = widths[i];
            in.window_h = 994;
            const DcssLayout out = DcssLayout::compute(in);
            check(out.ok, "boundary width " + std::to_string(widths[i]) + " computes");
            check_eq(out.small_layout, small[i], "small layout at width " + std::to_string(widths[i]));
        }
    }
    // 優先順位: ミニマップを外すと置かれない。"inventory" だけでも持ち物の欄は決まる。
    {
        DcssLayoutInput in = make_input(base);
        in.layout_priority = { "inventory" };
        const DcssLayout out = DcssLayout::compute(in);
        check(out.ok && !out.has_minimap && out.fixed_tabs.empty(), "no minimap and no fixed tab without them in the priority");
        // 名前の照合: 複数形・大文字小文字・未知の名前（無視する）。
        in.layout_priority = { "Commands", "unknown_thing" };
        const DcssLayout out2 = DcssLayout::compute(in);
        check(out2.ok && out2.fixed_tabs.size() == 1 && out2.fixed_tabs[0].tab_index == kTabCommand, "tab names match case-insensitively and in the plural");
    }
    // 呪文の数が多いと固定表示の行も増える。1920x1080 の J と同じ入力は表の中にある。
}

} // namespace

int main()
{
    test_screen_profile();
    for (const Case &c : kCases) {
        run_case(c);
    }
    for (const Case &c : kSmallCases) {
        run_case(c);
    }
    test_options_and_errors();

    if (g_failed != 0) {
        std::cerr << g_failed << " of " << g_checks << " checks failed" << std::endl;
        return 1;
    }
    std::cout << "OK " << g_checks << " checks" << std::endl;
    return 0;
}
