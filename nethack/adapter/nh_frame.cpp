/*
 * NetHack のコアの frame の組み立て。
 *
 * frame に入れる欄:
 *   hud / status_line      … status_update の項目（HP・Pw・金・階・レベルほか）
 *                            （専用の画面 nethack-ui のときは hud の数字だけ。status_line は載せない）
 *   messages               … putstr(WIN_MESSAGE) の直近の行（nethack-ui のときは載せない。窓の欄 menu_*・prompt も同じ）
 *   menu_*                 … メニューと文字の窓（menu_term_lines と menu_choices と menu_open。名前の問いと
 *                            キャラクタ作成の間は pre_game_menu も）
 *   prompt                 … yn_function（ゲーム中）
 *   text_input_active+prompt … getlin
 *   cells / minimap       … 地図のマス（fill_map。地形は nh_terrain.cpp で NetHack の地形番号〔20000〜〕へ、
 *                            怪物・アイテム・主人公は原作のタイルの絵の索引〔板〕へ直す）
 *   player_gx/gy・cam_*   … 主人公の位置と、地図の窓（地図全体）
 */

#include "nh_view.h"

#include "nh_terrain.h"

#include "frame/cell_feature_bits.h"
#include "frame/game_frame.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace nethack {

namespace {

//! 原作の glyph → 絵の番号（tiles.json の glyph2tile）。空なら絵の索引を付けない。
std::vector<int> g_glyph_to_tile;
//! 絵の番号 → 板があるか（slabs.tsv の絵の番号）。
std::vector<unsigned char> g_has_slab;
//! 主人公の絵の番号（原作の MG_HERO の glyph から覚える。主人公が見えない間〔透明など〕も同じ絵で出す）。-1 は未定。
int g_hero_tile = -1;

int first_number(const std::string &text)
{
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] >= '0' && text[i] <= '9') return std::atoi(text.c_str() + i);
    }
    return 0;
}

//! 「<記号>:<金額>」の金額（記号は \GXXXXNNNN のこともある）。
int gold_amount(const std::string &text)
{
    const std::size_t colon = text.find(':');
    return colon == std::string::npos ? first_number(text) : std::atoi(text.c_str() + colon + 1);
}

//! 端末の桁（全角は 2）。
int display_cols(const std::string &utf8)
{
    int cols = 0;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        std::size_t len = 1;
        unsigned cp = c;
        if (c >= 0xF0) {
            len = 4;
            cp = c & 0x07;
        } else if (c >= 0xE0) {
            len = 3;
            cp = c & 0x0F;
        } else if (c >= 0xC0) {
            len = 2;
            cp = c & 0x1F;
        }
        for (std::size_t k = 1; k < len && i + k < utf8.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        cols += (cp >= 0x2E80 && !(cp >= 0xFF61 && cp <= 0xFF9F)) ? 2 : 1;
        i += len;
    }
    return cols;
}

void add_choice(GameFrame &frame, int line_index, int line_bytes, int key, const std::string &label, bool selected)
{
    MenuChoice choice{};
    choice.line_index = line_index;
    choice.span_begin = 0;
    choice.span_len = line_bytes;
    choice.key_begin = 0;
    choice.key_len = (line_bytes > 0) ? 1 : 0;
    choice.key = key;
    choice.selected = selected;
    choice.label_utf8 = label;
    frame.menu_choices.push_back(std::move(choice));
}

void add_term_line(GameFrame &frame, const std::string &text)
{
    TermMirrorLine line;
    line.text_utf8 = text;
    line.color = 1;
    line.source_row = static_cast<int>(frame.menu_term_lines.size());
    frame.menu_term_lines.push_back(std::move(line));
}

