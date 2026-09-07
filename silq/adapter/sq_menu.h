/*!
 * @file sq_menu.h
 * @brief 選ぶ画面をコアが**名乗る**ための層。
 *
 * ## なぜ要るのか
 * 画面側（`hd2d/ui/ui_cursor.cpp`）の枠は **`frame.menu_choices` から作る**——
 * 字は 1 文字も読まない（`ui_cursor.cpp:74`）。空だと枠を描く関数が即座に戻る
 * （`game_hud.cpp:883`）ので、**コアが名乗らない画面はコントローラーで押せない**。
 * 詰めている箇所は 変愚 41／幻想蛮怒 5／**Sil-Q 0** だった。
 *
 * ## Sil-Q 固有の事情 — 調停が要らない代わりに逃げ道も無い
 * 幻想蛮怒と変愚には `use_menu`（コアが `》` を描くカーソル式メニュー）があるが、
 * **Sil-Q には 1 つも無い**。よって `gb_menu.cpp` の `fill_core_cursor()` に当たる
 * ものは作らない。
 *
 * ただし **Sil-Q の「品書き」は別の形で自分のカーソルを持っている**（実読 2026-08-22）。
 * `initial_menu()`・鍛冶の各段・技能・誓い・設定…は `*_menu_aux(int *highlight)` の形で、
 *
 * - 選んでいる行を **`TERM_L_BLUE` で塗る**（`》` のような印は書かない）
 * - `'8'` / `'2'` で上下、`'\r'` / `' '` / `'6'` で決定、`ESC` / `'4'` で戻る
 *
 * という作りである（`silq/src` に `if (ch == '8')` が **22 か所**）。
 * 画面側の十字は割り当てが無ければ `move` として送られ、アダプタが `'8'` `'2'` へ
 * 直す（`input_event_adapter.cpp`）ので、**この形の画面はもともとパッドで動く**。
 * だからここで選択肢を名乗ってはいけない——名乗るとカーソル層が方向を食い、
 * コアの `highlight` が動かなくなる。
 *
 * **残るのは `get_item()` の一覧だけ**である。あれは `inkey()` で 1 文字を待つだけで、
 * 数字は銘の付け札（`get_tag`）に食われる。ここが「装備が替えられない」の正体だった。
 *
 * ## いちばん踏みやすい罠（`gb_menu.h` から引き継ぐ）
 * `[y/n]` と数値入力は `screen_save()` を通らない＝ **icky にならない**ので、
 * **Term ミラーには 1 行も出てこない**。行 0 を直に読まないと、パッドでは
 * 「はい」と答える手段も個数を入れる手段も無い。
 * だから `fill_row0_prompt()` は **`menu_open` に関わらず毎フレーム呼ぶ**。
 */
#pragma once

#include "frame/game_frame.h"

namespace sq {

/*!
 * @brief Term 行 0 を読んで `frame.prompt` / `frame.numeric` を埋める。
 * @details 当てる先は 2 つだけ（`silq/src` を実読して数えた。2026-08-22）:
 *
 * | 形 | 出どころ | 行 0 の字面 |
 * |---|---|---|
 * | はい／いいえ | `get_check()`（`util.c:3793`） | `<問い>[y/n]\s` |
 * | 個数 | `get_quantity()` → `term_get_string()`（`util.c:3684`） | `いくつ (0-N):\s<編集中>` |
 *
 * `[y/n]` の枠（`"%.70s[y/n] "`）は**訳さない指定**でカタログに入っている
 * （`messages.ja.txt:8641` の `J:` が空）ので、`--lang` の有無で字面が変わらない。
 * 個数のほうは訳されるが `(0-N):\s` の並びは訳文にも在る（同 `:8543`）。
 */
void fill_row0_prompt(GameFrame &frame);

/*!
 * @brief `get_item()` の品一覧を `frame.menu_choices` へ積む。
 * @return 1 件でも積んだら true
 * @details **この 1 画面しか当てない**（上の @file の註記）。見分けは行 0 の
 * `\s ESC) ` ——`object1.c:3032` の `my_strcat(out_val, " ESC", ...)` は
 * 訳のフックを 1 つも通らないので、**どの言語でも必ずこの綴りで出る**。
 */
bool fill_menu_choices(GameFrame &frame);

} // namespace sq
