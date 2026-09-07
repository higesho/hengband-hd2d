/*!
 * @file gb_lang_c.h
 * @brief 英語層の**C の皮**。`gensoband/src` から見えるのはこの 1 枚だけ。
 *
 * 基準は （フック）。手本は Sil-Q の
 * `silq/adapter/sq_lang_c.h`（日本語化 M1）——あちらの**逆向き**である。
 *
 * ## 呼ぶ側の作法（設計 §5）
 * - どの穴も **`gb_lang_enabled()` が偽なら即座に元の道へ落ちる**（設計 §2 制約 1）。
 *   ここの関数は自分でもそれを見るので、呼び側で先に見る要は無い。
 * - **引けなければ原文（日本語）をそのまま返す**（設計 §2 制約 4）。
 *   空も NULL も返さない。JP ビルドは常に SJIS を正しく描けるので、未訳は化けずに
 *   日本語のまま出る。
 * - 差し込んだ行には `//GB:` の印を付ける（設計 §2 制約 2・親契約 §7）。
 *
 * ## 文字コード
 * ここを出入りする文字列は**すべて CP932**である。鍵は日本語（0x80 以上を含む）、
 * 訳は ASCII。**0x80 以上を 1 バイトも含まない文字列は引かずに返す**——
 * 訳し終わった英語・数字・記号・桁合わせの空白がそれで、二重引きも起きない。
 *
 * ## 実装
 * すべて `gb_lang.cpp`（カタログ＝ハッシュ表を持つので C++）。Sil-Q と違い
 * バイト算の TU は無い——英語は ASCII ⊂ CP932 で、切りも折りも今のままでよい。
 */
#ifndef GENSOBAND_ADAPTER_GB_LANG_C_H
#define GENSOBAND_ADAPTER_GB_LANG_C_H

/* `gb_fprintf()` の `FILE *` のため。ここを include する C++ の TU
 * （`gb_frame.cpp` など）が angband.h を見ていないので、ここで引く。 */
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief 英語層が生きているか。
 * @return 0 = 日本語（今までと 1 ビットも違わない動き）／1 = 英語
 */
int gb_lang_enabled(void);

/*!
 * @brief 表示文字列をカタログで引く。
 * @param s 原文（CP932。NULL 可）
 * @return 訳文（静的な寿命。解放しない）。引けなければ `s` をそのまま返す
 */
const char *gb_tr(const char *s);

/*!
 * @brief `Term_addstr()` 用（フック #1）。
 * @param s 原文（CP932。NULL 可）
 * @param n 長さ引数の置き場（`Term_addstr` の `n`）
 * @return 訳文か `s`
 *
 * @details **全文の描画（`*n < 0` か `*n >= strlen(s)`）のときだけ**引く。
 * 部分文字列の描画（`*n < strlen(s)`）は触らない——切り出しの意味が壊れる。
 * 引けたら `*n = -1` に直す。訳は長さが変わる（英語は日本語より長いことが多い）ので、
 * 元の `n` のままだと途中で切れる。
 */
const char *gb_tr_addstr(const char *s, int *n);

/*!
 * @brief **書式そのもの**を引く（フック #2。`vstrnfmt()` の頭）。
 * @details 変換指定（`%s` `%d` など）の**並びが日英で一致する項目しか
 * カタログに入っていない**（読み込みのときに弾く）。
 */
const char *gb_tr_fmt(const char *fmt);

/*!
 * @brief `%s` の**引数として渡ってくる断片**を引く（フック #3）。
 * @details 引けなくても未訳一覧へ**積まない**——`%s` には敵名・品名・
 * プレイヤ名・パス・数字も流れてくる（Sil-Q #9 と同じ決め）。
 */
const char *gb_tr_arg(const char *s);

/*!
 * @brief `object_desc()` の結果を英語の形へ直す（フック #6。E2）。
 * @param obj `object_type *`（型を見せないため void*）
 * @details JP の組み立て（数詞・エゴと銘の前置・冠詞なし）を後処理で直す。
 * 実装は `gb_name_en.c`（幻想蛮怒のヘッダを見てよい C の TU）。
 */
void gb_object_desc_en_fix(char *buf, void *obj, unsigned long mode);

/*!
 * @brief `monster_desc()` の結果を英語の形へ直す（フック #7。E2）。
 * @param mon `monster_type *`
 */
void gb_monster_desc_en_fix(char *desc, void *mon, unsigned long mode);

/*!
 * @brief 言語で変わる**ファイル名**の引き替え（フック #8 #9）。
 * @param name `get_rnd_line()` / `show_file()` に渡ったファイル名
 * @return 英語版のファイル名（静的な寿命）。引き替えないときは `name` のまま
 *
 * @details `thelp.hlp`→`help.hlp`・`〜_j.txt`→`〜.txt`・`〜_jp.txt`→`〜.txt`。
 * **引き替え先が lib/file か lib/help に実在するときだけ**替える
 * （無ければ原名のまま＝日本語の中身が出る。設計 §2 制約 4）。
 *
 * **ヘルプの引き替えは `gb_lang_set_en_help(1)` で止まる**（E7）。
 */
const char *gb_lang_file(const char *name);

/*!
 * @brief 英訳したヘルプの置き場（`gensoband/lib-en/help`）を使うと申告する（E7）。
 * @param on 1 = 使う（`ANGBAND_DIR_HELP` を差し替えた）／0 = 使わない
 *
 * @details 使うときは `thelp.hlp` → `help.hlp` の引き替え（#9）を**止める**。
 * 引き替えたままだと `lib-en/help/help.hlp`＝**変愚蛮怒の古い英語ヘルプ**が開き、
 * 幻想蛮怒の英訳版へ辿り着けない。呼ぶのは `gb_bootstrap()` の 1 か所だけで、
 * 最初の `show_file()` より前である（`gb_lang_file()` は答えを憶えるため）。
 */
void gb_lang_set_en_help(int on);

/*!
 * @brief `fprintf()` の代わり（フック **#29**。§11.13）。
 * @param fff 書き出し先
 * @param fmt 書式（CP932）
 *
 * @details 知識の画面（`~`）と日記（`|`）は一時ファイルへ `fprintf` で書いてから
 * `show_file()` で見せるが、**`fprintf` はフックを 1 つも通らない**——
 * 書式（#2）も `%s` の引数（#3）も引けない。`show_file()` は 1 行ずつ描くので
 * #1 は「行がまるごと鍵に合うとき」しか効かず、値で埋めた行
 * （`合計: %lu 体を倒した。`）も頭に空白の付く行（`   なし`）も外れる。
 *
 * ここは `vstrnfmt()` を挟むだけである。`vstrnfmt()` の頭に #2 が、`%s` の
 * 取り出しに #3 が在るので、書式も引数も引かれる。**日本語モードでは
 * `gb_lang_enabled()` が偽で引きが素通りする**ので、出来上がりは今までと同じ。
 *
 * 実装は `gb_shim.c`（`vstrnfmt()` を呼ぶので幻想蛮怒のヘッダが要る C の TU）。
 */
void gb_fprintf(FILE *fff, const char *fmt, ...);

/*!
 * @brief `fputs()` の代わり（フック **#29**）。引数の順は `fputs()` と同じ。
 * @param s 原文（CP932）
 * @param fff 書き出し先
 */
void gb_fputs(const char *s, FILE *fff);

#ifdef __cplusplus
}
#endif

#endif /* GENSOBAND_ADAPTER_GB_LANG_C_H */
