#include "EditorApplication.hpp"

#if defined(_WIN32)
#include <dwmapi.h>

namespace kb::editor {
namespace {

constexpr wchar_t kWindowClassName[] = L"KBEditorWindow";
constexpr wchar_t kWindowTitle[] = L"21kb Engine";
constexpr int kInitialWindowWidth = 1600;
constexpr int kInitialWindowHeight = 960;

constexpr COLORREF kBackground = RGB(15, 17, 21);
constexpr COLORREF kSurface = RGB(24, 27, 33);
constexpr COLORREF kSurfaceAlt = RGB(29, 33, 40);
constexpr COLORREF kBorder = RGB(58, 64, 74);
constexpr COLORREF kText = RGB(232, 236, 242);
constexpr COLORREF kMutedText = RGB(142, 151, 165);
constexpr COLORREF kAccent = RGB(41, 174, 199);

void FillRectColor(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

RECT Inset(RECT rect, int amount) {
    rect.left += amount;
    rect.top += amount;
    rect.right -= amount;
    rect.bottom -= amount;
    return rect;
}

} // namespace

EditorApplication::~EditorApplication() {
    Shutdown();
}

bool EditorApplication::Initialize() {
    instance_ = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &EditorApplication::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClassName;

    if (RegisterClassExW(&windowClass) == 0) {
        return false;
    }

    DWORD windowStyle = WS_OVERLAPPEDWINDOW;
    RECT windowRect{ 0, 0, kInitialWindowWidth, kInitialWindowHeight };
    AdjustWindowRect(&windowRect, windowStyle, FALSE);

    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        windowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance_,
        this);

    if (window_ == nullptr) {
        return false;
    }

    const BOOL darkMode = TRUE;
    DwmSetWindowAttribute(window_, 20, &darkMode, sizeof(darkMode));

    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);

    running_ = true;
    return true;
}

void EditorApplication::Run() {
    MSG message{};
    while (running_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void EditorApplication::Shutdown() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    if (instance_ != nullptr) {
        UnregisterClassW(kWindowClassName, instance_);
        instance_ = nullptr;
    }
}

LRESULT CALLBACK EditorApplication::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    EditorApplication* app = nullptr;

    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = static_cast<EditorApplication*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<EditorApplication*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    if (app != nullptr) {
        return app->HandleWindowMessage(message, wparam, lparam);
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT EditorApplication::HandleWindowMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_SIZE:
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        running_ = false;
        DestroyWindow(window_);
        window_ = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window_, message, wparam, lparam);
    }
}

