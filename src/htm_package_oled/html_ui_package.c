#include "html_ui_package.h"

/*
 * html_ui_package.c
 *
 * UI layer on top of html_lite parser events.
 * Responsibility: interpret supported HTML tags/attributes, render widgets,
 * maintain action map, and resolve key/touch input dispatch.
 * Keeps HTML-driven startup/screens isolated from legacy UI state logic.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "html_lite.h"
#include "html_lite_schema.h"
#include "html_ui_schema.h"

LOG_MODULE_REGISTER(html_ui_core, LOG_LEVEL_INF);

/*
 * NOTE:
 * Keep src/htm_package/HTML_PARSING_README.md updated when tag/attribute
 * handling or UI parser behavior changes in this file.
 */

/*
 * HTML UI package flow (startup or screen-level usage):
 * 1) html_ui_render(): parse HTML, draw text/buttons, and build action map.
 * 2) html_ui_handle_input(): resolve keypad first, then touch tag.
 * 3) dispatch_action(): call custom handler or default html screen transition.
 *
 * This keeps HTML concerns isolated from existing UI screen code and lets
 * callers opt-in screen by screen without changing the global state machine.
 */

/* Keep parser state local to this package so existing UI paths stay untouched. */
typedef struct {
    bool in_elem;
    bool is_button;
    bool is_card;
    bool is_progress;
    bool is_icon;
    bool is_row;
    bool is_column;
    char id[32];
    char action[24];
    char keys[HTML_UI_MAX_KEYS_PER_ACTION + 1];
    char key;
    bool has_target_html_screen;
    html_ui_screen_t target_html_screen;
    int x;
    int y;
    int w;
    int h;
    int margin_top;
    int margin_right;
    int margin_bottom;
    int margin_left;
    int padding_top;
    int padding_right;
    int padding_bottom;
    int padding_left;
    int radius;
    int size;
    uint8_t tag;
    bool has_text_color;
    html_ui_color_t text_color;
    bool has_bg_color;
    html_ui_color_t bg_color;
    bool has_border_color;
    html_ui_color_t border_color;
    bool has_fill_color;
    html_ui_color_t fill_color;
    int value;
    int max;
    char icon_name[32];
} current_elem_t;

typedef enum {
    LAYOUT_NONE = 0,
    LAYOUT_ROW,
    LAYOUT_COLUMN,
} layout_type_t;

typedef struct {
    layout_type_t type;
    int x;
    int y;
    int w;
    int h;
    int cursor;
} layout_context_t;

typedef struct {
    html_ui_context_t *ctx;
    current_elem_t cur;
    layout_context_t layout_stack[8];
    uint8_t layout_depth;
} parser_state_t;

#define HTML_UI_BASE_WIDTH 800
#define HTML_UI_BASE_HEIGHT 1280
#define HTML_UI_TOUCH_RELEASE_TIMEOUT_MS 1200U
#define HTML_UI_VALIDATION_STACK_DEPTH 32U
#define HTML_UI_VALIDATION_IDS 64U
#define HTML_UI_LOCALIZATION_PROBE_CAPACITY 64U
#define HTML_UI_WARN_ONCE_CAPACITY 128U

typedef struct {
    char tag_stack[HTML_UI_VALIDATION_STACK_DEPTH][32];
    size_t depth;
    char ids[HTML_UI_VALIDATION_IDS][32];
    size_t id_count;
} html_template_validator_t;

typedef struct {
    char page[32];
    char key[32];
    bool missing;
    bool used;
} localization_probe_t;

typedef struct {
    char kind[16];
    char a[32];
    char b[32];
    bool used;
} warn_once_entry_t;

typedef struct {
    int start_count;
    int end_count;
    int self_closing_count;
    int text_count;
} parser_smoke_stats_t;

static localization_probe_t g_localization_probe[HTML_UI_LOCALIZATION_PROBE_CAPACITY];
static warn_once_entry_t g_warn_once_entries[HTML_UI_WARN_ONCE_CAPACITY];

static const char *attr_get(const html_evt_t *evt, const char *name);
static void copy_text(char *dst, size_t dst_sz, const char *src);
static bool html_ui_is_dark_mode(const html_ui_context_t *ctx);
static bool html_ui_localization_exists(const html_ui_context_t *ctx, const char *page, const char *key);
static bool html_ui_resolve_color_token(const html_ui_context_t *ctx, const char *token, html_ui_color_t *color);
static void html_ui_fill_rect(const html_ui_context_t *ctx,
                              int x1,
                              int y1,
                              int x2,
                              int y2,
                              html_ui_color_t color);
static void html_ui_draw_line(const html_ui_context_t *ctx,
                              int x1,
                              int y1,
                              int x2,
                              int y2,
                              int line_width,
                              html_ui_color_t color);
static void html_ui_draw_text_call(const html_ui_context_t *ctx,
                                   int x,
                                   int y,
                                   int font,
                                   int options,
                                   html_ui_color_t color,
                                   const char *page,
                                   const char *key);
static void html_ui_draw_icon_call(const html_ui_context_t *ctx, const char *icon_name, int x, int y, bool center);
static void html_ui_set_touch_tag(const html_ui_context_t *ctx, uint8_t tag);
static uint8_t html_ui_read_touch_tag(const html_ui_context_t *ctx);

static bool g_html_smoke_test_ran;

static bool html_ui_is_dark_mode(const html_ui_context_t *ctx)
{
    if (ctx != NULL && ctx->platform != NULL && ctx->platform->is_dark_mode_enabled != NULL) {
        return ctx->platform->is_dark_mode_enabled(ctx->platform_user);
    }

    return false;
}

static bool html_ui_localization_exists(const html_ui_context_t *ctx, const char *page, const char *key)
{
    if (ctx != NULL && ctx->platform != NULL && ctx->platform->localization_exists != NULL) {
        return ctx->platform->localization_exists(ctx->platform_user, page, key);
    }

    return true;
}

static bool html_ui_resolve_color_token(const html_ui_context_t *ctx, const char *token, html_ui_color_t *color)
{
    const html_ui_style_t *style;

    if (ctx == NULL || token == NULL || *token == 0 || color == NULL) {
        return false;
    }

    style = &ctx->style;

    if (strcmp(token, "BACKGROUND") == 0) {
        *color = style->background_color;
    } else if (strcmp(token, "PRIMARY") == 0) {
        *color = style->primary_color;
    } else if (strcmp(token, "ERROR") == 0) {
        *color = style->error_color;
    } else if (strcmp(token, "DISABLE") == 0) {
        *color = style->disable_color;
    } else if (strcmp(token, "TEXT") == 0) {
        *color = style->text_color;
    } else if (strcmp(token, "DISABLE_TEXT") == 0) {
        *color = style->disable_text_color;
    } else if (strcmp(token, "AVAILABLE_COMPARTMENT") == 0) {
        *color = style->available_compartment_color;
    } else if (strcmp(token, "INPUT_LINE") == 0) {
        *color = style->input_line_color;
    } else if (strcmp(token, "SUMMARY_LINE") == 0) {
        *color = style->summary_line_color;
    } else if (strcmp(token, "INFORMATION_LINE") == 0) {
        *color = style->information_line_color;
    } else if (strcmp(token, "INFORMATION_BOX") == 0) {
        *color = style->information_box_color;
    } else if (strcmp(token, "CIRCLE_TEXT") == 0) {
        *color = style->circle_text_color;
    } else if (strcmp(token, "HEADER_BOX") == 0) {
        *color = style->header_box_color;
    } else if (strcmp(token, "TEST_COLOR_RED") == 0) {
        *color = style->test_red_color;
    } else if (strcmp(token, "TEST_COLOR_BLUE") == 0) {
        *color = style->test_blue_color;
    } else if (strcmp(token, "TEST_COLOR_WHITE") == 0) {
        *color = style->test_white_color;
    } else if (strcmp(token, "TEST_COLOR_GREEN") == 0) {
        *color = style->test_green_color;
    } else if (strcmp(token, "TEST_COLOR_BLACK") == 0) {
        *color = style->test_black_color;
    } else {
        return false;
    }

    return true;
}

static void html_ui_fill_rect(const html_ui_context_t *ctx,
                              int x1,
                              int y1,
                              int x2,
                              int y2,
                              html_ui_color_t color)
{
    if (ctx != NULL && ctx->platform != NULL && ctx->platform->fill_rect != NULL) {
        ctx->platform->fill_rect(ctx->render_ctx, x1, y1, x2, y2, color);
    }
}

static void html_ui_draw_line(const html_ui_context_t *ctx,
                              int x1,
                              int y1,
                              int x2,
                              int y2,
                              int line_width,
                              html_ui_color_t color)
{
    if (ctx != NULL && ctx->platform != NULL && ctx->platform->draw_line != NULL) {
        ctx->platform->draw_line(ctx->render_ctx, x1, y1, x2, y2, line_width, color);
    }
}

static void html_ui_draw_text_call(const html_ui_context_t *ctx,
                                   int x,
                                   int y,
                                   int font,
                                   int options,
                                   html_ui_color_t color,
                                   const char *page,
                                   const char *key)
{
    if (ctx != NULL && ctx->platform != NULL && ctx->platform->draw_text != NULL) {
        ctx->platform->draw_text(ctx->render_ctx, x, y, font, options, color, page, key);
    }
}

static void html_ui_draw_icon_call(const html_ui_context_t *ctx, const char *icon_name, int x, int y, bool center)
{
    if (ctx != NULL && ctx->platform != NULL && ctx->platform->draw_icon != NULL) {
        ctx->platform->draw_icon(ctx->render_ctx, icon_name, x, y, center);
    }
}

static void html_ui_set_touch_tag(const html_ui_context_t *ctx, uint8_t tag)
{
    if (ctx != NULL && ctx->platform != NULL && ctx->platform->set_touch_tag != NULL) {
        ctx->platform->set_touch_tag(ctx->render_ctx, tag);
    }
}

static uint8_t html_ui_read_touch_tag(const html_ui_context_t *ctx)
{
    if (ctx != NULL && ctx->platform != NULL && ctx->platform->read_touch_tag != NULL) {
        return ctx->platform->read_touch_tag(ctx->render_ctx);
    }

    return 0;
}

