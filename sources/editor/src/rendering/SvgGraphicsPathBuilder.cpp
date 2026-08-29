#include "rendering/SvgGraphicsPathBuilder.hpp"

#if defined(_WIN32)

namespace kb::editor {

SvgGraphicsPathBuilder::SvgGraphicsPathBuilder(std::string_view data) : cursor_(data) {}

void SvgGraphicsPathBuilder::Build(Gdiplus::GraphicsPath& path) {
    SvgPathFigureBuilder figure(path);
    char command = '\0';

    while (cursor_.HasMore()) {
        if (cursor_.NextIsCommand()) {
            command = cursor_.ReadCommand();
        }
        if (command == '\0') {
            break;
        }
        // A command this builder does not implement reads nothing, and the character after
        // it is not a command either - so without this the loop would sit on the same spot
        // forever. One malformed path must cost the rest of that path, not the editor.
        const std::size_t before = cursor_.Position();
        Execute(figure, command);
        if (cursor_.Position() == before) {
            break;
        }
    }
}

void SvgGraphicsPathBuilder::Execute(SvgPathFigureBuilder& figure, char command) {
    switch (command) {
    case 'M':
    case 'm':
        ReadMove(figure, command == 'm');
        return;
    case 'L':
    case 'l':
        ReadLines(figure, command == 'l');
        return;
    case 'H':
    case 'h':
        ReadHorizontal(figure, command == 'h');
        return;
    case 'V':
    case 'v':
        ReadVertical(figure, command == 'v');
        return;
    case 'C':
    case 'c':
        ReadCubics(figure, command == 'c');
        return;
    case 'A':
    case 'a':
        ReadArcs(figure, command == 'a');
        return;
    case 'Z':
    case 'z':
        figure.Close();
        return;
    default:
        return;
    }
}

void SvgGraphicsPathBuilder::ReadMove(SvgPathFigureBuilder& figure, bool relative) {
    double x = 0.0;
    double y = 0.0;
    if (!cursor_.ReadNumber(x) || !cursor_.ReadNumber(y)) {
        return;
    }

    figure.Move(x, y, relative);
    while (cursor_.HasMore() && !cursor_.NextIsCommand()) {
        if (!cursor_.ReadNumber(x) || !cursor_.ReadNumber(y)) {
            return;
        }
        figure.Line(x, y, relative);
    }
}

void SvgGraphicsPathBuilder::ReadLines(SvgPathFigureBuilder& figure, bool relative) {
    double x = 0.0;
    double y = 0.0;
    while (cursor_.ReadNumber(x) && cursor_.ReadNumber(y)) {
        figure.Line(x, y, relative);
    }
}

void SvgGraphicsPathBuilder::ReadHorizontal(SvgPathFigureBuilder& figure, bool relative) {
    double x = 0.0;
    while (cursor_.ReadNumber(x)) {
        figure.Horizontal(x, relative);
    }
}

void SvgGraphicsPathBuilder::ReadVertical(SvgPathFigureBuilder& figure, bool relative) {
    double y = 0.0;
    while (cursor_.ReadNumber(y)) {
        figure.Vertical(y, relative);
    }
}

void SvgGraphicsPathBuilder::ReadCubics(SvgPathFigureBuilder& figure, bool relative) {
    std::array<double, 6> values{};
    while (ReadValues(values)) {
        figure.Cubic(values[0], values[1], values[2], values[3], values[4], values[5], relative);
    }
}

void SvgGraphicsPathBuilder::ReadArcs(SvgPathFigureBuilder& figure, bool relative) {
    std::array<double, 7> values{};
    while (ReadValues(values)) {
        figure.Arc(values[0], values[1], values[2], values[3] != 0.0, values[4] != 0.0, values[5], values[6], relative);
    }
}

} // namespace kb::editor

#endif
