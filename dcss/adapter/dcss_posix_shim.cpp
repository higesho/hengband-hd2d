// DCSS コア: 原作 tileweb.cc が使う POSIX のソケットまわりの仮の実装。
//
// 原作のウェブタイル版（tileweb.cc）は、UNIX ドメインの SOCK_DGRAM ソケットで
//   - 送る: JSON を 1 メッセージ 1 行で sendto()（2048 バイトごとに分割）
//   - 受ける: ウェブサーバからの制御メッセージ（attach・key・click_cell…）を recvfrom()
// している。Windows にはこのソケットが無い。そこで同じ名前の関数を sys/socket.h の
// 互換ヘッダ（dcss/adapter/shim/sys/）で宣言し、ここで次の仮の実体を与える。
//
//   送り先: 環境変数 DCSS_JSON_OUT のファイル（無ければ標準出力）。メッセージはそのまま
//           書く（改行区切りの JSON。「*」で始まる行はサーバ宛ての制御メッセージ）。
//   受け口: 標準入力の 1 行 = 1 制御メッセージ（例 {"msg":"key","keycode":13}）。
//           読むのは専用のスレッド。ソケットの bind() の直後に、画面側が繋がった印の
//           {"msg":"attach","primary":true} をこちらで 1 本足す（無いとゲームは何も送らない）。
//           標準入力が閉じたら、登録されたコールバックを呼び、以後は Esc キーを返し続ける
//           （ゲームは保存して終わる）。
//
// 仮の実装である。正式な画面側のプロトコル v1（長さ付き枠）への置き換えは M1 以降。
//
// Linux（-DHENGBAND_BUILD_DCSS=ON）では本物の POSIX のヘッダ・型を使い、互換ヘッダ（shim/sys/）は使わない。
// 同じ関数を __wrap_socket などの名前で定義し、リンクの --wrap（dcss_core.cmake）で tileweb.cc などの呼び出しを
// ここへ向ける。仮のソケットの番号（/dev/null を開いた番号）以外の呼び出しは __real_ で本物へ素通しする。
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>

#include <errno.h>
#include <fcntl.h>

#ifdef _WIN32
#include <io.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WINSOCKAPI_
#include <windows.h>

#include "dcss_posix_shim.h"
#include "shim/sys/socket.h"
#include "shim/sys/time.h"

// Windows: 互換ヘッダが宣言した名前（socket・bind…）をそのまま定義する。
#define DCSS_API(name) name
#define DCSS_API_EXTERN_C_BEGIN
#define DCSS_API_EXTERN_C_END
typedef ptrdiff_t dcss_ssize_t;
#else
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "dcss_posix_shim.h"

// Linux: 本物の宣言を使い、--wrap で呼び出しをここへ向ける。素通しは __real_ 。
#define DCSS_API(name) __wrap_##name
#define DCSS_API_EXTERN_C_BEGIN extern "C" {
#define DCSS_API_EXTERN_C_END }
typedef ssize_t dcss_ssize_t;

extern "C" {
int __real_socket(int domain, int type, int protocol);
int __real_bind(int fd, const sockaddr *addr, socklen_t len);
int __real_setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen);
ssize_t __real_sendto(int fd, const void *buf, size_t len, int flags, const sockaddr *dest, socklen_t destlen);
ssize_t __real_recvfrom(int fd, void *buf, size_t len, int flags, sockaddr *src, socklen_t *srclen);
int __real_select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, timeval *timeout);
int __real_pselect(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, const timespec *timeout, const sigset_t *sigmask);
}
#endif

