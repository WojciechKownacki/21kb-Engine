#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "project/EditorProjectRepository.hpp"
#include "scene/EditorSceneComparison.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#if !defined(KB_EDITOR_VERSION_CONTROL_TEST_ROOT)
#error "KB_EDITOR_VERSION_CONTROL_TEST_ROOT must name the scratch folder of the version control tests."
#endif

#if defined(_WIN32)

namespace {

using kb::editor::EditorChangeList;
using kb::editor::EditorChangeState;
using kb::editor::EditorProjectRepository;
using kb::editor::EditorSceneChangeKind;
using kb::editor::EditorSceneComparer;
using kb::editor::EditorSceneComparison;
using kb::editor::tests::Require;

namespace fs = std::filesystem;

[[nodiscard]] std::string Utf8(const std::u8string& text) {
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

void WriteFile(const fs::path& path, const std::string& text) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary);
    output << text;
}

[[nodiscard]] std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

[[nodiscard]] const kb::editor::EditorFileChange* FindChange(const EditorChangeList& list, const std::string& path) {
    const auto found = std::ranges::find(list.changes, path, &kb::editor::EditorFileChange::path);
    return found == list.changes.end() ? nullptr : &*found;
}

// One scene object: its name, its parent (an index into the nodes added so far, or none) and where it stands.
std::uint32_t AddNode(kb::scene::SceneDocument& document, const std::string& name, std::uint32_t parent, float x = 0.0F) {
    kb::scene::ScenePrefabNodeDesc node;
    node.name = name;
    node.parentNode = parent;
    node.transform.localPosition = { x, 0.0F, 0.0F };
    return document.worldPrefab.AddNode(std::move(node));
}

constexpr std::uint32_t kRoot = kb::scene::ScenePrefabNodeDesc::NoParent;

[[nodiscard]] kb::scene::SceneDocument LevelScene(bool changed) {
    kb::scene::SceneDocument document;
    document.name = "Level";
    document.guid = "scene:Level";
    const std::uint32_t level = AddNode(document, "Level", kRoot);
    static_cast<void>(AddNode(document, "Door", level, 2.0F));
    // The lamp is the object that changes: it moves and gets brighter.
    kb::scene::ScenePrefabNodeDesc lamp;
    lamp.name = "Lamp";
    lamp.parentNode = level;
    lamp.transform.localPosition = { changed ? 5.0F : 1.0F, 0.0F, 0.0F };
    lamp.components.light = kb::scene::LightComponent{};
    lamp.components.light->intensity = changed ? 7.0F : 3.0F;
    static_cast<void>(document.worldPrefab.AddNode(std::move(lamp)));
    static_cast<void>(AddNode(document, changed ? "Window" : "Crate", level));
    return document;
}

void RunSceneComparisonTest() {
    const kb::scene::SceneDocument before = LevelScene(false);
    const kb::scene::SceneDocument after = LevelScene(true);

    const EditorSceneComparison same = EditorSceneComparer::Compare(before, before);
    Require(same.succeeded && same.objects.empty(), "A scene compared with itself must have no differences");

    const EditorSceneComparison comparison = EditorSceneComparer::Compare(before, after);
    Require(comparison.succeeded && comparison.objects.size() == 3U, "One added, one removed and one changed object must be the only differences");
    const auto kindOf = [&](EditorSceneChangeKind kind) {
        return std::ranges::count(comparison.objects, kind, &kb::editor::EditorSceneObjectChange::kind);
    };
    Require(kindOf(EditorSceneChangeKind::Added) == 1 && kindOf(EditorSceneChangeKind::Removed) == 1 && kindOf(EditorSceneChangeKind::Changed) == 1,
        "The differences must be one of each kind");
    for (const kb::editor::EditorSceneObjectChange& object : comparison.objects) {
        switch (object.kind) {
        case EditorSceneChangeKind::Added:
            Require(object.path == "Level/Window", "The added object must be the window");
            break;
        case EditorSceneChangeKind::Removed:
            Require(object.path == "Level/Crate", "The removed object must be the crate");
            break;
        case EditorSceneChangeKind::Changed: {
            Require(object.path == "Level/Lamp" && object.fields.size() == 2U, "The lamp must differ in exactly two values");
            const auto field = [&](const char* component, const char* property) -> const kb::editor::EditorSceneFieldChange* {
                const auto found = std::ranges::find_if(object.fields, [&](const kb::editor::EditorSceneFieldChange& change) {
                    return change.component == component && change.property == property;
                });
                return found == object.fields.end() ? nullptr : &*found;
            };
            const auto* position = field("Transform", "localPosition.x");
            const auto* intensity = field("Light", "intensity");
            Require(position != nullptr && position->before == "1" && position->after == "5", "The lamp's position must show its old and new value");
            Require(intensity != nullptr && intensity->before == "3" && intensity->after == "7", "The lamp's light must show its old and new value");
            break;
        }
        }
    }

    // A change names its object in the newer scene, so the editor can select it there.
    kb::scene::Scene live{ kb::scene::SceneMode::PrefabPrivate };
    const kb::scene::ScenePrefabInstance instance = live.Prefabs().Instantiate(after.worldPrefab);
    Require(!instance.Empty(), "The newer scene must come to life");
    for (const kb::editor::EditorSceneObjectChange& object : comparison.objects) {
        if (object.kind == EditorSceneChangeKind::Removed) {
            Require(object.address.empty(), "A removed object has no place in the newer scene");
            continue;
        }
        const kb::scene::SceneEntity found = EditorSceneComparer::Find(live, object.address);
        const std::string path = object.path.substr(object.path.rfind('/') + 1U);
        Require(found.IsValid() && live.Entities().Name(found) == path, "A change must find its object in the scene");
    }
}

