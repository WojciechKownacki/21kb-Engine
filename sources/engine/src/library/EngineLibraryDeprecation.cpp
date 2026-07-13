#include "engine/library/EngineLibraryDeprecation.hpp"

namespace kb::library {

std::string FormatDeprecationWarning(std::string_view functionName, const LibraryDeprecation& deprecation) {
    std::string warning = "'" + std::string{ functionName } + "' is deprecated since " + ToString(deprecation.sinceVersion) + ": " + deprecation.message;
    if (!deprecation.replacementCanonicalName.empty()) {
        warning += " Use '" + deprecation.replacementCanonicalName + "' instead.";
    }
    return warning;
}

std::size_t MigrateVisualGraphCallNativeNodes(
    std::vector<kb::visual::VisualGraphNode>& nodes,
    std::string_view deprecatedCanonicalName,
    const LibraryDeprecation& deprecation) {
    if (deprecation.replacementCanonicalName.empty()) {
        return 0U;
    }
    const std::string deprecatedSymbol = "Function." + std::string{ deprecatedCanonicalName };
    const std::string replacementSymbol = "Function." + deprecation.replacementCanonicalName;

    std::size_t migrated = 0U;
    for (kb::visual::VisualGraphNode& node : nodes) {
        if (node.kind == kb::visual::VisualGraphNodeKind::CallNative && node.symbol == deprecatedSymbol) {
            node.symbol = replacementSymbol;
            ++migrated;
        }
    }
    return migrated;
}

} // namespace kb::library
