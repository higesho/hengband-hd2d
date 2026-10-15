#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t chtype;
typedef uint32_t attr_t;

typedef struct r2_window {
    int unused;
} WINDOW;

extern WINDOW *stdscr;
extern WINDOW *curscr;
extern int LINES;
extern int COLS;

#define OK 0
#define ERR (-1)

#define A_NORMAL  0x00000000u
#define A_REVERSE 0x01000000u
#define A_CHARTEXT 0x001fffffu
#define COLOR_PAIR(n) (((attr_t)(n) & 0xffu) << 16)

#define COLOR_BLACK   0
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE   7

WINDOW *initscr(void);
int endwin(void);
int raw(void);
int crmode(void);
int noecho(void);
int nonl(void);

int clear(void);
int erase(void);
int refresh(void);
int wrefresh(WINDOW *window);
int move(int y, int x);
int clrtoeol(void);
int clrtobot(void);
int addch(const chtype ch);
int mvaddch(int y, int x, const chtype ch);
int addstr(const char *text);
int mvaddstr(int y, int x, const char *text);
chtype mvinch(int y, int x);
int getch(void);

int standout(void);
int standend(void);
int attrset(int attrs);
int attron(int attrs);
int attroff(int attrs);
int attr_get(attr_t *attrs, short *pair, void *opts);
int start_color(void);
int assume_default_colors(int fg, int bg);
int init_pair(short pair, short fg, short bg);

void r2_getyx(int *y, int *x);
#define getyx(win, y, x) r2_getyx(&(y), &(x))

int r2_term_rows(void);
int r2_term_cols(void);
uint32_t r2_term_codepoint(int row, int col);
uint32_t r2_term_attr(int row, int col);
uint32_t r2_term_map_codepoint(int row, int col);
uint32_t r2_term_map_attr(int row, int col);
int r2_term_cursor_row(void);
int r2_term_cursor_col(void);
int r2_term_snapshot_push(void);
int r2_term_snapshot_pop(void);
void r2_term_modal_begin(void);
void r2_term_modal_end(void);
int r2_term_modal_active(void);
void r2_term_text_input_begin(void);
void r2_term_text_input_end(void);
int r2_term_text_input_active(void);
void r2_term_numeric_begin(int value);
void r2_term_numeric_set(int value);
void r2_term_numeric_end(void);
int r2_term_numeric_active(void);
int r2_term_numeric_value(void);
void r2_term_char_prompt_begin(int kind);
void r2_term_char_prompt_end(void);
int r2_term_char_prompt_kind(void);
void r2_term_pack_prompt_begin(unsigned int mask);
void r2_term_pack_prompt_end(void);
unsigned int r2_term_pack_prompt_mask(void);
void r2_term_command_input_begin(void);
void r2_term_command_input_end(void);
int r2_term_command_input_active(void);
int r2_term_export(uint32_t *codepoints, uint32_t *attrs, int count,
    int *cursor_y, int *cursor_x, uint32_t *current_attr);
int r2_term_import(const uint32_t *codepoints, const uint32_t *attrs, int count,
    int cursor_y, int cursor_x, uint32_t current_attr);

#ifdef __cplusplus
}
#endif
