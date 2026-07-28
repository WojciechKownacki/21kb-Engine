#include "app/EditorHeadlessAutomation.hpp"

#if defined(_WIN32)
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorPanelInteraction.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

namespace kb::editor {
namespace {

constexpr RECT kInspectorContent{ 0, 0, 900, 700 };

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
    for (int scroll = 0;
         scroll <= InspectorPanelRenderer::MaxScrollOffset(
             kInspectorContent, context);
         scroll += 520) {
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
        if (scroll >= maxScroll) break;
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

} // namespace

EditorHeadlessAutomation::EditorHeadlessAutomation(
    EditorSceneContext& context,
    std::filesystem::path artifactRoot)
    : context_(context)
    , artifactRoot_(std::filesystem::absolute(
          std::move(artifactRoot)))
    , tracePath_(artifactRoot_ / "trace.jsonl") {
    std::error_code error;
    std::filesystem::create_directories(
        artifactRoot_ / "screenshots", error);
    std::filesystem::create_directories(
        artifactRoot_ / "snapshots", error);
}

bool EditorHeadlessAutomation::AddComponent(
    std::string_view componentId) {
    const InspectorComponentTile* tile =
        InspectorComponentCatalog::Find(componentId);
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
    for (const char character : componentId) {
        static_cast<void>(InspectorPanelInteraction::HandleChar(
            context_, static_cast<wchar_t>(character)));
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
    Trace("add_component", routed, componentId);
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
    kb::input::InputKey key, bool down) {
    if (key == kb::input::InputKey::None) {
        Trace("gameplay_key", false, "invalid-key");
        return false;
    }
    context_.Scene().Input().MutableDeviceState().SetKeyDown(
        key, down);
    Trace(
        "gameplay_key", true,
        std::string{ kb::input::ToString(key) } +
            (down ? ":down" : ":up"));
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
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        static_cast<void>(
            context_.Scene().Runtime().Update(deltaSeconds));
        context_.SurfaceScriptDiagnostics();
    }
    Trace(
        "step_runtime", true,
        std::to_string(frames) + "@" +
            std::to_string(deltaSeconds));
    return true;
}

bool EditorHeadlessAutomation::CaptureInspector(
    std::string_view checkpoint) {
    const std::filesystem::path path =
        artifactRoot_ / "screenshots" /
        (SafeCheckpoint(checkpoint) + ".bmp");
    HDC screen = GetDC(nullptr);
    HDC memory = screen == nullptr ? nullptr : CreateCompatibleDC(screen);
    HBITMAP bitmap = memory == nullptr
        ? nullptr
        : CreateCompatibleBitmap(
              screen, kInspectorContent.right,
              kInspectorContent.bottom);
    HGDIOBJ previous = bitmap == nullptr
        ? nullptr
        : SelectObject(memory, bitmap);
    bool saved = false;
    if (previous != nullptr) {
        HeroIconGdiplusRuntime::EnsureStarted();
        InspectorPanelRenderer renderer;
        renderer.Paint(
            memory, kInspectorContent, MakeEditorDarkTheme(),
            context_);
        if (const auto encoder = EncoderClsid(L"image/bmp")) {
            Gdiplus::Bitmap image(bitmap, nullptr);
            saved = image.Save(
                        path.wstring().c_str(), &*encoder, nullptr) ==
                Gdiplus::Ok;
        }
        SelectObject(memory, previous);
    }
    if (bitmap != nullptr) DeleteObject(bitmap);
    if (memory != nullptr) DeleteDC(memory);
    if (screen != nullptr) ReleaseDC(nullptr, screen);
    Trace("capture_inspector", saved, path.filename().string());
    return saved;
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
