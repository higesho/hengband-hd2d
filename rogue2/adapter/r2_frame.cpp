#include "r2_frame.h"

#include "r2_shim.h"
#include "compat/curses.h"

#include "bridge/named_attack_fx.h"
#include "bridge/official_tile_table.h"
#include "bridge/rogue_borrowed_tiles.h"
#include "frame/cell_feature_bits.h"
#include "frame/attack_fx.h"
#include "frame/sound_event.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

constexpr uint16_t kDungeonStairs = 04u;
constexpr uint16_t kDungeonMonster = 02u;
constexpr uint16_t kDungeonHorizontalWall = 010u;
constexpr uint16_t kDungeonVerticalWall = 020u;
constexpr uint16_t kDungeonDoor = 040u;
constexpr uint16_t kDungeonFloor = 0100u;
constexpr uint16_t kDungeonTunnel = 0200u;
constexpr uint16_t kDungeonTrap = 0400u;
constexpr uint16_t kDungeonHidden = 01000u;

//! 画面の字の言語（修-56）。反映済みの言語が英語なら 2 つ目を返す。
const char *tr(const char *japanese, const char *english)
{
    return r2_language_english() != 0 ? english : japanese;
}

std::string utf8_from_codepoint(uint32_t cp)
{
    std::string out;
    if (cp <= 0x7f) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0x10ffff) {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
    return out;
}

/*
 * 原作の色（display.c の色ペア。1〜7 が WHITE〜CYAN、8〜14 がその反転）を、画面側の
 * `TERM_COLOR`（0〜15。`hd2d/render/term_colors.h` の 16 色）へ写す。ペア 0 は既定の白。
 * 反転（得点表の自分の行・mvaddstr_rogue の A_REVERSE）は前景と同じ色を背景にも入れて表す。
 */
void term_colors_from_attr(uint32_t attr, uint8_t &fg, uint8_t &bg)
{
    // 0:既定(白) 1:WHITE 2:RED 3:GREEN 4:YELLOW 5:BLUE 6:MAGENTA 7:CYAN
    static constexpr uint8_t kPairToTerm[8] = { 1, 1, 4, 5, 11, 6, 10, 14 };
    unsigned pair = (attr >> 16) & 0xffu;
    bool reverse = (attr & A_REVERSE) != 0u;
    if (pair >= 8u && pair <= 14u) {
        pair -= 7u;
        reverse = true;
    }
    fg = (pair < 8u) ? kPairToTerm[pair] : 1;
    bg = reverse ? fg : 0;
}

/*
 * Term の 1 行を色の区間つきで写す。墓標の花・勝利画面・得点表の見出しのように 1 行に
 * 何色も出る画面のために、色が変わる位置（UTF-8 のバイト位置）を `color_spans` へ積む。
 * 1 色しか無い行は `color` だけを入れて `color_spans` は空のままにする。
 */
TermMirrorLine term_mirror_line(int row)
{
    TermMirrorLine line;
    line.source_row = row;
    int last_non_space = -1;
    for (int col = 0; col < r2_term_cols(); ++col) {
        const uint32_t cp = r2_term_codepoint(row, col);
        if (cp != 0 && cp != ' ') last_non_space = col;
    }
    uint8_t first_color = 1;
    bool multi = false;
    TermColorSpan current;
    for (int col = 0; col <= last_non_space; ++col) {
        uint32_t cp = r2_term_codepoint(row, col);
        if (cp == 0) cp = ' ';
        uint8_t fg = 1;
        uint8_t bg = 0;
        term_colors_from_attr(r2_term_attr(row, col), fg, bg);
        if (col == 0) {
            first_color = fg;
            current.begin = 0;
            current.len = 0;
            current.color = fg;
        } else if (fg != current.color) {
            multi = true;
            line.color_spans.push_back(current);
            current.begin = static_cast<int>(line.text_utf8.size());
            current.len = 0;
            current.color = fg;
        }
        const std::string bytes = utf8_from_codepoint(cp);
        current.len += static_cast<int>(bytes.size());
        line.text_utf8 += bytes;
    }
    line.color = first_color;
    if (multi) {
        line.color_spans.push_back(current);
    }
    return line;
}

std::string term_line(int row)
{
    std::string line;
    int last_non_space = -1;
    for (int col = 0; col < r2_term_cols(); ++col) {
        const uint32_t cp = r2_term_codepoint(row, col);
        if (cp != 0 && cp != ' ') {
            last_non_space = col;
        }
    }
    for (int col = 0; col <= last_non_space; ++col) {
        uint32_t cp = r2_term_codepoint(row, col);
        if (cp == 0) cp = ' ';
        line += utf8_from_codepoint(cp);
    }
    return line;
}

/*
 * 端末のセルは 1 字 1 セル（全角も 1 セル）だが、`frame.menu_term_curs_col` は画面の桁
 * （全角は 2 桁。`modern_column_byte` の数え方）で渡す約束。セルの番号のまま渡すと、
 * 日本語の値を消したときにカーソルが見出しの中にあることになり、見出しごと入力の中身に
 * 見えていた（2026-09-26。Rogue 3.6/5.4 の rl_frame.cpp と同じ手当て）。
 */
int display_column(int row, int cell_col)
{
    int width = 0;
    for (int col = 0; col < cell_col && col < r2_term_cols(); ++col) {
        const uint32_t cp = r2_term_codepoint(row, col);
        const bool wide = cp >= 0x1100 && (cp <= 0x115f || cp == 0x2329 || cp == 0x232a
            || (cp >= 0x2e80 && cp <= 0xa4cf) || (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xf900 && cp <= 0xfaff)
            || (cp >= 0xfe10 && cp <= 0xfe6f) || (cp >= 0xff01 && cp <= 0xff60) || (cp >= 0xffe0 && cp <= 0xffe6)
            || cp >= 0x1f300);
        width += wide ? 2 : 1;
    }
    return width;
}

uint16_t feature_flags(const r2_cell_state &state, int row, int col)
{
    uint32_t visible_codepoint = state.codepoint;
    if ((visible_codepoint == 0 || visible_codepoint == ' ')
        && (state.dungeon_flags & kDungeonMonster) != 0u && state.under_codepoint != 0) {
        visible_codepoint = state.under_codepoint;
    }
    if (visible_codepoint == 0 || visible_codepoint == ' ') {
        return 0;
    }
    const uint16_t raw = state.dungeon_flags;
    if (raw == 0u) {
        /*
         * 原作は岩盤（dungeon が 0 のマス）に地図の字を描かない。字が見えていれば、それは
         * 地図の上に重なった文字である。既知にすると minimap_kind() の既定値 Floor に落ち、
         * HD2D に歩けない床が残る。
         */
        return 0;
    }
    uint16_t flags = CELL_FEAT_KNOWN | CELL_FEAT_GLOWING;
    if ((raw & (kDungeonHorizontalWall | kDungeonVerticalWall)) != 0
        || ((raw & kDungeonDoor) != 0 && (raw & kDungeonHidden) != 0)) {
        /*
         * RogueClone2 の秘密扉は dungeon 上では DOOR|HIDDEN だが、原作端末では
         * 発見されるまで '-' / '|' の壁として描かれ、is_passable() も通行不可にする。
         * ここで WALL を立てないと minimap_kind() の既定値 Floor に落ち、HD2D だけ
         * 「壁が無い床」に見えるのに原作側では進入できないマスになる。
         * KNOWN 判定はこの前段で実際の端末表示を確認済みなので、秘密扉の存在を
         * 未探索領域へ漏らすことはない。
         */
        flags |= CELL_FEAT_WALL;
    }
    if ((raw & kDungeonDoor) != 0 && (raw & kDungeonHidden) == 0) {
        flags |= CELL_FEAT_DOOR;
    }
    if ((raw & kDungeonStairs) != 0) {
        flags |= CELL_FEAT_STAIRS;
    }
    if ((raw & (kDungeonFloor | kDungeonTunnel | kDungeonStairs | kDungeonTrap)) != 0
        && (flags & CELL_FEAT_WALL) == 0 && (flags & CELL_FEAT_DOOR) == 0) {
        flags |= CELL_FEAT_PASSABLE;
    }
    if (row == r2_player_row() && col == r2_player_col()) {
        flags |= CELL_FEAT_PLAYER | CELL_FEAT_PASSABLE;
    }
    return flags;
}

class RogueTerrainMemory {
public:
    void follow(int level)
    {
        if (level_ == level) return;
        level_ = level;
        kinds_.fill(static_cast<uint8_t>(MinimapKind::Unknown));
    }

