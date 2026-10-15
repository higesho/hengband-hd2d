/*
 * Umoria のフレームをゲームの状態から組む（「M2」）。
 *
 * 地図は端末の字ではなく `dg.floor`・`game.treasure.list`・`monsters` から読む。
 * 送る地形番号は変愚のもの（TerrainDefinitions.jsonc の id）。画面側の地形の役割はミニマップの種類で決まるので、
 * ミニマップはフロア全域を毎回埋める。
 */

#include "um_frame.h"
#include "um_lang.h"

#include "headers.h"
#ifndef _WIN32
// ui.h renames open/fopen to the tilde-expanding topen/tfopen (defined in um_io.cpp) for the upstream
// files. The adapter does not need that, and the macros would also rename std::fopen and fstream::open.
#undef open
#undef fopen
#endif
#include "um_host.h"
#include "um_town.h"
#include "um_tile_map.generated.h"

#include "bridge/named_attack_fx.h"
#include "bridge/official_tile_table.h"
#include "frame/cell_feature_bits.h"
#include "frame/attack_fx.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr int kViewCols = 67;
constexpr int kViewRows = 25;
constexpr int kStatusCols = 13; // 左の状態列（ui.cpp が 0〜12 桁に書く）

//! 変愚の地形番号（lib/edit/TerrainDefinitions.jsonc）。
enum HengbandTerrain : std::uint16_t {
    kFloor = 1,
    kRuneProtection = 3,
    kOpenDoor = 4,
    kBrokenDoor = 5,
    kUpStair = 6,
    kDownStair = 7,
    kTrapTrapdoor = 16,
    kTrapPit = 17,
    kTrapEvilRune = 20,
    kTrapTeleport = 21,
    kTrapFire = 22,
    kTrapAcid = 23,
    kTrapSlowDart = 24,
    kTrapStrDart = 25,
    kTrapConDart = 27,
    kTrapBlindGas = 28,
    kTrapConfuseGas = 29,
    kTrapPoisonGas = 30,
    kTrapSleepGas = 31,
    kClosedDoor = 32,
    kLockedDoor = 33,
    kJammedDoor = 40,
    kSecretDoor = 48,
    kRubble = 49,
    kMagma = 50,
    kQuartz = 51,
    kMagmaTreasure = 54,
    kQuartzTreasure = 55,
    kGranite = 56,
    kPermanent = 60,
    kGeneralStore = 74, // 74〜79 が Umoria の 6 軒と同じ並び
    kDeepWater = 83,
    kGrass = 89,
    kTree = 96,
    kMountain = 97,
};

/*
 * 罠の種類（原作 player_move.cpp の TrapTypes。`sub_category_id`）→ 変愚の罠。
 * 変愚に同じものが無い 5 種は見た目の近い罠で代える（遊ぶ人の了承 2026-10-03。
 * 効果はコアが原作どおりに処理するので、変わるのは絵だけ）。
 */
std::uint16_t trap_terrain(int sub_category)
{
    switch (sub_category) {
    case 1: return kTrapPit;        // open pit
    case 2: return kTrapSlowDart;   // arrow trap（変愚に無い。ダーツの罠の見た目）
    case 3: return kTrapPit;        // covered pit
    case 4: return kTrapTrapdoor;   // trap door
    case 5: return kTrapSleepGas;   // sleeping gas
    case 6: return kTrapPit;        // loose rock（変愚に無い）
    case 7: return kTrapStrDart;    // dart of strength
    case 8: return kTrapTeleport;   // teleport rune
    case 9: return kTrapPit;        // rockfall（変愚に無い）
    case 10: return kTrapPoisonGas; // corroding gas（変愚に無い。ガスの罠の見た目）
    case 11: return kTrapEvilRune;  // summon rune（変愚に無い。邪悪なルーンの見た目）
    case 12: return kTrapFire;
    case 13: return kTrapAcid;
    case 14: return kTrapPoisonGas;
    case 15: return kTrapBlindGas;
    case 16: return kTrapConfuseGas;
    case 17: return kTrapSlowDart;
    case 18: return kTrapConDart;
    case 99: return kRuneProtection; // glyph of warding（scare monster）
    default: return kTrapPit;
    }
}

//! Umoria のアイテムの分類（tval）→ 変愚の代表のベースアイテム（BaseitemDefinitions.jsonc の id）。
std::uint16_t representative_baseitem(int tval)
{
    switch (tval) {
    case TV_MISC: return 391;      // Broken Skull
    case TV_CHEST: return 338;     // Small wooden chest
    case TV_SLING_AMMO: return 83; // Iron Shot
    case TV_BOLT: return 80;
    case TV_ARROW: return 78;
    case TV_SPIKE: return 345;
    case TV_LIGHT: return 346;     // Wooden Torch
    case TV_BOW: return 73;        // Short Bow
    case TV_HAFTED: return 53;     // Mace
    case TV_POLEARM: return 64;    // Spear
    case TV_SWORD: return 43;      // Dagger
    case TV_DIGGING: return 84;    // Shovel
    case TV_BOOTS: return 91;
    case TV_GLOVES: return 125;
    case TV_CLOAK: return 123;
    case TV_HELM: return 94;
    case TV_SHIELD: return 128;
    case TV_HARD_ARMOR: return 108;
    case TV_SOFT_ARMOR: return 103;
    case TV_AMULET: return 163;
    case TV_RING: return 132;
    case TV_STAFF: return 300;
    case TV_WAND: return 269;
    case TV_SCROLL1:
    case TV_SCROLL2: return 173;
    case TV_POTION1:
    case TV_POTION2: return 222;
    case TV_FLASK: return 348;
    case TV_FOOD: return 21;       // Ration of Food
    case TV_MAGIC_BOOK: return 334;
    case TV_PRAYER_BOOK: return 330;
    case TV_GOLD: return 488;
    default: return 0;
    }
}

struct CellMeaning {
    std::uint16_t terrain = 0;
    std::uint16_t flags = 0;
    bool item = false;      //!< 床に落ちているアイテム（金を含む）
    bool trap = false;
    int item_tval = 0;
    const Inventory_t *object = nullptr; //!< 床のアイテムそのもの（item が真のときだけ）
    MinimapKind kind = MinimapKind::Unknown; //!< Unknown なら flags から決める
};

/*
 * アイテムの絵。種類が分からない間（薬・巻物・指輪などの見た目だけ分かる物）は
 * 分類の代表の絵、分かったら種類ごとの絵（`kObjectBaseitem`。名前で変愚のアイテムへ当てた表）。
 * 「分かったか」は原作の判定 `itemSetColorlessAsIdentified()` をそのまま使う（見た目の無い分類は常に真）。
 */
std::uint16_t item_baseitem(const Inventory_t &item)
{
    const bool aware = itemSetColorlessAsIdentified(item.category_id, item.sub_category_id, item.identification);
    if (aware && item.id < umoria::kObjectBaseitemCount && umoria::kObjectBaseitem[item.id] != 0) {
        return umoria::kObjectBaseitem[item.id];
    }
    return representative_baseitem(item.category_id);
}

bool tile_known(const Tile_t &tile)
{
    return tile.permanent_light || tile.temporary_light || tile.field_mark;
}

CellMeaning cell_meaning(const Tile_t &tile, int y, int x)
{
    CellMeaning m;
    switch (tile.feature_id) {
    case TILE_GRANITE_WALL: m.terrain = kGranite; m.flags = CELL_FEAT_WALL; break;
    case TILE_MAGMA_WALL: m.terrain = kMagma; m.flags = CELL_FEAT_WALL; break;
    case TILE_QUARTZ_WALL: m.terrain = kQuartz; m.flags = CELL_FEAT_WALL; break;
    case TILE_BOUNDARY_WALL: m.terrain = kPermanent; m.flags = CELL_FEAT_WALL | CELL_FEAT_PERMANENT; break;
    default: m.terrain = kFloor; m.flags = CELL_FEAT_PASSABLE; break;
    }
    // 固定の町の地図で、コアでは壁・床のマスを、見た目どおりの地形で送る。
    switch (umoria::town_visual(y, x)) {
    case umoria::TownVisual::Cliff: m.terrain = kMountain; m.flags = CELL_FEAT_WALL; m.kind = MinimapKind::Mountain; return m;
    case umoria::TownVisual::Water: m.terrain = kDeepWater; m.flags = CELL_FEAT_WATER; m.kind = MinimapKind::Water; return m;
    case umoria::TownVisual::Tree: m.terrain = kTree; m.flags = CELL_FEAT_TREE; m.kind = MinimapKind::Tree; return m;
    case umoria::TownVisual::Ruin: m.terrain = kRubble; m.flags = CELL_FEAT_RUBBLE; return m;
    case umoria::TownVisual::Grass: m.terrain = kGrass; m.kind = MinimapKind::Grass; break; // 床の上の物は下で拾う
    case umoria::TownVisual::None: break;
    }
    if (tile.treasure_id == 0) return m;

    const Inventory_t &item = game.treasure.list[tile.treasure_id];
    switch (item.category_id) {
    case TV_INVIS_TRAP:
        break; // 見つかるまでは床
    case TV_VIS_TRAP:
        m.terrain = trap_terrain(item.sub_category_id);
        m.trap = true;
        break;
    case TV_RUBBLE: m.terrain = kRubble; m.flags = CELL_FEAT_RUBBLE; break;
    case TV_OPEN_DOOR:
        m.terrain = item.misc_use != 0 ? kBrokenDoor : kOpenDoor;
        m.flags = CELL_FEAT_DOOR | CELL_FEAT_DOOR_OPEN | CELL_FEAT_PASSABLE;
        break;
    case TV_CLOSED_DOOR:
        m.terrain = item.misc_use > 0 ? kLockedDoor : (item.misc_use < 0 ? kJammedDoor : kClosedDoor);
        m.flags = CELL_FEAT_DOOR;
        break;
    case TV_SECRET_DOOR: m.terrain = kSecretDoor; m.flags = CELL_FEAT_WALL; break;
    case TV_UP_STAIR: m.terrain = kUpStair; m.flags = CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE; break;
    case TV_DOWN_STAIR: m.terrain = kDownStair; m.flags = CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE; break;
    case TV_STORE_DOOR:
        m.terrain = static_cast<std::uint16_t>(kGeneralStore + (item.sub_category_id - 101));
        m.flags = CELL_FEAT_PASSABLE;
        break;
    case TV_GOLD:
        // 壁の中の金は「財宝入りの鉱脈」。床の金はアイテム。
        if (tile.feature_id == TILE_MAGMA_WALL) { m.terrain = kMagmaTreasure; break; }
        if (tile.feature_id == TILE_QUARTZ_WALL) { m.terrain = kQuartzTreasure; break; }
        m.item = true;
        m.item_tval = TV_GOLD;
        m.object = &item;
        break;
    default:
        if (item.category_id <= TV_MAX_OBJECT) {
            m.item = true;
            m.item_tval = item.category_id;
            m.object = &item;
        }
        break;
    }
    return m;
}

MinimapKind minimap_kind(const CellMeaning &m)
{
    if (m.kind != MinimapKind::Unknown) return m.kind;
    if ((m.flags & CELL_FEAT_WALL) != 0) return MinimapKind::Wall;
    if ((m.flags & CELL_FEAT_RUBBLE) != 0) return MinimapKind::Wall;
    if ((m.flags & CELL_FEAT_DOOR) != 0) return MinimapKind::Door;
    if ((m.flags & CELL_FEAT_STAIRS) != 0) return MinimapKind::Stairs;
    return MinimapKind::Floor;
}

std::string term_text(int row, int col_begin, int col_end)
{
    if (umoria::lang_ja()) return umoria::term_line(row, col_begin, col_end).utf8;
    std::string text;
    int last = col_begin - 1;
    for (int col = col_begin; col < col_end; ++col) {
        if (umoria::term_char(row, col) != ' ') last = col;
    }
    for (int col = col_begin; col <= last; ++col) {
        const unsigned char ch = static_cast<unsigned char>(umoria::term_char(row, col));
        text.push_back(ch >= 0x20 && ch < 0x7f ? static_cast<char>(ch) : ' ');
    }
    return text;
}

