/*!
 * @file android_iconv.cpp
 * @brief `iconv` の代替（UTF-8 ⇄ EUC-JP のみ）
 *
 * ## なぜ要るか
 * コアは日本語の内部表現を EUC-JP にして（`-DEUC`）、UTF-8 のデータファイルとの
 * 相互変換を `iconv` で行う（`src/locale/japanese.cpp:444` / `:474`）。
 *
 * **Android の bionic が持つ `iconv` は Unicode 系と Latin-1 しか扱えない。**
 * `iconv_open("EUC-JP", "UTF-8")` が失敗し、ゲームは起動直後に
 * 「警告:文字コードの変換に失敗しました」を出して `inkey()` で止まる
 * （エミュレータで実測。原因はネイティブスタックを取るまで分からなかった）。
 * NDK が公開する ICU のヘッダにも変換 API（`ucnv.h`）は含まれていないので、
 * **必要な 2 方向だけを自前で持つ**のがいちばん素直だった。
 *
 * ## どう差し込むか
 * `src/` は 1 行も書き換えない。CMake が `main` ターゲットにだけ
 * `-Diconv_open=hb_iconv_open` 等を渡し、**呼び先をここへ差し替える**。
 * `<iconv.h>` の宣言も同じマクロで書き換わるので、型は自動的に合う。
 *
 * ## 変換できない文字
 * 本物の `iconv` は `EILSEQ` で失敗するが、ここでは `?` に落として**成功を返す**。
 * 表示のための変換で 1 文字のために全体を失敗させると、呼び出し側
 * （`guess_convert_to_system_encoding`）が警告を出して入力待ちに入り、
 * **画面が止まる**。読めない 1 文字より止まらない方がよい。
 */
#include "android/android_iconv.h"

#include "android/eucjp_table.h"

#include <android/log.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>

