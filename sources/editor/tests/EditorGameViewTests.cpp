#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "app/EditorPlayModeState.hpp"
#include "docking/EditorDockModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorGameView.hpp"
#include "scene/EditorViewportMessages.hpp"
#include "settings/EditorConfigurationStore.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

using kb::editor::EditorGameView;
using kb::editor::EditorGameViewDevice;
using kb::editor::EditorGameViewSize;
using kb::editor::tests::Require;

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_game_view_tests";
}

[[nodiscard]] long Width(const RECT& rect) { return rect.right - rect.left; }
[[nodiscard]] long Height(const RECT& rect) { return rect.bottom - rect.top; }

void RunDeviceTableTest() {
    Require((EditorGameView::Size(EditorGameViewDevice::Computer, false) == EditorGameViewSize{ 1920U, 1080U }),
        "A computer renders at 1920 x 1080");
    Require((EditorGameView::Size(EditorGameViewDevice::Computer, true) == EditorGameViewSize{ 1920U, 1080U }),
        "A computer cannot stand");
    const EditorGameViewSize tablet = EditorGameView::Size(EditorGameViewDevice::Tablet, false);
    Require((tablet == EditorGameViewSize{ 1544U, 907U }), "A tablet lies at 1544 x 907");
    Require(std::fabs(static_cast<double>(tablet.width) / tablet.height - 16.0 / 9.0) < 0.1, "The tablet is about 16:9");
    const EditorGameViewSize phone = EditorGameView::Size(EditorGameViewDevice::Phone, false);
    Require((phone == EditorGameViewSize{ 2340U, 1080U }), "A phone lies at 2340 x 1080");
    Require(std::fabs(static_cast<double>(phone.width) / phone.height - 19.5 / 9.0) < 0.01, "The phone is 19.5:9");
    Require((EditorGameView::Size(EditorGameViewDevice::Phone, true) == EditorGameViewSize{ 1080U, 2340U }),
        "Standing swaps the sides of a phone");
    Require((EditorGameView::Size(EditorGameViewDevice::Tablet, true) == EditorGameViewSize{ 907U, 1544U }),
        "Standing swaps the sides of a tablet");
    Require(!EditorGameView::CanRotate(EditorGameViewDevice::Computer) && EditorGameView::CanRotate(EditorGameViewDevice::Tablet) &&
            EditorGameView::CanRotate(EditorGameViewDevice::Phone),
        "Tablets and phones can be turned, computers cannot");

    for (const EditorGameViewDevice device : EditorGameView::kDevices) {
        const auto parsed = EditorGameView::FromName(EditorGameView::Name(device));
        Require(parsed.has_value() && *parsed == device, "Every device should keep its name through the settings text");
        Require(!EditorGameView::Label(device).empty(), "Every device has a name on the bar");
    }
    Require(!EditorGameView::FromName("Nonsense").has_value(), "An unknown name should not select a device");

    Require(EditorGameView::FormatResolution(phone) == "2340x1080", "The resolution reads width x height without spaces");
    Require(EditorGameView::FormatScale(960U, 1920U) == "50%", "A half size picture reads 50%");
    Require(EditorGameView::FormatScale(1544U, 1544U) == "100%", "A picture at its own size reads 100%");
    Require(EditorGameView::FormatScale(1845U, 2340U) == "79%", "The scale is rounded to a whole percent");
    Require(EditorGameView::FormatScale(100U, 0U) == "100%", "No render width reads as 100%");
}

