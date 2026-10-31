#include "app/EditorHeadlessPopupChecks.hpp"

#if defined(_WIN32)
#include "app/EditorWindowMessageContext.hpp"
#include "app/EditorKeyState.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/ParticleEditorPanelLayout.hpp"
#include "rendering/DockTabControlGeometry.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "windowing/EditorDisplayRecovery.hpp"
#include "app/EditorWindowMessageRouter.hpp"
#include "inspection/InspectorAddComponentBrowserModel.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "rendering/EditorOverlayPopupWindow.hpp"
#include "rendering/EditorToolbarLayout.hpp"
#include "rendering/EditorToolbarMenuOverlayWindow.hpp"
#include "rendering/EditorToolbarRenderer.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/MainWindowBackBufferPainter.hpp"
#include "scene/EditorSceneContext.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <utility>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {
namespace {

constexpr wchar_t kEditorWindowClassName[] = L"KBEditorHeadlessEditorWindow";
constexpr wchar_t kViewportWindowClassName[] = L"KBEditorSceneBgfxViewport";
constexpr wchar_t kAddComponentWindowClassName[] = L"KBEditorInspectorAddComponentOverlay";
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 760;
constexpr int kOffscreen = -12000;

[[nodiscard]] RECT ToRect(const DockRect& rect) noexcept {
    return RECT{ rect.x, rect.y, rect.x + rect.width, rect.y + rect.height };
}

[[nodiscard]] bool SameRect(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

[[nodiscard]] LPARAM PointParam(int x, int y) noexcept {
    return MAKELPARAM(x, y);
}

[[nodiscard]] POINT Center(const RECT& rect) noexcept {
    return POINT{ (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2 };
}

// An editor window as the application builds one, minus the application: its
// messages go through EditorWindowMessageRouter, the router the editor's window
// procedure dispatches to, with the same kinds of state behind it.
class EditorWindowUnderTest final {
public:
    EditorWindowUnderTest(EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport) noexcept
        : sceneContext_(sceneContext)
        , viewport_(viewport) {}

    ~EditorWindowUnderTest() {
        Destroy();
    }

    EditorWindowUnderTest(const EditorWindowUnderTest&) = delete;
    EditorWindowUnderTest& operator=(const EditorWindowUnderTest&) = delete;

    [[nodiscard]] bool Create() {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_DBLCLKS;
        windowClass.lpfnWndProc = &EditorWindowUnderTest::WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.lpszClassName = kEditorWindowClassName;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        window_ = CreateWindowExW(
            0, kEditorWindowClassName, L"", WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            kOffscreen, kOffscreen, kWindowWidth, kWindowHeight, nullptr, nullptr, instance, this);
        if (window_ == nullptr) {
            return false;
        }
        floatingWindows_.Lifecycle().Configure(instance, window_, metrics_);
        dockController_.Configure(window_, dockModel_, floatingWindows_, metrics_);
        running_ = true;
        routing_ = true;
        // Shown, so it gets painted, but off screen, at the bottom and never activated.
        SetWindowPos(window_, HWND_BOTTOM, kOffscreen, kOffscreen, kWindowWidth, kWindowHeight,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        return true;
    }

    void Destroy() {
        if (window_ == nullptr) {
            return;
        }
        // The router treats the destruction of its main window as the end of the
        // application, so the window leaves the router before it goes.
        routing_ = false;
        MainWindowBackBufferPainter::HideAllOverlays();
        floatingWindows_.Lifecycle().Shutdown();
        // Drain native framebuffer creation before destroying this test host;
        // the render thread must not create a swap chain for an expired HWND.
        viewport_.SetHostSurfaceSuspended(window_, true);
        for (int frame = 0; frame < 3; ++frame) static_cast<void>(viewport_.AdvanceAsyncReadbacks());
        const HWND window = window_;
        window_ = nullptr;
        DestroyWindow(window);
        for (int frame = 0; frame < 3; ++frame) static_cast<void>(viewport_.AdvanceAsyncReadbacks());
    }

    // Runs everything queued for this thread the way the editor's message loop does.
    void Pump() const {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        MSG message{};
        while (std::chrono::steady_clock::now() < deadline) {
            bool any = false;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
                any = true;
                if (message.message == WM_QUIT) {
                    PostQuitMessage(static_cast<int>(message.wParam));
                    return;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!any) {
                return;
            }
        }
    }

    void Click(HWND target, POINT point) const {
        PostMessageW(target, WM_MOUSEMOVE, 0, PointParam(point.x, point.y));
        PostMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, PointParam(point.x, point.y));
        PostMessageW(target, WM_LBUTTONUP, 0, PointParam(point.x, point.y));
        Pump();
    }

    [[nodiscard]] HWND Window() const noexcept {
        return window_;
    }

    [[nodiscard]] EditorPlayMode Mode() const noexcept { return playMode_.Mode(); }
    [[nodiscard]] EditorDockModel& Dock() noexcept { return dockModel_; }

    [[nodiscard]] EditorShellInteractionState& Shell() noexcept {
        return shell_;
    }

    [[nodiscard]] DockLayout Layout() const {
        RECT client{};
        GetClientRect(window_, &client);
        return dockModel_.Queries().BuildLayout(
            client.right - client.left, client.bottom - client.top,
            metrics_.menuHeight, metrics_.tabStripHeight,
            metrics_.tabMinWidth, metrics_.tabWidth, metrics_.splitterSize);
    }

    [[nodiscard]] std::optional<DockPanelLayout> ActivePanel(DockPanelKind kind) const {
        for (const DockPanelLayout& panelLayout : Layout().panels) {
            const DockPanel* panel = dockModel_.Queries().FindPanel(panelLayout.panelId);
            if (panelLayout.active && panel != nullptr && panel->kind == kind) {
                return panelLayout;
            }
        }
        return std::nullopt;
    }

private:
    [[nodiscard]] EditorWindowMessageContext Context() noexcept {
        return EditorWindowMessageContext{
            window_, running_, dockModel_, sceneContext_, theme_, metrics_, renderer_, backendSettings_,
            viewport_, floatingWindows_, dockController_, hierarchySelection_, playMode_, shell_, pointerDrag_,
        };
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* editor = reinterpret_cast<EditorWindowUnderTest*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (editor != nullptr && editor->routing_ && editor->window_ == window) {
            return EditorWindowMessageRouter{ editor->Context() }.Handle(window, message, wparam, lparam);
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    EditorSceneContext& sceneContext_;
    EditorSceneBgfxViewport& viewport_;
    HWND window_ = nullptr;
    bool running_ = false;
    bool routing_ = false;
    EditorDockModel dockModel_;
    EditorTheme theme_ = MakeEditorDarkTheme();
    EditorMetrics metrics_{};
    EditorGdiRenderer renderer_;
    EditorRenderBackendSettings backendSettings_;
    EditorFloatingWindowManager floatingWindows_;
    EditorDockController dockController_;
    EditorHierarchySelectionController hierarchySelection_;
    EditorPlayModeState playMode_;
    EditorShellInteractionState shell_;
    EditorPointerDragState pointerDrag_;
};

[[nodiscard]] HWND FindOwnedPopup(HWND owner, const wchar_t* className) noexcept {
    for (HWND window = FindWindowExW(nullptr, nullptr, className, nullptr);
         window != nullptr;
         window = FindWindowExW(nullptr, window, className, nullptr)) {
        if (GetWindow(window, GW_OWNER) == owner) {
            return window;
        }
    }
    return nullptr;
}

[[nodiscard]] HWND FindVisibleViewportSurface(HWND host) noexcept {
    for (HWND child = FindWindowExW(host, nullptr, kViewportWindowClassName, nullptr);
         child != nullptr;
         child = FindWindowExW(host, child, kViewportWindowClassName, nullptr)) {
        if (IsWindowVisible(child) != 0) {
            return child;
        }
    }
    return nullptr;
}

// True when `upper` is above `lower` in the z-order of top-level windows.
[[nodiscard]] bool Above(HWND upper, HWND lower) noexcept {
    for (HWND window = GetWindow(upper, GW_HWNDNEXT); window != nullptr; window = GetWindow(window, GW_HWNDNEXT)) {
        if (window == lower) {
            return true;
        }
    }
    return false;
}

// Presents the scene into the window's scene panel, which is what makes the
// panel's child render surface visible there.
[[nodiscard]] bool PresentScenePanel(
    EditorSceneBgfxViewport& viewport, EditorSceneContext& sceneContext, HWND host, const DockPanelLayout& panel) {
    const RECT bounds = ToRect(panel.content);
    const auto width = static_cast<std::uint32_t>(bounds.right - bounds.left);
    const auto height = static_cast<std::uint32_t>(bounds.bottom - bounds.top);
    EditorSceneBgfxViewport::PresentSettings settings{};
    settings.renderWidth = width;
    settings.renderHeight = height;
    settings.viewportKey = panel.panelId;
    kb::render::SceneRenderCamera camera{};
    bx::mtxLookAt(camera.view.data(), bx::Vec3{ 0.0F, 0.0F, 3.0F }, bx::Vec3{ 0.0F, 0.0F, 0.0F });
    kb::render::SceneDepthPolicy::MakePerspective(
        camera.projection.data(), 60.0F, static_cast<float>(width) / static_cast<float>(height), 0.05F, 100.0F,
        kb::render::SceneDepthPolicy::HomogeneousDepth());
    settings.cameraOverride = camera;
    settings.sceneRevision = sceneContext.SceneRenderRevision();
    settings.sceneDirtyBaseRevision = settings.sceneRevision;
    settings.sceneFullSyncRequired = true;
    settings.editorSceneOverlaysEnabled = false;
    settings.selectionMaskEnabled = false;
    settings.selectionOutlineEnabled = false;
    settings.drawSafeArea = false;
    viewport.BeginPaintLayout(host);
    viewport.Present(host, bounds, sceneContext.Scene(), settings);
    viewport.EndPaintLayout();
    return std::string_view{ viewport.ActiveBackendLabel() } != "Not initialized";
}

// What the window paints, read back through WM_PRINTCLIENT (32-bit BGRA rows).
struct ClientPixels {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    [[nodiscard]] bool Save(const std::filesystem::path& path) const {
        if (pixels.empty()) return false;
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        BITMAPFILEHEADER file{}; file.bfType = 0x4d42;
        file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size() * sizeof(std::uint32_t));
        BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -height;
        info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&file), sizeof(file));
        output.write(reinterpret_cast<const char*>(&info), sizeof(info));
        output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size() * sizeof(std::uint32_t)));
        return static_cast<bool>(output);
    }

