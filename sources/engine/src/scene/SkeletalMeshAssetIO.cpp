#include "engine/scene/SkeletalMeshAssetIO.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/VersionedTextAssetHeader.hpp"

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace kb::scene {
namespace {
[[nodiscard]] bool End(std::istringstream& in) { in >> std::ws; return in.eof() || in.peek() == '#'; }
[[nodiscard]] std::optional<std::string> Read(const std::filesystem::path& path) {
    const auto bytes = SceneAssetBinaryIO::ReadAllBytes(path);
    return bytes.empty() ? std::nullopt : std::optional<std::string>{ std::string{ reinterpret_cast<const char*>(bytes.data()), bytes.size() } };
}
[[nodiscard]] bool Write(const std::filesystem::path& path, const std::string& text) {
    return SceneAssetBinaryIO::WriteBytesAtomically(path, { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}
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
    std::istringstream file{ *text }; file.imbue(std::locale::classic()); std::string line;
    bool schemaRead = false;
    bool fixedBoundsRead = false;
    std::size_t lineNumber = 0U;
    while (std::getline(file, line)) {
        ++lineNumber;
        std::istringstream in{ line }; in.imbue(std::locale::classic()); std::string cmd;
        if (!(in >> cmd) || cmd.starts_with('#')) continue;
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
        if (cmd == "skeleton") { if (!(in >> asset.skeletonAssetId >> asset.skeletonCompatibilitySignature) || !End(in)) return std::nullopt; }
        else if (cmd == "bounds") { auto& b=asset.conservativeBounds; if (!(in>>b.center.x>>b.center.y>>b.center.z>>b.extents.x>>b.extents.y>>b.extents.z)||!End(in)) return std::nullopt; }
        else if (cmd == "fixedBounds") { auto& b=asset.fixedBounds; if (!(in>>b.center.x>>b.center.y>>b.center.z>>b.extents.x>>b.extents.y>>b.extents.z)||!End(in)) return std::nullopt; fixedBoundsRead = true; }
        else if (cmd == "boundsMode") { std::string mode; if (!(in >> mode) || !End(in)) return std::nullopt; if (mode == "imported") asset.boundsMode = SkeletalMeshBoundsMode::ImportedConservative; else if (mode == "fixed") asset.boundsMode = SkeletalMeshBoundsMode::Fixed; else return std::nullopt; }
        else if (cmd == "lod") { SkeletalMeshLod lod{}; if (!(in>>lod.minScreenCoverage)||!End(in)) return std::nullopt; asset.lods.push_back(std::move(lod)); }
        else if (cmd == "vertex") { std::size_t l=0; SkeletalMeshVertex v{}; if (!(in>>l>>v.position.x>>v.position.y>>v.position.z>>v.normal.x>>v.normal.y>>v.normal.z>>v.tangent.x>>v.tangent.y>>v.tangent.z>>v.tangent.w>>v.uv[0]>>v.uv[1]>>v.jointIndices[0]>>v.jointIndices[1]>>v.jointIndices[2]>>v.jointIndices[3]>>v.jointWeights[0]>>v.jointWeights[1]>>v.jointWeights[2]>>v.jointWeights[3])||l>=asset.lods.size()||!End(in)) return std::nullopt; asset.lods[l].vertices.push_back(v); }
        else if (cmd == "index") { std::size_t l=0; std::uint32_t v=0; if (!(in>>l>>v)||l>=asset.lods.size()||!End(in)) return std::nullopt; asset.lods[l].indices.push_back(v); }
        else if (cmd == "section") { std::size_t l=0; SkeletalMeshSection s{}; if (!(in>>l>>s.firstIndex>>s.indexCount>>s.materialAssetId)||l>=asset.lods.size()||!End(in)) return std::nullopt; asset.lods[l].sections.push_back(std::move(s)); }
        else if (cmd == "sectionBone") { std::size_t l=0,s=0; SkeletonBoneId b=0; if (!(in>>l>>s>>b)||l>=asset.lods.size()||s>=asset.lods[l].sections.size()||!End(in)) return std::nullopt; asset.lods[l].sections[s].boneMap.push_back(b); }
        else if (cmd == "requiredBone") { std::size_t l=0; SkeletonBoneId b=0; if (!(in>>l>>b)||l>=asset.lods.size()||!End(in)) return std::nullopt; asset.lods[l].requiredBones.push_back(b); }
        else if (cmd == "morph") { SkeletalMeshMorphTarget m{}; if (!(in>>std::quoted(m.name)>>m.lodIndex)||!End(in)) return std::nullopt; morphs.push_back(std::move(m)); }
        else if (cmd == "delta") { std::size_t m=0; SkeletalMeshMorphDelta d{}; if (!(in>>m>>d.vertexIndex>>d.positionDelta.x>>d.positionDelta.y>>d.positionDelta.z>>d.normalDelta.x>>d.normalDelta.y>>d.normalDelta.z>>d.tangentDelta.x>>d.tangentDelta.y>>d.tangentDelta.z)||m>=morphs.size()||!End(in)) return std::nullopt; morphs[m].deltas.push_back(d); }
        else return std::nullopt;
    }
    if (!fixedBoundsRead) asset.fixedBounds = asset.conservativeBounds;
    asset.morphTargets = std::move(morphs);
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
    for(std::size_t l=0;l<asset.lods.size();++l){const auto& lod=asset.lods[l];out<<"lod "<<lod.minScreenCoverage<<'\n';for(const auto& v:lod.vertices)out<<"vertex "<<l<<' '<<v.position.x<<' '<<v.position.y<<' '<<v.position.z<<' '<<v.normal.x<<' '<<v.normal.y<<' '<<v.normal.z<<' '<<v.tangent.x<<' '<<v.tangent.y<<' '<<v.tangent.z<<' '<<v.tangent.w<<' '<<v.uv[0]<<' '<<v.uv[1]<<' '<<v.jointIndices[0]<<' '<<v.jointIndices[1]<<' '<<v.jointIndices[2]<<' '<<v.jointIndices[3]<<' '<<v.jointWeights[0]<<' '<<v.jointWeights[1]<<' '<<v.jointWeights[2]<<' '<<v.jointWeights[3]<<'\n';for(auto i:lod.indices)out<<"index "<<l<<' '<<i<<'\n';for(std::size_t s=0;s<lod.sections.size();++s){const auto& x=lod.sections[s];out<<"section "<<l<<' '<<x.firstIndex<<' '<<x.indexCount<<' '<<x.materialAssetId<<'\n';for(auto b:x.boneMap)out<<"sectionBone "<<l<<' '<<s<<' '<<b<<'\n';}for(auto b:lod.requiredBones)out<<"requiredBone "<<l<<' '<<b<<'\n';}
    for(std::size_t m=0;m<asset.morphTargets.size();++m){const auto& x=asset.morphTargets[m];out<<"morph "<<std::quoted(x.name)<<' '<<x.lodIndex<<'\n';for(const auto& d:x.deltas)out<<"delta "<<m<<' '<<d.vertexIndex<<' '<<d.positionDelta.x<<' '<<d.positionDelta.y<<' '<<d.positionDelta.z<<' '<<d.normalDelta.x<<' '<<d.normalDelta.y<<' '<<d.normalDelta.z<<' '<<d.tangentDelta.x<<' '<<d.tangentDelta.y<<' '<<d.tangentDelta.z<<'\n';}
    return Write(path,out.str());
}
} // namespace kb::scene
