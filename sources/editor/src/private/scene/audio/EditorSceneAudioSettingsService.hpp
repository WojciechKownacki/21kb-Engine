#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

class EditorSceneAudioSettingsService {
public:
    using Mutation = std::function<bool()>;
    using Executor = std::function<bool(std::string, Mutation)>;

    EditorSceneAudioSettingsService(kb::scene::Scene& scene, Executor executor);

    [[nodiscard]] bool SetSceneAudioMixer(kb::assets::AssetId id);
    [[nodiscard]] bool SetSceneAudioSnapshot(std::string_view snapshot);
    [[nodiscard]] bool SetSceneAudioOcclusion(const kb::scene::AudioOcclusionSettings& settings);
    static void ResetForNewDocument(kb::scene::Scene& scene) noexcept;
    static void PrepareDocument(kb::scene::Scene& scene);

private:
    [[nodiscard]] bool IsMixerCandidateValid(kb::assets::AssetId id);

    kb::scene::Scene& scene_;
    Executor executor_;
};

} // namespace kb::editor
