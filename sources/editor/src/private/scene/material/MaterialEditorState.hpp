#pragma once

#include "inspection/InspectorPanelState.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"
#include "engine/assets/AssetId.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace kb::editor {

enum class MaterialEditorParameterValueKind : std::uint8_t {
    None,
    Scalar,
    Vec3,
    Vec4,
    Color,
    Enum,
    Bool,
    TextureAsset,
};

enum class MaterialEditorParameterGroup : std::uint8_t {
    Core,
    Surface,
    Texture,
    Advanced,
};

enum class MaterialEditorGraphMenuCommand : std::uint8_t {
    None,
    CreateTextureSample,
    CreateTextureParameter,
    CreateUv,
    CreateScalar,
    CreateVector,
    CreateColor,
    CreateScalarParameter,
    CreateVectorParameter,
    CreateColorParameter,
    CreateAdd,
    CreateMultiply,
    CreateClamp,
    CreateLerp,
    CreateNormalUnpack,
    DisconnectSelected,
    DeleteSelected,
};

struct MaterialEditorParameterValue {
    MaterialEditorParameterValueKind kind = MaterialEditorParameterValueKind::None;
    std::array<float, 4U> numbers{};
    std::uint64_t assetId = 0;
    bool boolValue = false;
    std::string text;
};

struct MaterialEditorParameter {
    std::string stableId;
    kb::render::RenderMaterialParameterType type = kb::render::RenderMaterialParameterType::Scalar;
    MaterialEditorParameterGroup group = MaterialEditorParameterGroup::Core;
    std::string displayName;
    std::string description;
    MaterialEditorParameterValue value{};
    MaterialEditorParameterValue defaultValue{};
    std::optional<kb::render::RenderMaterialParameterRange> range;
    std::optional<kb::render::RenderMaterialTextureColorSpace> expectedTextureColorSpace;
    bool overrideEnabled = true;
    bool enabled = true;
    std::uint32_t sortOrder = 0U;
};

class MaterialEditorState {
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

    [[nodiscard]] bool Dirty() const noexcept {
        return dirty_;
    }

    [[nodiscard]] const std::vector<std::string>& Diagnostics() const noexcept {
        return diagnostics_;
    }

    [[nodiscard]] bool DiagnosticsHaveError() const noexcept {
        return diagnosticsHaveError_;
    }

    [[nodiscard]] const std::vector<MaterialEditorParameter>& Parameters() const noexcept {
        return parameters_;
    }

    [[nodiscard]] std::uint32_t SelectedNodeId() const noexcept {
        return selectedNodeId_;
    }

    [[nodiscard]] InspectorPropertyId SelectedParameter() const noexcept {
        return selectedParameter_;
    }

    [[nodiscard]] bool InfoPanelVisible() const noexcept {
        return infoPanelVisible_;
    }

