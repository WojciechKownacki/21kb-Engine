add_library(kb_cli_core STATIC
    src/CliAnimatorCommands.cpp
    src/CliSkeletonCommands.cpp
    src/CliAbilityCommands.cpp
    src/CliApiCommands.cpp
    src/CliAudioCommands.cpp
    src/CliBatchCommand.cpp
    src/CliCommon.cpp
    src/CliDispatch.cpp
    src/CliEntityCommands.cpp
    src/CliImportCommand.cpp
    src/CliMcpCommand.cpp
    src/CliNavMeshCommands.cpp
    src/CliPackSetCommands.cpp
    src/CliPlayerPreset.cpp
    src/CliPhysicsCommands.cpp
    src/CliQualityCommands.cpp
    src/CliRunCommand.cpp
    src/CliSceneCommands.cpp
    src/CliScriptCommands.cpp
    src/CliSecurityCommands.cpp
    src/CliValidateCommand.cpp
    src/CliWorldCommands.cpp
    src/private/CliCommands.hpp
    src/private/CliCommon.hpp
    src/private/CliPlayerPreset.hpp
    src/private/CliProgram.hpp
)

target_include_directories(kb_cli_core PUBLIC src/private)

target_link_libraries(kb_cli_core PUBLIC kb_engine)
# world build resolves meshes for HLOD proxies with the same loaders kb_cooker uses.
# Materials (.kbmat) are renderer assets: material-new/set/show and MeshRenderer material=.
if(KB_BUILD_RENDERER)
    target_link_libraries(kb_cli_core PUBLIC kb_renderer kb_21kb_particle_editor_core)
    target_compile_definitions(kb_cli_core PUBLIC KB_CLI_WORLD_HLOD=1 KB_CLI_RENDERER=1)
    target_sources(kb_cli_core PRIVATE
        src/CliMaterialCommands.cpp
        src/CliMaterialGraphCommands.cpp
        src/CliPostProcessCommands.cpp
        src/CliParticleCommands.cpp
        src/CliParticleProfileCommand.cpp
        src/CliMap2DCommands.cpp
        src/private/CliMaterialShared.hpp)
    target_link_libraries(kb_cli_core PUBLIC kb_world2d_core)
endif()

kb_apply_quality_gates(kb_cli_core)

add_executable(kb_cli
    src/main.cpp
)

