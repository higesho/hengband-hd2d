/*!
 * @file tb_native_store.c
 * @brief 商店・持ち物の「新しい画面」（`NativeStoreMenu` / `NativeInventoryMenu`）の材料。
 *
 * 幻想蛮怒の `gb_native_store.c` と同じ作り。原作の `store.c` を丸ごと `#include` して
 * `do_cmd_store()` を包み、商店の品物の一覧・値段・持ち物の行を、原作の関数
 * （`object_desc`・`object_char`・`object_attr`・`price_item`・`store_will_buy` ほか）を
 * **呼んで**作る。原作のファイルは書き換えず、関数の写しも持たない。
 * 組み立てでは `store.c` を単独では組まない（`TobandCore.vcxproj`・`platform/linux/CMakeLists.txt`・
 * `android/hd2d/src/main/cpp/CMakeLists.txt`）。このファイルが取り込む。
 *
 * ## 幻想蛮怒との違い（TOband の `store.c` の作り）
 * - 1 ページは **12 品で固定**（`store_bottom` は無い。`display_entry()` の `pos % 12`）。
 * - 値段の幅は関数 `min_inflate()` ではなく、店の主人の欄 `ot_ptr->min_inflate`・`ot_ptr->max_inflate`。
 * - 店の名前は `f_name + f_info[FEAT_SHOP_HEAD + 店の番号].name`（幻想蛮怒の `new_store_name` 表は無い）。
 *   主人の名前は `ow_name + ot_ptr->owner_name`。デネブの店（`FEAT_DENEB_SHOP`）は `STORE_BLACK` の番号で、
 *   ダンジョンの中にある（`display_store()` が店名を `FEAT_DENEB_SHOP` の地形名にする）。
 * - ダンジョンの中の我が家（`old_town_num == NO_TOWN`）は原作が品物の一覧を出さない
 *   （`c) クラスチェンジ` だけ）ので、新しい画面の対象にしない（0 を返し、従来の画面にする）。
 * - 値切りの入力（`get_haggle()` の `get_string()`）と命令待ち（`request_command()`）は、取り込む間だけ包み関数へ向ける
 *   （下の注記）。新しい画面が入力の段・店の一覧の画を確かに知るため。原作のファイルは書き換えない。
 * - 使う・唱えるなどの判定が `static` の関数（`item_tester_hook_wear`・`item_tester_learn_spell`）は
 *   アダプタから呼べない。原作を書き換えずに済ませるため、公開されている部品
 *   （`wield_slot`・`can_use_realm`・`party_book_caster`・`object_is_food` ほか）で作る。
 *   **着る・読むの判定は原作の完全な写しではない**（性別の制限・竜騎兵と魔女の本の限定は見ない。
 *   外れたときは原作が「着られるものがない」などのメッセージを出すだけ）。
 */

#include "angband.h"
/*
 * 値切りの入力（`get_haggle()` の `get_string()`。TOband は買い物のたびに「提示する金額?」と聞く）が、
 * 新しい画面で文字入力の段（`frame.text_input_active`）になるようにする。TOband の原作は `askfor_aux()` を
 * 旗で囲んでいない（幻想蛮怒はレシピで囲んでいる）ので、旗は常に偽のままだと、新しい画面は入力待ちに気づかない。
 * 原作は書き換えず、`store.c` を取り込む間だけ `get_string` を包み関数へ向ける。
 */
static bool tb_store_get_string(cptr prompt, char *buf, int len);
#define get_string(P, B, L) tb_store_get_string((P), (B), (L))
/*
 * 店の一覧の画（命令の入力待ち）かどうかを、画の行の位置の目安ではなく確かな旗で渡す（`native_store_info.root_frame`）。
 * 目安（`NativeStoreMenu` の `command_band`）は「Term の最後の 6 行」にある命令の字を見るので、主 Term が 28 行以上だと
 * 博物館の `d) アイテムを置く`（21 行目）が外れ、新しい画面にならない。`store.c` が命令を読むのは `request_command()` だけなので、
 * それを呼ぶ間だけ旗を立てる。
 */
