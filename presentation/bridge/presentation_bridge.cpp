/*!
 * @file presentation_bridge.cpp
 * @brief Bridge::capture 実装（map_info・terrain_id・公式タイル索引・Sub2/3/5 / PHASE4）
 */
#include "bridge/presentation_bridge.h"
#include "bootstrap/sdl_game_bootstrap.h"
#include "bridge/official_tile_table.h"
#ifdef TANGBAND
#include "bridge/official_tile_table_tang.h"
#endif
#include "bridge/tile_index_table.h"
#include "audio/sound_event_queue.h"
#include "frame/cell_feature_bits.h"

#include "core/asking-player.h"
#include "core/combat-feedback.h"
#include "core/realtime-clock.h"
#include "core/visuals-reseter.h"
#include "flavor/flavor-describer.h"
#include "game-option/special-options.h"
#include "io/read-pref-file.h"
#include "system/angband-system.h"
#include "system/system-variables.h"
#include "frame/sdl_ui_options.h"
#include "term/sdl_sub_window_terms.h"
#include "game-option/input-options.h"
#include "game-option/runtime-arguments.h"
#include "io/input-key-requester.h"
#include "flavor/object-flavor-types.h"
#include "floor/floor-util.h" //!< ボクセル HD2D P10: `map_name()`（カットイン演出の地名）
#include "inventory/inventory-slot-types.h"
#include "locale/character-encoding.h"
#include "locale/language-switcher.h"
#include "monster/monster-describer.h"
#include "monster/monster-description-types.h"
#include "system/enums/terrain/terrain-characteristics.h"
#include "system/enums/terrain/terrain-tag.h"
#include "system/floor/floor-info.h"
#include "system/inner-game-data.h" //!< ボクセル HD2D §12-4: 時刻の算出（開始種族で基準が変わる）
#include "term/gameterm.h" //!< `angband_color_table`（`&` で変えられる色表）
#include "term/term-color-types.h"
#include "system/grid-type-definition.h"
#include "system/terrain/terrain-definition.h"
#include "system/item/item-entity.h"
#include "system/monrace/monrace-definition.h"
#include "system/monrace/monrace-list.h"
#include "system/monster-entity.h"
#include "system/player-type-definition.h"
#include "util/enum-converter.h"
#include "util/point-2d.h"
#include "view/display-map.h"
#include "view/display-messages.h"
#include "view/display-symbol.h"
#include "world/world.h"

#include "term/z-term.h"

#include <tl/optional.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace presentation {

namespace {

/*!
 * @brief K-47: タイトル画を敷いてよい間だけ真。`set_title_screen` が降ろす。
 * @details **初期値が真**なのは、`init_angband`（データ初期化。Debug では十数秒かかり、
 * 画面には `データの初期化中...` の進捗が出る）が**オープニング選択より前**に走るから
 * （`run_sdl_game` の段 6 → 段 7）。ここを偽から始めると、その間だけ背景が素の黒地になり、
 * 「処理が終わってから絵が出る」ように見える。描画側の素材は `app.init()` で
 * `run_sdl_game` より前に読み終わっているので、待たせる理由が無い。
 */
bool g_title_screen = true;

// z-term.cpp の AF_KANJI / AF_TILE フラグ（scr->a）。
constexpr uint8_t kAfKanji1 = 0x10;
constexpr uint8_t kAfKanji2 = 0x20;
constexpr uint8_t kAfKanjic = 0x0F;
constexpr uint8_t kAfTile1 = 0x80;
constexpr uint8_t kAfBigtile2 = 0xf0;

/*!
 * @name 立ち木・岩の板の足元に敷く地面の地形 id（版 2.7）
 * @details 立ち木・岩を＋字に交差した 2 枚の板で描くと、そのマスの床が抜けて穴になる。
 * 板の下に敷く地面の**タイル索引**は Bridge が `MapCellView::under_tile_index` へ入れて渡す
 * （ui は地形テーブルを知らない。設計書 §4.1・§4.6）。
 * 対応は人間の指示「地面には岩は地面、木は草のタイルを組み合わせて」そのまま。
 * 値は `lib/edit/TerrainDefinitions.jsonc` の id（FLOOR=1 / GRASS=89）。
 * @{
 */
constexpr uint16_t kTerrainIdFloor = 1;
constexpr uint16_t kTerrainIdGrass = 89;
/*! @} */

constexpr int kMessageLines = 8;
constexpr int kSub2Max = 8;
constexpr int kSub3Max = 8;
constexpr int kSub5Max = 12;

//! グラフィックタイル属性のセルは「コア地図の流し込み」なのでメニュー本文から除外する。
bool is_graphic_tile_attr(uint8_t attr)
{
    if ((attr & kAfTile1) != 0) {
        return true;
    }
    return (attr & kAfBigtile2) == kAfBigtile2;
}

void fill_hud(HudSnapshot &hud, const PlayerType *player)
{
    hud.name = player->name;
    hud.hp = player->chp;
    hud.hp_max = player->mhp;
    hud.sp = player->csp;
    hud.sp_max = player->msp;
    hud.gold = static_cast<int>(player->au);
    hud.level = player->lev;
    hud.depth = (player->current_floor_ptr != nullptr)
        ? static_cast<int>(player->current_floor_ptr->dun_level)
        : 0;

    char status[128];
    std::snprintf(status, sizeof(status), "HP %d/%d  SP %d/%d  LEV %d",
        hud.hp, hud.hp_max, hud.sp, hud.sp_max, hud.level);
    hud.status_line = status;
}

void fill_messages(GameFrame &frame)
{
    const int32_t num = message_num();
    const int take = (std::min)(kMessageLines, static_cast<int>(num));
    frame.messages.clear();
    frame.messages.reserve(static_cast<size_t>(take));

    for (int age = take - 1; age >= 0; --age) {
        const auto msg = message_str(age);
        if (!msg || msg->empty()) {
            continue;
        }
        MessageEvent ev{};
        ev.seq = static_cast<uint32_t>(frame.messages.size());
        ev.color = 1;
        if (const auto utf8 = sys_to_utf8(*msg)) {
            ev.text_utf8 = *utf8;
        } else {
            ev.text_utf8 = *msg;
        }
        frame.messages.push_back(std::move(ev));
    }
}

std::string to_utf8_line(const std::string &sys)
{
    if (const auto utf8 = sys_to_utf8(sys)) {
        return *utf8;
    }
    return sys;
}

void fill_sub2_equipment(GameFrame &frame, PlayerType *player)
{
    frame.sub2_lines.clear();
    static const char *kSlotLabel[] = {
        "MH", "SH", "BW", "MR", "SR", "NK", "LT", "BD", "OT", "HD", "AR", "FT"
    };
    int lines = 0;
    for (int i = INVEN_MAIN_HAND; i < INVEN_TOTAL && lines < kSub2Max; ++i) {
        if (i >= static_cast<int>(player->inventory.size())) {
            break;
        }
        const auto &item_ptr = player->inventory[static_cast<size_t>(i)];
        if (!item_ptr || !item_ptr->is_valid()) {
            continue;
        }
        const auto name = describe_flavor(player, *item_ptr, OD_NAME_ONLY | OD_OMIT_PREFIX);
        const int slot = i - INVEN_MAIN_HAND;
        char buf[256];
        const char *lab = (slot >= 0 && slot < 12) ? kSlotLabel[slot] : "?";
        std::snprintf(buf, sizeof(buf), "%s %s", lab, name.c_str());
        frame.sub2_lines.push_back(to_utf8_line(buf));
        ++lines;
    }
    if (frame.sub2_lines.empty()) {
        frame.sub2_lines.emplace_back("(empty)");
    }
}

void fill_sub3_monsters(GameFrame &frame, PlayerType *player, int ox, int oy, int vw, int vh)
{
    frame.sub3_lines.clear();
    const auto *floor = player->current_floor_ptr;
    if (floor == nullptr) {
        frame.sub3_lines.emplace_back("(none)");
        return;
    }

    int lines = 0;
    for (int vy = 0; vy < vh && lines < kSub3Max; ++vy) {
        for (int vx = 0; vx < vw && lines < kSub3Max; ++vx) {
            const int gx = ox + vx;
            const int gy = oy + vy;
            if (gx < 0 || gy < 0 || gx >= floor->width || gy >= floor->height) {
                continue;
            }
            const auto &grid = floor->get_grid(Pos2D(gy, gx));
            if (grid.m_idx <= 0 || grid.m_idx >= static_cast<MONSTER_IDX>(floor->m_list.size())) {
                continue;
            }
            const auto &mon = floor->m_list[grid.m_idx];
            if (!mon.is_valid() || !mon.ml) {
                continue;
            }
            // プレイヤー自身（騎乗等の特殊は Issue）。視認モンスターのみ。
            const auto name = monster_desc(player, mon, MD_ASSUME_VISIBLE | MD_INDEF_VISIBLE);
            frame.sub3_lines.push_back(to_utf8_line(name));
            ++lines;
        }
    }
    if (frame.sub3_lines.empty()) {
        frame.sub3_lines.emplace_back("(none)");
    }
}

void fill_sub5_inventory(GameFrame &frame, PlayerType *player)
{
    frame.sub5_lines.clear();
    int shown = 0;
    int skipped = 0;
    const int pack_end = (std::min)(static_cast<int>(INVEN_PACK), static_cast<int>(player->inventory.size()));
    for (int i = 0; i < pack_end; ++i) {
        const auto &item_ptr = player->inventory[static_cast<size_t>(i)];
        if (!item_ptr || !item_ptr->is_valid()) {
            continue;
        }
        if (shown >= kSub5Max) {
            ++skipped;
            continue;
        }
        const auto name = describe_flavor(player, *item_ptr, OD_NAME_ONLY | OD_OMIT_PREFIX);
        char buf[256];
        const char slot = static_cast<char>('a' + i);
        std::snprintf(buf, sizeof(buf), "%c) %s", slot, name.c_str());
        frame.sub5_lines.push_back(to_utf8_line(buf));
        ++shown;
    }
    if (skipped > 0) {
        char more[32];
        std::snprintf(more, sizeof(more), "…(+%d)", skipped);
        if (static_cast<int>(frame.sub5_lines.size()) >= kSub5Max) {
            frame.sub5_lines.back() = more;
        } else {
            frame.sub5_lines.emplace_back(more);
        }
    }
    if (frame.sub5_lines.empty()) {
        frame.sub5_lines.emplace_back("(empty)");
    }
}

//! いま光源で照らされているか（松明・モンスター光・視界内の常時照明・地上昼間の太陽光・夜視）。
//! 地下の CAVE_GLOW は視界内のみ満灯（遠い常時照明部屋を満灯のまま残さない）。
bool feature_is_lit_now(PlayerType *player, const FloorType *floor, const Grid &grid)
{
    if ((grid.info & (CAVE_LITE | CAVE_MNLT)) != 0) {
        return true;
    }

    const bool glow = (grid.info & (CAVE_GLOW | CAVE_MNDK)) == CAVE_GLOW;
    if (glow && (grid.info & CAVE_VIEW) != 0) {
        return true;
    }

    // 地上・昼間: 太陽光。踏破済み（MARK）の GLOW は視界外でも明るい。
    if (glow && floor != nullptr && !floor->is_underground() &&
        AngbandWorld::get_instance().is_daytime() && (grid.info & CAVE_MARK) != 0) {
        return true;
    }

    return player != nullptr && player->see_nocto != 0 && (grid.info & CAVE_VIEW) != 0;
}

//! 照明中、または一度見た記憶（CAVE_MARK）。未踏破は false。
bool feature_is_known(PlayerType *player, const FloorType *floor, const Grid &grid)
{
    return feature_is_lit_now(player, floor, grid) || (grid.info & CAVE_MARK) != 0;
}

/*!
 * @brief K-42: **プレイヤが実際に見た／魔法で調べた**マスか（`CAVE_KNOWN`）。
 *
 * ## `feature_is_known`（＝`CAVE_MARK`）との違い
 * `CAVE_MARK` は「**コアが地図に覚えた**」であって「プレイヤが見た」ではない。
 * `note_spot`（`src/grid/grid.cpp:361`）は、床のような REMEMBER を持たない地形を
 * ゲームオプション次第でしか覚えない:
 *
 * | オプション | 既定 | 覚える床 |
 * |---|---|---|
 * | `view_perma_grids` | **ON** | 恒久的に光っている床（＝**明るい部屋**） |
 * | `view_torch_grids` | **OFF** | 松明で照らしただけの床（＝**通路・暗い部屋**） |
 *
 * つまり既定では、通路と暗い部屋の**床だけが地図に残らない**。ミニマップは
 * `Floor` の色を 1 つしか持たないので、これが「通路と暗い部屋のドットの色が
 * 明るい部屋と違う」（＝暗い／出ていない）という見え方になる（人間の指摘）。
 *
 * 一方 `CAVE_KNOWN` は `note_spot` の可視判定を通ったマスへ**必ず**立つ
 * （同ファイル 434 行「Memorize terrain of the grid」）。コア自身も旅行コマンドで
 * `!(grid.info & CAVE_KNOWN)` を「未探索だから通さない」に使っている
 * （`src/action/travel-execution.cpp:94`）＝**プレイヤの知識の境界**そのものである。
 *
 * @note 未探索を漏らさない: `CAVE_KNOWN` が立つのは「視界に入った」か
 * 「魔法の地図・啓蒙で調べた」ときだけ。後者はどちらも壁の並びを先に開示するので、
 * 床を足しても新しく分かることは無い。
 */
bool feature_is_seen(const Grid &grid)
{
    return (grid.info & CAVE_KNOWN) != 0;
}

/*!
 * @brief ミニマップ用にフロア全域を種別コードで写す。
 * @details `GameFrame::cells` は視界内のビューポートだけなので全体図には使えない。
 * ここでフロア全域を走査し、**プレイヤが見たマスだけ**を種別へ落とす。
 * 未踏破を出すと未探索領域を UI が暴露してしまう。
 * 敵も cells と同じく `ml`（視認中）に限る。
 * @note K-42 で判定を `feature_is_known`（`CAVE_MARK`）から
 * **`|| feature_is_seen`（`CAVE_KNOWN`）**へ広げた。通路と暗い部屋の床が地図に残らず、
 * ミニマップ上でそこだけ明るい部屋と別物に見えていたため。理由と安全性は
 * `feature_is_seen` の doc コメントに書いた。
 */
void fill_minimap(GameFrame &frame, PlayerType *player, const FloorType *floor)
{
    frame.minimap = MinimapSnapshot{};
    if (floor == nullptr || floor->width <= 0 || floor->height <= 0) {
        return;
    }
    auto &mm = frame.minimap;
    mm.width = floor->width;
    mm.height = floor->height;
    mm.kinds.assign(static_cast<size_t>(mm.width) * static_cast<size_t>(mm.height),
        static_cast<uint8_t>(MinimapKind::Unknown));

    for (int gy = 0; gy < mm.height; ++gy) {
        for (int gx = 0; gx < mm.width; ++gx) {
            const Pos2D pos(gy, gx);
            const auto &grid = floor->get_grid(pos);
            /*
             * K-42: ミニマップは `CAVE_MARK`（コアが地図に覚えた）ではなく
             * `CAVE_KNOWN`（プレイヤが見た）で出す。既定オプションでは通路と暗い部屋の
             * **床だけ**が覚えられず、その一帯が明るい部屋と違う見え方になるため
             * （`feature_is_seen` の doc コメントに機序と「漏れない根拠」）。
             * `cells`（主画面）は従来どおり `CAVE_MARK` のまま。**コアの主画面と
             * 見え方を揃える約束はそちらが持っている**ので、ここだけを広げる。
             */
            const bool known = feature_is_known(player, floor, grid) || feature_is_seen(grid);
            const bool lit_now = feature_is_lit_now(player, floor, grid);

            // 見えている敵は未踏破のマスでも出す（テレパシー等）。cells 側と同じ扱い。
            bool monster_here = false;
            if (grid.m_idx > 0 && grid.m_idx < static_cast<MONSTER_IDX>(floor->m_list.size())) {
                const auto &mon = floor->m_list[grid.m_idx];
                monster_here = mon.is_valid() && mon.ml;
            }
            if (!known && !monster_here) {
                continue;
            }

            MinimapKind kind = MinimapKind::Floor;
            if (known) {
                // 見た目の地形（MIMIC）で判定する。隠し扉は花崗岩として出すのが正しい。
                const auto &terrain = grid.get_terrain(TerrainKind::MIMIC);
                if (terrain.flags.has(TerrainCharacteristics::STAIRS)) {
                    kind = MinimapKind::Stairs;
                } else if (terrain.flags.has(TerrainCharacteristics::DOOR)) {
                    kind = MinimapKind::Door;
                } else if (terrain.flags.has(TerrainCharacteristics::WALL)) {
                    /*
                     * **山は壁と分けて送る**（2026-08-12）。町の読み方が「侵入不可の塊は
                     * 敷地か岩山か」を決めるのに要る（`MinimapKind::Mountain` の注記）。
                     * `MOUNTAIN` は `WALL` も持っているので、こちらを先に見る。
                     */
                    kind = terrain.flags.has(TerrainCharacteristics::MOUNTAIN)
                        ? MinimapKind::Mountain
                        : MinimapKind::Wall;
                }
            }
            // 床のアイテムは照明下のみ（cells 側の object_id と同条件）。
            if (lit_now && !grid.o_idx_list.empty() && kind != MinimapKind::Stairs) {
                kind = MinimapKind::Item;
            }
            if (monster_here) {
                kind = MinimapKind::Monster;
            }
            mm.kinds[static_cast<size_t>(gy) * static_cast<size_t>(mm.width) + static_cast<size_t>(gx)] =
                static_cast<uint8_t>(kind);
        }
    }

    if (player != nullptr) {
        mm.player_gx = player->x;
        mm.player_gy = player->y;
        if (mm.player_gx >= 0 && mm.player_gx < mm.width && mm.player_gy >= 0 && mm.player_gy < mm.height) {
            mm.kinds[static_cast<size_t>(mm.player_gy) * static_cast<size_t>(mm.width) +
                static_cast<size_t>(mm.player_gx)] = static_cast<uint8_t>(MinimapKind::Player);
        }
    }
}

//! 周囲を数える半径（マス）。**耳が届くと思える範囲**であって、視界とは別。
constexpr int kSurroundingsRadius = 12;

/*!
 * @brief **周囲の地形の内訳**を数える。
 *
 * 環境音は「いま周りに何が在るか」で層を重ねる。数えるのをコア側に置いたのは、
 * 地形の意味（草か土か沼か）を知っているのが `TerrainCharacteristics` を読める側だけで、
 * **画面側は地形テーブルを知らない**ため（設計 §4.1）。
 *
 * ## 数え方で気をつけたこと
 *
 * * **既知のマスだけ**（`feature_is_known` か `feature_is_seen`）。未探査を混ぜると
 *   **まだ見ていない海の音が鳴る**——いちばん質の悪い間違い方をする
 * * **円で数える**（正方形だと角のマスが遠いのに同じ重みになる）
 * * **壁とガラスは材料と別勘定。**壁は「閉塞」の材料で、材質とは直交する
 *   （ガラスの壁は壁でもありガラスでもある）
 * * **沼を水より先に見る。**`SWAMP` は `WATER` を持っているので、順を逆にすると
 *   沼が浅い水として数えられて音が変わらない（`TerrainDefinitions.jsonc` id=194）
 * * **山は壁である**（`MOUNTAIN` は `WALL | PERMANENT`）。壁として数えたうえで
 *   岩としても数える——山の中に居るときに閉塞と岩の両方が要る
 *
 * 読むだけで、コアの状態は 1 つも変えない（必守制約 1）。
 */
void fill_surroundings(GameFrame &frame, PlayerType *player, const FloorType *floor)
{
    frame.surroundings = SurroundingsView{};
    if ((floor == nullptr) || (player == nullptr) || (floor->width <= 0) || (floor->height <= 0)) {
        return;
    }
    int grass = 0;
    int tree = 0;
    int dirt = 0;
    int swamp = 0;
    int water = 0;
    int deep_water = 0;
    int lava = 0;
    int rock = 0;
    int glass = 0;
    int wall = 0;
    int counted = 0;
    constexpr int r = kSurroundingsRadius;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (((dx * dx) + (dy * dy)) > (r * r)) {
                continue; //!< 円で数える
            }
            const int gx = player->x + dx;
            const int gy = player->y + dy;
            if ((gx < 0) || (gy < 0) || (gx >= floor->width) || (gy >= floor->height)) {
                continue;
            }
            const Pos2D pos(gy, gx);
            const auto &grid = floor->get_grid(pos);
            if (!feature_is_known(player, floor, grid) && !feature_is_seen(grid)) {
                continue;
            }
            ++counted;
            const auto &terrain = grid.get_terrain(TerrainKind::MIMIC);
            const auto &flags = terrain.flags;
            const auto tag = terrain.tag_enum;
            const bool is_wall = flags.has(TerrainCharacteristics::WALL);
            if (is_wall) {
                ++wall;
            }
            if (flags.has(TerrainCharacteristics::GLASS)) {
                ++glass;
            }
            // ---- 材料は排他。順番に意味がある（doc コメント参照） ----
            if (tag == TerrainTag::SWAMP) {
                ++swamp;
            } else if (flags.has(TerrainCharacteristics::LAVA)) {
                ++lava;
            } else if (flags.has(TerrainCharacteristics::WATER)) {
                if (flags.has(TerrainCharacteristics::DEEP)) {
                    ++deep_water;
                } else {
                    ++water;
                }
            } else if (flags.has(TerrainCharacteristics::TREE)) {
                ++tree;
            } else if (tag == TerrainTag::MOUNTAIN) {
                ++rock;
            } else if (flags.has(TerrainCharacteristics::STONE) && !is_wall && !flags.has(TerrainCharacteristics::DOOR)) {
                ++rock; //!< 岩石（RUBBLE とその水上派生）
            } else if ((tag == TerrainTag::GRASS) || (tag == TerrainTag::BRAKE) || (tag == TerrainTag::FLOWER)) {
                ++grass;
            } else if (tag == TerrainTag::DIRT) {
                ++dirt;
            }
        }
    }
    if (counted <= 0) {
        return; //!< `radius` が 0 のまま＝「数えていない」。画面側は層を使わない
    }
    const auto ratio = [counted](int n) {
        const int v = (n * 255) / counted;
        return static_cast<uint8_t>((v < 0) ? 0 : ((v > 255) ? 255 : v));
    };
    auto &s = frame.surroundings;
    s.grass = ratio(grass);
    s.tree = ratio(tree);
    s.dirt = ratio(dirt);
    s.swamp = ratio(swamp);
    s.water = ratio(water);
    s.deep_water = ratio(deep_water);
    s.lava = ratio(lava);
    s.rock = ratio(rock);
    s.glass = ratio(glass);
    s.wall = ratio(wall);
    s.radius = static_cast<uint8_t>(r);
    s.counted = static_cast<uint16_t>((counted > 0xFFFF) ? 0xFFFF : counted);
}

/*!
 * @brief 見た目（MIMIC）の地形特性を MapCellView::feature_flags のビットへ翻訳する。
 * @param terrain MIMIC で解決済みの地形定義（読取のみ）
 * @return cell_feature_bits.h のビット論理和。KNOWN / PLAYER は呼び出し側の責務。
 * @details HD2D は「壁はブロック／扉は薄板」のように**性質**で描き分けるが、ui に terrain_id から
 * 性質を逆引きさせると ui が地形テーブルを知ることになる（設計書 §4.1）。そこで Bridge が
 * `TerrainCharacteristics` を読んでビットへ落とす。判定に MIMIC を使うのは fill_minimap と同じ理由で、
 * 隠し扉は花崗岩の壁として立ち上がるのが正しい見え方だから。
 * 参照はビットセットの `has` だけなので、毎フレーム全セルに掛けても線形探索は発生しない。
 */
uint16_t translate_feature_flags(const TerrainType &terrain)
{
    // uint16_t への複合代入は /W4 で縮小変換の警告になりうるので unsigned に溜めて最後に 1 回だけ落とす。
    unsigned bits = 0u;
    if (terrain.flags.has(TerrainCharacteristics::WALL)) {
        bits |= CELL_FEAT_WALL;
    }
    if (terrain.flags.has(TerrainCharacteristics::DOOR)) {
        bits |= CELL_FEAT_DOOR;
        // 開いている／壊れている扉の判定は CLOSE（＝「閉じる」コマンドの対象＝いま開いている）で行う。
        // OPEN は「開ける」コマンドの対象＝まだ閉じている側なので、意味が逆になる点に注意。
        // 裏取り: lib/edit/TerrainDefinitions.jsonc で CLOSE を持つ扉は
        // OPEN_DOOR(4)・BROKEN_DOOR(5)・OPEN_GLASS_DOOR(200)・BROKEN_GLASS_DOOR(201)・OPEN_CURTAIN(220) のみ。
        // 閉じた扉（CLOSED / LOCKED_*/ JAMMED_* / SECRET_DOOR）は OPEN・BASH 側を持ち CLOSE を持たない。
        // 壊れた扉も CLOSE 持ちなので、閉じた扉と混ざらずそのまま R6（開口部の透過）へ乗る。
        // ※ 店舗・建物の入口も door 定義を持つため DOOR は立つが CLOSE は無い。薄板のまま描かれる＝意図どおり。
        if (terrain.flags.has(TerrainCharacteristics::CLOSE)) {
            bits |= CELL_FEAT_DOOR_OPEN;
        }
    }
    /*
     * 岩石（RUBBLE）。版 2.7 で追加。立ち木と同じく**立ち木・岩の板**で描くために要る。
     *
     * ## なぜ「STONE かつ 壁でも扉でもない」なのか（id 直判定を避けた）
     * `TerrainCharacteristics` に「岩石の山」そのものを表すフラグは無い。いちばん近いのが
     * `STONE`（= 岩石溶解の対象。`src/system/enums/terrain/terrain-characteristics.h` の 44）で、
     * これは壁・鉱脈にも扉にも付く。そこで**壁でも扉でもない STONE** を岩石と読む。
     * 裏取り: `lib/edit/TerrainDefinitions.jsonc` を機械的に走査すると、STONE を持ち
     * WALL も door 定義も持たない地形は
     *   RUBBLE(49) / RUBBLE_ON_SHALLOW_WATER(222) / RUBBLE_ON_DEEP_WATER(235)
     * の 3 件だけで、いずれも岩石。水上派生も同じ見た目にしたいのでこの 3 件で過不足がない。
     * id を直に見る手もあるが、それだと 222・235 を書き落とす／将来の追加に追随できないので、
     * 性質での判定を採った（設計書 §4.1 の「ui に逆引きさせない」と同じ理由が Bridge 内でも効く）。
     * 判定順は WALL・DOOR を立てた**後**でなければならない。
     */
    if (terrain.flags.has(TerrainCharacteristics::STONE) && (bits & (CELL_FEAT_WALL | CELL_FEAT_DOOR)) == 0u) {
        bits |= CELL_FEAT_RUBBLE;
    }
    if (terrain.flags.has(TerrainCharacteristics::STAIRS)) {
        bits |= CELL_FEAT_STAIRS;
    }
    if (terrain.flags.has(TerrainCharacteristics::PERMANENT)) {
        bits |= CELL_FEAT_PERMANENT;
    }
    if (terrain.flags.has(TerrainCharacteristics::TREE)) {
        bits |= CELL_FEAT_TREE;
    }
    if (terrain.flags.has(TerrainCharacteristics::WATER)) {
        bits |= CELL_FEAT_WATER;
    }
    if (terrain.flags.has(TerrainCharacteristics::LAVA)) {
        bits |= CELL_FEAT_LAVA;
    }
    if (terrain.flags.has(TerrainCharacteristics::GLOW)) {
        bits |= CELL_FEAT_GLOW;
    }
    /*
     * 通り抜けられる床か（HD2D の扉の向き判定の主材料）。
     * `MOVE` は「移動可能な地形」。閉じた扉は `MOVE` を持たない（開けるまで入れない）が、
     * 開いた扉・壊れた扉は持つ。ここで欲しいのは**扉そのものではなく扉の隣の通路／部屋の床**
     * なので、壁・扉・立木は明示的に外す。外さないと開扉の隣に開扉が並んだときに
     * 「通り抜けられる」と数えてしまい、壁の向きを見誤る。
     * 水・溶岩・階段・罠は `MOVE` を持つのでそのまま床として数える（通り抜けられるのは事実）。
     */
    const bool blocking = (bits & (CELL_FEAT_WALL | CELL_FEAT_DOOR | CELL_FEAT_TREE | CELL_FEAT_RUBBLE)) != 0u;
    if (!blocking && terrain.flags.has(TerrainCharacteristics::MOVE)) {
        bits |= CELL_FEAT_PASSABLE;
    }
    return static_cast<uint16_t>(bits);
}