/* Log internal LittleFS capacity to diagnose seed failures (for example ENOSPC). */
/* Return true only for the first occurrence of a warning key tuple. */
static bool warn_once_should_log(const char *kind, const char *a, const char *b)
{
    size_t i;

    for (i = 0; i < HTML_UI_WARN_ONCE_CAPACITY; i++) {
        if (!g_warn_once_entries[i].used) {
            continue;
        }
        if (strcmp(g_warn_once_entries[i].kind, kind ? kind : "") == 0 &&
            strcmp(g_warn_once_entries[i].a, a ? a : "") == 0 &&
            strcmp(g_warn_once_entries[i].b, b ? b : "") == 0) {
            return false;
        }
    }

    for (i = 0; i < HTML_UI_WARN_ONCE_CAPACITY; i++) {
        if (!g_warn_once_entries[i].used) {
            g_warn_once_entries[i].used = true;
            copy_text(g_warn_once_entries[i].kind, sizeof(g_warn_once_entries[i].kind), kind ? kind : "");
            copy_text(g_warn_once_entries[i].a, sizeof(g_warn_once_entries[i].a), a ? a : "");
            copy_text(g_warn_once_entries[i].b, sizeof(g_warn_once_entries[i].b), b ? b : "");
            return true;
        }
    }

    /* If cache is full, keep logging rather than hiding issues. */
    return true;
}

static void get_display_size(const html_ui_context_t *ctx, int *width, int *height)
{
    if (width == NULL || height == NULL) {
        return;
    }

    *width = HTML_UI_BASE_WIDTH;
    *height = HTML_UI_BASE_HEIGHT;

    if (ctx == NULL || ctx->platform == NULL || ctx->platform->get_display_size == NULL) {
        return;
    }

    (void)ctx->platform->get_display_size(ctx->render_ctx, width, height);
}

/* Scale one axis value from design-space units to current panel size. */
static int scale_axis(int value, int actual_size, int base_size)
{
    if (base_size <= 0) {
        return value;
    }

    return (value * actual_size) / base_size;
}

/* Scale font/size uniformly so elements stay proportional across aspect ratios. */
static int scale_size_uniform(int value, int actual_w, int actual_h)
{
    int by_w;
    int by_h;

    if (value <= 0) {
        return value;
    }

    by_w = scale_axis(value, actual_w, HTML_UI_BASE_WIDTH);
    by_h = scale_axis(value, actual_h, HTML_UI_BASE_HEIGHT);

    /* Keep font roughly proportional on non-800x1280 panels. */
    return (by_w < by_h) ? by_w : by_h;
}

static int clamp_non_negative(int value)
{
    /* Keep spacing/size fields safe for downstream geometry math. */
    return value < 0 ? 0 : value;
}

/* Compute inner content rectangle after applying element padding. */
static void get_content_rect(const current_elem_t *cur, int *x, int *y, int *w, int *h)
{
    int cx;
    int cy;
    int cw;
    int ch;

    if (cur == NULL || x == NULL || y == NULL || w == NULL || h == NULL) {
        return;
    }

    cx = cur->x + cur->padding_left;
    cy = cur->y + cur->padding_top;
    cw = cur->w - (cur->padding_left + cur->padding_right);
    ch = cur->h - (cur->padding_top + cur->padding_bottom);

    /* If padding collapses content area, fall back to full element rect. */
    if (cw <= 0) {
        cw = cur->w;
        cx = cur->x;
    }
    if (ch <= 0) {
        ch = cur->h;
        cy = cur->y;
    }

    *x = cx;
    *y = cy;
    *w = cw;
    *h = ch;
}

/* Map arbitrary requested text size to available loaded custom font sizes. */
static int normalize_font_size(int requested)
{
    if (requested >= 40) {
        return 48;
    }
    if (requested >= 29) {
        return 30;
    }
    if (requested >= 27) {
        return 27;
    }
    return 26;
}

/* Return current top-most row/column layout context if available. */
static layout_context_t *layout_top(parser_state_t *st)
{
    if (st == NULL || st->layout_depth == 0) {
        return NULL;
    }
    return &st->layout_stack[st->layout_depth - 1];
}

/* Push a new row/column layout context used for child auto-flow placement. */
static void layout_push(parser_state_t *st, layout_type_t type, int x, int y, int w, int h)
{
    layout_context_t *slot;

    if (st == NULL || st->layout_depth >= (sizeof(st->layout_stack) / sizeof(st->layout_stack[0]))) {
        return;
    }

    slot = &st->layout_stack[st->layout_depth++];
    slot->type = type;
    slot->x = x;
    slot->y = y;
    slot->w = w;
    slot->h = h;
    slot->cursor = 0;
}

/* Pop one layout context when leaving a row/column element scope. */
static void layout_pop(parser_state_t *st)
{
    if (st == NULL || st->layout_depth == 0) {
        return;
    }
    st->layout_depth--;
}

/* Parse token format [[PAGE.KEY]] into output buffers. */
static bool parse_localization_token(const char *text,
                                     char *page_out,
                                     size_t page_out_size,
                                     char *key_out,
                                     size_t key_out_size)
{
    const char *inner_start;
    const char *inner_end;
    const char *sep;
    size_t page_len;
    size_t key_len;

    if (text == NULL || page_out == NULL || key_out == NULL ||
        page_out_size == 0 || key_out_size == 0) {
        return false;
    }

    page_out[0] = '\0';
    key_out[0] = '\0';

    if (strncmp(text, "[[", 2) != 0) {
        return false;
    }

    inner_start = text + 2;
    inner_end = strstr(inner_start, "]]");
    if (inner_end == NULL || inner_end[2] != '\0') {
        return false;
    }

    sep = strchr(inner_start, '.');
    if (sep == NULL || sep >= inner_end) {
        return false;
    }

    page_len = (size_t)(sep - inner_start);
    key_len = (size_t)(inner_end - (sep + 1));
    if (page_len == 0 || key_len == 0 || page_len >= page_out_size || key_len >= key_out_size) {
        return false;
    }

    memcpy(page_out, inner_start, page_len);
    page_out[page_len] = '\0';
    memcpy(key_out, sep + 1, key_len);
    key_out[key_len] = '\0';

    return true;
}

/* Parse optional percentage field like data-wp="40" (no percent sign). */
static bool parse_percent_attr(const char *s, int actual_size, int *out_px)
{
    char *endptr;
    double parsed;
    int percent_px;

    if (s == NULL || *s == 0 || out_px == NULL) {
        return false;
    }

    parsed = strtod(s, &endptr);
    if (endptr == NULL || *endptr != 0) {
        return false;
    }

    if (parsed < 0.0) {
        parsed = 0.0;
    }

    percent_px = (int)((parsed * (double)actual_size) / 100.0);
    *out_px = percent_px;
    return true;
}

/* Parse numeric html screen id and validate enum bounds. */
static bool to_html_screen(const char *s, html_ui_screen_t *screen)
{
    long parsed;

    if (s == NULL || *s == 0 || screen == NULL) {
        return false;
    }

    parsed = strtol(s, NULL, 10);
    if (parsed < 0 || parsed >= _HTML_UI_SCREEN_COUNT) {
        return false;
    }

    *screen = (html_ui_screen_t)parsed;
    return true;
}

/* Resolve color token and its source attribute name based on active theme. */
static const char *resolve_theme_color_attr_name(bool dark_mode,
                                                 const char **out_name,
                                                 const char *base_name,
                                                 const char *base_attr,
                                                 const char *light_name,
                                                 const char *light_attr,
                                                 const char *dark_name,
                                                 const char *dark_attr)
{
    if (dark_mode) {
        if (dark_attr != NULL && *dark_attr != 0) {
            *out_name = dark_name;
            return dark_attr;
        }
    } else {
        if (light_attr != NULL && *light_attr != 0) {
            *out_name = light_name;
            return light_attr;
        }
    }

    *out_name = base_name;
    return base_attr;
}

/* Look up an attribute from parser event attributes by exact name. */
static const char *attr_get(const html_evt_t *evt, const char *name)
{
    size_t i;

    for (i = 0; i < evt->attr_count; i++) {
        if (evt->attrs[i].name && strcmp(evt->attrs[i].name, name) == 0) {
            return evt->attrs[i].value ? evt->attrs[i].value : "";
        }
    }

    return NULL;
}

/* Return true when attribute name is part of centralized HTML UI schema keys. */
static bool is_supported_attr_name(const char *name)
{
    if (name == NULL || *name == 0) {
        return false;
    }

    return strcmp(name, HTML_ATTR_ID) == 0 ||
           strcmp(name, HTML_ATTR_TYPE) == 0 ||
           strcmp(name, HTML_ATTR_ACTION) == 0 ||
           strcmp(name, HTML_ATTR_TARGET_SCREEN) == 0 ||
           strcmp(name, HTML_ATTR_TAG) == 0 ||
           strcmp(name, HTML_ATTR_KEY) == 0 ||
           strcmp(name, HTML_ATTR_X) == 0 ||
           strcmp(name, HTML_ATTR_Y) == 0 ||
           strcmp(name, HTML_ATTR_W) == 0 ||
           strcmp(name, HTML_ATTR_H) == 0 ||
           strcmp(name, HTML_ATTR_XP) == 0 ||
           strcmp(name, HTML_ATTR_YP) == 0 ||
           strcmp(name, HTML_ATTR_WP) == 0 ||
           strcmp(name, HTML_ATTR_HP) == 0 ||
           strcmp(name, HTML_ATTR_GRAVITY) == 0 ||
           strcmp(name, HTML_ATTR_GRAVITY_X) == 0 ||
           strcmp(name, HTML_ATTR_GRAVITY_Y) == 0 ||
           strcmp(name, HTML_ATTR_FLOW_P) == 0 ||
           strcmp(name, HTML_ATTR_ROW_WP) == 0 ||
           strcmp(name, HTML_ATTR_COLUMN_HP) == 0 ||
           strcmp(name, HTML_ATTR_TEXT_COLOR) == 0 ||
           strcmp(name, HTML_ATTR_TEXT_COLOR_LIGHT) == 0 ||
           strcmp(name, HTML_ATTR_TEXT_COLOR_DARK) == 0 ||
           strcmp(name, HTML_ATTR_BG_COLOR) == 0 ||
           strcmp(name, HTML_ATTR_BG_COLOR_LIGHT) == 0 ||
           strcmp(name, HTML_ATTR_BG_COLOR_DARK) == 0 ||
           strcmp(name, HTML_ATTR_BORDER_COLOR) == 0 ||
           strcmp(name, HTML_ATTR_BORDER_COLOR_LIGHT) == 0 ||
           strcmp(name, HTML_ATTR_BORDER_COLOR_DARK) == 0 ||
           strcmp(name, HTML_ATTR_FILL_COLOR) == 0 ||
           strcmp(name, HTML_ATTR_FILL_COLOR_LIGHT) == 0 ||
           strcmp(name, HTML_ATTR_FILL_COLOR_DARK) == 0 ||
           strcmp(name, HTML_ATTR_ICON) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_X) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_Y) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_TOP) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_RIGHT) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_BOTTOM) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_LEFT) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_P) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_XP) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_YP) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_TOP_P) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_RIGHT_P) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_BOTTOM_P) == 0 ||
           strcmp(name, HTML_ATTR_MARGIN_LEFT_P) == 0 ||
           strcmp(name, HTML_ATTR_PADDING) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_X) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_Y) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_TOP) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_RIGHT) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_BOTTOM) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_LEFT) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_P) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_XP) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_YP) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_TOP_P) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_RIGHT_P) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_BOTTOM_P) == 0 ||
           strcmp(name, HTML_ATTR_PADDING_LEFT_P) == 0 ||
           strcmp(name, HTML_ATTR_LEFT) == 0 ||
           strcmp(name, HTML_ATTR_RIGHT) == 0 ||
           strcmp(name, HTML_ATTR_TOP) == 0 ||
           strcmp(name, HTML_ATTR_BOTTOM) == 0 ||
           strcmp(name, HTML_ATTR_LEFTP) == 0 ||
           strcmp(name, HTML_ATTR_RIGHTP) == 0 ||
           strcmp(name, HTML_ATTR_TOPP) == 0 ||
           strcmp(name, HTML_ATTR_BOTTOMP) == 0 ||
           strcmp(name, HTML_ATTR_RADIUS) == 0 ||
           strcmp(name, HTML_ATTR_VALUE) == 0 ||
           strcmp(name, HTML_ATTR_MAX) == 0 ||
           strcmp(name, HTML_ATTR_SIZE) == 0;
}

