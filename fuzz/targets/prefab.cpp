// .kbprefab text assets.
#include "FuzzSupport.hpp"

#include "scene/prefab/io/ScenePrefabAssetReader.hpp"

#include <sstream>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::istringstream input{ std::string{ kb::fuzz::Text(data, size) } };
    kb::scene::ScenePrefabAssetReadResult result;
    static_cast<void>(kb::scene::ScenePrefabAssetReader::Read(input, result));
    return 0;
}
