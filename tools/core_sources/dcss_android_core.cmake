# DCSS（日本語版）のコア `libdcsscore.so`（Android・Meta Quest）。
# android/hd2d/src/main/cpp/CMakeLists.txt の include(…/dcss_android_core.cmake) から読まれる。
#
# Linux の tools/core_sources/dcss_core.cmake と同じソースの一覧・定義で組むが、次の 3 点が違う。
#   1. 生成物（ヘッダ・タイルの表）は **組む前に Windows のホストで作ってある**ものを読む。Android の CMake は
#      ホストで走らせる道具（原作の tilegen）を組めないため。作るのは tools/dcss/prepare_android_gen.py
#      （config.h は NDK の clang で確認した bionic 用）。置き場は <生成物の根>/gen。
#   2. 実行ファイルではなく共有ライブラリ。入口は dcss/adapter/dcss_main.cpp の hengband_core_entry
#      （画面と同じプロセスの別スレッドで呼ばれる。platform/android/hd2d_entry_android.cpp）。
#   3. 原作の C++ は **-U__ANDROID__ で組む**。NDK の clang は __ANDROID__ を必ず定義するが、原作の
#      `#ifdef __ANDROID__` は「原作自身の Android 版（SDL2 + AAssetManager + JNI）」の枝で、こちらの
#      構成（ウェブタイル版・標準入出力の代わりのパイプ）には無い関数を引く（Android_JNI_GetEnv・
#      SDL_AndroidGetExternalStoragePath など）。原作は書き換えないので、定義の側で外す。
#
# 原作は prepare.py が作る "${HB_TARGET_ORIGINAL_ROOT}/dcss" を読む。書き換えない。
# 生成物が無いとき（prepare_android_gen.py を走らせていない）は、DCSS だけ組まずに先へ進む
# （ほかのコアの組み立てを止めない）。配布の道具（Build-Hd2dApk.ps1）は先にそれを走らせる。

set(HB_DCSS_ANDROID_WORK "${HB_ROOT}/../roguelike-cores/dcss-android" CACHE PATH "tools/dcss/prepare_android_gen.py の作業フォルダ")
if(DEFINED ENV{DCSS_ANDROID_WORK})
    set(HB_DCSS_ANDROID_WORK "$ENV{DCSS_ANDROID_WORK}")
endif()
get_filename_component(HB_DCSS_ANDROID_WORK "${HB_DCSS_ANDROID_WORK}" ABSOLUTE)
set(HB_DCSS_GEN "${HB_DCSS_ANDROID_WORK}/gen")
set(HB_DCSS_ROOT "${HB_TARGET_ORIGINAL_ROOT}/dcss")
set(HB_DCSS_SRC "${HB_DCSS_ROOT}/crawl-ref/source")
set(HB_DCSS_ADAPTER "${HB_ROOT}/dcss/adapter")

if(NOT EXISTS "${HB_DCSS_SRC}/AppHdr.cc" OR NOT EXISTS "${HB_DCSS_GEN}/mon-data.h" OR NOT EXISTS "${HB_DCSS_GEN}/rltiles/tiledef-main.cc")
    message(STATUS "[hengband-hd2d] dcss: 原作または生成物（${HB_DCSS_GEN}）が無いので libdcsscore.so は組みません。python tools/dcss/prepare_android_gen.py を先に走らせてください")
    return()
endif()

# --- 原作のソース一覧（Makefile.obj の 3 つの変数。dcss_core.cmake と同じ規則） ---
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${HB_DCSS_SRC}/Makefile.obj")
file(READ "${HB_DCSS_SRC}/Makefile.obj" HB_DCSS_MK)
string(REPLACE "\\\n" " " HB_DCSS_MK "${HB_DCSS_MK}")
set(HB_DCSS_CC "")
foreach(var OBJECTS TILES_OBJECTS WEBTILES_OBJECTS)
    string(REGEX MATCH "(^|\n)${var} = [^\n]*" HB_DCSS_MK_LINE "${HB_DCSS_MK}")
    string(REGEX MATCHALL "[A-Za-z0-9_./-]+\\.o" HB_DCSS_MK_OBJS "${HB_DCSS_MK_LINE}")
    foreach(obj IN LISTS HB_DCSS_MK_OBJS)
        string(REGEX REPLACE "\\.o$" ".cc" obj "${obj}")
        list(APPEND HB_DCSS_CC "${HB_DCSS_SRC}/${obj}")
    endforeach()
