#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets {
class AssetRegistry;
struct AssetMetadata;
}

namespace kb::scene {
class Scene;
}

namespace kb::core {
class JsonValue;
}

namespace kb::cli {

struct CommandIo {
    std::ostream& out;
    std::ostream& err;
    // Set by --json: the answer of a command that reports one as data (CliJsonOutput::Structured) goes here, as the
    // same facts its text prints. Null otherwise, so a command builds it only when asked.
    kb::core::JsonValue* data = nullptr;

    [[nodiscard]] bool WantsData() const noexcept { return data != nullptr; }
};

// A data object that starts with its schema id ("21kb.cli.<command>/v1"); the command adds its fields and hands it
// to PublishData.
[[nodiscard]] kb::core::JsonValue DataObject(std::string_view schema);
// Gives `data` to the result object of --json; does nothing without --json.
void PublishData(CommandIo io, kb::core::JsonValue data);
// The text a command prints, as a data value: parsed when it is a JSON document (a source asset), else the text itself.
[[nodiscard]] kb::core::JsonValue JsonDocumentOrText(std::string_view text);

// Parses "--name value" options, "--name" boolean flags (from a caller-provided
// flag list), and bare positionals from a command's argument tail.
class ArgumentList final {
public:
    ArgumentList(std::span<const std::string> arguments, std::span<const std::string_view> flagNames = {});

    [[nodiscard]] std::optional<std::string> Option(std::string_view name) const;
    // Every value of an option that may be given more than once, in command-line order.
    [[nodiscard]] std::vector<std::string> Options(std::string_view name) const;
    [[nodiscard]] bool Flag(std::string_view name) const noexcept;
    [[nodiscard]] const std::vector<std::string>& Positionals() const noexcept;
    [[nodiscard]] const std::vector<std::string>& Errors() const noexcept;
    [[nodiscard]] std::vector<std::string> UnknownOptions(std::span<const std::string_view> allowed) const;
    // A copy with an option set to one value (added when missing), or with every value of it removed.
    [[nodiscard]] ArgumentList WithOption(std::string_view name, std::string value) const;
    [[nodiscard]] ArgumentList WithoutOption(std::string_view name) const;

private:
    std::vector<std::pair<std::string, std::string>> options_;
    std::vector<std::string> flags_;
    std::vector<std::string> positionals_;
    std::vector<std::string> errors_;
};

// The files of a folder tree, by path relative to its root.
struct Listing {
    std::map<std::filesystem::path, std::uintmax_t> files;
    std::map<std::filesystem::path, std::filesystem::file_time_type> times;
    std::set<std::filesystem::path> directories;
};
// Whether a path relative to a scanned root lies in such a folder (its first component names one).
[[nodiscard]] bool IsExcludedFolder(const std::filesystem::path& relative);
// Lists every file and folder below `root`, except folders of generated or foreign data (Library, Saved, .git ...).
// Strict: a symbolic link or other irregular entry is an error; otherwise it is left out.
[[nodiscard]] bool ScanProject(const std::filesystem::path& root, Listing& listing, std::string& error, bool strict = true);
// Whether two files hold the same bytes.
[[nodiscard]] bool SameContent(const std::filesystem::path& left, const std::filesystem::path& right);

// What a command changed on disk, the one source of the "changes" of a command's data: the files of the project and
// of the output paths the arguments name. On Windows the file system reports each change while the command runs
// (ReadDirectoryChangesW, started by the constructor), so the cost follows the number of changes, not the size of
// the project. Platforms without that API list the same folders before and after the run instead (a full scan, so
// the cost follows the size of the project).
class ChangeWatch final {
public:
    ChangeWatch(std::string_view command, const ArgumentList& arguments);
    ~ChangeWatch();
    ChangeWatch(const ChangeWatch&) = delete;
    ChangeWatch& operator=(const ChangeWatch&) = delete;

    // Sets data.changes [{path, kind: created|modified|removed, asset}]: path is relative to the project, or absolute
    // outside it; asset is the /Game/ path of a project asset. A command that gave no data gets the object
    // "21kb.cli.<command>-write/v1" for it; data the command gave itself gets "changes" only when something changed.
    // With `onlyIfChanged`, nothing is set when no file changed. Returns whether it set anything. When the list could
    // not be complete (the system's notification buffer overflowed) data.changesError says so.
    bool Publish(kb::core::JsonValue& data, bool onlyIfChanged = false) const;

private:
    struct State;

