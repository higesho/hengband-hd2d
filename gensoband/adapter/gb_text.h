/*!
 * @file gb_text.h
 * @brief 文字コードの変換所（設計 §3.1）。**変換の家はここ 1 つ**。
 *
 * 幻想蛮怒コアの内部コードは **CP932（SJIS）**、プロトコル v1 の線の上は
 * **UTF-8**。境界はここだけを通る。
 *
 * | 向き | 使う関数 | どこで |
 * |---|---|---|
 * | コア → 線 | `sjis_to_utf8` | Term ミラー・メッセージ・HUD の文字列 |
 * | 線 → コア | `GbKeyDecoder` | `keys` / `input_event` のバイト列 |
 *
 * ## 線から来るバイト列は UTF-8 である（V5 の答え）
 * 画面側（`hd2d/app/hd2d_app.cpp:8820-8831`）は `SDL_TEXTINPUT` の UTF-8 を
 * **そのまま**運び、コア側アダプタ（`presentation/bridge/input_event_adapter.cpp:61-78`）も
 * 変換しない。変愚では `presentation/term/sdl_null_term.cpp` の
 * `convert_text_input_to_system_encoding`（:124-164）が積む直前に 1 回だけ直している。
 * **幻想蛮怒でもその 1 回をここで行う**（二重変換は名前が `????` になる。あちらの :66-71）。
 *
 * 規則もあちらと同じ:
 * - **0x80 以上の連なり**を 1 つの塊として扱う
 * - ASCII（0x01〜0x7F）が来たら塊を閉じて先に流す
 * - 変換できない塊は**捨てる**（stderr に記録する。捏造しない）
 *
 * ## ステートフルである理由（設計 §3.1）
 * 塊が**メッセージをまたいで割れる**ことがある。1 通の中だけで閉じると、
 * 割れた UTF-8 の断片が両方とも「変換不能」で捨てられて 1 文字丸ごと消える。
 * だから閉じ切れなかった尻尾は持ち越す。
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace gb {

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
 */
std::string utf8_to_sjis(const std::string &utf8);

/*! @brief CP932 の 2 バイト文字の先行バイトか（`h-config.h` の `iskanji` と同じ範囲）。 */
inline bool is_sjis_lead(unsigned char c)
{
    return ((c >= 0x81) && (c <= 0x9F)) || ((c >= 0xE0) && (c <= 0xFC));
}

/*!
 * @brief 線から来たキーバイト列 → コアへ積むバイト列。**ステートフル**。
 *
 * @details 使い方は「1 メッセージ = 1 回の `feed`」。塊が閉じなければ内部に残り、
 * 次の `feed` の頭に繋がる。`reset()` は繋がりを捨てる（`TERM_XTRA_FLUSH` 相当）。
 */
class GbKeyDecoder {
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
    void flush_pending(std::vector<int> &out, bool final_flush);

    //! 閉じ切っていない 0x80 以上の連なり。
    std::string pending_;
    unsigned long dropped_{ 0 };
};

} // namespace gb
