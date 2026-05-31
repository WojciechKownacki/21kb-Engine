#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets {

struct AssetMount {
    std::string name;
    std::filesystem::path root;
};

class AssetMountTable {
public:
    [[nodiscard]] bool Mount(std::string name, std::filesystem::path root);
    [[nodiscard]] bool Unmount(std::string_view name) noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> Resolve(const std::filesystem::path& virtualPath) const;
    [[nodiscard]] std::optional<std::filesystem::path> ToVirtual(const std::filesystem::path& physicalPath) const;
    [[nodiscard]] const std::vector<AssetMount>& Mounts() const noexcept;
    void Clear() noexcept;

private:
    std::vector<AssetMount> mounts_;
};

} // namespace kb::assets
