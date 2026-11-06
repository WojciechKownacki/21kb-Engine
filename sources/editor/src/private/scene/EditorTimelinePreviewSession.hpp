#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/TimelineAsset.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/project/ProjectDescriptor.hpp"

#include <memory>
#include <functional>
#include <string>

namespace kb::editor {

class EditorTimelinePreviewSession {
public:
    ~EditorTimelinePreviewSession() { Clear(); }
    void SetReleaseHandler(std::function<void(const kb::scene::Scene&)> handler) { releaseHandler_ = std::move(handler); }
    [[nodiscard]] bool Open(kb::scene::Scene& source, kb::scene::SceneEntity owner,
        const kb::project::ProjectDescriptor& project, kb::assets::AssetId assetId,
        const kb::scene::TimelineAsset& working, std::string& error, float initialTime = 0.0F);
    [[nodiscard]] bool Seek(float time, std::string& error);
    [[nodiscard]] bool Play(float rate, bool loop, std::string& error);
    void Pause() noexcept;
    [[nodiscard]] bool Tick(float deltaSeconds, std::string& error);
    void Clear() noexcept;
    [[nodiscard]] const kb::scene::Scene* Scene() const noexcept { return scene_.get(); }
    [[nodiscard]] kb::scene::Scene* Scene() noexcept { return scene_.get(); }
    [[nodiscard]] const kb::script::ScriptRuntimeHost* Scripts() const noexcept { return scripts_.get(); }
    [[nodiscard]] kb::scene::SceneEntity Owner() const noexcept { return owner_; }
    [[nodiscard]] float Time() const noexcept;
    [[nodiscard]] bool Playing() const noexcept;

private:
    std::unique_ptr<kb::scene::Scene> scene_;
    std::unique_ptr<kb::script::ScriptRuntimeHost> scripts_;
    kb::scene::SceneEntity owner_{};
    std::uint64_t instance_ = 0;
    std::function<void(const kb::scene::Scene&)> releaseHandler_;
};

} // namespace kb::editor
