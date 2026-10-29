#include "engine/modules/EngineModuleContext.hpp"
#include "engine/assets/AssetManager.hpp"

namespace kb::modules {
bool EngineModuleContext::RegisterAssetLoader(kb::assets::AssetManager& manager,
    std::unique_ptr<kb::assets::IAssetLoader> loader) const {
    if (registerLoader_) return registerLoader_(manager, std::move(loader), libraryLifetime_);
    return manager.RegisterLoader(std::move(loader));
}
} // namespace kb::modules