bool player_lit_monster(int creature_id)
{
    return creature_id > 1 && monsters[creature_id].lit;
}

/*
 * プレイヤの光源の種類（`LightingState::light_kind`。・CORE_HUD_ROLLOUT.md §2.2）。
 * 0 = 光が無い、1 = 松明、2 = ランタン、4 = それ以外。
 *
 * 光源の枠（`PlayerEquipment::Light`）の物を見る。装備していても燃料が切れた（`carrying_light` が偽。原作は毎手番、
 * `misc_use`＝残りの手番を数えて偽にする）ら 0。松明は束ねて持てる種類（`sub_category_id` が 192 以上）、
 * ランタンは束ねない。店で買った物（店用の番号）も同じ見方で分かれる。読むだけでコアの状態は変えない。
 */
std::uint8_t player_light_kind()
{
    if (!py.carrying_light) return 0;
    const Inventory_t &light = py.inventory[PlayerEquipment::Light];
    if (light.category_id != TV_LIGHT) return 4;
    return light.sub_category_id >= ITEM_GROUP_MIN ? 1 : 2;
}

//! 盲目か（盲目の間は光が目に届かない）。
bool player_is_blind()
{
    return (py.flags.status & config::player::status::PY_BLIND) != 0u || py.flags.blind > 0;
}

/*
 * 位置の確認（`L`。ローグライクの配置では `W`。原作の `commandLocateOnMap`）の最中か。
 *
 * 原作はこの間 `dg.panel`（見ている区画）だけを動かし、`py.pos`（自キャラ）は動かさない。1 行目は
 * 「Map sector [y,x], which is … your sector. Look which direction?」を出して方向を待つ。
 * 画面のカメラは自キャラを追うので、そのままでは十字キーを押しても 3D の絵が動かない（修-64。決めたこと 2026-10-12）。
 * Angband（`an_locate_active`）・Sil-Q（`sq_locate_active`）と同じく、`GameFrame::camera_detached` を立てて区画の中心へ向ける。
 * 見分けは 1 行目の字（原作は 1 バイトも変えない）。
 */
bool locate_panel_center(int &cx, int &cy)
{
    if (umoria::term_question() != umoria::Question::Command) return false;
    static const char kMarker[] = "Map sector";
    for (std::size_t i = 0; i + 1 < sizeof(kMarker); ++i) {
        if (umoria::term_char(0, static_cast<int>(i)) != kMarker[i]) return false;
    }
    cx = (dg.panel.left + dg.panel.right) / 2;
    cy = (dg.panel.top + dg.panel.bottom) / 2;
    return true;
}

void fill_cells_and_minimap(GameFrame &frame)
{
    const int w = dg.width;
    const int h = dg.height;
    const int px = py.pos.x;
    const int py_ = py.pos.y;
    frame.player_gx = px;
    frame.player_gy = py_;

    MinimapSnapshot &minimap = frame.minimap;
    minimap.width = w;
    minimap.height = h;
    minimap.player_gx = px;
    minimap.player_gy = py_;
    minimap.kinds.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    minimap.markers.assign(minimap.kinds.size(), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Tile_t &tile = dg.floor[y][x];
            if (!tile_known(tile)) continue;
            const CellMeaning m = cell_meaning(tile, y, x);
            const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            minimap.kinds[at] = static_cast<std::uint8_t>(minimap_kind(m));
            if (player_lit_monster(tile.creature_id)) minimap.markers[at] = static_cast<std::uint8_t>(MinimapMarker::Monster);
            else if (m.trap) minimap.markers[at] = static_cast<std::uint8_t>(MinimapMarker::Trap);
            else if (m.item) {
                minimap.markers[at] = static_cast<std::uint8_t>(
                    m.item_tval == TV_GOLD ? MinimapMarker::Treasure : MinimapMarker::Item);
            }
        }
    }
    if (px >= 0 && px < w && py_ >= 0 && py_ < h) {
        minimap.kinds[static_cast<std::size_t>(py_) * static_cast<std::size_t>(w) + static_cast<std::size_t>(px)]
            = static_cast<std::uint8_t>(MinimapKind::Player);
    }

    int center_x = px;
    int center_y = py_;
    frame.camera_detached = locate_panel_center(center_x, center_y);
    const int ox = std::max(0, std::min(center_x - kViewCols / 2, std::max(0, w - kViewCols)));
    const int oy = std::max(0, std::min(center_y - kViewRows / 2, std::max(0, h - kViewRows)));
    frame.cam_x = ox + kViewCols / 2;
    frame.cam_y = oy + kViewRows / 2;
    frame.view_w = kViewCols;
    frame.view_h = kViewRows;
    frame.cells.reserve(static_cast<std::size_t>(kViewCols) * kViewRows);

    const bool blind = (py.flags.status & config::player::status::PY_BLIND) != 0u;
    /*
     * 光の届く範囲。光源を持っていれば（`light_kind` ≠ 0）、`cells` と同じ並びで
     * 自分の周り 3x3（原作の `dungeonMoveCharacterLight` が `temporary_light` を立てる範囲）を 1 マス 1 ビットで送る。
     * 盲目の間は光が届かないので全部 0。`temporary_light` そのものは読まない——走っている間（原作は
     * `temporary_light_only` を偽にして立てない）にビットが消えて、光の範囲が明滅するため。
     */
    const std::uint8_t light_kind = player_light_kind();
    const bool light_reaches = light_kind != 0 && !player_is_blind();
    std::vector<std::uint8_t> player_lit;
    if (light_kind != 0) {
        player_lit.assign((static_cast<std::size_t>(kViewCols) * static_cast<std::size_t>(kViewRows) + 7) / 8, 0);
    }
    for (int gy = oy; gy < std::min(h, oy + kViewRows); ++gy) {
        for (int gx = ox; gx < std::min(w, ox + kViewCols); ++gx) {
            const Tile_t &tile = dg.floor[gy][gx];
            MapCellView cell{};
            cell.gx = static_cast<std::int16_t>(gx);
            cell.gy = static_cast<std::int16_t>(gy);
            const bool in_ring = light_reaches && std::abs(gx - px) <= 1 && std::abs(gy - py_) <= 1;
            if (in_ring) {
                const std::size_t index = frame.cells.size();
                player_lit[index / 8] = static_cast<std::uint8_t>(player_lit[index / 8] | (1u << (index % 8)));
            }
            const bool is_player = gx == px && gy == py_;
            if (!tile_known(tile) && !is_player) {
                frame.cells.push_back(cell); // 未探知
                continue;
            }
            const CellMeaning m = cell_meaning(tile, gy, gx);
            cell.terrain_id = m.terrain;
            cell.under_tile_index = presentation::lookup_terrain_tile(m.terrain);
            cell.feature_flags = static_cast<std::uint16_t>(m.flags | CELL_FEAT_KNOWN);
            if (tile.perma_lit_room) cell.feature_flags |= CELL_FEAT_ROOM;
            if (tile.permanent_light) cell.feature_flags |= CELL_FEAT_GLOWING;
            const bool lit_now = !blind && (tile.temporary_light || tile.permanent_light);
            cell.light_level = (lit_now || in_ring) ? 2 : 1; // 光の範囲のマスは、走っていて temporary_light が立たなくても 2
            cell.ascii_fallback = caveGetTileSymbol(Coord_t{gy, gx});

            if (is_player) {
                cell.feature_flags |= CELL_FEAT_PLAYER;
                cell.tile_index = presentation::lookup_player_tile();
            } else if (!blind && player_lit_monster(tile.creature_id)) {
                const Monster_t &monster = monsters[tile.creature_id];
                cell.monster_slot = static_cast<std::uint16_t>(tile.creature_id);
                if (monster.creature_id < umoria::kCreatureMonraceCount) {
                    const std::uint16_t monrace = umoria::kCreatureMonrace[monster.creature_id];
                    cell.monster_id = monrace;
                    cell.tile_index = presentation::lookup_monster_tile(monrace);
                }
            } else if (m.item && lit_now) {
                const std::uint16_t baseitem = m.object != nullptr ? item_baseitem(*m.object) : representative_baseitem(m.item_tval);
                cell.object_id = baseitem;
                cell.tile_index = baseitem != 0 ? presentation::lookup_object_tile(baseitem) : 0;
            }
            frame.cells.push_back(cell);
        }
    }
    if (light_kind != 0) {
        player_lit.resize((frame.cells.size() + 7) / 8); // cells の数ぶんに切り詰めて渡す
        frame.lighting.player_lit = std::move(player_lit);
    }
}

/*
 * 部屋の種類を形から判じる（2026-10-03。「M2 の残り」§2）。
 *
 * 原作は内側の壁を生成の途中だけ `TMP1_WALL` にして、最後に花崗岩へ塗る。だから生成が終わると
 * 部屋の種類は残っていない。`dungeonBuildRoomWithInnerRooms` の寸法は決まっている
 * （中心から内側の壁が縦 ±3・横 ±10）ので、その形を探して中身で分ける。
 * 十字の部屋の中央の大柱（`dungeonPlaceLargeMiddlePillar`）は 3x3 の壁の塊として探す。
 *
 * 意匠の id は `dungeon_styles.jsonc` の Umoria の節が持つ（1001〜1004）。
 */
namespace {
constexpr int kZoneVault = 1001; // 封じられた宝物庫
constexpr int kZonePillarHall = 1002; // 柱の間
constexpr int kZoneMaze = 1003; // 崩れた坑道
constexpr int kZoneFourRooms = 1004; // ドワーフの居室

bool closed_at(int y, int x)
{
    if ((y < 0) || (y >= MAX_HEIGHT) || (x < 0) || (x >= MAX_WIDTH)) {
        return true;
    }
    return dg.floor[y][x].feature_id >= MIN_CLOSED_SPACE || dg.floor[y][x].feature_id == TILE_NULL_WALL;
}

bool open_room_at(int y, int x)
{
    if ((y < 0) || (y >= MAX_HEIGHT) || (x < 0) || (x >= MAX_WIDTH)) {
        return false;
    }
    const Tile_t &t = dg.floor[y][x];
    return t.perma_lit_room && (t.feature_id != TILE_NULL_WALL) && (t.feature_id <= MAX_OPEN_SPACE);
}

std::vector<FloorZone> classify_rooms()
{
    std::vector<FloorZone> zones;
    std::vector<std::vector<bool>> taken(MAX_HEIGHT, std::vector<bool>(MAX_WIDTH, false));
    const auto add = [&](int style, int x0, int y0, int x1, int y1) {
        zones.push_back(FloorZone{ style, x0, y0, x1, y1 });
        for (int y = std::max(0, y0); y <= std::min(MAX_HEIGHT - 1, y1); ++y) {
            for (int x = std::max(0, x0); x <= std::min(MAX_WIDTH - 1, x1); ++x) {
                taken[y][x] = true;
            }
        }
    };
    // 内側の部屋つきの部屋。
    for (int cy = 4; cy < MAX_HEIGHT - 4; ++cy) {
        for (int cx = 11; cx < MAX_WIDTH - 11; ++cx) {
            if (taken[cy][cx]) {
                continue;
            }
            bool border = true;
            for (int x = cx - 10; border && (x <= cx + 10); ++x) {
                border = closed_at(cy - 3, x) && closed_at(cy + 3, x);
            }
            for (int y = cy - 3; border && (y <= cy + 3); ++y) {
                border = closed_at(y, cx - 10) && closed_at(y, cx + 10);
            }
            if (!border) {
                continue;
            }
            bool ring = true; // 内側の壁の外を回る廊下（部屋の床）
            for (int x = cx - 9; ring && (x <= cx + 9); ++x) {
                ring = open_room_at(cy - 4, x) && open_room_at(cy + 4, x);
            }
            if (!ring) {
                continue;
            }
            int walls = 0;
            for (int y = cy - 2; y <= cy + 2; ++y) {
                for (int x = cx - 9; x <= cx + 9; ++x) {
                    walls += closed_at(y, x) ? 1 : 0;
                }
            }
            bool cross = true;
            for (int x = cx - 9; cross && (x <= cx + 9); ++x) {
                cross = closed_at(cy, x);
            }
            for (int y = cy - 2; cross && (y <= cy + 2); ++y) {
                cross = closed_at(y, cx);
            }
            bool vault_ring = true;
            for (int y = cy - 1; y <= cy + 1; ++y) {
                for (int x = cx - 1; x <= cx + 1; ++x) {
                    if (((y != cy) || (x != cx)) && !closed_at(y, x)) {
                        vault_ring = false;
                    }
                }
            }
            if (walls >= 40) {
                add(kZoneMaze, cx - 10, cy - 3, cx + 10, cy + 3);
            } else if (cross) {
                add(kZoneFourRooms, cx - 10, cy - 3, cx + 10, cy + 3);
            } else if (vault_ring && !closed_at(cy, cx)) {
                add(kZoneVault, cx - 10, cy - 3, cx + 10, cy + 3);
            } else if (vault_ring) {
                add(kZonePillarHall, cx - 12, cy - 5, cx + 12, cy + 5);
            }
        }
    }
    // 十字の部屋の中央の大柱（3x3 の壁の塊を部屋の床が囲む）。
    for (int cy = 2; cy < MAX_HEIGHT - 2; ++cy) {
        for (int cx = 2; cx < MAX_WIDTH - 2; ++cx) {
            if (taken[cy][cx]) {
                continue;
            }
            bool solid = true;
            for (int y = cy - 1; solid && (y <= cy + 1); ++y) {
                for (int x = cx - 1; solid && (x <= cx + 1); ++x) {
                    solid = closed_at(y, x) && dg.floor[y][x].perma_lit_room;
                }
            }
            if (!solid) {
                continue;
            }
            bool around = true;
            for (int d = -2; around && (d <= 2); ++d) {
                around = open_room_at(cy - 2, cx + d) && open_room_at(cy + 2, cx + d) && open_room_at(cy + d, cx - 2)
                    && open_room_at(cy + d, cx + 2);
            }
            if (around) {
                add(kZonePillarHall, cx - 5, cy - 5, cx + 5, cy + 5);
            }
        }
    }
    return zones;
}
} // namespace

