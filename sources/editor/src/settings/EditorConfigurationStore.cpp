#include "settings/EditorConfigurationStore.hpp"

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

constexpr std::array<std::string_view, 5U> kAreaNames{
    "Left", "Center", "Right", "Bottom", "Floating",
};

[[nodiscard]] std::string_view AreaName(DockArea area) noexcept {
    const auto index = static_cast<std::size_t>(area);
    return index < kAreaNames.size() ? kAreaNames[index] : kAreaNames[1U];
}

[[nodiscard]] DockArea ParseArea(std::string_view name, DockArea fallback) noexcept {
    const auto entry = std::ranges::find(kAreaNames, name);
    return entry == kAreaNames.end()
        ? fallback
        : static_cast<DockArea>(std::distance(kAreaNames.begin(), entry));
}

[[nodiscard]] std::string FormatRect(const DockRect& rect) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << rect.x << ' ' << rect.y << ' ' << rect.width << ' ' << rect.height;
    return stream.str();
}

[[nodiscard]] bool ParseRect(std::string_view text, DockRect& rect) {
    std::istringstream stream{std::string{text}};
    stream.imbue(std::locale::classic());
    DockRect parsed{};
    stream >> parsed.x >> parsed.y >> parsed.width >> parsed.height;
    if (stream.fail() || parsed.width <= 0 || parsed.height <= 0) {
        return false;
    }
    rect = parsed;
    return true;
}

// Kept project-relative on disk so moving a project folder does not strand the
// document a panel was showing. A path that leaves the project is refused rather
// than stored absolute: this file records where work happened inside one project.
[[nodiscard]] bool TryMakeRelative(
    const std::filesystem::path& path,
    const std::filesystem::path& projectRoot,
    std::string& relative) {
    relative.clear();
    if (path.empty()) {
        return true;
    }
    std::error_code error;
    const std::filesystem::path result = std::filesystem::relative(path, projectRoot, error);
    if (error || result.empty() || *result.begin() == "..") {
        return false;
    }
    relative = result.generic_string();
    return true;
}

[[nodiscard]] bool ParsePanelId(std::string_view key, std::uint32_t& panelId) {
    constexpr std::string_view prefix = "Panel.";
    if (!key.starts_with(prefix)) {
        return false;
    }
    const std::string_view digits = key.substr(prefix.size());
    std::uint32_t parsed = 0U;
    const std::from_chars_result result =
        std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size() || parsed == 0U) {
        return false;
    }
    panelId = parsed;
    return true;
}

[[nodiscard]] std::filesystem::path FromRelative(std::string_view text, const std::filesystem::path& projectRoot) {
    if (text.empty()) {
        return {};
    }
    const std::filesystem::path stored{text};
    return stored.is_absolute() ? stored : projectRoot / stored;
}

// "<area> <visible> <x> <y> <w> <h> [document]"
[[nodiscard]] bool ParsePanel(
    std::string_view text,
    const std::filesystem::path& projectRoot,
    EditorPanelSession& session) {
    std::istringstream stream{std::string{text}};
    stream.imbue(std::locale::classic());
    std::string area;
    int visible = 0;
    DockRect rect{};
    stream >> area >> visible >> rect.x >> rect.y >> rect.width >> rect.height;
    if (stream.fail() || rect.width <= 0 || rect.height <= 0) {
        return false;
    }
    session.area = ParseArea(area, DockArea::Center);
    session.visible = visible != 0;
    session.floatingRect = rect;
    std::string document;
    stream >> document;
    session.documentPath = FromRelative(document, projectRoot);
    return true;
}

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
        if (!ParsePanelId(key, session.panelId) || !ParsePanel(value, projectRoot, session)) {
            continue;
        }
        result.configuration.panels.push_back(std::move(session));
    }
    if (const std::optional<std::string_view> layout = document.GetString(kLayout, "Tree")) {
        result.configuration.layout = std::string{*layout};
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
        std::string relativeDocument;
        if (!TryMakeRelative(session.documentPath, projectRoot, relativeDocument)) {
            error = "Editor settings cannot record a document outside the project.";
            return false;
        }
        std::ostringstream value;
        value.imbue(std::locale::classic());
        value << AreaName(session.area) << ' ' << (session.visible ? '1' : '0') << ' '
              << FormatRect(session.floatingRect);
        if (!relativeDocument.empty()) {
            value << ' ' << relativeDocument;
        }
        document.SetString(kPanels, "Panel." + std::to_string(session.panelId), value.str());
    }
    document.SetString(kLayout, "Tree", configuration.layout);
    return document.Save(path, error);
}

} // namespace kb::editor
