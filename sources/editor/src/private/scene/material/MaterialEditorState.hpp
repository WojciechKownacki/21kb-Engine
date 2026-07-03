#pragma once

#include "inspection/InspectorPanelState.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"
#include "engine/assets/AssetId.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::editor {

enum class MaterialEditorParameterValueKind : std::uint8_t {
    None,
    Scalar,
    Vec2,
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

struct MaterialEditorGraphDiagnosticMarker {
    std::uint32_t nodeId = 0U;
    std::uint32_t linkId = 0U;
    std::uint32_t pinId = 0U;
    std::string pin;
    kb::render::RenderMaterialGraphDiagnosticSeverity severity = kb::render::RenderMaterialGraphDiagnosticSeverity::Error;
    kb::render::RenderMaterialGraphDiagnosticKind kind = kb::render::RenderMaterialGraphDiagnosticKind::UnsupportedNode;
    std::string message;
};

enum class MaterialEditorGraphMenuCommand : std::uint8_t {
    None,
    CreateTextureSample,
    CreateTextureParameter,
    CreateTextureObject,
    CreateTextureSampleCube,
    CreateTextureObjectCube,
    CreateTextureSampleVolume,
    CreateTextureObjectVolume,
    CreateTextureSample2DArray,
    CreateTextureObject2DArray,
    CreateUv,
    CreateScalar,
    CreateBool,
    CreateVector2,
    CreateVector,
    CreateColor,
    CreateScalarParameter,
    CreateVectorParameter,
    CreateColorParameter,
    CreateCollectionParameter,
    CreateAdd,
    CreateSubtract,
    CreateMultiply,
    CreateDivide,
    CreatePower,
    CreateOneMinus,
    CreateAbsolute,
    CreateMinimum,
    CreateMaximum,
    CreateSaturate,
    CreateFloor,
    CreateCeil,
    CreateFraction,
    CreateSquareRoot,
    CreateSine,
    CreateCosine,
    CreateExponential,
    CreateExponential2,
    CreateLogarithm,
    CreateLogarithm2,
    CreateSrgbToLinear,
    CreateLinearToSrgb,
    CreateLogarithm10,
    CreateHsvToRgb,
    CreateRgbToHsv,
    CreateDeriveNormalZ,
    CreateFmod,
    CreateInverseLerp,
    CreatePartialDerivativeX,
    CreatePartialDerivativeY,
    CreateSphereMask,
    CreateBlackBody,
    CreateNoise,
    CreateVectorNoise,
    CreateSobol,
    CreateAppendVector,
    CreateColorRamp,
    CreateAntialiasedTextureMask,
    CreateTransform,
    CreateTransformPosition,
    CreateDotProduct,
    CreateCrossProduct,
    CreateNormalize,
    CreateLength,
    CreateDistance,
    CreateBreakVector,
    CreateMakeVector,
    CreateStep,
    CreateSmoothStep,
    CreateIf,
    CreateSwitch,
    CreateDesaturate,
    CreateFresnel,
    CreateNegate,
    CreateSign,
    CreateRound,
    CreateTruncate,
    CreateTangent,
    CreateArcSine,
    CreateArcCosine,
    CreateArcTangent,
    CreateArcTangent2,
    CreateArcSineFast,
    CreateArcCosineFast,
    CreateArcTangentFast,
    CreateArcTangent2Fast,
    CreateClamp,
    CreateLerp,
    CreateNormalUnpack,
    CreateTime,
    CreateDeltaTime,
    CreateDynamicParameter,
    CreateVertexColor,
    CreateScreenPosition,
    CreateLocalPosition,
    CreateObjectPosition,
    CreateWorldPosition,
    CreatePerInstanceRandom,
    CreatePerInstanceFadeAmount,
    CreatePerInstanceCustomData,
    CreateObjectRadius,
    CreateObjectBounds,
    CreateObjectOrientation,
    CreatePreSkinnedPosition,
    CreatePreSkinnedNormal,
    CreateMakeMaterialAttributes,
    CreateBreakMaterialAttributes,
    CreateBlendMaterialAttributes,
    CreateGetMaterialAttributes,
    CreateSetMaterialAttributes,
    CreateStaticBoolParameter,
    CreateStaticSwitch,
    CreateStaticComponentMask,
    CreateQualitySwitch,
    CreateFeatureLevelSwitch,
    CreateShadingPathSwitch,
    CreateShaderStageSwitch,
    CreateTextureCoordinate,
    CreatePanner,
    CreateRotator,
    CreateBumpOffset,
    CreateConstantBiasScale,
    CreateRotateAboutAxis,
    CreateViewportUV,
    CreateCameraPosition,
    CreateCameraVector,
    CreateReflectionVector,
    CreateLightVector,
    CreatePixelNormalWS,
    CreateVertexNormalWS,
    CreateVertexTangentWS,
    CreateViewProperty,
    CreateViewSize,
    CreateTwoSidedSign,
    CreateSceneColor,
    CreateSceneTexture,
    CreateSceneDepth,
    CreateDepthFade,
    CreateCustomCode,
    CreateReroute,
    CreateNamedRerouteDeclaration,
    CreateNamedRerouteUsage,
    CreateCompositeInput,
    CreateCompositeOutput,
    CreateFunctionInput,
    CreateFunctionOutput,
    CreateMaterialFunctionCall,
    CreateLayerStack,
    CreateComposite,
    CreateComment,
    FrameSelected,
    SelectUpstream,
    SelectDownstream,
    AlignLeft,
    AlignCenter,
    AlignRight,
    AlignTop,
    AlignMiddle,
    AlignBottom,
    DistributeHorizontal,
    DistributeVertical,
    PromoteToParameter,
    DisconnectSelected,
    DeleteSelected,
};

enum class MaterialEditorGraphAlignMode : std::uint8_t {
    Left,
    Center,
    Right,
    Top,
    Middle,
    Bottom,
};

enum class MaterialEditorGraphDistributeAxis : std::uint8_t {
    Horizontal,
    Vertical,
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
    bool overrideActive = false;
    bool enabled = true;
    std::uint32_t sortOrder = 0U;
};

enum class MaterialEditorGraphNodePropertyKind : std::uint8_t {
    Text,
    Numeric,
    Color,
    Enum,
    TextureAsset,
};

struct MaterialEditorGraphNodePropertyOption {
    std::string value;
    std::string label;
};

struct MaterialEditorGraphNodeProperty {
    std::uint32_t nodeId = 0U;
    std::string stableId;
    std::string displayName;
    MaterialEditorGraphNodePropertyKind kind = MaterialEditorGraphNodePropertyKind::Numeric;
    kb::render::RenderMaterialParameterType type = kb::render::RenderMaterialParameterType::Scalar;
    MaterialEditorParameterValue value{};
    std::optional<kb::render::RenderMaterialParameterRange> range;
    std::vector<MaterialEditorGraphNodePropertyOption> options;
    std::size_t componentIndex = 0U;
    bool dropdownOpen = false;
    bool enabled = true;
};

struct MaterialEditorInstanceParentChainRow {
    kb::assets::AssetId assetId{};
    std::string label;
    bool current = false;
};

struct MaterialEditorInstanceOverrideGroupRow {
    MaterialEditorParameterGroup group = MaterialEditorParameterGroup::Core;
    bool expanded = true;
    std::uint32_t activeOverrideCount = 0U;
    std::uint32_t totalParameterCount = 0U;
    std::vector<MaterialEditorParameter> parameters;
};

struct MaterialEditorInstanceStaticSwitchRow {
    std::uint32_t nodeId = 0U;
    std::string stableId;
    std::string displayName;
    kb::render::RenderMaterialGraphNodeKind nodeKind = kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter;
    std::string parentValue;
    std::string value;
    bool overrideActive = false;
};

struct MaterialEditorLayerTreeRow {
    std::uint32_t nodeId = 0U;
    std::size_t index = 0U;
    bool enabled = true;
    std::uint64_t layerFunctionAssetId = 0U;
    std::uint64_t blendFunctionAssetId = 0U;
    std::string layerName;
    std::string blendName;
    std::string linkState;
    std::uint32_t layerParameterCount = 0U;
    std::uint32_t blendParameterCount = 0U;
};

struct MaterialEditorMaterialStatsPassRow {
    std::string passName;
    bool graphProgram = false;
    std::uint32_t instructionEstimate = 0U;
    std::uint32_t textureSampleCount = 0U;
    std::uint32_t samplerCount = 0U;
    std::uint32_t uniformCount = 0U;
    std::uint32_t varyingCount = 0U;
    std::uint32_t staticVariantCount = 1U;
    std::vector<std::string> warnings;
};

struct MaterialEditorMaterialStatsModel {
    bool available = false;
    std::uint64_t sourceHash = 0U;
    std::vector<MaterialEditorMaterialStatsPassRow> passRows;
    std::vector<std::string> warnings;
};

struct MaterialEditorShaderSourceView {
    std::string passName;
    std::string backendName;
    std::string stageName;
    std::string source;
    std::uint64_t sourceHash = 0U;
};

struct MaterialEditorShaderReflectionRow {
    std::string category;
    std::string name;
    std::string stableId;
    std::string detail;
};

struct MaterialEditorShaderViewerModel {
    bool available = false;
    std::uint64_t sourceHash = 0U;
    std::uint64_t reflectionHash = 0U;
    std::vector<MaterialEditorShaderSourceView> sources;
    std::vector<MaterialEditorShaderReflectionRow> reflectionRows;
    std::vector<std::string> warnings;
};

enum class MaterialEditorFindResultKind : std::uint8_t {
    Node,
    Pin,
    Parameter,
    Comment,
};

struct MaterialEditorFindResult {
    MaterialEditorFindResultKind kind = MaterialEditorFindResultKind::Node;
    std::uint32_t nodeId = 0U;
    std::uint32_t commentId = 0U;
    std::string label;
    std::string detail;
    std::int32_t focusX = 0;
    std::int32_t focusY = 0;
};

struct MaterialEditorFindFocusTarget {
    std::int32_t graphX = 0;
    std::int32_t graphY = 0;
};

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

