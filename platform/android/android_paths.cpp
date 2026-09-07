/*!
 * @file android_paths.cpp
 * @brief Android のディレクトリ解決（実装）
 */
#include "android/android_paths.h"

#include <SDL.h>

#include <android/log.h>
#include <unistd.h>

#include <system_error>

namespace platform_android {

namespace {

constexpr const char *kTag = "hengband";

std::filesystem::path g_base_dir;

} // namespace

std::filesystem::path base_dir()
{
    if (!g_base_dir.empty()) {
        return g_base_dir;
    }
    // SDL が JNI 越しに Context#getFilesDir() を引いてくる。
    // 失敗する状況（Activity 未生成など）はここには来ない（SDL_main は Activity から呼ばれる）。
    const char *path = SDL_AndroidGetInternalStoragePath();
    if (path == nullptr || path[0] == '\0') {
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "SDL_AndroidGetInternalStoragePath failed: %s", SDL_GetError());
        return {};
    }
    g_base_dir = std::filesystem::path(path);
    return g_base_dir;
}

std::filesystem::path lib_dir()
{
    return base_dir() / "lib";
}

bool prepare_and_enter_base_dir()
{
    const auto base = base_dir();
    if (base.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    if (ec) {
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "create_directories(%s) failed: %s", base.string().c_str(), ec.message().c_str());
        return false;
    }

    if (::chdir(base.string().c_str()) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "chdir(%s) failed", base.string().c_str());
        return false;
    }

    __android_log_print(ANDROID_LOG_INFO, kTag, "base dir = %s", base.string().c_str());
    return true;
}

} // namespace platform_android
