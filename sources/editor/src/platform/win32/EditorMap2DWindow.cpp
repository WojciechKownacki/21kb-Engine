#include "platform/win32/EditorMap2DWindow.hpp"

#if defined(_WIN32)
#include "Map2DEditing.hpp"
#include "Map2DView.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/Map2DAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneObject.hpp"
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "platform/win32/EditorTextEntryDialog.hpp"
#include "platform/win32/EditorMap2DForm.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "scene/EditorMap2DService.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorSceneCommandController.hpp"
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#include <objidl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windowsx.h>

#if !defined(EM_SETCUEBANNER)
#define EM_SETCUEBANNER 0x1501
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

using kb::assets::Map2DAsset;
using kb::assets::Map2DSprite;
using kb::assets::Map2DTile;
using kb::assets::Map2DTileKey;
using kb::world2d::Map2DBorderPiece;
using kb::world2d::Map2DGroundBrush;
using kb::world2d::Map2DObjectBrush;
using kb::world2d::Map2DRect;

constexpr wchar_t kWindowClass[] = L"KBEditorWorld2DMapEditor";

constexpr int kStatusHeight = 26;

constexpr int kPad = 8;
constexpr int kRowHeight = 28;
constexpr int kTileSize = 46;
constexpr int kTileGap = 4;
constexpr UINT_PTR kPublishTimer = 1U;
constexpr UINT_PTR kSearchEditId = 101U;
constexpr int kListRow = 26;
constexpr int kListRows = 14;
constexpr std::array<int, 12U> kZoomLevels{ 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192 };
constexpr int kNormalZoom = 32;
constexpr int kBoardCell = 62;
constexpr int kBoardGap = 3;
constexpr int kBoardSide = 5 * kBoardCell + 4 * kBoardGap;
constexpr int kMaximumBrushSize = 15;
constexpr std::size_t kRecentCount = 24U;
constexpr int kTutorialSteps = 5;
constexpr std::string_view kAllCategory = "*all";
constexpr std::string_view kRecentCategory = "*recent";

constexpr COLORREF kBackground = RGB(9, 12, 18);
constexpr COLORREF kPanel = RGB(18, 23, 32);
constexpr COLORREF kToolbar = RGB(16, 21, 30);
constexpr COLORREF kButton = RGB(29, 35, 47);
constexpr COLORREF kButtonHot = RGB(44, 52, 68);
constexpr COLORREF kButtonActive = RGB(52, 88, 150);
constexpr COLORREF kBorder = RGB(49, 58, 75);
constexpr COLORREF kText = RGB(235, 238, 247);
constexpr COLORREF kTextDim = RGB(150, 160, 182);
constexpr COLORREF kAccent = RGB(110, 168, 255);
constexpr COLORREF kGroundMark = RGB(120, 210, 120);
constexpr COLORREF kObjectMark = RGB(240, 190, 90);
constexpr COLORREF kWarning = RGB(255, 190, 110);
constexpr COLORREF kSelection = RGB(255, 220, 90);
constexpr COLORREF kClientBox = RGB(120, 230, 140);
constexpr COLORREF kVoid = RGB(4, 5, 8);
constexpr COLORREF kMapBase = RGB(13, 15, 21);
constexpr COLORREF kGridLine = RGB(36, 42, 56);
constexpr COLORREF kTile = RGB(11, 14, 20);

enum class Tool : std::uint8_t { Draw, Select, Eraser, Fill, Rectangle, Picker, Collision };
enum class Panel : std::uint8_t { Palette, Sheet };
enum class EntryKind : std::uint8_t { Ground, Floor, Item, Object };
enum class Control : std::uint8_t {
    None, Save, Undo, Redo, ToolDraw, ToolSelect, ToolEraser, ToolFill, ToolRectangle, ToolPicker, BrushSize,
    FloorDown, FloorUp, Automagic, Grid, ClientBox, ViewMenu, MapSize, ShowInScene, Help,
    TypeList, CategoryList, CategoryItem, PaletteTile, EditBrush, MakeGround, NewObject, OpenSpritesFolder, Reload, ImportImages, PaletteMenu,
    Back, RenameGround, ZOrderDown, ZOrderUp, BoardCell, BoardDone, DeleteGround,
    CellSize, JoinSprites, SaveObject, DeleteObject,
    TutorialBack, TutorialNext, TutorialSkip,
    AnimationCell, AnimationFrame, AnimationApply, AnimationCancel, AnimationRemove, AnimationLeft, AnimationRight,
    AnimationSpeed, AnimationPlay, AnimationClear, AnimationPrevious, AnimationNext,
};

enum Modifier : std::uint8_t { kCtrl = 1U, kShift = 2U, kAlt = 4U };

struct ControlBox {
    RECT rect{};
    Control id = Control::None;
    int param = 0;
    std::wstring label;
    bool active = false;
    bool enabled = true;
    std::wstring tip;
};

struct SheetImage {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint32_t> pixels;
    bool loaded = false;
};

// One tile of the palette: a ground brush, a floor tile, an item, or an object of several tiles.
struct PaletteEntry {
    EntryKind kind = EntryKind::Item;
    std::string key;
    std::string name;
    std::string category;
    std::string texture;
    std::uint32_t cellSize = 32U;
    std::uint32_t sheetColumns = 1U;
    std::uint32_t firstCell = 0U;
    std::uint32_t columns = 1U;
    std::uint32_t rows = 1U;
    // The non-empty cells of the block, relative to its top-left one.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> cells{ { 0U, 0U } };
    // Defined in a tileset (a ground or a named object), so it can be edited.
    bool defined = false;
    std::int32_t zOrder = 0;
    bool hasBorders = false;
    std::string prefab;
    std::shared_ptr<const Map2DAsset> stamp;

    [[nodiscard]] std::uint32_t CellAt(std::uint32_t column, std::uint32_t row) const noexcept {
        return (firstCell / sheetColumns + row) * sheetColumns + firstCell % sheetColumns + column;
    }
};

struct SheetLayout {
    RECT rect{};
    std::string texture;
    std::uint32_t cellSize = 32U;
    std::uint32_t columns = 0U;
    std::uint32_t rows = 0U;
    int cellPixels = 32;
    bool visible = false;
};

// A square of the ground editor's 5x5 board: a look of the ground (0 is its main sprite) or an edge piece.
struct BoardSlot {
    int variant = -1;
    int piece = -1;
};

// The ground lies as a plus in the middle of the board, so every edge piece shows in the place it takes
// around it on a map; the board's four corners take nothing.
[[nodiscard]] constexpr std::array<BoardSlot, 25U> BoardSlots() noexcept {
    using P = kb::world2d::Map2DBorderPiece;
    constexpr auto edge = [](P piece) { return BoardSlot{ -1, static_cast<int>(piece) }; };
    constexpr auto look = [](int variant) { return BoardSlot{ variant, -1 }; };
    return { {
        {}, edge(P::CornerSE), edge(P::S), edge(P::CornerSW), {},
        edge(P::CornerSE), edge(P::DiagonalSE), look(1), edge(P::DiagonalSW), edge(P::CornerSW),
        edge(P::E), look(2), look(0), look(3), edge(P::W),
        edge(P::CornerNE), edge(P::DiagonalNE), look(4), edge(P::DiagonalNW), edge(P::CornerNW),
        {}, edge(P::CornerNE), edge(P::N), edge(P::CornerNW), {},
    } };
}

// Tiles copied from the map, relative to the copied rectangle's top-left corner.
struct Clipboard {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::pair<std::pair<std::uint32_t, std::uint32_t>, Map2DTile>> tiles;
};

[[nodiscard]] std::wstring Wide(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring output(static_cast<std::size_t>(std::max(size, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), size);
    return output;
}

[[nodiscard]] std::string Narrow(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<std::size_t>(std::max(size, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), size, nullptr, nullptr);
    return output;
}

// The editor's text in the user's language (EditorText), as the window's wide strings.
[[nodiscard]] std::wstring T(std::string_view key, std::string_view english) {
    return Wide(EditorText(key, english));
}

[[nodiscard]] std::wstring TF(std::string_view key, std::string_view english, std::initializer_list<std::pair<std::string_view, std::string>> values) {
    return Wide(EditorLocalization::Format(key, english, values));
}

[[nodiscard]] std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] std::wstring Lower(std::wstring text) {
    std::ranges::transform(text, text.begin(), [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return text;
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] int Width(const RECT& rect) noexcept { return rect.right - rect.left; }
[[nodiscard]] int Height(const RECT& rect) noexcept { return rect.bottom - rect.top; }

[[nodiscard]] int FloorDivide(int value, int divisor) noexcept {
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

[[nodiscard]] bool LoadImagePixels(const std::filesystem::path& path, SheetImage& image) {
    HeroIconGdiplusRuntime::EnsureStarted();
    Gdiplus::Bitmap bitmap(path.wstring().c_str());
    if (bitmap.GetLastStatus() != Gdiplus::Ok || bitmap.GetWidth() == 0U || bitmap.GetHeight() == 0U) return false;
    Gdiplus::Rect bounds(0, 0, static_cast<INT>(bitmap.GetWidth()), static_cast<INT>(bitmap.GetHeight()));
    Gdiplus::BitmapData data{};
    if (bitmap.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) return false;
    image.width = bitmap.GetWidth();
    image.height = bitmap.GetHeight();
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height);
    for (std::uint32_t y = 0U; y < image.height; ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.Scan0) + static_cast<std::ptrdiff_t>(y) * data.Stride);
        std::copy_n(row, image.width, image.pixels.begin() + static_cast<std::ptrdiff_t>(y) * image.width);
    }
    static_cast<void>(bitmap.UnlockBits(&data));
    return true;
}

[[nodiscard]] std::optional<CLSID> PngEncoder() {
    UINT count = 0U;
    UINT bytes = 0U;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0U) return std::nullopt;
    std::vector<std::uint8_t> buffer(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) return std::nullopt;
    for (UINT index = 0U; index < count; ++index) {
        if (std::wstring_view{ encoders[index].MimeType } == L"image/png") return encoders[index].Clsid;
    }
    return std::nullopt;
}

// Where the editor keeps that the tutorial was seen, next to its preferences.
[[nodiscard]] std::filesystem::path TutorialSeenFile() {
    return EditorLocalization::PreferencesFile().parent_path() / "Map2DTutorial.seen";
}

// Next to kb_editor.exe: the tutorial's pictures.
[[nodiscard]] std::filesystem::path TutorialImageFile(int step) {
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    return std::filesystem::path{ module }.parent_path() / "Content" / "EditorShell" / "Map2D" / ("tutorial-" + std::to_string(step + 1) + ".png");
}

class Surface {
public:
    Surface() = default;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    ~Surface() { Release(); }

    [[nodiscard]] bool Ensure(int width, int height) {
        if (memory_ != nullptr && width == width_ && height == height_) return true;
        Release();
        width_ = std::max(width, 1);
        height_ = std::max(height, 1);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width_;
        info.bmiHeader.biHeight = -height_;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        memory_ = CreateCompatibleDC(nullptr);
        void* bits = nullptr;
        bitmap_ = memory_ != nullptr ? CreateDIBSection(memory_, &info, DIB_RGB_COLORS, &bits, nullptr, 0U) : nullptr;
        if (memory_ == nullptr || bitmap_ == nullptr) {
            Release();
            return false;
        }
        previous_ = SelectObject(memory_, bitmap_);
        pixels_ = static_cast<std::uint32_t*>(bits);
        return true;
    }

    [[nodiscard]] HDC Dc() const noexcept { return memory_; }
    [[nodiscard]] HBITMAP Bitmap() const noexcept { return bitmap_; }
    [[nodiscard]] int Width() const noexcept { return width_; }
    [[nodiscard]] int Height() const noexcept { return height_; }

    // Nearest-neighbour copy of a source rectangle into a destination rectangle, alpha blended
    // (alpha 0..256 scales the source's own alpha), clipped to `clip`; `shade` 0..256 darkens it.
    void Blit(const RECT& clip, int dx, int dy, int dw, int dh, const SheetImage& image,
        std::uint32_t sx, std::uint32_t sy, std::uint32_t sw, std::uint32_t sh, std::uint32_t alpha, std::uint32_t shade = 256U,
        Map2DSprite style = {}) noexcept {
        if (pixels_ == nullptr || dw <= 0 || dh <= 0 || sw == 0U || sh == 0U || !image.loaded) return;
        const int x0 = std::max({ dx, static_cast<int>(clip.left), 0 });
        const int y0 = std::max({ dy, static_cast<int>(clip.top), 0 });
        const int x1 = std::min({ dx + dw, static_cast<int>(clip.right), width_ });
        const int y1 = std::min({ dy + dh, static_cast<int>(clip.bottom), height_ });
        for (int y = y0; y < y1; ++y) {
            std::uint32_t* target = pixels_ + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
            for (int x = x0; x < x1; ++x) {
                const auto uv = kb::assets::Map2DSpriteUV(style, (static_cast<float>(x - dx) + 0.5F) / static_cast<float>(dw),
                    (static_cast<float>(y - dy) + 0.5F) / static_cast<float>(dh));
                const auto sourceX = sx + std::min(sw - 1U, static_cast<std::uint32_t>(uv[0] * static_cast<float>(sw)));
                const auto sourceY = sy + std::min(sh - 1U, static_cast<std::uint32_t>(uv[1] * static_cast<float>(sh)));
                if (sourceX >= image.width || sourceY >= image.height) continue;
                std::uint32_t pixel = image.pixels[static_cast<std::size_t>(sourceY) * image.width + sourceX];
                const std::uint32_t a = (((pixel >> 24U) * alpha) >> 8U) * (style.tint & 255U) / 255U;
                const auto tint = [&](unsigned shift, unsigned tintShift) { return (((pixel >> shift) & 255U) * ((style.tint >> tintShift) & 255U) / 255U) << shift; };
                pixel = tint(16U, 24U) | tint(8U, 16U) | tint(0U, 8U);
                if (a == 0U) continue;
                if (shade < 256U) {
                    const auto dim = [shade](std::uint32_t value, std::uint32_t shift) { return ((((value >> shift) & 0xFFU) * shade) >> 8U) << shift; };
                    pixel = dim(pixel, 16U) | dim(pixel, 8U) | dim(pixel, 0U);
                }
                if (a >= 255U) {
                    target[x] = pixel | 0xFF000000U;
                    continue;
                }
                const std::uint32_t back = target[x];
                const auto mix = [a](std::uint32_t front, std::uint32_t behind, std::uint32_t shift) {
                    const std::uint32_t f = (front >> shift) & 0xFFU;
                    const std::uint32_t b = (behind >> shift) & 0xFFU;
                    return ((f * a + b * (255U - a)) / 255U) << shift;
                };
                target[x] = 0xFF000000U | mix(pixel, back, 16U) | mix(pixel, back, 8U) | mix(pixel, back, 0U);
            }
        }
    }

    // Darkens everything outside `keep` (the tutorial's spotlight).
    void Shade(const RECT& area, std::uint32_t shade) noexcept {
        if (pixels_ == nullptr) return;
        for (int y = std::max(0, static_cast<int>(area.top)); y < std::min(height_, static_cast<int>(area.bottom)); ++y) {
            std::uint32_t* row = pixels_ + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
            for (int x = std::max(0, static_cast<int>(area.left)); x < std::min(width_, static_cast<int>(area.right)); ++x) {
                const std::uint32_t pixel = row[x];
                const auto dim = [shade](std::uint32_t value, std::uint32_t shift) { return ((((value >> shift) & 0xFFU) * shade) >> 8U) << shift; };
                row[x] = 0xFF000000U | dim(pixel, 16U) | dim(pixel, 8U) | dim(pixel, 0U);
            }
        }
    }

    void Dim(const RECT& keep, std::uint32_t shade) noexcept {
        if (pixels_ == nullptr) return;
        for (int y = 0; y < height_; ++y) {
            std::uint32_t* row = pixels_ + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
            for (int x = 0; x < width_; ++x) {
                if (Contains(keep, x, y)) continue;
                const std::uint32_t pixel = row[x];
                const auto dim = [shade](std::uint32_t value, std::uint32_t shift) { return ((((value >> shift) & 0xFFU) * shade) >> 8U) << shift; };
                row[x] = 0xFF000000U | dim(pixel, 16U) | dim(pixel, 8U) | dim(pixel, 0U);
            }
        }
    }

private:
    void Release() noexcept {
        if (memory_ != nullptr && previous_ != nullptr) SelectObject(memory_, previous_);
        if (bitmap_ != nullptr) DeleteObject(bitmap_);
        if (memory_ != nullptr) DeleteDC(memory_);
        memory_ = nullptr;
        bitmap_ = nullptr;
        previous_ = nullptr;
        pixels_ = nullptr;
    }

    HDC memory_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    std::uint32_t* pixels_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

struct MapWorkingCopy {
    EditorSceneContext* context = nullptr;
    kb::scene::SceneEntity entity{};
    kb::assets::AssetId asset{};
    Map2DAsset map;
    bool dirty = false;
    std::function<void()> notify;

    bool Publish() {
        std::string error;
        if (!EditorMap2DService::Publish(context->Scene(), asset, map, false, &error) ||
            !EditorMap2DService::SyncCollision(context->Scene(), asset, map, nullptr, &error)) {
            context->Console().Warning("2D Map", error);
            return false;
        }
        context->MarkSceneDocumentDirty();
        context->MarkSceneRenderDirty();
        dirty = true;
        if (notify) notify();
        return true;
    }
};

std::vector<std::weak_ptr<MapWorkingCopy>>& WorkingCopies() {
    static std::vector<std::weak_ptr<MapWorkingCopy>> copies;
    return copies;
}

std::shared_ptr<MapWorkingCopy> AcquireWorkingCopy(EditorSceneContext& context, kb::scene::SceneEntity entity,
    kb::assets::AssetId asset, Map2DAsset map) {
    auto& copies = WorkingCopies();
    std::erase_if(copies, [](const auto& copy) { return copy.expired(); });
    for (const auto& weak : copies) {
        if (auto copy = weak.lock(); copy && copy->context == &context && copy->asset == asset) {
            copy->entity = entity;
            return copy;
        }
    }
    auto copy = std::make_shared<MapWorkingCopy>();
    copy->context = &context; copy->entity = entity; copy->asset = asset; copy->map = std::move(map);
    copies.push_back(copy);
    return copy;
}

class MapStrokeCommand final : public IEditorCommand {
public:
    MapStrokeCommand(std::shared_ptr<MapWorkingCopy> document, kb::world2d::Map2DChangeSet change)
        : document_(std::move(document)), change_(std::move(change)) {}
    std::string_view Label() const noexcept override { return "Paint 2D Map"; }
    bool AffectsHierarchySelection() const noexcept override { return false; }
    bool Execute() override { return document_->Publish(); }
    bool Undo() override { return Apply(false); }
    bool Redo() override { return Apply(true); }
    void RemapEntities(const EditorEntityRemap& remap) override {
        if (auto found = remap.find(document_->entity.Id()); found != remap.end()) document_->entity = found->second;
    }
private:
    bool Apply(bool redo) {
        const auto oldOrientation = document_->map.orientation;
        const auto oldHeight = document_->map.height;
        const auto oldView = document_->map.viewHeight;
        const auto oldTileSize = document_->map.tileSize;
        const auto oldFloorHeight = document_->map.floorHeight;
        if (redo) change_.Redo(document_->map); else change_.Undo(document_->map);
        if (!document_->Publish()) {
            if (redo) change_.Undo(document_->map); else change_.Redo(document_->map);
            return false;
        }
        if (oldOrientation != document_->map.orientation || oldHeight != document_->map.height || oldView != document_->map.viewHeight ||
            oldTileSize != document_->map.tileSize || oldFloorHeight != document_->map.floorHeight)
            static_cast<void>(kb::world2d::SetUpMap2DView(document_->context->Scene(), document_->entity,
                document_->map, EditorProjectPaths::AssetsRoot()));
        return true;
    }
    std::shared_ptr<MapWorkingCopy> document_;
    kb::world2d::Map2DChangeSet change_;
};

class MapBatchCommand final : public IEditorCommand {
public:
    std::vector<std::unique_ptr<MapStrokeCommand>> edits;
    std::string_view Label() const noexcept override { return "Change 2D Map Settings"; }
    bool AffectsHierarchySelection() const noexcept override { return false; }
    bool Execute() override {
        for (auto& edit : edits) if (!edit->Execute()) return false;
        return true;
    }
    bool Undo() override {
        for (std::size_t i = edits.size(); i > 0; --i) {
            if (edits[i - 1]->Undo()) continue;
            for (std::size_t j = i; j < edits.size(); ++j) static_cast<void>(edits[j]->Redo());
            return false;
        }
        return true;
    }
    bool Redo() override {
        for (std::size_t i = 0; i < edits.size(); ++i) {
            if (edits[i]->Redo()) continue;
            while (i > 0) static_cast<void>(edits[--i]->Undo());
            return false;
        }
        return true;
    }
    void RemapEntities(const EditorEntityRemap& remap) override {
        for (auto& edit : edits) edit->RemapEntities(remap);
    }
};

bool VerifyNativeMap2D(EditorSceneContext& context, kb::scene::SceneEntity entity, kb::assets::AssetId asset, std::string* error);

class MapEditorWindow final {
public:
    MapEditorWindow(EditorSceneContext& context, kb::scene::SceneEntity entity, kb::assets::AssetId assetId,
        std::filesystem::path file, Map2DAsset map, std::function<void()> present)
        : context_(context), entity_(entity), assetId_(assetId), file_(std::move(file)),
          document_(AcquireWorkingCopy(context, entity, assetId, std::move(map))), map_(document_->map),
          present_(std::move(present)), assetsRoot_(EditorProjectPaths::AssetsRoot()) {
        const auto font = [](int height, int weight) {
            return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        };
        font_ = font(-14, FW_NORMAL);
        boldFont_ = font(-14, FW_SEMIBOLD);
        smallFont_ = font(-12, FW_NORMAL);
        titleFont_ = font(-22, FW_SEMIBOLD);
        ReloadLibrary();
        RefreshTextures();
        BuildPalette();
        if (!entries_.empty()) selectedKey_ = entries_.front().key;
        previousKey_ = selectedKey_;
        document_->notify = [this]() { entity_ = document_->entity; UpdateTitle(); Invalidate(); if (present_) present_(); };
    }

    ~MapEditorWindow() {
        form_.Close();
        document_->notify = {};
        if (window_ != nullptr && IsWindow(window_) != 0) {
            KillTimer(window_, kPublishTimer);
            SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
            DestroyWindow(window_);
        }
        for (HFONT font : { font_, boldFont_, smallFont_, titleFont_ }) {
            if (font != nullptr) DeleteObject(font);
        }
        if (editBrush_ != nullptr) DeleteObject(editBrush_);
    }

    MapEditorWindow(const MapEditorWindow&) = delete;
    MapEditorWindow& operator=(const MapEditorWindow&) = delete;

    [[nodiscard]] kb::assets::AssetId AssetId() const noexcept { return assetId_; }
    [[nodiscard]] bool Closed() const noexcept { return closed_; }
    bool ScenePainting() const noexcept { return paintInScene_; }
    bool SceneStroke() const noexcept { return sceneStroke_; }
    void ClearSceneHover() { sceneHover_.reset(); }
    std::vector<kb::scene::PhysicsDebugLineDesc> SceneBrushLines(const kb::math::DVec3& origin) const {
        std::vector<kb::scene::PhysicsDebugLineDesc> lines;
        if (!paintInScene_ || !sceneHover_ || context_.HasPlayModeSceneSession()) return lines;
        const auto transform = context_.Scene().Transforms().Get(entity_);
        const auto position = kb::math::RelativeTo(context_.Scene().Transforms().WorldTranslation(entity_), origin);
        const auto world = [&](float x, float y) {
            const float px = (x - static_cast<float>(map_.width) * 0.5F) * map_.tileSize;
            const float elevation = static_cast<float>(floor_) * map_.floorHeight;
            auto point = map_.orientation == kb::assets::Map2DOrientation::Wall ? kb::scene::Vec3{px, (static_cast<float>(map_.height)-y)*map_.tileSize, -elevation - 0.02F}
                : kb::scene::Vec3{px, elevation + 0.02F, (static_cast<float>(map_.height)*0.5F-y)*map_.tileSize};
            point = {point.x*transform.worldScale.x, point.y*transform.worldScale.y, point.z*transform.worldScale.z};
            return position + kb::math::Rotate(transform.worldRotation, point);
        };
        const auto [x,y] = *sceneHover_; const int half = brushSize_/2;
        const auto* entry = Selected();
        int x0 = x-half, y0 = y-half, x1 = x+half+1, y1 = y+half+1;
        if (entry && entry->kind == EntryKind::Object) { const auto start = kb::world2d::Map2DObjectOrigin(entry->columns,entry->rows,x,y); x0 = static_cast<int>(start.first); y0 = static_cast<int>(start.second); x1 = x0 + static_cast<int>(entry->columns); y1 = y0 + static_cast<int>(entry->rows); }
        if (rectActive_) { x0 = std::min(dragStartX_,x); y0 = std::min(dragStartY_,y); x1 = std::max(dragStartX_,x)+1; y1 = std::max(dragStartY_,y)+1; }
        const std::array points{world(static_cast<float>(x0),static_cast<float>(y0)),world(static_cast<float>(x1),static_cast<float>(y0)),world(static_cast<float>(x1),static_cast<float>(y1)),world(static_cast<float>(x0),static_cast<float>(y1))};
        const kb::scene::Vec3 color = Ctrl() || tool_ == Tool::Eraser ? kb::scene::Vec3{1,0.4F,0.2F} : kb::scene::Vec3{0.3F,0.8F,1};
        for (unsigned i = 0; i < 4; ++i) lines.push_back({points[i],points[(i+1)%4],color,1});
        return lines;
    }
    int Floor() const noexcept { return floor_; }
    void FinishSceneStroke() {
        if (!sceneStroke_) return;
        if (rectActive_) {
            auto editor = Editor(); PaintArea(editor, editor.Clamp(dragStartX_, dragStartY_, lastX_, lastY_), strokeErases_);
            rectActive_ = false;
        }
        sceneStroke_ = false; EndChange();
        if (publishPending_) Publish(false);
    }
    bool PaintSceneLocal(kb::scene::Vec3 point, bool begin) {
        if (!paintInScene_ || context_.HasPlayModeSceneSession() || tool_ == Tool::Select) return false;
        const auto* layer = std::as_const(context_.Scene()).Components().Tilemaps().TryGet(entity_);
        if (layer && layer->locked) return false;
        const int x = static_cast<int>(std::floor(point.x / map_.tileSize + static_cast<float>(map_.width) * 0.5F));
        const int y = static_cast<int>(std::floor(map_.orientation == kb::assets::Map2DOrientation::Wall
            ? static_cast<float>(map_.height) - point.y / map_.tileSize : static_cast<float>(map_.height) * 0.5F - point.z / map_.tileSize));
        if (x < 0 || y < 0 || x >= static_cast<int>(map_.width) || y >= static_cast<int>(map_.height)) return false;
        sceneHover_ = std::pair{x,y};
        if (begin) {
            if (sceneStroke_) FinishSceneStroke();
            if (tool_ == Tool::Picker) { Pick(x,y); return true; }
            if (tool_ == Tool::Fill) { FloodFill(x,y); return true; }
            BeginChange(); sceneStroke_ = true; strokeErases_ = Ctrl() || tool_ == Tool::Eraser;
            dragStartX_ = lastX_ = x; dragStartY_ = lastY_ = y;
            rectActive_ = tool_ == Tool::Rectangle || Shift();
            if (!rectActive_) ApplyAt(x,y);
        } else if (sceneStroke_) {
            if (!rectActive_ && (lastX_ != x || lastY_ != y)) ApplyLine(lastX_, lastY_, x, y);
            lastX_ = x; lastY_ = y;
        }
        if (publishPending_) Publish(false);
        return true;
    }