namespace
{
std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<std::string> g_lines;
bool g_eof = false;
bool g_eof_reported = false;
bool g_input_started = false;
void (*g_eof_cb)() = nullptr;
// プロトコル v1 の層（dcss_main.cpp / dcss_frame.cpp）が差し込むフック。無ければ従来どおり（仮の JSON の行）。
void (*g_sink)(const char *, size_t) = nullptr;
void (*g_idle_cb)() = nullptr;
void (*g_poll_cb)() = nullptr;
bool g_stdin_reader_disabled = false;
// 画面の部品（dcss-ui）の地図のマウス操作の受け口（dcss_ui_mouse.h）。無ければ `hd2d_` の行は原作へそのまま渡る（原作は知らない制御メッセージを捨てる）。
std::string (*g_ui_line_hook)(const std::string &, std::string &) = nullptr;
void (*g_ui_reply_sender)(const std::string &) = nullptr;

int g_sock_fd = -1;
bool g_bound = false;

std::mutex g_out_mutex;
FILE *g_out = nullptr;

// ---- 平台ごとの差（ここだけ分ける） ----

void set_binary_stdout()
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

//! 標準入力から読む。0 以下で終わり（EOF・エラー）。
int read_stdin(char *buf, int size)
{
#ifdef _WIN32
    return _read(0, buf, (unsigned)size);
#else
    for (;;)
    {
        const ssize_t n = ::read(0, buf, (size_t)size);
        if (n < 0 && errno == EINTR)
            continue;
        return (int)n;
    }
#endif
}

//! 仮のソケットの番号。close() が本物で通るよう、実在の（何も出さない・読めない）ファイルの番号を使う。
int open_dummy_fd()
{
#ifdef _WIN32
    return _open("NUL", _O_RDWR | _O_BINARY);
#else
    return ::open("/dev/null", O_RDWR);
#endif
}

void sleep_ms(long ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
    {
    }
#endif
}

void open_output()
{
    if (g_out)
        return;
    const char *path = std::getenv("DCSS_JSON_OUT");
    if (path && *path)
        g_out = std::fopen(path, "wb");
    if (!g_out)
    {
        set_binary_stdout();
        g_out = stdout;
    }
}

void reader_thread()
{
#ifdef _WIN32
    _setmode(0, _O_BINARY);
#endif
    std::string cur;
    char buf[4096];
    for (;;)
    {
        const int n = read_stdin(buf, (int)sizeof(buf));
        if (n <= 0)
            break;
        for (int i = 0; i < n; ++i)
        {
            if (buf[i] == '\n')
            {
                if (!cur.empty() && cur.back() == '\r')
                    cur.pop_back();
                if (!cur.empty())
                {
                    std::lock_guard<std::mutex> lk(g_mutex);
                    g_lines.push_back(cur);
                    g_cv.notify_all();
                }
                cur.clear();
            }
            else
                cur.push_back(buf[i]);
        }
    }
    std::lock_guard<std::mutex> lk(g_mutex);
    g_eof = true;
    g_cv.notify_all();
}

// 呼び出し側は g_mutex を持っていること。
bool ready_locked()
{
    return !g_lines.empty() || g_eof;
}
} // namespace

void dcss_shim_set_eof_callback(void (*cb)())
{
    g_eof_cb = cb;
}

void dcss_shim_start_input()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_input_started)
        return;
    g_input_started = true;
    if (g_stdin_reader_disabled)
        return; // プロトコル v1 の層が自前の受信スレッドで dcss_shim_feed_line / dcss_shim_signal_eof を呼ぶ
    std::thread(reader_thread).detach();
}

void dcss_shim_set_sink(void (*cb)(const char *, size_t))
{
    g_sink = cb;
}

void dcss_shim_set_idle_callbacks(void (*idle)(), void (*poll)())
{
    g_idle_cb = idle;
    g_poll_cb = poll;
}

void dcss_shim_game_delay(unsigned int ms)
{
    if (g_idle_cb) g_idle_cb();
    // 検査用: 環境変数 DCSS_DELAY_SCALE（整数。既定 1）で原作の待ちを何倍かに延ばす（飛んでいる途中の絵を撮るため。遊ぶときは使わない）。
    static const unsigned int scale = [] {
        const char *text = std::getenv("DCSS_DELAY_SCALE");
        const long value = (text != nullptr) ? std::strtol(text, nullptr, 10) : 1;
        return static_cast<unsigned int>(value >= 1 && value <= 100 ? value : 1);
    }();
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<unsigned long long>(ms) * scale));
}

