/*!
 * @file gb_lang.h
 * @brief 英訳カタログの **C++ 側**。
 *
 * `gensoband/src` からはここを見ない（見えるのは `gb_lang_c.h` だけ）。
 * ここを呼ぶのは `gb_main.cpp` の起動列だけである。
 *
 * ## いつ読むか
 * **言語が決まってから、`gb_bootstrap()` より前**（設計 §4.1）。
 * `gb_bootstrap()` が `gb_lang_enabled()` を見て `lib-en/edit` へ差し替えるので、
 * その前に言語が確定していなければならない。
 */
#pragma once

#include <string>

namespace gb {

/*! @brief 読み込みの結果。「読めた」ではなく「何件入ったか」を出す。 */
struct LangLoadReport {
    //! 英語層が起きたか（`lang == "en"`）。カタログが空でもこれは真になりうる。
    bool enabled{ false };
    //! ハッシュ表へ入った件数。
    int entries{ 0 };
    //! `E:` が空＝わざと訳さない印の件数（未訳一覧から外れる）。
    int silent{ 0 };
    //! 変換指定の並びが日英で食い違って**弾いた**件数。
    int rejected{ 0 };
    //! CP932 に無い字（鍵側）があって弾いた件数。
    int not_in_cp932{ 0 };
    //! `messages.en.txt` が `harvested.en.txt` を上書きした件数（手直しが勝つ）。
    int overridden{ 0 };
    //! 読み込みそのものの失敗（同一ファイル内の鍵の重複）。空なら成功。
    std::string error;
};

/*!
 * @brief カタログを読む。
 * @param lang `"en"` なら英語層を起こす。それ以外は何もせず `enabled = false` で返る
 * @param lang_dir `gensoband/lang/en` の絶対パス
 * @param lib_dir `gensoband/lib` の絶対パス（`gb_lang_file()` の実在検査に使う）
 * @param report_path `--report-untranslated=` の書き先。空なら記録しない
 *
 * @details 読む順は `ui/harvested.en.txt`（機械で抜いた対）→ `ui/messages.en.txt`
 * （手書き。**同じ鍵は手書きが勝つ**）。どちらも無くても `enabled` は下がらない
 * ——名前は `lib-en/edit` から来るので、カタログが空でも英語は出る。
 */
LangLoadReport lang_init(const std::string &lang, const std::string &lang_dir,
    const std::string &lib_dir, const std::string &report_path);

/*!
 * @brief 引けなかった鍵を `--report-untranslated=` の先へ書き出す。
 * @return 書いた件数（記録していなければ 0）
 * @details 終了の直前に 1 回呼ぶ。**遊んで、出てきた日本語を潰す**を回すための道具。
 * 仕分けは `tools/gensoband/gb_untranslated.py`（Sil-Q §8.2 の写し）。
 */
int lang_write_report();

} // namespace gb
