#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct ProjectFilesAssetTileVisualLayout {
    RECT icon{};
    RECT label{};
};

class ProjectFilesAssetTileMetrics {
public:
    ProjectFilesAssetTileMetrics() = delete;

    [[nodiscard]] static int NamePointSize(const RECT& tile) noexcept;
    [[nodiscard]] static ProjectFilesAssetTileVisualLayout ResolveVisualLayout(RECT tile) noexcept;
};

#endif

} // namespace kb::editor
