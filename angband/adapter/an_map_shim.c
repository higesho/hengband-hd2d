/*!
 * @file an_map_shim.c
 * @brief `an_map_shim.h` の実体。マス・HUD・メッセージを Angband の内部構造から読む。
 *
 * 手本は `silq/adapter/sq_shim.c` の
 * マス読み取り（`map_info()` を包む部分）。
 */

#include "angband.h"

#include "cave.h"
#include "game-event.h"
#include "game-world.h"
#include "init.h"
#include "message.h"
#include "mon-list.h"
#include "mon-util.h"
#include "monster.h"
#include "object.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-properties.h"
#include "obj-tval.h" /* tval_is_light（光源の種類） */
#include "player-timed.h" /* TMD_IMAGE（見直しの指摘 7） */
#include "trap.h"
#include "ui-map.h"
#include "ui-menu.h" /* all_letters_nohjkl（見直しの指摘 6） */
#include "ui-term.h" /* Term（AH-33。`Term->offset_x`/`offset_y`） */

#include "an_map_shim.h"
#include "an_profile.h" /* 階の作り方（A6） */
#include "an_shim.h"
#include "an_lang_c.h" /* P4: サブパネルの札と名 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*!
 * 死亡後の「新しいゲーム」（調-04・F-angband-07）の再初期化の間は、人物が居ないものとして読む。
 * 原作の `play_game()` は `play_again` を立てたまま `cleanup_angband()`→`init_display()`→`init_angband()`
 * を回し、その間 `character_generated` は死んだ人物のまま真である（`start_game()` が偽にするまで）。
 * この間に Term が描かれて画面を組むと、解放済みの `messages`・作り直したばかりの空の `player` を読んで
 * 落ちる。このファイルの中の `character_generated` は**読むだけ**（代入は無い）。マクロ自身は再展開されない
 * ので、右辺は原作の変数そのものを指す。
 */
#define character_generated (character_generated && !play_again)

/*!
 * @brief AH-23: 幻覚中は `map_info`（`cave-map.c`）
 * 自身が原作のゲーム RNG（`z-rand.c` の複素 RNG。`state_i`/`STATE[]`）を直接
 * 進める——「Rare random hallucination on non-outer walls」の節（`one_in_(128)`
 * を最大 2 回）が、幻覚中で敵もアイテムも無い平凡なマスにまで毎回効く。
 * 加えて `grid_data_as_text()`（`ui-map.c`）が呼ぶ `hallucinatory_monster()`/
 * `hallucinatory_object()` も `randint0()` で同じ RNG を進める。この RNG は
 * 敵の生成・アイテムの生成・命中判定など、原作のゲーム進行そのものに使う
 * 共有の乱数で、`an_null_term.c` の `an_term_event()` は入力待ちの間も
 * `AN_IDLE_SLICE_MS`（10ms）ごとに `an_present()`（`build_frame()`）を呼び
 * 続ける（`host_present()` 側の安いしきい値は Term の見た目が変わらない
 * 限り再構成そのものを省くが、幻覚中は町の視界にあるマスの大半がこの
 * `map_info()` の枝を踏むので、Term の見た目が動くたびに——実際のターンが
 * 1 つも進んでいなくても——原作の RNG が余分に進んでしまう。AH-36 の直しで
 * `TERM_XTRA_FRESH` 自体の連打（部屋を一括表示するときの `Term_fresh()` の
 * 連続呼び出し）はまとめて 1 回に減ったが、この入力待ちループの 10ms ごとの
 * 呼び出しは直す前と同じ無条件のまま残した——`an_null_term.c` 冒頭の註記
 * のとおり、Term を一切触らない新 UI の変化（持ち物一覧など）を拾うのに
 * この無条件呼び出しが要るため。つまりこの危険自体は AH-36 でも消えて
 * いないので、下の退避・書き戻しは引き続き必要）。
 * **実測で確かめた**（`tools/angband/ah23_hallucinate_rng_smoke.py` の
 * 調査中）：最初は `grid_data_as_text()` の枝だけを退避・書き戻ししたが、
 * それでも同じ 1 回の `build_frame()` の中で複数マスを跨いで `state_i` が
 * 動き続けた——原因は `map_info()` 自身の上の枝だった。
 *
 * 直し：`map_info()` を呼ぶ直前から（`an_map_cell()` では続けて呼ぶ
 * `grid_data_as_text()` の直後まで）RNG の内部状態一式（`Rand_quick`・
 * `Rand_value`・複素 RNG の `state_i`/`STATE[]`/`z0`/`z1`/`z2`。`z-rand.h`）を
 * 退避・書き戻しする——表示のための「見た目だけの乱数」を、原作の本当の
 * ゲーム進行用の乱数から隔離する。`an_map_shim.c` の `map_info()` 呼び出し
 * 3 か所（`an_map_cell()`・`an_minimap_kind()`・`an_map_marker()`）すべてに
 * 同じ退避・書き戻しを掛ける。幻覚中かどうかは `player->timed[TMD_IMAGE]`
 * だけで `map_info()` を呼ぶ前から分かるので、退避・書き戻しは幻覚中だけに
 * 限る（通常時は 1 バイトも余計なことをしない）。この構造体・関数は本
 * ファイルでしか使わない。
 */
typedef struct an_rng_snapshot {
    bool quick;
    uint32_t value;
    uint32_t idx;
    uint32_t state[RAND_DEG];
    uint32_t z0v, z1v, z2v;
} an_rng_snapshot;

static void an_rng_save(an_rng_snapshot *snap)
{
    snap->quick = Rand_quick;
    snap->value = Rand_value;
    snap->idx = state_i;
    memcpy(snap->state, STATE, sizeof(STATE));
    snap->z0v = z0;
    snap->z1v = z1;
    snap->z2v = z2;
}

static void an_rng_restore(const an_rng_snapshot *snap)
{
    Rand_quick = snap->quick;
    Rand_value = snap->value;
    state_i = snap->idx;
    memcpy(STATE, snap->state, sizeof(STATE));
    z0 = snap->z0v;
    z1 = snap->z1v;
    z2 = snap->z2v;
}

/*!
 * @brief AH-23 の検査用の目印（`tools/angband/ah23_hallucinate_rng_smoke.py`）。
 * `ANGBAND_DEBUG_RNG` が立っているときだけ、幻覚の表示替えで実際に乱数を
 * 引いた直後の `state_i` を stderr へ出す——検査は入力待ちの間（キーを 1 つも
 * 送らない）にこの値が変わり続けるかどうかで、直す前／直した後を見分ける。
 * 通常運用では環境変数が無いので何もしない。
 */
static void an_debug_dump_rng_state(void)
{
    static int checked = 0;
    static int enabled = 0;
    if (!checked) {
        enabled = getenv("ANGBAND_DEBUG_RNG") ? 1 : 0;
        checked = 1;
    }
    if (enabled) {
        fprintf(stderr, "[angband-rng] frame=%llu state_i=%lu\n",
            an_debug_current_frame_id(), (unsigned long)state_i);
        fflush(stderr);
    }
}

/*!
 * @brief AH-23 の検査専用フック。`ANGBAND_TEST_FORCE_IMAGE`（正の整数。残り手番数）
 * が立っているときだけ、毎回 `player->timed[TMD_IMAGE]` をその値へ強制する——
 * 幻覚を起こす経路（原作のアイテム・罠）を検査から辿らずに、幻覚中の表示替えを
 * 確実に再現するための検査専用の仕掛け（`ANGBAND_INJECT_KEYS` と同じ位置づけ。
 * `an_main.cpp` の検証用キー注入を参照）。環境変数が無ければ何もしない
 * （通常運用・他の検査には一切影響しない）。
 */
static void an_debug_force_image_if_requested(void)
{
    static int checked = 0;
    static long forced = 0;
    if (!checked) {
        const char *env = getenv("ANGBAND_TEST_FORCE_IMAGE");
        forced = env ? atol(env) : 0;
        checked = 1;
    }
    if ((forced > 0) && character_generated && player) {
        player->timed[TMD_IMAGE] = (int16_t)forced;
    }
}

int an_map_ready(void)
{
    return (character_dungeon && cave) ? 1 : 0;
}

int an_map_player(int *gx, int *gy)
{
    if (!an_map_ready() || !player) {
        return 0;
    }
    if (gx) {
        *gx = player->grid.x;
    }
    if (gy) {
        *gy = player->grid.y;
    }
    return 1;
}

int an_map_dims(int *w, int *h)
{
    if (!an_map_ready()) {
        return 0;
    }
    if (w) {
        *w = cave->width;
    }
    if (h) {
        *h = cave->height;
    }
    return 1;
}

/*!
 * @brief AH-33: `L`（`do_cmd_locate`。
 * `ui-knowledge.c`）が動かすのは `Term->offset_x`/`offset_y`（見ている区画。
 * `@` は動かない）だけなので、既存の「行 0 が `"Map sector"` で始まる」
 * 判定（AH-10。`fill_raw_row0_free_prompts()`）と同じ目印で「いま `L` の
 * 最中か」を見分ける——`do_cmd_locate()` 自身に専用の旗が無く、原作へ
 * 手を入れずに済ませるため（手本は Sil-Q の `sq_locate_shifted` だが、
 * あちらは原作へ 1 行足す手入れで作った専用の旗——Angband の A1〜A6 の
 * 手入れの表にこの用途の項目が無いので、既存の raw な行 0 判定を使う）。
 */