uint16_t resolve_tile_index(PlayerType *player, const FloorType *floor, const Grid &grid,
    int gx, int gy, char ascii, uint8_t fg, uint16_t terrain_id, bool know_feature, bool lit_now)
{
    // プレイヤセル → P 索引（variant_key = PlayerRaceType）。未登録種族は lookup 側で先頭 P フォールバック。
    if (player != nullptr && gx == player->x && gy == player->y) {
        const auto race_key = static_cast<uint16_t>(enum2i(player->prace));
        if (const uint16_t pt = lookup_player_tile(race_key)) {
            return pt;
        }
    }

    // 可視モンスター → R 索引（暗闇でも ml なら見える）。
    // ※ 公式 R タイルが無い monrace はここを素通りして地形タイルを返す（2026-08-12 時点は
    //   全 1416 体に索引がある。上流取り込みで増えた分は §7 の生成手順＝B-08 第 3 期で塞ぐ）。
    //   これは意図的で、下敷きの床を描くため。ただし素通りしたままだとモンスターが
    //   床で塗り潰されて不可視になるので、ui 側が monster_id != 0 かつ
    //   official_tile_kind(tile_index) != 'R' を検出して ASCII 記号を重ねる。
    if (floor != nullptr && grid.m_idx > 0 &&
        grid.m_idx < static_cast<MONSTER_IDX>(floor->m_list.size())) {
        const auto &mon = floor->m_list[grid.m_idx];
        if (mon.is_valid() && mon.ml) {
            const auto rid = static_cast<uint16_t>(enum2i(mon.get_monrace_id()));
            if (const uint16_t mt = lookup_monster_tile(rid)) {
                return mt;
            }
#ifdef TANGBAND
            // 本家表に無い短愚蛮怒の追加モンスター（official_tile_table_tang.h）
            if (const uint16_t tt = tangband_lookup_monster_tile(rid)) {
                return tt;
            }
#endif
        }
    }

    // 未視認: 地形／床オブジェクトなし（真っ暗）。
    if (!know_feature) {
        return 0;
    }

    // 床オブジェクトは「今見えている」ときのみ（記憶マスにアイテム絵を残さない）。
    if (lit_now && floor != nullptr && !grid.o_idx_list.empty()) {
        const OBJECT_IDX o_idx = *grid.o_idx_list.begin();
        if (o_idx > 0 && o_idx < static_cast<OBJECT_IDX>(floor->o_list.size()) &&
            floor->o_list[static_cast<size_t>(o_idx)]) {
            const auto &item = *floor->o_list[static_cast<size_t>(o_idx)];
            if (item.is_valid()) {
                if (const uint16_t ot = lookup_object_tile(static_cast<uint16_t>(item.bi_id))) {
                    return ot;
                }
            }
        }
    }

    // 地形公式 PNG（F）。未登録 ID は ASCII プレースホルダ索引へ。
    if (const uint16_t tt = lookup_terrain_tile(terrain_id)) {
        return tt;
    }

    return lookup_tile_index(ascii, fg);
}

char sanitize_term_char(char c)
{
    if (c == '\0') {
        return ' ';
    }
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
        return ' ';
    }
    return c;
}

/*!
 * @brief セル 1 個ぶんの文字を行へ足す。
 * @details セルは**文字を丸ごと**持つ（`term/term-char.h`）ので、全角はここで 1 度に載る。
 * 全角の右半分（継続セル）は空なので何も足さない。制御文字だけを空白へ落とす。
 */
void append_term_char(std::string &line, const TermChar &cell)
{
    const auto sv = cell.view();
    if (sv.empty()) {
        return; // 全角の右半分
    }

    if (sv.size() == 1) {
        line.push_back(sanitize_term_char(sv[0]));
        return;
    }

    line.append(sv);
}

//! セルが空白（または空）か。行末を削る判定に使う。
bool term_char_is_blank(const TermChar &cell)
{
    const auto sv = cell.view();
    return sv.empty() || ((sv.size() == 1) && (sanitize_term_char(sv[0]) == ' '));
}

//! フロートメニュー用: 通常プレイ中の Term スナップショット（screen_save 無し icky 対策）。
std::unique_ptr<term_win> g_float_menu_baseline;

void refresh_float_menu_baseline()
{
    if (game_term == nullptr || game_term->scr == nullptr) {
        g_float_menu_baseline.reset();
        return;
    }
    g_float_menu_baseline = game_term->scr->clone();
}

const term_win *float_menu_reference_win()
{
    // 1) screen_save 済みならその直前画面（ネストメニューも top が直前）
    if (game_term != nullptr && !game_term->mem_stack.empty()) {
        return game_term->mem_stack.top().get();
    }
    // 2) close_game のように icky だけ立てる経路用
    return g_float_menu_baseline.get();
}

//! 基準画面から変化したセルか（＝いまのフロートメニュー描画分）。
bool cell_is_float_menu_content(int x, int y)
{
    if (game_term == nullptr || game_term->scr == nullptr) {
        return false;
    }

    const int ax = x + game_term->offset_x;
    const int ay = y + game_term->offset_y;
    const auto &scr = *game_term->scr;
    if (ay < 0 || ax < 0 ||
        static_cast<size_t>(ay) >= scr.a.size() ||
        static_cast<size_t>(ax) >= scr.a[static_cast<size_t>(ay)].size()) {
        return false;
    }

    const auto sa = scr.a[static_cast<size_t>(ay)][static_cast<size_t>(ax)];
    const auto sc = scr.c[static_cast<size_t>(ay)][static_cast<size_t>(ax)];
    if (is_graphic_tile_attr(sa)) {
        return false;
    }

    if (const term_win *ref = float_menu_reference_win()) {
        if (static_cast<size_t>(ay) >= ref->a.size() ||
            static_cast<size_t>(ax) >= ref->a[static_cast<size_t>(ay)].size()) {
            return true;
        }
        const auto ra = ref->a[static_cast<size_t>(ay)][static_cast<size_t>(ax)];
        const auto rc = ref->c[static_cast<size_t>(ay)][static_cast<size_t>(ax)];
        return sa != ra || sc != rc;
    }

    // 基準無し最終手段: プロンプト行（0行目）の非空白のみ。
    if (y != 0) {
        return false;
    }
    if ((sa & kAfKanji2) != 0) {
        return false;
    }

    return !term_char_is_blank(sc);
}

void append_term_cell(std::string &line, int x, int y, int width, bool skip_graphic_tiles)
{
    (void)width;
    const DisplaySymbol ds = term_what(x, y, DisplaySymbol());
    const uint8_t attr = ds.color;

    if (skip_graphic_tiles && is_graphic_tile_attr(attr)) {
        line.push_back(' ');
        return;
    }

    if ((attr & kAfKanji2) != 0) {
        return; // 全角の右半分（左半分のセルが文字を丸ごと持っている）
    }

    append_term_char(line, term_what_char(x, y));
}

/*!
 * @brief ミラー 1 行の色（行内で最初に見つかった文字の色）。
 * @param crop_to_content 本文セルだけを見るか（ゲーム中フロートメニュー）。
 * @details **本文の抽出（`fill_menu_term_mirror`）と同じセルだけを見る**こと。
 * 外接矩形の中でも「基準から変化していないセル」は本文ではなく**背後の地図**なので、
 * ここで色を拾うと地図の色が行全体の色になる。実際、コマンドメニューの下辺が地図まで
 * 伸びる行（`引退する(Q)` と最下辺の枠）だけが床の橙色で描かれていた。
 */
uint8_t pick_line_color(int x0, int x1, int y, bool crop_to_content)
{
    for (int x = x0; x <= x1; ++x) {
        if (crop_to_content && !cell_is_float_menu_content(x, y)) {
            continue;
        }
        const DisplaySymbol ds = term_what(x, y, DisplaySymbol());
        if (is_graphic_tile_attr(ds.color)) {
            continue;
        }
        if ((ds.color & kAfKanji2) != 0) {
            continue;
        }
        if (!term_char_is_blank(term_what_char(x, y))) {
            return ds.color & kAfKanjic;
        }
    }
    return 1;
}

//! ゲーム中フロートメニュー: 基準画面から変化したセルの外接矩形。
void find_menu_content_bounds(int width, int height, int &x0, int &y0, int &x1, int &y1)
{
    x0 = width;
    y0 = height;
    x1 = -1;
    y1 = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!cell_is_float_menu_content(x, y)) {
                continue;
            }
            x0 = (std::min)(x0, x);
            y0 = (std::min)(y0, y);
            x1 = (std::max)(x1, x);
            y1 = (std::max)(y1, y);
        }
    }
    if (x1 < 0 || y1 < 0) {
        x0 = 0;
        y0 = 0;
        x1 = width - 1;
        y1 = height - 1;
    }
}

/*!
 * @name K-28: コアの左フレーム（キャラクター状態）を MainMap の左へ写す
 * @details コアは主画面の**列 0〜12**に種族・称号・レベル・経験値・所持金・装備記号・
 * 能力値・AC・HP・SP・切り傷・朦朧・空腹・状態・思い出・日付・ダンジョン名を描く
 * （`src/window/main-window-row-column.h`。`print_frame_basic` / `print_frame_extra`）。
 * SDL2 UI は Term を描かないので、その 13 桁を**そのまま読んで**渡す（作り直さない）。
 *
 * 読む相手は「いま icky か」で変わる。`redraw_stuff()` は
 * `character_icky_depth > 0` の間**左フレームを更新しない**（`window-redrawer.cpp:79`）ので、
 * 店やメニューを開いている間の `scr` は本文で塗り潰されている。
 * その場合は `screen_save()` が積んだ**直前の画面**（`float_menu_reference_win`）から読む。
 * どちらも無い画面（死亡プロンプト等）では**直前に読めた値を使い回す**。
 * @{
 */
//! コアの左フレームの桁数（`COL_*` が使う固定幅。`LEVEL xxxxxx` = 12 桁 ＋ 余白 1）。
constexpr int kStatusColCells = 13;

//! 直前に読めた状態列。icky で保存画面も無いときに使い回す。
std::vector<SubPanelLine> g_status_col_cache;
//! 同・最下行（K-32）。
std::vector<TermTextRun> g_bottom_row_cache;
//! 同・階層（K-33）。
SubPanelLine g_depth_cache;

/*!
 * @brief `win` の 1 行の桁 `[x0, x1)` を UTF-8 の 1 行にする（行の色＝最初の非空白の色）。
 * @details 位置は**論理座標**で受け、`term_win` の添字には `offset_x/offset_y` を足す
 * （`cell_is_float_menu_content` と同じ約束）。80×24 の Term では 0 だが、
 * `TermCenteredOffsetSetter` が効いている間は 0 でない。
 * @return 読めたら true（範囲外の行は false）
 */
bool read_term_row_span(const term_win &win, int row, int x0, int x1, SubPanelLine &out)
{
    const int off_x = (game_term != nullptr) ? game_term->offset_x : 0;
    const int off_y = (game_term != nullptr) ? game_term->offset_y : 0;
    const int y = row + off_y;
    if (y < 0 || static_cast<size_t>(y) >= win.c.size()) {
        return false;
    }
    const auto &row_c = win.c[static_cast<size_t>(y)];
    const auto &row_a = win.a[static_cast<size_t>(y)];
    const int begin = (std::max)(0, x0 + off_x);
    const int end = (std::min)(x1 + off_x, static_cast<int>(row_c.size()));

    std::string sys_line;
    uint8_t color = 1;
    bool color_found = false;
    for (int x = begin; x < end; ++x) {
        const uint8_t attr = static_cast<uint8_t>(row_a[static_cast<size_t>(x)]);
        if (is_graphic_tile_attr(attr)) {
            sys_line.push_back(' '); // 地図のタイルが入り込む桁は空白にする
            continue;
        }
        if ((attr & kAfKanji2) != 0) {
            continue; // 全角の右半分（左半分のセルが文字を丸ごと持っている）
        }
        const auto &cell = row_c[static_cast<size_t>(x)];
        append_term_char(sys_line, cell);
        if (!color_found && !term_char_is_blank(cell)) {
            color = static_cast<uint8_t>(attr & kAfKanjic);
            color_found = true;
        }
    }
    // 前後の空白は落とす（`print_depth` の `%7s` は右詰めで来る）。
    size_t head = sys_line.find_first_not_of(' ');
    if (head == std::string::npos) {
        head = sys_line.size();
    }
    size_t tail = sys_line.size();
    while (tail > head && sys_line[tail - 1] == ' ') {
        --tail;
    }
    sys_line = sys_line.substr(head, tail - head);

    out = SubPanelLine{};
    out.color = color;
    if (const auto utf8 = sys_to_utf8(sys_line)) {
        out.text_utf8 = *utf8;
    } else {
        out.text_utf8 = sys_line;
    }
    return true;
}

//! `win` の左 `kStatusColCells` 桁・行 `y0`〜`y1` を 1 行ずつ積む。
void read_status_col(const term_win &win, int y0, int y1, std::vector<SubPanelLine> &out)
{
    out.clear();
    for (int row = y0; row <= y1; ++row) {
        SubPanelLine line{};
        if (read_term_row_span(win, row, 0, kStatusColCells, line)) {
            out.push_back(std::move(line));
        }
    }

    while (!out.empty() && out.back().text_utf8.empty()) {
        out.pop_back();
    }
}

//! 階層が書かれている桁数（`COL_DEPTH = -8`。`%7s` ＋ 余白 1 桁）。
constexpr int kDepthCells = 8;

/*!
 * @brief K-31 / K-33: 階層（`100 階` / `地上`）を読む。
 * @details 人間の指摘「現在いる階層の情報が出ない。メインパネルの左に出るダンジョン名や
 * 地名に階層情報はなかったか？」。**無かった。** コアはダンジョン名を左の列
 * （`ROW_DUNGEON = hgt-2` / `COL_DUNGEON = 0`）に書くが、**階層は最下行の右端**
 * （`print_depth`＝`main-window-left-frame.cpp:171`。`ROW_DEPTH = hgt-1` /
 * `COL_DEPTH = wid-8` へ `%7s`）に書く。
 *
 * K-33（人間の指示「メインパネルの右下にだせないか？」）で置き場所を
 * **MainMap の右下だけ**に決めた。横に長い画面では最下段の右端が地図から遠すぎる。
 * 状態列にも最下段の帯にも入れない（同じ情報を 3 か所に出さない）。
 * @note 色は**階の雰囲気**（`DungeonFeeling`）を表しているのでコアのまま持ち上げる
 * （灰＝不明 / 水色＝特別 / 紫＝恐ろしい …）。
 */
void read_depth(const term_win &win, int width, int height, SubPanelLine &out)
{
    out = SubPanelLine{};
    SubPanelLine line{};
    if (!read_term_row_span(win, height - 1, width - kDepthCells, width, line)) {
        return;
    }
    out = std::move(line);
}

/*!
 * @brief K-32: コアの**最下行（ステータスバー）**を色つき断片として読む。
 * @details 人間の指示「最下行にコアと同じ表示をできる？」。この行は
 * **桁と色の両方に意味がある**（左に状態異常・空腹・休息の記号＝`print_status`、
 * 右端の決まった桁に速度・学習・階層＝`COL_SPEED = -24` / `COL_STUDY = -13` / `COL_DEPTH = -8`）。
 * 1 行 1 色の `SubPanelLine` では両方とも落ちるので、**同じ色が続く区間ごと**に切って
 * 桁とともに渡す。描画側はその桁に合わせて置くだけでコアと同じ並びになる。
 * @param win 読む画面（icky 中は `screen_save` の保存画面）
 * @param width Term の桁数
 * @param row 読む行（＝`height - 1`）
 */
void read_bottom_row_runs(const term_win &win, int width, int row, std::vector<TermTextRun> &out)
{
    out.clear();
    const int off_x = (game_term != nullptr) ? game_term->offset_x : 0;
    const int off_y = (game_term != nullptr) ? game_term->offset_y : 0;
    const int y = row + off_y;
    if (y < 0 || static_cast<size_t>(y) >= win.c.size()) {
        return;
    }
    const auto &row_c = win.c[static_cast<size_t>(y)];
    const auto &row_a = win.a[static_cast<size_t>(y)];
    const int end = (std::min)(width + off_x, static_cast<int>(row_c.size()));

    std::string sys_run;
    int run_col = 0;
    uint8_t run_color = 1;

    const auto flush = [&]() {
        while (!sys_run.empty() && sys_run.back() == ' ') {
            sys_run.pop_back();
        }
        if (sys_run.empty()) {
            return;
        }
        TermTextRun run{};
        run.col = run_col;
        run.color = run_color;
        if (const auto utf8 = sys_to_utf8(sys_run)) {
            run.text_utf8 = *utf8;
        } else {
            run.text_utf8 = sys_run;
        }
        out.push_back(std::move(run));
        sys_run.clear();
    };

    // K-33: 階層は MainMap の右下へ出すので、この行からは**除く**（二重に出さない）。
    const int depth_begin = width - kDepthCells + off_x;
    for (int x = (std::max)(0, off_x); x < end; ++x) {
        if (x >= depth_begin) {
            break;
        }
        const uint8_t attr = static_cast<uint8_t>(row_a[static_cast<size_t>(x)]);
        if ((attr & kAfKanji2) != 0) {
            continue; // 全角の右半分（左半分のセルが文字を丸ごと持っている）
        }
        if (is_graphic_tile_attr(attr)) {
            flush(); // 地図のタイルが入り込む桁で区切る
            continue;
        }

        const auto &cell = row_c[static_cast<size_t>(x)];
        const auto color = static_cast<uint8_t>(attr & kAfKanjic);
        if (term_char_is_blank(cell)) {
            // 空白 2 桁以上で区切る（1 桁の空白は語の間なので run に含める）。
            if (!sys_run.empty() && x + 1 < end) {
                const uint8_t next_attr = static_cast<uint8_t>(row_a[static_cast<size_t>(x) + 1]);
                if (term_char_is_blank(row_c[static_cast<size_t>(x) + 1]) && (next_attr & kAfKanji2) == 0) {
                    flush();
                    continue;
                }
            }
            if (!sys_run.empty()) {
                sys_run.push_back(' ');
            }
            continue;
        }

        if (sys_run.empty()) {
            run_col = x - off_x;
            run_color = color;
        } else if (color != run_color) {
            flush();
            run_col = x - off_x;
            run_color = color;
        }
        append_term_char(sys_run, cell); //!< 全角も 1 度にここへ載る（右半分のセルは空）
    }
    flush();
}

//! 左の状態列（K-28 / K-31）とコア最下行（K-32）をまとめて写す。読む相手の判断が同じなので 1 か所に置く。
void fill_status_col(GameFrame &frame)
{
    frame.status_col_lines.clear();
    //! 桁数は**コアが申告する**（画面側に 13 を持たせない。`game_frame.h` の `status_col_cols`）。
    frame.status_col_cols = kStatusColCells;
    frame.status_col_side = 0; //!< 変愚の左フレーム。明示する（設計 FROX §6.1: 既定任せにしない）
    frame.bottom_row_runs.clear();
    frame.bottom_row_cols = 0;
    frame.depth = SubPanelLine{};

    const auto &world = AngbandWorld::get_instance();
    if (!world.character_generated || !world.character_dungeon) {
        // キャラクターがまだ無い（オープニング・birth）。Term の左端は news の本文なので読まない。
        g_status_col_cache.clear();
        g_bottom_row_cache.clear();
        g_depth_cache = SubPanelLine{};
        return;
    }
    if (game_term == nullptr) {
        return;
    }

    const auto [width, height] = term_get_size();
    if (width <= 0 || height <= 2) {
        return;
    }
    // 行 0 はメッセージ行、最下行はステータスバー（横いっぱい）なので列としては外す。
    // ただし最下行の右端にある**階層**だけは末尾に足す（K-31。`append_depth_line`）。
    const int y0 = 1;
    const int y1 = height - 2;

    frame.bottom_row_cols = width;

    const bool icky = world.character_icky_depth > 0;
    const term_win *win = nullptr;
    if (!icky) {
        win = game_term->scr.get();
    } else {
        win = float_menu_reference_win();
    }
    if (win != nullptr) {
        read_status_col(*win, y0, y1, frame.status_col_lines);
        read_depth(*win, width, height, frame.depth);
        read_bottom_row_runs(*win, width, height - 1, frame.bottom_row_runs);
        if (!icky) {
            g_status_col_cache = frame.status_col_lines;
            g_bottom_row_cache = frame.bottom_row_runs;
            g_depth_cache = frame.depth;
        }
    }

    if (frame.status_col_lines.empty()) {
        frame.status_col_lines = g_status_col_cache;
    }
    if (frame.bottom_row_runs.empty()) {
        frame.bottom_row_runs = g_bottom_row_cache;
    }
    if (frame.depth.text_utf8.empty()) {
        frame.depth = g_depth_cache;
    }
}
/*! @} */

/*!
 * @brief コアの色表をそのままフレームへ写す。
 * @details コアは `do_cmd_colors`（`&`）と pref の `V:` 行で `angband_color_table` を
 * 書き換え、`term_xtra(TERM_XTRA_REACT)` で「引き直せ」と言ってくる。**null term では
 * その合図を受け取れない**（画素を持たないので受け取っても何もできない）ので、
 * 合図を待たずに**毎フレーム写す**。48 バイトで、既定と同じ間は codec が省く。
 * @note 表は `[K, R, G, B]` の 4 バイト。`K` は X11 版だけが使う色番号
 * （`main-x11.cpp:1985`）なので、RGB の 3 バイトだけ渡す。
 */
void fill_term_palette(GameFrame &frame)
{
    for (size_t i = 0; i < frame.term_palette.rgb.size(); ++i) {
        frame.term_palette.rgb[i][0] = angband_color_table[i][1];
        frame.term_palette.rgb[i][1] = angband_color_table[i][2];
        frame.term_palette.rgb[i][2] = angband_color_table[i][3];
    }
}

void fill_menu_term_mirror(GameFrame &frame)
{
    frame.menu_term_lines.clear();
    frame.menu_term_curs_col = -1;
    frame.menu_term_curs_row = -1;

    if (!frame.menu_open) {
        return;
    }

    if (game_term == nullptr || game_term->scr == nullptr) {
        return;
    }

    const auto [width, height] = term_get_size();
    if (width <= 0 || height <= 0) {
        return;
    }

    // pre_game_menu（タイトル／birth／死亡）: Term 全面。
    // ゲーム中フロート: 基準との差分セルだけ（地図・左ステータスごみを落とす）。
    const bool crop_to_content = !frame.pre_game_menu;
    int x0 = 0;
    int y0 = 0;
    int x1 = width - 1;
    int y1 = height - 1;
    if (crop_to_content) {
        find_menu_content_bounds(width, height, x0, y0, x1, y1);
    }

    frame.menu_term_lines.reserve(static_cast<size_t>(y1 - y0 + 1));

    for (int y = y0; y <= y1; ++y) {
        std::string sys_line;
        sys_line.reserve(static_cast<size_t>(x1 - x0 + 1) * 2U);

        /*
         * K-23: 黄色（`TERM_YELLOW`）で書かれた最初の連続部分の位置を覚える。
         * birth（種族・職業・性格・魔法領域・性別・オートローラー）はコアが
         * `c_put_str(TERM_YELLOW, ...)` で選択中の項目を、直前の項目を `TERM_WHITE` に
         * 戻して描く（`birth-select-race.cpp:46,76` ほか）。**印は色だけ**なので、
         * `》` を探す `fill_core_cursor` では見つからない。
         * `TermMirrorLine::color` は 1 行 1 色（`pick_line_color`）なので、
         * 行の途中の黄色は描画時に失われる。ここで位置を採っておく。
         */
        size_t yellow_begin_sys = std::string::npos;
        size_t yellow_end_sys = 0;

        /*
         * 色の切れ目（SJIS のバイト位置）。`(位置, 色)` の並びで、次の切れ目までが
         * その色である。**空白は直前の色に付ける**（文字の無いセルに色は無いので、
         * そこで切ると区間が細切れになるだけで得が無い）。行頭の空白も同じ理由で
         * 最初の色に飲ませるため、最初の切れ目は必ず位置 0 から始める。
         */
        std::vector<std::pair<size_t, uint8_t>> color_runs;

        for (int x = x0; x <= x1; ++x) {
            // 外接矩形内でも未変化セルは空白（地図の噛み込み防止）。
            if (crop_to_content && !cell_is_float_menu_content(x, y)) {
                sys_line.push_back(' ');
                continue;
            }
            const size_t before = sys_line.size();
            append_term_cell(sys_line, x, y, width, crop_to_content);
            if (sys_line.size() == before) {
                continue; // 全角の 2 セル目（1 セル目でまとめて積んである）
            }
            const DisplaySymbol ds = term_what(x, y, DisplaySymbol());
            const uint8_t color = ds.color & kAfKanjic;

            // 色を持つ文字か（`pick_line_color` と同じ選り分け）。
            const bool has_color = !is_graphic_tile_attr(ds.color) && ((ds.color & kAfKanji2) == 0)
                && (sanitize_term_char(ds.character) != ' ');
            if (has_color && (color_runs.empty() || (color_runs.back().second != color))) {
                color_runs.emplace_back(color_runs.empty() ? 0U : before, color);
            }

            if (color != TERM_YELLOW) {
                continue;
            }
            // 最初の連続部分だけ。離れた黄色（別の項目）まで飲み込まない。
            if (yellow_begin_sys == std::string::npos) {
                yellow_begin_sys = before;
            } else if (before > yellow_end_sys) {
                continue;
            }
            yellow_end_sys = sys_line.size();
        }

        while (!sys_line.empty() && sys_line.back() == ' ') {
            sys_line.pop_back();
        }

        if (sys_line.empty()) {
            continue;
        }

        TermMirrorLine line{};
        line.source_row = y;
        line.color = pick_line_color(x0, x1, y, crop_to_content);
        if (const auto utf8 = sys_to_utf8(sys_line)) {
            line.text_utf8 = *utf8;
        } else {
            line.text_utf8 = sys_line;
        }

        // SJIS のバイト位置は UTF-8 のそれと違う。**前置部分を変換した長さ**で位置を出す。
        const auto to_utf8_len = [](const std::string &sys) -> int {
            if (sys.empty()) {
                return 0;
            }
            const auto utf8 = sys_to_utf8(sys);
            return static_cast<int>((utf8 ? *utf8 : sys).size());
        };

        // 末尾の空白を落とした分だけ縮める（行末が黄色でも枠を失わないように clamp）。
        yellow_end_sys = (std::min)(yellow_end_sys, sys_line.size());

        if (yellow_begin_sys != std::string::npos && yellow_end_sys > yellow_begin_sys) {
            line.highlight_begin = to_utf8_len(sys_line.substr(0, yellow_begin_sys));
            line.highlight_len =
                to_utf8_len(sys_line.substr(yellow_begin_sys, yellow_end_sys - yellow_begin_sys));
        }

        /*
         * 1 行に 2 色以上あるときだけ区間を積む。ほとんどの行は 1 色なので、
         * ここで切り上げれば通常のプレイでは 1 バイトも増えない。
         * 区間は**行頭から行末まで隙間なく**並べる（描画側は区間だけを見て描く）。
         */
        if (color_runs.size() >= 2) {
            const int text_len = static_cast<int>(line.text_utf8.size());
            line.color_spans.reserve(color_runs.size());
            for (size_t i = 0; i < color_runs.size(); ++i) {
                // 末尾の空白を落としたので、それより後ろから始まる区間は消えている。
                if (color_runs[i].first >= sys_line.size()) {
                    break;
                }
                const int begin = to_utf8_len(sys_line.substr(0, color_runs[i].first));
                if (begin >= text_len) {
                    break;
                }
                if (!line.color_spans.empty()) {
                    TermColorSpan &previous = line.color_spans.back();
                    previous.len = begin - previous.begin;
                }
                line.color_spans.push_back(TermColorSpan{ begin, text_len - begin, color_runs[i].second });
            }
            if (line.color_spans.size() < 2) {
                line.color_spans.clear(); // 末尾の空白を落として 1 色になった
            }
        }

        frame.menu_term_lines.push_back(std::move(line));
    }

    /*
     * 文字入力のキャレット。**行は `menu_term_lines` の添字で渡す。**
     * Term の行番号のまま渡すと、空行を捨てて詰めた分だけずれる（描画側は添字として使う）。
     */
    const auto [cx, cy] = term_locate();
    if (cx >= x0 && cy >= y0 && cx <= x1 && cy <= y1) {
        for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
            if (frame.menu_term_lines[i].source_row != cy) {
                continue;
            }
            frame.menu_term_curs_col = cx - x0;
            frame.menu_term_curs_row = static_cast<int>(i);
            break;
        }
    }
}

