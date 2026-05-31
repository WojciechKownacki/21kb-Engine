#include "engine/assets/AssetMetadata.hpp"

#include <algorithm>

namespace kb::assets {

std::string NormalizeAssetPath(const std::filesystem::path& path) {
    std::string normalized = path.lexically_normal().generic_string();
    if (normalized.empty()) {
        return {};
    }
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    while (normalized.size() > 1 && normalized.back() == '/') {
        normalized.pop_back();
    }
    return normalized;
}

} // namespace kb::assets
