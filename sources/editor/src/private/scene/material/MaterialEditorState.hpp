#pragma once

#include "scene/material/MaterialEditorModels.hpp"
#include "scene/material/MaterialEditorCanonicalCompare.hpp"

#include "inspection/InspectorPanelState.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"
#include "engine/assets/AssetId.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::editor {

class MaterialEditorState {
    struct GraphClipboard {
        std::vector<kb::render::RenderMaterialGraphNode> nodes;
        std::vector<kb::render::RenderMaterialGraphLink> links;
        std::vector<kb::render::RenderMaterialGraphParameterValue> parameterValues;
    };

public:
    [[nodiscard]] kb::assets::AssetId OpenAssetId() const noexcept {
        return openAssetId_;
    }

    [[nodiscard]] bool HasOpenAsset() const noexcept {
        return openAssetId_.IsValid();
    }

    [[nodiscard]] const std::optional<kb::render::RenderMaterialAssetData>& WorkingCopy() const noexcept {
        return workingCopy_;
    }

    [[nodiscard]] const std::optional<kb::render::RenderMaterialAssetData>& CleanSnapshot() const noexcept {
        return cleanSnapshot_;
    }

    [[nodiscard]] bool IsMaterialInstanceOpen() const noexcept {
        return instanceWorkingCopy_.has_value();
    }

    [[nodiscard]] const std::optional<kb::render::RenderMaterialInstanceAssetData>& InstanceWorkingCopy() const noexcept {
        return instanceWorkingCopy_;
    }

    [[nodiscard]] const std::optional<kb::render::RenderMaterialInstanceAssetData>& InstanceCleanSnapshot() const noexcept {
        return instanceCleanSnapshot_;
    }

    [[nodiscard]] const std::optional<kb::render::RenderMaterialAssetData>& InstanceParentSnapshot() const noexcept {
        return instanceParentSnapshot_;
    }

    [[nodiscard]] bool Dirty() const noexcept {
        return dirty_;
    }

    [[nodiscard]] std::vector<std::string> MaterialDiffRows() const;

    [[nodiscard]] const std::vector<std::string>& Diagnostics() const;

    // The graph node each diagnostic line points at, strictly parallel to Diagnostics() (0 = not node-tied).
    [[nodiscard]] const std::vector<std::uint32_t>& DiagnosticNodeIds() const;

    [[nodiscard]] bool DiagnosticsHaveError() const;

    [[nodiscard]] const std::vector<MaterialEditorGraphDiagnosticMarker>& GraphDiagnosticMarkers() const;

    [[nodiscard]] kb::render::RenderMaterialGraphRuntimeState GraphRuntimeState() const;

    [[nodiscard]] std::string_view GraphRuntimeStateName() const;

    [[nodiscard]] const MaterialEditorMaterialStatsModel& MaterialStats() const;

    [[nodiscard]] const MaterialEditorShaderViewerModel& ShaderViewer() const;

    [[nodiscard]] std::string_view FindQuery() const noexcept {
        return findQuery_;
    }

    [[nodiscard]] const std::vector<MaterialEditorFindResult>& FindResults() const;

    void SetFindQuery(std::string query);

    void AppendFindText(wchar_t character);

    void InsertFindText(std::string_view text);

    void BackspaceFind();

    void ClearFindQuery();

    [[nodiscard]] std::optional<MaterialEditorFindFocusTarget> FindResultFocusTarget(std::size_t index) const;

    [[nodiscard]] bool FocusFindResult(std::size_t index);

    [[nodiscard]] const std::vector<MaterialEditorParameter>& Parameters() const;

    [[nodiscard]] std::vector<MaterialEditorInstanceParentChainRow> InstanceParentChainRows() const;

    [[nodiscard]] std::vector<MaterialEditorInstanceOverrideGroupRow> InstanceOverrideGroups() const;

    [[nodiscard]] bool ToggleInstanceOverrideGroup(MaterialEditorParameterGroup group) noexcept;

    [[nodiscard]] std::vector<MaterialEditorInstanceStaticSwitchRow> InstanceStaticSwitchRows() const;

    [[nodiscard]] std::vector<MaterialEditorLayerTreeRow> LayerTreeRows() const;

    [[nodiscard]] bool AddLayerStackEntry(
        std::uint32_t nodeId,
        kb::render::RenderMaterialGraphLayerStackEntry entry,
        std::size_t index = static_cast<std::size_t>(-1));

    [[nodiscard]] bool SetLayerStackEntry(
        std::uint32_t nodeId,
        std::size_t index,
        kb::render::RenderMaterialGraphLayerStackEntry entry);

    [[nodiscard]] bool RemoveLayerStackEntry(std::uint32_t nodeId, std::size_t index);

    [[nodiscard]] bool ClearInstanceParameterOverride(
        std::string_view stableId,
        kb::render::RenderMaterialParameterType type);

    [[nodiscard]] bool SetInstanceStaticParameterOverride(
        std::string_view stableId,
        kb::render::RenderMaterialGraphNodeKind nodeKind,
        std::string value);

    [[nodiscard]] std::uint32_t SelectedNodeId() const noexcept {
        return selectedNodeId_;
    }

    [[nodiscard]] const std::vector<std::uint32_t>& SelectedNodeIds() const noexcept {
        return selectedNodeIds_;
    }

    [[nodiscard]] bool IsNodeSelected(std::uint32_t nodeId) const noexcept {
        return std::ranges::find(selectedNodeIds_, nodeId) != selectedNodeIds_.end();
    }

    [[nodiscard]] std::size_t SelectedNodeCount() const noexcept {
        return selectedNodeIds_.size();
    }