    MinimapKind resolve(std::size_t index, MinimapKind current, bool map_visible)
    {
        if (index >= kinds_.size()) return current;
        if (!map_visible) {
            return static_cast<MinimapKind>(kinds_[index]);
        }
        if (current != MinimapKind::Unknown) {
            kinds_[index] = static_cast<uint8_t>(current);
            return current;
        }
        const auto remembered = static_cast<MinimapKind>(kinds_[index]);
        return remembered != MinimapKind::Unknown ? remembered : current;
    }

private:
    int level_{-32768};
    std::array<uint8_t, 80u * 22u> kinds_{};
};

MinimapKind minimap_kind(uint16_t flags)
{
    if ((flags & CELL_FEAT_KNOWN) == 0u) return MinimapKind::Unknown;
    if ((flags & CELL_FEAT_WALL) != 0u) return MinimapKind::Wall;
    if ((flags & CELL_FEAT_DOOR) != 0u) return MinimapKind::Door;
    if ((flags & CELL_FEAT_STAIRS) != 0u) return MinimapKind::Stairs;
    return MinimapKind::Floor;
}

char ascii_fallback(uint32_t cp)
{
    return (cp >= 0x20 && cp <= 0x7e) ? static_cast<char>(cp) : ' ';
}

uint16_t monster_tile_index(int rogue_monster_id)
{
    /*
     * 並びは原作 m_names[]（mesg 307〜332）。番号は変愚の MonraceDefinitions.jsonc の id。
     * 5.4 と同じ記号でも RogueClone2 は中身が違う物がある（J＝巨大トカゲ・T＝巨人・
     * P＝幽霊）。2026-09-28 に名前へ当て直した。変愚にいない種族は近い物で代える。
     */
    static constexpr std::array<int, 26> kHengbandMonsterId = {
        presentation::rogue_own_tile(1), // A 水ごけの怪物 → 生成した RLaquator（rogue_borrowed_tiles.h）
        114,  // B 大こうもり → Giant brown bat
        presentation::frox_player(51), // C ケンタウロス → Frox の種族 Centaur（FP51）
        589,  // D ドラゴン → Mature red dragon
        presentation::frox_monster(1327), // E 大うずら → Frox の Cassowary（地上の鳥。rogue_borrowed_tiles.h）
        1327, // F はえとりぐさ → Piranha Plant
        279,  // G 翼ライオン → Griffon
        118,  // H 小鬼 → Snaga
        570,  // I 氷の怪物 → Ice elemental
        82,   // J 巨大トカゲ → Cave lizard
        1315, // K 大はやぶさ → Hawk
        258,  // L 金持ち妖精 → Cheerful leprechaun
        presentation::rogue_own_tile(2), // M メデューサ → 生成した RLmedusa（蛇の髪の女性。rogue_borrowed_tiles.h）
        248,  // N ニンフ → Nixie
        126,  // O 欲ばり鬼 → Cave orc
        477,  // P 幽霊 → Ghost
        presentation::frox_monster(1081), // Q 大つのじか → Frox の Reindeer（変愚に鹿はいない）
        119,  // R がらがらへび → Rattlesnake
        28,   // S へび → Large brown snake
        255,  // T 巨人 → Hill giant
        1019, // U 一角獣 → Kokuo, Raou's Steed（黒王号。黒い馬）
        432,  // V バンパイア → Vampire
        416,  // W 死霊 → White wraith
        353,  // X 物まねの怪物 → Chest mimic
        154,  // Y 雪男 → Yeti
        229,  // Z ゾンビ → Zombified human
    };
    if (rogue_monster_id < 1 || rogue_monster_id > static_cast<int>(kHengbandMonsterId.size())) return 0;
    const int id = kHengbandMonsterId[static_cast<std::size_t>(rogue_monster_id - 1)];
    // 負の番号は Frox の板を借りる（変愚にいない種族。rogue_borrowed_tiles.h）。
    if (id < 0) return presentation::rogue_borrowed_tile(id);
    return presentation::lookup_monster_tile(static_cast<uint16_t>(id));
}

uint16_t object_tile_index(uint16_t encoded)
{
    if (encoded == 0) return 0;
    const int category = (encoded >> 6) & 0x01ff;
    const int kind = encoded & 0x003f;
    int hengband_object_id = 0;

    /* 種類 0x3f は未鑑定（r2_shim.c visible_kind）。種類を明かさないカテゴリごとの代表の絵にする。 */
    if (kind == 0x3f) {
        switch (category) {
        case 020: return presentation::lookup_object_tile(176);  // 巻物
        case 040: return presentation::lookup_object_tile(224);  // 薬
        case 0100: return presentation::lookup_object_tile(269); // 杖
        case 0200: return presentation::lookup_object_tile(510); // 指輪
        default: break;
        }
    }

    switch (category) {
    case 01:
        hengband_object_id = 488;
        break;
    case 02:
        hengband_object_id = (kind == 0) ? 21 : 24;
        break;
    case 04: {
        static constexpr std::array<int, 7> ids = { 103, 170, 108, 109, 113, 526, 116 };
        if (kind >= 0 && kind < static_cast<int>(ids.size())) hengband_object_id = ids[static_cast<std::size_t>(kind)];
        break;
    }
    case 010: {
        static constexpr std::array<int, 8> ids = { 73, 78, 78, 43, 83, 53, 42, 37 };
        if (kind >= 0 && kind < static_cast<int>(ids.size())) hengband_object_id = ids[static_cast<std::size_t>(kind)];
        break;
    }
    case 020: {
        static constexpr std::array<int, 12> ids = { 175, 176, 173, 175, 176, 176, 176, 176, 176, 176, 202, 176 }; // 10 は巻物の Aggravate Monster（642 はロッドだった）
        if (kind >= 0 && kind < static_cast<int>(ids.size())) hengband_object_id = ids[static_cast<std::size_t>(kind)];
        break;
    }
    case 040: {
        static constexpr std::array<int, 14> ids = { 225, 225, 240, 241, 248, 224, 246, 224, 224, 224, 224, 224, 224, 224 }; // 4 は Poison。11 の浮遊は変愚に水薬が無い（609 は石だった）
        if (kind >= 0 && kind < static_cast<int>(ids.size())) hengband_object_id = ids[static_cast<std::size_t>(kind)];
        break;
    }
    case 0100: {
        static constexpr std::array<int, 10> ids = { 269, 269, 278, 269, 274, 269, 269, 269, 269, 269 };
        if (kind >= 0 && kind < static_cast<int>(ids.size())) hengband_object_id = ids[static_cast<std::size_t>(kind)];
        break;
    }
    case 0200: {
        static constexpr std::array<int, 11> ids = { 510, 138, 510, 510, 132, 132, 133, 510, 510, 510, 137 };
        if (kind >= 0 && kind < static_cast<int>(ids.size())) hengband_object_id = ids[static_cast<std::size_t>(kind)];
        break;
    }
    case 0400:
        hengband_object_id = 503;
        break;
    default:
        break;
    }
    return (hengband_object_id != 0) ? presentation::lookup_object_tile(hengband_object_id) : 0;
}

uint16_t entity_tile_index(const r2_cell_state &state, uint16_t flags)
{
    if ((flags & CELL_FEAT_PLAYER) != 0u) return presentation::lookup_monster_tile(43);
    if (state.monster_id != 0) return monster_tile_index(state.monster_id);
    if (state.object_id != 0) return object_tile_index(state.object_id);
    return 0;
}

std::string clean_utf8(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size();) {
        const unsigned char lead = static_cast<unsigned char>(input[i]);
        if (lead < 0x80u) {
            out.push_back(static_cast<char>(lead));
            ++i;
            continue;
        }
        int width = 0;
        if ((lead & 0xe0u) == 0xc0u) width = 2;
        else if ((lead & 0xf0u) == 0xe0u) width = 3;
        else if ((lead & 0xf8u) == 0xf0u) width = 4;
        if (width == 0 || i + static_cast<std::size_t>(width) > input.size()) {
            ++i;
            continue;
        }
        bool valid = true;
        for (int j = 1; j < width; ++j) {
            const unsigned char next = static_cast<unsigned char>(input[i + static_cast<std::size_t>(j)]);
            if ((next & 0xc0u) != 0x80u) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            ++i;
            continue;
        }
        out.append(input.substr(i, static_cast<std::size_t>(width)));
        i += static_cast<std::size_t>(width);
    }
    return out;
}

std::string item_line(const r2_item_state &item)
{
    std::string line;
    if (item.key >= 0x20 && item.key <= 0x7e) {
        line.push_back(static_cast<char>(item.key));
        line += ") ";
    }
    line += clean_utf8(item.description);
    return line;
}

