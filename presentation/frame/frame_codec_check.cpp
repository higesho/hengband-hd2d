/*!
 * @file frame_codec_check.cpp
 * @brief frame codec の自己検査（合成フレーム）と実プレイ往復の実装。
 *
 * ============================================================================
 * 二重簿記（この検査の中心）
 * ============================================================================
 * 「情報が 1 ビットも落ちない」を主張するには、**独立に全フィールドを列挙した一覧が
 * 2 つ**要る。片方だけだと、その 1 つに載っていないフィールドは検査を素通りする。
 *
 *   一覧 A: `make_synthetic_frame()`（このファイル）
 *           — `game_frame.h` の**全メンバに非既定値を詰める**。
 *   一覧 B: `game_frames_equal()`（`frame_codec.cpp`）
 *           — `game_frame.h` の**全メンバを突き合わせる**。
 *
 * 落ち方の対応:
 *   - encode/decode が或るフィールドを落としている → **一覧 A が非既定値を入れていれば**
 *     一覧 B が「既定値に戻っている」と言う。byte 比較では出ない（両側同じに落ちるため）。
 *   - 一覧 A の列挙漏れ（既定値のまま） → そのフィールドは省略規則で JSON から消えるので、
 *     `check_no_field_left_default()` の**キー経路一覧**が「経路が足りない」と言う。
 *   - 一覧 B の列挙漏れ → そのフィールドの取りこぼしは一覧 B では見えないが、
 *     `encode(decode(encode(f))) == encode(f)` の byte 比較が残っている
 *     （encode が値を出しているなら decode の取りこぼしは 2 回目の encode で消える）。
 *
 * つまり 3 つの検査（一覧 B の等価比較・byte 一致・キー経路一覧）が互いの穴を塞ぐ。
 * `game_frame.h` にフィールドを足した人がこの 3 か所のどれかを直し忘れると、
 * 残りのどれかが落ちる。
 */
#include "frame/frame_codec_check.h"

#include "frame/frame_codec.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace presentation {

namespace {

using json = nlohmann::json;

/*
 * ============================================================================
 * ログ
 * ============================================================================
 */
std::FILE *open_log(const char *preferred, const char *fallback)
{
    const char *env = std::getenv("HENGBAND_SDL2_PROTO_LOG");
    if (env != nullptr && env[0] != '\0') {
        std::FILE *fp = std::fopen(env, "w");
        if (fp != nullptr) {
            return fp;
        }
    }
    std::FILE *fp = std::fopen(preferred, "w");
    if (fp == nullptr) {
        fp = std::fopen(fallback, "w"); // docs/ が無い場所から起動されたとき（K-10 と同じ事情）
    }
    return fp;
}

std::FILE *g_log = nullptr;

void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[1024]{};
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_log != nullptr) {
        std::fputs(buf, g_log);
        std::fputc('\n', g_log);
        std::fflush(g_log);
    }
    std::fputs(buf, stderr);
    std::fputc('\n', stderr);
}

/*
 * ============================================================================
 * 一覧 A: 合成フレーム
 * ============================================================================
 * `game_frame.h` の宣言順に、**全メンバへ非既定値**を入れる。
 * 入れる値の趣味:
 *   - 数値は「全ビット立ち」「負の端」「0 でない中途半端な値」を混ぜる
 *   - 文字列は ASCII・全角・絵文字（4 バイト UTF-8）・引用符/バックスラッシュ/制御文字
 *   - vector は**必ず 2 要素以上**（1 要素だと「先頭しか運ばない」バグが通る）
 *   - 配列の**先頭要素は全フィールド非既定**（キー経路一覧が先頭しか見ないため）
 */

//! 検査用の意地の悪い文字列。JSON のエスケープ経路と UTF-8 経路を同時に踏む。
const char *kNastyText()
{
    // 全角・絵文字（U+1F409 は 4 バイト）・引用符・バックスラッシュ・タブ・
    // 制御文字（\x01 は  に化ける）・ラテン拡張。
    return "全角　テスト 🐉 \"quote\" \\back\\ \tTAB\x01 ÀÖü";
}

