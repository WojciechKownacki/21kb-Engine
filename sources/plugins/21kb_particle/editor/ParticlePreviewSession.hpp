#pragma once

#include "ParticleEditorDocument.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>

namespace kb::assets { class AssetRegistry; }
namespace kb::project { struct ProjectDescriptor; }
namespace kb::render { class Renderer; }
namespace kb::scene { class Scene; }

namespace kb::particle_editor {

class ParticlePreviewSession final {
public:
    ParticlePreviewSession() = default;
    ~ParticlePreviewSession();
    ParticlePreviewSession(const ParticlePreviewSession&) = delete;
    ParticlePreviewSession& operator=(const ParticlePreviewSession&) = delete;

    [[nodiscard]] ParticleEditorResult Start(
        const kb::project::ProjectDescriptor& project,
        const kb::assets::AssetRegistry& sourceRegistry,
        kb::assets::AssetId assetId,
        std::filesystem::path virtualPath,
        const kb::scene::ParticleEffectAsset& asset);
    [[nodiscard]] ParticleEditorResult PublishWorkingCopy(const kb::scene::ParticleEffectAsset& asset);
    [[nodiscard]] ParticleEditorResult Tick(float wallDeltaSeconds);
    [[nodiscard]] bool Submit(kb::render::Renderer& renderer) const;
    void Release(kb::render::Renderer& renderer);
    void Release(const std::function<void(const kb::scene::Scene&)>& releaseRendererScene);

    [[nodiscard]] bool Active() const noexcept;
    [[nodiscard]] const kb::scene::Scene& PreviewScene() const;
    [[nodiscard]] kb::scene::Scene& PreviewScene();
    [[nodiscard]] kb::scene::SceneEntity EffectEntity() const noexcept;
    [[nodiscard]] kb::scene::SceneEntity CameraEntity() const noexcept;

private:
    std::unique_ptr<kb::scene::Scene> scene_;
    kb::assets::AssetId assetId_{};
    std::filesystem::path virtualPath_;
    kb::scene::SceneEntity effectEntity_{};
    kb::scene::SceneEntity cameraEntity_{};
};

} // namespace kb::particle_editor
