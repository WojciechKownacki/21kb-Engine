add_library(kb_audio_miniaudio_plugin SHARED
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

kb_apply_quality_gates(kb_audio_miniaudio_plugin)
