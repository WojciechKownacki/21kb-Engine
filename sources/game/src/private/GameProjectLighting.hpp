#pragma once

#include "GameProjectRuntime.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/scene/SceneLightingQuality.hpp"

#include <ostream>

namespace kb::game {

// Apply after renderer initialization, including surface/context recreation. The project's render scale is
// applied here too, so packaged hosts draw the 3D scene at the share of the surface the project chose.
inline void ApplyGameProjectLighting(kb::render::Renderer& renderer,
    const GameProjectRuntime& project, std::ostream& diagnostics) {
    renderer.SetRenderScale(project.display.renderScale);
    auto lighting=renderer.DefaultSceneLightingConfig();
    lighting.lightingPath=kb::render::ProjectLightingPath(project.lightingPath,kMobileDevice);
    kb::render::ApplyProjectLightingQuality(lighting,project.shadowQuality,project.giQuality,kMobileDevice);
    lighting.maxForwardLights=lighting.lightingPath==kb::render::SceneRenderLightingPath::ClusteredForwardPlus
        ? kb::render::kMaxSceneForwardPlusLights : kb::render::kMaxSceneForwardLights;
    renderer.SetDefaultSceneLightingConfig(lighting);
    if constexpr (kMobileDevice) {
        // A scene without a post-process profile uses FXAA instead of TAA on mobile: the temporal resolve and its
        // motion vectors are two more full-resolution passes. A scene profile keeps its own choice.
        auto postProcess=renderer.DefaultPostProcessSettings();
        postProcess.temporalAntiAliasingEnabled=false;
        postProcess.temporalJitterEnabled=false;
        postProcess.fxaaEnabled=true;
        renderer.SetDefaultPostProcessSettings(postProcess);
    }
    diagnostics << "packaged lighting path=" << static_cast<unsigned>(lighting.lightingPath) << '\n';
}

inline void ReportGameMaterialFallbacks(const kb::render::Renderer& renderer,
    bool& reportedShading, bool& reportedProgram, std::ostream& diagnostics) {
    for (const auto& event : renderer.LastSceneDiagnostics().events) {
        if (event.kind == kb::render::SceneRenderDiagnosticKind::MaterialShadingFallback && !reportedShading) {
            diagnostics << "material capability fallback: four-target G-buffer omits extra scattering color or iris data; material="
                << event.materialAssetId << '\n';
            reportedShading = true;
        } else if (event.kind == kb::render::SceneRenderDiagnosticKind::GraphMaterialProgramFallback && !reportedProgram) {
            diagnostics << "material program fallback: graph program unavailable; material=" << event.materialAssetId << '\n';
            reportedProgram = true;
        }
    }
}

} // namespace kb::game
