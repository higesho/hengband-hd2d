/*!
 * @file fc_lang_c.h
 * @brief 日本語層の**C の皮**。`frox/src` から見えるのはこの 1 枚だけ。
 *
 * 基準は （フック）。
 *
 * ## この 1 枚が境界である理由
 * `frox/src` は C である。そこへ C++ のヘッダを見せると上流追随が一気に難しくなる
 * **純 C・自己完結**。
 *
 * ## 呼ぶ側の作法（設計 §4 の「共通の作法」）
 * - どの穴も **`fc_lang_enabled()` が偽なら即座に元の道へ落ちる**（設計 §1 制約 1）。
 *   ここの関数は自分でもそれを見るので、呼び側で先に見るのは「元の道が 1 行で済む」ときだけ。
 * - **引けなければ原文をそのまま返す**（設計 §1 制約 4）。空を返さない・NULL を返さない。
 * - 差し込んだ行には `//FC:` の印を付ける（設計 §1 制約 2）。
 *
 * ## 文字コード
 * ここを出入りする文字列は**すべて CP932（SJIS）**である（設計 §3.1）。
 * UTF-8 との変換は `fc_text.h` の仕事で、`frox/src` からは見えない。
 *
 * ## 実装は 3 つの TU に分かれている
 * | 関数 | どこ | なぜ |
 * |---|---|---|
 * | `fc_lang_enabled` `fc_tr` `fc_tr_fmt` `fc_tr_arg` | `fc_lang.cpp` | カタログ（ハッシュ表）を持つので C++ |
 * | `fc_clip` `fc_is_*` `fc_last_char_bytes` `fc_doc_word_bytes` `fc_cp932_text` | `fc_lang_c.c` | 素のバイト算なので C のまま |
 * | `fc_object_desc_ja` `fc_monster_desc_ja` | `fc_name_ja.c` | コアの型を見るので C（設計 §6.3） |
 */

#ifndef FROX_ADAPTER_FC_LANG_C_H
#define FROX_ADAPTER_FC_LANG_C_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================== カタログ（引く） */

/*!
 * @brief 日本語層が生きているか。
 * @return 0 = 英語（M0 と 1 ビットも違わない動き）／1 = 日本語
 */
int fc_lang_enabled(void);

/*!
 * @brief 表示文字列をカタログで引く（フック #1 #5）。
 * @param s 原文（CP932。NULL 可）
 * @return 訳文（静的な寿命。解放しない）。引けなければ `s` をそのまま返す
 *
 * @details **0x80 以上のバイトを 1 つでも含むなら引かずに返す**
 * （設計 §4 の「二重引きを避ける」）。`vstrnfmt()` で訳した日本語が
 * `Term_addstr()` にも流れてくるためで、先頭 1 バイトだけを見るのでは足りない
 * ——`format("%c) %s", 'a', "訳語")` の結果は頭が ASCII で中身が訳済みである
 * （Sil-Q で実測した罠）。
 *
 * @note **鍵にはタグを含める**（設計 §3.4-1）。Frox のドキュメントは
 * `<color:y>…</color>` を文字列に埋めるので、剥がして引くと訳文で位置を復元できない。
 */
const char *fc_tr(const char *s);

/*!
 * @brief **書式そのもの**をカタログで引く（フック #2）。
 * @param fmt 英語の書式（NULL 可）
 * @return 訳文の書式（静的な寿命）。引けなければ `fmt` をそのまま返す
 *
 * @details 変換指定（`%d` `%^s` など）の**並びが英日で一致する項目しか
 * カタログに入っていない**（読み込みのときに弾く。設計 §5）。
 * ここでは引くだけなので、走査の費用は掛からない。
 *
 * @note `%1$s` のような位置指定は**扱わない**。`vstrnfmt()` は可変引数を
 * 順に食うので、書式の側だけで順を入れ替えることはできない。
 */
const char *fc_tr_fmt(const char *fmt);

/*!
 * @brief `%s` で**引数として渡ってくる断片**をカタログで引く（フック #3）。
 * @param s 断片（CP932。NULL 可）
 * @return 訳文（静的な寿命）。引けなければ `s` をそのまま返す
 *
 * @details `msg_format("%^s %s%s", m_name, act, punct)` の `act`
 * （`"hits you"` `"claws you"` …）は**書式ではなく引数**なので、
 * `fc_tr_fmt()` にも `fc_tr()` にも届かない（組み上がった文は敵の名で始まるため、
 * 二重引きよけに弾かれる）。ここが唯一の通り道である。
 *
 * @note **引けなくても未訳一覧へ積まない。** `%s` には敵名・品名・プレイヤ名・
 * パス・数字も流れてくる。積むと一覧がそれで埋まって読めなくなる
 * （`fc_tr()` の側は積む。あちらは画面へ出る字そのものだから）。
 *
 * @warning 鍵は**句にする**こと。`"monster"` のような裸の 1 語を載せると、
 * `format("%s.raw", name)` のような**パスの組み立てまで訳してしまう**。
 *
 * @note 追記（2026-08-25。設計 §4 追記 (d)）: 上の罠は**実装で塞いだ**。
 * 裸の 1 語（英数と `'` `-` `_` だけの鍵）は、`fc_doc_vprintf()` が立てる
 * doc の旗の中でしか引かない。種族名・職業名はこれで安全にカタログへ載る。
 */
