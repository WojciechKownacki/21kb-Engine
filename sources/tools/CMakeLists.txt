add_library(kb_cli_core STATIC
    src/CliApiCommands.cpp
    src/CliCommon.cpp
    src/CliMcpCommand.cpp
    src/CliRunCommand.cpp
    src/CliSceneCommands.cpp
    src/CliValidateCommand.cpp
    src/private/CliCommands.hpp
    src/private/CliCommon.hpp
)

target_include_directories(kb_cli_core PUBLIC src/private)

target_link_libraries(kb_cli_core PUBLIC kb_engine)

kb_apply_quality_gates(kb_cli_core)

add_executable(kb_cli
    src/main.cpp
)

set_target_properties(kb_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

target_link_libraries(kb_cli PRIVATE kb_cli_core)

kb_apply_quality_gates(kb_cli)

# LIB-023: the engine's own reference API manifest (kb.lua/script_api.md/
# script_api.json/manifest.json, the latter carrying ComputeApiManifestHash)
# is now a genuine build artifact — produced by running the just-built
# kb_cli itself, not something a human has to remember to invoke by hand.
# --project is deliberately omitted: this is the engine's own,
# project-agnostic catalog (ScriptApiCatalog::Build(host) with no project
# assets mounted), not any particular game project's. LIB-024 (comparing
# this manifest against a previous version's, in CI, to detect breaking
# changes) is a separate, explicitly blocked task pending a baseline-storage
# decision — this only makes the artifact exist reliably for that future
# comparison to read.
add_custom_command(TARGET kb_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/api"
    COMMAND $<TARGET_FILE:kb_cli> api --out "${CMAKE_BINARY_DIR}/api"
    COMMENT "Generating kb_cli API manifest build artifact"
)

if(BUILD_TESTING)
    add_executable(kb_cli_tests
        tests/KbCliTests.cpp
    )

    target_link_libraries(kb_cli_tests PRIVATE kb_cli_core)

    add_test(NAME kb_cli_tests COMMAND $<TARGET_FILE:kb_cli_tests>)
    set_tests_properties(kb_cli_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} LABELS tools)

    kb_apply_quality_gates(kb_cli_tests)
endif()