/* Warn on unknown color tokens and return parsed color only for known values. */
static bool parse_color_attr(const html_ui_context_t *ctx,
                             const char *attr_name,
                             const char *token,
                             html_ui_color_t *color)
{
    if (token == NULL || *token == 0) {
        return false;
    }

    if (html_ui_resolve_color_token(ctx, token, color)) {
        return true;
    }

    if (warn_once_should_log("color", attr_name, token)) {
        LOG_WRN(
                     "HTML_UI: unknown color token attr=%s value=%s",
                     attr_name ? attr_name : "-",
                     token);
    }
    return false;
}

/* Detect HTML void tags that should not be pushed onto validation stack. */
static bool is_void_tag_name(const char *tag)
{
    if (tag == NULL) {
        return false;
    }

    return strcmp(tag, HTML_LITE_TAG_BR) == 0 ||
           strcmp(tag, HTML_LITE_TAG_IMG) == 0 ||
           strcmp(tag, HTML_LITE_TAG_HR) == 0 ||
           strcmp(tag, HTML_LITE_TAG_META) == 0 ||
           strcmp(tag, HTML_LITE_TAG_LINK) == 0 ||
           strcmp(tag, HTML_LITE_TAG_INPUT) == 0;
}

/* Validate basic template consistency (tag nesting, duplicate ids, button actions). */
static void html_validate_evt(const html_evt_t *evt, void *user)
{
    html_template_validator_t *st = (html_template_validator_t *)user;
    const char *id;
    size_t i;

    if (st == NULL || evt == NULL || evt->tag == NULL) {
        return;
    }

    if (evt->type == HTML_EVT_START_TAG) {
        if (!is_void_tag_name(evt->tag)) {
            if (st->depth < HTML_UI_VALIDATION_STACK_DEPTH) {
                copy_text(st->tag_stack[st->depth], sizeof(st->tag_stack[st->depth]), evt->tag);
                st->depth++;
            } else {
                LOG_WRN( "HTML_UI: validator stack overflow at tag=%s", evt->tag);
            }
        }

        id = attr_get(evt, HTML_ATTR_ID);
        if (id != NULL && *id != 0) {
            for (i = 0; i < st->id_count; i++) {
                if (strcmp(st->ids[i], id) == 0) {
                    LOG_WRN( "HTML_UI: duplicate id in template id=%s", id);
                    break;
                }
            }
            if (i == st->id_count && st->id_count < HTML_UI_VALIDATION_IDS) {
                copy_text(st->ids[st->id_count], sizeof(st->ids[st->id_count]), id);
                st->id_count++;
            }
        }

        if (strcmp(evt->tag, HTML_TAG_BUTTON) == 0) {
            const char *action = attr_get(evt, HTML_ATTR_ACTION);
            if (action == NULL || *action == 0) {
                LOG_WRN( "HTML_UI: button missing data-action id=%s", id ? id : "-");
            }
        }
    } else if (evt->type == HTML_EVT_END_TAG) {
        if (st->depth == 0) {
            LOG_WRN( "HTML_UI: unmatched end tag </%s>", evt->tag);
            return;
        }

        if (strcmp(st->tag_stack[st->depth - 1], evt->tag) != 0) {
            LOG_WRN(
                         "HTML_UI: mismatched close tag expected </%s> got </%s>",
                         st->tag_stack[st->depth - 1],
                         evt->tag);
        }

        st->depth--;
    }
}

/* Run lightweight validator over HTML before rendering to catch authoring errors early. */
static void html_ui_validate_template(const char *html)
{
    html_template_validator_t st;
    size_t i;

    if (html == NULL || *html == 0) {
        return;
    }

    memset(&st, 0, sizeof(st));
    html_parse_lite(html, html_validate_evt, &st);

    if (st.depth > 0) {
        for (i = 0; i < st.depth; i++) {
            LOG_WRN( "HTML_UI: unclosed tag <%s>", st.tag_stack[i]);
        }
    }
}

/* Probe localization lookup once per unique token and warn when fallback is likely used. */
static void localization_probe_once(const html_ui_context_t *ctx, const char *page, const char *key)
{
    size_t i;
    bool missing = true;

    if (page == NULL || key == NULL || *page == 0 || *key == 0) {
        return;
    }

    for (i = 0; i < HTML_UI_LOCALIZATION_PROBE_CAPACITY; i++) {
        if (g_localization_probe[i].used &&
            strcmp(g_localization_probe[i].page, page) == 0 &&
            strcmp(g_localization_probe[i].key, key) == 0) {
            return;
        }
    }

    missing = !html_ui_localization_exists(ctx, page, key);

    for (i = 0; i < HTML_UI_LOCALIZATION_PROBE_CAPACITY; i++) {
        if (!g_localization_probe[i].used) {
            g_localization_probe[i].used = true;
            g_localization_probe[i].missing = missing;
            copy_text(g_localization_probe[i].page, sizeof(g_localization_probe[i].page), page);
            copy_text(g_localization_probe[i].key, sizeof(g_localization_probe[i].key), key);
            break;
        }
    }

    if (missing) {
        LOG_WRN( "HTML_UI: missing localization token page=%s key=%s", page, key);
    }
}

/* Count parser events for a tiny internal smoke test snippet. */
static void parser_smoke_evt(const html_evt_t *evt, void *user)
{
    parser_smoke_stats_t *stats = (parser_smoke_stats_t *)user;

    if (evt == NULL || stats == NULL) {
        return;
    }

    switch (evt->type) {
    case HTML_EVT_START_TAG:
        stats->start_count++;
        break;
    case HTML_EVT_END_TAG:
        stats->end_count++;
        break;
    case HTML_EVT_SELF_CLOSING_TAG:
        stats->self_closing_count++;
        break;
    case HTML_EVT_TEXT:
        stats->text_count++;
        break;
    default:
        break;
    }
}

/* Run lightweight parser smoke checks once to catch unexpected parser regressions early. */
static void html_ui_run_smoke_tests_once(void)
{
    parser_smoke_stats_t stats;

    if (g_html_smoke_test_ran) {
        return;
    }
    g_html_smoke_test_ran = true;

    memset(&stats, 0, sizeof(stats));
    html_parse_lite("<row><label>TV</label></row>", parser_smoke_evt, &stats);
    if (stats.start_count != 2 || stats.end_count != 2 || stats.text_count != 1) {
        LOG_WRN(
                     "HTML_UI: smoke test failed (row/label) start=%d end=%d text=%d self=%d",
                     stats.start_count,
                     stats.end_count,
                     stats.text_count,
                     stats.self_closing_count);
    }

    memset(&stats, 0, sizeof(stats));
    html_parse_lite("<br>", parser_smoke_evt, &stats);
    if (stats.start_count != 1 || stats.self_closing_count != 1) {
        LOG_WRN(
                     "HTML_UI: smoke test failed (void tag) start=%d self=%d",
                     stats.start_count,
                     stats.self_closing_count);
    }
}

/* Parse integer with fallback when attribute is empty. */
static int to_i(const char *s, int def)
{
    if (s == NULL || *s == 0) {
        return def;
    }

    return (int)strtol(s, NULL, 10);
}

/* Parse touch tag and clamp to valid BT817 tag range. */
static uint8_t to_tag(const char *s, uint8_t fallback)
{
    long parsed;

    if (s == NULL || *s == 0) {
        return fallback;
    }

    parsed = strtol(s, NULL, 10);
    if (parsed <= 0 || parsed > 255) {
        return fallback;
    }

    return (uint8_t)parsed;
}

/* Safe string copy helper used for fixed-size action fields. */
static void copy_text(char *dst, size_t dst_sz, const char *src)
{
    if (dst == NULL || dst_sz == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = 0;
        return;
    }

    snprintf(dst, dst_sz, "%s", src);
}

/* Identify separators accepted in multi-key data-key definitions. */
static bool is_key_delimiter(char c)
{
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
            c == ',' || c == ';' || c == '|' || c == '/');
}

/* Parse data-key into a compact unique key list (supports delimiters or plain text). */
static void parse_keys(const char *src, char *dst, size_t dst_sz)
{
    size_t i;
    size_t out = 0;

    if (dst == NULL || dst_sz == 0) {
        return;
    }

    dst[0] = 0;
    if (src == NULL || *src == 0) {
        return;
    }

    for (i = 0; src[i] != 0; i++) {
        char ch = src[i];

        if (is_key_delimiter(ch)) {
            continue;
        }

        if (strchr(dst, ch) != NULL) {
            continue;
        }

        if (out >= (dst_sz - 1)) {
            break;
        }

        dst[out++] = ch;
    }

    dst[out] = 0;
}

