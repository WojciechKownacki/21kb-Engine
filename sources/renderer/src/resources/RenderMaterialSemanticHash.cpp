#include "kb/render/resources/RenderMaterialSemanticHash.hpp"

#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>

namespace kb::render {
namespace {

void HashBytes(std::uint64_t& hash, std::string_view text) noexcept {
    for (const char ch : text) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ULL;
    }
}

void RemoveEditorOnlyGraphState(RenderMaterialGraphDocument& graph) {
    StripRenderMaterialGraphEditorOnlyState(graph);
}

} // namespace

std::uint64_t RenderMaterialRuntimeSemanticHash(const RenderMaterialAssetData& material) {
    RenderMaterialAssetData semantic = material;
    RemoveEditorOnlyGraphState(semantic.graph);
    std::ranges::sort(
        semantic.graphParameterValues,
        [](const RenderMaterialGraphParameterValue& lhs, const RenderMaterialGraphParameterValue& rhs) {
            return std::tie(lhs.stableId, lhs.type, lhs.numbers, lhs.assetId, lhs.boolValue, lhs.text) <
                std::tie(rhs.stableId, rhs.type, rhs.numbers, rhs.assetId, rhs.boolValue, rhs.text);
        });
    std::ostringstream serialized;
    RenderMaterialAssetWriter::Write(serialized, semantic);
    std::uint64_t hash = 1469598103934665603ULL;
    HashBytes(hash, serialized.str());
    return hash == 0U ? 1U : hash;
}

std::uint64_t RenderMaterialShaderSemanticHash(const RenderMaterialAssetData& material) {
    std::uint64_t hash = RenderMaterialGraphShaderSemanticHash(material.graph);
    HashBytes(hash, material.materialType);
    HashBytes(hash, std::to_string(material.materialTypeVersion));
    return hash == 0U ? 1U : hash;
}

} // namespace kb::render