const char *fc_tr_arg(const char *s);

/*!
 * @brief **品の種別の型**をカタログで引く（フック #25）。
 * @param s `object_desc()` の `basenm`（CP932。NULL 可）
 * @return 訳文（静的な寿命）。引けなければ `s` をそのまま返す
 *
 * @details `flavor.c` は品の名前を**型**で組む——`& Potion~ of %` の
 * `&`（冠詞）`~`（複数形）`%`（種別の名前）`#`（風味）を写しの環がバイトで解く。
 * 型が英語のままだと `Potion of 器用さ` のように**枠だけ英語**で出るので、
 * **型ごと引く**（訳文は `%のポーション`。`%` と `#` は日本語の語順へ移る）。
 *
 * **鍵は `O:` で書く**（`E:` ではない）。`%` を変換指定と読むと
 * 引数の並びの照合（設計 §5）が誤って弾くためで、代わりに
 * **`%` `#` の数が英日で一致するか**を読み込みのときに見る。
 *
 * @note 引けなければ英語の型がそのまま組まれる（制約 4）。`basenm` には
 * 日本語の `k_info` 名も渡ってくるが、**0x80 以上を含む鍵は引かない**
 * （`fc_tr()` と同じ二重引きよけ）ので素通りする。
 */
const char *fc_tr_basenm(const char *s);

/*!
 * @brief **枠の名札**を引く（フック #27）。
 * @param s 英語の名札（`Slay Undead` `Acid` `Melee` `Good` …）
 * @return 訳語（静的な寿命）。引けなければ `s` をそのまま返す
 *
 * @details 人物調書と品の説明の枠は `doc_printf(doc, " %-11.11s: ", name)` で
 * 組まれる（`py_info.c`）。**名札をここで訳せば、詰めものは先方の書式が付ける**
 * ——CP932 は 1 バイト＝1 桁なので、11 バイト以内の訳語なら欄はぴたりと合う。
 *
 * **一般のカタログ（`fc_tr`）とは別の表を見る。** 名札には `Light` `Chaos`
 * `Good` のような裸の 1 語が多く、一般のカタログでは別の意味を持っている
 * （`Light` = 光源・`Chaos` = カオス領域）。同じ表で引くと耐性の欄に「光源」と出る
 * （実測 2026-08-27）。原本では **`F:`** の札で書く。
 *
 * @note 訳語が 11 バイトを超えるものは**読み込みで撥ねる**（欄が崩れ、境で
 * 2 バイト文字が割れるため）。撥ねられた名札は英語のまま出る（制約 4）。
 */
const char *fc_frame_label(const char *s);

/*!
 * @brief **品に直に足される飾りの字**を引く（フック #31 #32 #33）。
 * @param s 英語の飾り（`cursed` `good` ` (charging)` `on level %d of %s` …）
 * @return 訳語（静的な寿命）。引けなければ `s` をそのまま返す
 *
 * @details 品の名前と由来の文には、カタログのどの口も通らない短い英語が付く
 * ——偽の銘（`{cursed}`）・品の感触（`{good}`）・箱と罠の札（` (Locked)`）・
 * 光源の残り・由来の場所（`on level %d of %s`）。どれも
 * `object_desc_str()` / `strcpy` / `string_alloc_format` で**バイト列として
 * 継ぎ足される**ので、`fc_tr()` にも `fc_tr_fmt()` にも届かない。
 *
 * **一般のカタログ（`fc_tr`）とは別の表を見る。** 鍵に裸の 1 語が多く
 * （`cursed` `good` `empty` `tried` `average`）、一般の表へ入れると
 * `%s` の引数や画面の字を巻き添えにする（設計 §4 追記 (d) と同じ罠）。
 * 原本では **`D:`** の札で書く。照合は `E:` と同じ（変換指定の並びとタグ）。
 *
 * @note **書式としても使う**（#33）。`in %s` `on level %d of %s` のように
 * 変換指定を含む鍵はそのまま `string_alloc_format()` へ渡す。
 * `vsnprintf` に位置指定は無いので、**訳文でも変換指定の順を変えられない**。
 */