void fill_floor(GameFrame &frame)
{
    static int last_level = -1;
    static std::uint64_t serial = 0;
    static int last_turn_bucket = -1;
    // 階が変わったら番号を進める（画面側はこれでフロアの作り直しを知る）。同じ階の作り直し（町へ戻るなど）も拾う。
    if (dg.current_level != last_level || dg.game_turn < last_turn_bucket) {
        ++serial;
        last_level = dg.current_level;
    }
    last_turn_bucket = dg.game_turn;
    const bool town = dg.current_level == 0;
    frame.floor.kind = town ? 1 : 2; // FloorKind::Surface / Dungeon
    frame.floor.town_id = town ? 1 : 0;
    frame.floor.dungeon_id = town ? 0 : 1;
    frame.floor.dun_level = dg.current_level;
    frame.floor.generated_turn = serial;
    static std::uint64_t zones_serial = 0;
    static std::vector<FloorZone> zones;
    if (zones_serial != serial) {
        zones_serial = serial;
        zones = town ? std::vector<FloorZone>{} : classify_rooms();
    }
    frame.floor.zones = zones;
    frame.floor.place_name_utf8 = town ? "Town" : std::to_string(dg.current_level * 50) + " ft";
    // 町の昼夜は原作と同じ数え方（5000 ターンごとに入れ替わる）。
    frame.lighting.daytime = !town || ((dg.game_turn / 5000) % 2) == 0;
    frame.lighting.day_minute = frame.lighting.daytime ? 720 : 0;
    /*
     * Umoria の灯りは半径 1（松明・ランタンとも）。光源を持っていて目が見えるときだけ 1。
     * 光源の種類と、照らされたマスのビット列は `fill_cells_and_minimap()` が詰める。
     */
    frame.lighting.light_radius = (player_light_kind() != 0 && !player_is_blind()) ? 1 : 0;
    frame.lighting.light_kind = player_light_kind();
}

void fill_hud(GameFrame &frame)
{
    frame.hud.name = py.misc.name;
    frame.hud.hp = py.misc.current_hp;
    frame.hud.hp_max = py.misc.max_hp;
    frame.hud.sp = py.misc.current_mana;
    frame.hud.sp_max = py.misc.mana;
    frame.hud.gold = py.misc.au;
    frame.hud.depth = dg.current_level;
    frame.hud.level = py.misc.level;
}

void fill_status_and_bottom(GameFrame &frame)
{
    /*
     * 全画面の窓（持ち物の一覧・ヘルプ・店など）の間は、最後に地図を出していた時の状態の列と最下行を送る（変愚の
     * presentation_bridge.cpp の fill_status_col と同じ）。窓の字を読むと、状態の列のパネルに窓の本文の頭が入った
     * （日本語のヘルプで 13 桁を超えた。m6_ui_audit.py。2026-10-04）。窓の中身は menu_term_lines に全部の行で入る。
     */
    static std::vector<SubPanelLine> map_status;
    static std::vector<TermTextRun> map_bottom;
    frame.status_col_cols = kStatusCols;
    frame.bottom_row_cols = umoria::kTermCols;
    const bool on_map = umoria::map_screen_active();
    if (!on_map && !map_status.empty()) {
        frame.status_col_lines = map_status;
        frame.bottom_row_runs = map_bottom;
        return;
    }
    for (int row = 1; row < umoria::kTermRows - 1; ++row) {
        SubPanelLine line;
        line.text_utf8 = term_text(row, 0, kStatusCols);
        frame.status_col_lines.push_back(std::move(line));
    }
    const std::string bottom = term_text(umoria::kTermRows - 1, 0, umoria::kTermCols);
    if (!bottom.empty()) frame.bottom_row_runs.push_back(TermTextRun{ 0, bottom, 1 });
    if (on_map) {
        map_status = frame.status_col_lines;
        map_bottom = frame.bottom_row_runs;
    }
}

void fill_messages(GameFrame &frame)
{
    // 直近の数行を、古い順に毎回丸ごと送る（seq はフレームの中の位置。an_frame.cpp と同じ理由）。
    constexpr int kLines = 8;
    for (int age = kLines - 1; age >= 0; --age) {
        const int at = (last_message_id - age + MESSAGE_HISTORY_SIZE) % MESSAGE_HISTORY_SIZE;
        const char *text = messages[at];
        if (text == nullptr || text[0] == '\0') continue;
        MessageEvent event;
        event.seq = static_cast<std::uint32_t>(frame.messages.size());
        event.color = 1;
        event.text_utf8 = text;
        if (umoria::lang_ja()) {
            /*
             * 履歴は 1 行に続けて書いた文を「空白 2 つ」で連ねて持つ（um_io.cpp の printMessage）。文ごとに訳す。
             * ただし原作の 1 つの文にも空白 2 つを含むものがある（店主の台詞「Finally!  I accept.」）。前から、なるべく長い
             * まとまりで表を引き、当たらなければ短くする（文ごとに分けて引いていたので台詞が英語のままだった。2026-10-04）。
             */
            std::vector<std::string> parts;
            const std::string all = text;
            for (std::size_t at = 0; at < all.size();) {
                const std::size_t next = all.find("  ", at);
                parts.push_back(all.substr(at, next == std::string::npos ? std::string::npos : next - at));
                if (next == std::string::npos) break;
                at = all.find_first_not_of(' ', next);
                if (at == std::string::npos) break;
            }
            std::string joined;
            for (std::size_t i = 0; i < parts.size();) {
                std::size_t j = parts.size();
                std::string shown;
                for (; j > i; --j) {
                    std::string piece = parts[i];
                    for (std::size_t k = i + 1; k < j; ++k) piece += "  " + parts[k];
                    shown = umoria::tr(piece);
                    if (j == i + 1) break;
                    // 長いまとまりは、英語が残らずに訳せたときだけ使う（後ろの文まで飲み込んで一部だけ訳していた。2026-10-04）。
                    if (shown != piece && umoria::fully_translated(shown)) break;
                }
                if (!joined.empty()) joined += " ";
                joined += shown;
                i = j;
            }
            event.text_utf8 = joined;
        }
        frame.messages.push_back(std::move(event));
    }
}

std::string item_text(const Inventory_t &item)
{
    obj_desc_t text = { '\0' };
    itemDescription(text, item, true);
    return text;
}

//! 日本語か英語かで字を選ぶ（説明の字はアダプタが持つので、訳の表を通さずにここで両方書く）。
const char *ja_en(const char *ja, const char *en) { return umoria::lang_ja() ? ja : en; }

std::string signed_text(int value) { return (value >= 0 ? "+" : "") + std::to_string(value); }

/*
 * 状態のアイコン（`GameFrame::status_effects`。・CORE_HUD_ROLLOUT.md §2.3。決めたこと 2026-10-11）。
 *
 * 範囲は原作の最下段（空腹・盲目・混乱・恐怖・毒・麻痺・速さ）に、メッセージで知らされる一時的な状態
 * （幻覚・無敵・ヒーロー・スーパー・ヒーロー・祝福・対邪悪・耐火・耐冷・透明視・赤外線・帰還待ち・混乱打撃の手）を足したもの。
 * 常時の効果は**鑑定済み**（`ID_KNOWN2`）の装備の旗だけ——未鑑定の物の旗は出さない（原作より情報を増やさない）。
 * 装備の旗を集める枠は原作の `inventoryCollectAllItemFlags()` と同じ（武器から外套まで。光源と予備の武器は含めない）。
 * 残りターンは出さない。速さは原作が「Fast」「Very Fast」などの字だけを出すので効果量は空。
 * どれも読むだけでコアの状態は変えない。
 */
void add_status(std::vector<StatusEffectView> &out, const char *key, const char *ja, const char *en, StatusEffectKind kind)
{
    const std::string label = ja_en(ja, en);
    for (const StatusEffectView &have : out) {
        if (have.key == key && have.kind == kind && have.label_utf8 == label) return; // 同じ物の重なりは 1 つに
    }
    StatusEffectView view{};
    view.key = key;
    view.label_utf8 = label;
    view.kind = kind;
    out.push_back(std::move(view));
}

//! 常時の効果の表の 1 行。`flag` が鑑定済みの装備にあれば出す。
struct PermanentFlag {
    std::uint32_t flag;
    const char *key;
    const char *ja;
    const char *en;
    StatusEffectKind kind;
};

