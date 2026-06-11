#include "MiniaudioSceneSystem.hpp"

#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "runtime/MiniaudioPlaybackBackend.hpp"

namespace kb::audio_miniaudio {

class MiniaudioSceneSystem::Impl final {
public:
    MiniaudioPlaybackBackend backend;
};

MiniaudioSceneSystem::MiniaudioSceneSystem()
    : impl_(std::make_unique<Impl>()) {}

MiniaudioSceneSystem::~MiniaudioSceneSystem() = default;

MiniaudioSceneSystem::MiniaudioSceneSystem(MiniaudioSceneSystem&&) noexcept = default;

MiniaudioSceneSystem& MiniaudioSceneSystem::operator=(MiniaudioSceneSystem&&) noexcept = default;

void MiniaudioSceneSystem::OnCreate(kb::scene::SceneSystemContext& context) {
    impl_->backend.OnCreate();
    kb::audio::AudioPlayback::RegisterBackend(context.GetScene(), impl_->backend);
}

void MiniaudioSceneSystem::OnUpdate(kb::scene::SceneSystemContext& context) {
    impl_->backend.OnUpdate(context);
}

void MiniaudioSceneSystem::OnDestroy(kb::scene::SceneSystemContext& context) {
    kb::audio::AudioPlayback::UnregisterBackend(context.GetScene(), impl_->backend);
    impl_->backend.Shutdown();
}

} // namespace kb::audio_miniaudio