static bool action_has_key(const html_ui_action_t *action, char key)
{
    if (action == NULL || key == 0) {
        return false;
    }

    if (action->keys[0] != 0) {
        return strchr(action->keys, key) != NULL;
    }

    return (action->key != 0 && action->key == key);
}

/* Persist one parsed button into the runtime action lookup table. */
static void save_action(parser_state_t *st)
{
    html_ui_action_t *a;
    size_t i;
    size_t k;

    if (st == NULL || st->ctx == NULL) {
        return;
    }

    if (!st->cur.is_button) {
        return;
    }

    if (st->ctx->action_count >= HTML_UI_MAX_ACTIONS) {
        LOG_WRN( "HTML_UI: action table full, dropping id=%s", st->cur.id);
        return;
    }

    for (i = 0; i < st->ctx->action_count; i++) {
        if (st->ctx->actions[i].tag == st->cur.tag) {
            if (warn_once_should_log("tag-collision", st->ctx->actions[i].id, st->cur.id)) {
                LOG_WRN(
                             "HTML_UI: duplicate touch tag=%u between ids %s and %s",
                             (unsigned)st->cur.tag,
                             st->ctx->actions[i].id,
                             st->cur.id);
            }
        }
    }

    for (k = 0; st->cur.keys[k] != 0; k++) {
        for (i = 0; i < st->ctx->action_count; i++) {
            if (action_has_key(&st->ctx->actions[i], st->cur.keys[k])) {
                if (warn_once_should_log("key-collision", st->ctx->actions[i].id, st->cur.id)) {
                    LOG_WRN(
                                 "HTML_UI: duplicate key=%c between ids %s and %s",
                                 st->cur.keys[k],
                                 st->ctx->actions[i].id,
                                 st->cur.id);
                }
            }
        }
    }

    /* Action table is resolved at runtime from touch tag/key to state transition. */
    a = &st->ctx->actions[st->ctx->action_count++];
    memset(a, 0, sizeof(*a));
    a->tag = st->cur.tag;
    copy_text(a->id, sizeof(a->id), st->cur.id);
    copy_text(a->action, sizeof(a->action), st->cur.action);
    a->has_target_html_screen = st->cur.has_target_html_screen;
    a->target_html_screen = st->cur.target_html_screen;
    copy_text(a->keys, sizeof(a->keys), st->cur.keys);
    a->key = st->cur.keys[0];

    LOG_DBG(
                 "HTML_UI: action[%u] id=%s action=%s tag=%u keys=%s key=%c(%d) has_target_html=%d target_html_screen=%d",
                 (unsigned)(st->ctx->action_count - 1),
                 a->id,
                 a->action,
                 (unsigned)a->tag,
                 a->keys[0] ? a->keys : "-",
                 a->key ? a->key : '-',
                 (int)a->key,
                 a->has_target_html_screen ? 1 : 0,
                 (int)a->target_html_screen);
}

/* Draw plain filled rectangle background for generic container-like elements. */
static void draw_background_block(const html_ui_context_t *ctx, const current_elem_t *cur)
{
    int x2;
    int y2;

    if (ctx == NULL || cur == NULL || !cur->has_bg_color || cur->w <= 0 || cur->h <= 0) {
        return;
    }

    x2 = cur->x + cur->w;
    y2 = cur->y + cur->h;

    html_ui_fill_rect(ctx, cur->x, cur->y, x2, y2, cur->bg_color);
}

/* Draw card as border rectangle with inner fill inset. */
static void draw_card_block(const html_ui_context_t *ctx, const current_elem_t *cur)
{
    int x2;
    int y2;
    int inset = 2;
    html_ui_color_t border;
    html_ui_color_t fill;

    if (ctx == NULL || cur == NULL || cur->w <= 0 || cur->h <= 0) {
        return;
    }

    border = cur->has_border_color ? cur->border_color : ctx->style.summary_line_color;
    fill = cur->has_bg_color ? cur->bg_color : ctx->style.background_color;

    x2 = cur->x + cur->w;
    y2 = cur->y + cur->h;

    html_ui_fill_rect(ctx, cur->x, cur->y, x2, y2, border);

    if (cur->w <= (inset * 2) || cur->h <= (inset * 2)) {
        return;
    }

    html_ui_fill_rect(ctx, cur->x + inset, cur->y + inset, x2 - inset, y2 - inset, fill);
}

/* Draw icon bitmap centered in element content area. */
static void draw_icon(const html_ui_context_t *ctx, const current_elem_t *cur)
{
    const char *icon;
    int cx;
    int cy;
    int cw;
    int ch;

    if (ctx == NULL || cur == NULL) {
        return;
    }

    icon = (cur->icon_name[0] != 0) ? cur->icon_name : cur->id;
    if (icon == NULL || icon[0] == 0) {
        return;
    }

    get_content_rect(cur, &cx, &cy, &cw, &ch);
    html_ui_draw_icon_call(ctx, icon, cx + (cw / 2), cy + (ch / 2), true);
}

/* Draw progress track and fill based on current value/max fields. */
static void draw_progress(const html_ui_context_t *ctx, const current_elem_t *cur)
{
    int x2;
    int y2;
    int clamped;
    int fill_w;
    int px;
    int py;
    int pw;
    int ph;
    html_ui_color_t track;
    html_ui_color_t fill;

    if (ctx == NULL || cur == NULL || cur->w <= 0 || cur->h <= 0 || cur->max <= 0) {
        return;
    }

    get_content_rect(cur, &px, &py, &pw, &ph);
    if (pw <= 0 || ph <= 0) {
        return;
    }

    clamped = cur->value;
    if (clamped < 0) {
        clamped = 0;
    }
    if (clamped > cur->max) {
        clamped = cur->max;
    }

    track = cur->has_bg_color ? cur->bg_color : ctx->style.disable_color;
    fill = cur->has_fill_color ? cur->fill_color : ctx->style.primary_color;

    x2 = px + pw;
    y2 = py + ph;

    html_ui_fill_rect(ctx, px, py, x2, y2, track);

    fill_w = (pw * clamped) / cur->max;
    if (fill_w <= 0) {
        return;
    }

    html_ui_fill_rect(ctx, px, py, px + fill_w, y2, fill);
}

/* Draw a plain text element from the HTML template. */
static void draw_text(const html_ui_context_t *ctx, const current_elem_t *cur, const char *text)
{
    int font;
    html_ui_color_t color;
    char page_buf[64];
    char key_buf[64];
    const char *page;
    const char *key;
    int tx;
    int ty;
    int tw;
    int th;

    if (ctx == NULL || cur == NULL || text == NULL || *text == 0) {
        return;
    }

    font = cur->size > 0 ? normalize_font_size(cur->size) : 30;
    color = cur->has_text_color ? cur->text_color : ctx->style.text_color;

    if (parse_localization_token(text, page_buf, sizeof(page_buf), key_buf, sizeof(key_buf))) {
        page = page_buf;
        key = key_buf;
        localization_probe_once(ctx, page, key);
    } else {
        page = text;
        key = "";
    }

    get_content_rect(cur, &tx, &ty, &tw, &th);
    html_ui_draw_text_call(ctx,
                           tx + (tw / 2),
                           ty,
                           font,
                           ctx->style.text_center_x_option,
                           color,
                           page,
                           key);
}

/* Draw one HTML button and center its label inside parsed button geometry. */
static void draw_html_button(const html_ui_context_t *ctx, const current_elem_t *cur, const char *text)
{
    int disp_w = HTML_UI_BASE_WIDTH;
    int disp_h = HTML_UI_BASE_HEIGHT;
    int label_offset_y;
    int label_font;
    int btn_w;
    int btn_h;
    int x2;
    int y2;
    int y_center;
    int radius;
    int corner_radius;
    int line_width;
    int inner_line_width;
    int start_x;
    int end_x;
    int content_x;
    int content_y;
    int content_w;
    int content_h;
    html_ui_color_t label_color;
    html_ui_color_t fill_color;
    html_ui_color_t border_color;
    char page_buf[64];
    char key_buf[64];
    const char *page;
    const char *key;

    if (ctx == NULL || cur == NULL || text == NULL || *text == 0) {
        return;
    }

    get_display_size(ctx, &disp_w, &disp_h);

    label_offset_y = scale_size_uniform(20, disp_w, disp_h);
    label_font = scale_size_uniform(ctx->style.button_font_size, disp_w, disp_h);
    if (label_font <= 0) {
        label_font = ctx->style.button_font_size;
    }
    label_font = normalize_font_size(label_font);

    btn_w = cur->w > 0 ? cur->w : ctx->style.button_width;
    btn_h = cur->h > 0 ? cur->h : ctx->style.button_height;
    label_color = cur->has_text_color ? cur->text_color : ctx->style.primary_color;
    fill_color = cur->has_bg_color ? cur->bg_color : ctx->style.background_color;
    border_color = cur->has_border_color ? cur->border_color : ctx->style.primary_color;

    if (parse_localization_token(text, page_buf, sizeof(page_buf), key_buf, sizeof(key_buf))) {
        page = page_buf;
        key = key_buf;

        /* Keep mode button text in sync with current theme state on every render. */
        if (strcmp(cur->id, "mode") == 0 && strcmp(page, "BUTTON_TEXT") == 0) {
            key = html_ui_is_dark_mode(ctx) ? "LIGHT_MODE" : "DARK_MODE";
        }

        localization_probe_once(ctx, page, key);
    } else {
        page = text;
        key = "";
    }

    get_content_rect(cur, &content_x, &content_y, &content_w, &content_h);

    /* Draw button using top-left geometry; rounded mode is enabled only when data-radius > 0. */
    html_ui_set_touch_tag(ctx, cur->tag);
    x2 = cur->x + btn_w;
    y2 = cur->y + btn_h;
    y_center = cur->y + (btn_h / 2);
    radius = cur->radius;
    corner_radius = btn_h / 2;

    if (radius <= 0) {
        int rx2 = x2 - 1;
        int ry2 = y2 - 1;

        if (rx2 < cur->x) {
            rx2 = cur->x;
        }
        if (ry2 < cur->y) {
            ry2 = cur->y;
        }

        html_ui_fill_rect(ctx, cur->x, cur->y, rx2, ry2, border_color);

        if (btn_w > 4 && btn_h > 4) {
            int ix1 = cur->x + 2;
            int iy1 = cur->y + 2;
            int ix2 = rx2 - 2;
            int iy2 = ry2 - 2;

            if (ix2 < ix1) {
                ix2 = ix1;
            }
            if (iy2 < iy1) {
                iy2 = iy1;
            }

            html_ui_fill_rect(ctx, ix1, iy1, ix2, iy2, fill_color);
        }
    } else {
        /* Rounded mode keeps legacy full-height capsule geometry for stable visuals. */
        (void)radius;
        corner_radius = btn_h / 2;

        line_width = corner_radius * 2;
        if (line_width < 2) {
            line_width = 2;
        }
        inner_line_width = line_width - 4;
        if (inner_line_width < 1) {
            inner_line_width = 1;
        }

        start_x = cur->x + corner_radius;
        end_x = x2 - corner_radius;
        if (start_x > end_x) {
            start_x = cur->x;
            end_x = x2;
        }

        html_ui_draw_line(ctx, start_x, y_center, end_x, y_center, line_width, border_color);

        html_ui_draw_line(ctx, start_x, y_center, end_x, y_center, inner_line_width, fill_color);
    }

    html_ui_draw_text_call(ctx,
                           content_x + (content_w / 2),
                           content_y + (content_h / 2) - label_offset_y,
                           label_font,
                           ctx->style.text_center_x_option,
                           label_color,
                           page,
                           key);
}

