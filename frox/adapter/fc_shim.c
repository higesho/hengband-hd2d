/*!
 * @file fc_shim.c
 * @brief 境界の実装のうち「コアの名札」と「ゲーム状態の読み口」を持つ TU（設計 §3）。
 *
 * ここは **Frox のヘッダを include してよい場所**である（`fc_null_term.c` /
 * `fc_bootstrap.c` も同族）。C++ からは `fc_shim.h` 越しにしか見えない。
 *
 * ## 文字コード（adapter/ 全体の約束）
 * このファイルは **UTF-8（BOM 付き）**。`frox/src/` の写しは **純 ASCII**。
 * vcxproj は `/source-charset` を足していないので、MSVC は既定で ACP（932）として読み、
 * **BOM のあるファイルだけ UTF-8 として読む**。だから 2 つが同居できる。
 * - `frox/src/` に charset 指定を足してはいけない。
 * - `frox/adapter/` から BOM を落としてはいけない（註釈が化ける）。
 *
 * **M0 が返す文字列はすべて ASCII** である。M1 でコアの中が CP932 になったとき、
 * UTF-8 化するのは C++ 側（`fc_text.h`）の仕事で、この境界では変換しない。
 */

#include "angband.h"
#include "menu.h" /* menu_t（原作の選択メニュー。`fc_menu_choose()` が読む） */
#include "bridge/native_item_display.h"

#include "fc_shim.h"
#include "fc_unaware_category.h"
#include "bridge/unaware_item_categories.h"

#include "fc_lang_c.h" /* HUD の場所名の訳（M1 J4。表示にしか行かない口） */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 版は `defines.h:19-22` の VER_* から組む。
 *
 *   #define VER_MAJOR 7
 *   #define VER_MINOR 3
 *   #define VER_PATCH "pipari"
 *   #define VER_EXTRA 2
 *
 * **`VER_PATCH` は文字列である。** だから `#` で文字列化してはいけない
 * （`"\"pipari\""` になる）。並べるだけで連結される。
 * 出来上がりは `src/Makefile.src:8` の `VERSION = 7.3.pipari.2` と一致する。
 * 数字の直書きはしない──写しを取り込み直したら自動で追随させるため。
 */
#define FC_STRINGIFY_(x) #x
#define FC_STRINGIFY(x) FC_STRINGIFY_(x)
#define FC_VERSION_STRING \
    FC_STRINGIFY(VER_MAJOR) "." FC_STRINGIFY(VER_MINOR) "." VER_PATCH "." FC_STRINGIFY(VER_EXTRA)

const char *fc_core_name(void)
{
    return "frox";
}

const char *fc_core_version(void)
{
    return FC_VERSION_STRING;
}

/* ==================================================================== 小さな道具 */

static void fc_copy_str(char *dst, int cap, cptr src)
{
    int n;

    if (!dst || (cap <= 0)) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    n = (int)strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
}

/* ================================================================== 状態の小口 */

int fc_character_generated(void)
{
    return character_generated ? 1 : 0;
}

int fc_rogue_like_commands(void)
{
    return rogue_like_commands ? 1 : 0;
}

int fc_screen_depth(void)
{
    return (int)character_icky;
}

int fc_screen_flags(void)
{
    int bits = 0;

    if (character_generated) {
        bits |= FC_SCREEN_GENERATED;
    }
    /*
     * **`character_icky` は計数器である。** 画面の退避が `++`、復帰が `--` する。
     * `== TRUE` で比べると入れ子の 2 段目を取り逃がす。非 0 だけを見る。
     */
    if (character_icky) {
        bits |= FC_SCREEN_ICKY;
    }
    if (character_xtra) {
        bits |= FC_SCREEN_XTRA;
    }
    if (character_dungeon) {
        bits |= FC_SCREEN_DUNGEON;
    }
    if (character_generated && p_ptr->is_dead) {
        bits |= FC_SCREEN_DEAD;
    }
    if (character_generated && p_ptr->playing) {
        bits |= FC_SCREEN_PLAYING;
    }
    return bits;
}

/* -------------------------------------- メッセージ行の置き場（FH-13。`fc_shim.h`） */

/*! `msg_on_startup()`（`init_angband()` の中）を通ったか。前に読むと落ちる。 */
static int fc_msg_ready = 0;

void fc_mark_msg_ready(void)
{
    fc_msg_ready = 1;
}

void fc_msg_rect_use_shop_for_test(int on)
{
    if (!fc_msg_ready) {
        return;
    }
    msg_line_init(on ? ui_shop_msg_rect() : ui_msg_rect());
}

/*!
 * @brief メッセージの箱に文が出ているか（修-33。`msg_line_is_empty()` の逆）。
 * @return 出ていれば 1。空・まだ準備前なら 0。
 */
int fc_msg_line_shown(void)
{
    if (!fc_msg_ready) {
        return 0;
    }
    return msg_line_is_empty() ? 0 : 1;
}

/*!
 * @brief メッセージの箱の文を消す（修-33。原作の `msg_line_clear()` をそのまま呼ぶ）。
 * @details 建物の命令の処理（`bldg_process_command()`）が始まる所で原作が毎回呼ぶ関数。クエストの説明文のように、
 * 命令のあとに箱へ出しっぱなしになる文を、画面側が「読み終えた」操作で消すために使う。
 */
void fc_msg_line_clear(void)
{
    if (!fc_msg_ready) {
        return;
    }
    msg_line_clear();
}

int fc_msg_rect(int *x, int *y, int *w, int *h)
{
    rect_t r;

    if (!fc_msg_ready) {
        return FC_ERR_STATE;
    }
    r = msg_line_rect();
    if (x) {
        *x = r.x;
    }
    if (y) {
        *y = r.y;
    }
    if (w) {
        *w = r.cx;
    }
    if (h) {
        *h = r.cy;
    }
    return FC_OK;
}

/* ------------------------------------------------ doc UI の旗（M0.5。設計 §14.2(a)） */

/*!
 * doc UI が開いているか。真偽 1 つで足りる——doc UI は入れ子でも save のたびに
 * 立て直すだけで、降ろすのは「コマンド待ちまで戻った」の 1 点だから。
 */
static int fc_doc_ui_open = 0;

/*!
 * @brief 6 TU の `Term_save()` はここへ来る（vcxproj の define。`fc_shim.h`）。
 */
errr fc_ui_term_save(void)
{
    fc_doc_ui_open = 1;
    return Term_save();
}

/*!
 * @brief 6 TU の `Term_load()` はここへ来る。**旗は降ろさない**——
 * 鍛冶（`weaponsmith.c`）は小画面から戻るたび load する（save 1 / load 10）。
 */
errr fc_ui_term_load(void)
{
    return Term_load();
}

/*!
 * @brief 品選びの窓が開いているか（フック #37）。
 * @details `fc_doc_ui_active()` の掛け金とは別に、`obj_prompt()` の入りと
 * 片づけでそのまま上げ下げする。入れ子にはならない（`obj_prompt` は再入しない）。
 */
static int fc_obj_prompt_open = 0;

void fc_obj_prompt_scope(int on)
{
    hb_ui_item_scope(on,0);
    fc_obj_prompt_open = (on != 0) ? 1 : 0;
}

int fc_obj_prompt_active(void)
{
    return fc_obj_prompt_open;
}

int fc_doc_ui_active(void)
{
    /*
     * `inkey_flag` はコマンド待ち（`request_command` → `inkey()`）でだけ TRUE
     * （`util.c:3731`。doc UI の中の待ちは全部 FALSE——設計 §14.1 の事実 3）。
     * そこまで戻った＝ doc UI はもう畳まれているので、ここで降ろす。
     * 店の中で開いた doc UI は店を出るまで降りない（店の inkey は旗を立てない）が、
     * 店の間は `character_icky` でどのみちミラーが開いているので見た目は変わらない。
     */
    if (fc_doc_ui_open && inkey_flag) {
        fc_doc_ui_open = 0;
    }
    return fc_doc_ui_open;
}

/* ------------------------------------------------ 選択メニュー `menu_choose()` の全件 */

/*!
 * 開いている選択メニューの全件。`fc_menu_choose()` が開く間だけ持つ。
 * `key` は原作の `_list()` が振るキーと同じ（`MENU_KEY`、無ければ `I2A(i)`。項目が 26 個以下なら小文字）。
 * 0 は選べない項目（原作は色が `TERM_DARK` の項目のキーを `'\0'` にして、行を描かない）。
 */
typedef struct {
    int   key;
    int   attr;
    char *text;
    char *help;
} fc_mc_item_t;

typedef struct {
    fc_mc_item_t *items;
    int           count;
} fc_mc_state_t;

static fc_mc_state_t fc_mc_cur = { NULL, 0 };

static char *fc_mc_dup(cptr s)
{
    size_t n = s ? strlen(s) : 0;
    char *p = (char *)malloc(n + 1);

    if (p) {
        if (n) {
            memcpy(p, s, n);
        }
        p[n] = '\0';
    }
    return p;
}

static void fc_mc_free(fc_mc_state_t *st)
{
    int i;

    if (st->items) {
        for (i = 0; i < st->count; i++) {
            free(st->items[i].text);
            free(st->items[i].help);
        }
        free(st->items);
    }
    st->items = NULL;
    st->count = 0;
}

/*!
 * @brief メニューの全件を、原作のコールバック（`menu->fn`）から取る。
 * @details 原作の `_list()`（`menu.c`）と同じ呼び方・同じ規則でキー・字・色を取る。字は `fc_spell_name()`
 * を通す（原作が `"  %c) %s"` で描く字と同じにするため）。説明は `MENU_HELP`（原作は `?` で閲覧に入って
 * 項目を選んだときだけ呼ぶ）を `fc_tr()` に通した字で、無ければ空。
 * @return 1 = 取れた / 0 = 項目が無い・メモリ不足
 */
static int fc_mc_collect(menu_ptr menu, fc_mc_state_t *out)
{
    int i;

    out->items = NULL;
    out->count = 0;
    if (!menu || !menu->fn || (menu->count <= 0)) {
        return 0;
    }
    out->items = (fc_mc_item_t *)calloc((size_t)menu->count, sizeof(fc_mc_item_t));
    if (!out->items) {
        return 0;
    }
    out->count = menu->count;
    for (i = 0; i < menu->count; i++) {
        variant key, text, color, help;
        int k;
        int attr = TERM_WHITE;

        var_init(&key);
        var_init(&text);
        var_init(&color);
        var_init(&help);

        menu->fn(MENU_KEY, i, menu->cookie, &key);
        k = var_is_null(&key) ? I2A(i) : (char)var_get_int(&key);
        if (menu->count <= 26) {
            k = tolower((unsigned char)k);
        }
        menu->fn(MENU_TEXT, i, menu->cookie, &text);
        menu->fn(MENU_COLOR, i, menu->cookie, &color);
        if (!var_is_null(&color)) {
            attr = var_get_int(&color);
        }
        if (attr == TERM_DARK) {
            k = 0;
        }
        out->items[i].key = k;
        out->items[i].attr = attr;
        out->items[i].text = fc_mc_dup(var_is_null(&text) ? "" : fc_spell_name(var_get_string(&text)));
        menu->fn(MENU_HELP, i, menu->cookie, &help);
        out->items[i].help = fc_mc_dup(var_is_null(&help) ? "" : fc_tr(var_get_string(&help)));

        var_clear(&key);
        var_clear(&text);
        var_clear(&color);
        var_clear(&help);
        if (!out->items[i].text || !out->items[i].help) {
            fc_mc_free(out);
            return 0;
        }
    }
    return 1;
}

/*!
 * @brief 原作の `menu_choose()` の差し替え先（組み立ての設定。`fc_shim.h`）。
 * @details 新しい UI が有効なときだけ、メニューの全件を先に取って置く（`fc_menu.cpp` の `menu_choose_apply()` が
 * 一覧を原作の Term の写しではなく全件から組む）。そのうえで**原作の `menu_choose()` をそのまま呼ぶ**。
 * 描画・キーの受け取り・`?` の閲覧・`C` の人物調書・繰り返しの記憶（`REPEAT_PULL`）・選んだ番号の返し方は
 * すべて原作のまま。新しい UI が無効なとき（Term の写しで遊ぶとき）は何も取らず、原作と同じに動く。
 */
extern int fc_menu_choose(menu_ptr menu);

int fc_menu_choose(menu_ptr menu)
{
    fc_mc_state_t saved = fc_mc_cur;
    int choice;
    int opened = 0;

    /*
     * 副 Term（サブパネル）の再描画の最中は、メニューを開かない（2026-10-15）。`window_stuff()` → `fix_spell()` は
     * `Term_activate(angband_term[j])`（j >= 1）の間に `get_spell_table()` を呼び、ワイルド・タレントなどの
     * `get_spells`（`wild_talent_get_spells()` ほか）は系統を選ぶ `menu_choose()` を開く。これを通すと、サブパネルの
     * 種類に魔法の一覧（`PW_SPELL`）がある既定の配置で、誕生の最後（能力値の確定の直後）にキーを待ったまま進まなくなる。
     * 遊ぶ人の命令が開くメニューは本線 Term（`angband_term[0]`）で開くので、そちらは変えない。-1 は原作の取消と同じ返り値で、
     * 呼び出し側は 0 件として扱う（サブパネルは見出しだけになる）。
     */
    if (menu && Term && (Term != angband_term[0])) {
        return -1;
    }
    fc_mc_cur.items = NULL;
    fc_mc_cur.count = 0;
    if (menu && fc_modern_ui_enabled()) {
        opened = fc_mc_collect(menu, &fc_mc_cur);
    }
    choice = menu_choose(menu);
    if (opened) {
        fc_mc_free(&fc_mc_cur);
    }
    fc_mc_cur = saved;
    return choice;
}

int fc_menu_choose_count(void)
{
    return fc_mc_cur.count;
}

int fc_menu_choose_item(int index, int *key, int *attr, char *text, int text_cap, char *help, int help_cap)
{
    if ((index < 0) || (index >= fc_mc_cur.count) || !fc_mc_cur.items) {
        return 0;
    }
    if (key) {
        *key = fc_mc_cur.items[index].key;
    }
    if (attr) {
        *attr = fc_mc_cur.items[index].attr;
    }
    fc_copy_str(text, text_cap, fc_mc_cur.items[index].text);
    fc_copy_str(help, help_cap, fc_mc_cur.items[index].help);
    return 1;
}

/* 検査用のメニュー。キーは MENU_KEY で振る（`a` `b` …）が、3 番目は色が TERM_DARK で選べない。 */
static void fc_mc_test_fn(int cmd, int which, vptr cookie, variant *res)
{
    static const char names[][8] = { "Alpha", "Beta", "Gamma", "Delta", "Eps" };

    (void)cookie;
    switch (cmd) {
    case MENU_KEY:
        if (which == 4) {
            var_set_int(res, 'Z');
        }
        break;
    case MENU_TEXT:
        var_set_string(res, names[which % 5]);
        break;
    case MENU_COLOR:
        if (which == 2) {
            var_set_int(res, TERM_DARK);
        }
        break;
    case MENU_HELP:
        if (which == 1) {
            var_set_string(res, "Second item.");
        }
        break;
    default:
        break;
    }
}

/* 検査用のメニュー（27 件）。26 件を超えるので、キーは大文字小文字を区別し、`I2A(i)` のまま。 */
static void fc_mc_test_big_fn(int cmd, int which, vptr cookie, variant *res)
{
    (void)cookie;
    if (cmd == MENU_TEXT) {
        var_set_string(res, format("Item %d", which));
    }
}

int fc_menu_choose_selftest(void)
{
    int failed = 0;
    fc_mc_state_t st;
    menu_t menu = { "Choose", NULL, NULL, fc_mc_test_fn, NULL, 5, 0, FALSE };
    menu_t big = { "Choose", NULL, NULL, fc_mc_test_big_fn, NULL, 27, 0, FALSE };

#define FC_MC_EXPECT(COND, WHAT) \
    do { \
        if (!(COND)) { \
            ++failed; \
            fprintf(stderr, "[frox] selftest: menu_choose: FAIL - %s\n", (WHAT)); \
        } \
    } while (0)

    /* 5 件（26 個以下）: 既定のキーは小文字の a b c d。MENU_KEY の `Z` も小文字へ。TERM_DARK の項目はキー 0。 */
    FC_MC_EXPECT(fc_mc_collect(&menu, &st) == 1, "collect: 5 items");
    if (st.items) {
        FC_MC_EXPECT(st.count == 5, "collect: the count is 5");
        FC_MC_EXPECT(st.items[0].key == 'a', "collect: the first key is a");
        FC_MC_EXPECT(st.items[1].key == 'b', "collect: the second key is b");
        FC_MC_EXPECT(st.items[2].key == 0, "collect: a TERM_DARK item has no key");
        FC_MC_EXPECT(st.items[3].key == 'd', "collect: the fourth key is d");
        FC_MC_EXPECT(st.items[4].key == 'z', "collect: MENU_KEY is lowered for 26 items or fewer");
        FC_MC_EXPECT(st.items[0].text && (strlen(st.items[0].text) > 0), "collect: the first text is empty");
        FC_MC_EXPECT(st.items[1].help && (strstr(st.items[1].help, "Second") != NULL), "collect: the help of the second item");
        FC_MC_EXPECT(st.items[0].help && (st.items[0].help[0] == '\0'), "collect: no help is an empty string");
    }
    fc_mc_free(&st);
    FC_MC_EXPECT(st.items == NULL && st.count == 0, "free: the state is cleared");

    /* 27 件（26 個を超える）: キーは I2A(i) のまま（大文字小文字を区別するので小文字にしない）。 */
    FC_MC_EXPECT(fc_mc_collect(&big, &st) == 1, "collect: 27 items");
    if (st.items) {
        FC_MC_EXPECT(st.count == 27, "collect: the count is 27");
        FC_MC_EXPECT(st.items[25].key == I2A(25), "collect: the 26th key is z");
        FC_MC_EXPECT(st.items[26].key == I2A(26), "collect: the 27th key follows I2A");
    }
    fc_mc_free(&st);

    /* 空・引数なし */
    big.count = 0;
    FC_MC_EXPECT(fc_mc_collect(&big, &st) == 0, "collect: an empty menu");
    FC_MC_EXPECT(fc_mc_collect(NULL, &st) == 0, "collect: a null menu");

    /* 副 Term（サブパネル）が有効な間は、メニューを開かず -1（取消）を返す。開くとキーを待って戻らない（誕生の最後で止まる）。 */
    {
        term  sub;
        term *saved = Term;
        int   choice;

        memset(&sub, 0, sizeof(sub));
        Term = &sub;
        choice = fc_menu_choose(&menu);
        Term = saved;
        FC_MC_EXPECT(choice == -1, "sub term: fc_menu_choose does not open a menu while a sub term is active");
    }
#undef FC_MC_EXPECT
    return failed;
}

int fc_read_limits(int *out, int cap)
{
    if (!out || (cap < 6)) {
        return FC_ERR_ARG;
    }
    out[0] = (int)max_r_idx;
    out[1] = (int)max_k_idx;
    out[2] = (int)max_f_idx;
    out[3] = (int)max_a_idx;
    out[4] = (int)max_e_idx;
    out[5] = (int)max_d_idx;
    return 6;
}

/* ================================================================== HUD の読み口 */

/*!
 * @brief いまの場所の名。`prt_depth()`（`xtra1.c:1620`）の分岐を写す。
 *
 * @details 幻想蛮怒版と違うのは町の引き方だけ——Frox に `town[]` の配列は無く、
 * `town_name(int)`（`shop.h:91`）が名を返す。
 */
static void fc_place_name(char *out, int cap)
{
    if (!out || (cap <= 0)) {
        return;
    }
    out[0] = '\0';

    if (!character_generated) {
        return;
    }

    /*
     * どの枝も **カタログを通してから** 写す（M1 J4。承認済みの町名など）。
     * ここはプロトコルへ運ぶだけの表示専用の口なので、裸の 1 語でも
     * `fc_tr` の丸ごと引きで安全に訳せる（設計 §4 追記 (d) と同じ理屈）。
     * 日本語層が寝ていれば `fc_tr` は原文をそのまま返す。
     */
    if (dun_level && d_info && d_name && (dungeon_type < max_d_idx)) {
        fc_copy_str(out, cap, fc_tr(d_name + d_info[dungeon_type].name));
        return;
    }

    if (p_ptr->inside_arena) {
        fc_copy_str(out, cap, fc_tr("Gladiator Ring"));
        return;
    }
    if (p_ptr->inside_battle) {
        fc_copy_str(out, cap, fc_tr("Monster Pit"));
        return;
    }
    if (p_ptr->town_num > 0) {
        fc_copy_str(out, cap, fc_tr(town_name(p_ptr->town_num)));
        return;
    }

    fc_copy_str(out, cap, fc_tr("Wilderness"));
}

/*! @brief 深さの文言。`prt_depth()` と同じ言い回しにする（**M0 は英語**）。 */
static void fc_depth_text(char *out, int cap)
{
    char buf[32];

    if (!out || (cap <= 0)) {
        return;
    }
    out[0] = '\0';

    if (!character_generated) {
        return;
    }

    if (!dun_level) {
        strcpy(buf, "Surface");
    } else if (quests_get_current() && !dungeon_type) {
        strcpy(buf, "Quest");
    } else if (depth_in_feet) {
        sprintf(buf, "%d ft", (int)dun_level * 50);
    } else {
        sprintf(buf, "L%d", (int)dun_level);
    }

    /* 場所名と同じく、表示専用の口なのでカタログを通す（Surface / Quest）。 */
    fc_copy_str(out, cap, fc_tr(buf));
}

int fc_read_hud(fc_hud_data *out)
{
    if (!out) {
        return FC_ERR_ARG;
    }

    memset(out, 0, sizeof(*out));

    fc_copy_str(out->name, (int)sizeof(out->name), player_name);
    out->in_game = character_generated ? 1 : 0;

    /*
     * `p_ptr` は静的な実体なので常に読める。ただし `character_generated` が偽の
     * あいだは中身が既定値のままで、誕生画面の途中経過を意味しない。
     * **数値を出すのはゲームが始まってから**にする。
     */
    if (!character_generated) {
        return FC_OK;
    }

    out->hp = (int)p_ptr->chp;
    out->hp_max = (int)p_ptr->mhp;
    out->sp = (int)p_ptr->csp;
    out->sp_max = (int)p_ptr->msp;
    out->level = (int)p_ptr->lev;
    out->depth = (int)dun_level;
    out->gold = (long)p_ptr->au;

    fc_place_name(out->place, (int)sizeof(out->place));
    fc_depth_text(out->depth_text, (int)sizeof(out->depth_text));

    return FC_OK;
}

/* ============================================================= メッセージの読み口 */

int fc_message_count(void)
{
    return msg_count();
}

int fc_message_display_text(int age, char *out, int cap)
{
    msg_ptr message;
    cptr pos;
    doc_token_t token;
    int ct = 0;
    int i;

    if (!out || (cap <= 0)) {
        return FC_ERR_ARG;
    }
    out[0] = '\0';
    if ((age < 0) || (age >= msg_count())) {
        return 0;
    }
    message = msg_get(age);
    /*
     * メッセージ履歴（`do_cmd_messages()`）は `doc_insert_text()` で記録の字を文書へ流し、
     * 文書へ入るとき `fc_tr()`（`z-doc.c` の `doc_insert()`。フック #12）が訳す。
     * 記録（`msg_get()`）は訳す前の字（読み込みの途中で足されたメッセージなど）のことがあるので、
     * 画面と同じ字にするには同じ訳を通す。色の印は `msg_get_plain_text()` と同じに落とす。
     */
    pos = fc_tr(string_buffer(message->msg));
    for (;;) {
        pos = doc_lex(pos, &token);
        if (token.type == DOC_TOKEN_EOF) break;
        if (token.type == DOC_TOKEN_TAG) continue;
        for (i = 0; i < token.size; i++) {
            if (ct >= cap - 1) break;
            out[ct++] = token.pos[i];
        }
        if (ct >= cap - 1) break;
    }
    out[ct] = '\0';
    for (i = 0; out[i]; i++) {
        if ((out[i] == '\n') || (out[i] == '\r') || (out[i] == '\t')) out[i] = ' ';
    }
    return ct;
}

int fc_message_repeat(int age)
{
    msg_ptr message;

    if ((age < 0) || (age >= msg_count())) {
        return 0;
    }
    message = msg_get(age);
    return message ? message->count : 0;
}

int fc_message_text(int age, char *out, int cap)
{
    if (!out || (cap <= 0)) {
        return FC_ERR_ARG;
    }

    out[0] = '\0';
    if (age < 0) {
        return 0;
    }

    /*
     * **Frox のメッセージはドキュメント片である**（`message.c`。色つきの
     * `doc` を持つ）。素の字が欲しいので、あちらが用意している
     * `msg_get_plain_text()`（`message.h:31`）を通す。範囲外は 0 を返す。
     */
    if (msg_get_plain_text(age, out, cap) <= 0) {
        out[0] = '\0';
        return 0;
    }
    /*
     * **改行を空白へ倒す。** Frox のメッセージはドキュメント片で、中に生の
     * 改行を含むものがある（誕生直後の歓迎文など）。1 行の器（`MessageEvent`）へ
     * そのまま載せると、画面側で豆腐（描けない字の箱）になる——実機の絵で見た。
     */
    {
        int i;
        for (i = 0; out[i]; i++) {
            if ((out[i] == '\n') || (out[i] == '\r') || (out[i] == '\t')) {
                out[i] = ' ';
            }
        }
    }
    return (int)strlen(out);
}

