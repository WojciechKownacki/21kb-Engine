#include "rendering/InspectorAudioMixerAssetView.hpp"

#include "engine/assets/AssetManager.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <array>
#include <string>
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

[[nodiscard]] int SectionHeight(
    const InspectorPanelState& state,
    InspectorSectionId section,
    std::span<const InspectorAudioMixerRow> rows) noexcept {
    if (state.IsCollapsed(section)) {
        return kSectionHeaderHeight;
    }
    int height = kSectionHeaderHeight + kDividerHeight;
    for (const InspectorAudioMixerRow& row : rows) {
        height += InspectorAudioMixerAssetModel::RowHeight(row.kind) + kDividerHeight;
    }
    return height;
}

[[nodiscard]] int SectionTop(InspectorSectionId section, const InspectorPanelState& state, const InspectorAudioMixerAssetModel& model) noexcept {
    int y = kHeaderHeight + kPanelPadTop;
    if (section == InspectorSectionId::AudioMixerSnapshots) {
        y += SectionHeight(state, InspectorSectionId::AudioMixerBuses, model.Rows(InspectorSectionId::AudioMixerBuses)) + kSectionGap;
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

void Text(HDC dc, RECT rect, std::string_view value, COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    if (value.empty()) {
        return;
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return;
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), wide.data(), length) != length) {
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

[[nodiscard]] RECT CheckboxRect(RECT row) noexcept {
    const int left = row.left + ((row.right - row.left) * 36 / 100);
    const int rowHeight = static_cast<int>(row.bottom - row.top);
    const int top = row.top + std::max(0, (rowHeight - kCheckboxSize) / 2);
    return Rect(left, top, left + kCheckboxSize, top + kCheckboxSize);
}

[[nodiscard]] RECT ActionRect(RECT row) noexcept {
    return Rect(row.left + kRowPadX, row.top + 2, row.right - kRowPadX, row.bottom - 2);
}

[[nodiscard]] std::optional<RECT> FindRowBounds(
    const RECT& content,
    const InspectorPanelState& state,
    const InspectorAudioMixerAssetModel& model,
    int flatIndex) {
    const InspectorAudioMixerRow* target = model.Find(flatIndex);
    if (target == nullptr || state.IsCollapsed(target->section)) {
        return std::nullopt;
    }
    int y = content.top + SectionTop(target->section, state, model) + kSectionHeaderHeight + kDividerHeight;
    for (const InspectorAudioMixerRow& row : model.Rows(target->section)) {
        const int height = InspectorAudioMixerAssetModel::RowHeight(row.kind);
        if (row.flatIndex == flatIndex) {
            return Rect(content.left, y, content.right, y + height);
        }
        y += height + kDividerHeight;
    }
    return std::nullopt;
}

void PaintHeader(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const kb::assets::AssetMetadata& metadata) {
    const RECT header = Rect(content.left, content.top, content.right, content.top + kHeaderHeight);
    Fill(dc, header, Color(theme.panel));
    const RECT icon = Rect(header.left + 12, header.top + 11, header.left + 52, header.top + 51);
    Frame(dc, icon, Color(theme.chrome), Color(theme.accent));
    {
        ScopedFont font(15, FW_SEMIBOLD);
        const ScopedGdiObject selected(dc, font.handle);
        Text(dc, icon, "M", Color(theme.textPrimary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    const std::string title = metadata.name.empty() ? metadata.virtualPath.filename().string() : metadata.name;
    {
        ScopedFont font(14, FW_SEMIBOLD);
        const ScopedGdiObject selected(dc, font.handle);
        Text(dc, Rect(icon.right + 10, header.top + 7, header.right - 12, header.top + 33), title, Color(theme.textPrimary));
    }
    Text(dc, Rect(icon.right + 10, header.top + 31, header.right - 12, header.bottom - 7), "Audio Mixer", Color(theme.textDisabled));
}

void PaintRow(
    HDC dc,
    const RECT& rowRect,
    const EditorTheme& theme,
    const InspectorPanelState& state,
    kb::assets::AssetId id,
    const InspectorAudioMixerRow& row) {
    const bool hovered = state.IsHovered(InspectorHitKind::Row, row.section, row.property, row.flatIndex)
        || state.IsHovered(InspectorHitKind::TextField, row.section, row.property, row.flatIndex)
        || state.IsHovered(InspectorHitKind::BoolField, row.section, row.property, row.flatIndex);
    Fill(dc, rowRect, hovered ? RGB(34, 38, 45) : Color(theme.background));
    if (row.kind == InspectorAudioMixerRowKind::Group) {
        Fill(dc, rowRect, RGB(25, 28, 33));
        ScopedFont font(11, FW_SEMIBOLD);
        const ScopedGdiObject selected(dc, font.handle);
        Text(dc, Rect(rowRect.left + kRowPadX, rowRect.top, rowRect.right - kRowPadX, rowRect.bottom), row.label, Color(theme.textDisabled));
        return;
    }
    if (row.kind == InspectorAudioMixerRowKind::Action) {
        const RECT button = ActionRect(rowRect);
        Frame(dc, button, hovered ? RGB(34, 38, 45) : Color(theme.chrome), hovered ? Color(theme.accent) : Color(theme.borderPanel));
        ScopedFont font(12, FW_SEMIBOLD);
        const ScopedGdiObject selected(dc, font.handle);
        Text(dc, button, row.label, Color(theme.textSecondary), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    if (row.kind == InspectorAudioMixerRowKind::Empty) {
        ScopedFont font(11, FW_NORMAL);
        const ScopedGdiObject selected(dc, font.handle);
        Text(dc, Rect(rowRect.left + kRowPadX, rowRect.top, rowRect.right - kRowPadX, rowRect.bottom), row.label, Color(theme.textDisabled));
        return;
    }
    {
        ScopedFont font(12, FW_SEMIBOLD);
        const ScopedGdiObject selected(dc, font.handle);
        Text(dc, Rect(rowRect.left + kRowPadX, rowRect.top, ValueRect(rowRect).left, rowRect.bottom), row.label, Color(theme.textSecondary));
    }
    if (row.kind == InspectorAudioMixerRowKind::Bool) {
        const RECT box = CheckboxRect(rowRect);
        Frame(dc, box, hovered ? RGB(34, 38, 45) : Color(theme.chrome), Color(theme.borderPanel));
        if (row.boolValue) {
            ScopedFont font(10, FW_SEMIBOLD);
            const ScopedGdiObject selected(dc, font.handle);
            Text(dc, box, "x", Color(theme.textPrimary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        return;
    }
    const RECT valueRect = ValueRect(rowRect);
    const bool editing = InspectorAudioMixerAssetView::IsRowEditing(state, id, row);
    const std::string_view shown = editing ? std::string_view{ state.EditBuffer() } : std::string_view{ row.value };
    Frame(dc, valueRect, Color(theme.chrome), editing || hovered ? Color(theme.accent) : Color(theme.borderPanel));
    ScopedFont font(12, FW_NORMAL);
    const ScopedGdiObject selected(dc, font.handle);
    Text(dc, Rect(valueRect.left + 8, valueRect.top, valueRect.right - 4, valueRect.bottom), shown,
        row.kind == InspectorAudioMixerRowKind::ReadOnly ? Color(theme.textDisabled) : Color(theme.textPrimary));
}

void PaintSection(
    HDC dc,
    const RECT& content,
    int top,
    const EditorTheme& theme,
    const InspectorPanelState& state,
    kb::assets::AssetId id,
    InspectorSectionId section,
    std::string_view title,
    std::span<const InspectorAudioMixerRow> rows) {
    const RECT header = Rect(content.left, top, content.right, top + kSectionHeaderHeight);
    Fill(dc, header, Color(theme.chrome));
    const bool collapsed = state.IsCollapsed(section);
    Text(dc, Rect(header.left + 8, header.top, header.left + 25, header.bottom), collapsed ? ">" : "v", Color(theme.textSecondary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    {
        ScopedFont font(12, FW_SEMIBOLD);
        const ScopedGdiObject selected(dc, font.handle);
        Text(dc, Rect(header.left + 29, header.top, header.right - 12, header.bottom), title, Color(theme.textPrimary));
    }
    if (collapsed) {
        return;
    }
    int y = header.bottom + kDividerHeight;
    for (const InspectorAudioMixerRow& row : rows) {
        const int height = InspectorAudioMixerAssetModel::RowHeight(row.kind);
        PaintRow(dc, Rect(content.left, y, content.right, y + height), theme, state, id, row);
        y += height;
        Fill(dc, Rect(content.left, y, content.right, y + kDividerHeight), RGB(0, 0, 0));
        y += kDividerHeight;
    }
}
#endif

} // namespace

bool InspectorAudioMixerAssetView::Supports(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == kb::audio::kAudioMixerAssetType;
}

kb::assets::AssetHandle<kb::audio::AudioMixerAsset> InspectorAudioMixerAssetView::LoadCached(
    kb::assets::AssetManager& manager,
    kb::assets::AssetId id) {
    kb::assets::AssetHandle<kb::audio::AudioMixerAsset> mixer =
        manager.AcquireLoaded<kb::audio::AudioMixerAsset>(id);
    if (!mixer.IsLoaded()) {
        mixer = manager.Load<kb::audio::AudioMixerAsset>(id);
    }
    return mixer;
}

int InspectorAudioMixerAssetView::ContentHeight(
    const InspectorPanelState& state,
    const kb::audio::AudioMixerAsset& asset) {
    const InspectorAudioMixerAssetModel model{ asset };
    return kHeaderHeight + kPanelPadTop
        + SectionHeight(state, InspectorSectionId::AudioMixerBuses, model.Rows(InspectorSectionId::AudioMixerBuses))
        + kSectionGap
        + SectionHeight(state, InspectorSectionId::AudioMixerSnapshots, model.Rows(InspectorSectionId::AudioMixerSnapshots));
}

bool InspectorAudioMixerAssetView::IsRowEditing(
    const InspectorPanelState& state,
    kb::assets::AssetId id,
    const InspectorAudioMixerRow& row) noexcept {
    if (row.kind != InspectorAudioMixerRowKind::Text
        || state.EditedProperty() != row.property
        || state.EditIndex() != row.flatIndex
        || !state.EditRowIdentity().has_value()
        || state.EditRowIdentity()->ownerAssetId != id.value) {
        return false;
    }
    InspectorDynamicRowIdentity semanticIdentity = *state.EditRowIdentity();
    semanticIdentity.ownerAssetId = 0U;
    return semanticIdentity == row.identity;
}

#if defined(_WIN32)
void InspectorAudioMixerAssetView::Paint(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const InspectorPanelState& state,
    const kb::assets::AssetMetadata& metadata,
    const kb::audio::AudioMixerAsset& asset) {
    if (dc == nullptr || content.right <= content.left || content.bottom <= content.top) {
        return;
    }
    const InspectorAudioMixerAssetModel model{ asset };
    Fill(dc, content, Color(theme.background));
    PaintHeader(dc, content, theme, metadata);
    const int busesTop = content.top + kHeaderHeight + kPanelPadTop;
    PaintSection(dc, content, busesTop, theme, state, metadata.id, InspectorSectionId::AudioMixerBuses, "Buses", model.Rows(InspectorSectionId::AudioMixerBuses));
    const int snapshotsTop = busesTop
        + SectionHeight(state, InspectorSectionId::AudioMixerBuses, model.Rows(InspectorSectionId::AudioMixerBuses))
        + kSectionGap;
    PaintSection(dc, content, snapshotsTop, theme, state, metadata.id, InspectorSectionId::AudioMixerSnapshots, "Snapshots", model.Rows(InspectorSectionId::AudioMixerSnapshots));
}

InspectorAudioMixerAssetHit InspectorAudioMixerAssetView::HitTest(
    const RECT& content,
    const InspectorPanelState& state,
    const kb::audio::AudioMixerAsset& asset,
    int x,
    int y) {
    const InspectorAudioMixerAssetModel model{ asset };
    for (const InspectorSectionId section : std::array{
             InspectorSectionId::AudioMixerBuses,
             InspectorSectionId::AudioMixerSnapshots }) {
        const int top = content.top + SectionTop(section, state, model);
        const RECT header = Rect(content.left, top, content.right, top + kSectionHeaderHeight);
        if (Contains(header, x, y)) {
            return InspectorAudioMixerAssetHit{
                .kind = InspectorHitKind::SectionHeader,
                .section = section,
                .rect = header,
            };
        }
        if (state.IsCollapsed(section)) {
            continue;
        }
        for (const InspectorAudioMixerRow& row : model.Rows(section)) {
            const std::optional<RECT> bounds = FindRowBounds(content, state, model, row.flatIndex);
            if (!bounds.has_value() || !Contains(*bounds, x, y)) {
                continue;
            }
            if (row.kind == InspectorAudioMixerRowKind::Action && !Contains(ActionRect(*bounds), x, y)) {
                continue;
            }
            InspectorHitKind kind = InspectorHitKind::Row;
            RECT hitRect = *bounds;
            if (row.kind == InspectorAudioMixerRowKind::Text && Contains(ValueRect(*bounds), x, y)) {
                kind = InspectorHitKind::TextField;
                hitRect = ValueRect(*bounds);
            } else if (row.kind == InspectorAudioMixerRowKind::Bool && Contains(CheckboxRect(*bounds), x, y)) {
                kind = InspectorHitKind::BoolField;
                hitRect = CheckboxRect(*bounds);
            }
            return InspectorAudioMixerAssetHit{
                .kind = kind,
                .section = row.section,
                .property = row.property,
                .index = row.flatIndex,
                .rect = hitRect,
            };
        }
    }
    return {};
}

std::optional<RECT> InspectorAudioMixerAssetView::RowBounds(
    const RECT& content,
    const InspectorPanelState& state,
    const kb::audio::AudioMixerAsset& asset,
    int flatIndex) {
    return FindRowBounds(content, state, InspectorAudioMixerAssetModel{ asset }, flatIndex);
}
#endif

} // namespace kb::editor
