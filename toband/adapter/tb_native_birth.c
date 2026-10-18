/*!
 * @file tb_native_birth.c
 * @brief 生い立ちの編集（`edit_history()`）の入力待ちを、新しい画面（`frame.text_input_active`）へ知らせる。
 *
 * 生い立ちの編集は 4 行 × 59 桁の自前のループで、`askfor_aux()` を通らない（`inkey_special()` を直に読む）。
 * `tb_native_util.c` が包む `get_string()` では旗が立たないので、ここで別に立てる。
 * 作りは `tb_native_util.c` と同じで、原作の `birth.c` を丸ごと `#include` し、`inkey_special()` の
 * **呼び出しだけ**を、旗を立てる包み関数へ向ける（`birth.c` の中で `inkey_special()` を読むのは `edit_history()` の 1 か所だけ。
 * 誕生の選択の `get_player_choice()` は `inkey()` を読むので旗は立たない）。原作のファイルは書き換えない。
 * 組み立てでは `birth.c` を単独では組まない（`TobandCore.vcxproj`・`platform/linux/CMakeLists.txt`・
 * `android/hd2d/src/main/cpp/CMakeLists.txt`）。このファイルが取り込む。
 */

#include "angband.h"

void tb_text_input_set(int active);

/*!
 * `edit_history()` の 1 キーの待ち。日本語の 1 文字目（`iskanji()`）が来たら、2 バイト目を読む `inkey()` の間も
 * 旗を立てたままにして（旗が一瞬下りて入力欄が消えるのを避ける）、次のキーの待ちの頭で立て直す。
 */
static int tb_birth_inkey_special(bool numpad_cursor)
{
    int key;
    tb_text_input_set(1);
    key = inkey_special(numpad_cursor);
    if (!(key >= 0 && key < 256 && iskanji((char)key)))
    {
        tb_text_input_set(0); /* 生い立ちの編集は、ほかの文字入力の中では呼ばれない */
    }
    return key;
}

#define inkey_special(numpad_cursor) tb_birth_inkey_special(numpad_cursor)

#include "birth.c"

#undef inkey_special
