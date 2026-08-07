#include "platform/win32/EditorAssetImportDialog.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetImportCatalog.hpp"
#include "platform/win32/EditorModalMessageLoop.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"

#include <CommDlg.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kMeshImportOptionsClass[] = L"KBEditorMeshImportOptions";
constexpr int kDialogWidth = 640;
constexpr int kDialogHeight = 560;
constexpr int kHeaderHeight = 62;
constexpr int kFooterHeight = 62;
constexpr int kPadding = 18;
constexpr int kRowHeight = 44;

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept { return RECT{ left, top, right, bottom }; }
[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept { return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom; }
[[nodiscard]] int Height(const RECT& rect) noexcept { return std::max(0, static_cast<int>(rect.bottom - rect.top)); }
[[nodiscard]] COLORREF Rgb(int red, int green, int blue) noexcept { return RGB(red, green, blue); }

void Text(HDC dc, RECT rect, const char* value, COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, value, -1, &rect, format | DT_NOPREFIX);
}

void DrawCheckMark(HDC dc, const RECT& rect) {
    HPEN pen = CreatePen(PS_SOLID, 2, Rgb(239, 255, 248));
    if (pen == nullptr) return;
    const HGDIOBJ previousPen = SelectObject(dc, pen);
    const int previousBackgroundMode = SetBkMode(dc, TRANSPARENT);
    const POINT points[]{
        POINT{ rect.left + 3, rect.top + 9 },
        POINT{ rect.left + 7, rect.bottom - 4 },
        POINT{ rect.right - 3, rect.top + 4 },
    };
    Polyline(dc, points, 3);
    SetBkMode(dc, previousBackgroundMode);
    SelectObject(dc, previousPen);
    DeleteObject(pen);
}

[[nodiscard]] std::wstring WidenAsciiFilter(const std::string& filter) {
    std::wstring output;
    output.reserve(filter.size());
    for (const char character : filter) output.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    return output;
}

[[nodiscard]] std::vector<std::filesystem::path> ParseOpenFileNameBuffer(const std::array<wchar_t, 65536>& buffer) {
    std::vector<std::filesystem::path> paths;
    const wchar_t* cursor = buffer.data();
    if (*cursor == L'\0') return paths;
    const std::wstring first{ cursor };
    cursor += first.size() + 1U;
    if (*cursor == L'\0') { paths.emplace_back(first); return paths; }
    const std::filesystem::path folder{ first };
    while (*cursor != L'\0') { const std::wstring filename{ cursor }; paths.push_back(folder / filename); cursor += filename.size() + 1U; }
    return paths;
}

class MeshImportOptionsWindow {
public:
    MeshImportOptionsWindow(std::size_t fileCount, bool materialSlotsAvailable, bool skeletalImportAvailable)
        : fileCount_(fileCount)
        , materialSlotsAvailable_(materialSlotsAvailable)
        , skeletalImportAvailable_(skeletalImportAvailable)
        , importSkeletalMesh_(skeletalImportAvailable) {}

