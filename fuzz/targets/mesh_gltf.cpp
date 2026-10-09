// glTF and GLB meshes from memory; with no source path, external buffers stay unopened.
#include "FuzzSupport.hpp"

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static_cast<void>(kb::render::RenderMeshAssetBuilder::LoadGltf(kb::fuzz::Bytes(data, size), {}));
    static_cast<void>(kb::render::RenderMeshAssetBuilder::GltfExternalBufferUris(kb::fuzz::Bytes(data, size)));
    return 0;
}