const char *fc_decor(const char *s);

/*!
 * @brief **店と建物の名**を引く（フック #34 #35）。
 * @param s 英語の名（`General Store` `Temple` `Home` `Museum` …）
 * @return 訳語（静的な寿命）。引けなければ `s` をそのまま返す
 *
 * @details 店の画面の見出し（`shop.c` の `shop->type->name`）と、品の一覧に出る
 * 建物の札（`cmd4.c` の `_dungeon_notes_store_name()`）で使う。**入口の地形の名は
 * `f_info` で訳してあるので、看板は前から日本語だった**——英語が残っていたのは
 * この 2 か所だけである。
 *
 * **一般のカタログ（`fc_tr`）とは別の表を見る。** 鍵が `Home` `Temple` `Museum`
 * のような**裸の 1 語**で、一般の表へ入れるとキーの名前まで巻き添えにする
 * （`cmd4.c` のマクロの引き金の表に `"Home"` がある）。原本では **`S:`** の札で書く。
 */
const char *fc_shop(const char *s);

/*!
 * @brief **店主の名前**を引く（フック #36）。
 * @param s 英語の名（`Bilbo the Friendly` `Grug` `Conan` …。241 人）
 * @return 訳語（静的な寿命）。引けなければ `s` をそのまま返す
 *
 * @details 店の画面の左上に出る。**`S:`（店の名）とも一般の表とも別**で、
 * 原本では **`K:`** の札で書く——鍵に `Grug` `Angel` `Vile` のような裸の 1 語が
 * 30 人ぶんあり、一般の表へ入れると画面の別の字を巻き添えにする。
 *
 * 訳の 204 人は変愚蛮怒の `src/store/store-owners.cpp` から引き、
 * 残り 37 人は承認票で決めた（2026-08-27）。作り直しは
 * `python tools/frox/fc_build_ja_keeper.py --catalog`。
 */
const char *fc_shop_owner(const char *s);

/*!
 * @brief **呪文・属性・召喚の名**を引く（フック #46）。
 * @param s 英語の名（`Magic Missile` `Breathe` `Fire` `Undead` …）
 * @return 訳語（静的な寿命）。引けなければ `s` をそのまま返す
 *
 * @details 敵の呪文の表示名（`monspell.c` の `_parse_t` の 63 本）・系統名
 * （`_mst_tbl[]` の 14 本）・属性の名（`gf.c` の `_gf_tbl[]` の 130 本）・
 * 召喚の種別名（`init1.c` の `_summon_type_tbl[]` の 84 本）。どれも表の中の
 * 素の `char *` で、`sprintf("$CASTER breathes <color:%c>%s</color>.", …)` の
 * ように**組み立ててから**画へ出るので、`fc_tr()` にも `fc_tr_fmt()` にも
 * 届かない。**呼ぶ側で引く。**
 *
 * **`A:`（引数の裸の 1 語）へは入れられない。** `Breathe` `Teleport`
 * `Identify` `Berserk` は**職の力の名前**でもあり（`var_set_string(res,
 * "Breathe")`）、力の一覧は `%-23.23s` の欄に並ぶ。引数の表へ入れると
 * 確かめていない画の桁が動く。**`F:`（枠の名札）とも別**で、あちらの
 * `Acid` は耐性の欄なので「耐酸」、こちらは属性そのものなので「酸」である。
 * 原本では **`M:`** の札で書く。
 *
 * @note 系統名は敵の思い出の `%-10.10s` の欄にも並ぶ（`monspell.c:4691`）。
 * 訳語を 10 バイト（＝全角 5 字）以内に収めること。
 */
const char *fc_spell_name(const char *s);

/*!
 * @brief **状態列の長い札**を引く（フック #77）。
 * @param s 英語の長い札（`ImmAcid`。NULL 可）
 * @return 訳文（静的な寿命）。`B:` の表に無ければ `fc_tr()` へ落とし、それも引けなければ `s`
 *
 * @details `xtra1.c` の `bar[]` は**短い札と長い札の対**である（`IAc` / `ImmAcid`）。
 * 画面の下の状態列は `strlen()` で桁を数えてから中央へ寄せるので、
 * **数える所と出す所の両方でこれを通さないと欄が崩れる**。
 * 原本では **`B:`** の札で書く。
 *
 * @note 落とし先が `fc_tr()` なのは、出す所の `Term_addstr()` がまた `fc_tr()` を
 * 通すからである。ここで落としておかないと、数えた桁と出た字の長さが食い違う。
 */
const char *fc_bar(const char *s);

