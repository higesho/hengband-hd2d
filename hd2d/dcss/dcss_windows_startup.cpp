/*!
 * @file dcss_windows_startup.cpp
 * @brief 開始メニュー（名前・ゲームモード・セーブデータ）を、原作の手元のタイル版（`startup.cc` の `UIStartupMenu`、`USE_TILE_LOCAL` の作り）と同じ形に描く。
 *
 * ウェブタイル版はこの画面を 80×24 の文字画面（`txt`）で送るだけなので、絵も項目の位置も無い。そこでアダプタが、原作が項目を作るときに使う材料
 * （モードの一覧と絵のタイル番号・セーブデータの一覧と人形の層。`dcss/adapter/dcss_ui_startup.h`）を送り、ここで原作と同じ並びに描く。
 * 選択中の項目・名前・操作の案内は、文字画面から読む（選択中の項目は黒い字で送られてくる）。操作（上下キー・マウスを載せる・クリック・Tab・名前の入力）は
 * コアが原作どおりに処理するので、画面はキーを送るだけ（`dcss_windows_input.cpp`）。
 *
 * ## 並び（原作の手元のタイル版の絵 `fix2/orig_start_*.png` と 1 画素の単位で見比べて決めた）
 * 窓の中身は縦に
 *   1. 開始の案内（`opening_screen()`。文字画面の先頭の行。下の余白 10）
 *   2. 名前の行（見出し 1 列目 + 名前。高さ 字 + 10）
 *   3. ゲームモードの行（見出し + 項目の縦の並び。項目 1 つの高さ 36。下の余白 10）
 *   4. セーブデータの行（あれば。見出し + 項目の縦の並びを捲れる欄に入れる。最後に「新しくゲームを始める」。欄の下の余白 10）
 *   5. 操作の案内（`[tab]`・`[ctrl-p]`）
 *   6. 説明（選択中のモードの説明。上の余白 10）
 * 1 列目の幅は見出しの最大の幅、2 列目は左に 10 の余白 + 項目の幅（絵 32 + 余白 6 + 字 + 余白）+ 捲り棒の 12。窓の幅は案内・格子・説明のうち最も広い物。
 * セーブデータの欄は窓の高さが足りなければ縮み（最小 項目 3 つぶん）、中で捲れる。
 */
#include "dcss/dcss_windows.h"

#include "app/ui_probe.h"
#include "dcss/dcss_windows_internal.h"
#include "render/gl_core.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <random>

