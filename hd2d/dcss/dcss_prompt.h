/*!
 * @file dcss_prompt.h
 * @brief DCSS のメッセージ欄に出る問い（`-more-`・拾うかの問い・文字入力の問い）の、マウスで押せる所を探す（GL にも SDL にも触れない）。
 *
 * ## 何のためのものか
 * 決めたこと（2026-10-23）「DCSS で階層移動のカットインや、more のメッセージ送り、メッセージの選択肢の決定等をマウスクリックで行えるようにして」。
 * 原作のタイル版は、メッセージ欄の問いをマウスで答えられない（`tilesdl.cc` の `handle_mouse` は `MOUSE_MODE_MORE`・`PROMPT`・`YESNO` の左クリックを
 * `CK_MOUSE_CLICK`、右クリックを `CK_MOUSE_CMD` にして返すだけで、`-more-` を進める以外に使う問いは拾うかの問い（一覧を開く）しか無い）。
 * ここは決めたことで足す部分で、**キーを送るのと同じ結果**にする（ゲームのルールは変えない）。
 *
 * ## 問いの形（コアの JSON の記録で確かめた。 の「メッセージ欄の問いのマウス操作」）
 * | 形 | JSON の並び | ここで見つける物 |
 * | --- | --- | --- |
 * | `-more-` | `msgs` の `more: true` → `input_mode` 5 | 最終行の `--more--`（押すとどこでも進む。強調はこの字だけ） |
 * | 拾うかの問い | `msgs`（本文に `(y)はい` `(n)いいえ` `(a)全部` `(m)一覧` が `/` で並ぶ）→ `input_mode` 8 | `(キー)ラベル` の形の字 1 つ 1 つ。押すとそのキーを送る |
 * | 文字入力の問い（「wiz」と打たせる確認・数を訊く欄・検索ほか） | `msgs`（問いの本文）→ `input_mode` 7 → `init_input`（`type` が `messages`） | 本文に `"単語"` があれば、その単語（押すと**入力欄にその単語が入るだけ**。入力欄がその単語のときにもう 1 回押す、または本文のどこかを押すと Enter で決まる。一押しでは実行されない）。本文のどこかを押すと Enter（打った字で決める） |
 * | はい/いいえの窓・カーソルで答える窓（`prompt_choose_key` ほか） | `ui-push`・`menu` | 窓の部品が既にマウスで選べる（ここでは扱わない） |
 *
 * 右クリックは、どの問いでも Esc（`-more-` は除く。原作は右クリックを無視し、Esc は「以後の `-more-` を省く」の意味になるため）。
 */
#pragma once

#include "dcss/dcss_cells.h"

#include <string>
#include <vector>

namespace hd2d::dcss {

//! メッセージ欄の中の、マウスで押せる字 1 つ。
struct DcssPromptHit {
    enum class Kind {
        More, //!< `--more--`
        Token, //!< `(キー)ラベル`
        Word, //!< `"単語"`（打たせる確認の単語。押すと入力欄に入るだけ。`word` が入力欄の字と同じなら Enter）
    };
    Kind kind{ Kind::Token };
    int row{ 0 }; //!< 格子の行（0 始まり）
    int col_begin{ 0 }; //!< 格子の桁（0 始まり。含む）
    int col_end{ 0 }; //!< 含まない
    std::vector<int> keys; //!< 押したときにコアへ送るキー（原作の符号。Enter = 13、Esc = 27）。単語は字だけ（Enter は含めない）
    std::string word; //!< 単語（引用符なし。`Kind::Word` のとき）
    std::string label; //!< 検査用: 押せる字
};

//! 見つけるための材料（画面の状態）。
struct DcssPromptInput {
    int input_mode{ 0 }; //!< 原作の `input_mode`
    bool more{ false }; //!< `-more-` が出ているか
    bool window_open{ false }; //!< 窓の部品が開いているか（開いていれば窓が答えを受ける）
    bool text_input{ false }; //!< メッセージ欄の 1 行入力が開いているか（`init_input`）
    std::string typed; //!< 1 行入力に今打たれている字（UTF-8）
    int first_row{ -1 }; //!< 最後のメッセージの先頭の行（0 始まり。無ければ -1）
    int row_count{ 0 }; //!< 最後のメッセージの行数
};

struct DcssPromptView {
    std::vector<DcssPromptHit> hits; //!< 押せる字
    //! 文字入力の問いの本文の行（押すと Enter。なければ -1）。
    int block_first_row{ -1 };
    int block_rows{ 0 };
    //! 右クリックを Esc にしてよいか。
    bool right_escape{ false };
    bool active() const { return !this->hits.empty() || (this->block_first_row >= 0) || this->right_escape; }
};

/*!
 * @brief メッセージ欄の格子から、マウスで押せる所を探す。
 * @details 何も無い・窓が開いている間は空。`(キー)ラベル` は `(` の次が空白でも括弧でもない 1 字で、そのあとに `)` とラベルの字（空白・`/`・`)`・`(` の前まで）が続くもの
 * （`(+0)` のような 2 字のものは含まない）。ラベルが次の行へ折り返す問いは、行の中に収まる分だけを押せる字にする。
 */
DcssPromptView dcss_prompt_view(const DcssTextGrid &grid, const DcssPromptInput &in);

/*!
 * 行の中の、桁（0 始まり）から表示の座標（`half` を半角 1 桁の幅とする）を求める。全角の 2 桁目（詰め物）は 1 桁目と一緒に数える。
 * 戻り値は桁の左端の x（行の左端 = 0）。
 */
int dcss_grid_col_x(const DcssTextGrid &grid, int row, int col, int half);
/*!
 * 行の中の x（行の左端 = 0）から桁（0 始まり）を求める。行の外（負・右端を超える）は -1。
 */
int dcss_grid_x_col(const DcssTextGrid &grid, int row, int x, int half);

} // namespace hd2d::dcss
