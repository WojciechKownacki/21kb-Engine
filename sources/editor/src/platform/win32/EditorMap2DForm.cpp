#include "platform/win32/EditorMap2DForm.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <array>

namespace kb::editor {
namespace {
constexpr COLORREF background = RGB(18, 23, 32), field = RGB(11, 14, 20), border = RGB(49, 58, 75);
constexpr COLORREF foreground = RGB(235, 238, 247), muted = RGB(150, 160, 182), accent = RGB(110, 168, 255);
bool Contains(RECT r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
void Fill(HDC dc, RECT r, COLORREF color) { SetDCBrushColor(dc, color); FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH))); }
void Text(HDC dc, RECT r, const std::string& text, COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    const auto wide = ScriptEditorTextEncoding::Widen(text);
    DrawTextW(dc, wide.data(), static_cast<int>(wide.size()), &r, format | DT_NOPREFIX);
}
void Button(HDC dc, RECT r, const std::string& label, bool primary = false) {
    Fill(dc, r, primary ? RGB(52, 88, 150) : RGB(29, 35, 47));
    Text(dc, r, label, foreground, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
}

EditorMap2DForm::~EditorMap2DForm() { Close(); }

std::optional<double> EditorMap2DForm::Number(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::nullopt;
    text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    std::replace(text.begin(), text.end(), ',', '.');
    if (!text.empty() && text.front() == '+') text.erase(text.begin());
    double value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::string EditorMap2DForm::Format(double value) {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general, 8);
    return result.ec == std::errc{} ? std::string(buffer.data(), result.ptr) : "0";
}

void EditorMap2DForm::Open(HWND owner, std::string title, std::string hint, std::vector<Map2DFormField> fields,
    std::vector<Map2DFormPreset> presets, Apply apply) {
    Close(); owner_ = owner; title_ = std::move(title); hint_ = std::move(hint);
    fields_ = std::move(fields); presets_ = std::move(presets); apply_ = std::move(apply);
    focused_ = 0; error_.clear(); open_ = true;
}

void EditorMap2DForm::Close() {
    open_ = false;
    for (auto& edit : edits_) if (edit.window && IsWindow(edit.window)) DestroyWindow(edit.window);
    edits_.clear();
    if (owner_ && IsWindow(owner_)) { SetFocus(owner_); InvalidateRect(owner_, nullptr, FALSE); }
}

RECT EditorMap2DForm::FieldRect(std::size_t index) const {
    const LONG y = bounds_.top + 132 + static_cast<LONG>(index) * 62;
    return {bounds_.left + 16, y, bounds_.right - 16, y + 28};
}

void EditorMap2DForm::ReadEdits() {
    for (std::size_t i = 0; i < edits_.size(); ++i) {
        if (!IsWindow(edits_[i].window)) continue;
        if (fields_[i].toggle) { fields_[i].value = SendMessageW(edits_[i].window,BM_GETCHECK,0,0)==BST_CHECKED ? "1" : "0"; continue; }
        std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(edits_[i].window)) + 1, L'\0');
        GetWindowTextW(edits_[i].window, text.data(), static_cast<int>(text.size()));
        text.resize(text.size() - 1);
        fields_[i].value = ScriptEditorTextEncoding::Narrow(text);
    }
}

