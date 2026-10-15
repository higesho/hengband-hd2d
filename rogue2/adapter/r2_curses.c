#include "compat/curses.h"
#include "r2_host.h"
#include "r2_shim.h"

#include <stddef.h>
#include <string.h>

enum {
    R2_ROWS = 24,
    R2_COLS = 80,
};

typedef struct r2_cell {
    uint32_t codepoint;
    uint32_t attr;
} r2_cell;

static WINDOW g_window;
WINDOW *stdscr = &g_window;
WINDOW *curscr = &g_window;
int LINES = R2_ROWS;
int COLS = R2_COLS;

static r2_cell g_cells[R2_ROWS][R2_COLS];
static int g_cursor_y;
static int g_cursor_x;
static uint32_t g_attr;
static r2_host_hooks g_hooks;

enum { R2_SNAPSHOT_DEPTH = 8, R2_CELL_COUNT = R2_ROWS * R2_COLS };
typedef struct r2_screen_snapshot {
    r2_cell cells[R2_ROWS][R2_COLS];
    int cursor_y;
    int cursor_x;
    uint32_t attr;
} r2_screen_snapshot;
static r2_screen_snapshot g_snapshots[R2_SNAPSHOT_DEPTH];
static int g_snapshot_depth;
static int g_modal_depth;
static int g_text_input_depth;
static int g_numeric_depth;
static int g_numeric_value;
static int g_char_prompt_kind;
static unsigned int g_pack_prompt_mask;
static int g_command_input_depth;

static uint32_t decode_utf8(const unsigned char **cursor)
{
    const unsigned char *p = *cursor;
    uint32_t cp;
    if (*p < 0x80) {
        cp = *p++;
    } else if ((*p & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x1f) << 6) | (uint32_t)(p[1] & 0x3f);
        p += 2;
    } else if ((*p & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x0f) << 12) | ((uint32_t)(p[1] & 0x3f) << 6) | (uint32_t)(p[2] & 0x3f);
        p += 3;
    } else if ((*p & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3f) << 12)
            | ((uint32_t)(p[2] & 0x3f) << 6) | (uint32_t)(p[3] & 0x3f);
        p += 4;
    } else {
        cp = '?';
        ++p;
    }
    *cursor = p;
    return cp;
}

static void blank_cell(r2_cell *cell)
{
    cell->codepoint = ' ';
    cell->attr = A_NORMAL;
}

void r2_set_host_hooks(const r2_host_hooks *hooks)
{
    if (hooks == NULL) {
        memset(&g_hooks, 0, sizeof(g_hooks));
    } else {
        g_hooks = *hooks;
    }
}

void r2_host_present(void)
{
    if (g_hooks.present != NULL) {
        g_hooks.present();
    }
}

int r2_host_next_key(void)
{
    return g_hooks.next_key != NULL ? g_hooks.next_key() : 0;
}

void r2_host_sleep_ms(int milliseconds)
{
    if (g_hooks.sleep_ms != NULL) {
        g_hooks.sleep_ms(milliseconds);
    }
}

int r2_host_shutdown_requested(void)
{
    return g_hooks.shutdown_requested != NULL ? g_hooks.shutdown_requested() : 0;
}

int r2_host_take_interrupt(void)
{
    return g_hooks.take_interrupt != NULL ? g_hooks.take_interrupt() : 0;
}

WINDOW *initscr(void)
{
    /* mesg は do_args() で読み込み済み。最初のメッセージの前に、指定された言語を反映する（修-56）。 */
    r2_language_apply();
    clear();
    return stdscr;
}

int endwin(void) { return OK; }
int raw(void) { return OK; }
int crmode(void) { return OK; }
int noecho(void) { return OK; }
int nonl(void) { return OK; }

int clear(void)
{
    int y, x;
    for (y = 0; y < R2_ROWS; ++y) {
        for (x = 0; x < R2_COLS; ++x) {
            blank_cell(&g_cells[y][x]);
        }
    }
    g_cursor_y = 0;
    g_cursor_x = 0;
    return OK;
}

int erase(void) { return clear(); }

