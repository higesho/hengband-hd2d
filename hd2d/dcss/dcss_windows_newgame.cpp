/*!
 * @file dcss_windows_newgame.cpp
 * @brief キャラクター作成の選択の窓（`newgame-choice`。種族・職業・武器）の描画。原作 `newgame.cc`・`outer-menu.cc` の再現。
 *
 * 並びは原作の窓の部品の規則を写した（手元のタイル版の絵 `run1_c.png`・種族の選択の絵と 1 画素の単位で見比べて決めた）:
 * - 題の行（武器の選択は人形の絵 32 + 余白 10 + 字。下の余白 20）・問い・主の格子（上下の余白 15）・説明の欄（種族・職業）・副の格子
 * - ボタン 1 つ: 余白（上 2・右 10・下 2・左 2）の中に、絵 32 + 余白 6、字。高さ 36。強調は薄い白の塗り + 色つきの 1 画素の枠
 * - 格子の見出し（初級・中級…）: 字の上下に 7、左に 40 の余白
 * - 副の格子のボタン: 字の上下に 4、左右に 8 の余白。格子は幅いっぱいに引き伸ばす
 */
#include "dcss/dcss_windows.h"

#include "app/ui_probe.h"
#include "dcss/dcss_windows_internal.h"

#include <SDL2/SDL.h>

#include <algorithm>