    [[nodiscard]] std::uint32_t SelectedCommentId() const noexcept {
        return selectedCommentId_;
    }

    [[nodiscard]] bool IsCommentSelected(std::uint32_t commentId) const noexcept {
        return commentId != 0U && selectedCommentId_ == commentId;
    }

    [[nodiscard]] bool HasGraphClipboard() const noexcept {
        return graphClipboard_.has_value() && !graphClipboard_->nodes.empty();
    }

    [[nodiscard]] InspectorPropertyId SelectedParameter() const noexcept {
        return selectedParameter_;
    }

    [[nodiscard]] bool InfoPanelVisible() const noexcept {
        return infoPanelVisible_;
    }

    [[nodiscard]] bool IsFindFocused() const noexcept {
        return findFocused_;
    }

    // Bumped by every mutation of the working copy. Lets the panel cache the built graph canvas between
    // pointer events instead of rebuilding it (nodes, pins, labels, links) several times per mouse move.
    [[nodiscard]] std::uint64_t DocumentRevision() const noexcept;

    [[nodiscard]] bool IsGraphConstantInlineEditing() const noexcept {
        return inlineConstantEditNodeId_ != 0U;
    }

    // Arming the edit prefills the buffer with the node's current value, so "editing" alone is not pending
    // work — only a buffer that differs from that prefill is something the user would lose.
    [[nodiscard]] bool IsGraphConstantInlineEditDirty() const noexcept;

    [[nodiscard]] bool IsGraphNodeRenameEditing() const noexcept {
        return renameNodeId_ != 0U;
    }

    [[nodiscard]] bool IsGraphConstantInlineEditing(std::uint32_t nodeId) const noexcept {
        return inlineConstantEditNodeId_ == nodeId && nodeId != 0U;
    }

    [[nodiscard]] bool IsGraphNodeRenameEditing(std::uint32_t nodeId) const noexcept {
        return renameNodeId_ == nodeId && nodeId != 0U;
    }

    [[nodiscard]] std::uint32_t GraphConstantInlineEditNodeId() const noexcept {
        return inlineConstantEditNodeId_;
    }

    [[nodiscard]] std::uint32_t GraphNodeRenameEditNodeId() const noexcept {
        return renameNodeId_;
    }

    [[nodiscard]] std::string_view GraphConstantInlineEditBuffer() const noexcept {
        return inlineConstantEditBuffer_;
    }

    [[nodiscard]] std::string_view GraphNodeRenameEditBuffer() const noexcept {
        return renameBuffer_;
    }

    [[nodiscard]] std::string GraphNodeDisplayName(std::uint32_t nodeId) const;

    [[nodiscard]] bool AddGraphNode(
        kb::render::RenderMaterialGraphNodeKind kind,
        std::int32_t positionX,
        std::int32_t positionY,
        std::uint32_t* createdNodeId = nullptr);

    [[nodiscard]] bool MoveGraphNode(std::uint32_t nodeId, std::int32_t positionX, std::int32_t positionY);

    [[nodiscard]] bool MoveGraphNodes(const std::vector<std::pair<std::uint32_t, std::pair<std::int32_t, std::int32_t>>>& positions);

    [[nodiscard]] bool SelectGraphUpstream() {
        return SelectGraphLinkedNodes(false);
    }

    [[nodiscard]] bool SelectGraphDownstream() {
        return SelectGraphLinkedNodes(true);
    }

    [[nodiscard]] bool AlignSelectedGraphNodes(MaterialEditorGraphAlignMode mode);

    [[nodiscard]] bool DistributeSelectedGraphNodes(MaterialEditorGraphDistributeAxis axis);

    [[nodiscard]] bool CanPromoteSelectedGraphNodeToParameter() const;

    [[nodiscard]] bool PromoteSelectedGraphNodeToParameter(std::uint32_t* promotedNodeId = nullptr);

    [[nodiscard]] bool SetGraphConstantValue(std::uint32_t nodeId, std::string_view valueText);

    [[nodiscard]] std::optional<float> GraphConstantComponentValue(std::uint32_t nodeId, std::size_t componentIndex) const;

    [[nodiscard]] std::optional<kb::render::RenderMaterialParameterRange> GraphConstantComponentRange(
        std::uint32_t nodeId,
        std::size_t componentIndex) const;

    [[nodiscard]] bool SetGraphConstantComponentValue(std::uint32_t nodeId, std::size_t componentIndex, float componentValue);

    [[nodiscard]] bool SetGraphConstantColorValue(std::uint32_t nodeId, const std::array<float, 4U>& color);

    [[nodiscard]] bool SetGraphNodeColorPropertyValue(
        std::uint32_t nodeId,
        std::string_view propertyId,
        const std::array<float, 4U>& color);

    [[nodiscard]] bool SetGraphNodeEnumValue(std::uint32_t nodeId, std::string_view propertyId, std::string_view value);

    [[nodiscard]] bool SetGraphNodeTextProperty(std::uint32_t nodeId, std::string_view propertyId, std::string_view value);

    [[nodiscard]] bool SetGraphMaterialFunctionCallSignature(
        std::uint32_t nodeId,
        std::uint64_t functionAssetId,
        const kb::render::RenderMaterialGraphDocument& functionGraph);

    [[nodiscard]] std::vector<MaterialEditorGraphNodeProperty> GraphNodeProperties(std::uint32_t nodeId) const;

    void ToggleGraphNodeEnumDropdown(std::uint32_t nodeId, std::string propertyId);

    void CloseGraphNodeEnumDropdown() noexcept;

    [[nodiscard]] bool IsGraphNodeEnumDropdownOpen(std::uint32_t nodeId, std::string_view propertyId) const noexcept;

