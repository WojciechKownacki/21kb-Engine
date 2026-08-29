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

    // How far into the path text the cursor has come. A caller driving this in a loop
    // needs to know whether a pass actually consumed anything.
    [[nodiscard]] std::size_t Position() const noexcept { return position_; }

private:
    void SkipSeparators();

    std::string_view text_;
    std::size_t position_ = 0;
};

} // namespace kb::editor
