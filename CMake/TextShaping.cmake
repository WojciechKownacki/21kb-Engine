# Build only the portable shaping cores; platform font discovery stays with the asset system.
add_library(kb_harfbuzz STATIC "${PROJECT_SOURCE_DIR}/third_party/harfbuzz/src/harfbuzz.cc")
target_include_directories(kb_harfbuzz SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/harfbuzz/src")
target_compile_definitions(kb_harfbuzz PRIVATE HB_NO_SUBSET HB_NO_PAINT)
set_target_properties(kb_harfbuzz PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(kb_sheenbidi STATIC "${PROJECT_SOURCE_DIR}/third_party/sheenbidi/Source/SheenBidi.c")
target_include_directories(kb_sheenbidi SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/sheenbidi/Headers")
target_include_directories(kb_sheenbidi PRIVATE "${PROJECT_SOURCE_DIR}/third_party/sheenbidi/Source")
target_compile_definitions(kb_sheenbidi PRIVATE SB_CONFIG_UNITY)
set_target_properties(kb_sheenbidi PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(kb_ui_text_shaping STATIC "${PROJECT_SOURCE_DIR}/sources/engine/src/ui/UITextShaping.cpp")
target_include_directories(kb_ui_text_shaping PRIVATE "${PROJECT_SOURCE_DIR}/sources/engine/include")
target_link_libraries(kb_ui_text_shaping PRIVATE kb_harfbuzz kb_sheenbidi)
target_compile_features(kb_ui_text_shaping PRIVATE cxx_std_20)
set_target_properties(kb_ui_text_shaping PROPERTIES POSITION_INDEPENDENT_CODE ON)
kb_apply_quality_gates(kb_ui_text_shaping)

if(BUILD_TESTING AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(kb_ui_grapheme_tests
        "${PROJECT_SOURCE_DIR}/sources/engine/tests/UITextGraphemeConformanceTests.cpp"
        "${PROJECT_SOURCE_DIR}/sources/engine/src/ui/UITextGraphemes.cpp")
    target_include_directories(kb_ui_grapheme_tests PRIVATE "${PROJECT_SOURCE_DIR}/sources/engine/include")
    target_compile_features(kb_ui_grapheme_tests PRIVATE cxx_std_20)
    kb_apply_quality_gates(kb_ui_grapheme_tests)
    add_test(NAME kb_ui_grapheme_conformance COMMAND kb_ui_grapheme_tests
        "${PROJECT_SOURCE_DIR}/third_party/unicode/GraphemeBreakTest.txt")
    add_executable(kb_ui_text_shaping_tests "${PROJECT_SOURCE_DIR}/sources/engine/tests/UITextShapingTests.cpp")
    target_include_directories(kb_ui_text_shaping_tests PRIVATE "${PROJECT_SOURCE_DIR}/sources/engine/include")
    target_link_libraries(kb_ui_text_shaping_tests PRIVATE kb_ui_text_shaping)
    target_compile_features(kb_ui_text_shaping_tests PRIVATE cxx_std_20)
    kb_apply_quality_gates(kb_ui_text_shaping_tests)
    add_test(NAME kb_ui_text_shaping COMMAND kb_ui_text_shaping_tests
        "${PROJECT_SOURCE_DIR}/sources/editor/content/EditorShell/Fonts/DejaVuSans.ttf"
        "${PROJECT_SOURCE_DIR}/sources/engine/tests/fixtures/fonts/CaretTest.ttf"
        "${PROJECT_SOURCE_DIR}/sources/engine/tests/fixtures/fonts/NotoSansDevanagari-Regular.ttf")
endif()
