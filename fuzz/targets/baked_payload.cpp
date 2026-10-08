// The baked mesh and texture blocks a pack carries, read as the runtime reads them.
#include "FuzzSupport.hpp"

#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/bake/TextureBaker.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0U) {
        return 0;
    }
    // The first byte splits the rest into a primary block and up to three chunks.
    const std::size_t chunkCount = data[0] % 4U;
    const std::span<const std::uint8_t> rest = kb::fuzz::Bytes(data + 1, size - 1U);
    const std::size_t part = rest.size() / (chunkCount + 1U);
    const std::span<const std::uint8_t> primary = rest.first(chunkCount == 0U ? rest.size() : part);
    std::vector<std::vector<std::uint8_t>> chunks;
    for (std::size_t index = 0U; index < chunkCount; ++index) {
        const std::size_t begin = part * (index + 1U);
        const std::size_t end = index + 1U == chunkCount ? rest.size() : begin + part;
        chunks.emplace_back(rest.begin() + static_cast<std::ptrdiff_t>(begin), rest.begin() + static_cast<std::ptrdiff_t>(end));
    }
    kb::render::RenderMeshAssetData mesh;
    static_cast<void>(kb::render::bake::ReadBakedMesh(primary, chunks, mesh));
    kb::render::RenderTextureAssetData texture;
    static_cast<void>(kb::render::bake::ReadBakedTexture(rest, texture));
    return 0;
}
