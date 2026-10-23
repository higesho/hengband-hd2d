/*!
 * @file dcss_stat.cpp
 * @brief `dcss_stat.h` の実装（原作 `output.cc` のステータス欄の再現）。
 */
#include "dcss/dcss_stat.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace hd2d::dcss {

namespace {

constexpr int kHudValue = kLightGrey;
constexpr int kNumPietyStars = 6;

//! 英語の見出し（原作のキー）と日本語の訳（`jtrans/output.txt`。前後の空白は辞書のキーに含まれない）。
struct Caption {
    const char *english;
    const char *japanese;
};

constexpr Caption kCaptions[] = {
    { "Health: ", "体力:" },
    { "Magic:  ", "魔力:" },
    { "EV:", "回避:" },
    { "SH:", "盾:" },
    { "Str:", "腕力:" },
    { "Int:", "知力:" },
    { "Dex:", "器用:" },
    { "XL: ", "Lv:" },
    { "Next: ", "次Lv:" },
    { "Place: ", "場所:" },
    { "Noise: ", "騒音:" },
    { "Time:", "時間:" },
    { "Turn:", "ターン:" },
    { "Gold:", "所持金:" },
    { "Doom: ", "破滅:" },
    { "Contam: ", "汚染:" },
};

//! 原作 `_hud_caption(en, width)`。日本語なら訳を「英語の見出しの表示幅」（または `width`）に詰める。
std::string hud_caption(const char *en, bool ja, int width = -1)
{
    std::string text = en;
    if (ja) {
        for (const Caption &c : kCaptions) {
            if (std::strcmp(c.english, en) == 0) {
                text = c.japanese;
                break;
            }
        }
    }
    return dcss_chop_string(text, (width < 0) ? dcss_text_width(en) : width);
}

//! 辞書のキーは前後の空白を除いたもの（`jtrans(en)` の戻り）。
std::string jtrans_caption(const char *en, bool ja)
{
    if (ja) {
        for (const Caption &c : kCaptions) {
            if (std::strcmp(c.english, en) == 0) {
                return c.japanese;
            }
        }
    }
    std::string s = en;
    while (!s.empty() && (s.back() == ' ')) {
        s.pop_back();
    }
    return s;
}

int colour_from_threshold(const std::vector<std::pair<int, int>> &table, int percent, int fallback)
{
    int colour = fallback;
    for (const auto &entry : table) {
        if (percent <= entry.first) {
            colour = entry.second;
        }
    }
    return colour;
}

int count_digits(int v)
{
    if (v > 999) {
        return 4;
    }
    if (v > 99) {
        return 3;
    }
    if (v > 9) {
        return 2;
    }
    return 1;
}

int colour_from_stat_mod(int mod)
{
    if (mod == 0) {
        return kHudValue;
    }
    return (mod < 0) ? kRed : kLightBlue;
}

int stat_colour(const DcssUiModel &model, int value)
{
    if (value <= 0) {
        return kLightRed;
    }
    for (const auto &entry : model.options().stat_colour) {
        if (value <= entry.first) {
            return entry.second;
        }
    }
    return kHudValue;
}

std::string format_int(int v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", v);
    return buf;
}

std::string pad_left(const std::string &s, int width)
{
    std::string out = s;
    while (dcss_text_width(out) < width) {
        out.insert(out.begin(), ' ');
    }
    return out;
}

std::string pad_right(const std::string &s, int width)
{
    std::string out = s;
    while (dcss_text_width(out) < width) {
        out.push_back(' ');
    }
    return out;
}

std::string game_time_text(const DcssUiModel &model, bool with_delta)
{
    const DcssPlayer &p = model.player();
    char buf[64];
    if (model.options().show_game_time) {
        std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(p.time) / 10.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%d", p.turn);
    }
    std::string s = buf;
    if (with_delta) {
        std::snprintf(buf, sizeof(buf), " (%.1f)", static_cast<double>(std::max(0, p.time - model.time_at_prev_input())) / 10.0);
        s += buf;
    }
    return s;
}

const DcssInvItem *item_at(const DcssPlayer &p, int index)
{
    const auto it = p.inv.find(index);
    return (it == p.inv.end()) ? nullptr : &it->second;
}

//! 武器の名前の色。`DcssInvItem::colour` は原作のメニューの色（-1 なら既定）。
int weapon_colour(const DcssInvItem &item)
{
    return dcss_item_colour(item.colour);
}

//! 状態の灯。小さい配置は 1 字に詰める。
void print_status_lights_normal(const DcssPlayer &p, DcssTextGrid &g, int cols, int rows, int y)
{
    if (p.status.empty()) {
        return;
    }
    int line_cur = y;
    const int line_end = rows + 1;
    g.goto_xy(1, line_cur);
    std::size_t i = 0;
    for (int guard = 0; guard < 4096; ++guard) {
        const int end_x = (g.where_x() - 1) + ((i < p.status.size()) ? dcss_text_width(p.status[i].light) : 10000);
        if (end_x <= cols) {
            g.textcolour(p.status[i].colour);
            g.putstr(p.status[i].light);
            if (end_x < cols) {
                g.putstr(" ");
            }
            ++i;
        } else {
            g.clear_to_end_of_line();
            ++line_cur;
            if (line_cur == line_end) {
                break;
            }
            g.goto_xy(1, line_cur);
        }
    }
}

std::string first_char(const std::string &s)
{
    const std::vector<char32_t> chars = dcss_decode_utf8(s);
    return chars.empty() ? std::string() : dcss_encode_utf8(chars[0]);
}

void print_status_lights_small(const DcssPlayer &p, DcssTextGrid &g, int cols, int row)
{
    if (p.status.empty()) {
        return;
    }
    g.goto_xy(1, row);
    if (p.status.size() == 1) {
        g.textcolour(p.status[0].colour);
        g.putstr(p.status[0].light);
        return;
    }
    for (std::size_t i = 0; (i < p.status.size()) && (static_cast<int>(i) < cols - 1); ++i) {
        g.textcolour(p.status[i].colour);
        if ((i == p.status.size() - 1) && (dcss_text_width(p.status[i].light) < cols - g.where_x() + 1)) {
            g.putstr(p.status[i].light);
        } else if (static_cast<int>(p.status.size()) > (cols / 2)) {
            g.putstr(first_char(p.status[i].light));
        } else {
            g.putstr(first_char(p.status[i].light) + " ");
        }
    }
}

std::string god_asterisks(const DcssPlayer &p, bool leading_space)
{
    // 信仰の星。無神は呼ばない（モンクの "**...." は呼び出し側で作る）。
    std::string str;
    const int prank = std::clamp(p.piety_rank, 0, kNumPietyStars);
    if (p.god == "Xom") {
        const int p_rank = p.piety_rank;
        if (p_rank >= 0) {
            str = std::string(static_cast<std::size_t>(p_rank), '.') + "*" + std::string(static_cast<std::size_t>(std::max(0, kNumPietyStars - 1 - p_rank)), '.');
        } else {
            str = std::string(kNumPietyStars, '.');
        }
    } else {
        const int pips = p.ostracism_pips;
        if (pips > 0) {
            str = std::string(static_cast<std::size_t>(prank), '*') + std::string(static_cast<std::size_t>(std::max(0, kNumPietyStars - prank - pips)), '.')
                + "<lightmagenta>" + std::string(static_cast<std::size_t>(pips), 'X') + "</lightmagenta>";
        } else {
            str = std::string(static_cast<std::size_t>(prank), '*') + std::string(static_cast<std::size_t>(kNumPietyStars - prank), '.');
        }
    }
    return (leading_space ? " " : "") + str;
}

int god_status_colour(const DcssPlayer &p, int default_colour)
{
    return (p.penance != 0) ? kRed : default_colour;
}

} // namespace

