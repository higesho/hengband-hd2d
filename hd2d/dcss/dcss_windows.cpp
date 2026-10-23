/*!
 * @file dcss_windows.cpp
 * @brief `dcss_windows.h` の実装（描画の本体・メニュー・文字画面・捲れる文書・説明の窓）。入力は `dcss_windows_input.cpp`。
 */
#include "dcss/dcss_windows.h"

#include "app/ui_probe.h"
#include "dcss/dcss_windows_internal.h"
#include "render/gl_core.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace hd2d::dcss {

namespace {

//! 原作の字（`settings/init.txt` の `tile_font_crt_file`）。Windows は OS の BIZ UDゴシック、無ければ同梱の OFL 版。
const char *const kFontCandidates[] = {
    "C:/Windows/Fonts/BIZ-UDGothicR.ttc",
    "assets/fonts/dcss/BIZUDGothic-Regular.ttf",
};

//! 字の高さの基準線の合わせ（`dcss_ui.cpp` の `kGlyphYShift` と同じ）。
constexpr int kGlyphYShift = -1;

constexpr const char *kAtlasDir = "assets/ui/dcss/atlas/";

} // namespace

// ================================================================== 画像ページ

DcssWindowAtlas::~DcssWindowAtlas()
{
    this->shutdown();
}

int DcssWindowAtlas::page_index(char page)
{
    switch (page) {
    case 'f':
        return 0;
    case 'w':
        return 1;
    case 'e':
        return 2;
    case 'm':
        return 3;
    case 'p':
        return 4;
    case 'g':
        return 5;
    case 'i':
        return 6;
    default:
        return -1;
    }
}

bool DcssWindowAtlas::load(std::string &err)
{
    if (this->ready_) {
        return true;
    }
    if (this->tried_) {
        return false;
    }
    this->tried_ = true;
    std::ifstream in(std::string(kAtlasDir) + "tiles.tsv", std::ios::binary);
    if (!in) {
        err = std::string("DCSS のタイルの表を開けません: ") + kAtlasDir + "tiles.tsv";
        return false;
    }
    std::string line;
    int max_id = 0;
    struct Row {
        int id;
        Info info;
    };
    std::vector<Row> rows;
    while (std::getline(in, line)) {
        if (line.empty() || (line[0] == '#') || (line.compare(0, 5, "const") == 0)) {
            continue;
        }
        std::istringstream ss(line);
        Row r{};
        std::string page;
        if (!(ss >> r.id >> page >> r.info.w >> r.info.h >> r.info.ox >> r.info.oy >> r.info.sx >> r.info.sy >> r.info.ex >> r.info.ey) || page.empty()) {
            continue;
        }
        r.info.page = page[0];
        max_id = std::max(max_id, r.id);
        rows.push_back(r);
    }
    this->info_.assign(static_cast<std::size_t>(max_id) + 1, Info{});
    for (const Row &r : rows) {
        this->info_[static_cast<std::size_t>(r.id)] = r.info;
    }
    static const char *const kFiles[] = { "floor.png", "wall.png", "feat.png", "main.png", "player.png", "gui.png", "icons.png" };
    this->pages_.clear();
    this->pages_.resize(7);
    for (std::size_t i = 0; i < 7; ++i) {
        std::string e;
        if (!this->pages_[i].load(std::string(kAtlasDir) + kFiles[i], e)) {
            err = "DCSS のタイルの画像を読めません: " + e;
            return false;
        }
        // 原作は拡大してもタイルを最近傍で描く（`tile_filter_scaling` が偽）。
        gl::glBindTexture(gl::GL_TEXTURE_2D, this->pages_[i].texture());
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, static_cast<gl::GLint>(0x2600)); // GL_NEAREST
        gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, static_cast<gl::GLint>(0x2600));
        gl::glBindTexture(gl::GL_TEXTURE_2D, 0);
    }
    this->ready_ = true;
    return true;
}

bool DcssWindowAtlas::has(int tile) const
{
    return this->ready_ && (tile >= 0) && (tile < static_cast<int>(this->info_.size())) && (this->info_[static_cast<std::size_t>(tile)].page != 0);
}

bool DcssWindowAtlas::size(int tile, int &w, int &h) const
{
    if (!this->has(tile)) {
        return false;
    }
    const Info &i = this->info_[static_cast<std::size_t>(tile)];
    w = i.w;
    h = i.h;
    return true;
}

