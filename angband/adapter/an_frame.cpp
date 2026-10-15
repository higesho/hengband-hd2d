/*!
 * @file an_frame.cpp
 * @brief `an_frame.h` の実体（設計 §6.2。S2／見直しで S3 と並行して修正）。
 */
#include "an_frame.h"

#include "an_map_shim.h"
#include "an_shim.h"
#include "an_lang.h"
#include "an_wide.h"

#include "bridge/named_attack_fx.h"
#include "frame/cell_feature_bits.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

//! 魔法・投射物の記録で、物の番号を引くための名寄せ表（`fill_game_frame()` が毎フレーム据える。
//! `an_named_fx_missile_step()` は原作の中から呼ばれるので、引数で受け取れない）。
static const an::TileMapping *g_fx_mapping = nullptr;

namespace an {

namespace {

//! 表示するビューポートの寸法。`ui-term.c` の `SIDEBAR_LEFT`（`COL_MAP`=13・上下 1 行ずつ）
//! と同じ広さ（80x27 の Term から左 13 桁・上下各 1 行を除いた分）。
constexpr int kViewCols = 67;
constexpr int kViewRows = 25;

/*!
 * @brief 見直しの指摘 9。`cell_feature_bits.h` の契約どおりに翻訳し直した。
 * @details 手本は `presentation/bridge/presentation_bridge.cpp` の
 * `translate_feature_flags(const TerrainType&)`。
 * - 扉は開閉問わず `CELL_FEAT_DOOR` を立て、開いている／壊れている扉だけ追加で
 *   `CELL_FEAT_DOOR_OPEN` を立てる（旧実装は閉じた扉だけ `DOOR`、開いた扉だけ
 *   `DOOR_OPEN` で、両方を同時に持つ扉が無いという前提が間違っていた）。
 * - `CELL_FEAT_PASSABLE`（「通り抜けられる床」）は壁・扉・岩を明示的に除く。
 *   Angband の開いた扉・壊れた扉は `TF_PASSABLE` を持つが、これは「扉そのもの」
 *   であって「扉の隣の床」ではないので除かないと扉の向き判定が壊れる。
 * - `CELL_FEAT_RUBBLE` は Angband に `TF_TREE`/`TF_RUBBLE` が無いので、
 *   `TF_ROCK` かつ壁でも扉でもないマス（`RUBBLE`/`PASS_RUBBLE`）で見る
 *   （`presentation_bridge.cpp` が `TerrainCharacteristics::STONE` で見ているのと同じ考え方）。
 * - `CELL_FEAT_LAVA` は `TF_FIERY`、`CELL_FEAT_GLOW` は `TF_BRIGHT`
 *   （`lib/gamedata/terrain.txt` の `LAVA` 定義に両方載っている）。
 * - Angband に立ち木・水面は無いので `CELL_FEAT_TREE`/`CELL_FEAT_WATER` は立てない。
 */
/*!
 * @brief AH-25: `an_term_colors()` が返す桁ごとの色から、`color_spans` の並びを作る。
 * @details 状態列・最下段は**桁と色の両方に意味がある**（`TermColorSpan` の註記どおり。
 * 体力バーの色つきの塗り分け・朦朧などの状態異常の色・速度の色つき数字）。以前は
 * `colors[0]`（先頭桁の色。たいていは白）だけを行全体の色として使っていたので、
 * これらの色分けがまるごと消えていた。
 *
 * @note 区間の `begin`/`len` は `text` の**バイト位置**（画面側は `substr()` で切り出す）で、
 * 色は桁ごと（`colors[桁]`）。英語では 1 桁 = 1 バイトなので両者は同じ。日本語（P4。
 * ）では全角が 2 桁・3 バイトなので、字ごとに桁を数えて
 * その字の最初の桁の色を取る（区間は字の境で切る）。以前は非 ASCII があれば区間を
 * 作らず 1 色に落としていた（状態列の札を日本語にすると値の色が消えた）。
 */
std::vector<TermColorSpan> build_color_spans(const char *text, int text_len, const unsigned char *colors, int color_len)
{
    std::vector<TermColorSpan> spans;
    if ((text_len <= 0) || (color_len <= 0)) {
        return spans;
    }
    // 字ごとに（バイトの頭・バイト数・色）を並べる。
    struct Glyph {
        int begin;
        int len;
        unsigned char color;
    };
    std::vector<Glyph> glyphs;
    glyphs.reserve(static_cast<std::size_t>(text_len));
    int col = 0;
    int i = 0;
    while (i < text_len) {
        const unsigned char c0 = static_cast<unsigned char>(text[i]);
        int len = 1;
        unsigned long cp = c0;
        if (c0 >= 0xF0) {
            len = 4;
            cp = c0 & 0x07u;
        } else if (c0 >= 0xE0) {
            len = 3;
            cp = c0 & 0x0Fu;
        } else if (c0 >= 0xC0) {
            len = 2;
            cp = c0 & 0x1Fu;
        }
        if (i + len > text_len) {
            len = text_len - i;
        }
        for (int k = 1; k < len; ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
        }
        const int width = (len > 1 && an_wcwidth(cp) == 2) ? 2 : 1;
        if (col >= color_len) {
            break;
        }
        glyphs.push_back(Glyph{ i, len, colors[col] });
        col += width;
        i += len;
    }
    std::size_t g = 0;
    while (g < glyphs.size()) {
        std::size_t h = g + 1;
        while ((h < glyphs.size()) && (glyphs[h].color == glyphs[g].color)) {
            ++h;
        }
        TermColorSpan span;
        span.begin = glyphs[g].begin;
        span.len = (glyphs[h - 1].begin + glyphs[h - 1].len) - glyphs[g].begin;
        span.color = glyphs[g].color;
        spans.push_back(span);
        g = h;
    }
    // 1 区間だけ（＝行全体が同じ色）なら、`line.color` に任せて `color_spans` は空にする
    // （`TermColorSpan` の契約「空なら行全体が color」に合わせ、無駄なデータを送らない）。
    if (spans.size() <= 1) {
        return {};
    }
    return spans;
}

uint16_t translate_feature_flags(unsigned int tf_bits, bool room, bool glowing, bool known)
{
    unsigned flags = 0;
    if (tf_bits & AN_TF_WALL) flags |= CELL_FEAT_WALL;
    if (tf_bits & AN_TF_PERMANENT) flags |= CELL_FEAT_PERMANENT;
    if (tf_bits & AN_TF_DOOR_ANY) {
        flags |= CELL_FEAT_DOOR;
        if (!(tf_bits & AN_TF_DOOR_CLOSED)) {
            flags |= CELL_FEAT_DOOR_OPEN; // 開いた扉・壊れた扉（DOOR_CLOSED を持たない DOOR_ANY）。
        }
    }
    // `TF_ROCK` かつ壁でも扉でもないマスは瓦礫（`RUBBLE`/`PASS_RUBBLE`）。
    // どちらも見た目は岩の板（`CELL_FEAT_RUBBLE`）だが、**通れる瓦礫は歩ける**
    // ——`AN_TF_PASSABLE` の有無で区別する（罠: 旧実装は瓦礫を一律 blocking 扱いにして
    // いたため、通れる瓦礫の上を歩けなかった）。
    const bool is_rock = (tf_bits & AN_TF_ROCK) && !(flags & (CELL_FEAT_WALL | CELL_FEAT_DOOR));
    if (is_rock) {
        flags |= CELL_FEAT_RUBBLE;
    }
    if (tf_bits & AN_TF_STAIR) flags |= CELL_FEAT_STAIRS;
    if (tf_bits & AN_TF_FIERY) flags |= CELL_FEAT_LAVA;
    if (tf_bits & AN_TF_BRIGHT) flags |= CELL_FEAT_GLOW;
    if (room) flags |= CELL_FEAT_ROOM;
    if (glowing) flags |= CELL_FEAT_GLOWING;
    if (known) flags |= CELL_FEAT_KNOWN;

    const bool passable_rock = is_rock && ((tf_bits & AN_TF_PASSABLE) != 0u);
    const bool blocking
        = ((flags & (CELL_FEAT_WALL | CELL_FEAT_DOOR)) != 0u) || (is_rock && !passable_rock);
    if (!blocking && (tf_bits & AN_TF_PASSABLE)) {
        flags |= CELL_FEAT_PASSABLE;
    }
    return static_cast<uint16_t>(flags);
}

void fill_status_and_bottom(GameFrame &frame)
{
    int cols = 0;
    int rows = 0;
    if (an_term_size(&cols, &rows) != AN_OK) {
        return;
    }

    /*
     * 見直しの指摘（T4「見る」の詳細を確かめていて発覚）:
     * 原作 `ui-term.h` の `ROW_MAP`（`row_top_map[SIDEBAR_LEFT] == 1`。`ui-term.c`）が
     * 言うとおり、**状態列（左 `AN_STATUS_COL_COLS` 桁）は Term の行 1 から始まる**——
     * 行 0 は原作のメッセージの行である（`ui-command.c` の `prt("Type '?' for help.", 0, 0)`
     * ほか）。ここが行 0 から読んでいたため、メッセージが載っていない普段は空行が
     * 状態列の先頭に紛れ込むだけで実害が無かったが、メッセージが行 0 に出ている瞬間
     * （「見る」の詳細（設計 §8.5.1）でメッセージパネルに送った文を含む）を撮ると、
     * 状態列の 1 行目にその断片が写り込んで見えた。
     */
    constexpr int kStatusColFirstRow = 1; // ROW_MAP（row_top_map[SIDEBAR_LEFT]）。
    frame.status_col_cols = AN_STATUS_COL_COLS;
    char text[256];
    unsigned char colors[256];
    /*
     * の 3。状態列は Term の左 `AN_STATUS_COL_COLS` 桁を
     * そのまま読んでいるので、ヘルプ・人物表・店のように**画面全体を覆う画**を開いて
     * いる間（`screen_save()` の後に Term 全体を描き直す。`frame.menu_open` が真）は、
     * その画の左端（ヘルプの本文・人物表の欄名）が状態列へ写り込んでいた。原作も
     * 画を閉じるまで状態列を描き直さない（`screen_load()` で元へ戻す）ので、
     * メニューを開いている間は**最後にメニューの外で読んだ状態列**をそのまま送る。
     * まだ 1 度も読んでいない（誕生の直後など）ときだけ、今までどおり Term を読む。
     */
    static std::vector<SubPanelLine> last_status_col_lines;
    if (frame.menu_open && !frame.pre_game_menu && !last_status_col_lines.empty()) {
        frame.status_col_lines = last_status_col_lines;
    } else {
    frame.status_col_lines.reserve(static_cast<std::size_t>(std::max(0, rows - kStatusColFirstRow)));
    for (int y = kStatusColFirstRow; y < rows; ++y) {
        SubPanelLine line;
        const int n = an_term_row_cols(y, 0, AN_STATUS_COL_COLS, text, static_cast<int>(sizeof(text)));
        if (n > 0) {
            line.text_utf8 = text;
        }
        const int color_n = an_term_colors(y, colors, static_cast<int>(sizeof(colors)));
        if (color_n > 0) {
            line.color = colors[0];
            // AH-25。桁ごとに色が違う行（体力バー・状態異常の並びなど）を区間で持ち回す。
            line.color_spans = build_color_spans(text, n, colors, color_n);
        }
        frame.status_col_lines.push_back(std::move(line));
    }
    if (!frame.menu_open && !frame.pre_game_menu) {
        last_status_col_lines = frame.status_col_lines;
    }
    }

    // 最下段（設計 §6.2「左の状態列・最下段」）。
    const int bottom_row = rows - 1;
    const int n = an_term_row(bottom_row, text, static_cast<int>(sizeof(text)));
    if (n > 0) {
        TermTextRun run;
        run.col = 0;
        run.text_utf8 = text;
        const int color_n = an_term_colors(bottom_row, colors, static_cast<int>(sizeof(colors)));
        if (color_n > 0) {
            run.color = colors[0];
            // AH-25。速度・学習・階層など、右端の決まった桁に別の色で出る値を保つ。
            run.color_spans = build_color_spans(text, n, colors, color_n);
        }
        frame.bottom_row_runs.push_back(std::move(run));
    }
    frame.bottom_row_cols = cols;
}

void fill_hud(GameFrame &frame)
{
    an_hud_info hud{};
    if (!an_get_hud(&hud)) {
        return;
    }
    frame.hud.name = hud.name;
    frame.hud.hp = hud.hp;
    frame.hud.hp_max = hud.hp_max;
    frame.hud.sp = hud.sp;
    frame.hud.sp_max = hud.sp_max;
    frame.hud.gold = hud.gold;
    frame.hud.depth = hud.depth;
    frame.hud.level = hud.level;
}


/*!
 * @brief 正体不明のアイテムの tile_index を決める。
 *
 * @details 決めたこと（2026-09-23）:
 * - 種類そのものが分からないアイテム（`object_flavor_is_aware()` が偽。ここでは
 *   `object_kind::aware` を直接見た値 = `info.object_aware`）は、**分類ごとの
 *   代表のボクセル板**で描く。武器・防具のように最初から種類が分かる物は対象外
 *   （そもそも `object_category` が `AN_ITEM_CATEGORY_NONE` になる）。
 * - 識別済み、または対象外の分類は、今までどおり名前で引く。
 *
 * ## 代表の番号は暫定
 * 分類名 → 変愚の K 番号の対応表は、本来 `presentation/` に 1 か所だけ置く
 * 予定だが、まだ存在しない。**その表が
 * できるまでの間、ここに同じ値を仮に持つ**——表ができたら、このテーブルを
 * 削って `presentation::` 側の関数を呼ぶ形に差し替えること。
 *
 * ## 8 分類に当たらない分類
 * Angband 4.2.6 で「見た目だけ分かる」flavor 付きの tval は
 * potion/scroll/ring/amulet/wand/staff/rod/mushroom の 8 つだけで、それ以外は
 * 無い（`obj-properties.h` の tval 一覧を確認）。したがって「8 分類に当たらない
 * 見た目だけ分かる分類」は Angband には無く、報告すべき追加分類も無い。
 */
/*!
 * @brief `fill_cells()` が既に引いた `object_lookup`（識別済み・8 分類の外の
 * 名前照合結果）を受け取り、未識別のときだけ代表タイルへ差し替える。
 * @details 見直しの指摘 11（`lookup_object()` の二重呼び出し）:
 * 以前はここでも `mapping.lookup_object()` を呼び直していたが、呼び出し元が
 * `cell.object_id` のために既に引いた結果があるので、それをそのまま使い回す。
 */
uint16_t an_object_tile(const an_cell_info &info, const TileMapping &mapping, const TileLookup &object_lookup)
{
    if (!info.has_object) {
        return 0;
    }
    if (!info.object_aware && (info.object_category != AN_ITEM_CATEGORY_NONE)
        && (info.object_category <= AN_ITEM_CATEGORY_MUSHROOM)) {
        /*
         * 代表表は共有の `presentation/bridge/unaware_item_tiles.h` の 1 か所だけに
         * ある（見直しの指摘 2 で、ここに変愚の K 番号を直接返す暫定実装を削った）。
         * `AN_ITEM_CATEGORY_*` の 1..8 は `UnawareItemCategory` と値をそろえてある
         * （`an_map_shim.h` のコメント参照）。`tile_index` はこの目録が振った通し番号
         * であって、K 番号そのものではない（契約 §2 の 4。K229 をそのまま返すと、
         * このコアの目録の 229 番＝別の絵（敵など）を指してしまう）。
         */
        const auto category = static_cast<presentation::UnawareItemCategory>(info.object_category);
        const std::uint16_t tile_index = mapping.lookup_unaware_representative(category);
        if (tile_index != 0) {
            return tile_index;
        }
        // 代表が引けなかった：種類の板へ落ちてよい（契約 §2 の 7）。下へ流れる。
    }
    // 識別済み、または上の 8 分類に当たらない（＝武器・防具など）：呼び出し元が
    // 既に引いた分類＋名前の照合結果をそのまま使う。
    return object_lookup.tile_index;
}

void fill_cells(GameFrame &frame, const TileMapping &mapping)
{
    if (!an_map_ready()) {
        return;
    }
    int player_gx = 0;
    int player_gy = 0;
    if (!an_map_player(&player_gx, &player_gy)) {
        return;
    }
    int dungeon_w = 0;
    int dungeon_h = 0;
    an_map_dims(&dungeon_w, &dungeon_h);

    frame.player_gx = player_gx;
    frame.player_gy = player_gy;

    /*
     * AH-33。`L`（`do_cmd_locate`。原作の
     * `ui-knowledge.c`）は `Term->offset_x`/`offset_y`（見ている区画）だけを
     * 動かし、`@` は動かさない——このカメラはふつう `@` を追うので、`L` の
     * 最中は原作の Term がよそを見せているのに絵は動かなかった（監査記録
     * 監21）。手本は `silq/adapter/sq_frame.cpp`——同じ症状（気づいたこと
     * 2026-08-26）に `frame.camera_detached`（`presentation/frame/game_frame.h`。
     * 「コアが @ から離れた所を見ている」）を新設して直した。Sil-Q は原作へ
     * 専用の旗（`sq_locate_shifted`）を足して見分けたが、Angband の A1〜A6
     * の手入れの表にその余地が無いので、AH-10 で使った「行 0 が
     * `"Map sector"` で始まる」判定（`an_locate_active()`。`an_map_shim.c`）を
     * 使い回す——原作は 1 バイトも変えていない。
     */
    int ox;
    int oy;
    const bool locating = an_locate_active() != 0;
    frame.camera_detached = locating;
    if (locating) {
        int panel_x = 0;
        int panel_y = 0;
        an_panel_offset(&panel_x, &panel_y);
        ox = panel_x;
        oy = panel_y;
    } else {
        ox = player_gx - kViewCols / 2;
        oy = player_gy - kViewRows / 2;
    }
    if (dungeon_w > 0) {
        ox = std::max(0, std::min(ox, std::max(0, dungeon_w - kViewCols)));
    }
    if (dungeon_h > 0) {
        oy = std::max(0, std::min(oy, std::max(0, dungeon_h - kViewRows)));
    }

    frame.cam_x = ox + kViewCols / 2;
    frame.cam_y = oy + kViewRows / 2;
    frame.view_w = kViewCols;
    frame.view_h = kViewRows;

    frame.cells.reserve(static_cast<std::size_t>(kViewCols) * static_cast<std::size_t>(kViewRows));

    char player_race[64] = { 0 };
    an_get_player_race_name(player_race, static_cast<int>(sizeof(player_race)));

    /*
     * プレイヤの光で照らされた（暗黒側なら暗くされた）マスのビット列（`LightingState::player_lit`。
     * `cells` と同じ並び・1 マス 1 ビット・下位ビットから）。範囲外のマスは `cells` に入らない
     * ので、**入れるときの添字**（`frame.cells.size()`）で立てる。送るかどうかは光源の種類が決める
     * （`fill_floor_and_lighting()` が確定する）。
     */
    std::vector<std::uint8_t> player_lit;
    const auto mark_player_lit = [&player_lit](std::size_t index) {
        const std::size_t byte = index >> 3;
        if (player_lit.size() <= byte) {
            player_lit.resize(byte + 1, 0);
        }
        player_lit[byte] = static_cast<std::uint8_t>(player_lit[byte] | (1u << (index & 7u)));
    };

    for (int vy = 0; vy < kViewRows; ++vy) {
        const int gy = oy + vy;
        for (int vx = 0; vx < kViewCols; ++vx) {
            const int gx = ox + vx;

            an_cell_info info{};
            if (!an_map_cell(gx, gy, &info)) {
                continue; // 範囲外
            }
            if (info.player_lit != 0) {
                mark_player_lit(frame.cells.size()); // この後 `cells` へ入れる添字
            }

            MapCellView cell{};
            cell.gx = static_cast<int16_t>(gx);
            cell.gy = static_cast<int16_t>(gy);

            if (!info.known) {
                frame.cells.push_back(cell); // 未探知（feature_flags も terrain_id も 0）
                continue;
            }

            const TileLookup terrain = mapping.lookup_terrain(info.feat_name);
            cell.terrain_id = terrain.id;
            cell.under_tile_index = terrain.tile_index;
            cell.feature_flags = translate_feature_flags(info.tf_bits, info.room != 0, info.glow != 0, true);
            if (info.is_player) {
                cell.feature_flags |= CELL_FEAT_PLAYER;
            }
            cell.light_level = static_cast<uint8_t>(info.light_level);

            /*
             * 見直しの指摘 1/3/7/9。`ascii_fallback`/`fg_color` は `an_map_shim.c` が
             * `grid_data_as_text()`（`ui-map.c`）をそのまま呼んで作った値を使う——
             * 自分・敵・罠・アイテム・地形の優先順位も、幻覚時の身代わり表示も、
             * 未見の金・未見のアイテムの表示も、原作の同じ関数が解決済みだから
             * ここで作り直さない（作り直すと原作と食い違う場面がいつか出る）。
             */
            cell.ascii_fallback = info.display_char;
            cell.fg_color = info.display_attr;

            /*
             * 実体の tile_index（見直しの指摘 1）。手本は `silq/adapter/sq_frame.cpp` の
             * `resolve_tile_index()`——実体の絵は `tile_index`、地形の絵は
             * `under_tile_index`（上で設定済み）に分ける。優先順位は自分 > 敵 > アイテム
             * （`grid_data_as_text()` の描画優先順位と同じ）。絵が無い実体は 0 のまま
             * （画面側は `ascii_fallback`/`fg_color` へ落ちる）。
             */
            uint16_t entity_tile = 0;
            if (info.is_player) {
                entity_tile = mapping.lookup_player(player_race).tile_index;
            } else if (info.has_monster) {
                const TileLookup monster = mapping.lookup_monster(info.monster_name);
                cell.monster_id = monster.id;
                cell.monster_slot = static_cast<uint16_t>(info.monster_slot);
                entity_tile = monster.tile_index;
            } else if (info.has_object) {
                /*
                 * 見直しの指摘 1（本スレッドで取り消し・確定した規則）。識別の有無では
                 * 出す／出さないを分けない——変愚の `resolve_tile_index()` の床のアイテムの枝
                 * （`presentation_bridge.cpp:642` 以降）と同じく、`light_level == 2`
                 * （いま照らされている）ときだけ絵を出す。識別済みか否かの分岐は
                 * `an_object_tile` の中に閉じ込めた。
                 */
                // 規則の訂正（本スレッドの判断。2026-09-23。an_manifest.cpp の
                // lookup_object() 参照）: 分類の一致は 8 分類の `object_category`
                // ではなく、生の `object_tval` で全 tval を見る。`object_category`
                // は上の `an_object_tile()` の未識別代表タイルの判定専用のまま
                // （ここでは変更していない）。
                const TileLookup object = mapping.lookup_object(info.object_name, info.object_tval);
                cell.object_id = object.id;
                if (info.light_level >= 2) {
                    entity_tile = an_object_tile(info, mapping, object);
                }
            }
            cell.tile_index = entity_tile;

            frame.cells.push_back(cell);
        }
    }
    player_lit.resize((frame.cells.size() + 7u) / 8u, 0);
    frame.lighting.player_lit = std::move(player_lit);
}

/*!
 * @brief フロア全域の `MinimapSnapshot` を埋める（設計 §6.2）。
 * @details **これが空のままだと、画面側は地形の役割を 1 マスも決められない**
 * （`hd2d/world/floor_meaning.cpp:97-100` の `rebuild_floor_meaning()` は
 * `frame.minimap` だけを見て `role_at()` を作る。`frame.cells` は見ない）。
 * S2 の見直しで、マスが 1 つも描かれない不具合の原因がこれだと特定した。
 * S2 の見直しの指摘 3：見えている罠（ルーン・蜘蛛の巣を含む）を `markers` にも積む。
 */
void fill_minimap(GameFrame &frame)
{
    if (!an_map_ready()) {
        return;
    }
    int w = 0;
    int h = 0;
    if (!an_map_dims(&w, &h) || (w <= 0) || (h <= 0)) {
        return;
    }
    int player_gx = 0;
    int player_gy = 0;
    an_map_player(&player_gx, &player_gy);

    MinimapSnapshot &minimap = frame.minimap;
    minimap.width = w;
    minimap.height = h;
    minimap.player_gx = player_gx;
    minimap.player_gy = player_gy;
    const std::size_t cell_count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    minimap.kinds.assign(cell_count, 0);
    minimap.markers.assign(cell_count, 0);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x);
            minimap.kinds[idx] = static_cast<uint8_t>(an_minimap_kind(x, y));
            minimap.markers[idx] = static_cast<uint8_t>(an_map_marker(x, y));
        }
    }
}

