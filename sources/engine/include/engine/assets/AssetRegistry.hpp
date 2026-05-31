#pragma once

#include "engine/assets/AssetMetadata.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kb::assets {

class AssetRegistry {
public:
    [[nodiscard]] bool Upsert(AssetMetadata metadata);
    [[nodiscard]] bool Remove(AssetId id) noexcept;
    [[nodiscard]] const AssetMetadata* Find(AssetId id) const noexcept;
    [[nodiscard]] AssetMetadata* FindMutable(AssetId id) noexcept;
    [[nodiscard]] const AssetMetadata* FindByPath(const std::filesystem::path& virtualPath) const noexcept;
    [[nodiscard]] std::vector<AssetMetadata> ByType(std::string_view type) const;
    [[nodiscard]] std::span<const AssetMetadata> All() const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept;
    void Clear() noexcept;

private:
    std::vector<AssetMetadata> assets_;
    std::unordered_map<std::uint64_t, std::size_t> byId_;
    std::unordered_map<std::string, std::uint64_t> byPath_;
};

} // namespace kb::assets