static void tb_store_request_command(int shopping);
#define request_command(S) tb_store_request_command(S)
#define do_cmd_store tb_original_do_cmd_store
#include "store.c"
#undef do_cmd_store
#undef get_string
#undef request_command
#include "bridge/native_store_data.h"
#include "tb_unaware_category.h"
#include "tb_lang_c.h"

/*! 1 ページの品数。原作の `store_process_command()` の `store_top += 12` と同じ。 */
#define TB_STORE_PAGE 12

static int tb_modern_store_active = 0;

void tb_text_input_set(int active);

/*! 店の命令を読んでいる間だけ 1。`native_store_info.root_frame` の材料。 */
static int tb_store_at_root = 0;

static void tb_store_request_command(int shopping)
{
    tb_store_at_root = 1;
    request_command(shopping);
    tb_store_at_root = 0;
}

/*! `get_string()` を呼ぶ間だけ文字入力の旗を立てる（上の注記）。 */
static bool tb_store_get_string(cptr prompt, char *buf, int len)
{
    bool result;
    tb_text_input_set(1);
    result = get_string(prompt, buf, len);
    tb_text_input_set(0);
    return result;
}

/*!
 * @brief 持ち物・商品の 1 行ぶんの材料（名前・重さ・説明・使えるコマンド）を作る。
 *
 * 幻想蛮怒の `gb_native_describe_item` と同じ。武器・防具のように最初から種類が分かる物も、
 * 正体の分からない薬などの分類も、ここで決める。
 */
void tb_native_describe_item(const void *source, native_store_item *out)
{
    object_type copy = *(const object_type *)source;
    memset(out, 0, sizeof(*out));
    out->available = 1;
    out->selection = -1;
    out->count = copy.number;
    if (!copy.k_idx) {
        return;
    }
    out->kind_id = object_is_aware(&copy) ? copy.k_idx : 0;
    out->unaware_category = tb_unaware_category(&copy);
    out->glyph[0] = object_char(&copy);
    out->glyph_color = object_attr(&copy);
    object_desc(out->name, &copy, 0);
#ifdef JP
    snprintf(out->weight, sizeof(out->weight), "%d.%d kg", lbtokg1(copy.weight), lbtokg2(copy.weight));
#else
    snprintf(out->weight, sizeof(out->weight), "%d.%d lb", copy.weight / 10, copy.weight % 10);
#endif
    if (object_is_known(&copy) && copy.name1 && a_info[copy.name1].text) {
        snprintf(out->description, sizeof(out->description), "%s", tb_tr(a_text + a_info[copy.name1].text));
    }
    else if (object_is_aware(&copy) && k_info[copy.k_idx].text) {
        snprintf(out->description, sizeof(out->description), "%s", tb_tr(k_text + k_info[copy.k_idx].text));
    }
    native_item_command(out, 'I');
    native_item_command(out, '{');
    if (copy.inscription) {
        native_item_command(out, '}');
    }
    native_item_command(out, 'd');
    native_item_command(out, 'k');
    native_item_command(out, 'v');
    /* 着る（`do_cmd_wield` が選べる物）。`item_tester_hook_wear` は static なので `wield_slot` で見る。 */
    if (wield_slot((object_type *)source) >= INVEN_RARM) {
        native_item_command(out, 'w');
    }
    /* 撃つ（`do_cmd_fire` が選べる弾。装備中の武器の弾の種類 `tval_ammo`）。 */
    if (p_ptr->tval_ammo && copy.tval == p_ptr->tval_ammo) {
        native_item_command(out, 'f');
    }
    if (copy.tval == TV_FLASK || copy.tval == TV_LITE) {
        native_item_command(out, 'F');
    }
    /* 食べる・飲む・読む・始動・使う（`dungeon.c` のコマンドと `cmd6.c` の選択条件。どれも公開の関数）。 */
    if (object_is_food((object_type *)source)) {
        native_item_command(out, 'E');
    }
    if (object_is_potion((object_type *)source)) {
        native_item_command(out, 'q');
    }
    if (object_is_readable((object_type *)source)) {
        native_item_command(out, 'r');
    }
    if (object_is_activate((object_type *)source)) {
        native_item_command(out, 'A');
    }
    if (use_command && object_is_useable((object_type *)source)) {
        native_item_command(out, rogue_like_commands ? 'z' : 'u');
    }
    else {
        if (copy.tval == TV_STAFF) {
            native_item_command(out, 'u');
        }
        if (copy.tval == TV_WAND) {
            native_item_command(out, 'a');
        }
        if (copy.tval == TV_ROD) {
            native_item_command(out, 'z');
        }
    }
    /* 本：読む（`do_cmd_browse`）・唱える（`do_cmd_cast`）。TOband には覚えるコマンドが無い。
     * 唱える条件 `item_tester_party_book` は `party_book_caster()` そのもの。読む条件 `item_tester_learn_spell` は static。 */
    if (copy.tval >= TV_MAGERY_BOOK && copy.tval < TV_MAGERY_BOOK + MAX_REALM) {
        if (can_use_realm(tval2realm(copy.tval))) {
            native_item_command(out, 'b');
        }
        if (party_book_caster(&copy) >= 0) {
            native_item_command(out, 'm');
        }
    }
}