void RunSafeAreaTest() {
    // A computer's whole screen is safe, as in other engines' desktop previews.
    Require(!EditorGameView::HasSafeArea(EditorGameViewDevice::Computer) && EditorGameView::HasSafeArea(EditorGameViewDevice::Tablet) &&
            EditorGameView::HasSafeArea(EditorGameViewDevice::Phone),
        "Tablets and phones have a safe area, a computer does not");
    const kb::editor::EditorViewportSafeArea computer = EditorGameView::SafeArea(EditorGameViewDevice::Computer, false);
    Require(computer.left == 0U && computer.top == 0U && computer.right == 0U && computer.bottom == 0U, "A computer keeps no margins");
    Require(EditorGameView::UnsafeAreaShapes(EditorGameViewDevice::Computer, false, 960U, 540U).empty(), "A computer's picture is not tinted");

    // Devices take the insets of the built-in profiles, grown to their screens.
    const kb::editor::EditorViewportSafeArea phoneLying = EditorGameView::SafeArea(EditorGameViewDevice::Phone, false);
    Require(phoneLying.left == 130U && phoneLying.right == 130U && phoneLying.top == 0U && phoneLying.bottom == 58U,
        "A lying phone keeps clear of its sides and its bottom edge");
    const kb::editor::EditorViewportSafeArea phoneStanding = EditorGameView::SafeArea(EditorGameViewDevice::Phone, true);
    Require(phoneStanding.top > 0U && phoneStanding.bottom > 0U && phoneStanding.left == 0U && phoneStanding.right == 0U,
        "A standing phone keeps clear of its top and its bottom edge");
    const kb::editor::EditorViewportSafeArea tabletLying = EditorGameView::SafeArea(EditorGameViewDevice::Tablet, false);
    Require(tabletLying.top == 26U && tabletLying.bottom == 22U && tabletLying.left == 0U && tabletLying.right == 0U,
        "A lying tablet keeps clear of its bars at the top and the bottom");
    const kb::editor::EditorViewportSafeArea tabletStanding = EditorGameView::SafeArea(EditorGameViewDevice::Tablet, true);
    Require(tabletStanding.top == 31U && tabletStanding.bottom == 26U && tabletStanding.left == 0U,
        "A standing tablet keeps its bars at the top and the bottom, grown to its taller screen");

    // A lying phone shown at half its size: bands at the bottom and both sides, none at the top.
    const auto phone = EditorGameView::UnsafeAreaShapes(EditorGameViewDevice::Phone, false, 1170U, 540U);
    Require(phone.size() == 3U, "A lying phone has a bottom band and two side bands");
    for (const auto& band : phone) {
        Require(band.kind == kb::editor::EditorSceneViewportOverlayShape::Kind::RoundedRect && band.radius == 0.0F && band.points.size() == 2U &&
                band.color == (std::array<std::uint8_t, 4U>{ 255U, 0U, 0U, 51U }),
            "A band is a square, translucent red rectangle");
    }
    Require(phone[0].points[0] == (std::array<float, 2U>{ 0.0F, 511.0F }) && phone[0].points[1] == (std::array<float, 2U>{ 1170.0F, 540.0F }),
        "The bottom band spans the width at the scaled margin");
    Require(phone[1].points[0] == (std::array<float, 2U>{ 0.0F, 0.0F }) && phone[1].points[1] == (std::array<float, 2U>{ 65.0F, 511.0F }),
        "The left band fills down to the bottom band");
    Require(phone[2].points[0][0] == 1105.0F, "The right band starts at the scaled margin");
    Require(EditorGameView::UnsafeAreaShapes(EditorGameViewDevice::Phone, false, 2U, 0U).empty(),
        "A picture without height has no bands");
}

