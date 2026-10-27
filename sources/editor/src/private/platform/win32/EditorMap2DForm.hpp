#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kb::editor {

struct Map2DFormField {
    std::string label;
    std::string value;
    double minimum = 0;
    double maximum = 100;
    double step = 1;
    bool integer = false;
    bool text = false;
    bool toggle = false;
};

struct Map2DFormPreset {
    std::string label;
    std::vector<std::string> values;
};

// A docked, cancellable form. Native edit controls retain normal selection,
// clipboard and keyboard behavior; the same draft and validation serve automation.
class EditorMap2DForm final {
public:
    using Apply = std::function<std::string(const std::vector<std::string>&)>;
    ~EditorMap2DForm();
    void Open(HWND owner, std::string title, std::string hint, std::vector<Map2DFormField> fields,
        std::vector<Map2DFormPreset> presets, Apply apply);
    void Close();
    bool IsOpen() const noexcept { return open_; }
    const std::string& Error() const noexcept { return error_; }
    void Paint(HDC dc, RECT bounds, HFONT font);
    void Click(int x, int y);
    bool Key(WPARAM key, bool shift = false);
    bool Accept();
    bool SetValue(std::size_t index, const std::string& value);
    bool Preset(std::size_t index);
    static std::optional<double> Number(std::string text);
    static std::string Format(double value);

private:
    void ReadEdits();
    void Focus(std::size_t index);
    void Adjust(std::size_t index, int direction);
    RECT FieldRect(std::size_t index) const;
    static LRESULT CALLBACK EditProc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam);
    struct Edit { HWND window = nullptr; WNDPROC procedure = nullptr; };
    HWND owner_ = nullptr;
    HFONT font_ = nullptr;
    RECT bounds_{};
    std::vector<Map2DFormField> fields_;
    std::vector<Map2DFormPreset> presets_;
    std::vector<Edit> edits_;
    Apply apply_;
    std::string title_, hint_, error_;
    std::size_t focused_ = 0;
    bool open_ = false;
};
}
#endif
