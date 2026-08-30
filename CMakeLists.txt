cmake_minimum_required(VERSION 3.27)

# Must be set before project() to actually take effect. FORCEd because
# GitHub's macOS runner images export a MACOSX_DEPLOYMENT_TARGET
# environment variable that CMake reads into CMAKE_OSX_DEPLOYMENT_TARGET
# before this line ever runs, so a plain unconditional set() would still
# lose to that pre-populated cache entry. A reasonably modern floor for a
# game engine target in general; NOT a fix for anything specific (see
# EngineLibraryParsing.cpp - std::from_chars<double> turned out to be
# genuinely absent from Apple's shipped libc++, not deployment-target
# gated, so no CMAKE_OSX_DEPLOYMENT_TARGET/availability-macro combination
# was ever going to restore it).
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS deployment target" FORCE)

project(Engine21kb LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    add_compile_definitions(_LIBCPP_DISABLE_AVAILABILITY)
endif()

include(CTest)
include(FetchContent)

option(KB_BUILD_EDITOR "Build the Win32 editor application and editor tests" ${WIN32})
option(KB_BUILD_RENDERER "Build the bgfx renderer module and smoke executable" ${WIN32})
option(KB_ENABLE_SANITIZERS "Build project targets with AddressSanitizer and UndefinedBehaviorSanitizer where supported" OFF)
option(KB_ENGINE_ENABLE_NATIVE_AVX2 "Opt in to AVX2 code generation for engine targets and consumers; produced binaries require AVX2-capable CPUs" OFF)
option(KB_GENERATE_RENDERER_SHADERS "Generate renderer shaders with bgfx shaderc instead of staging prebuilt shaders" OFF)
option(KB_BUILD_GRAPH_SHADERC "Build bgfx shaderc for the Material Editor graph cooker" ${KB_BUILD_EDITOR})
option(KB_STRICT_OWN_WARNINGS "Treat warnings from first-party targets as build errors" ON)
set(KB_DXCOMPILER_RUNTIME "" CACHE FILEPATH "Path to dxcompiler.dll copied next to shaderc for DXIL graph cooking")
option(KB_BUILD_PROVIDER_MODULES_AS_DLL "Build provider engine modules as hot-reloadable DLL/shared libraries instead of static libraries" ON)

set(KB_MSVC_ASAN_RUNTIME_DIR "" CACHE PATH "Directory containing the MSVC-compatible LLVM AddressSanitizer runtime")
set(KB_MSVC_STL_ASAN_RUNTIME_DIR "" CACHE PATH "Directory containing the MSVC AddressSanitizer STL runtime")
set(KB_MSVC_ASAN_DYNAMIC_RUNTIME "" CACHE FILEPATH "Path to the MSVC AddressSanitizer runtime DLL")

if(MSVC)
    get_filename_component(_kb_msvc_toolset_root "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_kb_msvc_toolset_root "${_kb_msvc_toolset_root}" DIRECTORY)
    get_filename_component(_kb_msvc_toolset_root "${_kb_msvc_toolset_root}" DIRECTORY)
    get_filename_component(_kb_msvc_toolset_root "${_kb_msvc_toolset_root}" DIRECTORY)
endif()

if(MSVC AND KB_ENABLE_SANITIZERS AND NOT KB_MSVC_ASAN_RUNTIME_DIR)
    if(EXISTS "${_kb_msvc_toolset_root}/lib/x64/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
        set(KB_MSVC_ASAN_RUNTIME_DIR "${_kb_msvc_toolset_root}/lib/x64" CACHE PATH "Directory containing the MSVC-compatible LLVM AddressSanitizer runtime" FORCE)
    else()
        file(GLOB _kb_msvc_asan_runtime_candidates
            "C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64/lib/clang/*/lib/windows"
            "C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/lib/x64"
        )
        foreach(_kb_msvc_asan_runtime_candidate IN LISTS _kb_msvc_asan_runtime_candidates)
            if(EXISTS "${_kb_msvc_asan_runtime_candidate}/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
                set(KB_MSVC_ASAN_RUNTIME_DIR "${_kb_msvc_asan_runtime_candidate}" CACHE PATH "Directory containing the MSVC-compatible LLVM AddressSanitizer runtime" FORCE)
                break()
            endif()
        endforeach()
    endif()
endif()

if(MSVC AND KB_ENABLE_SANITIZERS AND NOT KB_MSVC_ASAN_DYNAMIC_RUNTIME)
    if(EXISTS "${_kb_msvc_toolset_root}/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll")
        set(KB_MSVC_ASAN_DYNAMIC_RUNTIME "${_kb_msvc_toolset_root}/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll" CACHE FILEPATH "Path to the MSVC AddressSanitizer runtime DLL" FORCE)
    else()
        file(GLOB _kb_msvc_asan_dynamic_runtime_candidates
            "C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll"
        )
        if(_kb_msvc_asan_dynamic_runtime_candidates)
            list(SORT _kb_msvc_asan_dynamic_runtime_candidates COMPARE NATURAL ORDER DESCENDING)
            list(GET _kb_msvc_asan_dynamic_runtime_candidates 0 _kb_msvc_asan_dynamic_runtime_candidate)
            if(EXISTS "${_kb_msvc_asan_dynamic_runtime_candidate}")
                set(KB_MSVC_ASAN_DYNAMIC_RUNTIME "${_kb_msvc_asan_dynamic_runtime_candidate}" CACHE FILEPATH "Path to the MSVC AddressSanitizer runtime DLL" FORCE)
            endif()
        endif()
    endif()
endif()

if(MSVC AND KB_ENABLE_SANITIZERS AND NOT KB_MSVC_STL_ASAN_RUNTIME_DIR)
    if(EXISTS "${_kb_msvc_toolset_root}/lib/x64/stl_asan.lib")
        set(KB_MSVC_STL_ASAN_RUNTIME_DIR "${_kb_msvc_toolset_root}/lib/x64" CACHE PATH "Directory containing the MSVC AddressSanitizer STL runtime" FORCE)
    else()
        file(GLOB _kb_msvc_stl_asan_runtime_candidates
            "C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/lib/x64"
        )
        foreach(_kb_msvc_stl_asan_runtime_candidate IN LISTS _kb_msvc_stl_asan_runtime_candidates)
            if(EXISTS "${_kb_msvc_stl_asan_runtime_candidate}/stl_asan.lib")
                set(KB_MSVC_STL_ASAN_RUNTIME_DIR "${_kb_msvc_stl_asan_runtime_candidate}" CACHE PATH "Directory containing the MSVC AddressSanitizer STL runtime" FORCE)
                break()
            endif()
        endforeach()
    endif()
endif()

if(WIN32 AND NOT KB_DXCOMPILER_RUNTIME AND DEFINED ENV{VULKAN_SDK})
    set(_kb_vulkan_sdk_dxcompiler "$ENV{VULKAN_SDK}/Bin/dxcompiler.dll")
    if(EXISTS "${_kb_vulkan_sdk_dxcompiler}")
        set(KB_DXCOMPILER_RUNTIME "${_kb_vulkan_sdk_dxcompiler}" CACHE FILEPATH "Path to dxcompiler.dll copied next to shaderc for DXIL graph cooking" FORCE)
    endif()
endif()

function(kb_add_bgfx_subdirectory)
    if(MSVC)
        set(_kb_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
        string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(REPLACE "/EHs" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(REPLACE "/EHc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        add_subdirectory(third_party/bgfx.cmake third_party/bgfx.cmake EXCLUDE_FROM_ALL)
        set(CMAKE_CXX_FLAGS "${_kb_saved_cxx_flags}" PARENT_SCOPE)
    else()
        add_subdirectory(third_party/bgfx.cmake third_party/bgfx.cmake EXCLUDE_FROM_ALL)
    endif()

    if(TARGET bimg_decode)
        get_target_property(_kb_bimg_decode_sources bimg_decode SOURCES)
        if(_kb_bimg_decode_sources)
            list(FILTER _kb_bimg_decode_sources EXCLUDE REGEX "miniz\\.c$")
            set_target_properties(bimg_decode PROPERTIES SOURCES "${_kb_bimg_decode_sources}")
        endif()
        if(MSVC)
            target_compile_options(bimg_decode PRIVATE /EHsc)
        endif()
    endif()

    if(MSVC)
        foreach(_kb_bgfx_exception_target IN ITEMS glslang spirv-opt spirv-cross tint shaderc)
            if(TARGET ${_kb_bgfx_exception_target})
                target_compile_options(${_kb_bgfx_exception_target} PRIVATE /EHsc /FS)
            endif()
        endforeach()
        # Tint is third-party Dawn code built as part of bgfx. MSVC 14.44 emits
        # C4715 for exhaustive internal switch lambdas that terminate on every
        # unmatched branch; suppress only this upstream diagnostic on its target.
        if(TARGET tint)
            target_compile_options(tint PRIVATE /wd4715)
        endif()
        if(TARGET glsl-optimizer)
            target_compile_options(glsl-optimizer PRIVATE /FS)
        endif()
    endif()
endfunction()

function(kb_apply_quality_gates target_name)
    # This function is called only by first-party kb_* targets. Vendor projects
    # are added separately and intentionally do not inherit these flags: their
    # pre-existing diagnostics must stay visible without blocking a clean build
    # of engine-owned code.
    if(KB_STRICT_OWN_WARNINGS)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /W4 /WX /FS /external:anglebrackets /external:W0)
        else()
            target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
        endif()
    endif()

    if(KB_ENABLE_SANITIZERS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer)
            target_link_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr)
        elseif(MSVC)
            if(NOT EXISTS "${KB_MSVC_ASAN_RUNTIME_DIR}/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib" OR
                NOT EXISTS "${KB_MSVC_STL_ASAN_RUNTIME_DIR}/stl_asan.lib" OR
                NOT EXISTS "${KB_MSVC_ASAN_DYNAMIC_RUNTIME}")
                message(FATAL_ERROR "KB_ENABLE_SANITIZERS requires MSVC AddressSanitizer libraries and runtime DLL; set KB_MSVC_ASAN_RUNTIME_DIR, KB_MSVC_STL_ASAN_RUNTIME_DIR, and KB_MSVC_ASAN_DYNAMIC_RUNTIME.")
            endif()
            target_compile_options(${target_name} PRIVATE /fsanitize=address /Zi)
            # The vendored bgfx/bimg archives are intentionally not rebuilt with
            # project sanitizer flags. Keep the MSVC STL annotation ABI uniform
            # across first-party and vendor objects while retaining ASan for the
            # first-party code under test.
            target_compile_definitions(${target_name} PRIVATE
                _DISABLE_STRING_ANNOTATION
                _DISABLE_VECTOR_ANNOTATION
                _DISABLE_OPTIONAL_ANNOTATION
            )
            target_link_options(${target_name} PRIVATE /INCREMENTAL:NO)
            target_link_directories(${target_name} PRIVATE "${KB_MSVC_ASAN_RUNTIME_DIR}")
            target_link_directories(${target_name} PRIVATE "${KB_MSVC_STL_ASAN_RUNTIME_DIR}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${KB_MSVC_ASAN_DYNAMIC_RUNTIME}"
                    "$<TARGET_FILE_DIR:${target_name}>"
            )
        else()
            message(WARNING "KB_ENABLE_SANITIZERS is not configured for ${CMAKE_CXX_COMPILER_ID}; continuing without sanitizer flags.")
        endif()
    endif()
endfunction()

set(FLECS_STATIC ON CACHE BOOL "Build Flecs as a static library" FORCE)
set(FLECS_SHARED OFF CACHE BOOL "Do not build Flecs as a shared library" FORCE)
set(FLECS_TESTS OFF CACHE BOOL "Do not build Flecs tests with the engine" FORCE)
add_subdirectory(third_party/flecs third_party/flecs EXCLUDE_FROM_ALL)

set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "Do not enable Jolt warnings for the vendored build" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "Do not enable Jolt LTO from the vendored build" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "Do not let Jolt override project compiler flags" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Build Jolt with the project MSVC runtime library" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "Do not build Jolt unit tests" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "Do not build Jolt HelloWorld" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "Do not build Jolt performance test" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "Do not build Jolt samples" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "Do not build Jolt viewer" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "Do not build Jolt DirectX compute support" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "Do not build Jolt Vulkan compute support" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "Do not build Jolt Metal compute support" FORCE)
add_subdirectory(third_party/jolt/Build third_party/jolt EXCLUDE_FROM_ALL)
if(TARGET Jolt AND NOT WIN32)
    set_target_properties(Jolt PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "Do not build miniaudio examples" FORCE)
set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "Do not build miniaudio tests" FORCE)
set(MINIAUDIO_BUILD_TOOLS OFF CACHE BOOL "Do not build miniaudio tools" FORCE)
set(MINIAUDIO_INSTALL OFF CACHE BOOL "Do not install vendored miniaudio" FORCE)
add_subdirectory(third_party/miniaudio third_party/miniaudio EXCLUDE_FROM_ALL)

# ufbx is intentionally built as a small isolated C library. The engine owns
# conversion into its canonical skeletal asset types; no ufbx types escape the
# importer implementation.
add_library(kb_ufbx STATIC
    third_party/ufbx/ufbx.c
)
target_include_directories(kb_ufbx PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/ufbx
)
if(NOT WIN32)
    set_target_properties(kb_ufbx PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

FetchContent_Declare(
    kb_lua
    URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
    URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
)
FetchContent_MakeAvailable(kb_lua)
if(NOT TARGET kb_lua)
    add_library(kb_lua STATIC
        ${kb_lua_SOURCE_DIR}/src/lapi.c
        ${kb_lua_SOURCE_DIR}/src/lauxlib.c
        ${kb_lua_SOURCE_DIR}/src/lbaselib.c
        ${kb_lua_SOURCE_DIR}/src/lcode.c
        ${kb_lua_SOURCE_DIR}/src/lcorolib.c
        ${kb_lua_SOURCE_DIR}/src/lctype.c
        ${kb_lua_SOURCE_DIR}/src/ldblib.c
        ${kb_lua_SOURCE_DIR}/src/ldebug.c
        ${kb_lua_SOURCE_DIR}/src/ldo.c
        ${kb_lua_SOURCE_DIR}/src/ldump.c
        ${kb_lua_SOURCE_DIR}/src/lfunc.c
        ${kb_lua_SOURCE_DIR}/src/lgc.c
        ${kb_lua_SOURCE_DIR}/src/linit.c
        ${kb_lua_SOURCE_DIR}/src/llex.c
        ${kb_lua_SOURCE_DIR}/src/lmathlib.c
        ${kb_lua_SOURCE_DIR}/src/lmem.c
        ${kb_lua_SOURCE_DIR}/src/lobject.c
        ${kb_lua_SOURCE_DIR}/src/lopcodes.c
        ${kb_lua_SOURCE_DIR}/src/lparser.c
        ${kb_lua_SOURCE_DIR}/src/lstate.c
        ${kb_lua_SOURCE_DIR}/src/lstring.c
        ${kb_lua_SOURCE_DIR}/src/lstrlib.c
        ${kb_lua_SOURCE_DIR}/src/ltable.c
        ${kb_lua_SOURCE_DIR}/src/ltablib.c
        ${kb_lua_SOURCE_DIR}/src/ltm.c
        ${kb_lua_SOURCE_DIR}/src/lundump.c
        ${kb_lua_SOURCE_DIR}/src/lutf8lib.c
        ${kb_lua_SOURCE_DIR}/src/lvm.c
        ${kb_lua_SOURCE_DIR}/src/lzio.c
    )
    target_include_directories(kb_lua PUBLIC ${kb_lua_SOURCE_DIR}/src)
    if(NOT WIN32)
        set_target_properties(kb_lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    if(WIN32)
        target_compile_definitions(kb_lua PRIVATE LUA_USE_WINDOWS)
    elseif(UNIX)
        target_compile_definitions(kb_lua PRIVATE LUA_USE_POSIX)
    endif()
endif()

add_subdirectory(sources/engine engine)
add_subdirectory(sources/tools tools)
add_subdirectory(sources/plugins/physics_jolt physics_jolt)
add_subdirectory(sources/plugins/audio_miniaudio audio_miniaudio)
add_subdirectory(sources/plugins/basic_lighting basic_lighting)
add_subdirectory(sources/plugins/21kb_particle 21kb_particle)
if(BUILD_TESTING AND TARGET kb_21kb_particle_stage2_tests)
    target_link_libraries(kb_21kb_particle_stage2_tests PRIVATE kb_21kb_particle_core)
    if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
        add_dependencies(kb_21kb_particle_stage2_tests kb_21kb_particle_plugin)
        target_compile_definitions(kb_21kb_particle_stage2_tests PRIVATE
            "KB_21KB_PARTICLE_PLUGIN_PATH=\"$<TARGET_FILE:kb_21kb_particle_plugin>\"")
    endif()
endif()
add_subdirectory(sources/plugins/terrain_editor terrain_editor)

if(TARGET kb_engine_tests)
    add_dependencies(kb_engine_tests kb_physics_jolt_plugin kb_audio_miniaudio_plugin kb_basic_lighting_plugin)
    if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
        set(KB_PHYSICS_JOLT_TEST_PLUGIN_PATH "$<TARGET_FILE:kb_physics_jolt_plugin>")
        set(KB_AUDIO_MINIAUDIO_TEST_PLUGIN_PATH "$<TARGET_FILE:kb_audio_miniaudio_plugin>")
        set(KB_BASIC_LIGHTING_TEST_PLUGIN_PATH "$<TARGET_FILE:kb_basic_lighting_plugin>")
    else()
        set(KB_PHYSICS_JOLT_TEST_PLUGIN_PATH "")
        set(KB_AUDIO_MINIAUDIO_TEST_PLUGIN_PATH "")
        set(KB_BASIC_LIGHTING_TEST_PLUGIN_PATH "")
    endif()
    target_compile_definitions(kb_engine_tests
        PRIVATE
            "KB_PHYSICS_JOLT_PLUGIN_PATH=\"${KB_PHYSICS_JOLT_TEST_PLUGIN_PATH}\""
            "KB_AUDIO_MINIAUDIO_PLUGIN_PATH=\"${KB_AUDIO_MINIAUDIO_TEST_PLUGIN_PATH}\""
            "KB_BASIC_LIGHTING_PLUGIN_PATH=\"${KB_BASIC_LIGHTING_TEST_PLUGIN_PATH}\""
    )
endif()

if(TARGET kb_cli_tests)
    add_dependencies(kb_cli_tests kb_physics_jolt_plugin)
    if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
        set(KB_PHYSICS_JOLT_CLI_TEST_PLUGIN_PATH "$<TARGET_FILE:kb_physics_jolt_plugin>")
    else()
        set(KB_PHYSICS_JOLT_CLI_TEST_PLUGIN_PATH "")
    endif()
    target_compile_definitions(kb_cli_tests
        PRIVATE
            "KB_PHYSICS_JOLT_PLUGIN_PATH=\"${KB_PHYSICS_JOLT_CLI_TEST_PLUGIN_PATH}\""
    )
endif()

if(KB_BUILD_RENDERER)
    if(NOT WIN32)
        message(FATAL_ERROR "KB_BUILD_RENDERER currently requires the Win32 bgfx surface integration. Configure with -DKB_BUILD_RENDERER=OFF on non-Windows hosts.")
    endif()

    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    # Authored Material Graphs are compiled by the editor at runtime. That is
    # independent of generating staged renderer shaders, so editor builds keep
    # shaderc available even when they consume prebuilt renderer shaders.
    set(_kb_build_bgfx_shaderc OFF)
    if(KB_GENERATE_RENDERER_SHADERS OR KB_BUILD_GRAPH_SHADERC)
        set(_kb_build_bgfx_shaderc ON)
    endif()
    set(BGFX_BUILD_TOOLS ${_kb_build_bgfx_shaderc} CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_SHADER ${_kb_build_bgfx_shaderc} CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
    set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
    set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
    kb_add_bgfx_subdirectory()

    include(CMake/KbBgfxRuntimeShaders.cmake)
    add_subdirectory(sources/renderer renderer)
    add_subdirectory(sources/game game)
    add_subdirectory(samples/render_smoke render_smoke)
    add_subdirectory(samples/standalone_player standalone_player)
endif()

if(KB_BUILD_EDITOR)
    if(NOT WIN32)
        message(FATAL_ERROR "KB_BUILD_EDITOR requires the current Win32 editor backend. Configure with -DKB_BUILD_EDITOR=OFF on non-Windows hosts.")
    endif()

    add_subdirectory(sources/editor editor)
    add_subdirectory(sources/hub hub)
endif()
