#include "rendering/AnimatorEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <string>

namespace kb::editor {
namespace {

constexpr int kHeaderHeight = 30;

[[nodiscard]] const char* ParameterTypeLabel(kb::scene::AnimatorParameterType type) noexcept {
    switch (type) {
    case kb::scene::AnimatorParameterType::Bool: return "Bool";
    case kb::scene::AnimatorParameterType::Int: return "Int";
    case kb::scene::AnimatorParameterType::Float: return "Float";
    case kb::scene::AnimatorParameterType::Trigger: return "Trigger";
    }
    return "Unknown";
}

[[nodiscard]] const char* ConditionModeLabel(kb::scene::AnimatorConditionMode mode) noexcept {
    switch (mode) {
    case kb::scene::AnimatorConditionMode::BoolEquals: return "==";
    case kb::scene::AnimatorConditionMode::IntEquals: return "==";
    case kb::scene::AnimatorConditionMode::IntGreater: return ">";
    case kb::scene::AnimatorConditionMode::IntLess: return "<";
    case kb::scene::AnimatorConditionMode::FloatGreater: return ">";
    case kb::scene::AnimatorConditionMode::FloatLess: return "<";
    case kb::scene::AnimatorConditionMode::TriggerSet: return "set";
    }
    return "?";
}

void DrawText(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize, int weight = FW_NORMAL,
    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    const ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, flags | DT_NOPREFIX);
}

void PaintGraph(HDC dc, const RECT& rect, const kb::scene::AnimatorController* controller) {
    GdiDrawing::FillRectColor(dc, rect, RGB(29, 32, 37));
    GdiDrawing::DrawSharpFrame(dc, rect, RGB(29, 32, 37), RGB(56, 61, 69));
    DrawText(dc, RECT{ rect.left + 10, rect.top + 4, rect.right - 10, rect.top + 28 }, "State Machine", RGB(204, 212, 221), 12, FW_SEMIBOLD);
    if (controller == nullptr) return;

    const int nodeWidth = std::clamp((static_cast<int>(rect.right) - static_cast<int>(rect.left)) / 4, 104, 168);
    const int nodeHeight = 44;
    const int layerHeight = std::max(nodeHeight + 36, (static_cast<int>(rect.bottom) - static_cast<int>(rect.top) - 30) /
        std::max(1, static_cast<int>(controller->layers.size())));
    for (std::size_t layerIndex = 0U; layerIndex < controller->layers.size(); ++layerIndex) {
        const kb::scene::AnimatorControllerLayer& layer = controller->layers[layerIndex];
        const int rowTop = rect.top + 30 + static_cast<int>(layerIndex) * layerHeight;
        if (rowTop >= rect.bottom) break;
        DrawText(dc, RECT{ rect.left + 10, rowTop, rect.right - 10, rowTop + 18 }, layer.name.c_str(), RGB(130, 167, 205), 11, FW_SEMIBOLD);
        constexpr int entryWidth = 48;
        const RECT entry{ rect.left + 12, rowTop + 22, rect.left + 12 + entryWidth, rowTop + 22 + nodeHeight };
        GdiDrawing::DrawSharpFrame(dc, entry, RGB(57, 67, 48), RGB(132, 172, 106));
        DrawText(dc, RECT{ entry.left + 4, entry.top + 3, entry.right - 4, entry.bottom - 3 }, "Entry", RGB(218, 233, 205), 10, FW_SEMIBOLD);
        for (std::size_t stateIndex = 0U; stateIndex < layer.states.size(); ++stateIndex) {
            const kb::scene::AnimatorControllerState& state = layer.states[stateIndex];
            const auto layout = std::ranges::find_if(controller->graphLayout, [&state](const auto& value) {
                return value.stateId == state.id;
            });
            const int left = rect.left + entryWidth + 30 +
                (layout == controller->graphLayout.end() ? static_cast<int>(stateIndex) * (nodeWidth + 22) : layout->positionX);
            if (left >= rect.right - 8) break;
            const RECT node{ left, rowTop + 22, std::min(static_cast<int>(rect.right) - 8, left + nodeWidth), rowTop + 22 + nodeHeight };
            const bool defaultState = state.name == layer.defaultState;
            GdiDrawing::DrawSharpFrame(dc, node, defaultState ? RGB(42, 72, 91) : RGB(42, 46, 53),
                defaultState ? RGB(90, 156, 210) : RGB(78, 84, 93));
            DrawText(dc, RECT{ node.left + 7, node.top + 3, node.right - 7, node.bottom - 3 },
                state.name.c_str(), RGB(224, 230, 237), 11, FW_SEMIBOLD);
            if (defaultState) {
                const int middle = node.top + (node.bottom - node.top) / 2;
                GdiDrawing::FillRectColor(dc, RECT{ entry.right, middle, node.left, middle + 2 }, RGB(132, 172, 106));
            }
        }
        int transitionY = rowTop + 22 + nodeHeight + 4;
        for (const kb::scene::AnimatorControllerTransition& transition : layer.transitions) {
            if (transitionY + 14 >= rowTop + layerHeight || transitionY + 14 >= rect.bottom) break;
            const std::string text = transition.fromState + " -> " + transition.toState +
                "  (" + std::to_string(transition.durationSeconds) + "s)";
            DrawText(dc, RECT{ rect.left + 18, transitionY, rect.right - 10, transitionY + 14 }, text.c_str(), RGB(203, 173, 99), 10);
            transitionY += 14;
        }
    }
}

void PaintDetails(HDC dc, const RECT& rect, const kb::scene::AnimatorController* controller) {
    GdiDrawing::FillRectColor(dc, rect, RGB(27, 29, 33));
    GdiDrawing::DrawSharpFrame(dc, rect, RGB(27, 29, 33), RGB(56, 61, 69));
    DrawText(dc, RECT{ rect.left + 10, rect.top + 4, rect.right - 10, rect.top + 28 }, "Details / Assets", RGB(204, 212, 221), 12, FW_SEMIBOLD);
    if (controller == nullptr) return;
    int y = rect.top + 36;
    const auto row = [&](const std::string& label) {
        DrawText(dc, RECT{ rect.left + 10, y, rect.right - 10, y + 19 }, label.c_str(), RGB(168, 178, 190), 11);
        y += 20;
    };
    row("Parameters: " + std::to_string(controller->parameters.size()));
    for (const kb::scene::AnimatorParameterDefinition& parameter : controller->parameters) {
        row(parameter.name + " : " + ParameterTypeLabel(parameter.type));
        if (y >= rect.bottom) return;
    }
    row("Layers: " + std::to_string(controller->layers.size()));
    for (const kb::scene::AnimatorControllerLayer& layer : controller->layers) {
        row(layer.name + " | Entry: " + layer.defaultState);
        if (y >= rect.bottom) return;
    }
    row("Constraints: " + std::to_string(controller->rigConstraints.size()));
    y += 5;
    DrawText(dc, RECT{ rect.left + 10, y, rect.right - 10, y + 19 }, "Transitions", RGB(130, 167, 205), 11, FW_SEMIBOLD);
    y += 21;
    for (const kb::scene::AnimatorControllerLayer& layer : controller->layers) {
        for (const kb::scene::AnimatorControllerTransition& transition : layer.transitions) {
            row(transition.fromState + " -> " + transition.toState + " | " +
                std::to_string(transition.durationSeconds) + " s");
            for (const kb::scene::AnimatorTransitionCondition& condition : transition.conditions) {
                row("  " + condition.parameter + " " + ConditionModeLabel(condition.mode));
            }
            if (y >= rect.bottom) return;
        }
    }
    DrawText(dc, RECT{ rect.left + 10, y, rect.right - 10, y + 19 }, "Referenced clips", RGB(130, 167, 205), 11, FW_SEMIBOLD);
    y += 21;
    for (const kb::scene::AnimatorControllerLayer& layer : controller->layers) {
        for (const kb::scene::AnimatorControllerState& state : layer.states) {
            const std::string reference = state.clipReference.empty() ? state.blendParameter : state.clipReference;
            if (!reference.empty()) row(reference);
            for (const kb::scene::AnimatorControllerState::BlendChild& child : state.blendChildren) {
                if (!child.clipReference.empty()) row(child.clipReference);
            }
            if (y >= rect.bottom) return;
        }
    }
}

} // namespace

