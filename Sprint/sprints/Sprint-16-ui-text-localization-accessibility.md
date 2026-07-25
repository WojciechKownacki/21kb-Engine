# Sprint 16 · UI / Text / Localization / Accessibility

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver a complete runtime and editor UI stack with predictable layout and rendering, robust input and focus, international text, localization, accessibility semantics, reusable authoring, testing, and scalable batching.

## UI Core & Widget Framework

- [ ] Add a retained-mode widget tree with a hierarchy
- [ ] Add widget lifecycle (construct, mount, update, unmount, destroy)
- [ ] Add parent/child slots and slot properties
- [ ] Add per-widget visibility modes (visible, hidden, collapsed, hit-test-invisible)
- [ ] Add enable and disable states
- [ ] Add render transform, opacity, and pivot per widget
- [ ] Add z-order and layering within a parent
- [ ] Add invalidation of layout and paint on change
- [ ] Add a dirty-tracking and rebuild pipeline
- [ ] Add widget identity and stable ids
- [ ] Add a widget registry and factory
- [ ] Add clipping and content culling
- [ ] Add hit-testing against the widget tree
- [ ] Add per-widget user data linking to gameplay
- [ ] Add a data-driven widget asset format
- [ ] Add widget asset versioning and migration
- [ ] Add a scripting API to build and query widgets
- [ ] Add widget-tree debug inspection

## Layout System

- [ ] Add a canvas panel with absolute positioning and anchors
- [ ] Add anchor points and offsets relative to the parent
- [ ] Add a horizontal and vertical box (stack) panel
- [ ] Add a grid panel with rows and columns
- [ ] Add a uniform grid panel
- [ ] Add a wrap panel that flows children
- [ ] Add an overlay panel that stacks children
- [ ] Add a scroll box with vertical and horizontal scrolling
- [ ] Add a size box with fixed and min/max sizing
- [ ] Add a spacer and separator
- [ ] Add a border and padding container
- [ ] Add margins, padding, and content alignment
- [ ] Add horizontal and vertical alignment per slot
- [ ] Add fill, auto, and fixed sizing rules
- [ ] Add aspect-ratio and constraint containers
- [ ] Add a two-pass measure-and-arrange layout
- [ ] Add flow direction (left-to-right and right-to-left)
- [ ] Add responsive breakpoints and adaptive layouts
- [ ] Add a safe-zone-aware layout container
- [ ] Add layout debug visualization

## UI Rendering & Draw

- [ ] Add a draw-element list per frame
- [ ] Add batching of draw elements by material and texture
- [ ] Add brushes for solid color, image, and nine-slice
- [ ] Add gradient and rounded-rectangle brushes
- [ ] Add custom material support on widgets
- [ ] Add clipping regions and scissor rectangles
- [ ] Add retained-mode caching of static sub-trees
- [ ] Add invalidation panels for partial redraw
- [ ] Add blur and backdrop effects
- [ ] Add per-widget custom draw callbacks
- [ ] Add render-target rendering of widgets
- [ ] Add draw-call and batch statistics
- [ ] Add pixel-snapping for crisp edges
- [ ] Add UI rendering debug overlay

## Input, Focus & Interaction

- [ ] Add mouse input (move, buttons, wheel, hover)
- [ ] Add touch input with multi-touch
- [ ] Add keyboard input and shortcuts
- [ ] Add gamepad navigation and activation
- [ ] Add a focus system with a focused-widget path
- [ ] Add directional navigation between widgets
- [ ] Add tab-order navigation
- [ ] Add event bubbling and tunneling
- [ ] Add pointer capture and release
- [ ] Add hover, press, and release states
- [ ] Add drag-and-drop with payloads and drop targets
- [ ] Add gesture recognition (tap, long-press, swipe, pinch)
- [ ] Add input routing between UI and gameplay
- [ ] Add input consumption and pass-through rules
- [ ] Add focus-visible indicators
- [ ] Add on-screen virtual cursor for gamepad
- [ ] Add repeat and hold input handling
- [ ] Add interaction debug visualization

## Widget Library — Common Controls

