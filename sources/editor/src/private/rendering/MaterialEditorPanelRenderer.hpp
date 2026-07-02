#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"
#include "inspection/MaterialAssetFormatter.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace kb::editor {

enum class MaterialEditorPanelCommand {
    None,
    Info,
    ApplyToSelection,
    Save,
    Revert,
    Validate,
};

inline constexpr std::array<MaterialEditorPanelCommand, 5U> kMaterialEditorPanelToolbarCommands{
    MaterialEditorPanelCommand::Info,
    MaterialEditorPanelCommand::ApplyToSelection,
    MaterialEditorPanelCommand::Save,
    MaterialEditorPanelCommand::Revert,
    MaterialEditorPanelCommand::Validate,
};

[[nodiscard]] inline constexpr const char* MaterialEditorPanelCommandName(MaterialEditorPanelCommand command) noexcept {
    switch (command) {
    case MaterialEditorPanelCommand::None:
        return "None";
    case MaterialEditorPanelCommand::Info:
        return "Info";
    case MaterialEditorPanelCommand::ApplyToSelection:
        return "Apply To Selection";
    case MaterialEditorPanelCommand::Save:
        return "Save";
    case MaterialEditorPanelCommand::Revert:
        return "Revert";
    case MaterialEditorPanelCommand::Validate:
        return "Validate";
    }
    return "None";
}

[[nodiscard]] inline bool ExecuteMaterialEditorPanelCommand(
    EditorSceneContext& sceneContext,
    kb::assets::AssetId materialId,
    MaterialEditorPanelCommand command) {
    switch (command) {
    case MaterialEditorPanelCommand::Info:
        return sceneContext.MaterialEditor().ToggleInfoPanel();
    case MaterialEditorPanelCommand::ApplyToSelection:
        return sceneContext.ApplyMaterialToSelectedMeshRenderers(materialId);
    case MaterialEditorPanelCommand::Save:
        return sceneContext.SaveMaterialEditorAsset(materialId);
    case MaterialEditorPanelCommand::Revert:
        return sceneContext.RevertMaterialEditorAsset(materialId);
    case MaterialEditorPanelCommand::Validate:
        return sceneContext.ValidateMaterialEditorAsset(materialId);
    case MaterialEditorPanelCommand::None:
        return false;
    }
    return false;
}

[[nodiscard]] inline constexpr bool MaterialEditorPanelCommandHasBackendAction(MaterialEditorPanelCommand command) noexcept {
    switch (command) {
    case MaterialEditorPanelCommand::Info:
    case MaterialEditorPanelCommand::ApplyToSelection:
    case MaterialEditorPanelCommand::Save:
    case MaterialEditorPanelCommand::Revert:
    case MaterialEditorPanelCommand::Validate:
        return true;
    case MaterialEditorPanelCommand::None:
        return false;
    }
    return false;
}

struct MaterialEditorPanelDiagnosticRows {
    std::vector<std::string> rows;
    bool hasError = false;
};

struct MaterialEditorPanelDetailsRows {
    std::string title;
    std::vector<MaterialEditorInstanceParentChainRow> instanceParentRows;
    std::vector<MaterialEditorInstanceOverrideGroupRow> instanceOverrideGroupRows;
    std::vector<MaterialEditorInstanceStaticSwitchRow> instanceStaticSwitchRows;
    std::vector<MaterialEditorLayerTreeRow> layerTreeRows;
    MaterialEditorMaterialStatsModel materialStats;
    MaterialEditorShaderViewerModel shaderViewer;
    std::vector<MaterialEditorFindResult> findResults;
    std::vector<MaterialEditorGraphNodeProperty> nodePropertyRows;
    std::vector<MaterialDebugChannelRow> debugChannelRows;
    std::vector<std::string> parameterRows;
    std::vector<std::string> textureSlotRows;
};

struct MaterialEditorPanelParameterHit {
    std::string stableId;
    std::string displayName;
    kb::render::RenderMaterialParameterType type = kb::render::RenderMaterialParameterType::Scalar;
    MaterialEditorParameterValue value{};
};

enum class MaterialEditorGraphNodePropertyHitKind : std::uint8_t {
    None,
    Slider,
    ColorPicker,
    EnumField,
    EnumOption,
    TexturePicker,
};

struct MaterialEditorGraphNodePropertyHit {
    MaterialEditorGraphNodePropertyHitKind kind = MaterialEditorGraphNodePropertyHitKind::None;
    std::uint32_t nodeId = 0U;
    std::string stableId;
    std::string displayName;
    MaterialEditorGraphNodePropertyKind propertyKind = MaterialEditorGraphNodePropertyKind::Numeric;
    kb::render::RenderMaterialParameterType type = kb::render::RenderMaterialParameterType::Scalar;
    MaterialEditorParameterValue value{};
    std::size_t componentIndex = 0U;
    std::string optionValue;
};

enum class MaterialEditorGraphPinDirection : std::uint8_t {
    Input,
    Output,
};

struct MaterialEditorGraphPinHit {
    std::uint32_t nodeId = 0U;
    MaterialEditorGraphPinDirection direction = MaterialEditorGraphPinDirection::Input;
    std::string pin;
    kb::render::RenderMaterialGraphPinType type = kb::render::RenderMaterialGraphPinType::Unknown;
};

enum class MaterialEditorGraphPinDragState : std::uint8_t {
    None,
    Source,
    Compatible,
    Incompatible,
};

struct MaterialEditorGraphLinkHit {
    std::uint32_t fromNodeId = 0U;
    std::string fromPin;
    std::uint32_t toNodeId = 0U;
    std::string toPin;
};

struct MaterialEditorGraphConstantValueHit {
    std::uint32_t nodeId = 0U;
    std::size_t componentIndex = 0U;
    std::string displayName;
    kb::render::RenderMaterialParameterType type = kb::render::RenderMaterialParameterType::Scalar;
    MaterialEditorParameterValue value{};
};

enum class MaterialEditorGraphContextMenuHitKind : std::uint8_t {
    None,
    Search,
    Category,
    Command,
    FavoriteToggle,
};

struct MaterialEditorGraphContextMenuHit {
    MaterialEditorGraphContextMenuHitKind kind = MaterialEditorGraphContextMenuHitKind::None;
    std::size_t categoryIndex = 0U;
    MaterialEditorGraphMenuCommand command = MaterialEditorGraphMenuCommand::None;
};

struct MaterialEditorPanelLayout {
#if defined(_WIN32)
    RECT applyButton{};
    RECT infoButton{};
    RECT saveButton{};
    RECT revertButton{};
    RECT validateButton{};
    RECT previewFrame{};
    RECT diagnosticsPanel{};
    RECT detailsPanel{};
    RECT graphCanvas{};
#endif
};

#if defined(_WIN32)
namespace MaterialEditorPanelMetrics {
inline constexpr int HeaderHeight = 42;
inline constexpr int Padding = 10;
inline constexpr int RowHeight = 18;
inline constexpr int PreviewWidth = 154;
inline constexpr int PreviewHeight = 104;
inline constexpr int GraphNodeWidth = 240;
inline constexpr int GraphNodeHeight = 160;
inline constexpr int GraphNodeHeaderHeight = 24;
inline constexpr int GraphNodeBodyTopPadding = 6;
inline constexpr int GraphNodePinRowHeight = 22;
inline constexpr int TextureSlotRowCount = 5;
inline constexpr int DetailsNodePropertyRowHeight = 22;
inline constexpr int DetailsNodePropertyOptionHeight = 20;
} // namespace MaterialEditorPanelMetrics
#endif

#if defined(_WIN32)
inline SIZE MaterialEditorPanelGraphNodeSize(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
        return SIZE{ 150, 54 };
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
        return SIZE{ 172, 72 };
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
        return SIZE{ 190, 92 };
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
        return SIZE{ 220, 92 };
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
    case kb::render::RenderMaterialGraphNodeKind::TextureObject:
        return SIZE{ 220, 118 };
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
        return SIZE{ 160, 66 };
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
        return SIZE{ 176, 72 };
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
        return SIZE{ 176, 72 };
    case kb::render::RenderMaterialGraphNodeKind::CollectionParameter:
        return SIZE{ 196, 112 };
    case kb::render::RenderMaterialGraphNodeKind::CustomCode:
        return SIZE{ 220, 88 };
    case kb::render::RenderMaterialGraphNodeKind::Reroute:
        return SIZE{ 116, 46 };
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage:
    case kb::render::RenderMaterialGraphNodeKind::CompositeInput:
    case kb::render::RenderMaterialGraphNodeKind::CompositeOutput:
        return SIZE{ 180, 58 };
    case kb::render::RenderMaterialGraphNodeKind::QualitySwitch:
        return SIZE{ 220, 128 };
    case kb::render::RenderMaterialGraphNodeKind::FeatureLevelSwitch:
        return SIZE{ 220, 106 };
    case kb::render::RenderMaterialGraphNodeKind::ShadingPathSwitch:
    case kb::render::RenderMaterialGraphNodeKind::ShaderStageSwitch:
        return SIZE{ 220, 86 };
    case kb::render::RenderMaterialGraphNodeKind::Uv:
        return SIZE{ 164, 52 };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return SIZE{ 420, 232 };
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return SIZE{ 250, 204 };
    case kb::render::RenderMaterialGraphNodeKind::Add:
    case kb::render::RenderMaterialGraphNodeKind::Subtract:
    case kb::render::RenderMaterialGraphNodeKind::Multiply:
    case kb::render::RenderMaterialGraphNodeKind::Divide:
    case kb::render::RenderMaterialGraphNodeKind::Minimum:
    case kb::render::RenderMaterialGraphNodeKind::Maximum:
    case kb::render::RenderMaterialGraphNodeKind::DotProduct:
    case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
    case kb::render::RenderMaterialGraphNodeKind::Distance:
    case kb::render::RenderMaterialGraphNodeKind::SphereMask:
    case kb::render::RenderMaterialGraphNodeKind::AppendVector:
        return SIZE{ 150, 62 };
    case kb::render::RenderMaterialGraphNodeKind::Power:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2:
    case kb::render::RenderMaterialGraphNodeKind::Desaturate:
        return SIZE{ 166, 62 };
    case kb::render::RenderMaterialGraphNodeKind::OneMinus:
    case kb::render::RenderMaterialGraphNodeKind::Absolute:
    case kb::render::RenderMaterialGraphNodeKind::Saturate:
    case kb::render::RenderMaterialGraphNodeKind::Floor:
    case kb::render::RenderMaterialGraphNodeKind::Ceil:
    case kb::render::RenderMaterialGraphNodeKind::Fraction:
    case kb::render::RenderMaterialGraphNodeKind::SquareRoot:
    case kb::render::RenderMaterialGraphNodeKind::Sine:
    case kb::render::RenderMaterialGraphNodeKind::Cosine:
    case kb::render::RenderMaterialGraphNodeKind::Exponential:
    case kb::render::RenderMaterialGraphNodeKind::Exponential2:
    case kb::render::RenderMaterialGraphNodeKind::Logarithm:
    case kb::render::RenderMaterialGraphNodeKind::Logarithm2:
    case kb::render::RenderMaterialGraphNodeKind::SrgbToLinear:
    case kb::render::RenderMaterialGraphNodeKind::LinearToSrgb:
    case kb::render::RenderMaterialGraphNodeKind::Logarithm10:
    case kb::render::RenderMaterialGraphNodeKind::HsvToRgb:
    case kb::render::RenderMaterialGraphNodeKind::RgbToHsv:
    case kb::render::RenderMaterialGraphNodeKind::DeriveNormalZ:
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeX:
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeY:
    case kb::render::RenderMaterialGraphNodeKind::BlackBody:
    case kb::render::RenderMaterialGraphNodeKind::Noise:
    case kb::render::RenderMaterialGraphNodeKind::VectorNoise:
    case kb::render::RenderMaterialGraphNodeKind::ColorRamp:
    case kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask:
    case kb::render::RenderMaterialGraphNodeKind::Transform:
    case kb::render::RenderMaterialGraphNodeKind::TransformPosition:
    case kb::render::RenderMaterialGraphNodeKind::Normalize:
    case kb::render::RenderMaterialGraphNodeKind::Length:
    case kb::render::RenderMaterialGraphNodeKind::Negate:
    case kb::render::RenderMaterialGraphNodeKind::Sign:
    case kb::render::RenderMaterialGraphNodeKind::Round:
    case kb::render::RenderMaterialGraphNodeKind::Truncate:
    case kb::render::RenderMaterialGraphNodeKind::Tangent:
    case kb::render::RenderMaterialGraphNodeKind::ArcSine:
    case kb::render::RenderMaterialGraphNodeKind::ArcCosine:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent:
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        return SIZE{ 142, 54 };
    case kb::render::RenderMaterialGraphNodeKind::Clamp:
    case kb::render::RenderMaterialGraphNodeKind::Lerp:
    case kb::render::RenderMaterialGraphNodeKind::SmoothStep:
    case kb::render::RenderMaterialGraphNodeKind::InverseLerp:
        return SIZE{ 168, 92 };
    case kb::render::RenderMaterialGraphNodeKind::MakeVector:
    case kb::render::RenderMaterialGraphNodeKind::Fresnel:
    case kb::render::RenderMaterialGraphNodeKind::BreakVector:
        return SIZE{ 180, 112 };
    case kb::render::RenderMaterialGraphNodeKind::Step:
        return SIZE{ 154, 68 };
    case kb::render::RenderMaterialGraphNodeKind::If:
        return SIZE{ 194, 142 };
    default:
        return SIZE{ MaterialEditorPanelMetrics::GraphNodeWidth, MaterialEditorPanelMetrics::GraphNodeHeight };
    }
}
#endif

/// Dedicated Material Editor panel. The selected material document is presented as
/// a full-tab graph workspace; compact overlays provide preview and identity context
/// without turning the tab back into an inspector-style property form.
class MaterialEditorPanelRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const;

    [[nodiscard]] static MaterialEditorPanelLayout ResolveLayout(const RECT& content) noexcept;

    /// Live material preview target rect (sphere) inside this panel's content area, or
    /// std::nullopt when no material document is selected. Consumed by the editor frame
    /// loop to present the bgfx preview into this panel (mirrors the Inspector preview path).
    [[nodiscard]] static std::optional<RECT> MaterialPreviewRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static MaterialEditorPanelCommand CommandAt(const RECT& content, int x, int y) noexcept;
    [[nodiscard]] static MaterialEditorPanelDiagnosticRows DiagnosticRows(const kb::render::RenderMaterialAssetParseResult& result);
    [[nodiscard]] static MaterialEditorPanelDetailsRows DetailsRows(const std::vector<MaterialEditorParameter>& parameters, std::uint32_t selectedNodeId);
    [[nodiscard]] static MaterialEditorPanelDetailsRows DetailsRows(
        const std::vector<MaterialEditorParameter>& parameters,
        std::uint32_t selectedNodeId,
        const std::vector<MaterialEditorGraphNodeProperty>& nodeProperties);
    [[nodiscard]] static std::optional<MaterialEditorGraphNodePropertyHit> GraphNodePropertyAt(
        const RECT& content,
        const std::vector<MaterialEditorGraphNodeProperty>& nodeProperties,
        int x,
        int y) noexcept;
    [[nodiscard]] static std::optional<MaterialEditorPanelParameterHit> ParameterAt(
        const RECT& content,
        const std::vector<MaterialEditorParameter>& parameters,
        const std::vector<MaterialEditorGraphNodeProperty>& nodeProperties,
        std::size_t debugRowCount,
        int x,
        int y) noexcept;
    [[nodiscard]] static std::optional<MaterialEditorPanelParameterHit> ParameterAt(
        const RECT& content,
        const std::vector<MaterialEditorParameter>& parameters,
        std::size_t debugRowCount,
        int x,
        int y) noexcept;
    [[nodiscard]] static std::optional<MaterialEditorPanelParameterHit> TextureParameterAt(
        const RECT& content,
        const std::vector<MaterialEditorParameter>& parameters,
        const std::vector<MaterialEditorGraphNodeProperty>& nodeProperties,
        std::size_t debugRowCount,
        int x,
        int y) noexcept;
    [[nodiscard]] static std::optional<EditorMaterialTextureSlot> TextureSlotAt(const RECT& content, int x, int y) noexcept;
    [[nodiscard]] static std::optional<EditorMaterialTextureSlot> TextureSlotAt(
        const RECT& content,
        const kb::render::RenderMaterialGraphDocument& graph,
        const EditorSceneContext& sceneContext,
        kb::assets::AssetId assetId,
        int x,
        int y) noexcept;
    [[nodiscard]] static std::optional<std::uint32_t> GraphNodeAt(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, int x, int y) noexcept;
    [[nodiscard]] static std::optional<std::uint32_t> GraphNodeAt(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, const EditorSceneContext& sceneContext, kb::assets::AssetId assetId, int x, int y) noexcept;
    [[nodiscard]] static std::optional<std::uint32_t> GraphCommentAt(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, const EditorSceneContext& sceneContext, kb::assets::AssetId assetId, int x, int y) noexcept;
    [[nodiscard]] static std::vector<std::uint32_t> GraphNodeIdsInRect(
        const RECT& content,
        const kb::render::RenderMaterialGraphDocument& graph,
        const EditorSceneContext& sceneContext,
        kb::assets::AssetId assetId,
        const RECT& selectionRect);
    [[nodiscard]] static std::optional<MaterialEditorGraphPinHit> GraphPinAt(
        const RECT& content,
        const kb::render::RenderMaterialGraphDocument& graph,
        int x,
        int y) noexcept;
    [[nodiscard]] static std::optional<MaterialEditorGraphPinHit> GraphPinAt(
        const RECT& content,
        const kb::render::RenderMaterialGraphDocument& graph,
        const EditorSceneContext& sceneContext,
        kb::assets::AssetId assetId,
        int x,
        int y);
    [[nodiscard]] static MaterialEditorGraphPinDragState GraphPinDragState(
        const kb::render::RenderMaterialGraphDocument& graph,
        std::uint32_t pendingNodeId,
        std::string_view pendingPin,
        bool pendingOutputPin,
        std::uint32_t candidateNodeId,
        std::string_view candidatePin,
        bool candidateOutputPin) noexcept;
    [[nodiscard]] static std::optional<MaterialEditorGraphLinkHit> GraphLinkAt(
        const RECT& content,
        const kb::render::RenderMaterialGraphDocument& graph,
        const EditorSceneContext& sceneContext,
        kb::assets::AssetId assetId,
        int x,
        int y) noexcept;
    [[nodiscard]] static std::optional<std::uint32_t> GraphTextureSampleAt(
        const RECT& content,
        const kb::render::RenderMaterialGraphDocument& graph,
        const EditorSceneContext& sceneContext,
        kb::assets::AssetId assetId,
        int x,
        int y) noexcept;
    [[nodiscard]] static std::optional<MaterialEditorGraphConstantValueHit> GraphConstantValueAt(
        const RECT& content,
        const kb::render::RenderMaterialGraphDocument& graph,
        const EditorSceneContext& sceneContext,
        kb::assets::AssetId assetId,
        int x,
        int y) noexcept;
    [[nodiscard]] static RECT GraphContextMenuRect(const EditorSceneContext& sceneContext);
    [[nodiscard]] static MaterialEditorGraphContextMenuHit GraphContextMenuHit(const EditorSceneContext& sceneContext, int x, int y);
    [[nodiscard]] static std::optional<RECT> GraphNodeRect(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, std::uint32_t nodeId) noexcept;
    [[nodiscard]] static std::optional<RECT> GraphNodeRect(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, std::uint32_t nodeId, const EditorSceneContext& sceneContext, kb::assets::AssetId assetId) noexcept;
    [[nodiscard]] static std::optional<RECT> GraphCommentRect(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, std::uint32_t commentId, const EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static std::optional<RECT> GraphCompositeRect(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, std::uint32_t compositeId, const EditorSceneContext& sceneContext) noexcept;
#endif
};