void RunDeviceFitTest() {
    const auto aspect = [](const RECT& rect) { return static_cast<double>(Width(rect)) / static_cast<double>(Height(rect)); };
    const auto inside = [](const RECT& inner, const RECT& outer) {
        return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right && inner.bottom <= outer.bottom;
    };
    const auto centered = [](const RECT& inner, const RECT& outer) {
        return std::abs((inner.left - outer.left) - (outer.right - inner.right)) <= 1 &&
            std::abs((inner.top - outer.top) - (outer.bottom - inner.bottom)) <= 1;
    };

    // A computer is only the picture with a thin frame.
    const RECT area{ 0, 0, 1000, 600 };
    const kb::editor::EditorGameViewFrame computer = EditorGameView::Fit(area, EditorGameViewDevice::Computer, false);
    Require(std::fabs(aspect(computer.screen) - 16.0 / 9.0) < 0.02, "A computer's screen keeps its 16:9 shape");
    Require(computer.body.left == computer.screen.left - 1 && computer.body.bottom == computer.screen.bottom + 1 && computer.cameraRadius == 0,
        "A computer has a one pixel frame and no camera");
    Require(inside(computer.body, area) && centered(computer.body, area), "The computer is inside the area and centered");

    // A tablet and a phone have a wide dark bezel and a camera dot on the left while they lie.
    const kb::editor::EditorGameViewFrame tablet = EditorGameView::Fit(area, EditorGameViewDevice::Tablet, false);
    Require(std::fabs(aspect(tablet.screen) - 1544.0 / 907.0) < 0.02, "A tablet's screen keeps the shape of its resolution");
    Require(tablet.screen.left - tablet.body.left > tablet.rim + 15 && tablet.body.right - tablet.screen.right > tablet.rim + 15,
        "A tablet has a wide bezel");
    Require(inside(tablet.body, area) && centered(tablet.body, area), "The tablet is inside the area and centered");
    Require(tablet.cameraRadius > 0 && tablet.camera.x > tablet.body.left && tablet.camera.x < tablet.screen.left &&
            std::abs(tablet.camera.y - (tablet.screen.top + tablet.screen.bottom) / 2) <= 1,
        "A lying tablet carries its camera in the left bezel");
    const kb::editor::EditorGameViewFrame phone = EditorGameView::Fit(area, EditorGameViewDevice::Phone, false);
    Require(std::fabs(aspect(phone.screen) - 2340.0 / 1080.0) < 0.02, "A phone's screen keeps the shape of its resolution");
    const kb::editor::EditorGameViewFrame standing = EditorGameView::Fit(area, EditorGameViewDevice::Phone, true);
    Require(std::fabs(aspect(standing.screen) - 1080.0 / 2340.0) < 0.02, "A standing phone's screen is tall");
    Require(Height(standing.body) <= Height(area) && inside(standing.body, area), "A standing phone fits the height of the area");
    Require(standing.camera.y > standing.body.top && standing.camera.y < standing.screen.top &&
            std::abs(standing.camera.x - (standing.screen.left + standing.screen.right) / 2) <= 1,
        "A standing phone carries its camera in the top bezel");

    // The scale the bar reads follows the screen.
    Require(EditorGameView::FormatScale(static_cast<std::uint32_t>(Width(computer.screen)), 1920U) ==
            std::to_string(std::lround(100.0 * Width(computer.screen) / 1920.0)) + "%",
        "The scale is the screen's width over the render width");

    // The picture never disappears while the area can hold one, and an area that cannot gives nothing.
    for (const EditorGameViewDevice device : EditorGameView::kDevices) {
        for (const bool portrait : { false, true }) {
            for (const long size : { 150L, 400L, 1300L, 3000L }) {
                for (const RECT shape : { RECT{ 5, 7, 5 + size, 7 + size }, RECT{ 0, 0, size * 2, size }, RECT{ 0, 0, size, size * 2 } }) {
                    const kb::editor::EditorGameViewFrame frame = EditorGameView::Fit(shape, device, portrait);
                    Require(Width(frame.screen) > 0 && Height(frame.screen) > 0, "A picture is shown in an area that can hold one");
                    Require(inside(frame.body, shape), "A device stays inside its area");
                }
            }
        }
    }
    const kb::editor::EditorGameViewFrame none = EditorGameView::Fit(RECT{ 0, 0, 12, 12 }, EditorGameViewDevice::Tablet, false);
    Require(Width(none.screen) == 0 && Height(none.screen) == 0, "An area too small for a device has no screen");
}

