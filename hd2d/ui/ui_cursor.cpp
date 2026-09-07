/*!
 * @file ui_cursor.cpp
 * @brief `ui_cursor.h` の実装。
 */
#include "ui/ui_cursor.h"

#include "ui/ui_layout.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace hd2d {

namespace {

int vertical_delta(CursorNav nav)
{
    if (nav == CursorNav::Up) {
        return -1;
    }
    return (nav == CursorNav::Down) ? 1 : 0;
}

int horizontal_delta(CursorNav nav)
{
    if (nav == CursorNav::Left) {
        return -1;
    }
    return (nav == CursorNav::Right) ? 1 : 0;
}

} // namespace

void UiCursors::sync(const GameFrame &frame)
{
    /*
     * --- 選択肢（K-16 相当）---
     * **コアが自分のカーソル（`》`）を出している画面では口を出さない。**
     * `cursor_mode` を立ててあるので、コマンドメニュー・呪文・持ち物はコアが
     * カーソルを持つ。両方が動くと 2 つのカーソルが別々の行を指す。
     */
    this->core_owns_cursor_ = !frame.menu_core_cursors.empty() || (frame.menu_core_cursor.line_index >= 0);

    std::vector<int> signature;
    signature.reserve(frame.menu_choices.size());
    for (const auto &choice : frame.menu_choices) {
        signature.push_back(choice.key);
    }
    if (signature != this->choice_signature_) {
        this->choice_signature_ = signature;
        this->choice_index_ = frame.menu_choices.empty() ? -1 : 0;
        /*
         * **Enter の選択肢があるなら、初期カーソルはそこへ**（2026-08-11。気づいたこと
         * 「新規で開始する際、名前選択の手前で決定ができない」の正体）。
         *
         * 能力値のロール確認は `['r' 次の数値, 'h' 生い立ちを表示, Enter この数値に決定]` の
         * 3 択で、K-24 が `Enter` も選択肢として拾う（拾わないと A ボタンから決定できない）。
         * だが初期カーソルが**先頭の `'r'`** だったので、画面が「Enter この数値に決定」と
         * 言っているのに、**Enter を押すとカーソル層が `'r'`（振り直し）へ翻訳して送る**。
         * 何度押しても振り直しになり、「決定が効かない」に見えた。
         *
         * `Enter` という選択肢は、その画面の**既定の動作**をコアが名指ししたものである。
         * 初期カーソルをそこへ置けば、素の Enter は画面の説明どおりに働き、
         * `'r'` を選びたい人は矢印で動かしてから決定すればよい（パッドも同じ道）。
         */
        for (std::size_t i = 0; i < frame.menu_choices.size(); ++i) {
            if (frame.menu_choices[i].key == '\r') {
                this->choice_index_ = static_cast<int>(i);
                break;
            }
        }
    }
    this->choices_ = frame.menu_choices;
    /*
     * --- ページを繰るキー（2026-08-29 に決めた「ヘルプで左右でページ送りを」）---
     * **入れるのはアダプタ**（`GameFrame::menu_page_*_key`）。ここは翻訳するだけで、
     * コアの文言を 1 つも読まない。0 のままのコアでは左右は今までどおり選択肢を動かす。
     */
    this->page_prev_key_ = frame.menu_page_prev_key;
    this->page_next_key_ = frame.menu_page_next_key;
    if (this->choices_.empty()) {
        this->choice_index_ = -1;
    } else if ((this->choice_index_ < 0) || (this->choice_index_ >= static_cast<int>(this->choices_.size()))) {
        this->choice_index_ = 0;
    }

    // --- はい／いいえ（K-20 相当）。本文が変われば「はい」へ戻す ---
    if (frame.prompt.text_utf8 != this->prompt_.text_utf8) {
        this->prompt_index_ = 0;
    }
    this->prompt_ = frame.prompt;
    if (this->prompt_.choices.empty()) {
        this->prompt_index_ = 0;
    } else {
        this->prompt_index_ = std::clamp(this->prompt_index_, 0, static_cast<int>(this->prompt_.choices.size()) - 1);
    }

    // --- 数値入力（K-22 相当）。別の入力になったら 1 の位へ ---
    if (frame.numeric.prompt_utf8 != this->numeric_.prompt_utf8) {
        this->numeric_place_ = 0;
    }
    this->numeric_ = frame.numeric;
    if (!this->numeric_.active) {
        this->numeric_place_ = 0;
    } else {
        this->numeric_place_ = std::clamp(this->numeric_place_, 0, std::max(1, this->numeric_.digits) - 1);
    }
}