/* =================================================================== 地図の読み口 */

int fc_floor_size(int *width, int *height)
{
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return FC_ERR_STATE;
    }
    if (width) {
        *width = (int)cur_wid;
    }
    if (height) {
        *height = (int)cur_hgt;
    }
    return FC_OK;
}

int fc_player_kind(int *pclass, int *prace)
{
    if (!character_generated) {
        return FC_ERR_STATE;
    }
    if (pclass) {
        *pclass = (int)p_ptr->pclass;
    }
    if (prace) {
        *prace = (int)p_ptr->prace;
    }
    return FC_OK;
}

int fc_player_pos(int *x, int *y)
{
    if (!character_generated || !character_dungeon) {
        return FC_ERR_STATE;
    }
    if (x) {
        *x = px;
    }
    if (y) {
        *y = py;
    }
    return FC_OK;
}

/*!
 * @brief 位置の確認（`L`。原作の `do_cmd_locate()`）の最中なら、見ている区画の中心を返す（修-64）。
 *
 * @details 原作はこの間 `viewport_origin`（見ている区画の左上）だけを `viewport_scroll()` で動かし、
 * `px`/`py`（自キャラ）は動かさない。行 0 は `Map sector [..], which is.. your sector. Direction?`
 * （日本語は `マップ位置 [..]..  方向?`）を出して方向を待つ。この目印で見分ける（原作は 1 バイトも変えない）。
 * 目印は `%` より前（`Map sector [`／`マップ位置 [`）を `fc_tr_fmt()` で引いた訳で比べる
 * （`fc_options_title_is_frox_page()` と同じ手。訳した字の符号を直書きしない）。
 * 区画の大きさは `do_cmd_locate()` と同じ `ui_map_rect()` で取る。
 * @return 最中なら 1（`*cx`/`*cy` に区画の中心のマス）。そうでなければ 0。
 */
int fc_locate_panel_center(int *cx, int *cy)
{
    static const char en_fmt[] = "Map sector [%d(%02d),%d(%02d)], which is%s your sector. Direction?";
    char row0[256];
    const char *fmt;
    const char *percent;
    size_t prefix;
    rect_t map_rect;

    if (!character_generated || !character_dungeon) {
        return 0;
    }
    if (fc_term_row(0, row0, NULL, (int)sizeof(row0)) <= 0) {
        return 0;
    }
    fmt = fc_tr_fmt(en_fmt);
    if (!fmt) {
        fmt = en_fmt;
    }
    percent = strchr(fmt, '%');
    prefix = percent ? (size_t)(percent - fmt) : strlen(fmt);
    if ((prefix == 0) || (strncmp(row0, fmt, prefix) != 0)) {
        /* 訳が無い（英語のまま）ときのため、英語の目印も見る。 */
        if (strncmp(row0, "Map sector [", 12) != 0) {
            return 0;
        }
    }
    map_rect = ui_map_rect();
    if (cx) {
        *cx = (int)viewport_origin.x + (map_rect.cx / 2);
    }
    if (cy) {
        *cy = (int)viewport_origin.y + (map_rect.cy / 2);
    }
    return 1;
}

/*!
 * @brief Term のカーソルが指している地図のマス（修-32）。
 * @details 移動の目標のマスを選ぶ問い（`tgt_pt()`）は、十字キーで `x`/`y` を動かすたびに `move_cursor_relative(y, x)` を呼ぶ
 * だけで、地図へ何も描かない（Sil-Q の `hilite_target` と同じ）。画面側へ位置を運ぶため、カーソルの桁と行を
 * `ui_pt_to_cave_pt()` でマスへ戻す。原作は 1 バイトも変えない。
 */
int fc_term_cursor_cave_cell(int *gx, int *gy)
{
    int cx = 0;
    int cy = 0;
    point_t cell;
    rect_t map_rect;

    if (!character_generated || !character_dungeon) {
        return 0;
    }
    if (fc_term_cursor(&cx, &cy, NULL) != FC_OK) {
        return 0;
    }
    map_rect = ui_map_rect();
    if (!rect_contains_pt(map_rect, cx, cy)) {
        return 0;
    }
    cell = ui_pt_to_cave_pt(point(cx, cy));
    if (!in_bounds2(cell.y, cell.x)) {
        return 0;
    }
    if (gx) {
        *gx = cell.x;
    }
    if (gy) {
        *gy = cell.y;
    }
    return 1;
}

/*!
 * @brief そのマスの地形が「見えている」か。**`map_info()` と同じ判定にする**。
 *
 * @details 判定を独自に作ると、コアが画面に描いているものと HD2D の立体が食い違う
 * （記号は伏せたまま形だけ漏れる、あるいはその逆）。`cave.c:970` 以降の分岐を写す:
 * - `FF_REMEMBER` を持つ地形（壁・扉・階段）… `CAVE_MARK` だけ。盲目でも消えない
 * - それ以外（床）… 盲目なら見えない。`CAVE_MARK|CAVE_LITE|CAVE_MNLT` か、
 *   視界内かつ（自照かつ消灯でない、または暗視）
 */
static int fc_grid_known(cave_type *c_ptr, s16b feat)
{
    feature_type *f_ptr = &f_info[feat];

    if (have_flag(f_ptr->flags, FF_REMEMBER)) {
        return (c_ptr->info & CAVE_MARK) ? 1 : 0;
    }

    if (p_ptr->blind) {
        return 0;
    }
    if (c_ptr->info & (CAVE_MARK | CAVE_LITE | CAVE_MNLT)) {
        return 1;
    }
    if ((c_ptr->info & CAVE_VIEW)
        && (((c_ptr->info & (CAVE_GLOW | CAVE_MNDK)) == CAVE_GLOW) || p_ptr->see_nocto)) {
        return 1;
    }
    return 0;
}

/*! @brief いま照明が当たっているか。 */
static int fc_grid_lit_now(cave_type *c_ptr)
{
    if (c_ptr->info & CAVE_MNDK) {
        return 0;
    }
    if (c_ptr->info & (CAVE_LITE | CAVE_MNLT)) {
        return 1;
    }
    return ((c_ptr->info & (CAVE_GLOW | CAVE_VIEW)) == (CAVE_GLOW | CAVE_VIEW)) ? 1 : 0;
}

/*!
 * @brief 光源の物から光の種類を決める（`LightingState::light_kind`）。
 * @param o_ptr 装備の光源（`TV_LITE`）。無ければ NULL
 * @param cur_lite 光の半径（`p_ptr->cur_lite`）
 * @return 0 = 光が無い、1 = 松明、2 = ランタン、3 = アーティファクトの光源、4 = 装備によらない光
 * @details 原作の `_calc_torch_imp()` と同じ見方をする。松明・ランタンは燃料切れなら半径に足されないので、
 * 半径が残っていれば装備によらない光（4）。暗黒の光源と全てを見る目（半径に足されない）も 4。
 * アーティファクトの判定は原作と同じ `name1 || art_name || name3`。読むだけでコアの状態は変えない。
 */
static int fc_light_kind_of(object_type *o_ptr, int cur_lite)
{
    u32b flgs[OF_ARRAY_SIZE];

    if (cur_lite <= 0) {
        return 0;
    }
    if (!o_ptr || !o_ptr->k_idx || (o_ptr->tval != TV_LITE)) {
        return 4;
    }
    if (o_ptr->name1 == ART_ALL_SEEING_EYE) {
        return 4;
    }
    obj_flags(o_ptr, flgs);
    if (have_flag(flgs, OF_DARKNESS)) {
        return 4;
    }
    if (o_ptr->name1 || o_ptr->art_name || o_ptr->name3) {
        return 3;
    }
    if (o_ptr->sval == SV_LITE_TORCH) {
        return (o_ptr->xtra4 > 0) ? 1 : 4;
    }
    if (o_ptr->sval == SV_LITE_LANTERN) {
        return (o_ptr->xtra4 > 0) ? 2 : 4;
    }
    if (o_ptr->sval == SV_LITE_FEANOR) {
        return 2;
    }
    return 3;
}

/*! @brief いまのプレイヤの光源の種類。装備の最初の `TV_LITE` を見る（複数あっても半径は足し算なので代表で足りる）。 */
static int fc_player_light_kind(void)
{
    object_type *o_ptr = NULL;
    const slot_t slot = equip_find_obj(TV_LITE, SV_ANY);

    if (slot) {
        o_ptr = equip_obj(slot);
    }
    return fc_light_kind_of(o_ptr, (int)p_ptr->cur_lite);
}

/*! @brief `fc_light_selftest` の 1 件。作った物（`tval`/`sval`/`name1`/`name2`/`name3`/`art_name`/`xtra4`）の判定が `want` か。 */
static void fc_light_case(const char *name, int tval, int sval, int name1, int name2, int name3, int art_name,
    int xtra4, int cur_lite, int want, char *err, int cap, int *fails)
{
    object_type forge;
    s16b k_idx = lookup_kind(tval, sval);
    int got;

    if (!k_idx) {
        got = -1;
    } else {
        object_prep(&forge, k_idx);
        forge.name1 = (s16b)name1;
        forge.name2 = (s16b)name2;
        forge.name3 = (s16b)name3;
        forge.art_name = (u16b)art_name;
        forge.xtra4 = (s16b)xtra4;
        got = fc_light_kind_of(&forge, cur_lite);
    }
    if (got != want) {
        if (*fails == 0 && err && cap > 1) {
            sprintf(err, "%.60s (want %d, got %d)", name, want, got);
            err[cap - 1] = '\0';
        }
        (*fails)++;
    }
}

int fc_light_selftest(char *err, int cap)
{
    int fails = 0;
    object_type empty;

    if (err && cap > 0) {
        err[0] = '\0';
    }

    /* 光が無い */
    fc_light_case("torch, radius 0", TV_LITE, SV_LITE_TORCH, 0, 0, 0, 0, 1000, 0, 0, err, cap, &fails);
    if (fc_light_kind_of(NULL, 0) != 0) {
        fails++;
    }
    /* 光源が無い・光源でない物でも半径が残っていれば装備によらない光 */
    if (fc_light_kind_of(NULL, 1) != 4) {
        if (fails == 0 && err && cap > 1) {
            sprintf(err, "no light source (want 4)");
        }
        fails++;
    }
    memset(&empty, 0, sizeof(empty));
    if (fc_light_kind_of(&empty, 2) != 4) {
        if (fails == 0 && err && cap > 1) {
            sprintf(err, "k_idx 0 (want 4)");
        }
        fails++;
    }
    /* 松明・ランタン（燃料の有無） */
    fc_light_case("torch with fuel", TV_LITE, SV_LITE_TORCH, 0, 0, 0, 0, 1000, 1, 1, err, cap, &fails);
    fc_light_case("torch without fuel", TV_LITE, SV_LITE_TORCH, 0, 0, 0, 0, 0, 1, 4, err, cap, &fails);
    fc_light_case("lantern with fuel", TV_LITE, SV_LITE_LANTERN, 0, 0, 0, 0, 1000, 2, 2, err, cap, &fails);
    fc_light_case("lantern without fuel", TV_LITE, SV_LITE_LANTERN, 0, 0, 0, 0, 0, 2, 4, err, cap, &fails);
    fc_light_case("feanorian lamp", TV_LITE, SV_LITE_FEANOR, 0, 0, 0, 0, 0, 2, 2, err, cap, &fails);
    /* アーティファクト（固定・ランダム・固定の置き換え） */
    fc_light_case("fixed artifact (Phial)", TV_LITE, SV_LITE_GALADRIEL, ART_GALADRIEL, 0, 0, 0, 0, 3, 3, err, cap, &fails);
    fc_light_case("random artifact", TV_LITE, SV_LITE_TORCH, 0, 0, 0, 1, 1000, 3, 3, err, cap, &fails);
    fc_light_case("replaced fixed artifact", TV_LITE, SV_LITE_LANTERN, 0, 0, 1, 0, 1000, 3, 3, err, cap, &fails);
    fc_light_case("other light (Star)", TV_LITE, SV_LITE_ARKENSTONE, 0, 0, 0, 0, 0, 3, 3, err, cap, &fails);
    /* 暗黒の光源・全てを見る目は半径に足されない */
    fc_light_case("darkness torch", TV_LITE, SV_LITE_TORCH, 0, EGO_LITE_DARKNESS, 0, 0, 1000, 1, 4, err, cap, &fails);
    fc_light_case("all-seeing eye", TV_LITE, SV_LITE_EYE, ART_ALL_SEEING_EYE, 0, 0, 0, 0, 1, 4, err, cap, &fails);
    return fails;
}

/*! @brief 地形フラグ → `FC_FEAT_*`。`KNOWN` / `PLAYER` は呼び出し側。 */
static unsigned int fc_feature_bits(s16b feat, cave_type *c_ptr)
{
    feature_type *f_ptr = &f_info[feat];
    unsigned int bits = 0;

    if (have_flag(f_ptr->flags, FF_WALL)) {
        bits |= FC_FEAT_WALL;
    }
    if (have_flag(f_ptr->flags, FF_DOOR)) {
        bits |= FC_FEAT_DOOR;
        /*
         * 開いているか。**`FF_CLOSE`（＝「閉じる」の対象＝いま開いている）で見る。**
         * `FF_OPEN` は「開ける」の対象＝まだ閉じている側なので、意味が逆になる。
         */
        if (have_flag(f_ptr->flags, FF_CLOSE)) {
            bits |= FC_FEAT_DOOR_OPEN;
        }
    }
    /*
     * 瓦礫。`FF_HURT_ROCK`（岩石溶解の対象）は壁・鉱脈・隠し扉にも付くので、
     * **壁でも扉でもない HURT_ROCK** を瓦礫と読む。
     */
    if (have_flag(f_ptr->flags, FF_HURT_ROCK) && !(bits & (FC_FEAT_WALL | FC_FEAT_DOOR))) {
        bits |= FC_FEAT_RUBBLE;
    }
    /* 階段のほか、ダンジョンの口とクエストの入口も「降りる場所」として同じ扱いにする。 */
    if (have_flag(f_ptr->flags, FF_STAIRS) || have_flag(f_ptr->flags, FF_ENTRANCE)
        || have_flag(f_ptr->flags, FF_QUEST_ENTER)) {
        bits |= FC_FEAT_STAIRS;
    }
    if (have_flag(f_ptr->flags, FF_SHAFT)) {
        bits |= FC_FEAT_SHAFT;
    }
    if (have_flag(f_ptr->flags, FF_PERMANENT)) {
        bits |= FC_FEAT_PERMANENT;
    }
    if (have_flag(f_ptr->flags, FF_TREE)) {
        bits |= FC_FEAT_TREE;
    }
    if (have_flag(f_ptr->flags, FF_WATER)) {
        bits |= FC_FEAT_WATER;
    }
    if (have_flag(f_ptr->flags, FF_LAVA)) {
        bits |= FC_FEAT_LAVA;
    }
    if (have_flag(f_ptr->flags, FF_GLOW)) {
        bits |= FC_FEAT_GLOW;
    }
    /* 通り抜けられる床（HD2D の扉の向き判定の主材料）。壁・扉・木・岩は明示的に外す。 */
    if (have_flag(f_ptr->flags, FF_MOVE)
        && !(bits & (FC_FEAT_WALL | FC_FEAT_DOOR | FC_FEAT_TREE | FC_FEAT_RUBBLE))) {
        bits |= FC_FEAT_PASSABLE;
    }
    if (c_ptr->info & CAVE_ROOM) {
        bits |= FC_FEAT_ROOM;
    }
    if ((c_ptr->info & (CAVE_GLOW | CAVE_MNDK)) == CAVE_GLOW) {
        bits |= FC_FEAT_GLOWING;
    }
    return bits;
}

/*!
 * @brief 正体の分からないアイテムの分類（1..8）を返す。0 = 対象外。
 * @details。
 * 「正体そのものが分からない」の判定は `k_info[].flavor != 0`（未鑑定名の有無。
 * 変愚の `is_apparent()` に当たる）と `k_info[].aware`（鑑定済みか。変愚の
 * `is_aware()` に当たる）。Frox の `TV_*` は変愚と同じ値（`defines.h` 実測）。
 * 番号は `unaware_item_categories.h`（C からも読める）と
 * `presentation::UnawareItemCategory`（`unaware_item_tiles.h`）の 2 つに `static_assert`
 * で突き合わせてある。**`fc_native_store_common.h` と共有する**
 * （宣言は `fc_unaware_category.h`）——同じ判定を 2 か所に書かない（契約 §4）。
 *
 * **指輪・アミュレット・ワンド・杖・ロッドは対象外**と実測で確かめた
 * （`lib/edit/k_info.txt` を読んだ結果）。Frox はこの 5 種を tval ごとに
 * `N:*:& Ring~` のような**種類そのものが 1 個しか無い**形で持っており
 * （薬 67 種・巻物 61 種・キノコ入り食料 28 種とは対照的）、`init1.c:2886-2895`
 * の解析ロジック（`N:` 行の 2 個目の `:` の後ろだけを `flavor_name` として拾う）
 * に掛かる素材がそもそも無い。だから `k_info[].flavor` は指輪等では**常に 0**
 * ——武器・防具と同じ「種類は最初から分かり、エゴ／修正値だけが未鑑定」の側
 * （契約 §2 の 2）。ここへ来る前の `!k_ptr->flavor` の判定で自然に弾かれる
 * ので、この 5 種の `case` は書かない（書いても到達しない死んだ分岐になる）。
 * `Hand of Vecna`（`TV_GLOVES`）・`Eye of Vecna`（`TV_LITE`）も同じ側
 * （`INSTA_ART` の固定アーティファクトで、`a_info.txt` にしか無く `k_info.txt` の
 * flavor 対象ではない）。
 */
unsigned char fc_unaware_category(const object_type *o_ptr)
{
    object_kind *k_ptr;
    if (!o_ptr->k_idx) {
        return 0;
    }
    k_ptr = &k_info[o_ptr->k_idx];
    if (!k_ptr->flavor || k_ptr->aware) {
        return 0; /* 未鑑定名が無い（武器・防具・指輪・アミュレット・ワンド・杖・ロッドの類）か、すでに鑑定済み */
    }
    switch (k_ptr->tval) {
    case TV_POTION:
        return UNAWARE_CATEGORY_POTION;
    case TV_SCROLL:
        return UNAWARE_CATEGORY_SCROLL;
    case TV_FOOD:
        return UNAWARE_CATEGORY_MUSHROOM; /* flavor!=0 の FOOD はキノコだけ */
    default:
        return 0; /* 指輪・アミュレット・ワンド・杖・ロッドはここに来ない（上のコメント参照）。それ以外の未確認の分類も含む */
    }
}

/*
 * 画面を作るための呼び出しで、ゲーム本体の乱数を進めない。
 * `map_info()`（`cave.c:1209` 付近）は幻覚中、マスごとに `Rand_quick = TRUE` と
 * `Rand_value` を自分で書き換えて `one_in_(255)` を引き、最後に `Rand_quick = FALSE`
 * で終わる（呼ぶ前の `Rand_quick` の値は残らない）。`image_random()` は
 * `Rand_quick = FALSE` にして `randint0` を引く。`monster_desc()` も幻覚中は乱数を引く。
 * 原作はゲームの進行に沿った回数だけ呼ぶが、ここは画面を送る頻度（経過時間）で回数が
 * 変わるので、同じセーブ・同じ入力でも乱数の列が一致しなくなる。呼び出しの前に
 * `Rand_quick` / `Rand_value` / `Rand_place` / `Rand_state[]` をすべて退避し、
 * 終わったら戻す。`fc_targeting.inl` の `_fc_target_rng_save` と同じ内訳。
 */
typedef struct fc_rng_snapshot {
    bool quick;
    u32b value;
    u16b place;
    u32b state[RAND_DEG];
} fc_rng_snapshot;

static void fc_rng_save(fc_rng_snapshot *snap)
{
    snap->quick = Rand_quick;
    snap->value = Rand_value;
    snap->place = Rand_place;
    memcpy(snap->state, Rand_state, sizeof(snap->state));
}

static void fc_rng_restore(const fc_rng_snapshot *snap)
{
    Rand_quick = snap->quick;
    Rand_value = snap->value;
    Rand_place = snap->place;
    memcpy(Rand_state, snap->state, sizeof(snap->state));
}

/*
 * 検査用（環境変数が無ければ何もしない）。`HD2D_TEST_RNG=1` で、呼び出しの間に乱数の状態が
 * どう動いたか（inside）と戻した後（kept）の要約値を標準エラーへ出す。`HD2D_TEST_RNG=nofix` は
 * 戻さない（この直しの前の動きを再現する）。`HD2D_TEST_FORCE_IMAGE=1` は毎回幻覚にする。
 */
static unsigned fc_rng_digest(void)
{
    unsigned h = 2166136261u;
    int i;

    h = (h ^ (unsigned)Rand_quick) * 16777619u;
    h = (h ^ (unsigned)Rand_value) * 16777619u;
    h = (h ^ (unsigned)Rand_place) * 16777619u;
    for (i = 0; i < RAND_DEG; i++) {
        h = (h ^ (unsigned)Rand_state[i]) * 16777619u;
    }
    return h;
}

static void fc_test_force_image(void)
{
    if (getenv("HD2D_TEST_FORCE_IMAGE") != NULL) {
        p_ptr->image = 1000;
    }
}

static void fc_rng_finish(const fc_rng_snapshot *snap, const char *name)
{
    const char *mode = getenv("HD2D_TEST_RNG");
    const int report = (mode != NULL) && (mode[0] != '\0');
    const unsigned inside = report ? fc_rng_digest() : 0;

    if (!(report && (strcmp(mode, "nofix") == 0))) {
        fc_rng_restore(snap);
    }
    if (report) {
        fprintf(stderr, "[hd2d-rng] %s inside=%08x kept=%08x\n", name, inside, fc_rng_digest());
    }
}

static int fc_read_map_raw(int x0, int y0, int w, int h, fc_map_cell *out, int cap)
{
    int vx;
    int vy;
    int n = 0;

    if (!out || (w <= 0) || (h <= 0) || (cap < (w * h))) {
        return FC_ERR_ARG;
    }
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return FC_ERR_STATE;
    }

    for (vy = 0; vy < h; vy++) {
        const int gy = y0 + vy;
        for (vx = 0; vx < w; vx++) {
            const int gx = x0 + vx;
            fc_map_cell *cell = &out[n++];
            cave_type *c_ptr;
            s16b feat;
            byte a = 0;
            char c = ' ';
            byte ta = 0;
            char tc = ' ';
            int known;

            memset(cell, 0, sizeof(*cell));
            cell->gx = (short)gx;
            cell->gy = (short)gy;
            cell->ascii = ' ';

            /* 階の外。**空白で返す**（画面側が黒く塗る）。 */
            if (!in_bounds2(gy, gx)) {
                continue;
            }

            c_ptr = &cave[gy][gx];
            feat = get_feat_mimic(c_ptr);
            known = fc_grid_known(c_ptr, feat);

            /* 見た目はコアの `map_info()` そのまま（記号・色を作り直さない）。 */
            map_info(gy, gx, &a, &c, &ta, &tc);
            cell->fg_color = (unsigned char)(a & 0x0F);
            cell->bg_color = (unsigned char)(ta & 0x0F);
            cell->ascii = c;

            if (known) {
                cell->terrain_id = (unsigned short)feat;
                cell->feature_flags = (unsigned short)(FC_FEAT_KNOWN | fc_feature_bits(feat, c_ptr));
            }
            /* @ の居場所は既知・未知に関わらず立てる（隠す情報ではない）。 */
            if ((gy == py) && (gx == px)) {
                cell->feature_flags = (unsigned short)(cell->feature_flags | FC_FEAT_PLAYER);
            }

            if (fc_grid_lit_now(c_ptr)) {
                cell->light_level = 2;
            } else if (c_ptr->info & CAVE_VIEW) {
                cell->light_level = 1;
            } else {
                cell->light_level = 0;
            }
            /* プレイヤの光源で照らされているマス（`update_lite()` が立てる）。 */
            cell->lit_by_player = (c_ptr->info & CAVE_LITE) ? 1 : 0;

            /*
             * モンスターは **`ml`（視認中）のときだけ**。種族は `ap_r_idx`
             * （見かけの種族）を使う——`map_info` が絵に使うのもこちらで、
             * 変化・変装の敵で記号とタイルが食い違わないようにする。
             */
            if (c_ptr->m_idx > 0 && c_ptr->m_idx < m_max) {
                monster_type *m_ptr = &m_list[c_ptr->m_idx];
                if (m_ptr->r_idx && m_ptr->ml) {
                    cell->monster_id = (unsigned short)(m_ptr->ap_r_idx ? m_ptr->ap_r_idx : m_ptr->r_idx);
                    cell->monster_slot = (unsigned short)c_ptr->m_idx;
                }
            }

            /*
             * 床のアイテムは `OM_FOUND`（見つけたもの）の先頭 1 つだけ。
             * **いま照らされているマスに限る**（`light_level == 2`）——記憶している
             * だけの暗いマスで立てると、画面側が「実体がいる」と見て板を探しに行き、
             * 絵の決め方（地形が勝つ）と食い違って必ず外れる（幻想蛮怒で踏んだ穴）。
             * 記憶しているだけの物は Term の記号（`ascii`）には残るので情報は落ちない。
             */
            if (cell->light_level >= 2) {
                s16b this_o_idx;
                s16b next_o_idx = 0;
                for (this_o_idx = c_ptr->o_idx; this_o_idx; this_o_idx = next_o_idx) {
                    object_type *o_ptr = &o_list[this_o_idx];
                    next_o_idx = o_ptr->next_o_idx;
                    if (o_ptr->k_idx && (o_ptr->marked & OM_FOUND)) {
                        cell->object_id = (unsigned short)o_ptr->k_idx;
                        cell->object_unaware_category = fc_unaware_category(o_ptr);
                        break;
                    }
                }
            }

            /* 未視認のマスは地形を出さない（真っ暗）。ただし見えている敵の記号は残す。 */
            if (!known) {
                cell->bg_color = 0;
                if (!cell->monster_id) {
                    cell->ascii = ' ';
                    cell->fg_color = 0;
                }
            }
        }
    }

    return n;
}

