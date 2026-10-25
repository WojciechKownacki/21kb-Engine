# The project bootstrap a runtime host performs before it can tick a scene.
# It is a library rather than part of kb_game so the standalone runtime sample
# and the shipped game read a project through the same code.
if(WIN32)
    set(KB_GAME_TARGET_PROFILE_ID "Windows.x64")
elseif(ANDROID)
    if(KB_ANDROID_TEXTURE_PROFILE STREQUAL "ASTC")
        set(KB_GAME_TARGET_PROFILE_ID "Android.ASTC.arm64")
    else()
        set(KB_GAME_TARGET_PROFILE_ID "Android.ETC2.arm64")
    endif()
elseif(EMSCRIPTEN)
    if(KB_WEB_RENDERER STREQUAL "WEBGPU")
        set(KB_GAME_TARGET_PROFILE_ID "WebGPU.wasm32")
    else()
        set(KB_GAME_TARGET_PROFILE_ID "WebGL.wasm32")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(KB_GAME_TARGET_PROFILE_ID "Linux.x64")
else()
    set(KB_GAME_TARGET_PROFILE_ID "")
endif()

add_library(kb_game_core STATIC
    src/GameProjectRuntime.cpp
    src/GameRuntimeDiagnostics.cpp
    src/private/GameRuntimeDiagnostics.hpp
    src/GameScriptLog.cpp
    src/private/GameScriptLog.hpp
    src/ParticleRuntimeDiagnostics.cpp
    src/private/ParticleRuntimeDiagnostics.hpp
    src/private/GameProjectRuntime.hpp
    src/private/PackagedRuntimeModuleContract.hpp
)

target_include_directories(kb_game_core PUBLIC src/private)

target_link_libraries(kb_game_core PUBLIC kb_renderer)
target_compile_definitions(kb_game_core PRIVATE
    "KB_GAME_TARGET_PROFILE_ID=\"${KB_GAME_TARGET_PROFILE_ID}\""
)

