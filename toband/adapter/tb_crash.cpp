/*!
 * @file tb_crash.cpp
 * @brief 落ちたとき（アクセス違反など）に、呼ばれていた関数の並びを stderr へ書く（Windows だけ）。
 * @details この PC にはデバッガが無く、コアが落ちた場所を取れなかった（2026-10-16。
 * ダンジョンの階のセーブの読み込みと、アルモリカ城への進入で 0xC0000005）。
 * 組み立てで出る `TobandCore.pdb` から、Windows 標準の dbghelp で関数名と行番号を引く。
 * 書いたあとは既定の扱い（プロセスの終了）に戻す。遊びの振る舞いは変えない。
 */
#include "tb_crash.h"

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

namespace {

LONG WINAPI tb_crash_filter(EXCEPTION_POINTERS *info)
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    std::fprintf(stderr, "[toband] crash: exception 0x%08lX at %p\n",
        static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode), info->ExceptionRecord->ExceptionAddress);
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
        std::fprintf(stderr, "[toband] crash: %s address %p\n",
            (info->ExceptionRecord->ExceptionInformation[0] == 0) ? "read" : "write",
            reinterpret_cast<void *>(info->ExceptionRecord->ExceptionInformation[1]));
    }
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame{};
#if defined(_M_IX86)
    const DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
#else
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256]{};
    auto *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
    for (int depth = 0; depth < 40; ++depth) {
        if (!StackWalk64(machine, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }
        const DWORD64 pc = frame.AddrPC.Offset;
        if (pc == 0) {
            break;
        }
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        const char *name = SymFromAddr(process, pc, &displacement, symbol) ? symbol->Name : "?";
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0;
        if (SymGetLineFromAddr64(process, pc, &line_displacement, &line)) {
            std::fprintf(stderr, "[toband] crash: #%02d %s  %s(%lu)\n", depth, name, line.FileName, line.LineNumber);
        } else {
            std::fprintf(stderr, "[toband] crash: #%02d %s  +0x%llx\n", depth, name, static_cast<unsigned long long>(displacement));
        }
    }
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void tb_install_crash_handler()
{
    SetUnhandledExceptionFilter(tb_crash_filter);
}

#else

void tb_install_crash_handler()
{
}

#endif
