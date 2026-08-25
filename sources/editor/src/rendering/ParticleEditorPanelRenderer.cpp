#include "rendering/ParticleEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "engine/scene/SceneAssets.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/ParticleEditorPanelLayout.hpp"
#include "rendering/components/CategoryHeader.hpp"
#include "rendering/components/DenseListRow.hpp"
#include "rendering/components/PropertyRow.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/EditorSceneContext.hpp"

#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

constexpr COLORREF kStudioSurface = RGB(32, 35, 42);
constexpr COLORREF kStudioSelected = RGB(54, 46, 30);
constexpr COLORREF kStudioBorder = RGB(58, 62, 72);
constexpr COLORREF kStudioGold = RGB(212, 168, 64);
constexpr COLORREF kStudioGoldSoft = RGB(168, 132, 52);
constexpr COLORREF kStudioText = RGB(232, 235, 240);
constexpr COLORREF kStudioMuted = RGB(150, 158, 170);
constexpr COLORREF kStudioDim = RGB(108, 116, 128);
constexpr COLORREF kStudioDanger = RGB(176, 74, 78);
constexpr COLORREF kStudioToggle = RGB(72, 148, 108);
constexpr COLORREF kStudioPrimary = RGB(58, 92, 132);

enum class ParticleEditorButtonTone : std::uint8_t { Neutral, Primary, Selected, Toggle, Destructive };

void AddRoundedRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    const float diameter = std::max(0.1F, radius * 2.0F);
    if (rect.Width <= diameter || rect.Height <= diameter) {
        path.AddRectangle(rect);
        return;
    }
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270.0F, 90.0F);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0.0F, 90.0F);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90.0F, 90.0F);
    path.CloseFigure();
}

[[nodiscard]] Gdiplus::Color Gdip(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

void FillRound(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, float radius) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    AddRoundedRect(path, Gdiplus::RectF(
        static_cast<float>(rect.left) + 0.5F, static_cast<float>(rect.top) + 0.5F,
        static_cast<float>(rect.right - rect.left) - 1.0F, static_cast<float>(rect.bottom - rect.top) - 1.0F), radius);
    Gdiplus::SolidBrush brush(Gdip(fill));
    graphics.FillPath(&brush, &path);
    Gdiplus::Pen pen(Gdip(border), 1.0F);
    graphics.DrawPath(&pen, &path);
}

void FillEllipse(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush(Gdip(fill));
    graphics.FillEllipse(&brush,
        static_cast<INT>(rect.left), static_cast<INT>(rect.top),
        static_cast<INT>(rect.right - rect.left), static_cast<INT>(rect.bottom - rect.top));
    Gdiplus::Pen pen(Gdip(border), 1.0F);
    graphics.DrawEllipse(&pen,
        static_cast<INT>(rect.left), static_cast<INT>(rect.top),
        static_cast<INT>(rect.right - rect.left - 1), static_cast<INT>(rect.bottom - rect.top - 1));
}

void DrawTextUtf8(HDC dc, RECT rect, const char* text, COLORREF color, UINT format) {
    if (text == nullptr || text[0] == '\0') return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (wideLength <= 1) {
        DrawTextA(dc, text, -1, &rect, format | DT_NOPREFIX);
        return;
    }
    std::wstring wide(static_cast<std::size_t>(wideLength - 1), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide.data(), wideLength) <= 0) return;
    DrawTextW(dc, wide.c_str(), -1, &rect, format | DT_NOPREFIX);
}

