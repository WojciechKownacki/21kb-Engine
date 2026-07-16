#include "engine/assets/AssetCompatibility.hpp"

namespace kb::assets {

std::string_view ToString(AssetCompatibilityIssue issue) noexcept {
    switch (issue) {
    case AssetCompatibilityIssue::MissingDependency:
        return "MissingDependency";
    case AssetCompatibilityIssue::IncompatibleType:
        return "IncompatibleType";
    }
    return "MissingDependency";
}

std::string AssetCompatibilityReport::FormatDiagnostics() const {
    std::string formatted;
    for (const AssetCompatibilityDiagnostic& diagnostic : diagnostics) {
        if (!formatted.empty()) {
            formatted.push_back('\n');
        }
        formatted.append(diagnostic.message);
    }
    return formatted;
}

} // namespace kb::assets
