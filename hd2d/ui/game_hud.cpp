/*!
 * @file game_hud.cpp
 * @brief `game_hud.h` の実装。
 */
#include "ui/game_hud.h"
#include "app/ui_probe.h"

#include "frame/cell_feature_bits.h" //!< `CELL_FEAT_KNOWN`（2D アスキー地図が未踏破を飛ばす）
#include "i18n/lang.h"
#include "render/camera.h" //!< `rotate_screen_delta`（相対表示の回し方は世界とここで 1 つの定義）
#include "render/term_colors.h"
#include "render/text_overlay.h"
#include "ui/ui_paint.h"
#include "ui/modern_skin.h"
#include "ui/menu_frame.h"
#include "ui/menu_chrome.h"
#include "ui/menu_scale.h"
#include "ui/native_menu_view.h"
#include "ui/minimap_markers.h"
#include "ui/orb_hud.h" //!< `hp_label_of` / `sp_label_of`（玉と同じ名札）

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

TextColor text_from_term_color(std::uint8_t index, float alpha = 1.f,int theme=-1)
{
    if(theme==1&&!panel_transparent()) {
        if(index==0||index==1||index==9){auto ink=modern_skin(theme).text;ink.a=alpha;return ink;}
        const auto color=term_color_to_rgb(index);
        return {color.r/255.f*.55f,color.g/255.f*.55f,color.b/255.f*.55f,alpha};
    }
    const RgbColor rgb = term_color_to_rgb(index);
    return TextColor{ static_cast<float>(rgb.r) / 255.f, static_cast<float>(rgb.g) / 255.f,
        static_cast<float>(rgb.b) / 255.f, alpha };
}

const TextColor kTitleColor{ 0.72f, 0.80f, 0.95f, 1.f };
const TextColor kDimColor{ 0.62f, 0.66f, 0.72f, 1.f };

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
 * @details AH-25 の直しで**`color_spans` があれば桁ごとに塗り分ける**ようになった
 * （手本は menu_term_lines の同じ仕組み。下の menu overlay の描画）。空のときは
 * これまでどおり `SubPanelLine::color` の 1 行 1 色に落ちるので、Angband 以外の
 * コア（`color_spans` を埋めない）の見た目は 1 ドットも変わらない。
 */
void draw_text_grid(TextOverlay &text, const RectPx &body, const std::string &head,
    const std::vector<SubPanelLine> &lines, int cell_h,int theme=-1)
{
    if (body.empty()) {
        return;
    }
    int y = body.y;
    if (!head.empty()) {
        text.draw(body.x, y, head,theme>=0&&!panel_transparent()?modern_skin(theme).strong:kTitleColor, body.w);
        y += cell_h;
    }
    for (const auto &line : lines) {
        if ((y + cell_h) > (body.y + body.h)) {
            break; // 入り切らないぶんは出さない（枠の外へは描かない）
        }
        if (line.color_spans.empty()) {
            text.draw(body.x, y, line.text_utf8, text_from_term_color(line.color,1.f,theme), body.w);
        } else {
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
                text.draw(x, y, part, text_from_term_color(span.color,1.f,theme), (body.x + body.w) - x);
            }
        }
        y += cell_h;
    }
}

//! 文字列だけの一覧（UI 既定のサブパネル）。
void draw_plain_lines(TextOverlay &text, const RectPx &body, const std::string &head,
    const std::vector<std::string> &lines, int cell_h,int theme=-1)
{
    std::vector<SubPanelLine> converted;
    converted.reserve(lines.size());
    for (const auto &line : lines) {
        converted.push_back(SubPanelLine{ line, 9 }); // 9 = L_WHITE
    }
    draw_text_grid(text, body, head, converted, cell_h,theme);
}

/*!
 * @brief UI 既定（`kind < 0`）の枚に何を出すか。
 * @details 既存 UI と同じ割り当て（Sub1 = メッセージ / Sub2 = 装備 / Sub3 = 視界の敵 /
 * Sub4 = キャラクター / Sub5 = 持ち物）。**遊ぶ人がコアのサブウインドウを割り当てたら
 * そちらが優先**されるので、ここは「割り当てが無いときの中身」でしかない。
 */
