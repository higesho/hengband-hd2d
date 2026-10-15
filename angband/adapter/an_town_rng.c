/*!
 * @file an_town_rng.c
 * @brief `an_town_rng.h` の実体。
 */
#include "an_town_rng.h"

#include "z-rand.h"

#include <stdio.h>
#include <stdlib.h>

/*! @brief 退避した乱数の状態一式（`z-rand.c` のグローバル）。 */
static bool s_saved_quick;
static uint32_t s_saved_value;
static uint32_t s_saved_state_i;
static uint32_t s_saved_state[RAND_DEG];
static uint32_t s_saved_z0, s_saved_z1, s_saved_z2;

/*! @brief begin/end の対が崩れていないかの見張り（入れ子・二重終了を防ぐ）。 */
static int s_active = 0;

uint32_t an_town_rng_seed(void)
{
    const char *env = getenv("ANGBAND_TOWN_SEED");
    if (env != NULL && env[0] != '\0') {
        return (uint32_t)strtoul(env, NULL, 10);
    }
    return AN_TOWN_RNG_SEED_DEFAULT;
}

void an_town_rng_begin(void)
{
    int i;

    if (s_active) {
        /* 入れ子で呼ばれた——town_gen_layout() は 1 回の町づくりで 1 度しか
         * 呼ばれないはずなので、これは A5 の当て方自体が壊れているサイン。
         * 状態を壊さないよう、退避し直さずにそのまま数える。 */
        fprintf(stderr, "[angband] an_town_rng_begin() called while already active (ignored)\n");
        return;
    }
    s_active = 1;

    s_saved_quick = Rand_quick;
    s_saved_value = Rand_value;
    s_saved_state_i = state_i;
    for (i = 0; i < RAND_DEG; i++) {
        s_saved_state[i] = STATE[i];
    }
    s_saved_z0 = z0;
    s_saved_z1 = z1;
    s_saved_z2 = z2;

    Rand_quick = false;
    Rand_state_init(an_town_rng_seed());
}

void an_town_rng_end(void)
{
    int i;

    if (!s_active) {
        fprintf(stderr, "[angband] an_town_rng_end() called without a matching begin() (ignored)\n");
        return;
    }

    Rand_quick = s_saved_quick;
    Rand_value = s_saved_value;
    state_i = s_saved_state_i;
    for (i = 0; i < RAND_DEG; i++) {
        STATE[i] = s_saved_state[i];
    }
    z0 = s_saved_z0;
    z1 = s_saved_z1;
    z2 = s_saved_z2;

    s_active = 0;
}
