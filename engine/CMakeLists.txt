add_library(kb_engine STATIC
    src/render/postfx/PostProcessPipeline.cpp
)

target_include_directories(kb_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(kb_engine PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
