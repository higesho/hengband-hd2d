/*!
 * @file tb_lang_c.h
 * @brief 英語層の**C の皮**。`toband/src` から見えるのはこの 1 枚だけ。
 *
 * （幻想蛮怒）` §5（フック）。手本は Sil-Q の
 * `silq/adapter/sq_lang_c.h`（日本語化 M1）——あちらの**逆向き**である。
 *
 * ## 呼ぶ側の作法（設計 §5）
 * - どの穴も **`tb_lang_enabled()` が偽なら即座に元の道へ落ちる**（設計 §2 制約 1）。
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
 * すべて `tb_lang.cpp`（カタログ＝ハッシュ表を持つので C++）。Sil-Q と違い
 * バイト算の TU は無い——英語は ASCII ⊂ CP932 で、切りも折りも今のままでよい。
 */
#ifndef TOBAND_ADAPTER_TB_LANG_C_H
#define TOBAND_ADAPTER_TB_LANG_C_H

/* `tb_fprintf()` の `FILE *` のため。ここを include する C++ の TU
 * （`tb_frame.cpp` など）が angband.h を見ていないので、ここで引く。 */
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief 英語層が生きているか。
 * @return 0 = 日本語（今までと 1 ビットも違わない動き）／1 = 英語
 */
int tb_lang_enabled(void);

/*!
 * @brief 表示文字列をカタログで引く。
 * @param s 原文（CP932。NULL 可）
 * @return 訳文（静的な寿命。解放しない）。引けなければ `s` をそのまま返す
 */
const char *tb_tr(const char *s);

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
const char *tb_tr_addstr(const char *s, int *n);

/*!
 * @brief **書式そのもの**を引く（フック #2。`vstrnfmt()` の頭）。
 * @details 変換指定（`%s` `%d` など）の**並びが日英で一致する項目しか
 * カタログに入っていない**（読み込みのときに弾く）。
 */
const char *tb_tr_fmt(const char *fmt);

/*!
 * @brief `%s` の**引数として渡ってくる断片**を引く（フック #3）。
 * @details 引けなくても未訳一覧へ**積まない**——`%s` には敵名・品名・
 * プレイヤ名・パス・数字も流れてくる（Sil-Q #9 と同じ決め）。
 */
const char *tb_tr_arg(const char *s);

/*!
 * @brief `object_desc()` の英語の組み立て（フック **#6**。`object_desc()` の頭で呼ぶ）。
 * @param buf 出力（`MAX_NLEN` バイト）
 * @param obj `object_type *`（型を見せないため void*）
 * @param mode `object_desc()` の mode
 * @return 1 = 英語で組んで `buf` に書いた（呼び側はそのまま戻る）／0 = 何もしていない（日本語モード・入れ子の呼び出し）
 *
 * @details 個数・冠詞・種別の語・エゴと銘・複数形は、`object_type` と `k_info` の英語の名前から**構造で**組み直す。
 * 飾り（ダメージ・修正値・回数・充填・銘）は JP の組み立てをそのまま使い、日本語の語だけを表で英語にする。
 * 名前と飾りの境目は、同じ品を `OD_NAME_ONLY` で組んだ長さで分かる（このため `object_desc()` を 2 度、内側で呼ぶ。
 * 入れ子の呼び出しは 0 を返して通常の道を通す）。実装は `tb_name_en.c`。
 */
int tb_object_desc_en(char *buf, void *obj, unsigned long mode);

/*!
 * @brief `monster_desc()` の英語の組み立て（フック **#7**。`monster_desc()` の頭で呼ぶ）。
 * @param desc 出力
 * @param mon `monster_type *`
 * @param mode `monster_desc()` の mode（`MD_*`）
 * @return 1 = 英語で組んで `desc` に書いた／0 = 何もしていない（日本語モード）
 *
 * @details 原作の英語の枝（`monster2.c` の `#else`）と同じ形を、乱数の使い方も同じ順で組む。
 */
int tb_monster_desc_en(char *desc, void *mon, int mode);

/*!
 * @brief ランダムユニークの名前の英語の組み立て（フック **新**。`random-unique.c` の `random_unique_name()` の中）。
 * @details 英語は「名前 the 二つ名」（例 `Arnold the Firecracker`。決めたこと）で、`《》` を付けない。
 * `tb_ru_suffix_en()` は重複の判定の鍵（`Arnold the `）に、`tb_ru_name_en()` は出来上がった名前に差し替える。
 * 英語の表は `tb_name_en_tables.inc`（`tools/toband/gen_tb_name_tables.py` が用語表から作る）。日本語のときは何もしない。
 */
void tb_ru_suffix_en(char *suffix, size_t size, int human, int index);
void tb_ru_name_en(char *name, size_t size, int profile);

/*!
 * @brief 言語で変わる**ファイル名**の引き替え（フック #8 #9）。
 * @param name `get_rnd_line()` / `show_file()` に渡ったファイル名
 * @return 英語版のファイル名（静的な寿命）。引き替えないときは `name` のまま
 *
 * @details `thelp.hlp`→`help.hlp`・`〜_j.txt`→`〜.txt`・`〜_jp.txt`→`〜.txt`。
 * **引き替え先が lib/file か lib/help に実在するときだけ**替える
 * （無ければ原名のまま＝日本語の中身が出る。設計 §2 制約 4）。
 *
 * **ヘルプの引き替えは `tb_lang_set_en_help(1)` で止まる**（E7）。
 */
