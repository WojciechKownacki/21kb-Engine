#pragma once

#include "engine/platform/FileSystemPath.hpp"
#include <filesystem>

namespace kb::editor {
struct EditorEngineInstallation {
    std::filesystem::path root;
    std::filesystem::path build;
};

inline EditorEngineInstallation ResolveEditorEngineInstallation() {
    for (auto folder = kb::platform::RunningExecutablePath().parent_path(); !folder.empty(); folder = folder.parent_path()) {
        std::error_code error;
        if (std::filesystem::is_regular_file(folder / "prebuilt-engine.json", error)) {
            return {folder, folder};
        }
        if (folder == folder.parent_path()) break;
    }
#if defined(KB_EDITOR_ENGINE_ROOT) && defined(KB_EDITOR_BUILD_ROOT)
    return {KB_EDITOR_ENGINE_ROOT, KB_EDITOR_BUILD_ROOT};
#else
    return {};
#endif
}
} // namespace kb::editor
