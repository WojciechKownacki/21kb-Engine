#pragma once

#include "engine/modules/EngineModuleMetadata.hpp"
#include "engine/modules/IEngineModule.hpp"

namespace kb::terrain_editor {

class TerrainEditorModule final : public kb::modules::IEngineModule {
public:
    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override;
};

} // namespace kb::terrain_editor