int fc_read_minimap(unsigned char *out, int cap)
{
    int gx;
    int gy;
    int need;

    if (!out || (cap <= 0)) {
        return FC_ERR_ARG;
    }
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return FC_ERR_STATE;
    }

    need = (int)cur_hgt * (int)cur_wid;
    if (cap < need) {
        return FC_ERR_ARG;
    }
    memset(out, FC_MM_UNKNOWN, (size_t)need);

    for (gy = 0; gy < (int)cur_hgt; gy++) {
        for (gx = 0; gx < (int)cur_wid; gx++) {
            cave_type *c_ptr = &cave[gy][gx];
            const s16b feat = get_feat_mimic(c_ptr);
            feature_type *f_ptr = &f_info[feat];
            const int known = fc_grid_known(c_ptr, feat);
            int monster_here = 0;
            unsigned char kind;

            if (c_ptr->m_idx > 0 && c_ptr->m_idx < m_max) {
                monster_type *m_ptr = &m_list[c_ptr->m_idx];
                monster_here = (m_ptr->r_idx && m_ptr->ml) ? 1 : 0;
            }
            /* 未踏破は出さない。ただし見えている敵（テレパシー等）は出す。 */
            if (!known && !monster_here) {
                continue;
            }

            kind = FC_MM_FLOOR;
            if (known) {
                if (have_flag(f_ptr->flags, FF_STAIRS) || have_flag(f_ptr->flags, FF_ENTRANCE)
                    || have_flag(f_ptr->flags, FF_QUEST_ENTER)) {
                    kind = FC_MM_STAIRS;
                } else if (have_flag(f_ptr->flags, FF_DOOR)) {
                    kind = FC_MM_DOOR;
                } else if (have_flag(f_ptr->flags, FF_WALL)) {
                    /* 山は壁と分けて送る（町の読み方が「敷地か岩山か」を決めるのに使う）。 */
                    kind = have_flag(f_ptr->flags, FF_MOUNTAIN) ? FC_MM_MOUNTAIN : FC_MM_WALL;
                } else if (have_flag(f_ptr->flags, FF_TREE)) {
                    /*
                     * 歩ける地形の細別（FH-09）。Frox の町と荒野は草・木・水が大半で、
                     * 全部 FLOOR に畳むと一色塗りになる。**歩ける意味は変えない**
                     * （画面側の経路判定は FLOOR と同じに扱う）。順は
                     * 木 → 水 → 溶岩 → 雪 → 草——雪解け（SLUSH）のように旗が
                     * 重なる地形は、より「見て効く」ほうに倒す。
                     */
                    kind = FC_MM_TREE;
                } else if (have_flag(f_ptr->flags, FF_WATER)) {
                    kind = FC_MM_WATER;
                } else if (have_flag(f_ptr->flags, FF_LAVA)) {
                    kind = FC_MM_LAVA;
                } else if (have_flag(f_ptr->flags, FF_SNOW)) {
                    kind = FC_MM_SNOW;
                } else if ((feat == feat_grass) || (feat == feat_flower)) {
                    /* 草には旗が無い（`FF_GRASS` は存在しない）。番号で見る。 */
                    kind = FC_MM_GRASS;
                }
            }
            if (known && fc_grid_lit_now(c_ptr) && c_ptr->o_idx && (kind != FC_MM_STAIRS)) {
                s16b this_o_idx;
                s16b next_o_idx = 0;
                for (this_o_idx = c_ptr->o_idx; this_o_idx; this_o_idx = next_o_idx) {
                    object_type *o_ptr = &o_list[this_o_idx];
                    next_o_idx = o_ptr->next_o_idx;
                    if (o_ptr->k_idx && (o_ptr->marked & OM_FOUND)) {
                        kind = FC_MM_ITEM;
                        break;
                    }
                }
            }
            if (monster_here) {
                kind = FC_MM_MONSTER;
            }
            out[(gy * (int)cur_wid) + gx] = kind;
        }
    }

    if (in_bounds2(py, px)) {
        out[(py * (int)cur_wid) + px] = FC_MM_PLAYER;
    }

    return need;
}


/* ============================================ 周囲の地形の内訳（環境音の層） */

/*! 数える半径（マス）。**耳が届くと思える範囲**であって、視界とは別。変愚と同じ 12。 */
#define FC_SURROUNDINGS_RADIUS 12

int fc_read_surroundings(fc_surroundings *out)
{
    int dy;
    int dx;
    int counted = 0;
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
    const int r = FC_SURROUNDINGS_RADIUS;

    if (!out) {
        return FC_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return FC_OK; /* radius 0 のまま＝「数えていない」 */
    }

    for (dy = -r; dy <= r; dy++) {
        for (dx = -r; dx <= r; dx++) {
            const int gy = py + dy;
            const int gx = px + dx;
            cave_type *c_ptr;
            feature_type *f_ptr;
            s16b feat;
            int is_wall;

            if (((dx * dx) + (dy * dy)) > (r * r)) {
                continue; /* 円で数える */
            }
            if (!in_bounds2(gy, gx)) {
                continue;
            }
            c_ptr = &cave[gy][gx];
            feat = get_feat_mimic(c_ptr);
            if (!fc_grid_known(c_ptr, feat)) {
                continue;
            }
            counted++;

            f_ptr = &f_info[feat];
            is_wall = have_flag(f_ptr->flags, FF_WALL) ? 1 : 0;
            if (is_wall) {
                wall++;
            }
            if (have_flag(f_ptr->flags, FF_GLASS)) {
                glass++;
            }

            /* ---- 材料は排他。順番に意味がある（`fc_shim.h` の註記）---- */
            if (feat == feat_swamp) {
                swamp++;
            } else if (have_flag(f_ptr->flags, FF_LAVA)) {
                lava++;
            } else if (have_flag(f_ptr->flags, FF_WATER)) {
                if (have_flag(f_ptr->flags, FF_DEEP)) {
                    deep_water++;
                } else {
                    water++;
                }
            } else if (have_flag(f_ptr->flags, FF_TREE)) {
                tree++;
            } else if (feat == feat_mountain) {
                rock++;
            } else if (have_flag(f_ptr->flags, FF_HURT_ROCK) && !is_wall
                && !have_flag(f_ptr->flags, FF_DOOR)) {
                rock++; /* 瓦礫（変愚の STONE に当たる） */
            } else if ((feat == feat_grass) || (feat == feat_brake) || (feat == feat_flower)) {
                grass++;
            } else if (feat == feat_dirt) {
                dirt++;
            }
        }
    }

    if (counted <= 0) {
        return FC_OK;
    }

#define FC_SURR_RATIO(N) ((unsigned char)(((N) * 255) / counted))
    out->grass = FC_SURR_RATIO(grass);
    out->tree = FC_SURR_RATIO(tree);
    out->dirt = FC_SURR_RATIO(dirt);
    out->swamp = FC_SURR_RATIO(swamp);
    out->water = FC_SURR_RATIO(water);
    out->deep_water = FC_SURR_RATIO(deep_water);
    out->lava = FC_SURR_RATIO(lava);
    out->rock = FC_SURR_RATIO(rock);
    out->glass = FC_SURR_RATIO(glass);
    out->wall = FC_SURR_RATIO(wall);
#undef FC_SURR_RATIO
    out->radius = (unsigned char)r;
    out->counted = (unsigned short)((counted > 0xFFFF) ? 0xFFFF : counted);
    return FC_OK;
}

/*!
 * @brief 町の地図の左上が、階の座標（`cave` の x, y）のどこに出ているかを求める。
 *
 * @details Frox の町は荒野の 3x3 の窓の中の 1 枚の区画として作られ（`wild.c` の `_generate_cave`）、
 * 窓は `wilderness_dx`／`dy` が 1 増えるたびに 66 桁・22 行（`WILD_SCROLL_CX`／`CY`）ずれる。
 * 区画 (tx, ty) の左上は、窓の中で
 *
 *     x = ((tx - wilderness_x) * 3 - wilderness_dx) * WILD_SCROLL_CX
 *     y = ((ty - wilderness_y) * 3 - wilderness_dy) * WILD_SCROLL_CY
 *
 * に出る（`_generate_cave` が区画を `rect_translate(viewport, dx, dy)` で置く式そのもの）。
 * 町の区画は `wilderness[ty][tx].town == p_ptr->town_num` で見つける——**`town_num` は
 * `_generate_cave` が 3x3 を x、y の順になめて最後に当たった町で決める**ので、ここも同じ順で
 * 最後に当たったものを採る。町が窓に無い（`town_num` が 0）ときは 0。
 *
 * 画面側は町の座標が動かない前提なので（意匠の表・見た目の乱数の種・岩山の記憶）、
 * この原点を引いた座標で扱う（`presentation/frame/game_frame.h` の `FloorIdentity::town_origin_x`）。
 */
static void fc_town_origin(int *ox, int *oy)
{
    int x;
    int y;
    int tx = 0;
    int ty = 0;
    int found = 0;

    *ox = 0;
    *oy = 0;
    if ((p_ptr->town_num <= 0) || (wilderness == NULL)) {
        return;
    }
    for (x = -1; x <= 1; x++) {
        for (y = -1; y <= 1; y++) {
            const int wx = (int)p_ptr->wilderness_x + x;
            const int wy = (int)p_ptr->wilderness_y + y;

            if ((wx < 0) || (wy < 0) || (wx >= (int)max_wild_x) || (wy >= (int)max_wild_y)) {
                continue;
            }
            if (wilderness[wy][wx].town == p_ptr->town_num) {
                tx = x;
                ty = y;
                found = 1;
            }
        }
    }
    if (!found) {
        return;
    }
    *ox = ((tx * 3) - (int)p_ptr->wilderness_dx) * WILD_SCROLL_CX;
    *oy = ((ty * 3) - (int)p_ptr->wilderness_dy) * WILD_SCROLL_CY;
}