std::vector<StatusEffectView> collect_status_effects()
{
    namespace pst = config::player::status;
    namespace trf = config::treasure::flags;
    constexpr StatusEffectKind Good = StatusEffectKind::Good;
    constexpr StatusEffectKind Bad = StatusEffectKind::Bad;
    std::vector<StatusEffectView> out;
    const auto &flags = py.flags;

    // (1) 速さ。原作の `printCharacterSpeed` と同じ（探索の構えの分は引く）。値が正のほど遅い。
    int speed = flags.speed;
    if ((flags.status & pst::PY_SEARCH) != 0u) speed--;
    if (speed > 1) add_status(out, "slow", "超減速", "Very Slow", Bad);
    else if (speed == 1) add_status(out, "slow", "減速", "Slow", Bad);
    else if (speed == -1) add_status(out, "haste", "加速", "Fast", Good);
    else if (speed < -1) add_status(out, "haste", "超加速", "Very Fast", Good);

    // (2) 最下段の状態。空腹は `printCharacterHungerStatus` と同じ（衰弱が先）。
    if ((flags.status & pst::PY_WEAK) != 0u) add_status(out, "hungry", "衰弱", "Weak", Bad);
    else if ((flags.status & pst::PY_HUNGRY) != 0u) add_status(out, "hungry", "空腹", "Hungry", Bad);
    if ((flags.status & pst::PY_BLIND) != 0u || flags.blind > 0) add_status(out, "blind", "盲目", "Blind", Bad);
    if ((flags.status & pst::PY_CONFUSED) != 0u || flags.confused > 0) add_status(out, "confused", "混乱", "Confused", Bad);
    if ((flags.status & pst::PY_FEAR) != 0u || flags.afraid > 0) add_status(out, "afraid", "恐怖", "Afraid", Bad);
    if ((flags.status & pst::PY_POISONED) != 0u || flags.poisoned > 0) add_status(out, "poisoned", "毒", "Poisoned", Bad);
    if (flags.paralysis > 1) add_status(out, "paralyzed", "麻痺", "Paralysed", Bad);

    // (3) メッセージで知らされる一時的な状態。
    if (flags.image > 0) add_status(out, "hallucination", "幻覚", "Hallucination", Bad);
    if (flags.invulnerability > 0) add_status(out, "invuln", "無敵", "Invulnerable", Good);
    if (flags.heroism > 0) add_status(out, "hero", "ヒーロー", "Heroism", Good);
    if (flags.super_heroism > 0) add_status(out, "berserk", "スーパー・ヒーロー", "Super Heroism", Good);
    if (flags.blessed > 0) add_status(out, "blessed", "祝福", "Blessed", Good);
    if (flags.protect_evil > 0) add_status(out, "protevil", "対邪悪結界", "Prot. from Evil", Good);
    if (flags.heat_resistance > 0) add_status(out, "res_fire", "耐火", "Resist Heat", Good);
    if (flags.cold_resistance > 0) add_status(out, "res_cold", "耐冷", "Resist Cold", Good);
    if (flags.detect_invisible > 0) add_status(out, "senseunseen", "透明視", "See Invisible", Good);
    if (flags.timed_infra > 0) add_status(out, "infravision", "赤外線視力", "Infravision", Good);
    if (flags.confuse_monster) add_status(out, "attk_conf", "混乱打撃", "Confusing Touch", Good);
    if (flags.word_of_recall > 0) add_status(out, "recall", "帰還待ち", "Recall", StatusEffectKind::Waiting);

    // (4) 常時の効果。鑑定済みの装備の旗だけ。
    constexpr StatusEffectKind Perm = StatusEffectKind::Permanent;
    constexpr StatusEffectKind PermBad = StatusEffectKind::PermanentBad;
    static const PermanentFlag kPermanent[] = {
        { trf::TR_RES_FIRE, "res_fire", "耐火", "Resist Fire", Perm },
        { trf::TR_RES_ACID, "res_acid", "耐酸", "Resist Acid", Perm },
        { trf::TR_RES_COLD, "res_cold", "耐冷", "Resist Cold", Perm },
        { trf::TR_RES_LIGHT, "res_elec", "耐電撃", "Resist Lightning", Perm }, // 旗の名前は RES_LIGHT だが、原作が出す字は電撃（持ち物の説明と同じ）
        { trf::TR_FREE_ACT, "free_act", "麻痺知らず", "Free Action", Perm },
        { trf::TR_SEE_INVIS, "senseunseen", "透明視", "See Invisible", Perm },
        { trf::TR_REGEN, "regeneration", "再生", "Regeneration", Perm },
        { trf::TR_SLOW_DIGEST, "slow_digest", "遅消化", "Slow Digestion", Perm },
        { trf::TR_FFALL, "levitate", "浮遊", "Feather Falling", Perm }, // 羽根の絵。落下で傷つかない
        { trf::TR_STEALTH, "stealth", "隠密", "Stealth", Perm },
        { trf::TR_SEARCH, "search", "探索", "Searching", Perm },
        { trf::TR_TUNNEL, "tunnel", "採掘", "Tunneling", Perm },
        { trf::TR_INFRA, "infravision", "赤外線視力", "Infravision", Perm },
        { trf::TR_AGGRAVATE, "aggravate", "反感", "Aggravate", PermBad },
        { trf::TR_TELEPORT, "cursed", "テレポート", "Teleportation", PermBad }, // 勝手に飛ばされる呪い。呪いの絵を使う
    };
    // 武器の性質（手に持つ武器の枠だけ）。
    static const PermanentFlag kWeapon[] = {
        { trf::TR_SLAY_DRAGON, "slay_dragon", "ドラゴン倍打", "Slay Dragon", Perm },
        { trf::TR_SLAY_ANIMAL, "slay_animal", "動物倍打", "Slay Animal", Perm },
        { trf::TR_SLAY_EVIL, "slay_evil", "邪悪倍打", "Slay Evil", Perm },
        { trf::TR_SLAY_UNDEAD, "slay_undead", "不死倍打", "Slay Undead", Perm },
        { trf::TR_FROST_BRAND, "attk_cold", "凍結", "Frost Brand", Perm },
        { trf::TR_FLAME_TONGUE, "attk_fire", "焼棄", "Flame Tongue", Perm },
    };
    static const char *const kSustainKey[] = { "sust_str", "sust_int", "sust_wis", "sust_con", "sust_dex", "sust_chr" };
    static const char *const kSustainJa[] = { "腕力維持", "知能維持", "賢さ維持", "耐久力維持", "器用さ維持", "魅力維持" };
    static const char *const kSustainEn[] = { "Sust Str", "Sust Int", "Sust Wis", "Sust Con", "Sust Dex", "Sust Chr" };
    for (int slot = PlayerEquipment::Wield; slot < PlayerEquipment::Light; ++slot) {
        const Inventory_t &item = py.inventory[slot];
        if (item.category_id == TV_NOTHING || !spellItemIdentified(item)) continue; // 未鑑定の旗は出さない
        for (const PermanentFlag &row : kPermanent) {
            if ((item.flags & row.flag) != 0u) add_status(out, row.key, row.ja, row.en, row.kind);
        }
        if (slot == PlayerEquipment::Wield) {
            for (const PermanentFlag &row : kWeapon) {
                if ((item.flags & row.flag) != 0u) add_status(out, row.key, row.ja, row.en, row.kind);
            }
        }
        if ((item.flags & trf::TR_SUST_STAT) != 0u && item.misc_use >= 1 && item.misc_use <= 6) {
            const int which = item.misc_use - 1;
            add_status(out, kSustainKey[which], kSustainJa[which], kSustainEn[which], Perm);
        }
        // 呪いの旗だけが立っていて、上で個別に出した呪い（反感・テレポート）でない物は「呪い」。
        if ((item.flags & trf::TR_CURSED) != 0u && (item.flags & (trf::TR_AGGRAVATE | trf::TR_TELEPORT)) == 0u) {
            add_status(out, "cursed", "呪い", "Cursed", PermBad);
        }
    }
    return out;
}

void fill_status_effects(GameFrame &frame)
{
    frame.status_effects = collect_status_effects();
}

//! 分類の名前（説明の 1 行目）。原作の分類番号（treasure.h の TV_*）ごと。
const char *category_name(int category)
{
    switch (category) {
    case TV_SLING_AMMO: case TV_BOLT: case TV_ARROW: return ja_en("弾・矢", "Ammunition");
    case TV_SPIKE: return ja_en("くさび", "Spike");
    case TV_LIGHT: return ja_en("光源", "Light source");
    case TV_BOW: return ja_en("射撃武器", "Missile weapon");
    case TV_HAFTED: return ja_en("鈍器", "Hafted weapon");
    case TV_POLEARM: return ja_en("長柄武器", "Polearm");
    case TV_SWORD: return ja_en("刀剣", "Sword");
    case TV_DIGGING: return ja_en("掘る道具", "Digging tool");
    case TV_BOOTS: return ja_en("靴", "Boots");
    case TV_GLOVES: return ja_en("小手", "Gloves");
    case TV_CLOAK: return ja_en("外套", "Cloak");
    case TV_HELM: return ja_en("兜", "Helm");
    case TV_SHIELD: return ja_en("盾", "Shield");
    case TV_HARD_ARMOR: return ja_en("硬い鎧", "Hard armor");
    case TV_SOFT_ARMOR: return ja_en("軟らかい鎧", "Soft armor");
    case TV_AMULET: return ja_en("アミュレット", "Amulet");
    case TV_RING: return ja_en("指輪", "Ring");
    case TV_STAFF: return ja_en("杖", "Staff");
    case TV_WAND: return ja_en("魔法棒", "Wand");
    case TV_SCROLL1: case TV_SCROLL2: return ja_en("巻物", "Scroll");
    case TV_POTION1: case TV_POTION2: return ja_en("薬", "Potion");
    case TV_FLASK: return ja_en("油つぼ", "Flask of oil");
    case TV_FOOD: return ja_en("食べ物", "Food");
    case TV_MAGIC_BOOK: return ja_en("魔術書", "Book of magic spells");
    case TV_PRAYER_BOOK: return ja_en("祈祷書", "Holy book of prayers");
    default: return "";
    }
}

/*
 * 持ち物一覧で品物を選んだときの説明（決めたこと 2026-10-04「Heng と同様に能力や説明を表示させて」）。
 * 変愚（presentation/bridge/modern_store.cpp の item_details）と同じ形で、効果（effects）と説明（description）を埋める。
 * **見せるのは原作がプレイヤーに見せている範囲だけ**: ダメージのダイスと基本防御力は原作も名前に出す。命中・ダメージ・防御力の
 * 補正と能力値の修正は、原作が名前に (+x,+y)・(+2) を出す条件（鑑定済み＝spellItemIdentified）と同じときだけ出す。
 * 能力（耐性など）も鑑定済みのときだけ。魔法書は、原作の「本を読む」と同じ中身（その職業が使える呪文）を並べる。
 */
