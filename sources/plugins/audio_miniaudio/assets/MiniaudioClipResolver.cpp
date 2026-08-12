#include "assets/MiniaudioClipResolver.hpp"

#include "assets/MiniaudioEncodingFormat.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetImportCatalog.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/audio/AudioClipFormats.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace kb::audio_miniaudio {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr ma_uint64 kDecodeProbeFrames = 256U;
constexpr std::uint64_t kCompressedTailBytes = 64U * 1024U;

[[nodiscard]] std::uint64_t HashBytes(std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = kFnvOffset;
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<unsigned char>(value);
        hash *= kFnvPrime;
    }
    return hash == 0U ? kFnvPrime : hash;
}

[[nodiscard]] std::uint32_t ReadLittleEndianU32(const unsigned char* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1]) << 8U)
        | (static_cast<std::uint32_t>(bytes[2]) << 16U)
        | (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

[[nodiscard]] std::uint32_t ReadBigEndianU32(const unsigned char* bytes) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U)
        | (static_cast<std::uint32_t>(bytes[1]) << 16U)
        | (static_cast<std::uint32_t>(bytes[2]) << 8U)
        | static_cast<std::uint32_t>(bytes[3]);
}

class EncodedReader final {
public:
    explicit EncodedReader(const ResolvedAudioClip& clip)
        : path_(clip.path)
        , bytes_(clip.EncodedBytes()) {
        if (!bytes_.empty()) {
            size_ = bytes_.size();
            valid_ = true;
            return;
        }
        std::error_code error;
        size_ = std::filesystem::file_size(path_, error);
        valid_ = !error && size_ != 0U;
    }

    [[nodiscard]] bool IsValid() const noexcept { return valid_; }
    [[nodiscard]] std::uint64_t Size() const noexcept { return size_; }

    [[nodiscard]] bool Read(std::uint64_t offset, std::span<unsigned char> destination) const {
        if (!valid_ || offset > size_ || destination.size() > size_ - offset) {
            return false;
        }
        if (!bytes_.empty()) {
            std::memcpy(destination.data(), bytes_.data() + offset, destination.size());
            return true;
        }
        if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())
            || destination.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
            return false;
        }
        std::ifstream input{ path_, std::ios::binary };
        if (!input.is_open()) {
            return false;
        }
        input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        input.read(reinterpret_cast<char*>(destination.data()),
            static_cast<std::streamsize>(destination.size()));
        return input.gcount() == static_cast<std::streamsize>(destination.size());
    }

private:
    std::filesystem::path path_;
    std::span<const std::byte> bytes_;
    std::uint64_t size_ = 0U;
    bool valid_ = false;
};

[[nodiscard]] bool ValidateWaveContainer(const EncodedReader& reader) {
    std::array<unsigned char, 12U> prefix{};
    if (!reader.Read(0U, prefix)) {
        return false;
    }
    return prefix[0] == 'R' && prefix[1] == 'I' && prefix[2] == 'F' && prefix[3] == 'F'
        && prefix[8] == 'W' && prefix[9] == 'A' && prefix[10] == 'V' && prefix[11] == 'E'
        && static_cast<std::uint64_t>(ReadLittleEndianU32(prefix.data() + 4U)) + 8U <= reader.Size();
}

[[nodiscard]] bool ValidateFlacContainer(const EncodedReader& reader) {
    std::array<unsigned char, 4U> prefix{};
    return reader.Read(0U, prefix)
        && prefix[0] == 'f' && prefix[1] == 'L' && prefix[2] == 'a' && prefix[3] == 'C';
}

[[nodiscard]] std::uint16_t FlacFrameCrc(std::span<const unsigned char> bytes) noexcept {
    std::uint16_t crc = 0U;
    for (const unsigned char byte : bytes) {
        crc ^= static_cast<std::uint16_t>(byte) << 8U;
        for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
            crc = static_cast<std::uint16_t>((crc << 1U) ^ ((crc & 0x8000U) != 0U ? 0x8005U : 0U));
        }
    }
    return crc;
}

[[nodiscard]] std::uint8_t FlacHeaderCrc(std::span<const unsigned char> bytes) noexcept {
    std::uint8_t crc = 0U;
    for (const unsigned char byte : bytes) {
        crc ^= byte;
        for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
            crc = static_cast<std::uint8_t>((crc << 1U) ^ ((crc & 0x80U) != 0U ? 0x07U : 0U));
        }
    }
    return crc;
}

