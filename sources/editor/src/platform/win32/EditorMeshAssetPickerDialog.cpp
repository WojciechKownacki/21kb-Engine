#include "platform/win32/EditorMeshAssetPickerDialog.hpp"
#include "platform/win32/EditorMaterialAssetPickerDialog.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/EditorTexturePreviewService.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/EditorSceneMeshAssetActions.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kMeshPickerClassName[] = L"KBEditorMeshAssetPickerDialog";
constexpr int kDialogWidth = 480;
constexpr int kDialogHeight = 420;
constexpr int kHeaderHeight = 58;
constexpr int kFooterHeight = 42;
constexpr int kRowHeight = 48;
constexpr int kPad = 14;
constexpr int kCloseSize = 24;
constexpr int kScrollbarWidth = 10;

struct AssetPickerRow {
    kb::assets::AssetId assetId{};
    std::string name;
    std::string path;
};

struct AssetPickerResult {
    bool accepted = false;
    kb::assets::AssetId assetId{};
};

[[nodiscard]] COLORREF Color(EditorColor color) {
    return GdiDrawing::ToColorRef(color);
}

[[nodiscard]] COLORREF Rgb(int r, int g, int b) noexcept {
    return RGB(r, g, b);
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

[[nodiscard]] int RectWidth(const RECT& rect) noexcept {
    return std::max(0L, rect.right - rect.left);
}

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept {
    return RECT{ left, top, right, bottom };
}

void Text(HDC dc, RECT rect, std::string_view text, COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
}

[[nodiscard]] std::string DisplayName(const kb::assets::AssetMetadata& metadata, std::string_view fallback) {
    if (!metadata.name.empty()) {
        return metadata.name;
    }
    if (!metadata.virtualPath.empty()) {
        return metadata.virtualPath.stem().string();
    }
    return std::string{ fallback };
}

[[nodiscard]] std::string DisplayPath(const kb::assets::AssetMetadata& metadata) {
    if (!metadata.virtualPath.empty()) {
        return kb::assets::NormalizeAssetPath(metadata.virtualPath);
    }
    return metadata.physicalPath.string();
}

[[nodiscard]] std::string TextureFilterLabel(EditorTextureAssetPickerFilter filter) {
    switch (filter) {
    case EditorTextureAssetPickerFilter::TextureCube:
        return "Texture Cube";
    case EditorTextureAssetPickerFilter::TextureVolume:
        return "Texture Volume";
    case EditorTextureAssetPickerFilter::Texture2DArray:
        return "Texture 2D Array";
    case EditorTextureAssetPickerFilter::Texture2D:
    default:
        return "Texture 2D";
    }
}

[[nodiscard]] std::string TextureFilterDescription(EditorTextureAssetPickerFilter filter) {
    return "Choose a " + TextureFilterLabel(filter) + " asset for this material graph node.";
}

[[nodiscard]] std::vector<AssetPickerRow> BuildMeshRows(const EditorSceneContext& sceneContext) {
    std::vector<AssetPickerRow> rows;
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (!EditorSceneMeshAssetActions::IsMeshAsset(metadata)) {
            continue;
        }
        rows.push_back(AssetPickerRow{ .assetId = metadata.id, .name = DisplayName(metadata, "Mesh"), .path = DisplayPath(metadata) });
    }
    std::ranges::sort(rows, [](const AssetPickerRow& lhs, const AssetPickerRow& rhs) {
        if (lhs.name != rhs.name) {
            return lhs.name < rhs.name;
        }
        return lhs.assetId.value < rhs.assetId.value;
    });
    return rows;
}

[[nodiscard]] std::vector<AssetPickerRow> BuildMaterialRows(const EditorSceneContext& sceneContext) {
    std::vector<AssetPickerRow> rows;
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (!EditorSceneMaterialAssetActions::IsMaterialAsset(metadata)) {
            continue;
        }
        rows.push_back(AssetPickerRow{ .assetId = metadata.id, .name = DisplayName(metadata, "Material"), .path = DisplayPath(metadata) });
    }
    std::ranges::sort(rows, [](const AssetPickerRow& lhs, const AssetPickerRow& rhs) {
        if (lhs.name != rhs.name) {
            return lhs.name < rhs.name;
        }
        return lhs.assetId.value < rhs.assetId.value;
    });
    return rows;
}

