# The imported originals remain immutable. Each build tree gets its own overlay.
if(HENGBAND_BUILD_CORES)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(HB_TARGET_ORIGINAL_ROOT "${HB_CORE_SOURCE_ROOT}")
    set(HB_TARGET_GENERATED_ROOT "${CMAKE_BINARY_DIR}/targeting-sources")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${HB_ROOT}/tools/targeting/overlay.py"
            --sources "${HB_TARGET_ORIGINAL_ROOT}" --output "${HB_TARGET_GENERATED_ROOT}"
        RESULT_VARIABLE HB_TARGET_RESULT OUTPUT_QUIET)
    if(NOT HB_TARGET_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not prepare common targeting input sources")
    endif()
    file(GLOB_RECURSE HB_TARGET_INPUTS CONFIGURE_DEPENDS
        "${HB_TARGET_ORIGINAL_ROOT}/src/*.cpp" "${HB_TARGET_ORIGINAL_ROOT}/src/*.h"
        "${HB_TARGET_ORIGINAL_ROOT}/tangband/src/*.cpp" "${HB_TARGET_ORIGINAL_ROOT}/tangband/src/*.h"
        "${HB_TARGET_ORIGINAL_ROOT}/frox/src/*.c" "${HB_TARGET_ORIGINAL_ROOT}/frox/src/*.h"
        "${HB_TARGET_ORIGINAL_ROOT}/gensoband/src/*.c" "${HB_TARGET_ORIGINAL_ROOT}/gensoband/src/*.h"
        "${HB_TARGET_ORIGINAL_ROOT}/toband/src/*.c" "${HB_TARGET_ORIGINAL_ROOT}/toband/src/*.h"
        "${HB_TARGET_ORIGINAL_ROOT}/silq/src/*.c" "${HB_TARGET_ORIGINAL_ROOT}/silq/src/*.h"
        "${HB_TARGET_ORIGINAL_ROOT}/umoria/patched-src/*.cpp" "${HB_TARGET_ORIGINAL_ROOT}/umoria/patched-src/*.h"
        "${HB_TARGET_ORIGINAL_ROOT}/rogue36/patched-src/*.c" "${HB_TARGET_ORIGINAL_ROOT}/rogue36/patched-src/*.h"
        "${HB_TARGET_ORIGINAL_ROOT}/rogue54/patched-src/*.c" "${HB_TARGET_ORIGINAL_ROOT}/rogue54/patched-src/*.h"
        "${HB_TARGET_ORIGINAL_ROOT}/rogue2/patched-src/*.c" "${HB_TARGET_ORIGINAL_ROOT}/rogue2/patched-src/*.h")
    file(GLOB HB_TARGET_GENERATORS CONFIGURE_DEPENDS "${HB_ROOT}/tools/targeting/*.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HB_TARGET_INPUTS} ${HB_TARGET_GENERATORS})
    set(HB_CORE_SOURCE_ROOT "${HB_TARGET_GENERATED_ROOT}")
endif()
