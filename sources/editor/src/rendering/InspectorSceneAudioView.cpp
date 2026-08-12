#include "rendering/InspectorSceneAudioView.hpp"

#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace kb::editor {
namespace {

constexpr int kHeaderHeight = 62;
constexpr int kPanelPadTop = 10;
constexpr int kSectionGap = 8;
constexpr int kSectionHeaderHeight = 24;
constexpr int kDividerHeight = 1;
constexpr int kValueHeight = 20;
constexpr int kRowPadX = 16;
constexpr int kCheckboxSize = 16;
constexpr int kPickerWidth = 22;

[[nodiscard]] int SectionHeight(
    const InspectorPanelState& state,
    InspectorSectionId section,
    std::span<const InspectorSceneAudioRow> rows) noexcept {
    if (state.IsCollapsed(section)) {
        return kSectionHeaderHeight;
    }
    int height = kSectionHeaderHeight + kDividerHeight;
    for (const InspectorSceneAudioRow& row : rows) {
        height += InspectorSceneAudioModel::RowHeight(row.kind) + kDividerHeight;
    }
    return height;
}

[[nodiscard]] int SectionTop(
    InspectorSectionId section,
    const InspectorPanelState& state,
    const InspectorSceneAudioModel& model) noexcept {
    int y = kHeaderHeight + kPanelPadTop;
    if (section == InspectorSectionId::SceneAudioOcclusion) {
        y += SectionHeight(
            state,
            InspectorSectionId::SceneAudioRouting,
            model.Rows(InspectorSectionId::SceneAudioRouting)) + kSectionGap;
    }
    return y;
}

#if defined(_WIN32)
[[nodiscard]] COLORREF Color(EditorColor color) noexcept {
    return RGB(color.r, color.g, color.b);
}

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept {
    return RECT{ left, top, right, bottom };
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

void Fill(HDC dc, const RECT& rect, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    if (brush != nullptr) {
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

void Frame(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    Fill(dc, rect, fill);
    const HBRUSH brush = CreateSolidBrush(border);
    if (brush != nullptr) {
        FrameRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

void Text(
    HDC dc,
    RECT rect,
    std::string_view value,
    COLORREF color,
    UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    if (value.empty()) {
        return;
    }
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return;
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), wide.data(), length) != length) {
        return;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, wide.data(), length, &rect, format | DT_NOPREFIX);
}

[[nodiscard]] RECT ValueRect(RECT row) noexcept {
    const int labelRight = row.left + ((row.right - row.left) * 36 / 100);
    const int rowHeight = static_cast<int>(row.bottom - row.top);
    const int top = row.top + std::max(0, (rowHeight - kValueHeight) / 2);
    return Rect(labelRight, top, row.right - kRowPadX, top + kValueHeight);
}

[[nodiscard]] RECT PickerRect(RECT row) noexcept {
    RECT value = ValueRect(row);
    return Rect(value.right - kPickerWidth, value.top, value.right, value.bottom);
}

[[nodiscard]] RECT AssetValueRect(RECT row) noexcept {
    RECT value = ValueRect(row);
    value.right -= kPickerWidth + 2;
    return value;
}

[[nodiscard]] RECT CheckboxRect(RECT row) noexcept {
    const int left = row.left + ((row.right - row.left) * 36 / 100);
    const int top = row.top + std::max(0, (static_cast<int>(row.bottom - row.top) - kCheckboxSize) / 2);
    return Rect(left, top, left + kCheckboxSize, top + kCheckboxSize);
}

[[nodiscard]] RECT ActionRect(RECT row) noexcept {
    return Rect(row.left + kRowPadX, row.top + 2, row.right - kRowPadX, row.bottom - 2);
}

[[nodiscard]] std::optional<RECT> FindRowBounds(
    const RECT& content,
    const InspectorPanelState& state,
    const InspectorSceneAudioModel& model,
    int flatIndex) {
    const InspectorSceneAudioRow* target = model.Find(flatIndex);
    if (target == nullptr || state.IsCollapsed(target->section)) {
        return std::nullopt;
    }
    int y = content.top + SectionTop(target->section, state, model)
        + kSectionHeaderHeight + kDividerHeight;
    for (const InspectorSceneAudioRow& row : model.Rows(target->section)) {
        const int height = InspectorSceneAudioModel::RowHeight(row.kind);
        if (row.flatIndex == flatIndex) {
            return Rect(content.left, y, content.right, y + height);
        }
        y += height + kDividerHeight;
    }
    return std::nullopt;
}

void PaintHeader(HDC dc, const RECT& content, const EditorTheme& theme) {
    const RECT header = Rect(content.left, content.top, content.right, content.top + kHeaderHeight);
    Fill(dc, header, Color(theme.panel));
    const RECT icon = Rect(header.left + 12, header.top + 11, header.left + 52, header.top + 51);
    Frame(dc, icon, Color(theme.chrome), Color(theme.accent));
    {
        ScopedFont font(15, FW_SEMIBOLD);
        const ScopedGdiObject selected(dc, font.handle);
        Text(dc, icon, "A", Color(theme.textPrimary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    {
        ScopedFont font(14, FW_SEMIBOLD);
        const ScopedGdiObject selected(dc, font.handle);
        Text(dc, Rect(icon.right + 10, header.top + 7, header.right - 12, header.top + 33), "Scene Audio", Color(theme.textPrimary));
    }
    Text(dc, Rect(icon.right + 10, header.top + 31, header.right - 12, header.bottom - 7), "Scene Settings", Color(theme.textDisabled));
}

void PaintRow(
    HDC dc,
    const RECT& rowRect,
    const EditorTheme& theme,
    const InspectorPanelState& state,
    std::uint64_t documentGeneration,
    const InspectorSceneAudioRow& row) {
    const bool hovered = state.IsHovered(InspectorHitKind::Row, row.section, row.property, row.flatIndex)
        || state.IsHovered(InspectorHitKind::TextField, row.section, row.property, row.flatIndex)
        || state.IsHovered(InspectorHitKind::BoolField, row.section, row.property, row.flatIndex)
        || (row.property == InspectorPropertyId::SceneAudioMixer
            && state.IsHovered(
                InspectorHitKind::Row,
                row.section,
                InspectorPropertyId::SceneAudioMixerPicker,
                row.flatIndex));
    Fill(dc, rowRect, hovered ? RGB(34, 38, 45) : Color(theme.background));
    if (row.kind == InspectorSceneAudioRowKind::Action) {
        const RECT button = ActionRect(rowRect);
        Frame(dc, button, hovered ? RGB(34, 38, 45) : Color(theme.chrome), hovered ? Color(theme.accent) : Color(theme.borderPanel));
        ScopedFont font(12, FW_SEMIBOLD);
        const ScopedGdiObject selected(dc, font.handle);
        Text(dc, button, row.label, Color(theme.textSecondary), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    if (row.kind == InspectorSceneAudioRowKind::Empty) {
        Text(dc, Rect(rowRect.left + kRowPadX, rowRect.top, rowRect.right - kRowPadX, rowRect.bottom), row.label, Color(theme.textDisabled));
        return;
    }
    {
        ScopedFont font(12, FW_SEMIBOLD);
        const ScopedGdiObject selected(dc, font.handle);
        Text(dc, Rect(rowRect.left + kRowPadX, rowRect.top, ValueRect(rowRect).left, rowRect.bottom), row.label, Color(theme.textSecondary));
    }
    if (row.kind == InspectorSceneAudioRowKind::Bool) {
        const RECT box = CheckboxRect(rowRect);
        Frame(dc, box, hovered ? RGB(34, 38, 45) : Color(theme.chrome), Color(theme.borderPanel));
        if (row.boolValue) {
            Text(dc, box, "x", Color(theme.textPrimary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        return;
    }
    if (row.kind == InspectorSceneAudioRowKind::Asset) {
        const RECT value = AssetValueRect(rowRect);
        const RECT picker = PickerRect(rowRect);
        Frame(dc, value, Color(theme.chrome), row.invalid ? RGB(190, 76, 76) : Color(theme.borderPanel));
        Frame(dc, picker, Color(theme.chrome), hovered ? Color(theme.accent) : Color(theme.borderPanel));
        Text(dc, Rect(value.left + 8, value.top, value.right - 4, value.bottom), row.value,
            row.invalid ? RGB(235, 132, 132) : Color(theme.textPrimary));
        Text(dc, picker, "...", Color(theme.textSecondary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    const RECT valueRect = ValueRect(rowRect);
    const bool editing = InspectorSceneAudioView::IsRowEditing(state, documentGeneration, row);
    const std::string_view shown = editing ? std::string_view{ state.EditBuffer() } : std::string_view{ row.value };
    Frame(dc, valueRect, Color(theme.chrome), editing || hovered ? Color(theme.accent) : Color(theme.borderPanel));
    Text(dc, Rect(valueRect.left + 8, valueRect.top, valueRect.right - 4, valueRect.bottom), shown,
        row.invalid ? RGB(235, 132, 132)
                    : (row.kind == InspectorSceneAudioRowKind::ReadOnly ? Color(theme.textDisabled) : Color(theme.textPrimary)));
}

void PaintSection(
    HDC dc,
    const RECT& content,
    int top,
    const EditorTheme& theme,
    const InspectorPanelState& state,
    std::uint64_t documentGeneration,
    InspectorSectionId section,
    std::string_view title,
    std::span<const InspectorSceneAudioRow> rows) {
    const RECT header = Rect(content.left, top, content.right, top + kSectionHeaderHeight);
    Fill(dc, header, Color(theme.chrome));
    const bool collapsed = state.IsCollapsed(section);
    Text(dc, Rect(header.left + 8, header.top, header.left + 25, header.bottom), collapsed ? ">" : "v", Color(theme.textSecondary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Text(dc, Rect(header.left + 29, header.top, header.right - 12, header.bottom), title, Color(theme.textPrimary));
    if (collapsed) {
        return;
    }
    int y = header.bottom + kDividerHeight;
    for (const InspectorSceneAudioRow& row : rows) {
        const int height = InspectorSceneAudioModel::RowHeight(row.kind);
        PaintRow(dc, Rect(content.left, y, content.right, y + height), theme, state, documentGeneration, row);
        y += height;
        Fill(dc, Rect(content.left, y, content.right, y + kDividerHeight), RGB(0, 0, 0));
        y += kDividerHeight;
    }
}
#endif

} // namespace

int InspectorSceneAudioView::ContentHeight(
    const InspectorPanelState& state,
    const InspectorSceneAudioModel& model) {
    return kHeaderHeight + kPanelPadTop
        + SectionHeight(state, InspectorSectionId::SceneAudioRouting, model.Rows(InspectorSectionId::SceneAudioRouting))
        + kSectionGap
        + SectionHeight(state, InspectorSectionId::SceneAudioOcclusion, model.Rows(InspectorSectionId::SceneAudioOcclusion));
}

bool InspectorSceneAudioView::IsRowEditing(
    const InspectorPanelState& state,
    std::uint64_t documentGeneration,
    const InspectorSceneAudioRow& row) noexcept {
    if (row.kind != InspectorSceneAudioRowKind::Text
        || state.EditedProperty() != row.property
        || state.EditIndex() != row.flatIndex
        || !state.EditRowIdentity().has_value()
        || state.EditRowIdentity()->ownerDocumentGeneration != documentGeneration) {
        return false;
    }
    InspectorDynamicRowIdentity semantic = *state.EditRowIdentity();
    semantic.ownerDocumentGeneration = 0U;
    return semantic == row.identity;
}

#if defined(_WIN32)
void InspectorSceneAudioView::Paint(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const InspectorPanelState& state,
    std::uint64_t documentGeneration,
    const InspectorSceneAudioModel& model) {
    if (dc == nullptr || content.right <= content.left || content.bottom <= content.top) {
        return;
    }
    Fill(dc, content, Color(theme.background));
    PaintHeader(dc, content, theme);
    const int routingTop = content.top + kHeaderHeight + kPanelPadTop;
    PaintSection(dc, content, routingTop, theme, state, documentGeneration, InspectorSectionId::SceneAudioRouting, "Routing", model.Rows(InspectorSectionId::SceneAudioRouting));
    const int occlusionTop = routingTop
        + SectionHeight(state, InspectorSectionId::SceneAudioRouting, model.Rows(InspectorSectionId::SceneAudioRouting))
        + kSectionGap;
    PaintSection(dc, content, occlusionTop, theme, state, documentGeneration, InspectorSectionId::SceneAudioOcclusion, "Occlusion", model.Rows(InspectorSectionId::SceneAudioOcclusion));
}

InspectorSceneAudioHit InspectorSceneAudioView::HitTest(
    const RECT& content,
    const InspectorPanelState& state,
    const InspectorSceneAudioModel& model,
    int x,
    int y) {
    for (const InspectorSectionId section : std::array{
             InspectorSectionId::SceneAudioRouting,
             InspectorSectionId::SceneAudioOcclusion }) {
        const int top = content.top + SectionTop(section, state, model);
        const RECT header = Rect(content.left, top, content.right, top + kSectionHeaderHeight);
        if (Contains(header, x, y)) {
            return InspectorSceneAudioHit{ .kind = InspectorHitKind::SectionHeader, .section = section, .rect = header };
        }
        if (state.IsCollapsed(section)) {
            continue;
        }
        for (const InspectorSceneAudioRow& row : model.Rows(section)) {
            const std::optional<RECT> bounds = FindRowBounds(content, state, model, row.flatIndex);
            if (!bounds.has_value() || !Contains(*bounds, x, y)) {
                continue;
            }
            if (row.kind == InspectorSceneAudioRowKind::Action && !Contains(ActionRect(*bounds), x, y)) {
                continue;
            }
            InspectorHitKind kind = InspectorHitKind::Row;
            InspectorPropertyId property = row.property;
            RECT hitRect = *bounds;
            if (row.kind == InspectorSceneAudioRowKind::Text && Contains(ValueRect(*bounds), x, y)) {
                kind = InspectorHitKind::TextField;
                hitRect = ValueRect(*bounds);
            } else if (row.kind == InspectorSceneAudioRowKind::Bool && Contains(CheckboxRect(*bounds), x, y)) {
                kind = InspectorHitKind::BoolField;
                hitRect = CheckboxRect(*bounds);
            } else if (row.kind == InspectorSceneAudioRowKind::Asset && Contains(PickerRect(*bounds), x, y)) {
                property = InspectorPropertyId::SceneAudioMixerPicker;
                hitRect = PickerRect(*bounds);
            }
            return InspectorSceneAudioHit{
                .kind = kind,
                .section = row.section,
                .property = property,
                .index = row.flatIndex,
                .rect = hitRect,
            };
        }
    }
    return {};
}

std::optional<RECT> InspectorSceneAudioView::RowBounds(
    const RECT& content,
    const InspectorPanelState& state,
    const InspectorSceneAudioModel& model,
    int flatIndex) {
    return FindRowBounds(content, state, model, flatIndex);
}
#endif

} // namespace kb::editor