int fc_read_floor_info(fc_floor_info *out)
{
    int day = 0;
    int hour = 0;
    int minute = 0;

    if (!out) {
        return FC_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->day_minute = 720;
    out->daytime = 1;

    if (!character_generated) {
        return FC_OK;
    }

    out->dungeon_id = (int)dungeon_type;
    out->dun_level = (int)dun_level;
    out->town_id = (int)p_ptr->town_num;
    out->wild_mode = p_ptr->wild_mode ? 1 : 0;

    if (p_ptr->inside_arena || p_ptr->inside_battle) {
        out->kind = 4; /* 闘技場 */
    } else if (quests_get_current() && !dungeon_type) {
        out->kind = 3; /* クエスト階 */
    } else if (dun_level) {
        out->kind = 2; /* ダンジョン */
    } else {
        out->kind = 1; /* 地上（町・荒野・広域マップ） */
    }

    fc_place_name(out->place_name, (int)sizeof(out->place_name));

    /* 地上（町・荒野）で広域マップを見ていないときだけ。町の地図の左上の位置。 */
    if ((out->kind == 1) && !p_ptr->wild_mode) {
        fc_town_origin(&out->town_origin_x, &out->town_origin_y);
    }

    extract_day_hour_min(&day, &hour, &minute);
    out->day_minute = ((hour % 24) * 60) + (minute % 60);
    out->daytime = is_daytime() ? 1 : 0;
    out->light_radius = (int)p_ptr->cur_lite;
    out->light_kind = fc_player_light_kind();

    return FC_OK;
}

/* ============================================================ 戦闘の見せ場（FH-05） */

/*
 * 建付けは `fc_shim.h` の節註のとおり。ここに在るのは
 *   - 見せ場の環（`fc_fx_take` で汲んだら消える。幻想蛮怒の `gb_fx_ring` と同じ形）
 *   - 描画色 → element の逆引き（**色が往復でほぼ保たれる**ように選ぶ）
 *   - HP の減りの走査（被弾・命中・とどめ）
 */

#define FC_FX_MAX 128

static fc_fx_event fc_fx_ring[FC_FX_MAX];
static int fc_fx_num = 0;
static unsigned long fc_fx_pushed_total = 0;

/*! 直前の弾道セル（Bolt の src を繋ぐ）。game_turn が変わったら忘れる。 */
static int fc_fx_last_bolt_y = -1;
static int fc_fx_last_bolt_x = -1;
static s32b fc_fx_last_bolt_turn = -1;

/*!
 * `sound(SOUND_KILL)` を聞いた数と、最後に聞いた turn。
 *
 * **走査ごとに使い切ってはいけない**——音（`mon_take_hit`）とスロットの消滅
 * （`delete_monster_idx`）は同じコマンドの中でも別の capture に割れることがある
 * （fresh のたびに frame が出るため。実測 2026-08-24: とどめだけ絵が出なかった）。
 * 代わりに **turn の年齢で失効**させる——古い音を後のテレポート消滅に
 * 化けさせないための守り。
 */
static int fc_fx_kill_sounds = 0;
static s32b fc_fx_kill_heard_turn = 0;
#define FC_FX_KILL_SOUND_AGE 50 /*!< これより古い音は捨てる（1 ターン＝100 前後） */

/*!
 * @brief 描画色（TERM の 16 色）→ `CombatFxElement`。
 *
 * @details 弾道の色は `spell_color()` が `lib/pref/spell-xx.prf` の `Z:` 行
 * から選ぶ。
 * GF_* の番号はこちらへ届かないので、**色から束へ**逆に引く。element は
 * 画面側で色にしか使われない（`combat_fx_view.cpp` の `color_of`）ので、
 * 「描かれた色 → 画面で同系の色になる束」を選べば、コアの宣言した色が
 * 画面でもほぼ保たれる。番号は `CombatFxElement` と同じ（0=物理 … 9=精神）。
 */
static unsigned char fc_fx_element_of_attr(int attr)
{
    static const unsigned char table[16] = {
        6, /* 0 DARK      -> Dark（黒。いちばん暗い束へ） */
        0, /* 1 WHITE     -> Physical（白。COLD の `w` もここ——画面でも白系） */
        0, /* 2 SLATE     -> Physical（灰） */
        1, /* 3 ORANGE    -> Fire */
        1, /* 4 RED       -> Fire */
        4, /* 5 GREEN     -> Acid（緑。POIS の `g` もここ——画面でも緑） */
        2, /* 6 BLUE      -> Cold（青系） */
        0, /* 7 UMBER     -> Physical（茶。ARROW / CONFUSION の `U`） */
        6, /* 8 L_DARK    -> Dark */
        0, /* 9 L_WHITE   -> Physical */
        5, /* 10 VIOLET   -> Poison（画面の紫） */
        3, /* 11 YELLOW   -> Elec（黄） */
        1, /* 12 L_RED    -> Fire */
        4, /* 13 L_GREEN  -> Acid */
        2, /* 14 L_BLUE   -> Cold */
        0, /* 15 L_UMBER  -> Physical */
    };
    return table[attr & 0x0F];
}

static void fc_fx_push(const fc_fx_event *ev)
{
    if (fc_fx_num >= FC_FX_MAX) {
        int i;
        for (i = 1; i < FC_FX_MAX; i++) {
            fc_fx_ring[i - 1] = fc_fx_ring[i];
        }
        fc_fx_num = FC_FX_MAX - 1;
    }
    fc_fx_ring[fc_fx_num++] = *ev;
    fc_fx_pushed_total++;
}

int fc_fx_take(fc_fx_event *out, int cap)
{
    int n = fc_fx_num;
    if (!out || (cap <= 0)) {
        return 0;
    }
    if (n > cap) {
        n = cap;
    }
    memcpy(out, fc_fx_ring, (size_t)n * sizeof(fc_fx_event));
    /* 汲んだら消す（消さないと同じ縁を何度も拾って演出が止まらない）。 */
    fc_fx_num = 0;
    return n;
}

unsigned long fc_fx_total(void)
{
    return fc_fx_pushed_total;
}

void fc_fx_note_kill_sound(void)
{
    if (fc_fx_kill_sounds < 8) {
        fc_fx_kill_sounds++;
    }
    fc_fx_kill_heard_turn = game_turn;
}

/* ==================================================================== 魔法・投射物の効果（projections / missiles） */

/*
 * `GameFrame::projections`（飛ぶ魔法・ボール・ブレス・ビーム・視界内の全員）と `GameFrame::missiles`（矢弾・投擲）の材料
 * （決めたこと 2026-10-11）。
 *
 * 原作の `project()` などに足した**観測の呼び出し 1 行**（`tools/targeting/attack_fx_overlay.py`。記録するだけで遊びの結果は
 * 変えない）がここを呼ぶ。ここは原作の型（`GF_xxx`・`PROJECT_xxx`・物）を読んで、属性のコア名・形・範囲・経路・旗の整数に直し、
 * 共通の記録の置き場（`presentation/bridge/named_attack_fx.h`）へ `fc_named_fx_*`（fc_frame.cpp）経由で積む。
 * 属性のコア名から見た目の名前への変換は C++ 側（`attack_fx_core_attr_aliases.h` の `frox_look_name`）。
 *
 * 旧来の `combat_fx` の弾道・爆発の火花（Term に描かれた記号の観測。`scraped`）は**積まない**（上の記録と二重に出るため。
 * HP の減りから拾う命中の火花だけ残す）。
 */
#include "fc_fx_names.inc"

/*! `presentation/frame/attack_fx.h` の `ProjectionShape` / `AttackFxOrigin` / `ProjectionFxFlag` / `MissileKind` と同じ並び。 */
enum { FC_FXS_BOLT = 0, FC_FXS_BEAM = 1, FC_FXS_BALL = 2, FC_FXS_BREATH = 3, FC_FXS_JUMP = 4 };
enum { FC_FXO_PLAYER = 0, FC_FXO_MONSTER = 1, FC_FXO_OTHER = 2 };
enum { FC_FXF_HIDDEN = 1, FC_FXF_BLIND = 2, FC_FXF_REFLECTED = 4, FC_FXF_ALL_IN_SIGHT = 8 };
enum { FC_FXM_ARROW = 0, FC_FXM_BOLT = 1, FC_FXM_SHOT = 2, FC_FXM_THROWN = 3 };

/*! 1 件に積むマスの上限（`named_attack_fx.h` の `kNamedMaxCells` と同じ）。 */
#define FC_FX_CELLS 512

static const char *fc_fx_gf_name(int typ)
{
    size_t i;

    if (typ <= 0) {
        return "";
    }
    for (i = 0; i < sizeof(fc_fx_gf_names) / sizeof(fc_fx_gf_names[0]); i++) {
        if (fc_fx_gf_names[i].typ == typ) {
            return fc_fx_gf_names[i].name;
        }
    }
    return "";
}

static int fc_fx_origin_of(int who)
{
    return (who == 0) ? FC_FXO_PLAYER : ((who > 0) ? FC_FXO_MONSTER : FC_FXO_OTHER);
}

/*! 直前の投射の要約（反射の見分け用。反射は `project()` の中で入れ子に呼ばれるので、記録の並びで見分ける）。 */
static struct {
    int valid;
    int reflectable;
    int grids;
    int typ;
    int impact_y, impact_x;
    int flg;
} fc_fx_prev;

void fc_fx_note_projection(int who, int y1, int x1, int by, int bx, int typ, int rad, int flg, int blind, int breath, int jump,
    const u16b *path_g, int path_n, const byte *gy, const byte *gx, int grids)
{
    static int path[FC_FX_CELLS * 2];
    static int area[FC_FX_CELLS * 3];
    int shape;
    int flags = 0;
    int pn = 0;
    int an = 0;
    int i;
    int impact_y = by;
    int impact_x = bx;
    const int hidden = (flg & PROJECT_HIDE) ? 1 : 0;
    const int origin = fc_fx_origin_of(who);

    if (breath) {
        shape = FC_FXS_BREATH;
    } else if (flg & PROJECT_BEAM) {
        shape = FC_FXS_BEAM;
    } else if (jump || (path_n <= 0) || ((y1 == by) && (x1 == bx))) {
        shape = FC_FXS_JUMP;
    } else if (rad > 0) {
        shape = FC_FXS_BALL;
    } else {
        shape = FC_FXS_BOLT;
    }

    /* 経路：始点の次のマスから着弾点まで（ボルト・ボールは `path_n` が着弾点で切ってある。ビーム・ブレスは全部） */
    if (shape != FC_FXS_JUMP) {
        for (i = 0; (i < path_n) && (pn < FC_FX_CELLS); i++) {
            path[pn * 2] = GRID_Y(path_g[i]);
            path[pn * 2 + 1] = GRID_X(path_g[i]);
            pn++;
        }
        if ((shape == FC_FXS_BEAM) && (pn > 0)) {
            impact_y = path[(pn - 1) * 2];
            impact_x = path[(pn - 1) * 2 + 1];
        }
    }

    /* 範囲：プレイヤの視界内のマスだけ（隠す投射は全部）。距離は中心（ブレスは撃った者）から。ビームは 0。 */
    for (i = 0; (i < grids) && (an < FC_FX_CELLS); i++) {
        const int y = gy[i];
        const int x = gx[i];
        int d = 0;

        /* ビームの範囲の先頭には撃った者のマスが入っている（原作の「ビームのマスを集める」の最初の 1 マス）。経路に無いので数えない */
        if ((shape == FC_FXS_BEAM) && (y == y1) && (x == x1)) {
            continue;
        }
        if (!hidden && !player_has_los_bold(y, x)) {
            continue;
        }
        if (shape == FC_FXS_BREATH) {
            d = distance(y1, x1, y, x);
        } else if (shape != FC_FXS_BEAM) {
            d = distance(by, bx, y, x);
        }
        area[an * 3] = (d < 0) ? 0 : ((d > 255) ? 255 : d);
        area[an * 3 + 1] = y;
        area[an * 3 + 2] = x;
        an++;
    }

    if (hidden) {
        flags |= FC_FXF_HIDDEN;
    }
    if (blind) {
        flags |= FC_FXF_BLIND;
    }
    /* 視界内の全員（`project_hack()`）：プレイヤが飛び先指定・当てる・隠す、で半径 0 */
    if ((who == PROJECT_WHO_PLAYER) && jump && (rad == 0) && !breath && (flg == (PROJECT_KILL | PROJECT_HIDE))) {
        flags |= FC_FXF_ALL_IN_SIGHT;
    }
    /* 反射：直前が反射できる 1 マスの投射で、そのマスからモンスターが同じ属性を撃ち返した */
    if (fc_fx_prev.valid && fc_fx_prev.reflectable && (fc_fx_prev.grids <= 1) && (origin == FC_FXO_MONSTER) && (rad == 0)
        && (typ == fc_fx_prev.typ) && (y1 == fc_fx_prev.impact_y) && (x1 == fc_fx_prev.impact_x)
        && ((flg & ~PROJECT_PLAYER) == (fc_fx_prev.flg & ~PROJECT_PLAYER))) {
        flags |= FC_FXF_REFLECTED;
    }
    fc_fx_prev.valid = 1;
    fc_fx_prev.reflectable = (flg & PROJECT_REFLECTABLE) ? 1 : 0;
    fc_fx_prev.grids = grids;
    fc_fx_prev.typ = typ;
    fc_fx_prev.impact_y = impact_y;
    fc_fx_prev.impact_x = impact_x;
    fc_fx_prev.flg = flg;

    fc_named_fx_projection(origin, shape, fc_fx_gf_name(typ), rad, y1, x1, impact_y, impact_x, flags, path, pn, area, an);
}

/*! シーカーレイ・スーパーレイ：`project()` が途中で戻るので、最初の経路だけを太いビームとして記録する。 */
void fc_fx_note_ray(int who, int y1, int x1, int typ, int flg, int blind, int jump, const u16b *path_g, int path_n)
{
    static int path[FC_FX_CELLS * 2];
    static int area[FC_FX_CELLS * 3];
    int pn = 0;
    int an = 0;
    int i;
    int impact_y = y1;
    int impact_x = x1;
    const int hidden = (flg & PROJECT_HIDE) ? 1 : 0;

    (void)jump;
    for (i = 0; (i < path_n) && (pn < FC_FX_CELLS); i++) {
        const int y = GRID_Y(path_g[i]);
        const int x = GRID_X(path_g[i]);

        path[pn * 2] = y;
        path[pn * 2 + 1] = x;
        pn++;
        impact_y = y;
        impact_x = x;
        if (hidden || player_has_los_bold(y, x)) {
            area[an * 3] = 0;
            area[an * 3 + 1] = y;
            area[an * 3 + 2] = x;
            an++;
        }
    }
    fc_named_fx_projection(fc_fx_origin_of(who), FC_FXS_BEAM, fc_fx_gf_name(typ), 0, y1, x1, impact_y, impact_x,
        (hidden ? FC_FXF_HIDDEN : 0) | (blind ? FC_FXF_BLIND : 0), path, pn, area, an);
}

/*! 飛ばした物の基本アイテムの番号。名前を知らない物（正体の分からないアイテム）は正体を漏らさないので 0。 */
static int fc_fx_item_id(const object_type *o_ptr)
{
    if (!o_ptr || !o_ptr->k_idx) {
        return 0;
    }
    if (fc_unaware_category(o_ptr) != 0) {
        return 0;
    }
    return o_ptr->k_idx;
}

/*!
 * 矢弾の属性：**識別済みのものだけ**（原作より情報を増やさない）。プレイヤ自身が選んだスナイパーの技（`shoot_hack`）と、
 * 矢弾・弓の識別済みの旗（`obj_flags_known`）から決める。理力は原作と同じ MP の条件（`1 + dd * ds / 2 <= csp`）を満たすときだけ。
 */
static const char *fc_fx_known_brand(const object_type *ammo, const object_type *launcher)
{
    u32b fa[OF_ARRAY_SIZE];
    u32b fl[OF_ARRAY_SIZE];

    if (shoot_hack == SP_LITE) {
        return "lite";
    }
    if (shoot_hack == SP_FIRE) {
        return "fire";
    }
    if (shoot_hack == SP_COLD) {
        return "cold";
    }
    if (shoot_hack == SP_ELEC) {
        return "elec";
    }
    if (shoot_hack == SP_KILL_WALL) {
        return "kill_wall";
    }
    if (shoot_hack == SP_FINAL) {
        return "mana";
    }
    obj_flags_known((object_type *)ammo, fa);
    obj_flags_known((object_type *)launcher, fl);
#define FC_FX_KNOWN(flag) (have_flag(fa, (flag)) || have_flag(fl, (flag)))
    if (FC_FX_KNOWN(OF_BRAND_FIRE)) {
        return "fire";
    }
    if (FC_FX_KNOWN(OF_BRAND_COLD)) {
        return "cold";
    }
    if (FC_FX_KNOWN(OF_BRAND_ELEC)) {
        return "elec";
    }
    if (FC_FX_KNOWN(OF_BRAND_ACID)) {
        return "acid";
    }
    if (FC_FX_KNOWN(OF_BRAND_POIS)) {
        return "pois";
    }
    if (FC_FX_KNOWN(OF_BRAND_MANA) && ((1 + ammo->dd * ammo->ds / 2) <= p_ptr->csp)) {
        return "mana";
    }
#undef FC_FX_KNOWN
    return "";
}

/*! 射撃 1 歩（`do_cmd_fire_aux2()`）。`cur_dis == 1` が 1 発の始まり。盲目のときは記録しない（原作も見えないマスの動きは描かない）。 */
void fc_fx_shot(int cur_dis, int y, int x, int prev_y, int prev_x, const object_type *ammo, const object_type *launcher)
{
    int kind = FC_FXM_ARROW;

    if (p_ptr->blind || !ammo) {
        return;
    }
    if (ammo->tval == TV_BOLT) {
        kind = FC_FXM_BOLT;
    } else if (ammo->tval == TV_SHOT) {
        kind = FC_FXM_SHOT;
    }
    fc_named_fx_missile_step((cur_dis <= 1) ? 1 : 0, kind, fc_fx_item_id(ammo), (cur_dis <= 1) ? fc_fx_known_brand(ammo, launcher) : "",
        prev_y, prev_x, y, x);
}

/*!
 * 投擲 1 歩（`py_throw.c` の `_throw()`。`path_pos == 0` が 1 発の始まり）。
 * 壁のマス（原作は壁の手前に落とす）は経路に入れない。
 */
void fc_fx_throw(int path_pos, const u16b *path, const object_type *obj)
{
    const int y = GRID_Y(path[path_pos]);
    const int x = GRID_X(path[path_pos]);
    int prev_y = py;
    int prev_x = px;

    if (p_ptr->blind || !obj) {
        return;
    }
    if (!cave_have_flag_bold(y, x, FF_PROJECT) && !cave[y][x].m_idx) {
        return;
    }
    if (path_pos > 0) {
        prev_y = GRID_Y(path[path_pos - 1]);
        prev_x = GRID_X(path[path_pos - 1]);
    }
    fc_named_fx_missile_step((path_pos <= 0) ? 1 : 0, FC_FXM_THROWN, fc_fx_item_id(obj), "", prev_y, prev_x, y, x);
}

/*! 射撃・投擲が相手に当たった。 */
void fc_fx_hit(void)
{
    if (p_ptr->blind) {
        return;
    }
    fc_named_fx_missile_hit();
}

/*! 投げた物が戻ってくる（ブーメランなど。原作が戻りの動きを描く所）。 */
void fc_fx_throw_return(void)
{
    if (p_ptr->blind) {
        return;
    }
    fc_named_fx_missile_returns();
}

/* ==================================================================== 効果音 */

/*
 * 見せ場の環（上）と同じ形。違いは 2 つだけ。
 *   - 画面側が欲しがっているときしか積まない（`fc_sound_wanted`）
 *   - 溢れたら**古いほうを捨てる**（見せ場は詰め直すが、音は 1 コマンドで
 *     何十も出ることがあり、詰め直しの写しが割に合わない）
 */

#define FC_SOUND_MAX 32

static fc_sound_event fc_sound_ring[FC_SOUND_MAX];
static int fc_sound_num = 0;
static unsigned long fc_sound_pushed_total = 0;
static int fc_sound_wanted = 0;

void fc_sound_set_wanted(int wanted)
{
    fc_sound_wanted = wanted ? 1 : 0;
    if (!fc_sound_wanted) {
        /* 欲しがらなくなったら溜まっているぶんも捨てる（古い音が後で鳴らないように）。 */
        fc_sound_num = 0;
    }
}

void fc_sound_push(int v)
{
    fc_sound_event *ev;
    const char *name;

    if (!fc_sound_wanted) {
        return;
    }
    if ((v <= 0) || (v >= SOUND_MAX)) {
        return;
    }
    name = angband_sound_name[v];
    if (!name || !name[0]) {
        return;
    }

    if (fc_sound_num >= FC_SOUND_MAX) {
        /* 溢れたら最も古い 1 つを捨てる。 */
        int i;
        for (i = 1; i < FC_SOUND_MAX; i++) {
            fc_sound_ring[i - 1] = fc_sound_ring[i];
        }
        fc_sound_num = FC_SOUND_MAX - 1;
    }

    ev = &fc_sound_ring[fc_sound_num++];
    my_strcpy(ev->name, name, sizeof(ev->name));
    /*
     * **鳴ったマスは @ のマス**にする。`TERM_XTRA_SOUND` は番号しか運ばないので
     * 音源の位置が分からない——画面側では頭で鳴る。マスを持つ音は後から
     * 呼び出し箇所ごとに足していく。
     */
    if (character_generated) {
        ev->y = (short)py;
        ev->x = (short)px;
    } else {
        ev->y = 0;
        ev->x = 0;
    }
    fc_sound_pushed_total++;
}

int fc_sound_take(fc_sound_event *out, int cap)
{
    int n = fc_sound_num;
    if (!out || (cap <= 0)) {
        return 0;
    }
    if (n > cap) {
        n = cap;
    }
    memcpy(out, fc_sound_ring, (size_t)n * sizeof(fc_sound_event));
    /* 汲んだら消す。音は状態ではなく一度きりの出来事である（設計 §2.3）。 */
    fc_sound_num = 0;
    return n;
}

unsigned long fc_sound_total(void)
{
    return fc_sound_pushed_total;
}

void fc_fx_push_spark(int gy, int gx, int ch, int attr)
{
    fc_fx_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.scraped = 1; /* Term の観測。`fill_combat_fx` は積まない（projections / missiles が受け持つ） */
    ev.element = fc_fx_element_of_attr(attr);
    ev.y = (short)gy;
    ev.x = (short)gx;
    ev.src_y = (short)gy;
    ev.src_x = (short)gx;
    /* 強さは動きの絵なので固定（ダメージの割合は HP の走査のほうが持つ）。 */
    ev.num = 1;
    ev.den = 2;

    if (ch == '*') {
        /* 爆発・ボール（`bolt_pict` の base 0x30）。そのマスで弾ける。 */
        ev.kind = FC_FX_KIND_HIT_MONSTER;
        fc_fx_last_bolt_y = -1;
        fc_fx_last_bolt_x = -1;
    } else {
        /*
         * 弾（`|` `-` `/` `\`）。直前の弾道セルが同じ turn で隣接していれば
         * そこからの線にする（画面は src -> dst の線を引く）。
         */
        ev.kind = FC_FX_KIND_BOLT;
        if ((fc_fx_last_bolt_turn == game_turn) && (fc_fx_last_bolt_y >= 0)
            && (ABS(fc_fx_last_bolt_y - gy) <= 2) && (ABS(fc_fx_last_bolt_x - gx) <= 2)) {
            ev.src_y = (short)fc_fx_last_bolt_y;
            ev.src_x = (short)fc_fx_last_bolt_x;
        }
        fc_fx_last_bolt_y = gy;
        fc_fx_last_bolt_x = gx;
        fc_fx_last_bolt_turn = game_turn;
    }
    fc_fx_push(&ev);
}

/*!
 * @name HP の走査の前回値
 * @details スロットの使い回しに備えて `r_idx` も覚える。階が替わったら
 * （`character_dungeon` が降りたら／場所の素性が動いたら）**黙って捨てる**
 * ——階またぎの消滅にとどめの絵を出すのは嘘になる。
 * @{
 */
typedef struct fc_fx_mon_prev {
    s16b r_idx;
    s16b hp;
    s16b maxhp;
    byte fy;
    byte fx;
    byte ml;
} fc_fx_mon_prev;

#define FC_FX_MON_PREV_MAX 1024

static fc_fx_mon_prev fc_fx_mon_prev_tab[FC_FX_MON_PREV_MAX];
static int fc_fx_prev_valid = 0;
static int fc_fx_prev_chp = 0;
static int fc_fx_prev_dungeon_type = -1;
static int fc_fx_prev_dun_level = -1;
static int fc_fx_prev_wild_x = -1;
static int fc_fx_prev_wild_y = -1;
/*! @} */

/*! @brief 前回値を取り直すだけ（見せ場は積まない）。 */
static void fc_fx_rebase(void)
{
    int i;
    int n = (int)m_max;

    if (n > FC_FX_MON_PREV_MAX) {
        n = FC_FX_MON_PREV_MAX;
    }
    memset(fc_fx_mon_prev_tab, 0, sizeof(fc_fx_mon_prev_tab));
    for (i = 1; i < n; i++) {
        monster_type *m_ptr = &m_list[i];
        if (!m_ptr->r_idx) {
            continue;
        }
        fc_fx_mon_prev_tab[i].r_idx = m_ptr->r_idx;
        fc_fx_mon_prev_tab[i].hp = m_ptr->hp;
        fc_fx_mon_prev_tab[i].maxhp = m_ptr->maxhp;
        fc_fx_mon_prev_tab[i].fy = m_ptr->fy;
        fc_fx_mon_prev_tab[i].fx = m_ptr->fx;
        fc_fx_mon_prev_tab[i].ml = m_ptr->ml ? 1 : 0;
    }
    fc_fx_prev_chp = character_generated ? (int)p_ptr->chp : 0;
    fc_fx_prev_dungeon_type = (int)dungeon_type;
    fc_fx_prev_dun_level = (int)dun_level;
    fc_fx_prev_wild_x = (int)p_ptr->wilderness_x;
    fc_fx_prev_wild_y = (int)p_ptr->wilderness_y;
    fc_fx_prev_valid = 1;
}

void fc_fx_scan_deltas(void)
{
    int i;
    int n;
    int floor_moved;

    if (!character_generated || !character_dungeon || p_ptr->is_dead) {
        /* @ がまだ無い・階がまだ無い・もう死んだ。次に揃ったとき取り直す。 */
        fc_fx_prev_valid = 0;
        fc_fx_kill_sounds = 0;
        return;
    }

    floor_moved = (fc_fx_prev_dungeon_type != (int)dungeon_type)
        || (fc_fx_prev_dun_level != (int)dun_level)
        || (fc_fx_prev_wild_x != (int)p_ptr->wilderness_x)
        || (fc_fx_prev_wild_y != (int)p_ptr->wilderness_y);

    if (!fc_fx_prev_valid || floor_moved) {
        /* 初回か階またぎ。差分は取れない（取ったら嘘になる）ので基準だけ更新。 */
        fc_fx_rebase();
        fc_fx_kill_sounds = 0;
        return;
    }

    /* ---- 被弾（`p_ptr->chp` の減り） ---- */
    if (((int)p_ptr->chp < fc_fx_prev_chp) && (p_ptr->mhp > 0)) {
        fc_fx_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.kind = FC_FX_KIND_HIT_PLAYER;
        ev.element = 0; /* 何で減ったかは分からない（毒・罠・地形も混ざる）。物理の白 */
        ev.y = (short)py;
        ev.x = (short)px;
        ev.src_y = (short)py;
        ev.src_x = (short)px;
        ev.num = (short)(fc_fx_prev_chp - (int)p_ptr->chp);
        ev.den = (short)p_ptr->mhp;
        fc_fx_push(&ev);
    }

    /* ---- 命中ととどめ（`m_list[]` の HP の減りとスロット消滅） ---- */
    n = (int)m_max;
    if (n > FC_FX_MON_PREV_MAX) {
        n = FC_FX_MON_PREV_MAX;
    }
    for (i = 1; i < n; i++) {
        monster_type *m_ptr = &m_list[i];
        fc_fx_mon_prev *prev = &fc_fx_mon_prev_tab[i];

        if (prev->r_idx && m_ptr->r_idx == prev->r_idx) {
            if ((m_ptr->hp < prev->hp) && (m_ptr->ml || prev->ml) && (prev->maxhp > 0)) {
                fc_fx_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.kind = FC_FX_KIND_HIT_MONSTER;
                ev.element = 0;
                ev.y = (short)m_ptr->fy;
                ev.x = (short)m_ptr->fx;
                ev.src_y = ev.y;
                ev.src_x = ev.x;
                ev.num = (short)(prev->hp - m_ptr->hp);
                ev.den = prev->maxhp;
                fc_fx_push(&ev);
            }
        } else if (prev->r_idx && prev->ml && (fc_fx_kill_sounds > 0)) {
            /*
             * 見えていた敵のスロットが消えた（または別の敵になった）。
             * **`sound(SOUND_KILL)` を聞いた数の中でだけ**とどめと読む——
             * テレポートや解放でも消えるので、消滅だけでとどめにすると嘘になる。
             */
            fc_fx_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.kind = FC_FX_KIND_HIT_MONSTER;
            ev.element = 0;
            ev.y = (short)prev->fy;
            ev.x = (short)prev->fx;
            ev.src_y = ev.y;
            ev.src_x = ev.x;
            ev.num = 1; /* とどめは満額（overkill の量は分からない） */
            ev.den = 1;
            fc_fx_push(&ev);
            fc_fx_kill_sounds--;
        }
    }

    fc_fx_rebase();
    /* とどめの音は年齢で失効（節註）。使い切りにすると音と消滅が別 capture に
     * 割れたとき（fresh ごとに frame が出る）とどめの絵が落ちる。 */
    if ((game_turn - fc_fx_kill_heard_turn) > FC_FX_KILL_SOUND_AGE) {
        fc_fx_kill_sounds = 0;
    }
}

/* ================================================================== セーブの口 */

const char *fc_save_dir(void)
{
    return ANGBAND_DIR_SAVE ? ANGBAND_DIR_SAVE : "";
}

int fc_set_savefile(const char *slot)
{
    if (!slot || !slot[0]) {
        return FC_ERR_ARG;
    }
    if (!ANGBAND_DIR_SAVE || !ANGBAND_DIR_SAVE[0]) {
        return FC_ERR_STATE; /* init_file_paths() より前 */
    }
    if (strlen(slot) >= sizeof(savefile_base)) {
        return FC_ERR_ARG;
    }

    /* `process_player_name()` が両方を見る（片方だけだと sf が真に倒れて訊かれる）。 */
    strcpy(savefile_base, slot);
    path_build(savefile, sizeof(savefile), ANGBAND_DIR_SAVE, slot);
    return FC_OK;
}

void fc_clear_savefile(void)
{
    savefile[0] = '\0';
    savefile_base[0] = '\0';
}

const char *fc_savefile_base(void)
{
    return savefile_base;
}

const char *fc_savefile_path(void)
{
    return savefile;
}

/* ========================================================== 持ち物・装備・矢筒 */

/*!
 * @brief 1 個ぶんの行を組む。
 * @details **ここが設計 §3.4（決めたこと F3）の書き直しである。**
 *
 * 幻想蛮怒は `inventory[]` という 1 本の配列を添字で切って持ち物と装備に
 * 分けていたが、Frox はその方式を捨てて 3 つの器に分けた:
 *
 * | 器 | 反復 | 枠の名 |
 * |---|---|---|
 * | 持ち物 | `pack_obj(1 .. pack_max())`（`pack.h`） | `slot_label()` の英字 |
 * | 装備 | `equip_obj(1 .. equip_max())`（`equip.h`） | `equip_describe_slot()` |
 * | 矢筒 | `quiver_obj(1 .. quiver_max())`（`quiver.h`） | `slot_label()` の英字 |
 *
 * **枠は空でもよい**（`inv_obj()` は空き枠に NULL を返す）ので、
 * 「埋まっている枠だけ数える」形は変えない——画面側の目録は詰めた並びを期待する。
 *
 * 1 個の説明文は `object_desc()`（`flavor.c`）で、署名は幻想蛮怒と同じ。
 */
static int fc_read_item_line_raw(int what, int index, char *out, int cap)
{
    int slot;
    int last;
    int seen = 0;

    if (!out || (cap <= 1)) {
        return FC_ERR_ARG;
    }
    out[0] = 0;
    if (!character_generated) {
        return 0;
    }

    if (what == FC_ITEMS_EQUIPMENT) {
        last = equip_max();
    } else if (what == FC_ITEMS_QUIVER) {
        last = quiver_max();
    } else {
        last = pack_max();
    }

    for (slot = 1; slot <= last; slot++) {
        obj_ptr o_ptr;
        char name[MAX_NLEN];
        char line[MAX_NLEN + 32];

        if (what == FC_ITEMS_EQUIPMENT) {
            o_ptr = equip_obj(slot);
        } else if (what == FC_ITEMS_QUIVER) {
            o_ptr = quiver_obj(slot);
        } else {
            o_ptr = pack_obj(slot);
        }

        if (!o_ptr) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }

        object_desc(name, o_ptr, OD_NAME_ONLY | OD_OMIT_PREFIX);
        if (what == FC_ITEMS_EQUIPMENT) {
            /*
             * 枠の名は**コアに訊く**（`equip_describe_slot()`）。幻想蛮怒のように
             * 2 文字の表を当方で持つと、Frox の職・種族で枠の並びが変わったときに
             * 黙ってずれる（あちらの枠は種族で増減する）。
             */
            cptr desc = equip_describe_slot(slot);
            sprintf(line, "%.12s %s", (desc && desc[0]) ? desc : "??", name);
        } else {
            sprintf(line, "%c) %s", slot_label(slot), name);
        }
        fc_copy_str(out, cap, line);
        return (int)strlen(out);
    }
    return 0;
}

static int fc_read_visible_monster_raw(int index, char *out, int cap)
{
    int i;
    int seen = 0;

    if (!out || (cap <= 1)) {
        return FC_ERR_ARG;
    }
    out[0] = 0;
    if (!character_generated || !character_dungeon) {
        return 0;
    }

    for (i = 1; i < m_max; i++) {
        monster_type *m_ptr = &m_list[i];
        char name[MAX_NLEN];

        if (!m_ptr->r_idx) {
            continue;
        }
        /* **視認しているものだけ。**見えていない敵を名前で漏らさない。 */
        if (!m_ptr->ml) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }
        monster_desc(name, m_ptr, MD_ASSUME_VISIBLE | MD_INDEF_VISIBLE);
        fc_copy_str(out, cap, name);
        return (int)strlen(out);
    }
    return 0;
}

/* ================================================== コマンドの表（v1 §8） */

/*
 * **`menu_info` / `special_menu_info` の型は `externs.h` に出ていない**
 * （`util.c:3339` と `:3480` の中だけで typedef している）。配列そのものは
 * static ではないのでリンクはできる。そこで**同じ並びの型をここに写して** extern する。
 * 幻想蛮怒の `gb_shim.c` と同じ手である——Frox も変愚の直系なので表の形が同じ。
 *
 * 写しなので、先方が構造体を変えたら黙って別物を読む。それを防ぐために
 * `fc_menu_table_sane()` で**表の実際の値**を確かめ、合わなければ 0 件を返す
 * （呼び手の `fc_pad_commands.cpp` が手書きの最小表へ落ちる）。
 * **`frox/src/` は 1 バイトも触らない**（設計 §1 制約 1）。
 * **上流は現役**なので、取り込み直しのたびにこの sane 検査が効く。
 */
typedef struct fc_menu_naiyou {
    cptr name;
    byte cmd;
    bool fin;
} fc_menu_naiyou;

typedef struct fc_special_menu_naiyou {
    cptr name;
    byte window;
    byte number;
    byte jouken;
    byte jouken_naiyou;
} fc_special_menu_naiyou;

extern fc_menu_naiyou menu_info[10][10];
extern fc_special_menu_naiyou special_menu_info[];

#define FC_MENU_CLASS 1 /* util.c:3489 の MENU_CLASS */
#define FC_MENU_WILD 2 /* 同 MENU_WILD */

/*!
 * @brief 型の写しが合っているかを、表の**実際の値**で確かめる。
 * @details 大きさが同じでも並びが違えば読み間違える。だから値で見る。
 * ここに挙げた 5 つは `util.c:3347` の表の頭と尻で、どれも意味のある値である。
 */
static int fc_menu_table_sane(void)
{
    if (menu_info[0][0].fin) return 0; /* 分類は fin == FALSE */
    if (menu_info[0][0].cmd != 1) return 0; /* "Magic/Special" → 小メニュー 1 */
    if (!menu_info[1][0].fin) return 0; /* 小メニューの項目は fin == TRUE */
    if (menu_info[1][0].cmd != (byte)0x6D) return 0; /* "Use(m)" の m */
    if (!menu_info[0][9].name || menu_info[0][9].name[0]) return 0; /* 尻は空文字列 */
    return 1;
}

/*!
 * @brief コマンド → その配列で押すべきキー（逆引き）。
 * @details `keymap_act[mode][cmd]` の逆引き。幻想蛮怒の同名関数の写し。
 * @return 押すキー。**0 は「そのコマンドは出せない」**
 */
static unsigned char fc_key_for_command(int mode, unsigned char command)
{
    int i;

    if (!command) {
        return 0;
    }
    for (i = 0; i < 256; i++) {
        cptr act = keymap_act[mode][i];
        if (!act) continue;
        if ((act[0] == (char)command) && !act[1]) {
            return (unsigned char)i;
        }
    }
    /* 逆引き失敗。cmd 自身がキーマップ済みなら押しても別物になるので出さない。 */
    if (keymap_act[mode][command]) {
        return 0;
    }
    return command;
}

/*!
 * @brief `special_menu_info` による名札の差し替え（`util.c:3544-3558` の写し）。
 * @details 画面に出る名前とパッドの名札を食い違わせないために、判定を丸ごと写す。
 * 幻想蛮怒版との違いは MENU_WILD の条件が `py_on_surface()` になったことだけ。
 */
static cptr fc_menu_name(int window, int number, cptr fallback)
{
    int i;

    for (i = 0;; i++) {
        if (!special_menu_info[i].name || !special_menu_info[i].name[0]) break;
        if ((int)special_menu_info[i].window != window) continue;
        if ((int)special_menu_info[i].number != number) continue;
        switch (special_menu_info[i].jouken) {
        case FC_MENU_CLASS:
            if (p_ptr->pclass == special_menu_info[i].jouken_naiyou) {
                fallback = special_menu_info[i].name;
            }
            break;
        case FC_MENU_WILD:
            if (py_on_surface()) {
                if ((byte)p_ptr->wild_mode == special_menu_info[i].jouken_naiyou) {
                    fallback = special_menu_info[i].name;
                }
            }
            break;
        default:
            break;
        }
    }
    return fallback;
}

