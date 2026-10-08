// Wavefront OBJ meshes.
#include "FuzzSupport.hpp"

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <sstream>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::istringstream input{ std::string{ kb::fuzz::Text(data, size) } };
    static_cast<void>(kb::render::RenderMeshAssetBuilder::LoadObj(input));
    return 0;
}
