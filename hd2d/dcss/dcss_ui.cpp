/*!
 * @file dcss_ui.cpp
 * @brief `dcss_ui.h` の実装。
 */
#include "dcss/dcss_ui.h"

#include "app/ui_probe.h"

#include <SDL2/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace hd2d::dcss {

namespace {

//! 原作の字（`settings/init.txt` の `tile_font_*_file`）。Windows は OS の BIZ UDゴシック、無ければ同梱の OFL 版。
const char *const kFontCandidates[] = {
    "C:/Windows/Fonts/BIZ-UDGothicR.ttc",
    "assets/fonts/dcss/BIZUDGothic-Regular.ttf",
};

//! 原作は `FT_Set_Pixel_Sizes` の高さを `max_advance` の元にする。BIZ UDゴシックは最大送り幅が 1 em（全角の幅）。
DcssFontMetrics metrics_for(int px)
{
    return DcssFontMetrics{ px, px, px };
}

PaintColor paint_rgb(const DcssRgb &c, float a = 1.f)
{
    return PaintColor{ static_cast<float>(c.r) / 255.f, static_cast<float>(c.g) / 255.f, static_cast<float>(c.b) / 255.f, a };
}

TextColor text_rgb(const DcssRgb &c)
{
    return TextColor{ static_cast<float>(c.r) / 255.f, static_cast<float>(c.g) / 255.f, static_cast<float>(c.b) / 255.f, 1.f };
}

//! 字の高さの基準線の合わせ。原作は基準線を `FT_Size_Metrics.ascender`（丸め）に置き、SDL_ttf は切り上げた値に置く。
constexpr int kGlyphYShift = -1;
//! 窓の拡大（Android・Quest）では字を 2 倍以上の大きさで描いて縮尺しているので、基準線の丸めの違いは 1 画素より小さい（論理の座標で 0）。-1 のままだと 1 行目の上が切れる。

std::vector<std::string> grid_to_lines(const DcssTextGrid &g)
{
    std::vector<std::string> out;
    for (int y = 0; y < g.rows; ++y) {
        std::string line;
        for (int x = 0; x < g.cols; ++x) {
            const char32_t c = g.at(x, y);
            if (c == kWideFiller) {
                continue;
            }
            line += dcss_encode_utf8(c);
        }
        while (!line.empty() && (line.back() == ' ')) {
            line.pop_back();
        }
        out.push_back(std::move(line));
    }
    while (!out.empty() && out.back().empty()) {
        out.pop_back();
    }
    return out;
}

/*!
 * 窓の拡大（Android・Quest。`DcssUi::use_android_profile`）の間だけ、四角・絵・字を座標系ごと倍率倍に拡大して描く。
 * 倍率 1（Windows・Linux）では何もしない。四角は積んだ時点で、絵は描いた時点で、字は積んだ時点で拡大されるので、
 * この範囲で積んだ分は、後で流すときに倍率を戻していても正しく出る（`UiPaint`・`TextOverlay` の `set_layout_scale`）。
 */
class ScaleScope {
public:
    ScaleScope(float scale, UiPaint &paint, const UiImagePainter &images, std::vector<TextOverlay *> texts)
        : scale_(scale), paint_(paint), images_(images), texts_(std::move(texts))
    {
        if (this->scale_ > 1.f) {
            this->paint_.set_layout_scale(this->scale_);
            this->images_.set_layout_scale(this->scale_);
            for (TextOverlay *t : this->texts_) {
                t->set_layout_scale(this->scale_);
            }
        }
    }
    ~ScaleScope()
    {
        if (this->scale_ > 1.f) {
            this->paint_.set_layout_scale(1.f);
            this->images_.set_layout_scale(1.f);
            for (TextOverlay *t : this->texts_) {
                t->set_layout_scale(1.f);
            }
        }
    }
    ScaleScope(const ScaleScope &) = delete;
    ScaleScope &operator=(const ScaleScope &) = delete;

private:
    float scale_;
    UiPaint &paint_;
    const UiImagePainter &images_;
    std::vector<TextOverlay *> texts_;
};

} // namespace

DcssUi::DcssUi()
{
    // 割り当ての画面の窓（画面の側で積んだ窓）の操作は、コアへ送らずここへ渡す（段 5）。
    this->windows_.set_local_handler([this](const std::string &line) { this->pad_screen_.on_line(line); });
}

DcssUi::~DcssUi()
{
    this->shutdown();
}

void DcssUi::set_language(const std::string &lang)
{
    const bool ja = (lang != "en");
    if (ja != this->japanese_) {
        this->japanese_ = ja;
        this->stat_dirty_ = true;
    }
    this->windows_.set_language(lang);
    this->pad_.set_language(lang);
}