void DcssWindowAtlas::draw(const UiImagePainter &images, int tile, int x, int y, int scale, int ymax) const
{
    if (!this->has(tile)) {
        return;
    }
    const Info &inf = this->info_[static_cast<std::size_t>(tile)];
    const int page = page_index(inf.page);
    if ((page < 0) || (page >= static_cast<int>(this->pages_.size())) || !this->pages_[static_cast<std::size_t>(page)].valid()) {
        return;
    }
    const UiImage &img = this->pages_[static_cast<std::size_t>(page)];
    // 原作 `TilesTexture::get_coords`（ymin なし・centre なし）。
    const int ymax_eff = (ymax > 0) ? ymax : 32;
    const int sy_adjust = inf.oy;
    const int ey_adjust = sy_adjust + (inf.ey - inf.sy);
    const int sy = sy_adjust;
    const int ey = std::min(ymax_eff, ey_adjust);
    if (sy >= ey) {
        return;
    }
    const int sw = inf.ex - inf.sx;
    const int src_y0 = inf.sy + (sy - sy_adjust);
    const int src_y1 = inf.ey + (ey - ey_adjust);
    const int sh = src_y1 - src_y0;
    if ((sw <= 0) || (sh <= 0)) {
        return;
    }
    const RectPx dst{ x + (inf.ox * scale), y + (sy * scale), sw * scale, sh * scale };
    if (scale == 1) {
        images.draw_crop(img, dst, inf.sx, src_y0, sw, sh, 1.f);
    } else {
        const float iw = static_cast<float>(img.width());
        const float ih = static_cast<float>(img.height());
        images.draw_region(img, dst, static_cast<float>(inf.sx) / iw, static_cast<float>(src_y0) / ih, static_cast<float>(inf.ex) / iw,
            static_cast<float>(src_y1) / ih, 1.f);
    }
}

void DcssWindowAtlas::shutdown()
{
    for (UiImage &img : this->pages_) {
        img.unload();
    }
    this->pages_.clear();
    this->info_.clear();
    this->ready_ = false;
    this->tried_ = false;
}

// ================================================================== 窓の部品

DcssWindows::DcssWindows() = default;

DcssWindows::~DcssWindows()
{
    this->shutdown();
}

bool DcssWindows::ingest_line(const std::string &line)
{
    return this->stack_.ingest_line(line);
}

std::vector<std::string> DcssWindows::take_outgoing()
{
    std::vector<std::string> out;
    out.swap(this->outgoing_);
    return out;
}

void DcssWindows::shutdown()
{
    if (this->font_open_) {
        this->font_.shutdown();
        this->font_open_ = false;
    }
    this->atlas_.shutdown();
    this->atlas_tried_ = false;
    this->views_.clear();
}

bool DcssWindows::ensure_font(std::string &err)
{
    if (this->font_open_ && (this->font_px_ == this->crt_px_)) {
        return true;
    }
    if (this->font_failed_ && (this->font_px_ == this->crt_px_)) {
        return false;
    }
    if (this->font_open_) {
        this->font_.shutdown();
        this->font_open_ = false;
    }
    std::vector<std::string> candidates;
    if (const char *forced = std::getenv("HD2D_DCSS_FONT"); (forced != nullptr) && (*forced != '\0')) {
        candidates.emplace_back(forced);
    } else {
        for (const char *path : kFontCandidates) {
            candidates.emplace_back(path);
        }
    }
    std::string last_err;
    for (const std::string &path : candidates) {
        if (!this->font_path_.empty() && (this->font_path_ != path)) {
            continue;
        }
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            continue;
        }
        std::string e;
        if (this->font_.init_with_font(this->crt_px_, path, 0, e)) {
            this->font_open_ = true;
            this->font_px_ = this->crt_px_;
            this->font_failed_ = false;
            this->font_path_ = path;
            this->measure_.half_w = std::max(1, this->font_.cell_w());
            this->font_.set_wide_advance(this->measure_.half_w * 2); // 原作は全角を半角のちょうど 2 倍で並べる
            this->measure_.line_h = this->crt_px_;
            this->measure_.char_w = this->crt_px_;
            std::fprintf(stderr, "[hd2d] dcss windows font: %s px=%d half=%d measure(A)=%d measure(あ)=%d\n", path.c_str(), this->crt_px_, this->measure_.half_w,
                this->font_.measure("A"), this->font_.measure("あ"));
            return true;
        }
        last_err = e;
        this->font_.shutdown();
    }
    this->font_failed_ = true;
    this->font_px_ = this->crt_px_;
    err = "DCSS の窓の字（BIZ UDゴシック）を開けませんでした。" + last_err;
    return false;
}

bool DcssWindows::configure(const RectPx &area, int crt_px, bool small_layout, std::string &err)
{
    this->area_ = area;
    this->crt_px_ = std::max(8, crt_px);
    this->small_layout_ = small_layout;
    if (this->stack_.empty()) {
        return true; // 窓が無い間は字も絵も開かない
    }
    return this->ensure_font(err);
}

// ---- 描画の道具 ----

namespace {

TextColor text_rgb(int colour)
{
    const DcssRgb &c = dcss_term_rgb(colour);
    return TextColor{ static_cast<float>(c.r) / 255.f, static_cast<float>(c.g) / 255.f, static_cast<float>(c.b) / 255.f, 1.f };
}

PaintColor paint_rgb(int r, int g, int b, int a = 255)
{
    return PaintColor{ static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f, static_cast<float>(b) / 255.f, static_cast<float>(a) / 255.f };
}

using ScissorFn = void (*)(int, int, int, int);

ScissorFn scissor_fn()
{
    static ScissorFn fn = reinterpret_cast<ScissorFn>(SDL_GL_GetProcAddress("glScissor"));
    return fn;
}

constexpr unsigned kGlScissorTest = 0x0C11;

} // namespace

