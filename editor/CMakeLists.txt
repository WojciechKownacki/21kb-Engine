add_executable(kb_editor
    src/EditorApplication.cpp
    src/EditorApplication.hpp
    src/EditorDockModel.cpp
    src/EditorDockModel.hpp
    src/EditorGdiRenderer.cpp
    src/EditorGdiRenderer.hpp
    src/EditorTheme.hpp
    src/main.cpp
)

target_link_libraries(kb_editor
    PRIVATE
        kb_engine
)

if(WIN32)
    target_link_libraries(kb_editor PRIVATE user32 gdi32 dwmapi)
else()
    message(FATAL_ERROR "kb_editor currently has a Win32 platform backend only. Add a platform backend for this OS before building the editor.")
endif()

if(MSVC)
    target_compile_options(kb_editor PRIVATE /W4 /WX)
else()
    target_compile_options(kb_editor PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
