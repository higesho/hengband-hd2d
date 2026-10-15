#include "rl_frame.h"

#include "rl_curses_api.h"
#include "rl_i18n.h"
#include "rl_inventory_api.h"
#include "rl_sound.h"
#include "bridge/named_attack_fx.h"
#include "bridge/input_event_adapter.h"
#include "bridge/official_tile_table.h"
#include "bridge/rogue_borrowed_tiles.h"
#include "bridge/player_identity.h"
#include "frame/cell_feature_bits.h"
#include "frame/attack_fx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void append_utf8(std::string &out, uint32_t cp)
{
    if (cp <= 0x7fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ffu) {
        out.push_back(static_cast<char>(0xc0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
    } else if (cp <= 0xffffu) {
        out.push_back(static_cast<char>(0xe0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
    } else {
        out.push_back(static_cast<char>(0xf0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
    }
}

std::string term_line(int row, int limit = -1)
{
    std::string out;
    int last = -1;
    if (limit >= 0) last = std::min(limit, rl_term_cols()) - 1;
    else for (int col = 0; col < rl_term_cols(); ++col) {
        const auto cp = rl_term_codepoint(row, col);
        if (cp != 0 && cp != ' ') last = col;
    }
    for (int col = 0; col <= last; ++col) {
        auto cp = rl_term_codepoint(row, col);
        if (cp == 0) cp = ' ';
        append_utf8(out, cp);
    }
    const auto id = presentation::player_identity_snapshot();
    const std::string shown = (id.edit_active && id.edit_touched && !id.edit_name_utf8.empty())
        ? id.edit_name_utf8 : id.display_name_utf8;
    const std::string core_name = (id.edit_active && id.edit_touched && !id.edit_name_utf8.empty())
        ? presentation::player_identity_core_name(id.edit_name_utf8, 8) : id.core_name_ascii;
    if (!shown.empty() && !core_name.empty() && shown != core_name) {
        std::size_t pos = 0;
        while ((pos = out.find(core_name, pos)) != std::string::npos) {
            out.replace(pos, core_name.size(), shown);
            pos += shown.size();
        }
    }
    return out;
}

/*
 * 端末のセルは 1 字 1 セル（全角も 1 セル）だが、`frame.menu_term_curs_col` は
 * 画面の桁（全角は 2 桁。`modern_column_byte` の数え方）で渡す約束。セルの番号の
 * まま渡すと、日本語の見出し「好きな果物: 」の途中にカーソルがあることになり、
 * 見出しと値を分けられず、見出しごと入力の中身に見えていた（2026-09-26）。
 */
int display_column(int row, int cell_col)
{
    int width = 0;
    const std::string prefix = term_line(row, cell_col);
    for (std::size_t i = 0; i < prefix.size();) {
        const unsigned char c = static_cast<unsigned char>(prefix[i]);
        std::uint32_t cp = c;
        std::size_t n = 1;
        if (c >= 0xf0) { n = 4; cp = c & 0x07u; }
        else if (c >= 0xe0) { n = 3; cp = c & 0x0fu; }
        else if (c >= 0xc0) { n = 2; cp = c & 0x1fu; }
        for (std::size_t k = 1; k < n && i + k < prefix.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(prefix[i + k]) & 0x3fu);
        i += n;
        const bool wide = cp >= 0x1100 && (cp <= 0x115f || cp == 0x2329 || cp == 0x232a
            || (cp >= 0x2e80 && cp <= 0xa4cf) || (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xf900 && cp <= 0xfaff)
            || (cp >= 0xfe10 && cp <= 0xfe6f) || (cp >= 0xff01 && cp <= 0xff60) || (cp >= 0xffe0 && cp <= 0xffe6)
            || cp >= 0x1f300);
        width += wide ? 2 : 1;
    }
    return width;
}

#if RL_GENERATION == 54
/*
 * 原作が最下行に「スペースで続ける」の案内を出して Space を待つ全画面の一覧（5.4 の `?` → `*` のヘルプ）が
 * 出ているか。案内の字は日本語が「［スペースで続ける］」、英語が "--Press space to continue--"。
 * 通常の地図の最下行は状態行なので、この案内が出ているなら地図ではない。
 */
bool bottom_row_space_wait()
{
    static const std::u32string kJa = U"スペースで続ける";
    static const std::string kEn = "press space to continue";
    const int row = rl_term_rows() - 1;
    if (row < 1) return false;
    std::u32string wide;
    std::string ascii;
    for (int col = 0; col < rl_term_cols(); ++col) {
        const auto cp = rl_term_codepoint(row, col);
        if (cp == 0) continue;
        wide.push_back(static_cast<char32_t>(cp));
        if (cp >= 0x20 && cp <= 0x7e) ascii.push_back(static_cast<char>(cp >= 'A' && cp <= 'Z' ? cp + 32 : cp));
    }
    return wide.find(kJa) != std::u32string::npos || ascii.find(kEn) != std::string::npos;
}
#endif

bool find_player(int &row, int &col)
{
#if RL_GENERATION == 54
    /*
     * 5.4 のヘルプの一覧には「@  print current stats」の行がある。その '@' を自分の位置と読むと、
     * 一覧の全体を地図のマスとして扱い、窓にならず文字だけが地図の上に散らばって出ていた（調-11・F-rogue54-02）。
     * さらに rl_can_see() が一覧の字を座標と読んで、原作の roomin() が英語のメッセージを出していた。
     * 案内が最下行に出ている画は地図ではないので、自分の位置は無いとする（3.6 のヘルプには '@' の行が無い）。
     *
     * 5.4 の他の一覧も同じ読み違えをする。原作は地図（stdscr）の代わり・上に別の窓（hw と add_line の重ねた窓）を出し、
     * その窓に wrefresh して Space（options は設定の入力）を待つ。窓の中に '@' が出る例は、名前・好きな果物の設定に
     * '@' を入れたときの options、'@' を含む名前を付けた物の一覧、墓標の前の勝利の画面（これは最下行の案内で見分ける）。
     * そこで、直近の画面反映が stdscr 以外の窓なら地図は出ていないとする（rl_page_window_shown。rl_curses.c）。
     * 原作が printf で書いた字（V のスコア一覧など）は地図に重なって残るので、その字のマスは '@' の探索と
     * 地図のマスから外す（rl_term_is_text）。
     */
    if (bottom_row_space_wait() || rl_page_window_shown() != 0) {
        row = 1;
        col = 0;
        return false;
    }
#endif
    for (int y = 1; y < std::min(23, rl_term_rows() - 1); ++y) {
        for (int x = 0; x < rl_term_cols(); ++x) {
#if RL_GENERATION == 54
            if (rl_term_is_text(y, x) != 0) continue;
#endif
            if (rl_term_codepoint(y, x) == '@') {
                row = y;
                col = x;
                return true;
            }
        }
    }
    row = 1;
    col = 0;
    return false;
}

/*
 * ゲームが終わった画面（墓石・得点表・勝利の画面）が出ているか。
 * 原作はどの画面にも「死んだ」旗を持たないので、画面の字で見分ける（墓石は ASCII の絵で、日本語の表示でも
 * 「REST」「PEACE」はそのまま出る。得点表と勝利の画面は rl_i18n.c が訳す前の英語・訳した日本語の両方を見る）。
 * 通常の地図や持ち物・ヘルプの一覧に、これらの字が同時に出ることは無い。
 */
bool game_over_screen_text()
{
    bool rest = false;
    bool peace = false;
    for (int row = 0; row < rl_term_rows(); ++row) {
        std::string ascii;
        std::u32string wide;
        for (int col = 0; col < rl_term_cols(); ++col) {
            const auto cp = rl_term_codepoint(row, col);
            if (cp == 0) continue;
            wide.push_back(static_cast<char32_t>(cp));
            if (cp >= 0x20 && cp <= 0x7e) ascii.push_back(static_cast<char>(cp));
        }
        if (ascii.find("REST") != std::string::npos) rest = true;
        if (ascii.find("PEACE") != std::string::npos) peace = true;
        if (ascii.find("Top Ten") != std::string::npos || ascii.find("Congratulations") != std::string::npos
            || ascii.find("You are dead") != std::string::npos) {
            return true;
        }
        if (wide.find(U"おめでとう") != std::u32string::npos || wide.find(U"あなたは死にました") != std::u32string::npos
            || wide.find(U"上位 10") != std::u32string::npos) {
            return true;
        }
    }
    return rest && peace;
}

//! このプロセスで自キャラが 1 度でも見えたか（見えない画面がタイトル・名前の入力か、ゲーム中の全画面の一覧かの見分け）。
bool g_hero_seen = false;
//! ゲームが終わった画面を 1 度見たら、プロセスが終わるまで真（得点表のあとの画面も同じ扱い）。
bool g_game_over = false;

bool is_monster(uint32_t cp) { return cp >= 'A' && cp <= 'Z'; }

bool is_object(uint32_t cp)
{
    switch (cp) {
    case '*': // gold
    case '!': // potion
    case '?': // scroll
    case ':': // food
    case ')': // weapon
    case ']': // armor
    case ',': // amulet
    case '=': // ring
    case '/': // wand/staff
        return true;
    default:
        return false;
    }
}

uint16_t object_tile(uint32_t cp)
{
    int hengband_object_id = 0;
    switch (cp) {
    case '*': hengband_object_id = 488; break; // gold
    case ':': hengband_object_id = 21; break;  // food
    case ']': hengband_object_id = 103; break; // armor
    case ')': hengband_object_id = 73; break;  // weapon
    case '?': hengband_object_id = 175; break; // scroll
    case '!': hengband_object_id = 225; break; // potion
    case '/': hengband_object_id = 269; break; // wand/staff
    case '=': hengband_object_id = 510; break; // ring
    case ',': hengband_object_id = 503; break; // amulet
    default: break;
    }
    return hengband_object_id != 0 ? presentation::lookup_object_tile(hengband_object_id) : 0;
}

/*
 * 絵は記号ではなく原作の名前で引く。3.6 と 5.4 は同じ記号でも別の種族である
 * （K は 3.6 が kobold、5.4 が kestrel）。以前は 5.4 の並びの表 1 本を両方に使っていて、
 * 3.6 のコボルドがタカ（変愚 1315）の絵になっていた（2026-09-28）。
 * 番号は変愚の MonraceDefinitions.jsonc の id。変愚にいない種族は近い物で代える。
 */
int hengband_monrace_for(std::string_view name)
{
    static constexpr std::pair<std::string_view, int> table[] = {
        // 3.6 と 5.4 で共通
        { "bat", 114 },              // Giant brown bat
        { "centaur", presentation::frox_player(51) }, // Frox の種族 Centaur（FP51。rogue_borrowed_tiles.h）
        { "dragon", 589 },           // Mature red dragon（Rogue の竜は最深部の強敵）
        { "hobgoblin", 118 },        // Snaga（変愚の説明で「ゴブリン」と呼ばれるオーク）
        { "leprechaun", 258 },       // Cheerful leprechaun
        { "nymph", 248 },            // Nixie（変愚にニンフはいない）
        { "orc", 126 },              // Cave orc
        { "snake", 28 },             // Large brown snake
        { "troll", 496 },            // Cave troll
        { "vampire", 432 },          // Vampire
        { "wraith", 416 },           // White wraith
        { "yeti", 154 },             // Yeti
        { "zombie", 229 },           // Zombified human
        // 3.6 だけ
        { "giant ant", 49 },         // Giant black ant
        { "floating eye", 32 },      // Floating eye
        { "violet fungi", 108 },     // Purple mushroom patch（紫のきのこ）
        { "gnome", 281 },            // Gnome mage
        { "invisible stalker", 514 },// Night stalker
        { "jackal", 35 },            // Jackal
        { "kobold", 30 },            // Kobold
        { "mimic", 353 },            // Chest mimic
        { "purple worm", 446 },      // Giant purple worm
        { "quasit", 294 },           // Quasit
        { "rust monster", 284 },     // Rust monster
        { "umber hulk", 283 },       // Umber hulk
        { "xorn", 550 },             // Xorn
        // 5.4 だけ
        { "aquator", presentation::rogue_own_tile(1) }, // 生成した水ごけの怪物（RLaquator。rogue_borrowed_tiles.h）
        { "emu", presentation::frox_monster(1327) }, // Frox の Cassowary（走鳥。rogue_borrowed_tiles.h）
        { "venus flytrap", 1327 },   // Piranha Plant
        { "griffin", 279 },          // Griffon
        { "ice monster", 570 },      // Ice elemental
        { "jabberwock", 778 },       // Jabberwock
        { "kestrel", 1315 },         // Hawk
        { "medusa", presentation::rogue_own_tile(2) }, // 生成した蛇の髪の女性（RLmedusa。rogue_borrowed_tiles.h）
        { "phantom", 152 },          // Phantom warrior
        { "quagga", presentation::frox_monster(1334) }, // Frox の Brumby（野生の馬）
        { "rattlesnake", 119 },      // Rattlesnake
        { "black unicorn", 1019 },   // Kokuo, Raou's Steed（黒王号。黒い馬）
        { "xeroc", 353 },            // Chest mimic
    };
    for (const auto &[key, id] : table) {
        if (key == name) return id;
    }
    return 0;
}

uint16_t monster_tile(uint32_t cp)
{
    if (!is_monster(cp)) return 0;
    const char *name = rl_monster_name(static_cast<int>(cp));
    if (name == nullptr) return 0;
    const int id = hengband_monrace_for(name);
    // 負の番号は Frox の板を借りる（変愚にいない種族。rogue_borrowed_tiles.h）。
    if (id < 0) return presentation::rogue_borrowed_tile(id);
    return presentation::lookup_monster_tile(static_cast<uint16_t>(id));
}

uint16_t flags_for(uint32_t cp)
{
    if (cp == 0 || cp == ' ') return 0;
    uint16_t flags = CELL_FEAT_KNOWN | CELL_FEAT_GLOWING;
    if (cp == '|' || cp == '-' || cp == '&') flags |= CELL_FEAT_WALL;
    else if (cp == '+') flags |= CELL_FEAT_DOOR;
    else flags |= CELL_FEAT_PASSABLE;
    if (cp == '%') flags |= CELL_FEAT_STAIRS;
    if (cp == '@') flags |= CELL_FEAT_PLAYER;
    return flags;
}

MinimapKind minimap_kind(uint16_t flags)
{
    if ((flags & CELL_FEAT_KNOWN) == 0) return MinimapKind::Unknown;
    if ((flags & CELL_FEAT_WALL) != 0) return MinimapKind::Wall;
    if ((flags & CELL_FEAT_DOOR) != 0) return MinimapKind::Door;
    if ((flags & CELL_FEAT_STAIRS) != 0) return MinimapKind::Stairs;
    if ((flags & CELL_FEAT_PLAYER) != 0) return MinimapKind::Player;
    return MinimapKind::Floor;
}

bool continuation_visible()
{
    const auto line = term_line(0);
    return line.find("--More--") != std::string::npos || line.find("More") != std::string::npos
        || line.find("Press space to continue") != std::string::npos
        || line.find("press space to continue") != std::string::npos
        || line.find("続") != std::string::npos;
}

//! 表示言語（修-46）。アダプタが直に書く字は、画面の言語が英語のときは英語を選ぶ。
const char *tr(const char *ja, const char *en)
{
    return rl_i18n_is_english() != 0 ? en : ja;
}

void add_prompt_choice(GameFrame &frame, std::string label, int key)
{
    PromptChoice choice;
    choice.label_utf8 = std::move(label);
    choice.key = key;
    frame.prompt.choices.push_back(std::move(choice));
}

void fill_lightweight_raw_prompt(GameFrame &frame, const std::string &title, bool offer_all)
{
    frame.menu_open = false;
    frame.pre_game_menu = false;
    frame.menu_over_map = true;
    frame.awaiting_command = false;
    frame.menu_term_lines.clear();
    frame.menu_choices.clear();
    frame.prompt = {};
    frame.prompt.lightweight = true;
    frame.prompt.text_utf8 = title;
    frame.prompt.line_index = -1;
    frame.text_input_active = true;
    if (offer_all) add_prompt_choice(frame, tr("すべて", "All"), '*');
    add_prompt_choice(frame, tr("戻る", "Back"), 27);
    frame.prompt.footer_utf8 = offer_all
        ? tr("1文字入力  *: すべて  B/Esc: 戻る", "Type 1 character  *: all  B/Esc: back")
        : tr("1文字入力  B/Esc: 戻る", "Type 1 character  B/Esc: back");
}

void fill_choice_menu(GameFrame &frame, const std::string &title,
    const std::vector<std::pair<int, std::string>> &choices, bool player_visible)
{
    frame.menu_open = true;
    frame.pre_game_menu = false;
    frame.menu_over_map = player_visible;
    frame.menu_popup = player_visible;
    frame.awaiting_command = false;
    frame.menu_term_lines.clear();
    frame.menu_choices.clear();
    frame.menu_term_curs_row = -1;
    frame.menu_term_curs_col = -1;

    if (!title.empty()) {
        TermMirrorLine line;
        line.text_utf8 = title;
        line.color = 1;
        line.source_row = 0;
        frame.menu_term_lines.push_back(std::move(line));
    }
    for (const auto &[key, label] : choices) {
        std::string key_text;
        if (key == 27) key_text = "ESC";
        else if (key >= 0x20 && key <= 0x7e) key_text.push_back(static_cast<char>(key));
        else key_text = "?";

        TermMirrorLine line;
        line.source_row = static_cast<int>(frame.menu_term_lines.size());
        line.color = 1;
        line.text_utf8 = key_text + ") " + label;
        const int line_index = static_cast<int>(frame.menu_term_lines.size());
        frame.menu_term_lines.push_back(line);

        MenuChoice choice;
        choice.line_index = line_index;
        choice.span_begin = 0;
        choice.span_len = static_cast<int>(line.text_utf8.size());
        choice.key_begin = 0;
        choice.key_len = static_cast<int>(key_text.size());
        choice.key = key;
        choice.label_utf8 = label;
        frame.menu_choices.push_back(std::move(choice));
    }
    frame.controller_hint = tr("上下:選択  A:決定  B:戻る", "Up/Down: select  A: OK  B: back");
}

bool contains_any(std::string_view text, std::initializer_list<std::string_view> needles)
{
    for (const auto needle : needles) if (text.find(needle) != std::string_view::npos) return true;
    return false;
}

std::string translated(const char *text)
{
    if (text == nullptr || *text == '\0') return {};
    const char *ja = rl_ja_text(text);
    return (ja != nullptr && *ja != '\0') ? std::string(ja) : std::string(text);
}

//! 装備・視界の敵・持ち物のサブパネル。原作はどれも一覧を持たないので、ここで組む。
void fill_sub_panels(GameFrame &frame, const std::vector<uint32_t> &monster_glyphs)
{
    for (const auto glyph : monster_glyphs) {
        const std::string name = translated(rl_monster_name(static_cast<int>(glyph)));
        if (!name.empty()) frame.sub3_lines.push_back(name);
    }
    if (frame.sub3_lines.empty()) frame.sub3_lines.push_back(tr("(なし)", "(none)"));

    std::string weapon, armor, left_ring, right_ring;
    const int count = rl_inventory_count();
    int shown = 0;
    for (int i = 0; i < count; ++i) {
        rl_inventory_item_state item{};
        if (!rl_inventory_item(i, &item)) continue;
        std::string description = translated(item.description);
        // 3.6 の inv_name() は英文の終止符を付ける。訳した名前の後ろには要らない。
        if (!description.empty() && description.back() == '.') description.pop_back();
        if ((item.equipped_flags & 1u) != 0u) weapon = description;
        if ((item.equipped_flags & 2u) != 0u) armor = description;
        if ((item.equipped_flags & 4u) != 0u) left_ring = description;
        if ((item.equipped_flags & 8u) != 0u) right_ring = description;
        if (shown < 11) {
            frame.sub5_lines.push_back(description);
            ++shown;
        }
    }
    if (count > shown) frame.sub5_lines.push_back("…(+" + std::to_string(count - shown) + ")");
    if (frame.sub5_lines.empty()) frame.sub5_lines.push_back(tr("(なし)", "(none)"));

    const auto line = [](const char *label, const std::string &value) {
        return std::string(label) + (value.empty() ? std::string(tr("(なし)", "(none)")) : value);
    };
    frame.sub2_lines.push_back(line(tr("武器: ", "Weapon: "), weapon));
    frame.sub2_lines.push_back(line(tr("防具: ", "Armor: "), armor));
    frame.sub2_lines.push_back(line(tr("左手: ", "Left hand: "), left_ring));
    frame.sub2_lines.push_back(line(tr("右手: ", "Right hand: "), right_ring));
}

//! メッセージ。原作は 1 行目を上書きするだけなので、履歴はアダプタが積んだものを使う。
void fill_messages(GameFrame &frame, const std::string &current)
{
    const int history = rl_message_history_count();
    for (int i = 0; i < history; ++i) {
        uint32_t seq = 0;
        const char *text = rl_message_history_text(i, &seq);
        if (text == nullptr || *text == '\0') continue;
        MessageEvent event;
        event.seq = seq;
        event.color = 1;
        event.text_utf8 = text;
        frame.messages.push_back(std::move(event));
    }
    if (current.empty()) return;
    if (frame.messages.empty()) {
        MessageEvent event;
        event.seq = 1;
        event.color = 1;
        event.text_utf8 = current;
        frame.messages.push_back(std::move(event));
        return;
    }
    /*
     * ［続く］待ちの間、1 行目は履歴の本文に印を足したものになる。別の 1 本として
     * 積むと同じ文が 2 度並ぶので、最後の 1 本を実際の 1 行目へ差し替える。
     */
    auto &last = frame.messages.back();
    // 1 行目は末尾の空白を削って読むので、履歴の側も削ってから比べる。
    std::string last_text = last.text_utf8;
    while (!last_text.empty() && last_text.back() == ' ') last_text.pop_back();
    if (current.rfind(last_text, 0) == 0) last.text_utf8 = current;
    else if (current != last_text) {
        MessageEvent event;
        event.seq = last.seq + 1;
        event.color = 1;
        event.text_utf8 = current;
        frame.messages.push_back(std::move(event));
    }
}

void parse_status(GameFrame &frame, const std::string &status)
{
    int depth = 0, gold = 0, hp = 0, hpmax = 0, exp_level = 0;
    const bool english = std::sscanf(status.c_str(), "Level: %d Gold: %d Hp: %d(%d)", &depth, &gold, &hp, &hpmax) >= 4;
    const bool japanese = !english
        && std::sscanf(status.c_str(), "階: %d 金塊: %d 体力: %d(%d)", &depth, &gold, &hp, &hpmax) >= 4;
    if (english || japanese) {
        frame.hud.depth = depth;
        frame.hud.gold = gold;
        frame.hud.hp = hp;
        frame.hud.hp_max = hpmax;
    }
    const auto exp = status.find("Exp:");
    if (exp != std::string::npos) {
        (void)std::sscanf(status.c_str() + exp, "Exp: %d", &exp_level);
        frame.hud.level = exp_level;
    } else {
        const auto ja_exp = status.find("経験:");
        if (ja_exp != std::string::npos) {
            (void)std::sscanf(status.c_str() + ja_exp, "経験: %d", &exp_level);
            frame.hud.level = exp_level;
        }
    }
    frame.hud.status_line = status;
}

} // namespace

namespace rogue_legacy {

void reset_frame_state()
{
    g_hero_seen = false;
    g_game_over = false;
}

std::uint16_t object_tile_for_glyph(std::uint32_t glyph)
{
    return object_tile(glyph);
}

GameFrame capture_frame(std::uint64_t frame_id, const char *core_name)
{
    GameFrame frame;
    frame.frame_id = frame_id;
    frame.view_w = 80;
    frame.view_h = 22;

    int player_row = 1, player_col = 0;
    const bool player_visible = find_player(player_row, player_col);
    frame.player_gx = player_col;
    frame.player_gy = std::max(0, player_row - 1);
    frame.cam_x = frame.player_gx;
    frame.cam_y = frame.player_gy;
    frame.floor.dungeon_id = 1;
    frame.floor.kind = static_cast<int>(FloorKind::Dungeon);
    frame.floor.place_name_utf8 = core_name != nullptr ? core_name : "Rogue";
    const auto identity = presentation::player_identity_snapshot();
    frame.hud.name = identity.display_name_utf8.empty() ? frame.floor.place_name_utf8 : identity.display_name_utf8;
    frame.lighting.daytime = false;

    frame.minimap.width = 80;
    frame.minimap.height = 22;
    frame.minimap.player_gx = frame.player_gx;
    frame.minimap.player_gy = frame.player_gy;
    frame.minimap.kinds.assign(80u * 22u, static_cast<uint8_t>(MinimapKind::Unknown));
    frame.minimap.markers.assign(80u * 22u, static_cast<uint8_t>(MinimapMarker::None));

    /*
     * 光の届く範囲（・CORE_HUD_ROLLOUT.md §2.2。決めたこと 2026-10-11）。
     * 光源は松明風（`light_kind` 1・半径 1）。`player_lit` は **`cells` に積んだ順**に 1 マス 1 ビット
     * （このアダプタは字のあるマスだけを積むので、添字は `frame.cells.size()`）。立てるのは自分の周り 3x3 で
     * 目が見えているマス（原作の `cansee()`）。いま見えていない記憶のマスは `light_level` 1（少し暗い。
     * 見えなくはしない）、いま見えているマス（同じ明るい部屋か近く）は 2。
     * 地図が出ていない画（誕生前・全画面の窓）はゲームの状態が定まらないので、従来どおり送らない。
     */
    const bool lighting_on = player_visible;
    std::vector<uint8_t> player_lit;
    if (lighting_on) {
        frame.lighting.light_kind = 1;
        frame.lighting.light_radius = 1;
        // いま見えていない記憶のマス（light_level 1）を少し暗くしてほしい、という申告。画面側は申告のあるコアだけ暗くする。
        frame.lighting.memory_dim = 1;
        player_lit.assign((80u * 22u + 7u) / 8u, 0);
    }

    /*
     * 状態のアイコン。空腹・盲目・混乱・麻痺・捕まっている・加速・
     * 透明視・混乱させる手・鑑定済みの指輪の効果（5.4 は幻覚・浮遊・怪物感知も）。集め方は rl_inventory.c の rl_status_effects。
     * 地図が出ていない画（誕生前・全画面の窓）はゲームの状態が定まらないので、従来どおり送らない。
     */
    if (player_visible) {
        rl_test_status_apply(); // 環境変数 HENGBAND_TEST_STATUS が無ければ何もしない
        rl_test_fx_items_apply(); // 環境変数 HENGBAND_TEST_FX_ITEMS が無ければ何もしない
        rl_status_effect effects[24];
        const int count = rl_status_effects(effects, 24);
        for (int i = 0; i < count; ++i) {
            StatusEffectView view;
            view.key = effects[i].key;
            view.label_utf8 = effects[i].label;
            view.amount_utf8 = effects[i].amount;
            view.kind = static_cast<StatusEffectKind>(effects[i].kind);
            frame.status_effects.push_back(std::move(view));
        }
    }

    std::vector<uint32_t> monster_glyphs;
    for (int y = 1; y < 23; ++y) {
        for (int x = 0; x < 80; ++x) {
#if RL_GENERATION == 54
            if (rl_term_is_text(y, x) != 0) continue; // 原作が printf で書いた字（V のスコア一覧）は地図のマスではない
#endif
            const uint32_t cp = rl_term_codepoint(y, x);
            const uint16_t flags = flags_for(cp);
            if (flags == 0) continue;
            MapCellView cell;
            cell.gx = static_cast<int16_t>(x);
            cell.gy = static_cast<int16_t>(y - 1);
            cell.feature_flags = flags;
            cell.light_level = 2;
            if (lighting_on) {
                const bool lit = rl_light_reaches(y, x) != 0;
                if (lit) {
                    const std::size_t at = frame.cells.size();
                    player_lit[at / 8] = static_cast<uint8_t>(player_lit[at / 8] | (1u << (at % 8)));
                }
                cell.light_level = (lit || rl_can_see(y, x) != 0) ? 2 : 1;
            }
            cell.ascii_fallback = (cp >= 0x20 && cp <= 0x7e) ? static_cast<char>(cp) : ' ';
            if ((flags & CELL_FEAT_PLAYER) != 0) {
                cell.tile_index = presentation::lookup_monster_tile(43);
            } else if (is_monster(cp)) {
                cell.monster_id = static_cast<uint16_t>(cp - 'A' + 1);
                cell.tile_index = monster_tile(cp);
            } else if (is_object(cp)) {
                // Legacy Rogue exposes only the visible glyph here, not the
                // underlying object kind.  Preserve that honest granularity:
                // mark it as an object and use one representative tile for the
                // visible category instead of inventing a subtype.
                cell.object_id = static_cast<uint16_t>(cp);
                cell.tile_index = object_tile(cp);
            } else if (cp == '^') {
                // The terminal tells us only that a discovered trap exists,
                // not which trap type it is.  Use the same generic trap tile
                // fallback as RogueClone2 when no precise type is available.
                cell.object_id = 0xff00u;
                cell.tile_index = presentation::lookup_terrain_tile(24);
            }
            const std::size_t at = static_cast<std::size_t>(y - 1) * 80u + static_cast<std::size_t>(x);
            frame.minimap.kinds[at] = static_cast<uint8_t>(minimap_kind(flags));
            if (is_monster(cp) && std::find(monster_glyphs.begin(), monster_glyphs.end(), cp) == monster_glyphs.end())
                monster_glyphs.push_back(cp);
            if (is_monster(cp)) frame.minimap.markers[at] = static_cast<uint8_t>(MinimapMarker::Monster);
            else if (cp == '^') frame.minimap.markers[at] = static_cast<uint8_t>(MinimapMarker::Trap);
            else if (cp == '*') frame.minimap.markers[at] = static_cast<uint8_t>(MinimapMarker::Treasure);
            else if (is_object(cp)) frame.minimap.markers[at] = static_cast<uint8_t>(MinimapMarker::Item);
            frame.cells.push_back(cell);
        }
    }

    if (lighting_on && !frame.cells.empty()) {
        player_lit.resize((frame.cells.size() + 7) / 8); // cells の数ぶんに切り詰めて渡す
        frame.lighting.player_lit = std::move(player_lit);
    }

    const std::string message = term_line(0);
    fill_messages(frame, message);
    fill_sub_panels(frame, monster_glyphs);

    frame.bottom_row_cols = 80;
    const std::string status = term_line(23);
    if (!status.empty()) {
        frame.bottom_row_runs.push_back(TermTextRun{ 0, status, 1 });
        parse_status(frame, status);
        frame.floor.dun_level = frame.hud.depth;
    }

    const bool more = continuation_visible();
    /*
     * **命令を読んでいる間だけ**「命令待ち」にする（原作の command() が readchar() を
     * rl_command_input_begin/end で囲んでいる。tools/rogue_legacy/prepare_sources.py）。
     * 以前は「子の入力待ちでない」だけで判定していたので、命令のキーを読んだ直後から
     * 次の問い（「どの持ち物を調べますか:」「方向は？」）が立つまでの処理中の画も
     * 命令待ちに見えた。持ち物メニューはそれを「命令が終わった」と取り違え、原作が
     * 問いを待っている上に一覧を開き直していた（2026-09-25。`I` と `t`）。
     */
    frame.awaiting_command = player_visible && !more && rl_command_input_active() && !rl_child_input_active();
    /*
     * 数字で前置きする繰り返し回数（原作の command() の isdigit の輪。prepare_sources.py が
     * rl_count_input_* で囲んでいる）。RogueClone2 の「繰り返し回数を指定」と同じ枠を出す。
     * 決めた値は共通の set_number が「DEL×5＋数字」に展開し（input_event_adapter.h）、
     * 決定の Enter は回数の終わりだけを意味する（コマンドはその後で選ぶ）。
     */
    presentation::set_repeat_count_input_active(false);
    if (rl_count_input_active()) {
        const int max = (std::max)(1, rl_count_input_max());
        frame.numeric.active = true;
        frame.numeric.prompt_utf8 = std::string(tr("繰り返し回数 (1-", "Repeat count (1-")) + std::to_string(max) + "): ";
        frame.numeric.min = 1;
        frame.numeric.max = max;
        frame.numeric.value = std::clamp(rl_count_input_value(), 0, max);
        frame.numeric.digits = static_cast<int>(std::to_string(max).size());
        frame.numeric.text_len = static_cast<int>(std::to_string(frame.numeric.value).size());
        frame.awaiting_command = false;
        presentation::set_repeat_count_input_active(true);
    }
    /*
     * 自キャラが見えない画面は全部 `menu_open` だが、`pre_game_menu`（タイトル・誕生・死亡後）は、まだ 1 度も自キャラが
     * 見えていないとき（名前の入力）と、ゲームが終わった画面だけにする。ゲーム中の持ち物・ヘルプ・発見済みの物の
     * 全画面の一覧まで誕生の画面の扱い（題「キャラクター作成」・phase が birth）にしていた（修-10・F-rogue36-01）。
     */
    if (player_visible) g_hero_seen = true;
    if (!g_game_over && game_over_screen_text()) g_game_over = true;
    frame.menu_open = !player_visible;
    frame.pre_game_menu = frame.menu_open && (!g_hero_seen || g_game_over);
    frame.game_over = frame.menu_open && g_game_over;
    frame.text_input_active = rl_text_input_active() != 0;
    frame.player_name_input_active = rl_player_name_input_active() != 0;
    if (frame.menu_open) {
        const int cursor_row = rl_term_cursor_row();
        for (int row = 0; row < rl_term_rows(); ++row) {
            const std::string text = term_line(row);
            if (text.empty()) continue;
            TermMirrorLine line;
            line.text_utf8 = text;
            line.color = 1;
            line.source_row = row;
            if (row == cursor_row) {
                frame.menu_term_curs_row = static_cast<int>(frame.menu_term_lines.size());
                frame.menu_term_curs_col = display_column(row, rl_term_cursor_col());
            }
            frame.menu_term_lines.push_back(std::move(line));
        }
    }

    // Rogue 3.6/5.4 options are a sequential native editor: Enter advances,
    // '-' moves back, and the active boolean waits for T/F.  Mark the live
    // row as a cursor so the shared modern settings adapter can show one
    // settings list without inventing another modal menu.
    if (frame.menu_open && !frame.text_input_active && frame.menu_term_curs_row >= 0) {
        int bool_rows = 0;
        for (const auto &line : frame.menu_term_lines) {
            // get_bool() は想定外のキーで値の後ろへ "(T or F)" を書き足す。
            // その行も設定行として数える。
            std::string_view text = line.text_utf8;
            if (text.ends_with(")")) {
                const auto hint = text.rfind('(');
                if (hint != std::string_view::npos) text = text.substr(0, hint);
                while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
            }
            if (text.ends_with("True") || text.ends_with("False")
                || text.ends_with(reinterpret_cast<const char *>(u8"有効"))
                || text.ends_with(reinterpret_cast<const char *>(u8"無効"))) ++bool_rows;
        }
        if (bool_rows >= 2) {
            frame.menu_core_cursor = {};
            frame.menu_core_cursor.line_index = frame.menu_term_curs_row;
            frame.menu_core_cursor.key = 13;
            frame.menu_core_cursors.assign(1, frame.menu_core_cursor);
            frame.menu_choices.clear();
            frame.awaiting_command = false;
        }
    }

    /*
     * Legacy get_item() waits for a raw pack letter.  The source patch does
     * not change that rule; it only publishes the same valid letters and the
     * already-rendered descriptions while readchar() is blocked.  Turn those
     * into an HD2D cursor menu so a controller/touch user can choose the one
     * byte the original game is already waiting for.
     */
    if (rl_prompt_active() && !rl_prompt_is_item()) {
        frame.menu_open = false;
        frame.pre_game_menu = false;
        frame.menu_over_map = player_visible;
        frame.awaiting_command = false;
        frame.menu_choices.clear();
        frame.menu_core_cursor = {};
        frame.menu_core_cursors.clear();
        frame.prompt = {};
        frame.prompt.lightweight = true;
        frame.prompt.line_index = -1;
        const char *title = rl_prompt_title();
        frame.prompt.text_utf8 = title != nullptr ? title : "";
        const int count = rl_prompt_choice_count();
        bool has_yes = false, has_no = false;
        for (int i = 0; i < count; ++i) {
            const int key = rl_prompt_choice_key(i);
            if (key == 'y') has_yes = true;
            if (key == 'n') has_no = true;
        }
        /*
         * はい／いいえの問い（やめる・5.4 の保存先と上書き）は、ほかのコアと同じ確認の窓にする
         * （修-62。決めたこと「確認の窓にする」）。画面下の帯（lightweight）にしない。
         * 窓の答え（決定・取消・y/n）は画面側の共通の入力の取り込みが、選択肢の key を 1 キーにして返す。
         * 選択肢の字と問いの文は原作の日本語の字なので、英語の表示ではアダプタで英語にする。
         */
        const bool yes_no = has_yes && has_no;
        if (yes_no) {
            frame.prompt.lightweight = false;
            if (rl_i18n_is_english() != 0 && !message.empty()) frame.prompt.text_utf8 = message;
        }
        for (int i = 0; i < count; ++i) {
            const int key = rl_prompt_choice_key(i);
            const char *label = rl_prompt_choice_label(i);
            if (yes_no && key == 'y') add_prompt_choice(frame, tr("はい", "Yes"), key);
            else if (yes_no && key == 'n') add_prompt_choice(frame, tr("いいえ", "No"), key);
            else if (yes_no && key == 27) add_prompt_choice(frame, tr("戻る", "Back"), key);
            else add_prompt_choice(frame, label != nullptr ? label : "", key);
        }
        frame.prompt.footer_utf8 = tr("左右/上下:選択  A:決定  B/Esc:戻る", "Left/Right/Up/Down: select  A: OK  B/Esc: back");
    } else if (rl_prompt_active()) {
        frame.menu_open = true;
        frame.pre_game_menu = false;
        frame.menu_over_map = player_visible;
        // 対象を選ぶ問いは地図の上の小さな箱で出す（決めたこと 2026-10-02「全画面にならないように」）。
        frame.menu_popup = player_visible;
        frame.awaiting_command = false;
        frame.menu_term_lines.clear();
        frame.menu_choices.clear();
        frame.menu_term_curs_row = -1;
        frame.menu_term_curs_col = -1;

        const char *title = rl_prompt_title();
        if (title != nullptr && *title != '\0') {
            TermMirrorLine line;
            line.text_utf8 = title;
            line.color = 1;
            line.source_row = 0;
            frame.menu_term_lines.push_back(std::move(line));
        }
        /*
         * 選択肢の並べ方（決めたこと 2026-10-03「案1」「ソースをいじらずアダプタで対応して」）。
         * 原作の get_item() は種類の合わない持ち物の文字も受け付ける（武器以外を投げるなど）が、
         * ソース側が知らせるのは合う種類だけ。そこで **合う種類（ソースが知らせた物）→
         * 「その他: 」を付けた残りの持ち物 → 戻る** の順に並べる。全部の持ち物が箱に出るので、
         * 全画面の持ち物一覧へ移る `*` は出さない（問いの文の案内も `rl_i18n.c` で外してある）。
         */
        std::vector<std::pair<int, std::string>> ordered;
        bool has_escape = false;
        const int count = rl_prompt_choice_count();
        for (int i = 0; i < count; ++i) {
            const int key = rl_prompt_choice_key(i);
            const char *label = rl_prompt_choice_label(i);
            if (key == '*') continue;
            if (key == 27) {
                has_escape = true;
                continue;
            }
            ordered.emplace_back(key, label != nullptr ? label : "");
        }
        const bool letters_offered = std::any_of(ordered.begin(), ordered.end(),
            [](const auto &entry) { return entry.first >= 'a' && entry.first <= 'z'; });
        if (letters_offered) {
            const std::size_t offered = ordered.size();
            const int pack_count = rl_inventory_count();
            for (int i = 0; i < pack_count; ++i) {
                rl_inventory_item_state item{};
                if (!rl_inventory_item(i, &item)) continue;
                const bool already = std::any_of(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(offered),
                    [&](const auto &entry) { return entry.first == item.key; });
                if (already) continue;
                const char *translated = rl_ja_text(item.description);
                ordered.emplace_back(item.key, std::string(tr("その他: ", "Other: ")) + (translated != nullptr ? translated : item.description));
            }
        }
        if (has_escape) ordered.emplace_back(27, tr("戻る", "Back"));
        for (const auto &[key, label_text] : ordered) {
            const char *label = label_text.c_str();
            std::string key_text;
            if (key == 27) key_text = "ESC";
            else if (key >= 0x20 && key <= 0x7e) key_text.push_back(static_cast<char>(key));
            else key_text = "?";

            TermMirrorLine line;
            line.source_row = static_cast<int>(frame.menu_term_lines.size());
            line.color = 1;
            line.text_utf8 = key_text + ") " + (label != nullptr ? label : "");
            const int line_index = static_cast<int>(frame.menu_term_lines.size());
            frame.menu_term_lines.push_back(line);

            MenuChoice choice;
            choice.line_index = line_index;
            choice.span_begin = 0;
            choice.span_len = static_cast<int>(line.text_utf8.size());
            choice.key_begin = 0;
            choice.key_len = static_cast<int>(key_text.size());
            choice.key = key;
            choice.label_utf8 = label != nullptr ? label : "";
            frame.menu_choices.push_back(std::move(choice));
        }
        frame.controller_hint = tr("上下:選択  A:決定  B:戻る", "Up/Down: select  A: OK  B: back");
    } else if (contains_any(message, { "本当に終了しますか", "Really quit", "really quit" })) {
        frame.awaiting_command = false;
        frame.prompt.lightweight = false; // 確認の窓（修-62）
        frame.menu_open = false;
        frame.pre_game_menu = false;
        frame.prompt.text_utf8 = message;
        add_prompt_choice(frame, tr("はい", "Yes"), 'y');
        add_prompt_choice(frame, tr("いいえ", "No"), 'n');
        frame.prompt.footer_utf8 = tr("左右/上下:選択  A:決定", "Left/Right/Up/Down: select  A: OK");
    } else if (contains_any(message, { "左手と右手のどちら", "Left or Right ring", "left or right ring",
                   "Left hand or right hand", "left hand or right hand", "L（左）かR（右）" })) {
        frame.awaiting_command = false;
        frame.prompt.lightweight = true;
        frame.menu_open = false;
        frame.pre_game_menu = false;
        frame.prompt.text_utf8 = message;
        add_prompt_choice(frame, tr("左手", "Left hand"), 'l');
        add_prompt_choice(frame, tr("右手", "Right hand"), 'r');
        add_prompt_choice(frame, tr("戻る", "Back"), 27);
        frame.prompt.footer_utf8 = tr("左右/上下:選択  A:決定  B:戻る", "Left/Right/Up/Down: select  A: OK  B: back");
    } else if (contains_any(message, { "調べるコマンドは", "Character you want help for", "character you want help for" })) {
        fill_lightweight_raw_prompt(frame, message.empty() ? tr("調べるコマンドを1文字入力", "Type the command to look up") : message, true);
    } else if (contains_any(message, { "調べる記号は", "What do you want identified", "what do you want identified" })) {
        fill_lightweight_raw_prompt(frame, message.empty() ? tr("調べる記号を1文字入力", "Type the symbol to identify") : message, false);
    } else if (contains_any(message, { "Which monster", "which monster", "wipe out" })) {
        fill_lightweight_raw_prompt(frame, message.empty() ? tr("対象モンスターを1文字入力", "Type the monster to target") : message, false);
    }
    /*!
     * 溜まっている音を汲んで `frame.sounds` へ移す。
     * **汲んだら消える**（`rl_sound_take`）。一度きりの出来事で、載ったその 1 枚を
     * 逃すと二度と来ない（§2.3）。Frox（`fc_frame.cpp` の `fill_sounds`）と同じ流儀。
     */
    {
        rl_sound_event raw[32];
        const int n = rl_sound_take(raw, 32);
        frame.sounds.clear();
        if (n > 0) {
            frame.sounds.reserve(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) {
                if (raw[i].name[0] == '\0') continue;
                SoundEvent s{};
                s.name = raw[i].name;
                s.y = static_cast<std::int16_t>(raw[i].y);
                s.x = static_cast<std::int16_t>(raw[i].x);
                frame.sounds.push_back(std::move(s));
            }
        }
    }
    return frame;
}

} // namespace rogue_legacy

/* ==================================================================== 魔法・投射物の効果（projections / missiles）
 *
 * Rogue 3.6 と 5.4 に共通。原作へ足した観測の呼び出し
 * （`tools/targeting/native_fx_overlay.py` が組み立てのたびに patched-src の写しへ足す。レシピには触れない。記録するだけで
 * ゲームのルールは変えない）がここを呼ぶ。積む先は共通の `presentation/bridge/named_attack_fx.h`。
 *
 * - 原作は座標を (行 y, 桁 x) で持つ（先頭の行はメッセージ行）。フレームのマスは (x, y - 1)。
 * - 状態を変える杖（ポリモルフ・加速・減速ほか）・拘束の巻物は、**怪物に効いたときだけ**絵を出す（効かなかったときに出すと
 *   未識別の杖の種類が分かってしまう。決めたこと 2026-10-11）。しかも**見えている怪物**のときだけ（見えない相手へ飛ばした絵は
 *   原作の情報より多い）。マジック・ミサイルは原作もいつも描くので常に出す。
 * - 稲妻・炎・氷の杖は原作が描くマス（跳ね返りを含む）をそのまま経路にする。
 * - 投げた武器・矢は、原作が描く条件（`cansee()` のマス）のマスだけを歩にし、当たったときだけ相手のマスを足す。
 *   盲目のときは出さない。アイテムの絵は武器の名前ごとの変愚のアイテム番号（地図は記号しか持たないが、投げた武器は種類を知っている）。
 * - 属性の名前は変愚の名前（`presentation/bridge/attack_fx_attr_names.h`）。表は下の 3 つ。原作の杖・巻物・武器の名前との突き合わせは
 *   `tools/rogue_legacy/rl_attack_fx_check.py --selftest`。
 */
namespace {

//! 原作の武器の名前 → 変愚のアイテム番号と、飛ばし方（'T' 投擲・'A' 弓の矢・'B' クロスボウの矢・'S' スリングの弾）。
//! 3.6 の `w_names[]` と 5.4 の `weap_info[]` の全部。矢弾は合う発射具を構えているときだけ射撃になる。
const struct {
    const char *name;
    int item_id;
    char role;
} kFxWeaponNames[] = {
    { "mace", 53, 'T' },
    { "long sword", 42, 'T' },
    { "int bow", 73, 'T' }, // 3.6 の表の綴り（原作のまま）
    { "short bow", 73, 'T' },
    { "arrow", 78, 'A' },
    { "dagger", 43, 'T' },
    { "rock", 82, 'S' },
    { "two handed sword", 37, 'T' },
    { "sling", 77, 'T' },
    { "dart", 78, 'T' },
    { "crossbow", 75, 'T' },
    { "crossbow bolt", 80, 'B' },
    { "shuriken", 83, 'T' },
    { "spear", 64, 'T' },
};

//! 原作の杖・巻物の名前 → 属性の名前と形（'T' 足元だけ・'A' 視界内の全員・'B' 見えている怪物へのボルト・'M' マジック・ミサイル・
//! 'L' 跳ね返る稲妻・炎・氷〔下の表で名前を引く〕・'N' 絵を出さない）。
const struct {
    const char *name;
    const char *attr;
    char mode;
} kFxWandNames[] = {
    { "light", "lite_weak", 'T' }, // 杖と巻物の両方
    { "striking", "", 'N' }, // 3.6 だけ。近接の一撃（原作が普通の打撃として描く）
    { "invisibility", "dark_weak", 'B' }, // 5.4 だけ
    { "lightning", "elec", 'L' },
    { "fire", "fire", 'L' },
    { "cold", "cold", 'L' },
    { "polymorph", "old_poly", 'B' },
    { "magic missile", "missile", 'M' },
    { "haste monster", "old_speed", 'B' },
    { "slow monster", "old_slow", 'B' },
    { "drain life", "hypodynamia", 'A' },
    { "nothing", "", 'N' },
    { "teleport away", "away_all", 'B' },
    { "teleport to", "telekinesis", 'B' },
    { "cancellation", "disenchant", 'B' },
    { "hold monster", "stasis", 'B' }, // 巻物
};

//! 跳ね返る杖が `fire_bolt()` などへ渡す名前（原作の綴り）→ 属性の名前。
const struct {
    const char *name;
    const char *attr;
} kFxBoltNames[] = {
    { "bolt", "elec" },
    { "flame", "fire" },
    { "ice", "cold" },
};

//! 1 件に積むマス数の上限（`named_attack_fx.h` の `kNamedMaxCells` と同じ）。
constexpr int kFxMaxSteps = 80;

//! 投擲の途中で開いている 1 発があるか（盲目のときは開かない）。
bool g_fx_throw_active = false;
//! 跳ね返る杖が描いたマス（`hb_rl_fx_bolt_cell` が足し、`hb_rl_fx_bolt_end` が 1 件にまとめる）。
std::vector<FxCell> g_fx_bolt_cells;

FxCell fx_cell(int y, int x)
{
    FxCell cell;
    cell.y = static_cast<std::int16_t>(y - 1); // 原作の先頭の行はメッセージ行
    cell.x = static_cast<std::int16_t>(x);
    return cell;
}

//! (y0, x0) の次のマスから (y1, x1) まで（両端のうち始点は入れない）を 1 マスずつ。杖の向きは 8 方向なので直線になる。
std::vector<std::pair<int, int>> fx_line(int y0, int x0, int y1, int x1)
{
    std::vector<std::pair<int, int>> out;
    const int dy = y1 - y0;
    const int dx = x1 - x0;
    const int n = std::max(std::abs(dy), std::abs(dx));
    for (int i = 1; i <= n && i <= kFxMaxSteps; ++i) {
        out.emplace_back(y0 + static_cast<int>(std::lround(static_cast<double>(dy) * i / n)),
            x0 + static_cast<int>(std::lround(static_cast<double>(dx) * i / n)));
    }
    return out;
}

ProjectionFx fx_projection(AttackFxOrigin origin, ProjectionShape shape, const char *attr, int src_y, int src_x, int impact_y, int impact_x)
{
    ProjectionFx fx;
    fx.origin = origin;
    fx.shape = shape;
    fx.attr = attr;
    const FxCell src = fx_cell(src_y, src_x);
    const FxCell impact = fx_cell(impact_y, impact_x);
    fx.src_y = src.y;
    fx.src_x = src.x;
    fx.impact_y = impact.y;
    fx.impact_x = impact.x;
    if (rl_player_blind() != 0) fx.flags |= kProjPlayerBlind;
    return fx;
}

} // namespace

namespace rogue_legacy {

void fill_attack_fx(GameFrame &frame)
{
    // 命令待ち（ほかの問い・-more- の最中でない）なら動きの終わり。開いている 1 発をそこで確定する。
    const bool settled = rl_command_input_active() != 0 && rl_child_input_active() == 0;
    attack_fx::NamedRecorder::instance().take(frame.projections, frame.missiles, settled);
}

} // namespace rogue_legacy

extern "C" void hb_rl_fx_throw(const char *weapon, int hero_y, int hero_x, int dy, int dx, int end_y, int end_x, int launcher_matches)
{
    auto &recorder = attack_fx::NamedRecorder::instance();
    g_fx_throw_active = false;
    if (weapon == nullptr || rl_player_blind() != 0 || (dy == 0 && dx == 0)) {
        recorder.end_missile();
        return;
    }
    int item_id = 0;
    char role = 'T';
    for (const auto &row : kFxWeaponNames) {
        if (std::strcmp(row.name, weapon) == 0) {
            item_id = row.item_id;
            role = row.role;
        }
    }
    MissileKind kind = MissileKind::Thrown;
    if (launcher_matches != 0) {
        if (role == 'A') kind = MissileKind::Arrow;
        else if (role == 'B') kind = MissileKind::Bolt;
        else if (role == 'S') kind = MissileKind::Shot;
    }
    recorder.begin_missile(kind, AttackFxOrigin::Player, static_cast<std::uint16_t>(item_id), std::string(), hero_y - 1, hero_x);
    // 原作は、見えているマス（`cansee()`）だけに飛ぶ物を描く。止まったマス（壁・扉・怪物）は当たったときに足す。
    int y = hero_y;
    int x = hero_x;
    for (int i = 0; i < kFxMaxSteps; ++i) {
        y += dy;
        x += dx;
        if (y == end_y && x == end_x) break;
        if (rl_can_see(y, x) != 0) recorder.missile_step(y - 1, x);
    }
    g_fx_throw_active = true;
}

extern "C" void hb_rl_fx_thrown_hit(int thrown_weapon, int y, int x)
{
    if (thrown_weapon == 0 || !g_fx_throw_active) return;
    g_fx_throw_active = false;
    auto &recorder = attack_fx::NamedRecorder::instance();
    if (rl_player_blind() != 0 || rl_can_see(y, x) == 0) {
        recorder.end_missile(); // 見えない相手に当たった：当たりは見せない
        return;
    }
    recorder.missile_step(y - 1, x);
    recorder.missile_hit();
}

extern "C" void hb_rl_fx_zap(const char *name, int hero_y, int hero_x, int end_y, int end_x, int end_is_monster)
{
    if (name == nullptr) return;
    auto &recorder = attack_fx::NamedRecorder::instance();
    for (const auto &row : kFxWandNames) {
        if (std::strcmp(row.name, name) != 0) continue;
        switch (row.mode) {
        case 'T': { // 足元
            recorder.add_projection(fx_projection(AttackFxOrigin::Player, ProjectionShape::Touch, row.attr, hero_y, hero_x, hero_y, hero_x));
            return;
        }
        case 'A': { // 視界内の全員（効いたときだけ呼ばれる）
            ProjectionFx fx = fx_projection(AttackFxOrigin::Player, ProjectionShape::Jump, row.attr, hero_y, hero_x, hero_y, hero_x);
            fx.flags |= kProjHidden | kProjAllInSight;
            recorder.add_projection(std::move(fx));
            return;
        }
        case 'B': { // 見えている怪物に効いた
            if (end_is_monster == 0 || rl_player_blind() != 0 || rl_can_see(end_y, end_x) == 0) return;
            ProjectionFx fx = fx_projection(AttackFxOrigin::Player, ProjectionShape::Bolt, row.attr, hero_y, hero_x, end_y, end_x);
            for (const auto &step : fx_line(hero_y, hero_x, end_y, end_x)) fx.path.push_back(fx_cell(step.first, step.second));
            recorder.add_projection(std::move(fx));
            return;
        }
        case 'M': { // マジック・ミサイル：原作は見えているマスに '*' を描く
            ProjectionFx fx = fx_projection(AttackFxOrigin::Player, ProjectionShape::Bolt, row.attr, hero_y, hero_x, hero_y, hero_x);
            const auto line = fx_line(hero_y, hero_x, end_y, end_x);
            for (std::size_t i = 0; i < line.size(); ++i) {
                const bool is_end = (line[i].first == end_y && line[i].second == end_x);
                if (is_end && end_is_monster == 0) break; // 止まったマスが壁・扉なら入れない
                if (rl_can_see(line[i].first, line[i].second) != 0) fx.path.push_back(fx_cell(line[i].first, line[i].second));
            }
            if (fx.path.empty()) return;
            fx.impact_y = fx.path.back().y;
            fx.impact_x = fx.path.back().x;
            recorder.add_projection(std::move(fx));
            return;
        }
        default:
            return; // 'L' は hb_rl_fx_bolt_end が、'N' は絵を出さない
        }
    }
}

extern "C" void hb_rl_fx_bolt_cell(int y, int x)
{
    if (g_fx_bolt_cells.size() < static_cast<std::size_t>(kFxMaxSteps)) g_fx_bolt_cells.push_back(fx_cell(y, x));
}

extern "C" void hb_rl_fx_bolt_end(const char *name, int from_monster, int start_y, int start_x)
{
    std::vector<FxCell> cells;
    cells.swap(g_fx_bolt_cells);
    if (name == nullptr || cells.empty()) return;
    const char *attr = "";
    for (const auto &row : kFxBoltNames) {
        if (std::strcmp(row.name, name) == 0) attr = row.attr;
    }
    if (attr[0] == '\0') return;
    ProjectionFx fx = fx_projection(from_monster != 0 ? AttackFxOrigin::Monster : AttackFxOrigin::Player, ProjectionShape::Bolt, attr, start_y,
        start_x, start_y, start_x);
    fx.path = std::move(cells);
    fx.impact_y = fx.path.back().y;
    fx.impact_x = fx.path.back().x;
    attack_fx::NamedRecorder::instance().add_projection(std::move(fx));
}

namespace rogue_legacy {

/*
 * `Rogue36Core.exe` / `Rogue54Core.exe` の `--selftest-fx`：攻撃の効果の記録を、ゲームの状態が要らない部分（跳ね返る杖の経路・
 * モンスターの炎・足元と視界内の全員の杖・絵を出さない杖）で数える。見えている怪物かの判定（`cansee()`）が要る部分は実機の検査が見る。
 * 0 なら全部合った。
 */
int attack_fx_selftest()
{
    int failures = 0;
    int checks = 0;
    const auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            std::fprintf(stderr, "[rogue-legacy] selftest-fx: FAIL %s\n", what);
            ++failures;
        }
    };
    auto &recorder = attack_fx::NamedRecorder::instance();
    std::vector<ProjectionFx> projections;
    std::vector<MissileFx> missiles;
    const auto drain = [&]() {
        projections.clear();
        missiles.clear();
        recorder.take(projections, missiles, true);
    };
    drain();

    // 跳ね返る杖：原作が描いたマスがそのまま経路。怪物の炎（5.4 の fire_bolt）は撃った者が怪物。
    hb_rl_fx_bolt_cell(10, 20);
    hb_rl_fx_bolt_cell(10, 21);
    hb_rl_fx_bolt_cell(10, 20); // 跳ね返って戻る
    hb_rl_fx_bolt_end("flame", 1, 10, 25);
    drain();
    check(projections.size() == 1 && projections[0].attr == "fire" && projections[0].origin == AttackFxOrigin::Monster, "a monster's flame is fire from the monster");
    check(projections.size() == 1 && projections[0].path.size() == 3 && projections[0].path[0].y == 9 && projections[0].path[0].x == 20, "bolt cells are screen row - 1");
    check(projections.size() == 1 && projections[0].src_y == 9 && projections[0].src_x == 25, "the monster is the source");
    hb_rl_fx_bolt_cell(3, 3);
    hb_rl_fx_bolt_end("bolt", 0, 4, 4);
    hb_rl_fx_bolt_cell(3, 3);
    hb_rl_fx_bolt_end("ice", 0, 4, 4);
    hb_rl_fx_bolt_end("bolt", 0, 4, 4); // マスが無い
    drain();
    check(projections.size() == 2 && projections[0].attr == "elec" && projections[1].attr == "cold", "bolt -> elec, ice -> cold; no cells draws nothing");
    hb_rl_fx_bolt_cell(3, 3);
    hb_rl_fx_bolt_end("unknown", 0, 4, 4);
    drain();
    check(projections.empty(), "an unknown bolt name draws nothing");

    // 足元・視界内の全員・絵を出さない杖。
    hb_rl_fx_zap("light", 5, 5, 5, 5, 0);
    hb_rl_fx_zap("drain life", 5, 5, 5, 5, 0);
    hb_rl_fx_zap("nothing", 5, 5, 5, 5, 0);
    hb_rl_fx_zap("striking", 5, 5, 5, 5, 0);
    hb_rl_fx_zap("lightning", 5, 5, 5, 5, 0); // 跳ね返る杖は bolt_end が出す
    hb_rl_fx_zap("no such wand", 5, 5, 5, 5, 0);
    drain();
    check(projections.size() == 2, "only light and drain life draw here");
    check(projections.size() == 2 && projections[0].shape == ProjectionShape::Touch && projections[0].attr == "lite_weak", "light is a Touch");
    check(projections.size() == 2 && (projections[1].flags & (kProjHidden | kProjAllInSight)) == (kProjHidden | kProjAllInSight) && projections[1].attr == "hypodynamia",
        "drain life affects everything in sight");
    // 効かなかった（怪物がいない）怪物向けの杖は絵を出さない。
    hb_rl_fx_zap("haste monster", 5, 5, 5, 8, 0);
    drain();
    check(projections.empty(), "a monster wand with no monster in the way draws nothing");

    // 投擲：盲目でなければ開いて、当たり（相手が見えないとき）は見せない。見えるかは cansee() 次第なので、ここは武器の表だけ。
    for (const auto &row : kFxWeaponNames) check(row.item_id != 0 && row.role != '\0', "weapon table row");
    for (const auto &row : kFxWandNames) check(row.mode != '\0', "wand table row");

    if (failures == 0) std::fprintf(stderr, "selftest-fx: ok (%d checks)\n", checks);
    return failures == 0 ? 0 : 1;
}

} // namespace rogue_legacy