//! ステータス 2 行を 1 本にしたもの。
std::string compose_status_line(const View &v)
{
    const auto field = [&](int idx) -> const std::string & { return v.status[idx]; };
    std::string text = field(NH_BL_TITLE);
    const struct {
        int idx;
        const char *label;
    } stats[] = { { NH_BL_STR, "St:" }, { NH_BL_DX, "Dx:" }, { NH_BL_CO, "Co:" }, { NH_BL_IN, "In:" }, { NH_BL_WI, "Wi:" }, { NH_BL_CH, "Ch:" } };
    for (const auto &stat : stats) {
        if (!field(stat.idx).empty()) text += std::string(text.empty() ? "" : " ") + stat.label + field(stat.idx);
    }
    if (!field(NH_BL_ALIGN).empty()) text += " " + field(NH_BL_ALIGN);
    std::string second = field(NH_BL_LEVELDESC);
    while (!second.empty() && second.back() == ' ') second.pop_back();
    const auto add = [&](const std::string &part) {
        if (part.empty()) return;
        if (!second.empty()) second += " ";
        second += part;
    };
    if (!field(NH_BL_GOLD).empty()) {
        const std::size_t colon = field(NH_BL_GOLD).find(':');
        add("$:" + (colon == std::string::npos ? field(NH_BL_GOLD) : field(NH_BL_GOLD).substr(colon + 1)));
    }
    if (!field(NH_BL_HP).empty()) add("HP:" + field(NH_BL_HP) + "(" + field(NH_BL_HPMAX) + ")");
    if (!field(NH_BL_ENE).empty()) add("Pw:" + field(NH_BL_ENE) + "(" + field(NH_BL_ENEMAX) + ")");
    if (!field(NH_BL_AC).empty()) add("AC:" + field(NH_BL_AC));
    if (!field(NH_BL_XP).empty()) add("Xp:" + field(NH_BL_XP));
    if (!field(NH_BL_TIME).empty()) add("T:" + field(NH_BL_TIME));
    add(field(NH_BL_HUNGER));
    add(field(NH_BL_CAP));
    add(v.condition_names);
    if (!second.empty()) text += (text.empty() ? "" : " / ") + second;
    return text;
}

//! 原作の色の番号（0〜15）→ HD2D の TERM_* の番号。DCSS のアダプタ（dcss_text.h）と同じ割り当てに、橙を足した。
std::uint8_t term_color_of(int nethack_color)
{
    // 原作 include/color.h: 0 黒 1 赤 2 緑 3 茶 4 青 5 赤紫 6 青緑 7 灰 8 色なし 9 橙 10 明るい緑 11 黄 12 明るい青 13 明るい赤紫 14 明るい青緑 15 白
    static const std::uint8_t kMap[16] = {
        0,  // 黒 → TERM_DARK
        4,  // 赤 → TERM_RED
        5,  // 緑 → TERM_GREEN
        7,  // 茶 → TERM_UMBER
        6,  // 青 → TERM_BLUE
        10, // 赤紫 → TERM_VIOLET
        14, // 青緑 → TERM_L_BLUE（水色）
        9,  // 灰（普通の字の色）→ TERM_L_WHITE
        9,  // 色なし（既定の字の色）→ TERM_L_WHITE
        3,  // 橙 → TERM_ORANGE
        13, // 明るい緑 → TERM_L_GREEN
        11, // 黄 → TERM_YELLOW
        6,  // 明るい青 → TERM_BLUE
        10, // 明るい赤紫 → TERM_VIOLET
        14, // 明るい青緑 → TERM_L_BLUE
        1,  // 白 → TERM_WHITE
    };
    return kMap[nethack_color & 15];
}

//! 一番上に見える物が、地形ではなく載っている物（怪物・アイテムなど）か。ビームなどの効果の記号も含む。
bool has_thing_on_top(const MapCell &m)
{
    switch (m.glyph_class) {
    case NH_GLYPH_NOTHING:
    case NH_GLYPH_UNEXPLORED:
    case NH_GLYPH_TRAP:
        return false;
    case NH_GLYPH_CMAP:
        return m.glyph_index >= 74; // S_vbeam 以降は効果の記号
    default:
        return true;
    }
}

/*!
 * 絵の番号（原作のタイル）から、画面へ送る絵の索引（tile_index）を引く。板が無い絵は 0（字の板で出る）。
 */
std::uint16_t tile_index_of_tile(int tile)
{
    if (tile < 0 || static_cast<std::size_t>(tile) >= g_has_slab.size() || g_has_slab[static_cast<std::size_t>(tile)] == 0) return 0;
    return static_cast<std::uint16_t>(tile_index_of(tile));
}

