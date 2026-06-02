#pragma once

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <memory>
#include <vector>

namespace kb::render {

class SceneMeshSubmitter;
class RenderScene;

class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown();
    void Submit(bgfx::ViewId viewId, const RenderScene& renderScene, std::uint32_t viewportWidth, std::uint32_t viewportHeight, const SceneRenderCamera* cameraOverride = nullptr, SceneRenderDrawBudget drawBudget = {}, SceneRenderLightingConfig lightingConfig = {}) const;
    void SubmitMeshPass(bgfx::ViewId viewId, MeshPassType pass, const RenderScene& renderScene, std::uint32_t viewportWidth, std::uint32_t viewportHeight, const SceneRenderCamera* cameraOverride = nullptr, SceneRenderDrawBudget drawBudget = {}, SceneRenderLightingConfig lightingConfig = {}, const SceneRenderShadowMapBinding* shadowMap = nullptr) const;
    void SetDefaultDrawBudget(SceneRenderDrawBudget drawBudget) noexcept;
    [[nodiscard]] SceneRenderDrawBudget DefaultDrawBudget() const noexcept;
    void SetDefaultLightingConfig(SceneRenderLightingConfig lightingConfig) noexcept;
    [[nodiscard]] SceneRenderLightingConfig DefaultLightingConfig() const noexcept;
    void TickFrame() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] RenderResourceRegistry& Resources() noexcept;
    [[nodiscard]] const RenderResourceRegistry& Resources() const noexcept;
    [[nodiscard]] SceneRenderResourceMap& ResourceMap() noexcept;
    [[nodiscard]] const SceneRenderResourceMap& ResourceMap() const noexcept;
    [[nodiscard]] SceneRenderSubmitStats ValidateSceneResources(const RenderScene& renderScene) const noexcept;
    [[nodiscard]] SceneRenderSubmitStats ValidateSceneResources(const RenderScene& renderScene, MeshPassType pass) const noexcept;
    [[nodiscard]] SceneRenderDiagnostics ValidateSceneDiagnostics(const RenderScene& renderScene) const;
    [[nodiscard]] SceneRenderDiagnostics ValidateSceneDiagnostics(const RenderScene& renderScene, MeshPassType pass) const;
    [[nodiscard]] SceneRenderSubmitStats LastSubmitStats() const noexcept;
    [[nodiscard]] const SceneRenderDiagnostics& LastDiagnostics() const noexcept;

private:
    RenderResourceRegistry resources_;
    SceneRenderResourceMap resourceMap_;
    std::unique_ptr<SceneMeshSubmitter> meshSubmitter_;
    mutable std::vector<SceneRenderDrawGroup> validationDrawGroupsScratch_;
    mutable MeshPipelineBuildResult validationPipelineScratch_;
    mutable SceneRenderSubmitStats lastSubmitStats_{};
    mutable SceneRenderDiagnostics lastDiagnostics_{};
    SceneRenderDrawBudget defaultDrawBudget_{};
    SceneRenderLightingConfig defaultLightingConfig_{};
    bool initialized_ = false;
};

} // namespace kb::render
