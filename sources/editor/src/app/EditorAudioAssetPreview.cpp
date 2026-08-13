#include "app/EditorAudioAssetPreview.hpp"

#include "engine/assets/AssetKind.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

namespace kb::editor {
namespace {

struct AudioPreviewState final {
    kb::assets::AssetId assetId{};
    std::uint64_t sceneId = 0U;
    std::uint64_t voiceId = 0U;
};

AudioPreviewState& State() noexcept {
    static AudioPreviewState state;
    return state;
}

void StopAndClear(kb::scene::Scene& scene) noexcept {
    AudioPreviewState& state = State();
    if (state.sceneId == scene.Id() && state.voiceId != 0U) {
        static_cast<void>(kb::audio::AudioPlayback::StopVoice(scene, state.voiceId));
    }
    state = {};
}

} // namespace

bool EditorAudioAssetPreview::Play(kb::scene::Scene& scene, kb::assets::AssetId assetId) {
    StopAndClear(scene);

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(assetId);
    if (metadata == nullptr || !kb::assets::AssetMatchesKind(*metadata, kb::assets::AssetKind::Audio)) {
        return false;
    }

    const kb::audio::AudioPlayResult result = kb::audio::AudioPlayback::PlayOneShot(scene, kb::audio::AudioPlayDesc{
        .clipAssetId = assetId.value,
        .spatial = false,
    });
    if (!result.Succeeded()) {
        return false;
    }

    State() = AudioPreviewState{ .assetId = assetId, .sceneId = scene.Id(), .voiceId = result.voiceId };
    return true;
}

bool EditorAudioAssetPreview::Tick(kb::scene::Scene& scene) noexcept {
    AudioPreviewState& state = State();
    if (state.voiceId == 0U) {
        return false;
    }
    if (state.sceneId != scene.Id()) {
        state = {};
        return true;
    }

    if (kb::audio::AudioPlayback::IsVoicePlaying(scene, state.voiceId)) {
        return false;
    }
    StopAndClear(scene);
    return true;
}

bool EditorAudioAssetPreview::HasActivePreview() noexcept {
    return State().voiceId != 0U;
}

bool EditorAudioAssetPreview::IsPlaying(kb::assets::AssetId assetId) noexcept {
    const AudioPreviewState& state = State();
    return assetId.IsValid() && state.assetId == assetId && state.voiceId != 0U;
}

} // namespace kb::editor