void DcssUi::use_android_profile(int display_w, int display_h)
{
    if (!this->android_profile_) {
        // 初めて呼ばれたとき: 原作の Android 版の足し物（見出しの絵は既定で隠す・システム命令とゲームメニューのキーボード・ツールチップ無し）。
        this->tab_icons_shown_ = false;
        this->tabs_.set_android_extras(true);
        this->windows_.set_android_extras(true, [this]() { this->keyboard_toggle_ = true; });
        this->map_mouse_.set_tooltip_enabled(false);
    }
    this->android_profile_ = true;
    this->display_w_ = display_w;
    this->display_h_ = display_h;
}

int DcssUi::glyph_y_shift() const
{
    return (this->ui_scale_ > 1) ? 0 : kGlyphYShift;
}

SDL_Event DcssUi::to_logical(const SDL_Event &event, float scale) const
{
    SDL_Event out = event;
    if (scale <= 1.f) {
        return out;
    }
    const auto div = [&](int v) { return static_cast<int>(std::floor(static_cast<float>(v) / scale)); };
    switch (event.type) {
    case SDL_MOUSEMOTION:
        out.motion.x = div(event.motion.x);
        out.motion.y = div(event.motion.y);
        out.motion.xrel = div(event.motion.xrel);
        out.motion.yrel = div(event.motion.yrel);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        out.button.x = div(event.button.x);
        out.button.y = div(event.button.y);
        break;
    default:
        break;
    }
    return out;
}

void DcssUi::ingest_payload(const std::string &payload)
{
    const nlohmann::json m = nlohmann::json::parse(payload, nullptr, false);
    if (m.is_discarded() || !m.is_object()) {
        return;
    }
    const std::string type = m.value("t", std::string());
    if (type == "webtiles") {
        const auto line = m.find("line");
        if ((line != m.end()) && line->is_string()) {
            (void)this->model_.ingest_line(line->get<std::string>());
            this->windows_.set_pregame(!this->model_.player().seen); // 名前の問い（`DcssWindowStack::name_prompt`）の検出に要る
            (void)this->windows_.ingest_line(line->get<std::string>()); // 窓の部品（段 U3）
        }
    } else if (type == "dcss_ui") {
        const std::string kind = m.value("k", std::string());
        if (kind == "char") {
            this->stat_panel_.set_character_abbrev(m.value("abbrev", std::string()));
            this->stat_dirty_ = true;
        } else if (kind == "hover") {
            this->map_mouse_.ingest_reply(payload); // 地図のマスの説明（ツールチップ。段 U3・U4）
        } else if (kind == "marks") {
            if (this->marks_.ingest_json(payload)) { // 地図の上の印（態度・強さの輪・雲。段 3）と、攻撃の演出（ビーム・矢・爆発・閃光。段 4）
                this->fx_.update(this->marks_.fx_cells(), this->marks_.flash_cells(), this->marks_.player_gx(), this->marks_.player_gy(), DcssFx::seconds_now());
            }
        } else if (kind == "tabs") {
            (void)this->tabs_.ingest_json(payload); // タブの欄の中身（持ち物・呪文ほか。段 U2）
        } else if (kind == "startup") {
            (void)this->windows_.ingest_startup(m); // 開始メニューの材料（モードの一覧・セーブデータと人形。段 U3）
        } else if (kind == "pad") {
            (void)this->pad_.ingest_table(m); // ゲームパッドの割り当てられるコマンドの表（番号・名前・いまのキー。段 5）
        }
    }
}

bool DcssUi::open_font(FontSlot &slot, int px, std::string &err)
{
    if (slot.open && (slot.px == px)) {
        return true;
    }
    if (slot.open) {
        slot.text.shutdown();
        slot.open = false;
    }
    std::string last_err;
    //! 検査用: `HD2D_DCSS_FONT` があれば、その 1 つだけを使う（同梱の OFL 版の字が OS の字と同じ並びで出ることを確かめるため）。
    std::vector<std::string> candidates;
    if (const char *forced = std::getenv("HD2D_DCSS_FONT"); (forced != nullptr) && (*forced != '\0')) {
        candidates.emplace_back(forced);
    } else {
        for (const char *path : kFontCandidates) {
            candidates.emplace_back(path);
        }
    }
    for (const std::string &path : candidates) {
        if (!this->font_path_.empty() && (this->font_path_ != path)) {
            continue; // 開けたものを使い続ける
        }
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            continue;
        }
        std::string e;
        if (slot.text.init_with_font(px, path, 0, e)) {
            if (this->ui_scale_ > 1) {
                // 拡大して描くとき、全角は半角のちょうど 2 倍で並べる（原作と同じ。拡大した字にも引き継がれる）。
                slot.text.set_wide_advance(std::max(1, slot.text.cell_w()) * 2);
            }
            slot.px = px;
            slot.open = true;
            this->font_path_ = path;
            return true;
        }
        last_err = e;
        slot.text.shutdown();
    }
    err = "DCSS の字（BIZ UDゴシック）を開けませんでした。" + last_err;
    return false;
}

