#pragma once

#include "CliCommon.hpp"

#include "engine/scene/SceneDocumentService.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <vector>

// The file a command that makes a new project asset writes: <Assets>/<folder>/<name><extension>. One set of rules
// for image new, sound new, timeline new, ui-theme new and localization: the name is a plain name (no path, no
// system device name), the folder lies inside the project's Assets (links followed), and an existing file is kept
// unless --replace says so.
namespace kb::cli {

struct AssetTarget {
    std::filesystem::path projectRoot;
    std::filesystem::path file;
    std::string name;
    std::string folderVirtual; // /Game/Images
    std::string virtualPath;   // /Game/Images/Heart.png
    std::string project;       // --project as given
};

// `usage` is the text after "requires" in the error for a missing --project or --name. With `allowExisting` an
// existing file is returned instead of refused (the caller then edits it).
[[nodiscard]] std::optional<AssetTarget> ResolveAssetTarget(const ArgumentList& arguments, std::string_view command, std::string_view usage,
    std::string_view defaultFolder, std::string_view extension, bool allowExisting, CommandIo io);

// Runs another kb_cli command of this process (entity, component, script-new, scene-attach ...) as a step of a larger
// one. Returns whether it succeeded; `message` gets what it said, so a failure can be passed on.
[[nodiscard]] bool RunSiblingCommand(int (*command)(const ArgumentList&, CommandIo), const std::vector<std::string>& words, std::string& message);

// The scene file --scene names (the startup scene without it), loaded. Empty with `error` set when it cannot be.
struct ProjectSceneDocument {
    std::filesystem::path file;
    std::string virtualPath;
    kb::scene::SceneDocument document;
};
[[nodiscard]] std::optional<ProjectSceneDocument> LoadProjectScene(const ArgumentList& arguments, std::string& error);
// An empty object `object` (kept when it is there) with the Lua script `scriptName` on it, whose whole text is `code`: a script the
// command wrote before is written again, so it follows what the command knows now. Returns false with `error` set.
[[nodiscard]] bool AttachScriptOnObject(const ArgumentList& arguments, const std::string& object, const std::string& scriptName, const std::string& code,
    std::string& error);
// "Parent/Child/Name" of a node of the document.
[[nodiscard]] std::string NodePath(std::span<const kb::scene::ScenePrefabNodeDesc> nodes, std::size_t index);

// A scene object found by name or path, with its own (local) transform as the scene file stores it.
struct SceneObjectTransform {
    kb::scene::LocalTransform transform;
    std::string path;
};
[[nodiscard]] std::optional<SceneObjectTransform> FindSceneObject(const ProjectSceneDocument& scene, std::string_view name);

} // namespace kb::cli
