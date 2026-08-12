#include "assets/MiniaudioClipResolver.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/audio/AudioMixerAssetIO.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneAudioListenerAccess.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "playback/MiniaudioVoicePool.hpp"
#include "runtime/MiniaudioEngine.hpp"
#include "runtime/MiniaudioPlaybackBackend.hpp"
#include "runtime/MiniaudioSound.hpp"
#include "scene/MiniaudioBusRegistry.hpp"
#include "scene/MiniaudioListenerSynchronizer.hpp"
#include "scene/MiniaudioSourceRegistry.hpp"

#include <cmath>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

void Require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{ std::string{ message } };
    }
}

[[nodiscard]] bool Near(float lhs, float rhs, float tolerance = 0.001F) noexcept {
    return std::abs(lhs - rhs) <= tolerance;
}

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_audio_runtime_tests";
}

void WriteU16(std::ofstream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xFFU));
    output.put(static_cast<char>((value >> 8U) & 0xFFU));
}

void WriteU32(std::ofstream& output, std::uint32_t value) {
    output.put(static_cast<char>(value & 0xFFU));
    output.put(static_cast<char>((value >> 8U) & 0xFFU));
    output.put(static_cast<char>((value >> 16U) & 0xFFU));
    output.put(static_cast<char>((value >> 24U) & 0xFFU));
}

void WriteSilentWav(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    constexpr std::uint16_t channels = 1U;
    constexpr std::uint32_t sampleRate = 44100U;
    constexpr std::uint16_t bitsPerSample = 16U;
    constexpr std::uint32_t sampleCount = sampleRate;
    constexpr std::uint32_t bytesPerSample = bitsPerSample / 8U;
    constexpr std::uint32_t dataSize = sampleCount * channels * bytesPerSample;
    constexpr std::uint32_t byteRate = sampleRate * channels * bytesPerSample;
    constexpr std::uint16_t blockAlign = channels * bytesPerSample;
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    Require(output.is_open(), "Audio test wave could not be opened");
    output.write("RIFF", 4);
    WriteU32(output, 36U + dataSize);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    WriteU32(output, 16U);
    WriteU16(output, 1U);
    WriteU16(output, channels);
    WriteU32(output, sampleRate);
    WriteU32(output, byteRate);
    WriteU16(output, blockAlign);
    WriteU16(output, bitsPerSample);
    output.write("data", 4);
    WriteU32(output, dataSize);
    for (std::uint32_t index = 0U; index < dataSize; ++index) {
        output.put('\0');
    }
    Require(output.good(), "Audio test wave could not be written");
}

void WriteTruncatedAudio(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    Require(output.is_open(), "Truncated audio fixture could not be opened");
    output.write("RIFF", 4);
    Require(output.good(), "Truncated audio fixture could not be written");
}

void RegisterClip(kb::scene::Scene& scene, std::uint64_t id, const std::filesystem::path& path) {
    Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                .id = kb::assets::AssetId{ id },
                .type = "AudioClip",
                .name = "RuntimeClip",
                .virtualPath = "/Audio/Runtime" + std::to_string(id) + ".wav",
                .physicalPath = path.string(),
                .contentHash = id,
            }),
        "Audio test clip registration failed");
}

template <typename Resolver>
void RunClipResolverValidationTest(const std::filesystem::path& clipPath, Resolver& resolver);

[[nodiscard]] kb::scene::TransformComponent TransformAt(float x, float y = 0.0F, float z = 0.0F) noexcept {
    return kb::scene::TransformComponent{
        .localPosition = { x, y, z },
        .worldPosition = { x, y, z },
        .worldDirty = false,
    };
}

class OfflineEnginePump final {
public:
    explicit OfflineEnginePump(kb::audio_miniaudio::MiniaudioEngine& engine)
        : thread_([this, &engine]() {
              std::array<float, 512U> frames{};
              while (!stop_.load(std::memory_order_relaxed)) {
                  static_cast<void>(ma_engine_read_pcm_frames(&engine.Native(), frames.data(), frames.size() / 2U, nullptr));
                  std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
              }
          }) {}

    ~OfflineEnginePump() {
        stop_.store(true, std::memory_order_relaxed);
        thread_.join();
    }

    OfflineEnginePump(const OfflineEnginePump&) = delete;
    OfflineEnginePump& operator=(const OfflineEnginePump&) = delete;

private:
    std::atomic<bool> stop_{ false };
    std::thread thread_;
};

void RunSoundStateTest(const std::filesystem::path& clipPath) {
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    Require(engine.Status() == kb::audio::AudioDeviceStatus::NoPlaybackDevice, "Controlled no-device engine did not report its state");

    kb::audio_miniaudio::MiniaudioSound sound;
    Require(sound.InitializeFromFile(engine.Native(), clipPath, true) == MA_SUCCESS, "Spatial audio test sound could not be initialized");
    Require(sound.FlatForTesting() != nullptr, "Spatial blend did not allocate its flat branch");
    sound.Apply(kb::audio_miniaudio::MiniaudioSoundSettings{
        .volume = 2.0F,
        .mute = true,
        .spatial = true,
        .pan = 0.25F,
        .spatialBlend = 0.25F,
        .position = { 1.0F, 2.0F, 3.0F },
        .velocity = { 4.0F, 5.0F, 6.0F },
    });
    Require(Near(ma_sound_get_volume(sound.PrimaryForTesting()), 0.0F) && Near(ma_sound_get_volume(sound.FlatForTesting()), 0.0F),
        "Mute did not silence both spatial branches");
    sound.SetVolume(4.0F);
    Require(Near(ma_sound_get_volume(sound.PrimaryForTesting()), 0.0F), "Volume unexpectedly cleared mute");
    sound.SetMute(false);
    Require(Near(ma_sound_get_volume(sound.PrimaryForTesting()), 1.0F) && Near(ma_sound_get_volume(sound.FlatForTesting()), 3.0F),
        "Spatial blend volumes were not restored independently of mute");
    sound.SetPan(std::numeric_limits<float>::infinity());
    Require(Near(ma_sound_get_pan(sound.FlatForTesting()), 0.0F), "Non-finite pan was not normalized deterministically");
    sound.SetPan(4.0F);
    Require(Near(ma_sound_get_pan(sound.FlatForTesting()), 1.0F), "Pan was not clamped");
    const ma_vec3f velocity = ma_sound_get_velocity(sound.PrimaryForTesting());
    Require(Near(velocity.x, 4.0F) && Near(velocity.y, 5.0F) && Near(velocity.z, 6.0F), "Native sound velocity was not applied");
    sound.SetVelocity({ std::numeric_limits<float>::infinity(), 1.0F, 2.0F });
    const ma_vec3f normalizedVelocity = ma_sound_get_velocity(sound.PrimaryForTesting());
    Require(Near(normalizedVelocity.x, 0.0F) && Near(normalizedVelocity.y, 1.0F), "Non-finite native sound velocity was not normalized");

    kb::audio_miniaudio::MiniaudioSound flatSound;
    Require(flatSound.InitializeFromFile(engine.Native(), clipPath, false) == MA_SUCCESS && flatSound.FlatForTesting() == nullptr,
        "A purely flat sound allocated an unnecessary second branch");
}