/*!
 * @brief **状態列の短い札**を引く（フック #77）。
 * @param lstr 同じ行の長い札（鍵の前半。NULL 可）
 * @param sstr 英語の短い札（`IAc`。NULL 可）
 * @return 訳文（静的な寿命）。引けなければ `sstr` をそのまま返す
 *
 * @details 鍵は `"<長い札>\\t<短い札>"` である。**短い札だけでは引けない**
 * ——`Bl` は `Blind` `Blood Blade` `Blending` `Block` `Blink` の 5 つに使い回されている。
 *
 * @note **一般のカタログへは落とさない。** 2 字の略号は、一般の表に同じ字があると
 * 別物の訳が出る（`Str` は「腕力」である）。
 */
const char *fc_bar_short(const char *lstr, const char *sstr);

/*!
 * @brief モンスター一覧の見出しを日本語で組む（フック #22 #23）。
 * @param buf   書き込む先（CP932）
 * @param max   `buf` の大きさ
 * @param los   1 = 視界の中の群れ（`_GROUP_LOS`）／0 = 知っている群れ（`_GROUP_AWARE`）
 * @param sub   1 = サブウィンドウ版の短い言い回し
 * @param probe 1 = 探査（`MON_LIST_PROBING`）。`los` のときだけ効く
 * @param other 1 = 「ほかの」が入る（`_GROUP_AWARE` で視界の群れが別に出ているとき）
 * @param count 体数
 * @param awake 起きている体数
 * @return 1 = 組んだ（呼び手は従来の道を飛ばす）／0 = 英語の道へ落ちる
 *
 * @details **英語の複数形を引数で組んでいるので、書式を訳すだけでは足りない**
 * （`format("You %s %d monster%s, %d %s awake:", "see", n, "s", k, "are")`。
 * `cmd3.c`）。`%s` に入る `s` と `are` は裸の 1 語で、引いてしまうと
 * ファイル名や pref 名まで巻き添えにする（設計 §4 追記 (d)）。
 *
 * だからここでは**複数形を解いた英語の 1 文**を鍵にして引く
 * （`You see %d monsters, %d are awake:` など 7 本）。引けなければ 0 を返し、
 * 呼び手の英語の道がそのまま走る——**英語の出力は 1 バイトも変わらない**（制約 1）。
 */
int fc_mon_list_header(char *buf, size_t max, int los, int sub, int probe,
                       int other, int count, int awake);

/*!
 * @brief doc の旗（フック #13 の展開の間だけ立てる）。
 * @param on 1 = doc の中（裸の 1 語も引いてよい）／0 = 外
 * @details **`frox/src` からは呼ばない**——アダプタ内部の継ぎ目である
 * （`fc_doc.c` が `vstrnfmt()` を囲むのに使う）。
 */
void fc_lang_doc_scope(int on);

/*!
 * @brief `doc_printf()` / `doc_cprintf()` の展開を日本語層で肩代わりする（フック #13）。
 * @param string_ptr_s 書き足す先（`string_ptr`。型を見せないため `void *`）
 * @param fmt 書式（CP932）
 * @param vp 可変引数
 * @return 1 = 肩代わりした（呼び手は従来の展開を飛ばす）／0 = 英語の道へ落ちる
 *
 * @details 中身は `vstrnfmt()` である——書式は #2（`fc_tr_fmt`）、`%s` の引数は
 * #3（`fc_tr_arg`）が既に居るので、道を通すだけで両方が効く。展開の間だけ
 * doc の旗を立てる（上の `fc_lang_doc_scope`）。
 * **日本語層が寝ているときは `vp` に触れずに 0 を返す**（制約 1。呼び手の
 * `string_vprintf()` がそのまま使える）。実装は `fc_doc.c`（コアの型を見る C）。
 */
int fc_doc_vprintf(void *string_ptr_s, const char *fmt, va_list vp);

/* ============================================ 品選びの窓（フック #37） */

/*!
 * @brief **品選びの窓が開いている／閉じた**（フック #37。`obj_prompt.c`）。
 * @param on 1 = 開いた（コアが「どれ？」と訊いている）／0 = 閉じた
 *
 * @details `obj_prompt()` は**いま出ている画の上に自分の窓を重ねる**。下に残る画は
 * 消えないので、店で開くと**店の命令列（`[b/p/g] …`）がミラーに残ったまま**になる。
 * 画面のカーソル層は「命令列のある画で、コアが何も訊いていない間は命令列だけを
 * 札にする」（FH-14）ので、この旗が無いと**品の letter が 1 つも札にならない**
 * ——2026-08-28 に気づいた「店の譲る（売る）を選択した際に、売る対象のリストに
 * カーソルが移らない」の正体である。
 *
 * **メッセージ行では見分けられない。** FH-14 の「訊いているか」は
 * `msg_line` の行数で見ているが、`obj_prompt` の問いは `prt()` ではなく
 * **doc の窓**へ出るので、メッセージ行は空のままである。
 *
 * @note 実装は `fc_shim.c`（画面向けの旗はあそこに集めてある）。
 * 読むのは `fc_menu.cpp` の `fill_menu_choices()`（`fc_obj_prompt_active()`）。
 */
