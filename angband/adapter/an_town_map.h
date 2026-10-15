/*!
 * @file an_town_map.h
 * @brief 手で描いた地図のファイルを読んで町を作る。
 *
 * @details 原作の `gen-cave.c` の `town_gen()` が**保存済みの町が無いとき**、
 * まず `an_town_build_from_file()` を試す（`tools/angband/prepare_sources.py` が
 * 当てる、弱い既定つきの流儀。実体はここ）。**偽を返したときは、呼び出し元が
 * そのまま原作の `town_gen_layout()`（乱数で作る、いつもの作り方）へ落ちる**
 * ——地図のファイルが無いとき・読めないとき・中身が壊れているときの、どれでも
 * 安全に元の作り方へ戻るための約束。
 */
#pragma once

#include <stdbool.h>

struct chunk;
struct player;

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief `gen-cave.c` の A5 の強シンボル。`angband/lib/gamedata/hd2d_town.txt`
 * （`an_town_map_path()`）を読み、書式が正しければ `c` へ地形を敷いて
 * `player_place()` まで済ませ、`true` を返す。
 *
 * ファイルが無い・開けない・行数や桁数が合わない・知らない字がある・
 * 店の入口（`1`〜`8`）が 8 種そろっていない・下り階段（`>`）や開始位置（`@`）が
 * 無い——**どれも `stderr` へ理由をはっきり書いてから `false` を返す**
 * （地図が無いだけの場合は、これから作るだけなので致命ではない旨を書く）。
 */
bool an_town_build_from_file(struct chunk *c, struct player *p);

#ifdef __cplusplus
} /* extern "C" */
#endif