void RunLargeSceneComparisonTest() {
    constexpr std::uint32_t kObjects = 100000U;
    constexpr std::uint32_t kStep = 10000U;
    const auto build = [&](bool changed) {
        kb::scene::SceneDocument document;
        document.name = "Large";
        document.worldPrefab.Reserve(kObjects + 1U);
        const std::uint32_t world = AddNode(document, "World", kRoot);
        for (std::uint32_t index = 0U; index < kObjects; ++index) {
            static_cast<void>(AddNode(document, "E" + std::to_string(index), world, changed && index % kStep == 0U ? 1.0F : 0.0F));
        }
        return document;
    };
    const EditorSceneComparison comparison = EditorSceneComparer::Compare(build(false), build(true));
    Require(comparison.succeeded && comparison.objects.size() == 10U, "A hundred thousand objects with ten moved must give ten changes");
    Require(std::ranges::all_of(comparison.objects, [](const kb::editor::EditorSceneObjectChange& object) {
        return object.kind == EditorSceneChangeKind::Changed && object.fields.size() == 1U && object.fields.front().component == "Transform";
    }), "Every change must be a moved object");
}

void RunChangesAndRestoreTest() {
    const fs::path root = fs::path{ KB_EDITOR_VERSION_CONTROL_TEST_ROOT };
    std::error_code error;
    fs::remove_all(root, error);
    const fs::path repository = root / "repo";
    fs::create_directories(repository, error);
    // A fresh repository of its own, whatever the folder it sits in, and an identity for its commit.
    _putenv_s("GIT_CEILING_DIRECTORIES", root.string().c_str());
    _putenv_s("GIT_AUTHOR_NAME", "Test");
    _putenv_s("GIT_AUTHOR_EMAIL", "test@example.com");
    _putenv_s("GIT_COMMITTER_NAME", "Test");
    _putenv_s("GIT_COMMITTER_EMAIL", "test@example.com");

    const std::string polish = Utf8(u8"Za\u017c\u00f3\u0142\u0107 g\u0119\u015bl\u0105.txt");
    const std::string spaced = "Folder ze spacj\xC4\x85/dane " + Utf8(u8"\u017a\u00f3\u0142w.txt");
    const std::string added = "Folder ze spacj\xC4\x85/nowy " + Utf8(u8"\u0105\u0119.txt");
    WriteFile(repository / "zmieniany.txt", "stary");
    WriteFile(repository / fs::path{ std::u8string{ u8"Za\u017c\u00f3\u0142\u0107 g\u0119\u015bl\u0105.txt" } }, "do usuniecia");
    WriteFile(repository / fs::path{ std::u8string{ u8"Folder ze spacj\u0105" } } / fs::path{ std::u8string{ u8"dane \u017a\u00f3\u0142w.txt" } }, "bez zmian");
    Require(kb::editor::EditorProjectRepository::CreateLocal(repository).succeeded, "The test repository must be created with its first commit");

    const EditorChangeList clean = EditorProjectRepository::Changes(repository);
    Require(clean.available && clean.changes.empty(), "A repository just saved must have no changes");

    WriteFile(repository / "zmieniany.txt", "nowy");
    fs::remove(repository / fs::path{ std::u8string{ u8"Za\u017c\u00f3\u0142\u0107 g\u0119\u015bl\u0105.txt" } }, error);
    WriteFile(repository / fs::path{ std::u8string{ u8"Folder ze spacj\u0105" } } / fs::path{ std::u8string{ u8"nowy \u0105\u0119.txt" } }, "dodany");
    // The new file is added to the index so that it shows as added; the others stay as the edit left them.
    const std::string stage = "git -C \"" + repository.string() + "\" add \"Folder ze spacj*/nowy*\"";
    Require(std::system(stage.c_str()) == 0, "The new file must be added to the index");

    const EditorChangeList changed = EditorProjectRepository::Changes(repository);
    Require(changed.available && changed.changes.size() == 3U, "One changed, one added and one deleted file must be exactly three entries");
    const auto* modifiedEntry = FindChange(changed, "zmieniany.txt");
    const auto* deletedEntry = FindChange(changed, polish);
    const auto* addedEntry = FindChange(changed, added);
    Require(modifiedEntry != nullptr && modifiedEntry->state == EditorChangeState::Modified, "The edited file must be Modified");
    Require(deletedEntry != nullptr && deletedEntry->state == EditorChangeState::Deleted, "The removed file with Polish letters must be Deleted");
    Require(addedEntry != nullptr && addedEntry->state == EditorChangeState::Added, "The new file in a folder with a space must be Added");
    Require(FindChange(changed, spaced) == nullptr, "An untouched file must not be listed");

    WriteFile(repository / "nieznany.txt", "poza repozytorium");
    const EditorChangeList withUntracked = EditorProjectRepository::Changes(repository);
    const auto* untrackedEntry = FindChange(withUntracked, "nieznany.txt");
    Require(withUntracked.changes.size() == 4U && untrackedEntry != nullptr && untrackedEntry->state == EditorChangeState::Untracked,
        "A file the repository does not know must be Untracked");
    Require(EditorProjectRepository::CanRestore(EditorChangeState::Modified) && EditorProjectRepository::CanRestore(EditorChangeState::Deleted) &&
        !EditorProjectRepository::CanRestore(EditorChangeState::Added) && !EditorProjectRepository::CanRestore(EditorChangeState::Untracked),
        "Only a file the last saved version has can be brought back");

    Require(EditorProjectRepository::Restore(repository, "zmieniany.txt").succeeded, "Restoring the edited file must succeed");
    Require(ReadFile(repository / "zmieniany.txt") == "stary", "The restored file must read as the last saved version");
    Require(EditorProjectRepository::Restore(repository, polish).succeeded, "Restoring the removed file must succeed");
    Require(ReadFile(repository / fs::path{ std::u8string{ u8"Za\u017c\u00f3\u0142\u0107 g\u0119\u015bl\u0105.txt" } }) == "do usuniecia",
        "The removed file must come back as it was");
    const EditorChangeList restored = EditorProjectRepository::Changes(repository);
    Require(FindChange(restored, "zmieniany.txt") == nullptr && FindChange(restored, polish) == nullptr && restored.changes.size() == 2U,
        "A restored file must leave the list");

    // The last saved version of a changed scene file, compared with the scene as it is now.
    const std::string sceneFile = "Scenes/Level.21kbscene";
    fs::create_directories(repository / "Scenes", error);
    Require(kb::scene::SceneDocumentService::Save(LevelScene(false), repository / sceneFile), "The scene must be written");
    const std::string stageScene = "git -C \"" + repository.string() + "\" add -A";
    Require(std::system(stageScene.c_str()) == 0, "The scene must be added to the index");
    const std::string commit = "git -C \"" + repository.string() + "\" commit -q -m \"Add scene\"";
    Require(std::system(commit.c_str()) == 0, "The scene must be committed");
    const EditorSceneComparison unchanged = EditorSceneComparer::CompareWithSaved(repository, sceneFile, LevelScene(false));
    Require(unchanged.succeeded && unchanged.objects.empty(), "The scene as saved must have no differences from the last saved version");
    const EditorSceneComparison fromGit = EditorSceneComparer::CompareWithSaved(repository, sceneFile, LevelScene(true));
    Require(fromGit.succeeded && fromGit.objects.size() == 3U, "The scene must differ from the last saved version by its three changes");
    const EditorSceneComparison missing = EditorSceneComparer::CompareWithSaved(repository, "Scenes/Nothing.21kbscene", LevelScene(true));
    Require(!missing.succeeded && !missing.error.empty(), "A scene the last saved version does not have must say so");

    // No repository above the folder, and then no git program at all: unavailable, with nothing thrown.
    const fs::path plain = root / "plain";
    fs::create_directories(plain, error);
    const EditorChangeList outside = EditorProjectRepository::Changes(plain);
    Require(!outside.available && outside.changes.empty(), "A project outside any repository must be unavailable");
    char* path = nullptr;
    std::size_t pathLength = 0U;
    static_cast<void>(_dupenv_s(&path, &pathLength, "PATH"));
    const std::string savedPath = path != nullptr ? path : "";
    std::free(path);
    _putenv_s("PATH", "");
    const EditorChangeList noGit = EditorProjectRepository::Changes(repository);
    _putenv_s("PATH", savedPath.c_str());
    Require(!noGit.available && noGit.changes.empty(), "Without git the changes must be unavailable");
    Require(!EditorProjectRepository::SavedContent(plain, "x.txt").has_value(), "Outside a repository there is no saved content");

    fs::remove_all(root, error);
}

} // namespace

namespace kb::editor::tests {

void RunEditorVersionControlTests() {
    RunSceneComparisonTest();
    RunLargeSceneComparisonTest();
    RunChangesAndRestoreTest();
}

} // namespace kb::editor::tests

#else

namespace kb::editor::tests {
void RunEditorVersionControlTests() {}
} // namespace kb::editor::tests

#endif