void AnimatorEditorPanelRenderer::Paint(
    HDC dc,
    HWND host,
    const RECT& content,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport* sceneViewport) const {
    if (!sceneContext.HasAnimatorEditorAsset() || sceneContext.AnimatorEditorPreviewScene() == nullptr) {
        GdiDrawing::FillRectColor(dc, content, RGB(27, 29, 33));
        DrawText(dc, content, "Open an Animator Controller asset to begin editing.", RGB(168, 178, 190), 15,
            FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    GdiDrawing::FillRectColor(dc, RECT{ content.left, content.top, content.right, content.top + kHeaderHeight }, RGB(34, 37, 43));
    const kb::assets::AssetMetadata* metadata =
        sceneContext.Scene().Assets().Manager().Registry().Find(sceneContext.AnimatorEditorAssetId());
    const kb::assets::AssetMetadata* previewMesh =
        sceneContext.Scene().Assets().Manager().Registry().Find(sceneContext.AnimationPreview().SkeletalMeshAsset());
    const std::string title = (metadata == nullptr ? std::string{ "Animator Controller" } : metadata->name) +
        (previewMesh == nullptr ? std::string{} : "  |  Preview " + previewMesh->name);
    DrawText(dc, RECT{ content.left + 10, content.top, content.right - 10, content.top + kHeaderHeight },
        title.c_str(), RGB(219, 225, 233), 13, FW_SEMIBOLD);

    const AnimatorEditorPanelLayout layout = ResolveLayout(
        RECT{ content.left, content.top + kHeaderHeight, content.right, content.bottom });
    const kb::scene::AnimatorController* controller = sceneContext.AnimatorEditorController();
    GdiDrawing::FillRectColor(dc, layout.preview, RGB(20, 23, 28));
    PaintGraph(dc, layout.graph, controller);
    PaintDetails(dc, layout.details, controller);
    if (sceneViewport == nullptr) return;
    const std::uint64_t revision = sceneContext.AnimatorEditorPreviewRevision();
    EditorSceneBgfxViewport::PresentSettings settings{};
    settings.viewportKey = panel.id;
    settings.editorSceneOverlaysEnabled = false;
    settings.sceneRevision = revision;
    settings.sceneDirtyBaseRevision = revision;
    settings.sceneFullSyncRequired = false;
    settings.msaaSamples = renderBackendSettings.MsaaSamples();
    settings.shadowPassEnabled = renderBackendSettings.ShadowsEnabled();
    settings.postProcessEnabled = true;
    settings.selectionMaskEnabled = false;
    settings.selectionOutlineEnabled = false;
    settings.gpuDrivenRuntimeDispatchEnabled = renderBackendSettings.GpuDrivenEnabled();
    sceneViewport->Present(dc, host, layout.preview, *sceneContext.AnimatorEditorPreviewScene(), theme, settings);
}

} // namespace kb::editor

#endif
