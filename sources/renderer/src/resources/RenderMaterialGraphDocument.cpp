#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace kb::render {
namespace {

[[nodiscard]] bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        char left = lhs[index];
        char right = rhs[index];
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<char>(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = static_cast<char>(right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }
    return true;
}

void HashString64(std::uint64_t& hash, std::string_view value) noexcept {
    for (const char ch : value) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ULL;
    }
}

[[nodiscard]] std::string EncodeToken(std::string_view value) {
    std::string encoded;
    for (const char ch : value) {
        switch (ch) {
        case '%': encoded += "%25"; break;
        case ' ': encoded += "%20"; break;
        case '\t': encoded += "%09"; break;
        case '\n': encoded += "%0A"; break;
        case '\r': encoded += "%0D"; break;
        case '#': encoded += "%23"; break;
        default: encoded += ch; break;
        }
    }
    return encoded;
}

[[nodiscard]] std::string EncodeCustomPinSpec(std::span<const RenderMaterialGraphCustomPin> pins) {
    if (pins.empty()) {
        return "_";
    }
    std::string spec;
    for (std::size_t index = 0U; index < pins.size(); ++index) {
        if (index != 0U) {
            spec += ",";
        }
        spec += EncodeToken(pins[index].name);
        spec += ":";
        spec += RenderMaterialGraphPinTypeName(pins[index].type);
    }
    return spec;
}

[[nodiscard]] std::string EncodeGraphNodeIdList(std::span<const std::uint32_t> nodeIds) {
    if (nodeIds.empty()) {
        return "_";
    }
    std::string text;
    for (std::size_t index = 0U; index < nodeIds.size(); ++index) {
        if (index != 0U) {
            text += ",";
        }
        text += std::to_string(nodeIds[index]);
    }
    return text;
}

[[nodiscard]] bool IsRenderMaterialGraphConstantNode(RenderMaterialGraphNodeKind kind) noexcept {
    return kind == RenderMaterialGraphNodeKind::ConstantScalar ||
        kind == RenderMaterialGraphNodeKind::ConstantVector2 ||
        kind == RenderMaterialGraphNodeKind::ConstantVector ||
        kind == RenderMaterialGraphNodeKind::ConstantColor ||
        kind == RenderMaterialGraphNodeKind::ConstantBool;
}

[[nodiscard]] bool IsRenderMaterialGraphOrganizationNode(RenderMaterialGraphNodeKind kind) noexcept {
    return kind == RenderMaterialGraphNodeKind::Reroute ||
        kind == RenderMaterialGraphNodeKind::NamedRerouteDeclaration ||
        kind == RenderMaterialGraphNodeKind::NamedRerouteUsage ||
        kind == RenderMaterialGraphNodeKind::CompositeInput ||
        kind == RenderMaterialGraphNodeKind::CompositeOutput;
}

[[nodiscard]] bool ShouldPersistGraphNodeMetadata(const RenderMaterialGraphNode& node) noexcept {
    return IsRenderMaterialGraphParameterNode(node.kind) ||
        IsRenderMaterialGraphConstantNode(node.kind) ||
        IsRenderMaterialGraphOrganizationNode(node.kind) ||
        node.kind == RenderMaterialGraphNodeKind::StaticBoolParameter ||
        node.kind == RenderMaterialGraphNodeKind::StaticSwitch ||
        node.kind == RenderMaterialGraphNodeKind::StaticComponentMask ||
        node.kind == RenderMaterialGraphNodeKind::CollectionParameter ||
        node.kind == RenderMaterialGraphNodeKind::ColorRamp ||
        node.kind == RenderMaterialGraphNodeKind::FunctionInput ||
        node.kind == RenderMaterialGraphNodeKind::FunctionOutput ||
        node.kind == RenderMaterialGraphNodeKind::MaterialFunctionCall ||
        node.kind == RenderMaterialGraphNodeKind::LayerStack ||
        ((node.kind == RenderMaterialGraphNodeKind::TextureSample ||
          node.kind == RenderMaterialGraphNodeKind::TextureObject ||
          node.kind == RenderMaterialGraphNodeKind::TextureSampleCube ||
          node.kind == RenderMaterialGraphNodeKind::TextureObjectCube ||
          node.kind == RenderMaterialGraphNodeKind::TextureSampleVolume ||
          node.kind == RenderMaterialGraphNodeKind::TextureObjectVolume ||
          node.kind == RenderMaterialGraphNodeKind::TextureSample2DArray ||
          node.kind == RenderMaterialGraphNodeKind::TextureObject2DArray) &&
            !node.parameter.stableId.empty()) ||
        (node.kind == RenderMaterialGraphNodeKind::Uv && !node.parameter.defaultValueHint.empty());
}

