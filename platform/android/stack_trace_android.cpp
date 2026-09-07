/*!
 * @file stack_trace_android.cpp
 * @brief `util::StackTrace` の Android 実装（`src/util/stack-trace.h`）
 *
 * bionic に `execinfo.h`（`backtrace` / `backtrace_symbols`）は無い。
 * `_Unwind_Backtrace` で戻りアドレスだけは取れるので、そこまでを残す。
 * 記号化は行わない（NDK の `ndk-stack` に `logcat` を食わせれば行番号まで戻せる）。
 */
#include "system/h-basic.h"
#include "util/stack-trace.h"

#include <dlfcn.h>
#include <unwind.h>

#include <cstdio>
#include <sstream>
#include <utility>

namespace util {

struct StackTrace::Frame {
    void *address;
    std::string symbol_name;
};

namespace {

constexpr int kFramesMax = 100;

struct UnwindState {
    void **current;
    void **end;
};

_Unwind_Reason_Code unwind_callback(_Unwind_Context *context, void *arg)
{
    auto *state = static_cast<UnwindState *>(arg);
    const auto pc = _Unwind_GetIP(context);
    if (pc != 0) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        }
        *state->current++ = reinterpret_cast<void *>(pc);
    }
    return _URC_NO_REASON;
}

} // namespace

StackTrace::StackTrace()
{
    void *addrs[kFramesMax]{};
    UnwindState state{ addrs, addrs + kFramesMax };
    _Unwind_Backtrace(&unwind_callback, &state);

    const auto count = static_cast<size_t>(state.current - addrs);
    try {
        for (size_t i = 0; i < count; ++i) {
            this->frames.push_back(Frame{ addrs[i], std::string{} });
        }
    } catch (...) {
    }
}

StackTrace::~StackTrace() = default;

/*!
 * @brief 各フレームを「モジュール名 ＋ その中のオフセット」で出す。
 * @details 絶対アドレスだけでは ASLR のせいで後から追えない。`dladdr` で
 * 読み込み基準を引き、**`ndk-stack` や `llvm-addr2line` にそのまま渡せる形**にする。
 * 記号名が取れる場合（動的シンボル）は併記する。
 */
std::string StackTrace::dump() const
{
    std::ostringstream oss;
    for (auto frame_no = 0; const auto &frame : this->frames) {
        char buf[512]{};
        Dl_info info{};
        if (::dladdr(frame.address, &info) != 0 && info.dli_fname != nullptr) {
            const auto offset = reinterpret_cast<const char *>(frame.address)
                - static_cast<const char *>(info.dli_fbase);
            std::snprintf(buf, sizeof(buf), "#%02d pc %012zx  %s%s%s",
                frame_no, static_cast<size_t>(offset), info.dli_fname,
                (info.dli_sname != nullptr) ? "  " : "",
                (info.dli_sname != nullptr) ? info.dli_sname : "");
        } else {
            std::snprintf(buf, sizeof(buf), "#%02d pc %p  (unknown)", frame_no, frame.address);
        }
        ++frame_no;
        oss << buf << '\n';
    }
    return oss.str();
}

} // namespace util
