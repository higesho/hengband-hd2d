#pragma once
// Android 用の代わりの <execinfo.h>。bionic に backtrace が入ったのは API 33 からで、Quest（minSdk 32）の組み立てでは宣言が無い。
// 原作の crash.cc（クラッシュのときの呼び出しの記録。write_stack_trace）だけがこれを引く。原作自身の Android 版は
// `#undef BACKTRACE_SUPPORTED` で使わない（crash.cc の `defined(__ANDROID__)`）ので、同じに「記録は取れない」代わりにする。
// 何も返さない物にして、リンクにも実行時にも libc の記号を要らなくする。crash.cc にだけ -I で当てる（dcss_android_core.cmake）。
static inline int backtrace(void **, int) { return 0; }
static inline char **backtrace_symbols(void *const *, int) { return 0; }
static inline void backtrace_symbols_fd(void *const *, int, int) {}
