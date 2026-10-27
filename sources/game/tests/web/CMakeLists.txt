cmake_minimum_required(VERSION 3.25)
project(WebInputTests LANGUAGES CXX)
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Web input tests require the Emscripten toolchain")
endif()
add_executable(kb_web_input_tests InputTests.cpp ../../src/web/WebTouchInput.cpp
    ../../src/web/WebGamepadInput.cpp)
target_compile_features(kb_web_input_tests PRIVATE cxx_std_20)
target_include_directories(kb_web_input_tests PRIVATE ../../src/private ../../../engine/include)
target_compile_options(kb_web_input_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wno-dollar-in-identifier-extension
    -Wno-gnu-zero-variadic-macro-arguments -fsanitize=address,undefined -g)
target_link_options(kb_web_input_tests PRIVATE
    -fsanitize=address,undefined -sASSERTIONS=2 -sALLOW_MEMORY_GROWTH=1
    -sENVIRONMENT=web "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString']"
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
set_target_properties(kb_web_input_tests PROPERTIES OUTPUT_NAME InputTests SUFFIX ".html")
set_property(TARGET kb_web_input_tests APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)

add_executable(kb_web_background_tests BackgroundLoadTests.cpp
    ../../../engine/src/assets/streaming/BackgroundLoadService.cpp)
target_compile_features(kb_web_background_tests PRIVATE cxx_std_23)
target_include_directories(kb_web_background_tests PRIVATE ../../../engine/include ../../src/private)
target_compile_options(kb_web_background_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers -fsanitize=address,undefined -g)
target_link_options(kb_web_background_tests PRIVATE
    -fsanitize=address,undefined -sASSERTIONS=2 -sALLOW_MEMORY_GROWTH=1
    -sENVIRONMENT=node -sEXIT_RUNTIME=1)
set_target_properties(kb_web_background_tests PROPERTIES OUTPUT_NAME BackgroundLoadTests SUFFIX ".js")
