#include "engine/scene/SkeletalMeshAssetIO.hpp"

#include "engine/library/EngineLibraryParsing.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/VersionedTextAssetHeader.hpp"

#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace kb::scene {
namespace {
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
    SkeletalMeshAsset asset{}; std::vector<SkeletalMeshMorphTarget> morphs;
    std::string_view remaining{ *text };
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
    for (SkeletalMeshLod& lod : asset.lods) {
        if (lod.boneBounds.empty()) BuildSkeletalMeshLodBoneBounds(lod);
    }
    const SkeletalMeshAssetValidationResult validation =
        ValidateSkeletalMeshAsset(asset);
    if (!validation.valid) return fail(validation.error);
    if (error != nullptr) error->clear();
    return asset;
}

bool SkeletalMeshAssetIO::Save(const std::filesystem::path& path, const SkeletalMeshAsset& asset) {
    if (path.extension()!=kSkeletalMeshAssetExtension || !ValidateSkeletalMeshAsset(asset).valid) return false;
    std::ostringstream out; out.imbue(std::locale::classic()); out<<std::setprecision(std::numeric_limits<float>::max_digits10);
    out << asset_io::TextAssetHeader(
        kSkeletalMeshAssetType, kSkeletalMeshAssetSchemaVersion);
    out<<"skeleton "<<asset.skeletonAssetId<<' '<<asset.skeletonCompatibilitySignature<<"\nbounds "<<asset.conservativeBounds.center.x<<' '<<asset.conservativeBounds.center.y<<' '<<asset.conservativeBounds.center.z<<' '<<asset.conservativeBounds.extents.x<<' '<<asset.conservativeBounds.extents.y<<' '<<asset.conservativeBounds.extents.z<<"\nfixedBounds "<<asset.fixedBounds.center.x<<' '<<asset.fixedBounds.center.y<<' '<<asset.fixedBounds.center.z<<' '<<asset.fixedBounds.extents.x<<' '<<asset.fixedBounds.extents.y<<' '<<asset.fixedBounds.extents.z<<"\nboundsMode "<<(asset.boundsMode == SkeletalMeshBoundsMode::Fixed ? "fixed" : "imported")<<'\n';
    for(std::size_t l=0;l<asset.lods.size();++l){const auto& lod=asset.lods[l];out<<"lod "<<lod.minScreenCoverage<<'\n';for(const auto& v:lod.vertices)out<<"vertex "<<l<<' '<<v.position.x<<' '<<v.position.y<<' '<<v.position.z<<' '<<v.normal.x<<' '<<v.normal.y<<' '<<v.normal.z<<' '<<v.tangent.x<<' '<<v.tangent.y<<' '<<v.tangent.z<<' '<<v.tangent.w<<' '<<v.uv[0]<<' '<<v.uv[1]<<' '<<v.jointIndices[0]<<' '<<v.jointIndices[1]<<' '<<v.jointIndices[2]<<' '<<v.jointIndices[3]<<' '<<v.jointWeights[0]<<' '<<v.jointWeights[1]<<' '<<v.jointWeights[2]<<' '<<v.jointWeights[3]<<'\n';for(auto i:lod.indices)out<<"index "<<l<<' '<<i<<'\n';for(std::size_t s=0;s<lod.sections.size();++s){const auto& x=lod.sections[s];out<<"section "<<l<<' '<<x.firstIndex<<' '<<x.indexCount<<' '<<x.materialAssetId<<'\n';for(auto b:x.boneMap)out<<"sectionBone "<<l<<' '<<s<<' '<<b<<'\n';}for(auto b:lod.requiredBones)out<<"requiredBone "<<l<<' '<<b<<'\n';for(const auto& b:lod.boneBounds)out<<"boneBounds "<<l<<' '<<b.boneId<<' '<<b.center.x<<' '<<b.center.y<<' '<<b.center.z<<' '<<b.extents.x<<' '<<b.extents.y<<' '<<b.extents.z<<'\n';}
    for(std::size_t m=0;m<asset.morphTargets.size();++m){const auto& x=asset.morphTargets[m];out<<"morph "<<std::quoted(x.name)<<' '<<x.lodIndex<<'\n';for(const auto& d:x.deltas)out<<"delta "<<m<<' '<<d.vertexIndex<<' '<<d.positionDelta.x<<' '<<d.positionDelta.y<<' '<<d.positionDelta.z<<' '<<d.normalDelta.x<<' '<<d.normalDelta.y<<' '<<d.normalDelta.z<<' '<<d.tangentDelta.x<<' '<<d.tangentDelta.y<<' '<<d.tangentDelta.z<<'\n';}
    return Write(path,out.str());
}
} // namespace kb::scene
