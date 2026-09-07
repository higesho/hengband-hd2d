/*!
 * @file protocol_transport.cpp
 * @brief `protocol_transport.h` の実装（v1 §2.1 のフレーミング）。Win32 と POSIX の 2 面。
 *
 * 依存は標準ライブラリと OS API だけ（中立性は protocol_transport.h 冒頭のとおり）。
 */
#include "frame/protocol_transport.h"

#include <cstdio>

namespace presentation {

const char *transport_result_name(TransportResult result)
{
    switch (result) {
    case TransportResult::Ok:
        return "Ok";
    case TransportResult::Eof:
        return "Eof";
    case TransportResult::IoError:
        return "IoError";
    case TransportResult::TooLarge:
        return "TooLarge";
    case TransportResult::Truncated:
        return "Truncated";
    }
    return "Unknown";
}

} // namespace presentation

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <string>
#include <vector>

namespace presentation {

namespace {

//! `GetLastError()` を添えたエラー文字列。
std::string win_err(const char *what)
{
    char buf[128]{};
    std::snprintf(buf, sizeof(buf), "%s (GetLastError=%lu)", what, static_cast<unsigned long>(::GetLastError()));
    return std::string(buf);
}

/*!
 * @brief `want` バイト読み切る。
 * @return 読み切れたら Ok、1 バイトも読めずに終端なら Eof、途中で終端なら Truncated。
 * @details `ReadFile` は無名パイプでも要求量より少なく返しうるので必ずループする。
 * 相手が閉じたパイプの読み取りは「0 バイト成功」または `ERROR_BROKEN_PIPE` の
 * どちらでも来るので、両方を EOF として扱う。
 */
TransportResult read_exact(HANDLE handle, void *dest, size_t want, std::string &err)
{
    auto *out = static_cast<unsigned char *>(dest);
    size_t done = 0;
    while (done < want) {
        const DWORD chunk = static_cast<DWORD>((want - done > 0x10000000u) ? 0x10000000u : (want - done));
        DWORD got = 0;
        if (::ReadFile(handle, out + done, chunk, &got, nullptr) == FALSE) {
            const DWORD code = ::GetLastError();
            if ((code == ERROR_BROKEN_PIPE) || (code == ERROR_HANDLE_EOF)) {
                if (done == 0) {
                    return TransportResult::Eof;
                }
                err = "peer closed in the middle of a message";
                return TransportResult::Truncated;
            }
            err = win_err("ReadFile failed");
            return TransportResult::IoError;
        }
        if (got == 0) {
            if (done == 0) {
                return TransportResult::Eof;
            }
            err = "unexpected EOF in the middle of a message";
            return TransportResult::Truncated;
        }
        done += got;
    }
    return TransportResult::Ok;
}

//! `want` バイト書き切る。`WriteFile` も部分書き込みを返しうるのでループする。
bool write_exact(HANDLE handle, const void *src, size_t want, std::string &err)
{
    const auto *in = static_cast<const unsigned char *>(src);
    size_t done = 0;
    while (done < want) {
        const DWORD chunk = static_cast<DWORD>((want - done > 0x10000000u) ? 0x10000000u : (want - done));
        DWORD put = 0;
        if (::WriteFile(handle, in + done, chunk, &put, nullptr) == FALSE) {
            err = win_err("WriteFile failed");
            return false;
        }
        if (put == 0) {
            err = "WriteFile wrote 0 bytes";
            return false;
        }
        done += put;
    }
    return true;
}

} // namespace

TransportResult read_message(TransportHandle handle, std::string &payload, std::string &err)
{
    if (handle == nullptr) {
        err = "read_message: null handle";
        return TransportResult::IoError;
    }
    auto *h = static_cast<HANDLE>(handle);

    unsigned char header[4]{};
    const TransportResult head = read_exact(h, header, sizeof(header), err);
    if (head != TransportResult::Ok) {
        return head;
    }

    // u32 リトルエンディアン（v1 §2.1）。ホストのバイト順に依存しないよう手で組む。
    const uint32_t length = static_cast<uint32_t>(header[0]) | (static_cast<uint32_t>(header[1]) << 8) | (static_cast<uint32_t>(header[2]) << 16) | (static_cast<uint32_t>(header[3]) << 24);
    if (length > kProtocolMaxPayloadBytes) {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "payload length %lu exceeds the 64MB limit", static_cast<unsigned long>(length));
        err = buf;
        return TransportResult::TooLarge;
    }
    if (length == 0) {
        payload.clear();
        return TransportResult::Ok;
    }

