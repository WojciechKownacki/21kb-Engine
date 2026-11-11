#include "app/EditorCurveEditorPointerHandler.hpp"

#if defined(_WIN32)
#include "app/EditorKeyState.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "localization/EditorLocalization.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "rendering/AnimationClipEditorPanelRenderer.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/ParticleEditorPanelLayout.hpp"
#include "scene/EditorSceneContext.hpp"

#include <windowsx.h>

#include <array>
#include <string>
#include <string_view>

namespace kb::editor {
namespace {

[[nodiscard]] bool Inside(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] CurveEditorViewport ToViewport(const RECT& rect) noexcept {
    return { static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(rect.right), static_cast<float>(rect.bottom) };
}

} // namespace

std::optional<RECT> EditorCurveEditorPointerHandler::BindViewAt(HWND messageWindow, int x, int y) {
    EditorSceneContext& scene = context_.sceneContext;
    if (scene.HasAnimationClipEditorAsset()) {
        if (const std::optional<RECT> content = EditorPanelContentResolver::Resolve(DockPanelKind::AnimationClipEditor,
                messageWindow, context_.mainWindow, context_.dockModel, context_.floatingWindows, context_.metrics)) {
            const std::optional<RECT> view = AnimationClipEditorPanelRenderer::CurveViewRect(*content, scene.AnimationClipEditorTimeline());
            if (view.has_value() && Inside(*view, x, y) && scene.BindAnimationClipCurveEditor()) {
                scene.CurveEditor().SetViewport(ToViewport(*view));
                return view;
            }
        }
    }
    if (scene.HasParticleEditorAsset()) {
        if (const std::optional<RECT> content = EditorPanelContentResolver::Resolve(DockPanelKind::ParticleEditor,
                messageWindow, context_.mainWindow, context_.dockModel, context_.floatingWindows, context_.metrics);
            content.has_value() && Inside(*content, x, y)) {
            const auto rows = scene.ParticleEditorEmitterRows();
            const auto inspector = scene.ParticleEditorInspector();
            const auto recipes = scene.ParticleEditorRecipes();
            const ParticleEditorPanelLayout layout = ParticleEditorPanelLayoutResolver::Resolve(*content, rows,
                scene.ParticleEditorWorkspace().ComposerScrollOffset(), GetDpiForWindow(messageWindow), &inspector,
                recipes.size(), &scene.ParticleEditorWorkspace());
            for (std::size_t index = 0U; index < layout.propertyRowCount; ++index) {
                const RECT& view = layout.propertyRows[index].curvePreview;
                if (inspector.properties[index].widget == kb::particle_editor::ParticleEditorPropertyWidget::Curve &&
                    Inside(view, x, y) && Inside(layout.composer, x, y) && scene.BindParticleCurveEditor(index)) {
                    scene.CurveEditor().SetViewport(ToViewport(view));
                    return view;
                }
            }
        }
    }
    return std::nullopt;
}

void EditorCurveEditorPointerHandler::Apply(HWND messageWindow, const CurveEditorResult& result) {
    if (result.editBegin || result.edited || result.editEnd) {
        static_cast<void>(context_.sceneContext.ApplyCurveEditorResult(result));
        context_.sceneViewport.RequestPresent();
    }
    if (result.redraw || result.edited) EditorWindowInvalidator::InvalidateMainAndSource(context_.mainWindow, messageWindow);
}

void EditorCurveEditorPointerHandler::ShowTangentMenu(HWND messageWindow, int x, int y) {
    CurveEditorState& state = context_.sceneContext.CurveEditor();
    const CurveEditorHit hit = state.HitTest(static_cast<float>(x), static_cast<float>(y));
    if (hit.handle == CurveEditorHandle::Key) static_cast<void>(state.Select(hit.key));
    const std::optional<std::size_t> selected = state.Selected();
    if (!selected.has_value()) return;
    EditorWindowInvalidator::InvalidateMainAndSource(context_.mainWindow, messageWindow);
    struct Item { kb::math::CurveTangentMode mode; const char* key; const char* fallback; };
    constexpr std::array<Item, 5> items{ {
        { kb::math::CurveTangentMode::Auto, "curveEditor.tangent.auto", "Auto" },
        { kb::math::CurveTangentMode::Flat, "curveEditor.tangent.flat", "Flat" },
        { kb::math::CurveTangentMode::Linear, "curveEditor.tangent.linear", "Linear" },
        { kb::math::CurveTangentMode::Constant, "curveEditor.tangent.constant", "Constant" },
        { kb::math::CurveTangentMode::Free, "curveEditor.tangent.free", "Free" },
    } };
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    const kb::math::CurveTangentMode current = state.Curve().keyframes[*selected].tangentMode;
    for (std::size_t index = 0U; index < items.size(); ++index) {
        const std::string_view text = EditorText(items[index].key, items[index].fallback);
        const std::wstring label = WidenUtf8(text.data(), static_cast<int>(text.size()));
        AppendMenuW(menu, MF_STRING | (items[index].mode == current ? MF_CHECKED : 0U), index + 1U, label.c_str());
    }
    POINT screen{ x, y };
    ClientToScreen(messageWindow, &screen);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, screen.x, screen.y, 0, messageWindow, nullptr);
    DestroyMenu(menu);
    if (command == 0U || command > items.size()) return;
    Apply(messageWindow, CurveEditorInteraction::SetTangentMode(state, items[command - 1U].mode));
}

