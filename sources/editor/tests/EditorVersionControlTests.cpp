#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "project/EditorProjectRepository.hpp"
#include "project/EditorVersionControl.hpp"
#include "scene/EditorSceneComparison.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

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

// An object with the id it keeps across saves.
std::uint32_t AddObject(kb::scene::SceneDocument& document, const std::string& name, std::uint32_t parent, std::uint64_t id, float x = 0.0F) {
    kb::scene::ScenePrefabNodeDesc node;
    node.stableId = id;
    node.name = name;
    node.parentNode = parent;
    node.transform.localPosition = { x, 0.0F, 0.0F };
    return document.worldPrefab.AddNode(std::move(node));
}

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
    // The crate and the window are two objects, so they do not share an id.
    static_cast<void>(AddObject(document, changed ? "Window" : "Crate", level, changed ? 100U : 4U));
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

// A scene of two versions of a level: the door is renamed, the second of two twins moves and the box goes under the shelf.
[[nodiscard]] kb::scene::SceneDocument TwinScene(bool changed, std::uint32_t fileVersion) {
    kb::scene::SceneDocument document;
    document.name = "Level";
    document.guid = "scene:Level";
    document.fileVersion = fileVersion;
    const std::uint32_t level = AddObject(document, "Level", kRoot, 1U);
    static_cast<void>(AddObject(document, changed ? "Gate" : "Door", level, 2U));
    static_cast<void>(AddObject(document, "Twin", level, 3U, 1.0F));
    static_cast<void>(AddObject(document, "Twin", level, 4U, changed ? 9.0F : 2.0F));
    const std::uint32_t shelf = AddObject(document, "Shelf", level, 5U);
    static_cast<void>(AddObject(document, "Box", changed ? shelf : level, 6U));
    return document;
}

[[nodiscard]] const kb::editor::EditorSceneObjectChange* FindChange(const EditorSceneComparison& comparison, const std::string& path) {
    const auto found = std::ranges::find(comparison.objects, path, &kb::editor::EditorSceneObjectChange::path);
    return found == comparison.objects.end() ? nullptr : &*found;
}

[[nodiscard]] const kb::editor::EditorSceneFieldChange* FindField(const kb::editor::EditorSceneObjectChange& object, const char* component, const char* property) {
    const auto found = std::ranges::find_if(object.fields, [&](const kb::editor::EditorSceneFieldChange& change) {
        return change.component == component && change.property == property;
    });
    return found == object.fields.end() ? nullptr : &*found;
}

// Objects pair by the id they keep: a rename is one change, two objects of one name stay apart and a moved object is one
// change. Files from before objects kept their ids pair by name, so there the same edits read as objects gone and new.
void RunSceneComparisonByIdTest() {
    const EditorSceneComparison comparison = EditorSceneComparer::Compare(
        TwinScene(false, kb::scene::SceneDocument::CurrentFileVersion), TwinScene(true, kb::scene::SceneDocument::CurrentFileVersion));
    Require(comparison.succeeded && comparison.objects.size() == 3U, "A rename, a move of one twin and a reparent must be three changes");
    const kb::editor::EditorSceneObjectChange* renamed = FindChange(comparison, "Level/Gate");
    Require(renamed != nullptr && renamed->kind == EditorSceneChangeKind::Changed && renamed->fields.size() == 1U, "A rename must be one changed object");
    const kb::editor::EditorSceneFieldChange* name = FindField(*renamed, "Object", "name");
    Require(name != nullptr && name->before == "Door" && name->after == "Gate", "A rename must show the old and the new name");
    const kb::editor::EditorSceneObjectChange* twin = FindChange(comparison, "Level/Twin");
    Require(twin != nullptr && twin->kind == EditorSceneChangeKind::Changed && twin->address == std::vector<std::uint32_t>{ 0U, 2U },
        "Only the second of two objects named alike must change, at its place in the newer scene");
    const kb::editor::EditorSceneFieldChange* moved = FindField(*twin, "Transform", "localPosition.x");
    Require(twin->fields.size() == 1U && moved != nullptr && moved->before == "2" && moved->after == "9", "The twin must show its own old and new position");
    const kb::editor::EditorSceneObjectChange* box = FindChange(comparison, "Level/Shelf/Box");
    const kb::editor::EditorSceneFieldChange* parent = box == nullptr ? nullptr : FindField(*box, "Object", "parent");
    Require(box != nullptr && box->kind == EditorSceneChangeKind::Changed && parent != nullptr && parent->before == "Level" && parent->after == "Level/Shelf",
        "A moved object must be one change that names its old and new parent");

    const EditorSceneComparison legacy = EditorSceneComparer::Compare(TwinScene(false, 68U), TwinScene(true, 68U));
    Require(legacy.succeeded && legacy.objects.size() == 5U &&
            std::ranges::none_of(legacy.objects, [](const kb::editor::EditorSceneObjectChange& object) { return FindField(object, "Object", "name") != nullptr; }),
        "Files that renumbered their objects must pair them by name");
    Require(FindChange(legacy, "Level/Door") != nullptr && FindChange(legacy, "Level/Gate") != nullptr && FindChange(legacy, "Level/Twin") != nullptr,
        "By name, a rename is an object gone and a new one, and the twins pair in order");
}

constexpr std::uint32_t kLargeObjects = 100000U;