- [ ] Add a button with click and hold events
- [ ] Add a text label
- [ ] Add an image and icon widget
- [ ] Add a checkbox
- [ ] Add a radio button group
- [ ] Add a toggle switch
- [ ] Add a slider
- [ ] Add a range (dual-handle) slider
- [ ] Add a progress bar
- [ ] Add a spinner and busy indicator
- [ ] Add a single-line text field
- [ ] Add a multi-line text area
- [ ] Add a password field
- [ ] Add a numeric spin box
- [ ] Add a dropdown and combo box
- [ ] Add a list view with selection
- [ ] Add a tile and grid view
- [ ] Add a tree view
- [ ] Add a data table with columns
- [ ] Add a scrollbar
- [ ] Add a tab control
- [ ] Add an accordion and expander
- [ ] Add a menu bar and submenus
- [ ] Add a context menu
- [ ] Add a tooltip
- [ ] Add a modal dialog and message box
- [ ] Add a window and panel container
- [ ] Add a breadcrumb and pagination control

## Advanced & Composite Widgets

- [ ] Add a color picker
- [ ] Add a date and time picker
- [ ] Add a virtualized list for very large data sets
- [ ] Add a carousel and page viewer
- [ ] Add dockable and resizable panels
- [ ] Add a tree-table hybrid
- [ ] Add a property grid
- [ ] Add chart and graph widgets
- [ ] Add a minimap widget
- [ ] Add a radial and pie menu
- [ ] Add an on-screen virtual keyboard
- [ ] Add a rich item card widget
- [ ] Add a tag and chip input
- [ ] Add a split-view with draggable dividers

## Data Binding & View Models

- [ ] Add one-way data binding to widget properties
- [ ] Add two-way binding for input widgets
- [ ] Add view models separating data from presentation
- [ ] Add property-change notification
- [ ] Add collection and list binding with change tracking
- [ ] Add value converters and formatters in bindings
- [ ] Add command binding for actions
- [ ] Add binding to gameplay and entity data
- [ ] Add binding expressions and paths
- [ ] Add fallback and default values in bindings
- [ ] Add binding validation and error reporting
- [ ] Add lazy and batched binding updates
- [ ] Add debounced bindings for frequent changes
- [ ] Add a binding editor in the designer
- [ ] Add binding debug inspection
- [ ] Add compiled bindings for performance

## Styling & Theming

- [ ] Add reusable style assets for widgets
- [ ] Add per-state styles (normal, hover, pressed, disabled, focused)
- [ ] Add themes grouping styles and colors
- [ ] Add color schemes and palettes
- [ ] Add typography scales and text styles
- [ ] Add design tokens for spacing, radius, and color
- [ ] Add style inheritance and overrides
- [ ] Add light and dark theme variants
- [ ] Add runtime theme switching
- [ ] Add per-widget style overrides
- [ ] Add nine-slice and stateful brush styling
- [ ] Add a style editor with live preview
- [ ] Add high-contrast theme variants
- [ ] Add theme validation and coverage checks
- [ ] Add import and export of themes
- [ ] Add a default theme that looks good out of the box

## UI Animation & Transitions

- [ ] Add widget animation timelines
- [ ] Add property tweens (position, size, opacity, color, rotation)
- [ ] Add easing curves and custom curves
- [ ] Add enter and exit animations
- [ ] Add state-transition animations
- [ ] Add sequenced and parallel animations
- [ ] Add looping and ping-pong animations
- [ ] Add animation events and callbacks
- [ ] Add play, pause, reverse, and scrub control
- [ ] Add data-driven and scripted animations
- [ ] Add a timeline editor for widget animations
- [ ] Add reduced-motion respect in animations
- [ ] Add animation performance budgets
- [ ] Add animation preview in the designer

## Visual UI Designer

- [ ] Add a WYSIWYG design canvas
- [ ] Add drag-and-drop placement of widgets
- [ ] Add a hierarchy outliner of the widget tree
- [ ] Add a property panel for the selected widget
- [ ] Add live preview with sample data
- [ ] Add multi-resolution and device preview
- [ ] Add alignment, snapping, and distribution guides
- [ ] Add rulers, grid, and measurement overlays
- [ ] Add copy, paste, and duplicate of widgets
- [ ] Add undo and redo across design edits
- [ ] Add reusable component creation from a selection
- [ ] Add a component and template library
- [ ] Add a binding editor integrated in the designer
- [ ] Add a style and theme editor
- [ ] Add an animation timeline editor
- [ ] Add responsive-layout editing with breakpoints
- [ ] Add a preview of different cultures and text lengths
- [ ] Add an accessibility preview and checker
- [ ] Add zoom, pan, and isolation of sub-trees
- [ ] Add a widget palette with categories and search
- [ ] Add starter templates for menus, HUDs, and dialogs
- [ ] Add a gallery of example UIs to open and learn from

## UserWidget Authoring & Reusable Components

