/*!
 * @file sq_text.h
 * @brief 文字コードの変換所。**変換の家はここ 1 つ**。
 *
 * M1 から Sil-Q コアの内部コードは **CP932（SJIS）**、プロトコル v1 の線の上は
 * **UTF-8** になった。境界はここだけを通る。
 *
 * | 向き | 使う関数 | どこで |
 * |---|---|---|
 * | コア → 線 | `sq::sjis_to_utf8` | Term ミラー・状態列・帯（`sq_frame.cpp` の 3 か所） |
 * | 訳の原本 → コア | `sq::utf8_to_sjis` | `sq_lang` がカタログを読み込むとき |
 *
 * ## これは `gensoband/adapter/gb_text.{h,cpp}` の**写し**である
 * `silq/adapter/` から `gensoband/adapter/` を include すると木の分担
 * が崩れ、片方のコアを直すともう片方が組めなくなる。
 * **写しにする代わりに、片方を直したら両方直す**（設計 §2 の註）。
 * いまの差は次の 3 つだけ:
 *
 * - 名前空間が `gb` ではなく `sq`
 * - stderr の印が `[gensoband]` ではなく `[silq]`
 * - 名札の `[silq]` と名前空間 `sq` のほか、**差は無くなった**（2026-08-22）。
 *   キーの復号器（`SqKeyDecoder`）も持つ——**日本語入力を通したから**である
 *   （設計 §12.1 の A1-3）。それまでは「キーの側は変換しない」と書いてあった。
 *
 * ## この TU は Sil-Q のヘッダを見ない
 * C++ の TU なので `angband.h` を include してはいけない（親契約 §3）。
 * CP932 という数字だけを知っていればよい。
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace sq {

/*!
 * @brief CP932（SJIS）のバイト列を UTF-8 にする。
 * @details 変換できないバイトは `?` に落とす（`WC_*` の既定の置換）。**空を返さない**
 * ——ミラーの 1 行が丸ごと消えるより、化けた 1 文字が見えるほうが調べやすい。
 */
std::string sjis_to_utf8(const char *bytes, std::size_t len);
inline std::string sjis_to_utf8(const std::string &s) { return sjis_to_utf8(s.data(), s.size()); }

/*!
 * @brief UTF-8 を CP932（SJIS）にする。
 * @return 変換できなければ空文字列（呼び手が「捨てる」を選べるように）。
 * @details CP932 に無い字（波ダッシュ・丸数字・絵文字）が混じっていても**空を返す**。
 * 黙って `?` にしない——訳文の取り込みで踏むと、画面に出るまで気づけない。
 */
std::string utf8_to_sjis(const std::string &utf8);

/*! @brief CP932 の 2 バイト文字の先行バイトか（`h-config.h` の `iskanji` と同じ範囲）。 */
inline bool is_sjis_lead(unsigned char c)
{
    return ((c >= 0x81) && (c <= 0x9F)) || ((c >= 0xE0) && (c <= 0xFC));
}

/*!
 * @brief 線から来たキーのバイト列 → コアへ積むバイト列。**ステートフル**。
 *
 * @details `gensoband/adapter/gb_text.h` の `GbKeyDecoder` の写し（この 1 枚が写しである
 * 理由は頭の註記）。規則も同じ:
 *
 * - **0x80 以上の連なり**を 1 つの塊として扱う
 * - ASCII（0x01〜0x7F）が来たら塊を閉じて先に流す
 * - 変換できない塊は**捨てる**（stderr に記録する。捏造しない）
 *
 * ## ステートフルである理由
 * 塊が**メッセージをまたいで割れる**ことがある。1 通の中だけで閉じると、割れた UTF-8 の
 * 断片が両方とも「変換不能」で捨てられて 1 文字丸ごと消える。閉じ切れなかった尻尾は持ち越す。
 *
 * ## どこで使うか
 * `sq_main.cpp` の `enqueue_keys()` **1 か所だけ**（設計 §3.1「変換の家は 1 つ」）。
 */
class SqKeyDecoder {
public:
    /*!
     * @param in 受け取った値（1〜255。範囲外は呼び出し前に落ちている）
     * @param out 変換後を末尾へ足す
     */
    void feed(const std::vector<int> &in, std::vector<int> &out);

    /*! @brief 持ち越しを捨てる。 */
    void reset();

    /*! @brief 変換できずに捨てたバイト数（stderr へ出す用の目印）。 */
    unsigned long dropped() const { return this->dropped_; }

private:
    void flush_pending(std::vector<int> &out);

    //! 閉じ切っていない 0x80 以上の連なり。
    std::string pending_;
    unsigned long dropped_{ 0 };
};

} // namespace sq
