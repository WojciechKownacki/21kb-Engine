#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/audio/AudioPlayback.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
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
        kb::audio::AudioPlayback::StopAll(scene);
    }
}

} // namespace

namespace kb::tests {

void RunAudioSceneSystemTests() {
    RunMiniaudioPluginUpdatesSceneSourcesTest();
}

} // namespace kb::tests
