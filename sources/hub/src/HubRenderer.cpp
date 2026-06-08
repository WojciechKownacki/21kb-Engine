#include "HubRenderer.hpp"

#include "HubGeometry.hpp"
#include "HubProjectFilters.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace kb::hub {
namespace {

constexpr COLORREF kBackground = RGB(18, 19, 21);
constexpr COLORREF kTop = RGB(26, 28, 31);
constexpr COLORREF kPanel = RGB(30, 32, 36);
constexpr COLORREF kPanelAlt = RGB(36, 39, 44);
constexpr COLORREF kPanelSelected = RGB(34, 48, 62);
constexpr COLORREF kField = RGB(23, 25, 28);
constexpr COLORREF kBorder = RGB(59, 64, 72);
constexpr COLORREF kBorderSoft = RGB(46, 50, 57);
constexpr COLORREF kText = RGB(238, 241, 246);
constexpr COLORREF kTextMuted = RGB(164, 172, 184);
constexpr COLORREF kTextDim = RGB(111, 120, 134);
constexpr COLORREF kAccent = RGB(93, 160, 185);
constexpr COLORREF kAccentStrong = RGB(114, 185, 210);
constexpr COLORREF kOk = RGB(79, 166, 119);
constexpr COLORREF kWarn = RGB(214, 105, 88);
constexpr int kTopHeight = 86;
constexpr int kProjectCardHeight = 86;

class Font {
public:
    Font(int height, int weight) {
        handle = CreateFontW(
            height,
            0,
            0,
            0,
            weight,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
    }

    ~Font() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    HFONT handle = nullptr;
};

class Brush {
public:
    explicit Brush(COLORREF color) {
        handle = CreateSolidBrush(color);
    }

    ~Brush() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    Brush(const Brush&) = delete;
    Brush& operator=(const Brush&) = delete;

    HBRUSH handle = nullptr;
};

class Pen {
public:
    Pen(COLORREF color, int width = 1) {
        handle = CreatePen(PS_SOLID, width, color);
    }

    ~Pen() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    Pen(const Pen&) = delete;
    Pen& operator=(const Pen&) = delete;

    HPEN handle = nullptr;
};

void Fill(HDC dc, RECT rect, COLORREF color) {
    Brush brush(color);
    FillRect(dc, &rect, brush.handle);
}

void RoundFill(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius = 10) {
    Brush brush(fill);
    Pen pen(border);
    const HGDIOBJ oldBrush = SelectObject(dc, brush.handle);
    const HGDIOBJ oldPen = SelectObject(dc, pen.handle);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}

void DrawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color) {
    Pen pen(color);
    const HGDIOBJ oldPen = SelectObject(dc, pen.handle);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, oldPen);
}