void DrawButton(HDC dc, const RECT& rect, const char* label, ParticleEditorButtonTone tone = ParticleEditorButtonTone::Neutral,
                bool enabled = true) {
    COLORREF fill = kStudioSurface;
    COLORREF border = kStudioBorder;
    COLORREF textColor = kStudioText;
    if (!enabled) {
        fill = RGB(28, 30, 35);
        border = RGB(48, 52, 60);
        textColor = kStudioDim;
    } else if (tone == ParticleEditorButtonTone::Primary) {
        fill = kStudioPrimary;
        border = RGB(98, 142, 196);
    } else if (tone == ParticleEditorButtonTone::Selected) {
        fill = kStudioSelected;
        border = kStudioGold;
    } else if (tone == ParticleEditorButtonTone::Toggle) {
        fill = RGB(36, 78, 58);
        border = kStudioToggle;
    } else if (tone == ParticleEditorButtonTone::Destructive) {
        fill = RGB(72, 38, 42);
        border = kStudioDanger;
    }
    FillRound(dc, rect, fill, border, 5.0F);
    RECT text = rect;
    text.left += 5;
    text.right -= 5;
    DrawTextUtf8(dc, text, label, textColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawIconButton(HDC dc, const RECT& rect, HeroIconKind icon, ParticleEditorButtonTone tone, bool enabled = true) {
    COLORREF fill = kStudioSurface;
    COLORREF border = kStudioBorder;
    COLORREF iconColor = kStudioMuted;
    if (!enabled) {
        fill = RGB(28, 30, 35);
        iconColor = kStudioDim;
    } else if (tone == ParticleEditorButtonTone::Destructive) {
        fill = RGB(72, 38, 42);
        border = kStudioDanger;
        iconColor = RGB(232, 168, 168);
    } else if (tone == ParticleEditorButtonTone::Toggle) {
        fill = RGB(36, 78, 58);
        border = kStudioToggle;
        iconColor = RGB(186, 232, 204);
    } else if (tone == ParticleEditorButtonTone::Selected) {
        fill = kStudioSelected;
        border = kStudioGold;
        iconColor = kStudioGold;
    }
    FillRound(dc, rect, fill, border, 4.0F);
    RECT iconRect = rect;
    iconRect.left += 4;
    iconRect.right -= 4;
    iconRect.top += 4;
    iconRect.bottom -= 4;
    HeroIconPainter::Draw(dc, iconRect, icon, iconColor, 2);
}

void DrawMoveChevron(HDC dc, const RECT& rect, bool up) {
    FillRound(dc, rect, kStudioSurface, kStudioBorder, 4.0F);
    const int centerX = (rect.left + rect.right) / 2;
    const int centerY = (rect.top + rect.bottom) / 2;
    POINT points[3]{};
    if (up) {
        points[0] = POINT{centerX, centerY - 4};
        points[1] = POINT{centerX + 5, centerY + 3};
        points[2] = POINT{centerX - 5, centerY + 3};
    } else {
        points[0] = POINT{centerX - 5, centerY - 3};
        points[1] = POINT{centerX + 5, centerY - 3};
        points[2] = POINT{centerX, centerY + 4};
    }
    HBRUSH brush = CreateSolidBrush(kStudioMuted);
    HPEN pen = CreatePen(PS_SOLID, 1, kStudioMuted);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, points, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawGrip(HDC dc, const RECT& rect) {
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush(Gdip(kStudioDim));
    const int cx = (rect.left + rect.right) / 2;
    const int cy = (rect.top + rect.bottom) / 2;
    for (int row = -1; row <= 1; ++row) {
        for (int col = 0; col < 2; ++col) {
            graphics.FillEllipse(&brush, cx - 4 + col * 5, cy - 5 + row * 5, 2, 2);
        }
    }
}

void DrawToggle(HDC dc, const RECT& rect, bool on) {
    FillRound(dc, rect, on ? RGB(42, 92, 68) : RGB(28, 30, 36), on ? kStudioToggle : kStudioBorder, 9.0F);
    const int diameter = std::max(8, static_cast<int>(rect.bottom - rect.top) - 4);
    RECT knob = on
        ? RECT{rect.right - 3 - diameter, rect.top + 2, rect.right - 3, rect.top + 2 + diameter}
        : RECT{rect.left + 3, rect.top + 2, rect.left + 3 + diameter, rect.top + 2 + diameter};
    FillEllipse(dc, knob, on ? RGB(214, 236, 222) : RGB(176, 182, 192), RGB(8, 8, 10));
}

void DrawSectionHeader(
    HDC dc,
    const RECT& rect,
    const EditorTheme& theme,
    std::string_view label,
    HeroIconKind icon,
    bool expanded,
    std::string_view trailingText = {}) {
    CategoryHeader::Paint(dc, theme, CategoryHeaderDescriptor{
        .bounds = rect,
        .title = label,
        .trailingText = trailingText,
        .icon = icon,
        .expanded = expanded,
    });
}

void DrawRecipeTile(HDC dc, const RECT& rect, const kb::assets::AssetMetadata& recipe) {
    FillRound(dc, rect, kStudioSurface, kStudioBorder, 7.0F);
    RECT mark{rect.left + 8, rect.top + 10, rect.left + 14, rect.bottom - 10};
    FillRound(dc, mark, kStudioGold, kStudioGoldSoft, 2.0F);
    RECT title = rect;
    title.left += 20;
    title.right -= 8;
    title.top += 6;
    title.bottom = title.top + 20;
    DrawTextUtf8(dc, title, recipe.name.c_str(), kStudioText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT category = rect;
    category.left += 20;
    category.right -= 8;
    category.top = title.bottom;
    category.bottom -= 6;
    DrawTextUtf8(dc, category, recipe.browseTag.c_str(), kStudioGoldSoft,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawCurvePreview(HDC dc, const RECT& rect, const kb::math::Curve& curve) {
    FillRound(dc, rect, RGB(18, 20, 24), kStudioBorder, 4.0F);
    if (curve.keyframes.size() < 2U || rect.right - rect.left < 4) return;
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(Gdip(kStudioGold), 1.6F);
    float minValue = curve.keyframes.front().value;
    float maxValue = minValue;
    for (const auto& key : curve.keyframes) {
        minValue = std::min(minValue, key.value);
        maxValue = std::max(maxValue, key.value);
    }
    const float span = std::max(0.0001F, maxValue - minValue);
    const float width = static_cast<float>(rect.right - rect.left - 4);
    const float height = static_cast<float>(rect.bottom - rect.top - 4);
    Gdiplus::PointF previous{};
    for (std::size_t index = 0U; index < curve.keyframes.size(); ++index) {
        const float x = static_cast<float>(rect.left + 2) + curve.keyframes[index].time * width;
        const float y = static_cast<float>(rect.bottom - 2) -
            ((curve.keyframes[index].value - minValue) / span) * height;
        const Gdiplus::PointF point{x, y};
        if (index != 0U) graphics.DrawLine(&pen, previous, point);
        previous = point;
    }
}

void DrawColorSwatch(HDC dc, const RECT& rect, const kb::math::Color& color) {
    FillRound(dc, rect, RGB(18, 20, 24), kStudioBorder, 4.0F);
    if (rect.right - rect.left < 4 || rect.bottom - rect.top < 4) return;
    RECT inner{rect.left + 2, rect.top + 2, rect.right - 2, rect.bottom - 2};
    const int cell = 6;
    for (int y = inner.top; y < inner.bottom; y += cell) {
        for (int x = inner.left; x < inner.right; x += cell) {
            const bool dark = ((x - inner.left) / cell + (y - inner.top) / cell) % 2 == 0;
            RECT tile{x, y, std::min<LONG>(x + cell, inner.right), std::min<LONG>(y + cell, inner.bottom)};
            GdiDrawing::FillRectColor(dc, tile, dark ? RGB(48, 50, 56) : RGB(78, 80, 88));
        }
    }
    const BYTE alpha = static_cast<BYTE>(std::clamp(color.a, 0.0F, 1.0F) * 255.0F + 0.5F);
    const COLORREF rgb = RGB(static_cast<BYTE>(std::clamp(color.r, 0.0F, 1.0F) * 255.0F),
                             static_cast<BYTE>(std::clamp(color.g, 0.0F, 1.0F) * 255.0F),
                             static_cast<BYTE>(std::clamp(color.b, 0.0F, 1.0F) * 255.0F));
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush(Gdip(rgb, alpha));
    graphics.FillRectangle(&brush,
        static_cast<INT>(inner.left), static_cast<INT>(inner.top),
        static_cast<INT>(inner.right - inner.left), static_cast<INT>(inner.bottom - inner.top));
}

void DrawGradientPreview(HDC dc, const RECT& rect, const kb::math::Gradient& gradient) {
    FillRound(dc, rect, RGB(18, 20, 24), kStudioBorder, 4.0F);
    if (gradient.stops.empty() || rect.right - rect.left < 4) return;
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float width = static_cast<float>(rect.right - rect.left - 4);
    const float height = static_cast<float>(rect.bottom - rect.top - 4);
    std::vector<kb::math::GradientStop> stops = gradient.stops;
    std::sort(stops.begin(), stops.end(), [](const auto& left, const auto& right) { return left.time < right.time; });
    for (std::size_t index = 0U; index + 1U < stops.size(); ++index) {
        const float left = static_cast<float>(rect.left + 2) + stops[index].time * width;
        const float right = static_cast<float>(rect.left + 2) + stops[index + 1U].time * width;
        Gdiplus::LinearGradientBrush brush(
            Gdiplus::PointF(left, static_cast<float>(rect.top + 2)),
            Gdiplus::PointF(std::max(left + 1.0F, right), static_cast<float>(rect.top + 2)),
            Gdip(RGB(static_cast<BYTE>(std::clamp(stops[index].color.r, 0.0F, 1.0F) * 255.0F),
                     static_cast<BYTE>(std::clamp(stops[index].color.g, 0.0F, 1.0F) * 255.0F),
                     static_cast<BYTE>(std::clamp(stops[index].color.b, 0.0F, 1.0F) * 255.0F))),
            Gdip(RGB(static_cast<BYTE>(std::clamp(stops[index + 1U].color.r, 0.0F, 1.0F) * 255.0F),
                     static_cast<BYTE>(std::clamp(stops[index + 1U].color.g, 0.0F, 1.0F) * 255.0F),
                     static_cast<BYTE>(std::clamp(stops[index + 1U].color.b, 0.0F, 1.0F) * 255.0F))));
        graphics.FillRectangle(&brush, left, static_cast<float>(rect.top + 2), std::max(1.0F, right - left), height);
    }
    Gdiplus::SolidBrush handleBrush(Gdip(RGB(236, 214, 150)));
    Gdiplus::Pen handlePen(Gdip(kStudioGold), 1.0F);
    for (const auto& stop : stops) {
        const float x = static_cast<float>(rect.left + 2) + stop.time * width;
        const float y = static_cast<float>(rect.top) + height * 0.5F + 2.0F;
        graphics.FillEllipse(&handleBrush, x - 4.0F, y - 4.0F, 8.0F, 8.0F);
        graphics.DrawEllipse(&handlePen, x - 4.0F, y - 4.0F, 8.0F, 8.0F);
    }
}

void DrawSlider(HDC dc, const RECT& track, const RECT& fill, const RECT& thumb) {
    FillRound(dc, track, RGB(36, 40, 48), RGB(64, 70, 82), 3.0F);
    if (fill.right > fill.left)
        FillRound(dc, fill, kStudioGold, kStudioGoldSoft, 3.0F);
    FillEllipse(dc, thumb, RGB(236, 214, 150), kStudioGold);
}

void DrawProperty(HDC dc, const ParticleEditorPropertyRowLayout& row,
                  const kb::particle_editor::ParticleEditorPropertyRow& property,
                  const EditorTheme& theme) {
    if (row.bounds.right <= row.bounds.left) return;
    PropertyRow::PaintBackground(dc, theme, row.bounds, false);
    PropertyRow::PaintLabel(
        dc, theme, row.label, property.label, true, row.enumChipCount != 0U);
    using kb::particle_editor::ParticleEditorPropertyWidget;
    if (row.hasSlider) {
        DrawSlider(dc, row.sliderTrack, row.sliderFill, row.sliderThumb);
        char display[32]{};
        if (property.widget == ParticleEditorPropertyWidget::IntegerSlider)
            std::snprintf(display, sizeof(display), "%g", static_cast<double>(std::round(property.numericValue)));
        else
            std::snprintf(display, sizeof(display), "%.3g", static_cast<double>(property.numericValue));
        PropertyRow::PaintValue(
            dc, theme, row.valueBox, display, false, property.editable, PropertyRowValueAlignment::Right);
        return;
    }
    if (row.hasToggle) {
        DrawToggle(dc, row.toggle, property.boolValue);
        return;
    }
    if (row.enumChipCount != 0U) {
        for (std::uint8_t index = 0U; index < row.enumChipCount; ++index) {
            const bool active = index == property.enumValue;
            DrawButton(dc, row.enumChips[index],
                property.enumLabels[index] != nullptr ? property.enumLabels[index] : "",
                active ? ParticleEditorButtonTone::Selected : ParticleEditorButtonTone::Neutral, property.editable);
        }
        return;
    }
    if (property.widget == ParticleEditorPropertyWidget::Vector) {
        constexpr const char* axes[] = {"X", "Y", "Z"};
        constexpr COLORREF axisColors[] = {RGB(196, 86, 86), RGB(86, 168, 102), RGB(86, 132, 196)};
        const float values[] = {property.vectorValue.x, property.vectorValue.y, property.vectorValue.z};
        for (int axis = 0; axis < 3; ++axis) {
            FillRound(dc, row.vectorAxes[static_cast<std::size_t>(axis)], RGB(18, 20, 24), axisColors[axis], 4.0F);
            char display[24]{};
            std::snprintf(display, sizeof(display), "%s %.2g", axes[axis], static_cast<double>(values[axis]));
            DrawTextUtf8(dc, row.vectorAxes[static_cast<std::size_t>(axis)], display, kStudioText,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        return;
    }
    if (property.widget == ParticleEditorPropertyWidget::Curve)
        DrawCurvePreview(dc, row.curvePreview, property.curve);
    else if (property.widget == ParticleEditorPropertyWidget::Gradient)
        DrawGradientPreview(dc, row.curvePreview, property.gradient);
    else if (property.widget == ParticleEditorPropertyWidget::Color) {
        DrawColorSwatch(dc, row.colorSwatch, property.colorValue);
        return;
    }
    if (row.valueBox.right > row.valueBox.left)
        PropertyRow::PaintValue(
            dc, theme, row.valueBox, property.value, false, property.editable, PropertyRowValueAlignment::Right);
}

[[nodiscard]] unsigned int WindowDpi(HWND window) noexcept {
    using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
    static_assert(sizeof(GetDpiForWindowFunction) == sizeof(FARPROC));
    static const GetDpiForWindowFunction getDpiForWindow = []() noexcept {
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        const FARPROC address = user32 == nullptr ? nullptr : GetProcAddress(user32, "GetDpiForWindow");
        return address == nullptr ? nullptr : std::bit_cast<GetDpiForWindowFunction>(address);
    }();
    if (getDpiForWindow != nullptr) {
        const UINT dpi = getDpiForWindow(window);
        if (dpi != 0U)
            return dpi;
    }
    HDC dc = GetDC(window);
    if (dc == nullptr)
        return 96U;
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(window, dc);
    return dpi > 0 ? static_cast<unsigned int>(dpi) : 96U;
}

} // namespace

RECT ParticleEditorPanelRenderer::ViewportRect(const RECT& content, unsigned int dpi) noexcept {
    return ParticleEditorPanelLayoutResolver::Resolve(content, {}, 0, dpi).preview;
}

bool ParticleEditorPanelRenderer::PresentViewport(
    EditorSceneBgfxViewport& viewport,
    HWND host,
    const RECT& content,
    const DockPanel& panel,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& settings) {
    const kb::scene::Scene* preview = sceneContext.ParticleEditorPreviewScene();
    const RECT renderRect = ViewportRect(content, WindowDpi(host));
    if (preview == nullptr || renderRect.right <= renderRect.left || renderRect.bottom <= renderRect.top) return false;
    const std::uint64_t revision = sceneContext.ParticleEditorPreviewRevision();
    EditorSceneBgfxViewport::PresentSettings present{};
    present.viewportKey = panel.id;
    present.editorSceneOverlaysEnabled = false;
    present.sceneRevision = revision;
    present.sceneDirtyBaseRevision = revision;
    present.sceneFullSyncRequired = false;
    present.msaaSamples = settings.MsaaSamples();
    present.shadowPassEnabled = false;
    present.postProcessEnabled = true;
    present.selectionMaskEnabled = false;
    present.selectionOutlineEnabled = false;
    present.gpuDrivenRuntimeDispatchEnabled = settings.GpuDrivenEnabled();
    viewport.Present(host, renderRect, *preview, present);
    return true;
}

void ParticleEditorPanelRenderer::Paint(
    HDC dc,
    HWND host,
    const RECT& content,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport* sceneViewport) const {
    HeroIconGdiplusRuntime::EnsureStarted();
    const unsigned int dpi = WindowDpi(host);
    const auto rows = sceneContext.ParticleEditorEmitterRows();
    const auto inspector = sceneContext.ParticleEditorInspector();
    const auto recipes = sceneContext.ParticleEditorRecipes();
    const auto& workspace = sceneContext.ParticleEditorWorkspace();
    const ParticleEditorPanelLayout layout = ParticleEditorPanelLayoutResolver::Resolve(
        content, rows, workspace.ComposerScrollOffset(), dpi, &inspector, recipes.size(), &workspace);
    GdiDrawing::FillRectColor(dc, content, GdiDrawing::ToColorRef(theme.background));
    GdiDrawing::FillRectColor(dc, layout.toolbar, GdiDrawing::ToColorRef(theme.chrome));
    GdiDrawing::FillRectColor(dc, layout.composer, GdiDrawing::ToColorRef(theme.panel));
    GdiDrawing::FillRectColor(dc, layout.statusBar, RGB(14, 15, 18));
    const ScopedFont font{12, FW_SEMIBOLD};
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    RECT header{layout.toolbar.left + 14, layout.toolbar.top, layout.toolbar.right - 14, layout.toolbar.bottom};
    std::string title = "21kb Particle System";
    if (sceneContext.HasParticleEditorAsset()) {
        const auto* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(sceneContext.ParticleEditorAssetId());
        if (metadata != nullptr) title = metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name;
    }
    DrawTextUtf8(dc, header, title.c_str(), kStudioText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (sceneContext.HasParticleEditorAsset() && sceneContext.ParticleEditorDirty()) {
        RECT badge{layout.toolbar.right - 92, layout.toolbar.top + 10, layout.toolbar.right - 16, layout.toolbar.bottom - 10};
        FillRound(dc, badge, RGB(64, 48, 22), kStudioGold, 8.0F);
        DrawTextUtf8(dc, badge, "UNSAVED", kStudioGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (!sceneContext.HasParticleEditorAsset()) {
        RECT empty = layout.preview;
        DrawTextUtf8(dc, empty, "Open a .kbvfx asset to begin editing.", kStudioMuted,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, layout.emitterList.left, layout.emitterList.top,
                      layout.emitterList.right, layout.emitterList.bottom);
    const auto expanded = [&workspace](kb::particle_editor::ParticleEditorComposerSection section) noexcept {
        return workspace.ComposerSectionExpanded(section);
    };
    const std::string emitterCount = std::to_string(rows.size());
    DrawSectionHeader(dc, layout.composerHeader, theme, "Emitters", HeroIconKind::Bolt,
        expanded(kb::particle_editor::ParticleEditorComposerSection::Emitters), emitterCount);
    if (expanded(kb::particle_editor::ParticleEditorComposerSection::Emitters)) {
        DrawButton(dc, layout.addEmitter, "+  Add Emitter", ParticleEditorButtonTone::Primary,
            rows.size() < kb::scene::kParticleEffectMaxEmitters);
    }
    for (std::size_t index = 0U; index < layout.emitterRowCount; ++index) {
        const auto& rowLayout = layout.emitterRows[index];
        const auto& row = rows[index];
        const std::string& name = workspace.RenameActive() &&
                workspace.RenameEmitterId() == row.emitterId
            ? workspace.RenameText() : row.name;
        DenseListRow::Paint(dc, theme, DenseListRowDescriptor{
            .bounds = rowLayout.bounds,
            .title = name,
            .contentLeftInset = static_cast<int>(rowLayout.name.left - rowLayout.bounds.left) + 4,
            .contentRightInset = static_cast<int>(rowLayout.bounds.right - rowLayout.name.right),
            .selected = row.selected,
            .enabled = row.enabled,
        });
        DrawGrip(dc, rowLayout.dragGrip);
        DrawToggle(dc, rowLayout.enabledToggle, row.enabled);
        DrawMoveChevron(dc, rowLayout.moveUp, true);
        DrawMoveChevron(dc, rowLayout.moveDown, false);
        DrawIconButton(dc, rowLayout.remove, HeroIconKind::XMark, ParticleEditorButtonTone::Destructive);
    }
    DrawSectionHeader(dc, layout.propertyHeader, theme, "Emitter Settings", HeroIconKind::AdjustmentsHorizontal,
        expanded(kb::particle_editor::ParticleEditorComposerSection::Settings));
    for (std::size_t index = 0U; index < layout.propertyRowCount; ++index)
        DrawProperty(dc, layout.propertyRows[index], inspector.properties[index], theme);
    const std::string recipeCount = std::to_string(recipes.size());
    DrawSectionHeader(dc, layout.recipeHeader, theme, "Recipes", HeroIconKind::ListBullet,
        expanded(kb::particle_editor::ParticleEditorComposerSection::Recipes), recipeCount);
    for (std::size_t index = 0U; index < layout.recipeTileCount; ++index)
        DrawRecipeTile(dc, layout.recipeTiles[index], recipes[index]);
    const std::string moduleCount = std::to_string(inspector.modules.size());
    DrawSectionHeader(dc, layout.moduleHeader, theme, "Behavior Modules", HeroIconKind::RectangleGroup,
        expanded(kb::particle_editor::ParticleEditorComposerSection::Modules), moduleCount);
    if (expanded(kb::particle_editor::ParticleEditorComposerSection::Modules)) {
        DrawButton(dc, layout.addModule, "+  Add Module", ParticleEditorButtonTone::Primary,
            inspector.modules.size() < kb::scene::kParticleEffectMaxModulesPerEmitter);
    }
    for (std::size_t index = 0U; index < layout.moduleRowCount; ++index) {
        const auto& module = inspector.modules[index];
        const auto& row = layout.moduleRows[index];
        DenseListRow::Paint(dc, theme, DenseListRowDescriptor{
            .bounds = row.bounds,
            .title = module.label,
            .summary = module.summary,
            .contentLeftInset = static_cast<int>(row.name.left - row.bounds.left) + 4,
            .contentRightInset = static_cast<int>(row.bounds.right - row.name.right),
            .selected = module.selected,
            .enabled = module.enabled,
        });
        DrawGrip(dc, row.dragGrip);
        DrawToggle(dc, row.enabledToggle, module.enabled);
        DrawMoveChevron(dc, row.moveUp, true);
        DrawMoveChevron(dc, row.moveDown, false);
        DrawIconButton(dc, row.remove, HeroIconKind::XMark, ParticleEditorButtonTone::Destructive);
    }
    DrawSectionHeader(dc, layout.outputHeader, theme, "Renderer", HeroIconKind::Cube,
        expanded(kb::particle_editor::ParticleEditorComposerSection::Output));
    if (expanded(kb::particle_editor::ParticleEditorComposerSection::Output)) {
        DrawButton(dc, layout.materialPicker, "Material");
        DrawButton(dc, layout.meshPicker, "Mesh");
        DrawButton(dc, layout.texturePicker, "Atlas");
    }
    for (std::size_t index = 0U; index < layout.outputChoiceCount; ++index) {
        const auto& choice = inspector.outputChoices[index];
        const auto* asset = sceneContext.ParticleEditorWorkingAsset();
        const auto* emitter = asset == nullptr ? nullptr : kb::particle_editor::ParticleEmitterListModel::Find(*asset, inspector.emitterId);
        const bool active = emitter != nullptr && emitter->output.type == choice.type;
        DrawButton(dc, layout.outputChoices[index], choice.label.c_str(),
            active ? ParticleEditorButtonTone::Selected : ParticleEditorButtonTone::Neutral, choice.enabled);
        if (!choice.enabled) {
            RECT mark = layout.outputChoices[index];
            mark.left = mark.right - 36;
            DrawTextUtf8(dc, mark, "N/A", RGB(176, 132, 132), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }
    if (workspace.ModuleDragActive() && layout.moduleRowCount != 0U) {
        const std::uint32_t order = std::min<std::uint32_t>(workspace.ModuleDragTargetOrder(),
            static_cast<std::uint32_t>(layout.moduleRowCount - 1U));
        const RECT& target = layout.moduleRows[order].bounds;
        GdiDrawing::FillRectColor(dc, {target.left, target.top - 1, target.right, target.top + 2}, kStudioGold);
    }
    const std::string dependencyCount = std::to_string(inspector.dependencies.size());
    DrawSectionHeader(dc, layout.dependencyHeader, theme, "Dependencies", HeroIconKind::Folder,
        expanded(kb::particle_editor::ParticleEditorComposerSection::Dependencies), dependencyCount);
    for (std::size_t index = 0U; index < layout.dependencyRowCount; ++index) {
        DenseListRow::Paint(dc, theme, DenseListRowDescriptor{
            .bounds = layout.dependencyRows[index],
            .title = inspector.dependencies[index].virtualPath,
            .icon = HeroIconKind::Folder,
            .showIcon = true,
        });
    }
    const std::string diagnosticCount = std::to_string(inspector.diagnostics.size());
    DrawSectionHeader(dc, layout.diagnosticHeader, theme, "Diagnostics", HeroIconKind::CommandLine,
        expanded(kb::particle_editor::ParticleEditorComposerSection::Diagnostics), diagnosticCount);
    for (std::size_t index = 0U; index < layout.diagnosticRowCount; ++index) {
        FillRound(dc, layout.diagnosticRows[index], RGB(48, 28, 30), RGB(128, 72, 76), 4.0F);
        RECT text = layout.diagnosticRows[index];
        text.left += 8;
        const std::string diagnostic = kb::scene::FormatParticleEffectDiagnostic(inspector.diagnostics[index]);
        DrawTextUtf8(dc, text, diagnostic.c_str(), RGB(226, 168, 168), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (workspace.EmitterDragActive() && layout.emitterRowCount != 0U) {
        const std::uint32_t order = std::min<std::uint32_t>(
            workspace.DragTargetOrder(), static_cast<std::uint32_t>(layout.emitterRowCount - 1U));
        const RECT& target = layout.emitterRows[order].bounds;
        GdiDrawing::FillRectColor(dc,
            {target.left, target.top - 1, target.right, target.top + 2}, kStudioGold);
    }
    RestoreDC(dc, saved);
    if (layout.composerScrollThumb.bottom > layout.composerScrollThumb.top) {
        FillRound(dc, layout.composerScrollTrack, RGB(14, 16, 19), RGB(14, 16, 19), 4.0F);
        FillRound(dc, layout.composerScrollThumb, RGB(86, 94, 108), RGB(110, 118, 132), 4.0F);
    }
    const bool dirty = sceneContext.ParticleEditorDirty();
    RECT dot{layout.statusBar.left + 12, layout.statusBar.top + 7,
             layout.statusBar.left + 20, layout.statusBar.bottom - 7};
    FillEllipse(dc, dot, dirty ? kStudioGold : kStudioToggle, dirty ? kStudioGoldSoft : RGB(48, 96, 72));
    RECT status = layout.statusBar;
    status.left += 26;
    const std::string statusText = std::to_string(rows.size()) + " / " +
        std::to_string(kb::scene::kParticleEffectMaxEmitters) + " emitters   ·   " +
        (dirty ? "Unsaved changes" : "Saved") + "   ·   LMB orbit  ·  Wheel zoom";
    DrawTextUtf8(dc, status, statusText.c_str(), kStudioMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (sceneViewport != nullptr)
        static_cast<void>(PresentViewport(*sceneViewport, host, content, panel, sceneContext, renderBackendSettings));
}

} // namespace kb::editor
#endif
