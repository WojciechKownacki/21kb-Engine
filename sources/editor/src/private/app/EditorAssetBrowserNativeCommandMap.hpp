#pragma once

#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#include <cstdint>

namespace kb::editor {

class EditorAssetBrowserNativeCommandMap {
public:
    EditorAssetBrowserNativeCommandMap() = delete;

    [[nodiscard]] static std::uint32_t Id(EditorAssetContextCommand command) noexcept;
    [[nodiscard]] static EditorAssetContextCommand Command(std::uint32_t id) noexcept;
};

} // namespace kb::editor
