/*!
 * @file cp932.h
 * @brief CP932（SJIS）⇄ UTF-8 の変換。**Windows 以外**のための `MultiByteToWideChar` 代役。
 *
 *
 * ## 誰が使うか
 * 旧 C の 2 コア（幻想蛮怒・Sil-Q）は内部が CP932 で、プロトコル v1 の線の上は UTF-8。
 * その境界に立つのが `gensoband/adapter/gb_text.cpp` と `silq/adapter/sq_text.cpp` で、
 * Windows では両方とも `MultiByteToWideChar` / `WideCharToMultiByte` を呼んでいる。
 * **Android にその API は無い**ので、この 2 つがその代わりを務める。
 *
 * ## 約束は Windows 版と同じにする
 * 移植で挙動を変えないことがいちばん大事なので、返し方まで揃える:
 *
 * | | Windows（本家） | ここ |
 * |---|---|---|
 * | SJIS → UTF-8 で読めないバイト | `?`（`WC_*` の既定の置換） | 同じ |
 * | UTF-8 → SJIS で壊れた UTF-8 | 空（`MB_ERR_INVALID_CHARS`） | 同じ |
 * | UTF-8 → SJIS で CP932 に無い字 | 空（`used_default` を見て捨てる） | 同じ |
 *
 * 表は `cp932_table.h`（`tools/gen_cp932_table.py` が Python の `cp932` コーデックから焼く）。
 */
#pragma once

#include <cstddef>
#include <string>

namespace portable {

/*!
 * @brief CP932（SJIS）のバイト列を UTF-8 にする。
 * @details 翻せないバイトは `?` に落とす。**空を返さない**——ミラーの 1 行が丸ごと
 * 消えるより、化けた 1 文字が見えるほうが調べやすい。
 */
std::string cp932_to_utf8(const char *bytes, std::size_t len);

/*!
 * @brief UTF-8 を CP932（SJIS）にする。
 * @return 翻せなければ**空文字列**（壊れた UTF-8 か、CP932 に無い字が 1 つでもある場合）。
 * 呼び手が「捨てる」を選べるように、部分的な成功を返さない。
 */
std::string utf8_to_cp932(const std::string &utf8);

} // namespace portable
