#include "resources/RenderMeshObjImporter.hpp"

#include "resources/RenderMeshAssetFinalizer.hpp"
#include "resources/RenderMeshObjMaterialResolver.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <istream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {
namespace {

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct ObjVertexKey {
    int position = 0;
    int texCoord = -1;
    int normal = -1;

    [[nodiscard]] bool operator<(const ObjVertexKey& rhs) const noexcept {
        if (position != rhs.position) {
            return position < rhs.position;
        }
        if (texCoord != rhs.texCoord) {
            return texCoord < rhs.texCoord;
        }
        return normal < rhs.normal;
    }
};

struct ObjImportContext {
    std::vector<Vec3> positions;
    std::vector<Vec2> texCoords;
    std::vector<Vec3> normals;
    std::map<ObjVertexKey, std::uint32_t> vertexMap;
    std::uint32_t currentMaterialSlot = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t currentSectionStart = 0U;
};

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] std::string_view StripComment(std::string_view line) noexcept {
    const std::size_t comment = line.find('#');
    return comment == std::string_view::npos ? line : line.substr(0U, comment);
}

[[nodiscard]] bool ParseFloat(std::string_view text, float& output) noexcept {
    text = Trim(text);
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseInt(std::string_view text, int& output) noexcept {
    text = Trim(text);
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseVec3Line(std::string_view rest, Vec3& output) {
    std::istringstream stream{ std::string{ rest } };
    std::string x;
    std::string y;
    std::string z;
    return (stream >> x >> y >> z) && ParseFloat(x, output.x) && ParseFloat(y, output.y) && ParseFloat(z, output.z);
}

[[nodiscard]] bool ParseVec2Line(std::string_view rest, Vec2& output, bool flipV) {
    std::istringstream stream{ std::string{ rest } };
    std::string x;
    std::string y;
    if (!(stream >> x >> y) || !ParseFloat(x, output.x) || !ParseFloat(y, output.y)) {
        return false;
    }
    if (flipV) {
        output.y = 1.0F - output.y;
    }
    return true;
}

[[nodiscard]] int ResolveObjIndex(int index, std::size_t count) noexcept {
    if (index > 0) {
        return index - 1;
    }
    if (index < 0) {
        return static_cast<int>(count) + index;
    }
    return -1;
}

[[nodiscard]] bool ParseFaceVertex(std::string_view token, const ObjImportContext& context, ObjVertexKey& output) noexcept {
    const std::size_t firstSlash = token.find('/');
    const std::size_t secondSlash = firstSlash == std::string_view::npos ? std::string_view::npos : token.find('/', firstSlash + 1U);

    int position = 0;
    if (!ParseInt(firstSlash == std::string_view::npos ? token : token.substr(0U, firstSlash), position)) {
        return false;
    }
    output.position = ResolveObjIndex(position, context.positions.size());
    if (output.position < 0 || static_cast<std::size_t>(output.position) >= context.positions.size()) {
        return false;
    }

    if (firstSlash != std::string_view::npos && secondSlash != firstSlash + 1U) {
        int texCoord = 0;
        const std::string_view texCoordText = secondSlash == std::string_view::npos
            ? token.substr(firstSlash + 1U)
            : token.substr(firstSlash + 1U, secondSlash - firstSlash - 1U);
        if (!texCoordText.empty() && ParseInt(texCoordText, texCoord)) {
            output.texCoord = ResolveObjIndex(texCoord, context.texCoords.size());
        }
    }
    if (secondSlash != std::string_view::npos) {
        int normal = 0;
        const std::string_view normalText = token.substr(secondSlash + 1U);
        if (!normalText.empty() && ParseInt(normalText, normal)) {
            output.normal = ResolveObjIndex(normal, context.normals.size());
        }
    }
    return output.texCoord < static_cast<int>(context.texCoords.size()) && output.normal < static_cast<int>(context.normals.size());
}

void FinishSection(RenderMeshAssetData& asset, ObjImportContext& context) {
    const std::uint32_t indexCount = asset.indices32.empty()
        ? static_cast<std::uint32_t>(asset.indices16.size())
        : static_cast<std::uint32_t>(asset.indices32.size());
    if (indexCount <= context.currentSectionStart) {
        return;
    }

    asset.sections.push_back(RenderMeshSectionDesc{
        .indexStart = context.currentSectionStart,
        .indexCount = indexCount - context.currentSectionStart,
        .materialSlot = context.currentMaterialSlot,
    });
    context.currentSectionStart = indexCount;
}

[[nodiscard]] std::uint32_t AddVertex(RenderMeshAssetData& asset, ObjImportContext& context, const ObjVertexKey& key) {
    const auto existing = context.vertexMap.find(key);
    if (existing != context.vertexMap.end()) {
        return existing->second;
    }

    const Vec3& position = context.positions[static_cast<std::size_t>(key.position)];
    const Vec2 texCoord = key.texCoord >= 0 ? context.texCoords[static_cast<std::size_t>(key.texCoord)] : Vec2{};
    // A zero normal means "the source authored none"; RenderMeshAssetFinalizer derives it
    // from the geometry. A made-up constant here would shade the mesh as one flat surface.
    const Vec3 normal = key.normal >= 0 ? context.normals[static_cast<std::size_t>(key.normal)] : Vec3{};
    const std::uint32_t vertexIndex = static_cast<std::uint32_t>(asset.vertices.size());
    asset.vertices.push_back(RenderStaticMeshVertexP3N3UV2{
        .x = position.x,
        .y = position.y,
        .z = position.z,
        .nx = normal.x,
        .ny = normal.y,
        .nz = normal.z,
        .u = texCoord.x,
        .v = texCoord.y,
        // OBJ exposes a single UV channel; uv1 falls back to uv0 (MAT-73).
        .u1 = texCoord.x,
        .v1 = texCoord.y,
    });
    context.vertexMap[key] = vertexIndex;
    return vertexIndex;
}

void AppendIndex(RenderMeshAssetData& asset, std::uint32_t index) {
    asset.indices32.push_back(index);
}

[[nodiscard]] bool ParseFace(
    std::string_view rest,
    RenderMeshAssetData& asset,
    ObjImportContext& context,
    const RenderMeshObjImportDesc& desc) {
    if (context.currentMaterialSlot == std::numeric_limits<std::uint32_t>::max()) {
        context.currentMaterialSlot = RenderMeshObjMaterialResolver::EnsureMaterialSlot(asset, {}, desc);
    }

    std::istringstream stream{ std::string{ rest } };
    std::vector<std::uint32_t> faceIndices;
    std::string token;
    while (stream >> token) {
        ObjVertexKey key{};
        if (!ParseFaceVertex(token, context, key)) {
            return false;
        }
        faceIndices.push_back(AddVertex(asset, context, key));
    }
    if (faceIndices.size() < 3U) {
        return false;
    }

    for (std::size_t index = 1U; index + 1U < faceIndices.size(); ++index) {
        AppendIndex(asset, faceIndices[0]);
        AppendIndex(asset, faceIndices[index]);
        AppendIndex(asset, faceIndices[index + 1U]);
    }
    return true;
}

} // namespace

std::optional<RenderMeshAssetData> RenderMeshObjImporter::Load(const std::filesystem::path& path, const RenderMeshObjImportDesc& desc) {
    std::ifstream input{ path };
    if (!input) {
        return std::nullopt;
    }
    return Load(input, desc);
}

std::optional<RenderMeshAssetData> RenderMeshObjImporter::Load(std::istream& input, const RenderMeshObjImportDesc& desc) {
    RenderMeshAssetData asset{};
    ObjImportContext context{};

    std::string line;
    while (std::getline(input, line)) {
        std::string_view trimmed = Trim(StripComment(line));
        if (trimmed.empty()) {
            continue;
        }

        const std::size_t keywordEnd = trimmed.find_first_of(" \t");
        const std::string_view keyword = keywordEnd == std::string_view::npos ? trimmed : trimmed.substr(0U, keywordEnd);
        const std::string_view rest = keywordEnd == std::string_view::npos ? std::string_view{} : Trim(trimmed.substr(keywordEnd + 1U));
        if (keyword == "v") {
            Vec3 position{};
            if (!ParseVec3Line(rest, position)) {
                return std::nullopt;
            }
            context.positions.push_back(position);
        } else if (keyword == "vt") {
            Vec2 texCoord{};
            if (!ParseVec2Line(rest, texCoord, desc.flipV)) {
                return std::nullopt;
            }
            context.texCoords.push_back(texCoord);
        } else if (keyword == "vn") {
            Vec3 normal{};
            if (!ParseVec3Line(rest, normal)) {
                return std::nullopt;
            }
            context.normals.push_back(normal);
        } else if (keyword == "usemtl") {
            FinishSection(asset, context);
            context.currentMaterialSlot = RenderMeshObjMaterialResolver::EnsureMaterialSlot(asset, rest, desc);
        } else if (keyword == "f") {
            if (!ParseFace(rest, asset, context, desc)) {
                return std::nullopt;
            }
        }
    }

    FinishSection(asset, context);
    if (asset.vertices.empty() || asset.indices32.empty()) {
        return std::nullopt;
    }

    if (!RenderMeshAssetFinalizer::Finalize(asset)) {
        return std::nullopt;
    }
    return asset;
}

} // namespace kb::render
