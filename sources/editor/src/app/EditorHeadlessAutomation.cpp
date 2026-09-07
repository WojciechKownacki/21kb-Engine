#include "app/EditorHeadlessAutomation.hpp"

#if defined(_WIN32)
#include "app/EditorWorkspaceSession.hpp"
#include "app/pointer/EditorRightButtonDownRouter.hpp"
#include "docking/EditorWorkspaceArrangement.hpp"
#include "windowing/EditorFloatingWindowFrame.hpp"
#include "windowing/FloatingWindowFactory.hpp"
#include "app/EditorPlayModeState.hpp"
#include "app/EditorPointerDragState.hpp"
#include "app/EditorShellInteractionState.hpp"
#include "app/ParticleEditorPanelInteraction.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "docking/EditorDockModel.hpp"
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorPanelInteraction.hpp"
#include "inspection/ui/InspectorUIComponentModel.hpp"
#include "engine/scene/SceneUI.hpp"
#include "platform/win32/EditorParticleEffectAssetPickerDialog.hpp"
#include "rendering/DockWorkspaceRenderer.hpp"
#include "rendering/FloatingWindowBackBufferPainter.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorHostSurfaceLayoutResolver.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/EditorParticleThumbnailService.hpp"
#include "rendering/ParticleThumbnailTimeline.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "app/scene_viewport/EditorUIRectInteraction.hpp"
#include "app/scene_viewport/EditorSceneViewportToolbarPointerController.hpp"
#include "app/panels/EditorPanelPointerHitContext.hpp"
#include "rendering/ScenePanelContentRenderer.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "rendering/FloatingEditorWindowRenderer.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/MainWindowBackBufferPainter.hpp"
#include "rendering/PanelContentRenderer.hpp"
#include "rendering/ParticleEditorPanelLayout.hpp"
#include "rendering/ScriptEditorPanelRenderer.hpp"
#include "rendering/script_editor/ScriptEditorWindow.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorViewportPreviewState.hpp"
#include "settings/EditorConfigurationStore.hpp"
#include "settings/EditorLayoutLibrary.hpp"
#include "project/EditorProjectPaths.hpp"

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputHaptics.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/platform/win32/Win32XInputHapticsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "kb/render/SceneDepthPolicy.hpp"

#include <bx/math.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <iterator>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <vector>

namespace kb::editor {
namespace {

constexpr RECT kInspectorContent{ 0, 0, 900, 700 };
constexpr std::uint64_t kParticlePickerAnimationTimerTicks = 2U;

struct ScreenshotDimensions {
    int logicalWidth = 0;
    int logicalHeight = 0;
    int dpi = 96;
};

struct ScreenshotProfile {
    std::string_view name;
    ScreenshotDimensions dimensions;
};

constexpr ScreenshotDimensions kDefaultScreenshotDimensions{
    .logicalWidth = 900,
    .logicalHeight = 700,
};

constexpr std::array<ScreenshotProfile, 3U> kEditorScreenshotProfiles{{
    { "1920x1080", { 1920, 1080, 96 } },
    { "1366x768", { 1366, 768, 96 } },
    // A 1280x720 logical client at 144 DPI produces a 1920x1080 capture.
    { "150dpi", { 1280, 720, 144 } },
}};

[[nodiscard]] int PhysicalPixels(int logicalPixels, int dpi) noexcept {
    return static_cast<int>(
        (static_cast<long long>(logicalPixels) * dpi + 48LL) /
        96LL);
}

[[nodiscard]] POINT Center(const RECT& rect) noexcept {
    return POINT{
        (rect.left + rect.right) / 2,
        (rect.top + rect.bottom) / 2,
    };
}

[[nodiscard]] std::string JsonEscape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) >= 0x20U) {
                escaped.push_back(character);
            }
            break;
        }
    }
    return escaped;
}

[[nodiscard]] std::string SafeCheckpoint(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_') {
            result.push_back(character);
        }
    }
    return result.empty() ? "checkpoint" : result;
}

