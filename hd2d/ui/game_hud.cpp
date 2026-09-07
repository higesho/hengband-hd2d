/*!
 * @file game_hud.cpp
 * @brief `game_hud.h` の実装。
 */
#include "ui/game_hud.h"

#include "frame/cell_feature_bits.h" //!< `CELL_FEAT_KNOWN`（2D アスキー地図が未踏破を飛ばす）
#include "i18n/lang.h"
#include "render/camera.h" //!< `rotate_screen_delta`（相対表示の回し方は世界とここで 1 つの定義）
#include "render/term_colors.h"
#include "render/text_overlay.h"
#include "ui/ui_paint.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace hd2d {

namespace {

TextColor text_from_term_color(std::uint8_t index, float alpha = 1.f)
{
    const RgbColor rgb = term_color_to_rgb(index);
    return TextColor{ static_cast<float>(rgb.r) / 255.f, static_cast<float>(rgb.g) / 255.f,
        static_cast<float>(rgb.b) / 255.f, alpha };
}

const TextColor kTitleColor{ 0.72f, 0.80f, 0.95f, 1.f };
const TextColor kDimColor{ 0.62f, 0.66f, 0.72f, 1.f };

/*!
 * @brief `sp` の名札（SH-08 / W5）。**コアが送ってこなければ `SP`。**
 * @details 画面は「魔力」も「声」も知らない。語はコアが決め、訳もコアの側で
 * 当たっている（`hud_snapshot.h` の `sp_label`）。ここは**既定を 1 か所に置く**だけ。
 */
const char *sp_label_of(const GameFrame &frame)
{
    return frame.hud.sp_label.empty() ? "SP" : frame.hud.sp_label.c_str();
}

//! @copydoc sp_label_of
const char *hp_label_of(const GameFrame &frame)
{
    return frame.hud.hp_label.empty() ? "HP" : frame.hud.hp_label.c_str();
}

/*!
 * @brief 割り当ての表示に使う行数の上限。
 * @details 旧 2D UI の操作ヒントの行数の上限と同じ 2 行。
 * **数を写しているのであって、コードは引いていない**（必守制約 6）。
 * 13 入力ぶんが 1 行に収まらない窓でも、帯を押し広げて地図を縮めるよりは切るほうがよい。
 */
constexpr int kControllerBindMaxLines = 2;

/*!
 * @brief 1 行に入らない字を**折り返す**（切り捨てない）。
 * @param max_width 1 行に使える画素。0 以下なら折らずにそのまま 1 行で返す。
 * @return 上の行から順に。**必ず 1 本以上**返る（空文字列を渡したときを除く）。
 * @details メッセージ用である。`TextOverlay::draw` は入らない字を**捨てる**ので、
 * 長いメッセージが途中で切れて読めなくなっていた（2026-08-21 に気づいた
 * 「メッセージがパネル全体に表示せず半分くらいで切れる」）。パネルには行が余っているので、
 * 切るのではなく次の行へ送る。
 *
 * @note **語の切れ目は見ない。**日本語には空白が無く、英語だけ語で折ると
 * 「言語で挙動が変わる」ものが 1 つ増える。マスで折るのはコアの `msg_print` と同じ流儀である
 * （あちらも `split_length` で桁を見ており、語の切れ目は二の次にしている）。
 */
std::vector<std::string> wrap_utf8(TextOverlay &text, const std::string &utf8, int max_width)
{
    std::vector<std::string> out;
    if (utf8.empty()) {
        return out;
    }
    if (max_width <= 0) {
        out.push_back(utf8);
        return out;
    }
    std::string_view rest{ utf8 };
    while (!rest.empty()) {
        const std::size_t take = text.fit_bytes(rest, max_width);
        if (take == 0) {
            //! 1 文字も入らない幅。**輪を止める**（残りは出さない。無限に回すよりよい）。
            break;
        }
        out.emplace_back(rest.substr(0, take));
        rest.remove_prefix(take);
    }
    return out;
}

/*!
 * @brief 文字グリッド（コアの Term の写し）をマスで置く。
 * @param head 見出し（空なら出さない）。
 * @details **1 行 1 色**（`SubPanelLine::color`）。行の中の色分けを持っているのは
 * `TermMirrorLine::highlight_*` と `bottom_row_runs` だけで、そちらは別に扱う。
 */
void draw_text_grid(TextOverlay &text, const RectPx &body, const std::string &head,
    const std::vector<SubPanelLine> &lines, int cell_h)
{
    if (body.empty()) {
        return;
    }
    int y = body.y;
    if (!head.empty()) {
        text.draw(body.x, y, head, kTitleColor, body.w);
        y += cell_h;
    }
    for (const auto &line : lines) {
        if ((y + cell_h) > (body.y + body.h)) {
            break; // 入り切らないぶんは出さない（枠の外へは描かない）
        }
        text.draw(body.x, y, line.text_utf8, text_from_term_color(line.color), body.w);
        y += cell_h;
    }
}

//! 文字列だけの一覧（UI 既定のサブパネル）。
void draw_plain_lines(TextOverlay &text, const RectPx &body, const std::string &head,
    const std::vector<std::string> &lines, int cell_h)
{
    std::vector<SubPanelLine> converted;
    converted.reserve(lines.size());
    for (const auto &line : lines) {
        converted.push_back(SubPanelLine{ line, 9 }); // 9 = L_WHITE
    }
    draw_text_grid(text, body, head, converted, cell_h);
}

/*!
 * @brief UI 既定（`kind < 0`）の枚に何を出すか。
 * @details 既存 UI と同じ割り当て（Sub1 = メッセージ / Sub2 = 装備 / Sub3 = 視界の敵 /
 * Sub4 = キャラクター / Sub5 = 持ち物）。**利用者がコアのサブウインドウを割り当てたら
 * そちらが優先**されるので、ここは「割り当てが無いときの中身」でしかない。
 */
void draw_default_sub_panel(TextOverlay &text, const RectPx &body, int index,
    const GameFrame &frame, int cell_h)
{
    switch (index) {
    case 0: { // Sub1: 直近のメッセージ（新しいものが下・**入らない字は折り返す**）
        const int rows = std::max(1, body.h / cell_h);
        int y = body.y;
        for (const SubPanelLine &line : recent_message_lines(text, frame.messages, body.w, rows)) {
            text.draw(body.x, y, line.text_utf8, text_from_term_color(line.color), body.w);
            y += cell_h;
        }
        break;
    }
    case 1:
        draw_plain_lines(text, body, i18n::tr("hd2d.ui.game-hud.equipment"), frame.sub2_lines, cell_h);
        break;
    case 2:
        draw_plain_lines(text, body, i18n::tr("hd2d.ui.game-hud.enemies-in-view"), frame.sub3_lines, cell_h);
        break;
    case 3: { // Sub4: キャラクター
        char buf[160]{};
        std::vector<std::string> lines;
        lines.push_back(frame.hud.name);
        /*
         * **負は「そのコアにその概念が無い」**（`hud_snapshot.h`）。項目ごと出さない。
         * Sil-Q にはレベルも所持金も無く、0 で出すと `LV 0` / `AU 0` が並ぶ。
         */
        if (frame.hud.level >= 0) {
            std::snprintf(buf, sizeof(buf), "LV %d", frame.hud.level);
            lines.emplace_back(buf);
        }
        std::snprintf(buf, sizeof(buf), "%s %d/%d", hp_label_of(frame), frame.hud.hp,
            frame.hud.hp_max);
        lines.emplace_back(buf);
        //! 名札はコアが決める（SH-08。`hud_snapshot.h` の `sp_label`）。空なら既定。
        std::snprintf(buf, sizeof(buf), "%s %d/%d", sp_label_of(frame), frame.hud.sp,
            frame.hud.sp_max);
        lines.emplace_back(buf);
        if (frame.hud.gold >= 0) {
            std::snprintf(buf, sizeof(buf), "AU %d", frame.hud.gold);
            lines.emplace_back(buf);
        }
        for (const auto &line : frame.hud.right_top_lines) {
            lines.push_back(line);
        }
        draw_plain_lines(text, body, std::string(), lines, cell_h);
        break;
    }
    case 4:
        draw_plain_lines(text, body, i18n::tr("hd2d.ui.game-hud.inventory"), frame.sub5_lines, cell_h);
        break;
    default:
        break;
    }
}

/*!
 * @brief コアの最下行（ステータスバー）と操作ヒント。
 * @details **桁に意味がある**（`print_speed` は右から 24 桁目、`print_depth` は 8 桁目）ので、
 * 帯の幅を `bottom_row_cols` で割ったマスに置く。1 本の文字列に詰めると右端揃えが崩れる（K-32）。
 */
void draw_bottom_bar(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const GameFrame &frame,
    const HudState &state)
{
    const RectPx &bar = layout.bottom_bar;
    if (bar.empty()) {
        return;
    }
    /*
     * **透けさせるかどうかは「3D に重なる作りか」で決める**（作りの名前で決めない）。
     * `Tall` を足したときに `== Split` のままだと、地図に重なっていないパネルが
     * 半透明のまま出て「下が透けているのに何も無い」絵になる。
     */
    PanelStyle style = layout.sub_overlaid ? overlay_panel_style() : solid_panel_style();
    style.border_px = 0;
    paint.panel(bar, style);

    const RectPx body = panel_body(bar, layout.cell_w);
    if (!frame.bottom_row_runs.empty() && (frame.bottom_row_cols > 0)) {
        const double col_px = static_cast<double>(body.w) / static_cast<double>(frame.bottom_row_cols);
        for (const auto &run : frame.bottom_row_runs) {
            const int x = body.x + static_cast<int>(static_cast<double>(run.col) * col_px);
            text.draw(x, body.y, run.text_utf8, text_from_term_color(run.color), (body.x + body.w) - x);
        }
    }
    int y = body.y + layout.cell_h;
    if (!frame.controller_hint.empty()) {
        text.draw(body.x, y, frame.controller_hint, kDimColor, body.w);
    }
    //! UI が横取りしているキーは**右端に寄せて**出す（コアの案内と混ざらないように）。
    if (!state.ui_key_hint.empty()) {
        const int w = text.measure(state.ui_key_hint);
        text.draw((body.x + body.w) - w, y, state.ui_key_hint, kDimColor, w);
    }
    y += layout.cell_h;

    /*
     * コントローラーの割り当て（2026-08-11 に決めた）。**幅に応じて折り返す。**
     * 13 入力ぶんは狭い窓では 1 行に収まらないので、実測した幅で詰められるだけ詰め、
     * 入らないぶんは次の行へ送る。行数の上限を超えたら**そこで止める**
     * （帯を押し広げると地図が縮む。旧 2D UI と同じ判断）。
     */
    const int lines_room = ((body.y + body.h) - y) / layout.cell_h;
    if ((lines_room <= 0) || state.controller_binds.empty()) {
        return;
    }
    const int max_lines = std::min(lines_room, kControllerBindMaxLines);
    const int sep_w = text.measure("  ");
    std::string line;
    int line_w = 0;
    int drawn = 0;
    for (std::size_t i = 0; i <= state.controller_binds.size(); ++i) {
        const bool last = (i == state.controller_binds.size());
        const int item_w = last ? 0 : text.measure(state.controller_binds[i]);
        const bool overflow = !line.empty() && ((line_w + sep_w + item_w) > body.w);
        if (last || overflow) {
            if (!line.empty()) {
                text.draw(body.x, y, line, kDimColor, body.w);
                y += layout.cell_h;
                ++drawn;
            }
            line.clear();
            line_w = 0;
            if (last || (drawn >= max_lines)) {
                break;
            }
        }
        if (!line.empty()) {
            line += "  ";
            line_w += sep_w;
        }
        line += state.controller_binds[i];
        line_w += item_w;
    }
}

//! `Full` のゲージ。数字だけだと減ったことに気づけないので棒でも出す。
void draw_hud_gauges(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const GameFrame &frame)
{
    const RectPx &hud = layout.hud;
    if (hud.empty()) {
        return;
    }
    const int bar_h = std::max(4, layout.cell_h / 2);
    /*
     * 名札の桁。**両方の名札を測って広いほうに合わせる**（SH-08 / W5）。
     * 3 桁の決め打ちのままだと、コアが `Voice` のような長い名札を送ってきたときに
     * 名札がゲージへ食い込む。`HP`/`SP` しか来ないコアでは `cell_w * 3` のままなので、
     * **絵は 1 画素も変わらない**。
     */
    const char *const hp_name = hp_label_of(frame);
    const char *const sp_name = sp_label_of(frame);
    const int label_w = std::max(layout.cell_w * 3,
        std::max(text.measure(hp_name), text.measure(sp_name)) + layout.cell_w);
    char buf[96]{};

    const auto ratio = [](int now, int max) {
        return (max > 0) ? (static_cast<float>(now) / static_cast<float>(max)) : 0.f;
    };
    const PaintColor back{ 0.f, 0.f, 0.f, 0.55f };

    text.draw(hud.x, hud.y, hp_name, kDimColor);
    paint.gauge(RectPx{ hud.x + label_w, hud.y + (layout.cell_h / 4), hud.w - label_w, bar_h },
        ratio(frame.hud.hp, frame.hud.hp_max), PaintColor{ 0.85f, 0.25f, 0.25f, 0.92f }, back);
    std::snprintf(buf, sizeof(buf), "%d/%d", frame.hud.hp, frame.hud.hp_max);
    text.draw(hud.x + label_w + layout.cell_w, hud.y, buf, TextColor{ 1.f, 1.f, 1.f, 0.95f });

    const int row2 = hud.y + layout.cell_h;
    text.draw(hud.x, row2, sp_name, kDimColor);
    paint.gauge(RectPx{ hud.x + label_w, row2 + (layout.cell_h / 4), hud.w - label_w, bar_h },
        ratio(frame.hud.sp, frame.hud.sp_max), PaintColor{ 0.30f, 0.45f, 0.90f, 0.92f }, back);
    std::snprintf(buf, sizeof(buf), "%d/%d", frame.hud.sp, frame.hud.sp_max);
    text.draw(hud.x + label_w + layout.cell_w, row2, buf, TextColor{ 1.f, 1.f, 1.f, 0.95f });

    /*
     * 名前の行。**無い項目は足さない**（`hud_snapshot.h` の負の約束）。
     * べた書きの `"%s  LV %d  AU %d"` のままだと、Sil-Q で `LV 0  AU 0` が名前の隣に並ぶ。
     */
    std::string who = frame.hud.name;
    if (frame.hud.level >= 0) {
        std::snprintf(buf, sizeof(buf), "  LV %d", frame.hud.level);
        who += buf;
    }
    if (frame.hud.gold >= 0) {
        std::snprintf(buf, sizeof(buf), "  AU %d", frame.hud.gold);
        who += buf;
    }
    text.draw(hud.x, hud.y + (layout.cell_h * 2), who, kDimColor, hud.w);
}

/*!
 * @brief ミニマップの自分を**向きのある三角**で描く（一人称のときだけ）。
 * @param cx,cy 自分のマスの中心（画素）。
 * @param facing 方位（ラジアン。0 = 北・時計回り）。
 * @param scale 1 マスの画素。
 *
 * @details 2026-08-11 に決めた。輝点だと「どちらを向いているか」が出せない。
 *
 * **画面の上が北**（ミニマップは地図をそのまま並べたもの）なので、
 * 方位 0 で先が上（-y）を向く。時計回りが正なので、90° で右（+x）。
 * 世界の向きの約束（`render/camera.h`: x = 東・y = 南）と同じ回し方である。
 */
void draw_minimap_arrow(UiPaint &paint, float cx, float cy, float facing, int scale, const PaintColor &color)
{
    const float s = std::sin(facing);
    const float c = std::cos(facing);
    //! 前 = (sin, −cos)（`FpsMode::direction_delta` と同じ式）。右 = (cos, sin)。
    const float fx = s;
    const float fy = -c;
    const float rx = c;
    const float ry = s;
    /*
     * マスより**はっきり大きく**取る。1 マスは 6px しかないので、マスなりの大きさだと
     * 三角に見えず、ただの点に戻る（＝「回っていない」ように見える。
     * 2026-08-11 に気づいた「360 度回転で表示させたい」）。
     * 鼻を長く・尾を短くすると、斜めの角でも先がどちらか読める。
     */
    const float nose = static_cast<float>(scale) * 1.90f;
    const float tail = static_cast<float>(scale) * 0.85f;
    const float wing = static_cast<float>(scale) * 1.05f;
    paint.triangle(cx + (fx * nose), cy + (fy * nose),
        cx - (fx * tail) + (rx * wing), cy - (fy * tail) + (ry * wing),
        cx - (fx * tail) - (rx * wing), cy - (fy * tail) - (ry * wing), color);
}

/*!
 * @brief ミニマップ。1 格子 1 画素では読めないので、入るだけ倍にする。
 * @return 実際に描いた箱（描かなかったら空）。階層の文字をその真上へ置くのに使う。
 */
RectPx draw_minimap(UiPaint &paint, const UiLayout &layout, const GameFrame &frame, const HudState &state)
{
    const RectPx &area = layout.minimap;
    if (area.empty() || !frame.minimap.valid()) {
        return RectPx{};
    }
    /*
     * `layout.minimap` は**取っておく場所（予算）**であって、描く箱ではない。
     *
     * **フロア全体を入れようとしない**（旧 HD2D と同じ作り）。198×66 を 1 画素/マスで
     * 詰め込むと、入らないときに予算をはみ出すし、入っても粒が小さすぎて読めない。
     * **1 マスを数画素に取り、プレイヤを中心に切り出す。**箱は予算の右上に寄せる
     * （2026-08-08 に決めた。旧 HD2D と同じ位置）。
     */
    /*
     * 1 マスの画素（**縮尺**）。既定 6 は2026-08-09 に決めた「縦横今の倍のサイズに」で
     * 3 から上げた値で、2026-08-19 から機能メニューで回せる（`Hd2dSettings::minimap_cell_px`）。
     */
    const int kPixelsPerGrid = std::max(1, state.minimap_cell_px);
    const int cols = std::max(1, (area.w - 4) / kPixelsPerGrid);
    const int rows = std::max(1, (area.h - 4) / kPixelsPerGrid);
    const int centre_gx = (frame.minimap.player_gx >= 0) ? frame.minimap.player_gx : (frame.minimap.width / 2);
    const int centre_gy = (frame.minimap.player_gy >= 0) ? frame.minimap.player_gy : (frame.minimap.height / 2);
    const int ox_grid = std::clamp(centre_gx - (cols / 2), 0, std::max(0, frame.minimap.width - cols));
    const int oy_grid = std::clamp(centre_gy - (rows / 2), 0, std::max(0, frame.minimap.height - rows));
    const int shown_cols = std::min(cols, frame.minimap.width - ox_grid);
    const int shown_rows = std::min(rows, frame.minimap.height - oy_grid);
    const int scale = kPixelsPerGrid;
    const int draw_w = shown_cols * scale;
    const int draw_h = shown_rows * scale;
    /*
     * 枠は**予算（`layout.minimap`）の中**に置く。マスの数は整数なので予算をきっちり埋められず、
     * 端数がどちら側に出るかを**置いた隅に合わせる**必要がある
     * （右上なら右へ寄せる。左に寄せると、左上を選んだのに右へずれて見える）。
     */
    const bool align_right = (state.minimap_corner == MinimapCorner::TopRight)
        || (state.minimap_corner == MinimapCorner::BottomRight);
    const bool align_bottom = (state.minimap_corner == MinimapCorner::BottomRight)
        || (state.minimap_corner == MinimapCorner::BottomLeft);
    const int box_w = draw_w + 4;
    const int box_h = draw_h + 4;
    int box_x = align_right ? (area.x + area.w - box_w) : area.x;
    int box_y = align_bottom ? (area.y + area.h - box_h) : area.y;
    if (state.minimap_corner == MinimapCorner::Centre) {
        box_x = area.x + ((area.w - box_w) / 2);
        box_y = area.y + ((area.h - box_h) / 2);
    }
    const RectPx box{ box_x, box_y, box_w, box_h };

    /*
     * 色は**青を下地に**する（2026-08-08 に決めた「旧 HD2D と同様に青ベース」）。
     * 値は旧 HD2D のミニマップの実測値と同じもの。**コードは引かない**
     * （必守制約 6）。同じ見え方にしたいので数が揃うだけである。
     * 床が明るい青・壁が沈んだ紺で、**地図として「通れる所」が浮き上がる**のが要点。
     */
    /*
     * **表示濃度**（2026-08-19 に決めた）。下敷きも点も同じ倍率で掛ける
     * ——下敷きだけ薄くすると点が浮いて、地図としてかえって読みにくい。
     * 100% で従来の値そのもの（掛け算が恒等になる）。
     */
    const float opacity = static_cast<float>(std::max(0, state.minimap_opacity_pct)) / 100.f;
    const auto fade = [opacity](float a) { return std::min(1.f, a * opacity); };
    PanelStyle style = overlay_panel_style();
    style.fill = PaintColor{ 8.f / 255.f, 10.f / 255.f, 20.f / 255.f, fade(0.72f) };
    style.border = PaintColor{ 150.f / 255.f, 170.f / 255.f, 210.f / 255.f, fade(0.60f) };
    paint.panel(box, style);

    const int ox = box.x + 2;
    const int oy = box.y + 2;
    const auto rgba8 = [fade](int r, int g, int b, int a) {
        return PaintColor{ static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f,
            static_cast<float>(b) / 255.f, fade(static_cast<float>(a) / 255.f) };
    };

    /*
     * **相対表示**（2026-08-19 に決めた「地図ごと回すかどうかを切り替える」）。
     *
     * 絵のマス → 世界のマスの写しを 1 か所に閉じる。回し方は
     * `rotate_screen_delta()`（`render/camera.h`）——**移動の回転と同じ関数**である。
     * ここに別の式を書くと、「地図の上へ歩いたのに地図の右へ進む」という
     * 一番わかりにくい食い違いが生まれる。
     *
     * 絶対表示（既定）では回転が入らないので、**従来と 1 マスも変わらない**
     * （`ox_grid`／`oy_grid` の切り出しもそのまま通る）。
     */
    const bool relative = state.minimap_relative && (state.camera_turn != 0);
    //! 地図を回す段。45°（奇数の段）は近い 90° へ寄せる（下の `world_of` の註記）。
    const int map_turn = ((state.camera_turn + 1) / 2 * 2) % kViewTurnCount;
    const int half_cols = shown_cols / 2;
    const int half_rows = shown_rows / 2;
    const auto world_of = [&](int rx, int ry, int &x, int &y) {
        if (!relative) {
            x = ox_grid + rx;
            y = oy_grid + ry;
            return;
        }
        /*
         * 相対では**自機を枠の中心に据える**（切り出しの clamp は使わない）。
         * フロアの端では地図の外を舐めるが、`MinimapSnapshot::at()` が
         * 範囲外に `Unknown` を返すので「描かない」に落ちる（捏造しない）。
         */
        int u = rx - half_cols;
        int v = ry - half_rows;
        /*
         * **地図は 90° の段へ寄せて回す**（2026-09-06。視点は 45° 刻みになった）。
         * 45° の回転は格子に載らず、丸めると同じマスを 2 度舐めて地図に穴が開く。
         * 地図の向きは最大 45° ずれるが、**歩く向き（`rotate_screen_delta` を通る移動）は
         * 45° のまま正しい**——ずれるのは地図の絵だけである。
         */
        rotate_screen_delta(map_turn, u, v);
        x = centre_gx + u;
        y = centre_gy + v;
    };

    for (int ry = 0; ry < shown_rows; ++ry) {
        for (int rx = 0; rx < shown_cols; ++rx) {
            int x = 0;
            int y = 0;
            world_of(rx, ry, x, y);
            const MinimapKind kind = frame.minimap.at(x, y);
            if (kind == MinimapKind::Unknown) {
                continue; // 未踏破は描かない（暗いのではなく「無い」）
            }
            PaintColor color;
            switch (kind) {
            case MinimapKind::Floor:
                color = rgba8(60, 110, 220, 170);
                break;
            case MinimapKind::Wall:
                color = rgba8(34, 46, 84, 150);
                break;
            //! 山は壁より明るい岩の色（町の外れの山肌が「壁」と見分かるように）。
            case MinimapKind::Mountain:
                color = rgba8(92, 78, 62, 170);
                break;
            /*
             * 歩ける地形の細別（FH-09）。**役割は Floor と同じ**で、色だけを
             * 地形の系に寄せる（Frox の町と荒野が一色塗りにならないように）。
             * 送らないコアでは 1 ドットも変わらない。
             */
            case MinimapKind::Water:
                color = rgba8(38, 150, 200, 185);
                break;
            case MinimapKind::Tree:
                color = rgba8(34, 112, 52, 190);
                break;
            case MinimapKind::Grass:
                color = rgba8(96, 158, 84, 160);
                break;
            case MinimapKind::Lava:
                color = rgba8(220, 92, 30, 200);
                break;
            //! 雪は白系だが、階段（純白 230）より薄くして印が沈まないようにする。
            case MinimapKind::Snow:
                color = rgba8(212, 222, 238, 170);
                break;
            case MinimapKind::Door:
                color = rgba8(168, 168, 176, 200);
                break;
            case MinimapKind::Stairs:
                color = rgba8(255, 255, 255, 230);
                break;
            case MinimapKind::Item:
                color = rgba8(120, 210, 255, 220);
                break;
            case MinimapKind::Monster:
                color = rgba8(235, 60, 60, 230);
                break;
            case MinimapKind::Player:
                color = rgba8(255, 230, 90, 255);
                break;
            default:
                continue;
            }
            const RectPx dot{ ox + (rx * scale), oy + (ry * scale), scale, scale };
            /*
             * かつては**淡い暈**（同じ黄を濃さ 70 で 3×3）を先に敷いていた。1 ドットだと
             * 見失う、という当方の理屈だったが、**決めたことで外した**（2026-08-25
             * 「ミニマップの淡い黄色は不要。削除しよう」）。自分の印は濃さ 255 の純黄で、
             * 周りに同じ色の物が無いので暈が無くても読める。
             */
            if (kind == MinimapKind::Player) {
                if (state.first_person) {
                    /*
                     * **一人称のときだけ三角**（2026-08-11 に決めた）。見下ろしでは
                     * 向きという概念が無い（カメラは必ず北）ので、輝点のままにする。
                     */
                    draw_minimap_arrow(paint, static_cast<float>(dot.x) + (static_cast<float>(scale) * 0.5f),
                        static_cast<float>(dot.y) + (static_cast<float>(scale) * 0.5f),
                        state.first_person_facing, scale, color);
                    continue;
                }
            }
            paint.rect(dot, color);
        }
    }

    /*
     * **隅の印**。
     * 見下ろしの視点を 90° 回している間だけ出す。回転が 0 のときは**何も描かない**
     * ——回っていなければ北も進行方向も上で、印は画面の飾りにしかならない。
     *
     * **何を指すかは表示の仕方で変わる。**どちらの表示でも「上に来ているもの」は
     * 自明なので、印は**自明でないほう**を指す:
     *
     * | 表示 | 地図の上 | 印が指すもの | 角 |
     * |---|---|---|---|
     * | 絶対 | 北（固定） | **進行方向**（3D の絵が向いている方） | `+turn × 90°` |
     * | 相対 | 進行方向 | **北** | `−turn × 90°` |
     *
     * `draw_minimap_arrow` の `facing` は 0 で上向き・時計回りが正である。
     * 絶対で turn=1（東を見ている）なら進行方向は地図の右 ＝ +90°、
     * 相対で turn=1 なら北は地図の左 ＝ −90°。**符号が逆になるのは偶然ではなく、
     * 一方が他方の逆写像だからである。**
     */
    if (state.camera_turn != 0) {
        //! 矢は角そのもので回す（45° 刻み。地図の絵と違って格子に載る必要が無い）。
        const float sign = state.minimap_relative ? -1.f : 1.f;
        const float facing = sign * static_cast<float>(state.camera_turn) * kViewTurnStep;
        const float cx = static_cast<float>(box.x) + 9.f;
        const float cy = static_cast<float>(box.y) + 9.f;
        //! 下地を敷いてから矢を置く（地図の上に直に描くと、明るい床の上で見えなくなる）。
        paint.rect(RectPx{ box.x + 2, box.y + 2, 15, 15 }, rgba8(8, 10, 20, 190));
        draw_minimap_arrow(paint, cx, cy, facing, 4, rgba8(255, 255, 255, 235));
    }
    return box;
}

/*!
 * @brief Term の写しの 1 行の中で、**バイト範囲**が占める画素の矩形を出す。
 * @details 位置はどれも `TermMirrorLine::text_utf8` のバイト位置で来る（Term の列ではない）。
 * 全角は 1 セル 2 列だが UTF-8 では 3 バイトなので、列で持つと必ずずれる。
 * 幅は**実測**（`TextOverlay::measure`）以外に出しようが無い。
 */
RectPx span_rect(TextOverlay &text, const RectPx &body, int cell_h,
    const std::vector<TermMirrorLine> &lines, int line_index, int begin, int len)
{
    if ((line_index < 0) || (line_index >= static_cast<int>(lines.size())) || (len <= 0)) {
        return RectPx{};
    }
    const std::string &line = lines[static_cast<std::size_t>(line_index)].text_utf8;
    if ((begin < 0) || ((begin + len) > static_cast<int>(line.size()))) {
        return RectPx{};
    }
    const int x = body.x + text.measure(line.substr(0, static_cast<std::size_t>(begin)));
    const int w = text.measure(line.substr(static_cast<std::size_t>(begin), static_cast<std::size_t>(len)));
    return RectPx{ x, body.y + (line_index * cell_h), w, cell_h };
}

//! 選んでいる所へ重ねる枠。**下線は決定キーの 1 文字**（何を押せば決まるかを見せる）。
void draw_choice_frame(UiPaint &paint, const RectPx &span, const RectPx &key_span)
{
    if (span.empty()) {
        return;
    }
    paint.rect(span, PaintColor{ 0.95f, 0.85f, 0.30f, 0.22f });
    paint.frame(span, PaintColor{ 1.f, 0.88f, 0.35f, 0.95f }, 1);
    if (!key_span.empty()) {
        paint.rect(RectPx{ key_span.x, key_span.y + key_span.h - 2, key_span.w, 2 },
            PaintColor{ 1.f, 0.88f, 0.35f, 1.f });
    }
}

/*!
 * @brief コアの Term をそのまま写す（タイトル・birth・店・メニュー）。
 * @details 行の中の黄色（`highlight_*`）は**選んでいる項目そのもの**なので必ず出す。
 * 落とすと birth で「どれを選んでいるか見えない」画面になる（K-23）。
 */
void draw_term_mirror(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const GameFrame &frame,
    const UiCursors &cursors, bool over_backdrop)
{
    const bool full = frame.title_screen || frame.pre_game_menu;
    const RectPx area = full ? layout.term_full : layout.term_overlay;
    if (area.empty()) {
        return;
    }
    PanelStyle style = solid_panel_style();
    if (!full) {
        style.fill.a = 0.97f;
    }
    /*
     * **背面に絵があるなら下敷きを薄くする。**濃いままだと絵が完全に隠れ、
     * 「読み込めているのに出ていない」ことになる（1 度そう撮った）。
     * 0 にはしない。文字が読めなくなるので、薄い幕として残す。
     */
    if (over_backdrop) {
        style.fill.a = 0.30f;
        style.border.a = 0.f;
    }
    paint.panel(area, style);

    const RectPx body = panel_body(area, layout.cell_w);
    const int rows = std::max(1, body.h / layout.cell_h);
    const auto shown = std::min<std::size_t>(frame.menu_term_lines.size(), static_cast<std::size_t>(rows));
    for (std::size_t i = 0; i < shown; ++i) {
        const auto &line = frame.menu_term_lines[i];
        const int y = body.y + (static_cast<int>(i) * layout.cell_h);
        if (line.color_spans.empty()) {
            text.draw(body.x, y, line.text_utf8, text_from_term_color(line.color), body.w);
        } else {
            /*
             * 1 行に何色も出る行（カラーの設定の見本、オプションの色分けなど）。
             * 区間は行頭から行末まで隙間なく並んでいるので、**順に置いていくだけ**でよい。
             * 幅は実測する（全角は 1 セル 2 桁で、バイト数からは出ない）。
             */
            for (const auto &span : line.color_spans) {
                if ((span.begin < 0) || (span.len <= 0)
                    || (static_cast<std::size_t>(span.begin) + static_cast<std::size_t>(span.len)
                        > line.text_utf8.size())) {
                    continue;
                }
                const std::string part = line.text_utf8.substr(
                    static_cast<std::size_t>(span.begin), static_cast<std::size_t>(span.len));
                const int x = body.x
                    + text.measure(line.text_utf8.substr(0, static_cast<std::size_t>(span.begin)));
                text.draw(x, y, part, text_from_term_color(span.color), (body.x + body.w) - x);
            }
        }
        /*
         * 行の途中の黄色（K-23。birth はここだけで選択中の項目を示す）。
         * **区間で描いた行には要らない**（区間が同じ黄色をすでに置いている）。
         * 重ねて描くと同じ字が 2 度乗って太って見える。
         */
        if (line.color_spans.empty() && line.highlight_len > 0) {
            const std::string before = line.text_utf8.substr(0, static_cast<std::size_t>(line.highlight_begin));
            const std::string picked = line.text_utf8.substr(static_cast<std::size_t>(line.highlight_begin),
                static_cast<std::size_t>(line.highlight_len));
            const int x = body.x + text.measure(before);
            text.draw(x, y, picked, text_from_term_color(11), (body.x + body.w) - x); // 11 = YELLOW
        }
    }
    // 文字カーソル（コアが持っている。記憶 `hengband-core-owns-the-cursor`）。
    if ((frame.menu_term_curs_row >= 0) && (frame.menu_term_curs_row < static_cast<int>(shown))
        && (frame.menu_term_curs_col >= 0)) {
        const RectPx caret{ body.x + (frame.menu_term_curs_col * layout.cell_w),
            body.y + (frame.menu_term_curs_row * layout.cell_h) + layout.cell_h - 2, layout.cell_w, 2 };
        paint.rect(caret, PaintColor{ 1.f, 0.9f, 0.2f, 1.f });
    }

    /*
     * --- 選択のカーソル ---
     * **コアの `》` は全部に枠を描く**（K-20 の注）。コマンドメニューでサブメニューへ入ると
     * コアは親の行にも `》` を残すので、先頭 1 個だけ枠にすると「枠付きと `》` の 2 種類の
     * カーソルが同時に出る」ことになる。見た目を枠へ統一する。
     */
    for (const auto &cursor : frame.menu_core_cursors) {
        draw_choice_frame(paint,
            span_rect(text, body, layout.cell_h, frame.menu_term_lines, cursor.line_index, cursor.span_begin, cursor.span_len),
            RectPx{});
    }
    //! UI のカーソル（コアが持っていない画面＝店・建物）。
    if ((cursors.choice_index() >= 0) && frame.menu_core_cursors.empty()) {
        const MenuChoice &choice = cursors.choices()[static_cast<std::size_t>(cursors.choice_index())];
        draw_choice_frame(paint,
            span_rect(text, body, layout.cell_h, frame.menu_term_lines, choice.line_index, choice.span_begin, choice.span_len),
            span_rect(text, body, layout.cell_h, frame.menu_term_lines, choice.line_index, choice.key_begin, choice.key_len));
    }
    /*
     * はい／いいえ・数値入力が**元の画面に出ている**なら、そこへ枠を重ねる
     * （別窓を出さない。店ではプロンプト行がミラーにも出るので、別窓だと 2 か所に見える）。
     * `line_index < 0` ならミラーに出ていない画面なので、プロンプトの帯が受け持つ。
     */
    if (!cursors.prompt().choices.empty() && (cursors.prompt().line_index >= 0)) {
        const auto &choice = cursors.prompt().choices[static_cast<std::size_t>(cursors.prompt_index())];
        draw_choice_frame(paint,
            span_rect(text, body, layout.cell_h, frame.menu_term_lines, cursors.prompt().line_index, choice.begin, choice.len),
            RectPx{});
    }
    if (cursors.numeric().active && (cursors.numeric().line_index >= 0)) {
        //! 選んでいる桁 1 文字。値は右詰めなので、末尾から数えた位置になる。
        const int len = std::max(1, cursors.numeric().value_len);
        const int offset = len - 1 - cursors.numeric_place();
        if (offset >= 0) {
            draw_choice_frame(paint,
                span_rect(text, body, layout.cell_h, frame.menu_term_lines, cursors.numeric().line_index,
                    cursors.numeric().value_begin + offset, 1),
                RectPx{});
        }
    }
}

/*!
 * @brief 直近のメッセージ（帯）。**Sub1 が出ているときは矩形が空**なので何もしない。
 * @details 「メッセージの家は 1 つ」の当のところ。こことサブパネルの両方が出ると
 * 同じ行が 2 か所に並ぶ。
 */
void draw_message_bar(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const GameFrame &frame)
{
    const RectPx &bar = layout.message_bar;
    if (bar.empty() || frame.messages.empty()) {
        return; // 何も無いときはパネルごと出さない（3D の上に空の帯を置かない）
    }
    const int rows = std::max(1, bar.h / layout.cell_h);
    const int line_w = bar.w - (layout.cell_w * 2);
    //! **入らない字は折り返す**（Sub1 と同じ。切ると長いメッセージが読めない）。
    const std::vector<SubPanelLine> lines = recent_message_lines(text, frame.messages, line_w, rows);
    if (lines.empty()) {
        return; // 出す字が 1 行も無いならパネルも出さない（3D の上に空の帯を置かない）
    }

    PanelStyle style = overlay_panel_style();
    style.fill.a = 0.55f;
    style.border_px = 0;
    paint.panel(bar, style);

    int y = bar.y;
    for (const SubPanelLine &line : lines) {
        text.draw(bar.x + layout.cell_w, y, line.text_utf8, text_from_term_color(line.color), line_w);
        y += layout.cell_h;
    }
}

/*!
 * @brief プロンプト 1 行。**3 つの作りとも同じ場所に、必ず出す。**
 *
 * @details 答えないと先へ進まないものなので、パネルの開閉で消えてはいけない。
 * ここが受け持つのは**元の画面（Term の写し）に出ていないとき**だけである
 * （`line_index < 0`）。出ているならそちらへ枠を重ねるほうが、利用者から見て
 * 「同じものが 2 か所にある」状態にならない。
 *
 * はい／いいえと数値入力は**同時には出ない**（K-22 の注）ので、1 本の帯で足りる。
 */
void draw_prompt_bar(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const GameFrame &frame,
    const UiCursors &cursors)
{
    const RectPx &bar = layout.prompt_bar;
    if (bar.empty()) {
        return;
    }
    const bool show_numeric = cursors.numeric().active && (cursors.numeric().line_index < 0);
    const bool show_prompt = prompt_bar_shows_text(frame);
    if (!show_numeric && !show_prompt) {
        return;
    }
    PanelStyle style = overlay_panel_style();
    style.fill = PaintColor{ 0.10f, 0.09f, 0.04f, 0.88f };
    style.border = PaintColor{ 0.85f, 0.78f, 0.30f, 0.85f };
    paint.panel(bar, style);

    const int x = bar.x + layout.cell_w;
    const int limit = bar.w - (layout.cell_w * 2);
    if (show_numeric) {
        /*
         * 値は**0 詰めで**出す。桁カーソルは 1 文字に枠を重ねるので、
         * 「12」に対して 100 の位を選ぶと重ねる文字が無くなる。
         */
        const int digits = std::max(1, cursors.numeric().digits);
        std::string value = std::to_string(cursors.numeric().value);
        while (static_cast<int>(value.size()) < digits) {
            value.insert(value.begin(), '0');
        }
        const std::string head = cursors.numeric().prompt_utf8;
        const int head_w = text.draw(x, bar.y, head, TextColor{ 1.f, 1.f, 0.4f, 1.f }, limit);
        text.draw(x + head_w, bar.y, value, TextColor{ 1.f, 1.f, 1.f, 1.f }, limit - head_w);
        const int offset = digits - 1 - cursors.numeric_place();
        if ((offset >= 0) && (offset < digits)) {
            const int cx = x + head_w + text.measure(value.substr(0, static_cast<std::size_t>(offset)));
            const int cw = text.measure(value.substr(static_cast<std::size_t>(offset), 1));
            draw_choice_frame(paint, RectPx{ cx, bar.y, cw, layout.cell_h }, RectPx{});
        }
        return;
    }

    text.draw(x, bar.y, frame.prompt.text_utf8, TextColor{ 1.f, 1.f, 0.4f, 1.f }, limit);
    if (!cursors.prompt().choices.empty()) {
        const auto &choice = cursors.prompt().choices[static_cast<std::size_t>(cursors.prompt_index())];
        const std::string &line = frame.prompt.text_utf8;
        if ((choice.begin >= 0) && (choice.len > 0)
            && ((choice.begin + choice.len) <= static_cast<int>(line.size()))) {
            const int cx = x + text.measure(line.substr(0, static_cast<std::size_t>(choice.begin)));
            const int cw = text.measure(line.substr(static_cast<std::size_t>(choice.begin),
                static_cast<std::size_t>(choice.len)));
            draw_choice_frame(paint, RectPx{ cx, bar.y, cw, layout.cell_h }, RectPx{});
        }
    }
    if (!frame.prompt.footer_utf8.empty() && ((bar.y + (layout.cell_h * 2)) <= (bar.y + bar.h + layout.cell_h))) {
        text.draw(x, bar.y + layout.cell_h, frame.prompt.footer_utf8, kDimColor, limit);
    }
}

} // namespace

