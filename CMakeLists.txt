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

# Release objects carry debug information, so a shipped binary can be linked
# with a PDB (see the shipped targets at the end of this file): a crash report
# from a player is only readable with the PDB of that exact build. Embedded keeps
# parallel compiles from sharing one compiler PDB. Sanitizer builds choose their
# own format.
if(MSVC AND NOT KB_ENABLE_SANITIZERS AND NOT DEFINED CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>$<$<CONFIG:Release>:Embedded>")
    # Jolt would otherwise add its own /Zi on top of the format chosen here.
    set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "Jolt debug information follows CMAKE_MSVC_DEBUG_INFORMATION_FORMAT" FORCE)
endif()

include(CTest)
include(FetchContent)

option(KB_BUILD_EDITOR "Build the Win32 editor application and editor tests" ${WIN32})
option(KB_BUILD_RENDERER "Build the bgfx renderer module and smoke executable" ${WIN32})
option(KB_BUILD_ANDROID_HOST "Build the GameActivity shared-library host" OFF)
option(KB_BUILD_PACKAGED_GAME_HOST "Build the monolithic platform game host" OFF)
set(KB_ANDROID_TEXTURE_PROFILE "" CACHE STRING "Android package texture profile: ASTC or ETC2")
set_property(CACHE KB_ANDROID_TEXTURE_PROFILE PROPERTY STRINGS ASTC ETC2)
set(KB_WEB_RENDERER "WEBGL" CACHE STRING "Browser package renderer: WEBGL or WEBGPU")
set_property(CACHE KB_WEB_RENDERER PROPERTY STRINGS WEBGL WEBGPU)
option(KB_ENABLE_SANITIZERS "Build project targets with AddressSanitizer and UndefinedBehaviorSanitizer where supported" OFF)
option(KB_BUILD_FUZZERS "Build the libFuzzer harnesses in fuzz/ (requires KB_ENABLE_SANITIZERS; build the kb_fuzzers target)" OFF)
if(KB_BUILD_FUZZERS AND NOT KB_ENABLE_SANITIZERS)
    message(FATAL_ERROR "KB_BUILD_FUZZERS requires KB_ENABLE_SANITIZERS=ON")
endif()
option(KB_ENGINE_ENABLE_NATIVE_AVX2 "Opt in to AVX2 code generation for engine targets and consumers; produced binaries require AVX2-capable CPUs" OFF)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    option(KB_ENGINE_TRANSFORM_LTO "Opt in to transform-only MSVC Release LTO; requires matching-toolset consumers" OFF)
endif()
option(KB_GENERATE_RENDERER_SHADERS "Generate renderer shaders with bgfx shaderc instead of staging prebuilt shaders" OFF)
option(KB_BUILD_GRAPH_SHADERC "Build bgfx shaderc for the Material Editor graph cooker" ${KB_BUILD_EDITOR})
option(KB_STRICT_OWN_WARNINGS "Treat warnings from first-party targets as build errors" ON)
set(KB_DXCOMPILER_RUNTIME "" CACHE FILEPATH "Path to dxcompiler.dll copied next to shaderc for DXIL graph cooking")
option(KB_BUILD_PROVIDER_MODULES_AS_DLL "Build provider engine modules as hot-reloadable DLL/shared libraries instead of static libraries" ON)

option(KB_HARDENING "Build with exploit mitigations" ON)
option(KB_HARDENING_SHADOW_STACK "Mark Windows images compatible with hardware-enforced shadow stacks (CET); costs CPU time in call-heavy engine code" OFF)

# Exploit mitigations for everything the build produces, third-party code included, so they cover a whole
# player: Control Flow Guard, ASLR and DEP on MSVC (shadow stacks on request); stack protector, fortified libc calls,
# position-independent executables and read-only relocations with GCC and Clang. AddressSanitizer builds
# leave out the ones it cannot be combined with.
if(KB_HARDENING AND NOT EMSCRIPTEN)
    if(MSVC)
        if(NOT KB_ENABLE_SANITIZERS)
            add_compile_options(/guard:cf)
            add_link_options(/GUARD:CF)
        endif()
        add_link_options(/DYNAMICBASE /NXCOMPAT)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            add_link_options(/HIGHENTROPYVA)
        endif()
        # Shadow stacks check every return; in the engine's call-heavy loops that measured 10-20% slower,
        # so a game opts in with KB_HARDENING_SHADOW_STACK when it can afford that.
        if(KB_HARDENING_SHADOW_STACK AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|X86)$")
            add_link_options(/CETCOMPAT)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fstack-protector-strong)
        if(NOT KB_ENABLE_SANITIZERS)
            add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>)
        endif()
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        include(CheckPIESupported)
        check_pie_supported()
        if(NOT APPLE)
            add_link_options(LINKER:-z,relro LINKER:-z,now LINKER:-z,noexecstack)
        endif()
    endif()
