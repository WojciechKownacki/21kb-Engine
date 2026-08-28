#include "app/EditorHeadlessAutomation.hpp"

#if defined(_WIN32)
#include "app/EditorWorkspaceSession.hpp"
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
#include "platform/win32/EditorParticleEffectAssetPickerDialog.hpp"
#include "rendering/DockWorkspaceRenderer.hpp"
#include "rendering/FloatingWindowBackBufferPainter.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorParticleThumbnailService.hpp"
#include "rendering/ParticleThumbnailTimeline.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
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
#include "engine/scene/SceneEntities.hpp"
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
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
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
    int index = -1) {
    for (int scroll = 0;;) {
        const int maxScroll = InspectorPanelRenderer::MaxScrollOffset(
            kInspectorContent, context);
        static_cast<void>(
            const_cast<EditorSceneContext&>(context).Inspector()
                .SetScrollOffset(
                    std::min(scroll, maxScroll), maxScroll));
        for (int y = kInspectorContent.top;
             y < kInspectorContent.bottom; ++y) {
            for (int x = kInspectorContent.left;
                 x < kInspectorContent.right; x += 4) {
                const InspectorPanelRenderer::Hit hit =
                    InspectorPanelRenderer::HitTest(
                        kInspectorContent, context, x, y);
                if (hit.section == section &&
                    hit.property == property &&
                    (index < 0 || hit.index == index)) {
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
        constexpr RECT bounds{ 0, 0, 640, 360 };
        EditorSceneBgfxViewport::PresentSettings settings{};
        settings.renderWidth = 640U;
        settings.renderHeight = 360U;
        settings.viewportKey = viewportKey;
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
    const int wideLength = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, tile->label.data(),
        static_cast<int>(tile->label.size()), nullptr, 0);
    if (wideLength <= 0) {
        Trace("add_component", false, "invalid-utf8-label");
        return false;
    }
    std::wstring wideLabel(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, tile->label.data(),
            static_cast<int>(tile->label.size()), wideLabel.data(), wideLength) != wideLength) {
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
    std::string_view panel, std::string_view checkpoint) {
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
        path, kDefaultScreenshotDimensions,
        [this, kind, &panelContentCaptured](HDC memory) {
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
                memory, kInspectorContent, kInspectorContent,
                kInspectorContent, kInspectorContent, dockPanel,
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
                            kInspectorContent);
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