int DcssWindows::draw_styled(int x, int y, const DcssStyledLine &line, int max_w)
{
    const int half = this->measure_.half_w;
    int cx = x;
    std::string run;
    int run_x = x;
    int run_colour = -1;
    int pending_spaces = 0;
    const auto flush_run = [&]() {
        if (!run.empty()) {
            this->font_.draw(run_x, y + kGlyphYShift, run, text_rgb(run_colour));
            run.clear();
        }
        pending_spaces = 0;
    };
    for (const DcssStyledChar &c : line) {
        const int cw = dcss_cell_width(c.ch);
        if (cw == 0) {
            continue;
        }
        const int adv = cw * half;
        if ((max_w > 0) && ((cx - x) + adv > max_w)) {
            break;
        }
        if (c.ch == U' ') {
            if (!run.empty()) {
                ++pending_spaces;
            }
            cx += adv;
            continue;
        }
        if (run.empty() || (c.colour != run_colour)) {
            flush_run();
            run_x = cx;
            run_colour = c.colour;
        } else {
            run.append(static_cast<std::size_t>(pending_spaces), ' ');
            pending_spaces = 0;
        }
        run += dcss_encode_utf8(c.ch);
        cx += adv;
    }
    flush_run();
    return cx - x;
}

void DcssWindows::draw_lines(int x, int y, const std::vector<DcssStyledLine> &lines)
{
    int cy = y;
    for (const DcssStyledLine &l : lines) {
        this->draw_styled(x, cy, l);
        cy += this->measure_.line_h;
    }
}

void DcssWindows::draw_scrim_and_frame(UiPaint &paint, const DcssPopupGeometry &g)
{
    // 暗幕（窓の部品の層いっぱい）・外枠・内側の黒・内側の地（`Popup::_allocate_region`）。
    paint.rect(this->area_, paint_rgb(0, 0, 0, 150));
    const RectPx f = g.frame;
    paint.rect(f, paint_rgb(125, 98, 60));
    paint.rect(f.inset(2), paint_rgb(0, 0, 0));
    paint.rect(f.inset(3), paint_rgb(4, 2, 4));
}

void DcssWindows::draw_scroller_bar(UiPaint &paint, const RectPx &region, int total, int scroll)
{
    // 原作 `Scroller::_allocate_region`: 影（上下 12 画素のグラデーション）と、領域の右 10〜12 画素の捲り棒。
    if ((total <= region.h) || region.empty()) {
        return;
    }
    const int shade_h = 12;
    const int shade_top = std::min({ scroll / 4, shade_h, region.h / 2 });
    const int shade_bot = std::min({ (total - region.h - scroll) / 4, shade_h, region.h / 2 });
    constexpr int kSteps = 6;
    // 影は段で近似する（上: 濃い→薄い、下: 薄い→濃い）。
    if (shade_top > 0) {
        for (int i = 0; i < kSteps; ++i) {
            const int y0 = region.y + (shade_top * i / kSteps);
            const int y1 = region.y + (shade_top * (i + 1) / kSteps);
            const int a = 150 * (kSteps - 1 - i) / (kSteps);
            if (y1 > y0) {
                paint.rect(RectPx{ region.x, y0, region.w, y1 - y0 }, paint_rgb(4, 2, 4, a));
            }
        }
    }
    if (shade_bot > 0) {
        const int base = region.y + region.h - shade_bot;
        for (int i = 0; i < kSteps; ++i) {
            const int y0 = base + (shade_bot * i / kSteps);
            const int y1 = base + (shade_bot * (i + 1) / kSteps);
            const int a = 150 * (i + 1) / kSteps;
            if (y1 > y0) {
                paint.rect(RectPx{ region.x, y0, region.w, y1 - y0 }, paint_rgb(4, 2, 4, a));
            }
        }
    }
    const int x = region.x + region.w;
    const float h_percent = static_cast<float>(region.h) / static_cast<float>(total);
    const int h = static_cast<int>(static_cast<float>(region.h) * std::min(std::max(0.05f, h_percent), 1.0f));
    const float scroll_percent = static_cast<float>(scroll) / static_cast<float>(total - region.h);
    const int y = region.y + static_cast<int>(static_cast<float>(region.h - h) * scroll_percent);
    paint.rect(RectPx{ x + 10, region.y, 2, region.h }, paint_rgb(41, 41, 41));
    paint.rect(RectPx{ x + 10, y, 2, h }, paint_rgb(125, 98, 60));
}

int DcssWindows::fit_pad() const
{
    return dcss_popup_base_margin(this->area_.h, this->small_layout_) + kPopupPadding;
}

int DcssWindows::fit_need_h() const
{
    int need = 0;
    for (const auto &entry : this->views_) {
        need = std::max(need, entry.second->need_h);
    }
    return need;
}

DcssWindows::View *DcssWindows::view_of(const DcssWindow &w)
{
    const auto it = this->views_.find(w.generation);
    return (it == this->views_.end()) ? nullptr : it->second.get();
}

