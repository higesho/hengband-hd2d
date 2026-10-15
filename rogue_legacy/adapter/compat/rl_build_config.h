#pragma once

/*
 * The restored Rogue 3.x/5.x trees predate modern self-contained headers.
 * Force the standard types they assume into every translation unit, while
 * leaving platform behavior in mdport.c.  This file is build glue only; the
 * immutable upstream source remains untouched.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>

#if defined(_WIN32)
typedef unsigned int uid_t;
#else
#include <unistd.h>
int _getch(void);
#endif

#define HAVE_CONFIG_H 1
#define HAVE_CURSES_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_TYPES 1
#define HAVE_LIMITS_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STDINT_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_MEMSET 1
#define HAVE_STRCHR 1
#define HAVE_STRERROR 1
#define HAVE_VPRINTF 1
#define HAVE_ERASECHAR 1
#define HAVE_KILLCHAR 1
#define HAVE_ERRNO_H 1

#if defined(_WIN32)
#define HAVE_PROCESS_H 1
#endif

/* md_hasclreol() uses this branch without pulling in a real terminfo API. */
#define __PDCURSES__ 1

/* HD2D-only home override used by the reproducible mdport.c source patch. */
const char *rl_host_home_dir(void);
/* HD2D: OS のアカウント名の代わりに PlayerIdentity の名前を既定値にする。 */
const char *rl_host_player_name(void);
/* HD2D: 新規ゲームの好きな果物の既定値（UI 言語が日本語なら日本語名）。 */
const char *rl_host_default_fruit(void);
char *rl_storage_file(const char *basename);
void rl_game_exit(int code);
void rl_prompt_begin(const char *title);
void rl_item_prompt_begin(const char *title);
void rl_prompt_add_choice(int key, const char *label);
void rl_prompt_clear(void);
void rl_message_record(const char *text);
void rl_text_input_begin(void);
void rl_text_input_end(void);
void rl_player_name_input_set(int active);
/* HD2D: 設定一覧の文字列の行の編集（今の値から始め、BS は UTF-8 の 1 字ずつ）。rl_curses.c */
struct rl_window;
int rl_option_edit_string(struct rl_window *win, int oy, int ox, char *opt, int cap, int first, int (*read_key)(void));
void rl_command_input_begin(void);
void rl_command_input_end(void);
/* HD2D: 繰り返し回数の入力（数字の前置き）を画面へ知らせる。rl_curses.c */
void rl_count_input_begin(int value, int max);
void rl_count_input_set(int value);
void rl_count_input_end(void);
/*!
 * HD2D: 音の出来事。コアは鳴らさず、
 * 「そのマスで音が要る」出来事だけを `rl_sound.c` の環へ積む。汲むのは
 * `rl_frame.cpp`。定義は `rl_sound.h`（この一行だけをここへ貸す——
 * 原作の .c は force-include でこのヘッダしか見ないため）。
 */
void rl_sound_push(const char *name, int y, int x);

/*
 * Legacy Rogue assumes it owns the whole Unix process and freely rewrites
 * process-wide signal dispositions.  Android cores are shared libraries in
 * the HD2D process, so that would also replace the UI/render/audio handlers.
 * Prepared sources route those calls through these wrappers.  Desktop builds
 * keep the native behavior; Android treats them as core-local no-ops.
 */
typedef void (*rl_signal_handler)(int);
rl_signal_handler rl_game_signal(int sig, rl_signal_handler handler);
unsigned int rl_game_alarm(unsigned int seconds);
int rl_game_kill(int pid, int sig);

/* Never let a legacy core terminate the Android host process. */
#define exit(code) rl_game_exit(code)
