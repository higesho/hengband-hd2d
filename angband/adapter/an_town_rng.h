/*!
 * @file an_town_rng.h
 * @brief 町を作る間だけ乱数の種を固定する。
 *
 * @details 原作の `gen-cave.c` の `town_gen()` が**保存済みの町が無いとき**
 * `town_gen_layout()` を呼ぶ直前に `an_town_rng_begin()` を、呼んだ直後に
 * `an_town_rng_end()` を呼ぶ（`tools/angband/prepare_sources.py` が当てる、
 * 弱い既定つきの流儀。実体はここ）。
 *
 * `an_town_rng_begin()` は Angband の乱数の状態一式（`z-rand.c` の
 * `Rand_quick` / `Rand_value` / `state_i` / `STATE[]` / `z0` / `z1` / `z2`）を
 * 退避してから、決まった種で `Rand_state_init()` をやり直す。
 * `an_town_rng_end()` は退避した状態をそのまま書き戻す——**町を作ったことで
 * ゲームのほかの乱数の流れが 1 歩も変わらない**ようにするため。
 *
 * 種の値は `AN_TOWN_RNG_SEED`（この 1 か所の定数）。検査・候補地図づくりの
 * 道具は環境変数 `ANGBAND_TOWN_SEED` で上書きできる（`an_town_rng_seed()`）
 * ——利用の道具が実行体を種の数だけ作り直さずに済むようにするための、
 * 検査専用の抜け道。実際に遊ぶときはこの環境変数を立てないので、
 * `AN_TOWN_RNG_SEED` が効く。
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! @brief 既定の種（遊ぶ人が候補から選ぶまでの仮の値）。 */
#define AN_TOWN_RNG_SEED_DEFAULT 1u

/*!
 * @brief いま使う種を返す。環境変数 `ANGBAND_TOWN_SEED`（10 進の非負整数）が
 * 立っていればそれを、無ければ `AN_TOWN_RNG_SEED_DEFAULT` を返す。
 */
uint32_t an_town_rng_seed(void);

/*!
 * @brief `gen-cave.c` の A5 の強シンボル。乱数の状態一式を退避し、
 * `an_town_rng_seed()` の種で作り直す。**`an_town_rng_end()` と対で呼ぶこと**
 * （入れ子にしない。1 回の町づくりにつき 1 対）。
 */
void an_town_rng_begin(void);

/*!
 * @brief `an_town_rng_begin()` が退避した乱数の状態一式を書き戻す。
 */
void an_town_rng_end(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
