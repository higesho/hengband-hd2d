/*!
 * @file tb_report_stub.c
 * @brief 原作の `report.c` の代わり。スコアを外へ送らない（設計 §1 制約 5・§3.3）。
 *
 * 原作の `report_score()` は死亡時に「送るか」を聞いて、フォーク作者のサーバーへ
 * WinHTTP でダンプを送る。TOband コアは `report.c` を組まず、ここで受ける。
 * 何もしないので、送るかどうかの問いも出ない。
 */

#include "angband.h"

void report_score(void)
{
}

/*!
 * `make_screen_dump()` も `report.c` の中にある（死亡・セーブ時に画面の HTML を取る）。
 * `report.c` を組まないので、ここで受ける。NULL を返すと呼び手（`effects.c`・`xtra2.c`）は
 * 画面の記録を付けないだけで、セーブ・遊びには影響しない。
 */
cptr make_screen_dump(void)
{
    return NULL;
}