namespace {

constexpr const char *kTag = "hengband";

enum class Encoding {
    Unsupported,
    Utf8,
    EucJp,
};

struct Conversion {
    Encoding from{ Encoding::Unsupported };
    Encoding to{ Encoding::Unsupported };
};

//! 名前の比較は大文字小文字と `-` / `_` を無視する（`EUC-JP` / `eucJP` / `euc_jp`）。
std::string normalize(const char *name)
{
    std::string out;
    for (const char *p = name; p != nullptr && *p != '\0'; ++p) {
        if (*p == '-' || *p == '_') {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
    // "//TRANSLIT" や "//IGNORE" の接尾辞は落とす。
    if (const auto pos = out.find("//"); pos != std::string::npos) {
        out.erase(pos);
    }
    return out;
}

Encoding encoding_from_name(const char *name)
{
    const auto n = normalize(name);
    if (n == "utf8") {
        return Encoding::Utf8;
    }
    if (n == "eucjp" || n == "eucjpms" || n == "ujis") {
        return Encoding::EucJp;
    }
    return Encoding::Unsupported;
}

// --------------------------------------------------------------------- 表引き

uint32_t ucs_from_euc(uint32_t packed)
{
    const auto *begin = std::begin(platform_android::kEucToUcs);
    const auto *end = std::end(platform_android::kEucToUcs);
    const auto *it = std::lower_bound(begin, end, packed,
        [](const platform_android::EucJpEntry &e, uint32_t value) { return e.euc < value; });
    if (it != end && it->euc == packed) {
        return it->ucs;
    }
    return 0;
}

uint32_t euc_from_ucs(uint32_t ucs)
{
    const auto *begin = std::begin(platform_android::kUcsToEuc);
    const auto *end = std::end(platform_android::kUcsToEuc);
    const auto *it = std::lower_bound(begin, end, ucs,
        [](const platform_android::EucJpEntry &e, uint32_t value) { return e.ucs < value; });
    if (it != end && it->ucs == ucs) {
        return it->euc;
    }
    return 0;
}

// ------------------------------------------------------------------- UTF-8 側

//! 1 文字読む。@return 消費したバイト数（0 = 不正なバイト列）
size_t utf8_decode(const unsigned char *p, size_t avail, uint32_t &out)
{
    if (avail == 0) {
        return 0;
    }
    const unsigned char c = p[0];
    if (c < 0x80) {
        out = c;
        return 1;
    }
    size_t len = 0;
    uint32_t value = 0;
    if ((c & 0xE0) == 0xC0) {
        len = 2;
        value = c & 0x1Fu;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
        value = c & 0x0Fu;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
        value = c & 0x07u;
    } else {
        return 0;
    }
    if (avail < len) {
        return 0;
    }
    for (size_t i = 1; i < len; ++i) {
        if ((p[i] & 0xC0) != 0x80) {
            return 0;
        }
        value = (value << 6) | (p[i] & 0x3Fu);
    }
    out = value;
    return len;
}

//! @return 書いたバイト数（0 = バッファ不足）
size_t utf8_encode(uint32_t ucs, unsigned char *out, size_t room)
{
    if (ucs < 0x80) {
        if (room < 1) {
            return 0;
        }
        out[0] = static_cast<unsigned char>(ucs);
        return 1;
    }
    if (ucs < 0x800) {
        if (room < 2) {
            return 0;
        }
        out[0] = static_cast<unsigned char>(0xC0 | (ucs >> 6));
        out[1] = static_cast<unsigned char>(0x80 | (ucs & 0x3F));
        return 2;
    }
    if (ucs < 0x10000) {
        if (room < 3) {
            return 0;
        }
        out[0] = static_cast<unsigned char>(0xE0 | (ucs >> 12));
        out[1] = static_cast<unsigned char>(0x80 | ((ucs >> 6) & 0x3F));
        out[2] = static_cast<unsigned char>(0x80 | (ucs & 0x3F));
        return 3;
    }
    if (room < 4) {
        return 0;
    }
    out[0] = static_cast<unsigned char>(0xF0 | (ucs >> 18));
    out[1] = static_cast<unsigned char>(0x80 | ((ucs >> 12) & 0x3F));
    out[2] = static_cast<unsigned char>(0x80 | ((ucs >> 6) & 0x3F));
    out[3] = static_cast<unsigned char>(0x80 | (ucs & 0x3F));
    return 4;
}

// ------------------------------------------------------------------ EUC-JP 側

//! 1 文字読む。@return 消費したバイト数（0 = 不正なバイト列）
size_t euc_decode(const unsigned char *p, size_t avail, uint32_t &out)
{
    if (avail == 0) {
        return 0;
    }
    const unsigned char c = p[0];
    if (c < 0x80) {
        out = c;
        return 1;
    }
    // 0x8F は 3 バイト（JIS X 0212）、それ以外の 0x8E / 0xA1..0xFE は 2 バイト。
    const size_t len = (c == 0x8F) ? 3 : 2;
    if (avail < len) {
        return 0;
    }
    uint32_t packed = 0;
    for (size_t i = 0; i < len; ++i) {
        packed = (packed << 8) | p[i];
    }
    const uint32_t ucs = ucs_from_euc(packed);
    if (ucs == 0) {
        return 0;
    }
    out = ucs;
    return len;
}

//! @return 書いたバイト数（0 = バッファ不足）。表に無い文字は `?` にする。
size_t euc_encode(uint32_t ucs, unsigned char *out, size_t room)
{
    if (ucs < 0x80) {
        if (room < 1) {
            return 0;
        }
        out[0] = static_cast<unsigned char>(ucs);
        return 1;
    }
    uint32_t packed = euc_from_ucs(ucs);
    if (packed == 0) {
        if (room < 1) {
            return 0;
        }
        out[0] = '?'; // 落とすより読めない 1 文字で通す（本ファイル冒頭の注記）
        return 1;
    }
    const size_t len = (packed > 0xFFFFu) ? 3 : 2;
    if (room < len) {
        return 0;
    }
    for (size_t i = 0; i < len; ++i) {
        out[i] = static_cast<unsigned char>((packed >> (8 * (len - 1 - i))) & 0xFFu);
    }
    return len;
}

} // namespace

extern "C" {

void *hb_iconv_open(const char *tocode, const char *fromcode)
{
    const Encoding to = encoding_from_name(tocode);
    const Encoding from = encoding_from_name(fromcode);
    if (to == Encoding::Unsupported || from == Encoding::Unsupported) {
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "iconv_open: unsupported conversion %s -> %s",
            (fromcode != nullptr) ? fromcode : "(null)",
            (tocode != nullptr) ? tocode : "(null)");
        errno = EINVAL;
        return reinterpret_cast<void *>(-1);
    }
    auto *cd = new Conversion{ from, to };
    return cd;
}

int hb_iconv_close(void *cd)
{
    if (cd == nullptr || cd == reinterpret_cast<void *>(-1)) {
        return 0;
    }
    delete static_cast<Conversion *>(cd);
    return 0;
}

size_t hb_iconv(void *cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft)
{
    if (cd == nullptr || cd == reinterpret_cast<void *>(-1)) {
        errno = EBADF;
        return static_cast<size_t>(-1);
    }
    const auto *conv = static_cast<const Conversion *>(cd);

    // 状態を持たない変換なので、リセット要求（inbuf == nullptr）は何もしなくてよい。
    if (inbuf == nullptr || *inbuf == nullptr) {
        return 0;
    }

    auto *in = reinterpret_cast<const unsigned char *>(*inbuf);
    auto *out = reinterpret_cast<unsigned char *>(*outbuf);
    size_t in_left = (inbytesleft != nullptr) ? *inbytesleft : 0;
    size_t out_left = (outbytesleft != nullptr) ? *outbytesleft : 0;
    size_t substituted = 0;

    while (in_left > 0) {
        uint32_t ucs = 0;
        const size_t consumed = (conv->from == Encoding::Utf8)
            ? utf8_decode(in, in_left, ucs)
            : euc_decode(in, in_left, ucs);
        if (consumed == 0) {
            // 途中で切れているのか、そもそも不正なのかを区別する。
            const bool incomplete = in_left < 4;
            errno = incomplete ? EINVAL : EILSEQ;
            break;
        }

        const size_t written = (conv->to == Encoding::Utf8)
            ? utf8_encode(ucs, out, out_left)
            : euc_encode(ucs, out, out_left);
        if (written == 0) {
            errno = E2BIG;
            break;
        }
        if (conv->to == Encoding::EucJp && written == 1 && ucs >= 0x80) {
            ++substituted; // `?` に落とした
        }

        in += consumed;
        in_left -= consumed;
        out += written;
        out_left -= written;
    }

    *inbuf = reinterpret_cast<char *>(const_cast<unsigned char *>(in));
    if (inbytesleft != nullptr) {
        *inbytesleft = in_left;
    }
    *outbuf = reinterpret_cast<char *>(out);
    if (outbytesleft != nullptr) {
        *outbytesleft = out_left;
    }

    if (in_left > 0) {
        return static_cast<size_t>(-1); // errno は上で設定済み
    }
    // 本物の iconv は「置き換えた文字数」を返す。呼び出し側は >= 0 しか見ていないが、
    // 仕様に合わせておく。
    return substituted;
}

} // extern "C"
