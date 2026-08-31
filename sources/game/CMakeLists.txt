# The project bootstrap a runtime host performs before it can tick a scene.
# It is a library rather than part of kb_game so the standalone runtime sample
# and the shipped game read a project through the same code.
add_library(kb_game_core STATIC
    src/GameProjectRuntime.cpp
    src/private/GameProjectRuntime.hpp
    src/private/PackagedRuntimeModuleContract.hpp
)

target_include_directories(kb_game_core PUBLIC src/private)

target_link_libraries(kb_game_core PUBLIC kb_renderer)

kb_apply_quality_gates(kb_game_core)

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
    if(TARGET bgfx::shaderc)
        add_dependencies(kb_cooker_core bgfx::shaderc)
        target_compile_definitions(kb_cooker_core
            PRIVATE KB_COOKER_DEFAULT_SHADERC="$<PATH:CMAKE_PATH,$<TARGET_FILE:bgfx::shaderc>>"
        )
    endif()
    add_dependencies(kb_cooker_core kb_renderer_shaders)
    kb_apply_quality_gates(kb_cooker_core)

    add_executable(kb_cooker src/CookerMain.cpp)
    target_link_libraries(kb_cooker PRIVATE kb_cooker_core)
    set_target_properties(kb_cooker PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    kb_apply_quality_gates(kb_cooker)
endif()

if(ANDROID AND KB_BUILD_ANDROID_HOST)
    find_package(game-activity REQUIRED CONFIG)

    add_library(kb_game_android SHARED
        src/android/AndroidGameHost.cpp
    )

    target_link_libraries(kb_game_android
        PRIVATE
            kb_game_core
            kb_physics_jolt_plugin
            kb_audio_miniaudio_plugin
            kb_basic_lighting_plugin
            kb_21kb_particle_plugin
            game-activity::game-activity_static
            android
            log
    )

    target_include_directories(kb_game_android PRIVATE
        ${CMAKE_SOURCE_DIR}/sources/plugins/physics_jolt
        ${CMAKE_SOURCE_DIR}/sources/plugins/audio_miniaudio
        ${CMAKE_SOURCE_DIR}/sources/plugins/basic_lighting
        ${CMAKE_SOURCE_DIR}/sources/plugins/21kb_particle
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
        user32
        gdi32
        shell32
)

set_target_properties(kb_game PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# The particle provider is the one project plugin that is not discoverable from
# a sibling build directory, so it is staged beside the runtime executable.
add_dependencies(kb_game kb_21kb_particle_plugin)
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    add_custom_command(TARGET kb_game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:kb_21kb_particle_plugin>
            $<TARGET_FILE_DIR:kb_game>
        VERBATIM
    )
endif()

kb_apply_quality_gates(kb_game)
kb_target_copy_bgfx_runtime_shaders(kb_game)
kb_target_stage_material_graph_shader_cache(kb_game)

if(BUILD_TESTING)
    # The shared bootstrap, run directly. The launch test drives the executable
    # against a project with no plugins, no physics layers and no input mapping,
    # so it cannot reach most of what kb_game_core does.
    add_executable(kb_game_core_tests
        tests/KbGameCoreTests.cpp
    )

    target_link_libraries(kb_game_core_tests PRIVATE kb_game_core kb_cooker_core)

    add_test(NAME kb_game_core COMMAND $<TARGET_FILE:kb_game_core_tests>)
    set_tests_properties(kb_game_core PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        LABELS "game;runtime"
        TIMEOUT 120
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
endif()