int an_locate_active(void)
{
    char row0[32];
    int n;

    if (!an_map_ready()) {
        return 0;
    }
    n = an_term_row(0, row0, (int)sizeof(row0));
    if (n <= 0) {
        return 0;
    }
    if (strncmp(row0, "Map sector", 10) == 0) {
        return 1;
    }
    /* 日本語（設計 §20）: 目印の訳（E:Map sector）でも見る。英語では an_tr() が同じ字を返す。 */
    {
        const char *ja = an_tr("Map sector");
        return ((ja != NULL) && (strcmp(ja, "Map sector") != 0) && (strncmp(row0, ja, strlen(ja)) == 0)) ? 1 : 0;
    }
}

/*!
 * @brief AH-33。`Term->offset_x`/`offset_y`（`do_cmd_locate()` が動かす、
 * 見ている区画の左上隅。原作のマス単位）をそのまま返す。
 */
int an_panel_offset(int *ox, int *oy)
{
    if (!an_map_ready() || !Term) {
        return 0;
    }
    if (ox) {
        *ox = Term->offset_x;
    }
    if (oy) {
        *oy = Term->offset_y;
    }
    return 1;
}

static void an_copy_name(char *dest, size_t cap, const char *src)
{
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, cap - 1);
    dest[cap - 1] = '\0';
}

int an_player_light_kind(void)
{
    int light;
    const struct object *obj;

    if (!character_generated || !player) {
        return AN_LIGHT_NONE;
    }
    light = player->state.cur_light;
    if (player_has(player, PF_UNLIGHT) && (light <= 1)) {
        return AN_LIGHT_DARK; /* 闇の探知範囲で見る（`update_view_one()`） */
    }
    if (light < 0) {
        return AN_LIGHT_DARK;
    }
    if (light == 0) {
        return AN_LIGHT_NONE;
    }

    obj = equipped_item_by_slot_name(player, "light");
    if ((obj == NULL) || !tval_is_light(obj)) {
        return AN_LIGHT_OTHER;
    }
    if (obj->artifact != NULL) {
        return AN_LIGHT_ARTEFACT;
    }
    if (of_has(obj->flags, OF_BURNS_OUT)) {
        return (obj->timeout > 0) ? AN_LIGHT_TORCH : AN_LIGHT_OTHER;
    }
    if (of_has(obj->flags, OF_TAKES_FUEL)) {
        return (obj->timeout > 0) ? AN_LIGHT_LANTERN : AN_LIGHT_OTHER;
    }
    return AN_LIGHT_ARTEFACT; /* 燃料の要らない光 */
}

/*!
 * @brief プレイヤの光で照らされた（暗黒側なら暗くされた）マスか。`an_cell_info::player_lit`。
 * @details 読むだけ。光は原作の `SQUARE_CLOSE_PLAYER`（`cave-view.c` の `become_viewable()` が、
 * 光源の半径の内側で視認できるマスに立てる）。暗黒側は、UNLIGHT の人なら `CLOSE_PLAYER`（闇の探知範囲）
 * のうち**暗いままのマス**（原作の `map_info()` が `LIGHTING_DARK` にするのと同じ条件）、
 * 負の光なら `calc_lighting()` の `add_light()` と同じ範囲（`SQUARE_VIEW` かつ距離が `-cur_light - 1` 以内）。
 */
static int an_cell_player_lit(struct loc grid)
{
    const int kind = an_player_light_kind();

    if (kind == AN_LIGHT_NONE) {
        return 0;
    }
    if (kind != AN_LIGHT_DARK) {
        return sqinfo_has(square(cave, grid)->info, SQUARE_CLOSE_PLAYER) ? 1 : 0;
    }
    if (player_has(player, PF_UNLIGHT) && (player->state.cur_light <= 1)) {
        return (sqinfo_has(square(cave, grid)->info, SQUARE_CLOSE_PLAYER) && !square_islit(cave, grid)) ? 1 : 0;
    }
    return (sqinfo_has(square(cave, grid)->info, SQUARE_VIEW)
            && (distance(player->grid, grid) <= (-player->state.cur_light - 1))) ? 1 : 0;
}

int an_map_cell(int gx, int gy, an_cell_info *out)
{
    struct loc grid;
    struct grid_data g;
    struct feature *feature;
    int a = 0, ta = 0;
    wchar_t c = 0, tc = 0;

    if (!out) {
        return 0;
    }
    memset(out, 0, sizeof(*out));

    if (!an_map_ready()) {
        return 0;
    }

    an_debug_force_image_if_requested();

    grid.x = gx;
    grid.y = gy;
    if (!square_in_bounds(cave, grid)) {
        return 0;
    }

    out->known = square_isknown(cave, grid) ? 1 : 0;
    out->room = square_isroom(cave, grid) ? 1 : 0;
    out->glow = square_isglow(cave, grid) ? 1 : 0;
    out->player_lit = an_cell_player_lit(grid);

    if (!out->known) {
        return 1;
    }

    /*
     * AH-23。`map_info` 自身も乱数を引く
     * ——`cave-map.c` の「Rare random hallucination on non-outer walls」
     * （`one_in_(128)` を 2 回まで）が、幻覚中で敵もアイテムも無い平凡な
     * マスにまで効く。`grid_data_as_text()` の `hallucinatory_monster()`/
     * `hallucinatory_object()` と合わせて、退避・書き戻しを `map_info()`
     * の直前から `grid_data_as_text()` の直後までひと続きにする——
     * 幻覚中かどうかは `player->timed[TMD_IMAGE]` だけで `map_info()` を
     * 呼ぶ前に分かる。
     */
    an_rng_snapshot an_h23_snap;
    const int an_h23_hallucinating =
        (character_generated && player && (player->timed[TMD_IMAGE] > 0)) ? 1 : 0;
    if (an_h23_hallucinating) {
        an_rng_save(&an_h23_snap);
    }
    map_info(grid, &g);
    out->in_view = g.in_view ? 1 : 0;
    out->is_player = g.is_player ? 1 : 0;
    out->lighting = (int)g.lighting;
    out->hallucinating = g.hallucinate ? 1 : 0;
    /*
     * 契約上の光の段（0/1/2）（見直しの指摘 5）。
     * 視界外は記憶のみ。0。視界内で LIGHTING_DARK（UNLIGHT 能力で無灯）なら 1。
     * それ以外（LOS/TORCH/LIT）は 2。
     */
    if (!g.in_view) {
        out->light_level = 0;
    } else if (g.lighting == LIGHTING_DARK) {
        out->light_level = 1;
    } else {
        out->light_level = 2;
    }

    feature = &f_info[g.f_idx];
    an_copy_name(out->feat_name, sizeof(out->feat_name), feature->name);

    if (tf_has(feature->flags, TF_WALL)) out->tf_bits |= AN_TF_WALL;
    if (tf_has(feature->flags, TF_PERMANENT)) out->tf_bits |= AN_TF_PERMANENT;
    if (tf_has(feature->flags, TF_DOOR_ANY)) out->tf_bits |= AN_TF_DOOR_ANY;
    if (tf_has(feature->flags, TF_DOOR_CLOSED)) out->tf_bits |= AN_TF_DOOR_CLOSED;
    if (tf_has(feature->flags, TF_STAIR)) out->tf_bits |= AN_TF_STAIR;
    if (tf_has(feature->flags, TF_UPSTAIR)) out->tf_bits |= AN_TF_UPSTAIR;
    if (tf_has(feature->flags, TF_DOWNSTAIR)) out->tf_bits |= AN_TF_DOWNSTAIR;
    if (tf_has(feature->flags, TF_PASSABLE)) out->tf_bits |= AN_TF_PASSABLE;
    if (tf_has(feature->flags, TF_FIERY)) out->tf_bits |= AN_TF_FIERY;
    if (tf_has(feature->flags, TF_SHOP)) out->tf_bits |= AN_TF_SHOP;
    /* 見直しの指摘 9。Angband に TF_TREE/TF_LAVA/TF_WATER は無いので、岩石は ROCK、溶岩は FIERY で見る */
    if (tf_has(feature->flags, TF_ROCK)) out->tf_bits |= AN_TF_ROCK;
    if (tf_has(feature->flags, TF_BRIGHT)) out->tf_bits |= AN_TF_BRIGHT;
    if (tf_has(feature->flags, TF_GOLD)) out->tf_bits |= AN_TF_GOLD;

    /*
     * 見直しの指摘 8：`map_info()` 自体が見えない敵をすでに 0 にしている
     * （`cave-map.c` の `if (!monster_is_visible(mon)) g->m_idx = 0;`）ので、
     * ここで `g.in_view` を重ねて調べる必要はない——重ねるとテレパシー・
     * 感知・赤外線視力で見えている敵が消える（実際は `in_view`=偽でも見えうる）。
     */
    if ((g.m_idx > 0) && !g.hallucinate) {
        struct monster *mon = cave_monster(cave, (int)g.m_idx);
        if (mon && mon->race) {
            out->has_monster = 1;
            out->monster_slot = (unsigned int)g.m_idx;
            an_copy_name(out->monster_name, sizeof(out->monster_name), mon->race->name);
        }
    }

    if (g.first_kind && !g.unseen_object && !g.hallucinate) {
        out->has_object = 1;
        an_copy_name(out->object_name, sizeof(out->object_name), g.first_kind->name);
        out->object_tval = (int)g.first_kind->tval;
        out->object_aware = g.first_kind->aware ? 1 : 0;
        switch (g.first_kind->tval) {
            case TV_POTION: out->object_category = AN_ITEM_CATEGORY_POTION; break;
            case TV_SCROLL: out->object_category = AN_ITEM_CATEGORY_SCROLL; break;
            case TV_RING: out->object_category = AN_ITEM_CATEGORY_RING; break;
            case TV_AMULET: out->object_category = AN_ITEM_CATEGORY_AMULET; break;
            case TV_WAND: out->object_category = AN_ITEM_CATEGORY_WAND; break;
            case TV_STAFF: out->object_category = AN_ITEM_CATEGORY_STAFF; break;
            case TV_ROD: out->object_category = AN_ITEM_CATEGORY_ROD; break;
            case TV_MUSHROOM: out->object_category = AN_ITEM_CATEGORY_MUSHROOM; break;
            default: out->object_category = AN_ITEM_CATEGORY_NONE; break;
        }
    }

    /*
     * 一番上の絵（見直しの指摘 1、3、7、9）。`grid_data_as_text()` は自分・敵・
     * アイテム・罠・地形の優先順位と幻覚を全て原作と同じように解決する（`ui-map.c`）。
     * `use_graphics == GRAPHICS_NONE`（ヘッドレスなので常に真）ので a/c は色番号と ASCII。
     */
    grid_data_as_text(&g, &a, &c, &ta, &tc);
    if (an_h23_hallucinating) {
        /* AH-23。`map_info()` の直前から退避した RNG をここでまとめて書き戻す。 */
        an_rng_restore(&an_h23_snap);
        an_debug_dump_rng_state();
    }
    out->display_attr = an_round_color(a);
    out->display_char = (char)c;
    out->terrain_attr = an_round_color(ta);
    out->terrain_char = (char)tc;

    return 1;
}