//! `FloorIdentity` / `LightingState`（設計 §6.2。町とダンジョンで `kind` を切り替える）。
void fill_floor_and_lighting(GameFrame &frame)
{
    an_floor_info info{};
    if (!an_get_floor_info(&info)) {
        frame.lighting.player_lit.clear(); // 光の種類が分からないなら、照らされたマスも送らない
        return;
    }
    /*
     * ダンジョンの番号には**階の作り方**（原作の cave profile。A6 の記録）を入れる
     * （classic=101 … hard centre=108）。
     * Angband のダンジョンは 1 本だけなので、この欄を作り方の区別に使い、画面側は
     * `assets/voxel/dungeon_styles.jsonc` の `"core": "angband"` の節をこの番号で引く。
     * 記録に無い階と町は 0（＝表の既定＝classic と同じ絵）。環境音の表
     * （`assets/audio/ambience.jsonc`）と地名のカットインは 101〜108 に当たる行を持たない。
     */
    frame.floor.dungeon_id = info.is_town ? 0 : info.profile_id;
    frame.floor.dun_level = info.depth;
    frame.floor.generated_turn = static_cast<uint64_t>(info.generated_turn); // 見直しの指摘 13：`cave->turn`。
    frame.floor.kind = info.is_town ? 1 : 2; // FloorKind::Surface / Dungeon
    /*
     * 町の番号は 1（Angband の町は 1 つだけ）。**0 を送ると画面側が町として組み立てない**
     * （`hd2d/world/town_plan.cpp` の `rebuild_town_plan()` が `town_id <= 0` を「町の外」と読む）。
     * 以前は「変愚の町 1 番（辺境の地）の絵を引いてしまう」のを避けて 0 にしていたが、
     * 町の意匠は `assets/voxel/town_styles.jsonc` の `"core": "angband"` の節が先に当たるので
     * 変愚の町 1 番の意匠は引かない（`town_style_for()` / `town_signs_for()` はデータが先）。
     * 地名の絵（`floor_cutin.cpp` の `place_t1`）は絵が 1 枚も無く、環境音は町 0 と同じ
     * `town_outpost` に落ちるので、1 にしても変わらない。
     */
    frame.floor.town_id = info.is_town ? 1 : 0;

    frame.lighting.day_minute = info.day_minute;
    frame.lighting.daytime = info.daytime != 0;
    frame.lighting.light_radius = info.light_radius;
    /*
     * 光の届く範囲。
     * 種類は `AN_LIGHT_*`（`LightingState::light_kind` と同じ値。5 = 暗黒側）。
     * 光が無い（0）なら照らされたマスも送らない（画面側は従来どおり点光源の減衰だけで描く）。
     */
    frame.lighting.light_kind = static_cast<std::uint8_t>(info.light_kind);
    if (info.light_kind == AN_LIGHT_NONE) {
        frame.lighting.player_lit.clear();
    }
}

