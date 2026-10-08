// The .meta sidecar a scene is checked against before it is trusted.
#include "FuzzSupport.hpp"

#include "scene/asset/io/SceneAssetMetaReader.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static_cast<void>(kb::scene::SceneAssetMetaReader::Read(kb::fuzz::WriteScratchFile(data, size, "Scene.meta")));
    return 0;
}
