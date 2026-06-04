#pragma once

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "scene/lighting/SceneLightingPacker.hpp"
#include "scene/submit/SceneMeshPassResources.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <span>

namespace kb::render {

struct SceneMeshDrawCommandSubmitDesc {
    bgfx::ViewId viewId = 0;
    std::span<const MeshDrawCommand> commands{};
    MeshPassType pass = MeshPassType::BaseOpaque;
    const RenderResourceRegistry& resources;
    const SceneRenderResourceMap& resourceMap;
    const PackedSceneLighting& lighting;
    const std::array<float, 4>& cameraPosition;
    const SceneRenderShadowMapBinding* shadowMap = nullptr;
    const SceneMeshPassResources& passResources;
    SceneRenderDiagnostics* diagnostics = nullptr;
    SceneRenderSubmitStats& stats;
};

class SceneMeshDrawCommandSubmitter {
public:
    SceneMeshDrawCommandSubmitter() = delete;

    static void Submit(const SceneMeshDrawCommandSubmitDesc& desc);
};

} // namespace kb::render
