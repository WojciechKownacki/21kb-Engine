# Engine Library support matrix

This matrix describes support only where CI executes the named target. A cell
without a target is intentionally not a support claim.

| Platform | Runtime/backend scope | Frontends exercised | CI targets |
| --- | --- | --- | --- |
| Windows / MSVC | Engine, renderer and Win32 editor | Native, Lua, Visual Graph | `kb_engine_library_tests`, `kb_engine_tests`, `kb_renderer_tests`, `kb_editor_tests`, `kb_editor_material_graph_canvas_tests` |
| Linux / GCC ASan+UBSan | Headless engine runtime | Native, Lua, Visual Graph | `kb_engine_library_tests`, `kb_engine_tests` |
| macOS / Clang ASan+UBSan | Headless engine runtime | Native, Lua, Visual Graph | `kb_engine_library_tests`, `kb_engine_tests` |

`kb_engine_library_tests` verifies the catalog and parity of Native, Lua and
Visual Graph entry points. The CI workflow builds this target in every row;
the renderer/editor targets are Windows-only and must not be described as
Linux or macOS support until a corresponding CI row builds and tests them.

When adding a platform, rendering backend, or frontend, update this table and
the matching CI target list in the same change. A claimed production cell must
name a test that actually executes on that row.
