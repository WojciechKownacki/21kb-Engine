#include "platform/win32/EditorAssetImportDialog.hpp"
#include "localization/EditorLocalization.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetImportCatalog.hpp"
#include "engine/assets/AssetImportPreset.hpp"
#include "platform/win32/EditorFileDialogFilter.hpp"
#include "platform/win32/EditorModalMessageLoop.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/components/EditorDialogStyle.hpp"

#include <CommDlg.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <sstream>
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
// Scale and the three rotation angles, typed into the Transform section.
constexpr int kFieldCount = 4;
constexpr int kScaleField = 0;
// Where the Transform section and its rows sit in the scrolled content.
constexpr int kTransformTitleY = 374;
constexpr int kPresetRowY = 404;
constexpr int kScaleRowY = 454;
constexpr int kRotationRowY = 504;
constexpr int kNoteY = 566;
constexpr int kContentHeight = 620;

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept { return RECT{ left, top, right, bottom }; }
[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept { return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom; }
[[nodiscard]] int Height(const RECT& rect) noexcept { return std::max(0, static_cast<int>(rect.bottom - rect.top)); }
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

[[nodiscard]] std::string NumberText(float value) {
    std::ostringstream text;
    text << value;
    return text.str();
}

class MeshImportOptionsWindow {
public:
    MeshImportOptionsWindow(
        std::size_t fileCount,
        bool materialSlotsAvailable,
        bool skeletalImportAvailable,
        std::span<const EditorAssetImportPreset> presets)
        : fileCount_(fileCount)
        , materialSlotsAvailable_(materialSlotsAvailable)
        , skeletalImportAvailable_(skeletalImportAvailable)
        , importSkeletalMesh_(skeletalImportAvailable)
        , presets_(presets.begin(), presets.end()) {
        ShowTransform(kb::assets::AssetMeshImportTransform{});
    }

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
        if (exit != EditorModalLoopExit::Completed || !accepted_) return std::nullopt;
        return Options(*ParsedTransform());
    }

private:
    [[nodiscard]] kb::assets::AssetImportOptions Options(const kb::assets::AssetMeshImportTransform& transform) const {
        return kb::assets::AssetImportOptions{
            .mesh = {
                .importMaterialSlots = importMaterialSlots_,
                .importTextures = importTextures_,
                .importMaterials = importMaterials_,
                .combineMeshes = combineMeshes_,
                .importSkeletalMesh = importSkeletalMesh_,
                // The skeletal publication keeps the source's scale and orientation.
                .transform = importSkeletalMesh_ ? kb::assets::AssetMeshImportTransform{} : transform,
            },
        };
    }

    void ShowTransform(const kb::assets::AssetMeshImportTransform& transform) {
        fields_[kScaleField] = NumberText(transform.scale);
        for (int axis = 0; axis < 3; ++axis) fields_[axis + 1] = NumberText(transform.rotationDegrees[static_cast<std::size_t>(axis)]);
    }

    // The typed scale and rotation as the import takes them (kb::assets::AssetImportPresetIO's
    // own parser, as a preset line), or nullopt with the reason shown in the dialog.
    [[nodiscard]] std::optional<kb::assets::AssetMeshImportTransform> ParsedTransform() const {
        kb::assets::AssetImportOptions options{};
        if (!kb::assets::AssetImportPresetIO::SetField(options, "scale", fields_[kScaleField]) ||
            !kb::assets::AssetImportPresetIO::SetField(options, "rotation", fields_[1] + " " + fields_[2] + " " + fields_[3])) {
            return std::nullopt;
        }
        return options.mesh.transform;
    }

    void ApplyPreset() {
        if (presetIndex_ == 0U) return;
        const kb::assets::AssetImportOptions& options = presets_[presetIndex_ - 1U].options;
        importMaterialSlots_ = options.mesh.importMaterialSlots;
        importTextures_ = options.mesh.importTextures;
        importMaterials_ = options.mesh.importMaterials;
        combineMeshes_ = options.mesh.combineMeshes;
        importSkeletalMesh_ = skeletalImportAvailable_ && options.mesh.importSkeletalMesh;
        ShowTransform(options.mesh.transform);
    }

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
    [[nodiscard]] RECT RowAt(int offset) const noexcept { const RECT viewport = Viewport(); return Rect(viewport.left, viewport.top + offset - scrollOffset_, viewport.right - 12, viewport.top + offset - scrollOffset_ + kRowHeight); }
    [[nodiscard]] RECT MaterialSlotsRow() const noexcept { return RowAt(68); }
    [[nodiscard]] RECT TexturesRow() const noexcept { return RowAt(162); }
    [[nodiscard]] RECT MaterialsRow() const noexcept { return RowAt(212); }
    [[nodiscard]] RECT CombineMeshesRow() const noexcept { return RowAt(262); }
    [[nodiscard]] RECT SkeletonRow() const noexcept { return RowAt(312); }
    [[nodiscard]] RECT PresetRow() const noexcept { return RowAt(kPresetRowY); }
    [[nodiscard]] RECT FieldRect(int field) const noexcept {
        if (field == kScaleField) {
            const RECT row = RowAt(kScaleRowY);
            return Rect(row.right - 110, row.top + 8, row.right - 10, row.bottom - 8);
        }
        const RECT row = RowAt(kRotationRowY);
        const int right = row.right - 10 - (3 - field) * 110;
        return Rect(right - 100, row.top + 8, right, row.bottom - 8);
    }
    [[nodiscard]] bool TransformEnabled() const noexcept { return !importSkeletalMesh_; }
    [[nodiscard]] int MaxScroll() const noexcept { return std::max(0, kContentHeight - Height(Viewport())); }
    void Scroll(int delta) noexcept { scrollOffset_ = std::clamp(scrollOffset_ + delta, 0, MaxScroll()); InvalidateRect(window_, nullptr, FALSE); }

    [[nodiscard]] std::string PresetName() const {
        return presetIndex_ == 0U
            ? std::string{ EditorText("importMesh.presetNone", "None (the settings below)") }
            : presets_[presetIndex_ - 1U].name;
    }

    void Paint(HDC dc) const {
        const RECT client = Client();
        const EditorTheme theme = MakeEditorDarkTheme();
        EditorDialogStyle::PaintSurface(dc, client, theme);
        const std::string description = importSkeletalMesh_
            ? EditorLocalization::Plural("importMesh.skeletalFileCount", static_cast<std::int64_t>(fileCount_),
                  "Skeletal mesh  /  {count} file", "Skeletal mesh  /  {count} files")
            : EditorLocalization::Plural("importMesh.staticFileCount", static_cast<std::int64_t>(fileCount_),
                  "Static mesh  /  {count} file", "Static mesh  /  {count} files");
        EditorDialogStyle::PaintHeader(
            dc,
            theme,
            EditorDialogHeaderDescriptor{
                .bounds = Rect(1, 3, client.right - 1, kHeaderHeight),
                .closeButton = CloseRect(),
                .title = EditorText("importMesh.title", "Import Mesh"),
                .description = description,
                .icon = HeroIconKind::DocumentText,
                .showIcon = true,
            });
        const RECT viewport = Viewport();
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
        const int y = viewport.top - scrollOffset_;
        EditorDialogStyle::PaintText(dc, Rect(viewport.left, y, viewport.right, y + 24), EditorText("importMesh.meshSection", "Mesh"), EditorDialogStyle::Color(theme.accent), 12, FW_SEMIBOLD);
        EditorDialogStyle::PaintText(
            dc,
            Rect(viewport.left, y + 25, viewport.right, y + 48),
            importSkeletalMesh_ ? EditorText("importMesh.skeletalSourceImport", "Skeletal source import")
                                : EditorText("importMesh.staticMeshImport", "Static mesh import"),
            EditorDialogStyle::Color(theme.textSecondary));
        DrawCheckRow(
            dc,
            theme,
            MaterialSlotsRow(),
            importMaterialSlots_,
            EditorText("importMesh.importMaterialSlots", "Import material slots"),
            materialSlotsAvailable_
                ? EditorText("importMesh.importMaterialSlotsHint", "Preserve FBX material names and per-section assignments")
                : EditorText("importMesh.importMaterialSlotsUnavailable", "Available only when every selected mesh is an FBX source"),
            materialSlotsAvailable_ && !importSkeletalMesh_);
        EditorDialogStyle::PaintText(dc, Rect(viewport.left, y + 132, viewport.right, y + 156), EditorText("importMesh.pipelineSection", "Import pipeline"), EditorDialogStyle::Color(theme.accent), 12, FW_SEMIBOLD);
        DrawCheckRow(dc, theme, TexturesRow(), importTextures_, EditorText("importMesh.importTextures", "Import textures"),
            EditorText("importMesh.importTexturesHint", "Copies external images and extracts embedded FBX, glTF, or GLB image data"),
            skeletalImportAvailable_);
        DrawCheckRow(dc, theme, MaterialsRow(), importMaterials_, EditorText("importMesh.importMaterials", "Import materials"),
            EditorText("importMesh.importMaterialsHint", "Creates PBR Material assets and assigns them to imported mesh sections"),
            skeletalImportAvailable_);
        DrawCheckRow(dc, theme, CombineMeshesRow(), combineMeshes_, EditorText("importMesh.combineMeshes", "Combine meshes"),
            EditorText("importMesh.combineMeshesHint", "Merges all compatible mesh nodes bound to the imported skin into one asset"),
            skeletalImportAvailable_);
        DrawCheckRow(
            dc,
            theme,
            SkeletonRow(),
            importSkeletalMesh_,
            EditorText("importMesh.importSkeleton", "Import skeleton"),
            skeletalImportAvailable_
                ? EditorText("importMesh.importSkeletonHint", "Creates Skeleton, Skeletal Mesh and Animation Clips from FBX, glTF, or GLB")
                : EditorText("importMesh.importSkeletonUnavailable", "Requires every selected source to use one supported skeletal format: FBX, glTF, or GLB"),
            skeletalImportAvailable_);

        EditorDialogStyle::PaintText(dc, Rect(viewport.left, y + kTransformTitleY, viewport.right, y + kTransformTitleY + 24),
            EditorText("importMesh.transformSection", "Transform"), EditorDialogStyle::Color(theme.accent), 12, FW_SEMIBOLD);
        DrawValueRow(dc, theme, PresetRow(), EditorText("importMesh.preset", "Import preset"),
            presets_.empty() ? EditorText("importMesh.presetEmpty", "No import presets in the project (Create > New Import Preset)")
                             : EditorText("importMesh.presetHint", "Click to use the next preset of the project"),
            PresetName(), !presets_.empty());
        const RECT scaleRow = RowAt(kScaleRowY);
        DrawLabelRow(dc, theme, scaleRow, EditorText("importMesh.scaleFactor", "Scale factor"),
            EditorText("importMesh.scaleFactorHint", "Multiplies every vertex (0.01 for centimetres)"), TransformEnabled());
        const RECT rotationRow = RowAt(kRotationRowY);
        DrawLabelRow(dc, theme, rotationRow, EditorText("importMesh.rotation", "Rotation (X Y Z degrees)"),
            EditorText("importMesh.rotationHint", "Turns the mesh after the right- to left-handed conversion"), TransformEnabled());
        for (int field = 0; field < kFieldCount; ++field) {
            EditorDialogStyle::PaintField(dc, FieldRect(field), theme, fields_[static_cast<std::size_t>(field)],
                TransformEnabled() && focusedField_ == field);
        }
        const bool transformValid = ParsedTransform().has_value();
        EditorDialogStyle::PaintText(dc, Rect(viewport.left, y + kNoteY, viewport.right - 12, y + kNoteY + 42),
            !transformValid ? EditorText("importMesh.transformInvalid", "The scale must be a number above 0 and the rotation three numbers.")
            : importSkeletalMesh_
                ? EditorText("importMesh.skeletalPipelineNote", "The importer publishes textures, materials, rig, skinned mesh and clips as one deterministic import operation.")
                : EditorText("importMesh.staticPipelineNote", "Static mesh import uses the same texture and material asset pipeline."),
            EditorDialogStyle::Color(transformValid ? theme.textDisabled : theme.accent), 11, FW_NORMAL, DT_LEFT | DT_WORDBREAK);
        RestoreDC(dc, saved);

        if (MaxScroll() > 0) {
            const RECT track = Rect(viewport.right - 6, viewport.top, viewport.right - 2, viewport.bottom);
            const int thumb = std::max(30, Height(track) * Height(viewport) / std::max(1, Height(viewport) + MaxScroll()));
            const int top = track.top + (Height(track) - thumb) * scrollOffset_ / std::max(1, MaxScroll());
            EditorDialogStyle::PaintScrollbar(dc, track, Rect(track.left, top, track.right, top + thumb), theme);
        }
        const RECT footer = Rect(1, client.bottom - kFooterHeight, client.right - 1, client.bottom - 1);
        EditorDialogStyle::PaintFooter(dc, footer, theme);
        EditorDialogStyle::PaintButton(dc, Rect(client.right - 198, client.bottom - 27, client.right - 106, client.bottom - 3), theme, EditorText("common.cancel", "Cancel"));
        EditorDialogStyle::PaintButton(dc, Rect(client.right - 96, client.bottom - 27, client.right - 16, client.bottom - 3), theme,
            EditorText("importMesh.import", "Import"), EditorDialogButtonTone::Primary, false, transformValid);
    }

    static void DrawCheckRow(HDC dc, const EditorTheme& theme, const RECT& rect, bool checked, std::string_view title, std::string_view subtitle, bool enabled) {
        GdiDrawing::FillRectColor(dc, rect, EditorDialogStyle::Color(theme.panel));
        EditorDialogStyle::PaintDivider(dc, Rect(rect.left, rect.bottom - 1, rect.right, rect.bottom), theme);
        const RECT check = Rect(rect.left + 12, rect.top + 15, rect.left + 29, rect.top + 32);
        const bool activeCheck = enabled && checked;
        EditorDialogStyle::PaintCheckbox(dc, check, theme, activeCheck, enabled);
        EditorDialogStyle::PaintText(dc, Rect(rect.left + 42, rect.top + 5, rect.right - 10, rect.top + 25), title, EditorDialogStyle::Color(enabled ? theme.textPrimary : theme.textDisabled), 12, FW_SEMIBOLD);
        EditorDialogStyle::PaintText(dc, Rect(rect.left + 42, rect.top + 24, rect.right - 10, rect.bottom - 4), subtitle, EditorDialogStyle::Color(enabled ? theme.textSecondary : theme.textDisabled), 11);
    }

    // A row with a title, a hint, and (on the right) what it is set to.
    static void DrawValueRow(HDC dc, const EditorTheme& theme, const RECT& rect, std::string_view title, std::string_view subtitle,
        std::string_view value, bool enabled) {
        DrawLabelRow(dc, theme, rect, title, subtitle, enabled);
        EditorDialogStyle::PaintText(dc, Rect(rect.right - 260, rect.top + 5, rect.right - 10, rect.bottom - 5), value,
            EditorDialogStyle::Color(enabled ? theme.textPrimary : theme.textDisabled), 12, FW_SEMIBOLD, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    static void DrawLabelRow(HDC dc, const EditorTheme& theme, const RECT& rect, std::string_view title, std::string_view subtitle, bool enabled) {
        GdiDrawing::FillRectColor(dc, rect, EditorDialogStyle::Color(theme.panel));
        EditorDialogStyle::PaintDivider(dc, Rect(rect.left, rect.bottom - 1, rect.right, rect.bottom), theme);
        EditorDialogStyle::PaintText(dc, Rect(rect.left + 12, rect.top + 5, rect.right - 280, rect.top + 25), title, EditorDialogStyle::Color(enabled ? theme.textPrimary : theme.textDisabled), 12, FW_SEMIBOLD);
        EditorDialogStyle::PaintText(dc, Rect(rect.left + 12, rect.top + 24, rect.right - 280, rect.bottom - 4), subtitle, EditorDialogStyle::Color(enabled ? theme.textSecondary : theme.textDisabled), 11);
    }

    void Refresh() const noexcept { InvalidateRect(window_, nullptr, FALSE); }

    void OnClick(int x, int y) {
        const RECT client = Client();
        if (Contains(CloseRect(), x, y)) { Close(false); return; }
        for (int field = 0; field < kFieldCount; ++field) {
            if (TransformEnabled() && Contains(FieldRect(field), x, y)) { focusedField_ = field; Refresh(); return; }
        }
        focusedField_ = -1;
        if (!presets_.empty() && Contains(PresetRow(), x, y)) {
            presetIndex_ = (presetIndex_ + 1U) % (presets_.size() + 1U);
            ApplyPreset();
            Refresh();
            return;
        }
        if (materialSlotsAvailable_ && !importSkeletalMesh_ && Contains(MaterialSlotsRow(), x, y)) { importMaterialSlots_ = !importMaterialSlots_; Refresh(); return; }
        if (skeletalImportAvailable_ && Contains(TexturesRow(), x, y)) {
            importTextures_ = !importTextures_;
            if (!importTextures_) importMaterials_ = false;
            Refresh();
            return;
        }
        if (skeletalImportAvailable_ && Contains(MaterialsRow(), x, y)) {
            importMaterials_ = !importMaterials_;
            if (importMaterials_) importTextures_ = true;
            Refresh();
            return;
        }
        if (skeletalImportAvailable_ && Contains(CombineMeshesRow(), x, y)) {
            combineMeshes_ = !combineMeshes_;
            Refresh();
            return;
        }
        if (skeletalImportAvailable_ && Contains(SkeletonRow(), x, y)) { importSkeletalMesh_ = !importSkeletalMesh_; Refresh(); return; }
        if (Contains(Rect(client.right - 198, client.bottom - 27, client.right - 106, client.bottom - 3), x, y)) { Close(false); return; }
        if (Contains(Rect(client.right - 96, client.bottom - 27, client.right - 16, client.bottom - 3), x, y)) Accept();
        Refresh();
    }

    // Typing into the focused scale or rotation field: digits, sign, decimal point; Backspace
    // deletes, Tab moves to the next field.
    [[nodiscard]] bool OnChar(wchar_t character) {
        if (focusedField_ < 0) return false;
        std::string& text = fields_[static_cast<std::size_t>(focusedField_)];
        if (character == L'\b') {
            if (!text.empty()) text.pop_back();
        } else if (character == L'\t') {
            focusedField_ = (focusedField_ + 1) % kFieldCount;
        } else if ((character >= L'0' && character <= L'9') || character == L'.' || character == L'-' || character == L'e' || character == L'E') {
            if (text.size() < 16U) text.push_back(static_cast<char>(character));
        } else {
            return false;
        }
        Refresh();
        return true;
    }

    void Accept() noexcept {
        if (!ParsedTransform().has_value()) {
            Refresh();
            return;
        }
        Close(true);
    }

    [[nodiscard]] RECT CloseRect() const noexcept {
        const RECT client = Client();
        return Rect(client.right - 36, 6, client.right - 14, 28);
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
        case WM_CHAR: if (self->OnChar(static_cast<wchar_t>(wparam))) return 0; break;
        case WM_KEYDOWN: if (wparam == VK_ESCAPE) { self->Close(false); return 0; } if (wparam == VK_RETURN) { self->Accept(); return 0; } break;
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
    std::vector<EditorAssetImportPreset> presets_;
    std::size_t presetIndex_ = 0U;
    std::array<std::string, kFieldCount> fields_{};
    int focusedField_ = -1;
    bool accepted_ = false;
    bool running_ = true;
};

} // namespace

std::optional<EditorAssetImportSelection> EditorAssetImportDialog::Open(HWND owner, std::span<const EditorAssetImportPreset> presets) {
    std::array<wchar_t, 65536> fileBuffer{};
    std::string extensions;
    for (const std::string& extension : kb::assets::AssetImportCatalog::SupportedSourceExtensions()) {
        extensions += (extensions.empty() ? "*" : ";*") + extension;
    }
    const std::wstring filter = EditorFileDialogFilter({
        { EditorLocalization::Format("fileDialog.assetSourcesFilter", "Game asset sources ({extensions})",
              { { "extensions", extensions } }), extensions },
        { EditorText("fileDialog.allFiles", "All files (*.*)"), "*.*" },
    });
    const std::wstring title = ScriptEditorTextEncoding::Widen(EditorText("fileDialog.importAssetsTitle", "Import assets"));
    OPENFILENAMEW openFileName{};
    openFileName.lStructSize = sizeof(openFileName); openFileName.hwndOwner = owner; openFileName.lpstrFilter = filter.c_str(); openFileName.lpstrFile = fileBuffer.data(); openFileName.nMaxFile = static_cast<DWORD>(fileBuffer.size()); openFileName.lpstrTitle = title.c_str();
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
    if (containsMesh) {
        const std::optional<kb::assets::AssetImportOptions> meshOptions =
            MeshImportOptionsWindow{ files.size(), materialSlotsAvailable, skeletalImportAvailable, presets }.Show(owner);
        if (!meshOptions.has_value()) return std::nullopt;
        options = *meshOptions;
    }
    return EditorAssetImportSelection{ .files = std::move(files), .options = options };
}

} // namespace kb::editor

#endif
