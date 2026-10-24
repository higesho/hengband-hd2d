/*!
 * @file dcss_layout.cpp
 * @brief `dcss_layout.h` の実装。原作 `tilesdl.cc` の `do_layout` ほかの移植（手順は冒頭の説明を参照）。
 */
#include "dcss/dcss_layout.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace hd2d::dcss {

namespace {

// 原作 `tilesdl.cc:813-818` の定数。
constexpr int kMapMargin = 2;
constexpr int kMapStatMargin = 4;
constexpr int kMinStatHeight = 13;
constexpr int kMinInvHeight = 4;
constexpr int kMaxInvHeight = 8;
constexpr int kMaxMonHeight = 3;

// 原作 `tilesdl.cc:64-75` の `_screen_sizes`（Android 以外）。幅・高さ・ミニマップ・crt・stat・msg・tip・lbl。
constexpr int kScreenSizes[4][8] = {
    { 1024, 700, 3, 15, 16, 14, 15, 14 }, // 既定
    { 1024, 600, 2, 14, 14, 12, 13, 12 }, // Eee PC 900+
    { 800, 600, 2, 14, 11, 12, 13, 12 }, // 小さい画面
    { 800, 480, 2, 13, 12, 10, 13, 11 }, // Eee PC
};

// 見出しの名前（原作 `GridRegion::name()`。`find_tab` が小文字で比べる）。
constexpr const char *kTabNames[kTabCount] = {
    "inventory", "spells", "memorisation", "abilities", "monsters", "skills", "commands", "system commands", "navigation",
};

int round_up_to_multiple(int a, int b)
{
    const int m = a % b;
    return m == 0 ? a : a + b - m;
}

/*!
 * 原作 `Region`（`tilereg.cc`）の寸法だけを写した物。`text` が真なら `TextRegion`
 * （格子の 1 桁・1 行が字の幅・高さ）、偽なら `dx`・`dy` が 1 マス。
 */
struct Region {
    int ox{ 0 }, oy{ 0 };
    int dx{ 0 }, dy{ 0 };
    int mx{ 0 }, my{ 0 };
    int wx{ 0 }, wy{ 0 };
    int sx{ 0 }, sy{ 0 };
    int ex{ 0 }, ey{ 0 };
    bool text{ false };
    int cw{ 0 }, ch{ 0 };

    static Region make_text(const DcssFontMetrics &f)
    {
        Region r;
        r.text = true;
        r.cw = f.char_w;
        r.ch = f.char_h;
        r.dx = f.char_w;
        r.dy = f.char_h;
        return r;
    }

    int grid_width_to_pixels(int x) const { return text ? x * cw : x * dx; }
    int grid_height_to_pixels(int y) const { return text ? y * ch : y * dy; }

    void calculate_grid_size(int inner_x, int inner_y)
    {
        if (text) {
            mx = cw ? inner_x / cw : 0;
            my = ch ? inner_y / ch : 0;
        } else {
            mx = dx ? inner_x / dx : 0;
            my = dy ? inner_y / dy : 0;
        }
    }

    void recalculate()
    {
        wx = ox * 2 + grid_width_to_pixels(mx);
        wy = oy * 2 + grid_height_to_pixels(my);
        ex = sx + wx;
        ey = sy + wy;
    }

    void resize(int new_mx, int new_my)
    {
        mx = new_mx;
        my = new_my;
        recalculate();
    }

    void place(int new_sx, int new_sy, int margin)
    {
        sx = new_sx;
        sy = new_sy;
        ox = margin;
        oy = margin;
        recalculate();
    }

    void place(int new_sx, int new_sy)
    {
        sx = new_sx;
        sy = new_sy;
        recalculate();
    }

    // 5 引数の `place`: 区画の幅を右下で決め、格子は触らない（ミニマップが使う）。
    void place(int new_sx, int new_sy, int new_ex, int new_ey, int margin)
    {
        sx = new_sx;
        sy = new_sy;
        ex = new_ex;
        ey = new_ey;
        wx = ex - sx;
        wy = ey - sy;
        ox = margin;
        oy = margin;
    }

