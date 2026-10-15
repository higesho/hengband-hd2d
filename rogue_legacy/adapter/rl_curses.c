#include "compat/curses.h"
#include "rl_curses_api.h"
#include "rl_host.h"
#include "rl_i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RL_ROWS = 24, RL_COLS = 80 };
enum { RL_PROMPT_MAX_CHOICES = 32, RL_PROMPT_LABEL_BYTES = 512 };

typedef struct rl_cell {
    uint32_t codepoint;
    uint32_t attr;
    /* 原作が printf で書いた字（スコア一覧の V など）。地図のマスではない（rl_term_is_text）。 */
    uint8_t text;
} rl_cell;

static rl_host_hooks g_hooks;
static char g_home_dir[4096];
static WINDOW g_stdscr;
static WINDOW g_curscr;
static rl_cell g_std_cells[RL_ROWS * RL_COLS];
static rl_cell g_cur_cells[RL_ROWS * RL_COLS];
static int g_initialized;
static int g_ended = 1;
/* 直近の画面反映（wrefresh）が stdscr 以外の窓（原作の hw や add_line の重ねた窓）だったか。rl_page_window_shown。 */
static int g_page_shown;
/* 1 の間に waddch が書いたセルは text を立てる（rl_game_printf・rl_game_putchar が囲む）。 */
static int g_mark_text;
static int g_prompt_active;
static int g_prompt_is_item;
static char g_prompt_title[RL_PROMPT_LABEL_BYTES];
static int g_prompt_choice_count;
static int g_prompt_choice_keys[RL_PROMPT_MAX_CHOICES];
static char g_prompt_choice_labels[RL_PROMPT_MAX_CHOICES][RL_PROMPT_LABEL_BYTES];
static int g_text_input_active;
static int g_player_name_input_active;
static int g_command_input_depth;
static int g_child_input_depth;
/* HD2D: 数字で前置きする繰り返し回数の入力（command.c の手当て。prepare_sources.py）。 */
static int g_count_input_depth;
static int g_count_input_value;
static int g_count_input_max;

WINDOW *stdscr = &g_stdscr;
WINDOW *curscr = &g_curscr;
int LINES = RL_ROWS;
int COLS = RL_COLS;

static uint32_t decode_utf8(const unsigned char **cursor)
{
    const unsigned char *p = *cursor;
    uint32_t cp;
    if (*p < 0x80) { cp = *p++; }
    else if ((*p & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x1f) << 6) | (uint32_t)(p[1] & 0x3f); p += 2;
    } else if ((*p & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x0f) << 12) | ((uint32_t)(p[1] & 0x3f) << 6) | (uint32_t)(p[2] & 0x3f); p += 3;
    } else if ((*p & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) {
        cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3f) << 12) | ((uint32_t)(p[2] & 0x3f) << 6) | (uint32_t)(p[3] & 0x3f); p += 4;
    } else {
        /* UTF-8 でない並び。Windows では原作が文に埋めた ANSI のパスなので、その 1 字として読む。 */
        unsigned int ansi = 0;
        const int used = rl_decode_ansi_char(p, &ansi);
        if (used > 0) { cp = ansi; p += used; }
        else { cp = '?'; ++p; }
    }
    *cursor = p;
    return cp;
}

static void blank(rl_cell *cell)
{
    cell->codepoint = ' ';
    cell->attr = A_NORMAL;
    cell->text = 0;
}

static void init_window(WINDOW *w, int lines, int cols, int y, int x, rl_cell *cells)
{
    memset(w, 0, sizeof(*w));
    w->_maxy = lines;
    w->_maxx = cols;
    w->_begy = y;
    w->_begx = x;
    w->_cells = cells;
}

static rl_cell *root_cells(WINDOW *w)
{
    if (w == NULL) return NULL;
    while (w->_parent != NULL) w = w->_parent;
    return (rl_cell *)w->_cells;
}

static WINDOW *root_window(WINDOW *w)
{
    if (w == NULL) return NULL;
    while (w->_parent != NULL) w = w->_parent;
    return w;
}

static int absolute_xy(WINDOW *w, int y, int x, int *ay, int *ax)
{
    if (w == NULL || y < 0 || x < 0 || y >= w->_maxy || x >= w->_maxx) return ERR;
    *ay = y;
    *ax = x;
    while (w->_parent != NULL) {
        *ay += w->_sub_y;
        *ax += w->_sub_x;
        w = w->_parent;
    }
    return (*ay >= 0 && *ax >= 0 && *ay < w->_maxy && *ax < w->_maxx) ? OK : ERR;
}

static rl_cell *cell_at(WINDOW *w, int y, int x)
{
    int ay, ax;
    WINDOW *root;
    rl_cell *cells;
    if (absolute_xy(w, y, x, &ay, &ax) == ERR) return NULL;
    root = root_window(w);
    cells = root_cells(w);
    return &cells[ay * root->_maxx + ax];
}