/*!
 * @name K-16: Term ミラーから「1 キーで決まる選択肢」を読み取る（設計書 §14）
 *
 * @details コアの建物（`src/cmd-building/cmd-building.cpp:370`）は `inkey()` で 1 文字受けて
 * `bldg.letters[]` と突き合わせるだけ、店（`src/store/cmd-store.cpp:152`）は
 * `InputKeyRequestor(..., shopping=true)` なので `input-key-requester.cpp:117` の条件で
 * コマンドメニューが**明示的に殺されている**。どちらもコアにカーソル操作の仕組みが無い。
 * コアは非破壊（必守制約 1）なので、**Term ミラーを読んで ui 側にカーソルを作る**。
 *
 * ここでの解析はすべて **UTF-8 化した後の行**に対して行う。理由は 2 つ:
 * 1. この TU は `/execution-charset:utf-8` でコンパイルされる（vcxproj:947）。
 *    `_("スペース", "SPACE")` のような**リテラルは UTF-8** なので、SJIS の Term バイト列
 *    とは直接比較できない。
 * 2. 描画側（`draw_term_mirror`）が触るのは `text_utf8` なので、位置は
 *    UTF-8 バイト位置で渡さないとハイライトがずれる。
 * UTF-8 の多バイト列に 0x80 未満のバイトは現れないため、ASCII トークンの素朴な走査で誤らない。
 * @{
 */

//! 選択肢が多すぎる画面（誤検出）でカーソルが実用にならないので上限を切る。
constexpr size_t kMenuChoiceMax = 64;

bool is_ascii_alnum(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z');
}

//! `)` の直後として妥当か（空白／行末／全角文字）。JP の " -)前ページ" は `)` の後が全角。
bool is_choice_token_terminator(const std::string &text, size_t pos)
{
    if (pos >= text.size()) {
        return true;
    }
    const auto u = static_cast<unsigned char>(text[pos]);
    return u == ' ' || u >= 0x80;
}

//! 1 つのトークン `ESC)` / `X)` / `X/Y)` / `-)` / `スペース)`。keys は `i/e)` で 2 個になる。
struct ChoiceTokenGroup {
    size_t begin{}; //!< キー文字列の開始バイト
    std::vector<std::pair<size_t, size_t>> key_spans; //!< 各キー文字の (開始, 長さ)
    std::vector<int> keys; //!< 各キーの実キーコード
};

/*!
 * @brief 行頭または空白の直後から始まる選択肢トークンを 1 件読む。
 * @param text UTF-8 のミラー 1 行
 * @param p 走査開始バイト位置
 * @return 読めたらトークン。読めなければ nullopt
 */
tl::optional<ChoiceTokenGroup> parse_choice_token(const std::string &text, size_t p)
{
    static const std::string kEsc = "ESC";
    static const std::string kSpaceWord = _("スペース", "SPACE");
    /*
     * K-37: 日本語版でも `SPACE)` と**英字のまま**書く画面がある
     * （知識メニュー `cmd-io/cmd-knowledge.cpp:64`。店は `スペース)`）。
     * どちらも拾えないと知識メニューのページ送りがカーソルから外れる。
     */
    static const std::string kSpaceWordAscii = "SPACE";

    ChoiceTokenGroup g{};
    g.begin = p;

    auto accept = [&](size_t key_len, int key) {
        if (text.compare(p + key_len, 1, ")") != 0) {
            return false;
        }
        if (!is_choice_token_terminator(text, p + key_len + 1)) {
            return false;
        }
        g.key_spans.emplace_back(p, key_len);
        g.keys.push_back(key);
        return true;
    };

    if (text.compare(p, kEsc.size(), kEsc) == 0 && accept(kEsc.size(), 0x1B)) {
        return g;
    }
    if (text.compare(p, kSpaceWord.size(), kSpaceWord) == 0 && accept(kSpaceWord.size(), ' ')) {
        return g;
    }
    if (text.compare(p, kSpaceWordAscii.size(), kSpaceWordAscii) == 0 && accept(kSpaceWordAscii.size(), ' ')) {
        return g;
    }
    // `i/e)` `w/t)`: 1 トークンで 2 コマンド。キーはそれぞれ独立に積める。
    if (p + 2 < text.size() && is_ascii_alnum(text[p]) && text[p + 1] == '/' && is_ascii_alnum(text[p + 2])) {
        if (text.compare(p + 3, 1, ")") == 0 && is_choice_token_terminator(text, p + 4)) {
            g.key_spans.emplace_back(p, 1);
            g.keys.push_back(static_cast<unsigned char>(text[p]));
            g.key_spans.emplace_back(p + 2, 1);
            g.keys.push_back(static_cast<unsigned char>(text[p + 2]));
            return g;
        }
        return tl::nullopt;
    }
    if (p < text.size() && (is_ascii_alnum(text[p]) || text[p] == '-')) {
        if (accept(1, static_cast<unsigned char>(text[p]))) {
            return g;
        }
    }
    return tl::nullopt;
}

//! 1 行分のトークンを読み、各トークンの表示範囲（次のトークン直前まで）も決める。
std::vector<ChoiceTokenGroup> scan_choice_tokens(const std::string &text)
{
    std::vector<ChoiceTokenGroup> groups;
    for (size_t p = 0; p < text.size(); ++p) {
        if (p != 0 && text[p - 1] != ' ') {
            continue;
        }
        if (auto g = parse_choice_token(text, p)) {
            groups.push_back(std::move(*g));
        }
    }
    return groups;
}

/*!
 * @brief トークン開始位置から選択肢の**表示範囲**（ハイライトを敷く範囲）の終端を求める。
 * @details 終端は「次のトークンの直前」まで。ただしコアは同じ行の別の桁へ
 * 値段（`%9d`）や `手持ちのお金:` を置くので、**空白 3 つ以上の並び**が来たらそこで切る。
 * ラベル自身に空白 3 連は現れない（`src/market/building-service.cpp:109` の
 * `" %c) %s %s"` も 1 個ずつ）。切らないと ` ESC) 建物を出る` の枠が所持金まで伸びる。
 */
size_t choice_span_end(const std::string &text, const std::vector<ChoiceTokenGroup> &groups, size_t index)
{
    const size_t begin = groups[index].begin;
    size_t end = (index + 1 < groups.size()) ? groups[index + 1].begin : text.size();
    const size_t gap = text.find("   ", begin);
    if (gap != std::string::npos && gap < end) {
        end = gap;
    }
    // K-37: 区切りの `,` は枠に含めない（`'l'全て,` の形が呪術の中断プロンプトに出る）。
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == ',')) {
        --end;
    }
    return end;
}

//! 先頭の空白を読み飛ばした位置を返す（行頭トークン判定用）。
size_t first_non_space(const std::string &text)
{
    size_t i = 0;
    while (i < text.size() && text[i] == ' ') {
        ++i;
    }
    return i;
}

bool line_starts_with(const std::string &text, const std::string &needle)
{
    return text.compare(first_non_space(text), needle.size(), needle) == 0;
}

/*!
 * @brief 行 0 が `(持ち物:b-c,...)` 形式のアイテム選択プロンプトなら、選べる文字の範囲を返す。
 * @details `src/store/store.cpp:183`（`(商品:a-l, ESCで中断) ...`）と
 * `get_item` 系のプロンプトが同じ形。**この形以外は letter を積まない**
 * （`i` の持ち物一覧のように「次の 1 キーがそのままコマンドになる」画面で
 * 誤って文字を積むと別コマンドが走るため）。
 */
bool parse_letter_range_prompt(const std::string &text, char &lo, char &hi)
{
    if (text.empty() || text[0] != '(') {
        return false;
    }
    const size_t close = text.find(')', 1);
    const size_t limit = (close == std::string::npos) ? text.size() : close;
    for (size_t i = 1; i + 2 < limit; ++i) {
        if (text[i + 1] != '-') {
            continue;
        }
        if (!is_ascii_alnum(text[i]) || !is_ascii_alnum(text[i + 2])) {
            continue;
        }
        lo = text[i];
        hi = text[i + 2];
        return true;
    }
    return false;
}

/*!
 * @brief K-20: コアが自前で描いているカーソル `》`（EN は `"> "`）を探す。
 * @details `use_menu` が真のとき、コアは選択行の頭にこの印を書く。書き手は 1 箇所ではなく
 * コマンドメニュー（`input-key-requester.cpp:137`）・持ち物／装備
 * （`display-inventory.cpp:92` / `main-window-equipments.cpp:97`）・床
 * （`object-scanner.cpp:138`）・呪文（`spell-info.cpp:290`）・特殊能力
 * （`cmd-racial.cpp:48`）・超能力（`mind-power-getter.cpp:269`）・ペット
 * （`cmd-pet.cpp:617`）と散らばっているが、**印は全部これ 1 種類**である。
 *
 * これを見つけることには 2 つの意味がある。
 * 1. **描画**: UI 側カーソル（K-16）と同じ枠を重ねられる。コアの画面もカーソル選択式に見える。
 * 2. **調停**: 「この画面はコアがカーソルを持っている」の判定そのもの。K-16 は
 *    グローバル `use_menu` で判定していたが、それでは**店のアイテム選択**
 *    （`store.cpp:171` の `input_stock`。コアにカーソルが無い）まで
 *    「コアが持っている」と誤判定して UI 側カーソルが消える。
 *    印の有無で見れば画面ごとに正しく分かれる。
 * @return 見つかったら true（`frame.menu_core_cursor` を埋める）
 */
bool fill_core_cursor(GameFrame &frame)
{
    frame.menu_core_cursor = MenuChoice{ -1, 0, 0, 0, 0, 0 };
    frame.menu_core_cursors.clear();

    static const std::string kCursorMark = _("》", "> ");
    for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        // サブメニューの箱は親メニューと**同じ行に重ねて**描かれるので、親の `》` と
        // 子の `》` が 1 行に 2 つ並ぶ。1 行 1 個で打ち切らず、行内を最後まで走査する。
        size_t p = 0;
        while (p + kCursorMark.size() <= text.size()) {
            if (text.compare(p, kCursorMark.size(), kCursorMark) != 0) {
                ++p;
                continue;
            }
            // 行頭・空白の直後・枠線 `|` の直後だけ（本文中の `> ` を拾わないため）。
            if (p != 0 && text[p - 1] != ' ' && text[p - 1] != '|') {
                ++p;
                continue;
            }
            // 印だけで終わる行（消去済みの残骸）は選択行ではない。
            const size_t after = p + kCursorMark.size();
            if (text.find_first_not_of(' ', after) == std::string::npos) {
                ++p;
                continue;
            }
            /*
             * 枠の右端は「空白 3 連（別の桁）」か「**枠線 `|` の手前**」の早い方。
             * `|` で切らないと、親メニューの枠が同じ行にあるサブメニューの項目まで伸び、
             * 2 つの枠がつながった 1 本の帯に見える。
             * コアは箱を詰めて描くので、枠線の前に空白があるとは限らない（`装備|` `》情報|`）。
             * よって `|` は**原則すべて枠線**と見なし、項目名の中に出る `(|)`
             * （`プレイ記録(|)` のキー表記）だけを除外する。
             */
            size_t end = text.find("   ", after);
            for (size_t q = after; q < text.size(); ++q) {
                if (text[q] != '|' || (q > 0 && text[q - 1] == '(')) {
                    continue;
                }
                end = (end == std::string::npos) ? q : (std::min)(end, q);
                break;
            }
            if (end == std::string::npos) {
                end = text.size();
            }
            while (end > after && text[end - 1] == ' ') {
                --end;
            }
            MenuChoice cursor{ -1, 0, 0, 0, 0, 0 };
            cursor.line_index = static_cast<int>(i);
            cursor.span_begin = static_cast<int>(p);
            cursor.span_len = static_cast<int>(end - p);
            cursor.key_len = 0; // コア側の画面に「決定で積む 1 文字」は無い
            if (frame.menu_core_cursors.empty()) {
                frame.menu_core_cursor = cursor;
            }
            frame.menu_core_cursors.push_back(cursor);
            p = end; // 次の印はこの枠より右から探す
        }
    }
    return !frame.menu_core_cursors.empty();
}

//! 画面種別。判定順がそのまま優先順（店は建物の記号を含むので先に見る）。
enum class MenuChoiceScreen {
    None,
    ItemPrompt, //!< 行 0 が `(x-y ...)` のアイテム選択
    Store, //!< `コマンド:` ＋ ` ESC) 建物から出る`
    Building, //!< `手持ちのお金: ` ＋ ` ESC) 建物を出る`
};

MenuChoiceScreen classify_menu_screen(const GameFrame &frame, int &store_command_line)
{
    store_command_line = -1;

    static const std::string kStoreCommandLabel = _("コマンド:", "You may:");
    static const std::string kGoldLabel = _("手持ちのお金:", "Gold Remaining:");

    bool has_esc_choice = false;
    bool has_gold = false;
    for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        if (line_starts_with(text, "ESC)")) {
            has_esc_choice = true;
        }
        if (line_starts_with(text, kStoreCommandLabel)) {
            store_command_line = static_cast<int>(i);
        }
        if (text.find(kGoldLabel) != std::string::npos) {
            has_gold = true;
        }
    }

    // アイテム選択は店・建物の中からも開く（店の商品一覧はそのまま残る）ので最優先で見る。
    // コアが自前のカーソルを持つ画面は呼び出し前に弾いてある（fill_core_cursor）。
    if (!frame.menu_term_lines.empty() && frame.menu_term_lines.front().source_row == 0) {
        char lo = 0;
        char hi = 0;
        if (parse_letter_range_prompt(frame.menu_term_lines.front().text_utf8, lo, hi)) {
            return MenuChoiceScreen::ItemPrompt;
        }
    }
    if (has_esc_choice && store_command_line >= 0) {
        return MenuChoiceScreen::Store;
    }
    if (has_esc_choice && has_gold) {
        return MenuChoiceScreen::Building;
    }
    return MenuChoiceScreen::None;
}

//! 1 行分のトークンを `frame.menu_choices` へ積む。
void emit_choices_for_line(GameFrame &frame, size_t line_index, const std::vector<ChoiceTokenGroup> &groups)
{
    const std::string &text = frame.menu_term_lines[line_index].text_utf8;
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        const auto &g = groups[gi];
        const size_t span_end = choice_span_end(text, groups, gi);
        for (size_t ki = 0; ki < g.keys.size(); ++ki) {
            if (frame.menu_choices.size() >= kMenuChoiceMax) {
                return;
            }
            MenuChoice choice{};
            choice.line_index = static_cast<int>(line_index);
            choice.span_begin = static_cast<int>(g.begin);
            choice.span_len = static_cast<int>(span_end - g.begin);
            choice.key_begin = static_cast<int>(g.key_spans[ki].first);
            choice.key_len = static_cast<int>(g.key_spans[ki].second);
            choice.key = g.keys[ki];
            frame.menu_choices.push_back(choice);
        }
    }
}

/*!
 * @brief アイテム選択プロンプト: 行頭トークンが `lo` から**連番で続く**行だけを拾う。
 * @details 店で購入すると商品一覧（`a)` から連番・列 0）とコマンド行（`p)` `d)` 等）が
 * 同時に画面へ出る。連番であることを条件にすると商品一覧だけが残る。
 */
void collect_item_prompt_choices(GameFrame &frame)
{
    char lo = 0;
    char hi = 0;
    if (!parse_letter_range_prompt(frame.menu_term_lines.front().text_utf8, lo, hi)) {
        return;
    }

    char expected = lo;
    for (size_t i = 1; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        const size_t head = first_non_space(text);
        auto g = parse_choice_token(text, head);
        if (!g || g->keys.size() != 1 || g->keys[0] != static_cast<int>(static_cast<unsigned char>(expected))) {
            continue;
        }
        const std::vector<ChoiceTokenGroup> one{ *g };
        emit_choices_for_line(frame, i, one);
        if (expected == hi) {
            break;
        }
        expected = (expected == 'z') ? 'A' : static_cast<char>(expected + 1);
    }
}

/*!
 * @name 建物の選択肢が載る Term 行
 * @details `src/market/building-service.cpp:109` が `19 + (i / 2)` 行・`35 * (i % 2)` 列へ
 * 8 個まで、同 144 行が 23 行目へ ` ESC) 建物を出る` を出す。
 * 建物の**サブ画面**（賭博・鑑定など）は同じ画面の上に別の文言を重ねたまま
 * `手持ちのお金:` を出し続けるので、行を絞らないとサブ画面の括弧を選択肢と誤認する。
 * @{
 */
constexpr int kBuildingChoiceRowMin = 19;
constexpr int kBuildingChoiceRowMax = 23;
/*! @} */

/*!
 * @brief K-23: birth 画面（種族・職業・性格・魔法領域・性別・オートローラー）のカーソル。
 * @details コアはこれらの画面で**色だけ**で選択中の項目を示す
 * （`c_put_str(TERM_YELLOW, ...)`。直前の項目は `TERM_WHITE` に戻す。
 * `birth-select-race.cpp:46,76` / `birth-select-class.cpp:90` /
 * `birth-select-personality.cpp:74` / `birth-select-realm.cpp:97` /
 * `birth-wizard.cpp:113` / `auto-roller.cpp:245,248,474,477`）。
 * 方向キーと Enter はコアが自分で処理する（`interpret_race_select_key_move` ほか）ので、
 * **UI がやることは「見えるようにする」だけ**。
 * @return 見つかったら true
 */
bool fill_birth_cursor(GameFrame &frame)
{
    /*
     * K-39: **選択画面であることを先に確かめる。** birth の「黄色＝選択中」は
     * 選択画面だけの約束で、同じ pre_game_menu でも**キャラクターシート**では
     * 黄色は「良い能力値」（`魔法防御 : 41-良い`）を意味する。名前入力や生い立ち編集は
     * シートの上に出るので、絞らないと関係ない行に枠が出るうえ、
     * `suppress_meaningless_term_cursor` が「カーソルが他にある」と判断して
     * **文字入力のキャレットまで消してしまう**（2026-07-29 の実機で踏んだ）。
     *
     * ロール確認は `collect_bracket_prompt_choices` が先に拾うのでここには来ない。
     */
    static const char *const kSelectSignatures[] = {
        _("を選んで下さい", "Choose a"), // 性別・種族・職業・性格・魔法領域・元素領域
        _("で項目選択", "for Select"), // オートローラー（能力値／体格・地位）
    };
    bool is_select_screen = false;
    for (const auto &line : frame.menu_term_lines) {
        for (const char *const signature : kSelectSignatures) {
            if (line.text_utf8.find(signature) != std::string::npos) {
                is_select_screen = true;
                break;
            }
        }
        if (is_select_screen) {
            break;
        }
    }
    if (!is_select_screen) {
        return false;
    }

    for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const TermMirrorLine &line = frame.menu_term_lines[i];
        if (line.highlight_len <= 0) {
            continue;
        }
        frame.menu_core_cursor.line_index = static_cast<int>(i);
        frame.menu_core_cursor.span_begin = line.highlight_begin;
        frame.menu_core_cursor.span_len = line.highlight_len;
        frame.menu_core_cursor.key_len = 0; // 下線は引かない
        frame.menu_core_cursors.assign(1, frame.menu_core_cursor);
        return true;
    }
    return false;
}

/*!
 * @brief K-24: `['r' 次の数値, 'h' 生い立ちを表示, Enter この数値に決定]` 形式の行を選択肢にする。
 * @details 能力値のロール確認（`birth-wizard.cpp:436` `display_auto_roller_result`）は
 * **`'r'` のような引用符付きの 1 文字**でコマンドを示す。K-16 の `x)` 形式ではないので
 * そのままでは拾えず、パッドから「振り直し」を選べなかった（人間の指摘）。
 *
 * 判定は厳しくする。**行頭が `[`・行末が `]`・引用符付きの英字が 1 つ以上**の 3 条件。
 * birth のほとんどの画面に出る
 * `キャラクターを作成します。('S'やり直す, 'Q'終了, '?'ヘルプ)` も同じ引用符形式だが、
 * こちらは `(` で始まるので当たらない。**当たると `'Q'`（終了）を積める選択肢にしてしまう。**
 * `pause_line` の `[ 何かキーを押して下さい ]` は引用符付きの英字が無いので当たらない。
 *
 * `Enter` も選択肢に含める。含めないと「決定」がカーソルから外れ、
 * A ボタンが常にカーソル下の文字（既定では `'r'`＝振り直し）になってしまう。
 * @return 1 件でも拾えたら true
 */
bool collect_bracket_prompt_choices(GameFrame &frame)
{
    static const std::string kEnterWord = "Enter";

    for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        const size_t head = first_non_space(text);
        if (head >= text.size() || text[head] != '[') {
            continue;
        }
        size_t tail = text.size();
        while (tail > head && text[tail - 1] == ' ') {
            --tail;
        }
        if (tail == head || text[tail - 1] != ']') {
            continue;
        }

        // まず「引用符付きの英字」を数える。1 つも無ければこの行は対象外。
        std::vector<std::pair<size_t, int>> tokens; // (開始バイト, キー)
        for (size_t p = head; p + 2 < tail; ++p) {
            if (text[p] != '\'' || text[p + 2] != '\'') {
                continue;
            }
            if (!is_ascii_alnum(text[p + 1])) {
                continue;
            }
            tokens.emplace_back(p, static_cast<unsigned char>(text[p + 1]));
        }
        if (tokens.empty()) {
            continue;
        }
        const size_t enter_at = text.find(kEnterWord, head);
        if (enter_at != std::string::npos && enter_at < tail) {
            tokens.emplace_back(enter_at, '\r');
        }
        std::sort(tokens.begin(), tokens.end());

        for (size_t t = 0; t < tokens.size(); ++t) {
            if (frame.menu_choices.size() >= kMenuChoiceMax) {
                break;
            }
            const size_t begin = tokens[t].first;
            // 表示範囲は次のトークンの直前まで（`, ` と `]` は削る）。
            size_t end = (t + 1 < tokens.size()) ? tokens[t + 1].first : tail - 1;
            while (end > begin && (text[end - 1] == ' ' || text[end - 1] == ',')) {
                --end;
            }

            MenuChoice choice{};
            choice.line_index = static_cast<int>(i);
            choice.span_begin = static_cast<int>(begin);
            choice.span_len = static_cast<int>(end - begin);
            choice.key = tokens[t].second;
            // 下線は「実際に積む 1 文字」。`'r'` は引用符の中、`Enter` は語そのもの。
            choice.key_begin = static_cast<int>((choice.key == '\r') ? begin : begin + 1);
            choice.key_len = static_cast<int>((choice.key == '\r') ? kEnterWord.size() : 1);
            frame.menu_choices.push_back(choice);
        }
        return !frame.menu_choices.empty();
    }
    return false;
}

/*!
 * @brief K-27: レベルアップ時の「どの能力値を上げますか？」を選択肢にする。
 * @details `check_experience()`（`src/player/player-status.cpp:2840-2873`）は 10 レベルごとに
 * `screen_save()` して `a) 腕力 (現在値 …)` 〜 `f) 魅力 …` を並べ、`inkey()` で
 * **`'a'`〜`'f'` しか受け付けない**（`》` も出さない）。
 * 店・建物・アイテム選択のような手掛かり（`ESC)` / `コマンド:` / `手持ちのお金:` /
 * 行 0 の `(a-f …)`）が 1 つも無いので `classify_menu_screen` は `None` を返し、
 * **カーソルで選べる選択肢が 1 件も作られなかった**（人間の指摘）。
 *
 * 判定はこの画面固有の**プロンプト文**だけに絞る。緩めると他の icky 画面の
 * `x)` まで拾って誤ったキーを積むので、`classify_menu_screen` と同じ作法にする。
 * @return 1 件でも拾えたら true
 */
bool collect_stat_raise_choices(GameFrame &frame)
{
    static const std::string kPrompt = _("どの能力値を上げますか", "Which stat do you want to raise");

    size_t prompt_line = frame.menu_term_lines.size();
    for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        if (frame.menu_term_lines[i].text_utf8.find(kPrompt) != std::string::npos) {
            prompt_line = i;
            break;
        }
    }
    if (prompt_line >= frame.menu_term_lines.size()) {
        return false;
    }

    for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        if (i == prompt_line) {
            continue;
        }
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        /*
         * **1 行につき英字キーの最初の 1 件だけ**にする。行末は
         * `(現在値      3)` のように「空白＋数字＋ `)`」で終わることがあり、
         * そのままでは `3)` が 2 件目の選択肢として拾われる（コアは `'a'`〜`'f'`
         * しか見ないので押しても無反応の、選べるだけの行になる）。
         */
        std::vector<ChoiceTokenGroup> groups;
        for (auto &g : scan_choice_tokens(text)) {
            if (g.keys.size() != 1) {
                continue;
            }
            const int key = g.keys.front();
            if (key < 'a' || key > 'z') {
                continue;
            }
            groups.push_back(std::move(g));
            break;
        }
        emit_choices_for_line(frame, i, groups);
    }
    return !frame.menu_choices.empty();
}

/*!
 * @name K-37: コアにカーソルが無い「名指しの選択画面」
 * @details K-27（レベルアップの能力値選択）で作った作法
 * ——「手掛かりの無い画面は**プロンプト文で名指しして**拾う」——を、
 * 洗い出した残りの画面へ広げる。判定を緩めて「icky 画面の `x)` を全部拾う」ように
 * してはいけない（他の画面で誤ったキーを積む）ので、画面ごとに
 * **文言・行範囲・トークン書式**の 3 つを明示する。
 * @{
 */

