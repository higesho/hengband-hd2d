/*!
 * @file sdl_ui_options.cpp
 * @brief SDL2 UI 共通オプション実装
 */
#include "frame/sdl_ui_options.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

namespace {

SdlUiOptions g_opts{};
SdlUiOptionsApplyFn g_apply_hook = nullptr;

int clamp_index(int v, int n)
{
    if (n <= 0) {
        return 0;
    }
    if (v < 0) {
        return 0;
    }
    if (v >= n) {
        return n - 1;
    }
    return v;
}

} // namespace

/*!
 * @note 並びは**小さい順**。0・1（1/4・1/3）は 2026-07-31 に足した段で、
 * 8px／16px タイル版やアスキーアート版で地図を広く見るためのもの。
 * 1/4 は基準 64px に対して 16px＝`kMapCellAbsMinPx` そのもの（これ以上は縮まない）。
 */
int SdlUiOptions::tile_scale_num(int index)
{
    switch (clamp_index(index, kTileScaleCount)) {
    case 0:
        return 1; // 1/4
    case 1:
        return 1; // 1/3
    case 2:
        return 1; // 1/2
    case 3:
        return 1; // 1
    case 4:
        return 3; // 3/2
    case 5:
        return 2; // 2/1
    default:
        return 1;
    }
}

int SdlUiOptions::tile_scale_den(int index)
{
    switch (clamp_index(index, kTileScaleCount)) {
    case 0:
        return 4;
    case 1:
        return 3;
    case 2:
        return 2;
    case 3:
        return 1;
    case 4:
        return 2;
    case 5:
        return 1;
    default:
        return 1;
    }
}

const char *SdlUiOptions::tile_scale_label(int index)
{
    switch (clamp_index(index, kTileScaleCount)) {
    case 0:
        return "1/4";
    case 1:
        return "1/3";
    case 2:
        return "1/2";
    case 3:
        return "1";
    case 4:
        return "1.5";
    case 5:
        return "2";
    default:
        return "1";
    }
}

namespace {
//! 1 行動あたりの秒数。既定は index 4 の 1.0 秒。
constexpr double kRealtimeSeconds[SdlUiOptions::kRealtimeSpeedCount] = {
    0.2, 0.3, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0
};
constexpr const char *kRealtimeLabels[SdlUiOptions::kRealtimeSpeedCount] = {
    "0.2 秒/行動", "0.3 秒/行動", "0.5 秒/行動", "0.75 秒/行動",
    "1.0 秒/行動", "1.5 秒/行動", "2.0 秒/行動", "3.0 秒/行動"
};
}

namespace {
//! 振れ幅の段。**1.0 は「自分の拍は絶対に変わらない」**（速度差は全部世界へ）。
constexpr double kRealtimeSelfSpans[SdlUiOptions::kRealtimeSelfSpanCount] = { 1.0, 2.0, 3.0, 5.0, 100.0 };
constexpr const char *kRealtimeSelfSpanLabels[SdlUiOptions::kRealtimeSelfSpanCount] = {
    "1.0 倍（自分の拍は不変・世界が伸縮）",
    "2.0 倍まで",
    "3.0 倍まで",
    "5.0 倍まで",
    "制限なし（自分が全部速くなる）",
};
}

double SdlUiOptions::realtime_self_span(int index)
{
    return kRealtimeSelfSpans[clamp_index(index, kRealtimeSelfSpanCount)];
}

const char *SdlUiOptions::realtime_self_span_label(int index)
{
    return kRealtimeSelfSpanLabels[clamp_index(index, kRealtimeSelfSpanCount)];
}

double SdlUiOptions::realtime_seconds_per_turn(int index)
{
    return kRealtimeSeconds[clamp_index(index, kRealtimeSpeedCount)];
}

const char *SdlUiOptions::realtime_speed_label(int index)
{
    return kRealtimeLabels[clamp_index(index, kRealtimeSpeedCount)];
}

const char *SdlUiOptions::map_style_label(int index)
{
    switch (clamp_index(index, kMapStyleCount)) {
    case 0:
        return "HD タイル";
    case 1:
        return "16px タイル";
    case 2:
        return "8px タイル";
    case 3:
        return "アスキーアート";
    default:
        return "HD タイル";
    }
}

