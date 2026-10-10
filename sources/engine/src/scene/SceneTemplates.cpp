#include "engine/scene/SceneTemplates.hpp"

#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/scene/SceneCreateMenu.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/TransformComponent.hpp"

#include "engine/assets/AssetFileName.hpp"

#include <algorithm>
#include <array>
#include <vector>
#include <system_error>

namespace kb::scene {
namespace {

constexpr std::array<SceneTemplateInfo, 2> kTemplates{ {
    { SceneTemplate::Basic, "Basic", "Camera, sun, sky and a floor - ready to press Play." },
    { SceneTemplate::Empty, "Empty", "Nothing at all. Add objects from the Create menu." },
} };

[[nodiscard]] bool EqualsIgnoreCase(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto lower = [](char character) {
            return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
        };
        if (lower(left[index]) != lower(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path{ std::u8string{ reinterpret_cast<const char8_t*>(text.data()), text.size() } };
}

// The UTF-8 file stem, for the scene document's name.
[[nodiscard]] std::string Utf8Stem(const std::filesystem::path& path) {
    const std::u8string stem = path.stem().u8string();
    return std::string{ reinterpret_cast<const char*>(stem.data()), stem.size() };
}

} // namespace

std::span<const SceneTemplateInfo> SceneTemplates::All() noexcept {
    return kTemplates;
}

std::string_view SceneTemplates::Name(SceneTemplate kind) noexcept {
    for (const SceneTemplateInfo& info : kTemplates) {
        if (info.kind == kind) {
            return info.name;
        }
    }
    return {};
}

std::optional<SceneTemplate> SceneTemplates::Parse(std::string_view name) noexcept {
    for (const SceneTemplateInfo& info : kTemplates) {
        if (EqualsIgnoreCase(info.name, name)) {
            return info.kind;
        }
    }
    return std::nullopt;
}

std::string SceneTemplates::Fill(Scene& scene, SceneTemplate kind, const kb::project::ProjectDescriptor& project) {
    if (kind == SceneTemplate::Empty) {
        return {};
    }

    // The camera stands behind the middle of the scene and looks along +Z, the engine's forward:
    // the floor and the sky's horizon are in the shot, as in Unity's new scene.
    const SceneCreateResult camera = SceneCreateMenu::Create(scene, "Camera", "Main Camera", project);
    if (!camera.entity.IsValid()) {
        return "Basic template: " + camera.error;
    }
    if (const TransformComponent* placed = scene.Transforms().TryGet(camera.entity); placed != nullptr) {
        TransformComponent transform = *placed;
        transform.localPosition = Vec3{ 0.0F, 2.0F, -8.0F };
        scene.Transforms().Set(camera.entity, transform);
    }
    // Unity's Main Camera carries the listener; without one a new scene plays no sound.
    if (const SceneComponentKind* listener = SceneComponentAuthoring::Find("AudioListener");
        listener != nullptr && SceneComponentAuthoring::PluginEnabled(project, *listener)) {
        static_cast<void>(SceneComponentAuthoring::Add(scene, camera.entity, "AudioListener", project));
    }

    // The sun from above at an angle (the Create menu's Directional Light), the procedural sky
    // with its ambient light, and the 10 x 10 m floor - without them the game view is black.
    for (const auto& [item, name] : { std::pair{ "Directional Light", "Sun" }, std::pair{ "Environment", "Environment" },
             std::pair{ "Plane", "Plane" } }) {
        const SceneCreateResult created = SceneCreateMenu::Create(scene, item, name, project);
        if (!created.entity.IsValid()) {
            return "Basic template: " + created.error;
        }
    }
    return {};
}

NewScenePath SceneTemplates::ResolveNewScene(const std::filesystem::path& projectRoot, std::string_view folder,
    std::string_view name) {
    if (std::string problem = kb::assets::AssetFileNameError(name); !problem.empty()) {
        return { {}, {}, std::move(problem) };
    }
    // "/Game/Scenes" and "Assets/Scenes" name the same folder.
    std::string relative{ folder };
    std::replace(relative.begin(), relative.end(), '\\', '/');
    if (relative == "/Game" || relative.starts_with("/Game/")) {
        relative = "Assets" + relative.substr(5);
    }
    while (!relative.empty() && relative.back() == '/') {
        relative.pop_back();
    }
    std::vector<std::string> parts;
    for (std::size_t start = 0; start <= relative.size();) {
        const std::size_t slash = std::min(relative.find('/', start), relative.size());
        parts.emplace_back(relative.substr(start, slash - start));
        start = slash + 1;
    }
    if (parts.empty() || parts.front() != "Assets") {
        return { {}, {}, "The folder must be inside Assets, for example Assets/Scenes." };
    }
    std::filesystem::path file = projectRoot;
    std::string virtualPath = "/Game";
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (std::string problem = kb::assets::AssetFileNameError(parts[index]); !problem.empty()) {
            return { {}, {}, "Folder: " + problem };
        }
        file /= Utf8Path(parts[index]);
        if (index > 0) {
            virtualPath += "/" + parts[index];
        }
    }
    const std::string fileName = std::string{ name } + std::string{ ".21kbscene" };
    file /= Utf8Path(fileName);
    virtualPath += "/" + fileName;
    std::error_code error;
    if (std::filesystem::exists(file, error)) {
        return { {}, {}, "A scene named " + std::string{ name } + " already exists there. Choose another name." };
    }
    return { std::move(file), std::move(virtualPath), {} };
}

std::string SceneTemplates::CreateFile(const std::filesystem::path& scenePath, SceneTemplate kind,
    const kb::project::ProjectDescriptor& project) {
    std::error_code error;
    if (std::filesystem::exists(scenePath, error)) {
        return "A scene named " + Utf8Stem(scenePath) + " already exists. Choose another name.";
    }
    std::filesystem::create_directories(scenePath.parent_path(), error);
    if (error) {
        return "The scene folder could not be created: " + error.message();
    }
    Scene scene{ project };
    if (std::string filled = Fill(scene, kind, project); !filled.empty()) {
        return filled;
    }
    if (!SceneDocumentService::Save(scene, scenePath, Utf8Stem(scenePath))) {
        return "The scene file could not be written.";
    }
    return {};
}

} // namespace kb::scene
