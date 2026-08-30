#include "engine/scene/SkeletalMeshAssetIO.hpp"

#include "engine/library/EngineLibraryParsing.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/VersionedTextAssetHeader.hpp"

#include <charconv>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <span>
#include <string_view>
#include <type_traits>
#include <array>
#include <system_error>

namespace kb::scene {
namespace {
constexpr std::array<std::uint8_t, 8U> kDerivedMagic{ 'K', 'B', 'S', 'M', 'D', 'D', 'C', 0U };
// Version 3 stores the canonical outward-facing winding. Version 2 caches may contain meshes
// produced by the legacy FBX axis conversion, where both triangle winding and normals point
// inward, so they must be rebuilt from the source asset and migrated below.
constexpr std::uint32_t kDerivedVersion = 3U;
constexpr std::uint32_t kMaxDerivedLods = 64U;
constexpr std::uint32_t kMaxDerivedVertices = 10'000'000U;
constexpr std::uint32_t kMaxDerivedIndices = 30'000'000U;
constexpr std::uint32_t kMaxDerivedSections = 1'000'000U;
constexpr std::uint32_t kMaxDerivedBones = 1'000'000U;
constexpr std::uint32_t kMaxDerivedMorphTargets = 100'000U;
constexpr std::uint32_t kMaxDerivedMorphDeltas = 10'000'000U;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

static_assert(std::is_trivially_copyable_v<SkeletalMeshVertex>);
static_assert(std::is_trivially_copyable_v<SkeletalMeshBoneBounds>);
static_assert(std::is_trivially_copyable_v<SkeletalMeshMorphDelta>);

[[nodiscard]] std::uint64_t HashBytes(std::string_view bytes) noexcept {
    std::uint64_t hash = kFnvOffset;
    for (const unsigned char value : bytes) {
        hash ^= value;
        hash *= kFnvPrime;
    }
    return hash == 0U ? kFnvPrime : hash;
}

[[nodiscard]] std::uint64_t HashBytes(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = kFnvOffset;
    for (const std::uint8_t value : bytes) {
        hash ^= value;
        hash *= kFnvPrime;
    }
    return hash == 0U ? kFnvPrime : hash;
}

[[nodiscard]] bool UsesLegacyFbxUpAxis(const SkeletalMeshBounds& bounds) noexcept {
    const float tolerance = std::max(0.001F, std::abs(bounds.extents.y) * 0.01F);
    return bounds.center.y < -tolerance &&
        bounds.center.y + bounds.extents.y <= tolerance;
}

[[nodiscard]] double SignedVolumeSix(const SkeletalMeshLod& lod) noexcept {
    double volume = 0.0;
    for (std::size_t index = 0U; index + 2U < lod.indices.size(); index += 3U) {
        const std::uint32_t i0 = lod.indices[index];
        const std::uint32_t i1 = lod.indices[index + 1U];
        const std::uint32_t i2 = lod.indices[index + 2U];
        if (i0 >= lod.vertices.size() || i1 >= lod.vertices.size() || i2 >= lod.vertices.size()) {
            return 0.0;
        }
        const kb::math::Vec3& a = lod.vertices[i0].position;
        const kb::math::Vec3& b = lod.vertices[i1].position;
        const kb::math::Vec3& c = lod.vertices[i2].position;
        volume += static_cast<double>(a.x) *
                (static_cast<double>(b.y) * c.z - static_cast<double>(b.z) * c.y) -
            static_cast<double>(a.y) *
                (static_cast<double>(b.x) * c.z - static_cast<double>(b.z) * c.x) +
            static_cast<double>(a.z) *
                (static_cast<double>(b.x) * c.y - static_cast<double>(b.y) * c.x);
    }
    return volume;
}

void NormalizeLegacyFbxWinding(SkeletalMeshAsset& asset) {
    if (!UsesLegacyFbxUpAxis(asset.conservativeBounds)) return;

    const double boundsVolume = 8.0 * static_cast<double>(asset.conservativeBounds.extents.x) *
        static_cast<double>(asset.conservativeBounds.extents.y) *
        static_cast<double>(asset.conservativeBounds.extents.z);
    const double negativeThreshold = -std::max(1.0e-9, boundsVolume * 1.0e-6) * 6.0;
    std::vector<bool> normalizedLods(asset.lods.size(), false);
    for (std::size_t lodIndex = 0U; lodIndex < asset.lods.size(); ++lodIndex) {
        SkeletalMeshLod& lod = asset.lods[lodIndex];
        if (SignedVolumeSix(lod) >= negativeThreshold) continue;

        for (std::size_t index = 0U; index + 2U < lod.indices.size(); index += 3U) {
            std::swap(lod.indices[index + 1U], lod.indices[index + 2U]);
        }
        for (SkeletalMeshVertex& vertex : lod.vertices) {
            vertex.normal.x = -vertex.normal.x;
            vertex.normal.y = -vertex.normal.y;
            vertex.normal.z = -vertex.normal.z;
            vertex.tangent.w = -vertex.tangent.w;
        }
        normalizedLods[lodIndex] = true;
    }
    for (SkeletalMeshMorphTarget& morph : asset.morphTargets) {
        if (morph.lodIndex >= normalizedLods.size() || !normalizedLods[morph.lodIndex]) continue;
        for (SkeletalMeshMorphDelta& delta : morph.deltas) {
            delta.normalDelta.x = -delta.normalDelta.x;
            delta.normalDelta.y = -delta.normalDelta.y;
            delta.normalDelta.z = -delta.normalDelta.z;
        }
    }
}

[[nodiscard]] std::optional<std::filesystem::path> DerivedDataPath(
    const std::filesystem::path& sourcePath,
    std::uint64_t sourceContentHash) {
    if (sourceContentHash == 0U) return std::nullopt;
    std::filesystem::path cursor = sourcePath.parent_path();
    while (!cursor.empty()) {
        std::string name = cursor.filename().string();
        std::ranges::transform(name, name.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (name == "assets") {
            return cursor.parent_path() / "Saved" / "Cache" / "SkeletalMesh" /
                (std::to_string(sourceContentHash) + ".kbskeletalmesh.ddc");
        }
        const std::filesystem::path parent = cursor.parent_path();
        if (parent == cursor) break;
        cursor = parent;
    }
    return std::nullopt;
}

template <typename T>
void WriteVectorRaw(std::vector<std::uint8_t>& output, const std::vector<T>& values) {
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(values.size()));
    if (!values.empty()) {
        SceneAssetBinaryIO::WriteRaw(output, values.data(), values.size() * sizeof(T));
    }
}

template <typename T>
[[nodiscard]] bool ReadVectorRaw(
    SceneAssetBinaryIO::ByteReader& reader,
    std::vector<T>& values,
    std::uint32_t maximumCount) {
    std::uint32_t count = 0U;
    if (!reader.ReadUInt32(count) || count > maximumCount) return false;
    values.resize(count);
    return count == 0U || reader.ReadRaw(values.data(), values.size() * sizeof(T));
}

void WriteBounds(std::vector<std::uint8_t>& output, const SkeletalMeshBounds& bounds) {
    SceneAssetBinaryIO::WriteFloat(output, bounds.center.x);
    SceneAssetBinaryIO::WriteFloat(output, bounds.center.y);
    SceneAssetBinaryIO::WriteFloat(output, bounds.center.z);
    SceneAssetBinaryIO::WriteFloat(output, bounds.extents.x);
    SceneAssetBinaryIO::WriteFloat(output, bounds.extents.y);
    SceneAssetBinaryIO::WriteFloat(output, bounds.extents.z);
}

[[nodiscard]] bool ReadBounds(SceneAssetBinaryIO::ByteReader& reader, SkeletalMeshBounds& bounds) {
    return reader.ReadFloat(bounds.center.x) && reader.ReadFloat(bounds.center.y) &&
        reader.ReadFloat(bounds.center.z) && reader.ReadFloat(bounds.extents.x) &&
        reader.ReadFloat(bounds.extents.y) && reader.ReadFloat(bounds.extents.z);
}

[[nodiscard]] std::vector<std::uint8_t> EncodeDerivedData(
    std::uint64_t sourceContentHash,
    const SkeletalMeshAsset& asset) {
    std::vector<std::uint8_t> output;
    std::size_t reserveBytes = 256U;
    for (const SkeletalMeshLod& lod : asset.lods) {
        reserveBytes += lod.vertices.size() * sizeof(SkeletalMeshVertex) +
            lod.indices.size() * sizeof(std::uint32_t);
    }
    output.reserve(reserveBytes);
    SceneAssetBinaryIO::WriteRaw(output, kDerivedMagic.data(), kDerivedMagic.size());
    SceneAssetBinaryIO::WriteUInt32(output, kDerivedVersion);
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(sizeof(SkeletalMeshVertex)));
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(sizeof(SkeletalMeshBoneBounds)));
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(sizeof(SkeletalMeshMorphDelta)));
    SceneAssetBinaryIO::WriteUInt64(output, sourceContentHash);
    SceneAssetBinaryIO::WriteUInt64(output, asset.skeletonAssetId);
    SceneAssetBinaryIO::WriteUInt64(output, asset.skeletonCompatibilitySignature);
    WriteBounds(output, asset.conservativeBounds);
    WriteBounds(output, asset.fixedBounds);
    SceneAssetBinaryIO::WriteUInt8(output, static_cast<std::uint8_t>(asset.boundsMode));
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(asset.lods.size()));
    for (const SkeletalMeshLod& lod : asset.lods) {
        SceneAssetBinaryIO::WriteFloat(output, lod.minScreenCoverage);
        WriteVectorRaw(output, lod.vertices);
        WriteVectorRaw(output, lod.indices);
        SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(lod.sections.size()));
        for (const SkeletalMeshSection& section : lod.sections) {
            SceneAssetBinaryIO::WriteUInt32(output, section.firstIndex);
            SceneAssetBinaryIO::WriteUInt32(output, section.indexCount);
            SceneAssetBinaryIO::WriteUInt64(output, section.materialAssetId);
            WriteVectorRaw(output, section.boneMap);
        }
        WriteVectorRaw(output, lod.requiredBones);
        WriteVectorRaw(output, lod.boneBounds);
    }
    SceneAssetBinaryIO::WriteUInt32(output, static_cast<std::uint32_t>(asset.morphTargets.size()));
    for (const SkeletalMeshMorphTarget& morph : asset.morphTargets) {
        SceneAssetBinaryIO::WriteString(output, morph.name);
        SceneAssetBinaryIO::WriteUInt32(output, morph.lodIndex);
        WriteVectorRaw(output, morph.deltas);
    }
    SceneAssetBinaryIO::WriteUInt64(output, HashBytes(output));
    return output;
}