/*!
 * @brief 直近のメッセージを**折り返して**下詰めで積む（新しいものが下）。
 * @details 約束は `game_hud.h` の宣言に書いてある。ここは組み立て方だけ:
 * **新しいほうから**折って行数を数え、入り切らない**古いほう**を落とす。
 */
std::vector<SubPanelLine> recent_message_lines(TextOverlay &text, const std::vector<MessageEvent> &messages,
    int max_width, int rows)
{
    std::vector<SubPanelLine> lines;
    if (rows <= 0) {
        return lines;
    }
    for (std::size_t i = messages.size(); (i > 0) && (static_cast<int>(lines.size()) < rows); --i) {
        const MessageEvent &msg = messages[i - 1];
        std::vector<SubPanelLine> chunk;
        for (std::string &piece : wrap_utf8(text, msg.text_utf8, max_width)) {
            chunk.push_back(SubPanelLine{ std::move(piece), msg.color });
        }
        //! 古いほうを見ているので、束ごと**前へ**差し込む（束の中の順序は保つ）。
        lines.insert(lines.begin(), std::make_move_iterator(chunk.begin()),
            std::make_move_iterator(chunk.end()));
    }
    if (static_cast<int>(lines.size()) > rows) {
        //! 溢れたぶんは**上（古いほう）から**落とす。新しいメッセージは必ず見えること。
        lines.erase(lines.begin(), lines.begin() + (lines.size() - static_cast<std::size_t>(rows)));
    }
    return lines;
}

