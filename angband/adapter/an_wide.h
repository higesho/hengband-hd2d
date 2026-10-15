/*!
 * @file an_wide.h
 * @brief 全角 2 セル（埋め字のセル）の判定と展開。
 *
 * @details Angband 4.2 の Term は 1 字 1 セル（`wchar_t`）。日本語では**全角 1 字の
 * 直後に埋め字のセル（`AN_WIDE_PAD`）を 1 つ積み**、Term 上で 2 セルにする。
 * どの関数も**日本語層が寝ている（`an_lang_enabled()` が偽）ときは何もしない**
 * ——英語の起動は 1 ビットも変わらない（設計 §1 制約 1）。
 *
 * 原作（`ui-term.c` `z-util.c` ほか）からは `prepare_sources.py` の置換で差した
 * 宣言だけを通して呼ばれる（原作の include 経路にアダプタの置き場は無い）。
 */
#ifndef AN_WIDE_H
#define AN_WIDE_H

#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! 埋め字（私用領域）。`L' '` にしない理由は設計 §3.1。 */
#define AN_WIDE_PAD ((wchar_t)0xF8FF)

/*! 全角（East Asian Width の W/F）なら 2、それ以外は 1、`AN_WIDE_PAD` は 0。 */
int an_wcwidth(unsigned long wc);

/*!
 * @brief `s`（NUL 終わり、容量 `cap` 要素）の全角の直後に埋め字を積む（その場で）。
 * @details 容量が尽きたら、そこで切る（全角の片割れは残さない）。日本語層が寝て
 * いれば何もしない。
 */
void an_wide_expand(wchar_t *s, int cap);

/*!
 * @brief `s` の先頭 `n` セルで切るとき、切れ目が「全角とその埋め字の間」なら
 * 全角ごと落とした `n` を返す。日本語層が寝ていれば `n` のまま。
 */
int an_wide_clip(const wchar_t *s, int n);

/*! UTF-8 の字列の桁数（全角 2・半角 1）。 */
size_t an_utf8_cols(const char *s);

/*! UTF-8 の字列を `cols` 桁までで切る（全角を割らない）。 */
void an_utf8_clip_cols(char *s, size_t cols);

/*!
 * @brief `utf8_strlen()` / `utf8_clipto()`（`z-util.c`）の差し替え口（フック #9）。
 * @return 日本語層が起きていて差し替えたなら 1（`*out` に結果）、寝ていれば 0（原作の道へ）。
 */
int an_utf8_strlen_hook(const char *s, size_t *out);
int an_utf8_clipto_hook(char *s, size_t n);

/*! 行頭に来てはいけない字か（禁則。`。、」』）！？ー` と小書きの仮名）。 */
int an_kinsoku_head(unsigned long wc);

/*!
 * @name 説明文の折り（フック #5。`z-textblock.c`）
 * @details 説明文（`textblock`）は字を `wchar_t` の並びで持ち、`display_area()` が
 * 1 字 1 セルで Term へ置く。日本語では (1) 足した字の全角の直後に埋め字を積み
 * （`an_wide_expand_len()`。場所は `an_wide_room()` で先に取る）、(2) 折るときは
 * 空白だけでなく全角の境でも折り、行頭の禁則を守る（`an_textblock_lines()`）。
 * @{
 */
size_t an_wide_room(size_t n);
size_t an_wide_expand_len(wchar_t *s, size_t len, size_t cap);
/*!
 * @return 日本語層が寝ていれば 0（原作の `textblock_calculate_lines()` の道へ）。
 *   組んだら 1（`*out_lines` に行数。行の記録は原作と同じく `mem_realloc()` で伸ばす）。
 */
int an_textblock_lines(const wchar_t *text, size_t n, size_t **line_starts, size_t **line_lengths, size_t width,
    size_t *out_lines);
/*! @} */

#ifdef __cplusplus
}
#endif

#endif /* AN_WIDE_H */
