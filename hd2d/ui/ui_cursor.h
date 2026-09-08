/*!
 * @file ui_cursor.h
 * @brief カーソル操作（P8）— 選択肢・はい／いいえ・数値入力を**方向キーと決定で**選ぶ。
 *
 * ## なぜ UI が持つのか
 * コアは**非破壊**（必守制約 1）なので、コアに無い操作は UI 側で作るしかない。
 * 3 つとも「コアはその操作を持っていない」ことから来ている:
 *
 * | | コアの都合 | UI が補うこと |
 * |---|---|---|
 * | 選択肢（店・建物） | 1 文字キーを待つだけ。カーソルの仕組みが無い | 枠を重ね、決定で**その 1 キーだけ**送る |
 * | はい／いいえ | `input_check_strict()` は `'y'`/`'n'` しか受けず **`'\r'` を撥ねる** | 決定を答えの 1 文字へ翻訳する |
 * | 数値入力 | `askfor` の編集バッファが値の持ち主 | 上下で増減し、**バッファを書き直す** |
 *
 * ## 送る道
 * プロトコルに `answer`（yes/no）と `set_number`（値＋0 詰め桁数）が**既にある**
 * （v1 §8.3）。編集バッファの書き直し（行末へ → 全消し → 数字）はコア側のアダプタが持つので、
 * ここは**値を送るだけ**でよい。キー列を組み立てて順序を気にする必要は無い。
 *
 * ## 既存 UI との関係
 * 旧2D画面 の K-16 / K-20 / K-22 と**同じことができる**ようにしてある。
 * 実装は引いていない（必守制約 6）。あちらは `KeyQueue` へ 1 キーずつ積む作りで、
 * こちらはワイヤの出来事を積む作りなので、そもそも形が違う。
 */
#pragma once

#include "frame/game_frame.h"
#include "frame/protocol_messages.h"

#include <vector>

namespace hd2d {

//! カーソルが食う入力。方向と決定だけ（文字キーと取消は素通し）。
enum class CursorNav {
    None,
    Up,
    Down,
    Left,
    Right,
    Confirm,
};

class UiCursors {
public:
    /*!
     * @brief 新しいフレームを取り込む。
     * @details **画面が変わったらカーソルを頭へ戻す。**戻さないと、店から出た先の
     * 別の一覧で「前の画面の 5 番目」が選ばれた状態になる。
     * 画面が変わったかは**行の位置ではなくキーの並び**で見る（メッセージ 1 行で行がずれるため）。
     */
    void sync(const GameFrame &frame);

    /*!
     * @brief 方向・決定を食う。**食ったら true**（コアへは流さない）。
     * @param out 決定したときだけ、ここへ**ちょうど 1 件**積む。
     */
    bool handle(CursorNav nav, presentation::InputEventsMessage &out);

    //! いま選んでいる選択肢（`choices()` の添字）。-1 = 選択肢が出ていない。
    int choice_index() const { return this->choice_index_; }
    const std::vector<MenuChoice> &choices() const { return this->choices_; }
    //! いま選んでいる答え（`prompt().choices` の添字）。
    int prompt_index() const { return this->prompt_index_; }
    const PromptBar &prompt() const { return this->prompt_; }
    //! いま選んでいる桁（0 = 1 の位）。
    int numeric_place() const { return this->numeric_place_; }
    const NumericInput &numeric() const { return this->numeric_; }

    //! 画面と記録に出す 1 行（何が効いているか）。
    std::string to_line() const;

private:
    bool handle_numeric(CursorNav nav, presentation::InputEventsMessage &out);
    bool handle_prompt(CursorNav nav, presentation::InputEventsMessage &out);
    bool handle_choice(CursorNav nav, presentation::InputEventsMessage &out);
    //! 左右をページのキーへ翻訳する（アダプタが `menu_page_*_key` を入れた画だけ）。
    bool handle_page(CursorNav nav, presentation::InputEventsMessage &out);

