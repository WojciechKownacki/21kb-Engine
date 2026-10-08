// Binary FBX meshes through the engine's own static-mesh importer.
#include "FuzzSupport.hpp"

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static_cast<void>(kb::render::RenderMeshAssetBuilder::LoadFbx(
        std::span<const std::byte>{ reinterpret_cast<const std::byte*>(data), size }));
    return 0;
}
