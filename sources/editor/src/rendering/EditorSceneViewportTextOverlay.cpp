#include "rendering/EditorSceneViewportTextOverlay.hpp"

#if defined(_WIN32)
#include "rendering/HeroIconGdiplusRuntime.hpp"

#pragma warning(push, 0)
#include <objidl.h>
#include <gdiplus.h>
#pragma warning(pop)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace kb::editor {
namespace {

constexpr wchar_t kTextOverlayWindowClass[] = L"21kbEditorSceneViewportTextOverlay";

[[nodiscard]] std::filesystem::path RobotoFontPath() {
    std::vector<wchar_t> modulePath(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0U || length >= modulePath.size()) return {};
    modulePath.resize(length);
    return std::filesystem::path{ modulePath.data() }.parent_path() /
        L"Content" / L"EditorShell" / L"Fonts" / L"Roboto-Regular.ttf";
}

[[nodiscard]] std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring output(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), output.data(), required) != required) return {};
    return output;
}

LRESULT CALLBACK TextOverlayWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

[[nodiscard]] bool EnsureWindowClass(HINSTANCE instance) {
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(instance, kTextOverlayWindowClass, &existing) != 0) return true;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &TextOverlayWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = kTextOverlayWindowClass;
    return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

// The overlay's font: Roboto from the editor's content, in the sizes asked for.
struct OverlayFonts {
    // Created once GDI+ is running: a collection made before that is unusable.
    std::unique_ptr<Gdiplus::PrivateFontCollection> fonts;
    std::unique_ptr<Gdiplus::FontFamily[]> fontFamilies;
    INT fontFamilyCount = 0;
    std::unordered_map<int, std::unique_ptr<Gdiplus::Font>> fontByPixelHeight;

    [[nodiscard]] bool EnsureFont() {
        if (fontFamilies != nullptr && fontFamilyCount > 0) return true;
        HeroIconGdiplusRuntime::EnsureStarted();
        fonts = std::make_unique<Gdiplus::PrivateFontCollection>();
        const std::filesystem::path path = RobotoFontPath();
        if (path.empty() || !std::filesystem::exists(path) ||
            fonts->AddFontFile(path.c_str()) != Gdiplus::Ok) return false;
        fontFamilyCount = fonts->GetFamilyCount();
        if (fontFamilyCount <= 0) return false;
        fontFamilies = std::make_unique<Gdiplus::FontFamily[]>(
            static_cast<std::size_t>(fontFamilyCount));
        INT found = 0;
        if (fonts->GetFamilies(fontFamilyCount, fontFamilies.get(), &found) != Gdiplus::Ok ||
            found <= 0) {
            fontFamilies.reset();
            fontFamilyCount = 0;
            return false;
        }
        fontFamilyCount = found;
        return fontFamilies[0].IsAvailable();
    }