int an_minimap_kind(int gx, int gy)
{
    struct loc grid;
    struct grid_data g;
    struct feature *feature;

    if (!an_map_ready()) {
        return 0;
    }
    grid.x = gx;
    grid.y = gy;
    if (!square_in_bounds(cave, grid)) {
        return 0;
    }
    if (!square_isknown(cave, grid)) {
        /*
         * 町の永久壁は、見えていなくても壁として送る。
         * Angband は町を照らしても、床に面した壁の外側しか「既知」にしない
         * （店の 7×5 の塊なら内側の 15 マスほどが未探知のまま）。画面側の町の読み
         * （`hd2d/world/town_plan.cpp` の敷地。外接矩形の充填率 0.72 以上）が
         * 店の塊を敷地と読めず、建物も前庭も 1 つも組まれなかった。
         * 町の地図は固定で、隠れた情報は無い（店の壁の内側は壁しか無い）ので、
         * ミニマップにだけ壁として載せる。`frame.cells` は変えない。
         */
        if (cave->depth == 0 && square_isperm(cave, grid)) {
            return 2;
        }
        /* 町の花崗岩も未探知のまま山として送る（塊の内側は既知にならないため）。 */
        if (cave->depth == 0 && square(cave, grid)->feat == FEAT_GRANITE) {
            return 8;
        }
        return 0; /* 未探知。 */
    }

    /* AH-23。ここから先は RNG を消費しない（純粋な参照・分岐だけ）ので、`map_info()` の直後で書き戻せる。 */
    {
        an_rng_snapshot an_h23_snap;
        const int an_h23_hallucinating =
            (character_generated && player && (player->timed[TMD_IMAGE] > 0)) ? 1 : 0;
        if (an_h23_hallucinating) {
            an_rng_save(&an_h23_snap);
        }
        map_info(grid, &g);
        if (an_h23_hallucinating) {
            an_rng_restore(&an_h23_snap);
        }
    }
    if (g.is_player) {
        return 7;
    }
    /* 見直しの指摘 8：`map_info()` 自体が見えない敵を 0 にしているので `in_view` は見ない。 */
    if ((g.m_idx > 0) && !g.hallucinate) {
        return 6;
    }
    if (g.first_kind && !g.unseen_object && !g.hallucinate) {
        return 5;
    }

    feature = &f_info[g.f_idx];
    if (tf_has(feature->flags, TF_DOOR_ANY)) {
        return 3;
    }
    if (tf_has(feature->flags, TF_STAIR)) {
        return 4;
    }
    /*
     * 見直しの指摘 2：店の入り口（SHOP|PASSABLE|PERMANENT）が TF_PERMANENT だけで壁に化けていた。
     * 変愚の `presentation_bridge.cpp` と同じく WALL だけで判定する。
     */
    /*
     * 町の花崗岩（`FEAT_GRANITE`）は**山（8）**として送る（決めたこと 2026-09-24
     * 「ダンジョンの入り口は周囲を瓦礫と岩山で囲み、はっきりわかるように」）。
     * 画面側の町の読みは山の印（`MinimapKind::Mountain`）を岩山（`TownRole::Crag`）に
     * するので、階段を囲む馬蹄形も外周の花崗岩も岩山として描かれる（設計 §2
     * 「崩れかけた岩（花崗岩）を不規則に」）。ダンジョンでは従来どおり壁。
     */
    if (cave->depth == 0 && g.f_idx == FEAT_GRANITE) {
        return 8;
    }
    if (tf_has(feature->flags, TF_WALL)) {
        return 2;
    }
    return 1; /* 床。 */
}

int an_map_marker(int gx, int gy)
{
    struct loc grid;
    struct grid_data g;
    struct feature *feature;
    int marker = AN_MARKER_NONE;

    if (!an_map_ready()) {
        return AN_MARKER_NONE;
    }
    grid.x = gx;
    grid.y = gy;
    if (!square_in_bounds(cave, grid)) {
        return AN_MARKER_NONE;
    }
    if (!square_isknown(cave, grid)) {
        return AN_MARKER_NONE;
    }

    {
        /* AH-23。以降は RNG を消費しない（純粋な参照・分岐だけ）。 */
        an_rng_snapshot an_h23_snap;
        const int an_h23_hallucinating =
            (character_generated && player && (player->timed[TMD_IMAGE] > 0)) ? 1 : 0;
        if (an_h23_hallucinating) {
            an_rng_save(&an_h23_snap);
        }
        map_info(grid, &g);
        if (an_h23_hallucinating) {
            an_rng_restore(&an_h23_snap);
        }
    }

    /*
     * 見直しの指摘 3。優先順位は `presentation_bridge.cpp` の `fill_minimap()` と同じ順番
     * （後の代入が前を上書き）——罠・宝石を先に見て、アイテム・金で上書きし、
     * 最後に敵で上書きする。
     */
    if (g.trap && (trf_has(g.trap->flags, TRF_VISIBLE) || trf_has(g.trap->flags, TRF_GLYPH)
            || trf_has(g.trap->flags, TRF_WEB))) {
        marker = AN_MARKER_TRAP;
    }
    feature = &f_info[g.f_idx];
    if (tf_has(feature->flags, TF_GOLD)) {
        marker = AN_MARKER_TREASURE;
    }
    if (g.unseen_money) {
        marker = AN_MARKER_TREASURE;
    } else if (g.first_kind || g.unseen_object) {
        marker = AN_MARKER_ITEM;
    }
    if ((g.m_idx > 0) && !g.hallucinate) {
        marker = AN_MARKER_MONSTER;
    }
    return marker;
}

int an_get_floor_info(an_floor_info *out)
{
    if (!out) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    if (!an_map_ready()) {
        return 0;
    }
    out->depth = player ? player->depth : 0;
    out->is_town = (out->depth == 0) ? 1 : 0;
    out->daytime = is_daytime() ? 1 : 0;
    /*
     * 見直しの指摘 4：`turn % 周期 == 0` が夜明け（`game-world.c:555`）なので、
     * `pos=0` は真夜中ではなく日の出（day_minute≈360=6:00）に対応する。
     * `+360` して循環させる（`presentation/frame/game_frame.h:416` の契約の注釈を参照）。
     */
    if (z_info && (z_info->day_length > 0)) {
        const long span = 10L * (long)z_info->day_length;
        const long pos = ((long)turn % span);
        out->day_minute = (int)(((pos * 1440L) / span + 360L) % 1440L);
    }
    out->light_radius = player ? player->state.cur_light : 0;
    out->light_kind = an_player_light_kind();
    out->generated_turn = cave ? (long long)cave->turn : 0;
    /* 作り方（A6 の記録を `cave->turn` で引く。町は 0）。 */
    out->profile_id = (out->depth > 0) ? an_profile_id_for_turn(out->generated_turn) : 0;
    return 1;
}

int an_get_player_race_name(char *out, int cap)
{
    if (!out || (cap <= 0)) {
        return 0;
    }
    out[0] = '\0';
    if (!character_generated || !player || !player->race) {
        return 0;
    }
    an_copy_name(out, (size_t)cap, player->race->name);
    return 1;
}