[[nodiscard]] std::optional<std::size_t> FlacFrameHeaderBytes(
    std::span<const unsigned char> bytes) noexcept {
    if (bytes.size() < 7U || bytes[0] != 0xFFU || (bytes[1] & 0xFEU) != 0xF8U
        || (bytes[2] & 0xF0U) == 0xF0U || (bytes[2] & 0x0FU) == 0x0FU
        || (bytes[3] >> 4U) > 10U
        || ((bytes[3] >> 1U) & 0x07U) == 3U || ((bytes[3] >> 1U) & 0x07U) == 7U
        || (bytes[3] & 0x01U) != 0U) {
        return std::nullopt;
    }
    std::size_t cursor = 4U;
    const unsigned char numberPrefix = bytes[cursor];
    std::size_t numberBytes = 1U;
    if ((numberPrefix & 0x80U) != 0U) {
        unsigned char mask = 0x80U;
        numberBytes = 0U;
        while ((numberPrefix & mask) != 0U && numberBytes < 7U) {
            ++numberBytes;
            mask >>= 1U;
        }
        if (numberBytes < 2U || numberBytes > 7U || cursor + numberBytes > bytes.size()) {
            return std::nullopt;
        }
        for (std::size_t index = 1U; index < numberBytes; ++index) {
            if ((bytes[cursor + index] & 0xC0U) != 0x80U) {
                return std::nullopt;
            }
        }
    }
    cursor += numberBytes;
    const unsigned char blockSizeCode = bytes[2] >> 4U;
    cursor += blockSizeCode == 6U ? 1U : blockSizeCode == 7U ? 2U : 0U;
    const unsigned char sampleRateCode = bytes[2] & 0x0FU;
    cursor += sampleRateCode == 12U ? 1U : sampleRateCode == 13U || sampleRateCode == 14U ? 2U : 0U;
    if (cursor >= bytes.size()) {
        return std::nullopt;
    }
    return FlacHeaderCrc(bytes.first(cursor)) == bytes[cursor]
        ? std::optional<std::size_t>{ cursor + 1U }
        : std::nullopt;
}

[[nodiscard]] bool ValidateFlacTailIfBounded(
    const EncodedReader& reader,
    std::uint64_t& inspectedTailBytes) {
    const std::uint64_t tailBytes = std::min(reader.Size(), kCompressedTailBytes);
    inspectedTailBytes = tailBytes;
    if (tailBytes < 8U) {
        return false;
    }
    std::vector<unsigned char> tail(static_cast<std::size_t>(tailBytes));
    if (!reader.Read(reader.Size() - tailBytes, tail)) {
        return false;
    }
    constexpr std::size_t kMaxTailFrameCandidates = 128U;
    std::vector<std::size_t> frameOffsets;
    frameOffsets.reserve(16U);
    for (std::size_t offset = 0U; offset + 4U <= tail.size(); ++offset) {
        if (FlacFrameHeaderBytes(std::span<const unsigned char>{ tail }.subspan(offset)).has_value()) {
            if (frameOffsets.size() == kMaxTailFrameCandidates) {
                // An adversarial or unusual tail with excessive sync-like headers
                // is left to the native bounded readiness probe.
                return true;
            }
            frameOffsets.push_back(offset);
        }
    }
    if (frameOffsets.empty()) {
        // A valid unusually large final frame can begin before the fixed window. Do
        // not narrow the native decoder's accepted variants merely because bounded
        // tail validation cannot see that frame's header.
        return true;
    }
    const auto frameCrcMatches = [&tail](std::size_t begin, std::size_t end) {
        if (end < begin + 4U) {
            return false;
        }
        const std::span<const unsigned char> frame{ tail.data() + begin, end - begin };
        const std::uint16_t expected = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(frame[frame.size() - 2U]) << 8U)
            | frame[frame.size() - 1U]);
        return FlacFrameCrc(frame.first(frame.size() - 2U)) == expected;
    };
    std::vector<bool> linkedFrames(frameOffsets.size() > 1U ? frameOffsets.size() - 1U : 0U);
    for (std::size_t index = 0U; index < linkedFrames.size(); ++index) {
        linkedFrames[index] = frameCrcMatches(frameOffsets[index], frameOffsets[index + 1U]);
    }
    for (std::size_t index = frameOffsets.size(); index-- > 0U;) {
        const bool hasVerifiedPredecessor = index > 0U && linkedFrames[index - 1U];
        const bool hasVerifiedSuccessor = index < linkedFrames.size() && linkedFrames[index];
        if (hasVerifiedPredecessor && !hasVerifiedSuccessor) {
            return frameCrcMatches(frameOffsets[index], tail.size());
        }
    }
    for (const std::size_t offset : frameOffsets) {
        if (frameCrcMatches(offset, tail.size())) {
            return true;
        }
    }
    // An isolated sync-like header is insufficient evidence of truncation.
    return true;
}

