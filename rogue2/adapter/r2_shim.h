#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct r2_cell_state {
    uint16_t dungeon_flags;
    uint32_t codepoint;
    uint32_t under_codepoint;
    uint32_t attr;
    uint16_t monster_id;
    uint16_t object_id;
} r2_cell_state;

typedef struct r2_item_state {
    int key;
    uint16_t category;
    uint16_t kind;
    uint16_t object_id;
    int quantity;
    uint16_t in_use_flags;
    int is_cursed;
    int identified;
    int callable;
    char description[256];
} r2_item_state;

/*! 効果音 1 つ。汲んだら消える。 */
typedef struct r2_sound_event {
    char name[32];
    short row;
    short col;
} r2_sound_event;

int r2_game_main(int argc, char **argv);

/*
 * 言語（修-56）。ui_state の lang から受信スレッドが r2_set_language_english を呼び、ゲームのスレッドが
 * r2_language_apply（mesg の差し替え）と r2_language_service（切り替え直後の状態の行の描き直し）で反映する。
 * r2_language_english は反映済みの言語（英語なら 1）。r2_status_column_en は状態の行の桁の置き直し。
 */
void r2_set_language_english(int english);
int r2_language_english(void);
void r2_language_apply(void);
void r2_language_service(void);
int r2_status_column_en(int japanese_column);
/*
 * 画面へ書く手前の変換（修-56 の第 2 回）。英語のとき、日本語の語順で継ぎ足された字（持ち物の説明・動詞が後ろの文）を
 * 英語の語順にし、辞書（色・材質・音節）と全角の数・括弧を英語にする。変換したら out に作って 1 を返し、
 * そのまま使ってよいなら 0（out は触らない）。日本語のときは、保存された英語の断片（私用領域の字）だけ日本語に戻す。
 */
int r2_localize_text(const char *in, char *out, size_t cap);
/* 英語の表示の自己検査（修-56 の第 2 回）。日本語のメッセージ表のパスを渡す。通れば 0。 */
int r2_i18n_selftest(const char *message_path);

int r2_player_row(void);
int r2_player_col(void);
int r2_player_hp(void);
int r2_player_hp_max(void);
int r2_player_level(void);
long r2_player_gold(void);
int r2_dungeon_level(void);
const char *r2_player_name(void);
const char *r2_hunger_text(void);
const char *r2_byebye_text(void);
/*
 * 終了時の文を、原作の message() と同じ作法で行 0 へ出す。直前のメッセージ（終了の確認の問い）を消して
 * 文を出し、［続く］を付けて wait_for_ack() で決定・取消・Space を待つ（待ちの間は画面側へ［続く］付きで届く）。
 * 返ったら行 0 を消して戻る。画面が閉じられたときは getch() が r2_hd2d_quit() へ進む（ほかの待ちと同じ）。
 */
void r2_farewell_show(const char *text);
int r2_read_cell(int row, int col, r2_cell_state *out);
int r2_inventory_count(void);
int r2_inventory_item(int index, r2_item_state *out);
const char *r2_monster_name(int monster_id);
int r2_trap_type_at(int row, int col);
/* 光の届く範囲。row は画面の行（先頭行がメッセージ行）。 */
int r2_can_see(int row, int col);       /* いま見えているマス（原作の rogue_can_see） */
int r2_light_reaches(int row, int col); /* 松明風の光（半径 1）が届くマス */
int r2_player_blind(void);                /* 盲目なら真（攻撃の効果の記録が使う。読むだけ） */
/*
 * 状態のアイコン（`GameFrame::status_effects`。・CORE_HUD_ROLLOUT.md §2.3）。
 * 一時的な状態（盲目・混乱・幻覚・浮遊・加速・透明視・怪物感知・捕まっている）・空腹・鑑定済みの指輪の効果を、
 * 並べて返す（書いた件数。cap を超えたぶんは捨てる）。kind は StatusEffectKind の値（0 良い・1 悪い・4 常時・5 呪い）。
 * 名前は日本語（UTF-8。原作の mesg の空腹の字を除く）。どれも読むだけでゲームの状態は変えない。
 */
typedef struct r2_status_effect {
    char key[24];
    char label[48];
    char amount[12];
    int kind;
} r2_status_effect;
int r2_status_effects(r2_status_effect *out, int cap);
/* テスト用: 環境変数 HENGBAND_TEST_STATUS があれば、ゲームが始まった最初の 1 回だけ本物の状態を付ける。無ければ何もしない。 */
void r2_test_status_apply(void);
/* テスト用: 環境変数 HENGBAND_TEST_FX_ITEMS があれば、ゲームが始まった最初の 1 回だけ、指定した杖・巻物・武器を持ち物へ足す。無ければ何もしない。 */
void r2_test_fx_items_apply(void);
/* 状態のアイコンの集め方の自己検査（`Rogue2Core.exe --selftest` から）。0 なら全部合った。 */
int r2_status_effects_selftest(void);
void r2_message_history_push(const char *text);
int r2_message_history_count(void);
int r2_message_history_get(int index, char *out, int out_size, uint32_t *seq);
const char *r2_message_history_text(int index, uint32_t *seq);
int r2_text_set(char **target, const char *text);
int r2_text_set_n(char **target, const char *text, size_t bytes);
int r2_text_setf(char **target, const char *format, ...);
int r2_text_append(char **target, const char *text);
int r2_text_appendf(char **target, const char *format, ...);
void r2_text_clear(char **target);
const char *r2_text_cstr(const char *text);

void r2_sound_set_wanted(int wanted);
void r2_sound_push(const char *name);
void r2_sound_push_at(const char *name, int row, int col);
int r2_sound_take(r2_sound_event *out, int cap);

#ifdef __cplusplus
}
#endif