ModernAction item_action(std::string id, std::string label, int key)
{
    ModernAction action;
    action.id = std::move(id);
    action.label = std::move(label);
    action.key = key;
    return action;
}

void append_item_actions(ModernRow &row, const r2_item_state &item,
    bool armor_equipped, int rings_equipped)
{
    constexpr uint16_t kFood = 02u;
    constexpr uint16_t kArmor = 04u;
    constexpr uint16_t kWeapon = 010u;
    constexpr uint16_t kScroll = 020u;
    constexpr uint16_t kPotion = 040u;
    constexpr uint16_t kWand = 0100u;
    constexpr uint16_t kRing = 0200u;
    constexpr uint16_t kWielded = 01u;
    constexpr uint16_t kWorn = 02u;
    constexpr uint16_t kOnEitherHand = 014u;

    switch (item.category) {
    case kFood:
        row.actions.push_back(item_action("eat", tr("食べる", "Eat"), 'e'));
        break;
    case kArmor:
        if ((item.in_use_flags & kWorn) != 0u) {
            /* 呪いは外そうとしたときに原作が初めて知らせる。項目の有無で先に漏らさない。 */
            row.actions.push_back(item_action("takeoff", tr("脱ぐ", "Take off"), 'T'));
        } else if (!armor_equipped) {
            row.actions.push_back(item_action("wear", tr("装備する", "Wear"), 'W'));
        }
        break;
    case kWeapon:
        if ((item.in_use_flags & kWielded) == 0u) {
            row.actions.push_back(item_action("wield", tr("武器として装備", "Wield as weapon"), 'w'));
        }
        row.actions.push_back(item_action("throw", tr("投げる", "Throw"), 't'));
        break;
    case kScroll:
        row.actions.push_back(item_action("read", tr("読む", "Read"), 'r'));
        break;
    case kPotion:
        row.actions.push_back(item_action("quaff", tr("飲む", "Quaff"), 'q'));
        break;
    case kWand:
        row.actions.push_back(item_action("zap", tr("振る", "Zap"), 'z'));
        break;
    case kRing:
        if ((item.in_use_flags & kOnEitherHand) == 0u) {
            if (rings_equipped < 2) row.actions.push_back(item_action("put_ring", tr("指輪をはめる", "Put on ring"), 'P'));
        } else {
            row.actions.push_back(item_action("remove_ring", tr("指輪を外す", "Remove ring"), 'R'));
        }
        break;
    default:
        break;
    }
    row.actions.push_back(item_action("inspect", tr("詳しく調べる", "Inspect"), 'I'));
    if (item.callable) row.actions.push_back(item_action("call", tr("名前を付ける", "Name it"), 'c'));
    row.actions.push_back(item_action("drop", tr("捨てる", "Drop"), 'd'));
}

void add_choice(GameFrame &frame, int line_index, int key, std::string label)
{
    MenuChoice choice{};
    choice.line_index = line_index;
    choice.key = key;
    choice.label_utf8 = std::move(label);
    frame.menu_choices.push_back(std::move(choice));
}

void fill_options_semantics(GameFrame &frame)
{
    if (frame.text_input_active || frame.menu_term_curs_row < 0
        || frame.menu_term_curs_row >= static_cast<int>(frame.menu_term_lines.size())) return;
    const auto &active = frame.menu_term_lines[static_cast<std::size_t>(frame.menu_term_curs_row)].text_utf8;
    if (active.find("(\"") == std::string::npos || active.find("\"): ") == std::string::npos) return;

    int option_rows = 0;
    for (const auto &line : frame.menu_term_lines) {
        if (line.text_utf8.find("(\"") != std::string::npos && line.text_utf8.find("\"): ") != std::string::npos) ++option_rows;
    }
    if (option_rows < 3) return;

    // The HD2D source patch now keeps only gameplay-relevant boolean options.
    // Do not turn the current y/n wait into another choice menu.  Publish the
    // native row as a cursor instead; NativeModernMenu renders the complete
    // option table and maps left/right back to y/n.
    frame.menu_choices.clear();
    frame.menu_core_cursor = {};
    frame.menu_core_cursor.line_index = frame.menu_term_curs_row;
    frame.menu_core_cursor.key = 13;
    frame.menu_core_cursors.assign(1, frame.menu_core_cursor);
    frame.awaiting_command = false;
}

void fill_char_prompt_semantics(GameFrame &frame, int kind, const std::string &current_message)
{
    if (kind == 0) return;
    frame.menu_open = false;
    frame.pre_game_menu = false;
    frame.awaiting_command = false;
    frame.menu_choices.clear();
    frame.prompt = {};
    frame.prompt.lightweight = true;
    frame.prompt.text_utf8 = current_message;
    frame.prompt.line_index = -1;

    const auto add_prompt = [&](int key, std::string label) {
        PromptChoice choice{};
        choice.key = key;
        choice.label_utf8 = std::move(label);
        frame.prompt.choices.push_back(std::move(choice));
    };

    if (kind == 2) {
        add_prompt('?', tr("巻物", "Scrolls"));
        add_prompt('!', tr("薬", "Potions"));
        add_prompt('/', tr("杖", "Wands"));
        add_prompt('=', tr("指輪", "Rings"));
        add_prompt('*', tr("すべて", "All"));
        add_prompt(27, tr("戻る", "Back"));
        frame.prompt.footer_utf8 = tr("上下/左右: 選択  A/Enter: 決定  B/Esc: 戻る", "Up/Down/Left/Right: select  A/Enter: confirm  B/Esc: back");
        return;
    }

    if (kind == 3) {
        // はい／いいえは、ほかのコアと同じ確認の窓にする（修-62。決めたこと）。画面下の帯（lightweight）にしない。
        frame.prompt.lightweight = false;
        add_prompt('y', tr("はい", "Yes"));
        add_prompt('n', tr("いいえ", "No"));
        frame.prompt.footer_utf8 = tr("左右/上下: 選択  A/Enter: 決定  B/Esc: いいえ", "Left/Right/Up/Down: select  A/Enter: confirm  B/Esc: No");
        return;
    }

    // Monster/symbol identification is a raw one-character input in Rogue.
    // Do not expand A-Z and punctuation into a full-screen menu.  Mark it as
    // text input so Android can show the software keyboard, while lightweight
    // keeps the map and PromptBar visible.
    frame.text_input_active = true;
    if (frame.prompt.text_utf8.empty()) frame.prompt.text_utf8 = tr("調べる記号を1文字入力", "Enter one symbol to look up");
    frame.prompt.footer_utf8 = tr("1文字入力  B/Esc: 戻る", "Enter one character  B/Esc: back");
}

bool map_has_player();

/*
 * アイテムを選ぶ問い（食べる・飲む・読むなど）。**地図の上に小さな箱で重ねる**
 * （`menu_over_map`。決めたこと 2026-10-02「対象を選ぶ必要があるときは小さく選択肢を
 * ポップアップさせよう。全画面にならないように」）。Rogue 3.6 / 5.4 の get_item と同じ形。
 * 箱の中身は問いの 1 行と選択肢だけに刈り込む。全画面の持ち物一覧へ移る `*` は出さない
 * ——当てはまるアイテムはすでに選択肢に並んでいる。
 * 原作は種類の合わない持ち物の文字も受け付ける（武器以外を投げるなど。`is_pack_letter()`）ので、
 * 合う種類を先に、残りを「その他:」を付けて後ろに並べる（決めたこと 2026-10-03「案1で対応して」）。
 */
void fill_pack_prompt_from_items(GameFrame &frame, unsigned int mask, const std::string &current_message,
    const std::vector<r2_item_state> &items, bool over_map = false)
{
    if (mask == 0) return;
    frame.menu_open = true;
    frame.pre_game_menu = false;
    frame.menu_over_map = over_map;
    frame.menu_popup = over_map;
    frame.awaiting_command = false;
    frame.menu_choices.clear();
    frame.menu_term_lines.clear();
    frame.menu_term_curs_row = -1;
    frame.menu_term_curs_col = -1;
    {
        TermMirrorLine line;
        line.source_row = 0;
        line.color = 1;
        line.text_utf8 = current_message;
        frame.menu_term_lines.push_back(std::move(line));
    }
    for (const bool matching : { true, false }) {
        for (const auto &item : items) {
            if (((item.category & mask) != 0) != matching) continue;
            std::string label = clean_utf8(item.description);
            if (label.empty()) label = "Item";
            if (!matching) label = std::string(tr("その他: ", "Other: ")) + label;
            add_choice(frame, 0, item.key, std::move(label));
        }
    }
    add_choice(frame, 0, 27, tr("戻る", "Back"));
}