bool DcssUi::prepare(int origin_x, int origin_y, int usable_w, int usable_h, int reserved_bottom, std::string &err)
{
    if (this->android_profile_) {
        // 原作の Android 版と同じ: 窓を game_scale 分の 1 にした論理の窓で配置し、描くときに game_scale 倍に拡大する。
        // 切り欠きなどの余白の内側だけを使う（左上は切り上げ・右下は切り下げ）。
        this->android_ = dcss_android_profile(this->display_w_, this->display_h_);
        const int s = std::max(1, this->android_.game_scale);
        this->ui_scale_ = s;
        const int x0 = (origin_x + s - 1) / s;
        const int y0 = (origin_y + s - 1) / s;
        const int x1 = (origin_x + usable_w) / s;
        const int y1 = (origin_y + usable_h) / s;
        reserved_bottom = (reserved_bottom + s - 1) / s;
        origin_x = x0;
        origin_y = y0;
        usable_w = std::max(1, x1 - x0);
        usable_h = std::max(1, y1 - y0);
        this->profile_ = this->android_.fonts;
    } else {
        this->ui_scale_ = 1;
        this->profile_ = dcss_screen_profile(usable_w, usable_h);
    }
    this->origin_x_ = origin_x;
    this->origin_y_ = origin_y;
    this->usable_w_ = usable_w;
    this->usable_h_ = usable_h;
    this->reserved_bottom_ = reserved_bottom;
    if (!this->open_font(this->stat_font_, this->profile_.stat_px, err) || !this->open_font(this->msg_font_, this->profile_.msg_px, err)
        || !this->open_font(this->lbl_font_, this->profile_.lbl_px, err)) {
        this->layout_ = DcssLayout{};
        return false;
    }

    DcssLayoutInput in;
    in.window_w = usable_w;
    in.window_h = std::max(1, usable_h - reserved_bottom);
    in.fonts.crt = metrics_for(this->profile_.crt_px);
    in.fonts.msg = metrics_for(this->profile_.msg_px);
    in.fonts.stat = metrics_for(this->profile_.stat_px);
    in.fonts.tip = metrics_for(this->profile_.tip_px);
    in.fonts.lbl = metrics_for(this->profile_.lbl_px);
    in.tile_map_pixels = 0;
    if (this->android_profile_) {
        in.tile_map_pixels = this->profile_.map_px;
        in.tile_viewport_scale_pct = this->android_.viewport_scale_pct;
        in.tile_map_scale_pct = this->android_.map_scale_pct;
        in.show_tab_icons = this->tab_icons_shown_; // 原作 `tilereg-tab.cc:73`: Android は既定で見出しの絵を出さない（ステータス欄を押すと切り替わる）
    }
    in.spell_count = this->tabs_.spell_count();
    in.ability_count = this->tabs_.ability_count();

    const bool same = this->have_layout_input_ && (in.window_w == this->last_input_.window_w) && (in.window_h == this->last_input_.window_h)
        && (in.fonts.stat.size_px == this->last_input_.fonts.stat.size_px) && (in.fonts.msg.size_px == this->last_input_.fonts.msg.size_px)
        && (in.fonts.lbl.size_px == this->last_input_.fonts.lbl.size_px) && (in.tile_viewport_scale_pct == this->last_input_.tile_viewport_scale_pct)
        && (in.show_tab_icons == this->last_input_.show_tab_icons) && (in.spell_count == this->last_input_.spell_count)
        && (in.ability_count == this->last_input_.ability_count);
    if (!same) {
        this->layout_ = DcssLayout::compute(in);
        this->last_input_ = in;
        this->have_layout_input_ = true;
        this->stat_dirty_ = true;
        ++this->layout_serial_;
        this->stat_panel_.reset_bars();
        if (!this->layout_.ok) {
            err = "DCSS の画面の配置を決められませんでした: " + this->layout_.error;
        }
    }
    if (!this->layout_.ok) {
        return false;
    }
    if (!this->atlas_tried_) {
        // タブの欄の絵（原作のタイルの画像ページ）。無くても遊べる（枠と見出しだけが出る）。
        this->atlas_tried_ = true;
        std::string aerr;
        if (!this->tabs_.load_atlas("assets/ui/dcss/atlas", aerr)) {
            std::fprintf(stderr, "[hd2d] DCSS のタブの欄の絵を読めません: %s\n", aerr.c_str());
        }
        // 立体の雲の模様（原作の雲のタイルの区画ごとの色と不透明度）。無ければ雲は出ない。
        std::string cerr_text;
        if (!this->clouds_.load("assets/ui/dcss/atlas", cerr_text)) {
            std::fprintf(stderr, "[hd2d] DCSS の雲の模様を読めません: %s\n", cerr_text.c_str());
        }
        // 攻撃の演出の絵（ビーム・矢・爆発の原作のタイル）。無ければ演出は出ない。
        std::string ferr_text;
        if (!this->fx_.load("assets/ui/dcss/atlas", ferr_text)) {
            std::fprintf(stderr, "[hd2d] DCSS の攻撃の演出の絵を読めません: %s\n", ferr_text.c_str());
        }
    }
    this->tabs_.set_layout(this->layout_, this->origin_x_, this->origin_y_);
    {
        // 窓の部品の層は窓全体（原作の `UIRoot`）。字の大きさは窓の大きさの表の `tile_font_crt`。
        std::string werr;
        const float k = this->fit_k_; // 収まらない捲れない窓があるとき、層を大きく取って縮めて描く（`draw_windows`）
        const auto vk = [&](int v) { return static_cast<int>(std::lround(static_cast<float>(v) * k)); };
        if (!this->windows_.configure(RectPx{ vk(origin_x), vk(origin_y), vk(usable_w), std::max(1, vk(std::max(1, usable_h - reserved_bottom))) }, this->profile_.crt_px,
                this->layout_.small_layout, werr)
            && !werr.empty()) {
            std::fprintf(stderr, "[hd2d] %s\n", werr.c_str());
        }
    }
    return true;
}

