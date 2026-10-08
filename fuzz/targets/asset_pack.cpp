// .kbpack containers: the index and every block through the reader, then the
#include "FuzzSupport.hpp"

// runtime view and the typed payload validation a packaged game runs at mount.
#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "kb/render/bake/RuntimeAssetPackValidation.hpp"

#include <memory>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::vector<std::uint8_t> bytes(data, data + size);
    {
        kb::assets::bake::AssetPackReader reader;
        if (reader.MountMemory(bytes) == kb::assets::bake::AssetPackReadStatus::Success) {
            std::vector<std::uint8_t> block;
            for (const kb::assets::bake::AssetPackArtifactEntry& artifact : reader.Artifacts()) {
                for (const kb::assets::bake::AssetPackBlockEntry& entry : artifact.blocks) {
                    static_cast<void>(reader.ReadBlock(artifact, entry.name, block));
                }
            }
        }
    }
    const kb::assets::bake::BakeTargetProfile profile = kb::assets::bake::WindowsX64BakeTargetProfile();
    auto pack = std::make_shared<kb::assets::bake::RuntimeAssetPack>();
    if (pack->MountMemory(bytes, profile) == kb::assets::bake::RuntimeAssetPackStatus::Success) {
        static_cast<void>(kb::render::ValidateRuntimeAssetPack(pack, profile));
    }
    return 0;
}
