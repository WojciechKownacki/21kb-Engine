#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "engine/project/ProjectManager.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "project/EditorProjectBootstrap.hpp"
#include "project/EditorProjectPaths.hpp"
#include "scene/EditorDefaultSceneFactory.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace {

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_project_tests";
}

class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const std::filesystem::path& path)
        : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }

    ~ScopedCurrentPath() {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
    }

    ScopedCurrentPath(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

private:
    std::filesystem::path previous_;
};

[[nodiscard]] bool HasEnabledPlugin(const kb::project::ProjectDescriptor& descriptor, std::string_view name) {
    return std::ranges::any_of(descriptor.plugins, [name](const kb::project::ProjectPluginReference& plugin) {
        return plugin.name == name && plugin.enabled;
    });
}

void RunProjectBootstrapCreatesDescriptorAndRuntimeFoldersTest() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    std::filesystem::create_directories(TempRoot(), error);
    kb::editor::tests::Require(!error, "Editor project bootstrap test temp root could not be created");

    const ScopedCurrentPath currentPath{ TempRoot() };

    const kb::editor::EditorProjectBootstrapResult created = kb::editor::EditorProjectBootstrap::BootstrapDefaultProject();
    kb::editor::tests::Require(created.succeeded, "Editor project bootstrap did not create a project descriptor");
    kb::editor::tests::Require(created.created, "Editor project bootstrap should report first-run creation");
    kb::editor::tests::Require(std::filesystem::is_regular_file(kb::editor::EditorProjectPaths::ProjectFile()), "Editor project descriptor file was not created");
    kb::editor::tests::Require(std::filesystem::is_directory(kb::editor::EditorProjectPaths::ScenesRoot()), "Editor project scenes folder was not created");
    kb::editor::tests::Require(std::filesystem::is_directory(kb::editor::EditorProjectPaths::PrefabsRoot()), "Editor project prefabs folder was not created");
    kb::editor::tests::Require(created.descriptor.defaultScene == "/Game/Scenes/Main.21kbscene", "Editor project default scene virtual path is invalid");
    kb::editor::tests::Require(HasEnabledPlugin(created.descriptor, "Physics.Jolt"), "Editor project default physics plugin was not configured");
    kb::editor::tests::Require(HasEnabledPlugin(created.descriptor, "Audio.Miniaudio"), "Editor project default audio plugin was not configured");
    kb::editor::tests::Require(HasEnabledPlugin(created.descriptor, "Rendering.BasicLighting"), "Editor project default lighting plugin was not configured");

    const kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(kb::editor::EditorProjectPaths::ProjectFile());
    kb::editor::tests::Require(loaded.succeeded, "Created editor project descriptor did not load");
    kb::editor::tests::Require(loaded.descriptor.name == "Project", "Created editor project descriptor name is invalid");
    kb::editor::tests::Require(loaded.descriptor.contentRoot == "Assets", "Created editor project content root is invalid");
    kb::editor::tests::Require(HasEnabledPlugin(loaded.descriptor, "Physics.Jolt"), "Created editor project descriptor did not persist the default physics plugin");
    kb::editor::tests::Require(HasEnabledPlugin(loaded.descriptor, "Audio.Miniaudio"), "Created editor project descriptor did not persist the default audio plugin");
    kb::editor::tests::Require(HasEnabledPlugin(loaded.descriptor, "Rendering.BasicLighting"), "Created editor project descriptor did not persist the default lighting plugin");

    const kb::editor::EditorProjectBootstrapResult reopened = kb::editor::EditorProjectBootstrap::BootstrapDefaultProject();
    kb::editor::tests::Require(reopened.succeeded, "Editor project bootstrap did not reopen an existing project descriptor");
    kb::editor::tests::Require(!reopened.created, "Editor project bootstrap should not recreate an existing descriptor");

    std::filesystem::current_path(TempRoot().parent_path(), error);
    std::filesystem::remove_all(TempRoot(), error);
}

[[nodiscard]] kb::scene::SceneEntity FindRootByName(const kb::scene::Scene& scene, std::string_view name) {
    for (const kb::scene::SceneEntity entity : scene.Hierarchy().RootEntities()) {
        if (scene.Entities().Name(entity) == name) {
            return entity;
        }
    }
    return {};
}

void RunDefaultSceneFactorySeedsPhysicsDemoTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity selected = kb::editor::EditorDefaultSceneFactory::Seed(scene);
    kb::editor::tests::Require(selected.IsValid(), "Editor default scene did not select the main camera");

    const kb::scene::SceneEntity floor = FindRootByName(scene, "Physics Floor");
    const kb::scene::SceneEntity cube = FindRootByName(scene, "Falling Cube");
    kb::editor::tests::Require(floor.IsValid(), "Editor default scene did not create the physics floor");
    kb::editor::tests::Require(cube.IsValid(), "Editor default scene did not create the falling cube");

    const kb::scene::RigidbodyComponent* floorBody = scene.Components().Rigidbodies().TryGet(floor);
    const kb::scene::RigidbodyComponent* cubeBody = scene.Components().Rigidbodies().TryGet(cube);
    const kb::scene::ColliderComponent* floorCollider = scene.Components().Colliders().TryGet(floor);
    const kb::scene::ColliderComponent* cubeCollider = scene.Components().Colliders().TryGet(cube);
    kb::editor::tests::Require(floorBody != nullptr && floorBody->bodyType == kb::scene::RigidbodyBodyType::Static, "Physics floor is not a static rigidbody");
    kb::editor::tests::Require(cubeBody != nullptr && cubeBody->bodyType == kb::scene::RigidbodyBodyType::Dynamic, "Falling cube is not a dynamic rigidbody");
    kb::editor::tests::Require(floorCollider != nullptr && floorCollider->shape == kb::scene::ColliderShape::Box, "Physics floor is missing a box collider");
    kb::editor::tests::Require(cubeCollider != nullptr && cubeCollider->shape == kb::scene::ColliderShape::Box, "Falling cube is missing a box collider");
}

} // namespace

namespace kb::editor::tests {

void RunEditorProjectTests() {
    RunProjectBootstrapCreatesDescriptorAndRuntimeFoldersTest();
    RunDefaultSceneFactorySeedsPhysicsDemoTest();
}

} // namespace kb::editor::tests
