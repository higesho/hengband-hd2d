# DCSS（日本語版）のコア `DcssCore`（Linux）。platform/linux/CMakeLists.txt の include(…/dcss_core.cmake) から読まれる。
#
# 原作は tools/core_sources/prepare.py が作る "${HB_TARGET_ORIGINAL_ROOT}/dcss"（crawl-ref/ と ja-tools/）。書き換えない。
# 組み立ては次の 4 つ（「原作の再構成と Linux」）。
#   1. dcss_tilegen   原作の rltiles/tool（5 本の C++）を、ホストで走らせる道具として組む
#   2. dcss_gen       tools/core_sources/dcss_generate.py。Perl・Python のスクリプトと tilegen で、ヘッダと
#                     rltiles/tiledef-*.cc を ${CMAKE_BINARY_DIR}/dcss-gen に作る（原作の写しには書かない）
#   3. dcss_contrib   付属ライブラリ（lua・sqlite・pcre・zlib）の C ソースを 1 つの静的ライブラリに
#   4. DcssCore       原作の C++（Makefile.obj の OBJECTS + TILES_OBJECTS + WEBTILES_OBJECTS + main/version/prebuilt）と
#                     生成物と、dcss/adapter（端末の層・ソケットの差し替え・プロトコル v1）
# 原作のソース一覧は Makefile.obj から読む（Windows の DcssCore.vcxproj と同じ規則）。
#
# 既定は ON（2026-10-22。配布物に入れるため）。`HENGBAND_BUILD_CORES`（全コアを組む。既定 ON）と同じ扱いで、DCSS だけ省くときは
# `-DHENGBAND_BUILD_DCSS=OFF`。理由: (1) 配布物の組み立て（tools/package/build_linux_package.py）は既定で DcssCore を必須にするので、
# 既定 OFF だと手順に ON を渡し忘れた配布物が黙って 11 本になる。(2) 原作の再構成（tools/core_sources/prepare.py）はもう DCSS を含み、
# `HENGBAND_BUILD_CORES` が要る原作の準備（`.prepared.json`）と同じ前提で足りる。(3) 他のコアに無い前提の道具は Perl 5 と Python 3 の PyYAML
# （生成の段が使う）。足りない環境は OFF にできる（配布物は `--dcss off` で作る）。
# アダプタ（dcss/adapter）は Windows・Linux の両方で組める（2026-10-21）。Linux では互換ヘッダ（adapter/shim/sys/）を使わず、
# 本物の POSIX のヘッダを使う。tileweb.cc のソケットまわりは、リンクの --wrap で dcss_posix_shim.cpp の __wrap_* へ向ける。
# HB_DCSS_MAIN_SOURCES・HB_DCSS_SHIM_SOURCES を渡すと、プロトコル層とソケットの差し替えの C++ を差し替えられる（検査用）。

option(HENGBAND_BUILD_DCSS "Build DcssCore (Dungeon Crawl Stone Soup, Japanese edition)" ON)
if(NOT HENGBAND_BUILD_DCSS OR NOT HENGBAND_BUILD_CORES)
    return()
endif()

set(HB_DCSS_ROOT "${HB_TARGET_ORIGINAL_ROOT}/dcss")
set(HB_DCSS_SRC "${HB_DCSS_ROOT}/crawl-ref/source")
if(NOT EXISTS "${HB_DCSS_SRC}/AppHdr.cc")
    message(FATAL_ERROR "HENGBAND_BUILD_DCSS: ${HB_DCSS_SRC} がありません。tools/core_sources/prepare.py で dcss を作ってください")
endif()
set(HB_DCSS_GEN "${CMAKE_BINARY_DIR}/dcss-gen")
set(HB_DCSS_WORK "${CMAKE_BINARY_DIR}/dcss-work")
set(HB_DCSS_ADAPTER "${HB_ROOT}/dcss/adapter")

# --- 原作のソース一覧（Makefile.obj の 3 つの変数） ---
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${HB_DCSS_SRC}/Makefile.obj")
file(READ "${HB_DCSS_SRC}/Makefile.obj" HB_DCSS_MK)
string(REPLACE "\\\n" " " HB_DCSS_MK "${HB_DCSS_MK}")   # 行末のバックスラッシュでつながる行を 1 行にする
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
# libunix.cc・libw32c.cc は Makefile.obj の ALL_OBJECTS にだけ在り、上の 3 変数には入らない（端末の層は dcss/adapter/libhd2d.cpp）