[[nodiscard]] std::optional<SkeletalMeshAsset> DecodeDerivedData(
    std::vector<std::uint8_t> bytes,
    std::uint64_t expectedContentHash) {
    if (bytes.size() < sizeof(std::uint64_t)) return std::nullopt;
    std::uint64_t storedChecksum = 0U;
    const std::size_t checksumOffset = bytes.size() - sizeof(std::uint64_t);
    for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
        storedChecksum |= static_cast<std::uint64_t>(bytes[checksumOffset + byte]) << (byte * 8U);
    }
    if (storedChecksum != HashBytes(std::span<const std::uint8_t>{bytes.data(), checksumOffset})) {
        return std::nullopt;
    }
    bytes.resize(checksumOffset);
    SceneAssetBinaryIO::ByteReader reader{std::move(bytes)};
    std::array<std::uint8_t, kDerivedMagic.size()> magic{};
    std::uint32_t version = 0U;
    std::uint32_t vertexSize = 0U;
    std::uint32_t boneBoundsSize = 0U;
    std::uint32_t morphDeltaSize = 0U;
    std::uint64_t contentHash = 0U;
    if (!reader.ReadRaw(magic.data(), magic.size()) || magic != kDerivedMagic ||
        !reader.ReadUInt32(version) || version != kDerivedVersion ||
        !reader.ReadUInt32(vertexSize) || vertexSize != sizeof(SkeletalMeshVertex) ||
        !reader.ReadUInt32(boneBoundsSize) || boneBoundsSize != sizeof(SkeletalMeshBoneBounds) ||
        !reader.ReadUInt32(morphDeltaSize) || morphDeltaSize != sizeof(SkeletalMeshMorphDelta) ||
        !reader.ReadUInt64(contentHash) || contentHash != expectedContentHash) {
        return std::nullopt;
    }

    SkeletalMeshAsset asset{};
    std::uint8_t boundsMode = 0U;
    std::uint32_t lodCount = 0U;
    if (!reader.ReadUInt64(asset.skeletonAssetId) ||
        !reader.ReadUInt64(asset.skeletonCompatibilitySignature) ||
        !ReadBounds(reader, asset.conservativeBounds) || !ReadBounds(reader, asset.fixedBounds) ||
        !reader.ReadUInt8(boundsMode) || boundsMode > static_cast<std::uint8_t>(SkeletalMeshBoundsMode::Fixed) ||
        !reader.ReadUInt32(lodCount) || lodCount > kMaxDerivedLods) {
        return std::nullopt;
    }
    asset.boundsMode = static_cast<SkeletalMeshBoundsMode>(boundsMode);
    asset.lods.resize(lodCount);
    for (SkeletalMeshLod& lod : asset.lods) {
        std::uint32_t sectionCount = 0U;
        if (!reader.ReadFloat(lod.minScreenCoverage) ||
            !ReadVectorRaw(reader, lod.vertices, kMaxDerivedVertices) ||
            !ReadVectorRaw(reader, lod.indices, kMaxDerivedIndices) ||
            !reader.ReadUInt32(sectionCount) || sectionCount > kMaxDerivedSections) {
            return std::nullopt;
        }
        lod.sections.resize(sectionCount);
        for (SkeletalMeshSection& section : lod.sections) {
            if (!reader.ReadUInt32(section.firstIndex) || !reader.ReadUInt32(section.indexCount) ||
                !reader.ReadUInt64(section.materialAssetId) ||
                !ReadVectorRaw(reader, section.boneMap, kMaxDerivedBones)) {
                return std::nullopt;
            }
        }
        if (!ReadVectorRaw(reader, lod.requiredBones, kMaxDerivedBones) ||
            !ReadVectorRaw(reader, lod.boneBounds, kMaxDerivedBones)) {
            return std::nullopt;
        }
    }
    std::uint32_t morphCount = 0U;
    if (!reader.ReadUInt32(morphCount) || morphCount > kMaxDerivedMorphTargets) return std::nullopt;
    asset.morphTargets.resize(morphCount);
    for (SkeletalMeshMorphTarget& morph : asset.morphTargets) {
        if (!reader.ReadString(morph.name) || !reader.ReadUInt32(morph.lodIndex) ||
            !ReadVectorRaw(reader, morph.deltas, kMaxDerivedMorphDeltas)) {
            return std::nullopt;
        }
    }
    if (!reader.Exhausted()) return std::nullopt;
    // The cache checksum covers a payload emitted only after canonical validation. Repeating the
    // full semantic vertex/index scan here would turn a cache hit back into the original stall.
    return asset;
}

