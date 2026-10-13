#pragma once

#include "CliCommon.hpp"

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kb::assets {
class AssetRegistry;
}

// What material-*, material-instance-* and material-graph share: the project root, an asset of
// the project by its /Game path, the material's named fields (as the editor's material panel
// names them) and texture references.
namespace kb::cli::material {

[[nodiscard]] std::string Utf8Text(const std::filesystem::path& path);
// Lower case without spaces, underscores or dashes: "Base Color" == "base_color" == "basecolor".
[[nodiscard]] std::string Normalized(std::string_view text);
[[nodiscard]] std::filesystem::path ProjectRoot(const ArgumentList& arguments, std::string_view command, CommandIo io);

struct ProjectAsset {
    std::filesystem::path file;
    std::string virtualPath;
};

// `text` as a virtual path (/Game/...) or a path inside the project, an existing file with
// `extension` under Assets; else the reason is printed with `createHint` and nullopt returned.
[[nodiscard]] std::optional<ProjectAsset> ProjectAssetFile(
    const std::filesystem::path& projectRoot,
    const std::string& text,
    std::string_view extension,
    std::string_view kind,
    std::string_view createHint,
    CommandIo io);

// A texture of the project (an image under Assets or an imported texture) by its path; 0 with
// `error` set when there is none, "none" clears.
using TextureResolver = std::function<std::optional<std::uint64_t>(std::string_view path, std::string& error)>;
[[nodiscard]] TextureResolver ProjectTextureResolver(const kb::assets::AssetRegistry& registry);
// The project path of a texture id, for printing; empty when it is not registered.
[[nodiscard]] std::string TexturePath(const kb::assets::AssetRegistry& registry, std::uint64_t textureId);

// One "Field=value" of the built-in PBR material (Base Color, Roughness, ..., Base Color Texture,
// ...) applied to `desc`; the reason it was refused, or empty. `known` is false when the field
// name is not one of them (so a caller can try graph parameters first).
[[nodiscard]] std::string ApplyBuiltinField(
    kb::render::RenderMaterialDesc& desc,
    std::string_view name,
    std::string_view value,
    const TextureResolver& textures,
    bool& known);
[[nodiscard]] std::string BuiltinFieldList();
// "Field=value" split; empty name when there is no '='.
[[nodiscard]] std::pair<std::string, std::string> SplitSet(const std::string& spec);

// The built-in values and textures, as material-show prints them.
void PrintBuiltinValues(const kb::render::RenderMaterialDesc& desc, const kb::assets::AssetRegistry* registry, CommandIo io);

} // namespace kb::cli::material