RectPx DcssUi::scene_rect() const
{
    if (!this->layout_.ok) {
        return RectPx{};
    }
    const int s = this->ui_scale_;
    return RectPx{ this->origin_x_ * s, this->origin_y_ * s, this->layout_.dungeon_view_w * s, this->layout_.dungeon_view_h * s };
}

void DcssUi::rebuild_grids()
{
    const DcssRegion &s = this->layout_.stat;
    const bool small = this->layout_.small_layout;
    const int turn = this->model_.player().turn;
    const int redraw = this->stat_panel_.redraw_after_turn();
    if (this->stat_dirty_ || (this->player_version_seen_ != this->model_.player().version) || (redraw != 0 && turn >= redraw)
        || (this->small_seen_ != small)) {
        this->stat_panel_.build(this->model_, s.cols, s.rows, small, this->japanese_, this->stat_grid_);
        this->player_version_seen_ = this->model_.player().version;
        this->stat_dirty_ = false;
        this->small_seen_ = small;
    }
    const DcssRegion &m = this->layout_.message;
    this->msgwin_.resize(m.cols, m.rows);
    this->msgwin_.sync(this->model_);
    this->msgwin_.build(this->model_, this->msg_grid_);
}

void DcssUi::draw_grid(FontSlot &font, UiPaint &paint, int x0, int y0, const DcssTextGrid &g, int char_w, int char_h)
{
    const int half = std::max(1, font.text.cell_w());
    for (int row = 0; row < g.rows; ++row) {
        const int y = y0 + (row * char_h);
        int x = x0;
        // 同じ色が続く字は 1 回で描く（字の位置は原作と同じ: 半角は `half`、全角は 2 × `half` ずつ進む）。
        std::string run;
        int run_x = x;
        int run_colour = -1;
        int pending_spaces = 0; // 同じ色の字にはさまれた空白は 1 回の描きに含める（行の終わり・色の変わり目の空白は捨てる）
        const auto flush_run = [&]() {
            if (!run.empty()) {
                font.text.draw(run_x, y + this->glyph_y_shift(), run, text_rgb(dcss_term_rgb(run_colour)));
                run.clear();
            }
            pending_spaces = 0;
        };
        for (int col = 0; col < g.cols; ++col) {
            char32_t c = g.at(col, row);
            if (c == kWideFiller) {
                if ((col > 0) && (dcss_cell_width(g.at(col - 1, row)) == 2)) {
                    continue; // 全角の 2 マス目（1 マス目が送り幅を持つ）
                }
                c = U' ';
            }
            const bool wide = (dcss_cell_width(c) == 2);
            const int adv = wide ? (half * 2) : half;
            const std::uint8_t attr = g.attr_at(col, row);
            const int bg = attr >> 4;
            const int fg = attr & 0x0F;
            if (bg != 0) {
                paint.rect(RectPx{ x, y, std::max(char_w, adv), char_h }, paint_rgb(dcss_term_rgb(bg)));
            }
            if (c == U' ') {
                if (!run.empty()) {
                    ++pending_spaces;
                }
            } else {
                if (run.empty() || (fg != run_colour)) {
                    flush_run();
                    run_x = x;
                    run_colour = fg;
                } else {
                    run.append(static_cast<std::size_t>(pending_spaces), ' ');
                    pending_spaces = 0;
                }
                run += dcss_encode_utf8(c);
            }
            x += adv;
        }
        flush_run();
    }
}