[[nodiscard]] std::optional<std::string> Read(const std::filesystem::path& path) {
    const auto bytes = SceneAssetBinaryIO::ReadAllBytes(path);
    return bytes.empty() ? std::nullopt : std::optional<std::string>{ std::string{ reinterpret_cast<const char*>(bytes.data()), bytes.size() } };
}
[[nodiscard]] bool Write(const std::filesystem::path& path, const std::string& text) {
    return SceneAssetBinaryIO::WriteBytesAtomically(path, { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}

class RecordCursor final {
public:
    explicit RecordCursor(std::string_view line) noexcept : remaining_(line) {}

    [[nodiscard]] bool ReadToken(std::string_view& output) noexcept {
        SkipWhitespace();
        if (remaining_.empty() || remaining_.front() == '#') return false;
        std::size_t length = 0U;
        while (length < remaining_.size() && remaining_[length] != ' ' && remaining_[length] != '\t' &&
               remaining_[length] != '\r' && remaining_[length] != '#') {
            ++length;
        }
        if (length == 0U) return false;
        output = remaining_.substr(0U, length);
        remaining_.remove_prefix(length);
        return true;
    }

    template <typename T>
    [[nodiscard]] bool ReadUnsigned(T& output) noexcept {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
        std::string_view token;
        if (!ReadToken(token)) return false;
        T value{};
        const std::from_chars_result parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) return false;
        output = value;
        return true;
    }

    [[nodiscard]] bool ReadFloat(float& output) noexcept {
        std::string_view token;
        if (!ReadToken(token)) return false;
#if defined(__APPLE__)
        double parsedValue = 0.0;
        if (!kb::library::TryParseDouble(token, parsedValue) || !std::isfinite(parsedValue) ||
            std::fabs(parsedValue) > static_cast<double>(std::numeric_limits<float>::max())) {
            return false;
        }
        const float value = static_cast<float>(parsedValue);
#else
        float value = 0.0F;
        const std::from_chars_result parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || !std::isfinite(value)) return false;
#endif
        output = value;
        return true;
    }

    [[nodiscard]] bool ReadQuoted(std::string& output) {
        SkipWhitespace();
        if (remaining_.empty() || remaining_.front() != '"') return false;
        remaining_.remove_prefix(1U);
        std::string value;
        while (!remaining_.empty()) {
            const char character = remaining_.front();
            remaining_.remove_prefix(1U);
            if (character == '"') {
                output = std::move(value);
                return true;
            }
            if (character == '\\') {
                if (remaining_.empty()) return false;
                value.push_back(remaining_.front());
                remaining_.remove_prefix(1U);
            } else {
                value.push_back(character);
            }
        }
        return false;
    }

    [[nodiscard]] bool End() noexcept {
        SkipWhitespace();
        return remaining_.empty() || remaining_.front() == '#';
    }

private:
    void SkipWhitespace() noexcept {
        while (!remaining_.empty() &&
               (remaining_.front() == ' ' || remaining_.front() == '\t' || remaining_.front() == '\r')) {
            remaining_.remove_prefix(1U);
        }
    }

    std::string_view remaining_;
};
} // namespace

