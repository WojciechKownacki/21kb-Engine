#include "kb/render/resources/RenderMaterialGraphCost.hpp"

#include <algorithm>
#include <unordered_set>

namespace kb::render {
namespace {

using Kind = RenderMaterialGraphNodeKind;

[[nodiscard]] std::uint32_t NoiseInstructions(const RenderMaterialGraphNode& node) {
    const RenderMaterialGraphNoiseSettings settings = ParseRenderMaterialGraphNoiseSettings(node.parameter.defaultValueHint).settings.value_or(RenderMaterialGraphNoiseSettings{});
    std::uint32_t perLevel = 20U;
    switch (settings.function) {
    case RenderMaterialGraphNoiseFunction::Simplex: perLevel = 24U; break;
    case RenderMaterialGraphNoiseFunction::Gradient: perLevel = 16U; break;
    case RenderMaterialGraphNoiseFunction::Value: perLevel = 10U; break;
    case RenderMaterialGraphNoiseFunction::Voronoi: perLevel = 24U * settings.quality * settings.quality; break;
    }
    return perLevel * std::max(1U, settings.levels);
}

[[nodiscard]] std::uint32_t Instructions(const RenderMaterialGraphNode& node) {
    switch (node.kind) {
    case Kind::MaterialOutput:
    case Kind::Reroute:
    case Kind::NamedRerouteDeclaration:
    case Kind::NamedRerouteUsage:
    case Kind::CompositeInput:
    case Kind::CompositeOutput:
    case Kind::ConstantScalar:
    case Kind::ConstantVector2:
    case Kind::ConstantVector:
    case Kind::ConstantColor:
    case Kind::ConstantBool:
    case Kind::ParameterScalar:
    case Kind::ParameterVector:
    case Kind::ParameterColor:
    case Kind::ParameterTexture:
    case Kind::TextureObject:
    case Kind::TextureObjectCube:
    case Kind::TextureObjectVolume:
    case Kind::TextureObject2DArray:
        return 0U;
    case Kind::Noise: return NoiseInstructions(node);
    case Kind::VectorNoise: return 3U * NoiseInstructions(node);
    case Kind::ParallaxOcclusionMapping: return 160U;
    case Kind::WorldAlignedTexture: return 24U;
    case Kind::TextureSample:
    case Kind::TextureSampleCube:
    case Kind::TextureSampleVolume:
    case Kind::TextureSample2DArray: return 4U;
    case Kind::SceneColor:
    case Kind::SceneTexture:
    case Kind::SceneDepth:
    case Kind::DepthFade: return 6U;
    case Kind::ColorRamp: return 6U * std::max<std::uint32_t>(2U, static_cast<std::uint32_t>(std::count(node.parameter.defaultValueHint.begin(), node.parameter.defaultValueHint.end(), ' ') + 1) / 4U);
    case Kind::BlackBody:
    case Kind::HueShift:
    case Kind::HsvToRgb:
    case Kind::RgbToHsv:
    case Kind::Twirl:
    case Kind::PolarCoordinates:
    case Kind::RotateAboutAxis: return 12U;
    case Kind::Fresnel:
    case Kind::Ellipse:
    case Kind::Rectangle:
    case Kind::RoundedRectangle:
    case Kind::Checkerboard:
    case Kind::Rotator:
    case Kind::SphereMask: return 8U;
    case Kind::Power:
    case Kind::Sine:
    case Kind::Cosine:
    case Kind::Tangent:
    case Kind::Exponential:
    case Kind::Exponential2:
    case Kind::Logarithm:
    case Kind::Logarithm2:
    case Kind::Logarithm10:
    case Kind::Normalize:
    case Kind::Desaturate:
    case Kind::Contrast:
    case Kind::ChannelMixer: return 4U;
    case Kind::MaterialFunctionCall:
    case Kind::LayerStack:
    case Kind::CustomCode: return 30U;
    default: return 1U;
    }
}

[[nodiscard]] bool ReadsScene(Kind kind) {
    return kind == Kind::SceneColor || kind == Kind::SceneTexture || kind == Kind::SceneDepth || kind == Kind::DepthFade;
}

[[nodiscard]] bool SamplesTexture(Kind kind) {
    return kind == Kind::TextureSample || kind == Kind::TextureSampleCube || kind == Kind::TextureSampleVolume ||
        kind == Kind::TextureSample2DArray || kind == Kind::ParallaxOcclusionMapping;
}

} // namespace

RenderMaterialGraphCost EstimateRenderMaterialGraphCost(const RenderMaterialGraphDocument& graph) {
    // The nodes that feed the output on a phone, found from the output backwards: a quality or level switch keeps the
    // branch the phone takes, so a scene read behind the High quality is no cost here.
    const RenderMaterialGraphBuildContext phone = RenderMaterialGraphDeviceContext(true);
    std::unordered_set<std::uint32_t> reached;
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind != Kind::MaterialOutput) continue;
        for (const std::uint32_t id : RenderMaterialGraphReachableNodes(graph, phone, node.id)) reached.insert(id);
    }
    RenderMaterialGraphCost cost;
    bool parallax = false;
    bool voronoiOrManyOctaves = false;
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (!reached.contains(node.id) || node.kind == Kind::MaterialOutput) continue;
        ++cost.nodes;
        cost.instructions += Instructions(node);
        if (SamplesTexture(node.kind)) ++cost.textureSamples;
        if (node.kind == Kind::WorldAlignedTexture) cost.textureSamples += 3U;
        if (ReadsScene(node.kind)) ++cost.sceneReads;
        parallax = parallax || node.kind == Kind::ParallaxOcclusionMapping;
        voronoiOrManyOctaves = voronoiOrManyOctaves || (node.kind == Kind::Noise && NoiseInstructions(node) > 60U);
    }
    if (cost.instructions > kRenderMaterialMobileInstructionBudget) {
        cost.mobileWarnings.push_back("about " + std::to_string(cost.instructions) + " instructions per pixel (a phone should stay under " +
            std::to_string(kRenderMaterialMobileInstructionBudget) + ")" + (voronoiOrManyOctaves ? "; a Noise with many levels or Voronoi dominates, use fewer levels or the value function" : ""));
    }
    if (cost.textureSamples > kRenderMaterialMobileTextureSampleBudget) {
        cost.mobileWarnings.push_back(std::to_string(cost.textureSamples) + " texture samples (a phone should stay under " + std::to_string(kRenderMaterialMobileTextureSampleBudget) + ")");
    }
    // Single Layer Water reads the scene behind it too, but a phone draws it as a lit translucent surface (see RenderMaterialGraphDeviceContext).
    if (cost.sceneReads > 0U) {
        cost.mobileWarnings.push_back("reads the scene behind the surface (" + std::to_string(cost.sceneReads) + " reads): a full-screen copy per frame on a phone");
    }
    if (parallax) {
        cost.mobileWarnings.push_back("Parallax Occlusion Mapping ray-marches the height map per pixel; use Bump Offset on phones");
    }
    if (IsRenderMaterialGraphBlendModeTransparent(ParseRenderMaterialGraphBlendMode(graph.blendMode))) {
        cost.notes.push_back("blending draws every layer of overlapping surfaces; on a phone keep such surfaces small, and use Masked for leaves, hair and fences");
    }
    const bool movesVertices = std::ranges::any_of(graph.links, [&](const RenderMaterialGraphLink& link) { return link.toPin == "worldPositionOffset" && reached.contains(link.toNodeId); });
    if (movesVertices && graph.maxWorldPositionOffsetDisplacement <= 0.0F) {
        cost.notes.push_back("the graph moves vertices but Max World Position Offset Displacement is 0, so the object can vanish at the screen edge; set it to the largest move");
    }
    return cost;
}

} // namespace kb::render
