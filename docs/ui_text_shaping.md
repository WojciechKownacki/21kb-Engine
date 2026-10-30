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

Rich text supports `<link=target>text</link>` on selectable text elements.
Targets are nonempty printable ASCII identifiers of at most 255 bytes; they
are delivered as `event.args.text` to `OnUILinkActivated`, and are never opened
automatically. Nested links restore the outer target. A pointer must press and
release within the same link; clipping and transformed text geometry apply.
Link markup is literal in input fields. Unrecognized tags, malformed tag syntax
and unmatched closing tags remain literal; an unclosed valid opening tag applies
through the rest of the text (or the current table cell). Invalid UTF-8 fails
parsing without producing partial text. A stale layout for changed text cannot
activate a link. `<color=#RRGGBB>` and `<color=#RRGGBBAA>` set nested colors.
`<u>text</u>` underlines each laid-out line in the current text color; it nests
with colors and links and follows the element's transform and clipping.

Non-editable rich text also accepts `<table><tr><td>text</td><td>text</td></tr></table>`.
Tables are blocks with equal-width columns; rows must have the same number of
cells. Cells may contain colors, links, underlines and `<br>` or `<br/>` line
breaks. A table supports up to 64 rows, 64 columns and 1024 cells. Nested tables,
attributes and merged cells are not supported. An invalid or incomplete table
is shown literally. Styles inherited from outside the table apply inside cells;
styles opened inside a cell do not escape it. Wrapping and text overflow settings
apply to the cell width; overflow can extend into an adjacent cell. Input fields
retain table markup as literal text.

Caret geometry retains logical UTF-8 offsets independently of visual glyph
order. RTL starts appear on the right, combining sequences retain grapheme
boundaries, and ligatures expose insertion points between their characters.
Fonts with GDEF ligature caret positions use those positions for insertion and
selection. Fonts without suitable positions use equal grapheme divisions of the
cluster advance. Internal shaping boundaries within an Indic grapheme are not
published as caret stops. Empty trailing lines retain a caret position.

Input fields consume the last rendered layout for visual left/right navigation,
line navigation, Home/End and pointer selection. Shift extends a selection and
Control+A selects the field. Typing, Backspace and Delete replace or remove the
logical selection without splitting grapheme clusters. Layout feedback for old
contents is ignored. An empty focused field still draws its caret. Android text
snapshots preserve both selection endpoints across UTF-16 conversion.

Committed browser text enters the input snapshot as a complete transaction, with
room for 1024 Unicode scalars. Invalid or oversized transactions leave the snapshot
unchanged. Field storage and character limits reject an entire incoming grapheme
when it does not fit, including its combining marks and joined emoji sequence.

Windows keeps uncommitted IME text in the native composition window. Candidate
navigation does not move runtime focus, and committed results are bound to the
field and scene that started composition. Changing field or losing window focus
cancels pending text. The native candidate window follows the field viewport in
both the standalone player and editor Play mode.

The Android activity observes selection changes in GameTextInput's editable so
selection-only IME operations reach the native snapshot as well as text edits.
It preserves both UTF-16 endpoints and the composing region; provisional text
remains excluded from runtime field updates.
Opening a multiline standard field configures the Android IME for line breaks;
single-line fields retain the Done action.

Windows runtime fields and editor Play mode support Control+C/X/V through the
native Unicode clipboard. Copying a password or PIN is refused; read-only fields
allow copying but reject replacement. Paste uses the same validation and character
limits as typing, preserving the suffix and retaining a selection when every
pasted character is rejected. Editor text controls also transfer UTF-8 through
the Unicode clipboard instead of discarding non-ASCII characters.

`UIText.fontVariations` accepts comma-separated OpenType axis settings, for
example `wght=450,wdth=90`. Each axis must exist in the font and remain within
its declared range. Shaping and glyph rasterization use the same coordinates;
different settings have separate atlas and shaping caches. Unknown, duplicate,
malformed or out-of-range axes make that text unavailable and report a text
preparation failure. Empty settings use the font's defaults.

Shaping results use an LRU cache per font asset, variation settings, pixel size and load generation,
limited to 32 entries and 64 KiB of vector payload per font instance. Container
metadata and glyph atlas storage are separate. Text, line range, style boundaries
and scale participate in matching. Reloading the font invalidates its entries;
releasing the scene releases its cache.

Clipboard and IME composition integration still depend on the input host. Font
fallback is a separate capability; an imported font must cover the text.

The CLI's `entity add --create` accepts the UI preset names listed by `schema`,
including `Dropdown`, `Slider` and `Scroll View`. These call the same hierarchy
builders as the editor and scripts, preserving template, fill and scrollbar
references when saved and reloaded.

The native development player accepts `--input-replay=<recording>` together with
`--headless --frames=<count>`. It uses the existing input recording format and
the recorded delta for each tick. The recording must contain every requested
frame. Replay cannot be combined with fixed-step profiling or particle diagnostics;
the shipped executable refuses this development override.

