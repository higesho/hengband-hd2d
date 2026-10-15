# 原作から作る生成ヘッダをリポジトリの外へ作る（configure 時）。
#   presentation/bridge/modern_item_details.generated.h  … 原作 identification.cpp から（コアを組むときだけ）
#   presentation/bridge/modern_artifact_lore.generated.h … 定義ファイルと自作の英訳から（画面側の検査も使う）
# 出力先は "<HB_CORE_SOURCE_ROOT>-generated"。`presentation` をインクルードパスへ足すと
# `#include "bridge/..."` がそのまま解決する（HB_GENERATED_INCLUDE_DIR）。
# tools/targeting/targeting_sources.cmake が HB_CORE_SOURCE_ROOT を差し替える前に include すること。
# 短愚蛮怒は変愚 3.0.2.4 の写し（tangband/src）を自分のソースとして持つので、生成ヘッダも
# "<生成先>/tangband/presentation"（HB_GENERATED_INCLUDE_DIR_TANGBAND）に分けて作る
# （変愚の版上げで短愚を変えないため）。
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(DEFINED ENV{HENGBAND_CORE_GENERATED_ROOT})
    set(HB_GENERATED_ROOT "$ENV{HENGBAND_CORE_GENERATED_ROOT}")
else()
    set(HB_GENERATED_ROOT "${HB_CORE_SOURCE_ROOT}-generated")
endif()
set(HB_GENERATED_INCLUDE_DIR "${HB_GENERATED_ROOT}/presentation")
set(HB_GENERATED_INCLUDE_DIR_TANGBAND "${HB_GENERATED_ROOT}/tangband/presentation")
set(HB_GEN_TOOLS "${HB_ROOT}/tools/core_sources")
set(HB_GEN_INPUTS "${HB_GEN_TOOLS}/generate_modern_artifact_lore.py" "${HB_GEN_TOOLS}/generate_modern_item_details.py"
    "${HB_GEN_TOOLS}/paths.py" "${HB_GEN_TOOLS}/modern_artifact_lore.en.json" "${HB_ROOT}/lib/edit/ArtifactDefinitions.jsonc")
set(HB_GEN_SCRIPTS generate_modern_artifact_lore.py)
if(HENGBAND_BUILD_CORES)
    list(APPEND HB_GEN_SCRIPTS generate_modern_item_details.py)
    list(APPEND HB_GEN_INPUTS "${HB_CORE_SOURCE_ROOT}/src/perception/identification.cpp"
        "${HB_CORE_SOURCE_ROOT}/tangband/src/perception/identification.cpp" "${HB_ROOT}/tangband/lib/edit/ArtifactDefinitions.jsonc")
endif()
set(HB_GEN_VARIANTS hengband)
if(HENGBAND_BUILD_CORES)
    list(APPEND HB_GEN_VARIANTS tangband)
endif()
foreach(variant ${HB_GEN_VARIANTS})
    foreach(script ${HB_GEN_SCRIPTS})
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" "${HB_GEN_TOOLS}/${script}"
                --source-root "${HB_CORE_SOURCE_ROOT}" --output-root "${HB_GENERATED_ROOT}" --variant ${variant}
            RESULT_VARIABLE HB_GEN_RESULT)
        if(NOT HB_GEN_RESULT EQUAL 0)
            message(FATAL_ERROR "${script} --variant ${variant} failed (${HB_GEN_RESULT})")
        endif()
    endforeach()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HB_GEN_INPUTS})