int SdlUiOptions::map_style_graf_px(int index)
{
    switch (clamp_index(index, kMapStyleCount)) {
    case 1:
        return 16; // lib/xtra/graf/16x16.bmp（mask.bmp あり）
    case 2:
        return 8; // lib/xtra/graf/8x8.bmp（マスク無し＝不透明）
    default:
        return 0; // コアのグラフィックモードは使わない
    }
}

const char *SdlUiOptions::map_style_graf_tag(int index)
{
    switch (clamp_index(index, kMapStyleCount)) {
    case 1:
        return "new"; // graf-new.prf（Adam Bolt 16x16）
    case 2:
        return "old"; // graf-xxx.prf（オリジナル 8x8）
    default:
        return "ascii";
    }
}

bool SdlUiOptions::map_style_is_ascii(int index)
{
    return clamp_index(index, kMapStyleCount) == 3;
}

int SdlUiOptions::volume_percent(int index)
{
    const int i = clamp_index(index, kVolumeLevels);
    return 100 - i * 10; // 100,90,...,10
}

int SdlUiOptions::minimap_px_per_grid(int index)
{
    return clamp_index(index, kMinimapZoomCount) + 1; // 1..5 px
}

const char *SdlUiOptions::minimap_size_label(int index)
{
    switch (clamp_index(index, kMinimapSizeCount)) {
    case 0:
        return "基本";
    case 1:
        return "拡大";
    case 2:
        return "最大";
    default:
        return "基本";
    }
}

int SdlUiOptions::minimap_size_percent(int index)
{
    // 従来の最大（46%）を「基本」に据え、そこから 2 段階広げる。
    switch (clamp_index(index, kMinimapSizeCount)) {
    case 0:
        return 46;
    case 1:
        return 58;
    case 2:
        return 70;
    default:
        return 46;
    }
}

int SdlUiOptions::minimap_opacity_percent(int index)
{
    switch (clamp_index(index, kMinimapOpacityCount)) {
    case 0:
        return 40;
    case 1:
        return 55;
    case 2:
        return 70;
    case 3:
        return 85;
    case 4:
        return 100;
    default:
        return 70;
    }
}

/*!
 * @brief サブパネルの文字サイズ（pt）。
 * @details 標準（index 2）は Term ミラーと同じ 22pt（従来の見た目）。
 * 小さい側はコアのサブウインドウに入る桁・行を増やすため、大きい側は読みやすさのため。
 */
int SdlUiOptions::sub_panel_font_pt(int index)
{
    switch (clamp_index(index, kSubPanelFontCount)) {
    case 0:
        return 16;
    case 1:
        return 19;
    case 2:
        return 22;
    case 3:
        return 26;
    case 4:
        return 30;
    default:
        return 22;
    }
}

const char *SdlUiOptions::sub_panel_font_label(int index)
{
    switch (clamp_index(index, kSubPanelFontCount)) {
    case 0:
        return "極小 (16pt)";
    case 1:
        return "小 (19pt)";
    case 2:
        return "標準 (22pt)";
    case 3:
        return "大 (26pt)";
    case 4:
        return "特大 (30pt)";
    default:
        return "標準 (22pt)";
    }
}

void SdlUiOptions::normalize_sub_bottom_widths()
{
    int total = 0;
    for (int i = 0; i < 3; ++i) {
        if (sub_bottom_w20[i] < kSubBottomUnitMin) {
            sub_bottom_w20[i] = kSubBottomUnitMin;
        }
        if (sub_bottom_w20[i] > kSubBottomUnits) {
            sub_bottom_w20[i] = kSubBottomUnits;
        }
        total += sub_bottom_w20[i];
    }
    if (total == kSubBottomUnits) {
        return;
    }
    // 端数は真ん中で吸わせる。それでも下限を割るなら壊れた設定なので均等割りへ戻す。
    sub_bottom_w20[1] += kSubBottomUnits - total;
    if (sub_bottom_w20[1] < kSubBottomUnitMin) {
        constexpr int even = kSubBottomUnits / 3;
        sub_bottom_w20[0] = even;
        sub_bottom_w20[1] = even;
        sub_bottom_w20[2] = kSubBottomUnits - even * 2;
    }
}