int fc_read_pad_commands(fc_pad_command *out, int cap)
{
    int n = 0;
    int g;

    if (!out || (cap <= 0)) {
        return FC_ERR_ARG;
    }
    if (!fc_menu_table_sane()) {
        return 0;
    }

    /* menu_info[0] は分類（fin == FALSE で cmd が小メニュー番号）。 */
    for (g = 0; g < 10; g++) {
        const fc_menu_naiyou *group = &menu_info[0][g];
        cptr group_name;
        int sub;
        int k;

        if (group->fin) continue;
        if (!group->name || !group->name[0]) continue;
        sub = (int)group->cmd;
        if ((sub <= 0) || (sub >= 10)) continue;
        group_name = fc_menu_name(0, g, group->name);

        for (k = 0; k < 10; k++) {
            const fc_menu_naiyou *item = &menu_info[sub][k];

            /* fin == TRUE の項目だけがコマンド（FALSE は小メニューへの分岐）。 */
            if (!item->fin) continue;
            if (!item->name || !item->name[0] || !item->cmd) continue;
            if (n >= cap) return n;

            memset(&out[n], 0, sizeof(out[n]));
            out[n].command = item->cmd;
            out[n].key_normal = fc_key_for_command(KEYMAP_MODE_ORIG, item->cmd);
            out[n].key_rogue = fc_key_for_command(KEYMAP_MODE_ROGUE, item->cmd);
            fc_copy_str(out[n].group, (int)sizeof(out[n].group), group_name);
            fc_copy_str(out[n].label, (int)sizeof(out[n].label), fc_menu_name(sub, k, item->name));
            n++;
        }
    }
    return n;
}

unsigned char fc_pad_key_for_command(int rogue, int command)
{
    if ((command <= 0) || (command > 255)) {
        return 0;
    }
    return fc_key_for_command(rogue ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG, (unsigned char)command);
}

int fc_pad_table_stamp(void)
{
    int s = (int)p_ptr->pclass;

    s = s * 31 + (p_ptr->wild_mode ? 1 : 0);
    s = s * 31 + (dun_level ? 1 : 0);
    s = s * 31 + (p_ptr->inside_arena ? 1 : 0);
    s = s * 31 + (py_on_surface() ? 1 : 0);
    s = s * 31 + (rogue_like_commands ? 1 : 0);
    s = s * 31 + (character_generated ? 1 : 0);
    return s;
}

void fc_apply_cursor_mode(int enable)
{
    if (!enable) {
        return; /* 従来どおり（文字キー主体）。use_menu はコアに任せる */
    }
    if (!character_generated) {
        return; /* タイトル・誕生画面はコアの別メニュー。触らない */
    }
    command_menu = TRUE; /* Enter ＝ コマンドメニュー */
    use_menu = TRUE; /* 以降の選択はコアのカーソルで */
}

/* ============================================================ 地形の全数照合（P7） */

int fc_terrain_defined(int feat)
{
    if ((feat < 0) || (feat >= (int)max_f_idx)) {
        return -1;
    }
    if (!f_info || !f_name) {
        return 0;
    }
    /* 定義の無い枠は名前のオフセットが 0（f_name の頭は空文字列）。 */
    return (f_info[feat].name && f_name[f_info[feat].name]) ? 1 : 0;
}

int fc_monster_defined(int r_idx)
{
    if ((r_idx < 0) || (r_idx >= (int)max_r_idx)) {
        return -1;
    }
    if (!r_info || !r_name) {
        return 0;
    }
    /* 定義の無い枠は名前のオフセットが 0（`r_name` の頭は空文字列）。地形と同じ。 */
    return (r_info[r_idx].name && r_name[r_info[r_idx].name]) ? 1 : 0;
}

int fc_object_defined(int k_idx)
{
    if ((k_idx < 0) || (k_idx >= (int)max_k_idx)) {
        return -1;
    }
    if (!k_info || !k_name) {
        return 0;
    }
    return (k_info[k_idx].name && k_name[k_info[k_idx].name]) ? 1 : 0;
}

/* ==================================================================== 色の申告 */

int fc_read_palette(unsigned char *rgb, int cap)
{
    int i;

    if (!rgb || (cap < 16 * 3)) {
        return FC_ERR_ARG;
    }

    /*
     * `angband_color_table[i]` は 4 バイト（[0] は使わず、[1..3] が RGB）。
     * **16 色だけ**を送る（設計 §4.1。Frox の色は `TERM_DARK`〜`TERM_L_UMBER`）。
     */
    for (i = 0; i < 16; i++) {
        rgb[(i * 3) + 0] = angband_color_table[i][1];
        rgb[(i * 3) + 1] = angband_color_table[i][2];
        rgb[(i * 3) + 2] = angband_color_table[i][3];
    }
    return 16 * 3;
}

/* Report only the core command input boundary, never a nested prompt. */
int fc_awaiting_command(void)
{
    return character_generated && !p_ptr->is_dead && inkey_flag;
}

/* ============================================== 検査用：敵の行動の指定（HD2D_TEST_MONSTER_ACT） */

/*
 * 攻撃の効果の検査で、敵が魔法を撃つまで休んで待たずに済ませるための入口。
 * **環境変数 `HD2D_TEST_MONSTER_ACT`（命令ファイルのパス）が無いときは何もしない**
 * （普段の遊び・配布物には効かない）。
 *
 * 呼び出しは `fc_null_term.c` の「キーを待つ間の繰り返し」の頭（キーを Term へ積む直前）から。
 * 最上位のコマンドを待っているとき（`fc_awaiting_command()`）だけ読む。-more- や選択画面の最中は読まない。
 * ファイルがあれば**複数行**を上から順に、1 回のコマンド待ちの中で全部実行し（その間ゲームの時間は進めない）、
 * ファイルを消す（消せなければ名前を変える）。空行と `#` で始まる行は読み飛ばす。
 * 結果は標準エラーへ 1 行ずつ `[test-monster-act] ok <命令>` / `[test-monster-act] fail <理由>`。
 * 途中で fail したら残りの行は実行せず、各行に `[test-monster-act] skip <命令>` を出す。
 *
 *   cast <原作の魔法の名前> [nearest|<番号>]  … その敵に、その魔法をプレイヤーへ向けていま撃たせる
 *   melee [nearest|<番号>]                    … その敵に、いまプレイヤーへ直接攻撃させる（隣にいるときだけ）
 *   place <nearest|番号> <dx> <dy>            … その敵を、プレイヤーの位置から (dx, dy) ずれたマスへ動かす（時間は使わない）
 *   hold <nearest|番号|all> on|off            … on の間、その敵は自分の番に行動しない（all は敵対している敵だけ。ペットは止めない）
 *
 * `place`：マスが地図の外・その敵が立てない地形・ほかの敵やプレイヤーがいる、のとき fail。動かしたあとの更新は、
 * 原作が敵を動かすとき（`melee2.c` の `process_monster()`、`spells3.c` の `teleport_away()`）と同じ
 * （`cave[][].m_idx` の付け替え・`update_mon()`・`lite_spot()`・`PW_MONSTER_LIST`）。
 *
 * `hold`：敵が行動する条件は、原作の `process_monsters()` が敵ごとの `energy_need`（次の番までの待ち。0 以下で番が来る）を
 * 速さの分だけ減らし、0 以下になった敵だけを動かす仕組み。止めている敵の `energy_need` を、コマンド待ちのたびに
 * 番が来ない大きな値（`FC_TMA_HOLD_VALUE`）へ置き直す。止める前の値を覚えておき、off で戻す。
 * 眠らせる・麻痺させる方法は使わない（画面の表示や状態が変わるため）。止めている敵が死んだ・階を移った（その枠が
 * ほかの敵や空になった）ときは、記録から外す。入口から命じた cast・melee・place は `energy_need` を見ないので働く。
 * `hold` が成功したときは、判定の行のほかに `[test-monster-act] info held=<止めている敵の番号,...>` を出す（検査用）。
 *
 * 魔法の名前は原作の記法（`BR_FIRE`・`BO_FIRE`・`BA_POIS`・`MISSILE`・`BLINK` ほか。`mon_spell_parse()` が読む名前）。
 * 敵がその魔法を持っているかは問わない。持っていればその種族の定義で、持っていなければ原作の読み取りが返す既定の強さで撃たせる。
 * 撃たせ方は原作の `mon_spell_cast(mon, ai)` に「指定の魔法を選ぶ ai」を渡す（原作のデバッグ用の
 * `mon_spell_ai_wizard` と同じ道。原作の旗・確率の表は書き換えない）。
 *
 * 番の外から呼ぶので、モンスターの番の途中で原作が前提にしているものを整える:
 *   - 出現直後の敵にプレイヤーの最初の 1 手を待たせる印（`MFLAG_NICE`）を外す（原作もそのターンの終わりに外す）
 *   - 敵がプレイヤーに気づいている印（`MFLAG2_AWARE`。呼んだばかりの敵は最初の番までは立っていない）を、
 *     敵の最初の番と同じ条件で立てる
 *   - `hack_m_idx`（いまの番の敵。`process_monsters()` が立てる）を撃たせる敵にし、終わったら戻す
 *   - `msg_boundary()`（敵の番の頭と同じ。メッセージの区切り）
 *   - `inkey_flag`（コマンド待ちの旗。メッセージの -more- が入れ子の `inkey()` を呼ぶと下ろされるので、終わったら戻す）
 *   - 終わったら `notice_stuff()`・`handle_stuff()`・`Term_fresh()`（番の終わりと同じ再描画の要求の処理）
 * `mon_current()`／`mon_spell_current()` は `mon_spell_cast()` 自身が立てて消す。
 * **乱数と乱数の種には触らない**（魔法の内側の通常の乱数の消費だけ）。
 */
#define FC_TMA_MAX_LINES 128

static int fc_tma_busy = 0;
static int fc_tma_stuck = 0;
static const mon_spell_t *fc_tma_spell = NULL;

static bool fc_tma_ai(mon_spell_cast_ptr cast)
{
    cast->spell = (mon_spell_ptr)fc_tma_spell;
    return cast->spell != NULL;
}

static void fc_tma_report(const char *verdict, const char *line, const char *reason)
{
    if (reason) {
        fprintf(stderr, "[test-monster-act] %s %s\n", verdict, reason);
    } else {
        fprintf(stderr, "[test-monster-act] %s %s\n", verdict, line);
    }
    fflush(stderr);
}

/* 敵の指定（nearest・省略・番号）。番号は m_list の添字（フレームの monster_slot と同じ）。 */
static monster_type *fc_tma_pick(const char *arg, int *m_idx_out)
{
    int i;
    int best = 0;
    int best_dist = 0;

    if (arg && arg[0] && strcmp(arg, "nearest") != 0) {
        char *end = NULL;
        long n = strtol(arg, &end, 10);
        if (!end || (*end != '\0') || (n < 1) || (n >= (long)m_max)) {
            return NULL;
        }
        if (!m_list[n].r_idx) {
            return NULL;
        }
        *m_idx_out = (int)n;
        return &m_list[n];
    }

    for (i = 1; i < m_max; i++) {
        monster_type *m_ptr = &m_list[i];
        int d;

        if (!m_ptr->r_idx || !m_ptr->ml) {
            continue;
        }
        d = distance(py, px, m_ptr->fy, m_ptr->fx);
        if (!best || (d < best_dist)) {
            best = i;
            best_dist = d;
        }
    }
    if (!best) {
        return NULL;
    }
    *m_idx_out = best;
    return &m_list[best];
}

/*
 * 止めている敵の記録。`energy_need` は s16b（最大 32767）。
 *   FC_TMA_HOLD_VALUE … 止めるときに置く値。速い敵でも休みを数十ターン重ねて減らし切れない大きさ
 *   FC_TMA_HOLD_REARM … これを下回っていたら置き直す
 *   FC_TMA_HOLD_VALID … これを下回っていたら「止めていた敵ではない」とみなして記録から外す
 *                       （通常の敵の `energy_need` は数百以下。その枠が別の敵に使い回されたことを見分ける）
 */
#define FC_TMA_HOLD_MAX   256
#define FC_TMA_HOLD_VALUE 30000
#define FC_TMA_HOLD_REARM 25000
#define FC_TMA_HOLD_VALID 5000

typedef struct {
    int m_idx;
    s16b r_idx;
    s16b saved;
} fc_tma_hold_t;

static fc_tma_hold_t fc_tma_holds[FC_TMA_HOLD_MAX];
static int fc_tma_nholds = 0;
static int fc_tma_hold_all = 0;

static int fc_tma_hold_find(int m_idx)
{
    int i;
    for (i = 0; i < fc_tma_nholds; i++) {
        if (fc_tma_holds[i].m_idx == m_idx) {
            return i;
        }
    }
    return -1;
}

static void fc_tma_hold_remove_at(int k)
{
    fc_tma_holds[k] = fc_tma_holds[fc_tma_nholds - 1];
    fc_tma_nholds--;
}

/* その敵を止める記録に足す（止める前の energy_need を覚えて置き直す）。すでに止めていれば何もしない。足せなければ FALSE。 */
static bool fc_tma_hold_add(int m_idx)
{
    monster_type *m_ptr = &m_list[m_idx];

    if (fc_tma_hold_find(m_idx) >= 0) {
        return TRUE;
    }
    if (fc_tma_nholds >= FC_TMA_HOLD_MAX) {
        return FALSE;
    }
    fc_tma_holds[fc_tma_nholds].m_idx = m_idx;
    fc_tma_holds[fc_tma_nholds].r_idx = m_ptr->r_idx;
    fc_tma_holds[fc_tma_nholds].saved = m_ptr->energy_need;
    fc_tma_nholds++;
    m_ptr->energy_need = FC_TMA_HOLD_VALUE;
    return TRUE;
}

/* 記録の k 番を元に戻して外す。枠がほかの敵に使い回されていれば値は戻さない。 */
static void fc_tma_hold_release_at(int k)
{
    const fc_tma_hold_t h = fc_tma_holds[k];
    monster_type *m_ptr = &m_list[h.m_idx];

    if (m_ptr->r_idx == h.r_idx && m_ptr->energy_need >= FC_TMA_HOLD_VALID) {
        m_ptr->energy_need = h.saved;
    }
    fc_tma_hold_remove_at(k);
}

/* コマンド待ちのたびに呼ぶ：外れた記録を外し、all のとき新しい敵対の敵を足し、止めている敵の待ちを置き直す。 */
static void fc_tma_hold_maintain(void)
{
    int i;

    if (!character_generated || (fc_tma_nholds == 0 && !fc_tma_hold_all)) {
        return;
    }
    for (i = fc_tma_nholds - 1; i >= 0; i--) {
        const fc_tma_hold_t *h = &fc_tma_holds[i];
        monster_type *m_ptr = ((h->m_idx >= 1) && (h->m_idx < m_max)) ? &m_list[h->m_idx] : NULL;

        if (!m_ptr || !m_ptr->r_idx || (m_ptr->r_idx != h->r_idx) || (m_ptr->energy_need < FC_TMA_HOLD_VALID)) {
            fc_tma_hold_remove_at(i);
        }
    }
    if (fc_tma_hold_all) {
        for (i = 1; i < m_max; i++) {
            monster_type *m_ptr = &m_list[i];
            if (m_ptr->r_idx && (i != p_ptr->riding) && is_hostile(m_ptr)) {
                (void)fc_tma_hold_add(i);
            }
        }
    }
    for (i = 0; i < fc_tma_nholds; i++) {
        monster_type *m_ptr = &m_list[fc_tma_holds[i].m_idx];
        if (m_ptr->energy_need < FC_TMA_HOLD_REARM) {
            m_ptr->energy_need = FC_TMA_HOLD_VALUE;
        }
    }
}

/* 1 命令を実行する。成功なら NULL、失敗なら理由の文字列を返す。 */
static const char *fc_tma_run_one(char *line)
{
    char *tok[4];
    int ntok = 0;
    char *p = line;
    int m_idx = 0;
    monster_type *m_ptr;
    int saved_hack;
    bool saved_inkey_flag;
    bool done = FALSE;

    while (*p && (ntok < 4)) {
        while ((*p == ' ') || (*p == '\t')) {
            *p++ = '\0';
        }
        if (!*p) {
            break;
        }
        tok[ntok++] = p;
        while (*p && (*p != ' ') && (*p != '\t')) {
            p++;
        }
    }
    if (*p) {
        *p = '\0';
    }
    if (ntok == 0) {
        return NULL;
    }

    if (!character_generated || !p_ptr->playing || p_ptr->is_dead) {
        return "the game is not running";
    }

    if (strcmp(tok[0], "cast") == 0) {
        mon_spell_t want;
        mon_spell_ptr spell;
        char name[64];

        if (ntok < 2) {
            return "cast needs a spell name";
        }
        if (strlen(tok[1]) >= sizeof(name)) {
            return "spell name too long";
        }
        m_ptr = fc_tma_pick((ntok >= 3) ? tok[2] : NULL, &m_idx);
        if (!m_ptr) {
            return "no such monster";
        }
        /* 原作の記法の名前を原作の読み取りで id に直す（BR_FIRE → 息・火 など） */
        memset(&want, 0, sizeof(want));
        strcpy(name, tok[1]);
        if (mon_spell_parse(&want, r_info[m_ptr->r_idx].level, name) != 0) {
            return "unknown spell name";
        }
        /* その敵の種族が持つ魔法ならその定義を、持たなければ読み取った定義（既定の強さ）をそのまま使う */
        spell = r_info[m_ptr->r_idx].spells ? mon_spells_find(r_info[m_ptr->r_idx].spells, want.id) : NULL;
        if (!spell) {
            spell = &want;
        }
        if (m_ptr->cdis > MAX_RANGE) {
            return "the monster is out of range";
        }
        if (!is_hostile(m_ptr)) {
            return "the monster is not hostile";
        }
        if (MON_CONFUSED(m_ptr)) {
            return "the monster is confused";
        }
        /*
         * 原作の `mon_spell_cast()` は「プレイヤーに気づいている」敵にしか撃たせない（`_can_cast()`）。
         * 気づく処理は敵の最初の番で `process_monsters()` が自動で行う（`melee2.c`「Monsters are automatically aware
         * of the player」）。呼んだばかりの敵はまだ番が来ていないので、同じ印を立てる。
         */
        if (!is_aware(m_ptr) && (p_ptr->prace != RACE_MON_RING)) {
            m_ptr->mflag2 |= MFLAG2_AWARE;
        }
        /*
         * `MFLAG_NICE`（出現した直後の敵に「プレイヤーの最初の 1 手」を待たせる印）が立っていると撃てない（`_can_cast()`）。
         * 原作はそのターンの終わりに `dungeon.c` の `repair_monsters` の処理で全員から外す。同じ印を外す。
         */
        m_ptr->mflag &= ~(MFLAG_NICE);
        fc_tma_spell = spell;
        saved_hack = hack_m_idx;
        saved_inkey_flag = inkey_flag;
        hack_m_idx = m_idx;
        msg_boundary();
        done = mon_spell_cast(m_ptr, fc_tma_ai);
        hack_m_idx = saved_hack;
        inkey_flag = saved_inkey_flag;
        fc_tma_spell = NULL;
        if (!done) {
            return "the monster could not cast (not aware, or anti-magic level)";
        }
    } else if (strcmp(tok[0], "melee") == 0) {
        m_ptr = fc_tma_pick((ntok >= 2) ? tok[1] : NULL, &m_idx);
        if (!m_ptr) {
            return "no such monster";
        }
        if (distance(py, px, m_ptr->fy, m_ptr->fx) != 1) {
            return "the monster is not adjacent";
        }
        saved_hack = hack_m_idx;
        saved_inkey_flag = inkey_flag;
        hack_m_idx = m_idx;
        msg_boundary();
        done = make_attack_normal(m_idx);
        hack_m_idx = saved_hack;
        inkey_flag = saved_inkey_flag;
        if (!done) {
            return "the monster did not attack (never-blow/friendly?)";
        }
    } else if (strcmp(tok[0], "place") == 0) {
        char *end_dx = NULL;
        char *end_dy = NULL;
        long ldx, ldy;
        int ny, nx, oy, ox;
        monster_race *r_ptr;

        if (ntok < 4) {
            return "place needs <monster> <dx> <dy>";
        }
        ldx = strtol(tok[2], &end_dx, 10);
        ldy = strtol(tok[3], &end_dy, 10);
        if (!end_dx || *end_dx || !end_dy || *end_dy || (ldx < -300) || (ldx > 300) || (ldy < -300) || (ldy > 300)) {
            return "place needs integer dx and dy";
        }
        m_ptr = fc_tma_pick(tok[1], &m_idx);
        if (!m_ptr) {
            return "no such monster";
        }
        if (m_idx == p_ptr->riding) {
            return "the monster is the player's mount";
        }
        ny = py + (int)ldy;
        nx = px + (int)ldx;
        if (!in_bounds(ny, nx)) {
            return "the grid is outside the map";
        }
        r_ptr = &r_info[m_ptr->r_idx];
        oy = m_ptr->fy;
        ox = m_ptr->fx;
        if (!((oy == ny) && (ox == nx))) {
            if (player_bold(ny, nx)) {
                return "the player is on that grid";
            }
            if (cave[ny][nx].m_idx) {
                return "another monster is on that grid";
            }
            if (!monster_can_enter(ny, nx, r_ptr, 0)) {
                return "the monster cannot stand on that terrain";
            }
            /* 原作が敵を動かすとき（teleport_away() ほか）と同じ付け替えと更新 */
            cave[oy][ox].m_idx = 0;
            cave[ny][nx].m_idx = m_idx;
            m_ptr->fy = ny;
            m_ptr->fx = nx;
            reset_target(m_ptr);
            update_mon(m_idx, TRUE);
            lite_spot(oy, ox);
            lite_spot(ny, nx);
            p_ptr->window |= PW_MONSTER_LIST;
            if (r_ptr->flags7 & (RF7_LITE_MASK | RF7_DARK_MASK)) {
                p_ptr->update |= PU_MON_LITE;
            }
        }
    } else if (strcmp(tok[0], "hold") == 0) {
        int on;
        int k;

        if (ntok < 3) {
            return "hold needs <monster|all> on|off";
        }
        if (strcmp(tok[2], "on") == 0) {
            on = 1;
        } else if (strcmp(tok[2], "off") == 0) {
            on = 0;
        } else {
            return "hold needs on or off";
        }
        if (strcmp(tok[1], "all") == 0) {
            if (on) {
                fc_tma_hold_all = 1;
                fc_tma_hold_maintain();
            } else {
                fc_tma_hold_all = 0;
                while (fc_tma_nholds > 0) {
                    fc_tma_hold_release_at(fc_tma_nholds - 1);
                }
            }
        } else {
            m_ptr = fc_tma_pick(tok[1], &m_idx);
            if (!m_ptr) {
                return "no such monster";
            }
            if (on) {
                if (!fc_tma_hold_add(m_idx)) {
                    return "too many held monsters";
                }
            } else {
                if (fc_tma_hold_all) {
                    return "hold all is on (turn it off first)";
                }
                k = fc_tma_hold_find(m_idx);
                if (k >= 0) {
                    fc_tma_hold_release_at(k);
                }
            }
        }
        /* 止めている敵の番号の一覧（検査が、ペットが含まれないことなどを確かめる。判定の行ではない） */
        {
            int j;
            fprintf(stderr, "[test-monster-act] info held=");
            for (j = 0; j < fc_tma_nholds; j++) {
                fprintf(stderr, "%s%d", (j > 0) ? "," : "", fc_tma_holds[j].m_idx);
            }
            fprintf(stderr, "\n");
            fflush(stderr);
        }
    } else {
        return "unknown command";
    }

    if (p_ptr->notice) {
        notice_stuff();
    }
    handle_stuff();
    Term_fresh();
    return NULL;
}

