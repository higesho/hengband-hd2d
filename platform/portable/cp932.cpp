/*!
 * @file cp932.cpp
 * @brief `cp932.h` の実装。表引き 1 段で CP932 ⇄ UTF-8 を往復する。
 *
 * 作りは `platform/android/android_iconv.cpp`（EUC-JP 側）と同じ形にしてある。
 * 違うのは表と、バイト列の刻み方だけ。
 */
#include "portable/cp932.h"

#include "portable/cp932_table.h"

#include <algorithm>

namespace portable {

namespace {

//! 翻せなかったバイトの落とし先。`WideCharToMultiByte` の既定と同じ。
constexpr char kFallback = '?';

//! CP932 の 2 バイト文字の先行バイトか。
bool is_lead(unsigned char c)
{
    return ((c >= 0x81) && (c <= 0x9F)) || ((c >= 0xE0) && (c <= 0xFC));
}

//! 同じく後続バイト。**0x5C（`\`）を含む**のが CP932 の厄介なところ。
bool is_trail(unsigned char c)
{
    return ((c >= 0x40) && (c <= 0x7E)) || ((c >= 0x80) && (c <= 0xFC));
}

/*! @brief 詰めた CP932 の値 → Unicode。無ければ 0。 */
uint32_t ucs_from_sjis(uint32_t packed)
{
    const auto *end = kSjisToUcs + std::size(kSjisToUcs);
    const auto *it = std::lower_bound(kSjisToUcs, end, packed,
        [](const Cp932Entry &e, uint32_t value) { return e.sjis < value; });
    return ((it != end) && (it->sjis == packed)) ? it->ucs : 0u;
}

/*! @brief Unicode → 詰めた CP932 の値。無ければ 0。 */
uint32_t sjis_from_ucs(uint32_t ucs)
{
    const auto *end = kUcsToSjis + std::size(kUcsToSjis);
    const auto *it = std::lower_bound(kUcsToSjis, end, ucs,
        [](const Cp932Entry &e, uint32_t value) { return e.ucs < value; });
    return ((it != end) && (it->ucs == ucs)) ? it->sjis : 0u;
}

/*!
 * @brief UTF-8 を 1 文字読む。
 * @param out 読めた符号位置
 * @return 進んだバイト数。**0 なら壊れている**（呼び手は諦める）。
 */
std::size_t utf8_decode(const unsigned char *p, std::size_t avail, uint32_t &out)
{
    if (avail == 0) {
        return 0;
    }
    const unsigned char c = p[0];
    if (c < 0x80) {
        out = c;
        return 1;
    }

    std::size_t len = 0;
    uint32_t ucs = 0;
    if ((c & 0xE0) == 0xC0) {
        len = 2;
        ucs = c & 0x1Fu;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
        ucs = c & 0x0Fu;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
        ucs = c & 0x07u;
    } else {
        return 0; // 継続バイトが単独で来た／5 バイト以上の古い形
    }
    if (avail < len) {
        return 0;
    }
    for (std::size_t i = 1; i < len; ++i) {
        if ((p[i] & 0xC0) != 0x80) {
            return 0;
        }
        ucs = (ucs << 6) | (p[i] & 0x3Fu);
    }
    /*
     * 冗長な符号（過長形式）は**壊れている扱い**にする。`MB_ERR_INVALID_CHARS` を
     * 付けた `MultiByteToWideChar` と同じ判断で、ここを緩めると化けが素通りする。
     */
    static constexpr uint32_t kLowest[] = { 0, 0, 0x80u, 0x800u, 0x10000u };
    if (ucs < kLowest[len]) {
        return 0;
    }
    out = ucs;
    return len;
}

//! 符号位置を UTF-8 で書き足す。
void utf8_encode(uint32_t ucs, std::string &out)
{
    if (ucs < 0x80u) {
        out.push_back(static_cast<char>(ucs));
    } else if (ucs < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (ucs >> 6)));
        out.push_back(static_cast<char>(0x80u | (ucs & 0x3Fu)));
    } else if (ucs < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (ucs >> 12)));
        out.push_back(static_cast<char>(0x80u | ((ucs >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (ucs & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (ucs >> 18)));
        out.push_back(static_cast<char>(0x80u | ((ucs >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((ucs >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (ucs & 0x3Fu)));
    }
}

} // namespace

std::string cp932_to_utf8(const char *bytes, std::size_t len)
{
    if ((bytes == nullptr) || (len == 0)) {
        return std::string();
    }

    const auto *p = reinterpret_cast<const unsigned char *>(bytes);
    std::string out;
    out.reserve(len + (len / 2)); // 2 バイト → 3 バイトが日本語の並みの伸び

    for (std::size_t i = 0; i < len;) {
        const unsigned char c = p[i];
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        if (is_lead(c) && ((i + 1) < len) && is_trail(p[i + 1])) {
            const uint32_t packed = (static_cast<uint32_t>(c) << 8) | p[i + 1];
            if (const uint32_t ucs = ucs_from_sjis(packed); ucs != 0u) {
                utf8_encode(ucs, out);
            } else {
                out.push_back(kFallback);
            }
            i += 2;
            continue;
        }
        // 半角カナ（1 バイト）と、対にならなかった孤立バイト。
        if (const uint32_t ucs = ucs_from_sjis(c); ucs != 0u) {
            utf8_encode(ucs, out);
        } else {
            out.push_back(kFallback);
        }
        ++i;
    }
    return out;
}

std::string utf8_to_cp932(const std::string &utf8)
{
    if (utf8.empty()) {
        return std::string();
    }

    const auto *p = reinterpret_cast<const unsigned char *>(utf8.data());
    const std::size_t len = utf8.size();
    std::string out;
    out.reserve(len);

    for (std::size_t i = 0; i < len;) {
        uint32_t ucs = 0;
        const std::size_t step = utf8_decode(p + i, len - i, ucs);
        if (step == 0) {
            return std::string(); // 壊れた UTF-8。まるごと捨てる
        }
        i += step;

        if (ucs < 0x80u) {
            out.push_back(static_cast<char>(ucs));
            continue;
        }
        const uint32_t packed = sjis_from_ucs(ucs);
        if (packed == 0u) {
            return std::string(); // CP932 に無い字。まるごと捨てる
        }
        if (packed > 0xFFu) {
            out.push_back(static_cast<char>((packed >> 8) & 0xFFu));
        }
        out.push_back(static_cast<char>(packed & 0xFFu));
    }
    return out;
}

} // namespace portable
