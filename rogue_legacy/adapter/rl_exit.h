#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*rl_game_main_fn)(int argc, char **argv, char **envp);

/* Run legacy Rogue behind an exit() boundary. */
int rl_run_game(rl_game_main_fn game_main, int argc, char **argv, char **envp);

/* Replacement for legacy exit().  It returns control to rl_run_game(). */
void rl_game_exit(int code);

#ifdef __cplusplus
}
#endif