int an_get_hud(an_hud_info *out)
{
    if (!out) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    if (!character_generated || !player) {
        return 0;
    }
    an_copy_name(out->name, sizeof(out->name), player->full_name);
    out->hp = player->chp;
    out->hp_max = player->mhp;
    out->sp = player->csp;
    out->sp_max = player->msp;
    out->gold = player->au;
    out->depth = player->depth;
    out->level = player->lev;
    return 1;
}

/* ==================================================================== メッセージ */

#define AN_MSG_QUEUE_CAP 256

typedef struct an_msg_entry {
    char text[256];
    unsigned char color;
} an_msg_entry;

static an_msg_entry an_msg_queue[AN_MSG_QUEUE_CAP];
static int an_msg_head = 0;
static int an_msg_tail = 0;
static int an_msg_count = 0;
static unsigned long long an_msg_seq = 0;

static void an_message_handler(game_event_type type, game_event_data *data, void *user)
{
    an_msg_entry *slot;

    (void)type;
    (void)user;

    if (!data || !data->message.msg) {
        return;
    }

    if (an_msg_count >= AN_MSG_QUEUE_CAP) {
        /* 溢れたら一番古いものを捨てる（取りこぼされるより古いメッセージの消失のほうが実害が小さい）。 */
        an_msg_head = (an_msg_head + 1) % AN_MSG_QUEUE_CAP;
        an_msg_count--;
    }

    slot = &an_msg_queue[an_msg_tail];
    strncpy(slot->text, data->message.msg, sizeof(slot->text) - 1);
    slot->text[sizeof(slot->text) - 1] = '\0';
    slot->color = an_round_color(message_type_color((uint16_t)data->message.type));

    an_msg_tail = (an_msg_tail + 1) % AN_MSG_QUEUE_CAP;
    an_msg_count++;
    an_msg_seq++;
}

void an_install_message_hook(void)
{
    /* 付け直し（2 回目のゲーム）でも二重にならないよう、先に外す（無ければ何もしない）。 */
    event_remove_handler(EVENT_MESSAGE, an_message_handler, NULL);
    event_add_handler(EVENT_MESSAGE, an_message_handler, NULL);
}

void an_push_synthetic_message(const char *text, int color)
{
    an_msg_entry *slot;

    if (!text) {
        return;
    }
    /*
     * AH-01/AH-02 の直し後、`fill_messages()`（`an_frame.cpp`）はコア自身の
     * メッセージ履歴（`messages_num()`/`message_str()`）だけを毎フレーム読むので、
     * ここでキューへ積むだけでは画面に出ない。実の履歴へも積む。
     * `color` 引数は実の履歴の色（`message_type_color()`）には掛からない
     * （型で決まる。pref が無いので既定は白）。このアダプタ発の文言は稀な
     * 警告のみなので実害は無いと見て、ここでは追わない。
     */
    message_add(text, MSG_GENERIC);
    if (an_msg_count >= AN_MSG_QUEUE_CAP) {
        an_msg_head = (an_msg_head + 1) % AN_MSG_QUEUE_CAP;
        an_msg_count--;
    }
    slot = &an_msg_queue[an_msg_tail];
    strncpy(slot->text, text, sizeof(slot->text) - 1);
    slot->text[sizeof(slot->text) - 1] = '\0';
    slot->color = (unsigned char)an_round_color(color);
    an_msg_tail = (an_msg_tail + 1) % AN_MSG_QUEUE_CAP;
    an_msg_count++;
    an_msg_seq++;
}

unsigned long long an_message_event_count(void)
{
    return an_msg_seq;
}

int an_take_message(char *out, int cap, int *color_out)
{
    an_msg_entry *slot;

    if (!out || (cap <= 0) || (an_msg_count <= 0)) {
        return 0;
    }

    slot = &an_msg_queue[an_msg_head];
    strncpy(out, slot->text, (size_t)cap - 1);
    out[cap - 1] = '\0';
    if (color_out) {
        *color_out = slot->color;
    }

    an_msg_head = (an_msg_head + 1) % AN_MSG_QUEUE_CAP;
    an_msg_count--;
    return 1;
}

/*!
 * @brief `an_mark_messages_ready()` が立つまでは偽（実測でクラッシュしたため）。
 * @details `an_message_event_count()` の註記のとおり、コアの `messages_num()` は
 * `messages`（`message.c` の内部状態）が `messages_init()`（`init_angband()` の中の
 * `messages_module.init`）で作られる前——`EVENT_ENTER_INIT` の直後など——に呼ぶと
 * 落ちる。`host_present()`（ゆえに `build_frame()`／`fill_messages()`）はその
 * 前にも呼ばれることがある（savefile 選択の画面が出るより前の 1 枚目）ので、
 * `an_bootstrap.c` が `init_angband()` から戻った直後に立てるまでは 0 件として扱う。
 */
static int an_messages_ready = 0;

void an_mark_messages_ready(void)
{
    an_messages_ready = 1;
}

/*!
 * @brief AH-02 の直し。直近の実メッセージ履歴を、取りこぼしなく毎フレーム同じ形で読む。
 * `an_take_message()`（先入れ先出しで取り出すと消える）と違い、こちらは読むだけで
 * 履歴を消費しない。手本は `presentation_bridge.cpp` の `fill_messages()`
 *（`message_num()`/`message_str()` を使い、画面側へ直近 N 行を毎回まるごと送る）。
 * `age` は 0 が最新（`message_str()` と同じ約束）。
 */
int an_recent_message_count(void)
{
    if (!an_messages_ready || play_again) { /* `play_again`：`cleanup_angband()` が `messages` を解放している間 */
        return 0;
    }
    return (int)messages_num();
}

int an_get_recent_message(int age, char *out, int cap, int *color_out)
{
    const char *text;

    if (!out || (cap <= 0)) {
        return 0;
    }
    out[0] = '\0';
    if (!an_messages_ready || play_again) {
        return 0;
    }
    if ((age < 0) || (age >= (int)messages_num())) {
        return 0;
    }

    text = message_str((uint16_t)age);
    if (!text) {
        return 0;
    }
    strncpy(out, text, (size_t)cap - 1);
    out[cap - 1] = '\0';
    if (color_out) {
        *color_out = an_round_color(message_color((uint16_t)age));
    }
    return 1;
}

/* ==================================================================== サブパネル（設計 §6.3） */

int an_equipment_lines(char out[][AN_SUBLIST_LINE_CAP], int max_lines)
{
    int i;
    int n = 0;

    if (!out || (max_lines <= 0)) {
        return 0;
    }
    if (!character_generated || !player) {
        return 0;
    }

    for (i = 0; (i < (int)player->body.count) && (n < max_lines); i++) {
        struct equip_slot *slot = &player->body.slots[i];
        char desc[192];
        if (!slot->obj) {
            continue; /* 空きスロットは出さない（要約なので）。 */
        }
        (void)object_desc(desc, sizeof(desc), slot->obj, ODESC_PREFIX | ODESC_FULL, player);
        /* P4: 部位の札（`weapon` ほか）は F: で引く（英語ではそのまま）。 */
        snprintf(out[n], AN_SUBLIST_LINE_CAP, "%s: %s", slot->name ? an_tr_tag('F', slot->name) : "?", desc);
        n++;
    }
    return n;
}

/*!
 * @details 見直しの指摘 6。以前は `player->gear`（装備・持ち物・矢筒を混ぜた内部の
 * 連結リスト）を先頭から辿り、出す順に `'a'+n` で記号を振っていた——`gear` の並びは
 * 拾った順であって、ゲームが実際に表示する記号（`player->upkeep->inven[]` の
 * 添字）とは無関係なので、ゲーム画面の "c) Something" とサブパネルの記号がずれる。
 * 原作の表示（`ui-object.c` の `show_inven()`/`build_obj_list()`）と同じく、
 * `player->upkeep->inven[]` を添字どおりに読み、記号も同じ `all_letters_nohjkl[i]`
 * を使う。矢筒は同様に `player->upkeep->quiver[]` を添字どおりに読み、原作の
 * `show_quiver()` と同じ `I2D(i)`（'0'..)を記号にする。
 */
int an_inventory_lines(char out[][AN_SUBLIST_LINE_CAP], int max_lines, int *skipped_out)
{
    int n = 0;
    int skipped = 0;
    int i;

    if (skipped_out) {
        *skipped_out = 0;
    }
    if (!out || (max_lines <= 0)) {
        return 0;
    }
    if (!character_generated || !player) {
        return 0;
    }

    for (i = 0; i < z_info->pack_size; i++) {
        struct object *obj = player->upkeep->inven[i];
        char desc[192];
        if (!obj) {
            continue;
        }
        if (n >= max_lines) {
            skipped++;
            continue;
        }
        (void)object_desc(desc, sizeof(desc), obj, ODESC_PREFIX | ODESC_FULL, player);
        snprintf(out[n], AN_SUBLIST_LINE_CAP, "%c) %s", all_letters_nohjkl[i], desc);
        n++;
    }
    for (i = 0; i < z_info->quiver_size; i++) {
        struct object *obj = player->upkeep->quiver[i];
        char desc[192];
        if (!obj) {
            continue;
        }
        if (n >= max_lines) {
            skipped++;
            continue;
        }
        (void)object_desc(desc, sizeof(desc), obj, ODESC_PREFIX | ODESC_FULL, player);
        snprintf(out[n], AN_SUBLIST_LINE_CAP, "%c) %s", (char)I2D(i), desc);
        n++;
    }
    if (skipped_out) {
        *skipped_out = skipped;
    }
    return n;
}

