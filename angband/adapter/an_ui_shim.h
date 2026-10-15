/*!
 * @file an_ui_shim.h
 * @brief `get_check_hook` / `get_quantity_hook` / `get_string_hook` を包む純 C ABI
 * （設計 §6.3「確認」「数値入力」「文字入力」）。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief 3 つのフックを包む。**`textui_init()` の後**に呼ぶこと
 * （それより前は `get_check_hook` 等がまだ NULL で、包む元が無い）。
 */
void an_install_ui_hooks(void);

typedef struct an_check_state {
    int active;
    /*!< `"[y/n] "` を含む文言。原作 `textui_get_check()` が Term へ出すのと同じ
     * 書式（`"%.70s[y/n] "`）で、このフック自身が組む（見直しの指摘 11。以前の
     * 註記「実際に Term へ出した文言」は誤り——`get_check_hook` が受け取る
     * `prompt` には "[y/n]" が付いておらず、原作はその**後**で内部の `buf` に
     * 付け足すので、フックが素通しするだけでは付かない）。 */
    char prompt[256];
} an_check_state;

typedef struct an_quantity_state {
    int active;
    char prompt[256];
    int min;
    int max;
} an_quantity_state;

typedef struct an_text_state {
    int active;
    int rest; /* 1: 原作の休憩の問い（`textui_cmd_rest()`。AH-48）。 */
    char prompt[256];
} an_text_state;

int an_get_check_state(an_check_state *out);
int an_get_quantity_state(an_quantity_state *out);
int an_get_text_state(an_text_state *out);

/*!
 * @brief AH-59: 持ち物の窓の「全部落とす」（原作 `context_menu_object()` の Drop All）の正準コード。
 * @details `native_store_item::commands` の 1 字として行の操作に載せ、`an_main.cpp` の持ち物用の表が
 * 同じ値の項目（ラベル `Drop All`）を持つ。原作のどのコマンドの鍵とも重ならない制御文字を使う
 * （画面側は `commands` の字をそのまま操作の識別子 `inventory:<コード>` にするだけ）。
 */
#define AN_INV_CMD_DROP_ALL 0x1F

#ifdef __cplusplus
} /* extern "C" */
#endif
