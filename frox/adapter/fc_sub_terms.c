/*!
 * @file fc_sub_terms.c
 * @brief サブパネル用の**画素なし Term を 7 枚**立てる（設計 §4.4）。
 *
 * ## 何をしているか
 * Frox の `window_stuff()`（`frox/src/xtra1.c:6348`）は `angband_term[j]` のうち
 * `window_flag[j]` に立っている種類だけを `fix_*` で描く。ここでは画素を出さない
 * Term を `angband_term[1..7]` に置き、そのフラグを画面側の選択で入れ替え、
 * コアが描いた文字グリッドを読み取って `GameFrame::sub_panels` へ積む。
 *
 * 手本は `silq/adapter/sq_sub_terms.c`。**先方のソースツリーは 1 バイトも触らない**
 * （設計 §1 制約 1）ので、幻想蛮怒が `cmd4.c` に 1 行入れて作った
 * 「世代計数器」は使えない。持ち主の決め方は `fc_sub_panels.cpp` の頭にある
 * （**画面の cfg が持ち主。希望を押し続ける**）。
 *
 * ## Sil-Q との違い
 * - フラグの置き場が `op_ptr->window_flag[]` ではなく**素の大域 `window_flag[8]`**
 *   （`frox/src/externs.h:565`）
 * - 描画関数のある種類が 7 つではなく **10** ある（下の `fc_flag_has_fix`）
 *
 * ## 描かせるのはコアの仕事
 * `window_stuff()` を自分で呼びに行かない。`p_ptr->window` にビットを立てておけば、
 * コアが `handle_stuff()` の中で自分の都合のいいときに描く。横から呼ぶと
 * 「いまコアがどの画面を持っているか」に関わらず描くことになり、
 * 品選びの最中に Term を掻き回す。
 *
 * ## 文字コード
 * この TU は **UTF-8（BOM 付き）**。M0 の Frox は純 ASCII なので、
 * 返す文字列もすべて ASCII である（`fc_shim.h` の約束）。
 */

#include "angband.h"

#include "fc_shim.h"

#include <string.h>

/*! 立てた Term の実体。`angband_term[1 + i]` に挿す。 */
static term fc_sub_body[FC_SUB_PANELS];
static int fc_sub_ready[FC_SUB_PANELS];
/*! 画面側から届いた希望の大きさ。`fc_sub_term_apply_cells()` で `Term_resize` する。 */
static int fc_sub_want_cols[FC_SUB_PANELS];
static int fc_sub_want_rows[FC_SUB_PANELS];

/*! `angband_term[0]` は本線。サブは 1 から（Frox の器は `angband_term[8]`）。 */
#define FC_SUB_FIRST 1

/*!
 * @name 大きさの安全域
 * @details 実寸が届く前（起動直後）と、桁違いの値が来たときの丸め。
 * 下限を小さくしすぎるとコアの表示関数が何も出せない。上は `term` の
 * `wid` / `hgt` が **byte** なので 255 まで。
 * @{
 */
#define FC_SUB_MIN_COLS 20
#define FC_SUB_MAX_COLS 255
#define FC_SUB_MIN_ROWS 3
#define FC_SUB_MAX_ROWS 200
#define FC_SUB_DEF_COLS 40
#define FC_SUB_DEF_ROWS 10
/*! @} */

/*!
 * @brief サブ Term の xtra フック。**ホストのフックは一切叩かない。**
 * @details `fix_*` は描いたあと `Term_fresh()` を呼ぶ。ここで本線と同じ `present` へ
 * 落ちると、capture の中から capture を呼ぶ再入になる。サブ Term は
 * 「コアの描画結果を溜めておく箱」でしかないので、黙って 0 を返す。
 */
static errr fc_sub_xtra(int n, int v)
{
    (void)v;
    if (n == TERM_XTRA_EVENT) {
        /* 入力は本線 Term だけが持つ。「キーは無い」を返す（1 は z-term が承知の値）。 */
        return 1;
    }
    /* FRESH も CLEAR も DELAY も何もしない。**0 を返すこと**（非 0 は失敗扱い）。 */
    return 0;
}