int dcss_item_colour(int colour)
{
    return ((colour >= 0) && (colour < kColourCount)) ? colour : kLightGrey;
}

// ---------------------------------------------------------------- 棒

void DcssColourBar::draw(DcssTextGrid &grid, int cols, int ox, int oy, int val, int max_val, int sub_val, int turn)
{
    if (max_val <= 0) {
        this->old_disp = -1;
        return;
    }
    val = std::clamp(val, 0, max_val);
    const int width = (this->horiz_bar_width != -1) ? this->horiz_bar_width : (cols - (ox - 1));
    const int sub_disp = (width * val) / max_val;
    int disp = (width * std::max(0, val - sub_val)) / max_val;
    const int old = (this->old_disp < 0) ? sub_disp : this->old_disp;
    this->old_disp = sub_disp;
    if (sub_val != 0) {
        disp = std::max(0, std::min(sub_disp - 1, disp));
    }
    grid.goto_xy(ox, oy);
    for (int cx = 0; cx < width; ++cx) {
        grid.textcolour(kBlack + (this->m_empty * 16));
        if ((cx < disp) && (cx < old)) {
            grid.textcolour(kBlack + (this->m_default * 16));
        } else if (cx < disp) {
            grid.textcolour(kBlack + (this->m_change_pos * 16));
        } else if (cx < sub_disp) {
            grid.textcolour(kBlack + (kYellow * 16));
        } else if ((old >= sub_disp) && (cx < old)) {
            grid.textcolour(kBlack + (this->m_change_neg * 16));
        }
        grid.putwch(U' ');
    }
    this->request_redraw_after = (old != disp) ? (turn + 4) : 0;
    grid.textcolour(kLightGrey);
    grid.goto_xy(ox, oy);
}