[[nodiscard]] std::optional<InspectorPanelRenderer::Hit>
FindInspectorHit(
    const EditorSceneContext& context,
    InspectorSectionId section,
    InspectorPropertyId property,
    int index = -1,
    InspectorHitKind kind = InspectorHitKind::None, POINT* matchedPoint = nullptr) {
    const bool uiField = InspectorUIComponentModel::Component(section).has_value();
    const bool addButton = section == InspectorSectionId::AddComponent &&
        property == InspectorPropertyId::AddComponentButton;
    int compactX = -1;
    if (const auto component = InspectorUIComponentModel::Component(section); component && index >= 0) {
        const auto rows = InspectorUIComponentModel::Properties(context.Scene(), context.SelectedEntity(), *component);
        if (static_cast<std::size_t>(index) < rows.size()) {
            const auto& group = rows[static_cast<std::size_t>(rows[index].groupStart)];
            if (group.fieldCount > 1 && !group.color) {
                const int left = kInspectorContent.left + (kInspectorContent.right - kInspectorContent.left) * 36 / 100;
                const int width = kInspectorContent.right - left - 24;
                compactX = left + width * (2 * (index - group.groupStart) + 1) / (2 * group.fieldCount);
            }
        }
    }
    const int firstX = compactX >= 0 ? compactX
        : property == InspectorPropertyId::UIAnchorPresets ? 40
        : property == InspectorPropertyId::UIAnchorPreset ? 40 + 64 * (std::max(0, index) % 4)
        : property == InspectorPropertyId::UIRectLayoutField ? (index % 2 == 0 ? 225 : 585)
        : uiField
        ? kInspectorContent.left + (kInspectorContent.right - kInspectorContent.left) * 36 / 100 + 8
        : addButton ? (kInspectorContent.left + kInspectorContent.right) / 2
        : kInspectorContent.left;
    const int lastX = uiField || addButton ? firstX + 1 : kInspectorContent.right;
    for (int scroll = addButton ? InspectorPanelRenderer::MaxScrollOffset(kInspectorContent, context) : 0;;) {
        const int maxScroll = InspectorPanelRenderer::MaxScrollOffset(
            kInspectorContent, context);
        static_cast<void>(
            const_cast<EditorSceneContext&>(context).Inspector()
                .SetScrollOffset(
                    std::min(scroll, maxScroll), maxScroll));
        for (int y = kInspectorContent.top;
             y < kInspectorContent.bottom; ++y) {
            for (int x = firstX; x < lastX; x += 4) {
                const InspectorPanelRenderer::Hit hit =
                    InspectorPanelRenderer::HitTest(
                        kInspectorContent, context, x, y);
                if (hit.section == section &&
                    hit.property == property &&
                    (index < 0 || hit.index == index) &&
                    (kind == InspectorHitKind::None || hit.kind == kind)) {
                    if (matchedPoint != nullptr) *matchedPoint = POINT{x, y};
                    return hit;
                }
            }
        }
        if (scroll >= maxScroll) {
            break;
        }
        scroll = std::min(scroll + 520, maxScroll);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<CLSID> EncoderClsid(
    const wchar_t* mimeType) {
    UINT count = 0U;
    UINT bytes = 0U;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) !=
            Gdiplus::Ok ||
        bytes == 0U) {
        return std::nullopt;
    }
    std::vector<std::byte> storage(bytes);
    auto* encoders =
        reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    if (Gdiplus::GetImageEncoders(
            count, bytes, encoders) != Gdiplus::Ok) {
        return std::nullopt;
    }
    for (UINT index = 0U; index < count; ++index) {
        if (encoders[index].MimeType != nullptr &&
            std::wstring_view{ encoders[index].MimeType } == mimeType) {
            return encoders[index].Clsid;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool ValidateCapturedImage(
    const std::filesystem::path& path,
    bool requireNonUniform) {
    if (!std::filesystem::is_regular_file(path)) {
        return false;
    }
    if (!requireNonUniform) {
        return true;
    }

    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Bitmap image(path.wstring().c_str());
    bool valid = image.GetLastStatus() == Gdiplus::Ok &&
        image.GetWidth() > 0U && image.GetHeight() > 0U;
    Gdiplus::Color first{};
    bool firstSet = false;
    bool varied = false;
    for (UINT y = 0U; valid && !varied && y < image.GetHeight(); ++y) {
        for (UINT x = 0U; x < image.GetWidth(); ++x) {
            Gdiplus::Color pixel{};
            if (image.GetPixel(x, y, &pixel) != Gdiplus::Ok) {
                valid = false;
                break;
            }
            if (!firstSet) {
                first = pixel;
                firstSet = true;
            } else if (pixel.GetValue() != first.GetValue()) {
                varied = true;
                break;
            }
        }
    }
    return valid && varied;
}

[[nodiscard]] InspectorSectionId PhysicsSection(
    PhysicsComponentKind component) noexcept {
    switch (component) {
    case PhysicsComponentKind::Rigidbody:
        return InspectorSectionId::Rigidbody;
    case PhysicsComponentKind::Collider:
        return InspectorSectionId::Collider;
    case PhysicsComponentKind::CharacterController:
        return InspectorSectionId::CharacterController;
    case PhysicsComponentKind::Joint:
        return InspectorSectionId::Joint;
    }
    return InspectorSectionId::None;
}

[[nodiscard]] InspectorPropertyId PhysicsProperty(
    PhysicsComponentKind component) noexcept {
    switch (component) {
    case PhysicsComponentKind::Rigidbody:
        return InspectorPropertyId::RigidbodyField;
    case PhysicsComponentKind::Collider:
        return InspectorPropertyId::ColliderField;
    case PhysicsComponentKind::CharacterController:
        return InspectorPropertyId::CharacterControllerField;
    case PhysicsComponentKind::Joint:
        return InspectorPropertyId::JointField;
    }
    return InspectorPropertyId::None;
}

[[nodiscard]] std::optional<DockPanelKind> ParsePanelKind(
    std::string_view panel) noexcept {
    if (panel == "hierarchy") return DockPanelKind::Hierarchy;
    if (panel == "scene") return DockPanelKind::Scene;
    if (panel == "inspector") return DockPanelKind::Inspector;
    if (panel == "assets") return DockPanelKind::Assets;
    if (panel == "console") return DockPanelKind::Console;
    if (panel == "project_settings") {
        return DockPanelKind::ProjectSettings;
    }
    if (panel == "editor_settings") {
        return DockPanelKind::EditorSettings;
    }
    if (panel == "script_editor") {
        return DockPanelKind::ScriptEditor;
    }
    if (panel == "plugins") return DockPanelKind::Plugins;
    if (panel == "build_game") return DockPanelKind::BuildGame;
    if (panel == "material_editor") {
        return DockPanelKind::MaterialEditor;
    }
    if (panel == "skeletal_mesh_editor") {
        return DockPanelKind::SkeletalMeshEditor;
    }
    if (panel == "animation_clip_editor") {
        return DockPanelKind::AnimationClipEditor;
    }
    if (panel == "animator_editor") {
        return DockPanelKind::AnimatorEditor;
    }
    if (panel == "particle_editor") {
        return DockPanelKind::ParticleEditor;
    }
    return std::nullopt;
}

template <typename Paint>
[[nodiscard]] bool CaptureBitmap(
    const std::filesystem::path& path,
    ScreenshotDimensions dimensions,
    Paint&& paint) {
    if (dimensions.logicalWidth <= 0 ||
        dimensions.logicalHeight <= 0 || dimensions.dpi < 96) {
        return false;
    }
    const int pixelWidth = PhysicalPixels(
        dimensions.logicalWidth, dimensions.dpi);
    const int pixelHeight = PhysicalPixels(
        dimensions.logicalHeight, dimensions.dpi);
    if (pixelWidth <= 0 || pixelHeight <= 0) {
        return false;
    }
    HDC screen = GetDC(nullptr);
    HDC memory =
        screen == nullptr ? nullptr : CreateCompatibleDC(screen);
    HBITMAP bitmap =
        memory == nullptr
        ? nullptr
        : CreateCompatibleBitmap(
              screen, pixelWidth, pixelHeight);
    HGDIOBJ previous =
        bitmap == nullptr ? nullptr : SelectObject(memory, bitmap);
    bool saved = false;
    if (previous != nullptr) {
        HeroIconGdiplusRuntime::EnsureStarted();
        const int savedDc = SaveDC(memory);
        const bool scaled = dimensions.dpi == 96 ||
            (savedDc != 0 &&
             SetMapMode(memory, MM_ANISOTROPIC) != 0 &&
             SetWindowExtEx(
                 memory, dimensions.logicalWidth,
                 dimensions.logicalHeight, nullptr) != 0 &&
             SetViewportExtEx(
                 memory, pixelWidth, pixelHeight, nullptr) != 0);
        if (scaled) {
            paint(memory);
        }
        if (savedDc != 0) {
            static_cast<void>(RestoreDC(memory, savedDc));
        }

        std::array<COLORREF, 16U> sampledColors{};
        std::size_t distinctColorCount = 0U;
        for (int y = pixelHeight / 8;
             y < pixelHeight &&
             distinctColorCount < sampledColors.size();
             y += std::max(1, pixelHeight / 8)) {
            for (int x = pixelWidth / 8;
                 x < pixelWidth &&
                 distinctColorCount < sampledColors.size();
                 x += std::max(1, pixelWidth / 8)) {
                const COLORREF color = GetPixel(memory, x, y);
                bool seen = color == CLR_INVALID;
                for (std::size_t index = 0U;
                     index < distinctColorCount && !seen; ++index) {
                    seen = sampledColors[index] == color;
                }
                if (!seen) {
                    sampledColors[distinctColorCount++] = color;
                }
            }
        }
        if (scaled && distinctColorCount >= 1U) {
            if (const auto encoder = EncoderClsid(L"image/bmp")) {
                Gdiplus::Bitmap image(bitmap, nullptr);
                const Gdiplus::Status resolution = image.SetResolution(
                    static_cast<float>(dimensions.dpi),
                    static_cast<float>(dimensions.dpi));
                saved = resolution == Gdiplus::Ok &&
                    image.GetWidth() ==
                        static_cast<UINT>(pixelWidth) &&
                    image.GetHeight() ==
                        static_cast<UINT>(pixelHeight) &&
                    image.Save(
                        path.wstring().c_str(), &*encoder, nullptr) ==
                        Gdiplus::Ok;
            }
        }
        SelectObject(memory, previous);
    }
    if (bitmap != nullptr) DeleteObject(bitmap);
    if (memory != nullptr) DeleteDC(memory);
    if (screen != nullptr) ReleaseDC(nullptr, screen);
    return saved;
}

[[nodiscard]] bool RectEquals(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top &&
        left.right == right.right && left.bottom == right.bottom;
}

[[nodiscard]] RECT WindowRectInHostClient(HWND window, HWND host) noexcept {
    RECT rect{};
    if (window == nullptr || host == nullptr || GetWindowRect(window, &rect) == 0) {
        return {};
    }
    POINT points[2]{ { rect.left, rect.top }, { rect.right, rect.bottom } };
    static_cast<void>(MapWindowPoints(nullptr, host, points, 2U));
    return { points[0].x, points[0].y, points[1].x, points[1].y };
}

[[nodiscard]] HWND FindViewportClipWindow(HWND host, const RECT& bounds) noexcept {
    constexpr wchar_t kViewportWindowClass[] = L"KBEditorSceneBgfxViewport";
    for (HWND child = FindWindowExW(host, nullptr, kViewportWindowClass, nullptr);
         child != nullptr;
         child = FindWindowExW(host, child, kViewportWindowClass, nullptr)) {
        if (IsWindowVisible(child) != 0 &&
            RectEquals(WindowRectInHostClient(child, host), bounds)) {
            return child;
        }
    }
    return nullptr;
}

// Overlay popups are WS_POPUP windows owned by an editor window; ownership
// shows up through GWLP_HWNDPARENT on the top-level enumeration.
[[nodiscard]] bool HasVisibleOwnedOverlay(HWND owner) noexcept {
    struct EnumContext {
        HWND owner;
        bool found;
    } context{ owner, false };
    EnumWindows(
        [](HWND window, LPARAM lparam) -> BOOL {
            auto* context = reinterpret_cast<EnumContext*>(lparam);
            if (reinterpret_cast<HWND>(GetWindowLongPtrW(window, GWLP_HWNDPARENT)) == context->owner &&
                IsWindowVisible(window) != 0) {
                context->found = true;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));
    return context.found;
}

// A torn-off panel's window, built exactly as the editor builds one, so the frame
// Windows keeps can be measured rather than guessed at.
LRESULT CALLBACK FloatingFrameProbeProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCALCSIZE) {
        return EditorFloatingWindowFrame::HandleNonClientCalcSize(window, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

[[nodiscard]] int ReservedFrameHeight(HINSTANCE instance, const wchar_t* className, WNDPROC windowProc) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = className;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return -1;
    }

    int reserved = -1;
    HWND window = CreateWindowExW(
        FloatingWindowFactory::ExtendedStyle, className, L"", FloatingWindowFactory::Style,
        0, 0, 900, 640, nullptr, nullptr, instance, nullptr);
    if (window != nullptr) {
        RECT frame{};
        RECT client{};
        if (GetWindowRect(window, &frame) != FALSE && GetClientRect(window, &client) != FALSE) {
            reserved = static_cast<int>((frame.bottom - frame.top) - (client.bottom - client.top));
        }
        DestroyWindow(window);
    }
    UnregisterClassW(className, instance);
    return reserved;
}

[[nodiscard]] HWND FindOwnedWindowByClass(
    HWND owner, const wchar_t* className) noexcept {
    for (HWND window = FindWindowExW(nullptr, nullptr, className, nullptr);
         window != nullptr;
         window = FindWindowExW(nullptr, window, className, nullptr)) {
        if (reinterpret_cast<HWND>(
                GetWindowLongPtrW(window, GWLP_HWNDPARENT)) == owner) {
            return window;
        }
    }
    return nullptr;
}

} // namespace

struct EditorHeadlessAutomation::Impl {
    explicit Impl(EditorSceneContext& sceneContext)
        : sceneContext(&sceneContext) {
        window = CreateWindowExW(
            0, L"STATIC", L"21kb headless render host",
            WS_POPUP | WS_CLIPCHILDREN,
            0, 0, 640, 360, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (window == nullptr) return;
        viewport.Configure(
            GetModuleHandleW(nullptr), window, &backendSettings);
        viewport.SetErrorReporter(
            [&sceneContext](std::string_view message) {
                sceneContext.Console().Error(
                    "Renderer", std::string{ message });
            });
    }

    ~Impl() {
        if (sceneContext != nullptr) {
            kb::input::InputHaptics::UnregisterBackend(
                sceneContext->Scene(), hapticsBackend);
        }
        hapticsBackend.StopAll();
        viewport.Shutdown();
        if (window != nullptr) {
            DestroyWindow(window);
        }
    }

    [[nodiscard]] bool RenderScene(
        EditorSceneContext& context,
        std::uint64_t viewportKey,
        bool editorOverlaysEnabled) {
        if (window == nullptr) return false;
        if (editorOverlaysEnabled && context.ViewportPreview(viewportKey).Is2D()) {
            viewport.BeginPaintLayout(window);
            const DockPanel panel{.id=static_cast<std::uint32_t>(viewportKey),.kind=DockPanelKind::Scene};
            auto settings = ScenePanelContentRenderer::BuildSettings(RECT{0,-34,640,360},panel,context,backendSettings);
            // Readbacks consume the offscreen scene target; composite UI into that target.
            settings.presentToHost = false;
            settings.postProcessEnabled = false;
            viewport.Present(window, RECT{0,0,640,360},context.Scene(),settings);
            viewport.EndPaintLayout();
            return std::string_view{viewport.ActiveBackendLabel()} != "Not initialized";
        }
        constexpr RECT bounds{ 0, 0, 640, 360 };
        EditorSceneBgfxViewport::PresentSettings settings{};
        settings.renderWidth = 640U;
        settings.renderHeight = 360U;
        context.SetUIAuthoringViewportSize(640.0F, 360.0F);
        settings.viewportKey = viewportKey;
        // Runtime captures read the offscreen scene target, including its UI composite.
        settings.presentToHost = editorOverlaysEnabled;
        settings.postProcessEnabled = editorOverlaysEnabled;
        kb::render::SceneRenderCamera camera{};
        const bx::Vec3 eye = editorOverlaysEnabled
            ? bx::Vec3{ 4.0F, 3.0F, 4.0F }
            : bx::Vec3{ 0.0F, 0.0F, 3.0F };
        bx::mtxLookAt(
            camera.view.data(),
            eye,
            bx::Vec3{ 0.0F, 0.0F, 0.0F });
        kb::render::SceneDepthPolicy::MakePerspective(
            camera.projection.data(), 60.0F,
            640.0F / 360.0F, 0.05F, 100.0F,
            kb::render::SceneDepthPolicy::HomogeneousDepth());
        settings.cameraOverride = camera;
        settings.sceneRevision = context.SceneRenderRevision();
        settings.sceneDirtyBaseRevision = settings.sceneRevision;
        settings.sceneFullSyncRequired = true;
        settings.editorSceneOverlaysEnabled = editorOverlaysEnabled;
        settings.selectionMaskEnabled = false;
        settings.selectionOutlineEnabled = false;
        settings.drawSafeArea = false;
        viewport.BeginPaintLayout(window);
        viewport.Present(
            window, bounds, context.Scene(), settings);
        viewport.EndPaintLayout();
        return std::string_view{ viewport.ActiveBackendLabel() } !=
            "Not initialized";
    }

    [[nodiscard]] bool Render(EditorSceneContext& context) {
        return RenderScene(context, 1U, false);
    }

    // Renders the scene viewport and, when an Animator Controller asset is
    // open, the Animator Editor preview in a single paint. The preview mirrors
    // AnimatorEditorPanelRenderer: animation editor previews share the
    // host-surface mechanism keyed by panel.id, so lifecycle checks must cover
    // both surfaces of the host window.
    [[nodiscard]] bool RenderAll(EditorSceneContext& context, std::uint64_t animatorPreviewKey) {
        if (window == nullptr) return false;
        constexpr RECT bounds{ 0, 0, 640, 360 };
        EditorSceneBgfxViewport::PresentSettings settings{};
        settings.renderWidth = 640U;
        settings.renderHeight = 360U;
        settings.viewportKey = 1U;
        kb::render::SceneRenderCamera camera{};
        bx::mtxLookAt(
            camera.view.data(),
            bx::Vec3{ 0.0F, 0.0F, 3.0F },
            bx::Vec3{ 0.0F, 0.0F, 0.0F });
        kb::render::SceneDepthPolicy::MakePerspective(
            camera.projection.data(), 60.0F,
            640.0F / 360.0F, 0.05F, 100.0F,
            kb::render::SceneDepthPolicy::HomogeneousDepth());
        settings.cameraOverride = camera;
        settings.sceneRevision = context.SceneRenderRevision();
        settings.sceneDirtyBaseRevision = settings.sceneRevision;
        settings.sceneFullSyncRequired = true;
        settings.editorSceneOverlaysEnabled = false;
        settings.selectionMaskEnabled = false;
        settings.selectionOutlineEnabled = false;
        settings.drawSafeArea = false;
        viewport.BeginPaintLayout(window);
        viewport.Present(
            window, bounds, context.Scene(), settings);
        if (animatorPreviewKey != 0U && context.AnimatorEditorPreviewScene() != nullptr) {
            constexpr RECT previewBounds{ 320, 180, 640, 360 };
            const std::uint64_t revision = context.AnimatorEditorPreviewRevision();
            EditorSceneBgfxViewport::PresentSettings previewSettings{};
            previewSettings.viewportKey = animatorPreviewKey;
            previewSettings.editorSceneOverlaysEnabled = false;
            previewSettings.sceneRevision = revision;
            previewSettings.sceneDirtyBaseRevision = revision;
            previewSettings.sceneFullSyncRequired = false;
            previewSettings.msaaSamples = backendSettings.MsaaSamples();
            previewSettings.shadowPassEnabled = backendSettings.ShadowsEnabled();
            previewSettings.postProcessEnabled = true;
            previewSettings.selectionMaskEnabled = false;
            previewSettings.selectionOutlineEnabled = false;
            previewSettings.gpuDrivenRuntimeDispatchEnabled = backendSettings.GpuDrivenEnabled();
            viewport.Present(
                window, previewBounds, *context.AnimatorEditorPreviewScene(), previewSettings);
        }
        viewport.EndPaintLayout();
        return std::string_view{ viewport.ActiveBackendLabel() } !=
            "Not initialized";
    }

    HWND window = nullptr;
    HWND scriptEditorWindow = nullptr;
    EditorSceneContext* sceneContext = nullptr;
    kb::input::Win32XInputHapticsBackend hapticsBackend;
    EditorRenderBackendSettings backendSettings;
    EditorSceneBgfxViewport viewport;
};

EditorHeadlessAutomation::EditorHeadlessAutomation(
    EditorSceneContext& context,
    std::filesystem::path artifactRoot)
    : context_(context)
    , artifactRoot_(std::filesystem::absolute(
          std::move(artifactRoot)))
    , tracePath_(artifactRoot_ / "trace.jsonl")
    , impl_(std::make_unique<Impl>(context)) {
    std::error_code error;
    std::filesystem::create_directories(
        artifactRoot_ / "screenshots", error);
    std::filesystem::create_directories(
        artifactRoot_ / "snapshots", error);
}

EditorHeadlessAutomation::~EditorHeadlessAutomation() = default;

bool EditorHeadlessAutomation::AddComponent(
    std::string_view componentId) {
    const InspectorComponentTile* tile = InspectorComponentCatalog::Find(componentId);
    if (tile == nullptr) {
        const std::span<const InspectorComponentTile> tiles = InspectorComponentCatalog::Tiles();
        const auto byLabel = std::ranges::find_if(tiles, [componentId](const InspectorComponentTile& candidate) {
            return candidate.label == componentId;
        });
        tile = byLabel == tiles.end() ? nullptr : &*byLabel;
    }
    if (tile == nullptr ||
        !context_.Scene().Entities().IsAlive(
            context_.SelectedEntity())) {
        Trace("add_component", false, componentId);
        return false;
    }
    const auto add = FindInspectorHit(
        context_, InspectorSectionId::AddComponent,
        InspectorPropertyId::AddComponentButton);
    if (!add.has_value()) {
        Trace("add_component", false, "button-not-found");
        return false;
    }
    const POINT addPoint = Center(add->rect);
    if (!InspectorPanelInteraction::HandlePointerDown(
            context_, *add, addPoint.x, addPoint.y)) {
        Trace("add_component", false, "button-not-routed");
        return false;
    }
    const auto overlay =
        InspectorPanelRenderer::AddComponentOverlayRect(
            kInspectorContent, context_);
    if (!overlay.has_value()) {
        Trace("add_component", false, "overlay-not-open");
        return false;
    }
    InspectorPanelRenderer::Hit search{};
    bool foundSearch = false;
    for (int y = overlay->top; y < overlay->bottom && !foundSearch;
         ++y) {
        search =
            InspectorPanelRenderer::HitTestAddComponentOverlay(
                *overlay, context_, (overlay->left + overlay->right) / 2,
                y);
        foundSearch =
            search.property ==
            InspectorPropertyId::AddComponentSearch;
    }
    if (!foundSearch) {
        Trace("add_component", false, "search-not-found");
        return false;
    }
    const POINT searchPoint = Center(search.rect);
    static_cast<void>(
        InspectorPanelInteraction::HandlePointerDown(
            context_, search, searchPoint.x, searchPoint.y));
    const std::string_view searchText = tile->id;
    const int wideLength = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, searchText.data(),
        static_cast<int>(searchText.size()), nullptr, 0);
    if (wideLength <= 0) {
        Trace("add_component", false, "invalid-utf8-label");
        return false;
    }
    std::wstring wideLabel(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, searchText.data(),
            static_cast<int>(searchText.size()), wideLabel.data(), wideLength) != wideLength) {
        Trace("add_component", false, "utf8-conversion-failed");
        return false;
    }
    for (const wchar_t character : wideLabel) {
        static_cast<void>(InspectorPanelInteraction::HandleChar(
            context_, character));
    }
    const auto filteredOverlay =
        InspectorPanelRenderer::AddComponentOverlayRect(
            kInspectorContent, context_);
    if (!filteredOverlay.has_value()) {
        Trace("add_component", false, "overlay-closed");
        return false;
    }
    InspectorPanelRenderer::Hit option{};
    bool foundOption = false;
    for (int y = filteredOverlay->top;
         y < filteredOverlay->bottom && !foundOption; ++y) {
        option =
            InspectorPanelRenderer::HitTestAddComponentOverlay(
                *filteredOverlay, context_,
                (filteredOverlay->left + filteredOverlay->right) / 2,
                y);
        foundOption =
            option.property ==
                InspectorPropertyId::AddComponentOption &&
            option.index >= 0;
    }
    if (!foundOption) {
        Trace("add_component", false, "result-not-found");
        return false;
    }
    const POINT optionPoint = Center(option.rect);
    const bool routed =
        InspectorPanelInteraction::HandlePointerDown(
            context_, option, optionPoint.x, optionPoint.y);
    Trace("add_component", routed, tile->label);
    return routed;
}

bool EditorHeadlessAutomation::SetPhysicsFloat(
    PhysicsComponentKind component, int fieldIndex, float value) {
    if (InspectorPhysicsModel::KindOf(component, fieldIndex) !=
        PhysicsFieldKind::Float) {
        Trace("set_physics_float", false, "field-not-float");
        return false;
    }
    const auto hit = FindInspectorHit(
        context_, PhysicsSection(component),
        PhysicsProperty(component), fieldIndex);
    if (!hit.has_value()) {
        Trace("set_physics_float", false, "field-not-found");
        return false;
    }
    const POINT point = Center(hit->rect);
    if (!InspectorPanelInteraction::HandlePointerDown(
            context_, *hit, point.x, point.y)) {
        Trace("set_physics_float", false, "pointer-down-not-routed");
        return false;
    }
    if (context_.Inspector().IsDraggingFloat()) {
        static_cast<void>(
            InspectorPanelInteraction::HandlePointerUp(context_));
    }
    if (!context_.Inspector().IsTextEditing()) {
        Trace("set_physics_float", false, "field-not-editing");
        return false;
    }
    while (!context_.Inspector().EditBuffer().empty()) {
        static_cast<void>(
            InspectorPanelInteraction::HandleKeyDown(
                nullptr, context_, VK_BACK));
    }
    std::ostringstream text;
    text << std::setprecision(9) << value;
    for (const char character : text.str()) {
        static_cast<void>(InspectorPanelInteraction::HandleChar(
            context_, static_cast<wchar_t>(character)));
    }
    static_cast<void>(InspectorPanelInteraction::HandleKeyDown(
        nullptr, context_, VK_RETURN));
    const bool applied = !context_.Inspector().IsTextEditing();
    Trace("set_physics_float", applied, text.str());
    return applied;
}

bool EditorHeadlessAutomation::SetUIComponentProperty(
    kb::scene::UIComponentType component,
    std::string_view property,
    const kb::scene::UIComponentPropertyValue& value) {
    const kb::scene::SceneEntity entity = context_.SelectedEntity();
    const kb::scene::UIComponentPropertyDescriptor* descriptor =
        kb::scene::FindUIComponentProperty(component, property);
    if (!context_.Scene().Entities().IsAlive(entity) ||
        descriptor == nullptr || !descriptor->writable) {
        Trace("set_ui_component_property", false, "property-not-editable");
        return false;
    }

    const std::vector<InspectorUIPropertyRow> rows =
        InspectorUIComponentModel::Properties(context_.Scene(), entity, component);
    const auto row = std::ranges::find_if(rows,
        [property](const InspectorUIPropertyRow& candidate) {
            return candidate.name == property;
        });
    if (row == rows.end()) {
        Trace("set_ui_component_property", false, "property-not-visible");
        return false;
    }
    const int rowIndex = static_cast<int>(std::distance(rows.begin(), row));
    const bool openedAdvanced = component == kb::scene::UIComponentType::RectTransform &&
        !context_.Inspector().IsDisclosureExpanded(InspectorDisclosureId::UIRectAdvanced);
    if (openedAdvanced) context_.Inspector().ToggleDisclosure(InspectorDisclosureId::UIRectAdvanced);

    kb::scene::UIComponentSet current = kb::scene::CaptureSceneUIComponents(
        context_.Scene().Components().UI(), entity);
    kb::scene::UIComponentPropertyValue currentValue;
    if (!kb::scene::ReadUIComponentProperty(
            current, component, property, currentValue)) {
        Trace("set_ui_component_property", false, "property-not-readable");
        return false;
    }

    const auto& group = rows[static_cast<std::size_t>(row->groupStart)];
    if (group.color || !row->choices.empty()) {
        const auto kind = group.color ? InspectorHitKind::ColorField : InspectorHitKind::ChoiceField;
        const int index = group.color ? row->groupStart : rowIndex;
        if (!FindInspectorHit(context_, InspectorUIComponentModel::Section(component),
                InspectorUIComponentModel::Property(component), index, kind)) {
            Trace("set_ui_component_property", false, "compact-control-not-found");
            return false;
        }
        if (currentValue != value) {
            const auto revision = context_.SceneRenderRevision();
            bool changed = false;
            if (group.color) {
                auto color = group.rgba;
                color[static_cast<std::size_t>(rowIndex - row->groupStart)] = std::get<float>(value);
                changed = context_.SetUIColor(entity, component, group.name, color);
            } else {
                changed = context_.SetUIComponentProperty(entity, component, property, value);
            }
            if (!changed || context_.SceneRenderRevision() == revision) {
                Trace("set_ui_component_property", false, "compact-edit-not-applied-or-not-dirty");
                return false;
            }
        }
        current = kb::scene::CaptureSceneUIComponents(context_.Scene().Components().UI(), entity);
        kb::scene::UIComponentPropertyValue applied;
        const bool succeeded = kb::scene::ReadUIComponentProperty(current, component, property, applied) && applied == value;
        Trace("set_ui_component_property", succeeded, property);
        return succeeded;
    }

    POINT point{};
    const auto hit = FindInspectorHit(
        context_, InspectorUIComponentModel::Section(component),
        InspectorUIComponentModel::Property(component), rowIndex,
        descriptor->type == kb::scene::UIComponentPropertyType::Bool
            ? InspectorHitKind::BoolField
            : InspectorHitKind::TextField, &point);
    if (!hit.has_value()) {
        Trace("set_ui_component_property", false, "field-not-found");
        return false;
    }
    if (currentValue == value) {
        if (openedAdvanced) context_.Inspector().ToggleDisclosure(InspectorDisclosureId::UIRectAdvanced);
        Trace("set_ui_component_property", true, "already-set");
        return true;
    }
    impl_->viewport.ClearPresentRequest();
    if (!EditorInspectorPointerController{context_}.HandlePointerDown(
            kInspectorContent, point.x, point.y, impl_->viewport)) {
        Trace("set_ui_component_property", false, "pointer-down-not-routed");
        return false;
    }
    if (descriptor->type == kb::scene::UIComponentPropertyType::Bool && !impl_->viewport.PresentRequested()) {
        Trace("ui_property_refresh", false, "checkbox-edit-did-not-request-render");
        return false;
    }

    if (descriptor->type != kb::scene::UIComponentPropertyType::Bool) {
        if (!context_.Inspector().IsTextEditing()) {
            Trace("set_ui_component_property", false, "field-not-editing");
            return false;
        }
        while (!context_.Inspector().EditBuffer().empty()) {
            static_cast<void>(InspectorPanelInteraction::HandleKeyDown(
                nullptr, context_, VK_BACK));
        }
        const std::string text = std::visit([](const auto& typed) {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, bool>) {
                return std::string{ typed ? "true" : "false" };
            } else if constexpr (std::is_same_v<T, float>) {
                std::ostringstream output;
                output << std::setprecision(9) << typed;
                return output.str();
            } else if constexpr (std::is_same_v<T, std::string>) {
                return typed;
            } else {
                return std::to_string(typed);
            }
        }, value);
        const int wideLength = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), nullptr, 0);
        if (wideLength < 0 || (wideLength == 0 && !text.empty())) {
            context_.Inspector().EndTextEdit();
            Trace("set_ui_component_property", false, "invalid-utf8-value");
            return false;
        }
        std::wstring wideText(static_cast<std::size_t>(wideLength), L'\0');
        if (wideLength > 0 && MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                static_cast<int>(text.size()), wideText.data(), wideLength) != wideLength) {
            context_.Inspector().EndTextEdit();
            Trace("set_ui_component_property", false, "utf8-conversion-failed");
            return false;
        }
        for (const wchar_t character : wideText) {
            static_cast<void>(InspectorPanelInteraction::HandleChar(
                context_, character));
        }
        static_cast<void>(InspectorPanelInteraction::HandleKeyDown(
            nullptr, context_, VK_RETURN));
    }

    current = kb::scene::CaptureSceneUIComponents(
        context_.Scene().Components().UI(), entity);
    kb::scene::UIComponentPropertyValue applied;
    const bool succeeded = kb::scene::ReadUIComponentProperty(
            current, component, property, applied) &&
        applied == value;
    if (openedAdvanced) context_.Inspector().ToggleDisclosure(InspectorDisclosureId::UIRectAdvanced);
    Trace("set_ui_component_property", succeeded, property);
    return succeeded;
}

