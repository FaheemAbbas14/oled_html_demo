# HTML Parsing README

This document describes the current HTML parsing behavior used by the firmware UI package.

If you change parser logic in either file below, update this README in the same commit:
- `src/htm_package/html_lite.c`
- `src/htm_package/html_ui_package.c`
- `src/htm_package/html_ui_platform_swipbox.c`
- `src/htm_package/html_ui_template_store.c`

## 1) Parsing Layers

There are two layers:

1. `html_lite` tokenizer/parser (`html_lite.c`)
- Reads HTML string and emits parse events.
- Does not render UI directly.

2. HTML UI package core (`html_ui_package.c`)
- Consumes parser events.
- Handles semantic tags (`div`, `label`, `button`, `card`, `icon`, `progress`).
- Draws text/buttons/background/progress/icon and builds action map.

3. Swipbox adapter (`html_ui_platform_swipbox.c`)
- Implements the built-in BT817 drawing/input adapter and default style values.

4. Template store (`html_ui_template_store.c`)
- Handles LittleFS template loading, one-time seeding attempts, storage diagnostics, and fallback render orchestration.

Renderer portability structure:
- The HTML UI package now separates generic parsing/render flow from project-specific drawing/input details.
- `html_ui_platform_t` is the adapter boundary for display size, themed mode state, localization probing, primitive drawing, icon/text rendering, and touch-tag reads.
- `html_ui_style_t` carries semantic colors and button sizing so another project can remap visuals without editing parser logic.
- `html_ui_init()` uses the built-in Swipbox adapter; another project can call `html_ui_init_with_platform()` with its own adapter and style.
- Template I/O is no longer part of the core renderer API header; it lives in `src/htm_package/html_ui_template_store.h`.

## 2) Event Model (`html_lite`)

Events from `html_lite.h`:
- `HTML_EVT_START_TAG`
- `HTML_EVT_END_TAG`
- `HTML_EVT_SELF_CLOSING_TAG`
- `HTML_EVT_TEXT`

Event payload:
- `tag`: tag name (for tag events)
- `text`: normalized text (for text events)
- `attrs[]`: parsed attributes (max 16 per tag)

## 3) Tag Handling in `html_lite.c`

General handling:
- Parses standard start/end tags.
- Parses explicit self-closing tags (`<x ... />`).
- Emits text nodes between tags.

Special handling:
- HTML comments (`<!-- ... -->`) are skipped.
- Markup declarations (`<!...>`) are skipped.
- Processing instructions (`<? ... ?>`) are skipped.
- `script` and `style` contents are skipped until closing tag.

Auto self-closing behavior:
- For non-self-closing tag syntax, parser emits an extra `HTML_EVT_SELF_CLOSING_TAG` for these void tags:
  - `br`, `img`, `hr`, `meta`, `link`, `input`

Name and buffer limits:
- Tag name buffer: 32 chars (`tag[32]`, truncated if longer).
- Text buffer: 256 chars (`txt[256]`, collapsed whitespace).
- Attribute scratch buffer: 256 chars.
- Attributes per tag: max 16 (`attrs[16]`).

Schema key location:
- UI tag/attribute/gravity keys are centralized in `src/htm_package/html_ui_schema.h`.
- HTML-lite parser markers/tags (comment/script/style/void tags) are centralized in `src/htm_package/html_lite_schema.h`.
- When renaming any HTML key, update that file first, then verify parser/template behavior.

## 4) Tag Handling in `html_ui_package.c`

UI parser callback: `on_evt(...)`

Handled tags:
- `div` (backward compatible container)
- `label` (text)
- `button` (action + touch tag + text)
- `card` (background container)
- `icon` (bitmap by id or `data-icon`)
- `progress` (track + fill bar)
- `row` (left-to-right flow layout container)
- `column` (top-to-bottom flow layout container)
- Any other tag is ignored by UI layer.

Event behavior:
- `HTML_EVT_START_TAG` on supported tags:
  - Reads attributes into current element state.
  - `card` draws background block.
  - `progress` draws bar immediately from value/max.
  - `icon` draws bitmap immediately.
  - `button` registers action entry.