endforeach()
list(LENGTH HB_DCSS_CC HB_DCSS_CC_COUNT)
if(HB_DCSS_CC_COUNT LESS 270)
    message(FATAL_ERROR "Makefile.obj から読めた原作のソースが少なすぎます (${HB_DCSS_CC_COUNT})")
endif()
list(APPEND HB_DCSS_CC "${HB_DCSS_SRC}/main.cc" "${HB_DCSS_SRC}/version.cc"
    "${HB_DCSS_SRC}/prebuilt/levcomp.lex.cc" "${HB_DCSS_SRC}/prebuilt/levcomp.tab.cc")
message(STATUS "[hengband-hd2d] dcss sources = ${HB_DCSS_CC_COUNT} (+4)")

set(HB_DCSS_TILEDEF_CC "")
foreach(name main dngn floor wall feat player gui icons unrand)
    list(APPEND HB_DCSS_TILEDEF_CC "${HB_DCSS_GEN}/rltiles/tiledef-${name}.cc")
endforeach()

# --- 付属ライブラリ（C）。lua・sqlite・pcre・zlib を 1 つの静的ライブラリに ---
file(GLOB HB_DCSS_LUA CONFIGURE_DEPENDS "${HB_DCSS_SRC}/contrib/lua/src/*.c")
list(FILTER HB_DCSS_LUA EXCLUDE REGEX "/(lua|luac)\\.c$")
set(HB_DCSS_ZLIB "")
foreach(name adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil)
    list(APPEND HB_DCSS_ZLIB "${HB_DCSS_SRC}/contrib/zlib/${name}.c")
endforeach()
set(HB_DCSS_PCRE "")
foreach(name chartables compile config dfa_exec exec fullinfo get globals maketables newline ord2utf8 refcount study tables ucd valid_utf8 version xclass)
    list(APPEND HB_DCSS_PCRE "${HB_DCSS_SRC}/contrib/pcre/pcre_${name}.c")
endforeach()
add_library(dcss_contrib STATIC ${HB_DCSS_LUA} "${HB_DCSS_SRC}/contrib/sqlite/sqlite3.c" ${HB_DCSS_ZLIB} ${HB_DCSS_PCRE})
target_compile_options(dcss_contrib PRIVATE -w -fPIC)
target_include_directories(dcss_contrib PRIVATE
    "${HB_DCSS_SRC}/contrib/lua/src" "${HB_DCSS_SRC}/contrib/sqlite" "${HB_DCSS_SRC}/contrib/pcre" "${HB_DCSS_SRC}/contrib/zlib")
target_compile_definitions(dcss_contrib PRIVATE HAVE_CONFIG_H PCRE_STATIC LUA_USE_POSIX
    SQLITE_OMIT_AUTHORIZATION SQLITE_OMIT_AUTOVACUUM SQLITE_OMIT_COMPLETE SQLITE_OMIT_BLOB_LITERAL SQLITE_OMIT_COMPOUND_SELECT
    SQLITE_OMIT_CONFLICT_CLAUSE SQLITE_OMIT_DATETIME_FUNCS SQLITE_OMIT_EXPLAIN SQLITE_OMIT_INTEGRITY_CHECK SQLITE_OMIT_PAGER_PRAGMAS
    SQLITE_OMIT_PROGRESS_CALLBACK SQLITE_OMIT_SCHEMA_PRAGMAS SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS SQLITE_OMIT_TCL_VARIABLE
    SQLITE_OMIT_LOAD_EXTENSION)