#if defined(_WIN32)
inline bool MaterialEditorPanelPointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

inline int MaterialEditorPanelRectWidth(const RECT& rect) noexcept {
    return std::max(0, static_cast<int>(rect.right - rect.left));
}

inline int MaterialEditorPanelRectHeight(const RECT& rect) noexcept {
    return std::max(0, static_cast<int>(rect.bottom - rect.top));
}

inline int MaterialEditorPanelScaled(int value, float zoom) noexcept {
    return std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * zoom)));
}

inline MaterialEditorPanelLayout MaterialEditorPanelRenderer::ResolveLayout(const RECT& content) noexcept {
    MaterialEditorPanelLayout layout{};
    const int buttonTop = content.top + 8;
    const int buttonBottom = buttonTop + 26;
    const int buttonGap = 6;
    layout.validateButton = RECT{ content.right - MaterialEditorPanelMetrics::Padding - 72, buttonTop, content.right - MaterialEditorPanelMetrics::Padding, buttonBottom };
    layout.revertButton = RECT{ layout.validateButton.left - buttonGap - 62, buttonTop, layout.validateButton.left - buttonGap, buttonBottom };
    layout.saveButton = RECT{ layout.revertButton.left - buttonGap - 54, buttonTop, layout.revertButton.left - buttonGap, buttonBottom };
    layout.applyButton = RECT{ layout.saveButton.left - buttonGap - 118, buttonTop, layout.saveButton.left - buttonGap, buttonBottom };
    layout.infoButton = RECT{ layout.applyButton.left - buttonGap - 54, buttonTop, layout.applyButton.left - buttonGap, buttonBottom };

    layout.graphCanvas = RECT{
        content.left,
        content.top + MaterialEditorPanelMetrics::HeaderHeight,
        content.right,
        content.bottom,
    };
    const int overlayLeft = layout.graphCanvas.left + MaterialEditorPanelMetrics::Padding;
    const int overlayTop = layout.graphCanvas.top + MaterialEditorPanelMetrics::Padding;
    layout.previewFrame = RECT{
        overlayLeft,
        overlayTop,
        overlayLeft + MaterialEditorPanelMetrics::PreviewWidth,
        overlayTop + MaterialEditorPanelMetrics::PreviewHeight,
    };
    layout.diagnosticsPanel = RECT{
        std::max(layout.previewFrame.right + MaterialEditorPanelMetrics::Padding, layout.graphCanvas.right - 372),
        overlayTop,
        layout.graphCanvas.right - MaterialEditorPanelMetrics::Padding,
        std::min(static_cast<int>(layout.graphCanvas.bottom - MaterialEditorPanelMetrics::Padding), overlayTop + 132),
    };
    layout.detailsPanel = RECT{
        layout.diagnosticsPanel.left,
        layout.diagnosticsPanel.bottom + MaterialEditorPanelMetrics::Padding,
        layout.diagnosticsPanel.right,
        layout.graphCanvas.bottom - MaterialEditorPanelMetrics::Padding,
    };
    return layout;
}

inline MaterialEditorPanelCommand MaterialEditorPanelRenderer::CommandAt(const RECT& content, int x, int y) noexcept {
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    if (MaterialEditorPanelPointInRect(layout.infoButton, x, y)) {
        return MaterialEditorPanelCommand::Info;
    }
    if (MaterialEditorPanelPointInRect(layout.applyButton, x, y)) {
        return MaterialEditorPanelCommand::ApplyToSelection;
    }
    if (MaterialEditorPanelPointInRect(layout.saveButton, x, y)) {
        return MaterialEditorPanelCommand::Save;
    }
    if (MaterialEditorPanelPointInRect(layout.revertButton, x, y)) {
        return MaterialEditorPanelCommand::Revert;
    }
    if (MaterialEditorPanelPointInRect(layout.validateButton, x, y)) {
        return MaterialEditorPanelCommand::Validate;
    }
    return MaterialEditorPanelCommand::None;
}

inline MaterialEditorPanelDiagnosticRows MaterialEditorPanelRenderer::DiagnosticRows(const kb::render::RenderMaterialAssetParseResult& result) {
    MaterialEditorPanelDiagnosticRows rows;
    for (const kb::render::RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        rows.hasError = rows.hasError || diagnostic.severity == kb::render::RenderMaterialAssetParseDiagnosticSeverity::Error;
        std::ostringstream line;
        line << (diagnostic.severity == kb::render::RenderMaterialAssetParseDiagnosticSeverity::Error ? "Error" : "Warning")
             << " " << kb::render::RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code);
        if (diagnostic.line > 0U) {
            line << " line " << diagnostic.line;
        }
        if (!diagnostic.field.empty()) {
            line << " " << diagnostic.field;
        }
        line << ": " << diagnostic.message;
        if (!diagnostic.text.empty()) {
            line << " [" << diagnostic.text << "]";
        }
        rows.rows.push_back(line.str());
    }
    return rows;
}

inline std::string MaterialEditorPanelParameterTypeName(kb::render::RenderMaterialParameterType type) {
    switch (type) {
    case kb::render::RenderMaterialParameterType::Scalar:
        return "Scalar";
    case kb::render::RenderMaterialParameterType::Vec3:
        return "Vec3";
    case kb::render::RenderMaterialParameterType::Vec4:
        return "Vec4";
    case kb::render::RenderMaterialParameterType::Color:
        return "Color";
    case kb::render::RenderMaterialParameterType::Enum:
        return "Enum";
    case kb::render::RenderMaterialParameterType::Bool:
        return "Bool";
    case kb::render::RenderMaterialParameterType::Texture:
        return "Texture";
    }
    return "Value";
}

inline std::string MaterialEditorPanelColorSpaceName(kb::render::RenderMaterialTextureColorSpace colorSpace) {
    switch (colorSpace) {
    case kb::render::RenderMaterialTextureColorSpace::Srgb:
        return "sRGB";
    case kb::render::RenderMaterialTextureColorSpace::Linear:
        return "Linear";
    case kb::render::RenderMaterialTextureColorSpace::Unknown:
        return "Any";
    }
    return "Any";
}

inline std::string MaterialEditorPanelParameterGroupName(MaterialEditorParameterGroup group) {
    switch (group) {
    case MaterialEditorParameterGroup::Core:
        return "Core";
    case MaterialEditorParameterGroup::Surface:
        return "Surface";
    case MaterialEditorParameterGroup::Texture:
        return "Texture";
    case MaterialEditorParameterGroup::Advanced:
        return "Advanced";
    }
    return "Advanced";
}