/*!
 * @brief サブパネル（設計 §6.3）。**画面側の「UI 既定」の枠を直接埋める**
 * （`hd2d/ui/game_hud.cpp` の `draw_default_sub_panel()`）。
 * @details `frame.sub_panels[]`（コア独自のサブウインドウの申告）は使わない——
 * Angband の `PW_INVEN`/`PW_EQUIP`/`PW_MONLIST`（`ui-display.c`）を読んで検討したが、
 * M0（英語のみ・サブウインドウの選択画面も無い段）でそれを流用すると
 * `angband_term[]` を追加で立てて `window_flag[]` を切り替える大掛かりな仕組みが
 * 要る（Sil-Q の `sq_sub_terms.c` 相当）。画面側は「コアがサブウインドウを
 * 申告しない（`kind < 0`。既定のまま）」ときに `sub2_lines`（装備）・`sub3_lines`
 * （視界の敵）・`sub5_lines`（持ち物）・`frame.messages`（メッセージ。既に
 * `fill_messages()` が埋めている）・`frame.hud`（キャラクター情報。既に
 * `fill_hud()` が埋めている）をそのまま描く経路を持っているので、それに乗せる。
 */
void fill_legacy_sub_panels(GameFrame &frame)
{
    constexpr int kMaxLines = 16;
    char raw[kMaxLines][AN_SUBLIST_LINE_CAP];

    frame.sub2_lines.clear();
    const int equip_n = an_equipment_lines(raw, kMaxLines);
    for (int i = 0; i < equip_n; ++i) {
        frame.sub2_lines.emplace_back(raw[i]);
    }
    if (frame.sub2_lines.empty()) {
        frame.sub2_lines.emplace_back(ui_text("(empty)"));
    }

    frame.sub3_lines.clear();
    const int mon_n = an_visible_monster_lines(raw, kMaxLines);
    for (int i = 0; i < mon_n; ++i) {
        frame.sub3_lines.emplace_back(raw[i]);
    }
    if (frame.sub3_lines.empty()) {
        frame.sub3_lines.emplace_back(ui_text("(none)"));
    }

    frame.sub5_lines.clear();
    int skipped = 0;
    const int inv_n = an_inventory_lines(raw, kMaxLines, &skipped);
    for (int i = 0; i < inv_n; ++i) {
        frame.sub5_lines.emplace_back(raw[i]);
    }
    if (skipped > 0) {
        char more[32];
        std::snprintf(more, sizeof(more), "...(+%d)", skipped);
        frame.sub5_lines.emplace_back(more);
    }
    if (frame.sub5_lines.empty()) {
        frame.sub5_lines.emplace_back(ui_text("(empty)"));
    }
}

