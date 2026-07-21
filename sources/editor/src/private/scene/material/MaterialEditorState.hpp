#pragma once

#include "scene/material/MaterialEditorModels.hpp"
#include "scene/material/MaterialEditorCanonicalCompare.hpp"

#include "inspection/InspectorPanelState.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialNumericParsing.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"
#include "engine/assets/AssetId.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
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

    [[nodiscard]] std::vector<std::string> MaterialDiffRows() const {
        std::vector<std::string> rows;
        const auto formatFloat = [](float value) {
            std::ostringstream output;
            output << std::fixed << std::setprecision(3) << value;
            return output.str();
        };
        const auto appendText = [&rows](std::string_view label, std::string_view before, std::string_view after) {
            if (before != after) {
                rows.push_back(std::string{ label } + ": " + std::string{ before } + " -> " + std::string{ after });
            }
        };
        const auto appendUInt = [&rows](std::string_view label, std::uint64_t before, std::uint64_t after) {
            if (before != after) {
                rows.push_back(std::string{ label } + ": " + std::to_string(before) + " -> " + std::to_string(after));
            }
        };
        const auto appendBool = [&rows](std::string_view label, bool before, bool after) {
            if (before != after) {
                rows.push_back(std::string{ label } + ": " + (before ? "true" : "false") + " -> " + (after ? "true" : "false"));
            }
        };
        const auto appendFloat = [&rows, &formatFloat](std::string_view label, float before, float after) {
            if (before != after) {
                rows.push_back(std::string{ label } + ": " + formatFloat(before) + " -> " + formatFloat(after));
            }
        };
        const auto appendFloatArray = [&appendFloat](std::string_view label, const float* before, const float* after, std::size_t count) {
            for (std::size_t index = 0U; index < count; ++index) {
                appendFloat(std::string{ label } + "[" + std::to_string(index) + "]", before[index], after[index]);
            }
        };
        const auto nodeSignature = [](const kb::render::RenderMaterialGraphNode& node) {
            std::ostringstream output;
            output << static_cast<int>(node.kind) << "|" << node.positionX << "|" << node.positionY << "|"
                   << node.parameter.stableId << "|" << node.parameter.displayName << "|" << node.parameter.defaultValueHint << "|"
                   << node.parameter.textureRole << "|" << static_cast<int>(node.parameter.expectedTextureColorSpace) << "|"
                   << node.customCode.body << "|" << static_cast<int>(node.customCode.outputType) << "|"
                   << node.layerStack.size();
            return output.str();
        };
        const auto linkSignature = [](const kb::render::RenderMaterialGraphLink& link) {
            std::ostringstream output;
            output << link.fromNodeId << ":" << link.fromPin << "->" << link.toNodeId << ":" << link.toPin;
            return output.str();
        };
        const auto appendGraphDiff = [&](const kb::render::RenderMaterialGraphDocument& before, const kb::render::RenderMaterialGraphDocument& after) {
            appendText("Graph domain", before.materialDomain, after.materialDomain);
            appendText("Graph shading", before.shadingModel, after.shadingModel);
            appendText("Graph blend", before.blendMode, after.blendMode);
            appendUInt("Graph nodes", before.nodes.size(), after.nodes.size());
            appendUInt("Graph links", before.links.size(), after.links.size());
            appendUInt("Graph comments", before.comments.size(), after.comments.size());
            for (const kb::render::RenderMaterialGraphNode& node : after.nodes) {
                const auto match = std::ranges::find_if(before.nodes, [&node](const kb::render::RenderMaterialGraphNode& candidate) {
                    return candidate.id == node.id;
                });
                if (match == before.nodes.end()) {
                    rows.push_back("Added node #" + std::to_string(node.id) + " " + std::string{ kb::render::RenderMaterialGraphNodeKindName(node.kind) });
                } else if (nodeSignature(*match) != nodeSignature(node)) {
                    rows.push_back("Changed node #" + std::to_string(node.id) + " " + std::string{ kb::render::RenderMaterialGraphNodeKindName(node.kind) });
                }
            }
            for (const kb::render::RenderMaterialGraphNode& node : before.nodes) {
                const auto match = std::ranges::find_if(after.nodes, [&node](const kb::render::RenderMaterialGraphNode& candidate) {
                    return candidate.id == node.id;
                });
                if (match == after.nodes.end()) {
                    rows.push_back("Removed node #" + std::to_string(node.id) + " " + std::string{ kb::render::RenderMaterialGraphNodeKindName(node.kind) });
                }
            }
            for (const kb::render::RenderMaterialGraphLink& link : after.links) {
                const auto match = std::ranges::find_if(before.links, [&link](const kb::render::RenderMaterialGraphLink& candidate) {
                    return candidate.id == link.id;
                });
                if (match == before.links.end()) {
                    rows.push_back("Added link " + linkSignature(link));
                } else if (linkSignature(*match) != linkSignature(link)) {
                    rows.push_back("Changed link " + linkSignature(link));
                }
            }
            for (const kb::render::RenderMaterialGraphLink& link : before.links) {
                const auto match = std::ranges::find_if(after.links, [&link](const kb::render::RenderMaterialGraphLink& candidate) {
                    return candidate.id == link.id;
                });
                if (match == after.links.end()) {
                    rows.push_back("Removed link " + linkSignature(link));
                }
            }
        };
        const auto appendMaterialDiff = [&](const kb::render::RenderMaterialAssetData& before, const kb::render::RenderMaterialAssetData& after) {
            appendText("Material type", before.materialType, after.materialType);
            appendUInt("Material type version", before.materialTypeVersion, after.materialTypeVersion);
            appendUInt("Material type asset", before.materialTypeAssetId, after.materialTypeAssetId);
            appendText("Material type path", before.materialTypeAssetPath, after.materialTypeAssetPath);
            appendUInt("Graph source asset", before.graphSourceAssetId, after.graphSourceAssetId);
            appendText("Graph source path", before.graphSourceAssetPath, after.graphSourceAssetPath);
            appendFloatArray("Base color", before.desc.baseColor, after.desc.baseColor, 4U);
            appendFloatArray("Emissive color", before.desc.emissiveColor, after.desc.emissiveColor, 3U);
            appendFloat("Metallic", before.desc.metallicFactor, after.desc.metallicFactor);
            appendFloat("Roughness", before.desc.roughnessFactor, after.desc.roughnessFactor);
            appendFloat("Normal scale", before.desc.normalScale, after.desc.normalScale);
            appendFloat("Occlusion", before.desc.occlusionStrength, after.desc.occlusionStrength);
            appendFloat("Emissive strength", before.desc.emissiveStrength, after.desc.emissiveStrength);
            appendFloat("Alpha cutoff", before.desc.alphaCutoff, after.desc.alphaCutoff);
            appendBool("Double sided", before.desc.doubleSided, after.desc.doubleSided);
            appendBool("Writes depth", before.desc.writesDepth, after.desc.writesDepth);
            appendUInt("Albedo texture", before.desc.albedoTextureAssetId, after.desc.albedoTextureAssetId);
            appendUInt("Normal texture", before.desc.normalTextureAssetId, after.desc.normalTextureAssetId);
            appendUInt("Metallic-roughness texture", before.desc.metallicRoughnessTextureAssetId, after.desc.metallicRoughnessTextureAssetId);
            appendUInt("Occlusion texture", before.desc.occlusionTextureAssetId, after.desc.occlusionTextureAssetId);
            appendUInt("Emissive texture", before.desc.emissiveTextureAssetId, after.desc.emissiveTextureAssetId);
            appendText("Albedo texture path", before.albedoTexturePath, after.albedoTexturePath);
            appendText("Normal texture path", before.normalTexturePath, after.normalTexturePath);
            appendUInt("Graph parameter values", before.graphParameterValues.size(), after.graphParameterValues.size());
            appendGraphDiff(before.graph, after.graph);
            if (rows.empty() && CanonicalDocument(before) != CanonicalDocument(after)) {
                rows.push_back("Serialized material data changed");
            }
        };
        const auto appendInstanceDiff = [&](const kb::render::RenderMaterialInstanceAssetData& before, const kb::render::RenderMaterialInstanceAssetData& after) {
            appendUInt("Instance parent material", before.parentMaterialAssetId.value, after.parentMaterialAssetId.value);
            appendUInt("Instance static overrides", before.staticParameterOverrides.size(), after.staticParameterOverrides.size());
            appendBool("Instance has overrides", before.hasOverrides, after.hasOverrides);
            appendBool("Instance base overrides", before.basePropertyOverrides.HasAny(), after.basePropertyOverrides.HasAny());
            appendUInt("Instance graph parameter values", before.overrides.graphParameterValues.size(), after.overrides.graphParameterValues.size());
            if (CanonicalInstance(before) != CanonicalInstance(after)) {
                rows.push_back("Serialized material instance data changed");
            }
        };

        if (cleanSnapshot_.has_value() && workingCopy_.has_value()) {
            appendMaterialDiff(*cleanSnapshot_, *workingCopy_);
        }
        if (instanceCleanSnapshot_.has_value() && instanceWorkingCopy_.has_value()) {
            appendInstanceDiff(*instanceCleanSnapshot_, *instanceWorkingCopy_);
        }
        return rows;
    }

    [[nodiscard]] const std::vector<std::string>& Diagnostics() const {
        EnsureGraphDiagnostics();
        return diagnostics_;
    }

    [[nodiscard]] bool DiagnosticsHaveError() const {
        EnsureGraphDiagnostics();
        return diagnosticsHaveError_;
    }

    [[nodiscard]] const std::vector<MaterialEditorGraphDiagnosticMarker>& GraphDiagnosticMarkers() const {
        EnsureGraphDiagnostics();
        return graphDiagnosticMarkers_;
    }

    [[nodiscard]] kb::render::RenderMaterialGraphRuntimeState GraphRuntimeState() const {
        EnsureGraphDiagnostics();
        return graphRuntimeState_;
    }

    [[nodiscard]] std::string_view GraphRuntimeStateName() const {
        EnsureGraphDiagnostics();
        return kb::render::RenderMaterialGraphRuntimeStateName(graphRuntimeState_);
    }

    [[nodiscard]] const MaterialEditorMaterialStatsModel& MaterialStats() const {
        EnsureGraphDiagnostics();
        return materialStats_;
    }

    [[nodiscard]] const MaterialEditorShaderViewerModel& ShaderViewer() const {
        EnsureGraphDiagnostics();
        return shaderViewer_;
    }

    [[nodiscard]] std::string_view FindQuery() const noexcept {
        return findQuery_;
    }

    [[nodiscard]] const std::vector<MaterialEditorFindResult>& FindResults() const {
        EnsureFindResults();
        return findResults_;
    }

    void SetFindQuery(std::string query) {
        findQuery_ = std::move(query);
        InvalidateFindResults();
    }

    void AppendFindText(wchar_t character) {
        if (character < 0x20) {
            return;
        }
        findQuery_.push_back(static_cast<char>(character));
        InvalidateFindResults();
    }

    void InsertFindText(std::string_view text) {
        for (const char character : text) {
            if (static_cast<unsigned char>(character) >= 0x20U) {
                findQuery_.push_back(character);
            }
        }
        InvalidateFindResults();
    }

    void BackspaceFind() {
        if (findQuery_.empty()) {
            return;
        }
        findQuery_.pop_back();
        InvalidateFindResults();
    }

    void ClearFindQuery() {
        findQuery_.clear();
        findResults_.clear();
        findResultsStale_ = false;
    }

    [[nodiscard]] std::optional<MaterialEditorFindFocusTarget> FindResultFocusTarget(std::size_t index) const {
        EnsureFindResults();
        if (index >= findResults_.size()) {
            return std::nullopt;
        }
        return MaterialEditorFindFocusTarget{
            .graphX = findResults_[index].focusX,
            .graphY = findResults_[index].focusY,
        };
    }

    [[nodiscard]] bool FocusFindResult(std::size_t index) {
        EnsureFindResults();
        if (index >= findResults_.size()) {
            return false;
        }
        const MaterialEditorFindResult& result = findResults_[index];
        if (result.commentId != 0U) {
            return SelectComment(result.commentId);
        }
        if (result.nodeId != 0U) {
            return SetNodeSelection({ result.nodeId }, result.nodeId);
        }
        return false;
    }

    [[nodiscard]] const std::vector<MaterialEditorParameter>& Parameters() const {
        EnsureParameters();
        return parameters_;
    }

    [[nodiscard]] std::vector<MaterialEditorInstanceParentChainRow> InstanceParentChainRows() const {
        std::vector<MaterialEditorInstanceParentChainRow> rows;
        if (!instanceWorkingCopy_.has_value()) {
            return rows;
        }
        rows.push_back(MaterialEditorInstanceParentChainRow{
            .assetId = openAssetId_,
            .label = "Current instance " + std::to_string(openAssetId_.value),
            .current = true,
        });
        if (instanceWorkingCopy_->parentMaterialAssetId.IsValid()) {
            rows.push_back(MaterialEditorInstanceParentChainRow{
                .assetId = instanceWorkingCopy_->parentMaterialAssetId,
                .label = "Parent " + std::to_string(instanceWorkingCopy_->parentMaterialAssetId.value),
                .current = false,
            });
        }
        return rows;
    }

    [[nodiscard]] std::vector<MaterialEditorInstanceOverrideGroupRow> InstanceOverrideGroups() const {
        std::vector<MaterialEditorInstanceOverrideGroupRow> groups;
        if (!instanceWorkingCopy_.has_value()) {
            return groups;
        }
        for (const MaterialEditorParameterGroup group : {
                 MaterialEditorParameterGroup::Core,
                 MaterialEditorParameterGroup::Surface,
                 MaterialEditorParameterGroup::Texture,
                 MaterialEditorParameterGroup::Advanced,
             }) {
            MaterialEditorInstanceOverrideGroupRow row{
                .group = group,
                .expanded = instanceOverrideGroupExpanded_[MaterialEditorParameterGroupIndex(group)],
            };
            for (const MaterialEditorParameter& parameter : Parameters()) {
                if (parameter.group != group) {
                    continue;
                }
                ++row.totalParameterCount;
                if (parameter.overrideActive) {
                    ++row.activeOverrideCount;
                }
                if (row.expanded) {
                    row.parameters.push_back(parameter);
                }
            }
            if (row.totalParameterCount > 0U) {
                groups.push_back(std::move(row));
            }
        }
        return groups;
    }

    [[nodiscard]] bool ToggleInstanceOverrideGroup(MaterialEditorParameterGroup group) noexcept {
        const std::size_t index = MaterialEditorParameterGroupIndex(group);
        instanceOverrideGroupExpanded_[index] = !instanceOverrideGroupExpanded_[index];
        return instanceOverrideGroupExpanded_[index];
    }

    [[nodiscard]] std::vector<MaterialEditorInstanceStaticSwitchRow> InstanceStaticSwitchRows() const {
        std::vector<MaterialEditorInstanceStaticSwitchRow> rows;
        if (!instanceWorkingCopy_.has_value() || !instanceParentSnapshot_.has_value()) {
            return rows;
        }
        for (const kb::render::RenderMaterialGraphNode& node : instanceParentSnapshot_->graph.nodes) {
            if (!IsStaticOverrideNodeKind(node.kind)) {
                continue;
            }
            const std::string stableId = StableIdForGraphNode(node);
            const kb::render::RenderMaterialInstanceStaticParameterOverride* active = nullptr;
            for (const kb::render::RenderMaterialInstanceStaticParameterOverride& overrideValue : instanceWorkingCopy_->staticParameterOverrides) {
                if (overrideValue.stableId == stableId && overrideValue.nodeKind == node.kind) {
                    active = &overrideValue;
                    break;
                }
            }
            const std::string parentValue = node.parameter.defaultValueHint.empty()
                ? DefaultStaticOverrideValue(node.kind)
                : node.parameter.defaultValueHint;
            rows.push_back(MaterialEditorInstanceStaticSwitchRow{
                .nodeId = node.id,
                .stableId = stableId,
                .displayName = DisplayNameOrStableId(node.parameter.displayName, stableId),
                .nodeKind = node.kind,
                .parentValue = parentValue,
                .value = active != nullptr ? active->value : parentValue,
                .overrideActive = active != nullptr,
            });
        }
        std::ranges::sort(rows, [](const MaterialEditorInstanceStaticSwitchRow& lhs, const MaterialEditorInstanceStaticSwitchRow& rhs) {
            return lhs.stableId < rhs.stableId;
        });
        return rows;
    }

    [[nodiscard]] std::vector<MaterialEditorLayerTreeRow> LayerTreeRows() const {
        std::vector<MaterialEditorLayerTreeRow> rows;
        if (!workingCopy_.has_value()) {
            return rows;
        }
        for (const kb::render::RenderMaterialGraphNode& node : workingCopy_->graph.nodes) {
            if (node.kind != kb::render::RenderMaterialGraphNodeKind::LayerStack) {
                continue;
            }
            for (std::size_t index = 0U; index < node.layerStack.size(); ++index) {
                const kb::render::RenderMaterialGraphLayerStackEntry& entry = node.layerStack[index];
                rows.push_back(MaterialEditorLayerTreeRow{
                    .nodeId = node.id,
                    .index = index,
                    .enabled = entry.enabled,
                    .layerFunctionAssetId = entry.layerFunctionAssetId,
                    .blendFunctionAssetId = entry.blendFunctionAssetId,
                    .layerName = entry.layerName,
                    .blendName = entry.blendName,
                    .linkState = entry.linkState,
                    .layerParameterCount = static_cast<std::uint32_t>(entry.layerParameters.size()),
                    .blendParameterCount = static_cast<std::uint32_t>(entry.blendParameters.size()),
                });
            }
        }
        return rows;
    }

    [[nodiscard]] bool AddLayerStackEntry(
        std::uint32_t nodeId,
        kb::render::RenderMaterialGraphLayerStackEntry entry,
        std::size_t index = static_cast<std::size_t>(-1)) {
        if (!workingCopy_.has_value() || instanceWorkingCopy_.has_value()) {
            return false;
        }
        kb::render::RenderMaterialAssetData document = *workingCopy_;
        kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(document.graph, nodeId);
        if (node == nullptr || node->kind != kb::render::RenderMaterialGraphNodeKind::LayerStack) {
            return false;
        }
        if (index >= node->layerStack.size()) {
            node->layerStack.push_back(std::move(entry));
        } else {
            node->layerStack.insert(node->layerStack.begin() + static_cast<std::ptrdiff_t>(index), std::move(entry));
        }
        SetWorkingCopy(std::move(document));
        return true;
    }

    [[nodiscard]] bool SetLayerStackEntry(
        std::uint32_t nodeId,
        std::size_t index,
        kb::render::RenderMaterialGraphLayerStackEntry entry) {
        if (!workingCopy_.has_value() || instanceWorkingCopy_.has_value()) {
            return false;
        }
        kb::render::RenderMaterialAssetData document = *workingCopy_;
        kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(document.graph, nodeId);
        if (node == nullptr || node->kind != kb::render::RenderMaterialGraphNodeKind::LayerStack || index >= node->layerStack.size()) {
            return false;
        }
        node->layerStack[index] = std::move(entry);
        SetWorkingCopy(std::move(document));
        return true;
    }

    [[nodiscard]] bool RemoveLayerStackEntry(std::uint32_t nodeId, std::size_t index) {
        if (!workingCopy_.has_value() || instanceWorkingCopy_.has_value()) {
            return false;
        }
        kb::render::RenderMaterialAssetData document = *workingCopy_;
        kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(document.graph, nodeId);
        if (node == nullptr || node->kind != kb::render::RenderMaterialGraphNodeKind::LayerStack || index >= node->layerStack.size()) {
            return false;
        }
        node->layerStack.erase(node->layerStack.begin() + static_cast<std::ptrdiff_t>(index));
        SetWorkingCopy(std::move(document));
        return true;
    }

    [[nodiscard]] bool ClearInstanceParameterOverride(
        std::string_view stableId,
        kb::render::RenderMaterialParameterType type) {
        if (!instanceWorkingCopy_.has_value() || !instanceParentSnapshot_.has_value() || stableId.empty()) {
            return false;
        }
        kb::render::RenderMaterialInstanceAssetData instance = *instanceWorkingCopy_;
        if (!RemoveGraphParameterOverride(instance.overrides, stableId, type)) {
            return false;
        }
        instance.hasOverrides = true;
        if (!kb::render::RenderMaterialInstanceAssetLoader::ValidateAgainstParent(instance, *instanceParentSnapshot_).Succeeded()) {
            return false;
        }
        kb::render::RenderMaterialAssetData effective =
            kb::render::BuildEffectiveRenderMaterialInstanceAsset(*instanceParentSnapshot_, instance);
        SetInstanceWorkingCopy(std::move(instance), std::move(effective));
        return true;
    }

    [[nodiscard]] bool SetInstanceStaticParameterOverride(
        std::string_view stableId,
        kb::render::RenderMaterialGraphNodeKind nodeKind,
        std::string value) {
        if (!instanceWorkingCopy_.has_value() || !instanceParentSnapshot_.has_value() ||
            stableId.empty() || !IsStaticOverrideNodeKind(nodeKind) || !IsStaticOverrideValueValid(nodeKind, value)) {
            return false;
        }
        const kb::render::RenderMaterialGraphNode* parentNode = FindStaticOverrideNode(*instanceParentSnapshot_, stableId, nodeKind);
        if (parentNode == nullptr) {
            return false;
        }
        kb::render::RenderMaterialInstanceAssetData instance = *instanceWorkingCopy_;
        RemoveStaticOverride(instance, stableId, nodeKind);
        const std::string parentValue = parentNode->parameter.defaultValueHint.empty()
            ? DefaultStaticOverrideValue(nodeKind)
            : parentNode->parameter.defaultValueHint;
        if (value != parentValue) {
            instance.staticParameterOverrides.push_back(kb::render::RenderMaterialInstanceStaticParameterOverride{
                .stableId = std::string{ stableId },
                .nodeKind = nodeKind,
                .value = std::move(value),
            });
        }
        if (!kb::render::RenderMaterialInstanceAssetLoader::ValidateAgainstParent(instance, *instanceParentSnapshot_).Succeeded()) {
            return false;
        }
        kb::render::RenderMaterialAssetData effective =
            kb::render::BuildEffectiveRenderMaterialInstanceAsset(*instanceParentSnapshot_, instance);
        SetInstanceWorkingCopy(std::move(instance), std::move(effective));
        return true;
    }

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
    [[nodiscard]] std::uint64_t DocumentRevision() const noexcept {
        return documentRevision_;
    }

    [[nodiscard]] bool IsGraphConstantInlineEditing() const noexcept {
        return inlineConstantEditNodeId_ != 0U;
    }

    // Arming the edit prefills the buffer with the node's current value, so "editing" alone is not pending
    // work — only a buffer that differs from that prefill is something the user would lose.
    [[nodiscard]] bool IsGraphConstantInlineEditDirty() const noexcept {
        return inlineConstantEditNodeId_ != 0U && inlineConstantEditBuffer_ != inlineConstantEditOriginal_;
    }

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

    [[nodiscard]] std::string GraphNodeDisplayName(std::uint32_t nodeId) const {
        if (!workingCopy_.has_value() || nodeId == 0U) {
            return {};
        }
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, nodeId);
        if (node == nullptr) {
            return {};
        }
        return GraphNodeDisplayNameForNode(*node);
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
        kb::render::RenderMaterialGraphNode node{
            .id = nodeId,
            .kind = kind,
            .positionX = positionX,
            .positionY = positionY,
            .parameter = DefaultParameterMetadata(kind, nodeId),
        };
        if (kind == kb::render::RenderMaterialGraphNodeKind::CustomCode) {
            node.customCode = DefaultCustomCode();
        } else if (kind == kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall) {
            node.customCode = DefaultMaterialFunctionCall();
        }
        document.graph.nodes.push_back(std::move(node));
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
        ++documentRevision_;
        return true;
    }

    [[nodiscard]] bool MoveGraphNodes(const std::vector<std::pair<std::uint32_t, std::pair<std::int32_t, std::int32_t>>>& positions) {
        if (!workingCopy_.has_value() || positions.empty()) {
            return false;
        }

        bool changed = false;
        for (const auto& position : positions) {
            kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(workingCopy_->graph, position.first);
            if (node == nullptr) {
                continue;
            }
            if (node->positionX != position.second.first || node->positionY != position.second.second) {
                node->positionX = position.second.first;
                node->positionY = position.second.second;
                changed = true;
                ++documentRevision_;
            }
        }
        return changed;
    }

    [[nodiscard]] bool SelectGraphUpstream() {
        return SelectGraphLinkedNodes(false);
    }

    [[nodiscard]] bool SelectGraphDownstream() {
        return SelectGraphLinkedNodes(true);
    }

    [[nodiscard]] bool AlignSelectedGraphNodes(MaterialEditorGraphAlignMode mode) {
        if (!workingCopy_.has_value() || selectedNodeIds_.size() < 2U) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        struct SelectedNodePosition {
            std::uint32_t nodeId = 0U;
            std::int32_t x = 0;
            std::int32_t y = 0;
        };
        std::vector<SelectedNodePosition> selectedNodes;
        selectedNodes.reserve(selectedNodeIds_.size());
        for (const std::uint32_t nodeId : selectedNodeIds_) {
            const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(document.graph, nodeId);
            if (node != nullptr) {
                selectedNodes.push_back(SelectedNodePosition{ .nodeId = nodeId, .x = node->positionX, .y = node->positionY });
            }
        }
        if (selectedNodes.size() < 2U) {
            return false;
        }

        std::int32_t minX = selectedNodes.front().x;
        std::int32_t maxX = selectedNodes.front().x;
        std::int32_t minY = selectedNodes.front().y;
        std::int32_t maxY = selectedNodes.front().y;
        for (const SelectedNodePosition& node : selectedNodes) {
            minX = std::min(minX, node.x);
            maxX = std::max(maxX, node.x);
            minY = std::min(minY, node.y);
            maxY = std::max(maxY, node.y);
        }

        bool changed = false;
        for (const SelectedNodePosition& selectedNode : selectedNodes) {
            kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(document.graph, selectedNode.nodeId);
            if (node == nullptr) {
                continue;
            }
            std::int32_t nextX = node->positionX;
            std::int32_t nextY = node->positionY;
            switch (mode) {
            case MaterialEditorGraphAlignMode::Left:
                nextX = minX;
                break;
            case MaterialEditorGraphAlignMode::Center:
                nextX = minX + ((maxX - minX) / 2);
                break;
            case MaterialEditorGraphAlignMode::Right:
                nextX = maxX;
                break;
            case MaterialEditorGraphAlignMode::Top:
                nextY = minY;
                break;
            case MaterialEditorGraphAlignMode::Middle:
                nextY = minY + ((maxY - minY) / 2);
                break;
            case MaterialEditorGraphAlignMode::Bottom:
                nextY = maxY;
                break;
            }
            if (node->positionX != nextX || node->positionY != nextY) {
                node->positionX = nextX;
                node->positionY = nextY;
                changed = true;
            }
        }
        if (!changed) {
            return false;
        }
        SetWorkingCopy(std::move(document));
        static_cast<void>(SetNodeSelection(selectedNodeIds_, selectedNodeId_));
        return true;
    }

    [[nodiscard]] bool DistributeSelectedGraphNodes(MaterialEditorGraphDistributeAxis axis) {
        if (!workingCopy_.has_value() || selectedNodeIds_.size() < 3U) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        struct SelectedNodePosition {
            std::uint32_t nodeId = 0U;
            std::int32_t x = 0;
            std::int32_t y = 0;
        };
        std::vector<SelectedNodePosition> selectedNodes;
        selectedNodes.reserve(selectedNodeIds_.size());
        for (const std::uint32_t nodeId : selectedNodeIds_) {
            const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(document.graph, nodeId);
            if (node != nullptr) {
                selectedNodes.push_back(SelectedNodePosition{ .nodeId = nodeId, .x = node->positionX, .y = node->positionY });
            }
        }
        if (selectedNodes.size() < 3U) {
            return false;
        }

        std::ranges::sort(selectedNodes, [axis](const SelectedNodePosition& lhs, const SelectedNodePosition& rhs) {
            if (axis == MaterialEditorGraphDistributeAxis::Horizontal && lhs.x != rhs.x) {
                return lhs.x < rhs.x;
            }
            if (axis == MaterialEditorGraphDistributeAxis::Vertical && lhs.y != rhs.y) {
                return lhs.y < rhs.y;
            }
            return lhs.nodeId < rhs.nodeId;
        });

        const std::int32_t first = axis == MaterialEditorGraphDistributeAxis::Horizontal ? selectedNodes.front().x : selectedNodes.front().y;
        const std::int32_t last = axis == MaterialEditorGraphDistributeAxis::Horizontal ? selectedNodes.back().x : selectedNodes.back().y;
        if (first == last) {
            return false;
        }

        bool changed = false;
        const std::int32_t span = last - first;
        const std::int32_t divisor = static_cast<std::int32_t>(selectedNodes.size() - 1U);
        for (std::size_t index = 1U; index + 1U < selectedNodes.size(); ++index) {
            const std::int32_t target = first + static_cast<std::int32_t>((static_cast<std::int64_t>(span) * static_cast<std::int64_t>(index)) / divisor);
            kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(document.graph, selectedNodes[index].nodeId);
            if (node == nullptr) {
                continue;
            }
            if (axis == MaterialEditorGraphDistributeAxis::Horizontal) {
                if (node->positionX != target) {
                    node->positionX = target;
                    changed = true;
                }
            } else if (node->positionY != target) {
                node->positionY = target;
                changed = true;
            }
        }
        if (!changed) {
            return false;
        }
        SetWorkingCopy(std::move(document));
        static_cast<void>(SetNodeSelection(selectedNodeIds_, selectedNodeId_));
        return true;
    }

    [[nodiscard]] bool CanPromoteSelectedGraphNodeToParameter() const {
        if (!workingCopy_.has_value() || selectedNodeId_ == 0U || selectedNodeIds_.size() != 1U) {
            return false;
        }
        const kb::render::RenderMaterialGraphNode* node =
            kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, selectedNodeId_);
        return node != nullptr && PromotedParameterKind(node->kind).has_value();
    }

    [[nodiscard]] bool PromoteSelectedGraphNodeToParameter(std::uint32_t* promotedNodeId = nullptr) {
        if (!workingCopy_.has_value() || selectedNodeId_ == 0U || selectedNodeIds_.size() != 1U) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(document.graph, selectedNodeId_);
        if (node == nullptr) {
            return false;
        }
        const kb::render::RenderMaterialGraphNodeKind sourceKind = node->kind;
        const std::optional<kb::render::RenderMaterialGraphNodeKind> targetKind = PromotedParameterKind(sourceKind);
        if (!targetKind.has_value()) {
            return false;
        }

        const std::string sourcePin = PromotedSourceOutputPin(sourceKind);
        const std::string targetPin = PromotedTargetOutputPin(*targetKind);
        kb::render::RenderMaterialGraphParameterMetadata metadata = DefaultParameterMetadata(*targetKind, node->id);
        metadata.defaultValueHint = PromotedParameterDefaultValueHint(*node, *targetKind);
        if (node->parameter.hasRange) {
            metadata.hasRange = node->parameter.hasRange;
            metadata.rangeMin = node->parameter.rangeMin;
            metadata.rangeMax = node->parameter.rangeMax;
        }
        metadata.overrideSupported = true;

        node->kind = *targetKind;
        node->parameter = std::move(metadata);
        const std::uint32_t targetPinId = kb::render::RenderMaterialGraphStablePinId(*node, targetPin, true);
        if (targetPinId == 0U) {
            return false;
        }
        for (kb::render::RenderMaterialGraphLink& link : document.graph.links) {
            if (link.fromNodeId == node->id && link.fromPin == sourcePin) {
                link.fromPin = targetPin;
                link.fromPinId = targetPinId;
                link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
            }
        }

        SetWorkingCopy(std::move(document));
        static_cast<void>(SetNodeSelection({ selectedNodeId_ }, selectedNodeId_));
        if (promotedNodeId != nullptr) {
            *promotedNodeId = selectedNodeId_;
        }
        return true;
    }

    [[nodiscard]] bool SetGraphConstantValue(std::uint32_t nodeId, std::string_view valueText) {
        if (!workingCopy_.has_value() || nodeId == 0U) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(document.graph, nodeId);
        if (node == nullptr || !IsGraphConstantNode(node->kind)) {
            return false;
        }

        std::optional<std::array<float, 4U>> parsed = ParseConstantValue(node->kind, valueText);
        if (!parsed.has_value()) {
            return false;
        }
        ClampConstantValues(*node, *parsed);

        const std::string nextValueHint = ConstantDefaultValueHint(node->kind, *parsed);
        node->parameter.defaultValueHint = nextValueHint;
        node->parameter.overrideSupported = false;
        if (node->parameter.displayName.empty()) {
            node->parameter.displayName = ConstantDisplayName(node->kind);
        }
        if (node->kind == kb::render::RenderMaterialGraphNodeKind::ConstantScalar ||
            node->kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor) {
            if (!node->parameter.hasRange) {
                node->parameter.hasRange = true;
                node->parameter.rangeMin = 0.0F;
                node->parameter.rangeMax = 1.0F;
            }
        }

        SetWorkingCopy(std::move(document));
        if (inlineConstantEditNodeId_ == nodeId) {
            inlineConstantEditBuffer_ = nextValueHint;
        }
        SelectNode(nodeId);
        return true;
    }

    [[nodiscard]] std::optional<float> GraphConstantComponentValue(std::uint32_t nodeId, std::size_t componentIndex) const {
        if (!workingCopy_.has_value() || nodeId == 0U) {
            return std::nullopt;
        }
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, nodeId);
        if (node != nullptr && node->kind == kb::render::RenderMaterialGraphNodeKind::TextureCoordinate) {
            if (componentIndex >= 2U) {
                return std::nullopt;
            }
            const std::array<float, 2U> tiling = TextureCoordinateTiling(*node);
            return tiling[componentIndex];
        }
        if (node != nullptr) {
            const std::span<const GraphHintNumericPropertyDefinition> definitions = GraphHintNumericProperties(node->kind);
            if (!definitions.empty()) {
                if (componentIndex >= definitions.size()) {
                    return std::nullopt;
                }
                const std::array<float, 4U> values = GraphHintNumericValues(*node);
                return values[componentIndex];
            }
        }
        if (node != nullptr && node->kind == kb::render::RenderMaterialGraphNodeKind::ColorRamp) {
            const std::optional<std::size_t> stopIndex = ColorRampStopIndexForPositionComponent(componentIndex);
            if (!stopIndex.has_value()) {
                return std::nullopt;
            }
            const std::vector<ColorRampStop> stops = ColorRampStops(*node);
            if (*stopIndex >= stops.size()) {
                return std::nullopt;
            }
            return stops[*stopIndex].position;
        }
        if (node == nullptr || !IsGraphConstantNode(node->kind)) {
            return std::nullopt;
        }
        const std::size_t componentCount = ConstantComponentCount(node->kind);
        if (componentIndex >= componentCount) {
            return std::nullopt;
        }
        std::array<float, 4U> values{};
        if (const std::optional<std::array<float, 4U>> parsed = ParseConstantValue(node->kind, node->parameter.defaultValueHint)) {
            values = *parsed;
        }
        return values[componentIndex];
    }

    [[nodiscard]] std::optional<kb::render::RenderMaterialParameterRange> GraphConstantComponentRange(
        std::uint32_t nodeId,
        std::size_t componentIndex) const {
        if (!workingCopy_.has_value() || nodeId == 0U) {
            return std::nullopt;
        }
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, nodeId);
        if (node != nullptr && node->kind == kb::render::RenderMaterialGraphNodeKind::TextureCoordinate && componentIndex < 2U) {
            return kb::render::RenderMaterialParameterRange{ .min = 0.0F, .max = 16.0F };
        }
        if (node != nullptr) {
            const std::span<const GraphHintNumericPropertyDefinition> definitions = GraphHintNumericProperties(node->kind);
            if (!definitions.empty() && componentIndex < definitions.size()) {
                return kb::render::RenderMaterialParameterRange{
                    .min = definitions[componentIndex].rangeMin,
                    .max = definitions[componentIndex].rangeMax,
                };
            }
        }
        if (node != nullptr && node->kind == kb::render::RenderMaterialGraphNodeKind::ColorRamp &&
            ColorRampStopIndexForPositionComponent(componentIndex).has_value()) {
            return kb::render::RenderMaterialParameterRange{ .min = 0.0F, .max = 1.0F };
        }
        if (node == nullptr || !IsGraphConstantNode(node->kind) || componentIndex >= ConstantComponentCount(node->kind)) {
            return std::nullopt;
        }
        return ConstantRange(*node);
    }

    [[nodiscard]] bool SetGraphConstantComponentValue(std::uint32_t nodeId, std::size_t componentIndex, float componentValue) {
        if (!workingCopy_.has_value() || nodeId == 0U || !std::isfinite(componentValue)) {
            return false;
        }
        kb::render::RenderMaterialAssetData document = *workingCopy_;
        if (kb::render::RenderMaterialGraphNode* textureCoordinateNode = FindMutableGraphNode(document.graph, nodeId);
            textureCoordinateNode != nullptr && textureCoordinateNode->kind == kb::render::RenderMaterialGraphNodeKind::TextureCoordinate) {
            if (componentIndex >= 2U) {
                return false;
            }
            std::array<float, 2U> tiling = TextureCoordinateTiling(*textureCoordinateNode);
            tiling[componentIndex] = std::clamp(componentValue, 0.0F, 16.0F);
            textureCoordinateNode->parameter.defaultValueHint =
                TextureCoordinateHint(tiling[0], tiling[1], GraphNodeUvSetValue(*textureCoordinateNode));
            textureCoordinateNode->parameter.overrideSupported = false;
            if (textureCoordinateNode->parameter.displayName.empty()) {
                textureCoordinateNode->parameter.displayName = GraphNodeDisplayName(textureCoordinateNode->kind);
            }
            SetWorkingCopy(std::move(document));
            SelectNode(nodeId);
            return true;
        }
        if (kb::render::RenderMaterialGraphNode* hintNumericNode = FindMutableGraphNode(document.graph, nodeId);
            hintNumericNode != nullptr && !GraphHintNumericProperties(hintNumericNode->kind).empty()) {
            const std::span<const GraphHintNumericPropertyDefinition> definitions = GraphHintNumericProperties(hintNumericNode->kind);
            if (componentIndex >= definitions.size()) {
                return false;
            }
            std::array<float, 4U> values = GraphHintNumericValues(*hintNumericNode);
            values[componentIndex] =
                std::clamp(componentValue, definitions[componentIndex].rangeMin, definitions[componentIndex].rangeMax);
            hintNumericNode->parameter.defaultValueHint = GraphHintNumericValueHint(hintNumericNode->kind, values);
            hintNumericNode->parameter.overrideSupported = false;
            if (hintNumericNode->parameter.displayName.empty()) {
                hintNumericNode->parameter.displayName = GraphNodeDisplayName(hintNumericNode->kind);
            }
            SetWorkingCopy(std::move(document));
            SelectNode(nodeId);
            return true;
        }
        if (kb::render::RenderMaterialGraphNode* colorRampNode = FindMutableGraphNode(document.graph, nodeId);
            colorRampNode != nullptr && colorRampNode->kind == kb::render::RenderMaterialGraphNodeKind::ColorRamp) {
            const std::optional<std::size_t> stopIndex = ColorRampStopIndexForPositionComponent(componentIndex);
            if (!stopIndex.has_value()) {
                return false;
            }
            std::vector<ColorRampStop> stops = ColorRampStops(*colorRampNode);
            if (*stopIndex >= stops.size()) {
                return false;
            }
            stops[*stopIndex].position = std::clamp(componentValue, 0.0F, 1.0F);
            colorRampNode->parameter.defaultValueHint = ColorRampHint(stops);
            colorRampNode->parameter.overrideSupported = false;
            if (colorRampNode->parameter.displayName.empty()) {
                colorRampNode->parameter.displayName = GraphNodeDisplayName(colorRampNode->kind);
            }
            SetWorkingCopy(std::move(document));
            SelectNode(nodeId);
            return true;
        }
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, nodeId);
        if (node == nullptr || !IsGraphConstantNode(node->kind)) {
            return false;
        }
        const std::size_t componentCount = ConstantComponentCount(node->kind);
        if (componentIndex >= componentCount) {
            return false;
        }
        std::array<float, 4U> values{};
        if (const std::optional<std::array<float, 4U>> parsed = ParseConstantValue(node->kind, node->parameter.defaultValueHint)) {
            values = *parsed;
        }
        values[componentIndex] = ClampConstantComponentValue(*node, componentValue);
        return SetGraphConstantValue(nodeId, ConstantDefaultValueHint(node->kind, values));
    }

    [[nodiscard]] bool SetGraphConstantColorValue(std::uint32_t nodeId, const std::array<float, 4U>& color) {
        if (!workingCopy_.has_value() || nodeId == 0U ||
            !std::ranges::all_of(color, [](float component) { return std::isfinite(component); })) {
            return false;
        }
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, nodeId);
        if (node == nullptr || node->kind != kb::render::RenderMaterialGraphNodeKind::ConstantColor) {
            return false;
        }
        std::array<float, 4U> values = color;
        ClampConstantValues(*node, values);
        return SetGraphConstantValue(nodeId, ConstantDefaultValueHint(node->kind, values));
    }

    [[nodiscard]] bool SetGraphNodeColorPropertyValue(
        std::uint32_t nodeId,
        std::string_view propertyId,
        const std::array<float, 4U>& color) {
        if (!workingCopy_.has_value() || nodeId == 0U || propertyId.empty() ||
            !std::ranges::all_of(color, [](float component) { return std::isfinite(component); })) {
            return false;
        }
        if (propertyId == "constant.color") {
            return SetGraphConstantColorValue(nodeId, color);
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(document.graph, nodeId);
        if (node == nullptr || node->kind != kb::render::RenderMaterialGraphNodeKind::ColorRamp) {
            return false;
        }
        const std::optional<std::size_t> stopIndex = ColorRampStopIndexForColorProperty(propertyId);
        if (!stopIndex.has_value()) {
            return false;
        }
        std::vector<ColorRampStop> stops = ColorRampStops(*node);
        if (*stopIndex >= stops.size()) {
            return false;
        }
        stops[*stopIndex].r = std::clamp(color[0], 0.0F, 1.0F);
        stops[*stopIndex].g = std::clamp(color[1], 0.0F, 1.0F);
        stops[*stopIndex].b = std::clamp(color[2], 0.0F, 1.0F);
        node->parameter.defaultValueHint = ColorRampHint(stops);
        node->parameter.overrideSupported = false;
        if (node->parameter.displayName.empty()) {
            node->parameter.displayName = GraphNodeDisplayName(node->kind);
        }

        SetWorkingCopy(std::move(document));
        SelectNode(nodeId);
        return true;
    }

    [[nodiscard]] bool SetGraphNodeEnumValue(std::uint32_t nodeId, std::string_view propertyId, std::string_view value) {
        if (!workingCopy_.has_value() || nodeId == 0U || propertyId.empty()) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(document.graph, nodeId);
        if (node == nullptr) {
            return false;
        }
        const std::vector<MaterialEditorGraphNodePropertyOption> options = GraphNodeEnumOptions(node->kind, propertyId);
        if (options.empty()) {
            return false;
        }
        const auto option = std::ranges::find_if(options, [value](const MaterialEditorGraphNodePropertyOption& candidate) {
            return candidate.value == value;
        });
        if (option == options.end()) {
            return false;
        }
        if (node->kind == kb::render::RenderMaterialGraphNodeKind::TextureCoordinate && propertyId == "uvSet") {
            node->parameter.defaultValueHint = TextureCoordinateHintWithUvSet(*node, option->value);
        } else if (node->kind == kb::render::RenderMaterialGraphNodeKind::StaticComponentMask &&
            IsStaticComponentMaskPropertyId(propertyId)) {
            node->parameter.defaultValueHint = StaticComponentMaskHintWithChannel(*node, propertyId, option->value == "true");
        } else if (IsTransformSpacePropertyId(propertyId) && IsTransformSpaceNode(node->kind)) {
            node->parameter.defaultValueHint = TransformSpaceHintWithProperty(*node, propertyId, option->value);
        } else if (node->kind == kb::render::RenderMaterialGraphNodeKind::CustomCode &&
            propertyId == "customCode.outputType") {
            const std::optional<kb::render::RenderMaterialGraphPinType> outputType =
                kb::render::ParseRenderMaterialGraphPinType(option->value);
            if (!outputType.has_value() || !IsCustomCodeEditableValueType(*outputType)) {
                return false;
            }
            node->customCode.outputType = *outputType;
        } else {
            node->parameter.defaultValueHint = option->value;
        }
        node->parameter.overrideSupported = false;
        if (node->parameter.displayName.empty()) {
            node->parameter.displayName = GraphNodeDisplayName(node->kind);
        }

        SetWorkingCopy(std::move(document));
        SelectNode(nodeId);
        CloseGraphNodeEnumDropdown();
        return true;
    }

    [[nodiscard]] bool SetGraphNodeTextProperty(std::uint32_t nodeId, std::string_view propertyId, std::string_view value) {
        if (!workingCopy_.has_value() || nodeId == 0U || propertyId.empty()) {
            return false;
        }
        if (propertyId == "node.name") {
            return RenameGraphNode(nodeId, value);
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(document.graph, nodeId);
        if (node == nullptr) {
            return false;
        }

        const std::string normalized = TrimAscii(value);
        if (node->kind == kb::render::RenderMaterialGraphNodeKind::CustomCode) {
            if (propertyId == "customCode.body") {
                if (normalized.empty()) {
                    return false;
                }
                node->customCode.body = normalized;
            } else if (propertyId == "customCode.defines") {
                node->customCode.defines = normalized;
            } else if (propertyId == "customCode.includes") {
                node->customCode.includes = normalized;
            } else {
                return false;
            }
            if (node->parameter.displayName.empty()) {
                node->parameter.displayName = GraphNodeDisplayName(node->kind);
            }
            SetWorkingCopy(std::move(document));
            SelectNode(nodeId);
            return true;
        }

        if (node->kind == kb::render::RenderMaterialGraphNodeKind::CollectionParameter) {
            if (propertyId == "collection.assetId") {
                std::uint64_t assetId = 0U;
                if (!ParseDecimalAssetId(normalized, assetId) || assetId == 0U) {
                    return false;
                }
                node->parameter.defaultValueHint = std::to_string(assetId);
                SetWorkingCopy(std::move(document));
                SelectNode(nodeId);
                return true;
            }
            if (propertyId == "collection.parameter") {
                if (!IsStableGraphIdentifier(normalized)) {
                    return false;
                }
                node->parameter.stableId = normalized;
                if (node->parameter.displayName.empty()) {
                    node->parameter.displayName = "Collection Parameter " + std::to_string(node->id);
                }
                SetWorkingCopy(std::move(document));
                SelectNode(nodeId);
                return true;
            }
        }

        if (node->kind == kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall && propertyId == "function.assetId") {
            std::uint64_t assetId = 0U;
            if (!ParseDecimalAssetId(normalized, assetId) || assetId == 0U) {
                return false;
            }
            node->parameter.stableId = std::to_string(assetId);
            node->parameter.defaultValueHint.clear();
            SetWorkingCopy(std::move(document));
            SelectNode(nodeId);
            return true;
        }

        if ((node->kind == kb::render::RenderMaterialGraphNodeKind::FunctionInput ||
                node->kind == kb::render::RenderMaterialGraphNodeKind::FunctionOutput) &&
            propertyId == "function.endpoint") {
            if (!IsStableGraphIdentifier(normalized)) {
                return false;
            }
            node->parameter.stableId = normalized;
            node->parameter.displayName = normalized;
            SetWorkingCopy(std::move(document));
            SelectNode(nodeId);
            return true;
        }

        return false;
    }

    [[nodiscard]] bool SetGraphMaterialFunctionCallSignature(
        std::uint32_t nodeId,
        std::uint64_t functionAssetId,
        const kb::render::RenderMaterialGraphDocument& functionGraph) {
        if (!workingCopy_.has_value() || nodeId == 0U || functionAssetId == 0U) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(document.graph, nodeId);
        if (node == nullptr || node->kind != kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall) {
            return false;
        }
        node->parameter.stableId = std::to_string(functionAssetId);
        node->parameter.defaultValueHint.clear();
        node->customCode = kb::render::BuildRenderMaterialFunctionCallCustomCode(functionGraph);

        const auto linkStillValid = [&document](kb::render::RenderMaterialGraphLink& link) {
            const kb::render::RenderMaterialGraphNode* fromNode = kb::render::FindRenderMaterialGraphNode(document.graph, link.fromNodeId);
            const kb::render::RenderMaterialGraphNode* toNode = kb::render::FindRenderMaterialGraphNode(document.graph, link.toNodeId);
            if (fromNode == nullptr || toNode == nullptr ||
                !kb::render::IsRenderMaterialGraphOutputPin(*fromNode, link.fromPin) ||
                !kb::render::IsRenderMaterialGraphInputPin(*toNode, link.toPin) ||
                !kb::render::AreRenderMaterialGraphPinsCompatible(*fromNode, link.fromPin, *toNode, link.toPin)) {
                return false;
            }
            link.fromPinId = kb::render::RenderMaterialGraphStablePinId(*fromNode, link.fromPin, true);
            link.toPinId = kb::render::RenderMaterialGraphStablePinId(*toNode, link.toPin, false);
            link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
            return link.fromPinId != 0U && link.toPinId != 0U && link.id != 0U;
        };
        document.graph.links.erase(
            std::remove_if(document.graph.links.begin(), document.graph.links.end(), [&](kb::render::RenderMaterialGraphLink& link) {
                if (link.fromNodeId != nodeId && link.toNodeId != nodeId) {
                    return false;
                }
                return !linkStillValid(link);
            }),
            document.graph.links.end());

        SetWorkingCopy(std::move(document));
        SelectNode(nodeId);
        return true;
    }

    [[nodiscard]] std::vector<MaterialEditorGraphNodeProperty> GraphNodeProperties(std::uint32_t nodeId) const {
        std::vector<MaterialEditorGraphNodeProperty> properties;
        if (!workingCopy_.has_value() || nodeId == 0U) {
            return properties;
        }
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, nodeId);
        if (node == nullptr) {
            return properties;
        }

        properties.push_back(MaterialEditorGraphNodeProperty{
            .nodeId = node->id,
            .stableId = "node.name",
            .displayName = "Name",
            .kind = MaterialEditorGraphNodePropertyKind::Text,
            .type = kb::render::RenderMaterialParameterType::Enum,
            .value = EnumValue(IsGraphNodeRenameEditing(node->id) ? std::string{ GraphNodeRenameEditBuffer() } : GraphNodeDisplayNameForNode(*node)),
        });

        if (IsGraphConstantNode(node->kind)) {
            std::array<float, 4U> values{ 0.0F, 0.0F, 0.0F, 1.0F };
            if (const std::optional<std::array<float, 4U>> parsed = ParseConstantValue(node->kind, node->parameter.defaultValueHint)) {
                values = *parsed;
            }
            const std::optional<kb::render::RenderMaterialParameterRange> range = ConstantRange(*node);
            if (node->kind == kb::render::RenderMaterialGraphNodeKind::ConstantBool) {
                const std::string boolText = ConstantBoolDefaultValueHint(values[0] != 0.0F);
                properties.push_back(MaterialEditorGraphNodeProperty{
                    .nodeId = node->id,
                    .stableId = "constant.bool",
                    .displayName = "Value",
                    .kind = MaterialEditorGraphNodePropertyKind::Enum,
                    .type = kb::render::RenderMaterialParameterType::Bool,
                    .value = EnumValue(boolText),
                    .options = GraphNodeEnumOptions(node->kind, "constant.bool"),
                    .dropdownOpen = IsGraphNodeEnumDropdownOpen(node->id, "constant.bool"),
                });
                return properties;
            }
            if (node->kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor) {
                properties.push_back(MaterialEditorGraphNodeProperty{
                    .nodeId = node->id,
                    .stableId = "constant.color",
                    .displayName = "Color",
                    .kind = MaterialEditorGraphNodePropertyKind::Color,
                    .type = kb::render::RenderMaterialParameterType::Color,
                    .value = Vec4Value(values.data(), MaterialEditorParameterValueKind::Color),
                    .range = range,
                });
            }

            const std::size_t componentCount = ConstantComponentCount(node->kind);
            const std::array<std::string_view, 4U> labels = ConstantComponentLabels(node->kind);
            for (std::size_t componentIndex = 0U; componentIndex < componentCount; ++componentIndex) {
                properties.push_back(MaterialEditorGraphNodeProperty{
                    .nodeId = node->id,
                    .stableId = "constant." + std::to_string(componentIndex),
                    .displayName = std::string{ labels[componentIndex] },
                    .kind = MaterialEditorGraphNodePropertyKind::Numeric,
                    .type = kb::render::RenderMaterialParameterType::Scalar,
                    .value = ScalarValue(values[componentIndex]),
                    .range = range,
                    .componentIndex = componentIndex,
                });
            }
            return properties;
        }

        if (node->kind == kb::render::RenderMaterialGraphNodeKind::TextureCoordinate) {
            const std::array<float, 2U> tiling = TextureCoordinateTiling(*node);
            constexpr std::array<std::string_view, 2U> labels{ "U Tiling", "V Tiling" };
            for (std::size_t componentIndex = 0U; componentIndex < labels.size(); ++componentIndex) {
                properties.push_back(MaterialEditorGraphNodeProperty{
                    .nodeId = node->id,
                    .stableId = "textureCoordinate.tiling." + std::to_string(componentIndex),
                    .displayName = std::string{ labels[componentIndex] },
                    .kind = MaterialEditorGraphNodePropertyKind::Numeric,
                    .type = kb::render::RenderMaterialParameterType::Scalar,
                    .value = ScalarValue(tiling[componentIndex]),
                    .range = kb::render::RenderMaterialParameterRange{ .min = 0.0F, .max = 16.0F },
                    .componentIndex = componentIndex,
                });
            }
        }

        if (const std::span<const GraphHintNumericPropertyDefinition> definitions = GraphHintNumericProperties(node->kind);
            !definitions.empty()) {
            const std::array<float, 4U> values = GraphHintNumericValues(*node);
            for (std::size_t componentIndex = 0U; componentIndex < definitions.size(); ++componentIndex) {
                properties.push_back(MaterialEditorGraphNodeProperty{
                    .nodeId = node->id,
                    .stableId = std::string{ definitions[componentIndex].stableId },
                    .displayName = std::string{ definitions[componentIndex].displayName },
                    .kind = MaterialEditorGraphNodePropertyKind::Numeric,
                    .type = kb::render::RenderMaterialParameterType::Scalar,
                    .value = ScalarValue(values[componentIndex]),
                    .range = kb::render::RenderMaterialParameterRange{
                        .min = definitions[componentIndex].rangeMin,
                        .max = definitions[componentIndex].rangeMax,
                    },
                    .componentIndex = componentIndex,
                });
            }
        }

        if (node->kind == kb::render::RenderMaterialGraphNodeKind::ColorRamp) {
            const std::vector<ColorRampStop> stops = ColorRampStops(*node);
            for (std::size_t stopIndex = 0U; stopIndex < 2U && stopIndex < stops.size(); ++stopIndex) {
                const ColorRampStop& stop = stops[stopIndex];
                properties.push_back(MaterialEditorGraphNodeProperty{
                    .nodeId = node->id,
                    .stableId = "colorRamp.stop" + std::to_string(stopIndex) + ".position",
                    .displayName = "Stop " + std::to_string(stopIndex) + " Position",
                    .kind = MaterialEditorGraphNodePropertyKind::Numeric,
                    .type = kb::render::RenderMaterialParameterType::Scalar,
                    .value = ScalarValue(stop.position),
                    .range = kb::render::RenderMaterialParameterRange{ .min = 0.0F, .max = 1.0F },
                    .componentIndex = stopIndex * 4U,
                });
                const std::array<float, 4U> color{ stop.r, stop.g, stop.b, 1.0F };
                properties.push_back(MaterialEditorGraphNodeProperty{
                    .nodeId = node->id,
                    .stableId = "colorRamp.stop" + std::to_string(stopIndex) + ".color",
                    .displayName = "Stop " + std::to_string(stopIndex) + " Color",
                    .kind = MaterialEditorGraphNodePropertyKind::Color,
                    .type = kb::render::RenderMaterialParameterType::Color,
                    .value = Vec4Value(color.data(), MaterialEditorParameterValueKind::Color),
                    .range = kb::render::RenderMaterialParameterRange{ .min = 0.0F, .max = 1.0F },
                });
            }
        }

        if (IsGraphTextureAssetNode(node->kind)) {
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "texture.asset",
                .displayName = node->parameter.displayName.empty() ? std::string{ "Texture" } : node->parameter.displayName,
                .kind = MaterialEditorGraphNodePropertyKind::TextureAsset,
                .type = kb::render::RenderMaterialParameterType::Texture,
                .value = TextureAssetValue(GraphNodeTextureAssetId(*node, *workingCopy_)),
            });
        }

        if (node->kind == kb::render::RenderMaterialGraphNodeKind::CustomCode) {
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "customCode.body",
                .displayName = "Body",
                .kind = MaterialEditorGraphNodePropertyKind::Text,
                .type = kb::render::RenderMaterialParameterType::Enum,
                .value = EnumValue(node->customCode.body),
            });
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "customCode.defines",
                .displayName = "Defines",
                .kind = MaterialEditorGraphNodePropertyKind::Text,
                .type = kb::render::RenderMaterialParameterType::Enum,
                .value = EnumValue(node->customCode.defines),
            });
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "customCode.includes",
                .displayName = "Includes",
                .kind = MaterialEditorGraphNodePropertyKind::Text,
                .type = kb::render::RenderMaterialParameterType::Enum,
                .value = EnumValue(node->customCode.includes),
            });
            const std::vector<MaterialEditorGraphNodePropertyOption> options =
                GraphNodeEnumOptions(node->kind, "customCode.outputType");
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "customCode.outputType",
                .displayName = "Output Type",
                .kind = MaterialEditorGraphNodePropertyKind::Enum,
                .type = kb::render::RenderMaterialParameterType::Enum,
                .value = EnumValue(std::string{ kb::render::RenderMaterialGraphPinTypeName(node->customCode.outputType) }),
                .options = options,
                .dropdownOpen = IsGraphNodeEnumDropdownOpen(node->id, "customCode.outputType"),
            });
        }

        if (node->kind == kb::render::RenderMaterialGraphNodeKind::CollectionParameter) {
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "collection.assetId",
                .displayName = "Collection Asset",
                .kind = MaterialEditorGraphNodePropertyKind::Text,
                .type = kb::render::RenderMaterialParameterType::Enum,
                .value = EnumValue(node->parameter.defaultValueHint),
            });
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "collection.parameter",
                .displayName = "Parameter Stable Id",
                .kind = MaterialEditorGraphNodePropertyKind::Text,
                .type = kb::render::RenderMaterialParameterType::Enum,
                .value = EnumValue(StableIdForGraphNode(*node)),
            });
        } else if (node->kind == kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall) {
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "function.assetId",
                .displayName = "Function Asset",
                .kind = MaterialEditorGraphNodePropertyKind::Text,
                .type = kb::render::RenderMaterialParameterType::Enum,
                .value = EnumValue(node->parameter.stableId.empty() ? node->parameter.defaultValueHint : node->parameter.stableId),
            });
        } else if (node->kind == kb::render::RenderMaterialGraphNodeKind::FunctionInput ||
            node->kind == kb::render::RenderMaterialGraphNodeKind::FunctionOutput) {
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "function.endpoint",
                .displayName = "Endpoint Stable Id",
                .kind = MaterialEditorGraphNodePropertyKind::Text,
                .type = kb::render::RenderMaterialParameterType::Enum,
                .value = EnumValue(StableIdForGraphNode(*node)),
            });
        }

        if (const std::vector<MaterialEditorGraphNodePropertyOption> options = GraphNodeEnumOptions(node->kind, "uvSet");
            !options.empty()) {
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "uvSet",
                .displayName = "UV Set",
                .kind = MaterialEditorGraphNodePropertyKind::Enum,
                .type = kb::render::RenderMaterialParameterType::Enum,
                .value = EnumValue(GraphNodeUvSetValue(*node)),
                .options = options,
                .dropdownOpen = IsGraphNodeEnumDropdownOpen(node->id, "uvSet"),
            });
        }
        if (const std::vector<MaterialEditorGraphNodePropertyOption> options = GraphNodeEnumOptions(node->kind, "viewProperty");
            !options.empty()) {
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "viewProperty",
                .displayName = "Property",
                .kind = MaterialEditorGraphNodePropertyKind::Enum,
                .type = kb::render::RenderMaterialParameterType::Enum,
                .value = EnumValue(GraphNodeViewPropertyValue(*node)),
                .options = options,
                .dropdownOpen = IsGraphNodeEnumDropdownOpen(node->id, "viewProperty"),
            });
        }
        if (const std::vector<MaterialEditorGraphNodePropertyOption> options = GraphNodeEnumOptions(node->kind, "sceneTexture.source");
            !options.empty()) {
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "sceneTexture.source",
                .displayName = "Source",
                .kind = MaterialEditorGraphNodePropertyKind::Enum,
                .type = kb::render::RenderMaterialParameterType::Enum,
                .value = EnumValue(GraphNodeSceneTextureSourceValue(*node)),
                .options = options,
                .dropdownOpen = IsGraphNodeEnumDropdownOpen(node->id, "sceneTexture.source"),
            });
        }
        if (const std::vector<MaterialEditorGraphNodePropertyOption> options = GraphNodeEnumOptions(node->kind, "staticSwitch.selector");
            !options.empty()) {
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "staticSwitch.selector",
                .displayName = "Default Branch",
                .kind = MaterialEditorGraphNodePropertyKind::Enum,
                .type = kb::render::RenderMaterialParameterType::Bool,
                .value = EnumValue(ParseStaticBoolNodeValue(*node) ? "true" : "false"),
                .options = options,
                .dropdownOpen = IsGraphNodeEnumDropdownOpen(node->id, "staticSwitch.selector"),
            });
        }
        if (node->kind == kb::render::RenderMaterialGraphNodeKind::StaticComponentMask) {
            constexpr std::array<std::pair<std::string_view, std::string_view>, 4U> maskProperties{
                std::pair<std::string_view, std::string_view>{ "staticComponentMask.r", "Red" },
                std::pair<std::string_view, std::string_view>{ "staticComponentMask.g", "Green" },
                std::pair<std::string_view, std::string_view>{ "staticComponentMask.b", "Blue" },
                std::pair<std::string_view, std::string_view>{ "staticComponentMask.a", "Alpha" },
            };
            const std::vector<MaterialEditorGraphNodePropertyOption> options =
                GraphNodeEnumOptions(node->kind, "staticComponentMask.r");
            for (const auto& [propertyId, label] : maskProperties) {
                properties.push_back(MaterialEditorGraphNodeProperty{
                    .nodeId = node->id,
                    .stableId = std::string{ propertyId },
                    .displayName = std::string{ label },
                    .kind = MaterialEditorGraphNodePropertyKind::Enum,
                    .type = kb::render::RenderMaterialParameterType::Bool,
                    .value = EnumValue(StaticComponentMaskChannelEnabled(*node, propertyId) ? "true" : "false"),
                    .options = options,
                    .dropdownOpen = IsGraphNodeEnumDropdownOpen(node->id, propertyId),
                });
            }
        }
        if (IsTransformSpaceNode(node->kind)) {
            constexpr std::array<std::pair<std::string_view, std::string_view>, 2U> transformProperties{
                std::pair<std::string_view, std::string_view>{ "transform.fromSpace", "From Space" },
                std::pair<std::string_view, std::string_view>{ "transform.toSpace", "To Space" },
            };
            const std::array<std::string, 2U> spaces = TransformSpaces(*node);
            for (std::size_t index = 0U; index < transformProperties.size(); ++index) {
                const auto& [propertyId, label] = transformProperties[index];
                const std::vector<MaterialEditorGraphNodePropertyOption> options = GraphNodeEnumOptions(node->kind, propertyId);
                properties.push_back(MaterialEditorGraphNodeProperty{
                    .nodeId = node->id,
                    .stableId = std::string{ propertyId },
                    .displayName = std::string{ label },
                    .kind = MaterialEditorGraphNodePropertyKind::Enum,
                    .type = kb::render::RenderMaterialParameterType::Enum,
                    .value = EnumValue(spaces[index]),
                    .options = options,
                    .dropdownOpen = IsGraphNodeEnumDropdownOpen(node->id, propertyId),
                });
            }
        }

        return properties;
    }

    void ToggleGraphNodeEnumDropdown(std::uint32_t nodeId, std::string propertyId) {
        if (nodeId == 0U || propertyId.empty()) {
            CloseGraphNodeEnumDropdown();
            return;
        }
        if (graphNodeEnumDropdownNodeId_ == nodeId && graphNodeEnumDropdownPropertyId_ == propertyId) {
            CloseGraphNodeEnumDropdown();
            return;
        }
        graphNodeEnumDropdownNodeId_ = nodeId;
        graphNodeEnumDropdownPropertyId_ = std::move(propertyId);
    }

    void CloseGraphNodeEnumDropdown() noexcept {
        graphNodeEnumDropdownNodeId_ = 0U;
        graphNodeEnumDropdownPropertyId_.clear();
    }

    [[nodiscard]] bool IsGraphNodeEnumDropdownOpen(std::uint32_t nodeId, std::string_view propertyId) const noexcept {
        return nodeId != 0U &&
            graphNodeEnumDropdownNodeId_ == nodeId &&
            graphNodeEnumDropdownPropertyId_ == propertyId;
    }

    [[nodiscard]] bool BeginGraphConstantInlineEdit(std::uint32_t nodeId) {
        if (!workingCopy_.has_value() || nodeId == 0U) {
            return false;
        }
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, nodeId);
        if (node == nullptr || !IsGraphConstantNode(node->kind)) {
            return false;
        }
        inlineConstantEditNodeId_ = nodeId;
        inlineConstantEditBuffer_ = node->parameter.defaultValueHint;
        if (inlineConstantEditBuffer_.empty()) {
            inlineConstantEditBuffer_ = ConstantDefaultValueHint(node->kind, {});
        }
        inlineConstantEditOriginal_ = inlineConstantEditBuffer_;
        SelectNode(nodeId);
        return true;
    }

    void AppendGraphConstantInlineEditText(wchar_t character) {
        if (inlineConstantEditNodeId_ == 0U || character < 32 || character > 126) {
            return;
        }
        const char ch = static_cast<char>(character);
        if ((ch >= '0' && ch <= '9') || ch == '.' || ch == ',' || ch == '-' || ch == '+' || ch == ' ' ||
            ch == 't' || ch == 'T' || ch == 'r' || ch == 'R' || ch == 'u' || ch == 'U' || ch == 'e' || ch == 'E' ||
            ch == 'f' || ch == 'F' || ch == 'a' || ch == 'A' || ch == 'l' || ch == 'L' || ch == 's' || ch == 'S') {
            inlineConstantEditBuffer_.push_back(ch);
        }
    }

    void BackspaceGraphConstantInlineEdit() {
        if (inlineConstantEditNodeId_ != 0U && !inlineConstantEditBuffer_.empty()) {
            inlineConstantEditBuffer_.pop_back();
        }
    }

    void CancelGraphConstantInlineEdit() noexcept {
        inlineConstantEditNodeId_ = 0U;
        inlineConstantEditBuffer_.clear();
        inlineConstantEditOriginal_.clear();
    }

    [[nodiscard]] bool BeginGraphNodeRenameEdit(std::uint32_t nodeId) {
        if (!workingCopy_.has_value() || nodeId == 0U) {
            return false;
        }
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, nodeId);
        if (node == nullptr) {
            return false;
        }
        CancelGraphConstantInlineEdit();
        renameNodeId_ = nodeId;
        renameBuffer_ = GraphNodeDisplayNameForNode(*node);
        renameSelectAll_ = true;
        SelectNode(nodeId);
        return true;
    }

    void AppendGraphNodeRenameEditText(wchar_t character) {
        if (renameNodeId_ == 0U || character < 32 || character > 126) {
            return;
        }
        ReplaceSelectedGraphNodeRenameText();
        renameBuffer_.push_back(static_cast<char>(character));
    }

    void InsertGraphNodeRenameEditText(std::string_view text) {
        if (renameNodeId_ == 0U) {
            return;
        }
        ReplaceSelectedGraphNodeRenameText();
        for (const char ch : text) {
            const unsigned char value = static_cast<unsigned char>(ch);
            if (value >= 32U && value <= 126U) {
                renameBuffer_.push_back(static_cast<char>(value));
            }
        }
    }

    void BackspaceGraphNodeRenameEdit() {
        if (renameNodeId_ == 0U) {
            return;
        }
        if (renameSelectAll_) {
            ClearGraphNodeRenameEditText();
        } else if (!renameBuffer_.empty()) {
            renameBuffer_.pop_back();
        }
    }

    void ClearGraphNodeRenameEditText() {
        if (renameNodeId_ != 0U) {
            renameBuffer_.clear();
            renameSelectAll_ = false;
        }
    }

    void SelectAllGraphNodeRenameEditText() noexcept {
        if (renameNodeId_ != 0U) {
            renameSelectAll_ = true;
        }
    }

    void CancelGraphNodeRenameEdit() noexcept {
        renameNodeId_ = 0U;
        renameBuffer_.clear();
        renameSelectAll_ = false;
    }

    [[nodiscard]] bool RenameGraphNode(std::uint32_t nodeId, std::string_view displayName) {
        if (!workingCopy_.has_value() || nodeId == 0U) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        kb::render::RenderMaterialGraphNode* node = FindMutableGraphNode(document.graph, nodeId);
        if (node == nullptr) {
            return false;
        }
        const std::string normalized = NormalizeGraphNodeDisplayName(displayName, node->kind);
        if (node->parameter.displayName == normalized) {
            return false;
        }
        node->parameter.displayName = normalized;
        SetWorkingCopy(std::move(document));
        SelectNode(nodeId);
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
        RemoveGraphCompositeNodeReferences(document.graph, std::vector<std::uint32_t>{ nodeId });
        SetWorkingCopy(std::move(document));
        ClearNodeSelection();
        return true;
    }

    [[nodiscard]] bool DeleteSelectedGraphNodes() {
        if (!workingCopy_.has_value() || selectedNodeIds_.empty()) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        std::vector<std::uint32_t> deleteIds;
        for (std::uint32_t nodeId : selectedNodeIds_) {
            const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(document.graph, nodeId);
            if (node != nullptr && node->kind != kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
                deleteIds.push_back(nodeId);
            }
        }
        if (deleteIds.empty()) {
            return false;
        }

        const auto shouldDeleteNode = [&deleteIds](const kb::render::RenderMaterialGraphNode& node) {
            return std::ranges::find(deleteIds, node.id) != deleteIds.end();
        };
        const auto shouldDeleteLink = [&deleteIds](const kb::render::RenderMaterialGraphLink& link) {
            return std::ranges::find(deleteIds, link.fromNodeId) != deleteIds.end() ||
                std::ranges::find(deleteIds, link.toNodeId) != deleteIds.end();
        };
        const auto nodeEnd = std::remove_if(document.graph.nodes.begin(), document.graph.nodes.end(), shouldDeleteNode);
        document.graph.nodes.erase(nodeEnd, document.graph.nodes.end());
        const auto linkEnd = std::remove_if(document.graph.links.begin(), document.graph.links.end(), shouldDeleteLink);
        document.graph.links.erase(linkEnd, document.graph.links.end());
        RemoveGraphCompositeNodeReferences(document.graph, deleteIds);
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
            !kb::render::IsRenderMaterialGraphOutputPin(*fromNode, fromPin) ||
            !kb::render::IsRenderMaterialGraphInputPin(*toNode, toPin) ||
            !kb::render::AreRenderMaterialGraphPinsCompatible(*fromNode, fromPin, *toNode, toPin)) {
            return false;
        }

        kb::render::RenderMaterialGraphLink link{
            .fromNodeId = fromNodeId,
            .fromPinId = kb::render::RenderMaterialGraphStablePinId(*fromNode, fromPin, true),
            .fromPin = std::string{ fromPin },
            .toNodeId = toNodeId,
            .toPinId = kb::render::RenderMaterialGraphStablePinId(*toNode, toPin, false),
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

    [[nodiscard]] bool AddGraphComment(
        std::string text,
        std::int32_t positionX,
        std::int32_t positionY,
        std::int32_t width = 320,
        std::int32_t height = 180,
        std::uint32_t color = 0x4A6385U,
        std::uint32_t* createdCommentId = nullptr) {
        if (!workingCopy_.has_value()) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        EnsureEditableGraph(document.graph);
        const std::uint32_t commentId = NextGraphCommentId(document.graph);
        kb::render::RenderMaterialGraphCommentBox comment{
            .id = commentId,
            .positionX = positionX,
            .positionY = positionY,
            .width = std::max<std::int32_t>(32, width),
            .height = std::max<std::int32_t>(32, height),
            .color = color,
            .text = text.empty() ? std::string{ "Comment" } : std::move(text),
        };
        document.graph.comments.push_back(std::move(comment));
        if (createdCommentId != nullptr) {
            *createdCommentId = commentId;
        }
        SetWorkingCopy(std::move(document));
        SelectComment(commentId);
        return true;
    }

    [[nodiscard]] std::optional<kb::render::RenderMaterialGraphCommentBox> GraphComment(std::uint32_t commentId) const {
        if (!workingCopy_.has_value()) {
            return std::nullopt;
        }
        const kb::render::RenderMaterialGraphCommentBox* comment = FindGraphComment(workingCopy_->graph, commentId);
        if (comment == nullptr) {
            return std::nullopt;
        }
        return *comment;
    }

    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> GraphCommentPosition(std::uint32_t commentId) const {
        const std::optional<kb::render::RenderMaterialGraphCommentBox> comment = GraphComment(commentId);
        if (!comment.has_value()) {
            return std::nullopt;
        }
        return std::pair<std::int32_t, std::int32_t>{ comment->positionX, comment->positionY };
    }

    [[nodiscard]] std::vector<std::uint32_t> GraphNodeIdsInsideComment(std::uint32_t commentId) const {
        std::vector<std::uint32_t> nodeIds;
        if (!workingCopy_.has_value()) {
            return nodeIds;
        }
        const kb::render::RenderMaterialGraphCommentBox* comment = FindGraphComment(workingCopy_->graph, commentId);
        if (comment == nullptr) {
            return nodeIds;
        }
        for (const kb::render::RenderMaterialGraphNode& node : workingCopy_->graph.nodes) {
            if (GraphNodeInsideComment(node, *comment)) {
                nodeIds.push_back(node.id);
            }
        }
        return nodeIds;
    }

    [[nodiscard]] bool MoveGraphComment(std::uint32_t commentId, std::int32_t positionX, std::int32_t positionY) {
        if (!workingCopy_.has_value() || commentId == 0U) {
            return false;
        }
        kb::render::RenderMaterialGraphCommentBox* comment = FindMutableGraphComment(workingCopy_->graph, commentId);
        if (comment == nullptr || (comment->positionX == positionX && comment->positionY == positionY)) {
            return false;
        }
        comment->positionX = positionX;
        comment->positionY = positionY;
        ++documentRevision_;
        return true;
    }

    [[nodiscard]] bool MoveGraphCommentGroup(std::uint32_t commentId, std::int32_t positionX, std::int32_t positionY) {
        return MoveGraphCommentGroup(commentId, positionX, positionY, GraphNodeIdsInsideComment(commentId));
    }

    [[nodiscard]] bool MoveGraphCommentGroup(
        std::uint32_t commentId,
        std::int32_t positionX,
        std::int32_t positionY,
        std::span<const std::uint32_t> memberNodeIds) {
        if (!workingCopy_.has_value() || commentId == 0U) {
            return false;
        }
        kb::render::RenderMaterialGraphCommentBox* comment = FindMutableGraphComment(workingCopy_->graph, commentId);
        if (comment == nullptr) {
            return false;
        }
        const std::int32_t deltaX = positionX - comment->positionX;
        const std::int32_t deltaY = positionY - comment->positionY;
        if (deltaX == 0 && deltaY == 0) {
            return false;
        }

        comment->positionX = positionX;
        comment->positionY = positionY;
        for (kb::render::RenderMaterialGraphNode& node : workingCopy_->graph.nodes) {
            if (std::ranges::find(memberNodeIds, node.id) != memberNodeIds.end()) {
                node.positionX += deltaX;
                node.positionY += deltaY;
            }
        }
        ++documentRevision_;
        return true;
    }

    [[nodiscard]] bool DeleteSelectedGraphComment() {
        if (!workingCopy_.has_value() || selectedCommentId_ == 0U) {
            return false;
        }
        kb::render::RenderMaterialAssetData document = *workingCopy_;
        const auto oldEnd = std::remove_if(document.graph.comments.begin(), document.graph.comments.end(), [commentId = selectedCommentId_](const kb::render::RenderMaterialGraphCommentBox& comment) {
            return comment.id == commentId;
        });
        if (oldEnd == document.graph.comments.end()) {
            return false;
        }
        document.graph.comments.erase(oldEnd, document.graph.comments.end());
        SetWorkingCopy(std::move(document));
        ClearCommentSelection();
        return true;
    }

    [[nodiscard]] bool AddGraphCompositeSubgraph(
        std::string name,
        std::int32_t positionX,
        std::int32_t positionY,
        std::int32_t width = 420,
        std::int32_t height = 260,
        std::vector<std::uint32_t> nodeIds = {},
        bool collapsed = false,
        std::uint32_t color = 0x425B4AU,
        std::uint32_t* createdCompositeId = nullptr) {
        if (!workingCopy_.has_value()) {
            return false;
        }

        kb::render::RenderMaterialAssetData document = *workingCopy_;
        EnsureEditableGraph(document.graph);
        std::vector<std::uint32_t> validNodeIds;
        validNodeIds.reserve(nodeIds.size());
        for (const std::uint32_t nodeId : nodeIds) {
            if (kb::render::FindRenderMaterialGraphNode(document.graph, nodeId) != nullptr &&
                std::ranges::find(validNodeIds, nodeId) == validNodeIds.end()) {
                validNodeIds.push_back(nodeId);
            }
        }
        const std::uint32_t compositeId = NextGraphCompositeId(document.graph);
        kb::render::RenderMaterialGraphCompositeSubgraph composite{
            .id = compositeId,
            .positionX = positionX,
            .positionY = positionY,
            .width = std::max<std::int32_t>(64, width),
            .height = std::max<std::int32_t>(64, height),
            .color = color,
            .collapsed = collapsed,
            .name = name.empty() ? std::string{ "Composite" } : std::move(name),
            .nodeIds = std::move(validNodeIds),
        };
        document.graph.composites.push_back(std::move(composite));
        if (createdCompositeId != nullptr) {
            *createdCompositeId = compositeId;
        }
        SetWorkingCopy(std::move(document));
        return true;
    }

    [[nodiscard]] bool CreateGraphCompositeFromSelection(
        std::string name,
        std::int32_t positionX,
        std::int32_t positionY,
        std::int32_t width = 420,
        std::int32_t height = 260,
        std::uint32_t* createdCompositeId = nullptr) {
        return AddGraphCompositeSubgraph(
            std::move(name),
            positionX,
            positionY,
            width,
            height,
            selectedNodeIds_,
            false,
            0x425B4AU,
            createdCompositeId);
    }

    [[nodiscard]] std::optional<kb::render::RenderMaterialGraphCompositeSubgraph> GraphCompositeSubgraph(std::uint32_t compositeId) const {
        if (!workingCopy_.has_value()) {
            return std::nullopt;
        }
        const kb::render::RenderMaterialGraphCompositeSubgraph* composite = FindGraphComposite(workingCopy_->graph, compositeId);
        if (composite == nullptr) {
            return std::nullopt;
        }
        return *composite;
    }

    [[nodiscard]] bool SetGraphCompositeCollapsed(std::uint32_t compositeId, bool collapsed) {
        if (!workingCopy_.has_value() || compositeId == 0U) {
            return false;
        }
        kb::render::RenderMaterialGraphCompositeSubgraph* composite = FindMutableGraphComposite(workingCopy_->graph, compositeId);
        if (composite == nullptr || composite->collapsed == collapsed) {
            return false;
        }
        composite->collapsed = collapsed;
        ++documentRevision_;
        return true;
    }

    [[nodiscard]] bool ToggleGraphCompositeCollapsed(std::uint32_t compositeId) {
        if (!workingCopy_.has_value() || compositeId == 0U) {
            return false;
        }
        kb::render::RenderMaterialGraphCompositeSubgraph* composite = FindMutableGraphComposite(workingCopy_->graph, compositeId);
        if (composite == nullptr) {
            return false;
        }
        composite->collapsed = !composite->collapsed;
        ++documentRevision_;
        return true;
    }

    [[nodiscard]] bool CopySelectedGraphNodes() {
        if (!workingCopy_.has_value() || selectedNodeIds_.empty()) {
            return false;
        }
        std::optional<GraphClipboard> clipboard = BuildGraphClipboard(*workingCopy_, selectedNodeIds_);
        if (!clipboard.has_value()) {
            return false;
        }
        graphClipboard_ = std::move(clipboard);
        return true;
    }

    [[nodiscard]] bool PasteGraphClipboard(std::int32_t offsetX, std::int32_t offsetY, std::vector<std::uint32_t>* pastedNodeIds = nullptr) {
        if (!workingCopy_.has_value() || !HasGraphClipboard()) {
            return false;
        }
        kb::render::RenderMaterialAssetData document = *workingCopy_;
        EnsureEditableGraph(document.graph);

        std::vector<std::pair<std::uint32_t, std::uint32_t>> remap;
        std::vector<std::uint32_t> pastedIds;
        std::uint32_t nextNodeId = NextGraphNodeId(document.graph);
        for (const kb::render::RenderMaterialGraphNode& sourceNode : graphClipboard_->nodes) {
            kb::render::RenderMaterialGraphNode pasted = sourceNode;
            const std::uint32_t oldId = pasted.id;
            pasted.id = nextNodeId++;
            pasted.positionX += offsetX;
            pasted.positionY += offsetY;
            remap.push_back({ oldId, pasted.id });
            pastedIds.push_back(pasted.id);
            document.graph.nodes.push_back(std::move(pasted));
        }

        for (const kb::render::RenderMaterialGraphLink& sourceLink : graphClipboard_->links) {
            const std::uint32_t fromNodeId = RemapGraphNodeId(remap, sourceLink.fromNodeId);
            const std::uint32_t toNodeId = RemapGraphNodeId(remap, sourceLink.toNodeId);
            if (fromNodeId == 0U || toNodeId == 0U) {
                continue;
            }
            kb::render::RenderMaterialGraphLink pastedLink = sourceLink;
            pastedLink.fromNodeId = fromNodeId;
            pastedLink.toNodeId = toNodeId;
            pastedLink.id = kb::render::MakeRenderMaterialGraphLinkId(pastedLink);
            document.graph.links.push_back(std::move(pastedLink));
        }
        for (const kb::render::RenderMaterialGraphParameterValue& value : graphClipboard_->parameterValues) {
            const auto existing = std::ranges::find_if(document.graphParameterValues, [&value](const kb::render::RenderMaterialGraphParameterValue& candidate) {
                return candidate.stableId == value.stableId && candidate.type == value.type;
            });
            if (existing == document.graphParameterValues.end()) {
                document.graphParameterValues.push_back(value);
            }
        }

        SetWorkingCopy(std::move(document));
        SetNodeSelection(pastedIds, pastedIds.empty() ? 0U : pastedIds.back());
        if (pastedNodeIds != nullptr) {
            *pastedNodeIds = std::move(pastedIds);
        }
        return true;
    }

    [[nodiscard]] bool DuplicateSelectedGraphNodes(std::int32_t offsetX, std::int32_t offsetY, std::vector<std::uint32_t>* pastedNodeIds = nullptr) {
        if (!CopySelectedGraphNodes()) {
            return false;
        }
        return PasteGraphClipboard(offsetX, offsetY, pastedNodeIds);
    }

    void Open(
        kb::assets::AssetId assetId,
        std::optional<kb::render::RenderMaterialAssetData> document,
        std::optional<kb::render::RenderMaterialTypeSchema> schema = std::nullopt,
        std::optional<kb::render::RenderMaterialInstanceAssetData> instanceDocument = std::nullopt) {
        ++documentRevision_;
        openAssetId_ = assetId;
        instanceParentSnapshot_ = document;
        instanceWorkingCopy_ = std::move(instanceDocument);
        instanceCleanSnapshot_ = instanceWorkingCopy_;
        if (document.has_value() && instanceWorkingCopy_.has_value()) {
            workingCopy_ = kb::render::BuildEffectiveRenderMaterialInstanceAsset(*document, *instanceWorkingCopy_);
            cleanSnapshot_ = workingCopy_;
            InvalidateCleanCanonical();
        } else {
            workingCopy_ = document;
            cleanSnapshot_ = std::move(document);
            InvalidateCleanCanonical();
            instanceParentSnapshot_.reset();
        }
        activeSchema_ = schema.has_value() && !schema->typeName.empty()
            ? std::move(*schema)
            : kb::render::GetBuiltInPbrMaterialTypeSchema();
        dirty_ = false;
        externalDiagnostics_.clear();
        externalDiagnosticsHaveError_ = false;
        ResetCookDiagnostics();
        selectedNodeId_ = 0U;
        selectedNodeIds_.clear();
        selectedCommentId_ = 0U;
        selectedParameter_ = InspectorPropertyId::None;
        infoPanelVisible_ = false;
        findFocused_ = false;
        instanceOverrideGroupExpanded_ = { true, true, true, true };
        findQuery_.clear();
        findResults_.clear();
        CloseGraphNodeEnumDropdown();
        CancelGraphNodeRenameEdit();
        // Every per-node text edit is scoped to the document it was started in: a node id from the previous
        // material would address a different node here and commit the stale buffer into it.
        CancelGraphConstantInlineEdit();
        InvalidateParameters();
        InvalidateGraphDiagnostics();
    }

    void Close() noexcept {
        ++documentRevision_;
        openAssetId_ = {};
        workingCopy_.reset();
        cleanSnapshot_.reset();
        InvalidateCleanCanonical();
        instanceWorkingCopy_.reset();
        instanceCleanSnapshot_.reset();
        instanceParentSnapshot_.reset();
        parameters_.clear();
        activeSchema_ = kb::render::GetBuiltInPbrMaterialTypeSchema();
        dirty_ = false;
        selectedNodeId_ = 0U;
        selectedNodeIds_.clear();
        selectedCommentId_ = 0U;
        selectedParameter_ = InspectorPropertyId::None;
        infoPanelVisible_ = false;
        findFocused_ = false;
        instanceOverrideGroupExpanded_ = { true, true, true, true };
        findQuery_.clear();
        findResults_.clear();
        CloseGraphNodeEnumDropdown();
        CancelGraphNodeRenameEdit();
        CancelGraphConstantInlineEdit();
        diagnostics_.clear();
        graphDiagnosticsLines_.clear();
        compilerDiagnostics_.clear();
        externalDiagnostics_.clear();
        cookDiagnostics_.clear();
        graphDiagnosticMarkers_.clear();
        diagnosticsHaveError_ = false;
        graphDiagnosticsHaveError_ = false;
        compilerDiagnosticsHaveError_ = false;
        externalDiagnosticsHaveError_ = false;
        ResetCookDiagnostics();
        materialStats_ = {};
        shaderViewer_ = {};
    }

    // Recomputes the dirty flag from the documents alone.
    //
    // Contract for the in-place mutators (MoveGraphNode/MoveGraphNodes/MoveGraphComment/MoveGraphCommentGroup
    // and the composite collapse): they write straight into workingCopy_ and deliberately do NOT recompute
    // dirty, refresh parameters, diagnostics or find results - a canonical compare of the whole document per
    // mouse-move during a drag is exactly the cost they exist to avoid. Anything that calls them owes the
    // state either a recorded edit (which routes through SetWorkingCopy) or a call to this.
    void RefreshDirty() {
        if (instanceWorkingCopy_.has_value() || instanceCleanSnapshot_.has_value()) {
            dirty_ = !EquivalentInstance(instanceWorkingCopy_, instanceCleanSnapshot_) ||
                !WorkingCopyMatchesCleanSnapshot();
            return;
        }
        dirty_ = !WorkingCopyMatchesCleanSnapshot();
    }

    void SetWorkingCopy(kb::render::RenderMaterialAssetData document) {
        workingCopy_ = std::move(document);
        ++documentRevision_;
        dirty_ = !WorkingCopyMatchesCleanSnapshot();
        InvalidateParameters();
        InvalidateGraphDiagnostics();
        InvalidateFindResults();
        PruneSelectionToWorkingCopy();
    }

    void SetInstanceWorkingCopy(
        kb::render::RenderMaterialInstanceAssetData instanceDocument,
        kb::render::RenderMaterialAssetData effectiveDocument) {
        instanceWorkingCopy_ = std::move(instanceDocument);
        workingCopy_ = std::move(effectiveDocument);
        ++documentRevision_;
        dirty_ = !EquivalentInstance(instanceWorkingCopy_, instanceCleanSnapshot_) ||
            !WorkingCopyMatchesCleanSnapshot();
        InvalidateParameters();
        InvalidateGraphDiagnostics();
        InvalidateFindResults();
        PruneSelectionToWorkingCopy();
    }

    void MarkSaved() {
        cleanSnapshot_ = workingCopy_;
        InvalidateCleanCanonical();
        if (instanceWorkingCopy_.has_value()) {
            instanceCleanSnapshot_ = instanceWorkingCopy_;
        }
        dirty_ = false;
    }

    void RevertToCleanSnapshot() {
        ++documentRevision_;
        if (instanceWorkingCopy_.has_value() || instanceCleanSnapshot_.has_value()) {
            instanceWorkingCopy_ = instanceCleanSnapshot_;
            if (instanceParentSnapshot_.has_value() && instanceWorkingCopy_.has_value()) {
                workingCopy_ = kb::render::BuildEffectiveRenderMaterialInstanceAsset(*instanceParentSnapshot_, *instanceWorkingCopy_);
                cleanSnapshot_ = workingCopy_;
                InvalidateCleanCanonical();
            } else {
                workingCopy_ = cleanSnapshot_;
            }
        } else {
            workingCopy_ = cleanSnapshot_;
        }
        dirty_ = false;
        InvalidateParameters();
        InvalidateGraphDiagnostics();
        // The find panel lists nodes and comments by id; a revert can delete the very things it is listing,
        // so it needs the same invalidation SetWorkingCopy does. Focusing a stale hit re-validates and
        // quietly does nothing, which reads as a broken panel.
        InvalidateFindResults();
        PruneSelectionToWorkingCopy();
    }

    bool SelectNode(std::uint32_t nodeId) {
        // Same validation SetNodeSelection does. Without it a hit-test against the fabricated default graph
        // an empty document is drawn with can put a node id that does not exist into the selection, where it
        // sits until the next SetWorkingCopy prunes it.
        if (nodeId != 0U && workingCopy_.has_value() &&
            kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, nodeId) == nullptr) {
            nodeId = 0U;
        }
        std::vector<std::uint32_t> nextSelection;
        if (nodeId != 0U) {
            nextSelection.push_back(nodeId);
        }
        if (selectedNodeId_ == nodeId && selectedNodeIds_ == nextSelection) {
            return false;
        }
        if (inlineConstantEditNodeId_ != 0U && inlineConstantEditNodeId_ != nodeId) {
            CancelGraphConstantInlineEdit();
        }
        if (renameNodeId_ != 0U && renameNodeId_ != nodeId) {
            CancelGraphNodeRenameEdit();
        }
        if (graphNodeEnumDropdownNodeId_ != 0U && graphNodeEnumDropdownNodeId_ != nodeId) {
            CloseGraphNodeEnumDropdown();
        }
        selectedNodeId_ = nodeId;
        selectedNodeIds_ = std::move(nextSelection);
        selectedCommentId_ = 0U;
        selectedParameter_ = InspectorPropertyId::None;
        return true;
    }

    bool ClearNodeSelection() {
        return SelectNode(0U);
    }

    bool AddNodeToSelection(std::uint32_t nodeId) {
        if (nodeId == 0U || IsNodeSelected(nodeId)) {
            return false;
        }
        if (inlineConstantEditNodeId_ != 0U && inlineConstantEditNodeId_ != nodeId) {
            CancelGraphConstantInlineEdit();
        }
        if (renameNodeId_ != 0U && renameNodeId_ != nodeId) {
            CancelGraphNodeRenameEdit();
        }
        if (graphNodeEnumDropdownNodeId_ != 0U && graphNodeEnumDropdownNodeId_ != nodeId) {
            CloseGraphNodeEnumDropdown();
        }
        selectedNodeIds_.push_back(nodeId);
        selectedNodeId_ = nodeId;
        selectedCommentId_ = 0U;
        selectedParameter_ = InspectorPropertyId::None;
        return true;
    }

    bool ToggleNodeSelection(std::uint32_t nodeId) {
        if (nodeId == 0U) {
            return false;
        }
        const auto found = std::ranges::find(selectedNodeIds_, nodeId);
        if (found == selectedNodeIds_.end()) {
            return AddNodeToSelection(nodeId);
        }
        selectedNodeIds_.erase(found);
        selectedNodeId_ = selectedNodeIds_.empty() ? 0U : selectedNodeIds_.back();
        if (inlineConstantEditNodeId_ == nodeId) {
            CancelGraphConstantInlineEdit();
        }
        if (renameNodeId_ == nodeId) {
            CancelGraphNodeRenameEdit();
        }
        if (graphNodeEnumDropdownNodeId_ == nodeId) {
            CloseGraphNodeEnumDropdown();
        }
        selectedParameter_ = InspectorPropertyId::None;
        return true;
    }

    bool SetNodeSelection(std::vector<std::uint32_t> nodeIds, std::uint32_t primaryNodeId = 0U) {
        std::vector<std::uint32_t> sanitized;
        sanitized.reserve(nodeIds.size());
        for (std::uint32_t nodeId : nodeIds) {
            if (nodeId == 0U || std::ranges::find(sanitized, nodeId) != sanitized.end()) {
                continue;
            }
            if (workingCopy_.has_value() && kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, nodeId) == nullptr) {
                continue;
            }
            sanitized.push_back(nodeId);
        }
        if (primaryNodeId == 0U || std::ranges::find(sanitized, primaryNodeId) == sanitized.end()) {
            primaryNodeId = sanitized.empty() ? 0U : sanitized.back();
        }
        if (selectedNodeId_ == primaryNodeId && selectedNodeIds_ == sanitized) {
            return false;
        }
        if (inlineConstantEditNodeId_ != 0U && std::ranges::find(sanitized, inlineConstantEditNodeId_) == sanitized.end()) {
            CancelGraphConstantInlineEdit();
        }
        if (renameNodeId_ != 0U && std::ranges::find(sanitized, renameNodeId_) == sanitized.end()) {
            CancelGraphNodeRenameEdit();
        }
        if (graphNodeEnumDropdownNodeId_ != 0U && std::ranges::find(sanitized, graphNodeEnumDropdownNodeId_) == sanitized.end()) {
            CloseGraphNodeEnumDropdown();
        }
        selectedNodeIds_ = std::move(sanitized);
        selectedNodeId_ = primaryNodeId;
        selectedCommentId_ = 0U;
        selectedParameter_ = InspectorPropertyId::None;
        return true;
    }

    bool SelectComment(std::uint32_t commentId) {
        if (commentId != 0U && (!workingCopy_.has_value() || FindGraphComment(workingCopy_->graph, commentId) == nullptr)) {
            return false;
        }
        if (selectedCommentId_ == commentId && selectedNodeIds_.empty() && selectedNodeId_ == 0U) {
            return false;
        }
        if (inlineConstantEditNodeId_ != 0U) {
            CancelGraphConstantInlineEdit();
        }
        if (renameNodeId_ != 0U) {
            CancelGraphNodeRenameEdit();
        }
        CloseGraphNodeEnumDropdown();
        selectedNodeId_ = 0U;
        selectedNodeIds_.clear();
        selectedCommentId_ = commentId;
        selectedParameter_ = InspectorPropertyId::None;
        return true;
    }

    bool ClearCommentSelection() {
        if (selectedCommentId_ == 0U) {
            return false;
        }
        selectedCommentId_ = 0U;
        selectedParameter_ = InspectorPropertyId::None;
        return true;
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
        if (!infoPanelVisible_) {
            findFocused_ = false;
        }
        return true;
    }

    void SetInfoPanelVisible(bool visible) noexcept {
        infoPanelVisible_ = visible;
        if (!infoPanelVisible_) {
            findFocused_ = false;
        }
    }

    void FocusFind(bool focused) noexcept {
        findFocused_ = focused;
        if (findFocused_) {
            infoPanelVisible_ = true;
        }
    }

    void SetDiagnostics(std::vector<std::string> diagnostics, bool hasError) {
        EnsureGraphDiagnostics();
        externalDiagnostics_.clear();
        externalDiagnostics_.reserve(diagnostics.size());
        for (std::string& diagnostic : diagnostics) {
            externalDiagnostics_.push_back("[external] " + std::move(diagnostic));
        }
        externalDiagnosticsHaveError_ = hasError;
        RebuildMergedDiagnostics();
    }

    void ApplyCookResult(
        std::vector<std::string> diagnostics,
        bool cookSucceeded,
        bool hasGpuProgram,
        bool hasLastGood,
        bool fallbackApplied) {
        EnsureGraphDiagnostics();
        cookDiagnostics_.clear();
        cookDiagnostics_.reserve(diagnostics.size());
        for (std::string& diagnostic : diagnostics) {
            cookDiagnostics_.push_back("[cook] " + std::move(diagnostic));
        }
        cookCompleted_ = true;
        cookSucceeded_ = cookSucceeded;
        cookHasGpuProgram_ = hasGpuProgram;
        cookHasLastGood_ = hasLastGood;
        cookFallbackApplied_ = fallbackApplied;
        RefreshGraphRuntimeState();
        RebuildMergedDiagnostics();
    }

    void ApplyCookMaterialStats(
        std::uint64_t sourceHash,
        std::string backendName,
        std::uint32_t textureBindingCount,
        std::uint32_t uniformCount,
        std::uint32_t varyingCount,
        std::vector<MaterialEditorCookPassTelemetry> passes) {
        EnsureGraphDiagnostics();
        MaterialEditorMaterialStatsModel model{};
        model.sourceHash = sourceHash;
        model.available = std::ranges::any_of(passes, [](const MaterialEditorCookPassTelemetry& pass) {
            return pass.succeeded;
        });
        for (MaterialEditorCookPassTelemetry& pass : passes) {
            MaterialEditorMaterialStatsPassRow row{
                .passName = std::move(pass.passName),
                .graphProgram = pass.succeeded,
                .cacheHit = pass.cacheHit,
                .instructionCountAvailable = false,
                .instructionCount = 0U,
                .samplerCount = pass.succeeded ? textureBindingCount : 0U,
                .uniformCount = pass.succeeded ? uniformCount : 0U,
                .varyingCount = pass.succeeded ? varyingCount : 0U,
                .staticVariantCount = pass.succeeded ? 1U : 0U,
                .binaryByteSize = pass.succeeded ? pass.binaryByteSize : 0U,
                .backendName = backendName,
            };
            if (!pass.succeeded) {
                row.warnings.push_back("GPU program unavailable for this cooked pass.");
            }
            model.passRows.push_back(std::move(row));
        }
        if (model.available) {
            model.warnings.push_back(
                "GPU instruction count unavailable for backend '" + backendName +
                "'; no source-text estimate is reported.");
        } else {
            model.warnings.push_back("Material stats unavailable: no cooked GPU pass succeeded.");
        }
        materialStats_ = std::move(model);
    }

    void ClearDiagnostics() {
        externalDiagnostics_.clear();
        externalDiagnosticsHaveError_ = false;
        RefreshGraphDiagnostics();
    }