void fc_test_monster_act_poll(void)
{
    const char *path = getenv("HD2D_TEST_MONSTER_ACT");
    FILE *fp;
    char buf[16384];
    char lines[FC_TMA_MAX_LINES][256];
    int nlines = 0;
    size_t len;
    int i;
    int failed = 0;

    if (!path || !path[0] || fc_tma_busy) {
        return;
    }
    /* 止めている敵の待ちの置き直し（命令ファイルが無くても毎回） */
    fc_tma_hold_maintain();
    if (!fc_awaiting_command()) {
        return;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        fc_tma_stuck = 0;
        return;
    }
    if (fc_tma_stuck) {
        fclose(fp);
        return;
    }
    len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[len] = '\0';

    /* 先に消す。消せなければ名前を変える。どちらもできなければ二重に撃たないよう印を立てる */
    if (remove(path) != 0) {
        char moved[1100];
        snprintf(moved, sizeof(moved), "%s.done", path);
        (void)remove(moved);
        if (rename(path, moved) != 0) {
            fc_tma_stuck = 1;
        }
    }

    {
        char *q = buf;
        while (*q && (nlines < FC_TMA_MAX_LINES)) {
            char *eol = q;
            size_t n;
            while (*eol && (*eol != '\n') && (*eol != '\r')) {
                eol++;
            }
            n = (size_t)(eol - q);
            if (n >= sizeof(lines[0])) {
                n = sizeof(lines[0]) - 1;
            }
            memcpy(lines[nlines], q, n);
            lines[nlines][n] = '\0';
            nlines++;
            while ((*eol == '\n') || (*eol == '\r')) {
                eol++;
            }
            q = eol;
        }
    }

    fc_tma_busy = 1;
    for (i = 0; i < nlines; i++) {
        char shown[256];
        const char *why;
        char *a = lines[i];
        size_t alen;

        while ((*a == ' ') || (*a == '\t')) {
            a++;
        }
        alen = strlen(a);
        while ((alen > 0) && ((a[alen - 1] == ' ') || (a[alen - 1] == '\t'))) {
            a[--alen] = '\0';
        }
        /* 空行と # で始まる行は読み飛ばす */
        if (!*a || (*a == '#')) {
            continue;
        }
        strcpy(shown, a);
        if (failed) {
            fc_tma_report("skip", shown, NULL);
            continue;
        }
        why = fc_tma_run_one(a);
        if (why) {
            fc_tma_report("fail", shown, why);
            failed = 1;
        } else {
            fc_tma_report("ok", shown, NULL);
        }
    }
    fc_tma_busy = 0;
}

/* ============================================== オプションの一覧（原作の表からの全件の取得） */

/*
 * `cmd4.c` の `do_cmd_options_aux()` が頁ごとに並べる項目は `option_info[]`（`tables.c`）から
 * 引く。Frox の頁（`OPT_PAGE_FROX`）だけは、ほかの頁にある項目から**名前で選んだ**一覧を並べる。
 * 選ぶ名前の表は原作の関数の中の static 配列で、外から読めない。そこで**名前（オプションの識別子）だけ**を
 * ここへ写す（原作の配列の書き方・並びは写さない。名前はゲームの設定の名前＝事実であって、処理の書き方ではない）。
 * 原作を取り込み直して名前が変わったら、筋書き `frox_options` が原作のソースの数え方と食い違って落ちる。
 */
static const char *const fc_options_frox_page_names[] = {
    "alert_item_major",
    "alert_item_minor",
    "autorun_max_steps",
    "center_stair_tele",
    "confirm_hit_unseen",
    "confirm_hit_visible",
    "display_percentages",
    "display_upkeep_bar",
    "disturb_wakeup",
    "find_ignore_side",
    "inscriptions_first",
    "map_edge_center",
    "messages_wrap",
    "monlist_awake",
    "monlist_distance",
    "monlist_los",
    "monlist_range",
    "monlist_range_all",
    "monlist_ungroup",
    "monlist_zzz",
    "retry_item_count",
    "retry_obvious_count",
    "show_damage_hp",
    "show_item_markers",
    "show_special_bags",
    "suppress_main_msgs",
    "temp_file_policy",
};

/*! @brief 頁の `index` 番目の項目が `option_info[]` の何番か（無ければ -1）。並びは原作の画面と同じ。 */
static int fc_options_find(int page, int index)
{
    int i;
    int n = 0;

    if ((page < OPT_PAGE_INPUT) || (page > OPT_PAGE_FROX) || (index < 0)) {
        return -1;
    }
    for (i = 0; option_info[i].o_desc; i++) {
        int hit = 0;

        if (page == OPT_PAGE_FROX) {
            size_t j;

            for (j = 0; j < (sizeof(fc_options_frox_page_names) / sizeof(fc_options_frox_page_names[0])); j++) {
                if (option_info[i].o_text && streq(option_info[i].o_text, fc_options_frox_page_names[j])) {
                    hit = 1;
                    break;
                }
            }
        } else {
            hit = (option_info[i].o_page == page);
        }
        if (hit) {
            if (n == index) {
                return i;
            }
            n++;
        }
    }
    return -1;
}

/*! @brief 原作が行の終わりに `(名前)` を付けない項目か（値だけの行。`do_cmd_options_aux()` の分岐の写し）。 */
static int fc_options_without_ident(const option_type *o)
{
    return (o->o_var == &ironman_empty_levels) || (o->o_var == &single_pantheon)
        || (o->o_var == &guaranteed_pantheon) || (o->o_var == &always_small_levels);
}

int fc_options_row(int page, int index, char *ident, int ident_cap, char *desc, int desc_cap)
{
    const int at = fc_options_find(page, index);

    if (at < 0) {
        return 0;
    }
    if (ident && (ident_cap > 0)) {
        ident[0] = '\0';
        if (!fc_options_without_ident(&option_info[at]) && option_info[at].o_text) {
            /* 原作は `(%.19s)` で書く */
            int n = (int)strlen(option_info[at].o_text);

            if (n > 19) {
                n = 19;
            }
            if (n > ident_cap - 1) {
                n = ident_cap - 1;
            }
            memcpy(ident, option_info[at].o_text, (size_t)n);
            ident[n] = '\0';
        }
    }
    /* 原作は `strnfmt("%-48s: ", o_desc)` で書く。`%s` の引数は #3（`fc_tr_arg`）が引く */
    fc_copy_str(desc, desc_cap, fc_tr_arg(option_info[at].o_desc));
    return 1;
}

/*! @brief 行全体の書式（`"%-48s: …"`）で組んだ字から、行頭の説明の部分を除いて値だけを `tmp` へ入れる。 */
static void fc_options_after_head(const char *head, const char *line, char *tmp, int cap)
{
    const size_t n = strlen(head);

    fc_copy_str(tmp, cap, (strncmp(line, head, n) == 0) ? (line + n) : line);
}

/*!
 * @brief 値の書き方（`do_cmd_options_aux()` の分岐の写し）。`buf` へ `": "` の後ろ・`(名前)` の前の字を入れる。
 * @details 原作の関数内の static（`_option_value_append` `_temp_file_policy_desc`）は外から呼べないので、
 * 同じ書式（`%-4s `）をここで書く。訳す字は原作と同じ口（`fc_tr`）を通す。
 * **原作が行全体を 1 つの書式で組む項目**（`"%-48s: %d of %d"` など）は、書式の訳（フック #2。
 * `"%d of %d"` → `"%d / %d"`）が行全体に掛かるので、ここでも行全体で組んでから行頭の説明を除く。
 */
int fc_options_value(int page, int index, char *buf, int cap)
{
    const int at = fc_options_find(page, index);
    const option_type *o;
    char tmp[128];
    char head[320];
    char line[320];

    if ((at < 0) || !buf || (cap <= 0)) {
        return 0;
    }
    o = &option_info[at];
    buf[0] = '\0';
    tmp[0] = '\0';
    strnfmt(head, sizeof(head), "%-48s: ", o->o_desc);
    if (o->o_var == &random_artifacts) {
        strnfmt(tmp, sizeof(tmp), "%-4s ", random_artifacts ? format("%d%%", random_artifact_pct) : "no");
    } else if (o->o_var == &ironman_empty_levels) {
        strnfmt(line, sizeof(line), "%-48s: %s", o->o_desc, fc_tr(empty_lv_description[generate_empty]));
        fc_options_after_head(head, line, tmp, sizeof(tmp));
    } else if (o->o_var == &reduce_uniques) {
        strnfmt(tmp, sizeof(tmp), "%-4s ", reduce_uniques ? format("%d%%", reduce_uniques_pct) : "no");
    } else if (o->o_var == &obj_list_width) {
        strnfmt(tmp, sizeof(tmp), "%-4s ", format("%d", object_list_width));
    } else if (o->o_var == &mon_list_width) {
        strnfmt(tmp, sizeof(tmp), "%-4s ", format("%d", monster_list_width));
    } else if (o->o_var == &msg_pane_wrap_width) {
        strnfmt(tmp, sizeof(tmp), "%-4s ", (messages_wrap_width > 150) ? "off" : format("%d", messages_wrap_width));
    } else if (o->o_var == &autorun_max_steps_dummy) {
        strnfmt(tmp, sizeof(tmp), "%-4s ", !autorun_max_steps ? "off" : format("%d", autorun_max_steps));
    } else if (o->o_var == &map_edge_center_dummy) {
        strnfmt(tmp, sizeof(tmp), "%-4s ", format("%d", map_edge_center_distance_normalize(map_edge_center_distance)));
    } else if (o->o_var == &inscriptions_first_dummy) {
        static const char *const names[] = { "None", "User", "All" };

        strnfmt(line, sizeof(line), "%-48s: %-4s ", o->o_desc, fc_tr(names[MIN(inscriptions_first, INSCRIPTIONS_FIRST_ON)]));
        fc_options_after_head(head, line, tmp, sizeof(tmp));
    } else if (o->o_var == &monlist_zzz_dummy) {
        static const char *const names[] = { "None", "Uniq", "All" };

        strnfmt(line, sizeof(line), "%-48s: %-4s ", o->o_desc, fc_tr(names[MIN(monlist_zzz, MONSTER_ZZZ_ALL)]));
        fc_options_after_head(head, line, tmp, sizeof(tmp));
    } else if (o->o_var == &always_repeat) {
        strnfmt(tmp, sizeof(tmp), "%-4s ", !always_repeat_count ? "off" : format("%d", always_repeat_count));
    } else if (o->o_var == &failed_item_retry_count_dummy) {
        strnfmt(tmp, sizeof(tmp), "%-4s ", !failed_item_retry_count ? "off" : format("%d", failed_item_retry_count));
    } else if (o->o_var == &single_pantheon) {
        strnfmt(line, sizeof(line), "%-48s: %d of %d", o->o_desc, pantheon_count, PANTHEON_MAX - 1);
        fc_options_after_head(head, line, tmp, sizeof(tmp));
    } else if (o->o_var == &guaranteed_pantheon) {
        if (pantheon_count == PANTHEON_MAX - 1) {
            my_strcat(tmp, fc_tr("All "), sizeof(tmp));
        } else if ((game_pantheon) && (game_pantheon < PANTHEON_MAX)) {
            strnfmt(tmp + strlen(tmp), sizeof(tmp) - strlen(tmp), "%s ", pant_list[game_pantheon].name);
        } else {
            my_strcat(tmp, fc_tr("None"), sizeof(tmp));
        }
    } else if (o->o_var == &always_small_levels) {
        strnfmt(line, sizeof(line), "%-48s: %s ", o->o_desc, fc_tr(lv_size_options[small_level_type]));
        fc_options_after_head(head, line, tmp, sizeof(tmp));
    } else if (o->o_var == &temp_file_policy_dummy) {
        const char *policy = (temp_file_policy == TEMP_FILE_POLICY_PROMPT) ? "ask"
            : ((temp_file_policy == TEMP_FILE_POLICY_FORCE) ? "wipe" : "auto");

        strnfmt(tmp, sizeof(tmp), "%-4s ", policy);
    } else {
        strnfmt(tmp, sizeof(tmp), "%-4s ", *o->o_var ? "yes" : "no");
    }
    fc_copy_str(buf, cap, tmp);
    return 1;
}

int fc_options_title_is_frox_page(const char *row0)
{
    static const char title[] = "New Options Introduced in Frox";
    const char *translated;

    if (!row0) {
        return 0;
    }
    if (strncmp(row0, title, sizeof(title) - 1) == 0) {
        return 1;
    }
    translated = fc_tr_arg(title);
    return (translated && (translated != title) && (strncmp(row0, translated, strlen(translated)) == 0)) ? 1 : 0;
}

int fc_world_map_overview_active(void)
{
    return world_map_overview_active ? 1 : 0;
}

int fc_read_map(int x0, int y0, int w, int h, fc_map_cell *out, int cap)
{
    fc_rng_snapshot snap;
    int result;

    fc_rng_save(&snap);
    fc_test_force_image();
    result = fc_read_map_raw(x0, y0, w, h, out, cap);
    fc_rng_finish(&snap, "fc_read_map");
    return result;
}

int fc_read_item_line(int what, int index, char *out, int cap)
{
    fc_rng_snapshot snap;
    int result;

    fc_rng_save(&snap);
    fc_test_force_image();
    result = fc_read_item_line_raw(what, index, out, cap);
    fc_rng_finish(&snap, "fc_read_item_line");
    return result;
}

int fc_read_visible_monster(int index, char *out, int cap)
{
    fc_rng_snapshot snap;
    int result;

    fc_rng_save(&snap);
    fc_test_force_image();
    result = fc_read_visible_monster_raw(index, out, cap);
    fc_rng_finish(&snap, "fc_read_visible_monster");
    return result;
}

/* ============================================================ 状態のアイコン（`GameFrame::status_effects`） */

/*
 * の Frox 版。
 *
 * ## 取り方
 * - **一時的な状態**は、原作の `prt_status()`（`xtra1.c`。static）が作る旗の配列から取る。
 *   `tools/targeting/overlay.py` が `prt_status()` の「長さを数える」の手前に 1 行足し、立っている旗の番号・
 *   名札（原作が画面へ出す字。`fc_bar()` を通した日本語か英語。実行中に書き換わる札を含む）を
 *   `fc_status_record()` へ渡す。記録するだけで、旗にも画面にも乱数にも触れない。
 * - **左の状態列**（`prt_effects()`。速さ・切り傷・朦朧・恐怖・空腹・変身・重い鎧ほか）は、同じ式を `p_ptr` から直に読む。
 *   名前の文字は原作が Term へ出す字を `fc_tr()`（Term_addstr のフック #1 と同じ引き）で引いたもの。
 * - **常時の効果**は C 画面（`py_info.c` の `_build_flags1` / `_build_flags2`）と同じ判定。装備は**鑑定済みの旗**
 *   （`obj_flags_display`）、種族・職業などの固有の旗は `player_flags()`。一時的な効果（画面の `#`）は載せない。
 *   耐性は `res_pct_known()` の百分率（C 画面と同じ）から一時的な分を引いた値で、**100% 未満でも耐性と同じ枠の盾**で出し、
 *   百分率を効果量に載せる。
 *
 * ## 種類（`StatusEffectKind`）
 * 0 良い・1 悪い・2 待っている・3 特殊・4 常時の良い・5 常時の悪い。
 */

#define FC_BAR_COUNT 195 /* xtra1.c の BAR_NO_AIR（194）+ 1。tools/frox/fc_status_table_check.py が原作と突き合わせる */

#define FC_SK_GOOD 0
#define FC_SK_BAD 1
#define FC_SK_WAIT 2
#define FC_SK_SPECIAL 3
#define FC_SK_PERM 4
#define FC_SK_PERM_BAD 5

typedef struct fc_bar_icon {
    const char *key;
    unsigned char kind;
} fc_bar_icon;