std::optional<SkeletalMeshAssetBinding> SkeletalMeshAssetIO::LoadBinding(
    const std::filesystem::path& path,
    std::string* error) {
    const auto fail = [error](std::string message) -> std::optional<SkeletalMeshAssetBinding> {
        if (error != nullptr) *error = std::move(message);
        return std::nullopt;
    };
    if (error != nullptr) error->clear();
    if (path.extension() != kSkeletalMeshAssetExtension) {
        return fail("Skeletal mesh asset has an unexpected file extension.");
    }

    std::ifstream input{path, std::ios::in | std::ios::binary};
    if (!input.is_open()) {
        return fail("Skeletal mesh asset could not be read.");
    }

    bool schemaRead = false;
    std::string line;
    while (std::getline(input, line)) {
        RecordCursor in{line};
        std::string_view command;
        if (!in.ReadToken(command)) continue;
        if (!schemaRead) {
            const asset_io::TextAssetHeaderStatus header = asset_io::ParseTextAssetHeader(
                line, kSkeletalMeshAssetType, kSkeletalMeshAssetSchemaVersion);
            if (header == asset_io::TextAssetHeaderStatus::Invalid) {
                return fail("Skeletal mesh asset has an invalid schema header.");
            }
            schemaRead = true;
            if (header == asset_io::TextAssetHeaderStatus::Current) continue;
        }
        if (command != "skeleton") {
            return fail("Skeletal mesh asset is missing its skeleton binding record.");
        }
        SkeletalMeshAssetBinding binding{};
        if (!in.ReadUnsigned(binding.skeletonAssetId) ||
            !in.ReadUnsigned(binding.skeletonCompatibilitySignature) || !in.End() ||
            binding.skeletonAssetId == 0U || binding.skeletonCompatibilitySignature == 0U) {
            return fail("Skeletal mesh asset has an invalid skeleton binding record.");
        }
        return binding;
    }
    return fail("Skeletal mesh asset is missing its skeleton binding record.");
}

