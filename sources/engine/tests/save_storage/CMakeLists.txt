cmake_minimum_required(VERSION 3.25)
project(SaveStorageTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(ENGINE "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT DEFINED MONOCYPHER_SOURCE_DIR)
    include(FetchContent)
    FetchContent_Declare(save_monocypher
        URL https://monocypher.org/download/monocypher-4.0.2.tar.gz
        URL_HASH SHA256=38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c)
    FetchContent_MakeAvailable(save_monocypher)
    set(MONOCYPHER_SOURCE_DIR "${save_monocypher_SOURCE_DIR}")
endif()
add_library(save_crypto STATIC
    "${MONOCYPHER_SOURCE_DIR}/src/monocypher.c"
    "${MONOCYPHER_SOURCE_DIR}/src/optional/monocypher-ed25519.c")
target_include_directories(save_crypto PUBLIC "${MONOCYPHER_SOURCE_DIR}/src" "${MONOCYPHER_SOURCE_DIR}/src/optional")
add_executable(save_storage_tests
    Main.cpp ../SaveGameTests.cpp
    "${ENGINE}/src/save/SaveGame.cpp"
    "${ENGINE}/src/save/SaveGameService.cpp"
    "${ENGINE}/src/save/SaveGameCodec.cpp"
    "${ENGINE}/src/save/SaveGameBinaryIO.cpp"
    "${ENGINE}/src/save/SaveGameMigration.cpp"
    "${ENGINE}/src/save/SaveGameChunkStore.cpp"
    "${ENGINE}/src/platform/UserStorage.cpp"
    "${ENGINE}/src/platform/FileSystemPath.cpp"
    "${ENGINE}/src/assets/streaming/BackgroundLoadService.cpp"
    "${ENGINE}/src/security/Crypto.cpp")
target_include_directories(save_storage_tests PRIVATE "${ENGINE}/include" "${ENGINE}/src/private" "${ENGINE}/tests")
target_link_libraries(save_storage_tests PRIVATE save_crypto)
if(MSVC)
    target_compile_options(save_storage_tests PRIVATE /W4 /WX)
    target_link_libraries(save_storage_tests PRIVATE bcrypt)
else()
    target_compile_options(save_storage_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(save_storage_tests PRIVATE pthread rt)
    endif()
endif()
enable_testing()
if(EMSCRIPTEN)
    target_compile_options(save_storage_tests PRIVATE -Wno-dollar-in-identifier-extension -Wno-gnu-zero-variadic-macro-arguments)
    target_link_options(save_storage_tests PRIVATE
        -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 -sENVIRONMENT=web -sINVOKE_RUN=0
        "-sEXPORTED_RUNTIME_METHODS=['callMain']" -lidbfs.js
        --pre-js "${ENGINE}/../game/src/web/WebUserStorage.js"
        --shell-file "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    set_target_properties(save_storage_tests PROPERTIES SUFFIX ".html")
    set_property(TARGET save_storage_tests APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shell.html" "${ENGINE}/../game/src/web/WebUserStorage.js")
else()
    add_test(NAME save_storage COMMAND save_storage_tests)
    add_test(NAME save_storage_write COMMAND save_storage_tests write "${CMAKE_BINARY_DIR}/persistence")
    add_test(NAME save_storage_reload COMMAND save_storage_tests read "${CMAKE_BINARY_DIR}/persistence")
    set_tests_properties(save_storage_write PROPERTIES FIXTURES_SETUP persisted_save)
    set_tests_properties(save_storage_reload PROPERTIES FIXTURES_REQUIRED persisted_save)
endif()