void item_details(ModernRow &row, const Inventory_t &item)
{
    namespace trf = config::treasure::flags;
    const int category = item.category_id;
    const bool known = spellItemIdentified(item);
    const bool wearable = category >= TV_MIN_WEAR && category <= TV_MAX_WEAR;
    const bool weapon = (category >= TV_SLING_AMMO && category <= TV_ARROW) || (category >= TV_HAFTED && category <= TV_DIGGING);

    const int tenths = static_cast<int>(item.weight) * std::max<int>(1, item.items_count);
    row.weight = std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + ja_en("ポンド", " lb");

    std::vector<std::string> lines;
    if (const char *kind = category_name(category); kind[0] != '\0') lines.push_back(std::string(ja_en("分類: ", "Type: ")) + kind);

    if (weapon && item.damage.dice > 0) {
        const int dice = item.damage.dice, sides = item.damage.sides;
        row.effects.push_back(std::string(ja_en("基礎ダメージ: ", "Base damage: ")) + std::to_string(dice) + "d" + std::to_string(sides)
            + " (" + std::to_string(dice) + "-" + std::to_string(dice * sides) + ")");
    }
    if (wearable && (item.ac != 0 || (known && item.to_ac != 0))) {
        std::string text = std::string(ja_en("基本防御力 ", "Base AC ")) + std::to_string(item.ac);
        if (known) text += std::string(ja_en(" / 防御力補正 ", " / AC bonus ")) + signed_text(item.to_ac);
        row.effects.push_back(text);
    }
    if (known && wearable && (weapon || category == TV_BOW || item.to_hit != 0 || item.to_damage != 0)) {
        row.effects.push_back(std::string(ja_en("命中補正 ", "To-hit ")) + signed_text(item.to_hit)
            + ja_en(" / ダメージ補正 ", " / to-dam ") + signed_text(item.to_damage));
    }
    if (known && wearable) {
        const std::pair<uint32_t, const char *> modifiers[] = {
            { trf::TR_STR, ja_en("腕力", "Strength") }, { trf::TR_INT, ja_en("知能", "Intelligence") },
            { trf::TR_WIS, ja_en("賢さ", "Wisdom") }, { trf::TR_DEX, ja_en("器用さ", "Dexterity") },
            { trf::TR_CON, ja_en("耐久力", "Constitution") }, { trf::TR_CHR, ja_en("魅力", "Charisma") },
            { trf::TR_SEARCH, ja_en("探索", "Searching") }, { trf::TR_STEALTH, ja_en("隠密", "Stealth") },
            { trf::TR_SPEED, ja_en("スピード", "Speed") }, { trf::TR_INFRA, ja_en("赤外線視力", "Infravision") },
            { trf::TR_TUNNEL, ja_en("掘削", "Tunneling") },
        };
        for (const auto &[flag, label] : modifiers) {
            if ((item.flags & flag) != 0) row.effects.push_back(std::string(label) + ": " + signed_text(item.misc_use));
        }
        const std::pair<uint32_t, const char *> abilities[] = {
            { trf::TR_RES_FIRE, ja_en("火炎への耐性", "Resist fire") }, { trf::TR_RES_COLD, ja_en("冷気への耐性", "Resist cold") },
            { trf::TR_RES_ACID, ja_en("酸への耐性", "Resist acid") }, { trf::TR_RES_LIGHT, ja_en("電撃への耐性", "Resist lightning") },
            { trf::TR_FREE_ACT, ja_en("麻痺知らず", "Free action") }, { trf::TR_SEE_INVIS, ja_en("透明なものが見える", "See invisible") },
            { trf::TR_SUST_STAT, ja_en("能力値の維持", "Sustain stat") }, { trf::TR_FFALL, ja_en("ゆっくり落ちる", "Feather fall") },
            { trf::TR_REGEN, ja_en("回復が速い", "Regeneration") }, { trf::TR_SLOW_DIGEST, ja_en("腹が減りにくい", "Slow digestion") },
            { trf::TR_SLAY_DRAGON, ja_en("ドラゴンに強い", "Slay dragon") }, { trf::TR_SLAY_ANIMAL, ja_en("動物に強い", "Slay animal") },
            { trf::TR_SLAY_EVIL, ja_en("邪悪に強い", "Slay evil") }, { trf::TR_SLAY_UNDEAD, ja_en("アンデッドに強い", "Slay undead") },
            { trf::TR_FROST_BRAND, ja_en("冷気の刃", "Frost brand") }, { trf::TR_FLAME_TONGUE, ja_en("炎の刃", "Flame tongue") },
            { trf::TR_AGGRAVATE, ja_en("怪物を怒らせる", "Aggravates monsters") }, { trf::TR_TELEPORT, ja_en("勝手にテレポートする", "Random teleportation") },
            { trf::TR_CURSED, ja_en("呪われている", "Cursed") },
        };
        for (const auto &[flag, label] : abilities) {
            if ((item.flags & flag) != 0) row.effects.push_back(label);
        }
    }

    if (category == TV_MAGIC_BOOK || category == TV_PRAYER_BOOK) {
        const uint8_t type = classes[py.misc.class_id].class_to_use_mage_spells;
        const bool readable = (category == TV_MAGIC_BOOK && type == config::spells::SPELL_TYPE_MAGE)
            || (category == TV_PRAYER_BOOK && type == config::spells::SPELL_TYPE_PRIEST);
        if (!readable) {
            lines.push_back(ja_en("この職業には読めない本です。", "Your class cannot read this book."));
        } else {
            const int offset = type == config::spells::SPELL_TYPE_MAGE ? config::spells::NAME_OFFSET_SPELLS : config::spells::NAME_OFFSET_PRAYERS;
            lines.push_back(ja_en("入っている呪文（Lv・マナ・失敗率）:", "Spells (level, mana, fail):"));
            uint32_t bits = item.flags;
            while (bits != 0u) {
                const int id = getAndClearFirstBit(bits);
                const Spell_t &spell = magic_spells[py.misc.class_id - 1][id];
                if (spell.level_required >= 99) continue;
                std::string line = "  " + umoria::tr(spell_names[id + offset]) + "  Lv " + std::to_string(spell.level_required)
                    + ja_en("  マナ ", "  mana ") + std::to_string(spell.mana_required) + "  " + std::to_string(spellChanceOfSuccess(id)) + "%";
                if ((py.flags.spells_learnt & (1UL << id)) == 0) line += ja_en("（未修得）", " (unknown)");
                else if ((py.flags.spells_forgotten & (1UL << id)) != 0) line += ja_en("（忘れた）", " (forgotten)");
                lines.push_back(line);
            }
        }
    }
    if (wearable && !known) lines.push_back(ja_en("まだ分かっていない性質があるかもしれません。", "Some properties may still be unknown."));

    row.description.clear();
    for (const std::string &line : lines) {
        if (!row.description.empty()) row.description += "\n";
        row.description += line;
    }
}

/*
 * サブパネルの中身。原作は 80×24 の画面 1 枚だけで別窓を持たないので、Rogue 系と同じく
 * 画面側の既定の描き方（装備＝sub2・視界の敵＝sub3・持ち物＝sub5）へ渡す。どの枠に出すかは画面側で選ぶ
 * （`presentation/bridge/rogue_sub_panels.h`）。M5 の日本語化までは英語。
 */
void fill_sub_panels(GameFrame &frame)
{
    /*
     * 部位の名前は原作の装備の一覧（ui_inventory.cpp の displayEquipment）と同じ字にし、日本語では同じ訳を引く。
     * 名前も訳す（日本語でも英語のままだった。気づいたこと 2026-10-04「サブパネルの装備や持ち物が英語のまま」）。
     */
    static constexpr const char *kSlots[] = { "Wielding", "On head", "Around neck", "On body", "On arm", "On hands",
        "On right hand", "On left hand", "On feet", "About body", "Light source", "Spare weapon" };
    for (int slot = PlayerEquipment::Wield; slot <= PlayerEquipment::Auxiliary; ++slot) {
        const Inventory_t &item = py.inventory[slot];
        if (item.category_id == TV_NOTHING) continue;
        frame.sub2_lines.push_back(umoria::tr(kSlots[slot - PlayerEquipment::Wield]) + ": " + umoria::tr(item_text(item)));
    }
    if (frame.sub2_lines.empty()) frame.sub2_lines.emplace_back(ja_en("（何も装備していない）", "(nothing)"));

    for (int i = 0; i < py.pack.unique_items && i < PlayerEquipment::Wield; ++i) {
        std::string line = "a) ";
        line[0] = static_cast<char>('a' + i);
        frame.sub5_lines.push_back(line + umoria::tr(item_text(py.inventory[i])));
    }
    if (frame.sub5_lines.empty()) frame.sub5_lines.emplace_back(ja_en("（何も持っていない）", "(empty)"));

    // 見えている敵を近い順に、種類ごとに 1 行（同じ種類は数を添える）。
    struct Seen { int distance; int creature; int count; };
    std::vector<Seen> seen;
    for (int id = config::monsters::MON_MIN_INDEX_ID; id < next_free_monster_id; ++id) {
        const Monster_t &monster = monsters[id];
        if (monster.hp <= 0 || !monster.lit) continue;
        auto found = std::find_if(seen.begin(), seen.end(), [&](const Seen &s) { return s.creature == monster.creature_id; });
        if (found != seen.end()) {
            ++found->count;
            found->distance = std::min<int>(found->distance, monster.distance_from_player);
        } else {
            seen.push_back(Seen{ monster.distance_from_player, monster.creature_id, 1 });
        }
    }
    std::sort(seen.begin(), seen.end(), [](const Seen &a, const Seen &b) { return a.distance < b.distance; });
    for (const Seen &s : seen) {
        std::string line = umoria::tr(creatures_list[s.creature].name);
        if (s.count > 1) line += " (x" + std::to_string(s.count) + ")";
        frame.sub3_lines.push_back(std::move(line));
    }
    if (frame.sub3_lines.empty()) frame.sub3_lines.emplace_back(ja_en("（見えない）", "(none)"));
}

} // namespace