void DcssColourBar::vdraw(DcssTextGrid &grid, int ox, int oy, int obase, int val, int max_val, int turn)
{
    constexpr int kBarWidth = 3;
    if (max_val <= 0) {
        this->old_disp = -1;
        return;
    }
    val = std::clamp(val, 0, max_val);
    const int height = kBarWidth * (obase - oy + 1);
    if (height <= 0) {
        return;
    }
    const int disp = (height * val) / max_val;
    const int old = (this->old_disp < 0) ? disp : this->old_disp;
    grid.goto_xy(ox, obase);
    for (int cx = 0; cx < height; ++cx) {
        grid.textcolour(kBlack + (this->m_empty * 16));
        if (cx < disp) {
            grid.textcolour(kBlack + (this->m_default * 16));
        } else if ((old > disp) && (cx < old)) {
            grid.textcolour(kBlack + (this->m_change_neg * 16));
        }
        grid.putwch(U' ');
        if (((cx + 1) % kBarWidth) == 0) {
            grid.goto_xy(ox, obase - (cx / kBarWidth));
        }
        this->request_redraw_after = (old != disp) ? (turn + 4) : 0;
    }
    // 原作の `vdraw` は直前の長さ（`m_old_disp`）を更新しない（そのため変化の色は出ない）。そのまま写す。
    grid.textcolour(kLightGrey);
}

// ---------------------------------------------------------------- 組み立て

int DcssStatPanel::redraw_after_turn() const
{
    int t = 0;
    for (const DcssColourBar *bar : { &this->hp_bar_, &this->mp_bar_, &this->noise_bar_ }) {
        if (bar->request_redraw_after != 0) {
            t = (t == 0) ? bar->request_redraw_after : std::min(t, bar->request_redraw_after);
        }
    }
    return t;
}

void DcssStatPanel::reset_bars()
{
    this->hp_bar_.reset();
    this->mp_bar_.reset();
    this->noise_bar_.reset();
}

void DcssStatPanel::build(const DcssUiModel &model, int cols, int rows, bool small_layout, bool japanese, DcssTextGrid &grid)
{
    grid.resize(cols, rows);
    if (!model.player().seen || (cols <= 0) || (rows <= 0)) {
        return;
    }
    if (small_layout) {
        this->build_small(model, cols, rows, japanese, grid);
    } else {
        this->build_normal(model, cols, rows, japanese, grid);
    }
}