int an_visible_monster_lines(char out[][AN_SUBLIST_LINE_CAP], int max_lines)
{
    monster_list_t *list;
    size_t i;
    int n = 0;

    if (!out || (max_lines <= 0)) {
        return 0;
    }
    if (!an_map_ready()) {
        return 0;
    }
    if (character_generated && player && (player->timed[TMD_IMAGE] > 0)) {
        /*
         * 見直しの指摘 7。原作 `ui-mon-list.c` の `monster_list_format_special()` と
         * 同じく、幻覚中（`TMD_IMAGE`）は本当の種族名を一切出さない
         * （さもないと、視界の敵のサブパネルだけ幻覚を無視して正体が漏れる）。
         */
        if (max_lines > 0) {
            snprintf(out[0], AN_SUBLIST_LINE_CAP, "%s",
                an_tr("Your hallucinations are too wild to see things clearly."));
            return 1;
        }
        return 0;
    }

    list = monster_list_shared_instance();
    if (!list) {
        return 0;
    }
    monster_list_reset(list);
    monster_list_collect(list);
    monster_list_sort(list, monster_list_standard_compare);

    for (i = 0; (i < list->distinct_entries) && (n < max_lines); i++) {
        const monster_list_entry_t *entry = &list->entries[i];
        const int count = (int)entry->count[MONSTER_LIST_SECTION_LOS];
        if ((count <= 0) || !entry->race) {
            continue; /* 視認できている（LOS）ものだけ。ESP 感知だけの分は出さない。 */
        }
        /* P4: 種族の名は名前の表で引く。数の書き方は日本語では「名 N体」（F: の型。名が先）。 */
        const char *race_name = an_name("monster", entry->race->name);
        if (!race_name) {
            race_name = entry->race->name;
        }
        if ((count > 1) && an_lang_enabled()) {
            snprintf(out[n], AN_SUBLIST_LINE_CAP, an_tr_tag('F', "%s x%d"), race_name, count);
        } else if (count > 1) {
            snprintf(out[n], AN_SUBLIST_LINE_CAP, "%d x %s", count, race_name);
        } else {
            snprintf(out[n], AN_SUBLIST_LINE_CAP, "%s", race_name);
        }
        n++;
    }
    return n;
}

/* ==================================================================== 状態のアイコン */

/*
 * `GameFrame::status_effects`（決めたこと 2026-10-11）。
 *
 * 範囲:
 *  - 速さ：原作の `prt_speed_aux()`（`player->state.speed`。110 が標準）。効果量は `+10` など原作が出す数。
 *    `TMD_FAST`・`TMD_SLOW` は速さに含まれるので、重ねて出さない。
 *  - 一時的な状態：原作の `prt_tmd()` と同じ（`player->timed[]` が 0 でなく、段階の名前があるもの）。段階の名前の
 *    字は日本語層の `F:` 札（`an_tr_tag('F', 名前)`）。満腹（`Full`）は空腹の絵（`hungry`）で出す。普通（`Fed`）は出さない。
 *    血の渇き（`TMD_BLOODLUST`）は段階を効果量に出す。恐怖（TERROR）・疾走（SPRINT）・挑発（TAUNT）・血の渇き・支配・
 *    攻撃して逃げるは特殊（種類 3）。
 *  - 待っている状態：帰還（`word_recall`）・降下（`deep_descent`）。
 *  - 常時の効果：キャラクターシートの耐性の表（`ui_entry.txt` の `CHAR_SCREEN1`）と**同じ道**
 *    （`compute_ui_entry_values_for_object()` / `_for_player()`）で、鑑定済みの旗・既知の属性だけを拾う（未鑑定は出さない）。
 *    項目の取り方は下の `an_perm_resolve()`（旗や属性を 1 つだけ持つ見本の物を作って項目を引く）。
 *  - 出さないもの：レベルの感じ・足元の明るさ・「無視を外す」・勉強・罠探知・地形（状態ではない）、休息と繰り返しの回数、
 *    残りターン。
 * 読むだけでコアの状態は変えない。
 */

#include "ui-entry.h"
#include "ui-entry-combiner.h"
#include "project.h" /* projections[]（弱点の属性名） */

/*! @name 状態の種類（`StatusEffectKind` と同じ値） @{ */
#define AN_SK_GOOD 0
#define AN_SK_BAD 1
#define AN_SK_WAIT 2
#define AN_SK_SPECIAL 3
#define AN_SK_PERM 4
#define AN_SK_PERM_BAD 5
/*! @} */

/*! @brief `TMD_*` 1 つぶんの対応。`key` が NULL のものは、理由（`excuse`）を持つ。 */
typedef struct an_timed_icon {
    const char *key;
    int kind;
    const char *ja; /*!< 日本語層が起きていて `F:` 札が無いときの字。無ければ NULL */
    const char *excuse; /*!< `key` が NULL のときの理由（`TMD_FOOD` は専用の枝） */
} an_timed_icon;

static const an_timed_icon an_timed_icons[TMD_MAX] = {
    [TMD_FAST] = { NULL, 0, NULL, "speed" }, /* 速さは state.speed から出す */
    [TMD_SLOW] = { NULL, 0, NULL, "speed" },
    [TMD_BLIND] = { "blind", AN_SK_BAD, NULL, NULL },
    [TMD_PARALYZED] = { "paralyzed", AN_SK_BAD, NULL, NULL },
    [TMD_CONFUSED] = { "confused", AN_SK_BAD, NULL, NULL },
    [TMD_AFRAID] = { "afraid", AN_SK_BAD, NULL, NULL },
    [TMD_IMAGE] = { "hallucination", AN_SK_BAD, NULL, NULL },
    [TMD_POISONED] = { "poisoned", AN_SK_BAD, NULL, NULL },
    [TMD_CUT] = { "cut", AN_SK_BAD, NULL, NULL },
    [TMD_STUN] = { "stun", AN_SK_BAD, NULL, NULL },
    [TMD_FOOD] = { "hungry", AN_SK_BAD, NULL, NULL }, /* 専用の枝（段階で出し分ける） */
    [TMD_PROTEVIL] = { "protevil", AN_SK_GOOD, NULL, NULL },
    [TMD_INVULN] = { "invuln", AN_SK_GOOD, NULL, NULL },
    [TMD_HERO] = { "hero", AN_SK_GOOD, NULL, NULL },
    [TMD_SHERO] = { "berserk", AN_SK_GOOD, NULL, NULL },
    [TMD_SHIELD] = { "magicdef", AN_SK_GOOD, NULL, NULL },
    [TMD_BLESSED] = { "blessed", AN_SK_GOOD, NULL, NULL },
    [TMD_SINVIS] = { "senseunseen", AN_SK_GOOD, NULL, NULL },
    [TMD_SINFRA] = { "infravision", AN_SK_GOOD, NULL, NULL },
    [TMD_OPP_ACID] = { "res_acid", AN_SK_GOOD, NULL, NULL },
    [TMD_OPP_ELEC] = { "res_elec", AN_SK_GOOD, NULL, NULL },
    [TMD_OPP_FIRE] = { "res_fire", AN_SK_GOOD, NULL, NULL },
    [TMD_OPP_COLD] = { "res_cold", AN_SK_GOOD, NULL, NULL },
    [TMD_OPP_POIS] = { "res_pois", AN_SK_GOOD, NULL, NULL },
    [TMD_OPP_CONF] = { "res_conf", AN_SK_GOOD, NULL, NULL },
    [TMD_AMNESIA] = { "an_amnesia", AN_SK_BAD, NULL, NULL },
    [TMD_TELEPATHY] = { "telepathy", AN_SK_GOOD, "テレパシー", NULL },
    [TMD_STONESKIN] = { "stoneskin", AN_SK_GOOD, NULL, NULL },
    [TMD_TERROR] = { "afraid", AN_SK_SPECIAL, NULL, NULL },
    [TMD_SPRINT] = { "haste", AN_SK_SPECIAL, NULL, NULL },
    [TMD_BOLD] = { "res_fear", AN_SK_GOOD, NULL, NULL },
    [TMD_SCRAMBLE] = { "an_scramble", AN_SK_BAD, NULL, NULL },
    [TMD_TRAPSAFE] = { "an_trapsafe", AN_SK_GOOD, NULL, NULL },
    [TMD_FASTCAST] = { "an_fastcast", AN_SK_GOOD, NULL, NULL },
    [TMD_ATT_ACID] = { "attk_acid", AN_SK_GOOD, NULL, NULL },
    [TMD_ATT_ELEC] = { "attk_elec", AN_SK_GOOD, NULL, NULL },
    [TMD_ATT_FIRE] = { "attk_fire", AN_SK_GOOD, NULL, NULL },
    [TMD_ATT_COLD] = { "attk_cold", AN_SK_GOOD, NULL, NULL },
    [TMD_ATT_POIS] = { "attk_pois", AN_SK_GOOD, NULL, NULL },
    [TMD_ATT_CONF] = { "attk_conf", AN_SK_GOOD, NULL, NULL },
    [TMD_ATT_EVIL] = { "slay_evil", AN_SK_GOOD, NULL, NULL },
    [TMD_ATT_DEMON] = { "slay_demon", AN_SK_GOOD, NULL, NULL },
    [TMD_ATT_VAMP] = { "vampiric", AN_SK_GOOD, NULL, NULL },
    [TMD_HEAL] = { "regeneration", AN_SK_GOOD, NULL, NULL },
    [TMD_COMMAND] = { "an_command", AN_SK_SPECIAL, NULL, NULL },
    [TMD_ATT_RUN] = { "an_att_run", AN_SK_SPECIAL, NULL, NULL },
    [TMD_COVERTRACKS] = { "stealth", AN_SK_GOOD, NULL, NULL },
    [TMD_POWERSHOT] = { "an_powershot", AN_SK_GOOD, NULL, NULL },
    [TMD_TAUNT] = { "aggravate", AN_SK_SPECIAL, NULL, NULL },
    [TMD_BLOODLUST] = { "an_bloodlust", AN_SK_SPECIAL, NULL, NULL },
    [TMD_BLACKBREATH] = { "an_blackbreath", AN_SK_BAD, NULL, NULL },
    [TMD_STEALTH] = { "superstealth", AN_SK_GOOD, NULL, NULL },
    [TMD_FREE_ACT] = { "free_act", AN_SK_GOOD, NULL, NULL },
};

