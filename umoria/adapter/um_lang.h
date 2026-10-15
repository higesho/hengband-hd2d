#pragma once

/*
 * Umoria の日本語化の層。
 *
 * 端末の格子（um_io.cpp）は英語のまま持ち、画面側へ送る字を組む所でここを通す。
 * 英語のとき（既定）は何もしないので、出力は M4 と同じになる。
 */

#include <string>
#include <vector>

namespace umoria {

//! 画面側の言語（`ui_state.lang`）。"ja" のときだけ訳す。
void set_language(const std::string &lang);
bool lang_ja();
//! 訳した字に英語の語（小文字を含む英字 2 字以上）が残っていなければ真。
bool fully_translated(const std::string &text);
//! 記号を 1 字聞く問い（文字の説明・抹殺）の選択肢。記号と、その説明の英語（訳の表の鍵。名札は localize_frame が訳す）。
std::vector<std::pair<char, std::string>> symbol_descriptions();
//! いまの職業（原作の英語の名前）。称号は職業ごとに訳が違うので、これで引く。
void set_player_class(const std::string &class_name);

//! 経歴の部品の日本語（history.ja.txt）。`chart` は Umoria の段、`en` は原作の部品そのまま（後ろの空白も）。
bool history_fragment(int chart, const std::string &en, std::string &ja, int &ja_chart);
//! アダプタが外から決める訳（経歴の行など、原作が組み立てて折り返した字）。ほかのどの表より先に引く。
void set_screen_translation(const std::string &en, const std::string &ja);
void clear_screen_translations();
void remove_screen_translation(const std::string &en);

//! 英語の文を訳す。訳が無ければそのまま返す。英語のときは何もしない。
std::string tr(const std::string &english);

//! 端末の 1 行（`col_begin` から `col_end` の手前まで）を、書いたかたまりごとに訳して UTF-8 で返す。
struct TermLineText {
    std::string utf8;
    //! 元の桁 → `utf8` のバイト位置（`col_end - col_begin + 1` 個。最後は末尾）。選択肢の範囲を直すのに使う。
    std::vector<int> byte_at_col;
};
TermLineText term_line(int row, int col_begin, int col_end);

//! 訳の当たらなかった英語を `UMORIA_JA_MISSES` のファイルへ書く（未訳を数える道具。指定が無ければ何もしない）。
void write_misses();

} // namespace umoria