void draw_default_sub_panel(TextOverlay &text, const RectPx &body, int index,
    const GameFrame &frame, int cell_h,int theme=-1)
{
    switch (index) {
    case 0: { // Sub1: 直近のメッセージ（新しいものが下・**入らない字は折り返す**）
        const int rows = std::max(1, body.h / cell_h);
        int y = body.y;
        for (const SubPanelLine &line : recent_message_lines(text, frame.messages, body.w, rows)) {
            text.draw(body.x, y, line.text_utf8, text_from_term_color(line.color,1.f,theme), body.w);
            y += cell_h;
        }
        break;
    }
    case 1:
        draw_plain_lines(text, body, i18n::tr("hd2d.ui.game-hud.equipment"), frame.sub2_lines, cell_h,theme);
        break;
    case 2:
        draw_plain_lines(text, body, i18n::tr("hd2d.ui.game-hud.enemies-in-view"), frame.sub3_lines, cell_h,theme);
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
        draw_plain_lines(text, body, std::string(), lines, cell_h,theme);
        break;
    }
    case 4:
        draw_plain_lines(text, body, i18n::tr("hd2d.ui.game-hud.inventory"), frame.sub5_lines, cell_h,theme);
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
            if (run.color_spans.empty()) {
                text.draw(x, body.y, run.text_utf8, text_from_term_color(run.color), (body.x + body.w) - x);
                continue;
            }
            /*
             * AH-25。最下段は速度・学習・階層などが右端の決まった桁に別の色で出る
             * （`TermTextRun` の設計ノート）——`color_spans` があれば桁ごとに塗り分ける。
             * 手本は `draw_text_grid()`（状態列）と menu overlay の同じ仕組み。
             */
            for (const auto &span : run.color_spans) {
                if ((span.begin < 0) || (span.len <= 0)
                    || (static_cast<std::size_t>(span.begin) + static_cast<std::size_t>(span.len)
                        > run.text_utf8.size())) {
                    continue;
                }
                const std::string part = run.text_utf8.substr(
                    static_cast<std::size_t>(span.begin), static_cast<std::size_t>(span.len));
                const int span_x = x + text.measure(run.text_utf8.substr(0, static_cast<std::size_t>(span.begin)));
                text.draw(span_x, body.y, part, text_from_term_color(span.color), (body.x + body.w) - span_x);
            }
        }
    }
    int y = body.y + layout.cell_h;
    /*
     * 修-14：左のコントローラーの案内と右のキーの案内は**同じ行**に出るので、狭い窓では
     * 字の幅の合計が帯の幅を超えて重なる。帯の高さは増やさない（増やすと地図が縮む）ので、
     * 入らないときは**キーの案内（UI だけが持つ情報）を右端へ残し**、コントローラーの案内を
     * 残りの幅で切る（残りが 4 桁ぶんも無ければ出さない。13 入力ぶんは下の行にも出る）。
     * キーの案内だけで帯の幅を超えるなら、幅で切って左端から出す。
     */
    const int key_w = state.ui_key_hint.empty() ? 0 : std::min(text.measure(state.ui_key_hint), body.w);
    if (!frame.controller_hint.empty()) {
        const int room = bottom_hint_room(
            body.w, text.measure(frame.controller_hint), key_w, layout.cell_w * 2, layout.cell_w * 4);
        if (room > 0) {
            text.draw(body.x, y, frame.controller_hint, kDimColor, room);
        }
    }
    //! UI が横取りしているキーは**右端に寄せて**出す（コアの案内と混ざらないように）。
    if (key_w > 0) {
        text.draw((body.x + body.w) - key_w, y, state.ui_key_hint, kDimColor, key_w);
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

/*!
 * @brief 名前の 1 行（`名前  LV n  AU n`）。
 * @details HP/MP の棒は玉（`draw_orb_hud`）に置き換わったので、
 * `UiLayout::hud` に残るのはこの 1 行だけである。
 */
void draw_hud_name(TextOverlay &text, const UiLayout &layout, const GameFrame &frame)
{
    const RectPx &hud = layout.hud;
    if (hud.empty()) {
        return;
    }
    char buf[96]{};
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
    ui_probe().hud_name(frame.hud.name, frame.hud.level, frame.hud.gold); //!< 検査用の問い合わせ：名前の行に出した値
    text.draw(hud.x, hud.y, who, kDimColor, hud.w);
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
            const auto marker=frame.minimap.marker_at(x,y);
            if(kind!=MinimapKind::Player&&marker!=MinimapMarker::None) {
                paint.rect({ox+rx*scale,oy+ry*scale,scale,scale},minimap_marker_color(marker,fade(1.f)));continue;
            }
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
void draw_choice_frame(UiPaint &paint, const RectPx &span, const RectPx &key_span,int theme=-1)
{
    if (span.empty()) {
        return;
    }
    if(theme>=0) {
        paint_modern_selection(paint,span,theme);
        if(!key_span.empty())paint.rect({key_span.x,key_span.y+key_span.h-2,key_span.w,2},modern_skin(theme).trim);
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
    const UiCursors &cursors, bool over_backdrop,int theme)
{
    const bool full = frame.title_screen || frame.pre_game_menu;
    const RectPx area = menu_text_area(layout,full,theme);
    if (area.empty()) {
        return;
    }
    const UiProbeLayer probe_layer("term");
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
    if(theme>=0)paint_modern_window(paint,area,theme);else paint.panel(area, style);
    const auto shown_text=theme>=0?modern_menu_text(frame):std::vector<std::string>{};
    const auto ink=[&](uint8_t color) {
        if(theme<0)return text_from_term_color(color);
        if(color==0||color==1||color==9)return modern_skin(theme).text;
        auto result=text_from_term_color(color);
        if(theme==1){result.r*=.55f;result.g*=.55f;result.b*=.55f;}
        return result;
    };

    const RectPx body = panel_body(area, layout.cell_w);
    const int rows = std::max(1, body.h / layout.cell_h);
    const auto shown = std::min<std::size_t>(frame.menu_term_lines.size(), static_cast<std::size_t>(rows));
    if(theme>=0 && (!frame.menu_choices.empty()||!frame.menu_core_cursors.empty()))
        paint_modern_row_rules(paint,{body.x,body.y,body.w,static_cast<int>(shown)*layout.cell_h},layout.cell_h,theme);
    for (std::size_t i = 0; i < shown; ++i) {
        const auto &line = frame.menu_term_lines[i];
        const auto &label=theme>=0?shown_text[i]:line.text_utf8;
        const int y = body.y + (static_cast<int>(i) * layout.cell_h);
        if (line.color_spans.empty()) {
            text.draw(body.x, y, label, ink(line.color), body.w);
        } else {
            /*
             * 1 行に何色も出る行（カラーの設定の見本、オプションの色分けなど）。
             * 区間は行頭から行末まで隙間なく並んでいるので、**順に置いていくだけ**でよい。
             * 幅は実測する（全角は 1 セル 2 桁で、バイト数からは出ない）。
             */
            for (const auto &span : line.color_spans) {
                if ((span.begin < 0) || (span.len <= 0)
                    || (static_cast<std::size_t>(span.begin) + static_cast<std::size_t>(span.len)
                        > label.size())) {
                    continue;
                }
                const std::string part = label.substr(
                    static_cast<std::size_t>(span.begin), static_cast<std::size_t>(span.len));
                const int x = body.x
                    + text.measure(label.substr(0, static_cast<std::size_t>(span.begin)));
                text.draw(x, y, part, ink(span.color), (body.x + body.w) - x);
            }
        }
        /*
         * 行の途中の黄色（K-23。birth はここだけで選択中の項目を示す）。
         * **区間で描いた行には要らない**（区間が同じ黄色をすでに置いている）。
         * 重ねて描くと同じ字が 2 度乗って太って見える。
         */
        if (line.color_spans.empty() && line.highlight_len > 0) {
            const std::string before = label.substr(0, static_cast<std::size_t>(line.highlight_begin));
            const std::string picked = label.substr(static_cast<std::size_t>(line.highlight_begin),
                static_cast<std::size_t>(line.highlight_len));
            const int x = body.x + text.measure(before);
            text.draw(x, y, picked, theme>=0?modern_skin(theme).selected:text_from_term_color(11), (body.x + body.w) - x); // 11 = YELLOW
        }
    }
    if (ui_probe().enabled()) {
        //! 検査用の問い合わせ：Term の写し（店・メニュー・タイトル）の窓と、選択肢の矩形（クリックの判定と同じ関数）。
        ProbeWindow w;
        w.kind = "term";
        w.stage = full ? "full" : "overlay";
        for (std::size_t i = 0; i < shown; ++i) {
            ProbeItem item;
            item.label = frame.menu_term_lines[i].text_utf8;
            w.items.push_back(std::move(item));
        }
        w.has_rect = true;
        w.rect = ProbeRect{ area.x, area.y, area.w, area.h };
        w.prompt = frame.prompt.text_utf8;
        if (!frame.menu_core_cursors.empty()) {
            w.selected = frame.menu_core_cursors.back().line_index;
        } else if (cursors.choice_index() >= 0) {
            w.selected = cursors.choices()[static_cast<std::size_t>(cursors.choice_index())].line_index;
        }
        ui_probe().window(std::move(w));
        for (std::size_t i = 0; i < frame.menu_choices.size(); ++i) {
            const MenuChoice &choice = frame.menu_choices[i];
            const RectPx span = span_rect(text, body, layout.cell_h, frame.menu_term_lines, choice.line_index,
                choice.span_begin, choice.span_len);
            if (span.empty()) {
                continue;
            }
            std::string label = choice.label_utf8;
            if (label.empty() && (choice.line_index >= 0) && (choice.line_index < static_cast<int>(frame.menu_term_lines.size()))) {
                const std::string &line = frame.menu_term_lines[static_cast<std::size_t>(choice.line_index)].text_utf8;
                if ((choice.span_begin >= 0) && (static_cast<std::size_t>(choice.span_begin) <= line.size())) {
                    label = line.substr(static_cast<std::size_t>(choice.span_begin), static_cast<std::size_t>(choice.span_len));
                }
            }
            ui_probe().button(label, ProbeRect{ span.x, span.y, span.w, span.h }, choice.available,
                choice.selected || (static_cast<int>(i) == cursors.choice_index()), "term-choice");
        }
    }
    // 文字カーソル（コアが持っている。記憶 `hengband-core-owns-the-cursor`）。
    if ((frame.menu_term_curs_row >= 0) && (frame.menu_term_curs_row < static_cast<int>(shown))
        && (frame.menu_term_curs_col >= 0)) {
        const RectPx caret{ body.x + (frame.menu_term_curs_col * layout.cell_w),
            body.y + (frame.menu_term_curs_row * layout.cell_h) + layout.cell_h - 2, layout.cell_w, 2 };
        paint.rect(caret, theme>=0?modern_skin(theme).trim:PaintColor{ 1.f, 0.9f, 0.2f, 1.f });
    }

    /*
     * --- 選択のカーソル ---
     * **コアの `》` は全部に枠を描く**（K-20 の注）。コマンドメニューでサブメニューへ入ると
     * コアは親の行にも `》` を残すので、先頭 1 個だけ枠にすると「枠付きと `》` の 2 種類の
     * カーソルが同時に出る」ことになる。見た目を枠へ統一する。
     */
    for (size_t index=0;index<frame.menu_core_cursors.size();++index) {
        if(theme>=0 && index+1<frame.menu_core_cursors.size())continue; // Only the active child is selected in the modern view.
        const auto &cursor=frame.menu_core_cursors[index];
        draw_choice_frame(paint,
            span_rect(text, body, layout.cell_h, frame.menu_term_lines, cursor.line_index, cursor.span_begin, cursor.span_len),
            RectPx{},theme);
    }
    //! UI のカーソル（コアが持っていない画面＝店・建物）。
    if ((cursors.choice_index() >= 0) && frame.menu_core_cursors.empty()) {
        const MenuChoice &choice = cursors.choices()[static_cast<std::size_t>(cursors.choice_index())];
        draw_choice_frame(paint,
            span_rect(text, body, layout.cell_h, frame.menu_term_lines, choice.line_index, choice.span_begin, choice.span_len),
            span_rect(text, body, layout.cell_h, frame.menu_term_lines, choice.line_index, choice.key_begin, choice.key_len),theme);
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
            RectPx{},theme);
    }
    if (cursors.numeric().active && (cursors.numeric().line_index >= 0)) {
        //! 選んでいる桁 1 文字。値は右詰めなので、末尾から数えた位置になる。
        const int len = std::max(1, cursors.numeric().value_len);
        const int offset = len - 1 - cursors.numeric_place();
        if (offset >= 0) {
            draw_choice_frame(paint,
                span_rect(text, body, layout.cell_h, frame.menu_term_lines, cursors.numeric().line_index,
                    cursors.numeric().value_begin + offset, 1),
                RectPx{},theme);
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
    const UiProbeLayer probe_layer("message");
    if (ui_probe().enabled()) {
        std::vector<std::string> shown;
        for (const SubPanelLine &line : lines) {
            shown.push_back(line.text_utf8); //!< 検査用の問い合わせ：メッセージ行に出している字
        }
        ui_probe().messages(std::move(shown));
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
 * （`line_index < 0`）。出ているならそちらへ枠を重ねるほうが、遊ぶ人から見て
 * 「同じものが 2 か所にある」状態にならない。
 *
 * はい／いいえと数値入力は**同時には出ない**（K-22 の注）ので、1 本の帯で足りる。
 */
void draw_prompt_bar(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const GameFrame &frame,
    const UiCursors &cursors,int theme)
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
    const UiProbeLayer probe_layer("prompt");
    if (ui_probe().enabled()) {
        //! 検査用の問い合わせ：はい／いいえ・数量の問い（新しい UI の窓が覆っていないとき）。
        ProbeWindow w;
        if (show_numeric) {
            w.kind = "number-prompt";
            w.title = cursors.numeric().prompt_utf8;
            w.prompt = cursors.numeric().prompt_utf8;
            w.has_rect = true;
            w.rect = ProbeRect{ bar.x, bar.y, bar.w, bar.h };
            w.has_numeric = true;
            w.numeric_value = cursors.numeric().value;
            w.numeric_min = cursors.numeric().min;
            w.numeric_max = cursors.numeric().max;
        } else {
            w.kind = "prompt";
            w.title = frame.prompt.text_utf8;
            w.has_rect = true;
            w.rect = ProbeRect{ bar.x, bar.y, bar.w, bar.h };
            for (const auto &choice : cursors.prompt().choices) {
                ProbeItem item;
                item.id = std::string(1, static_cast<char>(choice.key));
                item.label = choice.label_utf8;
                w.items.push_back(std::move(item));
            }
            w.prompt = frame.prompt.text_utf8;
            w.selected = cursors.prompt().choices.empty() ? -1 : cursors.prompt_index();
        }
        ui_probe().window(std::move(w));
    }
    /*
     * **下敷きは字のある所だけ**（決めたこと 2026-10-09「文字があるところだけ下敷きがあればいい。
     * みぎはじまである必要がない」）。帯の幅（`layout.prompt_bar`）は字の置き場所の上限で、
     * 下敷きは字の幅＋左右 1 マスに縮める。
     */
    const auto prompt_last_line = [&]() {
        const auto at = frame.prompt.text_utf8.rfind('\n');
        return (at == std::string::npos) ? frame.prompt.text_utf8 : frame.prompt.text_utf8.substr(at + 1);
    };
    int content_w = 0;
    if (show_numeric) {
        const std::size_t digits = static_cast<std::size_t>(std::max(1, cursors.numeric().digits));
        const std::size_t value_len = std::max(digits, std::to_string(cursors.numeric().value).size());
        content_w = text.measure(cursors.numeric().prompt_utf8) + text.measure(std::string(value_len, '0'));
    } else {
        content_w = text.measure(prompt_last_line());
    }
    const RectPx box{ bar.x, bar.y, std::min(bar.w, content_w + (layout.cell_w * 2)), bar.h };
    PanelStyle style = overlay_panel_style();
    style.fill = PaintColor{ 0.10f, 0.09f, 0.04f, 0.88f };
    style.border = PaintColor{ 0.85f, 0.78f, 0.30f, 0.85f };
    if(theme>=0)paint_modern_window(paint,box,theme,true);else paint.panel(box, style);

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
        const int head_w = text.draw(x, bar.y, head, theme>=0?modern_skin(theme).text:TextColor{ 1.f, 1.f, 0.4f, 1.f }, limit);
        text.draw(x + head_w, bar.y, value, theme>=0?modern_skin(theme).strong:TextColor{ 1.f, 1.f, 1.f, 1.f }, limit - head_w);
        const int offset = digits - 1 - cursors.numeric_place();
        if ((offset >= 0) && (offset < digits)) {
            const int cx = x + head_w + text.measure(value.substr(0, static_cast<std::size_t>(offset)));
            const int cw = text.measure(value.substr(static_cast<std::size_t>(offset), 1));
            draw_choice_frame(paint, RectPx{ cx, bar.y, cw, layout.cell_h }, RectPx{},theme);
        }
        return;
    }

    /*
     * **箱の最終行だけを帯へ出す**。FroxComposband の
     * `-more-` は箱の全行（最大 10 行）を `\n` でつないで `prompt.text_utf8` へ渡す
     * （`frox/adapter/fc_menu.cpp::fill_row0_prompt()`。店・知識など実在のメニューの
     * 中で全画面ダイアログへ渡す `modern_prompt_text()` はこの複数行をそのまま使う）。
     * この 1 行の帯は改行を折り返さないので、そのまま出すと `-more-` を含む末尾の行が
     * 途中の行に埋もれて読めなくなる——`-more-` の合図は必ず最終行に付く
     * （`message_more_suffix()`）ので、帯には最終行だけを出せば足りる。
     */
    const std::string prompt_line = prompt_last_line();
    /*
     * 選択肢を横に並べた帯が幅を超えるときは、選んでいる選択肢が見える範囲に絞る（`fit_prompt_band()`）。
     * 絞るのは 1 行だけの帯で、入るときは従来どおり（行をそのまま描き、枠は選択肢の位置）。
     */
    PromptBandView band;
    if (!cursors.prompt().choices.empty() && (prompt_line.size() == frame.prompt.text_utf8.size())) {
        std::vector<std::pair<int, int>> ranges;
        for (const auto &c : cursors.prompt().choices) {
            ranges.emplace_back(c.begin, c.len);
        }
        band = fit_prompt_band(prompt_line, ranges, cursors.prompt_index(), limit,
            [&text](const std::string &s) { return text.measure(s); });
    }
    text.draw(x, bar.y, band.cut ? band.text : prompt_line, theme>=0?modern_skin(theme).text:TextColor{ 1.f, 1.f, 0.4f, 1.f }, limit);
    if (band.cut) {
        draw_choice_frame(paint, RectPx{ x + band.frame_x, bar.y, band.frame_w, layout.cell_h }, RectPx{},theme);
    } else if (!cursors.prompt().choices.empty()) {
        const auto &choice = cursors.prompt().choices[static_cast<std::size_t>(cursors.prompt_index())];
        const std::string &line = frame.prompt.text_utf8;
        if ((choice.begin >= 0) && (choice.len > 0)
            && ((choice.begin + choice.len) <= static_cast<int>(line.size()))) {
            const int cx = x + text.measure(line.substr(0, static_cast<std::size_t>(choice.begin)));
            const int cw = text.measure(line.substr(static_cast<std::size_t>(choice.begin),
                static_cast<std::size_t>(choice.len)));
            draw_choice_frame(paint, RectPx{ cx, bar.y, cw, layout.cell_h }, RectPx{},theme);
        }
    }
    if (!frame.prompt.footer_utf8.empty() && ((bar.y + (layout.cell_h * 2)) <= (bar.y + bar.h + layout.cell_h))) {
        text.draw(x, bar.y + layout.cell_h, frame.prompt.footer_utf8, theme>=0?modern_skin(theme).muted:kDimColor, limit);
    }
}

} // namespace

int bottom_hint_room(int body_w, int hint_w, int key_w, int gap, int min_room)
{
    if ((hint_w <= 0) || (body_w <= 0)) {
        return 0;
    }
    if ((key_w <= 0) || ((hint_w + gap + key_w) <= body_w)) {
        return body_w; // 入る（右端の案内が無い場合も含む）。従来どおり帯の幅いっぱいで切る
    }
    const int room = body_w - key_w - gap;
    return (room >= min_room) ? room : 0;
}

PromptBandView fit_prompt_band(const std::string &line, const std::vector<std::pair<int, int>> &ranges, int selected,
    int limit, const std::function<int(const std::string &)> &measure)
{
    PromptBandView view;
    view.text = line;
    const int n = static_cast<int>(ranges.size());
    if ((limit <= 0) || (selected < 0) || (selected >= n) || (measure(line) <= limit)) {
        return view;
    }
    //! 選択肢の範囲の検算。壊れた並びのときは絞らない（従来どおり右を切るだけ）。
    int prev_end = 0;
    for (const auto &r : ranges) {
        if ((r.first < prev_end) || (r.second <= 0) || (static_cast<std::size_t>(r.first + r.second) > line.size())) {
            return view;
        }
        prev_end = r.first + r.second;
    }
    //! `[ラベル]` の括弧ごと 1 つの区切りにする（括弧が無い帯はラベルだけ）。
    std::vector<std::pair<int, int>> seg(static_cast<std::size_t>(n)); //!< 区切りの [始まり, 終わり)
    for (int i = 0; i < n; ++i) {
        int b = ranges[static_cast<std::size_t>(i)].first;
        int e = b + ranges[static_cast<std::size_t>(i)].second;
        if ((b > 0) && (line[static_cast<std::size_t>(b - 1)] == '[')) {
            --b;
        }
        if ((static_cast<std::size_t>(e) < line.size()) && (line[static_cast<std::size_t>(e)] == ']')) {
            ++e;
        }
        seg[static_cast<std::size_t>(i)] = { b, e };
    }
    const std::string full_head = line.substr(0, static_cast<std::size_t>(seg[0].first));
    const auto build = [&](const std::string &head, int f, int k) {
        std::string s = head;
        if (f > 0) {
            s += "... ";
        }
        s += line.substr(static_cast<std::size_t>(seg[static_cast<std::size_t>(f)].first),
            static_cast<std::size_t>(seg[static_cast<std::size_t>(k)].second - seg[static_cast<std::size_t>(f)].first));
        if (k < (n - 1)) {
            s += " ...";
        }
        return s;
    };
    int pick_f = selected;
    int pick_k = selected;
    std::string pick_head = std::string();
    bool found = false;
    const std::string no_head;
    for (const std::string *head : { &full_head, &no_head }) {
        for (int f = 0; (f <= selected) && !found; ++f) {
            for (int k = n - 1; k >= selected; --k) {
                if (measure(build(*head, f, k)) <= limit) {
                    pick_f = f;
                    pick_k = k;
                    pick_head = *head;
                    found = true;
                    break;
                }
            }
        }
        if (found) {
            break;
        }
    }
    if (!found) { //!< 選んでいる 1 つだけでも入らない。見出しを捨てて、その選択肢を頭から出す（右は描画側が切る）
        pick_f = selected;
        pick_k = selected;
    }
    view.text = build(pick_head, pick_f, pick_k);
    const int sel_begin = ranges[static_cast<std::size_t>(selected)].first;
    const int sel_start = seg[static_cast<std::size_t>(pick_f)].first;
    view.frame_x = measure(pick_head + ((pick_f > 0) ? "... " : "")
        + line.substr(static_cast<std::size_t>(sel_start), static_cast<std::size_t>(sel_begin - sel_start)));
    view.frame_w = measure(line.substr(static_cast<std::size_t>(sel_begin),
        static_cast<std::size_t>(ranges[static_cast<std::size_t>(selected)].second)));
    view.cut = true;
    return view;
}

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

RectPx draw_ascii_map_panel(UiPaint &paint, TextOverlay &text, const RectPx &panel, const GameFrame &frame, TargetingUi *targeting, double time)
{
    if(targeting)targeting->map_projection={};
    const UiProbeLayer probe_layer("map"); //!< 1 マス 1 字の描きは問い合わせに載せない（`map` の欄で答える）
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
    if (!frame.targeting.active && !frame.targeting.preview_only && (frame.target_gx >= 0) && (frame.target_gy >= 0)) {
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
    if(targeting) {
        targeting->map_projection={off_x,off_y,cols,rows,cw,ch,frame.cam_x,frame.cam_y};
        targeting->draw_map(paint,targeting->map_projection,time);
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

bool modern_window_keeps_hud(const GameFrame &frame)
{
    return !frame.title_screen && !frame.pre_game_menu && !frame.cells.empty();
}

int menu_choice_at(TextOverlay &text, const UiLayout &layout, const GameFrame &frame, int x, int y,int theme,const HudState *state)
{
    if(theme>=0&&state&&state->native_view_valid) {
        const float scale=menu_font_scale(state->modern_font_level),old=text.layout_scale();text.set_layout_scale(scale);
        const auto logical=menu_logical_layout(layout,scale,&text);
        const int result=menu_choice_at(text,logical,state->native_view,int(x/scale),int(y/scale),theme,nullptr);
        text.set_layout_scale(old);return result;
    }
    if (frame.menu_choices.empty() || frame.menu_term_lines.empty()) {
        return -1;
    }
    //! 面積の計算は `draw_term_mirror` と同じ（全画面かオーバーレイか、枠の内側）。
    const bool full = frame.title_screen || frame.pre_game_menu;
    const RectPx area = menu_text_area(layout,full,theme);
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

/*!
 * @brief 左のステータス欄の天候（アイコンと、雨・風・気温の値）。 
 * @param body 状態欄の本文の矩形（字の倍率を掛けた後の座標）
 * @param used_lines すでに字が並んでいる行数（天候はその下に置く）
 * @details コアが天候を申告したときだけ描く（`frame.weather.declared`）。アイコンは 3 行ぶんの高さの正方形、
 * 横に「雨 8」「風 3」「気温 9」の 3 行。気温の字の色は原作の `prt_weather()` と同じ（段 0 が薄い赤・1 が黄・2 が白・3 が水色）。
 * 置く場所は**状態欄の最後の字の 1 行下**。入りきらなければ描かない（枠の外へは描かない）。
 * アイコンは画像なので、先に下敷き（`paint`）を流してから描く（下敷きは後でまとめて流れるため、流さないと下敷きの下に隠れる）。
 */
void draw_weather_status(UiPaint &paint, TextOverlay &text, const RectPx &body, int cell_h, int used_lines,
    const GameFrame &frame, const OrbHudImages &images, float scale, int theme)
{
    const WeatherState &w = frame.weather;
    if (!w.declared || body.empty()) {
        return;
    }
    const int block_h = cell_h * 3;
    const int y0 = body.y + ((used_lines + 1) * cell_h);
    if ((y0 + block_h) > (body.y + body.h)) {
        return;
    }
    const int icon_size = block_h;
    if (images.has_weather_icons()) {
        paint.flush();
        MenuImageScaleScope image_scope(*images.painter, nullptr, scale);
        const UiImage &sheet = *images.weather_icons;
        const int cell = std::max(1, sheet.width() / kWeatherIconCount);
        const int icon = std::clamp(weather_icon_index(w), 0, kWeatherIconCount - 1);
        const float tw = static_cast<float>(sheet.width());
        const float th = static_cast<float>(sheet.height());
        //! 半画素だけ内側から取る（縮めたとき隣のセルが縁に混ざらない）。
        const float u0 = (static_cast<float>(icon * cell) + 0.5f) / tw;
        const float u1 = (static_cast<float>((icon + 1) * cell) - 0.5f) / tw;
        images.painter->draw_region(sheet, RectPx{ body.x, y0, icon_size, icon_size }, u0, 0.5f / th, u1,
            (static_cast<float>(cell) - 0.5f) / th, 1.f);
    }
    const struct {
        const char *key;
        int value;
        std::uint8_t color;
    } rows[3] = {
        { "hd2d.ui.weather.rain", w.rain, 1 },
        { "hd2d.ui.weather.wind", w.wind, 1 },
        { "hd2d.ui.weather.temp", w.temp, static_cast<std::uint8_t>((w.temp_level() == 0) ? 12 : (w.temp_level() == 1) ? 11 : (w.temp_level() == 2) ? 1 : 14) },
    };
    int label_w = 0;
    for (const auto &row : rows) {
        label_w = std::max(label_w, text.measure(i18n::tr(row.key)));
    }
    const int tx = body.x + icon_size + std::max(2, text.cell_w() / 2);
    const int right = body.x + body.w;
    for (int i = 0; i < 3; ++i) {
        const int y = y0 + (i * cell_h);
        const std::string label = i18n::tr(rows[i].key);
        const TextColor color = text_from_term_color(rows[i].color, 1.f, theme);
        text.draw(tx, y, label, color, right - tx);
        char value[16];
        std::snprintf(value, sizeof(value), "%2d", rows[i].value);
        const int vx = tx + label_w + std::max(2, text.cell_w() / 2);
        if (vx < right) {
            text.draw(vx, y, value, color, right - vx);
        }
    }
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

// Fill only the screen margin outside the scene. The map/3D viewport is never covered.
void draw_main_panel_design(UiPaint &paint,const UiLayout &layout,int theme) {
    if(theme<0)return;
    const auto skin=modern_skin(theme);const auto &area=layout.content;const auto &scene=layout.scene;
    if(!panel_transparent()) {
        const int x0=std::max(area.x,scene.x),y0=std::max(area.y,scene.y);
        const int x1=std::min(area.x+area.w,scene.x+scene.w),y1=std::min(area.y+area.h,scene.y+scene.h);
        if(x1<=x0||y1<=y0)paint.surface(area,skin.background,skin.material);
        else {
            paint.surface({area.x,area.y,area.w,y0-area.y},skin.background,skin.material);
            paint.surface({area.x,y1,area.w,area.y+area.h-y1},skin.background,skin.material);
            paint.surface({area.x,y0,x0-area.x,y1-y0},skin.background,skin.material);
            paint.surface({x1,y0,area.x+area.w-x1,y1-y0},skin.background,skin.material);
        }
    }
    // Compact edge treatment: no central background or header band on the game viewport.
    if(!scene.empty())paint_modern_window(paint,scene,theme,true,false);
}

void draw_game_ui(UiPaint &paint, TextOverlay &text, const UiLayout &layout,
    const GameFrame &frame, const HudState &state, const UiBackdrops *backdrops, bool modern_window)
{
    /*
     * **新 UI の窓の下にもサブパネルを描き続ける**（決めたこと 2026-09-26「メニュー表示中
     * サブパネルが消える。出したままに」）。マップの無い画（タイトル・キャラクター作成）だけは
     * 従来どおり窓だけを描く。窓は `fixed_menu_rect()` の位置と大きさのまま**上に重ねる**。
     * 文字は全部のパネルの後にまとめて流れるので、窓を描く前に一度流す（下の字が窓の上に出ない）。
     */
    if (state.modern.active() && !modern_window_keeps_hud(frame)) {
        if (modern_window) {
            state.modern.draw(paint, text, layout, frame, state.modern_theme);
        }
        return;
    }
    state.native_view_valid=false;
    if (ui_break_enabled("panels")) {
        return; // `HD2D_BREAK_UI=panels`。**検査が FAIL になるのが正しい。**
    }
    /*
     * タイトル・birth・死亡は**画面に地図が無い**。地図まわりを描くと、
     * 前のフロアの状態列やミニマップが残ったまま重なる。
     */
    if (state.window_theme<0 && !frame_shows_map(frame) && (frame.title_screen || frame.pre_game_menu)) {
        /*
         * タイトル画（K-47 相当）。**`title_screen` のときだけ**敷く。
         * `pre_game_menu` はキャラクター作成や死亡後も指すので、そこまで敷くと
         * ロール結果の裏に玉座の絵が出る。
         */
        const bool title_art = frame.title_screen && (backdrops != nullptr) && backdrops->has_title();
        if (title_art) {
            backdrops->painter->draw_cover(*backdrops->title, layout.term_full, 1.f);
        }
        draw_term_mirror(paint, text, layout, frame, state.cursors, title_art,state.window_theme);
        return;
    }

    //! 上と同じ理由——**重なる作りかどうか**で決める（`Split` / `Tall` は濃く塗る）。
    const bool solid = !layout.sub_overlaid;
    PanelStyle panel_style = solid ? solid_panel_style() : overlay_panel_style();
    draw_main_panel_design(paint,layout,state.window_theme);

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
            if(state.window_theme>=0)continue; // The assigned material replaces artwork for this panel.
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

    const float panel_scale=state.window_theme>=0?menu_font_scale(state.modern_font_level):1.f;
    // --- キャラクター状態の列（コアの左フレーム 13 桁の写し。K-28） ---
    if (!layout.status_col.empty() && !frame.status_col_lines.empty()) {
        if(state.window_theme>=0)paint_modern_window(paint,layout.status_col,state.window_theme,true,!panel_transparent(),solid?1.f:.88f);
        else paint.panel(layout.status_col, panel_style);
        MenuScaleScope scope(paint,text,panel_scale);
        const auto logical=panel_logical_layout(layout,scope.scale,text);
        const UiProbeLayer probe_layer("status");
        draw_text_grid(text,panel_body(logical.status_col,logical.cell_w),std::string(),frame.status_col_lines,logical.cell_h,state.window_theme);
        //! 天候（コアが申告したときだけ。TOband）。状態欄の最後の字の 1 行下にアイコンと値を置く。
        if (frame.weather.declared) {
            static const OrbHudImages no_weather_images{};
            draw_weather_status(paint, text, panel_body(logical.status_col, logical.cell_w), logical.cell_h,
                static_cast<int>(frame.status_col_lines.size()), frame,
                (backdrops != nullptr) ? backdrops->orb : no_weather_images, scope.scale, state.window_theme);
        }
    }

    // --- サブパネル 5 枚 ---
    if (draw_subs) {
        for (int i = 0; i < kUiSubPanels; ++i) {
            const RectPx &area = layout.sub[i];
            if (area.empty()) {
                continue;
            }
            const int theme=state.window_theme;
            if(theme>=0)paint_modern_window(paint,area,theme,true,!panel_transparent(),solid?1.f:.88f);
            else paint.panel(area, panel_style);
            MenuScaleScope scope(paint,text,panel_scale);
            const auto logical=panel_logical_layout(layout,scope.scale,text);
            const RectPx body = panel_body(logical.sub[i],logical.cell_w);
            const SubPanelContent &content = frame.sub_panels[static_cast<std::size_t>(i)];
            const UiProbeLayer probe_layer("sub");
            if ((content.kind >= kBuiltinSubPanelKindBase)
                && (content.kind < kBuiltinSubPanelKindBase + kBuiltinSubPanelKindCount)) {
                //! UI 既定の描き方を種類として選んだ枠（Rogue 系。`game_frame.h`）。
                draw_default_sub_panel(text, body, content.kind - kBuiltinSubPanelKindBase, frame, logical.cell_h, theme);
            } else if (content.kind >= 0) {
                draw_text_grid(text, body, content.title_utf8, content.lines, logical.cell_h,theme);
            } else {
                draw_default_sub_panel(text, body, i, frame, logical.cell_h,theme);
            }
        }
    }

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

    {
        MenuScaleScope scope(paint,text,panel_scale);const auto logical=panel_logical_layout(layout,scope.scale,text);
        const UiProbeLayer probe_layer("bottom");
        draw_bottom_bar(paint,text,logical,frame,state);
        draw_hud_name(text,logical,frame);
    }
    /*
     * --- HP/MP の玉とバフ・デバフのアイコン（棒の置き換え） ---
     * **字の倍率の外**で、物理の画素の配置のまま描く（玉は絵なので字の倍率に合わせない）。
     * 画像は呼んだ瞬間に描かれ、下敷きと字は後でまとめて流れる。素材が無ければ元の棒に落ちる。
     */
    {
        static const OrbHudImages no_images{};
        draw_orb_hud(paint, text, layout, frame, (backdrops != nullptr) ? backdrops->orb : no_images,
            state.orb_params, state.orb_anim, state.orb_now_ms);
    }
    /*
     * --- メッセージとプロンプト（帯が空なら Sub1 が受け持っている） ---
     * **玉の後に描く**（玉は地図の下縁に置き、帯が出ている間は帯を玉の上に重ねる。決めたこと 2026-10-09）。
     */
    {MenuScaleScope scope(paint,text,panel_scale);draw_message_bar(paint,text,panel_logical_layout(layout,scope.scale,text),frame);}
    if(!state.modern.active()) {
        MenuScaleScope scope(paint,text,state.window_theme>=0?menu_font_scale(state.modern_font_level):1.f);
        draw_prompt_bar(paint,text,menu_logical_layout(layout,scope.scale,&text),frame,state.cursors,state.window_theme);
    }
    const RectPx minimap_box = draw_minimap(paint, layout, frame, state);

    /*
     * --- 階層（K-33。**この 1 か所だけ**に出す） ---
     * **ミニマップのすぐ下に置く。**`scene` の右端に寄せると、重なる作りでは
     * 窓の右端＝サブパネルの上に出てしまう（1 度そう出した）。ミニマップは 3 つの作りとも
     * 「空いている所の右上」に置いてあるので、その真下なら必ず地図の中である。
     */
    if (!frame.depth.text_utf8.empty() && !minimap_box.empty()) {
        MenuScaleScope scope(paint,text,panel_scale);const auto box=menu_scaled_rect(minimap_box,1.f/scope.scale);
        const UiProbeLayer probe_layer("hud");
        const int w=std::min(box.w,text.measure(frame.depth.text_utf8));
        ui_probe().hud_depth(frame.depth.text_utf8); //!< 検査用の問い合わせ
        text.draw(box.x+box.w-w,box.y+box.h+2,frame.depth.text_utf8,text_from_term_color(frame.depth.color),box.w);
    }

    /*
     * --- ゲーム中メニュー（店・持ち物）の小窓は**いちばん上** ---
     * **`frame_shows_map()` では判じない。** 重ねのメニュー（`menu_over_map`）は
     * 「地図が出ている」と答えるが、箱そのものはここで描く必要がある。
     * 見るのは「写しが積んであるか」だけでよい。
     */
    if(state.modern.active()) {
        if(modern_window) {
            paint.flush();
            text.flush();
            state.modern.draw(paint,text,layout,frame,state.modern_theme);
        }
    }
    else if(state.targeting.active()) {const UiProbeLayer probe_layer("targeting");state.targeting.draw(paint,text,layout,state.modern_theme);}
    else if (state.window_theme<0 && !frame.menu_term_lines.empty()) {
        draw_term_mirror(paint, text, layout, frame, state.cursors, false,state.window_theme);
    }
}

} // namespace hd2d