    [[nodiscard]] std::optional<kb::assets::AssetImportOptions> Show(HWND owner) {
        owner_ = owner;
        if (!Create()) return std::nullopt;
        RECT ownerRect{};
        if (owner_ == nullptr || GetWindowRect(owner_, &ownerRect) == FALSE) SystemParametersInfoW(SPI_GETWORKAREA, 0U, &ownerRect, 0U);
        const int ownerWidth = static_cast<int>(ownerRect.right - ownerRect.left);
        const int ownerHeight = static_cast<int>(ownerRect.bottom - ownerRect.top);
        const int left = static_cast<int>(ownerRect.left) + std::max(0, (ownerWidth - kDialogWidth) / 2);
        const int top = static_cast<int>(ownerRect.top) + std::max(0, (ownerHeight - kDialogHeight) / 2);
        EnableOwner(false);
        SetWindowPos(window_, HWND_TOPMOST, left, top, kDialogWidth, kDialogHeight, SWP_SHOWWINDOW);
        // No dialog navigation: this window handles its own keys.
        const EditorModalLoopExit exit = RunEditorModalMessageLoop(window_, false, [this]() noexcept { return !running_; });
        if (window_ != nullptr && IsWindow(window_) != 0) DestroyWindow(window_);
        EnableOwner(true);
        if (owner_ != nullptr && IsWindow(owner_) != 0) SetForegroundWindow(owner_);
        // An app quit or a window destroyed under the pump must not start an import.
        return exit == EditorModalLoopExit::Completed && accepted_
            ? std::optional<kb::assets::AssetImportOptions>{ kb::assets::AssetImportOptions{
                .mesh = {
                    .importMaterialSlots = importMaterialSlots_,
                    .importSkeletalMesh = importSkeletalMesh_,
                },
            } }
            : std::nullopt;
    }

private:
    [[nodiscard]] bool Create() {
        WNDCLASSEXW klass{};
        klass.cbSize = sizeof(klass);
        klass.style = CS_HREDRAW | CS_VREDRAW;
        klass.lpfnWndProc = &MeshImportOptionsWindow::WindowProc;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        klass.lpszClassName = kMeshImportOptionsClass;
        if (RegisterClassExW(&klass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kMeshImportOptionsClass, L"", WS_POPUP, 0, 0, kDialogWidth, kDialogHeight, owner_, nullptr, klass.hInstance, this);
        return window_ != nullptr;
    }

    void EnableOwner(bool enabled) const noexcept { if (owner_ != nullptr && IsWindow(owner_) != 0) EnableWindow(owner_, enabled ? TRUE : FALSE); }
    [[nodiscard]] RECT Client() const noexcept { RECT rect{}; GetClientRect(window_, &rect); return rect; }
    [[nodiscard]] RECT Viewport() const noexcept { const RECT client = Client(); return Rect(kPadding, kHeaderHeight + 12, client.right - kPadding, client.bottom - kFooterHeight - 10); }
    [[nodiscard]] RECT MaterialSlotsRow() const noexcept { const RECT viewport = Viewport(); return Rect(viewport.left, viewport.top + 68 - scrollOffset_, viewport.right - 12, viewport.top + 68 - scrollOffset_ + kRowHeight); }
    [[nodiscard]] RECT SkeletonRow() const noexcept { const RECT viewport = Viewport(); return Rect(viewport.left, viewport.top + 312 - scrollOffset_, viewport.right - 12, viewport.top + 312 - scrollOffset_ + kRowHeight); }
    [[nodiscard]] int MaxScroll() const noexcept { return std::max(0, 430 - Height(Viewport())); }
    void Scroll(int delta) noexcept { scrollOffset_ = std::clamp(scrollOffset_ + delta, 0, MaxScroll()); InvalidateRect(window_, nullptr, FALSE); }

    void Paint(HDC dc) const {
        const RECT client = Client();
        GdiDrawing::FillRectColor(dc, client, Rgb(13, 16, 21));
        GdiDrawing::DrawSharpFrame(dc, client, Rgb(13, 16, 21), Rgb(70, 92, 122));
        GdiDrawing::FillRectColor(dc, Rect(1, 1, client.right - 1, kHeaderHeight), Rgb(23, 28, 37));
        GdiDrawing::FillRectColor(dc, Rect(1, kHeaderHeight - 1, client.right - 1, kHeaderHeight), Rgb(55, 86, 124));
        { ScopedFont font(15, FW_SEMIBOLD); Text(dc, Rect(kPadding, 12, client.right - 64, 36), "Import Mesh", Rgb(236, 241, 248)); }
        Text(dc, Rect(kPadding, 36, client.right - 64, 54),
            ((importSkeletalMesh_ ? "SKELETAL MESH  /  " : "STATIC MESH  /  ") + std::to_string(fileCount_) + " FILE(S)").c_str(), Rgb(117, 159, 208));
        Text(dc, Rect(client.right - 43, 13, client.right - 13, 43), "×", Rgb(190, 199, 213), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const RECT viewport = Viewport();
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
        const int y = viewport.top - scrollOffset_;
        { ScopedFont font(12, FW_SEMIBOLD); Text(dc, Rect(viewport.left, y, viewport.right, y + 24), "MESH", Rgb(124, 167, 220)); }
        Text(dc, Rect(viewport.left, y + 25, viewport.right, y + 48),
            importSkeletalMesh_ ? "Skeletal glTF import" : "Static mesh import", Rgb(181, 191, 207));
        DrawCheckRow(
            dc,
            MaterialSlotsRow(),
            importMaterialSlots_,
            "Import material slots",
            materialSlotsAvailable_ ? "Preserve FBX material names and per-section assignments" : "Available only when every selected mesh is an FBX source",
            materialSlotsAvailable_ && !importSkeletalMesh_);
        { ScopedFont font(12, FW_SEMIBOLD); Text(dc, Rect(viewport.left, y + 132, viewport.right, y + 156), "IMPORT PIPELINE", Rgb(124, 167, 220)); }
        DrawDisabledRow(dc, Rect(viewport.left, y + 162, viewport.right - 12, y + 206), "Import textures", "Requires source texture extraction and texture-asset dependencies");
        DrawDisabledRow(dc, Rect(viewport.left, y + 212, viewport.right - 12, y + 256), "Import materials", "Requires generated material assets and FBX material translation");
        DrawDisabledRow(dc, Rect(viewport.left, y + 262, viewport.right - 12, y + 306), "Combine meshes", "Requires a multi-output FBX asset import pipeline");
        DrawCheckRow(
            dc,
            SkeletonRow(),
            importSkeletalMesh_,
            "Import skeleton",
            skeletalImportAvailable_
                ? "Creates Skeleton, Skeletal Mesh and Animation Clips from FBX, glTF, or GLB"
                : "Requires every selected source to use one supported skeletal format: FBX, glTF, or GLB",
            skeletalImportAvailable_);
        { ScopedFont font(11, FW_NORMAL); Text(dc, Rect(viewport.left, y + 374, viewport.right - 12, y + 416),
            importSkeletalMesh_
                ? "The importer publishes the rig, skinned mesh and clips atomically. Source materials remain unassigned until material import is available."
                : "Unavailable options are disabled deliberately. The editor never records settings it cannot execute deterministically.",
            Rgb(126, 136, 151), DT_LEFT | DT_WORDBREAK | DT_NOPREFIX); }
        RestoreDC(dc, saved);

        if (MaxScroll() > 0) {
            const RECT track = Rect(viewport.right - 6, viewport.top, viewport.right - 2, viewport.bottom);
            GdiDrawing::FillRectColor(dc, track, Rgb(28, 34, 43));
            const int thumb = std::max(30, Height(track) * Height(viewport) / std::max(1, Height(viewport) + MaxScroll()));
            const int top = track.top + (Height(track) - thumb) * scrollOffset_ / std::max(1, MaxScroll());
            GdiDrawing::FillRectColor(dc, Rect(track.left, top, track.right, top + thumb), Rgb(77, 134, 204));
        }
        GdiDrawing::FillRectColor(dc, Rect(1, client.bottom - kFooterHeight, client.right - 1, client.bottom - 1), Rgb(19, 24, 32));
        GdiDrawing::FillRectColor(dc, Rect(client.right - 198, client.bottom - 43, client.right - 106, client.bottom - 15), Rgb(31, 42, 57));
        GdiDrawing::DrawSharpFrame(dc, Rect(client.right - 198, client.bottom - 43, client.right - 106, client.bottom - 15), Rgb(31, 42, 57), Rgb(72, 93, 120));
        Text(dc, Rect(client.right - 198, client.bottom - 43, client.right - 106, client.bottom - 15), "Cancel", Rgb(211, 219, 230), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        GdiDrawing::FillRectColor(dc, Rect(client.right - 96, client.bottom - 43, client.right - 16, client.bottom - 15), Rgb(45, 126, 104));
        GdiDrawing::DrawSharpFrame(dc, Rect(client.right - 96, client.bottom - 43, client.right - 16, client.bottom - 15), Rgb(45, 126, 104), Rgb(101, 211, 168));
        Text(dc, Rect(client.right - 96, client.bottom - 43, client.right - 16, client.bottom - 15), "Import", Rgb(244, 255, 250), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    static void DrawCheckRow(HDC dc, const RECT& rect, bool checked, const char* title, const char* subtitle, bool enabled) {
        const COLORREF fill = enabled ? Rgb(24, 31, 41) : Rgb(18, 22, 29);
        const COLORREF border = enabled ? Rgb(49, 67, 90) : Rgb(37, 45, 57);
        GdiDrawing::FillRectColor(dc, rect, fill);
        GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
        const RECT check = Rect(rect.left + 12, rect.top + 12, rect.left + 29, rect.top + 29);
        const bool activeCheck = enabled && checked;
        GdiDrawing::FillRectColor(dc, check, activeCheck ? Rgb(48, 141, 114) : Rgb(16, 20, 27));
        GdiDrawing::DrawSharpFrame(dc, check, activeCheck ? Rgb(48, 141, 114) : Rgb(16, 20, 27), activeCheck ? Rgb(112, 225, 180) : (enabled ? Rgb(83, 97, 117) : Rgb(62, 71, 84)));
        if (activeCheck) DrawCheckMark(dc, check);
        { ScopedFont font(12, FW_SEMIBOLD); Text(dc, Rect(rect.left + 42, rect.top + 6, rect.right - 10, rect.top + 24), title, enabled ? Rgb(226, 234, 244) : Rgb(117, 125, 137)); }
        Text(dc, Rect(rect.left + 42, rect.top + 24, rect.right - 10, rect.bottom - 4), subtitle, Rgb(139, 153, 173));
    }

    static void DrawDisabledRow(HDC dc, const RECT& rect, const char* title, const char* subtitle) { DrawCheckRow(dc, rect, false, title, subtitle, false); }

    void OnClick(int x, int y) {
        const RECT client = Client();
        if (Contains(Rect(client.right - 52, 6, client.right - 6, 52), x, y)) { Close(false); return; }
        if (materialSlotsAvailable_ && !importSkeletalMesh_ && Contains(MaterialSlotsRow(), x, y)) { importMaterialSlots_ = !importMaterialSlots_; InvalidateRect(window_, nullptr, FALSE); return; }
        if (skeletalImportAvailable_ && Contains(SkeletonRow(), x, y)) { importSkeletalMesh_ = !importSkeletalMesh_; InvalidateRect(window_, nullptr, FALSE); return; }
        if (Contains(Rect(client.right - 198, client.bottom - 43, client.right - 106, client.bottom - 15), x, y)) { Close(false); return; }
        if (Contains(Rect(client.right - 96, client.bottom - 43, client.right - 16, client.bottom - 15), x, y)) Close(true);
    }
    void Close(bool accepted) noexcept { accepted_ = accepted; running_ = false; DestroyWindow(window_); }
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<MeshImportOptionsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) { self = static_cast<MeshImportOptionsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->window_ = window; }
        if (self == nullptr) return DefWindowProcW(window, message, wparam, lparam);
        switch (message) {
        case WM_PAINT: { PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); self->Paint(dc); EndPaint(window, &paint); return 0; }
        case WM_LBUTTONUP: self->OnClick(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)); return 0;
        case WM_MOUSEWHEEL: self->Scroll(GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -36 : 36); return 0;
        case WM_KEYDOWN: if (wparam == VK_ESCAPE) { self->Close(false); return 0; } if (wparam == VK_RETURN) { self->Close(true); return 0; } break;
        case WM_CLOSE: self->Close(false); return 0;
        // Drop the handle here, like the other three editor dialogs do: after this message the HWND value can
        // be reused by Windows for someone else's window, and the post-loop teardown must not act on it.
        case WM_NCDESTROY: if (self->window_ == window) { self->window_ = nullptr; } SetWindowLongPtrW(window, GWLP_USERDATA, 0); break;
        default: break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    HWND window_ = nullptr;
    HWND owner_ = nullptr;
    std::size_t fileCount_ = 0U;
    int scrollOffset_ = 0;
    bool materialSlotsAvailable_ = false;
    bool skeletalImportAvailable_ = false;
    bool importMaterialSlots_ = true;
    bool importSkeletalMesh_ = false;
    bool accepted_ = false;
    bool running_ = true;
};

} // namespace

std::optional<EditorAssetImportSelection> EditorAssetImportDialog::Open(HWND owner) {
    std::array<wchar_t, 65536> fileBuffer{};
    const std::wstring filter = WidenAsciiFilter(kb::assets::AssetImportCatalog::WindowsFileDialogFilter());
    OPENFILENAMEW openFileName{};
    openFileName.lStructSize = sizeof(openFileName); openFileName.hwndOwner = owner; openFileName.lpstrFilter = filter.c_str(); openFileName.lpstrFile = fileBuffer.data(); openFileName.nMaxFile = static_cast<DWORD>(fileBuffer.size()); openFileName.lpstrTitle = L"Import assets";
    openFileName.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&openFileName) == FALSE) return std::nullopt;
    std::vector<std::filesystem::path> files = ParseOpenFileNameBuffer(fileBuffer);
    const bool containsMesh = std::ranges::any_of(files, [](const std::filesystem::path& path) { return kb::assets::AssetImportCatalog::ClassifyExtension(path.extension()) == kb::assets::AssetImportCategory::Model; });
    kb::assets::AssetImportOptions options{};
    const bool materialSlotsAvailable = !files.empty() && std::ranges::all_of(files, [](const std::filesystem::path& path) {
        std::string extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        return extension == ".fbx";
    });
    const bool skeletalImportAvailable = !files.empty() && std::ranges::all_of(files, [](const std::filesystem::path& path) {
        std::string extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        return extension == ".fbx" || extension == ".gltf" || extension == ".glb";
    });
    if (containsMesh) { const std::optional<kb::assets::AssetImportOptions> meshOptions = MeshImportOptionsWindow{ files.size(), materialSlotsAvailable, skeletalImportAvailable }.Show(owner); if (!meshOptions.has_value()) return std::nullopt; options = *meshOptions; }
    return EditorAssetImportSelection{ .files = std::move(files), .options = options };
}

} // namespace kb::editor

#endif
