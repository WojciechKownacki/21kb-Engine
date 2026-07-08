#include "scene/material/EditorMaterialAssetEditCommand.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/material/EditorMaterialAssetGateway.hpp"
#include "scene/material/MaterialEditorState.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] float Clamp01(float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float ClampNonNegative(float value) noexcept {
    return std::max(0.0F, value);
}

[[nodiscard]] std::uint64_t& TextureSlot(kb::render::RenderMaterialAssetData& asset, EditorMaterialTextureSlot slot) noexcept {
    switch (slot) {
    case EditorMaterialTextureSlot::Albedo:
        return asset.desc.albedoTextureAssetId;
    case EditorMaterialTextureSlot::Normal:
        return asset.desc.normalTextureAssetId;
    case EditorMaterialTextureSlot::MetallicRoughness:
        return asset.desc.metallicRoughnessTextureAssetId;
    case EditorMaterialTextureSlot::Occlusion:
        return asset.desc.occlusionTextureAssetId;
    case EditorMaterialTextureSlot::Emissive:
        return asset.desc.emissiveTextureAssetId;
    }
    return asset.desc.albedoTextureAssetId;
}

[[nodiscard]] bool IsMaterialGraphTextureObjectNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    return kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureObject ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureObjectCube ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume ||
        kind == kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray;
}

[[nodiscard]] bool IsMaterialGraphTextureValueNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    return kind == kb::render::RenderMaterialGraphNodeKind::TextureSample ||
        IsMaterialGraphTextureObjectNode(kind);
}

[[nodiscard]] const char* MaterialGraphTextureStableIdPrefix(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    if (kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture) {
        return "texture";
    }
    if (IsMaterialGraphTextureObjectNode(kind)) {
        return "textureObject";
    }
    return "textureSample";
}

void ConfigureNormalTextureValueNode(kb::render::RenderMaterialGraphNode& node) {
    if (node.parameter.stableId.empty()) {
        node.parameter.stableId = std::string{ MaterialGraphTextureStableIdPrefix(node.kind) } + std::to_string(node.id);
    }
    if (node.parameter.displayName.empty() || node.parameter.displayName.starts_with("Texture Sample") || node.parameter.displayName.starts_with("Texture Object")) {
        node.parameter.displayName = "Normal Texture";
    }
    node.parameter.textureRole = "normal";
    node.parameter.expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Linear;
    node.parameter.overrideSupported = true;
}

void UpsertGraphParameterValue(
    kb::render::RenderMaterialAssetData& material,
    kb::render::RenderMaterialGraphParameterValue value) {
    for (kb::render::RenderMaterialGraphParameterValue& existing : material.graphParameterValues) {
        if (existing.stableId == value.stableId) {
            existing = std::move(value);
            return;
        }
    }
    material.graphParameterValues.push_back(std::move(value));
}

[[nodiscard]] std::uint32_t NextMaterialGraphNodeId(const kb::render::RenderMaterialGraphDocument& graph) noexcept {
    std::uint32_t nextId = 1U;
    for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) {
        nextId = std::max(nextId, node.id + 1U);
    }
    return nextId;
}

