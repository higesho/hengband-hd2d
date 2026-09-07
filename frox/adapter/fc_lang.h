/*!
 * @file fc_lang.h
 * @brief 訳文カタログの**C++ 側の本体**。
 *
 * `frox/src` からはここを見ない（見えるのは `fc_lang_c.h` だけ）。
 * ここを呼ぶのは `fc_main.cpp` の起動列だけである。
 *
 * ## いつ読むか
 * **言語が決まってから、`fc_bootstrap()` より前**（設計 §3.3。親契約 §3.6 の
 * 起動の順序の 6）。`lib-ja/edit` を読ませるには、`init_angband()` に入る前に
 * 言語が確定していなければならない。
 */
#pragma once

#include <string>

namespace fc {

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
    //! 変換指定の並びが英日で食い違って**弾いた**件数（設計 §5）。
    int rejected{ 0 };
    //! ドキュメントのタグの数か種類が英日で食い違って**弾いた**件数（設計 §3.4-1）。
    int tag_mismatch{ 0 };
    //! CP932 に無い字があって弾いた件数。
    int not_in_cp932{ 0 };
    //! 読み込みそのものの失敗（鍵の重複・ファイルが無い）。空なら成功。
    std::string error;
};

/*!
 * @brief カタログを読む。
 * @param lang `"ja"` なら日本語層を起こす。それ以外は何もせず `enabled = false` で返る
 * @param lang_dir `frox/lang/ja` の絶対パス（`<exe_dir>/frox/lang/ja`）
 * @param report_path `--report-untranslated=` の書き先。空なら記録しない
 *
 * @details **カタログが読めなくても `enabled` は下がらない**。
 * 名前は `lib-ja/edit` から来るので、カタログが空でも画面には日本語が出る。
 * ここで `enabled` を下げると `fc_is_text_byte()` が 0x80 以上を潰し、
 * その日本語が空白になってしまう（設計 §1 制約 4 と直結）。
 */
LangLoadReport lang_init(const std::string &lang, const std::string &lang_dir,
    const std::string &report_path);

/*!
 * @brief カタログの**本文を直に**渡す（`lang_init()` の中身）。
 * @param text `messages.ja.txt` の中身（UTF-8）
 * @details `lang_init()` はファイルを読んでこれを呼ぶだけである。分けてあるのは
 * **検査から呼ぶため**——`--selftest` が使い捨てのカタログを組んで、
 * 弾く／通すの判断（変換指定の並び・タグの数・CP932 に無い字）を機械で押さえる。
 */
LangLoadReport lang_load_text(const std::string &lang, const std::string &text,
    const std::string &report_path);

/*!
 * @brief 日本語層の検査（`--selftest` から呼ぶ。設計 §11 の 5）。
 * @return 落ちた件数（0 なら通過）
 *
 * @details 見るのは 3 つ:
 *
 * - **カタログの判断**（引ける・弾く・わざと訳さない・二重引きを避ける）
 * - **バイト算**（`fc_clip` が 2 バイト文字を割らない・`fc_is_text_byte` が
 *   英語のときだけ 0x80 以上を落とす）
 * - **ドキュメントの単語切り**（`fc_doc_word_bytes`。禁則の付き方）
 *
 * ここが要るのは、**J2 の時点では訳が 1 件も無い**からである。遊んで確かめる
 * ことができない代わりに、配管そのものを恒久的に押さえる（FH-15 の綴りの検査と同じ立場）。
 *
 * @warning **日本語層の入切を触る。** 検査の終わりに元へ戻すが、
 * `--selftest` の**いちばん最後**で呼ぶこと。
 */
int lang_selftest();

/*!
 * @brief 引けなかった鍵を `--report-untranslated=` の先へ書き出す。
 * @return 書いた件数（記録していなければ 0）
 * @details 終了の直前に 1 回呼ぶ。**遊んで、出てきた英語を潰す**を回すための道具
 *（設計 §8.2 の J4「出た順に埋める」）。
 */
int lang_write_report();

} // namespace fc
