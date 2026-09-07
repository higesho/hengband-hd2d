/*!
 * @file sq_sub_terms.c
 * @brief サブパネル用の**画素なし Term を 7 枚**立てる。
 *
 * ## 何をしているか
 * Sil-Q の `window_stuff()`（`xtra1.c`）は `angband_term[j]` のうち
 * `op_ptr->window_flag[j]` に立っている種類だけを `fix_*` で描く。ここでは画素を出さない
 * Term を `angband_term[1..7]` に置き、そのフラグを画面側の選択で入れ替え、
 * コアが描いた文字グリッドを読み取って `GameFrame::sub_panels` へ積む。
 *
 * 手本は `gensoband/adapter/gb_sub_terms.c`。**先方の木は 1 バイトも触らない**
 * （必守制約 2）ので、あちらが `cmd4.c` に 1 行入れて作った「世代計数器」は使えない。
 * 代わりの手は `sq_sub_panels.cpp` の頭に書いてある（画面側が実効値を写し返すので、
 * **希望が変わったときだけ書く**ようにすれば計数器は要らない）。
 *
 * ## 描かせるのはコアの仕事
 * `window_stuff()` を自分で呼びに行かない。`p_ptr->window` にビットを立てておけば、
 * コアが `handle_stuff()` の中で自分の都合のいいときに描く。横から呼ぶと
 * 「いまコアがどの画面を持っているか」に関わらず描くことになり、
 * 品選びや鍛冶の最中に Term を掻き回す。
 *
 * ## 文字コード
 * この TU は **UTF-8（BOM 付き）**。返す文字列は CP932 のバイト列（`sq_shim.h` の約束）。
 */

#include "angband.h"

#include "sq_lang_c.h"
#include "sq_shim.h"

#include <string.h>

/*! 立てた Term の実体。`angband_term[1 + i]` に挿す。 */
static term sq_sub_body[SQ_SUB_PANELS];
static int sq_sub_ready[SQ_SUB_PANELS];
/*! 画面側から届いた希望の大きさ。`sq_sub_term_apply_cells()` で `Term_resize` する。 */
static int sq_sub_want_cols[SQ_SUB_PANELS];
static int sq_sub_want_rows[SQ_SUB_PANELS];

/*! `angband_term[0]` は本線。サブは 1 から（Sil-Q の `ANGBAND_TERM_MAX` は 8）。 */
#define SQ_SUB_FIRST 1

/*!
 * @name 大きさの安全域
 * @details 実寸が届く前（起動直後）と、桁違いの値が来たときの丸め。
 * 下限を小さくしすぎるとコアの表示関数が何も出せない。上は `term` の
 * `wid` / `hgt` が **byte** なので 255 まで。
 * @{
 */
#define SQ_SUB_MIN_COLS 20
#define SQ_SUB_MAX_COLS 255
#define SQ_SUB_MIN_ROWS 3
#define SQ_SUB_MAX_ROWS 200
#define SQ_SUB_DEF_COLS 40
#define SQ_SUB_DEF_ROWS 10
/*! @} */

/*!
 * @brief サブ Term の xtra フック。**ホストのフックは一切叩かない。**
 * @details `fix_*` は描いたあと `Term_fresh()` を呼ぶ。ここで本線と同じ `sq_present()` へ
 * 落ちると、capture の中から capture を呼ぶ再入になる。サブ Term は
 * 「コアの描画結果を溜めておく箱」でしかないので、黙って 0 を返す。
 */
static errr sq_sub_xtra(int n, int v)
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
 * Sil-Q の表（`tables.c:247`）は 10 個の名前を並べているが、`window_stuff()` が
 * `fix_*` を呼ぶのは **7 つだけ**である（実読 2026-08-22）:
 *
 * | 位 | 名前 | `fix_*` |
 * |---|---|---|
 * | 0 | `PW_INVEN` | `fix_inven` |
 * | 1 | `PW_EQUIP` | `fix_equip` |
 * | 2 | `PW_PLAYER_0` | `fix_player_0` |
 * | 3 | Display player (extra) | **無い**（`defines.h` にも `PW_` が無く `xxx` 扱い） |
 * | 4 | `PW_COMBAT_ROLLS` | `fix_combat_rolls`（**Sil-Q 固有**） |
 * | 5 | `PW_MONSTER` | `fix_monster` |
 * | 6 | `PW_OBJECT` | **無い** |
 * | 7 | `PW_MESSAGE` | `fix_message` |
 * | 8 | `PW_OVERHEAD` | **無い** |
 * | 9 | `PW_MONLIST` | `fix_monlist` |
 *
 * 3・6・8 を選ばせても永久に空のままなので一覧に載せない。
 */