set_target_properties(dcss_contrib PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

# --- 原作の C++ と端末の層・ソケットの差し替え（原作のヘッダを引く TU はすべてここ。C++14・警告は黙らせる） ---
add_library(dcss_original OBJECT ${HB_DCSS_CC} ${HB_DCSS_TILEDEF_CC}
    "${HB_DCSS_ADAPTER}/libhd2d.cpp" "${HB_DCSS_ADAPTER}/dcss_posix_shim.cpp")
set_target_properties(dcss_original PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
target_compile_options(dcss_original PRIVATE -w -U__ANDROID__)
target_include_directories(dcss_original PRIVATE
    "${HB_DCSS_GEN}" "${HB_DCSS_GEN}/rltiles" "${HB_DCSS_ADAPTER}" "${HB_DCSS_SRC}" "${HB_DCSS_SRC}/rltiles"
    "${HB_DCSS_SRC}/contrib/lua/src" "${HB_DCSS_SRC}/contrib/sqlite" "${HB_DCSS_SRC}/contrib/pcre" "${HB_DCSS_SRC}/contrib/zlib")
target_compile_definitions(dcss_original PRIVATE USE_TILE USE_TILE_WEB WIZARD CLUA_BINDINGS REGEX_PCRE PCRE_STATIC HENGBAND_ANDROID=1)
# crash.cc の backtrace は API 33 未満（Quest は minSdk 32）に宣言が無い。何も返さない代わりの <execinfo.h> を crash.cc にだけ当てる
# （原作自身の Android 版も、クラッシュの記録は使わない設定）。
set_source_files_properties("${HB_DCSS_SRC}/crash.cc" PROPERTIES COMPILE_OPTIONS "-I${HB_DCSS_ADAPTER}/android_compat")
# 原作の main() は dcss_original_main に名前を替える（原作の行は書き換えない）
set_source_files_properties("${HB_DCSS_SRC}/main.cc" PROPERTIES COMPILE_DEFINITIONS "main=dcss_original_main")

# --- プロトコル v1 の層（C++20。原作のヘッダは引かない）と、共用の符号化 ---
add_library(dcsscore SHARED $<TARGET_OBJECTS:dcss_original>
    "${HB_DCSS_ADAPTER}/dcss_main.cpp" "${HB_DCSS_ADAPTER}/dcss_frame.cpp"
    "${HB_ROOT}/presentation/frame/frame_codec.cpp" "${HB_ROOT}/presentation/frame/protocol_messages.cpp"
    "${HB_ROOT}/presentation/frame/protocol_transport.cpp" "${HB_ROOT}/platform/portable/legacy_os.cpp"
    "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp")
target_include_directories(dcsscore PRIVATE
    "${HB_DCSS_ADAPTER}" "${HB_ROOT}/presentation" "${HB_ROOT}/platform" "${HB_ROOT}/hd2d" "${HB_ROOT}/third_party/cpp/include"
    "${HB_DCSS_SRC}/contrib/zlib")
target_compile_definitions(dcsscore PRIVATE HENGBAND_ANDROID=1)
target_compile_options(dcsscore PRIVATE -ffunction-sections -fdata-sections)
target_link_options(dcsscore PRIVATE -Wl,--gc-sections -Wl,-Bsymbolic)
target_link_libraries(dcsscore PRIVATE dcss_contrib log)
# tileweb.cc の UNIX ドメインのデータグラムのソケットを、パイプ（dcss_posix_shim.cpp）に差し替える。
# 仮のソケットの番号でない呼び出しは __real_ で本物へ素通しする（Linux と同じ）。
# mkdir・stat・opendir も向ける（Android の SELinux が実在する上の階層への mkdir・stat を EACCES で返すため。dcss_posix_shim.cpp の __wrap_mkdir・__wrap_stat・__wrap_opendir）。
foreach(fn socket bind setsockopt sendto recvfrom select pselect mkdir stat opendir)
    target_link_options(dcsscore PRIVATE "-Wl,--wrap=${fn}")
endforeach()