void fill_pack_prompt_semantics(GameFrame &frame, unsigned int mask, const std::string &current_message)
{
    if (mask == 0) return;
    std::vector<r2_item_state> items;
    const int count = r2_inventory_count();
    items.reserve(static_cast<std::size_t>((std::max)(0, count)));
    for (int i = 0; i < count; ++i) {
        r2_item_state item{};
        if (r2_inventory_item(i, &item)) items.push_back(item);
    }
    fill_pack_prompt_from_items(frame, mask, current_message, items, map_has_player());
}

bool screen_has_player()
{
    const int row = r2_player_row();
    const int col = r2_player_col();
    return row >= 0 && row < r2_term_rows() && col >= 0 && col < r2_term_cols()
        && r2_term_codepoint(row, col) == '@';
}

/*
 * 地図（一覧を重ねる前の画面。`r2_term_map_codepoint()`）に '@' が見えているか。
 * 地形の記憶を更新してよいかはこちらで決める。`screen_has_player()` は全画面メニューの
 * 判定に使うので、いまの端末のまま残す。
 */
bool map_has_player()
{
    const int row = r2_player_row();
    const int col = r2_player_col();
    return row >= 0 && row < r2_term_rows() && col >= 0 && col < r2_term_cols()
        && r2_term_map_codepoint(row, col) == '@';
}

/*!
 * @brief 溜まっている音を汲んで `frame.sounds` へ移す。
 * @details **汲んだら消える**（`r2_sound_take`）。RogueClone2 は音の道のりを数えていないので
 * `heard_*` と `path` は載せない——画面側は 0 を「計算していない」と読んで実際のマスと
 * 直線距離へ落ちる（`presentation/frame/sound_event.h` の約束）。
 */
void fill_sounds(GameFrame &frame)
{
    r2_sound_event raw[32];
    const int n = r2_sound_take(raw, 32);

    frame.sounds.clear();
    if (n <= 0) {
        return;
    }
    frame.sounds.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const r2_sound_event &e = raw[i];
        if (e.name[0] == '\0') {
            continue;
        }
        SoundEvent s{};
        s.name = e.name;
        s.y = e.row;
        s.x = e.col;
        frame.sounds.push_back(std::move(s));
    }
}

} // namespace