void RunVoiceStateTest(const std::filesystem::path& clipPath) {
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    OfflineEnginePump pump{ engine };
    kb::scene::Scene scene;
    RegisterClip(scene, 8101U, clipPath);
    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    kb::audio_miniaudio::MiniaudioVoicePool pool;
    const kb::audio::AudioPlayResult played = pool.PlayOneShot(engine.Native(), scene, kb::audio::AudioPlayDesc{
        .clipAssetId = 8101U,
        .volume = 0.75F,
        .mute = true,
        .loop = true,
        .spatial = false,
        .pan = 0.25F,
        .velocity = { 1.0F, 2.0F, 3.0F },
    }, resolver, nullptr);
    Require(played.Succeeded(), "Offline one-shot could not be created for deterministic state verification");
    kb::audio_miniaudio::MiniaudioSound* voiceSound = pool.SoundForTesting(played.voiceId);
    Require(voiceSound != nullptr && Near(ma_sound_get_volume(voiceSound->PrimaryForTesting()), 0.0F), "Initial one-shot mute was not applied natively");
    Require(pool.SetVoiceVolume(played.voiceId, 0.5F) && Near(ma_sound_get_volume(voiceSound->PrimaryForTesting()), 0.0F),
        "One-shot volume setter changed mute state");
    Require(pool.SetVoiceMute(played.voiceId, false) && Near(ma_sound_get_volume(voiceSound->PrimaryForTesting()), 0.5F),
        "One-shot mute setter did not restore normalized volume");
    Require(pool.SetVoicePan(played.voiceId, 0.5F) && Near(ma_sound_get_pan(voiceSound->PrimaryForTesting()), 0.5F),
        "One-shot pan setter was not propagated to native state");
    Require(pool.SetVoicePitch(played.voiceId, 1.25F)
            && Near(ma_sound_get_pitch(voiceSound->PrimaryForTesting()), 1.25F),
        "One-shot pitch setter was not propagated to native state");
    const float nativeVolume = ma_sound_get_volume(voiceSound->PrimaryForTesting());
    const float nativePan = ma_sound_get_pan(voiceSound->PrimaryForTesting());
    const float nativePitch = ma_sound_get_pitch(voiceSound->PrimaryForTesting());
    const float nativePlaybackSeconds = voiceSound->PlaybackSeconds();
    Require(!pool.SeekVoice(played.voiceId, -0.01F)
            && !pool.SeekVoice(played.voiceId, std::numeric_limits<float>::quiet_NaN())
            && !pool.SetVoiceVolume(played.voiceId, -0.01F)
            && !pool.SetVoiceVolume(played.voiceId, std::numeric_limits<float>::infinity())
            && !pool.SetVoicePan(played.voiceId, -1.01F)
            && !pool.SetVoicePan(played.voiceId, std::numeric_limits<float>::quiet_NaN())
            && !pool.SetVoicePitch(played.voiceId, 0.009F)
            && !pool.SetVoicePitch(played.voiceId, std::numeric_limits<float>::infinity()),
        "Invalid direct voice controls were accepted by the pool");
    Require(Near(ma_sound_get_volume(voiceSound->PrimaryForTesting()), nativeVolume)
            && Near(ma_sound_get_pan(voiceSound->PrimaryForTesting()), nativePan)
            && Near(ma_sound_get_pitch(voiceSound->PrimaryForTesting()), nativePitch)
            && Near(voiceSound->PlaybackSeconds(), nativePlaybackSeconds, 0.02F),
        "Rejected direct voice controls mutated native sound state");
    Require(pool.PauseVoice(played.voiceId), "Playing one-shot could not be paused");
    Require(!pool.PauseVoice(played.voiceId), "Already paused one-shot accepted a second pause");
    Require(pool.ResumeVoice(played.voiceId), "Paused one-shot could not be resumed");
    Require(!pool.ResumeVoice(played.voiceId), "Playing one-shot accepted resume without a paused state");

    const kb::audio::AudioPlayResult ended = pool.PlayOneShot(engine.Native(), scene, kb::audio::AudioPlayDesc{
        .clipAssetId = 8101U,
        .volume = 0.0F,
        .loop = false,
        .spatial = false,
    }, resolver, nullptr);
    Require(ended.Succeeded(), "Finite one-shot could not be created for transport-state verification");
    kb::audio_miniaudio::MiniaudioSound* endedSound = pool.SoundForTesting(ended.voiceId);
    Require(endedSound != nullptr && endedSound->SeekSeconds(2.0F) == MA_SUCCESS,
        "Finite one-shot could not seek to its natural end");
    for (std::uint32_t attempt = 0U; attempt < 100U && !endedSound->AtEnd(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 2 });
    }
    Require(endedSound->AtEnd(), "Finite one-shot did not reach its natural end");
    Require(!pool.PauseVoice(ended.voiceId) && !pool.ResumeVoice(ended.voiceId),
        "Naturally ended one-shot accepted pause or resume");

    const std::size_t voiceCountBeforeInvalidPlay = pool.VoiceCountForTesting();
    const auto rejectedPlay = [&engine, &scene, &resolver, &pool](kb::audio::AudioPlayDesc desc) {
        return pool.PlayOneShot(engine.Native(), scene, desc, resolver, nullptr);
    };
    kb::audio::AudioPlayDesc invalidPlay{
        .clipAssetId = 8101U,
        .loop = true,
        .spatial = false,
    };
    invalidPlay.pan = 2.0F;
    Require(!rejectedPlay(invalidPlay).Succeeded(), "Out-of-range one-shot pan was accepted");
    invalidPlay.pan = 0.0F;
    invalidPlay.position.x = std::numeric_limits<float>::infinity();
    Require(!rejectedPlay(invalidPlay).Succeeded(), "Non-finite one-shot position was accepted");
    invalidPlay.position = {};
    invalidPlay.attenuationModel = static_cast<kb::audio::AudioAttenuationModel>(99);
    Require(!rejectedPlay(invalidPlay).Succeeded(), "Unknown one-shot attenuation model was accepted");
    invalidPlay.attenuationModel = kb::audio::AudioAttenuationModel::Inverse;
    invalidPlay.outputBus = "invalid bus";
    Require(!rejectedPlay(invalidPlay).Succeeded(), "Invalid one-shot route token was accepted");
    invalidPlay.outputBus.clear();
    invalidPlay.clipAssetId = 0U;
    const kb::audio::AudioPlayResult invalidClip = rejectedPlay(invalidPlay);
    Require(!invalidClip.Succeeded() && invalidClip.error == "audio clip id is invalid",
        "Direct pool validation did not distinguish an invalid clip");
    Require(pool.VoiceCountForTesting() == voiceCountBeforeInvalidPlay
            && pool.IsVoicePlaying(played.voiceId),
        "Rejected one-shot request changed the voice pool or stole a live voice");

    const kb::scene::SceneObject markerTarget = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "Pool Marker Target" });
    Require(pool.AddVoiceMarker(scene, played.voiceId, "beat", 0.25F, markerTarget.Entity())
            && pool.MarkerCountForTesting(played.voiceId) == 1U,
        "Valid direct pool marker was rejected");
    const kb::scene::SceneObject deadTarget = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "Dead Pool Marker Target" });
    scene.Entities().Destroy(deadTarget.Entity());
    const std::string embeddedNullMarker{ "bad\0name", 8U };
    const std::string controlMarker{ "bad\nname" };
    const std::string oversizedMarker(kb::audio::kMaxAudioVoiceMarkerNameBytes + 1U, 'm');
    Require(!pool.AddVoiceMarker(scene, played.voiceId, {}, 0.25F, markerTarget.Entity())
            && !pool.AddVoiceMarker(scene, played.voiceId, embeddedNullMarker, 0.25F, markerTarget.Entity())
            && !pool.AddVoiceMarker(scene, played.voiceId, controlMarker, 0.25F, markerTarget.Entity())
            && !pool.AddVoiceMarker(scene, played.voiceId, oversizedMarker, 0.25F, markerTarget.Entity())
            && !pool.AddVoiceMarker(scene, played.voiceId, "late", -0.01F, markerTarget.Entity())
            && !pool.AddVoiceMarker(scene, played.voiceId, "late", std::numeric_limits<float>::quiet_NaN(), markerTarget.Entity())
            && !pool.AddVoiceMarker(scene, played.voiceId, "dead", 0.25F, deadTarget.Entity())
            && pool.MarkerCountForTesting(played.voiceId) == 1U,
        "Invalid direct pool marker mutated marker state");
    RunClipResolverValidationTest(clipPath, resolver);
}