std::optional<SkeletalMeshAsset> SkeletalMeshAssetIO::Load(
    const std::filesystem::path& path,
    std::string* error) {
    const auto fail = [error](std::string message) -> std::optional<SkeletalMeshAsset> {
        if (error != nullptr) *error = std::move(message);
        return std::nullopt;
    };
    if (error != nullptr) error->clear();
    if (path.extension() != kSkeletalMeshAssetExtension) {
        return fail("Skeletal mesh asset has an unexpected file extension.");
    }
    const auto text = Read(path);
    if (!text) return fail("Skeletal mesh asset could not be read.");
    return Load(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(text->data()), text->size() }, error);
}

std::optional<SkeletalMeshAsset> SkeletalMeshAssetIO::Load(
    std::span<const std::uint8_t> bytes,
    std::string* error) {
    const auto fail = [error](std::string message) -> std::optional<SkeletalMeshAsset> {
        if (error != nullptr) *error = std::move(message);
        return std::nullopt;
    };
    if (bytes.empty()) {
        return fail("Skeletal mesh asset could not be read.");
    }
    const std::string text{ reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    SkeletalMeshAsset asset{}; std::vector<SkeletalMeshMorphTarget> morphs;
    std::string_view remaining{ text };
    bool schemaRead = false;
    bool fixedBoundsRead = false;
    std::size_t lineNumber = 0U;
    while (!remaining.empty()) {
        const std::size_t lineEnd = remaining.find('\n');
        const std::string_view line = lineEnd == std::string_view::npos
            ? remaining : remaining.substr(0U, lineEnd);
        remaining = lineEnd == std::string_view::npos
            ? std::string_view{} : remaining.substr(lineEnd + 1U);
        ++lineNumber;
        RecordCursor in{ line };
        std::string_view cmd;
        if (!in.ReadToken(cmd)) continue;
        if (error != nullptr) {
            *error = "Skeletal mesh asset has an invalid record at line " +
                std::to_string(lineNumber) + ".";
        }
        if (!schemaRead) {
            const asset_io::TextAssetHeaderStatus header =
                asset_io::ParseTextAssetHeader(
                    line, kSkeletalMeshAssetType, kSkeletalMeshAssetSchemaVersion);
            if (header == asset_io::TextAssetHeaderStatus::Invalid) return std::nullopt;
            schemaRead = true;
            if (header == asset_io::TextAssetHeaderStatus::Current) continue;
        }
        if (cmd == "skeleton") { if (!in.ReadUnsigned(asset.skeletonAssetId) || !in.ReadUnsigned(asset.skeletonCompatibilitySignature) || !in.End()) return std::nullopt; }
        else if (cmd == "bounds") { auto& b=asset.conservativeBounds; if (!in.ReadFloat(b.center.x)||!in.ReadFloat(b.center.y)||!in.ReadFloat(b.center.z)||!in.ReadFloat(b.extents.x)||!in.ReadFloat(b.extents.y)||!in.ReadFloat(b.extents.z)||!in.End()) return std::nullopt; }
        else if (cmd == "fixedBounds") { auto& b=asset.fixedBounds; if (!in.ReadFloat(b.center.x)||!in.ReadFloat(b.center.y)||!in.ReadFloat(b.center.z)||!in.ReadFloat(b.extents.x)||!in.ReadFloat(b.extents.y)||!in.ReadFloat(b.extents.z)||!in.End()) return std::nullopt; fixedBoundsRead = true; }
        else if (cmd == "boundsMode") { std::string_view mode; if (!in.ReadToken(mode) || !in.End()) return std::nullopt; if (mode == "imported") asset.boundsMode = SkeletalMeshBoundsMode::ImportedConservative; else if (mode == "fixed") asset.boundsMode = SkeletalMeshBoundsMode::Fixed; else return std::nullopt; }
        else if (cmd == "lod") { SkeletalMeshLod lod{}; if (!in.ReadFloat(lod.minScreenCoverage)||!in.End()) return std::nullopt; asset.lods.push_back(std::move(lod)); }
        else if (cmd == "vertex") { std::size_t l=0; SkeletalMeshVertex v{}; if (!in.ReadUnsigned(l)||!in.ReadFloat(v.position.x)||!in.ReadFloat(v.position.y)||!in.ReadFloat(v.position.z)||!in.ReadFloat(v.normal.x)||!in.ReadFloat(v.normal.y)||!in.ReadFloat(v.normal.z)||!in.ReadFloat(v.tangent.x)||!in.ReadFloat(v.tangent.y)||!in.ReadFloat(v.tangent.z)||!in.ReadFloat(v.tangent.w)||!in.ReadFloat(v.uv[0])||!in.ReadFloat(v.uv[1])||!in.ReadUnsigned(v.jointIndices[0])||!in.ReadUnsigned(v.jointIndices[1])||!in.ReadUnsigned(v.jointIndices[2])||!in.ReadUnsigned(v.jointIndices[3])||!in.ReadFloat(v.jointWeights[0])||!in.ReadFloat(v.jointWeights[1])||!in.ReadFloat(v.jointWeights[2])||!in.ReadFloat(v.jointWeights[3])||l>=asset.lods.size()||!in.End()) return std::nullopt; asset.lods[l].vertices.push_back(v); }
        else if (cmd == "index") { std::size_t l=0; std::uint32_t v=0; if (!in.ReadUnsigned(l)||!in.ReadUnsigned(v)||l>=asset.lods.size()||!in.End()) return std::nullopt; asset.lods[l].indices.push_back(v); }
        else if (cmd == "section") { std::size_t l=0; SkeletalMeshSection s{}; if (!in.ReadUnsigned(l)||!in.ReadUnsigned(s.firstIndex)||!in.ReadUnsigned(s.indexCount)||!in.ReadUnsigned(s.materialAssetId)||l>=asset.lods.size()||!in.End()) return std::nullopt; asset.lods[l].sections.push_back(std::move(s)); }
        else if (cmd == "sectionBone") { std::size_t l=0,s=0; SkeletonBoneId b=0; if (!in.ReadUnsigned(l)||!in.ReadUnsigned(s)||!in.ReadUnsigned(b)||l>=asset.lods.size()||s>=asset.lods[l].sections.size()||!in.End()) return std::nullopt; asset.lods[l].sections[s].boneMap.push_back(b); }
        else if (cmd == "requiredBone") { std::size_t l=0; SkeletonBoneId b=0; if (!in.ReadUnsigned(l)||!in.ReadUnsigned(b)||l>=asset.lods.size()||!in.End()) return std::nullopt; asset.lods[l].requiredBones.push_back(b); }
        else if (cmd == "boneBounds") { std::size_t l=0; SkeletalMeshBoneBounds b{}; if (!in.ReadUnsigned(l)||!in.ReadUnsigned(b.boneId)||!in.ReadFloat(b.center.x)||!in.ReadFloat(b.center.y)||!in.ReadFloat(b.center.z)||!in.ReadFloat(b.extents.x)||!in.ReadFloat(b.extents.y)||!in.ReadFloat(b.extents.z)||l>=asset.lods.size()||!in.End()) return std::nullopt; asset.lods[l].boneBounds.push_back(b); }
        else if (cmd == "morph") { SkeletalMeshMorphTarget m{}; if (!in.ReadQuoted(m.name)||!in.ReadUnsigned(m.lodIndex)||!in.End()) return std::nullopt; morphs.push_back(std::move(m)); }
        else if (cmd == "delta") { std::size_t m=0; SkeletalMeshMorphDelta d{}; if (!in.ReadUnsigned(m)||!in.ReadUnsigned(d.vertexIndex)||!in.ReadFloat(d.positionDelta.x)||!in.ReadFloat(d.positionDelta.y)||!in.ReadFloat(d.positionDelta.z)||!in.ReadFloat(d.normalDelta.x)||!in.ReadFloat(d.normalDelta.y)||!in.ReadFloat(d.normalDelta.z)||!in.ReadFloat(d.tangentDelta.x)||!in.ReadFloat(d.tangentDelta.y)||!in.ReadFloat(d.tangentDelta.z)||m>=morphs.size()||!in.End()) return std::nullopt; morphs[m].deltas.push_back(d); }
        else return std::nullopt;
    }
    if (!fixedBoundsRead) asset.fixedBounds = asset.conservativeBounds;
    asset.morphTargets = std::move(morphs);
    NormalizeLegacyFbxWinding(asset);
    for (SkeletalMeshLod& lod : asset.lods) {
        if (lod.boneBounds.empty()) BuildSkeletalMeshLodBoneBounds(lod);
    }
    const SkeletalMeshAssetValidationResult validation =
        ValidateSkeletalMeshAsset(asset);
    if (!validation.valid) return fail(validation.error);
    if (error != nullptr) error->clear();
    return asset;
}

std::optional<SkeletalMeshAsset> SkeletalMeshAssetIO::LoadDerivedData(
    const std::filesystem::path& sourcePath,
    std::uint64_t sourceContentHash,
    std::string* error) {
    if (error != nullptr) error->clear();
    const std::optional<std::filesystem::path> cachePath =
        DerivedDataPath(sourcePath, sourceContentHash);
    if (!cachePath.has_value()) return std::nullopt;
    std::vector<std::uint8_t> bytes = SceneAssetBinaryIO::ReadAllBytes(*cachePath);
    if (bytes.empty()) return std::nullopt;
    std::optional<SkeletalMeshAsset> asset =
        DecodeDerivedData(std::move(bytes), sourceContentHash);
    if (!asset.has_value() && error != nullptr) {
        *error = "Skeletal mesh derived-data cache is invalid and will be rebuilt.";
    }
    return asset;
}

bool SkeletalMeshAssetIO::SaveDerivedData(
    const std::filesystem::path& sourcePath,
    std::uint64_t sourceContentHash,
    const SkeletalMeshAsset& asset) {
    const std::optional<std::filesystem::path> cachePath =
        DerivedDataPath(sourcePath, sourceContentHash);
    // Callers publish this cache only after the canonical Load/Save validation succeeds. Repeating
    // the full vertex/index scan here would double the one-time cold-import cost; cache reads still
    // validate before exposing the payload and discard any damaged entry.
    if (!cachePath.has_value()) return false;
    const std::vector<std::uint8_t> bytes = EncodeDerivedData(sourceContentHash, asset);
    return SceneAssetBinaryIO::WriteBytesAtomically(*cachePath, bytes);
}

bool SkeletalMeshAssetIO::Save(const std::filesystem::path& path, const SkeletalMeshAsset& asset) {
    if (path.extension()!=kSkeletalMeshAssetExtension || !ValidateSkeletalMeshAsset(asset).valid) return false;
    std::ostringstream out; out.imbue(std::locale::classic()); out<<std::setprecision(std::numeric_limits<float>::max_digits10);
    out << asset_io::TextAssetHeader(
        kSkeletalMeshAssetType, kSkeletalMeshAssetSchemaVersion);
    out<<"skeleton "<<asset.skeletonAssetId<<' '<<asset.skeletonCompatibilitySignature<<"\nbounds "<<asset.conservativeBounds.center.x<<' '<<asset.conservativeBounds.center.y<<' '<<asset.conservativeBounds.center.z<<' '<<asset.conservativeBounds.extents.x<<' '<<asset.conservativeBounds.extents.y<<' '<<asset.conservativeBounds.extents.z<<"\nfixedBounds "<<asset.fixedBounds.center.x<<' '<<asset.fixedBounds.center.y<<' '<<asset.fixedBounds.center.z<<' '<<asset.fixedBounds.extents.x<<' '<<asset.fixedBounds.extents.y<<' '<<asset.fixedBounds.extents.z<<"\nboundsMode "<<(asset.boundsMode == SkeletalMeshBoundsMode::Fixed ? "fixed" : "imported")<<'\n';
    for(std::size_t l=0;l<asset.lods.size();++l){const auto& lod=asset.lods[l];out<<"lod "<<lod.minScreenCoverage<<'\n';for(const auto& v:lod.vertices)out<<"vertex "<<l<<' '<<v.position.x<<' '<<v.position.y<<' '<<v.position.z<<' '<<v.normal.x<<' '<<v.normal.y<<' '<<v.normal.z<<' '<<v.tangent.x<<' '<<v.tangent.y<<' '<<v.tangent.z<<' '<<v.tangent.w<<' '<<v.uv[0]<<' '<<v.uv[1]<<' '<<v.jointIndices[0]<<' '<<v.jointIndices[1]<<' '<<v.jointIndices[2]<<' '<<v.jointIndices[3]<<' '<<v.jointWeights[0]<<' '<<v.jointWeights[1]<<' '<<v.jointWeights[2]<<' '<<v.jointWeights[3]<<'\n';for(auto i:lod.indices)out<<"index "<<l<<' '<<i<<'\n';for(std::size_t s=0;s<lod.sections.size();++s){const auto& x=lod.sections[s];out<<"section "<<l<<' '<<x.firstIndex<<' '<<x.indexCount<<' '<<x.materialAssetId<<'\n';for(auto b:x.boneMap)out<<"sectionBone "<<l<<' '<<s<<' '<<b<<'\n';}for(auto b:lod.requiredBones)out<<"requiredBone "<<l<<' '<<b<<'\n';for(const auto& b:lod.boneBounds)out<<"boneBounds "<<l<<' '<<b.boneId<<' '<<b.center.x<<' '<<b.center.y<<' '<<b.center.z<<' '<<b.extents.x<<' '<<b.extents.y<<' '<<b.extents.z<<'\n';}
    for(std::size_t m=0;m<asset.morphTargets.size();++m){const auto& x=asset.morphTargets[m];out<<"morph "<<std::quoted(x.name)<<' '<<x.lodIndex<<'\n';for(const auto& d:x.deltas)out<<"delta "<<m<<' '<<d.vertexIndex<<' '<<d.positionDelta.x<<' '<<d.positionDelta.y<<' '<<d.positionDelta.z<<' '<<d.normalDelta.x<<' '<<d.normalDelta.y<<' '<<d.normalDelta.z<<' '<<d.tangentDelta.x<<' '<<d.tangentDelta.y<<' '<<d.tangentDelta.z<<'\n';}
    const std::string text = out.str();
    if (!Write(path, text)) return false;
    // Derived data is disposable and keyed by the canonical text bytes. Failure to create it must
    // not turn a successful source save/import into data loss; the loader will rebuild it later.
    static_cast<void>(SaveDerivedData(path, HashBytes(text), asset));
    return true;
}
} // namespace kb::scene