/* HTML parser callback: build element state, draw widgets, and register actions. */
static void on_evt(const html_evt_t *evt, void *user)
{
    parser_state_t *st = (parser_state_t *)user;

    if (st == NULL || evt == NULL) {
        return;
    }

    switch (evt->type) {
    case HTML_EVT_START_TAG: {
        int disp_w;
        int disp_h;
        const char *kind;
        const char *target_html_screen;
        const char *tag_attr;
        const char *action;
        const char *id;
        const char *key_attr;
        const char *x_attr;
        const char *y_attr;
        const char *w_attr;
        const char *h_attr;
        const char *gravity_attr;
        const char *gravity_x_attr;
        const char *gravity_y_attr;
        const char *x_percent_attr;
        const char *y_percent_attr;
        const char *w_percent_attr;
        const char *h_percent_attr;
        const char *flow_percent_attr;
        const char *row_width_percent_attr;
        const char *column_height_percent_attr;
        const char *text_color_attr;
        const char *text_color_light_attr;
        const char *text_color_dark_attr;
        const char *bg_color_attr;
        const char *bg_color_light_attr;
        const char *bg_color_dark_attr;
        const char *border_color_attr;
        const char *border_color_light_attr;
        const char *border_color_dark_attr;
        const char *fill_color_attr;
        const char *fill_color_light_attr;
        const char *fill_color_dark_attr;
        const char *icon_attr;
        const char *margin_all_attr;
        const char *margin_x_attr;
        const char *margin_y_attr;
        const char *margin_top_attr;
        const char *margin_right_attr;
        const char *margin_bottom_attr;
        const char *margin_left_attr;
        const char *padding_all_attr;
        const char *padding_x_attr;
        const char *padding_y_attr;
        const char *padding_top_attr;
        const char *padding_right_attr;
        const char *padding_bottom_attr;
        const char *padding_left_attr;
        const char *margin_all_percent_attr;
        const char *margin_x_percent_attr;
        const char *margin_y_percent_attr;
        const char *margin_top_percent_attr;
        const char *margin_right_percent_attr;
        const char *margin_bottom_percent_attr;
        const char *margin_left_percent_attr;
        const char *padding_all_percent_attr;
        const char *padding_x_percent_attr;
        const char *padding_y_percent_attr;
        const char *padding_top_percent_attr;
        const char *padding_right_percent_attr;
        const char *padding_bottom_percent_attr;
        const char *padding_left_percent_attr;
        const char *radius_attr;
        const char *left_attr;
        const char *right_attr;
        const char *top_attr;
        const char *bottom_attr;
        const char *left_percent_attr;
        const char *right_percent_attr;
        const char *top_percent_attr;
        const char *bottom_percent_attr;

        int side_left = 0;
        int side_right = 0;
        int side_top = 0;
        int side_bottom = 0;
        bool has_left = false;
        bool has_right = false;
        bool has_top = false;
        bool has_bottom = false;
        bool has_x = false;
        bool has_y = false;
        bool has_w = false;
        bool has_h = false;
        bool gravity_x_left = false;
        bool gravity_x_right = true;
        bool gravity_y_top = false;
        bool gravity_y_bottom = true;
        bool has_flow_percent = false;
        int flow_percent = 0;
        layout_context_t *active_layout;

        bool tag_is_div;
        bool tag_is_button;
        bool tag_is_label;
        bool tag_is_card;
        bool tag_is_progress;
        bool tag_is_icon;
        bool tag_is_row;
        bool tag_is_column;
        size_t attr_idx;
        const char *selected_color_attr_name;
        const char *selected_color_token;

        if (evt->tag == NULL) {
            break;
        }

        tag_is_div = (strcmp(evt->tag, HTML_TAG_DIV) == 0);
        tag_is_button = (strcmp(evt->tag, HTML_TAG_BUTTON) == 0);
        tag_is_label = (strcmp(evt->tag, HTML_TAG_LABEL) == 0);
        tag_is_card = (strcmp(evt->tag, HTML_TAG_CARD) == 0);
        tag_is_progress = (strcmp(evt->tag, HTML_TAG_PROGRESS) == 0);
        tag_is_icon = (strcmp(evt->tag, HTML_TAG_ICON) == 0);
        tag_is_row = (strcmp(evt->tag, HTML_TAG_ROW) == 0);
        tag_is_column = (strcmp(evt->tag, HTML_TAG_COLUMN) == 0);

        if (!(tag_is_div || tag_is_button || tag_is_label || tag_is_card || tag_is_progress || tag_is_icon ||
              tag_is_row || tag_is_column)) {
            if (warn_once_should_log("tag", evt->tag, "")) {
                LOG_WRN( "HTML_UI: unsupported tag <%s>", evt->tag);
            }
            break;
        }

        for (attr_idx = 0; attr_idx < evt->attr_count; attr_idx++) {
            const char *attr_name = evt->attrs[attr_idx].name;
            if (attr_name != NULL && !is_supported_attr_name(attr_name)) {
                if (warn_once_should_log("attr", attr_name, evt->tag)) {
                    LOG_WRN( "HTML_UI: unsupported attribute %s on <%s>", attr_name, evt->tag);
                }
            }
        }

        memset(&st->cur, 0, sizeof(st->cur));
        st->cur.in_elem = true;

        id = attr_get(evt, HTML_ATTR_ID);
        kind = attr_get(evt, HTML_ATTR_TYPE);
        action = attr_get(evt, HTML_ATTR_ACTION);
        target_html_screen = attr_get(evt, HTML_ATTR_TARGET_SCREEN);
        tag_attr = attr_get(evt, HTML_ATTR_TAG);
        key_attr = attr_get(evt, HTML_ATTR_KEY);
        x_attr = attr_get(evt, HTML_ATTR_X);
        y_attr = attr_get(evt, HTML_ATTR_Y);
        w_attr = attr_get(evt, HTML_ATTR_W);
        h_attr = attr_get(evt, HTML_ATTR_H);
        gravity_attr = attr_get(evt, HTML_ATTR_GRAVITY);
        gravity_x_attr = attr_get(evt, HTML_ATTR_GRAVITY_X);
        gravity_y_attr = attr_get(evt, HTML_ATTR_GRAVITY_Y);
        x_percent_attr = attr_get(evt, HTML_ATTR_XP);
        y_percent_attr = attr_get(evt, HTML_ATTR_YP);
        w_percent_attr = attr_get(evt, HTML_ATTR_WP);
        h_percent_attr = attr_get(evt, HTML_ATTR_HP);
        flow_percent_attr = attr_get(evt, HTML_ATTR_FLOW_P);
        row_width_percent_attr = attr_get(evt, HTML_ATTR_ROW_WP);
        column_height_percent_attr = attr_get(evt, HTML_ATTR_COLUMN_HP);
        text_color_attr = attr_get(evt, HTML_ATTR_TEXT_COLOR);
        text_color_light_attr = attr_get(evt, HTML_ATTR_TEXT_COLOR_LIGHT);
        text_color_dark_attr = attr_get(evt, HTML_ATTR_TEXT_COLOR_DARK);
        bg_color_attr = attr_get(evt, HTML_ATTR_BG_COLOR);
        bg_color_light_attr = attr_get(evt, HTML_ATTR_BG_COLOR_LIGHT);
        bg_color_dark_attr = attr_get(evt, HTML_ATTR_BG_COLOR_DARK);
        border_color_attr = attr_get(evt, HTML_ATTR_BORDER_COLOR);
        border_color_light_attr = attr_get(evt, HTML_ATTR_BORDER_COLOR_LIGHT);
        border_color_dark_attr = attr_get(evt, HTML_ATTR_BORDER_COLOR_DARK);
        fill_color_attr = attr_get(evt, HTML_ATTR_FILL_COLOR);
        fill_color_light_attr = attr_get(evt, HTML_ATTR_FILL_COLOR_LIGHT);
        fill_color_dark_attr = attr_get(evt, HTML_ATTR_FILL_COLOR_DARK);
        icon_attr = attr_get(evt, HTML_ATTR_ICON);
        margin_all_attr = attr_get(evt, HTML_ATTR_MARGIN);
        margin_x_attr = attr_get(evt, HTML_ATTR_MARGIN_X);
        margin_y_attr = attr_get(evt, HTML_ATTR_MARGIN_Y);
        margin_top_attr = attr_get(evt, HTML_ATTR_MARGIN_TOP);
        margin_right_attr = attr_get(evt, HTML_ATTR_MARGIN_RIGHT);
        margin_bottom_attr = attr_get(evt, HTML_ATTR_MARGIN_BOTTOM);
        margin_left_attr = attr_get(evt, HTML_ATTR_MARGIN_LEFT);
        padding_all_attr = attr_get(evt, HTML_ATTR_PADDING);
        padding_x_attr = attr_get(evt, HTML_ATTR_PADDING_X);
        padding_y_attr = attr_get(evt, HTML_ATTR_PADDING_Y);
        padding_top_attr = attr_get(evt, HTML_ATTR_PADDING_TOP);
        padding_right_attr = attr_get(evt, HTML_ATTR_PADDING_RIGHT);
        padding_bottom_attr = attr_get(evt, HTML_ATTR_PADDING_BOTTOM);
        padding_left_attr = attr_get(evt, HTML_ATTR_PADDING_LEFT);
        margin_all_percent_attr = attr_get(evt, HTML_ATTR_MARGIN_P);
        margin_x_percent_attr = attr_get(evt, HTML_ATTR_MARGIN_XP);
        margin_y_percent_attr = attr_get(evt, HTML_ATTR_MARGIN_YP);
        margin_top_percent_attr = attr_get(evt, HTML_ATTR_MARGIN_TOP_P);
        margin_right_percent_attr = attr_get(evt, HTML_ATTR_MARGIN_RIGHT_P);
        margin_bottom_percent_attr = attr_get(evt, HTML_ATTR_MARGIN_BOTTOM_P);
        margin_left_percent_attr = attr_get(evt, HTML_ATTR_MARGIN_LEFT_P);
        padding_all_percent_attr = attr_get(evt, HTML_ATTR_PADDING_P);
        padding_x_percent_attr = attr_get(evt, HTML_ATTR_PADDING_XP);
        padding_y_percent_attr = attr_get(evt, HTML_ATTR_PADDING_YP);
        padding_top_percent_attr = attr_get(evt, HTML_ATTR_PADDING_TOP_P);
        padding_right_percent_attr = attr_get(evt, HTML_ATTR_PADDING_RIGHT_P);
        padding_bottom_percent_attr = attr_get(evt, HTML_ATTR_PADDING_BOTTOM_P);
        padding_left_percent_attr = attr_get(evt, HTML_ATTR_PADDING_LEFT_P);
        left_attr = attr_get(evt, HTML_ATTR_LEFT);
        right_attr = attr_get(evt, HTML_ATTR_RIGHT);
        top_attr = attr_get(evt, HTML_ATTR_TOP);
        bottom_attr = attr_get(evt, HTML_ATTR_BOTTOM);
        left_percent_attr = attr_get(evt, HTML_ATTR_LEFTP);
        right_percent_attr = attr_get(evt, HTML_ATTR_RIGHTP);
        top_percent_attr = attr_get(evt, HTML_ATTR_TOPP);
        bottom_percent_attr = attr_get(evt, HTML_ATTR_BOTTOMP);
        radius_attr = attr_get(evt, HTML_ATTR_RADIUS);

        copy_text(st->cur.id, sizeof(st->cur.id), id ? id : "");
        copy_text(st->cur.action, sizeof(st->cur.action), action ? action : "");
        st->cur.is_button = tag_is_button || (kind != NULL && strcmp(kind, HTML_TYPE_BUTTON) == 0);
        st->cur.is_card = tag_is_card || (kind != NULL && strcmp(kind, HTML_TYPE_CARD) == 0);
        st->cur.is_progress = tag_is_progress || (kind != NULL && strcmp(kind, HTML_TYPE_PROGRESS) == 0);
        st->cur.is_icon = tag_is_icon || (kind != NULL && strcmp(kind, HTML_TYPE_ICON) == 0);
        st->cur.is_row = tag_is_row || (kind != NULL && strcmp(kind, HTML_TYPE_ROW) == 0);
        st->cur.is_column = tag_is_column || (kind != NULL && strcmp(kind, HTML_TYPE_COLUMN) == 0);

        get_display_size(st->ctx, &disp_w, &disp_h);
        st->cur.x = to_i(x_attr, 0);
        st->cur.y = to_i(y_attr, 0);
        st->cur.w = to_i(w_attr, 280);
        st->cur.h = to_i(h_attr, 80);
        has_x = ((x_attr != NULL && *x_attr != 0) || (x_percent_attr != NULL && *x_percent_attr != 0));
        has_y = ((y_attr != NULL && *y_attr != 0) || (y_percent_attr != NULL && *y_percent_attr != 0));
        has_w = ((w_attr != NULL && *w_attr != 0) || (w_percent_attr != NULL && *w_percent_attr != 0));
        has_h = ((h_attr != NULL && *h_attr != 0) || (h_percent_attr != NULL && *h_percent_attr != 0));

        /*
         * Optional gravity hints for omitted x/y axes only.
         * Precedence for each axis: explicit x/y -> side anchors -> gravity fallback.
         */
        if (gravity_attr != NULL && *gravity_attr != 0) {
            if (strstr(gravity_attr, HTML_GRAVITY_LEFT) != NULL) {
                gravity_x_left = true;
                gravity_x_right = false;
            } else if (strstr(gravity_attr, HTML_GRAVITY_RIGHT) != NULL) {
                gravity_x_left = false;
                gravity_x_right = true;
            }

            if (strstr(gravity_attr, HTML_GRAVITY_TOP) != NULL) {
                gravity_y_top = true;
                gravity_y_bottom = false;
            } else if (strstr(gravity_attr, HTML_GRAVITY_BOTTOM) != NULL) {
                gravity_y_top = false;
                gravity_y_bottom = true;
            }
        }

        if (gravity_x_attr != NULL && *gravity_x_attr != 0) {
            if (strcmp(gravity_x_attr, HTML_GRAVITY_LEFT) == 0) {
                gravity_x_left = true;
                gravity_x_right = false;
            } else if (strcmp(gravity_x_attr, HTML_GRAVITY_RIGHT) == 0) {
                gravity_x_left = false;
                gravity_x_right = true;
            }
        }

        if (gravity_y_attr != NULL && *gravity_y_attr != 0) {
            if (strcmp(gravity_y_attr, HTML_GRAVITY_TOP) == 0) {
                gravity_y_top = true;
                gravity_y_bottom = false;
            } else if (strcmp(gravity_y_attr, HTML_GRAVITY_BOTTOM) == 0) {
                gravity_y_top = false;
                gravity_y_bottom = true;
            }
        }

        /* Percentage values override pixel x/y/w/h when present. */
        (void)parse_percent_attr(x_percent_attr, disp_w, &st->cur.x);
        (void)parse_percent_attr(y_percent_attr, disp_h, &st->cur.y);
        (void)parse_percent_attr(w_percent_attr, disp_w, &st->cur.w);
        (void)parse_percent_attr(h_percent_attr, disp_h, &st->cur.h);

        /*
         * Side-anchor positioning runs before gravity fallback.
         * If both sides are set on an axis, the corresponding size is derived.
         */
        if (left_attr != NULL && *left_attr != 0) {
            side_left = to_i(left_attr, 0);
            has_left = true;
        }
        if (right_attr != NULL && *right_attr != 0) {
            side_right = to_i(right_attr, 0);
            has_right = true;
        }
        if (top_attr != NULL && *top_attr != 0) {
            side_top = to_i(top_attr, 0);
            has_top = true;
        }
        if (bottom_attr != NULL && *bottom_attr != 0) {
            side_bottom = to_i(bottom_attr, 0);
            has_bottom = true;
        }

        if (parse_percent_attr(left_percent_attr, disp_w, &side_left)) {
            has_left = true;
        }
        if (parse_percent_attr(right_percent_attr, disp_w, &side_right)) {
            has_right = true;
        }
        if (parse_percent_attr(top_percent_attr, disp_h, &side_top)) {
            has_top = true;
        }
        if (parse_percent_attr(bottom_percent_attr, disp_h, &side_bottom)) {
            has_bottom = true;
        }

        side_left = clamp_non_negative(side_left);
        side_right = clamp_non_negative(side_right);
        side_top = clamp_non_negative(side_top);
        side_bottom = clamp_non_negative(side_bottom);

        if (has_left && has_right) {
            st->cur.x = side_left;
            st->cur.w = disp_w - side_left - side_right;
        } else if (has_left) {
            st->cur.x = side_left;
        } else if (has_right) {
            st->cur.x = disp_w - side_right - st->cur.w;
        }

        if (has_top && has_bottom) {
            st->cur.y = side_top;
            st->cur.h = disp_h - side_top - side_bottom;
        } else if (has_top) {
            st->cur.y = side_top;
        } else if (has_bottom) {
            st->cur.y = disp_h - side_bottom - st->cur.h;
        }

        if (st->cur.w < 1) st->cur.w = 1;
        if (st->cur.h < 1) st->cur.h = 1;

        /* Final fallback: apply gravity only on omitted axes that have no side anchors. */
        if (!has_x && !has_left && !has_right) {
            st->cur.x = gravity_x_left ? 0 : (disp_w - st->cur.w);
        }
        if (!has_y && !has_top && !has_bottom) {
            st->cur.y = gravity_y_top ? 0 : (disp_h - st->cur.h);
        }

        /*
         * Flow layout support:
         * - row: children are placed left->right, optional width via data-flowp/data-row-wp
         * - column: children are placed top->bottom, optional height via data-flowp/data-column-hp
         * Explicit x/y/w/h and side anchors still win because this only fills omitted axes.
         */
        active_layout = layout_top(st);
        if (active_layout != NULL) {
            if (active_layout->type == LAYOUT_ROW) {
                if (row_width_percent_attr != NULL && parse_percent_attr(row_width_percent_attr, active_layout->w, &flow_percent)) {
                    st->cur.w = flow_percent;
                    has_w = true;
                    has_flow_percent = true;
                } else if (flow_percent_attr != NULL && parse_percent_attr(flow_percent_attr, active_layout->w, &flow_percent)) {
                    st->cur.w = flow_percent;
                    has_w = true;
                    has_flow_percent = true;
                }

                if (!has_w && !has_left && !has_right) {
                    st->cur.w = active_layout->w - active_layout->cursor;
                }
                if (!has_h && !has_top && !has_bottom) {
                    st->cur.h = active_layout->h;
                }
                if (!has_x && !has_left && !has_right) {
                    st->cur.x = active_layout->x + active_layout->cursor;
                }
                if (!has_y && !has_top && !has_bottom) {
                    st->cur.y = active_layout->y;
                }

                if (st->cur.w < 1) {
                    st->cur.w = 1;
                }

                if (has_flow_percent || (!has_x && !has_left && !has_right)) {
                    active_layout->cursor += st->cur.w;
                }
            } else if (active_layout->type == LAYOUT_COLUMN) {
                if (column_height_percent_attr != NULL && parse_percent_attr(column_height_percent_attr, active_layout->h, &flow_percent)) {
                    st->cur.h = flow_percent;
                    has_h = true;
                    has_flow_percent = true;
                } else if (flow_percent_attr != NULL && parse_percent_attr(flow_percent_attr, active_layout->h, &flow_percent)) {
                    st->cur.h = flow_percent;
                    has_h = true;
                    has_flow_percent = true;
                }

                if (!has_h && !has_top && !has_bottom) {
                    st->cur.h = active_layout->h - active_layout->cursor;
                }
                if (!has_w && !has_left && !has_right) {
                    st->cur.w = active_layout->w;
                }
                if (!has_y && !has_top && !has_bottom) {
                    st->cur.y = active_layout->y + active_layout->cursor;
                }
                if (!has_x && !has_left && !has_right) {
                    st->cur.x = active_layout->x;
                }

                if (st->cur.h < 1) {
                    st->cur.h = 1;
                }

                if (has_flow_percent || (!has_y && !has_top && !has_bottom)) {
                    active_layout->cursor += st->cur.h;
                }
            }
        }

        st->cur.size = scale_size_uniform(to_i(attr_get(evt, HTML_ATTR_SIZE), 0), disp_w, disp_h);
        selected_color_token = resolve_theme_color_attr_name(html_ui_is_dark_mode(st->ctx),
                                     &selected_color_attr_name,
                                                             HTML_ATTR_TEXT_COLOR,
                                                             text_color_attr,
                                                             HTML_ATTR_TEXT_COLOR_LIGHT,
                                                             text_color_light_attr,
                                                             HTML_ATTR_TEXT_COLOR_DARK,
                                                             text_color_dark_attr);
        st->cur.has_text_color = parse_color_attr(st->ctx, selected_color_attr_name, selected_color_token, &st->cur.text_color);

        selected_color_token = resolve_theme_color_attr_name(html_ui_is_dark_mode(st->ctx),
                                     &selected_color_attr_name,
                                                             HTML_ATTR_BG_COLOR,
                                                             bg_color_attr,
                                                             HTML_ATTR_BG_COLOR_LIGHT,
                                                             bg_color_light_attr,
                                                             HTML_ATTR_BG_COLOR_DARK,
                                                             bg_color_dark_attr);
        st->cur.has_bg_color = parse_color_attr(st->ctx, selected_color_attr_name, selected_color_token, &st->cur.bg_color);

        selected_color_token = resolve_theme_color_attr_name(html_ui_is_dark_mode(st->ctx),
                                     &selected_color_attr_name,
                                                             HTML_ATTR_BORDER_COLOR,
                                                             border_color_attr,
                                                             HTML_ATTR_BORDER_COLOR_LIGHT,
                                                             border_color_light_attr,
                                                             HTML_ATTR_BORDER_COLOR_DARK,
                                                             border_color_dark_attr);
        st->cur.has_border_color = parse_color_attr(st->ctx, selected_color_attr_name, selected_color_token, &st->cur.border_color);

        selected_color_token = resolve_theme_color_attr_name(html_ui_is_dark_mode(st->ctx),
                                     &selected_color_attr_name,
                                                             HTML_ATTR_FILL_COLOR,
                                                             fill_color_attr,
                                                             HTML_ATTR_FILL_COLOR_LIGHT,
                                                             fill_color_light_attr,
                                                             HTML_ATTR_FILL_COLOR_DARK,
                                                             fill_color_dark_attr);
        st->cur.has_fill_color = parse_color_attr(st->ctx, selected_color_attr_name, selected_color_token, &st->cur.fill_color);
        st->cur.value = to_i(attr_get(evt, HTML_ATTR_VALUE), 0);
        st->cur.max = to_i(attr_get(evt, HTML_ATTR_MAX), 100);
        copy_text(st->cur.icon_name, sizeof(st->cur.icon_name), icon_attr ? icon_attr : "");
        if (radius_attr != NULL) {
            st->cur.radius = scale_size_uniform(to_i(radius_attr, 0), disp_w, disp_h);
        } else {
            st->cur.radius = 0;
        }

        st->cur.margin_top = scale_size_uniform(to_i(margin_all_attr, 0), disp_w, disp_h);
        st->cur.margin_right = st->cur.margin_top;
        st->cur.margin_bottom = st->cur.margin_top;
        st->cur.margin_left = st->cur.margin_top;
        if (margin_x_attr != NULL && *margin_x_attr != 0) {
            int m = scale_size_uniform(to_i(margin_x_attr, 0), disp_w, disp_h);
            st->cur.margin_left = m;
            st->cur.margin_right = m;
        }
        if (margin_y_attr != NULL && *margin_y_attr != 0) {
            int m = scale_size_uniform(to_i(margin_y_attr, 0), disp_w, disp_h);
            st->cur.margin_top = m;
            st->cur.margin_bottom = m;
        }
        if (margin_top_attr != NULL && *margin_top_attr != 0) st->cur.margin_top = scale_size_uniform(to_i(margin_top_attr, 0), disp_w, disp_h);
        if (margin_right_attr != NULL && *margin_right_attr != 0) st->cur.margin_right = scale_size_uniform(to_i(margin_right_attr, 0), disp_w, disp_h);
        if (margin_bottom_attr != NULL && *margin_bottom_attr != 0) st->cur.margin_bottom = scale_size_uniform(to_i(margin_bottom_attr, 0), disp_w, disp_h);
        if (margin_left_attr != NULL && *margin_left_attr != 0) st->cur.margin_left = scale_size_uniform(to_i(margin_left_attr, 0), disp_w, disp_h);

        if (margin_all_percent_attr != NULL && *margin_all_percent_attr != 0) {
            int mx = 0;
            int my = 0;
            (void)parse_percent_attr(margin_all_percent_attr, disp_w, &mx);
            (void)parse_percent_attr(margin_all_percent_attr, disp_h, &my);
            st->cur.margin_left = mx;
            st->cur.margin_right = mx;
            st->cur.margin_top = my;
            st->cur.margin_bottom = my;
        }
        if (margin_x_percent_attr != NULL && *margin_x_percent_attr != 0) {
            int m = 0;
            if (parse_percent_attr(margin_x_percent_attr, disp_w, &m)) {
                st->cur.margin_left = m;
                st->cur.margin_right = m;
            }
        }
        if (margin_y_percent_attr != NULL && *margin_y_percent_attr != 0) {
            int m = 0;
            if (parse_percent_attr(margin_y_percent_attr, disp_h, &m)) {
                st->cur.margin_top = m;
                st->cur.margin_bottom = m;
            }
        }
        (void)parse_percent_attr(margin_top_percent_attr, disp_h, &st->cur.margin_top);
        (void)parse_percent_attr(margin_right_percent_attr, disp_w, &st->cur.margin_right);
        (void)parse_percent_attr(margin_bottom_percent_attr, disp_h, &st->cur.margin_bottom);
        (void)parse_percent_attr(margin_left_percent_attr, disp_w, &st->cur.margin_left);

        st->cur.padding_top = scale_size_uniform(to_i(padding_all_attr, 0), disp_w, disp_h);
        st->cur.padding_right = st->cur.padding_top;
        st->cur.padding_bottom = st->cur.padding_top;
        st->cur.padding_left = st->cur.padding_top;
        if (padding_x_attr != NULL && *padding_x_attr != 0) {
            int p = scale_size_uniform(to_i(padding_x_attr, 0), disp_w, disp_h);
            st->cur.padding_left = p;
            st->cur.padding_right = p;
        }
        if (padding_y_attr != NULL && *padding_y_attr != 0) {
            int p = scale_size_uniform(to_i(padding_y_attr, 0), disp_w, disp_h);
            st->cur.padding_top = p;
            st->cur.padding_bottom = p;
        }
        if (padding_top_attr != NULL && *padding_top_attr != 0) st->cur.padding_top = scale_size_uniform(to_i(padding_top_attr, 0), disp_w, disp_h);
        if (padding_right_attr != NULL && *padding_right_attr != 0) st->cur.padding_right = scale_size_uniform(to_i(padding_right_attr, 0), disp_w, disp_h);
        if (padding_bottom_attr != NULL && *padding_bottom_attr != 0) st->cur.padding_bottom = scale_size_uniform(to_i(padding_bottom_attr, 0), disp_w, disp_h);
        if (padding_left_attr != NULL && *padding_left_attr != 0) st->cur.padding_left = scale_size_uniform(to_i(padding_left_attr, 0), disp_w, disp_h);

        if (padding_all_percent_attr != NULL && *padding_all_percent_attr != 0) {
            int px = 0;
            int py = 0;
            (void)parse_percent_attr(padding_all_percent_attr, disp_w, &px);
            (void)parse_percent_attr(padding_all_percent_attr, disp_h, &py);
            st->cur.padding_left = px;
            st->cur.padding_right = px;
            st->cur.padding_top = py;
            st->cur.padding_bottom = py;
        }
        if (padding_x_percent_attr != NULL && *padding_x_percent_attr != 0) {
            int p = 0;
            if (parse_percent_attr(padding_x_percent_attr, disp_w, &p)) {
                st->cur.padding_left = p;
                st->cur.padding_right = p;
            }
        }
        if (padding_y_percent_attr != NULL && *padding_y_percent_attr != 0) {
            int p = 0;
            if (parse_percent_attr(padding_y_percent_attr, disp_h, &p)) {
                st->cur.padding_top = p;
                st->cur.padding_bottom = p;
            }
        }
        (void)parse_percent_attr(padding_top_percent_attr, disp_h, &st->cur.padding_top);
        (void)parse_percent_attr(padding_right_percent_attr, disp_w, &st->cur.padding_right);
        (void)parse_percent_attr(padding_bottom_percent_attr, disp_h, &st->cur.padding_bottom);
        (void)parse_percent_attr(padding_left_percent_attr, disp_w, &st->cur.padding_left);

        st->cur.margin_top = clamp_non_negative(st->cur.margin_top);
        st->cur.margin_right = clamp_non_negative(st->cur.margin_right);
        st->cur.margin_bottom = clamp_non_negative(st->cur.margin_bottom);
        st->cur.margin_left = clamp_non_negative(st->cur.margin_left);
        st->cur.padding_top = clamp_non_negative(st->cur.padding_top);
        st->cur.padding_right = clamp_non_negative(st->cur.padding_right);
        st->cur.padding_bottom = clamp_non_negative(st->cur.padding_bottom);
        st->cur.padding_left = clamp_non_negative(st->cur.padding_left);
        if (st->cur.radius < 0) {
            st->cur.radius = 0;
        }

        st->cur.x += st->cur.margin_left;
        st->cur.y += st->cur.margin_top;
        st->cur.w -= (st->cur.margin_left + st->cur.margin_right);
        st->cur.h -= (st->cur.margin_top + st->cur.margin_bottom);
        if (st->cur.w < 1) st->cur.w = 1;
        if (st->cur.h < 1) st->cur.h = 1;

        parse_keys(key_attr, st->cur.keys, sizeof(st->cur.keys));
        st->cur.key = st->cur.keys[0];

        st->cur.has_target_html_screen = to_html_screen(target_html_screen, &st->cur.target_html_screen);

        if (st->cur.is_card) {
            draw_card_block(st->ctx, &st->cur);
        } else if (!st->cur.is_button && !st->cur.is_progress && !st->cur.is_icon && !tag_is_label) {
            draw_background_block(st->ctx, &st->cur);
        }

        if (st->cur.is_progress) {
            draw_progress(st->ctx, &st->cur);
        }

        if (st->cur.is_icon) {
            draw_icon(st->ctx, &st->cur);
        }

        if (st->cur.is_button) {
            /* Buttons are both drawn and registered in the action map. */
            st->cur.tag = to_tag(tag_attr, st->ctx->next_auto_tag++);
            save_action(st);
        }

        if (st->cur.is_row || st->cur.is_column) {
            int lx = 0;
            int ly = 0;
            int lw = 0;
            int lh = 0;

            get_content_rect(&st->cur, &lx, &ly, &lw, &lh);
            if (lw < 1) {
                lw = 1;
            }
            if (lh < 1) {
                lh = 1;
            }

            layout_push(st, st->cur.is_row ? LAYOUT_ROW : LAYOUT_COLUMN, lx, ly, lw, lh);
        }
        break;
    }

    case HTML_EVT_TEXT:
        if (!st->cur.in_elem) {
            break;
        }

        /* Keep text rendering isolated to text-capable elements only. */
        if (st->cur.is_card || st->cur.is_progress || st->cur.is_icon || st->cur.is_row || st->cur.is_column) {
            break;
        }

        if (st->cur.is_button) {
            draw_html_button(st->ctx, &st->cur, evt->text ? evt->text : "");
        } else {
            draw_text(st->ctx, &st->cur, evt->text ? evt->text : "");
        }
        break;

    case HTML_EVT_END_TAG:
        if (evt->tag != NULL &&
            (strcmp(evt->tag, HTML_TAG_DIV) == 0 ||
             strcmp(evt->tag, HTML_TAG_BUTTON) == 0 ||
             strcmp(evt->tag, HTML_TAG_LABEL) == 0 ||
             strcmp(evt->tag, HTML_TAG_CARD) == 0 ||
             strcmp(evt->tag, HTML_TAG_ICON) == 0 ||
             strcmp(evt->tag, HTML_TAG_PROGRESS) == 0 ||
             strcmp(evt->tag, HTML_TAG_ROW) == 0 ||
             strcmp(evt->tag, HTML_TAG_COLUMN) == 0)) {
            if (strcmp(evt->tag, HTML_TAG_ROW) == 0 || strcmp(evt->tag, HTML_TAG_COLUMN) == 0) {
                layout_pop(st);
            }
            memset(&st->cur, 0, sizeof(st->cur));
        }
        break;

    case HTML_EVT_SELF_CLOSING_TAG:
    default:
        break;
    }
}

