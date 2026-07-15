cmake_minimum_required(VERSION 3.27)

# Must be set before project() to actually take effect. Apple's libc++
# does not merely mark std::from_chars/to_chars<double> "unavailable" below
# macOS 13.3 (kb::library::TryParseDouble, EngineLibraryParsing.cpp) - the
# overload's declaration is conditionally compiled out of the header
# entirely below that deployment target, which is why
# _LIBCPP_DISABLE_AVAILABILITY (an attribute-based escape hatch) had no
# effect below. FORCEd because GitHub's macOS runner images export a
# MACOSX_DEPLOYMENT_TARGET environment variable that CMake reads into
# CMAKE_OSX_DEPLOYMENT_TARGET before this line ever runs, so a plain
# unconditional set() would still lose to that pre-populated cache entry.
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS deployment target" FORCE)
message(STATUS "kb: CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")

project(Engine21kb LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    # Belt-and-suspenders: an explicit -mmacosx-version-min flag on every
    # compile command cannot be silently dropped the way
    # CMAKE_OSX_DEPLOYMENT_TARGET's propagation apparently was (two prior
    # attempts at that variable alone left the exact same "call to deleted
    # function" error unchanged). _LIBCPP_DISABLE_AVAILABILITY stays too -
    # harmless, and still correct for any OTHER attribute-gated libc++
    # symbol this project might reach, even though it wasn't what gated
    # from_chars<double> specifically.
    add_compile_options(-mmacosx-version-min=14.0)
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
set(KB_DXCOMPILER_RUNTIME "" CACHE FILEPATH "Path to dxcompiler.dll copied next to shaderc for DXIL graph cooking")
option(KB_BUILD_PROVIDER_MODULES_AS_DLL "Build provider engine modules as hot-reloadable DLL/shared libraries instead of static libraries" ON)

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
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX /FS /external:anglebrackets /external:W0)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    endif()

    if(KB_ENABLE_SANITIZERS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer)
            target_link_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr)
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
