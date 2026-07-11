add_library(kb_cli_core STATIC
    src/CliApiCommands.cpp
    src/CliCommon.cpp
    src/CliMcpCommand.cpp
    src/CliRunCommand.cpp
    src/CliSceneCommands.cpp
    src/CliValidateCommand.cpp
    src/MiniJson.cpp
    src/private/CliCommands.hpp
    src/private/CliCommon.hpp
    src/private/MiniJson.hpp
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

if(BUILD_TESTING)
    add_executable(kb_cli_tests
        tests/KbCliTests.cpp
    )

    target_link_libraries(kb_cli_tests PRIVATE kb_cli_core)

    add_test(NAME kb_cli_tests COMMAND $<TARGET_FILE:kb_cli_tests>)
    set_tests_properties(kb_cli_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} LABELS tools)

    kb_apply_quality_gates(kb_cli_tests)
endif()