inline std::string MaterialEditorPanelFloat(float value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(3) << value;
    std::string text = output.str();
    while (text.size() > 1U && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

inline std::string MaterialEditorPanelParameterValueText(const MaterialEditorParameterValue& value) {
    switch (value.kind) {
    case MaterialEditorParameterValueKind::Scalar:
        return MaterialEditorPanelFloat(value.numbers[0]);
    case MaterialEditorParameterValueKind::Vec2:
        return MaterialEditorPanelFloat(value.numbers[0]) + ", " + MaterialEditorPanelFloat(value.numbers[1]);
    case MaterialEditorParameterValueKind::Vec3:
    case MaterialEditorParameterValueKind::Color:
        return MaterialEditorPanelFloat(value.numbers[0]) + ", " + MaterialEditorPanelFloat(value.numbers[1]) + ", " + MaterialEditorPanelFloat(value.numbers[2]);
    case MaterialEditorParameterValueKind::Vec4:
        return MaterialEditorPanelFloat(value.numbers[0]) + ", " + MaterialEditorPanelFloat(value.numbers[1]) + ", " + MaterialEditorPanelFloat(value.numbers[2]) + ", " + MaterialEditorPanelFloat(value.numbers[3]);
    case MaterialEditorParameterValueKind::Enum:
        return value.text;
    case MaterialEditorParameterValueKind::Bool:
        return value.boolValue ? "true" : "false";
    case MaterialEditorParameterValueKind::TextureAsset:
        return value.assetId == 0U ? "None" : std::to_string(value.assetId);
    case MaterialEditorParameterValueKind::None:
        return "";
    }
    return "";
}

inline MaterialEditorPanelDetailsRows MaterialEditorPanelRenderer::DetailsRows(const std::vector<MaterialEditorParameter>& parameters, std::uint32_t selectedNodeId) {
    return DetailsRows(parameters, selectedNodeId, {});
}

inline MaterialEditorPanelDetailsRows MaterialEditorPanelRenderer::DetailsRows(
    const std::vector<MaterialEditorParameter>& parameters,
    std::uint32_t selectedNodeId,
    const std::vector<MaterialEditorGraphNodeProperty>& nodeProperties) {
    MaterialEditorPanelDetailsRows rows;
    rows.title = selectedNodeId == 0U ? "PBR Parameters" : "Selected Node #" + std::to_string(selectedNodeId);
    rows.nodePropertyRows = nodeProperties;
    for (const MaterialEditorParameter& parameter : parameters) {
        if (parameter.type == kb::render::RenderMaterialParameterType::Texture) {
            continue;
        }
        std::string row{ MaterialEditorPanelParameterGroupName(parameter.group) };
        row += "  ";
        row += MaterialEditorPanelParameterTypeName(parameter.type);
        row += "  ";
        row += parameter.displayName.empty() ? parameter.stableId : parameter.displayName;
        const std::string value = MaterialEditorPanelParameterValueText(parameter.value);
        if (!value.empty()) {
            row += " = ";
            row += value;
        }
        if (parameter.range.has_value()) {
            row += " ";
            row += MaterialEditorPanelFloat(parameter.range->min);
            row += "..";
            row += MaterialEditorPanelFloat(parameter.range->max);
        }
        const std::string defaultValue = MaterialEditorPanelParameterValueText(parameter.defaultValue);
        if (!defaultValue.empty()) {
            row += " default ";
            row += defaultValue;
        }
        row += parameter.overrideEnabled ? " override on" : " override disabled";
        if (parameter.overrideActive) {
            row += " instance override";
        }
        if (!parameter.enabled) {
            row += " disabled";
        }
        rows.parameterRows.push_back(std::move(row));
    }
    for (const MaterialEditorParameter& parameter : parameters) {
        if (parameter.type != kb::render::RenderMaterialParameterType::Texture) {
            continue;
        }
        std::string row{ MaterialEditorPanelParameterGroupName(parameter.group) };
        row += "  ";
        row += parameter.displayName.empty() ? parameter.stableId : parameter.displayName;
        if (parameter.expectedTextureColorSpace.has_value()) {
            row += "  ";
            row += MaterialEditorPanelColorSpaceName(*parameter.expectedTextureColorSpace);
        }
        row += "  ";
        row += parameter.stableId;
        row += " = ";
        row += MaterialEditorPanelParameterValueText(parameter.value);
        row += parameter.overrideEnabled ? " override on" : " override disabled";
        if (parameter.overrideActive) {
            row += " instance override";
        }
        if (!parameter.enabled) {
            row += " disabled";
        }
        rows.textureSlotRows.push_back(std::move(row));
    }
    return rows;
}

inline int MaterialEditorPanelAdvancePastNodeProperties(
    int rowY,
    int bottom,
    const std::vector<MaterialEditorGraphNodeProperty>& nodeProperties) noexcept {
    if (nodeProperties.empty()) {
        return rowY;
    }
    if (rowY + 20 <= bottom) {
        rowY += 22;
    }
    for (const MaterialEditorGraphNodeProperty& property : nodeProperties) {
        if (rowY + MaterialEditorPanelMetrics::DetailsNodePropertyRowHeight > bottom) {
            break;
        }
        rowY += MaterialEditorPanelMetrics::DetailsNodePropertyRowHeight;
        if (property.kind == MaterialEditorGraphNodePropertyKind::Enum && property.dropdownOpen) {
            const int optionCount = static_cast<int>(property.options.size());
            rowY += optionCount * MaterialEditorPanelMetrics::DetailsNodePropertyOptionHeight;
        }
    }
    return rowY + 6;
}

inline std::optional<MaterialEditorGraphNodePropertyHit> MaterialEditorPanelRenderer::GraphNodePropertyAt(
    const RECT& content,
    const std::vector<MaterialEditorGraphNodeProperty>& nodeProperties,
    int x,
    int y) noexcept {
    const MaterialEditorPanelLayout layout = ResolveLayout(content);
    if (nodeProperties.empty() ||
        !MaterialEditorPanelPointInRect(layout.detailsPanel, x, y) ||
        MaterialEditorPanelRectWidth(layout.detailsPanel) < 220 ||
        MaterialEditorPanelRectHeight(layout.detailsPanel) < 140) {
        return std::nullopt;
    }

    int rowY = layout.detailsPanel.top + 34;
    const int bottom = layout.detailsPanel.bottom - 10;
    if (rowY + 20 <= bottom) {
        rowY += 22;
    }
    for (const MaterialEditorGraphNodeProperty& property : nodeProperties) {
        if (rowY + MaterialEditorPanelMetrics::DetailsNodePropertyRowHeight > bottom) {
            break;
        }
        const RECT row{
            layout.detailsPanel.left + 12,
            rowY,
            layout.detailsPanel.right - 12,
            rowY + MaterialEditorPanelMetrics::DetailsNodePropertyRowHeight,
        };
        if (MaterialEditorPanelPointInRect(row, x, y) && property.enabled) {
            MaterialEditorGraphNodePropertyHit hit{
                .nodeId = property.nodeId,
                .stableId = property.stableId,
                .displayName = property.displayName,
                .propertyKind = property.kind,
                .type = property.type,
                .value = property.value,
                .componentIndex = property.componentIndex,
            };
            switch (property.kind) {
            case MaterialEditorGraphNodePropertyKind::Numeric:
                hit.kind = MaterialEditorGraphNodePropertyHitKind::Slider;
                break;
            case MaterialEditorGraphNodePropertyKind::Color:
                hit.kind = MaterialEditorGraphNodePropertyHitKind::ColorPicker;
                break;
            case MaterialEditorGraphNodePropertyKind::Enum:
                hit.kind = MaterialEditorGraphNodePropertyHitKind::EnumField;
                break;
            case MaterialEditorGraphNodePropertyKind::TextureAsset:
                hit.kind = MaterialEditorGraphNodePropertyHitKind::TexturePicker;
                break;
            }
            return hit;
        }
        rowY += MaterialEditorPanelMetrics::DetailsNodePropertyRowHeight;
        if (property.kind == MaterialEditorGraphNodePropertyKind::Enum && property.dropdownOpen) {
            for (const MaterialEditorGraphNodePropertyOption& option : property.options) {
                if (rowY + MaterialEditorPanelMetrics::DetailsNodePropertyOptionHeight > bottom) {
                    break;
                }
                const RECT optionRow{
                    layout.detailsPanel.left + 28,
                    rowY,
                    layout.detailsPanel.right - 18,
                    rowY + MaterialEditorPanelMetrics::DetailsNodePropertyOptionHeight,
                };
                if (MaterialEditorPanelPointInRect(optionRow, x, y) && property.enabled) {
                    return MaterialEditorGraphNodePropertyHit{
                        .kind = MaterialEditorGraphNodePropertyHitKind::EnumOption,
                        .nodeId = property.nodeId,
                        .stableId = property.stableId,
                        .displayName = property.displayName,
                        .propertyKind = property.kind,
                        .type = property.type,
                        .value = property.value,
                        .componentIndex = property.componentIndex,
                        .optionValue = option.value,
                    };
                }
                rowY += MaterialEditorPanelMetrics::DetailsNodePropertyOptionHeight;
            }
        }
    }
    return std::nullopt;
}

inline std::optional<MaterialEditorPanelParameterHit> MaterialEditorPanelRenderer::ParameterAt(
    const RECT& content,
    const std::vector<MaterialEditorParameter>& parameters,
    const std::vector<MaterialEditorGraphNodeProperty>& nodeProperties,
    std::size_t debugRowCount,
    int x,
    int y) noexcept {
    const MaterialEditorPanelLayout layout = ResolveLayout(content);
    if (!MaterialEditorPanelPointInRect(layout.detailsPanel, x, y) ||
        MaterialEditorPanelRectWidth(layout.detailsPanel) < 220 ||
        MaterialEditorPanelRectHeight(layout.detailsPanel) < 140) {
        return std::nullopt;
    }

    int rowY = layout.detailsPanel.top + 34;
    constexpr int rowHeight = 18;
    const int bottom = layout.detailsPanel.bottom - 10;
    rowY = MaterialEditorPanelAdvancePastNodeProperties(rowY, bottom, nodeProperties);
    if (debugRowCount > 0U) {
        rowY += 22;
        rowY += static_cast<int>(std::min<std::size_t>(debugRowCount, 6U)) * rowHeight;
        rowY += 6;
    }
    if (rowY + 20 <= bottom) {
        rowY += 22;
    }

    std::size_t visibleParameter = 0U;
    for (const MaterialEditorParameter& parameter : parameters) {
        if (parameter.type == kb::render::RenderMaterialParameterType::Texture) {
            continue;
        }
        if (rowY + rowHeight > bottom || visibleParameter >= 7U) {
            break;
        }
        const RECT row{
            layout.detailsPanel.left + 12,
            rowY,
            layout.detailsPanel.right - 12,
            rowY + rowHeight,
        };
        if (MaterialEditorPanelPointInRect(row, x, y) && parameter.enabled && parameter.overrideEnabled) {
            return MaterialEditorPanelParameterHit{
                .stableId = parameter.stableId,
                .displayName = parameter.displayName.empty() ? parameter.stableId : parameter.displayName,
                .type = parameter.type,
                .value = parameter.value,
            };
        }
        rowY += rowHeight;
        ++visibleParameter;
    }
    return std::nullopt;
}

inline std::optional<MaterialEditorPanelParameterHit> MaterialEditorPanelRenderer::ParameterAt(
    const RECT& content,
    const std::vector<MaterialEditorParameter>& parameters,
    std::size_t debugRowCount,
    int x,
    int y) noexcept {
    return ParameterAt(content, parameters, {}, debugRowCount, x, y);
}

inline std::optional<MaterialEditorPanelParameterHit> MaterialEditorPanelRenderer::TextureParameterAt(
    const RECT& content,
    const std::vector<MaterialEditorParameter>& parameters,
    const std::vector<MaterialEditorGraphNodeProperty>& nodeProperties,
    std::size_t debugRowCount,
    int x,
    int y) noexcept {
    const MaterialEditorPanelLayout layout = ResolveLayout(content);
    if (!MaterialEditorPanelPointInRect(layout.detailsPanel, x, y) ||
        MaterialEditorPanelRectWidth(layout.detailsPanel) < 220 ||
        MaterialEditorPanelRectHeight(layout.detailsPanel) < 140) {
        return std::nullopt;
    }

    int rowY = layout.detailsPanel.top + 34;
    constexpr int rowHeight = 18;
    const int bottom = layout.detailsPanel.bottom - 10;
    rowY = MaterialEditorPanelAdvancePastNodeProperties(rowY, bottom, nodeProperties);
    if (debugRowCount > 0U) {
        rowY += 22;
        rowY += static_cast<int>(std::min<std::size_t>(debugRowCount, 6U)) * rowHeight;
        rowY += 6;
    }
    if (rowY + 20 <= bottom) {
        rowY += 22;
    }

    std::size_t visibleParameter = 0U;
    for (const MaterialEditorParameter& parameter : parameters) {
        if (parameter.type == kb::render::RenderMaterialParameterType::Texture) {
            continue;
        }
        if (rowY + rowHeight > bottom || visibleParameter >= 7U) {
            break;
        }
        rowY += rowHeight;
        ++visibleParameter;
    }

    if (rowY + 28 <= bottom) {
        rowY += 6;
        rowY += 22;
    }

    std::size_t visibleTexture = 0U;
    for (const MaterialEditorParameter& parameter : parameters) {
        if (parameter.type != kb::render::RenderMaterialParameterType::Texture) {
            continue;
        }
        if (rowY + rowHeight > bottom || visibleTexture >= 6U) {
            break;
        }
        const RECT row{
            layout.detailsPanel.left + 12,
            rowY,
            layout.detailsPanel.right - 12,
            rowY + rowHeight,
        };
        if (MaterialEditorPanelPointInRect(row, x, y) && parameter.enabled && parameter.overrideEnabled) {
            return MaterialEditorPanelParameterHit{
                .stableId = parameter.stableId,
                .displayName = parameter.displayName.empty() ? parameter.stableId : parameter.displayName,
                .type = parameter.type,
                .value = parameter.value,
            };
        }
        rowY += rowHeight;
        ++visibleTexture;
    }
    return std::nullopt;
}

inline MaterialEditorGraphPinDragState MaterialEditorPanelRenderer::GraphPinDragState(
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t pendingNodeId,
    std::string_view pendingPin,
    bool pendingOutputPin,
    std::uint32_t candidateNodeId,
    std::string_view candidatePin,
    bool candidateOutputPin) noexcept {
    if (pendingNodeId == 0U || candidateNodeId == 0U || pendingPin.empty() || candidatePin.empty()) {
        return MaterialEditorGraphPinDragState::None;
    }
    if (pendingNodeId == candidateNodeId && pendingPin == candidatePin && pendingOutputPin == candidateOutputPin) {
        return MaterialEditorGraphPinDragState::Source;
    }
    if (pendingOutputPin == candidateOutputPin) {
        return MaterialEditorGraphPinDragState::None;
    }

    const kb::render::RenderMaterialGraphNode* pendingNode = kb::render::FindRenderMaterialGraphNode(graph, pendingNodeId);
    const kb::render::RenderMaterialGraphNode* candidateNode = kb::render::FindRenderMaterialGraphNode(graph, candidateNodeId);
    if (pendingNode == nullptr || candidateNode == nullptr) {
        return MaterialEditorGraphPinDragState::None;
    }
    if (pendingNodeId == candidateNodeId) {
        return MaterialEditorGraphPinDragState::Incompatible;
    }

    const kb::render::RenderMaterialGraphNode* fromNode = pendingOutputPin ? pendingNode : candidateNode;
    const kb::render::RenderMaterialGraphNode* toNode = pendingOutputPin ? candidateNode : pendingNode;
    const std::string_view fromPin = pendingOutputPin ? pendingPin : candidatePin;
    const std::string_view toPin = pendingOutputPin ? candidatePin : pendingPin;
    if (!kb::render::IsRenderMaterialGraphOutputPin(*fromNode, fromPin) ||
        !kb::render::IsRenderMaterialGraphInputPin(*toNode, toPin)) {
        return MaterialEditorGraphPinDragState::Incompatible;
    }
    return kb::render::AreRenderMaterialGraphPinsCompatible(*fromNode, fromPin, *toNode, toPin)
        ? MaterialEditorGraphPinDragState::Compatible
        : MaterialEditorGraphPinDragState::Incompatible;
}

inline std::vector<std::string> MaterialEditorPanelInputPins(kb::render::RenderMaterialGraphNodeKind kind) {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return { "baseColor", "normal", "roughness", "metallic", "emissive", "occlusion", "alpha", "worldPositionOffset", "customizedUv0", "displacement" };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return { "texture", "uv" };
    case kb::render::RenderMaterialGraphNodeKind::Add:
    case kb::render::RenderMaterialGraphNodeKind::Subtract:
    case kb::render::RenderMaterialGraphNodeKind::Multiply:
    case kb::render::RenderMaterialGraphNodeKind::Divide:
    case kb::render::RenderMaterialGraphNodeKind::Minimum:
    case kb::render::RenderMaterialGraphNodeKind::Maximum:
    case kb::render::RenderMaterialGraphNodeKind::DotProduct:
    case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
    case kb::render::RenderMaterialGraphNodeKind::Distance:
        return { "a", "b" };
    case kb::render::RenderMaterialGraphNodeKind::Power:
        return { "base", "exponent" };
    case kb::render::RenderMaterialGraphNodeKind::OneMinus:
    case kb::render::RenderMaterialGraphNodeKind::Absolute:
    case kb::render::RenderMaterialGraphNodeKind::Saturate:
    case kb::render::RenderMaterialGraphNodeKind::Floor:
    case kb::render::RenderMaterialGraphNodeKind::Ceil:
    case kb::render::RenderMaterialGraphNodeKind::Fraction:
    case kb::render::RenderMaterialGraphNodeKind::SquareRoot:
    case kb::render::RenderMaterialGraphNodeKind::Sine:
    case kb::render::RenderMaterialGraphNodeKind::Cosine:
    case kb::render::RenderMaterialGraphNodeKind::Normalize:
    case kb::render::RenderMaterialGraphNodeKind::Length:
    case kb::render::RenderMaterialGraphNodeKind::BreakVector:
        return { "value" };
    case kb::render::RenderMaterialGraphNodeKind::MakeVector:
        return { "x", "y", "z", "w" };
    case kb::render::RenderMaterialGraphNodeKind::Step:
        return { "edge", "value" };
    case kb::render::RenderMaterialGraphNodeKind::SmoothStep:
        return { "min", "max", "value" };
    case kb::render::RenderMaterialGraphNodeKind::If:
        return { "a", "b", "less", "equal", "greater" };
    case kb::render::RenderMaterialGraphNodeKind::Desaturate:
        return { "color", "fraction" };
    case kb::render::RenderMaterialGraphNodeKind::Fresnel:
        return { "normal", "view", "exponent", "base" };
    case kb::render::RenderMaterialGraphNodeKind::Negate:
    case kb::render::RenderMaterialGraphNodeKind::Sign:
    case kb::render::RenderMaterialGraphNodeKind::Round:
    case kb::render::RenderMaterialGraphNodeKind::Truncate:
    case kb::render::RenderMaterialGraphNodeKind::Tangent:
    case kb::render::RenderMaterialGraphNodeKind::ArcSine:
    case kb::render::RenderMaterialGraphNodeKind::ArcCosine:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent:
        return { "value" };
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2:
        return { "y", "x" };
    case kb::render::RenderMaterialGraphNodeKind::Clamp:
        return { "value", "min", "max" };
    case kb::render::RenderMaterialGraphNodeKind::Lerp:
        return { "a", "b", "t" };
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        return { "color" };
    case kb::render::RenderMaterialGraphNodeKind::Uv:
        return {};
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return {};
    default:
        break;
    }
    // Hit-testing falls back to the renderer's authoritative pin schema for any node not listed above,
    // so every node's pins are connectable (not just drawn).
    return kb::render::RenderMaterialGraphNodeInputPinNames(kind);
}

inline std::vector<std::string> MaterialEditorPanelOutputPins(kb::render::RenderMaterialGraphNodeKind kind) {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
    case kb::render::RenderMaterialGraphNodeKind::Add:
    case kb::render::RenderMaterialGraphNodeKind::Subtract:
    case kb::render::RenderMaterialGraphNodeKind::Multiply:
    case kb::render::RenderMaterialGraphNodeKind::Divide:
    case kb::render::RenderMaterialGraphNodeKind::Power:
    case kb::render::RenderMaterialGraphNodeKind::OneMinus:
    case kb::render::RenderMaterialGraphNodeKind::Absolute:
    case kb::render::RenderMaterialGraphNodeKind::Minimum:
    case kb::render::RenderMaterialGraphNodeKind::Maximum:
    case kb::render::RenderMaterialGraphNodeKind::Saturate:
    case kb::render::RenderMaterialGraphNodeKind::Floor:
    case kb::render::RenderMaterialGraphNodeKind::Ceil:
    case kb::render::RenderMaterialGraphNodeKind::Fraction:
    case kb::render::RenderMaterialGraphNodeKind::SquareRoot:
    case kb::render::RenderMaterialGraphNodeKind::Sine:
    case kb::render::RenderMaterialGraphNodeKind::Cosine:
    case kb::render::RenderMaterialGraphNodeKind::DotProduct:
    case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
    case kb::render::RenderMaterialGraphNodeKind::Normalize:
    case kb::render::RenderMaterialGraphNodeKind::Length:
    case kb::render::RenderMaterialGraphNodeKind::Distance:
    case kb::render::RenderMaterialGraphNodeKind::MakeVector:
    case kb::render::RenderMaterialGraphNodeKind::Step:
    case kb::render::RenderMaterialGraphNodeKind::SmoothStep:
    case kb::render::RenderMaterialGraphNodeKind::If:
    case kb::render::RenderMaterialGraphNodeKind::Fresnel:
    case kb::render::RenderMaterialGraphNodeKind::Negate:
    case kb::render::RenderMaterialGraphNodeKind::Sign:
    case kb::render::RenderMaterialGraphNodeKind::Round:
    case kb::render::RenderMaterialGraphNodeKind::Truncate:
    case kb::render::RenderMaterialGraphNodeKind::Tangent:
    case kb::render::RenderMaterialGraphNodeKind::ArcSine:
    case kb::render::RenderMaterialGraphNodeKind::ArcCosine:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2:
    case kb::render::RenderMaterialGraphNodeKind::Clamp:
    case kb::render::RenderMaterialGraphNodeKind::Lerp:
        return { "value" };
    case kb::render::RenderMaterialGraphNodeKind::Desaturate:
        return { "color" };
    case kb::render::RenderMaterialGraphNodeKind::BreakVector:
        return { "x", "y", "z", "w" };
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
        return { "xy" };
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
        return { "xyz" };
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
        return { "rgba" };
    case kb::render::RenderMaterialGraphNodeKind::CollectionParameter:
        return { "value", "scalar", "xyz", "rgba", "r", "g", "b", "a" };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return { "color", "r", "g", "b", "a" };
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return { "texture" };
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        return { "normal" };
    case kb::render::RenderMaterialGraphNodeKind::Uv:
        return { "uv" };
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return {};
    default:
        break;
    }
    return kb::render::RenderMaterialGraphNodeOutputPinNames(kind);
}

inline POINT MaterialEditorPanelInputPinPoint(const RECT& node, std::size_t index) noexcept {
    const float scale = static_cast<float>(MaterialEditorPanelRectWidth(node)) / static_cast<float>(std::max(1, MaterialEditorPanelMetrics::GraphNodeWidth));
    const int pinInset = MaterialEditorPanelScaled(6, scale);
    return POINT{
        node.left + pinInset,
        node.top
            + MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeHeaderHeight, scale)
            + MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeBodyTopPadding, scale)
            + (static_cast<int>(index) * MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodePinRowHeight, scale))
            + (MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodePinRowHeight, scale) / 2),
    };
}

inline POINT MaterialEditorPanelInputPinPoint(const RECT& node, kb::render::RenderMaterialGraphNodeKind kind, std::size_t index) noexcept {
    const SIZE graphNodeSize = MaterialEditorPanelGraphNodeSize(kind);
    const float scale = static_cast<float>(MaterialEditorPanelRectWidth(node)) / static_cast<float>(std::max<LONG>(1, graphNodeSize.cx));
    const int headerHeight = MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeHeaderHeight, scale);
    const int pinInset = MaterialEditorPanelScaled(6, scale);
    const int count = static_cast<int>(std::max<std::size_t>(1U, MaterialEditorPanelInputPins(kind).size()));
    const int bodyTop = node.top + headerHeight;
    const int bodyHeight = std::max(1, MaterialEditorPanelRectHeight(node) - headerHeight);
    const int rowHeight = std::min(
        MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodePinRowHeight, scale),
        std::max(MaterialEditorPanelScaled(16, scale), bodyHeight / count));
    const int total = count * rowHeight;
    const int bodyCenter = bodyTop + (bodyHeight / 2);
    const int y = bodyCenter - (total / 2) + (static_cast<int>(index) * rowHeight) + (rowHeight / 2);
    const int bottom = static_cast<int>(node.bottom);
    return POINT{
        node.left + pinInset,
        std::clamp(y, bodyTop + pinInset, bottom - pinInset),
    };
}

inline POINT MaterialEditorPanelOutputPinPoint(const RECT& node, std::size_t index, std::size_t count) noexcept {
    const float scale = static_cast<float>(MaterialEditorPanelRectWidth(node)) / static_cast<float>(std::max(1, MaterialEditorPanelMetrics::GraphNodeWidth));
    const int pinInset = MaterialEditorPanelScaled(6, scale);
    const int headerHeight = MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeHeaderHeight, scale);
    if (count <= 1U) {
        return POINT{ node.right - pinInset, node.top + headerHeight + ((MaterialEditorPanelRectHeight(node) - headerHeight) / 2) };
    }
    const int rowHeight = MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodePinRowHeight, scale);
    const int total = static_cast<int>(count) * rowHeight;
    const int top = static_cast<int>(node.top);
    const int bottom = static_cast<int>(node.bottom);
    return POINT{
        node.right - pinInset,
        std::clamp(
            top + headerHeight + ((MaterialEditorPanelRectHeight(node) - headerHeight) / 2) - (total / 2) + (static_cast<int>(index) * rowHeight) + (rowHeight / 2),
            top + headerHeight + pinInset,
            bottom - pinInset),
    };
}

