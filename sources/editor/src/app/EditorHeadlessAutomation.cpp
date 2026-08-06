#include "app/EditorHeadlessAutomation.hpp"

#if defined(_WIN32)
#include "app/EditorPlayModeState.hpp"
#include "app/EditorShellInteractionState.hpp"
#include "docking/EditorDockModel.hpp"
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorPanelInteraction.hpp"
#include "rendering/DockWorkspaceRenderer.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/FloatingEditorWindowRenderer.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "rendering/PanelContentRenderer.hpp"
#include "rendering/ScriptEditorPanelRenderer.hpp"
#include "rendering/script_editor/ScriptEditorWindow.hpp"
#include "scene/EditorSceneContext.hpp"

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputHaptics.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/platform/win32/Win32XInputHapticsBackend.hpp"
#include "engine/scene/Scene.hpp"
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

    [[nodiscard]] bool Render(EditorSceneContext& context) {
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
    for (std::size_t frame = 0U; frame < 120U; ++frame) {
        if (!impl_->Render(context_)) {
            Trace("capture_runtime", false, "render-backend-failed");
            return false;
        }
        const kb::scene::SceneScreenCaptureStatus status =
            kb::scene::SceneRenderFeedback::ScreenCaptureStatus(
                context_.Scene(), capture);
        if (status == kb::scene::SceneScreenCaptureStatus::Completed) {
            bool valid = std::filesystem::is_regular_file(output);
            if (valid && requireNonUniform) {
                HeroIconGdiplusRuntime::EnsureStarted();
                Gdiplus::Bitmap image(output.wstring().c_str());
                valid = image.GetLastStatus() == Gdiplus::Ok &&
                    image.GetWidth() > 0U && image.GetHeight() > 0U;
                Gdiplus::Color first{};
                bool firstSet = false;
                bool varied = false;
                for (UINT y = 0U; valid && !varied &&
                     y < image.GetHeight(); ++y) {
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
                valid = valid && varied;
            }
            Trace(
                "capture_runtime", valid,
                output.filename().string());
            return valid;
        }
        if (status == kb::scene::SceneScreenCaptureStatus::Failed) {
            Trace("capture_runtime", false, "capture-failed");
            return false;
        }
    }
    Trace("capture_runtime", false, "capture-timeout");
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
         *kind != DockPanelKind::AnimatorEditor)) {
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
                        DockWorkspaceRenderer{}.Paint(
                            impl_->window, memory,
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
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW) == 0 ||
        !impl_->Render(context_)) {
        Trace("verify_viewport_host_lifecycle", false, "initial-present-failed");
        return false;
    }

    constexpr std::uint64_t viewportKey = 1U;
    constexpr RECT initialBounds{ 0, 0, 640, 360 };
    const auto visible = [this] {
        return impl_->viewport.IsHostSurfaceVisible(impl_->window, viewportKey);
    };
    const bool initiallyVisible = visible() &&
        FindViewportClipWindow(impl_->window, initialBounds) != nullptr;

    // WM_SIZE/SIZE_MINIMIZED: hide the child before the parent is hidden.
    impl_->viewport.SetHostSurfaceSuspended(impl_->window, true);
    const bool minimizedHidden = !visible();
    impl_->viewport.SetHostSurfaceSuspended(impl_->window, false);
    const bool resumedAfterMinimize = impl_->viewport.PresentRequested() &&
        impl_->Render(context_) && visible();

    // WM_ACTIVATEAPP applies to every host surface, including floating hosts.
    impl_->viewport.SetAllHostSurfacesSuspended(true);
    const bool deactivatedHidden = !visible();
    impl_->viewport.SetAllHostSurfacesSuspended(false);
    const bool resumedAfterDeactivate = impl_->viewport.PresentRequested() &&
        impl_->Render(context_) && visible();

    // WM_DPICHANGED hides the old physical-pixel child until the next paint.
    impl_->viewport.NotifyHostDpiChanged(impl_->window);
    const bool dpiTransitionHidden = !visible();
    const bool resumedAfterDpi = impl_->viewport.PresentRequested() &&
        impl_->Render(context_) && visible();

    constexpr RECT movedBounds{ 113, 57, 529, 291 };
    const EditorSceneBgfxViewport::HostSurfaceLayout movedLayout{
        .viewportKey = viewportKey,
        .bounds = movedBounds,
    };
    impl_->viewport.SyncHostSurfaceLayoutsForResize(
        impl_->window,
        std::span<const EditorSceneBgfxViewport::HostSurfaceLayout>{
            &movedLayout, 1U });
    const bool resizeMoveHasNoLeakedViewport = visible() &&
        FindViewportClipWindow(impl_->window, movedBounds) != nullptr;

    // A layout without the viewport must hide the previous child surface.
    impl_->viewport.SyncHostSurfaceLayoutsForResize(
        impl_->window,
        std::span<const EditorSceneBgfxViewport::HostSurfaceLayout>{});
    const bool removedViewportHidden = !visible();

    const bool succeeded = initiallyVisible && minimizedHidden &&
        resumedAfterMinimize && deactivatedHidden && resumedAfterDeactivate &&
        dpiTransitionHidden && resumedAfterDpi && resizeMoveHasNoLeakedViewport &&
        removedViewportHidden;
    ShowWindow(impl_->window, SW_HIDE);
    Trace(
        "verify_viewport_host_lifecycle", succeeded,
        succeeded ? "minimize,deactivate,dpi,resize-move" : "lifecycle-invariant-failed");
    return succeeded;
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