void rl_set_host_hooks(const rl_host_hooks *hooks)
{
    if (hooks == NULL) memset(&g_hooks, 0, sizeof(g_hooks));
    else g_hooks = *hooks;
}

void rl_host_present(void) { if (g_hooks.present != NULL) g_hooks.present(); }
int rl_host_next_key(void) { return g_hooks.next_key != NULL ? g_hooks.next_key() : 0; }
void rl_host_sleep_ms(int ms) { if (g_hooks.sleep_ms != NULL) g_hooks.sleep_ms(ms); }
int rl_host_shutdown_requested(void) { return g_hooks.shutdown_requested != NULL ? g_hooks.shutdown_requested() : 0; }
void rl_set_home_dir(const char *path)
{
    if (path == NULL) {
        g_home_dir[0] = '\0';
        return;
    }
    strncpy(g_home_dir, path, sizeof(g_home_dir) - 1);
    g_home_dir[sizeof(g_home_dir) - 1] = '\0';
}
const char *rl_host_home_dir(void) { return g_home_dir; }

static char g_player_name[256];
void rl_set_player_name(const char *name)
{
    if (name == NULL) {
        g_player_name[0] = '\0';
        return;
    }
    strncpy(g_player_name, name, sizeof(g_player_name) - 1);
    g_player_name[sizeof(g_player_name) - 1] = '\0';
}
const char *rl_host_player_name(void) { return g_player_name; }

/*
 * HD2D: 新規ゲームの好きな果物の既定値。UI 言語が日本語なら「スライムモールド」、
 * それ以外は原作の既定 "slime-mold" のまま（rogue_restore 第 2 回・項目 2）。
 * ROGUEOPTS や設定画面で選んだ果物、復元したセーブの果物は常にこれより優先される
 * （main.c 側の手当てが「まだ何も決まっていないときだけ」ここを見るため）。
 */
static char g_default_fruit[256];
void rl_set_default_fruit(const char *fruit)
{
    if (fruit == NULL) {
        g_default_fruit[0] = '\0';
        return;
    }
    strncpy(g_default_fruit, fruit, sizeof(g_default_fruit) - 1);
    g_default_fruit[sizeof(g_default_fruit) - 1] = '\0';
}
const char *rl_host_default_fruit(void) { return g_default_fruit; }

void rl_prompt_clear(void)
{
    int i;
    g_prompt_active = 0;
    g_prompt_is_item = 0;
    g_prompt_title[0] = '\0';
    g_prompt_choice_count = 0;
    for (i = 0; i < RL_PROMPT_MAX_CHOICES; ++i) {
        g_prompt_choice_keys[i] = 0;
        g_prompt_choice_labels[i][0] = '\0';
    }
}

void rl_prompt_begin(const char *title)
{
    const char *translated;
    rl_prompt_clear();
    g_prompt_active = 1;
    translated = rl_ja_text(title != NULL ? title : "");
    strncpy(g_prompt_title, translated, sizeof(g_prompt_title) - 1);
    g_prompt_title[sizeof(g_prompt_title) - 1] = '\0';
}

void rl_item_prompt_begin(const char *title)
{
    rl_prompt_begin(title);
    g_prompt_is_item = 1;
}

void rl_prompt_add_choice(int key, const char *label)
{
    const char *translated;
    int index;
    if (!g_prompt_active || key <= 0 || key > 255 || g_prompt_choice_count >= RL_PROMPT_MAX_CHOICES) return;
    index = g_prompt_choice_count++;
    g_prompt_choice_keys[index] = key;
    translated = rl_ja_text(label != NULL ? label : "");
    strncpy(g_prompt_choice_labels[index], translated, RL_PROMPT_LABEL_BYTES - 1);
    g_prompt_choice_labels[index][RL_PROMPT_LABEL_BYTES - 1] = '\0';
}

int rl_prompt_active(void) { return g_prompt_active; }
int rl_prompt_is_item(void) { return g_prompt_is_item; }
const char *rl_prompt_title(void) { return g_prompt_title; }
int rl_prompt_choice_count(void) { return g_prompt_choice_count; }
int rl_prompt_choice_key(int index)
{
    return (index >= 0 && index < g_prompt_choice_count) ? g_prompt_choice_keys[index] : 0;
}
const char *rl_prompt_choice_label(int index)
{
    return (index >= 0 && index < g_prompt_choice_count) ? g_prompt_choice_labels[index] : "";
}
void rl_text_input_begin(void) { g_text_input_active = 1; }
void rl_text_input_end(void) { g_text_input_active = 0; }
int rl_text_input_active(void) { return g_text_input_active; }
void rl_player_name_input_set(int active)
{
    g_player_name_input_active = active ? 1 : 0;
    g_text_input_active = g_player_name_input_active;
}
int rl_player_name_input_active(void) { return g_player_name_input_active; }

/* 訳を通さずに UTF-8 の値をそのまま書く（入力欄の中身は原作の値そのもの）。 */
static void rl_waddstr_raw(WINDOW *win, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p) (void)waddch(win, decode_utf8(&p));
}