void EditorApplication::Paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window_, &paint);

    RECT client{};
    GetClientRect(window_, &client);
    FillRectColor(dc, client, kBackground);

    SetBkMode(dc, TRANSPARENT);

    HFONT titleFont = CreateFontW(
        -16,
        0,
        0,
        0,
        FW_SEMIBOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    HFONT bodyFont = CreateFontW(
        -14,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, bodyFont));

    RECT menu{ client.left, client.top, client.right, client.top + 40 };
    FillRectColor(dc, menu, RGB(18, 20, 25));
    DrawTextLine(dc, { 20, 10, 220, 34 }, "21kb Engine", kText);
    DrawTextLine(dc, { 140, 10, 420, 34 }, "Native editor shell", kMutedText);

    RECT toolbar{ client.left, menu.bottom, client.right, menu.bottom + 48 };
    FillRectColor(dc, toolbar, RGB(20, 23, 28));
    DrawTextLine(dc, { 20, 54, 120, 82 }, "Select", kText);
    DrawTextLine(dc, { 110, 54, 190, 82 }, "Move", kMutedText);
    DrawTextLine(dc, { 190, 54, 280, 82 }, "Rotate", kMutedText);
    DrawTextLine(dc, { 285, 54, 370, 82 }, "Scale", kMutedText);

    const int width = client.right - client.left;
    const int height = client.bottom - toolbar.bottom;
    const int leftWidth = max(260, width / 6);
    const int rightWidth = max(320, width / 5);
    const int bottomHeight = max(220, height / 4);
    const int gap = 8;
    const int top = toolbar.bottom + gap;

    RECT hierarchy{ gap, top, leftWidth, client.bottom - bottomHeight - gap };
    RECT scene{ hierarchy.right + gap, top, client.right - rightWidth - gap, client.bottom - bottomHeight - gap };
    RECT inspector{ scene.right + gap, top, client.right - gap, scene.bottom };
    RECT assets{ gap, scene.bottom + gap, width / 2 - gap, client.bottom - gap };
    RECT console{ assets.right + gap, scene.bottom + gap, client.right - gap, client.bottom - gap };

    SelectObject(dc, titleFont);
    DrawPanel(dc, hierarchy, "Hierarchy", kSurface);
    DrawPanel(dc, scene, "Scene", RGB(18, 22, 28));
    DrawPanel(dc, inspector, "Inspector", kSurface);
    DrawPanel(dc, assets, "Assets", kSurfaceAlt);
    DrawPanel(dc, console, "Console", kSurfaceAlt);

    SelectObject(dc, bodyFont);
    DrawTextLine(dc, Inset(hierarchy, 18), "Camera\nDirectional Light\nPlayer\nCanvas Root", kMutedText);
    DrawTextLine(dc, Inset(inspector, 18), "Entity: Camera\nTransform\nCamera\nRender Layer", kMutedText);
    DrawTextLine(dc, Inset(assets, 18), "Scenes\nMaterials\nMeshes\nTextures", kMutedText);
    DrawTextLine(dc, Inset(console, 18), "[info] Native window initialized\n[todo] Custom renderer-backed UI", kMutedText);

    RECT sceneInner = Inset(scene, 20);
    sceneInner.top += 36;
    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(43, 48, 56));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, gridPen));

    for (int x = sceneInner.left; x < sceneInner.right; x += 32) {
        MoveToEx(dc, x, sceneInner.top, nullptr);
        LineTo(dc, x, sceneInner.bottom);
    }
    for (int y = sceneInner.top; y < sceneInner.bottom; y += 32) {
        MoveToEx(dc, sceneInner.left, y, nullptr);
        LineTo(dc, sceneInner.right, y);
    }

    HPEN accentPen = CreatePen(PS_SOLID, 2, kAccent);
    SelectObject(dc, accentPen);
    const int centerX = (sceneInner.left + sceneInner.right) / 2;
    const int centerY = (sceneInner.top + sceneInner.bottom) / 2;
    Ellipse(dc, centerX - 48, centerY - 48, centerX + 48, centerY + 48);

    SelectObject(dc, oldPen);
    DeleteObject(accentPen);
    DeleteObject(gridPen);

    SelectObject(dc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);

    EndPaint(window_, &paint);
}

void EditorApplication::DrawPanel(HDC dc, const RECT& rect, const char* title, COLORREF fill) const {
    FillRectColor(dc, rect, fill);

    HPEN borderPen = CreatePen(PS_SOLID, 1, kBorder);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, borderPen));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(borderPen);

    RECT titleRect{ rect.left + 14, rect.top + 10, rect.right - 14, rect.top + 34 };
    DrawTextLine(dc, titleRect, title, kText);
}

void EditorApplication::DrawTextLine(HDC dc, const RECT& rect, const char* text, COLORREF color) const {
    RECT drawRect = rect;
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &drawRect, DT_LEFT | DT_TOP | DT_NOPREFIX);
}

} // namespace kb::editor

#else

namespace kb::editor {

EditorApplication::~EditorApplication() = default;

bool EditorApplication::Initialize() {
    return false;
}

void EditorApplication::Run() {
}

void EditorApplication::Shutdown() {
}

} // namespace kb::editor

#endif