const char *tb_lang_file(const char *name);

/*!
 * @brief 英訳したヘルプの置き場（`toband/lib-en/help`）を使うと申告する（E7）。
 * @param on 1 = 使う（`ANGBAND_DIR_HELP` を差し替えた）／0 = 使わない
 *
 * @details 使うときは `thelp.hlp` → `help.hlp` の引き替え（#9）を**止める**。
 * 引き替えたままだと `lib-en/help/help.hlp`＝**変愚蛮怒の古い英語ヘルプ**が開き、
 * 幻想蛮怒の英訳版へ辿り着けない。呼ぶのは `tb_bootstrap()` の 1 か所だけで、
 * 最初の `show_file()` より前である（`tb_lang_file()` は答えを憶えるため）。
 */
void tb_lang_set_en_help(int on);

/*!
 * @brief 人物表の札の長さ（フック **#12**。`files.c` の `display_player_one_line()` の `head_len`）。
 * @param head 札の原文（CP932。日本語）
 * @param col 札を置く桁
 * @param len 欄の幅（負は「札の後ろに値を続ける」欄）
 * @return 値を置く位置までの字数
 *
 * @details 原作は値の桁を**日本語の札の長さ**で決めている。英語の札のほうが長いと値が札を上書きする
 * （`Blows/Ro  1`）ので、英語のときは英語の札で測る。右の欄（`col == 54`・値が札に続く欄）は、英語の札が
 * 14 桁にそろえて訳してあるので、短い札（`AM Field  :`）でも値の桁を 14 にそろえる。日本語のときは今までどおり。
 */
int tb_head_len(const char *head, int col, int len);

/*!
 * @brief 英語のとき、字を `width` 字までに切って返す（フック **#12**。知識の画面の敵の名前の欄）。
 * @param s 字（CP932）
 * @param width 欄の幅（桁）
 * @return 日本語のとき・収まるときは `s` のまま。英語で長いときは、切った字（静的な寿命）
 *
 * @details 敵の名前の欄の右に記号（桁 70）と殺害数（桁 73）が続く。日本語の名前は 20 桁ほどだが、英語の名前は 59 字まである
 * （`The Blubbering idiot, agent of black market, Simon the weak`）ので、欄を超えた分は右の記号と数に重なって読めなくなる。
 */
const char *tb_clip(const char *s, int width);

/*!
 * @brief 英語のとき、字が `width` 字を超えるなら、`width - 1` 字に切って `.` を付けて返す（フック **#12**。左の状態欄のクラス名）。
 * @param s 字（CP932）
 * @param width 欄の幅（桁）
 * @return 日本語のとき・収まるときは `s` のまま。英語で長いときは、切った字（静的な寿命）
 *
 * @details 左の状態欄は 12 桁。英語のクラス名は 16 字まで（`Knight-Commander`）あり、そのまま出すと `Knight-Comma` と中途半端に切れる。
 * 切れたと分かる印として `.` を付ける（`Knight-Comm.`）。
 */
const char *tb_fit(const char *s, int width);

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
 * `tb_lang_enabled()` が偽で引きが素通りする**ので、出来上がりは今までと同じ。
 *
 * 実装は `tb_shim.c`（`vstrnfmt()` を呼ぶのでTOband のヘッダが要る C の TU）。
 */
void tb_fprintf(FILE *fff, const char *fmt, ...);

/*!
 * @brief `fputs()` の代わり（フック **#29**）。引数の順は `fputs()` と同じ。
 * @param s 原文（CP932）
 * @param fff 書き出し先
 */
void tb_fputs(const char *s, FILE *fff);

/*!
 * @brief `sprintf()` の代わり。**書き込み先の大きさを見ない**版（フック **#18**）。
 * @details 呼び出しの引数の途中に `#ifdef JP` などの行がある所（17 か所）は、関数のマクロ
 * `tb_sprintf` にできない（前処理の指令を引数に書けない）。そこだけこの関数を使う。
 * 書式（#2）と `%s` の引数（#3）は引かれる。書き込み先を切らないのは今までの `sprintf()` と同じ。
 * 実装は `tb_shim.c`。
 */
int tb_sprintf_u(char *dest, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
/*!
 * @brief 日本語の書式を持つ `sprintf()` の代わり（フック **#18**）。
 * @param dest 書き込み先。**配列ならその大きさで切る**（英語の訳は日本語より長いので、
 *             小さい配列を溢れさせない）。ポインタ（`sizeof` がポインタの大きさ以下）なら
 *             切らない——今までの `sprintf()` と同じ（`strnfmt()` の `max == 0` は「無制限」）。
 * @details `strnfmt()` は `vstrnfmt()` を通るので、書式そのもの（#2）と `%s` の引数（#3）が
 * カタログで引かれる。日本語モードでは引きが素通りするので、出来上がりは `sprintf()` と同じ。
 * 取り替える呼び出しは `tools/toband/tb_core_patches.py` の規則 #18 が選ぶ（書式に日本語のリテラルがある物だけ）。
 */
#define tb_sprintf(dest, ...) \
    strnfmt((dest), ((sizeof(dest) > sizeof(char *)) ? (unsigned int)sizeof(dest) : 0u), __VA_ARGS__)
#endif

#endif /* TOBAND_ADAPTER_TB_LANG_C_H */
