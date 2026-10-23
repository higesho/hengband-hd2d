/*!
 * @file dcss_windows_ui.cpp
 * @brief 構造を持つ窓（`ui-push`）の描画。捲れる文書・説明の窓（アイテム・呪文・怪物・地形ほか）・進み具合など。
 *
 * 説明の窓は原作の `describe.cc` の `show_description`（題の行・本文の捲り領域・下の案内）と同じ並びで描く。
 * 幅は字の幅の 80 字ぶんまで（`vbox->max_size().width = char_width()*80`）。
 */
#include "dcss/dcss_windows.h"

#include "app/ui_probe.h"
#include "dcss/dcss_windows_internal.h"
#include "render/gl_core.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>

namespace hd2d::dcss {

namespace {

using json = nlohmann::json;

const char *const kColourNames[16] = { "black", "blue", "green", "cyan", "red", "magenta", "brown", "lightgrey", "darkgrey", "lightblue",
    "lightgreen", "lightcyan", "lightred", "lightmagenta", "yellow", "white" };

std::string jstr(const json &o, const char *key)
{
    const auto it = o.find(key);
    if ((it == o.end()) || !it->is_string()) {
        return std::string();
    }
    return it->get<std::string>();
}

int jint(const json &o, const char *key, int fallback = 0)
{
    const auto it = o.find(key);
    if ((it == o.end()) || !it->is_number()) {
        return fallback;
    }
    return it->get<int>();
}

std::string trimmed(std::string s)
{
    const auto is_ws = [](char c) { return (c == ' ') || (c == '\n') || (c == '\r') || (c == '\t'); };
    while (!s.empty() && is_ws(s.back())) {
        s.pop_back();
    }
    std::size_t i = 0;
    while ((i < s.size()) && is_ws(s[i])) {
        ++i;
    }
    return s.substr(i);
}

//! 空でないものを空行で区切ってつなぐ（原作 `process_description`）。
std::string join_blocks(const std::vector<std::string> &parts)
{
    std::string out;
    for (const std::string &p : parts) {
        const std::string t = trimmed(p);
        if (t.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += "\n\n";
        }
        out += t;
    }
    return out;
}

std::string coloured(int colour, const std::string &text)
{
    const char *name = kColourNames[colour & 15];
    return std::string("<") + name + ">" + text + "</" + name + ">";
}

//! 呪文の一覧（`spellset`）を字にする。`SPELLSET_PLACEHOLDER` の所へ入れる。
std::string spellset_text(const json &spellset, bool use_colour)
{
    std::string out;
    if (!spellset.is_array()) {
        return out;
    }
    for (const json &book : spellset) {
        const std::string label = trimmed(jstr(book, "label"));
        if (!label.empty()) {
            out += label + "\n";
        }
        const auto spells = book.find("spells");
        if ((spells == book.end()) || !spells->is_array()) {
            continue;
        }
        for (const json &sp : *spells) {
            std::string line = " " + jstr(sp, "letter") + " - " + jstr(sp, "title");
            const std::string effect = jstr(sp, "effect");
            const std::string range = jstr(sp, "range_string");
            if (!effect.empty()) {
                line += "  " + effect;
            }
            if (!range.empty()) {
                line += "  " + range;
            }
            out += use_colour ? coloured(jint(sp, "colour", kLightGrey), line) : line;
            out += "\n";
        }
    }
    return trimmed(out);
}

std::string replace_spellset(std::string body, const std::string &spells)
{
    const std::string key = "SPELLSET_PLACEHOLDER";
    const std::size_t at = body.find(key);
    if (at != std::string::npos) {
        body.replace(at, key.size(), spells.empty() ? std::string() : ("\n" + spells + "\n"));
    }
    return body;
}

void collect_text(const json &o, std::vector<std::string> &out, int depth)
{
    if (depth > 4) {
        return;
    }
    if (o.is_string()) {
        out.push_back(o.get<std::string>());
        return;
    }
    if (o.is_array()) {
        for (const json &e : o) {
            collect_text(e, out, depth + 1);
        }
        return;
    }
    if (!o.is_object()) {
        return;
    }
    static const char *const kKeys[] = { "title", "name", "prefix", "body", "description", "desc", "status", "quote", "suffix", "footer",
        "information", "features", "changes", "bar_text", "prompt" };
    for (const char *key : kKeys) {
        const auto it = o.find(key);
        if (it == o.end()) {
            continue;
        }
        if (it->is_string()) {
            if (!it->get<std::string>().empty()) {
                out.push_back(it->get<std::string>());
            }
        } else if (it->is_array() || it->is_object()) {
            collect_text(*it, out, depth + 1);
        }
    }
}

int first_tile(const json &d, const char *key)
{
    const auto it = d.find(key);
    if ((it != d.end()) && it->is_object()) {
        return jint(*it, "t", 0);
    }
    return 0;
}

//! 操作の文の 1 つ（「(q)セットする」）の位置。1 行の中の半角単位のマスと、押すキー。
struct ActionSpan {
    int c0{ 0 };
    int c1{ 0 }; //!< 終わりの次のマス
    int hotkey{ 0 };
};

/*!
 * @brief 説明の下の操作の文を、押せる 1 つずつに切る（ウェブ版 `ui-layouts.js` の `clickify_action` と同じ考え方）。
 * @details 区切りは「、」「,」と、終わりの「。」「.」。「または」「or 」の前置きは操作に含めない。
 * 操作は、区切りの間に `(x)` の形がある部分。押すキーは括弧の中の 1 字。
 */
std::vector<ActionSpan> action_spans(const DcssStyledLine &line)
{
    std::vector<ActionSpan> out;
    std::vector<int> cell_x(line.size() + 1, 0);
    for (std::size_t i = 0; i < line.size(); ++i) {
        cell_x[i + 1] = cell_x[i] + dcss_cell_width(line[i].ch);
    }
    const auto is_delim = [&](std::size_t i) {
        const char32_t c = line[i].ch;
        return (c == U'、') || (c == U',') || (c == U'。') || ((c == U'.') && ((i + 1 >= line.size()) || (line[i + 1].ch == U' ')));
    };
    const auto starts_with = [&](std::size_t i, const std::u32string &word) {
        if (i + word.size() > line.size()) {
            return false;
        }
        for (std::size_t k = 0; k < word.size(); ++k) {
            if (line[i + k].ch != word[k]) {
                return false;
            }
        }
        return true;
    };
    std::size_t seg = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if ((i < line.size()) && !is_delim(i)) {
            continue;
        }
        std::size_t b = seg;
        while ((b < i) && (line[b].ch == U' ')) {
            ++b;
        }
        if (starts_with(b, U"または")) {
            b += 3;
        } else if (starts_with(b, U"or ")) {
            b += 3;
        }
        int hotkey = 0;
        for (std::size_t k = b; k + 2 < i; ++k) {
            if ((line[k].ch == U'(') && (line[k + 2].ch == U')')) {
                hotkey = static_cast<int>(line[k + 1].ch);
                break;
            }
        }
        std::size_t e = i;
        while ((e > b) && (line[e - 1].ch == U' ')) {
            --e;
        }
        if ((hotkey > 0) && (e > b)) {
            out.push_back(ActionSpan{ cell_x[b], cell_x[e], hotkey });
        }
        seg = i + 1;
    }
    return out;
}

} // namespace

