/*!
 * @file lang.h
 * @brief 多言語の入口（画面側）。**文言を実行時に引く。**
 *
 * ## なぜ画面側から始めるか
 * コアの `_("日","英")` はコンパイル時に片方へ潰れる（`src/locale/language-switcher.h`）。
 * そのうえ内部の文字コードが `JP` と結びついている（SJIS / EUC）ので、コアを実行時に
 * 切り替えるには**内部 UTF-8 化が先**になる。
 *
 * 対して `hd2d/` は **`/execution-charset:utf-8` で組まれ、`JP` も `#ifdef JP` も無い**。
 * つまり画面側だけは文字コードの工事なしで実行時切替ができる。ここを先に通して、
 * カタログの形・ID の付け方・フォールバックの規則を確かめる。コアは同じ器へ後から載せる。
 *
 * ## 引き方
 * ```cpp
 * text.draw(x, y, hd2d::i18n::tr("hd2d.ui.feature-menu.camera"));
 * ```
 * `tr()` は**カタログの中を指す `const char *`** を返す。カタログは一度読んだら
 * 解放しないので、返した先は実行中ずっと生きている（言語を切り替えても、
 * 前の言語のカタログはそのまま残る）。`const char *` を持ち回っても落ちない。
 *
 * ## 探す順（フォールバック）
 * いまの言語 → 英語 → **ID そのもの**。
 * 訳が抜けている所は英語で出て、英語も無ければ ID が出る。**黙って空にはしない**
 * （空文字だと「文言が無い」のか「訳が無い」のか画面から判らない）。
 *
 * ## カタログ
 * `assets/lang/<コード>.json`。中身は ID と文言の対だけの平らな JSON。
 *
 * ```json
 * { "hd2d.ui.feature-menu.camera": "Camera" }
 * ```
 *
 * **並ぶのは実際に読めた言語だけ**（`available()`）。`all_languages()` に名前があっても
 * ファイルが無ければ選べない。訳の無い言語を選べてしまうと、画面が ID だらけになる。
 *
 * ## 字が出るかどうかは別の話
 * カタログがあっても、**フォントにその字が無ければ豆腐になる**。いまの既定は
 * `msgothic.ttc`（`render/text_overlay.cpp`）で、ラテン・キリル・かな・漢字は持つが
 * **ハングルは持たない**。韓国語を足すときはフォントの手当てが要る。
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace hd2d::i18n {

//! 言語 1 つ。`endonym` は**その言語自身での呼び名**（選ぶ人が読めるように）。
struct LanguageInfo {
    const char *code; //!< BCP 47 風のコード。カタログのファイル名になる
    const char *endonym; //!< 自称（"日本語" / "English" / "Русский"）
};

/*!
 * @brief 対応を予定している言語の全部（フルセット 14）。
 * @details 並びは画面に出す順。**ここに在ることと選べることは別**で、
 * 選べるのは `assets/lang/<code>.json` が読めたものだけ（`available()`）。
 */
const std::vector<LanguageInfo> &all_languages();

/*!
 * @brief カタログを読み込む。起動時に 1 回だけ呼ぶ。
 * @param lang_dir `assets/lang` の場所
 * @details 見つかったカタログを全部読んで持つ。**読めなかったものは黙って落とす**
 * （その言語が `available()` に並ばなくなるだけで、起動は止めない）。
 */
void load(const std::string &lang_dir);

//! 実際に読めた言語（`load()` の後で確定する）。英語が読めなければ空になりうる。
const std::vector<LanguageInfo> &available();

/*!
 * @brief 言語を切り替える。
 * @return 切り替えられたら true。カタログが無いコードなら false（いまの言語のまま）
 */
bool set_language(std::string_view code);

//! いま選ばれている言語のコード。
std::string_view current();

//! いま選ばれている言語の自称（メニューに出す用）。
const char *current_endonym();

/*!
 * @brief ID を引く。
 * @return いまの言語 → 英語 → ID そのもの、の順で見つかったもの
 * @details 返す先はカタログの中で、**実行中ずっと生きている**。
 */
const char *tr(std::string_view id);

} // namespace hd2d::i18n
