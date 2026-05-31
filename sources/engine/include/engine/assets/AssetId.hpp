#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kb::assets {

struct AssetId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return value != 0;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return IsValid();
    }

    [[nodiscard]] friend constexpr bool operator==(AssetId lhs, AssetId rhs) noexcept = default;
};

[[nodiscard]] AssetId MakeAssetId(std::string_view stableKey) noexcept;
[[nodiscard]] std::string ToString(AssetId id);
[[nodiscard]] bool TryParseAssetId(std::string_view text, AssetId& output) noexcept;

} // namespace kb::assets