void RunTransactionalPlaybackFailureTest(const std::filesystem::path& clipPath) {
    const std::filesystem::path corruptPath = TestRoot() / "Truncated.wav";
    WriteTruncatedAudio(corruptPath);

    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    OfflineEnginePump pump{ engine };
    kb::scene::Scene scene;
    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    kb::audio_miniaudio::MiniaudioVoicePool pool;

    constexpr std::uint64_t firstClip = 8500U;
    constexpr std::size_t clipCount = 8U;
    constexpr std::size_t voicesPerClip = 8U;
    for (std::size_t clipIndex = 0U; clipIndex < clipCount; ++clipIndex) {
        const std::uint64_t clipId = firstClip + clipIndex;
        RegisterClip(scene, clipId, clipPath);
        for (std::size_t voiceIndex = 0U; voiceIndex < voicesPerClip; ++voiceIndex) {
            const kb::audio::AudioPlayResult result = pool.PlayOneShot(
                engine.Native(), scene,
                kb::audio::AudioPlayDesc{
                    .clipAssetId = clipId,
                    .volume = 0.4F,
                    .pitch = 1.2F,
                    .loop = true,
                    .spatial = false,
                    .pan = 0.3F,
                    .priority = 100U,
                },
                resolver, nullptr);
            Require(result.Succeeded(), "Transactional pool fixture did not fill to capacity");
        }
    }

    const std::vector<std::uint64_t> idsBefore = pool.VoiceIdsForTesting();
    Require(idsBefore.size() == clipCount * voicesPerClip,
        "Transactional pool fixture did not retain its full capacity");
    kb::audio_miniaudio::MiniaudioSound* retainedSound = pool.SoundForTesting(idsBefore.front());
    Require(retainedSound != nullptr, "Transactional pool fixture lost its oldest voice");
    const float volumeBefore = ma_sound_get_volume(retainedSound->PrimaryForTesting());
    const float panBefore = ma_sound_get_pan(retainedSound->PrimaryForTesting());
    const float pitchBefore = ma_sound_get_pitch(retainedSound->PrimaryForTesting());

    constexpr std::uint64_t corruptClip = 8598U;
    RegisterClip(scene, corruptClip, corruptPath);
    const kb::audio::AudioPlayResult rejected = pool.PlayOneShot(
        engine.Native(), scene,
        kb::audio::AudioPlayDesc{
            .clipAssetId = corruptClip,
            .volume = 0.9F,
            .pitch = 1.5F,
            .loop = true,
            .spatial = false,
            .priority = 255U,
        },
        resolver, nullptr);
    Require(!rejected.Succeeded() && rejected.error == "audio voice could not be created",
        "Truncated one-shot payload was not rejected during candidate initialization");
    Require(pool.VoiceIdsForTesting() == idsBefore && pool.VoiceCountForTesting() == idsBefore.size(),
        "Rejected one-shot initialization evicted or reordered a live voice");
    Require(pool.SoundForTesting(idsBefore.front()) == retainedSound
            && pool.IsVoicePlaying(idsBefore.front())
            && Near(ma_sound_get_volume(retainedSound->PrimaryForTesting()), volumeBefore)
            && Near(ma_sound_get_pan(retainedSound->PrimaryForTesting()), panBefore)
            && Near(ma_sound_get_pitch(retainedSound->PrimaryForTesting()), pitchBefore),
        "Rejected one-shot initialization changed the retained native voice state");

    constexpr std::uint64_t validCandidateClip = 8599U;
    RegisterClip(scene, validCandidateClip, clipPath);
    const kb::audio::AudioPlayResult admitted = pool.PlayOneShot(
        engine.Native(), scene,
        kb::audio::AudioPlayDesc{
            .clipAssetId = validCandidateClip,
            .volume = 0.0F,
            .loop = true,
            .spatial = false,
            .priority = 255U,
        },
        resolver, nullptr);
    Require(admitted.Succeeded() && pool.VoiceCountForTesting() == idsBefore.size()
            && !pool.IsVoicePlaying(idsBefore.front())
            && pool.IsVoicePlaying(admitted.voiceId),
        "A valid one-shot candidate did not commit after a rejected corrupt candidate");
}

void RunInitialFrameSoundTest(const std::filesystem::path& clipPath) {
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    kb::audio_miniaudio::MiniaudioSound sound;
    Require(sound.InitializeFromFile(engine.Native(), clipPath, true, nullptr, 11025U) == MA_SUCCESS,
        "Audio test sound could not initialize at a preserved frame");
}

