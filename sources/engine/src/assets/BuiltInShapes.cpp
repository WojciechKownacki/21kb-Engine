#include "engine/assets/BuiltInShapes.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace kb::assets {
namespace {

// The source text names the shape and the generator version: bumping the version changes the
// content hash, so cooked packages pick up a changed generator.
constexpr std::array kShapes{
    BuiltInShapeDesc{ BuiltInShape::Cube, "Cube", "/Engine/Shapes/Cube.kbshape", "21kb built-in shape Cube v1\n" },
    BuiltInShapeDesc{ BuiltInShape::Sphere, "Sphere", "/Engine/Shapes/Sphere.kbshape", "21kb built-in shape Sphere v1\n" },
    BuiltInShapeDesc{ BuiltInShape::Capsule, "Capsule", "/Engine/Shapes/Capsule.kbshape", "21kb built-in shape Capsule v1\n" },
    BuiltInShapeDesc{ BuiltInShape::Cylinder, "Cylinder", "/Engine/Shapes/Cylinder.kbshape", "21kb built-in shape Cylinder v1\n" },
    BuiltInShapeDesc{ BuiltInShape::Cone, "Cone", "/Engine/Shapes/Cone.kbshape", "21kb built-in shape Cone v1\n" },
    BuiltInShapeDesc{ BuiltInShape::Plane, "Plane", "/Engine/Shapes/Plane.kbshape", "21kb built-in shape Plane v1\n" },
    BuiltInShapeDesc{ BuiltInShape::Quad, "Quad", "/Engine/Shapes/Quad.kbshape", "21kb built-in shape Quad v1\n" },
};

// The same FNV-1a 64 a file's content hash uses (AssetFileSystem::HashFile).
[[nodiscard]] std::uint64_t HashSource(std::string_view bytes) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char byte : bytes) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    return hash == 0U ? 1099511628211ULL : hash;
}

[[nodiscard]] std::string_view WithoutExtension(std::string_view path) noexcept {
    if (path.ends_with(kBuiltInShapeExtension)) {
        path.remove_suffix(kBuiltInShapeExtension.size());
    }
    return path;
}

} // namespace

std::span<const BuiltInShapeDesc> BuiltInShapes() noexcept {
    return kShapes;
}

const BuiltInShapeDesc* FindBuiltInShape(std::string_view nameOrPath) noexcept {
    const std::string_view wanted = WithoutExtension(nameOrPath);
    const auto found = std::ranges::find_if(kShapes, [wanted](const BuiltInShapeDesc& desc) {
        return desc.name == wanted || WithoutExtension(desc.virtualPath) == wanted;
    });
    return found == kShapes.end() ? nullptr : &*found;
}

const BuiltInShapeDesc* FindBuiltInShape(const AssetMetadata& metadata) noexcept {
    const std::string path = NormalizeAssetPath(metadata.virtualPath);
    const BuiltInShapeDesc* desc = FindBuiltInShape(std::string_view{ path });
    return desc != nullptr && path == desc->virtualPath && metadata.type == "RenderMesh" ? desc : nullptr;
}

AssetId BuiltInShapeId(BuiltInShape shape) noexcept {
    const BuiltInShapeDesc& desc = kShapes[static_cast<std::size_t>(shape)];
    return MakeAssetId(NormalizeAssetPath(std::filesystem::path{ desc.virtualPath }) + ":RenderMesh");
}

AssetMetadata BuiltInShapeMetadata(const BuiltInShapeDesc& desc) {
    return AssetMetadata{
        .id = BuiltInShapeId(desc.shape),
        .type = "RenderMesh",
        .importCategory = "Mesh",
        .browseTag = "Engine shape",
        .name = std::string{ desc.name },
        .virtualPath = std::filesystem::path{ desc.virtualPath },
        .physicalPath = {},
        .sourceExtension = std::string{ kBuiltInShapeExtension },
        .contentHash = HashSource(desc.source),
        .dependencies = {},
        .runtimeLoadable = true,
    };
}

} // namespace kb::assets
