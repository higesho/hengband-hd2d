#pragma once

#include "rl_build_config.h"

#define SCOREFILE rl_storage_file("rogue54.scr")
#define LOCKFILE rl_storage_file("rogue54.lck")
#define printf rl_game_printf
#define putchar rl_game_putchar

/* 原作 configure.ac の既定（124〜131 行目）: スコアファイルは「歴代最高得点者一覧」
 * ではなく「上位得点の一覧」として数える。設計の判断（2026-09-24）。 */
#define ALLSCORES 1