/*!
 * @brief かかっている状態・常時の効果のアイコン（`GameFrame::status_effects`）。
 * @details 取り方は `an_map_shim.c` の `an_read_status_effects()`（原作の `prt_tmd()` と同じ段・キャラクターシートの
 * 耐性の表の既知の項目）。名前の字はコアの言語（日本語層が起きていれば日本語）で、UTF-8。
 * 載せるのはキャラクターがいるあいだだけ。載せるものが無ければ空のまま（線にも出ない）。
 */
void fill_status_effects(GameFrame &frame)
{
    static an_status_effect rows[AN_STATUS_MAX];

    frame.status_effects.clear();
    const int n = an_read_status_effects(rows, AN_STATUS_MAX);
    if (n <= 0) {
        return;
    }
    frame.status_effects.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const an_status_effect &src = rows[i];
        if ((src.kind < 0) || (src.kind > static_cast<int>(kStatusEffectKindMax))) {
            continue;
        }
        StatusEffectView view{};
        view.key = src.key;
        view.label_utf8 = src.label;
        view.amount_utf8 = src.amount;
        view.kind = static_cast<StatusEffectKind>(src.kind);
        frame.status_effects.push_back(std::move(view));
    }
}

} // namespace

/*!
 * @details 見直しの指摘 9。`fill_game_frame()` の中からだけでなく、
 * `an_main.cpp` の `build_frame()` が `frame.menu_open` の間も単独で呼べるよう、
 * 無名名前空間の外（`an_frame.h` の宣言と同じ外部結合）へ出した。
 */
