// Imported font payloads, which the validator must refuse before the rasterizer sees them.
#include "FuzzSupport.hpp"

#include "engine/assets/ImportedAsset.hpp"
#include "private/ui/ScreenUIFontPayloadValidator.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0U) {
        return 0;
    }
    kb::assets::ImportedAsset asset;
    asset.category = kb::assets::AssetImportCategory::Font;
    asset.sourceExtension = (data[0] & 1U) != 0U ? ".otf" : ".ttf";
    asset.payload.assign(reinterpret_cast<const std::byte*>(data + 1), reinterpret_cast<const std::byte*>(data + size));
    asset.sourceSize = asset.payload.size();
    static_cast<void>(kb::render::ScreenUIFontPayloadValidator::Validate(asset));
    return 0;
}
