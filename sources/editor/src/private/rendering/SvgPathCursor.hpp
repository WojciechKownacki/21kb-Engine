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

    // An arc's two flags are single digits and the grammar lets them run together with
    // what follows: "a1.5 1.5 0 007.342 7.343" is flags 0 and 0 then x=7.342. Reading them
    // as ordinary numbers swallows the coordinate with them and shifts the whole arc.
    [[nodiscard]] bool ReadFlag(bool& output);

    // How far into the path text the cursor has come. A caller driving this in a loop
    // needs to know whether a pass actually consumed anything.
    [[nodiscard]] std::size_t Position() const noexcept { return position_; }

private:
    void SkipSeparators();

    std::string_view text_;
    std::size_t position_ = 0;
};

} // namespace kb::editor
