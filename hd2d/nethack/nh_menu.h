/*!
 * @file nh_menu.h
 * @brief NetHack のメニューの窓の状態と操作（選ぶ・個数・ページ送り・検索）。描画にも通信にも触れない。
 *
 * ## 何のためのものか
 * 原作の Windows の GUI 版 `win/win32/mhmenu.c`（`onListChar`・`SelectMenuItem`）の操作を写す。
 * コアから届く `menu`（§3.1。`how` は 0 見るだけ・1 一つ選ぶ・2 いくつでも）の項目を持ち、キーとマウスの操作を受けて、
 * 決定か取り消しかを返す。返事の形（`nethack_reply` の `menu`・`dismiss`・`cancel`）もここで作る。
 *
 * ## 原作と同じ操作
 * - 項目の字（`ch`）で選ぶ（`how` が 2 は反転、1 は選んでその場で決定）。字は大文字小文字を区別する。同じ字の項目が複数あるときは、
 *   見えている先頭の行から順に探す（`mhmenu.c` の `iter = topIndex`）。
 * - グループの字（`gch`）で、その字を持つ項目をまとめて選ぶ（`how` が 1 のときは先頭の項目で決定）。
 * - ページ送り `>`・`<`・`^`（先頭）・`|`（末尾）。`.` 全選択・`-` 全解除・`@` 反転（ページ単位は `,`・`\`・`~`）。`:` 検索（文字列を含む項目を選ぶ）。
 * - 数字で個数（フォーカスの項目。`how` が 2 のときだけ使える）。Esc は個数の入力中なら個数を消すだけ。
 * - スペース: 「NetHack モード」（原作の既定）では、最後のページでなければ次のページ、最後なら決定。
 * - `how` が 1 のとき、矢印でフォーカスを動かすとその項目が選ばれる（ListView の選択がフォーカスに従う）。Enter はその項目で決定。
 * - 字の無い選べない行（見出し）は選べない。矢印の移動では飛ばす。
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hd2d::nethack {

struct NhMenuItem {
    int index{ 0 };       //!< 項目の位置（1 から。返事の `i`）。0 は選べない行（見出し・説明）
    std::string ch;       //!< 選ぶ字（1 文字。無ければ空）
    std::string gch;      //!< グループの字（1 文字。無ければ空）
    int attr{ 0 };        //!< 原作の `ATR_*`
    int clr{ 8 };         //!< 原作の色（0〜15。8 は指定なし。見出しは -1）
    bool preselected{ false };
    int glyph{ -1 };      //!< 原作の glyph（絵が無ければ -1）
    int tile{ -1 };       //!< コアが絵の番号を入れたときの値（普通は -1。画面は glyph から引く）
    std::string text;
    bool selectable() const { return this->index > 0; }
};

enum class NhMenuResult { None, Submit, Cancel };

//! 窓の操作で使うキー（画面の側が SDL のキーから直す）。
enum class NhMenuKey { Up, Down, PageUp, PageDown, Home, End, Enter, Escape, Space, Backspace };

class NhMenu {
public:
    //! `menu` の知らせ（JSON 1 通の字）を取り込む。壊れている・`menu` でないときは偽。
    bool load(const std::string &payload);

    int request() const { return this->request_; }
    int window_id() const { return this->window_id_; }
    int how() const { return this->how_; }
    const std::string &prompt() const { return this->prompt_; }
    const std::vector<NhMenuItem> &items() const { return this->items_; }
    int size() const { return static_cast<int>(this->items_.size()); }

    //! 項目 `pos`（0 から）の個数。0 は選ばれていない、-1 は全部、1 以上は個数の指定。
    int count_of(int pos) const;
    bool is_selected(int pos) const { return this->count_of(pos) != 0; }
    //! 選ばれている項目の数。
    int selected_total() const;

    int cursor() const { return this->cursor_; }
    int top() const { return this->top_; }
    //! 一度に見える行数（描く側が毎フレーム渡す。ページ送りの幅になる）。
    void set_page_rows(int rows);
    int page_rows() const { return this->page_rows_; }
    //! 最終行が見えているか。
    bool bottom_visible() const;
    bool counting() const { return this->counting_; }

    //! 検索の入力中か（`:`）と、その入力。
    bool searching() const { return this->searching_; }
    const std::string &search_text() const { return this->search_text_; }

    //! 字 1 つ（ASCII。`SDL_TEXTINPUT` の 1 字）。
    NhMenuResult on_char(char ch);
    //! 検索の入力中の文字（UTF-8 の列）。入力中でなければ何もしない。
    void on_search_text(const std::string &utf8);
    NhMenuResult on_key(NhMenuKey key);
    //! 行 `pos` のクリック。`how` が 2 は反転、1 は選ぶ（ダブルクリックで決定）。
    NhMenuResult click_row(int pos, bool double_click);
    //! ホイール。`rows` が正で下へ。
    void scroll(int rows);

    //! 決定の返事（`nethack_reply`）の JSON の字。`how` が 0 のときは `dismiss`。
    std::string submit_json() const;
    //! 取り消しの返事。`how` が 0 のときは `dismiss`。
    std::string cancel_json() const;

private:
    void set_count(int pos, int count);
    void ensure_visible(int pos);
    void move_cursor(int pos);
    int next_selectable(int from, int step) const;
    void clamp_top();
    void select_all_where(int from, int to, int mode);
    void apply_search();
    NhMenuResult page_down();
    NhMenuResult select_and_submit(int pos);

    int request_{ 0 };
    int window_id_{ -1 };
    int how_{ 0 };
    std::string prompt_;
    std::vector<NhMenuItem> items_;
    std::vector<int> counts_;
    std::string group_chars_;
    int cursor_{ 0 };
    int top_{ 0 };
    int page_rows_{ 10 };
    bool counting_{ false };
    bool searching_{ false };
    std::string search_text_;
};

} // namespace hd2d::nethack
