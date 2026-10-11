#pragma once

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kb::render {

// Document-level material authoring shared by the editor's Material Editor and kb_cli, so both
// add, connect, set and promote graph nodes and set parameter values and instance overrides in
// exactly one way (the editor keeps only its selection, undo and preview around these).

// Defaults a newly added node of `kind` gets (stable id, display name, value, range).
[[nodiscard]] RenderMaterialGraphParameterMetadata DefaultRenderMaterialGraphParameterMetadata(
    RenderMaterialGraphNodeKind kind,
    std::uint32_t nodeId);
[[nodiscard]] RenderMaterialGraphCustomCode DefaultRenderMaterialGraphCustomCode();
[[nodiscard]] RenderMaterialGraphCustomCode DefaultRenderMaterialGraphFunctionCall();
// A material without graph nodes gets the default graph (its Material Output) before an edit.
void EnsureEditableRenderMaterialGraph(RenderMaterialGraphDocument& graph);
[[nodiscard]] std::uint32_t NextRenderMaterialGraphNodeId(const RenderMaterialGraphDocument& graph) noexcept;
[[nodiscard]] RenderMaterialGraphNode* FindMutableRenderMaterialGraphNode(RenderMaterialGraphDocument& graph, std::uint32_t nodeId) noexcept;

// Constant nodes (Scalar, Bool, XY, RGB, RGBA): their value is the node's defaultValueHint.
[[nodiscard]] bool IsRenderMaterialGraphConstantNode(RenderMaterialGraphNodeKind kind) noexcept;
[[nodiscard]] std::size_t RenderMaterialGraphConstantComponentCount(RenderMaterialGraphNodeKind kind) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphConstantDisplayName(RenderMaterialGraphNodeKind kind) noexcept;
[[nodiscard]] std::optional<bool> ParseRenderMaterialGraphConstantBool(std::string_view text) noexcept;
[[nodiscard]] std::optional<std::array<float, 4U>> ParseRenderMaterialGraphConstantValue(
    RenderMaterialGraphNodeKind kind,
    std::string_view text);
[[nodiscard]] std::string RenderMaterialGraphFloatText(float value);
[[nodiscard]] std::string RenderMaterialGraphConstantValueHint(RenderMaterialGraphNodeKind kind, const std::array<float, 4U>& value);
[[nodiscard]] std::optional<RenderMaterialParameterRange> RenderMaterialGraphConstantRange(const RenderMaterialGraphNode& node) noexcept;
void ClampRenderMaterialGraphConstantValues(const RenderMaterialGraphNode& node, std::array<float, 4U>& values) noexcept;

// Promote to Parameter (Unreal's Convert to Parameter): a constant becomes the parameter node of
// its type and keeps its value and links.
[[nodiscard]] std::optional<RenderMaterialGraphNodeKind> RenderMaterialGraphPromotedParameterKind(RenderMaterialGraphNodeKind sourceKind) noexcept;
[[nodiscard]] std::string RenderMaterialGraphPromotedSourceOutputPin(RenderMaterialGraphNodeKind sourceKind);
[[nodiscard]] std::string RenderMaterialGraphPromotedTargetOutputPin(RenderMaterialGraphNodeKind targetKind);
[[nodiscard]] std::string RenderMaterialGraphPromotedParameterDefaultValueHint(
    const RenderMaterialGraphNode& sourceNode,
    RenderMaterialGraphNodeKind targetKind);

// Graph edits; each returns false and leaves the graph unchanged when it is refused.
[[nodiscard]] std::optional<std::uint32_t> AddRenderMaterialGraphNode(
    RenderMaterialGraphDocument& graph,
    RenderMaterialGraphNodeKind kind,
    std::int32_t positionX,
    std::int32_t positionY);
// Links an output pin to an input pin of compatible types; an input takes one link, so an older
// link into the same input is replaced.
[[nodiscard]] bool ConnectRenderMaterialGraphPins(
    RenderMaterialGraphDocument& graph,
    std::uint32_t fromNodeId,
    std::string_view fromPin,
    std::uint32_t toNodeId,
    std::string_view toPin);
// The value of a constant node, clamped to its range; `normalizedHint` receives the stored text.
[[nodiscard]] bool SetRenderMaterialGraphConstantValue(
    RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    std::string_view valueText,
    std::string* normalizedHint = nullptr);
[[nodiscard]] bool PromoteRenderMaterialGraphNodeToParameter(RenderMaterialGraphDocument& graph, std::uint32_t nodeId);
// Trimmed and at most 64 characters; empty becomes `fallbackName`.
[[nodiscard]] std::string NormalizeRenderMaterialGraphNodeDisplayName(std::string_view displayName, std::string_view fallbackName);
[[nodiscard]] bool RenameRenderMaterialGraphNode(
    RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    std::string_view displayName,
    std::string_view fallbackName);

// Texture Sample, Texture Object and Texture parameter nodes: the nodes a texture asset is given to.
[[nodiscard]] bool IsRenderMaterialGraphTextureAssetNode(RenderMaterialGraphNodeKind kind) noexcept;
// Gives a texture to such a node, as the Material Editor's texture picker does: the node gets
// its stable id, name, role and colour space defaults, and the material's value of that texture
// parameter becomes `textureAssetId` (0 clears it).
[[nodiscard]] bool SetRenderMaterialGraphTextureNodeAsset(
    RenderMaterialAssetData& material,
    std::uint32_t nodeId,
    std::uint64_t textureAssetId);

// A material's own value of a graph parameter (graphParameterValues), as the Material Editor's
// parameter rows and an instance's overrides store it. Texture values are asset ids and are not
// parsed from text here.
[[nodiscard]] std::optional<RenderMaterialGraphParameterValue> ParseRenderMaterialGraphParameterValue(
    std::string_view stableId,
    RenderMaterialParameterType type,
    std::string_view text);
void UpsertRenderMaterialGraphParameterValue(RenderMaterialAssetData& material, RenderMaterialGraphParameterValue value);
void RemoveRenderMaterialGraphParameterValue(RenderMaterialAssetData& material, std::string_view stableId);

// Material instances: the override document follows the parent's type, and one override is
// set only when the instance stays valid against its parent.
void EnsureRenderMaterialInstanceOverrideDocument(
    RenderMaterialInstanceAssetData& instance,
    const RenderMaterialAssetData& parentMaterial);
[[nodiscard]] RenderMaterialInstanceValidationResult SetRenderMaterialInstanceParameterOverride(
    RenderMaterialInstanceAssetData& instance,
    const RenderMaterialAssetData& parentMaterial,
    RenderMaterialGraphParameterValue value);

} // namespace kb::render