/*! `BAR_*` の番号で引く。鍵は `hd2d/ui/status_icon_table.inc` にあるものだけ。 */
static const fc_bar_icon fc_bar_icons[FC_BAR_COUNT] = {
    /*   0 */ { "tsuyoshi", FC_SK_SPECIAL }, /* BAR_TSUYOSHI */
    /*   1 */ { "hallucination", FC_SK_BAD }, /* BAR_HALLUCINATION */
    /*   2 */ { "blind", FC_SK_BAD }, /* BAR_BLINDNESS */
    /*   3 */ { "paralyzed", FC_SK_BAD }, /* BAR_PARALYZE */
    /*   4 */ { "confused", FC_SK_BAD }, /* BAR_CONFUSE */
    /*   5 */ { "poisoned", FC_SK_BAD }, /* BAR_POISONED */
    /*   6 */ { "afraid", FC_SK_BAD }, /* BAR_AFRAID */
    /*   7 */ { "levitate", FC_SK_GOOD }, /* BAR_LEVITATE */
    /*   8 */ { "reflect", FC_SK_GOOD }, /* BAR_REFLECTION */
    /*   9 */ { "passwall", FC_SK_GOOD }, /* BAR_PASSWALL */
    /*  10 */ { "wraith", FC_SK_GOOD }, /* BAR_WRAITH */
    /*  11 */ { "protevil", FC_SK_GOOD }, /* BAR_PROTEVIL */
    /*  12 */ { "kawarimi", FC_SK_GOOD }, /* BAR_KAWARIMI */
    /*  13 */ { "magicdef", FC_SK_GOOD }, /* BAR_MAGICDEFENSE */
    /*  14 */ { "expand", FC_SK_BAD }, /* BAR_EXPAND */
    /*  15 */ { "stoneskin", FC_SK_GOOD }, /* BAR_STONESKIN */
    /*  16 */ { "multishadow", FC_SK_GOOD }, /* BAR_MULTISHADOW */
    /*  17 */ { "resmagic", FC_SK_GOOD }, /* BAR_REGMAGIC */
    /*  18 */ { "ultimate", FC_SK_GOOD }, /* BAR_ULTIMATE */
    /*  19 */ { "invuln", FC_SK_GOOD }, /* BAR_INVULN */
    /*  20 */ { "imm_acid", FC_SK_GOOD }, /* BAR_IMMACID */
    /*  21 */ { "res_acid", FC_SK_GOOD }, /* BAR_RESACID */
    /*  22 */ { "imm_elec", FC_SK_GOOD }, /* BAR_IMMELEC */
    /*  23 */ { "res_elec", FC_SK_GOOD }, /* BAR_RESELEC */
    /*  24 */ { "imm_fire", FC_SK_GOOD }, /* BAR_IMMFIRE */
    /*  25 */ { "res_fire", FC_SK_GOOD }, /* BAR_RESFIRE */
    /*  26 */ { "imm_cold", FC_SK_GOOD }, /* BAR_IMMCOLD */
    /*  27 */ { "res_cold", FC_SK_GOOD }, /* BAR_RESCOLD */
    /*  28 */ { "res_pois", FC_SK_GOOD }, /* BAR_RESPOIS */
    /*  29 */ { "res_neth", FC_SK_GOOD }, /* BAR_RESNETH */
    /*  30 */ { "res_time", FC_SK_GOOD }, /* BAR_RESTIME */
    /*  31 */ { "dustrobe", FC_SK_GOOD }, /* BAR_DUSTROBE */
    /*  32 */ { "sh_fire", FC_SK_GOOD }, /* BAR_SHFIRE */
    /*  33 */ { "touki", FC_SK_GOOD }, /* BAR_TOUKI */
    /*  34 */ { "sh_holy", FC_SK_GOOD }, /* BAR_SHHOLY */
    /*  35 */ { "eyeeye", FC_SK_GOOD }, /* BAR_EYEEYE */
    /*  36 */ { "blessed", FC_SK_GOOD }, /* BAR_BLESSED */
    /*  37 */ { "hero", FC_SK_GOOD }, /* BAR_HEROISM */
    /*  38 */ { "berserk", FC_SK_GOOD }, /* BAR_BERSERK */
    /*  39 */ { "attk_fire", FC_SK_GOOD }, /* BAR_ATTKFIRE */
    /*  40 */ { "attk_cold", FC_SK_GOOD }, /* BAR_ATTKCOLD */
    /*  41 */ { "attk_elec", FC_SK_GOOD }, /* BAR_ATTKELEC */
    /*  42 */ { "attk_acid", FC_SK_GOOD }, /* BAR_ATTKACID */
    /*  43 */ { "attk_pois", FC_SK_GOOD }, /* BAR_ATTKPOIS */
    /*  44 */ { "attk_conf", FC_SK_GOOD }, /* BAR_ATTKCONF */
    /*  45 */ { "senseunseen", FC_SK_GOOD }, /* BAR_SENSEUNSEEN */
    /*  46 */ { "telepathy", FC_SK_GOOD }, /* BAR_TELEPATHY */
    /*  47 */ { "regeneration", FC_SK_GOOD }, /* BAR_REGENERATION */
    /*  48 */ { "infravision", FC_SK_GOOD }, /* BAR_INFRAVISION */
    /*  49 */ { "stealth", FC_SK_GOOD }, /* BAR_STEALTH */
    /*  50 */ { "superstealth", FC_SK_GOOD }, /* BAR_SUPERSTEALTH */
    /*  51 */ { "recall", FC_SK_WAIT }, /* BAR_RECALL */
    /*  52 */ { "alter", FC_SK_WAIT }, /* BAR_ALTER */
    /*  53 */ { "sh_cold", FC_SK_GOOD }, /* BAR_SHCOLD */
    /*  54 */ { "sh_elec", FC_SK_GOOD }, /* BAR_SHELEC */
    /*  55 */ { "sh_shadow", FC_SK_GOOD }, /* BAR_SHSHADOW */
    /*  56 */ { "might", FC_SK_GOOD }, /* BAR_MIGHT */
    /*  57 */ { "buildup", FC_SK_GOOD }, /* BAR_BUILD */
    /*  58 */ { "antimulti", FC_SK_GOOD }, /* BAR_ANTIMULTI */
    /*  59 */ { "antitele", FC_SK_GOOD }, /* BAR_ANTITELE */
    /*  60 */ { "antimagic", FC_SK_GOOD }, /* BAR_ANTIMAGIC */
    /*  61 */ { "patience", FC_SK_GOOD }, /* BAR_PATIENCE */
    /*  62 */ { "revenge", FC_SK_GOOD }, /* BAR_REVENGE */
    /*  63 */ { "runesword", FC_SK_GOOD }, /* BAR_RUNESWORD */
    /*  64 */ { "vampiric", FC_SK_GOOD }, /* BAR_VAMPILIC */
    /*  65 */ { "cure", FC_SK_GOOD }, /* BAR_CURE */
    /*  66 */ { "esp_evil", FC_SK_GOOD }, /* BAR_ESP_EVIL */
    /*  67 */ { "chaotic", FC_SK_GOOD }, /* BAR_CHAOTIC */
    /*  68 */ { "magicdef", FC_SK_GOOD }, /* BAR_BLOOD_SHIELD */
    /*  69 */ { "fc_blood_seek", FC_SK_GOOD }, /* BAR_BLOOD_SEEK */
    /*  70 */ { "revenge", FC_SK_GOOD }, /* BAR_BLOOD_REVENGE */
    /*  71 */ { "fc_blood_sight", FC_SK_GOOD }, /* BAR_BLOOD_SIGHT */
    /*  72 */ { "vampiric", FC_SK_GOOD }, /* BAR_BLOOD_FEAST */
    /*  73 */ { "no_magic", FC_SK_BAD }, /* BAR_NO_SPELLS */
    /*  74 */ { "haste", FC_SK_GOOD }, /* BAR_TIME_SPURT */
    /*  75 */ { "fc_sense_artifact", FC_SK_SPECIAL }, /* BAR_SPECIAL */
    /*  76 */ { "fc_duelist", FC_SK_SPECIAL }, /* BAR_DUELIST */
    /*  77 */ { "fc_shot_on_the_run", FC_SK_SPECIAL }, /* BAR_SHOT_ON_THE_RUN */
    /*  78 */ { "fc_rapid_shot", FC_SK_SPECIAL }, /* BAR_RAPID_SHOT */
    /*  79 */ { "fc_flying_dagger", FC_SK_SPECIAL }, /* BAR_FLYING_DAGGER */
    /*  80 */ { "fc_shadow_stance", FC_SK_SPECIAL }, /* BAR_SHADOW_STANCE */
    /*  81 */ { "fc_frenzy_stance", FC_SK_SPECIAL }, /* BAR_FRENZY_STANCE */
    /*  82 */ { "slay_orc", FC_SK_GOOD }, /* BAR_SLAY_ORC */
    /*  83 */ { "force_weapon", FC_SK_GOOD }, /* BAR_FORCE */
    /*  84 */ { "fc_defensive_stance", FC_SK_SPECIAL }, /* BAR_COMBAT_EXPERTISE */
    /*  85 */ { "fc_stone_bones", FC_SK_GOOD }, /* BAR_STONE_BONES */
    /*  86 */ { "fc_trade_blows", FC_SK_SPECIAL }, /* BAR_TRADE_BLOWS */
    /*  87 */ { "fc_power_attack", FC_SK_SPECIAL }, /* BAR_POWER_ATTACK */
    /*  88 */ { "fc_exposed", FC_SK_SPECIAL }, /* BAR_VICIOUS_STRIKE */
    /*  89 */ { "attk_fire", FC_SK_GOOD }, /* BAR_BURNING_BLADE */
    /*  90 */ { "attk_cold", FC_SK_GOOD }, /* BAR_ICE_BLADE */
    /*  91 */ { "attk_elec", FC_SK_GOOD }, /* BAR_THUNDER_BLADE */
    /*  92 */ { "vampiric", FC_SK_GOOD }, /* BAR_BLOOD_BLADE */
    /*  93 */ { "slay_evil", FC_SK_GOOD }, /* BAR_HOLY_BLADE */
    /*  94 */ { "fc_order_blade", FC_SK_SPECIAL }, /* BAR_ORDER_BLADE */
    /*  95 */ { "fc_wild_blade", FC_SK_SPECIAL }, /* BAR_WILD_BLADE */
    /*  96 */ { "fc_many_strike", FC_SK_SPECIAL }, /* BAR_MANY_STRIKE */
    /*  97 */ { "fc_piercing_strike", FC_SK_SPECIAL }, /* BAR_PIERCING_STRIKE */
    /*  98 */ { "fc_trip", FC_SK_SPECIAL }, /* BAR_TRIP */
    /*  99 */ { "fc_entrenched", FC_SK_SPECIAL }, /* BAR_ENTRENCHED */
    /* 100 */ { "fc_enlarge_weapon", FC_SK_GOOD }, /* BAR_ENLARGE_WEAPON */
    /* 101 */ { "fc_flurry", FC_SK_SPECIAL }, /* BAR_FLURRY_OF_BLOWS */
    /* 102 */ { "fc_greater_flurry", FC_SK_SPECIAL }, /* BAR_GREATER_FLURRY */
    /* 103 */ { "fc_undertaker_strength", FC_SK_SPECIAL }, /* BAR_STRENGTH_OF_THE_UNDERTAKER */
    /* 104 */ { "fc_stoicism", FC_SK_SPECIAL }, /* BAR_STOICISM */
    /* 105 */ { "fc_industrious_mortician", FC_SK_SPECIAL }, /* BAR_INDUSTRIOUS_MORTICIAN */
    /* 106 */ { "fc_shield_bash", FC_SK_SPECIAL }, /* BAR_SHIELD_BASH */
    /* 107 */ { "fc_bulwark", FC_SK_SPECIAL }, /* BAR_BULWARK */
    /* 108 */ { "fc_blood_rite", FC_SK_GOOD }, /* BAR_BLOOD_RITE */
    /* 109 */ { "fc_weapon_graft", FC_SK_GOOD }, /* BAR_WEAPON_GRAFT */
    /* 110 */ { "fc_psi_clarity", FC_SK_GOOD }, /* BAR_PSIONIC_CLARITY */
    /* 111 */ { "fc_psi_blending", FC_SK_GOOD }, /* BAR_PSIONIC_BLENDING */
    /* 112 */ { "fc_psi_shielding", FC_SK_GOOD }, /* BAR_PSIONIC_SHIELDING */
    /* 113 */ { "fc_psi_combat", FC_SK_GOOD }, /* BAR_PSIONIC_COMBAT */
    /* 114 */ { "fc_mental_fortress", FC_SK_GOOD }, /* BAR_MENTAL_FORTRESS */
    /* 115 */ { "fc_mindspring", FC_SK_GOOD }, /* BAR_MINDSPRING */
    /* 116 */ { "fc_psi_foresight", FC_SK_GOOD }, /* BAR_PSIONIC_FORESIGHT */
    /* 117 */ { "res_disen", FC_SK_GOOD }, /* BAR_RES_DISENCHANTMENT */
    /* 118 */ { "fc_spell_reaction", FC_SK_GOOD }, /* BAR_SPELL_REACTION */
    /* 119 */ { "res_curse", FC_SK_GOOD }, /* BAR_RESIST_CURSES */
    /* 120 */ { "fc_armor_of_fury", FC_SK_GOOD }, /* BAR_ARMOR_OF_FURY */
    /* 121 */ { "fc_spell_turning", FC_SK_GOOD }, /* BAR_SPELL_TURNING */
    /* 122 */ { "fc_fasting", FC_SK_GOOD }, /* BAR_FASTING */
    /* 123 */ { "sust_str", FC_SK_GOOD }, /* BAR_SUSTAIN_STR */
    /* 124 */ { "sust_int", FC_SK_GOOD }, /* BAR_SUSTAIN_INT */
    /* 125 */ { "sust_wis", FC_SK_GOOD }, /* BAR_SUSTAIN_WIS */
    /* 126 */ { "sust_dex", FC_SK_GOOD }, /* BAR_SUSTAIN_DEX */
    /* 127 */ { "sust_con", FC_SK_GOOD }, /* BAR_SUSTAIN_CON */
    /* 128 */ { "sust_chr", FC_SK_GOOD }, /* BAR_SUSTAIN_CHR */
    /* 129 */ { "hold_exp", FC_SK_GOOD }, /* BAR_HOLD_LIFE */
    /* 130 */ { "fc_transcendence", FC_SK_GOOD }, /* BAR_TRANSCENDENCE */
    /* 131 */ { "fc_the_world", FC_SK_GOOD }, /* BAR_THE_WORLD */
    /* 132 */ { "fc_dark_stalker", FC_SK_GOOD }, /* BAR_DARK_STALKER */
    /* 133 */ { "fc_nimble_dodge", FC_SK_GOOD }, /* BAR_NIMBLE_DODGE */
    /* 134 */ { "fc_stealthy_snipe", FC_SK_GOOD }, /* BAR_STEALTHY_SNIPE */
    /* 135 */ { "fc_block", FC_SK_SPECIAL }, /* BAR_BLOCK */
    /* 136 */ { "fc_mauler_drain", FC_SK_SPECIAL }, /* BAR_DRAIN */
    /* 137 */ { "earthquake", FC_SK_SPECIAL }, /* BAR_SHATTER */
    /* 138 */ { "fc_maul", FC_SK_SPECIAL }, /* BAR_MAUL */
    /* 139 */ { "fc_killing_spree", FC_SK_GOOD }, /* BAR_KILLING_SPREE */
    /* 140 */ { "tunnel", FC_SK_SPECIAL }, /* BAR_TUNNEL */
    /* 141 */ { "fc_quick_walk", FC_SK_GOOD }, /* BAR_QUICK_WALK */
    /* 142 */ { "fc_inven_prot", FC_SK_GOOD }, /* BAR_INVEN_PROT */
    /* 143 */ { "fc_inven_prot2", FC_SK_GOOD }, /* BAR_INVEN_PROT2 */
    /* 144 */ { "fc_weaponmastery", FC_SK_GOOD }, /* BAR_WEAPONMASTERY */
    /* 145 */ { "fc_device_power", FC_SK_GOOD }, /* BAR_DEVICE_POWER */
    /* 146 */ { "fc_splatter", FC_SK_SPECIAL }, /* BAR_SPLATTER */
    /* 147 */ { "fc_sh_time", FC_SK_GOOD }, /* BAR_SH_TIME */
    /* 148 */ { "fc_dbl_move", FC_SK_GOOD }, /* BAR_DBL_MOVE */
    /* 149 */ { "fc_readied_shot", FC_SK_SPECIAL }, /* BAR_READIED_SHOT */
    /* 150 */ { "fc_piercing_arrow", FC_SK_SPECIAL }, /* BAR_PIERCING_ARROW */
    /* 151 */ { "fc_rapid_reload", FC_SK_SPECIAL }, /* BAR_RAPID_RELOAD */
    /* 152 */ { "fc_exploding_bolt", FC_SK_SPECIAL }, /* BAR_EXPLODING_BOLT */
    /* 153 */ { "fc_overdraw", FC_SK_SPECIAL }, /* BAR_OVERDRAW */
    /* 154 */ { "fc_psi_archery", FC_SK_GOOD }, /* BAR_PSIONIC_ARCHERY */
    /* 155 */ { "stealth", FC_SK_SPECIAL }, /* BAR_MYSTIC_STEALTH */
    /* 156 */ { "haste", FC_SK_SPECIAL }, /* BAR_MYSTIC_FAST */
    /* 157 */ { "fc_mystic_retaliate", FC_SK_SPECIAL }, /* BAR_MYSTIC_RETALIATE */
    /* 158 */ { "fc_mystic_offense", FC_SK_SPECIAL }, /* BAR_MYSTIC_OFFENSE */
    /* 159 */ { "fc_mystic_defense", FC_SK_SPECIAL }, /* BAR_MYSTIC_DEFENSE */
    /* 160 */ { "fc_blink", FC_SK_GOOD }, /* BAR_BLINK */
    /* 161 */ { "fc_dtrap", FC_SK_SPECIAL }, /* BAR_DTRAP */
    /* 162 */ { "fc_dtrap", FC_SK_SPECIAL }, /* BAR_DTRAP_EDGE */
    /* 163 */ { "fc_vampire_light", FC_SK_BAD }, /* BAR_VAMPIRE_LIGHT */
    /* 164 */ { "fc_vampire_dark", FC_SK_GOOD }, /* BAR_VAMPIRE_DARK */
    /* 165 */ { "dustrobe", FC_SK_GOOD }, /* BAR_SH_SHARDS */
    /* 166 */ { "fc_sh_domination", FC_SK_GOOD }, /* BAR_SH_DOMINATION */
    /* 167 */ { "fc_psi_disruption", FC_SK_GOOD }, /* BAR_PSIONIC_DISRUPTION */
    /* 168 */ { "fc_psi_drain", FC_SK_GOOD }, /* BAR_PSIONIC_DRAIN */
    /* 169 */ { "fc_dragon_canter", FC_SK_GOOD }, /* BAR_DRAGON_CANTER */
    /* 170 */ { "fc_dragon_gallop", FC_SK_GOOD }, /* BAR_DRAGON_GALLOP */
    /* 171 */ { "fc_dragon_healing", FC_SK_GOOD }, /* BAR_DRAGON_HEALING */
    /* 172 */ { "fc_dragon_charge", FC_SK_GOOD }, /* BAR_DRAGON_HEROIC_CHARGE */
    /* 173 */ { "fc_hoarding", FC_SK_SPECIAL }, /* BAR_HOARDING */
    /* 174 */ { "fc_careful_aim", FC_SK_SPECIAL }, /* BAR_CAREFUL_AIM */
    /* 175 */ { "fc_spin", FC_SK_GOOD }, /* BAR_SPIN */
    /* 176 */ { "fc_grit", FC_SK_SPECIAL }, /* BAR_GRIT */
    /* 177 */ { "fc_grease", FC_SK_SPECIAL }, /* BAR_GREASE */
    /* 178 */ { "fc_glory", FC_SK_SPECIAL }, /* BAR_GLORY */
    /* 179 */ { "fc_upkeep", FC_SK_BAD }, /* BAR_UPKEEP */
    /* 180 */ { "fc_filibuster", FC_SK_SPECIAL }, /* BAR_FILIBUSTER */
    /* 181 */ { "fc_unwell", FC_SK_BAD }, /* BAR_UNWELL */
    /* 182 */ { "fc_force_field", FC_SK_GOOD }, /* BAR_FIELD */
    /* 183 */ { "slay_human", FC_SK_GOOD }, /* BAR_SLAY_HUMAN */
    /* 184 */ { "slay_giant", FC_SK_GOOD }, /* BAR_SLAY_GIANT */
    /* 185 */ { "slay_troll", FC_SK_GOOD }, /* BAR_SLAY_TROLL */
    /* 186 */ { "slay_dragon", FC_SK_GOOD }, /* BAR_SLAY_DRAGON */
    /* 187 */ { "slay_demon", FC_SK_GOOD }, /* BAR_SLAY_DEMON */
    /* 188 */ { "slay_undead", FC_SK_GOOD }, /* BAR_SLAY_UNDEAD */
    /* 189 */ { "slay_animal", FC_SK_GOOD }, /* BAR_SLAY_ANIMAL */
    /* 190 */ { "fc_wp_stun", FC_SK_GOOD }, /* BAR_WP_STUN */
    /* 191 */ { "vorpal", FC_SK_GOOD }, /* BAR_WP_SHARP */
    /* 192 */ { "fc_poet", FC_SK_SPECIAL }, /* BAR_POET */
    /* 193 */ { "fc_understand", FC_SK_GOOD }, /* BAR_UNDERSTAND */
    /* 194 */ { "fc_no_air", FC_SK_BAD }, /* BAR_NO_AIR */
};

int fc_status_bar_table_count(void)
{
    return FC_BAR_COUNT;
}

const char *fc_status_bar_table_key(int bar, int *kind)
{
    if ((bar < 0) || (bar >= FC_BAR_COUNT)) {
        return NULL;
    }
    if (kind) {
        *kind = (int)fc_bar_icons[bar].kind;
    }
    return fc_bar_icons[bar].key;
}

/* ---------------------------------------------------------------- 記録（overlay.py が足した 1 行の呼び先） */

static unsigned char fc_status_on[FC_BAR_COUNT];
static char fc_status_label[FC_BAR_COUNT][64];
static unsigned long fc_status_hash;
static int fc_status_overflow; /* 原作の `bar[]` が表より長い（原作が版上げで増えた）ときの印 */

/*!
 * @brief `prt_status()` が組んだ旗を覚える。`bar < 0` は「組み直しの始まり」（`attr` に原作の `bar[]` の行数）。
 * @details 名札は CP932 か英語。全角を割らないように切る。
 */
void fc_status_record(int bar, const char *label, int attr)
{
    (void)attr;
    if (bar < 0) {
        memset(fc_status_on, 0, sizeof(fc_status_on));
        fc_status_hash = 2166136261UL;
        fc_status_overflow = (attr > FC_BAR_COUNT) ? 1 : 0;
        return;
    }
    if (bar >= FC_BAR_COUNT) {
        fc_status_overflow = 1;
        return;
    }
    fc_status_on[bar] = 1;
    {
        int i = 0;
        const char *s = label ? label : "";

        while (s[i] && (i < (int)sizeof(fc_status_label[bar]) - 1)) {
            unsigned char c = (unsigned char)s[i];
            int lead = ((c >= 0x81) && (c <= 0x9F)) || ((c >= 0xE0) && (c <= 0xFC));

            if (lead && (!s[i + 1] || (i + 2 > (int)sizeof(fc_status_label[bar]) - 1))) {
                break;
            }
            fc_status_label[bar][i] = s[i];
            i++;
            if (lead) {
                fc_status_label[bar][i] = s[i];
                i++;
            }
        }
        fc_status_label[bar][i] = '\0';
    }
    fc_status_hash = (fc_status_hash ^ (unsigned long)(bar + 1)) * 16777619UL;
    /* 札の字そのものが変わる（決闘の標的・位相の盾など）ときも変わるように、札の頭を混ぜる。 */
    {
        const char *p = fc_status_label[bar];

        while (*p) {
            fc_status_hash = (fc_status_hash ^ (unsigned char)*p++) * 16777619UL;
        }
    }
}

/*! 旗の組が変わったら変わる値（`fc_change_digest` に混ぜる）。 */
unsigned long fc_status_stamp(void)
{
    return fc_status_hash;
}

/* ---------------------------------------------------------------- 常時の効果の表 */

#define FC_PM_NORMAL 0
#define FC_PM_SLAY 1 /* 皆殺し（`kill`）の旗を持つ行 */
#define FC_PM_CURSE 2 /* 装備の呪い（`_build_curse_flags`） */

/*! 効果量の種類（`fc_perm_amount`）。 */
#define FC_AM_NONE 0
#define FC_AM_REGEN 1 /* 急回復 %d%% */
#define FC_AM_SP_REGEN 2 /* SP 回復 %d%% */
#define FC_AM_LIFE 3 /* 生命力倍率 %+d%% */
#define FC_AM_MAGIC_SKILL 4 /* 魔道具技能 %+d%% */
#define FC_AM_SPELL_POWER 5 /* 呪文威力 %+d%% */
#define FC_AM_MAGIC_RES 6 /* 魔法耐性 %+d%% */
#define FC_AM_INFRA 7 /* 赤外線視 %d' */

typedef struct fc_perm_row {
    short flag; /*!< `OF_*`（呪いの行は -1） */
    short greater; /*!< 皆殺しの旗（無ければ -1） */
    unsigned char mode;
    unsigned char kind;
    unsigned char amount;
    const char *key;
    const char *name; /*!< C 画面の欄の名前（`fc_frame_label` の鍵。英語） */
} fc_perm_row;

#define FCP(flag, key, name) { flag, -1, FC_PM_NORMAL, FC_SK_PERM, FC_AM_NONE, key, name }
#define FCP_BAD(flag, key, name) { flag, -1, FC_PM_NORMAL, FC_SK_PERM_BAD, FC_AM_NONE, key, name }
#define FCP_AMT(flag, key, name, amt) { flag, -1, FC_PM_NORMAL, FC_SK_PERM, amt, key, name }
#define FCP_SLAY(flag, gr, key, name) { flag, gr, FC_PM_SLAY, FC_SK_PERM, FC_AM_NONE, key, name }

/*!
 * C 画面（`_build_flags1` / `_build_flags2`）の欄の順。耐性の欄（`RES_*`）は別に回す。
 * 描いていないアイコン（Slay Living・Dark Brand・Extra Might・Spell Cap・ESP Living・Rnd Tele）は載せない。
 */
static const fc_perm_row fc_perm_rows[] = {
    /* ---- オーラ ---- */
    FCP(OF_AURA_ELEC, "sh_elec", "Aura Elec"),
    FCP(OF_AURA_FIRE, "sh_fire", "Aura Fire"),
    FCP(OF_AURA_COLD, "sh_cold", "Aura Cold"),
    FCP(OF_AURA_SHARDS, "dustrobe", "Aura Shards"),
    FCP(OF_AURA_REVENGE, "eyeeye", "Revenge"),
    /* ---- スレイ・属性 ---- */
    FCP_SLAY(OF_SLAY_EVIL, OF_KILL_EVIL, "slay_evil", "Slay Evil"),
    FCP_SLAY(OF_SLAY_UNDEAD, OF_KILL_UNDEAD, "slay_undead", "Slay Undead"),
    FCP_SLAY(OF_SLAY_DEMON, OF_KILL_DEMON, "slay_demon", "Slay Demon"),
    FCP_SLAY(OF_SLAY_DRAGON, OF_KILL_DRAGON, "slay_dragon", "Slay Dragon"),
    FCP_SLAY(OF_SLAY_HUMAN, OF_KILL_HUMAN, "slay_human", "Slay Human"),
    FCP_SLAY(OF_SLAY_ANIMAL, OF_KILL_ANIMAL, "slay_animal", "Slay Animal"),
    FCP_SLAY(OF_SLAY_ORC, OF_KILL_ORC, "slay_orc", "Slay Orc"),
    FCP_SLAY(OF_SLAY_TROLL, OF_KILL_TROLL, "slay_troll", "Slay Troll"),
    FCP_SLAY(OF_SLAY_GIANT, OF_KILL_GIANT, "slay_giant", "Slay Giant"),
    FCP_SLAY(OF_SLAY_GOOD, OF_KILL_GOOD, "slay_good", "Slay Good"),
    FCP_SLAY(OF_BRAND_ACID, -1, "attk_acid", "Acid Brand"),
    FCP_SLAY(OF_BRAND_ELEC, -1, "attk_elec", "Elec Brand"),
    FCP_SLAY(OF_BRAND_FIRE, -1, "attk_fire", "Fire Brand"),
    FCP_SLAY(OF_BRAND_COLD, -1, "attk_cold", "Cold Brand"),
    FCP_SLAY(OF_BRAND_POIS, -1, "attk_pois", "Pois Brand"),
    FCP_SLAY(OF_BRAND_MANA, -1, "force_weapon", "Mana Brand"),
    FCP_SLAY(OF_VORPAL, OF_VORPAL2, "vorpal", "Sharpness"),
    FCP_SLAY(OF_STUN, -1, "fc_wp_stun", "Stunning"),
    FCP_SLAY(OF_IMPACT, -1, "earthquake", "Quake"),
    FCP_SLAY(OF_BRAND_VAMP, -1, "vampiric", "Vampiric"),
    FCP_SLAY(OF_BRAND_CHAOS, -1, "chaotic", "Chaotic"),
    FCP(OF_BLOWS, "blows", "Extra Blows"),
    FCP(OF_XTRA_SHOTS, "xtra_shots", "Extra Shots"),
    FCP(OF_BLESSED, "blessed", "Blessed"),
    FCP(OF_RIDING, "riding", "Riding"),
    FCP(OF_TUNNEL, "tunnel", "Digging"),
    FCP(OF_THROWING, "throw", "Throwing"),
    /* ---- 能力 ---- */
    FCP(OF_SPEED, "haste", "Speed"),
    FCP(OF_FREE_ACT, "free_act", "Free Act"),
    FCP(OF_SEE_INVIS, "senseunseen", "See Invis"),
    FCP(OF_WARNING, "warning", "Warning"),
    FCP(OF_SLOW_DIGEST, "slow_digest", "Slow Digest"),
    FCP_AMT(OF_REGEN, "regeneration", "Regenerate", FC_AM_REGEN),
    FCP_BAD(OF_SLOW_REGEN, "slow_regen", " HP Regen"),
    FCP_AMT(OF_REGEN_MANA, "fc_sp_regen", " SP Regen", FC_AM_SP_REGEN),
    FCP(OF_HOLD_LIFE, "hold_exp", "Hold Life"),
    FCP(OF_LEVITATION, "levitate", "Levitation"),
    FCP(OF_LITE, "lite", "Perm Lite"),
    FCP(OF_REFLECT, "reflect", "Reflection"),
    FCP(OF_NIGHT_VISION, "nightsight", "Nightvision"),
    FCP_AMT(OF_LIFE, "fc_life_mult", "Life Mult", FC_AM_LIFE),
    FCP(OF_DEC_MANA, "dec_mana", "Dec Mana"),
    FCP(OF_EASY_SPELL, "easy_spell", "Easy Spell"),
    FCP_BAD(OF_NO_MAGIC, "no_magic", "Anti Magic"),
    FCP_AMT(OF_MAGIC_MASTERY, "magic_mastery", "Magic Skill", FC_AM_MAGIC_SKILL),
    FCP_AMT(OF_SPELL_POWER, "fc_spell_power", "Spell Power", FC_AM_SPELL_POWER),
    FCP_AMT(OF_MAGIC_RESISTANCE, "resmagic", "Magic Res", FC_AM_MAGIC_RES),
    FCP_AMT(OF_INFRA, "infravision", "Infravision", FC_AM_INFRA),
    FCP(OF_STEALTH, "stealth", "Stealth"),
    FCP(OF_SEARCH, "search", "Searching"),
    /* ---- 能力値の維持 ---- */
    FCP(OF_SUST_STR, "sust_str", "Sust Str"),
    FCP(OF_SUST_INT, "sust_int", "Sust Int"),
    FCP(OF_SUST_WIS, "sust_wis", "Sust Wis"),
    FCP(OF_SUST_DEX, "sust_dex", "Sust Dex"),
    FCP(OF_SUST_CON, "sust_con", "Sust Con"),
    FCP(OF_SUST_CHR, "sust_chr", "Sust Chr"),
    /* ---- 感知 ---- */
    FCP(OF_TELEPATHY, "telepathy", "Telepathy"),
    FCP(OF_ESP_EVIL, "esp_evil", "ESP Evil"),
    FCP(OF_ESP_NONLIVING, "esp_nonliving", "ESP Nonliv"),
    FCP(OF_ESP_GOOD, "esp_good", "ESP Good"),
    FCP(OF_ESP_UNDEAD, "esp_undead", "ESP Undead"),
    FCP(OF_ESP_DEMON, "esp_demon", "ESP Demon"),
    FCP(OF_ESP_DRAGON, "esp_dragon", "ESP Dragon"),
    FCP(OF_ESP_HUMAN, "esp_human", "ESP Human"),
    FCP(OF_ESP_ANIMAL, "esp_animal", "ESP Animal"),
    FCP(OF_ESP_ORC, "esp_orc", "ESP Orc"),
    FCP(OF_ESP_TROLL, "esp_troll", "ESP Troll"),
    FCP(OF_ESP_GIANT, "esp_giant", "ESP Giant"),
    /* ---- 呪い ---- */
    { -1, -1, FC_PM_CURSE, FC_SK_PERM_BAD, FC_AM_NONE, "cursed", "Cursed" },
    FCP_BAD(OF_NO_TELE, "no_tele", "No Tele"),
    FCP_BAD(OF_DRAIN_EXP, "drain_exp", "Drain Exp"),
    FCP_BAD(OF_AGGRAVATE, "aggravate", "Aggravate"),
    FCP_BAD(OF_TY_CURSE, "ty_curse", "TY Curse"),
};

#define FC_PERM_ROWS ((int)(sizeof(fc_perm_rows) / sizeof(fc_perm_rows[0])))

/*! 耐性の欄（`RES_BEGIN`..`RES_END`）。免疫の鍵があるのは 5 つだけ。 */
typedef struct fc_res_row {
    const char *key;
    const char *imm_key; /*!< 100% のときの鍵（無ければ NULL ＝ key のまま、効果量は 100%） */
} fc_res_row;

static const fc_res_row fc_res_rows[RES_END] = {
    /* RES_ACID */ { "res_acid", "imm_acid" },
    /* RES_ELEC */ { "res_elec", "imm_elec" },
    /* RES_FIRE */ { "res_fire", "imm_fire" },
    /* RES_COLD */ { "res_cold", "imm_cold" },
    /* RES_POIS */ { "res_pois", NULL },
    /* RES_LITE */ { "res_lite", NULL },
    /* RES_DARK */ { "res_dark", "imm_dark" },
    /* RES_CONF */ { "res_conf", NULL },
    /* RES_NETHER */ { "res_neth", NULL },
    /* RES_NEXUS */ { "res_nexus", NULL },
    /* RES_SOUND */ { "res_sound", NULL },
    /* RES_SHARDS */ { "res_shard", NULL },
    /* RES_CHAOS */ { "res_chaos", NULL },
    /* RES_DISEN */ { "res_disen", NULL },
    /* RES_TIME */ { "res_time", NULL },
    /* RES_BLIND */ { "res_blind", NULL },
    /* RES_FEAR */ { "res_fear", NULL },
};