    // Material-level settings (domain / shading model / blend mode) reuse the graph node enum-row machinery
    // with a pseudo node id of 0, so its dropdown-open flag needs its own slot - node id 0 is the "no node"
    // sentinel the node-keyed state deliberately refuses.
    [[nodiscard]] bool IsMaterialSettingDropdownOpen(std::string_view propertyId) const noexcept;

    void ToggleMaterialSettingDropdown(std::string propertyId);

    // The document is the single source of truth for these values; this returns the current ones as enum
    // rows for the Inspector. The set of *selectable* options is derived from the renderer's production
    // predicates (IsRenderMaterial*Production), not hard-coded here, so the editor can never offer a value
    // the compiler rejects and never drifts from the runtime.
    [[nodiscard]] std::vector<MaterialEditorGraphNodeProperty> MaterialSettingsProperties() const;

    [[nodiscard]] bool SetGraphMaterialSetting(std::string_view propertyId, std::string_view value);

    [[nodiscard]] bool BeginGraphConstantInlineEdit(std::uint32_t nodeId);

    void AppendGraphConstantInlineEditText(wchar_t character);

    void BackspaceGraphConstantInlineEdit();

    void CancelGraphConstantInlineEdit() noexcept;

    [[nodiscard]] bool BeginGraphNodeRenameEdit(std::uint32_t nodeId);

    void AppendGraphNodeRenameEditText(wchar_t character);

    void InsertGraphNodeRenameEditText(std::string_view text);

    void BackspaceGraphNodeRenameEdit();

    void ClearGraphNodeRenameEditText();

    void SelectAllGraphNodeRenameEditText() noexcept;

    void CancelGraphNodeRenameEdit() noexcept;

    [[nodiscard]] bool RenameGraphNode(std::uint32_t nodeId, std::string_view displayName);

    [[nodiscard]] bool DeleteGraphNode(std::uint32_t nodeId);

    [[nodiscard]] bool DeleteSelectedGraphNodes();

    [[nodiscard]] bool ConnectGraphPins(
        std::uint32_t fromNodeId,
        std::string_view fromPin,
        std::uint32_t toNodeId,
        std::string_view toPin);

    [[nodiscard]] bool DisconnectGraphInputPin(std::uint32_t toNodeId, std::string_view toPin);

    [[nodiscard]] bool DisconnectGraphOutputPin(std::uint32_t fromNodeId, std::string_view fromPin);

    [[nodiscard]] bool DisconnectGraphLink(std::uint32_t fromNodeId, std::string_view fromPin, std::uint32_t toNodeId, std::string_view toPin);

    [[nodiscard]] bool DisconnectGraphNodeLinks(std::uint32_t nodeId);

    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> GraphNodePosition(std::uint32_t nodeId) const;

    [[nodiscard]] bool AddGraphComment(
        std::string text,
        std::int32_t positionX,
        std::int32_t positionY,
        std::int32_t width = 320,
        std::int32_t height = 180,
        std::uint32_t color = 0x4A6385U,
        std::uint32_t* createdCommentId = nullptr);

    [[nodiscard]] std::optional<kb::render::RenderMaterialGraphCommentBox> GraphComment(std::uint32_t commentId) const;

    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> GraphCommentPosition(std::uint32_t commentId) const;

    [[nodiscard]] std::vector<std::uint32_t> GraphNodeIdsInsideComment(std::uint32_t commentId) const;

    [[nodiscard]] bool MoveGraphComment(std::uint32_t commentId, std::int32_t positionX, std::int32_t positionY);

    [[nodiscard]] bool MoveGraphCommentGroup(std::uint32_t commentId, std::int32_t positionX, std::int32_t positionY) {
        return MoveGraphCommentGroup(commentId, positionX, positionY, GraphNodeIdsInsideComment(commentId));
    }

    [[nodiscard]] bool MoveGraphCommentGroup(
        std::uint32_t commentId,
        std::int32_t positionX,
        std::int32_t positionY,
        std::span<const std::uint32_t> memberNodeIds);

    [[nodiscard]] bool SetGraphCommentText(std::uint32_t commentId, std::string_view text);

    [[nodiscard]] bool SetGraphCommentColor(std::uint32_t commentId, std::uint32_t color);

    [[nodiscard]] bool DeleteSelectedGraphComment();

    [[nodiscard]] bool AddGraphCompositeSubgraph(
        std::string name,
        std::int32_t positionX,
        std::int32_t positionY,
        std::int32_t width = 420,
        std::int32_t height = 260,
        std::vector<std::uint32_t> nodeIds = {},
        bool collapsed = false,
        std::uint32_t color = 0x425B4AU,
        std::uint32_t* createdCompositeId = nullptr);

    [[nodiscard]] bool CreateGraphCompositeFromSelection(
        std::string name,
        std::int32_t positionX,
        std::int32_t positionY,
        std::int32_t width = 420,
        std::int32_t height = 260,
        std::uint32_t* createdCompositeId = nullptr);

    [[nodiscard]] std::optional<kb::render::RenderMaterialGraphCompositeSubgraph> GraphCompositeSubgraph(std::uint32_t compositeId) const;

    [[nodiscard]] bool SetGraphCompositeCollapsed(std::uint32_t compositeId, bool collapsed);

    [[nodiscard]] bool ToggleGraphCompositeCollapsed(std::uint32_t compositeId);

    [[nodiscard]] bool CopySelectedGraphNodes();

    [[nodiscard]] bool PasteGraphClipboard(std::int32_t offsetX, std::int32_t offsetY, std::vector<std::uint32_t>* pastedNodeIds = nullptr);

