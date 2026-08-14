#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#include <filesystem>
#include <string>

namespace kb::editor {

struct ParticleEditorHostSession {
    bool visible = true;
    DockArea area = DockArea::Center;
    DockRect floatingRect{148, 140, 900, 640};
    std::filesystem::path documentPath;
};

struct ParticleEditorHostSessionLoadResult {
    ParticleEditorHostSession session;
    bool found = false;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept { return error.empty(); }
};

class ParticleEditorHostSessionStore final {
public:
    static constexpr std::size_t kMaximumBytes = 16U * 1024U;

    [[nodiscard]] static ParticleEditorHostSessionLoadResult Load(
        const std::filesystem::path& statePath,
        const std::filesystem::path& projectRoot);
    [[nodiscard]] static bool Save(
        const std::filesystem::path& statePath,
        const std::filesystem::path& projectRoot,
        const ParticleEditorHostSession& session,
        std::string& error);
};

} // namespace kb::editor
