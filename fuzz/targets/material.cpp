// Material text assets: materials, instances, graphs, functions, parameter
#include "FuzzSupport.hpp"

// collections and material types, chosen by the first byte.
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"

#include <sstream>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0U) {
        return 0;
    }
    std::istringstream input{ std::string{ kb::fuzz::Text(data + 1, size - 1U) } };
    switch (data[0] % 6U) {
    case 0U: static_cast<void>(kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input)); break;
    case 1U: static_cast<void>(kb::render::RenderMaterialInstanceAssetLoader::LoadInstanceWithDiagnostics(input)); break;
    case 2U: static_cast<void>(kb::render::RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(input)); break;
    case 3U: static_cast<void>(kb::render::RenderMaterialFunctionAssetLoader::LoadFunctionWithDiagnostics(input)); break;
    case 4U: static_cast<void>(kb::render::RenderMaterialParameterCollectionAssetLoader::LoadCollectionWithDiagnostics(input)); break;
    default: static_cast<void>(kb::render::RenderMaterialTypeAssetLoader::LoadTypeWithDiagnostics(input)); break;
    }
    return 0;
}
