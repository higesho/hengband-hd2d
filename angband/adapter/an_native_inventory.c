/*!
 * @file an_native_inventory.c
 * @brief `NativeInventoryMenu`向けの
 * 持ち物一覧の読み取り。`an_map_shim.h` が純 C ABI を宣言し、実体はここに置く。
 *
 * @details `an_map_shim.c` へ足さず別の翻訳単位に分けてあるのは、Sil-Q の
 * `sq_native_look.c`（`sq_shim.c` へは足さず、独立したファイルにしてある）と
 * 同じ流儀に合わせたため。
 *
 * **以前この註記に書いてあった「同じ翻訳単位に置くだけで
 * `home_empty_store_check.py` が止まる」という診断は誤りだった**（2026-09-24。
 * 「応答待ち」4 回目の調査）。あの検査が止まって見えた原因は 2 つとも
 * このファイルとも翻訳単位の配置とも無関係で、(1) `an_frame.cpp` の
 * `fill_messages()` が毎フレーム `messages[].seq` を増やしていたため、店を出た
 * 直後にゲームターンが進む間、同じ画のフレームが 3ms ごとに出て検査の
 * 「10 通まで読む」枠を使い切っていたこと、(2) 誕生時の持ち物が乱数で
 * 「a Ration of Food」1 個のときは原作の `textui_get_quantity()`（`max != 1` の
 * ときだけ問う）が数量を聞かず、検査が待つ `number` 段が来ないこと、である。
 * どちらも `--protocol-log=` の実測で確かめた（HEAD の版でも同じ率で起きる）。
 * 「呼ぶかどうかでは変わらず、リンクするかどうかで変わった」という二分探索の
 * 結果は、乱数の持ち物に依る揺れを差だと読んだものと見られる。
 *
 * AH-04。手本は
 * `silq/adapter/sq_native_look.c` の `sq_native_describe_item()`/
 * `sq_native_owned_item()`。
 */

#include "angband.h"

#include "game-world.h" /* character_generated */
#include "init.h" /* z_info */
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-util.h" /* player_can_cast() ほか（AH-53） */
#include "ui-menu.h" /* all_letters_nohjkl */
#include "ui-object.h" /* object_attr()/object_char() */

#include "an_lang_c.h" /* an_u_wt_int() ほか（設計 §19.3） */
#include "an_map_shim.h"
#include "an_shim.h" /* an_round_color() */
#include "an_ui_shim.h" /* AN_INV_CMD_DROP_ALL（AH-59） */

#include <string.h>
#include <stdio.h>

static void an_native_copy_name(char *dest, size_t cap, const char *src)
{
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, cap - 1);
    dest[cap - 1] = '\0';
}

int an_kind_name_and_tval(int kind_id, char *name_out, int cap, int *tval_out)
{
    struct object_kind *kind;

    if (!name_out || (cap <= 0)) {
        return 0;
    }
    name_out[0] = '\0';
    if (tval_out) {
        *tval_out = 0;
    }
    kind = objkind_byid(kind_id);
    if (!kind || !kind->name) {
        return 0;
    }
    an_native_copy_name(name_out, (size_t)cap, kind->name);
    if (tval_out) {
        *tval_out = (int)kind->tval;
    }
    return 1;
}

/*!
 * @details 手本は `silq/adapter/sq_native_look.c` の `sq_native_describe_item()`。
 * Angband は英語のまま取り込んだコアなので（前書き）、
 * 説明文（`out->description`）はここでは埋めない——`NativeInventoryMenu` の一覧行は
 * 選んで実行するのが主目的で、詳細は原作の `I`（examine）へそのまま委ねる。
 */