- [ ] Add user-defined composite widgets
- [ ] Add named content slots for injected children
- [ ] Add exposed properties on custom widgets
- [ ] Add exposed events and callbacks
- [ ] Add default values and property metadata
- [ ] Add nesting of custom widgets
- [ ] Add widget variants and overrides
- [ ] Add a reusable-component library
- [ ] Add instancing with per-instance overrides
- [ ] Add versioning and migration of custom widgets
- [ ] Add validation of component interfaces
- [ ] Add packaging and sharing of components
- [ ] Add live-reload of edited widgets
- [ ] Add documentation and previews for components

## UI Logic & Scripting

- [ ] Add event handlers bound to widget events
- [ ] Add binding of buttons and inputs to gameplay actions
- [ ] Add a scripting API for widget logic
- [ ] Add visual scripting nodes for UI logic
- [ ] Add UI state machines for screen flow
- [ ] Add conditions and data-driven visibility
- [ ] Add timers and delays in UI logic
- [ ] Add navigation and routing between screens
- [ ] Add form validation and submission logic
- [ ] Add localized dynamic content in logic
- [ ] Add UI-logic debugging and step-through
- [ ] Add deterministic UI logic for tests

## HUD & Game UI Layers

- [ ] Add a HUD system rendered over the game
- [ ] Add UI layers with ordering (background, HUD, menus, overlays, tooltips)
- [ ] Add a screen stack with push and pop
- [ ] Add modal and blocking-screen management
- [ ] Add screen transitions and fades
- [ ] Add world-to-screen markers and waypoints
- [ ] Add floating damage and status numbers
- [ ] Add health, resource, and status bars
- [ ] Add a minimap and radar
- [ ] Add notification and toast system
- [ ] Add an objective and quest tracker UI
- [ ] Add an inventory and menu framework
- [ ] Add pause-menu and settings-menu scaffolding
- [ ] Add per-player HUD for local multiplayer

## World-Space & 3D UI

- [ ] Add widgets rendered on surfaces in the world
- [ ] Add interaction with world-space widgets via rays and cursors
- [ ] Add curved and cylindrical UI surfaces
- [ ] Add depth, occlusion, and sorting for world UI
- [ ] Add billboard and face-camera UI
- [ ] Add XR and VR UI panels and pointers
- [ ] Add hand and controller interaction with UI
- [ ] Add gaze and dwell selection
- [ ] Add distance-based scaling and fading
- [ ] Add world-UI performance budgets
- [ ] Add stereo-correct UI rendering
- [ ] Add world-UI debug visualization

## UI Scaling, DPI & Safe Area

- [ ] Add DPI awareness and per-monitor scaling
- [ ] Add a reference resolution and scale rules
- [ ] Add resolution-independent layout units
- [ ] Add safe-area handling for notches and rounded corners
- [ ] Add TV overscan safe zones
- [ ] Add aspect-ratio adaptation
- [ ] Add a global UI scale setting
- [ ] Add per-widget scaling overrides
- [ ] Add crisp rendering across scales
- [ ] Add multi-display support
- [ ] Add orientation change handling
- [ ] Add scaling debug visualization

## Font Assets & Loading

- [ ] Add import of vector fonts
- [ ] Add font families with weights and styles
- [ ] Add signed-distance-field font generation
- [ ] Add multi-channel signed-distance-field fonts
- [ ] Add bitmap and pixel fonts
- [ ] Add a dynamic glyph atlas
- [ ] Add on-demand glyph rasterization
- [ ] Add font fallback chains for missing glyphs
- [ ] Add script-specific fallback fonts
- [ ] Add embedded and packaged fonts
- [ ] Add variable-font axis support
- [ ] Add font hinting and gamma settings
- [ ] Add font asset validation
- [ ] Add font memory budgets and eviction

## Text Layout & Shaping

- [ ] Add glyph shaping with kerning
- [ ] Add ligatures and contextual forms
- [ ] Add bidirectional text ordering
- [ ] Add complex-script shaping (Arabic, Indic, Thai)
- [ ] Add line breaking with language rules
- [ ] Add word wrap and character wrap
- [ ] Add justification and alignment
- [ ] Add tab stops and indentation
- [ ] Add letter and line spacing
- [ ] Add vertical text layout
- [ ] Add overflow handling (clip, ellipsis, scroll)
- [ ] Add auto-sizing and shrink-to-fit text
- [ ] Add mixed-font and mixed-size runs
- [ ] Add text measurement and metrics queries
- [ ] Add hit-testing from a point to a character
- [ ] Add caret and selection geometry from indices
- [ ] Add hyphenation
- [ ] Add layout caching for unchanged text

## Text Rendering & Effects