void fc_obj_prompt_scope(int on);

/* ================================================== バイト算（切る・折る・通す） */

/*!
 * @brief いまコアの中の文字列に **CP932 の 2 バイト文字が混じりうるか**。
 * @return 0 = 純 ASCII と見てよい／1 = 2 バイト文字として扱う
 *
 * @details 下のバイト算はこれを見る。**`fc_lang_enabled()` とは別の旗である**
 * （追補 A3。設計 §9・§8.25）——英語で立てても、**日本語で作ったセーブを開いたら
 * 真になる**。記憶の中の生い立ち・銘・履歴は CP932 のバイトのままだからで、
 * ここで 0 を返すと `fc_is_text_byte()` が 0x80 以上を潰し、
 * **後続バイトだけが ASCII の範囲に残って化ける**（Sil-Q で実測した罠）。
 *
 * CP932 は ASCII の上位互換なので、印は「CP932 でありうるか」の 1 ビットで足りる
 * ——欄ごとの変換は要らない。
 */
int fc_cp932_text(void);

/* ================================ セーブの符号の印（追補 A3。フック #29 #30） */

/*!
 * @brief セーブヘッダへ書く**符号の印**（フック #29。`save.c` の `sf_system`）。
 * @return `0x46430001` = 純 ASCII ／ `0x46430002` = 人の字の欄に CP932 が載る
 *
 * @details `'F' 'C'` ＋ 1 か 2。上流は `sf_system` に 0 を書いて**読むだけで使わない**
 * ので、印なし（`0`）は「上流・M0 のセーブ＝ASCII」と読む。
 *
 * **一度 CP932 になったセーブは CP932 のままである。** 日本語で作った人物を英語で
 * 開いて遊び続けても、記憶の中の字は CP932 のバイトのままだからで、そこで ASCII と
 * 書き換えると次に開いたとき化ける。
 */
unsigned long fc_save_enc_mark(void);

/*!
 * @brief 読んだ印を控える（フック #30。`load.c`）。
 * @param mark ヘッダから読んだ `sf_system`
 *
 * @details **人の字の欄を読むより前に置くこと**（`msg_on_load()` の履歴が最初に来る）。
 * 印が CP932 なら、英語で立てていても `fc_cp932_text()` が真になる。
 */
void fc_save_enc_note(unsigned long mark);

/*!
 * @brief `n` バイトへの切り詰めを**2 バイト文字の途中で切らない**（フック #1）。
 * @param s 文字列（CP932）
 * @param n 切りたいバイト数
 * @return 実際に切ってよいバイト数（`n` 以下）
 *
 * @details 日本語層が寝ているときは `n` をそのまま返す（費用ゼロ）。
 */
int fc_clip(const char *s, int n);

/*!
 * @brief `s` の**先頭 1 文字が何バイトか**（追補 A1。フック #9）。
 * @param s 文字列（CP932。NULL 可）
 * @return 2 = 2 バイト文字／1 = それ以外（日本語層が寝ていれば常に 1）
 *
 * @details 自由入力の桁送りに使う。`pos++` のままだと**2 バイト文字の真ん中**へ
 * カーソルが降りて、次の描き直しで字が割れる。後ろへ戻るのは
 * `fc_last_char_bytes()`（あちらは終わりの位置から数える）。
 */
int fc_char_bytes(const char *s);

/* ============================================== 自由入力の旗（追補 A1。#9） */

/*!
 * @brief **自由入力の待ちに入った／出た**（フック #9）。
 * @param active 1 = 入った（名前・銘・地形の記憶を打っている）／0 = 出た
 *
 * @details 画面側はこの旗が立っている間だけ **IME の窓を出し、非 ASCII を送る**。
 * 立てないと日本語は 1 文字も届かない。**逆に立てっぱなしにすると、命令を待つ所へ
 * 2 バイト文字が流れてコアが 2 つの命令として読む**ので、待ちそのものを囲む
 * （`inkey_flag` も `character_icky` も「いま自由入力中」の印にはならない
 * ——`y/n` や品選びの待ちでも下りる）。
 */
void fc_text_input_set(int active);