/*!
 * そのマスの怪物・ペット・見えない怪物の印・アイテム・石像の絵の番号（原作の glyph2tile）。無ければ -1。
 * 警告の数字・ビームなどの効果・爆発・飲み込みの絵は板にならない範囲（tiles.json の other）なので、ここでは引かない（字で出る）。
 */
int tile_of_cell(const MapCell &m)
{
    switch (m.glyph_class) {
    case NH_GLYPH_MONSTER:
    case NH_GLYPH_PET:
    case NH_GLYPH_INVISIBLE:
    case NH_GLYPH_OBJECT:
        break;
    default:
        return -1;
    }
    if (m.glyph < 0 || static_cast<std::size_t>(m.glyph) >= g_glyph_to_tile.size()) return -1;
    return g_glyph_to_tile[static_cast<std::size_t>(m.glyph)];
}

/*!
 * 怪物・アイテムなどに frame の monster_id / object_id として入れる仮の番号。
 *
 * 画面（hd2d/world/entity_view.cpp）は、monster_id も object_id も 0 で主人公の印も無いマスの物を立体に描かない。
 * 番号は「いる」印と検査用の問い合わせにしか使わず、変愚の種族として名前を引くことはない。絵は tile_index（原作のタイルの板）で
 * 引く（板が無い物だけ字の板で出る）。決め方は DCSS と同じ考え方で、原作の番号に 1 を足す（0 は「無い」の意味なので）。
 *
 *   monster_id  1 + 怪物の番号（glyph_to_mon。ペットも同じ）。原作の怪物は 0x0400 未満
 *               0x0400            見えない怪物の記憶（I）
 *               0x0401 + 度合い   警告の数字（1〜5。度合いは 0〜5 の glyph_to_warning）
 *   object_id   1 + アイテムの番号（glyph_to_obj。像・死体も同じ。原作のアイテムは 0x0400 未満）
 *               0x0400 + (番号 - 74)  ビームなどの効果の記号（cmap の S_vbeam 以降）
 *               0x0500 + 番号     魔法の筋（ZAP）
 *               0x0600 + 番号     爆発
 *               0x0700 + 番号     飲み込まれているときの内壁
 *               0x07FF            ほかの分類
 * object_id の 0xFF00 以上は画面が罠など別の層に使うので、ここでは使わない（最大でも 0x07FF）。
 */
struct ThingIds {
    std::uint16_t monster{ 0 };
    std::uint16_t object{ 0 };
};

ThingIds thing_ids(const MapCell &m)
{
    ThingIds ids;
    const auto clamp_to = [](int value, int limit) { return value < 0 ? 0 : (value > limit ? limit : value); };
    switch (m.glyph_class) {
    case NH_GLYPH_MONSTER:
    case NH_GLYPH_PET:
        ids.monster = static_cast<std::uint16_t>(1 + clamp_to(m.glyph_index, 0x3FE));
        break;
    case NH_GLYPH_INVISIBLE:
        ids.monster = 0x0400;
        break;
    case NH_GLYPH_WARNING:
        ids.monster = static_cast<std::uint16_t>(0x0401 + clamp_to(m.glyph_index, 15));
        break;
    case NH_GLYPH_OBJECT:
        ids.object = static_cast<std::uint16_t>(1 + clamp_to(m.glyph_index, 0x3FE));
        break;
    case NH_GLYPH_CMAP: // 効果の記号（has_thing_on_top が真のときだけ呼ばれる）
        ids.object = static_cast<std::uint16_t>(0x0400 + clamp_to(m.glyph_index - 74, 0xFF));
        break;
    case NH_GLYPH_ZAP:
        ids.object = static_cast<std::uint16_t>(0x0500 + clamp_to(m.glyph_index, 0xFF));
        break;
    case NH_GLYPH_EXPLOSION:
        ids.object = static_cast<std::uint16_t>(0x0600 + clamp_to(m.glyph_index, 0xFF));
        break;
    case NH_GLYPH_SWALLOW:
        ids.object = static_cast<std::uint16_t>(0x0700 + clamp_to(m.glyph_index, 0xFF));
        break;
    default:
        ids.object = 0x07FF;
        break;
    }
    return ids;
}

