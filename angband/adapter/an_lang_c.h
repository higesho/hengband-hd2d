/*!
 * @file an_lang_c.h
 * @brief 日本語層の C の皮。原作とアダプタの C から見えるのはこれだけ。
 *
 * @details 実体は `an_lang.cpp`（C++）。**どの関数も日本語層が寝ている
 * （`an_lang_enabled()` が偽）ときは受け取った字をそのまま返す**ので、英語の起動は
 * 1 ビットも変わらない（設計 §1 制約 1）。
 *
 * 引けなければ英語のまま（制約 5）。返す字はカタログが持つ字か、呼び手が渡した字
 * そのもの——**呼び手は解放しない**。`an_tr_term()` だけは作業用の場所を返す
 * （次の呼び出しで上書き。ゲームのスレッドからしか呼ばれない）。
 */
#ifndef AN_LANG_C_H
#define AN_LANG_C_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! 日本語層が起きているか（`--lang=ja` か最初の `ui_state.lang=="ja"`）。 */
int an_lang_enabled(void);

/*! 一般の字（`E:`）を丸ごと引く。無ければ `en` のまま。 */
const char *an_tr(const char *en);

/*! 書式（フック #2。`vstrnfmt()` の頭）。`E:` を引く。無ければ `fmt` のまま。 */
const char *an_tr_fmt(const char *fmt);

/*!
 * @brief `%s` に裸で入る語（フック #3。`A:` と `M:`・種族と職業の名）。無ければ `s` のまま。
 * @param fmt_translated 書式そのものがカタログで引けたか（`vstrnfmt()` が渡す）。
 * @details **1 語（空白を含まない）の鍵は、書式が引けたときだけ引く**。原作は `%s` で
 * ファイル名や識別子も組む（`"%s.txt"` に `body` が入る——`body.txt` が開けなくなった。
 * 2026-09-27 の実測）。空白を含む句（`When eaten, it `・`right hand`）はファイル名に
 * ならないので、書式が英語のままでも引く。
 */
const char *an_tr_arg(const char *s, int fmt_translated);

/*!
 * @brief 読み物（`textblock`）の字を組む間を囲む（フック #17。`textblock_vappend_c()`）。
 * @details この間は 1 語の引数も `A:`/`M:` を引く（`an_tr_arg()`）。読み物の字は画へ
 * 出るだけで、ファイル名や鍵にはならない。
 */
void an_display_begin(void);
void an_display_end(void);

/*!
 * @brief Term へ書く字を丸ごと引く（フック #1。`Term_addstr()` の頭）。
 * @details 純 ASCII で、末尾の空白を除いた字が `E:` にあれば訳し、**元の字と同じ桁まで
 * 空白で埋める**（桁をそろえて描く一覧の欄を崩さない）。無ければ `buf` のまま。
 */
const char *an_tr_term(const char *buf);

/*!
 * @brief 組み上がった行の中の句を置き換える（`R:`。フック #13 ほか）。
 * @details 原作が `my_strcat()` で句を継いで作る行（持ち物の選択の見出し
 * `(Inven: a-d, / for Equip, ESC)` など）は、書式にも Term の丸ごとにも当たらない。
 * 長い句から順に置き換える。純 ASCII の句だけが対象。
 */
void an_tr_phrases(char *buf, size_t cap);

/*!
 * @brief `%s` の幅・精度を**桁**で扱う（フック #3 の続き。`vstrnfmt()` の `case 's'`）。
 * @details `snprintf()` の `%-14s` `%.70s` はバイトで数えるので、日本語では欄がずれ、
 * 精度は多バイト字の途中で切る。日本語層が起きていて、引数に非 ASCII があり、
 * 幅か精度があるときだけ代わりに組んで 1 を返す。それ以外は 0（原作の道へ）。
 */
int an_fmt_s(char *out, size_t cap, const char *aux, const char *arg);

/*!
 * @brief 次の `%s` の精度は `*` で渡った（バイトで数えた長さ）という印（フック #22。`vstrnfmt()`）。
 * @details 原作の型の置き換え（`{target}` の前までを `"%.*s"` で写す。mon-blows.c・mon-spell.c ほか）は
 * 長さをバイトで数える。an_fmt_s() は精度を桁で扱うので、この印があればバイトのまま切る。
 */
void an_fmt_star_precision(void);