bool SdlUiOptions::nudge_sub_bottom_width(int index, int delta)
{
    if (index < 0 || index > 2 || delta == 0) {
        return false;
    }
    // 融通する相手は右隣を優先。右端（index 2）だけ左隣から取る。
    const int partner = (index < 2) ? (index + 1) : 1;

    const int want = sub_bottom_w20[index] + delta;
    const int partner_want = sub_bottom_w20[partner] - delta;
    if (want < kSubBottomUnitMin || partner_want < kSubBottomUnitMin) {
        return false;
    }
    if (want > kSubBottomUnits || partner_want > kSubBottomUnits) {
        return false;
    }
    sub_bottom_w20[index] = want;
    sub_bottom_w20[partner] = partner_want;
    normalize_sub_bottom_widths();
    return true;
}

int SdlUiOptions::virtual_pad_opacity_percent(int index)
{
    return (clamp_index(index, kVirtualPadOpacityCount) + 1) * 10; // 10,20,…,100
}

/*!
 * @brief 方向パッド一辺の、画面短辺に対する割合（百分率）。
 * @details 画素で決めると端末ごとに指に対する大きさが変わる。短辺基準にすると
 * 携帯でも板でも「親指の届く範囲」がおおよそ揃う。
 */
int SdlUiOptions::virtual_pad_size_percent(int index)
{
    switch (clamp_index(index, kVirtualPadSizeCount)) {
    case 0:
        return 34;
    case 1:
        return 42;
    case 2:
        return 52;
    default:
        return 42;
    }
}

const char *SdlUiOptions::virtual_pad_size_label(int index)
{
    switch (clamp_index(index, kVirtualPadSizeCount)) {
    case 0:
        return "小";
    case 1:
        return "標準";
    case 2:
        return "大";
    default:
        return "標準";
    }
}

/*!
 * @brief 見下ろし角（度）。**1 点透視（設計書 版 2.1）向けに引き直した表**。
 * @details 旧表 75/60/50/40 は正射影時代の値で、1 点透視では上 2 段が実用外だった。
 * 画像平面が垂直である以上、地面の縦横比は `tanθ` に固定される（選べない）。
 * ```
 *   θ=75° → 3.73  床が横縞に潰れ、可視は 3 行弱      ← 実用外
 *   θ=60° → 1.73  床タイルが横へ伸びて縞に見える      ← 実用外
 * ```
 * そこで**実用域（およそ 0.6〜1.6）を 4 段に割り直した**。
 * | index | θ | 地面の縦横比 tanθ | 見え方 |
 * |---|---|---|---|
 * | 0 | 34° | 0.67 | 浅い。広く見渡せるが床はやや横長 |
 * | 1 | 42° | 0.90 | **既定**。ほぼ正方で素直 |
 * | 2 | 50° | 1.19 | やや縦長。奥行きが強い |
 * | 3 | 58° | 1.60 | 強く見下ろす。視界は狭い |
 * 成立条件 `θ > fov/2 = 20°` は 34° でも満たす。
 */
double SdlUiOptions::hd2d_pitch_degrees(int index)
{
    switch (clamp_index(index, kHd2dPitchCount)) {
    case 0:
        return 34.0;
    case 1:
        return 42.0;
    case 2:
        return 50.0;
    case 3:
        return 58.0;
    default:
        return 42.0;
    }
}

//! ラベルは度数だけでなく**見え方**が分かる語を添える（度数だけでは選べない）。
const char *SdlUiOptions::hd2d_pitch_label(int index)
{
    switch (clamp_index(index, kHd2dPitchCount)) {
    case 0:
        return "34 度（浅い・広く見渡す）";
    case 1:
        return "42 度（標準）";
    case 2:
        return "50 度（見下ろし強め）";
    case 3:
        return "58 度（最も見下ろす・視界は狭い）";
    default:
        return "42 度（標準）";
    }
}

