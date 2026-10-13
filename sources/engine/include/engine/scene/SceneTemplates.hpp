#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace kb::project {
struct ProjectDescriptor;
}

namespace kb::scene {

class Scene;

// What a new scene starts with, like Unity's New Scene templates: Basic is a camera, a sun, the sky
// and a floor, ready to press Play; Empty has nothing. The Hub, `kb_cli new`, `kb_cli scene-new`
// and the editor's File > New Scene all fill a scene through SceneTemplates::Fill.
enum class SceneTemplate : std::uint8_t { Basic, Empty };

struct SceneTemplateInfo {
    SceneTemplate kind{};
    std::string_view name;        // what kb_cli --template takes and the dialog card shows
    std::string_view description; // one line under the name
};

// Where a new scene goes: its file under the project's Assets and the virtual path the settings and
// the editor use (/Game/...). `error` says why there is none.
struct NewScenePath {
    std::filesystem::path file;
    std::string virtualPath;
    std::string error;
};

class SceneTemplates {
public:
    SceneTemplates() = delete;

    [[nodiscard]] static std::span<const SceneTemplateInfo> All() noexcept;
    [[nodiscard]] static std::string_view Name(SceneTemplate kind) noexcept;
    // Case-insensitive ("basic" works); nullopt for an unknown name.
    [[nodiscard]] static std::optional<SceneTemplate> Parse(std::string_view name) noexcept;
    // Adds the template's objects to `scene`. Returns why it could not, or an empty string.
    [[nodiscard]] static std::string Fill(Scene& scene, SceneTemplate kind, const kb::project::ProjectDescriptor& project);
    // The new scene `name` in `folder` of the project: `folder` is relative to the project root
    // ("Assets/Scenes") or virtual ("/Game/Scenes"). The name and every folder follow
    // AssetFileNameError, the folder lies inside Assets, and no scene of that name exists there.
    [[nodiscard]] static NewScenePath ResolveNewScene(const std::filesystem::path& projectRoot, std::string_view folder,
        std::string_view name);
    // Writes a new scene file (and its .meta) made from the template. Refuses an existing file.
    // Returns why nothing was written, or an empty string.
    [[nodiscard]] static std::string WriteSceneFile(const std::filesystem::path& scenePath, SceneTemplate kind,
        const kb::project::ProjectDescriptor& project);
};

} // namespace kb::scene
