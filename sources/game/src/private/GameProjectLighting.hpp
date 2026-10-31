#pragma once

#include "GameProjectRuntime.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/scene/SceneLightingQuality.hpp"

#include <ostream>

namespace kb::game {

// Apply after renderer initialization, including surface/context recreation.
inline void ApplyGameProjectLighting(kb::render::Renderer& renderer,
    const GameProjectRuntime& project, std::ostream& diagnostics) {
    auto lighting=renderer.DefaultSceneLightingConfig();
    switch(project.lightingPath) {
    case kb::project::ProjectSceneLightingPath::Forward: lighting.lightingPath=kb::render::SceneRenderLightingPath::Forward; break;
    case kb::project::ProjectSceneLightingPath::ForwardPlus: lighting.lightingPath=kb::render::SceneRenderLightingPath::ClusteredForwardPlus; break;
    case kb::project::ProjectSceneLightingPath::Deferred: lighting.lightingPath=kb::render::SceneRenderLightingPath::Deferred; break;
    }
    kb::render::ApplyProjectLightingQuality(lighting,project.shadowQuality,project.giQuality,kMobileDevice);
    lighting.maxForwardLights=lighting.lightingPath==kb::render::SceneRenderLightingPath::ClusteredForwardPlus
        ? kb::render::kMaxSceneForwardPlusLights : kb::render::kMaxSceneForwardLights;
    renderer.SetDefaultSceneLightingConfig(lighting);
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
