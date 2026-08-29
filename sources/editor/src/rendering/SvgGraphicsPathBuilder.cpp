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
        // Progress is the wrong test for that: a close consumes nothing either, and using it
        // stopped every icon at its first subpath.
        if (!Execute(figure, command)) {
            break;
        }
    }
}

bool SvgGraphicsPathBuilder::Execute(SvgPathFigureBuilder& figure, char command) {
    switch (command) {
    case 'M':
    case 'm':
        ReadMove(figure, command == 'm');
        return true;
    case 'L':
    case 'l':
        ReadLines(figure, command == 'l');
        return true;
    case 'H':
    case 'h':
        ReadHorizontal(figure, command == 'h');
        return true;
    case 'V':
    case 'v':
        ReadVertical(figure, command == 'v');
        return true;
    case 'C':
    case 'c':
        ReadCubics(figure, command == 'c');
        return true;
    case 'A':
    case 'a':
        ReadArcs(figure, command == 'a');
        return true;
    case 'Z':
    case 'z':
        figure.Close();
        return true;
    default:
        return false;
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
    // Read in the arc's own shape rather than as seven numbers: the large-arc and sweep
    // flags are single digits that the grammar allows to abut the coordinate after them.
    while (true) {
        double rx = 0.0;
        double ry = 0.0;
        double rotation = 0.0;
        bool largeArc = false;
        bool sweep = false;
        double x = 0.0;
        double y = 0.0;
        if (!cursor_.ReadNumber(rx) || !cursor_.ReadNumber(ry) || !cursor_.ReadNumber(rotation) ||
            !cursor_.ReadFlag(largeArc) || !cursor_.ReadFlag(sweep) ||
            !cursor_.ReadNumber(x) || !cursor_.ReadNumber(y)) {
            return;
        }
        figure.Arc(rx, ry, rotation, largeArc, sweep, x, y, relative);
    }
}

} // namespace kb::editor

#endif