void EditorMap2DForm::Paint(HDC dc, RECT bounds, HFONT font) {
    if (!open_) return;
    bounds_ = bounds; font_ = font; ReadEdits();
    const auto old = SelectObject(dc, font);
    Fill(dc, bounds, background);
    Text(dc, {bounds.left+16,bounds.top+10,bounds.right-16,bounds.top+38}, title_, foreground);
    Text(dc, {bounds.left+16,bounds.top+42,bounds.right-16,bounds.top+100}, hint_, muted, DT_LEFT | DT_TOP | DT_WORDBREAK);
    const bool create = owner_ && IsWindow(owner_) && edits_.empty();
    if (create) edits_.resize(fields_.size());
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        const auto& f = fields_[i]; const auto r = FieldRect(i);
        Text(dc, {r.left,r.top-25,r.right,r.top-3}, f.label, foreground);
        RECT input = r; if (!f.text && !f.toggle) input.right -= 68;
        Fill(dc, input, field);
        SetDCBrushColor(dc, focused_ == i ? accent : border);
        FrameRect(dc, &input, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        Text(dc, {input.left+7,input.top,input.right-7,input.bottom}, f.toggle ? std::string(EditorText(f.value=="1" ? "map2d.form.enabled" : "map2d.form.disabled",f.value=="1" ? "Enabled" : "Disabled")) : f.value, foreground);
        if (!f.text && !f.toggle) {
            Button(dc, {r.right-62,r.top,r.right-34,r.bottom}, "-");
            Button(dc, {r.right-28,r.top,r.right,r.bottom}, "+");
        }
        if (create) {
            const auto value = ScriptEditorTextEncoding::Widen(f.toggle ? std::string(EditorText("map2d.form.enabled","Enabled")) : f.value);
            auto& edit = edits_[i];
            edit.window = CreateWindowExW(0, f.toggle ? L"BUTTON" : L"EDIT", value.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | (f.toggle ? BS_AUTOCHECKBOX : ES_AUTOHSCROLL),
                0,0,1,1,owner_,nullptr,GetModuleHandleW(nullptr),nullptr);
            if (edit.window) {
                SetWindowLongPtrW(edit.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
                edit.procedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&EditProc)));
                SendMessageW(edit.window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (f.toggle) SendMessageW(edit.window,BM_SETCHECK,f.value=="1" ? BST_CHECKED : BST_UNCHECKED,0);
                else SendMessageW(edit.window, EM_SETLIMITTEXT, f.text ? 1024 : 64, 0);
            }
        }
        if (i < edits_.size() && edits_[i].window) SetWindowPos(edits_[i].window, HWND_TOP, input.left+7,input.top+5,
            std::max(1L,input.right-input.left-14),18,SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (create && !edits_.empty()) Focus(0);
    const LONG y = bounds.top + 132 + static_cast<LONG>(fields_.size()) * 62 + 2;
    const LONG width = (bounds.right-bounds.left-32-6*static_cast<LONG>(presets_.size() ? presets_.size()-1 : 0)) /
        static_cast<LONG>(std::max<std::size_t>(1,presets_.size()));
    for (std::size_t i = 0; i < presets_.size(); ++i) {
        const LONG x = bounds.left + 16 + static_cast<LONG>(i)*(width+6);
        Button(dc, {x,y,x+width,y+28}, presets_[i].label);
    }
    Text(dc, {bounds.left+16,y+36,bounds.right-16,bounds.bottom-48}, error_, RGB(255,190,110), DT_LEFT | DT_TOP | DT_WORDBREAK);
    Button(dc, {bounds.right-124,bounds.bottom-40,bounds.right-16,bounds.bottom-12}, std::string(EditorText("common.apply","Apply")), true);
    Button(dc, {bounds.right-240,bounds.bottom-40,bounds.right-132,bounds.bottom-12}, std::string(EditorText("common.cancel","Cancel")));
    SelectObject(dc, old);
}

void EditorMap2DForm::Focus(std::size_t index) {
    focused_ = index;
    if (index < edits_.size() && edits_[index].window) {
        SetFocus(edits_[index].window); if (!fields_[index].toggle) SendMessageW(edits_[index].window, EM_SETSEL, 0, -1);
    }
    if (owner_) InvalidateRect(owner_, nullptr, FALSE);
}

bool EditorMap2DForm::SetValue(std::size_t index, const std::string& value) {
    if (!open_ || index >= fields_.size()) return false;
    fields_[index].value = value; error_.clear();
    if (index < edits_.size() && edits_[index].window) {
        if (fields_[index].toggle) SendMessageW(edits_[index].window,BM_SETCHECK,value=="1" ? BST_CHECKED : BST_UNCHECKED,0);
        else SetWindowTextW(edits_[index].window, ScriptEditorTextEncoding::Widen(value).c_str());
    }
    if (owner_) InvalidateRect(owner_, nullptr, FALSE);
    return true;
}

void EditorMap2DForm::Adjust(std::size_t index, int direction) {
    ReadEdits(); auto& f = fields_[index]; if (f.text) return;
    const double value = Number(f.value).value_or(f.minimum);
    SetValue(index, Format(std::clamp(value + direction*f.step, f.minimum, f.maximum))); Focus(index);
}

bool EditorMap2DForm::Preset(std::size_t index) {
    if (!open_ || index >= presets_.size()) return false;
    for (std::size_t i = 0; i < presets_[index].values.size(); ++i) SetValue(i, presets_[index].values[i]);
    return true;
}

void EditorMap2DForm::Click(int x, int y) {
    if (!open_) return;
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        const auto r = FieldRect(i);
        if (!Contains(r,x,y)) continue;
        if (fields_[i].toggle) SetValue(i,fields_[i].value=="1" ? "0" : "1");
        else if (!fields_[i].text && x >= r.right-62) Adjust(i, x >= r.right-28 ? 1 : -1);
        else Focus(i);
        return;
    }
    const LONG top = bounds_.top + 134 + static_cast<LONG>(fields_.size())*62;
    const LONG width = (bounds_.right-bounds_.left-32-6*static_cast<LONG>(presets_.size() ? presets_.size()-1 : 0)) /
        static_cast<LONG>(std::max<std::size_t>(1,presets_.size()));
    for (std::size_t i = 0; i < presets_.size(); ++i) {
        const LONG left = bounds_.left+16+static_cast<LONG>(i)*(width+6);
        if (Contains({left,top,left+width,top+28},x,y)) { Preset(i); return; }
    }
    if (Contains({bounds_.right-124,bounds_.bottom-40,bounds_.right-16,bounds_.bottom-12},x,y)) Accept();
    else if (Contains({bounds_.right-240,bounds_.bottom-40,bounds_.right-132,bounds_.bottom-12},x,y)) Close();
}