void dcss_shim_set_ui_line_hook(std::string (*hook)(const std::string &line, std::string &reply), void (*reply_sender)(const std::string &reply))
{
    g_ui_line_hook = hook;
    g_ui_reply_sender = reply_sender;
}

void dcss_shim_disable_stdin_reader()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_stdin_reader_disabled = true;
}

void dcss_shim_signal_eof()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_eof = true;
    g_cv.notify_all();
}

void dcss_shim_feed_line(const char *line)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_lines.emplace_back(line);
    g_cv.notify_all();
}

bool dcss_shim_has_input()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    return ready_locked();
}

#ifdef _WIN32
// ---- シグナル（Windows では SIGWINCH は来ない。何もしない） ----

int sigemptyset(sigset_t *set)
{
    if (set)
        *set = 0;
    return 0;
}

int sigaddset(sigset_t *set, int signo)
{
    if (set && signo > 0 && signo < 32)
        *set |= (1ul << signo);
    return 0;
}

int sigprocmask(int, const sigset_t *, sigset_t *oldset)
{
    if (oldset)
        *oldset = 0;
    return 0;
}
#endif // _WIN32

// ---- 仮のソケット ----

DCSS_API_EXTERN_C_BEGIN

#ifdef _WIN32
#define DCSS_IS_FAKE(fd) true
#else
#define DCSS_IS_FAKE(fd) (g_sock_fd >= 0 && (fd) == g_sock_fd)
#endif

int DCSS_API(socket)(int domain, int type, int protocol)
{
#ifndef _WIN32
    // tileweb.cc の UNIX ドメインのデータグラムだけを仮にする。ほか（sqlite・lua などの呼び出し）は本物へ。
    if (domain != AF_UNIX || type != SOCK_DGRAM)
        return __real_socket(domain, type, protocol);
#else
    (void)domain;
    (void)type;
    (void)protocol;
#endif
    if (g_sock_fd >= 0)
        return g_sock_fd;
    g_sock_fd = open_dummy_fd();
    return g_sock_fd;
}

int DCSS_API(bind)(int fd, const sockaddr *addr, socklen_t len)
{
#ifndef _WIN32
    if (!DCSS_IS_FAKE(fd))
        return __real_bind(fd, addr, len);
#else
    (void)fd;
    (void)addr;
    (void)len;
#endif
    open_output();
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!g_bound)
        {
            g_bound = true;
            g_lines.push_front("{\"msg\":\"attach\",\"primary\":true}");
        }
    }
    dcss_shim_start_input();
    return 0;
}

int DCSS_API(setsockopt)(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
#ifndef _WIN32
    if (!DCSS_IS_FAKE(fd))
        return __real_setsockopt(fd, level, optname, optval, optlen);
#else
    (void)fd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
#endif
    return 0;
}

dcss_ssize_t DCSS_API(sendto)(int fd, const void *buf, size_t len, int flags, const sockaddr *dest, socklen_t destlen)
{
#ifndef _WIN32
    if (!DCSS_IS_FAKE(fd))
        return __real_sendto(fd, buf, len, flags, dest, destlen);
#else
    (void)fd;
    (void)flags;
    (void)dest;
    (void)destlen;
#endif
    std::lock_guard<std::mutex> lk(g_out_mutex);
    if (g_sink)
    {
        g_sink(static_cast<const char *>(buf), len);
        return (dcss_ssize_t)len;
    }
    open_output();
    const size_t w = std::fwrite(buf, 1, len, g_out);
    std::fflush(g_out);
    return (dcss_ssize_t)w;
}