[[nodiscard]] std::string_view ParameterGroupName(RenderMaterialParameterGroup group) noexcept {
    switch (group) {
    case RenderMaterialParameterGroup::Core: return "Core";
    case RenderMaterialParameterGroup::Surface: return "Surface";
    case RenderMaterialParameterGroup::Advanced: return "Advanced";
    }
    return "Core";
}

[[nodiscard]] std::string_view TextureColorSpaceName(RenderMaterialTextureColorSpace colorSpace) noexcept {
    switch (colorSpace) {
    case RenderMaterialTextureColorSpace::Srgb: return "Srgb";
    case RenderMaterialTextureColorSpace::Linear: return "Linear";
    case RenderMaterialTextureColorSpace::Unknown: return "Unknown";
    }
    return "Unknown";
}

[[nodiscard]] std::string_view SamplerFilterName(RenderMaterialGraphSamplerFilter filter) noexcept {
    return filter == RenderMaterialGraphSamplerFilter::Point ? "Point" : "Linear";
}

[[nodiscard]] std::string_view SamplerWrapName(RenderMaterialGraphSamplerWrap wrap) noexcept {
    switch (wrap) {
    case RenderMaterialGraphSamplerWrap::Clamp:
        return "Clamp";
    case RenderMaterialGraphSamplerWrap::Mirror:
        return "Mirror";
    case RenderMaterialGraphSamplerWrap::Repeat:
        return "Repeat";
    }
    return "Repeat";
}

} // namespace

bool IsRenderMaterialGraphRenderPathProduction(RenderMaterialGraphRenderPath path) noexcept {
    switch (path) {
    case RenderMaterialGraphRenderPath::GpuForward:
    case RenderMaterialGraphRenderPath::GpuShadow:
    case RenderMaterialGraphRenderPath::GpuDeferred:
    case RenderMaterialGraphRenderPath::CpuFallback:
    case RenderMaterialGraphRenderPath::Preview:
        return true;
    }
    return false;
}

RenderMaterialDomain ParseRenderMaterialDomain(std::string_view text) noexcept {
    if (EqualsIgnoreCase(text, "surface")) return RenderMaterialDomain::Surface;
    if (EqualsIgnoreCase(text, "deferredDecal") || EqualsIgnoreCase(text, "deferred_decal") || EqualsIgnoreCase(text, "deferreddecal")) return RenderMaterialDomain::DeferredDecal;
    if (EqualsIgnoreCase(text, "lightFunction") || EqualsIgnoreCase(text, "light_function") || EqualsIgnoreCase(text, "lightfunction")) return RenderMaterialDomain::LightFunction;
    if (EqualsIgnoreCase(text, "volume")) return RenderMaterialDomain::Volume;
    if (EqualsIgnoreCase(text, "postProcess") || EqualsIgnoreCase(text, "post_process") || EqualsIgnoreCase(text, "postprocess")) return RenderMaterialDomain::PostProcess;
    if (EqualsIgnoreCase(text, "userInterface") || EqualsIgnoreCase(text, "user_interface") || EqualsIgnoreCase(text, "userinterface") || EqualsIgnoreCase(text, "ui")) return RenderMaterialDomain::UserInterface;
    return RenderMaterialDomain::Surface;
}

std::string_view RenderMaterialDomainName(RenderMaterialDomain domain) noexcept {
    switch (domain) {
    case RenderMaterialDomain::Surface: return "surface";
    case RenderMaterialDomain::DeferredDecal: return "deferredDecal";
    case RenderMaterialDomain::LightFunction: return "lightFunction";
    case RenderMaterialDomain::Volume: return "volume";
    case RenderMaterialDomain::PostProcess: return "postProcess";
    case RenderMaterialDomain::UserInterface: return "userInterface";
    }
    return "surface";
}