inline POINT MaterialEditorPanelOutputPinPoint(const RECT& node, kb::render::RenderMaterialGraphNodeKind kind, std::size_t index, std::size_t count) noexcept {
    const SIZE graphNodeSize = MaterialEditorPanelGraphNodeSize(kind);
    const float scale = static_cast<float>(MaterialEditorPanelRectWidth(node)) / static_cast<float>(std::max<LONG>(1, graphNodeSize.cx));
    const int pinInset = MaterialEditorPanelScaled(6, scale);
    if (count <= 1U) {
        const int headerHeight = MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeHeaderHeight, scale);
        return POINT{ node.right - pinInset, node.top + headerHeight + ((MaterialEditorPanelRectHeight(node) - headerHeight) / 2) };
    }
    const int headerHeight = MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeHeaderHeight, scale);
    if (kind != kb::render::RenderMaterialGraphNodeKind::TextureSample) {
        const int bodyTop = node.top + headerHeight;
        const int bodyHeight = std::max(1, MaterialEditorPanelRectHeight(node) - headerHeight);
        const int rowHeight = std::min(
            MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodePinRowHeight, scale),
            std::max(MaterialEditorPanelScaled(16, scale), bodyHeight / static_cast<int>(count)));
        const int total = static_cast<int>(count) * rowHeight;
        const int bodyCenter = bodyTop + (bodyHeight / 2);
        const int y = bodyCenter - (total / 2) + (static_cast<int>(index) * rowHeight) + (rowHeight / 2);
        const int bottom = static_cast<int>(node.bottom);
        return POINT{ node.right - pinInset, std::clamp(y, bodyTop + pinInset, bottom - pinInset) };
    }
    const int previewTop = node.top + headerHeight + MaterialEditorPanelScaled(8, scale);
    const int previewBottom = node.bottom - MaterialEditorPanelScaled(10, scale);
    const int previewHeight = std::max(1, previewBottom - previewTop);
    const int rowHeight = std::min(
        MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodePinRowHeight, scale),
        std::max(MaterialEditorPanelScaled(16, scale), previewHeight / static_cast<int>(count)));
    const int total = static_cast<int>(count) * rowHeight;
    const int bodyCenter = previewTop + (previewHeight / 2);
    const int y = bodyCenter - (total / 2) + (static_cast<int>(index) * rowHeight) + (rowHeight / 2);
    return POINT{
        node.right - pinInset,
        std::clamp(y, previewTop + pinInset, previewBottom - pinInset),
    };
}

inline RECT MaterialEditorPanelTextureSamplePreviewRect(const RECT& node) noexcept {
    const SIZE graphNodeSize = MaterialEditorPanelGraphNodeSize(kb::render::RenderMaterialGraphNodeKind::TextureSample);
    const float scale = static_cast<float>(MaterialEditorPanelRectWidth(node)) / static_cast<float>(std::max<LONG>(1, graphNodeSize.cx));
    const int headerBottom = node.top + MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeHeaderHeight, scale);
    const int horizontalMargin = MaterialEditorPanelScaled(80, scale);
    const int left = node.left + horizontalMargin;
    const int right = node.right - horizontalMargin;
    const int top = headerBottom + MaterialEditorPanelScaled(8, scale);
    const int bottom = node.bottom - MaterialEditorPanelScaled(10, scale);
    return RECT{ left, top, std::max(left + MaterialEditorPanelScaled(96, scale), right), std::max(top + MaterialEditorPanelScaled(96, scale), bottom) };
}

inline bool MaterialEditorPanelIsConstantNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    return kind == kb::render::RenderMaterialGraphNodeKind::ConstantScalar ||
        kind == kb::render::RenderMaterialGraphNodeKind::ConstantBool ||
        kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector2 ||
        kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector ||
        kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor;
}

inline kb::render::RenderMaterialParameterType MaterialEditorPanelConstantParameterType(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
        return kb::render::RenderMaterialParameterType::Scalar;
    case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
        return kb::render::RenderMaterialParameterType::Bool;
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
        return kb::render::RenderMaterialParameterType::Vec4;
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
        return kb::render::RenderMaterialParameterType::Vec3;
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
        return kb::render::RenderMaterialParameterType::Color;
    default:
        return kb::render::RenderMaterialParameterType::Scalar;
    }
}

inline MaterialEditorParameterValue MaterialEditorPanelConstantParameterValue(
    kb::render::RenderMaterialGraphNodeKind kind,
    std::string_view defaultValueHint) {
    std::string normalized{ defaultValueHint };
    std::ranges::replace(normalized, ',', ' ');
    std::istringstream input{ normalized };

    MaterialEditorParameterValue value{};
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
        value.kind = MaterialEditorParameterValueKind::Scalar;
        if (!(input >> value.numbers[0])) {
            value.numbers[0] = 0.0F;
        }
        break;
    case kb::render::RenderMaterialGraphNodeKind::ConstantBool: {
        std::string boolText;
        input >> boolText;
        std::ranges::transform(boolText, boolText.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        value.kind = MaterialEditorParameterValueKind::Bool;
        value.boolValue = boolText == "true" || boolText == "1";
        value.numbers[0] = value.boolValue ? 1.0F : 0.0F;
        break;
    }
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
        value.kind = MaterialEditorParameterValueKind::Vec2;
        if (!(input >> value.numbers[0] >> value.numbers[1])) {
            value.numbers = { 0.0F, 0.0F, 0.0F, 0.0F };
        }
        break;
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
        value.kind = MaterialEditorParameterValueKind::Vec3;
        if (!(input >> value.numbers[0] >> value.numbers[1] >> value.numbers[2])) {
            value.numbers = { 0.0F, 0.0F, 0.0F, 0.0F };
        }
        break;
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
        value.kind = MaterialEditorParameterValueKind::Color;
        if (!(input >> value.numbers[0] >> value.numbers[1] >> value.numbers[2])) {
            value.numbers = { 1.0F, 1.0F, 1.0F, 1.0F };
        } else if (!(input >> value.numbers[3])) {
            value.numbers[3] = 1.0F;
        }
        break;
    default:
        break;
    }
    return value;
}

inline RECT MaterialEditorPanelConstantValueRect(const RECT& node) noexcept {
    const float scale = static_cast<float>(MaterialEditorPanelRectWidth(node)) / static_cast<float>(std::max(1, MaterialEditorPanelMetrics::GraphNodeWidth));
    const int headerHeight = MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeHeaderHeight, scale);
    const int bodyTop = node.top + headerHeight;
    const int bodyHeight = std::max(1, MaterialEditorPanelRectHeight(node) - headerHeight);
    const int padding = std::max(3, MaterialEditorPanelScaled(6, scale));
    const int fieldHeight = std::max(10, std::min(MaterialEditorPanelScaled(20, scale), bodyHeight - (padding * 2)));
    const int top = bodyTop + ((bodyHeight - fieldHeight) / 2);
    const int left = node.left + MaterialEditorPanelScaled(18, scale);
    const int right = node.right - MaterialEditorPanelScaled(48, scale);
    const int bottom = top + fieldHeight;
    return RECT{ left, top, right, bottom };
}

inline RECT MaterialEditorPanelConstantVectorFieldRect(const RECT& node, std::size_t componentIndex, std::size_t componentCount = 2U) noexcept {
    const float scale = static_cast<float>(MaterialEditorPanelRectWidth(node)) / static_cast<float>(std::max(1, MaterialEditorPanelMetrics::GraphNodeWidth));
    const int headerHeight = MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeHeaderHeight, scale);
    const int bodyTop = node.top + headerHeight;
    const int bodyHeight = std::max(1, MaterialEditorPanelRectHeight(node) - headerHeight);
    const int count = static_cast<int>(std::max<std::size_t>(1U, componentCount));
    const int padding = std::max(3, MaterialEditorPanelScaled(5, scale));
    const int gap = std::max(2, MaterialEditorPanelScaled(4, scale));
    const int maxFieldHeight = std::max(10, (bodyHeight - (padding * 2) - ((count - 1) * gap)) / count);
    const int fieldHeight = std::max(10, std::min(MaterialEditorPanelScaled(19, scale), maxFieldHeight));
    const int fieldsHeight = (count * fieldHeight) + ((count - 1) * gap);
    const int top = bodyTop + std::max(0, (bodyHeight - fieldsHeight) / 2)
        + static_cast<int>(componentIndex) * (fieldHeight + gap);
    const int left = node.left + MaterialEditorPanelScaled(56, scale);
    const int right = node.right - MaterialEditorPanelScaled(64, scale);
    const int bottom = top + fieldHeight;
    return RECT{ left, top, right, bottom };
}

inline RECT MaterialEditorPanelConstantVectorLabelRect(const RECT& node, std::size_t componentIndex, std::size_t componentCount = 2U) noexcept {
    const RECT field = MaterialEditorPanelConstantVectorFieldRect(node, componentIndex, componentCount);
    const float scale = static_cast<float>(MaterialEditorPanelRectWidth(node)) / static_cast<float>(std::max(1, MaterialEditorPanelMetrics::GraphNodeWidth));
    const int labelWidth = MaterialEditorPanelScaled(18, scale);
    const int gap = MaterialEditorPanelScaled(8, scale);
    return RECT{
        field.left - gap - labelWidth,
        field.top,
        field.left - gap,
        field.bottom,
    };
}

inline RECT MaterialEditorPanelConstantVectorFieldsBounds(const RECT& node, std::size_t componentCount) noexcept {
    if (componentCount == 0U) {
        return MaterialEditorPanelConstantValueRect(node);
    }
    RECT first = MaterialEditorPanelConstantVectorFieldRect(node, 0U, componentCount);
    RECT last = MaterialEditorPanelConstantVectorFieldRect(node, componentCount - 1U, componentCount);
    return RECT{ first.left, first.top, first.right, last.bottom };
}

inline RECT MaterialEditorPanelTextureParameterRect(const RECT& node) noexcept {
    const float scale = static_cast<float>(MaterialEditorPanelRectWidth(node)) / static_cast<float>(std::max(1, MaterialEditorPanelMetrics::GraphNodeWidth));
    const int left = node.left + MaterialEditorPanelScaled(26, scale);
    const int top = node.top
        + MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeHeaderHeight, scale)
        + MaterialEditorPanelScaled(54, scale);
    const int right = node.right - MaterialEditorPanelScaled(56, scale);
    const int bottom = top + MaterialEditorPanelScaled(92, scale);
    return RECT{ left, top, right, bottom };
}

inline std::uint32_t MaterialEditorPanelTextureValueNodeId(
    const kb::render::RenderMaterialGraphDocument& graph,
    const kb::render::RenderMaterialGraphNode& node) noexcept {
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture ||
        node.kind == kb::render::RenderMaterialGraphNodeKind::TextureObject) {
        return node.id;
    }
    if (node.kind != kb::render::RenderMaterialGraphNodeKind::TextureSample) {
        return 0U;
    }
    for (const kb::render::RenderMaterialGraphLink& link : graph.links) {
        if (link.toNodeId != node.id || link.toPin != "texture") {
            continue;
        }
        const kb::render::RenderMaterialGraphNode* source = kb::render::FindRenderMaterialGraphNode(graph, link.fromNodeId);
        if (source != nullptr && source->kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture && link.fromPin == "texture") {
            return source->id;
        }
    }
    return node.id;
}

inline bool MaterialEditorPanelPointNear(POINT point, int x, int y, int radius) noexcept {
    const int dx = x - point.x;
    const int dy = y - point.y;
    return (dx * dx) + (dy * dy) <= radius * radius;
}

inline std::optional<MaterialEditorGraphConstantValueHit> MaterialEditorPanelRenderer::GraphConstantValueAt(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId,
    int x,
    int y) noexcept {
    const kb::render::RenderMaterialGraphDocument defaultGraph = graph.nodes.empty()
        ? kb::render::MakeDefaultRenderMaterialGraphDocument()
        : kb::render::RenderMaterialGraphDocument{};
    const kb::render::RenderMaterialGraphDocument& graphView = graph.nodes.empty() ? defaultGraph : graph;
    for (std::size_t nodeIndex = graphView.nodes.size(); nodeIndex-- > 0U;) {
        const kb::render::RenderMaterialGraphNode& node = graphView.nodes[nodeIndex];
        if (!MaterialEditorPanelIsConstantNode(node.kind)) {
            continue;
        }
        if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantBool) {
            continue;
        }
        const std::optional<RECT> rect = GraphNodeRect(content, graphView, node.id, sceneContext, assetId);
        if (!rect.has_value()) {
            continue;
        }
        RECT hitRect = MaterialEditorPanelConstantValueRect(*rect);
        std::size_t componentIndex = 0U;
        if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector2) {
            hitRect = MaterialEditorPanelConstantVectorFieldsBounds(*rect, 2U);
            for (std::size_t index = 0U; index < 2U; ++index) {
                if (MaterialEditorPanelPointInRect(MaterialEditorPanelConstantVectorFieldRect(*rect, index, 2U), x, y)) {
                    componentIndex = index;
                    break;
                }
            }
        } else if (node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector) {
            hitRect = MaterialEditorPanelConstantVectorFieldsBounds(*rect, 3U);
            for (std::size_t index = 0U; index < 3U; ++index) {
                if (MaterialEditorPanelPointInRect(MaterialEditorPanelConstantVectorFieldRect(*rect, index, 3U), x, y)) {
                    componentIndex = index;
                    break;
                }
            }
        }
        if (MaterialEditorPanelPointInRect(hitRect, x, y)) {
            return MaterialEditorGraphConstantValueHit{
                .nodeId = node.id,
                .componentIndex = componentIndex,
                .displayName = node.parameter.displayName.empty() ? std::string{ "Constant" } : node.parameter.displayName,
                .type = MaterialEditorPanelConstantParameterType(node.kind),
                .value = MaterialEditorPanelConstantParameterValue(node.kind, node.parameter.defaultValueHint),
            };
        }
    }
    return std::nullopt;
}

inline std::optional<std::uint32_t> MaterialEditorPanelRenderer::GraphTextureSampleAt(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId,
    int x,
    int y) noexcept {
    const kb::render::RenderMaterialGraphDocument defaultGraph = graph.nodes.empty()
        ? kb::render::MakeDefaultRenderMaterialGraphDocument()
        : kb::render::RenderMaterialGraphDocument{};
    const kb::render::RenderMaterialGraphDocument& graphView = graph.nodes.empty() ? defaultGraph : graph;
    for (std::size_t nodeIndex = graphView.nodes.size(); nodeIndex-- > 0U;) {
        const kb::render::RenderMaterialGraphNode& node = graphView.nodes[nodeIndex];
        if (node.kind != kb::render::RenderMaterialGraphNodeKind::TextureSample &&
            node.kind != kb::render::RenderMaterialGraphNodeKind::ParameterTexture &&
            node.kind != kb::render::RenderMaterialGraphNodeKind::TextureObject) {
            continue;
        }
        const std::optional<RECT> rect = GraphNodeRect(content, graphView, node.id, sceneContext, assetId);
        if (!rect.has_value()) {
            continue;
        }
        if (node.kind == kb::render::RenderMaterialGraphNodeKind::TextureSample &&
            MaterialEditorPanelPointInRect(MaterialEditorPanelTextureSamplePreviewRect(*rect), x, y)) {
            const std::uint32_t textureNodeId = MaterialEditorPanelTextureValueNodeId(graphView, node);
            return textureNodeId == 0U ? node.id : textureNodeId;
        }
        if (node.kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture &&
            MaterialEditorPanelPointInRect(MaterialEditorPanelTextureParameterRect(*rect), x, y)) {
            return node.id;
        }
        if (node.kind == kb::render::RenderMaterialGraphNodeKind::TextureObject &&
            MaterialEditorPanelPointInRect(MaterialEditorPanelTextureParameterRect(*rect), x, y)) {
            return node.id;
        }
    }
    return std::nullopt;
}

inline std::optional<EditorMaterialTextureSlot> MaterialEditorPanelTextureSlotForOutputRow(std::size_t row) noexcept {
    switch (row) {
    case 0U:
        return EditorMaterialTextureSlot::Albedo;
    case 1U:
        return EditorMaterialTextureSlot::Normal;
    case 2U:
    case 3U:
        return EditorMaterialTextureSlot::MetallicRoughness;
    case 4U:
        return EditorMaterialTextureSlot::Emissive;
    case 5U:
        return EditorMaterialTextureSlot::Occlusion;
    default:
        return std::nullopt;
    }
}

inline std::optional<EditorMaterialTextureSlot> MaterialEditorPanelTextureSlotAtOutputNode(const RECT& node, int x, int y) noexcept {
    if (!MaterialEditorPanelPointInRect(node, x, y)) {
        return std::nullopt;
    }

    const float scale = static_cast<float>(MaterialEditorPanelRectWidth(node)) / static_cast<float>(std::max(1, MaterialEditorPanelMetrics::GraphNodeWidth));
    const int rowTop =
        node.top
        + MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeHeaderHeight, scale)
        + MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeBodyTopPadding, scale);
    const int rowHeight = MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodePinRowHeight, scale);
    const int relativeY = y - rowTop;
    if (relativeY < 0) {
        return std::nullopt;
    }

    const std::size_t row = static_cast<std::size_t>(relativeY / std::max(1, rowHeight));
    const RECT rowRect{
        node.left,
        rowTop + static_cast<int>(row) * rowHeight,
        node.right,
        rowTop + static_cast<int>(row + 1U) * rowHeight,
    };
    if (!MaterialEditorPanelPointInRect(rowRect, x, y)) {
        return std::nullopt;
    }
    return MaterialEditorPanelTextureSlotForOutputRow(row);
}

inline std::optional<EditorMaterialTextureSlot> MaterialEditorPanelRenderer::TextureSlotAt(const RECT& content, int x, int y) noexcept {
    const kb::render::RenderMaterialGraphDocument graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    for (std::size_t index = graph.nodes.size(); index-- > 0U;) {
        const kb::render::RenderMaterialGraphNode& node = graph.nodes[index];
        if (node.kind != kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            continue;
        }
        const std::optional<RECT> rect = GraphNodeRect(content, graph, node.id);
        if (!rect.has_value()) {
            continue;
        }
        if (const std::optional<EditorMaterialTextureSlot> slot = MaterialEditorPanelTextureSlotAtOutputNode(*rect, x, y)) {
            return slot;
        }
    }
    return std::nullopt;
}