kb_apply_quality_gates(kb_game_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Renderer diagnostics include bx's established GNU variadic assertion macros.
    target_compile_options(kb_game_core PRIVATE -Wno-gnu-zero-variadic-macro-arguments)
endif()

if(NOT ANDROID AND NOT EMSCRIPTEN)
    add_library(kb_cooker_core STATIC
        src/ProjectCooker.cpp
        src/private/ProjectCooker.hpp
    )

    target_include_directories(kb_cooker_core PUBLIC src/private)
    target_link_libraries(kb_cooker_core
        PUBLIC
            kb_renderer
            kb_renderer_bake
        PRIVATE
            kb_runtime_asset_pack_validation
    )
    target_compile_definitions(kb_cooker_core
        PRIVATE
            KB_COOKER_DEFAULT_ENGINE_ROOT="${CMAKE_SOURCE_DIR}"
    )
    set(KB_COOKER_SHADERC "" CACHE FILEPATH "External pinned renderer shader compiler when shaderc is not built")
    if(TARGET bgfx::shaderc)
        add_dependencies(kb_cooker_core bgfx::shaderc)
        target_compile_definitions(kb_cooker_core
            PRIVATE KB_COOKER_DEFAULT_SHADERC="$<PATH:CMAKE_PATH,$<TARGET_FILE:bgfx::shaderc>>"
        )
    elseif(KB_COOKER_SHADERC)
        target_compile_definitions(kb_cooker_core
            PRIVATE KB_COOKER_DEFAULT_SHADERC="$<PATH:CMAKE_PATH,${KB_COOKER_SHADERC}>"
        )
    endif()
    add_dependencies(kb_cooker_core kb_renderer_shaders)
    kb_apply_quality_gates(kb_cooker_core)

    add_executable(kb_cooker src/CookerMain.cpp)
    target_link_libraries(kb_cooker PRIVATE kb_cooker_core)
    set_target_properties(kb_cooker PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    kb_apply_quality_gates(kb_cooker)
endif()

if(NOT WIN32 AND (KB_BUILD_PACKAGED_GAME_HOST OR (ANDROID AND KB_BUILD_ANDROID_HOST)))
    add_library(kb_packaged_game_host_core STATIC
        src/PackagedGameRuntime.cpp
        src/PackagedRuntimeModules.cpp
        src/private/PackagedGameRuntime.hpp
        src/private/PackagedRuntimeModules.hpp
    )
    target_include_directories(kb_packaged_game_host_core
        PUBLIC src/private
        PRIVATE
            ${CMAKE_SOURCE_DIR}/sources/plugins/physics_jolt
            ${CMAKE_SOURCE_DIR}/sources/plugins/audio_miniaudio
            ${CMAKE_SOURCE_DIR}/sources/plugins/basic_lighting
            ${CMAKE_SOURCE_DIR}/sources/plugins/21kb_particle
            ${CMAKE_SOURCE_DIR}/sources/plugins/ai
            ${CMAKE_SOURCE_DIR}/sources/plugins/world_2d
    )
    target_compile_definitions(kb_packaged_game_host_core PRIVATE
        "KB_GAME_TARGET_PROFILE_ID=\"${KB_GAME_TARGET_PROFILE_ID}\""
    )
    target_link_libraries(kb_packaged_game_host_core
        PUBLIC kb_game_core
        PRIVATE
            kb_physics_jolt_plugin
            kb_audio_miniaudio_plugin
            kb_basic_lighting_plugin
            kb_21kb_particle_plugin
            kb_ai_plugin
            kb_world2d_plugin
    )
    set_target_properties(kb_packaged_game_host_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    kb_apply_quality_gates(kb_packaged_game_host_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Renderer.hpp exposes bx assertion macros that deliberately use the
        # established GNU empty-variadic extension.
        target_compile_options(kb_packaged_game_host_core
            PRIVATE -Wno-gnu-zero-variadic-macro-arguments
        )
    endif()
endif()

if(ANDROID AND KB_BUILD_ANDROID_HOST)
    find_package(game-activity REQUIRED CONFIG)
    find_package(games-controller REQUIRED CONFIG)

    add_library(kb_android_gamepad_input STATIC
        src/android/AndroidGamepadInput.cpp src/android/AndroidTouchInput.cpp)
    target_include_directories(kb_android_gamepad_input PUBLIC src/private)
    target_link_libraries(kb_android_gamepad_input PUBLIC
        kb_engine games-controller::paddleboat_static game-activity::game-activity_static)
    kb_apply_quality_gates(kb_android_gamepad_input)

    add_library(kb_game_android SHARED
        src/android/AndroidGameHost.cpp
    )

    target_link_libraries(kb_game_android
        PRIVATE
            kb_game_core
            kb_packaged_game_host_core
            kb_android_gamepad_input
            game-activity::game-activity_static
            android
            log
    )

    target_include_directories(kb_game_android PRIVATE
        ${CMAKE_SOURCE_DIR}/sources/plugins/physics_jolt
        ${CMAKE_SOURCE_DIR}/sources/plugins/audio_miniaudio
        ${CMAKE_SOURCE_DIR}/sources/plugins/basic_lighting
        ${CMAKE_SOURCE_DIR}/sources/plugins/21kb_particle
        ${CMAKE_SOURCE_DIR}/sources/plugins/ai
        ${CMAKE_SOURCE_DIR}/sources/plugins/world_2d
    )

    # GameActivity enters native code through JNI, so the object defining this
    # Java-discovered symbol must survive static-library dead stripping.
    target_link_options(kb_game_android
        PRIVATE
            "-Wl,-u,Java_com_google_androidgamesdk_GameActivity_initializeNativeCode"
    )

    kb_apply_quality_gates(kb_game_android)

    # bx's public assertion macro uses the long-standing GNU `, ##__VA_ARGS__`
    # extension. Keep our warnings strict while silencing only that vendor-header
    # diagnostic in the host translation unit that includes Renderer.hpp.
    target_compile_options(kb_game_android
        PRIVATE -Wno-gnu-zero-variadic-macro-arguments
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND KB_BUILD_PACKAGED_GAME_HOST)
    find_package(X11 REQUIRED)
    add_executable(kb_game_linux src/linux/LinuxGameHost.cpp)
    target_link_libraries(kb_game_linux PRIVATE kb_packaged_game_host_core X11::X11)
    set_target_properties(kb_game_linux PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    kb_apply_quality_gates(kb_game_linux)
endif()

if(EMSCRIPTEN AND KB_BUILD_PACKAGED_GAME_HOST)
    if(KB_WEB_RENDERER STREQUAL "WEBGPU")
        set(KB_GAME_WEB_TARGET kb_game_webgpu)
    else()
        set(KB_GAME_WEB_TARGET kb_game_webgl)
    endif()
    add_executable(${KB_GAME_WEB_TARGET} src/web/WebGameHost.cpp src/web/WebTouchInput.cpp)
    target_link_libraries(${KB_GAME_WEB_TARGET} PRIVATE kb_packaged_game_host_core)
    target_link_options(${KB_GAME_WEB_TARGET} PRIVATE
        "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/src/web/WebGameShell.html"
        "SHELL:--pre-js \"${CMAKE_CURRENT_SOURCE_DIR}/src/web/WebWasmLoader.js\""
        "SHELL:--pre-js \"${CMAKE_CURRENT_SOURCE_DIR}/src/web/WebUserStorage.js\""
        "-lidbfs.js"
        "-sEXPORTED_RUNTIME_METHODS=requestFullscreen"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sMAXIMUM_MEMORY=2147483648"
        "-sSTACK_SIZE=1048576"
        "-sASSERTIONS=0"
    )
    set_property(TARGET ${KB_GAME_WEB_TARGET} APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/web/WebGameShell.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/web/WebWasmLoader.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/web/WebUserStorage.js")
    if(KB_WEB_RENDERER STREQUAL "WEBGPU")
        target_compile_definitions(${KB_GAME_WEB_TARGET} PRIVATE KB_WEB_RENDERER_WEBGPU=1)
        target_compile_options(${KB_GAME_WEB_TARGET} PRIVATE "--use-port=emdawnwebgpu")
        target_link_options(${KB_GAME_WEB_TARGET} PRIVATE
            "--use-port=emdawnwebgpu"
            "-sASYNCIFY=1"
            "-sASYNCIFY_STACK_SIZE=65536"
        )
    else()
        target_link_options(${KB_GAME_WEB_TARGET} PRIVATE
            "-sMIN_WEBGL_VERSION=2"
            "-sMAX_WEBGL_VERSION=2"
            "-sFULL_ES3=1"
        )
    endif()
    set_target_properties(${KB_GAME_WEB_TARGET} PROPERTIES
        OUTPUT_NAME "Game"
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    kb_apply_quality_gates(${KB_GAME_WEB_TARGET})
    target_compile_options(${KB_GAME_WEB_TARGET} PRIVATE
        -Wno-gnu-zero-variadic-macro-arguments
        -Wno-dollar-in-identifier-extension
    )
endif()

if(NOT WIN32)
    return()
endif()

add_executable(kb_game WIN32
    src/GameWindow.cpp
    src/main.cpp
    src/private/GameWindow.hpp
)

target_link_libraries(kb_game
    PRIVATE
        kb_game_core
        kb_crash_reporting
        user32
        gdi32
        shell32
)

set_target_properties(kb_game PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# The particle provider is the one project plugin that is not discoverable from
# a sibling build directory, so it is staged beside the runtime executable - on
# every build, so a rebuilt provider replaces the copy even when kb_game does not relink.
add_dependencies(kb_game kb_21kb_particle_plugin)
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    add_custom_target(kb_game_stage_particle_plugin
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:kb_game>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:kb_21kb_particle_plugin>
            $<TARGET_FILE_DIR:kb_game>
        VERBATIM
    )
    add_dependencies(kb_game_stage_particle_plugin kb_21kb_particle_plugin)
    include("${CMAKE_SOURCE_DIR}/CMake/SerializeParticleRuntimeStaging.cmake")
    kb_serialize_particle_runtime_staging(kb_game_stage_particle_plugin)
    add_dependencies(kb_game kb_game_stage_particle_plugin)
endif()

kb_apply_quality_gates(kb_game)
kb_target_copy_bgfx_runtime_shaders(kb_game)
kb_target_stage_material_graph_shader_cache(kb_game)

if(BUILD_TESTING)
    add_library(kb_windows_runtime_module_test_plugin SHARED
        tests/WindowsRuntimeModuleTestPlugin.cpp
    )
    target_include_directories(kb_windows_runtime_module_test_plugin
        PRIVATE ${CMAKE_SOURCE_DIR}/sources/engine/include
    )
    kb_apply_quality_gates(kb_windows_runtime_module_test_plugin)

    # The shared bootstrap, run directly. The launch test drives the executable
    # against a project with no plugins, no physics layers and no input mapping,
    # so it cannot reach most of what kb_game_core does.
    add_executable(kb_game_core_tests
        tests/KbGameCoreTests.cpp
    )

    target_link_libraries(kb_game_core_tests PRIVATE kb_game_core kb_cooker_core kb_cli_core)
    add_dependencies(kb_game_core_tests
        kb_windows_runtime_module_test_plugin
        kb_physics_jolt_plugin
    )
    target_compile_definitions(kb_game_core_tests PRIVATE
        "KB_WINDOWS_RUNTIME_MODULE_TEST_PLUGIN_PATH=\"$<TARGET_FILE:kb_windows_runtime_module_test_plugin>\""
        "KB_WINDOWS_PHYSICS_PLUGIN_PATH=\"$<TARGET_FILE:kb_physics_jolt_plugin>\""
        "KB_GAME_CORE_TEST_ROOT=\"${CMAKE_BINARY_DIR}/game-core-test-scratch\""
    )

    add_test(NAME kb_game_core COMMAND $<TARGET_FILE:kb_game_core_tests>)
    add_test(NAME kb_windows_runtime_module_package
        COMMAND $<TARGET_FILE:kb_game_core_tests> --windows-runtime-modules
    )
    add_test(NAME kb_game_packaged_trust
        COMMAND $<TARGET_FILE:kb_game_core_tests> --packaged-trust
    )
    # Partitioned worlds: one build for kb_cli and kb_cooker, single-pack and region-chunk
    # packaging, streaming from the packages. Three cooks; kept out of kb_game_core's budget.
    add_test(NAME kb_game_partitioned_world
        COMMAND $<TARGET_FILE:kb_game_core_tests> --partitioned-world
    )
    set_tests_properties(kb_game_core PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        LABELS "game;runtime"
        TIMEOUT 300
    )
    set_tests_properties(kb_windows_runtime_module_package PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        LABELS "game;runtime;packaging"
        TIMEOUT 120
    )
    set_tests_properties(kb_game_packaged_trust PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        LABELS "game;runtime;packaging"
        TIMEOUT 300
    )
    set_tests_properties(kb_game_partitioned_world PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        LABELS "game;runtime;packaging;world"
        TIMEOUT 300
    )

    kb_apply_quality_gates(kb_game_core_tests)

    add_executable(kb_game_tests
        tests/KbGameLaunchTests.cpp
    )

    target_link_libraries(kb_game_tests PRIVATE kb_engine)

    target_compile_definitions(kb_game_tests
        PRIVATE
            "KB_GAME_EXECUTABLE_PATH=\"$<TARGET_FILE:kb_game>\""
    )

    add_dependencies(kb_game_tests kb_game)

    add_test(NAME kb_game_launch COMMAND $<TARGET_FILE:kb_game_tests>)
    set_tests_properties(kb_game_launch PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        LABELS "game;runtime"
        # Nine launches of the real executable, four of which have to be given
        # long enough to prove they are a refusal rather than a hang.
        TIMEOUT 420
    )

    kb_apply_quality_gates(kb_game_tests)
    if(TARGET kb_cli AND KB_BUILD_PROVIDER_MODULES_AS_DLL)
        find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
        add_dependencies(kb_game_tests kb_cli kb_cooker)
        set(_kb_particle_test_shaderc "${KB_COOKER_SHADERC}")
        if(TARGET bgfx::shaderc)
            set(_kb_particle_test_shaderc "$<TARGET_FILE:bgfx::shaderc>")
            add_dependencies(kb_game_tests bgfx::shaderc)
        endif()
        add_test(NAME kb_game_particle_gpu_package COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/ParticlePackagedGpuTests.py"
            "--cli" "$<TARGET_FILE:kb_cli>"
            "--cooker" "$<TARGET_FILE:kb_cooker>"
            "--player" "$<TARGET_FILE:kb_game>"
            "--engine-root" "${CMAKE_SOURCE_DIR}"
            "--test-root" "${CMAKE_BINARY_DIR}/particle_package_tests"
            "--shaderc" "${_kb_particle_test_shaderc}"
            "--plugin" "$<TARGET_FILE:kb_physics_jolt_plugin>"
            "--plugin" "$<TARGET_FILE:kb_audio_miniaudio_plugin>"
            "--plugin" "$<TARGET_FILE:kb_basic_lighting_plugin>"
            "--plugin" "$<TARGET_FILE:kb_21kb_particle_plugin>"
            "--plugin" "$<TARGET_FILE:kb_ai_plugin>")
        set_tests_properties(kb_game_particle_gpu_package PROPERTIES
            LABELS "game;runtime;packaging;gpu" TIMEOUT 360)
        set(_kb_touch_test_runtime_args)
        if(MSVC AND KB_ENABLE_SANITIZERS)
            list(APPEND _kb_touch_test_runtime_args
                "--runtime-dll" "${KB_MSVC_ASAN_DYNAMIC_RUNTIME}")
        endif()
        add_test(NAME kb_game_touch_script_package COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/ScriptTouchPackagedTests.py"
            "--cli" "$<TARGET_FILE:kb_cli>"
            "--cooker" "$<TARGET_FILE:kb_cooker>"
            "--player" "$<TARGET_FILE:kb_game>"
            "--engine-root" "${CMAKE_SOURCE_DIR}"
            "--test-root" "${CMAKE_BINARY_DIR}/touch_script_package_tests"
            "--shaderc" "${_kb_particle_test_shaderc}"
            ${_kb_touch_test_runtime_args}
            "--plugin" "$<TARGET_FILE:kb_physics_jolt_plugin>"
            "--plugin" "$<TARGET_FILE:kb_audio_miniaudio_plugin>"
            "--plugin" "$<TARGET_FILE:kb_basic_lighting_plugin>"
            "--plugin" "$<TARGET_FILE:kb_21kb_particle_plugin>"
            "--plugin" "$<TARGET_FILE:kb_ai_plugin>")
        set_tests_properties(kb_game_touch_script_package PROPERTIES
            LABELS "game;runtime;packaging;input" TIMEOUT 360)
        add_test(NAME kb_game_audio_dsp_package COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/AudioDspPackagedTests.py"
            "--cli" "$<TARGET_FILE:kb_cli>"
            "--cooker" "$<TARGET_FILE:kb_cooker>"
            "--player" "$<TARGET_FILE:kb_game>"
            "--engine-root" "${CMAKE_SOURCE_DIR}"
            "--test-root" "${CMAKE_BINARY_DIR}/audio_dsp_package_tests"
            "--shaderc" "${_kb_particle_test_shaderc}"
            ${_kb_touch_test_runtime_args}
            "--plugin" "$<TARGET_FILE:kb_physics_jolt_plugin>"
            "--plugin" "$<TARGET_FILE:kb_audio_miniaudio_plugin>"
            "--plugin" "$<TARGET_FILE:kb_basic_lighting_plugin>"
            "--plugin" "$<TARGET_FILE:kb_21kb_particle_plugin>"
            "--plugin" "$<TARGET_FILE:kb_ai_plugin>")
        set_tests_properties(kb_game_audio_dsp_package PROPERTIES
            LABELS "game;runtime;packaging;audio" TIMEOUT 180)
    endif()
endif()