double SdlUiOptions::hd2d_wall_height(int index)
{
    switch (clamp_index(index, kHd2dWallHeightCount)) {
    case 0:
        return 0.7;
    case 1:
        return 1.0;
    case 2:
        return 1.4;
    default:
        return 1.0;
    }
}

const char *SdlUiOptions::hd2d_wall_height_label(int index)
{
    switch (clamp_index(index, kHd2dWallHeightCount)) {
    case 0:
        return "0.7 マス（低い）";
    case 1:
        return "1.0 マス";
    case 2:
        return "1.4 マス（高い）";
    default:
        return "1.0 マス";
    }
}

const char *SdlUiOptions::hd2d_tiltshift_label(int index)
{
    switch (clamp_index(index, kHd2dLevelCount)) {
    case 0:
        return "OFF";
    case 1:
        return "弱";
    case 2:
        return "中";
    case 3:
        return "強";
    default:
        return "中";
    }
}

const char *SdlUiOptions::hd2d_dust_label(int index)
{
    switch (clamp_index(index, kHd2dLevelCount)) {
    case 0:
        return "OFF";
    case 1:
        return "少";
    case 2:
        return "中";
    case 3:
        return "多";
    default:
        return "中";
    }
}

int SdlUiOptions::hd2d_dust_particle_count(int index)
{
    switch (clamp_index(index, kHd2dLevelCount)) {
    case 0:
        return 0;
    case 1:
        return 96;
    case 2:
        return 192;
    case 3:
        return 320;
    default:
        return 192;
    }
}

SdlUiOptions &sdl_ui_options()
{
    return g_opts;
}

void set_sdl_ui_options_apply_hook(SdlUiOptionsApplyFn fn)
{
    g_apply_hook = fn;
}

void apply_sdl_ui_options()
{
    if (g_apply_hook != nullptr) {
        g_apply_hook();
    }
}

