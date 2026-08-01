#include "TerrainEditorModule.hpp"

#include "engine/modules/EngineModuleExports.hpp"
#include "engine/modules/EngineModuleLoadingPhase.hpp"

#include <cstdint>

namespace kb::terrain_editor {

kb::modules::EngineModuleMetadata TerrainEditorModule::Metadata() const {
    return kb::modules::EngineModuleMetadata{
        "Editor.Terrain",
        1U,
        {},
        kb::modules::EngineModuleLoadingPhase::Default,
    };
}

} // namespace kb::terrain_editor

extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() {
    return kb::modules::kEngineModuleAbiVersion;
}

extern "C" KB_ENGINE_MODULE_EXPORT const char* kb_engine_module_name() {
    return "Editor.Terrain";
}

extern "C" KB_ENGINE_MODULE_EXPORT kb::modules::IEngineModule* kb_create_engine_module() {
    return new kb::terrain_editor::TerrainEditorModule();
}

extern "C" KB_ENGINE_MODULE_EXPORT void kb_destroy_engine_module(kb::modules::IEngineModule* module) {
    delete module;
}