RenderMaterialShadingModel ParseRenderMaterialShadingModel(std::string_view text) noexcept {
    if (EqualsIgnoreCase(text, "unlit")) return RenderMaterialShadingModel::Unlit;
    if (EqualsIgnoreCase(text, "lit") || EqualsIgnoreCase(text, "defaultLit") || EqualsIgnoreCase(text, "default_lit") || EqualsIgnoreCase(text, "defaultlit")) return RenderMaterialShadingModel::DefaultLit;
    if (EqualsIgnoreCase(text, "subsurface")) return RenderMaterialShadingModel::Subsurface;
    if (EqualsIgnoreCase(text, "clearCoat") || EqualsIgnoreCase(text, "clear_coat") || EqualsIgnoreCase(text, "clearcoat")) return RenderMaterialShadingModel::ClearCoat;
    if (EqualsIgnoreCase(text, "cloth")) return RenderMaterialShadingModel::Cloth;
    if (EqualsIgnoreCase(text, "hair")) return RenderMaterialShadingModel::Hair;
    if (EqualsIgnoreCase(text, "eye")) return RenderMaterialShadingModel::Eye;
    if (EqualsIgnoreCase(text, "singleLayerWater") || EqualsIgnoreCase(text, "single_layer_water") || EqualsIgnoreCase(text, "singlelayerwater")) return RenderMaterialShadingModel::SingleLayerWater;
    if (EqualsIgnoreCase(text, "thinTranslucent") || EqualsIgnoreCase(text, "thin_translucent") || EqualsIgnoreCase(text, "thintranslucent")) return RenderMaterialShadingModel::ThinTranslucent;
    return RenderMaterialShadingModel::DefaultLit;
}

std::string_view RenderMaterialShadingModelName(RenderMaterialShadingModel model) noexcept {
    switch (model) {
    case RenderMaterialShadingModel::Unlit: return "unlit";
    case RenderMaterialShadingModel::DefaultLit: return "defaultLit";
    case RenderMaterialShadingModel::Subsurface: return "subsurface";
    case RenderMaterialShadingModel::ClearCoat: return "clearCoat";
    case RenderMaterialShadingModel::Cloth: return "cloth";
    case RenderMaterialShadingModel::Hair: return "hair";
    case RenderMaterialShadingModel::Eye: return "eye";
    case RenderMaterialShadingModel::SingleLayerWater: return "singleLayerWater";
    case RenderMaterialShadingModel::ThinTranslucent: return "thinTranslucent";
    }
    return "defaultLit";
}

RenderMaterialGraphBlendMode ParseRenderMaterialGraphBlendMode(std::string_view text) noexcept {
    if (EqualsIgnoreCase(text, "opaque")) return RenderMaterialGraphBlendMode::Opaque;
    if (EqualsIgnoreCase(text, "masked") || EqualsIgnoreCase(text, "mask")) return RenderMaterialGraphBlendMode::Masked;
    if (EqualsIgnoreCase(text, "translucent") || EqualsIgnoreCase(text, "transparent") || EqualsIgnoreCase(text, "alpha")) return RenderMaterialGraphBlendMode::Translucent;
    if (EqualsIgnoreCase(text, "additive")) return RenderMaterialGraphBlendMode::Additive;
    if (EqualsIgnoreCase(text, "modulate")) return RenderMaterialGraphBlendMode::Modulate;
    if (EqualsIgnoreCase(text, "alphaComposite") || EqualsIgnoreCase(text, "alpha_composite") || EqualsIgnoreCase(text, "alphacomposite") || EqualsIgnoreCase(text, "premultipliedAlpha") || EqualsIgnoreCase(text, "premultiplied_alpha")) return RenderMaterialGraphBlendMode::AlphaComposite;
    if (EqualsIgnoreCase(text, "alphaHoldout") || EqualsIgnoreCase(text, "alpha_holdout") || EqualsIgnoreCase(text, "alphaholdout")) return RenderMaterialGraphBlendMode::AlphaHoldout;
    return RenderMaterialGraphBlendMode::Opaque;
}

std::string_view RenderMaterialGraphBlendModeName(RenderMaterialGraphBlendMode mode) noexcept {
    switch (mode) {
    case RenderMaterialGraphBlendMode::Opaque: return "opaque";
    case RenderMaterialGraphBlendMode::Masked: return "masked";
    case RenderMaterialGraphBlendMode::Translucent: return "translucent";
    case RenderMaterialGraphBlendMode::Additive: return "additive";
    case RenderMaterialGraphBlendMode::Modulate: return "modulate";
    case RenderMaterialGraphBlendMode::AlphaComposite: return "alphaComposite";
    case RenderMaterialGraphBlendMode::AlphaHoldout: return "alphaHoldout";
    }
    return "opaque";
}

