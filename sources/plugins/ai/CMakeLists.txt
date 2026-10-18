set(KB_AI_PLUGIN_LIBRARY_TYPE STATIC)
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    set(KB_AI_PLUGIN_LIBRARY_TYPE SHARED)
endif()

add_library(kb_ai_plugin ${KB_AI_PLUGIN_LIBRARY_TYPE}
    AIModule.hpp
    AIModule.cpp
    AIBrainSystem.hpp
    AIBrainSystem.cpp
)

target_include_directories(kb_ai_plugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kb_ai_plugin
    PRIVATE
        kb_engine
)

if(NOT KB_BUILD_PROVIDER_MODULES_AS_DLL)
    target_compile_definitions(kb_ai_plugin
        PUBLIC
            KB_ENGINE_MODULE_STATIC_LINK
    )
endif()

kb_apply_quality_gates(kb_ai_plugin)

if(BUILD_TESTING)
    add_executable(kb_ai_plugin_tests
        tests/AIBrainTests.cpp
        AIBrainSystem.cpp
    )
    target_include_directories(kb_ai_plugin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(kb_ai_plugin_tests PRIVATE kb_engine)
    add_test(NAME kb_ai_plugin_tests COMMAND $<TARGET_FILE:kb_ai_plugin_tests>)
    set_tests_properties(kb_ai_plugin_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    kb_apply_quality_gates(kb_ai_plugin_tests)
endif()
