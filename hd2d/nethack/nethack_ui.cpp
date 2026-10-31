/*!
 * @file nethack_ui.cpp
 * @brief `nethack_ui.h` の実装。
 */
#include "nethack/nethack_ui.h"

#include "app/ui_probe.h"
#include "dcss/dcss_cells.h"
#include "nethack/nh_statwin.h"
#include "nethack/nh_style.h"

#include <SDL2/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace hd2d::nethack {

namespace {

//! 字（DCSS と同じ BIZ UDゴシック）。Windows は OS のもの、無ければ同梱の OFL 版。
const char *const kFontCandidates[] = {
    "C:/Windows/Fonts/BIZ-UDGothicR.ttc",
    "assets/fonts/dcss/BIZUDGothic-Regular.ttf",
};

//! 属性を持つ描き方に直す（状態の `HL_*` の位 → `ATR_*`）。
int atr_from_hl(int hl)
{
    if ((hl & kHlBold) != 0) return kAtrBold;
    if ((hl & kHlInverse) != 0) return kAtrInverse;
    if ((hl & kHlUline) != 0) return kAtrUline;
    if ((hl & kHlBlink) != 0) {
        return ((SDL_GetTicks() / 500u) % 2u == 1u) ? kAtrInverse : kAtrNone; // 500 ミリ秒ごとに反転（原作と同じ周期）
    }
    if ((hl & kHlDim) != 0) return kAtrDim;
    return kAtrNone;
}

//! utf8 の先頭から `chars` 字ぶんのバイト数。
std::size_t utf8_prefix_bytes(const std::string &s, std::size_t chars)
{
    std::size_t i = 0;
    while ((i < s.size()) && (chars > 0)) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        i = std::min(s.size(), i + len);
        --chars;
    }
    return i;
}

} // namespace

NethackUi::NethackUi() = default;

NethackUi::~NethackUi()
{
    this->shutdown();
}

void NethackUi::set_language(const std::string &lang)
{
    this->japanese_ = (lang != "en");
    this->windows_.set_language(this->japanese_);
}

void NethackUi::ingest_payload(const std::string &payload)
{
    (void)this->model_.ingest(payload);
    if (this->model_.hover_serial() != this->seen_hover_serial_) {
        this->seen_hover_serial_ = this->model_.hover_serial();
        this->map_mouse_.on_answer(this->model_.hover_x(), this->model_.hover_y(), this->model_.hover_text());
    }
    this->sync_windows();
}

void NethackUi::sync_windows()
{
    this->windows_.sync(this->model_);
}

void NethackUi::collect_window_outcome()
{
    NhOutcome outcome = this->windows_.take_outcome();
    if (outcome.replies.empty()) {
        return;
    }
    for (std::string &reply : outcome.replies) {
        this->out_.push_back(NativeUiOutgoing{ "nethack_reply", std::move(reply) });
    }
    this->model_.clear_request(); // 答えたので要求は終わった（次の知らせまで窓を出さない）
    this->model_.clear_wait();
    if (!outcome.echo.empty() && this->in_game()) {
        // 原作の GUI 版は、答えた問いをメッセージの窓に残す（`mswin_yn_function`・`mswin_getlin`）。
        this->model_.msgwin().put(outcome.echo_attr, outcome.echo, false);
        this->model_.msgwin().mark_seen();
    }
}

TextColor NethackUi::text_color_of(int nhcolor, float alpha)
{
    return nh_text_color(nhcolor, alpha);
}

PaintColor NethackUi::paint_color_of(int nhcolor, float alpha)
{
    return nh_paint_color(nhcolor, alpha);
}

bool NethackUi::open_font(int px, std::string &err)
{
    if (this->font_open_ && (this->font_px_ == px)) {
        return true;
    }
    if (this->font_open_) {
        this->text_.shutdown();
        this->font_open_ = false;
    }
    std::vector<std::string> candidates;
    if (const char *forced = std::getenv("HD2D_NETHACK_FONT"); (forced != nullptr) && (*forced != '\0')) {
        candidates.emplace_back(forced); // 検査用: 字を 1 つに決める
    } else {
        for (const char *path : kFontCandidates) {
            candidates.emplace_back(path);
        }
    }
    std::string last_err;
    for (const std::string &path : candidates) {
        if (!this->font_path_.empty() && (this->font_path_ != path)) {
            continue; // 開けたものを使い続ける
        }
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            continue;
        }
        std::string e;
        if (this->text_.init_with_font(px, path, 0, e)) {
            this->font_px_ = px;
            this->font_open_ = true;
            this->font_path_ = path;
            return true;
        }
        last_err = e;
        this->text_.shutdown();
    }
    err = "NetHack の字（BIZ UDゴシック）を開けませんでした。" + last_err;
    return false;
}