/*! 持ち物と装備の通し番号 `index` 番目（空きを除く）の行を作る。無ければ 0。 */
int tb_native_owned_item(int index, native_store_item *out)
{
    int slot, seen = 0;
    for (slot = 0; slot < INVEN_TOTAL; ++slot) {
        if (inventory[slot].k_idx && seen++ == index) {
            tb_native_describe_item(&inventory[slot], out);
            out->slot = slot;
            out->location = slot >= INVEN_RARM ? 1 : 0;
            out->key = index_to_label(slot);
            /* 装備中の品は wield（w）と破壊（k）の選択肢に載らない（幻想蛮怒と同じ）。 */
            if (out->location == 1) {
                native_item_command(out, 't');
                native_item_drop_command(out, 'w');
                native_item_drop_command(out, 'k');
            }
            return 1;
        }
    }
    return 0;
}

/*! 店に入っている間だけ、新しい画面の材料を出すための旗を立てて原作の `do_cmd_store()` を呼ぶ。 */
void do_cmd_store(void)
{
    tb_modern_store_active = 1;
    tb_original_do_cmd_store();
    tb_modern_store_active = 0;
}

/*! いま入っている店の見出し・種類・ページの大きさ。店に入っていなければ 0。 */
int tb_native_store_info(native_store_info *out)
{
    cptr store_name;
    cptr owner_name;
    byte feat;
    memset(out, 0, sizeof(*out));
    if (!tb_modern_store_active || !st_ptr || !ot_ptr) {
        return 0;
    }
    /* ダンジョンの中の我が家は品物の一覧を出さない（`display_store()`・`display_inventory()` の条件）。 */
    if (cur_store_num == STORE_HOME && old_town_num == NO_TOWN) {
        return 0;
    }
    out->active = 1;
    out->kind = cur_store_num == STORE_HOME ? 2 : (cur_store_num == STORE_MUSEUM ? 3 : 1);
    out->top = store_top;
    out->page_size = TB_STORE_PAGE;
    out->selection_style = 3;
    out->root_frame = tb_store_at_root ? 1 : 0;
    out->get_key = out->kind == 3 ? 'r' : (out->kind == 2 ? 'g' : 'p');
    out->put_key = out->kind == 1 ? 's' : 'd';
    out->previous_key = '-';
    out->next_key = ' ';
    /* 店の地形名。`display_store()` と同じ選び方（デネブの店は地形 FEAT_DENEB_SHOP）。 */
    if (cur_store_num == STORE_MUSEUM) {
        feat = FEAT_MUSEUM;
    }
    else if (p_ptr->town_num == NO_TOWN && cur_store_num == STORE_BLACK) {
        feat = FEAT_DENEB_SHOP;
    }
    else {
        feat = FEAT_SHOP_HEAD + cur_store_num;
    }
    store_name = f_name + f_info[feat].name;
    snprintf(out->title, sizeof(out->title), "%s", tb_tr(store_name));
    if (out->kind == 1) {
        /* 原作の `display_store()` と同じ並び：主人の名前、店名と主人の支払いの上限（`max_cost_fix()`）。 */
        owner_name = ow_name + ot_ptr->owner_name;
        snprintf(out->heading, sizeof(out->heading), "%s\n%s (%ld)", tb_tr(owner_name), tb_tr(store_name), (long)max_cost_fix());
    }
    return 1;
}