MapCellView make_cell_all_max()
{
    MapCellView c{};
    c.gx = static_cast<int16_t>(32767);
    c.gy = static_cast<int16_t>(32767);
    c.terrain_id = static_cast<uint16_t>(0xFFFF);
    c.feature_flags = static_cast<uint16_t>(0xFFFF); // 全ビット
    c.monster_id = static_cast<uint16_t>(0xFFFF);
    c.monster_slot = static_cast<uint16_t>(0xFFFE);
    c.object_id = static_cast<uint16_t>(0xFFFD);
    c.light_level = static_cast<uint8_t>(0xFF);
    c.fg_color = static_cast<uint8_t>(0xFE);
    c.bg_color = static_cast<uint8_t>(0xFD);
    c.ascii_fallback = static_cast<char>(0x7F);
    c.tile_index = static_cast<uint16_t>(0xFFFC);
    c.under_tile_index = static_cast<uint16_t>(0xFFFB);
    c.graf_fg_row = static_cast<int16_t>(32766);
    c.graf_fg_col = static_cast<int16_t>(32765);
    c.graf_bg_row = static_cast<int16_t>(32764);
    c.graf_bg_col = static_cast<int16_t>(32763);
    return c;
}

MapCellView make_cell_all_min()
{
    MapCellView c{};
    c.gx = static_cast<int16_t>(-32768); // 負の端
    c.gy = static_cast<int16_t>(-32767);
    c.terrain_id = 1;
    c.feature_flags = static_cast<uint16_t>(0x5555); // 交互ビット
    c.monster_id = 2;
    c.monster_slot = 3;
    c.object_id = 4;
    c.light_level = 5;
    c.fg_color = 6;
    c.bg_color = 7;
    c.ascii_fallback = static_cast<char>(-1); // char は MSVC で符号付き。0xFF が -1 で往復するか
    c.tile_index = 8;
    c.under_tile_index = 9;
    c.graf_fg_row = static_cast<int16_t>(-32768);
    c.graf_fg_col = static_cast<int16_t>(-2);
    c.graf_bg_row = static_cast<int16_t>(-3);
    c.graf_bg_col = static_cast<int16_t>(-4);
    return c;
}

MapCellView make_cell_mixed()
{
    MapCellView c{};
    c.gx = static_cast<int16_t>(-1);
    c.gy = static_cast<int16_t>(1);
    c.terrain_id = static_cast<uint16_t>(0xAAAA);
    c.feature_flags = static_cast<uint16_t>(0x0001);
    c.monster_id = static_cast<uint16_t>(0x8000);
    c.monster_slot = static_cast<uint16_t>(0x0100);
    c.object_id = static_cast<uint16_t>(0x00FF);
    c.light_level = static_cast<uint8_t>(0x80);
    c.fg_color = static_cast<uint8_t>(0x01);
    c.bg_color = static_cast<uint8_t>(0x7F);
    c.ascii_fallback = static_cast<char>(-128); // 符号付き char の下端
    c.tile_index = static_cast<uint16_t>(0x1234);
    c.under_tile_index = static_cast<uint16_t>(0x4321);
    c.graf_fg_row = static_cast<int16_t>(0);
    c.graf_fg_col = static_cast<int16_t>(-1); // 既定値そのもの（他の要素と混ざっていること）
    c.graf_bg_row = static_cast<int16_t>(127);
    c.graf_bg_col = static_cast<int16_t>(-128);
    return c;
}

SubPanelLine make_line(const char *text, uint8_t color)
{
    SubPanelLine l{};
    l.text_utf8 = text;
    l.color = color;
    return l;
}

MenuChoice make_choice(int base)
{
    MenuChoice mc{};
    mc.line_index = base + 1;
    mc.span_begin = base + 2;
    mc.span_len = base + 3;
    mc.key_begin = base + 4;
    mc.key_len = base + 5;
    mc.key = base + 6;
    return mc;
}

