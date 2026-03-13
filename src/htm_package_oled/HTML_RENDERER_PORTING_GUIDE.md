# HTML Renderer Porting Guide

This guide explains how to move the OLED-ready HTML renderer into another project.

## 1. Copy the Package

Copy the full `src/htm_package_oled/` directory into the target project.

Files to copy:
- `html_lite.c`
- `html_lite.h`
- `html_lite_schema.h`
- `html_ui_package.c`
- `html_ui_package.h`
- `html_ui_schema.h`
- `html_ui_templates.c`
- `html_ui_templates.h`
- `html_ui_template_store.c`
- `html_ui_template_store.h`
- `html_ui_platform_swipbox.c`
- `HTML_PARSING_README.md`

Optional:
- `HTML_RENDERER_PORTING_GUIDE.md`

## 2. Decide What To Reuse

Portable core only:
- `html_lite.c`
- `html_ui_package.c`
- `html_ui_package.h`
- schema headers

Portable core + file-backed templates:
- also keep `html_ui_template_store.c`
- also keep `html_ui_template_store.h`

Current OLED Zephyr backend:
- also keep `html_ui_platform_swipbox.c`

## 3. Core Architecture

The package is split into three layers:
- parser: `html_lite.*`
- portable renderer core: `html_ui_package.*`
- project-specific edges: adapter and template store

Project-specific edges are isolated so they can be replaced independently.

## 4. Rendering Backend

If the target project is not using Zephyr `display` APIs, replace `html_ui_platform_swipbox.c` with a project-specific adapter.

Implement these callbacks in `html_ui_platform_t`:
- `get_display_size`
- `is_dark_mode_enabled`
- `localization_exists`
- `fill_rect`
- `draw_line`
- `draw_text`
- `draw_icon`
- `set_touch_tag`
- `read_touch_tag`

Then provide:
- `html_ui_default_platform()`
- `html_ui_default_style()`

If you want full runtime control, call `html_ui_init_with_platform(...)` and pass your adapter directly.

## 5. Style Mapping

Map your project theme into `html_ui_style_t`:
- button width
- button height
- button font size
- centered-text option
- semantic colors (`background`, `primary`, `text`, `error`, `summary_line`, and related values)

`html_ui_color_t` is intentionally generic. Another project can store any backend-specific token there and interpret it inside its adapter.

## 6. Text and Localization

The renderer supports plain text and localization tokens in the form `[[PAGE.KEY]]`.

You can choose any of these strategies:
- keep the same token format and implement lookup in `localization_exists(...)` and `draw_text(...)`
- ignore localization and always render raw text
- map `page` and `key` into your own translation system

## 7. Input Handling

Action parsing and dispatch stay in the portable core.

The backend only needs to support:
- assigning touch tags with `set_touch_tag(...)`
- reading active touch tags with `read_touch_tag(...)`

If the target project has no touch support, these callbacks can be no-ops and keypad-only input can still work through `html_ui_handle_input(...)`.

## 8. Template Source Options

Embedded-only mode:
- call `html_ui_render(ctx, html_string)`
- do not include `html_ui_template_store.c`

File-backed template mode:
- call `html_ui_render_template_from_lfs_or_fallback(...)`
- keep `html_ui_template_store.c`
- replace the file operations there if the target project does not use Zephyr FS/LittleFS

## 9. Storage Porting Notes

If the target project does not use Zephyr FS, only `html_ui_template_store.c` should need storage-level changes.

Current Zephyr-specific calls in that file:
- `fs_statvfs`
- `fs_stat`
- `fs_mkdir`
- `fs_open`
- `fs_write`
- `fs_read`
- `fs_sync`
- `fs_close`

## 10. Build System Integration

Minimum source set:
```text
htm_package/html_lite.c
htm_package/html_ui_package.c
```

If using file-backed templates:
```text
htm_package/html_ui_template_store.c
```

If using the current OLED backend unchanged:
```text
htm_package_oled/html_ui_platform_swipbox.c
```

Also make sure the target build can include the `htm_package_oled/` headers.

## 11. Typical Runtime Setup

Typical startup sequence:
1. create an `html_ui_context_t`
2. initialize it with `html_ui_init(...)` or `html_ui_init_with_platform(...)`
3. set current screen if needed with `html_ui_set_screen(...)`
4. render a template
5. feed input into `html_ui_handle_input(...)`

Embedded-template example:
```c
html_ui_context_t ctx;

html_ui_init_with_platform(&ctx, render_ctx, &my_platform, my_user, &my_style, 1);
html_ui_render(&ctx, my_html_template);
```

File-backed example:
```c
html_ui_context_t ctx;
char template_buffer[4096];

html_ui_init(&ctx, render_ctx, 1);
html_ui_render_template_from_lfs_or_fallback(
    &ctx,
    "/internal/html_templates/scan_home.html",
    embedded_scan_home_html,
    template_buffer,
    sizeof(template_buffer));
```

## 12. Recommended Migration Order

Best sequence for another project:
1. copy the full `htm_package_oled/` directory
2. compile only `html_lite.c` and `html_ui_package.c`
3. implement a minimal adapter with text and rectangle drawing
4. render one static HTML screen
5. enable button input handling
6. add icon drawing
7. add localization only if needed
8. add file-backed templates only if needed

## 13. Project-Specific Pieces

These are intentionally isolated and are the first places to replace in another project:
- `html_ui_platform_swipbox.c`
- project UI asset/color integration
- project localization helpers
- touch-tag behavior
- Zephyr/LittleFS template store implementation

Once those pieces are swapped, the parser and renderer core should move with much less friction.