    [[nodiscard]] int BrightPixelsIn(const RECT& rect, int threshold) const noexcept {
        int count = 0;
        for (int y = std::max(0, static_cast<int>(rect.top)); y < std::min(height, static_cast<int>(rect.bottom)); ++y) {
            for (int x = std::max(0, static_cast<int>(rect.left)); x < std::min(width, static_cast<int>(rect.right)); ++x) {
                const std::uint32_t pixel = pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                const int blue = static_cast<int>(pixel & 0xFFU);
                const int green = static_cast<int>((pixel >> 8U) & 0xFFU);
                const int red = static_cast<int>((pixel >> 16U) & 0xFFU);
                if (std::max(red, std::max(green, blue)) >= threshold) {
                    ++count;
                }
            }
        }
        return count;
    }
};

[[nodiscard]] ClientPixels CaptureClient(HWND window) {
    ClientPixels capture{};
    RECT client{};
    GetClientRect(window, &client);
    capture.width = client.right - client.left;
    capture.height = client.bottom - client.top;
    if (capture.width <= 0 || capture.height <= 0) {
        return {};
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = capture.width;
    info.bmiHeader.biHeight = -capture.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC memory = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (memory == nullptr || bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory != nullptr) {
            DeleteDC(memory);
        }
        return {};
    }
    const HGDIOBJ previous = SelectObject(memory, bitmap);
    SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    GdiFlush();
    const auto* source = static_cast<const std::uint32_t*>(bits);
    capture.pixels.assign(source, source + static_cast<std::size_t>(capture.width) * static_cast<std::size_t>(capture.height));
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    return capture;
}