void DcssWindows::draw_scroll_text(DcssWindow &w, int depth, UiPaint &paint, const UiImagePainter &images, const ScrollContent &c)
{
    View &v = *this->views_[w.generation];
    const DcssTextMeasure &M = this->measure_;
    const int wrap_w = M.char_w * 80;

    std::vector<DcssStyledLine> title_lines;
    if (!c.title.empty()) {
        title_lines = dcss_split_styled_lines(dcss_parse_window_text(c.title, kLightGrey, kLightGrey));
    }
    int icon_w = 0;
    int icon_h = 0;
    if (!c.tiles.empty() && this->atlas_.ready()) {
        icon_w = 32;
        icon_h = 32;
    }
    const int title_text_w = dcss_lines_width(title_lines, M);
    const int title_total_w = title_text_w + ((icon_w > 0) ? (icon_w + 10) : 0);
    const int title_row_h = std::max(icon_h, static_cast<int>(title_lines.size()) * M.line_h);
    const int title_h = (title_lines.empty() && (icon_w == 0)) ? 0 : (title_row_h + 20);

    std::vector<DcssStyledLine> footer_lines;
    if (!c.footer.empty()) {
        footer_lines = dcss_wrap_text(c.footer, wrap_w, kLightGrey, M);
    }
    const int footer_h = footer_lines.empty() ? 0 : (static_cast<int>(footer_lines.size()) * M.line_h + 20);

    std::vector<DcssStyledLine> body = dcss_wrap_text(c.body, wrap_w, c.body_colour, M);
    const int body_w = dcss_lines_width(body, M);
    const int nat_w = std::min(wrap_w, std::max({ body_w, title_total_w, dcss_lines_width(footer_lines, M) }));

    // 窓の幅を決め、字が狭くなったらその幅で折り返し直す。
    DcssPopupGeometry geo = dcss_popup_place(this->area_, nat_w, 1, w.centred, depth, this->small_layout_);
    if (geo.content.w < nat_w) {
        body = dcss_wrap_text(c.body, geo.content.w, c.body_colour, M);
        footer_lines = c.footer.empty() ? footer_lines : dcss_wrap_text(c.footer, geo.content.w, kLightGrey, M);
    }
    const int body_h = static_cast<int>(body.size()) * M.line_h;
    const int footer_h2 = footer_lines.empty() ? 0 : (static_cast<int>(footer_lines.size()) * M.line_h + 20);
    geo = dcss_popup_place(this->area_, std::min(nat_w, geo.content.w), title_h + body_h + footer_h2, w.centred, depth, this->small_layout_);
    const RectPx content = geo.content;
    const int view_h = std::max(0, std::min(body_h, content.h - title_h - footer_h2));
    v.geo = geo;
    v.body_viewport = RectPx{ content.x, content.y + title_h, content.w, view_h };
    v.body_total = body_h;
    v.line_h = M.line_h;
    v.title_rect = RectPx{ content.x, content.y, content.w, title_h };
    v.more_rect = RectPx{ content.x, content.y + title_h + view_h, content.w, footer_h2 };

    const int max_scroll = std::max(0, body_h - view_h);
    if (!w.view_init) {
        w.view_init = true;
        w.scroll_y = c.start_at_end ? max_scroll : 0;
    }
    w.scroll_y = std::clamp(w.scroll_y, 0, max_scroll);

    this->draw_scrim_and_frame(paint, geo);
    v.hits.clear();
    if (c.footer_actions) {
        // 下の操作の文の各操作を押せるようにする。マウスの下は灰色（ウェブ版 `span[data-hotkey]:hover` の #555）。
        int fy = v.more_rect.y + 20;
        int index = 0;
        for (const DcssStyledLine &l : footer_lines) {
            for (const ActionSpan &a : action_spans(l)) {
                const RectPx r{ content.x + (a.c0 * M.half_w), fy, (a.c1 - a.c0) * M.half_w, M.line_h };
                v.hits.push_back(View::Hit{ r, 3, index, a.hotkey });
                if (w.hover_action == index) {
                    paint.rect(r, dcss_paint_rgb(85, 85, 85));
                }
                ++index;
            }
            fy += M.line_h;
        }
    }
    paint.flush();

    // 題の行
    if (title_h > 0) {
        const int total_w = title_total_w;
        int x = c.title_centred ? (content.x + std::max(0, (content.w - total_w) / 2)) : content.x;
        const int row_y = content.y;
        if (icon_w > 0) {
            for (const int tile : c.tiles) {
                this->atlas_.draw(images, tile, x, row_y + ((title_row_h - icon_h) / 2), 1, 0);
            }
            x += icon_w + 10;
        }
        int ty = row_y + ((title_row_h - (static_cast<int>(title_lines.size()) * M.line_h)) / 2);
        for (const DcssStyledLine &l : title_lines) {
            this->draw_styled(x, ty, l);
            ty += M.line_h;
        }
    }
    // 下の案内
    {
        int y = v.more_rect.y + 20;
        for (const DcssStyledLine &l : footer_lines) {
            this->draw_styled(content.x, y, l);
            y += M.line_h;
        }
    }
    this->font_.flush();

    // 本文（捲り領域の中だけ）
    const RectPx vp = v.body_viewport;
    if (!vp.empty()) {
        using ScissorFn = void (*)(int, int, int, int);
        static ScissorFn scissor = reinterpret_cast<ScissorFn>(SDL_GL_GetProcAddress("glScissor"));
        if (scissor != nullptr) {
            gl::glEnable(0x0C11);
            scissor(scaled(vp.x), this->screen_h_ - scaled(vp.y + vp.h), scaled(vp.w), scaled(vp.h));
        }
        const int first = std::max(0, w.scroll_y / M.line_h - 1);
        const int last = std::min(static_cast<int>(body.size()), (w.scroll_y + view_h) / M.line_h + 2);
        for (int i = first; i < last; ++i) {
            this->draw_styled(vp.x, vp.y - w.scroll_y + (i * M.line_h), body[static_cast<std::size_t>(i)]);
        }
        this->font_.flush();
        if (scissor != nullptr) {
            gl::glDisable(0x0C11);
        }
    }
    this->draw_scroller_bar(paint, vp, body_h, w.scroll_y);
    paint.flush();

    UiProbe &probe = ui_probe();
    if (probe.enabled()) {
        ProbeWindow pw;
        pw.kind = "dcss-window";
        pw.type = w.type;
        pw.title = dcss_strip_window_text(c.title);
        pw.has_rect = true;
        pw.rect = ProbeRect{ geo.frame.x, geo.frame.y, geo.frame.w, geo.frame.h };
        probe.window(std::move(pw));
    }
}

