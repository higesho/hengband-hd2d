/*!
 * @file process_core.cpp
 * @brief 別のプロセスで動かすコア（NetHack）の、コアスレッドの入口の代理。説明は process_core.h。
 */
#include "android/process_core.h"

#include <android/log.h>

#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

extern char **environ;

namespace platform_android {
namespace {

constexpr const char *kTag = "HengbandHd2d";

//! 別のプロセスで動かすコアの名前。日本語版は "Jp" を含む（画面の言語の見分け。hd2d_app.cpp の language_core_variant）
constexpr const char *kEnglish = "libNetHackCore.so";
constexpr const char *kJapanese = "libNetHackJpCore.so";

std::mutex g_mutex;
std::filesystem::path g_native_dir; //!< 最初の解決のときに覚える（入口は引数で置き場を受け取れないため）

//! `transport_from_fd` が詰めた値（fd + 1）を fd に戻す。
int fd_of(void *handle)
{
    return (handle == nullptr) ? -1 : static_cast<int>(reinterpret_cast<intptr_t>(handle)) - 1;
}

/*!
 * @brief このプロセスの 3 番以上の fd に FD_CLOEXEC を立てる。
 * @details `CoreLink` のパイプは O_CLOEXEC でない。子が画面側の書き口まで受け継ぐと、画面が閉じても子の標準入力に EOF が来ず、
 * 子が保存して終わらない（の 1）。bionic の posix_spawn に「既定で閉じる」
 * （POSIX_SPAWN_CLOEXEC_DEFAULT）が入るのは API 34 からで、minSdk 33 では使えない。子へ渡す 2 本は posix_spawn の dup2 が
 * 0・1 へ写し、写した先には FD_CLOEXEC が付かない。親の fd に印が付くだけで、親の読み書きは変わらない。
 */
void mark_inherited_fds_cloexec()
{
    DIR *dir = ::opendir("/proc/self/fd");
    if (dir == nullptr) return;
    const int self = ::dirfd(dir);
    std::vector<int> fds;
    while (const dirent *entry = ::readdir(dir)) {
        char *end = nullptr;
        const long fd = std::strtol(entry->d_name, &end, 10);
        if ((end == entry->d_name) || (*end != '\0') || (fd < 3) || (fd == self)) continue;
        fds.push_back(static_cast<int>(fd));
    }
    ::closedir(dir);
    for (const int fd : fds) {
        const int flags = ::fcntl(fd, F_GETFD);
        if ((flags >= 0) && ((flags & FD_CLOEXEC) == 0)) (void)::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
}

int run_process_core(const char *name, void *in, void *out, const std::string &protocol_log_path,
    const std::vector<std::string> &forwarded_args)
{
    std::filesystem::path native_dir;
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        native_dir = g_native_dir;
    }
    const std::string executable = (native_dir / name).string();
    const int in_fd = fd_of(in);
    const int out_fd = fd_of(out);
    if ((in_fd < 0) || (out_fd < 0)) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "process core: no pipe ends for %s", name);
        return 1;
    }
    //! 引数は Linux の子プロセスの枝（hd2d/net/core_link.cpp）と同じ
    std::vector<std::string> args{ executable, "--ui-protocol=stdio" };
    if (!protocol_log_path.empty()) args.push_back("--protocol-log=" + protocol_log_path);
    args.insert(args.end(), forwarded_args.begin(), forwarded_args.end());
    std::vector<char *> argv;
    for (auto &arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in_fd, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out_fd, STDOUT_FILENO);
    mark_inherited_fds_cloexec();
    pid_t pid = -1;
    //! 今のフォルダ（アプリの files/）と環境はそのまま引き継ぐ。コアはここの nethack/data を読み、nethack/<言語> へ書く
    const int rc = ::posix_spawn(&pid, executable.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "process core: posix_spawn %s failed: %s", executable.c_str(), std::strerror(rc));
        std::fprintf(stderr, "[hd2d] could not start the core process %s: %s\n", executable.c_str(), std::strerror(rc));
        return 1;
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "process core: started %s (pid %d)", executable.c_str(), static_cast<int>(pid));
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "process core: waitpid %d failed: %s", static_cast<int>(pid), std::strerror(errno));
            return 1;
        }
    }
    if (WIFEXITED(status)) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "process core: %s exited (%d)", name, WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "process core: %s was killed by signal %d", name, WTERMSIG(status));
    }
    return 1;
}

int english_entry(void *in, void *out, const std::string &log, const std::vector<std::string> &args)
{
    return run_process_core(kEnglish, in, out, log, args);
}

int japanese_entry(void *in, void *out, const std::string &log, const std::vector<std::string> &args)
{
    return run_process_core(kJapanese, in, out, log, args);
}

} // namespace

bool is_process_core_name(const std::string &name)
{
    return (name == kEnglish) || (name == kJapanese);
}

bool process_core_available(const std::filesystem::path &native_dir, const std::string &name)
{
    if (!is_process_core_name(name) || native_dir.empty()) return false;
    return ::access((native_dir / name).c_str(), X_OK) == 0;
}

hd2d::AppOptions::CoreThreadEntry process_core_entry(const std::filesystem::path &native_dir, const std::string &name)
{
    if (!is_process_core_name(name)) return nullptr;
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        if (g_native_dir.empty()) g_native_dir = native_dir;
    }
    return (name == kJapanese) ? &japanese_entry : &english_entry;
}

} // namespace platform_android