/*!
 * @brief 描画関数のある種類か（`window_stuff()` の分岐そのもの）。
 *
 * @details **`window_flag_desc` に名前が在ることと、描く関数が在ることは別である。**
 * Frox の `window_stuff()`（`xtra1.c:6348`）が `fix_*` を呼ぶのは **10**
 * （実読 2026-08-24。設計 §4.4 の一覧と同じ）:
 *
 * | 位 | 旗 | `fix_*` |
 * |---|---|---|
 * | 0 | `PW_INVEN` | `fix_inven` |
 * | 1 | `PW_EQUIP` | `fix_equip` |
 * | 2 | `PW_XXX` | **無い** |
 * | 3 | `PW_SPELL` | `fix_spell` |
 * | 4 | `PW_OBJECT_LIST` | `fix_object_list` |
 * | 5 | `PW_MONSTER_LIST` | `fix_monster_list` |
 * | 6 | `PW_MESSAGE` | `fix_message` |
 * | 7 | `PW_OVERHEAD` | `fix_overhead` |
 * | 8 | `PW_MONSTER` | `fix_monster` |
 * | 9 | `PW_OBJECT` | `fix_object` |
 * | 10 | `PW_DUNGEON` | `fix_dungeon` |
 * | 11 | `PW_SNAPSHOT` | **無い** |
 *
 * 2・11 以降を選ばせても永久に空のままなので一覧に載せない。
 * **`PW_PLAYER` と `PW_FLOOR_ITEM_LIST` は Frox に存在しない**（設計 §4.4。
 * 画面側の一覧から消すのではなく、コアが申告する種の一覧から外れる）。
 */
static int fc_flag_has_fix(int flag)
{
    switch (flag) {
    case 0: /* PW_INVEN */
    case 1: /* PW_EQUIP */
    case 3: /* PW_SPELL */
    case 4: /* PW_OBJECT_LIST */
    case 5: /* PW_MONSTER_LIST */
    case 6: /* PW_MESSAGE */
    case 7: /* PW_OVERHEAD */
    case 8: /* PW_MONSTER */
    case 9: /* PW_OBJECT */
    case 10: /* PW_DUNGEON */
        return 1;
    default:
        return 0;
    }
}

int fc_window_flag_name(int flag, char *out, int cap)
{
    int n;

    if (!out || (cap <= 1)) {
        return FC_ERR_ARG;
    }
    out[0] = 0;
    if ((flag < 0) || (flag >= 32) || !fc_flag_has_fix(flag)) {
        return 0;
    }
    if (!window_flag_desc[flag]) {
        return 0;
    }
    n = (int)strlen(window_flag_desc[flag]);
    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(out, window_flag_desc[flag], (size_t)n);
    out[n] = 0;
    return n;
}

int fc_sub_terms_install(void)
{
    term *prev = Term;
    int i;

    for (i = 0; i < FC_SUB_PANELS; i++) {
        term *t = &fc_sub_body[i];

        if (fc_sub_ready[i]) {
            continue;
        }
        fc_sub_want_cols[i] = FC_SUB_DEF_COLS;
        fc_sub_want_rows[i] = FC_SUB_DEF_ROWS;

        /* 鍵の待ち行列は 0（この Term から入力は取らない。取る経路も無い）。 */
        if (term_init(t, FC_SUB_DEF_COLS, FC_SUB_DEF_ROWS, 0)) {
            Term_activate(prev);
            return FC_ERR_TERM;
        }
        t->soft_cursor = TRUE;
        t->attr_blank = TERM_WHITE;
        t->char_blank = ' ';
        t->xtra_hook = fc_sub_xtra;
        /* 他のフックは term_init() が「何もせず 0 を返す」hack を入れてある。 */

        angband_term[FC_SUB_FIRST + i] = t;
        fc_sub_ready[i] = 1;
    }

    /* 本線 Term を奪ったままにしない（install は活性を変えない）。 */
    Term_activate(prev);
    return FC_OK;
}

void fc_sub_term_set_cells(int panel, int cols, int rows)
{
    if ((panel < 0) || (panel >= FC_SUB_PANELS)) {
        return;
    }
    if ((cols <= 0) || (rows <= 0)) {
        return; /* 0 は「申告なし」（v1 §7）。いまの大きさを保つ */
    }
    if (cols < FC_SUB_MIN_COLS) {
        cols = FC_SUB_MIN_COLS;
    }
    if (cols > FC_SUB_MAX_COLS) {
        cols = FC_SUB_MAX_COLS;
    }
    if (rows < FC_SUB_MIN_ROWS) {
        rows = FC_SUB_MIN_ROWS;
    }
    if (rows > FC_SUB_MAX_ROWS) {
        rows = FC_SUB_MAX_ROWS;
    }
    fc_sub_want_cols[panel] = cols;
    fc_sub_want_rows[panel] = rows;
}

