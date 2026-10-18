/*!
 * @file tb_sub_terms.c
 * @brief サブパネル用の**画素なし Term を 7 枚**立てる（`GENSOBAND_SYSTEM_ACCEPT.md（幻想蛮怒）` S1）。
 *
 * ## 何をしているか
 * 幻想蛮怒の `window_stuff()`（`xtra1.c:12512`）は `angband_term[j]` のうち
 * `window_flag[j]` に立っている種類だけを `fix_*` で描く。ここでは画素を出さない Term を
 * `angband_term[1..7]` に置き、そのフラグを画面側の選択で入れ替え、
 * コアが描いた文字グリッドを読み取って `GameFrame::sub_panels` へ積む。
 * 先方の木へ入れたのは `cmd4.c` の `//GB:` 1 行だけ（R1 の世代計数器。設計 §7）。
 *
 * 変愚の `presentation/term/sdl_sub_window_terms.cpp` と同じ形である。違いは 2 つ:
 *
 * 1. **旧 z-term に `never_fresh` は無い。** 変愚には「立てると `window_stuff()` が
 *    その Term を数えないので立てるな」という注記があるが、こちらにはフラグ自体が無い。
 * 2. **双方向にした**（R1。`=`→`w` で変えたぶんが画面へ返る）。**フラグの差分では
 *    見分けられない**——`toggle_inven_equip()`（`object1.c:4036`）が床の品物を
 *    選ぶたびに持ち物と装備を入れ替えるので、差分で拾うと歩くだけで振動する
 *    （変愚が実際に踏んだ穴）。そこで変愚と同じく**「`=` の画面で実際に変えたときだけ
 *    増える数」**を持たせた（`cmd4.c` の `//GB:` 1 行 → `tb_window_flags_generation()`）。
 *    抱え込みと手綱の返し方は `tb_sub_panels.cpp` の `take_core_side_changes()`。
 *
 * ## 描かせるのはコアの仕事
 * `window_stuff()` を自分で呼びに行かない。`p_ptr->window` にビットを立てておけば、
 * コアが `handle_stuff()` の中で自分の都合のいいときに描く。横から呼ぶと
 * 「いまコアがどの画面を持っているか」に関わらず描くことになり、
 * 店やメニューの最中に Term を掻き回す。
 *
 * ## 文字コード
 * この TU は **UTF-8（BOM 付き）**。返す文字列は SJIS のバイト列（`tb_shim.h` の約束）。
 */

#include "angband.h"

#include "tb_shim.h"

#include <string.h>

/*! 立てた Term の実体。`angband_term[1 + i]` に挿す。 */
static term tb_sub_body[TB_SUB_PANELS];
static int tb_sub_ready[TB_SUB_PANELS];
/*! 画面側から届いた希望の大きさ。`tb_sub_term_apply_cells()` で `Term_resize` する。 */
static int tb_sub_want_cols[TB_SUB_PANELS];
static int tb_sub_want_rows[TB_SUB_PANELS];

/*! `angband_term[0]` は本線。サブは 1 から。 */
#define TB_SUB_FIRST 1

/*!
 * @name 大きさの安全域
 * @details 実寸が届く前（起動直後）と、桁違いの値が来たときの丸め。
 * 下限を小さくしすぎるとコアの表示関数が何も出せない（`display_inven()` は
 * 桁数から表示幅を決める）。上は `term` の `wid` / `hgt` が **byte** なので 255 まで。
 * @{
 */
#define TB_SUB_MIN_COLS 20
#define TB_SUB_MAX_COLS 255
#define TB_SUB_MIN_ROWS 3
#define TB_SUB_MAX_ROWS 200
#define TB_SUB_DEF_COLS 40
#define TB_SUB_DEF_ROWS 10
/*! @} */

/*!
 * @brief サブ Term の xtra フック。**ホストのフックは一切叩かない。**
 * @details `fix_*` は描いたあと `Term_fresh()` を呼ぶ。ここで本線と同じ `tb_present()` へ
 * 落ちると、capture の中から capture を呼ぶ再入になる。サブ Term は
 * 「コアの描画結果を溜めておく箱」でしかないので、黙って 0 を返す。
 */
static errr tb_sub_xtra(int n, int v)
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
 * @details 記念撮影（11）とボーグ（14・15）には `fix_*` が無い。選ばせても永久に空の
 * ままなので一覧に載せない。**表をここ 1 か所に置く**——`window_flag_desc` に名前が
 * 在ることと、描く関数が在ることは別である。
 */