- `HTML_EVT_TEXT`:
  - Draws text for current element (`label`/`div`) or button caption.
  - For buttons, draws button shape + centered label.
  - Text events are ignored for non-text tags (`card`, `icon`, `progress`, `row`, `column`).
- `HTML_EVT_END_TAG` for supported tags:
  - Clears current element state.
- `HTML_EVT_SELF_CLOSING_TAG`:
  - Currently ignored by UI layer.

Runtime validation (render-time):
- Before rendering, `html_ui_render()` runs a lightweight template validator.
- Validator warnings include:
  - mismatched/unmatched/unclosed tags
  - duplicate `id` values
  - `button` missing `data-action`

Runtime template source:
- Startup HTML screens first try to load a screen-specific template from LittleFS under `/internal/html_templates/`.
- If `/internal/html_templates/` is missing, the template store creates the directory once and seeds it with the built-in startup templates.
- Current file mapping is:
  - `scan_home.html`
  - `dummy_language.html`
  - `dummy_mode.html`
  - `dummy_scan.html`
  - `dummy_courier.html`
- If a file is missing, empty, not a regular file, or too large for the startup template buffer, the template store logs a warning and falls back to the embedded C template for that screen.
- If template seeding fails (open/write/close/mkdir path), the template store also logs `/internal` LittleFS usage (`total`, `used`, `free`) for easier ENOSPC diagnosis.

Default adapter scope:
- Swipbox-specific BT817 drawing, touch-tag access, localization lookup, and dark-mode state are now grouped in `src/htm_package/html_ui_platform_swipbox.c`.
- When extracting to another project, replace the adapter and style in that file, and optionally replace `src/htm_package/html_ui_template_store.c` if the target does not use Zephyr/LittleFS.

## 5) Supported Attributes (UI Layer)

Core identity and type:
- `id` -> action/text element id
- `data-type` -> `button` enables button path; any other/missing value treated as non-button
- Semantic tags (`button`, `label`, `card`, `icon`, `progress`) work without `data-type`.

Position and size (dual support):
- Pixel fields:
  - `data-x`, `data-y`, `data-w`, `data-h`
- Percentage override fields (no `%` sign):
  - `data-xp`, `data-yp`, `data-wp`, `data-hp`
- Flow percentage fields (inside row/column containers):
  - `data-flowp` (main-axis percentage of parent container)
  - `data-row-wp` (row-specific width percentage override)
  - `data-column-hp` (column-specific height percentage override)
- Side-anchor fields (optional):
  - Pixel: `data-left`, `data-right`, `data-top`, `data-bottom`
  - Percent: `data-leftp`, `data-rightp`, `data-topp`, `data-bottomp`
- Gravity fields (optional, used when x/y are omitted):
  - Axis-specific: `data-gravity-x` (`left` or `right`), `data-gravity-y` (`top` or `bottom`)
  - Combined: `data-gravity` (contains any of `left`, `right`, `top`, `bottom`)
- Override rule:
  - Pixel values are read first.
  - If percent field exists and parses, it overrides corresponding pixel field.
  - Side-anchor fields are applied after `x/y/w/h` parsing.
  - If both horizontal sides (`left` + `right`) are provided, width is derived.
  - If both vertical sides (`top` + `bottom`) are provided, height is derived.
  - If `x` is omitted and no horizontal side anchors are provided, gravity decides horizontal placement (`left` => `x=0`, `right` => `x=display_w-w`). Default is `right`.
  - If `y` is omitted and no vertical side anchors are provided, gravity decides vertical placement (`top` => `y=0`, `bottom` => `y=display_h-h`). Default is `bottom`.
  - If parent is `row`, omitted child `x` flows left-to-right from the row content area; child width can be set by `data-flowp`/`data-row-wp`.
  - If parent is `column`, omitted child `y` flows top-to-bottom from the column content area; child height can be set by `data-flowp`/`data-column-hp`.

Spacing fields:
- Margin:
  - `data-margin`
  - `data-margin-x`, `data-margin-y`
  - `data-margin-top`, `data-margin-right`, `data-margin-bottom`, `data-margin-left`
  - Percentage variants:
    - `data-margin-p`
    - `data-margin-xp`, `data-margin-yp`
    - `data-margin-top-p`, `data-margin-right-p`, `data-margin-bottom-p`, `data-margin-left-p`