    std::vector<char> body(static_cast<size_t>(length));
    const TransportResult rest = read_exact(h, body.data(), body.size(), err);
    if (rest != TransportResult::Ok) {
        // 長さは読めたのにペイロードが来ない＝フレーミング破損。Eof も Truncated に格上げする。
        return (rest == TransportResult::Eof) ? TransportResult::Truncated : rest;
    }
    payload.assign(body.begin(), body.end());
    return TransportResult::Ok;
}

bool write_message(TransportHandle handle, const std::string &payload, std::string &err)
{
    if (handle == nullptr) {
        err = "write_message: null handle";
        return false;
    }
    if (payload.size() > static_cast<size_t>(kProtocolMaxPayloadBytes)) {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "refusing to send %zu bytes (over the 64MB limit)", payload.size());
        err = buf;
        return false;
    }

    const uint32_t length = static_cast<uint32_t>(payload.size());
    std::vector<char> wire(sizeof(uint32_t) + payload.size());
    wire[0] = static_cast<char>(length & 0xFFu);
    wire[1] = static_cast<char>((length >> 8) & 0xFFu);
    wire[2] = static_cast<char>((length >> 16) & 0xFFu);
    wire[3] = static_cast<char>((length >> 24) & 0xFFu);
    if (!payload.empty()) {
        std::char_traits<char>::copy(wire.data() + 4, payload.data(), payload.size());
    }
    return write_exact(static_cast<HANDLE>(handle), wire.data(), wire.size(), err);
}

void set_stdio_binary_mode()
{
    // CRT 側（_read/_write 経由の誰かが混ざったときの保険）と、
    // 以降 fread/fwrite を使わないという意思表示を兼ねる。
    (void)::_setmode(::_fileno(stdin), _O_BINARY);
    (void)::_setmode(::_fileno(stdout), _O_BINARY);
    // stdout に stdio のバッファが残っていると、こちらの WriteFile と混ざって
    // 順序が壊れる。プロトコルを流す前に必ず吐き切っておく。
    (void)std::fflush(stdout);
}

TransportHandle transport_stdin()
{
    HANDLE h = ::GetStdHandle(STD_INPUT_HANDLE);
    return (h == INVALID_HANDLE_VALUE) ? nullptr : static_cast<TransportHandle>(h);
}

TransportHandle transport_stdout()
{
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    return (h == INVALID_HANDLE_VALUE) ? nullptr : static_cast<TransportHandle>(h);
}

} // namespace presentation

#else /* !_WIN32 */

/*
 * POSIX（fd）実装。Android の HD2D 版が使う。
 * あちらは 2 プロセスではなく **1 プロセス 2 スレッド＋ pipe()** だが、
 * この層から見える形は同じ（読む端と書く端があるだけ）。
 *
 * `TransportHandle` は void* なので、**fd + 1** を詰める（fd 0 = stdin が
 * nullptr と衝突しないように。取り出しは `fd_of`）。
 *
 * 注意: 相手が閉じたパイプへの write は SIGPIPE でプロセスごと落ちる。
 * **入口で `signal(SIGPIPE, SIG_IGN)` を済ませておくこと**（Android は
 * `platform/android/hd2d_entry_android.cpp` が行う）。無視しておけば
 * write が EPIPE を返し、ここが IoError として持ち帰る。
 */
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <unistd.h>

#include <string>
#include <vector>