endif()

if((ANDROID AND KB_BUILD_ANDROID_HOST) OR
   ((EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Linux") AND KB_BUILD_PACKAGED_GAME_HOST))
    # Packaged hosts construct their four supported providers directly. A DLL
    # provider would make the package depend on an untracked side module.
    set(KB_BUILD_PROVIDER_MODULES_AS_DLL OFF CACHE BOOL "" FORCE)
endif()

if(ANDROID AND KB_BUILD_ANDROID_HOST AND
   NOT KB_ANDROID_TEXTURE_PROFILE MATCHES "^(ASTC|ETC2)$")
    message(FATAL_ERROR "KB_ANDROID_TEXTURE_PROFILE must be ASTC or ETC2")
endif()
if(ANDROID AND KB_BUILD_ANDROID_HOST AND NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "The packaged Android game host requires ANDROID_ABI=arm64-v8a")
endif()
if(EMSCRIPTEN AND NOT KB_WEB_RENDERER MATCHES "^(WEBGL|WEBGPU)$")
    message(FATAL_ERROR "KB_WEB_RENDERER must be WEBGL or WEBGPU")
endif()

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

# The Windows SDK ships the same compiler, and it is present on machines that build this at all -
# including CI, which has no Vulkan SDK. Without it the DXIL profile silently gets no compiler and
# the failure only surfaces much later, as "Unable to load DXC compiler" from a cook.
if(WIN32 AND NOT KB_DXCOMPILER_RUNTIME)
    file(GLOB _kb_windows_sdk_dxcompilers
        "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x64/dxcompiler.dll"
        "$ENV{ProgramFiles}/Windows Kits/10/bin/*/x64/dxcompiler.dll")
    if(_kb_windows_sdk_dxcompilers)
        list(SORT _kb_windows_sdk_dxcompilers)
        list(GET _kb_windows_sdk_dxcompilers -1 _kb_windows_sdk_dxcompiler)
        set(KB_DXCOMPILER_RUNTIME "${_kb_windows_sdk_dxcompiler}" CACHE FILEPATH "Path to dxcompiler.dll copied next to shaderc for DXIL graph cooking" FORCE)
    endif()
endif()

# Say so at configure time rather than letting a cook fail with a compiler that was never copied.
if(WIN32 AND NOT KB_DXCOMPILER_RUNTIME
        AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/tools/bin/windows/dxcompiler.dll")
    message(WARNING "No dxcompiler.dll found (Vulkan SDK or Windows SDK). DXIL shader cooking will fail; "
                    "set -DKB_DXCOMPILER_RUNTIME=<path to dxcompiler.dll> to enable it.")
endif()

# The renderer budgets shadow cascades and point-light shadow faces as dedicated views.
set(BGFX_CONFIG_MAX_VIEWS 512 CACHE STRING "" FORCE)

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
        # An image file states its own size and the decoders allocate for it before
        # reading a pixel. Nothing wider or taller than the largest GPU texture is
        # usable, so the stb decoders refuse such a header instead of allocating.
        target_compile_definitions(bimg_decode PRIVATE STBI_MAX_DIMENSIONS=16384)
    endif()
    # The image decoders behind texture import are fuzzed with the engine code that calls them.
    foreach(_kb_bimg_target IN ITEMS bimg bimg_decode)
        kb_apply_fuzz_instrumentation(${_kb_bimg_target})
    endforeach()

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

    kb_apply_fuzz_coverage(${target_name})
endfunction()

# Coverage feedback for libFuzzer. Instrumented objects call back into the fuzzer
# runtime, so a fuzzing tree builds the kb_fuzzers target rather than everything.
function(kb_apply_fuzz_coverage target_name)
    if(NOT KB_BUILD_FUZZERS)
        return()
    endif()
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /fsanitize-coverage=inline-8bit-counters
            /fsanitize-coverage=edge
            /fsanitize-coverage=trace-cmp
            /fsanitize-coverage=trace-div)
    else()
        target_compile_options(${target_name} PRIVATE -fsanitize=fuzzer-no-link)
    endif()
endfunction()

# Vendored parsers reached by the fuzz harnesses get the same address checking and
# coverage as first-party code; their warnings stay unmanaged.
function(kb_apply_fuzz_instrumentation target_name)
    if(NOT KB_BUILD_FUZZERS OR NOT TARGET ${target_name})
        return()
    endif()
    if(MSVC)
        # /Zi so a report inside the vendored code names its lines.
        target_compile_options(${target_name} PRIVATE /fsanitize=address /Zi)
        target_compile_definitions(${target_name} PRIVATE
            _DISABLE_STRING_ANNOTATION
            _DISABLE_VECTOR_ANNOTATION
            _DISABLE_OPTIONAL_ANNOTATION
        )
    else()
        target_compile_options(${target_name} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    endif()
    kb_apply_fuzz_coverage(${target_name})
endfunction()

set(FLECS_STATIC ON CACHE BOOL "Build Flecs as a static library" FORCE)
set(FLECS_SHARED OFF CACHE BOOL "Do not build Flecs as a shared library" FORCE)
set(FLECS_TESTS OFF CACHE BOOL "Do not build Flecs tests with the engine" FORCE)
add_subdirectory(third_party/flecs third_party/flecs EXCLUDE_FROM_ALL)
if(EMSCRIPTEN)
    # The browser player uses Flecs storage, systems and its pipeline. Exclude
    # editor-facing addons (HTTP, REST, JSON, script, metrics and units) so
    # world startup does not bootstrap unused modules on the browser main loop.
    target_compile_definitions(flecs_static PUBLIC
        FLECS_CUSTOM_BUILD
        FLECS_LOG
        FLECS_MODULE
        FLECS_NO_OS_API_IMPL
        FLECS_PIPELINE
        FLECS_SYSTEM
    )
endif()

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
# World positions are double precision (docs/large_worlds.md). Jolt publishes JPH_DOUBLE_PRECISION as a PUBLIC
# definition of its target, so every target that links Jolt and includes its headers compiles the same RVec3.
set(DOUBLE_PRECISION ON CACHE BOOL "Build Jolt with double-precision world positions" FORCE)
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
kb_apply_fuzz_instrumentation(kb_ufbx)

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

# Every cryptographic primitive the engine uses (Ed25519, SHA-512, HMAC-SHA512,
# XChaCha20-Poly1305) comes from this audited release. The engine reaches it only
# through engine/security/Crypto.hpp and never implements a primitive itself.
FetchContent_Declare(
    kb_monocypher
    URL https://monocypher.org/download/monocypher-4.0.2.tar.gz
    URL_HASH SHA256=38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c
)
FetchContent_MakeAvailable(kb_monocypher)
if(NOT TARGET kb_monocypher)
    add_library(kb_monocypher STATIC
        ${kb_monocypher_SOURCE_DIR}/src/monocypher.c
        ${kb_monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c
    )
    target_include_directories(kb_monocypher PUBLIC
        ${kb_monocypher_SOURCE_DIR}/src
        ${kb_monocypher_SOURCE_DIR}/src/optional
    )
    if(NOT WIN32)
        set_target_properties(kb_monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endif()

# Per-block compression of .kbpack containers. Only the single-threaded compressor and the
# decompressor of the official release tarball are built; the legacy formats, the dictionary
# builder and the hand-written x86-64 assembly decoder are left out, so the library compiles
# the same way on every compiler and platform the engine targets.
FetchContent_Declare(
    kb_zstd
    URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
)
FetchContent_MakeAvailable(kb_zstd)
if(NOT TARGET kb_zstd)
    add_library(kb_zstd STATIC
        ${kb_zstd_SOURCE_DIR}/lib/common/debug.c
        ${kb_zstd_SOURCE_DIR}/lib/common/entropy_common.c
        ${kb_zstd_SOURCE_DIR}/lib/common/error_private.c
        ${kb_zstd_SOURCE_DIR}/lib/common/fse_decompress.c
        ${kb_zstd_SOURCE_DIR}/lib/common/pool.c
        ${kb_zstd_SOURCE_DIR}/lib/common/threading.c
        ${kb_zstd_SOURCE_DIR}/lib/common/xxhash.c
        ${kb_zstd_SOURCE_DIR}/lib/common/zstd_common.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/fse_compress.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/hist.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/huf_compress.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/zstd_compress.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/zstd_compress_literals.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/zstd_compress_sequences.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/zstd_compress_superblock.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/zstd_double_fast.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/zstd_fast.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/zstd_lazy.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/zstd_ldm.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/zstd_opt.c
        ${kb_zstd_SOURCE_DIR}/lib/compress/zstd_preSplit.c
        ${kb_zstd_SOURCE_DIR}/lib/decompress/huf_decompress.c
        ${kb_zstd_SOURCE_DIR}/lib/decompress/zstd_ddict.c
        ${kb_zstd_SOURCE_DIR}/lib/decompress/zstd_decompress.c
        ${kb_zstd_SOURCE_DIR}/lib/decompress/zstd_decompress_block.c
    )
    target_include_directories(kb_zstd PUBLIC ${kb_zstd_SOURCE_DIR}/lib)
    target_compile_definitions(kb_zstd PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_LEGACY_SUPPORT=0)
    if(NOT WIN32)
        set_target_properties(kb_zstd PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    kb_apply_fuzz_instrumentation(kb_zstd)
endif()

# Navigation mesh generation (Recast), tiled navigation meshes and path queries (Detour), path
# corridors and local avoidance (DetourCrowd) and the compact layers a tile is rebuilt from when
# obstacles change (DetourTileCache builder). Only the libraries of the official release tarball
# are compiled; its own CMake project (demo, tests) is never added. Polygon references are 64-bit
# so a streamed world can hold millions of tiles, and query filters are virtual so the engine's
# 32 area types filter paths exactly.
FetchContent_Declare(
    kb_recast
    URL https://github.com/recastnavigation/recastnavigation/archive/refs/tags/v1.6.0.tar.gz
    URL_HASH SHA256=d48ca0121962fa0639502c0f56c4e3ae72f98e55d88727225444f500775c0074
    SOURCE_SUBDIR kb-libraries-only
)
FetchContent_MakeAvailable(kb_recast)
if(NOT TARGET kb_recast)
    add_library(kb_recast STATIC
        ${kb_recast_SOURCE_DIR}/Recast/Source/Recast.cpp
        ${kb_recast_SOURCE_DIR}/Recast/Source/RecastAlloc.cpp
        ${kb_recast_SOURCE_DIR}/Recast/Source/RecastArea.cpp
        ${kb_recast_SOURCE_DIR}/Recast/Source/RecastAssert.cpp
        ${kb_recast_SOURCE_DIR}/Recast/Source/RecastContour.cpp
        ${kb_recast_SOURCE_DIR}/Recast/Source/RecastFilter.cpp
        ${kb_recast_SOURCE_DIR}/Recast/Source/RecastLayers.cpp
        ${kb_recast_SOURCE_DIR}/Recast/Source/RecastMesh.cpp
        ${kb_recast_SOURCE_DIR}/Recast/Source/RecastMeshDetail.cpp
        ${kb_recast_SOURCE_DIR}/Recast/Source/RecastRasterization.cpp
        ${kb_recast_SOURCE_DIR}/Recast/Source/RecastRegion.cpp
        ${kb_recast_SOURCE_DIR}/Detour/Source/DetourAlloc.cpp
        ${kb_recast_SOURCE_DIR}/Detour/Source/DetourAssert.cpp
        ${kb_recast_SOURCE_DIR}/Detour/Source/DetourCommon.cpp
        ${kb_recast_SOURCE_DIR}/Detour/Source/DetourNavMesh.cpp
        ${kb_recast_SOURCE_DIR}/Detour/Source/DetourNavMeshBuilder.cpp
        ${kb_recast_SOURCE_DIR}/Detour/Source/DetourNavMeshQuery.cpp
        ${kb_recast_SOURCE_DIR}/Detour/Source/DetourNode.cpp
        ${kb_recast_SOURCE_DIR}/DetourCrowd/Source/DetourLocalBoundary.cpp
        ${kb_recast_SOURCE_DIR}/DetourCrowd/Source/DetourObstacleAvoidance.cpp
        ${kb_recast_SOURCE_DIR}/DetourCrowd/Source/DetourPathCorridor.cpp
        ${kb_recast_SOURCE_DIR}/DetourCrowd/Source/DetourProximityGrid.cpp
        ${kb_recast_SOURCE_DIR}/DetourTileCache/Source/DetourTileCacheBuilder.cpp
    )
    target_include_directories(kb_recast PUBLIC
        ${kb_recast_SOURCE_DIR}/Recast/Include
        ${kb_recast_SOURCE_DIR}/Detour/Include
        ${kb_recast_SOURCE_DIR}/DetourCrowd/Include
        ${kb_recast_SOURCE_DIR}/DetourTileCache/Include
    )
    target_compile_definitions(kb_recast PUBLIC DT_POLYREF64=1 DT_VIRTUAL_QUERYFILTER=1)
    if(NOT WIN32)
        set_target_properties(kb_recast PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    kb_apply_fuzz_instrumentation(kb_recast)
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
    if(NOT WIN32 AND NOT ANDROID AND NOT EMSCRIPTEN AND
       NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "KB_BUILD_RENDERER currently supports Win32, Linux, Android, and Emscripten browser integrations. Configure with -DKB_BUILD_RENDERER=OFF on other hosts.")
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
    if(EMSCRIPTEN)
        if(KB_WEB_RENDERER STREQUAL "WEBGPU")
            set(BGFX_CONFIG_RENDERER_WEBGPU ON CACHE BOOL "" FORCE)
        else()
            set(BGFX_CONFIG_RENDERER_WEBGPU OFF CACHE BOOL "" FORCE)
        endif()
    endif()
    kb_add_bgfx_subdirectory()
    if(ANDROID AND KB_BUILD_PACKAGED_GAME_HOST)
        target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_DEBUG_OBJECT_NAME=0)
    endif()
    if(EMSCRIPTEN AND KB_WEB_RENDERER STREQUAL "WEBGL")
        target_compile_definitions(bgfx PRIVATE BGFX_GL_CONFIG_BLIT_EMULATION=1)
    endif()

    include(CMake/KbBgfxRuntimeShaders.cmake)
    add_subdirectory(sources/renderer renderer)
    # The terrain editor imports heightmaps through bimg_decode, which comes with the renderer.
    add_subdirectory(sources/plugins/terrain_editor terrain_editor)
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

if(KB_BUILD_FUZZERS)
    if(NOT KB_BUILD_RENDERER)
        message(FATAL_ERROR "KB_BUILD_FUZZERS needs KB_BUILD_RENDERER for the mesh, texture, font and material readers")
    endif()
    add_subdirectory(fuzz)
endif()

# Everything packaging ships links with a full PDB in Release too, and so does the
# crash test helper that stands in for it; the packager files each PDB into a
# symbol store outside the shipped folder. /DEBUG turns off the Release reference
# and folding optimisations, so they are named again.
if(MSVC)
    foreach(_kb_shipped_target IN ITEMS
            kb_game kb_physics_jolt_plugin kb_audio_miniaudio_plugin kb_basic_lighting_plugin
            kb_21kb_particle_plugin kb_crash_reporting_test_helper)
        if(TARGET ${_kb_shipped_target})
            get_target_property(_kb_shipped_type ${_kb_shipped_target} TYPE)
            if(_kb_shipped_type MATCHES "^(EXECUTABLE|SHARED_LIBRARY|MODULE_LIBRARY)$")
                target_link_options(${_kb_shipped_target} PRIVATE
                    "$<$<CONFIG:Release>:/DEBUG:FULL;/OPT:REF;/OPT:ICF>")
            endif()
        endif()
    endforeach()
endif()

# Optional performance harness; use benchmarks/openworld for the repository harness.
set(KB_PERFORMANCE_SOURCE_DIR "" CACHE PATH "Directory containing the performance harness")
if(KB_PERFORMANCE_SOURCE_DIR)
    add_subdirectory("${KB_PERFORMANCE_SOURCE_DIR}" performance)
endif()

# Each test gets its own temporary directory, so tests that ctest runs in parallel never share a file in the
# system temporary directory. The directories stay in the system temporary directory, outside any source
# tree, because some tests check that what they write is not inside a project or a repository. Setting a
# property on another directory's test needs CMake 3.28.
function(kb_isolate_test_temp_directories directory root)
    get_property(tests DIRECTORY "${directory}" PROPERTY TESTS)
    foreach(test IN LISTS tests)
        set(temp "${root}/${test}")
        file(MAKE_DIRECTORY "${temp}")
        set_property(TEST "${test}" DIRECTORY "${directory}" APPEND PROPERTY ENVIRONMENT
            "TMP=${temp}" "TEMP=${temp}" "TMPDIR=${temp}")
    endforeach()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        kb_isolate_test_temp_directories("${subdirectory}" "${root}")
    endforeach()
endfunction()
if(BUILD_TESTING AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    if(DEFINED ENV{TEMP})
        file(TO_CMAKE_PATH "$ENV{TEMP}" _kb_system_temp)
    elseif(DEFINED ENV{TMPDIR})
        file(TO_CMAKE_PATH "$ENV{TMPDIR}" _kb_system_temp)
    else()
        set(_kb_system_temp "/tmp")
    endif()
    # One root per build tree, so two trees testing at once do not share one either.
    string(SHA1 _kb_tree_tag "${CMAKE_BINARY_DIR}")
    string(SUBSTRING "${_kb_tree_tag}" 0 12 _kb_tree_tag)
    kb_isolate_test_temp_directories("${CMAKE_CURRENT_SOURCE_DIR}" "${_kb_system_temp}/21kb-tests-${_kb_tree_tag}")
endif()