void DcssWindows::begin_pass(UiPaint &paint, int screen_w, int screen_h)
{
    (void)paint;
    this->font_.begin(screen_w, screen_h);
    if (this->ui_scale_ > 1.f) {
        this->font_.set_layout_scale(this->ui_scale_);
    }
}

void DcssWindows::end_pass(UiPaint &paint)
{
    paint.flush();
    this->font_.flush();
}

// ---- 全体 ----

void DcssWindows::draw(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h)
{
    if (this->stack_.empty()) {
        this->views_.clear();
        if (this->stack_.name_prompt()) {
            std::string perr;
            if (this->ensure_font(perr)) {
                this->screen_w_ = screen_w;
                this->screen_h_ = screen_h;
                this->draw_name_prompt(paint, screen_w, screen_h);
            }
        } else {
            this->name_hits_.clear();
        }
        return;
    }
    std::string err;
    if (!this->ensure_font(err)) {
        return;
    }
    if (!this->atlas_tried_) {
        this->atlas_tried_ = true;
        if (!this->atlas_.load(err)) {
            std::fprintf(stderr, "[hd2d] %s\n", err.c_str());
        }
    }
    this->screen_w_ = screen_w;
    this->screen_h_ = screen_h;
    // 閉じた窓の配置を捨てる。
    for (auto it = this->views_.begin(); it != this->views_.end();) {
        bool alive = false;
        for (const DcssWindow &w : this->stack_.windows()) {
            if (w.generation == it->first) {
                alive = true;
                break;
            }
        }
        it = alive ? std::next(it) : this->views_.erase(it);
    }
    this->title_done_ = true; // 窓が開いたので、起動の読み込み画面は終わり
    const UiProbeLayer probe_layer("dcss-win");
    int depth = 0;
    for (DcssWindow &w : this->stack_.windows()) {
        if (this->views_.find(w.generation) == this->views_.end()) {
            this->views_[w.generation] = std::make_unique<View>();
        }
        this->begin_pass(paint, screen_w, screen_h);
        switch (w.kind) {
        case DcssWindow::Kind::Menu:
            this->draw_menu(w, depth, paint, images);
            break;
        case DcssWindow::Kind::Crt:
            this->draw_crt(w, depth, paint, images);
            break;
        case DcssWindow::Kind::Ui:
            this->draw_ui(w, depth, paint, images);
            break;
        }
        ++depth;
    }
    this->seen_version_ = this->stack_.version();
}

std::string DcssWindows::describe() const
{
    std::string out = "dcss-windows";
    for (const DcssWindow &w : this->stack_.windows()) {
        out += " [";
        out += (w.kind == DcssWindow::Kind::Menu) ? ("menu:" + w.menu.tag) : (w.kind == DcssWindow::Kind::Crt) ? std::string("crt") : ("ui:" + w.type);
        out += "]";
    }
    return out;
}

// ================================================================== メニュー