bool NethackUi::prepare(int origin_x, int origin_y, int usable_w, int usable_h, int reserved_bottom, std::string &err)
{
    this->origin_x_ = origin_x;
    this->origin_y_ = origin_y;
    this->usable_w_ = usable_w;
    this->usable_h_ = usable_h;
    this->reserved_bottom_ = reserved_bottom;
    const int layout_h = std::max(1, usable_h - reserved_bottom);
    if (!this->open_font(nh_font_px_for_window(usable_w, layout_h), err)) {
        this->layout_ = NhLayout{};
        return false;
    }
    this->cell_w_ = std::max(1, this->text_.cell_w());
    this->line_h_ = std::max(1, this->text_.cell_h() + 2);
    this->pad_x_ = std::max(4, this->cell_w_ / 2);

    NhLayoutInput in;
    in.x = origin_x;
    in.y = origin_y;
    in.w = usable_w;
    in.h = layout_h;
    in.line_h = this->line_h_;
    in.msg_rows = 4;
    in.status_rows = 2;
    in.perm_invent = this->model_.perm_invent();
    // 右寄せの窓（メニュー・テキスト）の希望の幅。窓が無いときは 0（幅は地図の 1/4）。
    in.menu_content_w = this->windows_.desired_width(this->text_, this->cell_w_, this->line_h_, this->pad_x_, in.perm_invent);
    // コマンドの欄（コアが一覧を送ってから）。ボタンは字の 2 行ぶんほど（小さく多く並べる。決めたこと 2026-10-28）
    this->cmdbar_cell_ = std::clamp((this->line_h_ * 2) + 8, 40, 72);
    in.cmdbar_count = static_cast<int>(this->model_.cmdbar().size());
    in.cmdbar_cell = this->cmdbar_cell_;
    {
        std::string tiles = this->model_.pref_string("nethack_tiles"); // コアが `pref` で知らせた絵の置き場（設計書 §5）
        if (const char *forced = std::getenv("HD2D_NETHACK_TILES"); (forced != nullptr) && (*forced != '\0')) {
            tiles = forced; // 検査用
        }
        this->windows_.set_tile_dir(tiles);
    }
    const bool same = this->have_layout_input_ && (in.x == this->last_input_.x) && (in.y == this->last_input_.y) && (in.w == this->last_input_.w)
        && (in.h == this->last_input_.h) && (in.line_h == this->last_input_.line_h) && (in.msg_rows == this->last_input_.msg_rows)
        && (in.perm_invent == this->last_input_.perm_invent) && (in.menu_content_w == this->last_input_.menu_content_w)
        && (in.cmdbar_count == this->last_input_.cmdbar_count) && (in.cmdbar_cell == this->last_input_.cmdbar_cell);
    if (!same) {
        this->layout_ = nh_compute_layout(in);
        this->last_input_ = in;
        this->have_layout_input_ = true;
        ++this->layout_serial_;
        if (!this->layout_.ok) {
            err = "NetHack の画面の配置を決められませんでした: " + this->layout_.error;
        }
    }
    if (!this->layout_.ok) {
        return false;
    }
    // メッセージの窓の見える桁。折り返しも `--More--` の判定（コアの側）も、この桁と行数で決まる。
    this->msg_cols_ = std::max(1, (this->layout_.message.w - (2 * this->pad_x_)) / this->cell_w_);
    this->model_.msgwin().set_columns(this->msg_cols_);
    this->report_message_window_size();
    return true;
}

void NethackUi::report_message_window_size()
{
    const int rows = this->layout_.msg_rows;
    const int cols = this->msg_cols_;
    if ((rows == this->sent_rows_) && (cols == this->sent_cols_)) {
        return;
    }
    this->sent_rows_ = rows;
    this->sent_cols_ = cols;
    nlohmann::json j = nlohmann::json::object();
    j["t"] = "nethack_reply";
    j["r"] = 0;
    j["a"] = "msgwin";
    j["rows"] = rows;
    j["cols"] = cols;
    this->out_.push_back(NativeUiOutgoing{ "nethack_reply", j.dump() });
}

RectPx NethackUi::scene_rect() const
{
    if (!this->layout_.ok) {
        return RectPx{};
    }
    const NhRect &m = this->layout_.map;
    return RectPx{ m.x, m.y, m.w, m.h };
}

std::string NethackUi::describe() const
{
    const NhLayout &L = this->layout_;
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "nethack-ui layout ok=%d usable=%dx%d reserved=%d font=%dpx line=%d msg=%d,%d %dx%d(%dx%d) status=%d,%d %dx%d map=%d,%d %dx%d menu=%d,%d %dx%d",
        L.ok ? 1 : 0, this->usable_w_, this->usable_h_, this->reserved_bottom_, this->font_px_, this->line_h_, L.message.x, L.message.y, L.message.w,
        L.message.h, this->msg_cols_, L.msg_rows, L.status.x, L.status.y, L.status.w, L.status.h, L.map.x, L.map.y, L.map.w, L.map.h, L.menu.x,
        L.menu.y, L.menu.w, L.menu.h);
    return buf;
}

