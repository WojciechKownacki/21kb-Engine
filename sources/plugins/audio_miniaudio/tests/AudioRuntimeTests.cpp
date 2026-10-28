#include "assets/MiniaudioClipResolver.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/audio/AudioClipAsset.hpp"
#include "engine/audio/AudioClipFormats.hpp"
#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/audio/AudioMixerAssetIO.hpp"
#include "engine/audio/AudioMixerEffectEditing.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneAudioListenerAccess.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "playback/MiniaudioVoicePool.hpp"
#include "runtime/MiniaudioEngine.hpp"
#include "runtime/MiniaudioPlaybackBackend.hpp"
#include "runtime/MiniaudioSound.hpp"
#include "scene/MiniaudioAudioSpace.hpp"
#include "scene/MiniaudioBusRegistry.hpp"
#include "scene/MiniaudioListenerSynchronizer.hpp"
#include "scene/MiniaudioSourceRegistry.hpp"

#include <cmath>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

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

[[nodiscard]] std::filesystem::path FixtureRoot() {
    return std::filesystem::path{ KB_AUDIO_RUNTIME_TEST_ASSET_DIR };
}

struct AudioFixture final {
    std::string_view extension;
    std::string_view filename;
    std::uintmax_t expectedBytes = 0U;
    std::array<unsigned char, 4U> magic{};
    std::size_t firstFrameBytes = 0U;
};

constexpr std::array<AudioFixture, 4U> kAudioFixtures{
    AudioFixture{ .extension = ".wav", .filename = "tone.wav", .expectedBytes = 22094U, .magic = { 'R', 'I', 'F', 'F' } },
    AudioFixture{ .extension = ".flac", .filename = "tone.flac", .expectedBytes = 4999U, .magic = { 'f', 'L', 'a', 'C' } },
    AudioFixture{ .extension = ".mp3", .filename = "tone.mp3", .expectedBytes = 3448U, .magic = { 0xFFU, 0xFBU, 0x70U, 0xC4U }, .firstFrameBytes = 313U },
    AudioFixture{ .extension = ".ogg", .filename = "tone.ogg", .expectedBytes = 5761U, .magic = { 'O', 'g', 'g', 'S' } },
};

[[nodiscard]] const AudioFixture& FixtureFor(std::string_view extension) {
    const auto iterator = std::ranges::find(kAudioFixtures, extension, &AudioFixture::extension);
    Require(iterator != kAudioFixtures.end(), "Advertised audio format has no decode fixture");
    return *iterator;
}

[[nodiscard]] std::filesystem::path FixturePath(const AudioFixture& fixture) {
    return FixtureRoot() / fixture.filename;
}

[[nodiscard]] kb::audio_miniaudio::ResolvedAudioClip DirectTestClip(
    const std::filesystem::path& path) {
    return kb::audio_miniaudio::ResolvedAudioClip{
        .path = path,
        .extension = path.extension().string(),
        .identity = path.generic_string(),
    };
}

[[nodiscard]] std::vector<char> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    Require(input.is_open(), "Audio fixture could not be opened");
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

[[nodiscard]] std::vector<std::uint8_t> ReadFileBytesU8(const std::filesystem::path& path) {
    const std::vector<char> bytes = ReadFileBytes(path);
    std::vector<std::uint8_t> result(bytes.size());
    std::ranges::transform(bytes, result.begin(), [](char value) {
        return static_cast<std::uint8_t>(static_cast<unsigned char>(value));
    });
    return result;
}

void WriteBytes(const std::filesystem::path& path, std::span<const char> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    Require(output.is_open(), "Audio payload output could not be opened");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Require(output.good(), "Audio payload output could not be written");
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

void WriteLongSparseWav(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    constexpr std::uint16_t channels = 1U;
    constexpr std::uint32_t sampleRate = 44100U;
    constexpr std::uint16_t bitsPerSample = 16U;
    constexpr std::uint32_t durationSeconds = 120U;
    constexpr std::uint32_t bytesPerSample = bitsPerSample / 8U;
    constexpr std::uint32_t dataSize = sampleRate * durationSeconds * channels * bytesPerSample;
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    Require(output.is_open(), "Long audio probe fixture could not be opened");
    output.write("RIFF", 4);
    WriteU32(output, 36U + dataSize);
    output.write("WAVEfmt ", 8);
    WriteU32(output, 16U);
    WriteU16(output, 1U);
    WriteU16(output, channels);
    WriteU32(output, sampleRate);
    WriteU32(output, sampleRate * channels * bytesPerSample);
    WriteU16(output, channels * bytesPerSample);
    WriteU16(output, bitsPerSample);
    output.write("data", 4);
    WriteU32(output, dataSize);
    output.seekp(static_cast<std::streamoff>(dataSize - 1U), std::ios::cur);
    output.put('\0');
    Require(output.good(), "Long sparse audio probe fixture could not be completed");
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
                .virtualPath = "/Audio/Runtime" + std::to_string(id) + path.extension().string(),
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
                  ma_uint64 read = 0U;
                  const auto result = engine.ReadPcm(frames.data(), frames.size() / 2U, &read);
                  if ((result != MA_SUCCESS && result != MA_AT_END) || read > frames.size() / 2U) finite_.store(false, std::memory_order_relaxed);
                  const auto count = std::min<std::size_t>(static_cast<std::size_t>(read) * 2U, frames.size());
                  std::uint64_t nonzero = 0U;
                  for (std::size_t i = 0U; i < count; ++i) {
                      if (!std::isfinite(frames[i])) finite_.store(false, std::memory_order_relaxed);
                      if (std::abs(frames[i]) > 1e-9F) ++nonzero;
                  }
                  frames_.fetch_add(read, std::memory_order_relaxed);
                  nonzero_.fetch_add(nonzero, std::memory_order_relaxed);
                  std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
              }
          }) {}

    ~OfflineEnginePump() {
        Stop();
    }

    void Stop() {
        if (!thread_.joinable()) {
            return;
        }
        stop_.store(true, std::memory_order_relaxed);
        thread_.join();
    }
    [[nodiscard]] bool Finite() const noexcept { return finite_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t Frames() const noexcept { return frames_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t Nonzero() const noexcept { return nonzero_.load(std::memory_order_relaxed); }

    OfflineEnginePump(const OfflineEnginePump&) = delete;
    OfflineEnginePump& operator=(const OfflineEnginePump&) = delete;

private:
    std::atomic<bool> stop_{ false };
    std::atomic<bool> finite_{true};
    std::atomic<std::uint64_t> frames_{0U};
    std::atomic<std::uint64_t> nonzero_{0U};
    std::thread thread_;
};

struct ChannelEnergy {
    double left = 0.0;
    double right = 0.0;
    [[nodiscard]] double Total() const noexcept { return left + right; }
};

[[nodiscard]] ChannelEnergy MeasureSpatialChannelEnergy(
    const std::filesystem::path& clipPath,
    float sourceX, float sourceY, float sourceZ) {
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    Require(engine.Status() == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Spatial attenuation probe did not create its offline engine");

    kb::scene::Scene scene;
    RegisterClip(scene, 8001U, clipPath);
    const kb::scene::SceneObject listener = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Attenuation Listener",
        .transform = TransformAt(0.0F),
    });
    scene.Components().AudioListeners().Set(listener.Entity(), kb::scene::AudioListenerComponent{
        .primary = true,
    });
    const kb::scene::SceneObject source = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Attenuation Source",
        .transform = TransformAt(sourceX, sourceY, sourceZ),
    });
    scene.Components().AudioSources().Set(source.Entity(), kb::scene::AudioSourceComponent{
        .clipAssetId = 8001U,
        .volume = 1.0F,
        .loop = true,
        .spatial = true,
        .autoplay = true,
        .spatialBlend = 1.0F,
        .attenuationModel = kb::audio::AudioAttenuationModel::Inverse,
        .minDistance = 2.0F,
        .maxDistance = 32.0F,
        .rolloff = 1.0F,
        .dopplerFactor = 0.0F,
    });

    kb::scene::SceneSystemContext context{ scene, 1.0F / 60.0F };
    kb::audio_miniaudio::MiniaudioListenerSynchronizer listenerSynchronizer;
    Require(listenerSynchronizer.Sync(engine.Native(), context).active,
        "Spatial attenuation probe did not activate its scene listener");
    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    kb::audio_miniaudio::MiniaudioBusRegistry buses{engine};
    kb::audio_miniaudio::MiniaudioSourceRegistry sources;
    sources.Sync(engine.Native(), context, resolver, buses, nullptr, {}, true);
    Require(sources.IsSourcePlaying(scene, source.Entity(), true).playing,
        "Spatial attenuation probe did not autoplay its scene source");

    std::array<float, 4096U * 2U> output{};
    ma_uint64 framesRead = 0U;
    const ma_result readResult = ma_engine_read_pcm_frames(
        &engine.Native(), output.data(), output.size() / 2U, &framesRead);
    Require((readResult == MA_SUCCESS || readResult == MA_AT_END) && framesRead > 0U,
        "Spatial attenuation probe could not render output frames");
    ChannelEnergy energy;
    for (std::size_t frame = 0U; frame < static_cast<std::size_t>(framesRead); ++frame) {
        energy.left += std::abs(static_cast<double>(output[frame * 2U]));
        energy.right += std::abs(static_cast<double>(output[frame * 2U + 1U]));
    }
    return energy;
}

[[nodiscard]] double MeasureSpatialOutputEnergy(const std::filesystem::path& clipPath, float sourceDistance) {
    return MeasureSpatialChannelEnergy(clipPath, sourceDistance, 0.0F, 0.0F).Total();
}

// Panning of a spatial source in the offline output: a source on the listener's right is louder in the right
// channel, one on the left in the left channel, mirror positions carry the same total energy, and a source straight
// ahead or straight behind is centred.
void RunSpatialPanningOutputTest() {
    const std::filesystem::path clipPath = FixturePath(FixtureFor(".wav"));
    const ChannelEnergy right = MeasureSpatialChannelEnergy(clipPath, 4.0F, 0.0F, 0.0F);
    const ChannelEnergy left = MeasureSpatialChannelEnergy(clipPath, -4.0F, 0.0F, 0.0F);
    Require(right.Total() > 1.0 && left.Total() > 1.0, "Panning probe did not render audible output");
    // The listener's right is +x when it faces -z, and the engine keeps the sign convention of the scene.
    const bool rightIsPlusX = right.right > right.left;
    Require(rightIsPlusX ? (right.right > right.left * 1.5 && left.left > left.right * 1.5)
                         : (right.left > right.right * 1.5 && left.right > left.left * 1.5),
        "A source to one side must be clearly louder in the channel of that side");
    Require(rightIsPlusX, "A source at +x must sound on the right of a listener at the origin");
    Require(std::abs(right.Total() - left.Total()) < 0.02 * right.Total(),
        "Mirror positions of a source must carry the same total energy");
    Require(std::abs(right.right - left.left) < 0.03 * right.right && std::abs(right.left - left.right) < 0.03 * right.right + 1.0,
        "Mirror positions of a source must swap the two channels");
    for (const float z : { 4.0F, -4.0F }) {
        const ChannelEnergy centred = MeasureSpatialChannelEnergy(clipPath, 0.0F, 0.0F, z);
        Require(centred.Total() > 1.0 && std::abs(centred.left - centred.right) < 0.15 * centred.Total(),
            "A source straight ahead or behind must be centred");
    }
    // A louder source panned to one side keeps its ratio: twice the distance halves the energy of both channels.
    const ChannelEnergy farRight = MeasureSpatialChannelEnergy(clipPath, 8.0F, 0.0F, 0.0F);
    Require(farRight.right < right.right * 0.7 && farRight.right > 0.0 && farRight.right > farRight.left,
        "A source twice as far must be quieter but stay on the same side");
}

void RunSpatialDistanceAttenuationOutputTest() {
    const std::filesystem::path clipPath = FixturePath(FixtureFor(".wav"));
    const double nearEnergy = MeasureSpatialOutputEnergy(clipPath, 2.0F);
    const double farEnergy = MeasureSpatialOutputEnergy(clipPath, 32.0F);
    Require(nearEnergy > 1.0,
        "Spatial attenuation probe did not render audible near-field output");
    Require(farEnergy > 0.0 && farEnergy < nearEnergy * 0.12,
        "Spatial attenuation did not reduce far-field output energy");
}

// Output energy of a spatial source `distance` metres along +x from a listener at `listenerPosition`, with the
// listener, source and mixer working through an audio space (docs/large_worlds.md).
[[nodiscard]] double MeasureAudioSpaceOutputEnergy(const std::filesystem::path& clipPath, const kb::math::DVec3& listenerPosition, double distance) {
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    kb::scene::Scene scene;
    RegisterClip(scene, 8002U, clipPath);
    const kb::scene::SceneObject listener = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Listener" });
    scene.Transforms().SetLocalTranslation(listener.Entity(), listenerPosition);
    scene.Components().AudioListeners().Set(listener.Entity(), kb::scene::AudioListenerComponent{ .primary = true });
    const kb::scene::SceneObject source = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Source" });
    scene.Transforms().SetLocalTranslation(source.Entity(), listenerPosition + kb::math::DVec3{ distance, 0.0, 0.0 });
    scene.Components().AudioSources().Set(source.Entity(), kb::scene::AudioSourceComponent{
        .clipAssetId = 8002U, .volume = 1.0F, .loop = true, .spatial = true, .autoplay = true, .spatialBlend = 1.0F,
        .attenuationModel = kb::audio::AudioAttenuationModel::Inverse, .minDistance = 2.0F, .maxDistance = 64.0F,
        .rolloff = 1.0F, .dopplerFactor = 0.0F,
    });
    scene.Runtime().SynchronizeTransforms();

    kb::scene::SceneSystemContext context{ scene, 1.0F / 60.0F };
    kb::audio_miniaudio::MiniaudioAudioSpace space;
    kb::audio_miniaudio::MiniaudioListenerSynchronizer listenerSynchronizer;
    const kb::audio_miniaudio::MiniaudioListenerSynchronizer::State state = listenerSynchronizer.Sync(engine.Native(), context, &space);
    Require(state.active && std::abs(state.position.x) <= 1024.0F && std::abs(state.position.z) <= 1024.0F,
        "The listener was not placed near the audio origin");
    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    kb::audio_miniaudio::MiniaudioBusRegistry buses{engine};
    kb::audio_miniaudio::MiniaudioSourceRegistry sources;
    sources.SetAudioSpace(&space);
    sources.Sync(engine.Native(), context, resolver, buses, nullptr, state.position, true);
    std::array<float, 4096U * 2U> output{};
    ma_uint64 framesRead = 0U;
    const ma_result readResult = ma_engine_read_pcm_frames(&engine.Native(), output.data(), output.size() / 2U, &framesRead);
    Require((readResult == MA_SUCCESS || readResult == MA_AT_END) && framesRead > 0U, "The audio space probe could not render output frames");
    double energy = 0.0;
    for (std::size_t sample = 0U; sample < static_cast<std::size_t>(framesRead) * 2U; ++sample) {
        energy += std::abs(static_cast<double>(output[sample]));
    }
    return energy;
}

// A listener ten thousand kilometres out hears a source 12.6 m away exactly as one near the origin does (in float
// the two positions would round to whole metres), and moving the audio origin with the listener keeps its velocity.
void RunFarListenerAudioSpaceTest() {
    const std::filesystem::path clipPath = FixturePath(FixtureFor(".wav"));
    const double nearEnergy = MeasureAudioSpaceOutputEnergy(clipPath, kb::math::DVec3{ 0.4, 0.0, 0.0 }, 12.6);
    const double farEnergy = MeasureAudioSpaceOutputEnergy(clipPath, kb::math::DVec3{ 10'000'000.4, 0.0, 10'000'000.0 }, 12.6);
    Require(nearEnergy > 1.0 && std::abs(farEnergy - nearEnergy) <= nearEnergy * 0.005,
        "A far listener did not hear a far source at its true distance");

    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    kb::scene::Scene scene;
    const kb::scene::SceneObject listener = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Moving Listener" });
    scene.Components().AudioListeners().Set(listener.Entity(), kb::scene::AudioListenerComponent{});
    const kb::math::DVec3 start{ 10'000'000.0 + 1023.5 - std::fmod(10'000'000.0, 1024.0), 0.0, 0.0 };
    scene.Transforms().SetLocalTranslation(listener.Entity(), start);
    kb::audio_miniaudio::MiniaudioAudioSpace space;
    static_cast<void>(space.Follow(start - kb::math::DVec3{ 1023.5, 0.0, 0.0 }));
    kb::audio_miniaudio::MiniaudioListenerSynchronizer synchronizer;
    kb::scene::SceneSystemContext first{ scene, 0.5F };
    Require(synchronizer.Sync(engine.Native(), first, &space).active, "The moving far listener was not selected");
    const kb::math::DVec3 origin = space.Origin();
    scene.Transforms().SetLocalTranslation(listener.Entity(), start + kb::math::DVec3{ 2.0, 0.0, 0.0 });
    kb::scene::SceneSystemContext second{ scene, 0.5F };
    const kb::audio_miniaudio::MiniaudioListenerSynchronizer::State moved = synchronizer.Sync(engine.Native(), second, &space);
    Require(space.Origin() != origin && moved.shift.x != 0.0F, "The audio origin did not follow the listener past its rebase distance");
    Require(Near(ma_engine_listener_get_velocity(&engine.Native(), 0U).x, 4.0F),
        "Moving the audio origin with the listener produced a velocity spike");
}