namespace hd2d::dcss {

namespace {

using json = nlohmann::json;

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

struct GridButton {
    int x{ 0 };
    int y{ 0 };
    int hotkey{ 0 };
    int highlight{ kLightGrey };
    bool is_label{ false };
    std::vector<int> tiles;
    std::vector<DcssStyledLine> labels;
    std::string description;
    int natural_w{ 0 };
    int natural_h{ 0 };
    RectPx rect;
};

struct Grid {
    std::string menu_id;
    int width{ 1 };
    std::vector<GridButton> items;
    std::vector<int> col_w;
    std::vector<int> row_h;
    std::vector<int> col_x;
    std::vector<int> row_y;
    int natural_w{ 0 };
    int height{ 0 };
    int total_w{ 0 };
};

Grid parse_grid(const json &side, bool main_grid, const DcssTextMeasure &M)
{
    Grid g;
    g.menu_id = jstr(side, "menu_id");
    g.width = std::max(1, jint(side, "width", 1));
    const auto buttons = side.find("buttons");
    if ((buttons != side.end()) && buttons->is_array()) {
        for (const json &b : *buttons) {
            GridButton gb;
            gb.x = jint(b, "x");
            gb.y = jint(b, "y");
            gb.hotkey = jint(b, "hotkey");
            gb.highlight = jint(b, "highlight_colour", kLightGrey) & 15;
            gb.description = jstr(b, "description");
            const auto tiles = b.find("tile");
            if ((tiles != b.end()) && tiles->is_array()) {
                for (const json &t : *tiles) {
                    if (t.is_object()) {
                        gb.tiles.push_back(jint(t, "t"));
                    }
                }
            }
            const auto labels = b.find("labels");
            if ((labels != b.end()) && labels->is_array()) {
                for (const json &l : *labels) {
                    if (l.is_string()) {
                        gb.labels.push_back(dcss_parse_window_text(l.get<std::string>(), kLightGrey, kLightGrey));
                    }
                }
            } else if (b.contains("label") && b["label"].is_string()) {
                gb.labels.push_back(dcss_parse_window_text(jstr(b, "label"), kLightGrey, kLightGrey));
            }
            int text_w = 0;
            for (const DcssStyledLine &l : gb.labels) {
                text_w += M.cells_width(l);
            }
            if (main_grid) {
                gb.natural_w = 2 + 38 + text_w + 10;
                gb.natural_h = 36;
            } else {
                gb.natural_w = text_w + 16;
                gb.natural_h = M.line_h + 8;
            }
            g.items.push_back(std::move(gb));
        }
    }
    const auto labels = side.find("labels");
    if ((labels != side.end()) && labels->is_array()) {
        for (const json &l : *labels) {
            GridButton gb;
            gb.is_label = true;
            gb.x = jint(l, "x");
            gb.y = jint(l, "y");
            gb.labels.push_back(dcss_parse_window_text(jstr(l, "label"), kLightBlue, kLightBlue));
            gb.natural_w = 40 + M.cells_width(gb.labels.front());
            gb.natural_h = M.line_h + 14;
            g.items.push_back(std::move(gb));
        }
    }
    int max_y = 0;
    for (const GridButton &gb : g.items) {
        max_y = std::max(max_y, gb.y);
    }
    g.col_w.assign(static_cast<std::size_t>(g.width), 0);
    g.row_h.assign(static_cast<std::size_t>(max_y + 1), 0);
    for (const GridButton &gb : g.items) {
        if ((gb.x >= 0) && (gb.x < g.width)) {
            g.col_w[static_cast<std::size_t>(gb.x)] = std::max(g.col_w[static_cast<std::size_t>(gb.x)], gb.natural_w);
        }
        if ((gb.y >= 0) && (gb.y <= max_y)) {
            g.row_h[static_cast<std::size_t>(gb.y)] = std::max(g.row_h[static_cast<std::size_t>(gb.y)], gb.natural_h);
        }
    }
    for (const int w : g.col_w) {
        g.natural_w += w;
    }
    for (const int h : g.row_h) {
        g.height += h;
    }
    return g;
}

void place_grid(Grid &g, int x0, int y0, int total_w)
{
    g.total_w = total_w;
    const int extra = std::max(0, total_w - g.natural_w);
    const int share = extra / std::max(1, g.width);
    g.col_x.assign(g.col_w.size(), 0);
    int x = x0;
    for (std::size_t i = 0; i < g.col_w.size(); ++i) {
        g.col_x[i] = x;
        g.col_w[i] += share;
        x += g.col_w[i];
    }
    g.row_y.assign(g.row_h.size(), 0);
    int y = y0;
    for (std::size_t i = 0; i < g.row_h.size(); ++i) {
        g.row_y[i] = y;
        y += g.row_h[i];
    }
    for (GridButton &gb : g.items) {
        if ((gb.x < 0) || (gb.x >= g.width) || (gb.y < 0) || (gb.y >= static_cast<int>(g.row_h.size()))) {
            continue;
        }
        gb.rect = RectPx{ g.col_x[static_cast<std::size_t>(gb.x)], g.row_y[static_cast<std::size_t>(gb.y)], g.col_w[static_cast<std::size_t>(gb.x)],
            g.row_h[static_cast<std::size_t>(gb.y)] };
        if (!gb.is_label) {
            gb.rect.h = gb.natural_h;
        }
    }
}

} // namespace

void DcssWindows::draw_newgame_choice(DcssWindow &w, int depth, UiPaint &paint, const UiImagePainter &images)
{
    View &v = *this->views_[w.generation];
    const DcssTextMeasure &M = this->measure_;
    const json &d = w.data;

    const DcssStyledLine title = dcss_parse_window_text(jstr(d, "title"), kLightGrey, kLightGrey);
    const std::string prompt_text = jstr(d, "prompt");
    DcssStyledLine prompt;
    if (!prompt_text.empty()) {
        prompt = dcss_parse_window_text(prompt_text, kLightGrey, kLightGrey);
    }
    std::vector<std::pair<int, int>> doll;
    if (d.contains("doll") && d["doll"].is_array()) {
        for (const json &part : d["doll"]) {
            if (part.is_array() && (part.size() >= 2) && part[0].is_number() && part[1].is_number()) {
                doll.emplace_back(part[0].get<int>(), part[1].get<int>());
            }
        }
    }
    const bool has_doll = !doll.empty() && this->atlas_.ready();

    Grid main_grid = d.contains("main-items") ? parse_grid(d["main-items"], true, M) : Grid{};
    Grid sub_grid = d.contains("sub-items") ? parse_grid(d["sub-items"], false, M) : Grid{};
    bool has_descriptions = false;
    for (const GridButton &gb : main_grid.items) {
        if (!gb.description.empty()) {
            has_descriptions = true;
            break;
        }
    }

    // 窓の幅（題・主の格子・副の格子のうち最も広いもの）。
    const int title_w = M.cells_width(title) + (has_doll ? 42 : 0);
    const int content_w = std::max({ title_w, main_grid.natural_w, sub_grid.natural_w, M.cells_width(prompt) });

    // 説明の欄の高さ（全ボタンの説明のうち最も行が多いもの）。
    int desc_lines = 0;
    if (has_descriptions) {
        for (const Grid *g : { &main_grid, &sub_grid }) {
            for (const GridButton &gb : g->items) {
                if (!gb.description.empty()) {
                    desc_lines = std::max(desc_lines, static_cast<int>(dcss_wrap_text(gb.description, content_w, kWhite, M).size()));
                }
            }
        }
    }
    const bool title_is_hbox = has_doll || !prompt.empty();
    const int title_h = title_is_hbox ? (std::max(32, M.line_h) + 20) : M.line_h;
    const int prompt_h = prompt.empty() ? 0 : M.line_h;
    const int desc_h = has_descriptions ? ((desc_lines * M.line_h) + 15) : 0;
    const int total_h = title_h + prompt_h + 15 + main_grid.height + 15 + desc_h + sub_grid.height;

    const DcssPopupGeometry geo = dcss_popup_place(this->area_, content_w, total_h, w.centred, depth, this->small_layout_, total_h);
    v.geo = geo;
    v.need_h = total_h + (2 * this->fit_pad());
    const RectPx content = geo.content;

    int y = content.y;
    const int title_y = y;
    y += title_h;
    const int prompt_y = y;
    y += prompt_h;
    y += 15;
    place_grid(main_grid, content.x, y, content.w);
    y += main_grid.height + 15;
    const int desc_y = y;
    y += desc_h;
    place_grid(sub_grid, content.x, y, content.w);

    // 強調の見た目と、押せる所の記録。
    v.hits.clear();
    int focus_main = -1;
    int focus_sub = -1;
    const DcssWindow &cw = w;
    const auto focus_index = [&](const Grid &g, int base) {
        for (std::size_t i = 0; i < g.items.size(); ++i) {
            if (!g.items[i].is_label && (g.items[i].hotkey == cw.focus_hotkey) && (cw.focus_hotkey != 0)) {
                return static_cast<int>(i) + base;
            }
        }
        return -1;
    };
    focus_main = focus_index(main_grid, 0);
    focus_sub = focus_index(sub_grid, 0);

    this->draw_scrim_and_frame(paint, geo);
    // 強調（ボタンの枠）
    const auto paint_button = [&](const GridButton &gb, bool focused, bool hovered) {
        if (gb.is_label) {
            return;
        }
        const RectPx r = gb.rect;
        const DcssRgb &rgb = dcss_term_rgb(gb.highlight);
        if (focused) {
            paint.rect(r, dcss_paint_rgb(255, 255, 255, 25));
        }
        if (focused || hovered) {
            const int div = focused ? 1 : 2;
            const PaintColor line = dcss_paint_rgb(rgb.r / div, rgb.g / div, rgb.b / div);
            paint.rect(RectPx{ r.x + 1, r.y + 1, r.w - 1, 1 }, line);
            paint.rect(RectPx{ r.x + 1, r.y + r.h - 1, r.w - 1, 1 }, line);
            paint.rect(RectPx{ r.x + 1, r.y + 1, 1, r.h - 1 }, line);
            paint.rect(RectPx{ r.x + r.w - 1, r.y + 1, 1, r.h - 1 }, line);
        }
    };
    for (std::size_t i = 0; i < main_grid.items.size(); ++i) {
        paint_button(main_grid.items[i], static_cast<int>(i) == focus_main, static_cast<int>(i) == w.hover_button && static_cast<int>(i) != focus_main);
    }
    for (std::size_t i = 0; i < sub_grid.items.size(); ++i) {
        paint_button(sub_grid.items[i], static_cast<int>(i) == focus_sub, (static_cast<int>(i) + 100000) == w.hover_button && static_cast<int>(i) != focus_sub);
    }
    paint.flush();

    // 絵（題の人形・ボタンの絵）
    if (this->atlas_.ready()) {
        if (has_doll) {
            for (const auto &part : doll) {
                this->atlas_.draw(images, part.first, content.x, title_y, 1, part.second);
            }
        }
        for (const GridButton &gb : main_grid.items) {
            if (gb.is_label) {
                continue;
            }
            for (const int tile : gb.tiles) {
                this->atlas_.draw(images, tile, gb.rect.x + 2, gb.rect.y + 2, 1, 0);
            }
        }
    }

    // 字
    {
        int tx = content.x + (has_doll ? 42 : 0);
        const int ty = title_is_hbox ? (title_y + ((std::max(32, M.line_h) - M.line_h) / 2)) : title_y;
        this->draw_styled(tx, ty, title);
        if (!prompt.empty()) {
            this->draw_styled(content.x, prompt_y, prompt);
        }
    }
    for (const GridButton &gb : main_grid.items) {
        if (gb.is_label) {
            this->draw_styled(gb.rect.x + 40, gb.rect.y + 7, gb.labels.front());
            continue;
        }
        const int x0 = gb.rect.x + 2 + 38;
        const int ty = gb.rect.y + ((gb.rect.h - M.line_h) / 2);
        if (gb.labels.size() >= 2) {
            // 字の幅の余りを 2 つの字に等分する（`set_main_alignment(STRETCH)`）。
            const int w1 = M.cells_width(gb.labels[0]);
            const int w2 = M.cells_width(gb.labels[1]);
            const int avail = gb.rect.w - 2 - 10 - 38;
            const int extra = std::max(0, avail - w1 - w2);
            this->draw_styled(x0, ty, gb.labels[0]);
            this->draw_styled(x0 + w1 + (extra / 2), ty, gb.labels[1]);
        } else if (!gb.labels.empty()) {
            this->draw_styled(x0, ty, gb.labels[0]);
        }
    }
    for (const GridButton &gb : sub_grid.items) {
        if (gb.is_label || gb.labels.empty()) {
            continue;
        }
        this->draw_styled(gb.rect.x + 8, gb.rect.y + 4, gb.labels[0]);
    }
    // 説明（強調しているボタンの説明）
    if (has_descriptions) {
        std::string desc;
        if (focus_main >= 0) {
            desc = main_grid.items[static_cast<std::size_t>(focus_main)].description;
        } else if (focus_sub >= 0) {
            desc = sub_grid.items[static_cast<std::size_t>(focus_sub)].description;
        }
        if (!desc.empty()) {
            this->draw_lines(content.x, desc_y, dcss_wrap_text(desc, content.w, kWhite, M));
        }
    }
    this->font_.flush();

    // 押せる所の記録（当たり判定と検査の窓口）
    UiProbe &probe = ui_probe();
    for (std::size_t i = 0; i < main_grid.items.size(); ++i) {
        const GridButton &gb = main_grid.items[i];
        if (gb.is_label) {
            continue;
        }
        v.hits.push_back(View::Hit{ gb.rect, 1, static_cast<int>(i), gb.hotkey });
        if (probe.enabled()) {
            std::string label;
            for (const DcssStyledLine &l : gb.labels) {
                for (const DcssStyledChar &c : l) {
                    label += dcss_encode_utf8(c.ch);
                }
            }
            probe.button(label, ProbeRect{ gb.rect.x, gb.rect.y, gb.rect.w, gb.rect.h }, true, static_cast<int>(i) == focus_main, "dcss-choice", false,
                "choice:" + std::to_string(gb.hotkey));
        }
    }
    for (std::size_t i = 0; i < sub_grid.items.size(); ++i) {
        const GridButton &gb = sub_grid.items[i];
        if (gb.is_label) {
            continue;
        }
        v.hits.push_back(View::Hit{ gb.rect, 1, static_cast<int>(i) + 100000, gb.hotkey });
        if (probe.enabled() && !gb.labels.empty()) {
            std::string label;
            for (const DcssStyledChar &c : gb.labels[0]) {
                label += dcss_encode_utf8(c.ch);
            }
            probe.button(label, ProbeRect{ gb.rect.x, gb.rect.y, gb.rect.w, gb.rect.h }, true, static_cast<int>(i) == focus_sub, "dcss-choice", false,
                "choice:" + std::to_string(gb.hotkey));
        }
    }
    if (probe.enabled()) {
        ProbeWindow pw;
        pw.kind = "dcss-window";
        pw.type = w.type;
        // 検査の窓口の名前（言語に依存しない選択の名前。`dc_m0_check.py` などが「種族の選択」「職業の選択」を待つ）。
        const std::string menu_id = d.contains("main-items") ? jstr(d["main-items"], "menu_id") : std::string();
        pw.title = (menu_id == "species-main") ? "種族の選択" : (menu_id == "background-main") ? "職業の選択" : (menu_id == "weapon-main") ? "武器の選択" : dcss_strip_window_text(jstr(d, "title"));
        pw.has_rect = true;
        pw.rect = ProbeRect{ geo.frame.x, geo.frame.y, geo.frame.w, geo.frame.h };
        probe.window(std::move(pw));
    }
}

} // namespace hd2d::dcss