/*!
 * 地図のマスを cells と minimap に入れる。
 *
 * - 地形は terrain_cmap と terrain_variant（nh_host_map_cell 参照）を nh_terrain で引き、NetHack の地形番号（20000〜。
 *   壁は迷宮の系統ごと・祭壇は属性ごと）を terrain_id に入れる。罠は地形として描く（terrain_id が罠のブロック）。
 * - 怪物・ペット・アイテム・見えない印・主人公は、原作のタイルの絵の索引を tile_index に入れる（板。tile_of_cell。
 *   主人公の絵は原作の glyph が職業と男女で決める）。字と色は字の板の受け皿として ascii_fallback と fg_color にも入れ、
 *   仮の番号を monster_id / object_id に入れる（thing_ids。画面が実体として描く条件）。
 * - light_level は、いま見えているマス（原作の cansee）が 2、見えていないが覚えているマスが 1、知らないマスが 0。
 * - 地図は 80 x 21 を窓ごと送る（列 0 は原作が使わないので、いつも知らないマス）。
 */
void fill_map(const View &v, GameFrame &frame)
{
    frame.view_w = kMapCols;
    frame.view_h = kMapRows;
    // 窓の中心（画面側は cam を窓の中心として読む）。窓が地図全体なのでいつも同じ。3D のカメラは player_gx/gy を追う
    frame.cam_x = kMapCols / 2;
    frame.cam_y = kMapRows / 2;
    frame.player_gx = v.hero_x;
    frame.player_gy = v.hero_y;
    // 見えていない覚えているマス（light_level 1）を少し暗くしてほしい、という申告
    frame.lighting.memory_dim = 1;

    MinimapSnapshot &minimap = frame.minimap;
    minimap.width = kMapCols;
    minimap.height = kMapRows;
    minimap.player_gx = v.hero_x;
    minimap.player_gy = v.hero_y;
    minimap.kinds.assign(static_cast<std::size_t>(kMapCols) * kMapRows, static_cast<std::uint8_t>(MinimapKind::Unknown));
    minimap.markers.assign(minimap.kinds.size(), static_cast<std::uint8_t>(MinimapMarker::None));

    frame.cells.reserve(static_cast<std::size_t>(kMapCols) * kMapRows);
    for (int y = 0; y < kMapRows; ++y) {
        for (int x = 0; x < kMapCols; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * kMapCols + static_cast<std::size_t>(x);
            const MapCell &m = v.map[at];
            const bool is_hero = x == v.hero_x && y == v.hero_y;
            const TerrainLook look = m.terrain_cmap >= 0 ? terrain_for_cmap(m.terrain_cmap, m.terrain_variant) : TerrainLook{};
            const bool thing = has_thing_on_top(m);

            MapCellView cell{};
            cell.gx = static_cast<std::int16_t>(x);
            cell.gy = static_cast<std::int16_t>(y);
            if (look.known) {
                cell.terrain_id = look.terrain;
                cell.feature_flags = static_cast<std::uint16_t>(look.flags | CELL_FEAT_KNOWN);
                minimap.kinds[at] = static_cast<std::uint8_t>(look.minimap);
                if (look.trap) minimap.markers[at] = static_cast<std::uint8_t>(MinimapMarker::Trap);
            }
            // 地形が分からなくても、載っている物とプレイヤーの字は送る（字の地図に出る）。KNOWN は地形が分かるときだけ
            if (look.known || thing || is_hero) {
                const bool visible = v.visible[at] != 0;
                cell.light_level = visible ? 2 : 1;
                const int ch = m.ch;
                cell.ascii_fallback = (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : (thing ? '?' : ' ');
                cell.fg_color = term_color_of(m.color);
            }
            if (thing && !is_hero) {
                const ThingIds ids = thing_ids(m);
                cell.monster_id = ids.monster;
                cell.object_id = ids.object;
                cell.tile_index = tile_index_of_tile(tile_of_cell(m));
                switch (m.glyph_class) {
                case NH_GLYPH_MONSTER:
                case NH_GLYPH_PET:
                case NH_GLYPH_INVISIBLE:
                case NH_GLYPH_WARNING:
                    minimap.markers[at] = static_cast<std::uint8_t>(MinimapMarker::Monster);
                    break;
                case NH_GLYPH_OBJECT:
                    if (minimap.markers[at] == static_cast<std::uint8_t>(MinimapMarker::None)) {
                        minimap.markers[at] = static_cast<std::uint8_t>(MinimapMarker::Item);
                    }
                    break;
                default:
                    break;
                }
            }
            if (is_hero) {
                cell.feature_flags = static_cast<std::uint16_t>(cell.feature_flags | CELL_FEAT_PLAYER);
                cell.light_level = 2;
                if ((m.flags & 0x1u) == 0u) cell.ascii_fallback = '@'; // 主人公の字が出ていないとき（透明など）。0x1 は原作の MG_HERO
                // 主人公の絵: 原作が主人公の glyph（職業・男女。変身中は変身先）を出しているときの絵を覚えておき、見えない間も使う
                if ((m.flags & 0x1u) != 0u && m.glyph_class == NH_GLYPH_MONSTER) g_hero_tile = tile_of_cell(m);
                cell.tile_index = tile_index_of_tile(g_hero_tile);
                minimap.kinds[at] = static_cast<std::uint8_t>(MinimapKind::Player);
            }
            frame.cells.push_back(cell);
        }
    }
}

} // namespace