void RunAdvertisedFormatDecodeTest() {
    Require(kb::audio::kSupportedAudioClipExtensions.size() == kAudioFixtures.size(),
        "Advertised audio format count diverged from real decode fixtures");

    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    Require(engine.Status() == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Format decode test did not create its offline engine");

    std::size_t fixtureIndex = 0U;
    for (const std::string_view extension : kb::audio::kSupportedAudioClipExtensions) {
        const AudioFixture& fixture = FixtureFor(extension);
        const std::filesystem::path path = FixturePath(fixture);
        const std::vector<char> bytes = ReadFileBytes(path);
        Require(bytes.size() == fixture.expectedBytes && bytes.size() > fixture.magic.size(),
            "Audio decode fixture size changed unexpectedly");
        Require(std::equal(fixture.magic.begin(), fixture.magic.end(), bytes.begin(),
                    [](unsigned char expected, char actual) {
                        return expected == static_cast<unsigned char>(actual);
                    }),
            "Audio decode fixture magic is invalid");

        const bool spatial = fixtureIndex++ == 1U;
        kb::audio_miniaudio::MiniaudioSound sound;
        Require(sound.Initialize(engine.Native(), DirectTestClip(path), spatial) == MA_SUCCESS,
            "Advertised audio fixture could not initialize through the production sound");
        Require(sound.DecoderCountForTesting() == 0U
                && !sound.OwnsEncodedPayloadForTesting()
                && sound.UsesResourceManagerStreamForTesting(),
            "Advertised direct audio fixture did not use resource-manager streaming");
        sound.Apply(kb::audio_miniaudio::MiniaudioSoundSettings{
            .volume = 1.0F,
            .spatial = spatial,
            .spatialBlend = spatial ? 0.75F : 1.0F,
        });
        Require(sound.Start() == MA_SUCCESS,
            "Advertised audio fixture could not start through the production sound");

        std::array<float, 4096U * 2U> output{};
        double energy = 0.0;
        ma_uint64 pumpedFrames = 0U;
        for (std::uint32_t read = 0U; read < 4U; ++read) {
            ma_uint64 framesRead = 0U;
            const ma_result result = ma_engine_read_pcm_frames(
                &engine.Native(), output.data(), output.size() / 2U, &framesRead);
            Require(result == MA_SUCCESS || result == MA_AT_END,
                "Offline engine rejected an advertised decoded stream");
            pumpedFrames += framesRead;
            for (std::size_t sample = 0U; sample < static_cast<std::size_t>(framesRead) * 2U; ++sample) {
                energy += std::abs(static_cast<double>(output[sample]));
            }
        }
        Require(pumpedFrames > 0U && sound.PlaybackFrame() > 0U && energy > 1.0,
            "Advertised audio fixture start did not produce decoded nonzero PCM");
        sound.Reset();
    }

    const std::filesystem::path longPath = TestRoot() / "LongProbe.wav";
    WriteLongSparseWav(longPath);
    kb::scene::Scene validationScene;
    RegisterClip(validationScene, 8699U, longPath);
    kb::audio_miniaudio::MiniaudioClipResolver boundedResolver;
    Require(boundedResolver.Resolve(validationScene, 8699U).Succeeded(),
        "Long audio input failed bounded decode-readiness validation");
    const auto validationStats = boundedResolver.StatsForTesting();
    Require(validationStats.attempts == 1U
            && validationStats.maxDecodedProbeFrames <= 256U
            && validationStats.maxTailBytesInspected == 0U
            && validationStats.cacheEntries == 1U,
        "Long audio input caused validation work proportional to decoded duration");
    Require(boundedResolver.Resolve(validationScene, 8699U).Succeeded()
            && boundedResolver.StatsForTesting().attempts == 1U,
        "Repeated direct resolve did not reuse the bounded validation result");
    kb::assets::AssetMetadata revisedLongMetadata =
        *validationScene.Assets().Manager().Registry().Find(kb::assets::AssetId{ 8699U });
    ++revisedLongMetadata.contentHash;
    Require(validationScene.Assets().Manager().Registry().Upsert(std::move(revisedLongMetadata))
            && boundedResolver.Resolve(validationScene, 8699U).Succeeded(),
        "Changed direct asset revision did not refresh bounded validation");
    const auto revisedStats = boundedResolver.StatsForTesting();
    Require(revisedStats.attempts == 2U && revisedStats.cacheEntries == 1U,
        "Direct asset revision grew the per-asset validation cache");
    Require(validationScene.Assets().Manager().Registry().Remove(kb::assets::AssetId{ 8699U })
            && !boundedResolver.Resolve(validationScene, 8699U).Succeeded()
            && boundedResolver.StatsForTesting().cacheEntries == 0U,
        "Missing direct asset retained its validation cache entry");
    constexpr std::uint64_t cacheAssetBase = 900000U;
    const std::size_t cacheFixtureCount =
        kb::audio_miniaudio::MiniaudioClipResolver::ValidationCacheCapacityForTesting() + 5U;
    for (std::size_t index = 0U; index < cacheFixtureCount; ++index) {
        const std::uint64_t assetId = cacheAssetBase + index;
        RegisterClip(validationScene, assetId, longPath);
        Require(boundedResolver.Resolve(validationScene, assetId).Succeeded(),
            "Bounded validation cache fixture did not resolve");
    }
    const auto cappedStats = boundedResolver.StatsForTesting();
    Require(cappedStats.cacheEntries
                == kb::audio_miniaudio::MiniaudioClipResolver::ValidationCacheCapacityForTesting()
            && cappedStats.maxCacheEntries
                == kb::audio_miniaudio::MiniaudioClipResolver::ValidationCacheCapacityForTesting(),
        "Per-asset decode validation cache exceeded its deterministic capacity");
}

void RunVorbisUnicodePathTest() {
    const auto path = TestRoot() / std::filesystem::path{u8"Audio \u97F3\u983B \U0001F3B5.ogg"};
    std::filesystem::copy_file(FixturePath(FixtureFor(".ogg")), path,
        std::filesystem::copy_options::overwrite_existing);
    kb::scene::Scene scene;
    Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
        .id = kb::assets::AssetId{8870U}, .type = "AudioClip", .name = "Unicode Audio",
        .virtualPath = "/Audio/Unicode.ogg", .physicalPath = path, .contentHash = 8870U,
    }), "Unicode audio metadata registration failed");
    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    const auto resolved = resolver.Resolve(scene, 8870U);
    Require(resolved.Succeeded() && resolved.clip.path == path,
        "Unicode Vorbis file did not pass production decode readiness");
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    kb::audio_miniaudio::MiniaudioSound sound;
    Require(sound.Initialize(engine.Native(), resolved.clip, false) == MA_SUCCESS,
        "Unicode Vorbis file could not initialize its streamed sound");
    sound.Apply(kb::audio_miniaudio::MiniaudioSoundSettings{.volume = 1.0F, .spatial = false});
    Require(sound.Start() == MA_SUCCESS, "Unicode Vorbis stream could not start");
    std::array<float, 4096U * 2U> output{};
    ma_uint64 frames = 0U;
    Require(ma_engine_read_pcm_frames(&engine.Native(), output.data(), output.size() / 2U, &frames) == MA_SUCCESS,
        "Unicode Vorbis stream could not render PCM");
    double energy = 0.0;
    for (std::size_t index = 0U; index < static_cast<std::size_t>(frames) * 2U; ++index) {
        energy += std::abs(static_cast<double>(output[index]));
    }
    Require(frames > 0U && energy > 1.0, "Unicode Vorbis stream produced silent PCM");
}

void WriteToneWav(const std::filesystem::path& tonePath) {
    {
        std::ofstream output{tonePath, std::ios::binary | std::ios::trunc};
        output.write("RIFF", 4); WriteU32(output, 36U + 96000U); output.write("WAVEfmt ", 8);
        WriteU32(output, 16U); WriteU16(output, 1U); WriteU16(output, 1U);
        WriteU32(output, 48000U); WriteU32(output, 96000U); WriteU16(output, 2U); WriteU16(output, 16U);
        output.write("data", 4); WriteU32(output, 96000U);
        for (std::uint32_t frame = 0U; frame < 48000U; ++frame) {
            const auto sample = static_cast<std::int16_t>(3276.0 * std::sin(
                6.283185307179586 * 440.0 * static_cast<double>(frame) / 48000.0));
            WriteU16(output, static_cast<std::uint16_t>(sample));
        }
        Require(output.good(), "Snapshot PCM tone could not be written");
    }
}

void RunMixerDspAssetTest() {
    using Kind = kb::audio::AudioDspKind;
    kb::audio::AudioMixerAsset mixer;
    mixer.buses = {{.name = "Output", .volume = 0.123456791F}};
    std::size_t index = 0U;
    for (const auto kind : {Kind::LowPass, Kind::HighPass, Kind::PeakingEqualizer, Kind::Delay, Kind::Reverb, Kind::Compressor, Kind::Limiter}) {
        Require(kb::audio::AddAudioMixerEffect(mixer, "Output", "Stage" + std::to_string(index++), kind).empty(), "DSP kind could not be authored");
    }
    Require(kb::audio::AddAudioMixerEffect(mixer, "Output", "SecondDelay", Kind::Delay).empty(), "Repeated DSP kind could not be authored");
    const auto edit = [&](std::string_view id, std::string_view field, std::string_view value) {
        Require(kb::audio::SetAudioMixerEffectField(mixer, "Output", id, field, value).empty(), "DSP field edit failed");
    };
    edit("Stage0", "cutoffHz", "1234.5678901234567"); edit("Stage0", "order", "8");
    edit("Stage2", "gainDb", "-3.123456789012345"); edit("Stage2", "quality", "2.718281828459045");
    edit("Stage3", "delayMs", "12.345678901234567"); edit("Stage3", "feedback", "0.7"); edit("Stage3", "wet", "0.333");
    edit("Stage4", "roomSize", "0.8"); edit("Stage4", "damping", "0.6"); edit("Stage4", "width", "0.2");
    edit("Stage5", "thresholdDb", "-30.1234567890123"); edit("Stage5", "ratio", "3.141592653589793");
    edit("Stage6", "ceilingDb", "-1.1234567890123"); edit("Stage6", "releaseMs", "123.4567890123456");
    edit("SecondDelay", "bypass", "true"); edit("SecondDelay", "delayMs", "2000");
    Require(kb::audio::MoveAudioMixerEffect(mixer, "Output", "SecondDelay", 0U).empty(), "DSP reorder failed");
    const auto path = TestRoot() / "DspRoundTrip.kbmixer";
    Require(kb::audio::AudioMixerAssetIO::Save(path, mixer), "DSP mixer did not save");
    const auto loaded = kb::audio::AudioMixerAssetIO::Load(path);
    Require(loaded.has_value() && loaded->buses[0].effects == mixer.buses[0].effects
        && loaded->buses[0].volume == mixer.buses[0].volume, "DSP serialization lost exact parameters, order, IDs or bypass");
    const auto before = ReadFileBytesU8(path);
    Require(std::string{before.begin(), before.begin() + 10}.starts_with("kbmixer 2"), "DSP assets must require the effect format version");
    for (int failure = 0; failure < 5; ++failure) {
        auto invalid = mixer;
        if (failure == 0) invalid.buses[0].effects[0].id = invalid.buses[0].effects[1].id;
        if (failure == 1) invalid.buses[0].effects[0].id = "bad#id";
        if (failure == 2) invalid.buses[0].effects.resize(17U, invalid.buses[0].effects[0]);
        if (failure == 3) invalid.buses[0].effects[0].parameters.delayMs = std::numeric_limits<double>::quiet_NaN();
        if (failure == 4) invalid.buses[0].effects[0].parameters.gainDb = 1.0;
        Require(!kb::audio::ValidateAudioMixerAsset(invalid).empty() && !kb::audio::AudioMixerAssetIO::Save(path, invalid)
            && ReadFileBytesU8(path) == before, "Invalid DSP save changed the existing asset bytes");
    }
    const auto retained = mixer.buses[0].effects;
    Require(!kb::audio::SetAudioMixerEffectField(mixer, "Output", "Stage0", "gainDb", "0").empty()
        && !kb::audio::SetAudioMixerEffectField(mixer, "Output", "Stage0", "cutoffHz", "nan").empty()
        && !kb::audio::MoveAudioMixerEffect(mixer, "Output", "Stage0", 99U).empty()
        && mixer.buses[0].effects == retained, "Rejected DSP edit mutated authored state");
    std::size_t malformed = 0U;
    for (const auto text : {"kbmixer 1\nbus Output - 1 0\neffect Output X limiter 0 -1 100\n",
            "kbmixer 2\neffect Output X limiter 0 -1 100\n", "kbmixer 2\nbus Output - 1 0\neffect Output X unknown 0\n",
            "kbmixer 2\nbus Output - 1 0\neffect Output X delay 0 10 0 1\n",
            "kbmixer 2\nbus Output - 1 0\neffect Output X lowpass 0 1000 2.5\n",
            "kbmixer 2\nbus Output - 1 0\neffect Output X compressor 0 nan 4 10 100 0\n",
            "kbmixer 2\nbus Output - 1 0\neffect Output X limiter 0 -1 100 extra\n",
            "kbmixer 2\nbus Output - 1 0\neffect Output X limiter 0 -1 100\neffect Output X limiter 0 -2 100\n"}) {
        const auto bytes = std::string_view{text};
        Require(!kb::audio::AudioMixerAssetIO::Load(std::span{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()}).has_value(),
            "Malformed DSP record was ignored or silently downgraded"); ++malformed;
    }
    std::cout << "Mixer DSP asset exactStages=" << retained.size() << " invalidSaves=5 malformed=" << malformed << '\n';
}

void RunMixerDspRuntimeTest() {
    using Kind = kb::audio::AudioDspKind;
    using Playback = kb::audio::AudioPlayback;
    const auto tonePath = TestRoot() / "DspTone.wav"; WriteToneWav(tonePath);
    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(TestRoot() / "DspProject"), "DSP project did not mount");
    const auto imported = kb::assets::AssetImportService::ImportFiles(scene.Assets().Manager(), std::array{tonePath}, "/Game/Audio");
    Require(imported.Succeeded() && imported.items.size() == 1U, "DSP tone did not import");
    kb::audio::AudioMixerAsset mixer;
    mixer.buses = {{.name = "Output", .volume = 0.5F}, {.name = "Effects", .parentBus = "Output", .volume = 0.5F}};
    const auto path = TestRoot() / "DspMixer.kbmixer";
    constexpr kb::assets::AssetId mixerId{9501U};
    Require(kb::audio::AudioMixerAssetIO::Save(path, mixer), "DSP mixer did not save");
    Require(scene.Assets().Manager().RegisterAsset({.id = mixerId, .type = kb::audio::kAudioMixerAssetType, .name = "DspMixer",
        .virtualPath = "/Game/Audio/DspMixer.kbmixer", .physicalPath = path, .contentHash = 1U}), "DSP mixer did not register");
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, mixerId.value);
    kb::audio_miniaudio::MiniaudioPlaybackBackend backend;
    Playback::RegisterBackend(scene, backend);
    Require(Playback::ConfigureOffline(scene), "Actual provider did not enter explicit offline mode");
    const auto play = Playback::PlayOneShot(scene, {.clipAssetId = imported.items.front().id.value, .outputBus = "Effects", .volume = 1.0F, .loop = true, .spatial = false});
    Require(play.Succeeded(), "Public offline playback did not start the imported clip");
    const auto save = [&] {
        Require(kb::audio::AudioMixerAssetIO::Save(path, mixer) && scene.Assets().Manager().Unload(mixerId)
            && scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(mixerId).IsLoaded(), "DSP edit did not save, invalidate and reload");
    };
    std::uint64_t frames = 0U;
    const auto render = [&] {
        auto pcm = Playback::RenderOffline(scene, 2048U);
        Require(pcm.succeeded && pcm.frames == 2048U && pcm.channels == 2U && pcm.sampleRate == 48000U
            && pcm.samples.size() == 4096U, "Public DSP render did not return actual complete PCM"); frames += pcm.frames;
        return pcm.samples;
    };
    const auto measure = [&](std::string_view phase, double gain) {
        const double expected = (3276.0 / 32768.0) * gain * 2.0 / 3.141592653589793;
        std::size_t good = 0U; double mean = 0.0;
        for (std::size_t attempt = 0U; attempt < 12U && good < 4U; ++attempt) {
            const auto pcm = render(); mean = 0.0;
            for (float sample : pcm) mean += std::abs(sample);
            mean /= static_cast<double>(pcm.size());
            good = std::abs(mean / expected - 1.0) < 0.02 ? good + 1U : 0U;
        }
        Require(good == 4U && Playback::IsVoicePlaying(scene, play.voiceId), "DSP edit changed the wrong PCM or restarted its playing voice");
        std::cout << "Mixer DSP PCM " << phase << " mean=" << mean << " expected=" << expected << '\n';
    };
    measure("direct", 0.25);
    mixer.buses[1].effects = {{.id = "EchoA", .parameters = {.kind = Kind::Delay, .delayMs = 25.0, .wet = 0.25F, .dry = 0.5F}},
        {.id = "EchoB", .parameters = {.kind = Kind::Delay, .delayMs = 50.0, .wet = 0.5F, .dry = 0.25F}}}; save();
    measure("repeated-delay", 0.25 * 0.75 * 0.75);
    auto report = Playback::DspReport(scene);
    Require(report.backendAvailable && report.offline && report.buses.size() == 2U && report.buses[1].activeEffects == 2U
        && report.buses[1].settingsCurrent && report.buses[1].appliedEffects == mixer.buses[1].effects && report.renderedFrames == frames,
        "DSP diagnostics reported requested settings instead of actual executed state");
    kb::script::ScriptRuntimeHost dspApi{scene};
    const std::array dspArguments{kb::script::ScriptFunctionArgument{"bus", kb::script::ScriptValue{std::string{"Effects"}}}};
    const auto dspState = dspApi.Functions().Call("Audio.DspState", dspArguments,
        kb::script::ScriptFunctionCallContext{.scene = &scene});
    Require(dspState.Succeeded() && dspState.Output("current")->AsBool()
        && dspState.Output("activeEffects")->AsInt() == 2 && dspState.Output("error")->AsString().empty()
        && dspState.Output("sampleRate")->AsInt() == 48000 && dspState.Output("channels")->AsInt() == 2,
        "Audio.DspState did not expose the actual offline provider effects and format");
    mixer.buses[1].effects[0].parameters.bypass = true; save(); measure("bypass-one", 0.25 * 0.75);
    mixer.buses[1].effects = {{.id = "Equalizer", .parameters = {.kind = Kind::PeakingEqualizer, .cutoffHz = 440.0, .gainDb = 6.0}},
        {.id = "Gain", .parameters = {.kind = Kind::Compressor, .ratio = 1.0, .makeupDb = 6.0}}}; save();
    measure("equalizer-compressor", 0.25 * std::pow(10.0, 12.0 / 20.0));
    const auto applied = mixer.buses[1].effects;
    mixer.buses[1].effects = {{.id = "UnsupportedRate", .parameters = {.kind = Kind::LowPass, .cutoffHz = 94080.0}}}; save();
    measure("failed-edit-retains-PCM", 0.25 * std::pow(10.0, 12.0 / 20.0));
    report = Playback::DspReport(scene);
    Require(!report.buses[1].settingsCurrent && !report.buses[1].error.empty() && report.buses[1].appliedEffects == applied,
        "Failed DSP edit was presented as active or silently bypassed");
    mixer.buses[1].effects = applied; save(); measure("restored-settings", 0.25 * std::pow(10.0, 12.0 / 20.0));
    Require(Playback::DspReport(scene).buses[1].settingsCurrent, "Returning to applied settings left a stale DSP error");
    const double frequencyRatio = std::tan(3.141592653589793 * 440.0 / 48000.0)
        / std::tan(3.141592653589793 * 1000.0 / 48000.0);
    const double denominator = std::sqrt(1.0 + std::pow(frequencyRatio, 4.0));
    mixer.buses[1].effects = {{.id = "Low", .parameters = {.kind = Kind::LowPass, .cutoffHz = 1000.0}}};
    save(); measure("lowpass", 0.25 / denominator);
    mixer.buses[1].effects = {{.id = "High", .parameters = {.kind = Kind::HighPass, .cutoffHz = 1000.0}}};
    save(); measure("highpass", 0.25 * frequencyRatio * frequencyRatio / denominator);
    mixer.buses[1].effects = {{.id = "RoomA", .parameters = {.kind = Kind::Reverb, .wet = 0.0F, .dry = 0.5F}},
        {.id = "RoomB", .parameters = {.kind = Kind::Reverb, .wet = 0.0F, .dry = 0.5F}}};
    save(); measure("repeated-reverb-dry", 0.25 * 0.25);
    const auto peak = [](const auto& pcm) { double result = 0.0; for (float sample : pcm) result = std::max(result, std::abs(static_cast<double>(sample))); return result; };
    mixer.buses[1].effects = {{.id = "Boost", .parameters = {.kind = Kind::Compressor, .ratio = 1.0, .makeupDb = 12.0}},
        {.id = "Ceiling", .parameters = {.kind = Kind::Limiter, .releaseMs = 1.0, .ceilingDb = -24.0}}};
    save(); static_cast<void>(render());
    const double limited = peak(render());
    const double ceiling = 0.25 * std::pow(10.0, -24.0 / 20.0);
    Require(std::abs(limited - ceiling) < 1e-6, "Actual bus limiter did not apply its sample ceiling");
    std::swap(mixer.buses[1].effects[0], mixer.buses[1].effects[1]); save(); static_cast<void>(render());
    const double boosted = peak(render());
    Require(std::abs(boosted - ceiling * std::pow(10.0, 12.0 / 20.0)) < 1e-5
        && boosted > limited * 3.9 && Playback::IsVoicePlaying(scene, play.voiceId),
        "Nonlinear effect reorder did not change actual PCM in the authored order");
    std::cout << "Mixer DSP nonlinear limited=" << limited << " reordered=" << boosted << '\n';
    mixer.buses[1].effects = {{.id = "Tail", .parameters = {.kind = Kind::Delay, .delayMs = 25.0, .feedback = 0.8F, .wet = 1.0F, .dry = 0.0F}}}; save();
    for (int block = 0; block < 8; ++block) static_cast<void>(render());
    Require(Playback::StopVoice(scene, play.voiceId), "DSP source did not stop");
    Require(peak(render()) > 1e-4, "Stopping a source removed its actual delay tail");
    mixer.buses[1].mute = true; save(); static_cast<void>(render());
    Require(peak(render()) == 0.0, "Bus mute did not silence post-effect tails");
    mixer.buses[1].mute = false; save(); Require(peak(render()) > 1e-6, "Unmuting discarded retained DSP state");
    mixer.buses[1].effects = {{.id = "Room", .parameters = {.kind = Kind::Reverb, .wet = 1.0F, .dry = 0.0F}}}; save();
    const auto roomVoice = Playback::PlayOneShot(scene, {.clipAssetId = imported.items.front().id.value, .outputBus = "Effects",
        .volume = 1.0F, .loop = true, .spatial = false});
    Require(roomVoice.Succeeded(), "Actual reverb source did not start");
    for (int block = 0; block < 8; ++block) static_cast<void>(render());
    Require(Playback::StopVoice(scene, roomVoice.voiceId) && peak(render()) > 1e-5, "Source stop removed the actual reverb tail");
    mixer.buses[1].mute = true; save(); static_cast<void>(render());
    Require(peak(render()) == 0.0, "Post-effect mute leaked the reverb tail");
    mixer.buses[1].mute = false; save(); Require(peak(render()) > 1e-6, "Post-effect unmute discarded the reverb tail");
    Require(!Playback::RenderOffline(scene, 0U).succeeded && !Playback::RenderOffline(scene, 384001U).succeeded,
        "Offline PCM frame limits were bypassed");
    Playback::UnregisterBackend(scene, backend); backend.Shutdown();
    const auto resources = backend.ResourcesForTesting();
    Require(resources.buses == 0U && resources.voices == 0U && resources.sourceSounds == 0U, "DSP shutdown retained native routing resources");
    std::cout << "Mixer DSP runtime realFrames=" << frames << " voicePreserved=1 failedEditPreserved=1 mutedTail=0 shutdownBuses=0\n";
}