int NethackUi::draw_run(int x, int y, const std::string &text, const TextColor &color, int attr, int max_width)
{
    if (text.empty()) {
        return 0;
    }
    const int width = this->text_.measure(text);
    TextColor fg = color;
    if (attr == kAtrDim) {
        fg.r *= 0.6f;
        fg.g *= 0.6f;
        fg.b *= 0.7f;
    }
    if (attr == kAtrInverse) {
        // 反転: 字の色で塗って、字は下地の色で描く。
        this->text_.draw(x, y, text, TextColor{ kNhTextBg.r, kNhTextBg.g, kNhTextBg.b, 1.f }, max_width);
        return width;
    }
    this->text_.draw(x, y, text, fg, max_width);
    if (attr == kAtrBold) {
        this->text_.draw(x + 1, y, text, fg, max_width); // 太字は 1 画素ずらして重ねる（フォントに太字の字形が無い）
    }
    return width;
}

void NethackUi::draw_message_window(UiPaint &paint)
{
    const NhRect &r = this->layout_.message;
    paint.panel(RectPx{ r.x, r.y, r.w, r.h }, solid_panel_style());
    const UiProbeLayer probe_layer("nethack-msg");
    const std::string more = this->model_.more_pending() ? (this->japanese_ ? "--続き--" : "--More--") : std::string();
    const int inner_w = std::max(1, r.w - (2 * this->pad_x_));
    // ゲームの中の問い（yn・文字入力）は、メッセージの窓の最終行に太字で出す（原作の GUI 版 `mswin_yn_function`・`mswin_getlin`）。
    std::string prompt_text;
    bool prompt_input = false;
    std::vector<std::string> prompt_lines;
    if (this->windows_.message_row(prompt_text, prompt_input)) {
        prompt_lines = nh_wrap_to_width(this->text_, prompt_text, inner_w);
        while (static_cast<int>(prompt_lines.size()) > this->layout_.msg_rows) {
            prompt_lines.erase(prompt_lines.begin());
        }
    }
    const int np = static_cast<int>(prompt_lines.size());
    const std::vector<NhMsgWindow::Row> rows = this->model_.msgwin().rows(std::max(0, this->layout_.msg_rows - np), (np > 0) ? std::string() : more);
    const int nr = static_cast<int>(rows.size());
    const int n = nr + np;
    const TextColor fg = text_color_of(kNoColor);
    // 前のターンの行は、下地と字の色の平均の灰色（原作 `setMsgTextColor`）。
    const TextColor gray{ (fg.r + kNhTextBg.r) / 2.f, (fg.g + kNhTextBg.g) / 2.f, (fg.b + kNhTextBg.b) / 2.f, 1.f };
    for (int i = 0; i < nr; ++i) {
        const NhMsgWindow::Row &row = rows[static_cast<std::size_t>(i)];
        const int y = r.bottom() - ((n - i) * this->line_h_) + 1; // 最新が最下段
        const int x = r.x + this->pad_x_;
        if (row.attr == kAtrInverse) {
            paint.rect(RectPx{ x, y, std::min(inner_w, this->text_.measure(row.text)), this->line_h_ }, PaintColor{ fg.r, fg.g, fg.b, 1.f });
        }
        (void)this->draw_run(x, y, row.text, row.current ? fg : gray, row.attr, inner_w);
        if (row.attr == kAtrUline) {
            paint.rect(RectPx{ x, y + this->line_h_ - 2, std::min(inner_w, this->text_.measure(row.text)), 1 },
                PaintColor{ fg.r, fg.g, fg.b, row.current ? 1.f : 0.5f });
        }
    }
    for (int j = 0; j < np; ++j) {
        const std::string &line = prompt_lines[static_cast<std::size_t>(j)];
        const int y = r.bottom() - ((np - j) * this->line_h_) + 1;
        const int x = r.x + this->pad_x_;
        const int width = this->draw_run(x, y, line, fg, kAtrBold, inner_w);
        if (j == np - 1) {
            // 問いの末尾のキャレット（点滅。IME の欄の位置にもなる）。
            if (((SDL_GetTicks() / 500u) % 2u) == 0u) {
                paint.rect(RectPx{ x + width + 1, y, std::max(2, this->cell_w_ / 3), this->line_h_ }, PaintColor{ 1.f, 1.f, 1.f, 1.f });
            }
            this->windows_.set_message_caret(x + width, y, this->line_h_);
        }
    }
}