/* Initialize HTML UI context for a new render cycle/session. */
void html_ui_init(html_ui_context_t *ctx, void *phost, uint8_t first_auto_tag)
{
    html_ui_init_with_platform(ctx,
                               phost,
                               html_ui_default_platform(),
                               NULL,
                               html_ui_default_style(),
                               first_auto_tag);
}

void html_ui_init_with_platform(html_ui_context_t *ctx,
                                void *render_ctx,
                                const html_ui_platform_t *platform,
                                void *platform_user,
                                const html_ui_style_t *style,
                                uint8_t first_auto_tag)
{
    if (ctx == NULL) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->render_ctx = render_ctx;
    ctx->platform_user = platform_user;
    ctx->platform = platform != NULL ? platform : html_ui_default_platform();
    ctx->style = *(style != NULL ? style : html_ui_default_style());
    ctx->next_auto_tag = first_auto_tag == 0 ? 1 : first_auto_tag;
    ctx->current_screen = HTML_UI_SCREEN_STARTUP_HOME;
    html_ui_run_smoke_tests_once();
}

void html_ui_apply_style(html_ui_context_t *ctx, const html_ui_style_t *style)
{
    if (ctx == NULL || style == NULL) {
        return;
    }

    ctx->style = *style;
}

/* Update active HTML screen tracked in package context. */
void html_ui_set_screen(html_ui_context_t *ctx, html_ui_screen_t screen)
{
    if (ctx == NULL) {
        return;
    }

    if (screen < 0 || screen >= _HTML_UI_SCREEN_COUNT) {
        return;
    }

    ctx->current_screen = screen;
}