int refresh(void)
{
    r2_host_present();
    return OK;
}

int wrefresh(WINDOW *window)
{
    (void)window;
    return refresh();
}

int move(int y, int x)
{
    if (y < 0 || y >= R2_ROWS || x < 0 || x >= R2_COLS) {
        return ERR;
    }
    g_cursor_y = y;
    g_cursor_x = x;
    return OK;
}

int clrtoeol(void)
{
    int x;
    if (g_cursor_y < 0 || g_cursor_y >= R2_ROWS) {
        return ERR;
    }
    for (x = g_cursor_x; x < R2_COLS; ++x) {
        blank_cell(&g_cells[g_cursor_y][x]);
    }
    return OK;
}

int clrtobot(void)
{
    int y, x;
    if (g_cursor_y < 0 || g_cursor_y >= R2_ROWS) {
        return ERR;
    }
    for (y = g_cursor_y; y < R2_ROWS; ++y) {
        const int begin = (y == g_cursor_y) ? g_cursor_x : 0;
        for (x = begin; x < R2_COLS; ++x) {
            blank_cell(&g_cells[y][x]);
        }
    }
    return OK;
}

int addch(const chtype ch)
{
    if (g_cursor_y < 0 || g_cursor_y >= R2_ROWS || g_cursor_x < 0 || g_cursor_x >= R2_COLS) {
        return ERR;
    }
    g_cells[g_cursor_y][g_cursor_x].codepoint = ch & A_CHARTEXT;
    g_cells[g_cursor_y][g_cursor_x].attr = g_attr | (ch & ~A_CHARTEXT);
    if (++g_cursor_x >= R2_COLS) {
        g_cursor_x = R2_COLS - 1;
    }
    return OK;
}

int mvaddch(int y, int x, const chtype ch)
{
    return move(y, x) == OK ? addch(ch) : ERR;
}

static int addstr_plain(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    if (p == NULL) {
        return ERR;
    }
    while (*p != 0) {
        if (addch(decode_utf8(&p)) == ERR) {
            return ERR;
        }
    }
    return OK;
}

int addstr(const char *text)
{
    char localized[1024];
    /* 英語のときは、日本語の語順で継ぎ足された字を英語の語順にしてから書く（修-56 の第 2 回）。 */
    if (text != NULL && r2_localize_text(text, localized, sizeof(localized))) {
        text = localized;
    }
    return addstr_plain(text);
}

/* 行 0 のメッセージの末尾（最後の空白でないマスの次）。［続く］をこの桁に置く。 */
static int row0_text_end(void)
{
    int x;
    for (x = R2_COLS - 1; x >= 0; --x) {
        if (g_cells[0][x].codepoint != ' ' && g_cells[0][x].codepoint != 0) {
            return x + 1;
        }
    }
    return 0;
}

static int codepoint_count(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    int count = 0;
    while (*p != 0) {
        (void)decode_utf8(&p);
        ++count;
    }
    return count;
}

int mvaddstr(int y, int x, const char *text)
{
    /*
     * 英語のときは、最下段の状態の行（print_stats）だけ、原作の英語版の桁へ置き直す（修-56）。
     * 日本語版の配置のままだと "Level: " が値の桁（4）に食い込む。モーダルの画面（得点表など）は除く。
     */
    char localized[1024];
    if (text != NULL && r2_localize_text(text, localized, sizeof(localized))) {
        text = localized;
    }
    if (y == R2_ROWS - 1 && r2_language_english() && !r2_term_modal_active()) {
        x = r2_status_column_en(x);
    } else if (text != NULL && r2_language_english()) {
        /*
         * 英語は語順の組み直しで、原作が日本語の字数で測った桁（［続く］の置き場・右端に寄せる一覧の左端）とずれる。
         * ［続く］は行 0 に実際に書いた字の末尾へ置く。右端からはみ出す行は、切れないよう左へ寄せる。
         */
        if (y == 0 && strcmp(text, "--More--") == 0) {
            x = row0_text_end() + 1; /* 本文との間に空白を 1 つ置く */
            if (x + 8 > R2_COLS) x = R2_COLS - 8;
        } else if (x > 0) {
            const int length = codepoint_count(text);
            if (x + length > R2_COLS) {
                x = R2_COLS - length;
                if (x < 0) x = 0;
            }
        }
    }
    return move(y, x) == OK ? addstr_plain(text) : ERR;
}

