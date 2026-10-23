/*!
 * @file dcss_message.h
 * @brief DCSS のメッセージ欄（原作 `message.cc` の `message_window` の再現）。
 *
 * ## 何のためのものか
 * 原作のタイル版は、メッセージ欄を「行の並び」として持つ（`message_window`）。1 つのメッセージは
 * 欄の幅（先頭の 1 桁は印の字に使う）で折り返して行になり、欄がいっぱいになると `-more-` のあと全部を消して
 * 上から書き直す。ウェブタイル版は折り返した行を送らず、1 メッセージずつ色のタグつきの字で送る（`msgs`）ので、
 * ここで同じ規則で折り返して行を持つ。段 U1 の設計の材料。
 *
 * ## 原作のどこを写したか（`crawl-ref/source/message.cc`）
 * - `add_item`（折り返し `linebreak_string`・行ごとの先頭の印）・`make_space`（古い行を押し出す／全部消す）
 * - `new_cmdturn`・`got_input`（指令待ちに入ると最後の行の先頭を `_` にする・入力が来るとそこまでを既読にする）
 * - `more`（欄の最終行に `--more--`。色は PROMPT チャンネルの CYAN）
 *
 * ## 原作と違う所（確かめた事実・推測）
 * - `-more-` は原作では欄がいっぱいのときに自分で出すが、ここではコアが送る `more` の旗に従う（旗が立っている間だけ出す）。
 * - 取り消し（`rollback`）があったときは、持っているメッセージから行を作り直す（古い行の押し出しの状態は失う）。
 */
#pragma once

#include "dcss/dcss_cells.h"
#include "dcss/dcss_model.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace hd2d::dcss {

class DcssMessageWindow {
public:
    //! 欄の大きさ（桁数・行数）。変わったときは持っているメッセージから作り直す。
    void resize(int cols, int rows);
    //! モデルの新しいメッセージ・指令待ちの出入り・取り消しを取り込む。毎フレーム呼んでよい（変わらなければ何もしない）。
    void sync(const DcssUiModel &model);
    //! 現在の欄の内容を格子へ書く（格子は `cols`×`rows`）。
    void build(const DcssUiModel &model, DcssTextGrid &grid) const;
    //! 内容が変わるたびに増える。
    std::uint64_t version() const { return this->version_; }

    int cols() const { return this->cols_; }
    int rows() const { return this->rows_; }
    //! 最終行を除く、メッセージを置ける行数（原作 `out_height`）。
    int out_height() const { return std::max(1, this->rows_ - 1); }
    int next_line() const { return this->next_line_; }
    int line_count_for_overlay() const;

private:
    enum class Prefix { None, TurnStart, TurnEnd, NewCmd, NewTurn };

    void clear_lines();
    void rebuild(const DcssUiModel &model);
    void add_item(const DcssMessage &message);
    void make_space(int n);
    void scroll(int n);
    void output_prefix(Prefix p, bool new_turn_glyph);
    static DcssStyledChar prefix_glyph(Prefix p);

    int cols_{ 0 };
    int rows_{ 0 };
    std::vector<DcssStyledLine> lines_;
    int next_line_{ 0 };
    int input_line_{ 0 };
    Prefix prompt_{ Prefix::None };
    std::uint64_t consumed_serial_{ 0 };
    std::uint64_t rollback_seen_{ 0 };
    int last_input_mode_{ -1 };
    int last_message_turn_{ -1 };
    std::uint64_t version_{ 0 };
};

} // namespace hd2d::dcss