void NethackUi::draw_status_window(UiPaint &paint)
{
    const NhRect &r = this->layout_.status;
    paint.panel(RectPx{ r.x, r.y, r.w, r.h }, solid_panel_style());
    const UiProbeLayer probe_layer("nethack-stat");
    const NhStatusView view = nh_build_status(this->model_);
    const int inner_w = std::max(1, r.w - (2 * this->pad_x_));
    const TextColor default_fg = text_color_of(kNoColor);
    const int gap = this->cell_w_; // 欄の前の空白（原作は字の幅 1 つぶん）
    for (int row = 0; row < 2; ++row) {
        const int y = r.y + (row * this->line_h_) + 1;
        int x = r.x + this->pad_x_;
        for (const NhStatRun &run : view.rows[row]) {
            if (run.space_before && (x > r.x + this->pad_x_)) {
                x += gap;
            }
            if (x >= r.x + this->pad_x_ + inner_w) {
                break;
            }
            const int remain = r.x + this->pad_x_ + inner_w - x;
            const int atr = atr_from_hl(run.attr);
            const TextColor color = text_color_of(run.color);
            const int width = this->text_.measure(run.text);
            if ((row == 0) && (run.field == kBlTitle) && (view.bar_pct > 0)) {
                // 体力バー: 題の欄の文字の上に、HP の割合の幅だけ HP の色の帯を塗り、帯の上の字は下地の色にする。
                const int bar_w = std::min(width, (width * view.bar_pct) / 100);
                paint.rect(RectPx{ x, y, bar_w, this->line_h_ }, paint_color_of(view.bar_color, 1.f));
                const std::size_t chars = dcss::dcss_decode_utf8(run.text).size();
                std::size_t dark_chars = 0;
                for (std::size_t k = 1; k <= chars; ++k) {
                    const std::string prefix = run.text.substr(0, utf8_prefix_bytes(run.text, k));
                    const int end = this->text_.measure(prefix);
                    const int start = this->text_.measure(run.text.substr(0, utf8_prefix_bytes(run.text, k - 1)));
                    if (((start + end) / 2) < bar_w) {
                        dark_chars = k;
                    }
                }
                const std::size_t split = utf8_prefix_bytes(run.text, dark_chars);
                const std::string left = run.text.substr(0, split);
                const std::string right = run.text.substr(split);
                const int left_w = this->text_.measure(left);
                this->text_.draw(x, y, left, TextColor{ kNhTextBg.r, kNhTextBg.g, kNhTextBg.b, 1.f }, remain);
                this->text_.draw(x + left_w, y, right, (run.color == kNoColor) ? default_fg : color, std::max(1, remain - left_w));
            } else {
                if (atr == kAtrInverse) {
                    paint.rect(RectPx{ x, y, std::min(width, remain), this->line_h_ },
                        (run.color == kNoColor) ? PaintColor{ default_fg.r, default_fg.g, default_fg.b, 1.f } : paint_color_of(run.color, 1.f));
                }
                if (atr == kAtrUline) {
                    paint.rect(RectPx{ x, y + this->line_h_ - 2, std::min(width, remain), 1 }, paint_color_of(run.color, 1.f));
                }
                (void)this->draw_run(x, y, run.text, color, atr, remain);
            }
            x += width;
        }
    }
}

void NethackUi::draw(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h)
{
    if (!this->layout_.ok || !this->font_open_) {
        return;
    }
    this->text_.begin(screen_w, screen_h);
    if (!this->in_game()) {
        return;
    }
    // 区画の外は黒（窓の端の余りが立体の背景や HD2D の部品のままにならないよう、使える範囲全体を一度塗ってから区画を重ねる）。
    const PaintColor black{ 0.f, 0.f, 0.f, 1.f };
    const NhLayout &L = this->layout_;
    paint.rect(RectPx{ L.message.x, L.message.y, L.message.w, L.message.h }, black);
    paint.rect(RectPx{ L.status.x, L.status.y, L.status.w, L.status.h }, black);
    this->draw_message_window(paint);
    this->draw_status_window(paint);
    this->draw_cmdbar(paint);
    // 持ち物の常設の窓（`perm_invent`。既定は出さない）。行の頭のタイルの絵は四角を流したあとに貼る。
    NhDrawEnv env{ paint, this->text_, screen_w, screen_h, RectPx{ this->origin_x_, this->origin_y_, this->usable_w_, std::max(1, this->usable_h_ - this->reserved_bottom_) },
        this->layout_.menu, this->layout_.invent, this->cell_w_, this->line_h_, this->pad_x_, true, this->japanese_ };
    this->windows_.draw_invent(env, this->model_);
    this->draw_hover_tooltip(paint, screen_w, screen_h);
    this->draw_cmdbar_tip(paint);
    paint.flush();
    this->windows_.flush_tiles(images);
    this->text_.flush();
}

RectPx NethackUi::cmdbar_cell_rect(int index) const
{
    const NhLayout &L = this->layout_;
    const int cell = this->cmdbar_cell_;
    if (L.cmdbar.empty() || (cell <= 0) || (index < 0)) {
        return RectPx{};
    }
    // 縦長は横に並べて段を重ね、横長（右の列）は縦に並べて列を重ねる
    const int col = L.cmdbar_vertical ? (index / std::max(1, L.cmdbar_rows)) : (index % std::max(1, L.cmdbar_cols));
    const int row = L.cmdbar_vertical ? (index % std::max(1, L.cmdbar_rows)) : (index / std::max(1, L.cmdbar_cols));
    return RectPx{ L.cmdbar.x + (col * cell), L.cmdbar.y + (row * cell), cell, cell };
}

