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

    // Monotonic change counter bumped on every mutation (Upsert/Remove/Clear). It lets callers detect
    // "did anything in the asset set change since I last looked?" with a single integer compare instead of
    // re-hashing metadata. Content edits (reimport/discovery) flow through Upsert, so a changed contentHash
    // always bumps this too. Starts at 1 so 0 can serve as an "untracked" sentinel elsewhere.
    [[nodiscard]] std::uint64_t Generation() const noexcept {
        return generation_;
    }

private:
    std::vector<AssetMetadata> assets_;
    std::unordered_map<std::uint64_t, std::size_t> byId_;
    std::unordered_map<std::string, std::uint64_t> byPath_;
    std::uint64_t generation_ = 1U;
};

} // namespace kb::assets