//! トークンの書式。画面ごとに使い分ける。
enum ChoiceTokenStyle : unsigned {
    kStylePlain = 1u << 0, //!< `a) ラベル`（店・建物と同じ。`parse_choice_token`）
    kStyleParen = 1u << 1, //!< `(1) ラベル`（知識・マクロ・画面表示・カラー・記録）
    kStyleQuoted = 1u << 2, //!< `'l'全て`（呪術の中断プロンプト）
};

//! `(1)` `(a)` 形式のトークンを 1 件読む。`(` の直後 1 文字がキー。
tl::optional<ChoiceTokenGroup> parse_paren_choice_token(const std::string &text, size_t p)
{
    if (p + 3 > text.size() || text[p] != '(') {
        return tl::nullopt;
    }
    if (!is_ascii_alnum(text[p + 1]) || text[p + 2] != ')') {
        return tl::nullopt;
    }
    if (!is_choice_token_terminator(text, p + 3)) {
        return tl::nullopt;
    }
    ChoiceTokenGroup g{};
    g.begin = p;
    g.key_spans.emplace_back(p + 1, 1);
    g.keys.push_back(static_cast<unsigned char>(text[p + 1]));
    return g;
}

//! `'l'全て` 形式のトークンを 1 件読む（引用符の中の 1 文字がキー）。
tl::optional<ChoiceTokenGroup> parse_quoted_choice_token(const std::string &text, size_t p)
{
    if (p + 3 > text.size() || text[p] != '\'' || text[p + 2] != '\'') {
        return tl::nullopt;
    }
    if (!is_ascii_alnum(text[p + 1])) {
        return tl::nullopt;
    }
    ChoiceTokenGroup g{};
    g.begin = p;
    g.key_spans.emplace_back(p + 1, 1);
    g.keys.push_back(static_cast<unsigned char>(text[p + 1]));
    return g;
}

//! 指定した書式のトークンを 1 行から全部読む。開始位置の条件は `scan_choice_tokens` と同じ。
std::vector<ChoiceTokenGroup> scan_choice_tokens_styled(const std::string &text, unsigned styles)
{
    std::vector<ChoiceTokenGroup> groups;
    for (size_t p = 0; p < text.size(); ++p) {
        if (p != 0 && text[p - 1] != ' ') {
            continue;
        }
        tl::optional<ChoiceTokenGroup> g;
        if ((styles & kStylePlain) != 0) {
            g = parse_choice_token(text, p);
        }
        if (!g && (styles & kStyleParen) != 0) {
            g = parse_paren_choice_token(text, p);
        }
        if (!g && (styles & kStyleQuoted) != 0) {
            g = parse_quoted_choice_token(text, p);
        }
        if (g) {
            groups.push_back(std::move(*g));
        }
    }
    return groups;
}

/*!
 * @brief コアが入力を待っている Term 行。分からなければ -1。
 * @details `menu_term_curs_row` は `menu_term_lines` の**添字**なので、Term の行番号は
 * その行の `source_row` から引く。キャレットを消す前（`suppress_meaningless_term_cursor`
 * より前）に呼ぶこと。
 */
int cursor_term_row(const GameFrame &frame)
{
    const int index = frame.menu_term_curs_row;
    if ((index < 0) || (index >= static_cast<int>(frame.menu_term_lines.size()))) {
        return -1;
    }
    return frame.menu_term_lines[static_cast<size_t>(index)].source_row;
}

/*!
 * @brief 名指しの選択画面 1 件分の素性。
 * @details `row_min`/`row_max` は **Term の行番号**（`TermMirrorLine::source_row`）。
 * 行範囲を切るのは、これらの画面が**建物や店の上に重なって出る**ためである。
 * 例えばトランプタワーの帰還先選択は建物のメニュー（行 19〜23）が背後に残ったままなので、
 * 範囲を切らないと `classify_menu_screen` が `Building` と誤判定したときと同じ結果、
 * つまり**押しても効かない建物のキー**を選択肢にしてしまう。
 */
struct NamedMenuSpec {
    const char *tag; //!< 自己検査用の名前
    const char *signature; //!< この画面を名指しする文言（部分一致）
    unsigned styles; //!< 本体行のトークン書式
    int row_min; //!< 本体行の Term 行（下限）
    int row_max; //!< 同（上限）
    int extra_row; //!< 本体の**後ろに**足す行（プロンプト行）。`< 0` なら無し
    unsigned extra_styles; //!< その行のトークン書式
    /*!
     * @brief 枠を**行の終わりまで**伸ばすか（省略時 false）。
     * @details `choice_span_end` は空白 3 連で枠を切る（同じ行の別の桁に値段や所持金が
     * 来るため）。1 行 1 項目で、かつ**ラベル自身が空白で桁揃えされている**画面
     * （オプションの設定の `(1)     キー入力     オプション`）ではそれが裏目に出て、
     * 枠が `(1)` だけを囲む。そういう画面だけ true にする。
     */
    bool span_whole_line;
    /*!
     * @brief ここ以降でコアが入力を待っていたら「下位のやり取りへ降りている」。`< 0` なら判定しない。
     * @details マクロ・画面表示・カラーの設定は、項目を選ぶと**一覧を消さずに**
     * 下の行へプロンプトを書き足して、そこで入力を待つ
     * （`do_cmd_macros` なら `トリガーキー: ` を行 18 へ。`cmd-macro.cpp:232`）。
     * 一覧が画面に残っているので、放っておくと**もう効かない項目**が選択肢のまま残り、
     * パッドで決定するとその数字が**トリガーキーやマクロ行動の文字列に混ざる**。
     *
     * 見るのは「その行に何か書いてあるか」ではなく**コアのカーソルがどこにあるか**である。
     * `do_cmd_macros` は下位から戻っても**書いた行を消さずに**一覧の入力待ちへ帰る
     * （`clear_from(1)` はキーを受けた**後**に走る。`cmd-macro.cpp:180-183`）ので、
     * 字の有無で見ると戻ったあとも一覧を殺してしまう。カーソルはコアが
     * **いま待っている場所**そのものなので、そちらで見れば取り違えない。
     */
    int sub_prompt_row{ -1 };
};

/*!
 * @details 順に見て**最初に当たったもの**を使う。文言は原則コアの書式そのままの部分文字列。
 *
 * | tag | コア |
 * |-----|------|
 * | `recall_dungeon` | `choose_dungeon`（`spell-kind/spells-world.cpp:365`。帰還・フロアリセット・トランプタワー） |
 * | `tele_town` | `tele_town`（同 264） |
 * | `melee_arena` | `melee_arena_comm`（`market/melee-arena.cpp:47`） |
 * | `monk_stance` | `choose_monk_stance`（`mind/mind-monk.cpp:37`） |
 * | `samurai_stance` | `choose_samurai_stance`（`mind/mind-samurai.cpp:362`） |
 * | `hex_stop` | `SpellHex::stop_spells_with_selection`（`spell-realm/spells-hex.cpp:83`） |
 * | `knowledge` | `do_cmd_knowledge`（`cmd-io/cmd-knowledge.cpp:26`） |
 * | `macros` | `do_cmd_macros`（`cmd-io/cmd-macro.cpp:144`） |
 * | `visuals` | `do_cmd_visuals`（`cmd-visual/cmd-visuals.cpp:82`） |
 * | `colors` | `do_cmd_colors`（`cmd-io/cmd-dump.cpp:65`） |
 * | `diary` | `do_cmd_diary`（`cmd-io/cmd-diary.cpp:108`） |
 *
 * @note 英語版では修行僧と侍の見出しが同じ `Choose Stance:` になる。挙動も同じなので
 * 先に当たった方（`monk_stance`）で処理して構わない。
 */
const NamedMenuSpec kNamedMenus[] = {
    { "recall_dungeon", _("どのダンジョン", "Which dungeon do you"), kStylePlain, 1, 18, -1, 0 },
    { "tele_town", _("どこに行きますか", "Where do you want to go"), kStylePlain, 1, 18, -1, 0 },
    { "melee_arena", _("どれに賭けますか", "Which monster:"), kStylePlain, 1, 18, -1, 0 },
    { "monk_stance", _("どの構えをとりますか", "Choose Stance:"), kStylePlain, 2, 12, -1, 0 },
    { "samurai_stance", _("どの型で構えますか", "Choose Stance:"), kStylePlain, 2, 12, -1, 0 },
    { "hex_stop", _("どの呪文の詠唱を中断しますか", "Which spell do you stop casting"), kStylePlain, 1, 14, 0, kStylePlain | kStyleQuoted },
    /*
     * `(1)` 形式の画面は `kStylePlain` も一緒に立てる。**ミラーが行頭の `(` を落とすことがある**
     * ためである（`find_menu_content_bounds` は「基準から変化したセル」だけを採るので、
     * 背後の地図にたまたま同じ文字があるとその 1 セルが空白になる。2026-07-29 の実機で
     * マクロの設定の `(0) マクロ行動の入力` が ` 0) マクロ行動の入力` になり、
     * Paren だけでは最後の 1 項目が選べなかった）。
     * `(1)` のままでも二重には拾わない。Plain のトークン開始は「行頭か空白の直後」で、
     * `1` の直前は `(` なので当たらない。
     */
    { "knowledge", _("現在の知識を確認する", "Display current knowledge"), kStyleParen | kStylePlain, 4, 23, -1, 0 },
    /*
     * 下の 3 つだけ `sub_prompt_row` を持つ。行番号はコアの `prt` の実引数そのもの。
     * | 画面 | 一覧の入力待ちの行 | 降りた先の入力待ちの行 |
     * |------|--------------------|------------------------|
     * | マクロ（`cmd-macro.cpp:164-175`） | 0（`msg_print`） | 18・20 |
     * | 画面表示（`cmd-visuals.cpp:68-80`） | 15 | 17・22 |
     * | カラー（`cmd-dump.cpp:73-77`） | 8 | 10・14 |
     * ほかの画面は下位のやり取りで `term_clear()` するので一覧ごと消え、
     * 見出しが見つからなくなる（＝この表に当たらない）。判定は要らない。
     */
    { "macros", _("[ マクロの設定 ]", "Interact with Macros"), kStyleParen | kStylePlain, 3, 14, -1, 0, false, 16 },
    { "visuals", _("[ 画面表示の設定 ]", "Interact with Visuals"), kStyleParen | kStylePlain, 2, 14, -1, 0, false, 17 },
    { "colors", _("[ カラーの設定 ]", "Interact with Colors"), kStyleParen | kStylePlain, 3, 7, -1, 0, false, 10 },
    { "diary", _("[ 記録の設定 ]", "[ Play Record ]"), kStyleParen | kStylePlain, 3, 10, -1, 0 },
    /*
     * K-43: オプションの設定（最上位。`do_cmd_options`。`cmd-io/cmd-gameoption.cpp:444`）。
     * **ここだけはコアにカーソルを譲れない。** 各ページ（`do_cmd_options_aux`）は
     * `'8'` / `'2'` でも動くが、この画面の移動は `SKEY_UP` / `SKEY_DOWN` だけで、
     * SKEY はマクロトリガ（`^_x48\r` の形）から作られる。SDL2 経路には
     * トリガ表（`lib/pref/pref-<ANGBAND_SYS>.prf`。`ANGBAND_SYS` は既定の `xxx` で
     * 該当ファイルが無い）が無いので、**SKEY は 1 度も発生しない**。
     * しかも下を押すと `'2'`（＝マップ画面オプションのキー）が一致して
     * **別のページが開く**。よって ui がカーソルを持ち、決定でその項目のキーを積む。
     * 項目は `(%c)` を `toupper` して描かれる（`(R)` `(P)` …）が、コアは
     * `tolower` で照合し switch も大文字を持っているので、大文字のまま積んでよい。
     */
    { "option_root", _("[ オプションの設定 ]", "Game options"), kStyleParen | kStylePlain, 3, 19, -1, 0, true },
};

//! @brief 表に載っている画面なら選択肢を作る。
bool collect_named_menu_choices(GameFrame &frame)
{
    for (const NamedMenuSpec &spec : kNamedMenus) {
        const std::string signature = spec.signature;
        size_t signature_line = frame.menu_term_lines.size();
        for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
            if (frame.menu_term_lines[i].text_utf8.find(signature) != std::string::npos) {
                signature_line = i;
                break;
            }
        }
        if (signature_line >= frame.menu_term_lines.size()) {
            continue;
        }

        /*
         * 下位のやり取りへ降りているなら、一覧はもう飾りである（`sub_prompt_row` の注）。
         * **画面が何かは分かった**ので `true` を返して切り上げる。`false` で下へ流すと
         * `classify_menu_screen` が店や建物と誤判定して、まるで無関係なキーを積む。
         */
        if ((spec.sub_prompt_row >= 0) && (cursor_term_row(frame) >= spec.sub_prompt_row)) {
            return true;
        }

        for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
            if (i == signature_line) {
                continue;
            }
            const int row = frame.menu_term_lines[i].source_row;
            if (row < spec.row_min || row > spec.row_max) {
                continue;
            }
            const std::string &text = frame.menu_term_lines[i].text_utf8;
            emit_choices_for_line(frame, i, scan_choice_tokens_styled(text, spec.styles));
        }

        /*
         * プロンプト行は**本体の後ろ**に積む。画面側の選択カーソル（`hd2d/ui/ui_cursor.cpp`）の初期位置は
         * 先頭なので、先に積むと呪術の中断で「決定」が既定で `'l'`（全部中断）に乗る。
         */
        if (spec.extra_row >= 0) {
            for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
                if (frame.menu_term_lines[i].source_row != spec.extra_row) {
                    continue;
                }
                const std::string &text = frame.menu_term_lines[i].text_utf8;
                emit_choices_for_line(frame, i, scan_choice_tokens_styled(text, spec.extra_styles));
            }
        }

        if (spec.span_whole_line) {
            for (MenuChoice &choice : frame.menu_choices) {
                const std::string &text = frame.menu_term_lines[static_cast<size_t>(choice.line_index)].text_utf8;
                size_t end = text.size();
                while (end > static_cast<size_t>(choice.span_begin) && text[end - 1] == ' ') {
                    --end;
                }
                choice.span_len = static_cast<int>(end) - choice.span_begin;
            }
        }

        if (!frame.menu_choices.empty()) {
            return true;
        }
    }
    return false;
}

/*!
 * @brief K-37: 自動拾いエディタのコマンドメニュー（`autopick/autopick-command-menu.cpp:71`）。
 * @details 行 0 は `(a-x) コマンド:` なので `classify_menu_screen` は `ItemPrompt` を返すが、
 * 項目が `| a) 名前   ^X | ` と**枠線始まり**なので `collect_item_prompt_choices`
 * （行頭トークンだけ見る）では 1 件も拾えなかった。
 *
 * 階層メニューなので、1 行に**親の箱と子の箱が並ぶ**（子は `col0 = 5 + depth * 7` と
 * 右へずれる）。コアが受け付けるのは**いま開いている階層＝いちばん右の箱**の文字だけなので、
 * 行ごとに右端のトークン 1 件だけを採る。左の箱まで拾うと
 * 「選べるのに押しても効かない選択肢」ができる。
 * @return 1 件でも拾えたら true
 */
bool collect_autopick_menu_choices(GameFrame &frame)
{
    static const std::string kCommandLabel = _("コマンド:", "Command:");

    if (frame.menu_term_lines.empty() || frame.menu_term_lines.front().source_row != 0) {
        return false;
    }
    const std::string &head = frame.menu_term_lines.front().text_utf8;
    char lo = 0;
    char hi = 0;
    if (head.find(kCommandLabel) == std::string::npos) {
        return false;
    }
    if (!parse_letter_range_prompt(head, lo, hi) || lo != 'a' || hi < 'a' || hi > 'z') {
        return false;
    }

    // 行ごとに右端のトークン 1 件（＝その行に見えている最も深い箱の項目）。
    std::vector<std::pair<size_t, ChoiceTokenGroup>> picked_lines;
    for (size_t i = 1; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        tl::optional<ChoiceTokenGroup> picked;
        for (size_t p = 2; p < text.size(); ++p) {
            if (text[p - 1] != ' ' || text[p - 2] != '|') {
                continue;
            }
            auto g = parse_choice_token(text, p);
            if (!g || g->keys.size() != 1) {
                continue;
            }
            const int key = g->keys.front();
            if (key < static_cast<int>(lo) || key > static_cast<int>(hi)) {
                continue;
            }
            picked = std::move(g); // より右の箱＝より深い階層
        }
        if (picked) {
            picked_lines.emplace_back(i, std::move(*picked));
        }
    }

    /*
     * 子メニューが開いている間、**親の項目はコアが受け付けない**
     * （`do_command_menu` は再帰し、親の `inkey()` は止まっている）。
     * 子の箱は `row0 = 1 + depth * 3` と下へずれ、キーは必ず `a` から振り直される。
     * よって「最後に出てくる `a`」より上の行は親のものなので落とす。
     * 子が開いていなければ `a` は最初の行にあるので、全部残る。
     */
    size_t first_kept = 0;
    for (size_t n = 0; n < picked_lines.size(); ++n) {
        if (picked_lines[n].second.keys.front() == 'a') {
            first_kept = n;
        }
    }
    for (size_t n = first_kept; n < picked_lines.size(); ++n) {
        const std::vector<ChoiceTokenGroup> one{ picked_lines[n].second };
        emit_choices_for_line(frame, picked_lines[n].first, one);
    }
    return !frame.menu_choices.empty();
}

/*!
 * @brief K-37: オプション**ページ**のカーソル（印は `》` でも黄色でもなく **`TERM_L_BLUE` の行**）。
 * @details `do_cmd_options_aux`（`cmd-io/cmd-gameoption.cpp:665`）と
 * 自動セーブ・詐欺オプションは、選択中の行だけを `TERM_L_BLUE` で描き、`'8'` / `'2'` と
 * Enter を**コアが自分で処理する**。つまり操作はもともとできていて、
 * K-16 / K-20 と同じ枠が出ないだけだった。ここでは枠を描くためだけにカーソルを載せる
 * （birth の `fill_birth_cursor` とまったく同じ考え方。あちらは黄色）。
 *
 * **色を見るのはこの 2 種類の画面だけに絞る。** ゲーム中は `TERM_L_BLUE` が別の意味で
 * 多用されるので、画面を名指ししないと誤検出する。
 * @note K-43: 最上位の「オプションの設定」は**ここでは扱わない**。あの画面の移動は
 * `SKEY_UP` / `SKEY_DOWN` だけで、SDL2 経路では SKEY が発生しないためコアのカーソルが
 * 動かない（`kNamedMenus` の `option_root` で ui がカーソルを持つ）。
 * @return 見つかったら true
 */
bool fill_option_cursor(GameFrame &frame)
{
    static const char *const kSignatures[] = {
        _("リターンで次へ", "RET to advance"), // 自動セーブ・詐欺オプション
        _("リターン:次", "RET:next"), // 各オプションページ
    };

    bool is_option_screen = false;
    for (const TermMirrorLine &line : frame.menu_term_lines) {
        for (const char *const signature : kSignatures) {
            if (line.text_utf8.find(signature) != std::string::npos) {
                is_option_screen = true;
                break;
            }
        }
        if (is_option_screen) {
            break;
        }
    }
    if (!is_option_screen) {
        return false;
    }

    for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const TermMirrorLine &line = frame.menu_term_lines[i];
        if (line.color != TERM_L_BLUE) {
            continue;
        }
        const size_t begin = first_non_space(line.text_utf8);
        size_t end = line.text_utf8.size();
        while (end > begin && line.text_utf8[end - 1] == ' ') {
            --end;
        }
        if (end <= begin) {
            continue;
        }
        frame.menu_core_cursor.line_index = static_cast<int>(i);
        frame.menu_core_cursor.span_begin = static_cast<int>(begin);
        frame.menu_core_cursor.span_len = static_cast<int>(end - begin);
        frame.menu_core_cursor.key_len = 0; // 下線は引かない
        frame.menu_core_cursors.assign(1, frame.menu_core_cursor);
        return true;
    }
    return false;
}

/*! @} */

void fill_menu_choices(GameFrame &frame)
{
    frame.menu_choices.clear();
    frame.menu_core_cursor = MenuChoice{ -1, 0, 0, 0, 0, 0 };
    frame.menu_core_cursors.clear();
    if (!frame.menu_open || frame.menu_term_lines.empty()) {
        return;
    }
    if (frame.pre_game_menu) {
        // K-24: ロール確認のような `['r' …]` 行が**最優先**。キャラクターシートに
        // 黄色が混ざっていても、こちらが出ている画面ではこちらが正しい。
        if (collect_bracket_prompt_choices(frame)) {
            return;
        }
        // birth の選択画面。コアが自前でカーソルを持つので枠を描くだけ（K-23）。
        fill_birth_cursor(frame);
        return;
    }

    // K-20: コアが自前でカーソルを持っている画面（`》`）では UI 側カーソルを作らない。
    // 描画はする（`menu_core_cursor`）ので、見た目はどちらも同じカーソル選択式になる。
    if (fill_core_cursor(frame)) {
        return;
    }

    // K-37: オプション画面はコアが色でカーソルを持っている。枠を描くだけで譲る。
    if (fill_option_cursor(frame)) {
        return;
    }

    // K-27: レベルアップの能力値選択。手掛かりが無く classify では None になる画面なので、
    // プロンプト文で名指しして先に拾う。
    if (collect_stat_raise_choices(frame)) {
        return;
    }

    /*
     * K-37: 名指しの選択画面（帰還先ダンジョン・町テレポート・闘技場の賭け・
     * 修行僧／侍の構え・呪術の中断・知識・マクロ・画面表示・カラー・記録）。
     * **classify より先**に見る。これらは建物や店の上に重なって出るので、
     * 後回しにすると `Store` / `Building` と誤判定され、背後の建物のキーを積んでしまう。
     */
    if (collect_named_menu_choices(frame)) {
        return;
    }

    // K-37: 自動拾いエディタ。行 0 が `(a-x) コマンド:` なので ItemPrompt より先に見る。
    if (collect_autopick_menu_choices(frame)) {
        return;
    }

    int store_command_line = -1;
    switch (classify_menu_screen(frame, store_command_line)) {
    case MenuChoiceScreen::ItemPrompt:
        collect_item_prompt_choices(frame);
        return;
    case MenuChoiceScreen::Store:
        // 商品一覧（行 6〜）の `a)` はこの画面では押しても効かない。
        // コアが毎ループ描く `コマンド:` 行より下だけがコマンドである。
        for (size_t i = static_cast<size_t>(store_command_line) + 1; i < frame.menu_term_lines.size(); ++i) {
            emit_choices_for_line(frame, i, scan_choice_tokens(frame.menu_term_lines[i].text_utf8));
        }
        return;
    case MenuChoiceScreen::Building:
        for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
            const int row = frame.menu_term_lines[i].source_row;
            if (row < kBuildingChoiceRowMin || row > kBuildingChoiceRowMax) {
                continue;
            }
            emit_choices_for_line(frame, i, scan_choice_tokens(frame.menu_term_lines[i].text_utf8));
        }
        return;
    case MenuChoiceScreen::None:
    default:
        return;
    }
}

/*!
 * @brief K-37: `[S]弾, [A]矢, [B]クロスボウの矢 :` 形式の 1 行コマンドプロンプトを選択肢へ。
 * @details 矢弾の作成（`create_ammo`＝`src/mind/mind-archer.cpp:92`）は
 * `screen_save()` を通らないので icky にならず、**Term ミラーにも MenuOverlay にも出ない**。
 * `y/n` とまったく同じ事情なので同じ器（`PromptBar`）に載せる。
 *
 * 判定は厳しくする。行 0 はメッセージ行でもあるので、緩めると通常の表示中にキーを飲み込む。
 * 1. 行末が `:` か `?`
 * 2. `[英数字]` の形が 1 つ以上ある
 * 3. **行内のすべての `[` がその形**であること
 *
 * 3 を外してはいけない。防具の `[4,+0]` を含むアイテム名や `[ 何かキーを押して下さい ]`、
 * `よろしいですか？[Y/n]` は 3 で落ちる（`[y/n]` 系は呼び出し元で先に処理済み）。
 * @return プロンプトなら true
 */
bool parse_bracket_command_prompt(const std::string &text, PromptBar &out)
{
    if (text.empty()) {
        return false;
    }
    const char tail = text.back();
    if (tail != ':' && tail != '?') {
        return false;
    }

    std::vector<size_t> tokens; //!< 各 `[` の位置
    for (size_t p = 0; p < text.size(); ++p) {
        if (text[p] != '[') {
            continue;
        }
        if (p + 2 >= text.size() || !is_ascii_alnum(text[p + 1]) || text[p + 2] != ']') {
            return false;
        }
        tokens.push_back(p);
    }
    if (tokens.empty() || tokens.size() > 8) {
        return false;
    }

    static const std::string kOr = " or ";
    for (size_t t = 0; t < tokens.size(); ++t) {
        const size_t begin = tokens[t];
        // ラベルは「次のトークン」「`,`」「` or `」のいちばん早いところまで。
        size_t end = (t + 1 < tokens.size()) ? tokens[t + 1] : text.size();
        if (const size_t comma = text.find(',', begin); comma != std::string::npos && comma < end) {
            end = comma;
        }
        if (const size_t or_at = text.find(kOr, begin); or_at != std::string::npos && or_at < end) {
            end = or_at;
        }
        while (end > begin && (text[end - 1] == ' ' || text[end - 1] == ':' || text[end - 1] == '?')) {
            --end;
        }

        PromptChoice choice{};
        choice.label_utf8 = text.substr(begin, end - begin);
        choice.key = static_cast<unsigned char>(text[begin + 1]);
        // 枠を重ねる位置は**画面に出ている文字そのもの**（`y/n` と同じ作法）。
        choice.begin = static_cast<int>(begin + 1);
        choice.len = 1;
        out.choices.push_back(choice);
    }
    out.text_utf8 = text;
    out.footer_utf8 = _("上下:選択  A/Enter:決定  B/ESC:取消", "Up/Down: select  A/Enter: OK  B/ESC: cancel");
    return true;
}

/*!
 * @brief K-20: Term 行 0 の `y/n` プロンプトを「はい／いいえ」の選択肢へ持ち上げる。
 * @details `input_check_strict()`（`src/core/asking-player.cpp:220-286`）は
 *   - 本文の後ろに `[y/n]` / `[Y/n]` / `[(O)k/(C)ancel]` を足して `prt(buf, 0, 0)` で行 0 へ出し
 *   - `'y'`/`'n'`（または `'o'`/`'c'`）と ESC しか受け付けない（`'\r'` は `bell()`）
 *   - `screen_save()` を**通らない**ので icky にならず、MenuOverlay にも Term ミラーにも出ない
 * という作りなので、ここを拾わないと「はい」と答える手段がカーソルには無い。
 *
 * 判定は**行末が上記 3 種のいずれかに一致すること**だけに絞る。行 0 はメッセージ行でもあり、
 * 緩めると通常のメッセージ表示中にキーを飲み込んでしまう（誤コマンドより性質が悪い）。
 * 応答後にコアが `prt("", 0, 0)` で行 0 を消す（同 288 行）ので、居座りも起きない。
 */
