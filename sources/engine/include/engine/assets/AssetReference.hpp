#pragma once

#include "engine/assets/AssetId.hpp"

#include <filesystem>
#include <string>

namespace kb::assets {

struct AssetReference {
    AssetId id{};
    std::string type;
    std::filesystem::path virtualPath;

    [[nodiscard]] bool IsValid() const noexcept {
        return id.IsValid() || !virtualPath.empty();
    }
};

} // namespace kb::assets