void fill_messages(GameFrame &frame)
{
    /*!
     * AH-01/AH-02 の直し。他の 5 本のコア（手本は `presentation_bridge.cpp` の
     * `fill_messages()`）と同じく、`an_take_message()` で先入れ先出しに消費するのではなく、
     * 直近 `kMessageLines` 行を消費せずに毎フレーム丸ごと送る形へそろえた。差分で送ると
     * （直していた旧実装）画面側の最新値スロットで捨てられたフレームの分が読めないまま
     * 消えるので、画面側（`hd2d/net/core_link.cpp` の `carried_messages`）が
     * 捨てたフレームのメッセージを次のフレームへ運んで救っていたが、直近 N 行を毎回
     * 送るコアではその「運ぶ」が重複表示になっていた（AH-01）。
     */
    constexpr int kMessageLines = 8;
    const int count = an_recent_message_count();
    const int take = (count < kMessageLines) ? count : kMessageLines;
    frame.messages.clear();
    if (take <= 0) {
        return;
    }
    frame.messages.reserve(static_cast<std::size_t>(take));
    /*
     * AH-20 の直し。以前は 256 バイトの受け皿（`an_take_message()` の
     * キューの 1 件が 256 バイト固定だった）で切っていたので、モンスターの
     * 説明文（「見る」の詳細。設計 §8.5.1）の長い行（624 種中 24 種で発生。実測）が
     * 途中で切れていた。ここは 1 行ぶんの受け皿でしかないので、大きくしても
     * 他への影響は無い。
     */
    char text[2048];
    for (int age = take - 1; age >= 0; --age) {
        int color = 1;
        if (!an_get_recent_message(age, text, static_cast<int>(sizeof(text)), &color)) {
            continue;
        }
        if (text[0] == '\0') {
            continue;
        }
        MessageEvent event;
        /*
         * 「応答待ち」4 回目の根本（2026-09-24）。以前はここが `static` の通し番号を
         * `++seq` で振っていた——先入れ先出しで消費していた旧実装の名残で、直近 8 行を
         * **毎フレーム送り直す**いまの形では、中身が同じでも番号だけが毎回変わる。
         * `host_present()` の「前回と同じ内容は送らない」（v1 §6.4。`frame_compare_key()`）
         * がそれで一度も一致せず、安いしきい値（`an_change_digest()`）が動くたび
         * ——店を出た直後にゲームターンが 10 回進む間など——3ms ごとに同じ画の
         * フレームが出ていた（`--protocol-log=` で実測。差分は `messages[].seq` だけ）。
         * 画面側の `hd2d/ui/modern_store_ui.cpp` も `seq` の大小で「新しい行」を
         * 判じるので、番号が毎回増えると同じ行が何度も追加される（AH-01 の重複の
         * 本当の出どころ）。手本の `presentation_bridge.cpp` と同じく、フレーム内の
         * 位置（0 が最古）を `seq` にする——中身が同じならフレームも同じになる。
         */
        event.seq = static_cast<uint32_t>(frame.messages.size());
        event.color = static_cast<uint8_t>(color);
        event.text_utf8 = text;
        frame.messages.push_back(std::move(event));
    }
}

