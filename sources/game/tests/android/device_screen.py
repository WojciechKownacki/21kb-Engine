"""Screen captures of the display the app runs on, for the Android device suites."""
import re
import time

from png_readback import read_png


def default_display(adb):
    """The physical id of the display behind the default display, or None where the device has one display.

    `screencap` without `-d` on a device with several displays picks one itself and prints a warning in
    front of the PNG.
    """
    displays = re.findall(r"^Display (\d+) ", adb("shell", "dumpsys", "SurfaceFlinger", "--display-id"), re.M)
    if len(displays) < 2:
        return None
    found = re.search(r"DisplayViewport\{[^}]*?\bdisplayId=0, uniqueId='local:(\d+)'", adb("shell", "dumpsys", "display"))
    if not found or found[1] not in displays:
        raise RuntimeError("Cannot tell which physical display shows the default display")
    return found[1]


def capture(adb, path, display, timeout=15):
    """Writes the first screen capture whose central area is not black to `path` and returns its pixels.

    A frame the app has just logged may not be composited yet, so the capture is repeated until it is.
    """
    deadline = time.monotonic() + timeout
    while True:
        path.write_bytes(adb("exec-out", "screencap", *(["-d", display] if display else []), "-p", binary=True))
        pixels = read_png(path)
        # System bars at the edges are drawn before the app's surface is.
        height, width = len(pixels), len(pixels[0])
        if any(max(pixels[y][x][:3]) > 16 for y in range(height // 10, height * 9 // 10, 8)
                for x in range(width // 10, width * 9 // 10, 8)):
            return pixels
        if time.monotonic() > deadline:
            raise RuntimeError("The screen stayed black in " + path.name)
        time.sleep(0.5)
