// DCSS コアのアダプタ内部の宣言（仮のソケットの実装 dcss_posix_shim.cpp が外へ見せる口）。
#pragma once
#include <cstddef>
#include <string>

// 標準入力が閉じたとき（画面側が死んだ・パイプが切れた）に 1 度だけ呼ばれる。
// libhd2d.cpp が「ゲームを保存して終わる」ための印を立てる。
void dcss_shim_set_eof_callback(void (*cb)());

// 標準入力の読み取りスレッドを起こす。ソケットの bind() でも自動で起こされるので、
// 呼ばなくてもよい（多重に呼んでも害は無い）。
void dcss_shim_start_input();

// 制御メッセージ（1 行の JSON）を、標準入力から来たかのようにキューへ足す。
void dcss_shim_feed_line(const char *line);

// 読める制御メッセージがあるか。
bool dcss_shim_has_input();

// ---- プロトコル v1 の層（dcss_main.cpp）用 ----

// 原作が sendto() で出す JSON のかたまり（2048 バイト以下に分割される。行末の改行まで連結すると 1 メッセージ）の送り先。
// 登録すると標準出力や DCSS_JSON_OUT へは書かない。ゲームのスレッドから、g_out_mutex を持って呼ばれる。
void dcss_shim_set_sink(void (*cb)(const char *data, size_t len));

// ゲームが入力待ちに入るとき（idle）、入力が無いことを確かめに来たとき（poll。select の待ち 0）に呼ぶ。
// どちらもゲームのスレッドで呼ばれる。
void dcss_shim_set_idle_callbacks(void (*idle)(), void (*poll)());

// 原作の delay(ms)（旅・自動探索の 1 歩ごと、ビームなどの演出）から呼ぶ。その時点の絵を画面へ送り（idle と同じ）、ms だけ待つ。
// 原作の手元のタイル版は delay で描き直して眠るので、それと同じに、途中の歩みが画面に出る。ゲームのスレッドで呼ぶ。
void dcss_shim_game_delay(unsigned int ms);

// 標準入力の読み取りスレッドを使わない（プロトコル v1 の層が自前の受信スレッドから feed_line / signal_eof を呼ぶ）。
void dcss_shim_disable_stdin_reader();

// 入力が閉じた（画面が居なくなった・終了の要求）。ゲームは保存して終わる（EOF と同じ扱い）。
void dcss_shim_signal_eof();

// ---- 画面の部品（dcss-ui）用: ゲームの中の値（libhd2d.cpp が持つ。ゲームのスレッドで呼ぶ）----

// 種族と職業の略称（例 "HuFi"。原作の小さい配置のステータス欄 2 行目の字）。まだ決まっていなければ空。
std::string dcss_adapter_char_abbrev();

// タブの欄（持ち物・呪文・能力・怪物・技能・命令。段 U2。libhd2d.cpp が持つ）。
// タブの欄の中身の JSON 1 通（`{"t":"dcss_ui","k":"tabs",…}`）。キャラクターがまだ決まっていなければ空。ゲームのスレッドで呼ぶ。
std::string dcss_adapter_tabs_json();
// 画面のタブの欄への操作（`{"t":"dcss_action",…}` の JSON）を控える。受信スレッドから呼んでよい。実行はゲームのスレッドの入力待ちの中。
void dcss_adapter_queue_tab_action(const std::string &json);
// 控えた操作を入力待ちへ知らせるキーの番号。`{"msg":"key","keycode":N}` を `dcss_shim_feed_line` へ入れて入力待ちを起こす。
int dcss_adapter_tab_action_key();

// 画面の部品（dcss-ui）の地図のマウス操作（足元のクリック・ツールチップ。段 U3・U4。dcss_ui_mouse.h）。
// 画面が送る `{"msg":"hd2d_…"}` の行は、原作の制御メッセージではない。recvfrom が（ゲームのスレッドで）この受け口へ渡す。
// 戻り値は原作の入力待ちへ返す 1 行（空なら何も返さず、入力待ちを続ける）。`reply` に画面への返事（`{"t":"dcss_ui",…}`）を入れる（空なら無し）。
void dcss_shim_set_ui_line_hook(std::string (*hook)(const std::string &line, std::string &reply), void (*reply_sender)(const std::string &reply));
// 画面の部品の地図のマウス操作の処理（libhd2d.cpp が持つ。ゲームのスレッドで呼ぶ）。戻り値・`reply` は上と同じ。
std::string dcss_adapter_handle_ui_line(const std::string &line, std::string &reply);

// 画面の部品（dcss-ui）の開始メニューの中身（`{"t":"dcss_ui","k":"startup",…}` の JSON 1 通。段 U3。dcss_ui_startup.h。libhd2d.cpp が持つ）。ゲームが始まっていれば空。ゲームのスレッドで呼ぶ。
std::string dcss_adapter_startup_json();
// 画面の部品（dcss-ui）のゲームパッドの割り当てへ送る、割り当てられるコマンドの表（`{"t":"dcss_ui","k":"pad",…}` の JSON 1 通。段 5。dcss_pad_data.h。libhd2d.cpp が持つ）。ゲームが始まる前は空。ゲームのスレッドで呼ぶ。
std::string dcss_adapter_pad_json();
