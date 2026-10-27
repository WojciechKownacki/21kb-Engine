# UI text shaping

Runtime UI text uses HarfBuzz for OpenType glyph selection, joining, positioning
and ligatures, and SheenBidi for paragraph direction and script runs. Fonts still
come from imported assets, including immutable cooked packages. The rasterizer
packs the resulting glyph IDs rather than assuming one glyph per codepoint.

Line wrapping keeps shaping clusters intact and reshapes each line. Direction
comes from the original paragraph, while cursive joining stops at the line edge.
Rich-text color boundaries prevent a ligature from consuming another style's
characters. Combining marks use the shaper's offsets and do not acquire extra
character spacing inside their cluster. Later labels growing a shared atlas
refresh earlier labels' UV coordinates before submission.

Caret geometry retains logical UTF-8 offsets independently of visual glyph
order. RTL starts appear on the right, combining sequences retain grapheme
boundaries, and ligatures expose insertion points between their characters.
Fonts without separately exposed ligature caret positions use equal divisions
of the cluster advance. Empty trailing lines retain a caret position.

This rendering support does not by itself add visual arrow navigation, selection,
clipboard or IME composition to every input host. Font fallback and variable-font
axis controls are separate capabilities; an imported font must cover the text.

Validation:

- `kb_ui_text_shaping` checks Arabic contextual forms, Hebrew and mixed-direction
  order, paragraph/line boundaries, ligatures, combining marks and invalid inputs.
- `kb_renderer_tests screen-ui-batch` checks actual font preparation, cluster
  wrapping, rich-text colors, caret geometry and shared atlas updates.
- `kb_game_text_shaping_package` cooks a project, makes its source unavailable,
  and runs the shipped player. A separate instrumented pass reads the same
  immutable package and compares GPU pixels against explicit Unicode reference
  forms for Arabic, mixed-direction text, a Latin ligature and an accent. The
  shipped player continues to reject development capture switches.