void set_tile_tables(std::vector<int> glyph_to_tile, const std::vector<int> &slab_tiles)
{
    g_glyph_to_tile = std::move(glyph_to_tile);
    g_has_slab.clear();
    int max_tile = -1;
    for (const int tile : slab_tiles) max_tile = std::max(max_tile, tile);
    if (max_tile >= 0) g_has_slab.assign(static_cast<std::size_t>(max_tile) + 1, 0);
    for (const int tile : slab_tiles) {
        if (tile >= 0) g_has_slab[static_cast<std::size_t>(tile)] = 1;
    }
    if (g_has_slab.empty()) g_glyph_to_tile.clear(); // 板が無いなら、絵の索引は付けない
}

std::string clean_utf8(const char *text)
{
    std::string out;
    if (text == nullptr) return out;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(text);
    while (*p != 0) {
        const unsigned char c = *p;
        if (c < 0x20 || c == 0x7F) {
            out.push_back(' ');
            ++p;
            continue;
        }
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            ++p;
            continue;
        }
        std::size_t len = 0;
        unsigned cp = 0;
        if (c >= 0xC2 && c <= 0xDF) {
            len = 2;
            cp = c & 0x1F;
        } else if (c >= 0xE0 && c <= 0xEF) {
            len = 3;
            cp = c & 0x0F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            len = 4;
            cp = c & 0x07;
        }
        bool ok = len != 0;
        for (std::size_t k = 1; ok && k < len; ++k) {
            if ((p[k] & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (p[k] & 0x3F);
        }
        if (ok) {
            // 最短でない形・サロゲート・範囲外を弾く
            if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF)) ok = false;
        }
        if (!ok) {
            out.push_back('?');
            ++p;
            continue;
        }
        out.append(reinterpret_cast<const char *>(p), len);
        p += len;
    }
    return out;
}