int NethackUi::cmdbar_index_at(int x, int y) const
{
    const NhRect &b = this->layout_.cmdbar;
    if (b.empty() || !b.contains(x, y)) {
        return -1;
    }
    const int count = static_cast<int>(this->model_.cmdbar().size());
    for (int i = 0; i < count; ++i) {
        const RectPx c = this->cmdbar_cell_rect(i);
        if ((x >= c.x) && (x < c.x + c.w) && (y >= c.y) && (y < c.y + c.h)) {
            return i;
        }
    }
    return -1;
}

void NethackUi::draw_cmdbar(UiPaint &paint)
{
    const NhRect &b = this->layout_.cmdbar;
    const std::vector<NhCmdItem> &items = this->model_.cmdbar();
    if (b.empty() || items.empty()) {
        return;
    }
    const UiProbeLayer probe_layer("nethack-cmdbar");
    paint.rect(RectPx{ b.x, b.y, b.w, b.h }, PaintColor{ 0.f, 0.f, 0.f, 1.f });
    const bool live = this->model_.command_wait() && !this->windows_.active();
    const int cell = this->cmdbar_cell_;
    // 原作のタイルは 16 画素。ぼやけないよう整数倍で、ボタンの内側に収まる大きさにする
    const int tile_px = 16 * std::max(1, (cell - 8) / 16);
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const RectPx c = this->cmdbar_cell_rect(i);
        const RectPx box{ c.x + 2, c.y + 2, c.w - 4, c.h - 4 };
        const bool hot = (i == this->cmdbar_hover_) || ((i == this->cmdbar_tip_) && (SDL_GetTicks() < this->cmdbar_tip_until_));
        paint.panel(box, solid_panel_style());
        if (hot) {
            paint.rect(RectPx{ box.x + 2, box.y + 2, box.w - 4, box.h - 4 }, PaintColor{ 0.30f, 0.36f, 0.52f, live ? 1.f : 0.5f });
        }
        const RectPx dst{ c.x + ((c.w - tile_px) / 2), c.y + ((c.h - tile_px) / 2), tile_px, tile_px };
        if (!this->windows_.queue_glyph_tile(dst, items[static_cast<std::size_t>(i)].glyph)) {
            // 絵の無いコマンドはキーの字（原作の画面の字の色）
            const std::string &t = items[static_cast<std::size_t>(i)].text;
            const int tw = this->text_.measure(t);
            this->text_.draw(c.x + ((c.w - tw) / 2), c.y + ((c.h - this->line_h_) / 2) + 1, t, text_color_of(live ? 11 : 8), c.w);
        }
    }
}

void NethackUi::draw_yn_buttons(UiPaint &paint)
{
    if (!this->yn_mode()) {
        return;
    }
    const UiProbeLayer probe_layer("nethack-yn-buttons");
    const NhRect &b = this->layout_.cmdbar;
    paint.rect(RectPx{ b.x, b.y, b.w, b.h }, PaintColor{ 0.f, 0.f, 0.f, 1.f });
    const std::vector<int> keys = this->windows_.yn_answer_keys();
    const int count = static_cast<int>(keys.size()) + 1; // 末尾は Esc（取り消し）
    for (int i = 0; i < count; ++i) {
        const RectPx c = this->cmdbar_cell_rect(i);
        if ((c.w <= 0) || !b.contains(c.x, c.y)) {
            break;
        }
        const RectPx box{ c.x + 2, c.y + 2, c.w - 4, c.h - 4 };
        paint.panel(box, solid_panel_style());
        if (i == this->cmdbar_hover_) {
            paint.rect(RectPx{ box.x + 2, box.y + 2, box.w - 4, box.h - 4 }, PaintColor{ 0.30f, 0.36f, 0.52f, 1.f });
        }
        const std::string t = (i < static_cast<int>(keys.size())) ? std::string(1, static_cast<char>(keys[static_cast<std::size_t>(i)])) : std::string("Esc");
        const int tw = this->text_.measure(t);
        this->text_.draw(c.x + ((c.w - tw) / 2), c.y + ((c.h - this->line_h_) / 2) + 1, t, text_color_of(11), c.w);
    }
}

void NethackUi::draw_cmdbar_tip(UiPaint &paint)
{
    if (this->yn_mode()) {
        return;
    }
    const std::vector<NhCmdItem> &items = this->model_.cmdbar();
    int index = this->cmdbar_hover_;
    if ((this->cmdbar_tip_ >= 0) && (SDL_GetTicks() < this->cmdbar_tip_until_)) {
        index = this->cmdbar_tip_;
    }
    if ((index < 0) || (index >= static_cast<int>(items.size()))) {
        return;
    }
    const NhCmdItem &item = items[static_cast<std::size_t>(index)];
    const std::string tip = this->japanese_ ? item.ja : item.en;
    if (tip.empty()) {
        return;
    }
    const UiProbeLayer probe_layer("nethack-cmdbar-tip");
    const RectPx c = this->cmdbar_cell_rect(index);
    const NhRect &b = this->layout_.cmdbar;
    const int w = this->text_.measure(tip) + (2 * this->pad_x_);
    const int h = this->line_h_ + 4;
    int x = 0;
    int y = 0;
    if (this->layout_.cmdbar_vertical) {
        // 右の列: 列の左に出す
        x = b.x - w - 4;
        y = std::clamp(c.y + ((c.h - h) / 2), this->origin_y_, std::max(this->origin_y_, b.bottom() - h));
    } else {
        // 下の段: 段の上に出す
        x = std::clamp(c.x + ((c.w - w) / 2), b.x, std::max(b.x, b.right() - w));
        y = b.y - h - 4;
    }
    paint.panel(RectPx{ x, y, w, h }, solid_panel_style());
    this->text_.draw(x + this->pad_x_, y + 2, tip, text_color_of(kNoColor), w - (2 * this->pad_x_));
}