void DcssUi::draw_minimap(UiPaint &paint)
{
    if (!this->layout_.has_minimap) {
        return;
    }
    const DcssRegion &r = this->layout_.minimap;
    const int dx = std::max(1, r.cell_w);
    const DcssUiModel::MapBounds b = this->model_.map_bounds();
    if (!b.valid) {
        return;
    }
    const int ox = (r.w - (dx * (b.max_x - b.min_x))) / 2;
    const int oy = (r.h - (dx * (b.max_y - b.min_y))) / 2;
    const int base_x = this->origin_x_ + r.x + ox;
    const int base_y = this->origin_y_ + r.y + oy;
    const DcssOptions &opt = this->model_.options();
    this->model_.for_each_map_cell([&](int cx, int cy, int mf) {
        if ((mf < 0) || (mf >= kMfCount)) {
            return;
        }
        paint.rect(RectPx{ base_x + ((cx - b.min_x) * dx), base_y + ((cy - b.min_y) * dx), dx, dx }, paint_rgb(opt.map_colour[mf]));
    });
    // 見えている範囲の枠（原作 `set_window`: 中心 ± 区画のマス数の半分）。
    if (this->model_.player_pos_known()) {
        const int cells_x = this->layout_.dungeon.cols;
        const int cells_y = this->layout_.dungeon.rows;
        const int cx = this->model_.view_centre_x();
        const int cy = this->model_.view_centre_y();
        const int sx = cx - (cells_x / 2);
        const int sy = cy - (cells_y / 2);
        const int ex = cx + (cells_x / 2) + 1;
        const int ey = cy + (cells_y / 2) + 1;
        const int x0 = base_x + ((sx - b.min_x) * dx);
        const int y0 = base_y + ((sy - b.min_y) * dx);
        const int x1 = base_x + ((ex - b.min_x) * dx) + 1;
        const int y1 = base_y + ((ey - b.min_y) * dx) + 1;
        const RectPx clip{ this->origin_x_ + r.x, this->origin_y_ + r.y, r.w, r.h };
        const PaintColor col = paint_rgb(opt.window_colour);
        const auto clipped = [&](RectPx rc) {
            const int ax = std::max(rc.x, clip.x);
            const int ay = std::max(rc.y, clip.y);
            const int bx = std::min(rc.x + rc.w, clip.x + clip.w);
            const int by = std::min(rc.y + rc.h, clip.y + clip.h);
            return RectPx{ ax, ay, std::max(0, bx - ax), std::max(0, by - ay) };
        };
        paint.rect(clipped(RectPx{ x0, y0, x1 - x0, 1 }), col);
        paint.rect(clipped(RectPx{ x0, y1 - 1, x1 - x0, 1 }), col);
        paint.rect(clipped(RectPx{ x0, y0, 1, y1 - y0 }), col);
        paint.rect(clipped(RectPx{ x1 - 1, y0, 1, y1 - y0 }), col);
    }
}

void DcssUi::draw(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h)
{
    if (!this->layout_.ok || !this->stat_font_.open || !this->msg_font_.open) {
        return;
    }
    this->stat_font_.text.begin(screen_w, screen_h);
    this->msg_font_.text.begin(screen_w, screen_h);
    this->lbl_font_.text.begin(screen_w, screen_h);
    if (!this->in_game()) {
        return;
    }
    const ScaleScope scale_scope(this->ui_scale_, paint, images,
        { this->stat_font_.open ? &this->stat_font_.text : nullptr, this->msg_font_.open ? &this->msg_font_.text : nullptr,
            this->lbl_font_.open ? &this->lbl_font_.text : nullptr });
    this->rebuild_grids();

    const DcssLayout &L = this->layout_;
    const PaintColor black{ 0.f, 0.f, 0.f, 1.f };
    // 立体の地図の外は黒（原作は窓全体を黒で塗って区画を重ねる）。
    const int win_h = std::max(1, this->usable_h_ - this->reserved_bottom_);
    paint.rect(RectPx{ this->origin_x_ + L.stat_x_divider, this->origin_y_, std::max(0, this->usable_w_ - L.stat_x_divider), win_h }, black);
    if (!L.message_overlay) {
        paint.rect(RectPx{ this->origin_x_, this->origin_y_ + L.message_y_divider, L.stat_x_divider, std::max(0, win_h - L.message_y_divider) }, black);
    }

    this->draw_minimap(paint);
    // 原作の層の順は 地図 → メッセージ → ステータス → タブ（`tilesdl.cc`）。タブの欄が上に重なるので、メッセージとステータスの下敷きと字を先に流してからタブを描く。
    // ステータス欄
    {
    const UiProbeLayer probe_layer("dcss-stat");
    this->draw_grid(this->stat_font_, paint, this->origin_x_ + L.stat.x + L.stat.margin_x, this->origin_y_ + L.stat.y + L.stat.margin_y,
        this->stat_grid_, L.stat.cell_w, L.stat.cell_h);
    }
    // メッセージ欄
    if (L.message_overlay) {
        const int lines = this->msgwin_.line_count_for_overlay() + (this->model_.more() ? 1 : 0);
        const int h = std::min(L.message.h, lines * L.message.cell_h);
        if (h > 0) {
            paint.rect(RectPx{ this->origin_x_ + L.message.x, this->origin_y_ + L.message.y, L.message.w, h },
                PaintColor{ 100.f / 255.f, 100.f / 255.f, 100.f / 255.f, static_cast<float>(L.overlay_alpha) / 255.f });
        }
    }
    const UiProbeLayer probe_layer("dcss-msg");
    this->draw_grid(this->msg_font_, paint, this->origin_x_ + L.message.x + L.message.margin_x, this->origin_y_ + L.message.y + L.message.margin_y,
        this->msg_grid_, L.message.cell_w, L.message.cell_h);
    paint.flush();
    this->stat_font_.text.flush();
    this->msg_font_.text.flush();
    // タブの欄の絵（`images`）は呼んだ瞬間に描かれるので、下敷き・四角を先に流す。
    paint.flush();
    this->tabs_.set_state(this->model_.input_mode(), this->model_.map_screen(), this->japanese_);
    this->tabs_.draw(images, this->lbl_font_.text, this->layout_.tabs.cell_w, this->profile_.lbl_px);
    {
        const std::string d = this->tabs_.describe();
        if (d != this->last_tabs_described_) {
            this->last_tabs_described_ = d;
            std::fprintf(stderr, "[hd2d] %s\n", d.c_str());
        }
    }

}