static int sq_flag_has_fix(int flag)
{
    switch (flag) {
    case 0: /* PW_INVEN */
    case 1: /* PW_EQUIP */
    case 2: /* PW_PLAYER_0 */
    case 4: /* PW_COMBAT_ROLLS */
    case 5: /* PW_MONSTER */
    case 7: /* PW_MESSAGE */
    case 9: /* PW_MONLIST */
        return 1;
    default:
        return 0;
    }
}

int sq_window_flag_name(int flag, char *out, int cap)
{
    int n;

    if (!out || (cap <= 1)) {
        return SQ_ERR_ARG;
    }
    out[0] = 0;
    if ((flag < 0) || (flag >= 32) || !sq_flag_has_fix(flag)) {
        return 0;
    }
    if (!window_flag_desc[flag]) {
        return 0;
    }
    /*
     * **訳を通す**（M1 の道。`sq_tr()`）。`window_flag_desc` は表そのもので、
     * 画面へ出るときも `cmd4.c:7181` が `Term_putstr` へ渡す＝フック #1 を通る。
     * ここも同じ字を出さないと、機能メニューだけ英語になる。
     */
    {
        cptr text = sq_tr(window_flag_desc[flag]);
        n = (int)strlen(text);
        if (n > cap - 1) {
            n = cap - 1;
        }
        memcpy(out, text, (size_t)n);
    }
    out[n] = 0;
    return n;
}

int sq_sub_terms_install(void)
{
    term *prev = Term;
    int i;

    for (i = 0; i < SQ_SUB_PANELS; i++) {
        term *t = &sq_sub_body[i];

        if (sq_sub_ready[i]) {
            continue;
        }
        sq_sub_want_cols[i] = SQ_SUB_DEF_COLS;
        sq_sub_want_rows[i] = SQ_SUB_DEF_ROWS;

        /* 鍵の待ち行列は 0（この Term から入力は取らない。取る経路も無い）。 */
        if (term_init(t, SQ_SUB_DEF_COLS, SQ_SUB_DEF_ROWS, 0)) {
            Term_activate(prev);
            return SQ_ERR_TERM;
        }
        t->soft_cursor = TRUE;
        t->attr_blank = TERM_WHITE;
        t->char_blank = ' ';
        t->xtra_hook = sq_sub_xtra;
        /* 他のフックは term_init() が「何もせず 0 を返す」hack を入れてある。 */

        angband_term[SQ_SUB_FIRST + i] = t;
        sq_sub_ready[i] = 1;
    }

    /* 本線 Term を奪ったままにしない（install は活性を変えない）。 */
    Term_activate(prev);
    return SQ_OK;
}

void sq_sub_term_set_cells(int panel, int cols, int rows)
{
    if ((panel < 0) || (panel >= SQ_SUB_PANELS)) {
        return;
    }
    if ((cols <= 0) || (rows <= 0)) {
        return; /* 0 は「申告なし」（v1 §7）。いまの大きさを保つ */
    }
    if (cols < SQ_SUB_MIN_COLS) {
        cols = SQ_SUB_MIN_COLS;
    }
    if (cols > SQ_SUB_MAX_COLS) {
        cols = SQ_SUB_MAX_COLS;
    }
    if (rows < SQ_SUB_MIN_ROWS) {
        rows = SQ_SUB_MIN_ROWS;
    }
    if (rows > SQ_SUB_MAX_ROWS) {
        rows = SQ_SUB_MAX_ROWS;
    }
    sq_sub_want_cols[panel] = cols;
    sq_sub_want_rows[panel] = rows;
}

int sq_sub_term_apply_cells(int panel)
{
    term *prev;
    term *t;

    if ((panel < 0) || (panel >= SQ_SUB_PANELS) || !sq_sub_ready[panel]) {
        return 0;
    }
    t = &sq_sub_body[panel];
    if (((int)t->wid == sq_sub_want_cols[panel]) && ((int)t->hgt == sq_sub_want_rows[panel])) {
        return 0;
    }
    prev = Term;
    Term_activate(t);
    (void)Term_resize(sq_sub_want_cols[panel], sq_sub_want_rows[panel]);
    Term_activate(prev);
    return 1;
}