/*!
 * @brief 名前の表（`lang/ja/names/<kind>.ja.tsv`）を引く。無ければ NULL。
 * @param kind `monster` `object` `artifact` `ego` `flavor` `terrain` `trap` `spell`
 *   `race_class` `owner` `monster_base` `store` `slot` のどれか
 */
const char *an_name(const char *kind, const char *en);

/*!
 * @brief 怪物のメッセージ・打撃・痛みの文（`V:`。P2。設計 §16.3）。無ければ `en` のまま。
 * @details 原作が英語の句を組み合わせる所（`mon-msg.c` の述語 `die[s].`・`mon-blows.c` の
 * `hits {target}` と `you`・`player-attack.c` の動詞 `hit`）で、組む前の英語を鍵に引く。
 * 述語の訳は頭に「は」を付けて主語へ継ぐ（`コボルド ` ＋ `は死んだ。`。間の空白は
 * `an_msg_tidy()` が詰める）。
 */
const char *an_tr_v(const char *en);

/*!
 * @brief 主語に継ぐ述語（怪物のメッセージ・痛み・打撃。フック #19 #20）。V: を引き、無ければ、
 * gamedata 側で既に訳された述語（lib-ja の pain.txt・blow_methods.txt。P3）なら頭に「は」を足す
 * （頭が助詞なら足さない）。英語で訳が無ければ `en` のまま。
 */
const char *an_tr_pred(const char *en);

/*!
 * @brief 怪物のメッセージの主語（`mon-msg.c` の `get_subject()` の日本語版。フック #19）。
 * @return 日本語層が起きていて組めたら 1（`buf` に「コボルド 」「3体のコボルド 」「何か 」）。0 なら原作の道。
 */
int an_mon_subject_ja(char *buf, size_t cap, const char *race_name, int unique, int count, int invisible,
    int offscreen);

/*!
 * @brief 組み上がったメッセージを日本語の文へ整える（`msg()` `msgt()` の `vstrnfmt()` の直後。フック #18）。
 * @details 全角どうしの間の空白を詰め、全角の直後の `.` `!` `?` を「。」「！」「？」へ（句読点が
 * 続けば落とす）。英字 3 字以上の語が残るメッセージは `--report-untranslated=` へ `M?:` の印で
 * 書き出す（実機の未訳の収集。`tools/angband/an_msg_untranslated.py`）。英語では何もしない。
 */
void an_msg_tidy(char *buf, size_t cap);

/*!
 * @brief 行に収まらない 1 本のメッセージの頭を、字の境で切る（`display_message()`。フック #24）。
 * @param max_cols この桁までに収める @param min_cols これより右の半角の空白なら語の切れ目として使う
 * @param bytes 切った頭のバイト数 @param cols その桁数
 */
void an_msg_cut(const char *t, int max_cols, int min_cols, int *bytes, int *cols);

/*!
 * @brief 人物表・状態列・状態行・装備の枠の札（`F:`。P4 のフック #11）。
 * @details 純 ASCII の札の前後の空白を除いて `F:` を引き、元の字と同じ桁まで空白で埋める
 * （訳の方が広ければ埋めない）。休憩の残り（`Rest    50`）は頭の `Rest` だけ引く。
 * 無ければ `s` のまま（Term の段の an_tr_term() が E: を引く）。作業用の場所は 4 つを順に使う。
 */
const char *an_frame_label(const char *s);

/*!
 * @brief 人物表の能力の欄の札（`ui_entry.txt` の `label`/`label5`。読み込むときに引く）。
 * @details 鍵は `F:ui_entry:<name>:<field>`（同じ `Light` が耐性と光源の 2 つあるので名で分ける）。
 */
const char *an_ui_entry_label(const char *entry, const char *field, const char *en);

/*! an_frame_label() の結果の桁数（`prt_level_feeling()` の値の桁を札の幅で決める）。 */
int an_frame_label_cols(const char *s);

/*! 札 `tag`（`F:` ほか）を丸ごと引く。無ければ `en` のまま。 */
const char *an_tr_tag(char tag, const char *en);

/*!
 * @brief `buf` の英語名を名前の表（`kind`）で引き、あれば日本語名で上書きして 1 を返す。
 * @details `cap` バイトに収め、字の途中では切らない。日本語層が寝ているか、無ければ 0（`buf` はそのまま）。
 */