inline std::optional<RECT> MaterialEditorPanelGraphNodeRectWithView(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    float zoom,
    int panX,
    int panY,
    int nodeOffsetX,
    int nodeOffsetY) noexcept {
    const kb::render::RenderMaterialGraphNode* target = kb::render::FindRenderMaterialGraphNode(graph, nodeId);
    if (target == nullptr) {
        return std::nullopt;
    }
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    const RECT canvas = layout.graphCanvas;
    const float clampedZoom = std::clamp(zoom, 0.25F, 2.0F);
    const SIZE graphNodeSize = MaterialEditorPanelGraphNodeSize(target->kind);
    const int nodeWidth = MaterialEditorPanelScaled(static_cast<int>(graphNodeSize.cx), clampedZoom);
    const int nodeHeight = MaterialEditorPanelScaled(static_cast<int>(graphNodeSize.cy), clampedZoom);

    const int x = canvas.left + panX + MaterialEditorPanelScaled(target->positionX + nodeOffsetX, clampedZoom);
    const int y = canvas.top + panY + MaterialEditorPanelScaled(target->positionY + nodeOffsetY, clampedZoom);
    return RECT{ x, y, x + nodeWidth, y + nodeHeight };
}

inline std::optional<RECT> MaterialEditorPanelRenderer::GraphNodeRect(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, std::uint32_t nodeId) noexcept {
    return MaterialEditorPanelGraphNodeRectWithView(content, graph, nodeId, 1.0F, 0, 0, 0, 0);
}

inline std::optional<RECT> MaterialEditorPanelRenderer::GraphNodeRect(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId) noexcept {
    return MaterialEditorPanelGraphNodeRectWithView(
        content,
        graph,
        nodeId,
        sceneContext.MaterialGraphZoom(),
        sceneContext.MaterialGraphPanX(),
        sceneContext.MaterialGraphPanY(),
        sceneContext.MaterialGraphNodeOffsetX(assetId, nodeId),
        sceneContext.MaterialGraphNodeOffsetY(assetId, nodeId));
}

inline std::optional<RECT> MaterialEditorPanelRenderer::GraphCommentRect(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t commentId,
    const EditorSceneContext& sceneContext) noexcept {
    const kb::render::RenderMaterialGraphCommentBox* target = nullptr;
    for (const kb::render::RenderMaterialGraphCommentBox& comment : graph.comments) {
        if (comment.id == commentId) {
            target = &comment;
            break;
        }
    }
    if (target == nullptr) {
        return std::nullopt;
    }
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    const float clampedZoom = std::clamp(sceneContext.MaterialGraphZoom(), 0.25F, 2.0F);
    const int x = layout.graphCanvas.left + sceneContext.MaterialGraphPanX() + MaterialEditorPanelScaled(target->positionX, clampedZoom);
    const int y = layout.graphCanvas.top + sceneContext.MaterialGraphPanY() + MaterialEditorPanelScaled(target->positionY, clampedZoom);
    const int width = MaterialEditorPanelScaled(std::max<std::int32_t>(32, target->width), clampedZoom);
    const int height = MaterialEditorPanelScaled(std::max<std::int32_t>(32, target->height), clampedZoom);
    return RECT{ x, y, x + width, y + height };
}

inline std::optional<RECT> MaterialEditorPanelRenderer::GraphCompositeRect(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t compositeId,
    const EditorSceneContext& sceneContext) noexcept {
    const kb::render::RenderMaterialGraphCompositeSubgraph* target = nullptr;
    for (const kb::render::RenderMaterialGraphCompositeSubgraph& composite : graph.composites) {
        if (composite.id == compositeId) {
            target = &composite;
            break;
        }
    }
    if (target == nullptr) {
        return std::nullopt;
    }
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    const float clampedZoom = std::clamp(sceneContext.MaterialGraphZoom(), 0.25F, 2.0F);
    const int x = layout.graphCanvas.left + sceneContext.MaterialGraphPanX() + MaterialEditorPanelScaled(target->positionX, clampedZoom);
    const int y = layout.graphCanvas.top + sceneContext.MaterialGraphPanY() + MaterialEditorPanelScaled(target->positionY, clampedZoom);
    const int width = MaterialEditorPanelScaled(std::max<std::int32_t>(64, target->width), clampedZoom);
    const int height = MaterialEditorPanelScaled(std::max<std::int32_t>(64, target->height), clampedZoom);
    return RECT{ x, y, x + width, y + height };
}

inline bool MaterialEditorPanelGraphNodeHiddenByCollapsedComposite(
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId) noexcept {
    for (const kb::render::RenderMaterialGraphCompositeSubgraph& composite : graph.composites) {
        if (!composite.collapsed) {
            continue;
        }
        if (std::find(composite.nodeIds.begin(), composite.nodeIds.end(), nodeId) != composite.nodeIds.end()) {
            return true;
        }
    }
    return false;
}

inline std::optional<std::uint32_t> MaterialEditorPanelRenderer::GraphNodeAt(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, int x, int y) noexcept {
    for (std::size_t index = graph.nodes.size(); index-- > 0U;) {
        const kb::render::RenderMaterialGraphNode& node = graph.nodes[index];
        if (MaterialEditorPanelGraphNodeHiddenByCollapsedComposite(graph, node.id)) {
            continue;
        }
        const std::optional<RECT> rect = GraphNodeRect(content, graph, node.id);
        if (rect.has_value() && MaterialEditorPanelPointInRect(*rect, x, y)) {
            return node.id;
        }
    }
    return std::nullopt;
}

inline std::optional<std::uint32_t> MaterialEditorPanelRenderer::GraphNodeAt(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId,
    int x,
    int y) noexcept {
    for (std::size_t index = graph.nodes.size(); index-- > 0U;) {
        const kb::render::RenderMaterialGraphNode& node = graph.nodes[index];
        if (MaterialEditorPanelGraphNodeHiddenByCollapsedComposite(graph, node.id)) {
            continue;
        }
        const std::optional<RECT> rect = GraphNodeRect(content, graph, node.id, sceneContext, assetId);
        if (rect.has_value() && MaterialEditorPanelPointInRect(*rect, x, y)) {
            return node.id;
        }
    }
    return std::nullopt;
}

inline std::optional<std::uint32_t> MaterialEditorPanelRenderer::GraphCommentAt(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId,
    int x,
    int y) noexcept {
    static_cast<void>(assetId);
    for (std::size_t index = graph.comments.size(); index-- > 0U;) {
        const kb::render::RenderMaterialGraphCommentBox& comment = graph.comments[index];
        const std::optional<RECT> rect = GraphCommentRect(content, graph, comment.id, sceneContext);
        if (rect.has_value() && MaterialEditorPanelPointInRect(*rect, x, y)) {
            return comment.id;
        }
    }
    return std::nullopt;
}

inline RECT MaterialEditorPanelNormalizedRect(const RECT& rect) noexcept {
    return RECT{
        std::min(rect.left, rect.right),
        std::min(rect.top, rect.bottom),
        std::max(rect.left, rect.right),
        std::max(rect.top, rect.bottom),
    };
}

inline bool MaterialEditorPanelRectsIntersect(const RECT& lhs, const RECT& rhs) noexcept {
    return lhs.left < rhs.right && lhs.right > rhs.left && lhs.top < rhs.bottom && lhs.bottom > rhs.top;
}

inline std::vector<std::uint32_t> MaterialEditorPanelRenderer::GraphNodeIdsInRect(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId,
    const RECT& selectionRect) {
    const RECT normalizedSelection = MaterialEditorPanelNormalizedRect(selectionRect);
    std::vector<std::uint32_t> nodeIds;
    for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) {
        if (MaterialEditorPanelGraphNodeHiddenByCollapsedComposite(graph, node.id)) {
            continue;
        }
        const std::optional<RECT> rect = GraphNodeRect(content, graph, node.id, sceneContext, assetId);
        if (rect.has_value() && MaterialEditorPanelRectsIntersect(*rect, normalizedSelection)) {
            nodeIds.push_back(node.id);
        }
    }
    return nodeIds;
}

inline std::optional<MaterialEditorGraphPinHit> MaterialEditorPanelRenderer::GraphPinAt(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    int x,
    int y) noexcept {
    const kb::render::RenderMaterialGraphDocument defaultGraph = graph.nodes.empty()
        ? kb::render::MakeDefaultRenderMaterialGraphDocument()
        : kb::render::RenderMaterialGraphDocument{};
    const kb::render::RenderMaterialGraphDocument& graphView = graph.nodes.empty() ? defaultGraph : graph;
    for (std::size_t nodeIndex = graphView.nodes.size(); nodeIndex-- > 0U;) {
        const kb::render::RenderMaterialGraphNode& node = graphView.nodes[nodeIndex];
        if (MaterialEditorPanelGraphNodeHiddenByCollapsedComposite(graphView, node.id)) {
            continue;
        }
        const std::optional<RECT> rect = GraphNodeRect(content, graphView, node.id);
        if (!rect.has_value()) {
            continue;
        }
        const float scale = static_cast<float>(MaterialEditorPanelRectWidth(*rect)) / static_cast<float>(std::max(1, MaterialEditorPanelMetrics::GraphNodeWidth));
        const int radius = std::max(14, MaterialEditorPanelScaled(18, scale));
        std::optional<MaterialEditorGraphPinHit> best;
        long long bestDistanceSq = static_cast<long long>(radius) * static_cast<long long>(radius);
        const auto consider = [&](const POINT& pinPoint, MaterialEditorGraphPinDirection direction, const std::string& pin) {
            const long long dx = static_cast<long long>(pinPoint.x) - static_cast<long long>(x);
            const long long dy = static_cast<long long>(pinPoint.y) - static_cast<long long>(y);
            const long long distanceSq = (dx * dx) + (dy * dy);
            if (distanceSq <= bestDistanceSq) {
                bestDistanceSq = distanceSq;
                const bool outputPin = direction == MaterialEditorGraphPinDirection::Output;
                best = MaterialEditorGraphPinHit{
                    .nodeId = node.id,
                    .direction = direction,
                    .pin = pin,
                    .type = kb::render::RenderMaterialGraphPinDataType(node, pin, outputPin),
                };
            }
        };
        const std::vector<std::string> inputPins = kb::render::RenderMaterialGraphNodeInputPinNames(node);
        for (std::size_t pinIndex = 0U; pinIndex < inputPins.size(); ++pinIndex) {
            consider(MaterialEditorPanelInputPinPoint(*rect, node.kind, pinIndex), MaterialEditorGraphPinDirection::Input, inputPins[pinIndex]);
        }
        const std::vector<std::string> outputPins = kb::render::RenderMaterialGraphNodeOutputPinNames(node);
        for (std::size_t pinIndex = 0U; pinIndex < outputPins.size(); ++pinIndex) {
            consider(MaterialEditorPanelOutputPinPoint(*rect, node.kind, pinIndex, outputPins.size()), MaterialEditorGraphPinDirection::Output, outputPins[pinIndex]);
        }
        if (best.has_value()) {
            return best;
        }
    }
    return std::nullopt;
}

inline std::optional<MaterialEditorGraphPinHit> MaterialEditorPanelRenderer::GraphPinAt(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId,
    int x,
    int y) {
    const kb::render::RenderMaterialGraphDocument defaultGraph = graph.nodes.empty()
        ? kb::render::MakeDefaultRenderMaterialGraphDocument()
        : kb::render::RenderMaterialGraphDocument{};
    const kb::render::RenderMaterialGraphDocument& graphView = graph.nodes.empty() ? defaultGraph : graph;
    for (std::size_t nodeIndex = graphView.nodes.size(); nodeIndex-- > 0U;) {
        const kb::render::RenderMaterialGraphNode& node = graphView.nodes[nodeIndex];
        if (MaterialEditorPanelGraphNodeHiddenByCollapsedComposite(graphView, node.id)) {
            continue;
        }
        const std::optional<RECT> rect = GraphNodeRect(content, graphView, node.id, sceneContext, assetId);
        if (!rect.has_value()) {
            continue;
        }
        const float scale = static_cast<float>(MaterialEditorPanelRectWidth(*rect)) / static_cast<float>(std::max(1, MaterialEditorPanelMetrics::GraphNodeWidth));
        const int radius = std::max(14, MaterialEditorPanelScaled(18, scale));
        // Pick the NEAREST pin within the hit radius (not the first), so closely-spaced pins — e.g. a
        // texture node's Tex./UV inputs — resolve to the one actually under the cursor.
        std::optional<MaterialEditorGraphPinHit> best;
        long long bestDistanceSq = static_cast<long long>(radius) * static_cast<long long>(radius);
        const auto consider = [&](const POINT& pinPoint, MaterialEditorGraphPinDirection direction, const std::string& pin) {
            const long long dx = static_cast<long long>(pinPoint.x) - static_cast<long long>(x);
            const long long dy = static_cast<long long>(pinPoint.y) - static_cast<long long>(y);
            const long long distanceSq = (dx * dx) + (dy * dy);
            if (distanceSq <= bestDistanceSq) {
                bestDistanceSq = distanceSq;
                const bool outputPin = direction == MaterialEditorGraphPinDirection::Output;
                best = MaterialEditorGraphPinHit{
                    .nodeId = node.id,
                    .direction = direction,
                    .pin = pin,
                    .type = kb::render::RenderMaterialGraphPinDataType(node, pin, outputPin),
                };
            }
        };
        const std::vector<std::string> inputPins = kb::render::RenderMaterialGraphNodeInputPinNames(node);
        for (std::size_t pinIndex = 0U; pinIndex < inputPins.size(); ++pinIndex) {
            consider(MaterialEditorPanelInputPinPoint(*rect, node.kind, pinIndex), MaterialEditorGraphPinDirection::Input, inputPins[pinIndex]);
        }
        const std::vector<std::string> outputPins = kb::render::RenderMaterialGraphNodeOutputPinNames(node);
        for (std::size_t pinIndex = 0U; pinIndex < outputPins.size(); ++pinIndex) {
            consider(MaterialEditorPanelOutputPinPoint(*rect, node.kind, pinIndex, outputPins.size()), MaterialEditorGraphPinDirection::Output, outputPins[pinIndex]);
        }
        if (best.has_value()) {
            return best;
        }
    }
    return std::nullopt;
}

inline std::optional<EditorMaterialTextureSlot> MaterialEditorPanelRenderer::TextureSlotAt(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId,
    int x,
    int y) noexcept {
    const kb::render::RenderMaterialGraphDocument defaultGraph = graph.nodes.empty()
        ? kb::render::MakeDefaultRenderMaterialGraphDocument()
        : kb::render::RenderMaterialGraphDocument{};
    const kb::render::RenderMaterialGraphDocument& graphView = graph.nodes.empty() ? defaultGraph : graph;
    for (std::size_t index = graphView.nodes.size(); index-- > 0U;) {
        const kb::render::RenderMaterialGraphNode& node = graphView.nodes[index];
        if (node.kind != kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            continue;
        }
        const std::optional<RECT> rect = GraphNodeRect(content, graphView, node.id, sceneContext, assetId);
        if (!rect.has_value()) {
            continue;
        }
        if (const std::optional<EditorMaterialTextureSlot> slot = MaterialEditorPanelTextureSlotAtOutputNode(*rect, x, y)) {
            return slot;
        }
    }
    return std::nullopt;
}

inline constexpr int kMaterialEditorGraphMenuWidth = 238;
inline constexpr int kMaterialEditorGraphMenuSearchHeight = 34;
inline constexpr int kMaterialEditorGraphMenuCategoryHeight = 22;
inline constexpr int kMaterialEditorGraphMenuCommandHeight = 22;
inline constexpr int kMaterialEditorGraphMenuPadding = 8;

inline constexpr std::size_t kMaterialEditorGraphBaseCategoryCount = 11U;

inline constexpr std::size_t MaterialEditorGraphContextMenuCategoryCount() noexcept {
    return kMaterialEditorGraphBaseCategoryCount + 1U;
}

inline constexpr std::size_t MaterialEditorGraphContextMenuFavoritesCategoryIndex() noexcept {
    return 11U;
}

inline std::string_view MaterialEditorGraphContextMenuCategoryName(std::size_t index) noexcept {
    switch (index) {
    case 0U: return "Textures";
    case 1U: return "UV";
    case 2U: return "Inputs";
    case 3U: return "Constants";
    case 4U: return "Parameters";
    case 5U: return "Math";
    case 6U: return "Utility";
    case 7U: return "Material";
    case 8U: return "Static";
    case 9U: return "Organization";
    case 10U: return "Actions";
    case 11U: return "Favorites";
    default: return "";
    }
}