GameFrame make_synthetic_frame()
{
    GameFrame f{};

    // ---- frame_id / カメラ / 視界 / プレイヤ格子 -------------------------------
    f.frame_id = 0xFEDCBA9876543210ULL; // uint64 の上位ビットまで使う
    f.cam_x = -1234;
    f.cam_y = 5678;
    f.view_w = 41;
    f.view_h = 23;
    f.player_gx = -7;
    f.player_gy = 9999;

    // ---- cells（先頭は全フィールド非既定） ------------------------------------
    f.cells.push_back(make_cell_all_max());
    f.cells.push_back(make_cell_all_min());
    f.cells.push_back(make_cell_mixed());

    // ---- minimap（kinds は base64。長さ 16 = 4 の倍数でない → 詰め文字 2 個の経路） ----
    f.minimap.width = 8;
    f.minimap.height = 2;
    f.minimap.player_gx = 3;
    f.minimap.player_gy = 1;
    f.minimap.kinds = { 0, 1, 2, 3, 4, 5, 6, 7, 255, 128, 127, 64, 32, 16, 8, 200 };

    // ---- hud -----------------------------------------------------------------
    f.hud.name = "ぬるぽ🐉Ω";
    f.hud.hp = -12;
    f.hud.hp_max = 2147483647;
    f.hud.hp_label = "生命力";
    f.hud.sp = 3;
    f.hud.sp_max = -2147483647 - 1; // int の下端
    f.hud.sp_label = "声"; //!< SH-08。**空でない値を入れる**（番人が既定の残りを見る）
    f.hud.gold = 1234567;
    f.hud.depth = -2;
    f.hud.level = 50;
    f.hud.status_line = kNastyText();
    f.hud.right_top_lines = { "右上 1 行目", "right-top 2", "" };
    f.hud.right_bottom_lines = { "右下 1 行目", "right-bottom 2" };

    // ---- messages ------------------------------------------------------------
    {
        MessageEvent m{};
        m.seq = 0xFFFFFFFFu;
        m.color = 255;
        m.text_utf8 = kNastyText();
        f.messages.push_back(m);
        m.seq = 1;
        m.color = 1;
        m.text_utf8 = "2 通目のメッセージ";
        f.messages.push_back(m);
        m.seq = 2;
        m.color = 0; // 既定値の要素も混ぜる（省略規則が要素単位で効くこと）
        m.text_utf8 = "";
        f.messages.push_back(m);
    }

    // ---- 単純メンバ ----------------------------------------------------------
    f.controller_hint = "◀▶ 移動 / Ⓐ 決定";
    f.text_input_active = true;
    f.awaiting_command = true;
    f.camera_detached = true;
    f.menu_open = true;
    f.menu_over_map = true;
    f.pre_game_menu = true;
    f.title_screen = true;

    f.sub2_lines = { "装備 1", "装備 2 🗡", "" };
    f.sub3_lines = { "敵 1", "敵 2" };
    f.sub5_lines = { "持ち物 1", "持ち物 2", "持ち物 3" };

    // ---- sub_panels（全枚とも別々の値。先頭は全フィールド非既定） --------------
    for (int i = 0; i < kSubPanelCount; ++i) {
        SubPanelContent &p = f.sub_panels[static_cast<size_t>(i)];
        p.kind = i * 3 + 1; // 既定は -1
        char title[64]{};
        std::snprintf(title, sizeof(title), "見出し%d🐉", i);
        p.title_utf8 = title;
        p.lines.push_back(make_line(kNastyText(), static_cast<uint8_t>(200 + i)));
        p.lines.push_back(make_line("2 行目", static_cast<uint8_t>(2)));
    }

    // ---- status_col_lines ----------------------------------------------------
    f.status_col_lines.push_back(make_line(kNastyText(), 13));
    f.status_col_lines.push_back(make_line("ＬＥＶ    50", 1)); // color は既定値のまま
    f.status_col_lines.push_back(make_line("", 0));
    f.status_col_cols = 20; //!< 幻想蛮怒の `COL_MAP`。既定 0 と違う値を通す
    f.status_col_side = 1; //!< Frox の右置き。既定 0 と違う値を通す（設計 §6.1）

    // ---- bottom_row_runs / bottom_row_cols -----------------------------------
    {
        TermTextRun r{};
        r.col = 7;
        r.text_utf8 = kNastyText();
        r.color = 11;
        f.bottom_row_runs.push_back(r);
        r.col = -1; // 桁に負値が来ても運べること
        r.text_utf8 = "速度 (+10)";
        r.color = 1;
        f.bottom_row_runs.push_back(r);
    }
    f.bottom_row_cols = 80;

    // ---- depth ---------------------------------------------------------------
    f.depth = make_line("４５０ 階", 4);

    // ---- term_palette --------------------------------------------------------
    // 既定と 1 バイトでも違えば載る。端（0 と 255）と中間を混ぜておく。
    f.term_palette.rgb[0] = { { 0x01, 0x00, 0xFF } };
    f.term_palette.rgb[11] = { { 0x80, 0x7F, 0x00 } };

    // ---- menu_term_lines / カーソル ------------------------------------------
    {
        TermMirrorLine ln{};
        ln.text_utf8 = kNastyText();
        ln.color = 9;
        ln.source_row = 3;
        ln.highlight_begin = 6;
        ln.highlight_len = 12;
        /*
         * この検査は**全欄が既定値でないこと**を要求する（`check_no_field_left_default`）ので、
         * `begin` を 0 にできない。実物の区間は行頭から隙間なく並ぶ（`TermColorSpan` の注）。
         * ここで見ているのは並びが往復するかどうかだけである。
         */
        ln.color_spans.push_back(TermColorSpan{ 3, 6, 9 });
        ln.color_spans.push_back(TermColorSpan{ 9, 12, 11 });
        f.menu_term_lines.push_back(ln);
        ln.text_utf8 = "》 いいえ";
        ln.color = 1;
        ln.source_row = -4; // 負値
        ln.highlight_begin = 0;
        ln.highlight_len = 0;
        ln.color_spans.clear(); // 1 行 1 色の行（区間は積まない）
        f.menu_term_lines.push_back(ln);
    }
    f.menu_term_curs_col = 12;
    f.menu_term_curs_row = 0; // 既定は -1
    f.menu_page_prev_key = '-'; // ヘルプの「前ページ」
    f.menu_page_next_key = ' '; // 同「次ページ」

    // ---- menu_choices / menu_core_cursor / menu_core_cursors ------------------
    f.menu_choices.push_back(make_choice(10));
    f.menu_choices.push_back(make_choice(-100)); // 負値だらけの要素
    f.menu_core_cursor = make_choice(20);
    f.menu_core_cursors.push_back(make_choice(30));
    f.menu_core_cursors.push_back(make_choice(40));

    // ---- prompt --------------------------------------------------------------
    f.prompt.text_utf8 = "本当に終了しますか? [y/n]";
    {
        PromptChoice pc{};
        pc.label_utf8 = "はい";
        pc.key = 'y';
        pc.begin = 21;
        pc.len = 1;
        f.prompt.choices.push_back(pc);
        pc.label_utf8 = "いいえ";
        pc.key = 'n';
        pc.begin = 23;
        pc.len = 1;
        f.prompt.choices.push_back(pc);
    }
    f.prompt.footer_utf8 = "Ⓑ/ESC:いいえ";
    f.prompt.line_index = 0; // 既定は -1

    // ---- numeric -------------------------------------------------------------
    f.numeric.active = true;
    f.numeric.prompt_utf8 = "いくつですか (1-99): ";
    f.numeric.min = -5;
    f.numeric.max = 99;
    f.numeric.value = 42;
    f.numeric.digits = 2; // 既定は 1
    f.numeric.text_len = 1;
    f.numeric.line_index = 7; // 既定は -1
    f.numeric.value_begin = 19;
    f.numeric.value_len = 2;

    // ---- teleport_fx ---------------------------------------------------------
    // 端数のある float。0.85f は 2 進で割り切れないので、double 経由の往復が
    // 本当に無損失かを見るのにちょうどよい。
    f.teleport_fx.charge = 0.8499999f;
    f.teleport_fx.burst = true;

    // ---- floor（ボクセル HD2D §12。全部を既定から外す＝キー経路が 7 本増える） ----
    f.floor.dungeon_id = 5;
    f.floor.dun_level = 37;
    f.floor.generated_turn = 1234567u;
    f.floor.kind = 2; // FloorKind::Dungeon
    f.floor.town_id = 3; // P10 第 2 期（町の建物）で足した欄
    // カットイン演出（P10。2026-08-11）。**既定から外さないと省略されて経路が消える**。
    f.floor.place_name_utf8 = "イークの洞窟";
    f.floor.wild_mode = true;

    // ---- lighting（ボクセル HD2D §12-4・§12-5。同上で 3 本増える） ----
    // `daytime` の既定は true なので、**false にしないと省略されて経路が消える**。
    f.lighting.day_minute = 1387; // 既定 720（正午）から外す
    f.lighting.daytime = false;
    f.lighting.light_radius = 3;

    return f;
}