void RunMixerMeterRuntimeTest() {
    using Playback = kb::audio::AudioPlayback;
    using Meters = kb::audio::AudioMetering;
    kb::scene::Scene scene;
    Require(!Meters::Report(scene).backendAvailable && !Meters::Configure(scene, true), "Missing metering backend was reported as available");
    const auto tone = TestRoot() / "MeterTone.wav"; WriteToneWav(tone);
    const auto root = TestRoot() / "MeterProject";
    Require(scene.Assets().MountProject(root), "Meter project did not mount");
    const auto imported = kb::assets::AssetImportService::ImportFiles(scene.Assets().Manager(), std::array{tone}, "/Game/Audio");
    Require(imported.Succeeded() && imported.items.size() == 1U, "Meter tone did not import");
    kb::audio::AudioMixerAsset mixer;
    mixer.buses = {{.name = "Output", .volume = 0.25F}, {.name = "Signal", .parentBus = "Output"}};
    const auto path = root / "Meters.kbmixer";
    constexpr kb::assets::AssetId mixerId{9801U};
    Require(kb::audio::AudioMixerAssetIO::Save(path, mixer)
        && scene.Assets().Manager().RegisterAsset({.id = mixerId, .type = kb::audio::kAudioMixerAssetType,
            .name = "Meters", .virtualPath = "/Game/Meters.kbmixer", .physicalPath = path, .contentHash = 1U}), "Meter mixer did not register");
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, mixerId.value);
    kb::audio_miniaudio::MiniaudioPlaybackBackend backend;
    Playback::RegisterBackend(scene, backend);
    Require(Meters::Report(scene).supported && !Meters::Report(scene).initialized && !Meters::Configure(scene, true),
        "Uninitialized meter provider was reported as executing");
    Require(Playback::ConfigureOffline(scene), "Meter provider did not configure offline");
    const auto voice = Playback::PlayOneShot(scene, {.clipAssetId = imported.items.front().id.value,
        .outputBus = "Signal", .loop = true, .spatial = false});
    Require(voice.Succeeded(), "Meter tone did not start");
    const auto save = [&] {
        Require(kb::audio::AudioMixerAssetIO::Save(path, mixer) && scene.Assets().Manager().Unload(mixerId)
            && scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(mixerId).IsLoaded(), "Meter fixture edit did not reload");
    };
    std::uint64_t rendered = 0U;
    const auto render = [&] {
        auto pcm = Playback::RenderOffline(scene, 512U);
        Require(pcm.succeeded && pcm.frames == 512U && pcm.samples.size() == 1024U, "Meter did not mix actual complete PCM");
        rendered += pcm.frames; return pcm;
    };
    const auto bus = [](const kb::audio::AudioMeterReport& report, std::string_view name) -> const kb::audio::AudioMeterReading& {
        const auto found = std::find_if(report.buses.begin(), report.buses.end(), [&](const auto& item) { return item.bus == name; });
        Require(found != report.buses.end() && found->available, "Installed meter bus is missing or unavailable"); return *found;
    };
    static_cast<void>(render());
    Require(!Meters::Report(scene).captureEnabled && Meters::Report(scene).master.processedFrames == 0U,
        "Reading meters implicitly enabled capture");
    Require(Meters::Configure(scene, true), "Explicit capture did not enable");
    kb::audio::AudioOfflinePcm pcm;
    for (unsigned i = 0U; i < 8U; ++i) pcm = render();
    auto report = Meters::Report(scene);
    Require(report.initialized && report.supported && report.captureEnabled && report.sampleRate == 48000U
        && report.mixerAssetId == mixerId.value && report.master.available && report.master.channels == 2U
        && report.master.processedFrames == 4096U && report.master.blockFrames == 512U, "Master meter format or actual frame counts changed");
    for (std::size_t channel = 0U; channel < 2U; ++channel) {
        double energy = 0.0; float peak = 0.0F;
        for (std::size_t i = channel; i < pcm.samples.size(); i += 2U) { peak = std::max(peak, std::abs(pcm.samples[i])); energy += static_cast<double>(pcm.samples[i]) * pcm.samples[i]; }
        Require(report.master.channel[channel].peak == peak && std::abs(report.master.channel[channel].rms - std::sqrt(energy / 512.0)) < 1e-8,
            "Master meter differs from independently analyzed actual PCM");
    }
    const auto& child = bus(report, "Signal"); const auto& parent = bus(report, "Output");
    Require(std::abs(child.channel[0].heldPeak - 3276.0F / 32768.0F) < 1e-6F
        && std::abs(parent.channel[0].heldPeak * 4.0F - child.channel[0].heldPeak) < 1e-6F
        && parent.channel[0].heldPeak == report.master.channel[0].heldPeak, "Bus meter is not after its fader and before its parent");
    const auto beforeReset = Playback::VoicePlaybackSeconds(scene, voice.voiceId);
    Require(Meters::Reset(scene) && Meters::Report(scene).master.processedFrames == 0U
        && Playback::VoicePlaybackSeconds(scene, voice.voiceId) == beforeReset, "Meter reset restarted the voice");
    mixer.buses[1].effects = {{.id = "Boost", .parameters = {.kind = kb::audio::AudioDspKind::Compressor,
        .thresholdDb = 0.0, .ratio = 1.0, .attackMs = 0.0, .makeupDb = 24.0}}}; save();
    for (unsigned i = 0U; i < 4U; ++i) static_cast<void>(render());
    report = Meters::Report(scene);
    Require(bus(report, "Signal").channel[0].heldPeak > 1.5F && bus(report, "Signal").channel[0].clippedSamples > 0U
        && bus(report, "Output").channel[0].clippedSamples == 0U && report.master.channel[0].clippedSamples == 0U,
        "Post-DSP clipping did not distinguish the child from attenuated parent/master");
    const auto framesBeforeEdit = bus(report, "Signal").processedFrames;
    mixer.buses[1].effects = {{.id = "InvalidRate", .parameters = {.kind = kb::audio::AudioDspKind::LowPass, .cutoffHz = 24000.0}}}; save();
    const auto retainedPcm = render(); report = Meters::Report(scene);
    float retainedPeak = 0.0F;
    for (const auto sample : retainedPcm.samples) retainedPeak = std::max(retainedPeak, std::abs(sample));
    Require(!Playback::DspReport(scene).buses[1].settingsCurrent && bus(report, "Signal").processedFrames > framesBeforeEdit
        && bus(report, "Signal").channel[0].heldPeak > 1.5F && retainedPeak > 0.375F
        && Playback::DspReport(scene).buses[1].appliedEffects[0].parameters.makeupDb == 24.0
        && Playback::IsVoicePlaying(scene, voice.voiceId),
        "Failed DSP edit discarded the installed meter or previous signal");
    mixer.buses[1].effects.clear(); mixer.buses[0].mute = true; save();
    static_cast<void>(render()); Require(Meters::Reset(scene), "Mute capture did not reset");
    for (unsigned i = 0U; i < 4U; ++i) static_cast<void>(render());
    report = Meters::Report(scene);
    Require(bus(report, "Signal").channel[0].heldPeak > 0.09F && bus(report, "Output").channel[0].heldPeak == 0.0F
        && report.master.channel[0].heldPeak == 0.0F, "Parent mute erased child diagnostics or leaked output");
    mixer.buses[0].mute = false; save(); static_cast<void>(render());
    Require(Meters::Configure(scene, false), "Capture did not disable");
    pcm = render(); report = Meters::Report(scene);
    Require(!report.captureEnabled && report.master.processedFrames == 0U && bus(report, "Signal").processedFrames == 0U
        && std::any_of(pcm.samples.begin(), pcm.samples.end(), [](float value) { return std::abs(value) > 0.01F; }),
        "Disabling capture muted playback or kept accumulating");
    backend.Shutdown(); report = Meters::Report(scene);
    Require(report.supported && !report.initialized && !report.captureEnabled && !report.master.available && report.buses.empty()
        && backend.ResourcesForTesting().buses == 0U, "Meter shutdown retained native resources or stale readings");
    Playback::UnregisterBackend(scene, backend);
    Require(!Meters::Report(scene).backendAvailable, "Unregistered metering provider remained available");
    std::cout << "Mixer meters actualFrames=" << rendered << " masterPCM=exact postDSP=1 postFader=1 clipping=1 mutedParent=0 resetPreservesVoice=1 shutdownBuses=0\n";
}

void RunMixerDspWorkloadBenchmark(bool capture = false) {
    using Playback = kb::audio::AudioPlayback;
    using Kind = kb::audio::AudioDspKind;
    const auto memory = [] {
        std::array<std::uint64_t, 2U> values{};
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS_EX counters{};
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
            values = {static_cast<std::uint64_t>(counters.WorkingSetSize), static_cast<std::uint64_t>(counters.PrivateUsage)};
#endif
        return values;
    };
    const auto cpu = [](bool process) -> std::uint64_t {
#if defined(_WIN32)
        FILETIME created{}, exited{}, kernel{}, user{};
        const bool valid = process ? GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != 0
            : GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) != 0;
        if (valid) return ((static_cast<std::uint64_t>(kernel.dwHighDateTime) << 32U) + kernel.dwLowDateTime
            + (static_cast<std::uint64_t>(user.dwHighDateTime) << 32U) + user.dwLowDateTime) * 100U;
#else
        static_cast<void>(process);
#endif
        return 0U;
    };
    for (const bool effects : {false, true}) {
        const auto before = memory();
        std::array<std::uint64_t, 2U> active{};
        std::uint64_t realFrames = 0U, mixWall = 0U, threadCpu = 0U, processCpu = 0U;
        std::array<std::uint64_t, 12U> batches{};
        double peak = 0.0, energy = 0.0;
        const std::size_t busCount = effects ? 8U : 1U, voiceCount = busCount * 8U;
        {
            kb::scene::Scene scene;
            const auto root = TestRoot() / (effects ? "EffectsWorkload" : "PlainWorkload");
            Require(scene.Assets().MountProject(root), "Audio workload project did not mount");
            std::vector<std::filesystem::path> clips;
            for (std::size_t i = 0U; i < busCount; ++i) {
                const auto file = TestRoot() / ("WorkloadTone" + std::to_string(i) + ".wav");
                WriteToneWav(file); clips.push_back(file);
            }
            const auto imported = kb::assets::AssetImportService::ImportFiles(scene.Assets().Manager(), clips, "/Game/Audio");
            Require(imported.Succeeded() && imported.items.size() == busCount, "Audio workload clips did not import");
            kb::audio::AudioMixerAsset mixer;
            for (std::size_t i = 0U; i < busCount; ++i) {
                kb::audio::AudioMixerBus bus{.name = "Bus" + std::to_string(i)};
                if (effects) bus.effects = {
                    {.id = "Low", .parameters = {.kind = Kind::LowPass, .cutoffHz = 12000.0}},
                    {.id = "High", .parameters = {.kind = Kind::HighPass, .cutoffHz = 80.0}},
                    {.id = "Tone", .parameters = {.kind = Kind::PeakingEqualizer, .cutoffHz = 440.0, .gainDb = 6.0}},
                    {.id = "Echo", .parameters = {.kind = Kind::Delay, .delayMs = 20.0, .feedback = 0.4F, .wet = 0.2F, .dry = 0.8F}},
                    {.id = "Room", .parameters = {.kind = Kind::Reverb, .wet = 0.2F, .dry = 0.8F}},
                    {.id = "Dynamics", .parameters = {.kind = Kind::Compressor, .thresholdDb = -24.0, .ratio = 3.0}},
                    {.id = "Ceiling", .parameters = {.kind = Kind::Limiter}}};
                mixer.buses.push_back(std::move(bus));
            }
            const auto path = root / "Workload.kbmixer";
            constexpr kb::assets::AssetId mixerId{9701U};
            Require(kb::audio::AudioMixerAssetIO::Save(path, mixer)
                && scene.Assets().Manager().RegisterAsset({.id = mixerId, .type = kb::audio::kAudioMixerAssetType,
                    .name = "Workload", .virtualPath = "/Game/Workload.kbmixer", .physicalPath = path, .contentHash = 1U}),
                "Audio workload mixer did not register");
            kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, mixerId.value);
            kb::audio_miniaudio::MiniaudioPlaybackBackend backend;
            Playback::RegisterBackend(scene, backend);
            Require(Playback::ConfigureOffline(scene), "Audio workload provider did not configure offline");
            std::vector<std::uint64_t> voices;
            for (std::size_t i = 0U; i < voiceCount; ++i) {
                const auto voice = Playback::PlayOneShot(scene, {.clipAssetId = imported.items[i / 8U].id.value,
                    .outputBus = mixer.buses[i / 8U].name, .volume = 0.5F / static_cast<float>(voiceCount), .loop = true, .spatial = false});
                Require(voice.Succeeded(), "Audio workload voice did not start"); voices.push_back(voice.voiceId);
            }
            const auto verifyVoices = [&] {
                Require(backend.ResourcesForTesting().voices == voiceCount, "Audio workload silently stole a voice");
                for (const auto voice : voices) Require(Playback::IsVoicePlaying(scene, voice), "Audio workload lost a playing voice");
            };
            verifyVoices();
            if (capture) Require(kb::audio::AudioMetering::Configure(scene, true), "Workload meters did not enable");
            for (int i = 0; i < 8; ++i) Require(Playback::RenderOffline(scene, 1024U).succeeded, "Audio workload warmup did not execute");
            active = memory();
            const auto threadStart = cpu(false), processStart = cpu(true);
            for (auto& batch : batches) {
                for (int i = 0; i < 32; ++i) {
                    const auto begin = std::chrono::steady_clock::now();
                    const auto pcm = Playback::RenderOffline(scene, 1024U);
                    batch += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - begin).count());
                    Require(pcm.succeeded && pcm.frames == 1024U && pcm.channels == 2U && pcm.sampleRate == 48000U
                        && pcm.samples.size() == 2048U, "Audio workload did not produce actual complete PCM");
                    realFrames += pcm.frames;
                    for (const float sample : pcm.samples) {
                        Require(std::isfinite(sample), "Audio workload produced non-finite PCM");
                        peak = std::max(peak, std::abs(static_cast<double>(sample))); energy += static_cast<double>(sample) * sample;
                    }
                }
                mixWall += batch;
            }
            threadCpu = cpu(false) - threadStart; processCpu = cpu(true) - processStart;
            verifyVoices();
            const auto report = Playback::DspReport(scene);
            Require(report.buses.size() == busCount, "Audio workload lost its active buses");
            for (const auto& bus : report.buses) Require(bus.initialized && bus.settingsCurrent && bus.activeEffects == (effects ? 7U : 0U),
                "Audio workload skipped requested DSP processing");
            const auto meters = kb::audio::AudioMetering::Report(scene);
            Require(meters.captureEnabled == capture && meters.master.available
                && meters.master.processedFrames == (capture ? 401408U : 0U), "Workload master capture did not observe actual warmup and measured frames");
            for (const auto& bus : meters.buses) Require(bus.available && (capture ? bus.processedFrames > 0U : bus.processedFrames == 0U),
                "Workload bus meter did not follow explicit capture");
            Require(realFrames == 393216U && peak > 1e-5 && energy > 0.0, "Audio workload readback is empty or silent");
            Playback::UnregisterBackend(scene, backend); backend.Shutdown();
            const auto released = backend.ResourcesForTesting();
            Require(released.voices == 0U && released.buses == 0U && released.decoders == 0U, "Audio workload shutdown retained native resources");
        }
        const auto after = memory();
        std::sort(batches.begin(), batches.end());
        std::cout << "Audio workload kind=" << (effects ? "effects" : "plain") << " buses=" << busCount << " voices=" << voiceCount
            << " meterCapture=" << capture
            << " realFrames=" << realFrames << " mixWallNs=" << mixWall << " medianBatchMixWallNs=" << batches[batches.size()/2U]
            << " maxBatchMixWallNs=" << batches.back() << " probeOwnerThreadCpuNs=";
        if (threadCpu == 0U) std::cout << "unavailable"; else std::cout << threadCpu;
        std::cout << " probeProcessCpuNs=";
        if (processCpu == 0U) std::cout << "unavailable"; else std::cout << processCpu;
        const auto printMemory = [](std::string_view label, std::uint64_t value) {
            std::cout << ' ' << label << '=';
            if (value == 0U) std::cout << "unavailable"; else std::cout << value;
        };
        printMemory("rssBefore", before[0]); printMemory("rssActive", active[0]); printMemory("rssAfterScene", after[0]);
        printMemory("privateCommitBefore", before[1]); printMemory("privateCommitActive", active[1]); printMemory("privateCommitAfterScene", after[1]);
        std::cout << " peak=" << peak << " rms=" << std::sqrt(energy / static_cast<double>(realFrames * 2U)) << '\n';
    }
}