namespace {

/*!
 * @brief 2D アスキー地図の字の色。**真っ黒は床の灰へ持ち上げる**（SH-29。W4）。
 *
 * @details この枠の下敷きは**黒で塗ってある**（下の `paint.rect`）。だから
 * 色番号 0（`TERM_DARK`）で来た字は**黒地に黒**になり、**そこにあるのに見えない**。
 *
 * Sil-Q はこれを踏む。暗い床・盲目のあいだのマスを `TERM_DARK + TERM_SHADE`
 * （＝16。実体は `0x303030` の暗い灰。`silq/src/variable.c:222`）で塗るが、
 * アダプタが 16 色へ畳む所で上位ビットが落ちて **0＝真っ黒**になる
 * （`sq_shim.c`）。**結果、記憶している廊下が地図から消える。**
 *
 * ここで直すのは、これが**この枠だけの事情**だからである——3D では地形が
 * ボクセルで自前の照明を持つので同じことは起きないし、`light_level` は
 * 別に届いている。**線もプロトコルも触らない。**
 *
 * @note **色表そのものは尊重する。** 索引 0 を決め打ちで置き換えると、`&`（色の設定）や
 * pref の `V:` で `TERM_DARK` に見える色を入れた人の画が変わらなくなる。
 * 見るのは**引いた後の RGB** で、真っ黒に近いときだけ持ち上げる。
 */
TextColor ascii_map_text_color(std::uint8_t index)
{
    const RgbColor rgb = term_color_to_rgb(index);
    const std::uint8_t peak = std::max(rgb.r, std::max(rgb.g, rgb.b));
    if (peak > 0x10) {
        return TextColor{ static_cast<float>(rgb.r) / 255.f, static_cast<float>(rgb.g) / 255.f,
            static_cast<float>(rgb.b) / 255.f, 1.f };
    }
    //! `0x303030`。**コアが暗いマスに与えている色そのもの**（`TERM_SHADE` の実体）。
    constexpr float kFloor = 48.f / 255.f;
    return TextColor{ kFloor, kFloor, kFloor, 1.f };
}

} // namespace

