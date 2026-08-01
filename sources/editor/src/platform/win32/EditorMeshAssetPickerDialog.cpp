#include "platform/win32/EditorMeshAssetPickerDialog.hpp"
#include "platform/win32/EditorAnimatorControllerAssetPickerDialog.hpp"
#include "platform/win32/EditorMaterialAssetPickerDialog.hpp"

#if defined(_WIN32)
#include "platform/win32/EditorModalMessageLoop.hpp"
#include "platform/win32/EditorModalWindowScope.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "rendering/EditorMeshPreviewService.hpp"
#include "rendering/EditorMeshPreviewTypes.hpp"
#include "rendering/EditorMaterialThumbnailService.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "rendering/EditorTexturePreviewService.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/MaterialPreviewTextureAverageColor.hpp"
#include "rendering/ProjectFilesMaterialPreviewThumbnailModel.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/EditorSceneMeshAssetActions.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kMeshPickerClassName[] = L"KBEditorMeshAssetPickerDialog";
constexpr int kListDialogWidth = 480;
constexpr int kListDialogHeight = 420;
constexpr int kTextureDialogWidth = 720;
constexpr int kTextureDialogHeight = 560;
constexpr int kListHeaderHeight = 58;
constexpr int kFooterHeight = 42;
constexpr int kRowHeight = 48;
constexpr int kPad = 14;
constexpr int kCloseSize = 24;
constexpr int kScrollbarWidth = 10;
constexpr int kTextureHeaderHeight = 34;
constexpr int kTextureTileWidth = 158;
constexpr int kTextureTileHeight = 150;
constexpr int kTextureTileGap = 10;
constexpr int kTextureColumns = 4;
constexpr int kTextureViewportHeight = kTextureDialogHeight - 62;
constexpr int kTextureSearchHeight = 28;
constexpr int kTextureButtonWidth = 82;
// Shared asset grid used by mesh and material pickers.
constexpr int kAssetGridDialogWidth = 588;
constexpr int kAssetGridDialogHeight = 560;
constexpr int kAssetTileWidth = 168;
constexpr int kAssetTileHeight = 176;
constexpr int kAssetTilePreviewHeight = 130;
constexpr int kAssetTileGap = 12;
constexpr int kAssetTileColumns = 3;
constexpr UINT_PTR kMaterialThumbnailTimerId = 1U;
constexpr UINT kMaterialThumbnailTimerPeriodMs = 16U;

struct AssetPickerRow {
    kb::assets::AssetId assetId{};
    std::string name;
    std::string path;
};

struct AssetPickerResult {
    bool accepted = false;
    kb::assets::AssetId assetId{};
};