namespace rogue2 {

GameFrame capture_frame(std::uint64_t frame_id)
{
    GameFrame frame;
    frame.frame_id = frame_id;
    frame.view_w = 80;
    frame.view_h = 22;
    frame.player_gx = r2_player_col();
    frame.player_gy = std::max(0, r2_player_row() - 1);
    frame.cam_x = frame.player_gx;
    frame.cam_y = frame.player_gy;

    /*
     * HD2D の地形は `cells` の記号から直接組み立てず、フロア全域の `minimap` から
     * 部屋・通路・壁の並びを読んで作る。RogueClone2 はマップそのものが固定 80x22 なので、
     * 端末の地図領域をそのままフロア全域として渡せる。
     *
     * 注意: `dungeon` の生のフラグだけでは未発見の秘密扉・罠まで見えてしまう。
     * `feature_flags()` は「端末上で実際に見えているマス」にだけ KNOWN を立て、秘密扉も
     * 表示されている壁として扱うので、ここでもその結果だけを使う。
     */
    frame.minimap.width = 80;
    frame.minimap.height = 22;
    frame.minimap.player_gx = frame.player_gx;
    frame.minimap.player_gy = frame.player_gy;
    frame.minimap.kinds.assign(80u * 22u, static_cast<uint8_t>(MinimapKind::Unknown));
    frame.minimap.markers.assign(80u * 22u, static_cast<uint8_t>(MinimapMarker::None));

    frame.floor.dungeon_id = 1; // RogueClone2 は 1 つのダンジョンだけを持つ。
    frame.floor.dun_level = r2_dungeon_level();
    frame.floor.kind = static_cast<int>(FloorKind::Dungeon);
    frame.floor.place_name_utf8 = "RogueClone2";
    frame.lighting.daytime = false;
    frame.lighting.day_minute = 0;

    const bool player_visible = screen_has_player();
    const bool map_visible = map_has_player();
    static RogueTerrainMemory terrain_memory;
    terrain_memory.follow(frame.floor.dun_level);

    frame.hud.name = clean_utf8(r2_player_name());
    frame.hud.hp = r2_player_hp();
    frame.hud.hp_max = r2_player_hp_max();
    frame.hud.gold = static_cast<int>(r2_player_gold());
    frame.hud.level = r2_player_level();
    frame.hud.depth = r2_dungeon_level();
    frame.hud.status_line = clean_utf8(r2_hunger_text());

    /*
     * 状態のアイコン。空腹・盲目・混乱・幻覚・浮遊・加速・透明視・
     * 怪物感知・捕まっている・鑑定済みの指輪の効果。名前は日本語（原作の字）。集め方は r2_shim.c の r2_status_effects。
     */
    if (map_visible) r2_test_status_apply(); //!< 環境変数が無ければ何もしない（r2_shim.c）
    if (map_visible) r2_test_fx_items_apply(); //!< 環境変数 HENGBAND_TEST_FX_ITEMS が無ければ何もしない（r2_shim.c）
    {
        r2_status_effect effects[24];
        const int count = r2_status_effects(effects, 24);
        for (int i = 0; i < count; ++i) {
            StatusEffectView view;
            view.key = effects[i].key;
            view.label_utf8 = clean_utf8(effects[i].label);
            view.amount_utf8 = effects[i].amount;
            view.kind = static_cast<StatusEffectKind>(effects[i].kind);
            frame.status_effects.push_back(std::move(view));
        }
    }

    frame.cells.reserve(80 * 22);
    /*
     * 光の届く範囲（・CORE_HUD_ROLLOUT.md §2.2。決めたこと 2026-10-11）。
     * 光源は松明風（`light_kind` 1・半径 1）。`player_lit` は `cells` と同じ並びで、自分の周り 3x3（原作の
     * `rogue_is_around()`）を目が見えているときだけ立てる。いま見えていない記憶のマスは `light_level` 1
     * （少し暗い。見えなくはしない）、いま見えているマス（同じ部屋か隣。原作の `rogue_can_see()`）は 2。
     * 地図が出ていない画（誕生前・全画面の窓）はゲームの状態が定まらないので、従来どおり送らない。
     */
    const bool lighting_on = map_visible;
    std::vector<std::uint8_t> player_lit;
    if (lighting_on) {
        frame.lighting.light_kind = 1;
        frame.lighting.light_radius = 1;
        // いま見えていない記憶のマス（light_level 1）を少し暗くしてほしい、という申告。画面側は申告のあるコアだけ暗くする。
        frame.lighting.memory_dim = 1;
        player_lit.assign((80u * 22u + 7u) / 8u, 0);
    }
    std::unordered_set<int> visible_monsters;
    for (int row = 1; row < 23; ++row) {
        for (int col = 0; col < 80; ++col) {
            r2_cell_state state{};
            if (!r2_read_cell(row, col, &state)) continue;
            MapCellView cell;
            cell.gx = static_cast<int16_t>(col);
            cell.gy = static_cast<int16_t>(row - 1);
            cell.feature_flags = feature_flags(state, row, col);
            cell.terrain_id = 0;
            const bool visible = (cell.feature_flags & CELL_FEAT_KNOWN) != 0u;
            cell.monster_id = visible ? state.monster_id : 0;
            cell.object_id = visible ? state.object_id : 0;
            cell.tile_index = visible ? entity_tile_index(state, cell.feature_flags) : 0;
            cell.ascii_fallback = state.monster_id >= 1 && state.monster_id <= 26
                ? static_cast<char>('A' + state.monster_id - 1)
                : ascii_fallback(state.codepoint);
            cell.light_level = (cell.feature_flags & CELL_FEAT_KNOWN) != 0 ? 2 : 0;
            if (lighting_on && visible) {
                const bool lit = r2_light_reaches(row, col) != 0;
                if (lit) {
                    const std::size_t at = frame.cells.size();
                    player_lit[at / 8] = static_cast<std::uint8_t>(player_lit[at / 8] | (1u << (at % 8)));
                }
                // 光が届く／いま見えている＝2。見えていない記憶のマスは少し暗く＝1（0 は「知らない」なので使わない）。
                cell.light_level = (lit || r2_can_see(row, col) != 0) ? 2 : 1;
            }
            /* 原作の色（COLOR。設定 color / map で決まる）をマスの前景色として運ぶ。 */
            term_colors_from_attr(state.attr, cell.fg_color, cell.bg_color);
            const std::size_t map_index
                = static_cast<std::size_t>(row - 1) * 80u + static_cast<std::size_t>(col);
            MinimapKind kind = terrain_memory.resolve(map_index, minimap_kind(cell.feature_flags), map_visible);
            if ((cell.feature_flags & CELL_FEAT_PLAYER) != 0u) {
                kind = MinimapKind::Player;
            }
            frame.minimap.kinds[map_index] = static_cast<uint8_t>(kind);
            if (visible && (cell.feature_flags & CELL_FEAT_PLAYER) == 0u) {
                if (cell.monster_id != 0) {
                    frame.minimap.markers[map_index] = static_cast<uint8_t>(MinimapMarker::Monster);
                } else if (cell.object_id != 0) {
                    const int category = (cell.object_id >> 6) & 0x01ff;
                    frame.minimap.markers[map_index] = static_cast<uint8_t>(
                        category == 01 ? MinimapMarker::Treasure : MinimapMarker::Item);
                } else if ((state.dungeon_flags & kDungeonTrap) != 0u) {
                    frame.minimap.markers[map_index] = static_cast<uint8_t>(MinimapMarker::Trap);
                }
            }
            if (visible && (cell.feature_flags & CELL_FEAT_STAIRS) != 0u
                && (cell.feature_flags & CELL_FEAT_PLAYER) == 0u && cell.monster_id == 0 && cell.object_id == 0) {
                /*
                 * 階段は画像タイルの実体ではなく、共通の CellRole::Stairs として描く。
                 * これで変愚などと同じ立体の階段プレハブになる。RogueClone2 の原作は
                 * 1 本の階段を「通常は下り、イェンダーの魔除け取得後は上り」に共用する。
                 */
                cell.tile_index = 0;
                cell.ascii_fallback = '%';
            }
            if (visible && (state.dungeon_flags & kDungeonTrap) != 0u && state.codepoint == '^'
                && cell.monster_id == 0 && cell.object_id == 0) {
                /* 原作は罠を種類を問わず '^' で見せる（種類は ^ で調べたときの文だけ）。
                 * 種類で絵を変えると正体が漏れるので、代表の罠の絵（ダーツの罠）に統一する。 */
                const int terrain_id = 24;
                cell.object_id = static_cast<std::uint16_t>(0xff00u);
                cell.tile_index = presentation::lookup_terrain_tile(terrain_id);
                cell.ascii_fallback = '^';
            }
            frame.cells.push_back(cell);
            if (cell.monster_id != 0) visible_monsters.insert(cell.monster_id);
        }
    }

    if (lighting_on && !frame.cells.empty()) {
        player_lit.resize((frame.cells.size() + 7) / 8); // cells の数ぶんに切り詰めて渡す
        frame.lighting.player_lit = std::move(player_lit);
    }

    for (const int monster_id : visible_monsters) {
        const std::string name = clean_utf8(r2_monster_name(monster_id));
        if (!name.empty()) frame.sub3_lines.push_back(name);
    }
    if (frame.sub3_lines.empty()) frame.sub3_lines.push_back(tr("(なし)", "(none)"));

    const int item_count = r2_inventory_count();
    int shown_items = 0;
    for (int i = 0; i < item_count && shown_items < 11; ++i) {
        r2_item_state item{};
        if (!r2_inventory_item(i, &item)) continue;
        frame.sub5_lines.push_back(item_line(item));
        ++shown_items;
    }
    if (item_count > shown_items) {
        frame.sub5_lines.push_back("…(+" + std::to_string(item_count - shown_items) + ")");
    }

    std::string weapon;
    std::string armor;
    std::string left_ring;
    std::string right_ring;
    for (int i = 0; i < item_count; ++i) {
        r2_item_state item{};
        if (!r2_inventory_item(i, &item)) continue;
        const std::string description = clean_utf8(item.description);
        if ((item.in_use_flags & 01u) != 0u) weapon = description;
        if ((item.in_use_flags & 02u) != 0u) armor = description;
        if ((item.in_use_flags & 04u) != 0u) left_ring = description;
        if ((item.in_use_flags & 010u) != 0u) right_ring = description;
    }
    frame.sub2_lines.push_back(std::string(tr("武器: ", "Weapon: ")) + (weapon.empty() ? std::string(tr("(なし)", "(none)")) : weapon));
    frame.sub2_lines.push_back(std::string(tr("防具: ", "Armor: ")) + (armor.empty() ? std::string(tr("(なし)", "(none)")) : armor));
    frame.sub2_lines.push_back(std::string(tr("左手: ", "Left hand: ")) + (left_ring.empty() ? std::string(tr("(なし)", "(none)")) : left_ring));
    frame.sub2_lines.push_back(std::string(tr("右手: ", "Right hand: ")) + (right_ring.empty() ? std::string(tr("(なし)", "(none)")) : right_ring));

    const int history_count = r2_message_history_count();
    const int history_first = std::max(0, history_count - 32);
    frame.messages.reserve(static_cast<std::size_t>(history_count - history_first));
    for (int i = history_first; i < history_count; ++i) {
        uint32_t seq = 0;
        const char *text = r2_message_history_text(i, &seq);
        if (text == nullptr) continue;
        MessageEvent event;
        event.seq = seq;
        event.color = 1;
        event.text_utf8 = clean_utf8(text);
        if (!event.text_utf8.empty()) frame.messages.push_back(std::move(event));
    }
    const bool modal = r2_term_modal_active() != 0;
    const bool inline_continue = modal && player_visible;
    const std::string current_message = term_line(0);
    if (inline_continue && !current_message.empty() && !frame.messages.empty()) {
        /*
         * message() は次の本文を履歴へ積んだ後、直前の本文に［続く］を付けて Space を待つ。
         * 待っている間だけ、パネルの最下行を実際の行0へ差し替える。Space 後のフレームでは
         * 履歴に積んだ次本文へ自然に戻るので、全画面へ切り替える必要がない。
         */
        frame.messages.back().text_utf8 = current_message;
    }
    if (frame.messages.empty() && !current_message.empty()) {
        MessageEvent event;
        event.seq = 1;
        event.color = 1;
        event.text_utf8 = current_message;
        frame.messages.push_back(std::move(event));
    }

    frame.bottom_row_cols = 80;
    const std::string status = term_line(23);
    if (!status.empty()) {
        frame.bottom_row_runs.push_back(TermTextRun{ 0, status, 1 });
    }

    /*
     * 画面全体から '@' を探してはいけない。ヘルプ自身に '@ プレイヤー行を描き直す'
     * が含まれるため、以前はヘルプを通常ゲーム画面と誤認して「次へ」を無効化していた。
     * Space 待ちは原作側から明示し、通常画面は実際のプレイヤー座標だけで判定する。
     */
    /*
     * 移動処理は、古い位置の '@' を消してから新しい位置へ描き直す。その途中の refresh() で
     * screen_has_player() が一瞬だけ false になる。これをメニュー扱いすると、新UIの詳細画面が
     * 1 フレームだけ開いて移動のたびにちらつく。
     *
     * 通常プレイ中の全画面表示は wait_for_ack()/options() などが r2_term_modal_begin() を
     * 明示するので、プレイヤーが消えただけではメニューにしない。ゲーム開始前だけは HP 最大値が
     * まだ無いので、従来どおり端末画面をメニューとして扱う。
     */
    const bool pre_game = r2_player_hp_max() <= 0 && !player_visible;
    /*
     * 通常プレイ中の［続く］は行0のメッセージ待ちにすぎない。地図とプレイヤーが残って
     * いるなら全画面メニューへ切り替えず、メッセージパネル内でそのまま進める。
     * ヘルプ・得点表など、プレイヤーが消える本当の全画面表示だけ menu_open にする。
     */
    frame.text_input_active = r2_term_text_input_active() != 0;
    if (r2_term_numeric_active()) {
        frame.numeric.active = true;
        frame.numeric.prompt_utf8 = tr("繰り返し回数 (1-999): ", "Repeat count (1-999): ");
        frame.numeric.min = 1;
        frame.numeric.max = 999;
        frame.numeric.value = std::clamp(r2_term_numeric_value(), 0, 999);
        frame.numeric.digits = 3;
        frame.numeric.text_len = static_cast<int>(std::to_string(frame.numeric.value).size());
    }
    frame.menu_open = (modal && !player_visible) || pre_game;
    frame.pre_game_menu = frame.menu_open && r2_player_hp_max() <= 0;
    if (frame.menu_open || frame.text_input_active) {
        const int cursor_row = r2_term_cursor_row();
        for (int row = 0; row < 24; ++row) {
            TermMirrorLine line = term_mirror_line(row);
            if (line.text_utf8.empty()) continue;
            if (row == cursor_row) {
                frame.menu_term_curs_row = static_cast<int>(frame.menu_term_lines.size());
                frame.menu_term_curs_col = display_column(row, r2_term_cursor_col());
            }
            frame.menu_term_lines.push_back(std::move(line));
        }
    }
    fill_options_semantics(frame);
    const int char_prompt_kind = r2_term_char_prompt_kind();
    const unsigned int pack_prompt_mask = r2_term_pack_prompt_mask();
    fill_char_prompt_semantics(frame, char_prompt_kind, current_message);
    fill_pack_prompt_semantics(frame, pack_prompt_mask, current_message);
    /* ［続く］中は A/Enter を通常コマンドメニューに奪わせない。生の Enter はコア側で Space へ変換する。 */
    frame.awaiting_command = r2_term_command_input_active() != 0
        && !frame.menu_open && !inline_continue && !frame.text_input_active && !frame.numeric.active
        && char_prompt_kind == 0 && pack_prompt_mask == 0u;
    fill_sounds(frame);
    return frame;
}

std::vector<ModernRow> inventory_rows(unsigned int category_mask)
{
    const int count = r2_inventory_count();
    std::vector<r2_item_state> items;
    items.reserve(static_cast<std::size_t>((std::max)(0, count)));
    bool armor_equipped = false;
    int rings_equipped = 0;
    for (int i = 0; i < count; ++i) {
        r2_item_state item{};
        if (!r2_inventory_item(i, &item)) continue;
        if ((item.in_use_flags & 02u) != 0u) armor_equipped = true;
        if ((item.in_use_flags & 014u) != 0u) ++rings_equipped;
        items.push_back(item);
    }

    std::vector<ModernRow> rows;
    rows.reserve(items.size());
    for (const auto &item : items) {
        if (category_mask != 0u && (item.category & category_mask) == 0u) continue;
        ModernRow row;
        row.id = "item:";
        if (item.key >= 0x20 && item.key <= 0x7e) row.id.push_back(static_cast<char>(item.key));
        row.label = clean_utf8(item.description);
        row.description = row.label;
        row.count = std::max(1, item.quantity);
        row.key = item.key;
        row.hotkey = item.key;
        // NativeInventoryMenu re-finds the selected object after the original
        // command opens its filtered pack prompt.  A filtered list must not
        // renumber the object, otherwise the root inventory row no longer
        // matches the child prompt.  Rogue pack letters are stable while the
        // prompt is active, so use them as the persistent slot identity and
        // keep all RogueClone2 inventory rows in the same source (selection=0).
        row.selection = 0;
        row.slot = item.key;
        row.tab = 0;
        row.tile = object_tile_index(item.object_id);
        row.equipped = item.in_use_flags != 0;
        row.glyph = "?";
        append_item_actions(row, item, armor_equipped, rings_equipped);
        rows.push_back(std::move(row));
    }
    return rows;
}

int semantic_selftest()
{
    int failures = 0;
    const auto check = [&](bool ok, const char *what) {
        if (ok) return;
        std::fprintf(stderr, "[rogue2] semantic selftest FAIL: %s\n", what);
        ++failures;
    };
    const auto has_key = [](const GameFrame &frame, int key) {
        return std::any_of(frame.menu_choices.begin(), frame.menu_choices.end(),
            [key](const MenuChoice &choice) { return choice.key == key; });
    };
    const auto option_frame = [](std::string active) {
        GameFrame frame;
        frame.menu_term_curs_row = 0;
        for (int i = 0; i < 3; ++i) {
            TermMirrorLine line;
            line.source_row = i;
            line.text_utf8 = (i == 0) ? active : ("Option (\"x\"): " + std::to_string(i));
            frame.menu_term_lines.push_back(std::move(line));
        }
        return frame;
    };

    {
        /* 未鑑定の薬・巻物・杖・指輪（種類 0x3f）は、本当の種類によらずカテゴリごとの代表の絵 1 つ。 */
        for (const unsigned category : { 020u, 040u, 0100u, 0200u }) {
            const auto unknown = object_tile_index(static_cast<uint16_t>((category << 6) | 0x3fu));
            check(unknown != 0, "unidentified item has a representative tile");
            check(unknown == object_tile_index(static_cast<uint16_t>((category << 6) | 0x3fu)),
                "unidentified item tile is stable");
        }
        check(object_tile_index(static_cast<uint16_t>((040u << 6) | 0x3fu)) != 0
                && object_tile_index(static_cast<uint16_t>((040u << 6) | 0x3fu))
                    == object_tile_index(static_cast<uint16_t>((040u << 6) | 0x3fu)),
            "potion representative tile");
    }
    {
        auto frame = option_frame("Boolean option (\"b\"): Yes");
        fill_options_semantics(frame);
        check(frame.menu_choices.empty(), "boolean option does not open a second choice menu");
        check(frame.menu_core_cursor.line_index == 0 && frame.menu_core_cursors.size() == 1,
            "boolean option publishes the native settings cursor");
        check(!frame.awaiting_command, "boolean option owns input while the native editor is active");
    }
    {
        auto frame = option_frame("String option (\"s\"): player");
        fill_options_semantics(frame);
        check(frame.menu_choices.empty(), "non-boolean option does not synthesize a choice menu");
        check(frame.menu_core_cursor.line_index == 0 && frame.menu_core_cursors.size() == 1,
            "native option row remains cursor-driven");
        check(!frame.awaiting_command, "native option row owns input while the editor is active");
    }
    {
        GameFrame frame;
        fill_char_prompt_semantics(frame, 2, "Choose discovered type");
        const auto prompt_has = [&](int key) {
            return std::any_of(frame.prompt.choices.begin(), frame.prompt.choices.end(),
                [&](const PromptChoice &choice) { return choice.key == key; });
        };
        for (const int key : std::array<int, 6>{'?', '!', '/', '=', '*', 27})
            check(prompt_has(key), "discovered-type lightweight controls");
        check(frame.prompt.lightweight && !frame.menu_open && !frame.awaiting_command,
            "discovered-type prompt stays lightweight");
    }
    {
        RogueTerrainMemory memory;
        memory.follow(3);
        check(memory.resolve(10, MinimapKind::Floor, true) == MinimapKind::Floor,
            "terrain memory records a known Rogue floor");
        check(memory.resolve(10, MinimapKind::Unknown, true) == MinimapKind::Floor,
            "terrain memory keeps a known floor when a transient entity frame is blank");
        memory.follow(4);
        check(memory.resolve(10, MinimapKind::Unknown, true) == MinimapKind::Unknown,
            "terrain memory resets when Rogue changes dungeon level");
    }
    {
        r2_cell_state covered{};
        covered.dungeon_flags = kDungeonMonster | kDungeonFloor;
        covered.codepoint = ' ';
        covered.under_codepoint = '.';
        const auto flags = feature_flags(covered, 10, 10);
        check((flags & CELL_FEAT_KNOWN) != 0u && (flags & CELL_FEAT_PASSABLE) != 0u,
            "monster overlay keeps the known floor underneath it");
        covered.under_codepoint = ' ';
        check(feature_flags(covered, 10, 10) == 0u,
            "an actually unseen monster cell does not reveal unexplored terrain");
    }
    {
        r2_cell_state text_on_rock{};
        text_on_rock.codepoint = 0x6301u;
        check(feature_flags(text_on_rock, 3, 60) == 0u,
            "text drawn over Rogue rock does not become known terrain");
    }
    {
        mvaddch(5, 5, '.');
        r2_term_snapshot_push();
        mvaddch(5, 5, 'b');
        check(r2_term_map_codepoint(5, 5) == '.' && r2_term_codepoint(5, 5) == 'b',
            "Rogue terrain is read from the screen saved before an overlay");
        r2_term_snapshot_pop();
        check(r2_term_map_codepoint(5, 5) == '.', "Rogue terrain reads the live screen after the overlay");
        mvaddch(5, 5, ' ');
    }
    {
        r2_cell_state secret_door{};
        secret_door.dungeon_flags = kDungeonDoor | kDungeonHidden;
        secret_door.codepoint = '-';
        const auto flags = feature_flags(secret_door, 10, 10);
        check((flags & CELL_FEAT_KNOWN) != 0u && (flags & CELL_FEAT_WALL) != 0u,
            "an undiscovered Rogue secret door stays a visible wall");
        check((flags & CELL_FEAT_DOOR) == 0u && (flags & CELL_FEAT_PASSABLE) == 0u,
            "an undiscovered Rogue secret door is neither exposed nor passable");
        check(minimap_kind(flags) == MinimapKind::Wall,
            "an undiscovered Rogue secret door cannot become an HD2D floor tile");
    }
    {
        bool all_monsters_have_tiles = true;
        for (int monster_id = 1; monster_id <= 26; ++monster_id) {
            all_monsters_have_tiles = all_monsters_have_tiles && monster_tile_index(monster_id) != 0;
        }
        check(all_monsters_have_tiles, "all RogueClone2 monster species map to voxel tile indices");
    }
    {
        GameFrame frame;
        fill_char_prompt_semantics(frame, 1, "Identify monster");
        check(frame.prompt.lightweight && frame.text_input_active && frame.prompt.choices.empty(),
            "monster character/symbol prompt is raw one-character input");
        check(!frame.menu_open && !frame.awaiting_command,
            "monster character/symbol prompt does not open a full menu");
    }
    {
        GameFrame frame;
        fill_char_prompt_semantics(frame, 3, "Really quit?");
        check(!frame.prompt.lightweight && !frame.text_input_active && frame.prompt.choices.size() == 2,
            "quit confirmation is a confirm window (not the lightweight bar) with two choices");
        check(frame.prompt.choices[0].key == 'y' && frame.prompt.choices[1].key == 'n'
                && !frame.menu_open && !frame.awaiting_command,
            "quit confirmation owns input instead of reopening root commands");
    }
    {
        r2_item_state potion{};
        potion.key = 'a';
        potion.category = 040u;
        std::snprintf(potion.description, sizeof(potion.description), "%s", "Potion A");
        r2_item_state scroll{};
        scroll.key = 'b';
        scroll.category = 020u;
        std::snprintf(scroll.description, sizeof(scroll.description), "%s", "Scroll B");
        GameFrame frame;
        fill_pack_prompt_from_items(frame, 040u, "Which potion?", {potion, scroll});
        check(has_key(frame, 'a'), "item prompt includes matching category");
        check(has_key(frame, 'b'), "item prompt lists non-matching category after the matching ones");
        check(frame.menu_choices.size() >= 2 && frame.menu_choices[0].key == 'a' && frame.menu_choices[1].key == 'b',
            "item prompt orders matching category first");
        check(!has_key(frame, '*') && has_key(frame, 27), "item prompt cancel control without full-screen list");
    }

    check(r2_status_effects_selftest == 0, "status effects ");

    r2_term_text_input_begin();
    check(r2_term_text_input_active() != 0, "text input begin");
    r2_term_text_input_end();
    check(r2_term_text_input_active() == 0, "text input end");
    r2_term_numeric_begin(12);
    check(r2_term_numeric_active() != 0 && r2_term_numeric_value() == 12, "numeric input begin");
    r2_term_numeric_set(345);
    check(r2_term_numeric_value() == 345, "numeric input update");
    r2_term_numeric_end();
    check(r2_term_numeric_active() == 0, "numeric input end");
    r2_term_char_prompt_begin(2);
    check(r2_term_char_prompt_kind() == 2, "character prompt begin");
    r2_term_char_prompt_end();
    check(r2_term_char_prompt_kind() == 0, "character prompt end");
    r2_term_pack_prompt_begin(040u);
    check(r2_term_pack_prompt_mask() == 040u, "pack prompt begin");
    r2_term_pack_prompt_end();
    check(r2_term_pack_prompt_mask() == 0u, "pack prompt end");

    return failures;
}

} // namespace rogue2