/*
 * HD2D: 設定一覧（o）の文字列の行（名前・好きな果物）の編集。原作の get_str() は
 * 空の欄から打ち直す作りで、1 字消去は 1 バイト単位、0x80 以上のバイトは ^X の形で
 * 描いていた（5.4 は isprint で捨てる）。日本語の果物名を持てず、見出しと古い値が
 * 入力の中身に見えていた。
 * ここでは**今の値が入った状態**で始め、BS（8／127）は UTF-8 の 1 字ずつ、
 * Ctrl-U は全消去、Enter で確定、Esc で取消（値は変えない）。空のまま確定したら
 * 原作どおり値を変えない。戻り値は終わらせたキー（'\r' か 27）。
 */
static char g_edit_original[1100];
const char *rl_text_input_original(void) { return g_edit_original; }

int rl_option_edit_string(WINDOW *win, int oy, int ox, char *opt, int cap, int first, int (*read_key)(void))
{
    char buf[1100];
    int n, c, skip = 0;
    if (cap > (int)sizeof(buf) - 1) cap = (int)sizeof(buf) - 1;
    snprintf(g_edit_original, sizeof(g_edit_original), "%s", opt);
    n = (int)strlen(opt);
    if (n > cap) n = cap;
    while (n > 0 && ((unsigned char)opt[n] & 0xc0u) == 0x80u) --n;    /* 字の途中で切らない */
    memcpy(buf, opt, (size_t)n);
    c = first;
    for (;;) {
        buf[n] = '\0';
        (void)wmove(win, oy, ox);
        rl_waddstr_raw(win, buf);
        (void)wclrtoeol(win);
        (void)wrefresh(win);
        if (c < 0) c = read_key();
        if (c <= 0 || c >= 0xff) { c = -1; continue; }
        if (c == '\n' || c == '\r' || c == 0x1b || c == 0x07) {
            const int done = (c == '\n' || c == '\r') ? '\r' : 0x1b;
            if (done == '\r' && n > 0) { memcpy(opt, buf, (size_t)n); opt[n] = '\0'; }
            (void)wmove(win, oy, ox);
            rl_waddstr_raw(win, opt);
            (void)wclrtoeol(win);
            g_edit_original[0] = '\0';
            return done;
        }
        if (c == 8 || c == 127) {
            skip = 0;
            if (n > 0) {
                do --n; while (n > 0 && ((unsigned char)buf[n] & 0xc0u) == 0x80u);
            }
        } else if (c == 0x15) {
            skip = 0;
            n = 0;
        } else if ((c & 0xc0) == 0x80) {
            if (skip > 0) --skip;
            else if (n > 0 && n < cap) buf[n++] = (char)c;
        } else if (c >= 0xc0) {
            const int need = c >= 0xf0 ? 4 : (c >= 0xe0 ? 3 : 2);
            if (n + need > cap) skip = need - 1;
            else { skip = 0; buf[n++] = (char)c; }
        } else if (c >= 0x20 && c != 0x7f) {
            skip = 0;
            if (n < cap) buf[n++] = (char)c;
        }
        c = -1;
    }
}
int rl_child_input_active(void) { return g_child_input_depth > 0; }
void rl_command_input_begin(void) { ++g_command_input_depth; }
void rl_command_input_end(void) { if (g_command_input_depth > 0) --g_command_input_depth; }
int rl_command_input_active(void) { return g_command_input_depth > 0; }
void rl_count_input_begin(int value, int max)
{
    ++g_count_input_depth;
    g_count_input_value = value;
    g_count_input_max = max;
}
void rl_count_input_set(int value) { g_count_input_value = value; }
void rl_count_input_end(void) { if (g_count_input_depth > 0) --g_count_input_depth; }
int rl_count_input_active(void) { return g_count_input_depth > 0; }
int rl_count_input_value(void) { return g_count_input_value; }
int rl_count_input_max(void) { return g_count_input_max; }

char *rl_storage_file(const char *basename)
{
    static char path[4096];
    const char *base = basename != NULL ? basename : "";
    if (g_home_dir[0] == '\0') {
        snprintf(path, sizeof(path), "%s", base);
        return path;
    }
    const size_t length = strlen(g_home_dir);
    const char last = length > 0 ? g_home_dir[length - 1] : '\0';
#if defined(_WIN32)
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (last == '/' || last == '\\')
        snprintf(path, sizeof(path), "%s%s", g_home_dir, base);
    else
        snprintf(path, sizeof(path), "%s%c%s", g_home_dir, sep, base);
    return path;
}

WINDOW *initscr(void)
{
    int i;
    init_window(&g_stdscr, RL_ROWS, RL_COLS, 0, 0, g_std_cells);
    init_window(&g_curscr, RL_ROWS, RL_COLS, 0, 0, g_cur_cells);
    for (i = 0; i < RL_ROWS * RL_COLS; ++i) {
        blank(&g_std_cells[i]);
        blank(&g_cur_cells[i]);
    }
    g_initialized = 1;
    g_ended = 0;
    return stdscr;
}