struct CompressedFrame final {
    bool recognized = false;
    bool freeFormat = false;
    std::uint32_t bytes = 0U;
    std::uint32_t version = 0U;
    std::uint32_t layer = 0U;
    std::uint32_t sampleRate = 0U;
};

[[nodiscard]] CompressedFrame DecodeCompressedFrameHeader(const unsigned char* bytes) noexcept {
    const std::uint32_t header = ReadBigEndianU32(bytes);
    if ((header & 0xFFE00000U) != 0xFFE00000U) {
        return {};
    }
    const std::uint32_t version = (header >> 19U) & 0x3U;
    const std::uint32_t layer = (header >> 17U) & 0x3U;
    const std::uint32_t bitrateIndex = (header >> 12U) & 0xFU;
    const std::uint32_t sampleRateIndex = (header >> 10U) & 0x3U;
    if (version == 1U || layer != 1U || bitrateIndex == 15U || sampleRateIndex == 3U) {
        return {};
    }
    constexpr std::array<std::uint32_t, 14U> kVersion1Bitrates{
        32U, 40U, 48U, 56U, 64U, 80U, 96U, 112U, 128U, 160U, 192U, 224U, 256U, 320U,
    };
    constexpr std::array<std::uint32_t, 14U> kVersion2Bitrates{
        8U, 16U, 24U, 32U, 40U, 48U, 56U, 64U, 80U, 96U, 112U, 128U, 144U, 160U,
    };
    constexpr std::array<std::uint32_t, 3U> kSampleRates{ 44100U, 48000U, 32000U };
    std::uint32_t sampleRate = kSampleRates[sampleRateIndex];
    sampleRate /= version == 2U ? 2U : version == 0U ? 4U : 1U;
    if (bitrateIndex == 0U) {
        return {
            .recognized = true,
            .freeFormat = true,
            .version = version,
            .layer = layer,
            .sampleRate = sampleRate,
        };
    }
    const std::uint32_t bitrate =
        (version == 3U ? kVersion1Bitrates : kVersion2Bitrates)[bitrateIndex - 1U];
    return {
        .recognized = true,
        .bytes = (version == 3U ? 144000U : 72000U) * bitrate / sampleRate
            + ((header >> 9U) & 0x1U),
        .version = version,
        .layer = layer,
        .sampleRate = sampleRate,
    };
}

[[nodiscard]] bool CompatibleCompressedFrames(
    const CompressedFrame& left,
    const CompressedFrame& right) noexcept {
    return left.recognized && right.recognized
        && left.version == right.version
        && left.layer == right.layer
        && left.sampleRate == right.sampleRate;
}

[[nodiscard]] std::uint64_t CompressedLogicalEnd(const EncodedReader& reader) {
    std::uint64_t logicalEnd = reader.Size();
    if (logicalEnd >= 128U) {
        std::array<unsigned char, 3U> tag{};
        if (reader.Read(logicalEnd - 128U, tag)
            && tag[0] == 'T' && tag[1] == 'A' && tag[2] == 'G') {
            logicalEnd -= 128U;
        }
    }
    if (logicalEnd >= 32U) {
        std::array<unsigned char, 32U> footer{};
        if (reader.Read(logicalEnd - footer.size(), footer)
            && std::equal(footer.begin(), footer.begin() + 8U,
                std::array<unsigned char, 8U>{ 'A', 'P', 'E', 'T', 'A', 'G', 'E', 'X' }.begin())) {
            const std::uint32_t tagBytes = ReadLittleEndianU32(footer.data() + 12U);
            if (tagBytes >= footer.size() && tagBytes <= logicalEnd) {
                logicalEnd -= tagBytes;
            }
        }
    }
    return logicalEnd;
}