RenderMaterialGraphNodeSupport RenderMaterialGraphNodeSupportForPath(RenderMaterialGraphNodeKind kind, RenderMaterialGraphRenderPath path) noexcept {
    const RenderMaterialGraphNodeSupport status = RenderMaterialGraphNodeSupportStatus(kind);
    if (status == RenderMaterialGraphNodeSupport::Unsupported) {
        return RenderMaterialGraphNodeSupport::Unsupported;
    }
    if (path == RenderMaterialGraphRenderPath::GpuDeferred) {
        switch (kind) {
        case RenderMaterialGraphNodeKind::SceneDepth:
        case RenderMaterialGraphNodeKind::SceneColor:
        case RenderMaterialGraphNodeKind::SceneTexture:
        case RenderMaterialGraphNodeKind::DepthFade:
            return RenderMaterialGraphNodeSupport::Unsupported;
        default:
            break;
        }
    }
    if (!IsRenderMaterialGraphRenderPathProduction(path)) {
        return RenderMaterialGraphNodeSupport::FallbackOnly;
    }
    return status;
}

static std::string_view CanonicalGraphMaterialDomainName(std::string_view domain) noexcept {
    if (domain.empty()) return "surface";
    if (EqualsIgnoreCase(domain, "surface")) return "surface";
    if (EqualsIgnoreCase(domain, "deferredDecal") || EqualsIgnoreCase(domain, "deferred_decal") || EqualsIgnoreCase(domain, "deferreddecal")) return "deferredDecal";
    if (EqualsIgnoreCase(domain, "lightFunction") || EqualsIgnoreCase(domain, "light_function") || EqualsIgnoreCase(domain, "lightfunction")) return "lightFunction";
    if (EqualsIgnoreCase(domain, "volume")) return "volume";
    if (EqualsIgnoreCase(domain, "postProcess") || EqualsIgnoreCase(domain, "post_process") || EqualsIgnoreCase(domain, "postprocess")) return "postProcess";
    if (EqualsIgnoreCase(domain, "userInterface") || EqualsIgnoreCase(domain, "user_interface") || EqualsIgnoreCase(domain, "userinterface") || EqualsIgnoreCase(domain, "ui")) return "userInterface";
    return domain;
}

static std::string_view CanonicalGraphShadingModelName(std::string_view shadingModel) noexcept {
    if (shadingModel.empty()) return "defaultLit";
    if (EqualsIgnoreCase(shadingModel, "unlit")) return "unlit";
    if (EqualsIgnoreCase(shadingModel, "lit") || EqualsIgnoreCase(shadingModel, "defaultLit") || EqualsIgnoreCase(shadingModel, "default_lit") || EqualsIgnoreCase(shadingModel, "defaultlit")) return "defaultLit";
    if (EqualsIgnoreCase(shadingModel, "subsurface")) return "subsurface";
    if (EqualsIgnoreCase(shadingModel, "clearCoat") || EqualsIgnoreCase(shadingModel, "clear_coat") || EqualsIgnoreCase(shadingModel, "clearcoat")) return "clearCoat";
    if (EqualsIgnoreCase(shadingModel, "cloth")) return "cloth";
    if (EqualsIgnoreCase(shadingModel, "hair")) return "hair";
    if (EqualsIgnoreCase(shadingModel, "eye")) return "eye";
    if (EqualsIgnoreCase(shadingModel, "singleLayerWater") || EqualsIgnoreCase(shadingModel, "single_layer_water") || EqualsIgnoreCase(shadingModel, "singlelayerwater")) return "singleLayerWater";
    if (EqualsIgnoreCase(shadingModel, "thinTranslucent") || EqualsIgnoreCase(shadingModel, "thin_translucent") || EqualsIgnoreCase(shadingModel, "thintranslucent")) return "thinTranslucent";
    return shadingModel;
}