[[nodiscard]] kb::render::RenderMaterialGraphNode* FindMutableMaterialGraphNode(
    kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId) noexcept {
    for (kb::render::RenderMaterialGraphNode& node : graph.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

[[nodiscard]] kb::render::RenderMaterialGraphNode* FindMutableMaterialOutputNode(
    kb::render::RenderMaterialGraphDocument& graph) noexcept {
    for (kb::render::RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            return &node;
        }
    }
    return nullptr;
}

[[nodiscard]] const kb::render::RenderMaterialGraphLink* FindMaterialGraphInputLink(
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    std::string_view pin) noexcept {
    for (const kb::render::RenderMaterialGraphLink& link : graph.links) {
        if (link.toNodeId == nodeId && link.toPin == pin) {
            return &link;
        }
    }
    return nullptr;
}

[[nodiscard]] std::uint32_t TextureValueNodeIdForSample(
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t textureSampleNodeId) noexcept {
    if (const kb::render::RenderMaterialGraphLink* textureInput = FindMaterialGraphInputLink(graph, textureSampleNodeId, "texture");
        textureInput != nullptr) {
        const kb::render::RenderMaterialGraphNode* sourceNode = kb::render::FindRenderMaterialGraphNode(graph, textureInput->fromNodeId);
        if (sourceNode != nullptr && IsMaterialGraphTextureObjectNode(sourceNode->kind)) {
            return sourceNode->id;
        }
    }
    return textureSampleNodeId;
}

void RemoveMaterialGraphInputLinks(
    kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    std::string_view pin) {
    const auto oldEnd = std::remove_if(graph.links.begin(), graph.links.end(), [nodeId, pin](const kb::render::RenderMaterialGraphLink& link) {
        return link.toNodeId == nodeId && link.toPin == pin;
    });
    graph.links.erase(oldEnd, graph.links.end());
}

[[nodiscard]] bool AddMaterialGraphStableLink(
    kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t fromNodeId,
    std::string fromPin,
    std::uint32_t toNodeId,
    std::string toPin) {
    const kb::render::RenderMaterialGraphNode* fromNode = kb::render::FindRenderMaterialGraphNode(graph, fromNodeId);
    const kb::render::RenderMaterialGraphNode* toNode = kb::render::FindRenderMaterialGraphNode(graph, toNodeId);
    if (fromNode == nullptr || toNode == nullptr) {
        return false;
    }

    kb::render::RenderMaterialGraphLink link{
        .fromNodeId = fromNodeId,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(*fromNode, fromPin, true),
        .fromPin = std::move(fromPin),
        .toNodeId = toNodeId,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(*toNode, toPin, false),
        .toPin = std::move(toPin),
    };
    if (link.fromPinId == 0U || link.toPinId == 0U) {
        return false;
    }
    link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
    graph.links.push_back(std::move(link));
    return true;
}

} // namespace

