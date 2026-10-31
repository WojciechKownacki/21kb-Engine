cmake_minimum_required(VERSION 3.25)

# Exercise SDK discovery through the public CLI using a multi-config output layout
# backed by the current build's executable, headers and libraries.
file(MAKE_DIRECTORY "${TEST_ROOT}/sdk/bin/Release" "${TEST_ROOT}/sdk/engine/Release"
    "${TEST_ROOT}/sdk/third_party/flecs/Release" "${TEST_ROOT}/sdk/Release")
file(COPY_FILE "${CLI}" "${TEST_ROOT}/sdk/bin/Release/kb_cli.exe" ONLY_IF_DIFFERENT)
file(COPY_FILE "${ENGINE_LIBRARY}" "${TEST_ROOT}/sdk/engine/Release/kb_engine.lib" ONLY_IF_DIFFERENT)
file(COPY_FILE "${FLECS_LIBRARY}" "${TEST_ROOT}/sdk/third_party/flecs/Release/flecs_static.lib" ONLY_IF_DIFFERENT)
foreach(name IN ITEMS PHYSICS AUDIO LIGHTING PARTICLES AI WORLD2D)
    get_filename_component(plugin_name "${${name}_PLUGIN}" NAME)
    file(COPY_FILE "${${name}_PLUGIN}" "${TEST_ROOT}/sdk/bin/Release/${plugin_name}" ONLY_IF_DIFFERENT)
endforeach()
foreach(name IN ITEMS lua ufbx monocypher zstd recast ui_text_shaping harfbuzz sheenbidi)
    file(COPY_FILE "${${name}_LIBRARY}" "${TEST_ROOT}/sdk/Release/kb_${name}.lib" ONLY_IF_DIFFERENT)
endforeach()
if(SDK_GENERATOR STREQUAL "Ninja")
    set(SDK_GENERATOR "Ninja Multi-Config")
endif()
file(WRITE "${TEST_ROOT}/sdk/CMakeCache.txt"
    "CMAKE_HOME_DIRECTORY:INTERNAL=${ENGINE_ROOT}\nCMAKE_CONFIGURATION_TYPES:STRING=Debug;Release;RelWithDebInfo\n"
    "CMAKE_GENERATOR:INTERNAL=${SDK_GENERATOR}\nCMAKE_COMMAND:INTERNAL=${CMAKE_COMMAND}\n"
    "CMAKE_CXX_COMPILER:FILEPATH=${COMPILER}\nCMAKE_MAKE_PROGRAM:FILEPATH=${MAKE_PROGRAM}\n")

function(run_cli)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=KB_ENGINE_SDK_ROOT
        "${TEST_ROOT}/sdk/bin/Release/kb_cli.exe" ${ARGV}
        WORKING_DIRECTORY "${TEST_ROOT}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 180)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "CLI failed (${result}): ${ARGV}\n${output}\n${error}")
    endif()
    set(CLI_OUTPUT "${output}" PARENT_SCOPE)
endfunction()

# Every run gets fresh source and build outputs, without removing earlier artifacts.
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef session)
set(project "${TEST_ROOT}/${session}/SampleScene")
run_cli(new --parent "${TEST_ROOT}/${session}" --name SampleScene --template Empty)
file(MAKE_DIRECTORY "${TEST_ROOT}/sdk/bin/Debug")
file(COPY_FILE "${CLI}" "${TEST_ROOT}/sdk/bin/Debug/kb_cli.exe" ONLY_IF_DIFFERENT)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=KB_ENGINE_SDK_ROOT
    "${TEST_ROOT}/sdk/bin/Debug/kb_cli.exe" script-new --project "${project}" --name DebugProbe --language cpp
    WORKING_DIRECTORY "${TEST_ROOT}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
if(result STREQUAL "0" OR NOT error MATCHES "SDK was not found")
    message(FATAL_ERROR "A Debug executable must not select Release SDK libraries: ${output}\n${error}")
endif()
run_cli(script-new --project "${project}" --name NativeProbe --language cpp)
file(READ "${project}/Source/NativeScripts/NativeProbe/CMakeLists.txt" native_cmake)
if(NOT native_cmake MATCHES "/sdk/engine/Release/kb_engine.lib")
    message(FATAL_ERROR "The generated script did not select the multi-config SDK libraries")
endif()
set(native_source "${project}/Source/NativeScripts/NativeProbe/NativeProbe.cpp")
file(READ "${native_source}" native_code)
string(REPLACE "if (context == nullptr) { return; }"
    "if (context == nullptr) { return; }\n        std::vector<kb::scene::UITextShapedGlyph> glyphs;\n        if (kb::scene::ShapeUIText({}, {}, 0, 0, 1.0F, glyphs)) { return; }"
    native_code "${native_code}")
file(WRITE "${native_source}" "#include \"engine/ui/visual/UITextShaping.hpp\"\n${native_code}")
run_cli(script-build --project "${project}" --script /Game/Scripts/NativeProbe.native)
if(NOT EXISTS "${project}/Binaries/NativeScripts/NativeProbe.dll")
    message(FATAL_ERROR "The generated native script did not produce its module")
endif()
run_cli(entity add --project "${project}" --name NativeHost)
run_cli(scene-attach --project "${project}" --scene /Game/Scenes/Main.21kbscene
    --node NativeHost --script /Game/Scripts/NativeProbe.native)
run_cli(run --project "${project}" --scene /Game/Scenes/Main.21kbscene --frames 2)
if(NOT CLI_OUTPUT MATCHES "NativeProbe ready" OR NOT CLI_OUTPUT MATCHES "0 diagnostics")
    message(FATAL_ERROR "The built native script did not execute cleanly: ${CLI_OUTPUT}")
endif()
message(STATUS "Multi-config native SDK create/build/load/run passed")