int sq_sub_term_size(int panel, int *cols, int *rows)
{
    if ((panel < 0) || (panel >= SQ_SUB_PANELS) || !sq_sub_ready[panel]) {
        return SQ_ERR_STATE;
    }
    if (cols) {
        *cols = (int)sq_sub_body[panel].wid;
    }
    if (rows) {
        *rows = (int)sq_sub_body[panel].hgt;
    }
    return SQ_OK;
}

int sq_sub_term_row(int panel, int y, char *text, unsigned char *attr, int cap)
{
    const term_win *win;
    int n;
    int x;

    if ((panel < 0) || (panel >= SQ_SUB_PANELS) || !sq_sub_ready[panel]) {
        return SQ_ERR_STATE;
    }
    if (!text || (cap <= 1) || (y < 0) || (y >= (int)sq_sub_body[panel].hgt)) {
        return SQ_ERR_ARG;
    }
    /* `scr` が最新（`old` は fresh 後の写し）。`sq_term_row` と同じ流儀。 */
    win = sq_sub_body[panel].scr;
    if (!win) {
        return SQ_ERR_STATE;
    }
    n = (int)sq_sub_body[panel].wid;
    if (n > cap - 1) {
        n = cap - 1;
    }
    for (x = 0; x < n; x++) {
        text[x] = win->c[y][x];
        if (attr) {
            /* 16 に畳む（`sq_term_row` と同じ。`TermPalette` は 16 固定）。 */
            attr[x] = (unsigned char)(win->a[y][x] & 0x0F);
        }
    }
    text[n] = 0;
    return n;
}

int sq_sub_current_kind(int panel)
{
    u32b flags;
    int i;

    if ((panel < 0) || (panel >= SQ_SUB_PANELS)) {
        return -1;
    }
    flags = op_ptr->window_flag[SQ_SUB_FIRST + panel];
    for (i = 0; i < 32; i++) {
        if (flags & ((u32b)1 << i)) {
            return sq_flag_has_fix(i) ? i : -1;
        }
    }
    return -1;
}

int sq_sub_sanitize_kind(int flag)
{
    if ((flag < 0) || (flag >= 32) || !sq_flag_has_fix(flag)) {
        return -1;
    }
    return flag;
}

int sq_sub_clear_main_flags(void)
{
    if (op_ptr->window_flag[0] == 0) {
        return 0;
    }
    /*
     * 本線 Term（`angband_term[0]`）に種類を立てさせない。`fix_inven()` ほかは
     * `j = 0..7` を走査するので、0 番に印が立っていると**持ち物一覧が地図の上に
     * 描かれる**。0 番は地図そのものである。
     *
     * `=`→窓の画面からは立てられない（`cmd4.c` が 0 列を飛ばす）。立ちうるのは
     * **セーブに入っていた値**——本物の Sil-Q で遊んだセーブを開くと、
     * あちらの窓割りがそのまま載ってくる。
     */
    op_ptr->window_flag[0] = 0;
    return 1;
}

int sq_sub_set_kind(int panel, int flag)
{
    u32b want;

    if ((panel < 0) || (panel >= SQ_SUB_PANELS)) {
        return 0;
    }
    want = 0;
    if ((flag >= 0) && (flag < 32) && sq_flag_has_fix(flag)) {
        want = ((u32b)1 << flag);
    }
    if (op_ptr->window_flag[SQ_SUB_FIRST + panel] == want) {
        return 0;
    }
    op_ptr->window_flag[SQ_SUB_FIRST + panel] = want;
    return 1;
}

void sq_sub_request_redraw(void)
{
    int i;

    if (!character_generated) {
        return; /* `window_stuff()` が `character_generated` を見て早戻りする */
    }
    /*
     * **描くのはコアの都合のいいとき。**ここでは「描き直せ」の印を立てるだけで
     * `window_stuff()` は呼ばない（呼ぶと品選びや鍛冶の最中に Term を掻き回す）。
     * 立てるのは描画関数のある種類だけ——無い種類を立てると `window_stuff()` が
     * ビットを落とさないまま毎周回そこを通ることになる。
     */
    for (i = 0; i < 32; i++) {
        if (sq_flag_has_fix(i)) {
            p_ptr->window |= ((u32b)1 << i);
        }
    }
}
