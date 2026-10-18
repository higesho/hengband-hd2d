cmake_minimum_required(VERSION 3.20)
project(HengbandHd2dLinux LANGUAGES C CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
    message(FATAL_ERROR "This build is for desktop Linux")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(HB_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(HB_DEFAULT_CORE_ROOT "${HB_ROOT}/../roguelike-cores/build-sources" ABSOLUTE)
if(DEFINED ENV{HENGBAND_CORE_SOURCE_ROOT})
    set(HB_DEFAULT_CORE_ROOT "$ENV{HENGBAND_CORE_SOURCE_ROOT}")
endif()
set(HB_CORE_SOURCE_ROOT "${HB_DEFAULT_CORE_ROOT}" CACHE PATH "Verified external core sources")
option(HENGBAND_BUILD_CORES "Build all ten game cores" ON)
if(HENGBAND_BUILD_CORES AND NOT EXISTS "${HB_CORE_SOURCE_ROOT}/.prepared.json")
    message(FATAL_ERROR "Prepare external sources with tools/core_sources/prepare.py or set HB_CORE_SOURCE_ROOT")
endif()
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include("${HB_ROOT}/tools/core_sources/generated_headers.cmake")
include("${HB_ROOT}/tools/targeting/targeting_sources.cmake")
include("${HB_ROOT}/tools/rogue_legacy/rogue_legacy_sources.cmake")
pkg_check_modules(SDL REQUIRED IMPORTED_TARGET sdl2 SDL2_image SDL2_ttf SDL2_mixer)
pkg_check_modules(OPENAL REQUIRED IMPORTED_TARGET openal)
pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)
find_program(HB_CCACHE ccache)
if(HB_CCACHE)
    # CCACHE_BASEDIR rewrites paths under the build directory to relative ones, so a new build
    # directory (legacy-src/, generated headers, PCH) still hits the cache. pch_defines and
    # time_macros are required for ccache to accept precompiled headers. The user's global
    # ccache config is left alone.
    set(HB_CCACHE_LAUNCHER "${CMAKE_COMMAND}" -E env "CCACHE_BASEDIR=${CMAKE_BINARY_DIR}" "CCACHE_SLOPPINESS=pch_defines,time_macros" "CCACHE_PCH_EXTSUM=true" "${HB_CCACHE}")
    set(CMAKE_C_COMPILER_LAUNCHER "${HB_CCACHE_LAUNCHER}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${HB_CCACHE_LAUNCHER}")
endif()
# Keep local paths (user name, drive layout) out of __FILE__ strings in the shipped binaries.
# Paths under the build directory are already made relative by CCACHE_BASEDIR; the build
# directory itself is not mapped here because a per-directory flag would defeat ccache.
get_filename_component(HB_ROOT_ABS "${HB_ROOT}" ABSOLUTE)
# HB_TARGET_ORIGINAL_ROOT: targeting_sources.cmake has already pointed HB_CORE_SOURCE_ROOT at the
# build-directory overlay, which CCACHE_BASEDIR handles; map the external original tree instead.
get_filename_component(HB_CORE_SOURCE_ROOT_ABS "${HB_TARGET_ORIGINAL_ROOT}" ABSOLUTE)
add_compile_options("-ffile-prefix-map=${HB_CORE_SOURCE_ROOT_ABS}/=" "-ffile-prefix-map=${HB_ROOT_ABS}/=")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
function(hb_common target)
    target_include_directories(${target} PRIVATE "${HB_ROOT}/hd2d" "${HB_ROOT}/presentation" "${HB_GENERATED_INCLUDE_DIR}" "${HB_ROOT}/platform" "${HB_ROOT}/third_party/cpp/include")
    target_compile_definitions(${target} PRIVATE HENGBAND_DESKTOP_LINUX=1)
    target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
    target_link_libraries(${target} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN/lib;$ORIGIN/../lib")
endfunction()
file(GLOB_RECURSE HB_UI CONFIGURE_DEPENDS "${HB_ROOT}/hd2d/*.cpp")
list(FILTER HB_UI EXCLUDE REGEX "/hd2d/edit/")
list(FILTER HB_UI EXCLUDE REGEX "/hd2d/audio/audio_engine_null\\.cpp$")
file(GLOB HB_FRAME CONFIGURE_DEPENDS "${HB_ROOT}/presentation/frame/*.cpp")
add_executable(HengbandHd2d ${HB_UI} ${HB_FRAME} "${HB_ROOT}/platform/linux/hd2d_main.cpp" "${HB_ROOT}/platform/linux/user_data.cpp" "${HB_ROOT}/platform/portable/cp932.cpp" "${HB_ROOT}/third_party/cpp/fmt/format.cc")
hb_common(HengbandHd2d)
target_compile_definitions(HengbandHd2d PRIVATE JP UTF8)
target_link_libraries(HengbandHd2d PRIVATE PkgConfig::SDL PkgConfig::OPENAL PkgConfig::JSONCPP z)
if(NOT HENGBAND_BUILD_CORES)
    return()
endif()
# HengbandCore builds the Hengband source (src/); TangbandCore builds its own copy pinned to
# Hengband 3.0.2.4 (tangband/src), so that upgrading Hengband does not change Tangband
# . Both lists use the same filters.
function(hb_collect_core_sources out root)
    file(GLOB_RECURSE files CONFIGURE_DEPENDS "${root}/*.cpp")
    foreach(pattern "/src/main\\.cpp$" "/src/main-win\\.cpp$" "/src/main-x11\\.cpp$" "/src/main-gcu\\.cpp$" "/src/main-cap\\.cpp$" "/src/maid-x11\\.cpp$" "/src/main-win/" "/src/main-unix/x11-" "/src/main-unix/unix-music\\.cpp$" "/src/main-unix/unix-sound\\.cpp$" "/src/main-unix/stack-trace-unix\\.cpp$" "/src/test/")
        list(FILTER files EXCLUDE REGEX "${pattern}")
    endforeach()
    list(FILTER files EXCLUDE REGEX "/src/term/(z-term|screen-processor)\\.cpp$")
    set(${out} ${files} PARENT_SCOPE)
endfunction()
set(HB_SRC_HengbandCore "${HB_CORE_SOURCE_ROOT}/src")
set(HB_SRC_TangbandCore "${HB_CORE_SOURCE_ROOT}/tangband/src")
hb_collect_core_sources(HB_CORE_HengbandCore "${HB_SRC_HengbandCore}")
hb_collect_core_sources(HB_CORE_TangbandCore "${HB_SRC_TangbandCore}")
file(GLOB_RECURSE HB_PRESENTATION CONFIGURE_DEPENDS "${HB_ROOT}/presentation/*.cpp")
list(FILTER HB_PRESENTATION EXCLUDE REGEX "/audio/sdl_win_audio\\.cpp$")
foreach(core HengbandCore TangbandCore)
    add_executable(${core} ${HB_CORE_${core}} ${HB_PRESENTATION} "${HB_SRC_${core}}/external-lib/fmt/format.cc" "${HB_ROOT}/platform/windows/core_main.cpp" "${HB_ROOT}/platform/android/stack_trace_android.cpp" "${HB_ROOT}/platform/linux/core_main.cpp")
    hb_common(${core})
    target_include_directories(${core} PRIVATE "${HB_SRC_${core}}" "${HB_SRC_${core}}/external-lib/include")
    target_compile_definitions(${core} PRIVATE JP UTF8 HAVE_STDINT_H HAVE_SYS_TIME_H DISABLE_NET HENGBAND_SDL2_UI=1)
    target_link_libraries(${core} PRIVATE PkgConfig::SDL)
    set_target_properties(${core} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/cores")
endforeach()
# Precompiled header for HengbandCore/TangbandCore, shared with the Windows build
# (VisualStudio/CorePch.props) and the Android build: platform/shared/hengband_core_pch.h includes
# stdafx.h plus the Hengband headers that nearly every source reads. Cautions: 
# ("Shared precompiled header"); tools/core_sources/pch_safety_check.py guards it.
# -DHB_CORE_PCH=OFF falls back to a precompiled stdafx.h only (the old behaviour), so that a
# forgotten #include that the shared header hides shows up as a compile error.
# Only the shared Hengband sources use it; the presentation layer, main files and fmt keep
# compiling without it, as on Windows.
option(HB_CORE_PCH "Use the shared Hengband precompiled header for HengbandCore/TangbandCore" ON)
if(HB_CORE_PCH)
    set(HB_CORE_PCH_HEADER "${HB_ROOT}/platform/shared/hengband_core_pch.h")
    # The ccache checksum below is written at configure time, so editing the header must re-run configure.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${HB_CORE_PCH_HEADER}")
else()
    # A wrapper in the source tree: CMake's generated cmake_pch.hxx would otherwise contain the
    # per-build-directory absolute path of stdafx.h, which ccache hashes.
    set(HB_CORE_PCH_HEADER "${HB_ROOT}/platform/linux/core_pch.h")
endif()
foreach(core HengbandCore TangbandCore)
    target_precompile_headers(${core} PRIVATE "${HB_CORE_PCH_HEADER}")
    set_source_files_properties(${HB_PRESENTATION} "${HB_SRC_${core}}/external-lib/fmt/format.cc" "${HB_ROOT}/platform/windows/core_main.cpp" "${HB_ROOT}/platform/android/stack_trace_android.cpp" "${HB_ROOT}/platform/linux/core_main.cpp" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endforeach()
target_compile_definitions(TangbandCore PRIVATE TANGBAND=1)
# Tangband's generated headers come from its own source and data (tools/core_sources/generated_headers.cmake).
target_include_directories(TangbandCore BEFORE PRIVATE "${HB_GENERATED_INCLUDE_DIR_TANGBAND}")
# ccache would hash the 180 MB .gch, which embeds the build directory path, so every new build
# directory missed. With CCACHE_PCH_EXTSUM ccache reads <pch>.sum instead; write a hash of the
# preprocessed header (no paths, -P) so the key still changes whenever the PCH content or the
# flags change.
if(HB_CCACHE)
    foreach(core HengbandCore TangbandCore)
        get_target_property(inc ${core} INCLUDE_DIRECTORIES)
        get_target_property(def ${core} COMPILE_DEFINITIONS)
        set(flags -std=c++20 -O3 -DNDEBUG -pthread)
        foreach(i ${inc})
            list(APPEND flags "-I${i}")
        endforeach()
        foreach(d ${def})
            list(APPEND flags "-D${d}")
        endforeach()
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -E -P -x c++ ${flags} "${HB_CORE_PCH_HEADER}" OUTPUT_VARIABLE pch_text RESULT_VARIABLE pch_rc ERROR_QUIET)
        if(NOT pch_rc EQUAL 0)
            message(FATAL_ERROR "Cannot preprocess ${HB_CORE_PCH_HEADER} for the ccache PCH checksum")
        endif()
        string(REPLACE "${CMAKE_BINARY_DIR}" "<build>" sum_flags "${flags}")
        # range-v3 assert messages expand __FILE__, so the text contains the build directory.
        string(REPLACE "${CMAKE_BINARY_DIR}" "<build>" pch_text "${pch_text}")
        string(SHA256 pch_sum "${CMAKE_CXX_COMPILER_VERSION};${sum_flags};${pch_text}")
        file(WRITE "${CMAKE_BINARY_DIR}/CMakeFiles/${core}.dir/cmake_pch.hxx.gch.sum" "${pch_sum}
")
    endforeach()
endif()

# Converted copies belong to this build; the verified original input stays untouched.
set(HB_SJIS "${CMAKE_BINARY_DIR}/legacy-src")
# TOband is read from the targeting overlay once tools/targeting handles it; until then
# the overlay has no toband/, so the verified original tree is used directly (no edits either way).
if(EXISTS "${HB_CORE_SOURCE_ROOT}/toband/src")
    set(HB_TOBAND_SOURCE "${HB_CORE_SOURCE_ROOT}/toband/src")
else()
    set(HB_TOBAND_SOURCE "${HB_TARGET_ORIGINAL_ROOT}/toband/src")
endif()
foreach(spec "gensoband/src|cp932|${HB_CORE_SOURCE_ROOT}/gensoband/src" "gensoband/adapter|utf8|${HB_ROOT}/gensoband/adapter" "toband/src|cp932|${HB_TOBAND_SOURCE}" "toband/adapter|utf8|${HB_ROOT}/toband/adapter" "silq/adapter|utf8|${HB_ROOT}/silq/adapter" "frox/adapter|utf8|${HB_ROOT}/frox/adapter")
    string(REPLACE "|" ";" parts "${spec}")
    list(GET parts 0 dest)
    list(GET parts 1 encoding)
    list(GET parts 2 source)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${HB_ROOT}/tools/transcode_cp932_src.py" --from "${encoding}" --ext .c,.cpp,.h,.inl,.inc --quiet "${source}" "${HB_SJIS}/${dest}" COMMAND_ERROR_IS_FATAL ANY)
    file(GLOB_RECURSE originals CONFIGURE_DEPENDS "${source}/*.c" "${source}/*.cpp" "${source}/*.h" "${source}/*.inl" "${source}/*.inc")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${originals})
endforeach()
set(HB_LEGACY_SHARED "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp" "${HB_ROOT}/presentation/bridge/native_targeting_runtime.cpp" "${HB_ROOT}/presentation/frame/frame_codec.cpp" "${HB_ROOT}/presentation/frame/protocol_messages.cpp" "${HB_ROOT}/presentation/frame/protocol_transport.cpp" "${HB_ROOT}/platform/portable/legacy_os.cpp" "${HB_ROOT}/platform/portable/cp932.cpp")
function(hb_legacy target source adapter)
    hb_common(${target})
    target_include_directories(${target} PRIVATE "${adapter}" "${source}")
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:-fcommon> $<$<COMPILE_LANGUAGE:C>:-Wno-error=implicit-function-declaration> $<$<COMPILE_LANGUAGE:C>:-Wno-error=implicit-int> $<$<COMPILE_LANGUAGE:C>:-Wno-error=int-conversion> $<$<COMPILE_LANGUAGE:C>:-Wno-error=incompatible-pointer-types>)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/cores")
endfunction()
function(hb_legacy_sources variable dir)
    file(GLOB source CONFIGURE_DEPENDS "${dir}/*.c")
    foreach(pattern "/main\\.c$" "/main-(win|x11|gcu|cap|dos|ibm|mac|sdl|xaw)\\.c$" "/maid-x11\\.c$" "/readdib\\.c$" "/z-doc-test\\.c$")
        list(FILTER source EXCLUDE REGEX "${pattern}")
    endforeach()
    set(${variable} ${source} PARENT_SCOPE)
endfunction()
hb_legacy_sources(GB "${HB_SJIS}/gensoband/src")
list(FILTER GB EXCLUDE REGEX "/store\\.c$")
file(GLOB GB_ADAPTER CONFIGURE_DEPENDS "${HB_SJIS}/gensoband/adapter/*.c" "${HB_SJIS}/gensoband/adapter/*.cpp")
add_executable(GensobandCore ${GB} ${GB_ADAPTER} ${HB_LEGACY_SHARED})
hb_legacy(GensobandCore "${HB_SJIS}/gensoband/src" "${HB_SJIS}/gensoband/adapter")
target_compile_definitions(GensobandCore PRIVATE JP SJIS)
# TOband (`TobandCore`,  section 3.3). The CP932 copy of the original is
# converted like gensoband's. The original is never edited; everything below is build settings.
hb_legacy_sources(TB "${HB_SJIS}/toband/src")
# report.c sends the score to the fork author's server (constraint 5); tb_report_stub.c replaces it.
list(FILTER TB EXCLUDE REGEX "/report\\.c$")
# store.c is not built alone: tb_native_store.c includes it (same as gb_native_store.c above).
list(FILTER TB EXCLUDE REGEX "/store\\.c$")
# util.c and birth.c are not built alone either: tb_native_util.c and tb_native_birth.c include them
# (they raise the text-input flag for the new UI; the original is not edited).
list(FILTER TB EXCLUDE REGEX "/util\\.c$")
list(FILTER TB EXCLUDE REGEX "/birth\\.c$")
file(GLOB TB_ADAPTER CONFIGURE_DEPENDS "${HB_SJIS}/toband/adapter/*.c" "${HB_SJIS}/toband/adapter/*.cpp")
# The original's own warnings are silenced; the adapter's are kept (design section 3: no silencing there).
set_source_files_properties(${TB} PROPERTIES COMPILE_OPTIONS "-w")
add_executable(TobandCore ${TB} ${TB_ADAPTER} ${HB_LEGACY_SHARED})
hb_legacy(TobandCore "${HB_SJIS}/toband/src" "${HB_SJIS}/toband/adapter")
# L64: the original's h-type.h sets it only on Alpha, so on LP64 u32b would become 64 bits.
# Gensoband avoids this with stdint types; TOband gets the same result from the definition.
target_compile_definitions(TobandCore PRIVATE JP SJIS L64)
# bldg.c calls party.h functions without including party.h (design section 3.3; same as the
# ForcedIncludeFiles of TobandCore.vcxproj).
set_source_files_properties("${HB_SJIS}/toband/src/bldg.c" PROPERTIES COMPILE_OPTIONS "-w;-include;angband.h;-include;party.h")
hb_legacy_sources(SQ "${HB_CORE_SOURCE_ROOT}/silq/src")
list(FILTER SQ EXCLUDE REGEX "/cmd3\\.c$")
file(GLOB SQ_ADAPTER CONFIGURE_DEPENDS "${HB_SJIS}/silq/adapter/*.c" "${HB_SJIS}/silq/adapter/*.cpp")
add_executable(SilCore ${SQ} ${SQ_ADAPTER} ${HB_LEGACY_SHARED})
hb_legacy(SilCore "${HB_CORE_SOURCE_ROOT}/silq/src" "${HB_SJIS}/silq/adapter")
hb_legacy_sources(FC "${HB_CORE_SOURCE_ROOT}/frox/src")
list(FILTER FC EXCLUDE REGEX "/(shop|home|util)\\.c$")
file(GLOB FC_ADAPTER CONFIGURE_DEPENDS "${HB_SJIS}/frox/adapter/*.c" "${HB_SJIS}/frox/adapter/*.cpp")
list(FILTER FC_ADAPTER EXCLUDE REGEX "/fc_compat\\.c$")
foreach(file monspell.c obj_prompt.c skillmaster.c weaponsmith.c wild.c wiz_obj.c)
    set_source_files_properties("${HB_CORE_SOURCE_ROOT}/frox/src/${file}" PROPERTIES COMPILE_DEFINITIONS "Term_save=fc_ui_term_save;Term_load=fc_ui_term_load")
endforeach()
# 選択メニューの全件（原作の menu_choose）: FroxCore.vcxproj と同じ 12 TU にだけ、呼び先を `fc_menu_choose`
# （`frox/adapter/fc_shim.c`）へ替える。上の Term_save の 6 TU とは重ならない（重なるようになったら、ここを
# `COMPILE_DEFINITIONS "…;…"` の 1 つの値にまとめる——`set_source_files_properties` は値を上書きする）。
foreach(file bldg.c equip.c mut.c psion.c r_dragon.c r_mummy.c r_ring.c ragemage.c rune_kt.c samurai.c wild_talent.c yeqrezh.c)
    set_source_files_properties("${HB_CORE_SOURCE_ROOT}/frox/src/${file}" PROPERTIES COMPILE_DEFINITIONS "menu_choose=fc_menu_choose")
endforeach()
add_executable(FroxCore ${FC} ${FC_ADAPTER} ${HB_LEGACY_SHARED})
hb_legacy(FroxCore "${HB_CORE_SOURCE_ROOT}/frox/src" "${HB_SJIS}/frox/adapter")

# Angband 4.2.6 (`AngbandCore`). Modern upstream C11 source ( 
# section 3), unlike the four legacy CP932 cores above, so it needs neither the SJIS
# conversion tree nor hb_legacy()'s old-C compatibility flags. tools/core_sources/prepare.py
# reconstructs the verified original tree at "${HB_TARGET_ORIGINAL_ROOT}/angband/patched-src", the same
# convention tools/angband/build_windows.py uses for the Windows build. Like RogueClone2 above,
# this must use HB_TARGET_ORIGINAL_ROOT (not HB_CORE_SOURCE_ROOT): tools/targeting/targeting_sources.cmake
# repoints HB_CORE_SOURCE_ROOT at its own generated overlay, which never touches angband/.
file(GLOB HB_ANGBAND_CORE_C CONFIGURE_DEPENDS "${HB_TARGET_ORIGINAL_ROOT}/angband/patched-src/*.c")
foreach(pattern "/main[^/]*\\.c$" "/snd-sdl\\.c$" "/wiz-stats\\.c$")
    list(FILTER HB_ANGBAND_CORE_C EXCLUDE REGEX "${pattern}")
endforeach()
# design section 3: "アダプタ側は警告を黙らせない" -- only the untouched upstream .c
# files are silenced, matching build_windows.py's `core_c` vs `adapter_c` flag split.
set_source_files_properties(${HB_ANGBAND_CORE_C} PROPERTIES COMPILE_OPTIONS "-w")
set(HB_ANGBAND_ADAPTER_C
    "${HB_ROOT}/angband/adapter/an_bootstrap.c"
    "${HB_ROOT}/angband/adapter/an_null_term.c"
    "${HB_ROOT}/angband/adapter/an_wiz_stats_stub.c"
    "${HB_ROOT}/angband/adapter/an_map_shim.c"
    "${HB_ROOT}/angband/adapter/an_native_inventory.c"
    "${HB_ROOT}/angband/adapter/an_text_hooks.c"
    "${HB_ROOT}/angband/adapter/an_pad_shim.c"
    "${HB_ROOT}/angband/adapter/an_ui_shim.c"
    "${HB_ROOT}/angband/adapter/an_targeting.c"
    "${HB_ROOT}/angband/adapter/an_town_rng.c"
    "${HB_ROOT}/angband/adapter/an_town_map.c"
    "${HB_ROOT}/angband/adapter/an_profile.c"
    "${HB_ROOT}/angband/adapter/an_fx.c"
    "${HB_ROOT}/angband/adapter/an_test_monster_act.c"
    "${HB_ROOT}/angband/adapter/an_wide.c"
    "${HB_ROOT}/angband/adapter/an_name_ja.c"
    "${HB_ROOT}/angband/adapter/an_lore_ja.c")
set(HB_ANGBAND_ADAPTER_CPP
    "${HB_ROOT}/angband/adapter/an_main.cpp"
    "${HB_ROOT}/angband/adapter/an_frame.cpp"
    "${HB_ROOT}/angband/adapter/an_manifest.cpp"
    "${HB_ROOT}/angband/adapter/an_lang.cpp"
    "${HB_ROOT}/presentation/frame/frame_codec.cpp"
    "${HB_ROOT}/presentation/frame/protocol_messages.cpp"
    "${HB_ROOT}/presentation/frame/protocol_transport.cpp"
    "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp"
    "${HB_ROOT}/presentation/bridge/native_targeting_runtime.cpp"
    "${HB_ROOT}/platform/portable/legacy_os.cpp")
add_executable(AngbandCore ${HB_ANGBAND_CORE_C} ${HB_ANGBAND_ADAPTER_C} ${HB_ANGBAND_ADAPTER_CPP})
hb_common(AngbandCore)
target_include_directories(AngbandCore PRIVATE "${HB_TARGET_ORIGINAL_ROOT}/angband/patched-src" "${HB_ROOT}/angband/adapter")
set_target_properties(AngbandCore PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/cores")

# Umoria 5.7 (`UmoriaCore`,). Upstream C++ under
# "${HB_TARGET_GENERATED_ROOT}/umoria/patched-src", built the same way as tools/umoria/build_windows.py:
# every upstream .cpp except the curses terminal ui_io.cpp (umoria/adapter/um_io.cpp replaces it),
# upstream main() renamed to um_game_main, upstream files as C++14 with warnings silenced.
file(GLOB HB_UMORIA_CORE_CPP CONFIGURE_DEPENDS "${HB_TARGET_GENERATED_ROOT}/umoria/patched-src/*.cpp")
list(FILTER HB_UMORIA_CORE_CPP EXCLUDE REGEX "/ui_io\.cpp$")
set_source_files_properties(${HB_UMORIA_CORE_CPP} PROPERTIES COMPILE_OPTIONS "-std=c++14;-w")
set_source_files_properties("${HB_TARGET_GENERATED_ROOT}/umoria/patched-src/main.cpp" PROPERTIES COMPILE_DEFINITIONS "main=um_game_main")
add_executable(UmoriaCore ${HB_UMORIA_CORE_CPP}
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
    "${HB_ROOT}/platform/portable/legacy_os.cpp")
hb_common(UmoriaCore)
target_include_directories(UmoriaCore PRIVATE "${HB_TARGET_GENERATED_ROOT}/umoria/patched-src" "${HB_ROOT}/umoria/adapter")
set_target_properties(UmoriaCore PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/cores")

# RogueClone2. Its patched-src tree is reconstructed by tools/core_sources/prepare.py; the targeting overlay
# (tools/targeting/native_fx_overlay.py) copies it with the attack-effect observation calls added, and that copy is built.
if(DEFINED HB_TARGET_GENERATED_ROOT)
    set(HB_R2_ROOT "${HB_TARGET_GENERATED_ROOT}")
else()
    set(HB_R2_ROOT "${HB_CORE_SOURCE_ROOT}")
endif()
set(HB_R2_SRC_DIR "${HB_R2_ROOT}/rogue2/patched-src")
file(GLOB HB_R2_SRC CONFIGURE_DEPENDS "${HB_R2_SRC_DIR}/*.c")
list(FILTER HB_R2_SRC EXCLUDE REGEX "/main\\.c$")
set(HB_R2_MAIN "${HB_R2_SRC_DIR}/main.c")
set_source_files_properties(${HB_R2_MAIN} PROPERTIES COMPILE_DEFINITIONS "main=r2_game_main")
set(HB_R2_ADAPTER_C
    "${HB_ROOT}/rogue2/adapter/r2_curses.c"
    "${HB_ROOT}/rogue2/adapter/r2_compat_iconv.c"
    "${HB_ROOT}/rogue2/adapter/r2_persist.c"
    "${HB_ROOT}/rogue2/adapter/r2_shim.c")
set(HB_R2_ADAPTER_CPP
    "${HB_ROOT}/rogue2/adapter/r2_main.cpp"
    "${HB_ROOT}/rogue2/adapter/r2_frame.cpp"
    "${HB_ROOT}/presentation/frame/frame_codec.cpp"
    "${HB_ROOT}/presentation/frame/protocol_messages.cpp"
    "${HB_ROOT}/presentation/frame/protocol_transport.cpp"
    "${HB_ROOT}/presentation/bridge/input_event_adapter.cpp"
    "${HB_ROOT}/platform/portable/legacy_os.cpp")
add_executable(Rogue2Core ${HB_R2_SRC} ${HB_R2_MAIN} ${HB_R2_ADAPTER_C} ${HB_R2_ADAPTER_CPP})
hb_common(Rogue2Core)
target_include_directories(Rogue2Core PRIVATE
    "${HB_ROOT}/rogue2/adapter/compat" "${HB_ROOT}/rogue2/adapter"
    "${HB_R2_SRC_DIR}"
    "${HB_ROOT}/third_party/cpp/include" "${HB_ROOT}/presentation" "${HB_ROOT}/platform")
target_compile_definitions(Rogue2Core PRIVATE JAPAN TOPSCO)
target_compile_options(Rogue2Core PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include;${HB_ROOT}/rogue2/adapter/compat/r2_build_config.h>
    $<$<COMPILE_LANGUAGE:C>:-w>)
set_target_properties(Rogue2Core PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/cores")

# Restored Rogue 3.x / 5.x.  Both generations share the HD2D curses/protocol
# adapter; only their immutable upstream source trees and entry wrappers differ.
function(hb_rogue_legacy target source_dir generation entry wrapper forced_config)
    hb_rogue_legacy_collect(legacy_all "${source_dir}" "${generation}")
    set(legacy_src ${legacy_all})
    list(FILTER legacy_src EXCLUDE REGEX "/main\\.c$")
    set(legacy_main "${source_dir}/main.c")
    set_source_files_properties(${legacy_main} PROPERTIES COMPILE_DEFINITIONS "main=${entry}")
    add_executable(${target}
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
    hb_common(${target})
    target_compile_definitions(${target} PRIVATE RL_GENERATION=${generation})
    target_include_directories(${target} PRIVATE
        "${HB_ROOT}/rogue_legacy/adapter/compat"
        "${HB_ROOT}/rogue_legacy/adapter"
        "${source_dir}"
        "${HB_ROOT}/third_party/cpp/include"
        "${HB_ROOT}/presentation"
        "${HB_ROOT}/platform")
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-include;${forced_config};-w>)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/cores")
endfunction()

hb_rogue_legacy(Rogue36Core "${HB_TARGET_GENERATED_ROOT}/rogue36/patched-src" 36 r36_game_main
    "${HB_ROOT}/rogue36/adapter/r36_main.cpp" "${HB_ROOT}/rogue_legacy/adapter/compat/r36_build_config.h")
hb_rogue_legacy(Rogue54Core "${HB_TARGET_GENERATED_ROOT}/rogue54/patched-src" 54 r54_game_main
    "${HB_ROOT}/rogue54/adapter/r54_main.cpp" "${HB_ROOT}/rogue_legacy/adapter/compat/r54_build_config.h")

# Enable the original secure temporary-file implementation where it is compiled.
set_source_files_properties(
    "${HB_CORE_SOURCE_ROOT}/src/util/angband-files.cpp"
    "${HB_SJIS}/gensoband/src/util.c"
    "${HB_CORE_SOURCE_ROOT}/silq/src/util.c"
    "${HB_SJIS}/frox/adapter/fc_native_util.c"
    PROPERTIES COMPILE_DEFINITIONS "HAVE_MKSTEMP=1")
