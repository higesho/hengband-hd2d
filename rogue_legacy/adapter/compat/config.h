#pragma once

/* configure replacement used only by the HD2D core build. */
#define HAVE_CURSES_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_TYPES 1
#define HAVE_LIMITS_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STDINT_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_MEMSET 1
#define HAVE_STRCHR 1
#define HAVE_STRERROR 1
#define HAVE_VPRINTF 1
#define HAVE_ERASECHAR 1
#define HAVE_KILLCHAR 1
#define HAVE_ERRNO_H 1

#if defined(_WIN32)
#define HAVE_PROCESS_H 1
#else
#define HAVE_UNISTD_H 1
#define HAVE_PWD_H 1
#define HAVE_GETGID 1
#define HAVE_GETUID 1
#define HAVE_GETPWUID 1
#define HAVE_SETGID 1
#define HAVE_SETUID 1
#endif