    [[nodiscard]] bool AddGraphNode(
        kb::render::RenderMaterialGraphNodeKind kind,
        std::int32_t positionX,
        std::int32_t positionY,
        std::uint32_t* createdNodeId = nullptr) {
        if (!workingCopy_.has_value() || kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        EnsureEditableGraph(document.graph);
        const std::uint32_t nodeId = NextGraphNodeId(document.graph);
        document.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
            .id = nodeId,
            .kind = kind,
            .positionX = positionX,
            .positionY = positionY,
            .parameter = DefaultParameterMetadata(kind, nodeId),
        });
        if (createdNodeId != nullptr) {
            *createdNodeId = nodeId;
        }
        SetWorkingCopy(std::move(document));
        SelectNode(nodeId);
        return true;
    }

    [[nodiscard]] bool MoveGraphNode(std::uint32_t nodeId, std::int32_t positionX, std::int32_t positionY) {
        if (!workingCopy_.has_value() || nodeId == 0U) {
            return false;
        }

        kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(workingCopy_->graph, nodeId);
        if (node == nullptr || (node->positionX == positionX && node->positionY == positionY)) {
            return false;
        }
        node->positionX = positionX;
        node->positionY = positionY;
        return true;
    }

    [[nodiscard]] bool DeleteGraphNode(std::uint32_t nodeId) {
        if (!workingCopy_.has_value() || nodeId == 0U) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(document.graph, nodeId);
        if (node == nullptr || node->kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            return false;
        }

        const auto nodeEnd = std::remove_if(document.graph.nodes.begin(), document.graph.nodes.end(), [nodeId](const kb::render::RenderMaterialGraphNode& graphNode) {
            return graphNode.id == nodeId;
        });
        document.graph.nodes.erase(nodeEnd, document.graph.nodes.end());
        const auto linkEnd = std::remove_if(document.graph.links.begin(), document.graph.links.end(), [nodeId](const kb::render::RenderMaterialGraphLink& link) {
            return link.fromNodeId == nodeId || link.toNodeId == nodeId;
        });
        document.graph.links.erase(linkEnd, document.graph.links.end());
        SetWorkingCopy(std::move(document));
        ClearNodeSelection();
        return true;
    }

    [[nodiscard]] bool ConnectGraphPins(
        std::uint32_t fromNodeId,
        std::string_view fromPin,
        std::uint32_t toNodeId,
        std::string_view toPin) {
        if (!workingCopy_.has_value() || fromNodeId == 0U || toNodeId == 0U || fromNodeId == toNodeId) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        EnsureEditableGraph(document.graph);
        const kb::render::RenderMaterialGraphNode* fromNode = kb::render::FindRenderMaterialGraphNode(document.graph, fromNodeId);
        const kb::render::RenderMaterialGraphNode* toNode = kb::render::FindRenderMaterialGraphNode(document.graph, toNodeId);
        if (fromNode == nullptr || toNode == nullptr ||
            !kb::render::IsRenderMaterialGraphOutputPin(fromNode->kind, fromPin) ||
            !kb::render::IsRenderMaterialGraphInputPin(toNode->kind, toPin) ||
            !kb::render::AreRenderMaterialGraphPinsCompatible(fromNode->kind, fromPin, toNode->kind, toPin)) {
            return false;
        }

        kb::render::RenderMaterialGraphLink link{
            .fromNodeId = fromNodeId,
            .fromPinId = kb::render::RenderMaterialGraphStablePinId(fromNode->kind, fromPin, true),
            .fromPin = std::string{ fromPin },
            .toNodeId = toNodeId,
            .toPinId = kb::render::RenderMaterialGraphStablePinId(toNode->kind, toPin, false),
            .toPin = std::string{ toPin },
        };
        if (link.fromPinId == 0U || link.toPinId == 0U) {
            return false;
        }
        link.id = kb::render::MakeRenderMaterialGraphLinkId(link);

        const auto oldInputEnd = std::remove_if(document.graph.links.begin(), document.graph.links.end(), [toNodeId, toPin](const kb::render::RenderMaterialGraphLink& existing) {
            return existing.toNodeId == toNodeId && existing.toPin == toPin;
        });
        document.graph.links.erase(oldInputEnd, document.graph.links.end());
        if (kb::render::FindRenderMaterialGraphLink(document.graph, link.id) == nullptr) {
            document.graph.links.push_back(std::move(link));
        }
        SetWorkingCopy(std::move(document));
        SelectNode(toNodeId);
        return true;
    }

    [[nodiscard]] bool DisconnectGraphInputPin(std::uint32_t toNodeId, std::string_view toPin) {
        if (!workingCopy_.has_value() || toNodeId == 0U) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        const auto oldEnd = std::remove_if(document.graph.links.begin(), document.graph.links.end(), [toNodeId, toPin](const kb::render::RenderMaterialGraphLink& link) {
            return link.toNodeId == toNodeId && link.toPin == toPin;
        });
        if (oldEnd == document.graph.links.end()) {
            return false;
        }
        document.graph.links.erase(oldEnd, document.graph.links.end());
        SetWorkingCopy(std::move(document));
        SelectNode(toNodeId);
        return true;
    }

    [[nodiscard]] bool DisconnectGraphOutputPin(std::uint32_t fromNodeId, std::string_view fromPin) {
        if (!workingCopy_.has_value() || fromNodeId == 0U) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        const auto found = std::find_if(document.graph.links.begin(), document.graph.links.end(), [fromNodeId, fromPin](const kb::render::RenderMaterialGraphLink& link) {
            return link.fromNodeId == fromNodeId && link.fromPin == fromPin;
        });
        if (found == document.graph.links.end()) {
            return false;
        }
        document.graph.links.erase(found);
        SetWorkingCopy(std::move(document));
        SelectNode(fromNodeId);
        return true;
    }

    [[nodiscard]] bool DisconnectGraphLink(std::uint32_t fromNodeId, std::string_view fromPin, std::uint32_t toNodeId, std::string_view toPin) {
        if (!workingCopy_.has_value() || fromNodeId == 0U || toNodeId == 0U) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        const auto found = std::find_if(document.graph.links.begin(), document.graph.links.end(), [fromNodeId, fromPin, toNodeId, toPin](const kb::render::RenderMaterialGraphLink& link) {
            return link.fromNodeId == fromNodeId && link.fromPin == fromPin && link.toNodeId == toNodeId && link.toPin == toPin;
        });
        if (found == document.graph.links.end()) {
            return false;
        }
        document.graph.links.erase(found);
        SetWorkingCopy(std::move(document));
        SelectNode(toNodeId);
        return true;
    }

    [[nodiscard]] bool DisconnectGraphNodeLinks(std::uint32_t nodeId) {
        if (!workingCopy_.has_value() || nodeId == 0U) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        const auto oldEnd = std::remove_if(document.graph.links.begin(), document.graph.links.end(), [nodeId](const kb::render::RenderMaterialGraphLink& link) {
            return link.fromNodeId == nodeId || link.toNodeId == nodeId;
        });
        if (oldEnd == document.graph.links.end()) {
            return false;
        }
        document.graph.links.erase(oldEnd, document.graph.links.end());
        SetWorkingCopy(std::move(document));
        SelectNode(nodeId);
        return true;
    }

    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> GraphNodePosition(std::uint32_t nodeId) const {
        if (!workingCopy_.has_value()) {
            return std::nullopt;
        }
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, nodeId);
        if (node == nullptr) {
            return std::nullopt;
        }
        return std::pair<std::int32_t, std::int32_t>{ node->positionX, node->positionY };
    }

    void Open(
        kb::assets::AssetId assetId,
        std::optional<kb::render::RenderMaterialAssetData> document,
        std::optional<kb::render::RenderMaterialTypeSchema> schema = std::nullopt) {
        openAssetId_ = assetId;
        workingCopy_ = document;
        cleanSnapshot_ = std::move(document);
        activeSchema_ = schema.has_value() && !schema->typeName.empty()
            ? std::move(*schema)
            : kb::render::GetBuiltInPbrMaterialTypeSchema();
        dirty_ = false;
        selectedNodeId_ = 0U;
        selectedParameter_ = InspectorPropertyId::None;
        infoPanelVisible_ = false;
        RefreshParameters();
        RefreshGraphDiagnostics();
    }

    void Close() noexcept {
        openAssetId_ = {};
        workingCopy_.reset();
        cleanSnapshot_.reset();
        parameters_.clear();
        activeSchema_ = kb::render::GetBuiltInPbrMaterialTypeSchema();
        dirty_ = false;
        selectedNodeId_ = 0U;
        selectedParameter_ = InspectorPropertyId::None;
        infoPanelVisible_ = false;
        diagnostics_.clear();
        diagnosticsHaveError_ = false;
    }

    void SetWorkingCopy(kb::render::RenderMaterialAssetData document) {
        workingCopy_ = std::move(document);
        dirty_ = !EquivalentDocument(workingCopy_, cleanSnapshot_);
        RefreshParameters();
        RefreshGraphDiagnostics();
    }

    void MarkSaved() {
        cleanSnapshot_ = workingCopy_;
        dirty_ = false;
    }

    void RevertToCleanSnapshot() {
        workingCopy_ = cleanSnapshot_;
        dirty_ = false;
        RefreshParameters();
        RefreshGraphDiagnostics();
    }

    bool SelectNode(std::uint32_t nodeId) noexcept {
        if (selectedNodeId_ == nodeId) {
            return false;
        }
        selectedNodeId_ = nodeId;
        selectedParameter_ = InspectorPropertyId::None;
        return true;
    }

    bool ClearNodeSelection() noexcept {
        return SelectNode(0U);
    }

    bool SelectParameter(InspectorPropertyId property) noexcept {
        if (selectedParameter_ == property) {
            return false;
        }
        selectedParameter_ = property;
        return true;
    }

    bool ToggleInfoPanel() noexcept {
        infoPanelVisible_ = !infoPanelVisible_;
        return true;
    }

    void SetDiagnostics(std::vector<std::string> diagnostics, bool hasError) {
        diagnostics_ = std::move(diagnostics);
        diagnosticsHaveError_ = hasError;
    }

    void ClearDiagnostics() {
        RefreshGraphDiagnostics();
    }