void RunMixerDspConcurrentTest(bool deviceCallback) {
    struct Context {
        ma_context native{}; bool ready = false;
        ~Context() { if (ready) ma_context_uninit(&native); }
    } context;
    if (deviceCallback) {
        const ma_backend backend = ma_backend_null;
        context.ready = ma_context_init(&backend, 1U, nullptr, &context.native) == MA_SUCCESS;
        Require(context.ready, "Native null audio device context did not initialize");
    }
    kb::scene::Scene scene;
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(!deviceCallback, deviceCallback ? &context.native : nullptr);
    Require(engine.IsInitialized() && (!deviceCallback || engine.IsPlaybackAvailable()),
        "DSP device callback test fell back instead of starting its native device");
    kb::audio_miniaudio::MiniaudioBusRegistry buses{engine};
    kb::audio::AudioMixerAsset mixer; mixer.buses = {{.name = "Output", .volume = 0.5F}};
    const auto path = TestRoot() / (deviceCallback ? "DspDeviceThread.kbmixer" : "DspReadThread.kbmixer");
    constexpr kb::assets::AssetId mixerId{9601U};
    Require(kb::audio::AudioMixerAssetIO::Save(path, mixer)
        && scene.Assets().Manager().RegisterAsset({.id = mixerId, .type = kb::audio::kAudioMixerAssetType,
            .name = "ConcurrentMixer", .virtualPath = "/Game/Audio/ConcurrentMixer.kbmixer", .physicalPath = path, .contentHash = 1U}),
        "Concurrent DSP mixer did not register");
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, mixerId.value);
    Require(buses.Sync(scene, true), "Concurrent DSP bus did not initialize");
    const auto group = buses.Resolve("Output").group;
    Require(group != nullptr, "Concurrent DSP route is unavailable");
    struct Source {
        kb::audio_miniaudio::MiniaudioEngine& engine;
        ma_waveform wave{}; ma_data_source_node node{};
        bool waveReady = false, nodeReady = false;
        ~Source() {
            const auto lock = engine.LockGraph();
            if (nodeReady) ma_data_source_node_uninit(&node, nullptr);
            if (waveReady) ma_waveform_uninit(&wave);
        }
    } source{engine};
    const auto rate = ma_engine_get_sample_rate(&engine.Native());
    const auto channels = ma_engine_get_channels(&engine.Native());
    Require(channels == 2U, "Concurrent DSP fixture requires the configured stereo output");
    {
        const auto lock = engine.LockGraph();
        Require(engine.MasterMeter().Configure(channels, true), "Concurrent master meter did not enable");
        buses.ConfigureMeters(true);
    }
    const auto waveConfig = ma_waveform_config_init(ma_format_f32, channels, rate, ma_waveform_type_sine,
        0.02, 440.0);
    source.waveReady = ma_waveform_init(&waveConfig, &source.wave) == MA_SUCCESS;
    Require(source.waveReady, "Concurrent DSP source did not initialize");
    const auto nodeConfig = ma_data_source_node_config_init(&source.wave);
    source.nodeReady = ma_data_source_node_init(ma_engine_get_node_graph(&engine.Native()), &nodeConfig, nullptr, &source.node) == MA_SUCCESS;
    Require(source.nodeReady, "Concurrent DSP source node did not initialize");
    {
        const auto lock = engine.LockGraph();
        Require(ma_node_attach_output_bus(&source.node, 0U, group, 0U) == MA_SUCCESS, "Concurrent DSP source did not attach");
    }
    std::unique_ptr<OfflineEnginePump> pump;
    if (!deviceCallback) pump = std::make_unique<OfflineEnginePump>(engine);
    const auto waitForRead = [&](std::uint64_t previous) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
        while (engine.RenderedFrames() <= previous && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        Require(engine.RenderedFrames() > previous, "DSP processing thread did not execute actual PCM reads");
    };
    waitForRead(0U);
    const auto previous = engine.RenderedFrames(); const auto generation = buses.Generation();
    for (std::size_t edit = 0U; edit < 64U; ++edit) {
        kb::audio::AudioDspParameters parameters{.kind = static_cast<kb::audio::AudioDspKind>(edit % 7U)};
        parameters.bypass = edit % 5U == 0U;
        mixer.buses[0].effects = {{.id = "Stage", .parameters = parameters}};
        if (edit % 3U == 0U) mixer.buses[0].effects.push_back({.id = "Echo", .parameters = {
            .kind = kb::audio::AudioDspKind::Delay, .delayMs = 1.0, .feedback = 0.5F}});
        if (edit % 4U == 0U) mixer.buses[0].effects.clear();
        Require(kb::audio::AudioMixerAssetIO::Save(path, mixer) && scene.Assets().Manager().Unload(mixerId)
            && scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(mixerId).IsLoaded(), "Concurrent DSP edit did not reload");
        Require(!buses.Sync(scene, true) && buses.Generation() == generation && buses.Resolve("Output").group == group,
            "DSP-only edit rebuilt the native group while PCM was running");
        const auto report = buses.DspReport();
        Require(report.buses[0].initialized && report.buses[0].settingsCurrent && report.buses[0].error.empty()
            && report.buses[0].appliedEffects == mixer.buses[0].effects, "Concurrent DSP edit was skipped or bypassed");
        const auto meters = buses.MeterReport();
        Require(meters.buses.size() == 1U && (!meters.buses[0].available || (meters.buses[0].channels == channels
            && std::isfinite(meters.buses[0].channel[0].rms) && meters.buses[0].channel[0].nonFiniteSamples == 0U)),
            "Concurrent meter snapshot was incoherent or non-finite");
        if (edit % 7U == 0U) {
            const auto lock = engine.LockGraph(); engine.MasterMeter().Reset(); buses.ResetMeters();
        }
    }
    waitForRead(previous);
    waitForRead(engine.RenderedFrames());
    if (pump != nullptr) {
        pump->Stop();
        Require(pump->Finite() && pump->Frames() > 0U && pump->Nonzero() > 0U,
            "Concurrent DSP readback was non-finite, silent or did not execute");
    }
    std::uint64_t meterFrames = 0U;
    {
        const auto lock = engine.LockGraph();
        kb::audio_miniaudio::MiniaudioMeterSnapshot master;
        Require(engine.MasterMeter().TryRead(master) && master.enabled && master.processedFrames > 0U
            && master.channel[0].heldPeak > 0.0F && master.channel[0].nonFiniteSamples == 0U,
            "Native processing did not publish actual master signal");
        const auto meters = buses.MeterReport();
        Require(meters.buses[0].available && meters.buses[0].processedFrames > 0U && meters.buses[0].channel[0].heldPeak > 0.0F,
            "Native processing did not publish actual bus signal");
        meterFrames = master.processedFrames;
    }
    std::cout << "Mixer DSP concurrent deviceCallback=" << deviceCallback << " edits=64 actualFrames=" << engine.RenderedFrames()
        << " meterFrames=" << meterFrames << " finiteReadback=" << (pump != nullptr ? "verified" : "meter-verified") << " groupPreserved=1\n";
}

void RunBusTopologyPcmTest() {
    const auto tonePath = TestRoot() / "RoutingTone.wav";
    WriteToneWav(tonePath);
    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(TestRoot() / "RoutingPcmProject"), "Bus PCM project did not mount");
    const std::array files{tonePath};
    const auto imported = kb::assets::AssetImportService::ImportFiles(scene.Assets().Manager(), files, "/Game/Audio");
    Require(imported.Succeeded() && imported.items.size() == 1U, "Bus PCM tone did not import");
    const auto clipId = imported.items.front().id;
    kb::audio::AudioMixerAsset mixer;
    mixer.buses = {{.name = "Output", .volume = 0.5F}, {.name = "Effects", .parentBus = "Output", .volume = 0.5F}};
    const auto mixerPath = TestRoot() / "RoutingMixer.kbmixer";
    constexpr kb::assets::AssetId mixerId{9401U};
    Require(kb::audio::AudioMixerAssetIO::Save(mixerPath, mixer), "Bus PCM mixer did not save");
    Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
        .id = mixerId, .type = kb::audio::kAudioMixerAssetType, .name = "RoutingMixer",
        .virtualPath = "/Game/Audio/RoutingMixer.kbmixer", .physicalPath = mixerPath, .contentHash = 1U,
    }), "Bus PCM mixer did not register");
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, mixerId.value);
    kb::audio_miniaudio::MiniaudioPlaybackBackend backend;
    Require(backend.ReinitializeForTesting(scene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Bus PCM backend must use its actual offline engine");
    kb::audio::AudioPlayDesc desc{.clipAssetId = clipId.value, .volume = 0.25F, .loop = true, .spatial = false};
    desc.outputBus = "Effects";
    const auto play = [&] {
        const auto result = backend.PlayOneShotForTesting(scene, desc);
        Require(result.Succeeded() && backend.IsVoicePlaying(scene, result.voiceId), "Bus PCM voice did not start");
        Require(!backend.VoiceUsesResourceManagerStreamForTesting(result.voiceId), "Imported bus PCM tone must use owned encoded bytes");
        return result.voiceId;
    };
    const auto measure = [&](std::string_view phase, double gain) {
        const double expected = (3276.0 / 32768.0) * gain * 2.0 / 3.141592653589793;
        std::size_t good = 0U;
        double mean = 0.0;
        for (std::size_t attempt = 0U; attempt < 8U && good < 4U; ++attempt) {
            const auto block = backend.PumpFramesForTesting(2048U);
            Require(block.frames == 2048U && std::isfinite(block.energy), "Bus PCM block is incomplete or non-finite");
            mean = block.energy / (2.0 * static_cast<double>(block.frames));
            const bool matches = expected == 0.0 ? mean < 0.000001 : std::abs(mean / expected - 1.0) < 0.02;
            good = matches ? good + 1U : 0U;
        }
        std::cout << "Bus PCM " << phase << " gain=" << gain << " mean=" << mean << " expected=" << expected << " blocks=" << good << '\n';
        Require(good == 4U, "Live mixer edit did not produce the expected PCM");
    };
    const auto save = [&] {
        Require(kb::audio::AudioMixerAssetIO::Save(mixerPath, mixer), "Bus PCM edit did not save");
        Require(scene.Assets().Manager().Unload(mixerId), "Bus PCM edit did not invalidate the mixer cache");
        Require(scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(mixerId).IsLoaded(), "Bus PCM edit did not reload");
    };
    const auto trigger = [&] {
        auto silent = desc;
        silent.volume = 0.0F;
        return backend.PlayOneShotForTesting(scene, silent);
    };
    auto voice = play();
    measure("initial", 0.0625);
    mixer.buses[0].volume = 0.25F;
    save();
    const auto silent = trigger();
    Require(silent.Succeeded() && backend.IsVoicePlaying(scene, voice), "Gain-only edit stopped an active voice");
    Require(backend.StopVoice(scene, silent.voiceId), "Gain-only edit trigger did not stop");
    measure("gain-edit", 0.03125);
    mixer.buses[1].parentBus.clear();
    save();
    const auto reparented = trigger();
    Require(reparented.Succeeded() && !backend.IsVoicePlaying(scene, voice), "Topology replacement must stop existing one-shot voices");
    Require(backend.StopVoice(scene, reparented.voiceId), "Reparent trigger did not stop");
    voice = play();
    measure("reparented", 0.125);
    mixer.buses.erase(mixer.buses.begin());
    save();
    const auto removedParent = trigger();
    Require(removedParent.Succeeded() && !backend.IsVoicePlaying(scene, voice), "Removing an unused parent retained an old voice");
    Require(backend.StopVoice(scene, removedParent.voiceId), "Parent removal trigger did not stop");
    voice = play();
    measure("parent-removed", 0.125);
    mixer.buses.clear();
    save();
    const auto missing = trigger();
    Require(!missing.Succeeded() && missing.error.find("unknown") != std::string::npos
        && !backend.IsVoicePlaying(scene, voice) && backend.ResourcesForTesting().voices == 0U,
        "Removing an active bus must reject its route and stop its old voice");
    // Keep a real, explicitly silent master source connected while reading output after
    // refusal. An empty offline engine can finish without producing any PCM frames.
    const auto silenceControl = [&] {
        auto quiet = desc;
        quiet.volume = 0.0F;
        quiet.outputBus.clear();
        const auto result = backend.PlayOneShotForTesting(scene, quiet);
        Require(result.Succeeded(), "Silent master control did not start");
        return result.voiceId;
    };
    const auto removedControl = silenceControl();
    measure("bus-removed", 0.0);
    Require(backend.StopVoice(scene, removedControl), "Removed-bus silence control did not stop");
    mixer.buses = {{.name = "Output", .volume = 0.5F}, {.name = "Effects", .parentBus = "Output", .volume = 0.5F}};
    save();
    voice = play();
    measure("restored", 0.0625);
    const auto before = ReadFileBytes(mixerPath);
    auto cyclic = mixer;
    cyclic.buses[0].parentBus = "Effects";
    Require(!kb::audio::AudioMixerAssetIO::Save(mixerPath, cyclic) && ReadFileBytes(mixerPath) == before
        && backend.IsVoicePlaying(scene, voice), "Rejected cyclic edit changed disk or the playing voice");
    measure("cycle-refused", 0.0625);
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, 0U);
    const auto unavailable = trigger();
    Require(!unavailable.Succeeded() && unavailable.error.find("not available") != std::string::npos
        && !backend.IsVoicePlaying(scene, voice), "Clearing the mixer must not redirect a named route to master");
    const auto clearedControl = silenceControl();
    measure("mixer-cleared", 0.0);
    Require(backend.StopVoice(scene, clearedControl), "Cleared-mixer silence control did not stop");
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, mixerId.value);
    static_cast<void>(play());
    measure("mixer-restored", 0.0625);
    backend.Shutdown();
    const auto resources = backend.ResourcesForTesting();
    Require(resources.sourceSounds == 0U && resources.voices == 0U && resources.buses == 0U
        && resources.decoders == 0U && resources.encodedPayloads == 0U
        && backend.PumpFramesForTesting(2048U).frames == 0U, "Bus PCM shutdown retained native resources");
}

void RunSnapshotTransitionPcmTest() {
    const auto tonePath = TestRoot() / "SnapshotTone.wav";
    WriteToneWav(tonePath);
    kb::scene::Scene scene;
    kb::audio::AudioMixerAsset mixer;
    mixer.buses = {{.name = "Output", .volume = 0.5F}, {.name = "Effects", .parentBus = "Output", .volume = 0.5F}};
    mixer.snapshots = {
        {.name = "Quiet", .busVolumes = {{.bus = "Effects", .volume = 0.125F}}},
        {.name = "Loud", .busVolumes = {{.bus = "Output", .volume = 0.75F}, {.bus = "Effects", .volume = 1.0F}}},
    };
    const auto mixerPath = TestRoot() / "SnapshotMixer.kbmixer";
    Require(kb::audio::AudioMixerAssetIO::Save(mixerPath, mixer), "Snapshot PCM mixer could not be saved");
    Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
        .id = kb::assets::AssetId{9301U}, .type = kb::audio::kAudioMixerAssetType, .name = "SnapshotMixer",
        .virtualPath = "/Game/Audio/SnapshotMixer.kbmixer", .physicalPath = mixerPath, .contentHash = 1U,
    }), "Snapshot PCM mixer could not be registered");
    using Access = kb::scene::SceneAudioMixerAccess;
    Access::SetActiveMixer(scene, 9301U);
    Require(Access::SetActiveSnapshot(scene, "Quiet"), "Snapshot PCM selection failed");
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    kb::audio_miniaudio::MiniaudioBusRegistry buses{engine};
    Require(buses.Sync(scene, true), "Snapshot PCM graph could not initialize");
    const auto route = buses.Resolve("Effects");
    Require(route.Succeeded() && route.group != nullptr, "Snapshot PCM route is unavailable");
    auto encoded = std::make_shared<kb::audio::AudioClipAsset>();
    encoded->encodedBytes = ReadFileBytesU8(tonePath);
    encoded->sourceExtension = ".wav";
    const kb::audio_miniaudio::ResolvedAudioClip clip{
        .audioClip = encoded, .extension = ".wav", .identity = "SnapshotPcmTone",
    };
    kb::audio_miniaudio::MiniaudioSound sound;
    Require(sound.Initialize(engine.Native(), clip, false, route.group) == MA_SUCCESS,
        "Snapshot PCM sound could not initialize");
    Require(sound.OwnsEncodedPayloadForTesting(), "Snapshot PCM fixture did not use its synchronous memory decoder");
    sound.Apply(kb::audio_miniaudio::MiniaudioSoundSettings{.volume = 0.25F, .loop = true, .spatial = false});
    Require(sound.Start() == MA_SUCCESS, "Snapshot PCM sound could not start");
    const auto measure = [&](std::string_view label, double expectedGain) {
        Require(!buses.Sync(scene, true), "A snapshot edit unnecessarily rebuilt the routing graph");
        const double expected = (3276.0 / 32768.0) * expectedGain / std::sqrt(2.0);
        std::size_t consecutive = 0U;
        double rms = 0.0;
        for (std::size_t attempt = 0U; attempt < 8U && consecutive < 4U; ++attempt) {
            std::array<float, 2048U * 2U> output{};
            ma_uint64 frames = 0U;
            Require(ma_engine_read_pcm_frames(&engine.Native(), output.data(), 2048U, &frames) == MA_SUCCESS
                && frames == 2048U, "Snapshot PCM read is incomplete");
            double energy = 0.0;
            for (std::size_t frame = 0U; frame < 2048U; ++frame) {
                const float sample = output[frame * 2U];
                Require(std::isfinite(sample), "Snapshot transition produced non-finite PCM");
                energy += static_cast<double>(sample) * sample;
            }
            rms = std::sqrt(energy / 2048.0);
            consecutive = std::abs(rms / expected - 1.0) < 0.02 ? consecutive + 1U : 0U;
        }
        std::cout << "Snapshot PCM " << label << " gain=" << expectedGain << " rms=" << rms
            << " expected=" << expected << " blocks=" << consecutive << '\n';
        Require(consecutive == 4U, std::string{label} + " did not produce four PCM blocks at the expected gain");
        Require(sound.IsPlaying(), "Snapshot transition lost playing sound intent");
    };
    measure("quiet", 0.5 * 0.125 * 0.25);
    Require(Access::BeginSnapshotTransition(scene, "Loud", 1.0F), "Snapshot fade could not begin");
    Require(!Access::AdvanceSnapshotTransition(scene, 0.25F), "Snapshot fade completed too soon");
    measure("fade-quarter", 0.5625 * 0.34375 * 0.25);
    Require(Access::BeginSnapshotTransition(scene, "Quiet", 1.0F), "Snapshot interruption could not begin");
    measure("interruption-continuity", 0.5625 * 0.34375 * 0.25);
    Require(!Access::AdvanceSnapshotTransition(scene, 0.5F), "Interrupted fade completed too soon");
    measure("interruption-half", 0.53125 * 0.234375 * 0.25);
    Require(Access::BeginSnapshotTransition(scene, "", 1.0F), "Overlapping return fade could not begin");
    measure("overlap-continuity", 0.53125 * 0.234375 * 0.25);
    Require(!Access::AdvanceSnapshotTransition(scene, 0.5F), "Return fade completed too soon");
    measure("return-half", 0.515625 * 0.3671875 * 0.25);
    Require(Access::AdvanceSnapshotTransition(scene, 0.5F) && Access::ActiveSnapshot(scene).empty(),
        "Snapshot return did not finish at the authored mix");
    measure("authored-base", 0.5 * 0.5 * 0.25);
    Require(Access::SetBusVolumeOverride(scene, "Effects", 0.75F)
        && Access::BeginSnapshotTransition(scene, "Quiet", 1.0F)
        && !Access::AdvanceSnapshotTransition(scene, 0.5F), "Overridden fade could not begin");
    measure("override-precedence", 0.5 * 0.75 * 0.25);
    Require(Access::ClearBusVolumeOverride(scene, "Effects"), "Snapshot override could not be cleared");
    measure("underlying-fade-after-clear", 0.5 * 0.3125 * 0.25);
    Require(!Access::BeginSnapshotTransition(scene, "Loud", std::numeric_limits<float>::quiet_NaN()),
        "Non-finite duration changed a running fade");
    measure("invalid-duration-preserves-mix", 0.5 * 0.3125 * 0.25);
    Require(Access::AdvanceSnapshotTransition(scene, 0.5F), "Overridden fade did not finish");
    measure("quiet-complete", 0.5 * 0.125 * 0.25);
    Require(Access::BeginSnapshotTransition(scene, "Loud", 0.0F)
        && !Access::SnapshotTransition(scene).IsActive(), "Zero-duration transition did not apply immediately");
    measure("immediate-loud", 0.75 * 1.0 * 0.25);
    Require(Access::SetActiveSnapshot(scene, "") && Access::SetBusVolumeOverride(scene, "Effects", 0.75F)
        && Access::BeginSnapshotTransition(scene, "Quiet", 1.0F)
        && !Access::AdvanceSnapshotTransition(scene, 0.5F)
        && Access::BeginSnapshotTransition(scene, "Loud", 1.0F), "Overridden retarget could not begin");
    measure("override-through-retarget", 0.5 * 0.75 * 0.25);
    Require(Access::ClearBusVolumeOverride(scene, "Effects"), "Retarget override could not be cleared");
    measure("captured-mix-excludes-override", 0.5 * 0.3125 * 0.25);
    Require(!Access::AdvanceSnapshotTransition(scene, 0.5F), "Overridden retarget completed too soon");
    measure("captured-mix-half", 0.625 * 0.65625 * 0.25);
}

