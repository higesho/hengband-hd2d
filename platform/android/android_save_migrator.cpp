/*!
 * @file android_save_migrator.cpp
 * @brief 旧 EUC 版が書いたセーブのファイル名を UTF-8 へ改名する（実装）
 */
#include "android/android_save_migrator.h"

#include "android/android_iconv.h"
#include "android/android_paths.h"

#include <android/log.h>

#include <filesystem>
#include <string>
#include <vector>

namespace platform_android {

namespace {

constexpr const char *kTag = "HengbandSaveMigrate";

//! UTF-8 として正しい並びか（正しければ改名は要らない）。
bool is_valid_utf8(const std::string &s)
{
    const auto *p = reinterpret_cast<const unsigned char *>(s.data());
    const auto *end = p + s.size();
    while (p < end) {
        if (*p < 0x80) {
            p++;
            continue;
        }

        int len = 0;
        if ((*p & 0xe0) == 0xc0) {
            len = 2;
        } else if ((*p & 0xf0) == 0xe0) {
            len = 3;
        } else if ((*p & 0xf8) == 0xf0) {
            len = 4;
        } else {
            return false;
        }

        if (end - p < len) {
            return false;
        }

        for (int i = 1; i < len; i++) {
            if ((p[i] & 0xc0) != 0x80) {
                return false;
            }
        }

        p += len;
    }

    return true;
}

/*!
 * @brief EUC-JP のバイト列として読めるか。読めたら UTF-8 に直して返す。
 * @details 変換には自前の iconv（`hb_iconv`。EUC-JP ⇄ UTF-8 だけの実装）を使う。
 * 全バイトを消費できなかったら「EUC ではなかった」として諦める。
 */
bool euc_name_to_utf8(const std::string &euc, std::string &out_utf8)
{
    void *cd = hb_iconv_open("UTF-8", "EUC-JP");
    if (cd == nullptr || cd == reinterpret_cast<void *>(-1)) {
        return false;
    }

    std::vector<char> buf(euc.size() * 2 + 4, '\0');
    const char *in = euc.data();
    char *in_mut = const_cast<char *>(in);
    char *out = buf.data();
    size_t in_left = euc.size();
    size_t out_left = buf.size();
    const auto rc = hb_iconv(cd, &in_mut, &in_left, &out, &out_left);
    hb_iconv_close(cd);
    if (rc == static_cast<size_t>(-1) || in_left != 0) {
        return false;
    }

    out_utf8.assign(buf.data(), buf.size() - out_left);
    return !out_utf8.empty();
}

//! 1 つのディレクトリの中の、UTF-8 でない名前を改名する。
int migrate_dir(const std::filesystem::path &dir)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return 0;
    }

    int renamed = 0;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        const auto name = entry.path().filename().string();
        if (is_valid_utf8(name)) {
            continue;
        }

        std::string utf8_name;
        if (!euc_name_to_utf8(name, utf8_name) || !is_valid_utf8(utf8_name)) {
            __android_log_print(ANDROID_LOG_WARN, kTag,
                "cannot convert filename (leaving as-is): %s", entry.path().c_str());
            continue;
        }

        const auto to = dir / utf8_name;
        std::error_code exists_ec;
        if (std::filesystem::exists(to, exists_ec)) {
            //! 同名があるなら触らない（上書きで消すよりは□のままがよい）。
            __android_log_print(ANDROID_LOG_WARN, kTag,
                "target already exists (leaving as-is): %s", utf8_name.c_str());
            continue;
        }

        std::error_code rename_ec;
        std::filesystem::rename(entry.path(), to, rename_ec);
        if (rename_ec) {
            __android_log_print(ANDROID_LOG_WARN, kTag,
                "rename failed (%s): %s", rename_ec.message().c_str(), utf8_name.c_str());
            continue;
        }

        __android_log_print(ANDROID_LOG_INFO, kTag, "renamed to UTF-8: %s", utf8_name.c_str());
        renamed++;
    }

    return renamed;
}

} // namespace

int migrate_legacy_save_names()
{
    const auto lib = lib_dir();
    auto renamed = migrate_dir(lib / "save");
    renamed += migrate_dir(lib / "user");
    if (renamed > 0) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "migrated %d legacy filenames", renamed);
    }

    return renamed;
}

} // namespace platform_android