namespace umoria {

/*
 * 持ち物一覧の行（M3。画面側の「持ち物一覧」タブ＝`NativeInventoryMenu`）。
 * 持ち物は selection 0・番号は pack の添字、装備は selection 1・番号は装備の一覧の行（原作の
 * displayEquipment と同じ、着けている物だけを Wield から順に a, b, …）。どちらも字は原作の問いの字と同じ。
 * 行ごとの命令は原作のコマンドの字で持つ（部品が pad_commands から名札とキー列を引く）。
 */
std::vector<ModernRow> owned_rows()
{
    std::vector<ModernRow> rows;
    const auto make = [](const Inventory_t &item, int selection, int slot) {
        ModernRow row;
        obj_desc_t text = { 0 };
        itemDescription(text, item, true);
        std::string label(text);
        while (!label.empty() && (label.back() == '.' || label.back() == ' ')) label.pop_back();
        // 日本語では名前を訳す（新しい UI の窓の行。訳さずに英語のまま出していた。2026-10-04）。持ち物一覧から選び直すときの
        // 突き合わせも、この関数の行どうし（g_item_prompt_rows と持ち物一覧）なので、両方そろって訳される。
        label = umoria::tr(label);
        row.id = std::string(selection == 0 ? "item:" : "equip:") + static_cast<char>('a' + slot);
        row.label = label;
        item_details(row, item);
        row.count = std::max<int>(1, item.items_count);
        row.key = 'a' + slot;
        row.hotkey = row.key;
        row.selection = selection;
        row.slot = slot;
        row.tab = 0;
        const std::uint16_t baseitem = item_baseitem(item);
        row.tile = baseitem != 0 ? presentation::lookup_object_tile(baseitem) : 0;
        row.equipped = selection == 1;
        return row;
    };
    const auto act = [](ModernRow &row, int key) { row.actions.push_back({ "", "", "", true, key }); };
    for (int i = 0; i < PlayerEquipment::Wield; ++i) {
        const Inventory_t &item = py.inventory[i];
        if (item.category_id == TV_NOTHING) continue;
        ModernRow row = make(item, 0, i);
        // 部品（NativeInventoryMenu）は行の命令を逆順に並べるので、主な命令ほど後に積む。
        act(row, 'd');
        act(row, '{');
        act(row, 'f');
        if (item.category_id >= TV_MIN_WEAR && item.category_id <= TV_MAX_WEAR) act(row, 'w');
        switch (item.category_id) {
        case TV_FOOD: act(row, 'E'); break;
        case TV_POTION1: case TV_POTION2: act(row, 'q'); break;
        case TV_SCROLL1: case TV_SCROLL2: act(row, 'r'); break;
        case TV_WAND: act(row, 'a'); break;
        case TV_STAFF: act(row, 'u'); break;
        case TV_MAGIC_BOOK: case TV_PRAYER_BOOK: act(row, 'b'); break;
        case TV_FLASK: act(row, 'F'); break;
        default: break;
        }
        rows.push_back(std::move(row));
    }
    int line = 0;
    for (int slot = PlayerEquipment::Wield; slot < PLAYER_INVENTORY_SIZE; ++slot) {
        const Inventory_t &item = py.inventory[slot];
        if (item.category_id == TV_NOTHING) continue;
        ModernRow row = make(item, 1, line++);
        act(row, '{');
        act(row, 't');
        rows.push_back(std::move(row));
    }
    return rows;
}

bool map_screen_active()
{
    if (!game.character_generated || game.character_is_dead) return false;
    /*
     * コマンドのキーを読んでいる間は地図の画面。原作は画面を退避したあと、画面が変わっていなければ
     * 復元を呼ばずに戻る（ui_inventory.cpp の `current_screen_id != Screen::Blank` のときだけ復元）ので、
     * 退避の旗だけでは「まだメニューの中」と取り違える。
     */
    if (umoria_command_input_active) return true;
    /*
     * 方向の問い（`getDirectionWithMemory` の「Which direction?」など）は必ず地図の上で聞かれる。
     * 持ち物の画面から戻ったあとは退避の旗が残っていることがあり（上の注記と同じ理由）、
     * 旗だけで見ると地図の画が端末の字の頁に化けた（M3。2026-10-03）。
     */
    if (term_question() == Question::Command) {
        std::string top;
        for (int col = 0; col < kTermCols; ++col) top.push_back(term_char(0, col));
        if (top.find("direction") != std::string::npos || top.find("Direction") != std::string::npos) return true;
    }
    /*
     * 退避の旗が立っていても、退避の後に地図の場所へ字の列を書いていなければ、画面は地図のまま（原作は持ち物の一覧を出さずに
     * 品物を選ばせたときも画面を退避し、一覧を描かなかったので復元もしない）。そのときのメッセージ（「You are wearing …」）・
     * [y/n] の問い（「Drop all …?」）・敵の手番のメッセージを窓にしていた（m6_ui_audit.py の「無駄な全画面の窓」。2026-10-04）。
     * 「退避した時の画面のまま」で見る最初の直しは、感知の巻物が地図に物を描くと外れた。地図の変化は panelPutTile で書くので数えない。
     */
    if (term_screen_saved() && term_overlay_since_save()) return false;
    // 地図の画面なら、プレイヤーの位置に '@' が描かれている（走行中で描かない設定のときもある）。
    const int row = py.pos.y - dg.panel.row_prt;
    const int col = py.pos.x - dg.panel.col_prt;
    if (row < 1 || row >= kTermRows - 1 || col < kStatusCols || col >= kTermCols) return false;
    return term_char(row, col) == '@' || py.running_tracker != 0;
}

void fill_world_frame(GameFrame &frame)
{
    fill_cells_and_minimap(frame);
    fill_floor(frame);
    fill_hud(frame);
    fill_status_effects(frame);
    fill_status_and_bottom(frame);
    fill_messages(frame);
    fill_sub_panels(frame);
}

/*
 * `UmoriaCore.exe --selftest`: 状態のアイコン（`collect_status_effects`）を、実機で作りにくい組み合わせも含めて全数で照合する。
 * 空腹・未鑑定の装備の旗・光源や予備の武器の枠・重なりの 1 つ化・日英の名前。キャラクターもゲームの初期化も要らない
 * （`py` を直に書き換えるだけ。このプロセスはこの検査で終わる）。
 */
int status_effects_selftest()
{
    namespace pst = config::player::status;
    namespace trf = config::treasure::flags;
    int failures = 0;
    std::set<std::string> sent_keys; //!< 送った鍵の全部（最後に画面側の表と突き合わせる）
    const auto describe = [](const std::vector<StatusEffectView> &list, bool with_label) {
        std::string text;
        for (const StatusEffectView &e : list) {
            if (!text.empty()) text += ",";
            text += e.key + "/" + std::to_string(static_cast<int>(e.kind));
            if (with_label) text += "=" + e.label_utf8;
            if (!e.amount_utf8.empty()) text += "!" + e.amount_utf8; // 効果量を出してはいけない物の見張り
        }
        return text;
    };
    const auto check = [&](const char *what, const std::string &want, bool with_label = false) {
        const std::vector<StatusEffectView> list = collect_status_effects();
        for (const StatusEffectView &e : list) sent_keys.insert(e.key);
        const std::string got = describe(list, with_label);
        if (got != want) {
            std::fprintf(stderr, "[umoria] selftest: status effects: FAIL %s: want [%s] got [%s]\n", what, want.c_str(), got.c_str());
            ++failures;
        }
    };
    const auto reset = [] { py = Player_t{}; };
    const auto wear = [](int slot, std::uint8_t category, std::uint32_t item_flags, bool known, int p1 = 0) {
        Inventory_t &item = py.inventory[slot];
        item = Inventory_t{};
        item.category_id = category;
        item.flags = item_flags;
        item.misc_use = static_cast<std::int16_t>(p1);
        item.identification = known ? config::identification::ID_KNOWN2 : 0;
    };

    set_language("en");
    reset();
    check("何もかかっていない", "");

    // 速さ（値が正のほど遅い。探索の構えの分は引く）。
    py.flags.speed = 1; check("遅い", "slow/1=Slow", true);
    py.flags.speed = 3; check("とても遅い", "slow/1=Very Slow", true);
    py.flags.speed = -1; check("速い", "haste/0=Fast", true);
    py.flags.speed = -3; check("とても速い", "haste/0=Very Fast", true);
    py.flags.speed = 1; py.flags.status = pst::PY_SEARCH; check("探索の構えの分は速さに数えない", "");
    reset();

    // 空腹（衰弱が先）。
    py.flags.status = pst::PY_HUNGRY; check("空腹", "hungry/1=Hungry", true);
    py.flags.status = pst::PY_HUNGRY | pst::PY_WEAK; check("衰弱", "hungry/1=Weak", true);
    reset();

    // 最下段の状態。旗の立ち方が `status` でも数値でも出る。
    py.flags.status = pst::PY_BLIND | pst::PY_CONFUSED | pst::PY_FEAR | pst::PY_POISONED;
    check("最下段の 4 つ", "blind/1,confused/1,afraid/1,poisoned/1");
    reset();
    py.flags.blind = 5; py.flags.confused = 5; py.flags.afraid = 5; py.flags.poisoned = 5; py.flags.paralysis = 2;
    check("数値の側", "blind/1,confused/1,afraid/1,poisoned/1,paralyzed/1");
    reset();
    py.flags.paralysis = 1; check("麻痺は原作と同じく 2 以上から", "");

    // メッセージで知らされる一時的な状態（残りターンは出さない）。
    py.flags.image = 3; py.flags.invulnerability = 3; py.flags.heroism = 3; py.flags.super_heroism = 3; py.flags.blessed = 3;
    py.flags.protect_evil = 3; py.flags.heat_resistance = 3; py.flags.cold_resistance = 3; py.flags.detect_invisible = 3;
    py.flags.timed_infra = 3; py.flags.confuse_monster = true; py.flags.word_of_recall = 3;
    check("一時的な状態の全部",
        "hallucination/1,invuln/0,hero/0,berserk/0,blessed/0,protevil/0,res_fire/0,res_cold/0,senseunseen/0,infravision/0,attk_conf/0,recall/2");
    reset();

    // 常時の効果は鑑定済みの旗だけ。
    wear(PlayerEquipment::Left, TV_RING, trf::TR_RES_FIRE, true);
    check("鑑定済みの指輪の耐火", "res_fire/4");
    wear(PlayerEquipment::Left, TV_RING, trf::TR_RES_FIRE, false);
    check("未鑑定の指輪の耐火は出さない", "");
    wear(PlayerEquipment::Left, TV_RING, trf::TR_RES_FIRE, true);
    wear(PlayerEquipment::Right, TV_RING, trf::TR_RES_FIRE | trf::TR_FREE_ACT, true);
    check("同じ効果の重なりは 1 つ", "res_fire/4,free_act/4");
    reset();
    wear(PlayerEquipment::Light, TV_LIGHT, trf::TR_RES_FIRE, true);
    wear(PlayerEquipment::Auxiliary, TV_SWORD, trf::TR_SLAY_EVIL, true);
    check("光源と予備の武器の枠は数えない（原作の集め方と同じ）", "");
    reset();
    wear(PlayerEquipment::Wield, TV_SWORD, trf::TR_SLAY_EVIL | trf::TR_FROST_BRAND, true);
    check("手に持つ武器の性質", "slay_evil/4,attk_cold/4");
    wear(PlayerEquipment::Head, TV_HELM, trf::TR_SLAY_EVIL, true);
    wear(PlayerEquipment::Wield, TV_NOTHING, 0, true);
    check("武器の性質は兜にあっても出さない", "");
    reset();
    wear(PlayerEquipment::Neck, TV_AMULET, trf::TR_SUST_STAT, true, 3);
    check("能力値の維持は P1 で能力値を決める", "sust_wis/4");
    wear(PlayerEquipment::Neck, TV_AMULET, trf::TR_SUST_STAT, false, 3);
    check("未鑑定の維持は出さない", "");
    reset();
    wear(PlayerEquipment::Left, TV_RING, trf::TR_TELEPORT | trf::TR_CURSED, true);
    wear(PlayerEquipment::Right, TV_RING, trf::TR_AGGRAVATE | trf::TR_CURSED, true);
    check("テレポートと反感の呪い", "aggravate/5,cursed/5");
    reset();
    wear(PlayerEquipment::Body, TV_SOFT_ARMOR, trf::TR_CURSED, true);
    check("呪いの旗だけ", "cursed/5");
    wear(PlayerEquipment::Body, TV_SOFT_ARMOR, trf::TR_CURSED, false);
    check("未鑑定の呪いは出さない", "");
    reset();
    wear(PlayerEquipment::Outer, TV_CLOAK, trf::TR_STEALTH | trf::TR_FFALL | trf::TR_REGEN | trf::TR_SLOW_DIGEST | trf::TR_SEE_INVIS, true);
    check("外套の枠まで数える", "senseunseen/4,regeneration/4,slow_digest/4,levitate/4,stealth/4");

    // 全部の旗（鑑定済み）を一度に立てて、表の全部の鍵を 1 回は出す（画面側の表との突き合わせのため）。
    wear(PlayerEquipment::Wield, TV_SWORD, ~0u, true);
    for (int stat = 1; stat <= 6; ++stat) wear(PlayerEquipment::Head + stat - 1, TV_RING, trf::TR_SUST_STAT, true, stat);
    for (const StatusEffectView &e : collect_status_effects()) sent_keys.insert(e.key); // 中身は上で見た。ここは鍵を集めるだけ
    reset();

    // 日本語。
    set_language("ja");
    reset();
    py.flags.status = pst::PY_WEAK; check("衰弱（日本語）", "hungry/1=衰弱", true);
    py.flags.status = pst::PY_HUNGRY; check("空腹（日本語）", "hungry/1=空腹", true);
    reset();
    set_language("en");

    /*
     * 送る鍵が全部、画面側の表（`hd2d/ui/status_icon_table.inc`）にあること。環境変数 HENGBAND_STATUS_ICON_TABLE に
     * その絶対パスがあるときだけ見る（無ければ表との突き合わせは 0 件。関門はパスを渡す）。知らない鍵は疑問符のアイコンになる。
     */
    int keys_checked = 0;
    if (const char *table_path = std::getenv("HENGBAND_STATUS_ICON_TABLE"); table_path != nullptr && *table_path != '\0') {
        std::string table;
        if (std::FILE *fp = std::fopen(table_path, "rb")) {
            char buf[4096];
            std::size_t got = 0;
            while ((got = std::fread(buf, 1, sizeof(buf), fp)) > 0) table.append(buf, got);
            std::fclose(fp);
        }
        if (table.empty()) {
            std::fprintf(stderr, "[umoria] selftest: status effects: FAIL cannot read icon table %s\n", table_path);
            ++failures;
        }
        for (const std::string &key : sent_keys) {
            ++keys_checked;
            if (table.find("\"" + key + "\"") == std::string::npos) {
                std::fprintf(stderr, "[umoria] selftest: status effects: FAIL key %s is not in the icon table\n", key.c_str());
                ++failures;
            }
        }
    }
    if (failures == 0) std::fprintf(stderr, "selftest: status effects: ok (%d keys checked against the icon table)\n", keys_checked);
    return failures == 0 ? 0 : 1;
}

} // namespace umoria

/* ==================================================================== 魔法・投射物の効果（projections / missiles）
 *
 * 原作へ足した観測の呼び出し
 * （`tools/targeting/native_fx_overlay.py` が組み立てのたびに patched-src の写しへ足す。レシピには触れない。
 * 記録するだけでゲームのルールは変えない）がここを呼ぶ。積む先は共通の `presentation/bridge/named_attack_fx.h`。
 *
 * - 方向のある魔法（杖・ワンド・呪文）は、原作が止まったマスを渡してくる。経路は**原作の `playerMovePosition()` で
 *   プレイヤから 1 マスずつ進めて**再現する（原作の歩き方をここへ写さない）。壁は経路に入れない。最大射程は原作の定数。
 * - 状態を変える杖（眠り・混乱・減速ほか）は**効いたとき（原作が返す旗が真のとき）だけ**絵を出す。効かなかったときに
 *   出すと未識別の杖の種類が分かってしまう。ボルトとボールは原作もいつも描くので常に出す。
 * - 盲目のときは出さない（原作も `blind < 1` の間しか描かない）。矢弾・投擲は、原作が描く条件（照らされたマス）のマスだけを歩として足す。
 * - 属性の名前は変愚の名前（`presentation/bridge/attack_fx_attr_names.h`）。HolyOrb は `holy_fire`。表は下の 3 つ。
 *   原作の全数との突き合わせは `tools/umoria/um_attack_fx_check.py --selftest`。
 */
