#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/ScenePrefabNode.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace kb::script {

// A mistake in a scene's UI that is legal data, so loading it succeeds, but that leaves the widget
// invisible or dead at run time. `code` is stable and named for the mistake.
struct UISceneWarning {
    std::string code;
    // Path of the widget in the scene: Menu/PlayButton.
    std::string entity;
    std::string message;
};

struct UIProjectWarning {
    std::string scene;
    UISceneWarning warning;
};

inline constexpr std::string_view kUIWarningNoCanvas = "ui-no-canvas";
inline constexpr std::string_view kUIWarningTextNoFont = "ui-text-no-font";
inline constexpr std::string_view kUIWarningEventNoHandler = "ui-event-no-handler";
inline constexpr std::string_view kUIWarningButtonNoReaction = "ui-button-no-reaction";

// The global functions of the project's Lua scripts, by asset id: what a widget's behaviour can answer.
class UIScriptIndex {
public:
    // Reads every LuaScript asset in `assets` that has a file.
    [[nodiscard]] static UIScriptIndex FromAssets(std::span<const kb::assets::AssetMetadata> assets);

    void AddLua(std::uint64_t assetId, std::string source);
    [[nodiscard]] bool IsLua(std::uint64_t assetId) const noexcept;
    [[nodiscard]] bool Defines(std::uint64_t assetId, std::string_view function) const noexcept;
    // Some script subscribes with Events.Subscribe and names `eventName` in quotes: it may answer the event.
    [[nodiscard]] bool SubscribesTo(std::string_view eventName) const;

private:
    struct Script {
        std::vector<std::string> functions;
        std::string source;
    };
    std::unordered_map<std::uint64_t, Script> scripts_;
};

// Checks the widgets of a saved scene (parents before children): a widget outside any Canvas, a Text
// with no font, an event name nothing answers, a Button nothing reacts to. Scripts that are not Lua
// are not judged: they may answer anything.
[[nodiscard]] std::vector<UISceneWarning> ValidateUIScene(
    std::span<const kb::scene::ScenePrefabNodeDesc> nodes, const UIScriptIndex& scripts);

struct UIProjectReport {
    std::vector<UIProjectWarning> warnings;
    // A scene that could not be read, so was not checked.
    std::vector<std::string> errors;
};

// The same for every Scene asset in `assets` (only the one at `onlyScene` when given), with the scripts
// among them.
[[nodiscard]] UIProjectReport ValidateUIProject(
    std::span<const kb::assets::AssetMetadata> assets, const std::filesystem::path& onlyScene = {});

// One line: "WARN <scene>:<widget>: [code] message".
[[nodiscard]] std::string FormatUIWarning(std::string_view scene, const UISceneWarning& warning);

} // namespace kb::script