void RunListenerAndAttachedVelocityTest(const std::filesystem::path& clipPath) {
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    kb::scene::Scene scene;
    RegisterClip(scene, 8201U, clipPath);

    const kb::scene::SceneObject listener = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Listener", .transform = TransformAt(1.0F) });
    scene.Components().AudioListeners().Set(listener.Entity(), kb::scene::AudioListenerComponent{});
    kb::audio_miniaudio::MiniaudioListenerSynchronizer synchronizer;
    kb::scene::SceneSystemContext firstContext{ scene, 0.5F };
    Require(synchronizer.Sync(engine.Native(), firstContext).active, "Active listener was not selected");
    ma_vec3f listenerVelocity = ma_engine_listener_get_velocity(&engine.Native(), 0U);
    Require(Near(listenerVelocity.x, 0.0F), "First listener tick produced a velocity spike");

    kb::scene::TransformComponent movedListener = TransformAt(3.0F);
    constexpr float halfSqrt = 0.70710678F;
    movedListener.localRotation = { 0.0F, 0.0F, halfSqrt, halfSqrt };
    movedListener.worldRotation = movedListener.localRotation;
    scene.Transforms().Set(listener.Entity(), movedListener);
    kb::scene::SceneSystemContext movedContext{ scene, 0.5F };
    static_cast<void>(synchronizer.Sync(engine.Native(), movedContext));
    listenerVelocity = ma_engine_listener_get_velocity(&engine.Native(), 0U);
    const ma_vec3f up = ma_engine_listener_get_world_up(&engine.Native(), 0U);
    Require(Near(listenerVelocity.x, 4.0F), "Listener velocity did not use scene delta");
    Require(std::abs(up.x) > 0.9F && std::abs(up.y) < 0.1F, "Listener up vector did not rotate with its transform");

    const kb::scene::SceneObject preferredPrimaryUser = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "Preferred Primary User Listener", .transform = TransformAt(8.0F) });
    scene.Components().AudioListeners().Set(preferredPrimaryUser.Entity(), kb::scene::AudioListenerComponent{
        .priority = 10,
        .localUser = kb::input::kPrimaryLocalUser,
        .primary = false,
        .enabled = true,
    });
    const kb::scene::SceneObject secondUser = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "Second User Listener", .transform = TransformAt(20.0F) });
    scene.Components().AudioListeners().Set(secondUser.Entity(), kb::scene::AudioListenerComponent{
        .priority = 100,
        .localUser = kb::input::LocalUserId{ 1U },
        .primary = true,
        .enabled = true,
    });
    kb::scene::SceneSystemContext userSelectionContext{ scene, 0.5F };
    static_cast<void>(synchronizer.Sync(engine.Native(), userSelectionContext));
    Require(Near(ma_engine_listener_get_position(&engine.Native(), 0U).x, 8.0F),
        "Listener priority was not resolved within the selected local user");

    kb::scene::SceneAudioListenerAccess::SetLocalUser(scene, kb::input::LocalUserId{ 1U });
    static_cast<void>(synchronizer.Sync(engine.Native(), userSelectionContext));
    Require(Near(ma_engine_listener_get_position(&engine.Native(), 0U).x, 20.0F)
            && Near(ma_engine_listener_get_velocity(&engine.Native(), 0U).x, 0.0F),
        "Switching listener local user did not select its listener with zero velocity");
    // Rebind the same entity across a local-user switch. Entity equality alone must not
    // carry velocity across the selection boundary.
    kb::scene::AudioListenerComponent rebound = *scene.Components().AudioListeners().TryGet(secondUser.Entity());
    rebound.localUser = kb::input::LocalUserId{ 2U };
    scene.Components().AudioListeners().Set(secondUser.Entity(), rebound);
    scene.Transforms().Set(secondUser.Entity(), TransformAt(24.0F));
    kb::scene::SceneAudioListenerAccess::SetLocalUser(scene, kb::input::LocalUserId{ 2U });
    static_cast<void>(synchronizer.Sync(engine.Native(), userSelectionContext));
    Require(Near(ma_engine_listener_get_position(&engine.Native(), 0U).x, 24.0F)
            && Near(ma_engine_listener_get_velocity(&engine.Native(), 0U).x, 0.0F),
        "Listener rebinding across a local-user switch produced a velocity spike");
    kb::scene::SceneAudioListenerAccess::SetLocalUser(scene, kb::input::LocalUserId{ 3U });
    Require(!synchronizer.Sync(engine.Native(), userSelectionContext).active
            && ma_engine_listener_is_enabled(&engine.Native(), 0U) == MA_FALSE,
        "A local user without a matching listener retained stale listener state");
    kb::scene::SceneAudioListenerAccess::SetLocalUser(scene, kb::input::LocalUserId{ 2U });
    static_cast<void>(synchronizer.Sync(engine.Native(), userSelectionContext));
    rebound.enabled = false;
    scene.Components().AudioListeners().Set(secondUser.Entity(), rebound);
    Require(!synchronizer.Sync(engine.Native(), userSelectionContext).active,
        "Disabled matching listener remained active");
    rebound.enabled = true;
    scene.Components().AudioListeners().Set(secondUser.Entity(), rebound);
    scene.Entities().SetActive(secondUser.Entity(), false);
    Require(!synchronizer.Sync(engine.Native(), userSelectionContext).active,
        "Inactive matching listener remained active");
    scene.Entities().SetActive(secondUser.Entity(), true);

    kb::scene::SceneAudioListenerAccess::SetLocalUser(scene, kb::input::kPrimaryLocalUser);
    static_cast<void>(synchronizer.Sync(engine.Native(), userSelectionContext));
    Require(Near(ma_engine_listener_get_position(&engine.Native(), 0U).x, 8.0F)
            && Near(ma_engine_listener_get_velocity(&engine.Native(), 0U).x, 0.0F),
        "Returning to the primary local user did not reset listener velocity");
    scene.Entities().SetActive(preferredPrimaryUser.Entity(), false);
    static_cast<void>(synchronizer.Sync(engine.Native(), userSelectionContext));
    Require(Near(ma_engine_listener_get_position(&engine.Native(), 0U).x, 3.0F)
            && Near(ma_engine_listener_get_velocity(&engine.Native(), 0U).x, 0.0F),
        "Inactive preferred listener did not fall back within its local user");
    scene.Entities().Destroy(preferredPrimaryUser.Entity());
    scene.Entities().Destroy(secondUser.Entity());

    scene.Entities().SetActive(listener.Entity(), false);
    kb::scene::SceneSystemContext inactiveContext{ scene, 0.5F };
    Require(!synchronizer.Sync(engine.Native(), inactiveContext).active && ma_engine_listener_is_enabled(&engine.Native(), 0U) == MA_FALSE,
        "Inactive listener remained enabled");
    scene.Entities().SetActive(listener.Entity(), true);
    kb::scene::SceneSystemContext reactivatedContext{ scene, 0.5F };
    static_cast<void>(synchronizer.Sync(engine.Native(), reactivatedContext));
    listenerVelocity = ma_engine_listener_get_velocity(&engine.Native(), 0U);
    Require(Near(listenerVelocity.x, 0.0F), "Reactivated listener produced a velocity spike");
    scene.Transforms().Set(listener.Entity(), TransformAt(std::numeric_limits<float>::max()));
    kb::scene::SceneSystemContext overflowListenerContext{ scene, std::numeric_limits<float>::denorm_min() };
    static_cast<void>(synchronizer.Sync(engine.Native(), overflowListenerContext));
    listenerVelocity = ma_engine_listener_get_velocity(&engine.Native(), 0U);
    Require(Near(listenerVelocity.x, 0.0F), "Overflowing listener velocity was not normalized");
    scene.Transforms().Set(listener.Entity(), TransformAt(std::numeric_limits<float>::infinity()));
    kb::scene::SceneSystemContext invalidListenerContext{ scene, 0.5F };
    static_cast<void>(synchronizer.Sync(engine.Native(), invalidListenerContext));
    const ma_vec3f sanitizedListenerPosition = ma_engine_listener_get_position(&engine.Native(), 0U);
    listenerVelocity = ma_engine_listener_get_velocity(&engine.Native(), 0U);
    Require(Near(sanitizedListenerPosition.x, 0.0F) && Near(listenerVelocity.x, 0.0F),
        "Non-finite listener transform reached native state");

    const kb::scene::SceneObject owner = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Owner", .transform = TransformAt(2.0F) });
    kb::audio_miniaudio::MiniaudioVoicePool pool;
    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    kb::audio::AudioPlayDesc attached{
        .clipAssetId = 8201U,
        .volume = 1.0F,
        .loop = true,
        .spatial = true,
        .position = { 99.0F, 0.0F, 0.0F },
        .velocity = { 99.0F, 0.0F, 0.0F },
    };
    attached.ownerEntityId = owner.Entity().Id();
    const kb::audio::AudioPlayResult attachedResult = pool.PlayOneShot(engine.Native(), scene, attached, resolver, nullptr);
    Require(attachedResult.Succeeded(), "Attached offline voice could not be created");
    kb::audio_miniaudio::MiniaudioSound* attachedSound = pool.SoundForTesting(attachedResult.voiceId);
    ma_vec3f position = ma_sound_get_position(attachedSound->PrimaryForTesting());
    ma_vec3f velocity = ma_sound_get_velocity(attachedSound->PrimaryForTesting());
    Require(Near(position.x, 2.0F) && Near(velocity.x, 0.0F), "Attached voice did not take its owner's initial pose with zero velocity");
    pool.SyncAttachedVoices(scene, nullptr, {}, 0.5F);
    velocity = ma_sound_get_velocity(attachedSound->PrimaryForTesting());
    Require(Near(velocity.x, 0.0F), "First attached-voice tick produced a velocity spike");
    scene.Transforms().Set(owner.Entity(), TransformAt(4.0F));
    pool.SyncAttachedVoices(scene, nullptr, {}, 0.5F);
    velocity = ma_sound_get_velocity(attachedSound->PrimaryForTesting());
    Require(Near(velocity.x, 4.0F), "Attached voice velocity did not follow its owner");
    scene.Transforms().Set(owner.Entity(), TransformAt(std::numeric_limits<float>::max()));
    pool.SyncAttachedVoices(scene, nullptr, {}, std::numeric_limits<float>::denorm_min());
    velocity = ma_sound_get_velocity(attachedSound->PrimaryForTesting());
    Require(Near(velocity.x, 0.0F), "Overflowing attached-voice velocity was not normalized");
    scene.Transforms().Set(owner.Entity(), TransformAt(std::numeric_limits<float>::infinity()));
    pool.SyncAttachedVoices(scene, nullptr, {}, 0.5F);
    position = ma_sound_get_position(attachedSound->PrimaryForTesting());
    Require(Near(position.x, 0.0F), "Non-finite attached-owner position reached native state");
}