bool UiCursors::handle_numeric(CursorNav nav, presentation::InputEventsMessage &out)
{
    if (!this->numeric_.active) {
        return false;
    }
    const int digits = std::max(1, this->numeric_.digits);
    if (nav == CursorNav::Confirm) {
        /*
         * **決定は数値入力が食う**（P10 レビュー 12。こう気づいた——「店の数量指定で
         * 数量の入力と確定が出来ず先に進まない」）。
         *
         * 食わずに落とすと `handle_choice` まで流れ、**背後に写っている店の選択肢が
         * 拾って「選んでいる店コマンドの 1 キー」を送る**。個数はいつまでも確定せず、
         * 代わりに店のコマンドが走るので「先に進まない」になる。
         * `handle()` の順序（数値 → プロンプト → 選択肢）はそのために書かれているのに、
         * ここが false を返していたので順序が何の役にも立っていなかった。
         *
         * コアの `askfor` は Enter で確定する（`input_check_strict()` が `'\r'` を撥ねる
         * はい／いいえとは違い、ここは翻訳が要らない）。そのまま送る。
         */
        presentation::InputEventWire wire;
        wire.e = "confirm";
        out.events.push_back(wire);
        return true;
    }
    const int h = horizontal_delta(nav);
    const int v = vertical_delta(nav);
    if ((h == 0) && (v == 0)) {
        return false; // 数字キー・ESC はそのままコアへ
    }
    if (h != 0) {
        //! 画面の並びどおり: ← が上の桁（左）、→ が下の桁（右）。
        this->numeric_place_ = std::clamp(this->numeric_place_ - h, 0, digits - 1);
        return true;
    }

    int step = 1;
    for (int i = 0; i < this->numeric_place_; ++i) {
        step *= 10;
    }
    //! `v` は上が -1・下が +1。**上で増える**向きに揃える。
    const int next = std::clamp(this->numeric_.value - (v * step), this->numeric_.min, this->numeric_.max);
    if (next == this->numeric_.value) {
        return true; // 端。キーはコアへ流さない（方向コマンドの誤爆を防ぐ）
    }

    /*
     * **0 詰めで送る。**桁カーソルは画面の数字 1 文字に枠を重ねるので、
     * 「12」に対して 100 の位を選ぶと重ねる文字が無い。`std::stoi` は先頭の 0 を
     * 無視するのでコア側の解釈は変わらない。
     */
    presentation::InputEventWire wire;
    wire.e = "set_number";
    wire.number = next;
    wire.digits = digits;
    out.events.push_back(wire);

    //! 次のフレームが来るまでの表示を合わせる（枠の位置もこれで決まる）。
    this->numeric_.value = next;
    this->numeric_.text_len = digits;
    return true;
}

bool UiCursors::handle_prompt(CursorNav nav, presentation::InputEventsMessage &out)
{
    if (this->prompt_.choices.empty()) {
        return false;
    }
    const int count = static_cast<int>(this->prompt_.choices.size());
    if (nav == CursorNav::Confirm) {
        /*
         * コアは `'\r'` を撥ねる（`asking-player.cpp` の `input_check_strict()` が `bell()`）。
         * **選んでいる答えの 1 文字へ翻訳して送る。**
         */
        presentation::InputEventWire wire;
        const int key = this->prompt_.choices[static_cast<std::size_t>(this->prompt_index_)].key;
        if ((key == 'y') || (key == 'n')) {
            wire.e = "answer";
            wire.value = (key == 'y') ? "yes" : "no";
        } else {
            //! `[S]弾, [A]矢` のような、はい／いいえではないプロンプト（K-37 相当）。
            wire.e = "key";
            wire.chr = std::string(1, static_cast<char>(key));
        }
        out.events.push_back(wire);
        return true;
    }
    const int v = vertical_delta(nav);
    const int h = horizontal_delta(nav);
    const int delta = (v != 0) ? v : h;
    if (delta == 0) {
        return false; // 文字キー（y/n）・ESC は従来どおりコアへ
    }
    this->prompt_index_ = ((this->prompt_index_ + delta) + count) % count;
    return true;
}