void RunVorbisTransportTest() {
    const auto path = FixtureRoot() / "seek.ogg";
    const auto bytes = ReadFileBytesU8(path);
    auto memory = std::make_shared<kb::audio::AudioClipAsset>();
    memory->encodedBytes = bytes;
    memory->sourceExtension = ".ogg";
    for (const bool inMemory : {false, true}) {
        kb::audio_miniaudio::MiniaudioEngine engine;
        engine.Initialize(true);
        auto clip = inMemory ? kb::audio_miniaudio::ResolvedAudioClip{
            .audioClip = memory, .extension = ".ogg", .identity = "VorbisSeekMemory",
        } : DirectTestClip(path);
        kb::audio_miniaudio::MiniaudioSound sound;
        Require(sound.Initialize(engine.Native(), clip, false) == MA_SUCCESS,
            "Vorbis transport fixture could not initialize");
        sound.Apply(kb::audio_miniaudio::MiniaudioSoundSettings{.volume = 1.0F, .spatial = false});
        sound.SetLooping(true);
        Require(sound.Start() == MA_SUCCESS, "Vorbis transport fixture could not start");
        struct Block final { double rms; double frequency; ma_uint64 cursor; };
        const auto render = [&]() {
            std::array<float, 2048U * 2U> output{};
            ma_uint64 frames = 0U;
            Require(ma_engine_read_pcm_frames(&engine.Native(), output.data(), output.size() / 2U, &frames) == MA_SUCCESS
                && frames == 2048U, "Vorbis transport PCM block is incomplete");
            double energy = 0.0;
            std::uint32_t crossings = 0U;
            for (std::size_t frame = 0U; frame < static_cast<std::size_t>(frames); ++frame) {
                const float sample = output[frame * 2U];
                Require(std::isfinite(sample), "Vorbis transport produced non-finite PCM");
                energy += static_cast<double>(sample) * sample;
                if (frame > 0U && output[(frame - 1U) * 2U] <= 0.0F && sample > 0.0F) ++crossings;
            }
            return Block{std::sqrt(energy / static_cast<double>(frames)),
                static_cast<double>(crossings) * 48000.0 / static_cast<double>(frames), sound.PlaybackFrame()};
        };
        const auto matches = [](const Block& block, double frequency) {
            return block.rms > 0.05 && block.rms < 0.09 && std::abs(block.frequency - frequency) < 30.0;
        };
        const auto settle = [&](ma_uint64 target, double frequency) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            std::uint32_t good = 0U;
            std::uint32_t nonSilent = 0U;
            Block block{};
            do {
                block = render();
                if (block.rms > 0.001) ++nonSilent;
                good = matches(block, frequency) && block.cursor >= target && block.cursor <= target + 12288U ? good + 1U : 0U;
                Require(nonSilent <= 6U && std::chrono::steady_clock::now() < deadline,
                    "Vorbis seek did not settle within six non-silent output blocks");
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            } while (good < 4U);
            return block;
        };
        static_cast<void>(settle(0U, 440.0));
        Require(sound.SeekSeconds(0.625F) == MA_SUCCESS, "Forward Vorbis seek was rejected");
        const Block forward = settle(30000U, 880.0);
        Require(sound.SeekSeconds(0.125F) == MA_SUCCESS, "Backward Vorbis seek was rejected");
        const Block backward = settle(6000U, 440.0);
        Block loop{};
        for (std::uint32_t block = 0U; block < 24U; ++block) {
            loop = render();
            if (block >= 20U) Require(matches(loop, 440.0), "Vorbis loop did not restore first-half PCM after end of stream");
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        Require(sound.IsPlaying(), "Vorbis playback stopped at a looping boundary");
        std::cout << "Vorbis transport " << (inMemory ? "memory" : "file")
            << " forward=" << forward.cursor << " backward=" << backward.cursor
            << " loopHz=" << loop.frequency << " loopRms=" << loop.rms << '\n';
    }
}

[[nodiscard]] bool SameResources(
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting& left,
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting& right) noexcept {
    return left.sourceSounds == right.sourceSounds && left.voices == right.voices
        && left.buses == right.buses && left.decoders == right.decoders
        && left.encodedPayloads == right.encodedPayloads;
}

[[nodiscard]] std::string DescribeResources(
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting& resources) {
    return "sources=" + std::to_string(resources.sourceSounds)
        + ", voices=" + std::to_string(resources.voices)
        + ", buses=" + std::to_string(resources.buses)
        + ", decoders=" + std::to_string(resources.decoders)
        + ", payloads=" + std::to_string(resources.encodedPayloads);
}

void RequireVoiceDecodeEvidence(
    kb::audio_miniaudio::MiniaudioPlaybackBackend& backend,
    kb::scene::Scene& scene,
    std::uint64_t voiceId,
    std::string_view label) {
    std::uint64_t frames = 0U;
    double energy = 0.0;
    float previousCursor = backend.VoicePlaybackSecondsForTesting(voiceId);
    Require(previousCursor >= 0.0F, std::string{ label } + " voice does not exist before offline pump");
    bool cursorChanged = false;
    for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
        const auto pump = backend.PumpFramesForTesting(256U);
        frames += pump.frames;
        energy += pump.energy;
        const float cursor = backend.VoicePlaybackSecondsForTesting(voiceId);
        Require(cursor >= 0.0F, std::string{ label } + " voice disappeared during offline pump");
        Require(backend.IsVoicePlaying(scene, voiceId),
            std::string{ label } + " voice stopped during offline pump");
        cursorChanged = cursorChanged || cursor != previousCursor;
        previousCursor = cursor;
        if (frames > 0U && energy > 1.0 && cursorChanged) {
            break;
        }
    }
    Require(frames > 0U, std::string{ label } + " voice produced no offline PCM frames");
    Require(energy > 1.0, std::string{ label } + " voice produced no nonzero offline PCM energy");
    Require(cursorChanged, std::string{ label } + " voice playback cursor never changed");
}

void RunFormatRejectionTest() {
    kb::scene::Scene scene;
    kb::audio_miniaudio::MiniaudioPlaybackBackend backend;
    Require(backend.ReinitializeForTesting(scene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Format rejection backend did not initialize offline");

    constexpr std::uint64_t validBase = 8700U;
    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    for (std::size_t index = 0U; index < kAudioFixtures.size(); ++index) {
        RegisterClip(scene, validBase + index, FixturePath(kAudioFixtures[index]));
        Require(resolver.Resolve(scene, validBase + index).Succeeded(),
            "Advertised direct audio asset failed bounded resolver validation: " + std::string{kAudioFixtures[index].extension});
        const kb::audio::AudioPlayResult result = backend.PlayOneShotForTesting(scene, kb::audio::AudioPlayDesc{
            .clipAssetId = validBase + index,
            .volume = 0.0F,
            .loop = true,
            .spatial = false,
        });
        Require(result.Succeeded(), "Advertised direct audio asset did not play through the production backend");
        Require(backend.StopVoice(scene, result.voiceId),
            "Advertised direct audio validation voice could not be released");
    }

    const kb::scene::SceneObject retainedSource = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "Retained Format Source" });
    scene.Components().AudioSources().Set(retainedSource.Entity(), kb::scene::AudioSourceComponent{
        .clipAssetId = validBase,
        .volume = 0.0F,
        .loop = true,
        .spatial = false,
    });
    Require(backend.PlaySourceForTesting(scene, retainedSource.Entity()).Succeeded(),
        "Format rejection fixture did not create its retained source");
    const kb::audio::AudioPlayResult retainedVoice = backend.PlayOneShotForTesting(scene, kb::audio::AudioPlayDesc{
        .clipAssetId = validBase + 1U,
        .volume = 0.0F,
        .loop = true,
        .spatial = false,
    });
    Require(retainedVoice.Succeeded(), "Format rejection fixture did not create its retained voice");

    std::uint64_t corruptId = 8750U;
    for (const AudioFixture& fixture : kAudioFixtures) {
        const std::vector<char> validBytes = ReadFileBytes(FixturePath(fixture));
        const std::filesystem::path invalidMagic = TestRoot() /
            ("InvalidMagic" + std::string{ fixture.extension });
        const std::array<char, 32U> invalidBytes{};
        WriteBytes(invalidMagic, invalidBytes);

        const std::filesystem::path truncated = TestRoot() /
            ("Truncated" + std::string{ fixture.extension });
        WriteBytes(truncated, std::span<const char>{ validBytes.data(), validBytes.size() / 2U });

        std::vector<std::filesystem::path> rejectedPaths{invalidMagic, truncated};
        if (fixture.extension == ".ogg") {
            for (const auto& [name, offset] : std::array{
                    std::pair{"FirstPageCrc", std::size_t{22U}},
                    std::pair{"FinalPageCrc", validBytes.size() - 1U}}) {
                auto changed = validBytes;
                changed[offset] ^= 1;
                const auto path = TestRoot() / (std::string{name} + ".ogg");
                WriteBytes(path, changed);
                rejectedPaths.push_back(path);
            }
            const auto truncatedTail = TestRoot() / "TruncatedTail.ogg";
            WriteBytes(truncatedTail, std::span<const char>{validBytes}.first(validBytes.size() - 1U));
            rejectedPaths.push_back(truncatedTail);
            auto trailing = validBytes;
            trailing.push_back(0);
            const auto trailingPath = TestRoot() / "TrailingData.ogg";
            WriteBytes(trailingPath, trailing);
            rejectedPaths.push_back(trailingPath);
        }
        for (const std::filesystem::path& rejectedPath : rejectedPaths) {
            const std::uint64_t id = corruptId++;
            RegisterClip(scene, id, rejectedPath);
            const auto before = backend.ResourcesForTesting();
            Require(!resolver.Resolve(scene, id).Succeeded(),
                "Corrupt advertised audio payload passed resolver decode preflight");
            Require(!backend.PlayOneShotForTesting(scene, kb::audio::AudioPlayDesc{
                        .clipAssetId = id,
                        .volume = 0.0F,
                        .loop = true,
                        .spatial = false,
                    }).Succeeded(),
                "Corrupt advertised audio payload created a production one-shot");
            kb::scene::AudioSourceComponent* component =
                scene.Components().AudioSources().TryGet(retainedSource.Entity());
            Require(component != nullptr, "Retained format source component disappeared");
            component->clipAssetId = id;
            Require(!backend.PlaySourceForTesting(scene, retainedSource.Entity()).Succeeded(),
                "Corrupt advertised audio payload replaced a production source");
            Require(SameResources(before, backend.ResourcesForTesting()),
                "Rejected advertised audio payload mutated live backend resources");
        }
    }

    const std::filesystem::path mismatched = TestRoot() / "Mismatched.mp3";
    std::filesystem::copy_file(FixturePath(FixtureFor(".wav")), mismatched,
        std::filesystem::copy_options::overwrite_existing);
    const std::uint64_t mismatchedId = corruptId++;
    RegisterClip(scene, mismatchedId, mismatched);
    const auto beforeMismatch = backend.ResourcesForTesting();
    Require(!resolver.Resolve(scene, mismatchedId).Succeeded()
            && !backend.PlayOneShotForTesting(scene, kb::audio::AudioPlayDesc{
                    .clipAssetId = mismatchedId,
                    .volume = 0.0F,
                    .loop = true,
                    .spatial = false,
                }).Succeeded()
            && SameResources(beforeMismatch, backend.ResourcesForTesting()),
        "Extension-mismatched audio payload was accepted or mutated live resources");

    const std::vector<char> compressedBytes = ReadFileBytes(FixturePath(FixtureFor(".mp3")));
    std::vector<char> isolatedSync(1024U, '\0');
    std::copy_n(compressedBytes.begin(), 4U, isolatedSync.begin() + 137U);
    const std::filesystem::path isolatedSyncPath = TestRoot() / "IsolatedSync.mp3";
    WriteBytes(isolatedSyncPath, isolatedSync);
    const std::uint64_t isolatedSyncId = corruptId++;
    RegisterClip(scene, isolatedSyncId, isolatedSyncPath);
    const auto beforeIsolatedSync = backend.ResourcesForTesting();
    Require(!resolver.Resolve(scene, isolatedSyncId).Succeeded()
            && !backend.PlayOneShotForTesting(scene, kb::audio::AudioPlayDesc{
                    .clipAssetId = isolatedSyncId,
                    .volume = 0.0F,
                    .loop = true,
                    .spatial = false,
                }).Succeeded()
            && SameResources(beforeIsolatedSync, backend.ResourcesForTesting()),
        "An isolated compressed-frame sync was accepted as an audio stream");

    std::vector<char> withDeclaredTag(10U, '\0');
    withDeclaredTag[0] = 'I';
    withDeclaredTag[1] = 'D';
    withDeclaredTag[2] = '3';
    withDeclaredTag[3] = 4;
    withDeclaredTag.insert(withDeclaredTag.end(), compressedBytes.begin(), compressedBytes.end());
    const std::filesystem::path declaredTagPath = TestRoot() / "DeclaredTag.mp3";
    WriteBytes(declaredTagPath, withDeclaredTag);
    const std::uint64_t declaredTagId = corruptId++;
    RegisterClip(scene, declaredTagId, declaredTagPath);
    Require(resolver.Resolve(scene, declaredTagId).Succeeded(),
        "A legal declared compressed-audio prefix was rejected");

    std::vector<char> withTrailingTag = compressedBytes;
    const std::size_t tagOffset = withTrailingTag.size();
    withTrailingTag.resize(tagOffset + 128U, '\0');
    withTrailingTag[tagOffset] = 'T';
    withTrailingTag[tagOffset + 1U] = 'A';
    withTrailingTag[tagOffset + 2U] = 'G';
    const std::filesystem::path trailingTagPath = TestRoot() / "TrailingTag.mp3";
    WriteBytes(trailingTagPath, withTrailingTag);
    const std::uint64_t trailingTagId = corruptId++;
    RegisterClip(scene, trailingTagId, trailingTagPath);
    Require(resolver.Resolve(scene, trailingTagId).Succeeded(),
        "Legal trailing compressed-audio metadata was rejected");

    std::vector<char> withDisconnectedTail = compressedBytes;
    const std::size_t disconnectedTailOffset = withDisconnectedTail.size();
    const std::size_t fixtureFrameBytes = FixtureFor(".mp3").firstFrameBytes;
    Require(fixtureFrameBytes > 4U,
        "Compressed fixture did not declare its first frame length");
    withDisconnectedTail.resize(disconnectedTailOffset + fixtureFrameBytes + 32U, '\0');
    std::copy_n(compressedBytes.begin(), 4U,
        withDisconnectedTail.begin() + static_cast<std::ptrdiff_t>(disconnectedTailOffset + 7U));
    std::copy_n(compressedBytes.begin(), 4U,
        withDisconnectedTail.begin()
            + static_cast<std::ptrdiff_t>(disconnectedTailOffset + 7U + fixtureFrameBytes));
    const std::filesystem::path disconnectedTailPath = TestRoot() / "DisconnectedTail.mp3";
    WriteBytes(disconnectedTailPath, withDisconnectedTail);
    const std::uint64_t disconnectedTailId = corruptId++;
    RegisterClip(scene, disconnectedTailId, disconnectedTailPath);
    Require(resolver.Resolve(scene, disconnectedTailId).Succeeded(),
        "Disconnected exact-linked tail headers overrode forced native format validation");

    const std::filesystem::path revised = TestRoot() / "Revised.wav";
    std::filesystem::copy_file(FixturePath(FixtureFor(".wav")), revised,
        std::filesystem::copy_options::overwrite_existing);
    constexpr std::uint64_t revisedId = 8799U;
    RegisterClip(scene, revisedId, revised);
    Require(resolver.Resolve(scene, revisedId).Succeeded(),
        "Valid content revision fixture did not pass initial preflight");
    const std::array<char, 32U> revisedInvalid{};
    WriteBytes(revised, revisedInvalid);
    kb::assets::AssetMetadata revisedMetadata =
        *scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ revisedId });
    ++revisedMetadata.contentHash;
    Require(scene.Assets().Manager().Registry().Upsert(std::move(revisedMetadata))
            && !resolver.Resolve(scene, revisedId).Succeeded(),
        "Changed authoritative asset revision reused a stale decode validation");
    const auto formatValidationStats = resolver.StatsForTesting();
    Require(formatValidationStats.maxDecodedProbeFrames <= 256U
            && formatValidationStats.maxTailBytesInspected <= 64U * 1024U,
        "Compressed format validation exceeded its fixed probe or tail bounds");
}

[[nodiscard]] std::vector<kb::assets::AssetId> ImportFixtures(
    kb::scene::Scene& scene,
    const std::filesystem::path& projectRoot) {
    Require(scene.Assets().MountProject(projectRoot), "Imported audio memory project did not mount");
    std::vector<kb::assets::AssetId> ids;
    ids.reserve(kAudioFixtures.size());
    for (const AudioFixture& fixture : kAudioFixtures) {
        const std::array<std::filesystem::path, 1U> source{ FixturePath(fixture) };
        const kb::assets::AssetImportResult result = kb::assets::AssetImportService::ImportFiles(
            scene.Assets().Manager(), source, "/Game/Audio");
        Require(result.Succeeded() && result.items.size() == 1U,
            "Advertised audio fixture could not be imported");
        ids.push_back(result.items.front().id);
    }
    return ids;
}

struct PackagedAudioCase final {
    kb::assets::bake::RuntimeAssetManifestEntry manifestEntry;
    std::vector<std::uint8_t> sourceBytes;
    std::string encodedExtension;
    std::size_t encodedBytes = 0U;
    bool imported = false;
};

[[nodiscard]] kb::assets::bake::AssetBakeDigest StorePackagedAudioBytes(
    kb::assets::bake::AssetPackWriter& writer,
    const kb::assets::bake::BakeTargetProfile& profile,
    std::span<const std::uint8_t> bytes,
    std::string_view assetType,
    std::string_view bakerId,
    std::string_view settings) {
    kb::assets::bake::AssetBakeKey key{
        .sourceContentHash = kb::assets::bake::HashBakeBytes(bytes),
        .bakerId = std::string{ bakerId },
        .bakerVersion = "1",
        .targetProfileId = std::string{ profile.identifier },
        .targetProfileHash = kb::assets::bake::BakeTargetProfileFingerprint(profile),
        .settingsHash = kb::assets::bake::HashBakeText(settings),
    };
    const kb::assets::bake::BakedAssetDescriptor descriptor{
        .key = key,
        .assetTypeId = std::string{ assetType },
    };
    Require(writer.BeginAsset(descriptor) == kb::assets::bake::BakedAssetSinkStatus::Success
            && writer.WritePrimaryBlock(bytes, profile.packageBlockAlignmentBytes)
                == kb::assets::bake::BakedAssetSinkStatus::Success
            && writer.CommitAsset() == kb::assets::bake::BakedAssetSinkStatus::Success,
        "Packaged audio cook could not store an artifact");
    return key.Digest();
}

[[nodiscard]] kb::assets::bake::AssetBakeDigest StorePackagedAudioSource(
    kb::assets::bake::AssetPackWriter& writer,
    const kb::assets::bake::BakeTargetProfile& profile,
    std::span<const std::uint8_t> sourceBytes,
    std::string_view settings) {
    std::vector<std::uint8_t> sourceBlob;
    Require(kb::assets::bake::EncodeRuntimeSourceBlob(sourceBytes, sourceBlob),
        "Packaged audio cook could not encode source bytes");
    return StorePackagedAudioBytes(
        writer,
        profile,
        sourceBlob,
        kb::assets::bake::kSourceAssetTypeId,
        "AudioSource",
        settings);
}