- Padding:
  - `data-padding`
  - `data-padding-x`, `data-padding-y`
  - `data-padding-top`, `data-padding-right`, `data-padding-bottom`, `data-padding-left`
  - Percentage variants:
    - `data-padding-p`
    - `data-padding-xp`, `data-padding-yp`
    - `data-padding-top-p`, `data-padding-right-p`, `data-padding-bottom-p`, `data-padding-left-p`
- Notes:
  - Values are treated as design-space units and scaled uniformly with display size.
  - Percentage variants are relative to display width/height and override unit values when provided.
  - Negative values are clamped to `0`.
  - Margin affects outer element box (`x/y/w/h`).
  - Padding affects inner content area for text, icon, button label, and progress fill/track.

Text and font:
- Text comes from inner text node.
- `size` controls desired font size (scaled uniformly by display size).
- Effective font is normalized to available loaded sizes: `26`, `27`, `30`, `48`.

Input/action fields (buttons):
- `data-action` -> logical action name
- `data-target-html-screen` -> numeric `html_ui_screen_t` target
- `data-key` -> one or more keypad shortcuts
  - Accepts plain compact form (`"Ll"`) or delimiter-separated form (`"L,l"`, `"L|l"`, `"L/l"`, `"L; l"`).
  - Delimiters and whitespace are ignored; duplicate keys are removed.
- `data-tag` -> explicit touch tag (1..255)
  - If missing/invalid, auto-tag is assigned from `next_auto_tag`.

Button geometry note:
- Buttons use top-left rectangle semantics (`x/y` is top-left corner, `w/h` is size).
- `data-radius` controls corner style for button rendering:
  - omitted => rectangular button (default)
  - `<= 0` => rectangular button
  - `> 0` => rounded pill style (legacy full-height capsule)

Color/style fields:
- `data-text-color`
- `data-bg-color`
- `data-border-color`
- `data-fill-color` (progress fill)
- Theme-aware overrides (all tags that support corresponding color fields):
  - `data-text-color-light`, `data-text-color-dark`
  - `data-bg-color-light`, `data-bg-color-dark`
  - `data-border-color-light`, `data-border-color-dark`
  - `data-fill-color-light`, `data-fill-color-dark`
- Theme precedence:
  - Active mode is read from `g_dark_mode_enabled`.
  - If mode-specific attribute exists for active mode, it overrides generic color field.
  - If mode-specific attribute is missing/empty, generic color field is used.

Progress fields:
- `data-value` current value
- `data-max` max value (default 100)

Icon fields:
- `data-icon` bitmap asset name (fallback to `id`)

## 6) Supported Color Tokens

Mapped in `g_html_color_map` (`html_ui_package.c`):
- `BACKGROUND`
- `PRIMARY`
- `ERROR`
- `DISABLE`
- `TEXT`
- `DISABLE_TEXT`
- `AVAILABLE_COMPARTMENT`
- `INPUT_LINE`
- `SUMMARY_LINE`
- `INFORMATION_LINE`
- `INFORMATION_BOX`
- `CIRCLE_TEXT`
- `HEADER_BOX`
- `TEST_COLOR_RED`
- `TEST_COLOR_BLUE`
- `TEST_COLOR_WHITE`
- `TEST_COLOR_GREEN`
- `TEST_COLOR_BLACK`

## 7) Defaults and Fallbacks

Current UI defaults in `html_ui_package.c`:
- `data-x` default: `0`
- `data-y` default: `0`
- `data-w` default: `280`
- `data-h` default: `80`
- `size` default for text render path: `32`
- Button label color default: `PRIMARY`
- Button fill color default: `BACKGROUND`
- Button border color default: `PRIMARY`

Tag fallback behavior:
- Invalid/missing attributes use defaults.
- Unknown color token -> style flag remains false, default color path used.
- Invalid `data-target-html-screen` -> no target transition from default handler.

## 8) Input Dispatch Behavior

`html_ui_handle_input(...)` resolution order:
1. Key input match (`data-key`) first.
  - Action matches if pressed key exists in parsed key list.
2. If no key match, touch tag match (`data-tag` or auto tag).

Default dispatch behavior (when custom handler is NULL):
- If action has `data-target-html-screen`, updates `ctx->current_screen`.

