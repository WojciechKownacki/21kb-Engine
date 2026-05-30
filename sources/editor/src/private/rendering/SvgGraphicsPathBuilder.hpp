#pragma once

#include "rendering/SvgPathCursor.hpp"
#include "rendering/SvgPathFigureBuilder.hpp"

#if defined(_WIN32)
#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)
#endif

#include <array>
#include <string_view>

namespace kb::editor {

#if defined(_WIN32)

class SvgGraphicsPathBuilder {
public:
    explicit SvgGraphicsPathBuilder(std::string_view data);

    void Build(Gdiplus::GraphicsPath& path);

private:
    void Execute(SvgPathFigureBuilder& figure, char command);
    void ReadMove(SvgPathFigureBuilder& figure, bool relative);
    void ReadLines(SvgPathFigureBuilder& figure, bool relative);
    void ReadHorizontal(SvgPathFigureBuilder& figure, bool relative);
    void ReadVertical(SvgPathFigureBuilder& figure, bool relative);
    void ReadCubics(SvgPathFigureBuilder& figure, bool relative);
    void ReadArcs(SvgPathFigureBuilder& figure, bool relative);

    template <std::size_t Size>
    [[nodiscard]] bool ReadValues(std::array<double, Size>& values) {
        for (double& value : values) {
            if (!cursor_.ReadNumber(value)) {
                return false;
            }
        }
        return true;
    }

    SvgPathCursor cursor_;
};

#endif

} // namespace kb::editor