bool ApplyEditorMaterialOutputNormalTextureGraph(
    kb::render::RenderMaterialAssetData& material,
    kb::assets::AssetId textureId) {
    if (material.graph.nodes.empty()) {
        if (!textureId.IsValid()) {
            material.desc.normalTextureAssetId = 0U;
            return true;
        }
        material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    }

    kb::render::RenderMaterialGraphNode* outputNode = FindMutableMaterialOutputNode(material.graph);
    if (outputNode == nullptr) {
        if (!textureId.IsValid()) {
            material.desc.normalTextureAssetId = 0U;
            return true;
        }
        const std::uint32_t outputId = NextMaterialGraphNodeId(material.graph);
        material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
            .id = outputId,
            .kind = kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
            .positionX = 240,
            .positionY = 64,
        });
        outputNode = &material.graph.nodes.back();
    }

    const std::uint32_t outputNodeId = outputNode->id;
    const std::int32_t outputX = outputNode->positionX;
    const std::int32_t outputY = outputNode->positionY;
    std::uint32_t normalUnpackNodeId = 0U;
    std::uint32_t textureSampleNodeId = 0U;

    if (const kb::render::RenderMaterialGraphLink* normalInput = FindMaterialGraphInputLink(material.graph, outputNodeId, "normal");
        normalInput != nullptr) {
        const kb::render::RenderMaterialGraphNode* sourceNode = kb::render::FindRenderMaterialGraphNode(material.graph, normalInput->fromNodeId);
        if (sourceNode != nullptr &&
            sourceNode->kind == kb::render::RenderMaterialGraphNodeKind::NormalUnpack &&
            normalInput->fromPin == "normal") {
            normalUnpackNodeId = sourceNode->id;
        } else if (sourceNode != nullptr &&
            sourceNode->kind == kb::render::RenderMaterialGraphNodeKind::TextureSample &&
            normalInput->fromPin == "color") {
            textureSampleNodeId = sourceNode->id;
        }
    }

    if (normalUnpackNodeId != 0U) {
        if (const kb::render::RenderMaterialGraphLink* colorInput = FindMaterialGraphInputLink(material.graph, normalUnpackNodeId, "color");
            colorInput != nullptr) {
            const kb::render::RenderMaterialGraphNode* sampleNode = kb::render::FindRenderMaterialGraphNode(material.graph, colorInput->fromNodeId);
            if (sampleNode != nullptr &&
                sampleNode->kind == kb::render::RenderMaterialGraphNodeKind::TextureSample &&
                colorInput->fromPin == "color") {
                textureSampleNodeId = sampleNode->id;
            }
        }
    }

    if (!textureId.IsValid()) {
        if (textureSampleNodeId != 0U) {
            const std::uint32_t textureValueNodeId = TextureValueNodeIdForSample(material.graph, textureSampleNodeId);
            kb::render::RenderMaterialGraphNode* textureValueNode = FindMutableMaterialGraphNode(material.graph, textureValueNodeId);
            if (textureValueNode != nullptr && IsMaterialGraphTextureValueNode(textureValueNode->kind)) {
                ConfigureNormalTextureValueNode(*textureValueNode);
                UpsertGraphParameterValue(material, kb::render::RenderMaterialGraphParameterValue{
                    .stableId = textureValueNode->parameter.stableId,
                    .type = kb::render::RenderMaterialParameterType::Texture,
                    .assetId = 0U,
                });
            }
        }
        RemoveMaterialGraphInputLinks(material.graph, outputNodeId, "normal");
        material.desc.normalTextureAssetId = 0U;
        return true;
    }

    if (normalUnpackNodeId == 0U) {
        normalUnpackNodeId = NextMaterialGraphNodeId(material.graph);
        material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
            .id = normalUnpackNodeId,
            .kind = kb::render::RenderMaterialGraphNodeKind::NormalUnpack,
            .positionX = outputX - 240,
            .positionY = outputY + 96,
        });
    }

    if (textureSampleNodeId == 0U) {
        textureSampleNodeId = NextMaterialGraphNodeId(material.graph);
        material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
            .id = textureSampleNodeId,
            .kind = kb::render::RenderMaterialGraphNodeKind::TextureSample,
            .positionX = outputX - 520,
            .positionY = outputY + 88,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "textureSample" + std::to_string(textureSampleNodeId),
                .displayName = "Normal Texture",
                .textureRole = "normal",
                .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Linear,
                .overrideSupported = true,
            },
        });
    }

    kb::render::RenderMaterialGraphNode* textureSample = FindMutableMaterialGraphNode(material.graph, textureSampleNodeId);
    if (textureSample == nullptr || textureSample->kind != kb::render::RenderMaterialGraphNodeKind::TextureSample) {
        return false;
    }
    const std::uint32_t textureValueNodeId = TextureValueNodeIdForSample(material.graph, textureSampleNodeId);
    kb::render::RenderMaterialGraphNode* textureValueNode = FindMutableMaterialGraphNode(material.graph, textureValueNodeId);
    if (textureValueNode == nullptr || !IsMaterialGraphTextureValueNode(textureValueNode->kind)) {
        return false;
    }
    ConfigureNormalTextureValueNode(*textureValueNode);

    RemoveMaterialGraphInputLinks(material.graph, normalUnpackNodeId, "color");
    RemoveMaterialGraphInputLinks(material.graph, outputNodeId, "normal");
    if (!AddMaterialGraphStableLink(material.graph, textureSampleNodeId, "color", normalUnpackNodeId, "color") ||
        !AddMaterialGraphStableLink(material.graph, normalUnpackNodeId, "normal", outputNodeId, "normal")) {
        return false;
    }

    material.desc.normalTextureAssetId = textureId.value;
    UpsertGraphParameterValue(material, kb::render::RenderMaterialGraphParameterValue{
        .stableId = textureValueNode->parameter.stableId,
        .type = kb::render::RenderMaterialParameterType::Texture,
        .assetId = textureId.value,
    });
    return true;
}

EditorMaterialBaseColorChannelEdit::EditorMaterialBaseColorChannelEdit(int channel, float value) noexcept
    : channel_(channel)
    , value_(value) {}

std::string_view EditorMaterialBaseColorChannelEdit::Label() const noexcept {
    return "Edit Material Base Color";
}

void EditorMaterialBaseColorChannelEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    if (channel_ >= 0 && channel_ < 4) {
        asset.desc.baseColor[channel_] = Clamp01(value_);
    }
}

EditorMaterialEmissiveColorChannelEdit::EditorMaterialEmissiveColorChannelEdit(int channel, float value) noexcept
    : channel_(channel)
    , value_(value) {}

std::string_view EditorMaterialEmissiveColorChannelEdit::Label() const noexcept {
    return "Edit Material Emissive Color";
}

void EditorMaterialEmissiveColorChannelEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    if (channel_ >= 0 && channel_ < 3) {
        asset.desc.emissiveColor[channel_] = ClampNonNegative(value_);
    }
}