if(WIN32)
    # Authenticode-signs shipped Windows images with a PFX whose password arrives on
    # standard input. The editor's signing broker and the package script run it.
    add_executable(kb_authenticode_signer src/AuthenticodeSignerMain.cpp)
    set_target_properties(kb_authenticode_signer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    target_link_libraries(kb_authenticode_signer PRIVATE crypt32 ncrypt)
    kb_apply_quality_gates(kb_authenticode_signer)
endif()

set_target_properties(kb_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(KB_BUILD_RENDERER)
    add_custom_target(kb_cli_stage_particle_content
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/sources/plugins/21kb_particle/content/Materials"
            "$<TARGET_FILE_DIR:kb_cli>/Content/21kbParticle/Materials"
        VERBATIM)
    add_dependencies(kb_cli kb_cli_stage_particle_content)
endif()
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    add_custom_target(kb_cli_stage_particle_plugin
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:kb_cli>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:kb_21kb_particle_plugin>"
            "$<TARGET_FILE_DIR:kb_cli>"
        VERBATIM
    )
    add_dependencies(kb_cli_stage_particle_plugin kb_21kb_particle_plugin)
    include("${CMAKE_SOURCE_DIR}/CMake/SerializeParticleRuntimeStaging.cmake")
    kb_serialize_particle_runtime_staging(kb_cli_stage_particle_plugin)
    add_dependencies(kb_cli kb_cli_stage_particle_plugin)
endif()
if(WIN32)
    # UTF-8 process code page: Polish (any) letters in arguments and file names.
    target_sources(kb_cli PRIVATE kb_cli.manifest)
endif()

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
    if(KB_BUILD_RENDERER)
        target_include_directories(kb_cli_tests PRIVATE "${PROJECT_SOURCE_DIR}/sources/renderer/tests")
        add_custom_target(kb_cli_tests_stage_particle_content
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/sources/plugins/21kb_particle/content/Materials"
                "$<TARGET_FILE_DIR:kb_cli_tests>/Content/21kbParticle/Materials"
            VERBATIM)
        add_dependencies(kb_cli_tests kb_cli_tests_stage_particle_content)
        if(MSVC AND TARGET kb_test_process_error_tests)
            find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)
            add_dependencies(kb_cli_tests kb_test_process_error_tests)
            add_test(NAME kb_cli_assertion_errors
                COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/scripts/tests/test_test_process_errors.py
                    --probe $<TARGET_FILE:kb_test_process_error_tests> --debug-crt $<IF:$<CONFIG:Debug>,1,0>
                    --renderer-probe $<TARGET_FILE:kb_cli_tests>)
            set_tests_properties(kb_cli_assertion_errors PROPERTIES TIMEOUT 60)
        endif()
    endif()
    if(WIN32)
        # The commands run in-process here, under the same code page as kb_cli.
        target_sources(kb_cli_tests PRIVATE kb_cli.manifest)
    endif()

    add_test(NAME kb_cli_tests COMMAND $<TARGET_FILE:kb_cli_tests>)
    set_tests_properties(kb_cli_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} LABELS tools)
    if(WIN32 AND KB_BUILD_RENDERER AND KB_BUILD_PROVIDER_MODULES_AS_DLL)
        add_test(NAME kb_cli_particle_profile COMMAND ${CMAKE_COMMAND}
            "-DCLI=$<TARGET_FILE:kb_cli>"
            "-DTEST_ROOT=${CMAKE_BINARY_DIR}/particle_profile_tests"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/KbCliParticleProfileTests.cmake")
        set_tests_properties(kb_cli_particle_profile PROPERTIES LABELS "tools;gpu" TIMEOUT 180)
        add_test(NAME kb_cli_renderer_info COMMAND ${CMAKE_COMMAND}
            "-DCLI=$<TARGET_FILE:kb_cli>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/KbCliRendererInfoTests.cmake")
        set_tests_properties(kb_cli_renderer_info PROPERTIES LABELS "tools;gpu" TIMEOUT 300)
    endif()

    # The committed script API baseline: a removed or changed function fails here as well as in CI, so a breaking change
    # shows up in a local ctest run. An intentional change updates the baseline with --update-baseline in the same commit.
    add_test(NAME kb_cli_api_check_committed_baseline COMMAND kb_cli api-check
        --baseline "${PROJECT_SOURCE_DIR}/tests/api_baseline/script_api.json")
    set_tests_properties(kb_cli_api_check_committed_baseline PROPERTIES LABELS "tools" TIMEOUT 120)

    kb_apply_quality_gates(kb_cli_tests)

    if(WIN32 AND KB_BUILD_PROVIDER_MODULES_AS_DLL)
        add_dependencies(kb_cli kb_physics_jolt_plugin kb_audio_miniaudio_plugin
            kb_basic_lighting_plugin kb_21kb_particle_plugin kb_ai_plugin kb_world2d_plugin)
        add_test(NAME kb_native_sdk_layout COMMAND ${CMAKE_COMMAND}
            "-DCLI=$<TARGET_FILE:kb_cli>"
            "-DENGINE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DSDK_GENERATOR=${CMAKE_GENERATOR}"
            "-DCOMPILER=${CMAKE_CXX_COMPILER}"
            "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DTEST_ROOT=${CMAKE_BINARY_DIR}/native-sdk-layout"
            "-DENGINE_LIBRARY=$<TARGET_FILE:kb_engine>"
            "-DFLECS_LIBRARY=$<TARGET_FILE:flecs_static>"
            "-DPHYSICS_PLUGIN=$<TARGET_FILE:kb_physics_jolt_plugin>"
            "-DAUDIO_PLUGIN=$<TARGET_FILE:kb_audio_miniaudio_plugin>"
            "-DLIGHTING_PLUGIN=$<TARGET_FILE:kb_basic_lighting_plugin>"
            "-DPARTICLES_PLUGIN=$<TARGET_FILE:kb_21kb_particle_plugin>"
            "-DAI_PLUGIN=$<TARGET_FILE:kb_ai_plugin>"
            "-DWORLD2D_PLUGIN=$<TARGET_FILE:kb_world2d_plugin>"
            "-Dlua_LIBRARY=$<TARGET_FILE:kb_lua>"
            "-Dufbx_LIBRARY=$<TARGET_FILE:kb_ufbx>"
            "-Dmonocypher_LIBRARY=$<TARGET_FILE:kb_monocypher>"
            "-Dzstd_LIBRARY=$<TARGET_FILE:kb_zstd>"
            "-Drecast_LIBRARY=$<TARGET_FILE:kb_recast>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/NativeSdkLayout.cmake")
        set_tests_properties(kb_native_sdk_layout PROPERTIES
            CONFIGURATIONS "Release;RelWithDebInfo" LABELS "tools;native-sdk" TIMEOUT 240)
    endif()
endif()