/*!
 * @brief AH-31: 溜まっている見せ場（`an_fx_take()`）を `frame.combat_fx` へ移す。
 * @details 手本は `frox/adapter/fc_frame.cpp` の `fill_combat_fx()`。**汲んだら消える**
 * （`combat_fx.h` の契約）ので、フレームを組むたびに毎回呼ぶ——取りこぼしても
 * 演出が溜まって後から噴き出すことはない。
 */
void fill_combat_fx(GameFrame &frame)
{
    // 環（`an_fx.c` の `an_fx_ring[]`）と同じ大きさで受ける（見直しの指摘。
    // 以前はここが 64 で環の 128 より小さく、1 フレームの間に 64 件を超えて
    // 溜まると `an_fx_take()` が新しい見せ場のほうを取りこぼしていた）。
    an_fx_event raw[AN_FX_MAX];
    const int n = an_fx_take(raw, static_cast<int>(sizeof(raw) / sizeof(raw[0])));

    frame.combat_fx.clear();
    if (n <= 0) {
        return;
    }
    frame.combat_fx.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const an_fx_event &e = raw[i];
        CombatFxEvent fx{};
        fx.kind = static_cast<CombatFxKind>(e.kind);
        fx.element = static_cast<CombatFxElement>(e.element);
        fx.y = static_cast<int16_t>(e.y);
        fx.x = static_cast<int16_t>(e.x);
        fx.src_y = static_cast<int16_t>(e.src_y);
        fx.src_x = static_cast<int16_t>(e.src_x);
        fx.intensity = e.intensity;
        frame.combat_fx.push_back(fx);
    }
}