private:
    [[nodiscard]] static std::optional<kb::render::RenderMaterialGraphNodeKind> PromotedParameterKind(
        kb::render::RenderMaterialGraphNodeKind sourceKind) noexcept {
        switch (sourceKind) {
        case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
            return kb::render::RenderMaterialGraphNodeKind::ParameterScalar;
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
            return kb::render::RenderMaterialGraphNodeKind::ParameterVector;
        case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
            return kb::render::RenderMaterialGraphNodeKind::ParameterColor;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] static std::string PromotedSourceOutputPin(kb::render::RenderMaterialGraphNodeKind sourceKind) {
        switch (sourceKind) {
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
            return "xy";
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
            return "xyz";
        case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
            return "rgba";
        case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
        default:
            return "value";
        }
    }

    [[nodiscard]] static std::string PromotedTargetOutputPin(kb::render::RenderMaterialGraphNodeKind targetKind) {
        switch (targetKind) {
        case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
            return "xyz";
        case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
            return "rgba";
        case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
        default:
            return "value";
        }
    }

    [[nodiscard]] static std::string PromotedParameterDefaultValueHint(
        const kb::render::RenderMaterialGraphNode& sourceNode,
        kb::render::RenderMaterialGraphNodeKind targetKind) {
        std::array<float, 4U> values{};
        if (const std::optional<std::array<float, 4U>> parsed = ParseConstantValue(sourceNode.kind, sourceNode.parameter.defaultValueHint)) {
            values = *parsed;
        }
        switch (targetKind) {
        case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
            return ConstantDefaultValueHint(kb::render::RenderMaterialGraphNodeKind::ConstantScalar, values);
        case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
            return ConstantDefaultValueHint(kb::render::RenderMaterialGraphNodeKind::ConstantVector, values);
        case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
            return ConstantDefaultValueHint(kb::render::RenderMaterialGraphNodeKind::ConstantColor, values);
        default:
            return sourceNode.parameter.defaultValueHint;
        }
    }

    [[nodiscard]] bool SelectGraphLinkedNodes(bool downstream) {
        if (!workingCopy_.has_value() || selectedNodeIds_.empty()) {
            return false;
        }

        std::vector<std::uint32_t> reached = selectedNodeIds_;
        bool expanded = true;
        while (expanded) {
            expanded = false;
            for (const kb::render::RenderMaterialGraphLink& link : workingCopy_->graph.links) {
                const std::uint32_t sourceNodeId = downstream ? link.fromNodeId : link.toNodeId;
                const std::uint32_t linkedNodeId = downstream ? link.toNodeId : link.fromNodeId;
                if (sourceNodeId == 0U || linkedNodeId == 0U) {
                    continue;
                }
                if (std::ranges::find(reached, sourceNodeId) != reached.end() &&
                    std::ranges::find(reached, linkedNodeId) == reached.end()) {
                    reached.push_back(linkedNodeId);
                    expanded = true;
                }
            }
        }

        std::vector<std::uint32_t> ordered;
        ordered.reserve(reached.size());
        for (const kb::render::RenderMaterialGraphNode& node : workingCopy_->graph.nodes) {
            if (std::ranges::find(reached, node.id) != reached.end()) {
                ordered.push_back(node.id);
            }
        }
        const std::uint32_t primaryNodeId =
            std::ranges::find(ordered, selectedNodeId_) != ordered.end()
                ? selectedNodeId_
                : (ordered.empty() ? 0U : ordered.back());
        return SetNodeSelection(std::move(ordered), primaryNodeId);
    }

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

    [[nodiscard]] static std::string TrimAscii(std::string_view text) {
        const std::size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return {};
        }
        const std::size_t last = text.find_last_not_of(" \t\r\n");
        return std::string{ text.substr(first, last - first + 1U) };
    }

    [[nodiscard]] static bool ParseDecimalAssetId(std::string_view text, std::uint64_t& output) noexcept {
        if (text.empty()) {
            return false;
        }
        std::uint64_t value = 0U;
        for (const char ch : text) {
            if (ch < '0' || ch > '9') {
                return false;
            }
            const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
            if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10ULL) {
                return false;
            }
            value = value * 10ULL + digit;
        }
        output = value;
        return true;
    }

    [[nodiscard]] static bool IsStableGraphIdentifier(std::string_view text) noexcept {
        if (text.empty() || text.size() > 64U) {
            return false;
        }
        const unsigned char first = static_cast<unsigned char>(text.front());
        if (!(std::isalpha(first) || first == '_')) {
            return false;
        }
        for (const unsigned char ch : text) {
            if (!(std::isalnum(ch) || ch == '_')) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static std::vector<float> ParseDefaultNumbers(std::string_view text) {
        std::vector<float> values;
        if (text.empty() || text == "_") {
            return values;
        }
        static_cast<void>(kb::render::ParseFiniteMaterialFloatSequence(text, values, 1U, 64U));
        return values;
    }

    [[nodiscard]] static bool IsGraphConstantNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
        return kind == kb::render::RenderMaterialGraphNodeKind::ConstantScalar ||
            kind == kb::render::RenderMaterialGraphNodeKind::ConstantBool ||
            kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector2 ||
            kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector ||
            kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor;
    }

    [[nodiscard]] static std::string_view ConstantDisplayName(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
        switch (kind) {
        case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
            return "Scalar";
        case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
            return "Bool";
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
            return "XY";
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
            return "RGB Node";
        case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
            return "RGBA Node";
        default:
            return "Constant";
        }
    }

    [[nodiscard]] static std::size_t ConstantComponentCount(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
        switch (kind) {
        case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
        case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
            return 1U;
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
            return 2U;
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
            return 3U;
        case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
            return 4U;
        default:
            return 0U;
        }
    }

    [[nodiscard]] static std::optional<std::array<float, 4U>> ParseConstantValue(
        kb::render::RenderMaterialGraphNodeKind kind,
        std::string_view text) {
        std::array<float, 4U> value{ 0.0F, 0.0F, 0.0F, 1.0F };
        std::vector<float> numericValues;
        switch (kind) {
        case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
            if (kb::render::ParseFiniteMaterialFloatSequence(text, numericValues, 1U, 1U)) {
                value[0] = numericValues[0];
                return value;
            }
            return std::nullopt;
        case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
            if (const std::optional<bool> parsed = ParseConstantBool(text)) {
                value[0] = *parsed ? 1.0F : 0.0F;
                return value;
            }
            return std::nullopt;
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
            if (kb::render::ParseFiniteMaterialFloatSequence(text, numericValues, 2U, 2U)) {
                std::copy(numericValues.begin(), numericValues.end(), value.begin());
                return value;
            }
            return std::nullopt;
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
            if (kb::render::ParseFiniteMaterialFloatSequence(text, numericValues, 3U, 3U)) {
                std::copy(numericValues.begin(), numericValues.end(), value.begin());
                return value;
            }
            return std::nullopt;
        case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
            if (kb::render::ParseFiniteMaterialFloatSequence(text, numericValues, 3U, 4U)) {
                std::copy(numericValues.begin(), numericValues.end(), value.begin());
                value[3] = numericValues.size() == 4U ? numericValues[3] : 1.0F;
                return value;
            }
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] static std::optional<bool> ParseConstantBool(std::string_view text) noexcept {
        std::string normalized;
        normalized.reserve(text.size());
        for (const unsigned char ch : text) {
            if (!std::isspace(ch)) {
                normalized.push_back(static_cast<char>(std::tolower(ch)));
            }
        }
        if (normalized == "true" || normalized == "1") {
            return true;
        }
        if (normalized == "false" || normalized == "0") {
            return false;
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::string ConstantBoolDefaultValueHint(bool value) {
        return value ? "true" : "false";
    }

    [[nodiscard]] static std::string FloatText(float value) {
        std::ostringstream output;
        output << value;
        return output.str();
    }

    [[nodiscard]] static std::span<const GraphHintNumericPropertyDefinition> GraphHintNumericProperties(
        kb::render::RenderMaterialGraphNodeKind kind) noexcept {
        static constexpr std::array<GraphHintNumericPropertyDefinition, 2U> panner{
            GraphHintNumericPropertyDefinition{
                .stableId = "panner.speedU",
                .displayName = "Speed U",
                .defaultValue = 0.1F,
                .rangeMin = -16.0F,
                .rangeMax = 16.0F,
            },
            GraphHintNumericPropertyDefinition{
                .stableId = "panner.speedV",
                .displayName = "Speed V",
                .defaultValue = 0.0F,
                .rangeMin = -16.0F,
                .rangeMax = 16.0F,
            },
        };
        static constexpr std::array<GraphHintNumericPropertyDefinition, 3U> rotator{
            GraphHintNumericPropertyDefinition{
                .stableId = "rotator.speed",
                .displayName = "Speed",
                .defaultValue = 1.0F,
                .rangeMin = -16.0F,
                .rangeMax = 16.0F,
            },
            GraphHintNumericPropertyDefinition{
                .stableId = "rotator.centerU",
                .displayName = "Center U",
                .defaultValue = 0.5F,
                .rangeMin = -16.0F,
                .rangeMax = 16.0F,
            },
            GraphHintNumericPropertyDefinition{
                .stableId = "rotator.centerV",
                .displayName = "Center V",
                .defaultValue = 0.5F,
                .rangeMin = -16.0F,
                .rangeMax = 16.0F,
            },
        };
        static constexpr std::array<GraphHintNumericPropertyDefinition, 1U> bumpOffset{
            GraphHintNumericPropertyDefinition{
                .stableId = "bumpOffset.heightRatio",
                .displayName = "Height Ratio",
                .defaultValue = 0.05F,
                .rangeMin = -1.0F,
                .rangeMax = 1.0F,
            },
        };
        static constexpr std::array<GraphHintNumericPropertyDefinition, 2U> constantBiasScale{
            GraphHintNumericPropertyDefinition{
                .stableId = "constantBiasScale.bias",
                .displayName = "Bias",
                .defaultValue = 0.0F,
                .rangeMin = -16.0F,
                .rangeMax = 16.0F,
            },
            GraphHintNumericPropertyDefinition{
                .stableId = "constantBiasScale.scale",
                .displayName = "Scale",
                .defaultValue = 1.0F,
                .rangeMin = -16.0F,
                .rangeMax = 16.0F,
            },
        };
        static constexpr std::array<GraphHintNumericPropertyDefinition, 4U> rotateAboutAxis{
            GraphHintNumericPropertyDefinition{
                .stableId = "rotateAboutAxis.axisX",
                .displayName = "Axis X",
                .defaultValue = 0.0F,
                .rangeMin = -1.0F,
                .rangeMax = 1.0F,
            },
            GraphHintNumericPropertyDefinition{
                .stableId = "rotateAboutAxis.axisY",
                .displayName = "Axis Y",
                .defaultValue = 0.0F,
                .rangeMin = -1.0F,
                .rangeMax = 1.0F,
            },
            GraphHintNumericPropertyDefinition{
                .stableId = "rotateAboutAxis.axisZ",
                .displayName = "Axis Z",
                .defaultValue = 1.0F,
                .rangeMin = -1.0F,
                .rangeMax = 1.0F,
            },
            GraphHintNumericPropertyDefinition{
                .stableId = "rotateAboutAxis.angle",
                .displayName = "Angle",
                .defaultValue = 0.0F,
                .rangeMin = -6.283185F,
                .rangeMax = 6.283185F,
            },
        };
        static constexpr std::array<GraphHintNumericPropertyDefinition, 1U> desaturate{
            GraphHintNumericPropertyDefinition{
                .stableId = "desaturate.fraction",
                .displayName = "Fraction",
                .defaultValue = 1.0F,
                .rangeMin = 0.0F,
                .rangeMax = 1.0F,
            },
        };
        static constexpr std::array<GraphHintNumericPropertyDefinition, 2U> fresnel{
            GraphHintNumericPropertyDefinition{
                .stableId = "fresnel.exponent",
                .displayName = "Exponent",
                .defaultValue = 5.0F,
                .rangeMin = 0.0001F,
                .rangeMax = 16.0F,
            },
            GraphHintNumericPropertyDefinition{
                .stableId = "fresnel.base",
                .displayName = "Base",
                .defaultValue = 0.0F,
                .rangeMin = 0.0F,
                .rangeMax = 1.0F,
            },
        };
        static constexpr std::array<GraphHintNumericPropertyDefinition, 2U> sphereMask{
            GraphHintNumericPropertyDefinition{
                .stableId = "sphereMask.radius",
                .displayName = "Radius",
                .defaultValue = 1.0F,
                .rangeMin = 0.0001F,
                .rangeMax = 1024.0F,
            },
            GraphHintNumericPropertyDefinition{
                .stableId = "sphereMask.hardness",
                .displayName = "Hardness",
                .defaultValue = 0.5F,
                .rangeMin = 0.0F,
                .rangeMax = 0.999F,
            },
        };
        static constexpr std::array<GraphHintNumericPropertyDefinition, 1U> antialiasedTextureMask{
            GraphHintNumericPropertyDefinition{
                .stableId = "antialiasedTextureMask.threshold",
                .displayName = "Threshold",
                .defaultValue = 0.5F,
                .rangeMin = 0.0F,
                .rangeMax = 1.0F,
            },
        };
        static constexpr std::array<GraphHintNumericPropertyDefinition, 2U> cameraDepthFade{
            GraphHintNumericPropertyDefinition{
                .stableId = "cameraDepthFade.fadeLength",
                .displayName = "Fade Length",
                .defaultValue = 1.0F,
                .rangeMin = 0.0001F,
                .rangeMax = 100000.0F,
            },
            GraphHintNumericPropertyDefinition{
                .stableId = "cameraDepthFade.fadeOffset",
                .displayName = "Fade Offset",
                .defaultValue = 0.0F,
                .rangeMin = -100000.0F,
                .rangeMax = 100000.0F,
            },
        };
        static constexpr std::array<GraphHintNumericPropertyDefinition, 1U> depthFade{
            GraphHintNumericPropertyDefinition{
                .stableId = "depthFade.fadeDistance",
                .displayName = "Fade Distance",
                .defaultValue = 0.01F,
                .rangeMin = 0.0001F,
                .rangeMax = 100000.0F,
            },
        };

        switch (kind) {
        case kb::render::RenderMaterialGraphNodeKind::Panner:
            return panner;
        case kb::render::RenderMaterialGraphNodeKind::Rotator:
            return rotator;
        case kb::render::RenderMaterialGraphNodeKind::BumpOffset:
            return bumpOffset;
        case kb::render::RenderMaterialGraphNodeKind::ConstantBiasScale:
            return constantBiasScale;
        case kb::render::RenderMaterialGraphNodeKind::RotateAboutAxis:
            return rotateAboutAxis;
        case kb::render::RenderMaterialGraphNodeKind::Desaturate:
            return desaturate;
        case kb::render::RenderMaterialGraphNodeKind::Fresnel:
            return fresnel;
        case kb::render::RenderMaterialGraphNodeKind::SphereMask:
            return sphereMask;
        case kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask:
            return antialiasedTextureMask;
        case kb::render::RenderMaterialGraphNodeKind::CameraDepthFade:
            return cameraDepthFade;
        case kb::render::RenderMaterialGraphNodeKind::DepthFade:
            return depthFade;
        default:
            return {};
        }
    }

    [[nodiscard]] static std::array<float, 4U> GraphHintNumericValues(const kb::render::RenderMaterialGraphNode& node) {
        std::array<float, 4U> values{};
        const std::span<const GraphHintNumericPropertyDefinition> definitions = GraphHintNumericProperties(node.kind);
        for (std::size_t index = 0U; index < definitions.size(); ++index) {
            values[index] = definitions[index].defaultValue;
        }
        const std::vector<float> parsed = ParseDefaultNumbers(node.parameter.defaultValueHint);
        for (std::size_t index = 0U; index < definitions.size() && index < parsed.size(); ++index) {
            values[index] = parsed[index];
        }
        return values;
    }

    [[nodiscard]] static std::string GraphHintNumericValueHint(
        kb::render::RenderMaterialGraphNodeKind kind,
        const std::array<float, 4U>& values) {
        const std::span<const GraphHintNumericPropertyDefinition> definitions = GraphHintNumericProperties(kind);
        std::string hint;
        for (std::size_t index = 0U; index < definitions.size(); ++index) {
            if (!hint.empty()) {
                hint += ' ';
            }
            hint += FloatText(values[index]);
        }
        return hint;
    }

    [[nodiscard]] static std::string ConstantDefaultValueHint(
        kb::render::RenderMaterialGraphNodeKind kind,
        const std::array<float, 4U>& value) {
        switch (kind) {
        case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
            return FloatText(value[0]);
        case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
            return ConstantBoolDefaultValueHint(value[0] != 0.0F);
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
            return FloatText(value[0]) + " " + FloatText(value[1]);
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
            return FloatText(value[0]) + " " + FloatText(value[1]) + " " + FloatText(value[2]);
        case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
            return FloatText(value[0]) + " " + FloatText(value[1]) + " " + FloatText(value[2]) + " " + FloatText(value[3]);
        default:
            return {};
        }
    }

    [[nodiscard]] static std::optional<kb::render::RenderMaterialParameterRange> ConstantRange(
        const kb::render::RenderMaterialGraphNode& node) noexcept {
        if (!node.parameter.hasRange) {
            return std::nullopt;
        }
        return kb::render::RenderMaterialParameterRange{
            .min = node.parameter.rangeMin,
            .max = node.parameter.rangeMax,
        };
    }

    [[nodiscard]] static float ClampConstantComponentValue(
        const kb::render::RenderMaterialGraphNode& node,
        float value) noexcept {
        const std::optional<kb::render::RenderMaterialParameterRange> range = ConstantRange(node);
        if (!range.has_value()) {
            return value;
        }
        return std::clamp(value, range->min, range->max);
    }

    static void ClampConstantValues(
        const kb::render::RenderMaterialGraphNode& node,
        std::array<float, 4U>& values) noexcept {
        const std::size_t componentCount = ConstantComponentCount(node.kind);
        for (std::size_t index = 0U; index < componentCount; ++index) {
            values[index] = ClampConstantComponentValue(node, values[index]);
        }
    }

    [[nodiscard]] static std::array<std::string_view, 4U> ConstantComponentLabels(
        kb::render::RenderMaterialGraphNodeKind kind) noexcept {
        switch (kind) {
        case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
        case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
            return { "Value", "", "", "" };
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
            return { "X", "Y", "", "" };
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
            return { "X", "Y", "Z", "" };
        case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
            return { "Red", "Green", "Blue", "Alpha" };
        default:
            return { "Value", "", "", "" };
        }
    }

    [[nodiscard]] static std::string_view GraphNodeDisplayName(
        kb::render::RenderMaterialGraphNodeKind kind) noexcept {
        switch (kind) {
        case kb::render::RenderMaterialGraphNodeKind::Uv:
            return "UV";
        case kb::render::RenderMaterialGraphNodeKind::TextureCoordinate:
            return "Texture Coordinate";
        case kb::render::RenderMaterialGraphNodeKind::TextureSample:
            return "Texture Sample";
        case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
            return "Texture";
        case kb::render::RenderMaterialGraphNodeKind::TextureObject:
            return "Texture Object";
        case kb::render::RenderMaterialGraphNodeKind::CollectionParameter:
            return "Collection Parameter";
        case kb::render::RenderMaterialGraphNodeKind::TwoSidedSign:
            return "Two Sided Sign";
        case kb::render::RenderMaterialGraphNodeKind::PixelPosition:
            return "Pixel Position";
        case kb::render::RenderMaterialGraphNodeKind::PixelDepth:
            return "Pixel Depth";
        case kb::render::RenderMaterialGraphNodeKind::CameraDepthFade:
            return "Camera Depth Fade";
        case kb::render::RenderMaterialGraphNodeKind::DistanceCullFade:
            return "Distance Cull Fade";
        default:
            return ConstantDisplayName(kind);
        }
    }

    [[nodiscard]] static std::string GraphNodeDisplayNameForNode(const kb::render::RenderMaterialGraphNode& node) {
        if (!node.parameter.displayName.empty()) {
            return node.parameter.displayName;
        }
        return std::string{ GraphNodeDisplayName(node.kind) };
    }

    [[nodiscard]] static std::string NormalizeGraphNodeDisplayName(
        std::string_view displayName,
        kb::render::RenderMaterialGraphNodeKind kind) {
        const std::size_t first = displayName.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return std::string{ GraphNodeDisplayName(kind) };
        }
        const std::size_t last = displayName.find_last_not_of(" \t\r\n");
        std::string normalized{ displayName.substr(first, last - first + 1U) };
        constexpr std::size_t kMaxGraphNodeDisplayNameLength = 64U;
        if (normalized.size() > kMaxGraphNodeDisplayNameLength) {
            normalized.resize(kMaxGraphNodeDisplayNameLength);
        }
        return normalized;
    }

    [[nodiscard]] static std::vector<MaterialEditorGraphNodePropertyOption> GraphNodeEnumOptions(
        kb::render::RenderMaterialGraphNodeKind kind,
        std::string_view propertyId) {
        if (propertyId == "viewProperty" && kind == kb::render::RenderMaterialGraphNodeKind::ViewProperty) {
            return {
                MaterialEditorGraphNodePropertyOption{ .value = "viewSize", .label = "View Size" },
                MaterialEditorGraphNodePropertyOption{ .value = "invViewSize", .label = "Inverse View Size" },
                MaterialEditorGraphNodePropertyOption{ .value = "screenPosition", .label = "Screen Position" },
                MaterialEditorGraphNodePropertyOption{ .value = "pixelPosition", .label = "Pixel Position" },
            };
        }
        if (propertyId == "sceneTexture.source" && kind == kb::render::RenderMaterialGraphNodeKind::SceneTexture) {
            return {
                MaterialEditorGraphNodePropertyOption{ .value = "color", .label = "Scene Color" },
                MaterialEditorGraphNodePropertyOption{ .value = "depth", .label = "Scene Depth" },
            };
        }
        if (propertyId == "customCode.outputType" && kind == kb::render::RenderMaterialGraphNodeKind::CustomCode) {
            return {
                MaterialEditorGraphNodePropertyOption{ .value = "float", .label = "Float" },
                MaterialEditorGraphNodePropertyOption{ .value = "float2", .label = "Float2" },
                MaterialEditorGraphNodePropertyOption{ .value = "float3", .label = "Float3" },
                MaterialEditorGraphNodePropertyOption{ .value = "float4", .label = "Float4" },
            };
        }
        if (propertyId == "staticSwitch.selector" && kind == kb::render::RenderMaterialGraphNodeKind::StaticSwitch) {
            return {
                MaterialEditorGraphNodePropertyOption{ .value = "false", .label = "False" },
                MaterialEditorGraphNodePropertyOption{ .value = "true", .label = "True" },
            };
        }
        if (IsStaticComponentMaskPropertyId(propertyId) &&
            kind == kb::render::RenderMaterialGraphNodeKind::StaticComponentMask) {
            return {
                MaterialEditorGraphNodePropertyOption{ .value = "false", .label = "Off" },
                MaterialEditorGraphNodePropertyOption{ .value = "true", .label = "On" },
            };
        }
        if (IsTransformSpacePropertyId(propertyId) && IsTransformSpaceNode(kind)) {
            return {
                MaterialEditorGraphNodePropertyOption{ .value = "tangent", .label = "Tangent" },
                MaterialEditorGraphNodePropertyOption{ .value = "world", .label = "World" },
                MaterialEditorGraphNodePropertyOption{ .value = "view", .label = "View" },
            };
        }
        if (propertyId != "uvSet") {
            if (propertyId == "constant.bool" && kind == kb::render::RenderMaterialGraphNodeKind::ConstantBool) {
                return {
                    MaterialEditorGraphNodePropertyOption{ .value = "false", .label = "False" },
                    MaterialEditorGraphNodePropertyOption{ .value = "true", .label = "True" },
                };
            }
            return {};
        }
        if (kind == kb::render::RenderMaterialGraphNodeKind::Uv ||
            kind == kb::render::RenderMaterialGraphNodeKind::TextureCoordinate) {
            return {
                MaterialEditorGraphNodePropertyOption{ .value = "0", .label = "UV0" },
                MaterialEditorGraphNodePropertyOption{ .value = "1", .label = "UV1" },
            };
        }
        return {};
    }

    [[nodiscard]] static std::string GraphNodeUvSetValue(const kb::render::RenderMaterialGraphNode& node) {
        if (node.kind == kb::render::RenderMaterialGraphNodeKind::TextureCoordinate) {
            const std::string& hint = node.parameter.defaultValueHint;
            if (hint == "1" || hint == "uv1" || hint == "UV1") {
                return "1";
            }
            const std::vector<float> numbers = ParseDefaultNumbers(hint);
            if (numbers.size() > 2U && numbers[2] >= 0.5F) {
                return "1";
            }
            return "0";
        }
        if (node.parameter.defaultValueHint == "1" || node.parameter.defaultValueHint == "uv1" || node.parameter.defaultValueHint == "UV1") {
            return "1";
        }
        return "0";
    }

    [[nodiscard]] static bool ParseStaticBoolNodeValue(const kb::render::RenderMaterialGraphNode& node) noexcept {
        return node.parameter.defaultValueHint == "true" || node.parameter.defaultValueHint == "1";
    }

    [[nodiscard]] static bool IsStaticComponentMaskPropertyId(std::string_view propertyId) noexcept {
        return propertyId == "staticComponentMask.r" ||
            propertyId == "staticComponentMask.g" ||
            propertyId == "staticComponentMask.b" ||
            propertyId == "staticComponentMask.a";
    }

    [[nodiscard]] static char StaticComponentMaskPropertyChannel(std::string_view propertyId) noexcept {
        if (propertyId == "staticComponentMask.g") {
            return 'g';
        }
        if (propertyId == "staticComponentMask.b") {
            return 'b';
        }
        if (propertyId == "staticComponentMask.a") {
            return 'a';
        }
        return 'r';
    }

    [[nodiscard]] static bool StaticComponentMaskChannelEnabled(
        const kb::render::RenderMaterialGraphNode& node,
        std::string_view propertyId) noexcept {
        const std::string_view hint = node.parameter.defaultValueHint.empty()
            ? std::string_view{ "rgba" }
            : std::string_view{ node.parameter.defaultValueHint };
        switch (StaticComponentMaskPropertyChannel(propertyId)) {
        case 'g':
            return hint.find('g') != std::string_view::npos || hint.find('y') != std::string_view::npos;
        case 'b':
            return hint.find('b') != std::string_view::npos || hint.find('z') != std::string_view::npos;
        case 'a':
            return hint.find('a') != std::string_view::npos || hint.find('w') != std::string_view::npos;
        default:
            return hint.find('r') != std::string_view::npos || hint.find('x') != std::string_view::npos;
        }
    }

    [[nodiscard]] static std::string StaticComponentMaskHintWithChannel(
        const kb::render::RenderMaterialGraphNode& node,
        std::string_view propertyId,
        bool enabled) {
        std::array<bool, 4U> channels{
            StaticComponentMaskChannelEnabled(node, "staticComponentMask.r"),
            StaticComponentMaskChannelEnabled(node, "staticComponentMask.g"),
            StaticComponentMaskChannelEnabled(node, "staticComponentMask.b"),
            StaticComponentMaskChannelEnabled(node, "staticComponentMask.a"),
        };
        switch (StaticComponentMaskPropertyChannel(propertyId)) {
        case 'g':
            channels[1] = enabled;
            break;
        case 'b':
            channels[2] = enabled;
            break;
        case 'a':
            channels[3] = enabled;
            break;
        default:
            channels[0] = enabled;
            break;
        }
        std::string hint;
        if (channels[0]) hint.push_back('r');
        if (channels[1]) hint.push_back('g');
        if (channels[2]) hint.push_back('b');
        if (channels[3]) hint.push_back('a');
        return hint;
    }

    [[nodiscard]] static bool IsTransformSpaceNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
        return kind == kb::render::RenderMaterialGraphNodeKind::Transform ||
            kind == kb::render::RenderMaterialGraphNodeKind::TransformPosition;
    }

    [[nodiscard]] static bool IsTransformSpacePropertyId(std::string_view propertyId) noexcept {
        return propertyId == "transform.fromSpace" || propertyId == "transform.toSpace";
    }

    [[nodiscard]] static std::array<std::string, 2U> TransformSpaces(const kb::render::RenderMaterialGraphNode& node) {
        std::array<std::string, 2U> spaces{ "tangent", "world" };
        std::istringstream input{ node.parameter.defaultValueHint };
        std::string fromSpace;
        std::string toSpace;
        input >> fromSpace >> toSpace;
        if (IsTransformSpaceValue(fromSpace)) {
            spaces[0] = NormalizeTransformSpaceValue(fromSpace);
        }
        if (IsTransformSpaceValue(toSpace)) {
            spaces[1] = NormalizeTransformSpaceValue(toSpace);
        }
        return spaces;
    }

    [[nodiscard]] static bool IsTransformSpaceValue(std::string_view value) noexcept {
        return value == "tangent" || value == "Tangent" ||
            value == "world" || value == "World" ||
            value == "view" || value == "View";
    }

    [[nodiscard]] static std::string NormalizeTransformSpaceValue(std::string_view value) {
        if (value == "view" || value == "View") {
            return "view";
        }
        if (value == "world" || value == "World") {
            return "world";
        }
        return "tangent";
    }

    [[nodiscard]] static std::string TransformSpaceHintWithProperty(
        const kb::render::RenderMaterialGraphNode& node,
        std::string_view propertyId,
        std::string_view value) {
        std::array<std::string, 2U> spaces = TransformSpaces(node);
        if (propertyId == "transform.fromSpace") {
            spaces[0] = NormalizeTransformSpaceValue(value);
        } else {
            spaces[1] = NormalizeTransformSpaceValue(value);
        }
        return spaces[0] + " " + spaces[1];
    }

    [[nodiscard]] static std::string TextureCoordinateHintWithUvSet(
        const kb::render::RenderMaterialGraphNode& node,
        std::string_view uvSetValue) {
        const std::array<float, 2U> tiling = TextureCoordinateTiling(node);
        return TextureCoordinateHint(tiling[0], tiling[1], uvSetValue);
    }

    [[nodiscard]] static std::array<float, 2U> TextureCoordinateTiling(const kb::render::RenderMaterialGraphNode& node) {
        std::array<float, 2U> tiling{ 1.0F, 1.0F };
        const std::string& hint = node.parameter.defaultValueHint;
        if (!hint.empty() && hint != "0" && hint != "1" && hint != "uv0" && hint != "uv1" && hint != "UV0" && hint != "UV1") {
            const std::vector<float> numbers = ParseDefaultNumbers(hint);
            if (!numbers.empty()) {
                tiling[0] = numbers[0];
                tiling[1] = numbers.size() > 1U ? numbers[1] : tiling[0];
            }
        }
        return tiling;
    }

    [[nodiscard]] static std::string TextureCoordinateHint(float uTile, float vTile, std::string_view uvSetValue) {
        return FloatText(uTile) + " " + FloatText(vTile) + " " + (uvSetValue == "1" ? "1" : "0");
    }

    [[nodiscard]] static std::string GraphNodeViewPropertyValue(const kb::render::RenderMaterialGraphNode& node) {
        if (node.parameter.defaultValueHint == "invViewSize" ||
            node.parameter.defaultValueHint == "inverseViewSize" ||
            node.parameter.defaultValueHint == "viewInvSize") {
            return "invViewSize";
        }
        if (node.parameter.defaultValueHint == "screenPosition" ||
            node.parameter.defaultValueHint == "viewportUV" ||
            node.parameter.defaultValueHint == "screenUV") {
            return "screenPosition";
        }
        if (node.parameter.defaultValueHint == "pixelPosition" ||
            node.parameter.defaultValueHint == "viewportPixelPosition" ||
            node.parameter.defaultValueHint == "screenPixelPosition") {
            return "pixelPosition";
        }
        return "viewSize";
    }

    [[nodiscard]] static std::string GraphNodeSceneTextureSourceValue(const kb::render::RenderMaterialGraphNode& node) {
        if (node.parameter.defaultValueHint == "depth" ||
            node.parameter.defaultValueHint == "sceneDepth" ||
            node.parameter.defaultValueHint == "SceneDepth") {
            return "depth";
        }
        return "color";
    }

    [[nodiscard]] static bool IsCustomCodeEditableValueType(kb::render::RenderMaterialGraphPinType type) noexcept {
        return type == kb::render::RenderMaterialGraphPinType::Float ||
            type == kb::render::RenderMaterialGraphPinType::Float2 ||
            type == kb::render::RenderMaterialGraphPinType::Float3 ||
            type == kb::render::RenderMaterialGraphPinType::Float4;
    }

    struct ColorRampStop {
        float position = 0.0F;
        float r = 0.0F;
        float g = 0.0F;
        float b = 0.0F;
    };

    [[nodiscard]] static std::vector<ColorRampStop> ColorRampStops(const kb::render::RenderMaterialGraphNode& node) {
        const std::vector<float> numbers = ParseDefaultNumbers(node.parameter.defaultValueHint);
        std::vector<ColorRampStop> stops;
        for (std::size_t index = 0U; index + 3U < numbers.size(); index += 4U) {
            stops.push_back(ColorRampStop{
                .position = std::clamp(numbers[index], 0.0F, 1.0F),
                .r = std::clamp(numbers[index + 1U], 0.0F, 1.0F),
                .g = std::clamp(numbers[index + 2U], 0.0F, 1.0F),
                .b = std::clamp(numbers[index + 3U], 0.0F, 1.0F),
            });
        }
        if (stops.size() < 2U) {
            stops = {
                ColorRampStop{ .position = 0.0F, .r = 0.0F, .g = 0.0F, .b = 0.0F },
                ColorRampStop{ .position = 1.0F, .r = 1.0F, .g = 1.0F, .b = 1.0F },
            };
        }
        return stops;
    }

    [[nodiscard]] static std::string ColorRampHint(const std::vector<ColorRampStop>& stops) {
        std::string hint;
        for (const ColorRampStop& stop : stops) {
            if (!hint.empty()) {
                hint += ' ';
            }
            hint += FloatText(std::clamp(stop.position, 0.0F, 1.0F)) + " " +
                FloatText(std::clamp(stop.r, 0.0F, 1.0F)) + " " +
                FloatText(std::clamp(stop.g, 0.0F, 1.0F)) + " " +
                FloatText(std::clamp(stop.b, 0.0F, 1.0F));
        }
        return hint.empty() ? std::string{ "0 0 0 0 1 1 1 1" } : hint;
    }

    [[nodiscard]] static std::optional<std::size_t> ColorRampStopIndexForPositionComponent(std::size_t componentIndex) noexcept {
        if (componentIndex == 0U) {
            return 0U;
        }
        if (componentIndex == 4U) {
            return 1U;
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<std::size_t> ColorRampStopIndexForColorProperty(std::string_view propertyId) noexcept {
        if (propertyId == "colorRamp.stop0.color") {
            return 0U;
        }
        if (propertyId == "colorRamp.stop1.color") {
            return 1U;
        }
        return std::nullopt;
    }

    [[nodiscard]] static bool IsGraphTextureAssetNode(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
        return kind == kb::render::RenderMaterialGraphNodeKind::TextureSample ||
            kind == kb::render::RenderMaterialGraphNodeKind::TextureSampleCube ||
            kind == kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume ||
            kind == kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray ||
            kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture ||
            kind == kb::render::RenderMaterialGraphNodeKind::TextureObject ||
            kind == kb::render::RenderMaterialGraphNodeKind::TextureObjectCube ||
            kind == kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume ||
            kind == kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray;
    }

    [[nodiscard]] static std::uint64_t GraphNodeTextureAssetId(
        const kb::render::RenderMaterialGraphNode& node,
        const kb::render::RenderMaterialAssetData& document) noexcept {
        for (const kb::render::RenderMaterialGraphParameterValue& value : document.graphParameterValues) {
            if (value.type == kb::render::RenderMaterialParameterType::Texture &&
                value.stableId == node.parameter.stableId) {
                return value.assetId;
            }
        }
        return 0U;
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

    [[nodiscard]] static bool IsStaticOverrideNodeKind(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
        return kind == kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter ||
            kind == kb::render::RenderMaterialGraphNodeKind::StaticSwitch ||
            kind == kb::render::RenderMaterialGraphNodeKind::StaticComponentMask;
    }

    [[nodiscard]] static std::string StableIdForGraphNode(const kb::render::RenderMaterialGraphNode& node) {
        if (!node.parameter.stableId.empty()) {
            return node.parameter.stableId;
        }
        if (!node.parameter.displayName.empty()) {
            return node.parameter.displayName;
        }
        return std::string{ kb::render::RenderMaterialGraphNodeKindName(node.kind) } + std::to_string(node.id);
    }

    [[nodiscard]] static std::string DefaultStaticOverrideValue(kb::render::RenderMaterialGraphNodeKind kind) {
        switch (kind) {
        case kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter:
        case kb::render::RenderMaterialGraphNodeKind::StaticSwitch:
            return "false";
        case kb::render::RenderMaterialGraphNodeKind::StaticComponentMask:
            return "rgba";
        default:
            break;
        }
        return {};
    }

    [[nodiscard]] static bool IsStaticBoolText(std::string_view text) noexcept {
        return text == "true" || text == "false" || text == "1" || text == "0";
    }

    [[nodiscard]] static bool IsStaticMaskText(std::string_view text) noexcept {
        if (text.empty() || text.size() > 4U) {
            return false;
        }
        bool seen[4]{};
        for (const char ch : text) {
            std::size_t index = 4U;
            if (ch == 'r' || ch == 'R') index = 0U;
            if (ch == 'g' || ch == 'G') index = 1U;
            if (ch == 'b' || ch == 'B') index = 2U;
            if (ch == 'a' || ch == 'A') index = 3U;
            if (index >= 4U || seen[index]) {
                return false;
            }
            seen[index] = true;
        }
        return true;
    }

    [[nodiscard]] static bool IsStaticOverrideValueValid(
        kb::render::RenderMaterialGraphNodeKind kind,
        std::string_view value) noexcept {
        switch (kind) {
        case kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter:
        case kb::render::RenderMaterialGraphNodeKind::StaticSwitch:
            return IsStaticBoolText(value);
        case kb::render::RenderMaterialGraphNodeKind::StaticComponentMask:
            return IsStaticMaskText(value);
        default:
            break;
        }
        return false;
    }

    [[nodiscard]] static const kb::render::RenderMaterialGraphNode* FindStaticOverrideNode(
        const kb::render::RenderMaterialAssetData& material,
        std::string_view stableId,
        kb::render::RenderMaterialGraphNodeKind nodeKind) noexcept {
        for (const kb::render::RenderMaterialGraphNode& node : material.graph.nodes) {
            if (node.kind == nodeKind && StableIdForGraphNode(node) == stableId) {
                return &node;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static bool RemoveGraphParameterOverride(
        kb::render::RenderMaterialAssetData& material,
        std::string_view stableId,
        kb::render::RenderMaterialParameterType type) {
        const auto oldEnd = std::remove_if(
            material.graphParameterValues.begin(),
            material.graphParameterValues.end(),
            [stableId, type](const kb::render::RenderMaterialGraphParameterValue& value) {
                return value.stableId == stableId && value.type == type;
            });
        if (oldEnd == material.graphParameterValues.end()) {
            return false;
        }
        material.graphParameterValues.erase(oldEnd, material.graphParameterValues.end());
        return true;
    }

    static void RemoveStaticOverride(
        kb::render::RenderMaterialInstanceAssetData& instance,
        std::string_view stableId,
        kb::render::RenderMaterialGraphNodeKind nodeKind) {
        instance.staticParameterOverrides.erase(
            std::remove_if(
                instance.staticParameterOverrides.begin(),
                instance.staticParameterOverrides.end(),
                [stableId, nodeKind](const kb::render::RenderMaterialInstanceStaticParameterOverride& overrideValue) {
                    return overrideValue.stableId == stableId && overrideValue.nodeKind == nodeKind;
                }),
            instance.staticParameterOverrides.end());
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
                .overrideActive = false,
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
                .overrideActive = false,
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

    [[nodiscard]] static bool InstanceHasGraphParameterOverride(
        const kb::render::RenderMaterialInstanceAssetData& instance,
        std::string_view stableId,
        kb::render::RenderMaterialParameterType type) noexcept {
        if (!instance.hasOverrides) {
            return false;
        }
        for (const kb::render::RenderMaterialGraphParameterValue& value : instance.overrides.graphParameterValues) {
            if (value.stableId == stableId && value.type == type) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static std::vector<MaterialEditorParameter> BuildInstanceParameters(
        const kb::render::RenderMaterialAssetData& effectiveDocument,
        const kb::render::RenderMaterialTypeSchema& schema,
        const kb::render::RenderMaterialAssetData& parentDocument,
        const kb::render::RenderMaterialInstanceAssetData& instanceDocument) {
        std::vector<MaterialEditorParameter> parameters = BuildParameters(effectiveDocument, schema);
        const std::vector<MaterialEditorParameter> parentParameters = BuildParameters(parentDocument, schema);
        for (MaterialEditorParameter& parameter : parameters) {
            const auto parent = std::ranges::find_if(parentParameters, [&parameter](const MaterialEditorParameter& candidate) {
                return candidate.stableId == parameter.stableId && candidate.type == parameter.type;
            });
            if (parent != parentParameters.end()) {
                parameter.defaultValue = parent->value;
            }
            parameter.overrideActive = InstanceHasGraphParameterOverride(instanceDocument, parameter.stableId, parameter.type);
        }
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

    [[nodiscard]] static std::uint32_t NextGraphCommentId(const kb::render::RenderMaterialGraphDocument& graph) noexcept {
        std::uint32_t next = 1U;
        for (const kb::render::RenderMaterialGraphCommentBox& comment : graph.comments) {
            next = std::max(next, comment.id + 1U);
        }
        return next;
    }

    [[nodiscard]] static const kb::render::RenderMaterialGraphCommentBox* FindGraphComment(
        const kb::render::RenderMaterialGraphDocument& graph,
        std::uint32_t commentId) noexcept {
        for (const kb::render::RenderMaterialGraphCommentBox& comment : graph.comments) {
            if (comment.id == commentId) {
                return &comment;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static kb::render::RenderMaterialGraphCommentBox* FindMutableGraphComment(
        kb::render::RenderMaterialGraphDocument& graph,
        std::uint32_t commentId) noexcept {
        for (kb::render::RenderMaterialGraphCommentBox& comment : graph.comments) {
            if (comment.id == commentId) {
                return &comment;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static std::uint32_t NextGraphCompositeId(const kb::render::RenderMaterialGraphDocument& graph) noexcept {
        std::uint32_t next = 1U;
        for (const kb::render::RenderMaterialGraphCompositeSubgraph& composite : graph.composites) {
            next = std::max(next, composite.id + 1U);
        }
        return next;
    }

    [[nodiscard]] static const kb::render::RenderMaterialGraphCompositeSubgraph* FindGraphComposite(
        const kb::render::RenderMaterialGraphDocument& graph,
        std::uint32_t compositeId) noexcept {
        for (const kb::render::RenderMaterialGraphCompositeSubgraph& composite : graph.composites) {
            if (composite.id == compositeId) {
                return &composite;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static kb::render::RenderMaterialGraphCompositeSubgraph* FindMutableGraphComposite(
        kb::render::RenderMaterialGraphDocument& graph,
        std::uint32_t compositeId) noexcept {
        for (kb::render::RenderMaterialGraphCompositeSubgraph& composite : graph.composites) {
            if (composite.id == compositeId) {
                return &composite;
            }
        }
        return nullptr;
    }

    static void RemoveGraphCompositeNodeReferences(
        kb::render::RenderMaterialGraphDocument& graph,
        const std::vector<std::uint32_t>& nodeIds) {
        if (nodeIds.empty()) {
            return;
        }
        for (kb::render::RenderMaterialGraphCompositeSubgraph& composite : graph.composites) {
            const auto oldEnd = std::remove_if(composite.nodeIds.begin(), composite.nodeIds.end(), [&nodeIds](std::uint32_t nodeId) {
                return std::ranges::find(nodeIds, nodeId) != nodeIds.end();
            });
            composite.nodeIds.erase(oldEnd, composite.nodeIds.end());
        }
    }

    [[nodiscard]] static bool GraphNodeInsideComment(
        const kb::render::RenderMaterialGraphNode& node,
        const kb::render::RenderMaterialGraphCommentBox& comment) noexcept {
        const std::int32_t right = comment.positionX + std::max<std::int32_t>(1, comment.width);
        const std::int32_t bottom = comment.positionY + std::max<std::int32_t>(1, comment.height);
        return node.positionX >= comment.positionX &&
            node.positionX <= right &&
            node.positionY >= comment.positionY &&
            node.positionY <= bottom;
    }

    [[nodiscard]] static bool NodeIdInList(const std::vector<std::uint32_t>& nodeIds, std::uint32_t nodeId) noexcept {
        return std::ranges::find(nodeIds, nodeId) != nodeIds.end();
    }

    [[nodiscard]] static std::optional<GraphClipboard> BuildGraphClipboard(
        const kb::render::RenderMaterialAssetData& document,
        const std::vector<std::uint32_t>& selectedNodeIds) {
        GraphClipboard clipboard;
        for (const kb::render::RenderMaterialGraphNode& node : document.graph.nodes) {
            if (NodeIdInList(selectedNodeIds, node.id) && node.kind != kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
                clipboard.nodes.push_back(node);
            }
        }
        if (clipboard.nodes.empty()) {
            return std::nullopt;
        }

        std::vector<std::uint32_t> copiedNodeIds;
        copiedNodeIds.reserve(clipboard.nodes.size());
        for (const kb::render::RenderMaterialGraphNode& node : clipboard.nodes) {
            copiedNodeIds.push_back(node.id);
        }
        for (const kb::render::RenderMaterialGraphLink& link : document.graph.links) {
            if (NodeIdInList(copiedNodeIds, link.fromNodeId) && NodeIdInList(copiedNodeIds, link.toNodeId)) {
                clipboard.links.push_back(link);
            }
        }
        std::vector<std::string> copiedStableIds;
        for (const kb::render::RenderMaterialGraphNode& node : clipboard.nodes) {
            if (!node.parameter.stableId.empty() && std::ranges::find(copiedStableIds, node.parameter.stableId) == copiedStableIds.end()) {
                copiedStableIds.push_back(node.parameter.stableId);
            }
        }
        for (const kb::render::RenderMaterialGraphParameterValue& value : document.graphParameterValues) {
            if (std::ranges::find(copiedStableIds, value.stableId) != copiedStableIds.end()) {
                clipboard.parameterValues.push_back(value);
            }
        }
        return clipboard;
    }

    [[nodiscard]] static std::uint32_t RemapGraphNodeId(
        const std::vector<std::pair<std::uint32_t, std::uint32_t>>& remap,
        std::uint32_t nodeId) noexcept {
        for (const auto& entry : remap) {
            if (entry.first == nodeId) {
                return entry.second;
            }
        }
        return 0U;
    }

    void PruneSelectionToWorkingCopy() {
        if (!workingCopy_.has_value()) {
            selectedNodeIds_.clear();
            selectedNodeId_ = 0U;
            selectedCommentId_ = 0U;
            CancelGraphConstantInlineEdit();
            CancelGraphNodeRenameEdit();
            CloseGraphNodeEnumDropdown();
            return;
        }
        std::vector<std::uint32_t> kept;
        kept.reserve(selectedNodeIds_.size());
        for (std::uint32_t nodeId : selectedNodeIds_) {
            if (kb::render::FindRenderMaterialGraphNode(workingCopy_->graph, nodeId) != nullptr &&
                std::ranges::find(kept, nodeId) == kept.end()) {
                kept.push_back(nodeId);
            }
        }
        selectedNodeIds_ = std::move(kept);
        if (std::ranges::find(selectedNodeIds_, selectedNodeId_) == selectedNodeIds_.end()) {
            selectedNodeId_ = selectedNodeIds_.empty() ? 0U : selectedNodeIds_.back();
        }
        if (inlineConstantEditNodeId_ != 0U && std::ranges::find(selectedNodeIds_, inlineConstantEditNodeId_) == selectedNodeIds_.end()) {
            CancelGraphConstantInlineEdit();
        }
        if (renameNodeId_ != 0U && std::ranges::find(selectedNodeIds_, renameNodeId_) == selectedNodeIds_.end()) {
            CancelGraphNodeRenameEdit();
        }
        if (graphNodeEnumDropdownNodeId_ != 0U && std::ranges::find(selectedNodeIds_, graphNodeEnumDropdownNodeId_) == selectedNodeIds_.end()) {
            CloseGraphNodeEnumDropdown();
        }
        if (selectedCommentId_ != 0U && FindGraphComment(workingCopy_->graph, selectedCommentId_) == nullptr) {
            selectedCommentId_ = 0U;
        }
    }

    void ReplaceSelectedGraphNodeRenameText() {
        if (renameSelectAll_) {
            renameBuffer_.clear();
            renameSelectAll_ = false;
        }
    }

    [[nodiscard]] static kb::render::RenderMaterialGraphCustomCode DefaultCustomCode() {
        return kb::render::RenderMaterialGraphCustomCode{
            .body = "return A * B;",
            .outputType = kb::render::RenderMaterialGraphPinType::Float4,
            .inputs = {
                kb::render::RenderMaterialGraphCustomPin{ .name = "A", .type = kb::render::RenderMaterialGraphPinType::Float4 },
                kb::render::RenderMaterialGraphCustomPin{ .name = "B", .type = kb::render::RenderMaterialGraphPinType::Float4 },
            },
            .outputs = {},
        };
    }

    [[nodiscard]] static kb::render::RenderMaterialGraphCustomCode DefaultMaterialFunctionCall() {
        return kb::render::RenderMaterialGraphCustomCode{
            .body = {},
            .outputType = kb::render::RenderMaterialGraphPinType::Float4,
            .inputs = {
                kb::render::RenderMaterialGraphCustomPin{ .name = "Input", .type = kb::render::RenderMaterialGraphPinType::Float4 },
            },
            .outputs = {
                kb::render::RenderMaterialGraphCustomPin{ .name = "Output", .type = kb::render::RenderMaterialGraphPinType::Float4 },
            },
        };
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
        case kb::render::RenderMaterialGraphNodeKind::TextureObject:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "textureObject" + std::to_string(nodeId),
                .displayName = "Texture Object " + std::to_string(nodeId),
                .textureRole = "baseColor",
                .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
                .overrideSupported = true,
            };
        case kb::render::RenderMaterialGraphNodeKind::TextureObjectCube:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "textureCubeObject" + std::to_string(nodeId),
                .displayName = "Texture Cube Object " + std::to_string(nodeId),
                .textureRole = "baseColor",
                .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
                .overrideSupported = true,
            };
        case kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "textureVolumeObject" + std::to_string(nodeId),
                .displayName = "Texture Volume Object " + std::to_string(nodeId),
                .textureRole = "baseColor",
                .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
                .overrideSupported = true,
            };
        case kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "textureArrayObject" + std::to_string(nodeId),
                .displayName = "Texture 2D Array Object " + std::to_string(nodeId),
                .textureRole = "baseColor",
                .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
                .overrideSupported = true,
            };
        case kb::render::RenderMaterialGraphNodeKind::CollectionParameter:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "collectionParam" + std::to_string(nodeId),
                .displayName = "Collection Parameter " + std::to_string(nodeId),
                .defaultValueHint = "0",
                .overrideSupported = false,
                .description = "Material parameter collection value",
            };
        case kb::render::RenderMaterialGraphNodeKind::TextureSample:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "textureSample" + std::to_string(nodeId),
                .displayName = "Texture Sample " + std::to_string(nodeId),
                .textureRole = "baseColor",
                .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
                .overrideSupported = true,
            };
        case kb::render::RenderMaterialGraphNodeKind::TextureSampleCube:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "textureCubeSample" + std::to_string(nodeId),
                .displayName = "Texture Cube Sample " + std::to_string(nodeId),
                .textureRole = "baseColor",
                .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
                .overrideSupported = true,
            };
        case kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "textureVolumeSample" + std::to_string(nodeId),
                .displayName = "Texture Volume Sample " + std::to_string(nodeId),
                .textureRole = "baseColor",
                .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
                .overrideSupported = true,
            };
        case kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "textureArraySample" + std::to_string(nodeId),
                .displayName = "Texture 2D Array Sample " + std::to_string(nodeId),
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
        case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Bool",
                .defaultValueHint = "false",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "XY",
                .defaultValueHint = "0 0",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "RGB Node",
                .defaultValueHint = "0 0 0",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "RGBA Node",
                .defaultValueHint = "1 1 1 1",
                .hasRange = true,
                .rangeMin = 0.0F,
                .rangeMax = 1.0F,
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::CustomCode:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Custom Code",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::RuntimeSwitch:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Switch",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::StaticSwitch:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Static Switch",
                .defaultValueHint = "false",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::StaticComponentMask:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Static Component Mask",
                .defaultValueHint = "rgba",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::QualitySwitch:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Quality Switch",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::FeatureLevelSwitch:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Feature Level Switch",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::ShadingPathSwitch:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Shading Path Switch",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::ShaderStageSwitch:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Shader Stage Switch",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::Reroute:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Reroute",
                .defaultValueHint = "float4",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "NamedReroute",
                .displayName = "Named Reroute",
                .defaultValueHint = "float4",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "NamedReroute",
                .displayName = "Named Reroute",
                .defaultValueHint = "float4",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::CompositeInput:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Composite Input",
                .defaultValueHint = "float4",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::CompositeOutput:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Composite Output",
                .defaultValueHint = "float4",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::FunctionInput:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "Input",
                .displayName = "Function Input",
                .defaultValueHint = "float4",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::FunctionOutput:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "Output",
                .displayName = "Function Output",
                .defaultValueHint = "float4",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = {},
                .displayName = "Material Function",
                .defaultValueHint = {},
                .overrideSupported = false,
                .description = "Assign a RenderMaterialFunction asset id to call a reusable material function.",
            };
        case kb::render::RenderMaterialGraphNodeKind::LayerStack:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .stableId = "surfaceLayers",
                .displayName = "Layer Stack",
                .defaultValueHint = {},
                .overrideSupported = false,
                .description = "Composes material layer functions with material blend functions.",
            };
        case kb::render::RenderMaterialGraphNodeKind::Uv:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "UV",
                .defaultValueHint = "0",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::TextureCoordinate:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Texture Coordinate",
                .defaultValueHint = "1 1 0",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::Panner:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Panner",
                .defaultValueHint = "0.1 0",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::Rotator:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Rotator",
                .defaultValueHint = "1 0.5 0.5",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::BumpOffset:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Bump Offset",
                .defaultValueHint = "0.05",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::ConstantBiasScale:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Constant Bias Scale",
                .defaultValueHint = "0 1",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::RotateAboutAxis:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Rotate About Axis",
                .defaultValueHint = "0 0 1 0",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::Desaturate:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Desaturate",
                .defaultValueHint = "1",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::Fresnel:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Fresnel",
                .defaultValueHint = "5 0",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::SphereMask:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Sphere Mask",
                .defaultValueHint = "1 0.5",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Antialiased Texture Mask",
                .defaultValueHint = "0.5",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::Transform:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Transform",
                .defaultValueHint = "tangent world",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::TransformPosition:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Transform Position",
                .defaultValueHint = "tangent world",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::ViewProperty:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "View Property",
                .defaultValueHint = "viewSize",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::SceneTexture:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Scene Texture",
                .defaultValueHint = "color",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::CameraDepthFade:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Camera Depth Fade",
                .defaultValueHint = "1 0",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::DepthFade:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Depth Fade",
                .defaultValueHint = "0.01",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::ColorRamp:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "Color Ramp",
                .defaultValueHint = "0 0 0 0 1 1 1 1",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
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
        case kb::render::RenderMaterialGraphNodeKind::Fmod:
        case kb::render::RenderMaterialGraphNodeKind::InverseLerp:
        case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeX:
        case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeY:
        case kb::render::RenderMaterialGraphNodeKind::BlackBody:
        case kb::render::RenderMaterialGraphNodeKind::Noise:
        case kb::render::RenderMaterialGraphNodeKind::VectorNoise:
        case kb::render::RenderMaterialGraphNodeKind::Sobol:
        case kb::render::RenderMaterialGraphNodeKind::AppendVector:
        case kb::render::RenderMaterialGraphNodeKind::DotProduct:
        case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
        case kb::render::RenderMaterialGraphNodeKind::Normalize:
        case kb::render::RenderMaterialGraphNodeKind::Length:
        case kb::render::RenderMaterialGraphNodeKind::Distance:
        case kb::render::RenderMaterialGraphNodeKind::BreakVector:
        case kb::render::RenderMaterialGraphNodeKind::MakeVector:
        case kb::render::RenderMaterialGraphNodeKind::Step:
        case kb::render::RenderMaterialGraphNodeKind::SmoothStep:
        case kb::render::RenderMaterialGraphNodeKind::If:
        case kb::render::RenderMaterialGraphNodeKind::Negate:
        case kb::render::RenderMaterialGraphNodeKind::Sign:
        case kb::render::RenderMaterialGraphNodeKind::Round:
        case kb::render::RenderMaterialGraphNodeKind::Truncate:
        case kb::render::RenderMaterialGraphNodeKind::Tangent:
        case kb::render::RenderMaterialGraphNodeKind::ArcSine:
        case kb::render::RenderMaterialGraphNodeKind::ArcCosine:
        case kb::render::RenderMaterialGraphNodeKind::ArcTangent:
        case kb::render::RenderMaterialGraphNodeKind::ArcTangent2:
        case kb::render::RenderMaterialGraphNodeKind::ArcSineFast:
        case kb::render::RenderMaterialGraphNodeKind::ArcCosineFast:
        case kb::render::RenderMaterialGraphNodeKind::ArcTangentFast:
        case kb::render::RenderMaterialGraphNodeKind::ArcTangent2Fast:
        case kb::render::RenderMaterialGraphNodeKind::Clamp:
        case kb::render::RenderMaterialGraphNodeKind::Lerp:
        case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
            break;
        }
        return {};
    }

    [[nodiscard]] static std::string TextureDimensionName(kb::render::RenderMaterialGraphTextureDimension dimension) {
        switch (dimension) {
        case kb::render::RenderMaterialGraphTextureDimension::Texture2D:
            return "2D";
        case kb::render::RenderMaterialGraphTextureDimension::TextureCube:
            return "Cube";
        case kb::render::RenderMaterialGraphTextureDimension::Texture3D:
            return "3D";
        case kb::render::RenderMaterialGraphTextureDimension::Texture2DArray:
            return "2DArray";
        }
        return "2D";
    }

    [[nodiscard]] static std::string TextureColorSpaceName(kb::render::RenderMaterialTextureColorSpace colorSpace) {
        switch (colorSpace) {
        case kb::render::RenderMaterialTextureColorSpace::Srgb:
            return "sRGB";
        case kb::render::RenderMaterialTextureColorSpace::Linear:
            return "Linear";
        case kb::render::RenderMaterialTextureColorSpace::Unknown:
            return "Unknown";
        }
        return "Unknown";
    }

    [[nodiscard]] static std::string SamplerFilterName(kb::render::RenderMaterialGraphSamplerFilter filter) {
        switch (filter) {
        case kb::render::RenderMaterialGraphSamplerFilter::Linear:
            return "linear";
        case kb::render::RenderMaterialGraphSamplerFilter::Point:
            return "point";
        }
        return "linear";
    }

    [[nodiscard]] static std::string SamplerWrapName(kb::render::RenderMaterialGraphSamplerWrap wrap) {
        switch (wrap) {
        case kb::render::RenderMaterialGraphSamplerWrap::Repeat:
            return "repeat";
        case kb::render::RenderMaterialGraphSamplerWrap::Clamp:
            return "clamp";
        case kb::render::RenderMaterialGraphSamplerWrap::Mirror:
            return "mirror";
        }
        return "repeat";
    }

    [[nodiscard]] static std::string SamplerStateSummary(const kb::render::RenderMaterialGraphSamplerState& state) {
        return "sampler min=" + SamplerFilterName(state.minFilter) +
            " mag=" + SamplerFilterName(state.magFilter) +
            " wrapU=" + SamplerWrapName(state.wrapU) +
            " wrapV=" + SamplerWrapName(state.wrapV);
    }

    [[nodiscard]] static MaterialEditorShaderViewerModel ShaderViewerCompileFailure(
        const kb::render::RenderMaterialGraphCompileResult& compile) {
        MaterialEditorShaderViewerModel model{};
        if (!compile.Succeeded()) {
            model.warnings.push_back("Shader viewer unavailable: graph compile failed.");
            for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : compile.diagnostics) {
                model.warnings.push_back(GraphDiagnosticLine(diagnostic));
            }
        }
        return model;
    }

    [[nodiscard]] static MaterialEditorShaderViewerModel BuildShaderViewer(
        const kb::render::RenderMaterialGraphCompileResult& compile) {
        if (!compile.Succeeded()) {
            return ShaderViewerCompileFailure(compile);
        }
        MaterialEditorShaderViewerModel model{};
        const kb::render::RenderMaterialGraphReflection& reflection = compile.shader.reflection;
        model.available = true;
        model.sourceHash = compile.shader.sourceHash;
        model.reflectionHash = kb::render::ComputeRenderMaterialGraphReflectionHash(reflection);
        const bool hasVertexDomainOutput =
            reflection.hasWorldPositionOffset || reflection.hasCustomizedUv0 || reflection.hasDisplacement;
        const std::string basePass = kb::render::IsRenderMaterialGraphBlendModeTransparent(reflection.blendMode)
            ? "BaseTransparent"
            : "BaseOpaque";
        model.sources.push_back(MaterialEditorShaderSourceView{
            .passName = basePass,
            .backendName = "shaderc-input",
            .stageName = "fragment",
            .source = kb::render::BuildGraphFragmentWrapperSource(compile.shader, basePass),
            .sourceHash = compile.shader.sourceHash,
        });
        model.sources.push_back(MaterialEditorShaderSourceView{
            .passName = "ShadowDepth",
            .backendName = "builtin",
            .stageName = hasVertexDomainOutput ? "vertex" : "fixed",
            .source = hasVertexDomainOutput
                ? "Vertex-domain material outputs require a generated vertex wrapper during cook."
                : "ShadowDepth uses the built-in fixed shadow shader for this material.",
            .sourceHash = compile.shader.sourceHash,
        });

        for (const kb::render::RenderMaterialGraphReflectionUniform& uniform : reflection.uniforms) {
            model.reflectionRows.push_back(MaterialEditorShaderReflectionRow{
                .category = "uniform",
                .name = uniform.name,
                .stableId = uniform.stableId,
                .detail = std::string{ kb::render::RenderMaterialGraphNodeKindName(uniform.kind) },
            });
        }
        for (const kb::render::RenderMaterialGraphReflectionTexture& texture : reflection.textures) {
            model.reflectionRows.push_back(MaterialEditorShaderReflectionRow{
                .category = "texture",
                .name = texture.samplerName,
                .stableId = texture.stableId,
                .detail = "slot " + std::to_string(texture.slot) + " " +
                    TextureDimensionName(texture.dimension) + " " +
                    TextureColorSpaceName(texture.colorSpace) + " " +
                    SamplerStateSummary(texture.samplerState),
            });
        }
        for (const std::string& varying : reflection.requiredVaryings) {
            model.reflectionRows.push_back(MaterialEditorShaderReflectionRow{
                .category = "varying",
                .name = varying,
                .detail = "required",
            });
        }
        if (reflection.usesSceneColor) {
            model.reflectionRows.push_back(MaterialEditorShaderReflectionRow{
                .category = "sceneTexture",
                .name = "s_kbSceneColor",
                .detail = "slot 4 color snapshot",
            });
        }
        if (reflection.usesSceneDepth) {
            model.reflectionRows.push_back(MaterialEditorShaderReflectionRow{
                .category = "sceneTexture",
                .name = "s_kbSceneDepth",
                .detail = "slot 5 opaque depth",
            });
        }
        if (reflection.hasWorldPositionOffset) {
            model.reflectionRows.push_back(MaterialEditorShaderReflectionRow{
                .category = "vertex",
                .name = "WorldPositionOffset",
                .detail = "generated vertex shader required",
            });
        }
        if (reflection.hasCustomizedUv0) {
            model.reflectionRows.push_back(MaterialEditorShaderReflectionRow{
                .category = "vertex",
                .name = "CustomizedUV0",
                .detail = "generated vertex shader required",
            });
        }
        if (reflection.hasDisplacement) {
            model.reflectionRows.push_back(MaterialEditorShaderReflectionRow{
                .category = "vertex",
                .name = "Displacement",
                .detail = "generated vertex shader required; tessellation is non-production",
            });
        }
        return model;
    }

    [[nodiscard]] static char LowerAscii(char ch) noexcept {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return ch;
    }

    [[nodiscard]] static bool ContainsCaseInsensitive(std::string_view text, std::string_view query) noexcept {
        if (query.empty()) {
            return true;
        }
        if (query.size() > text.size()) {
            return false;
        }
        for (std::size_t offset = 0U; offset + query.size() <= text.size(); ++offset) {
            bool matched = true;
            for (std::size_t index = 0U; index < query.size(); ++index) {
                if (LowerAscii(text[offset + index]) != LowerAscii(query[index])) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static bool AnyFindFieldMatches(
        std::string_view query,
        std::initializer_list<std::string_view> fields) noexcept {
        for (std::string_view field : fields) {
            if (ContainsCaseInsensitive(field, query)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static std::string NodeFindLabel(const kb::render::RenderMaterialGraphNode& node) {
        const std::string stableId = StableIdForGraphNode(node);
        return DisplayNameOrStableId(node.parameter.displayName, stableId);
    }

    [[nodiscard]] static std::int32_t NodeFindFocusX(const kb::render::RenderMaterialGraphNode& node) noexcept {
        return node.positionX + 120;
    }

    [[nodiscard]] static std::int32_t NodeFindFocusY(const kb::render::RenderMaterialGraphNode& node) noexcept {
        return node.positionY + 80;
    }

    void RefreshFindResults() {
        findResults_.clear();
        if (!workingCopy_.has_value() || findQuery_.empty()) {
            return;
        }
        const kb::render::RenderMaterialGraphDocument& graph = workingCopy_->graph;
        for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) {
            const std::string kindName{ kb::render::RenderMaterialGraphNodeKindName(node.kind) };
            const std::string label = NodeFindLabel(node);
            const std::string stableId = StableIdForGraphNode(node);
            if (AnyFindFieldMatches(findQuery_, { kindName, label, stableId, node.parameter.description })) {
                findResults_.push_back(MaterialEditorFindResult{
                    .kind = MaterialEditorFindResultKind::Node,
                    .nodeId = node.id,
                    .label = label,
                    .detail = "node " + kindName,
                    .focusX = NodeFindFocusX(node),
                    .focusY = NodeFindFocusY(node),
                });
            }
            if (AnyFindFieldMatches(
                    findQuery_,
                    { node.parameter.stableId, node.parameter.displayName, node.parameter.defaultValueHint, node.parameter.description })) {
                findResults_.push_back(MaterialEditorFindResult{
                    .kind = MaterialEditorFindResultKind::Parameter,
                    .nodeId = node.id,
                    .label = label,
                    .detail = "parameter " + stableId,
                    .focusX = NodeFindFocusX(node),
                    .focusY = NodeFindFocusY(node),
                });
            }
        }

        kb::render::RenderMaterialGraphBuildContext context{};
        const kb::render::RenderMaterialGraphIrBuildResult ir = kb::render::BuildRenderMaterialGraphIr(graph, context);
        if (ir.Succeeded()) {
            for (const kb::render::RenderMaterialGraphIrNode& irNode : ir.ir.nodes) {
                const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(graph, irNode.nodeId);
                if (node == nullptr) {
                    continue;
                }
                const std::string label = NodeFindLabel(*node);
                for (const kb::render::RenderMaterialGraphIrPin& pin : irNode.inputs) {
                    if (ContainsCaseInsensitive(pin.name, findQuery_)) {
                        findResults_.push_back(MaterialEditorFindResult{
                            .kind = MaterialEditorFindResultKind::Pin,
                            .nodeId = node->id,
                            .label = label + "." + pin.name,
                            .detail = "input pin",
                            .focusX = NodeFindFocusX(*node),
                            .focusY = NodeFindFocusY(*node),
                        });
                    }
                }
                for (const kb::render::RenderMaterialGraphIrPin& pin : irNode.outputs) {
                    if (ContainsCaseInsensitive(pin.name, findQuery_)) {
                        findResults_.push_back(MaterialEditorFindResult{
                            .kind = MaterialEditorFindResultKind::Pin,
                            .nodeId = node->id,
                            .label = label + "." + pin.name,
                            .detail = "output pin",
                            .focusX = NodeFindFocusX(*node),
                            .focusY = NodeFindFocusY(*node),
                        });
                    }
                }
            }
        }

        for (const kb::render::RenderMaterialGraphCommentBox& comment : graph.comments) {
            if (ContainsCaseInsensitive(comment.text, findQuery_)) {
                findResults_.push_back(MaterialEditorFindResult{
                    .kind = MaterialEditorFindResultKind::Comment,
                    .commentId = comment.id,
                    .label = comment.text.empty() ? std::string{ "Comment" } : comment.text,
                    .detail = "comment",
                    .focusX = comment.positionX + (std::max<std::int32_t>(1, comment.width) / 2),
                    .focusY = comment.positionY + (std::max<std::int32_t>(1, comment.height) / 2),
                });
            }
        }
    }

    // Everything a document change makes void. Cheap - it is all clears - so an edit still pays for it.
    void ClearDerivedGraphDiagnostics() {
        graphDiagnosticsLines_.clear();
        compilerDiagnostics_.clear();
        graphDiagnosticMarkers_.clear();
        graphDiagnosticsHaveError_ = false;
        compilerDiagnosticsHaveError_ = false;
        ResetCookDiagnostics();
        materialStats_ = {};
        shaderViewer_ = {};
    }

    // An edit invalidates the diagnostics; it does not recompute them. Recomputing meant running the graph
    // validator AND a full shader compile on the UI thread for every single edit - a mouse-move during a
    // drag, a keystroke in an inline value - even though nothing reads the result until the panel repaints.
    // The stale results are cleared immediately, so nothing on screen describes a document that no longer
    // exists; the expensive part happens on the next read.
    void InvalidateGraphDiagnostics() {
        ClearDerivedGraphDiagnostics();
        graphDiagnosticsStale_ = true;
        graphRuntimeState_ = kb::render::RenderMaterialGraphRuntimeState::Dirty;
        RebuildMergedDiagnostics();
    }

    // Anything that reads diagnostic state calls this first, and so does anything that pushes cook or
    // external results IN - otherwise a deferred rebuild could land afterwards and wipe a fresh result.
    void EnsureGraphDiagnostics() const {
        if (!graphDiagnosticsStale_) {
            return;
        }
        graphDiagnosticsStale_ = false;
        const_cast<MaterialEditorState*>(this)->RefreshGraphDiagnostics();
    }

    void RefreshGraphDiagnostics() {
        graphDiagnosticsStale_ = false;
        ClearDerivedGraphDiagnostics();
        if (!workingCopy_.has_value()) {
            graphRuntimeState_ = kb::render::RenderMaterialGraphRuntimeState::Dirty;
            RebuildMergedDiagnostics();
            return;
        }
        const std::vector<kb::render::RenderMaterialGraphDiagnostic> graphDiagnostics = kb::render::ValidateRenderMaterialAssetGraphDiagnostics(*workingCopy_);
        graphDiagnosticsLines_.reserve(graphDiagnostics.size());
        graphDiagnosticMarkers_.reserve(graphDiagnostics.size());
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : graphDiagnostics) {
            graphDiagnosticsLines_.push_back("[validator] " + GraphDiagnosticLine(diagnostic));
            if (diagnostic.nodeId != 0U) {
                graphDiagnosticMarkers_.push_back(MaterialEditorGraphDiagnosticMarker{
                    .nodeId = diagnostic.nodeId,
                    .linkId = diagnostic.linkId,
                    .pinId = diagnostic.pinId,
                    .pin = diagnostic.pin,
                    .severity = diagnostic.severity,
                    .kind = diagnostic.kind,
                    .message = diagnostic.message,
                });
            }
            if (diagnostic.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error) {
                graphDiagnosticsHaveError_ = true;
            }
        }
        const bool valid = !graphDiagnosticsHaveError_;
        bool compileSucceeded = false;
        if (valid) {
            kb::render::RenderMaterialGraphBuildContext context{};
            context.assetId = openAssetId_.value;
            const kb::render::RenderMaterialGraphCompileResult compile =
                kb::render::CompileRenderMaterialGraphToShaderSource(workingCopy_->graph, context);
            compileSucceeded = compile.Succeeded();
            for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : compile.diagnostics) {
                compilerDiagnostics_.push_back("[compiler] " + GraphDiagnosticLine(diagnostic));
                compilerDiagnosticsHaveError_ = compilerDiagnosticsHaveError_ ||
                    diagnostic.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error;
                if (diagnostic.nodeId != 0U) {
                    graphDiagnosticMarkers_.push_back(MaterialEditorGraphDiagnosticMarker{
                        .nodeId = diagnostic.nodeId,
                        .linkId = diagnostic.linkId,
                        .pinId = diagnostic.pinId,
                        .pin = diagnostic.pin,
                        .severity = diagnostic.severity,
                        .kind = diagnostic.kind,
                        .message = diagnostic.message,
                    });
                }
            }
            materialStats_ = {};
            materialStats_.sourceHash = compile.shader.sourceHash;
            materialStats_.warnings.push_back("Awaiting cooked GPU pass telemetry.");
            shaderViewer_ = BuildShaderViewer(compile);
        }
        localCompileSucceeded_ = valid && compileSucceeded;
        RefreshGraphRuntimeState();
        RebuildMergedDiagnostics();
    }

    void ResetCookDiagnostics() {
        cookDiagnostics_.clear();
        cookCompleted_ = false;
        cookSucceeded_ = false;
        cookHasGpuProgram_ = false;
        cookHasLastGood_ = false;
        cookFallbackApplied_ = false;
        localCompileSucceeded_ = false;
    }

    void RefreshGraphRuntimeState() {
        if (!workingCopy_.has_value()) {
            graphRuntimeState_ = kb::render::RenderMaterialGraphRuntimeState::Dirty;
            return;
        }
        graphRuntimeState_ = kb::render::ResolveRenderMaterialGraphRuntimeState(kb::render::RenderMaterialGraphRuntimeStateInput{
            .phase = cookCompleted_ || !localCompileSucceeded_
                ? kb::render::RenderMaterialGraphCompilePhase::Compiled
                : kb::render::RenderMaterialGraphCompilePhase::Compiling,
            .validationSucceeded = !graphDiagnosticsHaveError_,
            .compileSucceeded = localCompileSucceeded_ && cookSucceeded_,
            .hasGpuProgram = cookHasGpuProgram_,
            .hasLastGood = cookHasLastGood_,
            .fallbackApplied = cookCompleted_ ? cookFallbackApplied_ : !localCompileSucceeded_,
            .failurePolicy = workingCopy_->graph.artifactFailurePolicy,
        });
    }

    void RebuildMergedDiagnostics() {
        diagnostics_.clear();
        const auto append = [this](const std::vector<std::string>& source) {
            diagnostics_.insert(diagnostics_.end(), source.begin(), source.end());
        };
        append(graphDiagnosticsLines_);
        append(compilerDiagnostics_);
        append(externalDiagnostics_);
        append(cookDiagnostics_);
        diagnosticsHaveError_ = graphDiagnosticsHaveError_ || compilerDiagnosticsHaveError_ ||
            externalDiagnosticsHaveError_ || (!cookSucceeded_ && cookCompleted_);
    }

    // Derived data is invalidated on edit and rebuilt when something actually reads it.
    //
    // Rebuilding eagerly inside SetWorkingCopy meant every edit paid for the parameter list and the find
    // results whether or not anyone was looking - and during a drag or an inline-value edit nobody is: the
    // Inspector reads the parameters once per repaint, the find panel only while a query is open. The
    // rebuild bodies below are unchanged; only when they run has moved.
    void InvalidateParameters() noexcept { parametersStale_ = true; }
    void InvalidateFindResults() noexcept { findResultsStale_ = true; }

    void EnsureParameters() const {
        if (!parametersStale_) {
            return;
        }
        parametersStale_ = false;
        const_cast<MaterialEditorState*>(this)->RefreshParameters();
    }

    void EnsureFindResults() const {
        if (!findResultsStale_) {
            return;
        }
        findResultsStale_ = false;
        const_cast<MaterialEditorState*>(this)->RefreshFindResults();
    }

    void RefreshParameters() {
        parameters_.clear();
        if (!workingCopy_.has_value()) {
            return;
        }
        if (instanceWorkingCopy_.has_value() && instanceParentSnapshot_.has_value()) {
            parameters_ = BuildInstanceParameters(*workingCopy_, activeSchema_, *instanceParentSnapshot_, *instanceWorkingCopy_);
            return;
        }
        parameters_ = BuildParameters(*workingCopy_, activeSchema_);
    }

    // The clean snapshot only changes on open/save/revert, so its canonical text is worth keeping: a dirty
    // check then serializes the working copy alone instead of both documents. Rebuilt lazily, because the
    // change and the next check are not always adjacent.
    void InvalidateCleanCanonical() noexcept {
        cleanCanonicalValid_ = false;
        cleanCanonical_.clear();
    }

    [[nodiscard]] const std::string& CleanCanonical() const {
        if (!cleanCanonicalValid_) {
            cleanCanonical_ = cleanSnapshot_.has_value() ? CanonicalDocument(*cleanSnapshot_) : std::string{};
            cleanCanonicalValid_ = true;
        }
        return cleanCanonical_;
    }

    // Same answer EquivalentDocument(workingCopy_, cleanSnapshot_) gave - byte-identical canonical forms -
    // reached without building either string on the hot path.
    [[nodiscard]] bool WorkingCopyMatchesCleanSnapshot() const {
        if (workingCopy_.has_value() != cleanSnapshot_.has_value()) {
            return false;
        }
        if (!workingCopy_.has_value()) {
            return true;
        }
        const kb::render::RenderMaterialAssetData& document = *workingCopy_;
        return CanonicalTextEquals(CleanCanonical(), [&document](std::ostream& output) {
            kb::render::RenderMaterialAssetWriter::Write(output, document);
        });
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

    [[nodiscard]] static std::string CanonicalInstance(const kb::render::RenderMaterialInstanceAssetData& document) {
        std::ostringstream output;
        kb::render::RenderMaterialInstanceAssetWriter::Write(output, document);
        return output.str();
    }

    [[nodiscard]] static bool EquivalentInstance(
        const std::optional<kb::render::RenderMaterialInstanceAssetData>& lhs,
        const std::optional<kb::render::RenderMaterialInstanceAssetData>& rhs) {
        if (lhs.has_value() != rhs.has_value()) {
            return false;
        }
        if (!lhs.has_value()) {
            return true;
        }
        return CanonicalInstance(*lhs) == CanonicalInstance(*rhs);
    }

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
    std::vector<std::string> graphDiagnosticsLines_;
    std::vector<std::string> compilerDiagnostics_;
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
};

} // namespace kb::editor