Safety guards:
- Touch release wait loop in `html_ui_handle_input()` has timeout guard (`HTML_UI_TOUCH_RELEASE_TIMEOUT_MS`).
- Action map build logs warnings for duplicate touch tags and duplicate key bindings.

Localization diagnostics:
- For tokenized text (`[[PAGE.KEY]]`), package performs one-time probe per unique token.
- Missing/fallback localization keys are logged once to aid translation completeness checks.

## 9) Template Authoring Rules

Use this shape for responsive templates:
- Use `data-xp/data-yp/data-wp/data-hp` for responsive layout.
- Keep `data-x/y/w/h` only if you want explicit pixel fallback in that template.
- Percent fields must be numeric values without `%`.
  - Example: `data-wp="40"`, `data-hp="7.03"`

Button minimum required fields for interaction:
- `data-type="button"`
- button text node
- optional but recommended: `id`, `data-action`, `data-key` and/or `data-tag`

For native `button` tags, `data-type="button"` is not required.

Template style conventions (current project policy):
- Home action buttons (`language`, `mode`, `scan`, `courier`) use a shared button contract:
  - Must provide all six theme fields:
    - `data-bg-color-light`, `data-bg-color-dark`
    - `data-text-color-light`, `data-text-color-dark`
    - `data-border-color-light`, `data-border-color-dark`
  - Current preset keeps light mode unified:
    - `data-bg-color-light="PRIMARY"`
    - `data-text-color-light="TEST_COLOR_WHITE"`
    - `data-border-color-light="TEST_COLOR_WHITE"`
  - Dark mode background may vary per action for quick visual differentiation,
    while text/border stay readable (`TEST_COLOR_WHITE`).
- Dummy-screen `BACK` buttons keep light mode as primary and use black in dark mode:
  - `data-bg-color-light="PRIMARY"`
  - `data-bg-color-dark="TEST_COLOR_BLACK"`
  - `data-text-color-light="TEST_COLOR_WHITE"`
  - `data-text-color-dark="TEST_COLOR_WHITE"`
  - `data-border-color-light="TEST_COLOR_WHITE"`
  - `data-border-color-dark="TEST_COLOR_WHITE"`
- New templates should follow these presets unless a screen has an explicit design exception.

## 10) Maintenance Checklist (Required)

When changing parser behavior, update this README in the same PR/commit.

Update this file if you change any of:
- Event types or emission behavior in `html_lite.c`
- Tag skipping rules (comments/script/style/doctype/pi)
- Attribute limits or buffers
- Auto self-closing tag list
- UI-recognized tags in `html_ui_package.c`
- Supported `data-*` attributes
- Defaults/fallback logic

## 11) On-Device Validation Checklist

Run these checks after HTML/template changes:

Core interaction checks:
- Startup home screen renders without missing widgets.
- All four home buttons respond to touch and mapped keys (`L`, `R`, `l`, `r`).
- `mode` button label toggles (`DARK_MODE`/`LIGHT_MODE`) on each mode press.
- Each dummy screen `BACK` button returns to home and remains clickable after repeated navigation.

Theme checks:
- In light mode, home action buttons use unified primary style.
- In dark mode, home action buttons may use per-action background colors, with readable white text/border.
- Dummy `BACK` buttons keep light style in light mode and switch to black background with white text in dark mode.

Layout checks:
- `row` and `column` demo blocks render in expected positions and order.
- No overlapping text after language/mode toggles.
- Percent-based sizes/positions still look correct on target panel resolution.

Diagnostics checks (logs):
- No repeated warnings for unsupported tags/attributes during normal templates.
- No duplicate action key/tag warnings unless intentionally configured.
- No touch release timeout warning during normal touch usage.
- No missing localization token warnings for expected keys.
- Warning classes are deduplicated (warn-once per unique issue) to reduce log noise.

Behavior regression checks:
- Color token mapping remains correct.
- Input dispatch priority stays key-first, then touch.
- Default transition behavior still follows `data-target-html-screen`.

Developer note (required for future additions):
- Document every new parser/UI behavior in this README in the same change.
- Add succinct inline comments in code for non-obvious logic (especially precedence/override rules).
