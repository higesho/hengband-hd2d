/*!
 * @file sq_lang.h
 * @brief 訳文カタログの**C++ 側の本体**。
 *
 * `silq/src` からはここを見ない（見えるのは `sq_lang_c.h` だけ）。
 * ここを呼ぶのは `sq_main.cpp` の起動列だけである。
 *
 * ## いつ読むか
 * **言語が決まってから、`sq_bootstrap()` より前**（設計 §3.3 の新しい起動列の 6）。
 * `lib-ja/edit` を読ませるには、`init_angband()` に入る前に言語が確定していなければならない。
 */
#pragma once

#include <string>

namespace sq {

/*!
 * @brief 読み込みの結果。**数を返す**——「読めた」ではなく「何件入ったか」を出すため。
 */
struct LangLoadReport {
    //! 日本語層が起きたか（`lang == "ja"`）。カタログが空でもこれは真になりうる。
    bool enabled{ false };
    //! ハッシュ表へ入った件数。
    int entries{ 0 };
    //! `J:` が空＝わざと訳さない印の件数（未訳一覧から外れる）。
    int silent{ 0 };
    //! 変換指定の並びが英日で食い違って**弾いた**件数（設計 §4）。
    int rejected{ 0 };
    //! CP932 に無い字があって弾いた件数。
    int not_in_cp932{ 0 };
    //! 読み込みそのものの失敗（鍵の重複・ファイルが無い）。空なら成功。
    std::string error;
};

/*!
 * @brief カタログを読む。
 * @param lang `"ja"` なら日本語層を起こす。それ以外は何もせず `enabled = false` で返る
 * @param lang_dir `silq/lang/ja` の絶対路（`<lib_dir>/../lang/ja`）
 * @param report_path `--report-untranslated=` の書き先。空なら記録しない
 *
 * @details **カタログが読めなくても `enabled` は下がらない**。
 * 名前は `lib-ja/edit` から来るので、カタログが空でも画面には日本語が出る。
 * ここで `enabled` を下げると `sq_is_text_byte()` が 0x80 以上を潰し、
 * その日本語が空白になってしまう（設計 §1 制約 4 と直結）。
 */
LangLoadReport lang_init(const std::string &lang, const std::string &lang_dir,
    const std::string &report_path);

/*!
 * @brief 引けなかった鍵を `--report-untranslated=` の先へ書き出す。
 * @return 書いた件数（記録していなければ 0）
 * @details 終了の直前に 1 回呼ぶ。**遊んで、出てきた英語を潰す**を回すための道具。
 */
int lang_write_report();

} // namespace sq