dcss_ssize_t DCSS_API(recvfrom)(int fd, void *buf, size_t len, int flags, sockaddr *src, socklen_t *srclen)
{
#ifndef _WIN32
    if (!DCSS_IS_FAKE(fd))
        return __real_recvfrom(fd, buf, len, flags, src, srclen);
#else
    (void)fd;
    (void)flags;
#endif
    if (src && srclen && *srclen > 0)
        std::memset(src, 0, (size_t)*srclen);

    std::unique_lock<std::mutex> lk(g_mutex);
    g_cv.wait(lk, [] { return ready_locked(); });

    std::string line;
    if (!g_lines.empty())
    {
        line = g_lines.front();
        g_lines.pop_front();
    }
    else
    {
        // 標準入力が閉じた。1 度だけ通知し、以後は Esc を返し続ける。
        if (!g_eof_reported)
        {
            g_eof_reported = true;
            void (*cb)() = g_eof_cb;
            lk.unlock();
            if (cb)
                cb();
            lk.lock();
        }
        line = "{\"msg\":\"key\",\"keycode\":27}";
    }
    // 画面の部品（dcss-ui）が送る `{"msg":"hd2d_…"}`（足元のクリック・ツールチップの問い合わせ）はゲームのスレッドのここで処理し、
    // 命令になるものだけを原作へ返す（それ以外は何もしない制御メッセージに置き換える。原作は知らない `msg` を捨てて入力待ちを続ける）。
    if (g_ui_line_hook && line.compare(0, 13, "{\"msg\":\"hd2d_") == 0 && line != "{\"msg\":\"hd2d_refresh\"}")
    {
        lk.unlock();
        std::string reply;
        const std::string replaced = g_ui_line_hook(line, reply);
        if (!reply.empty() && g_ui_reply_sender)
            g_ui_reply_sender(reply);
        lk.lock();
        line = replaced.empty() ? std::string("{\"msg\":\"hd2d_refresh\"}") : replaced;
    }
    if (line.size() > len)
        line.resize(len);
    std::memcpy(buf, line.data(), line.size());
    return (dcss_ssize_t)line.size();
}

DCSS_API_EXTERN_C_END

//! 仮のソケットが見張りの対象に入っているか。
static bool watching_fake(int nfds, fd_set *rd)
{
    return rd && g_sock_fd >= 0 && g_sock_fd < nfds && FD_ISSET(g_sock_fd, rd);
}

static int wait_ready(int nfds, fd_set *rd, long timeout_ms)
{
    const bool watch = watching_fake(nfds, rd);
    if (rd)
        FD_ZERO(rd);
    if (!watch)
    {
        // 見張る物が無い（標準入力は原作では端末のキー入力。ここでは使わない）。
        if (timeout_ms > 0)
            sleep_ms(timeout_ms);
        return 0;
    }
    std::unique_lock<std::mutex> lk(g_mutex);
    // 入力が無いまま待ちに入る＝ゲームが入力待ち。プロトコル v1 の層に、いまの状態でフレームを出させる。
    // コールバックは g_mutex を持たずに呼ぶ（中で標準出力へ書くので長くなりうる）。
    if (!ready_locked())
    {
        void (*cb)() = (timeout_ms < 0) ? g_idle_cb : g_poll_cb;
        if (cb)
        {
            lk.unlock();
            cb();
            lk.lock();
        }
    }
    bool ok;
    if (timeout_ms < 0)
    {
        g_cv.wait(lk, [] { return ready_locked(); });
        ok = true;
    }
    else
        ok = g_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [] { return ready_locked(); });
    if (!ok)
        return 0;
    FD_SET(g_sock_fd, rd);
    return 1;
}

DCSS_API_EXTERN_C_BEGIN

int DCSS_API(select)(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, timeval *timeout)
{
#ifndef _WIN32
    if (!watching_fake(nfds, rd))
        return __real_select(nfds, rd, wr, ex, timeout);
#else
    (void)wr;
    (void)ex;
#endif
    long ms = -1;
    if (timeout)
        ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
    return wait_ready(nfds, rd, ms);
}

int DCSS_API(pselect)(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, const timespec *timeout, const sigset_t *sigmask)
{
#ifndef _WIN32
    if (!watching_fake(nfds, rd))
        return __real_pselect(nfds, rd, wr, ex, timeout, sigmask);
#else
    (void)wr;
    (void)ex;
    (void)sigmask;
#endif
    long ms = -1;
    if (timeout)
        ms = (long)(timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000);
    return wait_ready(nfds, rd, ms);
}

DCSS_API_EXTERN_C_END

