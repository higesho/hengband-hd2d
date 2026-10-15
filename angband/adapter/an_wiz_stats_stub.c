/*!
 * @file an_wiz_stats_stub.c
 * @brief `src/wiz-stats.c` を除いたことで欠ける 4 つの記号の穴埋め。
 *
 * （組む範囲。`wiz-stats.c` を除いた 150 本）。
 *
 * ## なぜ要るか
 * `cmd-wizard.c` の `do_cmd_wiz_collect_{disconnect,obj_mon,pit}_stats()` は
 * `stats_are_enabled()` / `stats_collect()` / `disconnect_stats()` / `pit_stats()`
 * を**プリプロセッサの条件無しで**直に呼ぶ。原作は `wiz-stats.c` 自身の中に
 * `#else / * USE_STATS * /` の無効版（4 関数とも no-op）を持っており、
 * `USE_STATS` を立てなければ**そちらだけがコンパイルされる**仕組みになっている。
 *
 * ここでは `wiz-stats.c`（統計収集の実装。3,329 行、原作でも既定 OFF）を
 * 丸ごと持ち込まずに、**その無効版の 4 関数だけ**を写す。原作の該当ブロック
 * （`wiz-stats.c:3076-3093`）を写した（関数の並び順は同じ。ファイルの体裁
 * ——ヘッダ註記・include・空白行の付け方——はこの adapter の流儀に合わせてある）。
 */

#include "angband.h"
#include "wizard.h"

bool stats_are_enabled(void)
{
    msg("Statistics generation not turned on in this build.");
    return false;
}

void stats_collect(int nsim, int simtype)
{
    (void)nsim;
    (void)simtype;
}

void disconnect_stats(int nsim, bool stop_on_disconnect)
{
    (void)nsim;
    (void)stop_on_disconnect;
}

void pit_stats(int nsim, int pittype, int depth_min, int depth_max)
{
    (void)nsim;
    (void)pittype;
    (void)depth_min;
    (void)depth_max;
}