RectPx draw_ascii_map_panel(UiPaint &paint, TextOverlay &text, const RectPx &panel, const GameFrame &frame)
{
    if (panel.empty() || frame.cells.empty()) {
        return RectPx{};
    }
    const int cw = text.cell_w();
    const int ch = text.cell_h();
    if ((cw <= 0) || (ch <= 0)) {
        return RectPx{};
    }
    const int cols = panel.w / cw;
    const int rows = panel.h / ch;
    if ((cols <= 0) || (rows <= 0)) {
        return RectPx{};
    }

    /*
     * **下敷きは黒**。本家の端末の見え方に寄せる（半透明にすると 3D の残りが透けて
     * 「アスキーなのに絵がある」という中途半端な画になる）。
     */
    paint.rect(panel, PaintColor{ 0.f, 0.f, 0.f, 1.f });

    //! マスの総数が枠を割り切らないぶんは**両側へ等分**（片側に寄せると地図が偏って見える）。
    const int off_x = panel.x + ((panel.w - (cols * cw)) / 2);
    const int off_y = panel.y + ((panel.h - (rows * ch)) / 2);
    //! `cam` は**窓の中心**（ヘッダの注記）。枠の中心へ合わせる。
    const int half_cols = cols / 2;
    const int half_rows = rows / 2;

    /*
     * **気づいていない敵の地**。
     *
     * 3D では足元のリングで見せているが、オリジナル表示には板が無い。コア（Sil-Q）自身の
     * 答えは `hilite_unwary` の**背景の塗り**なので、ここでもそれに倣う。
     *
     * @note 2026-08-19 の指示「アスキーの背景色は無くていい」は**地形**の話である
     * （`bg_color` を全マスに塗ると地図が四角く塗り分けられて読みにくい）。ここで塗るのは
     * **まだこちらに気づいていない敵のマスだけ**——ふつう 0〜数個で、しかも
     * このゲームの隠密の中核の情報である。要らなければこの塊を消せば元に戻る。
     */
    for (const MonsterAlertCell &alert : frame.monster_alerts) {
        if ((alert.level != static_cast<uint8_t>(MonsterAlert::Asleep))
            && (alert.level != static_cast<uint8_t>(MonsterAlert::Unwary))) {
            continue; //!< 気づかれている敵は塗らない（コアの `hilite_unwary` と同じ範囲）
        }
        const int col = (alert.gx - frame.cam_x) + half_cols;
        const int line = (alert.gy - frame.cam_y) + half_rows;
        if ((col < 0) || (col >= cols) || (line < 0) || (line >= rows)) {
            continue;
        }
        //! 眠り＝青・気づいていない＝水色。**リングと同じ色**（見比べたときに食い違わない）。
        const bool asleep = (alert.level == static_cast<uint8_t>(MonsterAlert::Asleep));
        const PaintColor tint = asleep ? PaintColor{ 0.10f, 0.14f, 0.42f, 1.f }
                                       : PaintColor{ 0.06f, 0.30f, 0.34f, 1.f };
        paint.rect(RectPx{ off_x + (col * cw), off_y + (line * ch), cw, ch }, tint);
    }

    /*
     * **照準のマス**（SH-30。W4）。3D では足元の黄の輪で出ているが（`entity_view.cpp:428`）、
     * この枠には受け皿が無く、**投げる・射る・`*` で狙っているマスが読めなかった**。
     *
     * 照準は**重ね書きではない**——`hilite_target` はコアでは Term のカーソルを動かす
     * だけで `print_rel` を通らないので、`map_overlay` には 1 度も出てこない
     * 位置がフレームに直に載っているので、ここで枠にする。
     *
     * **字より先に塗る。**後から塗ると狙ったマスの字が薄い幕に隠れて、いちばん読みたい所が
     * いちばん読みにくくなる。絵柄は選択中の項目と同じ（`draw_choice_frame`）——
     * 画面のどこでも「いま指しているもの」は黄の枠、で通す。
     */
    if ((frame.target_gx >= 0) && (frame.target_gy >= 0)) {
        const int col = (frame.target_gx - frame.cam_x) + half_cols;
        const int line = (frame.target_gy - frame.cam_y) + half_rows;
        if ((col >= 0) && (col < cols) && (line >= 0) && (line < rows)) {
            draw_choice_frame(paint, RectPx{ off_x + (col * cw), off_y + (line * ch), cw, ch }, RectPx{});
        }
    }

    for (const auto &cell : frame.cells) {
        if ((cell.feature_flags & CELL_FEAT_KNOWN) == 0u) {
            continue; //!< 未踏破は描かない（暗いのではなく「無い」）
        }
        const int col = (cell.gx - frame.cam_x) + half_cols;
        const int line = (cell.gy - frame.cam_y) + half_rows;
        if ((col < 0) || (col >= cols) || (line < 0) || (line >= rows)) {
            continue;
        }
        const int x = off_x + (col * cw);
        const int y = off_y + (line * ch);
        /*
         * **背景色は塗らない**（2026-08-19 に決めた「アスキーの背景色は無くていい」）。
         * コアは一部のマスに `bg_color` を持たせるが、塗ると地形が四角く塗り分けられて
         * 賑やかになる。字だけのほうが地図として読みやすい、という判断である。
         */
        const char glyph = cell.ascii_fallback;
        if ((glyph == '\0') || (glyph == ' ')) {
            continue;
        }
        const char one[2] = { glyph, '\0' };
        //! **真っ黒は床の灰へ**（SH-29。`ascii_map_text_color` の註記）。
        text.draw(x, y, one, ascii_map_text_color(cell.fg_color), cw);
    }

    /*
     * **一過性の重ね書き**。ダメージの数字・
     * 矢の飛跡・爆風・聞き耳の `*`・照準カーソルは、コアが地図のマスへ直に書いたもので
     * `cells` には入っていない。**地図を描いた後**に上から重ねる（これがそもそも
     * コア側での順序である）。出さないコアでは `map_overlay` が空なので 1 度も回らない。
     */
    for (const MapOverlayCell &over : frame.map_overlay) {
        if ((over.ascii == '\0') || (over.ascii == ' ')) {
            continue;
        }
        const int col = (over.gx - frame.cam_x) + half_cols;
        const int line = (over.gy - frame.cam_y) + half_rows;
        if ((col < 0) || (col >= cols) || (line < 0) || (line >= rows)) {
            continue;
        }
        const char one[2] = { over.ascii, '\0' };
        text.draw(off_x + (col * cw), off_y + (line * ch), one, ascii_map_text_color(over.color), cw);
    }
    return RectPx{ off_x, off_y, cols * cw, rows * ch };
}