static std::string_view CanonicalGraphBlendModeName(std::string_view blendMode) noexcept {
    if (blendMode.empty()) return "opaque";
    if (EqualsIgnoreCase(blendMode, "opaque")) return "opaque";
    if (EqualsIgnoreCase(blendMode, "masked") || EqualsIgnoreCase(blendMode, "mask")) return "masked";
    if (EqualsIgnoreCase(blendMode, "translucent") || EqualsIgnoreCase(blendMode, "transparent") || EqualsIgnoreCase(blendMode, "alpha")) return "translucent";
    if (EqualsIgnoreCase(blendMode, "additive")) return "additive";
    if (EqualsIgnoreCase(blendMode, "modulate")) return "modulate";
    if (EqualsIgnoreCase(blendMode, "alphaComposite") || EqualsIgnoreCase(blendMode, "alpha_composite") || EqualsIgnoreCase(blendMode, "alphacomposite") || EqualsIgnoreCase(blendMode, "premultipliedAlpha") || EqualsIgnoreCase(blendMode, "premultiplied_alpha")) return "alphaComposite";
    if (EqualsIgnoreCase(blendMode, "alphaHoldout") || EqualsIgnoreCase(blendMode, "alpha_holdout") || EqualsIgnoreCase(blendMode, "alphaholdout")) return "alphaHoldout";
    return blendMode;
}

RenderMaterialGraphDocument MakeDefaultRenderMaterialGraphDocument() {
    RenderMaterialGraphDocument graph{};
    graph.documentVersion = kRenderMaterialGraphDocumentVersion;
    graph.hasExplicitDocumentVersion = true;
    graph.hasExplicitArtifactFailurePolicy = true;
    graph.materialDomain = "surface";
    graph.shadingModel = "defaultLit";
    graph.storageModel = "inline-kbmat";
    graph.diagnosticSchemaVersion = 1U;
    graph.persistCompileDiagnostics = true;
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 1U,
        .kind = RenderMaterialGraphNodeKind::MaterialOutput,
        .positionX = 640,
        .positionY = 240,
    });
    return graph;
}