bool parse_prompt_line(const std::string &raw, PromptBar &out)
{
    out = PromptBar{};

    std::string text = raw;
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    if (text.empty()) {
        return false;
    }

    /*
     * `yes_char` / `no_char` は**画面に出ている文字**。枠を重ねる位置をここから探し、
     * そのまま KeyQueue へ積むキーにもする。`input_check_strict` は大文字小文字の
     * どちらも受ける（`asking-player.cpp:266-278`）ので、`(O)k` は `'O'` を積んでよい。
     * 各文字は接尾辞の中で 1 度しか出てこない（`(C)ancel` の小文字 `c` とは別字）。
     */
    /*
     * **2 択とは限らない**（2026-08-10。こう気づいた——「アイテムを破壊するときの Y/N の
     * 選択がコントローラーでできない」）。アイテム破壊は `[y/n/Auto]` の **3 択**で、
     * 2 択の表に載っていなかったので**プロンプトとして認識されず、パッドで答えられなかった**
     * （`cmd-destroy.cpp:53`。'y' / 'n' / 'A' と ESC を直に `inkey()` で読む）。
     *
     * 表は**画面に出る文字列そのもの**で引く。緩い解釈（`[` と `/` があれば選択肢とみなす）は
     * 誤爆する——`[%s, Line %d/%d]`（文書表示の見出し）や
     * `[キー:(RET/スペース)↓ …]`（ヘルプの脚注）が引っかかる。**書いてある形だけを拾う。**
     */
    struct PromptOption {
        const char *label;
        char key; //!< 積むキー。**接尾辞の中で最初に現れる位置**へ枠を重ねる
    };
    struct PromptPattern {
        const char *suffix;
        /*!
         * @brief 末尾ではなく**この文字列で始まる `[`** を探すか。
         * @details ペットを放すときの `[Yes/No/Unnamed (3体)]` は括弧の中に頭数が入るので、
         * 行末との照合ができない（`cmd-pet.cpp:104`）。
         */
        bool by_prefix;
        PromptOption options[4]; //!< `key == 0` で終端
    };
    static const PromptPattern kPatterns[] = {
        { "[y/n]", false, { { _("はい", "Yes"), 'y' }, { _("いいえ", "No"), 'n' } } },
        { "[Y/n]", false, { { _("はい", "Yes"), 'Y' }, { _("いいえ", "No"), 'n' } } },
        //! クイックスタートの問い（`birth/quick-start.cpp:42`）。**行 0 ではない**（下の走査で拾う）。
        { "[y/N]", false, { { _("はい", "Yes"), 'y' }, { _("いいえ", "No"), 'N' } } },
        //! アイテム破壊（`cmd-destroy.cpp:53`）。'A' は「以後自動で壊す」（自動拾い登録）。
        { "[y/n/Auto]", false,
            { { _("はい", "Yes"), 'y' }, { _("いいえ", "No"), 'n' },
                { _("以後自動で", "Auto"), 'A' } } },
        { "[(O)k/(C)ancel]", false, { { "OK", 'O' }, { _("キャンセル", "Cancel"), 'C' } } },
        //! 記念撮影（`cmd-process-screen.cpp:375`）。
        { "[(y)es/(h)tml/(n)o]", false,
            { { _("テキスト", "Text"), 'y' }, { "HTML", 'h' }, { _("いいえ", "No"), 'n' } } },
        //! ペットを放す（`cmd-pet.cpp:104`）。頭数が入るので**前方一致**で引く。
        { "[Yes/No/Unnamed", true,
            { { _("はい", "Yes"), 'Y' }, { _("いいえ", "No"), 'N' },
                { _("名前なしを全部", "Unnamed"), 'U' } } },
    };

    for (const auto &pattern : kPatterns) {
        const std::string suffix = pattern.suffix;
        size_t suffix_begin = std::string::npos;
        if (pattern.by_prefix) {
            const size_t at = text.rfind(suffix);
            //! 前方一致でも**行末が `]`** であることは要る（本文中の言及を拾わない）。
            if ((at != std::string::npos) && (text.back() == ']')) {
                suffix_begin = at;
            }
        } else if ((text.size() >= suffix.size())
            && (text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0)) {
            suffix_begin = text.size() - suffix.size();
        }
        if (suffix_begin == std::string::npos) {
            continue;
        }

        //! 枠を重ねる位置は**行の中の実際の文字**。接尾辞の先頭から探す（前方一致でも同じ）。
        PromptBar built{};
        bool ok = true;
        for (const auto &option : pattern.options) {
            if (option.key == '\0') {
                break;
            }
            const size_t at = text.find(option.key, suffix_begin);
            if (at == std::string::npos) {
                ok = false; // 表の書き間違い。積むより出さない方が安全
                break;
            }
            built.choices.push_back({ option.label, option.key, static_cast<int>(at), 1 });
        }
        if (!ok || built.choices.size() < 2) {
            continue;
        }
        built.text_utf8 = text;
        out = built;
        return true;
    }

    // K-37: `[S]弾, [A]矢 :`（矢弾の作成）のような 1 行コマンドプロンプト。
    return parse_bracket_command_prompt(text, out);
}

/*!
 * @brief K-22: Term 行 0 が数値入力（`いくつですか (1-4): 1`）なら最小・最大・現在値を読む。
 * @details 判定は **`(数字-数字): ` があり、その後ろが数字だけ（または空）**であること。
 * `input_quantity` は `_("いくつですか (1-%d): ", "Quantity (1-%d): ")`、
 * `input_integer` は `<文言>(%d-%d): `（`asking-player.cpp:384`）なので、
 * 両方この形に必ず入る。アイテム選択の `(持ち物:b-c, ...)` は
 * **括弧内が英字**で、しかも `): ` の後ろが日本語なので当たらない。
 * @return 数値入力なら true
 */
bool parse_numeric_input_line(const std::string &raw, NumericInput &out)
{
    out = NumericInput{};

    // **末尾の空白を先に落とさない。** `prt` は行末まで空白で埋めるので、値を全部消した
    // 状態の行は `... (1-4): ` で終わる。先に trim すると `): ` の照合が外れる。
    const std::string &text = raw;

    for (size_t open = 0; open + 1 < text.size(); ++open) {
        if (text[open] != '(') {
            continue;
        }
        size_t p = open + 1;
        const size_t min_begin = p;
        while (p < text.size() && text[p] >= '0' && text[p] <= '9') {
            ++p;
        }
        if (p == min_begin || p >= text.size() || text[p] != '-') {
            continue;
        }
        ++p;
        const size_t max_begin = p;
        while (p < text.size() && text[p] >= '0' && text[p] <= '9') {
            ++p;
        }
        if (p == max_begin) {
            continue;
        }
        // `):` で閉じること（`残り (1-4) 個` のような表記は数値入力ではない）。
        if (text.compare(p, 2, "):") != 0) {
            continue;
        }
        const size_t prompt_end = p + 2;
        size_t rest_begin = prompt_end;
        while (rest_begin < text.size() && text[rest_begin] == ' ') {
            ++rest_begin;
        }
        std::string rest = text.substr(rest_begin);
        while (!rest.empty() && rest.back() == ' ') {
            rest.pop_back();
        }
        // 編集中のバッファは数字だけ（空＝全部消した状態）。英字を打った場合は譲る。
        for (const char c : rest) {
            if (c < '0' || c > '9') {
                return false;
            }
        }

        // 桁あふれは扱わない（コアの len は 6 桁程度）。
        if (p - max_begin > 9 || rest.size() > 9) {
            return false;
        }

        out.active = true;
        out.prompt_utf8 = text.substr(0, prompt_end);
        // `atoi` は数字以外で止まるので、開始位置から切り出すだけでよい（"1-4): 1" → 1）。
        out.min = std::atoi(text.substr(min_begin).c_str());
        out.max = std::atoi(text.substr(max_begin).c_str());
        out.value = rest.empty() ? out.min : std::atoi(rest.c_str());
        out.digits = static_cast<int>(std::to_string((std::max)(out.max, 1)).size());
        out.text_len = static_cast<int>(rest.size());
        // 値の位置（この行の中でのバイト位置）。ミラー行に対して呼んだときはそのまま
        // ハイライトの位置になる。
        out.value_begin = static_cast<int>(rest_begin);
        out.value_len = static_cast<int>(rest.size());
        return true;
    }
    return false;
}

//! Term 行 0 を読んで `parse_prompt_line` / `parse_numeric_input_line` へ渡す（実画面側の入口）。
void fill_row0_prompts(GameFrame &frame)
{
    frame.prompt = PromptBar{};
    frame.numeric = NumericInput{};
    /*
     * K-24: birth 中も対象にする。`よろしいですか？[Y/n]`（職業・性格を選んだ後の確認）は
     * `input_check_strict` なので `'\r'` を撥ねる。**パッドでは「はい」と答えられなかった。**
     * 判定は行末一致だけの厳しい規則なので、タイトルや死亡画面で誤爆しない。
     */
    if (game_term == nullptr || game_term->scr == nullptr) {
        return;
    }
    const auto [width, height] = term_get_size();
    if (width <= 0 || height <= 0) {
        return;
    }

    std::string sys_line;
    for (int x = 0; x < width; ++x) {
        append_term_cell(sys_line, x, 0, width, true);
    }
    const std::string row0 = to_utf8_line(sys_line);
    bool is_prompt = parse_prompt_line(row0, frame.prompt);
    if (!is_prompt && !parse_numeric_input_line(row0, frame.numeric)) {
        /*
         * **行 0 に無いプロンプトもある。**クイックスタートの問いは `put_str(…, 14, 10)`
         * で 14 行目に出る（`birth/quick-start.cpp:42`）ので、行 0 だけを見ていると
         * パッドで答えられない（2026-08-10 に気づいた「ほかのメニュー内でも同様に」）。
         *
         * **広げても誤爆しにくいのは、表が「画面に出る文字列そのもの」で引いているから**
         * である（`[y/N]` のような綴りは問い以外に出てこない）。緩い解釈にしていたら
         * この走査は危険だった。
         *
         * そのうえで**行 0 が空のときだけ**にしてある。行 0 に何か出ているなら、
         * 待っている入力はそちらのはずで、画面のどこかに残っている古い問いを
         * 拾ってしまう余地を残さない。
         */
        std::string row0_trimmed = row0;
        while (!row0_trimmed.empty() && row0_trimmed.back() == ' ') {
            row0_trimmed.pop_back();
        }
        for (size_t i = 0; row0_trimmed.empty() && (i < frame.menu_term_lines.size()); ++i) {
            if (parse_prompt_line(frame.menu_term_lines[i].text_utf8, frame.prompt)) {
                //! ミラー行そのものを解析したので、桁位置はもうミラー基準になっている。
                frame.prompt.line_index = static_cast<int>(i);
                is_prompt = true;
                break;
            }
        }
        /*
         * **打っている行そのもの**（2026-08-15 に決めた「入力中の表示も欲しい」）。
         *
         * 銘の刻印・検索語のように、`askfor` が**地図の上で**行 0 だけを使う場面がある。
         * この間は icky が立たないので `menu_open` が偽 → ミラーは 1 行も開かず
         * （`fill_menu_term_mirror` の先頭）、`y/n` でも数値でもないので prompt にも
         * 載らない。つまり **ui には「いま何を打っているか」を知る手立てが 1 つも無かった**
         * ——打っても画面が何も変わらないので、確定したのかどうかも分からない
         * （「銘の入力は Enter を 2 回押す」という報告の半分はこれだった）。
         *
         * コアが行 0 に書いた**そのもの**（`銘: !k` のようにプロンプト＋編集中の文字列）を
         * prompt の帯へ渡す。選択肢は 1 つも作らない——ここは選ぶ画面ではないので、
         * カーソル層は素通りし（`handle_prompt` は選択肢が空なら false）、
         * キーは今までどおりコアの `askfor` へ届く。
         *
         * **ミラーが開いている場面では渡さない。**名前入力・生い立ち・自動拾いの
         * エディタは全画面の Term で、行 0 はミラーにそのまま出ている。ここでも渡すと
         * 同じ行が 2 か所に出る。
         */
        if (!is_prompt && frame.menu_term_lines.empty() && !row0_trimmed.empty()
            && frame.text_input_active) {
            frame.prompt.text_utf8 = row0_trimmed;
            frame.prompt.line_index = -1; //!< ミラーには出ていない＝ ui が自分の帯へ出す
            return;
        }
        if (!is_prompt) {
            return; // `y/n` と数値入力は同時に出ない
        }
    }

    /*
     * 元の画面（Term ミラー）に同じ行が出ているなら、そこでの位置を持たせる。
     * ミラーは左端を切り詰めることがある（`find_menu_content_bounds`）ので、
     * **ミラー行そのものを解析し直す**。行 0 の生の桁位置を流用すると枠がずれる。
     */
    for (size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        if (frame.menu_term_lines[i].source_row != 0) {
            continue;
        }
        const std::string &mirror_line = frame.menu_term_lines[i].text_utf8;
        if (is_prompt) {
            PromptBar mirrored{};
            if (parse_prompt_line(mirror_line, mirrored) &&
                mirrored.choices.size() == frame.prompt.choices.size()) {
                frame.prompt.line_index = static_cast<int>(i);
                for (size_t c = 0; c < mirrored.choices.size(); ++c) {
                    frame.prompt.choices[c].begin = mirrored.choices[c].begin;
                    frame.prompt.choices[c].len = mirrored.choices[c].len;
                }
            }
            break;
        }
        NumericInput mirrored{};
        if (parse_numeric_input_line(mirror_line, mirrored)) {
            frame.numeric.line_index = static_cast<int>(i);
            frame.numeric.value_begin = mirrored.value_begin;
            frame.numeric.value_len = mirrored.value_len;
        }
        break;
    }
}

/*!
 * @brief K-20: カーソル操作モードを**コアの入力オプションへ反映**する。
 * @details 「全ての操作をカーソルと決定で」の本体はここ 2 行である。コアは元々
 * メニュー操作の仕組みを持っていて、
 *   - `command_menu`（ゲームオプション。既定 ON）が真なら Enter でコマンドメニュー
 *     （`input-key-requester.cpp:124` `inkey_from_menu`）が開く
 *   - `use_menu`（`io/input-key-requester.h` のグローバル）が真なら、以降の
 *     持ち物・床・呪文・特殊能力・超能力・ペット・ターゲットの選択が
 *     すべてカーソル（`》`）＋ Enter で動く
 * のだが、`use_menu` は `request_command()` の先頭で毎回 false にされ、
 * **コマンドメニューから選んだときだけ** true になる（同 417 行）。
 * つまり「文字キーやパッドでコマンドを出すと、その後の選択は文字入力に戻る」。
 * ここで毎フレーム立て直すことで、**どの経路でコマンドを出してもカーソルが効く**。
 *
 * 立てる場所が capture でよい理由: コアは `inkey()` の待ちループから
 * `TERM_XTRA_EVENT` → `SdlNullTerm::on_event` → capture を回すので、
 * `request_command()` が false にした**後**に必ずここを通る。
 * @note コアの src/ は書き換えない（必守制約 1）。触るのはコアが公開している
 * グローバル変数だけで、値も「コア自身が別経路で入れる値」と同じもの。
 */
void apply_cursor_mode()
{
    if (!sdl_ui_options().cursor_mode_enabled) {
        return; // 従来どおり（文字キー主体）。use_menu はコアに任せる
    }
    if (!AngbandWorld::get_instance().character_generated) {
        return; // タイトル・キャラ作成はコアの別メニュー。触らない
    }
    command_menu = true; // Enter ＝ 操作メニュー
    use_menu = true; // 以降の選択はコアのカーソルで
}

/*!
 * @brief K-49: ボット用 JSON 出力の ON/OFF をコアの `arg_bot_json_output` へ写す。
 * @details 機能メニュー（F10）の切替を毎フレーム反映する。`apply_cursor_mode` と同じ作りで、
 * 触るのはコアが公開しているグローバル変数だけ（`src/` は書き換えない。必守制約 1）。
 * @note コマンドライン `--bot-json-output` / 環境変数 `HENGBAND_BOT_JSON` で入れたときは
 * 設定に関わらず ON（`bot_json_forced()`）。**既定は OFF**で、普通に遊ぶ限り 1 行も書かない。
 */
void apply_bot_json_output()
{
    arg_bot_json_output = bot_json_forced() || sdl_ui_options().bot_json_enabled;
}

/*!
 * @brief 属性を「見て分かる束」へ落とす
 * @details `AttributeType` は 80 種類以上ある。**全部に色を振ると表の保守が破綻し、
 * しかも遊んでいて区別がつかない。** ここで 10 束に寄せ、色そのものは画面側が決める。
 * 表に無いものは物理（既定の落とし先）。
 */
CombatFxElement to_combat_fx_element(AttributeType attribute)
{
    switch (attribute) {
    case AttributeType::FIRE:
    case AttributeType::PLASMA:
    case AttributeType::HOLY_FIRE:
    case AttributeType::HELL_FIRE:
    case AttributeType::LAVA_FLOW:
    case AttributeType::METEOR:
        return CombatFxElement::Fire;
    case AttributeType::COLD:
    case AttributeType::ICE:
        return CombatFxElement::Cold;
    case AttributeType::ELEC:
        return CombatFxElement::Elec;
    case AttributeType::ACID:
        return CombatFxElement::Acid;
    case AttributeType::POIS:
    case AttributeType::NUKE:
    case AttributeType::HUNGRY:
        return CombatFxElement::Poison;
    case AttributeType::DARK:
    case AttributeType::DARK_WEAK:
    case AttributeType::NETHER:
    case AttributeType::ABYSS:
    case AttributeType::VOID_MAGIC:
        return CombatFxElement::Dark;
    case AttributeType::LITE:
    case AttributeType::LITE_WEAK:
    case AttributeType::PSY_SPEAR:
        return CombatFxElement::Light;
    case AttributeType::CHAOS:
    case AttributeType::NEXUS:
    case AttributeType::CONFUSION:
    case AttributeType::TIME:
        return CombatFxElement::Chaos;
    case AttributeType::PSI:
    case AttributeType::PSI_DRAIN:
    case AttributeType::MIND_BLAST:
    case AttributeType::BRAIN_SMASH:
    case AttributeType::DOMINATION:
        return CombatFxElement::Psy;
    default:
        return CombatFxElement::Physical;
    }
}

/*!
 * @brief コアが書き留めた戦闘の見せ場を汲んでフレームへ載せる（設計書 §8）
 * @details **汲んだら消える**（`take_events`）。`capture` は入力待ちの間も 10ms ごとに
 * 回るので、残す作りにすると同じ縁を拾い続けて演出が止まらなくなる。
 */
void fill_combat_fx(GameFrame &frame)
{
    const auto events = CombatFeedback::get_instance().take_events();
    // `HENGBAND_FX_LOG` を指したときだけ。コアが書き留めたものを画面へ渡せているかの確認。
    if (!events.empty() && (std::getenv("HENGBAND_FX_LOG") != nullptr)) {
        std::fprintf(stderr, "[fx] drained %d event(s)\n", static_cast<int>(events.size()));
    }
    frame.combat_fx.clear();
    frame.combat_fx.reserve(events.size());
    for (const auto &event : events) {
        CombatFxEvent fx;
        fx.kind = static_cast<CombatFxKind>(event.kind);
        fx.element = to_combat_fx_element(event.attribute);
        fx.y = static_cast<int16_t>(event.y);
        fx.x = static_cast<int16_t>(event.x);
        fx.src_y = static_cast<int16_t>(event.src_y);
        fx.src_x = static_cast<int16_t>(event.src_x);
        fx.intensity = event.intensity;
        frame.combat_fx.push_back(fx);
    }
}

/*!
 * @brief コアが書き留めた音を汲んでフレームへ載せる。
 * @details `fill_combat_fx` と同じ形——**汲んだら消える**。画面側が
 * `ui_state.audio.sound_events` を立てていなければ待ち行列は常に空なので、
 * ここは毎フレーム呼んでよい。
 */
void fill_sounds(GameFrame &frame)
{
    frame.sounds = take_sound_events();
}

/*!
 * @brief リアルタイムモードの設定を `RealtimeClock` へ写す
 * @details `apply_bot_json_output` と同じ作りで毎フレーム反映する。設定はセーブではなく
 * `sdl2_ui_options.cfg` にあるので、コアのオプションビットには触らない。
 * @note **スクリプト注入（K-38）とボットが動いているときは強制 OFF**（設計書 §12 罠 4）。
 * 締切で勝手に行動機会が流れると、スクリプトのキーと実際のターンがずれて再現しなくなる。
 */
void apply_realtime_mode()
{
    const auto scripted = (std::getenv("HENGBAND_SDL2_INJECT_KEYS") != nullptr) ||
        (std::getenv("HENGBAND_SDL2_INJECT_FILE") != nullptr);
    /*
     * 検証でだけスクリプトと併用する口（設計書 §13）。再現性は落ちるので普段は立てない。
     * **これは「リアルタイムを入れる」旗ではない**——スクリプトがあるときの遮断を外すだけで、
     * 入切そのものは設定（＝画面側から届いた値）が決める。ここを取り違えると、
     * 旗を立てた検証が「設定が届いていなくても動く」ので**配線の抜けを見逃す**。
     */
    const auto allow_with_script = std::getenv("HENGBAND_SDL2_REALTIME_FORCE") != nullptr;
    const auto &opts = sdl_ui_options();
    auto &clock = RealtimeClock::get_instance();
    clock.set_enabled(opts.realtime_enabled && (!scripted || allow_with_script));
    clock.set_seconds_per_turn(SdlUiOptions::realtime_seconds_per_turn(opts.realtime_speed_index));
    clock.set_prompt_background_wanted(opts.realtime_prompt_live);
    clock.set_self_speed_span(SdlUiOptions::realtime_self_span(opts.realtime_self_span_index));
}

/*!
 * @brief マップ表示スタイル（8px／16px タイル版）をコアのグラフィックモードへ写す。
 *
 * @details コアには元から**タイル表示の仕組みがある**（`use_graphics`。Windows 版の
 * 「オリジナル 8x8／Adam Bolt 16x16」がそれ）。立てると `map_info` の返り値が
 * 記号ではなく**タイル面の行・列**になる（attr の下位 7bit ＝行／char の下位 7bit ＝列。
 * `src/main-win.cpp` の `term_pict_win` と同じ約束）。どの実体がどのタイルかを
 * 決めているのはコアの prf なので、**UI 側に対応表を持たなくて済む**。
 *
 * `apply_cursor_mode` と同じで、触るのはコアが公開しているグローバルだけ
 * （`src/` は書き換えない。必守制約 1）。
 *
 * @note `reset_visuals()` は prf を読み直すので**切り替わった瞬間だけ**呼ぶ。
 * 毎フレーム呼ぶと 1,800 行の prf を毎回舐めることになる。
 *
 * @note `graf.prf` を経由しないのは、あれが `$SYS`（`ANGBAND_SYS`）で
 * x11／gcu／mac／win へ振り分ける作りだから。SDL2 版の `ANGBAND_SYS` は既定の
 * `"xxx"` で**どれにも当たらない**（＝タイルの対応表が 1 行も読まれない）。
 * `ANGBAND_SYS` を "win" に変えると `pref-win.prf` や `user-win.prf` の
 * キー割り当てまで巻き込むので、**要る prf を名指しで読む**。
 */
namespace {

//! いまコアへ入れてある値。切替の検出用（初期値はコアの既定と同じ）。
std::string g_applied_graf = "ascii";
/*!
 * @name 当てた索引が生きているかを見張るための標本
 * @details コアは**こちらの知らないところで `reset_visuals()` を呼ぶ**
 * （セーブのロードし直し `core/game-play.cpp:435`、記念撮影の後始末
 * `cmd-io/cmd-process-screen.cpp:396`、視覚設定メニュー）。そこを通ると
 * `graf-*.prf` で入れた索引が丸ごと消え、8px／16px 版が記号表示に落ちる。
 * 当てた直後に「タイルを持っている実体」を 1 つ覚えておき、毎フレーム見比べて
 * 食い違ったら当て直す。**コアの呼び出しを塞がずに追随する**ための標本。
 * @{
 */
MonraceId g_graf_probe_id{};
bool g_graf_probe_valid = false;
DisplaySymbol g_graf_probe_symbol{};
/*! @} */

//! 標本と現物が食い違っていたら真（＝コアが視覚設定を戻した）。
bool graf_mapping_lost()
{
    if (!g_graf_probe_valid) {
        return false;
    }
    const auto &monrace = MonraceList::get_instance().get_monrace(g_graf_probe_id);
    return monrace.symbol_config != g_graf_probe_symbol;
}

} // namespace

void apply_map_graphics_mode(PlayerType *player)
{
    const char *want = SdlUiOptions::map_style_graf_tag(sdl_ui_options().map_style_index);
    const bool want_graphics = (std::string_view(want) != "ascii");
    if (g_applied_graf == want && !(want_graphics && graf_mapping_lost())) {
        return;
    }
    if (player == nullptr || !AngbandWorld::get_instance().character_generated) {
        // キャラクターが出来る前は `reset_visuals` が読む `graf-<名前>.prf` の名前が定まらない。
        // タイトル・キャラ作成中は触らず、本編に入ってから当てる。
        return;
    }

    use_graphics = want_graphics;
    ANGBAND_GRAF = want_graphics ? want : "ascii";
    g_graf_probe_valid = false;

    // 実体（モンスター・アイテム・地形）の記号を定義値へ戻し、font.prf を当て直す。
    // グラフィックモードでも一度ここを通して**素の記号を土台に置く**（prf に載っていない
    // 実体がタイル面の行 0・列 0 を指したままになるのを防ぐ）。
    const bool saved = use_graphics;
    use_graphics = false;
    reset_visuals(player);
    use_graphics = saved;

    if (!want_graphics) {
        g_applied_graf = want;
        return;
    }

    // タイルの対応表。名指しで読む（上の @note）。
    process_pref_file(player, (std::string_view(want) == "new") ? "graf-new.prf" : "graf-xxx.prf");

    // 見張り用の標本を採る（タイル索引が入った実体を 1 つ）。
    for (const auto &[id, monrace] : MonraceList::get_instance()) {
        if (monrace && !monrace->symbol_config.is_ascii_graphics()) {
            g_graf_probe_id = id;
            g_graf_probe_symbol = monrace->symbol_config;
            g_graf_probe_valid = true;
            break;
        }
    }
    if (!g_graf_probe_valid) {
        std::fprintf(stderr, "[graf] no tile mapping found in graf-%s.prf\n", want);
    }

    g_applied_graf = want;
}

/*! @} */

/*!
 * @brief K-39: 意味のない Term カーソル（`_`）を消す。
 * @details コアは `inkey()` のたびに **icky 画面なら無条件でカーソルを点ける**
 * （`io/input-key-acceptor.cpp:214`。`character_icky_depth > 0` が条件に入っている）。
 * そのため選択メニューでも「最後に書いた場所」に `_` が残り、
 * タイトルの `Q) Quit _` やセーブ選択の `c) coon _` のように**意味のない文字**に見える
 * （人間の指摘: 「たまに `_` が無意味に表示されるから消して」）。
 *
 * `_` が要るのは**文字入力の途中だけ**（名前・ファイル名・マクロのトリガーキー・
 * 自動拾いエディタ）。そこでは編集位置を示す唯一の印なので消してはいけない。
 * 逆に**カーソル選択で操作する画面では、枠が位置を示しているので `_` は邪魔**である。
 *
 * 判定は「この画面に**別の選択カーソルが出ているか**」だけで足りる。
 * 1. コアのカーソル（`》`・birth の黄色・オプションの水色）
 * 2. UI の選択肢（店・建物・K-37 の名指し画面 …）
 * 3. 行 0 のプロンプト・数値入力（枠は `y`/`n` や桁そのものに重なる）
 * 4. **タイトルとセーブ選択**だけは 1〜3 のどれにも当たらない。
 *    コアが `> N) New Game` と**自前の `>` で選択行を示している**画面なので、
 *    行頭が `"> "` の行があることを手掛かりにする。
 *    文字入力の画面（名前・生い立ち）にはこの形の行が無いので巻き込まない。
 */
