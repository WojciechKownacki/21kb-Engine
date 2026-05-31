#pragma once

#include <filesystem>
#include <optional>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorProjectPanelHitTester {
public:
    EditorProjectPanelHitTester() = delete;

    [[nodiscard]] static bool IsPrefabDropTarget(const RECT& content, int x, int y) noexcept;
    [[nodiscard]] static std::optional<std::filesystem::path> PrefabAssetAt(const RECT& content, int x, int y);
};

#endif

} // namespace kb::editor