void WriteRenderMaterialGraphDocument(std::ostream& output, const RenderMaterialGraphDocument& graph) {
    output << "graphVersion " << (graph.documentVersion == 0U ? kRenderMaterialGraphDocumentVersion : graph.documentVersion) << '\n';
    output << "graphMaterialDomain " << CanonicalGraphMaterialDomainName(graph.materialDomain) << '\n';
    output << "graphShadingModel " << CanonicalGraphShadingModelName(graph.shadingModel) << '\n';
    output << "graphBlendMode " << CanonicalGraphBlendModeName(graph.blendMode) << '\n';
    output << "graphStorageModel " << (graph.storageModel.empty() ? "inline-kbmat" : graph.storageModel) << '\n';
    output << "graphDiagnosticSchemaVersion " << (graph.diagnosticSchemaVersion == 0U ? 1U : graph.diagnosticSchemaVersion) << '\n';
    output << "graphPersistCompileDiagnostics " << (graph.persistCompileDiagnostics ? "true" : "false") << '\n';
    output << "graphArtifactFailurePolicy " << RenderMaterialGraphArtifactFailurePolicyName(graph.artifactFailurePolicy) << '\n';
    if (graph.lastGoodArtifact.assetId != 0U) {
        output << "graphLastGoodArtifactAssetId " << graph.lastGoodArtifact.assetId << '\n';
    }
    if (graph.lastGoodArtifact.contentHash != 0U) {
        output << "graphLastGoodArtifactHash " << graph.lastGoodArtifact.contentHash << '\n';
    }
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        output << "graphNode "
            << node.id << ' '
            << RenderMaterialGraphNodeKindName(node.kind) << ' '
            << node.positionX << ' '
            << node.positionY << '\n';
        if (ShouldPersistGraphNodeMetadata(node)) {
            output << "graphParameter "
                << node.id << ' '
                << (node.parameter.stableId.empty() ? "_" : EncodeToken(node.parameter.stableId)) << ' '
                << (node.parameter.displayName.empty() ? "_" : EncodeToken(node.parameter.displayName)) << ' '
                << ParameterGroupName(node.parameter.group) << ' '
                << (node.parameter.defaultValueHint.empty() ? "_" : EncodeToken(node.parameter.defaultValueHint)) << ' '
                << (node.parameter.hasRange ? std::to_string(node.parameter.rangeMin) : "_") << ' '
                << (node.parameter.hasRange ? std::to_string(node.parameter.rangeMax) : "_") << ' '
                << (node.parameter.textureRole.empty() ? "_" : EncodeToken(node.parameter.textureRole)) << ' '
                << TextureColorSpaceName(node.parameter.expectedTextureColorSpace) << ' '
                << (node.parameter.overrideSupported ? "true" : "false") << ' '
                << node.parameter.editorOrder << ' '
                << (node.parameter.description.empty() ? "_" : EncodeToken(node.parameter.description)) << '\n';
            output << "graphSamplerState "
                << node.id << ' '
                << SamplerFilterName(node.parameter.samplerState.minFilter) << ' '
                << SamplerFilterName(node.parameter.samplerState.magFilter) << ' '
                << SamplerFilterName(node.parameter.samplerState.mipFilter) << ' '
                << SamplerWrapName(node.parameter.samplerState.wrapU) << ' '
                << SamplerWrapName(node.parameter.samplerState.wrapV) << '\n';
        }
        if (node.kind == RenderMaterialGraphNodeKind::CustomCode ||
            node.kind == RenderMaterialGraphNodeKind::MaterialFunctionCall) {
            output << "graphCustomCode "
                << node.id << ' '
                << RenderMaterialGraphPinTypeName(node.customCode.outputType) << ' '
                << EncodeCustomPinSpec(node.customCode.inputs) << ' '
                << EncodeCustomPinSpec(node.customCode.outputs) << ' '
                << (node.customCode.defines.empty() ? "_" : EncodeToken(node.customCode.defines)) << ' '
                << (node.customCode.includes.empty() ? "_" : EncodeToken(node.customCode.includes)) << ' '
                << (node.customCode.body.empty() ? "_" : EncodeToken(node.customCode.body)) << '\n';
        }
        if (node.kind == RenderMaterialGraphNodeKind::LayerStack) {
            for (std::size_t index = 0U; index < node.layerStack.size(); ++index) {
                const RenderMaterialGraphLayerStackEntry& entry = node.layerStack[index];
                output << "graphLayerStackEntry "
                    << node.id << ' '
                    << index << ' '
                    << entry.layerFunctionAssetId << ' '
                    << entry.blendFunctionAssetId << ' '
                    << (entry.enabled ? "true" : "false") << ' '
                    << (entry.layerName.empty() ? "_" : EncodeToken(entry.layerName)) << ' '
                    << (entry.blendName.empty() ? "_" : EncodeToken(entry.blendName)) << ' '
                    << (entry.linkState.empty() ? "_" : EncodeToken(entry.linkState)) << '\n';
                for (const RenderMaterialGraphLayerStackParameter& parameter : entry.layerParameters) {
                    output << "graphLayerStackParameter "
                        << node.id << ' '
                        << index << ' '
                        << "layer" << ' '
                        << (parameter.pinName.empty() ? "_" : EncodeToken(parameter.pinName)) << ' '
                        << RenderMaterialGraphPinTypeName(parameter.type) << ' '
                        << (parameter.valueHint.empty() ? "_" : EncodeToken(parameter.valueHint)) << '\n';
                }
                for (const RenderMaterialGraphLayerStackParameter& parameter : entry.blendParameters) {
                    output << "graphLayerStackParameter "
                        << node.id << ' '
                        << index << ' '
                        << "blend" << ' '
                        << (parameter.pinName.empty() ? "_" : EncodeToken(parameter.pinName)) << ' '
                        << RenderMaterialGraphPinTypeName(parameter.type) << ' '
                        << (parameter.valueHint.empty() ? "_" : EncodeToken(parameter.valueHint)) << '\n';
                }
            }
        }
    }
    for (const RenderMaterialGraphLink& link : graph.links) {
        const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link.fromNodeId);
        const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId);
        const std::uint32_t fromPinId = link.fromPinId != 0U || fromNode == nullptr
            ? link.fromPinId
            : RenderMaterialGraphStablePinId(*fromNode, link.fromPin, true);
        const std::uint32_t toPinId = link.toPinId != 0U || toNode == nullptr
            ? link.toPinId
            : RenderMaterialGraphStablePinId(*toNode, link.toPin, false);
        const std::uint32_t linkId = link.id != 0U
            ? link.id
            : MakeRenderMaterialGraphLinkId(RenderMaterialGraphLink{
                .fromNodeId = link.fromNodeId,
                .fromPinId = fromPinId,
                .toNodeId = link.toNodeId,
                .toPinId = toPinId,
            });
        output << "graphLink "
            << linkId << ' '
            << link.fromNodeId << ' '
            << fromPinId << ' '
            << link.fromPin << ' '
            << link.toNodeId << ' '
            << toPinId << ' '
            << link.toPin << '\n';
    }
    for (const RenderMaterialGraphCommentBox& comment : graph.comments) {
        output << "graphComment "
            << comment.id << ' '
            << comment.positionX << ' '
            << comment.positionY << ' '
            << comment.width << ' '
            << comment.height << ' '
            << comment.color << ' '
            << (comment.text.empty() ? "_" : EncodeToken(comment.text)) << '\n';
    }
    for (const RenderMaterialGraphCompositeSubgraph& composite : graph.composites) {
        output << "graphComposite "
            << composite.id << ' '
            << composite.positionX << ' '
            << composite.positionY << ' '
            << composite.width << ' '
            << composite.height << ' '
            << composite.color << ' '
            << (composite.collapsed ? "true" : "false") << ' '
            << (composite.name.empty() ? "_" : EncodeToken(composite.name)) << ' '
            << EncodeGraphNodeIdList(composite.nodeIds) << '\n';
    }
}

