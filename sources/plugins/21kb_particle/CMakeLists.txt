add_library(kb_21kb_particle_core STATIC
    ParticleEffectCompiler.hpp
    ParticleEffectCompiler.cpp
)
target_include_directories(kb_21kb_particle_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kb_21kb_particle_core PUBLIC kb_engine)
kb_apply_quality_gates(kb_21kb_particle_core)

set(KB_21KB_PARTICLE_PLUGIN_LIBRARY_TYPE STATIC)
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    set(KB_21KB_PARTICLE_PLUGIN_LIBRARY_TYPE SHARED)
endif()

add_library(kb_21kb_particle_plugin ${KB_21KB_PARTICLE_PLUGIN_LIBRARY_TYPE}
    CpuParticleBackend.hpp
    CpuParticleBackend.cpp
    ParticleModule.hpp
    ParticleModule.cpp
    ParticleSceneSystem.hpp
    ParticleSceneSystem.cpp
)

target_include_directories(kb_21kb_particle_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kb_21kb_particle_plugin PRIVATE kb_21kb_particle_core)

if(NOT KB_BUILD_PROVIDER_MODULES_AS_DLL)
    target_compile_definitions(kb_21kb_particle_plugin PUBLIC KB_ENGINE_MODULE_STATIC_LINK)
endif()

kb_apply_quality_gates(kb_21kb_particle_plugin)

add_library(kb_21kb_particle_editor_core STATIC
    editor/ParticleEditorDocument.hpp
    editor/ParticleEditorDocument.cpp
    editor/ParticleEmitterListModel.hpp
    editor/ParticleEmitterListModel.cpp
    editor/ParticleEditorWorkspaceState.hpp
    editor/ParticleEditorWorkspaceState.cpp
    editor/ParticleEditorCommands.hpp
    editor/ParticleEditorCommands.cpp
    editor/ParticleEmitterInspectorModel.hpp
    editor/ParticleEmitterInspectorModel.cpp
    editor/ParticleAssetGateway.hpp
    editor/ParticleAssetGateway.cpp
    editor/ParticleDocumentCloseGuard.hpp
    editor/ParticleDocumentCloseGuard.cpp
    editor/ParticlePreviewSession.hpp
    editor/ParticlePreviewSession.cpp
    editor/ParticleBakeService.hpp
    editor/ParticleBakeService.cpp
)
target_include_directories(kb_21kb_particle_editor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kb_21kb_particle_editor_core PUBLIC kb_engine kb_renderer kb_21kb_particle_core)
kb_apply_quality_gates(kb_21kb_particle_editor_core)

if(BUILD_TESTING)
    add_executable(kb_21kb_particle_cpu_backend_tests
        tests/CpuParticleBackendTests.cpp
        CpuParticleBackend.cpp
        ParticleSceneSystem.cpp
    )
    target_include_directories(kb_21kb_particle_cpu_backend_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(kb_21kb_particle_cpu_backend_tests PRIVATE kb_21kb_particle_core)
    add_test(NAME kb_21kb_particle_cpu_backend_tests COMMAND $<TARGET_FILE:kb_21kb_particle_cpu_backend_tests>)
    set_tests_properties(kb_21kb_particle_cpu_backend_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    kb_apply_quality_gates(kb_21kb_particle_cpu_backend_tests)

    add_executable(kb_21kb_particle_editor_tests tests/ParticleEditorTests.cpp)
    target_link_libraries(kb_21kb_particle_editor_tests PRIVATE kb_21kb_particle_editor_core)
    if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
        target_compile_definitions(kb_21kb_particle_editor_tests PRIVATE
            KB_21KB_PARTICLE_PLUGIN_PATH="$<TARGET_FILE:kb_21kb_particle_plugin>")
        add_dependencies(kb_21kb_particle_editor_tests kb_21kb_particle_plugin)
    endif()
    add_test(NAME kb_21kb_particle_editor_tests COMMAND $<TARGET_FILE:kb_21kb_particle_editor_tests>)
    set_tests_properties(kb_21kb_particle_editor_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    kb_apply_quality_gates(kb_21kb_particle_editor_tests)
endif()