void DcssStatPanel::build_normal(const DcssUiModel &model, int cols, int rows, bool ja, DcssTextGrid &g)
{
    const DcssPlayer &p = model.player();
    const DcssOptions &opt = model.options();
    const int caption_colour = opt.status_caption_colour;
    const int turn = p.turn;
    const int width = cols;

    // --- 1 行目: 称号『名前』 ---
    {
        std::string title;
        if (ja) {
            title = p.title + "『" + p.name + "』";
            const int over = dcss_text_width(title) - width;
            if (over > 0) {
                const int name_len = dcss_text_width(p.name);
                if ((name_len - 8) > over) {
                    title = p.title + "『" + dcss_chop_string(p.name, name_len - over - 1, false) + "』";
                }
            }
        } else {
            title = p.name + " " + p.title;
        }
        g.goto_xy(1, 1);
        g.textcolour(kYellow);
        g.putstr(dcss_chop_string(title, width));
        const char *flag = (p.wizard != 0) ? "WIZARD" : ((p.explore != 0) ? "EXPLORE" : nullptr);
        if (flag != nullptr) {
            g.textcolour(kLightMagenta);
            g.goto_xy(1 + width - (3 + static_cast<int>(std::strlen(flag))), 1);
            g.putstr(std::string(" *") + flag + "*");
        }
    }

    // --- 2 行目: 種族 [神] [信仰] ---
    {
        g.textcolour(kYellow);
        g.goto_xy(1, 2);
        const std::string species = ja ? dcss_species_name_ja(p.species) : p.species;
        g.putstr(species);
        if (p.god.empty()) {
            if (p.piety_rank == 2) {
                const std::string godpiety = "**....";
                g.textcolour(kDarkGrey);
                if ((dcss_text_width(species) + dcss_text_width(godpiety) + 1) <= width) {
                    g.putstr(" " + godpiety);
                }
            }
        } else {
            const std::string god_name = ja ? dcss_god_name_ja(p.god) : p.god;
            const std::string god = " " + (ja ? (god_name + "の信徒") : ("of " + god_name));
            g.putstr(god);
            const DcssStyledLine piety = dcss_parse_tagged(god_asterisks(p, true), god_status_colour(p, kYellow));
            const int textwidth = dcss_text_width(species) + dcss_text_width(god) + dcss_line_width(piety) + 1;
            g.textcolour(god_status_colour(p, kYellow));
            if (textwidth <= width) {
                g.put_styled(piety);
            }
        }
    }

    // --- 見出し（原作 draw_border）---
    g.textcolour(caption_colour);
    g.goto_xy(1, 5);
    g.putstr("AC:");
    g.goto_xy(1, 6);
    g.putstr(hud_caption("EV:", ja, 5));
    g.goto_xy(1, 7);
    g.putstr(hud_caption("SH:", ja, 5));
    g.goto_xy(19, 5);
    g.putstr(hud_caption("Str:", ja, 5));
    g.goto_xy(19, 6);
    g.putstr(hud_caption("Int:", ja, 5));
    g.goto_xy(19, 7);
    g.putstr(hud_caption("Dex:", ja, 5));
    const std::string time_caption = jtrans_caption(opt.show_game_time ? "Time:" : "Turn:", ja);
    g.goto_xy(19, 9);
    g.putstr(time_caption);

    // --- 3 行目: 体力 ---
    {
        const int max_max_hp = std::max(p.real_hp_max, p.hp_max);
        const int hp_percent = (p.hp_max > 0) ? ((p.hp * 100) / p.hp_max) : 100;
        const int hp_colour = colour_from_threshold(opt.hp_colour, hp_percent, kHudValue);
        g.goto_xy(1, 3);
        g.textcolour(caption_colour);
        g.putstr(hud_caption("Health: ", ja));
        g.textcolour(hp_colour);
        g.putstr(format_int(p.hp));
        g.textcolour(kHudValue);
        g.putstr("/" + format_int(p.hp_max));
        if (max_max_hp != p.hp_max) {
            g.putstr(" (" + format_int(max_max_hp) + ")");
        }
        const int col = g.where_x() - 1;
        for (int i = 18 - col; i > 0; --i) {
            g.putstr(" ");
        }
        this->hp_bar_.draw(g, width, 19, 3, p.hp, p.hp_max, p.hp - std::max(0, p.poison_survival), turn);
    }

    // --- 4 行目: 魔力 ---
    {
        g.goto_xy(1, 4);
        const int mp_percent = (p.mp_max == 0) ? 100 : ((p.mp * 100) / p.mp_max);
        const int mp_colour = colour_from_threshold(opt.mp_colour, mp_percent, kHudValue);
        g.textcolour(caption_colour);
        g.putstr(hud_caption("Magic:  ", ja));
        g.textcolour(mp_colour);
        g.putstr(format_int(p.mp));
        g.textcolour(kHudValue);
        g.putstr("/" + format_int(p.mp_max));
        int col = count_digits(p.mp) + count_digits(p.mp_max) + 1;
        if ((p.species == "Deep Dwarf") && (p.dd_real_mp_max != 0) && (p.dd_real_mp_max != p.mp_max)) {
            g.putstr(" (" + format_int(p.dd_real_mp_max) + ")");
            col += count_digits(p.dd_real_mp_max) + 3;
        }
        for (int i = 11 - col; i > 0; --i) {
            g.putstr(" ");
        }
        this->mp_bar_.draw(g, width, 19, 4, p.mp, p.mp_max, 0, turn);
    }

    // --- 5〜7 行目: AC・EV・SH と 腕力・知力・器用 ---
    {
        g.textcolour(colour_from_stat_mod(p.ac_mod));
        g.goto_xy(1 + 6, 5);
        g.putstr(pad_right(pad_left(format_int(p.ac), 2) + " ", 12));
        g.textcolour(colour_from_stat_mod(p.sh_mod));
        g.goto_xy(1 + 6, 7);
        g.putstr(pad_right(pad_left(format_int(p.sh), 2) + " ", 12));
        g.textcolour(colour_from_stat_mod(p.ev_mod));
        g.goto_xy(1 + 6, 6);
        g.putstr(pad_left(format_int(p.ev), 2) + " ");

        const int stats[3] = { p.str, p.intel, p.dex };
        for (int i = 0; i < 3; ++i) {
            g.goto_xy(19 + 6, 5 + i);
            g.textcolour(stat_colour(model, stats[i]));
            g.putstr(format_int(stats[i]) + "   ");
        }
    }

    // --- 破滅・汚染 ---
    if ((p.doom != 0) || opt.always_show_doom_contam) {
        g.goto_xy(32, 5);
        g.textcolour(caption_colour);
        g.putstr(hud_caption("Doom: ", ja));
        g.textcolour((p.doom >= 75) ? kLightMagenta : (p.doom >= 50) ? kLightRed : (p.doom >= 25) ? kYellow : (p.doom > 0) ? kLightGrey : kDarkGrey);
        g.putstr(format_int(p.doom) + "% ");
    }
    if ((p.contam != 0) || opt.always_show_doom_contam) {
        const int contam = std::max((p.contam > 0) ? 1 : 0, p.contam);
        g.goto_xy(30, 6);
        g.textcolour(caption_colour);
        g.putstr(hud_caption("Contam: ", ja));
        g.textcolour((contam >= 200) ? kRed : (contam >= 100) ? kYellow : kDarkGrey);
        g.putstr(format_int(contam) + "% ");
    }

    // --- 8 行目: XL と 場所 ---
    {
        g.goto_xy(1, 8);
        g.textcolour(caption_colour);
        g.putstr(hud_caption("XL: ", ja));
        g.textcolour(kHudValue);
        g.putstr(pad_left(format_int(p.xl), 2) + " ");
        if (p.xl >= 27) {
            g.putstr(std::string(10, ' '));
        } else {
            g.textcolour(caption_colour);
            g.putstr(hud_caption("Next: ", ja));
            g.textcolour(kHudValue);
            g.putstr(pad_left(format_int(p.progress), 2) + "% ");
        }

        g.goto_xy(19, 8);
        g.textcolour(caption_colour);
        g.putstr(hud_caption("Place: ", ja));
        g.textcolour(kHudValue);
        std::string place = ja ? dcss_branch_name_ja(p.place) : p.place;
        if (p.depth > 0) {
            place += ":" + format_int(p.depth);
        }
        const int place_width = width - (g.where_x() - 1);
        g.putstr(dcss_chop_string(place, std::max(0, place_width), false));
        g.clear_to_end_of_line();
    }

    // --- 9 行目: 騒音 と 時間 ---
    {
        const int level = std::clamp(p.adjusted_noise, 0, 1000);
        g.goto_xy(1, 9);
        g.textcolour(caption_colour);
        g.putstr(hud_caption("Noise: ", ja));
        int noise_colour = kLightGrey;
        if (level <= 333) {
            noise_colour = kLightGrey;
        } else if (level <= 666) {
            noise_colour = kYellow;
        } else if (level < 1000) {
            noise_colour = kRed;
        } else {
            noise_colour = kLightMagenta;
        }
        int bar_position = 7;
        this->noise_bar_.horiz_bar_width = 9;
        if ((p.wizard != 0) && (p.noise >= 0)) {
            this->noise_bar_.horiz_bar_width = 6;
            bar_position = 10;
            g.goto_xy(1 + bar_position - 3, 9);
            g.textcolour(noise_colour);
            g.putstr(pad_left(format_int(p.noise), 2) + " ");
        }
        if (level == 1000) {
            this->noise_bar_.horiz_bar_width += 1;
        } else {
            g.goto_xy(1 + 16, 9);
            g.putstr(" ");
        }
        this->noise_bar_.m_default = noise_colour;
        this->noise_bar_.m_change_pos = noise_colour;
        const int bw = this->noise_bar_.horiz_bar_width;
        this->noise_bar_.draw(g, width, 1 + bar_position, 9, (level * bw + 999) / 1000, bw, 0, turn);

        const int start_x = 19 + std::max(6, dcss_text_width(time_caption) + 1);
        g.goto_xy(start_x, 9);
        g.textcolour(kHudValue);
        g.putstr(dcss_chop_string(game_time_text(model, true), std::max(0, width - start_x + 1)));
    }

    // --- 10 行目: 武器 ---
    {
        g.goto_xy(1, 10);
        const DcssInvItem *weapon = (p.weapon_index >= 0) ? item_at(p, p.weapon_index) : nullptr;
        const DcssInvItem *offhand = (p.offhand_index >= 0) ? item_at(p, p.offhand_index) : nullptr;
        const auto print_weapon = [&](const DcssInvItem &w, int total_width) {
            g.textcolour(caption_colour);
            const std::string slot = std::string(1, static_cast<char>(w.letter)) + ") ";
            g.putstr(slot);
            g.textcolour(weapon_colour(w));
            g.putstr(dcss_chop_string(w.name, std::max(0, total_width - static_cast<int>(slot.size()))));
            g.textcolour(kLightGrey);
        };
        if ((weapon != nullptr) && (offhand != nullptr)) {
            print_weapon(*weapon, (width / 2) - 1);
            g.putstr("  ");
            print_weapon(*offhand, (width / 2) - 1);
        } else if ((weapon != nullptr) || (offhand != nullptr)) {
            print_weapon((weapon != nullptr) ? *weapon : *offhand, width);
        } else {
            g.textcolour(caption_colour);
            g.putstr("-) ");
            g.textcolour(dcss_item_colour(p.unarmed_attack_colour));
            g.putstr(dcss_chop_string(p.unarmed_attack, std::max(0, width - 3)));
        }
    }

    // --- 11 行目: 矢筒 ---
    {
        g.goto_xy(1, 11);
        const DcssStyledLine q = dcss_parse_tagged(p.quiver_desc, kLightGrey);
        g.put_styled(q);
    }

    print_status_lights_normal(p, g, cols, rows, 12);
}