/*
 * ============================================================================
 * キー経路一覧（一覧 A の列挙漏れを捕まえる番人）
 * ============================================================================
 * 合成フレームを encode した JSON の「葉に至るキー経路」を集め、期待一覧と突き合わせる。
 * 既定値のまま残したフィールドは省略規則で JSON から消えるので、経路が 1 本足りなくなる。
 * 配列は**先頭要素だけ**辿る（だから合成フレームの先頭要素は全フィールド非既定にしてある）。
 */
void collect_paths(const json &j, const std::string &path, std::set<std::string> &out)
{
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            collect_paths(it.value(), path.empty() ? it.key() : (path + "." + it.key()), out);
        }
        return;
    }
    if (j.is_array()) {
        if (!j.empty()) {
            collect_paths(j[0], path + "[]", out);
        }
        return;
    }
    out.insert(path);
}

const std::vector<std::string> &expected_paths()
{
    // `game_frame.h` を上から順に写したもの。フィールドを足したらここも足す。
    static const std::vector<std::string> paths = {
        "t",
        "frame_id", "cam_x", "cam_y", "view_w", "view_h", "player_gx", "player_gy",
        "cells[][]",
        "minimap.width", "minimap.height", "minimap.player_gx", "minimap.player_gy", "minimap.kinds_b64",
        "hud.name", "hud.hp", "hud.hp_label", "hud.hp_max", "hud.sp", "hud.sp_max", "hud.sp_label",
        "hud.gold", "hud.depth", "hud.level",
        "hud.status_line", "hud.right_top_lines[]", "hud.right_bottom_lines[]",
        "messages[].seq", "messages[].color", "messages[].text_utf8",
        "controller_hint", "text_input_active", "awaiting_command", "camera_detached", "menu_open",
        "menu_over_map",
        "pre_game_menu",
        "title_screen",
        "sub2_lines[]", "sub3_lines[]", "sub5_lines[]",
        "sub_panels[].kind", "sub_panels[].title_utf8", "sub_panels[].lines[].text_utf8",
        "sub_panels[].lines[].color",
        "status_col_lines[].text_utf8", "status_col_lines[].color",
        "status_col_cols",
        "status_col_side",
        "bottom_row_runs[].col", "bottom_row_runs[].text_utf8", "bottom_row_runs[].color",
        "bottom_row_cols",
        "depth.text_utf8", "depth.color",
        "term_palette[]",
        "menu_term_lines[].text_utf8", "menu_term_lines[].color", "menu_term_lines[].source_row",
        "menu_term_lines[].color_spans[].begin", "menu_term_lines[].color_spans[].len",
        "menu_term_lines[].color_spans[].color",
        "menu_term_lines[].highlight_begin", "menu_term_lines[].highlight_len",
        "menu_term_curs_col", "menu_term_curs_row",
        "menu_page_prev_key", "menu_page_next_key",
        "menu_choices[].line_index", "menu_choices[].span_begin", "menu_choices[].span_len",
        "menu_choices[].key_begin", "menu_choices[].key_len", "menu_choices[].key",
        "menu_core_cursor.line_index", "menu_core_cursor.span_begin", "menu_core_cursor.span_len",
        "menu_core_cursor.key_begin", "menu_core_cursor.key_len", "menu_core_cursor.key",
        "menu_core_cursors[].line_index", "menu_core_cursors[].span_begin", "menu_core_cursors[].span_len",
        "menu_core_cursors[].key_begin", "menu_core_cursors[].key_len", "menu_core_cursors[].key",
        "prompt.text_utf8", "prompt.choices[].label_utf8", "prompt.choices[].key", "prompt.choices[].begin",
        "prompt.choices[].len", "prompt.footer_utf8", "prompt.line_index",
        "numeric.active", "numeric.prompt_utf8", "numeric.min", "numeric.max", "numeric.value",
        "numeric.digits", "numeric.text_len", "numeric.line_index", "numeric.value_begin", "numeric.value_len",
        "teleport_fx.charge", "teleport_fx.burst",
        "floor.dungeon_id", "floor.dun_level", "floor.generated_turn", "floor.kind", "floor.town_id",
        "floor.place_name", "floor.wild_mode",
        "lighting.day_minute", "lighting.daytime", "lighting.light_radius",
    };
    return paths;
}