/*!
 * @brief **この入力欄は ASCII だけ**（追補 A1。フック #28）。
 * @param on 1 = ASCII だけの欄に入った／0 = 出た
 *
 * @details **冒険者の名前がこれである。** 名前はセーブファイルの綴りになるが、
 * `process_player_name()`（`files.c`）は **0x80 以上のバイトを 1 つ残らず捨てる**
 * ので、日本語だけの名前は綴りが空になり `PLAYER` へ落ちる
 * ——**別の冒険者どうしが同じセーブへ書き合う**。Sil-Q が同じ穴を踏み、
 * 「名前は英数字だけ」で決着している。
 *
 * 門は**画面側**に置く——`on` の間は `fc_text_input_active()` が 0 を返すので、
 * IME の窓が出ず、非 ASCII のバイトがそもそも送られてこない。
 */
void fc_text_input_ascii(int on);

/*!
 * @brief 旗が立っているか（`fc_frame.cpp` と `fc_change_digest()` が読む）。
 * @return 1 = 自由入力の待ちで、**日本語を受け付ける**／0 = それ以外
 * @note `fc_text_input_ascii(1)` の間は 0 を返す（上の註記）。
 */
int fc_text_input_active(void);

/*! @brief CP932 の 2 バイト文字の先行バイトか。`h-config.h` の `iskanji` と同じ範囲。 */
int fc_is_sjis_lead(int c);

/*!
 * @brief CP932 の 2 バイト文字の**後続**バイトか（A1）。
 * @details 範囲は 0x40〜0x7E と 0x80〜0xFC（0x7F と 0xFD〜0xFF は無い）。
 * **先行バイトだけ見て 2 バイト取ると、壊れた組で 1 文字ぶんの穴が空く。**
 */
int fc_is_sjis_trail(int c);

/*!
 * @brief `buf` の先頭から `len` バイトのうち、**最後の 1 文字**のバイト数（1 か 2）。
 * @details 後退（`0x7F` / `^H`）で 1 文字ぶん戻すために要る（A1。設計 §9）。
 *
 * **頭から数える。** 末尾の 2 バイト目だけを見て「先行バイトか」を問うと、
 * 「1 バイト文字＋たまたま先行バイトに見える 1 バイト文字」を 2 バイト文字と誤る。
 * CP932 は自己同期しないので、頭から辿るしか確かめようがない。
 */
int fc_last_char_bytes(const char *buf, int len);

/*!
 * @brief 画面／ファイルへ通してよいバイトか（フック #6）。
 * @details `isprint()` の代わり。**0x80 以上を潰さない**のが唯一の違いで、
 * 日本語層が寝ているときの答えは（C ロケールの）`isprint()` と同じである。
 *
 * @note ここを直すまで**日本語は 1 文字も出ない**。`my_fgets()` は
 * `lib-ja/edit/*.txt` も `lib-ja/help/*.txt` も読むので、0x80 以上が
 * 落とされると名前も説明もヘルプも空になる（Sil-Q で実測した罠）。
 */
int fc_is_text_byte(int c);

/* ================================================ 番号の参照（フック #11） */

/*!
 * @brief 実体の参照が**番号**ならその番号を返す（フック #11。設計 §6.2 の追記）。
 * @param name `G:KILL(...)` などの引数（CP932）
 * @return 番号（正の十進数の並びのときだけ）。それ以外・日本語層が寝ているときは 0
 *
 * @details `lib-ja` の参照は `fc_edit_refs.py` が番号へ書き換えてある。
 * 名前の照合（`_prep_name_aux()`）はバイト単位で CP932 を壊すので、
 * 訳語では引けない——番号で指すのが唯一の壊れない道である。
 * **日本語層が寝ているときは必ず 0**（英語の動きを 1 ビットも変えない。制約 1）。
 */
int fc_ref_index(const char *name);

/* ============================================ ドキュメントの単語切り（フック #4） */

/*!
 * @brief `pos` から始まる 1 単語のバイト数。日本語でなければ 0（＝英語の道へ落ちる）。
 *
 * @details **ここが Frox の勘所である。** `doc_lex()` の単語の切り出しは
 * `strchr(" <\n", *pos)` で止まるので、空白の無い日本語は**段落まるごとが 1 単語**になり、
 * 折り返しがまったく効かない（`z-doc.c:955`）。
 *
 * 返すのは**表示上の 1 かたまり**である:
 *
 * - 2 バイト文字 1 つ ＝ 2 バイト
 * - その後ろに**行頭禁則**の字（`。、）」` など）が続くならまとめて足す（最大 4 字ぶん）
 * - 先頭が**行末禁則**の字（`（「『` など）なら次の 1 字も足す
 *
 * @note 設計 §4 は `fc_is_sjis_lead(c)` と `fc_wrap_here(prev, next)` の 2 本と
 * 書いていたが、**Frox の折り返しは `doc_insert()` が単語の長さ（`cb`）で決める**ので、
 * 「ここで折ってよいか」ではなく「1 単語は何バイトか」を返す形でなければ噛み合わない。
 * 設計 §4 に追記した（2026-08-24）。
 *
 * @warning `doc_insert()` は連なる WORD を最大 10 個まとめて 1 かたまりとして
 * 幅を計るので、**まとめる側も止めなければならない**（フック #4 は
 * `z-doc.c` の 3 行になる。設計 §4 の追記）。
 */
