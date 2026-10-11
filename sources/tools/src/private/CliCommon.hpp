#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets {
class AssetRegistry;
struct AssetMetadata;
}

namespace kb::scene {
class Scene;
}

namespace kb::cli {

struct CommandIo {
    std::ostream& out;
    std::ostream& err;
};

// Parses "--name value" options, "--name" boolean flags (from a caller-provided
// flag list), and bare positionals from a command's argument tail.
class ArgumentList final {
public:
    ArgumentList(std::span<const std::string> arguments, std::span<const std::string_view> flagNames = {});

    [[nodiscard]] std::optional<std::string> Option(std::string_view name) const;
    // Every value of an option that may be given more than once, in command-line order.
    [[nodiscard]] std::vector<std::string> Options(std::string_view name) const;
    [[nodiscard]] bool Flag(std::string_view name) const noexcept;
    [[nodiscard]] const std::vector<std::string>& Positionals() const noexcept;
    [[nodiscard]] const std::vector<std::string>& Errors() const noexcept;

private:
    std::vector<std::pair<std::string, std::string>> options_;
    std::vector<std::string> flags_;
    std::vector<std::string> positionals_;
    std::vector<std::string> errors_;
};

// Mounts <projectRoot>/Assets and runs asset discovery. Returns false with a
// populated error when the root is missing or the mount fails.
[[nodiscard]] bool MountProjectAssets(
    kb::scene::Scene& scene,
    const std::filesystem::path& projectRoot,
    std::string& error,
    std::size_t* discoveredCount = nullptr);

// Resolves user-supplied paths: absolute paths pass through; relative paths
// are anchored at the project root when it is set and the file exists there,
// falling back to the current working directory.
[[nodiscard]] std::filesystem::path ResolveInputPath(
    const std::filesystem::path& input,
    const std::filesystem::path& projectRoot);

// A scene, world or output file of the project given by --project: relative paths are anchored as
// ResolveInputPath does, and the result must lie inside the project - kb_cli never writes into
// another project or elsewhere on disk through a project command. Without a project the path is
// taken as given. Empty with `error` set when the path leaves the project.
[[nodiscard]] std::filesystem::path ResolveProjectFile(
    const std::filesystem::path& input,
    const std::filesystem::path& projectRoot,
    std::string& error);

// A colour as a person writes it: "#RRGGBB" (or "#RRGGBBAA" when `channels` is 4) is sRGB, as
// every colour picker shows it, and comes back linear; "r,g,b[,a]" are linear values in 0..1.
[[nodiscard]] std::optional<std::array<float, 4>> ParseColorText(std::string_view text, std::size_t channels);

// Finds an asset by virtual path ("/Game/...") or by physical file path.
[[nodiscard]] const kb::assets::AssetMetadata* FindAssetByFlexiblePath(
    const kb::assets::AssetRegistry& registry,
    const std::filesystem::path& input);

} // namespace kb::cli
