#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace kb::project {
struct ProjectDescriptor;
struct ProjectSettings;
}

namespace kb::scene {

class Scene;

// What a new scene (and a new project's startup scene) starts with. Empty has nothing; Basic (named Basic3D, "Basic"
// still works) is a camera, a sun, the sky and a floor, ready to press Play; Basic2D is an orthographic camera and a
// sprite floor with a collider; FirstPerson and ThirdPerson are a walking character with its camera, a controller script
// and the Move and Jump input. The Hub, `kb_cli new`, `kb_cli scene-new` and the editor's File > New Scene all fill a
// scene through SceneTemplates::Fill; a new project also takes the template's settings and files.
enum class SceneTemplate : std::uint8_t { Basic, Empty, Basic2D, FirstPerson, ThirdPerson };

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
    // The project settings a template starts a project with (a 2D template its gravity, a character template a
    // snappier fall); `settings` holds the defaults on entry.
    static void ConfigureProjectSettings(SceneTemplate kind, kb::project::ProjectSettings& settings);
    // Writes the files the template's objects use into the project at `projectRoot` - a character template's
    // controller script and its input assets - and keeps what the project already has. Returns why it could not, or
    // an empty string; a template without files does nothing.
    [[nodiscard]] static std::string WriteProjectFiles(const std::filesystem::path& projectRoot, SceneTemplate kind,
        const kb::project::ProjectDescriptor& project);
    // Adds the template's objects to `scene`. Returns why it could not, or an empty string.
    [[nodiscard]] static std::string Fill(Scene& scene, SceneTemplate kind, const kb::project::ProjectDescriptor& project);
    // The new scene `name` in `folder` of the project: `folder` is relative to the project root
    // ("Assets/Scenes") or virtual ("/Game/Scenes"). The name and every folder follow
    // AssetFileNameError, the folder lies inside Assets, and no scene of that name exists there.
    [[nodiscard]] static NewScenePath ResolveNewScene(const std::filesystem::path& projectRoot, std::string_view folder,
        std::string_view name);
    // Writes a new scene file (and its .meta) made from the template. Refuses an existing file. A non-zero
    // `audioMixerAssetId` makes that mixer the scene's active one. Returns why nothing was written, or an
    // empty string.
    [[nodiscard]] static std::string WriteSceneFile(const std::filesystem::path& scenePath, SceneTemplate kind,
        const kb::project::ProjectDescriptor& project, std::uint64_t audioMixerAssetId = 0U);
};

} // namespace kb::scene