EditorMaterialMetallicFactorEdit::EditorMaterialMetallicFactorEdit(float value) noexcept
    : value_(value) {}

std::string_view EditorMaterialMetallicFactorEdit::Label() const noexcept {
    return "Edit Material Metallic";
}

void EditorMaterialMetallicFactorEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.metallicFactor = Clamp01(value_);
}

EditorMaterialRoughnessFactorEdit::EditorMaterialRoughnessFactorEdit(float value) noexcept
    : value_(value) {}

std::string_view EditorMaterialRoughnessFactorEdit::Label() const noexcept {
    return "Edit Material Roughness";
}

void EditorMaterialRoughnessFactorEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.roughnessFactor = Clamp01(value_);
}

EditorMaterialNormalScaleEdit::EditorMaterialNormalScaleEdit(float value) noexcept
    : value_(value) {}

std::string_view EditorMaterialNormalScaleEdit::Label() const noexcept {
    return "Edit Material Normal Scale";
}

void EditorMaterialNormalScaleEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.normalScale = ClampNonNegative(value_);
}

EditorMaterialOcclusionStrengthEdit::EditorMaterialOcclusionStrengthEdit(float value) noexcept
    : value_(value) {}

std::string_view EditorMaterialOcclusionStrengthEdit::Label() const noexcept {
    return "Edit Material Occlusion";
}

void EditorMaterialOcclusionStrengthEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.occlusionStrength = Clamp01(value_);
}

EditorMaterialEmissiveStrengthEdit::EditorMaterialEmissiveStrengthEdit(float value) noexcept
    : value_(value) {}

std::string_view EditorMaterialEmissiveStrengthEdit::Label() const noexcept {
    return "Edit Material Emissive Strength";
}

void EditorMaterialEmissiveStrengthEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.emissiveStrength = ClampNonNegative(value_);
}

EditorMaterialAlphaCutoffEdit::EditorMaterialAlphaCutoffEdit(float value) noexcept
    : value_(value) {}

std::string_view EditorMaterialAlphaCutoffEdit::Label() const noexcept {
    return "Edit Material Alpha Cutoff";
}

void EditorMaterialAlphaCutoffEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.alphaCutoff = Clamp01(value_);
}

EditorMaterialAlphaModeEdit::EditorMaterialAlphaModeEdit(kb::render::RenderMaterialAlphaMode mode) noexcept
    : mode_(mode) {}

std::string_view EditorMaterialAlphaModeEdit::Label() const noexcept {
    return "Edit Material Alpha Mode";
}

void EditorMaterialAlphaModeEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.alphaMode = mode_;
}

EditorMaterialDoubleSidedEdit::EditorMaterialDoubleSidedEdit(bool doubleSided) noexcept
    : doubleSided_(doubleSided) {}

std::string_view EditorMaterialDoubleSidedEdit::Label() const noexcept {
    return "Edit Material Double Sided";
}

void EditorMaterialDoubleSidedEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.doubleSided = doubleSided_;
}

EditorMaterialTextureAssetEdit::EditorMaterialTextureAssetEdit(EditorMaterialTextureSlot slot, kb::assets::AssetId textureId) noexcept
    : slot_(slot)
    , textureId_(textureId) {}

std::string_view EditorMaterialTextureAssetEdit::Label() const noexcept {
    return "Edit Material Texture";
}

void EditorMaterialTextureAssetEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    TextureSlot(asset, slot_) = textureId_.value;
}

std::unique_ptr<EditorMaterialAssetEditCommand> EditorMaterialAssetEditCommand::Create(
    kb::scene::Scene& scene,
    kb::assets::AssetId materialId,
    std::unique_ptr<IEditorMaterialAssetPropertyEdit> edit) {
    if (edit == nullptr) {
        return {};
    }

    std::optional<kb::render::RenderMaterialAssetData> before = EditorMaterialAssetGateway::Read(scene, materialId);
    if (!before.has_value()) {
        return {};
    }

    kb::render::RenderMaterialAssetData after = *before;
    edit->Apply(after);
    return std::unique_ptr<EditorMaterialAssetEditCommand>{ new EditorMaterialAssetEditCommand{
        scene,
        materialId,
        std::string{ edit->Label() },
        std::move(*before),
        std::move(after),
    } };
}