bool frame_shows_map(const GameFrame &frame)
{
    /*
     * **重ねのメニューは「地図が出ている」**（`menu_over_map`。2026-09-01）。
     * コマンドメニューは地図を消さずに箱を重ねるだけなので、ここで偽を返すと
     * 立体を 1 枚も描かず、写しに載った地図が字で出る。
     */
    return frame.menu_term_lines.empty() || frame.menu_over_map;
}

int menu_choice_at(TextOverlay &text, const UiLayout &layout, const GameFrame &frame, int x, int y)
{
    if (frame.menu_choices.empty() || frame.menu_term_lines.empty()) {
        return -1;
    }
    //! 面積の計算は `draw_term_mirror` と同じ（全画面かオーバーレイか、枠の内側）。
    const bool full = frame.title_screen || frame.pre_game_menu;
    const RectPx area = full ? layout.term_full : layout.term_overlay;
    if (area.empty()) {
        return -1;
    }
    const RectPx body = panel_body(area, layout.cell_w);
    for (std::size_t i = 0; i < frame.menu_choices.size(); ++i) {
        const MenuChoice &choice = frame.menu_choices[i];
        const RectPx span = span_rect(text, body, layout.cell_h, frame.menu_term_lines,
            choice.line_index, choice.span_begin, choice.span_len);
        if (!span.empty() && span.contains(x, y)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool prompt_bar_shows_text(const GameFrame &frame)
{
    return !frame.prompt.text_utf8.empty() && (frame.prompt.line_index < 0);
}

int grip_at(const UiLayout &layout, int x, int y)
{
    for (int i = 0; i < kSubGripCount; ++i) {
        if (layout.grip[static_cast<std::size_t>(i)].contains(x, y)) {
            return i;
        }
    }
    return -1;
}

namespace {

/*!
 * @brief 掴んだ画素位置を 1/20 単位の**累計**へ戻し、`n` 本目の仕切りを動かす。
 * @param units 前から `count - 1` 本ぶんの 1/20 単位（動かす先）。
 * @return 値が変わったら true。
 * @details 仕切りの位置は「そこまでの合計」なので、動かすのは `units[n]` だけ
 * ——`units[n-1]` までの合計を引き、`units[n+1]` 以降が潰れないところで止める。
 * **下段（幅）と右列（高さ）で同じ関数を通す。**
 */
bool drag_divider(int at_px, int origin_px, int block_px, int n, int count, int units_min, int *units)
{
    if ((block_px <= 0) || (n < 0) || (n + 1 >= count)) {
        return false;
    }
    int before_units = 0;
    for (int i = 0; i < n; ++i) {
        before_units += units[i];
    }
    const int total = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(at_px - origin_px) * kSubBottomUnits / block_px)),
        before_units + units_min, kSubBottomUnits - (units_min * (count - 1 - n)));
    const int want = total - before_units;
    if (want == units[n]) {
        return false;
    }
    units[n] = want;
    return true;
}

} // namespace