int endwin(void) { g_ended = 1; return OK; }
int isendwin(void) { return g_ended; }

WINDOW *newwin(int lines, int cols, int begin_y, int begin_x)
{
    WINDOW *w;
    rl_cell *cells;
    int i;
    if (lines <= 0 || cols <= 0 || lines > RL_ROWS || cols > RL_COLS) return NULL;
    w = (WINDOW *)calloc(1, sizeof(*w));
    cells = (rl_cell *)calloc((size_t)lines * (size_t)cols, sizeof(*cells));
    if (w == NULL || cells == NULL) {
        free(w);
        free(cells);
        return NULL;
    }
    init_window(w, lines, cols, begin_y, begin_x, cells);
    for (i = 0; i < lines * cols; ++i) blank(&cells[i]);
    return w;
}

WINDOW *subwin(WINDOW *parent, int lines, int cols, int begin_y, int begin_x)
{
    WINDOW *w;
    if (parent == NULL || lines <= 0 || cols <= 0) return NULL;
    w = (WINDOW *)calloc(1, sizeof(*w));
    if (w == NULL) return NULL;
    init_window(w, lines, cols, begin_y, begin_x, parent->_cells);
    w->_is_subwin = 1;
    w->_parent = parent;
    w->_sub_y = begin_y - parent->_begy;
    w->_sub_x = begin_x - parent->_begx;
    if (w->_sub_y < 0 || w->_sub_x < 0 || w->_sub_y + lines > parent->_maxy || w->_sub_x + cols > parent->_maxx) {
        free(w);
        return NULL;
    }
    return w;
}

int delwin(WINDOW *w)
{
    if (w == NULL) return ERR;
    if (w == stdscr || w == curscr) return OK;
    if (!w->_is_subwin) free(w->_cells);
    free(w);
    return OK;
}

int mvwin(WINDOW *w, int y, int x)
{
    if (w == NULL || y < 0 || x < 0 || y + w->_maxy > LINES || x + w->_maxx > COLS) return ERR;
    w->_begy = y;
    w->_begx = x;
    return OK;
}

int raw(void) { return OK; }
int noraw(void) { return OK; }
int crmode(void) { return OK; }
int nocrmode(void) { return OK; }
int cbreak(void) { return OK; }
int nocbreak(void) { return OK; }
int echo(void) { return OK; }
int noecho(void) { return OK; }
int nonl(void) { return OK; }
int keypad(WINDOW *w, int enabled) { (void)w; (void)enabled; return OK; }
int idlok(WINDOW *w, int enabled) { (void)w; (void)enabled; return OK; }
int leaveok(WINDOW *w, int enabled) { if (w != NULL) w->_leaveok = enabled; return OK; }
int scrollok(WINDOW *w, int enabled) { (void)w; (void)enabled; return OK; }
int nodelay(WINDOW *w, int enabled) { if (w == NULL) return ERR; w->_nodelay = enabled; return OK; }

int werase(WINDOW *w)
{
    int y, x;
    if (w == NULL) return ERR;
    for (y = 0; y < w->_maxy; ++y)
        for (x = 0; x < w->_maxx; ++x) {
            rl_cell *c = cell_at(w, y, x);
            if (c != NULL) blank(c);
        }
    w->_cury = 0;
    w->_curx = 0;
    return OK;
}

int wclear(WINDOW *w) { return werase(w); }
int erase(void) { return werase(stdscr); }
int clear(void) { return wclear(stdscr); }

static void copy_to_physical(WINDOW *w)
{
    int y, x;
    if (w == NULL || w == curscr) return;
    for (y = 0; y < w->_maxy; ++y) {
        for (x = 0; x < w->_maxx; ++x) {
            int dy = w->_begy + y;
            int dx = w->_begx + x;
            rl_cell *src = cell_at(w, y, x);
            if (src != NULL && dy >= 0 && dy < RL_ROWS && dx >= 0 && dx < RL_COLS)
                g_cur_cells[dy * RL_COLS + dx] = *src;
        }
    }
    g_curscr._cury = w->_begy + w->_cury;
    g_curscr._curx = w->_begx + w->_curx;
    /*
     * 原作が地図（stdscr）の代わり・上に別の窓を出す所は、どれも「一覧を見せて Space を待つ」形で、
     * その窓に wrefresh する（help の全画面・options・add_line の頁と重ねた窓・show_win）。
     * 地図へ戻すときは stdscr を refresh する。画面側が「いま一覧が出ている」ことを知る手がかりにする。
     */
    g_page_shown = (w != stdscr);
}

int wrefresh(WINDOW *w)
{
    if (!g_initialized) (void)initscr();
    copy_to_physical(w);
    rl_host_present();
    return OK;
}

int refresh(void) { return wrefresh(stdscr); }
int touchwin(WINDOW *w) { (void)w; return OK; }
int clearok(WINDOW *w, int enabled) { (void)w; (void)enabled; return OK; }

