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
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <cstdint>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
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

        // LIB-151: occlusion against the real collider raycast geometry - a wall between
        // the listener (origin) and the routed source exercises the budget-capped sampler
        // on the real plugin across the ticks below.
        kb::scene::SceneAudioOcclusionAccess::Configure(scene, kb::scene::AudioOcclusionSettings{
                                                                   .enabled = true,
                                                                   .occludedVolumeScale = 0.25F,
                                                                   .maxDistance = 100.0F,
                                                                   .layerMask = 0xFFFFFFFFU,
                                                                   .maxRaycastsPerTick = 4U,
                                                               });
        const kb::scene::SceneObject wall = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Occluding Wall",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 1.0F },
                .worldPosition = kb::scene::Vec3{ 0.0F, 0.0F, 1.0F },
                .worldDirty = false,
            },
        });
        scene.Components().Colliders().Set(wall.Entity(), kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Box,
            .boxSize = kb::scene::Vec3{ 4.0F, 4.0F, 0.2F },
        });
        const kb::scene::SceneObject occludedSourceObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Occluded Source",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 2.0F },
                .worldPosition = kb::scene::Vec3{ 0.0F, 0.0F, 2.0F },
                .worldDirty = false,
            },
        });
        scene.Components().AudioSources().Set(occludedSourceObject.Entity(), kb::scene::AudioSourceComponent{
            .clipAssetId = importedClip.id.value,
            .volume = 0.0F,
            .loop = true,
            .spatial = true,
            .autoplay = true,
        });

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
            // LIB-152: audio-clock position + markers on the REAL plugin - a marker at
            // 0.0 fires on the first tick's DispatchMarkers (the queued event is drained
            // by the same Update's script dispatch; no behaviours here, so it is honestly
            // dropped - the delivery path itself is proven in ScriptRuntimeTests).
            kb::tests::Require(kb::audio::AudioPlayback::VoicePlaybackSeconds(scene, attachedPlayed.voiceId) >= 0.0F,
                "A live voice must report a non-negative audio-clock position");
            kb::tests::Require(kb::audio::AudioPlayback::AddVoiceMarker(scene, attachedPlayed.voiceId, "start", 0.0F, ownerObject.Entity()),
                "AddVoiceMarker failed for a live voice on the real plugin");
            kb::tests::Require(!kb::audio::AudioPlayback::AddVoiceMarker(scene, 999999U, "ghost", 0.0F, ownerObject.Entity()),
                "AddVoiceMarker must be honestly false for a dead voice");
            [[maybe_unused]] const bool tickedAttached = scene.Runtime().Update(1.0F / 60.0F);
            kb::tests::Require(kb::audio::AudioPlayback::IsVoicePlaying(scene, attachedPlayed.voiceId),
                "An attached looping voice must survive ticks while its owner lives");
            scene.Entities().Destroy(ownerObject.Entity());
            [[maybe_unused]] const bool tickedAfterDestroy = scene.Runtime().Update(1.0F / 60.0F);
            kb::tests::Require(!kb::audio::AudioPlayback::IsVoicePlaying(scene, attachedPlayed.voiceId)
                    && !kb::audio::AudioPlayback::StopVoice(scene, attachedPlayed.voiceId),
                "An attached looping voice must be fully released with its owner - no leaked source record");
        }

        // LIB-154: pooled one-shots, asset unload/delete DURING playback, and a scene-change
        // entity teardown - the REAL backend paths (device-present branch; the no-device
        // case is covered by the controlled-error checks above).
        if (routedPlayed.Succeeded()) {
            // Pooled one-shots: the per-clip cap (kMaxOneShotVoicesPerClip) is a silent
            // clamp - every request past it still starts (the newest wins its slot), never
            // a crash or a leaked source.
            std::uint64_t lastPooled = 0U;
            for (int i = 0; i < 12; ++i) {
                const kb::audio::AudioPlayResult pooled = kb::audio::AudioPlayback::PlayOneShot(scene, kb::audio::AudioPlayDesc{
                    .clipAssetId = importedClip.id.value,
                    .volume = 0.0F,
                    .loop = true,
                    .spatial = false,
                });
                kb::tests::Require(pooled.Succeeded(), "Every pooled one-shot past the per-clip cap must still start (silent clamp)");
                lastPooled = pooled.voiceId;
            }
            kb::tests::Require(kb::audio::AudioPlayback::IsVoicePlaying(scene, lastPooled),
                "The newest pooled one-shot must own a live slot after the per-clip clamp");

            // Asset UNLOAD during playback keeps the registry metadata: the live streaming
            // voice is untouched (it holds its own file handle) and a NEW play still resolves.
            static_cast<void>(scene.Assets().Manager().Unload(importedClip.id));
            static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
            kb::tests::Require(kb::audio::AudioPlayback::IsVoicePlaying(scene, lastPooled),
                "A live streaming one-shot must survive an asset Unload");
            const kb::audio::AudioPlayResult afterUnload = kb::audio::AudioPlayback::PlayOneShot(scene, kb::audio::AudioPlayDesc{
                .clipAssetId = importedClip.id.value, .volume = 0.0F, .loop = false, .spatial = false });
            kb::tests::Require(afterUnload.Succeeded(), "Unload keeps registry metadata, so a new play still resolves");

            // Asset DELETE removes the registry metadata: the live streaming voices survive
            // (their handles are already open on the temp cache), but a NEW play now fails
            // to resolve, and the entity sources referencing the clip are cleanly dropped on
            // the next Sync (no crash, no dangling ma_sound).
            kb::tests::Require(scene.Assets().Manager().DeleteAsset(importedClip.id), "Imported clip DeleteAsset failed");
            static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
            kb::tests::Require(kb::audio::AudioPlayback::IsVoicePlaying(scene, lastPooled),
                "A live streaming one-shot must survive an asset DeleteAsset");
            const kb::audio::AudioPlayResult afterDelete = kb::audio::AudioPlayback::PlayOneShot(scene, kb::audio::AudioPlayDesc{
                .clipAssetId = importedClip.id.value, .volume = 0.0F, .loop = false, .spatial = false });
            kb::tests::Require(!afterDelete.Succeeded() && afterDelete.error == "audio clip file could not be resolved",
                "A new play of a deleted clip must honestly fail to resolve");

            kb::audio::AudioPlayback::StopAll(scene);
            kb::tests::Require(!kb::audio::AudioPlayback::IsVoicePlaying(scene, lastPooled),
                "StopAll must release every pooled voice with no leaked source");

            // Global one-shot pool cap: fill kMaxOneShotVoices with high-priority voices
            // across distinct clips, then a lower-priority request for a fresh clip is
            // honestly REFUSED (never steals a higher-priority voice), while a
            // higher-priority request EVICTS the lowest and starts. Distinct clips share
            // the same on-disk file (its own wav, untouched by the DeleteAsset above) -
            // separate asset ids, separate per-clip buckets.
            const std::filesystem::path poolClipPath = TestRoot() / "External" / "Pool.wav";
            WriteSilentWav(poolClipPath);
            constexpr int kFillClips = 8;   // 8 clips x 8 per-clip = 64 = kMaxOneShotVoices
            constexpr int kPerClip = 8;
            for (int c = 0; c < kFillClips; ++c) {
                const kb::assets::AssetId fillClip{ 9800U + static_cast<std::uint64_t>(c) };
                kb::tests::Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                                       .id = fillClip,
                                       .type = "AudioClip",
                                       .name = "PoolFill",
                                       .virtualPath = std::string{ "/Game/Audio/PoolFill" } + std::to_string(c) + ".wav",
                                       .physicalPath = poolClipPath.string(),
                                       .contentHash = 1U,
                                   }),
                    "Pool-fill clip registration failed");
                for (int i = 0; i < kPerClip; ++i) {
                    const kb::audio::AudioPlayResult filled = kb::audio::AudioPlayback::PlayOneShot(scene, kb::audio::AudioPlayDesc{
                        .clipAssetId = fillClip.value, .volume = 0.0F, .loop = true, .spatial = false, .priority = 200U });
                    kb::tests::Require(filled.Succeeded(), "Filling the one-shot pool with high-priority voices must succeed up to capacity");
                }
                if (c == 0) {
                    const kb::audio::AudioPlayResult refusedPerClip = kb::audio::AudioPlayback::PlayOneShot(scene, kb::audio::AudioPlayDesc{
                        .clipAssetId = fillClip.value, .volume = 0.0F, .loop = true, .spatial = false, .priority = 50U });
                    kb::tests::Require(!refusedPerClip.Succeeded()
                            && refusedPerClip.error == "audio clip pool is full of higher-priority voices",
                        "A full per-clip bucket must not let a low-priority request steal a high-priority voice");
                }
            }
            // A fresh clip at low priority: its per-clip bucket is empty, so nothing is
            // evicted within the clip, and the full pool of higher-priority voices refuses it.
            const kb::assets::AssetId lowPriorityClip{ 9808U };
            kb::tests::Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                                   .id = lowPriorityClip,
                                   .type = "AudioClip",
                                   .name = "PoolLow",
                                   .virtualPath = "/Game/Audio/PoolLow.wav",
                                   .physicalPath = poolClipPath.string(),
                                   .contentHash = 1U,
                               }),
                "Low-priority pool clip registration failed");
            const kb::audio::AudioPlayResult refusedLow = kb::audio::AudioPlayback::PlayOneShot(scene, kb::audio::AudioPlayDesc{
                .clipAssetId = lowPriorityClip.value, .volume = 0.0F, .loop = true, .spatial = false, .priority = 50U });
            kb::tests::Require(!refusedLow.Succeeded() && refusedLow.error == "audio one-shot pool is full of higher-priority voices",
                "A full pool of higher-priority voices must honestly refuse a lower-priority one-shot, never steal");
            // A higher-priority request evicts the lowest and starts.
            const kb::assets::AssetId highPriorityClip{ 9809U };
            kb::tests::Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                                   .id = highPriorityClip,
                                   .type = "AudioClip",
                                   .name = "PoolHigh",
                                   .virtualPath = "/Game/Audio/PoolHigh.wav",
                                   .physicalPath = poolClipPath.string(),
                                   .contentHash = 1U,
                               }),
                "High-priority pool clip registration failed");
            const kb::audio::AudioPlayResult admittedHigh = kb::audio::AudioPlayback::PlayOneShot(scene, kb::audio::AudioPlayDesc{
                .clipAssetId = highPriorityClip.value, .volume = 0.0F, .loop = true, .spatial = false, .priority = 255U });
            kb::tests::Require(admittedHigh.Succeeded(), "A higher-priority one-shot must evict the lowest and start when the pool is full");

            kb::audio::AudioPlayback::StopAll(scene);

            // A real non-additive scene change stops the outgoing pool before
            // destroying source entities, while retaining the registered backend.
            const kb::audio::AudioPlayResult carriedVoice =
                kb::audio::AudioPlayback::PlayOneShot(
                    scene,
                    kb::audio::AudioPlayDesc{
                        .clipAssetId = highPriorityClip.value,
                        .volume = 0.0F,
                        .loop = true,
                        .spatial = false,
                    });
            kb::tests::Require(
                carriedVoice.Succeeded(),
                "Scene-change voice did not start on the real backend");
            kb::scene::SceneDocument nextScene{};
            nextScene.guid = "scene:lib154-miniaudio-next";
            nextScene.name = "MiniaudioNext";
            kb::tests::Require(
                kb::scene::SceneDocumentService::LoadIntoScene(
                    scene, nextScene),
                "Real Miniaudio non-additive scene change failed");
            kb::tests::Require(
                !kb::audio::AudioPlayback::IsVoicePlaying(
                    scene, carriedVoice.voiceId) &&
                    !kb::audio::AudioPlayback::StopVoice(
                        scene, carriedVoice.voiceId),
                "A non-additive scene change must release pooled one-shots");
            kb::tests::Require(
                kb::audio::AudioPlayback::HasBackend(scene),
                "A scene change must retain the Miniaudio backend");
            const kb::audio::AudioPlayResult afterSceneChange = kb::audio::AudioPlayback::PlayOneShot(scene, kb::audio::AudioPlayDesc{
                .clipAssetId = highPriorityClip.value, .volume = 0.0F, .loop = false, .spatial = false });
            kb::tests::Require(afterSceneChange.Succeeded(), "The backend must remain usable after a scene-change entity teardown");
            kb::audio::AudioPlayback::StopAll(scene);
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