void suppress_meaningless_term_cursor(GameFrame &frame)
{
    if (frame.menu_term_curs_col < 0 && frame.menu_term_curs_row < 0) {
        return;
    }

    /*
     * **コアが文字を待っている間は何があっても消さない。**
     * `askfor_aux`（`core/asking-player.cpp`）に入っている間だけ真になる旗で、
     * 推測ではなくコアの状態そのものである（`text_input_state_hook`）。
     *
     * ここを入れていないと、マクロの設定（`@` →(4)）の「マクロ行動:」で
     * **キャレットが 1 度も出なかった**。あの画面は `(1)`〜`(0)` の一覧が出たままなので
     * `menu_choices` が 10 件残り、下の `has_other_cursor` が必ず真になる。
     * 文字を打っている最中に編集位置が見えないのは、選択カーソルの有無とは関係なく困る。
     */
    if (frame.text_input_active) {
        return;
    }

    bool has_other_cursor = (frame.menu_core_cursor.line_index >= 0) ||
        !frame.menu_choices.empty() || !frame.prompt.choices.empty() || frame.numeric.active;

    // 4: コアが `>` で選択行を示す pre_game_menu（タイトル／セーブ選択／死亡後のメニュー）。
    if (!has_other_cursor && frame.pre_game_menu) {
        for (const auto &line : frame.menu_term_lines) {
            const size_t head = first_non_space(line.text_utf8);
            if (line.text_utf8.compare(head, 2, "> ") == 0) {
                has_other_cursor = true;
                break;
            }
        }
    }

    if (has_other_cursor) {
        frame.menu_term_curs_col = -1;
        frame.menu_term_curs_row = -1;
    }
}

/*!
 * @brief `HENGBAND_SDL2_MENU_MIRROR_LOG` が指すファイルへ Term ミラーの現物を落とす（調査用）。
 * @details K-16（建物・店のカーソル操作）の設計は**画面の実文言**に依存する。
 * 推測で正規表現を書かないために、実プレイで通った icky 画面をそのまま記録する。
 * capture は毎フレーム走るので、**内容が変わったときだけ**追記する。
 */
void dump_menu_term_mirror(const GameFrame &frame)
{
    static const char *const path = std::getenv("HENGBAND_SDL2_MENU_MIRROR_LOG");
    if (path == nullptr || path[0] == '\0') {
        return;
    }

    std::string body;
    for (const auto &line : frame.menu_term_lines) {
        body += std::to_string(line.source_row);
        body += '|';
        body += std::to_string(static_cast<int>(line.color));
        body += '|';
        body += line.text_utf8;
        // 行内の色の切れ目。空なら 1 行 1 色（`line.color`）。
        for (const TermColorSpan &span : line.color_spans) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "   [span %d,%d=c%u]", span.begin, span.len,
                static_cast<unsigned>(span.color));
            body += buf;
        }
        // K-23: 行内の黄色（birth のカーソル）。ここが空なら枠は出ない。
        if (line.highlight_len > 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "   [yellow %d,%d=", line.highlight_begin, line.highlight_len);
            body += buf;
            if (static_cast<size_t>(line.highlight_begin) + static_cast<size_t>(line.highlight_len) <= line.text_utf8.size()) {
                body += line.text_utf8.substr(
                    static_cast<size_t>(line.highlight_begin), static_cast<size_t>(line.highlight_len));
            } else {
                body += "OUT-OF-RANGE";
            }
            body += ']';
        }
        body += '\n';
    }
    if (frame.menu_core_cursor.line_index >= 0) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "  core_cursor line=%d span=[%d,%d)\n",
            frame.menu_core_cursor.line_index, frame.menu_core_cursor.span_begin,
            frame.menu_core_cursor.span_begin + frame.menu_core_cursor.span_len);
        body += buf;
    }
    /*
     * K-38: **行 0 のプロンプトも必ず残す。** `y/n`・数値入力・`[S]弾, [A]矢 :` は
     * `screen_save()` を通らないので icky にならず、上の Term ミラーには 1 行も出ない。
     * ここを出さないと「スクリプトのキーが黙って食われた」ときに何が待っていたのか分からない
     * （2026-07-29 に `-more-` とこのプロンプトで何度もスクリプトがずれた）。
     */
    if (!frame.prompt.choices.empty()) {
        body += "  prompt line=" + std::to_string(frame.prompt.line_index) + " \"" + frame.prompt.text_utf8 + "\"";
        for (const auto &choice : frame.prompt.choices) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), " [key=0x%02X span=[%d,%d)]", choice.key, choice.begin, choice.begin + choice.len);
            body += buf;
        }
        body += '\n';
    }
    if (frame.numeric.active) {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "  numeric min=%d max=%d value=%d digits=%d line=%d span=[%d,%d) prompt=\"%s\"\n",
            frame.numeric.min, frame.numeric.max, frame.numeric.value, frame.numeric.digits,
            frame.numeric.line_index, frame.numeric.value_begin,
            frame.numeric.value_begin + frame.numeric.value_len, frame.numeric.prompt_utf8.c_str());
        body += buf;
    }

    for (const auto &choice : frame.menu_choices) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "  choice line=%d key=0x%02X span=[%d,%d) keyspan=[%d,%d) text=",
            choice.line_index, choice.key, choice.span_begin, choice.span_begin + choice.span_len,
            choice.key_begin, choice.key_begin + choice.key_len);
        body += buf;
        body += frame.menu_term_lines[static_cast<size_t>(choice.line_index)].text_utf8.substr(
            static_cast<size_t>(choice.span_begin), static_cast<size_t>(choice.span_len));
        body += '\n';
    }

    static std::string last_body;
    static bool last_open = false;
    if (body == last_body && frame.menu_open == last_open) {
        return;
    }
    last_body = body;
    last_open = frame.menu_open;

    FILE *fp = std::fopen(path, "ab");
    if (fp == nullptr) {
        return;
    }
    std::fprintf(fp, "==== frame=%llu menu_open=%d pre_game=%d icky=%d use_menu=%d curs=(%d,%d)\n",
        static_cast<unsigned long long>(frame.frame_id), frame.menu_open ? 1 : 0,
        frame.pre_game_menu ? 1 : 0, AngbandWorld::get_instance().character_icky_depth,
        use_menu ? 1 : 0, frame.menu_term_curs_col, frame.menu_term_curs_row);
    std::fwrite(body.data(), 1, body.size(), fp);
    std::fclose(fp);
}

} // namespace

void Bridge::set_view_size(int view_w, int view_h)
{
    view_cols_ = (std::max)(1, view_w);
    view_rows_ = (std::max)(1, view_h);
}

void Bridge::set_camera_follow_player(bool follow)
{
    camera_follow_player_ = follow;
}

namespace {

/*!
 * @brief K-40: 「転移した」と見なすプレイヤ格子の跳び（マス・チェビシェフ距離）。
 * @details ショートテレポート（`teleport_player(player_ptr, 10, ...)`）は
 * `teleport_player_aux`（`src/spell-kind/spells-teleport.cpp:280`）が候補の**遠い側半分**から
 * 行き先を選ぶので、10 マス指定なら実際には 5 マス前後より近くへは落ちない。
 * 一方で走行（`.`）は 1 手 1 マスなので、途中の再描画が数回抜けても 3 マスは超えない。
 * その間を取って 4 マスを境にする。
 */
constexpr int kTeleportJumpCells = 4;

/*!
 * @brief 帰還カウンタが 0 に落ちてから「発動だった」と認める game_turn の猶予。
 * @details `execute_recall`（`src/world/world-movement-processor.cpp:63`）はカウンタを 0 に
 * した同じ呼び出しで `leaving = true` を立てるので、階の入れ替わりは高々 1 プレイヤターン
 * （＝10 game_turn）後に起きる。取りこぼしを見て倍の余裕を置く。
 */
constexpr int32_t kRecallFireGraceTurns = 40;

/*!
 * @brief 溜めの上限。残りは発動の瞬間（`burst`）のために空けておく。
 * @details ここを 1 に寄せすぎると発動前に粒が集まりきってしまい、
 * 肝心の「転移の瞬間に一点へ集中する」段が見えなくなる。
 */
constexpr float kChargeCeiling = 0.72f;

} // namespace

void Bridge::fill_teleport_fx(GameFrame &frame, PlayerType *player)
{
    const auto *floor = player->current_floor_ptr;
    // 階の実体 id 代わり。`dun_level` では現実変容（同じ階を作り直す）を見分けられない。
    const int32_t floor_stamp = (floor != nullptr) ? static_cast<int32_t>(floor->generated_turn) : 0;
    const int32_t game_turn = static_cast<int32_t>(AngbandWorld::get_instance().game_turn);

    // --- 溜め（残りターンを持つ転移だけ） ---
    // 帰還（`word_recall`）と現実変容（`alter_reality`）はどちらも「回りの大気が張りつめてきた」
    // 系の予約型で、毎プレイヤターン 1 ずつ減って 0 で発動する。両方立つことは無いが、
    // 立っていた方を採ればよいので max で足りる。
    const int remain = (std::max)(static_cast<int>(player->word_recall), static_cast<int>(player->alter_reality));
    if (remain > 0) {
        // 巻物を重ねて読むと延長されることがあるので、分母は「これまでに見た最大値」で取る。
        recall_total_ = (std::max)(recall_total_, remain);
        const int span = (std::max)(1, recall_total_ - 1);
        const float done = static_cast<float>(recall_total_ - remain) / static_cast<float>(span);
        frame.teleport_fx.charge = kChargeCeiling * (std::min)(1.0f, (std::max)(0.0f, done));
    } else {
        if (last_recall_remain_ > 0) {
            // 発動したのか打ち消された（「張りつめた大気が流れ去った...」）のかは、
            // この時点では区別できない。階が入れ替わったら発動だったと決める。
            recall_fired_turn_ = game_turn;
        }
        recall_total_ = 0;
        frame.teleport_fx.charge = 0.f;
    }
    last_recall_remain_ = remain;

    // --- 発動 ---
    bool burst = false;
    const bool floor_changed = has_floor_stamp_ && (floor_stamp != last_floor_stamp_);
    if (floor_changed) {
        if (recall_fired_turn_ >= 0) {
            burst = true; // 帰還・現実変容の着地
        }
        recall_fired_turn_ = -1;
        // 別の階の座標と比べない（階段で降りただけで「転移した」ことになる）。
        has_last_pos_ = false;
    } else if (has_last_pos_) {
        const int jump = (std::max)(std::abs(player->x - last_px_), std::abs(player->y - last_py_));
        burst = (jump >= kTeleportJumpCells);
    }
    if ((recall_fired_turn_ >= 0) && ((game_turn - recall_fired_turn_) > kRecallFireGraceTurns)) {
        recall_fired_turn_ = -1; // 打ち消しだった。次の階段で誤爆させない
    }

    last_floor_stamp_ = floor_stamp;
    has_floor_stamp_ = true;
    last_px_ = player->x;
    last_py_ = player->y;
    has_last_pos_ = true;
    frame.teleport_fx.burst = burst;
}

GameFrame Bridge::capture(PlayerType *player)
{
    GameFrame frame{};
    frame.frame_id = ++frame_counter_;

    if (player == nullptr) {
        return frame;
    }

    fill_hud(frame.hud, player);
    fill_messages(frame);
    frame.controller_hint = _("移動:十字/L  Select/F10:機能  A:決定  B:戻る",
        "Move: D-pad/L  Select/F10: features  A: select  B: back");
    // 死亡時の y/n・遺言は icky 無しで Term に prt されるため、is_dead でもミラーを開く。
    const bool is_dead = player->is_dead;
    const bool character_generated = AngbandWorld::get_instance().character_generated;
    /*
     * キャラクターが出来る前（`init_angband` の進行表示・データ読込のエラー・
     * `-more-` 待ち）も icky が立たないまま Term にだけ出る。**MainMap には
     * まだ映すものが無い**のだから、この間は常にミラーを開く。
     *
     * これを入れていないと、`init_angband` が `inkey()` で入力待ちに入ったとき
     * 画面が真っ黒のまま固まり、**何を聞かれているのか利用者にも調査側にも分からない**
     * （Android のエミュレータで実際に踏んだ。原因はネイティブスタックを取るまで
     * 特定できなかった）。Windows でも同じ穴があり、こちらの方が素直な既定。
     */
    frame.menu_open = (AngbandWorld::get_instance().character_icky_depth > 0)
        || is_dead || !character_generated;
    frame.pre_game_menu = !character_generated || is_dead;
    // K-47: タイトル画の出し分け。コアから推測せず、オープニング側の宣言をそのまま流す。
    frame.title_screen = g_title_screen;

    // 通常プレイ中の Term（地図＋左ステータス）を基準保存。
    // close_game のように screen_save 無しで icky だけ立てる経路でも、差分＝メニュー本文にできる。
    if (!frame.menu_open) {
        refresh_float_menu_baseline();
    }

    // ソフトキーボードの出し入れに使う（Android）。推測ではなくコアの状態そのまま。
    frame.text_input_active = is_core_text_input_active();

    /*
     * `HENGBAND_FX_TEST=1` で、**戦闘なしに演出の道だけを試す**。
     * プレイヤの足元へ「当てた」印を 1 秒おきに置くだけ。演出が出ないと言われたとき、
     * 「記録が鳴っていない」のか「描けていない」のかを分けるために要る
     * （戦闘をスクリプトで起こすのは当てにならない。実際に起こせなかった）。
     */
    if (std::getenv("HENGBAND_FX_TEST") != nullptr) {
        static uint32_t test_counter = 0;
        if ((++test_counter % 100) == 0) {
            CombatFeedback::get_instance().record_monster_hit(
                player->y, player->x, 50, 100, AttributeType::FIRE);
        }
    }

    fill_combat_fx(frame);
    fill_sounds(frame);
    apply_cursor_mode();
    apply_bot_json_output();
    apply_realtime_mode();
    // 8px／16px タイル版のときだけコアのグラフィックモードを立てる（切替時のみ prf を読む）。
    apply_map_graphics_mode(player);
    // `&`（カラーの設定）と pref の `V:` 行で変わる。画面側の色はすべてここから引く。
    fill_term_palette(frame);
    fill_menu_term_mirror(frame);
    // K-20: 「コアがカーソルを持っているか」は画面上の `》` で見る（グローバル use_menu ではない）。
    fill_menu_choices(frame);
    fill_row0_prompts(frame);
    // K-39: 枠が位置を示している画面では Term カーソル（`_`）を出さない。
    suppress_meaningless_term_cursor(frame);
    dump_menu_term_mirror(frame);

    const auto *floor = player->current_floor_ptr;
    const int px = player->x;
    const int py = player->y;

    // H2: プレイヤ格子は Bridge が GameFrame に充填する。ui は cam を流用しない。
    frame.player_gx = px;
    frame.player_gy = py;

    // K-40: 転移の演出状態。プレイヤ格子を詰めた直後に見る（跳びの検知に使う）。
    fill_teleport_fx(frame, player);

    /*
     * フロアの素性（ボクセル HD2D 設計書 §12-1・§12-2。P3 で新設）。
     *
     * ボクセル HD2D は `seed = hash(絶対gx, 絶対gy, フロア識別子)` で見た目を引くので、
     * **フロアを一意に指す番号**が要る。`generated_turn` まで含めるのは、現実変容などで
     * **作り直された同じ階を別物と見分ける**ため（含めないと前の階の記憶が乗る）。
     *
     * 読むだけで、コアの状態は 1 つも変えない（必守制約 1）。
     * 既存 ui はこの欄を知らないが、未知キーは黙って捨てる約束なので影響しない（v1 §2.3）。
     */
    if (floor != nullptr) {
        frame.floor.dungeon_id = static_cast<int>(floor->dungeon_id);
        frame.floor.dun_level = static_cast<int>(floor->dun_level);
        frame.floor.generated_turn = static_cast<uint64_t>(floor->generated_turn);
        /*
         * 闘技場（設計書 §4.6 の観戦カメラの合図）。**カジノ闘技場（モンスター賭博）は
         * `inside_arena` を立てない**——`market/melee-arena.cpp:113` が立てるのは
         * `AngbandSystem::phase_out`（観戦状態）だけである。ここで拾わないと、
         * 賭けた試合が画面の外で進む（2026-08-24 に気づいた。幻想蛮怒側は
         * `gb_shim.c` が `inside_arena ‖ inside_battle` で最初から畳んでいた）。
         */
        if (floor->inside_arena || AngbandSystem::get_instance().is_phase_out()) {
            frame.floor.kind = static_cast<int>(FloorKind::Arena);
        } else if (floor->is_in_quest()) {
            frame.floor.kind = static_cast<int>(FloorKind::Quest);
        } else if (floor->is_underground()) {
            frame.floor.kind = static_cast<int>(FloorKind::Dungeon);
        } else {
            frame.floor.kind = static_cast<int>(FloorKind::Surface);
        }
        /*
         * 町の番号（§12-2。P10 第 2 期で足した）。`FloorType` は持っていないので
         * `AngbandWorld` から採る。**町の外では 0**（荒野・地下）。
         *
         * これが要るのは、町が**訪れるたびに同じ姿でなければならない**からである。
         * 見た目の種に `generated_turn` を混ぜると、町は出入りのたびに作り直されるので
         * 建物の意匠が毎回入れ替わる。地上では turn の代わりにこの番号を混ぜる
         * （`hd2d/world/terrain_view.cpp` の `cell_seed`）。
         */
        frame.floor.town_id = (frame.floor.kind == static_cast<int>(FloorKind::Surface))
            ? static_cast<int>(AngbandWorld::get_instance().get_town_index())
            : 0;
        /*
         * 場所の名前と広域マップの印（P10 のカットイン演出。2026-08-11 に決めた）。
         *
         * `map_name()` は `src/floor/floor-util.cpp` にあるコアの関数で、
         * クエスト階・広域マップ・アリーナ・闘技場・町・ダンジョンの分岐を全部持っている。
         * **読むだけ**なので必守制約 1（`src/` を触らない）に触れない。
         *
         * ## **キャラクターが出来る前は呼んではならない**（2026-08-11 に踏んだ）
         * `map_name()` の中は `std::map::at` と `THROW_EXCEPTION` だらけである:
         *   - `QuestList::get_quest()` … `.at()`（`quest-list.cpp` の注記そのもの——
         *     「でないと後続の新規ゲーム生成が get_quest() の std::map::at で
         *      分かりにくく落ちる」）
         *   - `DungeonList::get_dungeon()` … `validate_dungeon_id()` が投げる
         *   - `TownList::get_town()` … 範囲外で `std::out_of_range`
         * キャラクター作成中のフロアはどれも入っていないので、**毎フレーム投げる**。
         * 投げるとこの関数が途中で降り、**フレームが 1 枚も届かなくなる**ので、
         * 画面が固まって「決定が効かない」に見える（2026-08-11 に気づいた:
         * 「新規で開始する際、名前選択の手前で決定ができない。ESC で抜けて名前を入れると落ちる」）。
         *
         * 要るのは地図が出ている場面だけなので、**キャラクターが出来るまでは空のまま**にする。
         * それでも投げたときのために囲ってある——**フレームを 1 枚落とすほうが高くつく**。
         */
        if (character_generated) {
            try {
                frame.floor.place_name_utf8 = to_utf8_line(map_name(player));
                frame.floor.wild_mode = AngbandWorld::get_instance().is_wild_mode();
            } catch (const std::exception &e) {
                //! **黙らない。**1 度だけ理由を出す（毎フレーム出すと記録が埋まる）。
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    std::fprintf(stderr, "[bridge] 場所の名前を採れませんでした（カットインは出ません）: %s\n",
                        e.what());
                }
                frame.floor.place_name_utf8.clear();
                frame.floor.wild_mode = false;
            }
        }
    }

    /*
     * 光の状態（ボクセル HD2D 設計書 §12-4・§12-5。P5 で新設）。
     *
     * `extract_date_time()` は (日, 時, 分) を返す。**日は捨てて時刻だけ**渡す
     * （見た目に効くのは一日のうちの位置だけで、何日目かは効かない）。
     * `daytime` を別に渡すのは、コアの日中判定が `extract_date_time()` とは
     * 1/4 日ずれた基準（一日の前半か）で動いているため。時刻からは導けない。
     *
     * 読むだけで、コアの状態は 1 つも変えない（必守制約 1）。
     */
    {
        const auto &world = AngbandWorld::get_instance();
        const auto [day, hour, minute] = world.extract_date_time(InnerGameData::get_instance().get_start_race());
        static_cast<void>(day);
        frame.lighting.day_minute = (hour * 60) + minute;
        frame.lighting.daytime = world.is_daytime();
        frame.lighting.light_radius = static_cast<int>(player->cur_lite);
    }

    //! 周囲の地形の内訳。
    fill_surroundings(frame, player, floor);

    frame.view_w = view_cols_;
    frame.view_h = view_rows_;

    int ox = px - view_cols_ / 2;
    int oy = py - view_rows_ / 2;
    /*
     * カメラ追従（HD2D）とフロア端での丸め（SDL2）の切替。
     *
     * SDL2 経路は真上からの 2D で view が 66×44 程度にしか要らないので、丸めても
     * プレイヤが中心から外れるのはフロアの端に寄ったときだけで、外れ幅も数マスで済む。
     *
     * HD2D は 1 点透視で可視帯が奥へ深いぶん**行数・列数を大きく増やして**要求する
     * （既定 42° で 47×22・34° で 60×37）。コアのフロアは高々 66 行しか無いので、
     * 縦のスクロール余地が 29 行しか残らず、**カメラがフロア端に張り付いてプレイヤが
     * 画面の上下を泳ぐ**。人間の指摘「キャラクターが常に中央になるように。いまは
     * 南北方向に画面が固定のようになっている」はこれである。
     *
     * HD2D では丸めをやめ、**プレイヤを常に可視範囲の中心に置く**。はみ出したマスは
     * 下の `in_floor` 判定で空白セルになるので走査も描画も壊れない。
     * `frame.cam_x/cam_y` は `ox + view/2` なので追従時はプレイヤ格子そのものになり、
     * 消費側（`compute_main_map_origin` / ヒットテスト / `make_hd2d_camera`）が使う
     * `x0 = cam_x − view_w/2` は `ox` に一致したままである（切り出しと必ず同じ原点になる）。
     */
    if (!camera_follow_player_ && floor != nullptr && floor->width > 0 && floor->height > 0) {
        const int max_ox = (std::max)(0, floor->width - view_cols_);
        const int max_oy = (std::max)(0, floor->height - view_rows_);
        ox = (std::max)(0, (std::min)(ox, max_ox));
        oy = (std::max)(0, (std::min)(oy, max_oy));
    }
    frame.cam_x = ox + view_cols_ / 2;
    frame.cam_y = oy + view_rows_ / 2;

    fill_sub2_equipment(frame, player);
    fill_sub3_monsters(frame, player, ox, oy, view_cols_, view_rows_);
    fill_sub5_inventory(frame, player);
    // K-26: 「コアのサブウインドウ」を割り当てたパネルは、上の作り込み表示ではなく
    // コアが専用 Term に描いた内容を映す（`GameFrame::sub_panels`）。
    // 既定（-1）のままのパネルには何も入らないので、従来表示がそのまま出る。
    fill_sub_panels(frame);
    // K-28: コアの左フレーム（キャラクター状態）を MainMap の左へ。
    fill_status_col(frame);
    fill_minimap(frame, player, floor);

    frame.cells.reserve(static_cast<size_t>(view_cols_) * static_cast<size_t>(view_rows_));

    for (int vy = 0; vy < view_rows_; ++vy) {
        const int gy = oy + vy;
        for (int vx = 0; vx < view_cols_; ++vx) {
            const int gx = ox + vx;

            MapCellView cell{};
            cell.gx = static_cast<int16_t>(gx);
            cell.gy = static_cast<int16_t>(gy);

            const bool in_floor = (floor != nullptr && floor->width > 0 && floor->height > 0 &&
                gx >= 0 && gx < floor->width && gy >= 0 && gy < floor->height);
            if (!in_floor) {
                cell.ascii_fallback = ' ';
                cell.fg_color = 0;
                cell.bg_color = 0;
                cell.tile_index = 0;
                frame.cells.push_back(cell);
                continue;
            }

            const Pos2D pos(gy, gx);
            const auto &grid = floor->get_grid(pos);
            const DisplaySymbolPair symbol = map_info(player, pos);

            // 8px／16px タイル版: map_info の返り値は記号ではなくタイル面の位置。
            // `is_ascii_graphics()`（color < 0x80）が偽のものだけがタイルを指している。
            // prf に載っていない実体は記号のまま返るので、そこは負値のままにして
            // ui 側の「未生成アートの印」へ落とす（無いものを行 0・列 0 に化けさせない）。
            if (use_graphics) {
                const auto &fg = symbol.symbol_foreground;
                const auto &bg = symbol.symbol_background;
                if (!fg.is_ascii_graphics()) {
                    cell.graf_fg_row = static_cast<int16_t>(fg.color & 0x7f);
                    cell.graf_fg_col = static_cast<int16_t>(static_cast<unsigned char>(fg.character) & 0x7f);
                }
                if (!bg.is_ascii_graphics()) {
                    cell.graf_bg_row = static_cast<int16_t>(bg.color & 0x7f);
                    cell.graf_bg_col = static_cast<int16_t>(static_cast<unsigned char>(bg.character) & 0x7f);
                }
            }

            cell.fg_color = symbol.symbol_foreground.color;
            cell.bg_color = symbol.symbol_background.color;
            cell.ascii_fallback = symbol.symbol_foreground.character;
            // map_info と同じ MIMIC 外形 ID（隠し扉→花崗岩など）。feat 直読みだと見た目とずれる。
            cell.terrain_id = static_cast<uint16_t>(grid.get_terrain_id(TerrainKind::MIMIC));

            const bool lit_now = feature_is_lit_now(player, floor, grid);
            const bool know_feature = feature_is_known(player, floor, grid);

            // 0=記憶のみ／1=視界内だが無灯／2=いま照明あり。
            // ※ CAVE_GLOW だけでは 2 にしない（遠い常時照明部屋が満灯のまま残る不具合の修正）。
            if (lit_now) {
                cell.light_level = 2;
            } else if ((grid.info & CAVE_VIEW) != 0) {
                cell.light_level = 1;
            } else {
                cell.light_level = 0;
            }

            // 地形属性ビット（HD2D の立体化入力）。terrain_id と同じく MIMIC で判定する。
            // 未視認マスで terrain_id を 0 に潰すのと同じ思想で、KNOWN も地形ビットも立てない。
            // ここで壁・扉を出してしまうと、記号は伏せたまま立体形状だけが未探知の地形を暴露する。
            if (know_feature) {
                cell.feature_flags = static_cast<uint16_t>(
                    CELL_FEAT_KNOWN | translate_feature_flags(grid.get_terrain(TerrainKind::MIMIC)));
                /*
                 * 部屋と常時照明（P10 レビュー対応）。**既知マスにしか立てない**（上と同じ思想）。
                 * ROOM は意味づけ層の部屋/通路判定の正（ミニマップの幅の推定は、暗い部屋を
                 * 歩いた直後に「幅 1 = 通路」と誤読して床の見た目が後から化ける）。
                 * GLOWING は「マス自体が明るい」（CAVE_GLOW。MNDK で消灯中は立てない。
                 * 松明を明るい部屋にだけ置くための材料）。
                 */
                if (grid.is_room()) {
                    cell.feature_flags = static_cast<uint16_t>(cell.feature_flags | CELL_FEAT_ROOM);
                }
                if ((grid.info & (CAVE_GLOW | CAVE_MNDK)) == CAVE_GLOW) {
                    cell.feature_flags = static_cast<uint16_t>(cell.feature_flags | CELL_FEAT_GLOWING);
                }
            }
            /*
             * 立ち木・岩の板の足元に敷く地面のタイル索引（版 2.7）。
             * 索引が引けなければ 0 のまま＝ui は従来どおりそのマスのタイルを平らに敷く。
             * 未探知マスでは feature_flags が 0 なのでここも 0 のまま（地形は漏れない）。
             */
            if ((cell.feature_flags & CELL_FEAT_TREE) != 0u) {
                cell.under_tile_index = lookup_terrain_tile(kTerrainIdGrass);
            } else if ((cell.feature_flags & CELL_FEAT_RUBBLE) != 0u) {
                cell.under_tile_index = lookup_terrain_tile(kTerrainIdFloor);
            }
            // プレイヤ位置は既知・未知に関係なく立てる（自分の居場所は隠す情報ではない）。
            if (gx == px && gy == py) {
                cell.feature_flags = static_cast<uint16_t>(cell.feature_flags | CELL_FEAT_PLAYER);
            }

            // モンスター判定は地形の空白化より先に行う（下の !know_feature 分岐が参照する）。
            if (grid.m_idx > 0 && grid.m_idx < static_cast<MONSTER_IDX>(floor->m_list.size())) {
                const auto &mon = floor->m_list[grid.m_idx];
                // ml（視認中）を条件にする。resolve_tile_index の R 索引条件と一致させ、
                // かつ ui 側が monster_id を「見えている敵がいる」信号として使えるようにする。
                // ml を見ないと不可視・未探知のモンスター位置を UI が暴露してしまう。
                if (mon.is_valid() && mon.ml) {
                    cell.monster_id = static_cast<uint16_t>(enum2i(mon.get_monrace_id()));
                    // K-41: なめらか移動が「同じ個体」を追えるように通し番号も渡す。
                    // 種族番号（`monster_id`）では同種が並んだときに追跡が入れ替わる。
                    cell.monster_slot = static_cast<uint16_t>(grid.m_idx);
                }
            }

            // 未視認マスは公式地形アートを出さない（真っ暗）。
            if (!know_feature) {
                cell.bg_color = 0;
                cell.terrain_id = 0;
                // ただし ml のモンスターがいるマスでは記号／色を残す。テレパシーや赤外線視で
                // 「見えている」敵を UI が消してはならない。R タイルを持つ敵は resolve_tile_index
                // が未視認でも R を返して表示されるので、ここで記号を潰すと未生成タイルの敵だけが
                // 消えるという非対称になる（B-07）。terrain_id は 0 のままなので地形は漏れない。
                if (cell.monster_id == 0) {
                    cell.ascii_fallback = ' ';
                    cell.fg_color = 0;
                    // 8px／16px 版も同じ扱い。ここを残すと未探知の地形がタイルで漏れる。
                    cell.graf_fg_row = -1;
                    cell.graf_fg_col = -1;
                }
                cell.graf_bg_row = -1;
                cell.graf_bg_col = -1;
            }
            if (lit_now && !grid.o_idx_list.empty()) {
                const OBJECT_IDX o_idx = *grid.o_idx_list.begin();
                if (o_idx > 0 && o_idx < static_cast<OBJECT_IDX>(floor->o_list.size()) &&
                    floor->o_list[static_cast<size_t>(o_idx)] &&
                    floor->o_list[static_cast<size_t>(o_idx)]->is_valid()) {
                    cell.object_id = static_cast<uint16_t>(floor->o_list[static_cast<size_t>(o_idx)]->bi_id);
                }
            }

            // グラフィックモード中は `ascii_fallback` / `fg_color` がタイル面の位置なので、
            // それを鍵にする HD タイルの索引は引かない（引くと無関係なタイルが出る）。
            if (!use_graphics) {
                cell.tile_index = resolve_tile_index(player, floor, grid, gx, gy,
                    cell.ascii_fallback, cell.fg_color, cell.terrain_id, know_feature, lit_now);
            }
            frame.cells.push_back(cell);
        }
    }

    return frame;
}

