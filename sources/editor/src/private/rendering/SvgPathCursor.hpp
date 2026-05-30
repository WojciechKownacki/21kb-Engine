#pragma once

#include <string_view>

namespace kb::editor {

class SvgPathCursor {
public:
    explicit SvgPathCursor(std::string_view text);

    [[nodiscard]] bool HasMore();
    [[nodiscard]] bool NextIsCommand();
    [[nodiscard]] char ReadCommand();
    [[nodiscard]] bool ReadNumber(double& output);

private:
    void SkipSeparators();

    std::string_view text_;
    std::size_t position_ = 0;
};

} // namespace kb::editor
