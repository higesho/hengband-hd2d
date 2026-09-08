#include <cstdlib>
/*!
 * @file legacy_os.cpp
 * @brief `legacy_os.h` の実装。**この 1 ファイルだけが平台を知っている。**
 */
#include "portable/legacy_os.h"

#include "portable/legacy_os_c.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#endif

#include <cstdio>
#include <cstring>

namespace portable {

namespace {

//! 末尾の区切り（`\` と `/` の両方）を落とした写しを返す。
std::string without_trailing_sep(const std::string &path)
{
    std::string out = path;
    while (!out.empty() && ((out.back() == '\\') || (out.back() == '/'))) {
        out.pop_back();
    }
    return out;
}

} // namespace

// =============================================================================
#if defined(_WIN32)
// =============================================================================

bool dir_exists(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    const std::string trimmed = without_trailing_sep(path);
    if (trimmed.empty()) {
        return false;
    }
    const DWORD attr = ::GetFileAttributesA(trimmed.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0u;
}

bool make_dir(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    if (dir_exists(path)) {
        return true;
    }
    return ::_mkdir(path.c_str()) == 0;
}

std::vector<std::string> list_files(const std::string &dir)
{
    std::vector<std::string> names;
    if (dir.empty()) {
        return names;
    }
    std::string pattern = dir;
    if ((pattern.back() != '\\') && (pattern.back() != '/')) {
        pattern.push_back('\\');
    }
    pattern += "*";

    WIN32_FIND_DATAA find{};
    const HANDLE handle = ::FindFirstFileA(pattern.c_str(), &find);
    if (handle == INVALID_HANDLE_VALUE) {
        return names;
    }
    do {
        if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
            continue;
        }
        if (find.cFileName[0] == '\0') {
            continue;
        }
        names.emplace_back(find.cFileName);
    } while (::FindNextFileA(handle, &find) != 0);
    ::FindClose(handle);
    return names;
}

void sleep_ms(int ms)
{
    ::Sleep(static_cast<DWORD>((ms > 0) ? ms : 0));
}

uint64_t tick_ms()
{
    return static_cast<uint64_t>(::GetTickCount64());
}

std::string exe_dir()
{
    char buf[4096];
    const DWORD n = ::GetModuleFileNameA(nullptr, buf, static_cast<DWORD>(sizeof(buf)));
    if ((n == 0) || (n >= sizeof(buf))) {
        return std::string();
    }
    std::string path(buf, buf + n);
    const std::string::size_type cut = path.find_last_of("\\/");
    if (cut == std::string::npos) {
        return std::string();
    }
    return path.substr(0, cut + 1);
}

// =============================================================================
#else /* !_WIN32 */
// =============================================================================

bool dir_exists(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    const std::string trimmed = without_trailing_sep(path);
    if (trimmed.empty()) {
        return false;
    }
    struct stat st {};
    if (::stat(trimmed.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

bool make_dir(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    if (dir_exists(path)) {
        return true;
    }
    /*
     * 権限は 0777（umask に削らせる）。アプリの内部ストレージの下なので
     * 他人からは元々見えない。`EEXIST` は「誰かが先に作った」＝成功と見る。
     */
    if (::mkdir(path.c_str(), 0777) == 0) {
        return true;
    }
    return (errno == EEXIST) && dir_exists(path);
}

std::vector<std::string> list_files(const std::string &dir)
{
    std::vector<std::string> names;
    if (dir.empty()) {
        return names;
    }
    const std::string trimmed = without_trailing_sep(dir);
    DIR *handle = ::opendir(trimmed.empty() ? "/" : trimmed.c_str());
    if (handle == nullptr) {
        return names;
    }
    while (const dirent *entry = ::readdir(handle)) {
        const char *name = entry->d_name;
        if ((std::strcmp(name, ".") == 0) || (std::strcmp(name, "..") == 0)) {
            continue;
        }
        /*
         * `d_type` は当てにしない（ファイルシステムによっては `DT_UNKNOWN` を返す）。
         * 迷ったら `stat` で確かめる——数が少ないのでコストは問題にならない。
         */
        if (entry->d_type == DT_DIR) {
            continue;
        }
        if (entry->d_type == DT_UNKNOWN) {
            struct stat st {};
            const std::string full = trimmed + "/" + name;
            if ((::stat(full.c_str(), &st) == 0) && S_ISDIR(st.st_mode)) {
                continue;
            }
        }
        names.emplace_back(name);
    }
    ::closedir(handle);
    return names;
}

void sleep_ms(int ms)
{
    if (ms <= 0) {
        return;
    }
    struct timespec req {};
    req.tv_sec = ms / 1000;
    req.tv_nsec = static_cast<long>(ms % 1000) * 1000000L;
    // 割り込まれたら残りを寝直す（`Sleep` は割り込まれない）。
    while (::nanosleep(&req, &req) != 0) {
        if (errno != EINTR) {
            break;
        }
    }
}

uint64_t tick_ms()
{
    struct timespec ts {};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (static_cast<uint64_t>(ts.tv_sec) * 1000u) + (static_cast<uint64_t>(ts.tv_nsec) / 1000000u);
}

std::string exe_dir()
{
    // ヘッダの註のとおり、こちらは **cwd** を返す（入口が展開先へ chdir 済み）。
    char buf[4096];
    if (::getcwd(buf, sizeof(buf)) == nullptr) {
        return std::string();
    }
    std::string path(buf);
    if (path.empty()) {
        return std::string();
    }
    if (path.back() != '/') {
        path.push_back('/');
    }
    return path;
}

#endif /* _WIN32 */

std::string iso_utc_now()
{
    unsigned year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0, milli = 0;
#if defined(_WIN32)
    SYSTEMTIME st{};
    ::GetSystemTime(&st);
    year = st.wYear;
    month = st.wMonth;
    day = st.wDay;
    hour = st.wHour;
    minute = st.wMinute;
    second = st.wSecond;
    milli = st.wMilliseconds;
#else
    struct timespec ts {};
    if (::clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        struct tm parts {};
        const time_t seconds = ts.tv_sec;
        if (::gmtime_r(&seconds, &parts) != nullptr) {
            year = static_cast<unsigned>(parts.tm_year) + 1900u;
            month = static_cast<unsigned>(parts.tm_mon) + 1u;
            day = static_cast<unsigned>(parts.tm_mday);
            hour = static_cast<unsigned>(parts.tm_hour);
            minute = static_cast<unsigned>(parts.tm_min);
            second = static_cast<unsigned>(parts.tm_sec);
        }
        milli = static_cast<unsigned>(ts.tv_nsec / 1000000L);
    }
#endif
    char buf[40]{};
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        year, month, day, hour, minute, second, milli);
    return std::string(buf);
}

std::string data_dir()
{
#if defined(_WIN32)
    wchar_t wide[32768]{};
    const auto count = GetEnvironmentVariableW(L"HENGBAND_DATA_ROOT", wide, 32768);
    if (count > 0 && count < 32768) {
        const auto bytes = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (bytes > 0) {
            std::string path(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_ACP, 0, wide, -1, path.data(), bytes, nullptr, nullptr);
            path.pop_back();
            return path + kPathSep;
        }
    }
#else
    if (const auto root = std::getenv("HENGBAND_DATA_ROOT"); root && *root) return std::string(root) + kPathSep;
#endif
    const auto base = exe_dir();
#if defined(_WIN32)
    const auto folder = without_trailing_sep(base);
    const auto cut = folder.find_last_of("\\/");
    if (cut != std::string::npos && folder.substr(cut + 1) == "cores") {
        return folder.substr(0, cut + 1);
    }
#endif
    return base;
}

} // namespace portable

// ----------------------------------------------------------------- C からの API
extern "C" int portable_dir_exists(const char *path)
{
    return (path != nullptr) && portable::dir_exists(path) ? 1 : 0;
}

extern "C" int portable_make_dir(const char *path)
{
    return (path != nullptr) && portable::make_dir(path) ? 1 : 0;
}

extern "C" int portable_list_files(const char *dir, char *out, int stride, int max)
{
    if ((dir == nullptr) || (out == nullptr) || (stride <= 1) || (max <= 0)) {
        return 0;
    }
    const std::vector<std::string> names = portable::list_files(dir);
    int count = 0;
    for (const auto &name : names) {
        if (count >= max) {
            break;
        }
        char *const slot = out + (static_cast<std::size_t>(count) * static_cast<std::size_t>(stride));
        const std::size_t room = static_cast<std::size_t>(stride) - 1u;
        const std::size_t take = (name.size() < room) ? name.size() : room;
        std::memcpy(slot, name.data(), take);
        slot[take] = '\0';
        ++count;
    }
    return count;
}