bool EditorHeadlessAutomation::VerifyUI2DEditing() {
    constexpr float width = 640, height = 360;
    auto entity = context_.SelectedEntity();
    const auto entityName = context_.Scene().Entities().Name(entity);
    const auto cameraBefore = context_.ViewportCamera(1U).Position();
    const auto frameElement = [&]() -> std::optional<kb::scene::SceneUIFrameElement> {
        if (!context_.Scene().Entities().IsAlive(entity)) {
            for (const auto& row : context_.HierarchyRows())
                if (row.name == entityName) {
                    entity = row.entity;
                    context_.SelectEntity(entity);
                    break;
                }
        }
        kb::scene::SceneUIFrame frame;
        if (!kb::scene::SceneUIQueries{context_.Scene()}.BuildFrame(width, height, frame))
            return std::nullopt;
        const auto found = std::ranges::find(frame.elements, entity, &kb::scene::SceneUIFrameElement::entity);
        return found == frame.elements.end() ? std::nullopt : std::optional{*found};
    };
    const auto original = frameElement();
    if (!original)
        return false;
    const EditorResolvedPanelContent panel{.content = {0, 0, 900, 394}, .panelId = 1U};
    const auto toolbar = SceneViewportToolbarRenderer::Resolve(panel.content, context_.ViewportPreview(1U));
    const auto button = Center(toolbar.twoDButton);
    impl_->viewport.ClearPresentRequest();
    EditorSceneViewportToolbarPointerController pointer{context_, impl_->viewport};
    if (!pointer.HandlePointerDown(panel, button.x, button.y) || !context_.ViewportPreview(1U).Is2D() ||
        !impl_->viewport.PresentRequested() || toolbar.twoDButton.left <= toolbar.rotationSnapButton.right) {
        Trace("ui_2d", false, "toolbar-toggle-or-refresh-failed");
        return false;
    }
    if (!CaptureBitmap(artifactRoot_ / "screenshots" / "ui-2d-toolbar.bmp", ScreenshotDimensions{700, 40, 96},
                       [&](HDC dc) {
                           SceneViewportToolbarRenderer::Paint(dc, RECT{0, 0, 700, 40}, MakeEditorDarkTheme(),
                                                               context_.ViewportPreview(1U));
                       }))
        return false;
    const auto overlays = EditorUIRectInteraction::Overlays(context_, width, height);
    if (overlays.size() < 10U) {
        Trace("ui_2d", false, "canvas-outline-or-eight-handles-missing");
        return false;
    }
    constexpr std::array<kb::math::Vec2, 8> positions{
        {{0, 0}, {0.5F, 0}, {1, 0}, {1, 0.5F}, {1, 1}, {0.5F, 1}, {0, 1}, {0, 0.5F}}};
    const auto point = [](const kb::scene::SceneUIFrameElement& e, kb::math::Vec2 uv) {
        return kb::math::Vec2{
            e.corners[0].x + (e.corners[1].x - e.corners[0].x) * uv.x + (e.corners[3].x - e.corners[0].x) * uv.y,
            e.corners[0].y + (e.corners[1].y - e.corners[0].y) * uv.x + (e.corners[3].y - e.corners[0].y) * uv.y};
    };
    const auto close = [](kb::math::Vec2 a, kb::math::Vec2 b) {
        return std::abs(a.x - b.x) < 0.1F && std::abs(a.y - b.y) < 0.1F;
    };
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto p = point(*original, positions[i]);
        if (!EditorUIRectInteraction::Begin(context_, width, height, p.x, p.y) || !context_.UIRectDrag() ||
            context_.UIRectDrag()->handle != static_cast<int>(i) ||
            !EditorUIRectInteraction::Update(context_, p.x + 12, p.y + 8) || !EditorUIRectInteraction::End(context_)) {
            Trace("ui_2d", false, "resize-routing-failed:" + std::to_string(i));
            return false;
        }
        const auto after = frameElement();
        const kb::math::Vec2 opposite{1 - positions[i].x, 1 - positions[i].y};
        if (!after || !close(point(*original, opposite), point(*after, opposite)) ||
            (std::abs(after->rect.width - original->rect.width) < 0.1F &&
             std::abs(after->rect.height - original->rect.height) < 0.1F)) {
            Trace("ui_2d", false, "resize-geometry-failed:" + std::to_string(i));
            return false;
        }
        if (!context_.UndoSceneCommand())
            return false;
        const auto restored = frameElement();
        if (!restored || !close(restored->corners[0], original->corners[0]) ||
            !close(restored->corners[2], original->corners[2])) {
            Trace("ui_2d", false, "resize-undo-failed");
            return false;
        }
        Trace("ui_2d_handle", true, std::to_string(i));
    }
    const auto parent = frameElement()->canvas;
    if (!context_.SetUIComponentProperty(parent, kb::scene::UIComponentType::RectTransform, "rotationDegrees", 15.0F) ||
        !context_.SetUIComponentProperty(parent, kb::scene::UIComponentType::RectTransform, "scale.x", 1.2F))
        return false;
    if (!context_.SetUIComponentProperty(entity, kb::scene::UIComponentType::RectTransform, "rotationDegrees", 45.0F) ||
        !context_.SetUIComponentProperty(entity, kb::scene::UIComponentType::RectTransform, "scale.x", 1.5F))
        return false;
    const auto rotated = frameElement();
    if (!rotated)
        return false;
    const auto corner = rotated->corners[2];
    if (!EditorUIRectInteraction::Begin(context_, width, height, corner.x, corner.y) ||
        !EditorUIRectInteraction::Update(context_, corner.x + 12, corner.y + 8) ||
        !EditorUIRectInteraction::End(context_))
        return false;
    const auto rotatedAfter = frameElement();
    if (!rotatedAfter || !close(rotated->corners[0], rotatedAfter->corners[0])) {
        Trace("ui_2d", false, "rotated-resize-moved-opposite-corner");
        return false;
    }
    for (int undo = 0; undo < 5; ++undo)
        if (!context_.UndoSceneCommand())
            return false;
    if (!frameElement())
        return false;
    const auto resizeStart = original->corners[2];
    if (!EditorUIRectInteraction::Begin(context_, width, height, resizeStart.x, resizeStart.y) ||
        !EditorUIRectInteraction::Update(context_, resizeStart.x + 10, resizeStart.y + 4, true, true))
        return false;
    const auto proportional = frameElement();
    if (!proportional || !close(point(*original, {0.5F, 0.5F}), point(*proportional, {0.5F, 0.5F})) ||
        std::abs(proportional->rect.width / proportional->rect.height - original->rect.width / original->rect.height) >
            0.01F) {
        std::ostringstream detail;
        detail << "centered-proportional-resize-failed expected=" << original->rect.width << ','
               << original->rect.height;
        if (proportional)
            detail << " actual=" << proportional->rect.width << ',' << proportional->rect.height
                   << " center=" << point(*proportional, {0.5F, 0.5F}).x << ',' << point(*proportional, {0.5F, 0.5F}).y;
        Trace("ui_2d", false, detail.str());
        return false;
    }
    if (!EditorUIRectInteraction::End(context_, true) || !frameElement())
        return false;
    context_.ClearHierarchySelection();
    const auto center = point(*original, {0.5F, 0.5F});
    if (!EditorUIRectInteraction::Begin(context_, width, height, center.x, center.y) ||
        context_.SelectedEntity() != entity || !EditorUIRectInteraction::Update(context_, center.x + 20, center.y + 15))
        return false;
    const auto moved = frameElement();
    if (!moved || !close(moved->corners[0], {original->corners[0].x + 20, original->corners[0].y + 15})) {
        Trace("ui_2d", false, "move-geometry-failed");
        return false;
    }
    if (!EditorUIRectInteraction::End(context_, true))
        return false;
    const auto restored = frameElement();
    if (!restored || !close(restored->corners[0], original->corners[0]))
        return false;
    if (!VerifySceneRenderTargetAfterSecondary("ui-2d-handles"))
        return false;
    auto& preview = context_.ViewportPreview(1U);
    preview.ZoomUI(2, {320, 180});
    const float zoom = preview.UIZoom();
    const auto pan = preview.UIPan();
    if (zoom <= 1 || !close({320 * zoom + pan.x, 180 * zoom + pan.y}, {320, 180}))
        return false;
    if (!VerifySceneRenderTargetAfterSecondary("ui-2d-zoom"))
        return false;
    preview.BeginUIPan(0, 0);
    preview.UpdateUIPan(40, 20);
    if (!close(preview.UIPan(), {pan.x + 40, pan.y + 20}))
        return false;
    preview.UpdateUIPan(0, 0);
    preview.ZoomUI(-2, {320, 180});
    preview.ZoomUI(std::log(0.5F) / std::log(1.15F), {320,180});
    const std::array<kb::math::Vec2, 12> outside{{
        {-20,180}, {-60,180}, {660,180}, {700,180}, {320,-20}, {320,-60}, {320,380}, {320,420},
        {-300,180}, {940,180}, {320,-160}, {320,520}}};
    for (std::size_t index = 0; index < outside.size(); ++index) {
        const auto current = frameElement();
        if (!current) return false;
        const auto probeCenter = point(*current, {0.5F,0.5F});
        context_.SelectEntity({});
        if (!EditorUIRectInteraction::Begin(context_, width, height, probeCenter.x, probeCenter.y, preview.UIZoom()) ||
            !EditorUIRectInteraction::Update(context_, outside[index].x, outside[index].y) ||
            !EditorUIRectInteraction::End(context_)) return false;
        const std::string checkpoint = "ui-outside-canvas-" + std::to_string(index);
        if (!VerifySceneRenderTargetAfterSecondary(checkpoint)) return false;
        Gdiplus::Bitmap capture((artifactRoot_/"screenshots"/(checkpoint+".png")).wstring().c_str());
        const auto offset = preview.UIPan();
        const int x = static_cast<int>(std::lround(outside[index].x * preview.UIZoom() + offset.x));
        const int y = static_cast<int>(std::lround(outside[index].y * preview.UIZoom() + offset.y));
        Gdiplus::Color pixel;
        if (capture.GetPixel(x, y, &pixel) != Gdiplus::Ok || pixel.GetBlue() < 15) {
            Trace("ui_view_clip", false, checkpoint + ": visible object was clipped");
            return false;
        }
        if (index == 1) {
            const auto canvas = frameElement()->canvas;
            if (!context_.AddComponentToEntity(canvas, "kb21.ui.mask") ||
                !VerifySceneRenderTargetAfterSecondary("ui-outside-canvas-masked")) return false;
            Gdiplus::Bitmap masked((artifactRoot_/"screenshots"/"ui-outside-canvas-masked.png").wstring().c_str());
            Gdiplus::Color maskedPixel;
            if (masked.GetPixel(x, y, &maskedPixel) != Gdiplus::Ok || maskedPixel.GetBlue() >= 15) {
                Trace("ui_view_clip", false, "authored mask stopped clipping");
                return false;
            }
            if (!context_.UndoSceneCommand() || !frameElement()) return false;
            Trace("ui_view_clip", true, "authored mask preserved");
        }
        context_.SelectEntity({});
        if (!EditorUIRectInteraction::Begin(context_, width, height, outside[index].x, outside[index].y, preview.UIZoom()) ||
            context_.SelectedEntity() != entity) {
            Trace("ui_view_clip", false, checkpoint + ": object could not be selected");
            return false;
        }
        static_cast<void>(EditorUIRectInteraction::End(context_, true));
        if (!context_.UndoSceneCommand() || !frameElement()) return false;
        Trace("ui_view_clip", true, checkpoint + ": visible and selectable");
    }
    preview.ZoomUI(std::log(2.0F) / std::log(1.15F), {320,180});
    for (const int steps : {-1,-2,-4,-10,-20}) {
        preview.ZoomUI(static_cast<float>(steps),{320,180});
        preview.BeginUIPan(0,0);
        preview.UpdateUIPan(0.37F,0.61F);
        const auto offset=preview.UIPan();
        const float scale=preview.UIZoom();
        const std::string checkpoint="canvas-zoom-out-"+std::to_string(-steps);
        if (!VerifySceneRenderTargetAfterSecondary(checkpoint)) return false;
        Gdiplus::Bitmap image((artifactRoot_/"screenshots"/(checkpoint+".png")).wstring().c_str());
        const std::array<float,4> edges{offset.x,offset.y,offset.x+width*scale,offset.y+height*scale};
        for (int edge=0;edge<4;++edge) {
            const bool vertical=edge%2==0;
            const int start=static_cast<int>(std::ceil(vertical ? edges[1] : edges[0]))+3;
            const int stop=static_cast<int>(std::floor(vertical ? edges[3] : edges[2]))-3;
            int missing=0;
            for (int along=start;along<stop;++along) {
                bool visible=false;
                for (int across=static_cast<int>(std::floor(edges[edge]))-2;across<=static_cast<int>(std::ceil(edges[edge]))+1;++across) {
                    const int x=vertical ? across : along, y=vertical ? along : across;
                    if (x<0 || y<0 || x>=static_cast<int>(image.GetWidth()) || y>=static_cast<int>(image.GetHeight())) continue;
                    Gdiplus::Color color{};
                    if (image.GetPixel(static_cast<UINT>(x),static_cast<UINT>(y),&color)==Gdiplus::Ok && color.GetBlue()>12) visible=true;
                }
                if (!visible) ++missing;
            }
            if (missing!=0) {
                Trace("ui_canvas_edges",false,checkpoint+" edge="+std::to_string(edge)+" missing-pixels="+std::to_string(missing));
                return false;
            }
        }
        Trace("ui_canvas_edges",true,checkpoint+": four complete edges");
        preview.UpdateUIPan(0,0);
        preview.ZoomUI(std::log(1.0F/scale)/std::log(1.15F),{320,180});
    }
    if (!pointer.HandlePointerDown(panel, button.x, button.y) || preview.Is2D())
        return false;
    const auto cameraAfter = context_.ViewportCamera(1U).Position();
    const bool sameCamera =
        cameraBefore.x == cameraAfter.x && cameraBefore.y == cameraAfter.y && cameraBefore.z == cameraAfter.z;
    Trace("ui_2d", sameCamera, "eight-handles-move-cancel-undo-zoom-camera-restored");
    return sameCamera;
}