bool UiCursors::handle_choice(CursorNav nav, presentation::InputEventsMessage &out)
{
    if (this->choices_.empty() || (this->choice_index_ < 0) || this->core_owns_cursor_) {
        return false;
    }
    const int count = static_cast<int>(this->choices_.size());
    if (nav == CursorNav::Confirm) {
        //! **積むのはちょうど 1 件。**ここで方向を足すと、コアが受け取る順序が狂う。
        presentation::InputEventWire wire;
        const int key = this->choices_[static_cast<std::size_t>(this->choice_index_)].key;
        if (key == 0x1B) {
            wire.e = "cancel";
        } else if (key == '\r') {
            /*
             * **Enter の選択肢は `confirm` で送る**（2026-08-11。気づいたこと
             * 「Enter この数値に決定にカーソルがある状態で Enter を押しても動かない」）。
             *
             * `key` イベントの `chr` は**復号側が印字文字（0x20〜0x7E）しか受けない**
             * （`protocol_messages.cpp` の検証）。`'\r'`（0x0D）で送ると**黙って捨てられ**、
             * 何も起きない——制御キーには専用のイベント（confirm / cancel）を使うのが
             * この線の約束で、ここだけがそれを破っていた。
             * `confirm` はアダプタで 0x0D になる（数値入力の確定と同じ道）。
             */
            wire.e = "confirm";
        } else if (key < 0x20) {
            /*
             * **その他の制御キーの札**（`Tab) More Info` など。Frox の誕生画面が
             * `Tab)` を札として申告する——FH-12。2026-08-24）。`key` イベントの
             * `chr` は印字文字しか運べない（上の `'\r'` の註と同じ検証）ので、
             * **ctrl 付きの文字へ畳んで送る**——Tab は `^I`。アダプタが `c & 0x1F`
             * で元の制御キーに戻す（`handle_text_edit_arrows` と同じ道）。
             */
            wire.e = "key";
            wire.chr = std::string(1, static_cast<char>('a' + key - 1));
            wire.ctrl = true;
        } else {
            wire.e = "key";
            wire.chr = std::string(1, static_cast<char>(key));
        }
        out.events.push_back(wire);
        return true;
    }

    const int h = horizontal_delta(nav);
    if (h != 0) {
        this->choice_index_ = ((this->choice_index_ + h) + count) % count;
        return true;
    }
    const int v = vertical_delta(nav);
    if (v == 0) {
        return false; // 文字キーはそのままコアへ（従来どおり 1 文字でも選べる）
    }

    /*
     * 建物は 2 列・店は 3 列に選択肢が並ぶ。上下は**隣の行の、いちばん桁が近い選択肢**へ。
     * 単純に添字 ±1 にすると、2 列の画面で上下と左右が同じ動きになって列を渡れない。
     */
    const MenuChoice &current = this->choices_[static_cast<std::size_t>(this->choice_index_)];
    int best = -1;
    int best_line = 0;
    int best_dx = 0;
    for (int i = 0; i < count; ++i) {
        const MenuChoice &candidate = this->choices_[static_cast<std::size_t>(i)];
        const bool wrong_side = (v < 0) ? (candidate.line_index >= current.line_index)
                                        : (candidate.line_index <= current.line_index);
        if (wrong_side) {
            continue;
        }
        const int dx = std::abs(candidate.span_begin - current.span_begin);
        const bool nearer_line = (best < 0)
            || ((v < 0) ? (candidate.line_index > best_line) : (candidate.line_index < best_line));
        if (nearer_line) {
            best = i;
            best_line = candidate.line_index;
            best_dx = dx;
            continue;
        }
        if ((candidate.line_index == best_line) && (dx < best_dx)) {
            best = i;
            best_dx = dx;
        }
    }
    if (best >= 0) {
        this->choice_index_ = best;
    }
    return true; // 端でも食う（方向コマンドが店の画面へ漏れない）
}