int fc_sub_term_apply_cells(int panel)
{
    term *prev;
    term *t;

    if ((panel < 0) || (panel >= FC_SUB_PANELS) || !fc_sub_ready[panel]) {
        return 0;
    }
    t = &fc_sub_body[panel];
    if (((int)t->wid == fc_sub_want_cols[panel]) && ((int)t->hgt == fc_sub_want_rows[panel])) {
        return 0;
    }
    prev = Term;
    Term_activate(t);
    (void)Term_resize(fc_sub_want_cols[panel], fc_sub_want_rows[panel]);
    Term_activate(prev);
    return 1;
}

int fc_sub_term_size(int panel, int *cols, int *rows)
{
    if ((panel < 0) || (panel >= FC_SUB_PANELS) || !fc_sub_ready[panel]) {
        return FC_ERR_STATE;
    }
    if (cols) {
        *cols = (int)fc_sub_body[panel].wid;
    }
    if (rows) {
        *rows = (int)fc_sub_body[panel].hgt;
    }
    return FC_OK;
}

int fc_sub_term_row(int panel, int y, char *text, unsigned char *attr, int cap)
{
    const term_win *win;
    int n;
    int x;

    if ((panel < 0) || (panel >= FC_SUB_PANELS) || !fc_sub_ready[panel]) {
        return FC_ERR_STATE;
    }
    if (!text || (cap <= 1) || (y < 0) || (y >= (int)fc_sub_body[panel].hgt)) {
        return FC_ERR_ARG;
    }
    /* `scr` が最新（`old` は fresh 後の写し）。`fc_term_row` と同じ流儀。 */
    win = fc_sub_body[panel].scr;
    if (!win) {
        return FC_ERR_STATE;
    }
    n = (int)fc_sub_body[panel].wid;
    if (n > cap - 1) {
        n = cap - 1;
    }
    for (x = 0; x < n; x++) {
        text[x] = win->c[y][x];
        if (attr) {
            /* 16 に畳む（`fc_term_row` と同じ。`TermPalette` は 16 固定）。 */
            attr[x] = (unsigned char)(win->a[y][x] & 0x0F);
        }
    }
    text[n] = 0;
    return n;
}

int fc_sub_current_kind(int panel)
{
    u32b flags;
    int i;

    if ((panel < 0) || (panel >= FC_SUB_PANELS)) {
        return -1;
    }
    flags = window_flag[FC_SUB_FIRST + panel];
    for (i = 0; i < 32; i++) {
        if (flags & ((u32b)1 << i)) {
            return fc_flag_has_fix(i) ? i : -1;
        }
    }
    return -1;
}

int fc_sub_sanitize_kind(int flag)
{
    if ((flag < 0) || (flag >= 32) || !fc_flag_has_fix(flag)) {
        return -1;
    }
    return flag;
}

int fc_sub_clear_main_flags(void)
{
    if (window_flag[0] == 0) {
        return 0;
    }
    /*
     * 本線 Term（`angband_term[0]`）に種類を立てさせない。`fix_inven()` ほかは
     * `j = 0..7` を走査するので、0 番に印が立っていると**持ち物一覧が地図の上に
     * 描かれる**。0 番は地図そのものである。立ちうるのは**セーブに入っていた値**
     * ——本物の Frox で遊んだセーブを開くと、あちらの窓割りがそのまま載ってくる。
     */
    window_flag[0] = 0;
    return 1;
}

int fc_sub_set_kind(int panel, int flag)
{
    u32b want;

    if ((panel < 0) || (panel >= FC_SUB_PANELS)) {
        return 0;
    }
    want = 0;
    if ((flag >= 0) && (flag < 32) && fc_flag_has_fix(flag)) {
        want = ((u32b)1 << flag);
    }
    if (window_flag[FC_SUB_FIRST + panel] == want) {
        return 0;
    }
    window_flag[FC_SUB_FIRST + panel] = want;
    return 1;
}

void fc_sub_request_redraw(void)
{
    int i;

    if (!character_generated) {
        return; /* `window_stuff()` が `character_generated` を見て早戻りする */
    }
    /*
     * **描くのはコアの都合のいいとき。**ここでは「描き直せ」の印を立てるだけで
     * `window_stuff()` は呼ばない（呼ぶと品選びの最中に Term を掻き回す）。
     * 立てるのは描画関数のある種類だけ——無い種類を立てると `window_stuff()` が
     * ビットを落とさないまま毎周回そこを通ることになる。
     */
    for (i = 0; i < FC_SUB_PANELS; i++) {
        u32b flags = window_flag[FC_SUB_FIRST + i];
        int b;
        for (b = 0; b < 32; b++) {
            if ((flags & ((u32b)1 << b)) && fc_flag_has_fix(b)) {
                p_ptr->window |= ((u32b)1 << b);
            }
        }
    }
}
