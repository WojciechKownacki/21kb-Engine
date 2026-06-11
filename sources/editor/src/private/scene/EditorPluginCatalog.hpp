#pragma once

#include <cstddef>
#include <string_view>

namespace kb::editor {

struct EditorPluginDescriptor {
    std::string_view id;
    std::string_view displayName;
    std::string_view category;
    std::string_view provider;
    std::string_view description;
    std::string_view binaryPath;
};

class EditorPluginCatalog {
public:
    EditorPluginCatalog() = delete;

    [[nodiscard]] static std::size_t Count() noexcept;
    [[nodiscard]] static const EditorPluginDescriptor* At(std::size_t index) noexcept;
};

} // namespace kb::editor