bool UiCursors::handle_page(CursorNav nav, presentation::InputEventsMessage &out)
{
    const int key = (nav == CursorNav::Left) ? this->page_prev_key_
        : ((nav == CursorNav::Right) ? this->page_next_key_ : 0);
    //! `key` イベントの `chr` は印字文字しか運べない（`protocol_messages.cpp` の検証）。
    if ((key < 0x20) || (key > 0x7E)) {
        return false;
    }
    presentation::InputEventWire wire;
    wire.e = "key";
    wire.chr = std::string(1, static_cast<char>(key));
    out.events.push_back(wire);
    return true;
}

bool UiCursors::handle(CursorNav nav, presentation::InputEventsMessage &out)
{
    if (nav == CursorNav::None) {
        return false;
    }
    if (ui_break_enabled("cursor")) {
        return false; // `HD2D_BREAK_UI=cursor`。**検査が FAIL になるのが正しい。**
    }
    /*
     * **順序が意味を持つ。**数値入力とプロンプトは選択肢より手前にある
     * （店で個数を聞かれている間も背後の店画面は写り続けるので、選択肢も同時に「出ている」）。
     * ここで先に食わないと、個数入力の決定が「選んでいる店コマンドの 1 キー」に化ける。
     */
    if (this->handle_numeric(nav, out)) {
        return true;
    }
    if (this->handle_prompt(nav, out)) {
        return true;
    }
    /*
     * **ページは選択肢より先。**ヘルプの目次は 1 列に並ぶので、左右を選択肢の送りに
     * 使っても上下と同じ動きにしかならない。ページに割り当てるほうが手が増える。
     * 本文の画には選択肢が 1 つも無いので、ここで食わないと繰る手が無い。
     */
    if (this->handle_page(nav, out)) {
        return true;
    }
    return this->handle_choice(nav, out);
}

std::string UiCursors::to_line() const
{
    std::string line = "cursor";
    if (this->numeric_.active) {
        line += " numeric(" + std::to_string(this->numeric_.value) + " 桁" + std::to_string(this->numeric_place_) + ")";
    }
    if (!this->prompt_.choices.empty()) {
        line += " prompt(" + std::to_string(this->prompt_index_) + "/"
            + std::to_string(this->prompt_.choices.size()) + ")";
    }
    if (!this->choices_.empty()) {
        line += " choice(" + std::to_string(this->choice_index_) + "/" + std::to_string(this->choices_.size()) + ")";
    }
    if (this->core_owns_cursor_) {
        line += " コアが持っている";
    }
    if ((this->page_prev_key_ != 0) || (this->page_next_key_ != 0)) {
        line += " ページ送り可";
    }
    return line;
}

bool handle_text_edit_arrows(const GameFrame &frame, CursorNav nav, presentation::InputEventsMessage &out)
{
    if (!frame.text_input_active) {
        return false;
    }
    if (ui_break_enabled("textedit")) {
        return false; // `HD2D_BREAK_UI=textedit`。**検査が FAIL になるのが正しい。**
    }
    char key = '\0';
    switch (nav) {
    case CursorNav::Up:
        key = 'p';
        break;
    case CursorNav::Down:
        key = 'n';
        break;
    case CursorNav::Left:
        key = 'b';
        break;
    case CursorNav::Right:
        key = 'f';
        break;
    default:
        return false; // 決定・文字キー・ESC は従来どおりコアへ
    }
    presentation::InputEventWire wire;
    wire.e = "key";
    wire.chr = std::string(1, key);
    wire.ctrl = true; //!< アダプタが `c & 0x1F` で KTRL にする（`input_event_adapter.cpp`）
    out.events.push_back(wire);
    return true;
}

} // namespace hd2d