void DcssWindows::draw_ui(DcssWindow &w, int depth, UiPaint &paint, const UiImagePainter &images)
{
    const json &d = w.data;
    const std::string &type = w.type;
    ScrollContent c;

    if (type == "newgame-choice") {
        this->draw_newgame_choice(w, depth, paint, images);
        return;
    }
    if (type == "formatted-scroller") {
        c.title = jstr(d, "title");
        c.title_centred = true;
        c.body = (w.state.contains("text") && w.state["text"].is_string()) ? w.state["text"].get<std::string>() : jstr(d, "text");
        c.footer = jstr(d, "more");
        c.start_at_end = (jint(d, "start_at_end") != 0);
    } else if (type == "describe-generic") {
        c.title = jstr(d, "title");
        const int tile = first_tile(d, "tile");
        if (tile > 0) {
            c.tiles.push_back(tile);
        }
        const std::string quote = trimmed(jstr(d, "quote"));
        if ((w.pane == 1) && !quote.empty()) {
            c.body = quote;
        } else {
            c.body = join_blocks({ jstr(d, "prefix"), jstr(d, "body"), jstr(d, "suffix"), jstr(d, "footer") });
        }
        if (!quote.empty()) {
            c.footer = (w.pane == 1) ? (this->japanese_ ? "[<white>!</white>]: 説明|<white>引用</white>" : "[<white>!</white>]: Description|<white>Quote</white>")
                                     : (this->japanese_ ? "[<white>!</white>]: <white>説明</white>|引用" : "[<white>!</white>]: <white>Description</white>|Quote");
        }
    } else if (type == "describe-feature-wide") {
        const auto feats = d.find("feats");
        std::vector<std::string> blocks;
        if ((feats != d.end()) && feats->is_array()) {
            for (const json &f : *feats) {
                std::string block = coloured(kWhite, jstr(f, "title"));
                const std::string body = trimmed(jstr(f, "body"));
                if (!body.empty() && (body != jstr(f, "title"))) {
                    block += "\n" + body;
                }
                const std::string quote = trimmed(jstr(f, "quote"));
                if (!quote.empty()) {
                    block += "\n\n" + quote;
                }
                blocks.push_back(block);
                if (c.tiles.empty()) {
                    const int t = first_tile(f, "tile");
                    if (t > 0) {
                        c.tiles.push_back(t);
                    }
                }
            }
        }
        c.body = join_blocks(blocks);
        c.footer = jstr(d, "actions");
        c.footer_actions = true;
    } else if (type == "describe-item") {
        c.title = jstr(d, "title");
        const auto tiles = d.find("tiles");
        if ((tiles != d.end()) && tiles->is_array()) {
            for (const json &t : *tiles) {
                if (t.is_object() && (jint(t, "t") > 0)) {
                    c.tiles.push_back(jint(t, "t"));
                }
            }
        }
        c.body = replace_spellset(jstr(d, "body"), spellset_text(d.contains("spellset") ? d["spellset"] : json::array(), true));
        if (w.state.contains("body") && w.state["body"].is_string()) {
            c.body = w.state["body"].get<std::string>();
        }
        c.body = trimmed(c.body);
        const std::string actions = jstr(d, "actions");
        if (!actions.empty()) {
            c.footer = coloured(kCyan, actions);
            c.footer_actions = true;
        }
    } else if (type == "describe-spell") {
        c.title = jstr(d, "title");
        const int tile = first_tile(d, "tile");
        if (tile > 0) {
            c.tiles.push_back(tile);
        }
        c.body = trimmed((w.state.contains("body") && w.state["body"].is_string()) ? w.state["body"].get<std::string>() : jstr(d, "desc"));
    } else if (type == "describe-monster") {
        c.title = jstr(d, "title");
        const int fg = jint(d, "fg_idx");
        if (fg > 0) {
            c.tiles.push_back(fg);
        }
        const std::string status = trimmed(jstr(d, "status"));
        const std::string quote = trimmed(jstr(d, "quote"));
        std::vector<std::string> panes;
        panes.push_back(replace_spellset(jstr(d, "body"), spellset_text(d.contains("spellset") ? d["spellset"] : json::array(), false)));
        std::vector<std::string> names = { this->japanese_ ? "説明" : "Description" };
        if (!status.empty()) {
            panes.push_back(status);
            names.push_back(this->japanese_ ? "状態" : "Status");
        }
        if (!quote.empty()) {
            panes.push_back(quote);
            names.push_back(this->japanese_ ? "引用" : "Quote");
        }
        const int pane = (w.pane >= 0) && (w.pane < static_cast<int>(panes.size())) ? w.pane : 0;
        c.body = trimmed(panes[static_cast<std::size_t>(pane)]);
        if (names.size() > 1) {
            std::string footer;
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (i > 0) {
                    footer += " | ";
                }
                footer += (static_cast<int>(i) == pane) ? coloured(kWhite, names[i]) : names[i];
            }
            c.footer = footer;
        }
    } else if (type == "describe-god") {
        const std::string colour_name = coloured(jint(d, "colour", kLightGrey), jstr(d, "name"));
        c.title = colour_name;
        const int tile = first_tile(d, "tile");
        if (tile > 0) {
            c.tiles.push_back(tile);
        }
        std::vector<std::string> panes;
        std::string overview = trimmed(jstr(d, "description"));
        const std::string favour = trimmed(jstr(d, "title")) + "  " + trimmed(jstr(d, "favour"));
        overview += "\n\n" + coloured(jint(d, "colour", kLightGrey), favour);
        const std::string powers_list = jstr(d, "powers_list");
        if (!trimmed(powers_list).empty()) {
            overview += "\n\n" + trimmed(powers_list);
        }
        panes.push_back(overview);
        panes.push_back(join_blocks({ jstr(d, "powers"), jstr(d, "info_table") }));
        panes.push_back(trimmed(jstr(d, "wrath")));
        if (!trimmed(jstr(d, "extra")).empty()) {
            panes.push_back(trimmed(jstr(d, "extra")));
        }
        const int pane = (w.pane >= 0) && (w.pane < static_cast<int>(panes.size())) ? w.pane : 0;
        c.body = panes[static_cast<std::size_t>(pane)];
        if (w.state.contains("prompt") && w.state["prompt"].is_string()) {
            c.footer = coloured(kCyan, w.state["prompt"].get<std::string>());
        }
    } else if (type == "describe-cards") {
        std::vector<std::string> blocks;
        const auto cards = d.find("cards");
        if ((cards != d.end()) && cards->is_array()) {
            for (const json &card : *cards) {
                blocks.push_back(coloured(kWhite, jstr(card, "name")) + "\n" + trimmed(jstr(card, "desc")));
            }
        }
        c.body = join_blocks(blocks);
    } else if (type == "version") {
        c.title = jstr(d, "information");
        c.body = join_blocks({ jstr(d, "features"), jstr(d, "changes") });
    } else if (type == "game-over") {
        c.title = jstr(d, "title");
        const int tile = first_tile(d, "tile");
        if (tile > 0) {
            c.tiles.push_back(tile);
        }
        c.body = trimmed(jstr(d, "body"));
    } else if (type == "seed-selection") {
        c.title = jstr(d, "title");
        c.body = trimmed(jstr(d, "body"));
        c.footer = jstr(d, "footer");
    } else if (type == "progress-bar") {
        c.title = jstr(d, "title");
        const std::string bar = (w.state.contains("bar_text") && w.state["bar_text"].is_string()) ? w.state["bar_text"].get<std::string>() : jstr(d, "bar_text");
        const std::string status = (w.state.contains("status") && w.state["status"].is_string()) ? w.state["status"].get<std::string>() : jstr(d, "status");
        c.body = join_blocks({ bar, status });
    } else if (type == "msgwin-get-line") {
        c.title = jstr(d, "prompt");
        const DcssTextInput &in = this->stack_.text_input();
        c.body = (in.active ? in.typed : w.typed) + "_";
    } else if (type == "newgame-random-combo") {
        c.title = jstr(d, "prompt");
        c.body = std::string();
    } else {
        std::vector<std::string> parts;
        collect_text(d, parts, 0);
        c.body = join_blocks(parts);
        if (c.body.empty()) {
            c.body = type;
        }
    }
    this->draw_scroll_text(w, depth, paint, images, c);
}

} // namespace hd2d::dcss
