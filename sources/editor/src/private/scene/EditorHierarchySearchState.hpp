#pragma once

#include <string>
#include <string_view>

namespace kb::editor {

class EditorHierarchySearchState {
public:
    [[nodiscard]] std::string_view Query() const noexcept;
    [[nodiscard]] bool IsFocused() const noexcept;

    void Focus(bool focused) noexcept;
    void SetQuery(std::string query);
    void AppendAscii(wchar_t character);
    void Insert(std::string_view text);
    void Backspace();
    void SelectAll() noexcept;
    void Clear();

private:
    std::string query_;
    bool focused_ = false;
    bool selectingAll_ = false;
};

} // namespace kb::editor