void StripRenderMaterialGraphEditorOnlyState(RenderMaterialGraphDocument& graph) noexcept {
    graph.documentVersion = kRenderMaterialGraphDocumentVersion;
    graph.hasExplicitDocumentVersion = true;
    graph.comments.clear();
    graph.composites.clear();
    graph.storageModel.clear();
    graph.diagnosticSchemaVersion = 0U;
    graph.persistCompileDiagnostics = false;
    for (RenderMaterialGraphNode& node : graph.nodes) {
        node.positionX = 0;
        node.positionY = 0;
        const bool displayNameProvidesLegacyIdentity = node.parameter.stableId.empty() &&
            (node.kind == RenderMaterialGraphNodeKind::FunctionInput ||
                node.kind == RenderMaterialGraphNodeKind::FunctionOutput ||
                node.kind == RenderMaterialGraphNodeKind::NamedRerouteDeclaration ||
                node.kind == RenderMaterialGraphNodeKind::NamedRerouteUsage);
        if (!displayNameProvidesLegacyIdentity) {
            node.parameter.displayName.clear();
        }
        node.parameter.description.clear();
        node.parameter.group = RenderMaterialParameterGroup::Core;
        node.parameter.editorOrder = 0U;
        for (RenderMaterialGraphLayerStackEntry& entry : node.layerStack) {
            entry.layerName.clear();
            entry.blendName.clear();
            entry.linkState.clear();
        }
    }
    for (RenderMaterialGraphLink& link : graph.links) {
        const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link.fromNodeId);
        const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId);
        if (fromNode != nullptr) {
            link.fromPinId = RenderMaterialGraphStablePinId(*fromNode, link.fromPin, true);
        }
        if (toNode != nullptr) {
            link.toPinId = RenderMaterialGraphStablePinId(*toNode, link.toPin, false);
        }
        link.id = MakeRenderMaterialGraphLinkId(link);
    }
    std::ranges::sort(graph.nodes, [](const RenderMaterialGraphNode& lhs, const RenderMaterialGraphNode& rhs) {
        return lhs.id < rhs.id;
    });
    std::ranges::sort(graph.links, [](const RenderMaterialGraphLink& lhs, const RenderMaterialGraphLink& rhs) {
        return std::tie(lhs.id, lhs.fromNodeId, lhs.fromPinId, lhs.toNodeId, lhs.toPinId, lhs.fromPin, lhs.toPin) <
            std::tie(rhs.id, rhs.fromNodeId, rhs.fromPinId, rhs.toNodeId, rhs.toPinId, rhs.fromPin, rhs.toPin);
    });
}

std::uint64_t RenderMaterialGraphShaderSemanticHash(const RenderMaterialGraphDocument& graph) {
    RenderMaterialGraphDocument semantic = graph;
    StripRenderMaterialGraphEditorOnlyState(semantic);
    semantic.artifactFailurePolicy = RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial;
    semantic.hasExplicitArtifactFailurePolicy = false;
    semantic.lastGoodArtifact = {};
    std::ostringstream serialized;
    WriteRenderMaterialGraphDocument(serialized, semantic);
    std::uint64_t hash = 1469598103934665603ULL;
    HashString64(hash, serialized.str());
    return hash == 0U ? 1U : hash;
}

} // namespace kb::render