/* Return active HTML screen with a safe default when context is null. */
html_ui_screen_t html_ui_get_screen(const html_ui_context_t *ctx)
{
    if (ctx == NULL) {
        return HTML_UI_SCREEN_STARTUP_HOME;
    }

    return ctx->current_screen;
}

/* Clear action table before rendering a new template. */
void html_ui_reset_actions(html_ui_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->action_count = 0;
    memset(ctx->actions, 0, sizeof(ctx->actions));
}

/* Parse and render a template in one pass, rebuilding action bindings. */
void html_ui_render(html_ui_context_t *ctx, const char *html)
{
    parser_state_t st;

    if (ctx == NULL || html == NULL || *html == 0) {
        return;
    }

    memset(&st, 0, sizeof(st));
    st.ctx = ctx;
    html_ui_reset_actions(ctx);
    html_ui_validate_template(html);

    LOG_DBG( "HTML_UI: render start auto_tag=%u", (unsigned)ctx->next_auto_tag);

    html_parse_lite(html, on_evt, &st);

    LOG_DBG(
                 "HTML_UI: render done actions=%u next_auto_tag=%u",
                 (unsigned)ctx->action_count,
                 (unsigned)ctx->next_auto_tag);
}

/* Dispatch matched action to handler or apply default html screen transition. */
static bool dispatch_action(html_ui_context_t *ctx, const html_ui_action_t *action, html_ui_action_handler_t handler, void *user)
{
    LOG_DBG(
                 "HTML_UI: dispatch id=%s action=%s tag=%u keys=%s key=%c(%d) has_target_html=%d target_html_screen=%d",
                 action->id,
                 action->action,
                 (unsigned)action->tag,
                 action->keys[0] ? action->keys : "-",
                 action->key ? action->key : '-',
                 (int)action->key,
                 action->has_target_html_screen ? 1 : 0,
                 (int)action->target_html_screen);

    if (handler != NULL) {
        bool handled = handler(action, user);
        LOG_DBG(
                     "HTML_UI: handler result=%d",
                     handled ? 1 : 0);
        return handled;
    }

    if (action->has_target_html_screen && ctx != NULL) {
        ctx->current_screen = action->target_html_screen;
        LOG_DBG(
                     "HTML_UI: default html screen transition -> %d",
                     (int)ctx->current_screen);
        return true;
    }

    LOG_DBG( "HTML_UI: action not handled");
    return false;
}