    [[nodiscard]] bool Create(HWND owner) {
        owner_ = owner;
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_DBLCLKS;
        windowClass.lpfnWndProc = &MapEditorWindow::WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = kWindowClass;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0U, &work, 0U);
        const int width = std::min(1500, Width(work) - 60);
        const int height = std::min(920, Height(work) - 60);
        window_ = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, width, height, owner, nullptr, windowClass.hInstance, this);
        if (window_ == nullptr) return false;
        DragAcceptFiles(window_, TRUE);
        searchEdit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, window_,
            reinterpret_cast<HMENU>(kSearchEditId), windowClass.hInstance, nullptr);
        if (searchEdit_ != nullptr) {
            SendMessageW(searchEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            const std::wstring cue = T("map2d.search", "Search the palette (Ctrl+F)");
            SendMessageW(searchEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue.c_str()));
        }
        editBrush_ = CreateSolidBrush(kTile);
        UpdateTitle();
        GetClientRect(window_, &client_);
        CenterMap();
        SetTimer(window_, kPublishTimer, 100U, nullptr);
        // The first Map Editor a user opens explains itself.
        std::error_code error;
        if (!std::filesystem::exists(TutorialSeenFile(), error)) tutorialStep_ = 0;
        ShowWindow(window_, SW_SHOW);
        SetFocus(window_);
        return true;
    }

    void BringToFront() const {
        if (window_ == nullptr) return;
        if (IsIconic(window_) != 0) ShowWindow(window_, SW_RESTORE);
        SetFocus(window_);
    }

    void Dock(HWND parent, const RECT& bounds) {
        if (window_ == nullptr) return;
        if (GetParent(window_) != parent) SetParent(window_, parent);
        owner_ = parent;
        SetWindowPos(window_, HWND_TOP, bounds.left, bounds.top, Width(bounds), Height(bounds), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    void Hide(HWND parent) {
        if (window_ != nullptr && GetParent(window_) == parent) ShowWindow(window_, SW_HIDE);
    }

    bool BelongsTo(const EditorSceneContext& context) const noexcept { return &context_ == &context; }
    void BindEntity(kb::scene::SceneEntity entity) { entity_ = entity; document_->entity = entity; }
    bool Flush() { FinishSceneStroke(); return !dirty_ && !strokeActive_ && !rectActive_ ? true : Save(false); }

    // True when the window may go: saved, discarded (the scene gets the saved map back) or clean.
    [[nodiscard]] bool ConfirmClose() {
        if (strokeActive_ || rectActive_) FinishStroke();
        if (!dirty_) return true;
        const EditorChoiceDialogResult choice = EditorChoiceDialog::Show(window_ != nullptr ? window_ : owner_, EditorChoiceDialogDescriptor{
            .title = std::string{ EditorText("map2d.title", "World 2D Map Editor") },
            .message = EditorLocalization::Format("map2d.saveQuestion", "Save changes to {file}?", { { "file", Utf8(file_.filename()) } }),
            .supportingText = std::string{ EditorText("map2d.saveLost", "Unsaved changes are lost if you do not save them.") },
            .primaryLabel = std::string{ EditorText("map2d.save", "Save") },
            .secondaryLabel = std::string{ EditorText("map2d.dontSave", "Don't Save") },
            .cancelLabel = std::string{ EditorText("map2d.cancel", "Cancel") },
            .icon = HeroIconKind::Save,
        });
        if (choice == EditorChoiceDialogResult::Cancel) return false;
        if (choice == EditorChoiceDialogResult::Primary) return Save();
        return Discard();
    }

    void Close() {
        if (window_ != nullptr && IsWindow(window_) != 0) DestroyWindow(window_);
    }

    [[nodiscard]] const Map2DAsset& Map() const noexcept { return map_; }

    // The settings the Inspector changes, applied to the map being edited. The file holds them
    // already, so they do not make the map unsaved.
    void EditSettings(const std::function<void(Map2DAsset&)>& edit) {
        edit(map_);
        Publish(false);
        Invalidate();
    }

    [[nodiscard]] bool CaptureTo(const std::filesystem::path& png, std::string* error) {
        if (!Render()) {
            if (error != nullptr) *error = "The map editor could not be drawn";
            return false;
        }
        HeroIconGdiplusRuntime::EnsureStarted();
        const std::optional<CLSID> encoder = PngEncoder();
        Gdiplus::Bitmap image(surface_.Bitmap(), nullptr);
        std::error_code fileError;
        std::filesystem::create_directories(png.parent_path(), fileError);
        if (!encoder.has_value() || image.Save(png.wstring().c_str(), &*encoder, nullptr) != Gdiplus::Ok) {
            if (error != nullptr) *error = "The map editor capture could not be written";
            return false;
        }
        return true;
    }

    void SetClientSize(int width, int height) {
        client_ = RECT{ 0, 0, width, height };
        CenterMap();
    }

    // One automation line (EditorMap2DWindow::Drive).
    [[nodiscard]] bool DriveLine(const std::string& line, std::string* error) {
        std::istringstream words{ line };
        std::string verb;
        words >> verb;
        if (verb == "verify-native") return VerifyNativeMap2D(context_,entity_,assetId_,error);
        if (verb == "settings") { std::string name; words >> name; return OpenSettings(name); }
        if (verb == "field") { unsigned index = 0; std::string value; if (!(words >> index)) return false; std::getline(words >> std::ws,value); return form_.SetValue(index,value); }
        if (verb == "preset") { unsigned index = 0; return (words >> index) && form_.Preset(index); }
        if (verb == "apply") return form_.Accept();
        if (verb == "reject") return form_.IsOpen() && !form_.Accept() && !form_.Error().empty();
        if (verb == "cancel") { if (!form_.IsOpen()) return false; form_.Close(); return true; }
        if (verb == "remember-map") { automationMapSnapshot_ = kb::assets::Map2DAssetIO::Write(map_); return true; }
        if (verb == "assert-map-unchanged") return !automationMapSnapshot_.empty() && automationMapSnapshot_ == kb::assets::Map2DAssetIO::Write(map_);
        if (verb == "animation-open") return OpenAnimationEditor();
        if (verb == "animation-add") { unsigned cell = 0; if (!(words >> cell)) return false; return AddAnimationFrame(cell); }
        if (verb == "animation-select") { int index = 0; if (!(words >> index) || index < 0 || index >= static_cast<int>(animationFrames_.size())) return false; animationSelected_=index; return true; }
        if (verb == "animation-apply") return ApplyAnimation();
        if (verb == "animation-clear") { if (!animationEditing_) return false; animationFrames_.clear(); animationSelected_=-1; return true; }
        if (verb == "animation-move") { int direction = 0; if (!(words >> direction)) return false; MoveAnimationFrame(direction); return true; }
        if (verb == "discard") return Discard();
        if (verb == "unique") return MakeUnique();
        if (verb == "scene-local") { float x = 0,y = 0,z = 0; int begin = 0; if (!(words >> x >> y >> z >> begin)) return false; return PaintSceneLocal({x,y,z},begin != 0); }
        if (verb == "scene-finish") { FinishSceneStroke(); return true; }
        if (verb == "scene-paint") { paintInScene_ = true; return true; }
        if (verb == "density") { unsigned value = 0; if (!(words >> value) || value < 1U || value > 100U) return false; density_ = value; return true; }
        if (verb == "layout") {
            std::string texture; unsigned width = 0, height = 0, margin = 0, spacing = 0;
            if (!(words >> texture >> width >> height >> margin >> spacing)) return false;
            sheetTexture_ = texture; SetSheet([=](auto& sheet) { sheet.cellSize = width; sheet.cellHeight = height; sheet.margin = margin; sheet.spacing = spacing; }); return !messageIsError_;
        }
        if (verb == "lighting" || verb == "sort-y") { int enabled = 0; if (!(words >> enabled)) return false; BeginChange(); if (verb == "lighting") map_.lit = enabled != 0; else map_.sortByY = enabled != 0; EndChange(); return !messageIsError_; }
        if (verb == "parallax") { float x = 1, y = 1; if (!(words >> x >> y)) return false; BeginChange(); map_.parallaxX = x; map_.parallaxY = y; EndChange(); return !messageIsError_; }
        if (verb == "occlusion") { int enabled = 0; if (!(words >> enabled)) return false; EditSelectedDefinitions([&](auto& def) { def.occludesLight = enabled != 0; }); return !messageIsError_; }
        if (verb == "time") return static_cast<bool>(words >> previewSeconds_);
        if (verb == "select") {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (!(words >> x0 >> y0 >> x1 >> y1)) return false;
            selection_ = Editor().Clamp(x0, y0, x1, y1); hasSelection_ = true; return true;
        }
        if (verb == "transform") {
            std::string action; words >> action;
            TransformSelection(action); return !messageIsError_;
        }
        if (verb == "opacity") {
            unsigned percent = 0; if (!(words >> percent) || percent > 100U) return false;
            TransformSelection("opacity", (percent * 255U + 50U) / 100U); return !messageIsError_;
        }
        if (verb == "collision-default") { UseSelectedCollisionAsDefault(); return !messageIsError_; }
        if (verb == "animate") {
            unsigned count = 0; float speed = 0;
            if (!(words >> count >> speed)) return false;
            AnimateSelection(count, speed); return !messageIsError_;
        }
        if (verb == "collision") {
            unsigned kind = 0; if (!(words >> kind) || kind > 5U) return false;
            kb::assets::Map2DCollision shape; shape.kind = static_cast<kb::assets::Map2DCollisionKind>(kind);
            float x = 0, y = 0; while (words >> x >> y) shape.polygon.push_back({x, y});
            if (!kb::assets::IsMap2DCollisionValid(shape)) return false;
            EditSelectedTiles([&](Map2DTile& tile) { tile.collision = shape; tile.solid = false; }); return !messageIsError_;
        }
        if (verb == "stamp") { std::string name; std::getline(words >> std::ws, name); return SaveStamp(name); }
        if (verb == "pick") { std::string key; std::getline(words >> std::ws, key); if (!Find(key)) return false; selectedKey_ = key; return true; }
        if (verb == "place") { int x = 0, y = 0; if (!(words >> x >> y)) return false; BeginChange(); ApplyAt(x, y); EndChange(); return !messageIsError_; }
        if (verb == "data") {
            std::string key, value; words >> key; std::getline(words >> std::ws, value);
            EditSelectedTiles([&](Map2DTile& tile) { tile.properties[key] = value; }); return !messageIsError_;
        }
        if (verb == "order") { int order = 0; return (words >> order) && SetLayerOrder(order); }
        if (verb == "view") {
            std::string orientation;
            words >> orientation;
            if (orientation != "flat" && orientation != "wall") return false;
            SetOrientation(orientation == "wall" ? kb::assets::Map2DOrientation::Wall : kb::assets::Map2DOrientation::Flat);
            return true;
        }
        if (verb == "resize") {
            std::uint32_t width = 0U, height = 0U;
            if (!(words >> width >> height) || width == 0U || height == 0U || width > Map2DAsset::MaximumSize || height > Map2DAsset::MaximumSize) return false;
            ResizeTo(width, height);
            return true;
        }
        if (verb == "import") {
            std::string path;
            std::getline(words >> std::ws, path);
            if (path.empty()) return false;
            ImportImages({ assetsRoot_.parent_path() / kb::assets::Utf8Path(path) });
            return !messageIsError_;
        }
        if (verb == "search") {
            std::string text;
            std::getline(words, text);
            while (!text.empty() && text.front() == ' ') text.erase(text.begin());
            SetSearch(Wide(text));
            return true;
        }
        if (verb == "category") {
            std::string text;
            std::getline(words, text);
            while (!text.empty() && text.front() == ' ') text.erase(text.begin());
            category_ = text == "all" ? std::string{ kAllCategory } : text == "recent" ? std::string{ kRecentCategory } : text;
            paletteScroll_ = 0;
            return true;
        }
        if (verb == "palette") {
            std::string text;
            words >> text;
            paletteType_ = text;
            category_ = std::string{ kAllCategory };
            paletteScroll_ = 0;
            return true;
        }
        if (verb == "tutorial") {
            tutorialStep_ = 0;
            return true;
        }
        std::vector<std::string> rest;
        for (std::string word; words >> word;) rest.push_back(word);
        std::vector<int> numbers;
        std::uint8_t modifiers = 0U;
        std::string keyName;
        for (const std::string& word : rest) {
            int value = 0;
            const auto [end, parsed] = std::from_chars(word.data(), word.data() + word.size(), value);
            if (parsed == std::errc{} && end == word.data() + word.size()) numbers.push_back(value);
            else if (word == "ctrl") modifiers |= kCtrl;
            else if (word == "shift") modifiers |= kShift;
            else if (word == "alt") modifiers |= kAlt;
            else keyName = word;
        }
        driven_ = true;
        drivenModifiers_ = modifiers;
        const auto need = [&](std::size_t count) {
            if (numbers.size() >= count) return true;
            if (error != nullptr) *error = "Map editor input needs more numbers: " + line;
            return false;
        };
        bool ok = true;
        if (verb == "click") {
            ok = need(2U);
            if (ok) {
                OnMouseMove(numbers[0], numbers[1]);
                OnLeftDown(numbers[0], numbers[1]);
                OnLeftUp(numbers[0], numbers[1]);
            }
        } else if (verb == "dblclick") {
            ok = need(2U);
            if (ok) {
                OnMouseMove(numbers[0], numbers[1]);
                OnLeftDown(numbers[0], numbers[1]);
                OnLeftUp(numbers[0], numbers[1]);
                OnDoubleClick(numbers[0], numbers[1]);
                OnLeftUp(numbers[0], numbers[1]);
            }
        } else if (verb == "drag") {
            ok = need(4U);
            if (ok) {
                OnMouseMove(numbers[0], numbers[1]);
                OnLeftDown(numbers[0], numbers[1]);
                const int steps = 12;
                for (int step = 1; step <= steps; ++step) {
                    OnMouseMove(numbers[0] + (numbers[2] - numbers[0]) * step / steps, numbers[1] + (numbers[3] - numbers[1]) * step / steps);
                }
                OnLeftUp(numbers[2], numbers[3]);
            }
        } else if (verb == "move") {
            ok = need(2U);
            if (ok) OnMouseMove(numbers[0], numbers[1]);
        } else if (verb == "wheel") {
            ok = need(3U);
            if (ok) OnWheel(numbers[0], numbers[1], numbers[2]);
        } else if (verb == "key") {
            const std::optional<WPARAM> key = KeyCode(keyName);
            ok = key.has_value();
            if (ok) {
                static_cast<void>(OnKey(*key));
                OnKeyUp(*key);
            } else if (error != nullptr) {
                *error = "Unknown key in: " + line;
            }
        } else {
            ok = false;
            if (error != nullptr) *error = "Unknown map editor input: " + line;
        }
        drivenModifiers_ = 0U;
        if (publishPending_) Publish(false);
        return ok;
    }

private:
    [[nodiscard]] static std::optional<WPARAM> KeyCode(const std::string& name) {
        if (name.size() == 1U && std::isalnum(static_cast<unsigned char>(name[0])) != 0) {
            return static_cast<WPARAM>(std::toupper(static_cast<unsigned char>(name[0])));
        }
        const std::array<std::pair<std::string_view, WPARAM>, 18U> named{ {
            { "space", VK_SPACE }, { "delete", VK_DELETE }, { "escape", VK_ESCAPE }, { "pageup", VK_PRIOR },
            { "pagedown", VK_NEXT }, { "left", VK_LEFT }, { "right", VK_RIGHT }, { "up", VK_UP }, { "down", VK_DOWN },
            { "f5", VK_F5 }, { "plus", VK_OEM_PLUS }, { "minus", VK_OEM_MINUS }, { "[", VK_OEM_4 }, { "]", VK_OEM_6 },
            { "numplus", VK_ADD }, { "numminus", VK_SUBTRACT }, { "enter", VK_RETURN }, { "f1", VK_F1 },
        } };
        for (const auto& [text, code] : named) {
            if (text == name) return code;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool Down(int key, std::uint8_t bit) const noexcept {
        return driven_ ? (drivenModifiers_ & bit) != 0U : GetKeyState(key) < 0;
    }
    [[nodiscard]] bool Ctrl() const noexcept { return Down(VK_CONTROL, kCtrl); }
    [[nodiscard]] bool Shift() const noexcept { return Down(VK_SHIFT, kShift); }
    [[nodiscard]] bool Alt() const noexcept { return Down(VK_MENU, kAlt); }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<MapEditorWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self == nullptr) return DefWindowProcW(window, message, wparam, lparam);
        return self->Handle(window, message, wparam, lparam);
    }

    LRESULT Handle(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_DROPFILES: {
            const HDROP drop = reinterpret_cast<HDROP>(wparam);
            std::vector<std::filesystem::path> paths;
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0U);
            for (UINT index = 0U; index < count; ++index) {
                std::wstring path(DragQueryFileW(drop, index, nullptr, 0U) + 1U, L'\0');
                DragQueryFileW(drop, index, path.data(), static_cast<UINT>(path.size()));
                path.resize(path.size() - 1U);
                paths.emplace_back(path);
            }
            DragFinish(drop);
            ImportImages(paths);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            if (Render()) BitBlt(dc, 0, 0, surface_.Width(), surface_.Height(), surface_.Dc(), 0, 0, SRCCOPY);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_SIZE:
            GetClientRect(window, &client_);
            Invalidate();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize = POINT{ 1100, 640 };
            return 0;
        }
        case WM_TIMER:
            if (wparam == kPublishTimer && publishPending_) Publish(false);
            if (wparam == kPublishTimer) {
                previewSeconds_ = static_cast<double>(GetTickCount64()) * 0.001;
                if (std::ranges::any_of(map_.sheets, [](const auto& sheet) {
                    return std::ranges::any_of(sheet.definitions, [](const auto& entry) { return entry.second.frames.size() > 1U; });
                })) {
                    Invalidate();
                    if (!context_.HasPlayModeSceneSession() && live_) {
                        std::string error;
                        if (!EditorMap2DService::RefreshPreview(context_.Scene(), assetId_, map_, previewSeconds_, &error)) SetMessage(error, true);
                        context_.MarkSceneRenderDirty(); if (present_) present_();
                    }
                }
                if (animationEditing_) Invalidate();
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wparam) == kSearchEditId && (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS)) {
                searchFocused_ = HIWORD(wparam) == EN_SETFOCUS;
                Invalidate();
            }
            if (LOWORD(wparam) == kSearchEditId && HIWORD(wparam) == EN_CHANGE) {
                wchar_t text[256]{};
                GetWindowTextW(searchEdit_, text, 255);
                SetSearch(text);
                Invalidate();
            }
            return 0;
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
            SetTextColor(reinterpret_cast<HDC>(wparam), kText);
            SetBkColor(reinterpret_cast<HDC>(wparam), kTile);
            return reinterpret_cast<LRESULT>(editBrush_);
        case WM_LBUTTONDOWN:
            SetFocus(window);
            OnLeftDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_LBUTTONDBLCLK:
            SetFocus(window);
            OnDoubleClick(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_LBUTTONUP:
            OnLeftUp(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_RBUTTONDOWN:
            SetFocus(window);
            OnRightDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_MBUTTONDOWN:
            SetFocus(window);
            OnMiddleDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_MBUTTONUP:
            OnMiddleUp(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_MOUSELEAVE:
            hoverX_ = -1;
            hot_ = Control::None;
            trackingLeave_ = false;
            Invalidate();
            return 0;
        case WM_MOUSEWHEEL: {
            POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            ScreenToClient(window, &point);
            OnWheel(point.x, point.y, GET_WHEEL_DELTA_WPARAM(wparam));
            return 0;
        }
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
            // Alt is a mouse modifier here (Alt+wheel, Alt+drag), never the window menu.
            if (wparam == VK_MENU || OnKey(wparam)) return 0;
            break;
        case WM_SYSKEYUP:
        case WM_KEYUP:
            OnKeyUp(wparam);
            if (wparam == VK_MENU) return 0;
            break;
        case WM_SYSCHAR:
            return 0;
        case WM_CAPTURECHANGED:
            if (strokeActive_ || rectActive_) FinishStroke();
            if (selecting_ || moving_) FinishSelectionDrag();
            panning_ = false;
            sheetSelecting_ = false;
            return 0;
        case WM_CLOSE:
            if (ConfirmClose()) DestroyWindow(window);
            return 0;
        case WM_NCDESTROY:
            KillTimer(window, kPublishTimer);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            window_ = nullptr;
            searchEdit_ = nullptr;
            closed_ = true;
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    // ----- Layout -----

    [[nodiscard]] int PaletteWidth() const noexcept { return std::clamp(static_cast<int>(client_.right) / 3, 200, 320); }
    int toolbarHeight_ = 44;
    [[nodiscard]] RECT ToolbarRect() const noexcept { return RECT{ 0, 0, client_.right, toolbarHeight_ }; }
    [[nodiscard]] RECT StatusRect() const noexcept { return RECT{ 0, client_.bottom - kStatusHeight, client_.right, client_.bottom }; }
    [[nodiscard]] RECT PaletteRect() const noexcept { return RECT{ 0, toolbarHeight_, PaletteWidth(), client_.bottom - kStatusHeight }; }
    [[nodiscard]] RECT CanvasRect() const noexcept { return RECT{ PaletteWidth() + 1, toolbarHeight_, client_.right, client_.bottom - kStatusHeight }; }

    ControlBox& Add(RECT rect, Control id, std::wstring label, bool active = false, int param = 0, std::wstring tip = {}) {
        controls_.push_back(ControlBox{ rect, id, param, std::move(label), active, true, std::move(tip) });
        return controls_.back();
    }

    void Layout() {
        controls_.clear();
        sheet_ = SheetLayout{};
        gridRect_ = RECT{};
        searchRect_ = RECT{};
        typeRect_ = RECT{};
        categoryRect_ = RECT{};
        int x = kPad;
        toolbarHeight_ = 44;
        int top = 8;
        int bottom = 36;
        const auto button = [&](Control id, std::wstring label, int width, bool active, std::wstring tip, int param = 0) -> ControlBox& {
            if (x > kPad && x + width > client_.right - kPad) { x = kPad; top += 36; bottom += 36; toolbarHeight_ = bottom + 8; }
            ControlBox& box = Add(RECT{ x, top, x + width, bottom }, id, std::move(label), active, param, std::move(tip));
            x += width + 4;
            return box;
        };
        const auto gap = [&x]() { x += 10; };
        button(Control::Save, dirty_ ? T("map2d.save", "Save") + L" *" : T("map2d.save", "Save"), 64, false, T("map2d.tip.save", "Save the map (Ctrl+S)"));
        button(Control::Undo, T("map2d.undo", "Undo"), 56, false, T("map2d.tip.undo", "Undo (Ctrl+Z)")).enabled = context_.CanUndoSceneCommand();
        button(Control::Redo, T("map2d.redo", "Redo"), 56, false, T("map2d.tip.redo", "Redo (Ctrl+Shift+Z)")).enabled = context_.CanRedoSceneCommand();
        gap();
        button(Control::ToolDraw, T("map2d.paint", "Paint"), 72, tool_ == Tool::Draw, T("map2d.tip.draw", "Paint with the brush picked on the left; Ctrl+click erases"));
        button(Control::ToolEraser, T("map2d.eraser", "Eraser"), 72, tool_ == Tool::Eraser, T("map2d.tip.eraser", "Clear tiles; Shift removes only the top item"));
        button(Control::ToolFill, T("map2d.fill", "Fill"), 64, tool_ == Tool::Fill, T("map2d.tip.fillSimple", "Fill connected tiles with the selected ground"));
        button(Control::ToolSelect, T("map2d.select", "Select"), 76, tool_ == Tool::Select, T("map2d.tip.select", "Select tiles to move, copy, cut, paste or delete (Space)"));
        button(Control::BrushSize, TF("map2d.size", "Size {size}", { { "size", std::to_string(brushSize_) } }), 80, false,
            T("map2d.tip.size", "Brush size: [ bigger, ] smaller, or Alt+wheel"));
        gap();
        button(Control::ViewMenu, (tool_ == Tool::Collision ? T("map2d.collision", "Collision") : T("map2d.more", "More")) + L" \u25BE", 90, tool_ == Tool::Collision,
            T("map2d.tip.more", "Map size, view, floors and extra tools"));
        button(Control::ShowInScene, T("map2d.showInScene", "Show in Scene"), 128, false, T("map2d.tip.showInScene", "Look at the map in the scene"));
        button(Control::Help, L"?", 30, tutorialStep_ >= 0, T("map2d.tip.help", "How it works: the short tutorial (F1)"));

        const RECT palette = PaletteRect();
        const int left = palette.left + kPad;
        const int right = palette.right - kPad;
        if (panel_ == Panel::Palette) LayoutPalette(left, right, palette.top + kPad, palette.bottom - kPad);
        else LayoutSheetEditor(left, right, palette.top + kPad, palette.bottom - kPad);
        if (!editName_.empty()) LayoutBoard();
        LayoutCategoryList();
        PlaceSearchEdit();
        if (tutorialStep_ >= 0) LayoutTutorial();
        if (animationEditing_) LayoutAnimationEditor();
    }

    // The palette's type, as Remere's palettes: grounds (with floor tiles), objects, items, or everything.
    [[nodiscard]] static std::vector<std::string> TypeKeys() { return { "grounds", "objects", "items", "all" }; }

    [[nodiscard]] bool OfType(const PaletteEntry& entry) const noexcept {
        if (paletteType_ == "grounds") return entry.kind == EntryKind::Ground || entry.kind == EntryKind::Floor;
        if (paletteType_ == "objects") return entry.kind == EntryKind::Object;
        if (paletteType_ == "items") return entry.kind == EntryKind::Item;
        return true;
    }

    // Everything, Recently used, then the folders that have something of the type.
    [[nodiscard]] std::vector<std::string> CategoryKeys() const {
        std::vector<std::string> keys{ std::string{ kAllCategory }, std::string{ kRecentCategory } };
        for (const std::string& category : categories_) {
            if (std::ranges::any_of(entries_, [&](const PaletteEntry& entry) { return entry.category == category && OfType(entry); })) keys.push_back(category);
        }
        return keys;
    }

    [[nodiscard]] std::vector<std::string> ListKeys() const { return listIsType_ ? TypeKeys() : CategoryKeys(); }
    [[nodiscard]] const std::string& ListCurrent() const noexcept { return listIsType_ ? paletteType_ : category_; }
    [[nodiscard]] const RECT& ListAnchor() const noexcept { return listIsType_ ? typeRect_ : categoryRect_; }

    void LayoutCategoryList() {
        categoryListRect_ = RECT{};
        const RECT& anchor = ListAnchor();
        if (!categoryOpen_ || Width(anchor) == 0 || tutorialStep_ >= 0) {
            categoryOpen_ = false;
            return;
        }
        const int count = static_cast<int>(ListKeys().size());
        const int shown = std::min(count, kListRows);
        categoryFirst_ = std::clamp(categoryFirst_, 0, count - shown);
        // Categories: a little more room under the second entry, where a line divides the folders from the two views.
        const int divider = !listIsType_ && categoryFirst_ < 2 ? 7 : 0;
        const int top = anchor.bottom + 3;
        categoryListRect_ = RECT{ anchor.left, top, anchor.right, top + 6 + shown * kListRow + divider };
        int y = top + 3;
        for (int index = categoryFirst_; index < categoryFirst_ + shown; ++index) {
            Add(RECT{ categoryListRect_.left + 3, y, categoryListRect_.right - 9, y + kListRow }, Control::CategoryItem, {}, false, index);
            y += kListRow + (!listIsType_ && index == 1 ? 7 : 0);
        }
    }

    void OpenList(bool type, bool open) {
        listIsType_ = type;
        categoryOpen_ = open;
        if (!open) return;
        const std::vector<std::string> keys = ListKeys();
        const auto current = std::ranges::find(keys, ListCurrent());
        categoryKey_ = current == keys.end() ? 0 : static_cast<int>(current - keys.begin());
        // The current entry shows in the middle of the list.
        categoryFirst_ = categoryKey_ - kListRows / 2;
    }

    void PickCategory(int index) {
        const std::vector<std::string> keys = ListKeys();
        if (index >= 0 && index < static_cast<int>(keys.size())) {
            if (listIsType_) {
                paletteType_ = keys[static_cast<std::size_t>(index)];
                // A folder with nothing of the new type shows everything of it instead.
                const std::vector<std::string> categories = CategoryKeys();
                if (std::ranges::find(categories, category_) == categories.end()) category_ = std::string{ kAllCategory };
            } else {
                category_ = keys[static_cast<std::size_t>(index)];
            }
            paletteScroll_ = 0;
        }
        categoryOpen_ = false;
    }

    void FocusSearch() {
        searchFocused_ = true;
        PlaceSearchEdit();
        SetFocus(searchEdit_);
    }

    void PlaceSearchEdit() {
        if (searchEdit_ == nullptr) return;
        const bool visible = !form_.IsOpen() && !animationEditing_ && panel_ == Panel::Palette && tutorialStep_ < 0 && Width(searchRect_) > 0 && (searchFocused_ || !search_.empty());
        ShowWindow(searchEdit_, visible ? SW_SHOWNA : SW_HIDE);
        if (visible) {
            MoveWindow(searchEdit_, searchRect_.left + 8, searchRect_.top + 5, Width(searchRect_) - 16, Height(searchRect_) - 9, FALSE);
        }
    }

    void LayoutPalette(int left, int right, int y, int bottom) {
        Add(RECT{ left, y, right, y + 34 }, Control::ImportImages, T("map2d.addGraphics", "Add graphics..."), true, 0,
            T("map2d.tip.addGraphics", "Choose PNG or BMP images, or drop them into this window"));
        y += 42;
        searchRect_ = RECT{ left, y, right, y + kRowHeight };
        y += kRowHeight + 6;
        typeRect_ = RECT{};
        categoryRect_ = editName_.empty() ? RECT{ left, y, right, y + kRowHeight } : RECT{};
        if (Width(categoryRect_) > 0) {
            Add(categoryRect_, Control::CategoryList, {}, categoryOpen_ && !listIsType_, 0, T("map2d.tip.category", "Categories: the folders of Assets/2D Map Sprites"));
        }
        y += kRowHeight + 8;
        // Below the grid: what is picked and what can be done with it.
        const int actionsTop = bottom - kRowHeight - 40;
        gridRect_ = RECT{ left, y, right, actionsTop - 6 };
        const std::vector<const PaletteEntry*> visible = VisibleEntries();
        const int perRow = std::max(1, (Width(gridRect_) + kTileGap) / (kTileSize + kTileGap));
        const int rows = (static_cast<int>(visible.size()) + perRow - 1) / perRow;
        const int content = rows * (kTileSize + kTileGap);
        paletteScroll_ = std::clamp(paletteScroll_, 0, std::max(0, content - Height(gridRect_)));
        for (std::size_t index = 0U; index < visible.size(); ++index) {
            const int row = static_cast<int>(index) / perRow;
            const int column = static_cast<int>(index) % perRow;
            const int tileTop = gridRect_.top + row * (kTileSize + kTileGap) - paletteScroll_;
            if (tileTop + kTileSize <= gridRect_.top || tileTop >= gridRect_.bottom) continue;
            const int tileLeft = gridRect_.left + column * (kTileSize + kTileGap);
            Add(RECT{ tileLeft, tileTop, tileLeft + kTileSize, tileTop + kTileSize }, Control::PaletteTile, {},
                visible[index]->key == selectedKey_, static_cast<int>(index), EntryTip(*visible[index]));
        }
        infoTop_ = actionsTop;
        int actions = actionsTop + 40;
        const PaletteEntry* selected = Selected();
        Add(RECT{ left, actions, right, actions + kRowHeight }, Control::PaletteMenu, T("map2d.editSelected", "Edit selected graphic...") + L" \u25BE", false, 0,
            T("map2d.tip.editSelected", "Sprite size, objects and automatic ground edges")).enabled = selected != nullptr;
    }

    // The ground editor: a card over the map with the 5x5 board. A square is picked on the board,
    // then the sprite for it in the palette on the left.
    void LayoutBoard() {
        const Map2DGroundBrush* ground = library_.Find(editName_);
        if (ground == nullptr) {
            CloseBoard();
            return;
        }
        const RECT canvas = CanvasRect();
        constexpr int fullWidth = 20 + kBoardSide + 24 + 260 + 20;
        constexpr int fullHeight = 20 + 34 + 14 + kBoardSide + 18 + 34 + 16;
        boardCompact_ = Width(canvas) < fullWidth + 16 || Height(canvas) < fullHeight + 16;
        boardCellPixels_ = boardCompact_ ? std::clamp(std::min((Width(canvas) - 56 - 4 * kBoardGap) / 5,
            (Height(canvas) - 252 - 4 * kBoardGap) / 5), 16, kBoardCell) : kBoardCell;
        const int boardSide = 5 * boardCellPixels_ + 4 * kBoardGap;
        const int width = boardCompact_ ? std::min(fullWidth, Width(canvas) - 16) : fullWidth;
        const int height = boardCompact_ ? boardSide + 236 : fullHeight;
        const int left = std::max(static_cast<int>(canvas.left) + 8, static_cast<int>(canvas.left + canvas.right) / 2 - width / 2);
        const int top = std::max(toolbarHeight_ + 8, static_cast<int>(canvas.top + canvas.bottom) / 2 - height / 2);
        boardCard_ = RECT{ left, top, left + width, top + height };
        const int x = left + 20;
        boardLeft_ = boardCompact_ ? left + (width - boardSide) / 2 : x;
        int y = top + 20;
        Add(RECT{ x, y, boardCompact_ ? boardCard_.right - 20 : x + boardSide, y + 34 }, Control::RenameGround, Wide(ground->name) + L"  \u270E", false, 0, T("map2d.tip.rename", "Rename the ground"));
        y += 34 + 14;
        boardTop_ = y;
        const std::array<BoardSlot, 25U> slots = BoardSlots();
        for (int index = 0; index < 25; ++index) {
            const BoardSlot& slot = slots[static_cast<std::size_t>(index)];
            // The looks fill in order, so only the next free one takes a sprite.
            if ((slot.variant < 0 && slot.piece < 0) || slot.variant > static_cast<int>(ground->tiles.size())) continue;
            Add(BoardCellRect(index), Control::BoardCell, {}, boardCell_ == index, index,
                slot.piece >= 0 ? PieceTip(static_cast<Map2DBorderPiece>(slot.piece))
                    : slot.variant == 0 ? T("map2d.board.middle", "The ground itself: click it, then a sprite on the left to change it")
                                        : T("map2d.board.look", "Another look of the ground (optional, mixed in at random)"));
        }
        const int side = boardCompact_ ? x : x + boardSide + 24;
        const int right = boardCard_.right - 20;
        const int layer = boardTop_ + boardSide + (boardCompact_ ? 78 : -2 * kRowHeight - 10);
        Add(RECT{ right - 98, layer, right - 70, layer + kRowHeight }, Control::ZOrderDown, L"\u2212", false, 0, T("map2d.tip.zDown", "Lower (Shift: by 10)"));
        Add(RECT{ right - 66, layer, right - 30, layer + kRowHeight }, Control::None, std::to_wstring(ground->zOrder), false, 0,
            T("map2d.tip.z", "A higher ground draws its edges over a lower one")).enabled = false;
        Add(RECT{ right - 26, layer, right, layer + kRowHeight }, Control::ZOrderUp, L"+", false, 0, T("map2d.tip.zUp", "Higher (Shift: by 10)"));
        boardLayerTop_ = layer;
        const int buttons = boardCard_.bottom - 16 - 34;
        const int deleteTop = boardCompact_ ? buttons : boardTop_ + boardSide - kRowHeight;
        Add(RECT{ side, deleteTop, side + 110, deleteTop + kRowHeight }, Control::DeleteGround, T("map2d.deleteGround", "Delete ground"), false, 0,
            T("map2d.tip.deleteGround", "Delete this ground brush (what is painted stays)"));
        Add(RECT{ right - 150, buttons, right, buttons + 34 }, Control::BoardDone, T("map2d.board.done", "Done"), true, 0,
            T("map2d.tip.boardDone", "Close and paint with the ground (Esc)"));
    }

    [[nodiscard]] RECT BoardCellRect(int index) const noexcept {
        const int left = boardLeft_ + (index % 5) * (boardCellPixels_ + kBoardGap);
        const int top = boardTop_ + (index / 5) * (boardCellPixels_ + kBoardGap);
        return RECT{ left, top, left + boardCellPixels_, top + boardCellPixels_ };
    }

    void LayoutSheetEditor(int left, int right, int y, int bottom) {
        const kb::world2d::Map2DSheetSetting setting = library_.SheetSetting(sheetTexture_);
        Add(RECT{ left, y, left + 126, y + kRowHeight }, Control::Back, L"\u2190 " + T("map2d.paintMap", "Paint map"), false, 0, T("map2d.tip.back", "Back to the palette (Esc)"));
        Add(RECT{ left + 132, y, right, y + kRowHeight }, Control::None, Wide(std::filesystem::path{ sheetTexture_ }.filename().string()), false).enabled = false;
        y += kRowHeight + 6;
        sizeLabelTop_ = y;
        const std::array<std::uint32_t, 5U> sizes{ 16U, 32U, 48U, 64U, 0U };
        const int labelWidth = 92;
        const int width = (right - left - labelWidth - 4 * 4) / 5;
        for (std::size_t index = 0U; index < sizes.size(); ++index) {
            const int cellLeft = left + labelWidth + static_cast<int>(index) * (width + 4);
            Add(RECT{ cellLeft, y, cellLeft + width, y + kRowHeight }, Control::CellSize, sizes[index] == 0U ? L"..." : std::to_wstring(sizes[index]),
                setting.cellSize == sizes[index], static_cast<int>(sizes[index]), T("map2d.tip.cellSize", "The size of one sprite in this sheet, in pixels"));
        }
        y += kRowHeight + 6;
        Add(RECT{ left, y, right, y + kRowHeight }, Control::JoinSprites,
            setting.separate ? T("map2d.joinOff", "Touching sprites stay single") : T("map2d.joinOn", "Touching sprites form one object"), !setting.separate, 0,
            T("map2d.tip.join", "Whether a drawing that spans several sprites (a tree) shows as one object in the palette"));
        y += kRowHeight + 8;
        guideTop_ = y;
        y += 54;
        const int half = (right - left - 6) / 2;
        Add(RECT{ left, y, left + half, y + kRowHeight }, Control::SaveObject, T("map2d.saveObject", "Save as object"), false, 0,
            T("map2d.tip.saveObject", "Drag over the sprites of one object below, then save it under a name"));
        const PaletteEntry* selected = Selected();
        Add(RECT{ left + half + 6, y, right, y + kRowHeight }, Control::DeleteObject, T("map2d.deleteObject", "Delete object"), false, 0,
            T("map2d.tip.deleteObject", "Delete the picked named object (what is painted stays)")).enabled =
            selected != nullptr && selected->kind == EntryKind::Object && selected->defined;
        y += kRowHeight + 6;
        LayoutSheet(sheetTexture_, setting.cellSize, left, right, y, bottom);
    }

    void LayoutSheet(const std::string& texture, std::uint32_t cellSize, int left, int right, int top, int bottom) {
        const SheetImage* image = Image(texture);
        if (image == nullptr || cellSize == 0U || image->width < cellSize || image->height < cellSize) return;
        sheet_.visible = true;
        sheet_.texture = texture;
        sheet_.cellSize = cellSize;
        auto layout = library_.SheetSetting(texture); layout.cellSize = cellSize;
        sheet_.columns = layout.Count(image->width, cellSize);
        sheet_.rows = layout.Count(image->height, layout.Height());
        if (sheet_.columns == 0U || sheet_.rows == 0U) { sheet_.visible = false; return; }
        sheet_.rect = RECT{ left, top, right, bottom };
        sheet_.cellPixels = std::clamp((right - left) / static_cast<int>(sheet_.columns), 1, 48);
        const int content = static_cast<int>(sheet_.rows) * sheet_.cellPixels;
        sheetScroll_ = std::clamp(sheetScroll_, 0, std::max(0, content - Height(sheet_.rect)));
    }

    [[nodiscard]] static std::wstring PieceTip(Map2DBorderPiece piece) {
        switch (piece) {
        case Map2DBorderPiece::N: return T("map2d.piece.n", "Edge where the ground lies north (above)");
        case Map2DBorderPiece::E: return T("map2d.piece.e", "Edge where the ground lies east (right)");
        case Map2DBorderPiece::S: return T("map2d.piece.s", "Edge where the ground lies south (below)");
        case Map2DBorderPiece::W: return T("map2d.piece.w", "Edge where the ground lies west (left)");
        case Map2DBorderPiece::CornerNW: return T("map2d.piece.cnw", "Small corner: the ground only up-left");
        case Map2DBorderPiece::CornerNE: return T("map2d.piece.cne", "Small corner: the ground only up-right");
        case Map2DBorderPiece::CornerSE: return T("map2d.piece.cse", "Small corner: the ground only down-right");
        case Map2DBorderPiece::CornerSW: return T("map2d.piece.csw", "Small corner: the ground only down-left");
        case Map2DBorderPiece::DiagonalNW: return T("map2d.piece.dnw", "Inner corner: the ground above and left");
        case Map2DBorderPiece::DiagonalNE: return T("map2d.piece.dne", "Inner corner: the ground above and right");
        case Map2DBorderPiece::DiagonalSE: return T("map2d.piece.dse", "Inner corner: the ground below and right");
        case Map2DBorderPiece::DiagonalSW: return T("map2d.piece.dsw", "Inner corner: the ground below and left");
        case Map2DBorderPiece::Count: break;
        }
        return {};
    }

    // ----- Tutorial -----

    struct TutorialText {
        std::wstring title;
        std::wstring body;
    };

    [[nodiscard]] static TutorialText TutorialStep(int step) {
        switch (step) {
        case 0:
            return { T("map2d.tutorial.1.title", "Welcome to the Map Editor"),
                T("map2d.tutorial.1.body", "This is your map, seen from above. On the left is the palette: everything you can paint. "
                    "Click a tile in the palette, then click on the map. That is all you need to start.") };
        case 1:
            return { T("map2d.tutorial.2.title", "Painting"),
                T("map2d.tutorial.2.body", "Click or drag on the map to paint. Ctrl+click erases. Turn the mouse wheel to zoom and hold "
                    "the middle button to move around. Made a mistake? Ctrl+Z.") };
        case 2:
            return { T("map2d.tutorial.3.title", "Your sprites"),
                T("map2d.tutorial.import", "Press Add graphics or drop PNG images into the window. Check the tile size, then press Paint map. "
                    "Your original files stay safe. Use the search box or category list to find a tile.") };
        case 3:
            return { T("map2d.tutorial.4.title", "A ground with edges"),
                T("map2d.tutorial.4.body", "Double-click a floor tile (grass, sand) in the palette: a board of 25 black squares opens "
                    "with the ground in the middle. Click a black square, then the edge sprite for it on the left. Done, and paint.") };
        default:
            return { T("map2d.tutorial.5.title", "Automatic edges"),
                T("map2d.tutorial.5.body", "Auto edges draws where grounds meet for you, as in Remere's Map Editor. Turn it off (A) to "
                    "lay edges by hand. Ctrl+S saves; the scene shows every stroke at once. The ? button brings this tutorial back.") };
        }
    }

    // The part of the window a tutorial step points at.
    [[nodiscard]] RECT TutorialSpot(int step) const {
        const auto control = [this](Control id) {
            for (const ControlBox& box : controls_) {
                if (box.id == id) return box.rect;
            }
            return RECT{};
        };
        const RECT palette = PaletteRect();
        switch (step) {
        case 0: return palette;
        case 1: return CanvasRect();
        case 2: return RECT{ palette.left, palette.top, palette.right, gridRect_.bottom };
        case 3: {
            RECT make = control(Control::PaletteMenu);
            const RECT edit = control(Control::PaletteMenu);
            UnionRect(&make, &make, &edit);
            InflateRect(&make, 6, 6);
            return make;
        }
        default: {
            RECT automagic = control(Control::ViewMenu);
            InflateRect(&automagic, 6, 6);
            return automagic;
        }
        }
    }

    void LayoutTutorial() {
        const int width = 560;
        const int height = 520;
        const RECT canvas = CanvasRect();
        const int clientRight = static_cast<int>(client_.right);
        const int clientBottom = static_cast<int>(client_.bottom);
        const int left = std::clamp(static_cast<int>(canvas.left + canvas.right) / 2 - width / 2, 8, std::max(8, clientRight - width - 8));
        const int top = std::clamp(static_cast<int>(canvas.top + canvas.bottom) / 2 - height / 2, toolbarHeight_ + 8,
            std::max(toolbarHeight_ + 8, clientBottom - height - 8));
        tutorialCard_ = RECT{ left, top, left + width, top + height };
        const int buttonTop = tutorialCard_.bottom - 16 - 34;
        Add(RECT{ tutorialCard_.left + 20, buttonTop, tutorialCard_.left + 130, buttonTop + 34 }, Control::TutorialSkip,
            T("map2d.tutorial.skip", "Skip"), false, 0, T("map2d.tip.tutorialSkip", "Close the tutorial (Esc); ? brings it back"));
        const bool last = tutorialStep_ == kTutorialSteps - 1;
        Add(RECT{ tutorialCard_.right - 20 - 170, buttonTop, tutorialCard_.right - 20, buttonTop + 34 }, Control::TutorialNext,
            last ? T("map2d.tutorial.start", "Start painting") : T("map2d.tutorial.next", "Next") + L"  \u2192", true, 0,
            T("map2d.tip.tutorialNext", "Next (Enter)"));
        Add(RECT{ tutorialCard_.right - 20 - 170 - 8 - 110, buttonTop, tutorialCard_.right - 20 - 170 - 8, buttonTop + 34 }, Control::TutorialBack,
            L"\u2190  " + T("map2d.tutorial.back", "Back"), false, 0, T("map2d.tip.tutorialBack", "Back")).enabled = tutorialStep_ > 0;
    }

    void FinishTutorial() {
        tutorialStep_ = -1;
        if (driven_) return;
        std::error_code error;
        std::filesystem::create_directories(TutorialSeenFile().parent_path(), error);
        std::ofstream{ TutorialSeenFile() } << "seen\n";
    }

    // ----- Drawing -----

    [[nodiscard]] bool Render() {
        if (!surface_.Ensure(std::max(1, Width(client_)), std::max(1, Height(client_)))) return false;
        Layout();
        HDC dc = surface_.Dc();
        Fill(dc, client_, kBackground);
        DrawCanvas(dc);
        DrawPalette(dc);
        DrawToolbar(dc);
        DrawStatus(dc);
        if (!editName_.empty()) DrawBoard(dc);
        if (tutorialStep_ >= 0) DrawTutorial(dc);
        if (polygonEditing_) DrawPolygonEditor(dc);
        if (animationEditing_) DrawAnimationEditor(dc);
        if (form_.IsOpen()) form_.Paint(dc, CanvasRect(), font_);
        GdiFlush();
        return true;
    }

    static void Fill(HDC dc, const RECT& rect, COLORREF color) {
        SetDCBrushColor(dc, color);
        FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }

    static void Frame(HDC dc, const RECT& rect, COLORREF color, int thickness = 1) {
        for (int step = 0; step < thickness; ++step) {
            const RECT inner{ rect.left + step, rect.top + step, rect.right - step, rect.bottom - step };
            SetDCBrushColor(dc, color);
            FrameRect(dc, &inner, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
    }

    void Text(HDC dc, const RECT& rect, std::wstring_view text, COLORREF color, UINT format, HFONT font = nullptr) const {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        const HGDIOBJ previous = SelectObject(dc, font != nullptr ? font : font_);
        RECT box = rect;
        DrawTextW(dc, text.data(), static_cast<int>(text.size()), &box, format | DT_NOPREFIX | ((format & DT_WORDBREAK) != 0U ? 0U : DT_END_ELLIPSIS));
        SelectObject(dc, previous);
    }

    void DrawButton(HDC dc, const ControlBox& box) const {
        const bool hot = box.enabled && hot_ == box.id && hotParam_ == box.param && box.id != Control::None;
        Fill(dc, box.rect, box.active ? kButtonActive : (hot ? kButtonHot : kButton));
        Frame(dc, box.rect, box.active ? kAccent : kBorder);
        Text(dc, box.rect, box.label, box.enabled || box.id == Control::None ? kText : kTextDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void DrawSprite(Map2DSprite sprite, int x, int y, int size, const RECT& clip, std::uint32_t alpha, std::uint32_t shade) {
        std::uint32_t left = 0U;
        std::uint32_t top = 0U;
        std::uint32_t cell = 0U;
        std::uint32_t cellHeight = 0U;
        const auto& source = drawingLayer_ ? *drawingLayer_ : map_;
        sprite = kb::assets::Map2DAnimatedSprite(source, sprite, previewSeconds_);
        if (!kb::assets::Map2DSpriteRect(source, sprite, left, top, cell, cellHeight)) return;
        const SheetImage* image = Image(source.sheets[sprite.sheet].texture);
        int width = size, height = std::max(1, static_cast<int>(static_cast<float>(size) * static_cast<float>(cellHeight) / static_cast<float>(cell)));
        if (sprite.rotation % 2U) std::swap(width, height);
        x += (size - width) / 2 + static_cast<int>(std::lround(sprite.offsetX * static_cast<float>(size)));
        y += size - height + static_cast<int>(std::lround(sprite.offsetY * static_cast<float>(size)));
        if (image != nullptr) surface_.Blit(clip, x, y, width, height, *image, left, top, cell, cellHeight, alpha, shade, sprite);
    }

    void DrawTile(const Map2DTile& tile, int x, int y, const RECT& clip, std::uint32_t alpha, std::uint32_t shade) {
        if (tile.ground.IsValid()) DrawSprite(tile.ground, x, y, zoom_, clip, alpha, shade);
        for (const Map2DSprite sprite : tile.borders) DrawSprite(sprite, x, y, zoom_, clip, alpha, shade);
        for (const Map2DSprite sprite : tile.items) DrawSprite(sprite, x, y, zoom_, clip, alpha, shade);
        if (!tile.prefab.empty()) {
            const RECT marker{x, y, x + zoom_, y + zoom_};
            Frame(surface_.Dc(), marker, kObjectMark);
            Text(surface_.Dc(), marker, L"P", kObjectMark, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    void DrawCell(const std::string& texture, std::uint32_t cellSize, std::uint32_t cell, const RECT& target, const RECT& clip, std::uint32_t alpha = 256U) {
        const SheetImage* image = Image(texture);
        if (image == nullptr || cellSize == 0U || image->width < cellSize) return;
        auto layout = library_.SheetSetting(texture); layout.cellSize = cellSize;
        const auto columns = layout.Count(image->width, cellSize);
        if (columns == 0U || cell >= columns * layout.Count(image->height, layout.Height())) return;
        const float scale = std::min(static_cast<float>(Width(target)) / cellSize, static_cast<float>(Height(target)) / layout.Height());
        const int width = std::max(1, static_cast<int>(cellSize * scale)), height = std::max(1, static_cast<int>(layout.Height() * scale));
        surface_.Blit(clip, target.left + (Width(target) - width) / 2, target.top + (Height(target) - height) / 2, width, height, *image,
            layout.margin + (cell % columns) * (cellSize + layout.spacing), layout.margin + (cell / columns) * (layout.Height() + layout.spacing), cellSize, layout.Height(), alpha);
    }

    // A palette entry drawn fitted into `box`: every sprite of its block in its place.
    void DrawEntry(const PaletteEntry& entry, const RECT& box, std::uint32_t alpha = 256U) {
        if (!entry.prefab.empty()) {
            Text(surface_.Dc(), box, L"P", kObjectMark, DT_CENTER | DT_VCENTER | DT_SINGLELINE, titleFont_);
            return;
        }
        if (entry.stamp) {
            const auto* previous = drawingLayer_;
            drawingLayer_ = entry.stamp.get();
            const int cell = std::max(1, std::min(Width(box) / static_cast<int>(entry.columns), Height(box) / static_cast<int>(entry.rows)));
            for (const auto& [key, tile] : entry.stamp->tiles) {
                const int x = box.left + static_cast<int>(kb::assets::Map2DTileKeyX(key)) * cell;
                const int y = box.top + static_cast<int>(kb::assets::Map2DTileKeyY(key)) * cell;
                if (tile.ground.IsValid()) DrawSprite(tile.ground, x, y, cell, box, alpha, 256U);
                for (auto sprite : tile.borders) DrawSprite(sprite, x, y, cell, box, alpha, 256U);
                for (auto sprite : tile.items) DrawSprite(sprite, x, y, cell, box, alpha, 256U);
            }
            drawingLayer_ = previous;
            return;
        }
        const int inner = std::min(Width(box), Height(box));
        const int cell = std::max(1, inner / static_cast<int>(std::max(entry.columns, entry.rows)));
        const int blockWidth = cell * static_cast<int>(entry.columns);
        const int blockHeight = cell * static_cast<int>(entry.rows);
        const int left = box.left + (Width(box) - blockWidth) / 2;
        const int top = box.top + (Height(box) - blockHeight) / 2;
        for (const auto& [column, row] : entry.cells) {
            const RECT target{ left + static_cast<int>(column) * cell, top + static_cast<int>(row) * cell,
                left + static_cast<int>(column + 1U) * cell, top + static_cast<int>(row + 1U) * cell };
            DrawCell(entry.texture, entry.cellSize, entry.CellAt(column, row), target, box, alpha);
        }
    }

    [[nodiscard]] std::vector<std::int32_t> FloorsPresent(const Map2DAsset* layer = nullptr) const {
        const auto& source = layer ? *layer : map_;
        std::vector<std::int32_t> floors;
        auto it = source.tiles.begin();
        while (it != source.tiles.end()) {
            const std::int32_t z = kb::assets::Map2DTileKeyZ(it->first);
            floors.push_back(z);
            if (z >= Map2DAsset::MaximumFloor) break;
            it = source.tiles.lower_bound(Map2DTileKey(0U, 0U, z + 1));
        }
        return floors;
    }

    std::vector<const Map2DAsset*> VisibleLayers() {
        const auto& scene = std::as_const(context_.Scene());
        const auto parent = scene.Hierarchy().Parent(entity_);
        const auto entities = parent.IsValid() && scene.Components().Grids().Has(parent)
            ? scene.Hierarchy().ChildEntities(parent) : std::vector<kb::scene::SceneEntity>{ entity_ };
        std::vector<std::pair<std::int32_t, const Map2DAsset*>> ordered;
        for (const auto entity : entities) {
            const auto* settings = scene.Components().TilemapRenderers().TryGet(entity);
            if (settings && !settings->enabled) continue;
            const auto* metadata = EditorMap2DService::MapMetadata(scene, entity);
            if (!metadata) continue;
            const Map2DAsset* source = entity == entity_ ? &map_ : nullptr;
            for (const auto& weak : WorkingCopies()) {
                if (auto copy = weak.lock(); copy && copy->context == &context_ && copy->asset == metadata->id) {
                    source = &copy->map;
                    break;
                }
            }
            if (!source) {
                auto& cached = layerCache_[metadata->id.value];
                if (!cached.second || cached.first != metadata->contentHash) {
                    cached.second = kb::assets::Map2DAssetIO::Load(metadata->physicalPath);
                    cached.first = metadata->contentHash;
                }
                if (cached.second) source = &*cached.second;
            }
            if (source && source->orientation == map_.orientation) ordered.emplace_back(settings ? settings->sortingOrder : 0, source);
        }
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<const Map2DAsset*> result;
        for (const auto& [order, source] : ordered) { static_cast<void>(order); result.push_back(source); }
        return result;
    }

    [[nodiscard]] int OriginX() const noexcept { return CanvasRect().left - static_cast<int>(std::lround(panX_)); }
    [[nodiscard]] int OriginY() const noexcept { return CanvasRect().top - static_cast<int>(std::lround(panY_)); }
    [[nodiscard]] RECT TileRect(int x0, int y0, int x1, int y1) const noexcept {
        return RECT{ OriginX() + x0 * zoom_, OriginY() + y0 * zoom_, OriginX() + (x1 + 1) * zoom_, OriginY() + (y1 + 1) * zoom_ };
    }
    [[nodiscard]] bool HoverInside() const noexcept {
        return hoverX_ >= 0 && hoverY_ >= 0 && hoverX_ < static_cast<int>(map_.width) && hoverY_ < static_cast<int>(map_.height);
    }

    void DrawCanvas(HDC dc) {
        const RECT canvas = CanvasRect();
        Fill(dc, canvas, kVoid);
        const int originX = OriginX();
        const int originY = OriginY();
        const RECT mapRect{ originX, originY, originX + static_cast<int>(map_.width) * zoom_, originY + static_cast<int>(map_.height) * zoom_ };
        RECT visible{};
        if (IntersectRect(&visible, &mapRect, &canvas) == FALSE) return;
        Fill(dc, visible, kMapBase);
        GdiFlush();
        const int tx0 = std::max(0, FloorDivide(visible.left - originX, zoom_));
        const int ty0 = std::max(0, FloorDivide(visible.top - originY, zoom_));
        const int tx1 = std::min(static_cast<int>(map_.width) - 1, FloorDivide(visible.right - 1 - originX, zoom_));
        const int ty1 = std::min(static_cast<int>(map_.height) - 1, FloorDivide(visible.bottom - 1 - originY, zoom_));
        const bool moving = moving_ && HoverInside();
        const int moveX = moving ? hoverX_ - dragStartX_ : 0;
        const int moveY = moving ? hoverY_ - dragStartY_ : 0;
        for (const auto* layer : VisibleLayers()) {
          drawingLayer_ = layer;
          const int layerX = originX + (static_cast<int>(map_.width) - static_cast<int>(layer->width)) * zoom_ / 2;
          const int layerY = originY + (static_cast<int>(map_.height) - static_cast<int>(layer->height)) * zoom_ /
              (map_.orientation == kb::assets::Map2DOrientation::Wall ? 1 : 2);
          const int left = std::max(0, FloorDivide(visible.left - layerX, zoom_));
          const int right = std::min(static_cast<int>(layer->width) - 1, FloorDivide(visible.right - 1 - layerX, zoom_));
          const int top = std::max(0, FloorDivide(visible.top - layerY, zoom_));
          const int bottom = std::min(static_cast<int>(layer->height) - 1, FloorDivide(visible.bottom - 1 - layerY, zoom_));
          if (left > right || top > bottom) continue;
          for (const std::int32_t z : FloorsPresent(layer)) {
            // Floors below show under this one (shaded, Q), floors above only as ghosts (Ctrl+L).
            if (z < floor_ && !allFloors_) continue;
            if (z > floor_ && !ghostHigher_) continue;
            const std::uint32_t alpha = z > floor_ ? 90U : 256U;
            const std::uint32_t shade = z < floor_ && shade_ ? 150U : 256U;
            for (int ty = top; ty <= bottom; ++ty) {
                auto it = layer->tiles.lower_bound(Map2DTileKey(static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(ty), z));
                const auto end = layer->tiles.upper_bound(Map2DTileKey(static_cast<std::uint32_t>(right), static_cast<std::uint32_t>(ty), z));
                for (; it != end; ++it) {
                    const int tileX = static_cast<int>(kb::assets::Map2DTileKeyX(it->first));
                    // Tiles being moved draw where they are going, not where they were.
                    if (layer == &map_ && moving && z == floor_ && InSelection(tileX, ty)) continue;
                    DrawTile(it->second, layerX + tileX * zoom_, layerY + ty * zoom_, visible, alpha, shade);
                }
            }
          }
        }
        drawingLayer_ = nullptr;
        if (moving) {
            for (const auto& [offset, tile] : CopySelection().tiles) {
                const int tileX = static_cast<int>(selection_.x0) + static_cast<int>(offset.first) + moveX;
                const int tileY = static_cast<int>(selection_.y0) + static_cast<int>(offset.second) + moveY;
                DrawTile(tile, originX + tileX * zoom_, originY + tileY * zoom_, visible, 220U, 256U);
            }
        }
        if (pasting_ && HoverInside()) {
            for (const auto& [offset, tile] : clipboard_.tiles) {
                DrawTile(tile, originX + (hoverX_ + static_cast<int>(offset.first)) * zoom_, originY + (hoverY_ + static_cast<int>(offset.second)) * zoom_,
                    visible, 170U, 256U);
            }
        }
        RECT brushRect{};
        const bool showBrush = HoverInside() && !panning_ && !pasting_ && tool_ != Tool::Select && tutorialStep_ < 0 && editName_.empty();
        if (showBrush) brushRect = DrawBrushPreview(visible);
        if (tool_ == Tool::Collision) {
            const int savedDc = SaveDC(dc);
            IntersectClipRect(dc, visible.left, visible.top, visible.right, visible.bottom);
            for (int y = ty0; y <= ty1; ++y) {
                auto it = map_.tiles.lower_bound(Map2DTileKey(static_cast<std::uint32_t>(tx0), static_cast<std::uint32_t>(y), floor_));
                const auto end = map_.tiles.upper_bound(Map2DTileKey(static_cast<std::uint32_t>(tx1), static_cast<std::uint32_t>(y), floor_));
                for (; it != end; ++it) {
                    const auto shape = kb::assets::Map2DCellCollision(map_, it->second);
                    if (shape.kind == kb::assets::Map2DCollisionKind::None) continue;
                    const int x = static_cast<int>(kb::assets::Map2DTileKeyX(it->first));
                    const RECT rect = TileRect(x, y, x, y);
                    auto points = shape.polygon;
                    using Kind = kb::assets::Map2DCollisionKind;
                    if (shape.kind == Kind::SlopeUp) points = {{0,1},{1,0},{1,1}};
                    else if (shape.kind == Kind::SlopeDown) points = {{0,0},{1,1},{0,1}};
                    else if (shape.kind == Kind::OneWay) points = {{0,0},{1,0}};
                    else if (shape.kind == Kind::Solid) points = {{0,0},{1,0},{1,1},{0,1}};
                    const auto pen = SelectObject(dc, GetStockObject(DC_PEN)); SetDCPenColor(dc, shape.trigger ? kObjectMark : kWarning);
                    for (std::size_t i = 0; i <= points.size() && !points.empty(); ++i) {
                        const auto point = points[i % points.size()];
                        const int px = rect.left + static_cast<int>(point[0] * (Width(rect)-1)), py = rect.top + static_cast<int>(point[1] * (Height(rect)-1));
                        if (i == 0) MoveToEx(dc, px, py, nullptr); else LineTo(dc, px, py);
                    }
                    SelectObject(dc, pen);
                    if (shape.trigger) Text(dc, rect, L"T", kObjectMark, DT_CENTER | DT_VCENTER | DT_SINGLELINE, boldFont_);
                }
            }
            RestoreDC(dc, savedDc);
        }
        if (grid_ && zoom_ >= 8) {
            SetDCPenColor(dc, kGridLine);
            const HGDIOBJ previous = SelectObject(dc, GetStockObject(DC_PEN));
            for (int tx = tx0; tx <= tx1 + 1; ++tx) {
                MoveToEx(dc, originX + tx * zoom_, visible.top, nullptr);
                LineTo(dc, originX + tx * zoom_, visible.bottom);
            }
            for (int ty = ty0; ty <= ty1 + 1; ++ty) {
                MoveToEx(dc, visible.left, originY + ty * zoom_, nullptr);
                LineTo(dc, visible.right, originY + ty * zoom_);
            }
            SelectObject(dc, previous);
        }
        Frame(dc, mapRect, kBorder);
        if (hasSelection_) {
            Frame(dc, TileRect(static_cast<int>(selection_.x0) + moveX, static_cast<int>(selection_.y0) + moveY,
                static_cast<int>(selection_.x1) + moveX, static_cast<int>(selection_.y1) + moveY), kSelection, 2);
        }
        if (selecting_ && HoverInside()) {
            Frame(dc, TileRect(std::min(dragStartX_, hoverX_), std::min(dragStartY_, hoverY_), std::max(dragStartX_, hoverX_), std::max(dragStartY_, hoverY_)), kSelection, 1);
        }
        if (rectActive_ && HoverInside()) {
            Frame(dc, TileRect(std::min(dragStartX_, hoverX_), std::min(dragStartY_, hoverY_), std::max(dragStartX_, hoverX_), std::max(dragStartY_, hoverY_)), kAccent, 2);
        } else if (showBrush) {
            Frame(dc, brushRect, tool_ == Tool::Eraser || Ctrl() ? kWarning : kAccent, 2);
        }
        if (clientBox_) {
            // What a player sees around the middle of the view, as Remere's client box.
            const int centerX = FloorDivide((canvas.left + canvas.right) / 2 - originX, zoom_);
            const int centerY = FloorDivide((canvas.top + canvas.bottom) / 2 - originY, zoom_);
            const int boxX = centerX - static_cast<int>(map_.viewWidth) / 2;
            const int boxY = centerY - static_cast<int>(map_.viewHeight) / 2;
            const RECT box = TileRect(boxX, boxY, boxX + static_cast<int>(map_.viewWidth) - 1, boxY + static_cast<int>(map_.viewHeight) - 1);
            Frame(dc, box, kClientBox, 2);
            Text(dc, RECT{ box.left + 4, box.top + 2, box.right, box.top + 20 },
                std::to_wstring(map_.viewWidth) + L" \u00D7 " + std::to_wstring(map_.viewHeight), kClientBox, DT_LEFT | DT_SINGLELINE);
        }
        if (map_.tiles.empty() && tutorialStep_ < 0 && editName_.empty()) {
            // An empty map says what to do.
            Text(dc, RECT{ canvas.left + 40, canvas.bottom - 60, canvas.right - 40, canvas.bottom - 20 },
                entries_.empty()
                    ? T("map2d.startImport", "Start with Add graphics, or drop PNG images here. Then pick a tile and paint.")
                    : T("map2d.empty", "Pick a tile in the palette on the left, then click or drag here to paint."),
                kTextDim, DT_CENTER | DT_SINGLELINE, boldFont_);
        }
    }

    // The tile the brush paints from under the cursor and what it covers.
    [[nodiscard]] RECT BrushArea(int& left, int& top, int& columns, int& rows) const {
        const PaletteEntry* entry = Selected();
        if (entry != nullptr && entry->kind == EntryKind::Object && tool_ == Tool::Draw && !Ctrl()) {
            const auto [x, y] = kb::world2d::Map2DObjectOrigin(entry->columns, entry->rows, hoverX_, hoverY_);
            left = static_cast<int>(x);
            top = static_cast<int>(y);
            columns = static_cast<int>(entry->columns);
            rows = static_cast<int>(entry->rows);
        } else {
            const int half = rectActive_ || tool_ == Tool::Fill || tool_ == Tool::Picker ? 0 : brushSize_ / 2;
            left = hoverX_ - half;
            top = hoverY_ - half;
            columns = rows = 2 * half + 1;
        }
        return TileRect(left, top, left + columns - 1, top + rows - 1);
    }

    // What the brush would paint, faint under the cursor; returns its outline.
    [[nodiscard]] RECT DrawBrushPreview(const RECT& visible) {
        int left = 0;
        int top = 0;
        int columns = 1;
        int rows = 1;
        const RECT outline = BrushArea(left, top, columns, rows);
        const PaletteEntry* entry = Selected();
        if (entry == nullptr || tool_ == Tool::Eraser || tool_ == Tool::Picker || tool_ == Tool::Collision || Ctrl()) return outline;
        if (entry->kind == EntryKind::Object) {
            for (const auto& [column, row] : entry->cells) {
                const RECT target = TileRect(left + static_cast<int>(column), top + static_cast<int>(row), left + static_cast<int>(column), top + static_cast<int>(row));
                DrawCell(entry->texture, entry->cellSize, entry->CellAt(column, row), target, visible, 160U);
            }
            return outline;
        }
        const Map2DGroundBrush* ground = entry->kind == EntryKind::Ground ? library_.Find(entry->name) : nullptr;
        for (int dy = 0; dy < rows; ++dy) {
            for (int dx = 0; dx < columns; ++dx) {
                const int tileX = left + dx;
                const int tileY = top + dy;
                if (tileX < 0 || tileY < 0 || tileX >= static_cast<int>(map_.width) || tileY >= static_cast<int>(map_.height)) continue;
                const std::uint32_t cell = ground != nullptr && !ground->tiles.empty()
                    ? ground->VariantFor(static_cast<std::uint32_t>(tileX), static_cast<std::uint32_t>(tileY), floor_)
                    : entry->firstCell;
                DrawCell(entry->texture, entry->cellSize, cell, TileRect(tileX, tileY, tileX, tileY), visible, 150U);
            }
        }
        return outline;
    }

    void DrawPalette(HDC dc) {
        const RECT palette = PaletteRect();
        Fill(dc, palette, kPanel);
        Fill(dc, RECT{ palette.right, palette.top, palette.right + 1, palette.bottom }, kBorder);
        if (Width(searchRect_) > 0) {
            Fill(dc, searchRect_, kTile);
            Frame(dc, searchRect_, kBorder);
            if (searchEdit_ == nullptr || tutorialStep_ >= 0 || IsWindowVisible(searchEdit_) == FALSE) {
                Text(dc, RECT{ searchRect_.left + 10, searchRect_.top, searchRect_.right - 8, searchRect_.bottom },
                    search_.empty() ? T("map2d.search", "Search the palette (Ctrl+F)") : search_, search_.empty() ? kTextDim : kText,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }
        if (Width(typeRect_) > 0) DrawFace(dc, typeRect_, Control::TypeList, TypeParts(paletteType_), categoryOpen_ && listIsType_);
        if (Width(categoryRect_) > 0) DrawFace(dc, categoryRect_, Control::CategoryList, CategoryParts(category_), categoryOpen_ && !listIsType_);
        if (panel_ == Panel::Palette) {
            Fill(dc, gridRect_, kVoid);
            GdiFlush();
        }
        const std::vector<const PaletteEntry*> visible = panel_ == Panel::Palette ? VisibleEntries() : std::vector<const PaletteEntry*>{};
        for (const ControlBox& box : controls_) {
            if (box.rect.left >= palette.right || box.rect.top < palette.top || Contains(tutorialCard_, box.rect.left + 1, box.rect.top + 1) &&
                    (box.id == Control::TutorialBack || box.id == Control::TutorialNext || box.id == Control::TutorialSkip)) {
                continue;
            }
            if (box.id == Control::PaletteTile) {
                DrawPaletteTile(dc, box, *visible[static_cast<std::size_t>(box.param)]);
                continue;
            }
            if (box.id == Control::TypeList || box.id == Control::CategoryList || box.id == Control::CategoryItem) continue;
            DrawButton(dc, box);
        }
        if (panel_ == Panel::Palette) {
            if (visible.empty()) {
                Text(dc, RECT{ gridRect_.left + 12, gridRect_.top + 20, gridRect_.right - 12, gridRect_.bottom },
                    !editName_.empty() ? T("map2d.board.allUsed", "Every sprite of this sheet is already on the board. Right click a square to free its sprite.")
                    : entries_.empty()
                        ? T("map2d.importEmpty", "Add your graphics above, or drop PNG images into this window.")
                        : T("map2d.paletteNothing", "Nothing matches. Try another category or clear the search."),
                    kTextDim, DT_LEFT | DT_WORDBREAK);
            }
            const PaletteEntry* selected = Selected();
            Text(dc, RECT{ palette.left + kPad, infoTop_, palette.right - kPad, infoTop_ + 20 },
                selected != nullptr ? Wide(selected->name) : T("map2d.nothingPicked", "Nothing picked"), kText, DT_LEFT | DT_SINGLELINE, boldFont_);
            Text(dc, RECT{ palette.left + kPad, infoTop_ + 19, palette.right - kPad, infoTop_ + 38 },
                selected != nullptr ? EntryKindText(*selected) : T("map2d.pickHint", "Click a tile above"), kTextDim, DT_LEFT | DT_SINGLELINE, smallFont_);
        } else if (panel_ == Panel::Sheet) {
            Text(dc, RECT{ palette.left + kPad, sizeLabelTop_, palette.left + kPad + 90, sizeLabelTop_ + kRowHeight },
                T("map2d.spriteSize", "Sprite size"), kTextDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE, smallFont_);
            Text(dc, RECT{ palette.left + kPad, guideTop_, palette.right - kPad, guideTop_ + 52 },
                T("map2d.guideObject", "Drag over all the sprites of one object (a tree, a house) below, then press Save as object."),
                kText, DT_LEFT | DT_WORDBREAK, smallFont_);
        }
        DrawSheet(dc);
        if (categoryOpen_) DrawCategoryList(dc);
    }

    [[nodiscard]] std::pair<std::wstring, std::wstring> CategoryParts(const std::string& key) const {
        if (key == kRecentCategory) return { T("map2d.recent", "Recently used"), std::to_wstring(recent_.size()) };
        const auto count = std::ranges::count_if(entries_, [&](const PaletteEntry& entry) { return OfType(entry) && (key == kAllCategory || entry.category == key); });
        return { key == kAllCategory ? T("map2d.all", "Everything") : Wide(CategoryText(key)), std::to_wstring(count) };
    }

    [[nodiscard]] std::pair<std::wstring, std::wstring> TypeParts(const std::string& key) const {
        const auto count = [&](auto test) { return std::to_wstring(std::ranges::count_if(entries_, test)); };
        if (key == "grounds") {
            return { T("map2d.type.grounds", "Grounds"), count([](const PaletteEntry& e) { return e.kind == EntryKind::Ground || e.kind == EntryKind::Floor; }) };
        }
        if (key == "objects") return { T("map2d.type.objects", "Objects"), count([](const PaletteEntry& e) { return e.kind == EntryKind::Object; }) };
        if (key == "items") return { T("map2d.type.items", "Items"), count([](const PaletteEntry& e) { return e.kind == EntryKind::Item; }) };
        return { T("map2d.type.all", "Everything"), std::to_wstring(entries_.size()) };
    }

    // A small chevron, pointing down (or up while the list is open).
    void Chevron(HDC dc, int x, int y, bool up, COLORREF color) const {
        SetDCPenColor(dc, color);
        const HGDIOBJ previous = SelectObject(dc, GetStockObject(DC_PEN));
        const int d = up ? -1 : 1;
        for (int line = 0; line < 2; ++line) {
            MoveToEx(dc, x - 4, y - 2 * d + line, nullptr);
            LineTo(dc, x, y + 2 * d + line);
            LineTo(dc, x + 5, y - 3 * d + line);
        }
        SelectObject(dc, previous);
    }

    void DrawFace(HDC dc, const RECT& face, Control id, const std::pair<std::wstring, std::wstring>& parts, bool open) {
        const bool hot = hot_ == id;
        Fill(dc, face, hot || open ? kButtonHot : kButton);
        Frame(dc, face, open ? kAccent : (hot ? kTextDim : kBorder));
        Text(dc, RECT{ face.left + 12, face.top, face.right - 70, face.bottom }, parts.first, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE, boldFont_);
        Text(dc, RECT{ face.right - 80, face.top, face.right - 30, face.bottom }, parts.second, kTextDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, smallFont_);
        Chevron(dc, face.right - 15, (face.top + face.bottom) / 2, open, hot || open ? kText : kTextDim);
    }

    void DrawCategoryList(HDC dc) {
        const RECT& list = categoryListRect_;
        // A soft shadow under the list, then its panel.
        GdiFlush();
        surface_.Shade(RECT{ list.left + 3, list.bottom, list.right + 3, list.bottom + 5 }, 120U);
        surface_.Shade(RECT{ list.right, list.top + 4, list.right + 4, list.bottom + 5 }, 120U);
        Fill(dc, list, RGB(24, 30, 41));
        Frame(dc, list, kBorder);
        const std::vector<std::string> keys = ListKeys();
        for (const ControlBox& box : controls_) {
            if (box.id != Control::CategoryItem) continue;
            const std::string& key = keys[static_cast<std::size_t>(box.param)];
            const bool current = key == ListCurrent();
            const bool hot = (hot_ == Control::CategoryItem && hotParam_ == box.param) || categoryKey_ == box.param;
            if (hot) Fill(dc, box.rect, kButtonHot);
            if (current) Fill(dc, RECT{ box.rect.left, box.rect.top + 5, box.rect.left + 3, box.rect.bottom - 5 }, kAccent);
            const auto [name, count] = listIsType_ ? TypeParts(key) : CategoryParts(key);
            Text(dc, RECT{ box.rect.left + 12, box.rect.top, box.rect.right - 44, box.rect.bottom }, name, current ? kAccent : kText,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE, current ? boldFont_ : font_);
            Text(dc, RECT{ box.rect.right - 48, box.rect.top, box.rect.right - 8, box.rect.bottom }, count, kTextDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, smallFont_);
            if (!listIsType_ && box.param == 1) Fill(dc, RECT{ list.left + 10, box.rect.bottom + 3, list.right - 10, box.rect.bottom + 4 }, kBorder);
        }
        // A thin scroll bar when not every category fits.
        const int count = static_cast<int>(keys.size());
        if (count > kListRows) {
            const int track = Height(list) - 8;
            const int thumb = std::max(24, track * kListRows / count);
            const int top = list.top + 4 + (track - thumb) * categoryFirst_ / std::max(1, count - kListRows);
            Fill(dc, RECT{ list.right - 6, top, list.right - 3, top + thumb }, kBorder);
        }
    }

    void DrawPaletteTile(HDC dc, const ControlBox& box, const PaletteEntry& entry) {
        const bool hot = hot_ == Control::PaletteTile && hotParam_ == box.param;
        Fill(dc, box.rect, box.active ? RGB(28, 42, 66) : (hot ? kButtonHot : kTile));
        GdiFlush();
        const RECT inner{ box.rect.left + 3, box.rect.top + 3, box.rect.right - 3, box.rect.bottom - 3 };
        DrawEntry(entry, inner);
        // A ground draws edges (green corner), an object covers several tiles (amber corner).
        if (entry.kind == EntryKind::Ground || entry.kind == EntryKind::Object) {
            const COLORREF mark = entry.kind == EntryKind::Ground ? kGroundMark : kObjectMark;
            const POINT corner[3]{ { box.rect.right - 1, box.rect.top }, { box.rect.right - 1, box.rect.top + 11 }, { box.rect.right - 12, box.rect.top } };
            SetDCBrushColor(dc, mark);
            SetDCPenColor(dc, mark);
            const HGDIOBJ brush = SelectObject(dc, GetStockObject(DC_BRUSH));
            const HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
            Polygon(dc, corner, 3);
            SelectObject(dc, brush);
            SelectObject(dc, pen);
        }
        Frame(dc, box.rect, box.active ? kAccent : (hot ? kTextDim : kBorder), box.active ? 2 : 1);
    }

    void DrawSheet(HDC dc) {
        if (!sheet_.visible) return;
        const RECT& area = sheet_.rect;
        Fill(dc, area, kVoid);
        GdiFlush();
        const SheetImage* image = Image(sheet_.texture);
        if (image == nullptr) return;
        const int cell = sheet_.cellPixels;
        const int width = static_cast<int>(sheet_.columns) * cell;
        const int height = static_cast<int>(sheet_.rows) * cell;
        const int top = area.top - sheetScroll_;
        for (std::uint32_t row = 0U; row < sheet_.rows; ++row) {
            const int y = top + static_cast<int>(row) * cell;
            if (y + cell < area.top || y >= area.bottom) continue;
            for (std::uint32_t column = 0U; column < sheet_.columns; ++column) {
                const int x = area.left + static_cast<int>(column) * cell;
                DrawCell(sheet_.texture, sheet_.cellSize, row * sheet_.columns + column, RECT{x, y, x + cell, y + cell}, area);
            }
        }
        const HRGN clip = CreateRectRgn(area.left, area.top, area.right, area.bottom);
        SelectClipRgn(dc, clip);
        SetDCPenColor(dc, kGridLine);
        const HGDIOBJ previous = SelectObject(dc, GetStockObject(DC_PEN));
        for (std::uint32_t column = 0U; column <= sheet_.columns; ++column) {
            MoveToEx(dc, area.left + static_cast<int>(column) * cell, top, nullptr);
            LineTo(dc, area.left + static_cast<int>(column) * cell, top + height);
        }
        for (std::uint32_t row = 0U; row <= sheet_.rows; ++row) {
            MoveToEx(dc, area.left, top + static_cast<int>(row) * cell, nullptr);
            LineTo(dc, area.left + width, top + static_cast<int>(row) * cell);
        }
        SelectObject(dc, previous);
        if (sheetPickValid_) {
            const RECT selection{ area.left + static_cast<int>(sheetPick_.left) * cell, top + static_cast<int>(sheetPick_.top) * cell,
                area.left + static_cast<int>(sheetPick_.right + 1) * cell, top + static_cast<int>(sheetPick_.bottom + 1) * cell };
            Frame(dc, selection, kAccent, 2);
        }
        SelectClipRgn(dc, nullptr);
        DeleteObject(clip);
        Frame(dc, area, kBorder);
    }

    void DrawToolbar(HDC dc) {
        const RECT toolbar = ToolbarRect();
        Fill(dc, toolbar, kToolbar);
        Fill(dc, RECT{ 0, toolbar.bottom - 1, toolbar.right, toolbar.bottom }, kBorder);
        for (const ControlBox& box : controls_) {
            if (box.rect.bottom <= toolbarHeight_) DrawButton(dc, box);
        }
    }

    void DrawStatus(HDC dc) {
        const RECT status = StatusRect();
        Fill(dc, status, kToolbar);
        Fill(dc, RECT{ 0, status.top, status.right, status.top + 1 }, kBorder);
        std::wstring left;
        if (HoverInside()) {
            left = TF("map2d.position", "x {x}, y {y}, floor {floor}",
                { { "x", std::to_string(hoverX_) }, { "y", std::to_string(hoverY_) }, { "floor", std::to_string(floor_) } }) + L"   ";
        } else {
            left = TF("map2d.floor", "Floor {floor}", { { "floor", std::to_string(floor_) } }) + L"   ";
        }
        left += TF("map2d.zoom", "Zoom {zoom}%", { { "zoom", std::to_string(zoom_ * 100 / kNormalZoom) } }) + L"   ";
        if (hasSelection_) {
            left += TF("map2d.selection", "Selection {width} x {height}",
                { { "width", std::to_string(selection_.x1 - selection_.x0 + 1U) }, { "height", std::to_string(selection_.y1 - selection_.y0 + 1U) } });
        } else if (const PaletteEntry* entry = Selected(); entry != nullptr) {
            left += Wide(entry->name);
        }
        Text(dc, RECT{ status.left + kPad, status.top, status.left + 470, status.bottom }, left, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        std::wstring right = message_.empty() ? TipUnderCursor() : Wide(message_);
        std::size_t shared = 0;
        struct Count { std::uint64_t id; std::size_t* result; } count{assetId_.value,&shared};
        context_.Scene().Runtime().EcsWorld().CreateQuery<kb::scene::TilemapComponent>().ForEach([](kb::scene::SceneEntity, const kb::scene::TilemapComponent& layer, void* data) { auto& state = *static_cast<Count*>(data); if (layer.mapAssetId == state.id) ++*state.result; },&count);
        if (shared > 1 && !messageIsError_) {
            right = TF("map2d.sharedHint","Shared by {count} layers. More > Make independent",{{"count",std::to_string(shared)}});
        } else if (message_.empty()) {
            if (density_ < 100U) right = TF("map2d.scatterHint","Scatter: {percent}%",{{"percent",std::to_string(density_)}});
            else if (paintInScene_) right = T("map2d.scenePaintActive","Scene View painting enabled");
        }
        Text(dc, RECT{ status.left + 480, status.top, status.right - kPad, status.bottom }, right,
            messageIsError_ && !message_.empty() ? kWarning : kTextDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    void DrawBoard(HDC dc) {
        const Map2DGroundBrush* ground = library_.Find(editName_);
        if (ground == nullptr) return;
        GdiFlush();
        surface_.Shade(CanvasRect(), 80U);
        const RECT& card = boardCard_;
        Fill(dc, RECT{ card.left + 6, card.top + 6, card.right + 6, card.bottom + 6 }, RGB(2, 3, 5));
        Fill(dc, card, kPanel);
        Frame(dc, card, kAccent, 1);
        const std::array<BoardSlot, 25U> slots = BoardSlots();
        const int activePiece = boardCell_ >= 0 ? slots[static_cast<std::size_t>(boardCell_)].piece : -1;
        for (int index = 0; index < 25; ++index) {
            const BoardSlot& slot = slots[static_cast<std::size_t>(index)];
            const RECT cell = BoardCellRect(index);
            Fill(dc, cell, kVoid);
            std::uint32_t sprite = kb::world2d::kMap2DNoCell;
            if (slot.variant >= 0 && !ground->tiles.empty()) {
                sprite = slot.variant < static_cast<int>(ground->tiles.size()) ? ground->tiles[static_cast<std::size_t>(slot.variant)] : ground->tiles.front();
            }
            if (slot.piece >= 0) sprite = ground->borders[static_cast<std::size_t>(slot.piece)];
            if (sprite != kb::world2d::kMap2DNoCell) {
                GdiFlush();
                DrawCell(ground->texture, ground->cellSize, sprite, cell, cell);
            } else if (slot.piece >= 0) {
                Text(dc, cell, L"+", kBorder, DT_CENTER | DT_VCENTER | DT_SINGLELINE, titleFont_);
            }
            const bool hot = hot_ == Control::BoardCell && hotParam_ == index;
            if (index == boardCell_) Frame(dc, cell, kAccent, 3);
            else if (activePiece >= 0 && slot.piece == activePiece) Frame(dc, cell, kAccent, 1);
            else if (slot.variant < 0 && slot.piece < 0) Frame(dc, cell, kGridLine, 1);
            else Frame(dc, cell, hot ? kTextDim : kBorder, 1);
        }
        for (const ControlBox& box : controls_) {
            if (box.id != Control::BoardCell && Contains(card, box.rect.left + 1, box.rect.top + 1)) DrawButton(dc, box);
        }
        const int side = card.left + 20 + (boardCompact_ ? 0 : kBoardSide + 24);
        const int right = card.right - 20;
        if (boardCompact_) {
            const int helpTop = boardTop_ + 5 * boardCellPixels_ + 4 * kBoardGap + 12;
            Text(dc, RECT{ side, helpTop, right, helpTop + 22 }, T("map2d.board.step1", "1. Click a black square."),
                boardCell_ < 0 ? kAccent : kText, DT_LEFT | DT_WORDBREAK, boldFont_);
            Text(dc, RECT{ side, helpTop + 22, right, helpTop + 64 }, T("map2d.board.step2", "2. Click the sprite for it in the palette on the left."),
                boardCell_ >= 0 ? kAccent : kText, DT_LEFT | DT_WORDBREAK, boldFont_);
            Text(dc, RECT{ side, boardLayerTop_, right - 104, boardLayerTop_ + kRowHeight }, T("map2d.layering", "Layer (higher covers lower)"),
                kTextDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE, smallFont_);
            return;
        }
        int y = boardTop_;
        Text(dc, RECT{ side, y, right, y + 44 }, T("map2d.board.step1", "1. Click a black square."), boardCell_ < 0 ? kAccent : kText, DT_LEFT | DT_WORDBREAK, boldFont_);
        y += 44;
        Text(dc, RECT{ side, y, right, y + 44 }, T("map2d.board.step2", "2. Click the sprite for it in the palette on the left."),
            boardCell_ >= 0 ? kAccent : kText, DT_LEFT | DT_WORDBREAK, boldFont_);
        y += 56;
        std::wstring about = T("map2d.board.pickSquare", "Squares around the ground are its edges: what is drawn where it meets another ground.");
        if (boardCell_ >= 0) {
            const BoardSlot& slot = slots[static_cast<std::size_t>(boardCell_)];
            about = T("map2d.board.thisSquare", "This square:") + L" " +
                (slot.piece >= 0 ? PieceTip(static_cast<Map2DBorderPiece>(slot.piece))
                    : slot.variant == 0 ? T("map2d.board.middle", "The ground itself: click it, then a sprite on the left to change it")
                                        : T("map2d.board.look", "Another look of the ground (optional, mixed in at random)"));
        }
        Text(dc, RECT{ side, y, right, y + 72 }, about, kTextDim, DT_LEFT | DT_WORDBREAK, smallFont_);
        y += 80;
        const auto edges = std::ranges::count_if(ground->borders, [](std::uint32_t cell) { return cell != kb::world2d::kMap2DNoCell; });
        Text(dc, RECT{ side, y, right, y + 22 },
            TF("map2d.board.count", "Edges: {count} of 12", { { "count", std::to_string(edges) } }) +
                (edges == 12 ? L"  \u2713" : L""), edges == 12 ? kGroundMark : kText, DT_LEFT | DT_SINGLELINE, boldFont_);
        Text(dc, RECT{ side, boardLayerTop_, right - 104, boardLayerTop_ + kRowHeight }, T("map2d.layering", "Layer (higher covers lower)"),
            kTextDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE, smallFont_);
        Text(dc, RECT{ card.left + 20, card.bottom - 16 - 34, card.right - 200, card.bottom - 16 }, T("map2d.board.clear", "Right click clears a square."),
            kTextDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE, smallFont_);
    }

    void DrawTutorial(HDC dc) {
        GdiFlush();
        RECT spot = TutorialSpot(tutorialStep_);
        surface_.Dim(spot, 90U);
        Frame(dc, spot, kAccent, 3);
        const RECT& card = tutorialCard_;
        // The card's shadow and body.
        Fill(dc, RECT{ card.left + 6, card.top + 6, card.right + 6, card.bottom + 6 }, RGB(2, 3, 5));
        Fill(dc, card, kPanel);
        Frame(dc, card, kAccent, 1);
        const RECT picture{ card.left + 20, card.top + 20, card.right - 20, card.top + 20 + 292 };
        Fill(dc, picture, kVoid);
        GdiFlush();
        if (const SheetImage* image = TutorialImage(tutorialStep_); image != nullptr) {
            surface_.Blit(picture, picture.left, picture.top, Width(picture), Height(picture), *image, 0U, 0U, image->width, image->height, 256U);
        }
        Frame(dc, picture, kBorder);
        const TutorialText text = TutorialStep(tutorialStep_);
        Text(dc, RECT{ card.left + 20, picture.bottom + 14, card.right - 90, picture.bottom + 44 }, text.title, kText, DT_LEFT | DT_SINGLELINE, titleFont_);
        // Progress: a dot for each step, the current one lit.
        for (int step = 0; step < kTutorialSteps; ++step) {
            const int x = card.right - 20 - (kTutorialSteps - step) * 16 + 6;
            const RECT dot{ x, picture.bottom + 26, x + 9, picture.bottom + 35 };
            Fill(dc, dot, step == tutorialStep_ ? kAccent : (step < tutorialStep_ ? RGB(70, 100, 150) : kBorder));
        }
        Text(dc, RECT{ card.left + 20, picture.bottom + 50, card.right - 20, card.bottom - 60 }, text.body, kTextDim, DT_LEFT | DT_WORDBREAK);
        for (const ControlBox& box : controls_) {
            if (box.id == Control::TutorialBack || box.id == Control::TutorialNext || box.id == Control::TutorialSkip) DrawButton(dc, box);
        }
    }

    [[nodiscard]] std::wstring TipUnderCursor() const {
        for (const ControlBox& box : controls_) {
            if (box.id == hot_ && box.param == hotParam_ && !box.tip.empty() && hot_ != Control::None) return box.tip;
        }
        if (tool_ == Tool::Collision) return T("map2d.collisionHelp", "Paint where movement is blocked. Ctrl + paint clears a block. Graphics stay unchanged.");
        if (pasting_) return T("map2d.tip.paste", "Click places the copied tiles; Escape cancels");
        if (tool_ == Tool::Select) {
            return T("map2d.tip.selectMode", "Drag selects, drag it to move  \u00B7  Ctrl+C / X / V  \u00B7  Delete clears  \u00B7  Space draws");
        }
        return T("map2d.tip.drawMode", "Ctrl+click erases  \u00B7  Shift+drag rectangle  \u00B7  Space selects  \u00B7  Ctrl+wheel floor  \u00B7  Alt+wheel size");
    }

    [[nodiscard]] std::wstring EntryKindText(const PaletteEntry& entry) const {
        switch (entry.kind) {
        case EntryKind::Ground:
            return entry.hasBorders
                ? TF("map2d.kind.groundEdges", "Ground with edges (layer {z}) - {category}", { { "z", std::to_string(entry.zOrder) }, { "category", CategoryText(entry.category) } })
                : TF("map2d.kind.ground", "Ground without edges yet - double-click adds them - {category}", { { "category", CategoryText(entry.category) } });
        case EntryKind::Floor:
            return TF("map2d.kind.floor", "Floor tile - {category}", { { "category", CategoryText(entry.category) } });
        case EntryKind::Item:
            return TF("map2d.kind.item", "Item, stands on the ground - {category}", { { "category", CategoryText(entry.category) } });
        case EntryKind::Object:
            return TF("map2d.kind.object", "Object {width} x {height} tiles - {category}",
                { { "width", std::to_string(entry.columns) }, { "height", std::to_string(entry.rows) }, { "category", CategoryText(entry.category) } });
        }
        return {};
    }

    [[nodiscard]] std::wstring EntryTip(const PaletteEntry& entry) const {
        return Wide(entry.name) + L"  \u00B7  " + EntryKindText(entry);
    }

    [[nodiscard]] static std::string CategoryText(const std::string& category) {
        return category == kb::world2d::kMap2DGeneralCategory ? std::string{ EditorText("map2d.general", "General") } : category;
    }

    [[nodiscard]] std::wstring CategoryLabel(const std::string& category) const {
        if (category == kAllCategory) return T("map2d.all", "Everything") + L" (" + std::to_wstring(entries_.size()) + L")";
        if (category == kRecentCategory) return T("map2d.recent", "Recently used");
        const auto count = std::ranges::count_if(entries_, [&category](const PaletteEntry& entry) { return entry.category == category; });
        return Wide(CategoryText(category)) + L" (" + std::to_wstring(count) + L")";
    }

    // ----- Data -----

    void ReloadLibrary() {
        library_ = kb::world2d::Map2DBrushLibrary::Load(assetsRoot_);
    }

    // The project's PNG and BMP textures in the sprites folder.
    void RefreshTextures() {
        const std::string folder = "/Game/" + std::string{ kb::world2d::kMap2DSpritesFolder } + "/";
        textures_.clear();
        for (const kb::assets::AssetMetadata& metadata : context_.Scene().Assets().Manager().Registry().All()) {
            const std::string path = metadata.virtualPath.generic_string();
            std::string extension = metadata.physicalPath.extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            if (metadata.type != "RenderTexture" || !path.starts_with(folder) || (extension != ".png" && extension != ".bmp")) continue;
            textures_.push_back(path);
        }
        for (const auto& file : library_.files) for (const auto& sheet : file.tileset.sheets) textures_.push_back(sheet.texture);
        std::ranges::sort(textures_);
        textures_.erase(std::unique(textures_.begin(), textures_.end()), textures_.end());
    }

    // The palette: the grounds and objects of the tilesets, then every sprite of every sheet that
    // no brush uses, each in its category.
    void BuildPalette() {
        entries_.clear();
        // While a ground's board is open its sheet shows sprite by sprite, so each edge piece can be picked.
        const Map2DGroundBrush* edited = editName_.empty() ? nullptr : library_.Find(editName_);
        for (const kb::world2d::Map2DTilesetFile& file : library_.files) {
            for (const Map2DGroundBrush& ground : file.tileset.grounds) {
                if (ground.tiles.empty()) continue;
                PaletteEntry entry{ .kind = EntryKind::Ground, .key = "g:" + ground.name, .name = ground.name, .category = file.category,
                    .texture = ground.texture, .cellSize = ground.cellSize, .sheetColumns = SheetColumns(ground.texture, ground.cellSize),
                    .firstCell = ground.tiles.front(), .defined = true, .zOrder = ground.zOrder, .hasBorders = ground.HasBorders() };
                entries_.push_back(std::move(entry));
            }
            for (const Map2DObjectBrush& object : file.tileset.objects) {
                PaletteEntry entry{ .kind = EntryKind::Object, .key = "o:" + object.name, .name = object.name, .category = file.category,
                    .texture = object.texture, .cellSize = object.cellSize, .sheetColumns = SheetColumns(object.texture, object.cellSize),
                    .firstCell = object.firstCell, .columns = object.columns, .rows = object.rows, .defined = true };
                entry.cells.clear();
                const SheetImage* image = Image(object.texture);
                for (std::uint32_t row = 0U; row < object.rows; ++row) {
                    for (std::uint32_t column = 0U; column < object.columns; ++column) {
                        if (image == nullptr || !CellEmpty(*image, library_.SheetSetting(entry.texture), entry.cellSize, entry.CellAt(column, row))) entry.cells.emplace_back(column, row);
                    }
                }
                if (entry.cells.empty()) entry.cells.emplace_back(0U, 0U);
                entries_.push_back(std::move(entry));
            }
        }
        for (const std::string& texture : textures_) {
            const SheetImage* image = Image(texture);
            kb::world2d::Map2DSheetSetting setting = library_.SheetSetting(texture);
            const bool editingSheet = edited != nullptr && edited->texture == texture;
            if (editingSheet) setting.cellSize = edited->cellSize;
            if (image == nullptr || setting.cellSize == 0U || image->width < setting.cellSize) continue;
            const std::uint32_t columns = setting.Count(image->width, setting.cellSize);
            const std::vector<std::uint32_t> used = editingSheet ? std::vector<std::uint32_t>{} :
                library_.UsedCells(texture, setting.cellSize, columns);
            const std::string category = kb::world2d::Map2DCategoryOf(assetsRoot_, kb::world2d::Map2DAssetFile(assetsRoot_, texture));
            const std::string stem = std::filesystem::path{ texture }.stem().string();
            for (const kb::world2d::Map2DSheetPiece& piece :
                 kb::world2d::FindMap2DSheetPieces(image->pixels, image->width, image->height, setting.cellSize,
                     setting.separate || editingSheet, used, setting.cellHeight, setting.margin, setting.spacing)) {
                const EntryKind kind = piece.cells.size() > 1U ? EntryKind::Object : (piece.opaque ? EntryKind::Floor : EntryKind::Item);
                entries_.push_back(PaletteEntry{ .kind = kind, .key = "p:" + texture + "#" + std::to_string(setting.cellSize) + "#" + std::to_string(piece.firstCell),
                    .name = stem + " " + std::to_string(piece.firstCell), .category = category, .texture = texture, .cellSize = setting.cellSize,
                    .sheetColumns = columns, .firstCell = piece.firstCell, .columns = piece.columns, .rows = piece.rows, .cells = piece.cells });
            }
        }
        for (const auto& metadata : context_.Scene().Assets().Manager().Registry().All()) {
            const auto path = kb::assets::NormalizeAssetPath(metadata.virtualPath);
            if (metadata.type == "ScenePrefab" && path.starts_with("/Game/")) {
                entries_.push_back(PaletteEntry{ .kind = EntryKind::Object, .key = "prefab:" + path,
                    .name = Narrow(metadata.virtualPath.stem().wstring()), .category = "Prefabs", .prefab = path });
            } else if (path.starts_with("/Game/2D Map Stamps/") && metadata.physicalPath.extension() == ".kbmap2d") {
                if (auto stamp = kb::assets::Map2DAssetIO::Load(metadata.physicalPath)) {
                    entries_.push_back(PaletteEntry{ .kind = EntryKind::Object, .key = "stamp:" + path,
                        .name = Narrow(metadata.virtualPath.stem().wstring()), .category = "Stamps",
                        .columns = stamp->width, .rows = stamp->height, .stamp = std::make_shared<Map2DAsset>(std::move(*stamp)) });
                }
            }
        }
        categories_.clear();
        for (const PaletteEntry& entry : entries_) {
            if (std::ranges::find(categories_, entry.category) == categories_.end()) categories_.push_back(entry.category);
        }
        std::ranges::sort(categories_, [](const std::string& left, const std::string& right) {
            const bool leftGeneral = left == kb::world2d::kMap2DGeneralCategory;
            const bool rightGeneral = right == kb::world2d::kMap2DGeneralCategory;
            return leftGeneral != rightGeneral ? rightGeneral : left < right;
        });
        if (category_ != kAllCategory && category_ != kRecentCategory && std::ranges::find(categories_, category_) == categories_.end()) {
            category_ = std::string{ kAllCategory };
        }
    }

    [[nodiscard]] static bool CellEmpty(const SheetImage& image, kb::world2d::Map2DSheetSetting layout, std::uint32_t cellSize, std::uint32_t cell) {
        layout.cellSize = cellSize;
        const auto columns = layout.Count(image.width, cellSize);
        if (columns == 0U || cell >= columns * layout.Count(image.height, layout.Height())) return true;
        const auto left = layout.margin + (cell % columns) * (cellSize + layout.spacing);
        const auto top = layout.margin + (cell / columns) * (layout.Height() + layout.spacing);
        for (std::uint32_t y = top; y < top + layout.Height(); ++y) {
            for (std::uint32_t x = left; x < left + cellSize; ++x) {
                if ((image.pixels[static_cast<std::size_t>(y) * image.width + x] >> 24U) >= 16U) return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::vector<const PaletteEntry*> VisibleEntries() const {
        std::vector<const PaletteEntry*> visible;
        const std::wstring needle = Lower(search_);
        const auto matches = [&needle](const PaletteEntry& entry) {
            return needle.empty() || Lower(Wide(entry.name)).find(needle) != std::wstring::npos ||
                Lower(Wide(CategoryText(entry.category))).find(needle) != std::wstring::npos;
        };
        // The board needs every non-empty cell of its sheet, including cells already used by a
        // brush. Opaque sprites can be valid edges too, so preserve the sheet's reading order.
        if (const Map2DGroundBrush* edited = editName_.empty() ? nullptr : library_.Find(editName_); edited != nullptr) {
            for (const PaletteEntry& entry : entries_) {
                if ((entry.kind == EntryKind::Floor || entry.kind == EntryKind::Item) && entry.texture == edited->texture &&
                    entry.cellSize == edited->cellSize && matches(entry)) visible.push_back(&entry);
            }
            return visible;
        }
        if (category_ == kRecentCategory) {
            for (const std::string& key : recent_) {
                if (const PaletteEntry* entry = Find(key); entry != nullptr && OfType(*entry) && matches(*entry)) visible.push_back(entry);
            }
            return visible;
        }
        // Grounds and objects first, then floor tiles, then items, as one finds them in Remere's palettes.
        for (const EntryKind kind : { EntryKind::Ground, EntryKind::Object, EntryKind::Floor, EntryKind::Item }) {
            for (const PaletteEntry& entry : entries_) {
                if (entry.kind == kind && OfType(entry) && (category_ == kAllCategory || entry.category == category_) && matches(entry)) visible.push_back(&entry);
            }
        }
        return visible;
    }

    [[nodiscard]] const PaletteEntry* Find(std::string_view key) const noexcept {
        const auto found = std::ranges::find_if(entries_, [key](const PaletteEntry& entry) { return entry.key == key; });
        return found == entries_.end() ? nullptr : &*found;
    }

    [[nodiscard]] const PaletteEntry* Selected() const noexcept { return Find(selectedKey_); }

    void SetSearch(std::wstring text) {
        search_ = std::move(text);
        paletteScroll_ = 0;
        // A search looks everywhere.
        if (!search_.empty() && category_ == kRecentCategory) category_ = std::string{ kAllCategory };
    }

    void Reload() {
        static_cast<void>(context_.Scene().Assets().Discover());
        images_.clear();
        ReloadLibrary();
        RefreshTextures();
        BuildPalette();
        SetMessage(EditorLocalization::Format("map2d.reloaded", "Found {sheets} sprite sheets and {tiles} palette tiles.",
            { { "sheets", std::to_string(textures_.size()) }, { "tiles", std::to_string(entries_.size()) } }), false);
    }

    [[nodiscard]] const SheetImage* Image(const std::string& texture) {
        auto found = images_.find(texture);
        if (found == images_.end()) {
            SheetImage image{};
            image.loaded = LoadImagePixels(kb::world2d::Map2DAssetFile(assetsRoot_, texture), image);
            found = images_.emplace(texture, std::move(image)).first;
        }
        return found->second.loaded ? &found->second : nullptr;
    }

    [[nodiscard]] const SheetImage* TutorialImage(int step) {
        const std::string key = "*tutorial" + std::to_string(step);
        auto found = images_.find(key);
        if (found == images_.end()) {
            SheetImage image{};
            image.loaded = LoadImagePixels(TutorialImageFile(step), image);
            found = images_.emplace(key, std::move(image)).first;
        }
        return found->second.loaded ? &found->second : nullptr;
    }

    [[nodiscard]] std::uint32_t SheetColumns(const std::string& texture, std::uint32_t cellSize) {
        const SheetImage* image = Image(texture);
        auto layout = library_.SheetSetting(texture); layout.cellSize = cellSize;
        return image == nullptr ? 1U : std::max(1U, layout.Count(image->width, cellSize));
    }

    // The ground being edited inside its tileset.
    [[nodiscard]] std::pair<kb::world2d::Map2DTilesetFile*, Map2DGroundBrush*> MutableGround(std::string_view name) {
        for (kb::world2d::Map2DTilesetFile& file : library_.files) {
            if (Map2DGroundBrush* ground = file.tileset.Find(name); ground != nullptr) return { &file, ground };
        }
        return { nullptr, nullptr };
    }

    void UseEntry(const std::string& key) {
        if (key.empty() || key == selectedKey_) return;
        previousKey_ = selectedKey_;
        selectedKey_ = key;
        std::erase(recent_, key);
        recent_.insert(recent_.begin(), key);
        if (recent_.size() > kRecentCount) recent_.resize(kRecentCount);
        if (tool_ == Tool::Eraser || tool_ == Tool::Picker || tool_ == Tool::Select) tool_ = Tool::Draw;
    }

    void SetMessage(std::string text, bool error) {
        message_ = std::move(text);
        messageIsError_ = error;
    }

    void Invalidate() const {
        if (window_ != nullptr) InvalidateRect(window_, nullptr, FALSE);
    }

    void UpdateTitle() const {
        if (window_ == nullptr) return;
        const std::wstring title = file_.filename().wstring() + (dirty_ ? L" *" : L"") + L" - " + T("map2d.title", "World 2D Map Editor");
        SetWindowTextW(window_, title.c_str());
    }

    void CenterMap() {
        const RECT canvas = CanvasRect();
        panX_ = static_cast<double>(map_.width) * zoom_ * 0.5 - Width(canvas) * 0.5;
        panY_ = static_cast<double>(map_.height) * zoom_ * 0.5 - Height(canvas) * 0.5;
    }

    void CenterOn(int tileX, int tileY) {
        const RECT canvas = CanvasRect();
        panX_ = (tileX + 0.5) * zoom_ - Width(canvas) * 0.5;
        panY_ = (tileY + 0.5) * zoom_ - Height(canvas) * 0.5;
    }

    void Publish(bool persist) {
        publishPending_ = false;
        if (!live_ && !persist) return;
        std::string error;
        if (!EditorMap2DService::Publish(context_.Scene(), assetId_, map_, persist, &error)) {
            SetMessage(error.empty() ? std::string{ EditorText("map2d.sceneFailed", "The scene could not show the map.") } : error, true);
            Invalidate();
            return;
        }
        if (present_) present_();
        static_cast<void>(RefreshCollision(map_));
    }

    [[nodiscard]] bool Save(bool saveScene = true) {
        if (strokeActive_ || rectActive_) FinishStroke();
        if (!RefreshCollision(map_)) return false;
        std::string error;
        if (!EditorMap2DService::Publish(context_.Scene(), assetId_, map_, true, &error)) {
            SetMessage(error.empty() ? std::string{ EditorText("map2d.saveFailed", "The map could not be saved.") } : error, true);
            Invalidate();
            return false;
        }
        if (present_) present_();
        dirty_ = false;
        publishPending_ = false;
        SetMessage(EditorLocalization::Format("map2d.saved", "Saved {file}", { { "file", Utf8(file_.filename()) } }), false);
        UpdateTitle();
        Invalidate();
        return !saveScene || context_.SaveCurrentScene();
    }

    // Restoring a map's saved layers is itself undoable in the shared scene history.
    [[nodiscard]] bool Discard() {
        FinishStroke();
        auto& scene = context_.Scene();
        std::vector<kb::scene::SceneEntity> layers{ entity_ };
        const auto parent = scene.Hierarchy().Parent(entity_);
        if (scene.Components().Grids().Has(parent) && !scene.Components().Tilemaps().Has(parent)) layers = scene.Hierarchy().ChildEntities(parent);
        std::vector<std::pair<std::shared_ptr<MapWorkingCopy>, Map2DAsset>> originals;
        auto batch = std::make_unique<MapBatchCommand>();
        const auto rollback = [&]() {
            for (auto& [copy, original] : originals) { copy->map = std::move(original); static_cast<void>(copy->Publish()); }
        };
        for (const auto layer : layers) {
            const auto* metadata = EditorMap2DService::MapMetadata(scene, layer);
            if (!metadata) continue;
            std::string error;
            const auto saved = kb::assets::Map2DAssetIO::Load(metadata->physicalPath, &error);
            if (!saved) { rollback(); SetMessage(error, true); return false; }
            auto copy = AcquireWorkingCopy(context_, layer, metadata->id, *saved);
            if (std::ranges::any_of(originals, [&](const auto& item) { return item.first == copy; })) continue;
            originals.emplace_back(copy, copy->map);
            kb::world2d::Map2DChangeSet change;
            change.RecordSettings(copy->map);
            for (const auto& [key, tile] : copy->map.tiles) { static_cast<void>(tile); change.Record(copy->map, key); }
            for (const auto& [key, tile] : saved->tiles) { static_cast<void>(tile); change.Record(copy->map, key); }
            copy->map = *saved;
            change.Finish(copy->map);
            if (!change.Empty()) batch->edits.push_back(std::make_unique<MapStrokeCommand>(copy, std::move(change)));
        }
        if (!batch->edits.empty() && !context_.ExecuteSceneCommand(std::move(batch))) { rollback(); return false; }
        for (auto& [copy, original] : originals) {
            static_cast<void>(original);
            copy->dirty = false;
            if (copy->notify) copy->notify();
        }
        static_cast<void>(kb::world2d::SetUpMap2DView(scene, entity_, map_, assetsRoot_));
        publishPending_ = false;
        UpdateTitle();
        Invalidate();
        return true;
    }

    void MarkChanged() {
        if (!dirty_) {
            dirty_ = true;
            UpdateTitle();
        }
        publishPending_ = true;
    }

    [[nodiscard]] bool RefreshCollision(const Map2DAsset& map) {
        bool changed = false;
        std::string error;
        const bool success = EditorMap2DService::SyncCollision(context_.Scene(), assetId_, map, &changed, &error);
        if (!success) {
            SetMessage(error, true);
            Invalidate();
        }
        if (changed) {
            context_.MarkSceneDocumentDirty();
            context_.MarkSceneRenderDirty();
        }
        return success;
    }

    // ----- Editing -----

    [[nodiscard]] kb::world2d::Map2DEditor Editor() {
        kb::world2d::Map2DEditor editor{ map_, library_, assetsRoot_ };
        editor.SetChangeSet(stroke_.has_value() ? &*stroke_ : nullptr);
        editor.SetAutoBorder(automagic_);
        return editor;
    }

    void BeginChange() {
        strokeWasDirty_ = dirty_;
        scatterVisited_.clear(); ++scatterSeed_;
        stroke_.emplace();
        stroke_->RecordSettings(map_);
        message_.clear();
    }

    void EndChange() {
        if (!stroke_.has_value()) return;
        stroke_->Finish(map_);
        if (!stroke_->Empty()) {
            MarkChanged();
            const auto* component = std::as_const(context_.Scene()).Components().Tilemaps().TryGet(entity_);
            std::string validationError;
            if (!kb::assets::IsMap2DAssetValid(map_, &validationError)) {
                stroke_->Undo(map_); dirty_ = strokeWasDirty_; publishPending_ = false;
                SetMessage(validationError, true);
            } else if ((component && component->locked) || context_.HasPlayModeSceneSession()) {
                stroke_->Undo(map_);
                dirty_ = strokeWasDirty_;
                UpdateTitle();
                SetMessage(std::string{context_.HasPlayModeSceneSession() ? EditorText("map2d.stopToPaint","Stop Play to edit the map.") : EditorText("map2d.layerLockedHint", "This layer is locked. Unlock it in the Inspector to paint.")}, true);
                if (!context_.HasPlayModeSceneSession()) Publish(false);
                publishPending_ = false;
            } else if (!context_.ExecuteSceneCommand(std::make_unique<MapStrokeCommand>(document_, *stroke_))) {
                stroke_->Undo(map_);
                Publish(false);
            }
        }
        stroke_.reset();
    }

    void PaintArea(kb::world2d::Map2DEditor& editor, Map2DRect area, bool erase) {
        if (!scatterApplying_ && density_ < 100U && !erase && tool_ != Tool::Collision) {
            scatterApplying_ = true;
            for (auto y = area.y0; y <= area.y1; ++y) for (auto x = area.x0; x <= area.x1; ++x) {
                const auto key = Map2DTileKey(x, y, floor_);
                if (!scatterVisited_.insert(key).second) continue;
                std::uint32_t hash = x * 374761393U + y * 668265263U + scatterSeed_ * 2246822519U;
                hash = (hash ^ (hash >> 13U)) * 1274126177U; hash ^= hash >> 16U;
                if (hash % 100U < density_) PaintArea(editor, {x, y, x, y}, false);
            }
            scatterApplying_ = false;
            return;
        }
        if (tool_ == Tool::Collision) {
            for (auto y = area.y0; y <= area.y1; ++y) for (auto x = area.x0; x <= area.x1; ++x) {
                const auto key = Map2DTileKey(x, y, floor_);
                auto tile = map_.tiles.contains(key) ? map_.tiles.at(key) : Map2DTile{};
                tile.solid = false;
                tile.collision = erase ? kb::assets::Map2DCollision{} : collisionBrush_;
                editor.SetTile(x, y, floor_, tile);
            }
            publishPending_ = true;
            return;
        }
        std::string error;
        bool ok = true;
        const PaletteEntry* entry = Selected();
        if (erase) {
            editor.Erase(area, floor_, Shift());
        } else if (entry == nullptr) {
            SetMessage(std::string{ EditorText("map2d.pickFirst", "Pick a tile in the palette on the left first.") }, true);
            return;
        } else if (entry->kind == EntryKind::Ground) {
            const Map2DGroundBrush* ground = library_.Find(entry->name);
            ok = ground != nullptr && editor.PaintGround(*ground, area, floor_, &error);
        } else if (entry->kind == EntryKind::Object) {
            ok = PlaceObject(editor, *entry, static_cast<int>(area.x0) + static_cast<int>(area.x1 - area.x0) / 2, static_cast<int>(area.y1), &error);
        } else {
            // A floor tile replaces the ground, an item stands on it.
            ok = editor.PaintSprite(entry->texture, entry->cellSize, entry->firstCell, entry->kind == EntryKind::Floor, area, floor_, &error);
        }
        if (!ok && !error.empty()) SetMessage(error, true);
        publishPending_ = true;
    }

    // An object with its bottom middle tile at (x, y); the parts outside the map are left out.
    [[nodiscard]] bool PlaceObject(kb::world2d::Map2DEditor& editor, const PaletteEntry& entry, int x, int y, std::string* error) {
        if (!entry.prefab.empty()) {
            if (x < 0 || y < 0 || x >= static_cast<int>(map_.width) || y >= static_cast<int>(map_.height)) return false;
            const auto key = Map2DTileKey(static_cast<unsigned>(x), static_cast<unsigned>(y), floor_);
            auto tile = map_.tiles.contains(key) ? map_.tiles.at(key) : Map2DTile{};
            tile.prefab = entry.prefab;
            editor.SetTile(static_cast<unsigned>(x), static_cast<unsigned>(y), floor_, tile);
            return true;
        }
        if (entry.stamp) return PlaceStamp(editor, *entry.stamp, x, y, error);
        const auto [left, top] = kb::world2d::Map2DObjectOrigin(entry.columns, entry.rows, x, y);
        for (const auto& [column, row] : entry.cells) {
            const std::int64_t tileX = left + column;
            const std::int64_t tileY = top + row;
            if (tileX < 0 || tileY < 0 || tileX >= map_.width || tileY >= map_.height) continue;
            if (!editor.PaintSprite(entry.texture, entry.cellSize, entry.CellAt(column, row), false,
                    editor.Clamp(tileX, tileY, tileX, tileY), floor_, error)) {
                return false;
            }
        }
        return true;
    }

    void ApplyAt(int tileX, int tileY) {
        if (tileX < 0 || tileY < 0 || tileX >= static_cast<int>(map_.width) || tileY >= static_cast<int>(map_.height)) return;
        kb::world2d::Map2DEditor editor = Editor();
        const PaletteEntry* entry = Selected();
        if (density_ == 100U && tool_ != Tool::Collision && !strokeErases_ && entry != nullptr && entry->kind == EntryKind::Object) {
            std::string error;
            if (!PlaceObject(editor, *entry, tileX, tileY, &error) && !error.empty()) SetMessage(error, true);
            publishPending_ = true;
            return;
        }
        const int half = brushSize_ / 2;
        PaintArea(editor, editor.Clamp(tileX - half, tileY - half, tileX + half, tileY + half), strokeErases_);
    }

    void ApplyLine(int fromX, int fromY, int toX, int toY) {
        // Every tile between two mouse positions, so a fast stroke leaves no gaps; an object is
        // placed again only a whole object further on.
        const PaletteEntry* entry = Selected();
        if (density_ == 100U && tool_ != Tool::Collision && !strokeErases_ && entry != nullptr && entry->kind == EntryKind::Object) {
            if (std::abs(toX - lastObjectX_) >= static_cast<int>(entry->columns) || std::abs(toY - lastObjectY_) >= static_cast<int>(entry->rows)) {
                ApplyAt(toX, toY);
                lastObjectX_ = toX;
                lastObjectY_ = toY;
            }
            return;
        }
        const int steps = std::max(std::abs(toX - fromX), std::abs(toY - fromY));
        for (int step = 1; step <= steps; ++step) {
            ApplyAt(fromX + static_cast<int>(std::lround(static_cast<double>(toX - fromX) * step / steps)),
                fromY + static_cast<int>(std::lround(static_cast<double>(toY - fromY) * step / steps)));
        }
    }

    void FinishStroke() {
        if (rectActive_ && HoverInside()) {
            kb::world2d::Map2DEditor editor = Editor();
            PaintArea(editor, editor.Clamp(dragStartX_, dragStartY_, hoverX_, hoverY_), strokeErases_);
        }
        strokeActive_ = false;
        rectActive_ = false;
        EndChange();
        Invalidate();
    }

    void FloodFill(int tileX, int tileY) {
        const PaletteEntry* entry = Selected();
        const Map2DGroundBrush* ground = entry != nullptr && entry->kind == EntryKind::Ground ? library_.Find(entry->name) : nullptr;
        if (ground == nullptr && (entry == nullptr || entry->kind != EntryKind::Floor)) {
            SetMessage(std::string{ EditorText("map2d.fillPickFloor", "Choose a ground tile on the left, then click the area to fill.") }, true);
            return;
        }
        BeginChange();
        kb::world2d::Map2DEditor editor = Editor();
        std::string error;
        const std::size_t filled = ground != nullptr
            ? editor.FillGround(*ground, static_cast<std::uint32_t>(tileX), static_cast<std::uint32_t>(tileY), floor_, &error)
            : editor.FillSprite(entry->texture, entry->cellSize, entry->firstCell, static_cast<std::uint32_t>(tileX), static_cast<std::uint32_t>(tileY), floor_, &error);
        if (!error.empty()) SetMessage(error, true);
        else SetMessage(EditorLocalization::Format("map2d.filled", "Filled {count} tiles.", { { "count", std::to_string(filled) } }), false);
        EndChange();
    }

    // The palette tile of a sprite on the map: the ground brush it belongs to, or the sheet piece.
    [[nodiscard]] std::string KeyOf(Map2DSprite sprite, bool ground) {
        if (!sprite.IsValid() || sprite.sheet >= map_.sheets.size()) return {};
        if (ground) {
            kb::world2d::Map2DEditor editor = Editor();
            if (const Map2DGroundBrush* brush = editor.BrushOf(sprite); brush != nullptr) return "g:" + brush->name;
        }
        const kb::assets::Map2DSheet& sheet = map_.sheets[sprite.sheet];
        for (const PaletteEntry& entry : entries_) {
            if (entry.texture != sheet.texture || entry.cellSize != sheet.cellSize || entry.kind == EntryKind::Ground) continue;
            for (const auto& [column, row] : entry.cells) {
                if (entry.CellAt(column, row) == sprite.cell) return entry.key;
            }
        }
        return {};
    }

    void Pick(int tileX, int tileY) {
        const auto found = map_.tiles.find(Map2DTileKey(static_cast<std::uint32_t>(tileX), static_cast<std::uint32_t>(tileY), floor_));
        if (found == map_.tiles.end()) return;
        const Map2DTile& tile = found->second;
        const std::string key = !tile.items.empty() ? KeyOf(tile.items.back(), false) : KeyOf(tile.ground, true);
        if (key.empty()) {
            SetMessage(std::string{ EditorText("map2d.pickMissing", "That sprite is not in the palette (its sheet is outside Assets/2D Map Sprites).") }, true);
            return;
        }
        UseEntry(key);
        RevealSelected();
        tool_ = Tool::Draw;
    }

    // The picked tile's category opens and the grid scrolls to it.
    void RevealSelected() {
        const PaletteEntry* entry = Selected();
        if (entry == nullptr) return;
        if (!OfType(*entry)) {
            paletteType_ = entry->kind == EntryKind::Object ? "objects" : entry->kind == EntryKind::Item ? "items" : "grounds";
        }
        if (category_ != kAllCategory && category_ != entry->category) category_ = entry->category;
        search_.clear();
        if (searchEdit_ != nullptr) SetWindowTextW(searchEdit_, L"");
        paletteScroll_ = 0;
        const std::vector<const PaletteEntry*> visible = VisibleEntries();
        const auto position = std::ranges::find(visible, entry);
        if (position == visible.end() || Width(gridRect_) <= 0) return;
        const int perRow = std::max(1, (Width(gridRect_) + kTileGap) / (kTileSize + kTileGap));
        const int row = static_cast<int>(position - visible.begin()) / perRow;
        paletteScroll_ = std::max(0, row * (kTileSize + kTileGap) - Height(gridRect_) / 2);
    }

    void Undo() { static_cast<void>(context_.UndoSceneCommand()); Invalidate(); }

    void Redo() { static_cast<void>(context_.RedoSceneCommand()); Invalidate(); }

    // Remere's Borderize: the edges of an area (the selection, else the whole floor) made again.
    void Borderize(bool wholeMap) {
        BeginChange();
        kb::world2d::Map2DEditor editor = Editor();
        editor.SetAutoBorder(true);
        if (wholeMap) {
            for (const std::int32_t z : FloorsPresent()) editor.RefreshBorders(editor.Clamp(0, 0, map_.width - 1, map_.height - 1), z);
        } else {
            const Map2DRect area = hasSelection_ ? selection_ : editor.Clamp(0, 0, map_.width - 1, map_.height - 1);
            editor.RefreshBorders(area, floor_);
        }
        EndChange();
    }

    // ----- Selection -----

    void EditSelectedTiles(const std::function<void(Map2DTile&)>& edit) {
        if (!hasSelection_) { SetMessage(std::string(EditorText("map2d.selectionRequired","Select tiles first, or right-click a tile.")), true); return; }
        BeginChange();
        auto editor = Editor();
        for (auto y = selection_.y0; y <= selection_.y1; ++y) for (auto x = selection_.x0; x <= selection_.x1; ++x) {
            const auto key = Map2DTileKey(x, y, floor_);
            auto tile = map_.tiles.contains(key) ? map_.tiles.at(key) : Map2DTile{};
            edit(tile); editor.SetTile(x, y, floor_, tile);
        }
        EndChange();
        Invalidate();
    }

    bool MakeUnique() {
        if (context_.HasPlayModeSceneSession()) return false;
        const auto* component = std::as_const(context_.Scene()).Components().Tilemaps().TryGet(entity_);
        if (!component || component->locked) return false;
        const auto source = EditorMap2DService::MapMetadata(context_.Scene(), assetId_);
        if (!source) return false;
        auto destination = source->physicalPath;
        const auto stem = destination.stem().wstring();
        for (unsigned suffix = 1; suffix < 100000U; ++suffix) {
            destination = source->physicalPath.parent_path() / (stem + L" Copy " + std::to_wstring(suffix) + L".kbmap2d");
            if (!std::filesystem::exists(destination)) break;
        }
        std::string error;
        if (!kb::assets::Map2DAssetIO::Save(destination, map_, &error)) { SetMessage(error, true); return false; }
        static_cast<void>(context_.Scene().Assets().Discover());
        const auto path = context_.Scene().Assets().Manager().Mounts().ToVirtual(destination);
        const auto* copy = path ? context_.Scene().Assets().Manager().Registry().FindByPath(*path) : nullptr;
        if (!copy) { SetMessage(std::string(EditorText("map2d.copyFailed","The new map could not be registered.")), true); return false; }
        const auto copyId = copy->id;
        const bool changed = context_.ExecuteSceneCommand("Make Tilemap Unique", [&]() {
            auto tilemap = *std::as_const(context_.Scene()).Components().Tilemaps().TryGet(entity_); tilemap.mapAssetId = copyId.value;
            context_.Scene().Components().Tilemaps().Set(entity_, tilemap);
            if (EditorMap2DService::IsMapMeshRenderer(context_.Scene(), entity_))
                context_.Scene().Components().MeshRenderers().Set(entity_, {.meshAssetId = copyId.value, .castsShadow = false});
            return true;
        });
        if (!changed) return false;
        if (!EditorMap2DService::Publish(context_.Scene(), copyId, map_, false, &error)) { SetMessage(error, true); return false; }
        if (owner_) { Hide(owner_); return EditorMap2DWindow::Open(owner_, context_, entity_, present_); }
        return true;
    }

    void TransformSelection(std::string_view action, std::uint32_t tint = 0xFFFFFFFFU, float x = 0, float y = 0) {
        EditSelectedTiles([&](Map2DTile& tile) {
            const auto transform = [&](Map2DSprite& sprite) {
                if (!sprite.IsValid()) return;
                if (action == "rotate") sprite.rotation = static_cast<std::uint8_t>((sprite.rotation + 1U) % 4U);
                else if (action == "flipX") sprite.flipX = !sprite.flipX;
                else if (action == "flipY") sprite.flipY = !sprite.flipY;
                else if (action == "tint") sprite.tint = tint;
                else if (action == "opacity") sprite.tint = (sprite.tint & 0xFFFFFF00U) | (tint & 0xFFU);
                else if (action == "offset") { sprite.offsetX = x; sprite.offsetY = y; }
                else if (action == "reset") sprite = Map2DSprite{sprite.sheet, sprite.cell};
            };
            transform(tile.ground);
            for (auto& sprite : tile.borders) transform(sprite);
            for (auto& sprite : tile.items) transform(sprite);
        });
    }

    void UseSelectedCollisionAsDefault() {
        if (!hasSelection_) return;
        BeginChange();
        std::map<std::pair<unsigned, unsigned>, kb::assets::Map2DCollision> defaults;
        for (auto& [key, tile] : map_.tiles) {
            if (kb::assets::Map2DTileKeyZ(key) != floor_ || !InSelection(kb::assets::Map2DTileKeyX(key), kb::assets::Map2DTileKeyY(key))) continue;
            const auto sprite = tile.items.empty() ? tile.ground : tile.items.back();
            if (!sprite.IsValid()) continue;
            auto shape = kb::assets::Map2DCellCollision(map_, tile);
            // Store the shape in the graphic's original coordinates. Rendering and collision
            // apply the placed tile's rotation/reflection when the default is used again.
            if (sprite.rotation || sprite.flipX || sprite.flipY) {
                if (shape.kind == kb::assets::Map2DCollisionKind::SlopeUp) shape.polygon = {{0,1},{1,0},{1,1}};
                if (shape.kind == kb::assets::Map2DCollisionKind::SlopeDown) shape.polygon = {{0,0},{1,1},{0,1}};
                if (!shape.polygon.empty()) {
                    shape.kind = kb::assets::Map2DCollisionKind::Polygon;
                    for (auto& point : shape.polygon) point = kb::assets::Map2DSpriteUV(sprite, point[0], point[1]);
                }
            }
            defaults.try_emplace({sprite.sheet, sprite.cell}, std::move(shape));
            stroke_->Record(map_, key); tile.collision.reset(); tile.solid = false;
        }
        for (const auto& [graphic, shape] : defaults) map_.sheets[graphic.first].definitions[graphic.second].collision = shape;
        EndChange();
    }

    void AnimateSelection(unsigned count, float speed) {
        if (!hasSelection_ || count > 256U || !std::isfinite(speed) || speed <= 0 || speed > 240) { SetMessage(std::string(EditorText("map2d.animation.invalid","Choose up to 256 frames and a speed greater than 0, up to 240.")), true); return; }
        BeginChange();
        std::set<std::pair<unsigned, unsigned>> seen;
        for (const auto& [key, tile] : map_.tiles) {
            if (kb::assets::Map2DTileKeyZ(key) != floor_ || !InSelection(static_cast<int>(kb::assets::Map2DTileKeyX(key)), static_cast<int>(kb::assets::Map2DTileKeyY(key)))) continue;
            const auto sprite = tile.items.empty() ? tile.ground : tile.items.back();
            if (!sprite.IsValid() || !seen.emplace(sprite.sheet, sprite.cell).second) continue;
            auto& sheet = map_.sheets[sprite.sheet];
            auto& definition = sheet.definitions[sprite.cell];
            definition.frames.clear(); definition.framesPerSecond = speed;
            for (unsigned i = 0; i < count; ++i) definition.frames.push_back(sprite.cell + i);
        }
        EndChange(); Invalidate();
    }

    bool SaveStamp(const std::string& name) {
        if (!hasSelection_ || !kb::assets::AssetFileNameError(name).empty()) { SetMessage(std::string(EditorText("map2d.stamp.invalid","Select tiles and enter a valid stamp name.")), true); return false; }
        const auto file = assetsRoot_ / "2D Map Stamps" / kb::assets::Utf8Path(name + ".kbmap2d");
        if (std::filesystem::exists(file)) { SetMessage(std::string(EditorText("map2d.stamp.exists","A stamp with this name already exists.")), true); return false; }
        auto stamp = kb::assets::MakeEmptyMap2DAsset(selection_.x1 - selection_.x0 + 1U, selection_.y1 - selection_.y0 + 1U);
        stamp.background = false; stamp.orientation = map_.orientation; stamp.sheets = map_.sheets; stamp.groundRules = map_.groundRules;
        for (const auto& [offset, tile] : CopySelection().tiles) stamp.tiles[Map2DTileKey(offset.first, offset.second, 0)] = tile;
        std::string error;
        if (!kb::assets::Map2DAssetIO::Save(file, stamp, &error)) { SetMessage(error, true); return false; }
        static_cast<void>(context_.Scene().Assets().Discover()); BuildPalette();
        SetMessage(std::string(EditorText("map2d.stamp.saved","Stamp saved. It is available in Objects / Stamps.")), false); return true;
    }

    bool PlaceStamp(kb::world2d::Map2DEditor& editor, const Map2DAsset& stamp, int x, int y, std::string* error) {
        std::vector<std::uint16_t> sheets;
        for (const auto& source : stamp.sheets) {
            const auto existing = std::ranges::find(map_.sheets, source);
            if (existing != map_.sheets.end()) sheets.push_back(static_cast<std::uint16_t>(existing - map_.sheets.begin()));
            else {
                if (map_.sheets.size() >= Map2DAsset::MaximumSheets) { if (error) *error = "Too many sprite sheets in this map"; return false; }
                sheets.push_back(static_cast<std::uint16_t>(map_.sheets.size())); map_.sheets.push_back(source);
            }
        }
        for (auto rule : stamp.groundRules) {
            rule.sheet = sheets[rule.sheet];
            if (std::ranges::find(map_.groundRules, rule.name, &kb::assets::Map2DGroundRule::name) == map_.groundRules.end()) map_.groundRules.push_back(std::move(rule));
        }
        const auto [left, top] = kb::world2d::Map2DObjectOrigin(stamp.width, stamp.height, x, y);
        for (const auto& [key, original] : stamp.tiles) {
            const auto tx = left + kb::assets::Map2DTileKeyX(key), ty = top + kb::assets::Map2DTileKeyY(key);
            if (tx < 0 || ty < 0 || tx >= map_.width || ty >= map_.height) continue;
            auto tile = original;
            const auto remap = [&](Map2DSprite& sprite) { if (sprite.IsValid()) sprite.sheet = sheets[sprite.sheet]; };
            remap(tile.ground); for (auto& sprite : tile.borders) remap(sprite); for (auto& sprite : tile.items) remap(sprite);
            editor.SetTile(static_cast<unsigned>(tx), static_cast<unsigned>(ty), floor_, tile);
        }
        return true;
    }

    void ReplaceSelectedGraphic() {
        if (!hasSelection_ || !Selected()) return;
        const auto found = map_.tiles.find(Map2DTileKey(selection_.x0, selection_.y0, floor_));
        if (found == map_.tiles.end()) return;
        const auto source = found->second.items.empty() ? found->second.ground : found->second.items.back();
        const auto entry = *Selected();
        if (!source.IsValid() || !entry.prefab.empty() || entry.stamp) return;
        BeginChange(); std::string error;
        const auto sheet = kb::world2d::EnsureMap2DSheet(map_, assetsRoot_, entry.texture, entry.cellSize, &error, library_.SheetSetting(entry.texture));
        if (!sheet) { EndChange(); SetMessage(error, true); return; }
        for (auto& [key, tile] : map_.tiles) {
            stroke_->Record(map_, key);
            const auto replace = [&](Map2DSprite& sprite) { if (sprite.sheet == source.sheet && sprite.cell == source.cell) { sprite.sheet = *sheet; sprite.cell = entry.firstCell; } };
            replace(tile.ground); for (auto& sprite : tile.borders) replace(sprite); for (auto& sprite : tile.items) replace(sprite);
        }
        EndChange();
    }

    void EditSelectedDefinitions(const std::function<void(kb::assets::Map2DTileDefinition&)>& edit) {
        if (!hasSelection_) return;
        BeginChange();
        std::set<std::pair<unsigned, unsigned>> seen;
        for (const auto& [key, tile] : map_.tiles) {
            if (kb::assets::Map2DTileKeyZ(key) != floor_ || !InSelection(static_cast<int>(kb::assets::Map2DTileKeyX(key)), static_cast<int>(kb::assets::Map2DTileKeyY(key)))) continue;
            const auto sprite = tile.items.empty() ? tile.ground : tile.items.back();
            if (sprite.IsValid() && seen.emplace(sprite.sheet, sprite.cell).second) edit(map_.sheets[sprite.sheet].definitions[sprite.cell]);
        }
        EndChange();
    }

    const Map2DTile* FirstSelectedTile() const {
        if (!hasSelection_) return nullptr;
        for (const auto& [key,tile] : map_.tiles)
            if (kb::assets::Map2DTileKeyZ(key) == floor_ && InSelection(static_cast<int>(kb::assets::Map2DTileKeyX(key)),static_cast<int>(kb::assets::Map2DTileKeyY(key)))) return &tile;
        return nullptr;
    }

    Map2DSprite FirstSelectedSprite() const {
        const auto* tile = FirstSelectedTile();
        return tile ? (tile->items.empty() ? tile->ground : tile->items.back()) : Map2DSprite{};
    }

    bool OpenSettings(const std::string& kind) {
        const auto text = [](const char* key, const char* fallback) { return std::string(EditorText(key,fallback)); };
        const auto number = [&](const char* key, const char* label, double value, double low, double high, double step = 1, bool integer = false) {
            return Map2DFormField{text(key,label),EditorMap2DForm::Format(value),low,high,step,integer};
        };
        std::string title, hint;
        std::vector<Map2DFormField> fields;
        std::vector<Map2DFormPreset> presets;
        const auto sprite = FirstSelectedSprite();
        if (kind == "surface") {
            const auto* tile = FirstSelectedTile(); const auto shape = tile ? kb::assets::Map2DCellCollision(map_,*tile) : kb::assets::Map2DCollision{};
            title = text("map2d.surface","Surface friction and bounce...");
            hint = text("map2d.form.surfaceHint","Choose a surface, or adjust each value. Friction 0 slides freely; bounce 1 rebounds fully.");
            fields = {number("map2d.form.friction","Friction",shape.friction,0,10,0.1),number("map2d.form.bounce","Bounce",shape.restitution,0,1,0.1)};
            presets = {{text("map2d.form.normal","Normal"),{"0.5","0"}},{text("map2d.form.ice","Ice"),{"0","0"}},{text("map2d.form.rubber","Bouncy"),{"0.5","0.9"}}};
        } else if (kind == "opacity") {
            title = text("map2d.opacity","Opacity..."); hint = text("map2d.form.opacityHint","0 hides the graphic. 100 makes it fully visible.");
            fields = {number("map2d.form.opacity","Visible amount (%)",std::lround((sprite.tint&255U)*100.0/255.0),0,100,5,true)};
            presets = {{"25%",{"25"}},{"50%",{"50"}},{"100%",{"100"}}};
        } else if (kind == "offset") {
            title = text("map2d.offset","Graphic offset..."); hint = text("map2d.form.offsetHint","Move the graphic without moving its cell. Negative values move left or up. 0.5 is half a tile.");
            fields = {number("map2d.form.right","Right (tiles)",sprite.offsetX,-16,16,0.25),number("map2d.form.down","Down (tiles)",sprite.offsetY,-16,16,0.25)};
            presets = {{text("map2d.form.center","Center"),{"0","0"}}};
        } else if (kind == "schema") {
            title=text("map2d.propertySchema","Property schema...");
            hint=text("map2d.propertySchemaHint","Declare a default and type (bool, int, float or string). Rename from migrates existing values; incompatible conversions are rejected.");
            fields={{text("map2d.name","Name"),"",0,0,0,false,true},
                {text("map2d.propertyType","Type"),"string",0,0,0,false,true},
                {text("map2d.propertyDefault","Default value"),"",0,0,0,false,true},
                {text("map2d.propertyPrevious","Rename from (optional)"),"",0,0,0,false,true}};
        } else if (kind == "camera") {
            title = text("map2d.cameraSettings", "Play camera...");
            hint = text("map2d.cameraHint", "Follow an entity by name. Leave it empty for a fixed camera. Pixel density 0 allows smooth scaling.");
            fields = {{text("map2d.cameraFollow", "Follow target"),map_.camera.followTarget,0,0,0,false,true},
                number("map2d.cameraZoom","Zoom",map_.camera.zoom,0.01,1024,0.25),
                number("map2d.cameraPixels","Pixels per unit",map_.camera.pixelsPerUnit,0,4096,1),
                number("map2d.cameraSmoothing","Smoothing (seconds)",map_.camera.smoothingSeconds,0,60,0.05),
                number("map2d.cameraMarginX","Horizontal drag margin",map_.camera.deadZoneWidth,0,255,0.25),
                number("map2d.cameraMarginY","Vertical drag margin",map_.camera.deadZoneHeight,0,255,0.25),
                number("map2d.cameraBounds","Keep inside map",map_.camera.clampToMap?1:0,0,1,1,true)};
            fields.back().toggle = true;
        } else if (kind == "parallax") {
            title = text("map2d.parallax","Background scrolling..."); hint = text("map2d.form.parallaxHint","Choose how this layer moves when the camera moves. Use backgrounds without collision.");
            fields = {number("map2d.form.horizontal","Horizontal speed",map_.parallaxX,-100,100,0.1),number("map2d.form.vertical","Vertical speed",map_.parallaxY,-100,100,0.1)};
            presets = {{text("map2d.form.world","World"),{"1","1"}},{text("map2d.form.distant","Distant"),{"0.5","0.5"}},{text("map2d.form.camera","With camera"),{"0","0"}}};
        } else if (kind == "scatter") {
            title = text("map2d.scatter","Scatter density..."); hint = text("map2d.form.scatterHint","Choose how many cells receive grass, rocks or other objects with one stroke.");
            fields = {number("map2d.form.coverage","Coverage (%)",density_,1,100,5,true)};
            presets = {{text("map2d.form.sparse","Sparse"),{"20"}},{text("map2d.form.medium","Medium"),{"50"}},{text("map2d.form.full","Full"),{"100"}}};
        } else if (kind == "atlas") {
            const auto setting = library_.SheetSetting(sheetTexture_);
            title = text("map2d.form.atlas","Sprite sheet layout"); hint = text("map2d.form.atlasHint","Set the size of one graphic and the gaps around it. The sheet preview updates after Apply.");
            fields = {number("map2d.form.pixelWidth","Width (pixels)",setting.cellSize,1,16384,1,true),
                number("map2d.form.pixelHeight","Height (pixels)",setting.Height(),1,16384,1,true),
                number("map2d.form.margin","Outer margin (pixels)",setting.margin,0,16384,1,true),
                number("map2d.form.spacing","Gap between graphics (pixels)",setting.spacing,0,16384,1,true)};
        } else if (kind == "size") {
            title = text("map2d.mapSize","Map Size"); hint = text("map2d.form.sizeHint","Size in tiles. Shrinking removes tiles outside the new area; Undo restores them.");
            hint += " " + text("map2d.form.sizeLimits","Size: 1..4096. Coordinates: 0..size-1. Floors: -50..50. Sheets: 256. Stack: 32.");
            fields = {number("map2d.form.width","Width (tiles)",map_.width,1,Map2DAsset::MaximumSize,1,true),number("map2d.form.height","Height (tiles)",map_.height,1,Map2DAsset::MaximumSize,1,true)};
        } else if (kind == "position") {
            title = text("map2d.goTo","Go To Position"); hint = text("map2d.form.positionHint","Center the view on this tile. Coordinates start at 0.");
            fields = {number("map2d.form.column","Column",HoverInside()?hoverX_:map_.width/2,0,map_.width-1,1,true),
                number("map2d.form.row","Row",HoverInside()?hoverY_:map_.height/2,0,map_.height-1,1,true),
                number("map2d.form.floor","Floor",floor_,Map2DAsset::MinimumFloor,Map2DAsset::MaximumFloor,1,true)};
        } else if (kind == "order") {
            const auto* renderer = std::as_const(context_.Scene()).Components().TilemapRenderers().TryGet(entity_);
            title = text("map2d.layerOrder","Layer order..."); hint = text("map2d.layerOrderHint","Higher numbers draw in front (-1000 to 1000).");
            hint += " " + text("map2d.layerDepthHint","Same plane/floor: each step adds 0.5 depth units, matching Sprite 2D. Floors and entity transforms still affect depth.");
            fields = {number("map2d.form.order","Drawing order",renderer?renderer->sortingOrder:0,-1000,1000,1,true)};
        } else if (kind == "animation-speed") {
            title = text("map2d.form.animationSpeed","Animation speed"); hint = text("map2d.form.speedHint","More frames per second makes the animation faster. Watch the preview before saving.");
            fields = {number("map2d.form.fps","Frames per second",animationSpeed_,0.1,240,1),
                number("map2d.form.animationPhase","Selected tiles: phase (seconds)",animationPhase_,0,86400,0.05)};
            presets = {{text("map2d.form.slow","Slow"),{"4","0"}},{text("map2d.form.normal","Normal"),{"8","0"}},{text("map2d.form.fast","Fast"),{"16","0"}}};
        } else if (kind == "stamp") {
            title = text("map2d.saveStamp","Save selection as a stamp..."); hint = text("map2d.form.stampHint","Give this pattern a name. You can paint it again from Objects / Stamps.");
            fields = {{text("map2d.name","Name"),"",0,0,0,false,true}};
        } else if (kind == "text-data" || kind == "number-data" || kind == "flag-data" || kind == "remove-data") {
            title = text("map2d.form.tileData","Tile data"); hint = text("map2d.form.dataHint","Name a value, for example surface, damage or checkpoint. Scripts can read it from these tiles.");
            std::string name, value = kind == "number-data" ? "0" : kind == "flag-data" ? "1" : "";
            if (const auto* tile = FirstSelectedTile()) for (const auto& [key,data] : tile->properties) {
                if (kind == "remove-data" || (kind == "text-data" && std::holds_alternative<std::string>(data)) ||
                    (kind == "number-data" && (std::holds_alternative<double>(data)||std::holds_alternative<std::int64_t>(data))) ||
                    (kind == "flag-data" && std::holds_alternative<bool>(data))) {
                    name = key; value = std::visit([](const auto& v)->std::string { using V = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<V,std::string>) return v; else return EditorMap2DForm::Format(static_cast<double>(v)); },data); break;
                }
            }
            fields = {{text("map2d.name","Name"),name,0,0,0,false,true}};
            if (kind == "text-data") fields.push_back({text("map2d.form.text","Text"),value,0,0,0,false,true});
            if (kind == "number-data") { auto field = number("map2d.form.number","Number",0,-1e12,1e12); field.value = value; fields.push_back(field); }
            if (kind == "flag-data") {
                auto field = number("map2d.form.flag","Flag",0,0,1,1,true); field.value = value; field.toggle = true; fields.push_back(field);
            }
        } else return false;
        if (kind == "surface" || kind == "opacity" || kind == "offset" || kind == "stamp" || kind.ends_with("-data")) {
            if (!hasSelection_) { SetMessage(text("map2d.selectFirst","Select tiles first: Space, then drag over them."),true); return false; }
        }
        categoryOpen_ = false; messageIsError_ = false;
        form_.Open(window_,std::move(title),std::move(hint),std::move(fields),std::move(presets),[this,kind](const auto& values) -> std::string {
            messageIsError_ = false;
            const auto n = [&](std::size_t i) { return *EditorMap2DForm::Number(values[i]); };
            if (kind == "surface") EditSelectedTiles([&](auto& tile) { auto shape = kb::assets::Map2DCellCollision(map_,tile); shape.friction=static_cast<float>(n(0)); shape.restitution=static_cast<float>(n(1)); tile.collision=shape; });
            else if (kind == "opacity") TransformSelection("opacity",(static_cast<unsigned>(n(0))*255U+50U)/100U);
            else if (kind == "offset") TransformSelection("offset",0xFFFFFFFFU,static_cast<float>(n(0)),static_cast<float>(n(1)));
            else if (kind == "schema") {
                const auto value=kb::assets::ParseMap2DPropertyValue(values[1],values[2]);
                if (!value) return "Invalid property type or default value";
                auto updated=map_; std::string error;
                if (!kb::assets::SetMap2DPropertySchema(updated,values[0],*value,values[3],&error)) return error;
                BeginChange();
                for (const auto& [key,tile]:map_.tiles) { static_cast<void>(tile); stroke_->Record(map_,key); }
                map_=std::move(updated); EndChange();
            }
            else if (kind == "camera") {
                auto updated = map_;
                updated.camera = {.pixelsPerUnit=static_cast<float>(n(2)), .followTarget=values[0],
                    .smoothingSeconds=static_cast<float>(n(3)), .deadZoneWidth=static_cast<float>(n(4)),
                    .deadZoneHeight=static_cast<float>(n(5)), .clampToMap=n(6)!=0, .zoom=static_cast<float>(n(1))};
                std::string error;
                if (!kb::assets::IsMap2DAssetValid(updated, &error)) return error;
                BeginChange(); map_.camera=updated.camera; EndChange();
            }
            else if (kind == "parallax") { BeginChange(); map_.parallaxX=static_cast<float>(n(0)); map_.parallaxY=static_cast<float>(n(1)); EndChange(); }
            else if (kind == "scatter") density_=static_cast<unsigned>(n(0));
            else if (kind == "atlas") SetSheet([&](auto& setting) { setting.cellSize=static_cast<unsigned>(n(0)); setting.cellHeight=static_cast<unsigned>(n(1)); setting.margin=static_cast<unsigned>(n(2)); setting.spacing=static_cast<unsigned>(n(3)); });
            else if (kind == "size") ResizeTo(static_cast<unsigned>(n(0)),static_cast<unsigned>(n(1)));
            else if (kind == "position") { floor_=static_cast<int>(n(2)); CenterOn(static_cast<int>(n(0)),static_cast<int>(n(1))); }
            else if (kind == "order") { if (!SetLayerOrder(static_cast<int>(n(0)))) return std::string(EditorText("map2d.layerLockedHint","This layer is locked. Unlock it in the Inspector to paint.")); }
            else if (kind == "animation-speed") { animationSpeed_=static_cast<float>(n(0)); animationPhase_=static_cast<float>(n(1)); }
            else if (kind == "stamp") { if (!SaveStamp(values[0])) return message_; }
            else if (kind.ends_with("-data")) {
                if (values[0].empty()) return std::string(EditorText("map2d.form.nameRequired","Enter a name before applying."));
                const auto declared=map_.propertyDefaults.find(values[0]);
                const bool integer=declared!=map_.propertyDefaults.end() && std::holds_alternative<std::int64_t>(declared->second);
                if (kind=="number-data" && integer && std::floor(n(1))!=n(1)) return "This property requires a whole number";
                EditSelectedTiles([&](auto& tile) {
                    if (kind == "remove-data") tile.properties.erase(values[0]);
                    else if (kind == "text-data") tile.properties[values[0]]=values[1];
                    else if (kind == "number-data") tile.properties[values[0]]=integer ? kb::assets::Map2DValue{static_cast<std::int64_t>(n(1))} : kb::assets::Map2DValue{n(1)};
                    else tile.properties[values[0]]=n(1)!=0;
                });
            }
            Invalidate(); return messageIsError_ ? message_ : std::string{};
        });
        Invalidate(); return true;
    }

    void TilePropertiesMenu(POINT point) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, T("map2d.rotate", "Rotate 90 degrees").c_str());
        AppendMenuW(menu, MF_STRING, 2, T("map2d.flipX", "Flip horizontally").c_str());
        AppendMenuW(menu, MF_STRING, 3, T("map2d.flipY", "Flip vertically").c_str());
        AppendMenuW(menu, MF_STRING, 4, T("map2d.tint", "Color...").c_str());
        AppendMenuW(menu, MF_STRING, 14, T("map2d.opacity", "Opacity...").c_str());
        AppendMenuW(menu, MF_STRING, 5, T("map2d.animation", "Animation...").c_str());
        AppendMenuW(menu, MF_STRING, 6, T("map2d.offset", "Graphic offset...").c_str());
        AppendMenuW(menu, MF_STRING, 7, T("map2d.resetGraphic", "Reset graphic transform and color").c_str());
        const HMENU collision = CreatePopupMenu();
        const std::array<std::wstring, 6> shapes{T("map2d.passable", "Passable"), T("map2d.solid", "Solid"),
            T("map2d.slopeRight", "Slope rising right"), T("map2d.slopeLeft", "Slope rising left"),
            T("map2d.oneWay", "One-way platform"), T("map2d.drawCollision", "Draw collision outline...")};
        for (unsigned i = 0; i < shapes.size(); ++i) AppendMenuW(collision, MF_STRING, 20U + i, shapes[i].c_str());
        AppendMenuW(collision, MF_STRING, 26, T("map2d.trigger", "Trigger area").c_str());
        AppendMenuW(collision, MF_STRING, 27, T("map2d.useCollision", "Use graphic's default collision").c_str());
        AppendMenuW(collision, MF_STRING, 28, T("map2d.setCollision", "Use this collision for every matching graphic").c_str());
        AppendMenuW(collision, MF_STRING, 29, T("map2d.surface", "Surface friction and bounce...").c_str());
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(collision), T("map2d.collision", "Collision").c_str());
        AppendMenuW(menu, MF_STRING, 8, T("map2d.blockNavigation", "Toggle pathfinding obstacle for this graphic").c_str());
        AppendMenuW(menu, MF_STRING, 9, T("map2d.blockLight", "Toggle light blocking for this graphic").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 10, T("map2d.textData", "Add or edit text data...").c_str());
        AppendMenuW(menu, MF_STRING, 11, T("map2d.numberData", "Add or edit number data...").c_str());
        AppendMenuW(menu, MF_STRING, 12, T("map2d.flagData", "Add or toggle a flag...").c_str());
        AppendMenuW(menu, MF_STRING, 13, T("map2d.removeData", "Remove data...").c_str());
        const auto command = PopUp(menu, point);
        if (command >= 20 && command <= 24) {
            kb::assets::Map2DCollision shape{ .kind = static_cast<kb::assets::Map2DCollisionKind>(command - 20) };
            EditSelectedTiles([&](Map2DTile& tile) { tile.solid = false; tile.collision = shape; });
        } else if (command == 25) {
            StartPolygon();
        } else if (command == 26) {
            EditSelectedTiles([](Map2DTile& tile) { tile.solid = false; tile.collision = kb::assets::Map2DCollision{ .kind = kb::assets::Map2DCollisionKind::Solid, .trigger = true }; });
        } else if (command == 27) {
            EditSelectedTiles([](Map2DTile& tile) { tile.solid = false; tile.collision.reset(); });
        } else if (command == 28) {
            UseSelectedCollisionAsDefault();
        } else if (command == 29) {
            OpenSettings("surface");
        } else if (command >= 1 && command <= 3) TransformSelection(command == 1 ? "rotate" : command == 2 ? "flipX" : "flipY");
        else if (command == 4) {
            static COLORREF colors[16]{};
            const auto tint = FirstSelectedSprite().tint;
            CHOOSECOLORW choice{sizeof(choice)}; choice.hwndOwner = window_; choice.rgbResult = RGB((tint>>24U)&255U,(tint>>16U)&255U,(tint>>8U)&255U);
            choice.lpCustColors = colors; choice.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&choice)) TransformSelection("tint", (static_cast<std::uint32_t>(GetRValue(choice.rgbResult)) << 24U) |
                (static_cast<std::uint32_t>(GetGValue(choice.rgbResult)) << 16U) | (static_cast<std::uint32_t>(GetBValue(choice.rgbResult)) << 8U) | 255U);
        } else if (command == 14) OpenSettings("opacity");
        else if (command == 5) OpenAnimationEditor();
        else if (command == 6) OpenSettings("offset");
        else if (command == 7) TransformSelection("reset");
        else if (command == 8) EditSelectedDefinitions([](auto& def) { def.blocksNavigation = !def.blocksNavigation; });
        else if (command == 9) EditSelectedDefinitions([](auto& def) { def.occludesLight = !def.occludesLight; });
        else if (command == 10) OpenSettings("text-data");
        else if (command == 11) OpenSettings("number-data");
        else if (command == 12) OpenSettings("flag-data");
        else if (command == 13) OpenSettings("remove-data");
    }

    bool OpenAnimationEditor() {
        animationSprite_ = FirstSelectedSprite();
        if (!animationSprite_.IsValid() || animationSprite_.sheet >= map_.sheets.size()) {
            SetMessage(std::string(EditorText("map2d.animation.pickGraphic","Select a painted graphic to animate.")),true); return false;
        }
        const auto* definition = kb::assets::Map2DDefinition(map_,animationSprite_);
        animationFrames_ = definition && !definition->frames.empty() ? definition->frames : std::vector<std::uint32_t>{animationSprite_.cell};
        animationSpeed_ = definition ? definition->framesPerSecond : 8;
        animationPhase_ = animationSprite_.animationPhaseSeconds;
        animationSelected_ = 0; animationPage_ = 0; animationTimelineFirst_ = 0;
        animationEditing_ = true; animationPlaying_ = true; categoryOpen_ = false;
        Invalidate(); return true;
    }

    bool AddAnimationFrame(std::uint32_t cell) {
        if (!animationEditing_ || animationSprite_.sheet >= map_.sheets.size() || animationFrames_.size() >= 256) return false;
        const auto& sheet = map_.sheets[animationSprite_.sheet];
        if (cell >= sheet.columns*sheet.rows) return false;
        animationFrames_.push_back(cell); animationSelected_ = static_cast<int>(animationFrames_.size())-1;
        Invalidate(); return true;
    }

    void RemoveAnimationFrame() {
        if (animationSelected_ < 0 || animationSelected_ >= static_cast<int>(animationFrames_.size())) return;
        animationFrames_.erase(animationFrames_.begin()+animationSelected_);
        animationSelected_ = std::min(animationSelected_,static_cast<int>(animationFrames_.size())-1); Invalidate();
    }

    void MoveAnimationFrame(int direction) {
        const auto next = animationSelected_ + (direction < 0 ? -1 : 1);
        if (animationSelected_ < 0 || next < 0 || next >= static_cast<int>(animationFrames_.size())) return;
        std::swap(animationFrames_[animationSelected_],animationFrames_[next]); animationSelected_=next; Invalidate();
    }

    bool ApplyAnimation() {
        if (!animationEditing_ || animationSprite_.sheet >= map_.sheets.size()) return false;
        BeginChange();
        auto& definition = map_.sheets[animationSprite_.sheet].definitions[animationSprite_.cell];
        definition.frames = animationFrames_; definition.framesPerSecond = animationSpeed_;
        auto editor = Editor();
        for (auto y = selection_.y0; hasSelection_ && y <= selection_.y1; ++y) for (auto x = selection_.x0; x <= selection_.x1; ++x) {
            const auto found = map_.tiles.find(Map2DTileKey(x, y, floor_));
            if (found == map_.tiles.end()) continue;
            auto tile = found->second;
            const auto phase = [&](Map2DSprite& sprite) {
                if (sprite.sheet == animationSprite_.sheet && sprite.cell == animationSprite_.cell)
                    sprite.animationPhaseSeconds = animationPhase_;
            };
            phase(tile.ground);
            for (auto& sprite : tile.borders) phase(sprite);
            for (auto& sprite : tile.items) phase(sprite);
            editor.SetTile(x, y, floor_, tile);
        }
        EndChange();
        if (messageIsError_) return false;
        animationEditing_ = false; Invalidate(); return true;
    }

    void LayoutAnimationEditor() {
        const auto canvas = CanvasRect();
        const LONG left = canvas.left+16, right = canvas.right-16;
        const LONG footer = canvas.bottom-40;
        Add({right-108,footer,right,footer+28},Control::AnimationApply,T("common.apply","Apply"),true);
        Add({right-224,footer,right-116,footer+28},Control::AnimationCancel,T("common.cancel","Cancel"));
        const LONG row = canvas.top+82;
        Add({left,row,left+72,row+28},Control::AnimationPlay,animationPlaying_ ? T("map2d.animation.pause","Pause") : T("map2d.animation.play","Play"));
        Add({left+78,row,right,row+28},Control::AnimationSpeed,TF("map2d.animation.speed","Speed: {fps} frames/s...",{{"fps",EditorMap2DForm::Format(animationSpeed_)}}));
        animationPreview_ = {left,row+36,left+76,row+112};
        animationTimeline_ = {left+88,row+36,right,row+112};
        const int timelineCount = std::max(1,Width(animationTimeline_)/54);
        animationTimelineFirst_ = std::max(0,animationSelected_)/timelineCount*timelineCount;
        for (int i = animationTimelineFirst_; i < std::min(static_cast<int>(animationFrames_.size()),animationTimelineFirst_+timelineCount); ++i) {
            const LONG x = animationTimeline_.left+(i-animationTimelineFirst_)*54;
            Add({x,animationTimeline_.top,x+48,animationTimeline_.bottom},Control::AnimationFrame,L"",i==animationSelected_,i);
        }
        const LONG buttons = row+120;
        Add({left,buttons,left+34,buttons+28},Control::AnimationLeft,L"\u2190").enabled=animationSelected_>0;
        Add({left+40,buttons,left+74,buttons+28},Control::AnimationRight,L"\u2192").enabled=animationSelected_>=0 && animationSelected_+1<static_cast<int>(animationFrames_.size());
        Add({left+80,buttons,left+190,buttons+28},Control::AnimationRemove,T("map2d.animation.remove","Remove frame")).enabled=animationSelected_>=0;
        Add({left+196,buttons,right,buttons+28},Control::AnimationClear,T("map2d.animation.stop","No animation"));
        animationAtlas_ = {left,buttons+58,right,footer-42};
        const int columns = std::max(1,Width(animationAtlas_)/56), rows = std::max(1,Height(animationAtlas_)/56);
        const auto& sheet = map_.sheets[animationSprite_.sheet];
        const int count = static_cast<int>(sheet.columns*sheet.rows), capacity = columns*rows;
        animationPage_ = std::clamp(animationPage_,0,std::max(0,(count-1)/capacity));
        for (int i = animationPage_*capacity; i < std::min(count,(animationPage_+1)*capacity); ++i) {
            const int local = i-animationPage_*capacity;
            const LONG x = left+(local%columns)*56, y = animationAtlas_.top+(local/columns)*56;
            Add({x,y,x+50,y+50},Control::AnimationCell,L"",false,i).enabled=animationFrames_.size()<256;
        }
        Add({left,footer-34,left+100,footer-6},Control::AnimationPrevious,T("map2d.animation.previous","Previous")).enabled=animationPage_>0;
        Add({right-100,footer-34,right,footer-6},Control::AnimationNext,T("map2d.animation.next","Next")).enabled=(animationPage_+1)*capacity<count;
    }

    void DrawAnimationSprite(std::uint32_t cell, RECT rect) {
        const Map2DSprite sprite{animationSprite_.sheet,cell};
        std::uint32_t x=0,y=0,width=0,height=0;
        if (!kb::assets::Map2DSpriteRect(map_,sprite,x,y,width,height)) return;
        const auto* image = Image(map_.sheets[sprite.sheet].texture); if (!image) return;
        const float scale = std::min(static_cast<float>(Width(rect)-6)/width,static_cast<float>(Height(rect)-6)/height);
        const int w = std::max(1,static_cast<int>(width*scale)), h = std::max(1,static_cast<int>(height*scale));
        GdiFlush(); surface_.Blit(rect,rect.left+(Width(rect)-w)/2,rect.top+(Height(rect)-h)/2,w,h,*image,x,y,width,height,256,256);
    }

    void DrawAnimationEditor(HDC dc) {
        const auto canvas = CanvasRect(); Fill(dc,canvas,kPanel);
        Text(dc,{canvas.left+16,canvas.top+8,canvas.right-16,canvas.top+35},T("map2d.animation","Animation..."),kText,DT_LEFT|DT_SINGLELINE,boldFont_);
        Text(dc,{canvas.left+16,canvas.top+38,canvas.right-16,canvas.top+78},T("map2d.animation.hint","Click graphics below to add frames in any order. Applies to every use of this graphic."),kTextDim,DT_LEFT|DT_WORDBREAK);
        Fill(dc,animationPreview_,kTile); Frame(dc,animationPreview_,kBorder);
        auto frame = animationSprite_.cell;
        if (!animationFrames_.empty()) {
            const auto index = animationPlaying_ ? static_cast<std::size_t>(std::fmod((std::max(0.0,previewSeconds_)+animationPhase_)*animationSpeed_,static_cast<double>(animationFrames_.size()))) : static_cast<std::size_t>(std::max(0,animationSelected_));
            frame = animationFrames_[std::min(index,animationFrames_.size()-1)];
        }
        DrawAnimationSprite(frame,animationPreview_);
        Text(dc,{animationAtlas_.left,animationAtlas_.top-24,animationAtlas_.right,animationAtlas_.top-4},
            TF("map2d.animation.frames","Frames: {count} / 256. Click a graphic to add it.",{{"count",std::to_string(animationFrames_.size())}}),kTextDim,DT_LEFT|DT_SINGLELINE,smallFont_);
        for (const auto& box : controls_) {
            if (box.id < Control::AnimationCell) continue;
            DrawButton(dc,box);
            if (box.id == Control::AnimationCell) DrawAnimationSprite(static_cast<unsigned>(box.param),box.rect);
            else if (box.id == Control::AnimationFrame) {
                auto rect = box.rect; rect.bottom-=22;
                DrawAnimationSprite(animationFrames_[box.param],rect);
                Text(dc,{box.rect.left,rect.bottom,box.rect.right,box.rect.bottom},std::to_wstring(box.param+1),kText,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
        }
    }

    RECT PolygonRect() const {
        const auto canvas = CanvasRect();
        const int side = std::max(64, std::min({320, Width(canvas) - 32, Height(canvas) - 130}));
        const int x = canvas.left + (Width(canvas) - side) / 2, y = canvas.top + 60;
        return RECT{x, y, x + side, y + side};
    }

    void StartPolygon() {
        if (!hasSelection_) return;
        polygonEditing_ = true; polygonPoints_.clear(); Invalidate();
    }

    void FinishPolygon() {
        kb::assets::Map2DCollision shape{ .kind = kb::assets::Map2DCollisionKind::Polygon, .polygon = polygonPoints_ };
        if (!kb::assets::IsMap2DCollisionValid(shape)) { SetMessage(std::string(EditorText("map2d.polygon.invalid","Use 3 to 16 corners. Edges must not cross. Backspace removes the last corner.")), true); return; }
        polygonEditing_ = false;
        EditSelectedTiles([&](Map2DTile& tile) { tile.solid = false; tile.collision = shape; });
    }

    void DrawPolygonEditor(HDC dc) {
        const auto canvas = CanvasRect(), rect = PolygonRect();
        Fill(dc, canvas, kPanel);
        Text(dc, RECT{canvas.left + 12, canvas.top + 10, canvas.right - 12, canvas.top + 50}, T("map2d.polygon.title","Click the corners of the solid area"), kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE, boldFont_);
        const auto tile = map_.tiles.find(Map2DTileKey(selection_.x0, selection_.y0, floor_));
        GdiFlush();
        if (tile != map_.tiles.end()) {
            const auto sprite = tile->second.items.empty() ? tile->second.ground : tile->second.items.back();
            DrawSprite(sprite, rect.left, rect.top, Width(rect), rect, 256U, 256U);
        }
        Frame(dc, rect, kBorder);
        const auto old = SelectObject(dc, GetStockObject(DC_PEN)); SetDCPenColor(dc, kSelection);
        for (std::size_t i = 0; i < polygonPoints_.size(); ++i) {
            const auto point = polygonPoints_[i];
            const int x = rect.left + static_cast<int>(point[0] * static_cast<float>(Width(rect)));
            const int y = rect.top + static_cast<int>(point[1] * static_cast<float>(Height(rect)));
            if (i == 0) MoveToEx(dc, x, y, nullptr); else LineTo(dc, x, y);
            Fill(dc, RECT{x - 3, y - 3, x + 4, y + 4}, kSelection);
        }
        if (polygonPoints_.size() > 2) LineTo(dc, rect.left + static_cast<int>(polygonPoints_[0][0] * static_cast<float>(Width(rect))), rect.top + static_cast<int>(polygonPoints_[0][1] * static_cast<float>(Height(rect))));
        SelectObject(dc, old);
        Text(dc, RECT{canvas.left + 8, rect.bottom + 12, canvas.right - 8, rect.bottom + 64},
            T("map2d.polygon.help","Enter: apply   |   Backspace: remove corner   |   Escape: cancel"), kTextDim, DT_CENTER | DT_WORDBREAK);
    }

    [[nodiscard]] bool InSelection(int x, int y) const noexcept {
        return hasSelection_ && x >= static_cast<int>(selection_.x0) && x <= static_cast<int>(selection_.x1) &&
            y >= static_cast<int>(selection_.y0) && y <= static_cast<int>(selection_.y1);
    }

    [[nodiscard]] Clipboard CopySelection() const {
        Clipboard clipboard{};
        if (!hasSelection_) return clipboard;
        clipboard.width = selection_.x1 - selection_.x0 + 1U;
        clipboard.height = selection_.y1 - selection_.y0 + 1U;
        for (std::uint32_t y = selection_.y0; y <= selection_.y1; ++y) {
            auto it = map_.tiles.lower_bound(Map2DTileKey(selection_.x0, y, floor_));
            const auto end = map_.tiles.upper_bound(Map2DTileKey(selection_.x1, y, floor_));
            for (; it != end; ++it) {
                clipboard.tiles.emplace_back(std::pair{ kb::assets::Map2DTileKeyX(it->first) - selection_.x0, y - selection_.y0 }, it->second);
            }
        }
        return clipboard;
    }

    void ClearArea(kb::world2d::Map2DEditor& editor, Map2DRect area) {
        for (std::uint32_t y = area.y0; y <= area.y1; ++y) {
            for (std::uint32_t x = area.x0; x <= area.x1; ++x) editor.SetTile(x, y, floor_, std::nullopt);
        }
    }

    void PasteAt(kb::world2d::Map2DEditor& editor, const Clipboard& clipboard, int x, int y) {
        for (const auto& [offset, tile] : clipboard.tiles) {
            const int tileX = x + static_cast<int>(offset.first);
            const int tileY = y + static_cast<int>(offset.second);
            if (tileX < 0 || tileY < 0 || tileX >= static_cast<int>(map_.width) || tileY >= static_cast<int>(map_.height)) continue;
            editor.SetTile(static_cast<std::uint32_t>(tileX), static_cast<std::uint32_t>(tileY), floor_, tile);
        }
    }

    void BorderAround(kb::world2d::Map2DEditor& editor, int x0, int y0, int x1, int y1) {
        editor.AutoBorder(editor.Clamp(x0 - 1, y0 - 1, x1 + 1, y1 + 1), floor_);
    }

    void CopyToClipboard(bool cut) {
        if (!hasSelection_) {
            SetMessage(std::string{ EditorText("map2d.selectFirst", "Select tiles first: Space, then drag over them.") }, true);
            return;
        }
        clipboard_ = CopySelection();
        if (cut) DeleteSelection();
        SetMessage(EditorLocalization::Format(cut ? "map2d.cut" : "map2d.copied", cut ? "Cut {count} tiles." : "Copied {count} tiles.",
            { { "count", std::to_string(clipboard_.tiles.size()) } }), false);
    }

    void DeleteSelection() {
        if (!hasSelection_) return;
        BeginChange();
        kb::world2d::Map2DEditor editor = Editor();
        ClearArea(editor, selection_);
        BorderAround(editor, static_cast<int>(selection_.x0), static_cast<int>(selection_.y0), static_cast<int>(selection_.x1), static_cast<int>(selection_.y1));
        EndChange();
    }

    void FinishSelectionDrag() {
        if (selecting_) {
            selecting_ = false;
            if (HoverInside()) {
                kb::world2d::Map2DEditor editor = Editor();
                selection_ = editor.Clamp(dragStartX_, dragStartY_, hoverX_, hoverY_);
                hasSelection_ = true;
            }
        }
        if (moving_) {
            moving_ = false;
            const int dx = hoverX_ - dragStartX_;
            const int dy = hoverY_ - dragStartY_;
            if (HoverInside() && (dx != 0 || dy != 0)) {
                const Clipboard moved = CopySelection();
                BeginChange();
                kb::world2d::Map2DEditor editor = Editor();
                const Map2DRect from = selection_;
                ClearArea(editor, from);
                const int x = static_cast<int>(from.x0) + dx;
                const int y = static_cast<int>(from.y0) + dy;
                PasteAt(editor, moved, x, y);
                BorderAround(editor, static_cast<int>(from.x0), static_cast<int>(from.y0), static_cast<int>(from.x1), static_cast<int>(from.y1));
                BorderAround(editor, x, y, x + static_cast<int>(moved.width) - 1, y + static_cast<int>(moved.height) - 1);
                EndChange();
                selection_ = editor.Clamp(x, y, x + static_cast<int>(moved.width) - 1, y + static_cast<int>(moved.height) - 1);
            }
        }
        Invalidate();
    }

    // ----- Brushes -----

    // Saves a tileset, then the palette and every edge of the map follow (one undo step).
    void TilesetChanged(kb::world2d::Map2DTilesetFile& file) {
        std::string error;
        if (!kb::world2d::Map2DTilesetIO::Save(file.path, file.tileset, &error)) {
            SetMessage(error, true);
            return;
        }
        AfterTilesetSave();
    }

    void AfterTilesetSave() {
        ReloadLibrary();
        RefreshTextures();
        BuildPalette();
        Borderize(true);
    }

    // The tileset of a category, read (or begun) for a change and written back.
    [[nodiscard]] bool EditCategoryTileset(const std::string& category, const std::function<void(kb::world2d::Map2DTileset&)>& edit) {
        const std::filesystem::path path = kb::world2d::Map2DCategoryTileset(assetsRoot_, category);
        kb::world2d::Map2DTileset tileset{};
        std::error_code fileError;
        std::string error;
        if (std::filesystem::exists(path, fileError)) {
            std::optional<kb::world2d::Map2DTileset> loaded = kb::world2d::Map2DTilesetIO::Load(path, &error);
            if (!loaded.has_value()) {
                SetMessage(error, true);
                return false;
            }
            tileset = std::move(*loaded);
        }
        edit(tileset);
        if (!kb::world2d::Map2DTilesetIO::Save(path, tileset, &error)) {
            SetMessage(error, true);
            return false;
        }
        AfterTilesetSave();
        return true;
    }

    [[nodiscard]] bool NameTaken(const std::string& name) const {
        return library_.Find(name) != nullptr || library_.FindObject(name) != nullptr;
    }

    [[nodiscard]] std::optional<std::string> AskName(std::string_view title, std::string suggestion, std::string_view hint) {
        if (driven_) return suggestion;
        const std::optional<std::string> name = EditorTextEntryDialog::Show(window_, EditorTextEntryDialogDescriptor{
            .title = std::string{ title },
            .label = std::string{ EditorText("map2d.name", "Name") },
            .value = std::move(suggestion),
            .hint = std::string{ hint },
            .acceptLabel = std::string{ EditorText("map2d.create", "Create") },
        });
        if (!name.has_value() || name->empty()) return std::nullopt;
        if (NameTaken(*name)) {
            SetMessage(EditorLocalization::Format("map2d.nameTaken", "{name} is already taken; pick another name.", { { "name", *name } }), true);
            return std::nullopt;
        }
        return name;
    }

    [[nodiscard]] std::string FreeName(const std::string& base) const {
        if (!NameTaken(base)) return base;
        for (int index = 2; index < 1000; ++index) {
            const std::string candidate = base + " " + std::to_string(index);
            if (!NameTaken(candidate)) return candidate;
        }
        return base;
    }

    // Make ground: the picked floor tile becomes a ground brush (layered above the others) in its
    // category, and its editor opens.
    void MakeGround() {
        const PaletteEntry* entry = Selected();
        if (entry == nullptr || entry->defined) return;
        const PaletteEntry source = *entry;
        const std::optional<std::string> name = AskName(EditorText("map2d.newGround", "New Ground"),
            FreeName(std::string{ EditorText("map2d.groundName", "Ground") }),
            EditorText("map2d.newGroundHint", "Name it after what it is: Grass, Sand, Water..."));
        if (!name.has_value()) return;
        std::int32_t zOrder = 10;
        for (const Map2DGroundBrush* ground : library_.Grounds()) zOrder = std::max(zOrder, ground->zOrder + 10);
        const bool made = EditCategoryTileset(source.category, [&](kb::world2d::Map2DTileset& tileset) {
            tileset.grounds.push_back(Map2DGroundBrush{ .name = *name, .zOrder = zOrder, .texture = source.texture,
                .cellSize = source.cellSize, .tiles = { source.firstCell } });
        });
        if (!made) return;
        UseEntry("g:" + *name);
        OpenBoard(*name);
        SetMessage(std::string{ EditorText("map2d.groundMade", "Ground made. Now its edges: click a black square, then the sprite for it on the left.") }, false);
    }

    // Opens the board of a ground; the palette shows the sprites of the ground's sheet.
    void OpenBoard(const std::string& name) {
        const Map2DGroundBrush* ground = library_.Find(name);
        if (ground == nullptr) return;
        editName_ = name;
        panel_ = Panel::Palette;
        SetSearch(L"");
        if (searchEdit_ != nullptr) SetWindowTextW(searchEdit_, L"");
        paletteScroll_ = 0;
        boardCell_ = NextEmptyBoardCell(*ground);
        BuildPalette();
    }

    void CloseBoard() {
        const std::string name = editName_;
        editName_.clear();
        boardCell_ = -1;
        paletteScroll_ = 0;
        BuildPalette();
        if (Find("g:" + name) != nullptr) UseEntry("g:" + name);
    }

    [[nodiscard]] static int NextEmptyBoardCell(const Map2DGroundBrush& ground) {
        const std::array<BoardSlot, 25U> slots = BoardSlots();
        for (int index = 0; index < 25; ++index) {
            const int piece = slots[static_cast<std::size_t>(index)].piece;
            if (piece >= 0 && ground.borders[static_cast<std::size_t>(piece)] == kb::world2d::kMap2DNoCell) return index;
        }
        return -1;
    }

    void OpenSheetEditor() {
        const PaletteEntry* entry = Selected();
        if (entry == nullptr) return;
        sheetTexture_ = entry->texture;
        panel_ = Panel::Sheet;
        sheetScroll_ = 0;
        // The picked tile shows where it lies in its sheet.
        sheetPick_ = RECT{ static_cast<LONG>(entry->firstCell % entry->sheetColumns), static_cast<LONG>(entry->firstCell / entry->sheetColumns),
            static_cast<LONG>(entry->firstCell % entry->sheetColumns + entry->columns - 1U), static_cast<LONG>(entry->firstCell / entry->sheetColumns + entry->rows - 1U) };
        sheetPickValid_ = true;
    }

    void RenameGround() {
        auto [file, ground] = MutableGround(editName_);
        if (ground == nullptr || driven_) return;
        const std::optional<std::string> name = EditorTextEntryDialog::Show(window_, EditorTextEntryDialogDescriptor{
            .title = std::string{ EditorText("map2d.renameGround", "Rename Ground") }, .label = std::string{ EditorText("map2d.name", "Name") },
            .value = ground->name, .acceptLabel = std::string{ EditorText("map2d.rename", "Rename") } });
        if (!name.has_value() || name->empty() || *name == ground->name) return;
        if (NameTaken(*name)) {
            SetMessage(EditorLocalization::Format("map2d.nameTaken", "{name} is already taken; pick another name.", { { "name", *name } }), true);
            return;
        }
        ground->name = *name;
        editName_ = *name;
        const bool selected = selectedKey_ == "g:" + editName_;
        TilesetChanged(*file);
        if (selected) selectedKey_ = "g:" + *name;
        selectedKey_ = "g:" + *name;
    }

    void DeleteGround() {
        auto [file, ground] = MutableGround(editName_);
        if (ground == nullptr) return;
        if (!driven_) {
            const EditorChoiceDialogResult choice = EditorChoiceDialog::Show(window_, EditorChoiceDialogDescriptor{
                .title = std::string{ EditorText("map2d.deleteGroundTitle", "Delete Ground") },
                .message = EditorLocalization::Format("map2d.deleteGroundQuestion", "Delete the ground {name}?", { { "name", ground->name } }),
                .supportingText = std::string{ EditorText("map2d.deleteGroundDetail", "What is painted on the map stays; only the brush goes.") },
                .primaryLabel = std::string{ EditorText("map2d.delete", "Delete") }, .cancelLabel = std::string{ EditorText("map2d.cancel", "Cancel") },
                .icon = HeroIconKind::XMark, .primaryTone = EditorDialogButtonTone::Destructive });
            if (choice != EditorChoiceDialogResult::Primary) return;
        }
        std::erase_if(file->tileset.grounds, [this](const Map2DGroundBrush& candidate) { return candidate.name == editName_; });
        editName_.clear();
        boardCell_ = -1;
        TilesetChanged(*file);
    }

    // The sprite picked in the palette goes into the board's picked square, and the next empty edge is picked,
    // so a whole set goes square, sprite, sprite, sprite.
    void AssignFromPalette(const PaletteEntry& entry) {
        auto [file, ground] = MutableGround(editName_);
        if (ground == nullptr || boardCell_ < 0 || entry.kind == EntryKind::Ground) return;
        if (entry.kind == EntryKind::Object) {
            SetMessage(std::string{ EditorText("map2d.board.object", "An object covers several squares; pick a single sprite.") }, true);
            return;
        }
        if (entry.texture != ground->texture || entry.cellSize != ground->cellSize) {
            SetMessage(EditorLocalization::Format("map2d.board.otherSheet", "{name} takes sprites from its own sheet, {sheet} (the bright tiles).",
                { { "name", ground->name }, { "sheet", std::filesystem::path{ ground->texture }.filename().string() } }), true);
            return;
        }
        const BoardSlot slot = BoardSlots()[static_cast<std::size_t>(boardCell_)];
        if (slot.piece >= 0) {
            ground->borders[static_cast<std::size_t>(slot.piece)] = entry.firstCell;
        } else if (slot.variant < static_cast<int>(ground->tiles.size())) {
            ground->tiles[static_cast<std::size_t>(slot.variant)] = entry.firstCell;
        } else {
            ground->tiles.push_back(entry.firstCell);
            if (!ground->weights.empty()) ground->weights.push_back(1U);
        }
        const std::string name = ground->name;
        TilesetChanged(*file);
        const Map2DGroundBrush* saved = library_.Find(name);
        boardCell_ = saved != nullptr ? NextEmptyBoardCell(*saved) : -1;
        SetMessage(boardCell_ < 0 ? std::string{ EditorText("map2d.board.complete", "All 12 edges are in. Press Done and paint.") } : std::string{}, false);
    }

    void ClearBoardCell(int index) {
        auto [file, ground] = MutableGround(editName_);
        if (ground == nullptr) return;
        const BoardSlot slot = BoardSlots()[static_cast<std::size_t>(index)];
        if (slot.piece >= 0 && ground->borders[static_cast<std::size_t>(slot.piece)] != kb::world2d::kMap2DNoCell) {
            ground->borders[static_cast<std::size_t>(slot.piece)] = kb::world2d::kMap2DNoCell;
        } else if (slot.variant >= 1 && slot.variant < static_cast<int>(ground->tiles.size())) {
            ground->tiles.erase(ground->tiles.begin() + slot.variant);
            if (slot.variant < static_cast<int>(ground->weights.size())) ground->weights.erase(ground->weights.begin() + slot.variant);
        } else {
            return;
        }
        boardCell_ = index;
        TilesetChanged(*file);
    }


    void SaveObject() {
        const std::uint32_t columns = static_cast<std::uint32_t>(sheetPick_.right - sheetPick_.left + 1);
        const std::uint32_t rows = static_cast<std::uint32_t>(sheetPick_.bottom - sheetPick_.top + 1);
        if (!sheetPickValid_ || columns > 32U || rows > 32U) {
            SetMessage(std::string{ EditorText("map2d.needObject", "First drag over the sprites of the object in the sheet below.") }, true);
            return;
        }
        const kb::world2d::Map2DSheetSetting setting = library_.SheetSetting(sheetTexture_);
        const std::uint32_t sheetColumns = SheetColumns(sheetTexture_, setting.cellSize);
        const std::optional<std::string> name = AskName(EditorText("map2d.newObjectTitle", "New Object"),
            FreeName(std::string{ EditorText("map2d.objectName", "Object") }),
            EditorText("map2d.newObjectHint", "Name it after what it is: Oak, House, Fence..."));
        if (!name.has_value()) return;
        const std::string category = kb::world2d::Map2DCategoryOf(assetsRoot_, kb::world2d::Map2DAssetFile(assetsRoot_, sheetTexture_));
        const Map2DObjectBrush object{ .name = *name, .texture = sheetTexture_, .cellSize = setting.cellSize,
            .firstCell = static_cast<std::uint32_t>(sheetPick_.top) * sheetColumns + static_cast<std::uint32_t>(sheetPick_.left), .columns = columns, .rows = rows };
        if (!EditCategoryTileset(category, [&object](kb::world2d::Map2DTileset& tileset) { tileset.objects.push_back(object); })) return;
        UseEntry("o:" + *name);
        SetMessage(EditorLocalization::Format("map2d.objectSaved", "{name} is in the palette now: click the map to place it.", { { "name", *name } }), false);
    }

    void DeleteNamedObject() {
        const PaletteEntry* entry = Selected();
        if (entry == nullptr || entry->kind != EntryKind::Object || !entry->defined) return;
        const std::string name = entry->name;
        for (kb::world2d::Map2DTilesetFile& file : library_.files) {
            if (file.tileset.FindObject(name) == nullptr) continue;
            std::erase_if(file.tileset.objects, [&name](const Map2DObjectBrush& object) { return object.name == name; });
            TilesetChanged(file);
            return;
        }
    }

    void SetSheet(const std::function<void(kb::world2d::Map2DSheetSetting&)>& edit) {
        auto setting = library_.SheetSetting(sheetTexture_);
        edit(setting);
        const SheetImage* image = Image(sheetTexture_);
        if (image == nullptr || !setting.Fits(image->width, image->height)) {
            SetMessage(std::string{ EditorText("map2d.invalidCellSize", "The tile size must divide the image width and height without leaving partial tiles.") }, true);
            return;
        }
        const std::string category = kb::world2d::Map2DCategoryOf(assetsRoot_, kb::world2d::Map2DAssetFile(assetsRoot_, sheetTexture_));
        const std::string texture = sheetTexture_;
        static_cast<void>(EditCategoryTileset(category, [&](kb::world2d::Map2DTileset& tileset) {
            kb::world2d::Map2DSheetSetting* sheet = tileset.FindSheet(texture);
            if (sheet == nullptr) {
                tileset.sheets.push_back(library_.SheetSetting(texture));
                sheet = &tileset.sheets.back();
                sheet->texture = texture;
            }
            edit(*sheet);
        }));
        sheetPickValid_ = false;
    }

    // ----- Menus -----

    void ImportImages(const std::vector<std::filesystem::path>& paths) {
        if (paths.empty()) return;
        if (!editName_.empty()) CloseBoard();
        std::string first;
        std::string failure;
        std::size_t imported = 0U;
        for (const auto& path : paths) {
            const auto dimensions = kb::world2d::ReadMap2DImageSize(path);
            if (!dimensions || static_cast<std::uint64_t>(dimensions->first) * dimensions->second > 16777216U) {
                failure = std::string{ EditorText("map2d.imageTooLarge", "Choose a PNG or BMP image with at most 16 million pixels.") };
                continue;
            }
            SheetImage decoded;
            if (!LoadImagePixels(path, decoded)) {
                failure = EditorLocalization::Format("map2d.importFailed", "Could not read {file}. Choose a PNG or BMP image.", { { "file", Utf8(path.filename()) } });
                continue;
            }
            const auto texture = kb::world2d::ImportMap2DImage(assetsRoot_, path, &failure);
            if (!texture) continue;
            if (first.empty()) first = *texture;
            ++imported;
        }
        Reload();
        panel_ = Panel::Palette;
        paletteType_ = "all";
        category_ = std::string{ kAllCategory };
        SetSearch(L"");
        if (searchEdit_ != nullptr) SetWindowTextW(searchEdit_, L"");
        if (!first.empty()) {
            sheetTexture_ = first;
            const SheetImage* image = Image(first);
            std::uint32_t cellSize = 32U;
            if (image != nullptr && (image->width % cellSize != 0U || image->height % cellSize != 0U)) {
                for (std::uint32_t candidate : { 16U, 48U, 64U, 24U, 8U }) {
                    if (image->width % candidate == 0U && image->height % candidate == 0U) {
                        cellSize = candidate;
                        break;
                    }
                }
            }
            if (image != nullptr && image->width == image->height && image->width % cellSize != 0U) cellSize = image->width;
            if (cellSize != 32U) SetSheet([cellSize](kb::world2d::Map2DSheetSetting& setting) { setting.cellSize = cellSize; });
            for (const PaletteEntry& entry : entries_) {
                if (entry.texture == first) { UseEntry(entry.key); RevealSelected(); break; }
            }
            // Show the cut immediately, so a different sprite size is one click away.
            panel_ = Panel::Sheet;
            sheetScroll_ = 0;
            sheetPickValid_ = false;
            tutorialStep_ = -1;
        }
        SetMessage(failure.empty() ? EditorLocalization::Format("map2d.imported", "Added {count} images. Check the tile size, then press Paint map.",
            { { "count", std::to_string(imported) } }) : failure, !failure.empty());
        Invalidate();
    }

    void ChooseImages() {
        std::vector<wchar_t> names(65536U, L'\0');
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window_;
        const std::wstring title = T("map2d.addGraphics", "Add graphics...");
        dialog.lpstrTitle = title.c_str();
        dialog.lpstrFilter = L"PNG / BMP\0*.png;*.bmp\0\0";
        dialog.lpstrFile = names.data();
        dialog.nMaxFile = static_cast<DWORD>(names.size());
        dialog.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog)) return;
        const std::filesystem::path first{ names.data() };
        const wchar_t* next = names.data() + first.native().size() + 1U;
        std::vector<std::filesystem::path> paths;
        if (*next == L'\0') paths.push_back(first);
        else for (; *next != L'\0'; next += std::wcslen(next) + 1U) paths.push_back(first / next);
        ImportImages(paths);
    }

    void ShowPaletteMenu(const RECT& anchor) {
        const PaletteEntry* entry = Selected();
        if (entry == nullptr) return;
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1U, T("map2d.spriteSettings", "Sprite size and objects...").c_str());
        if (entry->kind == EntryKind::Ground) AppendMenuW(menu, MF_STRING, 2U, T("map2d.editEdges", "Edit automatic edges...").c_str());
        else if (!entry->defined && entry->kind != EntryKind::Object) AppendMenuW(menu, MF_STRING, 3U, T("map2d.addEdges", "Add automatic edges...").c_str());
        switch (PopUp(menu, POINT{ anchor.left, anchor.bottom })) {
        case 1U: OpenSheetEditor(); break;
        case 2U: OpenBoard(entry->name); break;
        case 3U: MakeGround(); break;
        default: break;
        }
    }

    [[nodiscard]] UINT PopUp(HMENU menu, POINT point) const {
        if (window_ == nullptr) {
            DestroyMenu(menu);
            return 0U;
        }
        ClientToScreen(window_, &point);
        const UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, window_, nullptr));
        DestroyMenu(menu);
        return command;
    }

    void ShowMapSizeMenu(const RECT& anchor) {
        HMENU menu = CreatePopupMenu();
        const std::array<std::uint32_t, 6U> sizes{ 32U, 64U, 128U, 256U, 512U, 1024U };
        for (std::size_t index = 0U; index < sizes.size(); ++index) {
            const std::wstring label = std::to_wstring(sizes[index]) + L" \u00D7 " + std::to_wstring(sizes[index]);
            AppendMenuW(menu, MF_STRING, 1U + index, label.c_str());
        }
        AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
        AppendMenuW(menu, MF_STRING, 100U, T("map2d.wider", "Wider by 16 tiles").c_str());
        AppendMenuW(menu, MF_STRING, 101U, T("map2d.taller", "Taller by 16 tiles").c_str());
        AppendMenuW(menu, MF_STRING, 102U, T("map2d.otherSize", "Other size...").c_str());
        const UINT command = PopUp(menu, POINT{ anchor.left, anchor.bottom });
        std::uint32_t width = map_.width;
        std::uint32_t height = map_.height;
        if (command >= 1U && command <= sizes.size()) {
            width = height = sizes[command - 1U];
        } else if (command == 100U) {
            width += 16U;
        } else if (command == 101U) {
            height += 16U;
        } else if (command == 102U) {
            OpenSettings("size");
            return;
        } else {
            return;
        }
        if (width < map_.width || height < map_.height) {
            const EditorChoiceDialogResult choice = EditorChoiceDialog::Show(window_, EditorChoiceDialogDescriptor{
                .title = std::string{ EditorText("map2d.mapSize", "Map Size") }, .message = std::string{ EditorText("map2d.smaller", "Make the map smaller?") },
                .supportingText = std::string{ EditorText("map2d.smallerDetail", "Tiles outside the new size are removed.") },
                .primaryLabel = std::string{ EditorText("map2d.resize", "Resize") }, .cancelLabel = std::string{ EditorText("map2d.cancel", "Cancel") },
                .icon = HeroIconKind::RectangleGroup, .primaryTone = EditorDialogButtonTone::Destructive });
            if (choice != EditorChoiceDialogResult::Primary) return;
        }
        ResizeTo(width, height);
    }

    void ResizeTo(std::uint32_t width, std::uint32_t height) {
        FinishStroke();
        std::string error;
        if (!EditorMap2DWindow::EditSettings(context_, entity_, [width, height](Map2DAsset& map) { kb::world2d::ResizeMap2D(map, width, height); }, &error)) SetMessage(error, true);
        hasSelection_ = false;
        Invalidate();
    }

    [[nodiscard]] static bool ParsePair(const std::string& text, std::uint32_t& first, std::uint32_t& second) {
        std::string clean = text;
        std::ranges::replace_if(clean, [](char character) { return character == 'x' || character == 'X' || character == ','; }, ' ');
        std::istringstream input{ clean };
        std::uint32_t a = 0U;
        std::uint32_t b = 0U;
        if (!(input >> a >> b) || a == 0U || b == 0U) return false;
        first = a;
        second = b;
        return true;
    }

    void SetOrientation(kb::assets::Map2DOrientation orientation) {
        if (orientation == map_.orientation) return;
        FinishStroke();
        std::string error;
        if (!EditorMap2DWindow::EditSettings(context_, entity_, [orientation](Map2DAsset& map) { map.orientation = orientation; }, &error)) SetMessage(error, true);
        Invalidate();
    }

    bool SetLayerOrder(int order) {
        if (order < -1000 || order > 1000 || context_.HasPlayModeSceneSession()) return false;
        FinishStroke();
        const bool changed = context_.ExecuteSceneCommand("Change Tilemap Order", [this, order]() {
            auto& scene = context_.Scene();
            const auto* current = std::as_const(scene).Components().TilemapRenderers().TryGet(entity_);
            auto renderer = current ? *current : kb::scene::TilemapRendererComponent{};
            renderer.sortingOrder = order;
            scene.Components().TilemapRenderers().Set(entity_, renderer);
            return true;
        });
        Invalidate();
        if (present_) present_();
        return changed;
    }

    void ShowViewMenu(const RECT& anchor) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 20U, T("map2d.mapSize", "Map Size").c_str());
        AppendMenuW(menu, MF_STRING | (map_.orientation == kb::assets::Map2DOrientation::Flat ? MF_CHECKED : 0U), 21U,
            T("map2d.topDown", "View from above").c_str());
        AppendMenuW(menu, MF_STRING | (map_.orientation == kb::assets::Map2DOrientation::Wall ? MF_CHECKED : 0U), 22U,
            T("map2d.sideView", "Side view (platformer)").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
        AppendMenuW(menu, MF_STRING, 32U, T("map2d.layerOrder", "Layer order...").c_str());
        AppendMenuW(menu, MF_STRING | (paintInScene_ ? MF_CHECKED : 0U), 33U, T("map2d.paintScene", "Paint in Scene View").c_str());
        AppendMenuW(menu, MF_STRING | (map_.sortByY ? MF_CHECKED : 0U), 34U, T("map2d.sortByRowScope", "Sort tiles by row (within this map)").c_str());
        AppendMenuW(menu, MF_STRING | (map_.lit ? MF_CHECKED : 0U), 35U, T("map2d.lit", "Lights affect this layer").c_str());
        AppendMenuW(menu, MF_STRING, 36U, T("map2d.parallax", "Background scrolling...").c_str());
        AppendMenuW(menu, MF_STRING, 39U, T("map2d.cameraSettings", "Play camera...").c_str());
        AppendMenuW(menu, MF_STRING, 40U, T("map2d.propertySchema", "Property schema...").c_str());
        AppendMenuW(menu, MF_STRING, 37U, T("map2d.makeUnique", "Make this map independent").c_str());
        HMENU tools = CreatePopupMenu();
        AppendMenuW(tools, MF_STRING, 38U, T("map2d.scatter", "Scatter density...").c_str());
        AppendMenuW(tools, MF_STRING, 23U, T("map2d.rectangleTool", "Paint a rectangle (Shift + drag)").c_str());
        AppendMenuW(tools, MF_STRING, 24U, T("map2d.pick", "Pick").c_str());
        AppendMenuW(tools, MF_STRING | (tool_ == Tool::Collision ? MF_CHECKED : 0U), 31U,
            T("map2d.paintCollision", "Paint walls / solid platforms").c_str());
        AppendMenuW(tools, MF_STRING | (automagic_ ? MF_CHECKED : 0U), 25U, T("map2d.automagic", "Auto edges").c_str());
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), T("map2d.extraTools", "Extra tools").c_str());
        HMENU floors = CreatePopupMenu();
        AppendMenuW(floors, MF_STRING, 26U, T("map2d.tip.floorUp", "Floor up (Page Up, Ctrl+wheel)").c_str());
        AppendMenuW(floors, MF_STRING, 27U, T("map2d.tip.floorDown", "Floor down (Page Down, Ctrl+wheel)").c_str());
        AppendMenuW(floors, MF_STRING, 28U, T("map2d.groundFloor", "Return to ground floor").c_str());
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(floors), T("map2d.floors", "Floors").c_str());
        HMENU view = CreatePopupMenu();
        const HMENU root = menu;
        menu = view;
        AppendMenuW(menu, MF_STRING | (allFloors_ ? MF_CHECKED : 0U), 1U, (T("map2d.menu.allFloors", "Show floors below") + L"\tCtrl+W").c_str());
        AppendMenuW(menu, MF_STRING | (shade_ ? MF_CHECKED : 0U), 2U, (T("map2d.menu.shade", "Shade floors below") + L"\tQ").c_str());
        AppendMenuW(menu, MF_STRING | (ghostHigher_ ? MF_CHECKED : 0U), 3U, (T("map2d.menu.ghost", "Ghost floors above") + L"\tCtrl+L").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
        AppendMenuW(menu, MF_STRING | (grid_ ? MF_CHECKED : 0U), 4U, (T("map2d.menu.grid", "Show grid") + L"\tShift+G").c_str());
        AppendMenuW(menu, MF_STRING | (clientBox_ ? MF_CHECKED : 0U), 5U, (T("map2d.menu.clientBox", "Show client box") + L"\tShift+I").c_str());
        AppendMenuW(menu, MF_STRING | (live_ ? MF_CHECKED : 0U), 6U, T("map2d.menu.live", "Show strokes in the scene at once").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
        AppendMenuW(menu, MF_STRING, 7U, (T("map2d.menu.zoomIn", "Zoom in") + L"\tCtrl++").c_str());
        AppendMenuW(menu, MF_STRING, 8U, (T("map2d.menu.zoomOut", "Zoom out") + L"\tCtrl+-").c_str());
        AppendMenuW(menu, MF_STRING, 9U, (T("map2d.menu.zoomNormal", "Zoom 100%") + L"\tCtrl+0").c_str());
        AppendMenuW(menu, MF_STRING, 10U, (T("map2d.menu.goTo", "Go to position...") + L"\tCtrl+G").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
        AppendMenuW(menu, MF_STRING, 11U, T("map2d.menu.borderize", "Redo all edges of the map").c_str());
        menu = root;
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), T("map2d.view", "View").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
        AppendMenuW(menu, MF_STRING, 29U, T("map2d.spritesFolder", "Sprites folder").c_str());
        AppendMenuW(menu, MF_STRING, 30U, T("map2d.reload", "Reload (F5)").c_str());
        const UINT command = PopUp(menu, POINT{ anchor.left, anchor.bottom });
        switch (command) {
        case 38U: OpenSettings("scatter"); break;
        case 37U: static_cast<void>(MakeUnique()); break;
        case 33U: paintInScene_ = !paintInScene_; if (!paintInScene_) FinishSceneStroke();
            SetMessage(std::string(EditorText("map2d.scenePaintHelp","Select a palette brush, then paint the selected layer in Scene View. Ctrl erases. Space returns to selection.")), false); break;
        case 34U: BeginChange(); map_.sortByY = !map_.sortByY; EndChange(); break;
        case 35U: BeginChange(); map_.lit = !map_.lit; EndChange(); break;
        case 36U: OpenSettings("parallax"); break;
        case 39U: OpenSettings("camera"); break;
        case 40U: OpenSettings("schema"); break;
        case 32U: OpenSettings("order"); break;
        case 20U: ShowMapSizeMenu(anchor); break;
        case 21U:
        case 22U:
            SetOrientation(command == 21U ? kb::assets::Map2DOrientation::Flat : kb::assets::Map2DOrientation::Wall);
            break;
        case 23U: tool_ = Tool::Rectangle; break;
        case 24U: tool_ = Tool::Picker; break;
        case 25U: automagic_ = !automagic_; break;
        case 26U: ChangeFloor(1); break;
        case 27U: ChangeFloor(-1); break;
        case 28U: ChangeFloor(-floor_); break;
        case 29U: Activate(ControlBox{ .id = Control::OpenSpritesFolder }); break;
        case 30U: Reload(); break;
        case 31U:
            tool_ = Tool::Collision;
            SetMessage(std::string{ EditorText("map2d.collisionHelp", "Paint where movement is blocked. Ctrl + paint clears a block. Graphics stay unchanged.") }, false);
            break;
        case 1U: allFloors_ = !allFloors_; break;
        case 2U: shade_ = !shade_; break;
        case 3U: ghostHigher_ = !ghostHigher_; break;
        case 4U: grid_ = !grid_; break;
        case 5U: clientBox_ = !clientBox_; break;
        case 6U:
            live_ = !live_;
            if (live_) Publish(false);
            break;
        case 7U: Zoom(1, CanvasCenter()); break;
        case 8U: Zoom(-1, CanvasCenter()); break;
        case 9U: SetZoom(kNormalZoom, CanvasCenter()); break;
        case 10U: GoToPosition(); break;
        case 11U: Borderize(true); break;
        default: break;
        }
    }

    void ShowContextMenu(int x, int y, int tileX, int tileY) {
        if (!InSelection(tileX, tileY)) { selection_ = Editor().Clamp(tileX, tileY, tileX, tileY); hasSelection_ = true; }
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 100U, T("map2d.tileProperties", "Tile properties...").c_str());
        AppendMenuW(menu, MF_STRING, 101U, T("map2d.saveStamp", "Save selection as a stamp...").c_str());
        AppendMenuW(menu, MF_STRING | (Selected() ? 0U : MF_GRAYED), 102U, T("map2d.replaceGraphic", "Replace this graphic everywhere with the palette selection").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
        const UINT selection = hasSelection_ ? 0U : MF_GRAYED;
        AppendMenuW(menu, MF_STRING | selection, 1U, (T("map2d.menu.cut", "Cut") + L"\tCtrl+X").c_str());
        AppendMenuW(menu, MF_STRING | selection, 2U, (T("map2d.menu.copy", "Copy") + L"\tCtrl+C").c_str());
        AppendMenuW(menu, MF_STRING | (clipboard_.tiles.empty() ? MF_GRAYED : 0U), 3U, (T("map2d.menu.paste", "Paste") + L"\tCtrl+V").c_str());
        AppendMenuW(menu, MF_STRING | selection, 4U, (T("map2d.menu.delete", "Delete") + L"\tDelete").c_str());
        AppendMenuW(menu, MF_STRING | selection, 5U, (T("map2d.menu.borderizeSelection", "Redo edges of the selection") + L"\tCtrl+B").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
        AppendMenuW(menu, MF_STRING, 6U, (T("map2d.menu.pick", "Pick this brush") + L"\tCtrl+Alt+click").c_str());
        AppendMenuW(menu, MF_STRING, 7U, T("map2d.menu.removeItem", "Remove the top item").c_str());
        AppendMenuW(menu, MF_STRING, 8U, T("map2d.menu.clearTile", "Clear the tile").c_str());
        const UINT command = PopUp(menu, POINT{ x, y });
        switch (command) {
        case 100U: TilePropertiesMenu(POINT{x, y}); break;
        case 101U: OpenSettings("stamp"); break;
        case 102U: ReplaceSelectedGraphic(); break;
        case 1U: CopyToClipboard(true); break;
        case 2U: CopyToClipboard(false); break;
        case 3U: pasting_ = true; break;
        case 4U: DeleteSelection(); break;
        case 5U: Borderize(false); break;
        case 6U: Pick(tileX, tileY); break;
        case 7U:
        case 8U: {
            BeginChange();
            kb::world2d::Map2DEditor editor = Editor();
            editor.Erase(editor.Clamp(tileX, tileY, tileX, tileY), floor_, command == 7U);
            EndChange();
            break;
        }
        default: break;
        }
    }

    void GoToPosition() { OpenSettings("position"); }

    // ----- Input -----

    [[nodiscard]] const ControlBox* ControlAt(int x, int y) const {
        if (animationEditing_) {
            for (const auto& box : controls_) if (box.id >= Control::AnimationCell && Contains(box.rect,x,y)) return &box;
            return nullptr;
        }
        // The tutorial's buttons first: they lie over everything.
        for (const ControlBox& box : controls_) {
            if ((box.id == Control::TutorialBack || box.id == Control::TutorialNext || box.id == Control::TutorialSkip) && Contains(box.rect, x, y)) return &box;
        }
        if (tutorialStep_ >= 0) return nullptr;
        // The open category list lies over the palette.
        if (categoryOpen_ && Contains(categoryListRect_, x, y)) {
            for (const ControlBox& box : controls_) {
                if (box.id == Control::CategoryItem && Contains(box.rect, x, y)) return &box;
            }
            return nullptr;
        }
        for (const ControlBox& box : controls_) {
            if (Contains(box.rect, x, y)) return &box;
        }
        return nullptr;
    }

    [[nodiscard]] bool TileAt(int x, int y, int& tileX, int& tileY) const {
        if (!Contains(CanvasRect(), x, y)) return false;
        tileX = FloorDivide(x - OriginX(), zoom_);
        tileY = FloorDivide(y - OriginY(), zoom_);
        return true;
    }

    [[nodiscard]] POINT CanvasCenter() const noexcept {
        const RECT canvas = CanvasRect();
        return POINT{ (canvas.left + canvas.right) / 2, (canvas.top + canvas.bottom) / 2 };
    }

    void SetZoom(int next, POINT around) {
        if (next == zoom_) return;
        // Keep the tile under the cursor where it is.
        const RECT canvas = CanvasRect();
        const double mapX = (around.x - canvas.left + panX_) / zoom_;
        const double mapY = (around.y - canvas.top + panY_) / zoom_;
        zoom_ = next;
        panX_ = mapX * zoom_ - (around.x - canvas.left);
        panY_ = mapY * zoom_ - (around.y - canvas.top);
    }

    void Zoom(int steps, POINT around) {
        const auto current = std::ranges::find(kZoomLevels, zoom_);
        int index = current == kZoomLevels.end() ? 6 : static_cast<int>(current - kZoomLevels.begin());
        index = std::clamp(index + steps, 0, static_cast<int>(kZoomLevels.size()) - 1);
        SetZoom(kZoomLevels[static_cast<std::size_t>(index)], around);
    }

    void ChangeFloor(int step) {
        floor_ = std::clamp(floor_ + step, Map2DAsset::MinimumFloor, Map2DAsset::MaximumFloor);
        hasSelection_ = false;
    }

    void ChangeBrushSize(int step) {
        brushSize_ = std::clamp(brushSize_ + step * 2, 1, kMaximumBrushSize);
    }

    void Activate(const ControlBox& box) {
        message_.clear();
        switch (box.id) {
        case Control::AnimationCell: AddAnimationFrame(static_cast<unsigned>(box.param)); break;
        case Control::AnimationFrame: animationSelected_=box.param; break;
        case Control::AnimationApply: ApplyAnimation(); break;
        case Control::AnimationCancel: animationEditing_=false; break;
        case Control::AnimationRemove: RemoveAnimationFrame(); break;
        case Control::AnimationLeft: MoveAnimationFrame(-1); break;
        case Control::AnimationRight: MoveAnimationFrame(1); break;
        case Control::AnimationSpeed: OpenSettings("animation-speed"); break;
        case Control::AnimationPlay: animationPlaying_=!animationPlaying_; break;
        case Control::AnimationClear: animationFrames_.clear(); animationSelected_=-1; break;
        case Control::AnimationPrevious: --animationPage_; break;
        case Control::AnimationNext: ++animationPage_; break;
        case Control::Save: static_cast<void>(Save()); break;
        case Control::ImportImages: ChooseImages(); break;
        case Control::PaletteMenu: ShowPaletteMenu(box.rect); break;
        case Control::Undo: Undo(); break;
        case Control::Redo: Redo(); break;
        case Control::ToolDraw: tool_ = Tool::Draw; break;
        case Control::ToolSelect: tool_ = Tool::Select; break;
        case Control::ToolEraser: tool_ = Tool::Eraser; break;
        case Control::ToolFill: tool_ = Tool::Fill; break;
        case Control::ToolRectangle: tool_ = Tool::Rectangle; break;
        case Control::ToolPicker: tool_ = Tool::Picker; break;
        case Control::BrushSize: brushSize_ = brushSize_ >= kMaximumBrushSize ? 1 : brushSize_ + 2; break;
        case Control::FloorDown: ChangeFloor(-1); break;
        case Control::FloorUp: ChangeFloor(1); break;
        case Control::Automagic:
            automagic_ = !automagic_;
            SetMessage(std::string{ automagic_ ? EditorText("map2d.automagicOn", "Auto edges on: grounds draw their edges.")
                                               : EditorText("map2d.automagicOff", "Auto edges off: edges stay as they are (Ctrl+B redoes them).") }, false);
            break;
        case Control::Grid: grid_ = !grid_; break;
        case Control::ClientBox: clientBox_ = !clientBox_; break;
        case Control::ViewMenu: ShowViewMenu(box.rect); break;
        case Control::MapSize: ShowMapSizeMenu(box.rect); break;
        case Control::ShowInScene:
            if (publishPending_ || !live_) {
                live_ = true;
                Publish(false);
            }
            context_.SelectEntity(entity_);
            static_cast<void>(context_.FrameSelectedEntitiesInViewport());
            if (present_) present_();
            if (owner_ != nullptr) SetForegroundWindow(owner_);
            break;
        case Control::Help: tutorialStep_ = tutorialStep_ >= 0 ? -1 : 0; break;
        case Control::TypeList: OpenList(true, !(categoryOpen_ && listIsType_)); break;
        case Control::CategoryList: OpenList(false, !(categoryOpen_ && !listIsType_)); break;
        case Control::CategoryItem: PickCategory(box.param); break;
        case Control::PaletteTile: {
            const std::vector<const PaletteEntry*> visible = VisibleEntries();
            if (box.param >= 0 && box.param < static_cast<int>(visible.size())) {
                const PaletteEntry entry = *visible[static_cast<std::size_t>(box.param)];
                UseEntry(entry.key);
                if (!editName_.empty()) AssignFromPalette(entry);
            }
            break;
        }
        case Control::EditBrush:
            if (const PaletteEntry* entry = Selected(); entry != nullptr && entry->defined) {
                if (entry->kind == EntryKind::Ground) OpenBoard(entry->name);
                else OpenSheetEditor();
            }
            break;
        case Control::MakeGround: MakeGround(); break;
        case Control::NewObject: OpenSheetEditor(); break;
        case Control::OpenSpritesFolder: {
            const std::filesystem::path folder = assetsRoot_ / kb::assets::Utf8Path(kb::world2d::kMap2DSpritesFolder);
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            if (!driven_) ShellExecuteW(window_, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            SetMessage(std::string{ EditorText("map2d.folderOpened", "Put PNG sprite sheets there (a folder per category), then press Reload.") }, false);
            break;
        }
        case Control::Reload: Reload(); break;
        case Control::Back:
            panel_ = Panel::Palette;
            sheetPickValid_ = false;
            break;
        case Control::RenameGround: RenameGround(); break;
        case Control::ZOrderDown:
        case Control::ZOrderUp: {
            auto [file, ground] = MutableGround(editName_);
            if (ground != nullptr) {
                const std::int32_t step = Shift() ? 10 : 1;
                ground->zOrder += box.id == Control::ZOrderUp ? step : -step;
                TilesetChanged(*file);
            }
            break;
        }
        case Control::BoardCell: boardCell_ = boardCell_ == box.param ? -1 : box.param; break;
        case Control::BoardDone: CloseBoard(); break;
        case Control::DeleteGround: DeleteGround(); break;
        case Control::CellSize: {
            std::uint32_t size = static_cast<std::uint32_t>(box.param);
            if (size == 0U) {
                OpenSettings("atlas");
            } else SetSheet([size](auto& sheet) { sheet.cellSize = size; sheet.cellHeight = 0U; sheet.margin = 0U; sheet.spacing = 0U; });
            break;
        }
        case Control::JoinSprites: SetSheet([](kb::world2d::Map2DSheetSetting& sheet) { sheet.separate = !sheet.separate; }); break;
        case Control::SaveObject: SaveObject(); break;
        case Control::DeleteObject: DeleteNamedObject(); break;
        case Control::TutorialBack: tutorialStep_ = std::max(0, tutorialStep_ - 1); break;
        case Control::TutorialNext:
            if (tutorialStep_ + 1 >= kTutorialSteps) FinishTutorial();
            else ++tutorialStep_;
            break;
        case Control::TutorialSkip: FinishTutorial(); break;
        case Control::None: break;
        }
        Invalidate();
    }

    [[nodiscard]] bool SheetCellAt(int x, int y, std::uint32_t& column, std::uint32_t& row) const {
        if (!sheet_.visible || !Contains(sheet_.rect, x, y)) return false;
        const int localX = x - sheet_.rect.left;
        const int localY = y - sheet_.rect.top + sheetScroll_;
        if (localX < 0 || localY < 0) return false;
        column = static_cast<std::uint32_t>(localX / sheet_.cellPixels);
        row = static_cast<std::uint32_t>(localY / sheet_.cellPixels);
        return column < sheet_.columns && row < sheet_.rows;
    }

    void Capture() const {
        if (window_ != nullptr) SetCapture(window_);
    }

    void Release() const {
        if (window_ != nullptr && GetCapture() == window_) ReleaseCapture();
    }

    void BeginPan(int x, int y) {
        panning_ = true;
        panMoved_ = false;
        panStart_ = POINT{ x, y };
        panStartX_ = panX_;
        panStartY_ = panY_;
        Capture();
    }

    void OnLeftDown(int x, int y) {
        Layout();
        if (form_.IsOpen()) { form_.Click(x,y); Invalidate(); return; }
        if (animationEditing_) {
            if (const auto* box = ControlAt(x,y); box && box->enabled) Activate(*box);
            return;
        }
        if (polygonEditing_) {
            const auto rect = PolygonRect();
            if (x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom && polygonPoints_.size() < 16U) {
                polygonPoints_.push_back({static_cast<float>(x - rect.left) / static_cast<float>(Width(rect)), static_cast<float>(y - rect.top) / static_cast<float>(Height(rect))});
                Invalidate();
            }
            return;
        }
        // An open list closes on any click; a click on one of its entries picks it.
        if (categoryOpen_) {
            const ControlBox* box = ControlAt(x, y);
            if (box != nullptr && box->id == Control::CategoryItem) PickCategory(box->param);
            else if (box != nullptr && (box->id == Control::TypeList || box->id == Control::CategoryList)) {
                const bool type = box->id == Control::TypeList;
                OpenList(type, type != listIsType_);
            } else if (!Contains(categoryListRect_, x, y)) categoryOpen_ = false;
            Invalidate();
            return;
        }
        if (searchEdit_ != nullptr && Contains(searchRect_, x, y) && tutorialStep_ < 0) {
            FocusSearch();
            Invalidate();
            return;
        }
        if (const ControlBox* box = ControlAt(x, y); box != nullptr) {
            if (box->enabled && box->id != Control::None) Activate(*box);
            return;
        }
        if (tutorialStep_ >= 0) return;
        // The map waits under the ground editor's card.
        if (!editName_.empty() && Contains(CanvasRect(), x, y)) return;
        std::uint32_t column = 0U;
        std::uint32_t row = 0U;
        if (SheetCellAt(x, y, column, row)) {
            sheetPick_ = RECT{ static_cast<LONG>(column), static_cast<LONG>(row), static_cast<LONG>(column), static_cast<LONG>(row) };
            sheetPickValid_ = true;
            sheetColumn0_ = column;
            sheetRow0_ = row;
            sheetSelecting_ = true;
            Capture();
            Invalidate();
            return;
        }
        int tileX = 0;
        int tileY = 0;
        if (!TileAt(x, y, tileX, tileY)) return;
        if (Alt() && !Ctrl()) {
            BeginPan(x, y);
            return;
        }
        if (tileX < 0 || tileY < 0 || tileX >= static_cast<int>(map_.width) || tileY >= static_cast<int>(map_.height)) return;
        const auto* layer = std::as_const(context_.Scene()).Components().Tilemaps().TryGet(entity_);
        if (((layer && layer->locked) || context_.HasPlayModeSceneSession()) &&
            tool_ != Tool::Picker && !(Ctrl() && Alt())) {
            SetMessage(std::string{ context_.HasPlayModeSceneSession()
                ? EditorText("map2d.stopToPaint", "Stop Play to edit the map.")
                : EditorText("map2d.layerLockedHint", "This layer is locked. Unlock it in the Inspector to paint.") }, true);
            Invalidate();
            return;
        }
        hoverX_ = tileX;
        hoverY_ = tileY;
        if (pasting_) {
            BeginChange();
            kb::world2d::Map2DEditor editor = Editor();
            PasteAt(editor, clipboard_, tileX, tileY);
            BorderAround(editor, tileX, tileY, tileX + static_cast<int>(clipboard_.width) - 1, tileY + static_cast<int>(clipboard_.height) - 1);
            EndChange();
            selection_ = editor.Clamp(tileX, tileY, tileX + static_cast<int>(clipboard_.width) - 1, tileY + static_cast<int>(clipboard_.height) - 1);
            hasSelection_ = true;
            pasting_ = Shift();
            Invalidate();
            return;
        }
        if (Ctrl() && Alt()) {
            Pick(tileX, tileY);
            Invalidate();
            return;
        }
        if (tool_ == Tool::Select) {
            dragStartX_ = tileX;
            dragStartY_ = tileY;
            if (InSelection(tileX, tileY) && !Shift()) {
                moving_ = true;
            } else {
                selecting_ = true;
                hasSelection_ = false;
            }
            Capture();
            Invalidate();
            return;
        }
        if (tool_ == Tool::Fill || (Ctrl() && fillKey_)) {
            FloodFill(tileX, tileY);
            Invalidate();
            return;
        }
        if (tool_ == Tool::Picker) {
            Pick(tileX, tileY);
            Invalidate();
            return;
        }
        strokeErases_ = tool_ == Tool::Eraser || Ctrl();
        BeginChange();
        dragStartX_ = tileX;
        dragStartY_ = tileY;
        const PaletteEntry* entry = Selected();
        if ((tool_ == Tool::Rectangle || Shift()) && (entry == nullptr || entry->kind != EntryKind::Object || strokeErases_ || tool_ == Tool::Collision)) {
            rectActive_ = true;
        } else {
            strokeActive_ = true;
            lastX_ = tileX;
            lastY_ = tileY;
            lastObjectX_ = tileX;
            lastObjectY_ = tileY;
            ApplyAt(tileX, tileY);
        }
        Capture();
        Invalidate();
    }

    void OnLeftUp(int x, int y) {
        if (panning_) {
            OnMiddleUp(x, y);
            return;
        }
        if (sheetSelecting_) {
            sheetSelecting_ = false;
            Release();
            return;
        }
        if (selecting_ || moving_) {
            Release();
            FinishSelectionDrag();
            return;
        }
        if (strokeActive_ || rectActive_) {
            Release();
            if (strokeActive_ || rectActive_) FinishStroke();
        }
    }

    void OnRightDown(int x, int y) {
        Layout();
        if (form_.IsOpen() || animationEditing_) return;
        if (polygonEditing_) { FinishPolygon(); return; }
        if (tutorialStep_ >= 0) return;
        if (const ControlBox* box = ControlAt(x, y); box != nullptr) {
            if (box->id == Control::BoardCell) ClearBoardCell(box->param);
            Invalidate();
            return;
        }
        if (!editName_.empty()) return;
        int tileX = 0;
        int tileY = 0;
        if (!TileAt(x, y, tileX, tileY) || tileX < 0 || tileY < 0 || tileX >= static_cast<int>(map_.width) || tileY >= static_cast<int>(map_.height)) return;
        if (pasting_) {
            pasting_ = false;
            Invalidate();
            return;
        }
        ShowContextMenu(x, y, tileX, tileY);
        Invalidate();
    }

    // A double click on a palette tile opens it: a ground's board (a floor tile becomes a ground first)
    // or an object's sheet.
    void OnDoubleClick(int x, int y) {
        Layout();
        if (form_.IsOpen() || animationEditing_) { OnLeftDown(x,y); return; }
        const ControlBox* box = ControlAt(x, y);
        const Control id = box != nullptr ? box->id : Control::None;
        if (id == Control::BoardCell) {
            boardCell_ = box->param;
            Invalidate();
            return;
        }
        // On the board the first click already put the sprite in.
        if (!editName_.empty() && id == Control::PaletteTile) return;
        OnLeftDown(x, y);
        if (id != Control::PaletteTile || !editName_.empty()) return;
        const PaletteEntry* entry = Selected();
        if (entry == nullptr) return;
        if (entry->kind == EntryKind::Ground) OpenBoard(entry->name);
        else if (entry->kind == EntryKind::Object) OpenSheetEditor();
        else MakeGround();
        Invalidate();
    }

    void OnMiddleDown(int x, int y) {
        if (form_.IsOpen() || animationEditing_) return;
        if (!Contains(CanvasRect(), x, y) || tutorialStep_ >= 0) return;
        if (Ctrl()) {
            SetZoom(kNormalZoom, POINT{ x, y });
            Invalidate();
            return;
        }
        BeginPan(x, y);
    }

    void OnMiddleUp(int x, int y) {
        if (!panning_) return;
        panning_ = false;
        Release();
        // A middle click that did not drag brings the clicked point to the middle.
        if (!panMoved_) {
            const POINT center = CanvasCenter();
            panX_ += x - center.x;
            panY_ += y - center.y;
        }
        Invalidate();
    }

    void OnMouseMove(int x, int y) {
        if (!trackingLeave_ && window_ != nullptr) {
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, window_, 0U };
            trackingLeave_ = TrackMouseEvent(&track) != FALSE;
        }
        if (panning_) {
            panMoved_ = panMoved_ || std::abs(x - panStart_.x) > 3 || std::abs(y - panStart_.y) > 3;
            panX_ = panStartX_ - (x - panStart_.x);
            panY_ = panStartY_ - (y - panStart_.y);
            Invalidate();
            return;
        }
        if (sheetSelecting_) {
            std::uint32_t column = 0U;
            std::uint32_t row = 0U;
            const int clampedX = std::clamp(x, static_cast<int>(sheet_.rect.left), static_cast<int>(sheet_.rect.right) - 1);
            const int clampedY = std::clamp(y, static_cast<int>(sheet_.rect.top), static_cast<int>(sheet_.rect.bottom) - 1);
            if (SheetCellAt(clampedX, clampedY, column, row)) {
                sheetPick_ = RECT{ static_cast<LONG>(std::min(column, sheetColumn0_)), static_cast<LONG>(std::min(row, sheetRow0_)),
                    static_cast<LONG>(std::max(column, sheetColumn0_)), static_cast<LONG>(std::max(row, sheetRow0_)) };
            }
            Invalidate();
            return;
        }
        const ControlBox* box = ControlAt(x, y);
        const Control hot = box != nullptr ? box->id : Control::None;
        const int hotParam = box != nullptr ? box->param : 0;
        int tileX = -1;
        int tileY = -1;
        if (!TileAt(x, y, tileX, tileY)) {
            tileX = -1;
            tileY = -1;
        }
        if (hot == Control::CategoryItem) categoryKey_ = hotParam;
        if (hot == hot_ && hotParam == hotParam_ && tileX == hoverX_ && tileY == hoverY_) return;
        hot_ = hot;
        hotParam_ = hotParam;
        if (strokeActive_ && tileX >= 0 && (tileX != lastX_ || tileY != lastY_)) {
            ApplyLine(lastX_, lastY_, tileX, tileY);
            lastX_ = tileX;
            lastY_ = tileY;
        }
        if (tileX >= 0 && (strokeActive_ || rectActive_ || selecting_ || moving_)) {
            // A drag keeps to the map even when the mouse leaves it.
            tileX = std::clamp(tileX, 0, static_cast<int>(map_.width) - 1);
            tileY = std::clamp(tileY, 0, static_cast<int>(map_.height) - 1);
        }
        hoverX_ = tileX;
        hoverY_ = tileY;
        Invalidate();
    }

    void OnWheel(int x, int y, int delta) {
        if (form_.IsOpen()) return;
        if (animationEditing_) { animationPage_ = std::max(0,animationPage_+(delta>0 ? -1 : 1)); Invalidate(); return; }
        if (tutorialStep_ >= 0) return;
        const int steps = delta > 0 ? 1 : -1;
        if (categoryOpen_) {
            if (Contains(categoryListRect_, x, y)) categoryFirst_ -= steps * 3;
            Invalidate();
            return;
        }
        if (Contains(PaletteRect(), x, y)) {
            if (panel_ == Panel::Palette && Contains(gridRect_, x, y)) paletteScroll_ -= steps * (kTileSize + kTileGap) * 2;
            else sheetScroll_ -= steps * (sheet_.cellPixels * 2);
            Invalidate();
            return;
        }
        if (!Contains(CanvasRect(), x, y)) return;
        if (Ctrl()) {
            ChangeFloor(steps);
        } else if (Alt()) {
            ChangeBrushSize(-steps);
        } else {
            Zoom(steps, POINT{ x, y });
        }
        Invalidate();
    }

    void ScrollTiles(int dx, int dy) {
        const int step = (Ctrl() ? 10 : 3) * zoom_;
        panX_ += dx * step;
        panY_ += dy * step;
    }

    [[nodiscard]] bool OnKey(WPARAM key) {
        if (form_.IsOpen()) { form_.Key(key,Shift()); Invalidate(); return true; }
        if (animationEditing_) {
            if (key == VK_ESCAPE) animationEditing_ = false;
            else if (key == VK_RETURN) ApplyAnimation();
            else if (key == VK_DELETE || key == VK_BACK) RemoveAnimationFrame();
            else if (key == VK_LEFT) MoveAnimationFrame(-1);
            else if (key == VK_RIGHT) MoveAnimationFrame(1);
            else if (key == VK_SPACE) animationPlaying_ = !animationPlaying_;
            Invalidate(); return true;
        }
        if (polygonEditing_) {
            if (key == VK_RETURN) FinishPolygon();
            else if (key == VK_ESCAPE) polygonEditing_ = false;
            else if (key == VK_BACK && !polygonPoints_.empty()) polygonPoints_.pop_back();
            Invalidate(); return true;
        }
        if (tutorialStep_ >= 0) {
            if (key == VK_RETURN || key == VK_RIGHT || key == VK_SPACE) {
                if (tutorialStep_ + 1 >= kTutorialSteps) FinishTutorial();
                else ++tutorialStep_;
            } else if (key == VK_LEFT || key == VK_BACK) {
                tutorialStep_ = std::max(0, tutorialStep_ - 1);
            } else if (key == VK_ESCAPE || key == VK_F1) {
                FinishTutorial();
            } else {
                return false;
            }
            Invalidate();
            return true;
        }
        if (categoryOpen_) {
            const int count = static_cast<int>(ListKeys().size());
            if (key == VK_DOWN || key == VK_UP) {
                categoryKey_ = std::clamp(categoryKey_ + (key == VK_DOWN ? 1 : -1), 0, count - 1);
                if (categoryKey_ < categoryFirst_) categoryFirst_ = categoryKey_;
                if (categoryKey_ >= categoryFirst_ + kListRows) categoryFirst_ = categoryKey_ - kListRows + 1;
            } else if (key == VK_RETURN || key == VK_SPACE) {
                PickCategory(categoryKey_);
            } else if (key == VK_ESCAPE) {
                categoryOpen_ = false;
            } else {
                return false;
            }
            Invalidate();
            return true;
        }
        const bool control = Ctrl();
        const bool shift = Shift();
        if (key == 'D') fillKey_ = true;
        if (control && key == 'S') {
            static_cast<void>(Save());
        } else if (control && (key == 'Y' || (key == 'Z' && shift))) {
            Redo();
        } else if (control && key == 'Z') {
            Undo();
        } else if (control && key == 'C') {
            CopyToClipboard(false);
        } else if (control && key == 'X') {
            CopyToClipboard(true);
        } else if (control && key == 'V') {
            if (!clipboard_.tiles.empty()) pasting_ = true;
        } else if (control && key == 'B') {
            Borderize(false);
        } else if (control && key == 'G') {
            GoToPosition();
        } else if (control && key == 'F') {
            panel_ = Panel::Palette;
            if (searchEdit_ != nullptr) {
                FocusSearch();
                SendMessageW(searchEdit_, EM_SETSEL, 0, -1);
            }
        } else if (control && key == 'W') {
            allFloors_ = !allFloors_;
        } else if (control && key == 'L') {
            ghostHigher_ = !ghostHigher_;
        } else if (control && (key == VK_OEM_PLUS || key == VK_ADD)) {
            Zoom(1, CanvasCenter());
        } else if (control && (key == VK_OEM_MINUS || key == VK_SUBTRACT)) {
            Zoom(-1, CanvasCenter());
        } else if (control && key == '0') {
            SetZoom(kNormalZoom, CanvasCenter());
        } else if (control && key >= '1' && key <= '9') {
            hotkeys_[key - '0'] = selectedKey_;
            SetMessage(EditorLocalization::Format("map2d.hotkeySaved", "Brush saved on key {key}.", { { "key", std::to_string(key - '0') } }), false);
        } else if (!control && key >= '0' && key <= '9') {
            if (!hotkeys_[key - '0'].empty()) {
                UseEntry(hotkeys_[key - '0']);
                RevealSelected();
            } else {
                SetMessage(EditorLocalization::Format("map2d.hotkeyEmpty", "No brush on key {key} (Ctrl+{key} saves one).", { { "key", std::to_string(key - '0') } }), false);
            }
        } else if (shift && key == 'G') {
            grid_ = !grid_;
        } else if (shift && key == 'I') {
            clientBox_ = !clientBox_;
        } else if (!control && key == 'C') {
            tool_ = tool_ == Tool::Collision ? Tool::Draw : Tool::Collision;
            SetMessage(std::string{ EditorText("map2d.collisionHelp", "Paint where movement is blocked. Ctrl + paint clears a block. Graphics stay unchanged.") }, false);
        } else if (key == 'A') {
            automagic_ = !automagic_;
            SetMessage(std::string{ automagic_ ? EditorText("map2d.automagicOn", "Auto edges on: grounds draw their edges.")
                                               : EditorText("map2d.automagicOff", "Auto edges off: edges stay as they are (Ctrl+B redoes them).") }, false);
        } else if (key == 'Q') {
            shade_ = !shade_;
        } else if (key == 'B') {
            const std::string previous = previousKey_;
            UseEntry(previous);
            RevealSelected();
        } else if (key == 'Z' || key == 'X') {
            NextEntry(key == 'X' ? 1 : -1);
        } else if (key == VK_SPACE) {
            tool_ = tool_ == Tool::Select ? Tool::Draw : Tool::Select;
            pasting_ = false;
        } else if (key == VK_DELETE) {
            DeleteSelection();
        } else if (key == VK_OEM_4 || key == VK_OEM_PLUS) {
            ChangeBrushSize(1);
        } else if (key == VK_OEM_6 || key == VK_OEM_MINUS) {
            ChangeBrushSize(-1);
        } else if (key == VK_PRIOR || key == VK_ADD) {
            ChangeFloor(1);
        } else if (key == VK_NEXT || key == VK_SUBTRACT) {
            ChangeFloor(-1);
        } else if (key == VK_MULTIPLY) {
            Zoom(1, CanvasCenter());
        } else if (key == VK_DIVIDE) {
            Zoom(-1, CanvasCenter());
        } else if (key == VK_LEFT) {
            ScrollTiles(-1, 0);
        } else if (key == VK_RIGHT) {
            ScrollTiles(1, 0);
        } else if (key == VK_UP) {
            ScrollTiles(0, -1);
        } else if (key == VK_DOWN) {
            ScrollTiles(0, 1);
        } else if (key == VK_F5) {
            Reload();
        } else if (key == VK_F1) {
            tutorialStep_ = 0;
        } else if (key == VK_ESCAPE) {
            if (rectActive_) {
                rectActive_ = false;
                Release();
                stroke_.reset();
            }
            if (!editName_.empty()) CloseBoard();
            else if (panel_ != Panel::Palette) panel_ = Panel::Palette;
            pasting_ = false;
            hasSelection_ = false;
        } else if (key == 'D') {
            SetMessage(std::string{ EditorText("map2d.fillHint", "Hold D and Ctrl+click to fill an area.") }, false);
        } else {
            return false;
        }
        Invalidate();
        return true;
    }

    void OnKeyUp(WPARAM key) {
        if (key == 'D') fillKey_ = false;
    }

    // Remere's Z / X: the previous or next tile of the palette.
    void NextEntry(int step) {
        const std::vector<const PaletteEntry*> visible = VisibleEntries();
        if (visible.empty()) return;
        const auto current = std::ranges::find_if(visible, [this](const PaletteEntry* entry) { return entry->key == selectedKey_; });
        const int index = current == visible.end() ? 0 : static_cast<int>(current - visible.begin());
        const int count = static_cast<int>(visible.size());
        UseEntry(visible[static_cast<std::size_t>((index + step + count) % count)]->key);
    }

    EditorSceneContext& context_;
    kb::scene::SceneEntity entity_;
    kb::assets::AssetId assetId_;
    std::filesystem::path file_;
    std::shared_ptr<MapWorkingCopy> document_;
    Map2DAsset& map_;
    const Map2DAsset* drawingLayer_ = nullptr;
    std::map<std::uint64_t, std::pair<std::uint64_t, std::optional<Map2DAsset>>> layerCache_;
    std::function<void()> present_;
    std::filesystem::path assetsRoot_;
    HWND window_ = nullptr;
    HWND owner_ = nullptr;
    HWND searchEdit_ = nullptr;
    RECT categoryRect_{};
    RECT categoryListRect_{};
    RECT typeRect_{};
    bool listIsType_ = false;
    std::string paletteType_{ "all" };
    bool categoryOpen_ = false;
    int categoryFirst_ = 0;
    int categoryKey_ = -1;
    bool searchFocused_ = false;
    HBRUSH editBrush_ = nullptr;
    bool closed_ = false;
    RECT client_{ 0, 0, 1440, 880 };
    Surface surface_;
    HFONT font_ = nullptr;
    HFONT boldFont_ = nullptr;
    HFONT smallFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    bool driven_ = false;
    std::string automationMapSnapshot_;
    double previewSeconds_ = 0.0;
    std::uint8_t drivenModifiers_ = 0U;

    kb::world2d::Map2DBrushLibrary library_;
    std::optional<kb::world2d::Map2DChangeSet> stroke_;
    bool strokeWasDirty_ = false;
    bool& dirty_ = document_->dirty;
    bool publishPending_ = false;
    std::string message_;
    bool messageIsError_ = false;

    std::vector<std::string> textures_;
    std::map<std::string, SheetImage> images_;
    std::vector<PaletteEntry> entries_;
    std::vector<std::string> categories_;
    std::string category_{ kAllCategory };
    std::wstring search_;
    std::string selectedKey_;
    std::string previousKey_;
    std::vector<std::string> recent_;
    std::array<std::string, 10U> hotkeys_{};
    int paletteScroll_ = 0;

    Panel panel_ = Panel::Palette;
    std::string editName_;
    std::string sheetTexture_;
    RECT sheetPick_{};
    bool sheetPickValid_ = false;
    int boardCell_ = -1;
    RECT boardCard_{};
    bool boardCompact_ = false;
    int boardCellPixels_ = kBoardCell;
    int boardLeft_ = 0;
    int boardTop_ = 0;
    int boardLayerTop_ = 0;

    Tool tool_ = Tool::Draw;
    kb::assets::Map2DCollision collisionBrush_{ .kind = kb::assets::Map2DCollisionKind::Solid };
    bool polygonEditing_ = false;
    EditorMap2DForm form_;
    bool animationEditing_ = false, animationPlaying_ = true;
    Map2DSprite animationSprite_{};
    std::vector<std::uint32_t> animationFrames_;
    float animationSpeed_ = 8;
    float animationPhase_ = 0;
    int animationSelected_ = -1, animationPage_ = 0, animationTimelineFirst_ = 0;
    RECT animationAtlas_{}, animationTimeline_{}, animationPreview_{};
    std::optional<std::pair<int,int>> sceneHover_;
    bool paintInScene_ = false;
    bool sceneStroke_ = false;
    std::vector<std::array<float, 2>> polygonPoints_;
    int brushSize_ = 1;
    unsigned density_ = 100U, scatterSeed_ = 0U;
    bool scatterApplying_ = false;
    std::set<std::uint64_t> scatterVisited_;
    std::int32_t floor_ = 0;
    bool automagic_ = true;
    bool grid_ = true;
    bool clientBox_ = false;
    bool allFloors_ = true;
    bool shade_ = true;
    bool ghostHigher_ = false;
    bool live_ = true;
    bool fillKey_ = false;

    int zoom_ = kNormalZoom;
    double panX_ = 0.0;
    double panY_ = 0.0;
    int hoverX_ = -1;
    int hoverY_ = -1;
    bool strokeActive_ = false;
    bool strokeErases_ = false;
    int lastX_ = 0;
    int lastY_ = 0;
    int lastObjectX_ = 0;
    int lastObjectY_ = 0;
    bool rectActive_ = false;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    bool panning_ = false;
    bool panMoved_ = false;
    POINT panStart_{};
    double panStartX_ = 0.0;
    double panStartY_ = 0.0;
    bool sheetSelecting_ = false;
    std::uint32_t sheetColumn0_ = 0U;
    std::uint32_t sheetRow0_ = 0U;
    int sheetScroll_ = 0;
    bool trackingLeave_ = false;

    bool selecting_ = false;
    bool moving_ = false;
    bool hasSelection_ = false;
    Map2DRect selection_{};
    Clipboard clipboard_{};
    bool pasting_ = false;

    int tutorialStep_ = -1;
    RECT tutorialCard_{};

    std::vector<ControlBox> controls_;
    SheetLayout sheet_{};
    RECT gridRect_{};
    RECT searchRect_{};
    int infoTop_ = 0;
    int guideTop_ = 0;
    int sizeLabelTop_ = 0;
    Control hot_ = Control::None;
    int hotParam_ = 0;
};

[[nodiscard]] std::vector<std::unique_ptr<MapEditorWindow>>& Windows() {
    static std::vector<std::unique_ptr<MapEditorWindow>> windows;
    return windows;
}

void ForgetClosed() {
    std::erase_if(Windows(), [](const std::unique_ptr<MapEditorWindow>& window) { return window->Closed(); });
}

[[nodiscard]] MapEditorWindow* OpenWindowFor(kb::assets::AssetId assetId) {
    ForgetClosed();
    for (const std::unique_ptr<MapEditorWindow>& window : Windows()) {
        if (window->AssetId() == assetId) return window.get();
    }
    return nullptr;
}

bool VerifyNativeMap2D(EditorSceneContext& context, kb::scene::SceneEntity entity, kb::assets::AssetId asset, std::string* error) {
    HWND host = CreateWindowExW(0,L"STATIC",L"Map interaction test",WS_POPUP|WS_CLIPCHILDREN,0,0,1200,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if (!host) return false;
    const auto selected = context.SelectedEntity();
    context.SelectEntity(entity);
    const auto originalTransform = context.Scene().Transforms().Get(entity);
    auto document = AcquireWorkingCopy(context,entity,asset,{});
    const auto notify = document->notify;
    const auto baseline = kb::assets::Map2DAssetIO::Write(document->map);
    EditorDockModel docks; EditorFloatingWindowManager floating; EditorMetrics metrics;
    auto panel = EditorPanelContentResolver::ResolvePanel(DockPanelKind::Scene,host,host,docks,floating,metrics);
    if (!panel || !EditorMap2DWindow::Open(host,context,entity,{})) { DestroyWindow(host); return false; }
    auto* native = OpenWindowFor(asset);
    native->Dock(host,{0,0,700,650});
    auto& camera = context.ViewportCamera(panel->panelId);
    const auto oldCamera = camera;
    const auto check = [&]() -> bool {
        const auto drive = [&](const std::string& line) { return native->DriveLine(line,error); };
        if (!drive("key escape") || !drive("select 12 5 12 5") || !drive("settings opacity")) return false;
        if (!native->CaptureTo(EditorProjectPaths::AssetsRoot().parent_path()/"Saved"/"native-map-form.png",error)) return false;
        HWND input = nullptr;
        for (HWND child = GetWindow(GetWindow(host,GW_CHILD),GW_CHILD); child; child=GetWindow(child,GW_HWNDNEXT)) {
            wchar_t name[32]{}; GetClassNameW(child,name,32);
            if (_wcsicmp(name,L"EDIT") == 0 && (GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)) { input=child; break; }
        }
        if (!input) { if (error) *error="numeric form has no native edit control"; return false; }
        SendMessageW(input,EM_SETSEL,0,-1);
        SendMessageW(input,WM_CHAR,'6',0); SendMessageW(input,WM_CHAR,'5',0); SendMessageW(input,WM_KEYDOWN,VK_RETURN,0);
        const auto tile = document->map.tiles.find(Map2DTileKey(12,5,0));
        if (tile == document->map.tiles.end()) return false;
        const auto sprite = tile->second.items.empty() ? tile->second.ground : tile->second.items.back();
        if ((sprite.tint&255U)!=166U || !drive("key z ctrl") || kb::assets::Map2DAssetIO::Write(document->map)!=baseline) {
            if (error) *error="native edit/Enter/Undo did not round-trip opacity"; return false;
        }
        if (!drive("pick g:Grass") || !drive("scene-paint")) return false;
        for (const auto view : {"flat","wall"}) {
            const auto oldView = document->map.orientation;
            if (!drive(std::string("view ")+view)) return false;
            const auto before = kb::assets::Map2DAssetIO::Write(document->map);
            auto transform = originalTransform;
            transform.localPosition = {3,4,5}; transform.localScale={2,1.5F,0.75F};
            const bool wall = document->map.orientation == kb::assets::Map2DOrientation::Wall;
            transform.localRotation = wall ? kb::scene::Quat{0,0,0.19509032F,0.98078528F} : kb::scene::Quat{0,0.19509032F,0,0.98078528F};
            context.Scene().Transforms().Set(entity,transform); context.Scene().Runtime().SynchronizeTransforms();
            const auto world = context.Scene().Transforms().Get(entity);
            const float x = (24.5F-static_cast<float>(document->map.width)*0.5F)*document->map.tileSize;
            const float y = (static_cast<float>(document->map.height)-8.5F)*document->map.tileSize;
            const float z = (static_cast<float>(document->map.height)*0.5F-8.5F)*document->map.tileSize;
            const kb::scene::Vec3 local = wall ? kb::scene::Vec3{x,y,0} : kb::scene::Vec3{x,0,z};
            const auto target = context.Scene().Transforms().WorldTranslation(entity)+kb::math::Rotate(world.worldRotation,
                kb::scene::Vec3{local.x*world.worldScale.x,local.y*world.worldScale.y,local.z*world.worldScale.z});
            camera.SetViewAngles(0,wall ? 0.0F : -89.0F); camera.FocusOn(target,4,0);
            const auto area = SceneViewportToolbarRenderer::Resolve(panel->content,context.ViewportPreview(panel->panelId),context).renderArea;
            const int px=(area.left+area.right)/2, py=(area.top+area.bottom)/2;
            if (!EditorMap2DWindow::PaintScenePointer(host,host,px,py,docks,floating,metrics,context,true)) {
                if (error) *error="scene pointer did not reach the transformed map plane"; return false;
            }
            EditorMap2DWindow::FinishSceneStroke(context);
            const auto painted = document->map.tiles.find(Map2DTileKey(24,8,0));
            if (painted==document->map.tiles.end() || !painted->second.ground.IsValid()) return false;
            if (!drive("key z ctrl") || kb::assets::Map2DAssetIO::Write(document->map)!=before) return false;
            context.Scene().Transforms().Set(entity,originalTransform); context.Scene().Runtime().SynchronizeTransforms();
            if (document->map.orientation!=oldView && !drive("key z ctrl")) return false;
        }
        return kb::assets::Map2DAssetIO::Write(document->map)==baseline;
    };
    const bool result = check();
    camera = oldCamera;
    context.Scene().Transforms().Set(entity,originalTransform); context.Scene().Runtime().SynchronizeTransforms();
    native->Close(); ForgetClosed(); DestroyWindow(host);
    document->notify = notify; context.SelectEntity(selected);
    if (!result && error && error->empty()) *error="native map interaction changed unexpected cells or failed Undo";
    return result;
}

[[nodiscard]] std::unique_ptr<MapEditorWindow> MakeWindow(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    std::function<void()> present,
    std::string* error) {
    const kb::assets::AssetMetadata* metadata = EditorMap2DService::MapMetadata(sceneContext.Scene(), entity);
    if (metadata == nullptr) {
        if (error != nullptr) *error = "The entity has no World 2D Map";
        return nullptr;
    }
    std::optional<Map2DAsset> map = kb::assets::Map2DAssetIO::Load(metadata->physicalPath, error);
    if (!map.has_value()) return nullptr;
    return std::make_unique<MapEditorWindow>(sceneContext, entity, metadata->id, metadata->physicalPath, std::move(*map), std::move(present));
}

} // namespace

bool EditorMap2DWindow::Open(
    HWND owner,
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    std::function<void()> requestScenePresent) {
    const kb::assets::AssetMetadata* metadata = EditorMap2DService::MapMetadata(sceneContext.Scene(), entity);
    if (metadata == nullptr) {
        sceneContext.Console().Warning("World 2D", "The selected entity has no World 2D Map.");
        return false;
    }
    if (MapEditorWindow* open = OpenWindowFor(metadata->id); open != nullptr) {
        open->BindEntity(entity);
        open->BringToFront();
        return true;
    }
    std::string error;
    std::unique_ptr<MapEditorWindow> window = MakeWindow(sceneContext, entity, std::move(requestScenePresent), &error);
    if (window == nullptr || !window->Create(owner)) {
        sceneContext.Console().Warning("World 2D", error.empty() ? "The Map Editor window could not be opened." : error);
        return false;
    }
    Windows().push_back(std::move(window));
    return true;
}

void EditorMap2DWindow::SyncDocked(HWND parent, const RECT& bounds, EditorSceneContext& sceneContext) {
    ForgetClosed();
    const auto entity = sceneContext.SelectedEntity();
    const auto* metadata = EditorMap2DService::MapMetadata(sceneContext.Scene(), entity);
    MapEditorWindow* active = metadata ? OpenWindowFor(metadata->id) : nullptr;
    if (active) active->BindEntity(entity);
    if (metadata && !active) {
        const auto id = metadata->id;
        if (Open(parent, sceneContext, entity, [&sceneContext, parent]() {
                sceneContext.MarkSceneRenderDirty(); InvalidateRect(parent, nullptr, FALSE);
            })) active = OpenWindowFor(id);
    }
    for (const auto& window : Windows()) {
        if (!window->BelongsTo(sceneContext)) continue;
        if (window.get() == active) window->Dock(parent, bounds);
        else window->Hide(parent);
    }
}

void EditorMap2DWindow::HideDocked(HWND parent) {
    for (const auto& window : Windows()) window->Hide(parent);
}

bool EditorMap2DWindow::ScenePainting(const EditorSceneContext& context) {
    const auto* metadata = EditorMap2DService::MapMetadata(context.Scene(), context.SelectedEntity());
    const auto* window = metadata ? OpenWindowFor(metadata->id) : nullptr;
    return window && window->ScenePainting() && !context.HasPlayModeSceneSession();
}
std::vector<kb::scene::PhysicsDebugLineDesc> EditorMap2DWindow::SceneBrushLines(const EditorSceneContext& context, const kb::math::DVec3& origin) {
    const auto* metadata = EditorMap2DService::MapMetadata(context.Scene(), context.SelectedEntity());
    const auto* window = metadata ? OpenWindowFor(metadata->id) : nullptr;
    return window ? window->SceneBrushLines(origin) : std::vector<kb::scene::PhysicsDebugLineDesc>{};
}
bool EditorMap2DWindow::SceneStrokeActive(EditorSceneContext& context) {
    for (const auto& window : Windows()) if (window->BelongsTo(context) && window->SceneStroke()) return true;
    return false;
}
void EditorMap2DWindow::FinishSceneStroke(EditorSceneContext& context) {
    for (const auto& window : Windows()) if (window->BelongsTo(context)) window->FinishSceneStroke();
}
bool EditorMap2DWindow::PaintScenePointer(HWND source, HWND main, int x, int y, const EditorDockModel& docks,
    const EditorFloatingWindowManager& floating, const EditorMetrics& metrics, EditorSceneContext& context, bool begin) {
    const auto entity = context.SelectedEntity();
    const auto* metadata = EditorMap2DService::MapMetadata(context.Scene(), entity);
    if (!metadata) return false;
    auto* window = OpenWindowFor(metadata->id);
    if (!window || !window->BelongsTo(context) || !window->ScenePainting()) return false;
    window->BindEntity(entity);
    window->ClearSceneHover();
    const auto hit = EditorSceneViewportHitResolver::ResolveRay(source, main, x, y, docks, floating, metrics, context);
    if (!hit) return false;
    auto transform = context.Scene().Transforms().Get(entity);
    transform.worldPosition = EditorSceneViewportMath::ViewportPosition(context.Scene(), entity, transform, hit->origin);
    if (std::abs(transform.worldScale.x) < 0.00001F || std::abs(transform.worldScale.y) < 0.00001F || std::abs(transform.worldScale.z) < 0.00001F) return false;
    const auto local = [&](kb::scene::Vec3 point) {
        const auto v = kb::math::Rotate(kb::math::Inverse(transform.worldRotation), point - transform.worldPosition);
        return kb::scene::Vec3{v.x / transform.worldScale.x, v.y / transform.worldScale.y, v.z / transform.worldScale.z};
    };
    const auto origin = local(hit->ray.origin), direction = local(hit->ray.origin + hit->ray.direction) - origin;
    const auto& map = window->Map();
    const bool wall = map.orientation == kb::assets::Map2DOrientation::Wall;
    const float divisor = wall ? direction.z : direction.y;
    if (std::abs(divisor) < 0.00001F) return false;
    const float plane = static_cast<float>(window->Floor()) * map.floorHeight * (wall ? -1.0F : 1.0F);
    const float distance = (plane - (wall ? origin.z : origin.y)) / divisor;
    return distance >= 0 && window->PaintSceneLocal(origin + direction * distance, begin);
}

bool EditorMap2DWindow::SaveAll(EditorSceneContext& sceneContext) {
    for (const auto& window : Windows()) if (window->BelongsTo(sceneContext) && !window->Flush()) return false;
    // History can retain a working copy after its tab closes; it is still part of Ctrl+S.
    for (const auto& weak : WorkingCopies()) {
        const auto copy = weak.lock();
        if (!copy || copy->context != &sceneContext || !copy->dirty) continue;
        std::string error;
        if (!EditorMap2DService::SyncCollision(sceneContext.Scene(), copy->asset, copy->map, nullptr, &error) ||
            !EditorMap2DService::Publish(sceneContext.Scene(), copy->asset, copy->map, true, &error)) {
            sceneContext.Console().Warning("2D Map", error);
            return false;
        }
        copy->dirty = false;
        if (copy->notify) copy->notify();
    }
    return true;
}

bool EditorMap2DWindow::EditSettings(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    const std::function<void(Map2DAsset&)>& edit,
    std::string* error) {
    const kb::assets::AssetMetadata* metadata = EditorMap2DService::MapMetadata(sceneContext.Scene(), entity);
    if (metadata == nullptr) {
        if (error != nullptr) *error = "The entity has no World 2D Map";
        return false;
    }
    auto& scene = sceneContext.Scene();
    const auto* tilemap = std::as_const(scene).Components().Tilemaps().TryGet(entity);
    if ((tilemap && tilemap->locked) || sceneContext.HasPlayModeSceneSession()) {
        if (error) *error = "The layer is locked or Play is active";
        return false;
    }
    std::vector<kb::scene::SceneEntity> layers{ entity };
    const auto parent = scene.Hierarchy().Parent(entity);
    if (scene.Components().Grids().Has(parent) && !scene.Components().Tilemaps().Has(parent)) layers = scene.Hierarchy().ChildEntities(parent);
    std::vector<std::pair<std::shared_ptr<MapWorkingCopy>, Map2DAsset>> originals;
    auto batch = std::make_unique<MapBatchCommand>();
    const auto rollback = [&]() {
        for (auto& [copy, original] : originals) { copy->map = std::move(original); static_cast<void>(copy->Publish()); }
    };
    for (const auto layer : layers) {
        const auto* layerMetadata = EditorMap2DService::MapMetadata(scene, layer);
        if (!layerMetadata) continue;
        const auto loaded = CurrentMap(sceneContext, layer);
        if (!loaded) { rollback(); return false; }
        auto document = AcquireWorkingCopy(sceneContext, layer, layerMetadata->id, *loaded);
        if (std::ranges::any_of(originals, [&](const auto& item) { return item.first == document; })) continue;
        originals.emplace_back(document, document->map);
        kb::world2d::Map2DChangeSet change;
        change.RecordSettings(document->map);
        for (const auto& [key, tile] : document->map.tiles) { static_cast<void>(tile); change.Record(document->map, key); }
        edit(document->map);
        if (!kb::assets::IsMap2DAssetValid(document->map, error)) { rollback(); return false; }
        change.Finish(document->map);
        if (!change.Empty()) batch->edits.push_back(std::make_unique<MapStrokeCommand>(document, std::move(change)));
    }
    if (batch->edits.empty()) return true;
    if (!sceneContext.ExecuteSceneCommand(std::move(batch))) { rollback(); return false; }
    if (const auto map = CurrentMap(sceneContext, entity))
        static_cast<void>(kb::world2d::SetUpMap2DView(scene, entity, *map, EditorProjectPaths::AssetsRoot()));

    return true;
}

std::optional<Map2DAsset> EditorMap2DWindow::CurrentMap(const EditorSceneContext& sceneContext, kb::scene::SceneEntity entity) {
    const kb::assets::AssetMetadata* metadata = EditorMap2DService::MapMetadata(sceneContext.Scene(), entity);
    if (metadata == nullptr) return std::nullopt;
    for (const auto& weak : WorkingCopies()) {
        if (const auto copy = weak.lock(); copy && copy->context == &sceneContext && copy->asset == metadata->id) return copy->map;
    }
    if (const MapEditorWindow* open = OpenWindowFor(metadata->id); open != nullptr) return open->Map();
    return kb::assets::Map2DAssetIO::Load(metadata->physicalPath);
}

bool EditorMap2DWindow::CloseAll(HWND owner) {
    static_cast<void>(owner);
    ForgetClosed();
    for (const std::unique_ptr<MapEditorWindow>& window : Windows()) {
        if (!window->ConfirmClose()) return false;
    }
    for (const std::unique_ptr<MapEditorWindow>& window : Windows()) window->Close();
    Windows().clear();
    return true;
}

bool EditorMap2DWindow::Drive(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    const std::vector<std::string>& inputs,
    const std::filesystem::path& png,
    int width,
    int height,
    std::string* error) {
    // One editing session per map across a scenario's steps, as a user keeps one window open.
    static std::map<std::uint64_t, std::unique_ptr<MapEditorWindow>> sessions;
    const kb::assets::AssetMetadata* metadata = EditorMap2DService::MapMetadata(sceneContext.Scene(), entity);
    if (metadata == nullptr) {
        if (error != nullptr) *error = "The entity has no World 2D Map";
        return false;
    }
    std::unique_ptr<MapEditorWindow>& window = sessions[metadata->id.value];
    if (window == nullptr) {
        window = MakeWindow(sceneContext, entity, {}, error);
        if (window == nullptr) return false;
        window->SetClientSize(width > 0 ? width : 1400, height > 0 ? height : 860);
        // Lay the window out once, as its first paint does, so clicks find its controls.
        if (!window->CaptureTo(png, error)) return false;
    }
    window->BindEntity(entity);
    if (width > 0 && height > 0) window->SetClientSize(width, height);
    for (const std::string& input : inputs) {
        if (!window->DriveLine(input, error)) { if (error && error->empty()) *error = "Map editor input failed: " + input; return false; }
    }
    return window->CaptureTo(png, error);
}

bool EditorMap2DWindow::Capture(
    EditorSceneContext& sceneContext,
    kb::scene::SceneEntity entity,
    const std::filesystem::path& png,
    int width,
    int height,
    std::string* error) {
    std::unique_ptr<MapEditorWindow> window = MakeWindow(sceneContext, entity, {}, error);
    if (window == nullptr) return false;
    window->SetClientSize(width, height);
    return window->CaptureTo(png, error);
}

} // namespace kb::editor

#endif