void DcssStatPanel::build_small(const DcssUiModel &model, int cols, int rows, bool ja, DcssTextGrid &g)
{
    const DcssPlayer &p = model.player();
    const DcssOptions &opt = model.options();
    const int caption_colour = opt.status_caption_colour;
    const int turn = p.turn;
    const bool super_small = (rows < 32);
    const auto Y = [super_small](int normal, int small) { return super_small ? small : normal; };
    // 見出しは英語のまま（原作は `_is_using_small_layout` のとき訳さない）。種族・神・場所の名前だけ訳す。
    // --- 名前・種族と職業の略称・神・信仰 ---
    g.goto_xy(1, 1);
    g.textcolour(((p.wizard != 0) || (p.explore != 0)) ? kLightMagenta : kYellow);
    g.putstr(p.name);
    g.goto_xy(1, 2);
    g.textcolour(kYellow);
    std::string abbrev = this->abbrev_;
    if (abbrev.empty()) {
        abbrev = p.species.substr(0, 2);
    }
    g.putstr(abbrev);
    if (p.god.empty()) {
        if (p.piety_rank == 2) {
            g.goto_xy(1, 4);
            g.textcolour(kDarkGrey);
            g.putstr(" **....");
        }
    } else {
        g.goto_xy(1, 3);
        g.textcolour(kYellow);
        g.putstr(ja ? dcss_god_name_ja(p.god) : p.god);
        g.goto_xy(1, 4);
        g.textcolour(god_status_colour(p, kYellow));
        g.put_styled(dcss_parse_tagged(god_asterisks(p, true), god_status_colour(p, kYellow)));
    }

    // --- AC・EV・SH ---
    const int ac_row = Y(6, 5);
    const struct {
        const char *label;
        int value;
        int mod;
        int x;
    } defence[3] = { { "AC", p.ac, p.ac_mod, 1 }, { "EV", p.ev, p.ev_mod, 4 }, { "SH", p.sh, p.sh_mod, 7 } };
    for (const auto &d : defence) {
        g.goto_xy(d.x, ac_row);
        g.textcolour(caption_colour);
        g.putstr(d.label);
        g.goto_xy(d.x, ac_row + 1);
        g.textcolour(colour_from_stat_mod(d.mod));
        g.putstr(pad_left(format_int(d.value).substr(0, 2), 2));
    }
    // --- 腕力・知力・器用 ---
    const int st_row = Y(8, 7);
    const struct {
        const char *label;
        int value;
        int x;
    } attr[3] = { { "St", p.str, 1 }, { "In", p.intel, 4 }, { "De", p.dex, 7 } };
    for (const auto &a : attr) {
        g.goto_xy(a.x, st_row);
        g.textcolour(caption_colour);
        g.putstr(a.label);
        g.goto_xy(a.x, st_row + 1);
        g.textcolour(stat_colour(model, a.value));
        g.putstr(pad_left(format_int(a.value).substr(0, 2), 2));
    }
    // --- XL と次のレベル ---
    {
        const int r = Y(10, 9);
        g.goto_xy(1, r);
        g.textcolour(caption_colour);
        g.putstr("XL Next");
        g.goto_xy(1, r + 1);
        g.textcolour(kHudValue);
        g.putstr(pad_left(format_int(p.xl), 2) + " ");
        if (p.xl < 27) {
            g.goto_xy(4, r + 1);
            g.putstr(pad_left(format_int(p.progress), 2) + "% ");
        }
    }
    // --- 騒音 ---
    {
        const int level = std::clamp(p.adjusted_noise, 0, 1000);
        const int r = Y(13, 11);
        g.goto_xy(1, r);
        g.textcolour(caption_colour);
        g.putstr("Noise");
        int noise_colour = (level <= 333) ? kLightGrey : (level <= 666) ? kYellow : (level < 1000) ? kRed : kLightMagenta;
        this->noise_bar_.horiz_bar_width = 9 - 1;
        if (level == 1000) {
            this->noise_bar_.horiz_bar_width += 1;
        }
        this->noise_bar_.m_default = noise_colour;
        this->noise_bar_.m_change_pos = noise_colour;
        const int bw = this->noise_bar_.horiz_bar_width;
        this->noise_bar_.draw(g, cols, 1, r + 1, (level * bw + 999) / 1000, bw, 0, turn);
    }
    // --- 場所 ---
    {
        const int r = Y(15, 13);
        g.goto_xy(1, r);
        g.textcolour(caption_colour);
        g.putstr("Place");
        g.goto_xy(1, r + 1);
        g.textcolour(kHudValue);
        const std::string branch = ja ? dcss_branch_name_ja(p.place) : p.place;
        if (p.depth > 0) {
            const std::string depth = format_int(p.depth);
            g.putstr(dcss_chop_string(branch, std::max(0, 8 - static_cast<int>(depth.size())), false) + ":" + depth);
        } else {
            g.putstr(dcss_chop_string(branch, 9, false));
        }
    }
    // --- 時間 ---
    {
        const int r = Y(17, 15);
        g.goto_xy(1, r);
        g.textcolour(caption_colour);
        g.putstr(opt.show_game_time ? "Time" : "Turn");
        g.goto_xy(1, r + 1);
        g.textcolour(kHudValue);
        g.putstr(dcss_chop_string(game_time_text(model, false), 7));
    }
    // --- 武器・矢筒 ---
    {
        const int r = Y(20, 17);
        g.goto_xy(1, r);
        const DcssInvItem *weapon = (p.weapon_index >= 0) ? item_at(p, p.weapon_index) : nullptr;
        if (weapon != nullptr) {
            g.textcolour(caption_colour);
            const std::string slot = std::string(1, static_cast<char>(weapon->letter)) + ") ";
            g.putstr(slot);
            g.textcolour(weapon_colour(*weapon));
            g.putstr(dcss_chop_string(weapon->name, std::max(0, cols - static_cast<int>(slot.size()))));
        } else {
            g.textcolour(caption_colour);
            g.putstr("-) ");
            g.textcolour(dcss_item_colour(p.unarmed_attack_colour));
            g.putstr(dcss_chop_string(p.unarmed_attack, std::max(0, cols - 3)));
        }
        g.goto_xy(1, Y(21, 18));
        g.put_styled(dcss_parse_tagged(p.quiver_desc, kLightGrey));
    }
    // --- 破滅・汚染 ---
    if ((p.doom != 0) || opt.always_show_doom_contam) {
        g.goto_xy(1, Y(23, 19));
        g.textcolour(caption_colour);
        g.putstr("Doom ");
        g.textcolour((p.doom >= 75) ? kLightMagenta : (p.doom >= 50) ? kLightRed : (p.doom >= 25) ? kYellow : (p.doom > 0) ? kLightGrey : kDarkGrey);
        g.putstr(format_int(p.doom) + "% ");
    }
    if ((p.contam != 0) || opt.always_show_doom_contam) {
        g.goto_xy(1, Y(24, 20));
        g.textcolour(caption_colour);
        g.putstr("Cont ");
        const int contam = std::max((p.contam > 0) ? 1 : 0, p.contam);
        g.textcolour((contam >= 200) ? kRed : (contam >= 100) ? kYellow : kDarkGrey);
        g.putstr(format_int(contam) + "% ");
    }
    // 状態の灯
    print_status_lights_small(p, g, cols, Y(25, 21));

    // --- 体力・魔力: 数字は一番下の行、縦の棒はその上 ---
    {
        const int bar_top = super_small ? 21 : 26;
        const int hp_percent = (p.hp_max > 0) ? ((p.hp * 100) / p.hp_max) : 100;
        g.goto_xy(2, rows);
        g.textcolour(colour_from_threshold(opt.hp_colour, hp_percent, kHudValue));
        g.putstr(pad_left(format_int(p.hp), 3));
        this->hp_bar_.vdraw(g, 2, bar_top, rows - 1, p.hp, p.hp_max, turn);
        const int mp_percent = (p.mp_max == 0) ? 100 : ((p.mp * 100) / p.mp_max);
        g.goto_xy(6, rows);
        g.textcolour(colour_from_threshold(opt.mp_colour, mp_percent, kHudValue));
        g.putstr(pad_left(format_int(p.mp), 3));
        this->mp_bar_.vdraw(g, 6, bar_top, rows - 1, p.mp, p.mp_max, turn);
    }
}

} // namespace hd2d::dcss