bool DcssUi::on_window_event(const SDL_Event &event, bool reserved_key)
{
    // 原作 `TilesFramework::handle_mouse`: `-more-` の間は、どこを左クリック（タップ）しても進む（窓も地図も見ない）。
    if ((event.type == SDL_MOUSEBUTTONDOWN) && (event.button.button == SDL_BUTTON_LEFT) && !this->windows_.captures_keys()
        && (this->model_.input_mode() == kInputMore)) {
        this->windows_.send_enter();
        return true;
    }
    const bool consumed = this->windows_.on_event(this->to_logical(event, this->window_scale()), reserved_key);
    this->pad_screen_.service(); // 割り当ての画面の窓の操作（Enter・Esc）を、窓のキーの処理が終わったあとで実行する
    return consumed;
}

bool DcssUi::on_back_key(const SDL_Event &event)
{
    if ((event.type != SDL_KEYDOWN) || (event.key.keysym.sym != SDLK_AC_BACK)) {
        return false;
    }
    this->windows_.send_escape();
    return true;
}

bool DcssUi::take_keyboard_toggle()
{
    const bool toggled = this->keyboard_toggle_ || this->tabs_.take_keyboard_toggle();
    this->keyboard_toggle_ = false;
    return toggled;
}

bool DcssUi::on_tab_mouse(const DcssTabMouse &ev)
{
    DcssTabMouse logical = ev; // 窓の座標を論理の座標に直す（倍率 1 なら同じ）
    logical.x = ev.x / this->ui_scale_;
    logical.y = ev.y / this->ui_scale_;
    if (this->tabs_.on_mouse(logical)) {
        return true;
    }
    if ((ev.kind != DcssTabMouse::Kind::Press) || (ev.button != 1) || !this->layout_.ok || (this->model_.input_mode() != 1)) {
        return false; // 原作: どちらの欄も左クリックで、命令待ち（`MOUSE_MODE_COMMAND`）のときだけ
    }
    const auto inside = [&](const DcssRegion &r) {
        const int x = logical.x - this->origin_x_;
        const int y = logical.y - this->origin_y_;
        return (r.w > 0) && (r.h > 0) && (x >= r.x) && (x < (r.x + r.w)) && (y >= r.y) && (y < (r.y + r.h));
    };
    const DcssRegion &stat = this->layout_.stat;
    if (inside(stat)) {
        if (this->android_profile_ && this->layout_.small_layout) {
            this->tab_icons_shown_ = !this->tab_icons_shown_; // `CMD_TOGGLE_TAB_ICONS`（次の `prepare` で配置を組み直す）
        } else {
            this->region_out_.push_back(DcssTabOutgoing{ true, "{\"msg\":\"key\",\"keycode\":37}" }); // `CMD_RESISTS_SCREEN`（既定のキー `%`）
        }
        return true;
    }
    if (!this->layout_.message_overlay && inside(this->layout_.message)) {
        this->region_out_.push_back(DcssTabOutgoing{ true, "{\"msg\":\"key\",\"keycode\":16}" }); // `CMD_REPLAY_MESSAGES`（既定のキー Ctrl-P）
        return true;
    }
    return false;
}

std::vector<DcssTabOutgoing> DcssUi::take_tab_outgoing()
{
    std::vector<DcssTabOutgoing> out = this->tabs_.take_outgoing();
    for (DcssTabOutgoing &o : this->region_out_) {
        out.push_back(std::move(o));
    }
    this->region_out_.clear();
    return out;
}