// ================================================================== 名前の問い

namespace hd2d::dcss {

void DcssWindows::draw_name_prompt(UiPaint &paint, int screen_w, int screen_h)
{
    const DcssTextMeasure &M = this->measure_;
    const bool ja = this->japanese_;
    const std::string &typed = this->stack_.text_input().typed;
    const auto escape = [](const std::string &t) {
        std::string out;
        for (const char ch : t) {
            out += ch;
            if (ch == '<') {
                out += '<'; // 書式の `<<`
            }
        }
        return out;
    };

    // 原作 `_choose_name`: 題「あなたは%sだ。」・問い「あなたの名前は？ 」＋打った字・下に 3 つのボタン（Esc - 終了 / * - ランダムな名前 / Enter - 開始！）。
    DcssStyledLine title;
    const std::string &job = this->stack_.name_prompt_job();
    if (!job.empty()) {
        title = dcss_parse_window_text(ja ? ("あなたは" + escape(job) + "だ。") : ("You are a " + escape(job) + "."));
    }
    DcssStyledLine prompt = dcss_parse_window_text(ja ? "<cyan>あなたの名前は？ </cyan>" : "<cyan>What is your name today? </cyan>");
    if (this->stack_.name_prompt_random()) {
        const DcssStyledLine rnd = dcss_parse_window_text(ja ? "<darkgrey>(ランダムな名前)</darkgrey>" : "<darkgrey>(random name)</darkgrey>");
        prompt.insert(prompt.end(), rnd.begin(), rnd.end());
    } else {
        const DcssStyledLine in = dcss_parse_window_text(escape(typed) + "_");
        prompt.insert(prompt.end(), in.begin(), in.end());
    }
    struct Btn {
        DcssStyledLine text;
        int key;
    };
    std::vector<Btn> btns;
    DcssStyledLine overwrite;
    if (this->stack_.name_overwrite()) {
        // 同じ名前のセーブが在る。原作は「本当に上書きしますか？ [Y/n]」を自分の窓に出して Y を待つ（ウェブへは来ない）。画面が同じ文とボタンを出す。
        overwrite = dcss_parse_window_text(ja ? "<lightred>この名前のゲームがすでにあります。本当に上書きしますか？ [Y/n]</lightred>"
                                              : "<lightred>You have an existing game under this name; really overwrite? [Y/n]</lightred>");
        btns.push_back({ dcss_parse_window_text(ja ? "<brown>Y - はい（上書きする）</brown>" : "<brown>Y - Yes (overwrite)</brown>"), 'Y' });
        btns.push_back({ dcss_parse_window_text(ja ? "<brown>N - いいえ（名前を直す）</brown>" : "<brown>N - No (change the name)</brown>"), 'n' });
    } else {
        btns.push_back({ dcss_parse_window_text(ja ? "<brown>Esc - 終了</brown>" : "<brown>Esc - Quit</brown>"), 27 });
        btns.push_back({ dcss_parse_window_text(ja ? "<brown>* - ランダムな名前</brown>" : "<brown>* - Random name</brown>"), '*' });
        btns.push_back({ dcss_parse_window_text(ja ? "<brown>Enter - 開始！</brown>" : "<brown>Enter - Begin!</brown>"), 13 });
    }

    constexpr int kBtnPadX = 10;
    constexpr int kBtnGap = 12;
    const int btn_h = M.line_h + 10;
    int btns_w = 0;
    for (const Btn &b : btns) {
        btns_w += M.cells_width(b.text) + (2 * kBtnPadX);
    }
    btns_w += kBtnGap * (static_cast<int>(btns.size()) - 1);
    const int content_w = std::max({ M.cells_width(title), M.cells_width(prompt), M.cells_width(overwrite), btns_w, 30 * M.half_w });
    const int title_h = title.empty() ? 0 : (M.line_h + 10);
    const int over_h = overwrite.empty() ? 0 : M.line_h;
    const int total_h = title_h + M.line_h + over_h + 15 + btn_h;
    const DcssPopupGeometry geo = dcss_popup_place(this->area_, content_w, total_h, true, 0, this->small_layout_);
    const RectPx c = geo.content;

    this->begin_pass(paint, screen_w, screen_h);
    paint.rect(this->area_, dcss_paint_rgb(0, 0, 0, 255)); // 作成の窓の背景（黒）を引き継ぐ
    this->draw_scrim_and_frame(paint, geo);
    int y = c.y;
    const int title_y = y;
    y += title_h;
    const int prompt_y = y;
    y += M.line_h;
    const int over_y = y;
    y += over_h + 15;
    this->name_hits_.clear();
    int x = c.x;
    const PaintColor edge = dcss_paint_rgb(125, 98, 60);
    for (const Btn &b : btns) {
        const int w = M.cells_width(b.text) + (2 * kBtnPadX);
        const RectPx r{ x, y, w, btn_h };
        paint.rect(RectPx{ r.x, r.y, r.w, 1 }, edge);
        paint.rect(RectPx{ r.x, r.y + r.h - 1, r.w, 1 }, edge);
        paint.rect(RectPx{ r.x, r.y, 1, r.h }, edge);
        paint.rect(RectPx{ r.x + r.w - 1, r.y, 1, r.h }, edge);
        this->name_hits_.push_back(NameHit{ r, b.key });
        x += w + kBtnGap;
    }
    paint.flush();
    if (!title.empty()) {
        this->draw_styled(c.x, title_y, title);
    }
    this->draw_styled(c.x, prompt_y, prompt);
    if (!overwrite.empty()) {
        this->draw_styled(c.x, over_y, overwrite);
    }
    for (std::size_t i = 0; i < btns.size(); ++i) {
        const RectPx &r = this->name_hits_[i].rect;
        this->draw_styled(r.x + kBtnPadX, r.y + ((r.h - M.line_h) / 2), btns[i].text);
    }
    this->end_pass(paint);
}

bool DcssWindows::on_name_mouse(const SDL_Event &event)
{
    if (event.type == SDL_MOUSEWHEEL) {
        return true;
    }
    if ((event.type == SDL_MOUSEBUTTONDOWN) && (event.button.button == SDL_BUTTON_LEFT)) {
        for (const NameHit &h : this->name_hits_) {
            if (h.rect.contains(event.button.x, event.button.y)) {
                this->send_key(h.key);
                return true;
            }
        }
    }
    return true; // 問いが出ている間は、地図などへ流さない
}

} // namespace hd2d::dcss
