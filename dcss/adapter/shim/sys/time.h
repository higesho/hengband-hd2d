// DCSS コアの差し替え用ヘッダ（POSIX の <sys/time.h> の互換版）。
//
// 原作の tileweb.cc が gettimeofday() と timeval を使う。MSVC には無いので、
// このフォルダで差し替える。timeval の定義は sys/socket.h と共通にしてある。
#pragma once

#ifndef DCSS_SHIM_SYS_TIME_H
#define DCSS_SHIM_SYS_TIME_H

#include <sys/socket.h>

int gettimeofday(timeval *tv, void *tz);

// tileweb.cc は <unistd.h> の usleep() も使う。実体は原作の syscalls.cc にある
// （TARGET_OS_WINDOWS では Sleep() を呼ぶ版。引数は 1000 の倍数でなければならない）。
// syscalls.h の宣言と同じ形にしてある。
void usleep(unsigned long time);

#endif