int overwrite(const WINDOW *src_const, WINDOW *dst)
{
    WINDOW *src = (WINDOW *)src_const;
    int y, x;
    int h, width;
    if (src == NULL || dst == NULL) return ERR;
    h = src->_maxy < dst->_maxy ? src->_maxy : dst->_maxy;
    width = src->_maxx < dst->_maxx ? src->_maxx : dst->_maxx;
    for (y = 0; y < h; ++y)
        for (x = 0; x < width; ++x) {
            rl_cell *s = cell_at(src, y, x);
            rl_cell *d = cell_at(dst, y, x);
            if (s != NULL && d != NULL) *d = *s;
        }
    return OK;
}

int overlay(const WINDOW *src_const, WINDOW *dst)
{
    WINDOW *src = (WINDOW *)src_const;
    int y, x;
    int h, width;
    if (src == NULL || dst == NULL) return ERR;
    h = src->_maxy < dst->_maxy ? src->_maxy : dst->_maxy;
    width = src->_maxx < dst->_maxx ? src->_maxx : dst->_maxx;
    for (y = 0; y < h; ++y) {
        for (x = 0; x < width; ++x) {
            rl_cell *s = cell_at(src, y, x);
            rl_cell *d = cell_at(dst, y, x);
            if (s != NULL && d != NULL && s->codepoint != ' ') *d = *s;
        }
    }
    return OK;
}

int wmove(WINDOW *w, int y, int x)
{
    if (w == NULL || y < 0 || y >= w->_maxy || x < 0 || x >= w->_maxx) return ERR;
    w->_cury = y;
    w->_curx = x;
    return OK;
}

int move(int y, int x) { return wmove(stdscr, y, x); }

int wclrtoeol(WINDOW *w)
{
    int x;
    if (w == NULL || w->_cury < 0 || w->_cury >= w->_maxy) return ERR;
    for (x = w->_curx; x < w->_maxx; ++x) {
        rl_cell *c = cell_at(w, w->_cury, x);
        if (c != NULL) blank(c);
    }
    return OK;
}

int clrtoeol(void) { return wclrtoeol(stdscr); }

int wclrtobot(WINDOW *w)
{
    int y, x;
    if (w == NULL) return ERR;
    for (y = w->_cury; y < w->_maxy; ++y) {
        int start = (y == w->_cury) ? w->_curx : 0;
        for (x = start; x < w->_maxx; ++x) {
            rl_cell *c = cell_at(w, y, x);
            if (c != NULL) blank(c);
        }
    }
    return OK;
}

int clrtobot(void) { return wclrtobot(stdscr); }

int waddch(WINDOW *w, chtype ch)
{
    rl_cell *c;
    if (w == NULL) return ERR;
    if ((ch & A_CHARTEXT) == '\n') {
        if (++w->_cury >= w->_maxy) w->_cury = w->_maxy - 1;
        w->_curx = 0;
        return OK;
    }
    /*
     * curses と同じく '\b' はカーソルを 1 つ左へ戻すだけ。以前はセルへ ^H を書いて
     * 右へ進んでいたので、原作の get_str() の 1 字消去が「変な文字列」になっていた
     *
     */
    if ((ch & A_CHARTEXT) == '\b') {
        if (w->_curx > 0) --w->_curx;
        return OK;
    }
    c = cell_at(w, w->_cury, w->_curx);
    if (c == NULL) return ERR;
    c->codepoint = ch & A_CHARTEXT;
    c->attr = w->_attr | (ch & ~A_CHARTEXT);
    c->text = g_mark_text ? 1 : 0;
    if (++w->_curx >= w->_maxx) w->_curx = w->_maxx - 1;
    return OK;
}

int addch(chtype ch) { return waddch(stdscr, ch); }
int mvwaddch(WINDOW *w, int y, int x, chtype ch) { return wmove(w, y, x) == OK ? waddch(w, ch) : ERR; }
int mvaddch(int y, int x, chtype ch) { return mvwaddch(stdscr, y, x, ch); }

int waddstr(WINDOW *w, const char *text)
{
    const char *translated = rl_ja_text(text);
    const unsigned char *p = (const unsigned char *)translated;
    if (w == NULL || text == NULL) return ERR;
    while (*p) {
        if (waddch(w, decode_utf8(&p)) == ERR) return ERR;
    }
    return OK;
}

int addstr(const char *text) { return waddstr(stdscr, text); }
int mvwaddstr(WINDOW *w, int y, int x, const char *text) { return wmove(w, y, x) == OK ? waddstr(w, text) : ERR; }
int mvaddstr(int y, int x, const char *text) { return mvwaddstr(stdscr, y, x, text); }

static int vwprintw_impl(WINDOW *w, const char *fmt, va_list ap)
{
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), rl_ja_format(fmt), ap);
    return waddstr(w, buffer);
}