inline std::vector<MaterialEditorGraphMenuCommand> MaterialEditorGraphContextMenuCommands(std::size_t index) {
    switch (index) {
    case 0U:
        return { MaterialEditorGraphMenuCommand::CreateTextureSample, MaterialEditorGraphMenuCommand::CreateTextureParameter, MaterialEditorGraphMenuCommand::CreateTextureObject };
    case 1U:
        return { MaterialEditorGraphMenuCommand::CreateUv, MaterialEditorGraphMenuCommand::CreateTextureCoordinate, MaterialEditorGraphMenuCommand::CreatePanner, MaterialEditorGraphMenuCommand::CreateRotator, MaterialEditorGraphMenuCommand::CreateBumpOffset, MaterialEditorGraphMenuCommand::CreateConstantBiasScale, MaterialEditorGraphMenuCommand::CreateRotateAboutAxis, MaterialEditorGraphMenuCommand::CreateViewportUV, MaterialEditorGraphMenuCommand::CreateTime, MaterialEditorGraphMenuCommand::CreateVertexColor, MaterialEditorGraphMenuCommand::CreateScreenPosition, MaterialEditorGraphMenuCommand::CreateLocalPosition, MaterialEditorGraphMenuCommand::CreateObjectPosition, MaterialEditorGraphMenuCommand::CreateWorldPosition, MaterialEditorGraphMenuCommand::CreatePerInstanceRandom, MaterialEditorGraphMenuCommand::CreateObjectRadius, MaterialEditorGraphMenuCommand::CreateObjectBounds, MaterialEditorGraphMenuCommand::CreateObjectOrientation, MaterialEditorGraphMenuCommand::CreateCameraPosition, MaterialEditorGraphMenuCommand::CreateCameraVector, MaterialEditorGraphMenuCommand::CreateReflectionVector, MaterialEditorGraphMenuCommand::CreateLightVector, MaterialEditorGraphMenuCommand::CreatePixelNormalWS, MaterialEditorGraphMenuCommand::CreateVertexNormalWS, MaterialEditorGraphMenuCommand::CreateVertexTangentWS, MaterialEditorGraphMenuCommand::CreateViewProperty, MaterialEditorGraphMenuCommand::CreateViewSize, MaterialEditorGraphMenuCommand::CreateSceneDepth, MaterialEditorGraphMenuCommand::CreateDepthFade };
    case 2U:
        return { MaterialEditorGraphMenuCommand::CreateTextureParameter, MaterialEditorGraphMenuCommand::CreateScalarParameter, MaterialEditorGraphMenuCommand::CreateVectorParameter, MaterialEditorGraphMenuCommand::CreateColorParameter, MaterialEditorGraphMenuCommand::CreateCollectionParameter };
    case 3U:
        return { MaterialEditorGraphMenuCommand::CreateScalar, MaterialEditorGraphMenuCommand::CreateBool, MaterialEditorGraphMenuCommand::CreateVector2, MaterialEditorGraphMenuCommand::CreateVector, MaterialEditorGraphMenuCommand::CreateColor };
    case 4U:
        return { MaterialEditorGraphMenuCommand::CreateScalarParameter, MaterialEditorGraphMenuCommand::CreateVectorParameter, MaterialEditorGraphMenuCommand::CreateColorParameter, MaterialEditorGraphMenuCommand::CreateCollectionParameter, MaterialEditorGraphMenuCommand::CreateTextureParameter };
    case 5U:
        return {
            MaterialEditorGraphMenuCommand::CreateAdd,
            MaterialEditorGraphMenuCommand::CreateSubtract,
            MaterialEditorGraphMenuCommand::CreateMultiply,
            MaterialEditorGraphMenuCommand::CreateDivide,
            MaterialEditorGraphMenuCommand::CreatePower,
            MaterialEditorGraphMenuCommand::CreateOneMinus,
            MaterialEditorGraphMenuCommand::CreateMinimum,
            MaterialEditorGraphMenuCommand::CreateMaximum,
            MaterialEditorGraphMenuCommand::CreateDotProduct,
            MaterialEditorGraphMenuCommand::CreateCrossProduct,
            MaterialEditorGraphMenuCommand::CreateLength,
            MaterialEditorGraphMenuCommand::CreateDistance,
            MaterialEditorGraphMenuCommand::CreateMakeVector,
            MaterialEditorGraphMenuCommand::CreateBreakVector,
            MaterialEditorGraphMenuCommand::CreateStep,
            MaterialEditorGraphMenuCommand::CreateSmoothStep,
            MaterialEditorGraphMenuCommand::CreateIf,
            MaterialEditorGraphMenuCommand::CreateDesaturate,
            MaterialEditorGraphMenuCommand::CreateFresnel,
            MaterialEditorGraphMenuCommand::CreateClamp,
            MaterialEditorGraphMenuCommand::CreateLerp,
        };
    case 6U:
        return {
            MaterialEditorGraphMenuCommand::CreateAbsolute,
            MaterialEditorGraphMenuCommand::CreateSaturate,
            MaterialEditorGraphMenuCommand::CreateFloor,
            MaterialEditorGraphMenuCommand::CreateCeil,
            MaterialEditorGraphMenuCommand::CreateFraction,
            MaterialEditorGraphMenuCommand::CreateSquareRoot,
            MaterialEditorGraphMenuCommand::CreateSine,
            MaterialEditorGraphMenuCommand::CreateCosine,
            MaterialEditorGraphMenuCommand::CreateExponential,
            MaterialEditorGraphMenuCommand::CreateExponential2,
            MaterialEditorGraphMenuCommand::CreateLogarithm,
            MaterialEditorGraphMenuCommand::CreateLogarithm2,
            MaterialEditorGraphMenuCommand::CreateSrgbToLinear,
            MaterialEditorGraphMenuCommand::CreateLinearToSrgb,
            MaterialEditorGraphMenuCommand::CreateLogarithm10,
            MaterialEditorGraphMenuCommand::CreateHsvToRgb,
            MaterialEditorGraphMenuCommand::CreateRgbToHsv,
            MaterialEditorGraphMenuCommand::CreateDeriveNormalZ,
            MaterialEditorGraphMenuCommand::CreateFmod,
            MaterialEditorGraphMenuCommand::CreateInverseLerp,
            MaterialEditorGraphMenuCommand::CreatePartialDerivativeX,
            MaterialEditorGraphMenuCommand::CreatePartialDerivativeY,
            MaterialEditorGraphMenuCommand::CreateSphereMask,
            MaterialEditorGraphMenuCommand::CreateBlackBody,
            MaterialEditorGraphMenuCommand::CreateNoise,
            MaterialEditorGraphMenuCommand::CreateVectorNoise,
            MaterialEditorGraphMenuCommand::CreateAppendVector,
            MaterialEditorGraphMenuCommand::CreateColorRamp,
            MaterialEditorGraphMenuCommand::CreateAntialiasedTextureMask,
            MaterialEditorGraphMenuCommand::CreateTransform,
            MaterialEditorGraphMenuCommand::CreateTransformPosition,
            MaterialEditorGraphMenuCommand::CreateNegate,
            MaterialEditorGraphMenuCommand::CreateSign,
            MaterialEditorGraphMenuCommand::CreateRound,
            MaterialEditorGraphMenuCommand::CreateTruncate,
            MaterialEditorGraphMenuCommand::CreateTangent,
            MaterialEditorGraphMenuCommand::CreateArcSine,
            MaterialEditorGraphMenuCommand::CreateArcCosine,
            MaterialEditorGraphMenuCommand::CreateArcTangent,
            MaterialEditorGraphMenuCommand::CreateArcTangent2,
            MaterialEditorGraphMenuCommand::CreateNormalize,
            MaterialEditorGraphMenuCommand::CreateNormalUnpack,
            MaterialEditorGraphMenuCommand::CreateCustomCode,
        };
    case 7U:
        return {
            MaterialEditorGraphMenuCommand::CreateMakeMaterialAttributes,
            MaterialEditorGraphMenuCommand::CreateBreakMaterialAttributes,
            MaterialEditorGraphMenuCommand::CreateBlendMaterialAttributes,
            MaterialEditorGraphMenuCommand::CreateGetMaterialAttributes,
            MaterialEditorGraphMenuCommand::CreateSetMaterialAttributes,
            MaterialEditorGraphMenuCommand::CreateFunctionInput,
            MaterialEditorGraphMenuCommand::CreateFunctionOutput,
            MaterialEditorGraphMenuCommand::CreateMaterialFunctionCall,
            MaterialEditorGraphMenuCommand::CreateLayerStack,
        };
    case 8U:
        return {
            MaterialEditorGraphMenuCommand::CreateStaticBoolParameter,
            MaterialEditorGraphMenuCommand::CreateStaticSwitch,
            MaterialEditorGraphMenuCommand::CreateStaticComponentMask,
            MaterialEditorGraphMenuCommand::CreateQualitySwitch,
            MaterialEditorGraphMenuCommand::CreateFeatureLevelSwitch,
            MaterialEditorGraphMenuCommand::CreateShadingPathSwitch,
            MaterialEditorGraphMenuCommand::CreateShaderStageSwitch,
        };
    case 9U:
        return {
            MaterialEditorGraphMenuCommand::CreateReroute,
            MaterialEditorGraphMenuCommand::CreateNamedRerouteDeclaration,
            MaterialEditorGraphMenuCommand::CreateNamedRerouteUsage,
            MaterialEditorGraphMenuCommand::CreateCompositeInput,
            MaterialEditorGraphMenuCommand::CreateCompositeOutput,
            MaterialEditorGraphMenuCommand::CreateComposite,
            MaterialEditorGraphMenuCommand::CreateComment,
        };
    case 10U:
        return { MaterialEditorGraphMenuCommand::DisconnectSelected, MaterialEditorGraphMenuCommand::DeleteSelected };
    default:
        return {};
    }
}

inline std::vector<MaterialEditorGraphMenuCommand> MaterialEditorGraphContextMenuCommands(
    std::size_t index,
    const std::vector<MaterialEditorGraphMenuCommand>& favorites) {
    if (index == MaterialEditorGraphContextMenuFavoritesCategoryIndex()) {
        return favorites;
    }
    return MaterialEditorGraphContextMenuCommands(index);
}

inline std::string_view MaterialEditorGraphContextMenuCommandName(MaterialEditorGraphMenuCommand command) noexcept {
    switch (command) {
    case MaterialEditorGraphMenuCommand::CreateTextureSample: return "Texture Sample";
    case MaterialEditorGraphMenuCommand::CreateTextureParameter: return "Texture Parameter";
    case MaterialEditorGraphMenuCommand::CreateTextureObject: return "Texture Object";
    case MaterialEditorGraphMenuCommand::CreateUv: return "UV";
    case MaterialEditorGraphMenuCommand::CreateScalar: return "Constant Scalar";
    case MaterialEditorGraphMenuCommand::CreateBool: return "Constant Bool";
    case MaterialEditorGraphMenuCommand::CreateVector2: return "Constant2Vector (XY)";
    case MaterialEditorGraphMenuCommand::CreateVector: return "Constant3Vector (RGB)";
    case MaterialEditorGraphMenuCommand::CreateColor: return "Constant4Vector (RGBA)";
    case MaterialEditorGraphMenuCommand::CreateScalarParameter: return "Scalar Parameter";
    case MaterialEditorGraphMenuCommand::CreateVectorParameter: return "Vector Parameter";
    case MaterialEditorGraphMenuCommand::CreateColorParameter: return "Color Parameter";
    case MaterialEditorGraphMenuCommand::CreateCollectionParameter: return "Collection Parameter";
    case MaterialEditorGraphMenuCommand::CreateAdd: return "Add";
    case MaterialEditorGraphMenuCommand::CreateSubtract: return "Subtract";
    case MaterialEditorGraphMenuCommand::CreateMultiply: return "Multiply";
    case MaterialEditorGraphMenuCommand::CreateDivide: return "Divide";
    case MaterialEditorGraphMenuCommand::CreatePower: return "Power";
    case MaterialEditorGraphMenuCommand::CreateOneMinus: return "One Minus";
    case MaterialEditorGraphMenuCommand::CreateAbsolute: return "Abs";
    case MaterialEditorGraphMenuCommand::CreateMinimum: return "Min";
    case MaterialEditorGraphMenuCommand::CreateMaximum: return "Max";
    case MaterialEditorGraphMenuCommand::CreateSaturate: return "Saturate";
    case MaterialEditorGraphMenuCommand::CreateFloor: return "Floor";
    case MaterialEditorGraphMenuCommand::CreateCeil: return "Ceil";
    case MaterialEditorGraphMenuCommand::CreateFraction: return "Frac";
    case MaterialEditorGraphMenuCommand::CreateSquareRoot: return "Sqrt";
    case MaterialEditorGraphMenuCommand::CreateSine: return "Sin";
    case MaterialEditorGraphMenuCommand::CreateCosine: return "Cos";
    case MaterialEditorGraphMenuCommand::CreateExponential: return "Exp";
    case MaterialEditorGraphMenuCommand::CreateExponential2: return "Exp2";
    case MaterialEditorGraphMenuCommand::CreateLogarithm: return "Log";
    case MaterialEditorGraphMenuCommand::CreateLogarithm2: return "Log2";
    case MaterialEditorGraphMenuCommand::CreateSrgbToLinear: return "sRGB to Linear";
    case MaterialEditorGraphMenuCommand::CreateLinearToSrgb: return "Linear to sRGB";
    case MaterialEditorGraphMenuCommand::CreateLogarithm10: return "Log10";
    case MaterialEditorGraphMenuCommand::CreateHsvToRgb: return "HSV to RGB";
    case MaterialEditorGraphMenuCommand::CreateRgbToHsv: return "RGB to HSV";
    case MaterialEditorGraphMenuCommand::CreateDeriveNormalZ: return "Derive Normal Z";
    case MaterialEditorGraphMenuCommand::CreateFmod: return "Fmod";
    case MaterialEditorGraphMenuCommand::CreateInverseLerp: return "Inverse Lerp";
    case MaterialEditorGraphMenuCommand::CreatePartialDerivativeX: return "DDX";
    case MaterialEditorGraphMenuCommand::CreatePartialDerivativeY: return "DDY";
    case MaterialEditorGraphMenuCommand::CreateSphereMask: return "Sphere Mask";
    case MaterialEditorGraphMenuCommand::CreateBlackBody: return "Black Body";
    case MaterialEditorGraphMenuCommand::CreateNoise: return "Noise";
    case MaterialEditorGraphMenuCommand::CreateVectorNoise: return "Vector Noise";
    case MaterialEditorGraphMenuCommand::CreateAppendVector: return "Append Vector";
    case MaterialEditorGraphMenuCommand::CreateColorRamp: return "Color Ramp";
    case MaterialEditorGraphMenuCommand::CreateAntialiasedTextureMask: return "Antialiased Mask";
    case MaterialEditorGraphMenuCommand::CreateTransform: return "Transform";
    case MaterialEditorGraphMenuCommand::CreateTransformPosition: return "Transform Position";
    case MaterialEditorGraphMenuCommand::CreateDotProduct: return "Dot Product";
    case MaterialEditorGraphMenuCommand::CreateCrossProduct: return "Cross Product";
    case MaterialEditorGraphMenuCommand::CreateNormalize: return "Normalize";
    case MaterialEditorGraphMenuCommand::CreateLength: return "Length";
    case MaterialEditorGraphMenuCommand::CreateDistance: return "Distance";
    case MaterialEditorGraphMenuCommand::CreateBreakVector: return "Break Vector";
    case MaterialEditorGraphMenuCommand::CreateMakeVector: return "Make Vector";
    case MaterialEditorGraphMenuCommand::CreateStep: return "Step";
    case MaterialEditorGraphMenuCommand::CreateSmoothStep: return "Smooth Step";
    case MaterialEditorGraphMenuCommand::CreateIf: return "If";
    case MaterialEditorGraphMenuCommand::CreateDesaturate: return "Desaturate";
    case MaterialEditorGraphMenuCommand::CreateFresnel: return "Fresnel";
    case MaterialEditorGraphMenuCommand::CreateNegate: return "Negate";
    case MaterialEditorGraphMenuCommand::CreateSign: return "Sign";
    case MaterialEditorGraphMenuCommand::CreateRound: return "Round";
    case MaterialEditorGraphMenuCommand::CreateTruncate: return "Truncate";
    case MaterialEditorGraphMenuCommand::CreateTangent: return "Tan";
    case MaterialEditorGraphMenuCommand::CreateArcSine: return "Asin";
    case MaterialEditorGraphMenuCommand::CreateArcCosine: return "Acos";
    case MaterialEditorGraphMenuCommand::CreateArcTangent: return "Atan";
    case MaterialEditorGraphMenuCommand::CreateArcTangent2: return "Atan2";
    case MaterialEditorGraphMenuCommand::CreateClamp: return "Clamp";
    case MaterialEditorGraphMenuCommand::CreateLerp: return "Lerp";
    case MaterialEditorGraphMenuCommand::CreateNormalUnpack: return "Normal Unpack";
    case MaterialEditorGraphMenuCommand::CreateTime: return "Time";
    case MaterialEditorGraphMenuCommand::CreateVertexColor: return "Vertex Color";
    case MaterialEditorGraphMenuCommand::CreateScreenPosition: return "Screen Position";
    case MaterialEditorGraphMenuCommand::CreateLocalPosition: return "Local Position";
    case MaterialEditorGraphMenuCommand::CreateObjectPosition: return "Object Position";
    case MaterialEditorGraphMenuCommand::CreateWorldPosition: return "World Position";
    case MaterialEditorGraphMenuCommand::CreatePerInstanceRandom: return "Per Instance Random";
    case MaterialEditorGraphMenuCommand::CreateObjectRadius: return "Object Radius";
    case MaterialEditorGraphMenuCommand::CreateObjectBounds: return "Object Bounds";
    case MaterialEditorGraphMenuCommand::CreateObjectOrientation: return "Object Orientation";
    case MaterialEditorGraphMenuCommand::CreateMakeMaterialAttributes: return "Make Material Attributes";
    case MaterialEditorGraphMenuCommand::CreateBreakMaterialAttributes: return "Break Material Attributes";
    case MaterialEditorGraphMenuCommand::CreateBlendMaterialAttributes: return "Blend Material Attributes";
    case MaterialEditorGraphMenuCommand::CreateGetMaterialAttributes: return "Get Material Attributes";
    case MaterialEditorGraphMenuCommand::CreateSetMaterialAttributes: return "Set Material Attributes";
    case MaterialEditorGraphMenuCommand::CreateStaticBoolParameter: return "Static Bool Parameter";
    case MaterialEditorGraphMenuCommand::CreateStaticSwitch: return "Static Switch";
    case MaterialEditorGraphMenuCommand::CreateStaticComponentMask: return "Static Component Mask";
    case MaterialEditorGraphMenuCommand::CreateQualitySwitch: return "Quality Switch";
    case MaterialEditorGraphMenuCommand::CreateFeatureLevelSwitch: return "Feature Level Switch";
    case MaterialEditorGraphMenuCommand::CreateShadingPathSwitch: return "Shading Path Switch";
    case MaterialEditorGraphMenuCommand::CreateShaderStageSwitch: return "Shader Stage Switch";
    case MaterialEditorGraphMenuCommand::CreateTextureCoordinate: return "Texture Coordinate";
    case MaterialEditorGraphMenuCommand::CreatePanner: return "Panner";
    case MaterialEditorGraphMenuCommand::CreateRotator: return "Rotator";
    case MaterialEditorGraphMenuCommand::CreateBumpOffset: return "Bump Offset";
    case MaterialEditorGraphMenuCommand::CreateConstantBiasScale: return "Constant Bias Scale";
    case MaterialEditorGraphMenuCommand::CreateRotateAboutAxis: return "Rotate About Axis";
    case MaterialEditorGraphMenuCommand::CreateViewportUV: return "Viewport UV";
    case MaterialEditorGraphMenuCommand::CreateCameraPosition: return "Camera Position";
    case MaterialEditorGraphMenuCommand::CreateCameraVector: return "Camera Vector";
    case MaterialEditorGraphMenuCommand::CreateReflectionVector: return "Reflection Vector";
    case MaterialEditorGraphMenuCommand::CreateLightVector: return "Light Vector";
    case MaterialEditorGraphMenuCommand::CreatePixelNormalWS: return "Pixel Normal WS";
    case MaterialEditorGraphMenuCommand::CreateVertexNormalWS: return "Vertex Normal WS";
    case MaterialEditorGraphMenuCommand::CreateVertexTangentWS: return "Vertex Tangent WS";
    case MaterialEditorGraphMenuCommand::CreateViewProperty: return "View Property";
    case MaterialEditorGraphMenuCommand::CreateViewSize: return "View Size";
    case MaterialEditorGraphMenuCommand::CreateSceneDepth: return "Scene Depth";
    case MaterialEditorGraphMenuCommand::CreateDepthFade: return "Depth Fade";
    case MaterialEditorGraphMenuCommand::CreateCustomCode: return "Custom Code";
    case MaterialEditorGraphMenuCommand::CreateReroute: return "Reroute";
    case MaterialEditorGraphMenuCommand::CreateNamedRerouteDeclaration: return "Named Reroute Declaration";
    case MaterialEditorGraphMenuCommand::CreateNamedRerouteUsage: return "Named Reroute Usage";
    case MaterialEditorGraphMenuCommand::CreateCompositeInput: return "Composite Input";
    case MaterialEditorGraphMenuCommand::CreateCompositeOutput: return "Composite Output";
    case MaterialEditorGraphMenuCommand::CreateFunctionInput: return "Function Input";
    case MaterialEditorGraphMenuCommand::CreateFunctionOutput: return "Function Output";
    case MaterialEditorGraphMenuCommand::CreateMaterialFunctionCall: return "Material Function Call";
    case MaterialEditorGraphMenuCommand::CreateLayerStack: return "Layer Stack";
    case MaterialEditorGraphMenuCommand::CreateComposite: return "Composite";
    case MaterialEditorGraphMenuCommand::CreateComment: return "Comment Box";
    case MaterialEditorGraphMenuCommand::DisconnectSelected: return "Disconnect Selected Links";
    case MaterialEditorGraphMenuCommand::DeleteSelected: return "Delete Selected";
    case MaterialEditorGraphMenuCommand::None: return "";
    }
    return "";
}