int fc_status_perm_row_count(void)
{
    return FC_PERM_ROWS;
}

const char *fc_status_perm_row_key(int row, int *kind)
{
    if ((row < 0) || (row >= FC_PERM_ROWS)) {
        return NULL;
    }
    if (kind) {
        *kind = (int)fc_perm_rows[row].kind;
    }
    return fc_perm_rows[row].key;
}

int fc_status_res_row_count(void)
{
    return (int)(sizeof(fc_res_rows) / sizeof(fc_res_rows[0]));
}

const char *fc_status_res_row_key(int row, const char **imm_key)
{
    if ((row < 0) || (row >= fc_status_res_row_count())) {
        return NULL;
    }
    if (imm_key) {
        *imm_key = fc_res_rows[row].imm_key;
    }
    return fc_res_rows[row].key;
}

/*!
 * C 画面の `_flagzilla_alloc` と同じ集め方。装備の枠ごとの**鑑定済みの旗**と、固有の旗（一時的でない分）。
 */
typedef struct fc_perm_ctx {
    u32b py[OF_ARRAY_SIZE];
    u32b obj[EQUIP_MAX + 1][OF_ARRAY_SIZE];
} fc_perm_ctx;

static void fc_perm_collect(fc_perm_ctx *c)
{
    int i;

    memset(c, 0, sizeof(*c));
    player_flags(c->py);
    for (i = 1; i <= equip_max(); i++) {
        object_type *o_ptr = equip_obj(i);

        if (!o_ptr) {
            continue;
        }
        obj_flags_display(o_ptr, c->obj[i]);
        remove_opposite_flags(c->obj[i]);
        switch (o_ptr->rune) {
        case RUNE_ABSORPTION:
            add_flag(c->obj[i], OF_MAGIC_RESISTANCE);
            break;
        case RUNE_SHADOW:
            if (object_is_body_armour(o_ptr) || (o_ptr->tval == TV_CLOAK)) {
                add_flag(c->obj[i], OF_STEALTH);
            }
            break;
        case RUNE_HASTE:
            if (o_ptr->tval == TV_BOOTS) {
                add_flag(c->obj[i], OF_SPEED);
            }
            break;
        }
    }
}

/*! 1 行の判定。装備のどこかか固有の旗が持っていれば真。`kill` の旗（皆殺し）があれば `*kill` に立てる。 */
static int fc_perm_row_has(const fc_perm_ctx *c, const fc_perm_row *r, int *kill)
{
    int i;
    int has = 0;

    *kill = 0;
    if (r->mode == FC_PM_CURSE) {
        for (i = 1; i <= equip_max(); i++) {
            object_type *o_ptr = equip_obj(i);

            if (o_ptr && (o_ptr->curse_flags & (OFC_CURSED | OFC_HEAVY_CURSE | OFC_PERMA_CURSE))) {
                return 1;
            }
        }
        return 0;
    }
    for (i = 1; i <= equip_max(); i++) {
        if ((r->greater >= 0) && have_flag(c->obj[i], r->greater)) {
            *kill = 1;
            has = 1;
        } else if (have_flag(c->obj[i], r->flag)) {
            has = 1;
        }
    }
    if ((r->greater >= 0) && have_flag(c->py, r->greater)) {
        *kill = 1;
        has = 1;
    } else if (have_flag(c->py, r->flag)) {
        has = 1;
    }
    return has;
}

/*! 一時的な分の数え（`calc_bonuses` が足している分。C 画面の百分率から引く）。 */
static int fc_res_temp_count(int which)
{
    int n = 0;

    if (p_ptr->ult_res) {
        n += 1; /* res_add_all() */
    }
    switch (which) {
    case RES_ACID:
        n += IS_OPPOSE_ACID() ? 1 : 0;
        n += (p_ptr->ele_immune && (p_ptr->special_defense & DEFENSE_ACID)) ? 100 : 0;
        break;
    case RES_ELEC:
        n += IS_OPPOSE_ELEC() ? 1 : 0;
        n += (p_ptr->ele_immune && (p_ptr->special_defense & DEFENSE_ELEC)) ? 100 : 0;
        break;
    case RES_FIRE:
        n += IS_OPPOSE_FIRE() ? 1 : 0;
        n += (p_ptr->ele_immune && (p_ptr->special_defense & DEFENSE_FIRE)) ? 100 : 0;
        break;
    case RES_COLD:
        n += IS_OPPOSE_COLD() ? 1 : 0;
        n += (p_ptr->ele_immune && (p_ptr->special_defense & DEFENSE_COLD)) ? 100 : 0;
        break;
    case RES_POIS:
        n += IS_OPPOSE_POIS() ? 1 : 0;
        break;
    case RES_NETHER:
        n += (p_ptr->tim_res_nether || IS_SPINNING()) ? 1 : 0;
        break;
    case RES_DISEN:
        n += p_ptr->tim_res_disenchantment ? 1 : 0;
        break;
    case RES_TIME:
        n += p_ptr->tim_res_time ? 1 : 0;
        break;
    case RES_BLIND:
    case RES_CONF:
        n += p_ptr->magicdef ? 1 : 0;
        break;
    case RES_DARK:
        n += IS_WRAITH() ? 100 : 0;
        break;
    case RES_FEAR:
        n += p_ptr->invuln ? 100 : 0;
        n += p_ptr->shero ? 100 : 0;
        n += (p_ptr->hero || p_ptr->shero) ? 1 : 0;
        break;
    }
    return n;
}

/* ---------------------------------------------------------------- 組み立て */

static int fc_status_push(fc_status_effect *out, int *n, int cap, const char *key, const char *label_cp932,
    const char *amount, int kind)
{
    int i;
    fc_status_effect *e;

    if (!key || !key[0]) {
        return 0;
    }
    for (i = 0; i < *n; i++) { /* 同じ鍵・同じ種類の重複は 1 つにする（DTRAP と DTRAP_EDGE など） */
        if ((out[i].kind == kind) && (strcmp(out[i].key, key) == 0)) {
            return 0;
        }
    }
    if (*n >= cap) {
        return 0;
    }
    e = &out[*n];
    memset(e, 0, sizeof(*e));
    strncpy(e->key, key, sizeof(e->key) - 1);
    if (label_cp932) {
        strncpy(e->label, label_cp932, sizeof(e->label) - 1);
    }
    if (amount) {
        strncpy(e->amount, amount, sizeof(e->amount) - 1);
    }
    e->kind = kind;
    (*n)++;
    return 1;
}

/*! 欄の名前を引いて、行末の空白を落とす（`fc_frame_label` は桁をそろえた字を返す）。 */
static const char *fc_status_frame_label(const char *name, char *buf, int cap)
{
    const char *s = fc_frame_label(name);
    int len;

    if (!s) {
        s = name;
    }
    len = (int)strlen(s);
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(buf, s, (size_t)len);
    buf[len] = '\0';
    while ((len > 0) && (buf[len - 1] == ' ')) {
        buf[--len] = '\0';
    }
    {
        int lead = 0;

        while (buf[lead] == ' ') { /* 「 HP Regen」のように頭に空白のある名もある */
            lead++;
        }
        if (lead > 0) {
            memmove(buf, buf + lead, strlen(buf + lead) + 1);
        }
    }
    return buf;
}

/*!
 * Term へ出す字と同じ引き（`Term_addstr` のフック #1 は `fc_tr`）。一般のカタログに無い字は、状態の札の表（`fc_bar`）で
 * 引き直す（`Blind` などは札の表にだけ訳がある）。どちらも無ければ原文のまま。
 */
static const char *fc_status_tr(const char *s)
{
    const char *t = fc_tr(s);

    if ((t == s) || (strcmp(t, s) == 0)) {
        t = fc_bar(s);
    }
    return t;
}

/*! 「Fast (+%d)」の書式の訳から、数字の前の名前だけを取る。 */
static void fc_status_speed_label(const char *english_fmt, char *buf, int cap)
{
    const char *s = fc_tr_fmt(english_fmt);
    int len = 0;

    while (s[len] && (len < cap - 1)) {
        if ((s[len] == '(') || ((s[len] == ' ') && (s[len + 1] == '('))) {
            break;
        }
        buf[len] = s[len];
        len++;
    }
    while ((len > 0) && (buf[len - 1] == ' ')) {
        len--;
    }
    buf[len] = '\0';
}

#define FC_STATUS_TEMP_END 0

int fc_read_status_effects(fc_status_effect *out, int cap)
{
    int n = 0;
    int i;
    char amt[24];
    char lab[64];

    if (!out || (cap <= 0) || !character_generated || !p_ptr || p_ptr->is_dead) {
        return 0;
    }
    if (fc_status_overflow) { /* 原作の `bar[]` が表より長い。版上げで BAR_* が増えたので表を足す（一度だけ知らせる） */
        static int warned;

        if (!warned) {
            warned = 1;
            fprintf(stderr, "[frox] status icons: the original bar[] is longer than FC_BAR_COUNT (%d); extend fc_bar_icons[]\n",
                FC_BAR_COUNT);
        }
    }

    /* (1) 速さ。`prt_speed` と同じ式（探索中の減速は見かけから外す）。 */
    {
        int sp = p_ptr->pspeed;

        if ((p_ptr->action == ACTION_SEARCH) && !IS_LIGHT_SPEED()) {
            sp += 10;
        }
        if (sp > 110) {
            if (effective_speed) {
                sprintf(amt, "%d.%dx", SPEED_TO_ENERGY(sp) / 10, SPEED_TO_ENERGY(sp) % 10);
            } else {
                sprintf(amt, "+%d", sp - 110);
            }
            fc_status_speed_label("Fast (+%d)", lab, (int)sizeof(lab));
            fc_status_push(out, &n, cap, "haste", p_ptr->riding ? fc_status_tr("Riding") : lab, amt, FC_SK_GOOD);
        } else if (sp < 110) {
            if (effective_speed) {
                sprintf(amt, "%d.%dx", SPEED_TO_ENERGY(sp) / 10, SPEED_TO_ENERGY(sp) % 10);
            } else {
                sprintf(amt, "-%d", 110 - sp);
            }
            fc_status_speed_label("Slow (-%d)", lab, (int)sizeof(lab));
            fc_status_push(out, &n, cap, "slow", p_ptr->riding ? fc_status_tr("Riding") : lab, amt, FC_SK_BAD);
        }
    }

    /* (2) 状態の旗。`BAR_*` の小さい順＝原作の最下段の並び。 */
    for (i = 0; i < FC_BAR_COUNT; i++) {
        if (fc_status_on[i]) {
            fc_status_push(out, &n, cap, fc_bar_icons[i].key, fc_status_label[i], NULL, (int)fc_bar_icons[i].kind);
        }
    }

    /* (3) 左の状態列（`prt_effects`）の項目。原作が Term へ出す字を、同じ引きで名前にする。 */
    if (p_ptr->cursed & 0x0000000F) {
        const char *s = (p_ptr->cursed & OFC_PERMA_CURSE) ? "*CURSED*" : ((p_ptr->cursed & OFC_HEAVY_CURSE) ? "CURSED" : "Cursed");

        fc_status_push(out, &n, cap, "cursed", fc_status_tr(s), NULL, FC_SK_PERM_BAD);
    }
    if (p_ptr->mimic_form != MIMIC_NONE) {
        char buf[MAX_NLEN];
        race_t *race_ptr = get_race();

        sprintf(buf, "[%.10s]", race_ptr->name);
        fc_status_push(out, &n, cap, "fc_form", fc_status_tr(buf), NULL, FC_SK_SPECIAL);
    } else if (prace_is_(RACE_WEREWOLF)) {
        fc_status_push(out, &n, cap, "fc_werebeast", fc_status_tr(werewolf_in_human_form() ? "[Human]" : "[Wolf]"), NULL,
            FC_SK_SPECIAL);
    } else if (prace_is_(RACE_BEORNING)) {
        fc_status_push(out, &n, cap, "fc_werebeast", fc_status_tr(beorning_is_(BEORNING_FORM_HUMAN) ? "[Human]" : "[Bear]"), NULL,
            FC_SK_SPECIAL);
    }
    if (monk_armour_aux) {
        fc_status_push(out, &n, cap, "fc_heavy_armor", fc_status_tr("Heavy Armor"), NULL, FC_SK_BAD);
    }
    if (p_ptr->cumber_glove) {
        fc_status_push(out, &n, cap, "fc_cumber_glove", fc_status_tr("Encumbrance"), NULL, FC_SK_BAD);
    }
    for (i = 0; i < MAX_HANDS; i++) {
        if (p_ptr->weapon_info[i].heavy_wield) {
            fc_status_push(out, &n, cap, "fc_heavy_wield", fc_status_tr("Heavy Wield"), NULL, FC_SK_BAD);
            break;
        }
    }
    for (i = 0; i < MAX_HANDS; i++) {
        if (p_ptr->weapon_info[i].icky_wield) {
            fc_status_push(out, &n, cap, "fc_icky_wield", fc_status_tr("Icky Wield"), NULL, FC_SK_BAD);
            break;
        }
    }
    if ((prace_is_(RACE_MON_MUMMY) || player_is_ninja) && (p_ptr->cur_lite > 0)) {
        fc_status_push(out, &n, cap, "fc_icky_light", fc_status_tr("Icky Light"), NULL, FC_SK_BAD);
    }
    if (p_ptr->shooter_info.heavy_shoot) {
        fc_status_push(out, &n, cap, "fc_heavy_shoot", fc_status_tr("Heavy Shoot"), NULL, FC_SK_BAD);
    }
    if (p_ptr->cut) {
        cut_info_t cut = cut_info(p_ptr->cut);

        if (cut.level != CUT_NONE) {
            fc_status_push(out, &n, cap, "cut", fc_status_tr(cut.desc), NULL, FC_SK_BAD);
        }
    }
    if (p_ptr->stun) {
        stun_info_t st = stun_info(p_ptr->stun);

        if (st.level != STUN_NONE) {
            fc_status_push(out, &n, cap, "stun", fc_status_tr(st.name), NULL, FC_SK_BAD);
        }
    }
    if (p_ptr->afraid) {
        const char *s = NULL;

        switch (fear_level_p()) {
        case FEAR_UNEASY:
            s = "Uneasy";
            break;
        case FEAR_NERVOUS:
            s = "Nervous";
            break;
        case FEAR_SCARED:
            s = "Scared";
            break;
        case FEAR_TERRIFIED:
            s = "Terrified";
            break;
        case FEAR_PETRIFIED:
            s = "Petrified";
            break;
        }
        if (s) {
            fc_status_push(out, &n, cap, "afraid", fc_status_tr(s), NULL, FC_SK_BAD);
        }
    }
    if (p_ptr->image) {
        fc_status_push(out, &n, cap, "hallucination", fc_status_tr("Hallucinate"), NULL, FC_SK_BAD);
    }
    if (p_ptr->blind) {
        fc_status_push(out, &n, cap, "blind", fc_status_tr("Blind"), NULL, FC_SK_BAD);
    }
    if (p_ptr->paralyzed) {
        fc_status_push(out, &n, cap, "paralyzed", fc_status_tr("Paralyzed"), NULL, FC_SK_BAD);
    }
    if (p_ptr->confused) {
        fc_status_push(out, &n, cap, "confused", fc_status_tr("Confused"), NULL, FC_SK_BAD);
    }
    if (p_ptr->poisoned) {
        sprintf(amt, "%d", (int)p_ptr->poisoned); /* 原作の「Poison:」の隣の数 */
        fc_status_push(out, &n, cap, "poisoned", fc_bar("Poisoned"), amt, FC_SK_BAD);
    }
    /* 空腹。悪いのは空腹・衰弱・食過ぎ（`Full` は出さない） */
    if ((p_ptr->food >= PY_FOOD_FULL) || (p_ptr->food < PY_FOOD_ALERT)) {
        const char *s = NULL;

        if (p_ptr->food < PY_FOOD_FAINT) {
            s = "Faint";
        } else if (p_ptr->food < PY_FOOD_WEAK) {
            s = "Weak";
        } else if (p_ptr->food < PY_FOOD_ALERT) {
            s = "Hungry";
        } else if (p_ptr->food >= PY_FOOD_MAX) {
            s = "Gorged";
        }
        if (s) {
            fc_status_push(out, &n, cap, "hungry", fc_status_tr(s), NULL, FC_SK_BAD);
        }
    }
    if (p_ptr->action == ACTION_QUICK_WALK) {
        fc_status_push(out, &n, cap, "fc_no_sp_regen", fc_status_tr("No SP Regen"), NULL, FC_SK_BAD);
    }
    if (p_ptr->action == ACTION_KATA) { /* 剣術家の特殊な構え（原作は `prt_state` が構えの名前を出す） */
        int k;

        for (k = 0; k < MAX_KATA; k++) {
            if (p_ptr->special_defense & (KATA_IAI << k)) {
                break;
            }
        }
        if (k < MAX_KATA) {
            fc_status_push(out, &n, cap, "fc_kata", fc_status_tr(kata_shurui[k].desc), NULL, FC_SK_SPECIAL);
        }
    }

    /* (4) 常時の効果（C 画面の耐性・能力の表。鑑定済みの分だけ）。 */
    {
        static fc_perm_ctx ctx; /* ゲームのスレッドは 1 本。大きいので静的に持つ */
        int which;

        fc_perm_collect(&ctx);
        /* 耐性。百分率は C 画面と同じ（`res_pct_known`）から、一時的な分を引いたもの。 */
        for (which = RES_BEGIN; which < RES_END; which++) {
            int count = res_ct_known(which) - fc_res_temp_count(which);
            int pct = res_pct_aux(which, count);
            char name[48];

            if (pct <= 0) {
                continue;
            }
            fc_status_frame_label(res_name(which), name, (int)sizeof(name));
            if ((pct >= 100) && fc_res_rows[which].imm_key) {
                fc_status_push(out, &n, cap, fc_res_rows[which].imm_key, name, NULL, FC_SK_PERM);
            } else {
                if ((which == RES_FEAR) && (pct < 100)) {
                    sprintf(amt, "%dx", count); /* 原作は恐怖だけ回数で見せる */
                } else {
                    sprintf(amt, "%d%%", pct);
                }
                fc_status_push(out, &n, cap, fc_res_rows[which].key, name, amt, FC_SK_PERM);
            }
        }
        for (i = 0; i < FC_PERM_ROWS; i++) {
            const fc_perm_row *r = &fc_perm_rows[i];
            int kill = 0;
            char name[48];

            if (!fc_perm_row_has(&ctx, r, &kill)) {
                continue;
            }
            amt[0] = '\0';
            switch (r->amount) {
            case FC_AM_REGEN:
                sprintf(amt, "%d%%", (int)p_ptr->regen);
                break;
            case FC_AM_SP_REGEN: {
                s32b regen = (s32b)p_ptr->regen * py_food_regen();

                if (p_ptr->msp) {
                    sprintf(amt, "%d%%",
                        (int)(((p_ptr->pclass == CLASS_RUNE_KNIGHT) || (p_ptr->pclass == CLASS_RAGE_MAGE) || mimic_no_regen())
                                ? 0
                                : (p_ptr->mana_regen ? regen * 2 : regen) / PY_REGEN_NORMAL));
                }
                break;
            }
            case FC_AM_LIFE: {
                int d = p_ptr->life - adj_con_mhp[p_ptr->stat_ind[A_CON]];

                if (d != 0) {
                    sprintf(amt, "%+d%%", d);
                }
                break;
            }
            case FC_AM_MAGIC_SKILL:
                if (p_ptr->device_power) {
                    sprintf(amt, "%+d%%", device_power_aux(100, p_ptr->device_power) - 100);
                }
                break;
            case FC_AM_SPELL_POWER:
                sprintf(amt, "%+d%%", spell_power_aux(100, p_ptr->spell_power) - 100);
                break;
            case FC_AM_MAGIC_RES:
                sprintf(amt, "%+d%%", (int)p_ptr->magic_resistance);
                break;
            case FC_AM_INFRA:
                sprintf(amt, "%d'", (int)p_ptr->see_infra * 10);
                break;
            }
            fc_status_frame_label(r->name, name, (int)sizeof(name));
            fc_status_push(out, &n, cap, r->key, name, amt, r->kind);
            (void)kill;
        }
    }
    return n;
}

/*!
 * @brief 状態のアイコンの表の自己検査（キャラクター不要）。
 * @details 表の鍵が空でない・長さの上限内・種類が 0..3（一時）／4・5（常時）、常時の効果の行の鍵・名前・旗が範囲内、
 * 耐性の行の数が `RES_END` と同じ、を見る。画面側の表（`status_icon_table.inc`）との突き合わせは、
 * 環境変数 `HENGBAND_STATUS_ICON_TABLE` にそのファイルのパスがあるときだけここで行う
 * （無ければ `*checked` を 0 のまま返し、突き合わせは `tools/frox/fc_status_table_check.py` が行う）。
 * @return 外れた件数（0 なら全部合っている）
 */
int fc_status_selftest(char *err, int cap, int *checked)
{
    int fails = 0;
    int i;
    const char *path = getenv("HENGBAND_STATUS_ICON_TABLE");
    char *table = NULL;
    long len = 0;
    static const char *const direct[] = { "haste", "slow", "stun", "cut", "hungry", "afraid", "hallucination", "blind",
        "paralyzed", "confused", "poisoned", "cursed", "fc_form", "fc_werebeast", "fc_heavy_armor", "fc_cumber_glove",
        "fc_heavy_wield", "fc_icky_wield", "fc_icky_light", "fc_heavy_shoot", "fc_no_sp_regen" };

    if (err && (cap > 0)) {
        err[0] = '\0';
    }
    if (checked) {
        *checked = 0;
    }
#define FC_ST_FAIL(...) \
    do { \
        if ((fails == 0) && err && (cap > 1)) { \
            sprintf(err, __VA_ARGS__); \
            err[cap - 1] = '\0'; \
        } \
        fails++; \
    } while (0)
#define FC_ST_KEY(KEY, WHAT) \
    do { \
        if (table) { \
            char needle_[64]; \
            sprintf(needle_, "{ \"%s\",", (KEY)); \
            if (!strstr(table, needle_)) { \
                FC_ST_FAIL("%s: key '%s' is not in the icon table", (WHAT), (KEY)); \
            } \
            if (checked) { \
                (*checked)++; \
            } \
        } \
    } while (0)

    if (path && path[0]) {
        FILE *fp = fopen(path, "rb");

        if (fp) {
            fseek(fp, 0, SEEK_END);
            len = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if ((len > 0) && (len < 4 * 1024 * 1024)) {
                table = (char *)malloc((size_t)len + 1);
                if (table) {
                    len = (long)fread(table, 1, (size_t)len, fp);
                    table[len] = '\0';
                }
            }
            fclose(fp);
        }
        if (!table) {
            FC_ST_FAIL("icon table unreadable: %.100s", path);
        }
    }

    for (i = 0; i < FC_BAR_COUNT; i++) {
        const fc_bar_icon *b = &fc_bar_icons[i];

        if (!b->key || !b->key[0] || (strlen(b->key) >= sizeof(((fc_status_effect *)0)->key))) {
            FC_ST_FAIL("bar %d: bad key", i);
            continue;
        }
        if (b->kind > FC_SK_SPECIAL) {
            FC_ST_FAIL("bar %d (%s): kind %d is not a temporary kind", i, b->key, (int)b->kind);
        }
        FC_ST_KEY(b->key, "bar");
    }
    for (i = 0; i < FC_PERM_ROWS; i++) {
        const fc_perm_row *r = &fc_perm_rows[i];

        if (!r->key || !r->key[0] || !r->name) {
            FC_ST_FAIL("perm row %d: missing key or name", i);
            continue;
        }
        if ((r->kind != FC_SK_PERM) && (r->kind != FC_SK_PERM_BAD)) {
            FC_ST_FAIL("perm row %d (%s): kind %d is not permanent", i, r->key, (int)r->kind);
        }
        if ((r->mode != FC_PM_CURSE) && ((r->flag < 0) || (r->flag >= OF_COUNT))) {
            FC_ST_FAIL("perm row %d (%s): flag %d out of range", i, r->key, (int)r->flag);
        }
        FC_ST_KEY(r->key, "perm");
    }
    if (fc_status_res_row_count() != RES_END) {
        FC_ST_FAIL("res rows %d != RES_END %d", fc_status_res_row_count(), (int)RES_END);
    }
    for (i = 0; i < fc_status_res_row_count(); i++) {
        FC_ST_KEY(fc_res_rows[i].key, "res");
        if (fc_res_rows[i].imm_key) {
            FC_ST_KEY(fc_res_rows[i].imm_key, "res imm");
        }
    }
    for (i = 0; i < (int)(sizeof(direct) / sizeof(direct[0])); i++) {
        FC_ST_KEY(direct[i], "direct");
    }
    free(table);
#undef FC_ST_KEY
#undef FC_ST_FAIL
    return fails;
}