void DcssWindows::draw_menu(DcssWindow &w, int depth, UiPaint &paint, const UiImagePainter &images)
{
    DcssMenuData &menu = w.menu;
    View &v = *this->views_[w.generation];
    const DcssTextMeasure &M = this->measure_;
    const int tile_indent_px = ((menu.tag == "ability") || (menu.tag == "spell")) ? (kMenuItemPad + kMenuTextIndent) : kMenuItemPad;
    const int title_left = tile_indent_px;
    const int title_right = kMenuItemPad + kMenuPadRight;

    const int pad = dcss_popup_base_margin(this->area_.h, this->small_layout_) + kPopupPadding;
    const int avail_w = std::max(0, this->area_.w - (2 * pad));
    const int avail_h = std::max(0, this->area_.h - (2 * pad) - (w.centred || this->small_layout_ ? 0 : depth * kPopupDepthIndent));

    // 題（`Menu::update_title`。字の左は 2 画素、下の余白 10）。
    std::vector<DcssStyledLine> title_lines;
    if (!menu.title.empty()) {
        title_lines = dcss_split_styled_lines(dcss_parse_window_text(menu.title, kLightGrey, kLightGrey));
    }
    const int title_nat_w = title_lines.empty() ? 0 : (dcss_lines_width(title_lines, M) + title_left + title_right);

    const DcssMenuLayout natural = dcss_layout_menu(menu, 2000000000, 1, M);
    const int menu_pref_w = dcss_menu_preferred_width(natural, M);

    bool scrollable = false;
    std::vector<DcssStyledLine> more_lines;
    int more_h = 0;
    int title_h = 0;
    DcssMenuLayout lay;
    int content_w = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const std::string &more_text = scrollable ? menu.more : (menu.alt_more.empty() && menu.more.empty() ? std::string() : menu.alt_more);
        std::string shown = more_text;
        more_lines.clear();
        if (!shown.empty()) {
            // `XXX` は捲り位置の割合。いまは幅の計算だけなので仮の字を入れる。
            const std::size_t at = shown.find("XXX");
            if (at != std::string::npos) {
                shown.replace(at, 3, this->japanese_ ? "先頭" : "top");
            }
            more_lines = dcss_split_styled_lines(dcss_parse_window_text(shown, kLightGrey, kLightGrey));
        }
        int more_nat_w = more_lines.empty() ? 0 : (dcss_lines_width(more_lines, M) + title_right);
        const int nat_w = std::max({ menu_pref_w, title_nat_w, more_nat_w });
        // ゲームパッドの割り当ての 1 枚目（画面の側で積んだ窓。`dcss_pad_screen.h`）は、原作と同じく画面いっぱいに出す。
        content_w = (menu.tag == "hd2d_pad_bindings") ? avail_w : std::min(avail_w, nat_w);
        lay = dcss_layout_menu(menu, content_w, 1, M);
        title_h = title_lines.empty() ? 0 : (static_cast<int>(title_lines.size()) * M.line_h + 10);
        more_h = more_lines.empty() ? 0 : (static_cast<int>(more_lines.size()) * M.line_h + 10);
        const int max_view_h = avail_h - title_h - more_h;
        const bool now_scrollable = lay.height > max_view_h;
        if (now_scrollable == scrollable) {
            break;
        }
        scrollable = now_scrollable;
    }

    const int nat_h = title_h + lay.height + more_h;
    const DcssPopupGeometry geo = dcss_popup_place(this->area_, content_w, nat_h, w.centred, depth, this->small_layout_);
    const RectPx content = geo.content;
    const int view_h = std::max(0, std::min(lay.height, content.h - title_h - more_h));
    v.geo = geo;
    v.title_rect = RectPx{ content.x, content.y, content.w, title_h };
    v.viewport = RectPx{ content.x, content.y + title_h, content.w, view_h };
    v.more_rect = RectPx{ content.x, content.y + title_h + view_h, content.w, more_h };
    v.menu = lay;
    v.menu_total = lay.height;

    // 最初の捲り位置・強調。
    const int max_scroll = std::max(0, lay.height - view_h);
    if (!w.view_init) {
        w.view_init = true;
        w.scroll_y = 0;
        this->menu_update_visible(w, v);
        if ((menu.flags & kMfStartAtEnd) != 0) {
            this->menu_scroll_bottom_to_item(w, static_cast<int>(menu.items.size()) - 1);
        } else if (menu.jump_to > 0) {
            this->menu_scroll_to_item(w, menu.jump_to);
        }
        if ((menu.last_hovered >= 0) && !dcss_menu_item_selectable(menu, menu.last_hovered)) {
            this->menu_cycle_hover(w, false);
        }
    }
    w.scroll_y = std::clamp(w.scroll_y, 0, max_scroll);
    this->menu_update_visible(w, v);
    this->send_scroll_if_changed(w);

    // 下の案内の字（捲り位置の割合を入れる）。
    if (!more_lines.empty()) {
        std::string shown = scrollable ? menu.more : menu.alt_more;
        const std::size_t at = shown.find("XXX");
        if (at != std::string::npos) {
            std::string perc;
            const int scroll_end = lay.height - view_h;
            if ((w.scroll_y <= 0) || (scroll_end <= 0)) {
                perc = this->japanese_ ? "先頭" : "top";
            } else if (w.scroll_y >= scroll_end) {
                perc = this->japanese_ ? "末尾" : "bot";
            } else {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%2d%%", (w.scroll_y * 100) / scroll_end);
                perc = buf;
            }
            shown.replace(at, 3, perc);
        }
        more_lines = dcss_split_styled_lines(dcss_parse_window_text(shown, kLightGrey, kLightGrey));
    }

    // ---- 枠と下の層 ----
    this->draw_scrim_and_frame(paint, geo);
    paint.flush();

    // 題
    {
        int y = content.y;
        for (const DcssStyledLine &l : title_lines) {
            this->draw_styled(content.x + title_left, y, l);
            y += M.line_h;
        }
    }
    // 下の案内
    {
        int y = v.more_rect.y + 10;
        for (const DcssStyledLine &l : more_lines) {
            this->draw_styled(content.x + ((menu.tag == "ability" || menu.tag == "spell") ? tile_indent_px : 0), y, l);
            y += M.line_h;
        }
    }
    this->font_.flush();

    // ---- 本文（捲り領域の中だけ。GL の切り抜きで切る）----
    const ScissorFn scissor = scissor_fn();
    const RectPx vp = v.viewport;
    if (!vp.empty()) {
        if (scissor != nullptr) {
            gl::glEnable(kGlScissorTest);
            scissor(scaled(vp.x), this->screen_h_ - scaled(vp.y + vp.h), scaled(vp.w), scaled(vp.h));
        }
        const int col_width = vp.w;
        const int top = vp.y - w.scroll_y;
        const int visible_from = w.scroll_y - 2 * M.line_h;
        const int visible_to = w.scroll_y + view_h + 2 * M.line_h;
        const int count = static_cast<int>(lay.entries.size());

        // 四角（見出しの線・強調）
        const bool selectable_menu = (menu.flags & kMfNoSelect) == 0;
        for (int i = 0; i < count; ++i) {
            const DcssMenuEntryLayout &e = lay.entries[static_cast<std::size_t>(i)];
            if ((e.y + e.height < visible_from) || (e.y > visible_to)) {
                continue;
            }
            if (e.heading) {
                const int line_y = e.y + ((i == 0) ? 0 : 5) + kMenuItemPad;
                if (((i + 1) < count) && !lay.entries[static_cast<std::size_t>(i + 1)].heading) {
                    paint.rect(RectPx{ vp.x + e.x, top + line_y, lay.num_columns * col_width, 1 }, paint_rgb(64, 64, 64, 200));
                }
            } else if (selectable_menu && (i == menu.last_hovered) && !menu.items[static_cast<std::size_t>(i)].hotkeys.empty()) {
                const int ex = e.column * col_width;
                const RectPx hb{ vp.x + ex, top + e.y, col_width, e.height };
                paint.rect(hb, paint_rgb(255, 255, 255, 25));
                // 強調の囲い（1 画素の線）
                const PaintColor line = paint_rgb(255, 255, 255, 51);
                paint.rect(RectPx{ hb.x + 1, hb.y + 1, hb.w - 1, 1 }, line);
                paint.rect(RectPx{ hb.x + 1, hb.y + hb.h - 1, hb.w - 1, 1 }, line);
                paint.rect(RectPx{ hb.x + 1, hb.y + 1, 1, hb.h - 1 }, line);
                paint.rect(RectPx{ hb.x + hb.w - 1, hb.y + 1, 1, hb.h - 1 }, line);
            }
        }
        paint.flush();
        // 絵
        if (this->atlas_.ready()) {
            for (int i = 0; i < count; ++i) {
                const DcssMenuEntryLayout &e = lay.entries[static_cast<std::size_t>(i)];
                if (e.heading || (e.y + e.height < visible_from) || (e.y > visible_to)) {
                    continue;
                }
                const DcssMenuItem &item = menu.items[static_cast<std::size_t>(i)];
                const int ty = e.y + std::max(e.height - 32, 0) / 2;
                const int ex = e.column * col_width;
                for (const DcssTileRef &t : item.tiles) {
                    this->atlas_.draw(images, t.t, vp.x + ex + kMenuItemPad, top + ty, 1, t.ymax);
                }
            }
        }
        // 字
        for (int i = 0; i < count; ++i) {
            const DcssMenuEntryLayout &e = lay.entries[static_cast<std::size_t>(i)];
            if ((e.y + e.height < visible_from) || (e.y > visible_to)) {
                continue;
            }
            const int ex = e.column * col_width;
            if (e.heading) {
                const int line_y = e.y + ((i == 0) ? 0 : 5) + kMenuItemPad;
                int ty = top + line_y + 3;
                for (const DcssStyledLine &l : e.wrapped) {
                    this->draw_styled(vp.x + e.x + ex, ty, l);
                    ty += M.line_h;
                }
            } else {
                int text_sx = vp.x + ex + e.x + kMenuItemPad;
                if (e.prefix_len > 0) {
                    DcssStyledLine head;
                    for (int k = 0; k < e.prefix_len; ++k) {
                        head.push_back(e.text[static_cast<std::size_t>(k)]);
                    }
                    int text_sy = top + e.y + ((e.height - M.line_h) / 2);
                    this->draw_styled(text_sx, text_sy, head);
                    text_sx += M.cells_width(head);
                }
                const int string_height = static_cast<int>(e.wrapped.size()) * M.line_h;
                int text_sy = top + e.y + ((e.height - string_height) / 2);
                for (const DcssStyledLine &l : e.wrapped) {
                    this->draw_styled(text_sx, text_sy, l);
                    text_sy += M.line_h;
                }
            }
        }
        this->font_.flush();
        if (scissor != nullptr) {
            gl::glDisable(kGlScissorTest);
        }
    }

    // 影と捲り棒（切り抜きの外）。
    this->draw_scroller_bar(paint, vp, lay.height, w.scroll_y);
    paint.flush();

    // 検査の窓口: 項目を押せる部品として記録する。
    UiProbe &probe = ui_probe();
    if (probe.enabled()) {
        ProbeWindow pw;
        pw.kind = "dcss-menu";
        pw.title = dcss_strip_window_text(menu.title);
        pw.type = menu.tag;
        pw.has_rect = true;
        pw.rect = ProbeRect{ geo.frame.x, geo.frame.y, geo.frame.w, geo.frame.h };
        pw.selected = menu.last_hovered;
        for (std::size_t i = 0; i < lay.entries.size(); ++i) {
            const DcssMenuEntryLayout &e = lay.entries[i];
            ProbeItem item;
            item.id = "item:" + std::to_string(i);
            item.label = dcss_strip_window_text(menu.items[i].text);
            item.selected = (static_cast<int>(i) == menu.last_hovered);
            item.enabled = !e.heading;
            pw.items.push_back(std::move(item));
            if (!e.heading && (e.y >= w.scroll_y) && ((e.y + e.height) <= (w.scroll_y + view_h))) {
                probe.button(dcss_strip_window_text(menu.items[i].text),
                    ProbeRect{ vp.x, vp.y + e.y - w.scroll_y, vp.w, e.height }, dcss_menu_item_selectable(menu, static_cast<int>(i)),
                    static_cast<int>(i) == menu.last_hovered, "dcss-menu-row", false, "item:" + std::to_string(i));
            }
        }
        probe.window(std::move(pw));
    }
}