private:
    [[nodiscard]] static std::string GraphDiagnosticLine(const kb::render::RenderMaterialGraphDiagnostic& diagnostic) {
        std::ostringstream line;
        line << kb::render::RenderMaterialGraphDiagnosticSeverityName(diagnostic.severity)
             << " graph." << kb::render::RenderMaterialGraphDiagnosticKindName(diagnostic.kind);
        if (diagnostic.nodeId != 0U) {
            line << " node " << diagnostic.nodeId;
        }
        if (diagnostic.linkId != 0U) {
            line << " link " << diagnostic.linkId;
        }
        if (!diagnostic.pin.empty()) {
            line << " pin " << diagnostic.pin;
        }
        line << ": " << diagnostic.message;
        return line.str();
    }

    [[nodiscard]] static MaterialEditorParameterValue ScalarValue(float value) {
        return MaterialEditorParameterValue{
            .kind = MaterialEditorParameterValueKind::Scalar,
            .numbers = { value, 0.0F, 0.0F, 0.0F },
        };
    }

    [[nodiscard]] static MaterialEditorParameterValue Vec3Value(const float value[3], MaterialEditorParameterValueKind kind = MaterialEditorParameterValueKind::Vec3) {
        return MaterialEditorParameterValue{
            .kind = kind,
            .numbers = { value[0], value[1], value[2], 0.0F },
        };
    }

    [[nodiscard]] static MaterialEditorParameterValue Vec4Value(const float value[4], MaterialEditorParameterValueKind kind = MaterialEditorParameterValueKind::Vec4) {
        return MaterialEditorParameterValue{
            .kind = kind,
            .numbers = { value[0], value[1], value[2], value[3] },
        };
    }

    [[nodiscard]] static MaterialEditorParameterValue BoolValue(bool value) {
        return MaterialEditorParameterValue{
            .kind = MaterialEditorParameterValueKind::Bool,
            .boolValue = value,
        };
    }

    [[nodiscard]] static MaterialEditorParameterValue EnumValue(std::string value) {
        return MaterialEditorParameterValue{
            .kind = MaterialEditorParameterValueKind::Enum,
            .text = std::move(value),
        };
    }

    [[nodiscard]] static MaterialEditorParameterValue TextureAssetValue(std::uint64_t assetId) {
        return MaterialEditorParameterValue{
            .kind = MaterialEditorParameterValueKind::TextureAsset,
            .assetId = assetId,
        };
    }

    [[nodiscard]] static std::vector<float> ParseDefaultNumbers(std::string_view text) {
        std::vector<float> values;
        if (text.empty() || text == "_") {
            return values;
        }
        std::istringstream input{ std::string{ text } };
        float value = 0.0F;
        while (input >> value) {
            values.push_back(value);
        }
        return values;
    }

    [[nodiscard]] static MaterialEditorParameterValue DefaultValueForSchema(
        const kb::render::RenderMaterialParameterSchema& parameter,
        const kb::render::RenderMaterialAssetData& fallbackDefaults) {
        const std::vector<float> numbers = ParseDefaultNumbers(parameter.defaultValueHint);
        switch (parameter.type) {
        case kb::render::RenderMaterialParameterType::Scalar:
            if (!numbers.empty()) {
                return ScalarValue(numbers[0]);
            }
            break;
        case kb::render::RenderMaterialParameterType::Vec3:
            if (numbers.size() >= 3U) {
                const float values[3]{ numbers[0], numbers[1], numbers[2] };
                return Vec3Value(values);
            }
            break;
        case kb::render::RenderMaterialParameterType::Vec4:
            if (numbers.size() >= 4U) {
                const float values[4]{ numbers[0], numbers[1], numbers[2], numbers[3] };
                return Vec4Value(values);
            }
            break;
        case kb::render::RenderMaterialParameterType::Color:
            if (numbers.size() >= 4U) {
                const float values[4]{ numbers[0], numbers[1], numbers[2], numbers[3] };
                return Vec4Value(values, MaterialEditorParameterValueKind::Color);
            }
            if (numbers.size() >= 3U) {
                const float values[3]{ numbers[0], numbers[1], numbers[2] };
                return Vec3Value(values, MaterialEditorParameterValueKind::Color);
            }
            break;
        case kb::render::RenderMaterialParameterType::Enum:
            if (!parameter.defaultValueHint.empty() && parameter.defaultValueHint != "_") {
                return EnumValue(parameter.defaultValueHint);
            }
            break;
        case kb::render::RenderMaterialParameterType::Bool:
            if (parameter.defaultValueHint == "true" || parameter.defaultValueHint == "1") {
                return BoolValue(true);
            }
            if (parameter.defaultValueHint == "false" || parameter.defaultValueHint == "0") {
                return BoolValue(false);
            }
            break;
        case kb::render::RenderMaterialParameterType::Texture:
            return TextureAssetValue(0U);
        }
        return ParameterValueForField(parameter.name, fallbackDefaults, parameter.type);
    }

    [[nodiscard]] static MaterialEditorParameterValue GraphParameterValueForSchema(
        const kb::render::RenderMaterialParameterSchema& parameter,
        const kb::render::RenderMaterialAssetData& document) {
        for (const kb::render::RenderMaterialGraphParameterValue& value : document.graphParameterValues) {
            if (value.stableId != parameter.name || value.type != parameter.type) {
                continue;
            }
            switch (value.type) {
            case kb::render::RenderMaterialParameterType::Scalar:
                return ScalarValue(value.numbers[0]);
            case kb::render::RenderMaterialParameterType::Vec3: {
                const float values[3]{ value.numbers[0], value.numbers[1], value.numbers[2] };
                return Vec3Value(values);
            }
            case kb::render::RenderMaterialParameterType::Vec4: {
                const float values[4]{ value.numbers[0], value.numbers[1], value.numbers[2], value.numbers[3] };
                return Vec4Value(values);
            }
            case kb::render::RenderMaterialParameterType::Color: {
                const float values[4]{ value.numbers[0], value.numbers[1], value.numbers[2], value.numbers[3] };
                return Vec4Value(values, MaterialEditorParameterValueKind::Color);
            }
            case kb::render::RenderMaterialParameterType::Bool:
                return BoolValue(value.boolValue);
            case kb::render::RenderMaterialParameterType::Enum:
                return EnumValue(value.text);
            case kb::render::RenderMaterialParameterType::Texture:
                return TextureAssetValue(value.assetId);
            }
        }
        return MaterialEditorParameterValue{};
    }

    [[nodiscard]] static std::string AlphaModeName(kb::render::RenderMaterialAlphaMode mode) {
        switch (mode) {
        case kb::render::RenderMaterialAlphaMode::Opaque:
            return "OPAQUE";
        case kb::render::RenderMaterialAlphaMode::Mask:
            return "MASK";
        case kb::render::RenderMaterialAlphaMode::Blend:
            return "BLEND";
        }
        return "OPAQUE";
    }

    [[nodiscard]] static std::string DecalBlendModeName(kb::render::RenderMaterialDecalBlendMode mode) {
        switch (mode) {
        case kb::render::RenderMaterialDecalBlendMode::Disabled:
            return "DISABLED";
        case kb::render::RenderMaterialDecalBlendMode::BaseColor:
            return "BASE_COLOR";
        case kb::render::RenderMaterialDecalBlendMode::Normal:
            return "NORMAL";
        case kb::render::RenderMaterialDecalBlendMode::Pbr:
            return "PBR";
        }
        return "DISABLED";
    }

    [[nodiscard]] static std::string LayerBlendModeName(kb::render::RenderMaterialLayerBlendMode mode) {
        switch (mode) {
        case kb::render::RenderMaterialLayerBlendMode::Replace:
            return "REPLACE";
        case kb::render::RenderMaterialLayerBlendMode::Add:
            return "ADD";
        case kb::render::RenderMaterialLayerBlendMode::Multiply:
            return "MULTIPLY";
        }
        return "REPLACE";
    }

    [[nodiscard]] static MaterialEditorParameterGroup EditorGroupFor(kb::render::RenderMaterialParameterGroup group) noexcept {
        switch (group) {
        case kb::render::RenderMaterialParameterGroup::Core:
            return MaterialEditorParameterGroup::Core;
        case kb::render::RenderMaterialParameterGroup::Surface:
            return MaterialEditorParameterGroup::Surface;
        case kb::render::RenderMaterialParameterGroup::Advanced:
            return MaterialEditorParameterGroup::Advanced;
        }
        return MaterialEditorParameterGroup::Advanced;
    }

    [[nodiscard]] static MaterialEditorParameterValue ParameterValueForField(
        std::string_view field,
        const kb::render::RenderMaterialAssetData& document,
        kb::render::RenderMaterialParameterType type) {
        const kb::render::RenderMaterialDesc& desc = document.desc;
        if (field == "baseColor" || field == "baseColorFactor") {
            return Vec4Value(desc.baseColor, MaterialEditorParameterValueKind::Color);
        }
        if (field == "emissiveColor" || field == "emissiveFactor") {
            return Vec3Value(desc.emissiveColor, MaterialEditorParameterValueKind::Color);
        }
        if (field == "metallicFactor") return ScalarValue(desc.metallicFactor);
        if (field == "roughnessFactor") return ScalarValue(desc.roughnessFactor);
        if (field == "normalScale") return ScalarValue(desc.normalScale);
        if (field == "occlusionStrength") return ScalarValue(desc.occlusionStrength);
        if (field == "emissiveStrength") return ScalarValue(desc.emissiveStrength);
        if (field == "alphaCutoff") return ScalarValue(desc.alphaCutoff);
        if (field == "alphaMode") return EnumValue(AlphaModeName(desc.alphaMode));
        if (field == "doubleSided") return BoolValue(desc.doubleSided);
        if (field == "clearcoatFactor") return ScalarValue(desc.clearcoatFactor);
        if (field == "clearcoatRoughnessFactor") return ScalarValue(desc.clearcoatRoughnessFactor);
        if (field == "sheenColor") return Vec3Value(desc.sheenColor, MaterialEditorParameterValueKind::Color);
        if (field == "sheenRoughnessFactor") return ScalarValue(desc.sheenRoughnessFactor);
        if (field == "transmissionFactor") return ScalarValue(desc.transmissionFactor);
        if (field == "thicknessFactor") return ScalarValue(desc.thicknessFactor);
        if (field == "attenuationColor") return Vec3Value(desc.attenuationColor, MaterialEditorParameterValueKind::Color);
        if (field == "attenuationDistance") return ScalarValue(desc.attenuationDistance);
        if (field == "subsurfaceColor") return Vec3Value(desc.subsurfaceColor, MaterialEditorParameterValueKind::Color);
        if (field == "subsurfaceFactor") return ScalarValue(desc.subsurfaceFactor);
        if (field == "anisotropyStrength") return ScalarValue(desc.anisotropyStrength);
        if (field == "anisotropyRotation") return ScalarValue(desc.anisotropyRotation);
        if (field == "layerWeight") return ScalarValue(desc.layerWeight);
        if (field == "decalBlendMode") return EnumValue(DecalBlendModeName(desc.decalBlendMode));
        if (field == "layerBlendMode") return EnumValue(LayerBlendModeName(desc.layerBlendMode));
        return MaterialEditorParameterValue{ .kind = type == kb::render::RenderMaterialParameterType::Bool ? MaterialEditorParameterValueKind::Bool : MaterialEditorParameterValueKind::None };
    }

    [[nodiscard]] static MaterialEditorParameterValue ParameterValueForSchema(
        const kb::render::RenderMaterialParameterSchema& parameter,
        const kb::render::RenderMaterialAssetData& document,
        const kb::render::RenderMaterialAssetData& fallbackDefaults) {
        MaterialEditorParameterValue value = GraphParameterValueForSchema(parameter, document);
        if (value.kind != MaterialEditorParameterValueKind::None) {
            return value;
        }
        value = ParameterValueForField(parameter.name, document, parameter.type);
        if (value.kind != MaterialEditorParameterValueKind::None) {
            return value;
        }
        return DefaultValueForSchema(parameter, fallbackDefaults);
    }

    [[nodiscard]] static MaterialEditorParameterValue TextureValueForField(
        std::string_view field,
        const kb::render::RenderMaterialAssetData& document) {
        for (const kb::render::RenderMaterialGraphParameterValue& value : document.graphParameterValues) {
            if (value.type == kb::render::RenderMaterialParameterType::Texture &&
                field == (value.stableId + "TextureAssetId")) {
                return TextureAssetValue(value.assetId);
            }
        }
        const kb::render::RenderMaterialDesc& desc = document.desc;
        if (field == "albedoTextureAssetId") return TextureAssetValue(desc.albedoTextureAssetId);
        if (field == "normalTextureAssetId") return TextureAssetValue(desc.normalTextureAssetId);
        if (field == "metallicRoughnessTextureAssetId") return TextureAssetValue(desc.metallicRoughnessTextureAssetId);
        if (field == "occlusionTextureAssetId") return TextureAssetValue(desc.occlusionTextureAssetId);
        if (field == "emissiveTextureAssetId") return TextureAssetValue(desc.emissiveTextureAssetId);
        if (field == "clearcoatTextureAssetId") return TextureAssetValue(desc.clearcoatTextureAssetId);
        if (field == "clearcoatRoughnessTextureAssetId") return TextureAssetValue(desc.clearcoatRoughnessTextureAssetId);
        if (field == "sheenColorTextureAssetId") return TextureAssetValue(desc.sheenColorTextureAssetId);
        if (field == "transmissionTextureAssetId") return TextureAssetValue(desc.transmissionTextureAssetId);
        if (field == "thicknessTextureAssetId") return TextureAssetValue(desc.thicknessTextureAssetId);
        if (field == "anisotropyTextureAssetId") return TextureAssetValue(desc.anisotropyTextureAssetId);
        if (field == "decalTextureAssetId") return TextureAssetValue(desc.decalTextureAssetId);
        if (field == "layerMaskTextureAssetId") return TextureAssetValue(desc.layerMaskTextureAssetId);
        return TextureAssetValue(0U);
    }

    [[nodiscard]] static std::string DisplayNameOrStableId(std::string_view displayName, std::string_view stableId) {
        return displayName.empty() ? std::string{ stableId } : std::string{ displayName };
    }

    [[nodiscard]] static std::vector<MaterialEditorParameter> BuildParameters(
        const kb::render::RenderMaterialAssetData& document,
        const kb::render::RenderMaterialTypeSchema& schema) {
        const kb::render::RenderMaterialAssetData defaults{};
        std::vector<MaterialEditorParameter> parameters;
        parameters.reserve(schema.parameters.size() + schema.textureSlots.size());

        for (const kb::render::RenderMaterialParameterSchema& parameter : schema.parameters) {
            parameters.push_back(MaterialEditorParameter{
                .stableId = std::string{ parameter.name },
                .type = parameter.type,
                .group = EditorGroupFor(parameter.group),
                .displayName = DisplayNameOrStableId(parameter.displayName, parameter.name),
                .description = std::string{ parameter.description },
                .value = ParameterValueForSchema(parameter, document, defaults),
                .defaultValue = DefaultValueForSchema(parameter, defaults),
                .range = parameter.range,
                .expectedTextureColorSpace = std::nullopt,
                .overrideEnabled = parameter.overrideSupported,
                .enabled = parameter.runtimeSupport == kb::render::RenderMaterialFeatureSupport::Supported,
                .sortOrder = parameter.editorOrder,
            });
        }

        for (const kb::render::RenderMaterialTextureSlotSchema& slot : schema.textureSlots) {
            parameters.push_back(MaterialEditorParameter{
                .stableId = std::string{ slot.assetIdFieldName },
                .type = kb::render::RenderMaterialParameterType::Texture,
                .group = slot.runtimeSupport == kb::render::RenderMaterialFeatureSupport::Supported
                    ? MaterialEditorParameterGroup::Texture
                    : MaterialEditorParameterGroup::Advanced,
                .displayName = DisplayNameOrStableId(slot.name, slot.assetIdFieldName),
                .description = std::string{ slot.description },
                .value = TextureValueForField(slot.assetIdFieldName, document),
                .defaultValue = TextureValueForField(slot.assetIdFieldName, defaults),
                .range = std::nullopt,
                .expectedTextureColorSpace = slot.expectedColorSpace,
                .overrideEnabled = slot.overrideSupported,
                .enabled = slot.runtimeSupport == kb::render::RenderMaterialFeatureSupport::Supported,
                .sortOrder = slot.editorOrder,
            });
        }

        std::ranges::sort(parameters, [](const MaterialEditorParameter& lhs, const MaterialEditorParameter& rhs) {
            if (lhs.group != rhs.group) {
                return static_cast<std::uint8_t>(lhs.group) < static_cast<std::uint8_t>(rhs.group);
            }
            if (lhs.sortOrder != rhs.sortOrder) {
                return lhs.sortOrder < rhs.sortOrder;
            }
            return lhs.stableId < rhs.stableId;
        });
        return parameters;
    }

    static void EnsureEditableGraph(kb::render::RenderMaterialGraphDocument& graph) {
        if (graph.nodes.empty()) {
            graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
        }
    }

    [[nodiscard]] static std::uint32_t NextGraphNodeId(const kb::render::RenderMaterialGraphDocument& graph) noexcept {
        std::uint32_t next = 1U;
        for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) {
            next = std::max(next, node.id + 1U);
        }
        return next;
    }

    [[nodiscard]] static kb::render::RenderMaterialGraphNode* FindMutableGraphNode(
        kb::render::RenderMaterialGraphDocument& graph,
        std::uint32_t nodeId) noexcept {
        for (kb::render::RenderMaterialGraphNode& node : graph.nodes) {
            if (node.id == nodeId) {
                return &node;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static kb::render::RenderMaterialGraphParameterMetadata DefaultParameterMetadata(
        kb::render::RenderMaterialGraphNodeKind kind,
        std::uint32_t nodeId) {
        switch (kind) {
        case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "scalar" + std::to_string(nodeId),
                .displayName = "Scalar " + std::to_string(nodeId),
                .defaultValueHint = "0",
                .hasRange = true,
                .rangeMin = 0.0F,
                .rangeMax = 1.0F,
                .overrideSupported = true,
            };
        case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "vector" + std::to_string(nodeId),
                .displayName = "Vector " + std::to_string(nodeId),
                .defaultValueHint = "0 0 0",
                .overrideSupported = true,
            };
        case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "color" + std::to_string(nodeId),
                .displayName = "Color " + std::to_string(nodeId),
                .defaultValueHint = "1 1 1 1",
                .hasRange = true,
                .rangeMin = 0.0F,
                .rangeMax = 1.0F,
                .overrideSupported = true,
            };
        case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "texture" + std::to_string(nodeId),
                .displayName = "Texture " + std::to_string(nodeId),
                .textureRole = "baseColor",
                .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
                .overrideSupported = true,
            };
        case kb::render::RenderMaterialGraphNodeKind::TextureSample:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "textureSample" + std::to_string(nodeId),
                .displayName = "Texture Sample " + std::to_string(nodeId),
                .textureRole = "baseColor",
                .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
                .overrideSupported = true,
            };
        case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Scalar",
                .defaultValueHint = "0",
                .hasRange = true,
                .rangeMin = 0.0F,
                .rangeMax = 1.0F,
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Vector",
                .defaultValueHint = "0 0 0",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Color",
                .defaultValueHint = "1 1 1 1",
                .hasRange = true,
                .rangeMin = 0.0F,
                .rangeMax = 1.0F,
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        case kb::render::RenderMaterialGraphNodeKind::Add:
        case kb::render::RenderMaterialGraphNodeKind::Multiply:
        case kb::render::RenderMaterialGraphNodeKind::Clamp:
        case kb::render::RenderMaterialGraphNodeKind::Lerp:
        case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        case kb::render::RenderMaterialGraphNodeKind::Uv:
            break;
        }
        return {};
    }

    void RefreshGraphDiagnostics() {
        diagnostics_.clear();
        diagnosticsHaveError_ = false;
        if (!workingCopy_.has_value()) {
            return;
        }
        const std::vector<kb::render::RenderMaterialGraphDiagnostic> graphDiagnostics = kb::render::ValidateRenderMaterialAssetGraphDiagnostics(*workingCopy_);
        diagnostics_.reserve(graphDiagnostics.size());
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : graphDiagnostics) {
            diagnostics_.push_back(GraphDiagnosticLine(diagnostic));
            if (diagnostic.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error) {
                diagnosticsHaveError_ = true;
            }
        }
    }

    void RefreshParameters() {
        parameters_.clear();
        if (!workingCopy_.has_value()) {
            return;
        }
        parameters_ = BuildParameters(*workingCopy_, activeSchema_);
    }

    [[nodiscard]] static std::string CanonicalDocument(const kb::render::RenderMaterialAssetData& document) {
        std::ostringstream output;
        kb::render::RenderMaterialAssetWriter::Write(output, document);
        return output.str();
    }

    [[nodiscard]] static bool EquivalentDocument(
        const std::optional<kb::render::RenderMaterialAssetData>& lhs,
        const std::optional<kb::render::RenderMaterialAssetData>& rhs) {
        if (lhs.has_value() != rhs.has_value()) {
            return false;
        }
        if (!lhs.has_value()) {
            return true;
        }
        return CanonicalDocument(*lhs) == CanonicalDocument(*rhs);
    }

    kb::assets::AssetId openAssetId_{};
    std::optional<kb::render::RenderMaterialAssetData> workingCopy_;
    std::optional<kb::render::RenderMaterialAssetData> cleanSnapshot_;
    kb::render::RenderMaterialTypeSchema activeSchema_ = kb::render::GetBuiltInPbrMaterialTypeSchema();
    std::vector<MaterialEditorParameter> parameters_;
    std::vector<std::string> diagnostics_;
    bool diagnosticsHaveError_ = false;
    bool dirty_ = false;
    bool infoPanelVisible_ = false;
    std::uint32_t selectedNodeId_ = 0U;
    InspectorPropertyId selectedParameter_ = InspectorPropertyId::None;
};

} // namespace kb::editor
