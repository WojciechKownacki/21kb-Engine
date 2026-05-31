cmake_minimum_required(VERSION 3.27)
project(Engine21kb LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(KB_BUILD_EDITOR "Build the Win32 editor application and editor tests" ${WIN32})
option(KB_ENABLE_SANITIZERS "Build project targets with AddressSanitizer and UndefinedBehaviorSanitizer where supported" OFF)

function(kb_apply_quality_gates target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX /FS)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
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

add_subdirectory(sources/engine engine)

if(KB_BUILD_EDITOR)
    if(NOT WIN32)
        message(FATAL_ERROR "KB_BUILD_EDITOR requires the current Win32 editor backend. Configure with -DKB_BUILD_EDITOR=OFF on non-Windows hosts.")
    endif()

    add_subdirectory(sources/editor editor)
endif()