Validation:

- `kb_ui_grapheme_conformance` checks Unicode 17.0.0 extended grapheme boundaries
  against the complete upstream corpus, including every UTF-8 byte offset for
  forward/backward movement, snapping and counts. Malformed UTF-8 bytes remain
  independent clusters. `python scripts/generate_grapheme_tables.py --check`
  verifies pinned source hashes and reproducible generated tables offline.
- `kb_ui_text_shaping` checks Arabic contextual forms, Hebrew and mixed-direction
  order, paragraph/line boundaries, ligatures, combining marks, Devanagari
  reordering/conjuncts, asymmetric GDEF caret positions and invalid inputs.
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
- `kb_renderer_ui_overdraw` measures actual GPU passing samples in the UI
  composite on a hidden D3D11 surface, without MSAA. Run
  `kb_renderer_tests screen-ui-overdraw` for a machine-readable report of sample
  count, batches and samples per render-target pixel. Four overlapping layers
  multiply fill work by four, half-width clipping halves it, shadows/outlines
  add work, and doubling target dimensions quadruples it. These are composite
  sample counts, not shader invocation counts or timings; blur prefilter passes
  and custom UI materials are outside this counter. Normal rendering allocates
  no queries. Diagnostic callers own and retire one query per submitted batch.
- `kb_game_ui_input_package` creates, reloads and removes all UI presets through
  the CLI, then exercises cooked buttons, toggles, sliders, scrollbars, scroll
  views, dropdowns and Unicode input. Recorded pointer, keyboard, controller and
  touch snapshots include disabled and canceled actions. This tests normalized
  input in the native runtime, not physical mobile or browser event delivery.
- `kb_game_text_shaping_package` cooks a project, makes its source unavailable,
  and runs the shipped player. A separate instrumented pass reads the same
  immutable package and compares GPU pixels against explicit Unicode reference
  forms for Arabic, mixed-direction text, a Latin ligature and an accent, plus
  explicit glyph references for three Devanagari sequences. Recorded edits verify
  visible selection and logical replacement for Arabic, Hebrew, mixed-direction
  text, Devanagari and an asymmetric GDEF ligature. The shipped player continues
  to reject development capture switches.

Localized text uses `localizationKey` to select a catalog entry. Set the optional
`localizationNumber` string to a decimal such as `"2"` or `"1.00"` to select a
plural form and replace `{count}`. Visible fraction digits are preserved for
plural rules. An empty number keeps ordinary translation. These properties are
available through the inspector, CLI and component property API; invalid numbers
are rejected without changing the component. Scene format 55 stores the number;
older scenes load with ordinary translation. Language changes update displayed
text without replacing the authored source text.

Catalog locale matching ignores ASCII case and accepts hyphen or underscore
separators. Regional tags fall back through their parents, then the catalog's
fallback language. Plurals use the language supplying the message. Catalogs reject
ambiguous equivalent locale declarations, malformed UTF-8, embedded NUL text and
repeated fallback directives. Lua `Localization.SetCatalog` accepts a nonnegative
integer or a hexadecimal asset ID string; use the string form for IDs above the
signed integer range. Declare dynamically selected catalogs with a Lua
`-- @asset /Game/Localization/Labels.kbloc` dependency so cooking includes them.

`kb_game_localization_package` verifies CLI-authored plural fields, live language
changes, regional and missing-key fallback, decimal preservation and full-width
Lua catalog IDs in a cooked player. Its GPU captures compare localized labels
with literal reference labels after the source project has been made unavailable.

Catalog schema 2 adds `resource "source-id" "localized-id"` entries inside each
language block. IDs use hexadecimal asset notation. Schema 1 text catalogs remain
readable. Resource lookup applies one replacement, using locale parents and then
the catalog fallback; absent mappings preserve the source ID. Source and target
must exist, have the same runtime asset type, and cannot be localization catalogs.
Imported replacements must also have the same import category. Catalogs can be
imported through the standard CLI or editor importer; malformed catalogs are
rejected and unchanged imports reuse their asset ID.
All mapped sources and targets are included in the catalog's cook dependencies.
UI fonts, images, raw images and sprites use these replacements on the display
frame. Authored scene references remain unchanged. Other consumers can call
`SceneLocalization::ResolveResource` or the registered
`Localization.ResolveResource` script function. Its Lua binding accepts a
nonnegative integer or hexadecimal ID string and returns a hexadecimal string
to preserve all 64 bits.

Open a localization catalog from Project Files to choose a language for the
editor scene preview in the 2D view. The menu marks the current language; cancel keeps the
previous preview. Preview changes request rendering without modifying the scene
document and are unavailable during play mode. The headless localization scenario
uses the same preview operation and compares viewport captures across languages.