int fc_doc_word_bytes(const char *pos);

/*!
 * @brief `s` の先頭が**行頭に来てはいけない字**か（フック #49。設計 §8.50 ④）。
 * @param s 文字列（CP932。NULL 可）
 * @return 1 = 行頭禁則の 2 バイト文字（`。、）」ー` など）／0 = それ以外
 *
 * @details `roff_to_buf()`（`util.c`）専用。あちらは `doc_insert()` と違って
 * **単語の長さではなく 1 文字ずつ**折るかどうかを決めるので、
 * `fc_doc_word_bytes()`（#4）とは噛み合わない——**同じ表を別の形で引く**。
 *
 * @note 日本語層が寝ているときは常に 0。**英語の折り返しには一切触らない。**
 */
int fc_roff_kinsoku(const char *s);

/* ============================================================ 名前の組み立て */

/*!
 * @brief `object_desc()` の日本語版（フック #7）。
 * @param buf 書き先（コアの決まりで `MAX_NLEN` バイト）
 * @param o_ptr `object_type *`。**型を見せない**ためにここでは `const void *`
 * @param mode どこまで飾るか（元の関数と同じ `OD_*`）
 * @return 1 なら `buf` を埋めた（呼び手はそのまま返る）／0 なら英語の道へ落ちる
 *
 * @details 日本語は語順が違う（冠詞が無い・複数形が無い・助数詞が付く）ので、
 * 途中に手を入れるのではなく**丸ごと組み直す**。実装は `fc_name_ja.c`。
 * **J2 の時点では常に 0 を返す**（配管だけ通して、中身は J3 で入れる）。
 */
int fc_object_desc_ja(char *buf, const void *o_ptr, unsigned long mode);

/*!
 * @brief `monster_desc()` の日本語版（フック #8）。
 * @param desc 書き先
 * @param m_ptr `monster_type *`
 * @param mode 元の関数と同じ `MD_*`
 * @return 1 なら `desc` を埋めた／0 なら英語の道へ落ちる
 */
int fc_monster_desc_ja(char *desc, const void *m_ptr, int mode);

/* ========================== 継ぎ足しで組むメッセージ（フック #38 #39） */

/*!
 * @brief 拾った品を告げる 1 行を日本語で組む（フック #38。`obj.c`）。
 * @param buf     書き込む先（CP932）
 * @param max     `buf` の大きさ
 * @param name    品の名前（`object_desc()` の結果。もう日本語）
 * @param wearing 1 = 装備した／0 = 持った
 * @param quiver  1 = 矢筒へ入った
 * @param slot_ch 欄の letter（`a`〜`z`）。出さないときは 0
 * @return 1 = 組んだ（呼び手は `buf` を出す）／0 = 英語の道へ落ちる
 *
 * @details `obj_delayed_describe()` は `"You have"` ＋品の名前 ＋
 * `" in your quiver"` ＋ `" (%c)"` ＋ `"."` を **`string_append_s()` で継いで**から
 * `msg_print()` へ渡す。だから鍵が「品の名前まで入った 1 本の字」になり、
 * `fc_tr()`（#5）にも `fc_tr_fmt()`（#2）にも届かない
 * ——**誕生の直後に出る `You have 食料×9.` がこれである**（設計 §8.31 ⑦）。
 *
 * ここでは**書式に組み直してから**引く。鍵は 6 本:
 *
 * ```
 * You have %s.                  You have %s (%c).
 * You have %s in your quiver.   You have %s in your quiver (%c).
 * You are wearing %s.           You are wearing %s (%c).
 * ```
 *
 * @note `You are wearing %s (%c).` は `equip.c:707` が**書式のまま**出しており、
 * そちらは前から日本語だった。**同じ画に、引ける字と引けない字が混じっていた。**
 * 鍵を分けず同じ 1 本にしてあるので、両方が同じ訳で出る。
 *
 * @note 引けなければ 0 を返す（制約 4）。呼び手の英語の道がそのまま走るので、
 * **英語の出力は 1 バイトも変わらない**（制約 1）。
 */
int fc_obj_carry_msg(char *buf, size_t max, const char *name,
                     int wearing, int quiver, int slot_ch);

