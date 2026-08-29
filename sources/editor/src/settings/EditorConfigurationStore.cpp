#include "settings/EditorConfigurationStore.hpp"

#include "settings/EditorPanelSessionText.hpp"

#include "engine/config/IniDocument.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <string_view>
#include <system_error>

namespace kb::editor {
namespace {

constexpr std::string_view kSaving = "Editor.Saving";
constexpr std::string_view kPanels = "Editor.Panels";
constexpr std::string_view kLayout = "Editor.Layout";

} // namespace

const EditorPanelSession* EditorConfiguration::FindPanel(std::uint32_t id) const noexcept {
    const auto entry = std::ranges::find(panels, id, &EditorPanelSession::panelId);
    return entry == panels.end() ? nullptr : &*entry;
}

std::filesystem::path EditorConfigurationStore::FilePath(const std::filesystem::path& projectRoot) {
    return projectRoot / "Config" / "EditorSettings.ini";
}

EditorConfigurationLoadResult EditorConfigurationStore::Load(
    const std::filesystem::path& path,
    const std::filesystem::path& projectRoot) {
    EditorConfigurationLoadResult result;

    std::error_code filesystemError;
    if (!std::filesystem::exists(path, filesystemError) || filesystemError) {
        return result;
    }

    config::IniDocument document;
    if (!document.Load(path, result.error)) {
        return result;
    }
    result.found = true;

    const EditorConfiguration defaults;
    result.configuration.saving.autosaveEnabled =
        document.GetBool(kSaving, "Autosave").value_or(defaults.saving.autosaveEnabled);
    const std::int64_t minutes = document.GetInt(kSaving, "AutosaveIntervalMinutes")
        .value_or(static_cast<std::int64_t>(defaults.saving.autosaveIntervalMinutes));
    result.configuration.saving.autosaveIntervalMinutes =
        static_cast<std::uint32_t>(std::clamp<std::int64_t>(minutes, 1, 120));

    // One key per panel, named by its id, so a panel this build does not know about
    // is simply not restored rather than dropped from the file.
    for (const auto& [key, value] : document.SectionEntries(kPanels)) {
        EditorPanelSession session;
        if (!EditorPanelSessionText::Parse(key, value, projectRoot, session)) {
            continue;
        }
        result.configuration.panels.push_back(std::move(session));
    }
    if (const std::optional<std::string_view> layout = document.GetString(kLayout, "Tree")) {
        result.configuration.layout = std::string{*layout};
    }
    if (const std::optional<std::string_view> name = document.GetString(kLayout, "Name")) {
        result.configuration.layoutName = std::string{*name};
    }

    return result;
}

bool EditorConfigurationStore::Save(
    const std::filesystem::path& path,
    const std::filesystem::path& projectRoot,
    const EditorConfiguration& configuration,
    std::string& error) {
    error.clear();

    // Read first so a key a person added by hand, or one a newer build wrote,
    // survives this save instead of being erased by it.
    config::IniDocument document;
    std::error_code filesystemError;
    if (std::filesystem::exists(path, filesystemError) && !filesystemError) {
        std::string loadError;
        if (!document.Load(path, loadError)) {
            document.Clear();
        }
    }

    document.SetBool(kSaving, "Autosave", configuration.saving.autosaveEnabled);
    document.SetInt(kSaving, "AutosaveIntervalMinutes",
        static_cast<std::int64_t>(std::clamp<std::uint32_t>(configuration.saving.autosaveIntervalMinutes, 1U, 120U)));
    for (const EditorPanelSession& session : configuration.panels) {
        if (session.panelId == 0U) {
            continue;
        }
        std::string value;
        if (!EditorPanelSessionText::Format(session, projectRoot, value)) {
            error = "Editor settings cannot record a document outside the project.";
            return false;
        }
        document.SetString(kPanels, EditorPanelSessionText::Key(session.panelId), std::move(value));
    }
    document.SetString(kLayout, "Tree", configuration.layout);
    document.SetString(kLayout, "Name", configuration.layoutName);
    return document.Save(path, error);
}

} // namespace kb::editor