static int tb_flag_has_fix(int flag)
{
    switch (flag) {
    case 0: /* PW_INVEN */
    case 1: /* PW_EQUIP */
    case 2: /* PW_SPELL */
    case 3: /* PW_PLAYER */
    case 4: /* PW_MONSTER_LIST */
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

int tb_window_flag_name(int flag, char *out, int cap)
{
    int n;

    if (!out || (cap <= 1)) {
        return TB_ERR_ARG;
    }
    out[0] = 0;
    if ((flag < 0) || (flag >= 32) || !tb_flag_has_fix(flag)) {
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

int tb_sub_terms_install(void)
{
    term *prev = Term;
    int i;

    for (i = 0; i < TB_SUB_PANELS; i++) {
        term *t = &tb_sub_body[i];

        if (tb_sub_ready[i]) {
            continue;
        }
        tb_sub_want_cols[i] = TB_SUB_DEF_COLS;
        tb_sub_want_rows[i] = TB_SUB_DEF_ROWS;

        /* 鍵の待ち行列は 0（この Term から入力は取らない。取る経路も無い）。 */
        if (term_init(t, TB_SUB_DEF_COLS, TB_SUB_DEF_ROWS, 0)) {
            Term_activate(prev);
            return TB_ERR_TERM;
        }
        t->soft_cursor = TRUE;
        t->attr_blank = TERM_WHITE;
        t->char_blank = 0x20;
        t->xtra_hook = tb_sub_xtra;
        /* 他のフックは term_init() が「何もせず 0 を返す」hack を入れてある。 */

        angband_term[TB_SUB_FIRST + i] = t;
        tb_sub_ready[i] = 1;
    }

    /* 本線 Term を奪ったままにしない（install は活性を変えない）。 */
    Term_activate(prev);
    return TB_OK;
}

void tb_sub_term_set_cells(int panel, int cols, int rows)
{
    if ((panel < 0) || (panel >= TB_SUB_PANELS)) {
        return;
    }
    if ((cols <= 0) || (rows <= 0)) {
        return; /* 0 は「申告なし」（v1 §7）。いまの大きさを保つ */
    }
    if (cols < TB_SUB_MIN_COLS) {
        cols = TB_SUB_MIN_COLS;
    }
    if (cols > TB_SUB_MAX_COLS) {
        cols = TB_SUB_MAX_COLS;
    }
    if (rows < TB_SUB_MIN_ROWS) {
        rows = TB_SUB_MIN_ROWS;
    }
    if (rows > TB_SUB_MAX_ROWS) {
        rows = TB_SUB_MAX_ROWS;
    }
    tb_sub_want_cols[panel] = cols;
    tb_sub_want_rows[panel] = rows;
}

int tb_sub_term_apply_cells(int panel)
{
    term *prev;
    term *t;

    if ((panel < 0) || (panel >= TB_SUB_PANELS) || !tb_sub_ready[panel]) {
        return 0;
    }
    t = &tb_sub_body[panel];
    if (((int)t->wid == tb_sub_want_cols[panel]) && ((int)t->hgt == tb_sub_want_rows[panel])) {
        return 0;
    }
    prev = Term;
    Term_activate(t);
    (void)Term_resize(tb_sub_want_cols[panel], tb_sub_want_rows[panel]);
    Term_activate(prev);
    return 1;
}

int tb_sub_term_size(int panel, int *cols, int *rows)
{
    if ((panel < 0) || (panel >= TB_SUB_PANELS) || !tb_sub_ready[panel]) {
        return TB_ERR_STATE;
    }
    if (cols) {
        *cols = (int)tb_sub_body[panel].wid;
    }
    if (rows) {
        *rows = (int)tb_sub_body[panel].hgt;
    }
    return TB_OK;
}

int tb_sub_term_row(int panel, int y, char *text, unsigned char *attr, int cap)
{
    const term_win *win;
    int n;
    int x;

    if ((panel < 0) || (panel >= TB_SUB_PANELS) || !tb_sub_ready[panel]) {
        return TB_ERR_STATE;
    }
    if (!text || (cap <= 1) || (y < 0) || (y >= (int)tb_sub_body[panel].hgt)) {
        return TB_ERR_ARG;
    }
    /* `scr` が最新（`old` は fresh 後の写し）。`tb_term_row` と同じ流儀。 */
    win = tb_sub_body[panel].scr;
    if (!win) {
        return TB_ERR_STATE;
    }
    n = (int)tb_sub_body[panel].wid;
    if (n > cap - 1) {
        n = cap - 1;
    }
    for (x = 0; x < n; x++) {
        text[x] = win->c[y][x];
        if (attr) {
            attr[x] = (unsigned char)win->a[y][x];
        }
    }
    text[n] = 0;
    return n;
}

int tb_sub_current_kind(int panel)
{
    u32b flags;
    int i;

    if ((panel < 0) || (panel >= TB_SUB_PANELS)) {
        return -1;
    }
    flags = window_flag[TB_SUB_FIRST + panel];
    for (i = 0; i < 32; i++) {
        if (flags & ((u32b)1 << i)) {
            return i;
        }
    }
    return -1;
}

int tb_sub_sanitize_kind(int flag)
{
    if ((flag < 0) || (flag >= 32) || !tb_flag_has_fix(flag)) {
        return -1;
    }
    return flag;
}

int tb_sub_clear_main_flags(void)
{
    if (window_flag[0] == 0) {
        return 0;
    }
    window_flag[0] = 0;
    return 1;
}

int tb_sub_set_kind(int panel, int flag)
{
    u32b want;

    if ((panel < 0) || (panel >= TB_SUB_PANELS)) {
        return 0;
    }
    want = 0;
    if ((flag >= 0) && (flag < 32) && tb_flag_has_fix(flag)) {
        want = ((u32b)1 << flag);
    }
    if (window_flag[TB_SUB_FIRST + panel] == want) {
        return 0;
    }
    window_flag[TB_SUB_FIRST + panel] = want;
    return 1;
}

void tb_sub_request_redraw(void)
{
    int i;

    /*
     * **描くのはコアの都合のいいとき。**ここでは「描き直せ」の印を立てるだけで
     * `window_stuff()` は呼ばない（呼ぶと店やメニューの最中に Term を掻き回す）。
     * 立てるのは描画関数のある種類だけ——無い種類を立てると `window_stuff()` が
     * ビットを落とさないまま毎周回そこを通ることになる。
     */
    for (i = 0; i < 32; i++) {
        if (tb_flag_has_fix(i)) {
            p_ptr->window |= ((u32b)1 << i);
        }
    }
}
