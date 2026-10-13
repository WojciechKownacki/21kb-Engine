#pragma once

#include "engine/assets/AssetImportTypes.hpp"
#include "engine/assets/IAssetLoader.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace kb::assets {

inline constexpr const char* kAssetImportPresetExtension = ".kbimportpreset";
inline constexpr const char* kAssetImportPresetAssetType = "ImportPreset";

// A reusable set of import options, as Unreal's Interchange pipeline asset or a Unity Preset of
// a ModelImporter: the import dialog and kb_cli import --preset start from it. Its payload IS
// AssetImportOptions (no second schema to keep in sync). A flat `key value` text file: unknown
// keys are ignored (a newer editor's preset still loads), a missing key keeps the default, and
// a value a key does not take refuses the whole file.
class AssetImportPresetIO final {
public:
    AssetImportPresetIO() = delete;

    [[nodiscard]] static std::optional<AssetImportOptions> Parse(std::istream& input, std::string* error = nullptr);
    [[nodiscard]] static std::optional<AssetImportOptions> Load(const std::filesystem::path& path, std::string* error = nullptr);
    [[nodiscard]] static std::string Write(const AssetImportOptions& options);
    // False for options the file could not load again (an invalid scale or rotation) or a failed write.
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const AssetImportOptions& options);
    // One option by its preset-file name (scale, rotation, importMaterials, ...); false, and
    // `options` unchanged, for an unknown name or a value it does not take.
    [[nodiscard]] static bool SetField(AssetImportOptions& options, std::string_view key, std::string_view value);
    [[nodiscard]] static std::vector<std::string> FieldNames();
};

class AssetImportPresetLoader final : public IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] AssetLoadResult Load(const AssetLoadRequest& request) override;
};

} // namespace kb::assets