bool DcssUi::on_map_event(const SDL_Event &event, bool on_map, int dx, int dy)
{
    if (!this->in_game() || !this->model_.player_pos_known() || this->windows_.captures_keys()) {
        return false;
    }
    const SDL_Keymod mod = SDL_GetModState();
    const int mods = (((mod & KMOD_SHIFT) != 0) ? 1 : 0) | (((mod & KMOD_CTRL) != 0) ? 2 : 0);
    return this->map_mouse_.on_event(this->to_logical(event, static_cast<float>(this->ui_scale_)), on_map, dx, dy, this->model_.player_x(), this->model_.player_y(), this->model_.input_mode(), mods);
}

void DcssUi::draw_text_input_echo(UiPaint &paint, int screen_w, int screen_h)
{
    // 1 行の文字入力（`init_input`。メッセージ欄の中の問い。例: 刻印・検索）。問いの文はメッセージとして欄に出ているので、
    // 打ち込んだ字は、欄の最後の行の字の終わりに続けて描く（原作は欄の中で行を編集する）。
    const DcssTextInput &in = this->windows_.stack().text_input();
    if (!in.active || !this->msg_font_.open || this->msg_grid_.rows <= 0) {
        return;
    }
    const DcssTextGrid &g = this->msg_grid_;
    const DcssRegion &m = this->layout_.message;
    const int half = std::max(1, this->msg_font_.text.cell_w());
    int row = -1;
    int end_x = 0;
    for (int y = g.rows - 1; (y >= 0) && (row < 0); --y) {
        int x_acc = 0;
        int last_end = -1;
        for (int x = 0; x < g.cols; ++x) {
            const char32_t c = g.at(x, y);
            if (c == kWideFiller) {
                continue;
            }
            x_acc += (dcss_cell_width(c) == 2) ? (half * 2) : half;
            if (c != U' ') {
                last_end = x_acc;
            }
        }
        if (last_end >= 0) {
            row = y;
            end_x = last_end;
        }
    }
    if (row < 0) {
        return;
    }
    const int x = this->origin_x_ + m.x + m.margin_x + end_x + half;
    const int y = this->origin_y_ + m.y + m.margin_y + (row * m.cell_h);
    const std::string text = in.typed + "_";
    const UiProbeLayer probe_layer("dcss-msg");
    paint.flush();
    this->msg_font_.text.begin(screen_w, screen_h);
    if (this->ui_scale_ > 1) {
        this->msg_font_.text.set_layout_scale(static_cast<float>(this->ui_scale_));
    }
    this->msg_font_.text.draw(x, y + this->glyph_y_shift(), text, text_rgb(dcss_term_rgb(kWhite)));
    this->msg_font_.text.flush();
}

void DcssUi::draw_windows(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h)
{
    const float ws = this->window_scale();
    const ScaleScope scale_scope(ws, paint, images, {});
    this->windows_.set_ui_scale(ws);
    this->draw_windows_body(paint, images, screen_w, screen_h);
    this->update_window_fit();
    this->draw_pad_cursor(paint); // 窓の上（いちばん上）に疑似カーソルを描く（段 5）
}

void DcssUi::update_window_fit()
{
    // 捲れない窓（作成の格子・文字画面・開始メニュー）が層に収まらないとき、層を大きく取って縮めて描く。次のフレームの `prepare` が配置し直す。
    float target = 1.f;
    const int need = this->windows_.fit_need_h();
    const int base_h = std::max(1, this->usable_h_ - this->reserved_bottom_);
    if (this->windows_.has_windows() && (need > base_h)) {
        target = std::ceil(static_cast<float>(need) * 20.f / static_cast<float>(base_h)) / 20.f;
        target = std::min(target, static_cast<float>(this->ui_scale_));
    }
    if ((target > this->fit_k_) || (target == 1.f) || (target < (this->fit_k_ - 0.06f))) {
        this->fit_k_ = target;
    }
}

void DcssUi::draw_pad_cursor(UiPaint &paint)
{
    int x = 0;
    int y = 0;
    if (!this->pad_.cursor_visible() || !this->pad_.cursor_position(x, y)) {
        return;
    }
    // 白い矢印（黒い縁つき）。大きさは窓の高さに合わせる（小さい窓でも見える）。
    const float u = std::max(1.f, static_cast<float>(this->usable_h_) / 720.f);
    const float ws = this->window_scale(); // 窓の部品の層を描く倍率（`draw_windows` の `ScaleScope` と同じ）
    const float fx = static_cast<float>(x) / ws; // カーソルの位置は窓の座標。層の座標に直す
    const float fy = static_cast<float>(y) / ws;
    const PaintColor black{ 0.f, 0.f, 0.f, 1.f };
    const PaintColor white{ 1.f, 1.f, 1.f, 1.f };
    // 縁（少し大きい黒の矢印）→ 白の矢印。先端が (x, y)。
    paint.triangle(fx - (1.5f * u), fy - (1.5f * u), fx - (1.5f * u), fy + (17.f * u), fx + (11.5f * u), fy + (12.f * u), black);
    paint.triangle(fx + (2.f * u), fy + (10.f * u), fx + (6.f * u), fy + (21.f * u), fx + (11.f * u), fy + (19.f * u), black);
    paint.triangle(fx, fy, fx, fy + (14.f * u), fx + (9.f * u), fy + (10.5f * u), white);
    paint.triangle(fx + (3.5f * u), fy + (10.f * u), fx + (6.5f * u), fy + (19.f * u), fx + (9.f * u), fy + (18.f * u), white);
    paint.flush();
}