void RunSourceLifecycleAndRoutingTest(const std::filesystem::path& clipPath, std::string_view checkpoint = {}) {
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    OfflineEnginePump pump{ engine };
    kb::scene::Scene scene;
    RegisterClip(scene, 8301U, clipPath);
    RegisterClip(scene, 8302U, clipPath);
    const std::filesystem::path corruptPath = TestRoot() / "SourceTruncated.wav";
    WriteTruncatedAudio(corruptPath);
    RegisterClip(scene, 8303U, corruptPath);
    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    kb::audio_miniaudio::MiniaudioBusRegistry buses;
    kb::audio_miniaudio::MiniaudioSourceRegistry sources;

    const kb::scene::SceneObject sourceObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Source", .transform = TransformAt(0.0F) });
    scene.Components().AudioSources().Set(sourceObject.Entity(), kb::scene::AudioSourceComponent{
        .clipAssetId = 8301U,
        .loop = true,
        .spatial = false,
        .autoplay = false,
    });
    const kb::audio::AudioSourceControlResult beforeTick = sources.PlaySource(engine.Native(), scene, sourceObject.Entity(), resolver, buses, true);
    Require(beforeTick.Succeeded() && beforeTick.playing, "Source Play did not work before its first audio tick");
    kb::scene::AudioSourceComponent* invalidSettingsSource = scene.Components().AudioSources().TryGet(sourceObject.Entity());
    invalidSettingsSource->pan = 2.0F;
    scene.Components().AudioSources().MarkModified(sourceObject.Entity());
    const kb::audio::AudioSourceControlResult invalidSettings =
        sources.PlaySource(engine.Native(), scene, sourceObject.Entity(), resolver, buses, true);
    Require(invalidSettings.status == kb::audio::AudioSourceControlStatus::InvalidSettings
            && sources.IsSourcePlaying(scene, sourceObject.Entity(), true).status
                == kb::audio::AudioSourceControlStatus::InvalidSettings,
        "Source transport did not reject an invalid component settings contract");
    invalidSettingsSource->pan = 0.0F;
    scene.Components().AudioSources().MarkModified(sourceObject.Entity());
    Require(sources.PauseSource(scene, sourceObject.Entity(), true).Succeeded(), "Source Pause failed");
    Require(sources.PauseSource(scene, sourceObject.Entity(), true).status == kb::audio::AudioSourceControlStatus::NotPlaying,
        "Already paused source accepted a second pause");
    Require(sources.ResumeSource(scene, sourceObject.Entity(), true).Succeeded(), "Source Resume failed");
    Require(sources.ResumeSource(scene, sourceObject.Entity(), true).status == kb::audio::AudioSourceControlStatus::NotPaused,
        "Playing source accepted resume without a paused state");
    Require(sources.StopSource(scene, sourceObject.Entity(), true).Succeeded(), "Source Stop failed");
    Require(sources.ResumeSource(scene, sourceObject.Entity(), true).status == kb::audio::AudioSourceControlStatus::NotPaused,
        "Stopped source accepted resume");
    Require(!sources.IsSourcePlaying(scene, sourceObject.Entity(), true).playing, "Stopped source still reported playing");
    Require(sources.PlaySource(engine.Native(), scene, sourceObject.Entity(), resolver, buses, true).Succeeded(), "Stopped source could not be played again");
    if (checkpoint == "source-control") {
        return;
    }

    kb::scene::SceneSystemContext firstContext{ scene, 0.25F };
    sources.Sync(engine.Native(), firstContext, resolver, buses, nullptr, {}, true);
    kb::audio_miniaudio::MiniaudioSound* firstSound = sources.SoundForTesting(sourceObject.Entity().Id());
    Require(firstSound != nullptr && firstSound->FlatForTesting() == nullptr, "Flat component source did not use one native sound");
    scene.Transforms().Set(sourceObject.Entity(), TransformAt(std::numeric_limits<float>::max()));
    kb::scene::SceneSystemContext overflowSourceContext{ scene, std::numeric_limits<float>::denorm_min() };
    sources.Sync(engine.Native(), overflowSourceContext, resolver, buses, nullptr, {}, true);
    ma_vec3f sourceVelocity = ma_sound_get_velocity(sources.SoundForTesting(sourceObject.Entity().Id())->PrimaryForTesting());
    Require(Near(sourceVelocity.x, 0.0F), "Overflowing component-source velocity was not normalized");
    scene.Transforms().Set(sourceObject.Entity(), TransformAt(std::numeric_limits<float>::infinity()));
    kb::scene::SceneSystemContext invalidSourceContext{ scene, 0.25F };
    sources.Sync(engine.Native(), invalidSourceContext, resolver, buses, nullptr, {}, true);
    const ma_vec3f sourcePosition = ma_sound_get_position(sources.SoundForTesting(sourceObject.Entity().Id())->PrimaryForTesting());
    sourceVelocity = ma_sound_get_velocity(sources.SoundForTesting(sourceObject.Entity().Id())->PrimaryForTesting());
    Require(Near(sourcePosition.x, 0.0F) && Near(sourceVelocity.x, 0.0F), "Non-finite component-source transform reached native state");
    scene.Transforms().Set(sourceObject.Entity(), TransformAt(0.0F));
    kb::scene::SceneSystemContext restoredSourceContext{ scene, 0.25F };
    sources.Sync(engine.Native(), restoredSourceContext, resolver, buses, nullptr, {}, true);
    firstSound = sources.SoundForTesting(sourceObject.Entity().Id());
    if (checkpoint == "source-first-sync") {
        return;
    }
    static_cast<void>(firstSound->SeekSeconds(0.25F));
    std::this_thread::sleep_for(std::chrono::milliseconds{ 20 });
    if (checkpoint == "source-seek") {
        return;
    }
    kb::scene::AudioSourceComponent* component = scene.Components().AudioSources().TryGet(sourceObject.Entity());
    component->spatial = true;
    scene.Components().AudioSources().MarkModified(sourceObject.Entity());
    kb::scene::SceneSystemContext spatialContext{ scene, 0.25F };
    sources.Sync(engine.Native(), spatialContext, resolver, buses, nullptr, {}, true);
    if (checkpoint == "source-spatial-sync") {
        return;
    }
    kb::audio_miniaudio::MiniaudioSound* spatialSound = sources.SoundForTesting(sourceObject.Entity().Id());
    const bool spatialStateValid = spatialSound != nullptr && spatialSound->FlatForTesting() != nullptr && spatialSound->PlaybackSeconds() >= 0.24F;
    if (checkpoint == "source-spatial-read") {
        return;
    }
    Require(spatialStateValid,
        "Spatial reconstruction did not preserve the source cursor");
    static_cast<void>(spatialSound->SeekSeconds(0.35F));
    std::this_thread::sleep_for(std::chrono::milliseconds{ 20 });
    sources.ReleaseNativeResources();
    Require(sources.SoundForTesting(sourceObject.Entity().Id()) == nullptr, "Routing release retained a native source handle");
    kb::scene::SceneSystemContext routingRebuildContext{ scene, 0.25F };
    sources.Sync(engine.Native(), routingRebuildContext, resolver, buses, nullptr, {}, true);
    spatialSound = sources.SoundForTesting(sourceObject.Entity().Id());
    Require(spatialSound != nullptr && spatialSound->PlaybackSeconds() >= 0.34F
            && sources.IsSourcePlaying(scene, sourceObject.Entity(), true).playing,
        "Routing reconstruction did not preserve a playing source cursor and state");
    Require(sources.PauseSource(scene, sourceObject.Entity(), true).Succeeded(), "Source could not pause before routing reconstruction");
    const float pausedPosition = spatialSound->PlaybackSeconds();
    sources.ReleaseNativeResources();
    kb::scene::SceneSystemContext pausedRebuildContext{ scene, 0.25F };
    sources.Sync(engine.Native(), pausedRebuildContext, resolver, buses, nullptr, {}, true);
    spatialSound = sources.SoundForTesting(sourceObject.Entity().Id());
    Require(spatialSound != nullptr && !sources.IsSourcePlaying(scene, sourceObject.Entity(), true).playing
            && spatialSound->PlaybackSeconds() + 0.001F >= pausedPosition,
        "Routing reconstruction did not preserve a paused source cursor and state");
    Require(sources.ResumeSource(scene, sourceObject.Entity(), true).Succeeded(), "Reconstructed paused source could not resume");
    component = scene.Components().AudioSources().TryGet(sourceObject.Entity());
    component->loop = false;
    scene.Components().AudioSources().MarkModified(sourceObject.Entity());
    kb::scene::SceneSystemContext nonLoopingContext{ scene, 0.25F };
    sources.Sync(engine.Native(), nonLoopingContext, resolver, buses, nullptr, {}, true);
    spatialSound = sources.SoundForTesting(sourceObject.Entity().Id());
    static_cast<void>(spatialSound->SeekSeconds(2.0F));
    std::this_thread::sleep_for(std::chrono::milliseconds{ 50 });
    Require(spatialSound->AtEnd(), "Audio test source did not reach its natural end");
    Require(sources.PauseSource(scene, sourceObject.Entity(), true).status == kb::audio::AudioSourceControlStatus::NotPlaying,
        "Naturally ended component source entered the paused state");
    Require(sources.ResumeSource(scene, sourceObject.Entity(), true).status == kb::audio::AudioSourceControlStatus::NotPaused,
        "Naturally ended component source accepted resume");
    Require(sources.PlaySource(engine.Native(), scene, sourceObject.Entity(), resolver, buses, true).Succeeded()
            && sources.IsSourcePlaying(scene, sourceObject.Entity(), true).playing
            && sources.SoundForTesting(sourceObject.Entity().Id())->PlaybackSeconds() < 0.1F,
        "Play did not restart a naturally ended component source from the beginning");
    if (checkpoint == "source-spatial") {
        return;
    }

    component = scene.Components().AudioSources().TryGet(sourceObject.Entity());
    component->loop = true;
    component->clipAssetId = 8303U;
    scene.Components().AudioSources().MarkModified(sourceObject.Entity());
    kb::audio_miniaudio::MiniaudioSound* retainedSource = sources.SoundForTesting(sourceObject.Entity().Id());
    const float retainedVolume = ma_sound_get_volume(retainedSource->PrimaryForTesting());
    const float retainedPan = ma_sound_get_pan(retainedSource->PrimaryForTesting());
    const float retainedPitch = ma_sound_get_pitch(retainedSource->PrimaryForTesting());
    Require(sources.PlaySource(engine.Native(), scene, sourceObject.Entity(), resolver, buses, true).status
            == kb::audio::AudioSourceControlStatus::SoundInitializationFailed,
        "Explicit source transport masked a corrupt changed-clip initialization failure");
    kb::scene::SceneSystemContext corruptClipContext{ scene, 0.25F };
    sources.Sync(engine.Native(), corruptClipContext, resolver, buses, nullptr, {}, true);
    Require(sources.SoundCountForTesting() == 1U
            && sources.SoundForTesting(sourceObject.Entity().Id()) == retainedSource
            && sources.IsSourcePlaying(scene, sourceObject.Entity(), true).playing,
        "A corrupt changed clip removed the live component-source record");
    Require(Near(ma_sound_get_volume(retainedSource->PrimaryForTesting()), retainedVolume)
            && Near(ma_sound_get_pan(retainedSource->PrimaryForTesting()), retainedPan)
            && Near(ma_sound_get_pitch(retainedSource->PrimaryForTesting()), retainedPitch),
        "A corrupt changed clip mutated the retained component-source native settings");

    component = scene.Components().AudioSources().TryGet(sourceObject.Entity());
    component->clipAssetId = 8302U;
    scene.Components().AudioSources().MarkModified(sourceObject.Entity());
    kb::scene::SceneSystemContext changedClipContext{ scene, 0.25F };
    sources.Sync(engine.Native(), changedClipContext, resolver, buses, nullptr, {}, true);
    Require(sources.SoundForTesting(sourceObject.Entity().Id()) != retainedSource
            && sources.SoundForTesting(sourceObject.Entity().Id())->PlaybackSeconds() < 0.05F
            && sources.IsSourcePlaying(scene, sourceObject.Entity(), true).playing,
        "A valid changed clip did not replace and restart after a corrupt candidate");
    if (checkpoint == "source-clip") {
        return;
    }

    scene.Entities().SetActive(sourceObject.Entity(), false);
    kb::scene::SceneSystemContext inactiveContext{ scene, 0.25F };
    sources.Sync(engine.Native(), inactiveContext, resolver, buses, nullptr, {}, true);
    Require(sources.SoundCountForTesting() == 0U, "Inactive source retained a native sound");
    scene.Entities().SetActive(sourceObject.Entity(), true);
    component = scene.Components().AudioSources().TryGet(sourceObject.Entity());
    component->autoplay = true;
    kb::scene::SceneSystemContext reactivatedContext{ scene, 0.25F };
    sources.Sync(engine.Native(), reactivatedContext, resolver, buses, nullptr, {}, true);
    Require(sources.SoundCountForTesting() == 1U && sources.IsSourcePlaying(scene, sourceObject.Entity(), true).playing,
        "Reactivated autoplay source did not recreate and start");
    scene.Components().AudioSources().Remove(sourceObject.Entity());
    kb::scene::SceneSystemContext removedContext{ scene, 0.25F };
    sources.Sync(engine.Native(), removedContext, resolver, buses, nullptr, {}, true);
    Require(sources.SoundCountForTesting() == 0U, "Removed source component retained a native sound");
    if (checkpoint == "source-lifecycle") {
        return;
    }

    kb::scene::AudioSourceComponent routed{ .clipAssetId = 8301U, .loop = true, .spatial = false, .autoplay = false };
    Require(kb::scene::SetAudioSourceOutputBus(routed, "Effects"),
        "Routed source fixture bus was invalid");
    scene.Components().AudioSources().Set(sourceObject.Entity(), routed);
    Require(sources.PlaySource(engine.Native(), scene, sourceObject.Entity(), resolver, buses, true).status
            == kb::audio::AudioSourceControlStatus::MixerUnavailable,
        "Named source route without a mixer was not rejected explicitly");

    const std::filesystem::path mixerPath = TestRoot() / "RuntimeMixer.kbmixer";
    kb::audio::AudioMixerAsset mixer;
    mixer.buses.push_back(kb::audio::AudioMixerBus{ .name = "Effects" });
    Require(kb::audio::AudioMixerAssetIO::Save(mixerPath, mixer), "Audio test mixer could not be saved");
    Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                .id = kb::assets::AssetId{ 8399U },
                .type = kb::audio::kAudioMixerAssetType,
                .name = "RuntimeMixer",
                .virtualPath = "/Audio/Runtime.kbmixer",
                .physicalPath = mixerPath.string(),
                .contentHash = 1U,
            }),
        "Audio test mixer registration failed");
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, 8399U);
    Require(buses.Sync(engine.Native(), scene, true), "Audio test mixer topology was not built");
    Require(buses.Resolve("Effects").status == kb::audio_miniaudio::MiniaudioBusRegistry::RouteStatus::Routed
            && buses.Resolve("Missing").status == kb::audio_miniaudio::MiniaudioBusRegistry::RouteStatus::UnknownBus,
        "Mixer route query did not distinguish routed and unknown buses");
    Require(kb::scene::SetAudioSourceOutputBus(
                *scene.Components().AudioSources().TryGet(sourceObject.Entity()), "Missing"),
        "Missing-route source fixture bus was invalid");
    Require(sources.PlaySource(engine.Native(), scene, sourceObject.Entity(), resolver, buses, true).status
            == kb::audio::AudioSourceControlStatus::UnknownBus,
        "Unknown source route did not fail explicitly");

    scene.Entities().Destroy(sourceObject.Entity());
    kb::scene::SceneSystemContext destroyedContext{ scene, 0.25F };
    sources.Sync(engine.Native(), destroyedContext, resolver, buses, nullptr, {}, true);
    Require(sources.SoundCountForTesting() == 0U, "Destroyed source entity retained a native sound");
}

