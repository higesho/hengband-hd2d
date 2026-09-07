# =============================================================================
#  Hengband — Android ボクセル HD2D（第 3 の実行体）ネイティブビルド
#
# Windows 版（HengbandHd2d.exe ＋ HengbandCore.exe
#  の 2 プロセス）と**同じソース**を使い、Android では 1 プロセス 2 スレッドに束ねる：
#    - SDL main スレッド … hd2d/**（窓・GLES 3.2・入力・描画）
#    - コアスレッド        … src/** ＋ presentation/**（platform/windows/core_main.cpp の
#                            共有本体 hengband_core_protocol_main を pipe() で結線）
#
#  旧 SDL2 UI 版（android/app）との違いは 3 つだけ：
#    1. ui/** の代わりに hd2d/** を組む（GLES 互換層も virtual_pad も使わない）
#    2. 入口が platform/android/hd2d_entry_android.cpp（コアスレッドの結線を持つ）
#    3. 内部 UTF-8（HengbandCore.exe と同じ）。EUC の変換ツリーは 2026-08-16 に廃止した
# =============================================================================
cmake_minimum_required(VERSION 3.22)
project(hengband_hd2d_android CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# android/hd2d/src/main/cpp から見たリポジトリ直下
get_filename_component(HB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(HB_TP "${HB_ROOT}/third_party/android")

message(STATUS "[hengband-hd2d] root = ${HB_ROOT}")

# -----------------------------------------------------------------------------
# SDL2 群（third_party/android。tools/fetch_android_deps.ps1 が展開する）
# 設定は android/app と同じ（設定を変えると .so の中身が食い違い、両アプリを
# 併載したとき紛らわしいため、意図的に 1 字も変えない）。
# -----------------------------------------------------------------------------
foreach(dep SDL2 SDL2_image SDL2_ttf SDL2_mixer)
    if(NOT EXISTS "${HB_TP}/${dep}/CMakeLists.txt")
        message(FATAL_ERROR
            "third_party/android/${dep} がありません。"
            "先に tools/fetch_android_deps.ps1 を実行してください。")
    endif()
endforeach()

set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

set(SDL_SHARED ON  CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
add_subdirectory("${HB_TP}/SDL2" SDL2 EXCLUDE_FROM_ALL)

set(SDL2IMAGE_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_TESTS   OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_VENDORED OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
foreach(fmt AVIF JXL TIF WEBP QOI XCF XPM XV SVG PNM LBM PCX TGA)
    set(SDL2IMAGE_${fmt} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL2IMAGE_PNG ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_JPG ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_BMP ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_GIF OFF CACHE BOOL "" FORCE)
add_subdirectory("${HB_TP}/SDL2_image" SDL2_image EXCLUDE_FROM_ALL)

set(SDL2TTF_INSTALL  OFF CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES  OFF CACHE BOOL "" FORCE)
set(SDL2TTF_VENDORED ON  CACHE BOOL "" FORCE)
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
add_subdirectory("${HB_TP}/SDL2_ttf" SDL2_ttf EXCLUDE_FROM_ALL)

set(SDL2MIXER_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_VENDORED OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_CMD     OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_FLAC    OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MOD     OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MIDI    OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_OPUS    OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_VORBIS  OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_WAVPACK OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_GME     OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3     ON  CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3_MINIMP3 ON CACHE BOOL "" FORCE)
set(SDL2MIXER_WAVE    ON  CACHE BOOL "" FORCE)
add_subdirectory("${HB_TP}/SDL2_mixer" SDL2_mixer EXCLUDE_FROM_ALL)

# -----------------------------------------------------------------------------
# コア（src/）— **内部 UTF-8**（Windows の HengbandCore.exe と同じ）
#
# かつては EUC の変換ツリー（transcode_src_to_euc.py）を組んでいた。NDK の clang に
# `-fexec-charset` が無く、`_()` のリテラルを EUC で作る必要があったためである。
# 内部 UTF-8 になった今、**ソース（UTF-8）をそのまま組めばリテラルも UTF-8** で、
# 変換ツリーは丸ごと要らない（clang の実行文字集合は既定で UTF-8）。
# EUC のセーブ・中継サーバのワイヤは iconv の道（locale/character-encoding.cpp。
# bionic は EUC-JP 非対応なので hb_iconv に差し替え）で読む。
# -----------------------------------------------------------------------------
file(GLOB_RECURSE HB_CORE_SRC CONFIGURE_DEPENDS "${HB_ROOT}/src/*.cpp")
set(HB_CORE_EXCLUDE
    "/src/main\\.cpp$"
    "/src/main-win\\.cpp$"
    "/src/main-x11\\.cpp$"
    "/src/main-gcu\\.cpp$"
    "/src/main-cap\\.cpp$"
    "/src/maid-x11\\.cpp$"
    "/src/main-win/"
    "/src/main-unix/x11-"
    "/src/main-unix/unix-music\\.cpp$"
    "/src/main-unix/unix-sound\\.cpp$"
    "/src/main-unix/stack-trace-unix\\.cpp$"
    "/src/test/"
)
foreach(pat ${HB_CORE_EXCLUDE})
    list(FILTER HB_CORE_SRC EXCLUDE REGEX "${pat}")
endforeach()
list(LENGTH HB_CORE_SRC HB_CORE_COUNT)
message(STATUS "[hengband-hd2d] core sources = ${HB_CORE_COUNT}")

list(APPEND HB_CORE_SRC "${HB_ROOT}/src/external-lib/fmt/format.cc")

# -----------------------------------------------------------------------------
# presentation（プロトコルとコア側の橋）— ui/ は組まない（hd2d が代わり）
# -----------------------------------------------------------------------------
file(GLOB_RECURSE HB_PRESENTATION_SRC CONFIGURE_DEPENDS "${HB_ROOT}/presentation/*.cpp")
list(FILTER HB_PRESENTATION_SRC EXCLUDE REGEX "/audio/sdl_win_audio\\.cpp$")

# -----------------------------------------------------------------------------
# hd2d（描画と UI）— エディタ（P9）は含めない（Windows の配布ビルドと同じ。
# HD2D_EDITOR を定義しないことと対で、hd2d/edit/ の TU を丸ごと外す）
# -----------------------------------------------------------------------------
file(GLOB_RECURSE HB_HD2D_SRC CONFIGURE_DEPENDS "${HB_ROOT}/hd2d/*.cpp")
list(FILTER HB_HD2D_SRC EXCLUDE REGEX "/hd2d/edit/")
# 音の装置。**OpenAL の本体は Windows だけ**で、
# こちらは空実装（audio_engine_null.cpp）を組む。あちらを Android でどう組むかが
# 決まったら、この 1 行を外して null のほうを外す。
list(FILTER HB_HD2D_SRC EXCLUDE REGEX "/hd2d/audio/audio_engine\\.cpp$")
list(LENGTH HB_HD2D_SRC HB_HD2D_COUNT)
message(STATUS "[hengband-hd2d] hd2d sources = ${HB_HD2D_COUNT}")

# -----------------------------------------------------------------------------
# platform — 入口とAndroid ヘルパ（旧版と共用）＋ コア側入口
# -----------------------------------------------------------------------------
# UI（libmain.so）側の platform TU。コアの TU は持たない（多コア形）。
set(HB_UI_PLATFORM_SRC
    "${HB_ROOT}/platform/android/hd2d_entry_android.cpp"
    "${HB_ROOT}/platform/android/android_paths.cpp"
    "${HB_ROOT}/platform/android/android_asset_installer.cpp"
    "${HB_ROOT}/platform/android/android_log_redirect.cpp"
    "${HB_ROOT}/platform/android/android_safe_area.cpp"
    "${HB_ROOT}/platform/android/android_save_migrator.cpp"
    "${HB_ROOT}/platform/android/android_iconv.cpp"
)

# コア .so 側の platform TU（UI と重複する TU があっても RTLD_LOCAL なので衝突しない）
set(HB_CORE_PLATFORM_SRC
    # コア側入口（共有本体 hengband_core_protocol_main と dlsym 用 hengband_core_entry）。
    # 内部 UTF-8 なので素の木でよい
    "${HB_ROOT}/platform/windows/core_main.cpp"
    "${HB_ROOT}/platform/android/android_iconv.cpp"
    "${HB_ROOT}/platform/android/stack_trace_android.cpp"
)

# UI が使う presentation は frame（ワイヤ層）だけ。bridge/bootstrap/term はコアの持ち物
# （Windows で HengbandHd2d.exe が frame だけ再コンパイルするのと同じ線引き）。
file(GLOB HB_FRAME_SRC CONFIGURE_DEPENDS "${HB_ROOT}/presentation/frame/*.cpp")

# ターゲット定義は下（SDL 互換ヘッダの後）でまとめて行う。

# -----------------------------------------------------------------------------
# `<SDL2/...>` 形式の互換ヘッダ（android/app と同じ理屈）。
# hd2d は SDL_opengl.h を使わない（GL は render/gl_core の自前ローダ）ので作らない。
# -----------------------------------------------------------------------------
set(HB_SDL_COMPAT_DIR "${CMAKE_CURRENT_BINARY_DIR}/sdl2-compat")
file(WRITE "${HB_SDL_COMPAT_DIR}/SDL2/SDL.h"        "#pragma once\n#include <SDL.h>\n")
file(WRITE "${HB_SDL_COMPAT_DIR}/SDL2/SDL_image.h"  "#pragma once\n#include <SDL_image.h>\n")
file(WRITE "${HB_SDL_COMPAT_DIR}/SDL2/SDL_ttf.h"    "#pragma once\n#include <SDL_ttf.h>\n")
file(WRITE "${HB_SDL_COMPAT_DIR}/SDL2/SDL_mixer.h"  "#pragma once\n#include <SDL_mixer.h>\n")

# =============================================================================
# ライブラリ構成
#   libmain.so     … UI（hd2d/**＋presentation/frame＋Android 入口）。コアを含まない
#   libhengcore.so … 変愚蛮怒コア（src/**＋presentation/**＋core_main）
# libtangcore.so … 短愚蛮怒コア
# コア選択画面で選ばれた .so を dlopen して `hengband_core_entry` を引く——Windows の
# HengbandCore.exe / TangbandCore.exe の選択と同じ形を 1 プロセスでやる。
# =============================================================================
function(hengband_common_config target)
    target_include_directories(${target} PRIVATE
        "${HB_SDL_COMPAT_DIR}"
        "${HB_ROOT}/src"
        "${HB_ROOT}/src/external-lib/include"
        "${HB_ROOT}/presentation"
        "${HB_ROOT}/hd2d"
        "${HB_ROOT}/platform"
    )
    target_compile_definitions(${target} PRIVATE
        JP
        UTF8    # 内部 UTF-8（HengbandCore.exe と同じ。実行時多言語化がそのまま載る）
        HAVE_STDINT_H
        DISABLE_NET
        HENGBAND_ANDROID=1
        HENGBAND_SDL2_UI=1      # HengbandCore.exe と同じ（src/main-win.cpp の WinMain を黙らせる等）
    )
    target_compile_options(${target} PRIVATE
        # NDK の -DANDROID を打ち消す
        -UANDROID
        # bionic の iconv は EUC-JP 非対応。自前実装へ差し替える
        -Diconv_open=hb_iconv_open
        -Diconv_close=hb_iconv_close
        -Diconv=hb_iconv
        -Wno-everything
        -ffunction-sections -fdata-sections
    )
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
endfunction()

add_library(main SHARED
    ${HB_HD2D_SRC}
    ${HB_FRAME_SRC}
    ${HB_UI_PLATFORM_SRC}
    "${HB_ROOT}/src/external-lib/fmt/format.cc"
)
hengband_common_config(main)
target_compile_definitions(main PRIVATE
    HENGBAND_GLES=1         # hd2d の GL を GLES 3.2 へ（gl_program / gl_core / hd2d_app が見る）
)

target_link_libraries(main PRIVATE
    SDL2::SDL2
    SDL2_image::SDL2_image
    SDL2_ttf::SDL2_ttf
    SDL2_mixer::SDL2_mixer
    GLESv3
    EGL
    android
    log
)

# コアも SDL2 / SDL2_mixer にリンクする——Android では**音はコア側が鳴らす**
# （presentation/audio/sdl_mixer_audio.cpp。v1 の「音はコアが自分で鳴らす」建付け）。
# libSDL2.so は共有なので、UI と同じ実体を使う＝単一ライブラリ時代と同じ意味論。
add_library(hengcore SHARED ${HB_CORE_SRC} ${HB_PRESENTATION_SRC} ${HB_CORE_PLATFORM_SRC})
hengband_common_config(hengcore)
target_link_libraries(hengcore PRIVATE SDL2::SDL2 SDL2_mixer::SDL2_mixer log)

add_library(tangcore SHARED ${HB_CORE_SRC} ${HB_PRESENTATION_SRC} ${HB_CORE_PLATFORM_SRC})
hengband_common_config(tangcore)
target_compile_definitions(tangcore PRIVATE TANGBAND=1)
target_link_libraries(tangcore PRIVATE SDL2::SDL2 SDL2_mixer::SDL2_mixer log)

# =============================================================================
#  旧 C の 2 コア（幻想蛮怒・Sil-Q）— libgensocore.so / libsilcore.so
#
# 上の 2 本（変愚・短愚蛮怒）とは**ソースツリーが別**で、
#  `src/` も `presentation/bridge` も共有しない。共有するのは
#  `presentation/frame`（線の符号化）と `presentation/bridge/input_event_adapter`、
#  それに `platform/portable/`（Windows API の代役）だけである。
#
#  ## 変換ツリー（build-src-sjis）が要る理由
#  この 2 つは **CP932 の世界**で書かれている:
#    - `gensoband/src` の 88 ファイルは**ソースそのものが CP932**
#    - アダプタは UTF-8 だが、**MSVC が CP932 で焼いていた**（vcxproj に /utf-8 が無い）
#  NDK の clang は入力を UTF-8 と決め打ち、実行文字集合も UTF-8 で動かせない。
#  そこで `tools/transcode_cp932_src.py` が
#  **リテラルのバイト列を CP932 の 8 進逃げ札で書き直した変換ツリー**を作り、それを組む。
#  Windows の実行体と 1 バイトも違わないものが焼ける（同 §2b の検算）。
#
#  ## SET_UID について
#  Windows 版は `WIN32` から `WINDOWS` が立ち、`h-config.h` が `SET_UID` を**外す**。
#  こちらは Unix なので `SET_UID` が立つ——これは先方が元から持っている Unix 構成
#  （`main-x11.c` / `main-gcu.c` 側の道）であって、こちらの発明ではない。
#  ただし**保存先の決め方が変わる**ので、実機で新規作成まで通すこと（Linux 版で
#  同じ穴を踏んでいる。記憶 hengband-linux-port）。
# =============================================================================
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(FATAL_ERROR
        "Python が見つかりません。旧 C の 2 コア（幻想蛮怒・Sil-Q）の影の木を作るのに要ります。"
        "-DPython3_EXECUTABLE=<path> で場所を教えてください。")
endif()

set(HB_SJIS_TOOL "${HB_ROOT}/tools/transcode_cp932_src.py")
set(HB_SJIS_DIR "${HB_ROOT}/android/build-src-sjis")

#! 写す木の一覧。`(写し元, 行き先, 元の文字コード, 拡張子)` の 4 つ組を `|` で綴じる。
set(HB_SJIS_TREES
    "gensoband/src|gensoband/src|cp932|.c,.h"
    "gensoband/adapter|gensoband/adapter|utf8|.c,.cpp,.h"
    "silq/adapter|silq/adapter|utf8|.c,.cpp,.h"
)

# 段取り前に 1 回走らせる。**glob より先でなければならない**（無い木は glob できない）。
foreach(spec ${HB_SJIS_TREES})
    string(REPLACE "|" ";" parts "${spec}")
    list(GET parts 0 from_dir)
    list(GET parts 1 to_dir)
    list(GET parts 2 from_enc)
    list(GET parts 3 exts)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${HB_SJIS_TOOL}"
                "--from" "${from_enc}" "--ext" "${exts}" "--quiet"
                "${HB_ROOT}/${from_dir}" "${HB_SJIS_DIR}/${to_dir}"
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "影の木を作れませんでした: ${from_dir}（rc=${rc}）")
    endif()
endforeach()

# 中身が変わったときのために、**組むたびにも**走らせる（同じ中身のファイルは触らない
# ので、ここが毎回走っても再コンパイルは起きない）。
#
# **`add_custom_command(TARGET ... PRE_BUILD)` は使わない。** あれが本当に「組む前」に
# なるのは Visual Studio の生成器だけで、他は PRE_LINK 扱いになり、link の無い
# カスタムターゲットでは**黙って走らない**。コマンドはターゲット本体に持たせる。
set(HB_SJIS_COMMANDS "")
foreach(spec ${HB_SJIS_TREES})
    string(REPLACE "|" ";" parts "${spec}")
    list(GET parts 0 from_dir)
    list(GET parts 1 to_dir)
    list(GET parts 2 from_enc)
    list(GET parts 3 exts)
    list(APPEND HB_SJIS_COMMANDS
        COMMAND "${Python3_EXECUTABLE}" "${HB_SJIS_TOOL}"
                "--from" "${from_enc}" "--ext" "${exts}" "--quiet"
                "${HB_ROOT}/${from_dir}" "${HB_SJIS_DIR}/${to_dir}")
endforeach()
add_custom_target(hb_transcode_sjis ALL
    ${HB_SJIS_COMMANDS}
    COMMENT "[hengband-hd2d] CP932 の影の木を更新（幻想蛮怒・Sil-Q）"
    VERBATIM)

#! 線の符号化と入力の翻訳。**この 4 つだけ**が新旧で共有される（vcxproj と同じ並び）。
set(HB_LEGACY_SHARED_SRC
    "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp"
    "${HB_ROOT}/presentation/frame/frame_codec.cpp"
    "${HB_ROOT}/presentation/frame/protocol_messages.cpp"
    "${HB_ROOT}/presentation/frame/protocol_transport.cpp"
    # Windows API の代役（`GetFileAttributesA` 等と CP932 変換）。
    "${HB_ROOT}/platform/portable/legacy_os.cpp"
    "${HB_ROOT}/platform/portable/cp932.cpp"
)

#! 組まない TU。vcxproj が挙げていないものと同じ（入口・X11・DIB）。
set(HB_LEGACY_EXCLUDE
    "/main\\.c$"
    "/main-win\\.c$"
    "/main-x11\\.c$"
    "/main-gcu\\.c$"
    "/maid-x11\\.c$"
    "/readdib\\.c$"
)

#! 旧 C コア 1 本ぶんの共通設定。
function(hengband_legacy_config target src_dir adapter_dir)
    target_include_directories(${target} PRIVATE
        "${adapter_dir}"
        "${src_dir}"
        "${HB_ROOT}/presentation"
        "${HB_ROOT}/src/external-lib/include"
        "${HB_ROOT}/platform"
        # bionic に無い古いヘッダの身代わり（`<sys/timeb.h>`）。**いちばん後ろに置く**
        # ——本物が在るヘッダを横取りしないため。
        "${HB_ROOT}/platform/portable/android_compat"
    )
    target_compile_options(${target} PRIVATE
        -Wno-everything
        # 20 年もののソースなので、いまの clang が既定で**エラー**にする書き方が残っている。
        # 警告に戻すだけで、意味は Windows 版（MSVC）と同じである。
        -Wno-implicit-function-declaration
        -Wno-implicit-int
        -Wno-int-conversion
        -Wno-incompatible-function-pointer-types
        # ヘッダに `int flag_generate_doppelganger;` のような**暫定定義**が置いてある。
        # MSVC も昔の gcc もこれを 1 つに束ねたが、clang の既定は `-fno-common` なので
        # 「重複した記号」で link が落ちる。昔と同じ束ね方に戻す（C のみ。C++ に口は無い）。
        $<$<COMPILE_LANGUAGE:C>:-fcommon>
        -ffunction-sections -fdata-sections
    )
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
    target_link_libraries(${target} PRIVATE log)
    add_dependencies(${target} hb_transcode_sjis)
endfunction()

# ---- 幻想蛮怒
file(GLOB HB_GB_SRC CONFIGURE_DEPENDS "${HB_SJIS_DIR}/gensoband/src/*.c")
foreach(pat ${HB_LEGACY_EXCLUDE})
    list(FILTER HB_GB_SRC EXCLUDE REGEX "${pat}")
endforeach()
file(GLOB HB_GB_ADAPTER_SRC CONFIGURE_DEPENDS
    "${HB_SJIS_DIR}/gensoband/adapter/*.c" "${HB_SJIS_DIR}/gensoband/adapter/*.cpp")
list(LENGTH HB_GB_SRC HB_GB_COUNT)
message(STATUS "[hengband-hd2d] gensoband sources = ${HB_GB_COUNT}")

add_library(gensocore SHARED ${HB_GB_SRC} ${HB_GB_ADAPTER_SRC} ${HB_LEGACY_SHARED_SRC})
hengband_legacy_config(gensocore
    "${HB_SJIS_DIR}/gensoband/src" "${HB_SJIS_DIR}/gensoband/adapter")
# vcxproj と同じ（`WINDOWS` と `WIN32` は当然こちらには無い）。
target_compile_definitions(gensocore PRIVATE JP SJIS)

# ---- Sil-Q
#
# `silq/src` は**影に写さない**——中身は ASCII で、非 ASCII は註釈の UTF-8 だけである
# （2026-08-21 に全数で確かめた）。写す必要が無いものを写すと、追う先が増えるだけ。
file(GLOB HB_SQ_SRC CONFIGURE_DEPENDS "${HB_ROOT}/silq/src/*.c")
foreach(pat ${HB_LEGACY_EXCLUDE})
    list(FILTER HB_SQ_SRC EXCLUDE REGEX "${pat}")
endforeach()
file(GLOB HB_SQ_ADAPTER_SRC CONFIGURE_DEPENDS
    "${HB_SJIS_DIR}/silq/adapter/*.c" "${HB_SJIS_DIR}/silq/adapter/*.cpp")
list(LENGTH HB_SQ_SRC HB_SQ_COUNT)
message(STATUS "[hengband-hd2d] silq sources = ${HB_SQ_COUNT}")

add_library(silcore SHARED ${HB_SQ_SRC} ${HB_SQ_ADAPTER_SRC} ${HB_LEGACY_SHARED_SRC})
hengband_legacy_config(silcore "${HB_ROOT}/silq/src" "${HB_SJIS_DIR}/silq/adapter")

# ---- FroxComposband
#
# Sil-Q と同じ英語の C コアだが**さらに素直**——`frox/src` も `frox/adapter` も
# 影に写さない。src は純 ASCII（P3 で全数確認済み）、アダプタは註釈が UTF-8 で
# **リテラルは ASCII だけ**（設計 §3.1。clang は UTF-8 をそのまま読む）。
# M1 で日本語リテラルが入ったら silq/adapter と同じく変換ツリーへ移すこと
# （HB_SJIS_TREES に 1 行足すだけ）。
#
# MSVC の穴 2 つ（設計 §3.7）はどちらもこちらでは要らない:
#   - `z-doc.c` の `/FIwindows.h` … windows.h が無いので衝突も無い
#   - `fc_compat.c`（strncasecmp -> _strnicmp）… bionic には strncasecmp が
#     最初から在り、逆に _strnicmp が無いのでリンクで落ちる。**外す**
file(GLOB HB_FC_SRC CONFIGURE_DEPENDS "${HB_ROOT}/frox/src/*.c")
set(HB_FC_EXCLUDE
    ${HB_LEGACY_EXCLUDE}
    "/main-cap\\.c$"
    "/main-dos\\.c$"
    "/main-ibm\\.c$"
    "/main-mac\\.c$"
    "/main-sdl\\.c$"
    "/main-xaw\\.c$"
    "/z-doc-test\\.c$"
)
foreach(pat ${HB_FC_EXCLUDE})
    list(FILTER HB_FC_SRC EXCLUDE REGEX "${pat}")
endforeach()
file(GLOB HB_FC_ADAPTER_SRC CONFIGURE_DEPENDS
    "${HB_ROOT}/frox/adapter/*.c" "${HB_ROOT}/frox/adapter/*.cpp")
list(FILTER HB_FC_ADAPTER_SRC EXCLUDE REGEX "/fc_compat\\.c$")
list(LENGTH HB_FC_SRC HB_FC_COUNT)
message(STATUS "[hengband-hd2d] frox sources = ${HB_FC_COUNT}")

# doc UI の旗（M0.5 FH-02。設計 §14.2(a)）: vcxproj のファイル単位 define と同じ
# 6 TU にだけ `Term_save/Term_load` の横取りを差す。
set_source_files_properties(
    "${HB_ROOT}/frox/src/monspell.c"
    "${HB_ROOT}/frox/src/obj_prompt.c"
    "${HB_ROOT}/frox/src/skillmaster.c"
    "${HB_ROOT}/frox/src/weaponsmith.c"
    "${HB_ROOT}/frox/src/wild.c"
    "${HB_ROOT}/frox/src/wiz_obj.c"
    PROPERTIES COMPILE_DEFINITIONS "Term_save=fc_ui_term_save;Term_load=fc_ui_term_load")

# 共有 TU は FroxCore.vcxproj と同じ並び。**ただし `cp932.cpp` はここだけ足す**
# ——Windows の `fc_text.cpp` は `MultiByteToWideChar` を直に呼ぶが、Android には
# あれが無く、`platform/portable/cp932.cpp` の表引きに落ちるためである
# （M0 のあいだは「Frox は CP932 の世界を持たない」ので入れていなかった。
# M1 の日本語層で要るようになった（2026-08-24）。
add_library(froxcore SHARED ${HB_FC_SRC} ${HB_FC_ADAPTER_SRC}
    "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp"
    "${HB_ROOT}/presentation/frame/frame_codec.cpp"
    "${HB_ROOT}/presentation/frame/protocol_messages.cpp"
    "${HB_ROOT}/presentation/frame/protocol_transport.cpp"
    "${HB_ROOT}/platform/portable/legacy_os.cpp"
    "${HB_ROOT}/platform/portable/cp932.cpp"
)
hengband_legacy_config(froxcore "${HB_ROOT}/frox/src" "${HB_ROOT}/frox/adapter")

# -----------------------------------------------------------------------------
# Meta Quest ネイティブ
#
# **既定は OFF。電話版 :hd2d はここを 1 行も通らない**（OpenXR を知らないまま組む）。
# 立てるのは android/quest/build.gradle の `-DHENGBAND_QUEST=ON` だけで、そのとき
# hd2d/xr/xr_session.cpp と xr_input.cpp のスタブが本物へ替わる（同 §3）。
#
# loader は Khronos 純正の AAR を prefab 経由で貰う。`find_package` が見えるのは
# **使う側のモジュールに `buildFeatures.prefab` と dependency の両方がある**
# ときだけである（罠 Q-10）。
# -----------------------------------------------------------------------------
option(HENGBAND_QUEST "Meta Quest ネイティブ（OpenXR）を組み込む" OFF)
if(HENGBAND_QUEST)
    find_package(OpenXR REQUIRED CONFIG)
    target_compile_definitions(main PRIVATE HENGBAND_QUEST=1)
    target_link_libraries(main PRIVATE OpenXR::openxr_loader)
    message(STATUS "[hengband-hd2d] Quest: OpenXR loader を組み込みます")
endif()