# --- 1. tilegen ---
set(HB_DCSS_TILEGEN_DIR "${HB_DCSS_SRC}/rltiles/tool")
add_executable(dcss_tilegen
    "${HB_DCSS_TILEGEN_DIR}/main.cc" "${HB_DCSS_TILEGEN_DIR}/tile.cc" "${HB_DCSS_TILEGEN_DIR}/tile_colour.cc"
    "${HB_DCSS_TILEGEN_DIR}/tile_list_processor.cc" "${HB_DCSS_TILEGEN_DIR}/tile_page.cc")
set_target_properties(dcss_tilegen PROPERTIES CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dcss-tools")
target_compile_options(dcss_tilegen PRIVATE -w)

# --- 2. 生成物 ---
set(HB_DCSS_TILEDEF_CC "")
foreach(name main dngn floor wall feat player gui icons unrand)
    list(APPEND HB_DCSS_TILEDEF_CC "${HB_DCSS_GEN}/rltiles/tiledef-${name}.cc")
endforeach()
find_program(HB_PERL perl REQUIRED)
add_custom_command(
    OUTPUT "${HB_DCSS_GEN}/.generated" ${HB_DCSS_TILEDEF_CC}
    COMMAND "${Python3_EXECUTABLE}" "${HB_ROOT}/tools/core_sources/dcss_generate.py"
        --source "${HB_DCSS_ROOT}" --tilegen "$<TARGET_FILE:dcss_tilegen>" --work "${HB_DCSS_WORK}" --out "${HB_DCSS_GEN}"
        --cxx "${CMAKE_CXX_COMPILER}" --perl "${HB_PERL}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${HB_DCSS_GEN}/.generated"
    DEPENDS dcss_tilegen "${HB_ROOT}/tools/core_sources/dcss_generate.py" "${HB_TARGET_ORIGINAL_ROOT}/.prepared.json"
    COMMENT "DCSS: ヘッダとタイルの表を生成"
    VERBATIM)
add_custom_target(dcss_gen DEPENDS "${HB_DCSS_GEN}/.generated")
set_source_files_properties(${HB_DCSS_TILEDEF_CC} PROPERTIES GENERATED TRUE)

# --- 3. 付属ライブラリ（C） ---
file(GLOB HB_DCSS_LUA CONFIGURE_DEPENDS "${HB_DCSS_SRC}/contrib/lua/src/*.c")
list(FILTER HB_DCSS_LUA EXCLUDE REGEX "/(lua|luac)\\.c$")
# lua は src/ の全部から単体の実行ファイル（lua.c・luac.c）を除いた物。zlib・pcre は Windows の DcssCore.vcxproj と同じ名簿
set(HB_DCSS_ZLIB "")
foreach(name adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil)
    list(APPEND HB_DCSS_ZLIB "${HB_DCSS_SRC}/contrib/zlib/${name}.c")
endforeach()
set(HB_DCSS_PCRE "")
foreach(name chartables compile config dfa_exec exec fullinfo get globals maketables newline ord2utf8 refcount study tables ucd valid_utf8 version xclass)
    list(APPEND HB_DCSS_PCRE "${HB_DCSS_SRC}/contrib/pcre/pcre_${name}.c")
endforeach()
add_library(dcss_contrib STATIC ${HB_DCSS_LUA} "${HB_DCSS_SRC}/contrib/sqlite/sqlite3.c" ${HB_DCSS_ZLIB} ${HB_DCSS_PCRE})
target_compile_options(dcss_contrib PRIVATE -w)
target_include_directories(dcss_contrib PRIVATE
    "${HB_DCSS_SRC}/contrib/lua/src" "${HB_DCSS_SRC}/contrib/sqlite" "${HB_DCSS_SRC}/contrib/pcre" "${HB_DCSS_SRC}/contrib/zlib")
target_compile_definitions(dcss_contrib PRIVATE HAVE_CONFIG_H PCRE_STATIC LUA_USE_POSIX
    SQLITE_OMIT_AUTHORIZATION SQLITE_OMIT_AUTOVACUUM SQLITE_OMIT_COMPLETE SQLITE_OMIT_BLOB_LITERAL SQLITE_OMIT_COMPOUND_SELECT
    SQLITE_OMIT_CONFLICT_CLAUSE SQLITE_OMIT_DATETIME_FUNCS SQLITE_OMIT_EXPLAIN SQLITE_OMIT_INTEGRITY_CHECK SQLITE_OMIT_PAGER_PRAGMAS
    SQLITE_OMIT_PROGRESS_CALLBACK SQLITE_OMIT_SCHEMA_PRAGMAS SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS SQLITE_OMIT_TCL_VARIABLE
    SQLITE_OMIT_LOAD_EXTENSION)
set_target_properties(dcss_contrib PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE OFF)

# --- 4. 原作の C++ と端末の層（原作のヘッダを引く TU はすべてここ。C++14・警告は黙らせる） ---
# 端末の層 libhd2d.cpp と、tileweb.cc のソケットの差し替え dcss_posix_shim.cpp（Windows・Linux 共通。Linux は --wrap の受け口）
if(NOT DEFINED HB_DCSS_SHIM_SOURCES)
    set(HB_DCSS_SHIM_SOURCES "${HB_DCSS_ADAPTER}/dcss_posix_shim.cpp")
endif()
add_library(dcss_original OBJECT ${HB_DCSS_CC} ${HB_DCSS_TILEDEF_CC} "${HB_DCSS_ADAPTER}/libhd2d.cpp" ${HB_DCSS_SHIM_SOURCES})
add_dependencies(dcss_original dcss_gen)
set_target_properties(dcss_original PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
target_compile_options(dcss_original PRIVATE -w)
target_include_directories(dcss_original PRIVATE
    "${HB_DCSS_GEN}" "${HB_DCSS_GEN}/rltiles" "${HB_DCSS_ADAPTER}" "${HB_DCSS_SRC}" "${HB_DCSS_SRC}/rltiles"
    "${HB_DCSS_SRC}/contrib/lua/src" "${HB_DCSS_SRC}/contrib/sqlite" "${HB_DCSS_SRC}/contrib/pcre" "${HB_DCSS_SRC}/contrib/zlib")
target_compile_definitions(dcss_original PRIVATE USE_TILE USE_TILE_WEB WIZARD CLUA_BINDINGS REGEX_PCRE PCRE_STATIC)
# 原作の main() は dcss_original_main に名前を替える（原作の行は書き換えない。本物の main() は dcss/adapter/dcss_main.cpp）
set_source_files_properties("${HB_DCSS_SRC}/main.cc" PROPERTIES COMPILE_DEFINITIONS "main=dcss_original_main")

# プロトコル v1 の層（C++20。原作のヘッダは引かない）と、共用の符号化
if(NOT DEFINED HB_DCSS_MAIN_SOURCES)
    set(HB_DCSS_MAIN_SOURCES
        "${HB_DCSS_ADAPTER}/dcss_main.cpp" "${HB_DCSS_ADAPTER}/dcss_frame.cpp"
        "${HB_ROOT}/presentation/frame/frame_codec.cpp" "${HB_ROOT}/presentation/frame/protocol_messages.cpp"
        "${HB_ROOT}/presentation/frame/protocol_transport.cpp" "${HB_ROOT}/platform/portable/legacy_os.cpp"
        "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp")
endif()
add_executable(DcssCore $<TARGET_OBJECTS:dcss_original> ${HB_DCSS_MAIN_SOURCES})
hb_common(DcssCore)
target_include_directories(DcssCore PRIVATE "${HB_DCSS_ADAPTER}")
target_link_libraries(DcssCore PRIVATE dcss_contrib)
# tileweb.cc の UNIX ドメインのデータグラムのソケットを、標準入出力のパイプ（dcss_posix_shim.cpp）に差し替える。
# 仮のソケットの番号でない呼び出しは __real_ で本物へ素通しする。
foreach(fn socket bind setsockopt sendto recvfrom select pselect)
    target_link_options(DcssCore PRIVATE "-Wl,--wrap=${fn}")
endforeach()
set_target_properties(DcssCore PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/cores")
