/*!
 * @file tb_win_stubs.c
 * @brief `main-win.c` を組まないことで宙に浮く記号の受け皿（設計 §3.3）。
 *
 * 幻想蛮怒の `gb_win_stubs.c` は、`main-win.c` の中にある BGM の口 2 つ
 * （`find_mon_music_priority()` と `init_music_hack()`）を外から与えていた。
 * TOband（変愚蛮怒 1.5.1 系）には BGM の仕組みが無く、`main-win.c` を外して宙に浮く記号は
 * 組んで確かめたところ **無かった**（リンクエラー 0）。
 *
 * もう 1 つ、`make_screen_dump()` が宙に浮くが、あれは `main-win.c` ではなく `report.c`
 * の中にある。`report.c` を組まない代わりの受け皿は `tb_report_stub.c` に置いてある。
 *
 * 今後、組み立てに `main-win.c` 由来の記号が必要になったらここに足す。
 */

#include "angband.h"