bool EditorCurveEditorPointerHandler::HandlePointer(HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam) {
    EditorSceneContext& scene = context_.sceneContext;
    CurveEditorInteraction& input = scene.CurveEditorInput();
    CurveEditorState& state = scene.CurveEditor();
    POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    if (message == WM_MOUSEWHEEL) ScreenToClient(messageWindow, &point);
    const float x = static_cast<float>(point.x);
    const float y = static_cast<float>(point.y);
    switch (message) {
    case WM_MOUSEMOVE:
        if (input.Dragging()) {
            if ((wparam & MK_LBUTTON) == 0U) {
                ReleaseCapture();
                Apply(messageWindow, input.PointerUp());
            } else {
                Apply(messageWindow, input.PointerMove(state, x, y, EditorKeyDown(VK_SHIFT)));
            }
            return true;
        }
        if (input.Panning()) {
            Apply(messageWindow, input.MiddleMove(state, x, y));
            return true;
        }
        return false;
    case WM_LBUTTONUP:
        if (!input.Dragging()) return false;
        ReleaseCapture();
        Apply(messageWindow, input.PointerUp());
        return true;
    case WM_MBUTTONUP:
        if (!input.Panning()) return false;
        input.MiddleUp();
        ReleaseCapture();
        return true;
    default:
        break;
    }
    if (!BindViewAt(messageWindow, point.x, point.y).has_value()) return false;
    switch (message) {
    case WM_LBUTTONDOWN:
        Apply(messageWindow, input.PointerDown(state, x, y, EditorKeyDown(VK_SHIFT), EditorKeyDown(VK_CONTROL)));
        if (input.Dragging()) SetCapture(messageWindow);
        return true;
    case WM_LBUTTONDBLCLK:
        Apply(messageWindow, input.DoubleClick(state, x, y));
        return true;
    case WM_MBUTTONDOWN:
        input.MiddleDown(x, y);
        SetCapture(messageWindow);
        return true;
    case WM_MOUSEWHEEL:
        Apply(messageWindow, CurveEditorInteraction::Wheel(state, x, y, GET_WHEEL_DELTA_WPARAM(wparam), EditorKeyDown(VK_CONTROL)));
        return true;
    case WM_RBUTTONDOWN:
        return true;
    case WM_RBUTTONUP:
        ShowTangentMenu(messageWindow, point.x, point.y);
        return true;
    default:
        return false;
    }
}

bool EditorCurveEditorPointerHandler::HandleKey(HWND messageWindow, WPARAM key) {
    if (key != 'F' && key != VK_DELETE) return false;
    POINT point{};
    if (!GetCursorPos(&point) || !ScreenToClient(messageWindow, &point)) return false;
    if (!BindViewAt(messageWindow, point.x, point.y).has_value()) return false;
    Apply(messageWindow, CurveEditorInteraction::Key(context_.sceneContext.CurveEditor(),
        key == 'F' ? CurveEditorKey::FrameAll : CurveEditorKey::Delete));
    return true;
}

} // namespace kb::editor
#endif