void NethackUi::send_cmdbar(int index)
{
    const std::vector<NhCmdItem> &items = this->model_.cmdbar();
    if ((index < 0) || (index >= static_cast<int>(items.size()))) {
        return;
    }
    const NhCmdItem &item = items[static_cast<std::size_t>(index)];
    nlohmann::json j = nlohmann::json::object();
    if (!item.ext.empty()) {
        // キーの無い拡張コマンド（祈る・終了）。コアが `#` を押したことにして、名前を聞く所でこの名前を答える
        j["t"] = "nethack_reply";
        j["r"] = 0;
        j["a"] = "extcmd";
        j["name"] = item.ext;
        this->out_.push_back(NativeUiOutgoing{ "nethack_reply", j.dump() });
    } else {
        j["t"] = "keys";
        j["keys"] = item.keys;
        this->out_.push_back(NativeUiOutgoing{ "keys", j.dump() });
    }
    this->model_.clear_wait();
}

bool NethackUi::on_cmdbar_event(const SDL_Event &event)
{
    if (this->layout_.cmdbar.empty() || (!this->in_game() && !this->yn_mode())) {
        return false;
    }
    switch (event.type) {
    case SDL_MOUSEMOTION: {
        const int i = this->cmdbar_index_at(event.motion.x, event.motion.y);
        this->cmdbar_hover_ = i;
        return i >= 0;
    }
    case SDL_MOUSEBUTTONDOWN: {
        const int i = this->cmdbar_index_at(event.button.x, event.button.y);
        if (i < 0) {
            return this->layout_.cmdbar.contains(event.button.x, event.button.y); // ボタンの間の隙間も食う
        }
        // 押した物の名前を少しの間だけ出す（タッチでは何を押したかが見えないため）。右クリック（長押し）は名前を見るだけ
        this->cmdbar_tip_ = i;
        this->cmdbar_tip_until_ = SDL_GetTicks() + ((event.button.button == SDL_BUTTON_RIGHT) ? 2000u : 800u);
        if (this->yn_mode()) {
            if (event.button.button == SDL_BUTTON_LEFT) {
                const std::vector<int> keys = this->windows_.yn_answer_keys();
                if (i < static_cast<int>(keys.size())) {
                    this->windows_.answer_yn(keys[static_cast<std::size_t>(i)]);
                } else if (i == static_cast<int>(keys.size())) {
                    this->windows_.answer_yn(0x1B);
                }
                this->collect_window_outcome();
            }
            return true;
        }
        if ((event.button.button == SDL_BUTTON_LEFT) && this->model_.command_wait()) {
            this->send_cmdbar(i);
        }
        return true;
    }
    case SDL_MOUSEBUTTONUP:
        return this->layout_.cmdbar.contains(event.button.x, event.button.y);
    case SDL_MOUSEWHEEL: {
        int x = 0;
        int y = 0;
        SDL_GetMouseState(&x, &y);
        return this->layout_.cmdbar.contains(x, y);
    }
    default:
        return false;
    }
}

void NethackUi::flush_text()
{
    if (this->font_open_) {
        this->text_.flush();
    }
}

void NethackUi::draw_windows(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h)
{
    // 窓（メニュー・テキスト・問い・作成・墓標）。HD2D の部品より上に重ねる（呼ぶ側が下の層を流したあとに呼ぶ）。
    if (!this->layout_.ok || !this->font_open_ || !this->windows_.active()) {
        return;
    }
    this->text_.begin(screen_w, screen_h);
    NhDrawEnv env{ paint, this->text_, screen_w, screen_h, RectPx{ this->origin_x_, this->origin_y_, this->usable_w_, std::max(1, this->usable_h_ - this->reserved_bottom_) },
        this->layout_.menu, this->layout_.invent, this->cell_w_, this->line_h_, this->pad_x_, this->in_game(), this->japanese_ };
    this->windows_.draw(env);
    this->draw_yn_buttons(paint);
    paint.flush();
    this->windows_.flush_tiles(images);
    this->text_.flush();
    // IME の変換候補の位置（入力欄のキャレット）。変わったときだけ渡す。
    SDL_Rect rect{ 0, 0, 0, 0 };
    if (this->windows_.text_input_rect(rect) && ((rect.x != this->last_ime_rect_.x) || (rect.y != this->last_ime_rect_.y)
            || (rect.w != this->last_ime_rect_.w) || (rect.h != this->last_ime_rect_.h))) {
        this->last_ime_rect_ = rect;
        SDL_SetTextInputRect(&rect);
    }
}