void DcssUi::draw_windows_body(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h)
{
    if (!this->windows_.has_windows() && this->windows_.name_prompt()) {
        // 名前の問い（原作 `_choose_name`。ウェブタイル版の構造に窓が無いので、画面が組む）。背景は作成の窓と同じ黒。
        this->windows_.draw(paint, images, screen_w, screen_h);
        return;
    }
    if (!this->windows_.has_windows()) {
        if (!this->in_game() && this->windows_.title_pending()) {
            // 起動の読み込み画面（コアが原作のデータを読んでいる間。原作の `loading-screen.cc`）。背景は黒。
            paint.rect(RectPx{ this->origin_x_, this->origin_y_, this->usable_w_, std::max(1, this->usable_h_ - this->reserved_bottom_) },
                PaintColor{ 0.f, 0.f, 0.f, 1.f });
            this->windows_.draw_title(paint, images, screen_w, screen_h);
            return;
        }
        // 窓が無いとき: 地図のマスの説明とツールチップ（原作は窓が開くと消える）。
        if (this->in_game() && this->layout_.ok) {
            this->draw_text_input_echo(paint, screen_w, screen_h);
            std::string terr;
            if (this->open_font(this->tip_font_, this->profile_.tip_px, terr) && this->lbl_font_.open) {
                const RectPx area{ this->origin_x_, this->origin_y_, this->usable_w_, std::max(1, this->usable_h_ - this->reserved_bottom_) };
                this->map_mouse_.draw(paint, this->tip_font_.text, this->lbl_font_.text, area, screen_w, screen_h, this->ui_scale_);
            }
        }
        return;
    }
    this->map_mouse_.clear();
    if (!this->in_game()) {
        // ゲームに入る前（開始メニュー・キャラクター作成）の背景は黒（原作は窓全体を黒で塗って窓を重ねる）。下の HD2D の部品を隠す。
        paint.rect(this->windows_.area(), PaintColor{ 0.f, 0.f, 0.f, 1.f }); // 層の矩形（縮めて描くときは大きい）
    }
    this->windows_.draw(paint, images, screen_w, screen_h);
}

void DcssUi::flush_text()
{
    if (this->stat_font_.open) {
        this->stat_font_.text.flush();
    }
    if (this->msg_font_.open) {
        this->msg_font_.text.flush();
    }
    if (this->lbl_font_.open) {
        this->lbl_font_.text.flush();
    }
}

void DcssUi::shutdown()
{
    for (FontSlot *slot : { &this->stat_font_, &this->msg_font_, &this->lbl_font_, &this->tip_font_ }) {
        if (slot->open) {
            slot->text.shutdown();
            slot->open = false;
        }
    }
    this->tabs_.shutdown();
    this->windows_.shutdown();
}

std::vector<std::string> DcssUi::stat_lines() const
{
    return grid_to_lines(this->stat_grid_);
}

std::vector<std::string> DcssUi::message_lines() const
{
    return grid_to_lines(this->msg_grid_);
}

std::string DcssUi::describe() const
{
    char buf[512];
    const DcssLayout &L = this->layout_;
    std::snprintf(buf, sizeof(buf),
        "dcss-ui layout ok=%d small=%d usable=%dx%d reserved=%d view=%dx%d stat=%d,%d %dx%d(%dx%d cells %dpx) msg=%d,%d %dx%d(%dx%d) minimap=%d(%d,%d %dx%d dx=%d) tabs=%d,%d %dx%d fixed=%zu font=%d/%d",
        L.ok ? 1 : 0, L.small_layout ? 1 : 0, this->usable_w_, this->usable_h_, this->reserved_bottom_, L.dungeon_view_w, L.dungeon_view_h,
        L.stat.x, L.stat.y, L.stat.w, L.stat.h, L.stat.cols, L.stat.rows, L.stat.cell_w, L.message.x, L.message.y, L.message.w, L.message.h,
        L.message.cols, L.message.rows, L.has_minimap ? 1 : 0, L.minimap.x, L.minimap.y, L.minimap.w, L.minimap.h, L.minimap.cell_w, L.tabs.x,
        L.tabs.y, L.tabs.w, L.tabs.h, L.fixed_tabs.size(), this->profile_.stat_px, this->profile_.msg_px);
    return buf;
}

} // namespace hd2d::dcss