    [[nodiscard]] bool DuplicateSelectedGraphNodes(std::int32_t offsetX, std::int32_t offsetY, std::vector<std::uint32_t>* pastedNodeIds = nullptr);

    void Open(
        kb::assets::AssetId assetId,
        std::optional<kb::render::RenderMaterialAssetData> document,
        std::optional<kb::render::RenderMaterialTypeSchema> schema = std::nullopt,
        std::optional<kb::render::RenderMaterialInstanceAssetData> instanceDocument = std::nullopt);

    void Close() noexcept;

    // Recomputes the dirty flag from the documents alone.
    //
    // Contract for the in-place mutators (MoveGraphNode/MoveGraphNodes/MoveGraphComment/MoveGraphCommentGroup
    // and the composite collapse): they write straight into workingCopy_ and deliberately do NOT recompute
    // dirty, refresh parameters, diagnostics or find results - a canonical compare of the whole document per
    // mouse-move during a drag is exactly the cost they exist to avoid. Anything that calls them owes the
    // state either a recorded edit (which routes through SetWorkingCopy) or a call to this.
    void RefreshDirty();

    void SetWorkingCopy(kb::render::RenderMaterialAssetData document);

    void SetInstanceWorkingCopy(
        kb::render::RenderMaterialInstanceAssetData instanceDocument,
        kb::render::RenderMaterialAssetData effectiveDocument);

    void MarkSaved();

    void RevertToCleanSnapshot();

    bool SelectNode(std::uint32_t nodeId);

    bool ClearNodeSelection() {
        return SelectNode(0U);
    }

    bool AddNodeToSelection(std::uint32_t nodeId);

    bool ToggleNodeSelection(std::uint32_t nodeId);

    bool SetNodeSelection(std::vector<std::uint32_t> nodeIds, std::uint32_t primaryNodeId = 0U);

    bool SelectComment(std::uint32_t commentId);

    bool ClearCommentSelection();

    bool SelectParameter(InspectorPropertyId property) noexcept;

    bool ToggleInfoPanel() noexcept;

    void SetInfoPanelVisible(bool visible) noexcept;

    void FocusFind(bool focused) noexcept;

    void SetDiagnostics(std::vector<std::string> diagnostics, bool hasError);

    void ApplyCookResult(
        std::vector<std::string> diagnostics,
        bool cookSucceeded,
        bool hasGpuProgram,
        bool hasLastGood,
        bool fallbackApplied);

    void ApplyCookMaterialStats(
        std::uint64_t sourceHash,
        std::string backendName,
        std::uint32_t textureBindingCount,
        std::uint32_t uniformCount,
        std::uint32_t varyingCount,
        std::vector<MaterialEditorCookPassTelemetry> passes);

    void ClearDiagnostics();

private:
    [[nodiscard]] static std::optional<kb::render::RenderMaterialGraphNodeKind> PromotedParameterKind(
        kb::render::RenderMaterialGraphNodeKind sourceKind) noexcept;

    [[nodiscard]] static std::string PromotedSourceOutputPin(kb::render::RenderMaterialGraphNodeKind sourceKind);

    [[nodiscard]] static std::string PromotedTargetOutputPin(kb::render::RenderMaterialGraphNodeKind targetKind);

    [[nodiscard]] static std::string PromotedParameterDefaultValueHint(
        const kb::render::RenderMaterialGraphNode& sourceNode,
        kb::render::RenderMaterialGraphNodeKind targetKind);

    [[nodiscard]] bool SelectGraphLinkedNodes(bool downstream);

    [[nodiscard]] static std::string GraphDiagnosticLine(const kb::render::RenderMaterialGraphDiagnostic& diagnostic);

    [[nodiscard]] static MaterialEditorParameterValue ScalarValue(float value);

    [[nodiscard]] static MaterialEditorParameterValue Vec3Value(const float value[3], MaterialEditorParameterValueKind kind = MaterialEditorParameterValueKind::Vec3);

    [[nodiscard]] static MaterialEditorParameterValue Vec4Value(const float value[4], MaterialEditorParameterValueKind kind = MaterialEditorParameterValueKind::Vec4);

    [[nodiscard]] static MaterialEditorParameterValue BoolValue(bool value);

    [[nodiscard]] static MaterialEditorParameterValue EnumValue(std::string value);

    // "defaultLit" -> "Default Lit", "alphaComposite" -> "Alpha Composite". Pure presentation, so a value
    // with no dedicated label still reads sensibly instead of showing a raw camelCase identifier.
    [[nodiscard]] static std::string PrettifyCanonicalName(std::string_view name);

    // The selectable options for a material setting, derived from the renderer enums filtered by the
    // production predicates - the compiler rejects non-production values, so they must never be offered.
    [[nodiscard]] static const std::vector<MaterialEditorGraphNodePropertyOption>& MaterialSettingOptions(
        std::string_view propertyId);

    // The label shown in the field: the option label when the stored value is one of them, otherwise the
    // prettified raw value plus "(unsupported)" so a hand-authored non-production value reads as the reason
    // its material fails to compile rather than looking like a normal choice.
    [[nodiscard]] static std::string MaterialSettingDisplayLabel(std::string_view propertyId, const std::string& value);

    [[nodiscard]] static MaterialEditorParameterValue TextureAssetValue(std::uint64_t assetId);

    [[nodiscard]] static std::string TrimAscii(std::string_view text);

    [[nodiscard]] static bool ParseDecimalAssetId(std::string_view text, std::uint64_t& output) noexcept;

    [[nodiscard]] static bool IsStableGraphIdentifier(std::string_view text) noexcept;

