#include "rendering/PackagesPanel.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorTextEntryDialog.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "project/EditorPackages.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace kb::editor {
namespace {

constexpr int kPad = 12;
constexpr int kRow = 22;
constexpr int kButtonHeight = 26;
constexpr int kButtonGap = 8;
constexpr int kPointSize = 12;
constexpr int kMessageRows = 5;
constexpr COLORREF kAmber = RGB(224, 165, 54);
constexpr COLORREF kGreen = RGB(63, 194, 123);
constexpr COLORREF kRed = RGB(232, 108, 108);

enum class Button : std::size_t { Add, Update, Remove, Verify, Registry };
constexpr std::size_t kButtonCount = 5U;

struct Layout {
    RECT registry{};
    std::array<RECT, kButtonCount> buttons{};
    RECT header{};
    RECT list{};
    RECT message{};
};

[[nodiscard]] COLORREF Color(const EditorColor& color) noexcept {
    return RGB(color.r, color.g, color.b);
}

[[nodiscard]] bool Inside(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

void Text(HDC dc, RECT rect, std::string_view text, COLORREF color, int weight = FW_NORMAL, UINT format = DT_LEFT | DT_SINGLELINE | DT_VCENTER) {
    const ScopedFont font(kPointSize, weight);
    const ScopedGdiObject selected(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextUtf8(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_END_ELLIPSIS | DT_NOPREFIX);
}

[[nodiscard]] Layout MakeLayout(const RECT& content) {
    Layout layout;
    const RECT inner{ content.left + kPad, content.top + kPad, content.right - kPad, content.bottom - kPad };
    layout.registry = RECT{ inner.left, inner.top, inner.right, inner.top + 20 };
    const int width = std::clamp((static_cast<int>(inner.right - inner.left) - static_cast<int>(kButtonCount - 1U) * kButtonGap) / static_cast<int>(kButtonCount), 70, 170);
    const int top = static_cast<int>(layout.registry.bottom) + 8;
    for (std::size_t index = 0U; index < kButtonCount; ++index) {
        const int left = inner.left + static_cast<int>(index) * (width + kButtonGap);
        layout.buttons[index] = RECT{ left, top, left + width, top + kButtonHeight };
    }
    layout.header = RECT{ inner.left, top + kButtonHeight + 10, inner.right, top + kButtonHeight + 10 + kRow };
    const int messageHeight = kMessageRows * kRow;
    const int messageTop = std::max(static_cast<int>(layout.header.bottom) + kRow, static_cast<int>(inner.bottom) - messageHeight);
    layout.list = RECT{ inner.left, layout.header.bottom, inner.right, messageTop - 6 };
    layout.message = RECT{ inner.left, messageTop, inner.right, std::max(messageTop, static_cast<int>(inner.bottom)) };
    return layout;
}

[[nodiscard]] int VisibleRows(const RECT& area) noexcept {
    return std::max(0, static_cast<int>(area.bottom - area.top) / kRow);
}

[[nodiscard]] const EditorPackageRow* SelectedRow(const EditorPackages& packages) {
    return packages.Selected() < packages.Rows().size() ? &packages.Rows()[packages.Selected()] : nullptr;
}

[[nodiscard]] bool IsEnabled(const EditorPackages& packages, Button button) {
    if (packages.Busy()) {
        return false;
    }
    const EditorPackageRow* row = SelectedRow(packages);
    switch (button) {
    case Button::Add: return row != nullptr && row->installed.empty() && !row->latest.empty();
    case Button::Update:
        return std::ranges::any_of(packages.Rows(), [](const EditorPackageRow& other) { return !other.installed.empty(); });
    case Button::Remove: return row != nullptr && row->asked;
    case Button::Verify:
    case Button::Registry: return true;
    }
    return false;
}

[[nodiscard]] std::string Label(Button button) {
    switch (button) {
    case Button::Add: return std::string{ EditorText("packages.add", "Add") };
    case Button::Update: return std::string{ EditorText("packages.update", "Update") };
    case Button::Remove: return std::string{ EditorText("packages.remove", "Remove") };
    case Button::Verify: return std::string{ EditorText("packages.verify", "Verify") };
    case Button::Registry: return std::string{ EditorText("packages.registryButton", "Registry...") };
    }
    return {};
}

void DrawButton(HDC dc, const RECT& rect, const EditorTheme& theme, std::string_view label, bool enabled) {
    GdiDrawing::DrawSharpFrame(dc, rect, Color(theme.toolbarButton), Color(theme.borderPanel));
    Text(dc, RECT{ rect.left + 6, rect.top, rect.right - 6, rect.bottom }, label, Color(enabled ? theme.textPrimary : theme.textDisabled), FW_NORMAL,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

// The columns of the list: name, installed version, newest version, note.
struct Columns {
    int name;
    int installed;
    int latest;
    int note;
};

[[nodiscard]] Columns MakeColumns(const RECT& area) noexcept {
    const int width = static_cast<int>(area.right - area.left);
    return Columns{ area.left + 6, area.left + 6 + width * 30 / 100, area.left + 6 + width * 42 / 100, area.left + 6 + width * 54 / 100 };
}

} // namespace

void PackagesPanel::Paint(HDC dc, const RECT& content, const EditorTheme& theme) {
    const EditorPackages& packages = EditorPackages::Instance();
    const Layout layout = MakeLayout(content);
    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));

    const std::string registry = packages.Registry().empty()
        ? std::string{ EditorText("packages.registryNone", "No package registry set. Choose Registry... and give a folder or an https address.") }
        : EditorLocalization::Format("packages.registryLine", "Registry: {registry}", { { "registry", packages.Registry() } });
    Text(dc, layout.registry, registry, Color(packages.Registry().empty() ? theme.textDisabled : theme.textPrimary), FW_SEMIBOLD);
    for (std::size_t index = 0U; index < kButtonCount; ++index) {
        DrawButton(dc, layout.buttons[index], theme, Label(static_cast<Button>(index)), IsEnabled(packages, static_cast<Button>(index)));
    }

    const Columns columns = MakeColumns(layout.list);
    const RECT header = layout.header;
    Text(dc, RECT{ columns.name, header.top, columns.installed - 4, header.bottom }, EditorText("packages.columnName", "Package"), Color(theme.textSecondary), FW_SEMIBOLD);
    Text(dc, RECT{ columns.installed, header.top, columns.latest - 4, header.bottom }, EditorText("packages.columnInstalled", "Installed"), Color(theme.textSecondary), FW_SEMIBOLD);
    Text(dc, RECT{ columns.latest, header.top, columns.note - 4, header.bottom }, EditorText("packages.columnLatest", "Available"), Color(theme.textSecondary), FW_SEMIBOLD);
    Text(dc, RECT{ columns.note, header.top, header.right - 6, header.bottom }, EditorText("packages.columnNote", "Note"), Color(theme.textSecondary), FW_SEMIBOLD);

    const std::vector<EditorPackageRow>& rows = packages.Rows();
    if (rows.empty()) {
        Text(dc, RECT{ layout.list.left, layout.list.top, layout.list.right, layout.list.top + kRow },
            EditorText("packages.empty", "No packages yet."), Color(theme.textDisabled));
    }
    for (int row = 0; row < VisibleRows(layout.list); ++row) {
        const std::size_t index = static_cast<std::size_t>(packages.scroll + row);
        if (index >= rows.size()) {
            break;
        }
        const EditorPackageRow& entry = rows[index];
        const RECT line{ layout.list.left, layout.list.top + row * kRow, layout.list.right, layout.list.top + (row + 1) * kRow };
        if (index == packages.Selected()) {
            GdiDrawing::FillRectColor(dc, line, Color(theme.header));
        }
        Text(dc, RECT{ columns.name, line.top, columns.installed - 4, line.bottom }, entry.name, Color(theme.textPrimary));
        Text(dc, RECT{ columns.installed, line.top, columns.latest - 4, line.bottom }, entry.installed.empty() ? "-" : entry.installed,
            entry.installed.empty() ? Color(theme.textDisabled) : kGreen);
        Text(dc, RECT{ columns.latest, line.top, columns.note - 4, line.bottom }, entry.latest.empty() ? "-" : entry.latest,
            Color(entry.latest.empty() ? theme.textDisabled : theme.textPrimary));
        Text(dc, RECT{ columns.note, line.top, line.right - 6, line.bottom }, entry.note, entry.updatable ? kAmber : Color(theme.textSecondary));
    }

    // What the last operation did: the sentence, wrapped, and the files verify found changed.
    if (packages.Busy()) {
        Text(dc, layout.message, EditorText("packages.working", "Working..."), Color(theme.textDisabled));
        return;
    }
    const int sentenceRows = 2;
    Text(dc, RECT{ layout.message.left, layout.message.top, layout.message.right, layout.message.top + sentenceRows * kRow }, packages.Message(),
        packages.MessageIsError() ? kRed : Color(theme.textPrimary), FW_NORMAL, DT_LEFT | DT_WORDBREAK);
    for (int row = sentenceRows; row < VisibleRows(layout.message) && static_cast<std::size_t>(row - sentenceRows) < packages.Problems().size(); ++row) {
        Text(dc, RECT{ layout.message.left + 12, layout.message.top + row * kRow, layout.message.right, layout.message.top + (row + 1) * kRow },
            packages.Problems()[static_cast<std::size_t>(row - sentenceRows)], kAmber);
    }
}

bool PackagesPanel::Pointer(HWND owner, const RECT& content, int x, int y) {
    EditorPackages& packages = EditorPackages::Instance();
    const Layout layout = MakeLayout(content);
    if (Inside(layout.list, x, y)) {
        const std::size_t index = static_cast<std::size_t>(packages.scroll + (y - layout.list.top) / kRow);
        packages.Select(index < packages.Rows().size() ? index : EditorPackages::kNone);
        return true;
    }
    for (std::size_t index = 0U; index < kButtonCount; ++index) {
        const Button button = static_cast<Button>(index);
        if (!Inside(layout.buttons[index], x, y) || !IsEnabled(packages, button)) {
            continue;
        }
        const EditorPackageRow* row = SelectedRow(packages);
        switch (button) {
        case Button::Add: packages.Add(row->name); break;
        // A chosen installed package is updated alone, otherwise every package is.
        case Button::Update: packages.Update(row != nullptr && !row->installed.empty() ? row->name : std::string{}); break;
        case Button::Remove: packages.Remove(row->name); break;
        case Button::Verify: packages.Verify(); break;
        case Button::Registry: {
            const std::optional<std::string> location = EditorTextEntryDialog::Show(owner, EditorTextEntryDialogDescriptor{
                .title = std::string{ EditorText("packages.registryTitle", "Package registry") },
                .label = std::string{ EditorText("packages.registryLabel", "Folder or https address of the registry") },
                .value = packages.Registry(),
                .hint = std::string{ EditorText("packages.registryHint", "A folder with an index.json, or an address such as https://example.com/packages/index.json") },
                .acceptLabel = std::string{ EditorText("packages.registryAccept", "Use this registry") },
            });
            if (location.has_value() && !location->empty()) {
                packages.SetRegistry(*location);
            }
            break;
        }
        }
        return true;
    }
    return true;
}

bool PackagesPanel::Wheel(const RECT& content, int x, int y, int delta) {
    EditorPackages& packages = EditorPackages::Instance();
    const Layout layout = MakeLayout(content);
    if (!Inside(layout.list, x, y)) {
        return false;
    }
    const int maximum = std::max(0, static_cast<int>(packages.Rows().size()) - VisibleRows(layout.list));
    const int next = std::clamp(packages.scroll + (delta > 0 ? -3 : 3), 0, maximum);
    const bool moved = next != packages.scroll;
    packages.scroll = next;
    return moved;
}

} // namespace kb::editor

#endif