void NethackUi::send_keys(int key)
{
    nlohmann::json j = nlohmann::json::object();
    j["t"] = "keys";
    j["keys"] = nlohmann::json::array({ key });
    this->out_.push_back(NativeUiOutgoing{ "keys", j.dump() });
    this->model_.clear_wait(); // 答えたので待ちは終わった（次の知らせまで `--More--` を出さない）
}

bool NethackUi::on_window_event(const SDL_Event &event, bool reserved_key)
{
    // はい／いいえの窓の答えのボタン（コマンドの欄の場所）。窓より先に見る（窓は開いている間のマウスを全部食うため）
    if (this->yn_mode() && this->on_cmdbar_event(event)) {
        return true;
    }
    // 窓（メニュー・問い・作成ほか）が開いている間は、キーも文字もマウスも窓が受ける。
    if (this->windows_.on_event(event, reserved_key)) {
        this->collect_window_outcome();
        return true;
    }
    this->collect_window_outcome();
    if (this->on_cmdbar_event(event)) {
        return true;
    }
    if (!this->model_.more_pending()) {
        // 空白のキーを押した直後に続く `SDL_TEXTINPUT` の空白は、コマンドとして漏らさない。
        if (this->swallow_space_ && (event.type == SDL_TEXTINPUT)) {
            this->swallow_space_ = false;
            return (event.text.text[0] == ' ');
        }
        this->swallow_space_ = false;
        return false;
    }
    // `--More--`: スペース・Enter は進める、Esc は進めて以後出さない（コアが覚える）、ほかのキーは無視する（原作 `mhmsgwnd.c:327-358`）。
    if (event.type == SDL_KEYDOWN) {
        if (reserved_key) {
            return false;
        }
        switch (event.key.keysym.sym) {
        case SDLK_SPACE:
            this->send_keys(' ');
            this->swallow_space_ = true;
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            this->send_keys('\r');
            return true;
        case SDLK_ESCAPE:
            this->send_keys(0x1B);
            return true;
        case SDLK_BACKSPACE:
        case SDLK_TAB:
        case SDLK_UP:
        case SDLK_DOWN:
        case SDLK_LEFT:
        case SDLK_RIGHT:
            return true; // 無視（コマンドとして漏らさない）
        default:
            return false;
        }
    }
    if (event.type == SDL_TEXTINPUT) {
        if (event.text.text[0] == ' ') {
            this->send_keys(' ');
        }
        return true; // 字はすべて食う（空白以外は無視）
    }
    return false;
}

bool NethackUi::on_pad(NativePad pad)
{
    // 窓（メニュー・文字・問い・作成ほか）。パッドの十字・A・B を窓の操作にする（メニューは下の OK・キャンセルのボタンまで動ける）
    if (this->windows_.active()) {
        (void)this->windows_.on_pad(pad);
        this->collect_window_outcome();
        return true;
    }
    // `--More--`: A は進める、B は進めて以後出さない（キーの Enter・Esc と同じ）。十字は食う
    if (this->model_.more_pending()) {
        if (pad == NativePad::A) {
            this->send_keys('\r');
        } else if (pad == NativePad::B) {
            this->send_keys(0x1B);
        }
        return true;
    }
    return false;
}

std::vector<NativeUiOutgoing> NethackUi::take_outgoing()
{
    this->collect_window_outcome();
    std::vector<NativeUiOutgoing> out;
    out.swap(this->out_);
    return out;
}

std::string NethackUi::key_hint(bool ja) const
{
    if (this->windows_.active()) {
        return this->windows_.key_hint(ja);
    }
    if (this->model_.more_pending()) {
        return ja ? "Space/Enter:続き  Esc:以後省略  " : "Space/Enter:Continue  Esc:Skip  ";
    }
    if (this->in_game()) {
        // 地図の上の場面（命令の待ち・位置の選択）の案内。HD2D の共通のパッドの割り当ては出さない（D13）
        return nh_map_key_hint(ja, this->model_.wait().reason);
    }
    return {};
}

NhMapMouseContext NethackUi::mouse_context() const
{
    NhMapMouseContext ctx;
    ctx.windows_active = this->windows_.active();
    ctx.more_pending = this->model_.more_pending();
    ctx.map_click_wait = this->model_.map_click_wait();
    return ctx;
}