bool EditorHeadlessAutomation::SelectUIAnchorPreset(int preset) {
    if (preset < 0 || preset >= 16) return false;
    EditorInspectorPointerController pointer{context_};
    impl_->viewport.ClearPresentRequest();
    if (!context_.Inspector().IsDisclosureExpanded(InspectorDisclosureId::UIAnchorPresets)) {
        const auto hit = FindInspectorHit(context_, InspectorSectionId::UIRectTransform, InspectorPropertyId::UIAnchorPresets);
        if (!hit) return false;
        const auto point = Center(hit->rect);
        if (!pointer.HandlePointerDown(kInspectorContent, point.x, point.y, impl_->viewport)) return false;
        if (impl_->viewport.PresentRequested()) {
            Trace("ui_anchor_refresh", false, "opening-selector-requested-render");
            return false;
        }
    }
    const auto hit = FindInspectorHit(context_, InspectorSectionId::UIRectTransform, InspectorPropertyId::UIAnchorPreset, preset);
    if (!hit) return false;
    const auto point = Center(hit->rect);
    if (!pointer.HandlePointerDown(kInspectorContent, point.x, point.y, impl_->viewport)) return false;
    if (!impl_->viewport.PresentRequested()) {
        Trace("ui_anchor_refresh", false, "anchor-edit-did-not-request-render");
        return false;
    }
    if (!impl_->RenderScene(context_, 1U, true)) return false;
    impl_->viewport.ClearPresentRequest();
    Trace("ui_anchor_refresh", true, "edit-mode-frame-presented-without-play");
    const auto* rect = context_.Scene().Components().UI().TryGet<kb::scene::UIRectTransform>(context_.SelectedEntity());
    const bool matched = rect != nullptr && InspectorUIComponentModel::AnchorPreset(*rect) == preset;
    Trace("ui_anchor_preset", matched, std::to_string(preset));
    return matched;
}

bool EditorHeadlessAutomation::SetUIRectLayoutField(int field, float value) {
    if (field < 0 || field >= 4 || !std::isfinite(value)) return false;
    const auto hit = FindInspectorHit(context_, InspectorSectionId::UIRectTransform, InspectorPropertyId::UIRectLayoutField, field);
    if (!hit) return false;
    const auto point = Center(hit->rect);
    if (!InspectorPanelInteraction::HandlePointerDown(context_, *hit, point.x, point.y) || !context_.Inspector().IsTextEditing()) return false;
    while (!context_.Inspector().EditBuffer().empty()) {
        static_cast<void>(InspectorPanelInteraction::HandleKeyDown(nullptr, context_, VK_BACK));
    }
    std::ostringstream text;
    text << std::setprecision(9) << value;
    for (const char character : text.str()) {
        static_cast<void>(InspectorPanelInteraction::HandleChar(context_, static_cast<wchar_t>(character)));
    }
    static_cast<void>(InspectorPanelInteraction::HandleKeyDown(nullptr, context_, VK_RETURN));
    const auto* rect = context_.Scene().Components().UI().TryGet<kb::scene::UIRectTransform>(context_.SelectedEntity());
    if (rect == nullptr || context_.Inspector().IsTextEditing()) return false;
    const auto fields = InspectorUIComponentModel::RectLayoutFields(*rect);
    const auto parsed = InspectorUIComponentModel::Parse(kb::scene::UIComponentPropertyType::Float, fields[static_cast<std::size_t>(field)].value);
    const bool matched = parsed && std::abs(std::get<float>(*parsed) - value) <= 0.001F;
    Trace("ui_rect_layout", matched, text.str());
    return matched;
}

bool EditorHeadlessAutomation::VerifyUICreationMenu() {
    const auto fail = [&](std::string_view reason) {
        Trace("ui_creation_menu", false, reason);
        return false;
    };
    const auto findSubmenu = [](HMENU menu, std::string_view label) -> HMENU {
        for (int index = 0; index < GetMenuItemCount(menu); ++index) {
            char text[128]{};
            GetMenuStringA(menu, static_cast<UINT>(index), text, sizeof(text), MF_BYPOSITION);
            if (label == text) return GetSubMenu(menu, index);
        }
        return nullptr;
    };
    HMENU menu = EditorRightButtonDownRouter::CreateHierarchyMenu();
    if (!menu) return fail("menu-allocation-failed");
    const auto create = findSubmenu(menu, "Create");
    const auto widgets = create ? findSubmenu(create, "User Widget") : nullptr;
    std::vector<std::pair<std::string, UINT>> commands;
    const auto collect = [&](auto&& self, HMENU current) -> void {
        for (int index = 0; index < GetMenuItemCount(current); ++index) {
            if (const auto child = GetSubMenu(current, index)) self(self, child);
            else {
                char text[128]{};
                GetMenuStringA(current, static_cast<UINT>(index), text, sizeof(text), MF_BYPOSITION);
                if (text[0] != '\0') commands.emplace_back(text, GetMenuItemID(current, index));
            }
        }
    };
    if (widgets) collect(collect, widgets);
    DestroyMenu(menu);
    if (!widgets || commands.size() != kb::scene::UIComponentCatalog().size())
        return fail("missing-user-widget-menu-entry");
    const auto baseline = context_.HierarchyRows().size();
    for (const auto& descriptor : kb::scene::UIComponentCatalog()) {
        const auto command = std::ranges::find_if(commands, [&](const auto& item) { return item.first == descriptor.displayName; });
        if (command == commands.end()) return fail("missing-component-command");
        const auto revision = context_.SceneRenderRevision();
        if (!EditorRightButtonDownRouter::ExecuteHierarchyMenuCommand(command->second, context_))
            return fail(std::string{descriptor.displayName} + ": creation failed");
        auto entity = context_.SelectedEntity();
        const auto components = InspectorUIComponentModel::Components(context_.Scene(), entity);
        if (std::ranges::find(components, descriptor.type) == components.end() ||
            context_.Scene().Entities().Name(entity) != descriptor.displayName ||
            context_.SceneRenderRevision() == revision) return fail("component-name-or-refresh-missing");
        for (const auto& preset : kb::scene::UIComponentPresetCatalog()) {
            if (preset.name != descriptor.displayName) continue;
            for (const auto dependency : preset.components)
                if (std::ranges::find(components, dependency) == components.end()) return fail("missing-preset-dependency");
        }
        auto root = entity;
        while (context_.Scene().Hierarchy().Parent(root).IsValid()) root = context_.Scene().Hierarchy().Parent(root);
        if (!context_.Scene().Components().UI().Has<kb::scene::UICanvas>(root)) return fail("missing-canvas");
        kb::scene::SceneUIFrame frame;
        if (!kb::scene::SceneUIQueries{context_.Scene()}.BuildFrame(1280.0F, 720.0F, frame) ||
            std::ranges::none_of(frame.elements, [entity](const auto& element) {
                return element.entity == entity && element.rect.width > 0.0F && element.rect.height > 0.0F;
            })) return fail("created-widget-absent-from-frame");
        if (const auto* text = context_.Scene().Components().UI().TryGet<kb::scene::UIText>(entity);
            text && text->fontAssetId == 0) return fail("missing-font-dependency");
        const auto createdCount = context_.HierarchyRows().size();
        if (!context_.UndoSceneCommand() || context_.HierarchyRows().size() != baseline ||
            !context_.RedoSceneCommand() || context_.HierarchyRows().size() != createdCount ||
            !context_.UndoSceneCommand() || context_.HierarchyRows().size() != baseline)
            return fail("creation-not-one-undo-command");
        Trace("ui_creation_menu", true, descriptor.displayName);
    }
    const auto createFromMenu = [&](std::string_view label, kb::scene::SceneEntity parent = {}) {
        const auto command = std::ranges::find_if(commands, [&](const auto& item) { return item.first == label; });
        if (command == commands.end() || !EditorRightButtonDownRouter::ExecuteHierarchyMenuCommand(command->second, context_, parent))
            return kb::scene::SceneEntity{};
        return context_.SelectedEntity();
    };
    const auto canvas = createFromMenu("Canvas");
    const auto button = createFromMenu("Button", canvas);
    const auto rows = context_.HierarchyRows();
    for (std::size_t index = 0; index < rows.size(); ++index)
        if (rows[index].entity == canvas && !context_.ToggleHierarchyRowExpanded(index)) return fail("collapse-parent-failed");
    const auto image = createFromMenu("Image", button);
    const auto sibling = createFromMenu("Button");
    if (!canvas.IsValid() || !button.IsValid() || !image.IsValid() || !sibling.IsValid() ||
        context_.Scene().Hierarchy().Parent(button) != canvas ||
        context_.Scene().Hierarchy().Parent(image) != button ||
        context_.Scene().Hierarchy().Parent(sibling) != canvas ||
        context_.Scene().Entities().Name(sibling) != "Button 1" ||
        context_.HierarchyRows().size() != baseline + 4) return fail("parenting-canvas-reuse-or-unique-name-failed");
    if (EditorRightButtonDownRouter::ExecuteHierarchyMenuCommand(0, context_)) return fail("unknown-command-accepted");
    for (int index = 0; index < 4; ++index) if (!context_.UndoSceneCommand()) return fail("parented-creation-undo-failed");
    if (context_.HierarchyRows().size() != baseline) return fail("creation-left-extra-entities");
    Trace("ui_creation_menu", true, "parenting-canvas-reuse-unique-names-undo");
    return true;
}

bool EditorHeadlessAutomation::VerifyUIComponentCatalog() {
    for (const auto& descriptor : kb::scene::UIComponentCatalog()) {
        auto entity = context_.CreateHierarchyObject();
        context_.SelectEntity(entity);
        if (!AddComponent(descriptor.stableId)) return false;
        if (descriptor.type == kb::scene::UIComponentType::Text) {
            const auto section = InspectorUIComponentModel::Section(descriptor.type);
            const auto property = InspectorUIComponentModel::Property(descriptor.type);
            const auto font = FindInspectorHit(context_, section, property, 1, InspectorHitKind::TextField);
            const auto text = FindInspectorHit(context_, section, property, 0, InspectorHitKind::TextField);
            if (!font || !text) return false;
            std::array<std::uint64_t, 2> before{}, after{};
            const auto capture = [&](std::string_view name, std::array<std::uint64_t, 2>& hashes) {
                return CaptureBitmap(artifactRoot_ / "screenshots" / (std::string{name} + ".bmp"),
                    kDefaultScreenshotDimensions, [&](HDC dc) {
                        InspectorPanelRenderer{}.Paint(dc, kInspectorContent, MakeEditorDarkTheme(), context_);
                        const std::array boxes{font->rect, text->rect};
                        for (std::size_t lane = 0; lane < boxes.size(); ++lane) {
                            auto& hash = hashes[lane];
                            hash = 14695981039346656037ULL;
                            for (int y = boxes[lane].top; y < boxes[lane].bottom; ++y)
                                for (int x = boxes[lane].left; x < boxes[lane].right; ++x)
                                    hash = (hash ^ GetPixel(dc, x, y)) * 1099511628211ULL;
                        }
                    });
            };
            static_cast<void>(context_.Inspector().SetHover(InspectorHitKind::None, InspectorSectionId::None, InspectorPropertyId::None));
            if (!capture("text-focus-before", before)) return false;
            static_cast<void>(context_.Inspector().SetHover(InspectorHitKind::TextField, section, property, 0));
            context_.Inspector().BeginTextEdit(property, "Editing text");
            context_.Inspector().SetEditIndex(0);
            const bool captured = capture("text-focus-active", after);
            context_.Inspector().EndTextEdit();
            static_cast<void>(context_.Inspector().SetHover(InspectorHitKind::None, InspectorSectionId::None, InspectorPropertyId::None));
            const bool isolated = captured && before[0] == after[0] && before[1] != after[1];
            Trace("ui_field_focus", isolated, isolated ? "text-only-highlight" : "text-focus-changed-font-field");
            if (!isolated) return false;
        }
        const auto components = InspectorUIComponentModel::Components(context_.Scene(), entity);
        if (components.empty() || components.front() != kb::scene::UIComponentType::RectTransform) {
            Trace("ui_catalog", false, "anchors-not-first");
            return false;
        }
        const auto entityName = context_.Scene().Entities().Name(entity);
        const auto reacquire = [&]() {
            for (const auto& candidate : context_.HierarchyRows()) {
                if (candidate.name == entityName && context_.Scene().Components().UI().Has<kb::scene::UIRectTransform>(candidate.entity)) {
                    entity = candidate.entity;
                    context_.SelectEntity(entity);
                    return true;
                }
            }
            return false;
        };
        const auto colorRows = InspectorUIComponentModel::Properties(context_.Scene(), entity, descriptor.type);
        for (const auto& row : colorRows) {
            if (!row.color) continue;
            if (!FindInspectorHit(context_, InspectorUIComponentModel::Section(descriptor.type),
                    InspectorUIComponentModel::Property(descriptor.type), row.groupStart, InspectorHitKind::ColorField)) {
                Trace("ui_palette", false, "color-swatch-not-found");
                return false;
            }
            const auto before = kb::scene::CaptureSceneUIComponents(context_.Scene().Components().UI(), entity);
            const std::array<float, 4> tint{0.21F, 0.43F, 0.67F, 0.59F};
            auto invalid = tint;
            invalid[3] = std::numeric_limits<float>::quiet_NaN();
            if (context_.SetUIColor(entity, descriptor.type, row.name, invalid) ||
                !kb::scene::AreUIComponentSetsEqual(before,
                    kb::scene::CaptureSceneUIComponents(context_.Scene().Components().UI(), entity))) {
                Trace("ui_palette", false, "invalid-color-partially-applied");
                return false;
            }
            const auto revision = context_.SceneRenderRevision();
            if (!context_.SetUIColor(entity, descriptor.type, row.name, tint) || context_.SceneRenderRevision() == revision) {
                Trace("ui_palette", false, "color-edit-not-applied-or-not-dirty");
                return false;
            }
            const auto edited = kb::scene::CaptureSceneUIComponents(context_.Scene().Components().UI(), entity);
            for (int lane = 0; lane < 4; ++lane) {
                kb::scene::UIComponentPropertyValue actual;
                if (!kb::scene::ReadUIComponentProperty(edited, descriptor.type,
                        colorRows[static_cast<std::size_t>(row.groupStart + lane)].name, actual) ||
                    std::get<float>(actual) != tint[lane]) {
                    Trace("ui_palette", false, "color-channel-not-applied");
                    return false;
                }
            }
            if (!context_.UndoSceneCommand() || !reacquire() ||
                !kb::scene::AreUIComponentSetsEqual(before,
                    kb::scene::CaptureSceneUIComponents(context_.Scene().Components().UI(), entity)) ||
                !context_.RedoSceneCommand() || !reacquire() ||
                !kb::scene::AreUIComponentSetsEqual(edited,
                    kb::scene::CaptureSceneUIComponents(context_.Scene().Components().UI(), entity))) {
                Trace("ui_palette", false, "color-not-one-undo-redo");
                return false;
            }
            Trace("ui_palette", true, std::string{descriptor.displayName} + "." + row.label);
        }
        static_cast<void>(context_.Inspector().SetScrollOffset(0, 0));
        if (!CaptureInspector(std::string{"compact-"} + std::string{descriptor.displayName})) return false;
        if (!CaptureBitmap(artifactRoot_ / "screenshots" /
                ("compact-" + SafeCheckpoint(descriptor.displayName) + "-narrow.bmp"), ScreenshotDimensions{440, 900, 96},
                [&](HDC dc) {
                    InspectorPanelRenderer{}.Paint(dc, RECT{0, 0, 440, 900}, MakeEditorDarkTheme(), context_);
                })) return false;
        const auto values = kb::scene::CaptureSceneUIComponents(context_.Scene().Components().UI(), entity);
        for (const auto& property : kb::scene::UIComponentPropertyCatalog(descriptor.type)) {
            kb::scene::UIComponentPropertyValue value;
            if (!kb::scene::ReadUIComponentProperty(values, descriptor.type, property.name, value) ||
                (property.writable && !SetUIComponentProperty(descriptor.type, property.name, value))) {
                Trace("ui_catalog", false, std::string{descriptor.displayName} + "." + std::string{property.name});
                return false;
            }
        }
        if (!SetUIComponentProperty(kb::scene::UIComponentType::RectTransform, "scale.x",
                kb::scene::UIComponentPropertyValue{1.25F})) return false;
        kb::scene::SceneUIFrame frame;
        if (!kb::scene::SceneUIQueries{context_.Scene()}.BuildFrame(1280.0F, 720.0F, frame) ||
            std::ranges::none_of(frame.elements, [entity](const auto& element) {
                return element.entity == entity && element.rect.width > 0.0F && element.rect.height > 0.0F;
            })) {
            Trace("ui_catalog", false, std::string{descriptor.displayName} + " absent from scene frame");
            return false;
        }
        Trace("ui_catalog", true, descriptor.displayName);
        auto root = entity;
        while (context_.Scene().Hierarchy().Parent(root).IsValid()) root = context_.Scene().Hierarchy().Parent(root);
        context_.SelectEntity(root);
        if (!context_.DeleteSelectedHierarchyEntity()) return false;
    }
    return true;
}

bool EditorHeadlessAutomation::SetGameplayKey(
    kb::input::InputKey key, bool down,
    std::uint8_t gamepadIndex) {
    if (key == kb::input::InputKey::None) {
        Trace("gameplay_key", false, "invalid-key");
        return false;
    }
    context_.Scene().Input().MutableDeviceState().SetKeyDown(
        key, down, gamepadIndex);
    Trace(
        "gameplay_key", true,
        std::string{ kb::input::ToString(key) } +
            (down ? ":down" : ":up"));
    return true;
}

bool EditorHeadlessAutomation::SetGameplayAnalog(
    kb::input::InputKey key, float value,
    std::uint8_t gamepadIndex) {
    if (key == kb::input::InputKey::None ||
        !kb::input::IsAnalogKey(key) || !std::isfinite(value) ||
        value < -1.0F || value > 1.0F) {
        Trace("gameplay_analog", false, "invalid-value");
        return false;
    }
    context_.Scene().Input().MutableDeviceState().SetAnalog(
        key, value, gamepadIndex);
    Trace(
        "gameplay_analog", true,
        std::string{ kb::input::ToString(key) } + ':' +
            std::to_string(value));
    return true;
}

