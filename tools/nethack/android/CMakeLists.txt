# NetHack のコア（英語版 NetHackCore・日本語版 NetHackJpCore）を Android の NDK で実行ファイル（PIE）として組む。
# tools/nethack/build_android.py が ABI ごとに呼ぶ（手で叩かない）。
#
# Android では NetHack を画面とは別のプロセスで動かす（決定 D5）。画面のアプリ（libmain.so）が、APK の jniLibs に
# lib*.so の名前で入れたこの実行ファイルを posix_spawn で起こし、標準入出力でプロトコル v1 を話す（Windows・Linux と同じ形）。
# 作りは platform/linux/CMakeLists.txt の hb_nethack_core() のコアの部分と同じ。違いは次のとおり。
#   - 実行時のデータ（makedefs の出力・sysconf・タイルの絵）はここでは作らない（build_android.py が WSL で作る）。
#   - COMPRESS を付けない（Android に /bin/gzip が無い。原作の unixconf.h は COMPRESS が無ければ圧縮しない）。
#   - C++ の標準ライブラリは静的に結ぶ（ANDROID_STL=c++_static。起こした子はアプリの検索経路を持たないため）。
#   - 16 KB のページの端末でも読めるよう、max-page-size を 16384 にする。
#   - クラッシュの報告と異常時の呼び出し履歴（原作の CRASHREPORT・PANICTRACE）を外す（NOCRASHREPORT・NOPANICTRACE。原作の設定の切り替え）。
#     report.c が execinfo.h を読まずに backtrace を呼び、NDK の clang では組めないため。
#   - SELF_RECOVER を付ける（Windows 版と同じ。原作の windconf.h が付け、unixconf.h は切っている）。Android ではアプリを払って閉じると
#     コアも保存の前に止まり、ロックと途中の階のファイルが残る。Unix 版はそれを外部の recover で直す前提だが、Android にその道具は無い。
#     付けると、次に同じ名前で始めたときに原作が「消す・復元する・やめる」を聞く（sys/unix/unixunix.c の getlock）。
# 原作（HB_NH_SOURCES/nethack・nethackjp）は読むだけで、何も書き込まない。
cmake_minimum_required(VERSION 3.22)
project(HengbandNetHackAndroid C CXX)

if(NOT DEFINED HB_ROOT OR NOT DEFINED HB_NH_SOURCES)
    message(FATAL_ERROR "HB_ROOT と HB_NH_SOURCES を渡すこと（tools/nethack/build_android.py が渡す）")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(HB_NH_ADAPTER "${HB_ROOT}/nethack/adapter")

function(hb_nethack_android_core target source_name is_japanese)
    set(src "${HB_NH_SOURCES}/${source_name}")
    if(NOT EXISTS "${src}/src/allmain.c")
        message(FATAL_ERROR "${src} is not prepared. Run tools/core_sources/prepare.py")
    endif()
    set(game_defs SHIM_GRAPHICS NOTTYGRAPHICS NOSHELL NOMAIL NOCRASHREPORT NOPANICTRACE SELF_RECOVER SIG_RET_TYPE=__sighandler_t)
    # 1. 原作のゲームの C（とアダプタの C）。Linux と同じく src/*.c を全部と、Unix の部品 3 本
    file(GLOB game_c CONFIGURE_DEPENDS "${src}/src/*.c")
    list(APPEND game_c "${src}/sys/unix/unixunix.c" "${src}/sys/unix/unixres.c" "${src}/sys/share/posixregex.c")
    set_source_files_properties(${game_c} PROPERTIES COMPILE_OPTIONS "-w")
    add_library(${target}_game OBJECT ${game_c} "${HB_NH_ADAPTER}/nh_winport.c" "${HB_NH_ADAPTER}/nh_unix.c")
    target_include_directories(${target}_game PRIVATE "${src}/include" "${src}/nhlua/lua/src" "${HB_NH_ADAPTER}")
    # NOCWD_ASSUMPTIONS: 読むデータ（nethack/data/<lang>）と書く置き場を別にする（Linux と同じ。platform/linux/CMakeLists.txt の註）
    target_compile_definitions(${target}_game PRIVATE ${game_defs} NOCWD_ASSUMPTIONS)
    # ANDROID は NDK が付ける定義。日本語版の原作は ANDROID を「原作の Flutter の画面で組む」の意味に使う（src/windows.c の
    # flutter_putmixed_with_tile・dump_open_log）。ここでは Linux と同じ Unix の形で組むので、原作のゲームの C では外す
    # （コンパイラーが付ける __ANDROID__ は残るので、Android の C ライブラリのヘッダは変わらない）。
    target_compile_options(${target}_game PRIVATE -UANDROID -ffunction-sections -fdata-sections)
    # 2. Lua（lua.c・luac.c は単独のプログラムなので外す）
    file(GLOB lua_c CONFIGURE_DEPENDS "${src}/nhlua/lua/src/*.c")
    list(FILTER lua_c EXCLUDE REGEX "/(lua|luac)\\.c$")
    add_library(${target}_lua OBJECT ${lua_c})
    target_include_directories(${target}_lua PRIVATE "${src}/nhlua/lua/src")
    target_compile_definitions(${target}_lua PRIVATE LUA_USE_LINUX)
    target_compile_options(${target}_lua PRIVATE -w -ffunction-sections -fdata-sections)
    # 3. コア: プロトコルの層（C++）と上の 2 つ
    add_executable(${target}
        "${HB_NH_ADAPTER}/nh_main.cpp"
        "${HB_NH_ADAPTER}/nh_frame.cpp"
        "${HB_NH_ADAPTER}/nh_terrain.cpp"
        "${HB_ROOT}/presentation/frame/frame_codec.cpp"
        "${HB_ROOT}/presentation/frame/protocol_messages.cpp"
        "${HB_ROOT}/presentation/frame/protocol_transport.cpp"
        "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp"
        "${HB_ROOT}/platform/portable/legacy_os.cpp"
        $<TARGET_OBJECTS:${target}_game> $<TARGET_OBJECTS:${target}_lua>)
    target_include_directories(${target} PRIVATE "${HB_NH_ADAPTER}" "${HB_ROOT}/hd2d" "${HB_ROOT}/presentation"
        "${HB_ROOT}/platform" "${HB_ROOT}/third_party/cpp/include")
    if(is_japanese)
        target_compile_definitions(${target} PRIVATE NH_LANG_JA)
    endif()
    target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(${target} PRIVATE -Wl,--gc-sections -Wl,-z,max-page-size=16384)
    target_link_libraries(${target} PRIVATE m log)
endfunction()

hb_nethack_android_core(NetHackCore nethack FALSE)
hb_nethack_android_core(NetHackJpCore nethackjp TRUE)
