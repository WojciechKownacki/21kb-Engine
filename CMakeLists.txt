cmake_minimum_required(VERSION 3.27)
project(Engine21kb LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(FetchContent)

option(KB_BUILD_EDITOR "Build the Win32 editor application and editor tests" ${WIN32})
option(KB_BUILD_RENDERER "Build the bgfx renderer module and smoke executable" ${WIN32})
option(KB_ENABLE_SANITIZERS "Build project targets with AddressSanitizer and UndefinedBehaviorSanitizer where supported" OFF)
option(KB_GENERATE_RENDERER_SHADERS "Generate renderer shaders with bgfx shaderc instead of staging prebuilt shaders" OFF)

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
endfunction()

function(kb_apply_quality_gates target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX /FS /external:anglebrackets /external:W0)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    endif()

    if(KB_ENABLE_SANITIZERS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
        else()
            message(WARNING "KB_ENABLE_SANITIZERS is not configured for ${CMAKE_CXX_COMPILER_ID}; continuing without sanitizer flags.")
        endif()
    endif()
endfunction()

set(FLECS_STATIC ON CACHE BOOL "Build Flecs as a static library" FORCE)
set(FLECS_SHARED OFF CACHE BOOL "Do not build Flecs as a shared library" FORCE)
set(FLECS_TESTS OFF CACHE BOOL "Do not build Flecs tests with the engine" FORCE)
add_subdirectory(third_party/flecs third_party/flecs EXCLUDE_FROM_ALL)

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
    if(WIN32)
        target_compile_definitions(kb_lua PRIVATE LUA_USE_WINDOWS)
    elseif(UNIX)
        target_compile_definitions(kb_lua PRIVATE LUA_USE_POSIX)
    endif()
endif()

add_subdirectory(sources/engine engine)

if(KB_BUILD_RENDERER)
    if(NOT WIN32)
        message(FATAL_ERROR "KB_BUILD_RENDERER currently requires the Win32 bgfx surface integration. Configure with -DKB_BUILD_RENDERER=OFF on non-Windows hosts.")
    endif()

    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS ${KB_GENERATE_RENDERER_SHADERS} CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_SHADER ${KB_GENERATE_RENDERER_SHADERS} CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
    set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
    set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
    kb_add_bgfx_subdirectory()

    include(CMake/KbBgfxRuntimeShaders.cmake)
    add_subdirectory(sources/renderer renderer)
    add_subdirectory(samples/render_smoke render_smoke)
endif()

if(KB_BUILD_EDITOR)
    if(NOT WIN32)
        message(FATAL_ERROR "KB_BUILD_EDITOR requires the current Win32 editor backend. Configure with -DKB_BUILD_EDITOR=OFF on non-Windows hosts.")
    endif()

    add_subdirectory(sources/editor editor)
endif()