bool EditorHeadlessAutomation::SetGameplayPointer(
    float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        Trace("gameplay_pointer", false, "invalid-position");
        return false;
    }
    context_.Scene().Input().MutableDeviceState()
        .SetPointerPosition(x, y);
    Trace(
        "gameplay_pointer", true,
        std::to_string(x) + ',' + std::to_string(y));
    return true;
}

bool EditorHeadlessAutomation::SetGameplayTouches(
    std::span<const kb::input::InputTouchPoint> points) {
    if (points.size() >
        kb::input::InputDeviceState::kMaxTouchPoints) {
        Trace("gameplay_touches", false, "too-many-points");
        return false;
    }
    for (const kb::input::InputTouchPoint& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            Trace("gameplay_touches", false, "invalid-position");
            return false;
        }
    }
    context_.Scene().Input().MutableDeviceState().SetTouchPoints(
        points);
    Trace(
        "gameplay_touches", true,
        std::to_string(points.size()) + " point(s)");
    return true;
}

bool EditorHeadlessAutomation::SetGameplayFocus(bool focused) {
    context_.Scene().Input().MutableDeviceState().SetHasFocus(
        focused);
    Trace("gameplay_focus", true, focused ? "true" : "false");
    return true;
}

bool EditorHeadlessAutomation::SetGamepadConnected(
    std::uint8_t gamepadIndex, bool connected) {
    if (gamepadIndex >=
        kb::input::InputDeviceState::kMaxGamepads) {
        Trace("gamepad_connected", false, "invalid-index");
        return false;
    }
    context_.Scene().Input().MutableDeviceState()
        .SetGamepadConnected(gamepadIndex, connected);
    Trace(
        "gamepad_connected", true,
        std::to_string(gamepadIndex) +
            (connected ? ":connected" : ":disconnected"));
    return true;
}

bool EditorHeadlessAutomation::StepRuntime(
    std::size_t frames, float deltaSeconds) {
    if (frames == 0U || !std::isfinite(deltaSeconds) ||
        deltaSeconds < 0.0F ||
        !context_.HasPlayModeSceneSession()) {
        Trace("step_runtime", false, "invalid-step");
        return false;
    }
    if (!kb::input::InputHaptics::HasBackend(context_.Scene())) {
        kb::input::InputHaptics::RegisterBackend(
            context_.Scene(), impl_->hapticsBackend);
    }
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        if (!context_.TickPlayModeSceneSession(deltaSeconds)) {
            Trace("step_runtime", false, "runtime-requested-stop");
            return false;
        }
        if (!impl_->Render(context_)) {
            Trace("step_runtime", false, "render-backend-failed");
            return false;
        }
    }
    Trace(
        "step_runtime", true,
        std::to_string(frames) + "@" +
            std::to_string(deltaSeconds));
    return true;
}

bool EditorHeadlessAutomation::StepEditorParticles(
    std::size_t frames, float deltaSeconds) {
    if (frames == 0U || !std::isfinite(deltaSeconds) ||
        deltaSeconds < 0.0F || context_.HasPlayModeSceneSession()) {
        Trace("step_editor_particles", false, "invalid-step");
        return false;
    }
    const auto before = kb::particles::ParticlePlayback::ReadRenderSnapshot(
        context_.Scene());
    const std::uint64_t revisionBefore = before == nullptr
        ? 0U
        : before->Revision();
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        if (context_.TickEditorSceneParticles(deltaSeconds) &&
            !impl_->RenderScene(context_, 1U, true)) {
            Trace("step_editor_particles", false, "render-backend-failed");
            return false;
        }
    }
    const auto after = kb::particles::ParticlePlayback::ReadRenderSnapshot(
        context_.Scene());
    const std::uint64_t revisionAfter = after == nullptr
        ? 0U
        : after->Revision();
    const bool advanced = revisionAfter > revisionBefore;
    Trace(
        "step_editor_particles",
        advanced,
        "revision=" + std::to_string(revisionBefore) + "->" +
            std::to_string(revisionAfter));
    return advanced;
}

EditorHeadlessAutomation::ParticleThumbnailVerification
EditorHeadlessAutomation::VerifyParticleThumbnail(
    kb::assets::AssetId assetId,
    std::size_t maximumTicks) {
    ParticleThumbnailVerification result{};
    if (impl_ == nullptr || impl_->window == nullptr ||
        !assetId.IsValid() || maximumTicks == 0U) {
        return result;
    }
    kb::assets::AssetManager& manager =
        context_.Scene().Assets().Manager();
    const kb::assets::AssetMetadata* metadata =
        manager.Registry().Find(assetId);
    if (metadata == nullptr ||
        metadata->type != kb::scene::kParticleEffectAssetType) {
        return result;
    }

    EditorParticleThumbnailService& thumbnails =
        EditorParticleThumbnailCache();
    thumbnails.Clear(&impl_->viewport);
    const EditorParticleThumbnailImage* image =
        thumbnails.ThumbnailFor(*metadata);
    constexpr RECT staging{632, 352, 640, 360};
    while (thumbnails.HasPendingWork() && result.ticks < maximumTicks) {
        static_cast<void>(thumbnails.Tick(
            context_, impl_->viewport, impl_->window, staging));
        ++result.ticks;
        image = thumbnails.ThumbnailFor(*metadata);
        // The production path polls at 33 ms. Yield a small bounded slice so
        // the automation exercises the asynchronous image workers instead of
        // exhausting its renderer-tick budget in a tight CPU loop.
        Sleep(10U);
    }
    const bool validStorage = image != nullptr && image->width > 0 &&
        image->height > 0 && image->bgra.size() ==
            static_cast<std::size_t>(image->width * image->height);
    const auto frameHash = [](const EditorParticleThumbnailImage& frame) {
        std::uint64_t hash = 1469598103934665603ULL;
        for (const std::uint32_t pixel : frame.bgra) {
            hash ^= pixel;
            hash *= 1099511628211ULL;
        }
        return hash;
    };
    const std::uint64_t firstFrameHash = validStorage
        ? frameHash(*image)
        : 0U;
    for (std::uint64_t frame = 1U;
         validStorage && frame < 8U; ++frame) {
        const EditorParticleThumbnailImage* candidate =
            thumbnails.ThumbnailFor(*metadata, frame);
        if (candidate != nullptr &&
            frameHash(*candidate) != firstFrameHash) {
            result.animated = true;
            break;
        }
    }
    const std::optional<kb::scene::ParticleEffectAsset> effect =
        kb::scene::ParticleEffectAssetIO::Load(metadata->physicalPath);
    bool fullTimeline = false;
    if (effect.has_value()) {
        const ParticleThumbnailTimelinePlan expected =
            ParticleThumbnailTimeline::Plan(*effect);
        fullTimeline =
            thumbnails.AnimationFrameCount(*metadata) ==
                expected.frameCount &&
            std::fabs(
                thumbnails.AnimationDurationSeconds(*metadata) -
                expected.durationSeconds) <= 0.0001F;
    }
    result.succeeded = validStorage && result.animated && fullTimeline &&
        !thumbnails.HasPendingWork();
    if (!result.succeeded) {
        thumbnails.CancelPendingWork(&impl_->viewport);
    }
    return result;
}

EditorHeadlessAutomation::InspectorDragProfile
EditorHeadlessAutomation::ProfileInspectorTransformDrag(std::size_t steps) {
    InspectorDragProfile result{};
    if (steps == 0U || steps > 1000U) {
        Trace("profile_inspector_drag", false, "step-count-out-of-range");
        return result;
    }
    const kb::scene::SceneEntity entity = context_.SelectedEntity();
    if (!context_.Scene().Entities().IsAlive(entity)) {
        Trace("profile_inspector_drag", false, "no-selection");
        return result;
    }
    if (!context_.BeginSelectedTransformEdit("Edit Transform")) {
        Trace("profile_inspector_drag", false, "transform-edit-refused");
        return result;
    }

    // Exactly what a held mouse button does: the same apply the pointer route calls,
    // once per drag step.
    const float start = context_.ActiveTransformEditPropertyStart(InspectorPropertyId::PositionX);
    const auto applyStart = std::chrono::steady_clock::now();
    for (std::size_t step = 0U; step < steps; ++step) {
        static_cast<void>(context_.ApplyActiveTransformEditProperty(
            InspectorPropertyId::PositionX, start + (static_cast<float>(step + 1U) * 0.01F)));
    }
    const double applyTotal =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - applyStart).count();
    context_.CancelActiveTransformEdit();

    // The Inspector panel repaints on every one of those steps, because that is the
    // panel the pointer is over.
    double paintTotal = 0.0;
    double heightTotal = 0.0;
    double rowPaintTotal = 0.0;
    double hitTestTotal = 0.0;
    HDC screen = GetDC(nullptr);
    HDC memory = screen == nullptr ? nullptr : CreateCompatibleDC(screen);
    HBITMAP bitmap = memory == nullptr
        ? nullptr
        : CreateCompatibleBitmap(screen, kInspectorContent.right, kInspectorContent.bottom);
    HGDIOBJ previous = bitmap == nullptr ? nullptr : SelectObject(memory, bitmap);
    if (previous != nullptr) {
        HeroIconGdiplusRuntime::EnsureStarted();
        const EditorTheme theme = MakeEditorDarkTheme();
        const auto paintStart = std::chrono::steady_clock::now();
        for (std::size_t step = 0U; step < steps; ++step) {
            InspectorPanelRenderer{}.Paint(memory, kInspectorContent, theme, context_);
        }
        paintTotal =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - paintStart).count();
        // The same measurement of the panel's content, which the paint asks for before
        // drawing anything: how much of a repaint is spent working out how tall the
        // Inspector is rather than putting pixels down.
        const auto heightStart = std::chrono::steady_clock::now();
        for (std::size_t step = 0U; step < steps; ++step) {
            static_cast<void>(InspectorPanelRenderer::MaxScrollOffset(kInspectorContent, context_));
        }
        heightTotal =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - heightStart).count();
        // The same repaint with only one property row left unclipped: what a drag would
        // cost if it asked for the row it is changing instead of the whole panel.
        const auto rowStart = std::chrono::steady_clock::now();
        for (std::size_t step = 0U; step < steps; ++step) {
            const int saved = SaveDC(memory);
            IntersectClipRect(memory, kInspectorContent.left, kInspectorContent.top + 120,
                kInspectorContent.right, kInspectorContent.top + 148);
            InspectorPanelRenderer{}.Paint(memory, kInspectorContent, theme, context_);
            RestoreDC(memory, saved);
        }
        rowPaintTotal =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rowStart).count();
        SelectObject(memory, previous);
    }
    if (bitmap != nullptr) DeleteObject(bitmap);
    if (memory != nullptr) DeleteDC(memory);
    if (screen != nullptr) ReleaseDC(nullptr, screen);

    // Moving the pointer across the panel without pressing anything asks where it is,
    // once per mouse move. That answer is worked out by walking the whole panel.
    const auto hitStart = std::chrono::steady_clock::now();
    for (std::size_t step = 0U; step < steps; ++step) {
        static_cast<void>(InspectorPanelRenderer::HitTest(
            kInspectorContent, context_, kInspectorContent.left + 200,
            kInspectorContent.top + 120 + static_cast<int>(step % 8U)));
    }
    hitTestTotal =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - hitStart).count();

    // And the scene has to be submitted again for the object to be seen moving.
    const auto sceneStart = std::chrono::steady_clock::now();
    std::size_t scenePresents = 0U;
    for (std::size_t step = 0U; step < steps; ++step) {
        if (impl_->RenderScene(context_, 1U, true)) {
            ++scenePresents;
        }
    }
    const double sceneTotal =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sceneStart).count();

    const auto divisor = static_cast<double>(steps);
    result.steps = steps;
    result.applyMs = applyTotal / divisor;
    result.inspectorPaintMs = paintTotal / divisor;
    result.inspectorHeightMs = heightTotal / divisor;
    result.inspectorRowPaintMs = rowPaintTotal / divisor;
    result.inspectorHitTestMs = hitTestTotal / divisor;
    result.scenePresentMs = sceneTotal / divisor;
    // Repainting one row of the Inspector must cost a fraction of repainting the whole
    // panel. That is what makes dragging a value cheap, and it only holds while the
    // panel skips the sections the repaint was not asked for; if that culling goes, the
    // two costs converge and a drag drowns the scene again. The margin is wide enough
    // that machine load cannot trip it: culled it costs about a fifth of a full
    // repaint, and without the culling about a half.
    const bool rowRepaintIsCheap = result.inspectorPaintMs > 0.0 &&
        result.inspectorRowPaintMs < (result.inspectorPaintMs * 0.35);
    result.succeeded = previous != nullptr && scenePresents == steps && rowRepaintIsCheap;
    Trace("profile_inspector_drag", result.succeeded,
        std::to_string(result.applyMs) + "/" + std::to_string(result.inspectorPaintMs) +
            "/" + std::to_string(result.scenePresentMs));
    return result;
}

EditorHeadlessAutomation::IdleFrameProfile
EditorHeadlessAutomation::ProfileIdleSceneFrame(std::size_t steps) {
    IdleFrameProfile result{};
    if (steps == 0U || steps > 1000U) {
        Trace("profile_idle_scene_frame", false, "step-count-out-of-range");
        return result;
    }
    HWND window = impl_->window;
    if (window == nullptr) {
        Trace("profile_idle_scene_frame", false, "no-host-window");
        return result;
    }

    // The same furniture the message loop resolves against: the workspace a new
    // project opens with, no torn-off panels, stock metrics.
    EditorDockModel dockModel;
    EditorFloatingWindowManager floatingWindows;
    const EditorMetrics metrics;

    RECT client{};
    GetClientRect(window, &client);
    const auto buildLayout = [&]() {
        return dockModel.Queries().BuildLayout(
            client.right - client.left,
            client.bottom - client.top,
            metrics.menuHeight,
            metrics.toolbarHeight,
            metrics.tabStripHeight,
            metrics.tabMinWidth,
            metrics.tabWidth,
            metrics.splitterSize);
    };

    // A sink every stage feeds, so nothing measured can be discarded as unused.
    std::size_t sink = 0U;
    const auto sample = [&](auto&& body) {
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t step = 0U; step < steps; ++step) {
            body();
        }
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count() / static_cast<double>(steps);
    };

    result.dockLayoutMs = sample([&] { sink += buildLayout().panels.size(); });
    result.hostSurfaceResolveMs = sample([&] {
        sink += EditorHostSurfaceLayoutResolver::ResolveMainWindow(
            window, dockModel, metrics, context_).size();
    });
    result.panelResolveMs = sample([&] {
        sink += EditorPanelContentResolver::Resolve(
            DockPanelKind::Inspector, window, window, dockModel, floatingWindows, metrics)
            .has_value() ? 1U : 0U;
    });
    // What ParticleEditorPanelIsVisible does: another whole layout, walked for one panel kind.
    result.particleVisibleMs = sample([&] {
        const DockLayout layout = buildLayout();
        bool visible = false;
        for (const DockPanelLayout& panelLayout : layout.panels) {
            if (!panelLayout.active) continue;
            const DockPanel* panel = dockModel.Queries().FindPanel(panelLayout.panelId);
            if (panel != nullptr && panel->kind == DockPanelKind::ParticleEditor) visible = true;
        }
        sink += visible ? 1U : 0U;
    });
    result.materialPreviewProbeMs = sample([&] {
        const kb::assets::AssetId inspectorAsset = context_.AssetBrowser().InspectorAsset();
        const kb::assets::AssetId materialAsset = context_.MaterialEditor().OpenAssetId();
        const auto& registry = context_.Scene().Assets().Manager().Registry();
        sink += (inspectorAsset.IsValid() && registry.Find(inspectorAsset) != nullptr) ? 1U : 0U;
        sink += (materialAsset.IsValid() && registry.Find(materialAsset) != nullptr) ? 1U : 0U;
    });

    // The non-render half of the frame the way it used to be performed: one layout for the
    // panel walk, one inside the host-surface resolve, and one inside each of the three
    // panel-content resolves. Five walks of one unchanged tree to ask it five questions.
    result.dockLayoutBuildsPerFrame = 5U;
    result.geometryTotalMs = sample([&] {
        sink += buildLayout().panels.size();
        sink += EditorHostSurfaceLayoutResolver::ResolveMainWindow(window, dockModel, metrics, context_).size();
        for (const DockPanelKind kind : { DockPanelKind::Inspector, DockPanelKind::MaterialEditor, DockPanelKind::Assets }) {
            sink += EditorPanelContentResolver::Resolve(
                kind, window, window, dockModel, floatingWindows, metrics).has_value() ? 1U : 0U;
        }
    });

    // The same answers, derived from a single layout build: what the frame does now.
    result.sharedGeometryMs = sample([&] {
        const DockLayout layout = buildLayout();
        sink += EditorHostSurfaceLayoutResolver::ResolveMainWindow(layout, dockModel, context_).size();
        for (const DockPanelKind kind : { DockPanelKind::Inspector, DockPanelKind::MaterialEditor, DockPanelKind::Assets }) {
            sink += EditorPanelContentResolver::Resolve(
                kind, layout, window, window, dockModel, floatingWindows, metrics).has_value() ? 1U : 0U;
        }
    });

    // The GPU half. One full sync first so the sampled frames are the steady state an
    // idle editor sits in - nothing dirty, nothing to re-upload - rather than the
    // first-frame cost of publishing the whole scene.
    static_cast<void>(impl_->RenderScene(context_, 1U, true));
    context_.AcknowledgeSceneRenderSubmitted();
    constexpr RECT bounds{ 0, 0, 640, 360 };
    std::size_t submitted = 0U;
    result.sceneSubmitMs = sample([&] {
        EditorSceneBgfxViewport::PresentSettings settings{};
        settings.renderWidth = 640U;
        settings.renderHeight = 360U;
        settings.viewportKey = 1U;
        kb::render::SceneRenderCamera camera{};
        bx::mtxLookAt(camera.view.data(), bx::Vec3{ 4.0F, 3.0F, 4.0F }, bx::Vec3{ 0.0F, 0.0F, 0.0F });
        kb::render::SceneDepthPolicy::MakePerspective(
            camera.projection.data(), 60.0F, 640.0F / 360.0F, 0.05F, 100.0F,
            kb::render::SceneDepthPolicy::HomogeneousDepth());
        settings.cameraOverride = camera;
        settings.sceneRevision = context_.SceneRenderRevision();
        settings.sceneDirtyBaseRevision = context_.SceneRenderDirtyBaseRevision();
        settings.sceneFullSyncRequired = context_.SceneRenderFullDirty();
        settings.editorSceneOverlaysEnabled = true;
        settings.selectionMaskEnabled = false;
        settings.selectionOutlineEnabled = false;
        settings.drawSafeArea = false;
        impl_->viewport.BeginPaintLayout(window);
        impl_->viewport.Present(window, bounds, context_.Scene(), settings);
        impl_->viewport.EndPaintLayout();
        context_.AcknowledgeSceneRenderSubmitted();
        ++submitted;
    });
    result.idleFrameMs = result.geometryTotalMs + result.sceneSubmitMs;
    result.steps = steps;
    // The two geometry timings are reported, not gated on. They are tens of microseconds
    // apart, and a ratio between two sub-millisecond wall-clock samples measures whatever
    // else the machine is doing: this gate failed on a build box with the shared path
    // reading 0.196 ms against the naive 0.047 ms, an inversion no code change caused.
    // A profile that fails for being measured on a busy machine teaches nothing and costs
    // a red build every time, so what stays gated is what the run either did or did not do.
    result.succeeded = submitted == steps && sink > 0U &&
        result.sceneSubmitMs > 0.0 && result.geometryTotalMs > 0.0;
    Trace("profile_idle_scene_frame", result.succeeded,
        std::to_string(result.idleFrameMs) + "/" + std::to_string(result.geometryTotalMs) +
            "/" + std::to_string(result.sceneSubmitMs));
    return result;
}

