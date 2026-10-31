/*!
 * @file nh_msgwin.h
 * @brief NetHack のメッセージの窓の中身（行・属性・今のターンの印・折り返し）。描画にも通信にも触れない。
 *
 * ## 何のためのものか
 * 原作の Windows の GUI 版 `win/win32/mhmsgwnd.c` のメッセージの窓の持ち方を写す。
 * - 行は新しいものを末尾に足す。最大 128 行（`MSG_LINES` の上限）。
 * - **今のターンの行と前の行で色を変える**。今のターンの行は `clear_nhwindow(WIN_MESSAGE)` のあとに足した行（`lines_last_turn`）。
 * - **同じ行への追記**（`can_append_text`）: 前の行と属性が同じで、そのターンにまだ行を足していて（`lines_not_seen` が 0 でない）、
 *   半角 2 つの空白と `--More--` を足しても 1 行に収まるなら、半角 2 つの空白を挟んで同じ行に続ける。
 * - **履歴に残さない行**（`ATR_NOHISTORY`）: 最終行を一時的に使い、次の通常の行か `clear` で元の行へ戻す。位置の選択の自動説明が使う。
 * - `--More--` を出すかの判定（`more_prompt_check`）はここでしない。コアが決めて `wait reason=more` で知らせる（設計書 T4）。
 *   ここは `wait` を受けたとき最終行に付ける字を `rows` に頼まれたときだけ足す。
 *
 * ## 折り返し
 * 窓の桁（半角を 1 とする）で折り返す。空白と日本語の禁則で切る（`dcss_wrap_line`）。原作の `DT_WORDBREAK` に近いが同じではない。
 */
#pragma once

#include <deque>
#include <string>
#include <vector>

namespace hd2d::nethack {

//! 原作の `ATR_*`（`include/wintype.h`）。
inline constexpr int kAtrNone = 0;
inline constexpr int kAtrBold = 1;
inline constexpr int kAtrDim = 2;
inline constexpr int kAtrItalic = 3;
inline constexpr int kAtrUline = 4;
inline constexpr int kAtrBlink = 5;
inline constexpr int kAtrInverse = 7;
inline constexpr int kAtrUrgent = 16;
inline constexpr int kAtrNoHistory = 32;

struct NhMsgLine {
    std::string text;
    int attr{ 0 };
};

class NhMsgWindow {
public:
    static constexpr int kMaxLines = 128;
    //! `--More--` の字の幅（半角の数）。英語の `--More--`・日本語の `--続き--` のどちらも 8。
    static constexpr int kMoreWidth = 8;

    //! 窓の見える桁（半角を 1 とする）。折り返しと追記の判定に使う。0 以下は 80。
    void set_columns(int cols) { this->cols_ = (cols > 0) ? cols : 80; }
    int columns() const { return this->cols_; }

    //! `putstr`。`nohistory` は履歴に残さない行。`attr` の `ATR_URGENT`・`ATR_NOHISTORY` の位は落として持つ。
    void put(int attr, const std::string &text, bool nohistory);
    //! `clear_nhwindow`: 今のターンの印を消す（行は残る）。
    void clear();
    //! 問いに答え終えた（原作の `MSNH_MSG_CARET` を壊したとき）。まだ見ていない行の数を 0 にする。
    void mark_seen() { this->lines_not_seen_ = 0; }

    struct Row {
        std::string text;
        int attr{ 0 };
        bool current{ false }; //!< 今のターンの行
    };
    /*!
     * @brief 下から `max_rows` 行ぶんの表示行（折り返し済み・古い順）。
     * @param more_token 空でなければ最終の行の末尾に足す（`--More--`・`--続き--`）。
     */
    std::vector<Row> rows(int max_rows, const std::string &more_token = std::string()) const;

    const std::deque<NhMsgLine> &lines() const { return this->lines_; }
    int lines_last_turn() const { return this->lines_last_turn_; }
    int lines_not_seen() const { return this->lines_not_seen_; }
    bool nohistory_active() const { return this->nohistory_active_; }

private:
    bool can_append(int attr, const std::string &text) const;
    void begin_nohistory();
    void end_nohistory();

    std::deque<NhMsgLine> lines_;
    int cols_{ 80 };
    int lines_last_turn_{ 0 };
    int lines_not_seen_{ 0 };
    bool nohistory_active_{ false };
    NhMsgLine saved_line_;
    int saved_last_turn_{ 0 };
    int saved_not_seen_{ 0 };
};

//! 末尾の改行を落とす。
std::string nh_strip_newline(std::string text);

} // namespace hd2d::nethack
