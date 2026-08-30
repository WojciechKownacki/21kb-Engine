if(NOT WIN32)
    return()
endif()

# The project bootstrap a runtime host performs before it can tick a scene.
# It is a library rather than part of kb_game so the standalone runtime sample
# and the shipped game read a project through the same code.
add_library(kb_game_core STATIC
    src/GameProjectRuntime.cpp
    src/private/GameProjectRuntime.hpp
)

target_include_directories(kb_game_core PUBLIC src/private)

target_link_libraries(kb_game_core PUBLIC kb_renderer)

kb_apply_quality_gates(kb_game_core)

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
        TIMEOUT 180
    )

    kb_apply_quality_gates(kb_game_tests)
endif()