/*!
 * @brief 店の品物（`index` が在庫の数より小さい間）と、そのあとに続く持ち物（売る・置く物）の 1 行を作る。
 * @return 行があれば 1、尽きたら 0
 */
int tb_native_store_item(int index, native_store_item *out)
{
    object_type *obj;
    int slot, seen = 0, own;
    long price = 0;
    if (!tb_modern_store_active || !st_ptr || !ot_ptr) {
        return 0;
    }
    own = index >= st_ptr->stock_num;
    memset(out, 0, sizeof(*out));
    out->selection = -1;
    if (!own) {
        slot = index;
        obj = &st_ptr->stock[slot];
        out->tab = 0;
        out->slot = slot;
        out->position = slot;
        out->key = slot >= store_top && slot < store_top + TB_STORE_PAGE ? 'a' + slot - store_top : 0;
        if (cur_store_num != STORE_HOME && cur_store_num != STORE_MUSEUM) {
            /* `display_entry()` の値段の出し方と同じ（固定・税込み・値切り）。 */
            if (obj->ident & IDENT_FIXED) {
                price = price_item(obj, ot_ptr->min_inflate, FALSE);
            }
            else if (!manual_haggle) {
                price = price_item(obj, ot_ptr->min_inflate, FALSE);
                if (!noneedtobargain(price)) {
                    price += price / 10;
                }
            }
            else {
                price = price_item(obj, ot_ptr->max_inflate, FALSE);
            }
            out->price_known = 1;
        }
    }
    else {
        index -= st_ptr->stock_num;
        obj = NULL;
        for (slot = 0; slot < INVEN_TOTAL; ++slot) {
            if (inventory[slot].k_idx && seen++ == index) {
                obj = &inventory[slot];
                break;
            }
        }
        if (!obj) {
            return 0;
        }
        out->tab = 1;
        out->slot = slot;
        out->position = slot;
        out->location = slot >= INVEN_RARM ? 1 : 0;
        out->key = index_to_label(slot);
        if (cur_store_num != STORE_HOME && cur_store_num != STORE_MUSEUM && object_is_known(obj)) {
            price = price_item(obj, ot_ptr->min_inflate, TRUE);
            out->price_known = 1;
        }
    }
    out->count = obj->number;
    out->price = (int)price;
    out->available = own ? store_will_buy(obj) : 1;
    /* 店の商品は IDENT_STORE でも種類の板にする（幻想蛮怒・Frox と同じ。契約 §2-8）。 */
    out->kind_id = (object_is_aware(obj) || (obj->ident & IDENT_STORE)) ? obj->k_idx : 0;
    out->unaware_category = tb_unaware_category(obj);
    out->glyph[0] = object_char(obj);
    out->glyph_color = object_attr(obj);
    object_desc(out->name, obj, 0);
#ifdef JP
    snprintf(out->weight, sizeof(out->weight), "%d.%d kg", lbtokg1(obj->weight), lbtokg2(obj->weight));
#else
    snprintf(out->weight, sizeof(out->weight), "%d.%d lb", obj->weight / 10, obj->weight % 10);
#endif
    snprintf(out->description, sizeof(out->description), "%s", out->name);
    if (object_is_known(obj) && k_info[obj->k_idx].text) {
        snprintf(out->description, sizeof(out->description), "%s\n%s", out->name, tb_tr(k_text + k_info[obj->k_idx].text));
    }
    return 1;
}

