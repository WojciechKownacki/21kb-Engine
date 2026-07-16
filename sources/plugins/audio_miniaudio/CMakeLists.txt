set(KB_AUDIO_MINIAUDIO_PLUGIN_LIBRARY_TYPE STATIC)
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    set(KB_AUDIO_MINIAUDIO_PLUGIN_LIBRARY_TYPE SHARED)
endif()

add_library(kb_audio_miniaudio_plugin ${KB_AUDIO_MINIAUDIO_PLUGIN_LIBRARY_TYPE}
    MiniaudioModule.hpp
    MiniaudioModule.cpp
    MiniaudioSceneSystem.hpp
    MiniaudioSceneSystem.cpp
    assets/MiniaudioClipResolver.hpp
    assets/MiniaudioClipResolver.cpp
    playback/MiniaudioVoicePool.hpp
    playback/MiniaudioVoicePool.cpp
    runtime/MiniaudioEngine.hpp
    runtime/MiniaudioEngine.cpp
    runtime/MiniaudioPlaybackBackend.hpp
    runtime/MiniaudioPlaybackBackend.cpp
    runtime/MiniaudioSound.hpp
    runtime/MiniaudioSound.cpp
    scene/MiniaudioBusRegistry.hpp
    scene/MiniaudioBusRegistry.cpp
    scene/MiniaudioListenerSynchronizer.hpp
    scene/MiniaudioListenerSynchronizer.cpp
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
        miniaudio
)

if(NOT KB_BUILD_PROVIDER_MODULES_AS_DLL)
    target_compile_definitions(kb_audio_miniaudio_plugin
        PUBLIC
            KB_ENGINE_MODULE_STATIC_LINK
    )
endif()

kb_apply_quality_gates(kb_audio_miniaudio_plugin)