namespace {

//! 原作の `MagicSpellFlags` → 属性の名前。
const struct {
    int type;
    const char *name;
} kFxSpellTypeNames[] = {
    { MagicSpellFlags::MagicMissile, "missile" },
    { MagicSpellFlags::Lightning, "elec" },
    { MagicSpellFlags::PoisonGas, "pois" },
    { MagicSpellFlags::Acid, "acid" },
    { MagicSpellFlags::Frost, "cold" },
    { MagicSpellFlags::Fire, "fire" },
    { MagicSpellFlags::HolyOrb, "holy_fire" },
};

//! 方向のある魔法の関数 → 属性の名前（`param` が 0 以上のときと負のとき。同じなら 2 つとも同じ名前）。
//! `spellFireBolt`・`spellFireBall` は `MagicSpellFlags` で決まるのでここには無い。
const struct {
    const char *fn;
    const char *attr;
    const char *attr_negative;
} kFxLineNames[] = {
    { "spellChangeMonsterHitPoints", "missile", "old_heal" },
    { "spellDrainLifeFromMonster", "hypodynamia", "hypodynamia" },
    { "spellSpeedMonster", "old_speed", "old_slow" },
    { "spellConfuseMonster", "old_conf", "old_conf" },
    { "spellSleepMonster", "old_sleep", "old_sleep" },
    { "spellWallToMud", "kill_wall", "kill_wall" },
    { "spellDestroyDoorsTrapsInDirection", "kill_door", "kill_door" },
    { "spellPolymorphMonster", "old_poly", "old_poly" },
    { "spellBuildWall", "make_wall", "make_wall" },
    { "spellCloneMonster", "old_clone", "old_clone" },
    { "spellTeleportAwayMonsterInDirection", "away_all", "away_all" },
    { "spellDisarmAllInDirection", "kill_trap", "kill_trap" },
    { "spellLightLine", "lite_weak", "lite_weak" },
};

//! 方向を持たない魔法の関数 → 属性の名前と形（'T' 足元だけ・'A' 視界内の全員）。`spellDispelCreature` は防御の旗で名前を決める。
const struct {
    const char *fn;
    const char *attr;
    const char *attr_negative;
    char scope;
} kFxAroundNames[] = {
    { "spellLightArea", "lite_weak", "lite_weak", 'T' },
    { "spellDarkenArea", "dark_weak", "dark_weak", 'T' },
    { "monsterSleep", "old_sleep", "old_sleep", 'T' },
    { "spellSleepAllMonsters", "old_sleep", "old_sleep", 'A' },
    { "spellSpeedAllMonsters", "old_speed", "old_slow", 'A' },
    { "spellMassPolymorph", "old_poly", "old_poly", 'A' },
    { "spellDispelCreature", "disp_all", "disp_all", 'A' },
    { "spellTurnUndead", "turn_undead", "turn_undead", 'A' },
};

const char *fx_spell_type_name(int spell_type)
{
    for (const auto &row : kFxSpellTypeNames) {
        if (row.type == spell_type) return row.name;
    }
    return "";
}

bool fx_player_blind() { return py.flags.blind > 0 || (py.flags.status & config::player::status::PY_BLIND) != 0u; }

//! プレイヤから `direction` へ 1 マスずつ、`end` まで（または最大射程まで）。壁は入れない。
std::vector<FxCell> fx_line_path(int direction, int end_y, int end_x)
{
    std::vector<FxCell> path;
    Coord_t at = py.pos;
    for (int i = 0; i < config::treasure::OBJECT_BOLTS_MAX_RANGE; ++i) {
        if (!playerMovePosition(direction, at)) break;
        if (dg.floor[at.y][at.x].feature_id >= MIN_CLOSED_SPACE) break;
        FxCell cell;
        cell.y = static_cast<std::int16_t>(at.y);
        cell.x = static_cast<std::int16_t>(at.x);
        path.push_back(cell);
        if (at.y == end_y && at.x == end_x) break;
    }
    return path;
}

ProjectionFx fx_projection(AttackFxOrigin origin, ProjectionShape shape, const char *attr, int rad, int src_y, int src_x, int impact_y, int impact_x)
{
    ProjectionFx fx;
    fx.origin = origin;
    fx.shape = shape;
    fx.attr = attr;
    fx.rad = static_cast<std::uint8_t>(rad);
    fx.src_y = static_cast<std::int16_t>(src_y);
    fx.src_x = static_cast<std::int16_t>(src_x);
    fx.impact_y = static_cast<std::int16_t>(impact_y);
    fx.impact_x = static_cast<std::int16_t>(impact_x);
    if (fx_player_blind()) fx.flags |= kProjPlayerBlind;
    return fx;
}

//! 中心から半径 2 以内で、中心から見通せる開いたマス（原作の爆発・ブレスが描くマスと同じ条件）。
std::vector<FxCell> fx_blast_area(int centre_y, int centre_x)
{
    std::vector<FxCell> area;
    const Coord_t centre{ centre_y, centre_x };
    for (int row = centre_y - 2; row <= centre_y + 2; ++row) {
        for (int col = centre_x - 2; col <= centre_x + 2; ++col) {
            const Coord_t spot{ row, col };
            if (!coordInBounds(spot) || coordDistanceBetween(centre, spot) > 2 || !los(centre, spot)) continue;
            if (dg.floor[row][col].feature_id > MAX_OPEN_SPACE) continue;
            FxCell cell;
            cell.y = static_cast<std::int16_t>(row);
            cell.x = static_cast<std::int16_t>(col);
            cell.dist = static_cast<std::uint8_t>(coordDistanceBetween(centre, spot));
            area.push_back(cell);
        }
    }
    return area;
}

//! 投擲・射撃の途中で開いている 1 発があるか（盲目のときは開かない）。
bool g_fx_throw_active = false;

} // namespace

namespace umoria {

void fill_attack_fx(GameFrame &frame, bool settled)
{
    attack_fx::NamedRecorder::instance().take(frame.projections, frame.missiles, settled);
}

} // namespace umoria

extern "C" void hb_um_fx_line(const char *fn, int direction, int end_y, int end_x, int effective, int param)
{
    if (fn == nullptr || !game.character_generated) return;
    auto &recorder = attack_fx::NamedRecorder::instance();
    const bool is_bolt = std::strcmp(fn, "spellFireBolt") == 0;
    const bool is_ball = std::strcmp(fn, "spellFireBall") == 0;
    const char *attr = "";
    if (is_bolt || is_ball) {
        attr = fx_spell_type_name(param);
    } else {
        for (const auto &row : kFxLineNames) {
            if (std::strcmp(row.fn, fn) == 0) attr = param >= 0 ? row.attr : row.attr_negative;
        }
        if (!effective) return; // 効かなかった杖は絵を出さない（出すと未識別の種類が分かる）
    }
    if (attr[0] == '\0') return;
    const std::vector<FxCell> path = fx_line_path(direction, end_y, end_x);
    const int src_y = py.pos.y;
    const int src_x = py.pos.x;
    if (is_ball && effective) {
        // 爆発：中心は原作が決めた点（壁に当たったときは壁の手前）。
        ProjectionFx fx = fx_projection(AttackFxOrigin::Player, ProjectionShape::Ball, attr, 2, src_y, src_x, end_y, end_x);
        fx.path = path;
        fx.area = fx_blast_area(end_y, end_x);
        recorder.add_projection(std::move(fx));
        return;
    }
    if (path.empty()) return; // 隣が壁：飛んだ先が無い
    const bool beam = std::strcmp(fn, "spellLightLine") == 0;
    ProjectionFx fx = fx_projection(AttackFxOrigin::Player, beam ? ProjectionShape::Beam : ProjectionShape::Bolt, attr, 0, src_y, src_x,
        path.back().y, path.back().x);
    fx.path = path;
    recorder.add_projection(std::move(fx));
}

extern "C" void hb_um_fx_around(const char *fn, int effective, int param)
{
    if (fn == nullptr || effective == 0 || !game.character_generated) return;
    for (const auto &row : kFxAroundNames) {
        if (std::strcmp(row.fn, fn) != 0) continue;
        const char *attr = param >= 0 ? row.attr : row.attr_negative;
        if (std::strcmp(fn, "spellDispelCreature") == 0) {
            if ((param & config::monsters::defense::CD_UNDEAD) != 0) attr = "disp_undead";
            else if ((param & config::monsters::defense::CD_EVIL) != 0) attr = "disp_evil";
        }
        const bool all_in_sight = row.scope == 'A';
        ProjectionFx fx = fx_projection(AttackFxOrigin::Player, all_in_sight ? ProjectionShape::Jump : ProjectionShape::Touch, attr, 0,
            py.pos.y, py.pos.x, py.pos.y, py.pos.x);
        if (all_in_sight) fx.flags |= kProjHidden | kProjAllInSight;
        attack_fx::NamedRecorder::instance().add_projection(std::move(fx));
        return;
    }
}

extern "C" void hb_um_fx_breath(int monster_id, int spell_type, int y, int x)
{
    if (monster_id <= 0 || !game.character_generated) return;
    const char *attr = fx_spell_type_name(spell_type);
    if (attr[0] == '\0') return;
    const Coord_t from = monsters[monster_id].pos;
    ProjectionFx fx = fx_projection(AttackFxOrigin::Monster, ProjectionShape::Breath, attr, 2, from.y, from.x, y, x);
    fx.area = fx_blast_area(y, x);
    attack_fx::NamedRecorder::instance().add_projection(std::move(fx));
}

extern "C" void hb_um_fx_throw_begin(const void *item_ptr)
{
    auto &recorder = attack_fx::NamedRecorder::instance();
    g_fx_throw_active = false;
    if (item_ptr == nullptr || fx_player_blind() || !game.character_generated) {
        recorder.end_missile();
        return;
    }
    const Inventory_t &item = *static_cast<const Inventory_t *>(item_ptr);
    // 合う発射具を構えていれば射撃（矢・クロスボウの矢・スリングの弾）、それ以外は投擲。
    MissileKind kind = MissileKind::Thrown;
    const Inventory_t &launcher = py.inventory[PlayerEquipment::Wield];
    if (launcher.category_id == TV_BOW) {
        switch (launcher.misc_use) {
        case 1: if (item.category_id == TV_SLING_AMMO) kind = MissileKind::Shot; break;
        case 2: case 3: case 4: if (item.category_id == TV_ARROW) kind = MissileKind::Arrow; break;
        case 5: case 6: if (item.category_id == TV_BOLT) kind = MissileKind::Bolt; break;
        default: break;
        }
    }
    // 属性は**識別済みの**ものだけ（原作の名前の表示と同じ条件）。
    std::string brand;
    if (spellItemIdentified(item)) {
        if ((item.flags & config::treasure::flags::TR_FLAME_TONGUE) != 0u) brand = "fire";
        else if ((item.flags & config::treasure::flags::TR_FROST_BRAND) != 0u) brand = "cold";
    }
    recorder.begin_missile(kind, AttackFxOrigin::Player, item_baseitem(item), brand, py.pos.y, py.pos.x);
    g_fx_throw_active = true;
}