static void an_native_describe_item(struct object *obj, native_store_item *out)
{
    memset(out, 0, sizeof(*out));
    out->available = 1;
    out->selection = -1;
    if (!obj || !obj->kind) {
        return;
    }
    out->count = obj->number;
    out->kind_id = (int)obj->kind->kidx;
    out->glyph[0] = (char)object_char(obj);
    out->glyph_color = an_round_color(object_attr(obj));
    (void)object_desc(out->name, sizeof(out->name), obj, ODESC_PREFIX | ODESC_FULL, player);
    /* 設計 §19.3: 日本語は kg（変愚の lb_to_kg_integer()/fraction() と同じ換算）。 */
    snprintf(out->weight, sizeof(out->weight), an_lang_enabled() ? "%d.%d kg" : "%d.%d lb",
             an_u_wt_int(obj->weight), an_u_wt_frac(obj->weight));

    /*
     * AH-53: 魔法書の「唱える m」「覚える G」「読む b」と、使う U。判定は原作の `context_menu_object()`
     * （`ui-context.c`。アイテムの右クリックメニュー）と同じ。
     */
    if (obj_can_browse(obj)) {
        if (obj_can_cast_from(obj) && player_can_cast(player, false)) {
            native_item_command(out, 'm');
        }
        if (obj_can_study(obj) && player_can_study(player, false)) {
            native_item_command(out, 'G');
        }
        if (player_can_read(player, false)) {
            native_item_command(out, 'b');
        }
    }
    if (obj_is_useable(obj)) {
        native_item_command(out, 'U');
    }
    /*
     * AH-59: 装備中のアイテムには「外す t」だけ、装備していないアイテムには「装備する w」だけを出す。
     * 判定は原作の `context_menu_object()`（`ui-context.c`）と同じ——`obj_can_wear()` は
     * 装備中の物にも真なので、以前は装備中の行にも「装備する」が並んでいた。呪い（`OF_STICKY`）で
     * 外せない装備には `obj_can_takeoff()` が偽で「外す」も出ない。
     */
    if (object_is_equipped(player->body, obj)) {
        if (obj_can_takeoff(obj)) {
            native_item_command(out, 't');
        }
    } else if (obj_can_wear(obj)) {
        native_item_command(out, 'w');
    }
    if (tval_is_potion(obj)) {
        native_item_command(out, 'q');
    }
    if (tval_is_scroll(obj)) {
        native_item_command(out, 'r');
    }
    if (tval_is_edible(obj)) {
        native_item_command(out, 'E');
    }
    if (tval_is_wand(obj)) {
        native_item_command(out, 'a');
    }
    if (tval_is_rod(obj)) {
        native_item_command(out, 'z');
    }
    if (tval_is_staff(obj)) {
        native_item_command(out, 'u');
    }
    if (obj_can_refill(obj)) {
        native_item_command(out, 'F');
    }
    if (obj_can_activate(obj)) {
        native_item_command(out, 'A');
    }
    if (obj_can_fire(obj)) {
        native_item_command(out, 'f');
    }
    if (obj_can_throw(obj)) {
        native_item_command(out, 'v');
    }
    /*
     * AH-59: 2 個以上の山には「全部置く」も出す。原作の `context_menu_object()`（`ui-context.c`）は
     * 持ち物・我が家で `obj->number > 1` のときだけ「Drop All」を「Drop」の次に並べる。店の一覧が使う行
     * （売る・預ける）は `an_native_store_item()` が `commands` を空にするので、ここで足しても店には出ない。
     * 窓は操作の並びを逆にして出す（この関数が後に足したものほど上）ので、「置く」の**前**に足して、
     * 窓では「置く」の次に出る。
     */
    if (obj->number > 1) {
        native_item_command(out, AN_INV_CMD_DROP_ALL);
    }
    native_item_command(out, 'd');
    native_item_command(out, 'k');
    native_item_command(out, 'I');
    native_item_command(out, '{');
    if (obj->note) {
        native_item_command(out, '}');
    }
}

/*!
 * @details 装備 → パック → 矢筒の順に通し番号を振る。`slot`/`location` は
 * `NativeInventoryMenu` が子プロンプトとの突き合わせに使うだけの内部値であって、
 * 原作へ送るキーそのものではない（実際に送るキーは `build_pad_commands()` が
 * 出す表から引く。`presentation/bridge/native_inventory_menu.h` 参照）。
 */
int an_native_owned_item(int index, native_store_item *out)
{
    int seen = 0;
    int i;

    if (!out) {
        return 0;
    }
    if (!character_generated || !player) {
        return 0;
    }
    for (i = 0; i < (int)player->body.count; i++) {
        struct object *obj = player->body.slots[i].obj;
        if (!obj) {
            continue;
        }
        if (seen++ == index) {
            an_native_describe_item(obj, out);
            out->slot = i;
            out->location = 1;
            out->key = 0;
            return 1;
        }
    }
    for (i = 0; i < z_info->pack_size; i++) {
        struct object *obj = player->upkeep->inven[i];
        if (!obj) {
            continue;
        }
        if (seen++ == index) {
            an_native_describe_item(obj, out);
            out->slot = i;
            out->location = 0;
            out->key = (int)(unsigned char)all_letters_nohjkl[i];
            return 1;
        }
    }
    for (i = 0; i < z_info->quiver_size; i++) {
        struct object *obj = player->upkeep->quiver[i];
        if (!obj) {
            continue;
        }
        if (seen++ == index) {
            an_native_describe_item(obj, out);
            out->slot = 1000 + i;
            out->location = 0;
            out->key = (int)I2D(i);
            return 1;
        }
    }
    return 0;
}

/* ==================================================================== 店（AH-41） */

#include "store.h"
#include "cave.h"

extern struct menu *an_menu_current(void); /* an_bootstrap.c */
extern int an_store_session_active(void);   /* an_bootstrap.c（調-05） */

/*!
 * @brief いま店の一覧（`ui-store.c` の `store_menu_region` = 桁 1・行 4）が開いていれば、
 * その店を返す。`root` には「いま開いているメニューが店の一覧そのものか」を入れる
 * （一覧の上に開く「Command for …」の小さなメニューや「Sell which item?」の持ち物の一覧、
 * 数量の問いの間は 0）。
 */
