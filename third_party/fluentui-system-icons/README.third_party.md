# Fluent UI System Icons

- Repository: https://github.com/microsoft/fluentui-system-icons
- Revision: `main` as fetched 2026-08-29
- License: MIT (Copyright (c) 2020 Microsoft Corporation)
- License file: `third_party/fluentui-system-icons/LICENSE`

Vendored subset used by the Win32 editor:

- `assets/Code/SVG/ic_fluent_code_24_filled.svg`
- `assets/Server/SVG/ic_fluent_server_24_filled.svg`

The editor embeds the selected SVG path data in read-only code data and renders
it as vector paths at runtime. The source fill is dropped so the glyph takes the
colour the panel draws it in. The SVG files stay in `third_party` as the
license/audit source for every embedded path.