/*!
 * @brief 武器の属性が乗ったことを告げる 1 行を組む（フック #39。`combat.c:939`）。
 * @param buf       書き込む先（CP932）
 * @param max       `buf` の大きさ
 * @param o_name    武器の名前（もう日本語）
 * @param is_slay   1 = 斬る（`slays`）／0 = まとう（`is covered in`）
 * @param kill_desc 相手か属性（`animals` `evil` `acid` … 17 語）
 * @return 1 = 組んだ／0 = 英語の道へ落ちる
 *
 * @details 先方は `msg_format("Your %s %s %s.", o_name, "slays", kill_desc)` と
 * **動詞まで引数で**組む。`"slays"` は裸の 1 語なので `fc_tr_arg()`（#3）が
 * わざと引かず（設計 §4 追記 (d)）、書式だけ訳しても英語が 2 つ残る。
 * 日本語は語順も違う（`vsnprintf` に位置指定は無い）。
 *
 * **だから鍵は「解いた英語の 1 文」にする**——`fc_mon_list_header()`（#22 #23）が
 * 複数形で採ったのと同じ手である。17 本:
 *
 * ```
 * Your %s slays animals.        Your %s is covered in acid.
 * Your %s slays evil.           Your %s is covered in fire.   …
 * ```
 *
 * @note 裸の 1 語（`animals` `good` `dark`）を一般のカタログへ入れない。
 * `Light` を入れて耐性の欄に「光源」と出た罠と同じである（設計 §4 追記 (j)）。
 */
int fc_slay_msg(char *buf, size_t max, const char *o_name,
                int is_slay, const char *kill_desc);

/* ================================ 建物の画面（フック #41 #42） */

/*!
 * @brief 建物の画面の見出しを組む（フック #41。`bldg.c:173`）。
 * @param buf   書き込む先（CP932）
 * @param max   `buf` の大きさ
 * @param owner 主の名（`t_*.txt` の `B:n:N:` の 2 欄目。82 人）
 * @param race  主の種族（同 3 欄目。24 種）
 * @param name  建物の名（同 1 欄目。34 種）
 * @return 1 = 組んだ（呼び手は `buf` を出す）／0 = 英語の道へ落ちる
 *
 * @details 先方は `snprintf("%.20s (%.20s) %35.35s", …)` で組む。**C の
 * `snprintf` なので `fc_tr_fmt()`（#2）も `fc_tr_arg()`（#3）も通らない**し、
 * 組み上がった字は `prt()` から `fc_tr()`（#1）へ行っても引けない
 * （主の名まで入った 1 本になるため）。
 *
 * **データ（`t_*.txt`）の側は英語のままにする。** `bldg.c:4486` が
 * `strpos("Cornucopia", bldg->name)` で**建物を英語の名で見分けている**ので
 * （レプラコーンと Implorington の入店を止める）、`lib-ja` の側で訳すと
 * **その判定が黙って効かなくなる**（J3 の #11 #16 と同じ穴）。だから
 * **画へ出す所で訳す。**
 *
 * 引くのは既にある表である——名は `S:`（`fc_shop`）・主は `K:`（`fc_shop_owner`）・
 * 種族は `S:`。**桁も当方で詰める**（`%.20s` はバイトで切るので
 * 2 バイト文字の途中で割れる。`fc_clip()` を通す）。
 */
int fc_bldg_header(char *buf, size_t max, const char *owner,
                   const char *race, const char *name);

/*!
 * @brief 建物の命令の名を引く（フック #42。`bldg.c:251`）。
 * @param s 英語の命令の名（`Rest for the night` `Request quest` … 58 種）
 * @return 訳語（静的な寿命）。引けなければ `s` をそのまま返す
 *
 * @details 先方は `sprintf(" %c) %s %s", letter, action_name, buff)` で組むので、
 * #41 と同じ理由でどの口も通らない。**引くのは `S:`**（`fc_shop` と同じ表）
 * ——`Help` `Poker` `Loans` のような裸の 1 語があり、一般のカタログへ入れると
 * **自動拾いの編集画面の `Help`（`autopick.c:5035`。わざと英語のまま）**や
 * **呪文の `Restoration`（`do-spell.c:1455`）**まで訳してしまう。
 *
 * @warning **桁は 20 バイト（全角 10 字）まで。** 命令は 35 桁の欄に 2 段で
 * 並ぶ（`c_put_str(..., 35*(i%2))`）ので、`" x) "`（4）＋名＋`" (99999gp)"`（10）で
 * 35 に収まらなければ右の段と重なる。超える訳は読み込みで撥ねる。
 */
const char *fc_bldg_action(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* FROX_ADAPTER_FC_LANG_C_H */
