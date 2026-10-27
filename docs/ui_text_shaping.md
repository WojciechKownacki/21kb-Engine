# UI text shaping

Runtime UI text uses HarfBuzz for OpenType glyph selection, joining, positioning
and ligatures, and SheenBidi for paragraph direction and script runs. Fonts still
come from imported assets, including immutable cooked packages. The rasterizer
packs the resulting glyph IDs rather than assuming one glyph per codepoint.

Line wrapping keeps shaping clusters intact and reshapes each line. Direction
comes from the original paragraph, while cursive joining stops at the line edge.
Rich-text color boundaries prevent a ligature from consuming another style's
characters. A style boundary inside a grapheme retains the base character's
style so an accent or joined sequence cannot be separated by markup.
Combining marks use the shaper's offsets and do not acquire extra
character spacing inside their cluster. Later labels growing a shared atlas
refresh earlier labels' UV coordinates before submission.

Caret geometry retains logical UTF-8 offsets independently of visual glyph
order. RTL starts appear on the right, combining sequences retain grapheme
boundaries, and ligatures expose insertion points between their characters.
Fonts without separately exposed ligature caret positions use equal divisions
of the cluster advance. Empty trailing lines retain a caret position.

Input fields consume the last rendered layout for visual left/right navigation,
line navigation, Home/End and pointer selection. Shift extends a selection and
Control+A selects the field. Typing, Backspace and Delete replace or remove the
logical selection without splitting grapheme clusters. Layout feedback for old
contents is ignored. An empty focused field still draws its caret. Android text
snapshots preserve both selection endpoints across UTF-16 conversion.

Windows runtime fields and editor Play mode support Control+C/X/V through the
native Unicode clipboard. Copying a password or PIN is refused; read-only fields
allow copying but reject replacement. Paste uses the same validation and character
limits as typing, preserving the suffix and retaining a selection when every
pasted character is rejected. Editor text controls also transfer UTF-8 through
the Unicode clipboard instead of discarding non-ASCII characters.

Shaping results use an LRU cache per font asset, pixel size and load generation,
limited to 32 entries and 64 KiB of vector payload per font instance. Container
metadata and glyph atlas storage are separate. Text, line range, style boundaries
and scale participate in matching. Reloading the font invalidates its entries;
releasing the scene releases its cache.

Clipboard and IME composition integration still depend on the input host. Font
fallback and variable-font axis controls are separate capabilities; an imported
font must cover the text.

Validation:

- `kb_ui_text_shaping` checks Arabic contextual forms, Hebrew and mixed-direction
  order, paragraph/line boundaries, ligatures, combining marks and invalid inputs.
- `kb_renderer_tests screen-ui-batch` checks actual font preparation, cluster
  wrapping, rich-text colors, caret geometry, partial ligature selections, empty
  input fields and shared atlas updates.
- `kb_engine_tests scene-ui` checks logical selection edits, visual RTL movement,
  pointer selection, multiline navigation and stale layout rejection.
- `kb_win32_clipboard` checks native Unicode transfers and runtime cut/copy/paste
  on an isolated, noninteractive window station with its own clipboard.
- `kb_game_ui_visual_package` checks cooked nested masks, shadow/outline ordering,
  long-label ellipsis and a blurred world edge at four resolutions. It also resizes
  the hidden native host through portrait and landscape while rendering, then checks
  GPU pixels and recorded viewport dimensions. Native DPI transitions and hardware
  overdraw counters are not covered by this test.
- `kb_game_text_shaping_package` cooks a project, makes its source unavailable,
  and runs the shipped player. A separate instrumented pass reads the same
  immutable package and compares GPU pixels against explicit Unicode reference
  forms for Arabic, mixed-direction text, a Latin ligature and an accent. The
  shipped player continues to reject development capture switches.
