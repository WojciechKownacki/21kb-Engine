add_executable(kb21hub WIN32
    src/main.cpp
    src/HubApplication.cpp
    src/HubProjectActions.cpp
    src/HubProjectStore.cpp
    src/HubRenderer.cpp
    src/HubText.cpp
    src/private/HubApplication.hpp
    src/private/HubGeometry.hpp
    src/private/HubProjectActions.hpp
    src/private/HubProjectFilters.hpp
    src/private/HubProjectStore.hpp
    src/private/HubRenderer.hpp
    src/private/HubState.hpp
    src/private/HubText.hpp
)

target_include_directories(kb21hub
    PRIVATE
        src/private
        ../editor/include
)

set_target_properties(kb21hub PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

target_link_libraries(kb21hub
    PRIVATE
        kb_engine
        user32
        gdi32
        gdiplus
        dwmapi
        comdlg32
        shell32
        ole32
)

kb_apply_quality_gates(kb21hub)