void RunPackagedAudioMemoryTest() {
    namespace bake = kb::assets::bake;

    const std::filesystem::path authoringRoot = TestRoot() / "PackagedAudioAuthoring";
    const std::filesystem::path inputRoot = authoringRoot / "Input";
    const std::filesystem::path packPath = TestRoot() / "packaged-audio.kbpack";
    std::error_code error;
    std::filesystem::remove_all(authoringRoot, error);
    std::filesystem::remove(packPath, error);
    std::filesystem::create_directories(inputRoot, error);
    Require(!error, "Packaged audio authoring root could not be prepared");

    std::vector<PackagedAudioCase> cases;
    cases.reserve(kAudioFixtures.size() * 2U);
    for (const AudioFixture& fixture : kAudioFixtures) {
        const std::string stem = fixture.extension == ".wav"
            ? "Wave"
            : fixture.extension == ".flac" ? "Flac" : "Mp3";
        const std::filesystem::path sourcePath = inputRoot / ("Direct" + stem + std::string{ fixture.extension });
        std::filesystem::copy_file(
            FixturePath(fixture), sourcePath, std::filesystem::copy_options::overwrite_existing, error);
        Require(!error, "Packaged direct audio source could not be staged");
        std::vector<std::uint8_t> sourceBytes = ReadFileBytesU8(sourcePath);
        const std::string virtualPath = "/Game/Audio/Direct" + stem + std::string{ fixture.extension };
        cases.push_back(PackagedAudioCase{
            .manifestEntry = bake::RuntimeAssetManifestEntry{
                .id = kb::assets::MakeAssetId(virtualPath + ":AudioClip"),
                .type = "AudioClip",
                .name = "Direct" + stem,
                .virtualPath = virtualPath,
                .sourceExtension = std::string{ fixture.extension },
                .contentHash = bake::HashBakeBytes(sourceBytes),
            },
            .sourceBytes = std::move(sourceBytes),
            .encodedExtension = std::string{ fixture.extension },
            .encodedBytes = static_cast<std::size_t>(fixture.expectedBytes),
            .imported = false,
        });
    }

    {
        kb::scene::Scene importScene;
        const std::filesystem::path projectRoot = authoringRoot / "ImportedProject";
        Require(importScene.Assets().MountProject(projectRoot),
            "Packaged imported audio project could not be mounted");
        for (const AudioFixture& fixture : kAudioFixtures) {
            const std::string stem = fixture.extension == ".wav"
                ? "Wave"
                : fixture.extension == ".flac" ? "Flac" : "Mp3";
            const std::filesystem::path sourcePath = inputRoot / ("Imported" + stem + std::string{ fixture.extension });
            std::filesystem::copy_file(
                FixturePath(fixture), sourcePath, std::filesystem::copy_options::overwrite_existing, error);
            Require(!error, "Packaged imported audio source could not be staged");
            const std::array<std::filesystem::path, 1U> sources{ sourcePath };
            const kb::assets::AssetImportResult imported = kb::assets::AssetImportService::ImportFiles(
                importScene.Assets().Manager(), sources, "/Game/Audio");
            Require(imported.Succeeded() && imported.items.size() == 1U,
                "Packaged imported audio source could not enter the asset pipeline");
            const kb::assets::AssetImportItemResult& item = imported.items.front();
            std::vector<std::uint8_t> containerBytes = ReadFileBytesU8(item.assetPhysicalPath);
            const std::string virtualPath = item.virtualPath.generic_string();
            cases.push_back(PackagedAudioCase{
                .manifestEntry = bake::RuntimeAssetManifestEntry{
                    .id = item.id,
                    .type = "ImportedAsset",
                    .importCategory = "Audio",
                    .name = "Imported" + stem,
                    .virtualPath = virtualPath,
                    .sourceExtension = item.assetPhysicalPath.extension().generic_string(),
                    .contentHash = bake::HashBakeBytes(containerBytes),
                },
                .sourceBytes = std::move(containerBytes),
                .encodedExtension = std::string{ fixture.extension },
                .encodedBytes = static_cast<std::size_t>(fixture.expectedBytes),
                .imported = true,
            });
        }
    }

    const bake::BakeTargetProfile profile = bake::WindowsX64BakeTargetProfile();
    bake::AssetPackWriter writer{ packPath, profile };
    bake::RuntimeAssetManifest manifest{
        .targetProfileId = std::string{ profile.identifier },
        .targetProfileHash = bake::BakeTargetProfileFingerprint(profile),
    };
    manifest.descriptor.targetPlatforms = { "Windows" };
    manifest.settings.name = "PackagedAudioTest";
    const std::string scenePath = "/Game/Scenes/AudioRuntime.21kbscene";
    manifest.settings.defaultMap = scenePath;
    manifest.assets.reserve(cases.size() + 1U);
    const std::filesystem::path authoredScenePath = authoringRoot / "AudioRuntime.21kbscene";
    kb::scene::Scene authoredScene;
    Require(kb::scene::SceneDocumentService::Save(
                authoredScene, authoredScenePath, "AudioRuntime"),
        "Packaged audio fixture could not write its runtime Scene");
    const std::vector<std::uint8_t> sceneBytes = ReadFileBytesU8(authoredScenePath);
    const bake::AssetBakeDigest sceneDigest = StorePackagedAudioSource(
        writer, profile, sceneBytes, scenePath);
    manifest.assets.push_back(bake::RuntimeAssetManifestEntry{
        .id = kb::assets::MakeAssetId(scenePath + ":Scene"),
        .type = "Scene",
        .name = "AudioRuntime",
        .virtualPath = scenePath,
        .sourceExtension = ".21kbscene",
        .contentHash = bake::HashBakeBytes(sceneBytes),
        .artifacts = { bake::RuntimeArtifactReference{
            .digest = sceneDigest,
            .encoding = bake::RuntimeArtifactEncoding::SourceBytes,
        } },
    });
    for (PackagedAudioCase& testCase : cases) {
        const std::string settings = testCase.manifestEntry.type + ":"
            + testCase.manifestEntry.sourceExtension + ":" + testCase.manifestEntry.virtualPath;
        const bake::AssetBakeDigest digest = StorePackagedAudioSource(
            writer, profile, testCase.sourceBytes, settings);
        testCase.manifestEntry.artifacts.push_back(bake::RuntimeArtifactReference{
            .digest = digest,
            .encoding = bake::RuntimeArtifactEncoding::SourceBytes,
        });
        manifest.assets.push_back(testCase.manifestEntry);
    }
    std::vector<std::uint8_t> manifestBytes;
    Require(bake::EncodeRuntimeAssetManifest(manifest, manifestBytes)
            == bake::RuntimeAssetManifestStatus::Success,
        "Packaged audio runtime manifest could not be encoded");
    static_cast<void>(StorePackagedAudioBytes(
        writer,
        profile,
        manifestBytes,
        bake::kRuntimeManifestAssetTypeId,
        "RuntimeManifest",
        "packaged-audio-memory"));
    Require(writer.Finish() == bake::BakedAssetSinkStatus::Success,
        "Packaged audio cook could not publish its pack");

    std::filesystem::remove_all(authoringRoot, error);
    Require(!error && !std::filesystem::exists(authoringRoot),
        "Packaged audio authoring sources could not be removed before runtime mount");

    auto runtimePack = std::make_shared<bake::RuntimeAssetPack>();
    Require(runtimePack->Mount(packPath, profile) == bake::RuntimeAssetPackStatus::Success,
        "Packaged audio pack could not be mounted");
    kb::scene::Scene runtimeScene;
    kb::assets::AssetManager& manager = runtimeScene.Assets().Manager();
    Require(manager.MountRuntimePack(runtimePack)
            && manager.IsRuntimePackMounted()
            && manager.Mounts().Resolve("/Game/Audio").has_value() == false,
        "Packaged audio runtime did not enter pathless package mode");

    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    kb::audio_miniaudio::MiniaudioPlaybackBackend backend;
    Require(backend.ReinitializeForTesting(runtimeScene, true)
            == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Packaged audio offline backend could not initialize");
    for (const PackagedAudioCase& testCase : cases) {
        if (testCase.imported) {
            const kb::assets::AssetHandle<kb::assets::ImportedAsset> loaded =
                manager.Load<kb::assets::ImportedAsset>(testCase.manifestEntry.id);
            Require(loaded.IsLoaded() && loaded->payload.size() == testCase.encodedBytes,
                "Packaged imported audio did not load its encoded payload from the pack");
        } else {
            const kb::assets::AssetHandle<kb::audio::AudioClipAsset> loaded =
                manager.Load<kb::audio::AudioClipAsset>(testCase.manifestEntry.id);
            Require(loaded.IsLoaded() && loaded->IsMemoryBacked() && loaded->path.empty()
                    && loaded->encodedBytes.size() == testCase.encodedBytes,
                "Packaged direct audio did not load its encoded payload from the pack");
        }
        Require(manager.Unload(testCase.manifestEntry.id),
            "Packaged audio preload could not be released before resolver validation");
        {
            const kb::audio_miniaudio::MiniaudioClipResolver::Resolution resolved =
                resolver.Resolve(runtimeScene, testCase.manifestEntry.id.value);
            Require(resolved.Succeeded() && resolved.clip.IsMemoryBacked()
                    && resolved.clip.path.empty()
                    && resolved.clip.extension == testCase.encodedExtension
                    && resolved.clip.EncodedBytes().size() == testCase.encodedBytes,
                "Packaged audio resolver did not return the expected memory-backed clip");
        }
        const kb::audio::AudioPlayResult voice = backend.PlayOneShotForTesting(
            runtimeScene,
            kb::audio::AudioPlayDesc{
                .clipAssetId = testCase.manifestEntry.id.value,
                .volume = 1.0F,
                .loop = true,
                .spatial = false,
            });
        Require(voice.Succeeded()
                && backend.ResourcesForTesting().decoders == 1U
                && backend.ResourcesForTesting().encodedPayloads == 1U
                && !backend.VoiceUsesResourceManagerStreamForTesting(voice.voiceId),
            "Packaged audio playback did not create exactly one memory decoder and payload owner");
        Require(manager.Unload(testCase.manifestEntry.id),
            "Packaged audio cache could not release its payload while a decoder owned it");
        RequireVoiceDecodeEvidence(
            backend,
            runtimeScene,
            voice.voiceId,
            testCase.imported ? "Packaged imported audio" : "Packaged direct audio");
        Require(backend.StopVoice(runtimeScene, voice.voiceId)
                && SameResources(backend.ResourcesForTesting(), {}),
            "Packaged audio voice retained a decoder or encoded payload after stop");
    }
    const auto validation = resolver.StatsForTesting();
    Require(validation.attempts == cases.size()
            && validation.payloadHashAttempts == kAudioFixtures.size(),
        "Packaged audio validation did not distinguish pack-verified direct bytes from imported payload integrity");
    backend.Shutdown();
}

void RunImportedMemoryLifecycleTest() {
    kb::scene::Scene scene;
    const std::vector<kb::assets::AssetId> importedIds =
        ImportFixtures(scene, TestRoot() / "ImportedFormatsProject");

    kb::audio_miniaudio::MiniaudioClipResolver integrityResolver;
    Require(integrityResolver.Resolve(scene, importedIds.front().value).Succeeded(),
        "Imported payload failed its first integrity validation");
    const auto firstIntegrityStats = integrityResolver.StatsForTesting();
    Require(firstIntegrityStats.attempts == 1U
            && firstIntegrityStats.payloadHashAttempts == 1U
            && firstIntegrityStats.payloadBytesHashed > 0U
            && firstIntegrityStats.cacheEntries == 1U,
        "First imported resolve did not perform exactly one payload integrity pass");
    Require(integrityResolver.Resolve(scene, importedIds.front().value).Succeeded(),
        "Repeated imported resolve failed");
    const auto repeatedIntegrityStats = integrityResolver.StatsForTesting();
    Require(repeatedIntegrityStats.attempts == firstIntegrityStats.attempts
            && repeatedIntegrityStats.payloadHashAttempts == firstIntegrityStats.payloadHashAttempts
            && repeatedIntegrityStats.payloadBytesHashed == firstIntegrityStats.payloadBytesHashed,
        "Imported validation cache hit rehashed or reprobed the encoded payload");

    kb::audio_miniaudio::MiniaudioPlaybackBackend backend;
    Require(backend.ReinitializeForTesting(scene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Imported format backend did not initialize offline");

    for (std::size_t index = 0U; index < importedIds.size(); ++index) {
        const kb::assets::AssetId id = importedIds[index];
        const std::string evidence = "Imported " + std::string{ kAudioFixtures[index].extension };
        const bool spatial = index == 1U;
        const auto resourcesBefore = backend.ResourcesForTesting();
        const kb::audio::AudioPlayResult voice = backend.PlayOneShotForTesting(scene, kb::audio::AudioPlayDesc{
            .clipAssetId = id.value,
            .volume = 1.0F,
            .loop = true,
            .spatial = spatial,
        });
        Require(voice.Succeeded(), evidence + " voice did not start through the production backend");
        const auto resourcesStarted = backend.ResourcesForTesting();
        Require(resourcesStarted.sourceSounds == resourcesBefore.sourceSounds
                && resourcesStarted.voices == resourcesBefore.voices + 1U
                && resourcesStarted.buses == resourcesBefore.buses
                && resourcesStarted.decoders == resourcesBefore.decoders + (spatial ? 2U : 1U)
                && resourcesStarted.encodedPayloads == resourcesBefore.encodedPayloads + 1U
                && !backend.VoiceUsesResourceManagerStreamForTesting(voice.voiceId),
            evidence + " voice start produced an unexpected native resource state");

        RequireVoiceDecodeEvidence(backend, scene, voice.voiceId, evidence);
        Require(backend.StopVoice(scene, voice.voiceId),
            evidence + " validation voice could not be stopped");
        Require(SameResources(backend.ResourcesForTesting(), resourcesBefore),
            evidence + " validation voice retained a native resource after stop");

        const kb::audio::AudioPlayResult finiteVoice = backend.PlayOneShotForTesting(scene, kb::audio::AudioPlayDesc{
            .clipAssetId = id.value,
            .volume = 0.0F,
            .loop = false,
            .spatial = false,
        });
        Require(finiteVoice.Succeeded(), evidence + " finite voice did not start");
        for (std::uint32_t attempt = 0U;
             attempt < 512U && backend.IsVoicePlaying(scene, finiteVoice.voiceId);
             ++attempt) {
            static_cast<void>(backend.PumpFramesForTesting(512U));
        }
        Require(!backend.IsVoicePlaying(scene, finiteVoice.voiceId),
            evidence + " finite voice remained playing after its imported decoder reached the end");
        Require(backend.StopVoice(scene, finiteVoice.voiceId),
            evidence + " finite validation voice could not be released");
        Require(SameResources(backend.ResourcesForTesting(), resourcesBefore),
            evidence + " finite validation voice retained a native resource after stop");
    }

    const kb::scene::SceneObject source = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "Imported Memory Source" });
    scene.Components().AudioSources().Set(source.Entity(), kb::scene::AudioSourceComponent{
        .clipAssetId = importedIds.front().value,
        .volume = 0.0F,
        .loop = true,
        .spatial = false,
    });
    Require(backend.PlaySourceForTesting(scene, source.Entity()).Succeeded(),
        "Imported memory reinitialize source could not be created");
    const kb::audio::AudioPlayResult memoryVoice = backend.PlayOneShotForTesting(scene, kb::audio::AudioPlayDesc{
        .clipAssetId = importedIds.front().value,
        .volume = 0.0F,
        .loop = true,
        .spatial = false,
    });
    const auto memoryPump = backend.PumpFramesForTesting(512U);
    Require(memoryVoice.Succeeded() && memoryPump.frames > 0U,
        "Imported memory reinitialize voice could not be pumped");
    Require(backend.ReinitializeForTesting(scene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice
            && SameResources(backend.ResourcesForTesting(), {}),
        "Backend reinitialize did not release imported sounds, decoders and payload owners");
    Require(backend.PlaySourceForTesting(scene, source.Entity()).Succeeded(),
        "Backend did not recreate imported component playback after reinitialize");
    const kb::audio::AudioPlayResult recreatedVoice = backend.PlayOneShotForTesting(scene, kb::audio::AudioPlayDesc{
        .clipAssetId = importedIds.front().value,
        .volume = 0.0F,
        .loop = true,
        .spatial = false,
    });
    const auto recreatedResources = backend.ResourcesForTesting();
    Require(recreatedVoice.Succeeded() && backend.PumpFramesForTesting(512U).frames > 0U
            && recreatedResources.sourceSounds == 1U && recreatedResources.voices == 1U
            && recreatedResources.decoders == 2U && recreatedResources.encodedPayloads == 2U,
        "Backend did not recreate exact imported decoder and payload ownership after reinitialize");
    backend.Shutdown();
    Require(SameResources(backend.ResourcesForTesting(), {}),
        "Backend shutdown retained an imported sound, decoder or payload owner");

    kb::assets::AssetMetadata revisedImportedMetadata =
        *scene.Assets().Manager().Registry().Find(importedIds.front());
    const std::filesystem::path revisedImportedPath = revisedImportedMetadata.physicalPath;
    std::vector<char> revisedImportedContainer = ReadFileBytes(revisedImportedPath);
    Require(!revisedImportedContainer.empty(),
        "Imported integrity revision container was empty");
    revisedImportedContainer.back() = static_cast<char>(revisedImportedContainer.back() ^ 0x5A);
    WriteBytes(revisedImportedPath, revisedImportedContainer);
    ++revisedImportedMetadata.contentHash;
    Require(scene.Assets().Manager().Registry().Upsert(std::move(revisedImportedMetadata)),
        "Imported integrity revision metadata could not be updated");
    static_cast<void>(scene.Assets().Manager().Unload(importedIds.front()));
    Require(!integrityResolver.Resolve(scene, importedIds.front().value).Succeeded(),
        "Changed imported revision reused stale payload integrity validation");
    const auto revisedIntegrityStats = integrityResolver.StatsForTesting();
    Require(revisedIntegrityStats.attempts == 2U
            && revisedIntegrityStats.payloadHashAttempts == 2U
            && revisedIntegrityStats.cacheEntries == 1U,
        "Changed imported revision did not replace its per-asset validation entry");
    revisedImportedContainer.back() = static_cast<char>(revisedImportedContainer.back() ^ 0x5A);
    WriteBytes(revisedImportedPath, revisedImportedContainer);
    kb::assets::AssetMetadata repairedImportedMetadata =
        *scene.Assets().Manager().Registry().Find(importedIds.front());
    ++repairedImportedMetadata.contentHash;
    Require(scene.Assets().Manager().Registry().Upsert(std::move(repairedImportedMetadata)),
        "Repaired imported integrity revision metadata could not be updated");
    static_cast<void>(scene.Assets().Manager().Unload(importedIds.front()));
    Require(integrityResolver.Resolve(scene, importedIds.front().value).Succeeded(),
        "Repaired imported asset did not validate after an invalid revision");
    const auto repairedIntegrityStats = integrityResolver.StatsForTesting();
    Require(repairedIntegrityStats.attempts == 3U
            && repairedIntegrityStats.payloadHashAttempts == 3U
            && repairedIntegrityStats.cacheEntries == 1U,
        "Invalid-to-repaired imported revision retained or grew stale validation state");
    const std::array<char, 1U> invalidContainer{ '\0' };
    WriteBytes(revisedImportedPath, invalidContainer);
    static_cast<void>(scene.Assets().Manager().Unload(importedIds.front()));
    Require(!integrityResolver.Resolve(scene, importedIds.front().value).Succeeded()
            && integrityResolver.StatsForTesting().cacheEntries == 0U,
        "Invalid imported container retained a stale per-asset validation entry");
    WriteBytes(revisedImportedPath, revisedImportedContainer);
    static_cast<void>(scene.Assets().Manager().Unload(importedIds.front()));
    Require(integrityResolver.Resolve(scene, importedIds.front().value).Succeeded(),
        "Imported container repair with stable metadata identity reused stale invalid state");
    const auto stableRepairStats = integrityResolver.StatsForTesting();
    Require(stableRepairStats.attempts == 4U
            && stableRepairStats.payloadHashAttempts == 4U
            && stableRepairStats.cacheEntries == 1U,
        "Stable-identity imported repair was not fully revalidated");

    kb::scene::Scene firstScene;
    kb::scene::Scene secondScene;
    const std::vector<kb::assets::AssetId> firstIds =
        ImportFixtures(firstScene, TestRoot() / "ConcurrentImportedProjectA");
    const std::vector<kb::assets::AssetId> secondIds =
        ImportFixtures(secondScene, TestRoot() / "ConcurrentImportedProjectB");
    kb::audio_miniaudio::MiniaudioPlaybackBackend firstBackend;
    kb::audio_miniaudio::MiniaudioPlaybackBackend secondBackend;
    Require(firstBackend.ReinitializeForTesting(firstScene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice
            && secondBackend.ReinitializeForTesting(secondScene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Concurrent imported backends did not initialize offline");
    const kb::audio::AudioPlayResult firstVoice = firstBackend.PlayOneShotForTesting(firstScene, kb::audio::AudioPlayDesc{
        .clipAssetId = firstIds.front().value,
        .volume = 1.0F,
        .loop = true,
        .spatial = false,
    });
    const kb::audio::AudioPlayResult secondVoice = secondBackend.PlayOneShotForTesting(secondScene, kb::audio::AudioPlayDesc{
        .clipAssetId = secondIds.front().value,
        .volume = 1.0F,
        .loop = true,
        .spatial = false,
    });
    const auto firstResources = firstBackend.ResourcesForTesting();
    const auto secondResources = secondBackend.ResourcesForTesting();
    Require(firstVoice.Succeeded() && secondVoice.Succeeded()
            && firstResources.voices == 1U && firstResources.decoders == 1U
            && firstResources.encodedPayloads == 1U
            && secondResources.voices == 1U && secondResources.decoders == 1U
            && secondResources.encodedPayloads == 1U,
        "Concurrent production backends did not own independent imported decoders");

    const kb::assets::AssetMetadata* secondMetadata =
        secondScene.Assets().Manager().Registry().Find(secondIds.front());
    Require(secondMetadata != nullptr, "Concurrent imported metadata disappeared");
    const std::filesystem::path secondContainer = secondMetadata->physicalPath;
    Require(secondScene.Assets().Manager().DeleteAsset(secondIds.front()),
        "Live imported asset container could not be deleted");
    Require(secondScene.Assets().Manager().Registry().Find(secondIds.front()) == nullptr
            && !std::filesystem::exists(secondContainer),
        "Deleted live imported asset retained metadata or its container");
    Require(secondBackend.PumpFramesForTesting(2048U).energy > 1.0,
        "Deleting a live imported asset interrupted its payload-owned decoder");
    firstBackend.Shutdown();
    Require(SameResources(firstBackend.ResourcesForTesting(), {})
            && secondBackend.ResourcesForTesting().voices == 1U
            && secondBackend.ResourcesForTesting().decoders == 1U
            && secondBackend.ResourcesForTesting().encodedPayloads == 1U,
        "First backend shutdown released another backend's decoder or payload owner");
    RequireVoiceDecodeEvidence(secondBackend, secondScene, secondVoice.voiceId,
        "Second isolated imported backend");
    secondBackend.Shutdown();
    Require(SameResources(secondBackend.ResourcesForTesting(), {}),
        "Second backend retained its decoder or encoded payload owner after shutdown");

    kb::scene::Scene corruptScene;
    Require(corruptScene.Assets().MountProject(TestRoot() / "CorruptImportedProject"),
        "Corrupt imported audio project did not mount");
    kb::audio_miniaudio::MiniaudioClipResolver corruptResolver;
    kb::audio_miniaudio::MiniaudioPlaybackBackend corruptBackend;
    Require(corruptBackend.ReinitializeForTesting(corruptScene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Corrupt imported backend did not initialize offline");
    const kb::scene::SceneObject corruptSourceEntity = corruptScene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "Corrupt Imported Source" });
    const std::array<std::filesystem::path, 1U> retainedSourcePath{
        FixturePath(FixtureFor(".wav")),
    };
    const kb::assets::AssetImportResult retainedImport = kb::assets::AssetImportService::ImportFiles(
        corruptScene.Assets().Manager(), retainedSourcePath, "/Game/Audio");
    Require(retainedImport.Succeeded() && retainedImport.items.size() == 1U,
        "Retained imported source fixture could not be imported");
    const std::uint64_t retainedClipId = retainedImport.items.front().id.value;
    corruptScene.Components().AudioSources().Set(corruptSourceEntity.Entity(), kb::scene::AudioSourceComponent{
        .clipAssetId = retainedClipId,
        .volume = 0.0F,
        .loop = true,
        .spatial = false,
    });
    Require(corruptBackend.PlaySourceForTesting(corruptScene, corruptSourceEntity.Entity()).Succeeded(),
        "Retained imported source could not be created");
    const kb::audio::AudioPlayResult retainedImportedVoice =
        corruptBackend.PlayOneShotForTesting(corruptScene, kb::audio::AudioPlayDesc{
            .clipAssetId = retainedClipId,
            .volume = 0.0F,
            .loop = true,
            .spatial = false,
        });
    Require(retainedImportedVoice.Succeeded(),
        "Retained imported one-shot could not be created");
    std::uint32_t corruptIndex = 0U;
    for (const AudioFixture& fixture : kAudioFixtures) {
        const std::vector<char> validBytes = ReadFileBytes(FixturePath(fixture));
        const std::array<char, 32U> invalid{};
        const std::array<std::span<const char>, 2U> rejectedPayloads{
            std::span<const char>{ invalid },
            std::span<const char>{ validBytes.data(), validBytes.size() / 2U },
        };
        for (std::size_t payloadIndex = 0U; payloadIndex < rejectedPayloads.size(); ++payloadIndex) {
            const std::span<const char> rejectedPayload = rejectedPayloads[payloadIndex];
            const std::string rejectionLabel = std::string{ fixture.extension }
                + (payloadIndex == 0U ? " invalid-magic" : " truncated");
            const std::filesystem::path corruptSource = TestRoot() /
                ("ImportedCorrupt" + std::to_string(corruptIndex++) + std::string{ fixture.extension });
            WriteBytes(corruptSource, rejectedPayload);
            const std::array<std::filesystem::path, 1U> sourcePath{ corruptSource };
            const kb::assets::AssetImportResult imported = kb::assets::AssetImportService::ImportFiles(
                corruptScene.Assets().Manager(), sourcePath, "/Game/Audio");
            Require(imported.Succeeded() && imported.items.size() == 1U,
                "Corrupt advertised payload could not enter the imported container path");
            const std::uint64_t corruptId = imported.items.front().id.value;
            const auto resolution = corruptResolver.Resolve(corruptScene, corruptId);
            Require(!resolution.Succeeded(), rejectionLabel
                    + " resolver unexpectedly succeeded; status="
                    + std::to_string(static_cast<unsigned int>(resolution.status)));
            corruptScene.Components().AudioSources().Set(corruptSourceEntity.Entity(), kb::scene::AudioSourceComponent{
                .clipAssetId = corruptId,
                .volume = 0.0F,
                .loop = true,
                .spatial = false,
            });
            const auto before = corruptBackend.ResourcesForTesting();
            const kb::audio::AudioPlayResult oneShotResult =
                corruptBackend.PlayOneShotForTesting(corruptScene, kb::audio::AudioPlayDesc{
                    .clipAssetId = corruptId,
                    .volume = 0.0F,
                    .loop = true,
                    .spatial = false,
                });
            Require(!oneShotResult.Succeeded(), rejectionLabel
                    + " one-shot unexpectedly succeeded; voiceId="
                    + std::to_string(oneShotResult.voiceId) + ", error=" + oneShotResult.error);
            const kb::audio::AudioSourceControlResult sourceResult =
                corruptBackend.PlaySourceForTesting(corruptScene, corruptSourceEntity.Entity());
            Require(!sourceResult.Succeeded(), rejectionLabel
                    + " source replacement unexpectedly succeeded; status="
                    + std::to_string(static_cast<unsigned int>(sourceResult.status))
                    + ", playing=" + (sourceResult.playing ? "true" : "false"));
            const auto after = corruptBackend.ResourcesForTesting();
            Require(SameResources(before, after), rejectionLabel
                    + " changed resources; before {" + DescribeResources(before)
                    + "}, after {" + DescribeResources(after) + "}");
            const bool retainedVoicePlaying =
                corruptBackend.IsVoicePlaying(corruptScene, retainedImportedVoice.voiceId);
            Require(retainedVoicePlaying, rejectionLabel
                    + " stopped the retained voice; voiceId="
                    + std::to_string(retainedImportedVoice.voiceId)
                    + ", resources {" + DescribeResources(after) + "}");
            const kb::audio::AudioSourceControlResult retainedSourceState =
                corruptBackend.IsSourcePlayingForTesting(
                    corruptScene, corruptSourceEntity.Entity());
            Require(retainedSourceState.Succeeded() && retainedSourceState.playing,
                rejectionLabel + " changed retained source transport; status="
                    + std::to_string(static_cast<unsigned int>(retainedSourceState.status))
                    + ", playing=" + (retainedSourceState.playing ? "true" : "false")
                    + ", resources {" + DescribeResources(after) + "}");
        }
    }
    const std::filesystem::path mismatchedSource = TestRoot() / "ImportedMismatch.mp3";
    std::filesystem::copy_file(FixturePath(FixtureFor(".wav")), mismatchedSource,
        std::filesystem::copy_options::overwrite_existing);
    const std::array<std::filesystem::path, 1U> mismatchedSourcePath{ mismatchedSource };
    const kb::assets::AssetImportResult mismatchedImported = kb::assets::AssetImportService::ImportFiles(
        corruptScene.Assets().Manager(), mismatchedSourcePath, "/Game/Audio");
    Require(mismatchedImported.Succeeded() && mismatchedImported.items.size() == 1U
            && !corruptResolver.Resolve(corruptScene, mismatchedImported.items.front().id.value).Succeeded()
            && !corruptBackend.PlayOneShotForTesting(corruptScene, kb::audio::AudioPlayDesc{
                    .clipAssetId = mismatchedImported.items.front().id.value,
                    .volume = 0.0F,
                    .loop = true,
                    .spatial = false,
                }).Succeeded(),
        "Imported extension-mismatched payload passed production decode preflight");
    const std::array<std::filesystem::path, 1U> replacementSourcePath{
        FixturePath(FixtureFor(".flac")),
    };
    const kb::assets::AssetImportResult replacementImport = kb::assets::AssetImportService::ImportFiles(
        corruptScene.Assets().Manager(), replacementSourcePath, "/Game/Audio");
    Require(replacementImport.Succeeded() && replacementImport.items.size() == 1U,
        "Valid imported replacement fixture could not be imported");
    kb::scene::AudioSourceComponent* corruptComponent =
        corruptScene.Components().AudioSources().TryGet(corruptSourceEntity.Entity());
    Require(corruptComponent != nullptr, "Imported replacement source component disappeared");
    corruptComponent->clipAssetId = replacementImport.items.front().id.value;
    corruptScene.Components().AudioSources().MarkModified(corruptSourceEntity.Entity());
    const auto beforeValidReplacement = corruptBackend.ResourcesForTesting();
    Require(corruptBackend.PlaySourceForTesting(corruptScene, corruptSourceEntity.Entity()).Succeeded()
            && SameResources(beforeValidReplacement, corruptBackend.ResourcesForTesting())
            && corruptBackend.IsSourcePlayingForTesting(
                   corruptScene, corruptSourceEntity.Entity()).playing,
        "Valid imported candidate did not replace the retained corrupt-candidate source");
    corruptResolver.Reset();
    corruptBackend.Shutdown();
    Require(SameResources(corruptBackend.ResourcesForTesting(), {}),
        "Audio memory playback retained corrupt native ownership");
}

void RunSoundStateTest(const std::filesystem::path& clipPath) {
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    Require(engine.Status() == kb::audio::AudioDeviceStatus::NoPlaybackDevice, "Controlled no-device engine did not report its state");

    kb::audio_miniaudio::MiniaudioSound sound;
    Require(sound.Initialize(engine.Native(), DirectTestClip(clipPath), true) == MA_SUCCESS, "Spatial audio test sound could not be initialized");
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
    Require(flatSound.Initialize(engine.Native(), DirectTestClip(clipPath), false) == MA_SUCCESS && flatSound.FlatForTesting() == nullptr,
        "A purely flat sound allocated an unnecessary second branch");
}

void RunVoiceStateTest(const std::filesystem::path& clipPath) {
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    kb::scene::Scene scene;
    RegisterClip(scene, 8101U, clipPath);
    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    kb::audio_miniaudio::MiniaudioVoicePool pool;
    OfflineEnginePump pump{ engine };
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
    for (const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 10 };
         std::chrono::steady_clock::now() < deadline && !endedSound->AtEnd();) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 2 });
    }
    Require(endedSound->AtEnd(), "Finite one-shot did not reach its natural end");
    Require(!pool.IsVoicePlaying(ended.voiceId),
        "Naturally ended one-shot remained visible as playing");
    Require(!pool.PauseVoice(ended.voiceId) && !pool.ResumeVoice(ended.voiceId),
        "Naturally ended one-shot accepted pause or resume");

    const kb::audio::AudioPlayResult naturallyEnded = pool.PlayOneShot(engine.Native(), scene, kb::audio::AudioPlayDesc{
        .clipAssetId = 8101U,
        .volume = 0.0F,
        .loop = false,
        .spatial = false,
    }, resolver, nullptr);
    Require(naturallyEnded.Succeeded(), "Finite one-shot could not start for natural completion verification");
    for (const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 10 };
         std::chrono::steady_clock::now() < deadline && pool.IsVoicePlaying(naturallyEnded.voiceId);) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 2 });
    }
    Require(!pool.IsVoicePlaying(naturallyEnded.voiceId),
        "Finite one-shot remained visible as playing after natural playback completion");

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
    kb::scene::Scene scene;
    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    kb::audio_miniaudio::MiniaudioVoicePool pool;
    OfflineEnginePump pump{ engine };

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
    Require(!rejected.Succeeded() && rejected.error == "audio clip file could not be resolved",
        "Truncated one-shot payload was not rejected by decode preflight");
    Require(pool.VoiceIdsForTesting() == idsBefore && pool.VoiceCountForTesting() == idsBefore.size(),
        "Rejected one-shot initialization evicted or reordered a live voice");
    Require(pool.SoundForTesting(idsBefore.front()) == retainedSound
            && pool.IsVoicePlaying(idsBefore.front())
            && Near(ma_sound_get_volume(retainedSound->PrimaryForTesting()), volumeBefore)
            && Near(ma_sound_get_pan(retainedSound->PrimaryForTesting()), panBefore)
            && Near(ma_sound_get_pitch(retainedSound->PrimaryForTesting()), pitchBefore),
        "Rejected one-shot initialization changed the retained native voice state");

    constexpr std::uint64_t lowerPriorityClip = 8597U;
    RegisterClip(scene, lowerPriorityClip, clipPath);
    const kb::audio::AudioPlayResult lowerPriority = pool.PlayOneShot(
        engine.Native(), scene,
        kb::audio::AudioPlayDesc{
            .clipAssetId = lowerPriorityClip,
            .volume = 0.0F,
            .loop = true,
            .spatial = false,
            .priority = 99U,
        },
        resolver, nullptr);
    Require(!lowerPriority.Succeeded() && pool.VoiceIdsForTesting() == idsBefore,
        "Lower-priority capacity request changed the exact live voice order");

    constexpr std::uint64_t equalPriorityClip = 8599U;
    RegisterClip(scene, equalPriorityClip, clipPath);
    const kb::audio::AudioPlayResult equalPriority = pool.PlayOneShot(
        engine.Native(), scene,
        kb::audio::AudioPlayDesc{
            .clipAssetId = equalPriorityClip,
            .volume = 0.0F,
            .loop = true,
            .spatial = false,
            .priority = 100U,
        },
        resolver, nullptr);
    std::vector<std::uint64_t> expectedAfterEqual(idsBefore.begin() + 1, idsBefore.end());
    expectedAfterEqual.push_back(equalPriority.voiceId);
    Require(equalPriority.Succeeded() && pool.VoiceIdsForTesting() == expectedAfterEqual
            && !pool.IsVoicePlaying(idsBefore.front()),
        "Equal-priority capacity request did not evict the deterministic oldest voice");

    constexpr std::uint64_t highPriorityClip = 8596U;
    RegisterClip(scene, highPriorityClip, clipPath);
    const kb::audio::AudioPlayResult highPriority = pool.PlayOneShot(
        engine.Native(), scene,
        kb::audio::AudioPlayDesc{
            .clipAssetId = highPriorityClip,
            .volume = 0.0F,
            .loop = true,
            .spatial = false,
            .priority = 255U,
        },
        resolver, nullptr);
    std::vector<std::uint64_t> expectedAfterHigh(expectedAfterEqual.begin() + 1, expectedAfterEqual.end());
    expectedAfterHigh.push_back(highPriority.voiceId);
    Require(highPriority.Succeeded() && pool.VoiceIdsForTesting() == expectedAfterHigh
            && !pool.IsVoicePlaying(idsBefore[1U]),
        "High-priority capacity request did not evict the deterministic oldest voice");
    pool.StopAll();
    Require(pool.VoiceCountForTesting() == 0U,
        "StopAll did not release the full offline one-shot capacity");
}

