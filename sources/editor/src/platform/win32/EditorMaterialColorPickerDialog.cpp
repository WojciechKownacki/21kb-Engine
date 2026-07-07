#include "platform/win32/EditorMaterialColorPickerDialog.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kColorPickerClassName[] = L"KBEditorMaterialColorPickerDialog";
constexpr int kDialogWidth = 568;
constexpr int kDialogHeight = 438;
constexpr int kPad = 18;
constexpr int kTitleHeight = 52;
constexpr int kButtonWidth = 86;
constexpr int kButtonHeight = 28;

enum class PickerField : std::uint8_t {
    None,
    Hex,
    Red,
    Green,
    Blue,
    Hue,
    Saturation,
    Value,
    Alpha,
};

enum class DragKind : std::uint8_t {
    None,
    SaturationValue,
    Hue,
    Alpha,
};

struct HsvColor {
    float h = 0.0F;
    float s = 0.0F;
    float v = 0.0F;
};

[[nodiscard]] int RectWidth(const RECT& rect) noexcept {
    return std::max(0L, rect.right - rect.left);
}

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept {
    return RECT{ left, top, right, bottom };
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] float Clamp01(float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] int ColorByte(float value) noexcept {
    return std::clamp(static_cast<int>(std::round(Clamp01(value) * 255.0F)), 0, 255);
}

[[nodiscard]] COLORREF Rgb(int r, int g, int b) noexcept {
    return RGB(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
}

[[nodiscard]] COLORREF RgbaColor(const std::array<float, 4U>& rgba) noexcept {
    return Rgb(ColorByte(rgba[0]), ColorByte(rgba[1]), ColorByte(rgba[2]));
}

[[nodiscard]] std::array<float, 3U> HsvToRgb(HsvColor hsv) noexcept {
    const float hue = std::fmod(std::max(0.0F, hsv.h), 360.0F);
    const float saturation = Clamp01(hsv.s);
    const float value = Clamp01(hsv.v);
    const float chroma = value * saturation;
    const float hPrime = hue / 60.0F;
    const float x = chroma * (1.0F - std::fabs(std::fmod(hPrime, 2.0F) - 1.0F));
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    if (hPrime < 1.0F) {
        r = chroma;
        g = x;
    } else if (hPrime < 2.0F) {
        r = x;
        g = chroma;
    } else if (hPrime < 3.0F) {
        g = chroma;
        b = x;
    } else if (hPrime < 4.0F) {
        g = x;
        b = chroma;
    } else if (hPrime < 5.0F) {
        r = x;
        b = chroma;
    } else {
        r = chroma;
        b = x;
    }
    const float m = value - chroma;
    return { r + m, g + m, b + m };
}

[[nodiscard]] HsvColor RgbToHsv(const std::array<float, 4U>& rgba) noexcept {
    const float r = Clamp01(rgba[0]);
    const float g = Clamp01(rgba[1]);
    const float b = Clamp01(rgba[2]);
    const float maxValue = std::max({ r, g, b });
    const float minValue = std::min({ r, g, b });
    const float delta = maxValue - minValue;
    float hue = 0.0F;
    if (delta > 0.00001F) {
        if (maxValue == r) {
            hue = 60.0F * std::fmod(((g - b) / delta), 6.0F);
        } else if (maxValue == g) {
            hue = 60.0F * (((b - r) / delta) + 2.0F);
        } else {
            hue = 60.0F * (((r - g) / delta) + 4.0F);
        }
        if (hue < 0.0F) {
            hue += 360.0F;
        }
    }
    return HsvColor{
        .h = hue,
        .s = maxValue <= 0.00001F ? 0.0F : delta / maxValue,
        .v = maxValue,
    };
}

[[nodiscard]] std::string IntText(int value) {
    char buffer[24]{};
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return std::string{ buffer };
}

[[nodiscard]] std::string HexText(const std::array<float, 4U>& rgba) {
    char buffer[16]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "#%02X%02X%02X%02X",
        ColorByte(rgba[0]),
        ColorByte(rgba[1]),
        ColorByte(rgba[2]),
        ColorByte(rgba[3]));
    return std::string{ buffer };
}