    [[nodiscard]] const std::vector<std::string>& Diagnostics() const noexcept {
        return diagnostics_;
    }

    [[nodiscard]] bool DiagnosticsHaveError() const noexcept {
        return diagnosticsHaveError_;
    }

    [[nodiscard]] const std::vector<MaterialEditorGraphDiagnosticMarker>& GraphDiagnosticMarkers() const noexcept {
        return graphDiagnosticMarkers_;
    }

    [[nodiscard]] kb::render::RenderMaterialGraphRuntimeState GraphRuntimeState() const noexcept {
        return graphRuntimeState_;
    }

    [[nodiscard]] std::string_view GraphRuntimeStateName() const noexcept {
        return kb::render::RenderMaterialGraphRuntimeStateName(graphRuntimeState_);
    }

    [[nodiscard]] const MaterialEditorMaterialStatsModel& MaterialStats() const noexcept {
        return materialStats_;
    }

    [[nodiscard]] const MaterialEditorShaderViewerModel& ShaderViewer() const noexcept {
        return shaderViewer_;
    }

    [[nodiscard]] std::string_view FindQuery() const noexcept {
        return findQuery_;
    }

    [[nodiscard]] const std::vector<MaterialEditorFindResult>& FindResults() const noexcept {
        return findResults_;
    }