EditorHeadlessAutomation::FloatingWindowFrame
EditorHeadlessAutomation::VerifyFloatingWindowFrame() {
    FloatingWindowFrame result{};
    HINSTANCE instance = GetModuleHandleW(nullptr);
    result.reservedWithoutHandler =
        ReservedFrameHeight(instance, L"21kbFloatingFrameDefault", &DefWindowProcW);
    result.reservedWithHandler =
        ReservedFrameHeight(instance, L"21kbFloatingFrameEditor", &FloatingFrameProbeProc);
    // The editor draws the whole of a torn-off panel, so Windows must be left holding
    // none of it. What it would otherwise keep is reported beside it.
    result.succeeded = result.reservedWithHandler == 0;
    Trace("assert_floating_window_frame", result.succeeded,
        std::to_string(result.reservedWithHandler));
    return result;
}

EditorHeadlessAutomation::SavedLayoutRoundTrip
EditorHeadlessAutomation::VerifySavedLayoutRoundTrip() {
    SavedLayoutRoundTrip result{};
    const std::string name = "Headless Check";
    const std::filesystem::path root = EditorProjectPaths::ProjectRoot();

    // The project has to be handed back exactly as it was found: this runs inside a
    // scenario, not in a sandbox of its own.
    const EditorConfiguration original = context_.EditorConfig();

    EditorDockModel arranged;
    std::uint32_t floatedPanel = 0U;
    std::uint32_t closedPanel = 0U;
    for (const DockPanel& panel : arranged.Queries().Panels()) {
        if (!panel.visible || !panel.detachable || panel.id == 14U) {
            continue;
        }
        if (floatedPanel == 0U) {
            floatedPanel = panel.id;
        } else if (closedPanel == 0U) {
            closedPanel = panel.id;
        }
    }
    if (floatedPanel == 0U || closedPanel == 0U || !arranged.Commands().ClosePanel(closedPanel)) {
        Trace("assert_saved_layout_roundtrip", false, "no-rearrangeable-panels");
        return result;
    }
    arranged.Commands().UndockPanel(floatedPanel, DockRect{ 240, 200, 880, 620 });
    const EditorLayoutPreset captured = EditorWorkspaceArrangement::Capture(arranged);
    result.layout = captured.tree;

    std::string error;
    if (!EditorLayoutLibrary::Save(root, name, captured, error)) {
        Trace("assert_saved_layout_roundtrip", false, error);
        return result;
    }
    const std::vector<std::string> listed = EditorLayoutLibrary::List(root);
    result.listed = std::ranges::find(listed, name) != listed.end();

    // A layout is only worth anything if it can be put back on a workspace that is
    // nothing like it, so this starts from the arrangement a new project gets.
    EditorDockModel reopened;
    const std::optional<EditorLayoutPreset> loaded = EditorLayoutLibrary::Load(root, name);
    result.applied = loaded.has_value() &&
        EditorWorkspaceArrangement::Apply(reopened, *loaded) &&
        reopened.Commands().SerializeWorkspace() == captured.tree;
    const DockPanel* floated = reopened.Queries().FindPanel(floatedPanel);
    const DockPanel* closed = reopened.Queries().FindPanel(closedPanel);
    result.applied = result.applied && floated != nullptr && closed != nullptr &&
        floated->visible && floated->area == DockArea::Floating &&
        floated->floatingRect.width == 880 && !closed->visible;

    EditorWorkspaceSession::SaveAs(reopened, context_, name);
    const auto stored = EditorConfigurationStore::Load(
        EditorConfigurationStore::FilePath(root), root);
    result.named = stored.Succeeded() && stored.found && stored.configuration.layoutName == name;

    const std::vector<std::string> remaining =
        (static_cast<void>(EditorLayoutLibrary::Delete(root, name)), EditorLayoutLibrary::List(root));
    result.deleted = std::ranges::find(remaining, name) == remaining.end();

    result.succeeded = result.listed && result.applied && result.named && result.deleted;
    static_cast<void>(context_.SaveEditorConfig(original));
    Trace("assert_saved_layout_roundtrip", result.succeeded, result.layout);
    return result;
}

EditorHeadlessAutomation::WorkspaceLayoutPersistence
EditorHeadlessAutomation::VerifyWorkspaceLayoutPersistence() {
    WorkspaceLayoutPersistence result{};

    // Whatever the project is really set up with has to come back untouched, so the
    // rest of the scenario keeps running against the workspace it started with.
    const EditorConfiguration original = context_.EditorConfig();

    EditorDockModel arranged;
    std::uint32_t floatedPanel = 0U;
    std::uint32_t closedPanel = 0U;
    for (const DockPanel& panel : arranged.Queries().Panels()) {
        if (!panel.visible || !panel.detachable || panel.id == 14U) {
            continue;
        }
        if (floatedPanel == 0U) {
            floatedPanel = panel.id;
        } else if (closedPanel == 0U) {
            closedPanel = panel.id;
        }
    }
    if (floatedPanel == 0U || closedPanel == 0U) {
        Trace("assert_workspace_layout_persistence", false, "no-rearrangeable-panels");
        return result;
    }
    arranged.Commands().UndockPanel(floatedPanel, DockRect{ 220, 180, 900, 640 });
    if (!arranged.Commands().ClosePanel(closedPanel)) {
        Trace("assert_workspace_layout_persistence", false, "close-failed");
        return result;
    }

    // A dragged splitter lives only in the dock tree - no per-panel session can carry
    // it - so the arrangement proves the tree itself made the round trip.
    const EditorMetrics metrics{};
    const DockLayout layout = arranged.Queries().BuildLayout(
        1600, 960, metrics.menuHeight, metrics.toolbarHeight, metrics.tabStripHeight,
        metrics.tabMinWidth, metrics.tabWidth, metrics.splitterSize);
    if (layout.splitters.empty()) {
        Trace("assert_workspace_layout_persistence", false, "no-splitters");
        return result;
    }
    const DockSplitterLayout& splitter = layout.splitters.front();
    arranged.Commands().ResizeSplitter(splitter.nodeId, splitter.rect.x - 64, splitter.rect.y - 64, layout);
    result.savedLayout = arranged.Commands().SerializeWorkspace();

    EditorWorkspaceSession::Save(arranged, context_);
    const auto stored = EditorConfigurationStore::Load(
        EditorConfigurationStore::FilePath(EditorProjectPaths::ProjectRoot()),
        EditorProjectPaths::ProjectRoot());
    result.storedOnDisk = stored.Succeeded() && stored.found &&
        stored.configuration.layout == result.savedLayout;

    EditorDockModel reopened;
    EditorWorkspaceSession::Restore(reopened, context_);
    result.restoredLayout = reopened.Commands().SerializeWorkspace();
    const DockPanel* floated = reopened.Queries().FindPanel(floatedPanel);
    const DockPanel* closed = reopened.Queries().FindPanel(closedPanel);
    result.succeeded = result.storedOnDisk && !result.savedLayout.empty() &&
        result.restoredLayout == result.savedLayout && floated != nullptr && closed != nullptr &&
        floated->visible && floated->area == DockArea::Floating &&
        floated->floatingRect.width == 900 && !closed->visible;

    static_cast<void>(context_.SaveEditorConfig(original));
    Trace("assert_workspace_layout_persistence", result.succeeded, result.restoredLayout);
    return result;
}


