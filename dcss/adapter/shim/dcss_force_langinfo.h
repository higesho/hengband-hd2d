// DCSS コアの差し替え用ヘッダ（POSIX の <langinfo.h> の互換版。main.cc にだけ強制的にインクルードする）。
//
// 原作の main.cc は、ウェブタイル版（USE_TILE_WEB）のときだけ
//   if (strcasecmp(nl_langinfo(CODESET), "UTF-8")) { 「UTF-8 のロケールが要る」と言って終了 }
// を実行する。<langinfo.h> は「Windows 以外」の分岐でだけインクルードされる（main.cc:25）ので、
// MSVC では CODESET も nl_langinfo も宣言されない。原作を書き換えずに済ませるため、
// このヘッダを main.cc の /FI（強制インクルード）で先頭に差す。
//
// このコアは文字列を最初から UTF-8 で扱い、標準入出力の JSON も UTF-8 なので、
// ロケールの確認は常に「UTF-8」と答えてよい（実際の確認は画面側の約束）。
#pragma once

#ifndef CODESET
#define CODESET 14
#endif

static inline const char *nl_langinfo(int)
{
    return "UTF-8";
}