/*! @brief `an_status_effect` を 1 つ足す。`cap` を超えたら捨てる。末尾の「!」は落とす（`Paralyzed!`）。 */
static int an_status_add(an_status_effect *out, int n, int cap, const char *key, const char *label,
    const char *amount, int kind)
{
    an_status_effect *e;
    size_t len;

    if ((out == NULL) || (n >= cap)) {
        return n;
    }
    e = &out[n];
    memset(e, 0, sizeof(*e));
    snprintf(e->key, sizeof(e->key), "%s", key);
    snprintf(e->label, sizeof(e->label), "%s", label ? label : "");
    snprintf(e->amount, sizeof(e->amount), "%s", amount ? amount : "");
    e->kind = kind;
    len = strlen(e->label);
    if ((len >= 1) && (e->label[len - 1] == '!')) {
        e->label[len - 1] = '\0';
    } else if ((len >= 3) && ((unsigned char)e->label[len - 3] == 0xEF) && ((unsigned char)e->label[len - 2] == 0xBC)
        && ((unsigned char)e->label[len - 1] == 0x81)) {
        e->label[len - 3] = '\0'; /* 全角の「！」 */
    }
    return n + 1;
}

/*! @brief 段階の名前の字。日本語層が起きていれば `F:` 札、無ければ `ja`（あれば）、英語層ならそのまま。 */
static const char *an_status_tr(const char *en, const char *ja)
{
    const char *t = an_tr_tag('F', en);

    if (an_lang_enabled() && (t == en) && (ja != NULL)) {
        return ja;
    }
    return t;
}

/*! @brief 弱点の属性の名前（`projection.txt` の `desc`。日本語層が起きていれば日本語）。先頭は大文字にする。 */
static const char *an_status_element_name(int elem, char *buf, size_t cap)
{
    const char *desc = ((projections != NULL) && (elem >= 0) && (elem < ELEM_MAX)) ? projections[elem].desc : NULL;

    snprintf(buf, cap, "%s", desc ? desc : "");
    if ((buf[0] >= 'a') && (buf[0] <= 'z')) {
        buf[0] = (char)(buf[0] - 'a' + 'A');
    }
    return buf;
}

/* ---------------------------------------------------------------- 常時の効果 */

/*! 項目の取り方の種類。 */
enum { AN_PT_FLAG, AN_PT_SUST, AN_PT_MOD, AN_PT_ELEM };

/*! @brief 常時の効果の表の 1 行。`ui` は初回に引く（`an_perm_resolve()`）。 */
typedef struct an_perm_row {
    int type;
    int index; /*!< `OF_*`（FLAG・SUST）／ `OBJ_MOD_*`（MOD）／ `ELEM_*`（ELEM） */
    const char *key; /*!< 耐性・能力の絵 */
    const char *imm_key; /*!< 免疫の絵（ELEM だけ。無ければ `key`） */
    int bad; /*!< 1 = 悪い効果（呪い。種類 5） */
    const char *entry; /*!< `ui_entry.txt` の名前（札の引き先。NULL なら下の `ja`/`en` をそのまま使う） */
    const char *label_en; /*!< 札の英語（`entry` があるときは `F:` 札の鍵に使う） */
    const char *ja; /*!< 日本語層が起きていて `F:` 札が無い（または `entry` が NULL の）ときの字 */
    const struct ui_entry *ui;
} an_perm_row;

static an_perm_row an_perm_rows[] = {
    /* 耐性・免疫・弱点（属性） */
    { AN_PT_ELEM, ELEM_ACID, "res_acid", "imm_acid", 0, "resist_ui_compact_0<ACID>", "Acid", NULL, NULL },
    { AN_PT_ELEM, ELEM_ELEC, "res_elec", "imm_elec", 0, "resist_ui_compact_0<ELEC>", "Elec", NULL, NULL },
    { AN_PT_ELEM, ELEM_FIRE, "res_fire", "imm_fire", 0, "resist_ui_compact_0<FIRE>", "Fire", NULL, NULL },
    { AN_PT_ELEM, ELEM_COLD, "res_cold", "imm_cold", 0, "resist_ui_compact_0<COLD>", "Cold", NULL, NULL },
    { AN_PT_ELEM, ELEM_POIS, "res_pois", NULL, 0, "resist_ui_compact_0<POIS>", "Poison", NULL, NULL },
    { AN_PT_ELEM, ELEM_LIGHT, "res_lite", NULL, 0, "resist_ui_compact_0<LIGHT>", "Light", NULL, NULL },
    { AN_PT_ELEM, ELEM_DARK, "res_dark", NULL, 0, "resist_ui_compact_0<DARK>", "Dark", NULL, NULL },
    { AN_PT_ELEM, ELEM_SOUND, "res_sound", NULL, 0, "resist_ui_compact_0<SOUND>", "Sound", NULL, NULL },
    { AN_PT_ELEM, ELEM_SHARD, "res_shard", NULL, 0, "resist_ui_compact_0<SHARD>", "Shard", NULL, NULL },
    { AN_PT_ELEM, ELEM_NEXUS, "res_nexus", NULL, 0, "resist_ui_compact_0<NEXUS>", "Nexus", NULL, NULL },
    { AN_PT_ELEM, ELEM_NETHER, "res_neth", NULL, 0, "resist_ui_compact_0<NETHER>", "Nether", NULL, NULL },
    { AN_PT_ELEM, ELEM_CHAOS, "res_chaos", NULL, 0, "resist_ui_compact_0<CHAOS>", "Chaos", NULL, NULL },
    { AN_PT_ELEM, ELEM_DISEN, "res_disen", NULL, 0, "resist_ui_compact_0<DISEN>", "Disenchantment", NULL, NULL },
    /* 能力（良い旗） */
    { AN_PT_FLAG, OF_PROT_FEAR, "res_fear", NULL, 0, "pfear_ui_compact_0", "Fear resistance", NULL, NULL },
    { AN_PT_FLAG, OF_PROT_BLIND, "res_blind", NULL, 0, "pblind_ui_compact_0", "Blindess resistance", NULL, NULL },
    { AN_PT_FLAG, OF_PROT_CONF, "res_conf", NULL, 0, "pconf_ui_compact_0", "Confusion resistance", NULL, NULL },
    { AN_PT_FLAG, OF_PROT_STUN, "res_stun", NULL, 0, "pstun_ui_compact_0", "Stun resistance", NULL, NULL },
    { AN_PT_FLAG, OF_HOLD_LIFE, "hold_exp", NULL, 0, "holdlife_ui_compact_0", "Hold life", NULL, NULL },
    { AN_PT_FLAG, OF_REGEN, "regeneration", NULL, 0, "regen_ui_compact_0", "Regeneration", NULL, NULL },
    { AN_PT_FLAG, OF_TELEPATHY, "telepathy", NULL, 0, "esp_ui_compact_0", "ESP", "テレパシー", NULL },
    { AN_PT_FLAG, OF_SEE_INVIS, "senseunseen", NULL, 0, "see_invis_ui_compact_0", "See invisible", NULL, NULL },
    { AN_PT_FLAG, OF_FREE_ACT, "free_act", NULL, 0, "free_action_ui_compact_0", "Free action", NULL, NULL },
    { AN_PT_FLAG, OF_FEATHER, "levitate", NULL, 0, "feather_falling_ui_compact_0", "Feather falling", NULL, NULL },
    { AN_PT_FLAG, OF_SLOW_DIGEST, "slow_digest", NULL, 0, "slow_digestion_ui_compact_0", "Slow digestion", NULL, NULL },
    { AN_PT_FLAG, OF_TRAP_IMMUNE, "an_trapsafe", NULL, 0, "trap_immunity_ui_compact_0", "Trap immunity", NULL, NULL },
    { AN_PT_FLAG, OF_BLESSED, "blessed", NULL, 0, "bless_ui_compact_0", "Bless", NULL, NULL },
    /* 能力値の維持（補助の値） */
    { AN_PT_SUST, OF_SUST_STR, "sust_str", NULL, 0, NULL, "Sustain STR", "腕力維持", NULL },
    { AN_PT_SUST, OF_SUST_INT, "sust_int", NULL, 0, NULL, "Sustain INT", "知力維持", NULL },
    { AN_PT_SUST, OF_SUST_WIS, "sust_wis", NULL, 0, NULL, "Sustain WIS", "賢さ維持", NULL },
    { AN_PT_SUST, OF_SUST_DEX, "sust_dex", NULL, 0, NULL, "Sustain DEX", "器用維持", NULL },
    { AN_PT_SUST, OF_SUST_CON, "sust_con", NULL, 0, NULL, "Sustain CON", "耐久力維持", NULL },
    /* 悪い旗（呪いなど） */
    { AN_PT_FLAG, OF_IMPAIR_HP, "slow_regen", NULL, 1, "imphp_ui_compact_0", "Impaired HP recovery", NULL, NULL },
    { AN_PT_FLAG, OF_IMPAIR_MANA, "drain_mana", NULL, 1, "impsp_ui_compact_0", "Impaired mana recovery", NULL, NULL },
    { AN_PT_FLAG, OF_AFRAID, "cowardice", NULL, 1, "fear_ui_compact_0", "Fear", NULL, NULL },
    { AN_PT_FLAG, OF_NO_TELEPORT, "no_tele", NULL, 1, "noteleport_ui_compact_0", "No teleportation", NULL, NULL },
    { AN_PT_FLAG, OF_AGGRAVATE, "aggravate", NULL, 1, "aggravate_ui_compact_0", "Aggravate", NULL, NULL },
    { AN_PT_FLAG, OF_DRAIN_EXP, "drain_exp", NULL, 1, "drainxp_ui_compact_0", "Drain experience", NULL, NULL },
    { AN_PT_FLAG, OF_STICKY, "cursed", NULL, 1, "sticky_ui_compact_0", "Sticky curse", NULL, NULL },
    { AN_PT_FLAG, OF_FRAGILE, "low_ac", NULL, 1, "fragile_ui_compact_0", "Fragile", NULL, NULL },
    /* 修正値（正のときだけ出す。速さは最下段の「速さ」が全部入りで出すので重ねない。光は常に付くので出さない） */
    { AN_PT_MOD, OBJ_MOD_STEALTH, "stealth", NULL, 0, "stealth_ui_compact_0", "Stealth", NULL, NULL },
    { AN_PT_MOD, OBJ_MOD_SEARCH, "search", NULL, 0, "search_ui_compact_0", "Search", NULL, NULL },
    { AN_PT_MOD, OBJ_MOD_INFRA, "infravision", NULL, 0, "infravision_ui_compact_0", "Infravision", NULL, NULL },
    { AN_PT_MOD, OBJ_MOD_TUNNEL, "tunnel", NULL, 0, "tunneling_ui_compact_0", "Tunneling", NULL, NULL },
    { AN_PT_MOD, OBJ_MOD_BLOWS, "blows", NULL, 0, "blows_ui_compact_0", "Attack speed", NULL, NULL },
    { AN_PT_MOD, OBJ_MOD_SHOTS, "xtra_shots", NULL, 0, "shots_ui_compact_0", "Shooting speed", NULL, NULL },
    { AN_PT_MOD, OBJ_MOD_MIGHT, "an_shooting_power", NULL, 0, "shooting_power_ui_compact_0", "Shooting power", NULL, NULL },
    { AN_PT_MOD, OBJ_MOD_DAM_RED, "an_damage_reduction", NULL, 0, "damage_reduction_ui_compact_0", "Damage reduction", NULL, NULL },
    { AN_PT_MOD, OBJ_MOD_MOVES, "an_movement_speed", NULL, 0, "movement_speed_ui_compact_0", "Movement speed", NULL, NULL },
};
#define AN_PERM_ROWS ((int)(sizeof(an_perm_rows) / sizeof(an_perm_rows[0])))
#define AN_MAX_SLOTS 32