int check_no_field_left_default(const std::string &encoded)
{
    const json j = json::parse(encoded, nullptr, false);
    if (j.is_discarded()) {
        say("  FAIL key-paths: encode の出力が JSON として読めない");
        return 1;
    }
    std::set<std::string> got;
    collect_paths(j, "", got);

    int failed = 0;
    for (const std::string &p : expected_paths()) {
        if (got.find(p) == got.end()) {
            say("  FAIL key-paths: 経路が無い（合成フレームが既定値のまま?）: %s", p.c_str());
            ++failed;
        }
    }
    std::set<std::string> expected(expected_paths().begin(), expected_paths().end());
    for (const std::string &p : got) {
        if (expected.find(p) == expected.end()) {
            say("  FAIL key-paths: 期待一覧に無い経路（仕様に足したなら一覧も直すこと）: %s", p.c_str());
            ++failed;
        }
    }
    if (failed == 0) {
        say("  PASS key-paths: %u 経路すべて一致（合成フレームに既定値残しなし）",
            static_cast<unsigned>(got.size()));
    }
    return failed;
}

/*
 * ============================================================================
 * base64 の単体検査
 * ============================================================================
 */
int check_base64()
{
    int failed = 0;
    // 長さ 0〜9（詰め文字 0/1/2 個の全パターンを跨ぐ）。
    for (size_t n = 0; n <= 9; ++n) {
        std::vector<uint8_t> src;
        for (size_t i = 0; i < n; ++i) {
            src.push_back(static_cast<uint8_t>(i * 37 + 3));
        }
        const std::string b64 = frame_base64_encode(src);
        std::vector<uint8_t> back;
        if (!frame_base64_decode(b64, back) || back != src) {
            say("  FAIL base64: 長さ %u で往復しない (\"%s\")", static_cast<unsigned>(n), b64.c_str());
            ++failed;
        }
    }
    // 0〜255 の全バイト値。
    {
        std::vector<uint8_t> src;
        for (int i = 0; i < 256; ++i) {
            src.push_back(static_cast<uint8_t>(i));
        }
        std::vector<uint8_t> back;
        if (!frame_base64_decode(frame_base64_encode(src), back) || back != src) {
            say("  FAIL base64: 0〜255 の全バイトが往復しない");
            ++failed;
        }
    }
    // 破損の検出。
    const char *bad[] = { "A", "AB", "ABC", "A===", "AB=C", "@@@@", "AAAAA" };
    for (const char *b : bad) {
        std::vector<uint8_t> back;
        if (frame_base64_decode(b, back)) {
            say("  FAIL base64: 壊れた入力を受け入れた: \"%s\"", b);
            ++failed;
        }
    }
    if (failed == 0) {
        say("  PASS base64: 往復・全バイト・破損検出");
    }
    return failed;
}

