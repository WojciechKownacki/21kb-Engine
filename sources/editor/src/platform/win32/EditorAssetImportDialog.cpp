#include "platform/win32/EditorAssetImportDialog.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetImportCatalog.hpp"
#include "platform/win32/EditorModalMessageLoop.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/components/EditorDialogStyle.hpp"

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
constexpr int kHeaderHeight = EditorDialogStyle::HeaderHeight;
constexpr int kFooterHeight = EditorDialogStyle::FooterHeight;
constexpr int kPadding = EditorDialogStyle::Padding;
constexpr int kRowHeight = EditorDialogStyle::ListRowHeight;

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept { return RECT{ left, top, right, bottom }; }
[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept { return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom; }
[[nodiscard]] int Height(const RECT& rect) noexcept { return std::max(0, static_cast<int>(rect.bottom - rect.top)); }
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
                    .importTextures = importTextures_,
                    .importMaterials = importMaterials_,
                    .combineMeshes = combineMeshes_,
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
    [[nodiscard]] RECT TexturesRow() const noexcept { const RECT viewport = Viewport(); return Rect(viewport.left, viewport.top + 162 - scrollOffset_, viewport.right - 12, viewport.top + 162 - scrollOffset_ + kRowHeight); }
    [[nodiscard]] RECT MaterialsRow() const noexcept { const RECT viewport = Viewport(); return Rect(viewport.left, viewport.top + 212 - scrollOffset_, viewport.right - 12, viewport.top + 212 - scrollOffset_ + kRowHeight); }
    [[nodiscard]] RECT CombineMeshesRow() const noexcept { const RECT viewport = Viewport(); return Rect(viewport.left, viewport.top + 262 - scrollOffset_, viewport.right - 12, viewport.top + 262 - scrollOffset_ + kRowHeight); }
    [[nodiscard]] RECT SkeletonRow() const noexcept { const RECT viewport = Viewport(); return Rect(viewport.left, viewport.top + 312 - scrollOffset_, viewport.right - 12, viewport.top + 312 - scrollOffset_ + kRowHeight); }
    [[nodiscard]] int MaxScroll() const noexcept { return std::max(0, 430 - Height(Viewport())); }
    void Scroll(int delta) noexcept { scrollOffset_ = std::clamp(scrollOffset_ + delta, 0, MaxScroll()); InvalidateRect(window_, nullptr, FALSE); }

    void Paint(HDC dc) const {
        const RECT client = Client();
        const EditorTheme theme = MakeEditorDarkTheme();
        EditorDialogStyle::PaintSurface(dc, client, theme);
        const std::string description = (importSkeletalMesh_ ? "Skeletal mesh  /  " : "Static mesh  /  ")
            + std::to_string(fileCount_) + " file(s)";
        EditorDialogStyle::PaintHeader(
            dc,
            theme,
            EditorDialogHeaderDescriptor{
                .bounds = Rect(1, 3, client.right - 1, kHeaderHeight),
                .closeButton = Rect(client.right - 36, 6, client.right - 14, 28),
                .title = "Import Mesh",
                .description = description,
                .icon = HeroIconKind::DocumentText,
                .showIcon = true,
            });
        const RECT viewport = Viewport();
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
        const int y = viewport.top - scrollOffset_;
        EditorDialogStyle::PaintText(dc, Rect(viewport.left, y, viewport.right, y + 24), "Mesh", EditorDialogStyle::Color(theme.accent), 12, FW_SEMIBOLD);
        EditorDialogStyle::PaintText(
            dc,
            Rect(viewport.left, y + 25, viewport.right, y + 48),
            importSkeletalMesh_ ? "Skeletal source import" : "Static mesh import",
            EditorDialogStyle::Color(theme.textSecondary));
        DrawCheckRow(
            dc,
            theme,
            MaterialSlotsRow(),
            importMaterialSlots_,
            "Import material slots",
            materialSlotsAvailable_ ? "Preserve FBX material names and per-section assignments" : "Available only when every selected mesh is an FBX source",
            materialSlotsAvailable_ && !importSkeletalMesh_);
        EditorDialogStyle::PaintText(dc, Rect(viewport.left, y + 132, viewport.right, y + 156), "Import pipeline", EditorDialogStyle::Color(theme.accent), 12, FW_SEMIBOLD);
        DrawCheckRow(dc, theme, TexturesRow(), importTextures_, "Import textures",
            "Copies external images and extracts embedded FBX, glTF, or GLB image data", skeletalImportAvailable_);
        DrawCheckRow(dc, theme, MaterialsRow(), importMaterials_, "Import materials",
            "Creates PBR Material assets and assigns them to imported mesh sections", skeletalImportAvailable_);
        DrawCheckRow(dc, theme, CombineMeshesRow(), combineMeshes_, "Combine meshes",
            "Merges all compatible mesh nodes bound to the imported skin into one asset", skeletalImportAvailable_);
        DrawCheckRow(
            dc,
            theme,
            SkeletonRow(),
            importSkeletalMesh_,
            "Import skeleton",
            skeletalImportAvailable_
                ? "Creates Skeleton, Skeletal Mesh and Animation Clips from FBX, glTF, or GLB"
                : "Requires every selected source to use one supported skeletal format: FBX, glTF, or GLB",
            skeletalImportAvailable_);
        EditorDialogStyle::PaintText(dc, Rect(viewport.left, y + 374, viewport.right - 12, y + 416),
            importSkeletalMesh_
                ? "The importer publishes textures, materials, rig, skinned mesh and clips as one deterministic import operation."
                : "Static mesh import uses the same texture and material asset pipeline.",
            EditorDialogStyle::Color(theme.textDisabled), 11, FW_NORMAL, DT_LEFT | DT_WORDBREAK);
        RestoreDC(dc, saved);

        if (MaxScroll() > 0) {
            const RECT track = Rect(viewport.right - 6, viewport.top, viewport.right - 2, viewport.bottom);
            const int thumb = std::max(30, Height(track) * Height(viewport) / std::max(1, Height(viewport) + MaxScroll()));
            const int top = track.top + (Height(track) - thumb) * scrollOffset_ / std::max(1, MaxScroll());
            EditorDialogStyle::PaintScrollbar(dc, track, Rect(track.left, top, track.right, top + thumb), theme);
        }
        const RECT footer = Rect(1, client.bottom - kFooterHeight, client.right - 1, client.bottom - 1);
        EditorDialogStyle::PaintFooter(dc, footer, theme);
        EditorDialogStyle::PaintButton(dc, Rect(client.right - 198, client.bottom - 27, client.right - 106, client.bottom - 3), theme, "Cancel");
        EditorDialogStyle::PaintButton(dc, Rect(client.right - 96, client.bottom - 27, client.right - 16, client.bottom - 3), theme, "Import", EditorDialogButtonTone::Primary);
    }

    static void DrawCheckRow(HDC dc, const EditorTheme& theme, const RECT& rect, bool checked, const char* title, const char* subtitle, bool enabled) {
        GdiDrawing::FillRectColor(dc, rect, EditorDialogStyle::Color(theme.panel));
        EditorDialogStyle::PaintDivider(dc, Rect(rect.left, rect.bottom - 1, rect.right, rect.bottom), theme);
        const RECT check = Rect(rect.left + 12, rect.top + 15, rect.left + 29, rect.top + 32);
        const bool activeCheck = enabled && checked;
        EditorDialogStyle::PaintCheckbox(dc, check, theme, activeCheck, enabled);
        EditorDialogStyle::PaintText(dc, Rect(rect.left + 42, rect.top + 5, rect.right - 10, rect.top + 25), title, EditorDialogStyle::Color(enabled ? theme.textPrimary : theme.textDisabled), 12, FW_SEMIBOLD);
        EditorDialogStyle::PaintText(dc, Rect(rect.left + 42, rect.top + 24, rect.right - 10, rect.bottom - 4), subtitle, EditorDialogStyle::Color(enabled ? theme.textSecondary : theme.textDisabled), 11);
    }

    void OnClick(int x, int y) {
        const RECT client = Client();
        if (Contains(Rect(client.right - 52, 6, client.right - 6, 52), x, y)) { Close(false); return; }
        if (materialSlotsAvailable_ && !importSkeletalMesh_ && Contains(MaterialSlotsRow(), x, y)) { importMaterialSlots_ = !importMaterialSlots_; InvalidateRect(window_, nullptr, FALSE); return; }
        if (skeletalImportAvailable_ && Contains(TexturesRow(), x, y)) {
            importTextures_ = !importTextures_;
            if (!importTextures_) importMaterials_ = false;
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        if (skeletalImportAvailable_ && Contains(MaterialsRow(), x, y)) {
            importMaterials_ = !importMaterials_;
            if (importMaterials_) importTextures_ = true;
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        if (skeletalImportAvailable_ && Contains(CombineMeshesRow(), x, y)) {
            combineMeshes_ = !combineMeshes_;
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        if (skeletalImportAvailable_ && Contains(SkeletonRow(), x, y)) { importSkeletalMesh_ = !importSkeletalMesh_; InvalidateRect(window_, nullptr, FALSE); return; }
        if (Contains(Rect(client.right - 198, client.bottom - 27, client.right - 106, client.bottom - 3), x, y)) { Close(false); return; }
        if (Contains(Rect(client.right - 96, client.bottom - 27, client.right - 16, client.bottom - 3), x, y)) Close(true);
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
    bool importTextures_ = true;
    bool importMaterials_ = true;
    bool combineMeshes_ = true;
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
