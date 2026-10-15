#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rl_host_hooks {
    void (*present)(void);
    int (*next_key)(void);
    void (*sleep_ms)(int milliseconds);
    int (*shutdown_requested)(void);
} rl_host_hooks;

void rl_set_host_hooks(const rl_host_hooks *hooks);
void rl_host_present(void);
int rl_host_next_key(void);
void rl_host_sleep_ms(int milliseconds);
int rl_host_shutdown_requested(void);
void rl_set_home_dir(const char *path);
const char *rl_host_home_dir(void);
void rl_set_player_name(const char *utf8_or_ascii);
const char *rl_host_player_name(void);
void rl_set_default_fruit(const char *utf8_or_ascii);
const char *rl_host_default_fruit(void);

#ifdef __cplusplus
}
#endif
