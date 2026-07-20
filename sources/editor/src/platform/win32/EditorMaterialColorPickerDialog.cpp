#include "platform/win32/EditorMaterialColorPickerDialog.hpp"

#if defined(_WIN32)
#include "platform/win32/EditorModalWindowScope.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "kb/render/resources/RenderMaterialNumericParsing.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kColorPickerClassName[] = L"KBEditorMaterialColorPickerDialog";
constexpr int kDialogWidth = 324;
constexpr int kDialogHeight = 420;
constexpr int kPad = 22;
constexpr int kTitleHeight = 18;
constexpr int kSquare = 216;
constexpr int kHueWidth = 22;
constexpr int kGap = 10;
constexpr int kFieldHeight = 22;
constexpr int kButtonWidth = 64;
constexpr int kButtonHeight = 26;
constexpr int kPreviewWidth = 44;

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
        "%02X%02X%02X",
        ColorByte(rgba[0]),
        ColorByte(rgba[1]),
        ColorByte(rgba[2]));
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
    std::vector<float> values;
    return kb::render::ParseFiniteMaterialFloatSequence(text, values, 1U, 1U, false)
        ? std::optional<float>{ values.front() }
        : std::nullopt;
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

void DrawBorderOnly(HDC dc, const RECT& rect, COLORREF border) {
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    if (pen == nullptr) {
        return;
    }
    {
        const ScopedGdiObject selectedPen(dc, pen);
        const ScopedGdiObject selectedBrush(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    }
    DeleteObject(pen);
}

[[nodiscard]] std::string ColorPickerLogPath() {
    std::array<char, MAX_PATH> buffer{};
    const DWORD length = GetTempPathA(static_cast<DWORD>(buffer.size()), buffer.data());
    std::string path = (length > 0U && length < buffer.size()) ? std::string(buffer.data(), length) : std::string(".\\");
    if (!path.empty() && path.back() != '\\' && path.back() != '/') {
        path.push_back('\\');
    }
    path += "kb_material_color_picker.log";
    return path;
}

void ColorPickerLogRaw(std::string_view message, std::ios_base::openmode mode = std::ios::app) {
    static const std::string path = ColorPickerLogPath();
    {
        std::ofstream log(path, mode);
        if (log.is_open()) {
            log << message << '\n';
        }
    }
    const std::string debug = "[MaterialColorPicker] " + std::string(message) + "\n";
    OutputDebugStringA(debug.c_str());
}

void ColorPickerLog(std::string_view message) {
    ColorPickerLogRaw(message);
}

void ColorPickerLogReset() {
    ColorPickerLogRaw("=== Material color picker log begin ===", std::ios::trunc);
    ColorPickerLog("path=" + ColorPickerLogPath());
}

[[nodiscard]] bool ShouldLog(int& counter, int first, int every) noexcept {
    ++counter;
    return counter <= first || (every > 0 && (counter % every) == 0);
}

[[nodiscard]] std::string RectText(const RECT& rect) {
    std::ostringstream stream;
    stream << "[" << rect.left << "," << rect.top << " - " << rect.right << "," << rect.bottom
           << " size=" << RectWidth(rect) << "x" << RectHeight(rect) << "]";
    return stream.str();
}

[[nodiscard]] std::string PixelText(std::uint32_t pixel) {
    const int a = static_cast<int>((pixel >> 24U) & 0xFFU);
    const int r = static_cast<int>((pixel >> 16U) & 0xFFU);
    const int g = static_cast<int>((pixel >> 8U) & 0xFFU);
    const int b = static_cast<int>(pixel & 0xFFU);
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << pixel
           << std::dec << " rgba=(" << r << "," << g << "," << b << "," << a << ")";
    return stream.str();
}

[[nodiscard]] std::string ColorRefText(COLORREF color) {
    std::ostringstream stream;
    stream << "rgb=(" << static_cast<int>(GetRValue(color)) << "," << static_cast<int>(GetGValue(color)) << ","
           << static_cast<int>(GetBValue(color)) << ")";
    return stream.str();
}

[[nodiscard]] std::string RgbaText(const std::array<float, 4U>& rgba) {
    std::ostringstream stream;
    stream << "rgba=(" << ColorByte(rgba[0]) << "," << ColorByte(rgba[1]) << "," << ColorByte(rgba[2]) << ","
           << ColorByte(rgba[3]) << ") floats=(" << rgba[0] << "," << rgba[1] << "," << rgba[2] << "," << rgba[3]
           << ")";
    return stream.str();
}

[[nodiscard]] std::string HsvText(HsvColor hsv) {
    std::ostringstream stream;
    stream << "hsv=(" << hsv.h << "," << hsv.s << "," << hsv.v << ")";
    return stream.str();
}

[[nodiscard]] std::uint32_t BgraPixel(int r, int g, int b) noexcept {
    return 0xFF000000U | (static_cast<std::uint32_t>(std::clamp(r, 0, 255)) << 16U) |
        (static_cast<std::uint32_t>(std::clamp(g, 0, 255)) << 8U) |
        static_cast<std::uint32_t>(std::clamp(b, 0, 255));
}

[[nodiscard]] std::uint32_t BlendBgra(COLORREF foreground, BYTE alpha, COLORREF background) noexcept {
    const int invAlpha = 255 - static_cast<int>(alpha);
    const int r = ((static_cast<int>(GetRValue(foreground)) * static_cast<int>(alpha)) +
                      (static_cast<int>(GetRValue(background)) * invAlpha) + 127) /
        255;
    const int g = ((static_cast<int>(GetGValue(foreground)) * static_cast<int>(alpha)) +
                      (static_cast<int>(GetGValue(background)) * invAlpha) + 127) /
        255;
    const int b = ((static_cast<int>(GetBValue(foreground)) * static_cast<int>(alpha)) +
                      (static_cast<int>(GetBValue(background)) * invAlpha) + 127) /
        255;
    return BgraPixel(r, g, b);
}

void DrawBgraPixels(HDC dc, const RECT& rect, const std::vector<std::uint32_t>& pixels) {
    const int width = RectWidth(rect);
    const int height = RectHeight(rect);
    if (width <= 0 || height <= 0 || pixels.size() < static_cast<std::size_t>(width * height)) {
        ColorPickerLog("DrawBgraPixels skip rect=" + RectText(rect) + " pixels=" + IntText(static_cast<int>(pixels.size())));
        return;
    }
    static int drawCounter = 0;
    const bool logDraw = ShouldLog(drawCounter, 18, 120);
    if (logDraw) {
        const std::size_t center = static_cast<std::size_t>((height / 2) * width + (width / 2));
        ColorPickerLog(
            "DrawBgraPixels #" + IntText(drawCounter) + " rect=" + RectText(rect) +
            " first=" + PixelText(pixels.front()) + " center=" + PixelText(pixels[center]) +
            " last=" + PixelText(pixels[static_cast<std::size_t>(width * height - 1)]));
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    SetLastError(0U);
    const int oldMode = SetStretchBltMode(dc, COLORONCOLOR);
    const int lines = StretchDIBits(
        dc,
        rect.left,
        rect.top,
        width,
        height,
        0,
        0,
        width,
        height,
        pixels.data(),
        &info,
        DIB_RGB_COLORS,
        SRCCOPY);
    SetStretchBltMode(dc, oldMode);
    if (logDraw) {
        ColorPickerLog(
            "DrawBgraPixels StretchDIBits lines=" + IntText(lines) +
            " error=" + IntText(static_cast<int>(GetLastError())));
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
        ColorPickerLogReset();
        ColorPickerLog("ctor " + RgbaText(rgba_) + " " + HsvText(hsv_));
    }

    [[nodiscard]] std::optional<std::array<float, 4U>> Show(HWND owner) {
        owner_ = owner;
        if (!EnsureWindow()) {
            return std::nullopt;
        }
        const RECT bounds = CenteredWindowRect(owner);
        ColorPickerLog("show owner=" + IntText(owner_ != nullptr ? 1 : 0) + " bounds=" + RectText(bounds));
        {
            const EditorModalWindowScope modal{ window_ };
            SetWindowPos(window_, HWND_TOPMOST, bounds.left, bounds.top, RectWidth(bounds), RectHeight(bounds), SWP_SHOWWINDOW);
            SetForegroundWindow(window_);
            InvalidateRect(window_, nullptr, FALSE);
            UpdateWindow(window_);

            MSG message{};
            while (running_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

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
            ColorPickerLog("EnsureWindow RegisterClassEx failed error=" + IntText(static_cast<int>(GetLastError())));
            return false;
        }
        SetLastError(0U);
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
        ColorPickerLog(
            "EnsureWindow CreateWindowEx window=" + IntText(window_ != nullptr ? 1 : 0) +
            " error=" + IntText(static_cast<int>(GetLastError())));
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
        const int top = kPad + kTitleHeight + kGap;
        return Rect(kPad, top, kPad + kSquare, top + kSquare);
    }

    [[nodiscard]] RECT HueRect() const noexcept {
        const RECT sv = SaturationValueRect();
        return Rect(sv.right + kGap, sv.top, sv.right + kGap + kHueWidth, sv.bottom);
    }

    [[nodiscard]] RECT AlphaRect() const noexcept {
        const RECT hue = HueRect();
        return Rect(hue.right + kGap, hue.top, hue.right + kGap + kHueWidth, hue.bottom);
    }

    [[nodiscard]] RECT TitleRect() const noexcept {
        return Rect(kPad, kPad, kDialogWidth - kPad, kPad + kTitleHeight);
    }

    [[nodiscard]] RECT FieldsBounds() const noexcept {
        const RECT sv = SaturationValueRect();
        return Rect(kPad, sv.bottom + kGap, kDialogWidth - kPad, sv.bottom + kGap + (3 * kFieldHeight) + (2 * kGap));
    }

    [[nodiscard]] RECT HexLabelRect() const noexcept {
        const RECT fields = FieldsBounds();
        return Rect(fields.left, fields.top, fields.left + 30, fields.top + kFieldHeight);
    }

    [[nodiscard]] RECT HexFieldRect() const noexcept {
        const RECT fields = FieldsBounds();
        return Rect(fields.left + 38, fields.top, fields.right, fields.top + kFieldHeight);
    }

    [[nodiscard]] RECT RgbFieldRect(std::size_t channel) const noexcept {
        const RECT fields = FieldsBounds();
        const int top = fields.top + kFieldHeight + kGap;
        const int gap = 6;
        const int width = (RectWidth(fields) - (2 * gap)) / 3;
        const int left = fields.left + static_cast<int>(channel) * (width + gap);
        const int right = channel == 2U ? fields.right : left + width;
        return Rect(left, top, right, top + kFieldHeight);
    }

    [[nodiscard]] RECT AlphaFieldRect() const noexcept {
        const RECT fields = FieldsBounds();
        const int top = fields.top + (2 * (kFieldHeight + kGap));
        return Rect(fields.left, top, fields.right, top + kFieldHeight);
    }

    [[nodiscard]] RECT PreviewRect() const noexcept {
        const RECT alpha = AlphaFieldRect();
        const int top = alpha.bottom + kGap;
        return Rect(kPad, top, kPad + kPreviewWidth, top + kButtonHeight);
    }

    [[nodiscard]] RECT FieldRect(PickerField field) const noexcept {
        switch (field) {
        case PickerField::Hex: return HexFieldRect();
        case PickerField::Red: return RgbFieldRect(0U);
        case PickerField::Green: return RgbFieldRect(1U);
        case PickerField::Blue: return RgbFieldRect(2U);
        case PickerField::Alpha: return AlphaFieldRect();
        default: return {};
        }
    }

    [[nodiscard]] RECT FieldLabelRect(PickerField field) const noexcept {
        if (field == PickerField::Hex) {
            return HexLabelRect();
        }
        return {};
    }

    [[nodiscard]] RECT OkButtonRect() const noexcept {
        const RECT preview = PreviewRect();
        return Rect(kDialogWidth - kPad - kButtonWidth, preview.top, kDialogWidth - kPad, preview.bottom);
    }

    [[nodiscard]] RECT CancelButtonRect() const noexcept {
        const RECT ok = OkButtonRect();
        return Rect(ok.left - 10 - kButtonWidth, ok.top, ok.left - 10, ok.bottom);
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
        fieldTexts_[7] = IntText(ColorByte(rgba_[3]));
    }

    void SetRgba(std::array<float, 4U> color) {
        rgba_ = color;
        ClampColor();
        hsv_ = RgbToHsv(rgba_);
        RefreshFields();
        if (ShouldLog(colorChangeLogCount_, 16, 60)) {
            ColorPickerLog("SetRgba #" + IntText(colorChangeLogCount_) + " " + RgbaText(rgba_) + " " + HsvText(hsv_));
        }
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
        if (ShouldLog(colorChangeLogCount_, 16, 60)) {
            ColorPickerLog("SetHsv #" + IntText(colorChangeLogCount_) + " " + RgbaText(rgba_) + " " + HsvText(hsv_));
        }
        Invalidate();
    }

    void Invalidate() const noexcept {
        if (window_ != nullptr) {
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    void Paint(HDC dc) const {
        const RECT client = Client();
        const int width = RectWidth(client);
        const int height = RectHeight(client);
        const bool logPaint = ShouldLog(paintLogCount_, 12, 120);
        if (logPaint) {
            ColorPickerLog("Paint #" + IntText(paintLogCount_) + " client=" + RectText(client) + " " + RgbaText(rgba_) + " " + HsvText(hsv_));
        }
        if (width <= 0 || height <= 0) {
            return;
        }

        SetLastError(0U);
        HDC memoryDc = CreateCompatibleDC(dc);
        if (memoryDc == nullptr) {
            if (logPaint) {
                ColorPickerLog("Paint CreateCompatibleDC failed error=" + IntText(static_cast<int>(GetLastError())));
            }
            PaintContent(dc);
            return;
        }

        SetLastError(0U);
        HBITMAP memoryBitmap = CreateCompatibleBitmap(dc, width, height);
        if (memoryBitmap == nullptr) {
            if (logPaint) {
                ColorPickerLog("Paint CreateCompatibleBitmap failed error=" + IntText(static_cast<int>(GetLastError())));
            }
            DeleteDC(memoryDc);
            PaintContent(dc);
            return;
        }

        HGDIOBJ oldBitmap = SelectObject(memoryDc, memoryBitmap);
        if (logPaint) {
            ColorPickerLog(
                "Paint double-buffer memoryDc=" + IntText(memoryDc != nullptr ? 1 : 0) +
                " bitmap=" + IntText(memoryBitmap != nullptr ? 1 : 0) +
                " oldBitmap=" + IntText(oldBitmap != nullptr ? 1 : 0));
        }
        PaintContent(memoryDc);
        SetLastError(0U);
        const BOOL blitOk = BitBlt(dc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);
        if (logPaint) {
            ColorPickerLog(
                "Paint final BitBlt ok=" + IntText(blitOk != FALSE ? 1 : 0) +
                " error=" + IntText(static_cast<int>(GetLastError())));
        }
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(memoryBitmap);
        DeleteDC(memoryDc);
    }

    void PaintContent(HDC dc) const {
        const RECT client = Client();
        GdiDrawing::FillRectColor(dc, client, Rgb(20, 23, 29));
        GdiDrawing::DrawSharpFrame(dc, client, Rgb(20, 23, 29), Rgb(50, 58, 70));
        {
            ScopedFont titleFont(12, FW_SEMIBOLD);
            const ScopedGdiObject selectedFont(dc, titleFont.handle);
            Text(dc, TitleRect(), "Color", Rgb(245, 248, 252));
        }

        DrawSaturationValue(dc);
        DrawHue(dc);
        DrawAlpha(dc);
        DrawFields(dc);
        DrawPreview(dc);
        DrawButton(dc, CancelButtonRect(), "Cancel", false);
        DrawButton(dc, OkButtonRect(), "Apply", true);
    }

    void DrawSaturationValue(HDC dc) const {
        const RECT rect = SaturationValueRect();
        const int width = std::max(1, RectWidth(rect));
        const int height = std::max(1, RectHeight(rect));
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height));
        for (int y = 0; y < height; ++y) {
            const float value = 1.0F - (static_cast<float>(y) / static_cast<float>(std::max(1, height - 1)));
            for (int x = 0; x < width; ++x) {
                const float saturation = static_cast<float>(x) / static_cast<float>(std::max(1, width - 1));
                const std::array<float, 3U> rgb = HsvToRgb(HsvColor{ .h = hsv_.h, .s = saturation, .v = value });
                pixels[static_cast<std::size_t>(y * width + x)] = BgraPixel(ColorByte(rgb[0]), ColorByte(rgb[1]), ColorByte(rgb[2]));
            }
        }
        if (ShouldLog(saturationValueLogCount_, 10, 120)) {
            const std::size_t center = static_cast<std::size_t>((height / 2) * width + (width / 2));
            ColorPickerLog(
                "DrawSaturationValue #" + IntText(saturationValueLogCount_) + " rect=" + RectText(rect) + " " + HsvText(hsv_) +
                " tl=" + PixelText(pixels.front()) + " center=" + PixelText(pixels[center]) +
                " tr=" + PixelText(pixels[static_cast<std::size_t>(width - 1)]) +
                " bl=" + PixelText(pixels[static_cast<std::size_t>((height - 1) * width)]));
        }
        DrawBgraPixels(dc, rect, pixels);
        DrawBorderOnly(dc, rect, Rgb(83, 96, 116));
        const int markerX = rect.left + static_cast<int>(std::round(hsv_.s * static_cast<float>(width - 1)));
        const int markerY = rect.top + static_cast<int>(std::round((1.0F - hsv_.v) * static_cast<float>(height - 1)));
        DrawMarker(dc, markerX, markerY);
    }

    void DrawHue(HDC dc) const {
        const RECT rect = HueRect();
        const int width = std::max(1, RectWidth(rect));
        const int height = std::max(1, RectHeight(rect));
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height));
        for (int y = 0; y < height; ++y) {
            const float hue = (static_cast<float>(y) / static_cast<float>(std::max(1, height - 1))) * 360.0F;
            const std::array<float, 3U> rgb = HsvToRgb(HsvColor{ .h = hue, .s = 1.0F, .v = 1.0F });
            const std::uint32_t pixel = BgraPixel(ColorByte(rgb[0]), ColorByte(rgb[1]), ColorByte(rgb[2]));
            for (int x = 0; x < width; ++x) {
                pixels[static_cast<std::size_t>(y * width + x)] = pixel;
            }
        }
        if (ShouldLog(hueLogCount_, 10, 120)) {
            const std::size_t center = static_cast<std::size_t>((height / 2) * width + (width / 2));
            ColorPickerLog(
                "DrawHue #" + IntText(hueLogCount_) + " rect=" + RectText(rect) + " " + HsvText(hsv_) +
                " top=" + PixelText(pixels.front()) + " center=" + PixelText(pixels[center]) +
                " bottom=" + PixelText(pixels[static_cast<std::size_t>((height - 1) * width)]));
        }
        DrawBgraPixels(dc, rect, pixels);
        DrawBorderOnly(dc, rect, Rgb(83, 96, 116));
        const int markerY = rect.top + static_cast<int>(std::round((hsv_.h / 360.0F) * static_cast<float>(height - 1)));
        GdiDrawing::FillRectColor(dc, Rect(rect.left - 3, markerY - 2, rect.right + 3, markerY + 3), Rgb(245, 248, 252));
    }

    void DrawAlpha(HDC dc) const {
        const RECT rect = AlphaRect();
        const int width = std::max(1, RectWidth(rect));
        const int height = std::max(1, RectHeight(rect));
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height));
        const COLORREF color = RgbaColor(rgba_);
        constexpr int kAlphaCheckerCell = 7;
        for (int y = 0; y < height; ++y) {
            const BYTE alpha = static_cast<BYTE>(std::clamp((y * 255) / std::max(1, height - 1), 0, 255));
            for (int x = 0; x < width; ++x) {
                const bool light = ((x / kAlphaCheckerCell) + (y / kAlphaCheckerCell)) % 2 == 0;
                const COLORREF background = light ? Rgb(78, 83, 90) : Rgb(42, 46, 52);
                pixels[static_cast<std::size_t>(y * width + x)] = BlendBgra(color, alpha, background);
            }
        }
        if (ShouldLog(alphaLogCount_, 10, 120)) {
            const std::size_t center = static_cast<std::size_t>((height / 2) * width + (width / 2));
            ColorPickerLog(
                "DrawAlpha #" + IntText(alphaLogCount_) + " rect=" + RectText(rect) + " color=" + ColorRefText(color) +
                " alphaByte=" + IntText(ColorByte(rgba_[3])) + " top=" + PixelText(pixels.front()) +
                " center=" + PixelText(pixels[center]) +
                " bottom=" + PixelText(pixels[static_cast<std::size_t>((height - 1) * width)]));
        }
        DrawBgraPixels(dc, rect, pixels);
        DrawBorderOnly(dc, rect, Rgb(83, 96, 116));
        const int markerY = rect.top + static_cast<int>(std::round(rgba_[3] * static_cast<float>(height - 1)));
        GdiDrawing::FillRectColor(dc, Rect(rect.left - 3, markerY - 2, rect.right + 3, markerY + 3), Rgb(245, 248, 252));
    }

    void DrawPreview(HDC dc) const {
        const RECT rect = PreviewRect();
        const COLORREF color = RgbaColor(rgba_);
        if (ShouldLog(previewLogCount_, 10, 120)) {
            ColorPickerLog("DrawPreview #" + IntText(previewLogCount_) + " rect=" + RectText(rect) + " " + RgbaText(rgba_) + " color=" + ColorRefText(color));
        }
        std::vector<std::uint32_t> pixels(
            static_cast<std::size_t>(std::max(1, RectWidth(rect)) * std::max(1, RectHeight(rect))),
            BgraPixel(GetRValue(color), GetGValue(color), GetBValue(color)));
        DrawBgraPixels(dc, rect, pixels);
        DrawBorderOnly(dc, rect, Rgb(83, 96, 116));
    }

    void DrawFields(HDC dc) const {
        Text(dc, HexLabelRect(), "Hex", Rgb(190, 205, 224), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawField(dc, PickerField::Hex, FieldText(PickerField::Hex));
        DrawField(dc, PickerField::Red, "R " + FieldText(PickerField::Red));
        DrawField(dc, PickerField::Green, "G " + FieldText(PickerField::Green));
        DrawField(dc, PickerField::Blue, "B " + FieldText(PickerField::Blue));
        DrawField(dc, PickerField::Alpha, "A " + FieldText(PickerField::Alpha));
    }

    void DrawButton(HDC dc, RECT rect, std::string_view label, bool emphasized) const {
        GdiDrawing::DrawSharpFrame(dc, rect, emphasized ? Rgb(245, 194, 50) : Rgb(17, 20, 27), emphasized ? Rgb(248, 211, 81) : Rgb(55, 65, 82));
        Text(dc, rect, label, emphasized ? Rgb(22, 23, 26) : Rgb(235, 241, 249), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void DrawField(HDC dc, PickerField field, std::string text) const {
        const RECT rect = FieldRect(field);
        const bool focused = focusedField_ == field;
        GdiDrawing::DrawSharpFrame(dc, rect, focused ? Rgb(25, 34, 48) : Rgb(17, 20, 27), focused ? Rgb(111, 171, 235) : Rgb(55, 65, 82));
        if (focused) {
            text = FieldText(field) + "|";
        }
        Text(dc, Rect(rect.left + 7, rect.top, rect.right - 7, rect.bottom), text, Rgb(236, 242, 250));
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

    void UpdateFromAlpha(int y) {
        const RECT rect = AlphaRect();
        const int clampedY = std::clamp<int>(y, static_cast<int>(rect.top), static_cast<int>(rect.bottom - 1));
        rgba_[3] = static_cast<float>(clampedY - rect.top) / static_cast<float>(std::max(1, RectHeight(rect) - 1));
        RefreshFields();
        if (ShouldLog(colorChangeLogCount_, 16, 60)) {
            ColorPickerLog("UpdateFromAlpha #" + IntText(colorChangeLogCount_) + " y=" + IntText(y) + " " + RgbaText(rgba_));
        }
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
                rgba_[3] = std::clamp(*parsed, 0.0F, 255.0F) / 255.0F;
                RefreshFields();
                Invalidate();
            }
            break;
        case PickerField::None:
            break;
        }
    }

    [[nodiscard]] PickerField FieldAt(int x, int y) const noexcept {
        static constexpr std::array<PickerField, 5U> fields{
            PickerField::Hex,
            PickerField::Red,
            PickerField::Green,
            PickerField::Blue,
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

    void OnMouseDown(int x, int y) {
        ApplyFocusedField();
        if (Contains(OkButtonRect(), x, y)) {
            Accept();
            return;
        }
        if (Contains(CancelButtonRect(), x, y)) {
            Cancel();
            return;
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
            UpdateFromAlpha(y);
        }
    }

    void OnMouseMove(int x, int y) {
        if (ShouldLog(mouseMoveLogCount_, 16, 120)) {
            ColorPickerLog(
                "WM_MOUSEMOVE #" + IntText(mouseMoveLogCount_) + " x=" + IntText(x) + " y=" + IntText(y) +
                " dragging=" + IntText(dragKind_ != DragKind::None ? 1 : 0) +
                " inSV=" + IntText(Contains(SaturationValueRect(), x, y) ? 1 : 0) +
                " inHue=" + IntText(Contains(HueRect(), x, y) ? 1 : 0) +
                " inAlpha=" + IntText(Contains(AlphaRect(), x, y) ? 1 : 0));
        }
        if (dragKind_ == DragKind::SaturationValue) {
            UpdateFromSaturationValue(x, y);
        } else if (dragKind_ == DragKind::Hue) {
            UpdateFromHue(y);
        } else if (dragKind_ == DragKind::Alpha) {
            UpdateFromAlpha(y);
        }
    }

    [[nodiscard]] bool IsDragging() const noexcept {
        return dragKind_ != DragKind::None;
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
            Cancel();
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
            static constexpr std::array<PickerField, 5U> fields{
                PickerField::Hex,
                PickerField::Red,
                PickerField::Green,
                PickerField::Blue,
                PickerField::Alpha,
            };
            const auto current = std::find(fields.begin(), fields.end(), focusedField_);
            const std::size_t index = current == fields.end()
                ? 0U
                : (static_cast<std::size_t>(std::distance(fields.begin(), current)) + 1U) % fields.size();
            focusedField_ = fields[index];
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
                if (picker->IsDragging()) {
                    return 0;
                }
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
    mutable int paintLogCount_ = 0;
    mutable int saturationValueLogCount_ = 0;
    mutable int hueLogCount_ = 0;
    mutable int alphaLogCount_ = 0;
    mutable int previewLogCount_ = 0;
    int colorChangeLogCount_ = 0;
    int mouseMoveLogCount_ = 0;
    bool running_ = true;
    bool accepted_ = false;
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
