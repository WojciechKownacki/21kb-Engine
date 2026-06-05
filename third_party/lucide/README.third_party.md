# Lucide

- Repository: https://github.com/lucide-icons/lucide
- Source used: `gamepad-2`, plus existing editor transport glyphs copied from LuizEngine's embedded Lucide catalogue
- License: ISC
- License file: `third_party/lucide/LICENSE`

Vendored subset used by the Win32 editor:

- `icons/play.svg`
- `icons/pause.svg`
- `icons/step-forward.svg`
- `icons/square.svg`
- `icons/gamepad-2.svg`

The editor embeds the selected SVG path data in read-only code data and renders
it as vector paths at runtime. The SVG files stay in `third_party` as the
license/audit source for every embedded path.