int an_name_to(const char *kind, char *buf, size_t cap);

/*! 店主の呼び名（`prt_welcome()` の `short_name`）。日本語名から種族の括弧を落として写す。 */
void an_owner_short(char *buf, size_t cap, const char *owner_name);

/*!
 * @brief 殿堂の記録（`build_score()` の `how[32]`）へ書く死因（AH-59。フック `score.c`）。
 * @details 日本語で遊ぶと `died_from` は怪物の**日本語名**で、記録の 31 バイトを超えると字の途中で切れて
 * 殿堂の行に壊れた字が出る。怪物の名は名前の表を逆に引いて**英語の名**を返す（記録は英語の起動と同じ字。
 * 画へ出すときに `an_tr_arg()` が日本語へ引く）。表に無い日本語の名は字の境で 31 バイトへ切る。英語では
 * 受け取った字のまま。
 */
const char *an_score_how(const char *died_from);

/*!
 * @name 「見る」の 1 行の説明（AH-59。`an_targeting.c` の `an_look_describe_grid()`）
 * @details どちらも日本語層が寝ているときは何もしない（英語の字は原作のまま）。
 * @{
 */
/*!
 * @brief 怪物の状態（`look_mon_desc()`（target.c）が作る英語の `unhurt, asleep` など）を日本語へ直す。
 * @details 「, 」で句に割り、句ごとに `T:` を引いて「、」でつなぎ直す。引けない句は英語のまま残す。
 */
void an_look_health(char *buf, size_t cap);

/*!
 * @brief 地形の説明を日本語で組み直す。`buf` には原作どおりの英語（`prefix` + `name`）が入っている。
 * @details 名前は名前の表（`terrain`）。冠詞（`a ` `an ` `some `）は日本語には要らないので落とし、
 * `the entrance to the ` のように句を持つものは `T:`（鍵は「句 + `%s`」）で組み直す（「雑貨屋の入口」）。
 * 名前の表に無い地形は `buf` のまま（英語）。
 */
void an_look_feature(char *buf, size_t cap, const char *prefix, const char *name);
/*! @} */

/*!
 * @brief 下の状態行の次の欄までの桁（`update_statusline_aux()`。an_null_term.c）。
 * @details 日本語では実際に書いた桁まで進める。英語では `n` のまま。
 */
int an_status_advance(int row, int col, int n);

/*!
 * @brief 左の状態列の 13 桁の欄（`prt_field()`）に出す字（設計 §19.1）。
 * @details 日本語では種族・職業の名を名前の表で引き、13 桁を超えるなら 13 桁までで
 * 字の境で切る（全角は割らない。「ネクロマンサー」14 桁 →「ネクロマンサ」12 桁）。
 * 人物表ほかの画面は正式名のまま（この関数を通らない）。英語では `s` のまま。
 */
const char *an_status_field(const char *s);

/*!
 * @name 単位（設計 §19.3）
 * @details 日本語ではキログラム・センチ・メートルに換算した値を返し、英語では原作と
 * 同じ式の値を返す（英語の起動の出る字は 1 バイトも変わらない）。換算は変愚
 * （`locale/japanese.{h,cpp}`）に合わせる——アイテムの重さは 1/10 ポンドの値を
 * ×5/100 で 0.5 kg 刻み（`lb_to_kg_integer()`/`lb_to_kg_fraction()`）、体重は
 * ×4536/10000（`lb_to_kg()`）、身長は ×254/100（`inch_to_cm()`）。変愚に無い距離は
 * フィート ×3048/10000 でメートル（切り捨て）。
 * @{
 */
int an_u_wt_int(int tenth_lb);      /*!< 1/10 lb の値の整数部（英語 w/10・日本語 kg） */
int an_u_wt_frac(int tenth_lb);     /*!< 同じく小数部 1 桁（英語 w%10） */
double an_u_wt_real(int tenth_lb);  /*!< `"%.1f"` に渡す値（英語 w/10.0F） */
int an_u_ft(int feet);              /*!< 距離（英語はフィートのまま・日本語はメートル） */
int an_u_body_kg(int lb);           /*!< 体重（lb → kg） */
int an_u_body_cm(int inch);         /*!< 身長（inch → cm） */
/*! @} */

#ifdef __cplusplus
}
#endif

#endif /* AN_LANG_C_H */