static bool an_ui_pred(const struct ui_entry *entry, void *closure)
{
    (void)closure;
    return ui_entry_has_category(entry, "CHAR_SCREEN1");
}

/*! @brief 値が「知っている値」か（未知・不在でない）。 */
static bool an_val_known(int v)
{
    return (v != UI_ENTRY_UNKNOWN_VALUE) && (v != UI_ENTRY_VALUE_NOT_PRESENT);
}

/*!
 * @brief 表の各行に、キャラクターシートの項目（`struct ui_entry`）を結び付ける。
 * @details 項目は名前の型が見えない（`ui-entry.c` の中）ので、**旗・属性・修正値を 1 つだけ持つ見本の物**を作り、
 * `compute_ui_entry_values_for_object()`（プレイヤ無し＝全部知っている扱い）に通して、値が返る項目をその行の項目とする。
 * 札の言語に左右されない。項目が読み込まれていなければ（`init_angband()` の前）何もせず、次の呼び出しでやり直す。
 */
/*! 表の `ui` を引き終えたか。2 回目のゲーム（`cleanup_angband()` が項目を解放して `init_angband()` が読み直す）の前に
 * `an_perm_reset()` が偽に戻す——戻さないと解放済みの `struct ui_entry` を指したまま `assert(0)` で落ちる。 */
static bool an_perm_done = false;

void an_perm_reset(void)
{
    int row;

    for (row = 0; row < AN_PERM_ROWS; ++row) {
        an_perm_rows[row].ui = NULL;
    }
    an_perm_done = false;
}

static void an_perm_resolve(void)
{
    static struct object probe;
    const struct ui_entry *entries[96];
    struct ui_entry_iterator *it;
    int n = 0;
    int row;

    if (an_perm_done) {
        return;
    }
    it = initialize_ui_entry_iterator(an_ui_pred, NULL, "CHAR_SCREEN1");
    if (it == NULL) {
        return;
    }
    while ((count_ui_entry_iterator(it) > 0) && (n < (int)(sizeof(entries) / sizeof(entries[0])))) {
        entries[n++] = advance_ui_entry_iterator(it);
    }
    release_ui_entry_iterator(it);
    if (n == 0) {
        return;
    }

    for (row = 0; row < AN_PERM_ROWS; ++row) {
        an_perm_row *r = &an_perm_rows[row];
        int i;

        r->ui = NULL;
        for (i = 0; (i < n) && (r->ui == NULL); ++i) {
            struct cached_object_data *cache = NULL;
            int v = 0, a = 0;

            memset(&probe, 0, sizeof(probe));
            switch (r->type) {
            case AN_PT_FLAG:
            case AN_PT_SUST:
                of_on(probe.flags, r->index);
                break;
            case AN_PT_MOD:
                probe.modifiers[r->index] = 1;
                break;
            default: /* AN_PT_ELEM */
                probe.el_info[r->index].res_level = 1;
                break;
            }
            compute_ui_entry_values_for_object(entries[i], &probe, NULL, &cache, &v, &a);
            if (cache != NULL) {
                release_cached_object_data(cache);
            }
            if ((an_val_known(v) && (v != 0)) || (an_val_known(a) && (a != 0))) {
                r->ui = entries[i];
            }
        }
    }
    an_perm_done = true;
}

/*!
 * @brief 1 行ぶんの値を、装備（鑑定済みの分だけ）と @ について調べる。
 * @param pos 資源（耐性・旗・正の修正値・維持）があるか
 * @param imm 免疫があるか（属性だけ）
 * @param neg 弱点があるか（属性だけ）
 */
static void an_perm_scan(const an_perm_row *r, bool *pos, bool *imm, bool *neg)
{
    struct cached_object_data *caches[AN_MAX_SLOTS];
    struct cached_player_data *pcache = NULL;
    int vals[AN_MAX_SLOTS + 1], auxs[AN_MAX_SLOTS + 1];
    int count = player->body.count;
    int j;

    *pos = *imm = *neg = false;
    if (count > AN_MAX_SLOTS) {
        count = AN_MAX_SLOTS;
    }
    for (j = 0; j < count; ++j) {
        caches[j] = NULL;
        compute_ui_entry_values_for_object(r->ui, slot_object(player, j), player, &caches[j], &vals[j], &auxs[j]);
    }
    compute_ui_entry_values_for_player(r->ui, player, &pcache, &vals[count], &auxs[count]);

    for (j = 0; j <= count; ++j) {
        const int v = (r->type == AN_PT_SUST) ? auxs[j] : vals[j];

        if (!an_val_known(v)) {
            continue;
        }
        if (v == UI_ENTRY_RESIST0_RES_VUL) {
            *pos = true;
            *neg = true;
        } else if (v >= 3) {
            *imm = true;
            *pos = true;
        } else if (v >= 1) {
            *pos = true;
        } else if (v <= -1) {
            *neg = true;
        }
    }

    if (pcache != NULL) {
        release_cached_player_data(pcache);
    }
    for (j = 0; j < count; ++j) {
        if (caches[j] != NULL) {
            release_cached_object_data(caches[j]);
        }
    }
}

/*! @brief 常時の効果の札（`ui_entry` の `F:` 札。無ければ `ja`、英語層は `label_en`）。 */
static const char *an_perm_label(const an_perm_row *r)
{
    const char *t;

    if (r->entry == NULL) {
        return an_lang_enabled() ? r->ja : r->label_en;
    }
    t = an_ui_entry_label(r->entry, "label", r->label_en);
    if (an_lang_enabled() && (t == r->label_en) && (r->ja != NULL)) {
        return r->ja; /* 札の訳が無い */
    }
    return t;
}