/*!
 * @brief 検査用の入口（環境変数 `HD2D_TEST_PARTY_QUESTS=1`）。仲間を加える報酬の条件
 * （アルモリカ城とバルマムッサの虐殺のクエストの達成）を、検査の遊びの中だけで達成済みにする。
 * 自宅の `a) 仲間を加える` を実際に押せるようにするためで、加入そのものは原作の処理がやる。
 * コピーしたセーブと検査用のデータの置き場でだけ使う（`tb_native_test_open` と同じ）。
 */
void tb_native_test_party_quests(void)
{
    quest[QUEST_ARMORICA].status = QUEST_STATUS_COMPLETED;
    quest[QUEST_BARMAMUTHA_L].status = QUEST_STATUS_COMPLETED;
}

/*!
 * @brief 検査用の入口（環境変数 `HD2D_TEST_PARTY_CALL=resurrect|necromancy|reincarnate`）。
 * 蘇生・転生の呪文を覚えた職を育てずに、その呪文が出す仲間の選択のメニューを開く。
 * 呪文の処理（`party_resurrect()` ほか）は原作のものをそのまま呼ぶだけで、呪文の消費は伴わない。
 * 呼べる条件（蘇生・死霊術は倒れた仲間が、転生はスケルトンかゴーストの仲間が居る）が整ったときだけ呼ぶ。
 * @param kind 1＝蘇生、2＝転生、3＝死霊術（ネクロマンシー）
 * @return 呼んだら 1、条件がまだ整っていなければ 0
 */
int tb_native_test_party_call(int kind)
{
    /* 命令の入力待ちの中で呼ぶので、メニューの `inkey()` が下げた「命令待ち」の旗を戻す（呪文の中から呼ばれる本来の流れでは起きない）。 */
    const bool waiting = inkey_flag;
    int called = 0;
    switch (kind) {
    case 1:
        if (party_has_dead_member()) {
            fprintf(stderr, "[native-menu-test] party call resurrect\n");
            (void)party_resurrect();
            called = 1;
        }
        break;
    case 2:
        if (party_has_reincarnation_target()) {
            fprintf(stderr, "[native-menu-test] party call reincarnate\n");
            (void)party_reincarnate();
            called = 1;
        }
        break;
    case 3:
        if (party_has_dead_member()) {
            fprintf(stderr, "[native-menu-test] party call necromancy\n");
            (void)party_necromancy();
            called = 1;
        }
        break;
    default:
        break;
    }
    if (called) {
        inkey_flag = waiting;
    }
    return called;
}

/*!
 * @brief 検査用の入口（環境変数 `HD2D_TEST_SCREEN_CALL=tele_town|magical_weapon`）。
 * 原作が自前のキー入力で読む画面（町の選択・マジカルウェポンの選択）を、その呪文や建物を育てずに開く。
 * 画面を出す原作の関数をそのまま呼ぶだけ（`tele_town()`・`choose_magical_weapon()`）。町の選択では、行ける町を出すため
 * 全部の町を訪問済みにする（`p_ptr->visit` を書く。コピーしたセーブと検査用のデータの置き場でだけ使う）。
 * @param kind 1＝町の選択（`tele_town(TRUE)`）、2＝マジカルウェポン（利き腕に武器があるときだけ）
 * @return 呼んだら 1、条件がまだ整っていなければ 0
 */
int tb_native_test_screen_call(int kind)
{
    /* 命令の入力待ちの中で呼ぶので、メニューの `inkey()` が下げた「命令待ち」の旗を戻す。 */
    const bool waiting = inkey_flag;
    int called = 0;
    switch (kind) {
    case 1:
        p_ptr->visit = (u32b)0x7FFFFFFF;
        fprintf(stderr, "[native-menu-test] screen call tele_town\n");
        (void)tele_town(TRUE);
        called = 1;
        break;
    case 2:
        if (buki_motteruka(INVEN_RARM)) {
            fprintf(stderr, "[native-menu-test] screen call magical_weapon\n");
            (void)choose_magical_weapon();
            called = 1;
        }
        break;
    default:
        break;
    }
    if (called) {
        inkey_flag = waiting;
    }
    return called;
}