bool drag_grip(const UiLayout &layout, int grip, int x, int y, SubSplit &split)
{
    if ((grip < 0) || (grip >= kSubGripCount)) {
        return false;
    }
    if (grip < kSubGripRight) {
        //! 下段の仕切り。ブロックの幅は**出ている下段の枚の合計**（余りは最後の枚が吸っている）。
        const RectPx &first = layout.sub[kSubBottomSlot];
        if (first.empty()) {
            return false;
        }
        int block_w = 0;
        for (int i = 0; i < split.bottom(); ++i) {
            block_w += layout.sub[static_cast<std::size_t>(kSubBottomSlot + i)].w;
        }
        return drag_divider(x, first.x, block_w, grip - kSubGripBottom, split.bottom(), kSubBottomW20Min,
            split.bottom_w20);
    }
    //! 右列の仕切り。こちらは**高さ**を動かす。
    const RectPx &top = layout.sub[kSubRightSlot];
    if (top.empty()) {
        return false;
    }
    int block_h = 0;
    for (int i = 0; i < split.right(); ++i) {
        block_h += layout.sub[static_cast<std::size_t>(kSubRightSlot + i)].h;
    }
    return drag_divider(y, top.y, block_h, grip - kSubGripRight, split.right(), kSubRightH20Min,
        split.right_h20);
}

