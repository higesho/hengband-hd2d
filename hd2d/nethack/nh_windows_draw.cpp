/*!
 * @file nh_windows_draw.cpp
 * @brief `nh_windows.h` の描画（メニュー・テキスト・問い・文字入力・拡張コマンド・キャラクタ作成・墓標・持ち物の常設の窓）。
 *
 * 窓の枠と地の色は HD2D の作り（暗い下敷きと細い枠。D14）。中身の並びは原作の GUI 版（`mhmenu.c`・`mhdlg.c`・`mhrip.c`）に従う。
 */
#include "nethack/nh_windows.h"

#include "app/ui_probe.h"
#include "nethack/nh_style.h"

#include <algorithm>
#include <cstdio>

namespace hd2d::nethack {

namespace {

const PaintColor kBlack{ 0.f, 0.f, 0.f, 1.f };
const PaintColor kRowFocus{ 0.18f, 0.28f, 0.48f, 0.85f };
const PaintColor kListSelect{ 0.20f, 0.32f, 0.58f, 0.95f };
const PaintColor kStrip{ 0.11f, 0.13f, 0.19f, 1.f };
const PaintColor kBorderDim{ 0.35f, 0.40f, 0.50f, 0.9f };
const PaintColor kBorderFocus{ 0.60f, 0.72f, 1.00f, 1.f };
const PaintColor kFieldFill{ 0.02f, 0.03f, 0.05f, 1.f };
const PaintColor kButtonFill{ 0.14f, 0.17f, 0.26f, 1.f };
const PaintColor kButtonFocus{ 0.22f, 0.30f, 0.50f, 1.f };
const PaintColor kScrollTrack{ 0.20f, 0.22f, 0.28f, 1.f };
const PaintColor kScrollThumb{ 0.55f, 0.62f, 0.78f, 1.f };

PanelStyle popup_style()
{
    PanelStyle s;
    s.fill = PaintColor{ 0.05f, 0.06f, 0.09f, 0.97f };
    s.border = PaintColor{ 0.50f, 0.58f, 0.74f, 0.95f };
    s.border_px = 1;
    return s;
}

TextColor default_text()
{
    return nh_text_color(kNoColor);
}

TextColor dark_text()
{
    return TextColor{ kNhTextBg.r, kNhTextBg.g, kNhTextBg.b, 1.f };
}

//! 属性つきで 1 行描く（太字は 1 画素ずらして重ねる。反転は下地の色の字）。戻り値は幅。
int put(NhDrawEnv &env, int x, int y, const std::string &text, TextColor color, int attr, int max_width)
{
    if (text.empty()) {
        return 0;
    }
    const int width = env.text.measure(text);
    if (attr == kAtrDim) {
        color.r *= 0.6f;
        color.g *= 0.6f;
        color.b *= 0.7f;
    }
    if (attr == kAtrInverse) {
        env.paint.rect(RectPx{ x, y, std::min(width, max_width > 0 ? max_width : width), env.line_h }, PaintColor{ color.r, color.g, color.b, 1.f });
        color = dark_text();
    }
    env.text.draw(x, y, text, color, max_width);
    if (attr == kAtrBold) {
        env.text.draw(x + 1, y, text, color, max_width);
    }
    if (attr == kAtrUline) {
        env.paint.rect(RectPx{ x, y + env.line_h - 2, std::min(width, max_width > 0 ? max_width : width), 1 }, PaintColor{ color.r, color.g, color.b, 1.f });
    }
    return width;
}

std::string label_play(bool ja) { return ja ? "開始" : "Play"; }
std::string label_random(bool ja) { return ja ? "ランダム" : "Random"; }
std::string label_quit(bool ja) { return ja ? "終了" : "Quit"; }

bool caret_on()
{
    return ((SDL_GetTicks() / 500u) % 2u) == 0u;
}

} // namespace

//! 文字列を `width` 画素で折り返す（空白があればそこで切る）。
std::vector<std::string> nh_wrap_to_width(TextOverlay &text, const std::string &line, int width)
{
    std::vector<std::string> out;
    std::string rest = line;
    if (width < 8) {
        out.push_back(rest);
        return out;
    }
    while (!rest.empty()) {
        if (text.measure(rest) <= width) {
            out.push_back(rest);
            break;
        }
        std::size_t cut = text.fit_bytes(rest, width);
        if (cut == 0) {
            cut = 1;
            while ((cut < rest.size()) && ((static_cast<unsigned char>(rest[cut]) & 0xC0) == 0x80)) {
                ++cut;
            }
        }
        std::size_t brk = cut;
        if (cut < rest.size()) {
            const std::size_t sp = rest.rfind(' ', cut);
            if ((sp != std::string::npos) && (sp > 0)) {
                brk = sp;
            }
        }
        out.push_back(rest.substr(0, brk));
        std::size_t next = brk;
        while ((next < rest.size()) && (rest[next] == ' ')) {
            ++next;
        }
        rest = rest.substr(next);
    }
    if (out.empty()) {
        out.emplace_back();
    }
    return out;
}

int NhWindows::tile_px(int line_h) const
{
    return std::max(16, ((line_h * 2) / 16) * 8);
}

RectPx NhWindows::centered(const NhDrawEnv &env, int w, int h) const
{
    w = std::min(w, env.area.w - 8);
    h = std::min(h, env.area.h - 8);
    return RectPx{ env.area.x + ((env.area.w - w) / 2), env.area.y + ((env.area.h - h) / 2), w, h };
}

void NhWindows::draw_scrim(NhDrawEnv &env)
{
    if (!env.in_game) {
        env.paint.rect(env.area, kBlack); // ゲームに入る前は、下の HD2D の部品を隠す
    } else {
        env.paint.rect(env.area, PaintColor{ 0.f, 0.f, 0.f, 0.5f });
    }
}

void NhWindows::draw_frame(NhDrawEnv &env, const RectPx &r, const std::string &title)
{
    env.paint.panel(r, popup_style());
    if (!title.empty()) {
        const int th = env.line_h + 8;
        env.paint.rect(RectPx{ r.x + 1, r.y + 1, r.w - 2, th }, kStrip);
        const int tw = env.text.measure(title);
        put(env, r.x + std::max(env.pad_x, (r.w - tw) / 2), r.y + 5, title, default_text(), kAtrBold, r.w - (2 * env.pad_x));
    }
}

void NhWindows::draw_button(NhDrawEnv &env, const RectPx &r, const std::string &label, bool focus, Hit::Type type, int a)
{
    env.paint.rect(r, focus ? kButtonFocus : kButtonFill);
    env.paint.frame(r, focus ? kBorderFocus : kBorderDim, focus ? 2 : 1);
    const int tw = env.text.measure(label);
    env.text.draw(r.x + std::max(0, (r.w - tw) / 2), r.y + ((r.h - env.line_h) / 2) + 1, label, default_text(), r.w);
    this->hits_.push_back(Hit{ r, type, a });
}

// ---- 幅の希望 ----

bool NhWindows::tab_columns(TextOverlay &text, const NhMenu &menu, std::vector<int> &widths) const
{
    bool any = false;
    widths.clear();
    for (const NhMenuItem &item : menu.items()) {
        if (item.text.find('\t') == std::string::npos) {
            continue;
        }
        any = true;
        std::size_t start = 0;
        std::size_t col = 0;
        while (true) {
            const std::size_t end = item.text.find('\t', start);
            const std::string piece = item.text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (widths.size() <= col) {
                widths.resize(col + 1, 0);
            }
            if (end != std::string::npos) { // 最後の欄は揃えない（幅の計算に入れない）
                widths[col] = std::max(widths[col], text.measure(piece));
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
            ++col;
        }
    }
    return any;
}

int NhWindows::menu_content_width(TextOverlay &text, const NhMenu &menu, int cell_w, int line_h, bool with_checks) const
{
    std::vector<int> cols;
    const bool tabs = this->tab_columns(text, menu, cols);
    int lead = 0;
    if (with_checks && (menu.how() != 0)) {
        lead += ((line_h * 2) / 3) + cell_w;
    }
    lead += 2 * cell_w + (cell_w / 2); // 選ぶ字
    if (this->tiles_ready_) {
        lead += this->tile_px(line_h) + (cell_w / 2);
    }
    int content = 0;
    for (const NhMenuItem &item : menu.items()) {
        int w = 0;
        if (tabs && (item.text.find('\t') != std::string::npos)) {
            std::size_t start = 0;
            std::size_t col = 0;
            while (true) {
                const std::size_t end = item.text.find('\t', start);
                const std::string piece = item.text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (end == std::string::npos) {
                    w += text.measure(piece);
                    break;
                }
                w += cols[col] + (2 * cell_w);
                start = end + 1;
                ++col;
            }
        } else {
            w = text.measure(item.text);
        }
        content = std::max(content, lead + w);
    }
    return content;
}

int NhWindows::desired_width(TextOverlay &text, int cell_w, int line_h, int pad_x, bool perm_invent)
{
    const int px = text.base_px();
    if ((this->kind_ == Kind::Menu) || (this->kind_ == Kind::Text)) {
        if ((this->desired_serial_ == this->seen_serial_) && (this->desired_font_px_ == px)) {
            return this->menu_desired_w_;
        }
        this->load_tiles();
        int content = 0;
        if (this->kind_ == Kind::Menu) {
            content = std::max(this->menu_content_width(text, this->menu_, cell_w, line_h, true), text.measure(this->menu_.prompt()));
        } else {
            for (const TextLine &line : this->text_lines_) {
                content = std::max(content, text.measure(line.text));
            }
        }
        this->menu_desired_w_ = content + (2 * pad_x) + 12; // 余白と捲り棒
        this->desired_serial_ = this->seen_serial_;
        this->desired_font_px_ = px;
        return this->menu_desired_w_;
    }
    if (perm_invent && (this->invent_.size() > 0)) {
        // 持ち物の常設の窓。中身の幅が地図の 1/4 を超えるときは広げる（行が切れないように）。
        if ((this->invent_desired_serial_ != this->invent_serial_) || (this->desired_font_px_ != px)) {
            this->load_tiles();
            this->invent_desired_w_ = this->menu_content_width(text, this->invent_, cell_w, line_h, false) + (2 * pad_x) + 12;
            this->invent_desired_serial_ = this->invent_serial_;
            this->desired_font_px_ = px;
            this->desired_serial_ = 0; // 窓の幅の記憶と取り違えない
        }
        return this->invent_desired_w_;
    }
    return 0;
}

// ---- メニュー ----

void NhWindows::draw_menu_rows(NhDrawEnv &env, NhMenu &menu, const RectPx &list, bool interactive, bool with_checks, bool collect_hits)
{
    const int tpx = this->tile_px(env.line_h);
    const int row_h = std::max(env.line_h, this->tiles_ready_ ? tpx : 0) + 2;
    const int rows_fit = std::max(1, list.h / row_h);
    menu.set_page_rows(rows_fit);
    const int total = menu.size();
    const int sb_w = (total > rows_fit) ? 8 : 0;
    const int inner_right = list.x + list.w - sb_w - env.pad_x;
    const int cb = ((env.line_h * 2) / 3);
    std::vector<int> cols;
    const bool tabs = this->tab_columns(env.text, menu, cols);
    const bool show_checks = with_checks && (menu.how() != 0);
    const TextColor fg = default_text();

    const int x_check = list.x + env.pad_x;
    const int x_letter = x_check + (show_checks ? (cb + (env.cell_w)) : 0);
    const int x_tile = x_letter + (2 * env.cell_w) + (env.cell_w / 2);
    const int x_text = x_tile + (this->tiles_ready_ ? (tpx + (env.cell_w / 2)) : 0);

    for (int i = menu.top(); (i < total) && (i < menu.top() + rows_fit); ++i) {
        const NhMenuItem &item = menu.items()[static_cast<std::size_t>(i)];
        const int y = list.y + ((i - menu.top()) * row_h);
        const RectPx row{ list.x, y, list.w - sb_w, row_h };
        if (interactive && (menu.how() != 0) && (i == menu.cursor())) {
            env.paint.rect(row, kRowFocus);
        }
        if (collect_hits && item.selectable()) {
            this->hits_.push_back(Hit{ row, Hit::MenuRow, i });
        }
        const int ty = y + ((row_h - env.line_h) / 2);
        int color_id = item.clr;
        TextColor color = ((color_id < 0) || (color_id == kNoColor)) ? fg : nh_text_color(color_id);
        const bool header = !item.selectable();
        int attr = item.attr;
        if (header && (attr == 0)) {
            attr = kAtrBold;
        }
        if (attr == kAtrInverse) {
            attr = kAtrBold; // 原作の GUI 版のメニューは、反転（見出しなど）を太字の字体で出す（`mhfont.c`）
        }
        if (item.selectable()) {
            if (show_checks) {
                const RectPx box{ x_check, y + ((row_h - cb) / 2), cb, cb };
                env.paint.frame(box, PaintColor{ fg.r, fg.g, fg.b, 1.f }, 1);
                if (menu.is_selected(i)) {
                    env.paint.rect(box.inset(3), PaintColor{ fg.r, fg.g, fg.b, 1.f });
                }
            }
            if (!item.ch.empty()) {
                env.text.draw(x_letter, ty, item.ch, fg, 2 * env.cell_w);
            }
            int tile = item.tile;
            if ((tile < 0) && this->tiles_ready_ && (item.glyph >= 0)) {
                tile = this->tile_table_.tile_for_glyph(item.glyph);
            }
            if ((tile >= 0) && this->tiles_ready_) {
                this->pending_tiles_.push_back(TileDraw{ RectPx{ x_tile, y + ((row_h - tpx) / 2), tpx, tpx }, tile });
            }
        } else if (item.glyph >= 0 && this->tiles_ready_) {
            const int tile = this->tile_table_.tile_for_glyph(item.glyph);
            if (tile >= 0) {
                this->pending_tiles_.push_back(TileDraw{ RectPx{ x_tile, y + ((row_h - tpx) / 2), tpx, tpx }, tile });
            }
        }
        // 文字列（タブで区切られた欄は揃える）
        const int max_w = std::max(1, inner_right - x_text);
        if (tabs && (item.text.find('\t') != std::string::npos)) {
            std::size_t start = 0;
            std::size_t col = 0;
            int cx = x_text;
            while (true) {
                const std::size_t end = item.text.find('\t', start);
                const std::string piece = item.text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                put(env, cx, ty, piece, color, attr, std::max(1, inner_right - cx));
                if (end == std::string::npos) {
                    break;
                }
                cx += cols[col] + (2 * env.cell_w);
                start = end + 1;
                ++col;
            }
        } else {
            put(env, x_text, ty, item.text, color, attr, max_w);
        }
        const int count = menu.count_of(i);
        if (count > 0) {
            const std::string label = (env.japanese ? "個数: " : "Count: ") + std::to_string(count);
            const int lw = env.text.measure(label);
            env.text.draw(std::max(x_text, inner_right - lw), ty, label, nh_text_color(11), lw);
        }
    }
    if (total > rows_fit) {
        const int track_x = list.x + list.w - 6;
        env.paint.rect(RectPx{ track_x, list.y, 4, list.h }, kScrollTrack);
        const int thumb_h = std::max(10, (list.h * rows_fit) / total);
        const int span = std::max(1, list.h - thumb_h);
        const int denom = std::max(1, total - rows_fit);
        env.paint.rect(RectPx{ track_x, list.y + ((span * menu.top()) / denom), 4, thumb_h }, kScrollThumb);
    }
}

void NhWindows::draw_menu(NhDrawEnv &env)
{
    this->load_tiles();
    RectPx r{ env.menu.x, env.menu.y, env.menu.w, env.menu.h };
    if (!env.in_game) {
        const int w = std::max(this->menu_desired_w_, env.area.w / 3);
        r = this->centered(env, std::min(w, (env.area.w * 2) / 3), env.area.h - 40);
    }
    if (r.empty()) {
        return;
    }
    env.paint.panel(r, popup_style());
    int y = r.y + 1;
    if (!this->menu_.prompt().empty()) {
        env.paint.rect(RectPx{ r.x + 1, y, r.w - 2, env.line_h + 4 }, kStrip);
        put(env, r.x + env.pad_x, y + 2, this->menu_.prompt(), default_text(), kAtrBold, r.w - (2 * env.pad_x));
        y += env.line_h + 4;
    }
    // 下のボタン（原作の GUI 版のメニューの窓の OK・Cancel。マウス・タップ・パッドで押せる）
    const int btn_h = env.line_h + 8;
    const int buttons_h = btn_h + 8;
    int footer_h = buttons_h;
    if (this->menu_.searching()) {
        footer_h += env.line_h + 6;
    }
    const RectPx list{ r.x + 1, y + 2, r.w - 2, std::max(env.line_h, (r.y + r.h - 1) - (y + 2) - footer_h) };
    this->draw_menu_rows(env, this->menu_, list, true, true, true);
    if (this->menu_.searching()) {
        const RectPx bar{ r.x + 1, r.y + r.h - 1 - footer_h, r.w - 2, footer_h - buttons_h };
        env.paint.rect(bar, kStrip);
        const std::string label = env.japanese ? "検索文字列: " : "Search: ";
        const int lw = env.text.measure(label);
        const int ty = bar.y + 3;
        env.text.draw(bar.x + env.pad_x, ty, label, default_text(), bar.w);
        const std::string typed = this->menu_.search_text();
        const int tw = env.text.draw(bar.x + env.pad_x + lw, ty, typed + this->field_.preedit(), default_text(), bar.w - lw - (2 * env.pad_x));
        const int caret_x = bar.x + env.pad_x + lw + tw;
        if (caret_on()) {
            env.paint.rect(RectPx{ caret_x, ty, 2, env.line_h }, PaintColor{ 1.f, 1.f, 1.f, 1.f });
        }
        this->caret_rect_ = SDL_Rect{ caret_x, ty, env.line_h, env.line_h };
        this->caret_valid_ = true;
    }
    {
        const int by = r.y + r.h - 1 - buttons_h + 4;
        const int bw = std::max(env.cell_w * 10, (r.w - (6 * env.pad_x)) / 2);
        this->draw_button(env, RectPx{ r.x + (2 * env.pad_x), by, bw, btn_h }, "OK", this->menu_btn_focus_ == 0, Hit::Ok);
        this->draw_button(env, RectPx{ r.x + r.w - (2 * env.pad_x) - bw, by, bw, btn_h }, env.japanese ? "キャンセル" : "Cancel",
            this->menu_btn_focus_ == 1, Hit::Cancel);
    }
}

// ---- テキスト ----

void NhWindows::wrap_text_lines(TextOverlay &text, int inner_w)
{
    if (this->text_wrapped_w_ == inner_w) {
        return;
    }
    this->text_wrapped_w_ = inner_w;
    this->text_wrapped_.clear();
    for (const TextLine &line : this->text_lines_) {
        for (std::string &piece : nh_wrap_to_width(text, line.text, inner_w)) {
            this->text_wrapped_.push_back(TextLine{ std::move(piece), line.attr });
        }
    }
}

void NhWindows::draw_text_window(NhDrawEnv &env)
{
    RectPx r{ env.menu.x, env.menu.y, env.menu.w, env.menu.h };
    if (!env.in_game) {
        const int w = std::max(this->menu_desired_w_, env.area.w / 2);
        r = this->centered(env, std::min(w, (env.area.w * 3) / 4), env.area.h - 40);
    }
    if (r.empty()) {
        return;
    }
    env.paint.panel(r, popup_style());
    // 下の OK（原作の GUI 版の文字の窓の OK。マウス・タップで閉じる。キーは今までどおり Enter・Esc）
    const int ok_h = env.line_h + 8;
    const RectPx body{ r.x + 1, r.y + 4, r.w - 2, r.h - 8 - (ok_h + 8) };
    const int inner_w = std::max(8, body.w - (2 * env.pad_x) - 10);
    this->wrap_text_lines(env.text, inner_w);
    this->text_page_ = std::max(1, body.h / env.line_h);
    const int total = static_cast<int>(this->text_wrapped_.size());
    this->text_top_ = std::clamp(this->text_top_, 0, std::max(0, total - this->text_page_));
    for (int i = this->text_top_; (i < total) && (i < this->text_top_ + this->text_page_); ++i) {
        const TextLine &line = this->text_wrapped_[static_cast<std::size_t>(i)];
        // 原作の文字の窓は読み取り専用の複数行の入力欄で、行ごとの属性は使わない（`mhtext.c`）。
        put(env, body.x + env.pad_x, body.y + ((i - this->text_top_) * env.line_h), line.text, default_text(), 0, inner_w);
    }
    if (total > this->text_page_) {
        const int track_x = r.x + r.w - 7;
        env.paint.rect(RectPx{ track_x, body.y, 4, body.h }, kScrollTrack);
        const int thumb_h = std::max(10, (body.h * this->text_page_) / total);
        const int span = std::max(1, body.h - thumb_h);
        const int denom = std::max(1, total - this->text_page_);
        env.paint.rect(RectPx{ track_x, body.y + ((span * this->text_top_) / denom), 4, thumb_h }, kScrollThumb);
    }
    {
        const int bw = std::max(env.cell_w * 10, r.w / 4);
        this->draw_button(env, RectPx{ r.x + ((r.w - bw) / 2), r.y + r.h - 4 - ok_h, bw, ok_h }, "OK", true, Hit::Close);
    }
}

// ---- 問いと文字入力 ----

int NhWindows::draw_field(NhDrawEnv &env, const RectPx &box, bool caret, const std::string &prefix)
{
    env.paint.rect(box, kFieldFill);
    env.paint.frame(box, kBorderFocus, 1);
    const int ty = box.y + ((box.h - env.line_h) / 2) + 1;
    int x = box.x + env.pad_x;
    if (!prefix.empty()) {
        x += env.text.draw(x, ty, prefix, default_text(), box.w);
    }
    const int inner_w = std::max(8, box.x + box.w - env.pad_x - x);
    std::string shown = this->field_.text();
    std::string pre = this->field_.preedit();
    // 入りきらないときは、先頭の字を落として末尾が見えるようにする。
    while (!shown.empty() && (env.text.measure(shown + pre) > inner_w)) {
        shown.erase(0, nh_utf8_prefix_bytes(shown, 1));
    }
    x += env.text.draw(x, ty, shown, default_text(), inner_w);
    if (!pre.empty()) {
        const int pw = env.text.draw(x, ty, pre, nh_text_color(11), std::max(1, box.x + box.w - env.pad_x - x));
        env.paint.rect(RectPx{ x, ty + env.line_h - 2, pw, 1 }, PaintColor{ 1.f, 1.f, 0.25f, 1.f }); // 変換中の字の下線
        x += pw;
    }
    if (caret && caret_on()) {
        env.paint.rect(RectPx{ x, ty, 2, env.line_h }, PaintColor{ 1.f, 1.f, 1.f, 1.f });
    }
    this->caret_rect_ = SDL_Rect{ x, box.y, env.line_h, box.h };
    this->caret_valid_ = true;
    return x;
}

void NhWindows::draw_yn_dialog(NhDrawEnv &env)
{
    this->draw_scrim(env);
    const std::string prompt = nh_yn_prompt_text(this->query_, this->choices_, this->def_);
    const int max_w = std::min(env.area.w - 80, env.cell_w * 60);
    const std::vector<std::string> lines = nh_wrap_to_width(env.text, prompt, max_w);
    int w = 0;
    for (const std::string &l : lines) {
        w = std::max(w, env.text.measure(l));
    }
    const int rw = w + (4 * env.pad_x);
    const int rh = (static_cast<int>(lines.size()) * env.line_h) + (2 * env.line_h);
    const RectPx r = this->centered(env, std::max(rw, env.cell_w * 24), rh);
    this->draw_frame(env, r, std::string());
    int y = r.y + env.line_h;
    for (const std::string &l : lines) {
        put(env, r.x + env.pad_x * 2, y, l, default_text(), kAtrBold, r.w);
        y += env.line_h;
    }
}

void NhWindows::draw_input_dialog(NhDrawEnv &env)
{
    this->draw_scrim(env);
    const int w = std::min(env.area.w - 40, std::max(env.cell_w * 40, env.text.measure(this->query_) + (6 * env.pad_x)));
    const int field_h = env.line_h + 10;
    const int btn_h = env.line_h + 12;
    const int h = (env.line_h + 8) + env.pad_x + field_h + (2 * env.pad_x) + btn_h + (2 * env.pad_x);
    const RectPx r = this->centered(env, w, h);
    this->draw_frame(env, r, this->query_);
    const RectPx box{ r.x + (2 * env.pad_x), r.y + env.line_h + 8 + (2 * env.pad_x), r.w - (4 * env.pad_x), field_h };
    this->draw_field(env, box, true);
    const int bw = std::max(env.cell_w * 10, (r.w - (6 * env.pad_x)) / 2);
    const int by = box.y + field_h + (2 * env.pad_x);
    this->draw_button(env, RectPx{ r.x + (2 * env.pad_x), by, bw, btn_h }, "OK", false, Hit::Ok);
    this->draw_button(env, RectPx{ r.x + r.w - (2 * env.pad_x) - bw, by, bw, btn_h }, env.japanese ? "キャンセル" : "Cancel", false, Hit::Cancel);
}

void NhWindows::draw_ext_cmd(NhDrawEnv &env)
{
    this->draw_scrim(env);
    const std::vector<int> matches = nh_ext_matches(this->ext_cmds_, this->field_.text());
    const int w = std::min(env.area.w - 40, env.cell_w * 40);
    const int field_h = env.line_h + 10;
    const int btn_h = env.line_h + 12;
    const int row_h = env.line_h + 2;
    const int rows_max = std::max(3, std::min(12, (env.area.h - (env.line_h * 12)) / row_h));
    const int rows = std::max(3, std::min(rows_max, static_cast<int>(matches.size())));
    const int h = (env.line_h + 8) + (2 * env.pad_x) + field_h + env.pad_x + (rows * row_h) + 6 + (2 * env.pad_x) + btn_h + (2 * env.pad_x);
    const RectPx r = this->centered(env, w, h);
    this->draw_frame(env, r, env.japanese ? "拡張コマンド" : "Extended Command");
    const RectPx box{ r.x + (2 * env.pad_x), r.y + env.line_h + 8 + (2 * env.pad_x), r.w - (4 * env.pad_x), field_h };
    // 入力欄の先頭に `#`（原作の GUI 版は `#` のあとに打つ）
    this->draw_field(env, box, true, "# ");
    const RectPx list{ box.x, box.y + field_h + env.pad_x, box.w, (rows * row_h) + 6 };
    env.paint.rect(list, kFieldFill);
    env.paint.frame(list, kBorderDim, 1);
    const int total = static_cast<int>(matches.size());
    if (this->ext_pick_ >= 0) {
        if (this->ext_pick_ < this->ext_top_) {
            this->ext_top_ = this->ext_pick_;
        } else if (this->ext_pick_ >= this->ext_top_ + rows) {
            this->ext_top_ = this->ext_pick_ - rows + 1;
        }
    }
    this->ext_top_ = std::clamp(this->ext_top_, 0, std::max(0, total - rows));
    for (int k = this->ext_top_; (k < total) && (k < this->ext_top_ + rows); ++k) {
        const RectPx row{ list.x + 2, list.y + 3 + ((k - this->ext_top_) * row_h), list.w - 4 - 8, row_h };
        if (k == this->ext_pick_) {
            env.paint.rect(row, kListSelect);
        }
        this->hits_.push_back(Hit{ row, Hit::ExtRow, k });
        env.text.draw(row.x + env.pad_x, row.y + 1, this->ext_cmds_[static_cast<std::size_t>(matches[static_cast<std::size_t>(k)])], default_text(),
            row.w - env.pad_x);
    }
    if (total > rows) {
        const int track_x = list.x + list.w - 6;
        env.paint.rect(RectPx{ track_x, list.y + 2, 4, list.h - 4 }, kScrollTrack);
        const int thumb_h = std::max(10, ((list.h - 4) * rows) / total);
        const int span = std::max(1, list.h - 4 - thumb_h);
        env.paint.rect(RectPx{ track_x, list.y + 2 + ((span * this->ext_top_) / std::max(1, total - rows)), 4, thumb_h }, kScrollThumb);
    }
    const int bw = std::max(env.cell_w * 10, (r.w - (6 * env.pad_x)) / 2);
    const int by = list.y + list.h + (2 * env.pad_x);
    this->draw_button(env, RectPx{ r.x + (2 * env.pad_x), by, bw, btn_h }, "OK", false, Hit::Ok);
    this->draw_button(env, RectPx{ r.x + r.w - (2 * env.pad_x) - bw, by, bw, btn_h }, env.japanese ? "キャンセル" : "Cancel", false, Hit::Cancel);
}

// ---- キャラクタ作成 ----

void NhWindows::draw_select(NhDrawEnv &env)
{
    this->draw_scrim(env);
    const NhSelect &sel = this->select_;
    const bool ja = env.japanese;
    const int row_h = env.line_h + 2;
    const int m = env.pad_x * 2;
    int roles_w = env.cell_w * 14;
    for (const NhSelOption &o : sel.roles()) {
        roles_w = std::max(roles_w, env.text.measure(sel.role_label(o)) + (2 * env.pad_x));
    }
    int races_w = env.cell_w * 12;
    for (const NhSelOption &o : sel.races()) {
        races_w = std::max(races_w, env.text.measure(o.name) + (2 * env.pad_x));
    }
    const int side_w = env.cell_w * 13;
    const int roles_h = (static_cast<int>(sel.roles().size()) * row_h) + 6;
    const int races_h = (static_cast<int>(sel.races().size()) * row_h) + 6;
    const int aligns_h = (static_cast<int>(sel.aligns().size()) * row_h) + 8;
    const int genders_h = (static_cast<int>(sel.genders().size()) * row_h) + 8;
    const int side_h = env.line_h + aligns_h + m + env.line_h + genders_h;
    const int col_h = env.line_h + std::max({ roles_h, races_h, side_h });
    const int w = (4 * m) + roles_w + races_w + side_w;
    const int title_h = env.line_h + 8;
    const int name_h = env.line_h + (env.line_h + 8);
    const int btn_h = env.line_h + 12;
    const int h = title_h + m + name_h + m + col_h + m + btn_h + m;
    const RectPx r = this->centered(env, w, h);
    this->draw_frame(env, r, ja ? "あなたは何者ですか？" : "What are you?");

    int y = r.y + title_h + m;
    env.text.draw(r.x + m, y, ja ? "名前" : "Name", default_text(), r.w);
    const RectPx name_box{ r.x + m, y + env.line_h, r.w - (2 * m), env.line_h + 8 };
    env.paint.rect(name_box, kFieldFill);
    env.paint.frame(name_box, kBorderDim, 1);
    env.text.draw(name_box.x + env.pad_x, name_box.y + 5, sel.name(), default_text(), name_box.w - (2 * env.pad_x));

    y = name_box.y + name_box.h + m;
    const int col_y = y;
    const NhSelFocus focus = sel.focus();
    // 職業
    int x = r.x + m;
    env.text.draw(x, col_y, ja ? "職業" : "Role", default_text(), roles_w);
    const RectPx roles_box{ x, col_y + env.line_h, roles_w, roles_h };
    env.paint.rect(roles_box, kFieldFill);
    env.paint.frame(roles_box, (focus == NhSelFocus::Roles) ? kBorderFocus : kBorderDim, 1);
    for (std::size_t i = 0; i < sel.roles().size(); ++i) {
        const NhSelOption &o = sel.roles()[i];
        const RectPx row{ roles_box.x + 2, roles_box.y + 3 + (static_cast<int>(i) * row_h), roles_box.w - 4, row_h };
        const bool chosen = (o.index == sel.role());
        if (chosen) {
            env.paint.rect(row, kListSelect);
        }
        this->hits_.push_back(Hit{ row, Hit::Role, o.index });
        env.text.draw(row.x + env.pad_x, row.y + 1, sel.role_label(o), default_text(), row.w - env.pad_x);
    }
    // 種族
    x += roles_w + m;
    env.text.draw(x, col_y, ja ? "種族" : "Race", default_text(), races_w);
    const RectPx races_box{ x, col_y + env.line_h, races_w, races_h };
    env.paint.rect(races_box, kFieldFill);
    env.paint.frame(races_box, (focus == NhSelFocus::Races) ? kBorderFocus : kBorderDim, 1);
    for (std::size_t i = 0; i < sel.races().size(); ++i) {
        const NhSelOption &o = sel.races()[i];
        const RectPx row{ races_box.x + 2, races_box.y + 3 + (static_cast<int>(i) * row_h), races_box.w - 4, row_h };
        const bool chosen = (o.index == sel.race());
        const bool ok = sel.race_ok(o.index);
        if (chosen) {
            env.paint.rect(row, kListSelect);
        }
        this->hits_.push_back(Hit{ row, Hit::Race, o.index });
        // 今の職業に合わない種族は赤い字（選べない）
        env.text.draw(row.x + env.pad_x, row.y + 1, o.name, ok ? default_text() : nh_text_color(1), row.w - env.pad_x);
    }
    // 属性・性別（ラジオボタン。合わない物は灰色）
    x += races_w + m;
    const auto radios = [&](const std::vector<NhSelOption> &list, int caption_y, const std::string &caption, int chosen, bool is_align, bool focused) {
        env.text.draw(x, caption_y, caption, default_text(), side_w);
        const RectPx box{ x, caption_y + env.line_h, side_w, (static_cast<int>(list.size()) * row_h) + 8 };
        env.paint.frame(box, focused ? kBorderFocus : kBorderDim, 1);
        for (std::size_t i = 0; i < list.size(); ++i) {
            const NhSelOption &o = list[i];
            const bool ok = is_align ? sel.align_ok(o.index) : sel.gender_ok(o.index);
            const RectPx row{ box.x + 2, box.y + 4 + (static_cast<int>(i) * row_h), box.w - 4, row_h };
            const int d = (env.line_h * 2) / 3;
            const RectPx dot{ row.x + env.pad_x, row.y + ((row_h - d) / 2), d, d };
            const PaintColor ring = ok ? PaintColor{ 0.93f, 0.94f, 0.96f, 1.f } : PaintColor{ 0.40f, 0.40f, 0.43f, 1.f };
            env.paint.rounded_frame(dot, ring, d / 2, 1);
            if (o.index == chosen) {
                env.paint.rect(dot.inset(std::max(2, d / 4)), ring);
            }
            const TextColor tc = ok ? default_text() : TextColor{ 0.45f, 0.45f, 0.48f, 1.f };
            env.text.draw(dot.x + d + env.pad_x, row.y + 1, o.name, tc, row.w - d - (2 * env.pad_x));
            this->hits_.push_back(Hit{ row, is_align ? Hit::Align : Hit::Gender, o.index });
        }
    };
    radios(sel.aligns(), col_y, ja ? "属性" : "Alignment", sel.align(), true, focus == NhSelFocus::Align);
    radios(sel.genders(), col_y + env.line_h + aligns_h + m, ja ? "性別" : "Gender", sel.gender(), false, focus == NhSelFocus::Gender);

    // ボタン
    const int by = r.y + r.h - m - btn_h;
    const int bw = std::max(env.cell_w * 10, (r.w - (4 * m)) / 3);
    this->draw_button(env, RectPx{ r.x + m, by, bw, btn_h }, label_play(ja), focus == NhSelFocus::Play, Hit::Play);
    this->draw_button(env, RectPx{ r.x + ((r.w - bw) / 2), by, bw, btn_h }, label_random(ja), focus == NhSelFocus::Random, Hit::Random);
    this->draw_button(env, RectPx{ r.x + r.w - m - bw, by, bw, btn_h }, label_quit(ja), focus == NhSelFocus::Quit, Hit::Quit);
}

// ---- 墓標 ----

void NhWindows::draw_rip(NhDrawEnv &env)
{
    this->draw_scrim(env);
    const bool ja = env.japanese;
    const int m = env.pad_x * 2;
    const int stone_w = std::min(env.area.w - 80, env.cell_w * 30);
    const int w = std::min(env.area.w - 40, std::max(stone_w + (2 * m), env.cell_w * 56));
    const int title_h = env.line_h + 8;
    const int btn_h = env.line_h + 12;
    const int stone_h = env.line_h * 9;
    const int inner_w = w - (2 * m);
    // 後ろの文章（死んだあとの文字）。収まる行数だけ出す。
    this->wrap_text_lines(env.text, inner_w);
    const int max_h = env.area.h - 24;
    const int fixed_h = title_h + m + stone_h + m + btn_h + m;
    const int text_rows = std::max(0, std::min(static_cast<int>(this->text_wrapped_.size()), (max_h - fixed_h) / env.line_h));
    const int h = fixed_h + (text_rows > 0 ? (text_rows * env.line_h) + m : 0);
    const RectPx r = this->centered(env, w, h);
    this->draw_frame(env, r, ja ? "ここに眠る..." : "Rest in peace");
    int y = r.y + title_h + m;
    for (int i = 0; i < text_rows; ++i) {
        const TextLine &line = this->text_wrapped_[static_cast<std::size_t>(i)];
        put(env, r.x + m, y, line.text, default_text(), line.attr, inner_w);
        y += env.line_h;
    }
    if (text_rows > 0) {
        y += m;
    }
    // 墓石（アーチの石に、名前・所持金・死因・年を中央に重ねる）
    const int sx = r.x + ((r.w - stone_w) / 2);
    const PaintColor stone{ 0.58f, 0.60f, 0.64f, 1.f };
    const PaintColor stone_dark{ 0.30f, 0.32f, 0.36f, 1.f };
    const PaintColor grass{ 0.16f, 0.28f, 0.14f, 1.f };
    const int arch = stone_w / 5;
    for (int k = 0; k < 4; ++k) { // 上のアーチ（段で丸みを出す）
        const int inset = (arch * (3 - k)) / 4;
        env.paint.rect(RectPx{ sx + inset, y + ((arch * k) / 4), stone_w - (2 * inset), (arch / 4) + 1 }, stone);
    }
    env.paint.rect(RectPx{ sx, y + arch, stone_w, stone_h - arch }, stone);
    // 縁は左右と下だけ（上の縁を引くと、名前の字の上を横切る）。
    env.paint.rect(RectPx{ sx, y + arch, 2, stone_h - arch }, stone_dark);
    env.paint.rect(RectPx{ sx + stone_w - 2, y + arch, 2, stone_h - arch }, stone_dark);
    env.paint.rect(RectPx{ sx, y + stone_h - 2, stone_w, 2 }, stone_dark);
    env.paint.rect(RectPx{ sx - env.cell_w, y + stone_h - (env.line_h / 2), stone_w + (2 * env.cell_w), env.line_h / 2 }, grass);
    const TextColor ink{ 0.10f, 0.10f, 0.13f, 1.f };
    const auto centered_line = [&](int ly, const std::string &s, int attr) {
        const int tw = env.text.measure(s);
        const int tx = sx + std::max(4, (stone_w - tw) / 2);
        env.text.draw(tx, ly, s, ink, stone_w - 8);
        if (attr == kAtrBold) {
            env.text.draw(tx + 1, ly, s, ink, stone_w - 8);
        }
    };
    int ty = y + (arch / 2);
    centered_line(ty, "R.I.P.", kAtrBold);
    ty += env.line_h + 2;
    centered_line(ty, this->rip_.name, kAtrBold);
    ty += env.line_h;
    centered_line(ty, std::to_string(this->rip_.gold) + (ja ? " 金貨" : " Au"), 0);
    ty += env.line_h;
    int killer_rows = 0;
    for (const std::string &piece : nh_wrap_to_width(env.text, this->rip_.killer, stone_w - 16)) {
        if (killer_rows >= 2) {
            break;
        }
        centered_line(ty, piece, 0);
        ty += env.line_h;
        ++killer_rows;
    }
    centered_line(y + stone_h - env.line_h - 6, std::to_string(this->rip_.year), 0);
    const int bw = std::max(env.cell_w * 10, r.w / 4);
    this->draw_button(env, RectPx{ r.x + ((r.w - bw) / 2), r.y + r.h - m - btn_h, bw, btn_h }, "OK", true, Hit::Close);
}

// ---- まとめ ----

void NhWindows::draw(NhDrawEnv &env)
{
    this->hits_.clear();
    this->pending_tiles_.clear();
    if (this->kind_ == Kind::None) {
        return;
    }
    const UiProbeLayer probe_layer("nethack-win");
    switch (this->kind_) {
    case Kind::Menu:
        if (!env.in_game) {
            env.paint.rect(env.area, kBlack);
        }
        this->desired_width(env.text, env.cell_w, env.line_h, env.pad_x, false);
        this->draw_menu(env);
        break;
    case Kind::Text:
        if (!env.in_game) {
            env.paint.rect(env.area, kBlack);
        }
        this->desired_width(env.text, env.cell_w, env.line_h, env.pad_x, false);
        this->draw_text_window(env);
        break;
    case Kind::Yn:
        if (!env.in_game) {
            this->draw_yn_dialog(env);
        }
        break;
    case Kind::Line:
        if (!env.in_game) {
            this->draw_input_dialog(env);
        }
        break;
    case Kind::Name:
        this->draw_input_dialog(env);
        break;
    case Kind::ExtCmd:
        this->draw_ext_cmd(env);
        break;
    case Kind::Select:
        this->draw_select(env);
        break;
    case Kind::Rip:
        this->draw_rip(env);
        break;
    case Kind::None:
        break;
    }
}

void NhWindows::draw_invent(NhDrawEnv &env, const NhModel &model)
{
    if (!model.perm_invent()) {
        return;
    }
    this->load_tiles();
    const UiProbeLayer probe_layer("nethack-inv");
    const RectPx r{ env.invent.x, env.invent.y, env.invent.w, env.invent.h };
    if (r.empty()) {
        return;
    }
    env.paint.panel(r, solid_panel_style());
    const RectPx list{ r.x + 1, r.y + 3, r.w - 2, r.h - 6 };
    this->draw_menu_rows(env, this->invent_, list, false, false, false);
}

bool NhWindows::queue_glyph_tile(const RectPx &dst, int glyph)
{
    if ((glyph < 0) || !this->load_tiles()) {
        return false;
    }
    const int tile = this->tile_table_.tile_for_glyph(glyph);
    if (tile < 0) {
        return false;
    }
    this->pending_tiles_.push_back(TileDraw{ dst, tile });
    return true;
}

void NhWindows::flush_tiles(const UiImagePainter &images)
{
    if (!this->tiles_ready_ || this->pending_tiles_.empty()) {
        this->pending_tiles_.clear();
        return;
    }
    for (const TileDraw &t : this->pending_tiles_) {
        float u0 = 0.f;
        float v0 = 0.f;
        float u1 = 0.f;
        float v1 = 0.f;
        if (nh_tile_uv(this->tile_table_, t.tile, this->tile_image_.width(), this->tile_image_.height(), u0, v0, u1, v1)) {
            images.draw_region(this->tile_image_, t.dst, u0, v0, u1, v1, 1.f);
        }
    }
    this->pending_tiles_.clear();
}

} // namespace hd2d::nethack