inline bool MaterialEditorGraphContextMenuCommandEnabled(MaterialEditorGraphMenuCommand command, bool hasSelectedNode) noexcept {
    switch (command) {
    case MaterialEditorGraphMenuCommand::DisconnectSelected:
    case MaterialEditorGraphMenuCommand::DeleteSelected:
        return hasSelectedNode;
    case MaterialEditorGraphMenuCommand::None:
        return false;
    default:
        return true;
    }
}

[[nodiscard]] inline bool MaterialEditorGraphCommandInList(
    const std::vector<MaterialEditorGraphMenuCommand>& commands,
    MaterialEditorGraphMenuCommand command) noexcept {
    return std::find(commands.begin(), commands.end(), command) != commands.end();
}

[[nodiscard]] inline std::optional<kb::render::RenderMaterialGraphNodeKind> MaterialEditorGraphMenuCommandNodeKind(
    MaterialEditorGraphMenuCommand command) noexcept {
    switch (command) {
    case MaterialEditorGraphMenuCommand::CreateTextureSample: return kb::render::RenderMaterialGraphNodeKind::TextureSample;
    case MaterialEditorGraphMenuCommand::CreateTextureParameter: return kb::render::RenderMaterialGraphNodeKind::ParameterTexture;
    case MaterialEditorGraphMenuCommand::CreateTextureObject: return kb::render::RenderMaterialGraphNodeKind::TextureObject;
    case MaterialEditorGraphMenuCommand::CreateUv: return kb::render::RenderMaterialGraphNodeKind::Uv;
    case MaterialEditorGraphMenuCommand::CreateScalar: return kb::render::RenderMaterialGraphNodeKind::ConstantScalar;
    case MaterialEditorGraphMenuCommand::CreateBool: return kb::render::RenderMaterialGraphNodeKind::ConstantBool;
    case MaterialEditorGraphMenuCommand::CreateVector2: return kb::render::RenderMaterialGraphNodeKind::ConstantVector2;
    case MaterialEditorGraphMenuCommand::CreateVector: return kb::render::RenderMaterialGraphNodeKind::ConstantVector;
    case MaterialEditorGraphMenuCommand::CreateColor: return kb::render::RenderMaterialGraphNodeKind::ConstantColor;
    case MaterialEditorGraphMenuCommand::CreateScalarParameter: return kb::render::RenderMaterialGraphNodeKind::ParameterScalar;
    case MaterialEditorGraphMenuCommand::CreateVectorParameter: return kb::render::RenderMaterialGraphNodeKind::ParameterVector;
    case MaterialEditorGraphMenuCommand::CreateColorParameter: return kb::render::RenderMaterialGraphNodeKind::ParameterColor;
    case MaterialEditorGraphMenuCommand::CreateCollectionParameter: return kb::render::RenderMaterialGraphNodeKind::CollectionParameter;
    case MaterialEditorGraphMenuCommand::CreateAdd: return kb::render::RenderMaterialGraphNodeKind::Add;
    case MaterialEditorGraphMenuCommand::CreateSubtract: return kb::render::RenderMaterialGraphNodeKind::Subtract;
    case MaterialEditorGraphMenuCommand::CreateMultiply: return kb::render::RenderMaterialGraphNodeKind::Multiply;
    case MaterialEditorGraphMenuCommand::CreateDivide: return kb::render::RenderMaterialGraphNodeKind::Divide;
    case MaterialEditorGraphMenuCommand::CreatePower: return kb::render::RenderMaterialGraphNodeKind::Power;
    case MaterialEditorGraphMenuCommand::CreateOneMinus: return kb::render::RenderMaterialGraphNodeKind::OneMinus;
    case MaterialEditorGraphMenuCommand::CreateAbsolute: return kb::render::RenderMaterialGraphNodeKind::Absolute;
    case MaterialEditorGraphMenuCommand::CreateMinimum: return kb::render::RenderMaterialGraphNodeKind::Minimum;
    case MaterialEditorGraphMenuCommand::CreateMaximum: return kb::render::RenderMaterialGraphNodeKind::Maximum;
    case MaterialEditorGraphMenuCommand::CreateSaturate: return kb::render::RenderMaterialGraphNodeKind::Saturate;
    case MaterialEditorGraphMenuCommand::CreateFloor: return kb::render::RenderMaterialGraphNodeKind::Floor;
    case MaterialEditorGraphMenuCommand::CreateCeil: return kb::render::RenderMaterialGraphNodeKind::Ceil;
    case MaterialEditorGraphMenuCommand::CreateFraction: return kb::render::RenderMaterialGraphNodeKind::Fraction;
    case MaterialEditorGraphMenuCommand::CreateSquareRoot: return kb::render::RenderMaterialGraphNodeKind::SquareRoot;
    case MaterialEditorGraphMenuCommand::CreateSine: return kb::render::RenderMaterialGraphNodeKind::Sine;
    case MaterialEditorGraphMenuCommand::CreateCosine: return kb::render::RenderMaterialGraphNodeKind::Cosine;
    case MaterialEditorGraphMenuCommand::CreateExponential: return kb::render::RenderMaterialGraphNodeKind::Exponential;
    case MaterialEditorGraphMenuCommand::CreateExponential2: return kb::render::RenderMaterialGraphNodeKind::Exponential2;
    case MaterialEditorGraphMenuCommand::CreateLogarithm: return kb::render::RenderMaterialGraphNodeKind::Logarithm;
    case MaterialEditorGraphMenuCommand::CreateLogarithm2: return kb::render::RenderMaterialGraphNodeKind::Logarithm2;
    case MaterialEditorGraphMenuCommand::CreateSrgbToLinear: return kb::render::RenderMaterialGraphNodeKind::SrgbToLinear;
    case MaterialEditorGraphMenuCommand::CreateLinearToSrgb: return kb::render::RenderMaterialGraphNodeKind::LinearToSrgb;
    case MaterialEditorGraphMenuCommand::CreateLogarithm10: return kb::render::RenderMaterialGraphNodeKind::Logarithm10;
    case MaterialEditorGraphMenuCommand::CreateHsvToRgb: return kb::render::RenderMaterialGraphNodeKind::HsvToRgb;
    case MaterialEditorGraphMenuCommand::CreateRgbToHsv: return kb::render::RenderMaterialGraphNodeKind::RgbToHsv;
    case MaterialEditorGraphMenuCommand::CreateDeriveNormalZ: return kb::render::RenderMaterialGraphNodeKind::DeriveNormalZ;
    case MaterialEditorGraphMenuCommand::CreateFmod: return kb::render::RenderMaterialGraphNodeKind::Fmod;
    case MaterialEditorGraphMenuCommand::CreateInverseLerp: return kb::render::RenderMaterialGraphNodeKind::InverseLerp;
    case MaterialEditorGraphMenuCommand::CreatePartialDerivativeX: return kb::render::RenderMaterialGraphNodeKind::PartialDerivativeX;
    case MaterialEditorGraphMenuCommand::CreatePartialDerivativeY: return kb::render::RenderMaterialGraphNodeKind::PartialDerivativeY;
    case MaterialEditorGraphMenuCommand::CreateSphereMask: return kb::render::RenderMaterialGraphNodeKind::SphereMask;
    case MaterialEditorGraphMenuCommand::CreateBlackBody: return kb::render::RenderMaterialGraphNodeKind::BlackBody;
    case MaterialEditorGraphMenuCommand::CreateNoise: return kb::render::RenderMaterialGraphNodeKind::Noise;
    case MaterialEditorGraphMenuCommand::CreateVectorNoise: return kb::render::RenderMaterialGraphNodeKind::VectorNoise;
    case MaterialEditorGraphMenuCommand::CreateAppendVector: return kb::render::RenderMaterialGraphNodeKind::AppendVector;
    case MaterialEditorGraphMenuCommand::CreateColorRamp: return kb::render::RenderMaterialGraphNodeKind::ColorRamp;
    case MaterialEditorGraphMenuCommand::CreateAntialiasedTextureMask: return kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask;
    case MaterialEditorGraphMenuCommand::CreateTransform: return kb::render::RenderMaterialGraphNodeKind::Transform;
    case MaterialEditorGraphMenuCommand::CreateTransformPosition: return kb::render::RenderMaterialGraphNodeKind::TransformPosition;
    case MaterialEditorGraphMenuCommand::CreateDotProduct: return kb::render::RenderMaterialGraphNodeKind::DotProduct;
    case MaterialEditorGraphMenuCommand::CreateCrossProduct: return kb::render::RenderMaterialGraphNodeKind::CrossProduct;
    case MaterialEditorGraphMenuCommand::CreateNormalize: return kb::render::RenderMaterialGraphNodeKind::Normalize;
    case MaterialEditorGraphMenuCommand::CreateLength: return kb::render::RenderMaterialGraphNodeKind::Length;
    case MaterialEditorGraphMenuCommand::CreateDistance: return kb::render::RenderMaterialGraphNodeKind::Distance;
    case MaterialEditorGraphMenuCommand::CreateBreakVector: return kb::render::RenderMaterialGraphNodeKind::BreakVector;
    case MaterialEditorGraphMenuCommand::CreateMakeVector: return kb::render::RenderMaterialGraphNodeKind::MakeVector;
    case MaterialEditorGraphMenuCommand::CreateStep: return kb::render::RenderMaterialGraphNodeKind::Step;
    case MaterialEditorGraphMenuCommand::CreateSmoothStep: return kb::render::RenderMaterialGraphNodeKind::SmoothStep;
    case MaterialEditorGraphMenuCommand::CreateIf: return kb::render::RenderMaterialGraphNodeKind::If;
    case MaterialEditorGraphMenuCommand::CreateDesaturate: return kb::render::RenderMaterialGraphNodeKind::Desaturate;
    case MaterialEditorGraphMenuCommand::CreateFresnel: return kb::render::RenderMaterialGraphNodeKind::Fresnel;
    case MaterialEditorGraphMenuCommand::CreateNegate: return kb::render::RenderMaterialGraphNodeKind::Negate;
    case MaterialEditorGraphMenuCommand::CreateSign: return kb::render::RenderMaterialGraphNodeKind::Sign;
    case MaterialEditorGraphMenuCommand::CreateRound: return kb::render::RenderMaterialGraphNodeKind::Round;
    case MaterialEditorGraphMenuCommand::CreateTruncate: return kb::render::RenderMaterialGraphNodeKind::Truncate;
    case MaterialEditorGraphMenuCommand::CreateTangent: return kb::render::RenderMaterialGraphNodeKind::Tangent;
    case MaterialEditorGraphMenuCommand::CreateArcSine: return kb::render::RenderMaterialGraphNodeKind::ArcSine;
    case MaterialEditorGraphMenuCommand::CreateArcCosine: return kb::render::RenderMaterialGraphNodeKind::ArcCosine;
    case MaterialEditorGraphMenuCommand::CreateArcTangent: return kb::render::RenderMaterialGraphNodeKind::ArcTangent;
    case MaterialEditorGraphMenuCommand::CreateArcTangent2: return kb::render::RenderMaterialGraphNodeKind::ArcTangent2;
    case MaterialEditorGraphMenuCommand::CreateClamp: return kb::render::RenderMaterialGraphNodeKind::Clamp;
    case MaterialEditorGraphMenuCommand::CreateLerp: return kb::render::RenderMaterialGraphNodeKind::Lerp;
    case MaterialEditorGraphMenuCommand::CreateNormalUnpack: return kb::render::RenderMaterialGraphNodeKind::NormalUnpack;
    case MaterialEditorGraphMenuCommand::CreateTime: return kb::render::RenderMaterialGraphNodeKind::Time;
    case MaterialEditorGraphMenuCommand::CreateVertexColor: return kb::render::RenderMaterialGraphNodeKind::VertexColor;
    case MaterialEditorGraphMenuCommand::CreateScreenPosition: return kb::render::RenderMaterialGraphNodeKind::ScreenPosition;
    case MaterialEditorGraphMenuCommand::CreateLocalPosition: return kb::render::RenderMaterialGraphNodeKind::LocalPosition;
    case MaterialEditorGraphMenuCommand::CreateObjectPosition: return kb::render::RenderMaterialGraphNodeKind::ObjectPosition;
    case MaterialEditorGraphMenuCommand::CreateWorldPosition: return kb::render::RenderMaterialGraphNodeKind::WorldPosition;
    case MaterialEditorGraphMenuCommand::CreatePerInstanceRandom: return kb::render::RenderMaterialGraphNodeKind::PerInstanceRandom;
    case MaterialEditorGraphMenuCommand::CreateObjectRadius: return kb::render::RenderMaterialGraphNodeKind::ObjectRadius;
    case MaterialEditorGraphMenuCommand::CreateObjectBounds: return kb::render::RenderMaterialGraphNodeKind::ObjectBounds;
    case MaterialEditorGraphMenuCommand::CreateObjectOrientation: return kb::render::RenderMaterialGraphNodeKind::ObjectOrientation;
    case MaterialEditorGraphMenuCommand::CreateMakeMaterialAttributes: return kb::render::RenderMaterialGraphNodeKind::MakeMaterialAttributes;
    case MaterialEditorGraphMenuCommand::CreateBreakMaterialAttributes: return kb::render::RenderMaterialGraphNodeKind::BreakMaterialAttributes;
    case MaterialEditorGraphMenuCommand::CreateBlendMaterialAttributes: return kb::render::RenderMaterialGraphNodeKind::BlendMaterialAttributes;
    case MaterialEditorGraphMenuCommand::CreateGetMaterialAttributes: return kb::render::RenderMaterialGraphNodeKind::GetMaterialAttributes;
    case MaterialEditorGraphMenuCommand::CreateSetMaterialAttributes: return kb::render::RenderMaterialGraphNodeKind::SetMaterialAttributes;
    case MaterialEditorGraphMenuCommand::CreateStaticBoolParameter: return kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter;
    case MaterialEditorGraphMenuCommand::CreateStaticSwitch: return kb::render::RenderMaterialGraphNodeKind::StaticSwitch;
    case MaterialEditorGraphMenuCommand::CreateStaticComponentMask: return kb::render::RenderMaterialGraphNodeKind::StaticComponentMask;
    case MaterialEditorGraphMenuCommand::CreateQualitySwitch: return kb::render::RenderMaterialGraphNodeKind::QualitySwitch;
    case MaterialEditorGraphMenuCommand::CreateFeatureLevelSwitch: return kb::render::RenderMaterialGraphNodeKind::FeatureLevelSwitch;
    case MaterialEditorGraphMenuCommand::CreateShadingPathSwitch: return kb::render::RenderMaterialGraphNodeKind::ShadingPathSwitch;
    case MaterialEditorGraphMenuCommand::CreateShaderStageSwitch: return kb::render::RenderMaterialGraphNodeKind::ShaderStageSwitch;
    case MaterialEditorGraphMenuCommand::CreateTextureCoordinate: return kb::render::RenderMaterialGraphNodeKind::TextureCoordinate;
    case MaterialEditorGraphMenuCommand::CreatePanner: return kb::render::RenderMaterialGraphNodeKind::Panner;
    case MaterialEditorGraphMenuCommand::CreateRotator: return kb::render::RenderMaterialGraphNodeKind::Rotator;
    case MaterialEditorGraphMenuCommand::CreateBumpOffset: return kb::render::RenderMaterialGraphNodeKind::BumpOffset;
    case MaterialEditorGraphMenuCommand::CreateConstantBiasScale: return kb::render::RenderMaterialGraphNodeKind::ConstantBiasScale;
    case MaterialEditorGraphMenuCommand::CreateRotateAboutAxis: return kb::render::RenderMaterialGraphNodeKind::RotateAboutAxis;
    case MaterialEditorGraphMenuCommand::CreateViewportUV: return kb::render::RenderMaterialGraphNodeKind::ViewportUV;
    case MaterialEditorGraphMenuCommand::CreateCameraPosition: return kb::render::RenderMaterialGraphNodeKind::CameraPosition;
    case MaterialEditorGraphMenuCommand::CreateCameraVector: return kb::render::RenderMaterialGraphNodeKind::CameraVector;
    case MaterialEditorGraphMenuCommand::CreateReflectionVector: return kb::render::RenderMaterialGraphNodeKind::ReflectionVector;
    case MaterialEditorGraphMenuCommand::CreateLightVector: return kb::render::RenderMaterialGraphNodeKind::LightVector;
    case MaterialEditorGraphMenuCommand::CreatePixelNormalWS: return kb::render::RenderMaterialGraphNodeKind::PixelNormalWS;
    case MaterialEditorGraphMenuCommand::CreateVertexNormalWS: return kb::render::RenderMaterialGraphNodeKind::VertexNormalWS;
    case MaterialEditorGraphMenuCommand::CreateVertexTangentWS: return kb::render::RenderMaterialGraphNodeKind::VertexTangentWS;
    case MaterialEditorGraphMenuCommand::CreateViewProperty: return kb::render::RenderMaterialGraphNodeKind::ViewProperty;
    case MaterialEditorGraphMenuCommand::CreateViewSize: return kb::render::RenderMaterialGraphNodeKind::ViewSize;
    case MaterialEditorGraphMenuCommand::CreateSceneDepth: return kb::render::RenderMaterialGraphNodeKind::SceneDepth;
    case MaterialEditorGraphMenuCommand::CreateDepthFade: return kb::render::RenderMaterialGraphNodeKind::DepthFade;
    case MaterialEditorGraphMenuCommand::CreateCustomCode: return kb::render::RenderMaterialGraphNodeKind::CustomCode;
    case MaterialEditorGraphMenuCommand::CreateReroute: return kb::render::RenderMaterialGraphNodeKind::Reroute;
    case MaterialEditorGraphMenuCommand::CreateNamedRerouteDeclaration: return kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration;
    case MaterialEditorGraphMenuCommand::CreateNamedRerouteUsage: return kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage;
    case MaterialEditorGraphMenuCommand::CreateCompositeInput: return kb::render::RenderMaterialGraphNodeKind::CompositeInput;
    case MaterialEditorGraphMenuCommand::CreateCompositeOutput: return kb::render::RenderMaterialGraphNodeKind::CompositeOutput;
    case MaterialEditorGraphMenuCommand::CreateFunctionInput: return kb::render::RenderMaterialGraphNodeKind::FunctionInput;
    case MaterialEditorGraphMenuCommand::CreateFunctionOutput: return kb::render::RenderMaterialGraphNodeKind::FunctionOutput;
    case MaterialEditorGraphMenuCommand::CreateMaterialFunctionCall: return kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall;
    case MaterialEditorGraphMenuCommand::CreateLayerStack: return kb::render::RenderMaterialGraphNodeKind::LayerStack;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] inline std::vector<MaterialEditorGraphMenuCommand> MaterialEditorGraphPaletteAllCommands() {
    std::vector<MaterialEditorGraphMenuCommand> commands;
    for (std::size_t categoryIndex = 0U; categoryIndex < kMaterialEditorGraphBaseCategoryCount; ++categoryIndex) {
        for (const MaterialEditorGraphMenuCommand command : MaterialEditorGraphContextMenuCommands(categoryIndex)) {
            if (command != MaterialEditorGraphMenuCommand::None && !MaterialEditorGraphCommandInList(commands, command)) {
                commands.push_back(command);
            }
        }
    }
    return commands;
}

[[nodiscard]] inline std::string MaterialEditorGraphPaletteNormalize(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (const unsigned char ch : text) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return normalized;
}

[[nodiscard]] inline bool MaterialEditorGraphPaletteCommandMatches(
    MaterialEditorGraphMenuCommand command,
    std::string_view query) {
    const std::string normalizedQuery = MaterialEditorGraphPaletteNormalize(query);
    if (normalizedQuery.empty()) {
        return true;
    }
    std::string haystack{ MaterialEditorGraphContextMenuCommandName(command) };
    if (const std::optional<kb::render::RenderMaterialGraphNodeKind> kind = MaterialEditorGraphMenuCommandNodeKind(command)) {
        haystack += " ";
        haystack += kb::render::RenderMaterialGraphNodeKindName(*kind);
        for (const std::string& pin : kb::render::RenderMaterialGraphNodeInputPinNames(*kind)) {
            haystack += " ";
            haystack += pin;
        }
        for (const std::string& pin : kb::render::RenderMaterialGraphNodeOutputPinNames(*kind)) {
            haystack += " ";
            haystack += pin;
        }
    }
    if (command == MaterialEditorGraphMenuCommand::CreateCollectionParameter) {
        haystack += " material parameter collection mpc global parameter global uniform";
    }
    if (command == MaterialEditorGraphMenuCommand::CreateTextureObject) {
        haystack += " texture object parameter texture object sampler object";
    }
    if (command == MaterialEditorGraphMenuCommand::CreateStaticSwitch) {
        haystack += " static switch parameter staticswitchparameter";
    }
    if (command == MaterialEditorGraphMenuCommand::CreateStaticComponentMask) {
        haystack += " static component mask parameter channel mask channelmask channelmaskparameter";
    }
    const std::string normalizedHaystack = MaterialEditorGraphPaletteNormalize(haystack);
    if (normalizedHaystack.find(normalizedQuery) != std::string::npos) {
        return true;
    }
    std::string token;
    bool sawToken = false;
    for (std::size_t index = 0U; index <= query.size(); ++index) {
        const char ch = index < query.size() ? static_cast<char>(query[index]) : ' ';
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            token.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            continue;
        }
        if (token.empty()) {
            continue;
        }
        sawToken = true;
        if (normalizedHaystack.find(token) == std::string::npos) {
            return false;
        }
        token.clear();
    }
    return sawToken;
}