[[nodiscard]] bool ValidateCompressedContainer(
    const EncodedReader& reader,
    std::uint64_t& inspectedTailBytes) {
    std::array<unsigned char, 10U> prefix{};
    if (!reader.Read(0U, prefix)) {
        return false;
    }
    std::uint64_t firstFrameOffset = 0U;
    if (prefix[0] == 'I' && prefix[1] == 'D' && prefix[2] == '3') {
        if ((prefix[6] | prefix[7] | prefix[8] | prefix[9]) >= 0x80U) {
            return false;
        }
        firstFrameOffset = 10U
            + (static_cast<std::uint64_t>(prefix[6]) << 21U)
            + (static_cast<std::uint64_t>(prefix[7]) << 14U)
            + (static_cast<std::uint64_t>(prefix[8]) << 7U)
            + static_cast<std::uint64_t>(prefix[9])
            + ((prefix[5] & 0x10U) != 0U ? 10U : 0U);
    }
    const std::uint64_t logicalEnd = CompressedLogicalEnd(reader);
    if (firstFrameOffset > logicalEnd) {
        return false;
    }
    const std::uint64_t boundedBytes =
        std::min(logicalEnd - firstFrameOffset, kCompressedTailBytes);
    inspectedTailBytes = boundedBytes;
    if (boundedBytes < 4U) {
        return true;
    }
    std::vector<unsigned char> boundedPrefix(static_cast<std::size_t>(boundedBytes));
    if (!reader.Read(firstFrameOffset, boundedPrefix)) {
        return true;
    }

    // Format identity, legal metadata variants and free-format support are owned
    // by the forced native decoder. This bounded walk can only reject truncation
    // proven by a compatible, exact frame chain reachable from the legal start.
    std::uint64_t frameStart = firstFrameOffset;
    CompressedFrame previous{};
    std::size_t provenFrames = 0U;
    while (frameStart + 4U <= logicalEnd
        && frameStart - firstFrameOffset + 4U <= boundedPrefix.size()) {
        const std::size_t relativeStart = static_cast<std::size_t>(frameStart - firstFrameOffset);
        const CompressedFrame frame = DecodeCompressedFrameHeader(
            boundedPrefix.data() + relativeStart);
        if (!frame.recognized || frame.freeFormat || frame.bytes == 0U
            || (provenFrames != 0U && !CompatibleCompressedFrames(previous, frame))) {
            return true;
        }
        ++provenFrames;
        if (frame.bytes > logicalEnd - frameStart) {
            return provenFrames >= 2U ? false : true;
        }
        const std::uint64_t nextFrame = frameStart + frame.bytes;
        if (nextFrame == logicalEnd) {
            return true;
        }
        if (nextFrame - firstFrameOffset + 4U > boundedPrefix.size()) {
            return true;
        }
        previous = frame;
        frameStart = nextFrame;
    }
    return true;
}

[[nodiscard]] ma_result InitializeDecoder(const ResolvedAudioClip& clip, ma_decoder& decoder) {
    const ma_encoding_format encodingFormat = EncodingFormatForAudioExtension(clip.extension);
    if (encodingFormat == ma_encoding_format_unknown) {
        return MA_INVALID_ARGS;
    }
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0U, 0U);
    config.encodingFormat = encodingFormat;
    if (clip.IsMemoryBacked()) {
        const std::span<const std::byte> bytes = clip.EncodedBytes();
        return ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder);
    }
#if defined(_WIN32)
    const std::wstring nativePath = clip.path.wstring();
    return ma_decoder_init_file_w(nativePath.c_str(), &config, &decoder);
#else
    const std::string nativePath = clip.path.string();
    return ma_decoder_init_file(nativePath.c_str(), &config, &decoder);
#endif
}

[[nodiscard]] bool ProbeDecoder(
    const ResolvedAudioClip& clip,
    std::uint64_t& decodedProbeFrames) {
    ma_decoder decoder{};
    if (InitializeDecoder(clip, decoder) != MA_SUCCESS) {
        return false;
    }
    struct DecoderScope final {
        ma_decoder* decoder = nullptr;
        ~DecoderScope() { ma_decoder_uninit(decoder); }
    } scope{ &decoder };

    ma_format format = ma_format_unknown;
    ma_uint32 channels = 0U;
    ma_uint32 sampleRate = 0U;
    if (ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0U) != MA_SUCCESS
        || format != ma_format_f32 || channels == 0U || channels > MA_MAX_CHANNELS || sampleRate == 0U) {
        return false;
    }
    std::vector<float> frames(static_cast<std::size_t>(kDecodeProbeFrames) * channels);
    ma_uint64 framesRead = 0U;
    const ma_result firstResult = ma_decoder_read_pcm_frames(
        &decoder, frames.data(), kDecodeProbeFrames, &framesRead);
    decodedProbeFrames = framesRead;
    if ((firstResult != MA_SUCCESS && firstResult != MA_AT_END) || framesRead == 0U) {
        return false;
    }
    return true;
}

