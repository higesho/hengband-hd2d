#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int rl_term_rows(void);
int rl_term_cols(void);
uint32_t rl_term_codepoint(int row, int col);
uint32_t rl_term_attr(int row, int col);
/* 原作が printf で書いた字のマスか（スコア一覧の V など。地図のマスではない）。 */
int rl_term_is_text(int row, int col);
/* 直近の画面反映が stdscr 以外の窓（help・options・add_line の頁と重ねた一覧・show_win）か。地図は出ていない。 */
int rl_page_window_shown(void);
int rl_term_cursor_row(void);
int rl_term_cursor_col(void);
void rl_term_present_now(void);
int rl_prompt_active(void);
int rl_prompt_is_item(void);
const char *rl_prompt_title(void);
int rl_prompt_choice_count(void);
int rl_prompt_choice_key(int index);
const char *rl_prompt_choice_label(int index);
void rl_message_record(const char *text);
int rl_message_history_count(void);
const char *rl_message_history_text(int index, uint32_t *seq);
int rl_text_input_active(void);
int rl_player_name_input_active(void);
/* 設定の文字列の編集を始めた時点の値（rl_option_edit_string）。 */
const char *rl_text_input_original(void);
int rl_child_input_active(void);
void rl_command_input_begin(void);
void rl_command_input_end(void);
int rl_command_input_active(void);
int rl_count_input_active(void);
int rl_count_input_value(void);
int rl_count_input_max(void);
/* 窓を閉じたことによる自動セーブを通ったか（1 度答えると戻る）。 */
int rl_shutdown_autosave_take(void);
/* 表示言語が切り替わった。命令待ちの間に、状態行などを新しい言語で書き直させる（修-46）。 */
void rl_language_changed(void);

#ifdef __cplusplus
}
#endif
