/*!
 * @file an_ui_shim.c
 * @brief `an_ui_shim.h` の実体。
 *
 * `game-input.c` の `get_check` /
 * `get_quantity()` / `get_string()` は、対応する `*_hook` が立っていればそれを
 * 呼ぶだけの薄い関数である（`game-input.c:57-96`）。`textui_init()`
 * （`ui-input.c:1728-1730`）が既定の実装（`textui_get_string` ほか）を差すので、
 * **その後で**元を控えてから差し替える——「差し替えるのではなく包む」
 * （設計 §4 の脚注と同じ考え方）。
 *
 * `textui_get_quantity()` は内部で `get_string()`（＝ `get_string_hook`）を
 * 呼ぶ（`ui-input.c:1221-1253` の `get_string(prompt, buf, 7)`）。つまり数量入力の
 * 間は**この 2 つのフックが入れ子で両方とも走る**。数量が優先なので、数量が
 * 立っている間は文字入力側の状態を立てない（`an_wrapped_get_string` の分岐）。
 */

#include "angband.h"

#include "game-input.h"

#include "an_ui_shim.h"
#include "an_lang_c.h" /* : 問いの字（フック #15 と同じ字） */

#include <string.h>

static bool (*an_orig_get_check)(const char *prompt) = NULL;
static int (*an_orig_get_quantity)(const char *prompt, int max) = NULL;
static bool (*an_orig_get_string)(const char *prompt, char *buf, size_t len) = NULL;

static an_check_state an_check;
static an_quantity_state an_quantity;
static an_text_state an_text;

static void an_copy_prompt(char *dest, size_t cap, const char *src)
{
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, cap - 1);
    dest[cap - 1] = '\0';
}

static bool an_wrapped_get_check(const char *prompt)
{
    bool result;
    char built_prompt[80];

    an_check.active = 1;
    /*
     * 見直しの指摘 11。以前はここで `prompt`（例: "Are you sure? "）を
     * そのまま控えていたが、"[y/n] " を足すのは**このあと呼ぶ**
     * `an_orig_get_check()`（＝原作の `textui_get_check()`。`ui-input.c`）の
     * 内部（`strnfmt(buf, 78, "%.70s[y/n] ", prompt)`）——つまり `an_check.prompt`
     * には "[y/n]" が一度も入らず、`an_main.cpp` の `text.find("[y/n]")` は
     * 常に外れていた（`an_ui_shim.h` の「実際に Term へ出した文言」という
     * 註記も誤り）。原作と同じ書式でここでも組む。
     */
    /*
     * 日本語（・フック #15）: 原作の `textui_get_check` は
     * 問いの字を `an_tr()` で訳してから `[y/n]` を足す。ここで控える字も同じにする。
     * `%.70s` はバイトで切ると多バイト字を割るので、桁で切る `strnfmt()`（`vstrnfmt()` の
     * フック #3）で組む。英語では `snprintf()` と同じ字になる。
     */
    strnfmt(built_prompt, sizeof(built_prompt), "%.70s[y/n] ", prompt ? an_tr(prompt) : "");
    an_copy_prompt(an_check.prompt, sizeof(an_check.prompt), built_prompt);

    result = an_orig_get_check ? an_orig_get_check(prompt) : false;

    an_check.active = 0;
    return result;
}

/*!
 * AH-59: 持ち物の窓の「全部落とす」の予約を 1 回だけ消費する（実体は `an_main.cpp`）。予約は受信スレッドが
 * `d`（アイテムを置く）のキーと一緒に立て、ゲームスレッドの命令待ちで何も読まれなければ捨てる。
 */
extern int an_drop_all_take(void);

static int an_wrapped_get_quantity(const char *prompt, int max)
{
    int result;
    char built_prompt[80];

    /*
     * AH-59: 原作の右クリックメニューの「Drop All」は数量を聞かずに山を全部落とす
     * （`context_menu_object()` の `MENU_VALUE_DROP_ALL`）。持ち物の窓の「全部置く」は `d` の後で
     * 対象を選ぶまでは普通の置く命令と同じで、数量の問い（`do_cmd_drop()` の `cmd_get_quantity()`。
     * 問いの字は NULL）だけを山の全部で答える。問いは出さない（数量の窓を立てない）。
     */
    if ((prompt == NULL) && (max > 1) && an_drop_all_take()) {
        return max;
    }

    an_quantity.active = 1;
    prompt = an_tr(prompt); /* 日本語（設計 §8）。NULL はそのまま NULL。 */
    if (!prompt) {
        /*
         * 見直しの指摘 10。`prompt` が NULL のときは、原作の
         * `textui_get_quantity()`（`ui-input.c`）がここで初めて
         * "Quantity (0-%d, *=all): " を組み立てる——このフックは
         * `an_orig_get_quantity()`（＝その `textui_get_quantity`）を呼ぶ
         * **前**に `an_quantity.prompt` を控えるので、そのままでは
         * 空文字のまま画面へ送られていた。原作と同じ書式でここでも組む。
         */
        /* 日本語: 原作は同じ書式を `strnfmt()`（書式の段で訳す。フック #2）で組む。 */
        strnfmt(built_prompt, sizeof(built_prompt), "Quantity (0-%d, *=all): ", max);
        prompt = built_prompt;
    }
    an_copy_prompt(an_quantity.prompt, sizeof(an_quantity.prompt), prompt);
    an_quantity.min = 0;
    an_quantity.max = max;

    result = an_orig_get_quantity ? an_orig_get_quantity(prompt, max) : 0;

    an_quantity.active = 0;
    return result;
}

static bool an_wrapped_get_string(const char *prompt, char *buf, size_t len)
{
    bool result;
    const int nested_in_quantity = an_quantity.active; /* 上の註記のとおり。 */

    const int is_rest = (prompt != NULL) && (strncmp(prompt, "Rest (0-9999", 12) == 0); /* 訳す前の原文で判じる（AH-48）。 */
    prompt = an_tr(prompt); /* 日本語（設計 §8）: 問いの字を画面へ出す字にそろえる。 */
    if (!nested_in_quantity) {
        an_text.active = 1;
        an_text.rest = is_rest;
        an_copy_prompt(an_text.prompt, sizeof(an_text.prompt), prompt);
    }

    result = an_orig_get_string ? an_orig_get_string(prompt, buf, len) : false;

    if (!nested_in_quantity) {
        an_text.active = 0;
        an_text.rest = 0;
    }
    return result;
}

void an_install_ui_hooks(void)
{
    memset(&an_check, 0, sizeof(an_check));
    memset(&an_quantity, 0, sizeof(an_quantity));
    memset(&an_text, 0, sizeof(an_text));

    an_orig_get_check = get_check_hook;
    an_orig_get_quantity = get_quantity_hook;
    an_orig_get_string = get_string_hook;

    get_check_hook = an_wrapped_get_check;
    get_quantity_hook = an_wrapped_get_quantity;
    get_string_hook = an_wrapped_get_string;
}

int an_get_check_state(an_check_state *out)
{
    if (!out) {
        return 0;
    }
    *out = an_check;
    return an_check.active;
}

int an_get_quantity_state(an_quantity_state *out)
{
    if (!out) {
        return 0;
    }
    *out = an_quantity;
    return an_quantity.active;
}

int an_get_text_state(an_text_state *out)
{
    if (!out) {
        return 0;
    }
    *out = an_text;
    return an_text.active;
}