[[nodiscard]] std::filesystem::path ResolveMountedAudio(
    kb::scene::Scene& scene,
    const kb::assets::AssetMetadata& metadata) {
    std::filesystem::path resolved = metadata.physicalPath;
    if (resolved.empty()) {
        const std::optional<std::filesystem::path> mountedPath =
            scene.Assets().Manager().Mounts().Resolve(metadata.virtualPath);
        if (!mountedPath.has_value()) {
            return {};
        }
        resolved = *mountedPath;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(resolved, error) && !error
        ? resolved
        : std::filesystem::path{};
}

} // namespace

MiniaudioClipResolver::Resolution MiniaudioClipResolver::ResolveImportedAudio(
    kb::scene::Scene& scene,
    std::uint64_t clipAssetId,
    std::uint64_t metadataContentHash) const {
    const kb::assets::AssetHandle<kb::assets::ImportedAsset> imported =
        scene.Assets().Manager().Load<kb::assets::ImportedAsset>(kb::assets::AssetId{ clipAssetId });
    if (!imported.IsLoaded() || imported->category != kb::assets::AssetImportCategory::Audio
        || imported->payload.empty() || imported->sourceSize != imported->payload.size()) {
        ForgetValidation(clipAssetId);
        return { .status = ResolutionStatus::DecodeRejected };
    }
    const std::string_view extension = kb::audio::CanonicalAudioClipExtension(imported->sourceExtension);
    if (extension.empty() || imported->sourceHash == 0U) {
        ForgetValidation(clipAssetId);
        return { .status = ResolutionStatus::DecodeRejected };
    }
    ResolvedAudioClip clip{
        .imported = imported.Shared(),
        .extension = std::string{ extension },
        .identity = std::to_string(clipAssetId) + ":" + std::to_string(metadataContentHash)
            + ":" + std::to_string(imported->sourceHash) + ":"
            + std::to_string(imported->payload.size()) + ":" + std::string{ extension },
    };
    return ValidateDecodeReady(clipAssetId, clip)
        ? Resolution{ .status = ResolutionStatus::Resolved, .clip = std::move(clip) }
        : Resolution{ .status = ResolutionStatus::DecodeRejected };
}

bool MiniaudioClipResolver::ValidateDecodeReady(
    std::uint64_t clipAssetId,
    const ResolvedAudioClip& clip) const {
    const auto cached = validatedContent_.find(clipAssetId);
    if (cached != validatedContent_.end() && cached->second.identity == clip.identity) {
        cached->second.lastUse = ++validationUseSerial_;
        return cached->second.valid;
    }
#if defined(KB_AUDIO_MINIAUDIO_TESTING)
    ++validationStats_.attempts;
#endif
    if (clip.IsMemoryBacked()) {
        const std::span<const std::byte> payload = clip.EncodedBytes();
#if defined(KB_AUDIO_MINIAUDIO_TESTING)
        ++validationStats_.payloadHashAttempts;
        validationStats_.payloadBytesHashed += payload.size();
#endif
        if (HashBytes(payload) != clip.imported->sourceHash) {
            StoreValidation(clipAssetId, clip.identity, false);
            return false;
        }
    }
    const EncodedReader reader{ clip };
    std::uint64_t inspectedTailBytes = 0U;
    const bool containerValid = reader.IsValid()
        && (clip.extension == ".wav"
                ? ValidateWaveContainer(reader)
                : clip.extension == ".flac"
                    ? ValidateFlacContainer(reader)
                        && ValidateFlacTailIfBounded(reader, inspectedTailBytes)
                    : clip.extension == ".mp3"
                        ? ValidateCompressedContainer(reader, inspectedTailBytes)
                        : false);
    std::uint64_t decodedProbeFrames = 0U;
    const bool valid = containerValid && ProbeDecoder(clip, decodedProbeFrames);
#if defined(KB_AUDIO_MINIAUDIO_TESTING)
    validationStats_.maxDecodedProbeFrames =
        std::max(validationStats_.maxDecodedProbeFrames, decodedProbeFrames);
    validationStats_.maxTailBytesInspected =
        std::max(validationStats_.maxTailBytesInspected, inspectedTailBytes);
#endif
    StoreValidation(clipAssetId, clip.identity, valid);
    return valid;
}

