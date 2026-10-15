/*!
 * @file an_lang.h
 * @brief 日本語層のカタログの読み込み（C++ 側）。
 * @details 引く口は `an_lang_c.h`（C）。こちらは起動と検査からだけ呼ぶ。
 */
#ifndef AN_LANG_H
#define AN_LANG_H

#include <string>
#include <vector>

namespace an {

struct LangLoadReport {
    bool enabled{ false };
    int entries{ 0 };      //!< `messages.ja.txt` の件数（札を問わず）
    int names{ 0 };        //!< 名前の表の件数（全部の表の合計）
    std::vector<std::string> errors; //!< 読み込みで弾いた件（変換指定の食い違いほか）
};

/*!
 * @brief 言語を決めて、日本語ならカタログを読む。**`an_bootstrap()` より前に 1 度だけ**呼ぶ。
 * @param lang `"ja"` 以外は英語（日本語層は寝たまま）
 * @param dir  `angband/lang/<lang>` の置き場
 * @param report_path 空でなければ、引けなかった Term の字をここへ追記する（`--report-untranslated=`）
 */
LangLoadReport lang_init(const std::string &lang, const std::string &dir, const std::string &report_path);

//! いまの言語（`"ja"` か `"en"`）。
const std::string &lang_code();

/*!
 * @brief アダプタ自身の字（制約 11）。`E:` を引く。無ければ `en` のまま。
 * @details 画面へ直に出る字（`Stash`・`Exit`・コマンドの名前など）はここを通す。
 */
std::string ui_text(const char *en);

/*!
 * @brief `--selftest` の `death causes` の段（AH-60。栄誉の殿堂と墓の死因）。
 * @details 死因は怪物のほかに固定の英語の句（`starvation` ほか）と罠の名（`a pit`）があり、書式の訳文の頭の
 * `{H}` が付いた 3 つの書式（墓の `by %s.`・殿堂の `Killed by %s in the town` / `... on dungeon level %d`）の引数だけ
 * `H:` の表と罠の名前の表で日本語へ引く。原作の書式の段（`strnfmt()`）をそのまま通し、英語では出力が変わらない
 * ことと、日本語で引けること・引けない句（`over-exertion` ほか）は英語のまま残ることを見る。殿堂の記録
 * （`how[32]`＝31 バイト）で切れた長い句（名前が 31 バイトを超える怪物を含む）も引く。
 * 訳文の日本語はここ（`an_lang*`。設計 §1 制約 11）に置く。
 * @return 全部通れば true。結果は stderr へ書く。
 */
bool selftest_death_causes();

/*!
 * @brief `--selftest` の `term translations` の段（修-40）。Term へ直に書かれる字（書式を通らないもの）の訳。
 * @details 鍵が末尾の空白を持つ字（`Hit any key to see more information: `）・書式の無い案内（`Press Return (or Escape).`）・
 * 店の窓の見出し行（`General Store (5000)`）・変身中の称号欄（`Fox`）が日本語で引けること、前後を落とした鍵で
 * 引けていた字（`Find: `）の訳が変わらないこと、`Home` のような語が店の見出し行の外では訳されないこと、日本語層が
 * 止まっていれば何も変わらないことを見る。
 * @return 全部通れば true。結果は stderr へ書く。
 */
bool selftest_term_translations();

} // namespace an

#endif /* AN_LANG_H */