// A world of a hundred thousand objects, ten of them moved when `changed`.
[[nodiscard]] kb::scene::SceneDocument LargeScene(bool changed) {
    constexpr std::uint32_t kStep = 10000U;
    kb::scene::SceneDocument document;
    document.name = "Large";
    document.worldPrefab.Reserve(kLargeObjects + 1U);
    const std::uint32_t world = AddNode(document, "World", kRoot);
    for (std::uint32_t index = 0U; index < kLargeObjects; ++index) {
        static_cast<void>(AddNode(document, "E" + std::to_string(index), world, changed && index % kStep == 0U ? 1.0F : 0.0F));
    }
    return document;
}

[[nodiscard]] bool SameComparison(const EditorSceneComparison& left, const EditorSceneComparison& right) {
    const auto sameField = [](const kb::editor::EditorSceneFieldChange& a, const kb::editor::EditorSceneFieldChange& b) {
        return a.component == b.component && a.property == b.property && a.before == b.before && a.after == b.after;
    };
    return left.succeeded == right.succeeded && left.error == right.error &&
        std::ranges::equal(left.objects, right.objects, [&](const kb::editor::EditorSceneObjectChange& a, const kb::editor::EditorSceneObjectChange& b) {
            return a.kind == b.kind && a.path == b.path && a.address == b.address && std::ranges::equal(a.fields, b.fields, sameField);
        });
}

void RunLargeSceneComparisonTest() {
    const EditorSceneComparison comparison = EditorSceneComparer::Compare(LargeScene(false), LargeScene(true));
    Require(comparison.succeeded && comparison.objects.size() == 10U, "A hundred thousand objects with ten moved must give ten changes");
    Require(std::ranges::all_of(comparison.objects, [](const kb::editor::EditorSceneObjectChange& object) {
        return object.kind == EditorSceneChangeKind::Changed && object.fields.size() == 1U && object.fields.front().component == "Transform";
    }), "Every change must be a moved object");
}

// What the first of two comparisons saw from its own thread, shared with the test by the thread that runs it.
struct ObservedComparison {
    std::atomic<bool> started{ false };
    std::atomic<bool> onCallerThread{ true };
    std::atomic<bool> cancelled{ false };
    std::atomic<bool> finished{ false };
};

template <typename Condition>
[[nodiscard]] bool WaitFor(Condition condition) {
    for (int attempt = 0; attempt < 6000 && !condition(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
    }
    return condition();
}

// The comparison of a hundred thousand objects runs on a thread of its own, comes back unchanged, and asking again
// cancels the comparison still running.
void RunBackgroundComparisonTest() {
    kb::editor::EditorVersionControl& control = kb::editor::EditorVersionControl::Instance();
    const std::thread::id caller = std::this_thread::get_id();
    const auto observed = std::make_shared<ObservedComparison>();
    // The first comparison only ends when it is cancelled: the call that starts it can return only if it runs elsewhere.
    control.StartComparison([observed, caller](const std::atomic<bool>& cancelled) {
        observed->onCallerThread = std::this_thread::get_id() == caller;
        observed->started = true;
        while (!cancelled.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
        }
        observed->cancelled = true;
        observed->finished = true;
        return EditorSceneComparison{ .error = "cancelled" };
    });
    Require(control.Comparing() && !control.HasComparison(), "A started comparison must be running, with no result yet");
    Require(WaitFor([&] { return observed->started.load(); }) && !observed->onCallerThread, "A comparison must run on a thread of its own");

    control.StartComparison([](const std::atomic<bool>& cancelled) {
        return EditorSceneComparer::Compare(LargeScene(false), LargeScene(true), &cancelled);
    });
    Require(WaitFor([&] { return observed->finished.load(); }) && observed->cancelled, "Asking again must cancel the comparison that was running");
    Require(WaitFor([&] {
        static_cast<void>(control.Pump({}, std::chrono::steady_clock::now()));
        return !control.Comparing();
    }), "The second comparison must finish");
    const EditorSceneComparison& large = control.Comparison();
    Require(control.HasComparison() && large.succeeded && large.objects.size() == 10U &&
            std::ranges::all_of(large.objects, [](const kb::editor::EditorSceneObjectChange& object) {
                return object.kind == EditorSceneChangeKind::Changed && object.fields.size() == 1U && object.fields.front().component == "Transform";
            }),
        "A hundred thousand objects with ten moved must give ten changes in the background too");

    // A small scene gives in the background what it gives directly.
    control.StartComparison([](const std::atomic<bool>& cancelled) {
        return EditorSceneComparer::Compare(LevelScene(false), LevelScene(true), &cancelled);
    });
    Require(WaitFor([&] {
        static_cast<void>(control.Pump({}, std::chrono::steady_clock::now()));
        return !control.Comparing();
    }) && SameComparison(control.Comparison(), EditorSceneComparer::Compare(LevelScene(false), LevelScene(true))),
        "The comparison in the background must give the result of the direct one");
    control.ClearComparison();
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
    RunSceneComparisonByIdTest();
    RunLargeSceneComparisonTest();
    RunBackgroundComparisonTest();
    RunChangesAndRestoreTest();
}

} // namespace kb::editor::tests

#else

namespace kb::editor::tests {
void RunEditorVersionControlTests() {}
} // namespace kb::editor::tests

#endif