    std::vector<MenuChoice> choices_;
    int choice_index_{ -1 };
    //! 画面が変わったかを見るための「キーの並び」。
    std::vector<int> choice_signature_;
    //! コアが自前のカーソルを出しているか（出ているなら UI は口を出さない）。
    bool core_owns_cursor_{ false };
    /*!
     * @brief ページを繰るキー（0 なら繰れない画）。
     * @details ヘルプのように**一覧と本文が同じ画で入れ替わる**もの用。入れるのは
     * アダプタで（`GameFrame::menu_page_prev_key` / `_next_key`）、ここは翻訳するだけ。
     */
    int page_prev_key_{ 0 };
    int page_next_key_{ 0 };

    PromptBar prompt_;
    int prompt_index_{ 0 };

    NumericInput numeric_;
    int numeric_place_{ 0 };
};

/*!
 * @brief 文字を編集している画面の矢印を、コアの編集キー（Ctrl-B/N/P/F）へ翻訳する。
 *
 * ## なぜ要るのか
 * コアの 2 つのエディタ（生い立ち `birth/history-editor.cpp` ／ 自動拾い
 * `cmd-io/cmd-autopick.cpp`）は `inkey_special()` で待ち、**矢印は `SKEY_UP` などの
 * 特殊キーとしてしか来ない**。SKEY はマクロトリガ（`lib/pref/pref-<ANGBAND_SYS>.prf`）
 * から作られるが、この線にはその表が無いので **SKEY は 1 度も発生しない**
 * （K-43 が最上位のオプション画面で踏んだのと同じ穴）。
 *
 * しかも矢印は素通りするのではなく**数字に化ける**。ui は矢印を `move` として送り、
 * アダプタが `'8'` `'2'` `'4'` `'6'` へ直す（`input_event_adapter.cpp`）ので、
 * エディタは `iscntrl` でない文字＝**本文への入力**として拾う
 * （`cmd-autopick.cpp` の `insert_single_letter`）。つまり矢印を押すと字が増える。
 *
 * ## なぜ Ctrl-B/N/P/F なのか
 * **両エディタが最初から受け付ける。**`history-editor.cpp` は `c == KTRL('p')` を
 * `SKEY_UP` と並べて見ており、自動拾いエディタは `autopick-menu-data-table.cpp` の
 * `MN_LEFT/DOWN/UP/RIGHT` が `KTRL('b'/'n'/'p'/'f')` に割り付いている。
 * `askfor`（名前・銘・検索語）も `KTRL('b')` / `KTRL('f')` を左右として持つ
 * （上下は `bell()` が鳴るだけで無害）。**コアもプロトコルも足すものが無い。**
 *
 * ## なぜ画面の見出しで判定しないのか
 * `kNamedMenus`（K-37/K-43）は見出しの文言で画面を名指しするが、その文言は
 * 翻訳の対象である。ここは**コア自身の状態**（`TextInputScope` →
 * `GameFrame::text_input_active`）だけで見るので、コアが何語で組まれていても動く。
 * この札が立つのは `askfor` と上の 2 つのエディタ——**文字を編集している場面ちょうど**である。
 *
 * ## 呼ぶ位置は `UiCursors::handle` の**後**
 * 札が立ったままでも矢印が ui のカーソルのものである画面が 3 つある:
 * 数値入力の桁カーソル（`askfor` の上に重ねている）・はい／いいえ・
 * **自動拾いエディタの ESC メニュー**（K-37。エディタの中なので札は立ったまま）。
 * どれもカーソル層が先に食うので、**食い残しだけ**をここで翻訳すれば取り違えない。
 *
 * @param nav 矢印（`Confirm` と `None` は翻訳しない＝従来どおりコアへ）。
 * @param out 食ったときだけ、ここへ**ちょうど 1 件**積む。
 * @return 食ったら true（コアへは `move` を流さない）。
 */
bool handle_text_edit_arrows(const GameFrame &frame, CursorNav nav, presentation::InputEventsMessage &out);

} // namespace hd2d
