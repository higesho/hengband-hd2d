/*!
 * @file android_iconv.h
 * @brief `iconv` の代替（UTF-8 ⇄ EUC-JP のみ）の宣言
 *
 * 詳しい経緯は `android_iconv.cpp` の冒頭を読むこと。要点だけ:
 * **bionic の `iconv` は EUC-JP を扱えない**ので、コアが呼ぶ `iconv_open` /
 * `iconv` / `iconv_close` を、CMake のマクロ（`-Diconv_open=hb_iconv_open` …）で
 * ここへ差し替える。`src/` は書き換えない。
 *
 * シグネチャは bionic の `<iconv.h>` と一致させてある（`iconv_t` は `void *`）。
 */
#pragma once

#include <cstddef>

extern "C" {

void *hb_iconv_open(const char *tocode, const char *fromcode);
int hb_iconv_close(void *cd);
size_t hb_iconv(void *cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft);

} // extern "C"