// Checks the open menu's popup: a visible top-level window owned by the editor
// window, above it, exactly over the dropdown, over the scene viewport, with every
// row painted. Appends what failed to `detail`.
[[nodiscard]] bool CheckMenuPopup(
    EditorWindowUnderTest& editor, HWND viewportSurface, EditorMenuCommand menu, std::string_view name, std::string& detail) {
    const HWND host = editor.Window();
    const HWND popup = FindOwnedPopup(host, EditorToolbarMenuOverlayWindow::ClassName);
    if (editor.Shell().OpenMenu() != menu) {
        detail += std::string{ name } + "-not-open;";
        return false;
    }
    if (popup == nullptr || IsWindowVisible(popup) == 0) {
        detail += std::string{ name } + "-no-popup;";
        return false;
    }
    bool succeeded = true;
    if (GetAncestor(popup, GA_PARENT) != GetDesktopWindow()) {
        detail += std::string{ name } + "-popup-not-top-level;";
        succeeded = false;
    }
    if (!Above(popup, host)) {
        detail += std::string{ name } + "-popup-below-window;";
        succeeded = false;
    }

    const int rows = editor.Shell().MenuRowCount(menu);
    const EditorMenuRects rects = EditorToolbarRenderer::ResolveMenu(ToRect(editor.Layout().menu), menu, rows);
    const RECT paintBounds = EditorToolbarRenderer::MenuDropdownPaintBounds(rects);
    RECT popupRect{};
    GetWindowRect(popup, &popupRect);
    if (rows <= 0 || !SameRect(popupRect, EditorOverlayPopupWindow::OwnerClientToScreen(host, paintBounds))) {
        detail += std::string{ name } + "-popup-misplaced;";
        succeeded = false;
    }
    RECT surfaceRect{};
    RECT overlap{};
    GetWindowRect(viewportSurface, &surfaceRect);
    if (IntersectRect(&overlap, &surfaceRect, &popupRect) == 0) {
        detail += std::string{ name } + "-not-over-viewport;";
        succeeded = false;
    }

    const ClientPixels pixels = CaptureClient(popup);
    for (int row = 0; row < rows; ++row) {
        RECT rowRect = rects.dropdownRows[static_cast<std::size_t>(row)];
        OffsetRect(&rowRect, -paintBounds.left, -paintBounds.top);
        // A row's label is light text on the dark menu surface.
        if (pixels.BrightPixelsIn(rowRect, 150) < 8) {
            detail += std::string{ name } + "-row-" + std::to_string(row) + "-not-painted;";
            succeeded = false;
        }
    }
    return succeeded;
}

// Key presses as the keyboard delivers them to the window with keyboard focus:
// WM_KEYDOWN/WM_KEYUP, turned into WM_CHAR by TranslateMessage in the pump.
void TypeKeys(const EditorWindowUnderTest& editor, HWND target, std::string_view letters) {
    for (const char letter : letters) {
        const auto key = static_cast<UINT>(std::toupper(static_cast<unsigned char>(letter)));
        const UINT scanCode = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
        PostMessageW(target, WM_KEYDOWN, key, static_cast<LPARAM>(1U | (scanCode << 16U)));
        PostMessageW(target, WM_KEYUP, key, static_cast<LPARAM>(1U | (scanCode << 16U) | (3U << 30U)));
    }
    editor.Pump();
}

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string lower{ text };
    for (char& character : lower) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lower;
}

[[nodiscard]] POINT MenuItemCenter(EditorWindowUnderTest& editor, EditorMenuCommand menu) {
    const EditorMenuRects rects = EditorToolbarRenderer::ResolveMenu(ToRect(editor.Layout().menu), EditorMenuCommand::None, 0);
    return Center(EditorToolbarLayout::MenuRectByCommand(rects, menu));
}

// Drive the actual modal controls from the same message queue as the editor.
thread_local bool particleDialogSeen = false;
LRESULT CALLBACK ParticleDialogInput(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HCBT_ACTIVATE) {
        const HWND window = reinterpret_cast<HWND>(wparam);
        wchar_t name[128]{};
        GetClassNameW(window, name, 128);
        if (std::wstring_view{name} == L"KBEditorTextEntryDialog") {
            particleDialogSeen = true;
            const HWND edit = FindWindowExW(window, nullptr, L"EDIT", nullptr);
            SetWindowTextW(edit, L"0,24,0;0.5,96,3;1,48,0");
            PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
        } else if (std::wstring_view{name} == L"KBEditorMaterialColorPickerDialog") {
            particleDialogSeen = true;
            PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, PointParam(200, 60));
            PostMessageW(window, WM_LBUTTONUP, 0, PointParam(200, 60));
            PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, PointParam(280, 375));
            PostMessageW(window, WM_LBUTTONUP, 0, PointParam(280, 375));
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

} // namespace