/*!
 * @brief 溜まっている魔法・投射物の効果（飛ぶ魔法・ボール・ブレス・ビーム・矢弾・投擲）を `frame.projections`・
 * `frame.missiles` へ移す（`presentation/bridge/named_attack_fx.h`）。**汲んだら空になる**。
 * @details 矢弾は 1 歩ごとに仮置きされ、命令待ち（`an_awaiting_command()`＝動きの終わり）で確定する。
 * 入力待ちの 10ms ごとのフレームで飛行中の 1 発が途中で割れないようにするため。
 */
void fill_attack_fx(GameFrame &frame)
{
    attack_fx::NamedRecorder::instance().take(frame.projections, frame.missiles, an_awaiting_command() != 0);
}

//! 汲めるものがあるか（`host_present()` が「画面が変わっていない」の判定を飛ばすのに使う）。
bool attack_fx_ready()
{
    return attack_fx::NamedRecorder::instance().has_ready(an_awaiting_command() != 0);
}

void fill_game_frame(GameFrame &frame, const TileMapping &mapping)
{
    g_fx_mapping = &mapping;
    fill_cells(frame, mapping);
    fill_minimap(frame);
    fill_floor_and_lighting(frame);
    fill_status_and_bottom(frame);
    fill_status_effects(frame); //!< 
    fill_hud(frame);
    fill_messages(frame);
    fill_legacy_sub_panels(frame);
    fill_combat_fx(frame);
    fill_attack_fx(frame); //!< 
}

} // namespace an