/* ==================================================================== 魔法・投射物の効果（projections / missiles）
 *
 * 原作へ足した観測の呼び出し
 * （`tools/targeting/native_fx_overlay.py` が組み立てのたびに patched-src の写しへ足す。レシピには触れない。記録するだけで
 * ゲームのルールは変えない）がここを呼ぶ。積む先は共通の `presentation/bridge/named_attack_fx.h`。
 *
 * - 原作は座標を (行 row, 桁 col) で持つ（先頭の行はメッセージ行）。フレームのマスは (col, row - 1)。
 * - 状態を変える杖（減速・混乱・眠りほか）・拘束の巻物は、**怪物に効いたときだけ**、しかも**見えている怪物**のときだけ絵を出す
 *   （効かなかったときに出すと未識別の杖の種類が分かってしまう。決めたこと 2026-10-11）。マジック・ミサイルは原作もいつも描く。
 * - 投げた武器・矢は、原作が描く条件（`rogue_can_see()` のマス）のマスだけを歩にし、当たったときだけ相手のマスを足す。
 *   盲目のときは出さない。アイテムの絵は武器の種類ごとの変愚のアイテム番号（地図のアイテムと同じ番号）。
 * - モンスターの炎（`flame_broil`）は、原作が `~` を描くとき（盲目でなく、隣でもない）だけボルトとして出す。
 * - 属性の名前は変愚の名前（`presentation/bridge/attack_fx_attr_names.h`）。表は下の 3 つ。原作の `rogue.h` の杖・巻物・武器の定数との
 *   突き合わせは `tools/rogue2/r2_attack_fx_check.py --selftest`。
 */