[[nodiscard]] std::vector<AssetPickerRow> BuildTextureRows(const EditorSceneContext& sceneContext, EditorTextureAssetPickerFilter filter) {
    std::vector<AssetPickerRow> rows;
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (!EditorTextureAssetMatchesFilter(metadata, filter)) {
            continue;
        }
        rows.push_back(AssetPickerRow{ .assetId = metadata.id, .name = DisplayName(metadata, "Texture"), .path = DisplayPath(metadata) });
    }
    std::ranges::sort(rows, [](const AssetPickerRow& lhs, const AssetPickerRow& rhs) {
        if (lhs.name != rhs.name) {
            return lhs.name < rhs.name;
        }
        return lhs.assetId.value < rhs.assetId.value;
    });
    return rows;
}

[[nodiscard]] RECT CenteredWindowRect(HWND owner) {
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

class AssetPickerWindow {
public:
    AssetPickerWindow(
        const EditorTheme& theme,
        std::vector<AssetPickerRow> rows,
        kb::assets::AssetId currentAsset,
        std::string title,
        std::string description,
        std::string clearDescription,
        HeroIconKind icon,
        const kb::assets::AssetManager* assetManager = nullptr,
        bool textureThumbnails = false)
        : theme_(theme)
        , rows_(std::move(rows))
        , currentAsset_(currentAsset)
        , title_(std::move(title))
        , description_(std::move(description))
        , clearDescription_(std::move(clearDescription))
        , icon_(icon)
        , assetManager_(assetManager)
        , textureThumbnails_(textureThumbnails) {}

    [[nodiscard]] AssetPickerResult Show(HWND owner) {
        owner_ = owner;
        if (!EnsureWindow()) {
            return {};
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
        return result_;
    }

private:
    [[nodiscard]] bool EnsureWindow() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &AssetPickerWindow::WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = kMeshPickerClassName;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kMeshPickerClassName, L"Select Asset", WS_POPUP, 0, 0, kDialogWidth, kDialogHeight, owner_, nullptr, windowClass.hInstance, this);
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

    [[nodiscard]] RECT CloseButton() const noexcept {
        const RECT client = Client();
        return Rect(client.right - kPad - kCloseSize, kPad, client.right - kPad, kPad + kCloseSize);
    }

    [[nodiscard]] RECT ListRect() const noexcept {
        const RECT client = Client();
        return Rect(kPad, kHeaderHeight, client.right - kPad, client.bottom - kFooterHeight);
    }

    [[nodiscard]] int RowCount() const noexcept {
        return static_cast<int>(rows_.size()) + 1;
    }

    [[nodiscard]] int MaxScroll() const noexcept {
        return std::max(0, RowCount() * kRowHeight - RectHeight(ListRect()));
    }

    [[nodiscard]] int RowAt(int x, int y) const noexcept {
        const RECT list = ListRect();
        if (!Contains(list, x, y)) {
            return -1;
        }
        const int row = (y - list.top + scrollOffset_) / kRowHeight;
        return row >= 0 && row < RowCount() ? row : -1;
    }

    void AcceptRow(int row) noexcept {
        if (row < 0 || row >= RowCount()) {
            return;
        }
        result_.accepted = true;
        result_.assetId = row == 0 ? kb::assets::AssetId{} : rows_[static_cast<std::size_t>(row - 1)].assetId;
        running_ = false;
        DestroyWindow(window_);
    }

    void Paint(HDC dc) const {
        const RECT client = Client();
        GdiDrawing::FillRectColor(dc, client, Rgb(17, 19, 23));
        GdiDrawing::DrawSharpFrame(dc, client, Rgb(17, 19, 23), Rgb(68, 76, 88));
        {
            ScopedFont titleFont(15, FW_SEMIBOLD);
            const ScopedGdiObject selectedFont(dc, titleFont.handle);
            Text(dc, Rect(kPad, 10, client.right - 58, 31), title_, Color(theme_.textPrimary));
        }
        Text(dc, Rect(kPad, 31, client.right - 58, 52), description_, Color(theme_.textSecondary));

        const RECT close = CloseButton();
        GdiDrawing::DrawSharpFrame(dc, close, hoveredRow_ == -2 ? Rgb(43, 48, 56) : Rgb(27, 30, 35), hoveredRow_ == -2 ? Color(theme_.accent) : Rgb(74, 82, 94));
        Text(dc, close, "x", hoveredRow_ == -2 ? Color(theme_.textPrimary) : Color(theme_.textSecondary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const RECT list = ListRect();
        GdiDrawing::DrawSharpFrame(dc, list, Rgb(20, 23, 27), Rgb(52, 58, 68));
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, list.left + 1, list.top + 1, list.right - 1, list.bottom - 1);
        for (int row = 0; row < RowCount(); ++row) {
            const int top = list.top + row * kRowHeight - scrollOffset_;
            const RECT rect = Rect(list.left + 1, top, list.right - 1 - kScrollbarWidth, top + kRowHeight);
            if (rect.bottom <= list.top || rect.top >= list.bottom) {
                continue;
            }
            const kb::assets::AssetId rowAsset = row == 0 ? kb::assets::AssetId{} : rows_[static_cast<std::size_t>(row - 1)].assetId;
            const bool selected = rowAsset.value == currentAsset_.value;
            const bool hovered = hoveredRow_ == row;
            GdiDrawing::FillRectColor(dc, rect, selected ? Rgb(35, 62, 78) : hovered ? Rgb(32, 37, 44) : (row % 2 == 0 ? Rgb(22, 25, 30) : Rgb(19, 22, 26)));

            RECT icon = Rect(rect.left + 10, rect.top + 7, rect.left + 44, rect.top + 41);
            bool drewTexturePreview = false;
            if (textureThumbnails_ && row > 0 && assetManager_ != nullptr) {
                const kb::assets::AssetMetadata* metadata = assetManager_->Registry().Find(rows_[static_cast<std::size_t>(row - 1)].assetId);
                if (metadata != nullptr) {
                    GdiDrawing::DrawSharpFrame(dc, icon, Rgb(12, 14, 17), selected ? Color(theme_.accent) : Rgb(68, 78, 94));
                    if (const EditorTexturePreviewImage* preview = EditorTexturePreviewService::PreviewFor(*metadata); preview != nullptr) {
                        EditorTexturePreviewService::DrawContain(dc, Rect(icon.left + 1, icon.top + 1, icon.right - 1, icon.bottom - 1), *preview, false);
                        drewTexturePreview = true;
                    }
                }
            }
            if (!drewTexturePreview) {
                HeroIconPainter::Draw(dc, icon, icon_, selected ? Color(theme_.accent) : Rgb(143, 158, 178), 2);
            }
            const std::string name = row == 0 ? std::string{ "None" } : rows_[static_cast<std::size_t>(row - 1)].name;
            const std::string path = row == 0 ? clearDescription_ : rows_[static_cast<std::size_t>(row - 1)].path;
            {
                ScopedFont nameFont(12, FW_SEMIBOLD);
                const ScopedGdiObject selectedFont(dc, nameFont.handle);
                Text(dc, Rect(rect.left + 56, rect.top + 7, rect.right - 10, rect.top + 26), name, Color(theme_.textPrimary));
            }
            Text(dc, Rect(rect.left + 56, rect.top + 26, rect.right - 10, rect.bottom - 5), path, selected ? Rgb(170, 221, 238) : Color(theme_.textSecondary));
        }
        RestoreDC(dc, saved);
        PaintScrollbar(dc, list);
        Text(dc, Rect(kPad, client.bottom - 32, client.right - kPad, client.bottom - 10), "Esc closes. Click a row to assign it.", Color(theme_.textSecondary));
    }

    void PaintScrollbar(HDC dc, RECT list) const {
        const int maxScroll = MaxScroll();
        if (maxScroll <= 0) {
            return;
        }
        const RECT track = Rect(list.right - kScrollbarWidth, list.top + 4, list.right - 4, list.bottom - 4);
        GdiDrawing::FillRectColor(dc, track, Rgb(14, 16, 19));
        const int contentHeight = RowCount() * kRowHeight;
        const int thumbHeight = std::clamp((RectHeight(track) * RectHeight(list)) / std::max(1, contentHeight), 28, RectHeight(track));
        const int travel = std::max(0, RectHeight(track) - thumbHeight);
        const int thumbTop = track.top + (travel * std::clamp(scrollOffset_, 0, maxScroll)) / std::max(1, maxScroll);
        GdiDrawing::FillRectColor(dc, Rect(track.left + 1, thumbTop, track.right - 1, thumbTop + thumbHeight), Rgb(83, 96, 113));
    }

    void UpdateHover(int x, int y) {
        const int next = Contains(CloseButton(), x, y) ? -2 : RowAt(x, y);
        if (next != hoveredRow_) {
            hoveredRow_ = next;
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    void Scroll(int delta) {
        const int next = std::clamp(scrollOffset_ + delta, 0, MaxScroll());
        if (next != scrollOffset_) {
            scrollOffset_ = next;
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* picker = reinterpret_cast<AssetPickerWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
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
        case WM_MOUSEMOVE:
            if (picker != nullptr) {
                picker->UpdateHover(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (picker != nullptr) {
                const int x = GET_X_LPARAM(lparam);
                const int y = GET_Y_LPARAM(lparam);
                if (Contains(picker->CloseButton(), x, y)) {
                    picker->running_ = false;
                    DestroyWindow(window);
                    return 0;
                }
                picker->AcceptRow(picker->RowAt(x, y));
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            if (picker != nullptr) {
                picker->Scroll(GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -kRowHeight : kRowHeight);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (picker != nullptr && wparam == VK_ESCAPE) {
                picker->running_ = false;
                DestroyWindow(window);
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
    EditorTheme theme_{};
    std::vector<AssetPickerRow> rows_;
    kb::assets::AssetId currentAsset_{};
    std::string title_;
    std::string description_;
    std::string clearDescription_;
    HeroIconKind icon_ = HeroIconKind::Cube;
    const kb::assets::AssetManager* assetManager_ = nullptr;
    bool textureThumbnails_ = false;
    AssetPickerResult result_{};
    bool running_ = true;
    int hoveredRow_ = -1;
    int scrollOffset_ = 0;
};

} // namespace

EditorMeshAssetPickerDialog::Result EditorMeshAssetPickerDialog::Show(
    HWND owner,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId currentMesh) {
    AssetPickerWindow window{
        theme,
        BuildMeshRows(sceneContext),
        currentMesh,
        "Select Mesh",
        "Choose a mesh asset for the selected Mesh Renderer.",
        "Clear Mesh Renderer mesh",
        HeroIconKind::Cube,
    };
    const AssetPickerResult result = window.Show(owner);
    return EditorMeshAssetPickerDialog::Result{ .accepted = result.accepted, .assetId = result.assetId };
}

EditorMaterialAssetPickerDialog::Result EditorMaterialAssetPickerDialog::Show(
    HWND owner,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId currentMaterial) {
    AssetPickerWindow window{
        theme,
        BuildMaterialRows(sceneContext),
        currentMaterial,
        "Select Material",
        "Choose a material asset for the selected Mesh Renderer.",
        "Clear Mesh Renderer material",
        HeroIconKind::AdjustmentsHorizontal,
    };
    const AssetPickerResult result = window.Show(owner);
    return EditorMaterialAssetPickerDialog::Result{ .accepted = result.accepted, .assetId = result.assetId };
}

EditorTextureAssetPickerDialog::Result EditorTextureAssetPickerDialog::Show(
    HWND owner,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId currentTexture,
    EditorTextureAssetPickerFilter filter) {
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    AssetPickerWindow window{
        theme,
        BuildTextureRows(sceneContext, filter),
        currentTexture,
        "Select " + TextureFilterLabel(filter),
        TextureFilterDescription(filter),
        "Clear material graph texture",
        HeroIconKind::RectangleGroup,
        &manager,
        true,
    };
    const AssetPickerResult result = window.Show(owner);
    return EditorTextureAssetPickerDialog::Result{ .accepted = result.accepted, .assetId = result.assetId };
}

} // namespace kb::editor

#endif