void MiniaudioClipResolver::ForgetValidation(std::uint64_t clipAssetId) const noexcept {
    validatedContent_.erase(clipAssetId);
}

void MiniaudioClipResolver::StoreValidation(
    std::uint64_t clipAssetId,
    std::string identity,
    bool valid) const {
    auto existing = validatedContent_.find(clipAssetId);
    if (existing == validatedContent_.end() && validatedContent_.size() >= kValidationCacheCapacity) {
        const auto oldest = std::min_element(validatedContent_.begin(), validatedContent_.end(),
            [](const auto& left, const auto& right) {
                return left.second.lastUse < right.second.lastUse
                    || (left.second.lastUse == right.second.lastUse && left.first < right.first);
            });
        validatedContent_.erase(oldest);
    }
    validatedContent_.insert_or_assign(clipAssetId, ValidationCacheEntry{
        .identity = std::move(identity),
        .lastUse = ++validationUseSerial_,
        .valid = valid,
    });
#if defined(KB_AUDIO_MINIAUDIO_TESTING)
    validationStats_.maxCacheEntries =
        std::max(validationStats_.maxCacheEntries, validatedContent_.size());
#endif
}

#if defined(KB_AUDIO_MINIAUDIO_TESTING)
MiniaudioClipResolver::ValidationStatsForTesting
MiniaudioClipResolver::StatsForTesting() const noexcept {
    ValidationStatsForTesting stats = validationStats_;
    stats.cacheEntries = validatedContent_.size();
    return stats;
}
#endif

void MiniaudioClipResolver::Reset() noexcept {
    validatedContent_.clear();
    validationUseSerial_ = 0U;
#if defined(KB_AUDIO_MINIAUDIO_TESTING)
    validationStats_ = {};
#endif
}

MiniaudioClipResolver::Resolution MiniaudioClipResolver::Resolve(
    kb::scene::Scene& scene,
    std::uint64_t clipAssetId) const {
    if (clipAssetId == 0U) {
        return { .status = ResolutionStatus::MissingOrInvalid };
    }
    const kb::assets::AssetMetadata* metadata =
        scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ clipAssetId });
    if (metadata == nullptr) {
        ForgetValidation(clipAssetId);
        return { .status = ResolutionStatus::MissingOrInvalid };
    }
    const std::filesystem::path authoredPath = metadata->physicalPath.empty()
        ? metadata->virtualPath
        : metadata->physicalPath;
    if (kb::assets::AssetImportCatalog::IsEngineAssetExtension(authoredPath.extension())) {
        if (metadata->type != "ImportedAsset" || metadata->importCategory != "Audio"
            || ResolveMountedAudio(scene, *metadata).empty()) {
            ForgetValidation(clipAssetId);
            return { .status = ResolutionStatus::MissingOrInvalid };
        }
        return ResolveImportedAudio(scene, clipAssetId, metadata->contentHash);
    }
    if (metadata->type != "AudioClip" || !metadata->importCategory.empty()) {
        ForgetValidation(clipAssetId);
        return { .status = ResolutionStatus::MissingOrInvalid };
    }
    const std::string_view extension =
        kb::audio::CanonicalAudioClipExtension(authoredPath.extension().string());
    const std::filesystem::path resolved = ResolveMountedAudio(scene, *metadata);
    if (extension.empty() || resolved.empty()) {
        ForgetValidation(clipAssetId);
        return { .status = ResolutionStatus::MissingOrInvalid };
    }
    ResolvedAudioClip clip{
        .path = resolved,
        .extension = std::string{ extension },
        .identity = std::to_string(clipAssetId) + ":" + std::to_string(metadata->contentHash)
            + ":" + resolved.generic_string(),
    };
    return ValidateDecodeReady(clipAssetId, clip)
        ? Resolution{ .status = ResolutionStatus::Resolved, .clip = std::move(clip) }
        : Resolution{ .status = ResolutionStatus::DecodeRejected };
}

} // namespace kb::audio_miniaudio
