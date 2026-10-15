#include "rl_exit.h"

#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

/*
 * The Android cores are shared libraries in the UI process.  Calling libc
 * exit() from Rogue would terminate the whole application.  Keep the jump
 * boundary entirely in C so no C++ automatic object is skipped by longjmp.
 * Only one legacy Rogue core is active at a time in the current host.
 */
static jmp_buf g_exit_target;
static int g_exit_active;
static int g_exit_code;

rl_signal_handler rl_game_signal(int sig, rl_signal_handler handler)
{
#if defined(HENGBAND_ANDROID)
    (void)sig;
    /* Report the requested handler as the previous one.  Old Rogue has a few
       "install if not already installed" comparisons; this makes them settle
       without ever changing the host process disposition. */
    return handler;
#else
    return signal(sig, handler);
#endif
}

unsigned int rl_game_alarm(unsigned int seconds)
{
#if defined(HENGBAND_ANDROID) || defined(_WIN32)
    (void)seconds;
    return 0;
#else
    return alarm(seconds);
#endif
}

int rl_game_kill(int pid, int sig)
{
#if defined(HENGBAND_ANDROID) || defined(_WIN32)
    (void)pid;
    (void)sig;
    return 0;
#else
    return kill(pid, sig);
#endif
}

int rl_run_game(rl_game_main_fn game_main, int argc, char **argv, char **envp)
{
    int jumped;
    if (game_main == NULL) return 127;
    g_exit_active = 1;
    jumped = setjmp(g_exit_target);
    if (jumped == 0) {
        const int result = game_main(argc, argv, envp);
        g_exit_active = 0;
        return result;
    }
    g_exit_active = 0;
    return g_exit_code;
}

void rl_game_exit(int code)
{
    if (g_exit_active) {
        g_exit_code = code;
        longjmp(g_exit_target, 1);
    }
    /* This should only be reachable if a future caller invokes the shim
       outside rl_run_game().  Do not recurse through the exit macro. */
    (void)code;
    abort();
}