void load_sdl_ui_options(const char *path)
{
    g_opts = SdlUiOptions{};
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    std::ifstream in(path);
    if (!in) {
        return;
    }
    /*
     * 2026-07-31: タイル表示サイズの**先頭に** 1/4・1/3 を足したので、
     * それ以前に書かれた cfg の `tile_scale_index` は 2 段ずれている
     * （旧 0=1/2 → 新 2、旧 2=1.5 → 新 4）。版が無い cfg を版 1 と見なして寄せる。
     * 版のキーは行の順番に依存させたくないので、**読み終わってから**当てる。
     */
    int options_version = 1;
    int raw_tile_scale = -1;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const int value = std::atoi(line.c_str() + eq + 1);
        if (key == "options_version") {
            options_version = value;
        } else if (key == "tile_scale_index") {
            raw_tile_scale = value; // 版が確定してから寄せる（上のコメント）
        } else if (key == "map_style_index") {
            g_opts.map_style_index = clamp_index(value, SdlUiOptions::kMapStyleCount);
        } else if (key == "sound_enabled") {
            g_opts.sound_enabled = (value != 0);
        } else if (key == "music_enabled") {
            g_opts.music_enabled = (value != 0);
        } else if (key == "sound_volume_index") {
            g_opts.sound_volume_index = clamp_index(value, SdlUiOptions::kVolumeLevels);
        } else if (key == "music_volume_index") {
            g_opts.music_volume_index = clamp_index(value, SdlUiOptions::kVolumeLevels);
        } else if (key == "minimap_enabled") {
            g_opts.minimap_enabled = (value != 0);
        } else if (key == "minimap_zoom_index") {
            g_opts.minimap_zoom_index = clamp_index(value, SdlUiOptions::kMinimapZoomCount);
        } else if (key == "minimap_size_index") {
            g_opts.minimap_size_index = clamp_index(value, SdlUiOptions::kMinimapSizeCount);
        } else if (key == "minimap_opacity_index") {
            g_opts.minimap_opacity_index = clamp_index(value, SdlUiOptions::kMinimapOpacityCount);
        } else if (key == "cursor_mode") {
            g_opts.cursor_mode_enabled = (value != 0);
        } else if (key == "bot_json") {
            g_opts.bot_json_enabled = (value != 0);
        } else if (key == "window_mode") {
            g_opts.window_mode_enabled = (value != 0);
        } else if (key == "window_w") {
            // 0（未設定）はそのまま通す。異常値だけ落とす（窓を作れない大きさを覚えない）。
            g_opts.window_w = (value < 0 || value > 16384) ? 0 : value;
        } else if (key == "window_h") {
            g_opts.window_h = (value < 0 || value > 16384) ? 0 : value;
        } else if (key == "hd2d_enabled") {
            g_opts.hd2d_enabled = (value != 0);
        } else if (key == "hd2d_pitch") {
            g_opts.hd2d_pitch_index = clamp_index(value, SdlUiOptions::kHd2dPitchCount);
        } else if (key == "hd2d_wall_height") {
            g_opts.hd2d_wall_height_index = clamp_index(value, SdlUiOptions::kHd2dWallHeightCount);
        } else if (key == "hd2d_tiltshift") {
            g_opts.hd2d_tiltshift_index = clamp_index(value, SdlUiOptions::kHd2dLevelCount);
        } else if (key == "hd2d_dust") {
            g_opts.hd2d_dust_index = clamp_index(value, SdlUiOptions::kHd2dLevelCount);
        } else if (key == "hd2d_xray") {
            g_opts.hd2d_xray_enabled = (value != 0);
        } else if (key == "hd2d_teleport_fx") {
            g_opts.hd2d_teleport_fx_enabled = (value != 0);
        } else if (key == "realtime_enabled") {
            g_opts.realtime_enabled = (value != 0);
        } else if (key == "realtime_speed_index") {
            g_opts.realtime_speed_index = clamp_index(value, SdlUiOptions::kRealtimeSpeedCount);
        } else if (key == "realtime_prompt_live") {
            g_opts.realtime_prompt_live = (value != 0);
        } else if (key == "realtime_self_span_index") {
            g_opts.realtime_self_span_index = clamp_index(value, SdlUiOptions::kRealtimeSelfSpanCount);
        } else if (key.size() == 15 && key.compare(0, 9, "sub_panel") == 0 &&
            key.compare(10, 5, "_kind") == 0 && key[9] >= '1' &&
            key[9] <= static_cast<char>('0' + SdlUiOptions::kSubPanelCount)) {
            // sub_panel1_kind .. sub_panel5_kind。値はコアの SubWindowRedrawingFlag 番号。
            // **番号の正当性はここで判断しない**（コアの表を知らない TU なので）。
            // 一覧に無い番号は presentation/term 側が「UI 既定」に落とす。
            const int panel = key[9] - '1';
            g_opts.sub_panel_kind[panel] = (value < 0 || value > 15) ? -1 : value;
        } else if (key == "sub_panel_font") {
            g_opts.sub_panel_font_index = clamp_index(value, SdlUiOptions::kSubPanelFontCount);
        } else if (key == "main_font") {
            g_opts.main_font_index = clamp_index(value, SdlUiOptions::kMainFontCount);
        } else if (key.size() == 16 && key.compare(0, 15, "sub_bottom_w20_") == 0) {
            // sub_bottom_w20_1 .. _3。合計の整合は読み終わりに normalize で取る。
            const int panel = key[15] - '1';
            if (panel >= 0 && panel <= 2) {
                g_opts.sub_bottom_w20[panel] = value;
            }
        } else if (key == "virtual_pad") {
            g_opts.virtual_pad_enabled = (value != 0);
        } else if (key == "virtual_pad_opacity") {
            g_opts.virtual_pad_opacity_index = clamp_index(value, SdlUiOptions::kVirtualPadOpacityCount);
        } else if (key == "virtual_pad_size") {
            g_opts.virtual_pad_size_index = clamp_index(value, SdlUiOptions::kVirtualPadSizeCount);
        }
        // 未知のキーは黙って無視する（古い／新しい cfg でも起動できること）。
    }

    // タイル表示サイズは版で意味が変わるので、全部読んでから寄せる。
    if (raw_tile_scale >= 0) {
        //! 版 1（＝版のキーが無い cfg）は 1/4・1/3 を足す前の番号。2 段ずらす。
        const int shifted = (options_version < 2) ? (raw_tile_scale + 2) : raw_tile_scale;
        g_opts.tile_scale_index = clamp_index(shifted, SdlUiOptions::kTileScaleCount);
    }

    // 下段パネルの幅は 3 つの合計が意味を持つので、全部読んでから整える。
    g_opts.normalize_sub_bottom_widths();
}

