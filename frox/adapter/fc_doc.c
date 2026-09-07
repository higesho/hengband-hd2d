/*!
 * @file fc_doc.c
 * @brief `doc_printf()` / `doc_cprintf()` の展開の肩代わり（フック #13）。
 *
 * 基準は 追記 (d)。
 *
 * ## なぜここが要るか
 * doc の展開は `string_vprintf()`（`c-string.c:180`）で、中身は素の `vsnprintf`
 * ——ゲームの `vstrnfmt()` を通らないので、書式（#2）も `%s` の引数（#3）も
 * カタログへ届かない。ここで `vstrnfmt()` へ道を通すと、既に居る 2 つの
 * フックがそのまま効く。
 *
 * ## doc の旗
 * 展開の間だけ `fc_lang_doc_scope(1)` を立てる。裸の 1 語の鍵
 * （種族名 `Dunadan`・職業名 `Warrior`…）は**この旗の中でしか引かれない**
 * ——`%s` にはファイル名も流れてくる（`autopick.c:925` の
 * `format("%s-%s.prf", namebase, player_base)`）ので、素で引くと同名の
 * キャラ名や pref 名まで訳してファイルが開けなくなる。doc の展開の結果は
 * **画面にしか行かない**から、ここでだけ許すのは安全である。
 *
 * ## 再入について
 * `vstrnfmt()` はここへ戻ってこない（doc_printf を呼ばない）。ゲームの
 * スレッド 1 本しか来ないのでロックは要らない（`fc_lang.cpp` と同じ）。
 */

#include "angband.h"

#include "fc_lang_c.h"

int fc_doc_vprintf(void *string_ptr_s, const char *fmt, va_list vp)
{
    /*
     * 4096 は Term の 1 行（最大 255 桁）にも doc の 1 段落にも足りる。
     * 万一あふれたら vstrnfmt() が黙って切り詰める——英語の道（string_vprintf）
     * は伸びる器なので、ここだけ上限がある。doc_printf で 4KB を超える塊を
     * 流す呼び手は無い（大きな文は doc_insert のリテラルで来る）。
     */
    char buf[4096];

    if (!fc_lang_enabled()) return 0; /* vp には触れていない（制約 1） */

    fc_lang_doc_scope(1);
    vstrnfmt(buf, sizeof(buf), fmt, vp);
    fc_lang_doc_scope(0);

    string_append_s((string_ptr)string_ptr_s, buf);
    return 1;
}