EditorHeadlessPopupCheckResult EditorHeadlessPopupChecks::VerifyParticleControls(
    EditorSceneContext& context, EditorSceneBgfxViewport& viewport) {
    const auto* asset = context.ParticleEditorWorkingAsset();
    if (!asset || asset->emitters.empty()) return {false, "missing particle document"};
    const auto assetId = context.ParticleEditorAssetId();
    const auto emitter = asset->emitters.front().emitterId;
    static_cast<void>(context.SelectParticleEditorEmitter(emitter));
    const auto& modules = context.ParticleEditorWorkingAsset()->emitters.front().modules;
    const auto colorModule = std::find_if(modules.begin(), modules.end(), [](const auto& module) {
        return module.type == kb::scene::ParticleModuleType::ColorOverLife;
    });
    if (colorModule == modules.end()) return {false, "gradient fixture"};
    const auto color = colorModule->moduleId;
    if (!context.SaveParticleEditorAsset()) return {false, "save fixture"};
    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
    const auto serialize = [&] { return kb::scene::ParticleEffectAssetIO::Serialize(*context.ParticleEditorWorkingAsset(), diagnostics); };
    const auto before = serialize();
    EditorWindowUnderTest editor(context, viewport);
    if (!editor.Create() || !editor.Dock().Commands().ActivatePanelKind(DockPanelKind::ParticleEditor, DockArea::Center))
        return {false, "particle panel activation"};
    auto panel = editor.ActivePanel(DockPanelKind::ParticleEditor);
    if (!panel) return {false, "particle panel layout"};
    static_cast<void>(editor.Dock().Commands().ToggleMaximizedLeaf(panel->leafId));
    const auto layout = [&] {
        const auto inspector = context.ParticleEditorInspector();
        return ParticleEditorPanelLayoutResolver::Resolve(ToRect(editor.ActivePanel(DockPanelKind::ParticleEditor)->content),
            context.ParticleEditorEmitterRows(), context.ParticleEditorWorkspace().ComposerScrollOffset(),
            GetDpiForWindow(editor.Window()), &inspector, context.ParticleEditorRecipes().size(), &context.ParticleEditorWorkspace());
    };
    for (const auto type : {kb::scene::ParticleOutputType::Volumetric, kb::scene::ParticleOutputType::Decal}) {
        context.SetParticleEditorComposerScrollOffset(0);
        auto geometry = layout();
        const auto index = static_cast<std::size_t>(type);
        if (index >= geometry.outputChoiceCount) return {false, "depth output choice missing"};
        context.SetParticleEditorComposerScrollOffset(std::max<LONG>(0,
            geometry.outputChoices[index].top - geometry.composer.top - 40));
        geometry = layout();
        editor.Click(editor.Window(), Center(geometry.outputChoices[index]));
        if (context.ParticleEditorWorkingAsset()->emitters.front().output.type != type)
            return {false, "depth output click did not change type"};
        {
            EditorScriptedKeyScope control(VK_CONTROL);
            SendMessageW(editor.Window(), WM_KEYDOWN, 'Z', 0);
            editor.Pump();
        }
        if (serialize() != before) return {false, "depth output undo lost serialized fields"};
    }
    // Each advertised blend choice must traverse the real chip hit-test and undo routing.
    static_cast<void>(context.SelectParticleEditorEmitter(emitter));
    const auto outputInspector = context.ParticleEditorInspector();
    const auto blendRow = std::find_if(outputInspector.properties.begin(), outputInspector.properties.end(), [](const auto& row) {
        return row.property == kb::particle_editor::ParticleEditorProperty::OutputBlend;
    });
    if (blendRow == outputInspector.properties.end()) return {false, "blend property missing"};
    const auto blendIndex = static_cast<std::size_t>(blendRow - outputInspector.properties.begin());
    const auto originalBlend = context.ParticleEditorWorkingAsset()->emitters.front().output.blend;
    for (unsigned choice = 0U; choice < 6U; ++choice) {
        context.SetParticleEditorComposerScrollOffset(0);
        auto geometry = layout();
        context.SetParticleEditorComposerScrollOffset(std::max<LONG>(0,
            geometry.propertyRows[blendIndex].bounds.top - geometry.composer.top - 40));
        geometry = layout();
        if (geometry.propertyRows[blendIndex].enumChipCount != 6U) return {false, "blend choices missing"};
        editor.Click(editor.Window(), Center(geometry.propertyRows[blendIndex].enumChips[choice]));
        if (static_cast<unsigned>(context.ParticleEditorWorkingAsset()->emitters.front().output.blend) != choice)
            return {false, "real blend chip did not edit output"};
        if (choice != static_cast<unsigned>(originalBlend)) {
            EditorScriptedKeyScope control(VK_CONTROL);
            SendMessageW(editor.Window(), WM_KEYDOWN, 'Z', 0);
            editor.Pump();
        }
        if (serialize() != before) return {false, "blend undo changed other fields"};
    }
    const auto editProperty = [&](kb::particle_editor::ParticleEditorPropertyWidget widget) {
        auto inspector = context.ParticleEditorInspector();
        const auto row = std::find_if(inspector.properties.begin(), inspector.properties.end(),
            [widget](const auto& item) { return item.widget == widget; });
        if (row == inspector.properties.end()) return false;
        const auto index = static_cast<std::size_t>(row - inspector.properties.begin());
        context.SetParticleEditorComposerScrollOffset(0);
        auto geometry = layout();
        context.SetParticleEditorComposerScrollOffset(std::max<LONG>(0,
            geometry.propertyRows[index].bounds.top - geometry.composer.top - 40));
        geometry = layout();
        const auto point = Center(geometry.propertyRows[index].curvePreview);
        const auto hit = ParticleEditorPanelLayoutResolver::HitTest(geometry, point.x, point.y);
        if (hit.action != ParticleEditorPanelAction::EditProperty || hit.propertyIndex != index) return false;
        particleDialogSeen = false;
        const HHOOK hook = SetWindowsHookExW(WH_CBT, ParticleDialogInput, nullptr, GetCurrentThreadId());
        if (!hook) return false;
        editor.Click(editor.Window(), point);
        UnhookWindowsHookEx(hook);
        return particleDialogSeen;
    };
    static_cast<void>(context.SelectParticleEditorEmitter(emitter));
    if (!editProperty(kb::particle_editor::ParticleEditorPropertyWidget::Curve)) return {false, "real curve dialog"};
    if (context.ParticleEditorWorkingAsset()->emitters.front().spawn.rateOverTime.keyframes.size() != 3U)
        return {false, "curve control did not change keys"};
    const auto beforeGradient = serialize();
    static_cast<void>(context.SelectParticleEditorModule(emitter, color));
    if (!editProperty(kb::particle_editor::ParticleEditorPropertyWidget::Gradient)) return {false, "real gradient picker"};
    const auto edited = serialize();
    if (!before || !edited || edited == before || edited == beforeGradient || !context.ParticleEditorDirty()) return {false, "controls did not edit document"};
    {
        EditorScriptedKeyScope control(VK_CONTROL);
        const auto key = [&](WPARAM value) { SendMessageW(editor.Window(), WM_KEYDOWN, value, 0); editor.Pump(); };
        key('Z'); key('Z');
        if (serialize() != before) return {false, "particle Ctrl+Z lost original fields"};
        key('Y'); key('Y');
        if (serialize() != edited) return {false, "particle Ctrl+Y lost edited fields"};
        key('S');
    }
    if (context.ParticleEditorDirty()) return {false, "particle Ctrl+S did not save"};
    if (!context.TickParticleEditorPreview(0.1F) || context.ParticleEditorPreviewRevision() == 0U)
        return {false, "edited live preview did not advance"};
    const auto liveColor = [&]() -> std::optional<kb::math::Color> {
        const auto* scene = context.ParticleEditorPreviewScene();
        if (!scene) return std::nullopt;
        for (const auto instance : kb::particles::ParticlePlayback::LiveInstanceIds(*scene)) {
            const auto particles = kb::particles::ParticlePlayback::LiveParticleStates(*scene, instance);
            if (!particles.empty()) return particles.front().color;
        }
        return std::nullopt;
    };
    const auto execution = context.ParticleEditorInspector().execution;
    if (!execution || !execution->cpuWorkProfiled || execution->cpuRenderRecords == 0U)
        return {false, "preview did not publish measured emitter integration work"};
    const auto tinted = liveColor();
    if (!tinted || tinted->r <= tinted->g + 0.2F) return {false, "gradient did not tint live particles"};
    context.SetParticleEditorComposerScrollOffset(0);
    auto geometry = layout();
    const auto row = std::find_if(geometry.moduleRows.begin(), geometry.moduleRows.begin() + geometry.moduleRowCount,
        [color](const auto& value) { return value.moduleId == color; });
    if (row == geometry.moduleRows.begin() + geometry.moduleRowCount) return {false, "module toggle missing"};
    const auto index = static_cast<std::size_t>(row - geometry.moduleRows.begin());
    context.SetParticleEditorComposerScrollOffset(std::max<LONG>(0, row->bounds.top - geometry.composer.top - 40));
    geometry = layout();
    editor.Click(editor.Window(), Center(geometry.moduleRows[index].enabledToggle));
    if (!context.TickParticleEditorPreview(0.1F)) return {false, "preview after topology edit"};
    const auto untinted = liveColor();
    if (!untinted || untinted->g <= tinted->g + 0.2F) return {false, "module toggle did not change live particles"};
    {
        EditorScriptedKeyScope control(VK_CONTROL);
        SendMessageW(editor.Window(), WM_KEYDOWN, 'Z', 0);
        editor.Pump();
    }
    if (serialize() != edited || context.ParticleEditorDirty()) return {false, "topology undo lost saved document"};
    editor.Click(editor.Window(), Center(ToRect(DockTabControlGeometry::CloseRect(
        editor.ActivePanel(DockPanelKind::ParticleEditor)->tab))));
    if (context.HasParticleEditorAsset() || context.ParticleEditorPreviewScene() != nullptr)
        return {false, "close retained particle document or preview"};
    if (!context.OpenParticleEditorAsset(assetId) || serialize() != edited)
        return {false, "saved curve/gradient fields lost on reopening"};
    return {true, "real curve and gradient dialogs; Ctrl+Z/Y/S; full document reload; live preview and close"};
}

