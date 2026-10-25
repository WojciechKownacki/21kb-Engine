add_library(kb_audio_miniaudio_asset_runtime STATIC
    assets/MiniaudioEncodingFormat.hpp
    assets/ResolvedAudioClip.hpp
    assets/MiniaudioClipResolver.hpp
    assets/MiniaudioClipResolver.cpp
)

target_include_directories(kb_audio_miniaudio_asset_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kb_audio_miniaudio_asset_runtime
    PUBLIC
        kb_engine
    PRIVATE
        miniaudio
)

set_target_properties(kb_audio_miniaudio_asset_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

kb_apply_quality_gates(kb_audio_miniaudio_asset_runtime)

set(KB_AUDIO_MINIAUDIO_PLUGIN_LIBRARY_TYPE STATIC)
set(KB_AUDIO_MINIAUDIO_DSP_SOURCES
    runtime/MiniaudioFilterChain.hpp
    runtime/MiniaudioFilterChain.cpp
    runtime/MiniaudioDelayNode.hpp
    runtime/MiniaudioDelayNode.cpp
    runtime/MiniaudioReverbNode.hpp
    runtime/MiniaudioReverbNode.cpp
    runtime/MiniaudioDynamicsNode.hpp
    runtime/MiniaudioDynamicsNode.cpp
)
# The existing reverb API supports a configurable fixed maximum sample rate.
# Use the same state layout in the vendor implementation and every consumer.
if(NOT TARGET miniaudio_reverb_node)
    add_library(miniaudio_reverb_node STATIC
        ${PROJECT_SOURCE_DIR}/third_party/miniaudio/extras/nodes/ma_reverb_node/ma_reverb_node.c)
endif()
target_compile_definitions(miniaudio_reverb_node PUBLIC verblib_max_sample_rate_multiplier=5)
set_target_properties(miniaudio_reverb_node PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    set(KB_AUDIO_MINIAUDIO_PLUGIN_LIBRARY_TYPE SHARED)
endif()

add_library(kb_audio_miniaudio_plugin ${KB_AUDIO_MINIAUDIO_PLUGIN_LIBRARY_TYPE}
    MiniaudioModule.hpp
    MiniaudioModule.cpp
    MiniaudioSceneSystem.hpp
    MiniaudioSceneSystem.cpp
    assets/MiniaudioEncodingFormat.hpp
    assets/ResolvedAudioClip.hpp
    assets/MiniaudioClipResolver.hpp
    playback/MiniaudioVoicePool.hpp
    playback/MiniaudioVoicePool.cpp
    runtime/MiniaudioEngine.hpp
    runtime/MiniaudioEngine.cpp
    runtime/MiniaudioMeterNode.hpp
    runtime/MiniaudioMeterNode.cpp
    runtime/MiniaudioMeterReport.hpp
    runtime/MiniaudioPlaybackBackend.hpp
    runtime/MiniaudioPlaybackBackend.cpp
    runtime/MiniaudioSound.hpp
    runtime/MiniaudioSound.cpp
    ${KB_AUDIO_MINIAUDIO_DSP_SOURCES}
    scene/MiniaudioBusRegistry.hpp
    scene/MiniaudioBusRegistry.cpp
    scene/MiniaudioListenerSynchronizer.hpp
    scene/MiniaudioListenerSynchronizer.cpp
    scene/MiniaudioOcclusionSampler.hpp
    scene/MiniaudioOcclusionSampler.cpp
    scene/MiniaudioSourceRegistry.hpp
    scene/MiniaudioSourceRegistry.cpp
)

target_include_directories(kb_audio_miniaudio_plugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kb_audio_miniaudio_plugin
    PRIVATE
        kb_engine
        kb_audio_miniaudio_asset_runtime
        miniaudio
        miniaudio_reverb_node
)

if(NOT KB_BUILD_PROVIDER_MODULES_AS_DLL)
    target_compile_definitions(kb_audio_miniaudio_plugin
        PUBLIC
            KB_ENGINE_MODULE_STATIC_LINK
    )
endif()

kb_apply_quality_gates(kb_audio_miniaudio_plugin)

if(BUILD_TESTING)
    add_executable(kb_audio_meter_tests tests/AudioMeterTests.cpp runtime/MiniaudioMeterNode.cpp)
    target_include_directories(kb_audio_meter_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(kb_audio_meter_tests PRIVATE miniaudio)
    target_compile_features(kb_audio_meter_tests PRIVATE cxx_std_20)
    add_test(NAME kb_audio_meter_tests COMMAND $<TARGET_FILE:kb_audio_meter_tests>)
    set_tests_properties(kb_audio_meter_tests PROPERTIES TIMEOUT 20)
    kb_apply_quality_gates(kb_audio_meter_tests)

    add_executable(kb_audio_filter_tests tests/AudioFilterTests.cpp ${KB_AUDIO_MINIAUDIO_DSP_SOURCES})
    target_include_directories(kb_audio_filter_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/sources/engine/include)
    target_link_libraries(kb_audio_filter_tests PRIVATE miniaudio miniaudio_reverb_node)
    target_compile_features(kb_audio_filter_tests PRIVATE cxx_std_20)
    add_test(NAME kb_audio_filter_tests COMMAND $<TARGET_FILE:kb_audio_filter_tests>)
    kb_apply_quality_gates(kb_audio_filter_tests)

    add_executable(kb_audio_delay_tests tests/AudioDelayTests.cpp ${KB_AUDIO_MINIAUDIO_DSP_SOURCES})
    target_include_directories(kb_audio_delay_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/sources/engine/include)
    target_link_libraries(kb_audio_delay_tests PRIVATE miniaudio miniaudio_reverb_node)
    target_compile_features(kb_audio_delay_tests PRIVATE cxx_std_20)
    add_test(NAME kb_audio_delay_tests COMMAND $<TARGET_FILE:kb_audio_delay_tests>)
    kb_apply_quality_gates(kb_audio_delay_tests)

    add_executable(kb_audio_reverb_tests tests/AudioReverbTests.cpp ${KB_AUDIO_MINIAUDIO_DSP_SOURCES})
    target_include_directories(kb_audio_reverb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/sources/engine/include)
    target_link_libraries(kb_audio_reverb_tests PRIVATE miniaudio miniaudio_reverb_node)
    target_compile_features(kb_audio_reverb_tests PRIVATE cxx_std_20)
    add_test(NAME kb_audio_reverb_tests COMMAND $<TARGET_FILE:kb_audio_reverb_tests>)
    kb_apply_quality_gates(kb_audio_reverb_tests)

    add_executable(kb_audio_dynamics_tests tests/AudioDynamicsTests.cpp ${KB_AUDIO_MINIAUDIO_DSP_SOURCES})
    target_include_directories(kb_audio_dynamics_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/sources/engine/include)
    target_link_libraries(kb_audio_dynamics_tests PRIVATE miniaudio miniaudio_reverb_node)
    target_compile_features(kb_audio_dynamics_tests PRIVATE cxx_std_20)
    add_test(NAME kb_audio_dynamics_tests COMMAND $<TARGET_FILE:kb_audio_dynamics_tests>)
    kb_apply_quality_gates(kb_audio_dynamics_tests)

    add_executable(kb_audio_runtime_tests
        tests/AudioRuntimeTests.cpp
        ${KB_AUDIO_MINIAUDIO_DSP_SOURCES}
        assets/MiniaudioEncodingFormat.hpp
        assets/ResolvedAudioClip.hpp
        assets/MiniaudioClipResolver.cpp
        playback/MiniaudioVoicePool.cpp
        runtime/MiniaudioEngine.cpp
        runtime/MiniaudioMeterNode.cpp
        runtime/MiniaudioPlaybackBackend.cpp
        runtime/MiniaudioSound.cpp
        scene/MiniaudioBusRegistry.cpp
        scene/MiniaudioListenerSynchronizer.cpp
        scene/MiniaudioOcclusionSampler.cpp
        scene/MiniaudioSourceRegistry.cpp
    )

    target_include_directories(kb_audio_runtime_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_definitions(kb_audio_runtime_tests
        PRIVATE
            KB_AUDIO_MINIAUDIO_TESTING
            KB_AUDIO_RUNTIME_TEST_ASSET_DIR="$<TARGET_FILE_DIR:kb_audio_runtime_tests>/assets"
    )

    set(KB_AUDIO_RUNTIME_TEST_ASSETS
        tests/assets/tone.wav
        tests/assets/tone.flac
        tests/assets/tone.mp3
        tests/assets/tone.ogg
        tests/assets/seek.ogg
    )
    set_source_files_properties(${KB_AUDIO_RUNTIME_TEST_ASSETS} PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(kb_audio_runtime_tests PRIVATE ${KB_AUDIO_RUNTIME_TEST_ASSETS})
    add_custom_command(TARGET kb_audio_runtime_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:kb_audio_runtime_tests>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${KB_AUDIO_RUNTIME_TEST_ASSETS}
            "$<TARGET_FILE_DIR:kb_audio_runtime_tests>/assets"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM
    )

    target_link_libraries(kb_audio_runtime_tests
        PRIVATE
            kb_engine
            miniaudio
            miniaudio_reverb_node
    )

    add_test(NAME kb_audio_runtime_tests COMMAND $<TARGET_FILE:kb_audio_runtime_tests>)
    add_test(NAME kb_audio_mixer_workload COMMAND $<TARGET_FILE:kb_audio_runtime_tests> mixer-dsp-workload)
    add_test(NAME kb_audio_mixer_meters COMMAND $<TARGET_FILE:kb_audio_runtime_tests> mixer-meters)
    set_tests_properties(kb_audio_mixer_meters PROPERTIES LABELS "audio" TIMEOUT 20)
    set_tests_properties(kb_audio_mixer_workload PROPERTIES LABELS "audio;performance" TIMEOUT 20)
    set_tests_properties(kb_audio_runtime_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    kb_apply_quality_gates(kb_audio_runtime_tests)
endif()