void DrawTextLine(HDC dc, RECT rect, const std::wstring& text, COLORREF color, const Font& font, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    const HGDIOBJ oldFont = SelectObject(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void DrawButton(HDC dc, RECT rect, const std::wstring& label, bool primary, bool enabled, const Font& font) {
    const COLORREF fill = !enabled ? RGB(29, 31, 35) : (primary ? RGB(63, 116, 137) : RGB(32, 35, 40));
    const COLORREF border = !enabled ? RGB(43, 47, 54) : (primary ? kAccentStrong : kBorder);
    const COLORREF text = enabled ? kText : kTextDim;
    RoundFill(dc, rect, fill, border, 9);
    DrawTextLine(dc, rect, label, text, font, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawDangerButton(HDC dc, RECT rect, const std::wstring& label, bool enabled, const Font& font) {
    const COLORREF fill = enabled ? RGB(39, 30, 31) : RGB(29, 31, 35);
    const COLORREF border = enabled ? RGB(142, 77, 72) : RGB(43, 47, 54);
    const COLORREF text = enabled ? RGB(255, 190, 183) : kTextDim;
    RoundFill(dc, rect, fill, border, 9);
    DrawTextLine(dc, rect, label, text, font, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawField(HDC dc, RECT rect, const std::wstring& text, bool focused, const Font& font) {
    RoundFill(dc, rect, kField, focused ? kAccentStrong : kBorder, 9);
    DrawTextLine(dc, Inset(rect, 12), text, text == L"Search projects" ? kTextDim : kText, font);
}

void ComputeLayout(HubState& state) {
    constexpr int margin = 24;
    constexpr int gap = 18;
    const int actionTop = 24;
    state.layout.newProjectButton = Rect(state.width - margin - 140, actionTop, state.width - margin, actionTop + 38);
    state.layout.openProjectButton = Rect(state.layout.newProjectButton.left - 132, actionTop, state.layout.newProjectButton.left - 10, actionTop + 38);
    state.layout.launchProjectButton = Rect(state.layout.openProjectButton.left - 118, actionTop, state.layout.openProjectButton.left - 10, actionTop + 38);

    const int contentTop = kTopHeight + 28;
    const int searchTop = contentTop + 58;
    const int detailsWidth = std::clamp(state.width / 3, 300, 390);
    state.layout.detailsRect = Rect(state.width - margin - detailsWidth, searchTop + 52, state.width - margin, state.height - 42);
    state.layout.listRect = Rect(margin, searchTop + 52, state.layout.detailsRect.left - gap, state.height - 42);
    state.layout.searchField = Rect(margin, searchTop, std::min(static_cast<int>(state.layout.listRect.right), margin + 420), searchTop + 38);
    state.layout.removeProjectButton = Rect(state.layout.detailsRect.left + 20, state.layout.detailsRect.bottom - 58, state.layout.detailsRect.right - 20, state.layout.detailsRect.bottom - 20);
}

void DrawHeader(HDC dc, const HubState& state, const Font& brandFont, const Font& bodyFont, const Font& smallFont) {
    Fill(dc, Rect(0, 0, state.width, state.height), kBackground);
    Fill(dc, Rect(0, 0, state.width, kTopHeight), kTop);
    DrawLine(dc, 0, kTopHeight - 1, state.width, kTopHeight - 1, kBorderSoft);
    DrawTextLine(dc, Rect(24, 18, 260, 48), L"21kb Hub", kText, brandFont);
    DrawTextLine(dc, Rect(26, 48, 520, 72), L"Project launcher and workspace manager", kTextMuted, smallFont);
    DrawButton(dc, state.layout.launchProjectButton, L"Launch", false, state.selectedProject >= 0, bodyFont);
    DrawButton(dc, state.layout.openProjectButton, L"Add existing", false, true, bodyFont);
    DrawButton(dc, state.layout.newProjectButton, L"New project", true, true, bodyFont);
}

void DrawSectionIntro(HDC dc, const HubState& state, const Font& titleFont, const Font& bodyFont, const Font& smallFont) {
    const int top = kTopHeight + 28;
    DrawTextLine(dc, Rect(24, top - 4, 340, top + 32), L"Projects", kText, titleFont);
    DrawTextLine(dc, Rect(24, top + 28, 540, top + 50), L"Select a workspace, inspect its descriptor, then launch the editor.", kTextMuted, smallFont);
    DrawField(dc, state.layout.searchField, state.searchQuery.empty() ? L"Search projects" : state.searchQuery, state.searchFocused, bodyFont);
}

void DrawBadge(HDC dc, RECT rect, const std::wstring& text, bool ok, const Font& font) {
    RoundFill(dc, rect, ok ? RGB(33, 63, 48) : RGB(75, 39, 36), ok ? kOk : kWarn, 8);
    DrawTextLine(dc, rect, text, ok ? RGB(186, 236, 205) : RGB(255, 184, 174), font, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawProjectCard(HDC dc, RECT rect, const HubProjectItem& project, bool selected, const Font& bodyFont, const Font& smallFont) {
    RoundFill(dc, rect, selected ? kPanelSelected : kPanel, selected ? kAccentStrong : kBorderSoft, 12);
    RECT icon = Rect(rect.left + 18, rect.top + 18, rect.left + 52, rect.top + 52);
    RoundFill(dc, icon, project.valid ? RGB(39, 87, 65) : RGB(91, 50, 44), project.valid ? kOk : kWarn, 8);

    DrawTextLine(dc, Rect(rect.left + 68, rect.top + 12, rect.right - 150, rect.top + 36), project.name, kText, bodyFont);
    DrawTextLine(dc, Rect(rect.left + 68, rect.top + 38, rect.right - 150, rect.top + 60), project.projectRoot.wstring(), project.valid ? kTextMuted : kWarn, smallFont);
    DrawTextLine(dc, Rect(rect.left + 68, rect.top + 60, rect.right - 150, rect.top + 80), project.modified + L"  |  " + project.size, kTextDim, smallFont);
    DrawBadge(dc, Rect(rect.right - 118, rect.top + 28, rect.right - 20, rect.top + 56), project.valid ? L"Ready" : L"Invalid", project.valid, smallFont);
}

void DrawProjectList(HDC dc, HubState& state, const Font& bodyFont, const Font& smallFont) {
    RoundFill(dc, state.layout.listRect, RGB(20, 22, 25), kBorderSoft, 12);
    RECT clip = Inset(state.layout.listRect, 12);
    const HRGN clipRegion = CreateRectRgn(clip.left, clip.top, clip.right, clip.bottom);
    SelectClipRgn(dc, clipRegion);

    const std::vector<int> visible = HubProjectFilters::VisibleIndices(state);
    if (visible.empty()) {
        const std::wstring title = state.projects.empty() ? L"No projects yet" : L"No projects match the search";
        const std::wstring detail = state.projects.empty() ? L"Use New project or Add existing to start." : L"Change the search text to show more results.";
        DrawTextLine(dc, Rect(clip.left + 12, clip.top + 24, clip.right - 12, clip.top + 54), title, kText, bodyFont);
        DrawTextLine(dc, Rect(clip.left + 12, clip.top + 54, clip.right - 12, clip.top + 82), detail, kTextMuted, smallFont);
    }

    for (std::size_t visibleIndex = 0; visibleIndex < visible.size(); ++visibleIndex) {
        const int projectIndex = visible[visibleIndex];
        const int top = clip.top + static_cast<int>(visibleIndex) * (kProjectCardHeight + 12) - state.scrollY;
        RECT card = Rect(clip.left, top, clip.right, top + kProjectCardHeight);
        if (card.bottom < clip.top || card.top > clip.bottom) {
            continue;
        }
        DrawProjectCard(dc, card, state.projects[static_cast<std::size_t>(projectIndex)], state.selectedProject == projectIndex, bodyFont, smallFont);
    }

    SelectClipRgn(dc, nullptr);
    DeleteObject(clipRegion);
}

void DrawDetails(HDC dc, const HubState& state, const Font& titleFont, const Font& bodyFont, const Font& smallFont) {
    RoundFill(dc, state.layout.detailsRect, kPanel, kBorderSoft, 12);
    DrawTextLine(dc, Rect(state.layout.detailsRect.left + 20, state.layout.detailsRect.top + 18, state.layout.detailsRect.right - 20, state.layout.detailsRect.top + 48), L"Workspace", kText, titleFont);

    if (state.selectedProject < 0 || state.selectedProject >= static_cast<int>(state.projects.size())) {
        DrawTextLine(dc, Rect(state.layout.detailsRect.left + 20, state.layout.detailsRect.top + 76, state.layout.detailsRect.right - 20, state.layout.detailsRect.top + 104), L"No project selected", kTextMuted, bodyFont);
        DrawTextLine(dc, Rect(state.layout.detailsRect.left + 20, state.layout.detailsRect.top + 106, state.layout.detailsRect.right - 20, state.layout.detailsRect.top + 156), L"Choose a project card to see descriptor details and launch readiness.", kTextDim, smallFont, DT_LEFT | DT_WORDBREAK);
        DrawDangerButton(dc, state.layout.removeProjectButton, L"Remove from Hub", false, bodyFont);
        return;
    }

    const HubProjectItem& project = state.projects[static_cast<std::size_t>(state.selectedProject)];
    DrawTextLine(dc, Rect(state.layout.detailsRect.left + 20, state.layout.detailsRect.top + 70, state.layout.detailsRect.right - 20, state.layout.detailsRect.top + 98), project.name, kText, titleFont);
    DrawBadge(dc, Rect(state.layout.detailsRect.left + 20, state.layout.detailsRect.top + 108, state.layout.detailsRect.left + 130, state.layout.detailsRect.top + 134), project.valid ? L"Ready" : L"Invalid", project.valid, smallFont);

    int y = state.layout.detailsRect.top + 164;
    const auto row = [&](const std::wstring& label, const std::wstring& value) {
        DrawTextLine(dc, Rect(state.layout.detailsRect.left + 20, y, state.layout.detailsRect.right - 20, y + 20), label, kTextDim, smallFont);
        DrawTextLine(dc, Rect(state.layout.detailsRect.left + 20, y + 22, state.layout.detailsRect.right - 20, y + 48), value, kText, smallFont);
        y += 62;
    };

    row(L"Project file", project.projectFile.wstring());
    row(L"Default scene", project.defaultScene.empty() ? L"-" : project.defaultScene);
    row(L"Category", project.category.empty() ? L"-" : project.category);
    row(L"Validation", project.validationMessage.empty() ? L"Ready" : project.validationMessage);

    DrawDangerButton(dc, state.layout.removeProjectButton, L"Remove from Hub", true, bodyFont);

    if (!state.status.empty()) {
        DrawTextLine(dc, Rect(state.layout.detailsRect.left + 20, state.layout.removeProjectButton.top - 34, state.layout.detailsRect.right - 20, state.layout.removeProjectButton.top - 10), state.status, project.valid ? kTextMuted : kWarn, smallFont);
    }
}

} // namespace

void HubRenderer::Paint(HWND window, HDC dc, HubState& state) {
    RECT client{};
    GetClientRect(window, &client);
    state.width = client.right - client.left;
    state.height = client.bottom - client.top;
    ComputeLayout(state);

    HDC memoryDc = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, state.width, state.height);
    const HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);

    Font brandFont(24, FW_SEMIBOLD);
    Font titleFont(22, FW_SEMIBOLD);
    Font bodyFont(14, FW_SEMIBOLD);
    Font smallFont(12, FW_NORMAL);

    DrawHeader(memoryDc, state, brandFont, bodyFont, smallFont);
    DrawSectionIntro(memoryDc, state, titleFont, bodyFont, smallFont);
    DrawProjectList(memoryDc, state, bodyFont, smallFont);
    DrawDetails(memoryDc, state, titleFont, bodyFont, smallFont);

    BitBlt(dc, 0, 0, state.width, state.height, memoryDc, 0, 0, SRCCOPY);
    SelectObject(memoryDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
}

int HubRenderer::ProjectIndexAt(const HubState& state, int x, int y) noexcept {
    RECT clip = Inset(state.layout.listRect, 12);
    if (!Contains(clip, x, y)) {
        return -1;
    }

    const int relativeY = y - clip.top + state.scrollY;
    if (relativeY < 0) {
        return -1;
    }
    constexpr int cardStep = kProjectCardHeight + 12;
    if (relativeY % cardStep >= kProjectCardHeight) {
        return -1;
    }
    const int visibleIndex = relativeY / cardStep;
    const std::vector<int> visible = HubProjectFilters::VisibleIndices(state);
    return visibleIndex >= 0 && visibleIndex < static_cast<int>(visible.size()) ? visible[static_cast<std::size_t>(visibleIndex)] : -1;
}

int HubRenderer::ExplorerIndexAt(const HubState&, int, int) noexcept {
    return -1;
}

} // namespace kb::hub