/*
 * ============================================================================
 * 実プレイ往復（HENGBAND_SDL2_PROTO_ROUNDTRIP）
 * ============================================================================
 */
struct RoundtripStats {
    uint64_t frames = 0;
    uint64_t mismatches = 0;
    uint64_t decode_failures = 0;
    size_t bytes_min = static_cast<size_t>(-1);
    size_t bytes_max = 0;
    uint64_t bytes_total = 0;
    double encode_ns_total = 0.0;
    double decode_ns_total = 0.0;
};

RoundtripStats g_stats;

void write_stats_line(const char *tag)
{
    const double n = (g_stats.frames > 0) ? static_cast<double>(g_stats.frames) : 1.0;
    say("%s frames=%llu mismatch=%llu decode_fail=%llu bytes[min=%u avg=%.0f max=%u] "
        "encode_total_ms=%.1f decode_total_ms=%.1f per_frame_us[encode=%.1f decode=%.1f]",
        tag, static_cast<unsigned long long>(g_stats.frames),
        static_cast<unsigned long long>(g_stats.mismatches),
        static_cast<unsigned long long>(g_stats.decode_failures),
        static_cast<unsigned>((g_stats.frames > 0) ? g_stats.bytes_min : 0),
        static_cast<double>(g_stats.bytes_total) / n, static_cast<unsigned>(g_stats.bytes_max),
        g_stats.encode_ns_total / 1.0e6, g_stats.decode_ns_total / 1.0e6,
        g_stats.encode_ns_total / n / 1000.0, g_stats.decode_ns_total / n / 1000.0);
}

void roundtrip_atexit()
{
    write_stats_line("FINAL");
}