// ---------------------------------------------------------------------------
// K-16 選択肢抽出の自己検査（HENGBAND_SDL2_MENUCHOICE_SMOKE）
// ---------------------------------------------------------------------------

namespace {

//! Term 行を 1 本組み立てる。`put(列, 文字列)` で列を指定して重ねる（コアの prt/c_put_str 相当）。
class SmokeRow {
public:
    explicit SmokeRow(int source_row)
        : source_row_(source_row)
    {
    }

    SmokeRow &put(size_t col, const std::string &utf8)
    {
        // Term の列＝半角 1 桁。全角は 2 桁なので、詰め物は「表示桁」で数える。
        while (display_cols_ < col) {
            text_ += ' ';
            ++display_cols_;
        }
        text_ += utf8;
        display_cols_ += display_width(utf8);
        return *this;
    }

    //! K-37: 行の色（オプション画面のカーソルは `TERM_L_BLUE` の行そのもの）。
    SmokeRow &color(uint8_t c)
    {
        color_ = c;
        return *this;
    }

    TermMirrorLine build() const
    {
        TermMirrorLine line{};
        line.source_row = source_row_;
        line.color = color_;
        line.text_utf8 = text_;
        return line;
    }

private:
    static size_t display_width(const std::string &utf8)
    {
        size_t w = 0;
        for (size_t i = 0; i < utf8.size();) {
            const auto u = static_cast<unsigned char>(utf8[i]);
            if (u < 0x80) {
                i += 1;
                w += 1;
            } else if ((u & 0xE0) == 0xC0) {
                i += 2;
                w += 2;
            } else if ((u & 0xF0) == 0xE0) {
                i += 3;
                w += 2;
            } else {
                i += 4;
                w += 2;
            }
        }
        return w;
    }

    int source_row_;
    std::string text_;
    size_t display_cols_{};
    uint8_t color_{1};
};

//! 抽出結果のキー列を "a,b,ESC" のような読める文字列にする。
std::string keys_to_text(const GameFrame &frame)
{
    std::string out;
    for (const MenuChoice &choice : frame.menu_choices) {
        if (!out.empty()) {
            out += ',';
        }
        if (choice.key == 0x1B) {
            out += "ESC";
        } else if (choice.key == ' ') {
            out += "SP";
        } else {
            out += static_cast<char>(choice.key);
        }
    }
    return out;
}

struct SmokeCase {
    const char *name;
    std::vector<TermMirrorLine> lines;
    //! K-20: コアのカーソル `》` を見つけるべき画面か（見つけた画面は UI 側が譲る）。
    bool expect_core_cursor;
    const char *expect_keys;
};

//! 店の画面（`src/store/cmd-store.cpp:118-151` ＋ `src/view/display-store.cpp:41`）。
std::vector<TermMirrorLine> make_store_screen(bool with_item_prompt)
{
    std::vector<TermMirrorLine> lines;
    if (with_item_prompt) {
        // src/store/store.cpp:183 の `(%s:%c-%c, ESCで中断) %s`。
        lines.push_back(SmokeRow(0).put(0, _("(商品:a-c, ESCで中断) どの品物が欲しいんだい? ",
                                            "(Items a-c, ESC to exit) Which item are you interested in? "))
                            .build());
    }
    lines.push_back(SmokeRow(6).put(0, "a) ").put(3, _("鉄の靴 [4,+0]", "Iron Shod Boots [4,+0]")).put(68, "      120").build());
    lines.push_back(SmokeRow(7).put(0, "b) ").put(3, _("硬革の帽子 [2,+0]", "Hard Leather Cap [2,+0]")).put(68, "       12").build());
    lines.push_back(SmokeRow(8).put(0, "c) ").put(3, _("松明(2500ターンの寿命)", "Wooden Torch (2500 turns)")).put(68, "        2").build());
    lines.push_back(SmokeRow(19).put(53, _("手持ちのお金: ", "Gold Remaining: ")).put(68, "      724").build());
    lines.push_back(SmokeRow(20).put(0, _("コマンド:", "You may: ")).build());
    lines.push_back(SmokeRow(21)
                        .put(0, _(" ESC) 建物から出る", " ESC) Exit from Building."))
                        .put(30, _("p) 商品を買う", "p) Purchase an item."))
                        .put(56, _("i/e) 持ち物/装備の一覧", "i/e) Inventry/Equipment list"))
                        .build());
    lines.push_back(SmokeRow(22)
                        .put(0, _(" -)前ページ", " -) Previous page"))
                        .put(30, _("s) アイテムを売る", "s) Sell an item."))
                        .put(56, _("w/t) 装備する/はずす", "w/t) Wear/Take off equipment"))
                        .build());
    lines.push_back(SmokeRow(23)
                        .put(0, _(" スペース) 次ページ", " SPACE) Next page"))
                        .put(30, _("x) 商品を調べる", "x) eXamine an item in the shop"))
                        .build());
    return lines;
}

//! 建物の画面（`src/market/building-service.cpp:90-144` ＋ `src/market/building-util.cpp:24`）。
std::vector<TermMirrorLine> make_building_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(2).put(1, _("ロバート (人間)", "Robert (Human)")).put(38, _("武器屋", "Weaponsmith")).build());
    lines.push_back(SmokeRow(19)
                        .put(0, _(" a) 賭けをする ($100)", " a) Make a bet (100gp)"))
                        .put(35, _(" b) 武器を鑑定する", " b) Identify a weapon"))
                        .build());
    lines.push_back(SmokeRow(20)
                        .put(0, _(" c) 修復する ($200)", " c) Repair (200gp)"))
                        .put(35, _(" d) 研究する", " d) Research"))
                        .build());
    lines.push_back(SmokeRow(23)
                        .put(0, _(" ESC) 建物を出る", " ESC) Exit building"))
                        .put(53, _("手持ちのお金: ", "Gold Remaining: "))
                        .put(68, "      724")
                        .build());
    return lines;
}

//! 持ち物一覧（`i`）。**選択画面ではない**ので 1 件も拾ってはいけない。
std::vector<TermMirrorLine> make_inventory_display_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(0).put(0, _("持ち物： 合計  32.1 kg (限界の44%) コマンド:",
                                        "Inventory: carrying 32.1 lbs (44% of capacity) Command: "))
                        .build());
    lines.push_back(SmokeRow(1).put(33, _("a) , 4つの 食料", "a) , 4 Rations of Food")).put(70, "2.0 kg").build());
    lines.push_back(SmokeRow(2).put(33, _("b) ? 「断縦 純 絶根」と書かれた巻物", "b) ? a Scroll titled \"dan jyu\"")).put(70, "0.2 kg").build());
    return lines;
}

/*!
 * @brief K-27: レベルアップの能力値選択（`src/player/player-status.cpp:2847-2855` の書式そのまま）。
 * @details 現在値は `cnv_stat()` の**右詰め 6 文字**。`f) 魅力 (現在値      3)` のように
 * 「空白＋1 桁＋ `)`」で終わる行を必ず 1 つ入れておく（`3)` を選択肢に拾うと
 * 押しても効かない行ができるため。抽出器は英字キーの最初の 1 件だけを採る）。
 */
std::vector<TermMirrorLine> make_stat_raise_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(1).put(14, _("        どの能力値を上げますか？", "        Which stat do you want to raise?")).build());
    static const char *const kLabels[] = {
        _("腕力", "Str"), _("知能", "Int"), _("賢さ", "Wis"),
        _("器用", "Dex"), _("耐久", "Con"), _("魅力", "Chr")
    };
    static const char *const kValues[] = {
        " 18/40", "     9", "    14", "    16", "    17", "     3"
    };
    for (int i = 0; i < 6; ++i) {
        std::string body = "        ";
        body += static_cast<char>('a' + i);
        body += ") ";
        body += kLabels[i];
        body += _(" (現在値 ", " (cur ");
        body += kValues[i];
        body += ")";
        lines.push_back(SmokeRow(2 + i).put(14, body).build());
    }
    return lines;
}

//! アイテム選択プロンプト（`get_item`）。2026-07-26 の実ミラー採取そのまま。
std::vector<TermMirrorLine> make_item_prompt_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(0).put(0, _("(持ち物:b-c,'(',')', ESC) どの巻物を読みますか?",
                                        "(Inven b-c,'(',')', ESC) Read which scroll? "))
                        .build());
    lines.push_back(SmokeRow(1).put(33, _("b) ? 「断縦 純 絶根」と書かれた巻物", "b) ? a Scroll titled \"dan jyu\"")).put(70, "0.2 kg").build());
    lines.push_back(SmokeRow(2).put(33, _("c) ? 「死没 完後 両」と書かれた巻物", "c) ? a Scroll titled \"shibotsu\"")).put(70, "0.2 kg").build());
    return lines;
}

/*!
 * @brief コマンドメニュー（`input-key-requester.cpp:124` `inkey_from_menu`）。
 * @details `make_commands_frame()` が枠を出し、`put_str(_("》","> "), base_y+1+num/2,
 * base_x+2+(num%2)*24)` で選択行に印を置く。項目名は同じ行の印の直後（+4 桁）。
 */
std::vector<TermMirrorLine> make_command_menu_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(1).put(0, _("  《コマンド》", "  [Command]")).build());
    lines.push_back(SmokeRow(2).put(2, _("》", "> ")).put(4, _("歩く", "Walk")).put(26, _("休息する", "Rest")).build());
    lines.push_back(SmokeRow(3).put(4, _("使う", "Use")).put(26, _("調べる", "Examine")).build());
    return lines;
}

/*!
 * @brief 呪文選択（`spell-info.cpp:290`）。`use_menu` が真のときの実際の書式。
 * @details 選択行だけ `"  》 "`、他は空白 5 桁。名前は `%-30s` で右に列が続く。
 */
std::vector<TermMirrorLine> make_spell_menu_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(1).put(1, _("Lv   MP 失率 効果", "Lv Mana Fail Info")).build());
    lines.push_back(SmokeRow(2).put(1, _("  》 ", "  >  ")).put(6, _("モンスター感知", "Detect Monsters")).put(38, " 1    1   5%").build());
    lines.push_back(SmokeRow(3).put(1, "     ").put(6, _("電撃", "Lightning Bolt")).put(38, " 3    2  10%").build());
    return lines;
}

/*!
 * @name K-37: 名指しの選択画面（コアの描画コードの書式そのまま）
 * @{
 */

/*!
 * @brief 帰還先ダンジョン選択（`spells-world.cpp:365` `choose_dungeon`）。
 * @details **トランプタワーから呼ばれた場合**を再現する。建物のメニュー（行 19〜23）が
 * 背後に残るので、行範囲を切らないと `Building` と同じ結果になり
 * **押しても効かない建物のキー**を積んでしまう。ここはその退行検査でもある。
 */
std::vector<TermMirrorLine> make_recall_dungeon_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(0).put(0, _("どのダンジョンに帰還しますか:", "Which dungeon do you recall?: ")).build());
    // `dungeon-service.cpp:25` の `      %c) %c%-12s : 最大 %d 階`
    lines.push_back(SmokeRow(2).put(14, _("      a)  ジャングル     : 最大  25 階", "      a)  Jungle          : Max level 25")).build());
    lines.push_back(SmokeRow(3).put(14, _("      b) !アングバンド    : 最大  99 階", "      b) !Angband         : Max level 99")).build());
    lines.push_back(SmokeRow(4).put(14, _("      c)  竜の巣         : 最大  60 階", "      c)  Dragon's lair   : Max level 60")).build());
    // 背後に残る建物（`building-service.cpp:109` / `building-util.cpp`）。拾ってはいけない。
    lines.push_back(SmokeRow(19).put(0, _(" d) 帰還する", " d) Recall")).build());
    lines.push_back(SmokeRow(23)
                        .put(0, _(" ESC) 建物を出る", " ESC) Exit building"))
                        .put(53, _("手持ちのお金: ", "Gold Remaining: "))
                        .put(68, "      724")
                        .build());
    return lines;
}

//! 町テレポート（`spells-world.cpp:264` `tele_town`）。行も文字も飛び飛びになりうる。
std::vector<TermMirrorLine> make_tele_town_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(0).put(0, _("どこに行きますか:", "Where do you want to go: ")).build());
    lines.push_back(SmokeRow(6).put(5, _("a) 郊外の街", "a) Outpost")).build());
    lines.push_back(SmokeRow(8).put(5, _("c) 地下都市", "c) Telmora")).build());
    return lines;
}

//! モンスター闘技場の賭け（`melee-arena.cpp:35`）。キーは英字ではなく数字。
std::vector<TermMirrorLine> make_melee_arena_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(0).put(0, _("どれに賭けますか:", "Which monster: ")).build());
    lines.push_back(SmokeRow(4).put(4, _("モンスター                          倍率", "Monsters                            Odds")).build());
    for (int i = 0; i < 4; ++i) {
        std::string body;
        body += static_cast<char>('1' + i);
        body += _(") ノーム                              1.50倍", ") Gnome                                1.50");
        lines.push_back(SmokeRow(5 + i).put(1, body).build());
    }
    // 背後の建物。拾ってはいけない。
    lines.push_back(SmokeRow(23)
                        .put(0, _(" ESC) 建物を出る", " ESC) Exit building"))
                        .put(53, _("手持ちのお金: ", "Gold Remaining: "))
                        .build());
    return lines;
}

//! 修行僧の構え（`mind-monk.cpp:37`）。見出しは行 1、選択肢は行 2 から。
std::vector<TermMirrorLine> make_monk_stance_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(1).put(14, _("        どの構えをとりますか？", "        Choose Stance: ")).build());
    lines.push_back(SmokeRow(2).put(20, _(" a) 構えをとく", " a) No form")).build());
    lines.push_back(SmokeRow(3).put(20, _(" b) 玄武の構え    非常に防御的な構え", " b) Genbu         Defensive stance")).build());
    lines.push_back(SmokeRow(4).put(20, _(" c) 白虎の構え    攻撃的な構え", " c) Byakko        Offensive stance")).build());
    return lines;
}

/*!
 * @brief 呪術の中断（`spells-hex.cpp:83`）。
 * @details プロンプト行の `'l'`（全て中断）と `ESC)` も選択肢にするが、
 * **本体の後ろ**に積む。先頭にすると決定が既定で「全部中断」に乗る。
 */
std::vector<TermMirrorLine> make_hex_stop_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(0).put(0, _("どの呪文の詠唱を中断しますか？(呪文 a-b, 'l'全て, ESC)",
                                        "Which spell do you stop casting? (Spell a-b, 'l' to all, ESC)"))
                        .build());
    lines.push_back(SmokeRow(1).put(25, _("名前", "Name")).build());
    lines.push_back(SmokeRow(2).put(22, _("a)  邪なる祝福", "a)  Evil blessing")).build());
    lines.push_back(SmokeRow(3).put(22, _("b)  凍結の刃", "b)  Frost weapon")).build());
    return lines;
}

//! 知識メニュー 1 ページ目（`cmd-knowledge.cpp:26`）。`(1)` 形式と `ESC)` 形式が混ざる。
std::vector<TermMirrorLine> make_knowledge_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(2).put(65, _("1/2 ページ", "page 1/2")).build());
    lines.push_back(SmokeRow(3).put(0, _("現在の知識を確認する", "Display current knowledge")).build());
    lines.push_back(SmokeRow(6).put(5, _("(1) 既知の伝説のアイテム                 の一覧", "(1) Display known artifacts")).build());
    lines.push_back(SmokeRow(7).put(5, _("(2) 既知のアイテム                       の一覧", "(2) Display known objects")).build());
    lines.push_back(SmokeRow(15).put(5, _("(0) *鑑定*済み装備の耐性                 の一覧", "(0) Display *identified* equip.")).build());
    lines.push_back(SmokeRow(17).put(8, _("-続く-", "-more-")).build());
    lines.push_back(SmokeRow(20).put(0, _("コマンド:", "Command: ")).build());
    lines.push_back(SmokeRow(21)
                        .put(1, _("ESC) 抜ける", "ESC) Exit menu"))
                        .put(30, _("SPACE) 次ページ", "SPACE) Next page"))
                        .build());
    return lines;
}

//! マクロの設定（`cmd-macro.cpp:144`）。`(1)` 形式だけ。
std::vector<TermMirrorLine> make_macro_menu_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(2).put(0, _("[ マクロの設定 ]", "Interact with Macros")).build());
    lines.push_back(SmokeRow(4).put(5, _("(1) ユーザー設定ファイルのロード", "(1) Load a user pref file")).build());
    lines.push_back(SmokeRow(5).put(5, _("(2) ファイルにマクロを追加", "(2) Append macros to a file")).build());
    // 実機ではミラーが行頭の `(` を落とすことがある（2026-07-29 に観測）。その形も拾えること。
    lines.push_back(SmokeRow(13).put(5, _(" 0) マクロ行動の入力", " 0) Enter a new action")).build());
    return lines;
}

/*!
 * @brief 自動拾いエディタのコマンドメニュー（`autopick-command-menu.cpp:38`）。
 * @details 階層メニュー。行 5 以降は**親の箱と子の箱が並ぶ**ので、
 * 右の箱（いま開いている階層）だけを拾うこと。
 */
std::vector<TermMirrorLine> make_autopick_menu_screen()
{
    std::vector<TermMirrorLine> lines;
    // 子メニュー（2 項目）が開いている状態。行 0 の範囲は**開いている階層**のもの。
    lines.push_back(SmokeRow(0).put(0, _("(a-b) コマンド:", "(a-b) Command:")).build());
    lines.push_back(SmokeRow(2).put(5, _("| a) ファイル       ▼ |", "| a) File           >  |")).build());
    lines.push_back(SmokeRow(3).put(5, _("| b) 編集           ▼ |", "| b) Edit           >  |")).build());
    lines.push_back(SmokeRow(4)
                        .put(5, _("| c) 検索           ▼ |", "| c) Search         >  |"))
                        .put(33, "+--------------------+")
                        .build());
    lines.push_back(SmokeRow(5)
                        .put(5, "|                     |")
                        .put(33, _("| a) 上書き保存    ^S |", "| a) Save file     ^S |"))
                        .build());
    lines.push_back(SmokeRow(6)
                        .put(5, "|                     |")
                        .put(33, _("| b) 終了          ^Q |", "| b) Quit          ^Q |"))
                        .build());
    return lines;
}

/*!
 * @brief ゲームオプションのページ（`cmd-gameoption.cpp:665`）。
 * @details 印は `》` でも黄色でもなく **`TERM_L_BLUE` の行**。方向キーと Enter は
 * コアが自分で処理するので、UI は枠を描くだけ（選択肢は 1 件も作らない）。
 */
std::vector<TermMirrorLine> make_option_page_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(0).put(0, _("キー入力オプション (リターン:次, y/n:変更, ESC:終了, ?:ヘルプ) ",
                                        "Input Options (RET:next, y/n:change, ESC:accept, ?:help) "))
                        .build());
    lines.push_back(SmokeRow(2).put(0, _("キー入力を使う                        : はい   (use_command)",
                                        "Use command menu                      : yes (use_command)"))
                        .build());
    lines.push_back(SmokeRow(3)
                        .put(0, _("矢印キーで移動する                    : いいえ (rogue_like_commands)",
                            "Rogue-like commands                   : no  (rogue_like_commands)"))
                        .color(TERM_L_BLUE)
                        .build());
    return lines;
}

/*!
 * @brief K-43: オプションの設定（最上位。`cmd-gameoption.cpp:444`）。
 * @details **コアのカーソル（`TERM_L_BLUE` の行）が動かない画面**なので、ui が選択肢を持つ。
 * キーは `toupper` されて `(R)` のように描かれる（コアは `tolower` で照合する）。
 * 見ているのは「行 1 の見出しで名指しできること」と「大文字のキーをそのまま拾うこと」。
 */
std::vector<TermMirrorLine> make_option_root_screen()
{
    std::vector<TermMirrorLine> lines;
    lines.push_back(SmokeRow(1).put(0, _("[ オプションの設定 ]", "Game options")).build());
    lines.push_back(SmokeRow(3)
                        .put(5, _("(1)     キー入力     オプション", "(1) Input Options"))
                        .color(TERM_L_BLUE)
                        .build());
    lines.push_back(SmokeRow(4).put(5, _("(2)    マップ画面    オプション", "(2) Map Screen Options")).build());
    lines.push_back(SmokeRow(9).put(5, _("(R)    プレイ記録    オプション", "(R) Play record Options")).build());
    lines.push_back(SmokeRow(11).put(5, _("(P) 自動拾いエディタ", "(P) Auto-picker/destroyer editor")).build());
    lines.push_back(SmokeRow(21)
                        .put(0, _("<方向>で移動, Enterで決定, ESCでキャンセル, ?でヘルプ: ",
                                 "Move to <dir>, Select to Enter, Cancel to ESC, ? to help: "))
                        .build());
    return lines;
}

/*! @} */

} // namespace

namespace {
//! コアの `askfor` に入っているか。`text_input_state_hook` が上げ下げする。
bool g_core_text_input_active = false;
}

void set_title_screen(bool active)
{
    g_title_screen = active;
}

void install_text_input_hook()
{
    text_input_state_hook = [](bool active) { g_core_text_input_active = active; };
}

bool is_core_text_input_active()
{
    return g_core_text_input_active;
}