    [[nodiscard]] static std::vector<float> ParseDefaultNumbers(std::string_view text);

    [[nodiscard]] static bool IsGraphConstantNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept;

    [[nodiscard]] static std::string_view ConstantDisplayName(kb::render::RenderMaterialGraphNodeKind kind) noexcept;

    [[nodiscard]] static std::size_t ConstantComponentCount(kb::render::RenderMaterialGraphNodeKind kind) noexcept;

    [[nodiscard]] static std::optional<std::array<float, 4U>> ParseConstantValue(
        kb::render::RenderMaterialGraphNodeKind kind,
        std::string_view text);

    [[nodiscard]] static std::optional<bool> ParseConstantBool(std::string_view text) noexcept;

    [[nodiscard]] static std::string ConstantBoolDefaultValueHint(bool value) {
        return value ? "true" : "false";
    }

    [[nodiscard]] static std::string FloatText(float value);

    [[nodiscard]] static std::span<const GraphHintNumericPropertyDefinition> GraphHintNumericProperties(
        kb::render::RenderMaterialGraphNodeKind kind) noexcept;

    [[nodiscard]] static std::array<float, 4U> GraphHintNumericValues(const kb::render::RenderMaterialGraphNode& node);

    [[nodiscard]] static std::string GraphHintNumericValueHint(
        kb::render::RenderMaterialGraphNodeKind kind,
        const std::array<float, 4U>& values);

    [[nodiscard]] static std::string ConstantDefaultValueHint(
        kb::render::RenderMaterialGraphNodeKind kind,
        const std::array<float, 4U>& value);

    [[nodiscard]] static std::optional<kb::render::RenderMaterialParameterRange> ConstantRange(
        const kb::render::RenderMaterialGraphNode& node) noexcept;

    [[nodiscard]] static float ClampConstantComponentValue(
        const kb::render::RenderMaterialGraphNode& node,
        float value) noexcept;

    static void ClampConstantValues(
        const kb::render::RenderMaterialGraphNode& node,
        std::array<float, 4U>& values) noexcept;

    [[nodiscard]] static std::array<std::string_view, 4U> ConstantComponentLabels(
        kb::render::RenderMaterialGraphNodeKind kind) noexcept;

    [[nodiscard]] static std::string_view GraphNodeDisplayName(
        kb::render::RenderMaterialGraphNodeKind kind) noexcept;

    [[nodiscard]] static std::string GraphNodeDisplayNameForNode(const kb::render::RenderMaterialGraphNode& node);

    [[nodiscard]] static std::string NormalizeGraphNodeDisplayName(
        std::string_view displayName,
        kb::render::RenderMaterialGraphNodeKind kind);

    [[nodiscard]] static std::vector<MaterialEditorGraphNodePropertyOption> GraphNodeEnumOptions(
        kb::render::RenderMaterialGraphNodeKind kind,
        std::string_view propertyId);

    [[nodiscard]] static std::string GraphNodeUvSetValue(const kb::render::RenderMaterialGraphNode& node);

    [[nodiscard]] static bool ParseStaticBoolNodeValue(const kb::render::RenderMaterialGraphNode& node) noexcept {
        return node.parameter.defaultValueHint == "true" || node.parameter.defaultValueHint == "1";
    }

    [[nodiscard]] static bool IsStaticComponentMaskPropertyId(std::string_view propertyId) noexcept;

    [[nodiscard]] static char StaticComponentMaskPropertyChannel(std::string_view propertyId) noexcept;

    [[nodiscard]] static bool StaticComponentMaskChannelEnabled(
        const kb::render::RenderMaterialGraphNode& node,
        std::string_view propertyId) noexcept;

    [[nodiscard]] static std::string StaticComponentMaskHintWithChannel(
        const kb::render::RenderMaterialGraphNode& node,
        std::string_view propertyId,
        bool enabled);

    [[nodiscard]] static bool IsTransformSpaceNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept;

    [[nodiscard]] static bool IsTransformSpacePropertyId(std::string_view propertyId) noexcept {
        return propertyId == "transform.fromSpace" || propertyId == "transform.toSpace";
    }

    [[nodiscard]] static std::array<std::string, 2U> TransformSpaces(const kb::render::RenderMaterialGraphNode& node);

    [[nodiscard]] static bool IsTransformSpaceValue(std::string_view value) noexcept;

    [[nodiscard]] static std::string NormalizeTransformSpaceValue(std::string_view value);

    [[nodiscard]] static std::string TransformSpaceHintWithProperty(
        const kb::render::RenderMaterialGraphNode& node,
        std::string_view propertyId,
        std::string_view value);

    [[nodiscard]] static std::string TextureCoordinateHintWithUvSet(
        const kb::render::RenderMaterialGraphNode& node,
        std::string_view uvSetValue);

    [[nodiscard]] static std::array<float, 2U> TextureCoordinateTiling(const kb::render::RenderMaterialGraphNode& node);

    [[nodiscard]] static std::string TextureCoordinateHint(float uTile, float vTile, std::string_view uvSetValue) {
        return FloatText(uTile) + " " + FloatText(vTile) + " " + (uvSetValue == "1" ? "1" : "0");
    }

    [[nodiscard]] static std::string GraphNodeViewPropertyValue(const kb::render::RenderMaterialGraphNode& node);

    [[nodiscard]] static std::string GraphNodeSceneTextureSourceValue(const kb::render::RenderMaterialGraphNode& node);

    [[nodiscard]] static bool IsCustomCodeEditableValueType(kb::render::RenderMaterialGraphPinType type) noexcept;