std::unique_ptr<EditorMaterialAssetEditCommand> EditorMaterialAssetEditCommand::CreateRecorded(
    kb::scene::Scene& scene,
    kb::assets::AssetId materialId,
    std::string label,
    kb::render::RenderMaterialAssetData before,
    kb::render::RenderMaterialAssetData after) {
    return std::unique_ptr<EditorMaterialAssetEditCommand>{ new EditorMaterialAssetEditCommand{
        scene,
        materialId,
        std::move(label),
        std::move(before),
        std::move(after),
    } };
}

EditorMaterialAssetEditCommand::EditorMaterialAssetEditCommand(
    kb::scene::Scene& scene,
    kb::assets::AssetId materialId,
    std::string label,
    kb::render::RenderMaterialAssetData before,
    kb::render::RenderMaterialAssetData after)
    : scene_(scene)
    , materialId_(materialId)
    , label_(std::move(label))
    , before_(std::move(before))
    , after_(std::move(after)) {}

std::string_view EditorMaterialAssetEditCommand::Label() const noexcept {
    return label_;
}

bool EditorMaterialAssetEditCommand::AffectsSceneDocument() const noexcept {
    return false;
}

bool EditorMaterialAssetEditCommand::AffectsHierarchySelection() const noexcept {
    return false;
}

bool EditorMaterialAssetEditCommand::AffectsOpenMaterialSource() const noexcept {
    return true;
}

bool EditorMaterialAssetEditCommand::Execute() {
    return Write(after_);
}

bool EditorMaterialAssetEditCommand::Undo() {
    return Write(before_);
}

bool EditorMaterialAssetEditCommand::Redo() {
    return Write(after_);
}

bool EditorMaterialAssetEditCommand::Write(const kb::render::RenderMaterialAssetData& asset) {
    return EditorMaterialAssetGateway::WriteExisting(scene_, materialId_, asset);
}

std::unique_ptr<EditorMaterialInstanceEditCommand> EditorMaterialInstanceEditCommand::CreateRecorded(
    kb::scene::Scene& scene,
    kb::assets::AssetId materialInstanceId,
    std::string label,
    kb::render::RenderMaterialInstanceAssetData before,
    kb::render::RenderMaterialInstanceAssetData after) {
    return std::unique_ptr<EditorMaterialInstanceEditCommand>{ new EditorMaterialInstanceEditCommand{
        scene,
        materialInstanceId,
        std::move(label),
        std::move(before),
        std::move(after),
    } };
}

EditorMaterialInstanceEditCommand::EditorMaterialInstanceEditCommand(
    kb::scene::Scene& scene,
    kb::assets::AssetId materialInstanceId,
    std::string label,
    kb::render::RenderMaterialInstanceAssetData before,
    kb::render::RenderMaterialInstanceAssetData after)
    : scene_(scene)
    , materialInstanceId_(materialInstanceId)
    , label_(std::move(label))
    , before_(std::move(before))
    , after_(std::move(after)) {}

std::string_view EditorMaterialInstanceEditCommand::Label() const noexcept {
    return label_;
}

bool EditorMaterialInstanceEditCommand::AffectsSceneDocument() const noexcept {
    return false;
}

bool EditorMaterialInstanceEditCommand::AffectsHierarchySelection() const noexcept {
    return false;
}

bool EditorMaterialInstanceEditCommand::AffectsOpenMaterialSource() const noexcept {
    return true;
}

bool EditorMaterialInstanceEditCommand::Execute() {
    return Write(after_);
}

bool EditorMaterialInstanceEditCommand::Undo() {
    return Write(before_);
}

bool EditorMaterialInstanceEditCommand::Redo() {
    return Write(after_);
}

bool EditorMaterialInstanceEditCommand::Write(const kb::render::RenderMaterialInstanceAssetData& asset) {
    return EditorMaterialAssetGateway::WriteExistingInstance(scene_, materialInstanceId_, asset);
}