[[nodiscard]] int HexNibble(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

[[nodiscard]] std::optional<std::array<float, 4U>> ParseHexColor(std::string_view text, float fallbackAlpha) {
    std::string hex;
    hex.reserve(text.size());
    for (const char ch : text) {
        if (ch != '#' && !std::isspace(static_cast<unsigned char>(ch))) {
            hex.push_back(ch);
        }
    }
    if (hex.size() != 6U && hex.size() != 8U) {
        return std::nullopt;
    }
    std::array<int, 4U> channels{ 0, 0, 0, ColorByte(fallbackAlpha) };
    for (std::size_t channel = 0U; channel < (hex.size() / 2U); ++channel) {
        const int hi = HexNibble(hex[channel * 2U]);
        const int lo = HexNibble(hex[channel * 2U + 1U]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        channels[channel] = (hi * 16) + lo;
    }
    return std::array<float, 4U>{
        static_cast<float>(channels[0]) / 255.0F,
        static_cast<float>(channels[1]) / 255.0F,
        static_cast<float>(channels[2]) / 255.0F,
        static_cast<float>(channels[3]) / 255.0F,
    };
}

[[nodiscard]] std::optional<float> ParseFloat(std::string_view text) {
    std::string copy{ text };
    char* end = nullptr;
    const float value = std::strtof(copy.c_str(), &end);
    if (end == copy.c_str()) {
        return std::nullopt;
    }
    while (end != nullptr && *end != '\0') {
        if (!std::isspace(static_cast<unsigned char>(*end))) {
            return std::nullopt;
        }
        ++end;
    }
    return value;
}

void Text(HDC dc, RECT rect, std::string_view text, COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text.data(), static_cast<int>(text.size()), &rect, static_cast<int>(format | DT_NOPREFIX));
}

void DrawCheckerboard(HDC dc, RECT rect, int cell) {
    const int safeCell = std::max(3, cell);
    for (int y = rect.top; y < rect.bottom; y += safeCell) {
        for (int x = rect.left; x < rect.right; x += safeCell) {
            const bool light = (((x - rect.left) / safeCell) + ((y - rect.top) / safeCell)) % 2 == 0;
            GdiDrawing::FillRectColor(
                dc,
                Rect(x, y, std::min<LONG>(rect.right, x + safeCell), std::min<LONG>(rect.bottom, y + safeCell)),
                light ? Rgb(78, 83, 90) : Rgb(42, 46, 52));
        }
    }
}

class ColorPickerWindow {
public:
    ColorPickerWindow(std::string title, const std::array<float, 4U>& color)
        : title_(std::move(title))
        , rgba_(color)
        , hsv_(RgbToHsv(color)) {
        ClampColor();
        RefreshFields();
    }

    [[nodiscard]] std::optional<std::array<float, 4U>> Show(HWND owner) {
        owner_ = owner;
        if (!EnsureWindow()) {
            return std::nullopt;
        }
        const RECT bounds = CenteredWindowRect(owner);
        EnableOwner(false);
        SetWindowPos(window_, HWND_TOPMOST, bounds.left, bounds.top, RectWidth(bounds), RectHeight(bounds), SWP_SHOWWINDOW);
        SetForegroundWindow(window_);

        MSG message{};
        while (running_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        EnableOwner(true);
        if (window_ != nullptr && IsWindow(window_) != 0) {
            DestroyWindow(window_);
            window_ = nullptr;
        }
        return accepted_ ? std::optional<std::array<float, 4U>>{ rgba_ } : std::nullopt;
    }

private:
    [[nodiscard]] bool EnsureWindow() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &ColorPickerWindow::WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = kColorPickerClassName;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        window_ = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kColorPickerClassName,
            L"Material Color",
            WS_POPUP,
            0,
            0,
            kDialogWidth,
            kDialogHeight,
            owner_,
            nullptr,
            windowClass.hInstance,
            this);
        return window_ != nullptr;
    }

    void EnableOwner(bool enabled) const noexcept {
        if (owner_ != nullptr && IsWindow(owner_) != 0) {
            EnableWindow(owner_, enabled ? TRUE : FALSE);
        }
    }

    [[nodiscard]] RECT Client() const noexcept {
        RECT client{};
        GetClientRect(window_, &client);
        return client;
    }

    [[nodiscard]] RECT CenteredWindowRect(HWND owner) const {
        RECT base{};
        if (owner != nullptr && IsWindow(owner) != 0) {
            GetWindowRect(owner, &base);
        } else {
            base = RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        }
        const int left = base.left + std::max(0, (RectWidth(base) - kDialogWidth) / 2);
        const int top = base.top + std::max(0, (RectHeight(base) - kDialogHeight) / 2);
        return Rect(left, top, left + kDialogWidth, top + kDialogHeight);
    }

    [[nodiscard]] RECT SaturationValueRect() const noexcept {
        return Rect(kPad, kTitleHeight + 18, kPad + 242, kTitleHeight + 198);
    }

    [[nodiscard]] RECT HueRect() const noexcept {
        const RECT sv = SaturationValueRect();
        return Rect(sv.right + 12, sv.top, sv.right + 34, sv.bottom);
    }

    [[nodiscard]] RECT AlphaRect() const noexcept {
        const RECT sv = SaturationValueRect();
        return Rect(sv.left, sv.bottom + 22, sv.right, sv.bottom + 42);
    }

    [[nodiscard]] RECT PreviewRect() const noexcept {
        return Rect(326, kTitleHeight + 18, 526, kTitleHeight + 78);
    }

    [[nodiscard]] RECT SwatchRect(std::size_t index) const noexcept {
        constexpr int chip = 22;
        constexpr int gap = 8;
        const int left = 326 + static_cast<int>(index % 7U) * (chip + gap);
        const int top = kTitleHeight + 96 + static_cast<int>(index / 7U) * (chip + gap);
        return Rect(left, top, left + chip, top + chip);
    }

    [[nodiscard]] RECT FieldRect(PickerField field) const noexcept {
        const int index = FieldIndex(field);
        if (index < 0) {
            return {};
        }
        const int top = kTitleHeight + 138 + index * 24;
        return Rect(372, top, 526, top + 20);
    }

    [[nodiscard]] RECT FieldLabelRect(PickerField field) const noexcept {
        const RECT fieldRect = FieldRect(field);
        return Rect(326, fieldRect.top, 366, fieldRect.bottom);
    }

    [[nodiscard]] RECT OkButtonRect() const noexcept {
        const RECT client = Client();
        return Rect(client.right - kPad - kButtonWidth, client.bottom - kPad - kButtonHeight, client.right - kPad, client.bottom - kPad);
    }

    [[nodiscard]] RECT CancelButtonRect() const noexcept {
        const RECT ok = OkButtonRect();
        return Rect(ok.left - 10 - kButtonWidth, ok.top, ok.left - 10, ok.bottom);
    }

    [[nodiscard]] RECT EyedropperButtonRect() const noexcept {
        const RECT cancel = CancelButtonRect();
        return Rect(kPad, cancel.top, kPad + 132, cancel.bottom);
    }

    [[nodiscard]] static int FieldIndex(PickerField field) noexcept {
        switch (field) {
        case PickerField::Hex: return 0;
        case PickerField::Red: return 1;
        case PickerField::Green: return 2;
        case PickerField::Blue: return 3;
        case PickerField::Hue: return 4;
        case PickerField::Saturation: return 5;
        case PickerField::Value: return 6;
        case PickerField::Alpha: return 7;
        case PickerField::None: break;
        }
        return -1;
    }

    [[nodiscard]] std::string& FieldText(PickerField field) noexcept {
        return fieldTexts_[static_cast<std::size_t>(std::max(0, FieldIndex(field)))];
    }

    [[nodiscard]] const std::string& FieldText(PickerField field) const noexcept {
        return fieldTexts_[static_cast<std::size_t>(std::max(0, FieldIndex(field)))];
    }

    void ClampColor() noexcept {
        for (float& component : rgba_) {
            component = Clamp01(component);
        }
        hsv_.h = std::clamp(hsv_.h, 0.0F, 360.0F);
        hsv_.s = Clamp01(hsv_.s);
        hsv_.v = Clamp01(hsv_.v);
    }

    void RefreshFields() {
        fieldTexts_[0] = HexText(rgba_);
        fieldTexts_[1] = IntText(ColorByte(rgba_[0]));
        fieldTexts_[2] = IntText(ColorByte(rgba_[1]));
        fieldTexts_[3] = IntText(ColorByte(rgba_[2]));
        fieldTexts_[4] = IntText(static_cast<int>(std::round(hsv_.h)));
        fieldTexts_[5] = IntText(static_cast<int>(std::round(hsv_.s * 100.0F)));
        fieldTexts_[6] = IntText(static_cast<int>(std::round(hsv_.v * 100.0F)));
        fieldTexts_[7] = IntText(static_cast<int>(std::round(rgba_[3] * 100.0F)));
    }

    void SetRgba(std::array<float, 4U> color) {
        rgba_ = color;
        ClampColor();
        hsv_ = RgbToHsv(rgba_);
        RefreshFields();
        Invalidate();
    }

    void SetHsv(HsvColor hsv) {
        hsv_ = hsv;
        ClampColor();
        const std::array<float, 3U> rgb = HsvToRgb(hsv_);
        rgba_[0] = rgb[0];
        rgba_[1] = rgb[1];
        rgba_[2] = rgb[2];
        RefreshFields();
        Invalidate();
    }

    void Invalidate() const noexcept {
        if (window_ != nullptr) {
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    void Paint(HDC dc) const {
        const RECT client = Client();
        GdiDrawing::FillRectColor(dc, client, Rgb(15, 17, 22));
        GdiDrawing::DrawSharpFrame(dc, client, Rgb(15, 17, 22), Rgb(77, 89, 106));
        GdiDrawing::FillRectColor(dc, Rect(1, 1, client.right - 1, kTitleHeight), Rgb(22, 27, 35));
        {
            ScopedFont titleFont(15, FW_SEMIBOLD);
            const ScopedGdiObject selectedFont(dc, titleFont.handle);
            Text(dc, Rect(kPad, 8, client.right - kPad, 30), title_.empty() ? std::string_view{ "Material Color" } : std::string_view{ title_ }, Rgb(235, 241, 249));
        }
        Text(dc, Rect(kPad, 30, client.right - kPad, 50), "HSV, alpha, HEX, RGB and presets", Rgb(153, 168, 190));

        DrawSaturationValue(dc);
        DrawHue(dc);
        DrawAlpha(dc);
        DrawPreview(dc);
        DrawSwatches(dc);
        DrawFields(dc);
        DrawButton(dc, EyedropperButtonRect(), eyedropperArmed_ ? "Click screen" : "Pick screen", eyedropperArmed_);
        DrawButton(dc, CancelButtonRect(), "Cancel", false);
        DrawButton(dc, OkButtonRect(), "Apply", true);
    }

    void DrawSaturationValue(HDC dc) const {
        const RECT rect = SaturationValueRect();
        const int width = std::max(1, RectWidth(rect));
        const int height = std::max(1, RectHeight(rect));
        for (int y = 0; y < height; ++y) {
            const float value = 1.0F - (static_cast<float>(y) / static_cast<float>(std::max(1, height - 1)));
            for (int x = 0; x < width; ++x) {
                const float saturation = static_cast<float>(x) / static_cast<float>(std::max(1, width - 1));
                const std::array<float, 3U> rgb = HsvToRgb(HsvColor{ .h = hsv_.h, .s = saturation, .v = value });
                SetPixelV(dc, rect.left + x, rect.top + y, Rgb(ColorByte(rgb[0]), ColorByte(rgb[1]), ColorByte(rgb[2])));
            }
        }
        GdiDrawing::DrawSharpFrame(dc, rect, Rgb(0, 0, 0), Rgb(83, 96, 116));
        const int markerX = rect.left + static_cast<int>(std::round(hsv_.s * static_cast<float>(width - 1)));
        const int markerY = rect.top + static_cast<int>(std::round((1.0F - hsv_.v) * static_cast<float>(height - 1)));
        DrawMarker(dc, markerX, markerY);
    }

    void DrawHue(HDC dc) const {
        const RECT rect = HueRect();
        const int height = std::max(1, RectHeight(rect));
        for (int y = 0; y < height; ++y) {
            const float hue = (static_cast<float>(y) / static_cast<float>(std::max(1, height - 1))) * 360.0F;
            const std::array<float, 3U> rgb = HsvToRgb(HsvColor{ .h = hue, .s = 1.0F, .v = 1.0F });
            GdiDrawing::FillRectColor(dc, Rect(rect.left, rect.top + y, rect.right, rect.top + y + 1), Rgb(ColorByte(rgb[0]), ColorByte(rgb[1]), ColorByte(rgb[2])));
        }
        GdiDrawing::DrawSharpFrame(dc, rect, Rgb(0, 0, 0), Rgb(83, 96, 116));
        const int markerY = rect.top + static_cast<int>(std::round((hsv_.h / 360.0F) * static_cast<float>(height - 1)));
        GdiDrawing::FillRectColor(dc, Rect(rect.left - 3, markerY - 2, rect.right + 3, markerY + 3), Rgb(245, 248, 252));
    }

    void DrawAlpha(HDC dc) const {
        const RECT rect = AlphaRect();
        DrawCheckerboard(dc, rect, 7);
        const int width = std::max(1, RectWidth(rect));
        for (int x = 0; x < width; ++x) {
            const BYTE alpha = static_cast<BYTE>(std::clamp((x * 255) / std::max(1, width - 1), 0, 255));
            GdiDrawing::FillRectAlpha(dc, Rect(rect.left + x, rect.top, rect.left + x + 1, rect.bottom), RgbaColor(rgba_), alpha);
        }
        GdiDrawing::DrawSharpFrame(dc, rect, Rgb(0, 0, 0), Rgb(83, 96, 116));
        const int markerX = rect.left + static_cast<int>(std::round(rgba_[3] * static_cast<float>(width - 1)));
        GdiDrawing::FillRectColor(dc, Rect(markerX - 2, rect.top - 3, markerX + 3, rect.bottom + 3), Rgb(245, 248, 252));
    }

    void DrawPreview(HDC dc) const {
        const RECT rect = PreviewRect();
        DrawCheckerboard(dc, rect, 9);
        GdiDrawing::FillRectAlpha(dc, rect, RgbaColor(rgba_), static_cast<BYTE>(ColorByte(rgba_[3])));
        GdiDrawing::DrawSharpFrame(dc, rect, Rgb(0, 0, 0), Rgb(83, 96, 116));
        Text(dc, Rect(rect.left + 10, rect.top, rect.right - 10, rect.bottom), HexText(rgba_), Rgb(245, 248, 252), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void DrawSwatches(HDC dc) const {
        for (std::size_t index = 0U; index < kPresetColors.size(); ++index) {
            const RECT chip = SwatchRect(index);
            DrawCheckerboard(dc, chip, 5);
            GdiDrawing::FillRectAlpha(dc, chip, RgbaColor(kPresetColors[index]), static_cast<BYTE>(ColorByte(kPresetColors[index][3])));
            GdiDrawing::DrawSharpFrame(dc, chip, Rgb(0, 0, 0), Rgb(83, 96, 116));
        }
    }

    void DrawFields(HDC dc) const {
        static constexpr std::array<PickerField, 8U> fields{
            PickerField::Hex,
            PickerField::Red,
            PickerField::Green,
            PickerField::Blue,
            PickerField::Hue,
            PickerField::Saturation,
            PickerField::Value,
            PickerField::Alpha,
        };
        static constexpr std::array<std::string_view, 8U> labels{ "HEX", "R", "G", "B", "H", "S", "V", "A" };
        for (std::size_t index = 0U; index < fields.size(); ++index) {
            const RECT label = FieldLabelRect(fields[index]);
            const RECT field = FieldRect(fields[index]);
            Text(dc, label, labels[index], Rgb(153, 168, 190), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            const bool focused = focusedField_ == fields[index];
            GdiDrawing::DrawSharpFrame(dc, field, focused ? Rgb(31, 45, 62) : Rgb(22, 26, 33), focused ? Rgb(111, 171, 235) : Rgb(66, 76, 91));
            std::string text = FieldText(fields[index]);
            if (focused) {
                text += "|";
            }
            Text(dc, Rect(field.left + 7, field.top, field.right - 7, field.bottom), text, Rgb(231, 238, 247));
        }
    }

    void DrawButton(HDC dc, RECT rect, std::string_view label, bool emphasized) const {
        GdiDrawing::DrawSharpFrame(dc, rect, emphasized ? Rgb(39, 68, 72) : Rgb(25, 29, 36), emphasized ? Rgb(96, 178, 184) : Rgb(72, 84, 102));
        Text(dc, rect, label, emphasized ? Rgb(237, 255, 255) : Rgb(218, 228, 240), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    static void DrawMarker(HDC dc, int x, int y) {
        GdiDrawing::FillRectColor(dc, Rect(x - 7, y - 1, x + 8, y + 2), Rgb(0, 0, 0));
        GdiDrawing::FillRectColor(dc, Rect(x - 1, y - 7, x + 2, y + 8), Rgb(0, 0, 0));
        GdiDrawing::FillRectColor(dc, Rect(x - 6, y, x + 7, y + 1), Rgb(245, 248, 252));
        GdiDrawing::FillRectColor(dc, Rect(x, y - 6, x + 1, y + 7), Rgb(245, 248, 252));
    }

    void UpdateFromSaturationValue(int x, int y) {
        const RECT rect = SaturationValueRect();
        const int clampedX = std::clamp<int>(x, static_cast<int>(rect.left), static_cast<int>(rect.right - 1));
        const int clampedY = std::clamp<int>(y, static_cast<int>(rect.top), static_cast<int>(rect.bottom - 1));
        const float saturation = static_cast<float>(clampedX - rect.left) / static_cast<float>(std::max(1, RectWidth(rect) - 1));
        const float value = 1.0F - (static_cast<float>(clampedY - rect.top) / static_cast<float>(std::max(1, RectHeight(rect) - 1)));
        SetHsv(HsvColor{ .h = hsv_.h, .s = saturation, .v = value });
    }

    void UpdateFromHue(int y) {
        const RECT rect = HueRect();
        const int clampedY = std::clamp<int>(y, static_cast<int>(rect.top), static_cast<int>(rect.bottom - 1));
        const float hue = (static_cast<float>(clampedY - rect.top) / static_cast<float>(std::max(1, RectHeight(rect) - 1))) * 360.0F;
        SetHsv(HsvColor{ .h = hue, .s = hsv_.s, .v = hsv_.v });
    }

    void UpdateFromAlpha(int x) {
        const RECT rect = AlphaRect();
        const int clampedX = std::clamp<int>(x, static_cast<int>(rect.left), static_cast<int>(rect.right - 1));
        rgba_[3] = static_cast<float>(clampedX - rect.left) / static_cast<float>(std::max(1, RectWidth(rect) - 1));
        RefreshFields();
        Invalidate();
    }

    void ApplyFocusedField() {
        switch (focusedField_) {
        case PickerField::Hex:
            if (const std::optional<std::array<float, 4U>> parsed = ParseHexColor(FieldText(PickerField::Hex), rgba_[3])) {
                SetRgba(*parsed);
            }
            break;
        case PickerField::Red:
        case PickerField::Green:
        case PickerField::Blue: {
            if (const std::optional<float> parsed = ParseFloat(FieldText(focusedField_))) {
                std::array<float, 4U> next = rgba_;
                const std::size_t channel = focusedField_ == PickerField::Red ? 0U : focusedField_ == PickerField::Green ? 1U : 2U;
                next[channel] = std::clamp(*parsed, 0.0F, 255.0F) / 255.0F;
                SetRgba(next);
            }
            break;
        }
        case PickerField::Hue:
            if (const std::optional<float> parsed = ParseFloat(FieldText(PickerField::Hue))) {
                SetHsv(HsvColor{ .h = std::clamp(*parsed, 0.0F, 360.0F), .s = hsv_.s, .v = hsv_.v });
            }
            break;
        case PickerField::Saturation:
            if (const std::optional<float> parsed = ParseFloat(FieldText(PickerField::Saturation))) {
                SetHsv(HsvColor{ .h = hsv_.h, .s = std::clamp(*parsed, 0.0F, 100.0F) / 100.0F, .v = hsv_.v });
            }
            break;
        case PickerField::Value:
            if (const std::optional<float> parsed = ParseFloat(FieldText(PickerField::Value))) {
                SetHsv(HsvColor{ .h = hsv_.h, .s = hsv_.s, .v = std::clamp(*parsed, 0.0F, 100.0F) / 100.0F });
            }
            break;
        case PickerField::Alpha:
            if (const std::optional<float> parsed = ParseFloat(FieldText(PickerField::Alpha))) {
                rgba_[3] = std::clamp(*parsed, 0.0F, 100.0F) / 100.0F;
                RefreshFields();
                Invalidate();
            }
            break;
        case PickerField::None:
            break;
        }
    }

    [[nodiscard]] PickerField FieldAt(int x, int y) const noexcept {
        static constexpr std::array<PickerField, 8U> fields{
            PickerField::Hex,
            PickerField::Red,
            PickerField::Green,
            PickerField::Blue,
            PickerField::Hue,
            PickerField::Saturation,
            PickerField::Value,
            PickerField::Alpha,
        };
        for (const PickerField field : fields) {
            if (Contains(FieldRect(field), x, y)) {
                return field;
            }
        }
        return PickerField::None;
    }

    void Accept() {
        ApplyFocusedField();
        accepted_ = true;
        running_ = false;
        DestroyWindow(window_);
    }

    void Cancel() {
        accepted_ = false;
        running_ = false;
        DestroyWindow(window_);
    }

    void ArmEyedropper() {
        eyedropperArmed_ = true;
        SetCapture(window_);
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32515)));
        Invalidate();
    }

    void SampleScreenAtClientPoint(int x, int y) {
        POINT screen{ x, y };
        ClientToScreen(window_, &screen);
        HDC screenDc = GetDC(nullptr);
        if (screenDc != nullptr) {
            const COLORREF sampled = GetPixel(screenDc, screen.x, screen.y);
            ReleaseDC(nullptr, screenDc);
            if (sampled != CLR_INVALID) {
                SetRgba(std::array<float, 4U>{
                    static_cast<float>(GetRValue(sampled)) / 255.0F,
                    static_cast<float>(GetGValue(sampled)) / 255.0F,
                    static_cast<float>(GetBValue(sampled)) / 255.0F,
                    rgba_[3],
                });
            }
        }
        eyedropperArmed_ = false;
        ReleaseCapture();
        Invalidate();
    }

    void OnMouseDown(int x, int y) {
        if (eyedropperArmed_) {
            SampleScreenAtClientPoint(x, y);
            return;
        }
        ApplyFocusedField();
        if (Contains(OkButtonRect(), x, y)) {
            Accept();
            return;
        }
        if (Contains(CancelButtonRect(), x, y)) {
            Cancel();
            return;
        }
        if (Contains(EyedropperButtonRect(), x, y)) {
            ArmEyedropper();
            return;
        }
        for (std::size_t index = 0U; index < kPresetColors.size(); ++index) {
            if (Contains(SwatchRect(index), x, y)) {
                SetRgba(kPresetColors[index]);
                return;
            }
        }
        if (const PickerField field = FieldAt(x, y); field != PickerField::None) {
            focusedField_ = field;
            Invalidate();
            return;
        }
        focusedField_ = PickerField::None;
        if (Contains(SaturationValueRect(), x, y)) {
            dragKind_ = DragKind::SaturationValue;
            SetCapture(window_);
            UpdateFromSaturationValue(x, y);
            return;
        }
        if (Contains(HueRect(), x, y)) {
            dragKind_ = DragKind::Hue;
            SetCapture(window_);
            UpdateFromHue(y);
            return;
        }
        if (Contains(AlphaRect(), x, y)) {
            dragKind_ = DragKind::Alpha;
            SetCapture(window_);
            UpdateFromAlpha(x);
        }
    }

    void OnMouseMove(int x, int y) {
        if (dragKind_ == DragKind::SaturationValue) {
            UpdateFromSaturationValue(x, y);
        } else if (dragKind_ == DragKind::Hue) {
            UpdateFromHue(y);
        } else if (dragKind_ == DragKind::Alpha) {
            UpdateFromAlpha(x);
        }
    }

    void OnMouseUp() {
        if (dragKind_ != DragKind::None) {
            dragKind_ = DragKind::None;
            ReleaseCapture();
        }
    }

    void OnChar(WPARAM wparam) {
        if (focusedField_ == PickerField::None || wparam < 32U || wparam > 126U) {
            return;
        }
        std::string& text = FieldText(focusedField_);
        if (text.size() < 16U) {
            text.push_back(static_cast<char>(wparam));
            Invalidate();
        }
    }

    void OnKeyDown(WPARAM wparam) {
        if (wparam == VK_ESCAPE) {
            if (eyedropperArmed_) {
                eyedropperArmed_ = false;
                ReleaseCapture();
                Invalidate();
            } else {
                Cancel();
            }
            return;
        }
        if (wparam == VK_RETURN) {
            ApplyFocusedField();
            return;
        }
        if (wparam == VK_BACK && focusedField_ != PickerField::None) {
            std::string& text = FieldText(focusedField_);
            if (!text.empty()) {
                text.pop_back();
                Invalidate();
            }
            return;
        }
        if (wparam == VK_TAB) {
            const int index = FieldIndex(focusedField_);
            focusedField_ = static_cast<PickerField>((std::max(0, index) + 1) % 8 + 1);
            Invalidate();
        }
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* picker = reinterpret_cast<ColorPickerWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        switch (message) {
        case WM_NCCREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            if (picker != nullptr) {
                picker->Paint(dc);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (picker != nullptr) {
                picker->OnMouseDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                return 0;
            }
            break;
        case WM_MOUSEMOVE:
            if (picker != nullptr) {
                picker->OnMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (picker != nullptr) {
                picker->OnMouseUp();
                return 0;
            }
            break;
        case WM_CHAR:
            if (picker != nullptr) {
                picker->OnChar(wparam);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (picker != nullptr) {
                picker->OnKeyDown(wparam);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (picker != nullptr) {
                picker->running_ = false;
            }
            DestroyWindow(window);
            return 0;
        case WM_NCDESTROY:
            if (picker != nullptr && picker->window_ == window) {
                picker->window_ = nullptr;
            }
            break;
        default:
            break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    std::string title_;
    std::array<float, 4U> rgba_{};
    HsvColor hsv_{};
    std::array<std::string, 8U> fieldTexts_{};
    PickerField focusedField_ = PickerField::Hex;
    DragKind dragKind_ = DragKind::None;
    bool running_ = true;
    bool accepted_ = false;
    bool eyedropperArmed_ = false;

    static constexpr std::array<std::array<float, 4U>, 14U> kPresetColors{ {
        { 1.0F, 1.0F, 1.0F, 1.0F },
        { 0.0F, 0.0F, 0.0F, 1.0F },
        { 0.93F, 0.18F, 0.24F, 1.0F },
        { 1.0F, 0.55F, 0.18F, 1.0F },
        { 1.0F, 0.82F, 0.24F, 1.0F },
        { 0.22F, 0.74F, 0.42F, 1.0F },
        { 0.24F, 0.62F, 1.0F, 1.0F },
        { 0.62F, 0.36F, 0.95F, 1.0F },
        { 0.0F, 0.0F, 0.0F, 0.0F },
        { 0.15F, 0.18F, 0.22F, 1.0F },
        { 0.54F, 0.67F, 0.82F, 1.0F },
        { 0.90F, 0.77F, 0.55F, 1.0F },
        { 0.32F, 0.88F, 0.78F, 1.0F },
        { 1.0F, 0.32F, 0.60F, 1.0F },
    } };
};

} // namespace

std::optional<std::array<float, 4U>> EditorMaterialColorPickerDialog::Show(
    HWND owner,
    std::string_view title,
    const std::array<float, 4U>& currentColor) {
    ColorPickerWindow window{ std::string{ title }, currentColor };
    return window.Show(owner);
}

} // namespace kb::editor

#endif
