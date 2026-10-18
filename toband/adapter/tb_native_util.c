/*!
 * @file tb_native_util.c
 * @brief 自由文字入力の待ちを、新しい画面（`frame.text_input_active`）へ知らせる。
 *
 * ## なぜ要るか
 * 画面側は、コアが文字入力を待っている間だけ入力欄を出し、非 ASCII の文字（日本語）を送る
 * （`frame.text_input_active`）。幻想蛮怒は原作の `askfor_aux` を
 * レシピで書き換えて旗を立てているが、TOband の原作は旗を立てない。旗が偽のままだと、
 * 名前・銘の刻印・検索語などの入力欄が新しい画面に出ず、日本語も打てない。
 *
 * ## 作り
 * `tb_native_store.c` が `store.c` を取り込むのと同じ作り。原作の `util.c` を丸ごと `#include` し、
 * 文字を入力させる 2 つの関数 `get_string()` と `askfor()` の**定義の名前だけ**を `tb_orig_*` に替える。
 * 本来の名前では、旗を立てて `tb_orig_*` を呼ぶ包み関数を、このファイルの末尾で定義する。
 * 呼ぶ側（`cmd4.c`・`files.c` ほか）は変えなくても、リンク時にこの包み関数へ届く。原作のファイルは書き換えない。
 *
 * - `askfor_aux()` は包まない。`get_quantity()`（数の入力）が使い、数の入力は別の仕組み
 *   （`parse_numeric_input_line`。行 0 の「数: 」）で新しい画面に届いているため。
 * - 組み立てでは `util.c` を単独では組まない（`TobandCore.vcxproj`・`platform/linux/CMakeLists.txt`・
 *   `android/hd2d/src/main/cpp/CMakeLists.txt`）。このファイルが取り込む。
 */

#include "angband.h"

void tb_text_input_set(int active);
int tb_text_input_active(void);

/* 本来の名前の定義を、取り込む間だけ別の名前にする。包み関数は取り込んだ後で定義する。 */
#define askfor tb_orig_askfor
#define get_string tb_orig_get_string
bool tb_orig_askfor(char *buf, int len);
bool tb_orig_get_string(cptr prompt, char *buf, int len);

#include "util.c"

#undef askfor
#undef get_string

/*! 入力の待ちの間だけ旗を立て、抜けたら元の値へ戻す（入れ子になっても外側の旗を消さない）。 */
bool askfor(char *buf, int len)
{
    const int previous = tb_text_input_active();
    bool result;
    tb_text_input_set(1);
    result = tb_orig_askfor(buf, len);
    tb_text_input_set(previous);
    return result;
}

bool get_string(cptr prompt, char *buf, int len)
{
    const int previous = tb_text_input_active();
    bool result;
    tb_text_input_set(1);
    result = tb_orig_get_string(prompt, buf, len);
    tb_text_input_set(previous);
    return result;
}
