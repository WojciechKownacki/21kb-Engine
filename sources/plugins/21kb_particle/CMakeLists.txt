add_library(kb_21kb_particle_core INTERFACE)
target_link_libraries(kb_21kb_particle_core INTERFACE kb_engine)

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

if(BUILD_TESTING)
    add_executable(kb_21kb_particle_cpu_backend_tests
        tests/CpuParticleBackendTests.cpp
        CpuParticleBackend.cpp
        ParticleSceneSystem.cpp
    )
    target_include_directories(kb_21kb_particle_cpu_backend_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(kb_21kb_particle_cpu_backend_tests PRIVATE kb_engine)
    add_test(NAME kb_21kb_particle_cpu_backend_tests COMMAND $<TARGET_FILE:kb_21kb_particle_cpu_backend_tests>)
    set_tests_properties(kb_21kb_particle_cpu_backend_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    kb_apply_quality_gates(kb_21kb_particle_cpu_backend_tests)
endif()
