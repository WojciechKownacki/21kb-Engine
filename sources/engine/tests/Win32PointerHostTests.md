# Windows pointer and keyboard host test

Build and run `kb_win32_pointer_host_tests` in an interactive desktop session.
This test is not registered with unattended CTest because it temporarily brings
its own window to the foreground, injects input into that window and uses
Alt-Tab. It restores the original cursor position, keyboard layout and foreground
window afterward. Other input during the test may cause it to stop.

The test checks native `WM_INPUT` delivery, accumulation and single-frame
consumption of relative mouse counts, suppression of cursor-warp motion, four
viewport scale mappings, two keyboard layouts, Alt-Tab release and focus
recovery. It also checks that a second host cannot replace the first host's raw
mouse registration. Viewport scale tests do not change physical monitor DPI.

Hosts own `Win32RawMouseRegistration`. Registration preserves ordinary mouse
messages and does not request background input. The shared collector reads
`WM_INPUT` before the host calls `DefWindowProc`. Relative counts are independent
of cursor acceleration and viewport scaling; absolute coordinates continue to
use the window's client-to-render mapping. Absolute devices retain cursor-based
movement. Losing focus discards pending relative movement.