void RunBackendRestartTest(const std::filesystem::path& clipPath) {
    kb::scene::Scene scene;
    RegisterClip(scene, 8401U, clipPath);
    const std::filesystem::path mixerPath = TestRoot() / "RestartMixer.kbmixer";
    kb::audio::AudioMixerAsset mixer;
    mixer.buses.push_back(kb::audio::AudioMixerBus{ .name = "Effects" });
    Require(kb::audio::AudioMixerAssetIO::Save(mixerPath, mixer), "Restart test mixer could not be saved");
    Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                .id = kb::assets::AssetId{ 8499U },
                .type = kb::audio::kAudioMixerAssetType,
                .name = "RestartMixer",
                .virtualPath = "/Audio/Restart.kbmixer",
                .physicalPath = mixerPath.string(),
                .contentHash = 1U,
            }),
        "Restart test mixer registration failed");
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, 8499U);
    const kb::scene::SceneObject source = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Restart Source" });
    kb::scene::AudioSourceComponent sourceComponent{
        .clipAssetId = 8401U,
        .volume = 0.0F,
        .loop = true,
        .spatial = false,
    };
    Require(kb::scene::SetAudioSourceOutputBus(sourceComponent, "Effects"),
        "Restart source fixture bus was invalid");
    scene.Components().AudioSources().Set(source.Entity(), sourceComponent);

    kb::audio_miniaudio::MiniaudioPlaybackBackend backend;
    Require(backend.ReinitializeForTesting(scene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Controlled no-device backend did not initialize");
    Require(backend.PlaySourceForTesting(scene, source.Entity()).Succeeded(), "Restart test source could not be created");
    kb::audio::AudioPlayDesc voiceDesc{ .clipAssetId = 8401U, .volume = 0.0F, .loop = true, .spatial = false };
    voiceDesc.outputBus = "Effects";
    Require(backend.PlayOneShotForTesting(scene, voiceDesc).Succeeded(), "Restart test voice could not be created");
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting resources = backend.ResourcesForTesting();
    Require(resources.sourceSounds == 1U && resources.voices == 1U && resources.buses == 1U,
        "Restart test did not create source, voice and bus resources");
    kb::audio::AudioPlayDesc invalidVoiceDesc = voiceDesc;
    invalidVoiceDesc.volume = std::numeric_limits<float>::quiet_NaN();
    Require(!backend.PlayOneShotForTesting(scene, invalidVoiceDesc).Succeeded(),
        "Backend accepted invalid one-shot settings");
    const kb::audio::AudioPlayResult liveVoice = backend.PlayOneShotForTesting(scene, voiceDesc);
    Require(liveVoice.Succeeded(), "Backend control validation fixture voice could not be created");
    const kb::scene::SceneObject markerTarget = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "Backend Marker Target" });
    const kb::scene::SceneObject deadMarkerTarget = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "Dead Backend Marker Target" });
    scene.Entities().Destroy(deadMarkerTarget.Entity());
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting resourcesBeforeRejectedControls =
        backend.ResourcesForTesting();
    Require(!backend.SeekVoice(scene, liveVoice.voiceId, -0.01F)
            && !backend.SetVoiceVolume(scene, liveVoice.voiceId, std::numeric_limits<float>::infinity())
            && !backend.SetVoicePan(scene, liveVoice.voiceId, 1.01F)
            && !backend.SetVoicePitch(scene, liveVoice.voiceId, 0.009F)
            && !backend.AddVoiceMarker(
                scene, liveVoice.voiceId, "late", std::numeric_limits<float>::quiet_NaN(), markerTarget.Entity())
            && !backend.AddVoiceMarker(scene, liveVoice.voiceId, "dead", 0.25F, deadMarkerTarget.Entity()),
        "Backend accepted an invalid direct voice control or marker");
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting resourcesAfterRejectedControls =
        backend.ResourcesForTesting();
    Require(resourcesAfterRejectedControls.sourceSounds == resourcesBeforeRejectedControls.sourceSounds
            && resourcesAfterRejectedControls.voices == resourcesBeforeRejectedControls.voices
            && resourcesAfterRejectedControls.buses == resourcesBeforeRejectedControls.buses,
        "Rejected backend controls changed native resource ownership");
    Require(backend.ReinitializeForTesting(scene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Controlled no-device restart failed with active resources");
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting released = backend.ResourcesForTesting();
    Require(released.sourceSounds == 0U && released.voices == 0U && released.buses == 0U,
        "Restart retained native source, voice or bus resources");

    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, 0U);
    Require(kb::scene::SetAudioSourceOutputBus(
                *scene.Components().AudioSources().TryGet(source.Entity()), {}),
        "Unavailable source fixture master route was invalid");
    voiceDesc.outputBus.clear();
    Require(backend.PlaySourceForTesting(scene, source.Entity()).Succeeded(), "Backend was not reusable after active-resource restart");
    const kb::audio::AudioPlayResult unavailableVoice = backend.PlayOneShotForTesting(scene, voiceDesc);
    Require(unavailableVoice.Succeeded(), "Backend voice was not reusable after active-resource restart");
    Require(backend.PauseVoice(scene, unavailableVoice.voiceId), "Unavailable-tick looping voice could not enter paused state");
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting beforeUnavailableTick = backend.ResourcesForTesting();
    Require(beforeUnavailableTick.sourceSounds == 1U && beforeUnavailableTick.voices == 1U && beforeUnavailableTick.buses == 0U,
        "Unavailable-tick test did not create master-routed source and paused looping voice resources");
    kb::scene::SceneSystemContext unavailableContext{ scene, 1.0F / 60.0F };
    backend.OnUpdate(unavailableContext);
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting afterUnavailableTick = backend.ResourcesForTesting();
    Require(afterUnavailableTick.sourceSounds == 0U && afterUnavailableTick.voices == 0U && afterUnavailableTick.buses == 0U,
        "No-device tick without a mixer retained a native source or paused looping voice");

    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, 8499U);
    Require(kb::scene::SetAudioSourceOutputBus(
                *scene.Components().AudioSources().TryGet(source.Entity()), "Effects"),
        "Unavailable source fixture bus was invalid");
    voiceDesc.outputBus = "Effects";
    Require(backend.PlaySourceForTesting(scene, source.Entity()).Succeeded(), "Bus-routed source was not reusable after unavailable cleanup");
    Require(backend.PlayOneShotForTesting(scene, voiceDesc).Succeeded(), "Bus-routed voice was not reusable after unavailable cleanup");
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting beforeUnavailableBusTick = backend.ResourcesForTesting();
    Require(beforeUnavailableBusTick.sourceSounds == 1U && beforeUnavailableBusTick.voices == 1U && beforeUnavailableBusTick.buses == 1U,
        "Unavailable bus-tick test did not recreate every native resource type");
    backend.OnUpdate(unavailableContext);
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting afterUnavailableBusTick = backend.ResourcesForTesting();
    Require(afterUnavailableBusTick.sourceSounds == 0U && afterUnavailableBusTick.voices == 0U && afterUnavailableBusTick.buses == 0U,
        "No-device tick retained a native source, voice or bus group");

    const kb::audio::AudioDeviceStatus initial = backend.Reinitialize(scene);
    Require(initial == backend.DeviceStatus()
            && (initial == kb::audio::AudioDeviceStatus::PlaybackAvailable || initial == kb::audio::AudioDeviceStatus::NoPlaybackDevice),
        "Audio backend did not initialize into a usable state");
    if (initial == kb::audio::AudioDeviceStatus::PlaybackAvailable) {
        Require(backend.StopPlaybackDeviceForTesting(), "Playback device could not be stopped for lifecycle verification");
        Require(backend.DeviceStatus() == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
            "Stopped playback device was not reflected in public status");
    }
    const kb::audio::AudioDeviceStatus restarted = backend.Reinitialize(scene);
    Require(restarted == kb::audio::AudioDeviceStatus::PlaybackAvailable || restarted == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Audio backend restart did not return to a usable state");
    backend.Shutdown();
    Require(backend.ReinitializeForTesting(scene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Controlled no-device restart did not recover after shutdown");
    Require(backend.PlaySourceForTesting(scene, source.Entity()).Succeeded()
            && backend.PlayOneShotForTesting(scene, voiceDesc).Succeeded(),
        "Controlled shutdown fixture could not recreate source, voice and bus resources");
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting beforeShutdown = backend.ResourcesForTesting();
    Require(beforeShutdown.sourceSounds == 1U && beforeShutdown.voices == 1U && beforeShutdown.buses == 1U,
        "Controlled shutdown fixture did not own every native resource type");
    backend.Shutdown();
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting afterShutdown = backend.ResourcesForTesting();
    Require(afterShutdown.sourceSounds == 0U && afterShutdown.voices == 0U && afterShutdown.buses == 0U,
        "Backend shutdown retained a native source, voice or bus resource");
}

template <typename Resolver>
void RunClipResolverValidationTest(const std::filesystem::path& clipPath, Resolver& resolver) {
    kb::scene::Scene scene;
    RegisterClip(scene, 8601U, clipPath);
    Require(resolver.Resolve(scene, 8601U) == clipPath,
        "Audio clip resolver rejected a valid native wave asset");

    Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                .id = kb::assets::AssetId{ 8602U },
                .type = "Texture",
                .name = "WrongType",
                .virtualPath = "/Audio/WrongType.wav",
                .physicalPath = clipPath,
                .contentHash = 8602U,
            }),
        "Wrong-type resolver fixture registration failed");
    Require(resolver.Resolve(scene, 8602U).empty(),
        "Audio clip resolver accepted wrong metadata type");

    Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                .id = kb::assets::AssetId{ 8603U },
                .type = "AudioClip",
                .importCategory = "Audio",
                .name = "WrongCategoryShape",
                .virtualPath = "/Audio/WrongCategoryShape.wav",
                .physicalPath = clipPath,
                .contentHash = 8603U,
            }),
        "Wrong-category resolver fixture registration failed");
    Require(resolver.Resolve(scene, 8603U).empty(),
        "Audio clip resolver accepted imported metadata on a native source file");

    const std::filesystem::path unsupportedPath = TestRoot() / "Unsupported.ogg";
    std::filesystem::copy_file(clipPath, unsupportedPath, std::filesystem::copy_options::overwrite_existing);
    Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                .id = kb::assets::AssetId{ 8604U },
                .type = "AudioClip",
                .name = "UnsupportedPhysical",
                .virtualPath = "/Audio/Unsupported.ogg",
                .physicalPath = unsupportedPath,
                .contentHash = 8604U,
            }),
        "Unsupported-physical resolver fixture registration failed");
    Require(resolver.Resolve(scene, 8604U).empty(),
        "Audio clip resolver accepted an unsupported physical extension");

    const std::filesystem::path projectRoot = TestRoot() / "ResolverProject";
    Require(scene.Assets().MountProject(projectRoot), "Audio resolver import project did not mount");
    const std::array<std::filesystem::path, 1U> sourceFiles{ clipPath };
    const kb::assets::AssetImportResult imported = kb::assets::AssetImportService::ImportFiles(
        scene.Assets().Manager(), sourceFiles, "/Game/Audio");
    Require(imported.Succeeded() && imported.items.size() == 1U,
        "Supported audio resolver import fixture failed");
    const std::filesystem::path resolvedImported = resolver.Resolve(scene, imported.items[0].id.value);
    Require(!resolvedImported.empty() && resolvedImported.extension() == ".wav"
            && std::filesystem::is_regular_file(resolvedImported),
        "Audio clip resolver rejected a supported imported source extension");

    const std::filesystem::path secondSource = TestRoot() / "UnsupportedSource.wav";
    std::filesystem::copy_file(clipPath, secondSource, std::filesystem::copy_options::overwrite_existing);
    const std::array<std::filesystem::path, 1U> secondFiles{ secondSource };
    const kb::assets::AssetImportResult unsupportedImported = kb::assets::AssetImportService::ImportFiles(
        scene.Assets().Manager(), secondFiles, "/Game/Audio");
    Require(unsupportedImported.Succeeded() && unsupportedImported.items.size() == 1U,
        "Unsupported source-extension resolver fixture failed to import");
    std::vector<char> container;
    {
        std::ifstream input{ unsupportedImported.items[0].assetPhysicalPath, std::ios::binary };
        container.assign(std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{});
    }
    const std::array<char, 4U> supportedExtension{ '.', 'w', 'a', 'v' };
    const auto extensionPosition = std::find_end(
        container.begin(), container.end(), supportedExtension.begin(), supportedExtension.end());
    Require(extensionPosition != container.end(), "Imported audio fixture source extension was not found");
    const std::array<char, 4U> unsupportedExtension{ '.', 'o', 'g', 'g' };
    std::copy(unsupportedExtension.begin(), unsupportedExtension.end(), extensionPosition);
    {
        std::ofstream output{ unsupportedImported.items[0].assetPhysicalPath, std::ios::binary | std::ios::trunc };
        output.write(container.data(), static_cast<std::streamsize>(container.size()));
        Require(output.good(), "Imported audio fixture source extension could not be corrupted");
    }
    Require(resolver.Resolve(scene, unsupportedImported.items[0].id.value).empty(),
        "Audio clip resolver accepted an unsupported imported source extension");
}

} // namespace