EditorHeadlessPopupCheckResult EditorHeadlessPopupChecks::VerifyDebuggerInteraction(
    EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport) {
    if (!sceneContext.LuaDebuggerPaused()) return {false, "debugger must be paused"};
    EditorWindowUnderTest editor(sceneContext, viewport);
    if (!editor.Create()) return {false, "window creation"};
    for (const auto& panel : editor.Layout().panels) {
        if (panel.panelId == 6U) editor.Click(editor.Window(), Center(ToRect(panel.tab)));
    }
    if (!editor.ActivePanel(DockPanelKind::Console)) return {false, "console tab during breakpoint"};
    const auto before = editor.Dock().Commands().SerializeWorkspace();
    const auto split = editor.Layout().splitters.front();
    const auto point = Center(ToRect(split.rect));
    SendMessageW(editor.Window(), WM_LBUTTONDOWN, MK_LBUTTON, PointParam(point.x, point.y));
    SendMessageW(editor.Window(), WM_MOUSEMOVE, MK_LBUTTON,
        PointParam(point.x + (split.axis == DockSplitAxis::Horizontal ? 40 : 0),
                   point.y + (split.axis == DockSplitAxis::Vertical ? 40 : 0)));
    SendMessageW(editor.Window(), WM_LBUTTONUP, 0, PointParam(point.x, point.y));
    editor.Pump();
    return {sceneContext.LuaDebuggerPaused() && before != editor.Dock().Commands().SerializeWorkspace(),
        "console tab, splitter drag and message pump while Lua remains paused"};
}