EditorHeadlessAutomation::ParticleDependencyNavigation
EditorHeadlessAutomation::VerifyParticleDependencyNavigation() {
    ParticleDependencyNavigation result{};
    if (!context_.HasParticleEditorAsset()) {
        Trace("assert_particle_dependency_navigation", false, "no-open-effect");
        return result;
    }

    const auto inspector = context_.ParticleEditorInspector();
    result.dependencyCount = inspector.dependencies.size();
    if (inspector.dependencies.empty()) {
        Trace("assert_particle_dependency_navigation", false, "no-dependencies");
        return result;
    }
    result.expectedAsset = inspector.dependencies.front().assetId;

    const auto rows = context_.ParticleEditorEmitterRows();
    const auto recipes = context_.ParticleEditorRecipes();
    const auto resolveLayout = [&]() {
        return ParticleEditorPanelLayoutResolver::Resolve(
            kInspectorContent, rows,
            context_.ParticleEditorWorkspace().ComposerScrollOffset(), 96U,
            &inspector, recipes.size(), &context_.ParticleEditorWorkspace());
    };
    const auto inside = [](const RECT& rect, int x, int y) {
        return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
    };
    // The composer is a scrolling stream and its hit test ignores anything outside the
    // visible list, so reach a target the way an author does: scroll it into view first,
    // in the same 108-pixel steps the production wheel router uses.
    const auto scrollIntoView = [&](auto&& target) -> std::optional<ParticleEditorPanelLayout> {
        for (int step = 0; step < 64; ++step) {
            ParticleEditorPanelLayout layout = resolveLayout();
            const RECT rect = target(layout);
            if (inside(layout.emitterList, (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2)) {
                return layout;
            }
            const int maximum = ParticleEditorPanelLayoutResolver::MaximumComposerScroll(layout, 96U);
            const int current = context_.ParticleEditorWorkspace().ComposerScrollOffset();
            if (current >= maximum) {
                return std::nullopt;
            }
            context_.SetParticleEditorComposerScrollOffset(std::min(maximum, current + 108));
        }
        return std::nullopt;
    };
    const auto clickCentre = [&](const ParticleEditorPanelLayout& layout, const RECT& target) {
        return ParticleEditorPanelLayoutResolver::HitTest(
            layout, (target.left + target.right) / 2, (target.top + target.bottom) / 2);
    };

    // An assertion must not leave the editor somewhere else than it found it: later steps
    // read the live Asset Browser selection and composer state.
    const kb::assets::AssetId originalAsset = context_.AssetBrowser().SelectedAsset();
    const int originalScroll = context_.ParticleEditorWorkspace().ComposerScrollOffset();
    const bool startedExpanded = context_.ParticleEditorWorkspace().ComposerSectionExpanded(
        kb::particle_editor::ParticleEditorComposerSection::Dependencies);
    const auto restore = [&]() {
        if (context_.AssetBrowser().SelectedAsset() != originalAsset) {
            context_.AssetBrowser().ClearSelection();
            if (originalAsset.IsValid()) {
                static_cast<void>(context_.AssetBrowser().SelectAsset(
                    originalAsset, context_.Scene().Assets().Manager()));
            }
        }
        if (!startedExpanded &&
            context_.ParticleEditorWorkspace().ComposerSectionExpanded(
                kb::particle_editor::ParticleEditorComposerSection::Dependencies)) {
            context_.ToggleParticleEditorComposerSection(
                kb::particle_editor::ParticleEditorComposerSection::Dependencies);
        }
        context_.SetParticleEditorComposerScrollOffset(originalScroll);
    };

    // The Dependencies section starts collapsed, so open it through the section header
    // the panel actually draws rather than by poking workspace state.
    if (!startedExpanded) {
        const auto header = scrollIntoView(
            [](const ParticleEditorPanelLayout& layout) { return layout.dependencyHeader; });
        if (!header.has_value()) {
            Trace("assert_particle_dependency_navigation", false, "dependency-header-unreachable");
            restore();
            return result;
        }
        const ParticleEditorPanelHit headerHit = clickCentre(*header, header->dependencyHeader);
        if (headerHit.action != ParticleEditorPanelAction::ToggleComposerSection ||
            headerHit.composerSection !=
                kb::particle_editor::ParticleEditorComposerSection::Dependencies ||
            !ParticleEditorPanelInteraction::Execute(context_, headerHit)) {
            Trace("assert_particle_dependency_navigation", false, "section-toggle-failed");
            restore();
            return result;
        }
    }

    if (resolveLayout().dependencyRowCount == 0U) {
        Trace("assert_particle_dependency_navigation", false, "no-dependency-rows");
        restore();
        return result;
    }
    const auto rowLayout = scrollIntoView(
        [](const ParticleEditorPanelLayout& layout) { return layout.dependencyRows[0]; });
    if (!rowLayout.has_value()) {
        Trace("assert_particle_dependency_navigation", false, "dependency-row-unreachable");
        restore();
        return result;
    }

    const ParticleEditorPanelHit hit = clickCentre(*rowLayout, rowLayout->dependencyRows[0]);
    if (hit.action != ParticleEditorPanelAction::NavigateDependency || hit.dependencyIndex != 0U) {
        Trace("assert_particle_dependency_navigation", false, "unexpected-row-action");
        restore();
        return result;
    }

    // Start from an empty Asset Browser selection so the assertion proves the navigation
    // performed the reveal instead of reading a selection that was already there.
    context_.AssetBrowser().ClearSelection();
    const bool navigated = ParticleEditorPanelInteraction::Execute(context_, hit);
    result.selectedAsset = context_.AssetBrowser().SelectedAsset();
    result.succeeded = navigated && result.expectedAsset.IsValid() &&
        result.selectedAsset == result.expectedAsset;
    restore();
    Trace("assert_particle_dependency_navigation", result.succeeded,
        std::string{"navigated="} + (navigated ? "1" : "0") +
            ", path=" + inspector.dependencies.front().virtualPath);
    return result;
}

bool EditorHeadlessAutomation::VerifyParticlePickerInteraction() {
    if (impl_ == nullptr || impl_->window == nullptr) {
        Trace("verify_particle_picker", false, "missing-host-window");
        return false;
    }

    const kb::scene::SceneEntity previousSelection =
        context_.SelectedEntity();
    const kb::scene::SceneEntity assignmentTarget =
        context_.CreateHierarchyObject();
    if (!assignmentTarget.IsValid() ||
        !context_.AddComponentToEntity(
            assignmentTarget, "Particle Effect")) {
        if (assignmentTarget.IsValid()) {
            context_.SelectEntity(assignmentTarget);
            static_cast<void>(context_.DeleteSelectedHierarchyEntity());
        }
        Trace("verify_particle_picker", false, "assignment-target-failed");
        return false;
    }

    RECT originalBounds{};
    static_cast<void>(GetWindowRect(impl_->window, &originalBounds));
    const int width = std::max(1L, originalBounds.right - originalBounds.left);
    const int height = std::max(1L, originalBounds.bottom - originalBounds.top);
    static_cast<void>(SetWindowPos(
        impl_->window, nullptr, -16000, -16000, width, height,
        SWP_NOACTIVATE | SWP_NOZORDER));

    bool accepted = false;
    bool assignmentSucceeded = false;
    kb::assets::AssetId acceptedAsset{};
    const bool opened = EditorParticleEffectAssetPickerDialog::Open(
        impl_->window,
        MakeEditorDarkTheme(),
        context_,
        impl_->viewport,
        {},
        [this, assignmentTarget, &accepted, &acceptedAsset,
         &assignmentSucceeded](kb::assets::AssetId assetId) {
            accepted = true;
            acceptedAsset = assetId;
            assignmentSucceeded = context_.SetParticleEffectAsset(
                assignmentTarget, assetId);
        });
    constexpr wchar_t kPickerClassName[] =
        L"KBEditorMeshAssetPickerDialog";
    HWND picker = opened
        ? FindOwnedWindowByClass(impl_->window, kPickerClassName)
        : nullptr;
    const bool ownerRemainedEnabled =
        IsWindowEnabled(impl_->window) != 0;
    bool draggableHeader = false;
    bool pickerCaptured = false;
    bool pickerAnimationCaptured = false;
    std::size_t visiblePosterTicks = 0U;
    bool visiblePostersReady = false;
    if (picker != nullptr) {
        RECT pickerBounds{};
        static_cast<void>(GetWindowRect(picker, &pickerBounds));
        const LRESULT headerHit = SendMessageW(
            picker, WM_NCHITTEST, 0U,
            MAKELPARAM(pickerBounds.left + 80, pickerBounds.top + 14));
        const LRESULT closeHit = SendMessageW(
            picker, WM_NCHITTEST, 0U,
            MAKELPARAM(pickerBounds.right - 24, pickerBounds.top + 14));
        draggableHeader = headerHit == HTCAPTION && closeHit != HTCAPTION;
        // The first paint queues only the visible recipes. Automation runs
        // inside the editor callback and therefore owns the main frame while
        // this method is active; explicitly drive the same bounded scheduler
        // that the production message loop advances between paints.
        static_cast<void>(RedrawWindow(
            picker, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW));
        std::vector<const kb::assets::AssetMetadata*> recipeAssets;
        for (const kb::assets::AssetMetadata& candidate :
             context_.Scene().Assets().Manager().Registry().All()) {
            if (candidate.type != kb::scene::kParticleEffectAssetType ||
                candidate.virtualPath.parent_path().generic_string() !=
                    "/21kbParticle/Recipes") {
                continue;
            }
            recipeAssets.push_back(&candidate);
        }
        std::ranges::sort(
            recipeAssets,
            [](const kb::assets::AssetMetadata* left,
               const kb::assets::AssetMetadata* right) {
                const std::string leftName = !left->name.empty()
                    ? left->name
                    : left->virtualPath.stem().string();
                const std::string rightName = !right->name.empty()
                    ? right->name
                    : right->virtualPath.stem().string();
                return leftName != rightName
                    ? leftName < rightName
                    : left->id.value < right->id.value;
            });
        const kb::assets::AssetMetadata* animationAsset =
            recipeAssets.empty() ? nullptr : recipeAssets.front();
        if (animationAsset != nullptr) {
            static_cast<void>(
                EditorParticleThumbnailCache().ThumbnailForTime(
                    *animationAsset, 0.0));
        }
        constexpr RECT thumbnailStaging{632, 344, 640, 352};
        constexpr std::size_t kVisibleRecipeProbeCount = 5U;
        for (int tick = 0;
             tick < 512 && animationAsset != nullptr &&
             EditorParticleThumbnailCache().AnimationFrameCount(
                 *animationAsset) < 2U;
             ++tick) {
            static_cast<void>(EditorParticleThumbnailCache().Tick(
                context_, impl_->viewport, impl_->window,
                thumbnailStaging));
            static_cast<void>(SendMessageW(
                picker, WM_TIMER, 1U, 0));
            const std::size_t probeCount = std::min(
                kVisibleRecipeProbeCount, recipeAssets.size());
            visiblePostersReady = probeCount ==
                kVisibleRecipeProbeCount;
            for (std::size_t probe = 0U; probe < probeCount; ++probe) {
                if (EditorParticleThumbnailCache().ThumbnailForTime(
                        *recipeAssets[probe], 0.0) == nullptr) {
                    visiblePostersReady = false;
                }
            }
            if (!visiblePostersReady) ++visiblePosterTicks;
            Sleep(1U);
        }
        static_cast<void>(RedrawWindow(
            picker, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW));
        RECT client{};
        static_cast<void>(GetClientRect(picker, &client));
        const auto capturePicker = [picker, client](
                                       const std::filesystem::path& path) {
            bool printSucceeded = false;
            return CaptureBitmap(
                       path,
                       ScreenshotDimensions{
                           .logicalWidth = client.right - client.left,
                           .logicalHeight = client.bottom - client.top,
                           .dpi = 96,
                       },
                       [picker, &printSucceeded](HDC destination) {
                           printSucceeded = PrintWindow(
                               picker, destination, PW_CLIENTONLY) != 0;
                       }) &&
                printSucceeded;
        };
        constexpr RECT firstAssetPreview{213, 79, 367, 207};
        const auto capturePickerRegion = [picker](
                const std::filesystem::path& path,
                const RECT& region) {
            bool printSucceeded = false;
            return CaptureBitmap(
                       path,
                       ScreenshotDimensions{
                           .logicalWidth = region.right - region.left,
                           .logicalHeight = region.bottom - region.top,
                           .dpi = 96,
                       },
                       [picker, region, &printSucceeded](HDC destination) {
                           POINT previousOrigin{};
                           SetViewportOrgEx(
                               destination,
                               -region.left,
                               -region.top,
                               &previousOrigin);
                           printSucceeded = PrintWindow(
                               picker, destination, PW_CLIENTONLY) != 0;
                           SetViewportOrgEx(
                               destination,
                               previousOrigin.x,
                               previousOrigin.y,
                               nullptr);
                       }) &&
                printSucceeded;
        };
        const std::filesystem::path firstFrame =
            artifactRoot_ / "screenshots" / "particle-picker.bmp";
        const std::filesystem::path firstAnimationFrame =
            artifactRoot_ / "screenshots" /
            "particle-picker-animation-first.bmp";
        const std::filesystem::path nextAnimationFrame =
            artifactRoot_ / "screenshots" /
            "particle-picker-animation-next.bmp";
        pickerCaptured = capturePicker(firstFrame) &&
            capturePickerRegion(
                firstAnimationFrame, firstAssetPreview);
        for (std::uint64_t tick = 0U;
             tick < kParticlePickerAnimationTimerTicks;
             ++tick) {
            // Animation is wall-clock based so delayed UI messages cannot
            // speed up or shorten a lifecycle. Let real time advance just as
            // it does between production timer deliveries.
            Sleep(50U);
            static_cast<void>(SendMessageW(picker, WM_TIMER, 1U, 0));
        }
        static_cast<void>(RedrawWindow(
            picker, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW));
        const bool nextFrameCaptured = capturePickerRegion(
            nextAnimationFrame, firstAssetPreview);
        const auto fileHash = [](const std::filesystem::path& path) {
            std::ifstream input{path, std::ios::binary};
            std::uint64_t hash = 1469598103934665603ULL;
            char byte = '\0';
            while (input.get(byte)) {
                hash ^= static_cast<unsigned char>(byte);
                hash *= 1099511628211ULL;
            }
            return std::pair{input.eof(), hash};
        };
        const bool timelineAdvanced = animationAsset != nullptr &&
            EditorParticleThumbnailCache().AnimationFrameForTime(
                *animationAsset, 0.0) !=
            EditorParticleThumbnailCache().AnimationFrameForTime(
                *animationAsset, 0.1);
        pickerAnimationCaptured = pickerCaptured && nextFrameCaptured &&
            visiblePostersReady && visiblePosterTicks <= 128U &&
            timelineAdvanced &&
            fileHash(firstAnimationFrame) !=
                fileHash(nextAnimationFrame);
    }

    bool singleClickDidNotAccept = false;
    bool doubleClickAccepted = false;
    bool thumbnailWorkCancelled = false;
    if (picker != nullptr) {
        // Tile zero clears the field; tile one is the first real asset. The
        // crash regression must exercise the delayed valid-asset callback.
        constexpr LPARAM kFirstAssetTilePoint = MAKELPARAM(280, 150);
        static_cast<void>(SendMessageW(
            picker, WM_LBUTTONDOWN, MK_LBUTTON, kFirstAssetTilePoint));
        singleClickDidNotAccept = !accepted &&
            IsWindowEnabled(impl_->window) != 0;
        static_cast<void>(SendMessageW(
            picker, WM_LBUTTONDBLCLK, MK_LBUTTON, kFirstAssetTilePoint));
        doubleClickAccepted = accepted && acceptedAsset.IsValid() &&
            assignmentSucceeded;

        for (int iteration = 0;
             iteration < 64 && IsWindow(picker) != 0;
             ++iteration) {
            MSG message{};
            if (PeekMessageW(
                    &message, picker, 0U, 0U, PM_REMOVE) == 0) {
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (IsWindow(picker) != 0) {
            static_cast<void>(SendMessageW(picker, WM_CLOSE, 0U, 0));
        }
    }
    constexpr RECT cancellationStaging{632, 336, 640, 344};
    for (int poll = 0;
         poll < 128 &&
         EditorParticleThumbnailCache().HasPendingWork();
         ++poll) {
        static_cast<void>(EditorParticleThumbnailCache().Tick(
            context_, impl_->viewport, impl_->window,
            cancellationStaging));
        Sleep(1U);
    }
    thumbnailWorkCancelled =
        !EditorParticleThumbnailCache().HasPendingWork();

    static_cast<void>(SetWindowPos(
        impl_->window, nullptr, originalBounds.left, originalBounds.top,
        width, height, SWP_NOACTIVATE | SWP_NOZORDER));
    context_.SelectEntity(assignmentTarget);
    const bool assignmentTargetRemoved =
        context_.DeleteSelectedHierarchyEntity();
    if (context_.Scene().Entities().IsAlive(previousSelection)) {
        context_.SelectEntity(previousSelection);
    } else {
        context_.ClearHierarchySelection();
    }
    const bool succeeded = opened && picker != nullptr &&
        ownerRemainedEnabled && draggableHeader && pickerCaptured &&
        pickerAnimationCaptured &&
        singleClickDidNotAccept && doubleClickAccepted &&
        IsWindow(picker) == 0 && thumbnailWorkCancelled &&
        assignmentTargetRemoved;
    std::ostringstream detail;
    detail << "opened=" << opened
           << ";picker=" << (picker != nullptr)
           << ";owner-enabled=" << ownerRemainedEnabled
           << ";floating=" << draggableHeader
           << ";captured=" << pickerCaptured
           << ";animated=" << pickerAnimationCaptured
           << ";visible-posters=" << visiblePostersReady
           << ";poster-ticks=" << visiblePosterTicks
           << ";single-select=" << singleClickDidNotAccept
           << ";double-accept=" << doubleClickAccepted
           << ";closed=" << (picker == nullptr || IsWindow(picker) == 0)
           << ";cancelled=" << thumbnailWorkCancelled
           << ";cleanup=" << assignmentTargetRemoved;
    Trace("verify_particle_picker", succeeded, detail.str());
    return succeeded;
}

bool EditorHeadlessAutomation::CaptureRuntime(
    std::string_view checkpoint,
    bool requireNonUniform) {
    if (!context_.HasPlayModeSceneSession()) {
        Trace("capture_runtime", false, "play-mode-required");
        return false;
    }
    const std::filesystem::path output =
        artifactRoot_ / "screenshots" /
        (SafeCheckpoint(checkpoint) + ".png");
    const std::uint64_t capture =
        kb::scene::SceneRenderFeedback::RequestScreenCapture(
            context_.Scene(), output.string());
    if (capture == 0U) {
        Trace("capture_runtime", false, "request-rejected");
        return false;
    }
    if (!impl_->Render(context_)) {
        Trace("capture_runtime", false, "render-backend-failed");
        return false;
    }
    for (std::size_t poll = 0U; poll < 240U; ++poll) {
        if (!impl_->viewport.AdvanceAsyncReadbacks()) {
            Trace("capture_runtime", false, "render-backend-failed");
            return false;
        }
        const kb::scene::SceneScreenCaptureStatus status =
            kb::scene::SceneRenderFeedback::ScreenCaptureStatus(
                context_.Scene(), capture);
        if (status == kb::scene::SceneScreenCaptureStatus::Completed) {
            const bool valid = ValidateCapturedImage(
                output, requireNonUniform);
            Trace(
                "capture_runtime", valid,
                output.filename().string());
            return valid;
        }
        if (status == kb::scene::SceneScreenCaptureStatus::Failed) {
            Trace("capture_runtime", false, "capture-failed");
            return false;
        }
        Sleep(5U);
    }
    Trace("capture_runtime", false, "capture-timeout");
    return false;
}

bool EditorHeadlessAutomation::VerifySceneRenderTargetAfterSecondary(
    std::string_view checkpoint) {
    constexpr std::uint64_t secondaryViewportKey = 14U;
    constexpr std::uint64_t sceneViewportKey = 1U;
    if (!impl_->RenderScene(
            context_, secondaryViewportKey, true)) {
        Trace(
            "verify_scene_render_target_after_secondary", false,
            "secondary-present-failed");
        return false;
    }

    const std::filesystem::path output =
        artifactRoot_ / "screenshots" /
        (SafeCheckpoint(checkpoint) + ".png");
    const std::uint64_t capture =
        kb::scene::SceneRenderFeedback::RequestScreenCapture(
            context_.Scene(), output.string());
    if (capture == 0U) {
        Trace(
            "verify_scene_render_target_after_secondary", false,
            "request-rejected");
        return false;
    }

    if (!impl_->RenderScene(context_, sceneViewportKey, true)) {
        Trace(
            "verify_scene_render_target_after_secondary", false,
            "scene-present-failed");
        return false;
    }
    for (std::size_t poll = 0U; poll < 240U; ++poll) {
        if (!impl_->viewport.AdvanceAsyncReadbacks()) {
            Trace(
                "verify_scene_render_target_after_secondary", false,
                "scene-present-failed");
            return false;
        }
        const kb::scene::SceneScreenCaptureStatus status =
            kb::scene::SceneRenderFeedback::ScreenCaptureStatus(
                context_.Scene(), capture);
        if (status == kb::scene::SceneScreenCaptureStatus::Completed) {
            const bool valid = ValidateCapturedImage(output, true);
            Trace(
                "verify_scene_render_target_after_secondary", valid,
                output.filename().string());
            return valid;
        }
        if (status == kb::scene::SceneScreenCaptureStatus::Failed) {
            Trace(
                "verify_scene_render_target_after_secondary", false,
                "capture-failed");
            return false;
        }
        Sleep(5U);
    }
    Trace(
        "verify_scene_render_target_after_secondary", false,
        "capture-timeout");
    return false;
}

bool EditorHeadlessAutomation::InspectorPointerDown(
    int x, int y) {
    const InspectorPanelRenderer::Hit hit =
        InspectorPanelRenderer::HitTest(
            kInspectorContent, context_, x, y);
    const bool routed =
        InspectorPanelInteraction::HandlePointerDown(
            context_, hit, x, y);
    Trace("inspector_pointer_down", routed);
    return routed;
}

bool EditorHeadlessAutomation::InspectorPointerDrag(
    int x, int y) {
    const bool routed =
        InspectorPanelInteraction::HandlePointerDrag(
            context_, x, y);
    Trace("inspector_pointer_drag", routed);
    return routed;
}

bool EditorHeadlessAutomation::InspectorPointerUp() {
    const bool routed =
        InspectorPanelInteraction::HandlePointerUp(context_);
    Trace("inspector_pointer_up", routed);
    return routed;
}

bool EditorHeadlessAutomation::InspectorChar(
    wchar_t character) {
    const bool routed =
        InspectorPanelInteraction::HandleChar(
            context_, character);
    Trace("inspector_char", routed);
    return routed;
}

bool EditorHeadlessAutomation::InspectorKey(
    std::uintptr_t key) {
    const bool routed =
        InspectorPanelInteraction::HandleKeyDown(
            nullptr, context_, static_cast<WPARAM>(key));
    Trace("inspector_key", routed);
    return routed;
}

bool EditorHeadlessAutomation::CaptureInspector(
    std::string_view checkpoint) {
    const std::filesystem::path path =
        artifactRoot_ / "screenshots" /
        (SafeCheckpoint(checkpoint) + ".bmp");
    const bool saved = CaptureBitmap(
        path, kDefaultScreenshotDimensions, [this](HDC memory) {
        InspectorPanelRenderer renderer;
        renderer.Paint(
            memory, kInspectorContent, MakeEditorDarkTheme(),
            context_);
    });
    Trace("capture_inspector", saved, path.filename().string());
    return saved;
}

bool EditorHeadlessAutomation::CapturePanel(
    std::string_view panel, std::string_view checkpoint, int width, int height) {
    if (width < 240 || width > 3840 || height < 200 || height > 2160) return false;
    const RECT bounds{0, 0, width, height};
    const auto kind = ParsePanelKind(panel);
    if (!kind.has_value()) {
        Trace("capture_panel", false, panel);
        return false;
    }
    const std::filesystem::path path =
        artifactRoot_ / "screenshots" /
        (SafeCheckpoint(checkpoint) + ".bmp");
    bool panelContentCaptured = true;
    const bool saved = CaptureBitmap(
        path, ScreenshotDimensions{width, height, 96},
        [this, kind, bounds, &panelContentCaptured](HDC memory) {
            const DockPanel dockPanel{
                .id = 1U,
                .kind = *kind,
                .title = "Headless Automation",
                .area = DockArea::Center,
            };
            const EditorTheme theme = MakeEditorDarkTheme();
            const EditorMetrics metrics{};
            const EditorRenderBackendSettings settings{};
            PanelContentRenderer{}.Paint(
                memory, bounds, bounds,
                bounds, bounds, dockPanel,
                theme, metrics, context_, settings, false);
            if (*kind == DockPanelKind::ScriptEditor &&
                context_.ScriptEditor().IsOpen()) {
                panelContentCaptured = false;
                if (impl_->scriptEditorWindow == nullptr ||
                    IsWindow(impl_->scriptEditorWindow) == 0) {
                    impl_->scriptEditorWindow =
                        ScriptEditorWindow::Ensure(impl_->window);
                }
                if (impl_->scriptEditorWindow != nullptr) {
                    const RECT body =
                        ScriptEditorPanelRenderer::BodyRect(
                            bounds);
                    ScriptEditorWindow::Sync(
                        impl_->scriptEditorWindow,
                        body,
                        context_.ScriptEditor().FilePath(),
                        context_.ScriptEditor().Generation());
                    const int savedDc = SaveDC(memory);
                    if (savedDc != 0 &&
                        SetViewportOrgEx(
                            memory, body.left, body.top, nullptr) != 0) {
                        const bool printed = SendMessageW(
                            impl_->scriptEditorWindow,
                            WM_PRINTCLIENT,
                            reinterpret_cast<WPARAM>(memory),
                            PRF_CLIENT) != 0;
                        panelContentCaptured =
                            RestoreDC(memory, savedDc) != 0 && printed;
                    } else if (savedDc != 0) {
                        static_cast<void>(RestoreDC(memory, savedDc));
                    }
                }
            }
        });
    const bool succeeded = saved && panelContentCaptured;
    Trace(
        "capture_panel", succeeded,
        std::string{ panel } + ':' + path.filename().string());
    return succeeded;
}

bool EditorHeadlessAutomation::CapturePanelScreenshotMatrix(
    std::string_view panel, std::string_view checkpoint) {
    const auto kind = ParsePanelKind(panel);
    if (!kind.has_value() ||
        (*kind != DockPanelKind::MaterialEditor &&
         *kind != DockPanelKind::SkeletalMeshEditor &&
         *kind != DockPanelKind::AnimationClipEditor &&
         *kind != DockPanelKind::AnimatorEditor &&
         *kind != DockPanelKind::ParticleEditor)) {
        Trace("capture_panel_screenshot_matrix", false, panel);
        return false;
    }

    const std::string safeCheckpoint = SafeCheckpoint(checkpoint);
    bool succeeded = true;
    for (const ScreenshotProfile& profile : kEditorScreenshotProfiles) {
        for (const bool floating : { false, true }) {
            const char* const placement = floating ? "floating" : "docked";
            const std::filesystem::path path =
                artifactRoot_ / "screenshots" /
                (safeCheckpoint + "-" + std::string{ profile.name } +
                 "-" + placement + ".bmp");

            bool rendered = true;
            const bool saved = CaptureBitmap(
                path, profile.dimensions,
                [this, kind, floating, &rendered, &profile](HDC memory) {
                    EditorDockModel dockModel;
                    if (!dockModel.Commands().ActivatePanelKind(
                            *kind, DockArea::Center)) {
                        rendered = false;
                        return;
                    }

                    const DockPanel* panelToRender = nullptr;
                    for (const DockPanel& candidate :
                         dockModel.Queries().Panels()) {
                        if (candidate.kind == *kind) {
                            panelToRender = &candidate;
                            break;
                        }
                    }
                    if (panelToRender == nullptr) {
                        rendered = false;
                        return;
                    }

                    const EditorTheme theme = MakeEditorDarkTheme();
                    const EditorMetrics metrics{};
                    const EditorRenderBackendSettings settings{};
                    if (!floating) {
                        EditorPlayModeState playMode;
                        EditorShellInteractionState shellInteraction;
                        const RECT fullSurface{
                            0, 0, profile.dimensions.logicalWidth, profile.dimensions.logicalHeight };
                        DockWorkspaceRenderer{}.Paint(
                            impl_->window, memory, fullSurface,
                            profile.dimensions.logicalWidth,
                            profile.dimensions.logicalHeight, dockModel,
                            theme, metrics, context_, settings, nullptr,
                            nullptr, playMode, shellInteraction, nullptr);
                        return;
                    }

                    const std::uint32_t panelId = panelToRender->id;
                    dockModel.Commands().UndockPanel(
                        panelId,
                        DockRect{
                            .x = 80,
                            .y = 80,
                            .width = profile.dimensions.logicalWidth,
                            .height = profile.dimensions.logicalHeight,
                        });
                    panelToRender =
                        dockModel.Queries().FindPanel(panelId);
                    if (panelToRender == nullptr ||
                        panelToRender->area != DockArea::Floating) {
                        rendered = false;
                        return;
                    }
                    FloatingEditorWindowRenderer{}.Paint(
                        memory, impl_->window,
                        RECT{
                            0, 0,
                            profile.dimensions.logicalWidth,
                            profile.dimensions.logicalHeight,
                        },
                        *panelToRender, theme, metrics, context_, settings,
                        nullptr);
                });
            const bool captureSucceeded = saved && rendered;
            succeeded = captureSucceeded && succeeded;
            Trace(
                "capture_panel_screenshot",
                captureSucceeded,
                std::string{ panel } + ':' +
                    std::string{ profile.name } + ':' + placement + ':' +
                    path.filename().string());
        }
    }
    return succeeded;
}

bool EditorHeadlessAutomation::VerifyViewportHostLifecycle() {
    if (impl_->window == nullptr ||
        SetWindowPos(
            impl_->window, HWND_BOTTOM, -10000, -10000, 640, 360,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW) == 0) {
        Trace("verify_viewport_host_lifecycle", false, "initial-present-failed");
        return false;
    }

    constexpr std::uint64_t viewportKey = 1U;
    constexpr RECT initialBounds{ 0, 0, 640, 360 };

    // The animation editors (Skeletal Mesh / Animation Clip / Animator) present
    // their preview through the shared host-surface mechanism keyed by panel.id.
    // Mirror the Animator Editor path so the lifecycle assertions below cover
    // every host surface registered for this window, not only the scene key.
    const bool animatorAssetOpen = context_.HasAnimatorEditorAsset() &&
        context_.AnimatorEditorPreviewScene() != nullptr;
    std::uint64_t animatorPreviewKey = 0U;
    if (animatorAssetOpen) {
        EditorDockModel animatorDock;
        if (animatorDock.Commands().ActivatePanelKind(DockPanelKind::AnimatorEditor, DockArea::Center)) {
            for (const DockPanel& panel : animatorDock.Queries().Panels()) {
                if (panel.kind == DockPanelKind::AnimatorEditor) {
                    animatorPreviewKey = panel.id;
                    break;
                }
            }
        }
    }
    if (!impl_->RenderAll(context_, animatorPreviewKey)) {
        Trace("verify_viewport_host_lifecycle", false, "initial-present-failed");
        return false;
    }

    const std::vector<std::uint64_t> expectedKeys =
        impl_->viewport.HostSurfaceKeysForHost(impl_->window);
    const auto visible = [this](std::uint64_t key) {
        return impl_->viewport.IsHostSurfaceVisible(impl_->window, key);
    };
    const auto allExpectedVisible = [this, &expectedKeys, &visible] {
        return !expectedKeys.empty() &&
            std::ranges::all_of(expectedKeys, visible);
    };
    const auto noneVisible = [this, &visible] {
        return std::ranges::none_of(
            impl_->viewport.HostSurfaceKeysForHost(impl_->window), visible);
    };

    const bool initiallyVisible = visible(viewportKey) &&
        FindViewportClipWindow(impl_->window, initialBounds) != nullptr;
    const bool animatorPreviewCovered = !animatorAssetOpen ||
        (animatorPreviewKey != 0U &&
         std::ranges::find(expectedKeys, animatorPreviewKey) != expectedKeys.end() &&
         visible(animatorPreviewKey));

    // WM_SIZE/SIZE_MINIMIZED: hide the child before the parent is hidden.
    impl_->viewport.SetHostSurfaceSuspended(impl_->window, true);
    const bool minimizedHidden = noneVisible();
    impl_->viewport.SetHostSurfaceSuspended(impl_->window, false);
    const bool resumedAfterMinimize = impl_->viewport.PresentRequested() &&
        impl_->RenderAll(context_, animatorPreviewKey) && allExpectedVisible();

    // Show a production overlay popup (scene viewport toolbar dropdown)
    // through the same paint path the live editor uses, owned by the test
    // window. The painter performs GDI only here; host-surface presents stay
    // with RenderAll, so the overlay phase adds no extra surfaces.
    EditorDockModel overlayDock;
    std::uint64_t overlayScenePanelId = 0U;
    if (overlayDock.Commands().ActivatePanelKind(DockPanelKind::Scene, DockArea::Center)) {
        for (const DockPanel& panel : overlayDock.Queries().Panels()) {
            if (panel.kind == DockPanelKind::Scene) {
                overlayScenePanelId = panel.id;
                break;
            }
        }
    }
    bool overlayShown = false;
    if (overlayScenePanelId != 0U) {
        context_.ViewportPreview(overlayScenePanelId).OpenToolbarDropdown(
            EditorViewportToolbarDropdown::GridSpacing);
        InvalidateRect(impl_->window, nullptr, FALSE);
        const EditorTheme theme = MakeEditorDarkTheme();
        const EditorMetrics metrics{};
        const EditorRenderBackendSettings settings{};
        EditorPlayModeState playMode;
        EditorShellInteractionState shellInteraction;
        EditorPointerDragState drag;
        MainWindowBackBufferPainter::Paint(
            impl_->window, overlayDock, theme, metrics, context_,
            nullptr, nullptr, drag, settings, playMode,
            shellInteraction, impl_->viewport);
        overlayShown = HasVisibleOwnedOverlay(impl_->window);
    }

    // WM_ACTIVATEAPP(wparam == FALSE): native render surfaces are WS_CHILD,
    // so the router leaves their last frame intact. This prevents a gray Scene
    // View while inactive; only owned WS_POPUP overlays must be hidden.
    MainWindowBackBufferPainter::HideAllOverlays();
    FloatingWindowBackBufferPainter::HideAllOverlays();
    const bool sceneStayedVisibleOnDeactivate = allExpectedVisible();
    const bool overlayHiddenOnDeactivate = !HasVisibleOwnedOverlay(impl_->window);
    bool overlayRestoredOnActivate = true;
    if (overlayShown) {
        // The repaint on reactivation re-shows the overlay because the UI
        // state (open toolbar dropdown) still requires it.
        InvalidateRect(impl_->window, nullptr, FALSE);
        const EditorTheme theme = MakeEditorDarkTheme();
        const EditorMetrics metrics{};
        const EditorRenderBackendSettings settings{};
        EditorPlayModeState playMode;
        EditorShellInteractionState shellInteraction;
        EditorPointerDragState drag;
        MainWindowBackBufferPainter::Paint(
            impl_->window, overlayDock, theme, metrics, context_,
            nullptr, nullptr, drag, settings, playMode,
            shellInteraction, impl_->viewport);
        overlayRestoredOnActivate = HasVisibleOwnedOverlay(impl_->window);
    }
    if (overlayScenePanelId != 0U) {
        context_.ViewportPreview(overlayScenePanelId).CloseToolbarDropdown();
        MainWindowBackBufferPainter::HideAllOverlays();
        FloatingWindowBackBufferPainter::HideAllOverlays();
    }

    // WM_DPICHANGED hides the old physical-pixel child until the next paint.
    impl_->viewport.NotifyHostDpiChanged(impl_->window);
    const bool dpiTransitionHidden = noneVisible();
    const bool resumedAfterDpi = impl_->viewport.PresentRequested() &&
        impl_->RenderAll(context_, animatorPreviewKey) && allExpectedVisible();

    constexpr RECT movedBounds{ 113, 57, 529, 291 };
    const EditorSceneBgfxViewport::HostSurfaceLayout movedLayout{
        .viewportKey = viewportKey,
        .bounds = movedBounds,
    };
    impl_->viewport.SyncHostSurfaceLayoutsForResize(
        impl_->window,
        std::span<const EditorSceneBgfxViewport::HostSurfaceLayout>{
            &movedLayout, 1U });
    // No leaked viewport during resize/move: the moved surface tracks its new
    // bounds and every surface absent from the layout stays hidden.
    const bool resizeMoveHasNoLeakedViewport = visible(viewportKey) &&
        FindViewportClipWindow(impl_->window, movedBounds) != nullptr &&
        std::ranges::all_of(
            impl_->viewport.HostSurfaceKeysForHost(impl_->window),
            [&visible](std::uint64_t key) {
                return key == viewportKey || !visible(key);
            });

    // A layout without the viewport must hide the previous child surface.
    impl_->viewport.SyncHostSurfaceLayoutsForResize(
        impl_->window,
        std::span<const EditorSceneBgfxViewport::HostSurfaceLayout>{});
    const bool removedViewportHidden = noneVisible();

    const bool succeeded = initiallyVisible && animatorPreviewCovered &&
        minimizedHidden && resumedAfterMinimize && overlayShown &&
        sceneStayedVisibleOnDeactivate && overlayHiddenOnDeactivate &&
        overlayRestoredOnActivate &&
        dpiTransitionHidden && resumedAfterDpi && resizeMoveHasNoLeakedViewport &&
        removedViewportHidden;
    ShowWindow(impl_->window, SW_HIDE);
    if (succeeded) {
        Trace(
            "verify_viewport_host_lifecycle", true,
            "minimize,deactivate,overlay,dpi,resize-move");
        return true;
    }
    std::string detail = "failed:";
    const auto append = [&detail](bool passed, std::string_view name) {
        if (!passed) {
            detail += ' ';
            detail += name;
        }
    };
    append(initiallyVisible, "initial");
    append(animatorPreviewCovered, "animator-preview");
    append(minimizedHidden, "minimize-hide");
    append(resumedAfterMinimize, "minimize-resume");
    append(overlayShown, "overlay-show");
    append(sceneStayedVisibleOnDeactivate, "deactivate-scene-visible");
    append(overlayHiddenOnDeactivate, "overlay-deactivate-hide");
    append(overlayRestoredOnActivate, "overlay-reactivate-show");
    append(dpiTransitionHidden, "dpi-hide");
    append(resumedAfterDpi, "dpi-resume");
    append(resizeMoveHasNoLeakedViewport, "resize-move");
    append(removedViewportHidden, "remove-hide");
    Trace("verify_viewport_host_lifecycle", false, detail);
    return false;
}

bool EditorHeadlessAutomation::SnapshotInspectorTree(
    std::string_view checkpoint) {
    const std::filesystem::path path =
        artifactRoot_ / "snapshots" /
        (SafeCheckpoint(checkpoint) + "-ui-tree.json");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        Trace("snapshot_ui", false, path.filename().string());
        return false;
    }
    std::set<
        std::tuple<int, int, int, int, int, int, int, int>>
        unique;
    output << "{\"width\":" << kInspectorContent.right
           << ",\"height\":" << kInspectorContent.bottom
           << ",\"controls\":[";
    bool first = true;
    for (int y = 0; y < kInspectorContent.bottom; ++y) {
        for (int x = 0; x < kInspectorContent.right; x += 3) {
            const auto hit = InspectorPanelRenderer::HitTest(
                kInspectorContent, context_, x, y);
            if (hit.kind == InspectorHitKind::None) continue;
            const auto key = std::tuple{
                static_cast<int>(hit.kind),
                static_cast<int>(hit.section),
                static_cast<int>(hit.property),
                hit.index, hit.rect.left, hit.rect.top,
                hit.rect.right, hit.rect.bottom };
            if (!unique.insert(key).second) continue;
            if (!first) output << ',';
            first = false;
            output << "{\"role\":" << static_cast<int>(hit.kind)
                   << ",\"section\":"
                   << static_cast<int>(hit.section)
                   << ",\"property\":"
                   << static_cast<int>(hit.property)
                   << ",\"index\":" << hit.index
                   << ",\"bounds\":[" << hit.rect.left << ','
                   << hit.rect.top << ',' << hit.rect.right << ','
                   << hit.rect.bottom << "]}";
        }
    }
    output << "]}\n";
    const bool saved = output.good();
    Trace("snapshot_ui", saved, path.filename().string());
    return saved;
}

void EditorHeadlessAutomation::SnapshotConsole(
    std::string_view checkpoint) {
    const std::filesystem::path path =
        artifactRoot_ / "snapshots" /
        (SafeCheckpoint(checkpoint) + "-console.log");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    for (const EditorConsoleEntry& entry :
         context_.Console().Entries()) {
        output << entry.sequence << '\t'
               << static_cast<int>(entry.level) << '\t'
               << entry.category << '\t' << entry.message << '\n';
    }
    Trace("snapshot_console", output.good(), path.filename().string());
}

void EditorHeadlessAutomation::Trace(
    std::string_view operation, bool succeeded,
    std::string_view detail) {
    std::ofstream output(
        tracePath_, std::ios::binary | std::ios::app);
    if (!output) return;
    output << "{\"operation\":\"" << JsonEscape(operation)
           << "\",\"succeeded\":"
           << (succeeded ? "true" : "false")
           << ",\"detail\":\"" << JsonEscape(detail)
           << "\"}\n";
}

const std::filesystem::path&
EditorHeadlessAutomation::ArtifactRoot() const noexcept {
    return artifactRoot_;
}

} // namespace kb::editor
#endif