int run_menu_choice_smoke()
{
    // SubSystem=Windows にコンソールは無い。pad_bind_smoke と同じくファイルへ残す。
    FILE *log = std::fopen("docs/menu_choice_smoke.log", "w");
    if (log == nullptr) {
        log = std::fopen("menu_choice_smoke.log", "w");
    }
    auto say = [&](const char *fmt, auto... args) {
        if (log != nullptr) {
            std::fprintf(log, fmt, args...);
            std::fputc('\n', log);
            std::fflush(log);
        }
    };
    say("# HENGBAND_SDL2_MENUCHOICE_SMOKE — K-16 選択肢抽出の自己検査");
    say("# 画面はコアの描画コードの書式そのまま（building-service.cpp / cmd-store.cpp / display-store.cpp）");

    std::vector<SmokeCase> cases;
    // 店: 商品一覧の a) b) c) は**この画面では効かない**ので拾わない。
    // `コマンド:` 行より下だけを拾い、`i/e)` `w/t)` は 2 キーに割る。
    cases.push_back({ "store_main", make_store_screen(false), false, "ESC,p,i,e,-,s,w,t,SP,x" });
    // 店の購入プロンプト: 商品一覧だけ（コマンド行の d) g) 等は連番から外れて落ちる）。
    // **K-20 の要**: 店の `input_stock`（store.cpp:171）はコアにカーソルが無い。
    // カーソル操作モードで `use_menu` を立てても、この画面は UI 側が持ち続けること。
    cases.push_back({ "store_item_prompt", make_store_screen(true), false, "a,b,c" });
    cases.push_back({ "building", make_building_screen(), false, "a,b,c,d,ESC" });
    // 持ち物一覧は「次の 1 キーがそのままコマンド」になる画面。1 件も拾ってはならない。
    cases.push_back({ "inventory_display", make_inventory_display_screen(), false, "" });
    cases.push_back({ "item_prompt", make_item_prompt_screen(), false, "b,c" });
    // K-27: レベルアップの能力値選択。手掛かりが無い画面なのでプロンプト文で名指しして拾う。
    // 末尾の `(現在値      3)` の `3)` を**拾わない**ことも同時に見ている。
    cases.push_back({ "stat_raise", make_stat_raise_screen(), false, "a,b,c,d,e,f" });
    // K-20: コアが `》` を出している画面は、コアがカーソルを持っている。UI は譲る。
    cases.push_back({ "command_menu", make_command_menu_screen(), true, "" });
    cases.push_back({ "spell_menu", make_spell_menu_screen(), true, "" });

    // ---- K-37: 名指しの選択画面。
    // 帰還先ダンジョン。**背後の建物（行 19〜23）の `d)` `ESC)` を拾わないこと**が要。
    cases.push_back({ "recall_dungeon", make_recall_dungeon_screen(), false, "a,b,c" });
    // 町テレポート。行も文字も飛ぶ（訪れていない町は出ない）ので連番を前提にしない。
    cases.push_back({ "tele_town", make_tele_town_screen(), false, "a,c" });
    // 闘技場の賭け。キーは数字。ここでも背後の建物を拾わないこと。
    cases.push_back({ "melee_arena", make_melee_arena_screen(), false, "1,2,3,4" });
    cases.push_back({ "monk_stance", make_monk_stance_screen(), false, "a,b,c" });
    // 呪術の中断。`'l'`（全て）と `ESC` は**本体の後ろ**に来ること（決定の初期位置が `a`）。
    cases.push_back({ "hex_stop", make_hex_stop_screen(), false, "a,b,l,ESC" });
    // 知識メニュー。`(1)` 形式と `ESC)` / `SPACE)` 形式が同じ画面に混ざる。
    cases.push_back({ "knowledge", make_knowledge_screen(), false, "1,2,0,ESC,SP" });
    cases.push_back({ "macro_menu", make_macro_menu_screen(), false, "1,2,0" });
    // 自動拾いエディタ。子メニューが開いている間、**親の項目を拾わないこと**。
    cases.push_back({ "autopick_menu", make_autopick_menu_screen(), false, "a,b" });
    // オプションページ。コアが `TERM_L_BLUE` でカーソルを持つので枠だけ描いて譲る。
    cases.push_back({ "option_page", make_option_page_screen(), true, "" });
    // K-43: 最上位のオプション画面は**逆**。コアのカーソルが動かないので ui が選択肢を持つ。
    // 大文字のキーをそのまま拾うこと、`TERM_L_BLUE` の行に引きずられて譲らないことを見る。
    cases.push_back({ "option_root", make_option_root_screen(), false, "1,2,R,P" });

    int failed = 0;
    for (const SmokeCase &c : cases) {
        GameFrame frame{};
        frame.menu_open = true;
        frame.pre_game_menu = false;
        frame.menu_term_lines = c.lines;
        fill_menu_choices(frame);

        const std::string got = keys_to_text(frame);
        const bool got_core_cursor = (frame.menu_core_cursor.line_index >= 0);
        const bool ok = (got == c.expect_keys) && (got_core_cursor == c.expect_core_cursor);
        if (!ok) {
            ++failed;
        }
        say("%-22s %s expect=\"%s\" got=\"%s\" core_cursor expect=%d got=%d", c.name, ok ? "PASS" : "FAIL",
            c.expect_keys, got.c_str(), c.expect_core_cursor ? 1 : 0, got_core_cursor ? 1 : 0);
        // コアカーソルの枠も行内に収まっていること（描画のずれの元）。
        if (got_core_cursor) {
            const auto &cc = frame.menu_core_cursor;
            const auto &text = frame.menu_term_lines[static_cast<size_t>(cc.line_index)].text_utf8;
            if (cc.span_begin < 0 || cc.span_len <= 0 ||
                static_cast<size_t>(cc.span_begin) + static_cast<size_t>(cc.span_len) > text.size()) {
                ++failed;
                say("  core_cursor RANGE-FAIL line=%d begin=%d len=%d", cc.line_index, cc.span_begin, cc.span_len);
            } else {
                say("  core_cursor line=%d span=\"%s\"", cc.line_index,
                    text.substr(static_cast<size_t>(cc.span_begin), static_cast<size_t>(cc.span_len)).c_str());
            }
        }

        // 位置（UTF-8 バイト範囲）が行からはみ出していないことも見る。描画のずれの元。
        for (const MenuChoice &choice : frame.menu_choices) {
            const auto &text = frame.menu_term_lines[static_cast<size_t>(choice.line_index)].text_utf8;
            const bool in_range = choice.span_begin >= 0 && choice.span_len > 0 &&
                static_cast<size_t>(choice.span_begin) + static_cast<size_t>(choice.span_len) <= text.size() &&
                choice.key_begin >= choice.span_begin && choice.key_len > 0 &&
                static_cast<size_t>(choice.key_begin) + static_cast<size_t>(choice.key_len) <= text.size();
            if (!in_range) {
                ++failed;
                say("  FAIL span out of range key=0x%02X", choice.key);
                continue;
            }
            const std::string span = text.substr(
                static_cast<size_t>(choice.span_begin), static_cast<size_t>(choice.span_len));
            const std::string key_text = text.substr(
                static_cast<size_t>(choice.key_begin), static_cast<size_t>(choice.key_len));
            say("  key=0x%02X key_text=\"%s\" span=\"%s\"", choice.key, key_text.c_str(), span.c_str());
        }
    }

    // ---- K-25: サブメニューの `》` は 1 行に 2 つ並ぶ（親＋子）。
    // 両方に枠を描くこと、そして**親の枠が子の項目まで伸びない**ことを見る
    // （伸びると 2 つの枠がつながった 1 本の帯に見える）。
    {
        // `input-key-requester.cpp` のコマンドメニューが実際に作る形（親の箱の中に子の箱）。
        // **枠線の前に空白は無い**（コアは箱を詰めて描く）。実機の形をそのまま使う。
        const std::string row = _("|  》情報|》階の雰囲気(^f)      ステータス(C)      |",
            "|  > Info|> Level feeling(^f)   Character(C)      |");
        const std::string parent = _("》情報", "> Info");
        const std::string child = _("》階の雰囲気(^f)", "> Level feeling(^f)");

        GameFrame frame{};
        frame.menu_open = true;
        TermMirrorLine l0{};
        l0.source_row = 3;
        l0.text_utf8 = row;
        frame.menu_term_lines.push_back(l0);

        fill_menu_choices(frame);
        std::string spans;
        bool in_range = true;
        for (const MenuChoice &cc : frame.menu_core_cursors) {
            if (cc.span_begin < 0 || cc.span_len <= 0 ||
                static_cast<size_t>(cc.span_begin) + static_cast<size_t>(cc.span_len) > row.size()) {
                in_range = false;
                continue;
            }
            if (!spans.empty()) {
                spans += '|';
            }
            spans += row.substr(static_cast<size_t>(cc.span_begin), static_cast<size_t>(cc.span_len));
        }
        const bool ok = in_range && (frame.menu_core_cursors.size() == 2) && (spans == parent + "|" + child);
        if (!ok) {
            ++failed;
        }
        say("submenu:%-14s %s n=%d spans=\"%s\" expect=\"%s|%s\"", "two_cursors", ok ? "PASS" : "FAIL",
            static_cast<int>(frame.menu_core_cursors.size()), spans.c_str(), parent.c_str(), child.c_str());
    }
    {
        // 項目名の中の `|`（`プレイ記録(|)`）で枠を切らないこと。
        const std::string row = _("|   》プレイ記録(|)", "|   > Play record(|)");
        const std::string picked = _("》プレイ記録(|)", "> Play record(|)");

        GameFrame frame{};
        frame.menu_open = true;
        TermMirrorLine l0{};
        l0.source_row = 7;
        l0.text_utf8 = row;
        frame.menu_term_lines.push_back(l0);

        fill_menu_choices(frame);
        const auto &cc = frame.menu_core_cursor;
        std::string span;
        if (cc.line_index == 0 && cc.span_len > 0 &&
            static_cast<size_t>(cc.span_begin) + static_cast<size_t>(cc.span_len) <= row.size()) {
            span = row.substr(static_cast<size_t>(cc.span_begin), static_cast<size_t>(cc.span_len));
        }
        const bool ok = (frame.menu_core_cursors.size() == 1) && (span == picked);
        if (!ok) {
            ++failed;
        }
        say("submenu:%-14s %s span=\"%s\" expect=\"%s\"", "pipe_in_label", ok ? "PASS" : "FAIL",
            span.c_str(), picked.c_str());
    }

    // ---- K-39: 意味のない Term カーソル（`_`）を消す。
    // **消しすぎてはいけない。** 文字入力中の `_` は編集位置を示す唯一の印である。
    {
        struct CursorCase {
            const char *name;
            std::vector<TermMirrorLine> lines;
            bool pre_game_menu;
            bool expect_visible;
        };
        std::vector<CursorCase> cursor_cases;

        // タイトル（`game-play.cpp` の pre_game_menu）。コアが `>` で選択行を示す。
        {
            std::vector<TermMirrorLine> lines;
            lines.push_back(SmokeRow(19).put(2, "Up/Down or 8/2  Enter=select  Esc=quit").build());
            lines.push_back(SmokeRow(20).put(3, "> N) New Game").color(TERM_L_BLUE).build());
            lines.push_back(SmokeRow(21).put(5, "L) Load").build());
            lines.push_back(SmokeRow(22).put(5, "Q) Quit").build());
            cursor_cases.push_back({ "title", lines, true, false });
        }
        // セーブ選択。同じく `>` で示す。
        {
            std::vector<TermMirrorLine> lines;
            lines.push_back(SmokeRow(1).put(2, "=== Load Game ===").build());
            lines.push_back(SmokeRow(3).put(3, "> a) DBG").color(TERM_L_BLUE).build());
            lines.push_back(SmokeRow(4).put(5, "b) PLAYER").build());
            lines.push_back(SmokeRow(5).put(5, "c) coon").build());
            cursor_cases.push_back({ "load_game", lines, true, false });
        }
        // 店（UI の選択肢が出ている）。枠が位置を示すので `_` は要らない。
        cursor_cases.push_back({ "store", make_store_screen(false), false, false });
        // コマンドメニュー（コアのカーソル）。同上。
        cursor_cases.push_back({ "command_menu", make_command_menu_screen(), false, false });
        /*
         * **名前入力**（birth）。`> ` の行も選択肢もコアカーソルも無い。
         * ここで消すと編集位置が分からなくなるので、`_` は**出したまま**にすること。
         */
        {
            std::vector<TermMirrorLine> lines;
            lines.push_back(SmokeRow(1).put(26, _("名前  : ふつうの", "Name  : Fuzzy")).build());
            lines.push_back(SmokeRow(3).put(1, _("性別     : 女性", "Sex      : Female")).build());
            lines.push_back(SmokeRow(23).put(1, _("名前を入力して下さい: ", "Enter your player's name: ")).build());
            cursor_cases.push_back({ "name_input", lines, true, true });
        }
        // 自動拾いエディタ（編集中）。カーソルが編集位置そのもの。
        {
            std::vector<TermMirrorLine> lines;
            lines.push_back(SmokeRow(1).put(0, _("この行はコメントです。", "This line is a comment.")).build());
            lines.push_back(SmokeRow(2).put(0, "?:$RACE").build());
            cursor_cases.push_back({ "autopick_edit", lines, false, true });
        }

        for (const CursorCase &c : cursor_cases) {
            GameFrame frame{};
            frame.menu_open = true;
            frame.pre_game_menu = c.pre_game_menu;
            frame.menu_term_lines = c.lines;
            fill_menu_choices(frame);
            frame.menu_term_curs_col = 12;
            //! 行は**添字**（Term 行ではない）。キャレットは最後の行＝プロンプトの上。
            frame.menu_term_curs_row = static_cast<int>(frame.menu_term_lines.size()) - 1;
            suppress_meaningless_term_cursor(frame);

            const bool visible = (frame.menu_term_curs_col >= 0);
            const bool ok = (visible == c.expect_visible);
            if (!ok) {
                ++failed;
            }
            say("cursor:%-14s %s expect_visible=%d got=%d", c.name, ok ? "PASS" : "FAIL",
                c.expect_visible ? 1 : 0, visible ? 1 : 0);
        }
    }

    // ---- K-20: y/n プロンプトの判定（`asking-player.cpp:226-234` の書式そのまま）
    // ここを緩めると**通常のメッセージ表示中にキーを飲み込む**ので、
    // 「拾ってはいけない行」を必ず一緒に検査する。
    struct PromptCase {
        const char *name;
        std::string line;
        const char *expect_keys; //!< "y,n" / "o,c" / "" （空＝プロンプトではない）
    };
    std::vector<PromptCase> prompt_cases;
    // 積むキーは**画面に出ている文字そのもの**（`input_check_strict` は大文字小文字の
    // どちらも受ける）。枠を重ねる位置と同じ文字なので、見えているものと積むものが一致する。
    prompt_cases.push_back({ "yn", std::string(_("本当に自殺しますか？", "Do you really want to commit suicide? ")) + "[y/n]", "y,n" });
    prompt_cases.push_back({ "default_y", std::string(_("よろしいですか？", "Are you sure? ")) + "[Y/n]", "Y,n" });
    prompt_cases.push_back({ "okay_cancel", std::string(_("確認", "Confirm")) + "[(O)k/(C)ancel]", "O,C" });
    // 末尾に空白が付いた状態（prt は行末まで空白で埋める）でも拾えること。
    prompt_cases.push_back({ "yn_trailing_space", std::string(_("店を出ますか？", "Leave the store? ")) + "[y/n]     ", "y,n" });
    // 拾ってはいけない行。
    prompt_cases.push_back({ "message", _("あなたはオークを倒した。", "You have slain the orc."), "" });
    prompt_cases.push_back({ "more", _("-続く-", "-more-"), "" });
    prompt_cases.push_back({ "empty", "", "" });
    // 途中に [y/n] があるだけの行（行末一致でないので拾わない）。
    prompt_cases.push_back({ "yn_not_at_end", std::string("[y/n] ") + _("と表示された", "was shown"), "" });
    // K-37: 矢弾の作成（`mind-archer.cpp:53`）。`[X]` 形式の 1 行コマンドプロンプト。
    prompt_cases.push_back({ "bracket_ammo",
        _("[S]弾, [A]矢, [B]クロスボウの矢 :", "Create [S]hots, Create [A]rrow or Create [B]olt ?"), "S,A,B" });
    prompt_cases.push_back({ "bracket_ammo_low", _("[S]弾:", "Create [S]hots ?"), "S" });
    // 拾ってはいけない行。`[` があるのに `[英数字]` の形でないものは全部落とす。
    prompt_cases.push_back({ "bracket_pause", _("[ 何かキーを押して下さい ]", "[ Press any key ]"), "" });
    prompt_cases.push_back({ "bracket_item_name",
        _("鉄の靴 [4,+0] を装備しますか?", "Wear the Iron Shod Boots [4,+0]?"), "" });
    prompt_cases.push_back({ "bracket_no_tail", _("[S]弾, [A]矢", "Create [S]hots, Create [A]rrow"), "" });

    for (const PromptCase &c : prompt_cases) {
        PromptBar bar{};
        parse_prompt_line(c.line, bar);
        std::string got;
        for (const PromptChoice &choice : bar.choices) {
            if (!got.empty()) {
                got += ',';
            }
            got += static_cast<char>(choice.key);
        }
        bool ok = (got == c.expect_keys);

        // K-22: `[y/n]` の文字へ枠を重ねるので、**位置がその文字そのもの**を指すこと。
        // ここがずれると枠が別の文字に乗る。
        std::string spans;
        for (const PromptChoice &choice : bar.choices) {
            const bool in_range = choice.begin >= 0 && choice.len > 0 &&
                static_cast<size_t>(choice.begin) + static_cast<size_t>(choice.len) <= c.line.size();
            if (!in_range) {
                ok = false;
                continue;
            }
            const std::string span = c.line.substr(
                static_cast<size_t>(choice.begin), static_cast<size_t>(choice.len));
            if (span.size() != 1 || span[0] != static_cast<char>(choice.key)) {
                ok = false;
            }
            if (!spans.empty()) {
                spans += ',';
            }
            spans += span;
        }
        if (!ok) {
            ++failed;
        }
        say("prompt:%-16s %s expect=\"%s\" got=\"%s\" spans=\"%s\" line=\"%s\"", c.name, ok ? "PASS" : "FAIL",
            c.expect_keys, got.c_str(), spans.c_str(), c.line.c_str());
    }

    // ---- K-22: 数値入力の判定（`asking-player.cpp:346,384` の書式そのまま）
    struct NumericCase {
        const char *name;
        std::string line;
        bool expect_active;
        int min;
        int max;
        int value;
        int digits;
    };
    std::vector<NumericCase> numeric_cases;
    // input_quantity: `_("いくつですか (1-%d): ", "Quantity (1-%d): ")` ＋ 初期値 "1"
    numeric_cases.push_back({ "quantity", _("いくつですか (1-4): 1", "Quantity (1-4): 1"), true, 1, 4, 1, 1 });
    // 全部消した状態（バッファが空）は最小値扱い。
    numeric_cases.push_back({ "quantity_empty", _("いくつですか (1-4): ", "Quantity (1-4): "), true, 1, 4, 1, 1 });
    // input_integer: `<文言>(%d-%d): `。桁カーソルは max の桁数まで動く。
    numeric_cases.push_back({ "integer", _("寄付する金額(1-10000): 250", "Donation(1-10000): 250"), true, 1, 10000, 250, 5 });
    // prt が行末まで空白で埋めた状態でも読めること。
    numeric_cases.push_back({ "trailing_space", _("いくつですか (1-99): 12   ", "Quantity (1-99): 12   "), true, 1, 99, 12, 2 });
    // 拾ってはいけないもの。
    numeric_cases.push_back({ "item_prompt", _("(持ち物:b-c, ESC) どの巻物を読みますか?", "(Inven b-c, ESC) Read which scroll? "), false, 0, 0, 0, 0 });
    numeric_cases.push_back({ "store_prompt", _("(商品:a-l, ESCで中断) どの品物が欲しいんだい? ", "(Items a-l, ESC to exit) Which item? "), false, 0, 0, 0, 0 });
    // 英字を打った状態（input_quantity は英字＝最大値扱い）はコアに譲る。
    numeric_cases.push_back({ "alpha_typed", _("いくつですか (1-4): a", "Quantity (1-4): a"), false, 0, 0, 0, 0 });
    numeric_cases.push_back({ "message", _("あなたはオークを倒した。", "You have slain the orc."), false, 0, 0, 0, 0 });
    // 括弧はあるが `): ` で閉じていない行。
    numeric_cases.push_back({ "range_only", _("残り (1-4) 個", "left (1-4) items"), false, 0, 0, 0, 0 });

    for (const NumericCase &c : numeric_cases) {
        NumericInput ni{};
        parse_numeric_input_line(c.line, ni);
        bool ok = (ni.active == c.expect_active) &&
            (!c.expect_active ||
                (ni.min == c.min && ni.max == c.max && ni.value == c.value && ni.digits == c.digits));

        // K-22: 元の画面の数字へ枠を重ねるので、**位置が行の中の数字そのもの**を
        // 指していることまで見る（ここがずれると枠が別の文字に乗る）。
        std::string span;
        if (ni.active) {
            const bool in_range = ni.value_begin >= 0 && ni.value_len >= 0 &&
                static_cast<size_t>(ni.value_begin) + static_cast<size_t>(ni.value_len) <= c.line.size();
            if (!in_range) {
                ok = false;
            } else {
                span = c.line.substr(static_cast<size_t>(ni.value_begin), static_cast<size_t>(ni.value_len));
                const std::string expect_span = (c.value == c.min && ni.value_len == 0) ? "" : std::to_string(c.value);
                if (ni.value_len != 0 && span != expect_span) {
                    ok = false;
                }
            }
        }
        if (!ok) {
            ++failed;
        }
        say("numeric:%-15s %s expect=(%d,%d..%d,v=%d,d=%d) got=(%d,%d..%d,v=%d,d=%d) span=\"%s\" line=\"%s\"",
            c.name, ok ? "PASS" : "FAIL", c.expect_active ? 1 : 0, c.min, c.max, c.value, c.digits,
            ni.active ? 1 : 0, ni.min, ni.max, ni.value, ni.digits, span.c_str(), c.line.c_str());
    }

    // ---- K-23: birth 画面のカーソル（コアは**色だけ**で選択中を示す）
    // 種族・職業・性格・魔法領域・性別・オートローラーはすべて `c_put_str(TERM_YELLOW, ...)`。
    // ミラーの 1 行 1 色では消えてしまうので、行内の黄色の位置を別に持って枠を重ねる。
    {
        const std::string row12 = _("a) 人間           b) ハーフエルフ", "a) Human          b) Half-Elf");
        const std::string row13 = _("f) ホビット       g) ノーム", "f) Hobbit         g) Gnome");
        const std::string picked = _("g) ノーム", "g) Gnome");

        GameFrame frame{};
        frame.menu_open = true;
        frame.pre_game_menu = true;
        // K-39: 「選択画面である」手掛かり（`birth-select-race.cpp:119`）。
        // これが無い画面（キャラクターシート）では黄色を見ない。
        frame.menu_term_lines.push_back(
            SmokeRow(10).put(10, _("種族を選んで下さい (a-L) ('='初期オプション設定):",
                                  "Choose a race (a-L) ('=' for options): "))
                .build());
        TermMirrorLine l0{};
        l0.source_row = 12;
        l0.text_utf8 = row12;
        TermMirrorLine l1{};
        l1.source_row = 13;
        l1.text_utf8 = row13;
        l1.highlight_begin = static_cast<int>(row13.find(picked));
        l1.highlight_len = static_cast<int>(picked.size());
        frame.menu_term_lines.push_back(l0);
        frame.menu_term_lines.push_back(l1);

        fill_menu_choices(frame);
        const auto &cc = frame.menu_core_cursor;
        std::string span;
        if (cc.line_index >= 0 && cc.span_len > 0 &&
            static_cast<size_t>(cc.span_begin) + static_cast<size_t>(cc.span_len) <= row13.size()) {
            span = row13.substr(static_cast<size_t>(cc.span_begin), static_cast<size_t>(cc.span_len));
        }
        // birth では選択肢を注入しない（コアが方向キーと Enter を自分で処理する）。
        const bool ok = (cc.line_index == 2) && (span == picked) && frame.menu_choices.empty();
        if (!ok) {
            ++failed;
        }
        say("birth:%-16s %s line=%d span=\"%s\" expect=\"%s\" choices=%d", "race_cursor",
            ok ? "PASS" : "FAIL", cc.line_index, span.c_str(), picked.c_str(),
            static_cast<int>(frame.menu_choices.size()));
    }
    {
        /*
         * K-39: **キャラクターシートで黄色を拾わないこと。** 名前入力と生い立ち編集は
         * シートの上に出る。ここの黄色は「良い能力値」の意味なので、枠を出すと
         * 関係ない行が選ばれているように見えるうえ、
         * `suppress_meaningless_term_cursor` が文字入力のキャレットまで消してしまう。
         */
        const std::string row12 = _(" 打撃回数              1+0                           魔法防御  : 41-良い",
            " Blows/Round           1+0                           Saving Throw : Good");
        const std::string yellow = _(" 41-良い", " Good");

        GameFrame frame{};
        frame.menu_open = true;
        frame.pre_game_menu = true;
        frame.menu_term_lines.push_back(
            SmokeRow(0).put(0, _("キャラクターの名前を入力して下さい:", "Enter your player's name: ")).build());
        TermMirrorLine sheet{};
        sheet.source_row = 12;
        sheet.text_utf8 = row12;
        sheet.highlight_begin = static_cast<int>(row12.find(yellow));
        sheet.highlight_len = static_cast<int>(yellow.size());
        frame.menu_term_lines.push_back(sheet);

        fill_menu_choices(frame);
        frame.menu_term_curs_col = 40;
        frame.menu_term_curs_row = 0; //!< 添字 0＝名前を聞いている行
        suppress_meaningless_term_cursor(frame);

        const bool ok = (frame.menu_core_cursor.line_index < 0) && frame.menu_choices.empty() &&
            (frame.menu_term_curs_col >= 0);
        if (!ok) {
            ++failed;
        }
        say("birth:%-16s %s core_cursor=%d choices=%d caret_visible=%d（枠は出さず `_` は残す）",
            "char_sheet", ok ? "PASS" : "FAIL", frame.menu_core_cursor.line_index,
            static_cast<int>(frame.menu_choices.size()), (frame.menu_term_curs_col >= 0) ? 1 : 0);
    }
    {
        // K-24: ロール確認 `['r' …, Enter …]`。**引用符付きの英字**を拾い、Enter も選択肢にする。
        // 書式は birth-wizard.cpp:452-465 そのまま。
        const std::string roll = _("['r' 次の数値, 'h' 生い立ちを表示, Enter この数値に決定]",
            "['r'eroll, 'h'istory, or Enter to accept]");
        GameFrame frame{};
        frame.menu_open = true;
        frame.pre_game_menu = true;
        TermMirrorLine l0{};
        l0.source_row = 23;
        l0.text_utf8 = roll;
        frame.menu_term_lines.push_back(l0);

        fill_menu_choices(frame);
        std::string got;
        for (const MenuChoice &c : frame.menu_choices) {
            if (!got.empty()) {
                got += ',';
            }
            got += (c.key == '\r') ? std::string("CR") : std::string(1, static_cast<char>(c.key));
        }
        bool ok = (got == "r,h,CR");
        for (const MenuChoice &c : frame.menu_choices) {
            const bool in_range = c.span_begin >= 0 && c.span_len > 0 &&
                static_cast<size_t>(c.span_begin) + static_cast<size_t>(c.span_len) <= roll.size() &&
                c.key_begin >= c.span_begin && c.key_len > 0 &&
                static_cast<size_t>(c.key_begin) + static_cast<size_t>(c.key_len) <= roll.size();
            if (!in_range) {
                ok = false;
                continue;
            }
            const std::string key_text = roll.substr(
                static_cast<size_t>(c.key_begin), static_cast<size_t>(c.key_len));
            const std::string expect = (c.key == '\r') ? "Enter" : std::string(1, static_cast<char>(c.key));
            if (key_text != expect) {
                ok = false;
            }
        }
        if (!ok) {
            ++failed;
        }
        say("birth:%-16s %s expect=\"r,h,CR\" got=\"%s\" line=\"%s\"", "roll_prompt",
            ok ? "PASS" : "FAIL", got.c_str(), roll.c_str());
    }
    {
        // **拾ってはいけない行。** birth のほとんどの画面に出る。`(` 始まりなので当たらない。
        // 当たると 'Q'（終了）を積める選択肢にしてしまう。
        GameFrame frame{};
        frame.menu_open = true;
        frame.pre_game_menu = true;
        TermMirrorLine l0{};
        l0.source_row = 8;
        l0.text_utf8 = _("キャラクターを作成します。('S'やり直す, 'Q'終了, '?'ヘルプ)",
            "Please select your character traits from menus below:");
        frame.menu_term_lines.push_back(l0);

        fill_menu_choices(frame);
        const bool ok = frame.menu_choices.empty();
        if (!ok) {
            ++failed;
        }
        say("birth:%-16s %s choices=%d（'Q' を拾ってはいけない）", "help_line", ok ? "PASS" : "FAIL",
            static_cast<int>(frame.menu_choices.size()));
    }
    {
        // 黄色が無い画面（タイトル等）ではカーソルを作らない。
        GameFrame frame{};
        frame.menu_open = true;
        frame.pre_game_menu = true;
        TermMirrorLine l0{};
        l0.source_row = 20;
        l0.text_utf8 = "  > N) New Game";
        frame.menu_term_lines.push_back(l0);

        fill_menu_choices(frame);
        const bool ok = (frame.menu_core_cursor.line_index < 0) && frame.menu_choices.empty();
        if (!ok) {
            ++failed;
        }
        say("birth:%-16s %s line=%d choices=%d", "no_yellow", ok ? "PASS" : "FAIL",
            frame.menu_core_cursor.line_index, static_cast<int>(frame.menu_choices.size()));
    }

    say("");
    say("RESULT failed=%d", failed);
    if (log != nullptr) {
        std::fclose(log);
    }
    return failed;
}

} // namespace presentation
