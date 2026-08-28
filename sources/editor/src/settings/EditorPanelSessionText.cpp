#include "settings/EditorPanelSessionText.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <locale>
#include <sstream>
#include <system_error>

namespace kb::editor {
namespace {

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

[[nodiscard]] std::filesystem::path FromRelative(
    std::string_view text, const std::filesystem::path& projectRoot) {
    if (text.empty()) {
        return {};
    }
    const std::filesystem::path stored{text};
    return stored.is_absolute() ? stored : projectRoot / stored;
}

} // namespace

bool EditorPanelSessionText::Format(
    const EditorPanelSession& session,
    const std::filesystem::path& projectRoot,
    std::string& value) {
    value.clear();
    std::string relativeDocument;
    if (!TryMakeRelative(session.documentPath, projectRoot, relativeDocument)) {
        return false;
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << AreaName(session.area) << ' ' << (session.visible ? '1' : '0') << ' '
           << session.floatingRect.x << ' ' << session.floatingRect.y << ' '
           << session.floatingRect.width << ' ' << session.floatingRect.height;
    if (!relativeDocument.empty()) {
        stream << ' ' << relativeDocument;
    }
    value = stream.str();
    return true;
}

std::string EditorPanelSessionText::Key(std::uint32_t panelId) {
    return "Panel." + std::to_string(panelId);
}

bool EditorPanelSessionText::Parse(
    std::string_view key,
    std::string_view value,
    const std::filesystem::path& projectRoot,
    EditorPanelSession& session) {
    constexpr std::string_view prefix = "Panel.";
    if (!key.starts_with(prefix)) {
        return false;
    }
    const std::string_view digits = key.substr(prefix.size());
    std::uint32_t panelId = 0U;
    const std::from_chars_result parsed =
        std::from_chars(digits.data(), digits.data() + digits.size(), panelId);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || panelId == 0U) {
        return false;
    }

    std::istringstream stream{std::string{value}};
    stream.imbue(std::locale::classic());
    std::string area;
    int visible = 0;
    DockRect rect{};
    stream >> area >> visible >> rect.x >> rect.y >> rect.width >> rect.height;
    if (stream.fail() || rect.width <= 0 || rect.height <= 0) {
        return false;
    }

    session.panelId = panelId;
    session.area = ParseArea(area, DockArea::Center);
    session.visible = visible != 0;
    session.floatingRect = rect;
    std::string document;
    stream >> document;
    session.documentPath = FromRelative(document, projectRoot);
    return true;
}

} // namespace kb::editor
