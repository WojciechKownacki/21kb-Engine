#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/BlendSpace2D.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <functional>
#include <memory>

namespace kb::editor {
class EditorBlendSpace2DPreviewSession final {
public:
    ~EditorBlendSpace2DPreviewSession() { Clear(); }
    [[nodiscard]] bool Open(kb::scene::Scene& source, kb::scene::SceneEntity owner,
        const kb::project::ProjectDescriptor& project, kb::assets::AssetId asset,
        const kb::scene::BlendSpace2DAsset& working, float x, float y, float phase, std::string& error);
    [[nodiscard]] bool SetCoordinates(float x, float y, std::string& error);
    [[nodiscard]] bool Seek(float phase, std::string& error);
    [[nodiscard]] bool Tick(float deltaSeconds, std::string& error);
    void SetPlaying(bool playing) noexcept { playing_ = scene_ && playing; }
    [[nodiscard]] bool Playing() const noexcept { return playing_; }
    [[nodiscard]] float Phase() const noexcept { return phase_; }
    [[nodiscard]] kb::scene::SceneEntity Owner() const noexcept { return owner_; }
    [[nodiscard]] const kb::scene::Scene* Scene() const noexcept { return scene_.get(); }
    [[nodiscard]] kb::scene::Scene* MutableScene() noexcept { return scene_.get(); }
    void SetReleaseHandler(std::function<void(const kb::scene::Scene&)> handler) { release_ = std::move(handler); }
    void Clear() noexcept;

private:
    std::unique_ptr<kb::scene::Scene> scene_;
    kb::scene::SceneEntity owner_{};
    float x_ = 0.0F, y_ = 0.0F, phase_ = 0.0F;
    bool playing_ = false;
    std::function<void(const kb::scene::Scene&)> release_;
};
} // namespace kb::editor