EditorHeadlessPopupCheckResult EditorHeadlessPopupChecks::VerifyWorkspaceControls(
    EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport) {
    EditorWindowUnderTest editor(sceneContext, viewport);
    if (!editor.Create()) return {false, "window creation"};
    const auto toolbar = EditorToolbarRenderer::ResolveToolbar(ToRect(editor.Layout().toolbar));
    editor.Click(editor.Window(), Center(toolbar.stopButton));
    editor.Click(editor.Window(), Center(toolbar.pauseButton));
    if (editor.Mode() != EditorPlayMode::Stopped) return {false, "disabled transport activated"};
    const auto count = sceneContext.HierarchyRows().size();
    {
        EditorScriptedKeyScope control(VK_CONTROL);
        const auto key = [&](WPARAM value) { SendMessageW(editor.Window(), WM_KEYDOWN, value, 0); editor.Pump(); };
        key('D');
        if (sceneContext.HierarchyRows().size() != count + 1U) return {false, "Ctrl+D"};
        key('Z');
        if (sceneContext.HierarchyRows().size() != count) return {false, "Ctrl+Z"};
        key('Y');
        if (sceneContext.HierarchyRows().size() != count + 1U) return {false, "Ctrl+Y"};
        key('Z');
        key('S');
    }
    for (const auto& panel : editor.Layout().panels) {
        if (panel.panelId != 6U) continue;
        editor.Click(editor.Window(), Center(ToRect(panel.tab)));
    }
    if (!editor.ActivePanel(DockPanelKind::Console)) return {false, "console tab click"};
    const auto before = editor.Dock().Commands().SerializeWorkspace();
    const auto split = editor.Layout().splitters.front();
    const auto point = Center(ToRect(split.rect));
    SendMessageW(editor.Window(), WM_LBUTTONDOWN, MK_LBUTTON, PointParam(point.x, point.y));
    SendMessageW(editor.Window(), WM_MOUSEMOVE, MK_LBUTTON,
        PointParam(point.x + (split.axis == DockSplitAxis::Horizontal ? 40 : 0),
                   point.y + (split.axis == DockSplitAxis::Vertical ? 40 : 0)));
    SendMessageW(editor.Window(), WM_LBUTTONUP, 0, PointParam(point.x, point.y));
    if (before == editor.Dock().Commands().SerializeWorkspace()) return {false, "splitter drag"};
    // Synthetic display topology drives the same recovery function used by the
    // native creation and display-change paths, including negative coordinates.
    const DockRect dual[]{{-1920, 0, 1920, 1040}, {0, 0, 2560, 1400}};
    const DockRect single[]{{0, 0, 1280, 720}};
    auto floated = RecoverDisplayRect({-1800, 200, 900, 640}, dual);
    if (floated.x != -1800) return {false, "second monitor placement"};
    floated = RecoverDisplayRect(floated, single);
    if (floated.x < 0 || floated.y < 0 || floated.x + floated.width > 1280 || floated.y + floated.height > 720)
        return {false, "removed monitor recovery"};
    ShowWindow(editor.Window(), SW_HIDE);
    for (const unsigned dpi : {96U, 144U, 192U}) {
        RECT suggested{-4000, -3000, -4000 + static_cast<LONG>(1280U * dpi / 96U),
            -3000 + static_cast<LONG>(720U * dpi / 96U)};
        SendMessageW(editor.Window(), WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
        SendMessageW(editor.Window(), WM_DISPLAYCHANGE, 32, 0);
        RECT actual{};
        GetWindowRect(editor.Window(), &actual);
        const auto expected = RecoverNativeDisplayRect({actual.left, actual.top, actual.right - actual.left, actual.bottom - actual.top});
        if (actual.left != expected.x || actual.top != expected.y || actual.right - actual.left != expected.width)
            return {false, "native display/DPI recovery"};
        const auto layout = editor.Layout();
        for (const auto& panel : editor.Dock().Queries().Panels()) {
            if (!panel.visible || panel.area == DockArea::Floating) continue;
            if (!std::ranges::any_of(layout.panels, [&](const auto& entry) { return entry.panelId == panel.id; }))
                return {false, "panel lost on DPI/resize"};
        }
    }
    return {true, "disabled stop/pause; Ctrl+D/Z/Y/S; tab click; splitter drag; display removal; DPI 96/144/192"};
}

EditorHeadlessPopupCheckResult EditorHeadlessPopupChecks::VerifyToolbarMenuOverlay(
    EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport) {
    EditorHeadlessPopupCheckResult result{};
    EditorWindowUnderTest editor(sceneContext, viewport);
    if (!editor.Create()) {
        result.detail = "editor-window-not-created";
        return result;
    }
    const std::optional<DockPanelLayout> scenePanel = editor.ActivePanel(DockPanelKind::Scene);
    if (!scenePanel.has_value() || !PresentScenePanel(viewport, sceneContext, editor.Window(), *scenePanel)) {
        result.detail = "scene-panel-not-presented";
        return result;
    }
    editor.Pump();
    const HWND viewportSurface = FindVisibleViewportSurface(editor.Window());
    if (viewportSurface == nullptr) {
        result.detail = "no-viewport-surface";
        return result;
    }

    std::string& detail = result.detail;
    bool succeeded = true;

    // World: the longest fixed menu, opened with a click on the menu bar.
    editor.Click(editor.Window(), MenuItemCenter(editor, EditorMenuCommand::World));
    succeeded = CheckMenuPopup(editor, viewportSurface, EditorMenuCommand::World, "world", detail) && succeeded;
    if (const HWND popup = FindOwnedPopup(editor.Window(), EditorToolbarMenuOverlayWindow::ClassName); popup != nullptr) {
        const EditorMenuRects rects = EditorToolbarRenderer::ResolveMenu(
            ToRect(editor.Layout().menu), EditorMenuCommand::World, editor.Shell().MenuRowCount(EditorMenuCommand::World));
        const RECT paintBounds = EditorToolbarRenderer::MenuDropdownPaintBounds(rects);
        const auto rowCenter = [&](int row) {
            POINT point = Center(rects.dropdownRows[static_cast<std::size_t>(row)]);
            point.x -= paintBounds.left;
            point.y -= paintBounds.top;
            return point;
        };
        // Hover over the popup reaches the toolbar's hover tracking.
        const POINT hover = rowCenter(2);
        PostMessageW(popup, WM_MOUSEMOVE, 0, PointParam(hover.x, hover.y));
        editor.Pump();
        if (editor.Shell().HoveredMenuRow() != std::optional<int>{ 2 }) {
            detail += "world-hover-not-forwarded;";
            succeeded = false;
        }
        // A click on "Show or Hide Cell Grid" runs that row and closes the menu.
        const bool gridVisible = sceneContext.WorldPartition().GridVisible();
        editor.Click(popup, rowCenter(4));
        if (sceneContext.WorldPartition().GridVisible() == gridVisible) {
            detail += "world-row-click-not-forwarded;";
            succeeded = false;
        }
        sceneContext.WorldPartition().SetGridVisible(gridVisible);
        if (editor.Shell().OpenMenu() != EditorMenuCommand::None || IsWindowVisible(popup) != 0) {
            detail += "world-not-closed-by-row;";
            succeeded = false;
        }
    }

    // Layout: as long as the project's layouts make it.
    editor.Click(editor.Window(), MenuItemCenter(editor, EditorMenuCommand::Layout));
    succeeded = CheckMenuPopup(editor, viewportSurface, EditorMenuCommand::Layout, "layout", detail) && succeeded;
    editor.Click(editor.Window(), MenuItemCenter(editor, EditorMenuCommand::Layout));
    if (const HWND popup = FindOwnedPopup(editor.Window(), EditorToolbarMenuOverlayWindow::ClassName);
        editor.Shell().OpenMenu() != EditorMenuCommand::None || (popup != nullptr && IsWindowVisible(popup) != 0)) {
        detail += "layout-not-closed;";
        succeeded = false;
    }

    result.succeeded = succeeded;
    if (succeeded) {
        detail = "world,layout";
    }
    return result;
}

EditorHeadlessPopupCheckResult EditorHeadlessPopupChecks::VerifyAddComponentSearchInput(
    EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport) {
    EditorHeadlessPopupCheckResult result{};
    if (!sceneContext.Scene().Entities().IsAlive(sceneContext.SelectedEntity())) {
        result.detail = "no-selected-entity";
        return result;
    }
    EditorWindowUnderTest editor(sceneContext, viewport);
    if (!editor.Create()) {
        result.detail = "editor-window-not-created";
        return result;
    }
    if (!editor.ActivePanel(DockPanelKind::Inspector).has_value()) {
        result.detail = "no-inspector-panel";
        return result;
    }

    InspectorPanelState& inspector = sceneContext.Inspector();
    if (!inspector.IsAddComponentBrowserOpen()) {
        inspector.ToggleAddComponentBrowser();
    }
    // The browser opens with its search box active; ending that here makes the
    // click below the thing that starts it.
    inspector.EndTextEdit();
    InvalidateRect(editor.Window(), nullptr, FALSE);
    editor.Pump();
    const HWND browser = FindOwnedPopup(editor.Window(), kAddComponentWindowClassName);
    if (browser == nullptr || IsWindowVisible(browser) == 0) {
        inspector.CloseAddComponentBrowser();
        result.detail = "no-browser-popup";
        return result;
    }

    std::string& detail = result.detail;
    bool succeeded = true;
    // A click must leave activation, and with it the keyboard, with the editor
    // window. WS_EX_NOACTIVATE alone only keeps the popup from taking the
    // foreground from another application; inside the active editor a click
    // activates the popup unless it answers WM_MOUSEACTIVATE with MA_NOACTIVATE.
    if (SendMessageW(browser, WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(editor.Window()),
            MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) != MA_NOACTIVATE) {
        detail += "browser-activates-on-click;";
        succeeded = false;
    }

    RECT client{};
    GetClientRect(browser, &client);
    std::optional<POINT> searchPoint;
    for (int y = client.top; y < client.bottom && !searchPoint.has_value(); ++y) {
        const int x = (client.left + client.right) / 2;
        if (InspectorPanelRenderer::HitTestAddComponentOverlay(client, sceneContext, x, y).property ==
            InspectorPropertyId::AddComponentSearch) {
            searchPoint = POINT{ x, y + 4 };
        }
    }
    if (!searchPoint.has_value()) {
        detail += "no-search-box;";
        succeeded = false;
    } else {
        editor.Click(browser, *searchPoint);
    }
    if (inspector.EditedProperty() != InspectorPropertyId::AddComponentSearch) {
        detail += "click-did-not-start-search;";
        succeeded = false;
    }

    // Typed with the editor window holding the keyboard, as it does after the click...
    TypeKeys(editor, editor.Window(), "me");
    // ...and into the popup itself, which hands keys to the window it belongs to.
    TypeKeys(editor, browser, "sh");
    const std::string typed = inspector.EditedProperty() == InspectorPropertyId::AddComponentSearch
        ? Lower(inspector.EditBuffer())
        : std::string{};
    if (typed != "mesh") {
        detail += "search-text=" + typed + ";";
        succeeded = false;
    }
    const auto pluginEnabled = [&sceneContext](std::string_view pluginId) {
        return sceneContext.IsProjectPluginEnabled(pluginId);
    };
    const std::vector<AddComponentRow> filtered = InspectorAddComponentBrowserModel::Rows({}, typed, pluginEnabled);
    const bool meshRendererListed = std::ranges::any_of(filtered, [](const AddComponentRow& row) {
        return row.label == "Mesh Renderer";
    });
    if (typed.empty() || !meshRendererListed ||
        filtered.size() >= InspectorAddComponentBrowserModel::Rows({}, "e", pluginEnabled).size()) {
        detail += "search-not-filtered;";
        succeeded = false;
    }
    // The popup lists what the search found: its first list row is the first match.
    if (!filtered.empty()) {
        bool firstRowShown = false;
        for (int y = client.top; y < client.bottom && !firstRowShown; ++y) {
            const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTestAddComponentOverlay(
                client, sceneContext, (client.left + client.right) / 2, y);
            firstRowShown = hit.property == InspectorPropertyId::AddComponentOption && hit.index == 0;
        }
        if (!firstRowShown) {
            detail += "filtered-row-not-listed;";
            succeeded = false;
        }
    }

    // Use real input and popup painting for everyday and technical names.
    for (const auto& [query, expected] : {std::pair{"billboard", "Facing Panel"},
             std::pair{"decal", "Surface Cast"}, std::pair{"facing panel", "Facing Panel"}}) {
        {
            EditorScriptedKeyScope control(VK_CONTROL);
            SendMessageW(browser, WM_KEYDOWN, 'A', 0);
            editor.Pump();
        }
        TypeKeys(editor, browser, query);
        const auto matches = InspectorAddComponentBrowserModel::Rows({}, Lower(inspector.EditBuffer()), pluginEnabled);
        if (matches.empty() || matches.front().id != expected ||
            InspectorAddComponentBrowserModel::Description(matches, 0).empty()) {
            detail += std::string{"synonym-help-missing:"} + query + ";"; succeeded = false; continue;
        }
        for (int y = client.top; y < client.bottom; ++y) {
            const auto hit = InspectorPanelRenderer::HitTestAddComponentOverlay(client, sceneContext, client.left + 32, y);
            if (hit.property == InspectorPropertyId::AddComponentOption && hit.index == 0) {
                SendMessageW(browser, WM_MOUSEMOVE, 0, PointParam(client.left + 32, y + 4));
                editor.Pump(); break;
            }
        }
        const ClientPixels painted = CaptureClient(browser);
        // A popup narrower than its default width gives the help text a taller band (see AddComponentHelpRect).
        const int helpHeight = client.right - client.left < 240 ? 112 : 70;
        const RECT help{client.left + 8, client.bottom - helpHeight, client.right - 8, client.bottom - 6};
        if (painted.BrightPixelsIn(help, 130) < 80 ||
            InspectorPanelRenderer::HitTestAddComponentOverlay(client, sceneContext, help.left + 10, help.bottom - 10).property ==
                InspectorPropertyId::AddComponentOption) {
            detail += std::string{"help-not-painted-or-overlaps-list:"} + query + ";"; succeeded = false;
        }
        if (!painted.Save(EditorProjectPaths::ProjectRoot() / "Saved" / "Automation" / (std::string{query} + "-help.bmp"))) {
            detail += "help-capture-failed;"; succeeded = false;
        }
    }

    inspector.CloseAddComponentBrowser();
    InvalidateRect(editor.Window(), nullptr, FALSE);
    editor.Pump();
    if (IsWindowVisible(browser) != 0) {
        detail += "browser-not-closed;";
        succeeded = false;
    }

    result.succeeded = succeeded;
    if (succeeded) {
        detail = "click,type-window,type-popup,filtered,billboard,decal,technical-name,painted-help";
    }
    return result;
}

} // namespace kb::editor

#endif
