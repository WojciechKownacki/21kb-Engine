#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/audio/AudioMixerAssetIO.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <cstdint>
#include <array>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#if !defined(KB_AUDIO_MINIAUDIO_PLUGIN_PATH)
#define KB_AUDIO_MINIAUDIO_PLUGIN_PATH ""
#endif

namespace {

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_audio_scene_system_tests";
}

void ResetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(TestRoot(), error);
    std::filesystem::create_directories(TestRoot(), error);
    kb::tests::Require(!error, "Audio scene system test root could not be prepared");
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
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    kb::tests::Require(!error, "Audio scene system test directory could not be created");

    constexpr std::uint16_t channels = 1U;
    constexpr std::uint32_t sampleRate = 44100U;
    constexpr std::uint16_t bitsPerSample = 16U;
    constexpr std::uint32_t sampleCount = 64U;
    constexpr std::uint32_t bytesPerSample = bitsPerSample / 8U;
    constexpr std::uint32_t dataSize = sampleCount * channels * bytesPerSample;
    constexpr std::uint32_t byteRate = sampleRate * channels * bytesPerSample;
    constexpr std::uint16_t blockAlign = channels * bytesPerSample;

    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    kb::tests::Require(output.is_open(), "Audio scene system test wav could not be opened");

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
    for (std::uint32_t i = 0; i < dataSize; ++i) {
        output.put('\0');
    }
    kb::tests::Require(output.good(), "Audio scene system test wav could not be written");
}

