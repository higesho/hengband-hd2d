// DCSS コアの差し替え用ヘッダ（POSIX の <sys/un.h> の互換版）。
//
// 原作の tileweb.h が #include <sys/un.h> して、sockaddr_un を値として持つ。
// MSVC には無いので、同じ名前のヘッダをこのフォルダに置いて差し替える（原作は書き換えない）。
// 中身は構造体 1 つだけ。ソケットの実体は dcss_posix_shim.cpp にある仮の実装で、
// アドレスの中身は読まれない（送り先は 1 つしか無い）。
#pragma once

#ifndef DCSS_SHIM_SYS_UN_H
#define DCSS_SHIM_SYS_UN_H

#ifndef AF_UNIX
#define AF_UNIX 1
#endif
#ifndef PF_UNIX
#define PF_UNIX AF_UNIX
#endif

typedef unsigned short dcss_sa_family_t;

struct sockaddr_un
{
    dcss_sa_family_t sun_family;
    char sun_path[108];
};

#endif