int an_read_status_effects(an_status_effect *out, int cap)
{
    int n = 0;
    int i, row;
    const int japanese = an_lang_enabled();

    if ((out == NULL) || (cap <= 0) || !character_generated || (player == NULL)) {
        return 0;
    }

    /* (1) 速さ。 */
    {
        const int speed = player->state.speed - 110;
        char amount[16];

        if (speed > 0) {
            snprintf(amount, sizeof(amount), "%+d", speed);
            n = an_status_add(out, n, cap, "haste", japanese ? an_tr_tag('F', "Haste") : "Fast", amount, AN_SK_GOOD);
        } else if (speed < 0) {
            snprintf(amount, sizeof(amount), "%+d", speed);
            n = an_status_add(out, n, cap, "slow", japanese ? an_tr_tag('F', "Slow") : "Slow", amount, AN_SK_BAD);
        }
    }

    /* (2) 一時的な状態（`prt_tmd()` と同じ段の引き方）。 */
    for (i = 0; i < TMD_MAX; ++i) {
        const struct timed_grade *grade;
        const an_timed_icon *icon = &an_timed_icons[i];
        char amount[16] = "";

        if ((player->timed[i] == 0) || (icon->key == NULL)) {
            continue;
        }
        grade = timed_effects[i].grade;
        while ((grade != NULL) && (player->timed[i] > grade->max)) {
            grade = grade->next;
        }
        if ((grade == NULL) || (grade->name == NULL)) {
            continue;
        }
        if (i == TMD_FOOD) {
            /* 空腹の段階を 1 つの絵で（餓死寸前・失神寸前・衰弱・空腹・満腹）。普通（Fed）は出さない。 */
            if (strcmp(grade->name, "Fed") != 0) {
                n = an_status_add(out, n, cap, icon->key, an_status_tr(grade->name, icon->ja), "", icon->kind);
            }
            continue;
        }
        if (i == TMD_BLOODLUST) {
            /* 血の渇きは段階（1 から）を効果量に出す。 */
            const struct timed_grade *g = timed_effects[i].grade;
            int stage = 0;

            /* 先頭に名前の無い段階（0）が付くので、名前のある段階だけを数える。 */
            for (; g != NULL; g = g->next) {
                if (g->name != NULL) {
                    ++stage;
                }
                if (g == grade) {
                    break;
                }
            }
            snprintf(amount, sizeof(amount), "%d", stage);
        }
        n = an_status_add(out, n, cap, icon->key, an_status_tr(grade->name, icon->ja), amount, icon->kind);
    }

    /* (3) 待っている状態。 */
    if (player->word_recall) {
        n = an_status_add(out, n, cap, "recall", an_status_tr("Recall", "帰還"), "", AN_SK_WAIT);
    }
    if (player->deep_descent) {
        n = an_status_add(out, n, cap, "an_descent", an_status_tr("Descent", "降下"), "", AN_SK_WAIT);
    }

    /* (4) 常時の効果（鑑定済みの旗・既知の属性だけ）。 */
    an_perm_resolve();
    for (row = 0; row < AN_PERM_ROWS; ++row) {
        const an_perm_row *r = &an_perm_rows[row];
        bool pos, imm, neg;

        if (r->ui == NULL) {
            continue;
        }
        an_perm_scan(r, &pos, &imm, &neg);
        switch (r->type) {
        case AN_PT_ELEM:
            if (imm) {
                n = an_status_add(out, n, cap, r->imm_key ? r->imm_key : r->key, an_perm_label(r), "", AN_SK_PERM);
            } else if (pos) {
                n = an_status_add(out, n, cap, r->key, an_perm_label(r), "", AN_SK_PERM);
            }
            if (neg) {
                char name[48];

                n = an_status_add(out, n, cap, "an_weakness", an_status_element_name(r->index, name, sizeof(name)), "",
                    AN_SK_PERM_BAD);
            }
            break;
        default:
            if (pos) {
                n = an_status_add(out, n, cap, r->key, an_perm_label(r), "", r->bad ? AN_SK_PERM_BAD : AN_SK_PERM);
            }
            break;
        }
    }
    if (player_has(player, PF_UNLIGHT)) {
        n = an_status_add(out, n, cap, "an_unlight", japanese ? "闇で見る" : "Unlight", "", AN_SK_PERM);
    }
    return n;
}

unsigned long an_status_stamp(void)
{
    unsigned long h = 14695981UL;
    int i;

#define AN_STAMP(V) (h = (h * 16777619UL) ^ (unsigned long)(V))
    if (!character_generated || (player == NULL)) {
        return h;
    }
    for (i = 0; i < TMD_MAX; ++i) {
        AN_STAMP(player->timed[i] != 0);
    }
    AN_STAMP(player->state.speed);
    AN_STAMP(player->word_recall != 0);
    AN_STAMP(player->deep_descent != 0);
    /* 装備の既知の旗・属性・修正値の数（ルーンを覚えるたびに動く）。 */
    for (i = 0; i < player->body.count; ++i) {
        const struct object *obj = slot_object(player, i);
        int e;

        AN_STAMP(obj != NULL);
        if ((obj == NULL) || (obj->known == NULL)) {
            continue;
        }
        AN_STAMP(obj->kind ? obj->kind->kidx : 0);
        AN_STAMP(of_count(obj->known->flags));
        for (e = 0; e < ELEM_MAX; ++e) {
            AN_STAMP(obj->known->el_info[e].res_level);
        }
        for (e = 0; e < OBJ_MOD_MAX; ++e) {
            AN_STAMP(obj->known->modifiers[e]);
        }
    }
    if (player->obj_k != NULL) {
        AN_STAMP(of_count(player->obj_k->flags));
    }
#undef AN_STAMP
    return h;
}

int an_status_timed_count(void)
{
    int i, n = 0;

    for (i = 0; i < TMD_MAX; ++i) {
        if (an_timed_icons[i].key != NULL) {
            ++n;
        }
    }
    return n;
}

int an_status_perm_row_count(void)
{
    return AN_PERM_ROWS;
}

/*! @brief 画面側の表（環境変数 `HENGBAND_STATUS_ICON_TABLE` のファイル）に鍵があるか。表が読めなければ -1。 */
static int an_icon_table_has(const char *key)
{
    static char *text = NULL;
    static bool tried = false;
    char needle[64];

    if (!tried) {
        const char *path = getenv("HENGBAND_STATUS_ICON_TABLE");
        FILE *fp = (path != NULL) ? fopen(path, "rb") : NULL;

        tried = true;
        if (fp != NULL) {
            long size;

            fseek(fp, 0, SEEK_END);
            size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if ((size > 0) && (size < 1000000L)) {
                text = (char *)malloc((size_t)size + 1);
                if (text != NULL) {
                    const size_t got = fread(text, 1, (size_t)size, fp);

                    text[got] = '\0';
                }
            }
            fclose(fp);
        }
    }
    if (text == NULL) {
        return -1;
    }
    snprintf(needle, sizeof(needle), "{ \"%s\",", key);
    return (strstr(text, needle) != NULL) ? 1 : 0;
}

int an_status_selftest(char *err, int cap, int *checked)
{
    int failures = 0;
    int i;
    static const char *const fixed_keys[] = { "haste", "slow", "recall", "an_descent", "an_unlight", "an_weakness" };

#define AN_SELFTEST_FAIL(...) \
    do { \
        ++failures; \
        if ((failures == 1) && (err != NULL) && (cap > 0)) { \
            snprintf(err, (size_t)cap, __VA_ARGS__); \
        } \
    } while (0)
#define AN_SELFTEST_KEY(K) \
    do { \
        const int has_ = an_icon_table_has(K); \
        if (has_ >= 0) { \
            if (checked != NULL) { \
                ++*checked; \
            } \
            if (has_ == 0) { \
                AN_SELFTEST_FAIL("key not in the icon table: %s", (K)); \
            } \
        } \
    } while (0)

    if (checked != NULL) {
        *checked = 0;
    }
    for (i = 0; i < TMD_MAX; ++i) {
        const an_timed_icon *icon = &an_timed_icons[i];

        if ((icon->key == NULL) && (icon->excuse == NULL)) {
            AN_SELFTEST_FAIL("TMD %d has neither an icon nor an excuse", i);
        } else if (icon->key != NULL) {
            AN_SELFTEST_KEY(icon->key);
        }
    }
    for (i = 0; i < (int)(sizeof(fixed_keys) / sizeof(fixed_keys[0])); ++i) {
        AN_SELFTEST_KEY(fixed_keys[i]);
    }
    AN_SELFTEST_KEY("hungry");
    an_perm_resolve();
    for (i = 0; i < AN_PERM_ROWS; ++i) {
        const an_perm_row *r = &an_perm_rows[i];

        AN_SELFTEST_KEY(r->key);
        if (r->imm_key != NULL) {
            AN_SELFTEST_KEY(r->imm_key);
        }
        if (r->ui == NULL) {
            AN_SELFTEST_FAIL("permanent row %d (%s) is not bound to a character-sheet entry", i, r->key);
        }
    }
#undef AN_SELFTEST_KEY
#undef AN_SELFTEST_FAIL
    return failures;
}