//! 進行中でも数字が残るように定期的に書く。差分ハーネスはプロセスを kill するので
//! atexit だけに頼ると「殺されたら統計が消える」。
constexpr uint64_t kStatsEvery = 256;

} // namespace

/*
 * ============================================================================
 * 公開関数
 * ============================================================================
 */
int run_frame_codec_smoke()
{
    g_log = open_log("docs/frame_codec_smoke.log", "frame_codec_smoke.log");
    say("# HENGBAND_SDL2_PROTO_SMOKE — frame codec（プロトコル v1 §6）の自己検査");
    say("# 一覧 A = make_synthetic_frame / 一覧 B = game_frames_equal / 番人 = キー経路一覧");

    int failed = 0;
    failed += check_base64();

    const GameFrame src = make_synthetic_frame();

    // 1) encode
    const std::string s1 = encode_game_frame(src);
    say("  encode: %u バイト", static_cast<unsigned>(s1.size()));

    // 2) 番人: 合成フレームに既定値の残りが無いこと
    failed += check_no_field_left_default(s1);

    // 3) decode
    GameFrame back{};
    std::string err;
    if (!decode_game_frame(s1, back, err)) {
        say("  FAIL decode: %s", err.c_str());
        ++failed;
    } else {
        say("  PASS decode");

        // 4) 一覧 B: フィールド単位の等価比較
        std::string diff;
        if (!game_frames_equal(src, back, diff)) {
            say("  FAIL field-compare: %s", diff.c_str());
            ++failed;
        } else {
            say("  PASS field-compare（game_frame.h の全メンバ一致）");
        }

        // 5) byte 一致: encode(decode(encode(f))) == encode(f)
        const std::string s2 = encode_game_frame(back);
        if (s2 != s1) {
            size_t at = 0;
            while (at < s1.size() && at < s2.size() && s1[at] == s2[at]) {
                ++at;
            }
            say("  FAIL byte-identity: %u vs %u バイト、最初の相違は %u バイト目", static_cast<unsigned>(s1.size()),
                static_cast<unsigned>(s2.size()), static_cast<unsigned>(at));
            ++failed;
        } else {
            say("  PASS byte-identity: encode(decode(encode(f))) == encode(f)");
        }

        // 6) 3 周目まで安定（正準出力なら 2 周目と 3 周目も同じ）
        GameFrame back2{};
        if (!decode_game_frame(s2, back2, err)) {
            say("  FAIL decode(2nd): %s", err.c_str());
            ++failed;
        } else if (encode_game_frame(back2) != s1) {
            say("  FAIL canonical: 3 周目で揺れた");
            ++failed;
        } else {
            say("  PASS canonical: 3 周目まで同じバイト列");
        }
    }

    // 7) 既定フレーム（省略規則の下限）も往復すること。
    //    ここが `t` と `frame_id` の**常時出力**（v1 §6.1 の例外）を固定する場所でもある。
    //    全フィールド既定なのに 2 キー残るのが正しい姿。キー順は nlohmann の辞書順。
    {
        const GameFrame empty{};
        const std::string se = encode_game_frame(empty);
        const char *kExpectedEmpty = "{\"frame_id\":0,\"t\":\"frame\"}";
        if (se != kExpectedEmpty) {
            say("  FAIL empty-frame shape: expect %s got %s", kExpectedEmpty, se.c_str());
            ++failed;
        }
        GameFrame eback{};
        std::string diff;
        if (!decode_game_frame(se, eback, err)) {
            say("  FAIL empty-frame decode: %s", err.c_str());
            ++failed;
        } else if (!game_frames_equal(empty, eback, diff)) {
            say("  FAIL empty-frame compare: %s", diff.c_str());
            ++failed;
        } else if (encode_game_frame(eback) != se) {
            say("  FAIL empty-frame byte-identity");
            ++failed;
        } else {
            say("  PASS empty-frame: %s", se.c_str());
        }
    }

    // 8) 壊れた入力・別種別を撥ねること（§2.3 は未知“キー”の無視。種別違いは拒否）
    {
        GameFrame dummy{};
        std::string e;
        const char *bad[] = { "", "{", "[]", "{\"t\":\"hello\"}", "{\"frame_id\":1}",
            "{\"t\":\"frame\",\"cells\":[[1,2,3]]}", "{\"t\":\"frame\",\"minimap\":{\"kinds_b64\":\"!!!!\"}}",
            "{\"t\":\"frame\",\"cam_x\":\"nope\"}" };
        for (const char *b : bad) {
            if (decode_game_frame(b, dummy, e)) {
                say("  FAIL reject: 壊れた入力を受け入れた: %s", b);
                ++failed;
            }
        }
        // 未知キーは無視されること（前方互換・§2.3）。
        const std::string with_unknown = "{\"t\":\"frame\",\"cam_x\":5,\"future_key\":{\"a\":[1,2]}}";
        GameFrame fk{};
        if (!decode_game_frame(with_unknown, fk, e) || fk.cam_x != 5) {
            say("  FAIL forward-compat: 未知キーを無視できていない (%s)", e.c_str());
            ++failed;
        } else {
            say("  PASS reject/forward-compat");
        }
    }

    if (failed == 0) {
        say("RESULT: PASS");
    } else {
        say("RESULT: FAIL (%d)", failed);
    }
    if (g_log != nullptr) {
        std::fclose(g_log);
        g_log = nullptr;
    }
    return failed;
}

