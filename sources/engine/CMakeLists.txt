add_library(kb_engine STATIC
    include/engine/ecs/Entity.hpp
    include/engine/ecs/World.hpp
    src/ecs/World.cpp
    src/render/postfx/PostProcessPipeline.cpp
)

target_include_directories(kb_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(kb_engine
    PRIVATE
        flecs::flecs_static
)

if(MSVC)
    target_compile_options(kb_engine PRIVATE /W4 /WX)
else()
    target_compile_options(kb_engine PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