void RunPreferencePersistenceTest() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    const std::filesystem::path root = TempRoot();
    const std::filesystem::path path = kb::editor::EditorConfigurationStore::FilePath(root);
    std::filesystem::create_directories(root / "Config", error);

    // Nothing stored: Play opens in the Game View on a computer, without the outline.
    {
        const auto result = kb::editor::EditorConfigurationStore::Load(path, root);
        Require(!result.found && result.configuration.play.target == kb::editor::EditorPlayTarget::GameView,
            "Play should default to the Game View");
        Require(result.configuration.play.gameViewDevice == EditorGameViewDevice::Computer && !result.configuration.play.gameViewPortrait &&
                !result.configuration.play.gameViewSafeArea,
            "The Game View should default to a lying computer without the safe area");
    }

    kb::editor::EditorConfiguration written;
    written.play.target = kb::editor::EditorPlayTarget::SceneView;
    written.play.gameViewDevice = EditorGameViewDevice::Tablet;
    written.play.gameViewPortrait = true;
    written.play.gameViewSafeArea = true;
    std::string saveError;
    Require(kb::editor::EditorConfigurationStore::Save(path, root, written, saveError), "Editor settings should save");
    {
        const auto loaded = kb::editor::EditorConfigurationStore::Load(path, root);
        Require(loaded.Succeeded() && loaded.found, "Saved editor settings should load");
        Require(loaded.configuration.play.target == kb::editor::EditorPlayTarget::SceneView,
            "Playing in the Scene View should be kept");
        Require(loaded.configuration.play.gameViewDevice == EditorGameViewDevice::Tablet && loaded.configuration.play.gameViewPortrait &&
                loaded.configuration.play.gameViewSafeArea,
            "The Game View device, its orientation and the safe area should be kept");
    }
    for (const EditorGameViewDevice device : EditorGameView::kDevices) {
        written.play.target = kb::editor::EditorPlayTarget::GameView;
        written.play.gameViewDevice = device;
        Require(kb::editor::EditorConfigurationStore::Save(path, root, written, saveError), "Editor settings should save again");
        const auto loaded = kb::editor::EditorConfigurationStore::Load(path, root);
        Require(loaded.configuration.play.target == kb::editor::EditorPlayTarget::GameView &&
                loaded.configuration.play.gameViewDevice == device,
            "Every Game View device should survive a save");
    }

    // A file written before the Game View showed devices, and one with a name this build does not know.
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "[Editor.Play]\nTarget=2\nGameViewResolution=Nonsense\nGameViewDevice=Nonsense\n";
    }
    const auto older = kb::editor::EditorConfigurationStore::Load(path, root);
    Require(older.configuration.play.target == kb::editor::EditorPlayTarget::Standalone,
        "A stored play place from an older editor should still mean the same place");
    Require(older.configuration.play.gameViewDevice == EditorGameViewDevice::Computer,
        "An unknown device name should fall back to a computer");
    std::filesystem::remove_all(TempRoot(), error);
}

[[nodiscard]] std::uint64_t NowMilliseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