int wprintw(WINDOW *w, const char *fmt, ...)
{
    int rc; va_list ap; va_start(ap, fmt); rc = vwprintw_impl(w, fmt, ap); va_end(ap); return rc;
}
int printw(const char *fmt, ...)
{
    int rc; va_list ap; va_start(ap, fmt); rc = vwprintw_impl(stdscr, fmt, ap); va_end(ap); return rc;
}
int mvwprintw(WINDOW *w, int y, int x, const char *fmt, ...)
{
    int rc; va_list ap; if (wmove(w, y, x) == ERR) return ERR; va_start(ap, fmt); rc = vwprintw_impl(w, fmt, ap); va_end(ap); return rc;
}
int mvprintw(int y, int x, const char *fmt, ...)
{
    int rc; va_list ap; if (move(y, x) == ERR) return ERR; va_start(ap, fmt); rc = vwprintw_impl(stdscr, fmt, ap); va_end(ap); return rc;
}

chtype winch(WINDOW *w)
{
    rl_cell *c;
    if (w == NULL) return (chtype)ERR;
    c = cell_at(w, w->_cury, w->_curx);
    return c == NULL ? (chtype)ERR : ((c->codepoint & A_CHARTEXT) | c->attr);
}

chtype inch(void) { return winch(stdscr); }

chtype mvwinch(WINDOW *w, int y, int x)
{
    /*
     * 本物の curses と同じく、カーソルを動かしてから読む。3.6 の winat() は
     * 「mvwinch(mw, y, x) の後に winch(mw)」でカーソル位置の字を読むので、
     * 動かさないと前の位置の字（多くは空白）が返り、隣の敵が look() で消え、
     * do_move() も敵のいるマスを岩と判定して攻撃できなかった（2026-09-23）。
     */
    if (wmove(w, y, x) == ERR) return (chtype)ERR;
    return winch(w);
}
chtype mvinch(int y, int x) { return mvwinch(stdscr, y, x); }

/*
 * HD2D: 原作 5.4 は SIGHUP を受けると auto_save() が既定の保存先へ書いてから終了する。
 * 画面側の quit_request（窓を閉じる）は信号を送らないため、ここで同じ経路を直接呼ぶ。
 * auto_save() は最後に exit()（rl_game_exit 経由の longjmp）へ進むので通常は戻らない。
 * 設計の判断（2026-09-24、監査で見つかった不具合の手当て）。
 */
extern void auto_save(int sig);
static int g_shutdown_autosaved;

/*
 * 窓を閉じたことによる自動セーブ（上の auto_save() の呼び出し）を通ったかを 1 度だけ答え、印を戻す。
 * 3.6 の auto_save() は保存の成否に関わらず exit(1) で終わるので、終了コードの判定
 * （rl_main.cpp の exit_code_after_autosave）が、この終わり方かどうかを知るのに使う。
 */
int rl_shutdown_autosave_take(void)
{
    const int was = g_shutdown_autosaved;
    g_shutdown_autosaved = 0;
    return was;
}

/*
 * 表示言語が切り替わったとき（修-46）。画面に残る字は書いた時点の言語で訳してあるので、そのままでは
 * 前の言語の状態行・メッセージが次に原作が書き直すまで残る。原作の status() は何も変わっていないと
 * 書き直さない（最後に書いた値を内部に覚えている）ので、**命令待ちの間に**次の手で書き直させる：
 *   1. 金塊を 1 つ増やして status() を呼ぶ（覚えている値と違うので書き直す）→ 元の値に戻して
 *      もう 1 度呼ぶ（また違うので正しい値で書き直す）。最後の状態行は正しい値になる。
 *   2. 1 行目にメッセージが残っているなら、原作が次の命令の前にする掃除（msg("")）を先にする。
 * どちらも原作自身の関数を呼ぶだけで、原作のソースは変えない。ゲームのスレッドの命令待ちでだけ呼ぶ
 * （問い・［続く］・文字入力・回数入力の最中は次の命令待ちまで待つ）。
 */
extern int purse;
extern int mpos;
extern void status(void);
#if RL_GENERATION == 54
extern unsigned char stat_msg; /* 原作は bool。1 バイトとして読む（int で宣言すると隣の変数まで読む） */
extern int msg(char *fmt, ...);
#else
extern void msg(char *fmt, ...);
#endif
static volatile int g_language_refresh;

void rl_language_changed(void) { g_language_refresh = 1; }

static void refresh_language_display(void)
{
    g_language_refresh = 0;
#if RL_GENERATION == 54
    if (stat_msg) return; /* 状態をメッセージ行へ出す設定のときは、状態行の書き直しがメッセージを増やすので何もしない */
#endif
    purse += 1;
    status();
    purse -= 1;
    status();
    if (mpos != 0) msg("");
}

/*
 * 'V'（スコア一覧）は再構成後の command.c で score(0, -1, 0) を呼ぶ。score() は順位表の初期化に
 * ゲーム本体の乱数 seed を大量に進める（3.6 で 830 回）。原作ではゲーム途中に通らない経路なので、
 * コマンド待ちで 'V' を受けた時点の seed を控え、次のコマンド待ちに戻ったときに復元する
 * （回数つきで V を繰り返す場合も、最初の 1 回の前の値へ戻る）。乱数に効く変数は seed だけ。
 */