namespace {

//! 原作の武器の種類（`rogue.h` の BOW..TWO_HANDED_SWORD） → 変愚のアイテム番号（地図の `object_tile_index` と同じ番号）と飛ばし方
//! （'T' 投擲・'A' 弓の矢。弓を構えているときだけ射撃になる）。
const struct {
    int kind;
    const char *macro;
    int item_id;
    char role;
} kFxWeaponKinds[] = {
    { 0, "BOW", 73, 'T' },
    { 1, "DART", 78, 'T' },
    { 2, "ARROW", 78, 'A' },
    { 3, "DAGGER", 43, 'T' },
    { 4, "SHURIKEN", 83, 'T' },
    { 5, "MACE", 53, 'T' },
    { 6, "LONG_SWORD", 42, 'T' },
    { 7, "TWO_HANDED_SWORD", 37, 'T' },
};

//! 原作の杖の種類（`rogue.h` の TELE_AWAY..DO_NOTHING） → 属性の名前と形（'B' 見えている怪物へのボルト・'M' マジック・ミサイル・'N' 絵を出さない）。
const struct {
    int kind;
    const char *macro;
    const char *attr;
    char mode;
} kFxWandKinds[] = {
    { 0, "TELE_AWAY", "away_all", 'B' },
    { 1, "SLOW_MONSTER", "old_slow", 'B' },
    { 2, "CONFUSE_MONSTER", "old_conf", 'B' },
    { 3, "INVISIBILITY", "dark_weak", 'B' },
    { 4, "POLYMORPH", "old_poly", 'B' },
    { 5, "HASTE_MONSTER", "old_speed", 'B' },
    { 6, "PUT_TO_SLEEP", "old_sleep", 'B' },
    { 7, "MAGIC_MISSILE", "missile", 'M' },
    { 8, "CANCELLATION", "disenchant", 'B' },
    { 9, "DO_NOTHING", "", 'N' },
};

//! 原作の巻物の種類（`rogue.h` の PROTECT_ARMOR..MAGIC_MAPPING） → 属性の名前と形。絵を出すのは怪物へ働きかける拘束の巻物だけ。
const struct {
    int kind;
    const char *macro;
    const char *attr;
    char mode;
} kFxScrollKinds[] = {
    { 0, "PROTECT_ARMOR", "", 'N' },
    { 1, "HOLD_MONSTER", "stasis", 'B' },
    { 2, "ENCH_WEAPON", "", 'N' },
    { 3, "ENCH_ARMOR", "", 'N' },
    { 4, "IDENTIFY", "", 'N' },
    { 5, "TELEPORT", "", 'N' },
    { 6, "SLEEP", "", 'N' },
    { 7, "SCARE_MONSTER", "", 'N' },
    { 8, "REMOVE_CURSE", "", 'N' },
    { 9, "CREATE_MONSTER", "", 'N' },
    { 10, "AGGRAVATE_MONSTER", "", 'N' },
    { 11, "MAGIC_MAPPING", "", 'N' },
};

//! 1 件に積むマス数の上限（`named_attack_fx.h` の `kNamedMaxCells` と同じ）。
constexpr int kFxMaxSteps = 80;

//! 投擲の途中で開いている 1 発があるか（盲目のときは開かない）。
bool g_fx_throw_active = false;

FxCell fx_cell(int row, int col)
{
    FxCell cell;
    cell.y = static_cast<std::int16_t>(row - 1); // 原作の先頭の行はメッセージ行
    cell.x = static_cast<std::int16_t>(col);
    return cell;
}

//! (r0, c0) の次のマスから (r1, c1) まで（始点は入れない）を 1 マスずつ。
std::vector<std::pair<int, int>> fx_line(int r0, int c0, int r1, int c1)
{
    std::vector<std::pair<int, int>> out;
    const int dr = r1 - r0;
    const int dc = c1 - c0;
    const int n = std::max(std::abs(dr), std::abs(dc));
    for (int i = 1; i <= n && i <= kFxMaxSteps; ++i) {
        out.emplace_back(r0 + static_cast<int>(std::lround(static_cast<double>(dr) * i / n)),
            c0 + static_cast<int>(std::lround(static_cast<double>(dc) * i / n)));
    }
    return out;
}

ProjectionFx fx_projection(AttackFxOrigin origin, const char *attr, int src_row, int src_col, int impact_row, int impact_col)
{
    ProjectionFx fx;
    fx.origin = origin;
    fx.shape = ProjectionShape::Bolt;
    fx.attr = attr;
    const FxCell src = fx_cell(src_row, src_col);
    const FxCell impact = fx_cell(impact_row, impact_col);
    fx.src_y = src.y;
    fx.src_x = src.x;
    fx.impact_y = impact.y;
    fx.impact_x = impact.x;
    if (r2_player_blind() != 0) fx.flags |= kProjPlayerBlind;
    return fx;
}

} // namespace