    struct ColorRampStop {
        float position = 0.0F;
        float r = 0.0F;
        float g = 0.0F;
        float b = 0.0F;
    };

    [[nodiscard]] static std::vector<ColorRampStop> ColorRampStops(const kb::render::RenderMaterialGraphNode& node);

    [[nodiscard]] static std::string ColorRampHint(const std::vector<ColorRampStop>& stops);

    [[nodiscard]] static std::optional<std::size_t> ColorRampStopIndexForPositionComponent(std::size_t componentIndex) noexcept;

    [[nodiscard]] static std::optional<std::size_t> ColorRampStopIndexForColorProperty(std::string_view propertyId) noexcept;

    [[nodiscard]] static bool IsGraphTextureAssetNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept;

    [[nodiscard]] static std::uint64_t GraphNodeTextureAssetId(
        const kb::render::RenderMaterialGraphNode& node,
        const kb::render::RenderMaterialAssetData& document) noexcept;

    [[nodiscard]] static MaterialEditorParameterValue DefaultValueForSchema(
        const kb::render::RenderMaterialParameterSchema& parameter,
        const kb::render::RenderMaterialAssetData& fallbackDefaults);

    [[nodiscard]] static MaterialEditorParameterValue GraphParameterValueForSchema(
        const kb::render::RenderMaterialParameterSchema& parameter,
        const kb::render::RenderMaterialAssetData& document);

    [[nodiscard]] static std::string AlphaModeName(kb::render::RenderMaterialAlphaMode mode);

    [[nodiscard]] static std::string DecalBlendModeName(kb::render::RenderMaterialDecalBlendMode mode);

    [[nodiscard]] static std::string LayerBlendModeName(kb::render::RenderMaterialLayerBlendMode mode);

    [[nodiscard]] static MaterialEditorParameterGroup EditorGroupFor(kb::render::RenderMaterialParameterGroup group) noexcept;

    [[nodiscard]] static constexpr std::size_t MaterialEditorParameterGroupIndex(MaterialEditorParameterGroup group) noexcept {
        switch (group) {
        case MaterialEditorParameterGroup::Core:
            return 0U;
        case MaterialEditorParameterGroup::Surface:
            return 1U;
        case MaterialEditorParameterGroup::Texture:
            return 2U;
        case MaterialEditorParameterGroup::Advanced:
            return 3U;
        }
        return 3U;
    }

    [[nodiscard]] static bool IsStaticOverrideNodeKind(kb::render::RenderMaterialGraphNodeKind kind) noexcept;

    [[nodiscard]] static std::string StableIdForGraphNode(const kb::render::RenderMaterialGraphNode& node);

    [[nodiscard]] static std::string DefaultStaticOverrideValue(kb::render::RenderMaterialGraphNodeKind kind);

    [[nodiscard]] static bool IsStaticBoolText(std::string_view text) noexcept {
        return text == "true" || text == "false" || text == "1" || text == "0";
    }

    [[nodiscard]] static bool IsStaticMaskText(std::string_view text) noexcept;

    [[nodiscard]] static bool IsStaticOverrideValueValid(
        kb::render::RenderMaterialGraphNodeKind kind,
        std::string_view value) noexcept;

    [[nodiscard]] static const kb::render::RenderMaterialGraphNode* FindStaticOverrideNode(
        const kb::render::RenderMaterialAssetData& material,
        std::string_view stableId,
        kb::render::RenderMaterialGraphNodeKind nodeKind) noexcept;

    [[nodiscard]] static bool RemoveGraphParameterOverride(
        kb::render::RenderMaterialAssetData& material,
        std::string_view stableId,
        kb::render::RenderMaterialParameterType type);

    static void RemoveStaticOverride(
        kb::render::RenderMaterialInstanceAssetData& instance,
        std::string_view stableId,
        kb::render::RenderMaterialGraphNodeKind nodeKind);

    [[nodiscard]] static MaterialEditorParameterValue ParameterValueForField(
        std::string_view field,
        const kb::render::RenderMaterialAssetData& document,
        kb::render::RenderMaterialParameterType type);

    [[nodiscard]] static MaterialEditorParameterValue ParameterValueForSchema(
        const kb::render::RenderMaterialParameterSchema& parameter,
        const kb::render::RenderMaterialAssetData& document,
        const kb::render::RenderMaterialAssetData& fallbackDefaults);

    [[nodiscard]] static MaterialEditorParameterValue TextureValueForField(
        std::string_view field,
        const kb::render::RenderMaterialAssetData& document);

    [[nodiscard]] static std::string DisplayNameOrStableId(std::string_view displayName, std::string_view stableId) {
        return displayName.empty() ? std::string{ stableId } : std::string{ displayName };
    }

    [[nodiscard]] static std::vector<MaterialEditorParameter> BuildParameters(
        const kb::render::RenderMaterialAssetData& document,
        const kb::render::RenderMaterialTypeSchema& schema);

    [[nodiscard]] static bool InstanceHasGraphParameterOverride(
        const kb::render::RenderMaterialInstanceAssetData& instance,
        std::string_view stableId,
        kb::render::RenderMaterialParameterType type) noexcept;

    [[nodiscard]] static std::vector<MaterialEditorParameter> BuildInstanceParameters(
        const kb::render::RenderMaterialAssetData& effectiveDocument,
        const kb::render::RenderMaterialTypeSchema& schema,
        const kb::render::RenderMaterialAssetData& parentDocument,
        const kb::render::RenderMaterialInstanceAssetData& instanceDocument);

    static void EnsureEditableGraph(kb::render::RenderMaterialGraphDocument& graph);