bool NethackUi::on_map_event(const SDL_Event &event, bool on_map, int cell_x, int cell_y)
{
    if (!this->in_game()) {
        return false;
    }
    const NhMapMouseContext ctx = this->mouse_context();
    if (event.type == SDL_MOUSEMOTION) {
        if (event.motion.which == SDL_TOUCH_MOUSEID) {
            return false; // 指は N5
        }
        const std::string query = this->map_mouse_.on_motion(ctx, on_map, cell_x, cell_y, event.motion.x, event.motion.y, this->model_.wait_serial());
        if (!query.empty()) {
            this->out_.push_back(NativeUiOutgoing{ "nethack_query", query });
        }
        return false; // 動きは食わない（HD2D の標準の hover も続ける）
    }
    if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (event.button.which == SDL_TOUCH_MOUSEID) {
            return false;
        }
        const int button = (event.button.button == SDL_BUTTON_LEFT) ? 1 : ((event.button.button == SDL_BUTTON_RIGHT) ? 2 : 0);
        const NhPressResult result = this->map_mouse_.on_press(ctx, on_map, cell_x, cell_y, button, event.button.clicks);
        if (!result.reply.empty()) {
            this->out_.push_back(NativeUiOutgoing{ "nethack_reply", result.reply });
            this->model_.clear_wait(); // 答えたので待ちは終わった（次の知らせまでクリックを受けない）
            this->model_.clear_pos_active();
        }
        return result.consumed;
    }
    return false;
}

bool NethackUi::map_cursor_cell(int &cell_x, int &cell_y) const
{
    if (!this->in_game() || !this->model_.pos_active() || this->windows_.active() || !this->model_.cursor_known()) {
        return false;
    }
    if (!nh_map_cell_ok(this->model_.cursor_x(), this->model_.cursor_y())) {
        return false;
    }
    cell_x = this->model_.cursor_x();
    cell_y = this->model_.cursor_y();
    return true;
}

void NethackUi::draw_map_cursor(UiPaint &paint, const RectPx &cell)
{
    // 原作の GUI 版は点線の枠（DrawFocusRect）。立体の上で見えるよう、明るい黄色の太い枠と薄い塗りにする。
    const float pulse = 0.75f + (0.25f * static_cast<float>((SDL_GetTicks() / 250u) % 2u));
    const PaintColor edge{ 1.f, 0.9f, 0.2f, pulse };
    const int t = std::max(2, std::min(cell.w, cell.h) / 12);
    paint.rect(RectPx{ cell.x, cell.y, cell.w, cell.h }, PaintColor{ 1.f, 0.9f, 0.2f, 0.14f });
    paint.rect(RectPx{ cell.x, cell.y, cell.w, t }, edge);
    paint.rect(RectPx{ cell.x, cell.y + cell.h - t, cell.w, t }, edge);
    paint.rect(RectPx{ cell.x, cell.y, t, cell.h }, edge);
    paint.rect(RectPx{ cell.x + cell.w - t, cell.y, t, cell.h }, edge);
}

void NethackUi::draw_hover_tooltip(UiPaint &paint, int screen_w, int screen_h)
{
    (void)screen_w;
    (void)screen_h;
    const NhMapMouseContext ctx = this->mouse_context();
    const std::string query = this->map_mouse_.on_tick(ctx, this->model_.wait_serial());
    if (!query.empty()) {
        this->out_.push_back(NativeUiOutgoing{ "nethack_query", query });
    }
    std::string tip;
    int px = 0;
    int py = 0;
    if (!this->map_mouse_.tooltip(ctx, tip, px, py)) {
        return;
    }
    const UiProbeLayer probe_layer("nethack-hover");
    const NhRect &map = this->layout_.map;
    const int w = std::min(this->text_.measure(tip) + (2 * this->pad_x_), std::max(1, map.w));
    const int h = this->line_h_ + 4;
    int x = px + 16;
    int y = py + 20;
    if (x + w > map.right()) {
        x = px - 12 - w;
    }
    if (y + h > map.bottom()) {
        y = py - 8 - h;
    }
    x = std::clamp(x, map.x, std::max(map.x, map.right() - w));
    y = std::clamp(y, map.y, std::max(map.y, map.bottom() - h));
    paint.panel(RectPx{ x, y, w, h }, solid_panel_style());
    this->text_.draw(x + this->pad_x_, y + 2, tip, text_color_of(kNoColor), w - (2 * this->pad_x_));
}

std::vector<std::string> NethackUi::message_lines() const
{
    std::vector<std::string> out;
    const std::string more = this->model_.more_pending() ? (this->japanese_ ? "--続き--" : "--More--") : std::string();
    for (const NhMsgWindow::Row &row : this->model_.msgwin().rows(std::max(1, this->layout_.msg_rows), more)) {
        out.push_back(row.text);
    }
    return out;
}

std::vector<std::string> NethackUi::status_lines() const
{
    std::vector<std::string> out;
    const NhStatusView view = nh_build_status(this->model_);
    for (int row = 0; row < 2; ++row) {
        std::string line;
        for (const NhStatRun &run : view.rows[row]) {
            line += ((run.space_before && !line.empty()) ? " " : "") + run.text;
        }
        out.push_back(std::move(line));
    }
    return out;
}

void NethackUi::shutdown()
{
    this->windows_.shutdown();
    if (this->font_open_) {
        this->text_.shutdown();
        this->font_open_ = false;
    }
}

} // namespace hd2d::nethack