extern int seed;
static int g_score_seed_saved;
static int g_score_seed_value;

int wgetch(WINDOW *w)
{
    if (g_score_seed_saved && g_command_input_depth > 0) {
        seed = g_score_seed_value;
        g_score_seed_saved = 0;
    }
    const int child_wait = g_command_input_depth == 0;
    if (child_wait) ++g_child_input_depth;
    for (;;) {
        int key = rl_host_next_key();
        if (key > 0) {
            if (key == 'V' && g_command_input_depth > 0 && !g_score_seed_saved) {
                g_score_seed_value = seed;
                g_score_seed_saved = 1;
            }
            if (child_wait && g_child_input_depth > 0) --g_child_input_depth;
            return key;
        }
        if (w != NULL && w->_nodelay) {
            if (child_wait && g_child_input_depth > 0) --g_child_input_depth;
            return ERR;
        }
        if (g_language_refresh && g_command_input_depth > 0 && g_child_input_depth == 0 && !g_prompt_active
            && !g_text_input_active && g_count_input_depth == 0) {
            refresh_language_display();
        }
        if (rl_host_shutdown_requested()) {
            if (!g_shutdown_autosaved) {
                g_shutdown_autosaved = 1;
                auto_save(0);
            }
            if (child_wait && g_child_input_depth > 0) --g_child_input_depth;
            return 27;
        }
        rl_host_present();
        rl_host_sleep_ms(10);
    }
}

int getch(void) { return wgetch(stdscr); }

int wgetnstr(WINDOW *w, char *buffer, int length)
{
    int used = 0;
    if (buffer == NULL || length <= 0) return ERR;
    while (used < length) {
        int ch = wgetch(w);
        if (ch == '\r' || ch == '\n') break;
        if (ch == 27) { buffer[0] = '\0'; return ERR; }
        if ((ch == 8 || ch == 127) && used > 0) {
            --used;
            continue;
        }
        if (ch >= 0x20 && ch <= 0x7e) buffer[used++] = (char)ch;
    }
    buffer[used] = '\0';
    return OK;
}

int flushinp(void) { return OK; }

char *unctrl(chtype ch)
{
    static char text[3];
    const unsigned value = (unsigned)(ch & 0xffu);
    if (value >= 0x20u && value < 0x7fu) {
        text[0] = (char)value;
        text[1] = '\0';
    } else if (value == 0x7fu) {
        text[0] = '^';
        text[1] = '?';
        text[2] = '\0';
    } else {
        text[0] = '^';
        text[1] = (char)(value + '@');
        text[2] = '\0';
    }
    return text;
}
int standout(void) { return attron(A_REVERSE); }
int standend(void) { return attrset(A_NORMAL); }
int wattrset(WINDOW *w, int attrs) { if (w == NULL) return ERR; w->_attr = (uint32_t)attrs; return OK; }
int attrset(int attrs) { return wattrset(stdscr, attrs); }
int wattron(WINDOW *w, int attrs) { if (w == NULL) return ERR; w->_attr |= (uint32_t)attrs; return OK; }
int wattroff(WINDOW *w, int attrs) { if (w == NULL) return ERR; w->_attr &= ~(uint32_t)attrs; return OK; }
int attron(int attrs) { return wattron(stdscr, attrs); }
int attroff(int attrs) { return wattroff(stdscr, attrs); }
int start_color(void) { return OK; }
int init_pair(short pair, short fg, short bg) { (void)pair; (void)fg; (void)bg; return OK; }
int beep(void) { return OK; }
int halfdelay(int tenths) { (void)tenths; return OK; }
int mvcur(int old_y, int old_x, int new_y, int new_x) { (void)old_y; (void)old_x; g_curscr._cury = new_y; g_curscr._curx = new_x; return OK; }
int erasechar(void) { return 8; }
int killchar(void) { return 0x15; }
int baudrate(void) { return 9600; }

int rl_game_printf(const char *fmt, ...)
{
    char buffer[4096];
    int count;
    va_list ap;
    va_start(ap, fmt);
    count = vsnprintf(buffer, sizeof(buffer), rl_ja_format(fmt), ap);
    va_end(ap);
    if (!g_initialized) {
        fputs(buffer, stderr);
        fflush(stderr);
        return count;
    }
    g_mark_text = 1;
    (void)waddstr(stdscr, buffer);
    g_mark_text = 0;
    (void)wrefresh(stdscr);
    return count;
}

int rl_game_putchar(int ch)
{
    if (!g_initialized) {
        fputc(ch, stderr);
        fflush(stderr);
        return ch;
    }
    g_mark_text = 1;
    (void)waddch(stdscr, (chtype)(unsigned char)ch);
    g_mark_text = 0;
    if (ch == '\n') (void)wrefresh(stdscr);
    return ch;
}

#if !defined(_WIN32)
int _getch(void) { return wgetch(stdscr); }
#endif