    [[nodiscard]] Gdiplus::Font* FontFor(float pixelHeight) {
        if (!EnsureFont()) return nullptr;
        const int quantizedHeight = std::clamp(
            static_cast<int>(std::lround(pixelHeight)), 1, 256);
        if (const auto existing = fontByPixelHeight.find(quantizedHeight);
            existing != fontByPixelHeight.end()) return existing->second.get();
        auto font = std::make_unique<Gdiplus::Font>(
            &fontFamilies[0], static_cast<Gdiplus::REAL>(quantizedHeight),
            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        if (font->GetLastStatus() != Gdiplus::Ok) return nullptr;
        Gdiplus::Font* result = font.get();
        fontByPixelHeight.emplace(quantizedHeight, std::move(font));
        return result;
    }
};

[[nodiscard]] Gdiplus::Color ToColor(const std::array<std::uint8_t, 4U>& color) {
    return Gdiplus::Color(color[3], color[0], color[1], color[2]);
}

void DrawShape(Gdiplus::Graphics& graphics, const EditorSceneViewportOverlayShape& shape) {
    if (shape.points.size() < 2U) return;
    if (shape.kind == EditorSceneViewportOverlayShape::Kind::RoundedRect) {
        const float left = std::min(shape.points[0][0], shape.points[1][0]);
        const float top = std::min(shape.points[0][1], shape.points[1][1]);
        const float width = std::abs(shape.points[1][0] - shape.points[0][0]);
        const float height = std::abs(shape.points[1][1] - shape.points[0][1]);
        const float diameter = std::min({ shape.radius * 2.0F, width, height });
        Gdiplus::GraphicsPath path;
        path.AddArc(left, top, diameter, diameter, 180.0F, 90.0F);
        path.AddArc(left + width - diameter, top, diameter, diameter, 270.0F, 90.0F);
        path.AddArc(left + width - diameter, top + height - diameter, diameter, diameter, 0.0F, 90.0F);
        path.AddArc(left, top + height - diameter, diameter, diameter, 90.0F, 90.0F);
        path.CloseFigure();
        const Gdiplus::SolidBrush brush(ToColor(shape.color));
        graphics.FillPath(&brush, &path);
        return;
    }
    Gdiplus::Pen pen(ToColor(shape.color), shape.thickness);
    if (shape.dashed) pen.SetDashStyle(Gdiplus::DashStyleDash);
    std::vector<Gdiplus::PointF> points;
    points.reserve(shape.points.size());
    for (const auto& point : shape.points) points.emplace_back(point[0], point[1]);
    graphics.DrawLines(&pen, points.data(), static_cast<INT>(points.size()));
}

// Shapes first, so labels lie on top of them.
[[nodiscard]] bool DrawContent(Gdiplus::Graphics& graphics, OverlayFonts& fonts,
    std::span<const EditorSceneViewportTextLabel> labels, std::span<const EditorSceneViewportOverlayShape> shapes) {
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    for (const EditorSceneViewportOverlayShape& shape : shapes) DrawShape(graphics, shape);

    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip | Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::StringFormat rightFormat(&format);
    rightFormat.SetAlignment(Gdiplus::StringAlignmentFar);
    const Gdiplus::SolidBrush shadow(Gdiplus::Color(220U, 0U, 0U, 0U));
    // A right-aligned label is laid out in a box that ends at its x.
    constexpr float kRightBoxWidth = 4096.0F;
    for (const EditorSceneViewportTextLabel& label : labels) {
        if (!std::isfinite(label.x) || !std::isfinite(label.y) ||
            !std::isfinite(label.pixelHeight) || label.pixelHeight < 1.0F || label.text.empty()) continue;
        Gdiplus::Font* font = fonts.FontFor(label.pixelHeight);
        const std::wstring text = Utf8ToWide(label.text);
        if (font == nullptr || text.empty()) return false;
        const bool right = label.align == EditorSceneViewportTextAlign::Right;
        const auto draw = [&](float offset, const Gdiplus::Brush* brush) {
            if (right) {
                const Gdiplus::RectF box{ label.x + offset - kRightBoxWidth, label.y + offset, kRightBoxWidth, label.pixelHeight * 1.5F };
                graphics.DrawString(text.data(), static_cast<INT>(text.size()), font, box, &rightFormat, brush);
            } else {
                graphics.DrawString(text.data(), static_cast<INT>(text.size()), font,
                    Gdiplus::PointF{ label.x + offset, label.y + offset }, &format, brush);
            }
        };
        if (label.shadow) draw(1.0F, &shadow);
        const Gdiplus::SolidBrush foreground(ToColor(label.color));
        draw(0.0F, &foreground);
    }
    graphics.Flush(Gdiplus::FlushIntentionSync);
    return true;
}

} // namespace

struct EditorSceneViewportTextOverlay::Impl {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HDC memoryDc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    void* pixels = nullptr;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    bool hasContent = false;
    OverlayFonts fonts;

    void ReleaseBitmap() noexcept {
        if (memoryDc != nullptr && previousBitmap != nullptr) {
            SelectObject(memoryDc, previousBitmap);
        }
        previousBitmap = nullptr;
        if (bitmap != nullptr) DeleteObject(bitmap);
        bitmap = nullptr;
        pixels = nullptr;
        if (memoryDc != nullptr) DeleteDC(memoryDc);
        memoryDc = nullptr;
        width = 0U;
        height = 0U;
    }

    [[nodiscard]] bool EnsureBitmap(std::uint32_t requestedWidth, std::uint32_t requestedHeight) {
        if (memoryDc != nullptr && bitmap != nullptr &&
            width == requestedWidth && height == requestedHeight) return true;
        ReleaseBitmap();
        if (requestedWidth == 0U || requestedHeight == 0U) return false;

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = static_cast<LONG>(requestedWidth);
        info.bmiHeader.biHeight = -static_cast<LONG>(requestedHeight);
        info.bmiHeader.biPlanes = 1U;
        info.bmiHeader.biBitCount = 32U;
        info.bmiHeader.biCompression = BI_RGB;
        HDC screenDc = GetDC(nullptr);
        if (screenDc == nullptr) return false;
        memoryDc = CreateCompatibleDC(screenDc);
        bitmap = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0U);
        ReleaseDC(nullptr, screenDc);
        if (memoryDc == nullptr || bitmap == nullptr || pixels == nullptr) {
            ReleaseBitmap();
            return false;
        }
        previousBitmap = SelectObject(memoryDc, bitmap);
        width = requestedWidth;
        height = requestedHeight;
        return true;
    }
};

EditorSceneViewportTextOverlay::EditorSceneViewportTextOverlay()
    : impl_(std::make_unique<Impl>()) {}

EditorSceneViewportTextOverlay::~EditorSceneViewportTextOverlay() {
    Destroy();
}

