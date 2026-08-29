# devicon

- Repository: https://github.com/devicons/devicon
- Revision: `v2.17.0`
- License: MIT (Copyright (c) 2015 konpa)
- License file: `third_party/devicon/LICENSE`

Vendored subset used by the Win32 editor's build target list:

- `icons/windows8/windows8-original.svg`
- `icons/android/android-plain.svg`
- `icons/linux/linux-plain.svg`

These are brand marks. They are used to name a build target, which is what the
marks are for; no endorsement is implied and the shapes are not altered. The
Android glyph is drawn in the current brand green rather than the older green
the file carries, and Tux is drawn as a single tone so it reads on a dark panel.

The editor embeds the selected SVG path data in read-only code data and renders
it as vector paths at runtime. The SVG files stay in `third_party` as the
license/audit source for every embedded path.