    void resize_to_fit(int new_wx, int new_wy)
    {
        if (new_wx < 0 || new_wy < 0) {
            mx = wx = my = wy = 0;
            ey = sy;
            ex = sy; // 原作のまま（`ex` に `sy` を入れている。`tilereg.cc:92`）
            return;
        }
        const int inner_x = new_wx - 2 * ox;
        const int inner_y = new_wy - 2 * oy;
        calculate_grid_size(inner_x, inner_y);
        recalculate();
        calculate_grid_size(inner_x, inner_y);
        ex = sx + new_wx;
        ey = sy + new_wy;
    }

    DcssRegion to_output() const
    {
        DcssRegion o;
        o.x = sx;
        o.y = sy;
        o.w = wx;
        o.h = wy;
        o.end_x = ex;
        o.end_y = ey;
        o.margin_x = ox;
        o.margin_y = oy;
        o.cell_w = dx;
        o.cell_h = dy;
        o.cols = mx;
        o.rows = my;
        return o;
    }
};

std::string lowercase(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 原作 `pluralise()` の規則のうち、見出しの名前の照合に要る分（語尾 y → ies、それ以外は s）。
std::string pluralise(const std::string &s)
{
    if (s.size() >= 2 && s.back() == 'y') {
        const char before = s[s.size() - 2];
        if (before != 'a' && before != 'e' && before != 'i' && before != 'o' && before != 'u') {
            return s.substr(0, s.size() - 1) + "ies";
        }
    }
    return s + "s";
}

// 原作 `TabbedRegion::find_tab`。
int find_tab(const std::string &name)
{
    const std::string lower = lowercase(name);
    const std::string plural = pluralise(lower);
    for (int i = 0; i < kTabCount; ++i) {
        if (lower == kTabNames[i] || plural == kTabNames[i]) {
            return i;
        }
    }
    return -1;
}

/*!
 * 原作 `TilesFramework` の配置に関わる状態と手順。`do_layout` 一式をこの中に写した。
 */
class Planner {
public:
    explicit Planner(const DcssLayoutInput &in)
        : in_(in)
        , tab_enabled_(in.tab_enabled)
    {
    }

    DcssLayout run();

private:
    // 原作 `tilesdl.cc` の手順
    bool is_using_small_layout() const;
    void set_tab_small_layout(bool small);
    int min_height_for_items() const;
    void autosize_minimap();
    void place_minimap();
    int calc_tab_lines(int num_elements) const;
    void place_tab(int idx);
    void resize_inventory();
    void layout_statcol();

    const DcssLayoutInput &in_;

    Region tile_;
    Region tab_;
    Region msg_;
    Region stat_;
    Region crt_;
    bool has_map_{ false };
    Region map_;
    std::map<int, Region> fixed_;
    std::vector<int> fixed_order_;
    std::array<bool, kTabCount> tab_enabled_;

    int stat_x_divider_{ 0 };
    int stat_col_{ 0 };
    int statcol_top_{ 0 };
    int statcol_bottom_{ 0 };
    int tab_margin_{ 0 };
    int map_pixels_{ 0 };
};

bool Planner::is_using_small_layout() const
{
    switch (in_.small_layout_mode) {
    case DcssSmallLayoutMode::Auto:
        return in_.window_w < in_.fonts.stat.char_w * 45 + in_.fonts.msg.char_w * 55;
    case DcssSmallLayoutMode::On:
        return true;
    case DcssSmallLayoutMode::Off:
    default:
        return false;
    }
}

// 原作 `TabbedRegion::set_small_layout` の寸法に効く部分。
void Planner::set_tab_small_layout(bool small)
{
    if (small) {
        tab_.ox = (in_.show_tab_icons || in_.tab_column_always) ? in_.tab_icon_w : 0;
        // 原作 `tilereg-tab.cc:447`: 左から順に整数で計算してから 2 で割る。歯車（`extra_tab_headings`）は見出し 1 つとして数に入れる。
        int scale = (in_.tile_sidebar_pixels - 1) * in_.window_h / (kTabCount + in_.extra_tab_headings) / in_.tab_icon_h - 1;
        scale /= 2;
        tab_.dx = scale;
        tab_.dy = scale;
    } else {
        tab_.ox = in_.tab_icon_w;
        tab_.dx = in_.tile_sidebar_pixels;
        tab_.dy = in_.tile_sidebar_pixels;
    }
}

// 原作 `TabbedRegion::min_height_for_items`: 有効な見出しの最後の `max_y`。見出しは (高さ + 1) 画素ずつ積む。
int Planner::min_height_for_items() const
{
    int enabled = 0;
    for (const bool e : tab_enabled_) {
        if (e) {
            ++enabled;
        }
    }
    if (enabled == 0) {
        return 0;
    }
    return enabled * (in_.tab_icon_h + 1) - 1;
}

void Planner::autosize_minimap()
{
    const int horiz = (in_.window_w - stat_col_ - kMapMargin * 2) / kMapGridCols;
    const int vert = (statcol_bottom_ - (map_.sy ? map_.sy : statcol_top_) - kMapMargin * 2) / kMapGridRows;
    int sz = std::min(horiz, vert);
    if (in_.tile_map_pixels) {
        sz = std::min(sz, in_.tile_map_pixels);
    }
    map_.dx = map_.dy = sz;
}

void Planner::place_minimap()
{
    map_ = Region{};
    map_.dx = map_.dy = map_pixels_;
    has_map_ = true;

    if (kMapGridCols * map_.dx > in_.window_w - stat_col_) {
        autosize_minimap();
    }
    map_.resize(kMapGridCols, kMapGridRows);
    if (map_.dy * kMapGridRows > statcol_bottom_ - statcol_top_) {
        has_map_ = false;
        map_ = Region{};
        return;
    }
    map_.place(stat_col_, stat_.ey, kMapMargin);
    statcol_top_ = map_.ey;
}

int Planner::calc_tab_lines(int num_elements) const
{
    // 原作は `tab_.mx` が 0 だと 0 除算になる。幅が足りない窓は最後に不正な配置として返すので、ここは落ちないようにするだけ。
    return (num_elements - 1) / std::max(1, tab_.mx) + 1;
}

void Planner::place_tab(int idx)
{
    if (idx == -1) {
        return;
    }

    const bool zero = in_.unassigned_tab_ids_are_zero;
    const int id_monster = zero ? 0 : kTabMonster;
    const int id_command2 = zero ? 0 : kTabSystemCommand;
    const int id_skill = zero ? 0 : kTabSkill;

    int min_ln = 1;
    int max_ln = 1;
    if (idx == kTabSpell) {
        if (in_.spell_count == 0) {
            tab_enabled_[kTabSpell] = true;
            return;
        }
        max_ln = calc_tab_lines(in_.spell_count);
    } else if (idx == kTabAbility) {
        if (in_.ability_count == 0) {
            tab_enabled_[kTabAbility] = true;
            return;
        }
        max_ln = calc_tab_lines(in_.ability_count);
    } else if (idx == id_monster) {
        max_ln = kMaxMonHeight;
    } else if (idx == kTabCommand) {
        min_ln = max_ln = calc_tab_lines(in_.action_command_count);
    } else if (idx == id_command2) {
        min_ln = max_ln = calc_tab_lines(in_.system_command_count);
    } else if (idx == kTabNavigation) {
        min_ln = max_ln = calc_tab_lines(in_.map_command_count);
    } else if (idx == id_skill) {
        min_ln = max_ln = calc_tab_lines(in_.skill_count);
    }

    const int lines = std::min(max_ln, (statcol_bottom_ - statcol_top_ - tab_margin_) / tab_.dy);
    if (lines >= min_ln) {
        Region fixed;
        fixed.dx = in_.tile_sidebar_pixels;
        fixed.dy = in_.tile_sidebar_pixels;
        fixed.ox = in_.tab_icon_w; // `push_tab_region` が見出しの絵の幅を余白にする
        fixed.place(stat_col_, statcol_bottom_ - lines * tab_.dy);
        fixed.resize(tab_.mx, lines);
        statcol_bottom_ = fixed.sy - tab_margin_;
        if (fixed_.find(idx) == fixed_.end()) {
            fixed_order_.push_back(idx);
        }
        fixed_[idx] = fixed;
        tab_enabled_[idx] = false;
    } else {
        tab_enabled_[idx] = true;
    }
}

void Planner::resize_inventory()
{
    const int lines = std::min(kMaxInvHeight - kMinInvHeight, (statcol_bottom_ - statcol_top_) / tab_.dy);

    const int prev_size = tab_.wy;

    int tabs_height = min_height_for_items();
    tabs_height = round_up_to_multiple(tabs_height, tab_.dx) / tab_.dx;

    tab_.resize(tab_.mx, std::max(kMinInvHeight + lines, tabs_height));
    tab_.place(stat_col_, in_.window_h - tab_.wy);

    const int delta_y = tab_.wy - prev_size;
    for (auto &entry : fixed_) {
        entry.second.place(entry.second.sx, entry.second.sy - delta_y);
    }

    statcol_bottom_ -= delta_y;
}

void Planner::layout_statcol()
{
    const bool use_small_layout = is_using_small_layout();

    fixed_.clear();
    fixed_order_.clear();
    has_map_ = false;
    map_ = Region{};

    if (use_small_layout) {
        // 命令の見出しは右端、ステータスは地図と見出しの間に詰める。
        statcol_top_ = 0;
        statcol_bottom_ = in_.window_h;
        stat_.resize_to_fit((in_.window_w - stat_x_divider_) - tab_.ox * tab_.dx / 32, statcol_bottom_ - statcol_top_);
        return;
    }

    stat_.resize(stat_.mx, kMinStatHeight);

    statcol_top_ = stat_.ey;

    // 持ち物の欄を最小の大きさにする。
    set_tab_small_layout(false);
    tab_.place(stat_col_, statcol_top_);
    tab_.resize_to_fit(in_.window_w - tab_.sx, in_.window_h - tab_.sy);
    // 区画は窓の右端より半マスほど外へはみ出す（描かれる範囲は右端に接する）。
    tab_.resize(tab_.mx + 1, kMinInvHeight);
    tab_.place(stat_col_, in_.window_h - tab_.wy);
    statcol_bottom_ = tab_.sy - tab_margin_;

    stat_.resize(stat_.mx, kMinStatHeight);
    statcol_top_ += stat_.dy;
    bool resized_inventory = false;

    for (const std::string &str : in_.layout_priority) {
        if (str == "inventory") {
            resize_inventory();
            resized_inventory = true;
        } else if (str == "minimap" || str == "map") {
            if (!resized_inventory) {
                resize_inventory();
                resized_inventory = true;
            }
            place_minimap();
        } else if (!(str == "gold_turn" || str == "gold_turns")) {
            place_tab(find_tab(str));
        }
    }
    // ミニマップを余った空間の中央になるよう引き伸ばす。
    if (has_map_) {
        autosize_minimap();
        map_.place(stat_.sx, stat_.ey, stat_.ex, statcol_bottom_, kMapMargin);
    }
}

DcssLayout Planner::run()
{
    DcssLayout out;

    const DcssFontSet &f = in_.fonts;
    const DcssFontMetrics *all[] = { &f.crt, &f.msg, &f.stat, &f.lbl }; // ツールチップの字は配置に使わない
    for (const DcssFontMetrics *m : all) {
        if (m->char_w <= 0 || m->char_h <= 0) {
            out.error = "font metrics must be positive";
            return out;
        }
    }
    if (f.lbl.size_px <= 0 || in_.window_w <= 0 || in_.window_h <= 0 || in_.tile_cell_pixels <= 0 || in_.tile_sidebar_pixels <= 0
        || in_.tab_icon_w < 0 || in_.tab_icon_h <= 0 || in_.msg_min_height <= 0 || in_.msg_max_height <= 0) {
        out.error = "invalid layout input";
        return out;
    }
    const int scale_pct = in_.map_mode ? in_.tile_map_scale_pct : in_.tile_viewport_scale_pct;
    if (scale_pct <= 0) {
        out.error = "scale must be positive";
        return out;
    }
    const int cell = in_.tile_cell_pixels * scale_pct / 100;
    if (cell <= 0) {
        out.error = "tile size is zero";
        return out;
    }

    const int win_w = in_.window_w;
    const int win_h = in_.window_h;

    tile_ = Region{};
    tile_.dx = tile_.dy = cell;
    tab_ = Region{};
    set_tab_small_layout(false); // 登録時の余白（見出しの絵の幅）と 1 マスの大きさ
    msg_ = Region::make_text(f.msg);
    stat_ = Region::make_text(f.stat);
    crt_ = Region::make_text(f.crt);
    tab_margin_ = f.lbl.size_px + 4;
    map_pixels_ = in_.tile_map_pixels ? in_.tile_map_pixels : dcss_screen_profile(win_w, win_h).map_px;

    int message_y_divider = 0;
    int sidebar_pw = 0;

    const bool use_small_layout = is_using_small_layout();
    bool message_overlay = in_.force_overlay;

    const int min_msg_h = msg_.grid_height_to_pixels(in_.msg_min_height);
    const int max_tile_h = tile_.grid_height_to_pixels(std::max(in_.view_max_height, kLosDiameter));

    if (use_small_layout) {
        // ダンジョンは窓の高さいっぱい、見出しは右端に細く立て、ステータスは 9 桁に詰める。
        int available_height_in_tiles = win_h / tile_.dy;
        if (available_height_in_tiles < kLosDiameter) {
            tile_.dy = win_h / kLosDiameter;
            tile_.dx = tile_.dy;
            available_height_in_tiles = kLosDiameter;
            if (tile_.dy <= 0) {
                out.error = "window too small";
                return out;
            }
        }

        set_tab_small_layout(true);
        tab_.resize_to_fit(win_w, win_h);
        sidebar_pw = tab_.grid_width_to_pixels(tab_.ox) / 32 + stat_.grid_width_to_pixels(9);
        stat_x_divider_ = win_w - sidebar_pw;
    } else {
        set_tab_small_layout(false);
        tab_.resize_to_fit(win_w, win_h);

        const int sidebar_min_pw = stat_.grid_width_to_pixels(in_.tile_min_stat_width_characters);
        sidebar_pw = tab_.grid_width_to_pixels(14) - 10;
        if (sidebar_pw > win_w / 3) {
            sidebar_pw = tab_.grid_width_to_pixels(7) - 10;
        }
        while (sidebar_pw < sidebar_min_pw) {
            sidebar_pw += tab_.grid_width_to_pixels(1);
        }

        stat_x_divider_ = win_w - sidebar_pw - kMapStatMargin;
    }

    if (max_tile_h + min_msg_h <= win_h && !message_overlay) {
        message_y_divider = max_tile_h;
        message_y_divider = std::max(message_y_divider, win_h - msg_.grid_height_to_pixels(in_.msg_max_height));
    } else {
        const int available_height_in_tiles = (win_h - (message_overlay ? 0 : min_msg_h)) / tile_.dy;

        // 視野が収まらなければ、メッセージを重ね表示にする。
        if (available_height_in_tiles < kLosDiameter) {
            message_y_divider = win_h;
            message_overlay = true;

            // それでも収まらなければタイルを縮める。
            if (win_h / tile_.dy < kLosDiameter) {
                tile_.dy = win_h / kLosDiameter;
                tile_.dx = tile_.dy;
                if (tile_.dy <= 0) {
                    out.error = "window too small";
                    return out;
                }
            }
        } else {
            message_y_divider = win_h - min_msg_h;
        }
    }

    if (message_overlay) {
        message_y_divider = win_h;
    }

    // メッセージは窓の下端に貼りつける。行の高さの倍数に切り詰める。
    int msg_height = win_h - message_y_divider;
    msg_height = msg_height / msg_.dy * msg_.dy;
    message_y_divider = win_h - msg_height;

    // ダンジョンの区画は窓より大きく取り、自キャラが中央に来るようずらして置く。
    const int tile_iw = stat_x_divider_;
    const int tile_ih = message_y_divider;
    const int tile_ow = round_up_to_multiple(tile_iw, tile_.dx * 2) + tile_.dx;
    const int tile_oh = round_up_to_multiple(tile_ih, tile_.dy * 2) + tile_.dx; // 原作のまま dx（正方形なので同じ）
    tile_.resize_to_fit(tile_ow, tile_oh);
    tile_.place(-(tile_ow - tile_iw) / 2, -(tile_oh - tile_ih) / 2, 0);

    // メッセージ。
    if (message_overlay) {
        msg_.place(0, 0, 0);
        msg_.resize_to_fit(stat_x_divider_, min_msg_h);
    } else {
        msg_.resize_to_fit(stat_x_divider_, win_h - message_y_divider);
        msg_.place(0, tile_ih, 0);
    }

    if (use_small_layout) {
        stat_col_ = stat_x_divider_;

        // 見出しは地図の上に重ね、右へ大きくずらして置く。
        set_tab_small_layout(true);
        tab_.resize_to_fit(stat_x_divider_ + tab_.ox * 2, message_y_divider - tab_margin_);
        tab_.place(win_w - tab_.ox, 0);
    } else {
        stat_col_ = stat_x_divider_ + kMapStatMargin;
    }
    stat_.resize_to_fit(sidebar_pw, win_h);
    stat_.place(stat_col_, 0, 0);
    stat_.resize(stat_.mx, kMinStatHeight);

    layout_statcol();

    crt_.place(0, 0, 0);
    crt_.resize_to_fit(win_w, win_h);

    if (tile_.mx == 0 || tile_.my == 0 || msg_.my == 0) {
        out.error = "Failed to find a valid window layout: screen too small or game_scale too large?";
        return out;
    }
    if (!use_small_layout && tab_.mx <= 0) {
        // 原作は見出しの欄の列数が 0 になると 0 除算で落ちる。窓が狭すぎる。
        out.error = "tab region too narrow";
        return out;
    }

    out.ok = true;
    out.small_layout = use_small_layout;
    out.message_overlay = message_overlay;
    out.overlay_alpha = 255 * in_.overlay_alpha_percent / 100;
    out.sidebar_width = sidebar_pw;
    out.stat_x_divider = stat_x_divider_;
    out.stat_column_x = stat_col_;
    out.message_y_divider = message_y_divider;
    out.statcol_top = statcol_top_;
    out.statcol_bottom = statcol_bottom_;
    out.dungeon = tile_.to_output();
    out.dungeon_view_w = tile_iw;
    out.dungeon_view_h = tile_ih;
    out.message = msg_.to_output();
    out.stat = stat_.to_output();
    out.has_minimap = has_map_;
    if (has_map_) {
        out.minimap = map_.to_output();
    }
    out.tabs = tab_.to_output();
    for (const int idx : fixed_order_) {
        DcssFixedTab ft;
        ft.tab_index = idx;
        ft.region = fixed_[idx].to_output();
        out.fixed_tabs.push_back(ft);
    }
    out.crt = crt_.to_output();
    out.tab_enabled = tab_enabled_;
    return out;
}

} // namespace

DcssScreenProfile dcss_screen_profile(int window_w, int window_h)
{
    constexpr int kRows = static_cast<int>(sizeof(kScreenSizes) / sizeof(kScreenSizes[0]));
    int row = 0;
    do {
        if (window_w >= kScreenSizes[row][0] && window_h >= kScreenSizes[row][1]) {
            break;
        }
    } while (++row < kRows - 1);

    DcssScreenProfile p;
    p.map_px = kScreenSizes[row][2];
    p.crt_px = kScreenSizes[row][3];
    p.stat_px = kScreenSizes[row][4];
    p.msg_px = kScreenSizes[row][5];
    p.tip_px = kScreenSizes[row][6];
    p.lbl_px = kScreenSizes[row][7];
    return p;
}

DcssAndroidProfile dcss_android_profile(int display_w, int display_h)
{
    // 原作 `tilesdl.cc:77-88`。幅・高さ・ミニマップ・crt・stat・msg・tip・lbl・ダンジョンの拡大率（%）・ミニマップの拡大率（%）。
    static constexpr int kAndroidSizes[5][10] = {
        { 800, 800, 1, 15, 17, 17, 15, 15, 148, 74 },
        { 720, 720, 1, 13, 15, 15, 13, 13, 132, 66 },
        { 640, 640, 1, 12, 14, 14, 12, 12, 120, 60 },
        { 540, 540, 1, 10, 12, 12, 10, 10, 100, 50 },
        { 480, 480, 1, 9, 11, 11, 9, 9, 90, 45 },
    };
    constexpr int kRows = 5;
    const int ref = std::max(1, std::min(display_w, display_h));
    DcssAndroidProfile out;
    out.game_scale = ref / 1080 + 1;
    const int adjust = (out.game_scale == ref / 960 + 1) ? out.game_scale : 1;
    int row = 0;
    do {
        if (ref >= kAndroidSizes[row][0] * adjust) {
            break;
        }
    } while (++row < kRows - 1);
    out.fonts.map_px = kAndroidSizes[row][2];
    out.fonts.crt_px = kAndroidSizes[row][3];
    out.fonts.stat_px = kAndroidSizes[row][4];
    out.fonts.msg_px = kAndroidSizes[row][5];
    out.fonts.tip_px = kAndroidSizes[row][6];
    out.fonts.lbl_px = kAndroidSizes[row][7];
    out.viewport_scale_pct = kAndroidSizes[row][8];
    out.map_scale_pct = kAndroidSizes[row][9];
    return out;
}

DcssLayout DcssLayout::compute(const DcssLayoutInput &in)
{
    Planner planner(in);
    return planner.run();
}

} // namespace hd2d::dcss
