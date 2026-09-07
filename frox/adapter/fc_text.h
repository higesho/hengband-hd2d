/*!
 * @file fc_text.h
 * @brief 文字コードの変換所。**変換の家はここ 1 つ**。
 *
 * M1 から Frox コアの内部コードは **CP932（SJIS）**、プロトコル v1 の線の上は
 * **UTF-8** になった。境界はここだけを通る。
 *
 * | 向き | 使う関数 | どこで |
 * |---|---|---|
 * | コア → 線 | `fc::sjis_to_utf8` | Term ミラー・状態列・帯（`fc_frame.cpp` の 1 か所） |
 * | 訳の原本 → コア | `fc::utf8_to_sjis` | `fc_lang` がカタログを読み込むとき |
 * | 線 → コア（キー） | `FcKeyDecoder` | `fc_main.cpp` の `enqueue_keys()` 1 か所（A1） |
 *
 * ## これは `silq/adapter/sq_text.{h,cpp}` の**写し**である
 * `frox/adapter/` から他コアの `adapter/` を include するとツリーの分担
 * が崩れ、片方のコアを直すともう片方が組めなくなる。
 * **写しにする代わりに、1 本を直したら 3 本とも直す**（`gb_text` / `sq_text` / これ。
 * 設計 §2 の註）。いまの差は 2 つだけ:
 *
 * - 名前空間が `sq` ではなく `fc`
 * - stderr の印が `[silq]` ではなく `[frox]`
 *
 * ## この TU は Frox のヘッダを見ない
 * C++ の TU なので `angband.h` を include してはいけない（親契約 §3）。
 * CP932 という数字だけを知っていればよい。
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace fc {

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
 * @details 規則は写し元と同じ:
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
 * `fc_main.cpp` の `enqueue_keys()` **1 か所だけ**（設計 §3.1「変換の家は 1 つ」）。
 * **A1（日本語入力）が来るまでは呼ばれない**——J2 の時点では英語しか打たないので、
 * 通しても答えは 1 バイトも変わらない（制約 1）。
 */
class FcKeyDecoder {
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

} // namespace fc