#ifdef _WIN32
int gettimeofday(timeval *tv, void *)
{
    if (!tv)
        return 0;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    t -= 116444736000000000ull; // 1601 年 → 1970 年
    tv->tv_sec = (long)(t / 10000000ull);
    tv->tv_usec = (long)((t % 10000000ull) / 10);
    return 0;
}
#endif // _WIN32

#if defined(HENGBAND_ANDROID)
// Android（アプリの SELinux）では、実在する上の階層（/data・/data/data など）に対して、mkdir が EEXIST ではなく EACCES で、
// stat も EACCES で失敗する。原作の `_create_dirs`（files.cc）は、作りたい道の先頭から 1 段ずつ mkdir し、EEXIST なら `dir_exists`（stat）で
// ディレクトリか確かめ、1 度でも失敗すると諦めるので、アプリの保存先にまだ無い下の階層（saves/db・saves/des・morgue ほか）を作れない。
// アプリの保存先（環境変数 CRAWL_DIR）の祖先に当たる道だけ、「ディレクトリとして在る」と返す（Linux の mkdir・stat と同じ結果にする）。
// リンクの --wrap=mkdir・--wrap=stat（tools/core_sources/dcss_android_core.cmake）で原作の呼び出しをここへ向ける。
namespace {
//! 原作の `_create_dirs` は先頭が "//data" のように / が重なった道を渡すので、重なりをならして、末尾の / も外す。
std::string flatten_path(const char *path)
{
    std::string flat;
    for (const char *c = path; *c != 0; ++c) {
        if (!(*c == '/' && !flat.empty() && flat.back() == '/')) {
            flat.push_back(*c);
        }
    }
    while (flat.size() > 1 && flat.back() == '/') {
        flat.pop_back();
    }
    return flat;
}

//! `path` がアプリの保存先（CRAWL_DIR）の祖先（または同じ道）か。
bool is_crawl_dir_ancestor(const char *path)
{
    const char *crawl_dir = std::getenv("CRAWL_DIR");
    if (crawl_dir == nullptr || path == nullptr) {
        return false;
    }
    const std::string flat = flatten_path(path);
    const std::size_t n = flat.size();
    return (n > 0) && (std::strncmp(crawl_dir, flat.c_str(), n) == 0) && ((n == 1) || (crawl_dir[n] == '/') || (crawl_dir[n] == 0));
}
} // namespace

extern "C" {
int __real_mkdir(const char *path, mode_t mode);
int __real_stat(const char *path, struct stat *buf);

int __wrap_mkdir(const char *path, mode_t mode)
{
    const int rc = __real_mkdir(path, mode);
    if (rc != 0 && errno == EACCES && is_crawl_dir_ancestor(path)) {
        errno = EEXIST;
    }
    if (rc != 0 && errno != EEXIST) {
        static int reported = 0; // 作れなかった理由を最初の数回だけ残す（Android で原因を切り分けるため）
        if (reported++ < 5) {
            std::fprintf(stderr, "[dcss] mkdir(%s) failed: errno=%d\n", path, errno);
        }
    }
    return rc;
}

DIR *__real_opendir(const char *path);

//! 原作の `dir_exists` は（HAVE_STAT が無いとき = -U__ANDROID__ の組み立て）opendir で調べる。祖先の道は、保存先を開いた物で代える（呼ぶ側はすぐ閉じる）。
DIR *__wrap_opendir(const char *path)
{
    DIR *d = __real_opendir(path);
    if (d == nullptr && errno == EACCES && is_crawl_dir_ancestor(path)) {
        d = __real_opendir(std::getenv("CRAWL_DIR"));
    }
    return d;
}

int __wrap_stat(const char *path, struct stat *buf)
{
    const int rc = __real_stat(path, buf);
    if (rc != 0 && errno == EACCES && is_crawl_dir_ancestor(path)) {
        std::memset(buf, 0, sizeof(*buf));
        buf->st_mode = S_IFDIR | 0755;
        return 0;
    }
    return rc;
}
}
#endif // HENGBAND_ANDROID