/*!
 * @brief 検査用の入口（環境変数 `HD2D_TEST_STORE` で商店を開く）。コピーしたセーブと検査用のデータの置き場でだけ使う。
 * @param kind 0..8 は店の番号（`STORE_*`）、-2 は我が家、-3 は博物館、-4 はデネブの店、
 *             -5 は仲間を 1 人加えてから我が家、-6 はガンナーのクエストを達成済みにして我が家（下の注記）、
 *             100..131 は建物の番号（100 + `building[]` の添字。その町の定義にある建物だけが開く。下の注記）、
 *             1000 + 町の番号 × 100 + 建物の番号 は、その町の定義を読み込んでから建物を開く
 * @return 開けたら 1
 */
int tb_native_test_open(int kind)
{
    cave_type *grid = &cave[py][px];
    byte old_feat, new_feat;
    /*
     * -5 は検査用：仲間を 1 人加えてから自宅を開く。雇用の資格（クエストの達成。`party_recruit_reward()`）を飛ばして、
     * 自宅の `a` の中身（`do_cmd_party_recruit()` → 仲間の誕生）と同じ関数を呼ぶだけ。誕生の画面の操作は呼び出し側が続ける。
     * -6 は検査用：ガンナーのクエスト（`QUEST_GUNNER`）を達成済みにして自宅を開く。クラスチェンジの一覧にガンナーが並ぶ
     * （`can_choose_class()`）。遊びの規則は変えない（セーブの値を 1 つ書くだけ）。
     */
    if (kind == -5) {
        party_capture();
        party_birth_member();
        kind = STORE_HOME;
    }
    else if (kind == -6) {
        quest[QUEST_GUNNER].status = QUEST_STATUS_FINISHED;
        kind = STORE_HOME;
    }
    if (kind == -2) {
        kind = STORE_HOME;
    }
    if (kind == -3) {
        kind = STORE_MUSEUM;
    }
    /*
     * 100..131 は検査用：足元を建物の入口にして `do_cmd_bldg()` を呼ぶ（建物の画面を、町を歩かずに開く）。
     * 町ごとの建物の定義（`lib/edit/t000000N.txt` の `B:` の行）にある番号だけが開く。遊びの規則は変えない（足元の地形を一時的に替えるだけ）。
     */
    /*
     * 1000 + 町の番号 × 100 + 建物の番号 は、その町の定義（`lib/edit/t000000N.txt` の `B:` の行）を読み込んでから建物を開く
     * （町を歩かずに、別の町の建物の画面を開く。町の地図の部分も書き換わるが、開いたあとは使わない）。
     */
    if (kind >= 1000) {
        char town_file[32];
        const int town_no = (kind - 1000) / 100;
        const int bldg_no = (kind - 1000) % 100;
        sprintf(town_file, "t%07d.txt", town_no);
        process_dungeon_file(town_file, 0, 0, MAX_HGT, MAX_WID);
        old_feat = grid->feat;
        grid->feat = (byte)(FEAT_BLDG_HEAD + bldg_no);
        do_cmd_bldg();
        grid->feat = old_feat;
        return 1;
    }
    if (kind >= 100 && kind <= 100 + (FEAT_BLDG_TAIL - FEAT_BLDG_HEAD)) {
        old_feat = grid->feat;
        grid->feat = (byte)(FEAT_BLDG_HEAD + (kind - 100));
        do_cmd_bldg();
        grid->feat = old_feat;
        return 1;
    }
    if (kind == -4) {
        new_feat = FEAT_DENEB_SHOP;
    }
    else if (kind == STORE_MUSEUM) {
        new_feat = FEAT_MUSEUM;
    }
    else if (kind >= 0 && kind <= STORE_BOOK) {
        new_feat = FEAT_SHOP_HEAD + kind;
    }
    else {
        return 0;
    }
    old_feat = grid->feat;
    grid->feat = new_feat;
    do_cmd_store();
    grid->feat = old_feat;
    return 1;
}
