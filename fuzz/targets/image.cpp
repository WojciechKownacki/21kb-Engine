// Texture sources as the texture loader decodes them: image files through bimg,
#include "FuzzSupport.hpp"

// the engine's text texture format and its imported-asset container.
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0U) {
        return 0;
    }
    static constexpr const char* kPaths[] = { "Texture.png", "Texture.kbtex", "Texture.21kb" };
    kb::assets::AssetMetadata metadata;
    metadata.type = "Texture";
    kb::render::RenderTextureAssetLoader loader{ bgfx::RendererType::Noop };
    const kb::assets::AssetLoadRequest request{
        .metadata = metadata,
        .resolvedPath = kPaths[data[0] % 3U],
        .runtimePack = nullptr,
        .sourceBytes = kb::fuzz::Bytes(data + 1, size - 1U),
    };
    static_cast<void>(loader.Load(request));
    return 0;
}