void draw_game_ui(UiPaint &paint, TextOverlay &text, const UiLayout &layout,
    const GameFrame &frame, const HudState &state, const UiBackdrops *backdrops)
{
    if (ui_break_enabled("panels")) {
        return; // `HD2D_BREAK_UI=panels`。**検査が FAIL になるのが正しい。**
    }
    /*
     * タイトル・birth・死亡は**画面に地図が無い**。地図まわりを描くと、
     * 前のフロアの状態列やミニマップが残ったまま重なる。
     */
    if (!frame_shows_map(frame) && (frame.title_screen || frame.pre_game_menu)) {
        /*
         * タイトル画（K-47 相当）。**`title_screen` のときだけ**敷く。
         * `pre_game_menu` はキャラクター作成や死亡後も指すので、そこまで敷くと
         * ロール結果の裏に玉座の絵が出る。
         */
        const bool title_art = frame.title_screen && (backdrops != nullptr) && backdrops->has_title();
        if (title_art) {
            backdrops->painter->draw_cover(*backdrops->title, layout.term_full, 1.f);
        }
        draw_term_mirror(paint, text, layout, frame, state.cursors, title_art);
        return;
    }

    //! 上と同じ理由——**重なる作りかどうか**で決める（`Split` / `Tall` は濃く塗る）。
    const bool solid = !layout.sub_overlaid;
    PanelStyle panel_style = solid ? solid_panel_style() : overlay_panel_style();

    /*
     * --- パネルの背面（K-47 相当）---
     * **下敷きより先に、絵として直に描く。**`paint` は後でまとめて流れるので、
     * ここで描いた絵の上に下敷きが乗る。だから**絵があるときは下敷きを薄くする**
     * （濃いままだと絵が見えず「読めているのに出ていない」と誤診する）。
     */
    const bool draw_subs = !layout.sub_overlaid || state.subs_open;
    if (draw_subs && (backdrops != nullptr) && backdrops->has_panels()) {
        for (int i = 0; i < kUiSubPanels; ++i) {
            if (layout.sub[i].empty()) {
                continue; //!< `Tall` は右の 2 枚が空。**下のパネルと同じ条件で飛ばす**
            }
            const int slot = panel_backdrop_slot(i);
            const UiImage *const master = (slot == 0) ? backdrops->panel_bottom : backdrops->panel_right;
            if ((master == nullptr) || !master->valid()) {
                continue;
            }
            int sx = 0;
            int sy = 0;
            int sw = 0;
            int sh = 0;
            if (!panel_backdrop_source(layout, i, master->width(), master->height(), sx, sy, sw, sh)) {
                continue; // マスタが足りない。**伸ばさない**（下地色が残る）
            }
            backdrops->painter->draw_crop(*master, layout.sub[i], sx, sy, sw, sh, 1.f);
        }
        panel_style.fill.a = solid ? 0.45f : 0.35f;
    }

    // --- キャラクター状態の列（コアの左フレーム 13 桁の写し。K-28） ---
    if (!layout.status_col.empty() && !frame.status_col_lines.empty()) {
        paint.panel(layout.status_col, panel_style);
        draw_text_grid(text, panel_body(layout.status_col, layout.cell_w), std::string(),
            frame.status_col_lines, layout.cell_h);
    }

    // --- サブパネル 5 枚 ---
    if (draw_subs) {
        for (int i = 0; i < kUiSubPanels; ++i) {
            const RectPx &area = layout.sub[i];
            if (area.empty()) {
                continue;
            }
            paint.panel(area, panel_style);
            const RectPx body = panel_body(area, layout.cell_w);
            const SubPanelContent &content = frame.sub_panels[static_cast<std::size_t>(i)];
            if (content.kind >= 0) {
                draw_text_grid(text, body, content.title_utf8, content.lines, layout.cell_h);
            } else {
                draw_default_sub_panel(text, body, i, frame, layout.cell_h);
            }
        }
    }

    // --- メッセージとプロンプト（帯が空なら Sub1 が受け持っている） ---
    draw_message_bar(paint, text, layout, frame);
    draw_prompt_bar(paint, text, layout, frame, state.cursors);

    /*
     * --- 下段の境界の印 ---
     * **当たり判定と同じ矩形から描く**（`grip_at()` が見るのもこれ）。別に組み直すと、
     * 掴めるように見えて掴めない所ができる。掴んでいる間は濃くして、動かせることを見せる。
     */
    if (draw_subs) {
        for (int i = 0; i < 2; ++i) {
            const RectPx &grip = layout.grip[i];
            if (grip.empty()) {
                continue;
            }
            const bool held = (state.grabbed_grip == i);
            const int inset = held ? 0 : (grip.w / 3);
            paint.rect(RectPx{ grip.x + inset, grip.y + (grip.h / 4), grip.w - (inset * 2), grip.h / 2 },
                held ? PaintColor{ 0.95f, 0.85f, 0.35f, 0.85f } : PaintColor{ 0.55f, 0.60f, 0.70f, 0.40f });
        }
    }

    draw_bottom_bar(paint, text, layout, frame, state);
    //! HP/MP の棒は**全部の作り**で出す（2026-08-14 に決めた。置き場所は `ui_layout`）。
    draw_hud_gauges(paint, text, layout, frame);
    const RectPx minimap_box = draw_minimap(paint, layout, frame, state);

    /*
     * --- 階層（K-33。**この 1 か所だけ**に出す） ---
     * **ミニマップのすぐ下に置く。**`scene` の右端に寄せると、重なる作りでは
     * 窓の右端＝サブパネルの上に出てしまう（1 度そう出した）。ミニマップは 3 つの作りとも
     * 「空いている所の右上」に置いてあるので、その真下なら必ず地図の中である。
     */
    if (!frame.depth.text_utf8.empty() && !minimap_box.empty()) {
        const int w = text.measure(frame.depth.text_utf8);
        text.draw(minimap_box.x + minimap_box.w - w, minimap_box.y + minimap_box.h + 2,
            frame.depth.text_utf8, text_from_term_color(frame.depth.color));
    }

    /*
     * --- ゲーム中メニュー（店・持ち物）の小窓は**いちばん上** ---
     * **`frame_shows_map()` では判じない。** 重ねのメニュー（`menu_over_map`）は
     * 「地図が出ている」と答えるが、箱そのものはここで描く必要がある。
     * 見るのは「写しが積んであるか」だけでよい。
     */
    if (!frame.menu_term_lines.empty()) {
        draw_term_mirror(paint, text, layout, frame, state.cursors, false);
    }
}

} // namespace hd2d