/* ==================================================================== 魔法・投射物の効果（C から呼ばれる入口。`an_fx.c`） */

extern "C" void an_named_fx_projection(int origin, int shape, const char *attr, int rad, int src_y, int src_x,
    int impact_y, int impact_x, int flags, const an_fx_cell *path, int path_n, const an_fx_cell *area, int area_n)
{
    ProjectionFx fx;
    fx.origin = static_cast<AttackFxOrigin>(origin);
    fx.shape = static_cast<ProjectionShape>(shape);
    fx.attr = (attr != nullptr) ? attr : "";
    fx.rad = static_cast<uint8_t>(std::max(0, std::min(rad, 255)));
    fx.src_y = static_cast<int16_t>(src_y);
    fx.src_x = static_cast<int16_t>(src_x);
    fx.impact_y = static_cast<int16_t>(impact_y);
    fx.impact_x = static_cast<int16_t>(impact_x);
    fx.flags = static_cast<uint16_t>(flags);
    for (int i = 0; (path != nullptr) && (i < path_n) && (fx.path.size() < attack_fx::kNamedMaxCells); ++i) {
        FxCell cell;
        cell.y = static_cast<int16_t>(path[i].y);
        cell.x = static_cast<int16_t>(path[i].x);
        fx.path.push_back(cell);
    }
    for (int i = 0; (area != nullptr) && (i < area_n) && (fx.area.size() < attack_fx::kNamedMaxCells); ++i) {
        FxCell cell;
        cell.y = static_cast<int16_t>(area[i].y);
        cell.x = static_cast<int16_t>(area[i].x);
        cell.dist = static_cast<uint8_t>(std::max(0, std::min(area[i].dist, 255)));
        fx.area.push_back(cell);
    }
    attack_fx::NamedRecorder::instance().add_projection(std::move(fx));
}

extern "C" void an_named_fx_missile_step(int first, int kind, const char *object_name, int object_tval, int unaware,
    const char *brand, int src_y, int src_x, int y, int x)
{
    auto &recorder = attack_fx::NamedRecorder::instance();
    if ((first != 0) || !recorder.has_open()) {
        std::uint16_t item_id = 0;
        if ((unaware == 0) && (g_fx_mapping != nullptr) && (object_name != nullptr)) {
            item_id = g_fx_mapping->lookup_object(object_name, object_tval).id;
        }
        recorder.begin_missile(static_cast<MissileKind>(kind), AttackFxOrigin::Player, item_id,
            (brand != nullptr) ? std::string(brand) : std::string(), src_y, src_x);
    }
    recorder.missile_step(y, x);
}

extern "C" void an_named_fx_missile_hit(void)
{
    attack_fx::NamedRecorder::instance().missile_hit();
}