    [[nodiscard]] static std::uint32_t NextGraphNodeId(const kb::render::RenderMaterialGraphDocument& graph) noexcept;

    [[nodiscard]] static kb::render::RenderMaterialGraphNode* FindMutableGraphNode(
        kb::render::RenderMaterialGraphDocument& graph,
        std::uint32_t nodeId) noexcept;

    [[nodiscard]] static std::uint32_t NextGraphCommentId(const kb::render::RenderMaterialGraphDocument& graph) noexcept;

    [[nodiscard]] static const kb::render::RenderMaterialGraphCommentBox* FindGraphComment(
        const kb::render::RenderMaterialGraphDocument& graph,
        std::uint32_t commentId) noexcept;

    [[nodiscard]] static kb::render::RenderMaterialGraphCommentBox* FindMutableGraphComment(
        kb::render::RenderMaterialGraphDocument& graph,
        std::uint32_t commentId) noexcept;

    [[nodiscard]] static std::uint32_t NextGraphCompositeId(const kb::render::RenderMaterialGraphDocument& graph) noexcept;

    [[nodiscard]] static const kb::render::RenderMaterialGraphCompositeSubgraph* FindGraphComposite(
        const kb::render::RenderMaterialGraphDocument& graph,
        std::uint32_t compositeId) noexcept;

    [[nodiscard]] static kb::render::RenderMaterialGraphCompositeSubgraph* FindMutableGraphComposite(
        kb::render::RenderMaterialGraphDocument& graph,
        std::uint32_t compositeId) noexcept;

    static void RemoveGraphCompositeNodeReferences(
        kb::render::RenderMaterialGraphDocument& graph,
        const std::vector<std::uint32_t>& nodeIds);

    [[nodiscard]] static bool GraphNodeInsideComment(
        const kb::render::RenderMaterialGraphNode& node,
        const kb::render::RenderMaterialGraphCommentBox& comment) noexcept;

    [[nodiscard]] static bool NodeIdInList(const std::vector<std::uint32_t>& nodeIds, std::uint32_t nodeId) noexcept {
        return std::ranges::find(nodeIds, nodeId) != nodeIds.end();
    }

    [[nodiscard]] static std::optional<GraphClipboard> BuildGraphClipboard(
        const kb::render::RenderMaterialAssetData& document,
        const std::vector<std::uint32_t>& selectedNodeIds);

    [[nodiscard]] static std::uint32_t RemapGraphNodeId(
        const std::vector<std::pair<std::uint32_t, std::uint32_t>>& remap,
        std::uint32_t nodeId) noexcept;

    void PruneSelectionToWorkingCopy();

    void ReplaceSelectedGraphNodeRenameText();

    [[nodiscard]] static kb::render::RenderMaterialGraphCustomCode DefaultCustomCode();

    [[nodiscard]] static kb::render::RenderMaterialGraphCustomCode DefaultMaterialFunctionCall();

    [[nodiscard]] static kb::render::RenderMaterialGraphParameterMetadata DefaultParameterMetadata(
        kb::render::RenderMaterialGraphNodeKind kind,
        std::uint32_t nodeId);

    [[nodiscard]] static std::string TextureDimensionName(kb::render::RenderMaterialGraphTextureDimension dimension);

    [[nodiscard]] static std::string TextureColorSpaceName(kb::render::RenderMaterialTextureColorSpace colorSpace);

    [[nodiscard]] static std::string SamplerFilterName(kb::render::RenderMaterialGraphSamplerFilter filter);

    [[nodiscard]] static std::string SamplerWrapName(kb::render::RenderMaterialGraphSamplerWrap wrap);

    [[nodiscard]] static std::string SamplerStateSummary(const kb::render::RenderMaterialGraphSamplerState& state);

    [[nodiscard]] static MaterialEditorShaderViewerModel ShaderViewerCompileFailure(
        const kb::render::RenderMaterialGraphCompileResult& compile);

    [[nodiscard]] static MaterialEditorShaderViewerModel BuildShaderViewer(
        const kb::render::RenderMaterialGraphCompileResult& compile);

    [[nodiscard]] static char LowerAscii(char ch) noexcept;

    [[nodiscard]] static bool ContainsCaseInsensitive(std::string_view text, std::string_view query) noexcept;

    [[nodiscard]] static bool AnyFindFieldMatches(
        std::string_view query,
        std::initializer_list<std::string_view> fields) noexcept;

    [[nodiscard]] static std::string NodeFindLabel(const kb::render::RenderMaterialGraphNode& node);

    [[nodiscard]] static std::int32_t NodeFindFocusX(const kb::render::RenderMaterialGraphNode& node) noexcept {
        return node.positionX + 120;
    }

    [[nodiscard]] static std::int32_t NodeFindFocusY(const kb::render::RenderMaterialGraphNode& node) noexcept {
        return node.positionY + 80;
    }

    void RefreshFindResults();

    // Everything a document change makes void. Cheap - it is all clears - so an edit still pays for it.
    void ClearDerivedGraphDiagnostics();

    // An edit invalidates the diagnostics; it does not recompute them. Recomputing meant running the graph
    // validator AND a full shader compile on the UI thread for every single edit - a mouse-move during a
    // drag, a keystroke in an inline value - even though nothing reads the result until the panel repaints.
    // The stale results are cleared immediately, so nothing on screen describes a document that no longer
    // exists; the expensive part happens on the next read.
    void InvalidateGraphDiagnostics();

    // Anything that reads diagnostic state calls this first, and so does anything that pushes cook or
    // external results IN - otherwise a deferred rebuild could land afterwards and wipe a fresh result.
    void EnsureGraphDiagnostics() const;

    void RefreshGraphDiagnostics();