namespace rogue2 {

void fill_attack_fx(GameFrame &frame)
{
    // 命令待ちなら動きの終わり。開いている 1 発をそこで確定する。
    attack_fx::NamedRecorder::instance().take(frame.projections, frame.missiles, r2_term_command_input_active() != 0);
}

} // namespace rogue2

extern "C" void hb_r2_fx_missile_bolt(int hero_row, int hero_col, int end_row, int end_col, int end_is_monster)
{
    (void)end_is_monster;
    // マジック・ミサイル：原作は見えているマスに '*' を描く。経路は始点の次のマスから、止まったマス（怪物のマスか、壁の手前）まで。
    ProjectionFx fx = fx_projection(AttackFxOrigin::Player, "missile", hero_row, hero_col, hero_row, hero_col);
    for (const auto &step : fx_line(hero_row, hero_col, end_row, end_col)) {
        if (r2_can_see(step.first, step.second) != 0) fx.path.push_back(fx_cell(step.first, step.second));
    }
    if (fx.path.empty()) return;
    fx.impact_y = fx.path.back().y;
    fx.impact_x = fx.path.back().x;
    attack_fx::NamedRecorder::instance().add_projection(std::move(fx));
}

extern "C" void hb_r2_fx_zap(int group, int kind, int hero_row, int hero_col, int end_row, int end_col)
{
    const char *attr = "";
    char mode = 'N';
    if (group == 'W') {
        for (const auto &row : kFxWandKinds) {
            if (row.kind == kind) {
                attr = row.attr;
                mode = row.mode;
            }
        }
    } else if (group == 'S') {
        for (const auto &row : kFxScrollKinds) {
            if (row.kind == kind) {
                attr = row.attr;
                mode = row.mode;
            }
        }
    }
    // 効いたのは見えている怪物だけ絵にする（マジック・ミサイルは hb_r2_fx_missile_bolt が出す）。
    if (mode != 'B' || attr[0] == '\0' || r2_player_blind() != 0 || r2_can_see(end_row, end_col) == 0) return;
    ProjectionFx fx = fx_projection(AttackFxOrigin::Player, attr, hero_row, hero_col, end_row, end_col);
    for (const auto &step : fx_line(hero_row, hero_col, end_row, end_col)) fx.path.push_back(fx_cell(step.first, step.second));
    attack_fx::NamedRecorder::instance().add_projection(std::move(fx));
}

extern "C" void hb_r2_fx_throw(int kind, int hero_row, int hero_col, int end_row, int end_col, int monster_found, int launcher_matches)
{
    auto &recorder = attack_fx::NamedRecorder::instance();
    g_fx_throw_active = false;
    if (r2_player_blind() != 0) {
        recorder.end_missile();
        return;
    }
    int item_id = 0;
    char role = 'T';
    for (const auto &row : kFxWeaponKinds) {
        if (row.kind == kind) {
            item_id = row.item_id;
            role = row.role;
        }
    }
    const MissileKind missile_kind = (role == 'A' && launcher_matches != 0) ? MissileKind::Arrow : MissileKind::Thrown;
    recorder.begin_missile(missile_kind, AttackFxOrigin::Player, static_cast<std::uint16_t>(item_id), std::string(), hero_row - 1, hero_col);
    // 原作は、見えているマス（`rogue_can_see()`）だけに飛ぶ物を描く。怪物のいるマスは当たったときに足す。
    for (const auto &step : fx_line(hero_row, hero_col, end_row, end_col)) {
        const bool is_end = (step.first == end_row && step.second == end_col);
        if (is_end && monster_found != 0) break;
        if (r2_can_see(step.first, step.second) != 0) recorder.missile_step(step.first - 1, step.second);
    }
    g_fx_throw_active = true;
}

extern "C" void hb_r2_fx_thrown_hit(int row, int col)
{
    if (!g_fx_throw_active) return;
    g_fx_throw_active = false;
    auto &recorder = attack_fx::NamedRecorder::instance();
    if (r2_player_blind() != 0 || r2_can_see(row, col) == 0) {
        recorder.end_missile(); // 見えない相手に当たった：当たりは見せない
        return;
    }
    recorder.missile_step(row - 1, col);
    recorder.missile_hit();
}

extern "C" void hb_r2_fx_flame(int monster_row, int monster_col, int hero_row, int hero_col, int visible_flight)
{
    if (visible_flight == 0) return;
    ProjectionFx fx = fx_projection(AttackFxOrigin::Monster, "fire", monster_row, monster_col, hero_row, hero_col);
    for (const auto &step : fx_line(monster_row, monster_col, hero_row, hero_col)) fx.path.push_back(fx_cell(step.first, step.second));
    if (fx.path.empty()) return;
    attack_fx::NamedRecorder::instance().add_projection(std::move(fx));
}

namespace rogue2 {

/*
 * `Rogue2Core.exe --selftest` の攻撃の効果の分：モンスターの炎・表の整合を、ゲームの状態が要らない部分で数える。
 * 見えている怪物かの判定（`rogue_can_see()`）が要る部分は実機の検査（tools/rogue2/r2_attack_fx_check.py）が見る。0 なら全部合った。
 */
int attack_fx_selftest()
{
    int failures = 0;
    int checks = 0;
    const auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            std::fprintf(stderr, "[rogue2] selftest: attack fx: FAIL %s\n", what);
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

    // モンスターの炎：原作が `~` を描く（盲目でなく、隣でもない）ときだけボルト。撃った者はモンスター。
    hb_r2_fx_flame(5, 10, 5, 15, 1);
    drain();
    check(projections.size() == 1 && projections[0].attr == "fire" && projections[0].origin == AttackFxOrigin::Monster, "a monster's flame is fire from the monster");
    check(projections.size() == 1 && projections[0].shape == ProjectionShape::Bolt && projections[0].path.size() == 5, "the flame crosses the cells between the monster and the hero");
    check(projections.size() == 1 && projections[0].src_y == 4 && projections[0].src_x == 10 && projections[0].impact_x == 15, "flame cells are screen row - 1");
    hb_r2_fx_flame(5, 10, 5, 11, 0); // 隣：原作は何も描かない
    drain();
    check(projections.empty(), "a flame with no drawn flight draws nothing");
    hb_r2_fx_flame(2, 2, 6, 6, 1); // 斜め
    drain();
    check(projections.size() == 1 && projections[0].path.size() == 4, "a diagonal flame");

    // 表：杖・巻物の絵を出す種類には属性がある。絵を出さない種類は属性が空。
    for (const auto &row : kFxWandKinds) check((row.mode == 'N') == (row.attr[0] == '\0'), "wand table: a picture needs an attribute");
    for (const auto &row : kFxScrollKinds) check((row.mode == 'N') == (row.attr[0] == '\0'), "scroll table: a picture needs an attribute");
    for (const auto &row : kFxWeaponKinds) check(row.item_id != 0, "weapon table: every kind has an item number");

    // 杖はマジック・ミサイル以外を `hb_r2_fx_zap` が出し、絵を出さない杖・巻物・知らない種類は何も出さない。
    hb_r2_fx_zap('W', 7, 5, 5, 5, 9); // マジック・ミサイルは hb_r2_fx_missile_bolt が出す
    hb_r2_fx_zap('W', 9, 5, 5, 5, 9); // 何も起きない杖
    hb_r2_fx_zap('S', 4, 5, 5, 5, 9); // 鑑定の巻物
    hb_r2_fx_zap('X', 0, 5, 5, 5, 9);
    drain();
    check(projections.empty(), "magic missile, do nothing, identify and unknown groups draw nothing here");

    if (failures == 0) std::fprintf(stderr, "selftest: attack fx: ok (%d checks)\n", checks);
    return failures == 0 ? 0 : 1;
}

} // namespace rogue2