/* Resolve keypad/touch events against current action map and dispatch matches. */
bool html_ui_handle_input(html_ui_context_t *ctx, char key, html_ui_action_handler_t handler, void *user)
{
    size_t i;
    uint8_t tag = 0;
    uint32_t wait_start_ms;

    if (ctx == NULL || ctx->render_ctx == NULL) {
        LOG_WRN( "HTML_UI: handle_input skipped (ctx/render null)");
        return false;
    }

    /* Key events have priority over touch to match existing keypad-driven flow. */
    if (key != 0) {
        LOG_DBG(
                     "HTML_UI: keypad key=%c(%d) actions=%u current_html_screen=%d",
                     key,
                     (int)key,
                     (unsigned)ctx->action_count,
                     (int)ctx->current_screen);
        for (i = 0; i < ctx->action_count; i++) {
            if (action_has_key(&ctx->actions[i], key)) {
                LOG_DBG(
                             "HTML_UI: key matched action[%u] id=%s keys=%s",
                             (unsigned)i,
                             ctx->actions[i].id,
                             ctx->actions[i].keys[0] ? ctx->actions[i].keys : "-");
                return dispatch_action(ctx, &ctx->actions[i], handler, user);
            }
        }
        LOG_DBG(
                 "HTML_UI: key did not match any action current_html_screen=%d",
                 (int)ctx->current_screen);
    }

    /* Touch fallback path when no matching key event is present. */
    tag = html_ui_read_touch_tag(ctx);
    if (tag == 0) {
        return false;
    }

    LOG_DBG(
                 "HTML_UI: touch tag=%u actions=%u current_html_screen=%d",
                 (unsigned)tag,
                 (unsigned)ctx->action_count,
                 (int)ctx->current_screen);

    wait_start_ms = k_uptime_get_32();
    while (html_ui_read_touch_tag(ctx) == tag) {
        if ((k_uptime_get_32() - wait_start_ms) > HTML_UI_TOUCH_RELEASE_TIMEOUT_MS) {
            LOG_WRN( "HTML_UI: touch release timeout tag=%u", (unsigned)tag);
            break;
        }
    }

    for (i = 0; i < ctx->action_count; i++) {
        if (ctx->actions[i].tag == tag) {
            LOG_DBG(
                         "HTML_UI: touch matched action[%u] id=%s",
                         (unsigned)i,
                         ctx->actions[i].id);
            return dispatch_action(ctx, &ctx->actions[i], handler, user);
        }
    }

    LOG_DBG(
                 "HTML_UI: touch tag=%u not mapped current_html_screen=%d",
                 (unsigned)tag,
                 (int)ctx->current_screen);
    return false;
}
