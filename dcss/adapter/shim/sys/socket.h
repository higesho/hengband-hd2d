// DCSS コアの差し替え用ヘッダ（POSIX の <sys/socket.h> の互換版）。
//
// 原作の tileweb.cc は UNIX ドメインの SOCK_DGRAM ソケットで、ウェブサーバと JSON を
// やり取りする。Windows には SOCK_DGRAM の UNIX ソケットが無いので、同じ名前の関数
// （socket・bind・setsockopt・sendto・recvfrom・select・pselect・sigprocmask…）を
// このヘッダで宣言し、実体を dcss_posix_shim.cpp の仮の実装（標準入出力の行）にする。
// こうすれば tileweb.cc を 1 行も書き換えずに組める。
//
// 前提: tileweb.cc だけを /D_WINSOCKAPI_ 付き・プリコンパイル済みヘッダなしで組む
// （windows.h が winsock.h を引き込んで timeval や select を二重に宣言するのを防ぐ）。
// 宣言は関数・型だけで、マクロで関数名を置き換えない（menu.h の select() などを壊さないため）。
#pragma once

#ifndef DCSS_SHIM_SYS_SOCKET_H
#define DCSS_SHIM_SYS_SOCKET_H

#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#ifndef _WINSOCKAPI_
#error "tileweb.cc is built with /D_WINSOCKAPI_ so that winsock.h does not clash with this shim"
#endif

#include <sys/un.h>

typedef int socklen_t;

#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif
#ifndef SOL_SOCKET
#define SOL_SOCKET 0xffff
#endif
#ifndef SO_SNDBUF
#define SO_SNDBUF 0x1001
#endif
#ifndef SO_SNDTIMEO
#define SO_SNDTIMEO 0x1005
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

struct sockaddr
{
    dcss_sa_family_t sa_family;
    char sa_data[14];
};

struct timeval
{
    long tv_sec;
    long tv_usec;
};

// fd_set: 小さい番号（標準入力 0 と仮のソケット）だけを扱う。
#define DCSS_FD_SETSIZE 1024
struct fd_set
{
    unsigned char dcss_bits[DCSS_FD_SETSIZE / 8];
};
#define FD_ZERO(s) memset((s)->dcss_bits, 0, sizeof((s)->dcss_bits))
#define FD_SET(fd, s) ((s)->dcss_bits[(fd) >> 3] |= (unsigned char)(1u << ((fd) & 7)))
#define FD_CLR(fd, s) ((s)->dcss_bits[(fd) >> 3] &= (unsigned char)~(1u << ((fd) & 7)))
#define FD_ISSET(fd, s) (((s)->dcss_bits[(fd) >> 3] >> ((fd) & 7)) & 1)

// シグナル（tileweb.cc は SIGWINCH だけを待つ。Windows では来ないので何もしない）。
typedef unsigned long sigset_t;
#ifndef SIGWINCH
#define SIGWINCH 28
#endif
#ifndef SIG_BLOCK
#define SIG_BLOCK 0
#endif
#ifndef SIG_SETMASK
#define SIG_SETMASK 2
#endif
int sigemptyset(sigset_t *set);
int sigaddset(sigset_t *set, int signo);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

int socket(int domain, int type, int protocol);
int bind(int fd, const sockaddr *addr, socklen_t len);
int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen);
ptrdiff_t sendto(int fd, const void *buf, size_t len, int flags,
                 const sockaddr *dest, socklen_t destlen);
ptrdiff_t recvfrom(int fd, void *buf, size_t len, int flags,
                   sockaddr *src, socklen_t *srclen);
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           timeval *timeout);
int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
            const timespec *timeout, const sigset_t *sigmask);

#endif
