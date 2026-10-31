/*!
 * @file nh_input.h
 * @brief NetHack の問い（yn）・文字入力・拡張コマンドの、描画に触れない部分。
 *
 * ## 何のためのものか
 * 〜§3.5。原作の Windows の GUI 版（`mswproc.c` の `mswin_yn_function`・`mswin_getlin`・
 * `mswin_get_ext_cmd`）の規則を写す。
 * - yn: 問いの文字は `問い [選べる字] (既定)`。選べない字は無視する（原作は `nhbell`）。Esc は選べる字の `q`・`n`・既定の順。
 *   スペース・Enter は既定。大文字小文字は同じ。コアの `ui_resolve_yn`（`nethack/adapter/nh_ui_impl.h`）と同じ決め方で、
 *   選べない字をコアへ送らない（送ると、コアは窓の閉じた画面へ返事を待ち続ける）。
 * - 文字入力: UTF-8 の欄。末尾にだけ足し、BS は 1 字（UTF-8 の文字単位）戻す。未確定の字（IME）は別に持つ。
 * - 拡張コマンド: 名前の一覧を、打った字の前方一致で絞り込む。
 */
#pragma once

#include <string>
#include <vector>

namespace hd2d::nethack {

class NhTextField {
public:
    //! 欄に入れられる最大のバイト数（名前は 31、`getlin` は 255。コアも同じ長さで切る）。
    void set_max_bytes(int bytes) { this->max_bytes_ = bytes; }
    int max_bytes() const { return this->max_bytes_; }

    //! 末尾に足す（制御文字は捨てる。上限を超える字は文字の切れ目で捨てる）。足せたバイト数を返す。
    int insert(const std::string &utf8);
    //! 末尾の 1 字（UTF-8 の文字単位）を消す。消せたら真。
    bool backspace();
    void clear();
    const std::string &text() const { return this->text_; }

    //! 変換中の字（`SDL_TEXTEDITING`。確定すると空に戻す）。表示にだけ使う。
    void set_preedit(const std::string &utf8) { this->preedit_ = utf8; }
    const std::string &preedit() const { return this->preedit_; }

private:
    std::string text_;
    std::string preedit_;
    int max_bytes_{ 255 };
};

/*!
 * @brief yn の押されたキーから答えを決める（コアの `ui_resolve_yn` と同じ規則）。
 * @param key 字の番号。Esc は 27、Enter は 13、スペースは 32
 * @param choices 選べる字（空なら何のキーでも受ける問い）
 * @param def 既定の字（無ければ 0）
 * @param[out] result 決まった答えの字
 * @return 決まったら真。選べない字は偽（もう一度待つ）
 */
bool nh_resolve_yn(int key, const std::string &choices, char def, char &result);

//! 問いの表示の字（`問い [選べる字] (既定)`）。選べる字が空なら問いのまま（括弧は問いに含まれている）。
std::string nh_yn_prompt_text(const std::string &query, const std::string &choices, char def);

//! 拡張コマンドの名前の一覧から、`typed` の前方一致（大文字小文字は同じ）の位置を一覧の順で返す。
std::vector<int> nh_ext_matches(const std::vector<std::string> &cmds, const std::string &typed);

//! 絞り込んだ名前の共通の前方（Tab の補完）。1 つなら、その名前。
std::string nh_ext_common_prefix(const std::vector<std::string> &cmds, const std::vector<int> &matches);

//! UTF-8 の先頭から `chars` 字ぶんのバイト数。
std::size_t nh_utf8_prefix_bytes(const std::string &s, std::size_t chars);

} // namespace hd2d::nethack
