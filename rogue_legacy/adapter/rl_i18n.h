#pragma once

#ifdef __cplusplus
extern "C" {
#endif

const char *rl_ja_text(const char *text);
const char *rl_ja_format(const char *format);
/*
 * 表示言語（修-46）。既定は日本語（訳す）。英語のときは rl_ja_text / rl_ja_format が原作の英語をそのまま返す。
 * 画面の言語（ui_state.lang）が変わったときに rl_main.cpp が切り替える。アダプタが直に書く日本語の字
 * （rl_frame.cpp・rl_inventory.c）も rl_i18n_is_english() を見て英語を選ぶ。
 */
void rl_i18n_set_english(int english);
int rl_i18n_is_english(void);
int rl_i18n_selftest(void);
/*
 * UTF-8 として読めなかった所を、Windows の ANSI（日本語の Windows では CP932）の 1 字として読む。
 * 読めたらバイト数（1 か 2）を返して *codepoint に入れる。読めなければ 0。Windows 以外は常に 0。
 * 原作がパス（Windows では ANSI）を文に埋めて出す所（Rogue 5.4 の保存の「ファイル名: …」）のため。
 */
int rl_decode_ansi_char(const unsigned char *p, unsigned int *codepoint);

#ifdef __cplusplus
}
#endif