- [ ] Add glyph-atlas caching and packing
- [ ] Add signed-distance-field text rendering
- [ ] Add subpixel and grayscale antialiasing
- [ ] Add outlines and borders on text
- [ ] Add drop shadows and glow
- [ ] Add gradient and texture fill on text
- [ ] Add per-character color and opacity
- [ ] Add underline, strikethrough, and overline
- [ ] Add scaling without re-rasterization via distance fields
- [ ] Add crisp small-text rendering
- [ ] Add emoji and color-glyph rendering
- [ ] Add text material and shader customization
- [ ] Add text rendering budgets
- [ ] Add text rendering debug visualization

## Rich Text & Markup

- [ ] Add a rich-text markup language
- [ ] Add inline style spans (bold, italic, color, size)
- [ ] Add inline images and icons
- [ ] Add hyperlinks with click handling
- [ ] Add custom tags and decorators
- [ ] Add named text styles referenced in markup
- [ ] Add inline widgets embedded in text
- [ ] Add a typewriter and reveal effect
- [ ] Add animated and wavy text effects
- [ ] Add localized rich text with placeholders
- [ ] Add auto-linking of URLs and references
- [ ] Add markup validation and error reporting
- [ ] Add a rich-text editor preview
- [ ] Add sanitization of untrusted markup

## Text Input & Editing

- [ ] Add a caret with blinking and positioning
- [ ] Add text selection with mouse, touch, and keyboard
- [ ] Add keyboard navigation (word, line, document)
- [ ] Add input-method (IME) composition support
- [ ] Add clipboard cut, copy, and paste
- [ ] Add undo and redo of edits
- [ ] Add multi-line editing with scrolling
- [ ] Add auto-complete and suggestions
- [ ] Add input validation and masking
- [ ] Add character and length limits
- [ ] Add password masking with reveal
- [ ] Add placeholder and hint text
- [ ] Add on-screen and platform virtual keyboards
- [ ] Add rich-text editing controls
- [ ] Add spell-check hooks
- [ ] Add deterministic editing for tests

## Localization — String Tables & Keys

- [ ] Add localized string tables with keys
- [ ] Add namespaces and grouping of keys
- [ ] Add source-string gathering from content and code
- [ ] Add localized text references usable in widgets
- [ ] Add default and source-culture fallback text
- [ ] Add missing-translation detection and reporting
- [ ] Add key-collision detection
- [ ] Add context and comments for translators
- [ ] Add in-context localization editing
- [ ] Add stable keys that survive source-text changes
- [ ] Add per-key metadata (max length, do-not-translate)
- [ ] Add string-table import and merge
- [ ] Add string-table validation
- [ ] Add a localization dashboard with coverage

## Localization — Translation Pipeline

- [ ] Add extraction and gathering of translatable text
- [ ] Add export to standard translation formats
- [ ] Add import of translated files
- [ ] Add a translation-memory store
- [ ] Add reuse of prior translations for matches
- [ ] Add machine-translation hooks for drafts
- [ ] Add a review and approval workflow
- [ ] Add batch and incremental gathering
- [ ] Add per-target and per-platform packaging
- [ ] Add pluralization data in the pipeline
- [ ] Add screenshots and context for translators
- [ ] Add translation progress and status reporting
- [ ] Add conflict resolution on re-import
- [ ] Add pipeline validation and diagnostics

## Localization — Formatting & Culture

- [ ] Add culture and locale detection at startup
- [ ] Add explicit culture selection and switching
- [ ] Add culture fallback chains
- [ ] Add number formatting per culture
- [ ] Add date and time formatting per culture
- [ ] Add currency and percent formatting
- [ ] Add plural-rule selection per culture
- [ ] Add gendered and inflected text handling
- [ ] Add a message-format with named placeholders
- [ ] Add ordinal and cardinal formatting
- [ ] Add unit and measurement-system formatting
- [ ] Add list and conjunction formatting
- [ ] Add time-zone-aware formatting
- [ ] Add relative-time formatting (e.g. "3 minutes ago")
- [ ] Add culture-aware sorting and collation
- [ ] Add formatting validation and tests

## Localization — Assets, Fonts & RTL

- [ ] Add localized asset variants (textures, audio, video)
- [ ] Add per-culture asset selection at runtime
- [ ] Add per-culture and per-script fonts
- [ ] Add automatic font fallback per language
- [ ] Add right-to-left layout mirroring
- [ ] Add mirroring of icons and directional elements
- [ ] Add bidirectional mixed-content handling
- [ ] Add text-expansion handling for longer translations
- [ ] Add culture-specific styling overrides
- [ ] Add localized input and keyboard layouts
- [ ] Add pseudo-localization for testing
- [ ] Add validation of RTL and expansion issues
- [ ] Add per-culture number-input parsing
- [ ] Add localized-asset packaging

