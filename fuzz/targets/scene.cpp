// .21kbscene documents, decoded the way SceneDocumentService::Load decodes them.
#include "FuzzSupport.hpp"

#include "scene/asset/io/SceneAssetReader.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static_cast<void>(kb::scene::SceneAssetReader::Read(std::vector<std::uint8_t>(data, data + size)));
    return 0;
}
