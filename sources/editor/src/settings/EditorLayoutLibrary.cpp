#include "settings/EditorLayoutLibrary.hpp"

#include "settings/EditorPanelSessionText.hpp"

#include "engine/config/IniDocument.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace kb::editor {
namespace {

constexpr std::string_view kLayout = "Layout";
constexpr std::string_view kPanels = "Layout.Panels";
constexpr std::string_view kExtension = ".kblayout";

[[nodiscard]] bool IsNameCharacter(unsigned char value) noexcept {
    return std::isalnum(value) != 0 || value == ' ' || value == '-' || value == '_';
}

} // namespace

bool EditorLayoutLibrary::IsValidName(std::string_view name) noexcept {
    if (name.empty() || name.size() > MaximumNameLength) {
        return false;
    }
    if (name.front() == ' ' || name.back() == ' ') {
        return false;
    }
    return std::ranges::all_of(name, [](char value) {
        return IsNameCharacter(static_cast<unsigned char>(value));
    });
}

std::filesystem::path EditorLayoutLibrary::DirectoryPath(const std::filesystem::path& projectRoot) {
    return projectRoot / "Config" / "Layouts";
}

std::filesystem::path EditorLayoutLibrary::FilePath(
    const std::filesystem::path& projectRoot, std::string_view name) {
    if (!IsValidName(name)) {
        return {};
    }
    return DirectoryPath(projectRoot) / (std::string{name} + std::string{kExtension});
}

std::vector<std::string> EditorLayoutLibrary::List(const std::filesystem::path& projectRoot) {
    std::vector<std::string> names;
    std::error_code error;
    const std::filesystem::path directory = DirectoryPath(projectRoot);
    if (!std::filesystem::is_directory(directory, error) || error) {
        return names;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator{directory, error}) {
        if (error) {
            break;
        }
        if (!entry.is_regular_file(error) || error || entry.path().extension() != kExtension) {
            continue;
        }
        std::string name = entry.path().stem().string();
        // A file dropped in by hand under a name the editor could not have written is
        // ignored rather than offered: clicking it would only ever fail.
        if (!IsValidName(name)) {
            continue;
        }
        names.push_back(std::move(name));
        if (names.size() >= MaximumLayouts) {
            break;
        }
    }
    std::ranges::sort(names);
    return names;
}

bool EditorLayoutLibrary::Save(
    const std::filesystem::path& projectRoot,
    std::string_view name,
    const EditorLayoutPreset& preset,
    std::string& error) {
    error.clear();
    const std::filesystem::path path = FilePath(projectRoot, name);
    if (path.empty()) {
        error = "A layout name may only use letters, digits, spaces, dashes and underscores.";
        return false;
    }
    if (preset.tree.empty()) {
        error = "The workspace has no arrangement to save.";
        return false;
    }

    std::error_code filesystemError;
    if (!std::filesystem::exists(path, filesystemError) &&
        List(projectRoot).size() >= MaximumLayouts) {
        error = "This project already holds as many layouts as the editor keeps.";
        return false;
    }

    config::IniDocument document;
    document.SetString(kLayout, "Tree", preset.tree);
    for (const EditorPanelSession& session : preset.panels) {
        if (session.panelId == 0U) {
            continue;
        }
        std::string value;
        if (!EditorPanelSessionText::Format(session, projectRoot, value)) {
            error = "A layout cannot record a document outside the project.";
            return false;
        }
        document.SetString(kPanels, EditorPanelSessionText::Key(session.panelId), std::move(value));
    }
    return document.Save(path, error);
}

std::optional<EditorLayoutPreset> EditorLayoutLibrary::Load(
    const std::filesystem::path& projectRoot, std::string_view name) {
    const std::filesystem::path path = FilePath(projectRoot, name);
    std::error_code filesystemError;
    if (path.empty() || !std::filesystem::exists(path, filesystemError) || filesystemError) {
        return std::nullopt;
    }

    config::IniDocument document;
    std::string error;
    if (!document.Load(path, error)) {
        return std::nullopt;
    }
    const std::optional<std::string_view> tree = document.GetString(kLayout, "Tree");
    if (!tree.has_value() || tree->empty()) {
        return std::nullopt;
    }

    EditorLayoutPreset preset;
    preset.tree = std::string{*tree};
    for (const auto& [key, value] : document.SectionEntries(kPanels)) {
        EditorPanelSession session;
        if (EditorPanelSessionText::Parse(key, value, projectRoot, session)) {
            preset.panels.push_back(std::move(session));
        }
    }
    return preset;
}

bool EditorLayoutLibrary::Delete(const std::filesystem::path& projectRoot, std::string_view name) {
    const std::filesystem::path path = FilePath(projectRoot, name);
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    return std::filesystem::remove(path, error) && !error;
}

} // namespace kb::editor
