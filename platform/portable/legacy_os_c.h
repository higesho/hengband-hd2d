/*!
 * @file legacy_os_c.h
 * @brief `legacy_os.h` のうち、**C から呼ぶ分**だけの口。
 *
 * `gensoband/adapter/gb_bootstrap.c` と `silq/adapter/sq_bootstrap.c` は C である
 * （コアと同じ翻訳単位の作法で書いてあり、`setjmp` で跳ぶ）。C++ の
 * `std::string` を跨げないので、生のポインタで受ける薄い口をここに置く。
 *
 * 実体は `legacy_os.cpp`（＝Windows / POSIX の分岐は結局あの 1 ファイルに閉じている）。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

//! ディレクトリとして在れば 1。`portable::dir_exists` と同じ判定。
int portable_dir_exists(const char *path);

//! ディレクトリを 1 段作る。**既に在れば 1**。`portable::make_dir` と同じ。
int portable_make_dir(const char *path);

/*!
 * @brief ディレクトリの中のファイル名を並べる（`portable::list_files` の C 版）。
 *
 * @param dir 見る所
 * @param out 受け皿。**`stride` バイトごとに 1 件**（名前は必ず NUL で終わる）
 * @param stride 1 件ぶんの幅（バイト）。溢れる名前は切って入れる
 * @param max 受け皿に入る件数
 * @return 入れた件数（0 なら 1 件も無いか、`dir` が読めない）
 *
 * @details 並び順は OS が返したまま——**要るなら呼ぶ側で並べ替えること**。
 * ディレクトリは返らない（ファイルだけ）。
 */
int portable_list_files(const char *dir, char *out, int stride, int max);

#ifdef __cplusplus
} /* extern "C" */
#endif