namespace hd2d::dcss {

namespace {

constexpr int kButtonH = 36; //!< 項目 1 つの高さ（絵 32 + 上下の余白 2）
constexpr int kIcon = 32;
constexpr int kBoxPad = 2; //!< 項目の左の余白
constexpr int kLabelIndent = kBoxPad + kIcon + 6; //!< 項目の字の位置（絵の幅 + 余白 6）
constexpr int kButtonRightPad = 10;
constexpr int kScrollBar = 12;

std::string trimmed(const std::string &s)
{
    std::size_t b = 0;
    std::size_t e = s.size();
    while ((b < e) && ((s[b] == ' ') || (s[b] == '\t'))) {
        ++b;
    }
    while ((e > b) && ((s[e - 1] == ' ') || (s[e - 1] == '\t'))) {
        --e;
    }
    return s.substr(b, e - b);
}

bool starts_with(const std::string &s, const std::string &prefix)
{
    return !prefix.empty() && (s.compare(0, prefix.size(), prefix) == 0);
}

//! マスの並びの [from, to) を字にする（全角の後ろの詰め物は飛ばす）。
std::string cells_text(const std::vector<CrtCell> &cells, std::size_t from, std::size_t to)
{
    std::string out;
    for (std::size_t i = from; (i < to) && (i < cells.size()); ++i) {
        if (cells[i].ch != kWideFiller) {
            out += dcss_encode_utf8(cells[i].ch);
        }
    }
    return out;
}

//! 行を色つきの字の並びにする（詰め物は飛ばし、行末の空白を落とす）。
DcssStyledLine styled_line(const std::vector<CrtCell> &cells)
{
    DcssStyledLine line;
    for (const CrtCell &c : cells) {
        if (c.ch == kWideFiller) {
            continue;
        }
        line.push_back(DcssStyledChar{ c.ch, c.fg });
    }
    while (!line.empty() && (line.back().ch == U' ')) {
        line.pop_back();
    }
    return line;
}

int first_text_col(const std::vector<CrtCell> &cells)
{
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if ((cells[i].ch != U' ') && (cells[i].ch != kWideFiller)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

bool DcssWindows::draw_startup(DcssWindow &w, int depth, UiPaint &paint, const UiImagePainter &images)
{
    const DcssStartupData &S = this->startup_;
    if (!S.valid || S.p_name.empty()) {
        return false;
    }
    View &v = *this->views_[w.generation];
    const DcssTextMeasure &M = this->measure_;

    // ---- 文字画面から読む（名前・選択中の項目・案内）----
    std::vector<std::vector<CrtCell>> rows;
    for (const std::string &l : this->stack_.crt_lines()) {
        rows.push_back(parse_crt_line(l));
    }
    int name_row = -1;
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const std::string text = trimmed(cells_text(rows[static_cast<std::size_t>(r)], 0, rows[static_cast<std::size_t>(r)].size()));
        if (starts_with(text, S.p_name)) {
            name_row = r;
            break;
        }
    }
    if (name_row < 0) {
        return false; // 開始メニューではない文字画面（ヘルプ・ハイスコアなど）
    }
    std::vector<DcssStyledLine> header;
    for (int r = 0; r < name_row; ++r) {
        const DcssStyledLine l = styled_line(rows[static_cast<std::size_t>(r)]);
        if (!l.empty()) {
            header.push_back(l);
        }
    }
    std::string name_value;
    {
        const auto &cells = rows[static_cast<std::size_t>(name_row)];
        const std::string text = trimmed(cells_text(cells, 0, cells.size()));
        name_value = trimmed(text.substr(S.p_name.size()));
    }
    std::vector<DcssStyledLine> instructions;
    std::string focus_text;
    for (int r = name_row + 1; r < static_cast<int>(rows.size()); ++r) {
        const auto &cells = rows[static_cast<std::size_t>(r)];
        const int f = first_text_col(cells);
        if (f < 0) {
            continue;
        }
        if ((f == 0) && (cells[0].ch == U'[')) {
            instructions.push_back(styled_line(cells));
            continue;
        }
        if (!focus_text.empty()) {
            continue;
        }
        std::size_t b = cells.size();
        std::size_t e = 0;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if ((cells[i].fg == 0) && (cells[i].ch != U' ') && (cells[i].ch != kWideFiller)) {
                b = std::min(b, i);
                e = i + 1;
            }
        }
        if (e > b) {
            focus_text = trimmed(cells_text(cells, b, e));
        }
    }

    const int n_modes = static_cast<int>(S.modes.size());
    const int n_saves = static_cast<int>(S.saves.size());
    const int n_items = n_modes + ((n_saves > 0) ? (n_saves + 1) : 0);

    // 選択中の項目の番号（モード → セーブデータ → 「新しくゲームを始める」）。文字画面は欄の一部しか載せないので、字で突き合わせる。
    int focus = -1;
    if (!focus_text.empty()) {
        // 長い字は行の幅で「..」や「…」に切られて届く（`set_ellipsize`）。切られた後ろを落として、前の部分で突き合わせる。
        std::string stripped = focus_text;
        while (!stripped.empty()) {
            if (stripped.back() == '.') {
                stripped.pop_back();
            } else if ((stripped.size() >= 3) && (stripped.compare(stripped.size() - 3, 3, "\xE2\x80\xA6") == 0)) {
                stripped.erase(stripped.size() - 3);
            } else {
                break;
            }
        }
        for (int i = 0; (i < n_modes) && (focus < 0); ++i) {
            if (S.modes[static_cast<std::size_t>(i)].label == focus_text) {
                focus = i;
            }
        }
        for (int i = 0; (i < n_saves) && (focus < 0); ++i) {
            if (S.saves[static_cast<std::size_t>(i)].label == focus_text) {
                focus = n_modes + i;
            }
        }
        if ((focus < 0) && (n_saves > 0) && (S.p_new_game == focus_text)) {
            focus = n_modes + n_saves;
        }
        for (int i = 0; (i < n_saves) && (focus < 0) && (stripped.size() >= 3); ++i) {
            if (starts_with(S.saves[static_cast<std::size_t>(i)].label, stripped)) {
                focus = n_modes + i;
            }
        }
    }
    // マウスの下へ選択を移すキーを送って、コアの返事（選択中の項目が変わった文字画面）が来るまでは、送った先を選択中として描く。
    if ((w.crt_pending_focus >= 0) && ((focus == w.crt_pending_focus) || ((SDL_GetTicks() - w.crt_pending_at) >= 400))) {
        w.crt_pending_focus = -1;
    }
    const int box_focus = (w.crt_pending_focus >= 0) ? w.crt_pending_focus : focus;

    // ---- 大きさ ----
    const auto label_w = [&](const std::string &s) { return M.width(s); };
    const int wiz_w = M.width(" (WIZ)");
    int btn_w = 0;
    for (const DcssStartupMode &m : S.modes) {
        btn_w = std::max(btn_w, label_w(m.label) + kLabelIndent + kButtonRightPad);
    }
    for (const DcssStartupSave &s : S.saves) {
        btn_w = std::max(btn_w, label_w(s.label) + (s.wiz ? wiz_w : 0) + kLabelIndent + kButtonRightPad);
    }
    if (n_saves > 0) {
        btn_w = std::max(btn_w, label_w(S.p_new_game) + kLabelIndent + kButtonRightPad);
    }
    const int col0_w = std::max({ label_w(S.p_name), label_w(S.p_modes), (n_saves > 0) ? label_w(S.p_saves) : 0 });
    const int col1_w = 10 + std::max(label_w(name_value), btn_w + kScrollBar);
    int header_w = 0;
    for (const DcssStyledLine &l : header) {
        header_w = std::max(header_w, M.cells_width(l));
    }
    int instr_w = 0;
    for (const DcssStyledLine &l : instructions) {
        instr_w = std::max(instr_w, M.cells_width(l));
    }
    int desc_w = 0;
    for (const DcssStartupMode &m : S.modes) {
        desc_w = std::max(desc_w, label_w(m.desc));
    }
    const int nat_w = std::max({ header_w, col0_w + col1_w, instr_w, desc_w });

    const int header_h = (static_cast<int>(header.size()) * M.line_h) + 10;
    const int name_h = M.line_h + 10;
    const int modes_h = (n_modes * kButtonH) + 10;
    const int saves_nat = (n_saves > 0) ? ((n_saves + 1) * kButtonH) : 0;
    const int instr_h = static_cast<int>(instructions.size()) * M.line_h;
    const int desc_h = M.line_h + 10;
    const int fixed_h = header_h + name_h + modes_h + ((n_saves > 0) ? 10 : 0) + instr_h + desc_h;
    const int nat_h = fixed_h + saves_nat;

    const DcssPopupGeometry geo = dcss_popup_place(this->area_, nat_w, nat_h, true, depth, this->small_layout_, fixed_h + std::min(saves_nat, kButtonH * 3));
    v.geo = geo;
    v.need_h = fixed_h + std::min(saves_nat, kButtonH * 3) + (2 * this->fit_pad()); // 保存の一覧は捲れるので、3 行ぶんまで縮めてよい
    const RectPx content = geo.content;
    int saves_h = 0;
    if (n_saves > 0) {
        saves_h = std::clamp(content.h - fixed_h, std::min(saves_nat, kButtonH * 3), saves_nat);
    }

    // ---- 位置 ----
    const int x0 = content.x;
    const int bx = content.x + col0_w + 10; // 項目の左
    const int y_name = content.y + header_h;
    const int y_modes = y_name + name_h;
    const int y_saves = y_modes + modes_h;
    const int y_instr = y_saves + ((n_saves > 0) ? (saves_h + 10) : 0);
    const int y_desc = y_instr + instr_h + 10;
    const RectPx region{ bx, y_saves, btn_w, saves_h }; // 捲り棒はこの右の 10〜12 画素（`draw_scroller_bar`）

    // セーブデータの欄の捲り: 選択中の項目が変わったら、見える位置へ寄せる。
    const int max_scroll = std::max(0, saves_nat - saves_h);
    if ((w.crt_last_focus != box_focus) && (box_focus >= n_modes) && (saves_h > 0)) {
        const int top = (box_focus - n_modes) * kButtonH;
        if (top < w.scroll_y) {
            w.scroll_y = top;
        } else if ((top + kButtonH) > (w.scroll_y + saves_h)) {
            w.scroll_y = top + kButtonH - saves_h;
        }
    }
    w.crt_last_focus = box_focus;
    w.scroll_y = std::clamp(w.scroll_y, 0, max_scroll);
    v.body_viewport = region;
    v.body_total = saves_nat;
    v.line_h = M.line_h;
    v.hits.clear();

    this->draw_scrim_and_frame(paint, geo);

    // 項目の位置（モード）と、押せる所の記録
    struct Item {
        RectPx rect; //!< 画面の位置（捲り込み）
        bool is_save{ false };
    };
    std::vector<Item> items(static_cast<std::size_t>(n_items));
    for (int i = 0; i < n_modes; ++i) {
        items[static_cast<std::size_t>(i)] = Item{ RectPx{ bx, y_modes + (i * kButtonH), btn_w, kButtonH }, false };
    }
    for (int i = n_modes; i < n_items; ++i) {
        items[static_cast<std::size_t>(i)] = Item{ RectPx{ bx, y_saves + ((i - n_modes) * kButtonH) - w.scroll_y, btn_w, kButtonH }, true };
    }

    const auto focus_box = [&](const RectPx &box) {
        paint.rect(box, dcss_paint_rgb(255, 255, 255, 25));
        const DcssRgb &grey = dcss_term_rgb(kLightGrey);
        const PaintColor border = dcss_paint_rgb(grey.r, grey.g, grey.b);
        paint.rect(RectPx{ box.x, box.y, box.w, 1 }, border);
        paint.rect(RectPx{ box.x, box.y + box.h - 1, box.w, 1 }, border);
        paint.rect(RectPx{ box.x, box.y, 1, box.h }, border);
        paint.rect(RectPx{ box.x + box.w - 1, box.y, 1, box.h }, border);
    };
    const auto item_label = [&](int i) {
        if (i < n_modes) {
            return S.modes[static_cast<std::size_t>(i)].label;
        }
        if (i < n_modes + n_saves) {
            return S.saves[static_cast<std::size_t>(i - n_modes)].label;
        }
        return S.p_new_game;
    };
    const auto draw_item_text = [&](int i) {
        const RectPx &r = items[static_cast<std::size_t>(i)].rect;
        const int ty = r.y + kBoxPad + ((kIcon - M.line_h) / 2);
        int colour = kWhite;
        if ((i >= n_modes) && (i < n_modes + n_saves)) {
            colour = S.saves[static_cast<std::size_t>(i - n_modes)].loadable ? kWhite : kRed;
        }
        DcssStyledLine l;
        for (const DcssStyledChar &c : dcss_parse_window_text(item_label(i), colour, colour)) {
            l.push_back(c);
        }
        const int tx = r.x + kLabelIndent;
        this->draw_styled(tx, ty, l);
        if ((i >= n_modes) && (i < n_modes + n_saves) && S.saves[static_cast<std::size_t>(i - n_modes)].wiz) {
            const DcssStartupSave &s = S.saves[static_cast<std::size_t>(i - n_modes)];
            const int wc = s.loadable ? kLightMagenta : kRed;
            this->draw_styled(tx + M.cells_width(l), ty, dcss_parse_window_text(" (WIZ)", wc, wc));
        }
    };

    // ---- 上の部分（案内・名前・ゲームモード）----
    if ((box_focus >= 0) && (box_focus < n_modes)) {
        focus_box(items[static_cast<std::size_t>(box_focus)].rect);
    }
    paint.flush();
    if (this->atlas_.ready()) {
        for (int i = 0; i < n_modes; ++i) {
            const int tile = S.modes[static_cast<std::size_t>(i)].tile;
            const RectPx &r = items[static_cast<std::size_t>(i)].rect;
            this->atlas_.draw(images, tile, r.x + kBoxPad, r.y + kBoxPad, 1, 0);
        }
    }
    {
        int y = content.y;
        for (const DcssStyledLine &l : header) {
            this->draw_styled(x0, y, l);
            y += M.line_h;
        }
        this->draw_styled(x0, y_name, dcss_parse_window_text(S.p_name, kLightGrey, kLightGrey));
        this->draw_styled(x0 + col0_w + 10, y_name, dcss_parse_window_text(name_value, kWhite, kWhite));
        this->draw_styled(x0, y_modes, dcss_parse_window_text(S.p_modes, kLightGrey, kLightGrey));
        if (n_saves > 0) {
            this->draw_styled(x0, y_saves, dcss_parse_window_text(S.p_saves, kLightGrey, kLightGrey));
        }
        for (int i = 0; i < n_modes; ++i) {
            draw_item_text(i);
        }
        y = y_instr;
        for (const DcssStyledLine &l : instructions) {
            this->draw_styled(x0, y, l);
            y += M.line_h;
        }
        // 説明（選択中のモードの説明。セーブデータと「新しくゲームを始める」には無い）
        if ((box_focus >= 0) && (box_focus < n_modes)) {
            this->draw_styled(x0, y_desc, dcss_parse_window_text(S.modes[static_cast<std::size_t>(box_focus)].desc, kWhite, kWhite));
        }
    }
    this->font_.flush();

    // ---- セーブデータの欄（欄の中だけ。GL の切り抜きで切る）----
    if ((n_saves > 0) && !region.empty()) {
        using ScissorFn = void (*)(int, int, int, int);
        static ScissorFn scissor = reinterpret_cast<ScissorFn>(SDL_GL_GetProcAddress("glScissor"));
        if (scissor != nullptr) {
            gl::glEnable(0x0C11);
            scissor(scaled(region.x), this->screen_h_ - scaled(region.y + region.h), scaled(region.w), scaled(region.h));
        }
        const auto visible = [&](int i) {
            const RectPx &r = items[static_cast<std::size_t>(i)].rect;
            return ((r.y + r.h) > region.y) && (r.y < (region.y + region.h));
        };
        if ((box_focus >= n_modes) && visible(box_focus)) {
            focus_box(items[static_cast<std::size_t>(box_focus)].rect);
        }
        paint.flush();
        if (this->atlas_.ready()) {
            for (int i = 0; i < n_saves; ++i) {
                if (!visible(n_modes + i)) {
                    continue;
                }
                const RectPx &r = items[static_cast<std::size_t>(n_modes + i)].rect;
                for (const auto &part : S.saves[static_cast<std::size_t>(i)].doll) {
                    this->atlas_.draw(images, part.first, r.x + kBoxPad, r.y + kBoxPad, 1, part.second);
                }
            }
        }
        for (int i = n_modes; i < n_items; ++i) {
            if (visible(i)) {
                draw_item_text(i);
            }
        }
        this->font_.flush();
        if (scissor != nullptr) {
            gl::glDisable(0x0C11);
        }
        this->draw_scroller_bar(paint, region, saves_nat, w.scroll_y);
        paint.flush();
    }

    // ---- 押せる所の記録（当たり判定と検査の窓口）----
    UiProbe &probe = ui_probe();
    for (int i = 0; i < n_items; ++i) {
        RectPx r = items[static_cast<std::size_t>(i)].rect;
        if (items[static_cast<std::size_t>(i)].is_save) {
            const int y0 = std::max(r.y, region.y);
            const int y1 = std::min(r.y + r.h, region.y + region.h);
            r = RectPx{ r.x, y0, r.w, std::max(0, y1 - y0) };
        }
        v.hits.push_back(View::Hit{ r, 2, i, (i == focus) ? 1 : 0 });
        if (probe.enabled() && !r.empty()) {
            probe.button(item_label(i), ProbeRect{ r.x, r.y, r.w, r.h }, true, i == box_focus, "dcss-start-item", false, "start:" + std::to_string(i));
        }
    }
    return true;
}

void DcssWindows::draw_title(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h)
{
    std::string err;
    if (!this->ensure_font(err)) {
        return;
    }
    if (!this->title_loaded_) {
        this->title_loaded_ = true;
        std::vector<std::string> files;
        try {
            for (const auto &e : std::filesystem::directory_iterator("assets/ui/dcss/title")) {
                if (e.is_regular_file() && (e.path().extension() == ".png")) {
                    files.push_back(e.path().generic_string());
                }
            }
        } catch (const std::filesystem::filesystem_error &) {
            files.clear();
        }
        std::sort(files.begin(), files.end());
        std::string e;
        if (files.empty()) {
            this->title_done_ = true;
            return;
        }
        std::random_device rd;
        const std::string &pick = files[std::uniform_int_distribution<std::size_t>(0, files.size() - 1)(rd)];
        if (!this->title_image_.load_minified(pick, e)) {
            std::fprintf(stderr, "[hd2d] DCSS の起動の絵を読めません: %s\n", e.c_str());
            this->title_done_ = true;
            return;
        }
    }
    if (!this->title_image_.valid()) {
        this->title_done_ = true;
        return;
    }
    this->screen_w_ = screen_w;
    this->screen_h_ = screen_h;
    const DcssTextMeasure &M = this->measure_;
    // 原作は、絵（`UIShrinkableImage`）と下の字（上の余白 15）を縦に並べて中央に置く。字の最小の幅は「読み込み完了」の案内の幅。
    const int msg_w = M.width(this->japanese_ ? "読み込みが完了しました。開始するには任意のキーを押してください。" : "Loading complete, press any key to start.");
    const int iw = this->title_image_.width();
    const int ih = this->title_image_.height();
    const int text_h = 15 + M.line_h;
    const DcssPopupGeometry probe_geo = dcss_popup_place(this->area_, std::max(iw, msg_w), ih + text_h, true, 0, this->small_layout_);
    const float scale = std::min({ 1.f, static_cast<float>(probe_geo.content.w) / static_cast<float>(iw), static_cast<float>(std::max(1, probe_geo.content.h - text_h)) / static_cast<float>(ih) });
    const int dw = std::max(1, static_cast<int>(static_cast<float>(iw) * scale));
    const int dh = std::max(1, static_cast<int>(static_cast<float>(ih) * scale));
    const DcssPopupGeometry geo = dcss_popup_place(this->area_, std::max(dw, msg_w), dh + text_h, true, 0, this->small_layout_);
    this->begin_pass(paint, screen_w, screen_h);
    this->draw_scrim_and_frame(paint, geo);
    paint.flush();
    images.draw_region(this->title_image_, RectPx{ geo.content.x + ((geo.content.w - dw) / 2), geo.content.y, dw, dh }, 0.f, 0.f, 1.f, 1.f, 1.f);
}

} // namespace hd2d::dcss