chtype mvinch(int y, int x)
{
    /*
     * 本物の curses と同じく、読む前にカーソルをそのマスへ動かす（修-50）。
     * room.c の draw_magic_map() は mvinch_rogue() で字を読んだあと、カーソルの位置へ
     * addch_rogue() で書く。動かさないと字が各マスではなく直前のカーソル位置（行 0 の
     * メッセージの末尾）へ並んで書かれ、魔法の地図で画面の地図が広がらなかった。
     * 3.6・5.4 の rl_curses.c の mvwinch() は 2026-09-23 に同じ理由で直してある。
     */
    if (move(y, x) == ERR) {
        return (chtype)ERR;
    }
    return (g_cells[y][x].codepoint & A_CHARTEXT) | g_cells[y][x].attr;
}

int getch(void)
{
    for (;;) {
        const int key = r2_host_next_key();
        if (key > 0) {
            return key;
        }
        if (r2_host_shutdown_requested()) {
            r2_hd2d_quit();
            return 27;
        }
        r2_host_present();
        r2_host_sleep_ms(10);
    }
}

int standout(void) { return attron(A_REVERSE); }
int standend(void) { return attrset(A_NORMAL); }
int attrset(int attrs) { g_attr = (uint32_t)attrs; return OK; }
int attron(int attrs) { g_attr |= (uint32_t)attrs; return OK; }
int attroff(int attrs) { g_attr &= ~(uint32_t)attrs; return OK; }

int attr_get(attr_t *attrs, short *pair, void *opts)
{
    (void)opts;
    if (attrs != NULL) *attrs = g_attr;
    if (pair != NULL) *pair = (short)((g_attr >> 16) & 0xffu);
    return OK;
}

int start_color(void) { return OK; }
int assume_default_colors(int fg, int bg) { (void)fg; (void)bg; return OK; }
int init_pair(short pair, short fg, short bg) { (void)pair; (void)fg; (void)bg; return OK; }

void r2_getyx(int *y, int *x)
{
    if (y != NULL) *y = g_cursor_y;
    if (x != NULL) *x = g_cursor_x;
}

int r2_term_rows(void) { return R2_ROWS; }
int r2_term_cols(void) { return R2_COLS; }
uint32_t r2_term_codepoint(int row, int col)
{
    return (row >= 0 && row < R2_ROWS && col >= 0 && col < R2_COLS) ? g_cells[row][col].codepoint : 0;
}
uint32_t r2_term_attr(int row, int col)
{
    return (row >= 0 && row < R2_ROWS && col >= 0 && col < R2_COLS) ? g_cells[row][col].attr : 0;
}
uint32_t r2_term_map_codepoint(int row, int col)
{
    if (row < 0 || row >= R2_ROWS || col < 0 || col >= R2_COLS) return 0;
    return (g_snapshot_depth > 0) ? g_snapshots[0].cells[row][col].codepoint : g_cells[row][col].codepoint;
}
uint32_t r2_term_map_attr(int row, int col)
{
    if (row < 0 || row >= R2_ROWS || col < 0 || col >= R2_COLS) return 0;
    return (g_snapshot_depth > 0) ? g_snapshots[0].cells[row][col].attr : g_cells[row][col].attr;
}
int r2_term_cursor_row(void) { return g_cursor_y; }
int r2_term_cursor_col(void) { return g_cursor_x; }

int r2_term_snapshot_push(void)
{
    r2_screen_snapshot *snapshot;
    if (g_snapshot_depth >= R2_SNAPSHOT_DEPTH) return ERR;
    snapshot = &g_snapshots[g_snapshot_depth++];
    memcpy(snapshot->cells, g_cells, sizeof(g_cells));
    snapshot->cursor_y = g_cursor_y;
    snapshot->cursor_x = g_cursor_x;
    snapshot->attr = g_attr;
    return OK;
}