// ================================================================== 全画面の文字画面（txt）

namespace {

void decode_entities(std::string &s)
{
    static const struct {
        const char *from;
        const char *to;
    } kTable[] = { { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&nbsp;", " " }, { "&#39;", "'" }, { "&amp;", "&" } };
    for (const auto &e : kTable) {
        std::size_t at = 0;
        const std::string from = e.from;
        while ((at = s.find(from, at)) != std::string::npos) {
            s.replace(at, from.size(), e.to);
            at += std::strlen(e.to);
        }
    }
}

} // namespace

std::vector<CrtCell> parse_crt_line(const std::string &html)
{
    std::vector<CrtCell> cells;
    std::uint8_t fg = kLightGrey;
    std::uint8_t bg = 0;
    std::string pending;
    const auto flush = [&]() {
        decode_entities(pending);
        for (const char32_t c : dcss_decode_utf8(pending)) {
            if (dcss_cell_width(c) == 0) {
                continue;
            }
            cells.push_back(CrtCell{ c, fg, bg });
            if (dcss_cell_width(c) == 2) {
                cells.push_back(CrtCell{ kWideFiller, fg, bg });
            }
        }
        pending.clear();
    };
    for (std::size_t i = 0; i < html.size();) {
        if (html[i] == '<') {
            const std::size_t close = html.find('>', i + 1);
            if (close == std::string::npos) {
                pending.push_back(html[i]);
                ++i;
                continue;
            }
            const std::string tag = html.substr(i + 1, close - i - 1);
            i = close + 1;
            flush();
            if (tag.compare(0, 4, "span") == 0) {
                const std::size_t f = tag.find("fg");
                const std::size_t b = tag.find("bg");
                fg = (f != std::string::npos) ? static_cast<std::uint8_t>(std::atoi(tag.c_str() + f + 2) & 15) : static_cast<std::uint8_t>(kLightGrey);
                bg = (b != std::string::npos) ? static_cast<std::uint8_t>(std::atoi(tag.c_str() + b + 2) & 15) : static_cast<std::uint8_t>(0);
            } else if (tag == "/span") {
                fg = kLightGrey;
                bg = 0;
            }
            continue;
        }
        pending.push_back(html[i]);
        ++i;
    }
    flush();
    return cells;
}

namespace {

//! 文字画面の選べる項目 1 つ（開始メニューのゲームモード・保存したゲーム・「新しいゲーム」）。
struct CrtItem {
    int row{ 0 };
    int c0{ 0 }; //!< 最初の字のマス（半角単位）
    int c1{ 0 }; //!< 最後の字のマス
    bool focused{ false }; //!< コアが選択中として送った項目（原作は黒字 fg0。背景が来ない）
};

/*!
 * @brief 開始メニューの文字画面から、選べる項目を行の順に拾う。
 * @details 原作の `UIStartupMenu` の並び: 1 行目の左に項目名（`fg7`、桁 0）が来る行が「名前」「ゲームモード」「保存したゲーム」の 3 つ。
 * 名前の行の次の項目名の行から、その行の項目名の右の字と、続く（桁 8 以上から始まる）行が項目。
 */
std::vector<CrtItem> crt_items(const std::vector<std::vector<CrtCell>> &rows)
{
    std::vector<CrtItem> items;
    const auto first_text = [&](int r) -> int {
        const auto &cells = rows[static_cast<std::size_t>(r)];
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (cells[i].ch != U' ') {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    int label_no = 0;
    bool in_block = false;
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const auto &cells = rows[static_cast<std::size_t>(r)];
        const int f = first_text(r);
        if (f < 0) {
            in_block = false;
            continue;
        }
        int start = -1;
        if ((f == 0) && (cells[0].fg == kLightGrey)) {
            // 項目名の行
            ++label_no;
            in_block = (label_no >= 2);
            if (!in_block) {
                continue;
            }
            for (std::size_t i = 0; i < cells.size(); ++i) {
                if ((cells[i].ch != U' ') && (cells[i].fg != kLightGrey)) {
                    start = static_cast<int>(i);
                    break;
                }
            }
        } else if (in_block && (f >= 8)) {
            start = f;
        } else {
            in_block = false;
            continue;
        }
        if (start < 0) {
            continue;
        }
        int end = start;
        for (std::size_t i = static_cast<std::size_t>(start); i < cells.size(); ++i) {
            if ((cells[i].ch != U' ') && (cells[i].ch != kWideFiller)) {
                end = static_cast<int>(i);
                if (dcss_cell_width(cells[i].ch) == 2) {
                    end = static_cast<int>(i) + 1;
                }
            }
        }
        CrtItem it;
        it.row = r;
        it.c0 = start;
        it.c1 = end;
        it.focused = (cells[static_cast<std::size_t>(start)].fg == 0);
        items.push_back(it);
    }
    return items;
}

} // namespace

void DcssWindows::draw_crt(DcssWindow &w, int depth, UiPaint &paint, const UiImagePainter &images)
{
    if (this->draw_startup(w, depth, paint, images)) {
        return;
    }
    View &v = *this->views_[w.generation];
    const DcssTextMeasure &M = this->measure_;
    // 80 桁 × 使われている行数の文字画面を窓の枠の中に置く。
    std::vector<std::vector<CrtCell>> rows;
    int last = -1;
    const auto &lines = this->stack_.crt_lines();
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        rows.push_back(parse_crt_line(lines[static_cast<std::size_t>(i)]));
        if (!lines[static_cast<std::size_t>(i)].empty()) {
            last = i;
        }
    }
    const int row_count = std::max(1, last + 1);
    const int cols = 80;
    const int nat_w = cols * M.half_w;
    const int nat_h = row_count * M.line_h;
    const DcssPopupGeometry geo = dcss_popup_place(this->area_, nat_w, nat_h, true, depth, this->small_layout_, nat_h);
    v.geo = geo;
    v.need_h = nat_h + (2 * this->fit_pad());
    this->draw_scrim_and_frame(paint, geo);
    // 背景色つきのマス
    for (int r = 0; r < row_count; ++r) {
        int x = geo.content.x;
        for (const CrtCell &c : rows[static_cast<std::size_t>(r)]) {
            if (c.ch == kWideFiller) {
                x += M.half_w;
                continue;
            }
            const int adv = dcss_cell_width(c.ch) * M.half_w;
            if (c.bg != 0) {
                const DcssRgb &rgb = dcss_term_rgb(c.bg);
                paint.rect(RectPx{ x, geo.content.y + (r * M.line_h), adv, M.line_h }, paint_rgb(rgb.r, rgb.g, rgb.b));
            }
            x += adv;
        }
    }
    // 選べる項目（開始メニュー）。選択中は原作のタイル版と同じ見た目（薄い白の地と明るい灰色の枠、白い字）、
    // マウスの下は暗くした枠（`MenuButton::_allocate_region`）。
    v.hits.clear();
    const std::vector<CrtItem> items = crt_items(rows);
    // マウスの下へ選択を移すキーを送って、コアの返事（選択中の項目が変わった文字画面）が来るまでは、送った先を選択中として描く。
    {
        int actual = -1;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].focused) {
                actual = static_cast<int>(i);
            }
        }
        if ((w.crt_pending_focus >= 0) && ((actual == w.crt_pending_focus) || ((SDL_GetTicks() - w.crt_pending_at) >= 400))) {
            w.crt_pending_focus = -1;
        }
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        const CrtItem &it = items[i];
        const int pad = 8;
        const RectPx box{ geo.content.x + (it.c0 * M.half_w) - pad, geo.content.y + (it.row * M.line_h), ((it.c1 - it.c0 + 1) * M.half_w) + (pad * 2), M.line_h };
        const bool box_focus = (w.crt_pending_focus >= 0) ? (w.crt_pending_focus == static_cast<int>(i)) : it.focused;
        v.hits.push_back(View::Hit{ box, 2, static_cast<int>(i), it.focused ? 1 : 0 });
        if (!box_focus) {
            continue;
        }
        paint.rect(box, paint_rgb(255, 255, 255, 25));
        const DcssRgb &grey = dcss_term_rgb(kLightGrey);
        const PaintColor border = paint_rgb(grey.r, grey.g, grey.b);
        paint.rect(RectPx{ box.x, box.y, box.w, 1 }, border);
        paint.rect(RectPx{ box.x, box.y + box.h - 1, box.w, 1 }, border);
        paint.rect(RectPx{ box.x, box.y, 1, box.h }, border);
        paint.rect(RectPx{ box.x + box.w - 1, box.y, 1, box.h }, border);
    }
    paint.flush();
    for (int r = 0; r < row_count; ++r) {
        DcssStyledLine line;
        const auto &cells = rows[static_cast<std::size_t>(r)];
        for (std::size_t ci = 0; ci < cells.size(); ++ci) {
            const CrtCell &c = cells[ci];
            if (c.ch == kWideFiller) {
                continue;
            }
            int fg = c.fg;
            for (const CrtItem &it : items) {
                if (it.focused && (it.row == r) && (static_cast<int>(ci) >= it.c0) && (static_cast<int>(ci) <= it.c1) && (fg == 0)) {
                    fg = kWhite; // 選択中の字は黒で送られてくる（背景が来ない）。読める色にする
                }
            }
            line.push_back(DcssStyledChar{ c.ch, static_cast<decltype(DcssStyledChar::colour)>(fg) });
        }
        this->draw_styled(geo.content.x, geo.content.y + (r * M.line_h), line);
    }
    this->font_.flush();
    (void)w;
}

} // namespace hd2d::dcss