/*
 * メッセージの履歴。
 *
 * 原作は 1 行目を上書きするだけで、過去のメッセージをどこにも残さない
 * （`huh` に直前の 1 本を覚えるだけ）。HD2D のサブパネルは履歴を出すので、
 * 画面へ出る手前の endmsg() から 1 本ずつここへ積む。
 *
 * **積むのは原作の字そのまま（訳す前）で、訳は読み出すときに当てる**（修-46）。
 * 以前は積むときに訳していたので、表示言語を切り替えると履歴だけ前の言語のまま残った。
 * 読み出しで当てても、2 度目の変換にはならない（積んだのは原作の英語で、訳は 1 度だけ）。
 */
enum { RL_MESSAGE_HISTORY = 64, RL_MESSAGE_BYTES = 512 };
static char g_message_history[RL_MESSAGE_HISTORY][RL_MESSAGE_BYTES];
static uint32_t g_message_seq[RL_MESSAGE_HISTORY];
static int g_message_count;
static uint32_t g_message_next_seq = 1;

/*
 * 画面の 1 行目（waddstr）と同じ読み方で、いまの表示言語に訳して UTF-8 にそろえる。原作が文に埋めた
 * パスは Windows では ANSI（CP932）で、そのまま使うと履歴のパネルで日本語のフォルダ名が化けた。
 */
static void message_render(const char *raw, char *out)
{
    const char *translated = rl_ja_text(raw);
    const unsigned char *p = (const unsigned char *)translated;
    size_t used = 0;
    while (*p) {
        const uint32_t cp = decode_utf8(&p);
        char bytes[4];
        size_t n;
        if (cp < 0x80) { bytes[0] = (char)cp; n = 1; }
        else if (cp < 0x800) { bytes[0] = (char)(0xc0 | (cp >> 6)); bytes[1] = (char)(0x80 | (cp & 0x3f)); n = 2; }
        else if (cp < 0x10000) { bytes[0] = (char)(0xe0 | (cp >> 12)); bytes[1] = (char)(0x80 | ((cp >> 6) & 0x3f)); bytes[2] = (char)(0x80 | (cp & 0x3f)); n = 3; }
        else { bytes[0] = (char)(0xf0 | (cp >> 18)); bytes[1] = (char)(0x80 | ((cp >> 12) & 0x3f)); bytes[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); bytes[3] = (char)(0x80 | (cp & 0x3f)); n = 4; }
        if (used + n >= RL_MESSAGE_BYTES) break;
        memcpy(out + used, bytes, n);
        used += n;
    }
    out[used] = '\0';
}

void rl_message_record(const char *text)
{
    int at;
    char fresh[RL_MESSAGE_BYTES];
    if (text == NULL || *text == '\0') return;
    message_render(text, fresh);
    if (fresh[0] == '\0') return;
    /* 同じ文を 2 度積まない判定は、いまの言語に訳した後の文で行う。 */
    if (g_message_count > 0) {
        char last_text[RL_MESSAGE_BYTES];
        const int last = (g_message_count - 1) % RL_MESSAGE_HISTORY;
        message_render(g_message_history[last], last_text);
        if (strcmp(last_text, fresh) == 0) return;
    }
    at = g_message_count % RL_MESSAGE_HISTORY;
    strncpy(g_message_history[at], text, RL_MESSAGE_BYTES - 1);
    g_message_history[at][RL_MESSAGE_BYTES - 1] = '\0';
    g_message_seq[at] = g_message_next_seq++;
    ++g_message_count;
}

int rl_message_history_count(void)
{
    return g_message_count < RL_MESSAGE_HISTORY ? g_message_count : RL_MESSAGE_HISTORY;
}

const char *rl_message_history_text(int index, uint32_t *seq)
{
    static char rendered[RL_MESSAGE_BYTES];
    int at;
    const int available = rl_message_history_count();
    if (index < 0 || index >= available) return NULL;
    at = (g_message_count - available + index) % RL_MESSAGE_HISTORY;
    if (seq != NULL) *seq = g_message_seq[at];
    message_render(g_message_history[at], rendered);
    return rendered;
}

int rl_term_rows(void) { return RL_ROWS; }
int rl_term_cols(void) { return RL_COLS; }
uint32_t rl_term_codepoint(int row, int col)
{
    if (row < 0 || row >= RL_ROWS || col < 0 || col >= RL_COLS) return 0;
    return g_cur_cells[row * RL_COLS + col].codepoint;
}
uint32_t rl_term_attr(int row, int col)
{
    if (row < 0 || row >= RL_ROWS || col < 0 || col >= RL_COLS) return 0;
    return g_cur_cells[row * RL_COLS + col].attr;
}
int rl_term_is_text(int row, int col)
{
    if (row < 0 || row >= RL_ROWS || col < 0 || col >= RL_COLS) return 0;
    return g_cur_cells[row * RL_COLS + col].text != 0;
}
int rl_page_window_shown(void) { return g_page_shown; }
int rl_term_cursor_row(void) { return g_curscr._cury; }
int rl_term_cursor_col(void) { return g_curscr._curx; }
void rl_term_present_now(void) { rl_host_present(); }