void RunInitialFrameSoundTest(const std::filesystem::path& clipPath) {
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    kb::audio_miniaudio::MiniaudioSound sound;
    Require(sound.Initialize(engine.Native(), DirectTestClip(clipPath), true, nullptr, 11025U) == MA_SUCCESS,
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
    kb::scene::Scene scene;
    RegisterClip(scene, 8301U, clipPath);
    RegisterClip(scene, 8302U, clipPath);
    Require(scene.Assets().MountProject(TestRoot() / "SourceLifecycleProject"),
        "Source lifecycle deletion project did not mount");
    const std::array<std::filesystem::path, 1U> deletableSource{ clipPath };
    const kb::assets::AssetImportResult deletableImport = kb::assets::AssetImportService::ImportFiles(
        scene.Assets().Manager(), deletableSource, "/Game/Audio");
    Require(deletableImport.Succeeded() && deletableImport.items.size() == 1U,
        "Source lifecycle deletion fixture did not enter the production asset pipeline");
    const kb::assets::AssetId deletableClipId = deletableImport.items.front().id;
    const kb::assets::AssetMetadata* deletableMetadata =
        scene.Assets().Manager().Registry().Find(deletableClipId);
    Require(deletableMetadata != nullptr,
        "Source lifecycle deletion fixture has no authoritative metadata");
    const std::filesystem::path deletableContainer = deletableMetadata->physicalPath;
    Require(std::filesystem::is_regular_file(deletableContainer),
        "Source lifecycle deletion fixture has no owned asset container");
    const std::filesystem::path corruptPath = TestRoot() / "SourceTruncated.wav";
    WriteTruncatedAudio(corruptPath);
    RegisterClip(scene, 8303U, corruptPath);
    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    kb::audio_miniaudio::MiniaudioBusRegistry buses{engine};
    kb::audio_miniaudio::MiniaudioSourceRegistry sources;
    OfflineEnginePump pump{ engine };

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
            == kb::audio::AudioSourceControlStatus::ClipUnavailable,
        "Explicit source transport masked a corrupt changed-clip decode failure");
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
    component = scene.Components().AudioSources().TryGet(sourceObject.Entity());
    component->clipAssetId = 0U;
    scene.Components().AudioSources().MarkModified(sourceObject.Entity());
    kb::scene::SceneSystemContext emptyClipContext{ scene, 0.25F };
    sources.Sync(engine.Native(), emptyClipContext, resolver, buses, nullptr, {}, true);
    Require(sources.SoundCountForTesting() == 0U,
        "A component with an empty clip retained its previous native source");
    component->clipAssetId = deletableClipId.value;
    scene.Components().AudioSources().MarkModified(sourceObject.Entity());
    kb::scene::SceneSystemContext deletableClipContext{ scene, 0.25F };
    sources.Sync(engine.Native(), deletableClipContext, resolver, buses, nullptr, {}, true);
    Require(sources.SoundCountForTesting() == 1U
            && sources.SoundForTesting(sourceObject.Entity().Id()) != nullptr,
        "Imported audio source was not live before asset deletion");
    Require(scene.Assets().Manager().DeleteAsset(deletableClipId),
        "Production asset deletion rejected the live imported audio fixture");
    Require(scene.Assets().Manager().Registry().Find(deletableClipId) == nullptr,
        "Deleted audio asset retained authoritative metadata");
    Require(!std::filesystem::exists(deletableContainer),
        "Deleted audio asset retained its owned container");
    kb::scene::SceneSystemContext deletedAssetContext{ scene, 0.25F };
    sources.Sync(engine.Native(), deletedAssetContext, resolver, buses, nullptr, {}, true);
    Require(sources.SoundCountForTesting() == 0U,
        "A deleted audio asset retained its previous native source");
    scene.Components().AudioSources().Remove(sourceObject.Entity());
    kb::scene::SceneSystemContext removedContext{ scene, 0.25F };
    sources.Sync(engine.Native(), removedContext, resolver, buses, nullptr, {}, true);
    Require(sources.SoundCountForTesting() == 0U, "Removed source component retained a native sound");
    if (checkpoint == "source-lifecycle") {
        return;
    }
    pump.Stop();

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
    Require(buses.Sync(scene, true), "Audio test mixer topology was not built");
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

void RunAllocationDiagnosticsTest() {
    kb::audio_miniaudio::MiniaudioAllocationTracker tracker;
    const auto callbacks = tracker.Callbacks();
    auto* pointer = callbacks.onMalloc(17U, callbacks.pUserData);
    Require(pointer != nullptr && reinterpret_cast<std::uintptr_t>(pointer) % alignof(std::max_align_t) == 0U,
        "Tracked audio allocation lost native alignment");
    Require(tracker.current == 17U && tracker.peak == 17U, "Tracked allocation size differs from requested bytes");
    Require(callbacks.onRealloc(pointer, std::numeric_limits<std::size_t>::max(), callbacks.pUserData) == nullptr
        && tracker.current == 17U, "Overflow realloc changed ownership or allocation totals");
    pointer = callbacks.onRealloc(pointer, 7U, callbacks.pUserData);
    Require(pointer != nullptr && tracker.current == 7U && tracker.peak == 17U, "Shrink realloc corrupted accounting");
    callbacks.onFree(pointer, callbacks.pUserData);
    Require(tracker.current == 0U, "Tracked allocation leaked after free");
    for (int cycle = 0; cycle < 4; ++cycle) {
        kb::audio_miniaudio::MiniaudioEngine engine;
        engine.Initialize(true);
        const auto initialized = engine.Diagnostics();
        Require(initialized.allocatedBytes > 0U && initialized.peakAllocatedBytes >= initialized.allocatedBytes,
            "Audio engine allocations did not use the measured allocator");
        engine.Shutdown();
        Require(engine.Diagnostics().allocatedBytes == 0U, "Audio engine shutdown leaked tracked allocations");
    }
}

void RunRealDeviceLifecycleTest(const std::filesystem::path& clipPath, int seconds) {
    kb::scene::Scene scene;
    RegisterClip(scene, 9851U, clipPath);
    kb::audio_miniaudio::MiniaudioPlaybackBackend backend;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{seconds};
    std::uint64_t blocks = 0U, misses = 0U, peak = 0U, cycles = 0U;
    do {
        Require(backend.Reinitialize(scene) == kb::audio::AudioDeviceStatus::PlaybackAvailable,
            "Real-device qualification requires a started OS output device; offline fallback is not accepted");
        const auto initial = backend.RuntimeDiagnostics();
        Require(!initial.offline && !initial.backend.empty() && initial.backend != "Null",
            "Device lifecycle unexpectedly used a null backend");
        if (cycles == 0U) {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
            const auto idle = backend.RuntimeDiagnostics();
            Require(idle.deviceBlocks > initial.deviceBlocks && idle.shortBlocks == 0U,
                "An empty graph must produce idle silence without reporting short device blocks");
        }
        const auto voice = backend.PlayOneShot(scene, {.clipAssetId = 9851U, .volume = 0.0F, .loop = true, .spatial = false});
        Require(voice.Succeeded(), "Real-device loop did not start through public playback");
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
        const auto running = backend.RuntimeDiagnostics();
        if (!running.playbackAvailable || running.deviceBlocks <= initial.deviceBlocks || running.shortBlocks != 0U)
            std::cerr << "Device callback observation backend=" << running.backend << " available=" << running.playbackAvailable
                << " initialBlocks=" << initial.deviceBlocks << " blocks=" << running.deviceBlocks << " frames=" << running.deviceFrames
                << " shortBlocks=" << running.shortBlocks << " period=" << running.periodFrames << " worstNs=" << running.worstCallbackNanoseconds << '\n';
        Require(running.playbackAvailable && running.deviceBlocks > initial.deviceBlocks && running.deviceFrames > initial.deviceFrames
            && running.periodFrames > 0U && running.worstCallbackNanoseconds > 0U && running.shortBlocks == 0U,
            "Real OS audio callback did not mix complete blocks");
        Require(backend.VoicePlaybackSeconds(scene, voice.voiceId) > 0.05F, "Real OS callback did not advance the voice audio clock");
        blocks += running.deviceBlocks; misses += running.callbackDeadlineMisses;
        peak = std::max(peak, running.peakAllocatedBytes);
        Require(backend.StopPlaybackDeviceForTesting(), "OS device stop failed");
        Require(backend.DeviceStatus() == kb::audio::AudioDeviceStatus::NoPlaybackDevice, "Stopped device still reports playback");
        Require(backend.Reinitialize(scene) == kb::audio::AudioDeviceStatus::PlaybackAvailable, "OS device restart failed");
        Require(backend.RuntimeDiagnostics().generation > running.generation
            && !backend.IsVoicePlaying(scene, voice.voiceId) && SameResources(backend.ResourcesForTesting(), {}),
            "Reset revived a stale voice or retained native resources");
        backend.Shutdown();
        Require(backend.RuntimeDiagnostics().allocatedBytes == 0U && SameResources(backend.ResourcesForTesting(), {}),
            "OS audio shutdown retained engine allocations or resources");
        ++cycles;
        if (cycles == 1U) std::cout << "audio_device backend=" << running.backend << " sampleRate=" << running.sampleRate
            << " periodFrames=" << running.periodFrames << '\n';
    } while (std::chrono::steady_clock::now() < end);
    Require(misses * 100U <= blocks, "More than one percent of real device callbacks exceeded their period budget");
    std::cout << "audio_device cycles=" << cycles << " blocks=" << blocks << " deadlineMisses=" << misses
        << " peakEngineAllocationBytes=" << peak << " retainedBytes=0 staleVoices=0\n";
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
    Require(resources.sourceSounds == 1U && resources.voices == 1U && resources.buses == 1U
            && resources.decoders == 0U && resources.encodedPayloads == 0U,
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
    Require(SameResources(resourcesAfterRejectedControls, resourcesBeforeRejectedControls),
        "Rejected backend controls changed native resource ownership");
    Require(backend.ReinitializeForTesting(scene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
        "Controlled no-device restart failed with active resources");
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting released = backend.ResourcesForTesting();
    Require(SameResources(released, {}),
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
    Require(SameResources(afterUnavailableTick, {}),
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
    Require(SameResources(afterUnavailableBusTick, {}),
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
    Require(beforeShutdown.sourceSounds == 1U && beforeShutdown.voices == 1U && beforeShutdown.buses == 1U
            && beforeShutdown.decoders == 0U && beforeShutdown.encodedPayloads == 0U,
        "Controlled shutdown fixture did not own every native resource type");
    backend.Shutdown();
    const kb::audio_miniaudio::MiniaudioPlaybackBackend::ResourceStateForTesting afterShutdown = backend.ResourcesForTesting();
    Require(SameResources(afterShutdown, {}),
        "Backend shutdown retained a native source, voice or bus resource");
}

void RunMultiSourceStressTest(const std::filesystem::path& clipPath) {
    kb::audio_miniaudio::MiniaudioEngine engine;
    engine.Initialize(true);
    kb::scene::Scene scene;
    RegisterClip(scene, 8801U, clipPath);

    const std::filesystem::path mixerPath = TestRoot() / "StressMixer.kbmixer";
    kb::audio::AudioMixerAsset mixer;
    mixer.buses.push_back(kb::audio::AudioMixerBus{ .name = "Effects" });
    Require(kb::audio::AudioMixerAssetIO::Save(mixerPath, mixer),
        "Multi-source stress mixer could not be saved");
    Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                .id = kb::assets::AssetId{ 8899U },
                .type = kb::audio::kAudioMixerAssetType,
                .name = "StressMixer",
                .virtualPath = "/Audio/Stress.kbmixer",
                .physicalPath = mixerPath,
                .contentHash = 1U,
            }),
        "Multi-source stress mixer could not be registered");
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, 8899U);

    constexpr std::size_t kSourceCount = 32U;
    std::array<kb::scene::SceneEntity, kSourceCount> entities{};
    std::array<bool, kSourceCount> expectedPlaying{};
    for (std::size_t index = 0U; index < kSourceCount; ++index) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Stress Source " + std::to_string(index),
            .transform = TransformAt(static_cast<float>(index)),
        });
        entities[index] = object.Entity();
        kb::scene::AudioSourceComponent source{
            .clipAssetId = 8801U,
            .volume = 0.25F + static_cast<float>(index) * 0.01F,
            .pitch = 1.0F + static_cast<float>(index % 4U) * 0.05F,
            .loop = true,
            .spatial = (index % 2U) != 0U,
            .autoplay = (index % 3U) != 0U,
            .pan = static_cast<float>(static_cast<int>(index % 5U) - 2) * 0.2F,
            .spatialBlend = 0.5F,
        };
        if ((index % 2U) != 0U) {
            Require(kb::scene::SetAudioSourceOutputBus(source, "Effects"),
                "Multi-source stress route token was rejected");
        }
        scene.Components().AudioSources().Set(object.Entity(), source);
        expectedPlaying[index] = source.autoplay;
    }

    kb::audio_miniaudio::MiniaudioClipResolver resolver;
    kb::audio_miniaudio::MiniaudioBusRegistry buses{engine};
    kb::audio_miniaudio::MiniaudioSourceRegistry sources;
    OfflineEnginePump pump{ engine };
    Require(buses.Sync(scene, true),
        "Multi-source stress routing graph was not created");
    for (std::size_t index = 0U; index < kSourceCount; index += 6U) {
        Require(sources.PlaySource(engine.Native(), scene, entities[index], resolver, buses, true).Succeeded(),
            "Multi-source stress manual transport could not start before the batch sync");
        expectedPlaying[index] = true;
    }
    kb::scene::SceneSystemContext firstContext{ scene, 0.25F };
    sources.Sync(engine.Native(), firstContext, resolver, buses, nullptr, {}, true);
    Require(sources.NativeSoundCountForTesting() == kSourceCount
            && sources.NativeSoundBranchCountForTesting() == 48U,
        "One source sync did not create all simultaneous native sources");
    for (std::size_t index = 0U; index < kSourceCount; index += 7U) {
        kb::audio_miniaudio::MiniaudioSound* sound = sources.SoundForTesting(entities[index].Id());
        const kb::scene::AudioSourceComponent* component = scene.Components().AudioSources().TryGet(entities[index]);
        const float expectedPrimaryVolume = component != nullptr && component->spatial
            ? component->volume * component->spatialBlend
            : component != nullptr ? component->volume : 0.0F;
        Require(sound != nullptr && component != nullptr
                && Near(ma_sound_get_volume(sound->PrimaryForTesting()), expectedPrimaryVolume)
                && Near(ma_sound_get_pitch(sound->PrimaryForTesting()), component->pitch),
            "Multi-source stress settings did not reach selected native sounds");
        const ma_vec3f position = ma_sound_get_position(sound->PrimaryForTesting());
        Require(Near(position.x, static_cast<float>(index)),
            "Multi-source stress transform did not reach a selected native sound");
        Require(sources.IsSourcePlaying(scene, entities[index], true).playing == expectedPlaying[index],
            "Multi-source stress autoplay/manual transport diverged");
    }

    for (std::size_t index = 0U; index < 8U; ++index) {
        scene.Entities().SetActive(entities[index], false);
    }
    for (std::size_t index = 8U; index < 16U; ++index) {
        scene.Components().AudioSources().Remove(entities[index]);
    }
    for (std::size_t index = 16U; index < 24U; ++index) {
        scene.Entities().Destroy(entities[index]);
    }
    kb::scene::SceneSystemContext cleanupContext{ scene, 0.25F };
    sources.Sync(engine.Native(), cleanupContext, resolver, buses, nullptr, {}, true);
    Require(sources.NativeSoundCountForTesting() == 8U
            && sources.NativeSoundBranchCountForTesting() == 12U,
        "Mixed deactivate/remove/destroy cleanup retained the wrong native-source count");
    for (std::size_t index = 24U; index < kSourceCount; ++index) {
        scene.Entities().Destroy(entities[index]);
    }
    kb::scene::SceneSystemContext finalContext{ scene, 0.25F };
    sources.Sync(engine.Native(), finalContext, resolver, buses, nullptr, {}, true);
    Require(sources.NativeSoundCountForTesting() == 0U
            && sources.NativeSoundBranchCountForTesting() == 0U
            && sources.SoundCountForTesting() == 0U,
        "Multi-source stress final cleanup retained source records or natives");
}