void RunMiniaudioPluginUpdatesSceneSourcesTest() {
    if (std::filesystem::path{ KB_AUDIO_MINIAUDIO_PLUGIN_PATH }.empty()) {
        return;
    }

    ResetTestRoot();

    const std::filesystem::path clipPath = TestRoot() / "External" / "Ping.wav";
    WriteSilentWav(clipPath);

    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Audio.Miniaudio",
        .binaryPath = KB_AUDIO_MINIAUDIO_PLUGIN_PATH,
        .enabled = true,
    });

    {
        kb::scene::Scene scene{ std::move(descriptor) };
        kb::tests::Require(scene.Assets().MountProject(TestRoot() / "Project"), "Audio scene system test project mount failed");

        const std::array<std::filesystem::path, 1> importedFiles{ clipPath };
        const kb::assets::AssetImportResult importResult = kb::assets::AssetImportService::ImportFiles(scene.Assets().Manager(), importedFiles, "/Game/Audio");
        kb::tests::Require(importResult.Succeeded() && importResult.ImportedCount() == 1U, "Audio scene system test import failed");
        const kb::assets::AssetImportItemResult& importedClip = importResult.items.front();
        kb::tests::Require(importedClip.category == kb::assets::AssetImportCategory::Audio, "Audio scene system test asset was not imported as audio");
        kb::tests::Require(kb::audio::AudioPlayback::HasBackend(scene), "Audio miniaudio plugin did not register a scene audio playback backend");

        kb::scene::SceneObject listener = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Listener" });
        scene.Components().AudioListeners().Set(listener.Entity(), kb::scene::AudioListenerComponent{
            .primary = true,
            .enabled = true,
        });

        kb::scene::SceneObject source = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Source" });
        scene.Components().AudioSources().Set(source.Entity(), kb::scene::AudioSourceComponent{
            .clipAssetId = importedClip.id.value,
            .volume = 0.0F,
            .pitch = 1.0F,
            .loop = true,
            .spatial = true,
            .autoplay = false,
        });

        for (int i = 0; i < 3; ++i) {
            [[maybe_unused]] const bool progressed = scene.Runtime().Update(1.0F / 60.0F);
        }

        const kb::audio::AudioPlayResult played = kb::audio::AudioPlayback::PlayOneShot(scene, kb::audio::AudioPlayDesc{
            .clipAssetId = importedClip.id.value,
            .volume = 0.0F,
            .pitch = 1.0F,
            .loop = false,
            .spatial = false,
        });
        kb::tests::Require(
            played.Succeeded() || played.error == "miniaudio playback device is not available",
            "Audio playback backend did not start a one-shot voice or report a controlled no-device error");

        // LIB-147: the mixer bus path against the REAL plugin - authored .kbmixer asset,
        // active mixer + snapshot, an entity source routed to a child bus, a one-shot
        // routed by outputBus, then the mixer cleared mid-play (bus topology teardown).
        // Exercises the full ma_sound_group lifecycle; audibility itself cannot be
        // asserted headlessly, honest no-device runs already skip via playbackAvailable.
        const std::filesystem::path mixerPath = TestRoot() / "External" / "Main.kbmixer";
        kb::audio::AudioMixerAsset mixerAsset;
        mixerAsset.buses = {
            kb::audio::AudioMixerBus{ .name = "Sfx", .parentBus = "", .volume = 0.5F, .mute = false },
            kb::audio::AudioMixerBus{ .name = "Weapons", .parentBus = "Sfx", .volume = 1.0F, .mute = false },
        };
        mixerAsset.snapshots = {
            kb::audio::AudioMixerSnapshot{ .name = "Quiet", .busVolumes = { kb::audio::AudioMixerSnapshotBusVolume{ .bus = "Sfx", .volume = 0.0F } } },
        };
        kb::tests::Require(kb::audio::AudioMixerAssetIO::Save(mixerPath, mixerAsset), "Audio scene system test mixer save failed");
        const kb::assets::AssetId mixerAssetId{ 9701U };
        kb::tests::Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                               .id = mixerAssetId,
                               .type = kb::audio::kAudioMixerAssetType,
                               .name = "MainMixer",
                               .virtualPath = "/Game/Audio/Main.kbmixer",
                               .physicalPath = mixerPath.string(),
                               .contentHash = 1U,
                           }),
            "Audio scene system test mixer asset registration failed");
        kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, mixerAssetId.value);
        kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(scene, "Quiet");
        // LIB-150: runtime override + a mid-flight snapshot transition exercise the full
        // volume-resolution stack (authored -> snapshot lerp -> override) on the real
        // plugin across the ticks below.
        kb::scene::SceneAudioMixerAccess::SetBusVolumeOverride(scene, "Weapons", 0.2F);
        kb::scene::SceneAudioMixerAccess::BeginSnapshotTransition(scene, "", 0.25F);

        kb::scene::AudioSourceComponent routedSource{
            .clipAssetId = importedClip.id.value,
            .volume = 0.0F,
            .loop = true,
            .spatial = false,
            .autoplay = true,
        };
        kb::scene::SetAudioSourceOutputBus(routedSource, "Weapons");
        scene.Components().AudioSources().Set(source.Entity(), routedSource);
        for (int i = 0; i < 3; ++i) {
            [[maybe_unused]] const bool progressed = scene.Runtime().Update(1.0F / 60.0F);
        }
        kb::audio::AudioPlayDesc routedOneShot{
            .clipAssetId = importedClip.id.value,
            .volume = 0.0F,
            .loop = false,
            .spatial = false,
        };
        routedOneShot.outputBus = "Weapons";
        const kb::audio::AudioPlayResult routedPlayed = kb::audio::AudioPlayback::PlayOneShot(scene, routedOneShot);
        kb::tests::Require(
            routedPlayed.Succeeded() || routedPlayed.error == "miniaudio playback device is not available",
            "Bus-routed one-shot did not start or report a controlled no-device error");

        // LIB-148: per-voice control against the REAL plugin (only when a device exists -
        // the honest no-device error above already covered the headless case).
        if (routedPlayed.Succeeded()) {
            const std::uint64_t voice = routedPlayed.voiceId;
            kb::tests::Require(kb::audio::AudioPlayback::IsVoicePlaying(scene, voice), "A started one-shot voice must report playing");
            kb::tests::Require(kb::audio::AudioPlayback::PauseVoice(scene, voice), "PauseVoice failed for a live voice");
            kb::tests::Require(!kb::audio::AudioPlayback::IsVoicePlaying(scene, voice), "A paused voice must not report playing");
            [[maybe_unused]] const bool ticked = scene.Runtime().Update(1.0F / 60.0F);
            kb::tests::Require(kb::audio::AudioPlayback::ResumeVoice(scene, voice), "ResumeVoice failed for a paused voice (it must survive the frame tick, never be reclaimed)");
            kb::tests::Require(kb::audio::AudioPlayback::SeekVoice(scene, voice, 0.01F), "SeekVoice failed for a live voice");
            kb::tests::Require(kb::audio::AudioPlayback::SetVoiceVolume(scene, voice, 0.0F) && kb::audio::AudioPlayback::SetVoicePitch(scene, voice, 1.1F)
                    && kb::audio::AudioPlayback::SetVoiceLoop(scene, voice, true),
                "Per-voice volume/pitch/loop setters failed for a live voice");
            kb::tests::Require(kb::audio::AudioPlayback::StopVoice(scene, voice), "StopVoice failed for a live voice");
            kb::tests::Require(!kb::audio::AudioPlayback::StopVoice(scene, voice) && !kb::audio::AudioPlayback::IsVoicePlaying(scene, voice)
                    && !kb::audio::AudioPlayback::PauseVoice(scene, voice),
                "Every operation on a stopped voice must be honestly false");
        }

        // LIB-149: an owner-attached looping voice must die with its owner - never leak
        // its source (only when a device exists; the no-device path was covered above).
        if (routedPlayed.Succeeded()) {
            const kb::scene::SceneObject ownerObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Voice Owner" });
            kb::audio::AudioPlayDesc attachedDesc{
                .clipAssetId = importedClip.id.value,
                .volume = 0.0F,
                .loop = true,
                .spatial = true,
            };
            attachedDesc.ownerEntityId = ownerObject.Entity().Id();
            const kb::audio::AudioPlayResult attachedPlayed = kb::audio::AudioPlayback::PlayOneShot(scene, attachedDesc);
            kb::tests::Require(attachedPlayed.Succeeded(), "Owner-attached one-shot did not start");
            [[maybe_unused]] const bool tickedAttached = scene.Runtime().Update(1.0F / 60.0F);
            kb::tests::Require(kb::audio::AudioPlayback::IsVoicePlaying(scene, attachedPlayed.voiceId),
                "An attached looping voice must survive ticks while its owner lives");
            scene.Entities().Destroy(ownerObject.Entity());
            [[maybe_unused]] const bool tickedAfterDestroy = scene.Runtime().Update(1.0F / 60.0F);
            kb::tests::Require(!kb::audio::AudioPlayback::IsVoicePlaying(scene, attachedPlayed.voiceId)
                    && !kb::audio::AudioPlayback::StopVoice(scene, attachedPlayed.voiceId),
                "An attached looping voice must be fully released with its owner - no leaked source record");
        }

        // Topology teardown mid-play: clearing the mixer must rebuild routing to the
        // implicit master on the next tick without crashing or dangling groups.
        kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, 0U);
        for (int i = 0; i < 3; ++i) {
            [[maybe_unused]] const bool progressed = scene.Runtime().Update(1.0F / 60.0F);
        }

        kb::audio::AudioPlayback::StopAll(scene);
    }
}

} // namespace

namespace kb::tests {

void RunAudioSceneSystemTests() {
    RunMiniaudioPluginUpdatesSceneSourcesTest();
}

} // namespace kb::tests
