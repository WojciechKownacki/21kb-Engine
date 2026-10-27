"""Verify browser safe areas and docked keyboard overlays in a cooked native UI."""
from run_game_text_tests import TextScenario, author_fixture
from run_game_touch_tests import main, require


class SafeAreaScenario(TextScenario):
    def run(self, targets):
        client = self.client
        self.wait("native field ready", lambda value: value["ready"])
        client.call("Emulation.setDeviceMetricsOverride", width=800, height=600,
            deviceScaleFactor=1, mobile=False)
        self.paint()
        self.geometry()
        self.field_picture("initial")
        client.call("Emulation.setSafeAreaInsetsOverride",
            insets=dict(left=50, top=44, right=30, bottom=34))
        self.paint()
        # Observe the native draw, not the hidden CSS measurement element.
        pixels = self.picture("cutout", [[60, 380], [110, 380], [110, 350]])
        require(pixels[0][2] < 50 and pixels[1][2] > pixels[1][0] + 50 and pixels[2][2] < 50,
            "CSS cutout insets did not move the native field: " + str(pixels))
        self.click(140, 390)
        self.wait("inset field hit test", lambda value: value["focused"] == value["entry"])
        client.call("Input.insertText", text="Safe")
        self.wait("inset field editing", lambda value: value["text"] == "Safe")
        client.call("Emulation.setVirtualKeyboardGeometryOverride",
            keyboardRect=dict(x=0, y=280, width=800, height=320))
        self.wait("overlay keyboard reveals focus without resizing canvas",
            lambda value: value["scrollY"] >= 163.9)
        require(self.geometry()["height"] == 600, "Overlay unexpectedly resized the backbuffer")
        pixels = self.picture("keyboard", [[110, 260], [110, 300]])
        require(pixels[0][2] > pixels[0][0] + 50 and pixels[1][2] < 50,
            "Focused field is hidden by the overlay keyboard: " + str(pixels))
        self.click(140, 230)
        client.call("Input.insertText", text="!")
        self.wait("revealed field editing", lambda value: "!" in value["text"])
        client.call("Emulation.setVirtualKeyboardGeometryOverride")
        client.call("Emulation.setSafeAreaInsetsOverride", insets={})
        self.paint()
        # Embedded canvases already outside a cutout must not receive it twice.
        client.evaluate("Object.assign(document.querySelector('#canvas').style,"
            "{position:'absolute',left:'100px',top:'60px',width:'600px',height:'500px'})")
        client.call("Emulation.setSafeAreaInsetsOverride",
            insets=dict(left=50, top=44, right=30, bottom=34))
        self.paint()
        self.geometry()
        pixels = self.picture("embedded", [[60, 380]])
        require(pixels[0][2] > pixels[0][0] + 50, "Embedded canvas double-counted cutout insets")
        client.evaluate("Object.assign(document.querySelector('#canvas').style,"
            "{left:'0',top:'0',width:'100vw',height:'100vh'})")
        client.call("Emulation.setDeviceMetricsOverride", width=600, height=800,
            deviceScaleFactor=2, mobile=False)
        self.paint()
        self.geometry()
        pixels = self.picture("portrait-dpi", [[160, 420], [160, 350]])
        require(pixels[0][2] > pixels[0][0] + 50 and pixels[1][2] < 50,
            "Portrait cutout did not scale CSS pixels to native pixels: " + str(pixels))
        self.report.update(graphics=self.graphics(), finalLayout=dict(self.layout))


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario_type=SafeAreaScenario,
        schema="21kb.web-game-safe-area-tests/v1", prefix="GameSafeArea", description=__doc__))
