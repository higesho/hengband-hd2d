/*!
 * @file android_log_redirect.cpp
 * @brief `stdout` / `stderr` を logcat へ流す（実装）
 */
#include "android/android_log_redirect.h"

#include <SDL.h>

#include <android/log.h>
#include <pthread.h>
#include <unistd.h>

#include <cstdio>
#include <string>

namespace platform_android {

namespace {

constexpr const char *kTag = "hengband";

int g_pipe[2] = { -1, -1 };
pthread_t g_thread{};
bool g_started = false;

/*!
 * @brief logcat と**同じ行**を落とす控えのファイル。
 *
 * @details logcat は `adb` が要る。手元に PC が無い機体で不具合が出たとき、
 * それだけが頼りだと「見えているのに読めない」で詰まる（2026-08-21 に実際に詰まった）。
 * そこで**外部ストレージのアプリ専用領域**へ同じ行を落とす:
 *
 *     /sdcard/Android/data/org.hengband.hd2d/files/hd2d.log
 *
 * ここは端末のファイル管理アプリから見え、USB で PC へ写せる。権限も要らない
 * （アプリ専用領域なので `WRITE_EXTERNAL_STORAGE` の対象外）。
 *
 * 開くのは**最初の 1 行を書くとき**にする。`redirect_stdio_to_logcat()` は
 * SDL の初期化より前に呼ばれるので、置き場を訊くのはそれより後でなければならない。
 */
std::FILE *g_log_file = nullptr;
bool g_log_file_tried = false;

//! 控えのファイルを開く（1 度だけ試す）。開けなくても logcat は流れ続ける。
void open_log_file_once()
{
    if (g_log_file_tried) {
        return;
    }
    g_log_file_tried = true;
    const char *dir = SDL_AndroidGetExternalStoragePath();
    if ((dir == nullptr) || (dir[0] == '\0')) {
        return;
    }
    const std::string path = std::string(dir) + "/hd2d.log";
    //! 起動ごとに書き直す（前回の分が残っていると、どちらを見ているか分からなくなる）。
    g_log_file = std::fopen(path.c_str(), "wb");
    if (g_log_file == nullptr) {
        __android_log_print(ANDROID_LOG_WARN, kTag, "log file: cannot open %s", path.c_str());
        return;
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "log file: %s", path.c_str());
}

//! 1 行を logcat と控えの両方へ。
void emit(const std::string &line)
{
    __android_log_write(ANDROID_LOG_INFO, kTag, line.c_str());
    open_log_file_once();
    if (g_log_file != nullptr) {
        std::fwrite(line.data(), 1, line.size(), g_log_file);
        std::fputc('\n', g_log_file);
        std::fflush(g_log_file); //!< 落ちたときに最後の行を失わない
    }
}

//! パイプの読み側を行単位で logcat へ流す。
void *pump(void *)
{
    std::string line;
    char buf[512];
    for (;;) {
        const ssize_t n = ::read(g_pipe[0], buf, sizeof(buf));
        if (n <= 0) {
            break; // 書き側が閉じた／エラー
        }
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                if (!line.empty()) {
                    emit(line);
                    line.clear();
                }
            } else if (buf[i] != '\r') {
                line.push_back(buf[i]);
                // 改行の無い長い出力で無限に溜めない。
                if (line.size() >= 1024) {
                    emit(line);
                    line.clear();
                }
            }
        }
    }
    return nullptr;
}

} // namespace

void redirect_stdio_to_logcat()
{
    if (g_started) {
        return;
    }

    // 行バッファのままだと落ちたときに最後の数行が消える。無バッファにする。
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    if (::pipe(g_pipe) != 0) {
        __android_log_write(ANDROID_LOG_WARN, kTag, "stdio redirect: pipe() failed");
        return;
    }
    ::dup2(g_pipe[1], STDOUT_FILENO);
    ::dup2(g_pipe[1], STDERR_FILENO);

    if (::pthread_create(&g_thread, nullptr, pump, nullptr) != 0) {
        __android_log_write(ANDROID_LOG_WARN, kTag, "stdio redirect: pthread_create() failed");
        return;
    }
    ::pthread_detach(g_thread);
    g_started = true;
    __android_log_write(ANDROID_LOG_INFO, kTag, "stdout/stderr -> logcat");
}

} // namespace platform_android
