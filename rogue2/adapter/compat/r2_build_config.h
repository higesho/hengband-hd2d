#pragma once

#include <stddef.h>
#include <stdio.h>

#ifndef PACKAGE_STRING
#define PACKAGE_STRING "rogueclone2s 6.0"
#endif

/*
 * 原作の色（config.h の COLOR）。組み立てが HAVE_CONFIG_H を渡さないので原作の config.h は
 * 読まれず、COLOR が未定義のままだった。このヘッダは Windows / Linux / Android の 3 つの
 * 組み立てすべてで C ファイルに強制 include されるので、ここで定義すれば 3 つとも揃う
 * （色の設定を戻してアスキー表示のモードで効かせる。2026-09-24）。
 */
#ifndef COLOR
#define COLOR 1
#endif

/*
 * mesg[] は起動時に一度読む不変カタログ。読み込み側は境界を守る。
 * 戦闘中に連結・成長する文字列にはこの固定幅を使わず、下の可変長APIを使う。
 */
#define R2_MESSAGE_BYTES 256

/* Shift-JIS 時代の固定バイト数を UTF-8 の作業領域として使わない。 */
#define R2_TEXT_BYTES 512
#define R2_TITLE_BYTES 128

/*
 * 可変長テキスト。戦闘メッセージやメッセージ表のように入力長が決まらない
 * 文字列は固定長配列へ押し込まず、必要な長さだけ確保する。
 */
int r2_text_set(char **target, const char *text);
int r2_text_set_n(char **target, const char *text, size_t bytes);
int r2_text_setf(char **target, const char *format, ...);
int r2_text_append(char **target, const char *text);
int r2_text_appendf(char **target, const char *format, ...);
void r2_text_clear(char **target);
const char *r2_text_cstr(const char *text);

/* RogueClone2の一部ファイルはdisplay.hを直接includeせず、この関数を使う。 */
int utf8strlen(const char *text);

/* HD2D 版の同期保存。原作ソースには宣言を足さず、強制 include から見せる。 */
void r2_autosave_turn(void);
void r2_autosave_boundary(void);
void r2_hd2d_quit(void);
void r2_save_extra(void *fp);
void r2_load_extra(void *fp);
/* 設定画面の項目。原作は環境変数 ROGUEOPTS からしか読まないので、HD2D 版で保存する。 */
void r2_options_save(void);
/* 文字列の設定（name / fruit / map）を 1 行確定するたびに反映して保存する。 */
void r2_options_apply_string(int index, const char *value);
/* 名前の初期値（アプリ側の PlayerIdentity）。空文字列なら「無い」。 */
const char *r2_host_player_name(void);
/* 原作の clean_up() の終了文をアダプタへ渡す（画面側へ exit / fatal で届ける）。 */
void r2_host_clean_up(const char *estr);
/* 画面側のキャンセルで休息・探索・走行を中断する（原作の SIGINT の代わり）。 */
void r2_interrupt_poll(void);
/*
 * 得点ファイルの置き場（自動セーブと同じフォルダ）。無ければ空文字列。原作の put_scores() は
 * 置き場のパスを char file[100] へ長さの確認なしに写すので、実際のパスは返さず、短い目印
 * "@r2home/" を返す。目印で始まるパスは fopen マクロ（下）が r2_fopen_redirect() 経由で
 * 実際の置き場へ置き換える。
 */
const char *r2_home_dir(void);
FILE *r2_fopen_redirect(const char *path, const char *mode);
/* 得点表をゲームを終えずに見る。put_scores() が r2_score_view_only を見て戻る。 */
void r2_show_scores(void);
extern int r2_score_view_only;

void r2_autosave_set_path(const char *path);
void r2_autosave_invalidate(void);
extern char r2_rand_pos[];

/* 原作には複数行のメッセージ履歴が無いので、HD2D 側で直近を保持する。 */
void r2_message_history_push(const char *text);

int r2_term_snapshot_push(void);
int r2_term_snapshot_pop(void);
void r2_term_modal_begin(void);
void r2_term_modal_end(void);
int r2_term_modal_active(void);
void r2_term_text_input_begin(void);
void r2_term_text_input_end(void);
int r2_term_text_input_active(void);
void r2_term_numeric_begin(int value);
void r2_term_numeric_set(int value);
void r2_term_numeric_end(void);
int r2_term_numeric_active(void);
int r2_term_numeric_value(void);
void r2_term_char_prompt_begin(int kind);
void r2_term_char_prompt_end(void);
int r2_term_char_prompt_kind(void);
void r2_term_pack_prompt_begin(unsigned int mask);

/*
 * 効果音。鳴らすのは画面側で、コアは出来事の
 * 名前だけを書き留める。画面側が欲しがっているときしか積まない（`r2_sound_set_wanted`）。
 * 位置が分からない音は `r2_sound_push` で積み、呼び出し箇所が実際のマスを持っていれば
 * `r2_sound_push_at` で積む（Frox・Sil-Q と同じ流儀）。
 */
void r2_sound_set_wanted(int wanted);
void r2_sound_push(const char *name);
void r2_sound_push_at(const char *name, int row, int col);
/* HP が残り 1/4 以下に落ちた境目でだけ rogue_hungry ならぬ rogue_hitpoint_warn を積む。 */
void r2_hp_warn_check(int hp_current, int hp_max, int row, int col);
void r2_term_pack_prompt_end(void);
unsigned int r2_term_pack_prompt_mask(void);
void r2_term_command_input_begin(void);
void r2_term_command_input_end(void);
int r2_term_command_input_active(void);

/*
 * 原作の fopen を、得点ファイルの置き場の目印を実際のパスに置き換える代役へ向ける
 * （r2_home_dir() の註釈を参照）。強制 include なので Windows / Linux / Android の C ファイル
 * すべてに効く。<stdio.h> は上で読み込み済みなので、システムの宣言は書き換わらない。
 */
#define fopen(path, mode) r2_fopen_redirect((path), (mode))