void save_sdl_ui_options(const char *path)
{
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "[sdl-options] failed to save: %s\n", path);
        return;
    }
    out << "# Hengband SDL2 UI options\n";
    // 索引の意味が変わったときに古い cfg を寄せるための版（load 側の注記を読むこと）。
    out << "options_version=" << SdlUiOptions::kOptionsVersion << '\n';
    out << "tile_scale_index=" << g_opts.tile_scale_index << '\n';
    out << "map_style_index=" << g_opts.map_style_index << '\n';
    out << "sound_enabled=" << (g_opts.sound_enabled ? 1 : 0) << '\n';
    out << "music_enabled=" << (g_opts.music_enabled ? 1 : 0) << '\n';
    out << "sound_volume_index=" << g_opts.sound_volume_index << '\n';
    out << "music_volume_index=" << g_opts.music_volume_index << '\n';
    out << "minimap_enabled=" << (g_opts.minimap_enabled ? 1 : 0) << '\n';
    out << "minimap_zoom_index=" << g_opts.minimap_zoom_index << '\n';
    out << "minimap_size_index=" << g_opts.minimap_size_index << '\n';
    out << "minimap_opacity_index=" << g_opts.minimap_opacity_index << '\n';
    out << "cursor_mode=" << (g_opts.cursor_mode_enabled ? 1 : 0) << '\n';
    out << "bot_json=" << (g_opts.bot_json_enabled ? 1 : 0) << '\n';
    out << "window_mode=" << (g_opts.window_mode_enabled ? 1 : 0) << '\n';
    out << "window_w=" << g_opts.window_w << '\n';
    out << "window_h=" << g_opts.window_h << '\n';
    out << "hd2d_enabled=" << (g_opts.hd2d_enabled ? 1 : 0) << '\n';
    out << "hd2d_pitch=" << g_opts.hd2d_pitch_index << '\n';
    out << "hd2d_wall_height=" << g_opts.hd2d_wall_height_index << '\n';
    out << "hd2d_tiltshift=" << g_opts.hd2d_tiltshift_index << '\n';
    out << "hd2d_dust=" << g_opts.hd2d_dust_index << '\n';
    out << "hd2d_xray=" << (g_opts.hd2d_xray_enabled ? 1 : 0) << '\n';
    out << "hd2d_teleport_fx=" << (g_opts.hd2d_teleport_fx_enabled ? 1 : 0) << '\n';
    out << "realtime_enabled=" << (g_opts.realtime_enabled ? 1 : 0) << '\n';
    out << "realtime_speed_index=" << g_opts.realtime_speed_index << '\n';
    out << "realtime_prompt_live=" << (g_opts.realtime_prompt_live ? 1 : 0) << '\n';
    out << "realtime_self_span_index=" << g_opts.realtime_self_span_index << '\n';
    for (int i = 0; i < SdlUiOptions::kSubPanelCount; ++i) {
        out << "sub_panel" << (i + 1) << "_kind=" << g_opts.sub_panel_kind[i] << '\n';
    }
    out << "sub_panel_font=" << g_opts.sub_panel_font_index << '\n';
    out << "main_font=" << g_opts.main_font_index << '\n';
    for (int i = 0; i < 3; ++i) {
        out << "sub_bottom_w20_" << (i + 1) << "=" << g_opts.sub_bottom_w20[i] << '\n';
    }
    out << "virtual_pad=" << (g_opts.virtual_pad_enabled ? 1 : 0) << '\n';
    out << "virtual_pad_opacity=" << g_opts.virtual_pad_opacity_index << '\n';
    out << "virtual_pad_size=" << g_opts.virtual_pad_size_index << '\n';
}