bool EditorSceneViewportTextOverlay::Ensure(
    HINSTANCE instance, HWND parent, std::uint32_t width, std::uint32_t height) {
    if (instance == nullptr || parent == nullptr || width == 0U || height == 0U ||
        !EnsureWindowClass(instance) || !impl_->fonts.EnsureFont()) return false;
    impl_->instance = instance;
    POINT destination{ 0, 0 };
    if (ClientToScreen(parent, &destination) == 0) return false;
    const HWND owner = GetAncestor(parent, GA_ROOT);
    if (owner == nullptr) return false;
    if (impl_->window != nullptr && IsWindow(impl_->window) != 0 &&
        GetWindow(impl_->window, GW_OWNER) != owner) {
        DestroyWindow(impl_->window);
        impl_->window = nullptr;
        impl_->hasContent = false;
    }
    if (impl_->window == nullptr || IsWindow(impl_->window) == 0) {
        impl_->window = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            kTextOverlayWindowClass,
            L"",
            WS_POPUP,
            destination.x,
            destination.y,
            static_cast<int>(width),
            static_cast<int>(height),
            owner,
            nullptr,
            instance,
            nullptr);
        if (impl_->window == nullptr) return false;
    }
    // Placement only: the popup is above its owner's children from the moment it is shown, and
    // raising it again on every frame would put it above the editor's own popups, such as the
    // profiler's window.
    if (SetWindowPos(impl_->window, HWND_TOP, destination.x, destination.y,
            static_cast<int>(width),
            static_cast<int>(height), SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER) == 0) return false;
    return impl_->EnsureBitmap(width, height);
}

bool EditorSceneViewportTextOverlay::Update(
    std::span<const EditorSceneViewportTextLabel> labels,
    std::span<const EditorSceneViewportOverlayShape> shapes) {
    if (impl_->window == nullptr || impl_->memoryDc == nullptr || impl_->pixels == nullptr ||
        impl_->width == 0U || impl_->height == 0U) return false;
    std::memset(impl_->pixels, 0,
        static_cast<std::size_t>(impl_->width) * impl_->height * sizeof(std::uint32_t));

    Gdiplus::Bitmap bitmap(
        static_cast<INT>(impl_->width), static_cast<INT>(impl_->height),
        static_cast<INT>(impl_->width * sizeof(std::uint32_t)),
        PixelFormat32bppPARGB, static_cast<BYTE*>(impl_->pixels));
    Gdiplus::Graphics graphics(&bitmap);
    if (!DrawContent(graphics, impl_->fonts, labels, shapes)) return false;

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) return false;
    RECT windowRect{};
    if (GetWindowRect(impl_->window, &windowRect) == 0) {
        ReleaseDC(nullptr, screenDc);
        return false;
    }
    POINT destination{ windowRect.left, windowRect.top };
    POINT source{ 0, 0 };
    SIZE size{ static_cast<LONG>(impl_->width), static_cast<LONG>(impl_->height) };
    BLENDFUNCTION blend{ AC_SRC_OVER, 0U, 255U, AC_SRC_ALPHA };
    const BOOL updated = UpdateLayeredWindow(
        impl_->window, screenDc, &destination, &size, impl_->memoryDc, &source,
        0U, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screenDc);
    impl_->hasContent = updated != 0;
    return impl_->hasContent;
}

bool EditorSceneViewportTextOverlay::Paint(HDC dc, const RECT& area,
    std::span<const EditorSceneViewportTextLabel> labels, std::span<const EditorSceneViewportOverlayShape> shapes) {
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetClip(Gdiplus::Rect(area.left, area.top, area.right - area.left, area.bottom - area.top));
    graphics.TranslateTransform(static_cast<Gdiplus::REAL>(area.left), static_cast<Gdiplus::REAL>(area.top));
    return DrawContent(graphics, impl_->fonts, labels, shapes);
}

void EditorSceneViewportTextOverlay::Show() noexcept {
    if (!impl_->hasContent || impl_->window == nullptr || IsWindow(impl_->window) == 0) return;
    // Ensure() already re-positions and re-sizes this popup every frame, so the only thing left for
    // Show() is the hidden -> shown transition. Test the popup's own WS_VISIBLE bit rather than
    // IsWindowVisible(), which reports 0 whenever the owner window is hidden and would make this
    // re-issue a redundant SetWindowPos on every presented frame.
    if ((GetWindowLongPtrW(impl_->window, GWL_STYLE) & WS_VISIBLE) != 0) return;
    SetWindowPos(impl_->window, HWND_TOP, 0, 0, static_cast<int>(impl_->width),
        static_cast<int>(impl_->height),
        SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
}

void EditorSceneViewportTextOverlay::Hide() noexcept {
    impl_->hasContent = false;
    if (impl_->window != nullptr && IsWindow(impl_->window) != 0) {
        ShowWindow(impl_->window, SW_HIDE);
    }
}

void EditorSceneViewportTextOverlay::Destroy() noexcept {
    if (impl_ == nullptr) return;
    impl_->ReleaseBitmap();
    if (impl_->window != nullptr && IsWindow(impl_->window) != 0) {
        DestroyWindow(impl_->window);
    }
    impl_->window = nullptr;
    impl_->fonts.fontByPixelHeight.clear();
    impl_->fonts.fontFamilies.reset();
    impl_->fonts.fonts.reset();
    impl_->fonts.fontFamilyCount = 0;
    impl_->hasContent = false;
}

HWND EditorSceneViewportTextOverlay::Window() const noexcept {
    return impl_->window;
}

} // namespace kb::editor

#endif