namespace presentation {

namespace {

//! `TransportHandle`（fd + 1 を void* に詰めたもの）から fd を取り出す。
int fd_of(TransportHandle handle)
{
    return static_cast<int>(reinterpret_cast<intptr_t>(handle)) - 1;
}

//! `errno` を添えたエラー文字列（Win32 側の `win_err` と同じ役目）。
std::string posix_err(const char *what)
{
    char buf[160]{};
    std::snprintf(buf, sizeof(buf), "%s (errno=%d %s)", what, errno, std::strerror(errno));
    return std::string(buf);
}

//! `want` バイト読み切る。意味論は Win32 側 `read_exact` と同じ（部分読みはループ）。
TransportResult read_exact(int fd, void *dest, size_t want, std::string &err)
{
    auto *out = static_cast<unsigned char *>(dest);
    size_t done = 0;
    while (done < want) {
        const ssize_t got = ::read(fd, out + done, want - done);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            err = posix_err("read failed");
            return TransportResult::IoError;
        }
        if (got == 0) {
            if (done == 0) {
                return TransportResult::Eof;
            }
            err = "peer closed in the middle of a message";
            return TransportResult::Truncated;
        }
        done += static_cast<size_t>(got);
    }
    return TransportResult::Ok;
}

//! `want` バイト書き切る。`write` も部分書き込みを返しうるのでループする。
bool write_exact(int fd, const void *src, size_t want, std::string &err)
{
    const auto *in = static_cast<const unsigned char *>(src);
    size_t done = 0;
    while (done < want) {
        const ssize_t put = ::write(fd, in + done, want - done);
        if (put < 0) {
            if (errno == EINTR) {
                continue;
            }
            err = posix_err("write failed");
            return false;
        }
        if (put == 0) {
            err = "write wrote 0 bytes";
            return false;
        }
        done += static_cast<size_t>(put);
    }
    return true;
}

} // namespace

TransportHandle transport_from_fd(int fd)
{
    return (fd < 0) ? nullptr : reinterpret_cast<TransportHandle>(static_cast<intptr_t>(fd) + 1);
}

TransportResult read_message(TransportHandle handle, std::string &payload, std::string &err)
{
    if (handle == nullptr) {
        err = "read_message: null handle";
        return TransportResult::IoError;
    }
    const int fd = fd_of(handle);

    unsigned char header[4]{};
    const TransportResult head = read_exact(fd, header, sizeof(header), err);
    if (head != TransportResult::Ok) {
        return head;
    }

    // u32 リトルエンディアン（v1 §2.1）。Win32 側と同じく手で組む。
    const uint32_t length = static_cast<uint32_t>(header[0]) | (static_cast<uint32_t>(header[1]) << 8) | (static_cast<uint32_t>(header[2]) << 16) | (static_cast<uint32_t>(header[3]) << 24);
    if (length > kProtocolMaxPayloadBytes) {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "payload length %lu exceeds the 64MB limit", static_cast<unsigned long>(length));
        err = buf;
        return TransportResult::TooLarge;
    }
    if (length == 0) {
        payload.clear();
        return TransportResult::Ok;
    }

    std::vector<char> body(static_cast<size_t>(length));
    const TransportResult rest = read_exact(fd, body.data(), body.size(), err);
    if (rest != TransportResult::Ok) {
        // 長さは読めたのにペイロードが来ない＝フレーミング破損（Win32 側と同じ格上げ）。
        return (rest == TransportResult::Eof) ? TransportResult::Truncated : rest;
    }
    payload.assign(body.begin(), body.end());
    return TransportResult::Ok;
}

bool write_message(TransportHandle handle, const std::string &payload, std::string &err)
{
    if (handle == nullptr) {
        err = "write_message: null handle";
        return false;
    }
    if (payload.size() > static_cast<size_t>(kProtocolMaxPayloadBytes)) {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "refusing to send %zu bytes (over the 64MB limit)", payload.size());
        err = buf;
        return false;
    }

    // ヘッダとペイロードを 1 回の write にまとめる理由は Win32 側と同じ（v1 §2.1）。
    const uint32_t length = static_cast<uint32_t>(payload.size());
    std::vector<char> wire(sizeof(uint32_t) + payload.size());
    wire[0] = static_cast<char>(length & 0xFFu);
    wire[1] = static_cast<char>((length >> 8) & 0xFFu);
    wire[2] = static_cast<char>((length >> 16) & 0xFFu);
    wire[3] = static_cast<char>((length >> 24) & 0xFFu);
    if (!payload.empty()) {
        std::char_traits<char>::copy(wire.data() + 4, payload.data(), payload.size());
    }
    return write_exact(fd_of(handle), wire.data(), wire.size(), err);
}

void set_stdio_binary_mode()
{
    // POSIX にテキストモードは無い。stdio バッファの吐き出しだけ Win32 側に合わせる。
    (void)std::fflush(stdout);
}

TransportHandle transport_stdin()
{
    return transport_from_fd(0);
}

TransportHandle transport_stdout()
{
    return transport_from_fd(1);
}

} // namespace presentation

#endif /* _WIN32 */
