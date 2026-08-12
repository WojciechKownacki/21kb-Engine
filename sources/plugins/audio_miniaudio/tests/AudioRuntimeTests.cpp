#include "assets/MiniaudioClipResolver.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/audio/AudioClipFormats.hpp"
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

constexpr std::array<AudioFixture, 3U> kAudioFixtures{
    AudioFixture{ .extension = ".wav", .filename = "tone.wav", .expectedBytes = 22094U, .magic = { 'R', 'I', 'F', 'F' } },
    AudioFixture{ .extension = ".flac", .filename = "tone.flac", .expectedBytes = 4999U, .magic = { 'f', 'L', 'a', 'C' } },
    AudioFixture{ .extension = ".mp3", .filename = "tone.mp3", .expectedBytes = 3448U, .magic = { 0xFFU, 0xFBU, 0x70U, 0xC4U }, .firstFrameBytes = 313U },
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
                  static_cast<void>(ma_engine_read_pcm_frames(&engine.Native(), frames.data(), frames.size() / 2U, nullptr));
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

    OfflineEnginePump(const OfflineEnginePump&) = delete;
    OfflineEnginePump& operator=(const OfflineEnginePump&) = delete;

private:
    std::atomic<bool> stop_{ false };
    std::thread thread_;
};

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
            "Advertised direct audio asset failed bounded resolver validation");
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

        for (const std::filesystem::path& rejectedPath : { invalidMagic, truncated }) {
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
    for (std::uint32_t attempt = 0U; attempt < 100U && !endedSound->AtEnd(); ++attempt) {
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
    for (std::uint32_t attempt = 0U; attempt < 1000U && pool.IsVoicePlaying(naturallyEnded.voiceId); ++attempt) {
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
    kb::audio_miniaudio::MiniaudioBusRegistry buses;
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
    kb::audio_miniaudio::MiniaudioBusRegistry buses;
    kb::audio_miniaudio::MiniaudioSourceRegistry sources;
    OfflineEnginePump pump{ engine };
    Require(buses.Sync(engine.Native(), scene, true),
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
    if (filter.empty() || filter == "formats") {
        RunAdvertisedFormatDecodeTest();
        RunFormatRejectionTest();
    }
    if (filter.empty() || filter == "import-memory") {
        RunImportedMemoryLifecycleTest();
    }
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