## Runtime Language Switching

- [ ] Add live language switching without a restart
- [ ] Add re-layout of UI on language change
- [ ] Add reload of strings, assets, and fonts on switch
- [ ] Add re-formatting of dynamic values on switch
- [ ] Add persistence of the chosen language
- [ ] Add a language-selection UI
- [ ] Add editor preview of any culture
- [ ] Add change notifications to widgets and gameplay
- [ ] Add graceful handling of partial translations
- [ ] Add language-switch performance budgets

## Accessibility — Screen Reader & Semantics

- [ ] Add an accessible widget tree parallel to the visual tree
- [ ] Add roles for widgets (button, checkbox, list, heading, etc.)
- [ ] Add accessible labels and descriptions
- [ ] Add live regions for announcements
- [ ] Add focus and selection reporting to assistive tech
- [ ] Add a defined reading order
- [ ] Add platform screen-reader integration
- [ ] Add custom-semantics overrides per widget
- [ ] Add state reporting (checked, expanded, disabled, busy)
- [ ] Add value and range reporting for sliders and progress
- [ ] Add grouping and landmark semantics
- [ ] Add localized accessibility text
- [ ] Add hints for available actions
- [ ] Add automatic label inference from content
- [ ] Add screen-reader testing hooks
- [ ] Add semantics debug inspection

## Accessibility — Visual & Motor

- [ ] Add text-scaling that reflows layout
- [ ] Add a global UI-scale accessibility option
- [ ] Add high-contrast modes
- [ ] Add colorblind-friendly palettes and filters
- [ ] Add reduced-motion mode
- [ ] Add strong focus indicators
- [ ] Add enlarged hit targets
- [ ] Add remappable UI navigation
- [ ] Add hold-to-toggle and sticky-input options
- [ ] Add dwell and one-switch scanning selection
- [ ] Add adjustable input timing and repeat
- [ ] Add cursor-size and pointer options
- [ ] Add pause and slow-down assists
- [ ] Add auto-advance and skip options for text
- [ ] Add flashing and photosensitivity safeguards
- [ ] Add motor-accessibility presets

## Accessibility — Audio & Feedback

- [ ] Add audio cues for UI focus and actions
- [ ] Add spoken descriptions of on-screen content
- [ ] Add haptic feedback for accessibility events
- [ ] Add customizable subtitle and caption styling
- [ ] Add a mono-audio option
- [ ] Add visual indicators for important sounds
- [ ] Add caption support for UI sounds
- [ ] Add volume-independent accessibility cues
- [ ] Add feedback-intensity settings
- [ ] Add validation of audio-accessibility coverage

## Accessibility — Settings & Compliance

- [ ] Add an accessibility settings menu
- [ ] Add accessibility presets and profiles
- [ ] Add first-run accessibility setup
- [ ] Add per-platform accessibility-API integration
- [ ] Add a compliance checklist against common guidelines
- [ ] Add an in-editor accessibility auditor
- [ ] Add contrast and text-size checks in the designer
- [ ] Add reporting of accessibility issues
- [ ] Add persistence and sync of accessibility settings
- [ ] Add documentation of accessibility features

## UI Performance & Batching

- [ ] Add draw-call batching across widgets
- [ ] Add invalidation panels to limit redraw
- [ ] Add retained-mode caching of static content
- [ ] Add widget pooling and recycling
- [ ] Add list and grid virtualization
- [ ] Add async and incremental widget construction
- [ ] Add layout caching for unchanged sub-trees
- [ ] Add per-frame UI time budgets
- [ ] Add texture-atlas usage for UI assets
- [ ] Add profiling and cost attribution per widget
- [ ] Add memory budgets for UI and fonts
- [ ] Add over-budget diagnostics

## UI Testing & Validation

- [ ] Add widget unit tests
- [ ] Add layout regression tests
- [ ] Add golden-image UI tests across resolutions
- [ ] Add input and navigation tests
- [ ] Add focus-order and tab-order tests
- [ ] Add data-binding correctness tests
- [ ] Add text-shaping and layout tests
- [ ] Add localization coverage and expansion tests
- [ ] Add right-to-left layout tests
- [ ] Add culture-formatting tests
- [ ] Add accessibility-semantics audits
- [ ] Add screen-reader announcement tests
- [ ] Add DPI and scaling tests
- [ ] Add UI performance stress tests