static struct store *an_native_current_store(int *root, struct menu **list_menu)
{
    struct menu *menu = an_menu_current();
    struct store *store;

    *root = 0;
    *list_menu = NULL;
    if (!character_generated || !player || !cave) {
        return NULL;
    }
    /* 足元が店のマスなだけ（店に入って出た直後の入口）では店の画とは数えない（調-05・F-angband-13）。 */
    if (!an_store_session_active()) {
        return NULL;
    }
    store = store_at(cave, player->grid);
    if (!store) {
        return NULL;
    }
    if (menu && (menu->active.row == 4) && (menu->active.col == 1)) {
        *root = 1;
        *list_menu = menu;
    }
    return store;
}

int an_native_store_info(native_store_info *out)
{
    int root = 0;
    struct menu *menu = NULL;
    struct store *store;

    if (!out) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    store = an_native_current_store(&root, &menu);
    if (!store) {
        return 0;
    }
    out->active = 1;
    out->kind = (store->feat == FEAT_HOME) ? 2 : 1;
    out->top = menu ? menu->top : 0;
    out->page_size = menu ? MAX(1, menu->active.page_rows) : 1;
    out->own_page_size = 23;
    out->selection_style = 0;
    out->root_frame = root;
    out->get_key = 'p'; /* store_menu_handle(): 'p'/'g' は店でも我が家でも「買う・取る」 */
    out->put_key = 'd'; /* 's'/'d' はどちらも store_sell()（我が家では預ける） */
    out->previous_key = 0;
    out->next_key = 0;
    {
        /* 店の名（`General Store`・`Home` ほか）は店専用の表で引く（修-40）。表に無ければ従来どおり E: を引く。 */
        const char *store_name = f_info[store->feat].name;
        const char *store_ja = an_name("store", store_name);
        snprintf(out->title, sizeof(out->title), "%s", store_ja ? store_ja : an_tr(store_name));
    }
    return 1;
}

int an_native_store_item(int index, native_store_item *out)
{
    int root = 0;
    struct menu *menu = NULL;
    struct store *store = an_native_current_store(&root, &menu);
    struct object **list;
    int count;
    int i;
    int seen;

    if (!store || !out) {
        return 0;
    }
    list = mem_zalloc(sizeof(*list) * z_info->store_inven_max);
    store_stock_list(store, list, z_info->store_inven_max);
    for (count = 0; (count < z_info->store_inven_max) && list[count]; ++count) {
    }
    if (index < count) {
        struct object *obj = list[index];
        an_native_describe_item(obj, out);
        (void)object_desc(out->name, sizeof(out->name), obj, ODESC_PREFIX | ODESC_FULL | ODESC_STORE, player);
        out->commands[0] = '\0';
        out->tab = 0;
        out->slot = index;
        out->position = index;
        out->key = 0;
        if (menu && menu->selections && (index >= menu->top) && (index - menu->top < menu->active.page_rows)) {
            const int pos = index - menu->top;
            if (pos < (int)strlen(menu->selections)) {
                out->key = (unsigned char)menu->selections[pos];
            }
        }
        if (store->feat != FEAT_HOME) {
            out->price = price_item(store, obj, false, 1);
            out->price_known = 1;
            out->available = (player->au >= out->price) ? 1 : 0;
            if (!out->available) {
                snprintf(out->reason, sizeof(out->reason), "%s", an_tr("You do not have enough gold for this item."));
            }
        }
        mem_free(list);
        return 1;
    }
    mem_free(list);

    /* 持ち物側（売る・預ける）。store_sell() の選択肢（USE_EQUIP | USE_INVEN | USE_QUIVER）と同じ順。 */
    seen = count;
    for (i = 0; i < (int)player->body.count + z_info->pack_size + z_info->quiver_size; ++i) {
        struct object *obj;
        int location = 0;
        int slot;
        if (i < (int)player->body.count) {
            obj = player->body.slots[i].obj;
            location = 1;
            slot = i;
        } else if (i < (int)player->body.count + z_info->pack_size) {
            slot = i - (int)player->body.count;
            obj = player->upkeep->inven[slot];
        } else {
            slot = i - (int)player->body.count - z_info->pack_size;
            obj = player->upkeep->quiver[slot];
            slot += 1000;
        }
        if (!obj) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }
        an_native_describe_item(obj, out);
        out->commands[0] = '\0';
        out->tab = 1;
        out->location = location;
        out->slot = (location + 1) * 10000 + slot;
        out->position = slot;
        out->selection = -1;
        out->key = 0;
        if (store->feat == FEAT_HOME) {
            out->available = 1;
        } else {
            out->available = store_will_buy_tester(obj) ? 1 : 0;
            if (out->available) {
                out->price = price_item(store, obj, true, 1);
                out->price_known = 1;
            } else {
                snprintf(out->reason, sizeof(out->reason), "%s", an_tr("You have nothing that I want. "));
            }
        }
        return 1;
    }
    return 0;
}