    std::string command_;
    std::filesystem::path project_;
    std::vector<std::filesystem::path> roots_;
    std::unique_ptr<State> state_;
};

// Mounts <projectRoot>/Assets and runs asset discovery. Returns false with a
// populated error when the root is missing or the mount fails.
[[nodiscard]] bool MountProjectAssets(
    kb::scene::Scene& scene,
    const std::filesystem::path& projectRoot,
    std::string& error,
    std::size_t* discoveredCount = nullptr);

// Resolves user-supplied paths: absolute paths pass through; relative paths
// are anchored at the project root when it is set and the file exists there,
// falling back to the current working directory.
[[nodiscard]] std::filesystem::path ResolveInputPath(
    const std::filesystem::path& input,
    const std::filesystem::path& projectRoot);

// A scene, world or output file of the project given by --project: relative paths are anchored as
// ResolveInputPath does, and the result must lie inside the project - kb_cli never writes into
// another project or elsewhere on disk through a project command. Without a project the path is
// taken as given. Empty with `error` set when the path leaves the project.
[[nodiscard]] std::filesystem::path ResolveProjectFile(
    const std::filesystem::path& input,
    const std::filesystem::path& projectRoot,
    std::string& error);

// A path an argument names, as every command takes it: "/Game/..." is a file of the project's content (the folder the
// project mounts as /Game, which needs --project), any other path is resolved as ResolveProjectFile does. Empty with
// `error` set when the path leaves the project or /Game/ is used without a project.
[[nodiscard]] std::filesystem::path ResolveProjectArgument(
    const std::string& text,
    const std::optional<std::string>& project,
    std::string& error);

// The scene --scene names, or without it the scene the project's game starts in (--project is needed for that), as
// ResolveProjectArgument resolves it. `error` is set when it is empty.
[[nodiscard]] std::filesystem::path ResolveSceneArgument(const ArgumentList& arguments, std::string& error);

// What a --scene text names: a path (kept as written), or a scene of the project by its name (Level2, Scenes/Level2.21kbscene)
// as its /Game/ path. A bare name that is no scene of the project sets `error` (with the nearest scene named).
[[nodiscard]] std::string CanonicalSceneText(const std::optional<std::string>& project, std::string_view text, std::string& error);

// Every scene of the project (a .21kbscene under its content), as /Game/ paths in name order.
[[nodiscard]] std::vector<std::string> ProjectScenePaths(const std::filesystem::path& projectRoot);

// A scene of the project as a person writes it - /Game/Scenes/Level2.21kbscene, Assets/Scenes/Level2.21kbscene or just
// Level2 - as its /Game/ path. nullopt with `error` set (and the nearest scene named) when the project has none like it.
[[nodiscard]] std::optional<std::string> ResolveProjectScene(
    const std::filesystem::path& projectRoot, std::string_view text, std::string& error);

// A scene one of the project's Lua scripts opens (Scene.Load with its path, or any scene path written in the script).
struct ScriptSceneReference {
    std::string scene;  // /Game/ path of a scene of the project
    std::string script; // /Game/ path of the script that names it
};
// Every scene of the project that a Lua script names, with the first script that does, in scene order.
[[nodiscard]] std::vector<ScriptSceneReference> FindScriptSceneReferences(const std::filesystem::path& projectRoot);
// The scenes the project's game cannot open: those a script names that its settings leave out of the build. The
// sentence tells what to run; empty when there are none.
[[nodiscard]] std::vector<ScriptSceneReference> ScenesReferencedButNotInBuild(const std::filesystem::path& projectRoot);

// A colour as a person writes it: "#RRGGBB" (or "#RRGGBBAA" when `channels` is 4) is sRGB, as
// every colour picker shows it, and comes back linear; "r,g,b[,a]" are linear values in 0..1.
[[nodiscard]] std::optional<std::array<float, 4>> ParseColorText(std::string_view text, std::size_t channels);
// A number as kb_cli prints it: at most four decimals, no trailing zeros and no "-0", so it can be
// pasted back into --set or --position (1, 0.5, -2.25).
[[nodiscard]] std::string FormatCliNumber(double value);
// "x,y,z" as three finite numbers; nullopt for anything else.
[[nodiscard]] std::optional<kb::math::Vec3> ParseVec3(std::string_view text);

// Appends `entity` and everything below it, parents before children.
void CollectEntities(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::vector<kb::scene::SceneEntity>& out);
// Every entity of the scene, parents before children.
[[nodiscard]] std::vector<kb::scene::SceneEntity> AllEntities(const kb::scene::Scene& scene);
// "Parent/Child/Name".
[[nodiscard]] std::string EntityPath(const kb::scene::Scene& scene, kb::scene::SceneEntity entity);
// The entities `name` addresses, in hierarchy order: by name, or by path ("Parent/Name").
[[nodiscard]] std::vector<kb::scene::SceneEntity> MatchEntities(const kb::scene::Scene& scene, std::string_view name);

// Finds an asset by virtual path ("/Game/...") or by physical file path.
[[nodiscard]] const kb::assets::AssetMetadata* FindAssetByFlexiblePath(
    const kb::assets::AssetRegistry& registry,
    const std::filesystem::path& input);

} // namespace kb::cli
