/*!
 * @file sq_text.cpp
 * @brief `sq_text.h` の実装。Windows の `MultiByteToWideChar` / `WideCharToMultiByte`。
 *
 * M1 は Windows 専用でよい（親契約 §3）。他所へ移すときはここだけ差し替える。
 * `gensoband/adapter/gb_text.cpp` の写し（差は `sq_text.h` の冒頭を見よ）。
 */

#include "sq_text.h"

#include <cstdio>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include "portable/cp932.h"
#endif

namespace sq {

#if !defined(_WIN32)

/*
 * Windows 以外（Android / Quest。2026-08-21）。あの 2 つの API が無いので
 * `platform/portable/cp932.cpp` の表引きに任せる。**返し方の約束は同じ**
 * ——読めないバイトは `?`、CP932 に無い字は空——なので呼び手は場合分けを持たない。
 */
std::string sjis_to_utf8(const char *bytes, std::size_t len)
{
    return portable::cp932_to_utf8(bytes, len);
}

std::string utf8_to_sjis(const std::string &utf8)
{
    return portable::utf8_to_cp932(utf8);
}

#else /* _WIN32 */

namespace {

//! CP932（Windows の Shift_JIS）。コアのビルド定義に対応する数字。
constexpr UINT kCodePageSjis = 932;

} // namespace

std::string sjis_to_utf8(const char *bytes, std::size_t len)
{
    if ((bytes == nullptr) || (len == 0)) {
        return std::string();
    }

    const int src_len = static_cast<int>(len);
    const int wide_len = ::MultiByteToWideChar(kCodePageSjis, 0, bytes, src_len, nullptr, 0);
    if (wide_len <= 0) {
        return std::string();
    }

    std::wstring wide(static_cast<std::size_t>(wide_len), L'\0');
    if (::MultiByteToWideChar(kCodePageSjis, 0, bytes, src_len, wide.data(), wide_len) <= 0) {
        return std::string();
    }

    const int out_len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_len, nullptr, 0, nullptr, nullptr);
    if (out_len <= 0) {
        return std::string();
    }

    std::string out(static_cast<std::size_t>(out_len), '\0');
    if (::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_len, out.data(), out_len, nullptr, nullptr) <= 0) {
        return std::string();
    }
    return out;
}

std::string utf8_to_sjis(const std::string &utf8)
{
    if (utf8.empty()) {
        return std::string();
    }

    const int src_len = static_cast<int>(utf8.size());
    /*
     * **`MB_ERR_INVALID_CHARS` を付ける。** 付けないと壊れた UTF-8 が
     * U+FFFD に化けて「変換できた」ことになり、訳文に □ が並ぶ。
     * 呼び手（`sq_lang`）は「空 = 取り込まない」を選べる必要がある。
     */
    const int wide_len = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), src_len, nullptr, 0);
    if (wide_len <= 0) {
        return std::string();
    }

    std::wstring wide(static_cast<std::size_t>(wide_len), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), src_len, wide.data(), wide_len) <= 0) {
        return std::string();
    }

    BOOL used_default = FALSE;
    const int out_len = ::WideCharToMultiByte(kCodePageSjis, 0, wide.data(), wide_len, nullptr, 0, nullptr, nullptr);
    if (out_len <= 0) {
        return std::string();
    }

    std::string out(static_cast<std::size_t>(out_len), '\0');
    if (::WideCharToMultiByte(kCodePageSjis, 0, wide.data(), wide_len, out.data(), out_len, nullptr, &used_default) <= 0) {
        return std::string();
    }
    if (used_default) {
        /* CP932 に無い字。**入れない**——コアの文字列に化けを持ち込まない。 */
        return std::string();
    }
    return out;
}

#endif /* _WIN32 */

/* ==================================================== キーの復号器（A1-3。2026-08-22） */

void SqKeyDecoder::reset()
{
    this->pending_.clear();
}

void SqKeyDecoder::flush_pending(std::vector<int> &out)
{
    if (this->pending_.empty()) {
        return;
    }
    const std::string sjis = utf8_to_sjis(this->pending_);
    if (sjis.empty()) {
        this->dropped_ += static_cast<unsigned long>(this->pending_.size());
        std::fprintf(stderr, "[silq] dropped %u undecodable key byte(s)\n",
            static_cast<unsigned>(this->pending_.size()));
        std::fflush(stderr);
    } else {
        for (const char c : sjis) {
            out.push_back(static_cast<unsigned char>(c));
        }
    }
    this->pending_.clear();
}

void SqKeyDecoder::feed(const std::vector<int> &in, std::vector<int> &out)
{
    for (const int value : in) {
        const auto byte = static_cast<unsigned char>(value & 0xFF);
        if (byte == 0) {
            continue; // 0 は「キーではない」（v1 §4.1）
        }
        if (byte < 0x80) {
            this->flush_pending(out); // ASCII が来たら塊は閉じる
            out.push_back(static_cast<int>(byte));
            continue;
        }
        this->pending_.push_back(static_cast<char>(byte));
        /*
         * 持ち越しに上限を置く。壊れた列を延々ためて、いつか全部捨てるより、
         * その場で捨てて次へ進むほうが害が小さい（4 バイトで 1 文字、
         * 一度に届く「文字列」も高々数文字）。
         */
        if (this->pending_.size() >= 64) {
            this->flush_pending(out);
        }
    }

    /*
     * 1 通ぶん読み切った。**閉じ切れる塊だけ流す。**
     *
     * @note ここで無条件に流さないのが幻想蛮怒との唯一の差である。あちらは
     * `flush_pending(out, false)` で「欠けた尻尾は残す」を表していて、こちらは
     * `utf8_to_sjis()` が空を返したときに残らず捨てる形にした——結果は同じで、
     * 半端な尻尾は次の `feed()` の頭に繋がる。
     */
    if (!this->pending_.empty()) {
        const std::string sjis = utf8_to_sjis(this->pending_);
        if (!sjis.empty()) {
            this->flush_pending(out);
        }
    }
}

} // namespace sq