void RunViewportMessageTest() {
    using kb::editor::EditorConsoleState;
    using kb::editor::EditorViewportMessage;
    using kb::editor::EditorViewportMessages;
    const auto glyphs = [](const std::string& text) {
        return static_cast<std::size_t>(std::ranges::count_if(text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
    };
    const std::string ellipsis = "\xE2\x80\xA6";

    // Wrapping: at spaces, at most `columns` characters and `maxLines` lines, the rest ending in an ellipsis.
    Require(EditorViewportMessages::Wrap("Lighting needs a rebuild", 40U, 6U) == std::vector<std::string>{ "Lighting needs a rebuild" },
        "A short message stays on one line");
    Require(EditorViewportMessages::Wrap("one two three four", 9U, 6U) == (std::vector<std::string>{ "one two", "three", "four" }),
        "A long message breaks at spaces");
    Require(EditorViewportMessages::Wrap("abcdefghij", 4U, 6U) == (std::vector<std::string>{ "abcd", "efgh", "ij" }),
        "A word longer than a line is cut where the line ends");
    Require(EditorViewportMessages::Wrap("first\nsecond", 20U, 6U) == (std::vector<std::string>{ "first", "second" }),
        "A line break in the text is kept");
    Require(EditorViewportMessages::Wrap("   ", 20U, 6U).empty() && EditorViewportMessages::Wrap("", 20U, 6U).empty(),
        "A message without text has no lines");
    const std::vector<std::string> cut = EditorViewportMessages::Wrap("a b c d e f g h", 1U, 6U);
    Require(cut.size() == 6U && cut.back() == ellipsis, "Lines beyond the sixth end in an ellipsis");
    // Polish words spelled in UTF-8 bytes, so the test does not depend on how this file is encoded.
    const std::string first = "za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87";
    const std::string second = "g\xC4\x99\xC5\x9Bl\xC4\x85";
    const std::string third = "ja\xC5\xBA\xC5\x84";
    const std::vector<std::string> polish = EditorViewportMessages::Wrap(
        first + " " + second + " " + third + " " + first + " " + second + " " + third, 8U, 2U);
    Require(polish.size() == 2U && polish[0] == first && glyphs(polish[1]) <= 8U && polish[1].ends_with(ellipsis),
        "Text in other languages is cut between characters, never inside one");
    for (const std::string& line : EditorViewportMessages::Wrap(std::string(500U, 'x'), 63U, EditorViewportMessages::kMaxLines)) {
        Require(glyphs(line) <= 63U, "No line is longer than the columns");
    }

    // Which messages are shown: warnings and errors, the newest three, while they are young.
    EditorConsoleState console;
    console.Info("test", "information");
    console.Warning("test", "first warning");
    console.Error("test", "an error");
    console.Warning("test", "second warning");
    console.Warning("test", "third warning");
    const std::uint64_t now = NowMilliseconds();
    const std::vector<EditorViewportMessage> recent = EditorViewportMessages::Recent(console, now + 100U);
    Require(recent.size() == EditorViewportMessages::kMaxShown && recent[0].text == "an error" && recent[2].text == "third warning" &&
            recent[0].level == kb::editor::EditorConsoleLevel::Error,
        "The newest three warnings and errors are shown, oldest first, without information");
    Require(EditorViewportMessages::Recent(console, now + EditorViewportMessages::kShownMilliseconds + 2000U).empty(),
        "A message goes away after a few seconds");

    // The plates: a border and a dark plate each, below one another, inside the width and the line limit.
    const std::string longText = "The lighting of this scene was built for an older version of its objects and needs to be built again before "
                                 "the baked light shows the objects where they are now. Choose World, Build Lighting. Light that is not built "
                                 "again keeps showing the old arrangement, so shadows and bounces may lie where the objects used to stand. "
                                 "Until then, anything that reads the baked light, such as reflections or the light on moving objects, is wrong too.";
    const std::vector<EditorViewportMessage> messages{
        EditorViewportMessage{ kb::editor::EditorConsoleLevel::Warning, longText },
        EditorViewportMessage{ kb::editor::EditorConsoleLevel::Error, "Script failed" },
    };
    std::vector<kb::editor::EditorSceneViewportTextLabel> labels;
    std::vector<kb::editor::EditorSceneViewportOverlayShape> shapes;
    EditorViewportMessages::Append(labels, shapes, messages, 0.0F, 0.0F);
    Require(shapes.size() == 4U, "Every message is a border and a plate");
    Require(shapes[0].points[1][0] - shapes[0].points[0][0] <= EditorViewportMessages::kMaxWidth, "A plate is at most 460 pixels wide");
    Require(shapes[0].points[0][0] > 0.0F && shapes[0].points[0][1] > 0.0F, "The plates start in the corner, with a margin");
    std::size_t firstPlateLines = 0U;
    for (const auto& label : labels) {
        if (label.y < shapes[0].points[1][1]) ++firstPlateLines;
    }
    Require(firstPlateLines == EditorViewportMessages::kMaxLines && labels[firstPlateLines - 1U].text.ends_with(ellipsis),
        "A long message fills six lines and ends in an ellipsis");
    Require(shapes[2].points[0][1] > shapes[0].points[1][1] && shapes[2].points[0][0] == shapes[0].points[0][0],
        "The next message sits under the first one");
    Require(shapes[0].color[0] > shapes[0].color[2] && shapes[2].color[0] > 200U && shapes[2].color[1] < 100U,
        "An error has a red border and a warning an amber one");
    Require(shapes[1].color[3] == 255U && shapes[1].color[0] < 40U, "The plate itself is dark and opaque");
    for (const auto& label : labels) {
        Require(label.x >= shapes[0].points[0][0] && label.x < shapes[0].points[1][0], "The text starts inside its plate");
    }
}

void RunPlayInputFocusTest() {
    kb::editor::EditorPlayModeState play;
    play.SetGameHasInput(true);
    Require(!play.GameHasInput(), "The game has no input while Play is stopped");
    play.Play();
    play.SetGameHasInput(true);
    Require(play.GameHasInput(), "The game takes the input when the Game View is clicked");
    play.SetGameHasInput(false);
    Require(!play.GameHasInput(), "A press elsewhere returns the input to the editor");
    play.SetGameHasInput(true);
    play.Stop();
    Require(!play.GameHasInput(), "Stopping returns the input to the editor");
}

void RunGameViewPanelDockingTest() {
    kb::editor::EditorDockModel model;
    const kb::editor::EditorMetrics metrics{};
    const auto layoutOf = [&]() {
        return model.Queries().BuildLayout(1280, 720, metrics.menuHeight, metrics.statusBarHeight, metrics.tabStripHeight,
            metrics.tabMinWidth, metrics.tabWidth, metrics.splitterSize);
    };
    const kb::editor::DockPanel* game = nullptr;
    for (const kb::editor::DockPanel& panel : model.Queries().Panels()) {
        if (panel.kind == kb::editor::DockPanelKind::Game) {
            game = &panel;
        }
    }
    Require(game != nullptr, "The default workspace has a Game View panel");
    Require(!game->visible && !game->detachable, "The Game View starts closed and stays in the main window");
    const std::uint32_t gameId = game->id;
    const auto find = [](const kb::editor::DockLayout& layout, std::uint32_t id) -> const kb::editor::DockPanelLayout* {
        for (const kb::editor::DockPanelLayout& panel : layout.panels) {
            if (panel.panelId == id) {
                return &panel;
            }
        }
        return nullptr;
    };
    Require(find(layoutOf(), gameId) == nullptr, "A closed Game View has no tab");

    Require(model.Commands().ActivatePanelKind(kb::editor::DockPanelKind::Game, kb::editor::DockArea::Center),
        "Play can open the Game View");
    const kb::editor::DockLayout opened = layoutOf();
    const kb::editor::DockPanelLayout* gameTab = find(opened, gameId);
    const kb::editor::DockPanelLayout* sceneTab = find(opened, 2U);
    Require(gameTab != nullptr && sceneTab != nullptr, "Both the Game View and the Scene View are docked");
    Require(gameTab->leafId == sceneTab->leafId, "The Game View opens as a tab beside the Scene View");
    Require(gameTab->active && !sceneTab->active, "The Game View comes to the front");

    // Stop brings the Scene View back to the front.
    model.Commands().ActivatePanel(2U);
    const kb::editor::DockLayout back = layoutOf();
    Require(find(back, 2U) != nullptr && find(back, 2U)->active, "The Scene View comes back to the front");
    Require(find(back, gameId) != nullptr, "The Game View stays docked as a tab");
}

} // namespace

namespace kb::editor::tests {

void RunEditorGameViewTests() {
    RunDeviceTableTest();
    RunSafeAreaTest();
    RunDeviceFitTest();
    RunPreferencePersistenceTest();
    RunViewportMessageTest();
    RunPlayInputFocusTest();
    RunGameViewPanelDockingTest();
}

} // namespace kb::editor::tests
