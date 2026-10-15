#pragma once

/*
 * Umoria の端末（上流 `ui_io.cpp` の代わり。um_io.cpp）とプロトコル側（um_main.cpp）の境目。
 * 端末は 24×80 の字の格子だけを持ち、curses は使わない（「方式」）。
 */

#include <cstdint>

namespace umoria {

constexpr int kTermRows = 24;
constexpr int kTermCols = 80;

struct HostHooks {
    void (*present)() = nullptr;
    //! 積まれたキーを 1 つ取る。無ければ 0。
    int (*next_key)() = nullptr;
    void (*sleep_ms)(int) = nullptr;
    //! 画面側が居なくなった・終了を求めたら真。
    bool (*shutdown_requested)() = nullptr;
};

void set_host_hooks(const HostHooks &hooks);

char term_char(int row, int col);
//! そのマスを書いたかたまり（run）の番号。0 は空き（M5。um_lang.cpp が訳す単位）。
std::uint32_t term_run(int row, int col);
int term_cursor_row();
int term_cursor_col();
//! getStringInput（名前などの文字入力）の最中なら真。
bool term_text_input_active();
//! 文字入力の欄が始まる桁（行は 0 とは限らないが、店の交渉では 0 行目）。
int term_text_input_col();
//! 文字入力の欄の行。
int term_text_input_row();
//! terminalSaveScreen() の後、terminalRestoreScreen() の前（持ち物・ヘルプなどの画面を出している間）なら真。
bool term_screen_saved();
//! 退避した後に、地図の場所（1〜22 行目・状態の列より右）へ字の列を書いたなら真（持ち物の一覧などの画面を出した）。
bool term_overlay_since_save();

//! いま待っている問いの種類（M3。画面側へ選択肢を出すため）。
enum class Question {
    None,
    Confirm, //!< `getInputConfirmationWithAbort`（[y/n]）
    Command, //!< `getCommand`（1 キーの問い。方向・呪文・記号など。文言は 1 行目）
    Tile, //!< `getTileCharacter`（記号を 1 字。文字の説明・抹殺の巻物）。画面側が記号の一覧から選べるようにする
};
Question term_question();

} // namespace umoria