std::unique_ptr<EditorMaterialWorkingCopyEditCommand> EditorMaterialWorkingCopyEditCommand::Create(
    MaterialEditorState& editor,
    kb::assets::AssetId materialId,
    std::string label,
    kb::render::RenderMaterialAssetData before,
    kb::render::RenderMaterialAssetData after,
    std::uint32_t beforeSelectedNodeId,
    std::uint32_t afterSelectedNodeId) {
    std::vector<std::uint32_t> beforeSelectedNodeIds;
    if (beforeSelectedNodeId != 0U) {
        beforeSelectedNodeIds.push_back(beforeSelectedNodeId);
    }
    std::vector<std::uint32_t> afterSelectedNodeIds;
    if (afterSelectedNodeId != 0U) {
        afterSelectedNodeIds.push_back(afterSelectedNodeId);
    }
    return Create(
        editor,
        materialId,
        std::move(label),
        std::move(before),
        std::move(after),
        std::move(beforeSelectedNodeIds),
        std::move(afterSelectedNodeIds),
        0U,
        0U);
}

std::unique_ptr<EditorMaterialWorkingCopyEditCommand> EditorMaterialWorkingCopyEditCommand::Create(
    MaterialEditorState& editor,
    kb::assets::AssetId materialId,
    std::string label,
    kb::render::RenderMaterialAssetData before,
    kb::render::RenderMaterialAssetData after,
    std::vector<std::uint32_t> beforeSelectedNodeIds,
    std::vector<std::uint32_t> afterSelectedNodeIds,
    std::uint32_t beforeSelectedCommentId,
    std::uint32_t afterSelectedCommentId) {
    return std::unique_ptr<EditorMaterialWorkingCopyEditCommand>{ new EditorMaterialWorkingCopyEditCommand{
        editor,
        materialId,
        std::move(label),
        std::move(before),
        std::move(after),
        std::move(beforeSelectedNodeIds),
        std::move(afterSelectedNodeIds),
        beforeSelectedCommentId,
        afterSelectedCommentId,
    } };
}

EditorMaterialWorkingCopyEditCommand::EditorMaterialWorkingCopyEditCommand(
    MaterialEditorState& editor,
    kb::assets::AssetId materialId,
    std::string label,
    kb::render::RenderMaterialAssetData before,
    kb::render::RenderMaterialAssetData after,
    std::vector<std::uint32_t> beforeSelectedNodeIds,
    std::vector<std::uint32_t> afterSelectedNodeIds,
    std::uint32_t beforeSelectedCommentId,
    std::uint32_t afterSelectedCommentId)
    : editor_(editor)
    , materialId_(materialId)
    , label_(std::move(label))
    , before_(std::move(before))
    , after_(std::move(after))
    , beforeSelectedNodeIds_(std::move(beforeSelectedNodeIds))
    , afterSelectedNodeIds_(std::move(afterSelectedNodeIds))
    , beforeSelectedCommentId_(beforeSelectedCommentId)
    , afterSelectedCommentId_(afterSelectedCommentId) {}

std::string_view EditorMaterialWorkingCopyEditCommand::Label() const noexcept {
    return label_;
}

bool EditorMaterialWorkingCopyEditCommand::AffectsSceneDocument() const noexcept {
    return false;
}

bool EditorMaterialWorkingCopyEditCommand::AffectsHierarchySelection() const noexcept {
    return false;
}

bool EditorMaterialWorkingCopyEditCommand::Execute() {
    return Apply(after_, afterSelectedNodeIds_, afterSelectedCommentId_);
}

bool EditorMaterialWorkingCopyEditCommand::Undo() {
    return Apply(before_, beforeSelectedNodeIds_, beforeSelectedCommentId_);
}

bool EditorMaterialWorkingCopyEditCommand::Redo() {
    return Apply(after_, afterSelectedNodeIds_, afterSelectedCommentId_);
}

bool EditorMaterialWorkingCopyEditCommand::Apply(
    const kb::render::RenderMaterialAssetData& asset,
    const std::vector<std::uint32_t>& selectedNodeIds,
    std::uint32_t selectedCommentId) {
    if (editor_.OpenAssetId() != materialId_ || !editor_.WorkingCopy().has_value()) {
        return false;
    }

    editor_.SetWorkingCopy(asset);
    static_cast<void>(editor_.SetNodeSelection(selectedNodeIds, selectedNodeIds.empty() ? 0U : selectedNodeIds.back()));
    if (selectedCommentId != 0U) {
        static_cast<void>(editor_.SelectComment(selectedCommentId));
    } else {
        static_cast<void>(editor_.ClearCommentSelection());
    }
    return true;
}

} // namespace kb::editor
