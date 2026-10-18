# =============================================================================
#  Hengband HD2D の Android 版（ネイティブ部分のビルド設定）
#
#  Windows 版（HengbandHd2d.exe と各コアの exe）と同じソースを使い、Android では
#  1 つのプロセスの中で 2 つのスレッドに分けて動かします。
#    - SDL のメインスレッド … hd2d/（ウィンドウ、GLES 3.2、入力、描画）
#    - コアのスレッド       … src/ と presentation/（platform/windows/core_main.cpp と
#                             共有している hengband_core_protocol_main を pipe() でつなぎます）
#
#  ビルドの手順
#    1. tools/fetch_android_deps.ps1 で SDL2 群を third_party/android/ に展開します
#    2. android/local.properties に sdk.dir=<Android SDK の場所> を書きます
#    3. python tools/build_android_hd2d_assets.py で素材をまとめます
#       （android/build-assets-hd2d/ に出ます。素材を変えたら作り直してください）
#    4. android/ で gradle -PhengbandAbi=arm64-v8a :hd2d:assemblePreview を実行します
#       APK は android/hd2d/build/outputs/apk/preview/hd2d-preview.apk にできます
#  以上をまとめて行うスクリプトが tools/package/Build-Hd2dApk.ps1 です。
#  Python 3 が必要です（幻想蛮怒と Sil-Q のソースを変換するために CMake からも呼びます）。
#
#  作られる .so
#    libmain.so      … 画面（hd2d/ と presentation/frame、Android の入口）
#    libopenal.so    … OpenAL Soft（画面側の音。電話版だけ。Quest は組まない）
#    libhengcore.so  … 変愚蛮怒のコア
#    libtangcore.so  … 短愚蛮怒のコア（同じソースを TANGBAND=1 でビルド）
#    libgensocore.so … 幻想蛮怒のコア
#    libsilcore.so   … Sil-Q のコア
#    libfroxcore.so  … FroxComposband のコア
#    libtobandcore.so … TOband のコア
#    librogue2core.so … RogueClone2 のコア
#    librogue36core.so … Rogue 3.6.4 のコア
#    librogue54core.so … Rogue 5.4.4 のコア
#  起動時のコア選択画面で選ばれた .so を dlopen し、hengband_core_entry を呼びます。
#
#  Meta Quest 版は android/quest/ からこのファイルを -DHENGBAND_QUEST=ON で使います。
# =============================================================================
cmake_minimum_required(VERSION 3.22)
project(hengband_hd2d_android CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# android/hd2d/src/main/cpp から見たリポジトリ直下
get_filename_component(HB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(HB_TP "${HB_ROOT}/third_party/android")

# UI 単体では原作ソースを要求しない。通常の APK は 8 コアを同梱する。
option(HENGBAND_BUILD_CORES "Build the eight external game cores" ON)
get_filename_component(HB_DEFAULT_CORE_SOURCES "${HB_ROOT}/../roguelike-cores/build-sources" ABSOLUTE)
if(DEFINED ENV{HENGBAND_CORE_SOURCE_ROOT})
    set(HB_DEFAULT_CORE_SOURCES "$ENV{HENGBAND_CORE_SOURCE_ROOT}")
endif()
set(HB_CORE_SOURCE_ROOT "${HB_DEFAULT_CORE_SOURCES}" CACHE PATH "Prepared external core sources")
if(HENGBAND_BUILD_CORES AND NOT EXISTS "${HB_CORE_SOURCE_ROOT}/.prepared.json")
    message(FATAL_ERROR "Run tools/core_sources/prepare.py first, or set HB_CORE_SOURCE_ROOT. UI-only builds may set HENGBAND_BUILD_CORES=OFF.")
endif()


message(STATUS "[hengband-hd2d] root = ${HB_ROOT}")
include("${HB_ROOT}/tools/core_sources/generated_headers.cmake")
include("${HB_ROOT}/tools/targeting/targeting_sources.cmake")
include("${HB_ROOT}/tools/rogue_legacy/rogue_legacy_sources.cmake")

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
# OpenAL Soft（画面側の音。 の 3 番・）
#
# Windows と同じ hd2d/audio/audio_engine.cpp（HRTF）をそのまま組むための依存。
# 版は Windows の vcpkg と同じ 1.25.1（tools/fetch_android_deps.ps1 が展開する）。
# **LGPL なので共有ライブラリ（libopenal.so）で入れる**（LIBTYPE SHARED）。
# 出力は OpenSL ES。Oboe（AAudio）は別のソースが要るので切る。
# 電話版だけ。Quest（-DHENGBAND_QUEST=ON）は指示があるまで空実装のまま。
# -----------------------------------------------------------------------------
if(NOT HENGBAND_QUEST)
    if(NOT EXISTS "${HB_TP}/openal-soft/CMakeLists.txt")
        message(FATAL_ERROR
            "third_party/android/openal-soft がありません。"
            "先に tools/fetch_android_deps.ps1 を実行してください。")
    endif()
    set(LIBTYPE SHARED CACHE STRING "" FORCE)
    foreach(opt UTILS EXAMPLES TESTS INSTALL INSTALL_CONFIG INSTALL_HRTF_DATA
            INSTALL_AMBDEC_PRESETS INSTALL_EXAMPLES INSTALL_UTILS UPDATE_BUILD_VERSION
            ENABLE_MODULES BACKEND_OBOE BACKEND_WAVE EAX)
        set(ALSOFT_${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(ALSOFT_NO_CONFIG_UTIL ON CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_OPENSL ON CACHE BOOL "" FORCE)
    set(ALSOFT_REQUIRE_OPENSL ON CACHE BOOL "" FORCE)
    add_subdirectory("${HB_TP}/openal-soft" openal-soft EXCLUDE_FROM_ALL)
endif()

# -----------------------------------------------------------------------------
# コア（src/）— **内部 UTF-8**（Windows の HengbandCore.exe と同じ）
#
# かつては EUC の変換ツリー（transcode_src_to_euc.py）を組んでいた。NDK の clang に
# `-fexec-charset` が無く、`_()` のリテラルを EUC で焼く必要があったためである。
# 内部 UTF-8 になった今、**ソース（UTF-8）をそのまま組めばリテラルも UTF-8** で、
# 変換ツリーは丸ごと要らない（clang の実行文字集合は既定で UTF-8）。
# EUC のセーブ・中継サーバのワイヤは iconv の道（locale/character-encoding.cpp。
# bionic は EUC-JP 非対応なので hb_iconv に差し替え）で読む。
# -----------------------------------------------------------------------------
file(GLOB_RECURSE HB_CORE_SRC CONFIGURE_DEPENDS "${HB_CORE_SOURCE_ROOT}/src/*.cpp")
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
list(FILTER HB_CORE_SRC EXCLUDE REGEX "/src/term/(z-term|screen-processor)\\.cpp$")
list(LENGTH HB_CORE_SRC HB_CORE_COUNT)
message(STATUS "[hengband-hd2d] core sources = ${HB_CORE_COUNT}")

list(APPEND HB_CORE_SRC "${HB_CORE_SOURCE_ROOT}/src/external-lib/fmt/format.cc")

# 短愚蛮怒（libtangcore.so）は変愚 3.0.2.4 の写し（tangband/src）を自分のソースとして組む。
# 変愚の版上げで短愚を変えないため。除外は上と同じ。
file(GLOB_RECURSE HB_TANG_SRC CONFIGURE_DEPENDS "${HB_CORE_SOURCE_ROOT}/tangband/src/*.cpp")
foreach(pat ${HB_CORE_EXCLUDE})
    list(FILTER HB_TANG_SRC EXCLUDE REGEX "${pat}")
endforeach()
list(FILTER HB_TANG_SRC EXCLUDE REGEX "/src/term/(z-term|screen-processor)\\.cpp$")
list(APPEND HB_TANG_SRC "${HB_CORE_SOURCE_ROOT}/tangband/src/external-lib/fmt/format.cc")

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
# 音の装置（の 3 番。2026-09-25 に決定）。
# 電話版は Windows と同じ OpenAL Soft の本体（audio_engine.cpp）を組み、空実装を外す。
# Quest は指示があるまで空実装（audio_engine_null.cpp）のまま。
if(HENGBAND_QUEST)
    list(FILTER HB_HD2D_SRC EXCLUDE REGEX "/hd2d/audio/audio_engine\\.cpp$")
else()
    list(FILTER HB_HD2D_SRC EXCLUDE REGEX "/hd2d/audio/audio_engine_null\\.cpp$")
endif()
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
# ライブラリ構成（多コア形）
#   libmain.so     … UI（hd2d/**＋presentation/frame＋Android 入口）。コアを含まない
#   libhengcore.so … 変愚蛮怒コア（src/**＋presentation/**＋core_main）
#   libtangcore.so … 短愚蛮怒コア（同じ木＋TANGBAND=1）
# コア選択画面で選ばれた .so を dlopen して `hengband_core_entry` を引く——Windows の
# HengbandCore.exe / TangbandCore.exe の選択と同じ形を 1 プロセスでやる。
# =============================================================================
function(hengband_common_config target)
    target_include_directories(${target} PRIVATE
        "${HB_SDL_COMPAT_DIR}"
        "${HB_ROOT}/third_party/cpp/include"
        "${HB_ROOT}/presentation"
        "${HB_ROOT}/hd2d"
        "${HB_ROOT}/platform"
    )
    # 短愚蛮怒は自分のソース（tangband/src）と、そこから作った生成ヘッダだけを見る。
    if("${target}" STREQUAL "tangcore")
        target_include_directories(${target} PRIVATE "${HB_GENERATED_INCLUDE_DIR_TANGBAND}" "${HB_CORE_SOURCE_ROOT}/tangband/src" "${HB_CORE_SOURCE_ROOT}/tangband/src/external-lib/include")
    else()
        target_include_directories(${target} PRIVATE "${HB_GENERATED_INCLUDE_DIR}")
        if(NOT "${target}" STREQUAL "main")
            target_include_directories(${target} PRIVATE "${HB_CORE_SOURCE_ROOT}/src" "${HB_CORE_SOURCE_ROOT}/src/external-lib/include")
        endif()
    endif()
    target_compile_definitions(${target} PRIVATE
        JP
        UTF8    # 内部 UTF-8（HengbandCore.exe と同じ。実行時多言語化がそのまま載る）
        HAVE_STDINT_H
        DISABLE_NET
        HENGBAND_ANDROID=1
        HENGBAND_SDL2_UI=1      # HengbandCore.exe と同じ（src/main-win.cpp の WinMain を黙らせる等）
    )
    target_compile_options(${target} PRIVATE
        # NDK の -DANDROID を打ち消す（コアの PlayerRaceType::ANDROID と衝突。ANDROID_PORT.md A-T1）
        -UANDROID
        # bionic の iconv は EUC-JP 非対応。自前実装へ差し替える（ANDROID_PORT.md §1.2a）
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
    "${HB_ROOT}/third_party/cpp/fmt/format.cc"
    "${HB_ROOT}/platform/portable/cp932.cpp"
)
hengband_common_config(main)
target_compile_definitions(main PRIVATE
    HENGBAND_GLES=1         # hd2d の GL を GLES 3.2 へ（gl_program / gl_core / hd2d_app が見る）
)

target_link_libraries(main PRIVATE z
    SDL2::SDL2
    SDL2_image::SDL2_image
    SDL2_ttf::SDL2_ttf
    SDL2_mixer::SDL2_mixer
    GLESv3
    EGL
    android
    log
)
if(NOT HENGBAND_QUEST)
    target_link_libraries(main PRIVATE OpenAL)   # libopenal.so（上の OpenAL Soft）
endif()

# コアも SDL2 / SDL2_mixer にリンクする——画面側の音の装置（OpenAL）が開けなかったときは
# **コア側が鳴らす**（presentation/audio/sdl_mixer_audio.cpp）。
# 電話版は 2026-09-25 から画面側が開くので、ふだんはこちらは鳴らさない。
# libSDL2.so は共有なので、UI と同じ実体を使う＝単一ライブラリ時代と同じ意味論。
if(HENGBAND_BUILD_CORES)
add_library(hengcore SHARED ${HB_CORE_SRC} ${HB_PRESENTATION_SRC} ${HB_CORE_PLATFORM_SRC})
hengband_common_config(hengcore)
target_link_libraries(hengcore PRIVATE SDL2::SDL2 SDL2_mixer::SDL2_mixer log)

add_library(tangcore SHARED ${HB_TANG_SRC} ${HB_PRESENTATION_SRC} ${HB_CORE_PLATFORM_SRC})
hengband_common_config(tangcore)
target_compile_definitions(tangcore PRIVATE TANGBAND=1)
target_link_libraries(tangcore PRIVATE SDL2::SDL2 SDL2_mixer::SDL2_mixer log)

# 変愚蛮怒・短愚蛮怒コアのプリコンパイル済みヘッダ。Windows（VisualStudio/CorePch.props）・Linux
# （platform/linux/CMakeLists.txt）と同じ platform/shared/hengband_core_pch.h（原作の stdafx.h と、
# ほぼ全ソースが読む原作のヘッダの一覧）を使う。注意点は の「共有のプリコンパイル済みヘッダ」。
# 環境変数 HB_CORE_PCH=OFF（または -DHB_CORE_PCH=OFF）で使わずに組む（ヘッダに隠れた #include の書き忘れを見つける）。
# プリコンパイル済みヘッダを使うのは原作の src/ だけで、presentation・core_main・fmt は Windows と同じく外す。
# gradle から渡しにくいので、環境変数 HB_CORE_PCH=OFF でも切り替えられる（PowerShell: $env:HB_CORE_PCH='OFF'。環境変数を外せば ON に戻る）。
if(DEFINED ENV{HB_CORE_PCH})
    set(HB_CORE_PCH "$ENV{HB_CORE_PCH}")
endif()
option(HB_CORE_PCH "Use the shared Hengband precompiled header for hengcore/tangcore" ON)
if(HB_CORE_PCH)
    foreach(core hengcore tangcore)
        target_precompile_headers(${core} PRIVATE "${HB_ROOT}/platform/shared/hengband_core_pch.h")
    endforeach()
    set_source_files_properties(${HB_PRESENTATION_SRC} ${HB_CORE_PLATFORM_SRC} "${HB_CORE_SOURCE_ROOT}/src/external-lib/fmt/format.cc" "${HB_CORE_SOURCE_ROOT}/tangband/src/external-lib/fmt/format.cc" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif()

# =============================================================================
#  C で書かれた 2 つのコア（幻想蛮怒・Sil-Q）: libgensocore.so / libsilcore.so
#
#  上の 2 つ（変愚蛮怒・短愚蛮怒）とはソースツリーが別で、src/ も presentation/bridge も
#  共有しません。共有するのは presentation/frame（画面データの符号化）、
#  presentation/bridge/input_event_adapter、platform/portable/（Windows API の代わり）だけです。
#
#  変換ツリー（android/build-src-sjis/）について
#  この 2 つは CP932（Shift_JIS）を前提に書かれています。gensoband/src はソース自体が
#  CP932 で、アダプタは UTF-8 ですが Windows では MSVC が文字列リテラルを CP932 で
#  コンパイルしています（vcxproj に /utf-8 がありません）。NDK の clang は入力も
#  実行時の文字コードも UTF-8 固定なので、tools/transcode_cp932_src.py で文字列リテラルを
#  CP932 のバイト列（8 進エスケープ）に書き換えたソースのコピーを作り、それをビルドします。
#  こうすると Windows 版と同じバイト列の文字列になります。
#
#  SET_UID について
#  Windows 版は WIN32 から WINDOWS が定義され、h-config.h が SET_UID を外します。
#  Android は Unix 扱いなので SET_UID が定義されます。これは元のコードが持っている
#  Unix 向けの構成（main-x11.c / main-gcu.c 側）で、こちらで加えたものではありません。
#  ただしセーブ先の決め方が変わるので、変更したときは実機で新規作成まで確認してください。
# =============================================================================
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(FATAL_ERROR
        "Python が見つかりません。幻想蛮怒と Sil-Q のソースの変換ツリーを作るために必要です。"
        "-DPython3_EXECUTABLE=<path> で場所を教えてください。")
endif()

set(HB_SJIS_TOOL "${HB_ROOT}/tools/transcode_cp932_src.py")
set(HB_SJIS_DIR "${HB_CORE_SOURCE_ROOT}/android-sjis")

#! 写す木の一覧。`(写し元, 行き先, 元の文字コード, 拡張子)` の 4 つ組を `|` で綴じる。
set(HB_SJIS_TREES
    "gensoband/src|gensoband/src|cp932|.c,.h,.inl"
    "gensoband/adapter|gensoband/adapter|utf8|.c,.cpp,.h,.inl,.inc"
    "toband/src|toband/src|cp932|.c,.h,.inl"
    "toband/adapter|toband/adapter|utf8|.c,.cpp,.h,.inl,.inc"
    "silq/adapter|silq/adapter|utf8|.c,.cpp,.h,.inl,.inc"
    "frox/adapter|frox/adapter|utf8|.c,.cpp,.h,.inl,.inc"
)

#! 元の準備済みソース（`HB_TARGET_ORIGINAL_ROOT`）から直に写す木。
#! `HB_CORE_SOURCE_ROOT` は狙いの上書き（tools/targeting。`targeting-sources`）を指している。
#! TOband の上書き（`overlay.py` の `build_toband`）が出来ていれば、その `toband/src` を写す。
#! 出来ていない間（上書きが toband を扱わない版）は、元の `toband/src` を写す（狙いの計装は入らない）。
if(EXISTS "${HB_CORE_SOURCE_ROOT}/toband/src/angband.h")
    set(HB_SJIS_ORIGINAL_ROOT_TREES "")
else()
    set(HB_SJIS_ORIGINAL_ROOT_TREES "toband/src")
endif()

# 段取り前に 1 回走らせる。**glob より先でなければならない**（無い木は glob できない）。
foreach(spec ${HB_SJIS_TREES})
    string(REPLACE "|" ";" parts "${spec}")
    list(GET parts 0 from_dir)
    list(GET parts 1 to_dir)
    list(GET parts 2 from_enc)
    list(GET parts 3 exts)
    if(from_dir MATCHES "/src$")
        if(from_dir IN_LIST HB_SJIS_ORIGINAL_ROOT_TREES)
            set(from_path "${HB_TARGET_ORIGINAL_ROOT}/${from_dir}")
        else()
            set(from_path "${HB_CORE_SOURCE_ROOT}/${from_dir}")
        endif()
    else()
        set(from_path "${HB_ROOT}/${from_dir}")
    endif()
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${HB_SJIS_TOOL}"
                "--from" "${from_enc}" "--ext" "${exts}" "--quiet"
                "${from_path}" "${HB_SJIS_DIR}/${to_dir}"
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "変換ツリーを作れませんでした: ${from_dir}（rc=${rc}）")
    endif()
    file(GLOB_RECURSE HB_SJIS_INPUTS CONFIGURE_DEPENDS "${from_path}/*.c" "${from_path}/*.cpp" "${from_path}/*.h" "${from_path}/*.inl" "${from_path}/*.inc")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HB_SJIS_INPUTS})
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
    if(from_dir MATCHES "/src$")
        if(from_dir IN_LIST HB_SJIS_ORIGINAL_ROOT_TREES)
            set(from_path "${HB_TARGET_ORIGINAL_ROOT}/${from_dir}")
        else()
            set(from_path "${HB_CORE_SOURCE_ROOT}/${from_dir}")
        endif()
    else()
        set(from_path "${HB_ROOT}/${from_dir}")
    endif()
    list(APPEND HB_SJIS_COMMANDS
        COMMAND "${Python3_EXECUTABLE}" "${HB_SJIS_TOOL}"
                "--from" "${from_enc}" "--ext" "${exts}" "--quiet"
                "${from_path}" "${HB_SJIS_DIR}/${to_dir}")
endforeach()
add_custom_target(hb_transcode_sjis ALL
    ${HB_SJIS_COMMANDS}
    COMMENT "[hengband-hd2d] CP932 の変換ツリーを更新（幻想蛮怒・TOband・Sil-Q・Frox）"
    VERBATIM)

#! 線の符号化と入力の翻訳。**この 4 つだけ**が新旧で共有される（vcxproj と同じ並び）。
set(HB_LEGACY_SHARED_SRC
    "${HB_ROOT}/presentation/bridge/native_targeting_runtime.cpp"
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
        "${HB_CORE_SOURCE_ROOT}/src/external-lib/include"
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
list(FILTER HB_GB_SRC EXCLUDE REGEX "/store\\.c$")
file(GLOB HB_GB_ADAPTER_SRC CONFIGURE_DEPENDS
    "${HB_SJIS_DIR}/gensoband/adapter/*.c" "${HB_SJIS_DIR}/gensoband/adapter/*.cpp")
list(LENGTH HB_GB_SRC HB_GB_COUNT)
message(STATUS "[hengband-hd2d] gensoband sources = ${HB_GB_COUNT}")

add_library(gensocore SHARED ${HB_GB_SRC} ${HB_GB_ADAPTER_SRC} ${HB_LEGACY_SHARED_SRC})
hengband_legacy_config(gensocore
    "${HB_SJIS_DIR}/gensoband/src" "${HB_SJIS_DIR}/gensoband/adapter")
# vcxproj と同じ（`WINDOWS` と `WIN32` は当然こちらには無い）。
target_compile_definitions(gensocore PRIVATE JP SJIS)

# ---- TOband
#
# 原作は変愚蛮怒 1.x 系の平屋の C で、幻想蛮怒と同じ組み方をする。原作ソースは書き換えず、
# 組み立ての指定だけで吸う（設計 §1 制約 1）。正本は VisualStudio/TobandCore/TobandCore.vcxproj
# と platform/linux/CMakeLists.txt の TobandCore。
#   - `main*.c`・`maid-x11.c` は入口と X11/DIB なので組まない
#   - `report.c` は作者のサーバーへスコアを送る（設計 §1 制約 5）ので組まず、
#     アダプタの `tb_report_stub.c` が代わりを持つ
#   - `bldg.c` は `party.h` を読まずに party 系の関数を呼ぶので、この 1 本にだけ
#     `angband.h` と `party.h` を差し込む（vcxproj の ForcedIncludeFiles と同じ）
file(GLOB HB_TB_SRC CONFIGURE_DEPENDS "${HB_SJIS_DIR}/toband/src/*.c")
set(HB_TB_EXCLUDE
    ${HB_LEGACY_EXCLUDE}
    "/main-[a-z]+\.c$"
    "/report\.c$"
)
foreach(pat ${HB_TB_EXCLUDE})
    list(FILTER HB_TB_SRC EXCLUDE REGEX "${pat}")
endforeach()
#! `store.c` は単独では組まない。アダプタの `tb_native_store.c` が丸ごと取り込む（幻想蛮怒の `gb_native_store.c` と同じ）
list(FILTER HB_TB_SRC EXCLUDE REGEX "/store\\.c$")
#! `util.c`・`birth.c` も単独では組まない。アダプタの `tb_native_util.c`・`tb_native_birth.c` が丸ごと取り込む
#! （新しい画面へ文字入力の待ちを知らせる旗を立てるため。原作は書き換えない）
list(FILTER HB_TB_SRC EXCLUDE REGEX "/util\\.c$")
list(FILTER HB_TB_SRC EXCLUDE REGEX "/birth\\.c$")
file(GLOB HB_TB_ADAPTER_SRC CONFIGURE_DEPENDS
    "${HB_SJIS_DIR}/toband/adapter/*.c" "${HB_SJIS_DIR}/toband/adapter/*.cpp")
list(LENGTH HB_TB_SRC HB_TB_COUNT)
message(STATUS "[hengband-hd2d] toband sources = ${HB_TB_COUNT}")

add_library(tobandcore SHARED ${HB_TB_SRC} ${HB_TB_ADAPTER_SRC} ${HB_LEGACY_SHARED_SRC})
hengband_legacy_config(tobandcore
    "${HB_SJIS_DIR}/toband/src" "${HB_SJIS_DIR}/toband/adapter")
target_compile_definitions(tobandcore PRIVATE JP SJIS)
# 64 ビットの罠: 原作の h-type.h は Alpha でしか L64 を立てず、LP64（arm64-v8a・x86_64）では
# `u32b` が 64 ビットの unsigned long になる。幻想蛮怒は stdint の型を使うので要らないが、
# TOband は定義で同じ結果にする（Linux の TobandCore と同じ。設計 §9 の M3）。
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(tobandcore PRIVATE L64)
endif()
set_source_files_properties("${HB_SJIS_DIR}/toband/src/bldg.c"
    PROPERTIES COMPILE_OPTIONS "-include;angband.h;-include;party.h")

# ---- Sil-Q
#
# `silq/src` は**影に写さない**——中身は ASCII で、非 ASCII は註釈の UTF-8 だけである
# （2026-08-21 に全数で確かめた）。写す必要が無いものを写すと、追う先が増えるだけ。
file(GLOB HB_SQ_SRC CONFIGURE_DEPENDS "${HB_CORE_SOURCE_ROOT}/silq/src/*.c")
foreach(pat ${HB_LEGACY_EXCLUDE})
    list(FILTER HB_SQ_SRC EXCLUDE REGEX "${pat}")
endforeach()
list(FILTER HB_SQ_SRC EXCLUDE REGEX "/cmd3\\.c$")
file(GLOB HB_SQ_ADAPTER_SRC CONFIGURE_DEPENDS
    "${HB_SJIS_DIR}/silq/adapter/*.c" "${HB_SJIS_DIR}/silq/adapter/*.cpp")
list(LENGTH HB_SQ_SRC HB_SQ_COUNT)
message(STATUS "[hengband-hd2d] silq sources = ${HB_SQ_COUNT}")

add_library(silcore SHARED ${HB_SQ_SRC} ${HB_SQ_ADAPTER_SRC} ${HB_LEGACY_SHARED_SRC})
hengband_legacy_config(silcore "${HB_CORE_SOURCE_ROOT}/silq/src" "${HB_SJIS_DIR}/silq/adapter")

# ---- FroxComposband
#
# 原作の英語ソースはそのまま使い、日本語を含むアダプタはCP932の通常リテラルを変換する。
# Unicode接頭辞付きリテラルはUnicodeのまま維持する。
#
# MSVC の穴 2 つ（設計 §3.7）はどちらもこちらでは要らない:
#   - `z-doc.c` の `/FIwindows.h` … windows.h が無いので衝突も無い
#   - `fc_compat.c`（strncasecmp -> _strnicmp）… bionic には strncasecmp が
#     最初から在り、逆に _strnicmp が無いのでリンクで落ちる。**外す**
file(GLOB HB_FC_SRC CONFIGURE_DEPENDS "${HB_CORE_SOURCE_ROOT}/frox/src/*.c")
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
list(FILTER HB_FC_SRC EXCLUDE REGEX "/(shop|home|util)\\.c$")
file(GLOB HB_FC_ADAPTER_SRC CONFIGURE_DEPENDS
    "${HB_SJIS_DIR}/frox/adapter/*.c" "${HB_SJIS_DIR}/frox/adapter/*.cpp")
list(FILTER HB_FC_ADAPTER_SRC EXCLUDE REGEX "/fc_compat\\.c$")
list(LENGTH HB_FC_SRC HB_FC_COUNT)
message(STATUS "[hengband-hd2d] frox sources = ${HB_FC_COUNT}")

# doc UI の旗（M0.5 FH-02。設計 §14.2(a)）: vcxproj のファイル単位 define と同じ
# 6 TU にだけ `Term_save/Term_load` の横取りを差す。
set_source_files_properties(
    "${HB_CORE_SOURCE_ROOT}/frox/src/monspell.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/obj_prompt.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/skillmaster.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/weaponsmith.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/wild.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/wiz_obj.c"
    PROPERTIES COMPILE_DEFINITIONS "Term_save=fc_ui_term_save;Term_load=fc_ui_term_load")

# 選択メニューの全件（原作の menu_choose）: vcxproj のファイル単位 define と同じ 12 TU にだけ、
# 呼び先を `fc_menu_choose`（`frox/adapter/fc_shim.c`）へ替える。上の 6 TU とは重ならない
# （重なるようになったら、`COMPILE_DEFINITIONS` を 1 つの値にまとめる——この命令は値を上書きする）。
set_source_files_properties(
    "${HB_CORE_SOURCE_ROOT}/frox/src/bldg.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/equip.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/mut.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/psion.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/r_dragon.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/r_mummy.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/r_ring.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/ragemage.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/rune_kt.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/samurai.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/wild_talent.c"
    "${HB_CORE_SOURCE_ROOT}/frox/src/yeqrezh.c"
    PROPERTIES COMPILE_DEFINITIONS "menu_choose=fc_menu_choose")

# 共有 TU は FroxCore.vcxproj と同じ並び。**ただし `cp932.cpp` はここだけ足す**
# ——Windows の `fc_text.cpp` は `MultiByteToWideChar` を直に呼ぶが、Android には
# あれが無く、`platform/portable/cp932.cpp` の表引きに落ちるためである
# （M0 のあいだは「Frox は CP932 の世界を持たない」ので入れていなかった。
# M1 の日本語層で要るようになった。2026-08-24）。
add_library(froxcore SHARED ${HB_FC_SRC} ${HB_FC_ADAPTER_SRC}
    "${HB_ROOT}/presentation/bridge/native_targeting_runtime.cpp"
    "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp"
    "${HB_ROOT}/presentation/frame/frame_codec.cpp"
    "${HB_ROOT}/presentation/frame/protocol_messages.cpp"
    "${HB_ROOT}/presentation/frame/protocol_transport.cpp"
    "${HB_ROOT}/platform/portable/legacy_os.cpp"
    "${HB_ROOT}/platform/portable/cp932.cpp"
)
hengband_legacy_config(froxcore "${HB_CORE_SOURCE_ROOT}/frox/src" "${HB_SJIS_DIR}/frox/adapter")

# ---- RogueClone2 -------------------------------------------------------------
set(HB_R2_SOURCE_ROOT "${HB_TARGET_GENERATED_ROOT}")
set(HB_R2_SRC_DIR "${HB_R2_SOURCE_ROOT}/rogue2/patched-src")
file(GLOB HB_R2_SRC CONFIGURE_DEPENDS "${HB_R2_SRC_DIR}/*.c")
list(FILTER HB_R2_SRC EXCLUDE REGEX "/main\\.c$")
set(HB_R2_MAIN "${HB_R2_SRC_DIR}/main.c")
set_source_files_properties(${HB_R2_MAIN} PROPERTIES COMPILE_DEFINITIONS "main=r2_game_main")
set(HB_R2_ADAPTER_C
    "${HB_ROOT}/rogue2/adapter/r2_curses.c"
    "${HB_ROOT}/rogue2/adapter/r2_compat_iconv.c"
    "${HB_ROOT}/rogue2/adapter/r2_persist.c"
    "${HB_ROOT}/rogue2/adapter/r2_shim.c"
)
set(HB_R2_ADAPTER_CPP
    "${HB_ROOT}/rogue2/adapter/r2_main.cpp"
    "${HB_ROOT}/rogue2/adapter/r2_frame.cpp"
    "${HB_ROOT}/presentation/frame/frame_codec.cpp"
    "${HB_ROOT}/presentation/frame/protocol_messages.cpp"
    "${HB_ROOT}/presentation/frame/protocol_transport.cpp"
    "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp"
    "${HB_ROOT}/platform/portable/legacy_os.cpp"
)
add_library(rogue2core SHARED ${HB_R2_SRC} ${HB_R2_MAIN} ${HB_R2_ADAPTER_C} ${HB_R2_ADAPTER_CPP})
target_include_directories(rogue2core PRIVATE
    "${HB_ROOT}/rogue2/adapter/compat"
    "${HB_ROOT}/rogue2/adapter"
    "${HB_R2_SRC_DIR}"
    "${HB_ROOT}/third_party/cpp/include"
    "${HB_ROOT}/presentation"
    "${HB_ROOT}/platform"
)
target_compile_definitions(rogue2core PRIVATE JAPAN TOPSCO HENGBAND_ANDROID=1)
target_compile_options(rogue2core PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include;${HB_ROOT}/rogue2/adapter/compat/r2_build_config.h>
    -Wno-everything -ffunction-sections -fdata-sections)
target_link_options(rogue2core PRIVATE -Wl,--gc-sections)
target_link_libraries(rogue2core PRIVATE log)

# ---- Rogue 3.x / 5.x ---------------------------------------------------------
function(hengband_rogue_legacy target source_dir generation entry wrapper forced_config)
    hb_rogue_legacy_collect(legacy_all "${source_dir}" "${generation}")
    set(legacy_src ${legacy_all})
    list(FILTER legacy_src EXCLUDE REGEX "/main\\.c$")
    set(legacy_main "${source_dir}/main.c")
    set_source_files_properties(${legacy_main} PROPERTIES COMPILE_DEFINITIONS "main=${entry}")
    add_library(${target} SHARED
        ${legacy_src} ${legacy_main}
        "${HB_ROOT}/rogue_legacy/adapter/rl_curses.c"
        "${HB_ROOT}/rogue_legacy/adapter/rl_i18n.c"
        "${HB_ROOT}/rogue_legacy/adapter/rl_exit.c"
        "${HB_ROOT}/rogue_legacy/adapter/rl_inventory.c"
        "${HB_ROOT}/rogue_legacy/adapter/rl_sound.c"
        "${wrapper}"
        "${HB_ROOT}/rogue_legacy/adapter/rl_frame.cpp"
        "${HB_ROOT}/presentation/frame/frame_codec.cpp"
        "${HB_ROOT}/presentation/frame/protocol_messages.cpp"
        "${HB_ROOT}/presentation/frame/protocol_transport.cpp"
        "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp"
        "${HB_ROOT}/platform/portable/legacy_os.cpp")
    target_include_directories(${target} PRIVATE
        "${HB_ROOT}/rogue_legacy/adapter/compat"
        "${HB_ROOT}/rogue_legacy/adapter"
        "${source_dir}"
        "${HB_ROOT}/third_party/cpp/include"
        "${HB_ROOT}/presentation"
        "${HB_ROOT}/platform")
    target_compile_definitions(${target} PRIVATE HENGBAND_ANDROID=1 RL_GENERATION=${generation})
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-include;${forced_config}>
        -Wno-everything -ffunction-sections -fdata-sections)
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
    target_link_libraries(${target} PRIVATE log)
endfunction()

hengband_rogue_legacy(rogue36core "${HB_TARGET_GENERATED_ROOT}/rogue36/patched-src" 36 r36_game_main
    "${HB_ROOT}/rogue36/adapter/r36_main.cpp" "${HB_ROOT}/rogue_legacy/adapter/compat/r36_build_config.h")
hengband_rogue_legacy(rogue54core "${HB_TARGET_GENERATED_ROOT}/rogue54/patched-src" 54 r54_game_main
    "${HB_ROOT}/rogue54/adapter/r54_main.cpp" "${HB_ROOT}/rogue_legacy/adapter/compat/r54_build_config.h")

# ---- Angband 4.2.6---------------------------------
#
# `tools/angband/build_windows.py` の Windows 版と同じ並び（clang / LLVM-MinGW で
# 組んでいて MSVC の CP932 実行文字集合を経由しないため、gensoband/silq/frox と違って
# CP932 の変換ツリーは要らない。アダプタの日本語は註釈だけで、送る文字列はすべて英語
# ＝設計 §3.6 相当。ASCII 前提は Frox と同じ）。
# 149 本（`main-*.c` / `snd-*.c` / `wiz-stats.c` を除く。`main.c` も除く——起動列は
# `an_bootstrap.c` が写す）。
set(HB_AN_CORE_FILES
    buildid.c cave-map.c cave-square.c cave-view.c cave.c cmd-cave.c cmd-core.c cmd-misc.c
    cmd-obj.c cmd-pickup.c cmd-spoil.c cmd-wizard.c datafile.c debug.c effect-handler-attack.c effect-handler-general.c
    effects-info.c effects.c game-event.c game-input.c game-world.c gen-cave.c gen-chunk.c gen-monster.c
    gen-room.c gen-util.c generate.c grafmode.c guid.c init.c load.c message.c
    mon-attack.c mon-blows.c mon-desc.c mon-group.c mon-init.c mon-list.c mon-lore.c mon-make.c
    mon-move.c mon-msg.c mon-predicate.c mon-spell.c mon-summon.c mon-timed.c mon-util.c obj-chest.c
    obj-curse.c obj-desc.c obj-gear.c obj-ignore.c obj-info.c obj-init.c obj-knowledge.c obj-list.c
    obj-make.c obj-pile.c obj-power.c obj-properties.c obj-randart.c obj-slays.c obj-tval.c obj-util.c
    option.c parser.c player-attack.c player-birth.c player-calcs.c player-class.c player-history.c player-path.c
    player-properties.c player-quest.c player-race.c player-spell.c player-timed.c player-util.c player.c project-feat.c
    project-mon.c project-obj.c project-player.c project.c randname.c save-charoutput.c save.c savefile.c
    score-util.c score.c sound-core.c source.c store.c target.c trap.c ui-birth.c
    ui-command.c ui-context.c ui-curse.c ui-death.c ui-display.c ui-effect.c ui-entry-combiner.c ui-entry-renderers.c
    ui-entry.c ui-equip-cmp.c ui-event.c ui-game.c ui-help.c ui-history.c ui-init.c ui-input.c
    ui-keymap.c ui-knowledge.c ui-map.c ui-menu.c ui-mon-list.c ui-mon-lore.c ui-obj-list.c ui-object.c
    ui-options.c ui-output.c ui-player-properties.c ui-player.c ui-prefs.c ui-score.c ui-signals.c ui-spell.c
    ui-spoil.c ui-store.c ui-target.c ui-term.c ui-visuals.c ui-wizard.c wiz-debug.c wiz-spoil.c
    z-bitflag.c z-color.c z-dice.c z-expression.c z-file.c z-form.c z-quark.c z-queue.c
    z-rand.c z-textblock.c z-type.c z-util.c z-virt.c
)
list(LENGTH HB_AN_CORE_FILES HB_AN_CORE_COUNT)
if(NOT HB_AN_CORE_COUNT EQUAL 149)
    message(FATAL_ERROR "[hengband-hd2d] angband: expected 149 core .c files, got ${HB_AN_CORE_COUNT}")
endif()

#! **`HB_CORE_SOURCE_ROOT` ではなく `HB_TARGET_ORIGINAL_ROOT` を使う。**
#! `tools/targeting/targeting_sources.cmake` が `HB_CORE_SOURCE_ROOT` を
#! `targeting-sources`（オーバーレイ生成木。src/frox/gensoband/silq だけコピーする）へ
#! 差し替えている——`angband/patched-src` はそこに無い。Rogue 3.6/5.4 と同じ理由で
#! 元の準備済みソース（`HB_TARGET_ORIGINAL_ROOT`）を直接見る。
set(HB_AN_SRC_DIR "${HB_TARGET_ORIGINAL_ROOT}/angband/patched-src")
set(HB_AN_ADAPTER_DIR "${HB_ROOT}/angband/adapter")

set(HB_AN_ADAPTER_C_FILES
    an_bootstrap.c an_null_term.c an_wiz_stats_stub.c an_map_shim.c
    an_native_inventory.c an_text_hooks.c an_pad_shim.c an_ui_shim.c
    an_targeting.c an_town_rng.c an_town_map.c an_profile.c an_fx.c
    an_test_monster_act.c an_wide.c an_name_ja.c an_lore_ja.c)

set(HB_AN_CORE_SRC)
foreach(name IN LISTS HB_AN_CORE_FILES)
    list(APPEND HB_AN_CORE_SRC "${HB_AN_SRC_DIR}/${name}")
endforeach()
# 原作側だけ警告を黙らせる（設計 §3「アダプタ側は警告を黙らせない」。Windows 版と同じ切り分け）。
set_source_files_properties(${HB_AN_CORE_SRC} PROPERTIES COMPILE_OPTIONS "-w")

set(HB_AN_ADAPTER_SRC)
foreach(name IN LISTS HB_AN_ADAPTER_C_FILES)
    list(APPEND HB_AN_ADAPTER_SRC "${HB_AN_ADAPTER_DIR}/${name}")
endforeach()

add_library(angbandcore SHARED
    ${HB_AN_CORE_SRC}
    ${HB_AN_ADAPTER_SRC}
    "${HB_AN_ADAPTER_DIR}/an_main.cpp"
    "${HB_AN_ADAPTER_DIR}/an_frame.cpp"
    "${HB_AN_ADAPTER_DIR}/an_manifest.cpp"
    "${HB_AN_ADAPTER_DIR}/an_lang.cpp"
    "${HB_ROOT}/presentation/frame/frame_codec.cpp"
    "${HB_ROOT}/presentation/frame/protocol_messages.cpp"
    "${HB_ROOT}/presentation/frame/protocol_transport.cpp"
    "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp"
    "${HB_ROOT}/presentation/bridge/native_targeting_runtime.cpp"
    "${HB_ROOT}/platform/portable/legacy_os.cpp"
)
target_include_directories(angbandcore PRIVATE
    "${HB_AN_ADAPTER_DIR}"
    "${HB_AN_SRC_DIR}"
    "${HB_ROOT}/presentation"
    "${HB_ROOT}/platform"
    "${HB_ROOT}/third_party/cpp/include"
)
target_compile_definitions(angbandcore PRIVATE HENGBAND_ANDROID=1)
target_compile_options(angbandcore PRIVATE
    # 20 年もの、というほどではないが、C99 の緩い書き方が残っている（build_windows.py も
    # アダプタは通常の警告のままで組んでいる。ここでは link を落とさないための最小限だけ緩める）。
    $<$<COMPILE_LANGUAGE:C>:-fcommon>
    -ffunction-sections -fdata-sections
)
target_link_options(angbandcore PRIVATE -Wl,--gc-sections)
target_link_libraries(angbandcore PRIVATE log)

# ---- Umoria 5.7------------------------------------
#
# `tools/umoria/build_windows.py` と同じ並び: 原作の .cpp のうち curses の端末 `ui_io.cpp` を除く全部
# （`umoria/adapter/um_io.cpp` が代わる）。原作の `main()` は `um_game_main` へ改名する。
# 原作は C++14・警告を黙らせる。攻撃の効果の観測の呼び出しを足した写し（`HB_TARGET_GENERATED_ROOT`。tools/targeting/native_fx_overlay.py）を組む。
# **原作の `exit()` は `um_game_exit` へ差し替える**——1 プロセスに同居するので、そのままだと
# セーブして終えた瞬間にアプリごと終わる（`um_main.cpp` が例外にしてコアのスレッドの入口へ戻す）。
set(HB_UM_SRC_DIR "${HB_TARGET_GENERATED_ROOT}/umoria/patched-src")
file(GLOB HB_UM_CORE_SRC CONFIGURE_DEPENDS "${HB_UM_SRC_DIR}/*.cpp")
list(FILTER HB_UM_CORE_SRC EXCLUDE REGEX "/ui_io\.cpp$")
set_source_files_properties(${HB_UM_CORE_SRC} PROPERTIES
    COMPILE_OPTIONS "-std=c++14;-w"
    COMPILE_DEFINITIONS "exit=um_game_exit")
set_source_files_properties("${HB_UM_SRC_DIR}/main.cpp" PROPERTIES
    COMPILE_DEFINITIONS "exit=um_game_exit;main=um_game_main")
add_library(umoriacore SHARED
    ${HB_UM_CORE_SRC}
    "${HB_ROOT}/umoria/adapter/um_io.cpp"
    "${HB_ROOT}/umoria/adapter/um_main.cpp"
    "${HB_ROOT}/umoria/adapter/um_frame.cpp"
    "${HB_ROOT}/umoria/adapter/um_town.cpp"
    "${HB_ROOT}/umoria/adapter/um_lang.cpp"
    "${HB_ROOT}/umoria/adapter/um_recall_ja.cpp"
    "${HB_ROOT}/presentation/frame/frame_codec.cpp"
    "${HB_ROOT}/presentation/frame/protocol_messages.cpp"
    "${HB_ROOT}/presentation/frame/protocol_transport.cpp"
    "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp"
    "${HB_ROOT}/platform/portable/legacy_os.cpp"
)
target_include_directories(umoriacore PRIVATE
    "${HB_ROOT}/umoria/adapter"
    "${HB_UM_SRC_DIR}"
    "${HB_ROOT}/presentation"
    "${HB_ROOT}/platform"
    "${HB_ROOT}/third_party/cpp/include"
)
target_compile_definitions(umoriacore PRIVATE HENGBAND_ANDROID=1)
target_compile_options(umoriacore PRIVATE -ffunction-sections -fdata-sections)
target_link_options(umoriacore PRIVATE -Wl,--gc-sections)
target_link_libraries(umoriacore PRIVATE log)

endif() # HENGBAND_BUILD_CORES

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