int r2_term_snapshot_pop(void)
{
    r2_screen_snapshot *snapshot;
    if (g_snapshot_depth <= 0) return ERR;
    snapshot = &g_snapshots[--g_snapshot_depth];
    memcpy(g_cells, snapshot->cells, sizeof(g_cells));
    g_cursor_y = snapshot->cursor_y;
    g_cursor_x = snapshot->cursor_x;
    g_attr = snapshot->attr;
    return OK;
}

void r2_term_modal_begin(void)
{
    if (g_modal_depth < 32767) ++g_modal_depth;
}

void r2_term_modal_end(void)
{
    if (g_modal_depth > 0) --g_modal_depth;
}

int r2_term_modal_active(void)
{
    return g_modal_depth > 0;
}

void r2_term_text_input_begin(void)
{
    if (g_text_input_depth < 32767) ++g_text_input_depth;
}

void r2_term_text_input_end(void)
{
    if (g_text_input_depth > 0) --g_text_input_depth;
}

int r2_term_text_input_active(void)
{
    return g_text_input_depth > 0;
}

void r2_term_numeric_begin(int value)
{
    if (g_numeric_depth < 32767) ++g_numeric_depth;
    g_numeric_value = value;
}

void r2_term_numeric_set(int value) { g_numeric_value = value; }

void r2_term_numeric_end(void)
{
    if (g_numeric_depth > 0) --g_numeric_depth;
}

int r2_term_numeric_active(void) { return g_numeric_depth > 0; }
int r2_term_numeric_value(void) { return g_numeric_value; }

void r2_term_char_prompt_begin(int kind) { g_char_prompt_kind = kind; }
void r2_term_char_prompt_end(void) { g_char_prompt_kind = 0; }
int r2_term_char_prompt_kind(void) { return g_char_prompt_kind; }
void r2_term_pack_prompt_begin(unsigned int mask) { g_pack_prompt_mask = mask; }
void r2_term_pack_prompt_end(void) { g_pack_prompt_mask = 0; }
unsigned int r2_term_pack_prompt_mask(void) { return g_pack_prompt_mask; }
void r2_term_command_input_begin(void) { if (g_command_input_depth < 32767) ++g_command_input_depth; }
void r2_term_command_input_end(void) { if (g_command_input_depth > 0) --g_command_input_depth; }
int r2_term_command_input_active(void) { return g_command_input_depth > 0; }

int r2_term_export(uint32_t *codepoints, uint32_t *attrs, int count,
    int *cursor_y, int *cursor_x, uint32_t *current_attr)
{
    int y, x, at = 0;
    if (codepoints == NULL || attrs == NULL || count < R2_CELL_COUNT) return ERR;
    for (y = 0; y < R2_ROWS; ++y) {
        for (x = 0; x < R2_COLS; ++x, ++at) {
            codepoints[at] = g_cells[y][x].codepoint;
            attrs[at] = g_cells[y][x].attr;
        }
    }
    if (cursor_y != NULL) *cursor_y = g_cursor_y;
    if (cursor_x != NULL) *cursor_x = g_cursor_x;
    if (current_attr != NULL) *current_attr = g_attr;
    return OK;
}

int r2_term_import(const uint32_t *codepoints, const uint32_t *attrs, int count,
    int cursor_y, int cursor_x, uint32_t current_attr)
{
    int y, x, at = 0;
    if (codepoints == NULL || attrs == NULL || count < R2_CELL_COUNT) return ERR;
    if (cursor_y < 0 || cursor_y >= R2_ROWS || cursor_x < 0 || cursor_x >= R2_COLS) return ERR;
    for (y = 0; y < R2_ROWS; ++y) {
        for (x = 0; x < R2_COLS; ++x, ++at) {
            g_cells[y][x].codepoint = codepoints[at];
            g_cells[y][x].attr = attrs[at];
        }
    }
    g_cursor_y = cursor_y;
    g_cursor_x = cursor_x;
    g_attr = current_attr;
    return OK;
}
