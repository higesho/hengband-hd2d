#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct r2_host_hooks {
    void (*present)(void);
    int (*next_key)(void);
    void (*sleep_ms)(int milliseconds);
    int (*shutdown_requested)(void);
    /* 先頭に並んでいるキャンセル（Esc / Ctrl-C）を 1 つ取り出せたら 1。休息・走行の中断用。 */
    int (*take_interrupt)(void);
} r2_host_hooks;

void r2_set_host_hooks(const r2_host_hooks *hooks);
void r2_host_present(void);
int r2_host_next_key(void);
void r2_host_sleep_ms(int milliseconds);
int r2_host_shutdown_requested(void);
int r2_host_take_interrupt(void);

#ifdef __cplusplus
}
#endif
