add_executable(kb_editor
    include/kb/editor/EditorApplication.hpp
    include/kb/editor/docking/DockTypes.hpp
    include/kb/editor/theme/EditorTheme.hpp
    src/app/EditorApplication.cpp
    src/app/main.cpp
    src/docking/DefaultDockWorkspace.cpp
    src/docking/DockDropPreviewResolver.cpp
    src/docking/DockGeometry.cpp
    src/docking/DockHitTester.cpp
    src/docking/DockLayoutBuilder.cpp
    src/docking/DockNodeFactory.cpp
    src/docking/DockTabIndexResolver.cpp
    src/docking/DockTypes.cpp
    src/docking/EditorDockController.cpp
    src/docking/EditorDockModel.cpp
    src/docking/EditorFloatingWindowManager.cpp
    src/private/docking/DefaultDockWorkspace.hpp
    src/private/docking/DockDropPreviewResolver.hpp
    src/private/docking/DockGeometry.hpp
    src/private/docking/DockHitTester.hpp
    src/private/docking/DockLayoutBuilder.hpp
    src/private/docking/DockNode.hpp
    src/private/docking/DockNodeFactory.hpp
    src/private/docking/DockTabIndexResolver.hpp
    src/private/docking/EditorDockController.hpp
    src/private/docking/EditorDockModel.hpp
    src/private/docking/EditorFloatingWindowManager.hpp
    src/private/rendering/FloatingWindowControlRenderer.hpp
    src/private/rendering/EditorGdiRenderer.hpp
    src/private/rendering/GdiDrawing.hpp
    src/private/rendering/HeroIconCatalog.hpp
    src/private/rendering/HeroIconKind.hpp
    src/private/rendering/HeroIconPainter.hpp
    src/private/rendering/PanelContentRenderer.hpp
    src/private/windowing/FloatingWindowControlActions.hpp
    src/private/windowing/FloatingWindowControlHitTester.hpp
    src/private/windowing/FloatingWindowControlInteractor.hpp
    src/private/windowing/FloatingWindowControlKind.hpp
    src/private/windowing/FloatingWindowControlLayout.hpp
    src/rendering/EditorGdiRenderer.cpp
    src/rendering/FloatingWindowControlRenderer.cpp
    src/rendering/PanelContentRenderer.cpp
    src/windowing/FloatingWindowControlActions.cpp
    src/windowing/FloatingWindowControlInteractor.cpp
    ../../third_party/heroicons/LICENSE
    ../../third_party/heroicons/README.third_party.md
    ../../third_party/heroicons/optimized/24/outline/minus.svg
    ../../third_party/heroicons/optimized/24/outline/stop.svg
    ../../third_party/heroicons/optimized/24/outline/x-mark.svg
)

target_include_directories(kb_editor
    PRIVATE
        include
        src/private
)

target_link_libraries(kb_editor
    PRIVATE
        kb_engine
)

if(WIN32)
    target_link_libraries(kb_editor PRIVATE user32 gdi32 dwmapi msimg32)
else()
    message(FATAL_ERROR "kb_editor currently has a Win32 platform backend only. Add a platform backend for this OS before building the editor.")
endif()

if(MSVC)
    target_compile_options(kb_editor PRIVATE /W4 /WX)
else()
    target_compile_options(kb_editor PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
