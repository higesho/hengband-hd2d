#pragma once

#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t chtype;
typedef uint32_t attr_t;

typedef struct rl_window {
    int _cury;
    int _curx;
    int _maxy;
    int _maxx;
    int _begy;
    int _begx;
    int _nodelay;
    int _leaveok;
    int _is_subwin;
    int _sub_y;
    int _sub_x;
    uint32_t _attr;
    struct rl_window *_parent;
    void *_cells;
} WINDOW;

extern WINDOW *stdscr;
extern WINDOW *curscr;
extern int LINES;
extern int COLS;

#define TRUE 1
#define FALSE 0
#define OK 0
#define ERR (-1)

#define A_NORMAL   0x00000000u
#define A_REVERSE  0x01000000u
#define A_BOLD     0x02000000u
#define A_STANDOUT A_REVERSE
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

#define KEY_LEFT   0x101
#define KEY_RIGHT  0x102
#define KEY_UP     0x103
#define KEY_DOWN   0x104
#define KEY_HOME   0x105
#define KEY_PPAGE  0x106
#define KEY_NPAGE  0x107
#define KEY_END    0x108
#define KEY_A1     0x109
#define KEY_A2     0x10a
#define KEY_A3     0x10b
#define KEY_B1     0x10c
#define KEY_B2     0x10d
#define KEY_B3     0x10e
#define KEY_C1     0x10f
#define KEY_C2     0x110
#define KEY_C3     0x111

#define getyx(win, y, x) do { (y) = (win)->_cury; (x) = (win)->_curx; } while (0)
#define getmaxy(win) ((win)->_maxy)
#define getmaxx(win) ((win)->_maxx)

WINDOW *initscr(void);
int endwin(void);
int isendwin(void);
WINDOW *newwin(int lines, int cols, int begin_y, int begin_x);
WINDOW *subwin(WINDOW *parent, int lines, int cols, int begin_y, int begin_x);
int delwin(WINDOW *window);
int mvwin(WINDOW *window, int y, int x);

int raw(void);
int noraw(void);
int crmode(void);
int nocrmode(void);
int cbreak(void);
int nocbreak(void);
int echo(void);
int noecho(void);
int nonl(void);
int keypad(WINDOW *window, int enabled);
int nodelay(WINDOW *window, int enabled);
int idlok(WINDOW *window, int enabled);
int leaveok(WINDOW *window, int enabled);
int scrollok(WINDOW *window, int enabled);

int clear(void);
int erase(void);
int wclear(WINDOW *window);
int werase(WINDOW *window);
int refresh(void);
int wrefresh(WINDOW *window);
int touchwin(WINDOW *window);
int clearok(WINDOW *window, int enabled);
int overwrite(const WINDOW *src, WINDOW *dst);
int overlay(const WINDOW *src, WINDOW *dst);

int move(int y, int x);
int wmove(WINDOW *window, int y, int x);
int clrtoeol(void);
int wclrtoeol(WINDOW *window);
int clrtobot(void);
int wclrtobot(WINDOW *window);

int addch(chtype ch);
int waddch(WINDOW *window, chtype ch);
int mvaddch(int y, int x, chtype ch);
int mvwaddch(WINDOW *window, int y, int x, chtype ch);
int addstr(const char *text);
int waddstr(WINDOW *window, const char *text);
int mvaddstr(int y, int x, const char *text);
int mvwaddstr(WINDOW *window, int y, int x, const char *text);
int printw(const char *fmt, ...);
int wprintw(WINDOW *window, const char *fmt, ...);
int mvprintw(int y, int x, const char *fmt, ...);
int mvwprintw(WINDOW *window, int y, int x, const char *fmt, ...);

chtype winch(WINDOW *window);
chtype inch(void);
chtype mvinch(int y, int x);
chtype mvwinch(WINDOW *window, int y, int x);
int getch(void);
int wgetch(WINDOW *window);
int wgetnstr(WINDOW *window, char *buffer, int length);
int flushinp(void);
char *unctrl(chtype ch);

int standout(void);
int standend(void);
int attrset(int attrs);
int wattrset(WINDOW *window, int attrs);
int attron(int attrs);
int attroff(int attrs);
int wattron(WINDOW *window, int attrs);
int wattroff(WINDOW *window, int attrs);
int start_color(void);
int init_pair(short pair, short fg, short bg);

int beep(void);
int halfdelay(int tenths);
int mvcur(int old_y, int old_x, int new_y, int new_x);
int erasechar(void);
int killchar(void);
int baudrate(void);
int rl_game_printf(const char *fmt, ...);
int rl_game_putchar(int ch);

#ifdef __cplusplus
}
#endif