void RunBackendCycleStressTest(const std::filesystem::path& clipPath) {
    kb::scene::Scene scene;
    RegisterClip(scene, 8901U, clipPath);
    const std::filesystem::path mixerPath = TestRoot() / "CycleMixer.kbmixer";
    kb::audio::AudioMixerAsset mixer;
    mixer.buses.push_back(kb::audio::AudioMixerBus{ .name = "Effects" });
    Require(kb::audio::AudioMixerAssetIO::Save(mixerPath, mixer)
            && scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                .id = kb::assets::AssetId{ 8999U },
                .type = kb::audio::kAudioMixerAssetType,
                .name = "CycleMixer",
                .virtualPath = "/Audio/Cycle.kbmixer",
                .physicalPath = mixerPath,
                .contentHash = 1U,
            }),
        "Backend-cycle mixer fixture could not be prepared");
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, 8999U);
    const kb::scene::SceneObject source = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "Cycle Source" });
    kb::scene::AudioSourceComponent sourceComponent{
        .clipAssetId = 8901U,
        .volume = 0.0F,
        .loop = true,
        .spatial = false,
    };
    Require(kb::scene::SetAudioSourceOutputBus(sourceComponent, "Effects"),
        "Backend-cycle source route was rejected");
    scene.Components().AudioSources().Set(source.Entity(), sourceComponent);
    kb::audio::AudioPlayDesc voice{
        .clipAssetId = 8901U,
        .volume = 0.0F,
        .loop = true,
        .spatial = false,
    };
    voice.outputBus = "Effects";

    kb::audio_miniaudio::MiniaudioPlaybackBackend backend;
    for (std::size_t cycle = 0U; cycle < 24U; ++cycle) {
        Require(backend.ReinitializeForTesting(scene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
            "Backend-cycle offline reinitialize failed");
        Require(backend.PlaySourceForTesting(scene, source.Entity()).Succeeded()
                && backend.PlayOneShotForTesting(scene, voice).Succeeded()
                && backend.PumpFramesForTesting(256U).frames > 0U,
            "Backend-cycle could not create and pump its live resources");
        const auto live = backend.ResourcesForTesting();
        Require(live.sourceSounds == 1U && live.voices == 1U && live.buses == 1U
                && live.decoders == 0U && live.encodedPayloads == 0U,
            "Backend-cycle did not own exact source, voice and bus resources");
        if ((cycle % 2U) == 0U) {
            Require(backend.ReinitializeForTesting(scene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice,
                "Backend-cycle active-resource reinitialize failed");
        } else {
            backend.Shutdown();
        }
        Require(SameResources(backend.ResourcesForTesting(), {}),
            "Backend-cycle teardown retained a native source, voice or bus");
    }
    Require(backend.ReinitializeForTesting(scene, true) == kb::audio::AudioDeviceStatus::NoPlaybackDevice
            && backend.PlaySourceForTesting(scene, source.Entity()).Succeeded()
            && backend.PlayOneShotForTesting(scene, voice).Succeeded()
            && backend.PumpFramesForTesting(256U).frames > 0U,
        "Backend was not reusable after 24 complete lifecycle cycles");
    backend.Shutdown();
    Require(SameResources(backend.ResourcesForTesting(), {}),
        "Final backend-cycle shutdown retained native resources");
}

template <typename Resolver>
void RunClipResolverValidationTest(const std::filesystem::path& clipPath, Resolver& resolver) {
    kb::scene::Scene scene;
    RegisterClip(scene, 8601U, clipPath);
    const auto resolvedDirect = resolver.Resolve(scene, 8601U);
    Require(resolvedDirect.Succeeded() && !resolvedDirect.clip.IsMemoryBacked()
            && resolvedDirect.clip.path == clipPath,
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
    Require(!resolver.Resolve(scene, 8602U).Succeeded(),
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
    Require(!resolver.Resolve(scene, 8603U).Succeeded(),
        "Audio clip resolver accepted imported metadata on a native source file");

    const std::filesystem::path unsupportedPath = TestRoot() / "Unsupported.aac";
    std::filesystem::copy_file(clipPath, unsupportedPath, std::filesystem::copy_options::overwrite_existing);
    Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                .id = kb::assets::AssetId{ 8604U },
                .type = "AudioClip",
                .name = "UnsupportedPhysical",
                .virtualPath = "/Audio/Unsupported.aac",
                .physicalPath = unsupportedPath,
                .contentHash = 8604U,
            }),
        "Unsupported-physical resolver fixture registration failed");
    Require(!resolver.Resolve(scene, 8604U).Succeeded(),
        "Audio clip resolver accepted an unsupported physical extension");

    const std::filesystem::path projectRoot = TestRoot() / "ResolverProject";
    Require(scene.Assets().MountProject(projectRoot), "Audio resolver import project did not mount");
    const std::array<std::filesystem::path, 1U> sourceFiles{ clipPath };
    const kb::assets::AssetImportResult imported = kb::assets::AssetImportService::ImportFiles(
        scene.Assets().Manager(), sourceFiles, "/Game/Audio");
    Require(imported.Succeeded() && imported.items.size() == 1U,
        "Supported audio resolver import fixture failed");
    const auto resolvedImported = resolver.Resolve(scene, imported.items[0].id.value);
    Require(resolvedImported.Succeeded() && resolvedImported.clip.IsMemoryBacked()
            && resolvedImported.clip.path.empty()
            && resolvedImported.clip.extension == ".wav"
            && resolvedImported.clip.EncodedBytes().size() == std::filesystem::file_size(clipPath),
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
    Require(!resolver.Resolve(scene, unsupportedImported.items[0].id.value).Succeeded(),
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
    if (filter.empty() || filter == "allocation-diagnostics") RunAllocationDiagnosticsTest();
    if (filter == "real-device") RunRealDeviceLifecycleTest(clipPath, 3);
    if (filter == "device-soak") RunRealDeviceLifecycleTest(clipPath, 1800);
    if (filter == "mixer-dsp-workload") RunMixerDspWorkloadBenchmark();
    if (filter == "mixer-meters-workload") RunMixerDspWorkloadBenchmark(true);
    if (filter == "mixer-meters") RunMixerMeterRuntimeTest();
    if (filter.empty() || filter == "mixer-dsp") {
        RunMixerDspAssetTest();
        RunMixerDspRuntimeTest();
    }
    if (filter.empty() || filter == "mixer-dsp-concurrent") {
        RunMixerDspConcurrentTest(false);
        RunMixerDspConcurrentTest(true);
    }
    if (filter.empty() || filter == "formats") {
        RunAdvertisedFormatDecodeTest();
        RunFormatRejectionTest();
    }
    if (filter.empty() || filter == "vorbis-unicode") {
        RunVorbisUnicodePathTest();
    }
    if (filter.empty() || filter == "vorbis-transport") {
        RunVorbisTransportTest();
    }
    if (filter.empty() || filter == "bus-topology") {
        RunBusTopologyPcmTest();
    }
    if (filter.empty() || filter == "snapshot-transition") {
        RunSnapshotTransitionPcmTest();
    }
    if (filter.empty() || filter == "import-memory") {
        RunImportedMemoryLifecycleTest();
    }
    if (filter.empty() || filter == "package-memory") {
        RunPackagedAudioMemoryTest();
    }
    if (filter.empty() || filter == "sound") {
        RunSoundStateTest(clipPath);
        RunSpatialDistanceAttenuationOutputTest();
        RunSpatialPanningOutputTest();
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
    if (filter.empty() || filter == "large-world") {
        RunFarListenerAudioSpaceTest();
    }
    if (filter.empty() || filter == "source-routing") {
        RunSourceLifecycleAndRoutingTest(clipPath);
    } else if (filter.starts_with("source-")) {
        RunSourceLifecycleAndRoutingTest(clipPath, filter);
    }
    if (filter.empty() || filter == "backend-restart") {
        RunBackendRestartTest(clipPath);
    }
    if (filter.empty() || filter == "source-stress") {
        RunMultiSourceStressTest(clipPath);
    }
    if (filter.empty() || filter == "cycle-stress") {
        RunBackendCycleStressTest(clipPath);
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