    void ResetCookDiagnostics();

    void RefreshGraphRuntimeState();

    void RebuildMergedDiagnostics();

    // Derived data is invalidated on edit and rebuilt when something actually reads it.
    //
    // Rebuilding eagerly inside SetWorkingCopy meant every edit paid for the parameter list and the find
    // results whether or not anyone was looking - and during a drag or an inline-value edit nobody is: the
    // Inspector reads the parameters once per repaint, the find panel only while a query is open. The
    // rebuild bodies below are unchanged; only when they run has moved.
    void InvalidateParameters() noexcept;
    void InvalidateFindResults() noexcept { findResultsStale_ = true; }

    void EnsureParameters() const;

    void EnsureFindResults() const;

    void RefreshParameters();

    // The clean snapshot only changes on open/save/revert, so its canonical text is worth keeping: a dirty
    // check then serializes the working copy alone instead of both documents. Rebuilt lazily, because the
    // change and the next check are not always adjacent.
    void InvalidateCleanCanonical() noexcept;

    [[nodiscard]] const std::string& CleanCanonical() const;

    // Same answer EquivalentDocument(workingCopy_, cleanSnapshot_) gave - byte-identical canonical forms -
    // reached without building either string on the hot path.
    [[nodiscard]] bool WorkingCopyMatchesCleanSnapshot() const;

    [[nodiscard]] static std::string CanonicalDocument(const kb::render::RenderMaterialAssetData& document);

    [[nodiscard]] static bool EquivalentDocument(
        const std::optional<kb::render::RenderMaterialAssetData>& lhs,
        const std::optional<kb::render::RenderMaterialAssetData>& rhs);

    [[nodiscard]] static std::string CanonicalInstance(const kb::render::RenderMaterialInstanceAssetData& document);

    [[nodiscard]] static bool EquivalentInstance(
        const std::optional<kb::render::RenderMaterialInstanceAssetData>& lhs,
        const std::optional<kb::render::RenderMaterialInstanceAssetData>& rhs);

    kb::assets::AssetId openAssetId_{};
    std::optional<kb::render::RenderMaterialAssetData> workingCopy_;
    std::optional<kb::render::RenderMaterialAssetData> cleanSnapshot_;
    std::optional<kb::render::RenderMaterialInstanceAssetData> instanceWorkingCopy_;
    std::optional<kb::render::RenderMaterialInstanceAssetData> instanceCleanSnapshot_;
    std::optional<kb::render::RenderMaterialAssetData> instanceParentSnapshot_;
    kb::render::RenderMaterialTypeSchema activeSchema_ = kb::render::GetBuiltInPbrMaterialTypeSchema();
    std::array<bool, 4U> instanceOverrideGroupExpanded_{ true, true, true, true };
    std::vector<MaterialEditorParameter> parameters_;
    std::vector<std::string> diagnostics_;
    // The graph node each merged diagnostic line points at (0 when the line is not tied to a node). Kept
    // strictly parallel to diagnostics_ so the panel can jump to the offending node on click.
    std::vector<std::uint32_t> diagnosticNodeIds_;
    std::vector<std::string> graphDiagnosticsLines_;
    std::vector<std::uint32_t> graphDiagnosticLineNodeIds_;
    std::vector<std::string> compilerDiagnostics_;
    std::vector<std::uint32_t> compilerDiagnosticLineNodeIds_;
    std::vector<std::string> externalDiagnostics_;
    std::vector<std::string> cookDiagnostics_;
    std::vector<MaterialEditorGraphDiagnosticMarker> graphDiagnosticMarkers_;
    bool diagnosticsHaveError_ = false;
    bool graphDiagnosticsHaveError_ = false;
    bool compilerDiagnosticsHaveError_ = false;
    bool externalDiagnosticsHaveError_ = false;
    bool localCompileSucceeded_ = false;
    bool cookCompleted_ = false;
    bool cookSucceeded_ = false;
    bool cookHasGpuProgram_ = false;
    bool cookHasLastGood_ = false;
    bool cookFallbackApplied_ = false;
    MaterialEditorMaterialStatsModel materialStats_{};
    MaterialEditorShaderViewerModel shaderViewer_{};
    std::string findQuery_;
    std::vector<MaterialEditorFindResult> findResults_;
    bool findFocused_ = false;
    kb::render::RenderMaterialGraphRuntimeState graphRuntimeState_ = kb::render::RenderMaterialGraphRuntimeState::Dirty;
    bool dirty_ = false;
    mutable std::string cleanCanonical_;
    mutable bool cleanCanonicalValid_ = false;
    mutable bool parametersStale_ = false;
    mutable bool graphDiagnosticsStale_ = false;
    mutable bool findResultsStale_ = false;
    bool infoPanelVisible_ = false;
    std::uint32_t selectedNodeId_ = 0U;
    std::vector<std::uint32_t> selectedNodeIds_;
    std::uint32_t selectedCommentId_ = 0U;
    InspectorPropertyId selectedParameter_ = InspectorPropertyId::None;
    std::optional<GraphClipboard> graphClipboard_;
    std::uint64_t documentRevision_ = 1U;
    std::uint32_t inlineConstantEditNodeId_ = 0U;
    std::string inlineConstantEditBuffer_;
    std::string inlineConstantEditOriginal_;
    std::uint32_t renameNodeId_ = 0U;
    std::string renameBuffer_;
    bool renameSelectAll_ = false;
    std::uint32_t graphNodeEnumDropdownNodeId_ = 0U;
    std::string graphNodeEnumDropdownPropertyId_;
    std::string materialSettingDropdownPropertyId_;
};

} // namespace kb::editor
