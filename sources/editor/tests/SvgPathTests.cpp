#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "rendering/SvgPathArcEndpoint.hpp"
#include "rendering/SvgPathArcMath.hpp"
#include "rendering/SvgPathCursor.hpp"

#include <optional>

namespace {

void RunCursorCommandAndNumberTest() {
    kb::editor::SvgPathCursor cursor(" M10,-20 l 5.5e1 -3.25 Z ");

    kb::editor::tests::Require(cursor.HasMore(), "SVG cursor should see initial command");
    kb::editor::tests::Require(cursor.NextIsCommand(), "SVG cursor should detect move command");
    kb::editor::tests::Require(cursor.ReadCommand() == 'M', "SVG cursor read invalid move command");

    double value = 0.0;
    kb::editor::tests::Require(cursor.ReadNumber(value) && kb::editor::tests::NearlyEqual(value, 10.0), "SVG cursor failed to read first coordinate");
    kb::editor::tests::Require(cursor.ReadNumber(value) && kb::editor::tests::NearlyEqual(value, -20.0), "SVG cursor failed to read comma-separated negative coordinate");
    kb::editor::tests::Require(cursor.NextIsCommand() && cursor.ReadCommand() == 'l', "SVG cursor failed to read relative line command");
    kb::editor::tests::Require(cursor.ReadNumber(value) && kb::editor::tests::NearlyEqual(value, 55.0), "SVG cursor failed to read exponent number");
    kb::editor::tests::Require(cursor.ReadNumber(value) && kb::editor::tests::NearlyEqual(value, -3.25), "SVG cursor failed to read signed number after whitespace");
    kb::editor::tests::Require(cursor.NextIsCommand() && cursor.ReadCommand() == 'Z', "SVG cursor failed to read close command");
    kb::editor::tests::Require(!cursor.HasMore(), "SVG cursor should be exhausted");
}

void RunCursorCompactSignedNumberTest() {
    kb::editor::SvgPathCursor cursor("M10-20");

    kb::editor::tests::Require(cursor.ReadCommand() == 'M', "SVG cursor failed to read compact move command");

    double x = 0.0;
    double y = 0.0;
    kb::editor::tests::Require(cursor.ReadNumber(x), "SVG cursor failed to read compact x coordinate");
    kb::editor::tests::Require(cursor.ReadNumber(y), "SVG cursor failed to read compact y coordinate");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(x, 10.0), "SVG cursor read invalid compact x coordinate");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(y, -20.0), "SVG cursor read invalid compact y coordinate");
}

void RunArcMathStraightFallbackTest() {
    const std::optional<kb::editor::SvgPathArcCenter> arc = kb::editor::SvgPathArcMath::ToCenterArc(kb::editor::SvgPathArcEndpoint{
        .from = kb::editor::SvgPathPoint{ 1.0, 1.0 },
        .to = kb::editor::SvgPathPoint{ 1.0, 1.0 },
        .rx = 10.0,
        .ry = 10.0,
    });

    kb::editor::tests::Require(!arc.has_value(), "SVG arc math should reject arcs with identical endpoints");
}

void RunArcMathHalfCircleTest() {
    const std::optional<kb::editor::SvgPathArcCenter> arc = kb::editor::SvgPathArcMath::ToCenterArc(kb::editor::SvgPathArcEndpoint{
        .from = kb::editor::SvgPathPoint{ 0.0, 0.0 },
        .to = kb::editor::SvgPathPoint{ 10.0, 0.0 },
        .rx = 5.0,
        .ry = 5.0,
        .rotationDegrees = 0.0,
        .largeArc = false,
        .sweep = true,
    });

    kb::editor::tests::Require(arc.has_value(), "SVG arc math did not produce a center arc");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(arc->cx, 5.0), "SVG arc math computed invalid center x");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(arc->cy, 0.0), "SVG arc math computed invalid center y");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(arc->rx, 5.0), "SVG arc math kept invalid rx");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(arc->ry, 5.0), "SVG arc math kept invalid ry");
    kb::editor::tests::Require(arc->delta > 3.1415 && arc->delta < 3.1417, "SVG arc math should use positive half-circle sweep");
}

void RunArcMathScalesSmallRadiiTest() {
    const std::optional<kb::editor::SvgPathArcCenter> arc = kb::editor::SvgPathArcMath::ToCenterArc(kb::editor::SvgPathArcEndpoint{
        .from = kb::editor::SvgPathPoint{ 0.0, 0.0 },
        .to = kb::editor::SvgPathPoint{ 10.0, 0.0 },
        .rx = 1.0,
        .ry = 1.0,
        .rotationDegrees = 0.0,
        .largeArc = false,
        .sweep = true,
    });

    kb::editor::tests::Require(arc.has_value(), "SVG arc math should scale undersized radii instead of rejecting the arc");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(arc->rx, 5.0), "SVG arc math did not scale undersized rx");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(arc->ry, 5.0), "SVG arc math did not scale undersized ry");
}

} // namespace

namespace kb::editor::tests {

void RunSvgPathTests() {
    RunCursorCommandAndNumberTest();
    RunCursorCompactSignedNumberTest();
    RunArcMathStraightFallbackTest();
    RunArcMathHalfCircleTest();
    RunArcMathScalesSmallRadiiTest();
}

} // namespace kb::editor::tests