bool EditorMap2DForm::Accept() {
    if (!open_) return false;
    ReadEdits(); std::vector<std::string> values;
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        const auto& f = fields_[i];
        if (f.text) { values.push_back(f.value); continue; }
        const auto value = Number(f.value);
        if (!value || *value < f.minimum || *value > f.maximum || (f.integer && std::floor(*value) != *value)) {
            error_ = EditorLocalization::Format(f.integer ? "map2d.form.integerError" : "map2d.form.numberError",
                f.integer ? "{field}: enter a whole number from {min} to {max}." : "{field}: enter a number from {min} to {max}.",
                {{"field",f.label},{"min",Format(f.minimum)},{"max",Format(f.maximum)}});
            Focus(i); return false;
        }
        values.push_back(Format(*value));
    }
    error_ = apply_(values);
    if (!error_.empty()) { if (owner_) InvalidateRect(owner_,nullptr,FALSE); return false; }
    Close(); return true;
}

bool EditorMap2DForm::Key(WPARAM key, bool shift) {
    if (!open_) return false;
    if (key == VK_ESCAPE) Close();
    else if (key == VK_RETURN) Accept();
    else if (key == VK_TAB && !fields_.empty()) Focus((focused_+fields_.size()+(shift ? -1 : 1))%fields_.size());
    else if ((key == VK_UP || key == VK_DOWN) && !fields_.empty()) Adjust(focused_,key == VK_UP ? 1 : -1);
    else return false;
    return true;
}

LRESULT CALLBACK EditorMap2DForm::EditProc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<EditorMap2DForm*>(GetWindowLongPtrW(edit,GWLP_USERDATA));
    if (!self) return DefWindowProcW(edit,message,wparam,lparam);
    const auto found = std::find_if(self->edits_.begin(),self->edits_.end(),[&](const auto& e) { return e.window == edit; });
    if (found == self->edits_.end()) return DefWindowProcW(edit,message,wparam,lparam);
    const auto original = found->procedure;
    if (message == WM_KEYDOWN && self->Key(wparam,GetKeyState(VK_SHIFT)<0)) return 0;
    if (message == WM_CHAR && (wparam == VK_RETURN || wparam == VK_ESCAPE || wparam == VK_TAB)) return 0;
    if (message == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    if (message == WM_SETFOCUS) { self->focused_ = static_cast<std::size_t>(found-self->edits_.begin()); InvalidateRect(self->owner_,nullptr,FALSE); }
    return CallWindowProcW(original,edit,message,wparam,lparam);
}
}
#endif