void build_frame(const View &v, bool ja, const char *core_name, bool modern, GameFrame &frame)
{
    const bool pregame = v.phase == NH_PHASE_PREGAME;

    // ---- hud・status_line ----
    frame.hud.name = v.player_name.empty() ? std::string(core_name) : v.player_name;
    frame.floor.place_name_utf8 = v.status[NH_BL_LEVELDESC].empty() ? std::string(core_name) : v.status[NH_BL_LEVELDESC];
    if (v.status_seen) {
        frame.hud.hp = std::atoi(v.status[NH_BL_HP].c_str());
        frame.hud.hp_max = std::atoi(v.status[NH_BL_HPMAX].c_str());
        frame.hud.sp = std::atoi(v.status[NH_BL_ENE].c_str());
        frame.hud.sp_max = std::atoi(v.status[NH_BL_ENEMAX].c_str());
        frame.hud.gold = gold_amount(v.status[NH_BL_GOLD]);
        frame.hud.depth = first_number(v.status[NH_BL_LEVELDESC]);
        frame.hud.level = std::atoi(v.status[NH_BL_XP].c_str());
        // 専用の画面（nethack-ui）はステータスの窓を自分で描くので、2 行を 1 本にした字は載せない
        if (!v.ui_mode) frame.hud.status_line = compose_status_line(v);
    }

    // ---- 地図（ゲームが始まっていて、主人公の位置が分かるときだけ） ----
    if (v.map_valid && !pregame) fill_map(v, frame);

    // ---- 専用の画面（nethack-ui）: 窓の字は載せない。地図と最小の hud だけ ----
    if (v.ui_mode) {
        // 新しい画面の窓（modern_ui）へ読み替えさせない（DCSS の dcss-ui と同じ。prompt.lightweight）
        frame.prompt.lightweight = true;
        frame.awaiting_command = !pregame && v.key_mode == NH_KEY_COMMAND;
        return;
    }

    // ---- messages（直近の数行を、古い順に毎回丸ごと送る。seq は frame の中の位置） ----
    const std::size_t total = v.history.size();
    const std::size_t from = total > static_cast<std::size_t>(kFrameMessageLines) ? total - kFrameMessageLines : 0;
    for (std::size_t i = from; i < total; ++i) {
        MessageEvent event;
        event.seq = static_cast<std::uint32_t>(frame.messages.size());
        event.color = 1;
        event.text_utf8 = v.history[i];
        frame.messages.push_back(std::move(event));
    }

    // ---- 窓ごとの欄 ----
    switch (v.mode) {
    case Mode::Map:
        frame.awaiting_command = !pregame && v.key_mode == NH_KEY_COMMAND;
        break;

    case Mode::Menu:
    case Mode::Text: {
        frame.menu_open = true;
        frame.pre_game_menu = pregame;
        frame.menu_over_map = !pregame;
        if (!v.prompt.empty()) add_term_line(frame, v.prompt);
        for (const Line &line : v.lines) {
            std::string text;
            if (line.selectable) {
                text.push_back(line.selector != 0 ? static_cast<char>(line.selector) : ' ');
                text += line.selected ? " + " : " - ";
            }
            text += line.text;
            const int index = static_cast<int>(frame.menu_term_lines.size());
            add_term_line(frame, text);
            if (line.selectable && line.selector != 0) {
                // 複数選択の行は、新しい画面の行に選び状態の欄が無いので、名札の先頭に付ける
                const std::string label = (modern && v.how == NH_PICK_ANY) ? (line.selected ? "[x] " : "[ ] ") + line.text : line.text;
                add_choice(frame, index, static_cast<int>(text.size()), line.selector, label, line.selected);
            }
        }
        if (v.mode == Mode::Text) {
            // 新しい画面は、選択肢の無い窓を「本文＋続ける」の窓にする（続ける＝Enter。nh_host_menu が受ける）
            if (!modern) add_choice(frame, 0, 0, ' ', ja ? "続ける" : "Continue", false);
        } else {
            if (v.how == NH_PICK_ANY) add_choice(frame, 0, 0, '\r', ja ? "決定" : "Confirm", false);
            add_choice(frame, 0, 0, 0x1B, ja ? "キャンセル" : "Cancel", false);
        }
        break;
    }

    case Mode::Yn: {
        frame.prompt.text_utf8 = v.prompt;
        frame.prompt.line_index = -1;
        if (pregame) {
            // キャラクタ作成の問いは、ほかの誕生の画面と同じく本文を全面に出し、字を選択肢にする。
            frame.menu_open = true;
            frame.pre_game_menu = true;
            add_term_line(frame, v.prompt);
            for (const YnChoice &choice : v.yn_choices) add_choice(frame, 0, 0, choice.key, choice.label, false);
        } else {
            for (const YnChoice &choice : v.yn_choices) {
                PromptChoice prompt_choice;
                prompt_choice.label_utf8 = choice.label;
                prompt_choice.key = choice.key;
                frame.prompt.choices.push_back(std::move(prompt_choice));
            }
        }
        break;
    }

    case Mode::Getlin: {
        frame.text_input_active = true;
        const std::string shown = v.prompt + (v.prompt.empty() ? "" : " ") + v.typed;
        frame.prompt.text_utf8 = shown;
        frame.prompt.line_index = -1;
        if (pregame) {
            frame.menu_open = true;
            frame.pre_game_menu = true;
            add_term_line(frame, shown);
            frame.menu_term_curs_row = 0;
            frame.menu_term_curs_col = display_cols(shown);
        }
        break;
    }
    }
}

} // namespace nethack