    void SetFindQuery(std::string query) {
        findQuery_ = std::move(query);
        RefreshFindResults();
    }

    void AppendFindText(wchar_t character) {
        if (character < 0x20) {
            return;
        }
        findQuery_.push_back(static_cast<char>(character));
        RefreshFindResults();
    }

    void InsertFindText(std::string_view text) {
        for (const char character : text) {
            if (static_cast<unsigned char>(character) >= 0x20U) {
                findQuery_.push_back(character);
            }
        }
        RefreshFindResults();
    }

    void BackspaceFind() {
        if (findQuery_.empty()) {
            return;
        }
        findQuery_.pop_back();
        RefreshFindResults();
    }

    void ClearFindQuery() {
        findQuery_.clear();
        findResults_.clear();
    }

    [[nodiscard]] std::optional<MaterialEditorFindFocusTarget> FindResultFocusTarget(std::size_t index) const noexcept {
        if (index >= findResults_.size()) {
            return std::nullopt;
        }
        return MaterialEditorFindFocusTarget{
            .graphX = findResults_[index].focusX,
            .graphY = findResults_[index].focusY,
        };
    }

    [[nodiscard]] bool FocusFindResult(std::size_t index) {
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

    [[nodiscard]] const std::vector<MaterialEditorParameter>& Parameters() const noexcept {
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
            for (const MaterialEditorParameter& parameter : parameters_) {
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

    [[nodiscard]] bool IsGraphConstantInlineEditing() const noexcept {
        return inlineConstantEditNodeId_ != 0U;
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
        if (node == nullptr || !IsGraphConstantNode(node->kind) || componentIndex >= ConstantComponentCount(node->kind)) {
            return std::nullopt;
        }
        return ConstantRange(*node);
    }

    [[nodiscard]] bool SetGraphConstantComponentValue(std::uint32_t nodeId, std::size_t componentIndex, float componentValue) {
        if (!workingCopy_.has_value() || nodeId == 0U) {
            return false;
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
        if (!workingCopy_.has_value() || nodeId == 0U) {
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
        node->parameter.defaultValueHint = option->value;
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

        if (node->kind == kb::render::RenderMaterialGraphNodeKind::TextureSample ||
            node->kind == kb::render::RenderMaterialGraphNodeKind::ParameterTexture ||
            node->kind == kb::render::RenderMaterialGraphNodeKind::TextureObject) {
            properties.push_back(MaterialEditorGraphNodeProperty{
                .nodeId = node->id,
                .stableId = "texture.asset",
                .displayName = node->parameter.displayName.empty() ? std::string{ "Texture" } : node->parameter.displayName,
                .kind = MaterialEditorGraphNodePropertyKind::TextureAsset,
                .type = kb::render::RenderMaterialParameterType::Texture,
                .value = TextureAssetValue(GraphNodeTextureAssetId(*node, *workingCopy_)),
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
        return true;
    }

    [[nodiscard]] bool MoveGraphCommentGroup(std::uint32_t commentId, std::int32_t positionX, std::int32_t positionY) {
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

        std::vector<std::uint32_t> containedNodeIds;
        for (const kb::render::RenderMaterialGraphNode& node : workingCopy_->graph.nodes) {
            if (GraphNodeInsideComment(node, *comment)) {
                containedNodeIds.push_back(node.id);
            }
        }
        comment->positionX = positionX;
        comment->positionY = positionY;
        for (kb::render::RenderMaterialGraphNode& node : workingCopy_->graph.nodes) {
            if (std::ranges::find(containedNodeIds, node.id) != containedNodeIds.end()) {
                node.positionX += deltaX;
                node.positionY += deltaY;
            }
        }
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
        openAssetId_ = assetId;
        instanceParentSnapshot_ = document;
        instanceWorkingCopy_ = std::move(instanceDocument);
        instanceCleanSnapshot_ = instanceWorkingCopy_;
        if (document.has_value() && instanceWorkingCopy_.has_value()) {
            workingCopy_ = kb::render::BuildEffectiveRenderMaterialInstanceAsset(*document, *instanceWorkingCopy_);
            cleanSnapshot_ = workingCopy_;
        } else {
            workingCopy_ = document;
            cleanSnapshot_ = std::move(document);
            instanceParentSnapshot_.reset();
        }
        activeSchema_ = schema.has_value() && !schema->typeName.empty()
            ? std::move(*schema)
            : kb::render::GetBuiltInPbrMaterialTypeSchema();
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
        RefreshParameters();
        RefreshGraphDiagnostics();
    }

    void Close() noexcept {
        openAssetId_ = {};
        workingCopy_.reset();
        cleanSnapshot_.reset();
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
        diagnostics_.clear();
        graphDiagnosticMarkers_.clear();
        diagnosticsHaveError_ = false;
        materialStats_ = {};
        shaderViewer_ = {};
    }

    void SetWorkingCopy(kb::render::RenderMaterialAssetData document) {
        workingCopy_ = std::move(document);
        dirty_ = !EquivalentDocument(workingCopy_, cleanSnapshot_);
        RefreshParameters();
        RefreshGraphDiagnostics();
        RefreshFindResults();
        PruneSelectionToWorkingCopy();
    }

    void SetInstanceWorkingCopy(
        kb::render::RenderMaterialInstanceAssetData instanceDocument,
        kb::render::RenderMaterialAssetData effectiveDocument) {
        instanceWorkingCopy_ = std::move(instanceDocument);
        workingCopy_ = std::move(effectiveDocument);
        dirty_ = !EquivalentInstance(instanceWorkingCopy_, instanceCleanSnapshot_) ||
            !EquivalentDocument(workingCopy_, cleanSnapshot_);
        RefreshParameters();
        RefreshGraphDiagnostics();
        RefreshFindResults();
        PruneSelectionToWorkingCopy();
    }

    void MarkSaved() {
        cleanSnapshot_ = workingCopy_;
        if (instanceWorkingCopy_.has_value()) {
            instanceCleanSnapshot_ = instanceWorkingCopy_;
        }
        dirty_ = false;
    }

    void RevertToCleanSnapshot() {
        if (instanceWorkingCopy_.has_value() || instanceCleanSnapshot_.has_value()) {
            instanceWorkingCopy_ = instanceCleanSnapshot_;
            if (instanceParentSnapshot_.has_value() && instanceWorkingCopy_.has_value()) {
                workingCopy_ = kb::render::BuildEffectiveRenderMaterialInstanceAsset(*instanceParentSnapshot_, *instanceWorkingCopy_);
                cleanSnapshot_ = workingCopy_;
            } else {
                workingCopy_ = cleanSnapshot_;
            }
        } else {
            workingCopy_ = cleanSnapshot_;
        }
        dirty_ = false;
        RefreshParameters();
        RefreshGraphDiagnostics();
        PruneSelectionToWorkingCopy();
    }

    bool SelectNode(std::uint32_t nodeId) {
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
        diagnostics_ = std::move(diagnostics);
        graphDiagnosticMarkers_.clear();
        diagnosticsHaveError_ = hasError;
    }

    void ClearDiagnostics() {
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
        std::istringstream input{ std::string{ text } };
        float value = 0.0F;
        while (input >> value) {
            values.push_back(value);
        }
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
            return "RGB";
        case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
            return "RGBA";
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
        std::string normalized{ text };
        std::replace(normalized.begin(), normalized.end(), ',', ' ');
        std::istringstream input{ normalized };
        std::array<float, 4U> value{ 0.0F, 0.0F, 0.0F, 1.0F };
        switch (kind) {
        case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
            if (input >> value[0]) {
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
            if (input >> value[0] >> value[1]) {
                return value;
            }
            return std::nullopt;
        case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
            if (input >> value[0] >> value[1] >> value[2]) {
                return value;
            }
            return std::nullopt;
        case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
            if (input >> value[0] >> value[1] >> value[2]) {
                if (!(input >> value[3])) {
                    value[3] = 1.0F;
                }
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
        if (node.parameter.defaultValueHint == "1" || node.parameter.defaultValueHint == "uv1" || node.parameter.defaultValueHint == "UV1") {
            return "1";
        }
        return "0";
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
                .displayName = "RGB",
                .defaultValueHint = "0 0 0",
                .overrideSupported = false,
            };
        case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
            return kb::render::RenderMaterialGraphParameterMetadata{
                .displayName = "RGBA",
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
                .defaultValueHint = "0",
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
        case kb::render::RenderMaterialGraphNodeKind::SphereMask:
        case kb::render::RenderMaterialGraphNodeKind::BlackBody:
        case kb::render::RenderMaterialGraphNodeKind::Noise:
        case kb::render::RenderMaterialGraphNodeKind::VectorNoise:
        case kb::render::RenderMaterialGraphNodeKind::Sobol:
        case kb::render::RenderMaterialGraphNodeKind::AppendVector:
        case kb::render::RenderMaterialGraphNodeKind::ColorRamp:
        case kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask:
        case kb::render::RenderMaterialGraphNodeKind::Transform:
        case kb::render::RenderMaterialGraphNodeKind::TransformPosition:
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
        case kb::render::RenderMaterialGraphNodeKind::Desaturate:
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

    [[nodiscard]] static std::uint32_t CountMaterialStatsOccurrences(std::string_view text, std::string_view needle) noexcept {
        if (needle.empty()) {
            return 0U;
        }
        std::uint32_t count = 0U;
        std::size_t offset = 0U;
        while ((offset = text.find(needle, offset)) != std::string_view::npos) {
            ++count;
            offset += needle.size();
        }
        return count;
    }

    [[nodiscard]] static std::uint32_t EstimateMaterialShaderInstructions(std::string_view source) noexcept {
        std::uint32_t estimate = 0U;
        std::size_t lineStart = 0U;
        while (lineStart < source.size()) {
            const std::size_t lineEnd = source.find('\n', lineStart);
            const std::string_view line = lineEnd == std::string_view::npos
                ? source.substr(lineStart)
                : source.substr(lineStart, lineEnd - lineStart);
            const std::size_t first = line.find_first_not_of(" \t\r");
            if (first != std::string_view::npos) {
                const std::string_view trimmed = line.substr(first);
                const bool declaration =
                    trimmed.rfind("SAMPLER", 0U) == 0U ||
                    trimmed.rfind("uniform", 0U) == 0U ||
                    trimmed.rfind("struct ", 0U) == 0U ||
                    trimmed.rfind("//", 0U) == 0U;
                if (!declaration &&
                    (trimmed.find('=') != std::string_view::npos ||
                     trimmed.find("return ") != std::string_view::npos ||
                     trimmed.find("texture2D(") != std::string_view::npos ||
                     trimmed.find("textureCube(") != std::string_view::npos ||
                     trimmed.find("texture3D(") != std::string_view::npos ||
                     trimmed.find("texture2DArray(") != std::string_view::npos)) {
                    ++estimate;
                }
            }
            if (lineEnd == std::string_view::npos) {
                break;
            }
            lineStart = lineEnd + 1U;
        }
        return estimate;
    }

    [[nodiscard]] static std::uint32_t CountMaterialShaderTextureSamples(std::string_view source) noexcept {
        return CountMaterialStatsOccurrences(source, "texture2D(") +
            CountMaterialStatsOccurrences(source, "textureCube(") +
            CountMaterialStatsOccurrences(source, "texture3D(") +
            CountMaterialStatsOccurrences(source, "texture2DArray(");
    }

    [[nodiscard]] static std::uint32_t SaturatingMaterialVariantMultiply(std::uint32_t value, std::uint32_t multiplier) noexcept {
        constexpr std::uint32_t kMaxReportedVariants = 4096U;
        if (value >= kMaxReportedVariants || multiplier == 0U) {
            return kMaxReportedVariants;
        }
        if (value > kMaxReportedVariants / multiplier) {
            return kMaxReportedVariants;
        }
        return value * multiplier;
    }

    [[nodiscard]] static std::uint32_t EstimateMaterialVariantCount(const kb::render::RenderMaterialGraphDocument& graph) noexcept {
        std::uint32_t variants = 1U;
        for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) {
            switch (node.kind) {
            case kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter:
            case kb::render::RenderMaterialGraphNodeKind::StaticSwitch:
            case kb::render::RenderMaterialGraphNodeKind::ShaderStageSwitch:
                variants = SaturatingMaterialVariantMultiply(variants, 2U);
                break;
            case kb::render::RenderMaterialGraphNodeKind::StaticComponentMask:
                variants = SaturatingMaterialVariantMultiply(variants, 16U);
                break;
            case kb::render::RenderMaterialGraphNodeKind::QualitySwitch:
                variants = SaturatingMaterialVariantMultiply(variants, 4U);
                break;
            case kb::render::RenderMaterialGraphNodeKind::FeatureLevelSwitch:
            case kb::render::RenderMaterialGraphNodeKind::ShadingPathSwitch:
                variants = SaturatingMaterialVariantMultiply(variants, 3U);
                break;
            default:
                break;
            }
        }
        return variants;
    }

    [[nodiscard]] static std::vector<std::string> MaterialStatsBudgetWarnings(
        const MaterialEditorMaterialStatsPassRow& row) {
        constexpr std::uint32_t kSamplerWarningThreshold = 8U;
        constexpr std::uint32_t kUniformWarningThreshold = 32U;
        constexpr std::uint32_t kVaryingWarningThreshold = 8U;
        constexpr std::uint32_t kInstructionWarningThreshold = 160U;
        constexpr std::uint32_t kVariantWarningThreshold = 16U;

        std::vector<std::string> warnings;
        const std::uint32_t graphSamplerBudget =
            kb::render::kRenderMaterialGraphMaxTextureSamplers - kb::render::kRenderMaterialGraphTextureBaseSlot;
        if (row.samplerCount > kSamplerWarningThreshold) {
            warnings.push_back(
                "Sampler budget high: " + std::to_string(row.samplerCount) + "/" + std::to_string(graphSamplerBudget));
        }
        if (row.uniformCount > kUniformWarningThreshold) {
            warnings.push_back("Uniform budget high: " + std::to_string(row.uniformCount) + "/32");
        }
        if (row.varyingCount > kVaryingWarningThreshold) {
            warnings.push_back("Varying budget high: " + std::to_string(row.varyingCount) + "/8");
        }
        if (row.instructionEstimate > kInstructionWarningThreshold) {
            warnings.push_back("Instruction estimate high: " + std::to_string(row.instructionEstimate) + "/160");
        }
        if (row.staticVariantCount > kVariantWarningThreshold) {
            warnings.push_back("Variant count high: " + std::to_string(row.staticVariantCount) + "/16");
        }
        return warnings;
    }

    [[nodiscard]] static MaterialEditorMaterialStatsModel MaterialStatsCompileFailure(
        const kb::render::RenderMaterialGraphCompileResult& compile) {
        MaterialEditorMaterialStatsModel model{};
        model.warnings.push_back("Material stats unavailable: graph compile failed.");
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : compile.diagnostics) {
            model.warnings.push_back(GraphDiagnosticLine(diagnostic));
        }
        return model;
    }

    [[nodiscard]] static MaterialEditorMaterialStatsModel BuildMaterialStats(
        const kb::render::RenderMaterialAssetData& document,
        const kb::render::RenderMaterialGraphCompileResult& compile) {
        if (!compile.Succeeded()) {
            return MaterialStatsCompileFailure(compile);
        }
        MaterialEditorMaterialStatsModel model{};
        const kb::render::RenderMaterialGraphReflection& reflection = compile.shader.reflection;
        const std::uint32_t textureSampleCount = CountMaterialShaderTextureSamples(compile.shader.source);
        const std::uint32_t sceneSamplerCount =
            (reflection.usesSceneColor ? 1U : 0U) + (reflection.usesSceneDepth ? 1U : 0U);
        const std::uint32_t staticVariantCount = EstimateMaterialVariantCount(document.graph);
        const bool hasVertexDomainOutput =
            reflection.hasWorldPositionOffset || reflection.hasCustomizedUv0 || reflection.hasDisplacement;

        MaterialEditorMaterialStatsPassRow baseRow{
            .passName = kb::render::IsRenderMaterialGraphBlendModeTransparent(reflection.blendMode)
                ? "BaseTransparent"
                : "BaseOpaque",
            .graphProgram = true,
            .instructionEstimate = EstimateMaterialShaderInstructions(compile.shader.source),
            .textureSampleCount = textureSampleCount,
            .samplerCount = static_cast<std::uint32_t>(reflection.textures.size()) + sceneSamplerCount,
            .uniformCount = static_cast<std::uint32_t>(reflection.uniforms.size()),
            .varyingCount = static_cast<std::uint32_t>(reflection.requiredVaryings.size()),
            .staticVariantCount = staticVariantCount,
        };
        baseRow.warnings = MaterialStatsBudgetWarnings(baseRow);

        MaterialEditorMaterialStatsPassRow shadowRow{
            .passName = "ShadowDepth",
            .graphProgram = hasVertexDomainOutput,
            .instructionEstimate = hasVertexDomainOutput ? baseRow.instructionEstimate : 0U,
            .textureSampleCount = hasVertexDomainOutput ? baseRow.textureSampleCount : 0U,
            .samplerCount = hasVertexDomainOutput ? baseRow.samplerCount : 0U,
            .uniformCount = hasVertexDomainOutput ? baseRow.uniformCount : 0U,
            .varyingCount = hasVertexDomainOutput ? baseRow.varyingCount : 0U,
            .staticVariantCount = staticVariantCount,
        };
        shadowRow.warnings = MaterialStatsBudgetWarnings(shadowRow);

        model.available = true;
        model.sourceHash = compile.shader.sourceHash;
        model.passRows.push_back(std::move(baseRow));
        model.passRows.push_back(std::move(shadowRow));
        for (const MaterialEditorMaterialStatsPassRow& row : model.passRows) {
            for (const std::string& warning : row.warnings) {
                model.warnings.push_back(row.passName + ": " + warning);
            }
        }
        return model;
    }

    [[nodiscard]] static MaterialEditorMaterialStatsModel BuildMaterialStats(
        const kb::render::RenderMaterialAssetData& document) {
        kb::render::RenderMaterialGraphBuildContext context{};
        context.assetId = document.materialTypeAssetId;
        const kb::render::RenderMaterialGraphCompileResult compile =
            kb::render::CompileRenderMaterialGraphToShaderSource(document.graph, context);
        return BuildMaterialStats(document, compile);
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

    void RefreshGraphDiagnostics() {
        diagnostics_.clear();
        graphDiagnosticMarkers_.clear();
        diagnosticsHaveError_ = false;
        materialStats_ = {};
        shaderViewer_ = {};
        if (!workingCopy_.has_value()) {
            graphRuntimeState_ = kb::render::RenderMaterialGraphRuntimeState::Dirty;
            return;
        }
        const std::vector<kb::render::RenderMaterialGraphDiagnostic> graphDiagnostics = kb::render::ValidateRenderMaterialAssetGraphDiagnostics(*workingCopy_);
        diagnostics_.reserve(graphDiagnostics.size());
        graphDiagnosticMarkers_.reserve(graphDiagnostics.size());
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : graphDiagnostics) {
            diagnostics_.push_back(GraphDiagnosticLine(diagnostic));
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
                diagnosticsHaveError_ = true;
            }
        }
        const bool valid = !diagnosticsHaveError_;
        if (valid) {
            kb::render::RenderMaterialGraphBuildContext context{};
            context.assetId = workingCopy_->materialTypeAssetId;
            const kb::render::RenderMaterialGraphCompileResult compile =
                kb::render::CompileRenderMaterialGraphToShaderSource(workingCopy_->graph, context);
            materialStats_ = BuildMaterialStats(*workingCopy_, compile);
            shaderViewer_ = BuildShaderViewer(compile);
        }
        graphRuntimeState_ = kb::render::ResolveRenderMaterialGraphRuntimeState(kb::render::RenderMaterialGraphRuntimeStateInput{
            .phase = kb::render::RenderMaterialGraphCompilePhase::Compiled,
            .validationSucceeded = valid,
            .compileSucceeded = valid,
            .hasGpuProgram = valid,
            .hasLastGood = workingCopy_->graph.lastGoodArtifact.IsValid(),
            .fallbackApplied = true,
            .failurePolicy = workingCopy_->graph.artifactFailurePolicy,
        });
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
    std::vector<MaterialEditorGraphDiagnosticMarker> graphDiagnosticMarkers_;
    bool diagnosticsHaveError_ = false;
    MaterialEditorMaterialStatsModel materialStats_{};
    MaterialEditorShaderViewerModel shaderViewer_{};
    std::string findQuery_;
    std::vector<MaterialEditorFindResult> findResults_;
    bool findFocused_ = false;
    kb::render::RenderMaterialGraphRuntimeState graphRuntimeState_ = kb::render::RenderMaterialGraphRuntimeState::Dirty;
    bool dirty_ = false;
    bool infoPanelVisible_ = false;
    std::uint32_t selectedNodeId_ = 0U;
    std::vector<std::uint32_t> selectedNodeIds_;
    std::uint32_t selectedCommentId_ = 0U;
    InspectorPropertyId selectedParameter_ = InspectorPropertyId::None;
    std::optional<GraphClipboard> graphClipboard_;
    std::uint32_t inlineConstantEditNodeId_ = 0U;
    std::string inlineConstantEditBuffer_;
    std::uint32_t renameNodeId_ = 0U;
    std::string renameBuffer_;
    bool renameSelectAll_ = false;
    std::uint32_t graphNodeEnumDropdownNodeId_ = 0U;
    std::string graphNodeEnumDropdownPropertyId_;
};

} // namespace kb::editor
