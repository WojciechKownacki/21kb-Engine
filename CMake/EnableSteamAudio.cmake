# CPU HRTF/AmbiX processing uses the pinned SDK binary on supported Windows hosts.
# Other providers keep panning and explicitly refuse spatial DSP requests.
set(KB_STEAM_AUDIO_AVAILABLE FALSE)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
    set(_kb_steam_archive "${CMAKE_BINARY_DIR}/_deps/steamaudio-4.8.1.zip")
    set(_kb_steam_root "${CMAKE_BINARY_DIR}/_deps/steamaudio-sdk/steamaudio")
    set(_kb_steam_hash "4a0aa5ec1176f38f0b0993a37c2259d9e86f27e22d5e24f83ec4c3cb9a1d5449")
    if(NOT EXISTS "${_kb_steam_archive}")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")
        file(DOWNLOAD "https://github.com/ValveSoftware/steam-audio/releases/download/v4.8.1/steamaudio_4.8.1.zip"
            "${_kb_steam_archive}" EXPECTED_HASH "SHA256=${_kb_steam_hash}" TLS_VERIFY ON)
    endif()
    file(SHA256 "${_kb_steam_archive}" _kb_steam_actual_hash)
    if(NOT _kb_steam_actual_hash STREQUAL _kb_steam_hash)
        message(FATAL_ERROR "Steam Audio SDK archive hash mismatch")
    endif()
    if(NOT EXISTS "${_kb_steam_root}/include/phonon.h")
        file(ARCHIVE_EXTRACT INPUT "${_kb_steam_archive}" DESTINATION "${CMAKE_BINARY_DIR}/_deps/steamaudio-sdk")
    endif()
    add_library(kb_steam_audio SHARED IMPORTED GLOBAL)
    set_target_properties(kb_steam_audio PROPERTIES
        IMPORTED_LOCATION "${_kb_steam_root}/lib/windows-x64/phonon.dll"
        IMPORTED_IMPLIB "${_kb_steam_root}/lib/windows-x64/phonon.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${_kb_steam_root}/include")
    set(KB_STEAM_AUDIO_AVAILABLE TRUE)
endif()

function(kb_enable_spatial_audio target)
    if(KB_STEAM_AUDIO_AVAILABLE)
        target_link_libraries(${target} PRIVATE kb_steam_audio)
        target_compile_definitions(${target} PRIVATE KB_AUDIO_HAS_STEAMAUDIO=1)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:kb_steam_audio>" "$<TARGET_FILE_DIR:${target}>/phonon.dll"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:kb_steam_audio>" "${CMAKE_BINARY_DIR}/bin/phonon.dll"
            VERBATIM)
    else()
        target_compile_definitions(${target} PRIVATE KB_AUDIO_HAS_STEAMAUDIO=0)
    endif()
endfunction()