int RunTests(int argc, char** argv) {
    std::error_code error;
    std::filesystem::remove_all(TestRoot(), error);
    std::filesystem::create_directories(TestRoot(), error);
    Require(!error, "Audio runtime test directory could not be prepared");
    const std::filesystem::path clipPath = TestRoot() / "Runtime.wav";
    WriteSilentWav(clipPath);
    const std::string_view filter = argc > 1 ? std::string_view{ argv[1] } : std::string_view{};
    if (filter.empty() || filter == "sound") {
        RunSoundStateTest(clipPath);
    }
    if (filter.empty() || filter == "voice" || filter == "resolver") {
        RunVoiceStateTest(clipPath);
    }
    if (filter.empty() || filter == "transaction") {
        RunTransactionalPlaybackFailureTest(clipPath);
    }
    if (filter.empty() || filter == "initial-frame") {
        RunInitialFrameSoundTest(clipPath);
    }
    if (filter.empty() || filter == "listener-attached") {
        RunListenerAndAttachedVelocityTest(clipPath);
    }
    if (filter.empty() || filter == "source-routing") {
        RunSourceLifecycleAndRoutingTest(clipPath);
    } else if (filter.starts_with("source-")) {
        RunSourceLifecycleAndRoutingTest(clipPath, filter);
    }
    if (filter.empty() || filter == "backend-restart") {
        RunBackendRestartTest(clipPath);
    }
    return 0;
}

int main(int argc, char** argv) {
    try {
        return RunTests(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
