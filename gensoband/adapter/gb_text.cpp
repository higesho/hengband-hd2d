/*!
 * @file gb_text.cpp
 * @brief `gb_text.h` の実装。Windows の `MultiByteToWideChar` / `WideCharToMultiByte`。
 *
 * M0 は Windows 専用でよい（設計 §3）。他所へ移すときはここだけ差し替える。
 *
 * ## この TU は幻想蛮怒のヘッダを見ない
 * C++ の TU なので `angband.h` を include してはいけない（設計 §3）。
 * CP932 という数字だけを知っていればよい。
 */

#include "gb_text.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include "portable/cp932.h"
#endif

#include <cstdio>

namespace gb {

namespace {

/*!
 * @brief 0x80 以上の連なりが「まだ続きうる UTF-8 の途中」か。
 * @details 先頭バイトから必要な長さを読み、足りていなければ true。
 * これで**メッセージをまたいで割れた文字**を持ち越せる（設計 §3.1）。
 */
bool utf8_incomplete_tail(const std::string &run)
{
    if (run.empty()) {
        return false;
    }

    std::size_t i = 0;
    while (i < run.size()) {
        const auto lead = static_cast<unsigned char>(run[i]);
        std::size_t need = 0;
        if (lead < 0x80) {
            need = 1; // ここへは来ない（呼び手が 0x80 以上だけを溜める）
        } else if ((lead & 0xE0) == 0xC0) {
            need = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            need = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            need = 4;
        } else {
            return false; // 継続バイトか不正。**足りない**のではなく壊れている
        }

        if (i + need > run.size()) {
            return true; // 尻尾が欠けている
        }
        i += need;
    }
    return false;
}

} // namespace


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

//! CP932（Windows の Shift_JIS）。コアのビルド定義 `SJIS` に対応する数字。
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
     * U+FFFD に化けて「変換できた」ことになり、名前に □ が並ぶ。
     * 呼び手（`GbKeyDecoder`）は「空 = 捨てる」を選べる必要がある。
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
        /* CP932 に無い字（絵文字など）。**入れない**——コアの文字列に化けを持ち込まない。 */
        return std::string();
    }
    return out;
}

#endif /* _WIN32 */

/* --- ここから下は平台を問わない（変換の実体だけが上で分かれている）。 --- */

void GbKeyDecoder::reset()
{
    this->pending_.clear();
}

void GbKeyDecoder::flush_pending(std::vector<int> &out, bool final_flush)
{
    if (this->pending_.empty()) {
        return;
    }

    if (!final_flush && utf8_incomplete_tail(this->pending_)) {
        return; // 尻尾が欠けている。次の feed に繋ぐ
    }

    const std::string sjis = utf8_to_sjis(this->pending_);
    if (sjis.empty()) {
        this->dropped_ += static_cast<unsigned long>(this->pending_.size());
        std::fprintf(stderr, "[gensoband] dropped %u undecodable key byte(s)\n",
            static_cast<unsigned>(this->pending_.size()));
        std::fflush(stderr);
    } else {
        for (const char c : sjis) {
            out.push_back(static_cast<unsigned char>(c));
        }
    }
    this->pending_.clear();
}

void GbKeyDecoder::feed(const std::vector<int> &in, std::vector<int> &out)
{
    for (const int value : in) {
        const auto byte = static_cast<unsigned char>(value & 0xFF);
        if (byte == 0) {
            continue; // 0 は「キーではない」（v1 §4.1）
        }
        if (byte < 0x80) {
            this->flush_pending(out, true); // ASCII が来たら塊は閉じる
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
            this->flush_pending(out, true);
        }
    }

    /* 1 通ぶん読み切った。閉じ切れる塊だけ流し、欠けた尻尾は残す。 */
    this->flush_pending(out, false);
}


} // namespace gb
