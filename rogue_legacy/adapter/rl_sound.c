/*!
 * @file rl_sound.c
 * @brief `rl_sound.h` の実装。`fc_shim.c` の `fc_sound_*` と同じ環。
 */
#include "rl_sound.h"

#include <string.h>

#define RL_SOUND_MAX 32

static rl_sound_event rl_sound_ring[RL_SOUND_MAX];
static int rl_sound_num = 0;
static int rl_sound_wanted = 0;

void rl_sound_set_wanted(int wanted)
{
    rl_sound_wanted = wanted ? 1 : 0;
    if (!rl_sound_wanted) {
        /* 欲しがらなくなったら溜まっているぶんも捨てる（古い音が後で鳴らないように）。 */
        rl_sound_num = 0;
    }
}

void rl_sound_push(const char *name, int y, int x)
{
    rl_sound_event *ev;

    if (!rl_sound_wanted) {
        return;
    }
    if ((name == NULL) || (name[0] == '\0')) {
        return;
    }

    if (rl_sound_num >= RL_SOUND_MAX) {
        /* 溢れたら最も古い 1 つを捨てる。 */
        int i;
        for (i = 1; i < RL_SOUND_MAX; i++) {
            rl_sound_ring[i - 1] = rl_sound_ring[i];
        }
        rl_sound_num = RL_SOUND_MAX - 1;
    }

    ev = &rl_sound_ring[rl_sound_num++];
    memset(ev, 0, sizeof(*ev));
    strncpy(ev->name, name, sizeof(ev->name) - 1);
    ev->y = (short)y;
    ev->x = (short)x;
}

int rl_sound_take(rl_sound_event *out, int cap)
{
    int n = rl_sound_num;
    if ((out == NULL) || (cap <= 0)) {
        return 0;
    }
    if (n > cap) {
        n = cap;
    }
    memcpy(out, rl_sound_ring, (size_t)n * sizeof(rl_sound_event));
    /* 汲んだら消す。音は状態ではなく一度きりの出来事。 */
    rl_sound_num = 0;
    return n;
}