enum class AssetPickerTileKind : std::uint8_t {
    None,
    Mesh,
    Material,
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

[[nodiscard]] int LerpChannel(int a, int b, int num, int den) noexcept {
    return a + ((b - a) * num) / std::max(1, den);
}

[[nodiscard]] COLORREF BlendColor(COLORREF a, COLORREF b, int num, int den) noexcept {
    return RGB(
        LerpChannel(GetRValue(a), GetRValue(b), num, den),
        LerpChannel(GetGValue(a), GetGValue(b), num, den),
        LerpChannel(GetBValue(a), GetBValue(b), num, den));
}

void FillVerticalGradient(HDC dc, RECT rect, COLORREF top, COLORREF bottom) {
    const int height = RectHeight(rect);
    for (int y = 0; y < height; ++y) {
        GdiDrawing::FillRectColor(
            dc,
            Rect(rect.left, rect.top + y, rect.right, rect.top + y + 1),
            BlendColor(top, bottom, y, std::max(1, height - 1)));
    }
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

[[nodiscard]] std::vector<AssetPickerRow> BuildAnimatorControllerRows(const EditorSceneContext& sceneContext) {
    std::vector<AssetPickerRow> rows;
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (metadata.type != kb::scene::kAnimatorControllerAssetType) {
            continue;
        }
        rows.push_back(AssetPickerRow{
            .assetId = metadata.id,
            .name = DisplayName(metadata, "Animator Controller"),
            .path = DisplayPath(metadata),
        });
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

[[nodiscard]] std::string LowerAscii(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

[[nodiscard]] bool TextureRowMatchesQuery(const AssetPickerRow& row, const std::string& query) {
    if (query.empty()) {
        return true;
    }
    const std::string needle = LowerAscii(query);
    return LowerAscii(row.name).find(needle) != std::string::npos ||
        LowerAscii(row.path).find(needle) != std::string::npos;
}

[[nodiscard]] bool ValidAsset(kb::assets::AssetId assetId) noexcept {
    return assetId.value != 0U;
}

// Blits a rendered mesh thumbnail (top-down BGRA) into a target rect, matching
// the Project Files tile renderer's StretchDIBits path (HALFTONE downscale).
void DrawMeshThumbnail(HDC dc, const RECT& target, const EditorMeshThumbnailImage& image) {
    if (image.width <= 0 || image.height <= 0 || image.bgra.empty() || RectWidth(target) <= 0 || RectHeight(target) <= 0) {
        return;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width;
    info.bmiHeader.biHeight = -image.height; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const int oldMode = SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    static_cast<void>(StretchDIBits(
        dc,
        target.left,
        target.top,
        RectWidth(target),
        RectHeight(target),
        0,
        0,
        image.width,
        image.height,
        image.bgra.data(),
        &info,
        DIB_RGB_COLORS,
        SRCCOPY));
    SetStretchBltMode(dc, oldMode);
}

void DrawRenderedMaterialThumbnail(
    HDC dc,
    const RECT& target,
    const EditorMaterialThumbnailImage& image) {
    if (image.width <= 0 || image.height <= 0 || image.bgra.empty() ||
        RectWidth(target) <= 0 || RectHeight(target) <= 0) {
        return;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width;
    info.bmiHeader.biHeight = -image.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC sourceDc = CreateCompatibleDC(dc);
    if (sourceDc == nullptr) {
        return;
    }
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0U);
    if (bitmap != nullptr && bits != nullptr) {
        std::memcpy(bits, image.bgra.data(), image.bgra.size() * sizeof(std::uint32_t));
        HGDIOBJ previous = SelectObject(sourceDc, bitmap);
        const BLENDFUNCTION blend{ AC_SRC_OVER, 0U, 255U, AC_SRC_ALPHA };
        static_cast<void>(AlphaBlend(
            dc,
            target.left,
            target.top,
            RectWidth(target),
            RectHeight(target),
            sourceDc,
            0,
            0,
            image.width,
            image.height,
            blend));
        SelectObject(sourceDc, previous);
    }
    if (bitmap != nullptr) {
        DeleteObject(bitmap);
    }
    DeleteDC(sourceDc);
}

[[nodiscard]] RECT CenteredWindowRect(HWND owner, int width, int height) {
    RECT base{};
    if (owner != nullptr && IsWindow(owner) != 0) {
        GetWindowRect(owner, &base);
    } else {
        base = RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    }
    const int left = base.left + std::max(0, (RectWidth(base) - width) / 2);
    const int top = base.top + std::max(0, (RectHeight(base) - height) / 2);
    return Rect(left, top, left + width, top + height);
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
        bool textureThumbnails = false,
        AssetPickerTileKind tileKind = AssetPickerTileKind::None,
        bool allowClear = true,
        EditorSceneContext* sceneContext = nullptr,
        EditorSceneBgfxViewport* sceneViewport = nullptr)
        : theme_(theme)
        , rows_(std::move(rows))
        , currentAsset_(currentAsset)
        , selectedTextureAsset_()
        , title_(std::move(title))
        , description_(std::move(description))
        , clearDescription_(std::move(clearDescription))
        , icon_(icon)
        , assetManager_(assetManager)
        , textureThumbnails_(textureThumbnails)
        , tileKind_(tileKind)
        , allowClear_(allowClear)
        , sceneContext_(sceneContext)
        , sceneViewport_(sceneViewport) {}

    [[nodiscard]] AssetPickerResult Show(HWND owner) {
        owner_ = owner;
        if (!EnsureWindow()) {
            return {};
        }
        if (tileKind_ == AssetPickerTileKind::Material && sceneContext_ != nullptr && sceneViewport_ != nullptr) {
            lastMaterialThumbnailRevision_ = EditorMaterialThumbnailCache().Revision();
            static_cast<void>(SetTimer(window_, kMaterialThumbnailTimerId, kMaterialThumbnailTimerPeriodMs, nullptr));
        }
        const RECT bounds = CenteredWindowRect(owner, DialogWidth(), DialogHeight());
        EditorModalLoopExit exit = EditorModalLoopExit::Completed;
        {
            const EditorModalWindowScope modal{ window_ };
            SetWindowPos(window_, textureThumbnails_ ? HWND_TOP : HWND_TOPMOST, bounds.left, bounds.top, RectWidth(bounds), RectHeight(bounds), SWP_SHOWWINDOW);
            SetForegroundWindow(window_);

            // No dialog navigation: the picker handles its own keys, and IsDialogMessageW would eat them.
            exit = RunEditorModalMessageLoop(window_, false, [this]() noexcept { return !running_; });
        }

        if (window_ != nullptr && IsWindow(window_) != 0) {
            KillTimer(window_, kMaterialThumbnailTimerId);
            DestroyWindow(window_);
            window_ = nullptr;
        }
        RestoreOwnerFocus();
        // An app quit or a window destroyed under the pump is not a pick.
        return exit == EditorModalLoopExit::Completed ? result_ : AssetPickerResult{};
    }

private:
    [[nodiscard]] bool EnsureWindow() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        windowClass.lpfnWndProc = &AssetPickerWindow::WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = kMeshPickerClassName;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kMeshPickerClassName, L"Select Asset", WS_POPUP, 0, 0, DialogWidth(), DialogHeight(), owner_, nullptr, windowClass.hInstance, this);
        return window_ != nullptr;
    }

    [[nodiscard]] int DialogWidth() const noexcept {
        if (textureThumbnails_) {
            return kTextureDialogWidth;
        }
        return tileKind_ != AssetPickerTileKind::None ? kAssetGridDialogWidth : kListDialogWidth;
    }

    [[nodiscard]] int DialogHeight() const noexcept {
        if (textureThumbnails_) {
            return kTextureDialogHeight;
        }
        return tileKind_ != AssetPickerTileKind::None ? kAssetGridDialogHeight : kListDialogHeight;
    }

    void EnableOwner(bool enabled) const noexcept {
        if (owner_ != nullptr && IsWindow(owner_) != 0) {
            EnableWindow(owner_, enabled ? TRUE : FALSE);
        }
    }

    void RestoreOwnerFocus() const noexcept {
        if (owner_ == nullptr || IsWindow(owner_) == 0) {
            return;
        }
        EnableWindow(owner_, TRUE);
        ShowWindow(owner_, SW_SHOW);
        BringWindowToTop(owner_);
        SetActiveWindow(owner_);
        SetForegroundWindow(owner_);
        SetFocus(owner_);
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
        return Rect(kPad, kListHeaderHeight, client.right - kPad, client.bottom - kFooterHeight);
    }

    [[nodiscard]] int RowCount() const noexcept {
        return static_cast<int>(rows_.size()) + (allowClear_ ? 1 : 0);
    }

    [[nodiscard]] int AssetRowOffset() const noexcept {
        return allowClear_ ? 1 : 0;
    }

    [[nodiscard]] const AssetPickerRow* AssetAtRow(int row) const noexcept {
        const int assetIndex = row - AssetRowOffset();
        return assetIndex >= 0 && assetIndex < static_cast<int>(rows_.size())
            ? &rows_[static_cast<std::size_t>(assetIndex)]
            : nullptr;
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
        const AssetPickerRow* asset = AssetAtRow(row);
        result_.assetId = asset != nullptr ? asset->assetId : kb::assets::AssetId{};
        running_ = false;
        DestroyWindow(window_);
    }

    [[nodiscard]] RECT TextureSearchRect() const noexcept {
        const RECT client = Client();
        const int right = client.right - kTextureTileGap - (kTextureButtonWidth * 2) - kTextureTileGap;
        return Rect(kTextureTileGap, kTextureTileGap, right, kTextureTileGap + kTextureSearchHeight);
    }

    [[nodiscard]] RECT TextureAcceptRect() const noexcept {
        const RECT client = Client();
        const int left = client.right - kTextureTileGap - (kTextureButtonWidth * 2) - kTextureTileGap;
        return Rect(left, kTextureTileGap, left + kTextureButtonWidth, kTextureTileGap + kTextureSearchHeight);
    }

    [[nodiscard]] RECT TextureCancelRect() const noexcept {
        const RECT accept = TextureAcceptRect();
        return Rect(accept.right + kTextureTileGap, accept.top, accept.right + kTextureTileGap + kTextureButtonWidth, accept.bottom);
    }

    [[nodiscard]] RECT TextureViewportRect() const noexcept {
        const RECT client = Client();
        const int top = kTextureTileGap + kTextureHeaderHeight;
        return Rect(kTextureTileGap, top, client.right - kTextureTileGap, top + kTextureViewportHeight);
    }

    [[nodiscard]] std::vector<std::size_t> FilteredTextureRows() const {
        std::vector<std::size_t> indices;
        indices.reserve(rows_.size());
        for (std::size_t index = 0U; index < rows_.size(); ++index) {
            if (TextureRowMatchesQuery(rows_[index], textureQuery_)) {
                indices.push_back(index);
            }
        }
        return indices;
    }

    [[nodiscard]] int TextureContentHeight(const std::vector<std::size_t>& indices) const noexcept {
        if (indices.empty()) {
            return 0;
        }
        const int rowCount = (static_cast<int>(indices.size()) + kTextureColumns - 1) / kTextureColumns;
        return (rowCount * kTextureTileHeight) + ((rowCount - 1) * kTextureTileGap);
    }

    [[nodiscard]] int MaxTextureScroll() const {
        const std::vector<std::size_t> indices = FilteredTextureRows();
        return std::max(0, TextureContentHeight(indices) - kTextureViewportHeight);
    }

    [[nodiscard]] int TextureGridLeft(const RECT& viewport) const noexcept {
        const int gridWidth = (kTextureColumns * kTextureTileWidth) + ((kTextureColumns - 1) * kTextureTileGap);
        const int usableWidth = std::max(0, RectWidth(viewport) - kScrollbarWidth);
        return viewport.left + std::max(0, (usableWidth - gridWidth) / 2);
    }

    [[nodiscard]] int TextureTileAt(int x, int y) const {
        const RECT viewport = TextureViewportRect();
        if (!Contains(viewport, x, y)) {
            return -1;
        }
        const std::vector<std::size_t> indices = FilteredTextureRows();
        const int gridLeft = TextureGridLeft(viewport);
        const int localX = x - gridLeft;
        const int slotWidth = kTextureTileWidth + kTextureTileGap;
        if (localX < 0) {
            return -1;
        }
        const int column = localX / slotWidth;
        if (column < 0 || column >= kTextureColumns || (localX % slotWidth) >= kTextureTileWidth) {
            return -1;
        }
        const int localY = y - viewport.top + textureScrollOffset_;
        const int slotHeight = kTextureTileHeight + kTextureTileGap;
        if (localY < 0 || (localY % slotHeight) >= kTextureTileHeight) {
            return -1;
        }
        const int row = localY / slotHeight;
        const int textureIndex = (row * kTextureColumns) + column;
        return textureIndex >= 0 && textureIndex < static_cast<int>(indices.size()) ? textureIndex : -1;
    }

    bool AcceptTextureSelection() noexcept {
        if (!ValidAsset(selectedTextureAsset_)) {
            return false;
        }
        result_.accepted = true;
        result_.assetId = selectedTextureAsset_;
        running_ = false;
        DestroyWindow(window_);
        return true;
    }

    void PaintTextureButton(HDC dc, RECT rect, std::string_view label, bool enabled, bool hovered) const {
        const COLORREF fill = enabled
            ? (hovered ? Rgb(58, 64, 76) : Rgb(51, 55, 65))
            : Rgb(31, 33, 38);
        const COLORREF border = enabled ? Rgb(64, 120, 217) : Rgb(0, 0, 0);
        GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
        Text(dc, rect, label, enabled ? Rgb(209, 214, 224) : Rgb(128, 133, 145), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void PaintTextureTile(
        HDC dc,
        RECT rect,
        const AssetPickerRow& row,
        int tileIndex,
        bool selected,
        bool hovered) const {
        const COLORREF fill = selected || hovered ? Rgb(51, 55, 65) : Rgb(19, 20, 24);
        const COLORREF border = selected ? Rgb(64, 120, 217) : Rgb(0, 0, 0);
        GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
        if (selected) {
            GdiDrawing::DrawSharpFrame(dc, GdiDrawing::Inset(rect, 1), fill, Rgb(64, 120, 217));
        }

        const RECT image = Rect(rect.left + 6, rect.top + 6, rect.right - 6, rect.top + 110);
        GdiDrawing::DrawSharpFrame(dc, image, Rgb(0, 0, 0), Rgb(0, 0, 0));
        bool drewTexturePreview = false;
        if (assetManager_ != nullptr) {
            const kb::assets::AssetMetadata* metadata = assetManager_->Registry().Find(row.assetId);
            if (metadata != nullptr) {
                if (const EditorTexturePreviewImage* preview = EditorTexturePreviewService::PreviewFor(*metadata); preview != nullptr) {
                    EditorTexturePreviewService::DrawContain(dc, GdiDrawing::Inset(image, 1), *preview, false);
                    drewTexturePreview = true;
                }
            }
        }
        if (!drewTexturePreview) {
            HeroIconPainter::Draw(dc, GdiDrawing::Inset(image, 32), icon_, selected ? Rgb(64, 120, 217) : Rgb(128, 133, 145), 2);
        }

        RECT label = Rect(rect.left + 6, rect.top + 116, rect.right - 6, rect.bottom - 6);
        ScopedFont labelFont(12, FW_NORMAL);
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, Rgb(209, 214, 224));
        DrawTextA(dc, row.name.c_str(), static_cast<int>(row.name.size()), &label, DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_NOPREFIX);
        (void)tileIndex;
    }

    void PaintTextureScrollbar(HDC dc, RECT viewport, int contentHeight) const {
        const int maxScroll = std::max(0, contentHeight - kTextureViewportHeight);
        if (maxScroll <= 0) {
            return;
        }
        const RECT track = Rect(viewport.right - kScrollbarWidth, viewport.top, viewport.right - 3, viewport.bottom);
        GdiDrawing::FillRectColor(dc, track, Rgb(17, 18, 22));
        const int thumbHeight = std::clamp((RectHeight(track) * kTextureViewportHeight) / std::max(1, contentHeight), 28, RectHeight(track));
        const int travel = std::max(0, RectHeight(track) - thumbHeight);
        const int thumbTop = track.top + (travel * std::clamp(textureScrollOffset_, 0, maxScroll)) / std::max(1, maxScroll);
        GdiDrawing::FillRectColor(dc, Rect(track.left + 1, thumbTop, track.right - 1, thumbTop + thumbHeight), Rgb(83, 96, 113));
    }

    void PaintTextureBrowser(HDC dc) const {
        const RECT client = Client();
        GdiDrawing::DrawSharpFrame(dc, client, Rgb(30, 33, 39), Rgb(0, 0, 0));
        FillVerticalGradient(dc, GdiDrawing::Inset(client, 1), Rgb(30, 33, 39), Rgb(23, 25, 31));

        const RECT search = TextureSearchRect();
        GdiDrawing::DrawSharpFrame(dc, search, Rgb(17, 18, 22), textureSearchFocused_ ? Rgb(64, 120, 217) : Rgb(0, 0, 0));
        const std::string searchText = textureQuery_.empty()
            ? (textureSearchFocused_ ? std::string{ "|" } : std::string{ "Search textures" })
            : (textureSearchFocused_ ? textureQuery_ + "|" : textureQuery_);
        Text(dc, Rect(search.left + 10, search.top, search.right - 10, search.bottom),
            searchText,
            textureQuery_.empty() && !textureSearchFocused_ ? Rgb(128, 133, 145) : Rgb(209, 214, 224));

        PaintTextureButton(dc, TextureAcceptRect(), "Accept", ValidAsset(selectedTextureAsset_), hoveredRow_ == -3);
        PaintTextureButton(dc, TextureCancelRect(), "Cancel", true, hoveredRow_ == -4);

        const RECT viewport = TextureViewportRect();
        const std::vector<std::size_t> indices = FilteredTextureRows();
        const int contentHeight = TextureContentHeight(indices);
        const int maxScroll = std::max(0, contentHeight - kTextureViewportHeight);
        const int scroll = std::clamp(textureScrollOffset_, 0, maxScroll);
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, viewport.left, viewport.top, viewport.right - kScrollbarWidth, viewport.bottom);
        const int gridLeft = TextureGridLeft(viewport);
        for (std::size_t tile = 0U; tile < indices.size(); ++tile) {
            const int row = static_cast<int>(tile) / kTextureColumns;
            const int column = static_cast<int>(tile) % kTextureColumns;
            const int left = gridLeft + (column * (kTextureTileWidth + kTextureTileGap));
            const int top = viewport.top + (row * (kTextureTileHeight + kTextureTileGap)) - scroll;
            const RECT tileRect = Rect(left, top, left + kTextureTileWidth, top + kTextureTileHeight);
            if (tileRect.bottom < viewport.top || tileRect.top > viewport.bottom) {
                continue;
            }
            const AssetPickerRow& texture = rows_[indices[tile]];
            PaintTextureTile(
                dc,
                tileRect,
                texture,
                static_cast<int>(tile),
                texture.assetId.value == selectedTextureAsset_.value,
                hoveredRow_ == static_cast<int>(tile));
        }
        if (indices.empty()) {
            Text(dc, Rect(viewport.left, viewport.top + 8, viewport.right, viewport.top + 36), "No textures found", Rgb(128, 133, 145));
        }
        RestoreDC(dc, saved);
        PaintTextureScrollbar(dc, viewport, contentHeight);
    }

    // --- Asset tile grid (mesh or material previews) ---

    [[nodiscard]] RECT TileGridViewportRect() const noexcept {
        const RECT client = Client();
        return Rect(kPad, kListHeaderHeight, client.right - kPad, client.bottom - kFooterHeight);
    }

    [[nodiscard]] int TileGridLeft(const RECT& viewport) const noexcept {
        const int gridWidth = (kAssetTileColumns * kAssetTileWidth) + ((kAssetTileColumns - 1) * kAssetTileGap);
        const int usableWidth = std::max(0, RectWidth(viewport) - kScrollbarWidth);
        return viewport.left + std::max(0, (usableWidth - gridWidth) / 2);
    }

    [[nodiscard]] int TileGridContentHeight() const noexcept {
        const int count = RowCount();
        const int gridRows = (count + kAssetTileColumns - 1) / kAssetTileColumns;
        return gridRows <= 0 ? 0 : (gridRows * kAssetTileHeight) + ((gridRows - 1) * kAssetTileGap);
    }

    [[nodiscard]] int MaxTileGridScroll() const noexcept {
        return std::max(0, TileGridContentHeight() - RectHeight(TileGridViewportRect()));
    }

    [[nodiscard]] RECT TileGridRect(const RECT& viewport, int index) const noexcept {
        const int row = index / kAssetTileColumns;
        const int column = index % kAssetTileColumns;
        const int left = TileGridLeft(viewport) + (column * (kAssetTileWidth + kAssetTileGap));
        const int top = viewport.top + (row * (kAssetTileHeight + kAssetTileGap)) - scrollOffset_;
        return Rect(left, top, left + kAssetTileWidth, top + kAssetTileHeight);
    }

    [[nodiscard]] int TileGridAt(int x, int y) const noexcept {
        const RECT viewport = TileGridViewportRect();
        if (!Contains(viewport, x, y)) {
            return -1;
        }
        for (int index = 0; index < RowCount(); ++index) {
            if (Contains(TileGridRect(viewport, index), x, y)) {
                return index;
            }
        }
        return -1;
    }

    // The currently assigned asset provides the initial highlight. A clearable picker
    // highlights its None tile when no asset is assigned.
    [[nodiscard]] int CurrentTile() const noexcept {
        for (int index = 0; index < RowCount(); ++index) {
            const AssetPickerRow* asset = AssetAtRow(index);
            if (asset != nullptr && asset->assetId.value == currentAsset_.value) {
                return index;
            }
        }
        return allowClear_ ? 0 : -1;
    }

    // The highlighted tile: the user's click selection, or the current asset.
    [[nodiscard]] int SelectedTile() const noexcept {
        return selectedTile_ >= 0 ? selectedTile_ : CurrentTile();
    }

    [[nodiscard]] const ProjectFilesMaterialPreviewImage* MaterialPreviewAtRow(int row) const {
        if (tileKind_ != AssetPickerTileKind::Material || assetManager_ == nullptr) {
            return nullptr;
        }
        const AssetPickerRow* asset = AssetAtRow(row);
        if (asset == nullptr) {
            return nullptr;
        }
        if (const auto found = materialPreviews_.find(asset->assetId.value); found != materialPreviews_.end()) {
            return &found->second;
        }
        const kb::assets::AssetMetadata* metadata = assetManager_->Registry().Find(asset->assetId);
        if (metadata == nullptr) {
            return nullptr;
        }
        const ProjectFilesMaterialPreviewStyle style = ProjectFilesMaterialPreviewThumbnailModel::StyleFromAsset(
            *metadata,
            assetManager_,
            &MaterialPreviewTextureAverageColor);
        auto [entry, inserted] = materialPreviews_.emplace(
            asset->assetId.value,
            ProjectFilesMaterialPreviewThumbnailModel::RenderImage(
                kAssetTilePreviewHeight,
                kAssetTilePreviewHeight,
                style,
                false));
        static_cast<void>(inserted);
        return &entry->second;
    }

    void PaintTileGridItem(HDC dc, RECT rect, int tileIndex, bool selected, bool hovered) const {
        const COLORREF fill = selected || hovered ? Rgb(51, 55, 65) : Rgb(19, 20, 24);
        const COLORREF border = selected ? Color(theme_.accent) : Rgb(0, 0, 0);
        GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
        if (selected) {
            GdiDrawing::DrawSharpFrame(dc, GdiDrawing::Inset(rect, 1), fill, Color(theme_.accent));
        }

        const RECT image = Rect(rect.left + 6, rect.top + 6, rect.right - 6, rect.top + 6 + kAssetTilePreviewHeight);
        GdiDrawing::DrawSharpFrame(dc, image, Rgb(10, 11, 14), Rgb(0, 0, 0));

        bool drewPreview = false;
        const AssetPickerRow* asset = AssetAtRow(tileIndex);
        if (asset != nullptr && assetManager_ != nullptr && tileKind_ == AssetPickerTileKind::Mesh) {
            const kb::assets::AssetMetadata* metadata = assetManager_->Registry().Find(asset->assetId);
            if (metadata != nullptr) {
                if (const EditorMeshThumbnailImage* preview = EditorMeshPreviewCache().PreviewFor(*assetManager_, *metadata, EditorMeshPreviewSettings{}); preview != nullptr) {
                    DrawMeshThumbnail(dc, GdiDrawing::Inset(image, 1), *preview);
                    drewPreview = true;
                }
            }
        }
        if (asset != nullptr && tileKind_ == AssetPickerTileKind::Material) {
            const RECT target = GdiDrawing::Inset(image, 1);
            const kb::assets::AssetMetadata* metadata = assetManager_ != nullptr
                ? assetManager_->Registry().Find(asset->assetId)
                : nullptr;
            if (metadata != nullptr) {
                if (const EditorMaterialThumbnailImage* rendered = EditorMaterialThumbnailCache().ThumbnailFor(
                        *metadata,
                        std::min(RectWidth(target), RectHeight(target)));
                    rendered != nullptr) {
                    DrawRenderedMaterialThumbnail(dc, target, *rendered);
                    drewPreview = true;
                }
            }
            if (!drewPreview) {
                const ProjectFilesMaterialPreviewImage* preview = MaterialPreviewAtRow(tileIndex);
                if (preview != nullptr && preview->width > 0 && preview->height > 0 && !preview->bgra.empty()) {
                    BITMAPINFO info{};
                    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    info.bmiHeader.biWidth = preview->width;
                    info.bmiHeader.biHeight = -preview->height;
                    info.bmiHeader.biPlanes = 1;
                    info.bmiHeader.biBitCount = 32;
                    info.bmiHeader.biCompression = BI_RGB;
                    static_cast<void>(StretchDIBits(
                        dc,
                        target.left,
                        target.top,
                        RectWidth(target),
                        RectHeight(target),
                        0,
                        0,
                        preview->width,
                        preview->height,
                        preview->bgra.data(),
                        &info,
                        DIB_RGB_COLORS,
                        SRCCOPY));
                    drewPreview = true;
                }
            }
        }
        if (!drewPreview) {
            // The "None" tile, or an asset whose preview could not be rasterised.
            const COLORREF iconColor = selected ? Color(theme_.accent) : Rgb(128, 133, 145);
            HeroIconPainter::Draw(dc, GdiDrawing::Inset(image, 42), icon_, iconColor, 2);
        }

        const std::string name = asset != nullptr ? asset->name : std::string{ "None" };
        RECT label = Rect(rect.left + 8, rect.top + kAssetTilePreviewHeight + 12, rect.right - 8, rect.bottom - 6);
        ScopedFont labelFont(12, FW_SEMIBOLD);
        const ScopedGdiObject selectedFont(dc, labelFont.handle);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, selected ? Color(theme_.textPrimary) : Rgb(209, 214, 224));
        DrawTextA(dc, name.c_str(), static_cast<int>(name.size()), &label, DT_CENTER | DT_TOP | DT_END_ELLIPSIS | DT_NOPREFIX | DT_SINGLELINE);
    }

    void PaintTileGridScrollbar(HDC dc, RECT viewport) const {
        const int maxScroll = MaxTileGridScroll();
        if (maxScroll <= 0) {
            return;
        }
        const int contentHeight = TileGridContentHeight();
        const RECT track = Rect(viewport.right - kScrollbarWidth, viewport.top, viewport.right - 3, viewport.bottom);
        GdiDrawing::FillRectColor(dc, track, Rgb(17, 18, 22));
        const int thumbHeight = std::clamp((RectHeight(track) * RectHeight(viewport)) / std::max(1, contentHeight), 28, RectHeight(track));
        const int travel = std::max(0, RectHeight(track) - thumbHeight);
        const int thumbTop = track.top + (travel * std::clamp(scrollOffset_, 0, maxScroll)) / std::max(1, maxScroll);
        GdiDrawing::FillRectColor(dc, Rect(track.left + 1, thumbTop, track.right - 1, thumbTop + thumbHeight), Rgb(83, 96, 113));
    }

    void PaintTileGrid(HDC dc) const {
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

        const RECT viewport = TileGridViewportRect();
        const int selectedTile = SelectedTile();
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, viewport.left, viewport.top, viewport.right - kScrollbarWidth, viewport.bottom);
        for (int index = 0; index < RowCount(); ++index) {
            const RECT tile = TileGridRect(viewport, index);
            if (tile.bottom < viewport.top || tile.top > viewport.bottom) {
                continue;
            }
            PaintTileGridItem(dc, tile, index, index == selectedTile, hoveredRow_ == index);
        }
        if (RowCount() == 0) {
            const char* message = tileKind_ == AssetPickerTileKind::Material
                ? "No material assets found"
                : "No mesh assets found";
            Text(dc, Rect(viewport.left, viewport.top + 12, viewport.right, viewport.top + 40),
                message, Color(theme_.textSecondary), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        RestoreDC(dc, saved);
        PaintTileGridScrollbar(dc, viewport);

        Text(dc, Rect(kPad, client.bottom - 32, client.right - kPad, client.bottom - 10), "Click to select. Double-click or Enter to assign. Esc closes.", Color(theme_.textSecondary));
    }

    void Paint(HDC dc) const {
        if (textureThumbnails_) {
            PaintTextureBrowser(dc);
            return;
        }
        if (tileKind_ != AssetPickerTileKind::None) {
            PaintTileGrid(dc);
            return;
        }

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
        int next = -1;
        if (textureThumbnails_) {
            if (Contains(TextureAcceptRect(), x, y)) {
                next = -3;
            } else if (Contains(TextureCancelRect(), x, y)) {
                next = -4;
            } else if (Contains(TextureSearchRect(), x, y)) {
                next = -5;
            } else {
                next = TextureTileAt(x, y);
            }
        } else if (tileKind_ != AssetPickerTileKind::None) {
            next = Contains(CloseButton(), x, y) ? -2 : TileGridAt(x, y);
        } else {
            next = Contains(CloseButton(), x, y) ? -2 : RowAt(x, y);
        }
        if (next != hoveredRow_) {
            hoveredRow_ = next;
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    void Scroll(int delta) {
        if (textureThumbnails_) {
            const int next = std::clamp(textureScrollOffset_ + delta, 0, MaxTextureScroll());
            if (next != textureScrollOffset_) {
                textureScrollOffset_ = next;
                InvalidateRect(window_, nullptr, FALSE);
            }
            return;
        }

        const int maxScroll = tileKind_ != AssetPickerTileKind::None ? MaxTileGridScroll() : MaxScroll();
        const int next = std::clamp(scrollOffset_ + delta, 0, maxScroll);
        if (next != scrollOffset_) {
            scrollOffset_ = next;
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    void HandleTextureLeftButtonDown(int x, int y) {
        pressedTextureTarget_ = -1;
        if (Contains(TextureSearchRect(), x, y)) {
            textureSearchFocused_ = true;
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        if (Contains(TextureCancelRect(), x, y)) {
            pressedTextureTarget_ = -4;
            SetCapture(window_);
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        if (Contains(TextureAcceptRect(), x, y)) {
            pressedTextureTarget_ = -3;
            SetCapture(window_);
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        const int tile = TextureTileAt(x, y);
        if (tile >= 0) {
            pressedTextureTarget_ = tile;
            SetCapture(window_);
            textureSearchFocused_ = false;
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        textureSearchFocused_ = false;
        InvalidateRect(window_, nullptr, FALSE);
    }

    void HandleTextureLeftButtonUp(int x, int y) {
        if (pressedTextureTarget_ == -1) {
            return;
        }
        if (GetCapture() == window_) {
            ReleaseCapture();
        }

        const int pressed = pressedTextureTarget_;
        pressedTextureTarget_ = -1;
        const int released = Contains(TextureAcceptRect(), x, y)
            ? -3
            : Contains(TextureCancelRect(), x, y)
                ? -4
                : TextureTileAt(x, y);
        if (pressed != released) {
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }

        if (released == -4) {
            running_ = false;
            DestroyWindow(window_);
            return;
        }
        if (released == -3) {
            static_cast<void>(AcceptTextureSelection());
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        if (released >= 0) {
            const std::vector<std::size_t> indices = FilteredTextureRows();
            if (released < static_cast<int>(indices.size())) {
                const kb::assets::AssetId assetId = rows_[indices[static_cast<std::size_t>(released)]].assetId;
                if (assetId.value == selectedTextureAsset_.value) {
                    static_cast<void>(AcceptTextureSelection());
                    return;
                }
                selectedTextureAsset_ = assetId;
                InvalidateRect(window_, nullptr, FALSE);
            }
        }
    }

    bool HandleTextureKeyDown(WPARAM key) {
        if (!textureThumbnails_) {
            return false;
        }
        switch (key) {
        case VK_ESCAPE:
            running_ = false;
            DestroyWindow(window_);
            return true;
        case VK_RETURN:
            static_cast<void>(AcceptTextureSelection());
            return true;
        case VK_BACK:
            if (textureSearchFocused_ && !textureQuery_.empty()) {
                textureQuery_.pop_back();
                textureScrollOffset_ = 0;
                InvalidateRect(window_, nullptr, FALSE);
            }
            return true;
        default:
            return textureSearchFocused_;
        }
    }

    bool HandleTextureChar(WPARAM ch) {
        if (!textureThumbnails_ || !textureSearchFocused_) {
            return false;
        }
        if (ch >= 32 && ch < 127 && textureQuery_.size() < 96U) {
            textureQuery_.push_back(static_cast<char>(ch));
            textureScrollOffset_ = 0;
            InvalidateRect(window_, nullptr, FALSE);
            return true;
        }
        return ch == 8 || ch == 13;
    }

    void TickMaterialThumbnailRenderer() {
        if (window_ == nullptr || sceneContext_ == nullptr || sceneViewport_ == nullptr) {
            return;
        }
        EditorMaterialThumbnailService& thumbnails = EditorMaterialThumbnailCache();
        if (thumbnails.HasPendingWork()) {
            static_cast<void>(sceneContext_->PumpMaterialGraphCookResults());
            sceneViewport_->SetGraphShaderCacheRoot(sceneContext_->GraphShaderCacheRoot());
            const RECT client = Client();
            const RECT staging{
                std::max(client.left, client.right - 8),
                std::max(client.top, client.bottom - 8),
                client.right,
                client.bottom,
            };
            sceneViewport_->BeginPaintLayout(window_);
            thumbnails.Tick(*sceneContext_, *sceneViewport_, window_, staging);
            sceneViewport_->EndPaintLayout();
        }
        const std::uint64_t revision = thumbnails.Revision();
        if (revision != lastMaterialThumbnailRevision_) {
            lastMaterialThumbnailRevision_ = revision;
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
                // Double-buffer: render the whole dialog into an off-screen bitmap
                // and blit once, so frequent hover repaints don't flicker.
                RECT client{};
                GetClientRect(window, &client);
                const int width = client.right - client.left;
                const int height = client.bottom - client.top;
                HDC memDc = CreateCompatibleDC(dc);
                HBITMAP memBitmap = CreateCompatibleBitmap(dc, width, height);
                auto* oldBitmap = static_cast<HBITMAP>(SelectObject(memDc, memBitmap));
                picker->Paint(memDc);
                BitBlt(dc, 0, 0, width, height, memDc, 0, 0, SRCCOPY);
                SelectObject(memDc, oldBitmap);
                DeleteObject(memBitmap);
                DeleteDC(memDc);
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
                if (picker->textureThumbnails_) {
                    picker->HandleTextureLeftButtonDown(x, y);
                    return 0;
                }
                if (Contains(picker->CloseButton(), x, y)) {
                    picker->running_ = false;
                    DestroyWindow(window);
                    return 0;
                }
                if (picker->tileKind_ != AssetPickerTileKind::None) {
                    // Single click selects (highlights) a tile; a double click
                    // (WM_LBUTTONDBLCLK) confirms the assignment.
                    const int tile = picker->TileGridAt(x, y);
                    if (tile >= 0 && tile != picker->selectedTile_) {
                        picker->selectedTile_ = tile;
                        InvalidateRect(window, nullptr, FALSE);
                    }
                    return 0;
                }
                picker->AcceptRow(picker->RowAt(x, y));
                return 0;
            }
            break;
        case WM_LBUTTONDBLCLK:
            if (picker != nullptr && picker->tileKind_ != AssetPickerTileKind::None) {
                const int tile = picker->TileGridAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                if (tile >= 0) {
                    picker->AcceptRow(tile);
                }
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (picker != nullptr && picker->textureThumbnails_) {
                picker->HandleTextureLeftButtonUp(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
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
            if (picker != nullptr && picker->HandleTextureKeyDown(wparam)) {
                return 0;
            }
            if (picker != nullptr && picker->tileKind_ != AssetPickerTileKind::None && wparam == VK_RETURN) {
                picker->AcceptRow(picker->SelectedTile());
                return 0;
            }
            if (picker != nullptr && wparam == VK_ESCAPE) {
                picker->running_ = false;
                DestroyWindow(window);
                return 0;
            }
            break;
        case WM_CHAR:
            if (picker != nullptr && picker->HandleTextureChar(wparam)) {
                return 0;
            }
            break;
        case WM_TIMER:
            if (picker != nullptr && wparam == kMaterialThumbnailTimerId) {
                picker->TickMaterialThumbnailRenderer();
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
                KillTimer(window, kMaterialThumbnailTimerId);
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
    kb::assets::AssetId selectedTextureAsset_{};
    std::string title_;
    std::string description_;
    std::string clearDescription_;
    HeroIconKind icon_ = HeroIconKind::Cube;
    const kb::assets::AssetManager* assetManager_ = nullptr;
    bool textureThumbnails_ = false;
    AssetPickerTileKind tileKind_ = AssetPickerTileKind::None;
    bool allowClear_ = true;
    EditorSceneContext* sceneContext_ = nullptr;
    EditorSceneBgfxViewport* sceneViewport_ = nullptr;
    AssetPickerResult result_{};
    bool running_ = true;
    int hoveredRow_ = -1;
    int scrollOffset_ = 0;
    int selectedTile_ = -1;
    int pressedTextureTarget_ = -1;
    int textureScrollOffset_ = 0;
    bool textureSearchFocused_ = true;
    std::string textureQuery_;
    mutable std::unordered_map<std::uint64_t, ProjectFilesMaterialPreviewImage> materialPreviews_;
    std::uint64_t lastMaterialThumbnailRevision_ = 0U;
};

} // namespace

EditorMeshAssetPickerDialog::Result EditorMeshAssetPickerDialog::Show(
    HWND owner,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId currentMesh) {
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    AssetPickerWindow window{
        theme,
        BuildMeshRows(sceneContext),
        currentMesh,
        "Select Mesh",
        "Choose a mesh asset for the selected Mesh Renderer.",
        "Clear Mesh Renderer mesh",
        HeroIconKind::Cube,
        &manager,
        false, // textureThumbnails
        AssetPickerTileKind::Mesh,
    };
    const AssetPickerResult result = window.Show(owner);
    return EditorMeshAssetPickerDialog::Result{ .accepted = result.accepted, .assetId = result.assetId };
}

EditorMaterialAssetPickerDialog::Result EditorMaterialAssetPickerDialog::Show(
    HWND owner,
    const EditorTheme& theme,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    kb::assets::AssetId currentMaterial,
    bool allowClear) {
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    AssetPickerWindow window{
        theme,
        BuildMaterialRows(sceneContext),
        currentMaterial,
        "Select Material",
        "Choose a material asset from this project.",
        "Clear material assignment",
        HeroIconKind::AdjustmentsHorizontal,
        &manager,
        false,
        AssetPickerTileKind::Material,
        allowClear,
        &sceneContext,
        &sceneViewport,
    };
    const AssetPickerResult result = window.Show(owner);
    return EditorMaterialAssetPickerDialog::Result{ .accepted = result.accepted, .assetId = result.assetId };
}

EditorAnimatorControllerAssetPickerDialog::Result EditorAnimatorControllerAssetPickerDialog::Show(
    HWND owner,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId currentController) {
    AssetPickerWindow window{
        theme,
        BuildAnimatorControllerRows(sceneContext),
        currentController,
        "Select Animator Controller",
        "Choose an Animator Controller asset from this project.",
        "Clear Animator Controller",
        HeroIconKind::Play,
    };
    const AssetPickerResult result = window.Show(owner);
    return EditorAnimatorControllerAssetPickerDialog::Result{
        .accepted = result.accepted,
        .assetId = result.assetId,
    };
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