[[nodiscard]] inline std::optional<std::string> MaterialEditorGraphCompatibleCommandPin(
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t sourceNodeId,
    std::string_view sourcePin,
    bool sourceOutput,
    MaterialEditorGraphMenuCommand command) {
    const kb::render::RenderMaterialGraphNode* sourceNode = kb::render::FindRenderMaterialGraphNode(graph, sourceNodeId);
    const std::optional<kb::render::RenderMaterialGraphNodeKind> candidateKind = MaterialEditorGraphMenuCommandNodeKind(command);
    if (sourceNode == nullptr || !candidateKind.has_value()) {
        return std::nullopt;
    }
    const bool sourcePinValid = sourceOutput
        ? kb::render::IsRenderMaterialGraphOutputPin(*sourceNode, sourcePin)
        : kb::render::IsRenderMaterialGraphInputPin(*sourceNode, sourcePin);
    if (!sourcePinValid) {
        return std::nullopt;
    }
    const kb::render::RenderMaterialGraphPinType sourceType =
        kb::render::RenderMaterialGraphPinDataType(*sourceNode, sourcePin, sourceOutput);
    if (sourceOutput) {
        for (const std::string& inputPin : kb::render::RenderMaterialGraphNodeInputPinNames(*candidateKind)) {
            if (kb::render::AreRenderMaterialGraphPinsCompatible(
                    sourceType,
                    kb::render::RenderMaterialGraphPinDataType(*candidateKind, inputPin, false))) {
                return inputPin;
            }
        }
    } else {
        for (const std::string& outputPin : kb::render::RenderMaterialGraphNodeOutputPinNames(*candidateKind)) {
            if (kb::render::AreRenderMaterialGraphPinsCompatible(
                    kb::render::RenderMaterialGraphPinDataType(*candidateKind, outputPin, true),
                    sourceType)) {
                return outputPin;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline std::vector<MaterialEditorGraphMenuCommand> MaterialEditorGraphCompatibleCommands(
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t sourceNodeId,
    std::string_view sourcePin,
    bool sourceOutput) {
    std::vector<MaterialEditorGraphMenuCommand> compatible;
    for (const MaterialEditorGraphMenuCommand command : MaterialEditorGraphPaletteAllCommands()) {
        if (MaterialEditorGraphCompatibleCommandPin(graph, sourceNodeId, sourcePin, sourceOutput, command).has_value()) {
            compatible.push_back(command);
        }
    }
    return compatible;
}

[[nodiscard]] inline bool MaterialEditorGraphContextMenuUsesFlatCommandList(const EditorSceneContext& sceneContext) noexcept {
    return !sceneContext.MaterialGraphContextMenuSearchQuery().empty() || sceneContext.IsMaterialGraphContextMenuPinFiltered();
}

[[nodiscard]] inline std::vector<MaterialEditorGraphMenuCommand> MaterialEditorGraphContextMenuFilteredCommands(
    const EditorSceneContext& sceneContext) {
    std::vector<MaterialEditorGraphMenuCommand> commands = MaterialEditorGraphPaletteAllCommands();
    if (sceneContext.IsMaterialGraphContextMenuPinFiltered()) {
        commands.clear();
        const std::optional<kb::render::RenderMaterialAssetData>& workingCopy = sceneContext.MaterialEditor().WorkingCopy();
        if (workingCopy.has_value()) {
            commands = MaterialEditorGraphCompatibleCommands(
                workingCopy->graph,
                sceneContext.MaterialGraphContextMenuPinFilterNodeId(),
                sceneContext.MaterialGraphContextMenuPinFilterPin(),
                sceneContext.MaterialGraphContextMenuPinFilterIsOutput());
        }
    }

    const std::string_view query = sceneContext.MaterialGraphContextMenuSearchQuery();
    const std::vector<MaterialEditorGraphMenuCommand>& favorites = sceneContext.MaterialGraphPaletteFavoriteCommands();
    std::vector<MaterialEditorGraphMenuCommand> filtered;
    filtered.reserve(commands.size());
    for (const MaterialEditorGraphMenuCommand command : commands) {
        if (MaterialEditorGraphPaletteCommandMatches(command, query)) {
            filtered.push_back(command);
        }
    }
    std::stable_sort(filtered.begin(), filtered.end(), [&favorites](MaterialEditorGraphMenuCommand lhs, MaterialEditorGraphMenuCommand rhs) {
        const bool lhsFavorite = MaterialEditorGraphCommandInList(favorites, lhs);
        const bool rhsFavorite = MaterialEditorGraphCommandInList(favorites, rhs);
        return lhsFavorite && !rhsFavorite;
    });
    return filtered;
}

inline int MaterialEditorGraphContextMenuHeight(const EditorSceneContext& sceneContext) {
    int height = kMaterialEditorGraphMenuPadding + kMaterialEditorGraphMenuSearchHeight + kMaterialEditorGraphMenuPadding;
    if (MaterialEditorGraphContextMenuUsesFlatCommandList(sceneContext)) {
        return height + static_cast<int>(MaterialEditorGraphContextMenuFilteredCommands(sceneContext).size()) * kMaterialEditorGraphMenuCommandHeight +
            kMaterialEditorGraphMenuPadding;
    }
    const std::vector<MaterialEditorGraphMenuCommand>& favorites = sceneContext.MaterialGraphPaletteFavoriteCommands();
    for (std::size_t categoryIndex = 0U; categoryIndex < MaterialEditorGraphContextMenuCategoryCount(); ++categoryIndex) {
        height += kMaterialEditorGraphMenuCategoryHeight;
        if (sceneContext.IsMaterialGraphContextMenuCategoryExpanded(categoryIndex)) {
            height += static_cast<int>(MaterialEditorGraphContextMenuCommands(categoryIndex, favorites).size()) * kMaterialEditorGraphMenuCommandHeight;
        }
    }
    return height + kMaterialEditorGraphMenuPadding;
}

inline RECT MaterialEditorPanelRenderer::GraphContextMenuRect(const EditorSceneContext& sceneContext) {
    const int left = sceneContext.MaterialGraphContextMenuX();
    const int top = sceneContext.MaterialGraphContextMenuY();
    return RECT{
        left,
        top,
        left + kMaterialEditorGraphMenuWidth,
        top + MaterialEditorGraphContextMenuHeight(sceneContext),
    };
}

inline MaterialEditorGraphContextMenuHit MaterialEditorPanelRenderer::GraphContextMenuHit(const EditorSceneContext& sceneContext, int x, int y) {
    if (!sceneContext.IsMaterialGraphContextMenuOpen()) {
        return {};
    }
    const RECT menu = GraphContextMenuRect(sceneContext);
    if (!MaterialEditorPanelPointInRect(menu, x, y)) {
        return {};
    }
    const RECT searchRect{
        menu.left + kMaterialEditorGraphMenuPadding + 8,
        menu.top + kMaterialEditorGraphMenuPadding,
        menu.right - kMaterialEditorGraphMenuPadding - 8,
        menu.top + kMaterialEditorGraphMenuPadding + 22,
    };
    if (MaterialEditorPanelPointInRect(searchRect, x, y)) {
        return MaterialEditorGraphContextMenuHit{ .kind = MaterialEditorGraphContextMenuHitKind::Search };
    }
    int rowTop = menu.top + kMaterialEditorGraphMenuPadding + kMaterialEditorGraphMenuSearchHeight + kMaterialEditorGraphMenuPadding;
    if (MaterialEditorGraphContextMenuUsesFlatCommandList(sceneContext)) {
        const std::vector<MaterialEditorGraphMenuCommand> commands = MaterialEditorGraphContextMenuFilteredCommands(sceneContext);
        for (const MaterialEditorGraphMenuCommand command : commands) {
            const RECT commandRect{ menu.left, rowTop, menu.right, rowTop + kMaterialEditorGraphMenuCommandHeight };
            if (MaterialEditorPanelPointInRect(commandRect, x, y)) {
                const RECT favoriteRect{ menu.left + 10, rowTop + 4, menu.left + 24, rowTop + 18 };
                return MaterialEditorGraphContextMenuHit{
                    .kind = MaterialEditorPanelPointInRect(favoriteRect, x, y)
                        ? MaterialEditorGraphContextMenuHitKind::FavoriteToggle
                        : MaterialEditorGraphContextMenuHitKind::Command,
                    .command = command,
                };
            }
            rowTop += kMaterialEditorGraphMenuCommandHeight;
        }
        return {};
    }
    const std::vector<MaterialEditorGraphMenuCommand>& favorites = sceneContext.MaterialGraphPaletteFavoriteCommands();
    for (std::size_t categoryIndex = 0U; categoryIndex < MaterialEditorGraphContextMenuCategoryCount(); ++categoryIndex) {
        const RECT categoryRect{ menu.left, rowTop, menu.right, rowTop + kMaterialEditorGraphMenuCategoryHeight };
        if (MaterialEditorPanelPointInRect(categoryRect, x, y)) {
            return MaterialEditorGraphContextMenuHit{
                .kind = MaterialEditorGraphContextMenuHitKind::Category,
                .categoryIndex = categoryIndex,
            };
        }
        rowTop += kMaterialEditorGraphMenuCategoryHeight;
        if (!sceneContext.IsMaterialGraphContextMenuCategoryExpanded(categoryIndex)) {
            continue;
        }
        const std::vector<MaterialEditorGraphMenuCommand> commands = MaterialEditorGraphContextMenuCommands(categoryIndex, favorites);
        for (const MaterialEditorGraphMenuCommand command : commands) {
            const RECT commandRect{ menu.left, rowTop, menu.right, rowTop + kMaterialEditorGraphMenuCommandHeight };
            if (MaterialEditorPanelPointInRect(commandRect, x, y)) {
                const RECT favoriteRect{ menu.left + 10, rowTop + 4, menu.left + 24, rowTop + 18 };
                return MaterialEditorGraphContextMenuHit{
                    .kind = MaterialEditorPanelPointInRect(favoriteRect, x, y)
                        ? MaterialEditorGraphContextMenuHitKind::FavoriteToggle
                        : MaterialEditorGraphContextMenuHitKind::Command,
                    .categoryIndex = categoryIndex,
                    .command = command,
                };
            }
            rowTop += kMaterialEditorGraphMenuCommandHeight;
        }
    }
    return {};
}
#endif

} // namespace kb::editor
