/*
 * NetHack の地形の表示記号（cmap）→ 地形の性質の表と、画面へ渡す地形番号の引き方。
 *
 * 原作のヘッダは読まない。`glyph_to_cmap()` の値で引く。
 * 表は `include/defsym.h` の PCHAR の番号の順（英語版 nethack と日本語版 nethackjp で同じ並び）。
 * 各行の頭の「番号と記号の名前」の註釈（例: 12 S_ndoor）を `tools/nethack/check_terrain_table.py` が defsym.h と突き合わせる。
 * 並びを取り違えると全部ずれるので、この註釈の書式は変えないこと。
 */

#include "nh_terrain.h"

#include "frame/cell_feature_bits.h"

#include <cstdint>

namespace nethack {

namespace {

//! NetHack の地形番号の表（`tools/nethack/gen_nethack_terrain_rules.py` が作る。組み立てが `--check` で照合する）。
#include "nh_terrain_ids.inc"

/*
 * 変愚の地形番号（lib/edit/TerrainDefinitions.jsonc の id。末尾の註釈は同じファイルの key）。
 * 2026-10-28 から画面へは送らない（terrain_for_cmap が NetHack の帯の番号に替える）。表の `terrain` 欄の名残。
 * Umoria のアダプタ（umoria/adapter/um_frame.cpp の HengbandTerrain）と同じ出どころで、
 * Angband・Frox のアダプタは同じ番号を tilework/tile_mapping.csv の F 行の id 列から名前で引いている。
 * key との一致は tools/nethack/check_terrain_table.py が確かめる。
 */
enum HengbandTerrain : std::uint16_t {
    kFloor = 1,           // FLOOR
    kRuneProtection = 3,  // RUNE_PROTECTION
    kOpenDoor = 4,        // OPEN_DOOR
    kBrokenDoor = 5,      // BROKEN_DOOR
    kUpStair = 6,         // UP_STAIR
    kDownStair = 7,       // DOWN_STAIR
    kTrapTrapdoor = 16,   // TRAP_TRAPDOOR
    kTrapPit = 17,        // TRAP_PIT
    kTrapSpikedPit = 18,  // TRAP_SPIKED_PIT
    kTrapEvilRune = 20,   // TRAP_TY_CURSE
    kTrapTeleport = 21,   // TRAP_TELEPORT
    kTrapFire = 22,       // TRAP_FIRE
    kTrapAcid = 23,       // TRAP_ACID
    kTrapSlowDart = 24,   // TRAP_SLOW
    kTrapStrDart = 25,    // TRAP_LOSE_STR
    kTrapSleepGas = 31,   // TRAP_SLEEP
    kClosedDoor = 32,     // CLOSED_DOOR
    kRubble = 49,         // RUBBLE
    kGranite = 56,        // GRANITE
    kRuneExplosion = 64,  // RUNE_EXPLOSION
    kPatternStart = 65,   // PATTERN_START
    kDeepWater = 83,      // DEEP_WATER
    kShallowWater = 84,   // SHALLOW_WATER
    kDeepLava = 85,       // DEEP_LAVA
    kDarkPit = 87,        // DARK_PIT
    kTrapTraps = 90,      // TRAP_TRAPS
    kTrapAlarm = 91,      // TRAP_ALARM
    kTree = 96,           // TREE
    kMirror = 195,        // MIRROR
    kGlassFloor = 199,    // GLASS_FLOOR
    kGlassWall = 218,     // GLASS_WALL
};

//! 地形の性質の組。`CELL_FEAT_KNOWN` は呼び出し側が足す。
constexpr std::uint16_t kFlagsWall = CELL_FEAT_WALL;
constexpr std::uint16_t kFlagsDoorOpen = CELL_FEAT_DOOR | CELL_FEAT_DOOR_OPEN | CELL_FEAT_PASSABLE;
constexpr std::uint16_t kFlagsDoorClosed = CELL_FEAT_DOOR;
constexpr std::uint16_t kFlagsStairs = CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE;
constexpr std::uint16_t kFlagsFloor = CELL_FEAT_PASSABLE;                      // 通路・罠・橋など
constexpr std::uint16_t kFlagsRoom = CELL_FEAT_PASSABLE | CELL_FEAT_ROOM;       // 部屋の床と部屋の中の設備
constexpr std::uint16_t kFlagsWater = CELL_FEAT_WATER;                          // Umoria の深い水と同じ（歩ける印は付けない）
constexpr std::uint16_t kFlagsLava = CELL_FEAT_LAVA | CELL_FEAT_GLOW;           // 変愚の DEEP_LAVA が GLOW を持つ

//! 描く記号。
constexpr TerrainLook look(std::uint16_t terrain, std::uint16_t flags, MinimapKind minimap, bool trap = false)
{
    return TerrainLook{ terrain, flags, minimap, true, trap };
}

//! 描かない記号（未探索の石・ビームや爆発などの効果）。
constexpr TerrainLook hidden()
{
    return TerrainLook{};
}

/*
 * 表の見かた。
 * - 壁（縦横・角・T 字・十字）はすべて花こう岩の壁にする。NetHack は壁の種類を画面へ出さない。
 * - 変愚に無い設備は、見た目が近い物で代える。効果は原作どおりコアが処理するので、変わるのは絵だけ。
 *   代えた理由は各行の末尾に書いた（M1 で専用の絵に替えるときに、この行だけ直せばよい）。
 * - 罠は変愚の罠の地形（TRAP_*）の中で近い物にし、`trap` を真にする。近い物が無い罠は TRAP_TRAPS
 *   （コンパクトルーン。罠の一般形）にする。
 */
constexpr TerrainLook kTable[kCmapCount] = {
    /*   0 S_stone */ hidden(), // 岩と何も無い所。未探索のマスは描かない（原作でも空白）
    /*   1 S_vwall */ look(kGranite, kFlagsWall, MinimapKind::Wall),
    /*   2 S_hwall */ look(kGranite, kFlagsWall, MinimapKind::Wall),
    /*   3 S_tlcorn */ look(kGranite, kFlagsWall, MinimapKind::Wall),
    /*   4 S_trcorn */ look(kGranite, kFlagsWall, MinimapKind::Wall),
    /*   5 S_blcorn */ look(kGranite, kFlagsWall, MinimapKind::Wall),
    /*   6 S_brcorn */ look(kGranite, kFlagsWall, MinimapKind::Wall),
    /*   7 S_crwall */ look(kGranite, kFlagsWall, MinimapKind::Wall),
    /*   8 S_tuwall */ look(kGranite, kFlagsWall, MinimapKind::Wall),
    /*   9 S_tdwall */ look(kGranite, kFlagsWall, MinimapKind::Wall),
    /*  10 S_tlwall */ look(kGranite, kFlagsWall, MinimapKind::Wall),
    /*  11 S_trwall */ look(kGranite, kFlagsWall, MinimapKind::Wall),
    /*  12 S_ndoor */ look(kBrokenDoor, kFlagsDoorOpen, MinimapKind::Door), // 戸の無い出入口。戸板の無い「壊れたドア」
    /*  13 S_vodoor */ look(kOpenDoor, kFlagsDoorOpen, MinimapKind::Door),
    /*  14 S_hodoor */ look(kOpenDoor, kFlagsDoorOpen, MinimapKind::Door),
    /*  15 S_vcdoor */ look(kClosedDoor, kFlagsDoorClosed, MinimapKind::Door),
    /*  16 S_hcdoor */ look(kClosedDoor, kFlagsDoorClosed, MinimapKind::Door),
    /*  17 S_bars */ look(kGlassWall, kFlagsWall, MinimapKind::Wall), // 鉄格子。見通せるが通れない→ガラスの壁
    /*  18 S_tree */ look(kTree, CELL_FEAT_TREE, MinimapKind::Tree),
    /*  19 S_room */ look(kFloor, kFlagsRoom, MinimapKind::Floor),
    /*  20 S_darkroom */ look(kFloor, kFlagsRoom, MinimapKind::Floor), // 見えていない部屋の床（覚えている分）。明暗は画面側
    /*  21 S_engroom */ look(kFloor, kFlagsRoom, MinimapKind::Floor), // 部屋の床の刻み文字。変愚に無いので床
    /*  22 S_corr */ look(kFloor, kFlagsFloor, MinimapKind::Floor), // 通路。CELL_FEAT_ROOM を立てないことで部屋と見分ける
    /*  23 S_litcorr */ look(kFloor, kFlagsFloor, MinimapKind::Floor),
    /*  24 S_engrcorr */ look(kFloor, kFlagsFloor, MinimapKind::Floor),
    /*  25 S_upstair */ look(kUpStair, kFlagsStairs, MinimapKind::Stairs),
    /*  26 S_dnstair */ look(kDownStair, kFlagsStairs, MinimapKind::Stairs),
    /*  27 S_upladder */ look(kUpStair, kFlagsStairs, MinimapKind::Stairs), // 梯子。坑道（2 階層）は意味が違うので階段
    /*  28 S_dnladder */ look(kDownStair, kFlagsStairs, MinimapKind::Stairs),
    /*  29 S_brupstair */ look(kUpStair, kFlagsStairs, MinimapKind::Stairs), // 分岐の階段。クエスト入口は町の絵になるので階段
    /*  30 S_brdnstair */ look(kDownStair, kFlagsStairs, MinimapKind::Stairs),
    /*  31 S_brupladder */ look(kUpStair, kFlagsStairs, MinimapKind::Stairs),
    /*  32 S_brdnladder */ look(kDownStair, kFlagsStairs, MinimapKind::Stairs),
    /*  33 S_altar */ look(kRuneProtection, kFlagsRoom, MinimapKind::Floor), // 祭壇。変愚に無い。神聖な印の「守りのルーン」
    /*  34 S_grave */ look(kRubble, CELL_FEAT_RUBBLE | kFlagsRoom, MinimapKind::Floor), // 墓。変愚に無い。石の山（歩けるので Floor）
    /*  35 S_throne */ look(kMirror, kFlagsRoom, MinimapKind::Floor), // 玉座。変愚に無い。床に立つ大きな調度の「鏡」
    /*  36 S_sink */ look(kDarkPit, kFlagsRoom, MinimapKind::Floor), // 流し。変愚に無い。排水口に見立てた「暗い穴」
    /*  37 S_fountain */ look(kShallowWater, CELL_FEAT_WATER | kFlagsRoom, MinimapKind::Water), // 噴水。浅い水
    /*  38 S_pool */ look(kDeepWater, kFlagsWater, MinimapKind::Water), // 池と堀の水
    /*  39 S_ice */ look(kGlassFloor, kFlagsFloor, MinimapKind::Floor), // 氷。変愚に無い。ガラスの床
    /*  40 S_lava */ look(kDeepLava, kFlagsLava, MinimapKind::Lava),
    /*  41 S_lavawall */ look(kDeepLava, kFlagsLava, MinimapKind::Lava), // 溶岩の壁。通れないが、絵は溶岩
    /*  42 S_vodbridge */ look(kFloor, kFlagsFloor, MinimapKind::Floor), // 下りた跳ね橋（橋板）。変愚に橋が無いので床
    /*  43 S_hodbridge */ look(kFloor, kFlagsFloor, MinimapKind::Floor),
    /*  44 S_vcdbridge */ look(kClosedDoor, kFlagsDoorClosed, MinimapKind::Door), // 上がった跳ね橋の門（原作の display.c では壁の側の DBWALL）
    /*  45 S_hcdbridge */ look(kClosedDoor, kFlagsDoorClosed, MinimapKind::Door),
    /*  46 S_air */ look(kFloor, kFlagsFloor, MinimapKind::Floor), // 風の階層の空。歩ける所なので床
    /*  47 S_cloud */ look(kFloor, kFlagsFloor, MinimapKind::Floor), // 雲。視界を遮るが歩ける。変愚に無いので床
    /*  48 S_water */ look(kDeepWater, kFlagsWater, MinimapKind::Water), // 水の階層・水の壁
    /*  49 S_arrow_trap */ look(kTrapSlowDart, kFlagsFloor, MinimapKind::Floor, true), // 矢の罠。Umoria と同じくダーツの罠
    /*  50 S_dart_trap */ look(kTrapStrDart, kFlagsFloor, MinimapKind::Floor, true),
    /*  51 S_falling_rock_trap */ look(kTrapPit, kFlagsFloor, MinimapKind::Floor, true), // 落石。Umoria の rockfall と同じく落とし穴
    /*  52 S_squeaky_board */ look(kTrapAlarm, kFlagsFloor, MinimapKind::Floor, true), // 鳴く床板。音で知らせる警報装置
    /*  53 S_bear_trap */ look(kTrapTraps, kFlagsFloor, MinimapKind::Floor, true), // 熊の罠。近い罠が無いので一般形
    /*  54 S_land_mine */ look(kRuneExplosion, kFlagsFloor, MinimapKind::Floor, true), // 地雷。爆発ルーン
    /*  55 S_rolling_boulder_trap */ look(kTrapTraps, kFlagsFloor, MinimapKind::Floor, true), // 近い罠が無いので一般形
    /*  56 S_sleeping_gas_trap */ look(kTrapSleepGas, kFlagsFloor, MinimapKind::Floor, true),
    /*  57 S_rust_trap */ look(kTrapAcid, kFlagsFloor, MinimapKind::Floor, true), // 錆の罠。水の噴出に近い物が無く、酸の染み
    /*  58 S_fire_trap */ look(kTrapFire, kFlagsFloor, MinimapKind::Floor, true),
    /*  59 S_pit */ look(kTrapPit, kFlagsFloor, MinimapKind::Floor, true),
    /*  60 S_spiked_pit */ look(kTrapSpikedPit, kFlagsFloor, MinimapKind::Floor, true),
    /*  61 S_hole */ look(kTrapTrapdoor, kFlagsFloor, MinimapKind::Floor, true), // 穴。下の階へ落ちるので落とし戸
    /*  62 S_trap_door */ look(kTrapTrapdoor, kFlagsFloor, MinimapKind::Floor, true),
    /*  63 S_teleportation_trap */ look(kTrapTeleport, kFlagsFloor, MinimapKind::Floor, true),
    /*  64 S_level_teleporter */ look(kTrapTeleport, kFlagsFloor, MinimapKind::Floor, true), // 階をまたぐ転送。変愚に無いのでテレポートの罠
    /*  65 S_magic_portal */ look(kTrapTeleport, kFlagsFloor, MinimapKind::Floor, true), // 魔法の入口。同上
    /*  66 S_web */ look(kTrapTraps, kFlagsFloor, MinimapKind::Floor, true), // 蜘蛛の巣。近い罠が無いので一般形
    /*  67 S_statue_trap */ look(kTrapEvilRune, kFlagsFloor, MinimapKind::Floor, true), // 像の罠（怪物が現れる）。邪悪なルーン
    /*  68 S_magic_trap */ look(kTrapEvilRune, kFlagsFloor, MinimapKind::Floor, true), // 魔法の罠。ルーン系はまとめて邪悪なルーン
    /*  69 S_anti_magic_trap */ look(kTrapEvilRune, kFlagsFloor, MinimapKind::Floor, true),
    /*  70 S_polymorph_trap */ look(kTrapEvilRune, kFlagsFloor, MinimapKind::Floor, true),
    /*  71 S_vibrating_square */ look(kPatternStart, kFlagsFloor, MinimapKind::Floor, true), // 振動する床。罠ではないが原作が罠として覚える。特別な床の「パターン出発点」
    /*  72 S_trapped_door */ look(kClosedDoor, kFlagsDoorClosed, MinimapKind::Door, true), // 罠のある扉
    /*  73 S_trapped_chest */ look(kFloor, kFlagsFloor, MinimapKind::Floor, true), // 罠のある箱。箱はアイテムなので足元の床
    // 74〜104 は効果（ビーム・閃光・盾・毒雲・位置の指定・呑み込み・爆発）。地形ではないので描かない。
    /*  74 S_vbeam */ hidden(),
    /*  75 S_hbeam */ hidden(),
    /*  76 S_lslant */ hidden(),
    /*  77 S_rslant */ hidden(),
    /*  78 S_digbeam */ hidden(),
    /*  79 S_flashbeam */ hidden(),
    /*  80 S_boomleft */ hidden(),
    /*  81 S_boomright */ hidden(),
    /*  82 S_ss1 */ hidden(),
    /*  83 S_ss2 */ hidden(),
    /*  84 S_ss3 */ hidden(),
    /*  85 S_ss4 */ hidden(),
    /*  86 S_poisoncloud */ hidden(),
    /*  87 S_goodpos */ hidden(),
    /*  88 S_sw_tl */ hidden(),
    /*  89 S_sw_tc */ hidden(),
    /*  90 S_sw_tr */ hidden(),
    /*  91 S_sw_ml */ hidden(),
    /*  92 S_sw_mr */ hidden(),
    /*  93 S_sw_bl */ hidden(),
    /*  94 S_sw_bc */ hidden(),
    /*  95 S_sw_br */ hidden(),
    /*  96 S_expl_tl */ hidden(),
    /*  97 S_expl_tc */ hidden(),
    /*  98 S_expl_tr */ hidden(),
    /*  99 S_expl_ml */ hidden(),
    /* 100 S_expl_mc */ hidden(),
    /* 101 S_expl_mr */ hidden(),
    /* 102 S_expl_bl */ hidden(),
    /* 103 S_expl_bc */ hidden(),
    /* 104 S_expl_br */ hidden(),
};

} // namespace

TerrainLook terrain_for_cmap(int cmap, int variant)
{
    if (cmap < 0 || cmap >= kCmapCount) return hidden();
    TerrainLook out = kTable[cmap];
    if (!out.known) return out;
    const int v = (variant >= 0 && variant <= 4) ? variant : 0;
    if (cmap >= 1 && cmap <= 11) {
        out.terrain = nh_wall_terrain(v, cmap);   // 壁は迷宮の系統ごと（11 種とも同じ絵。D23）
    } else if (cmap == 33) {
        out.terrain = nh_altar_terrain(v);        // 祭壇は属性ごと
    } else {
        out.terrain = kNhCmapTerrain[cmap];
    }
    return out;
}

} // namespace nethack