extern "C" void hb_um_fx_throw_step(int y, int x)
{
    if (!g_fx_throw_active || fx_player_blind()) return;
    const Tile_t &tile = dg.floor[y][x];
    if (!tile.temporary_light && !tile.permanent_light) return; // 原作も照らされたマスだけ描く
    attack_fx::NamedRecorder::instance().missile_step(y, x);
}

extern "C" void hb_um_fx_throw_hit(int y, int x, int visible)
{
    if (!g_fx_throw_active) return;
    g_fx_throw_active = false;
    auto &recorder = attack_fx::NamedRecorder::instance();
    if (visible == 0 || fx_player_blind()) {
        recorder.end_missile(); // 見えない相手に当たった：当たりは見せない
        return;
    }
    recorder.missile_step(y, x);
    recorder.missile_hit();
}

namespace umoria {

/*
 * `UmoriaCore.exe --selftest`（攻撃の効果の分）：原作の観測の呼び出しが来たときの記録を、実機で作りにくい組み合わせも含めて
 * 数える。ゲームの初期化は要らない（`dg`・`py` を直に書き換えるだけ。このプロセスはこの検査で終わる）。
 * 見ること：名前の表／経路は壁の手前まで／効かなかった杖は記録しない／盲目は記録しない／ボールの範囲／ブレス／
 * 矢弾の種類（合う発射具）と属性（識別済みだけ）／照らされていないマスは歩に入れない／見えない相手への命中は見せない。
 */
int attack_fx_selftest()
{
    int failures = 0;
    int checks = 0;
    const auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            std::fprintf(stderr, "[umoria] selftest: attack fx: FAIL %s\n", what);
            ++failures;
        }
    };
    auto &recorder = attack_fx::NamedRecorder::instance();
    const auto drain = [&](std::vector<ProjectionFx> &p, std::vector<MissileFx> &m) {
        p.clear();
        m.clear();
        recorder.take(p, m, true);
    };

    game.character_generated = true;
    py.flags.blind = 0;
    py.flags.status = 0;
    py.pos = Coord_t{ 10, 10 };
    dg.height = MAX_HEIGHT;
    dg.width = MAX_WIDTH;
    for (int y = 0; y < 21; ++y) {
        for (int x = 0; x < 30; ++x) {
            Tile_t &tile = dg.floor[y][x];
            tile.creature_id = 0;
            tile.treasure_id = 0;
            tile.feature_id = TILE_DARK_FLOOR;
            tile.permanent_light = false;
            tile.temporary_light = false;
        }
    }
    dg.floor[10][14].feature_id = TILE_GRANITE_WALL; // 東 4 マス目は壁
    std::vector<ProjectionFx> projections;
    std::vector<MissileFx> missiles;
    drain(projections, missiles);

    // 1. 属性の名前の表：全部に名前があり、効くものは変愚の名前。
    for (const auto &row : kFxSpellTypeNames) check(row.name[0] != '\0', "spell type name");
    check(std::strcmp(fx_spell_type_name(MagicSpellFlags::HolyOrb), "holy_fire") == 0, "HolyOrb -> holy_fire");
    check(std::strcmp(fx_spell_type_name(MagicSpellFlags::Frost), "cold") == 0, "Frost -> cold");

    // 2. ボルト：壁の手前まで（東へ 3 マス）。始点はプレイヤ。
    hb_um_fx_line("spellFireBolt", 6, 10, 14, 1, MagicSpellFlags::Fire);
    drain(projections, missiles);
    check(projections.size() == 1 && projections[0].shape == ProjectionShape::Bolt && projections[0].attr == "fire", "fire bolt recorded");
    check(projections.size() == 1 && projections[0].path.size() == 3 && projections[0].path.back().x == 13, "bolt path stops before the wall");
    check(projections.size() == 1 && projections[0].src_x == 10 && projections[0].impact_x == 13 && projections[0].origin == AttackFxOrigin::Player,
        "bolt source and impact");

    // 3. 効かなかった杖は記録しない。効いたときは記録する。
    hb_um_fx_line("spellSleepMonster", 6, 10, 13, 0, 0);
    drain(projections, missiles);
    check(projections.empty(), "a wand that did not take effect draws nothing");
    hb_um_fx_line("spellSleepMonster", 6, 10, 13, 1, 0);
    drain(projections, missiles);
    check(projections.size() == 1 && projections[0].attr == "old_sleep", "an effective sleep wand draws old_sleep");
    hb_um_fx_line("spellSpeedMonster", 4, 10, 8, 1, -1);
    hb_um_fx_line("spellSpeedMonster", 4, 10, 8, 1, 1);
    drain(projections, missiles);
    check(projections.size() == 2 && projections[0].attr == "old_slow" && projections[1].attr == "old_speed", "speed sign picks old_slow / old_speed");

    // 4. ボール：中心は渡された点、範囲は中心から半径 2・開いたマスだけ、中心の距離は 0。
    hb_um_fx_line("spellFireBall", 6, 10, 12, 1, MagicSpellFlags::PoisonGas);
    drain(projections, missiles);
    check(projections.size() == 1 && projections[0].shape == ProjectionShape::Ball && projections[0].attr == "pois" && projections[0].rad == 2, "ball recorded");
    bool centre_zero = false, within = true, wall_in_area = false;
    if (!projections.empty()) {
        for (const FxCell &cell : projections[0].area) {
            if (cell.y == 10 && cell.x == 12 && cell.dist == 0) centre_zero = true;
            if (cell.dist > 2) within = false;
            if (cell.y == 10 && cell.x == 14) wall_in_area = true;
        }
    }
    check(centre_zero && within && !wall_in_area, "ball area: centre at distance 0, radius 2, no wall cell");
    hb_um_fx_line("spellFireBall", 6, 10, 12, 0, MagicSpellFlags::Fire); // 射程切れで爆発しなかった玉は飛ぶ絵だけ
    drain(projections, missiles);
    check(projections.size() == 1 && projections[0].shape == ProjectionShape::Bolt, "a ball that fizzled out is a bolt");

    // 5. 足元・視界内の全員。
    hb_um_fx_around("spellSleepAllMonsters", 1, 0);
    hb_um_fx_around("spellLightArea", 1, 0);
    hb_um_fx_around("spellTurnUndead", 0, 0); // 効かなかった
    hb_um_fx_around("spellDispelCreature", 1, config::monsters::defense::CD_EVIL);
    drain(projections, missiles);
    check(projections.size() == 3, "around: three took effect, one did not");
    check(projections.size() == 3 && (projections[0].flags & (kProjHidden | kProjAllInSight)) == (kProjHidden | kProjAllInSight) && projections[0].attr == "old_sleep",
        "sleep monsters affects everything in sight");
    check(projections.size() == 3 && projections[1].shape == ProjectionShape::Touch && projections[1].attr == "lite_weak", "light area touches the player's cell");
    check(projections.size() == 3 && projections[2].attr == "disp_evil", "dispel evil -> disp_evil");

    // 6. 盲目のときは画面側が捨てる旗（kProjPlayerBlind）を立てる。矢弾は記録しない。
    py.flags.blind = 3;
    hb_um_fx_line("spellFireBolt", 6, 10, 13, 1, MagicSpellFlags::Fire);
    drain(projections, missiles);
    check(projections.size() == 1 && (projections[0].flags & kProjPlayerBlind) != 0u, "blind projection carries kProjPlayerBlind");
    Inventory_t arrow{};
    arrow.category_id = TV_ARROW;
    arrow.sub_category_id = 1;
    hb_um_fx_throw_begin(&arrow);
    hb_um_fx_throw_step(10, 11);
    drain(projections, missiles);
    check(missiles.empty(), "a blind throw is not recorded");
    py.flags.blind = 0;

    // 7. 矢弾：合う発射具で種類が決まり、属性は識別済みだけ。照らされたマスだけが歩になる。
    for (int x = 11; x <= 13; ++x) dg.floor[10][x].permanent_light = true;
    dg.floor[10][12].permanent_light = false; // 途中の 1 マスは暗い
    // 原作の流れ：相手のいないマスは歩（照らされたマスだけ）、相手のいるマスは命中。
    const auto fire_one = [&](Inventory_t &ammo, std::uint8_t launcher_category, int launcher_misc, bool lit_hit, int visible) {
        Inventory_t &wield = py.inventory[PlayerEquipment::Wield];
        wield = Inventory_t{};
        wield.category_id = launcher_category;
        wield.misc_use = static_cast<std::int16_t>(launcher_misc);
        hb_um_fx_throw_begin(&ammo);
        hb_um_fx_throw_step(10, 11);
        hb_um_fx_throw_step(10, 12);
        if (lit_hit) hb_um_fx_throw_hit(10, 13, visible);
        else hb_um_fx_throw_step(10, 13);
        drain(projections, missiles);
    };
    arrow.flags = config::treasure::flags::TR_FLAME_TONGUE;
    arrow.identification = config::identification::ID_KNOWN2;
    fire_one(arrow, TV_BOW, 3, true, 1);
    check(missiles.size() == 1 && missiles[0].kind == MissileKind::Arrow, "an arrow with a long bow is an Arrow");
    check(missiles.size() == 1 && missiles[0].brand == "fire", "an identified flaming arrow carries the fire brand");
    check(missiles.size() == 1 && missiles[0].path.size() == 2 && missiles[0].path[0].x == 11 && missiles[0].path[1].x == 13,
        "only lit cells are steps (the dark middle cell is skipped)");
    check(missiles.size() == 1 && missiles[0].hit, "a hit on a visible monster sets hit");
    arrow.identification = 0;
    fire_one(arrow, TV_BOW, 3, true, 1);
    check(missiles.size() == 1 && missiles[0].brand.empty(), "an unidentified flaming arrow carries no brand");
    arrow.flags = config::treasure::flags::TR_FROST_BRAND;
    arrow.identification = config::identification::ID_KNOWN2;
    fire_one(arrow, TV_BOW, 3, true, 1);
    check(missiles.size() == 1 && missiles[0].brand == "cold", "a frost brand is cold");
    arrow.flags = 0;
    fire_one(arrow, TV_BOW, 3, true, 0);
    check(missiles.size() == 1 && !missiles[0].hit && missiles[0].path.size() == 1, "a hit on a monster the player cannot see is not shown");
    fire_one(arrow, TV_BOW, 1, false, 1);
    check(missiles.size() == 1 && missiles[0].kind == MissileKind::Thrown, "an arrow with a sling is only thrown");
    Inventory_t bolt{};
    bolt.category_id = TV_BOLT;
    fire_one(bolt, TV_BOW, 5, false, 1);
    check(missiles.size() == 1 && missiles[0].kind == MissileKind::Bolt, "a bolt with a crossbow is a Bolt");
    Inventory_t shot{};
    shot.category_id = TV_SLING_AMMO;
    fire_one(shot, TV_BOW, 1, false, 1);
    check(missiles.size() == 1 && missiles[0].kind == MissileKind::Shot, "a sling shot with a sling is a Shot");
    Inventory_t dagger{};
    dagger.category_id = TV_SWORD;
    fire_one(dagger, TV_NOTHING, 0, false, 1);
    check(missiles.size() == 1 && missiles[0].kind == MissileKind::Thrown && missiles[0].item_id != 0, "a dagger is Thrown and has a tile number");

    // 8. モンスターのブレス：撃った者はモンスター・範囲はプレイヤの周り。
    monsters[1].pos = Coord_t{ 10, 16 };
    hb_um_fx_breath(1, MagicSpellFlags::Fire, 10, 10);
    drain(projections, missiles);
    check(projections.size() == 1 && projections[0].shape == ProjectionShape::Breath && projections[0].origin == AttackFxOrigin::Monster && projections[0].attr == "fire",
        "monster breath recorded as a Breath from the monster");
    check(projections.size() == 1 && projections[0].src_x == 16 && projections[0].impact_x == 10 && !projections[0].area.empty(), "breath source and area");

    if (failures == 0) std::fprintf(stderr, "selftest: attack fx: ok (%d checks)\n", checks);
    return failures == 0 ? 0 : 1;
}

} // namespace umoria