bool proto_roundtrip_enabled()
{
    const char *v = std::getenv("HENGBAND_SDL2_PROTO_ROUNDTRIP");
    return v != nullptr && v[0] != '\0' && v[0] != '0';
}

std::function<void(const GameFrame &)> wrap_present_with_roundtrip(std::function<void(const GameFrame &)> inner)
{
    if (g_log == nullptr) {
        g_log = open_log("docs/proto_roundtrip.log", "proto_roundtrip.log");
    }
    say("# HENGBAND_SDL2_PROTO_ROUNDTRIP — present へ渡すフレームを encode→decode に通す");
    std::atexit(&roundtrip_atexit);

    return [inner](const GameFrame &frame) {
        using clock = std::chrono::steady_clock;

        const auto t0 = clock::now();
        const std::string s1 = encode_game_frame(frame);
        const auto t1 = clock::now();

        GameFrame restored{};
        std::string err;
        const bool ok = decode_game_frame(s1, restored, err);
        const auto t2 = clock::now();

        ++g_stats.frames;
        g_stats.bytes_total += s1.size();
        g_stats.bytes_min = (std::min)(g_stats.bytes_min, s1.size());
        g_stats.bytes_max = (std::max)(g_stats.bytes_max, s1.size());
        g_stats.encode_ns_total += std::chrono::duration<double, std::nano>(t1 - t0).count();
        g_stats.decode_ns_total += std::chrono::duration<double, std::nano>(t2 - t1).count();

        if (!ok) {
            ++g_stats.decode_failures;
            ++g_stats.mismatches;
            say("MISMATCH frame_id=%llu decode failed: %s",
                static_cast<unsigned long long>(frame.frame_id), err.c_str());
            inner(frame); // 復元できないなら元のフレームで描く（画面は止めない）
            return;
        }

        // 元の encode 結果と、復元フレームの encode 結果が byte 一致すること。
        const std::string s2 = encode_game_frame(restored);
        if (s2 != s1) {
            ++g_stats.mismatches;
            size_t at = 0;
            while (at < s1.size() && at < s2.size() && s1[at] == s2[at]) {
                ++at;
            }
            std::string diff;
            game_frames_equal(frame, restored, diff);
            say("MISMATCH frame_id=%llu bytes %u vs %u, first diff at %u, field: %s",
                static_cast<unsigned long long>(frame.frame_id), static_cast<unsigned>(s1.size()),
                static_cast<unsigned>(s2.size()), static_cast<unsigned>(at),
                diff.empty() ? "(フィールド比較では一致)" : diff.c_str());
        } else {
            // byte が同じでも「encode が両側で同じように落とした」可能性は残るので、
            // フィールド比較も毎フレーム回す（合成フレーム検査と同じ二重簿記）。
            std::string diff;
            if (!game_frames_equal(frame, restored, diff)) {
                ++g_stats.mismatches;
                say("MISMATCH frame_id=%llu field: %s", static_cast<unsigned long long>(frame.frame_id),
                    diff.c_str());
            }
        }

        if ((g_stats.frames % kStatsEvery) == 0) {
            write_stats_line("STATS");
        }

        // ここが本題: **復元した方**を描画へ渡す。
        inner(restored);
    };
}

} // namespace presentation
